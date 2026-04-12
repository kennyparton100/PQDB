/* ============================================================================
 * Chunk Compression CLI Tool
 * 
 * Usage: chunk_compress <save.json> [options]
 * 
 * Reads a save.json file, analyzes chunks, and tests compression algorithms.
 * ============================================================================ */

#include "sdk_chunk_codec.h"
#include "../../Persistence/sdk_chunk_save_json.h"
#include "chunk_compress.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CHUNKS_TO_PROCESS 50  /* Limit for testing */

/* Simple JSON parsing state */
typedef struct {
    const char* data;
    size_t len;
    size_t pos;
} JsonParser;

/* Parse a chunk from the JSON */
typedef struct {
    int cx, cz;
    int top_y;
    int payload_version;
    char* codec;
    char* payload_b64;
    size_t payload_capacity;
} ParsedChunk;

static void init_parsed_chunk(ParsedChunk* chunk) {
    memset(chunk, 0, sizeof(*chunk));
}

static void free_parsed_chunk(ParsedChunk* chunk) {
    free(chunk->codec);
    free(chunk->payload_b64);
    chunk->codec = NULL;
    chunk->payload_b64 = NULL;
    chunk->payload_capacity = 0;
}

static int ensure_parsed_chunk_capacity(ParsedChunk* chunk, size_t required) {
    if (required <= chunk->payload_capacity) {
        return 0;
    }

    size_t new_capacity = (chunk->payload_capacity > 0) ? chunk->payload_capacity : 4096;
    while (new_capacity < required) {
        size_t grown = new_capacity * 2;
        if (grown <= new_capacity) {
            new_capacity = required;
            break;
        }
        new_capacity = grown;
    }

    char* new_payload = (char*)realloc(chunk->payload_b64, new_capacity);
    if (!new_payload) {
        return -1;
    }

    chunk->payload_b64 = new_payload;
    chunk->payload_capacity = new_capacity;
    return 0;
}

static int set_parsed_chunk_codec(ParsedChunk* chunk, const char* codec)
{
    size_t len;
    char* new_codec;

    if (!chunk || !codec) return -1;
    len = strlen(codec);
    new_codec = (char*)malloc(len + 1u);
    if (!new_codec) return -1;
    memcpy(new_codec, codec, len + 1u);
    free(chunk->codec);
    chunk->codec = new_codec;
    return 0;
}

static int floor_div_int(int value, int divisor) {
    if (divisor <= 0) {
        return 0;
    }
    if (value >= 0) {
        return value / divisor;
    }
    return -(((-value) + divisor - 1) / divisor);
}

static int parse_next_chunk(JsonParser* p, ParsedChunk* chunk) {
    const char* cursor;
    const char* end;

    if (!p || !chunk) return -1;

    cursor = p->data + p->pos;
    end = p->data + p->len;

    while (cursor < end) {
        const char* obj_start = NULL;
        const char* obj_end = NULL;
        SdkChunkSaveJsonEntry entry;
        size_t payload_len;

        if (!sdk_chunk_save_json_next_object(&cursor, end, &obj_start, &obj_end)) {
            p->pos = p->len;
            return -1;
        }

        sdk_chunk_save_json_entry_init(&entry);
        if (!sdk_chunk_save_json_parse_entry(obj_start, obj_end, &entry)) {
            sdk_chunk_save_json_entry_free(&entry);
            continue;
        }

        payload_len = entry.payload_b64 ? strlen(entry.payload_b64) : 0u;
        if (set_parsed_chunk_codec(chunk, entry.codec ? entry.codec : "cell_rle") < 0 ||
            ensure_parsed_chunk_capacity(chunk, payload_len + 1u) < 0) {
            sdk_chunk_save_json_entry_free(&entry);
            return -1;
        }

        chunk->cx = entry.cx;
        chunk->cz = entry.cz;
        chunk->top_y = entry.top_y;
        chunk->payload_version = entry.payload_version;
        if (entry.payload_b64 && payload_len > 0u) {
            memcpy(chunk->payload_b64, entry.payload_b64, payload_len);
        }
        chunk->payload_b64[payload_len] = '\0';
        sdk_chunk_save_json_entry_free(&entry);
        p->pos = (size_t)(cursor - p->data);
        return 0;
    }

    p->pos = p->len;
    return -1;
}

static void copy_sdk_chunk_to_raw(const SdkChunk* src, RawChunk* dst)
{
    int x;
    int y;
    int z;

    if (!src || !src->blocks || !dst) return;
    memset(dst->blocks, 0, sizeof(dst->blocks));
    dst->cx = src->cx;
    dst->cz = src->cz;
    dst->top_y = 0;

    for (y = 0; y < CHUNK_HEIGHT; ++y) {
        uint32_t layer_base = (uint32_t)y * CHUNK_BLOCKS_PER_LAYER;
        for (z = 0; z < CHUNK_DEPTH; ++z) {
            uint32_t row_base = layer_base + (uint32_t)z * CHUNK_WIDTH;
            for (x = 0; x < CHUNK_WIDTH; ++x) {
                uint16_t code = src->blocks[row_base + (uint32_t)x];
                dst->blocks[x][z][y] = code;
                if (code != 0u) {
                    dst->top_y = (uint16_t)(y + 1);
                }
            }
        }
    }
}

static int decode_parsed_chunk(const ParsedChunk* parsed, RawChunk* out)
{
    if (!parsed || !parsed->codec || !parsed->payload_b64 || !out) return -1;

    out->cx = parsed->cx;
    out->cz = parsed->cz;
    out->top_y = (uint16_t)parsed->top_y;

    if (strcmp(parsed->codec, "legacy_rle") == 0) {
        return rle_decode_legacy(parsed->payload_b64, strlen(parsed->payload_b64), out);
    }

    {
        SdkChunk sdk_chunk;
        int ok;

        sdk_chunk_init(&sdk_chunk, parsed->cx, parsed->cz, NULL);
        if (!sdk_chunk.blocks) {
            sdk_chunk_free(&sdk_chunk);
            return -1;
        }

        ok = sdk_chunk_codec_decode(parsed->codec,
                                    parsed->payload_version,
                                    parsed->payload_b64,
                                    parsed->top_y,
                                    &sdk_chunk);
        if (!ok) {
            sdk_chunk_free(&sdk_chunk);
            return -1;
        }
        copy_sdk_chunk_to_raw(&sdk_chunk, out);
        sdk_chunk_free(&sdk_chunk);
    }
    return 0;
}

static void free_raw_chunk_list(RawChunk** chunks, int count) {
    if (!chunks) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(chunks[i]);
    }
    free(chunks);
}

static int load_all_chunks(const char* data, size_t len,
                           RawChunk*** out_chunks, int* out_count,
                           int* out_min_cx, int* out_max_cx,
                           int* out_min_cz, int* out_max_cz) {
    JsonParser parser = {data, len, 0};
    ParsedChunk parsed;
    RawChunk** chunks = NULL;
    int capacity = 0;
    int count = 0;
    int min_cx = INT_MAX, max_cx = INT_MIN;
    int min_cz = INT_MAX, max_cz = INT_MIN;

    if (!out_chunks || !out_count) {
        return -1;
    }

    init_parsed_chunk(&parsed);

    while (parse_next_chunk(&parser, &parsed) == 0) {
        RawChunk* raw = (RawChunk*)calloc(1, sizeof(RawChunk));
        if (!raw) {
            free_raw_chunk_list(chunks, count);
            free_parsed_chunk(&parsed);
            return -1;
        }

        raw->cx = parsed.cx;
        raw->cz = parsed.cz;
        raw->top_y = (uint16_t)parsed.top_y;

        if (decode_parsed_chunk(&parsed, raw) != 0) {
            free(raw);
            continue;
        }

        if (count >= capacity) {
            int new_capacity = (capacity > 0) ? capacity * 2 : 64;
            RawChunk** new_chunks = (RawChunk**)realloc(chunks, sizeof(RawChunk*) * new_capacity);
            if (!new_chunks) {
                free(raw);
                free_raw_chunk_list(chunks, count);
                free_parsed_chunk(&parsed);
                return -1;
            }
            chunks = new_chunks;
            capacity = new_capacity;
        }

        chunks[count++] = raw;

        if (raw->cx < min_cx) min_cx = raw->cx;
        if (raw->cx > max_cx) max_cx = raw->cx;
        if (raw->cz < min_cz) min_cz = raw->cz;
        if (raw->cz > max_cz) max_cz = raw->cz;
    }

    free_parsed_chunk(&parsed);

    *out_chunks = chunks;
    *out_count = count;

    if (out_min_cx) *out_min_cx = (count > 0) ? min_cx : 0;
    if (out_max_cx) *out_max_cx = (count > 0) ? max_cx : 0;
    if (out_min_cz) *out_min_cz = (count > 0) ? min_cz : 0;
    if (out_max_cz) *out_max_cz = (count > 0) ? max_cz : 0;

    return 0;
}

static void populate_raw_superchunk(const DetectedSuperchunk* detected, RawSuperchunk* raw_sc) {
    memset(raw_sc, 0, sizeof(*raw_sc));
    raw_sc->scx = detected->scx;
    raw_sc->scz = detected->scz;

    for (int slot = 0; slot < SUPERCHUNK_CHUNKS; slot++) {
        raw_sc->chunks[slot] = detected->chunks[slot];
        if (detected->chunks[slot] && detected->chunks[slot]->top_y > raw_sc->max_top_y) {
            raw_sc->max_top_y = detected->chunks[slot]->top_y;
        }
    }
}

static int select_primary_superchunk(const DetectedSuperchunk* detected, int count) {
    int best_index = -1;

    for (int i = 0; i < count; i++) {
        if (best_index < 0 ||
            detected[i].num_terrain > detected[best_index].num_terrain ||
            (detected[i].num_terrain == detected[best_index].num_terrain &&
             detected[i].num_wall > detected[best_index].num_wall)) {
            best_index = i;
        }
    }

    return best_index;
}

/* Process a single chunk */
static void process_chunk(const ParsedChunk* parsed, int chunk_num, FILE* stats_file) {
    typedef struct { uint32_t count; uint16_t id; } BlockFreq;
    uint32_t* block_counts;
    BlockFreq* freqs;

    printf("\n--- Chunk %d (%d, %d) ---\n", chunk_num, parsed->cx, parsed->cz);
    printf("Top Y: %d, Codec: %s, Payload length: %zu\n",
           parsed->top_y,
           parsed->codec ? parsed->codec : "unknown",
           parsed->payload_b64 ? strlen(parsed->payload_b64) : 0u);
    
    /* Allocate large structs on heap to avoid stack overflow */
    RawChunk* raw = (RawChunk*)calloc(1, sizeof(RawChunk));
    RawChunk* decompressed = (RawChunk*)calloc(1, sizeof(RawChunk));
    block_counts = (uint32_t*)calloc(65536u, sizeof(uint32_t));
    freqs = (BlockFreq*)calloc(65536u, sizeof(BlockFreq));
    if (!raw || !decompressed || !block_counts || !freqs) {
        printf("Failed to allocate memory\n");
        free(raw); free(decompressed);
        free(block_counts); free(freqs);
        return;
    }
    
    raw->cx = parsed->cx;
    raw->cz = parsed->cz;
    raw->top_y = (uint16_t)parsed->top_y;
    
    clock_t start = clock();
    int decode_result = decode_parsed_chunk(parsed, raw);
    clock_t decode_time = clock() - start;
    
    if (decode_result < 0) {
        printf("Failed to decode chunk payload\n");
        free(raw); free(decompressed);
        free(block_counts); free(freqs);
        return;
    }
    
    printf("Decode time: %.2f ms\n", decode_time * 1000.0 / CLOCKS_PER_SEC);
    
    /* Analyze block distribution */
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                block_counts[raw->blocks[x][z][y]]++;
            }
        }
    }
    
    /* Find most common blocks */
    for (int i = 0; i < 65536; i++) {
        freqs[i].count = block_counts[i];
        freqs[i].id = (uint16_t)i;
    }
    
    /* Simple bubble sort for top 5 */
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 65536; j++) {
            if (freqs[j].count > freqs[i].count) {
                BlockFreq tmp = freqs[i];
                freqs[i] = freqs[j];
                freqs[j] = tmp;
            }
        }
    }
    
    printf("Top 5 blocks:\n");
    for (int i = 0; i < 5 && freqs[i].count > 0; i++) {
        printf("  Block %u: %u (%.1f%%)\n", 
               freqs[i].id, freqs[i].count,
               100.0 * freqs[i].count / CHUNK_TOTAL_BLOCKS);
    }
    
    /* Compress the chunk */
    CompressedChunk compressed;
    memset(&compressed, 0, sizeof(compressed));
    
    start = clock();
    int compress_result = compress_chunk(raw, &compressed);
    clock_t compress_time = clock() - start;
    
    if (compress_result < 0) {
        printf("Compression failed\n");
        free(raw); free(decompressed);
        free(block_counts); free(freqs);
        return;
    }
    
    printf("Compress time: %.2f ms\n", compress_time * 1000.0 / CLOCKS_PER_SEC);
    
    /* Print compression stats */
    size_t original_size = CHUNK_RAW_BYTES;
    compress_print_stats(&compressed, original_size);
    
    /* Verify by decompressing */
    start = clock();
    int decompress_result = decompress_chunk(&compressed, decompressed);
    clock_t decompress_time = clock() - start;
    
    if (decompress_result < 0) {
        printf("Decompression failed\n");
        compress_chunk_free(&compressed);
        free(raw); free(decompressed);
        free(block_counts); free(freqs);
        return;
    }
    
    printf("Decompress time: %.2f ms\n", decompress_time * 1000.0 / CLOCKS_PER_SEC);
    
    /* Verify correctness */
    bool verified = compress_verify(raw, decompressed);
    printf("Verification: %s\n", verified ? "PASS" : "FAIL");
    
    /* Calculate sizes */
    size_t source_size = parsed->payload_b64 ? strlen(parsed->payload_b64) : 0u;
    size_t estimated_binary = 20 + compressed.volume_count * 14;
    for (uint32_t i = 0; i < compressed.layer_count; i++) {
        estimated_binary += 6 + compressed.layers[i].patch_count * 8;
    }
    estimated_binary += 4 + compressed.residual_size;
    size_t estimated_base64 = ((estimated_binary + 2) / 3) * 4;
    
    /* Write stats to CSV */
    if (stats_file) {
        double rle_decode_ms = decode_time * 1000.0 / CLOCKS_PER_SEC;
        double new_decompress_ms = decompress_time * 1000.0 / CLOCKS_PER_SEC;
        double speedup = rle_decode_ms / new_decompress_ms;
        
        fprintf(stats_file, "%d,%d,%d,%d,%zu,%u,%u,%zu,%.1f,%.2f,%.2f,%.1f\n",
                chunk_num, parsed->cx, parsed->cz, parsed->top_y,
                source_size, compressed.volume_count, compressed.layer_count,
                estimated_base64,
                100.0 * compress_ratio(CHUNK_RAW_BYTES, estimated_binary),
                rle_decode_ms,
                new_decompress_ms,
                speedup);
    }
    
    printf("Speedup: %.1fx faster than RLE\n", 
           (decode_time * 1000.0 / CLOCKS_PER_SEC) / (decompress_time * 1000.0 / CLOCKS_PER_SEC));
    
    /* Template compression test (Phase 1 - Maximum Compression) */
    printf("\n  Testing template compression...\n");
    TerrainTemplate templates[32];
    int template_count = 0;
    template_init_defaults(templates, &template_count);
    
    float best_score = 0.0f;
    int best_template = template_find_best_match(raw, templates, template_count, &best_score);
    
    if (best_template >= 0 && best_score > 0.5f) {
        printf("  Best template: %d (%s) - match: %.1f%%\n", 
               best_template, templates[best_template].name, best_score * 100.0f);
        
        TemplateCompressedChunk tc;
        start = clock();
        int tc_result = compress_template(raw, (uint8_t)best_template, 
                                          &templates[best_template], &tc);
        clock_t tc_compress_time = clock() - start;
        
        if (tc_result == 0) {
            printf("  Template compress time: %.2f ms\n", 
                   tc_compress_time * 1000.0 / CLOCKS_PER_SEC);
            
            /* Verify */
            RawChunk* tc_decompressed = (RawChunk*)calloc(1, sizeof(RawChunk));
            if (tc_decompressed) {
                start = clock();
                int tc_decomp_result = decompress_template(&tc, &templates[best_template], 
                                                             tc_decompressed, raw->cx, raw->cz);
                clock_t tc_decompress_time = clock() - start;
                
                if (tc_decomp_result == 0) {
                    bool tc_verified = compress_verify(raw, tc_decompressed);
                    printf("  Template verification: %s\n", tc_verified ? "PASS" : "FAIL");
                    printf("  Template decompress time: %.2f ms\n",
                           tc_decompress_time * 1000.0 / CLOCKS_PER_SEC);
                    
                    /* Print template stats */
                    template_print_stats(&tc, CHUNK_RAW_BYTES);
                }
                free(tc_decompressed);
            }
            
            /* Cleanup */
            free(tc.patches);
        }
    } else {
        printf("  No good template match (best: %.1f%%)\n", best_score * 100.0f);
    }
    
    /* Auto-extract template from this chunk */
    printf("\n  Testing auto-extracted template...\n");
    TerrainTemplate auto_template;
    char auto_name[32];
    sprintf_s(auto_name, sizeof(auto_name), "Auto-Chunk-%d", raw->cx);
    int extract_result = template_extract_from_chunk(raw, &auto_template, auto_name);
    
    if (extract_result == 0) {
        printf("  Extracted template '%s' with %u layers:\n", auto_template.name, auto_template.layer_count);
        for (uint32_t i = 0; i < auto_template.layer_count && i < 6; i++) {
            printf("    Layer %u: block %u, Y %u-%u\n", i, 
                   auto_template.layers[i].block_id,
                   auto_template.layers[i].y_start,
                   auto_template.layers[i].y_end);
        }
        
        /* Calculate expected match score */
        uint32_t matching = 0, total = 0;
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                    uint16_t actual = raw->blocks[x][z][y];
                    uint16_t expected = 0;
                    for (uint32_t l = 0; l < auto_template.layer_count; l++) {
                        if (y >= auto_template.layers[l].y_start && 
                            y <= auto_template.layers[l].y_end) {
                            expected = (uint16_t)auto_template.layers[l].block_id;
                            break;
                        }
                    }
                    if (actual == expected) matching++;
                    total++;
                }
            }
        }
        float score = total > 0 ? (float)matching / total : 0.0f;
        printf("  Auto-template match: %.1f%%\n", score * 100.0f);
        
        /* Test compression with auto-extracted template */
        TemplateCompressedChunk auto_tc;
        start = clock();
        int auto_tc_result = compress_template(raw, 0, &auto_template, &auto_tc);
        clock_t auto_tc_time = clock() - start;
        
        if (auto_tc_result == 0) {
            printf("  Auto-template compress time: %.2f ms\n", 
                   auto_tc_time * 1000.0 / CLOCKS_PER_SEC);
            
            /* Verify */
            RawChunk* auto_decompressed = (RawChunk*)calloc(1, sizeof(RawChunk));
            if (auto_decompressed) {
                start = clock();
                int auto_decomp_result = decompress_template(&auto_tc, &auto_template,
                                                              auto_decompressed, raw->cx, raw->cz);
                clock_t auto_decomp_time = clock() - start;
                
                if (auto_decomp_result == 0) {
                    bool auto_verified = compress_verify(raw, auto_decompressed);
                    printf("  Auto-template verification: %s\n", auto_verified ? "PASS" : "FAIL");
                    printf("  Auto-template decompress time: %.2f ms\n",
                           auto_decomp_time * 1000.0 / CLOCKS_PER_SEC);
                    
                    template_print_stats(&auto_tc, CHUNK_RAW_BYTES);
                }
                free(auto_decompressed);
            }
            free(auto_tc.patches);
        }
    }
    
    /* Octree compression test (Phase 2 - Balanced) */
    printf("\n  Testing octree compression...\n");
    OctreeCompressedChunk oc;
    start = clock();
    int oc_result = octree_build(raw, &oc);
    clock_t oc_build_time = clock() - start;
    
    if (oc_result == 0) {
        printf("  Octree build time: %.2f ms\n", oc_build_time * 1000.0 / CLOCKS_PER_SEC);
        
        /* Verify */
        RawChunk* oc_decompressed = (RawChunk*)calloc(1, sizeof(RawChunk));
        if (oc_decompressed) {
            start = clock();
            int oc_decomp_result = decompress_octree(&oc, oc_decompressed);
            clock_t oc_decompress_time = clock() - start;
            
            if (oc_decomp_result == 0) {
                oc_decompressed->cx = raw->cx;
                oc_decompressed->cz = raw->cz;
                bool oc_verified = compress_verify(raw, oc_decompressed);
                printf("  Octree verification: %s\n", oc_verified ? "PASS" : "FAIL");
                printf("  Octree decompress time: %.2f ms\n",
                       oc_decompress_time * 1000.0 / CLOCKS_PER_SEC);
                
                octree_print_stats(&oc, CHUNK_RAW_BYTES);
            }
            free(oc_decompressed);
        }
        
        octree_free_forest(&oc);
    } else {
        printf("  Octree build failed\n");
    }
    
    /* Bit-Packed Palette compression test (Phase 6 - NEW high-performance) */
    printf("\n  Testing bit-packed palette compression...\n");
    BitPackedCompressedChunk bc;
    start = clock();
    int bp_result = compress_bitpack(raw, &bc);
    clock_t bp_compress_time = clock() - start;
    
    if (bp_result == 0) {
        printf("  Bit-pack compress time: %.2f ms\n", bp_compress_time * 1000.0 / CLOCKS_PER_SEC);
        
        /* Verify */
        RawChunk* bp_decompressed = (RawChunk*)calloc(1, sizeof(RawChunk));
        if (bp_decompressed) {
            start = clock();
            int bp_decomp_result = decompress_bitpack(&bc, bp_decompressed);
            clock_t bp_decompress_time = clock() - start;
            
            if (bp_decomp_result == 0) {
                bp_decompressed->cx = raw->cx;
                bp_decompressed->cz = raw->cz;
                bool bp_verified = compress_verify(raw, bp_decompressed);
                printf("  Bit-pack verification: %s\n", bp_verified ? "PASS" : "FAIL");
                printf("  Bit-pack decompress time: %.2f ms\n",
                       bp_decompress_time * 1000.0 / CLOCKS_PER_SEC);
                
                bitpack_print_stats(&bc, CHUNK_RAW_BYTES);
            }
            free(bp_decompressed);
        }
        
        bitpack_free(&bc);
    } else {
        printf("  Bit-pack compression failed\n");
    }
    
    /* Sparse Column Storage compression test (Phase 6 - NEW high-performance) */
    printf("\n  Testing sparse column storage compression...\n");
    SparseColumnCompressedChunk sc;
    start = clock();
    int sc_result = compress_sparse_column(raw, &sc);
    clock_t sc_compress_time = clock() - start;
    
    if (sc_result == 0) {
        printf("  Sparse column compress time: %.2f ms\n", sc_compress_time * 1000.0 / CLOCKS_PER_SEC);
        
        /* Verify */
        RawChunk* sc_decompressed = (RawChunk*)calloc(1, sizeof(RawChunk));
        if (sc_decompressed) {
            start = clock();
            int sc_decomp_result = decompress_sparse_column(&sc, sc_decompressed);
            clock_t sc_decompress_time = clock() - start;
            
            if (sc_decomp_result == 0) {
                sc_decompressed->cx = raw->cx;
                sc_decompressed->cz = raw->cz;
                bool sc_verified = compress_verify(raw, sc_decompressed);
                printf("  Sparse column verification: %s\n", sc_verified ? "PASS" : "FAIL");
                printf("  Sparse column decompress time: %.2f ms\n",
                       sc_decompress_time * 1000.0 / CLOCKS_PER_SEC);
                
                sparse_column_print_stats(&sc, CHUNK_RAW_BYTES);
            }
            free(sc_decompressed);
        }
        
        sparse_column_free(&sc);
    } else {
        printf("  Sparse column compression failed\n");
    }
    
    /* Zero-Run RLE + Bitmask compression test (Phase 6 - NEW high-performance) */
    printf("\n  Testing zero-run RLE + bitmask compression...\n");
    RLEBitmaskCompressedChunk rc;
    start = clock();
    int rc_result = compress_rle_bitmask(raw, &rc);
    clock_t rc_compress_time = clock() - start;
    
    if (rc_result == 0) {
        printf("  RLE+Bitmask compress time: %.2f ms\n", rc_compress_time * 1000.0 / CLOCKS_PER_SEC);
        
        /* Verify */
        RawChunk* rc_decompressed = (RawChunk*)calloc(1, sizeof(RawChunk));
        if (rc_decompressed) {
            start = clock();
            int rc_decomp_result = decompress_rle_bitmask(&rc, rc_decompressed);
            clock_t rc_decompress_time = clock() - start;
            
            if (rc_decomp_result == 0) {
                rc_decompressed->cx = raw->cx;
                rc_decompressed->cz = raw->cz;
                bool rc_verified = compress_verify(raw, rc_decompressed);
                printf("  RLE+Bitmask verification: %s\n", rc_verified ? "PASS" : "FAIL");
                printf("  RLE+Bitmask decompress time: %.2f ms\n",
                       rc_decompress_time * 1000.0 / CLOCKS_PER_SEC);
                
                /* Print detailed stats including palette info */
                printf("  RLE+Bitmask Palette: %u entries (%u bits/index)\n", 
                       rc.palette_size, rc.bits_per_index);
                printf("  RLE+Bitmask Non-air blocks: %u -> compressed %u bytes (%.1f%% saved)\n",
                       rc.total_non_air_blocks, rc.compressed_block_data_size,
                       rc.total_non_air_blocks > 0 ? 100.0f * (1.0f - (float)rc.compressed_block_data_size / rc.total_non_air_blocks) : 0.0f);
                
                /* Calculate total compressed size for this chunk */
                size_t mask_total = 0;
                for (uint32_t seg = 0; seg < rc.num_segments; seg++) {
                    mask_total += rc.mask_sizes[seg];
                }
                size_t total_compressed = 24 + rc.num_segments * 4 + mask_total + 
                                          rc.palette_size + rc.compressed_block_data_size + 4;
                printf("  RLE+Bitmask Total compressed: %zu bytes (ratio: %.1f%%)\n",
                       total_compressed, 100.0 * compress_ratio(CHUNK_RAW_BYTES, total_compressed));
            }
            free(rc_decompressed);
        }
        
        rle_bitmask_free(&rc);
    } else {
        printf("  RLE+Bitmask compression failed\n");
    }
    
    /* Hierarchical RLE V2 test (Phase 7 - 95%+ compression target) */
    printf("\n  Testing hierarchical RLE V2 compression...\n");
    HierarchicalRLECompressedChunk hc;
    start = clock();
    int hc_result = compress_hierarchical_rle(raw, &hc);
    clock_t hc_compress_time = clock() - start;
    
    if (hc_result == 0) {
        printf("  Hierarchical RLE V2 compress time: %.2f ms\n", hc_compress_time * 1000.0 / CLOCKS_PER_SEC);
        
        /* Verify */
        RawChunk* hc_decompressed = (RawChunk*)calloc(1, sizeof(RawChunk));
        if (hc_decompressed) {
            start = clock();
            int hc_decomp_result = decompress_hierarchical_rle(&hc, hc_decompressed);
            clock_t hc_decompress_time = clock() - start;
            
            if (hc_decomp_result == 0) {
                hc_decompressed->cx = raw->cx;
                hc_decompressed->cz = raw->cz;
                bool hc_verified = compress_verify(raw, hc_decompressed);
                printf("  Hierarchical RLE V2 verification: %s\n", hc_verified ? "PASS" : "FAIL");
                printf("  Hierarchical RLE V2 decompress time: %.2f ms\n",
                       hc_decompress_time * 1000.0 / CLOCKS_PER_SEC);
                
                hierarchical_rle_print_stats(&hc, CHUNK_RAW_BYTES);
            }
            free(hc_decompressed);
        }
        
        hierarchical_rle_free(&hc);
    } else {
        printf("  Hierarchical RLE V2 compression failed\n");
    }
    
    /* Auto-selection test (Phase 5 - Smart method selection) */
    printf("\n  Testing auto-selection...\n");
    CompressionRecommendation rec;
    compress_analyze_and_recommend(raw, &rec);
    
    printf("  Recommended method: %s (ID: %d)\n", rec.method_name, rec.method_id);
    printf("  Estimated size ratio: %.1f%% of raw\n", rec.compression_ratio * 100.0f);
    printf("  Estimated size: %zu bytes\n", rec.estimated_size);
    printf("  Est. compress time: %d ms, decompress: %d ms\n", 
           rec.compress_ms, rec.decompress_ms);
    
    /* Cleanup */
    compress_chunk_free(&compressed);
    free(raw);
    free(decompressed);
    free(block_counts);
    free(freqs);
}

int chunk_compress_cli_main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <save.json> [max_chunks]\n", argv[0]);
        printf("\nAnalyzes chunk compression on a save.json file.\n");
        printf("Default: processes up to %d chunks\n", MAX_CHUNKS_TO_PROCESS);
        return 1;
    }
    
    const char* filename = argv[1];
    int max_chunks = (argc > 2) ? atoi(argv[2]) : MAX_CHUNKS_TO_PROCESS;
    
    /* Open file */
    FILE* f = fopen(filename, "rb");
    if (!f) {
        printf("Error: Cannot open %s\n", filename);
        return 1;
    }
    
    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    printf("Loading %s (%.1f MB)...\n", filename, file_size / (1024.0 * 1024.0));
    
    /* Read file */
    char* data = (char*)malloc(file_size + 1);
    if (!data) {
        printf("Error: Out of memory\n");
        fclose(f);
        return 1;
    }
    
    size_t read = fread(data, 1, file_size, f);
    fclose(f);
    data[read] = '\0';
    
    /* Open stats CSV */
    FILE* stats_file = fopen("compression_stats.csv", "w");
    if (stats_file) {
        fprintf(stats_file, "chunk_num,cx,cz,top_y,source_payload_size,volumes,layers,compressed_size,ratio_pct,source_decode_ms,new_decompress_ms,speedup\n");
    }
    
    /* Parse chunks */
    JsonParser parser = {data, read, 0};
    ParsedChunk chunk;
    init_parsed_chunk(&chunk);
    int chunk_count = 0;
    
    printf("\nProcessing up to %d chunks...\n", max_chunks);
    
    int parse_result = parse_next_chunk(&parser, &chunk);
    
    while (chunk_count < max_chunks && parse_result == 0) {
        process_chunk(&chunk, chunk_count, stats_file);
        chunk_count++;
        
        /* Progress indicator */
        if (chunk_count % 10 == 0) {
            printf("\n--- Progress: %d chunks processed ---\n", chunk_count);
        }
        
        parse_result = parse_next_chunk(&parser, &chunk);
    }
    
    printf("\n=== Summary ===\n");
    printf("Total chunks processed: %d\n", chunk_count);
    printf("Stats written to: compression_stats.csv\n");
    
    if (chunk_count > 0) {
        RawChunk** all_chunks = NULL;
        int all_chunk_count = 0;
        int min_cx = 0, max_cx = 0, min_cz = 0, max_cz = 0;

        if (load_all_chunks(data, read, &all_chunks, &all_chunk_count,
                            &min_cx, &max_cx, &min_cz, &max_cz) == 0 &&
            all_chunk_count > 0) {
            printf("\n=== Superchunk Test (Loaded Neighborhood) ===\n");
            printf("Loaded %d chunks from save\n", all_chunk_count);
            printf("Chunk coordinate range: cx=[%d, %d], cz=[%d, %d]\n",
                   min_cx, max_cx, min_cz, max_cz);
            printf("Spans superchunk range: scx=[%d, %d], scz=[%d, %d]\n",
                   floor_div_int(min_cx, SUPERCHUNK_SIZE_CHUNKS),
                   floor_div_int(max_cx, SUPERCHUNK_SIZE_CHUNKS),
                   floor_div_int(min_cz, SUPERCHUNK_SIZE_CHUNKS),
                   floor_div_int(max_cz, SUPERCHUNK_SIZE_CHUNKS));

            if (all_chunk_count >= 3) {
                DetectedSuperchunk* detected_sc = NULL;
                int num_detected = 0;

                if (detect_superchunks(all_chunks, all_chunk_count, &detected_sc, &num_detected) == 0 &&
                    num_detected > 0) {
                    int total_terrain = 0;
                    int total_wall = 0;
                    int primary_index = select_primary_superchunk(detected_sc, num_detected);

                    printf("Detected %d candidate superchunk(s)\n", num_detected);
                    for (int sci = 0; sci < num_detected; sci++) {
                        total_terrain += detected_sc[sci].num_terrain;
                        total_wall += detected_sc[sci].num_wall;
                    }
                    printf("Total across detected superchunks: %d terrain + %d boundary = %d (loaded: %d)\n",
                           total_terrain, total_wall, total_terrain + total_wall, all_chunk_count);

                    if (primary_index >= 0) {
                        const DetectedSuperchunk* primary = &detected_sc[primary_index];
                        printf("\nPrimary superchunk: (%d, %d) with %u terrain chunks and %u boundary chunks\n",
                               primary->scx, primary->scz, primary->num_terrain, primary->num_wall);

                        if (primary->num_terrain > 0) {
                            RawSuperchunk raw_sc;
                            CompressedSuperchunk comp_sc;
                            populate_raw_superchunk(primary, &raw_sc);
                            memset(&comp_sc, 0, sizeof(comp_sc));

                            clock_t start = clock();
                            int result = compress_superchunk(&raw_sc, &comp_sc);
                            clock_t compress_time = clock() - start;

                            if (result == 0) {
                                printf("Superchunk compressed in %.2f ms\n",
                                       compress_time * 1000.0 / CLOCKS_PER_SEC);
                                superchunk_print_stats(&comp_sc, (size_t)primary->num_terrain * CHUNK_RAW_BYTES);

                                printf("\n=== Inter-Chunk Delta Test ===\n");
                                ChunkSimilarity pairs[16];
                                int pair_count = superchunk_find_similar_pairs(&raw_sc, pairs, 16);
                                printf("Found %d similar chunk pairs (>70%% similarity)\n", pair_count);

                                if (pair_count > 0) {
                                    for (int p = 0; p < pair_count && p < 3; p++) {
                                        printf("  Pair %d: chunks (%d,%d) <-> (%d,%d) = %.1f%% similar, %u diffs\n",
                                               p + 1,
                                               pairs[p].chunk1_cx, pairs[p].chunk1_cz,
                                               pairs[p].chunk2_cx, pairs[p].chunk2_cz,
                                               pairs[p].similarity * 100.0f, pairs[p].diff_count);
                                    }

                                    {
                                        RawChunk* chunk1 = NULL;
                                        RawChunk* chunk2 = NULL;

                                        for (int i = 0; i < SUPERCHUNK_CHUNKS; i++) {
                                            if (raw_sc.chunks[i] &&
                                                raw_sc.chunks[i]->cx == pairs[0].chunk1_cx &&
                                                raw_sc.chunks[i]->cz == pairs[0].chunk1_cz) {
                                                chunk1 = raw_sc.chunks[i];
                                            }
                                            if (raw_sc.chunks[i] &&
                                                raw_sc.chunks[i]->cx == pairs[0].chunk2_cx &&
                                                raw_sc.chunks[i]->cz == pairs[0].chunk2_cz) {
                                                chunk2 = raw_sc.chunks[i];
                                            }
                                        }

                                        if (chunk1 && chunk2) {
                                            DeltaCompressedChunk dc;
                                            memset(&dc, 0, sizeof(dc));

                                            start = clock();
                                            int dc_result = compress_delta(chunk2, chunk1, &dc);
                                            clock_t dc_time = clock() - start;

                                            if (dc_result == 0) {
                                                printf("\nDelta compression in %.2f ms:\n",
                                                       dc_time * 1000.0 / CLOCKS_PER_SEC);
                                                delta_print_stats(&dc, CHUNK_RAW_BYTES);

                                                RawChunk* dc_decompressed = (RawChunk*)calloc(1, sizeof(RawChunk));
                                                if (dc_decompressed) {
                                                    if (decompress_delta(&dc, chunk1, dc_decompressed) == 0) {
                                                        dc_decompressed->cx = chunk2->cx;
                                                        dc_decompressed->cz = chunk2->cz;
                                                        printf("Delta verification: %s\n",
                                                               compress_verify(chunk2, dc_decompressed) ? "PASS" : "FAIL");
                                                    } else {
                                                        printf("Delta verification: FAIL\n");
                                                    }
                                                    free(dc_decompressed);
                                                }

                                                delta_free(&dc);
                                            }
                                        }
                                    }
                                }

                                superchunk_free(&comp_sc);
                            } else {
                                printf("Superchunk compression failed\n");
                            }
                        }
                    }

                    printf("\n=== Superchunk Hierarchical V3 Test ===\n");
                    int tested_superchunks = 0;

                    for (int sci = 0; sci < num_detected; sci++) {
                        if (detected_sc[sci].num_terrain < 250) {
                            printf("\nSuperchunk %d: (%d, %d) - SKIPPED (incomplete core: %u terrain, %u boundary chunks)\n",
                                   sci + 1, detected_sc[sci].scx, detected_sc[sci].scz,
                                   detected_sc[sci].num_terrain, detected_sc[sci].num_wall);
                            continue;
                        }

                        tested_superchunks++;
                        printf("\nSuperchunk %d: (%d, %d) - %u terrain, %u boundary chunks - TESTING\n",
                               sci + 1, detected_sc[sci].scx, detected_sc[sci].scz,
                               detected_sc[sci].num_terrain, detected_sc[sci].num_wall);

                        SuperchunkHierarchicalCompressedChunk shc;
                        memset(&shc, 0, sizeof(shc));

                        clock_t start = clock();
                        int shc_result = compress_superchunk_hierarchical(&detected_sc[sci], &shc);
                        clock_t shc_compress_time = clock() - start;

                        if (shc_result == 0) {
                            printf("Superchunk Hierarchical V3 compress time: %.2f ms\n",
                                   shc_compress_time * 1000.0 / CLOCKS_PER_SEC);

                            /* Calculate sizes separately for fair comparison */
                            size_t terrain_original = (size_t)detected_sc[sci].num_terrain * CHUNK_RAW_BYTES;
                            size_t wall_original = (size_t)detected_sc[sci].num_wall * CHUNK_RAW_BYTES;
                            size_t total_original = terrain_original + wall_original;
                            
                            /* Get wall compressed size from shc */
                            size_t wall_compressed = 0;
                            for (int w = 0; w < shc.num_wall_chunks; w++) {
                                wall_compressed += shc.wall_chunks[w].compressed_size;
                            }
                            
                            /* Terrain compressed = total - wall portion */
                            size_t terrain_compressed = shc.compressed_block_data_size + 
                                                      (shc.num_chunks * sizeof(SuperchunkChunkSegments)) + 
                                                      48 /* header */ + shc.palette_size * 2;
                            size_t total_compressed = terrain_compressed + wall_compressed;
                            
                            printf("\n=== Hierarchical V3 Compression Breakdown ===\n");
                            printf("Terrain chunks (%u):\n", detected_sc[sci].num_terrain);
                            printf("  Original: %zu bytes (%.1f MB)\n", terrain_original, terrain_original / (1024.0*1024.0));
                            printf("  Compressed: %zu bytes (%.1f MB)\n", terrain_compressed, terrain_compressed / (1024.0*1024.0));
                            printf("  Ratio: %.1f%%\n", 100.0 * (1.0 - (double)terrain_compressed / terrain_original));
                            
                            printf("Wall chunks (%u):\n", detected_sc[sci].num_wall);
                            printf("  Original: %zu bytes (%.1f MB)\n", wall_original, wall_original / (1024.0*1024.0));
                            printf("  Compressed: %zu bytes (%.1f MB)\n", wall_compressed, wall_compressed / (1024.0*1024.0));
                            printf("  Ratio: %.1f%%\n", 100.0 * (1.0 - (double)wall_compressed / wall_original));
                            
                            printf("Combined total:\n");
                            printf("  Original: %zu bytes (%.1f MB)\n", total_original, total_original / (1024.0*1024.0));
                            printf("  Compressed: %zu bytes (%.1f MB)\n", total_compressed, total_compressed / (1024.0*1024.0));
                            printf("  Ratio: %.1f%%\n", 100.0 * (1.0 - (double)total_compressed / total_original));
                            
                            superchunk_hierarchical_print_stats(&shc, terrain_original);

                            RawChunk* decompressed_chunks[SUPERCHUNK_CHUNKS + SUPERCHUNK_BOUNDARY_CHUNKS];
                            memset(decompressed_chunks, 0, sizeof(decompressed_chunks));

                            int decompressed_count = 0;
                            start = clock();
                            int decomp_result =
                                decompress_superchunk_hierarchical(&shc, decompressed_chunks, &decompressed_count);
                            clock_t shc_decompress_time = clock() - start;

                            if (decomp_result == 0) {
                                printf("Superchunk Hierarchical V3 decompress time: %.2f ms\n",
                                       shc_decompress_time * 1000.0 / CLOCKS_PER_SEC);

                                int verified = 0;
                                int failed = 0;
                                for (int ci = 0; ci < decompressed_count; ci++) {
                                    RawChunk* orig = NULL;
                                    for (int oi = 0; oi < all_chunk_count; oi++) {
                                        if (all_chunks[oi]->cx == decompressed_chunks[ci]->cx &&
                                            all_chunks[oi]->cz == decompressed_chunks[ci]->cz) {
                                            orig = all_chunks[oi];
                                            break;
                                        }
                                    }

                                    if (orig && compress_verify(orig, decompressed_chunks[ci])) {
                                        verified++;
                                    } else {
                                        failed++;
                                    }
                                }

                                printf("Verification: %d passed, %d failed\n", verified, failed);

                                for (int ci = 0; ci < decompressed_count; ci++) {
                                    free(decompressed_chunks[ci]);
                                }
                            } else {
                                printf("Superchunk Hierarchical V3 decompression failed\n");
                            }

                            superchunk_hierarchical_free(&shc);
                        } else {
                            printf("Superchunk Hierarchical V3 compression failed\n");
                        }
                    }
                    
                    /* === SEPARATE TERRAIN-ONLY TEST === */
                    printf("\n\n=== SEPARATE TEST: Terrain-Only Superchunk ===\n");
                    if (detected_sc[0].num_terrain > 0) {
                        DetectedSuperchunk terrain_only;
                        memset(&terrain_only, 0, sizeof(terrain_only));
                        terrain_only.scx = detected_sc[0].scx;
                        terrain_only.scz = detected_sc[0].scz;
                        terrain_only.num_terrain = detected_sc[0].num_terrain;
                        terrain_only.num_wall = 0;
                        
                        /* Copy only terrain chunks */
                        for (int i = 0; i < SUPERCHUNK_CHUNKS; i++) {
                            terrain_only.chunks[i] = detected_sc[0].chunks[i];
                        }
                        
                        SuperchunkHierarchicalCompressedChunk terrain_comp;
                        memset(&terrain_comp, 0, sizeof(terrain_comp));
                        
                        clock_t start = clock();
                        int terrain_result = compress_superchunk_hierarchical(&terrain_only, &terrain_comp);
                        clock_t terrain_time = clock() - start;
                        
                        if (terrain_result == 0) {
                            size_t orig = (size_t)terrain_only.num_terrain * CHUNK_RAW_BYTES;
                            size_t comp = terrain_comp.compressed_block_data_size + 
                                         (terrain_comp.num_chunks * sizeof(SuperchunkChunkSegments)) + 
                                         48 + terrain_comp.palette_size * 2;
                            printf("Terrain-Only (%u chunks):\n", terrain_only.num_terrain);
                            printf("  Compress time: %.2f ms\n", terrain_time * 1000.0 / CLOCKS_PER_SEC);
                            printf("  Original: %zu bytes (%.1f MB)\n", orig, orig / (1024.0*1024.0));
                            printf("  Compressed: %zu bytes (%.1f MB)\n", comp, comp / (1024.0*1024.0));
                            printf("  Ratio: %.1f%%\n", 100.0 * (1.0 - (double)comp / orig));
                            
                            /* Verify with decompress timing */
                            RawChunk* decompressed[256];
                            memset(decompressed, 0, sizeof(decompressed));
                            int dc = 0;
                            clock_t decompress_start = clock();
                            int dr = decompress_superchunk_hierarchical(&terrain_comp, decompressed, &dc);
                            clock_t decompress_time = clock() - decompress_start;
                            
                            printf("  Decompress time: %.2f ms\n", decompress_time * 1000.0 / CLOCKS_PER_SEC);
                            
                            int v = 0, failed = 0;
                            if (dr == 0) {
                                for (int ci = 0; ci < dc; ci++) {
                                    RawChunk* o = NULL;
                                    for (int oi = 0; oi < all_chunk_count; oi++) {
                                        if (all_chunks[oi]->cx == decompressed[ci]->cx &&
                                            all_chunks[oi]->cz == decompressed[ci]->cz) {
                                            o = all_chunks[oi]; break;
                                        }
                                    }
                                    if (o && compress_verify(o, decompressed[ci])) v++; else failed++;
                                    free(decompressed[ci]);
                                }
                            }
                            printf("  Verification: %d passed, %d failed\n", v, failed);
                            superchunk_hierarchical_free(&terrain_comp);
                        }
                    }
                    
                    /* === SEPARATE WALL-ONLY TEST === */
                    printf("\n\n=== SEPARATE TEST: Wall-Only Superchunk ===\n");
                    if (detected_sc[0].num_wall > 0) {
                        DetectedSuperchunk wall_only;
                        memset(&wall_only, 0, sizeof(wall_only));
                        wall_only.scx = detected_sc[0].scx;
                        wall_only.scz = detected_sc[0].scz;
                        wall_only.num_terrain = 0;
                        wall_only.num_wall = detected_sc[0].num_wall;
                        
                        /* Copy only wall chunks */
                        for (int i = 0; i < wall_only.num_wall && i < SUPERCHUNK_BOUNDARY_CHUNKS; i++) {
                            wall_only.wall_chunks[i] = detected_sc[0].wall_chunks[i];
                            wall_only.wall_rel_x[i] = detected_sc[0].wall_rel_x[i];
                            wall_only.wall_rel_z[i] = detected_sc[0].wall_rel_z[i];
                        }
                        
                        SuperchunkHierarchicalCompressedChunk wall_comp;
                        memset(&wall_comp, 0, sizeof(wall_comp));
                        
                        clock_t wall_start = clock();
                        int wall_result = compress_superchunk_hierarchical(&wall_only, &wall_comp);
                        clock_t wall_time = clock() - wall_start;
                        
                        if (wall_result == 0) {
                            size_t orig = (size_t)wall_only.num_wall * CHUNK_RAW_BYTES;
                            size_t comp = 0;
                            for (int w = 0; w < wall_comp.num_wall_chunks; w++) {
                                comp += wall_comp.wall_chunks[w].compressed_size;
                            }
                            printf("Wall-Only (%u chunks):\n", wall_only.num_wall);
                            printf("  Compress time: %.2f ms\n", wall_time * 1000.0 / CLOCKS_PER_SEC);
                            printf("  Original: %zu bytes (%.1f MB)\n", orig, orig / (1024.0*1024.0));
                            printf("  Compressed: %zu bytes (%.1f MB)\n", comp, comp / (1024.0*1024.0));
                            printf("  Ratio: %.1f%%\n", 100.0 * (1.0 - (double)comp / orig));
                            
                            /* Verify */
                            RawChunk* decompressed[68];
                            memset(decompressed, 0, sizeof(decompressed));
                            int dc = 0;
                            int dr = decompress_superchunk_hierarchical(&wall_comp, decompressed, &dc);
                            int v = 0, failed = 0;
                            if (dr == 0) {
                                for (int ci = 0; ci < dc; ci++) {
                                    RawChunk* o = NULL;
                                    for (int oi = 0; oi < all_chunk_count; oi++) {
                                        if (all_chunks[oi]->cx == decompressed[ci]->cx &&
                                            all_chunks[oi]->cz == decompressed[ci]->cz) {
                                            o = all_chunks[oi]; break;
                                        }
                                    }
                                    if (o && compress_verify(o, decompressed[ci])) v++; else failed++;
                                    free(decompressed[ci]);
                                }
                            }
                            printf("  Verification: %d passed, %d failed\n", v, failed);
                            superchunk_hierarchical_free(&wall_comp);
                        }
                    }

                    if (tested_superchunks == 0) {
                        printf("No complete superchunks were available for hierarchical testing\n");
                    }
                } else {
                    printf("No complete superchunk candidates detected\n");
                }

                free(detected_sc);
            }

            /* ============================================================================
             * COMPREHENSIVE TEST: New Lazy Loading and Partial Decode APIs (NEW)
             * ============================================================================
             */
            #if 1
            printf("\n\n=== COMPREHENSIVE TEST: Lazy Loading APIs ===\n");
            printf("Testing new partial decode and lazy loading functionality...\n");
            printf("Debug: Starting test section, all_chunks=%p, count=%d\n", 
                   (void*)all_chunks, all_chunk_count);
            
            if (!all_chunks || all_chunk_count == 0) {
                printf("No chunks available for testing, skipping lazy loading tests.\n");
            } else {
                printf("Have %d chunks, starting tests...\n", all_chunk_count);
                
                /* Test 1: Sparse Column - simple only */
                printf("TEST 1: Sparse Column Direct Access API\n");
                {
                    SparseColumnCompressedChunk sc_comp;
                    memset(&sc_comp, 0, sizeof(sc_comp));
                    
                    printf("  Compressing with sparse column...\n");
                    if (compress_sparse_column(all_chunks[0], &sc_comp) == 0) {
                        printf("  Compression OK, testing get_surface_height...\n");
                        int height1 = sparse_column_get_surface_height(&sc_comp, 32, 32);
                        printf("  Surface height at (32,32): %d Y-blocks\n", height1);
                        
                        printf("  Testing get_column_ptr...\n");
                        int col_height = 0;
                        const uint8_t* col_ptr = sparse_column_get_column_ptr(&sc_comp, 32, 32, &col_height);
                        if (col_ptr) {
                            printf("  Column (32,32) data: height=%d, top block=%d\n", col_height, col_ptr[col_height-1]);
                            printf("  ✓ Direct column access works\n");
                        } else {
                            printf("  ✗ Column ptr returned NULL (column may be empty)\n");
                        }
                        
                        sparse_column_free(&sc_comp);
                    } else {
                        printf("  ✗ Sparse column compression failed\n");
                    }
                }
                
                /* Test 2: Hierarchical RLE V2 */
                printf("\nTEST 2: Hierarchical RLE V2 Segment-Level Decode\n");
                {
                    HierarchicalRLECompressedChunk hrle_comp;
                    RawChunk hrle_decomp;
                    memset(&hrle_comp, 0, sizeof(hrle_comp));
                    memset(&hrle_decomp, 0, sizeof(hrle_decomp));
                    
                    printf("  Compressing with Hierarchical RLE...\n");
                    if (compress_hierarchical_rle(all_chunks[0], &hrle_comp) == 0) {
                        printf("  Compression OK, %d segments\n", hrle_comp.num_segments);
                        
                        printf("  Testing segment_has_blocks...\n");
                        int has_blocks_seg0 = hierarchical_rle_segment_has_blocks(&hrle_comp, 0);
                        printf("    Segment 0: %s\n", has_blocks_seg0 ? "blocks" : "air");
                        
                        printf("  Testing decompress_hierarchical_segments...\n");
                        clock_t seg_start = clock();
                        if (decompress_hierarchical_segments(&hrle_comp, 30, 4, &hrle_decomp) == 0) {
                            clock_t seg_end = clock();
                            double seg_time = 1000.0 * (seg_end - seg_start) / CLOCKS_PER_SEC;
                            printf("    Decompress 4 segments (30-33): %.2f ms\n", seg_time);
                            printf("    ✓ Segment-level decode works\n");
                        } else {
                            printf("    ✗ Segment decode failed\n");
                        }
                        
                        printf("  Testing get_block...\n");
                        uint16_t block_val = hierarchical_rle_get_block(&hrle_comp, 32, 500, 32);
                        printf("    Block at (32,500,32): %s\n", block_val ? "exists" : "air");
                        
                        hierarchical_rle_free(&hrle_comp);
                    } else {
                        printf("  ✗ Hierarchical RLE compression failed\n");
                    }
                }
                
                /* Test 3: RLE+Bitmask */
                printf("\nTEST 3: RLE+Bitmask Segment-Level Decode\n");
                {
                    RLEBitmaskCompressedChunk rle_comp;
                    RawChunk rle_decomp;
                    memset(&rle_comp, 0, sizeof(rle_comp));
                    memset(&rle_decomp, 0, sizeof(rle_decomp));
                    
                    printf("  Compressing with RLE+Bitmask...\n");
                    if (compress_rle_bitmask(all_chunks[0], &rle_comp) == 0) {
                        printf("  Compression OK, %u segments\n", rle_comp.num_segments);
                        
                        printf("  Testing segment_has_blocks...\n");
                        int has_blocks = rle_bitmask_segment_has_blocks(&rle_comp, 32);
                        printf("    Segment 32: %s\n", has_blocks ? "blocks" : "air");
                        
                        printf("  Testing decompress_rle_bitmask_segments...\n");
                        clock_t seg_start = clock();
                        if (decompress_rle_bitmask_segments(&rle_comp, 28, 8, &rle_decomp) == 0) {
                            clock_t seg_end = clock();
                            double seg_time = 1000.0 * (seg_end - seg_start) / CLOCKS_PER_SEC;
                            printf("    Decompress 8 segments (28-35): %.2f ms\n", seg_time);
                            printf("    ✓ Segment-level decode works\n");
                        } else {
                            printf("    ✗ Segment decode failed\n");
                        }
                        
                        printf("  Testing get_block...\n");
                        uint16_t block_val = rle_bitmask_get_block(&rle_comp, 32, 500, 32);
                        printf("    Block at (32,500,32): %s\n", block_val ? "exists" : "air");
                        
                        rle_bitmask_free(&rle_comp);
                    } else {
                        printf("  ✗ RLE+Bitmask compression failed\n");
                    }
                }
                
                /* Test 4: Superchunk Lazy Loading - THE BIG TEST */
                printf("\nTEST 4: Superchunk V3 Lazy Loading API - THE 50MS CLAIM\n");
                {
                    /* Find a superchunk to test with */
                    DetectedSuperchunk* sc_test = NULL;
                    int sc_count = 0;
                    printf("  Detecting superchunks for lazy loading test...\n");
                    if (detect_superchunks(all_chunks, all_chunk_count, &sc_test, &sc_count) == 0 && sc_count > 0) {
                        printf("  Found %d superchunk(s), using primary\n", sc_count);
                        
                        SuperchunkHierarchicalCompressedChunk sc_hier;
                        memset(&sc_hier, 0, sizeof(sc_hier));
                        
                        printf("  Compressing with Superchunk Hierarchical V3...\n");
                        clock_t comp_start = clock();
                        if (compress_superchunk_hierarchical(&sc_test[0], &sc_hier) == 0) {
                            clock_t comp_end = clock();
                            double comp_time = 1000.0 * (comp_end - comp_start) / CLOCKS_PER_SEC;
                            printf("  Compression: %.0f ms, %d chunks, %u terrain chunks\n", 
                                   comp_time, sc_hier.num_chunks + sc_hier.num_wall_chunks, sc_hier.num_chunks);
                            
                            /* Test 4a: Single chunk lazy loading */
                            printf("\n  4a: Single chunk lazy loading\n");
                            int center_x = 8, center_z = 8;
                            clock_t single_start = clock();
                            RawChunk* single_chunk = superchunk_load_single_chunk(&sc_hier, center_x, center_z);
                            clock_t single_end = clock();
                            double single_time = 1000.0 * (single_end - single_start) / CLOCKS_PER_SEC;
                            
                            if (single_chunk) {
                                printf("    Loaded chunk (%d,%d): %.2f ms\n", center_x, center_z, single_time);
                                int non_zero = 0;
                                for (int x = 0; x < CHUNK_SIZE_X && non_zero < 10; x++) {
                                    for (int z = 0; z < CHUNK_SIZE_Z && non_zero < 10; z++) {
                                        for (int y = 0; y < CHUNK_SIZE_Y && non_zero < 10; y++) {
                                            if (single_chunk->blocks[x][z][y] != 0) non_zero++;
                                        }
                                    }
                                }
                                printf("    Found %d non-air blocks\n", non_zero);
                                printf("    ✓ Single chunk lazy load works!\n");
                                free(single_chunk);
                            } else {
                                printf("    ✗ Failed to load single chunk\n");
                            }
                            
                            /* Test 4b: Radius loading - THE 50MS CLAIM */
                            printf("\n  4b: Radius-based loading (THE 50MS CLAIM)\n");
                            int radii[] = {1, 2, 3, 4, 5};
                            int max_chunks_expected[] = {9, 25, 49, 81, 121};
                            double full_decomp_per_chunk = 0;
                            
                            for (int r_idx = 0; r_idx < 5; r_idx++) {
                                int radius = radii[r_idx];
                                int max_load = max_chunks_expected[r_idx];
                                
                                RawChunk** radius_chunks = (RawChunk**)calloc(max_load, sizeof(RawChunk*));
                                if (!radius_chunks) continue;
                                
                                clock_t radius_start = clock();
                                int loaded = superchunk_load_chunks_radius(&sc_hier, 8, 8, radius, 
                                                                         radius_chunks, max_load);
                                clock_t radius_end = clock();
                                double radius_time = 1000.0 * (radius_end - radius_start) / CLOCKS_PER_SEC;
                                
                                printf("    Radius %d (%d chunks): %.2f ms", radius, loaded, radius_time);
                                if (radius == 3) {
                                    printf(" <-- TYPICAL VIEW DISTANCE TARGET: <50ms");
                                    if (radius_time < 50.0) {
                                        printf(" ✓ PASS");
                                    } else {
                                        printf(" ✗ FAIL");
                                    }
                                }
                                printf("\n");
                                
                                if (loaded > 0) {
                                    printf("      Per-chunk: %.2f ms\n", radius_time / loaded);
                                }
                                
                                /* Cleanup */
                                for (int i = 0; i < loaded; i++) {
                                    if (radius_chunks[i]) free(radius_chunks[i]);
                                }
                                free(radius_chunks);
                            }
                            
                            /* Test 4c: Full decompress comparison */
                            printf("\n  4c: Full superchunk decompress (for comparison)\n");
                            RawChunk** all_out = (RawChunk**)calloc(324, sizeof(RawChunk*));
                            int all_count = 0;
                            clock_t full_start = clock();
                            int full_result = decompress_superchunk_hierarchical(&sc_hier, all_out, &all_count);
                            clock_t full_end = clock();
                            double full_time = 1000.0 * (full_end - full_start) / CLOCKS_PER_SEC;
                            
                            if (full_result == 0) {
                                printf("    Full decompress (%d chunks): %.2f ms\n", all_count, full_time);
                                printf("    Average per chunk: %.2f ms\n", full_time / all_count);
                                full_decomp_per_chunk = full_time / all_count;
                            }
                            
                            for (int i = 0; i < all_count; i++) free(all_out[i]);
                            free(all_out);
                            
                            /* Test 4d: Single block query */
                            printf("\n  4d: Single block query (no decompression)\n");
                            clock_t query_start = clock();
                            int query_count = 1000;
                            int found_count = 0;
                            for (int q = 0; q < query_count; q++) {
                                uint16_t block = superchunk_get_block(&sc_hier, 8, 8, 
                                                                       q % CHUNK_SIZE_X, 500, q % CHUNK_SIZE_Z);
                                if (block != 0) found_count++;
                            }
                            clock_t query_end = clock();
                            double query_time = 1000.0 * (query_end - query_start) / CLOCKS_PER_SEC;
                            printf("    %d queries: %.2f ms (%.3f ms each)\n", 
                                   query_count, query_time, query_time / query_count);
                            
                            /* SUMMARY */
                            printf("\n  === LAZY LOADING PERFORMANCE SUMMARY ===\n");
                            printf("  Full decompress: ~%.0f ms for %d chunks (%.2f ms/chunk)\n", 
                                   full_time, all_count, full_decomp_per_chunk);
                            printf("  Target radius=3: Should load ~49 chunks in <50ms\n");
                            double expected_lazy = full_decomp_per_chunk * 49;
                            printf("  Theoretical lazy: ~%.0f ms (%.1f%% of full)\n", 
                                   expected_lazy, 100.0 * expected_lazy / full_time);
                            
                            superchunk_hierarchical_free(&sc_hier);
                        } else {
                            printf("  ✗ Superchunk compression failed\n");
                        }
                        free(sc_test);
                    } else {
                        printf("  ✗ No superchunks available for testing\n");
                    }
                }
            }
            #endif
            
            printf("\n=== END LAZY LOADING API TESTS ===\n");

            free_raw_chunk_list(all_chunks, all_chunk_count);
        } else {
            printf("Failed to load chunks for superchunk testing\n");
        }
    }
    
    /* Cleanup */
    if (stats_file) fclose(stats_file);
    free_parsed_chunk(&chunk);
    free(data);
    
    return 0;
}

#ifndef SDK_CHUNK_COMPRESS_NO_MAIN
int main(int argc, char* argv[]) {
    return chunk_compress_cli_main(argc, argv);
}
#endif
