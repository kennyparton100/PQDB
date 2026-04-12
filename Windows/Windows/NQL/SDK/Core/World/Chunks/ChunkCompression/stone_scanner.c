#include "sdk_chunk_codec.h"
#include "../sdk_chunk.h"
#include "../../Persistence/sdk_chunk_save_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int chunk_contains_stone_material(const SdkChunk* chunk, uint32_t* out_count)
{
    uint32_t count = 0u;
    uint32_t i;

    if (!chunk || !chunk->blocks) return 0;

    for (i = 0u; i < CHUNK_TOTAL_BLOCKS; ++i) {
        SdkWorldCellCode code = chunk->blocks[i];
        uint16_t material = 0u;

        if (sdk_world_cell_is_full_block(code)) {
            material = (uint16_t)sdk_world_cell_decode_full_block(code);
        } else if (sdk_world_cell_is_inline_construction(code)) {
            material = (uint16_t)sdk_world_cell_inline_material(code);
        }

        if (material == 24u || material == 114u) {
            count++;
        }
    }

    if (out_count) *out_count = count;
    return count > 0u;
}

int main(int argc, char** argv)
{
    char* json = NULL;
    long size = 0;
    FILE* f = NULL;
    int total_chunks = 0;
    int chunks_with_stone = 0;
    const char* cursor;
    const char* end;

    if (argc < 2) {
        printf("Usage: %s <save.json>\n", argv[0]);
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        printf("Failed to open %s\n", argv[1]);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    json = (char*)malloc((size_t)size + 1u);
    if (!json) {
        fclose(f);
        return 1;
    }

    fread(json, 1, (size_t)size, f);
    json[size] = '\0';
    fclose(f);

    printf("Scanning save file for stone/stone-brick materials...\n\n");

    cursor = json;
    end = json + size;
    while (cursor < end) {
        const char* obj_start = NULL;
        const char* obj_end = NULL;
        SdkChunkSaveJsonEntry entry;

        if (!sdk_chunk_save_json_next_object(&cursor, end, &obj_start, &obj_end)) {
            break;
        }

        sdk_chunk_save_json_entry_init(&entry);
        if (!sdk_chunk_save_json_parse_entry(obj_start, obj_end, &entry)) {
            sdk_chunk_save_json_entry_free(&entry);
            continue;
        }

        if (entry.codec && entry.payload_b64 && strcmp(entry.codec, "legacy_rle") != 0) {
            SdkChunk chunk;
            uint32_t stone_count = 0u;

            sdk_chunk_init(&chunk, entry.cx, entry.cz, NULL);
            if (chunk.blocks &&
                sdk_chunk_codec_decode(entry.codec,
                                       entry.payload_version,
                                       entry.payload_b64,
                                       entry.top_y,
                                       &chunk)) {
                total_chunks++;
                if (chunk_contains_stone_material(&chunk, &stone_count)) {
                    chunks_with_stone++;
                    printf("Chunk (%d, %d): found %u stone-like materials\n",
                           entry.cx,
                           entry.cz,
                           stone_count);
                }
            }
            sdk_chunk_free(&chunk);
        }

        sdk_chunk_save_json_entry_free(&entry);
    }

    printf("\n=== Summary ===\n");
    printf("Total chunks scanned: %d\n", total_chunks);
    printf("Chunks with stone/stone_bricks: %d\n", chunks_with_stone);

    if (chunks_with_stone == 0) {
        printf("\nNo stone materials were found in decoded chunk payloads.\n");
    } else {
        printf("\nStone materials were found in decoded chunk payloads.\n");
    }

    free(json);
    return 0;
}
