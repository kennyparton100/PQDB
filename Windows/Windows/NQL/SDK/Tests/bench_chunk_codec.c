#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Core/World/Blocks/sdk_block.h"
#include "../Core/World/Chunks/ChunkCompression/sdk_chunk_codec.h"
#include "../Core/World/Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../Core/World/Chunks/sdk_chunk.h"
#include "../Core/World/Persistence/sdk_chunk_save_json.h"
#include "../Renderer/d3d12_renderer.h"

#undef assert
#define assert(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            exit(1); \
        } \
    } while (0)

bool sdk_frustum_contains_aabb(const SdkFrustum* frustum,
                               float min_x,
                               float min_y,
                               float min_z,
                               float max_x,
                               float max_y,
                               float max_z)
{
    (void)frustum;
    (void)min_x;
    (void)min_y;
    (void)min_z;
    (void)max_x;
    (void)max_y;
    (void)max_z;
    return true;
}

void sdk_renderer_free_chunk_unified_buffer(SdkChunk* chunk)
{
    (void)chunk;
}

void sdk_renderer_free_chunk_mesh(SdkChunk* chunk)
{
    (void)chunk;
}

SdkChunk* sdk_chunk_manager_get_chunk(SdkChunkManager* cm, int cx, int cz)
{
    (void)cm;
    (void)cx;
    (void)cz;
    return NULL;
}

typedef struct {
    int method_id;
    int decode_cost;
} BenchCodecMethod;

static void fill_test_chunk(SdkChunk* chunk)
{
    int x;
    int y;
    int z;

    assert(chunk && chunk->blocks);
    memset(chunk->blocks, 0, sizeof(*chunk->blocks) * CHUNK_TOTAL_BLOCKS);

    for (y = 0; y < 64; ++y) {
        for (z = 0; z < CHUNK_DEPTH; ++z) {
            for (x = 0; x < CHUNK_WIDTH; ++x) {
                sdk_chunk_set_cell_code_raw(chunk, x, y, z, sdk_world_cell_encode_full_block(BLOCK_STONE));
            }
        }
    }

    for (y = 64; y < 72; ++y) {
        for (z = 0; z < CHUNK_DEPTH; ++z) {
            for (x = 0; x < CHUNK_WIDTH; ++x) {
                sdk_chunk_set_cell_code_raw(chunk, x, y, z, sdk_world_cell_encode_full_block(BLOCK_DIRT));
            }
        }
    }

    for (x = 8; x < 56; ++x) {
        for (z = 8; z < 56; ++z) {
            for (y = 72; y < 80; ++y) {
                sdk_chunk_set_cell_code_raw(chunk, x, y, z, sdk_world_cell_encode_full_block(BLOCK_STONE_BRICKS));
            }
        }
    }

    for (x = 4; x < CHUNK_WIDTH; x += 7) {
        for (z = 5; z < CHUNK_DEPTH; z += 9) {
            for (y = 80; y < 120; ++y) {
                sdk_chunk_set_cell_code_raw(
                    chunk,
                    x,
                    y,
                    z,
                    sdk_world_cell_encode_inline_construction(BLOCK_TIMBER_DARK, SDK_INLINE_PROFILE_BEAM_Y));
            }
        }
    }

    for (x = 0; x < CHUNK_WIDTH; ++x) {
        int ridge_y = 120 + (x % 11);
        for (z = 0; z < CHUNK_DEPTH; ++z) {
            if (((x + z) % 13) == 0) {
                sdk_chunk_set_cell_code_raw(chunk, x, ridge_y, z, sdk_world_cell_encode_full_block(BLOCK_GRANITE));
            }
        }
    }

    sdk_chunk_set_cell_code_raw(chunk, 1, 121, 1,
                                sdk_world_cell_encode_inline_construction(BLOCK_STONE_BRICKS,
                                                                          SDK_INLINE_PROFILE_HALF_POS_Y));
    sdk_chunk_set_cell_code_raw(chunk, 2, 122, 2,
                                sdk_world_cell_encode_inline_construction(BLOCK_PLANKS,
                                                                          SDK_INLINE_PROFILE_QUARTER_NEG_X));
}

static void assert_chunks_equal(const SdkChunk* expected, const SdkChunk* actual)
{
    assert(expected && actual);
    assert(expected->blocks && actual->blocks);
    assert(memcmp(expected->blocks,
                  actual->blocks,
                  sizeof(*expected->blocks) * CHUNK_TOTAL_BLOCKS) == 0);
}

static void expect_method_roundtrip(const SdkChunk* source, int method_id)
{
    SdkChunk decoded;
    char* payload_b64 = NULL;
    int payload_version = 0;
    int top_y = 0;
    int ok;

    printf("roundtrip %s\n", sdk_chunk_codec_method_name(method_id));
    fflush(stdout);

    sdk_chunk_init(&decoded, source->cx, source->cz, NULL);
    assert(decoded.blocks);

    ok = sdk_chunk_codec_encode_with_method(source,
                                            method_id,
                                            &payload_b64,
                                            &payload_version,
                                            &top_y);
    assert(ok);
    assert(payload_b64);
    assert(payload_version == SDK_CHUNK_CODEC_PAYLOAD_VERSION);
    assert(top_y > 0);

    ok = sdk_chunk_codec_decode(sdk_chunk_codec_method_name(method_id),
                                payload_version,
                                payload_b64,
                                top_y,
                                &decoded);
    assert(ok);
    assert_chunks_equal(source, &decoded);

    free(payload_b64);
    sdk_chunk_free(&decoded);
}

static void expect_auto_selection(const SdkChunk* source)
{
    static const BenchCodecMethod methods[] = {
        { SDK_CHUNK_CODEC_METHOD_CELL_RLE, 1 },
        { COMPRESS_METHOD_SPARSE_COLUMN, 2 },
        { COMPRESS_METHOD_BITPACK, 2 },
        { COMPRESS_METHOD_TEMPLATE, 3 },
        { COMPRESS_METHOD_VOLUME_LAYER, 4 },
        { COMPRESS_METHOD_OCTREE, 5 },
        { COMPRESS_METHOD_RLE_BITMASK, 6 },
        { COMPRESS_METHOD_HIERARCHICAL_RLE, 7 }
    };
    SdkChunkCodecBuffer auto_buffer;
    SdkChunkCodecBuffer candidates[sizeof(methods) / sizeof(methods[0])];
    size_t best_index = (size_t)-1;
    size_t min_size = (size_t)-1;
    size_t threshold_size;
    size_t i;
    int auto_method;

    memset(&auto_buffer, 0, sizeof(auto_buffer));
    memset(candidates, 0, sizeof(candidates));
    printf("auto-select\n");
    fflush(stdout);

    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        if (sdk_chunk_codec_encode_binary_with_method(source, methods[i].method_id, &candidates[i])) {
            if (candidates[i].payload_size < min_size) {
                min_size = candidates[i].payload_size;
            }
        }
    }

    assert(min_size != (size_t)-1);
    threshold_size = min_size + (min_size + 19u) / 20u;

    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        if (!candidates[i].payload) continue;
        if (candidates[i].payload_size > threshold_size) continue;

        if (best_index == (size_t)-1 ||
            methods[i].decode_cost < methods[best_index].decode_cost ||
            (methods[i].decode_cost == methods[best_index].decode_cost &&
             candidates[i].payload_size < candidates[best_index].payload_size)) {
            best_index = i;
        }
    }

    assert(best_index != (size_t)-1);
    assert(sdk_chunk_codec_encode_binary_auto(source, &auto_buffer));
    auto_method = sdk_chunk_codec_method_from_name(auto_buffer.codec_name);
    assert(auto_method == methods[best_index].method_id);

    sdk_chunk_codec_buffer_reset(&auto_buffer);
    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        sdk_chunk_codec_buffer_reset(&candidates[i]);
    }
}

static void expect_json_entry_roundtrip(const SdkChunk* source)
{
    SdkChunk decoded;
    SdkChunkSaveJsonEntry parsed;
    char* codec = NULL;
    char* payload_b64 = NULL;
    char* json = NULL;
    int payload_version = 0;
    int top_y = 0;
    size_t json_size;
    int ok;

    printf("json-entry\n");
    fflush(stdout);

    ok = sdk_chunk_codec_encode_auto(source, &codec, &payload_b64, &payload_version, &top_y);
    assert(ok);
    assert(codec);
    assert(payload_b64);

    json_size = strlen(codec) + strlen(payload_b64) + 256u;
    json = (char*)malloc(json_size);
    assert(json);
    snprintf(json,
             json_size,
             "{\"cx\": %d, \"cz\": %d, \"top_y\": %d, "
             "\"cells\": {\"codec\": \"%s\", \"payload_b64\": \"%s\", \"payload_version\": %d}, "
             "\"fluid\": \"\", \"construction\": \"\"}",
             source->cx,
             source->cz,
             top_y,
             codec,
             payload_b64,
             payload_version);

    sdk_chunk_save_json_entry_init(&parsed);
    assert(sdk_chunk_save_json_parse_entry(json, json + strlen(json), &parsed));
    assert(strcmp(parsed.codec, codec) == 0);
    assert(parsed.payload_version == payload_version);

    sdk_chunk_init(&decoded, source->cx, source->cz, NULL);
    assert(decoded.blocks);
    assert(sdk_chunk_codec_decode(parsed.codec,
                                  parsed.payload_version,
                                  parsed.payload_b64,
                                  parsed.top_y,
                                  &decoded));
    assert_chunks_equal(source, &decoded);

    sdk_chunk_free(&decoded);
    sdk_chunk_save_json_entry_free(&parsed);
    free(json);
    free(codec);
    free(payload_b64);
}

int main(void)
{
    static const int methods[] = {
        SDK_CHUNK_CODEC_METHOD_CELL_RLE,
        COMPRESS_METHOD_VOLUME_LAYER,
        COMPRESS_METHOD_TEMPLATE,
        COMPRESS_METHOD_OCTREE,
        COMPRESS_METHOD_BITPACK,
        COMPRESS_METHOD_SPARSE_COLUMN,
        COMPRESS_METHOD_RLE_BITMASK,
        COMPRESS_METHOD_HIERARCHICAL_RLE
    };
    SdkChunk chunk;
    size_t i;

    sdk_chunk_init(&chunk, 3, -5, NULL);
    assert(chunk.blocks);
    fill_test_chunk(&chunk);

    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        expect_method_roundtrip(&chunk, methods[i]);
    }
    expect_auto_selection(&chunk);
    expect_json_entry_roundtrip(&chunk);

    sdk_chunk_free(&chunk);
    puts("bench_chunk_codec: PASS");
    return 0;
}
