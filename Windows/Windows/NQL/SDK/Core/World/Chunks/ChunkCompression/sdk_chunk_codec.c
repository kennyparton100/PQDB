#include "sdk_chunk_codec.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDK_CHUNK_CODEC_RLE_SEGMENT_HEIGHT 16
#define SDK_CHUNK_CODEC_HIER_SEGMENT_HEIGHT 16

typedef struct {
    uint8_t* data;
    size_t   len;
    size_t   cap;
} SdkCodecByteBuffer;

typedef struct {
    const uint8_t* data;
    size_t         len;
    size_t         pos;
} SdkCodecReader;

typedef struct {
    uint16_t block;
    uint8_t* mask;
    uint32_t mask_size;
    uint8_t  type;
} SdkCodecHierSegment;

typedef struct {
    int method_id;
    int decode_cost;
} SdkCodecCandidateMethod;

static char* codec_strdup(const char* text)
{
    size_t len;
    char* copy;

    if (!text) return NULL;
    len = strlen(text);
    copy = (char*)malloc(len + 1u);
    if (!copy) return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

static int codec_bbuf_reserve(SdkCodecByteBuffer* buffer, size_t extra)
{
    size_t needed;
    size_t new_cap;
    uint8_t* resized;

    if (!buffer) return 0;
    needed = buffer->len + extra;
    if (needed <= buffer->cap) return 1;

    new_cap = buffer->cap ? buffer->cap : 256u;
    while (new_cap < needed) {
        if (new_cap > (SIZE_MAX / 2u)) {
            new_cap = needed;
            break;
        }
        new_cap *= 2u;
    }

    resized = (uint8_t*)realloc(buffer->data, new_cap);
    if (!resized) return 0;
    buffer->data = resized;
    buffer->cap = new_cap;
    return 1;
}

static int codec_bbuf_append_data(SdkCodecByteBuffer* buffer, const void* data, size_t size)
{
    if (!codec_bbuf_reserve(buffer, size)) return 0;
    if (size > 0u && data) {
        memcpy(buffer->data + buffer->len, data, size);
    }
    buffer->len += size;
    return 1;
}

static int codec_bbuf_append_u8(SdkCodecByteBuffer* buffer, uint8_t value)
{
    return codec_bbuf_append_data(buffer, &value, sizeof(value));
}

static int codec_bbuf_append_u16(SdkCodecByteBuffer* buffer, uint16_t value)
{
    uint8_t bytes[2];
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    return codec_bbuf_append_data(buffer, bytes, sizeof(bytes));
}

static int codec_bbuf_append_u32(SdkCodecByteBuffer* buffer, uint32_t value)
{
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFu);
    return codec_bbuf_append_data(buffer, bytes, sizeof(bytes));
}

static int codec_bbuf_append_i32(SdkCodecByteBuffer* buffer, int32_t value)
{
    return codec_bbuf_append_u32(buffer, (uint32_t)value);
}

static int codec_bbuf_append_u64(SdkCodecByteBuffer* buffer, uint64_t value)
{
    uint8_t bytes[8];
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFu);
    bytes[4] = (uint8_t)((value >> 32) & 0xFFu);
    bytes[5] = (uint8_t)((value >> 40) & 0xFFu);
    bytes[6] = (uint8_t)((value >> 48) & 0xFFu);
    bytes[7] = (uint8_t)((value >> 56) & 0xFFu);
    return codec_bbuf_append_data(buffer, bytes, sizeof(bytes));
}

static int codec_bbuf_append_varuint(SdkCodecByteBuffer* buffer, uint32_t value)
{
    do {
        uint8_t byte = (uint8_t)(value & 0x7Fu);
        value >>= 7u;
        if (value != 0u) byte |= 0x80u;
        if (!codec_bbuf_append_u8(buffer, byte)) return 0;
    } while (value != 0u);
    return 1;
}

static int codec_reader_take(SdkCodecReader* reader, void* out_data, size_t size)
{
    if (!reader || reader->pos + size > reader->len) return 0;
    if (size > 0u && out_data) {
        memcpy(out_data, reader->data + reader->pos, size);
    }
    reader->pos += size;
    return 1;
}

static int codec_reader_u8(SdkCodecReader* reader, uint8_t* out_value)
{
    return codec_reader_take(reader, out_value, sizeof(*out_value));
}

static int codec_reader_u16(SdkCodecReader* reader, uint16_t* out_value)
{
    uint8_t bytes[2];
    if (!codec_reader_take(reader, bytes, sizeof(bytes))) return 0;
    *out_value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return 1;
}

static int codec_reader_u32(SdkCodecReader* reader, uint32_t* out_value)
{
    uint8_t bytes[4];
    if (!codec_reader_take(reader, bytes, sizeof(bytes))) return 0;
    *out_value = (uint32_t)bytes[0] |
                 ((uint32_t)bytes[1] << 8) |
                 ((uint32_t)bytes[2] << 16) |
                 ((uint32_t)bytes[3] << 24);
    return 1;
}

static int codec_reader_i32(SdkCodecReader* reader, int32_t* out_value)
{
    uint32_t value = 0u;
    if (!codec_reader_u32(reader, &value)) return 0;
    *out_value = (int32_t)value;
    return 1;
}

static int codec_reader_u64(SdkCodecReader* reader, uint64_t* out_value)
{
    uint8_t bytes[8];
    if (!codec_reader_take(reader, bytes, sizeof(bytes))) return 0;
    *out_value = (uint64_t)bytes[0] |
                 ((uint64_t)bytes[1] << 8) |
                 ((uint64_t)bytes[2] << 16) |
                 ((uint64_t)bytes[3] << 24) |
                 ((uint64_t)bytes[4] << 32) |
                 ((uint64_t)bytes[5] << 40) |
                 ((uint64_t)bytes[6] << 48) |
                 ((uint64_t)bytes[7] << 56);
    return 1;
}

static int codec_reader_varuint(SdkCodecReader* reader, uint32_t* out_value)
{
    uint32_t value = 0u;
    uint32_t shift = 0u;

    while (reader && reader->pos < reader->len && shift < 35u) {
        uint8_t byte = reader->data[reader->pos++];
        value |= ((uint32_t)(byte & 0x7Fu) << shift);
        if ((byte & 0x80u) == 0u) {
            *out_value = value;
            return 1;
        }
        shift += 7u;
    }

    return 0;
}

static void sdk_chunk_zero_blocks(SdkChunk* chunk)
{
    if (!chunk || !chunk->blocks) return;
    memset(chunk->blocks, 0, (size_t)CHUNK_TOTAL_BLOCKS * sizeof(SdkWorldCellCode));
}

static int sdk_chunk_compute_top_y(const SdkChunk* chunk)
{
    int index;
    SdkWorldCellCode air_code;

    if (!chunk || !chunk->blocks) return 0;
    air_code = sdk_world_cell_encode_full_block(BLOCK_AIR);

    for (index = CHUNK_TOTAL_BLOCKS - 1; index >= 0; --index) {
        if (chunk->blocks[index] != air_code) {
            return (index / CHUNK_BLOCKS_PER_LAYER) + 1;
        }
    }
    return 0;
}

static void sdk_chunk_copy_to_raw(const SdkChunk* chunk, RawChunk* raw)
{
    int x;
    int y;
    int z;

    memset(raw, 0, sizeof(*raw));
    raw->cx = chunk ? chunk->cx : 0;
    raw->cz = chunk ? chunk->cz : 0;
    raw->top_y = (uint16_t)sdk_chunk_compute_top_y(chunk);
    if (!chunk || !chunk->blocks) return;

    for (y = 0; y < CHUNK_HEIGHT; ++y) {
        uint32_t layer_base = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER;
        for (z = 0; z < CHUNK_DEPTH; ++z) {
            uint32_t row_base = layer_base + (uint32_t)z * (uint32_t)CHUNK_WIDTH;
            for (x = 0; x < CHUNK_WIDTH; ++x) {
                raw->blocks[x][z][y] = (uint16_t)chunk->blocks[row_base + (uint32_t)x];
            }
        }
    }
}

static void sdk_chunk_copy_from_raw(const RawChunk* raw, SdkChunk* chunk)
{
    int x;
    int y;
    int z;

    if (!chunk || !chunk->blocks || !raw) return;
    sdk_chunk_zero_blocks(chunk);

    for (y = 0; y < CHUNK_HEIGHT; ++y) {
        uint32_t layer_base = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER;
        for (z = 0; z < CHUNK_DEPTH; ++z) {
            uint32_t row_base = layer_base + (uint32_t)z * (uint32_t)CHUNK_WIDTH;
            for (x = 0; x < CHUNK_WIDTH; ++x) {
                chunk->blocks[row_base + (uint32_t)x] = (SdkWorldCellCode)raw->blocks[x][z][y];
            }
        }
    }
}

static RawChunk* sdk_chunk_alloc_raw_copy(const SdkChunk* chunk)
{
    RawChunk* raw = (RawChunk*)calloc(1u, sizeof(RawChunk));
    if (!raw) return NULL;
    sdk_chunk_copy_to_raw(chunk, raw);
    return raw;
}

static RawChunk* sdk_chunk_alloc_raw_empty(void)
{
    return (RawChunk*)calloc(1u, sizeof(RawChunk));
}

static int codec_bits_for_palette_size(uint16_t palette_size)
{
    int bits = 1;
    uint32_t capacity = 2u;

    if (palette_size <= 1u) return 1;
    while (capacity < (uint32_t)palette_size && bits < 16) {
        bits++;
        capacity <<= 1u;
    }
    return bits;
}

static int codec_build_palette(const uint16_t* values,
                               size_t count,
                               uint16_t max_entries,
                               uint16_t* out_palette,
                               uint16_t* out_palette_size)
{
    uint8_t* seen;
    uint16_t palette_size = 0u;
    size_t i;

    if (!out_palette || !out_palette_size) return 0;
    *out_palette_size = 0u;

    seen = (uint8_t*)calloc(65536u, sizeof(uint8_t));
    if (!seen) return 0;

    for (i = 0; i < count; ++i) {
        uint16_t value = values[i];
        if (seen[value]) continue;
        seen[value] = 1u;
        if (palette_size >= max_entries) {
            free(seen);
            return 0;
        }
        out_palette[palette_size++] = value;
    }

    free(seen);
    *out_palette_size = palette_size;
    return 1;
}

static int codec_find_palette_index(const uint16_t* palette, uint16_t palette_size, uint16_t value, uint16_t* out_index)
{
    uint16_t i;
    for (i = 0u; i < palette_size; ++i) {
        if (palette[i] == value) {
            *out_index = i;
            return 1;
        }
    }
    return 0;
}

static int codec_pack_values(const uint16_t* values,
                             size_t count,
                             const uint16_t* palette,
                             uint16_t palette_size,
                             int bits_per_value,
                             SdkCodecByteBuffer* out)
{
    size_t i;

    if (!out) return 0;
    if (bits_per_value == 16 && palette_size == 0u) {
        for (i = 0; i < count; ++i) {
            if (!codec_bbuf_append_u16(out, values[i])) return 0;
        }
        return 1;
    }

    {
        uint64_t bit_buffer = 0u;
        int bits_in_buffer = 0;
        uint32_t mask = (1u << bits_per_value) - 1u;

        for (i = 0; i < count; ++i) {
            uint16_t index = 0u;
            if (!codec_find_palette_index(palette, palette_size, values[i], &index)) return 0;

            bit_buffer = (bit_buffer << bits_per_value) | ((uint64_t)index & (uint64_t)mask);
            bits_in_buffer += bits_per_value;

            while (bits_in_buffer >= 8) {
                uint8_t byte_value;
                bits_in_buffer -= 8;
                byte_value = (uint8_t)((bit_buffer >> bits_in_buffer) & 0xFFu);
                if (!codec_bbuf_append_u8(out, byte_value)) return 0;
            }
        }

        if (bits_in_buffer > 0) {
            uint8_t byte_value = (uint8_t)((bit_buffer << (8 - bits_in_buffer)) & 0xFFu);
            if (!codec_bbuf_append_u8(out, byte_value)) return 0;
        }
    }

    return 1;
}

static int codec_unpack_values(SdkCodecReader* reader,
                               uint16_t* out_values,
                               size_t count,
                               const uint16_t* palette,
                               uint16_t palette_size,
                               int bits_per_value,
                               uint32_t packed_size)
{
    size_t i;

    if (bits_per_value == 16 && palette_size == 0u) {
        for (i = 0; i < count; ++i) {
            if (!codec_reader_u16(reader, &out_values[i])) return 0;
        }
        return 1;
    }

    if (!reader || reader->pos + packed_size > reader->len) return 0;
    {
        const uint8_t* packed = reader->data + reader->pos;
        size_t in_pos = 0u;
        uint64_t bit_buffer = 0u;
        int bits_in_buffer = 0;
        uint32_t mask = (1u << bits_per_value) - 1u;

        for (i = 0; i < count; ++i) {
            while (bits_in_buffer < bits_per_value) {
                if (in_pos >= packed_size) return 0;
                bit_buffer = (bit_buffer << 8) | packed[in_pos++];
                bits_in_buffer += 8;
            }

            bits_in_buffer -= bits_per_value;
            {
                uint16_t index = (uint16_t)((bit_buffer >> bits_in_buffer) & mask);
                if (index >= palette_size) return 0;
                out_values[i] = palette[index];
            }
        }
    }
    reader->pos += packed_size;
    return 1;
}

static int codec_encode_cell_rle(const SdkChunk* chunk, SdkChunkCodecBuffer* out_buffer)
{
    SdkCodecByteBuffer bytes = {0};
    int top_y;
    uint32_t total_blocks;
    uint32_t index;

    if (!chunk || !chunk->blocks || !out_buffer) return 0;
    top_y = sdk_chunk_compute_top_y(chunk);
    total_blocks = (uint32_t)top_y * (uint32_t)CHUNK_BLOCKS_PER_LAYER;

    for (index = 0u; index < total_blocks; ) {
        SdkWorldCellCode code = chunk->blocks[index];
        uint32_t run_len = 1u;
        while (index + run_len < total_blocks && chunk->blocks[index + run_len] == code) {
            run_len++;
        }
        if (!codec_bbuf_append_u16(&bytes, (uint16_t)code) ||
            !codec_bbuf_append_varuint(&bytes, run_len)) {
            free(bytes.data);
            return 0;
        }
        index += run_len;
    }

    out_buffer->method_id = SDK_CHUNK_CODEC_METHOD_CELL_RLE;
    out_buffer->codec_name = sdk_chunk_codec_method_name(SDK_CHUNK_CODEC_METHOD_CELL_RLE);
    out_buffer->payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    out_buffer->top_y = top_y;
    out_buffer->payload = bytes.data;
    out_buffer->payload_size = bytes.len;
    return 1;
}

static int codec_decode_cell_rle(const uint8_t* payload, size_t payload_size, int top_y, SdkChunk* out_chunk)
{
    SdkCodecReader reader;
    uint32_t total_blocks;
    uint32_t write_index = 0u;

    if (!out_chunk || !out_chunk->blocks) return 0;
    if (top_y < 0 || top_y > CHUNK_HEIGHT) return 0;

    sdk_chunk_zero_blocks(out_chunk);
    if (top_y == 0) return 1;

    reader.data = payload;
    reader.len = payload_size;
    reader.pos = 0u;
    total_blocks = (uint32_t)top_y * (uint32_t)CHUNK_BLOCKS_PER_LAYER;

    while (reader.pos < reader.len && write_index < total_blocks) {
        uint16_t code_u16 = 0u;
        uint32_t run_len = 0u;

        if (!codec_reader_u16(&reader, &code_u16) ||
            !codec_reader_varuint(&reader, &run_len)) {
            return 0;
        }
        if (run_len == 0u || write_index + run_len > total_blocks) return 0;

        while (run_len > 0u) {
            out_chunk->blocks[write_index++] = (SdkWorldCellCode)code_u16;
            run_len--;
        }
    }

    return write_index == total_blocks;
}

static int codec_encode_volume_layer(const SdkChunk* chunk, SdkChunkCodecBuffer* out_buffer)
{
    RawChunk* raw = NULL;
    CompressedChunk compressed;
    uint8_t* payload = NULL;
    size_t payload_size = 0u;

    raw = sdk_chunk_alloc_raw_copy(chunk);
    if (!raw) return 0;
    memset(&compressed, 0, sizeof(compressed));

    if (compress_chunk(raw, &compressed) != 0) {
        free(raw);
        return 0;
    }
    if (compress_encode_binary(&compressed, &payload, &payload_size) != 0) {
        compress_chunk_free(&compressed);
        free(raw);
        return 0;
    }
    compress_chunk_free(&compressed);

    out_buffer->method_id = COMPRESS_METHOD_VOLUME_LAYER;
    out_buffer->codec_name = sdk_chunk_codec_method_name(COMPRESS_METHOD_VOLUME_LAYER);
    out_buffer->payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    out_buffer->top_y = raw->top_y;
    out_buffer->payload = payload;
    out_buffer->payload_size = payload_size;
    free(raw);
    return 1;
}

static int codec_decode_volume_layer(const uint8_t* payload, size_t payload_size, SdkChunk* out_chunk)
{
    CompressedChunk compressed;
    RawChunk* raw = NULL;
    int ok;

    memset(&compressed, 0, sizeof(compressed));
    if (compress_decode_binary(payload, payload_size, &compressed) != 0) return 0;
    raw = sdk_chunk_alloc_raw_empty();
    if (!raw) {
        compress_chunk_free(&compressed);
        return 0;
    }
    ok = (decompress_chunk(&compressed, raw) == 0);
    compress_chunk_free(&compressed);
    if (!ok) {
        free(raw);
        return 0;
    }
    sdk_chunk_copy_from_raw(raw, out_chunk);
    free(raw);
    return 1;
}

static int codec_encode_template(const SdkChunk* chunk, SdkChunkCodecBuffer* out_buffer)
{
    RawChunk* raw = NULL;
    TerrainTemplate templates[32];
    int template_count = 0;
    float score = 0.0f;
    int best_index;
    TemplateCompressedChunk compressed;
    SdkCodecByteBuffer bytes = {0};
    uint32_t patch_index;

    raw = sdk_chunk_alloc_raw_copy(chunk);
    if (!raw) return 0;
    template_init_defaults(templates, &template_count);
    best_index = template_find_best_match(raw, templates, template_count, &score);
    if (best_index < 0 || score <= 0.0f) {
        free(raw);
        return 0;
    }

    memset(&compressed, 0, sizeof(compressed));
    if (compress_template(raw, (uint8_t)best_index, &templates[best_index], &compressed) != 0) {
        free(raw);
        return 0;
    }

    if (!codec_bbuf_append_u8(&bytes, compressed.template_id) ||
        !codec_bbuf_append_u16(&bytes, (uint16_t)templates[best_index].layer_count)) {
        free(compressed.patches);
        free(bytes.data);
        free(raw);
        return 0;
    }

    for (patch_index = 0u; patch_index < templates[best_index].layer_count; ++patch_index) {
        if (!codec_bbuf_append_u16(&bytes, templates[best_index].layers[patch_index].block_id) ||
            !codec_bbuf_append_u16(&bytes, templates[best_index].layers[patch_index].y_start) ||
            !codec_bbuf_append_u16(&bytes, templates[best_index].layers[patch_index].y_end)) {
            free(compressed.patches);
            free(bytes.data);
            free(raw);
            return 0;
        }
    }

    if (!codec_bbuf_append_u32(&bytes, compressed.patch_count)) {
        free(compressed.patches);
        free(bytes.data);
        free(raw);
        return 0;
    }

    for (patch_index = 0u; patch_index < compressed.patch_count; ++patch_index) {
        if (!codec_bbuf_append_u16(&bytes, compressed.patches[patch_index].x) ||
            !codec_bbuf_append_u16(&bytes, compressed.patches[patch_index].z) ||
            !codec_bbuf_append_u16(&bytes, compressed.patches[patch_index].y) ||
            !codec_bbuf_append_u16(&bytes, compressed.patches[patch_index].block_id)) {
            free(compressed.patches);
            free(bytes.data);
            free(raw);
            return 0;
        }
    }

    free(compressed.patches);
    out_buffer->method_id = COMPRESS_METHOD_TEMPLATE;
    out_buffer->codec_name = sdk_chunk_codec_method_name(COMPRESS_METHOD_TEMPLATE);
    out_buffer->payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    out_buffer->top_y = raw->top_y;
    out_buffer->payload = bytes.data;
    out_buffer->payload_size = bytes.len;
    free(raw);
    return 1;
}

static int codec_decode_template(const uint8_t* payload, size_t payload_size, SdkChunk* out_chunk)
{
    SdkCodecReader reader;
    TerrainTemplate template_def;
    TemplateCompressedChunk compressed;
    uint16_t layer_count = 0u;
    uint32_t patch_count = 0u;
    uint32_t i;
    RawChunk* raw = NULL;
    int ok;

    reader.data = payload;
    reader.len = payload_size;
    reader.pos = 0u;
    memset(&template_def, 0, sizeof(template_def));
    memset(&compressed, 0, sizeof(compressed));

    if (!codec_reader_u8(&reader, &compressed.template_id) ||
        !codec_reader_u16(&reader, &layer_count)) {
        return 0;
    }
    if (layer_count > 16u) return 0;
    template_def.template_id = compressed.template_id;
    template_def.layer_count = layer_count;
    for (i = 0u; i < layer_count; ++i) {
        if (!codec_reader_u16(&reader, &template_def.layers[i].block_id) ||
            !codec_reader_u16(&reader, &template_def.layers[i].y_start) ||
            !codec_reader_u16(&reader, &template_def.layers[i].y_end)) {
            return 0;
        }
    }

    if (!codec_reader_u32(&reader, &patch_count)) return 0;
    compressed.patch_count = patch_count;
    if (patch_count > 0u) {
        compressed.patches = (Patch*)calloc(patch_count, sizeof(Patch));
        if (!compressed.patches) return 0;
    }

    for (i = 0u; i < patch_count; ++i) {
        if (!codec_reader_u16(&reader, &compressed.patches[i].x) ||
            !codec_reader_u16(&reader, &compressed.patches[i].z) ||
            !codec_reader_u16(&reader, &compressed.patches[i].y) ||
            !codec_reader_u16(&reader, &compressed.patches[i].block_id)) {
            free(compressed.patches);
            return 0;
        }
    }

    raw = sdk_chunk_alloc_raw_empty();
    if (!raw) {
        free(compressed.patches);
        return 0;
    }
    ok = (decompress_template(&compressed, &template_def, raw, 0, 0) == 0);
    free(compressed.patches);
    if (!ok) {
        free(raw);
        return 0;
    }
    sdk_chunk_copy_from_raw(raw, out_chunk);
    free(raw);
    return 1;
}

static int codec_octree_write_node(SdkCodecByteBuffer* bytes, const OctreeNode* node)
{
    uint8_t flags = 0u;

    if (!node) return 0;
    if (node->is_uniform) flags |= 0x01u;
    if (node->raw_data) flags |= 0x02u;
    if (!node->is_uniform && !node->raw_data) flags |= 0x04u;

    if (!codec_bbuf_append_u8(bytes, flags) ||
        !codec_bbuf_append_u16(bytes, node->x) ||
        !codec_bbuf_append_u16(bytes, node->y) ||
        !codec_bbuf_append_u16(bytes, node->z) ||
        !codec_bbuf_append_u16(bytes, node->size_x) ||
        !codec_bbuf_append_u16(bytes, node->size_y) ||
        !codec_bbuf_append_u16(bytes, node->size_z)) {
        return 0;
    }

    if (node->is_uniform) {
        return codec_bbuf_append_u16(bytes, node->block_id);
    }

    if (node->raw_data) {
        uint32_t count = (uint32_t)node->size_x * (uint32_t)node->size_y * (uint32_t)node->size_z;
        uint32_t i;
        if (!codec_bbuf_append_u32(bytes, count)) return 0;
        for (i = 0u; i < count; ++i) {
            if (!codec_bbuf_append_u16(bytes, node->raw_data[i])) return 0;
        }
        return 1;
    }

    {
        int child_index;
        for (child_index = 0; child_index < 8; ++child_index) {
            if (!node->children[child_index]) return 0;
            if (!codec_octree_write_node(bytes, node->children[child_index])) return 0;
        }
    }
    return 1;
}

static OctreeNode* codec_octree_read_node(SdkCodecReader* reader)
{
    OctreeNode* node;
    uint8_t flags = 0u;
    int child_index;

    if (!codec_reader_u8(reader, &flags)) return NULL;

    node = (OctreeNode*)calloc(1u, sizeof(OctreeNode));
    if (!node) return NULL;

    if (!codec_reader_u16(reader, &node->x) ||
        !codec_reader_u16(reader, &node->y) ||
        !codec_reader_u16(reader, &node->z) ||
        !codec_reader_u16(reader, &node->size_x) ||
        !codec_reader_u16(reader, &node->size_y) ||
        !codec_reader_u16(reader, &node->size_z)) {
        free(node);
        return NULL;
    }

    node->is_uniform = (uint8_t)((flags & 0x01u) ? 1u : 0u);

    if ((flags & 0x01u) != 0u) {
        if (!codec_reader_u16(reader, &node->block_id)) {
            free(node);
            return NULL;
        }
        return node;
    }

    if ((flags & 0x02u) != 0u) {
        uint32_t count = 0u;
        uint32_t i;
        if (!codec_reader_u32(reader, &count)) {
            free(node);
            return NULL;
        }
        node->raw_data = (uint16_t*)malloc((size_t)count * sizeof(uint16_t));
        if (!node->raw_data) {
            free(node);
            return NULL;
        }
        for (i = 0u; i < count; ++i) {
            if (!codec_reader_u16(reader, &node->raw_data[i])) {
                free(node->raw_data);
                free(node);
                return NULL;
            }
        }
        return node;
    }

    if ((flags & 0x04u) == 0u) {
        free(node);
        return NULL;
    }

    for (child_index = 0; child_index < 8; ++child_index) {
        node->children[child_index] = codec_octree_read_node(reader);
        if (!node->children[child_index]) {
            OctreeCompressedChunk cleanup;
            memset(&cleanup, 0, sizeof(cleanup));
            cleanup.slice_count = 1;
            cleanup.roots[0] = node;
            octree_free_forest(&cleanup);
            return NULL;
        }
    }
    return node;
}

static int codec_encode_octree(const SdkChunk* chunk, SdkChunkCodecBuffer* out_buffer)
{
    RawChunk* raw = NULL;
    OctreeCompressedChunk octree;
    SdkCodecByteBuffer bytes = {0};
    uint8_t root_count;
    int root_index;

    raw = sdk_chunk_alloc_raw_copy(chunk);
    if (!raw) return 0;
    memset(&octree, 0, sizeof(octree));
    if (octree_build(raw, &octree) != 0) {
        free(raw);
        return 0;
    }

    root_count = octree.slice_count;
    if (!codec_bbuf_append_u8(&bytes, root_count)) {
        octree_free_forest(&octree);
        free(bytes.data);
        free(raw);
        return 0;
    }

    for (root_index = 0; root_index < root_count; ++root_index) {
        if (!codec_octree_write_node(&bytes, octree.roots[root_index])) {
            octree_free_forest(&octree);
            free(bytes.data);
            free(raw);
            return 0;
        }
    }

    octree_free_forest(&octree);
    out_buffer->method_id = COMPRESS_METHOD_OCTREE;
    out_buffer->codec_name = sdk_chunk_codec_method_name(COMPRESS_METHOD_OCTREE);
    out_buffer->payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    out_buffer->top_y = raw->top_y;
    out_buffer->payload = bytes.data;
    out_buffer->payload_size = bytes.len;
    free(raw);
    return 1;
}

static int codec_decode_octree(const uint8_t* payload, size_t payload_size, SdkChunk* out_chunk)
{
    SdkCodecReader reader;
    OctreeCompressedChunk octree;
    RawChunk* raw = NULL;
    uint8_t root_count = 0u;
    int root_index;
    int ok;

    reader.data = payload;
    reader.len = payload_size;
    reader.pos = 0u;
    memset(&octree, 0, sizeof(octree));

    if (!codec_reader_u8(&reader, &root_count)) return 0;
    if (root_count > 16u) return 0;
    octree.slice_count = root_count;

    for (root_index = 0; root_index < root_count; ++root_index) {
        octree.roots[root_index] = codec_octree_read_node(&reader);
        if (!octree.roots[root_index]) {
            octree_free_forest(&octree);
            return 0;
        }
    }

    raw = sdk_chunk_alloc_raw_empty();
    if (!raw) {
        octree_free_forest(&octree);
        return 0;
    }
    ok = (decompress_octree(&octree, raw) == 0);
    octree_free_forest(&octree);
    if (!ok) {
        free(raw);
        return 0;
    }
    sdk_chunk_copy_from_raw(raw, out_chunk);
    free(raw);
    return 1;
}

static int codec_encode_bitpack(const SdkChunk* chunk, SdkChunkCodecBuffer* out_buffer)
{
    uint16_t* values = NULL;
    uint16_t palette[256];
    uint16_t palette_size = 0u;
    size_t count = (size_t)CHUNK_TOTAL_BLOCKS;
    int top_y;
    int bits_per_value;
    SdkCodecByteBuffer payload = {0};
    SdkCodecByteBuffer packed = {0};
    size_t i;
    int ok = 0;

    if (!chunk || !chunk->blocks || !out_buffer) return 0;
    top_y = sdk_chunk_compute_top_y(chunk);
    count = (size_t)top_y * (size_t)CHUNK_BLOCKS_PER_LAYER;

    values = (uint16_t*)malloc((count > 0u ? count : 1u) * sizeof(uint16_t));
    if (!values) return 0;
    for (i = 0u; i < count; ++i) {
        values[i] = (uint16_t)chunk->blocks[i];
    }

    if (count == 0u) {
        bits_per_value = 1;
        palette_size = 0u;
    } else if (codec_build_palette(values, count, 256u, palette, &palette_size)) {
        bits_per_value = codec_bits_for_palette_size(palette_size);
        if (!codec_pack_values(values, count, palette, palette_size, bits_per_value, &packed)) {
            goto cleanup;
        }
    } else {
        bits_per_value = 16;
        palette_size = 0u;
        if (!codec_pack_values(values, count, NULL, 0u, bits_per_value, &packed)) {
            goto cleanup;
        }
    }

    if (!codec_bbuf_append_u8(&payload, (uint8_t)bits_per_value) ||
        !codec_bbuf_append_u16(&payload, palette_size) ||
        !codec_bbuf_append_u32(&payload, (uint32_t)packed.len)) {
        goto cleanup;
    }

    for (i = 0u; i < palette_size; ++i) {
        if (!codec_bbuf_append_u16(&payload, palette[i])) goto cleanup;
    }
    if (!codec_bbuf_append_data(&payload, packed.data, packed.len)) goto cleanup;

    out_buffer->method_id = COMPRESS_METHOD_BITPACK;
    out_buffer->codec_name = sdk_chunk_codec_method_name(COMPRESS_METHOD_BITPACK);
    out_buffer->payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    out_buffer->top_y = top_y;
    out_buffer->payload = payload.data;
    out_buffer->payload_size = payload.len;
    ok = 1;

cleanup:
    free(values);
    free(packed.data);
    if (!ok) free(payload.data);
    return ok;
}

static int codec_decode_bitpack(const uint8_t* payload, size_t payload_size, int top_y, SdkChunk* out_chunk)
{
    SdkCodecReader reader;
    uint8_t bits_per_value = 0u;
    uint16_t palette_size = 0u;
    uint16_t palette[256];
    uint32_t packed_size = 0u;
    uint16_t* values = NULL;
    uint32_t total_blocks;
    uint32_t i;
    int ok = 0;

    if (!out_chunk || !out_chunk->blocks) return 0;
    if (top_y < 0 || top_y > CHUNK_HEIGHT) return 0;

    reader.data = payload;
    reader.len = payload_size;
    reader.pos = 0u;
    sdk_chunk_zero_blocks(out_chunk);
    total_blocks = (uint32_t)top_y * (uint32_t)CHUNK_BLOCKS_PER_LAYER;

    if (!codec_reader_u8(&reader, &bits_per_value) ||
        !codec_reader_u16(&reader, &palette_size) ||
        !codec_reader_u32(&reader, &packed_size)) {
        return 0;
    }
    if (palette_size > 256u) return 0;
    for (i = 0u; i < palette_size; ++i) {
        if (!codec_reader_u16(&reader, &palette[i])) return 0;
    }

    values = (uint16_t*)malloc((total_blocks > 0u ? total_blocks : 1u) * sizeof(uint16_t));
    if (!values) return 0;

    if (total_blocks == 0u) {
        ok = 1;
    } else if (!codec_unpack_values(&reader, values, total_blocks, palette, palette_size, bits_per_value, packed_size)) {
        ok = 0;
    } else {
        for (i = 0u; i < total_blocks; ++i) {
            out_chunk->blocks[i] = (SdkWorldCellCode)values[i];
        }
        ok = 1;
    }

    free(values);
    return ok;
}

static int codec_encode_sparse_column(const SdkChunk* chunk, SdkChunkCodecBuffer* out_buffer)
{
    SdkCodecByteBuffer bytes = {0};
    int top_y;
    uint32_t total_stored_blocks = 0u;
    uint16_t heights[CHUNK_WIDTH][CHUNK_DEPTH];
    uint8_t has_column[CHUNK_WIDTH][CHUNK_DEPTH];
    int x;
    int z;
    int y;

    if (!chunk || !chunk->blocks || !out_buffer) return 0;
    memset(heights, 0, sizeof(heights));
    memset(has_column, 0, sizeof(has_column));
    top_y = sdk_chunk_compute_top_y(chunk);

    for (z = 0; z < CHUNK_DEPTH; ++z) {
        for (x = 0; x < CHUNK_WIDTH; ++x) {
            int surface_y = -1;
            for (y = top_y - 1; y >= 0; --y) {
                uint32_t idx = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER +
                               (uint32_t)z * (uint32_t)CHUNK_WIDTH +
                               (uint32_t)x;
                if (chunk->blocks[idx] != 0u) {
                    surface_y = y;
                    break;
                }
            }
            if (surface_y >= 0) {
                uint16_t height = (uint16_t)(surface_y + 1);
                heights[x][z] = height;
                has_column[x][z] = 1u;
                total_stored_blocks += height;
            }
        }
    }

    if (!codec_bbuf_append_u32(&bytes, total_stored_blocks)) {
        free(bytes.data);
        return 0;
    }

    for (z = 0; z < CHUNK_DEPTH; ++z) {
        for (x = 0; x < CHUNK_WIDTH; ++x) {
            if (!codec_bbuf_append_u16(&bytes, heights[x][z])) {
                free(bytes.data);
                return 0;
            }
        }
    }
    for (z = 0; z < CHUNK_DEPTH; ++z) {
        for (x = 0; x < CHUNK_WIDTH; ++x) {
            if (!codec_bbuf_append_u8(&bytes, has_column[x][z])) {
                free(bytes.data);
                return 0;
            }
        }
    }

    for (z = 0; z < CHUNK_DEPTH; ++z) {
        for (x = 0; x < CHUNK_WIDTH; ++x) {
            if (!has_column[x][z]) continue;
            if (!codec_bbuf_append_u16(&bytes, heights[x][z])) {
                free(bytes.data);
                return 0;
            }
            for (y = 0; y < heights[x][z]; ++y) {
                uint32_t idx = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER +
                               (uint32_t)z * (uint32_t)CHUNK_WIDTH +
                               (uint32_t)x;
                if (!codec_bbuf_append_u16(&bytes, (uint16_t)chunk->blocks[idx])) {
                    free(bytes.data);
                    return 0;
                }
            }
        }
    }

    out_buffer->method_id = COMPRESS_METHOD_SPARSE_COLUMN;
    out_buffer->codec_name = sdk_chunk_codec_method_name(COMPRESS_METHOD_SPARSE_COLUMN);
    out_buffer->payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    out_buffer->top_y = top_y;
    out_buffer->payload = bytes.data;
    out_buffer->payload_size = bytes.len;
    return 1;
}

static int codec_decode_sparse_column(const uint8_t* payload, size_t payload_size, SdkChunk* out_chunk)
{
    SdkCodecReader reader;
    uint32_t total_stored_blocks = 0u;
    uint16_t heights[CHUNK_WIDTH][CHUNK_DEPTH];
    uint8_t has_column[CHUNK_WIDTH][CHUNK_DEPTH];
    int x;
    int z;
    int y;

    if (!out_chunk || !out_chunk->blocks) return 0;
    sdk_chunk_zero_blocks(out_chunk);

    reader.data = payload;
    reader.len = payload_size;
    reader.pos = 0u;

    if (!codec_reader_u32(&reader, &total_stored_blocks)) return 0;
    (void)total_stored_blocks;

    for (z = 0; z < CHUNK_DEPTH; ++z) {
        for (x = 0; x < CHUNK_WIDTH; ++x) {
            if (!codec_reader_u16(&reader, &heights[x][z])) return 0;
        }
    }
    for (z = 0; z < CHUNK_DEPTH; ++z) {
        for (x = 0; x < CHUNK_WIDTH; ++x) {
            if (!codec_reader_u8(&reader, &has_column[x][z])) return 0;
        }
    }

    for (z = 0; z < CHUNK_DEPTH; ++z) {
        for (x = 0; x < CHUNK_WIDTH; ++x) {
            uint16_t stored_height;
            if (!has_column[x][z]) continue;
            if (!codec_reader_u16(&reader, &stored_height)) return 0;
            if (stored_height != heights[x][z] || stored_height > CHUNK_HEIGHT) return 0;
            for (y = 0; y < stored_height; ++y) {
                uint16_t code = 0u;
                uint32_t idx;
                if (!codec_reader_u16(&reader, &code)) return 0;
                idx = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER +
                      (uint32_t)z * (uint32_t)CHUNK_WIDTH +
                      (uint32_t)x;
                out_chunk->blocks[idx] = (SdkWorldCellCode)code;
            }
        }
    }

    return 1;
}

static int codec_collect_non_air_codes(const SdkChunk* chunk,
                                       int y_start,
                                       int y_end,
                                       uint8_t* out_mask,
                                       uint32_t mask_size,
                                       uint16_t* out_values,
                                       uint32_t* io_value_count)
{
    uint32_t bit_pos = 0u;
    int x;
    int z;
    int y;
    uint32_t value_count = io_value_count ? *io_value_count : 0u;

    if (!chunk || !out_mask || !out_values || !io_value_count) return 0;
    memset(out_mask, 0, mask_size);

    for (x = 0; x < CHUNK_WIDTH; ++x) {
        for (z = 0; z < CHUNK_DEPTH; ++z) {
            for (y = y_start; y < y_end; ++y) {
                uint32_t idx = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER +
                               (uint32_t)z * (uint32_t)CHUNK_WIDTH +
                               (uint32_t)x;
                SdkWorldCellCode code = chunk->blocks[idx];
                if (code != 0u) {
                    uint32_t byte_pos = bit_pos / 8u;
                    uint8_t bit_offset = (uint8_t)(bit_pos % 8u);
                    out_mask[byte_pos] |= (uint8_t)(1u << bit_offset);
                    out_values[value_count++] = (uint16_t)code;
                }
                bit_pos++;
            }
        }
    }

    *io_value_count = value_count;
    return 1;
}

static int codec_encode_rle_bitmask(const SdkChunk* chunk, SdkChunkCodecBuffer* out_buffer)
{
    const uint32_t num_segments = (uint32_t)CHUNK_HEIGHT / SDK_CHUNK_CODEC_RLE_SEGMENT_HEIGHT;
    const uint32_t mask_size = (uint32_t)(CHUNK_WIDTH * CHUNK_DEPTH * SDK_CHUNK_CODEC_RLE_SEGMENT_HEIGHT) / 8u;
    uint8_t** masks = NULL;
    uint32_t* mask_sizes = NULL;
    uint16_t* values = NULL;
    uint16_t palette[256];
    uint16_t palette_size = 0u;
    uint32_t total_non_air = 0u;
    int bits_per_value = 16;
    SdkCodecByteBuffer packed = {0};
    SdkCodecByteBuffer payload = {0};
    uint32_t seg;
    int top_y;
    int ok = 0;
    size_t i;

    if (!chunk || !chunk->blocks || !out_buffer) return 0;
    top_y = sdk_chunk_compute_top_y(chunk);

    masks = (uint8_t**)calloc(num_segments, sizeof(uint8_t*));
    mask_sizes = (uint32_t*)calloc(num_segments, sizeof(uint32_t));
    values = (uint16_t*)malloc((size_t)CHUNK_TOTAL_BLOCKS * sizeof(uint16_t));
    if (!masks || !mask_sizes || !values) goto cleanup;

    for (seg = 0u; seg < num_segments; ++seg) {
        int y_start = (int)(seg * SDK_CHUNK_CODEC_RLE_SEGMENT_HEIGHT);
        int y_end = y_start + SDK_CHUNK_CODEC_RLE_SEGMENT_HEIGHT;
        masks[seg] = (uint8_t*)calloc(mask_size, sizeof(uint8_t));
        if (!masks[seg]) goto cleanup;
        mask_sizes[seg] = mask_size;
        if (!codec_collect_non_air_codes(chunk, y_start, y_end, masks[seg], mask_size, values, &total_non_air)) {
            goto cleanup;
        }
    }

    if (total_non_air > 0u && codec_build_palette(values, total_non_air, 256u, palette, &palette_size)) {
        bits_per_value = codec_bits_for_palette_size(palette_size);
        if (!codec_pack_values(values, total_non_air, palette, palette_size, bits_per_value, &packed)) goto cleanup;
    } else {
        bits_per_value = 16;
        palette_size = 0u;
        if (!codec_pack_values(values, total_non_air, NULL, 0u, bits_per_value, &packed)) goto cleanup;
    }

    if (!codec_bbuf_append_u32(&payload, num_segments) ||
        !codec_bbuf_append_u16(&payload, palette_size) ||
        !codec_bbuf_append_u8(&payload, (uint8_t)bits_per_value) ||
        !codec_bbuf_append_u8(&payload, 0u) ||
        !codec_bbuf_append_u32(&payload, total_non_air) ||
        !codec_bbuf_append_u32(&payload, (uint32_t)packed.len)) {
        goto cleanup;
    }

    for (seg = 0u; seg < num_segments; ++seg) {
        if (!codec_bbuf_append_u32(&payload, mask_sizes[seg])) goto cleanup;
    }
    for (seg = 0u; seg < num_segments; ++seg) {
        if (!codec_bbuf_append_data(&payload, masks[seg], mask_sizes[seg])) goto cleanup;
    }
    for (i = 0u; i < palette_size; ++i) {
        if (!codec_bbuf_append_u16(&payload, palette[i])) goto cleanup;
    }
    if (!codec_bbuf_append_data(&payload, packed.data, packed.len)) goto cleanup;

    out_buffer->method_id = COMPRESS_METHOD_RLE_BITMASK;
    out_buffer->codec_name = sdk_chunk_codec_method_name(COMPRESS_METHOD_RLE_BITMASK);
    out_buffer->payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    out_buffer->top_y = top_y;
    out_buffer->payload = payload.data;
    out_buffer->payload_size = payload.len;
    ok = 1;

cleanup:
    if (!ok) free(payload.data);
    free(values);
    free(mask_sizes);
    if (masks) {
        for (seg = 0u; seg < num_segments; ++seg) free(masks[seg]);
    }
    free(masks);
    free(packed.data);
    return ok;
}

static int codec_decode_rle_bitmask(const uint8_t* payload, size_t payload_size, SdkChunk* out_chunk)
{
    SdkCodecReader reader;
    uint32_t num_segments = 0u;
    uint16_t palette_size = 0u;
    uint8_t bits_per_value = 0u;
    uint8_t reserved = 0u;
    uint32_t total_non_air = 0u;
    uint32_t packed_size = 0u;
    uint16_t palette[256];
    uint32_t* mask_sizes = NULL;
    uint8_t** masks = NULL;
    uint16_t* values = NULL;
    uint32_t value_pos = 0u;
    uint32_t seg;
    uint32_t i;
    int x;
    int z;
    int y;
    int ok = 0;

    if (!out_chunk || !out_chunk->blocks) return 0;
    sdk_chunk_zero_blocks(out_chunk);

    reader.data = payload;
    reader.len = payload_size;
    reader.pos = 0u;

    if (!codec_reader_u32(&reader, &num_segments) ||
        !codec_reader_u16(&reader, &palette_size) ||
        !codec_reader_u8(&reader, &bits_per_value) ||
        !codec_reader_u8(&reader, &reserved) ||
        !codec_reader_u32(&reader, &total_non_air) ||
        !codec_reader_u32(&reader, &packed_size)) {
        return 0;
    }
    (void)reserved;
    if (palette_size > 256u) return 0;

    mask_sizes = (uint32_t*)calloc(num_segments ? num_segments : 1u, sizeof(uint32_t));
    masks = (uint8_t**)calloc(num_segments ? num_segments : 1u, sizeof(uint8_t*));
    values = (uint16_t*)malloc((total_non_air > 0u ? total_non_air : 1u) * sizeof(uint16_t));
    if (!mask_sizes || !masks || !values) goto cleanup;

    for (seg = 0u; seg < num_segments; ++seg) {
        if (!codec_reader_u32(&reader, &mask_sizes[seg])) goto cleanup;
    }
    for (seg = 0u; seg < num_segments; ++seg) {
        masks[seg] = (uint8_t*)malloc(mask_sizes[seg] > 0u ? mask_sizes[seg] : 1u);
        if (!masks[seg]) goto cleanup;
        if (!codec_reader_take(&reader, masks[seg], mask_sizes[seg])) goto cleanup;
    }
    for (i = 0u; i < palette_size; ++i) {
        if (!codec_reader_u16(&reader, &palette[i])) goto cleanup;
    }
    if (!codec_unpack_values(&reader, values, total_non_air, palette, palette_size, bits_per_value, packed_size)) {
        goto cleanup;
    }

    for (seg = 0u; seg < num_segments; ++seg) {
        uint32_t bit_pos = 0u;
        int y_start = (int)(seg * SDK_CHUNK_CODEC_RLE_SEGMENT_HEIGHT);
        int y_end = y_start + SDK_CHUNK_CODEC_RLE_SEGMENT_HEIGHT;
        for (x = 0; x < CHUNK_WIDTH; ++x) {
            for (z = 0; z < CHUNK_DEPTH; ++z) {
                for (y = y_start; y < y_end; ++y) {
                    uint32_t byte_pos = bit_pos / 8u;
                    uint8_t bit_offset = (uint8_t)(bit_pos % 8u);
                    if ((masks[seg][byte_pos] & (uint8_t)(1u << bit_offset)) != 0u) {
                        uint32_t idx;
                        if (value_pos >= total_non_air) goto cleanup;
                        idx = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER +
                              (uint32_t)z * (uint32_t)CHUNK_WIDTH +
                              (uint32_t)x;
                        out_chunk->blocks[idx] = (SdkWorldCellCode)values[value_pos++];
                    }
                    bit_pos++;
                }
            }
        }
    }

    ok = (value_pos == total_non_air);

cleanup:
    if (masks) {
        for (seg = 0u; seg < num_segments; ++seg) free(masks[seg]);
    }
    free(masks);
    free(mask_sizes);
    free(values);
    return ok;
}

static int codec_encode_hierarchical_rle(const SdkChunk* chunk, SdkChunkCodecBuffer* out_buffer)
{
    const uint32_t num_segments = (uint32_t)CHUNK_HEIGHT / SDK_CHUNK_CODEC_HIER_SEGMENT_HEIGHT;
    const uint32_t mask_size = (uint32_t)(CHUNK_WIDTH * CHUNK_DEPTH * SDK_CHUNK_CODEC_HIER_SEGMENT_HEIGHT) / 8u;
    SdkCodecHierSegment segments[CHUNK_SUBCHUNK_COUNT * 4];
    uint16_t* values = NULL;
    uint16_t palette[256];
    uint16_t palette_size = 0u;
    uint32_t total_non_air = 0u;
    uint64_t segment_summary = 0u;
    int bits_per_value = 16;
    SdkCodecByteBuffer packed = {0};
    SdkCodecByteBuffer payload = {0};
    uint32_t seg;
    int top_y;
    int ok = 0;
    size_t i;

    if (!chunk || !chunk->blocks || !out_buffer) return 0;
    memset(segments, 0, sizeof(segments));
    top_y = sdk_chunk_compute_top_y(chunk);
    values = (uint16_t*)malloc((size_t)CHUNK_TOTAL_BLOCKS * sizeof(uint16_t));
    if (!values) return 0;

    for (seg = 0u; seg < num_segments; ++seg) {
        int y_start = (int)(seg * SDK_CHUNK_CODEC_HIER_SEGMENT_HEIGHT);
        int y_end = y_start + SDK_CHUNK_CODEC_HIER_SEGMENT_HEIGHT;
        uint16_t uniform_block = 0u;
        int has_non_air = 0;
        int is_uniform = 1;
        int x;
        int z;
        int y;
        int first = 1;

        for (x = 0; x < CHUNK_WIDTH && is_uniform; ++x) {
            for (z = 0; z < CHUNK_DEPTH && is_uniform; ++z) {
                for (y = y_start; y < y_end && is_uniform; ++y) {
                    uint32_t idx = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER +
                                   (uint32_t)z * (uint32_t)CHUNK_WIDTH +
                                   (uint32_t)x;
                    uint16_t code = (uint16_t)chunk->blocks[idx];
                    if (code != 0u) has_non_air = 1;
                    if (first) {
                        uniform_block = code;
                        first = 0;
                    } else if (code != uniform_block) {
                        is_uniform = 0;
                    }
                }
            }
        }

        if (!has_non_air && uniform_block == 0u) {
            segments[seg].type = SEG_TYPE_ALL_AIR;
            continue;
        }

        segment_summary |= (1ull << seg);
        if (is_uniform) {
            segments[seg].type = SEG_TYPE_UNIFORM;
            segments[seg].block = uniform_block;
            continue;
        }

        segments[seg].type = SEG_TYPE_MIXED;
        segments[seg].mask = (uint8_t*)calloc(mask_size, sizeof(uint8_t));
        if (!segments[seg].mask) goto cleanup;
        segments[seg].mask_size = mask_size;
        if (!codec_collect_non_air_codes(chunk, y_start, y_end, segments[seg].mask, mask_size, values, &total_non_air)) {
            goto cleanup;
        }
    }

    if (total_non_air > 0u && codec_build_palette(values, total_non_air, 256u, palette, &palette_size)) {
        bits_per_value = codec_bits_for_palette_size(palette_size);
        if (!codec_pack_values(values, total_non_air, palette, palette_size, bits_per_value, &packed)) goto cleanup;
    } else {
        bits_per_value = 16;
        palette_size = 0u;
        if (!codec_pack_values(values, total_non_air, NULL, 0u, bits_per_value, &packed)) goto cleanup;
    }

    if (!codec_bbuf_append_u32(&payload, num_segments) ||
        !codec_bbuf_append_u64(&payload, segment_summary) ||
        !codec_bbuf_append_u16(&payload, palette_size) ||
        !codec_bbuf_append_u8(&payload, (uint8_t)bits_per_value) ||
        !codec_bbuf_append_u8(&payload, 0u) ||
        !codec_bbuf_append_u32(&payload, total_non_air) ||
        !codec_bbuf_append_u32(&payload, (uint32_t)packed.len)) {
        goto cleanup;
    }

    for (seg = 0u; seg < num_segments; ++seg) {
        if ((segment_summary & (1ull << seg)) == 0u) continue;
        if (!codec_bbuf_append_u8(&payload, segments[seg].type)) goto cleanup;
        if (segments[seg].type == SEG_TYPE_UNIFORM) {
            if (!codec_bbuf_append_u16(&payload, segments[seg].block)) goto cleanup;
        } else if (segments[seg].type == SEG_TYPE_MIXED) {
            if (!codec_bbuf_append_u32(&payload, segments[seg].mask_size) ||
                !codec_bbuf_append_data(&payload, segments[seg].mask, segments[seg].mask_size)) {
                goto cleanup;
            }
        }
    }

    for (i = 0u; i < palette_size; ++i) {
        if (!codec_bbuf_append_u16(&payload, palette[i])) goto cleanup;
    }
    if (!codec_bbuf_append_data(&payload, packed.data, packed.len)) goto cleanup;

    out_buffer->method_id = COMPRESS_METHOD_HIERARCHICAL_RLE;
    out_buffer->codec_name = sdk_chunk_codec_method_name(COMPRESS_METHOD_HIERARCHICAL_RLE);
    out_buffer->payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    out_buffer->top_y = top_y;
    out_buffer->payload = payload.data;
    out_buffer->payload_size = payload.len;
    ok = 1;

cleanup:
    for (seg = 0u; seg < num_segments; ++seg) free(segments[seg].mask);
    free(values);
    free(packed.data);
    if (!ok) free(payload.data);
    return ok;
}

static int codec_decode_hierarchical_rle(const uint8_t* payload, size_t payload_size, SdkChunk* out_chunk)
{
    SdkCodecReader reader;
    uint32_t num_segments = 0u;
    uint64_t segment_summary = 0u;
    uint16_t palette_size = 0u;
    uint8_t bits_per_value = 0u;
    uint8_t reserved = 0u;
    uint32_t total_non_air = 0u;
    uint32_t packed_size = 0u;
    uint16_t palette[256];
    SdkCodecHierSegment segments[CHUNK_SUBCHUNK_COUNT * 4];
    uint16_t* values = NULL;
    uint32_t value_pos = 0u;
    uint32_t seg;
    uint32_t i;
    int ok = 0;

    if (!out_chunk || !out_chunk->blocks) return 0;
    memset(segments, 0, sizeof(segments));
    sdk_chunk_zero_blocks(out_chunk);

    reader.data = payload;
    reader.len = payload_size;
    reader.pos = 0u;

    if (!codec_reader_u32(&reader, &num_segments) ||
        !codec_reader_u64(&reader, &segment_summary) ||
        !codec_reader_u16(&reader, &palette_size) ||
        !codec_reader_u8(&reader, &bits_per_value) ||
        !codec_reader_u8(&reader, &reserved) ||
        !codec_reader_u32(&reader, &total_non_air) ||
        !codec_reader_u32(&reader, &packed_size)) {
        return 0;
    }
    (void)reserved;
    if (num_segments > (CHUNK_SUBCHUNK_COUNT * 4u) || palette_size > 256u) return 0;

    for (seg = 0u; seg < num_segments; ++seg) {
        if ((segment_summary & (1ull << seg)) == 0u) {
            segments[seg].type = SEG_TYPE_ALL_AIR;
            continue;
        }
        if (!codec_reader_u8(&reader, &segments[seg].type)) goto cleanup;
        if (segments[seg].type == SEG_TYPE_UNIFORM) {
            if (!codec_reader_u16(&reader, &segments[seg].block)) goto cleanup;
        } else if (segments[seg].type == SEG_TYPE_MIXED) {
            if (!codec_reader_u32(&reader, &segments[seg].mask_size)) goto cleanup;
            segments[seg].mask = (uint8_t*)malloc(segments[seg].mask_size > 0u ? segments[seg].mask_size : 1u);
            if (!segments[seg].mask) goto cleanup;
            if (!codec_reader_take(&reader, segments[seg].mask, segments[seg].mask_size)) goto cleanup;
        } else {
            goto cleanup;
        }
    }

    for (i = 0u; i < palette_size; ++i) {
        if (!codec_reader_u16(&reader, &palette[i])) goto cleanup;
    }

    values = (uint16_t*)malloc((total_non_air > 0u ? total_non_air : 1u) * sizeof(uint16_t));
    if (!values) goto cleanup;
    if (!codec_unpack_values(&reader, values, total_non_air, palette, palette_size, bits_per_value, packed_size)) {
        goto cleanup;
    }

    for (seg = 0u; seg < num_segments; ++seg) {
        int x;
        int z;
        int y;
        int y_start = (int)(seg * SDK_CHUNK_CODEC_HIER_SEGMENT_HEIGHT);
        int y_end = y_start + SDK_CHUNK_CODEC_HIER_SEGMENT_HEIGHT;
        if ((segment_summary & (1ull << seg)) == 0u) continue;

        if (segments[seg].type == SEG_TYPE_UNIFORM) {
            for (x = 0; x < CHUNK_WIDTH; ++x) {
                for (z = 0; z < CHUNK_DEPTH; ++z) {
                    for (y = y_start; y < y_end; ++y) {
                        uint32_t idx = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER +
                                       (uint32_t)z * (uint32_t)CHUNK_WIDTH +
                                       (uint32_t)x;
                        out_chunk->blocks[idx] = (SdkWorldCellCode)segments[seg].block;
                    }
                }
            }
            continue;
        }

        {
            uint32_t bit_pos = 0u;
            for (x = 0; x < CHUNK_WIDTH; ++x) {
                for (z = 0; z < CHUNK_DEPTH; ++z) {
                    for (y = y_start; y < y_end; ++y) {
                        uint32_t byte_pos = bit_pos / 8u;
                        uint8_t bit_offset = (uint8_t)(bit_pos % 8u);
                        if ((segments[seg].mask[byte_pos] & (uint8_t)(1u << bit_offset)) != 0u) {
                            uint32_t idx;
                            if (value_pos >= total_non_air) goto cleanup;
                            idx = (uint32_t)y * (uint32_t)CHUNK_BLOCKS_PER_LAYER +
                                  (uint32_t)z * (uint32_t)CHUNK_WIDTH +
                                  (uint32_t)x;
                            out_chunk->blocks[idx] = (SdkWorldCellCode)values[value_pos++];
                        }
                        bit_pos++;
                    }
                }
            }
        }
    }

    ok = (value_pos == total_non_air);

cleanup:
    for (seg = 0u; seg < num_segments && seg < (CHUNK_SUBCHUNK_COUNT * 4u); ++seg) free(segments[seg].mask);
    free(values);
    return ok;
}

static int codec_decode_inter_chunk_delta(const uint8_t* payload, size_t payload_size, SdkChunk* out_chunk)
{
    SdkCodecReader reader;
    DeltaCompressedChunk delta;
    uint32_t diff_index;
    RawChunk* raw = NULL;
    int ok;

    memset(&delta, 0, sizeof(delta));
    reader.data = payload;
    reader.len = payload_size;
    reader.pos = 0u;

    if (!codec_reader_i32(&reader, &delta.ref_cx) ||
        !codec_reader_i32(&reader, &delta.ref_cz) ||
        !codec_reader_u32(&reader, &delta.ref_hash) ||
        !codec_reader_u32(&reader, &delta.diff_count) ||
        !codec_reader_u8(&reader, &delta.backup_method)) {
        return 0;
    }
    {
        uint32_t backup_size_u32 = 0u;
        if (!codec_reader_u32(&reader, &backup_size_u32)) return 0;
        delta.backup_size = backup_size_u32;
    }

    if (delta.backup_size > 0u) {
        delta.backup_data = (uint8_t*)malloc(delta.backup_size);
        if (!delta.backup_data) {
            delta_free(&delta);
            return 0;
        }
        if (!codec_reader_take(&reader, delta.backup_data, delta.backup_size)) {
            delta_free(&delta);
            return 0;
        }
    }

    if (delta.diff_count > 0u) {
        delta.differences = (Patch*)calloc(delta.diff_count, sizeof(Patch));
        if (!delta.differences) {
            delta_free(&delta);
            return 0;
        }
    }
    for (diff_index = 0u; diff_index < delta.diff_count; ++diff_index) {
        if (!codec_reader_u16(&reader, &delta.differences[diff_index].x) ||
            !codec_reader_u16(&reader, &delta.differences[diff_index].z) ||
            !codec_reader_u16(&reader, &delta.differences[diff_index].y) ||
            !codec_reader_u16(&reader, &delta.differences[diff_index].block_id)) {
            delta_free(&delta);
            return 0;
        }
    }

    raw = sdk_chunk_alloc_raw_empty();
    if (!raw) {
        delta_free(&delta);
        return 0;
    }
    ok = (decompress_delta(&delta, NULL, raw) == 0);
    delta_free(&delta);
    if (!ok) {
        free(raw);
        return 0;
    }
    sdk_chunk_copy_from_raw(raw, out_chunk);
    free(raw);
    return 1;
}

const char* sdk_chunk_codec_method_name(int method_id)
{
    switch (method_id) {
        case SDK_CHUNK_CODEC_METHOD_CELL_RLE: return "cell_rle";
        case COMPRESS_METHOD_VOLUME_LAYER: return "volume_layer";
        case COMPRESS_METHOD_TEMPLATE: return "template";
        case COMPRESS_METHOD_OCTREE: return "octree";
        case COMPRESS_METHOD_INTER_CHUNK_DELTA: return "inter_chunk_delta";
        case COMPRESS_METHOD_BITPACK: return "bitpack";
        case COMPRESS_METHOD_SPARSE_COLUMN: return "sparse_column";
        case COMPRESS_METHOD_RLE_BITMASK: return "rle_bitmask";
        case COMPRESS_METHOD_HIERARCHICAL_RLE: return "hierarchical_rle_v2";
        default: return NULL;
    }
}

int sdk_chunk_codec_method_from_name(const char* codec_name)
{
    if (!codec_name) return -1;
    if (strcmp(codec_name, "cell_rle") == 0) return SDK_CHUNK_CODEC_METHOD_CELL_RLE;
    if (strcmp(codec_name, "volume_layer") == 0) return COMPRESS_METHOD_VOLUME_LAYER;
    if (strcmp(codec_name, "template") == 0) return COMPRESS_METHOD_TEMPLATE;
    if (strcmp(codec_name, "octree") == 0) return COMPRESS_METHOD_OCTREE;
    if (strcmp(codec_name, "inter_chunk_delta") == 0) return COMPRESS_METHOD_INTER_CHUNK_DELTA;
    if (strcmp(codec_name, "bitpack") == 0) return COMPRESS_METHOD_BITPACK;
    if (strcmp(codec_name, "sparse_column") == 0) return COMPRESS_METHOD_SPARSE_COLUMN;
    if (strcmp(codec_name, "rle_bitmask") == 0) return COMPRESS_METHOD_RLE_BITMASK;
    if (strcmp(codec_name, "hierarchical_rle_v2") == 0) return COMPRESS_METHOD_HIERARCHICAL_RLE;
    return -1;
}

int sdk_chunk_codec_method_is_runtime_enabled(int method_id)
{
    switch (method_id) {
        case SDK_CHUNK_CODEC_METHOD_CELL_RLE:
        case COMPRESS_METHOD_VOLUME_LAYER:
        case COMPRESS_METHOD_TEMPLATE:
        case COMPRESS_METHOD_OCTREE:
        case COMPRESS_METHOD_BITPACK:
        case COMPRESS_METHOD_SPARSE_COLUMN:
        case COMPRESS_METHOD_RLE_BITMASK:
        case COMPRESS_METHOD_HIERARCHICAL_RLE:
            return 1;
        default:
            return 0;
    }
}

void sdk_chunk_codec_buffer_reset(SdkChunkCodecBuffer* buffer)
{
    if (!buffer) return;
    free(buffer->payload);
    memset(buffer, 0, sizeof(*buffer));
}

int sdk_chunk_codec_encode_binary_with_method(const SdkChunk* chunk,
                                              int method_id,
                                              SdkChunkCodecBuffer* out_buffer)
{
    if (!out_buffer) return 0;
    sdk_chunk_codec_buffer_reset(out_buffer);

    switch (method_id) {
        case SDK_CHUNK_CODEC_METHOD_CELL_RLE:
            return codec_encode_cell_rle(chunk, out_buffer);
        case COMPRESS_METHOD_VOLUME_LAYER:
            return codec_encode_volume_layer(chunk, out_buffer);
        case COMPRESS_METHOD_TEMPLATE:
            return codec_encode_template(chunk, out_buffer);
        case COMPRESS_METHOD_OCTREE:
            return codec_encode_octree(chunk, out_buffer);
        case COMPRESS_METHOD_BITPACK:
            return codec_encode_bitpack(chunk, out_buffer);
        case COMPRESS_METHOD_SPARSE_COLUMN:
            return codec_encode_sparse_column(chunk, out_buffer);
        case COMPRESS_METHOD_RLE_BITMASK:
            return codec_encode_rle_bitmask(chunk, out_buffer);
        case COMPRESS_METHOD_HIERARCHICAL_RLE:
            return codec_encode_hierarchical_rle(chunk, out_buffer);
        default:
            return 0;
    }
}

int sdk_chunk_codec_encode_binary_auto(const SdkChunk* chunk,
                                       SdkChunkCodecBuffer* out_buffer)
{
    static const SdkCodecCandidateMethod methods[] = {
        { SDK_CHUNK_CODEC_METHOD_CELL_RLE, 1 },
        { COMPRESS_METHOD_SPARSE_COLUMN, 2 },
        { COMPRESS_METHOD_BITPACK, 2 },
        { COMPRESS_METHOD_TEMPLATE, 3 },
        { COMPRESS_METHOD_VOLUME_LAYER, 4 },
        { COMPRESS_METHOD_OCTREE, 5 },
        { COMPRESS_METHOD_RLE_BITMASK, 6 },
        { COMPRESS_METHOD_HIERARCHICAL_RLE, 7 }
    };
    SdkChunkCodecBuffer candidates[sizeof(methods) / sizeof(methods[0])];
    size_t method_count = sizeof(methods) / sizeof(methods[0]);
    size_t i;
    size_t best_index = SIZE_MAX;
    size_t min_size = SIZE_MAX;
    size_t threshold_size = SIZE_MAX;

    if (!out_buffer) return 0;
    memset(candidates, 0, sizeof(candidates));

    for (i = 0u; i < method_count; ++i) {
        if (sdk_chunk_codec_encode_binary_with_method(chunk, methods[i].method_id, &candidates[i])) {
            if (candidates[i].payload_size < min_size) {
                min_size = candidates[i].payload_size;
            }
        }
    }

    if (min_size == SIZE_MAX) return 0;
    threshold_size = min_size + (min_size + 19u) / 20u;

    for (i = 0u; i < method_count; ++i) {
        if (!candidates[i].payload) continue;
        if (candidates[i].payload_size > threshold_size) continue;

        if (best_index == SIZE_MAX ||
            methods[i].decode_cost < methods[best_index].decode_cost ||
            (methods[i].decode_cost == methods[best_index].decode_cost &&
             candidates[i].payload_size < candidates[best_index].payload_size)) {
            best_index = i;
        }
    }

    if (best_index == SIZE_MAX) {
        for (i = 0u; i < method_count; ++i) {
            sdk_chunk_codec_buffer_reset(&candidates[i]);
        }
        return 0;
    }

    *out_buffer = candidates[best_index];
    memset(&candidates[best_index], 0, sizeof(candidates[best_index]));

    for (i = 0u; i < method_count; ++i) {
        sdk_chunk_codec_buffer_reset(&candidates[i]);
    }

    return 1;
}

int sdk_chunk_codec_encode_with_method(const SdkChunk* chunk,
                                       int method_id,
                                       char** out_payload_b64,
                                       int* out_payload_version,
                                       int* out_top_y)
{
    SdkChunkCodecBuffer buffer;
    char* payload_b64 = NULL;
    size_t payload_b64_len = 0u;
    int ok;

    if (out_payload_b64) *out_payload_b64 = NULL;
    if (out_payload_version) *out_payload_version = 0;
    if (out_top_y) *out_top_y = 0;

    memset(&buffer, 0, sizeof(buffer));
    ok = sdk_chunk_codec_encode_binary_with_method(chunk, method_id, &buffer);
    if (!ok) return 0;

    if (base64_encode(buffer.payload, buffer.payload_size, &payload_b64, &payload_b64_len) != 0) {
        sdk_chunk_codec_buffer_reset(&buffer);
        return 0;
    }
    (void)payload_b64_len;

    if (out_payload_b64) *out_payload_b64 = payload_b64;
    else free(payload_b64);
    if (out_payload_version) *out_payload_version = buffer.payload_version;
    if (out_top_y) *out_top_y = buffer.top_y;
    sdk_chunk_codec_buffer_reset(&buffer);
    return 1;
}

int sdk_chunk_codec_encode_auto(const SdkChunk* chunk,
                                char** out_codec_name,
                                char** out_payload_b64,
                                int* out_payload_version,
                                int* out_top_y)
{
    SdkChunkCodecBuffer buffer;
    char* payload_b64 = NULL;
    char* codec_name = NULL;
    size_t payload_b64_len = 0u;
    int ok;

    if (out_codec_name) *out_codec_name = NULL;
    if (out_payload_b64) *out_payload_b64 = NULL;
    if (out_payload_version) *out_payload_version = 0;
    if (out_top_y) *out_top_y = 0;

    memset(&buffer, 0, sizeof(buffer));
    ok = sdk_chunk_codec_encode_binary_auto(chunk, &buffer);
    if (!ok) return 0;

    codec_name = codec_strdup(buffer.codec_name);
    if (!codec_name) {
        sdk_chunk_codec_buffer_reset(&buffer);
        return 0;
    }

    if (base64_encode(buffer.payload, buffer.payload_size, &payload_b64, &payload_b64_len) != 0) {
        free(codec_name);
        sdk_chunk_codec_buffer_reset(&buffer);
        return 0;
    }
    (void)payload_b64_len;

    if (out_codec_name) *out_codec_name = codec_name;
    else free(codec_name);
    if (out_payload_b64) *out_payload_b64 = payload_b64;
    else free(payload_b64);
    if (out_payload_version) *out_payload_version = buffer.payload_version;
    if (out_top_y) *out_top_y = buffer.top_y;
    sdk_chunk_codec_buffer_reset(&buffer);
    return 1;
}

int sdk_chunk_codec_decode_binary(const char* codec_name,
                                  int payload_version,
                                  const uint8_t* payload,
                                  size_t payload_size,
                                  int top_y,
                                  SdkChunk* out_chunk)
{
    int method_id = sdk_chunk_codec_method_from_name(codec_name);

    if (payload_version != SDK_CHUNK_CODEC_PAYLOAD_VERSION) return 0;
    if (!out_chunk || !out_chunk->blocks) return 0;
    if (top_y == 0) {
        sdk_chunk_zero_blocks(out_chunk);
        return 1;
    }

    switch (method_id) {
        case SDK_CHUNK_CODEC_METHOD_CELL_RLE:
            return codec_decode_cell_rle(payload, payload_size, top_y, out_chunk);
        case COMPRESS_METHOD_VOLUME_LAYER:
            return codec_decode_volume_layer(payload, payload_size, out_chunk);
        case COMPRESS_METHOD_TEMPLATE:
            return codec_decode_template(payload, payload_size, out_chunk);
        case COMPRESS_METHOD_OCTREE:
            return codec_decode_octree(payload, payload_size, out_chunk);
        case COMPRESS_METHOD_INTER_CHUNK_DELTA:
            return codec_decode_inter_chunk_delta(payload, payload_size, out_chunk);
        case COMPRESS_METHOD_BITPACK:
            return codec_decode_bitpack(payload, payload_size, top_y, out_chunk);
        case COMPRESS_METHOD_SPARSE_COLUMN:
            return codec_decode_sparse_column(payload, payload_size, out_chunk);
        case COMPRESS_METHOD_RLE_BITMASK:
            return codec_decode_rle_bitmask(payload, payload_size, out_chunk);
        case COMPRESS_METHOD_HIERARCHICAL_RLE:
            return codec_decode_hierarchical_rle(payload, payload_size, out_chunk);
        default:
            return 0;
    }
}

int sdk_chunk_codec_decode(const char* codec_name,
                           int payload_version,
                           const char* payload_b64,
                           int top_y,
                           SdkChunk* out_chunk)
{
    uint8_t* payload = NULL;
    size_t payload_size = 0u;
    int ok;

    if (!codec_name || !out_chunk || !out_chunk->blocks) return 0;
    if (top_y == 0) {
        sdk_chunk_zero_blocks(out_chunk);
        return 1;
    }
    if (!payload_b64) return 0;

    if (base64_decode(payload_b64, strlen(payload_b64), &payload, &payload_size) != 0) {
        return 0;
    }

    ok = sdk_chunk_codec_decode_binary(codec_name, payload_version, payload, payload_size, top_y, out_chunk);
    free(payload);
    return ok;
}
