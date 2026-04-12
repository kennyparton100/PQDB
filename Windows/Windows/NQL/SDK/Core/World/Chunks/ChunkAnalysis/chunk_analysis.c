#include "chunk_analysis.h"
#include "../../Superchunks/Geometry/sdk_superchunk_geometry.h"
#include "../ChunkCompression/sdk_chunk_codec.h"
#include "../sdk_chunk.h"
#include "../../Persistence/sdk_chunk_save_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ============================================================================
 * JSON Parsing (simplified, adapted from chunk_compress)
 * ============================================================================ */

static inline uint16_t decode_cell_code_to_material(uint16_t code)
{
    if (code == SDK_WORLD_CELL_OVERFLOW_CODE) {
        return 0u;
    }
    if (code >= SDK_WORLD_CELL_INLINE_BASE) {
        return code & SDK_WORLD_CELL_INLINE_MATERIAL_MASK;
    }
    return code;
}

static char* read_file_contents(const char* filename, size_t* out_size) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }
    
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    if (out_size) *out_size = size;
    return buffer;
}

static void copy_sdk_chunk_to_ca(const SdkChunk* src, CA_RawChunk* dst)
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
                uint16_t material = decode_cell_code_to_material(src->blocks[row_base + (uint32_t)x]);
                dst->blocks[x][z][y] = material;
                if (material != 0u) {
                    dst->top_y = (uint16_t)(y + 1);
                }
            }
        }
    }
}

static int load_chunk_entry_into_collection(const SdkChunkSaveJsonEntry* entry,
                                            CA_ChunkCollection* collection,
                                            int* chunk_idx)
{
    CA_RawChunk* chunk;

    if (!entry || !entry->codec || !entry->payload_b64 || !collection || !chunk_idx) return 0;
    if (*chunk_idx < 0 || *chunk_idx >= collection->capacity) return 0;

    chunk = &collection->chunks[*chunk_idx];
    memset(chunk, 0, sizeof(*chunk));
    chunk->cx = entry->cx;
    chunk->cz = entry->cz;
    chunk->top_y = (uint16_t)entry->top_y;

    if (strcmp(entry->codec, "legacy_rle") == 0) {
        return 0;
    } else {
        SdkChunk sdk_chunk;
        int ok;

        sdk_chunk_init(&sdk_chunk, entry->cx, entry->cz, NULL);
        if (!sdk_chunk.blocks) {
            sdk_chunk_free(&sdk_chunk);
            return 0;
        }

        ok = sdk_chunk_codec_decode(entry->codec,
                                    entry->payload_version,
                                    entry->payload_b64,
                                    entry->top_y,
                                    &sdk_chunk);
        if (!ok) {
            sdk_chunk_free(&sdk_chunk);
            return 0;
        }
        copy_sdk_chunk_to_ca(&sdk_chunk, chunk);
        sdk_chunk_free(&sdk_chunk);
    }

    (*chunk_idx)++;
    collection->count = *chunk_idx;
    return 1;
}

static int parse_chunks_from_array(const char* array_start,
                                   const char* array_end,
                                   CA_ChunkCollection* collection,
                                   int* chunk_idx)
{
    const char* cursor;

    if (!array_start || !array_end || !collection || !chunk_idx) return 0;
    cursor = array_start + 1;

    while (*chunk_idx < collection->capacity) {
        const char* obj_start = NULL;
        const char* obj_end = NULL;
        SdkChunkSaveJsonEntry entry;

        if (!sdk_chunk_save_json_next_object(&cursor, array_end, &obj_start, &obj_end)) break;
        sdk_chunk_save_json_entry_init(&entry);
        if (sdk_chunk_save_json_parse_entry(obj_start, obj_end, &entry)) {
            load_chunk_entry_into_collection(&entry, collection, chunk_idx);
        }
        sdk_chunk_save_json_entry_free(&entry);
    }

    return *chunk_idx;
}

static int parse_nested_superchunks(const char* text,
                                    const char* key,
                                    CA_ChunkCollection* collection,
                                    int* chunk_idx)
{
    const char* array_start = NULL;
    const char* array_end = NULL;
    const char* cursor;

    if (!text || !key || !collection || !chunk_idx) return 0;
    if (!sdk_chunk_save_json_find_array(text, key, &array_start, &array_end)) return 0;

    cursor = array_start + 1;
    while (*chunk_idx < collection->capacity) {
        const char* obj_start = NULL;
        const char* obj_end = NULL;
        const char* chunks_start = NULL;
        const char* chunks_end = NULL;

        if (!sdk_chunk_save_json_next_object(&cursor, array_end, &obj_start, &obj_end)) break;
        if (sdk_chunk_save_json_find_array(obj_start, "chunks", &chunks_start, &chunks_end)) {
            parse_chunks_from_array(chunks_start, chunks_end, collection, chunk_idx);
        }
    }

    return *chunk_idx;
}

int ca_load_json_save(const char* filename, CA_ChunkCollection* collection, int max_chunks) {
    size_t size;
    char* json = read_file_contents(filename, &size);
    int chunk_idx = 0;
    const char* chunks_start = NULL;
    const char* chunks_end = NULL;

    (void)size;
    if (!filename || !collection || !json) {
        free(json);
        return -1;
    }

    memset(collection, 0, sizeof(*collection));
    collection->capacity = max_chunks > 0 ? max_chunks : 1000;
    collection->chunks = (CA_RawChunk*)calloc(collection->capacity, sizeof(CA_RawChunk));
    if (!collection->chunks) {
        free(json);
        return -1;
    }

    if (parse_nested_superchunks(json, "superchunks", collection, &chunk_idx) <= 0) {
        parse_nested_superchunks(json, "terrain_superchunks", collection, &chunk_idx);
        if (sdk_chunk_save_json_find_array(json, "wall_chunks", &chunks_start, &chunks_end)) {
            parse_chunks_from_array(chunks_start, chunks_end, collection, &chunk_idx);
        }
    }
    if (chunk_idx == 0 && sdk_chunk_save_json_find_array(json, "chunks", &chunks_start, &chunks_end)) {
        parse_chunks_from_array(chunks_start, chunks_end, collection, &chunk_idx);
    }

    free(json);
    if (chunk_idx <= 0) {
        ca_free_collection(collection);
        return -1;
    }
    return 0;
}

void ca_free_collection(CA_ChunkCollection* collection) {
    if (collection->chunks) {
        free(collection->chunks);
        collection->chunks = NULL;
    }
    collection->count = 0;
    collection->capacity = 0;
}

/* ============================================================================
 * Statistical Analysis
 * ============================================================================ */

int ca_analyze_block_stats(const CA_RawChunk* chunk, CA_BlockStats* stats) {
    if (!chunk || !stats) return -1;
    
    memset(stats, 0, sizeof(CA_BlockStats));
    
    /* Count all blocks */
    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                uint16_t block = chunk->blocks[x][z][y];
                if (block < 65536) {
                    stats->block_counts[block]++;
                    if (block != 0) {
                        stats->non_air_count++;
                    }
                }
            }
        }
    }
    
    /* Build palette */
    for (int i = 0; i < 65536; i++) {
        if (stats->block_counts[i] > 0) {
            stats->palette[stats->palette_size++] = (uint16_t)i;
        }
    }
    
    /* Calculate entropy */
    stats->entropy = ca_calculate_entropy(stats->block_counts, 
                                          stats->non_air_count + stats->block_counts[0]);
    
    return 0;
}

double ca_calculate_entropy(const uint32_t* counts, uint32_t total) {
    if (total == 0) return 0.0;
    
    double entropy = 0.0;
    
    for (int i = 0; i < 65536; i++) {
        if (counts[i] > 0) {
            double p = (double)counts[i] / (double)total;
            entropy -= p * log2(p);
        }
    }
    
    return entropy;
}

int ca_analyze_layer_stats(const CA_RawChunk* chunk, CA_LayerStats* stats) {
    if (!chunk || !stats) return -1;
    
    memset(stats, 0, sizeof(CA_LayerStats));
    
    uint32_t layer_counts[65536];
    
    for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
        memset(layer_counts, 0, sizeof(layer_counts));
        uint32_t layer_total = 0;
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                uint16_t block = chunk->blocks[x][z][y];
                layer_counts[block]++;
                layer_total++;
                
                if (block == 0) {
                    stats->air_per_layer[y]++;
                } else {
                    stats->blocks_per_layer[y]++;
                }
            }
        }
        
        stats->layer_entropy[y] = ca_calculate_entropy(layer_counts, layer_total);
    }
    
    /* Calculate surface height (highest non-air block per column) */
    double total_height = 0.0;
    int height_samples = 0;
    
    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
            uint16_t max_y = 0;
            for (int y = CA_CHUNK_SIZE_Y - 1; y >= 0; y--) {
                if (chunk->blocks[x][z][y] != 0) {
                    max_y = (uint16_t)y;
                    break;
                }
            }
            stats->surface_height[x][z] = max_y;
            total_height += max_y;
            height_samples++;
        }
    }
    
    stats->avg_surface_height = height_samples > 0 ? total_height / height_samples : 0;
    
    /* Calculate surface variance */
    double variance_sum = 0.0;
    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
            double diff = stats->surface_height[x][z] - stats->avg_surface_height;
            variance_sum += diff * diff;
        }
    }
    stats->surface_variance = height_samples > 0 ? variance_sum / height_samples : 0;
    
    return 0;
}

double ca_estimate_compression_potential(const CA_BlockStats* stats, const CA_LayerStats* layer_stats) {
    if (!stats || stats->palette_size == 0) return 0.0;
    
    double score = 100.0;
    uint32_t total_blocks = CA_CHUNK_TOTAL_BLOCKS;
    
    /* Factor 1: Air ratio (more air = better compression) */
    double air_ratio = (double)(total_blocks - stats->non_air_count) / (double)total_blocks;
    score *= (0.5 + 0.5 * air_ratio);  /* 0.5-1.0 multiplier based on air */
    
    /* Factor 2: Palette size (smaller palette = better compression) */
    double palette_ratio = (double)stats->palette_size / 256.0;  /* Normalize to 256 */
    if (palette_ratio > 1.0) palette_ratio = 1.0;
    score *= (1.0 - 0.3 * palette_ratio);  /* Up to 30% penalty for large palettes */
    
    /* Factor 3: Entropy (lower entropy = more compressible) */
    /* Max entropy for 16-bit values is 16, typical is 8-12 */
    double entropy_factor = stats->entropy / 16.0;
    if (entropy_factor > 1.0) entropy_factor = 1.0;
    score *= (1.0 - 0.2 * entropy_factor);  /* Up to 20% penalty for high entropy */
    
    /* Factor 4: Layer consistency (consistent layers = better compression) */
    if (layer_stats) {
        double avg_layer_entropy = 0.0;
        for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
            avg_layer_entropy += layer_stats->layer_entropy[y];
        }
        avg_layer_entropy /= CA_CHUNK_SIZE_Y;
        
        double layer_factor = avg_layer_entropy / 16.0;
        if (layer_factor > 1.0) layer_factor = 1.0;
        score *= (1.0 - 0.15 * layer_factor);  /* Up to 15% penalty */
    }
    
    /* Clamp to 0-100 */
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    
    return score;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char* ca_compression_method_name(int method_id) {
    switch (method_id) {
        case 0: return "Volume+Layer";
        case 1: return "Template";
        case 2: return "Octree";
        case 3: return "Delta Template";
        case 4: return "Inter-Chunk Delta";
        case 5: return "Auto-Select";
        case 6: return "Bit-Pack";
        case 7: return "Sparse Column";
        case 8: return "RLE+Bitmask";
        case 9: return "Hierarchical RLE V2";
        case 10: return "Superchunk Hierarchical V3";
        default: return "Unknown";
    }
}

int ca_recommend_compression_method(const CA_ChunkAnalysis* analysis) {
    if (!analysis) return 5;  /* Auto-select as default */
    
    const CA_BlockStats* stats = &analysis->block_stats;
    const CA_LayerStats* layers = &analysis->layer_stats;
    
    /* High air ratio suggests sparse methods */
    double air_ratio = 1.0 - ((double)stats->non_air_count / (double)CA_CHUNK_TOTAL_BLOCKS);
    
    if (air_ratio > 0.9) {
        /* Very sparse - use sparse column or RLE */
        if (layers && layers->avg_surface_height > 500) {
            return 7;  /* Sparse Column for terrain with clear surface */
        }
        return 8;  /* RLE+Bitmask */
    }
    
    /* Small palette suggests bit-pack */
    if (stats->palette_size <= 64) {
        return 6;  /* Bit-Pack */
    }
    
    /* High entropy suggests hierarchical RLE for complex patterns */
    if (stats->entropy > 10.0) {
        return 9;  /* Hierarchical RLE V2 */
    }
    
    /* Default to hierarchical RLE for good general performance */
    return 9;
}

/* ============================================================================
 * Single Chunk Analysis
 * ============================================================================ */

int ca_analyze_single_chunk(const CA_RawChunk* chunk, const CA_AnalysisConfig* config, 
                            CA_ChunkAnalysis* analysis) {
    if (!chunk || !analysis) return -1;
    
    memset(analysis, 0, sizeof(CA_ChunkAnalysis));
    analysis->chunk_idx = -1;  /* Will be set by caller */
    
    /* Copy basic info */
    /* Note: chunk pointer is const, so we can't copy from it directly */
    /* The caller should set chunk_idx appropriately */
    
    /* Statistical analysis */
    if (config->do_statistical) {
        ca_analyze_block_stats(chunk, &analysis->block_stats);
        ca_analyze_layer_stats(chunk, &analysis->layer_stats);
        analysis->estimated_compression_ratio = 
            ca_estimate_compression_potential(&analysis->block_stats, &analysis->layer_stats);
    }
    
    /* Geometric analysis */
    if (config->do_geometric) {
        ca_detect_volumes(chunk, &analysis->volumes);
        ca_identify_layers(chunk, &analysis->layers);
        ca_match_templates(chunk, &analysis->best_template, 1);
    }
    
    /* Recommendation */
    analysis->recommended_compression_method = ca_recommend_compression_method(analysis);
    
    return 0;
}

void ca_free_volume_list(CA_VolumeList* volumes) {
    if (volumes && volumes->volumes) {
        free(volumes->volumes);
        volumes->volumes = NULL;
        volumes->count = 0;
        volumes->capacity = 0;
    }
}

void ca_free_layer_list(CA_LayerList* layers) {
    if (layers && layers->layers) {
        free(layers->layers);
        layers->layers = NULL;
        layers->count = 0;
        layers->capacity = 0;
    }
}

/* ============================================================================
 * Export Functions
 * ============================================================================ */

int ca_export_csv_stats(const CA_AnalysisReport* report, const char* filename) {
    if (!report || !filename) return -1;
    
    FILE* f = fopen(filename, "w");
    if (!f) return -1;
    
    /* Header */
    fprintf(f, "chunk_idx,cx,cz,top_y,palette_size,non_air_count,air_count,");
    fprintf(f, "entropy,compression_potential,recommended_method\n");
    
    /* Data rows */
    for (int i = 0; i < report->count; i++) {
        const CA_ChunkAnalysis* a = &report->analyses[i];
        fprintf(f, "%d,0,0,%d,%u,%u,%u,%.4f,%.2f,%d\n",
                i,
                0,  /* top_y placeholder */
                a->block_stats.palette_size,
                a->block_stats.non_air_count,
                CA_CHUNK_TOTAL_BLOCKS - a->block_stats.non_air_count,
                a->block_stats.entropy,
                a->estimated_compression_ratio,
                a->recommended_compression_method);
    }
    
    fclose(f);
    return 0;
}

int ca_export_json_report(const CA_AnalysisReport* report, const char* filename) {
    if (!report || !filename) return -1;
    
    FILE* f = fopen(filename, "w");
    if (!f) return -1;
    
    fprintf(f, "{\n");
    fprintf(f, "  \"summary\": {\n");
    fprintf(f, "    \"total_chunks\": %d,\n", report->count);
    fprintf(f, "    \"total_blocks\": %u,\n", report->total_blocks);
    fprintf(f, "    \"total_non_air\": %u,\n", report->total_non_air);
    fprintf(f, "    \"avg_entropy\": %.4f,\n", report->avg_entropy);
    fprintf(f, "    \"avg_compression_potential\": %.2f\n", report->avg_compression_potential);
    fprintf(f, "  },\n");
    
    fprintf(f, "  \"chunks\": [\n");
    
    for (int i = 0; i < report->count; i++) {
        const CA_ChunkAnalysis* a = &report->analyses[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"index\": %d,\n", i);
        fprintf(f, "      \"palette_size\": %u,\n", a->block_stats.palette_size);
        fprintf(f, "      \"non_air_count\": %u,\n", a->block_stats.non_air_count);
        fprintf(f, "      \"entropy\": %.4f,\n", a->block_stats.entropy);
        fprintf(f, "      \"compression_potential\": %.2f,\n", a->estimated_compression_ratio);
        fprintf(f, "      \"recommended_method\": \"%s\",\n", 
                ca_compression_method_name(a->recommended_compression_method));
        fprintf(f, "      \"recommended_method_id\": %d\n", a->recommended_compression_method);
        
        if (i < report->count - 1) {
            fprintf(f, "    },\n");
        } else {
            fprintf(f, "    }\n");
        }
    }
    
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    
    fclose(f);
    return 0;
}

/* ============================================================================
 * Geometric Analysis Implementation
 * ============================================================================ */

int ca_detect_volumes(const CA_RawChunk* chunk, CA_VolumeList* volumes) {
    if (!chunk || !volumes) return -1;
    
    volumes->count = 0;
    volumes->capacity = 100;
    volumes->volumes = (CA_Volume*)calloc(volumes->capacity, sizeof(CA_Volume));
    volumes->total_volume_blocks = 0;
    volumes->large_volumes = 0;
    volumes->medium_volumes = 0;
    volumes->small_volumes = 0;
    
    if (!volumes->volumes) return -1;
    
    /* Simple volume detection - find contiguous regions in Y-slices */
    for (int y = 0; y < CA_CHUNK_SIZE_Y && volumes->count < volumes->capacity; y += 16) {
        uint16_t dominant_block = 0;
        uint32_t block_count = 0;
        
        /* Count blocks in this Y-slice */
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int ly = y; ly < y + 16 && ly < CA_CHUNK_SIZE_Y; ly++) {
                    uint16_t block = chunk->blocks[x][z][ly];
                    if (block != 0) {  /* Skip air */
                        dominant_block = block;
                        block_count++;
                    }
                }
            }
        }
        
        if (block_count > 100) {  /* Only count significant volumes */
            CA_Volume* vol = &volumes->volumes[volumes->count];
            vol->x1 = 0;
            vol->y1 = y;
            vol->z1 = 0;
            vol->x2 = CA_CHUNK_SIZE_X - 1;
            vol->y2 = (y + 15 < CA_CHUNK_SIZE_Y) ? y + 15 : CA_CHUNK_SIZE_Y - 1;
            vol->z2 = CA_CHUNK_SIZE_Z - 1;
            vol->block_id = dominant_block;
            vol->volume = block_count;
            vol->is_rectangular = true;
            
            volumes->total_volume_blocks += block_count;
            if (block_count > 1000) volumes->large_volumes++;
            else if (block_count > 100) volumes->medium_volumes++;
            else volumes->small_volumes++;
            
            volumes->count++;
        }
    }
    
    return 0;
}

int ca_identify_layers(const CA_RawChunk* chunk, CA_LayerList* layers) {
    if (!chunk || !layers) return -1;
    
    layers->count = 0;
    layers->capacity = 64;  /* Max layers (1024 / 16) */
    layers->layers = (CA_Layer*)calloc(layers->capacity, sizeof(CA_Layer));
    
    if (!layers->layers) return -1;
    
    /* Identify layers by grouping consecutive Y-levels with similar properties */
    for (int y = 0; y < CA_CHUNK_SIZE_Y; y += 16) {
        if (layers->count >= layers->capacity) break;
        
        CA_Layer* layer = &layers->layers[layers->count];
        layer->y_start = y;
        layer->y_end = (y + 15 < CA_CHUNK_SIZE_Y) ? y + 15 : CA_CHUNK_SIZE_Y - 1;
        
        /* Find dominant block in this layer */
        uint32_t block_counts[65536] = {0};
        uint32_t total_blocks = 0;
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int ly = layer->y_start; ly <= layer->y_end; ly++) {
                    uint16_t block = chunk->blocks[x][z][ly];
                    block_counts[block]++;
                    total_blocks++;
                }
            }
        }
        
        /* Find most common block */
        uint32_t max_count = 0;
        uint16_t dominant_block = 0;
        for (int i = 0; i < 65536; i++) {
            if (block_counts[i] > max_count) {
                max_count = block_counts[i];
                dominant_block = (uint16_t)i;
            }
        }
        
        layer->dominant_block = dominant_block;
        layer->block_count = max_count;
        layer->uniformity = total_blocks > 0 ? (double)max_count / total_blocks : 0.0;
        
        layers->count++;
    }
    
    return 0;
}

int ca_match_templates(const CA_RawChunk* chunk, CA_TemplateMatch* matches, int max_matches) {
    if (!chunk || !matches || max_matches <= 0) return -1;
    
    /* Placeholder for template matching */
    /* Would need terrain templates from ChunkCompression */
    for (int i = 0; i < max_matches; i++) {
        matches[i].name[0] = '\0';
        matches[i].match_score = 0.0f;
        matches[i].template_id = 0;
    }
    
    return 0;
}

/* ============================================================================
 * Similarity Analysis Implementation
 * ============================================================================ */

float ca_calculate_similarity(const CA_RawChunk* chunk1, const CA_RawChunk* chunk2, uint32_t* out_diff_count) {
    if (!chunk1 || !chunk2) return 0.0f;
    
    uint32_t total_blocks = CA_CHUNK_TOTAL_BLOCKS;
    uint32_t matching_blocks = 0;
    uint32_t diff_count = 0;
    
    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                if (chunk1->blocks[x][z][y] == chunk2->blocks[x][z][y]) {
                    matching_blocks++;
                } else {
                    diff_count++;
                }
            }
        }
    }
    
    if (out_diff_count) *out_diff_count = diff_count;
    
    return total_blocks > 0 ? (float)matching_blocks / (float)total_blocks : 0.0f;
}

float ca_calculate_structural_similarity(const CA_RawChunk* chunk1, const CA_RawChunk* chunk2) {
    if (!chunk1 || !chunk2) return 0.0f;
    
    /* Structural similarity: compare air/non-air patterns only */
    uint32_t total_blocks = CA_CHUNK_TOTAL_BLOCKS;
    uint32_t matching_structure = 0;
    
    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                bool c1_air = (chunk1->blocks[x][z][y] == 0);
                bool c2_air = (chunk2->blocks[x][z][y] == 0);
                if (c1_air == c2_air) {
                    matching_structure++;
                }
            }
        }
    }
    
    return total_blocks > 0 ? (float)matching_structure / (float)total_blocks : 0.0f;
}

int ca_build_similarity_matrix(const CA_ChunkCollection* collection, float** out_matrix) {
    if (!collection || !out_matrix) return -1;
    
    int count = collection->count;
    if (count == 0) return -1;
    
    float* matrix = (float*)malloc(count * count * sizeof(float));
    if (!matrix) return -1;
    
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < count; j++) {
            if (i == j) {
                matrix[i * count + j] = 1.0f;
            } else {
                matrix[i * count + j] = ca_calculate_similarity(&collection->chunks[i], &collection->chunks[j], NULL);
            }
        }
    }
    
    *out_matrix = matrix;
    return 0;
}

int ca_find_most_similar_pairs(const CA_ChunkCollection* collection, const float* similarity_matrix,
                                int* most_similar_indices, float* max_similarities) {
    if (!collection || !similarity_matrix || !most_similar_indices || !max_similarities) return -1;
    
    int count = collection->count;
    for (int i = 0; i < count; i++) {
        most_similar_indices[i] = -1;
        max_similarities[i] = 0.0f;
        
        for (int j = 0; j < count; j++) {
            if (i != j) {
                float sim = similarity_matrix[i * count + j];
                if (sim > max_similarities[i]) {
                    max_similarities[i] = sim;
                    most_similar_indices[i] = j;
                }
            }
        }
    }
    
    return 0;
}

int ca_cluster_chunks(const CA_ChunkCollection* collection, const float* similarity_matrix,
                      int num_clusters, CA_ClusterAssignment* assignments, CA_ClusterInfo* cluster_info) {
    if (!collection || !similarity_matrix || !assignments || !cluster_info) return -1;
    
    int count = collection->count;
    
    /* Simple clustering: assign to cluster based on most similar neighbor */
    /* This is a placeholder - could use k-means or hierarchical clustering */
    for (int i = 0; i < count; i++) {
        assignments[i].chunk_idx = i;
        assignments[i].cluster_id = i % num_clusters;  /* Round-robin for now */
        assignments[i].distance_to_centroid = 0.0f;
    }
    
    /* Initialize cluster info */
    for (int i = 0; i < num_clusters; i++) {
        cluster_info[i].cluster_id = i;
        cluster_info[i].chunk_count = 0;
        cluster_info[i].avg_entropy = 0.0f;
        cluster_info[i].dominant_block = 0;
        snprintf(cluster_info[i].description, sizeof(cluster_info[i].description), "Cluster %d", i);
    }
    
    /* Count chunks per cluster */
    for (int i = 0; i < count; i++) {
        int cluster_id = assignments[i].cluster_id;
        if (cluster_id >= 0 && cluster_id < num_clusters) {
            cluster_info[cluster_id].chunk_count++;
        }
    }
    
    return 0;
}

/* ============================================================================
 * Block Type Distribution Analysis (for ID scope optimization)
 * ============================================================================ */

#define WALL_THRESHOLD 0.95f

static inline int ca_is_wall_signature_material(uint16_t material)
{
    switch (material) {
        case BLOCK_STONE_BRICKS:
        case BLOCK_COBBLESTONE:
        case BLOCK_CRUSHED_STONE:
        case BLOCK_COMPACTED_FILL:
            return 1;
        default:
            return 0;
    }
}

/* Check if chunk is a wall chunk (mostly wall-signature construction materials) */
int ca_detect_wall_chunks(const CA_ChunkCollection* collection, bool* is_wall) {
    if (!collection || !is_wall) return -1;
    
    for (int i = 0; i < collection->count; i++) {
        const CA_RawChunk* chunk = &collection->chunks[i];
        
        uint32_t stone_brick_count = 0;
        uint32_t total_blocks = 0;
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    uint16_t block = chunk->blocks[x][z][y];
                    if (block != 0) {
                        total_blocks++;
                        if (ca_is_wall_signature_material(block)) {
                            stone_brick_count++;
                        }
                    }
                }
            }
        }
        
        if (total_blocks == 0) {
            is_wall[i] = false;
        } else {
            float ratio = (float)stone_brick_count / (float)total_blocks;
            is_wall[i] = (ratio >= WALL_THRESHOLD) ? true : false;
        }
    }
    
    return 0;
}

/* Detect superchunks from wall chunks (16x16 areas between walls) */
int ca_detect_superchunks(const CA_ChunkCollection* collection, const bool* is_wall, 
                          CA_Superchunk* superchunks, int max_superchunks) {
    if (!collection || !is_wall || !superchunks) return -1;
    
    int superchunk_count = 0;
    
    /* Group wall chunks by coordinates to find grid lines */
    /* Simplified approach: find wall chunks and infer 16x16 areas */
    /* Grid spacing: wall(1) + terrain(16) + gap(1) = 18 chunks */
    
    /* Find all wall chunk coordinates */
    typedef struct {
        int32_t cx, cz;
        int chunk_idx;
    } WallCoord;
    
    WallCoord* walls = (WallCoord*)malloc(collection->count * sizeof(WallCoord));
    int wall_count = 0;
    
    for (int i = 0; i < collection->count; i++) {
        if (is_wall[i]) {
            walls[wall_count].cx = collection->chunks[i].cx;
            walls[wall_count].cz = collection->chunks[i].cz;
            walls[wall_count].chunk_idx = i;
            wall_count++;
        }
    }
    
    if (wall_count == 0) {
        free(walls);
        return 0;  /* No superchunks without walls */
    }
    
    /* Find unique X and Z coordinates of walls */
    int32_t* unique_x = (int32_t*)malloc(wall_count * sizeof(int32_t));
    int32_t* unique_z = (int32_t*)malloc(wall_count * sizeof(int32_t));
    int unique_x_count = 0;
    int unique_z_count = 0;
    
    for (int i = 0; i < wall_count; i++) {
        /* Check if X is already in list */
        int found = 0;
        for (int j = 0; j < unique_x_count; j++) {
            if (unique_x[j] == walls[i].cx) {
                found = 1;
                break;
            }
        }
        if (!found) {
            unique_x[unique_x_count++] = walls[i].cx;
        }
        
        /* Check if Z is already in list */
        found = 0;
        for (int j = 0; j < unique_z_count; j++) {
            if (unique_z[j] == walls[i].cz) {
                found = 1;
                break;
            }
        }
        if (!found) {
            unique_z[unique_z_count++] = walls[i].cz;
        }
    }
    
    /* Sort coordinates */
    for (int i = 0; i < unique_x_count - 1; i++) {
        for (int j = i + 1; j < unique_x_count; j++) {
            if (unique_x[i] > unique_x[j]) {
                int32_t temp = unique_x[i];
                unique_x[i] = unique_x[j];
                unique_x[j] = temp;
            }
        }
    }
    for (int i = 0; i < unique_z_count - 1; i++) {
        for (int j = i + 1; j < unique_z_count; j++) {
            if (unique_z[i] > unique_z[j]) {
                int32_t temp = unique_z[i];
                unique_z[i] = unique_z[j];
                unique_z[j] = temp;
            }
        }
    }
    
    /* Detect superchunks between wall lines */
    /* Expected spacing: 18 chunks (1 wall + 16 terrain + 1 gap) */
    for (int xi = 0; xi < unique_x_count - 1; xi++) {
        for (int zi = 0; zi < unique_z_count - 1; zi++) {
            int32_t x1 = unique_x[xi];
            int32_t x2 = unique_x[xi + 1];
            int32_t z1 = unique_z[zi];
            int32_t z2 = unique_z[zi + 1];
            
            int x_spacing = x2 - x1;
            int z_spacing = z2 - z1;
            
            /* Check if spacing matches expected pattern (allow some tolerance) */
            if (x_spacing >= 16 && x_spacing <= 20 && z_spacing >= 16 && z_spacing <= 20) {
                /* Found potential superchunk */
                if (superchunk_count < max_superchunks) {
                    superchunks[superchunk_count].cx_min = x1 + 1;
                    superchunks[superchunk_count].cz_min = z1 + 1;
                    superchunks[superchunk_count].cx_max = x2 - 1;
                    superchunks[superchunk_count].cz_max = z2 - 1;
                    
                    /* Find chunks in this superchunk */
                    int capacity = (x2 - x1) * (z2 - z1);
                    superchunks[superchunk_count].chunk_indices = (int*)malloc(capacity * sizeof(int));
                    superchunks[superchunk_count].chunk_count = 0;
                    
                    for (int i = 0; i < collection->count; i++) {
                        int32_t cx = collection->chunks[i].cx;
                        int32_t cz = collection->chunks[i].cz;
                        
                        if (cx >= x1 + 1 && cx <= x2 - 1 && cz >= z1 + 1 && cz <= z2 - 1) {
                            if (superchunks[superchunk_count].chunk_count < capacity) {
                                superchunks[superchunk_count].chunk_indices[superchunks[superchunk_count].chunk_count++] = i;
                            }
                        }
                    }
                    
                    superchunk_count++;
                }
            }
        }
    }
    
    free(walls);
    free(unique_x);
    free(unique_z);
    
    return superchunk_count;
}

void ca_free_superchunks(CA_Superchunk* superchunks, int count) {
    if (!superchunks) return;
    
    for (int i = 0; i < count; i++) {
        if (superchunks[i].chunk_indices) {
            free(superchunks[i].chunk_indices);
        }
    }
}

/* Count unique block types in a single chunk */
int ca_count_block_types_chunk(const CA_RawChunk* chunk, uint32_t* out_count) {
    if (!chunk || !out_count) return -1;
    
    bool block_seen[65536] = {false};
    uint32_t count = 0;
    
    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                uint16_t block = chunk->blocks[x][z][y];
                if (!block_seen[block]) {
                    block_seen[block] = true;
                    count++;
                }
            }
        }
    }
    
    *out_count = count;
    return 0;
}

/* Count unique block types in a superchunk */
int ca_count_block_types_superchunk(const CA_ChunkCollection* collection, const CA_Superchunk* sc, 
                                    uint32_t* out_count) {
    if (!collection || !sc || !out_count) return -1;
    
    bool block_seen[65536] = {false};
    uint32_t count = 0;
    
    for (int i = 0; i < sc->chunk_count; i++) {
        int chunk_idx = sc->chunk_indices[i];
        const CA_RawChunk* chunk = &collection->chunks[chunk_idx];
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    uint16_t block = chunk->blocks[x][z][y];
                    if (!block_seen[block]) {
                        block_seen[block] = true;
                        count++;
                    }
                }
            }
        }
    }
    
    *out_count = count;
    return 0;
}

/* Count unique block types in a rectangular chunk range */
int ca_count_block_types_range(const CA_ChunkCollection* collection, int cx1, int cz1, 
                               int cx2, int cz2, uint32_t* out_count) {
    if (!collection || !out_count) return -1;
    
    /* Ensure cx1 <= cx2 and cz1 <= cz2 */
    if (cx1 > cx2) { int temp = cx1; cx1 = cx2; cx2 = temp; }
    if (cz1 > cz2) { int temp = cz1; cz1 = cz2; cz2 = temp; }
    
    bool block_seen[65536] = {false};
    uint32_t count = 0;
    
    for (int i = 0; i < collection->count; i++) {
        int32_t cx = collection->chunks[i].cx;
        int32_t cz = collection->chunks[i].cz;
        
        if (cx >= cx1 && cx <= cx2 && cz >= cz1 && cz <= cz2) {
            const CA_RawChunk* chunk = &collection->chunks[i];
            
            for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
                for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                    for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                        uint16_t block = chunk->blocks[x][z][y];
                        if (!block_seen[block]) {
                            block_seen[block] = true;
                            count++;
                        }
                    }
                }
            }
        }
    }
    
    *out_count = count;
    return 0;
}

/* Count block types shared between chunks in a superchunk */
int ca_count_shared_block_types_superchunk(const CA_ChunkCollection* collection, const CA_Superchunk* sc,
                                           uint32_t* out_shared_count) {
    if (!collection || !sc || !out_shared_count) return -1;
    
    if (sc->chunk_count < 2) {
        *out_shared_count = 0;
        return 0;
    }
    
    int chunk_count = sc->chunk_count;
    if (chunk_count > 256) chunk_count = 256;  /* Limit to avoid excessive memory */
    
    /* Track which chunks have each block type - use heap allocation to avoid stack overflow */
    bool* block_in_chunk = (bool*)calloc(65536 * chunk_count, sizeof(bool));
    if (!block_in_chunk) return -1;
    
    for (int i = 0; i < chunk_count; i++) {
        int chunk_idx = sc->chunk_indices[i];
        const CA_RawChunk* chunk = &collection->chunks[chunk_idx];
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    uint16_t block = chunk->blocks[x][z][y];
                    block_in_chunk[block * chunk_count + i] = true;
                }
            }
        }
    }
    
    /* Count block types appearing in multiple chunks */
    uint32_t shared_count = 0;
    for (int b = 0; b < 65536; b++) {
        int chunk_with_block = 0;
        for (int i = 0; i < chunk_count; i++) {
            if (block_in_chunk[b * chunk_count + i]) {
                chunk_with_block++;
            }
        }
        if (chunk_with_block >= 2) {
            shared_count++;
        }
    }
    
    free(block_in_chunk);
    *out_shared_count = shared_count;
    return 0;
}

/* Count block types shared between chunks in a rectangular range */
int ca_count_shared_block_types_range(const CA_ChunkCollection* collection, int cx1, int cz1,
                                     int cx2, int cz2, uint32_t* out_shared_count) {
    if (!collection || !out_shared_count) return -1;
    
    /* Ensure cx1 <= cx2 and cz1 <= cz2 */
    if (cx1 > cx2) { int temp = cx1; cx1 = cx2; cx2 = temp; }
    if (cz1 > cz2) { int temp = cz1; cz1 = cz2; cz2 = temp; }
    
    /* Collect chunk indices in range */
    int chunk_indices[256];
    int chunk_count = 0;
    
    for (int i = 0; i < collection->count && chunk_count < 256; i++) {
        int32_t cx = collection->chunks[i].cx;
        int32_t cz = collection->chunks[i].cz;
        
        if (cx >= cx1 && cx <= cx2 && cz >= cz1 && cz <= cz2) {
            chunk_indices[chunk_count++] = i;
        }
    }
    
    if (chunk_count < 2) {
        *out_shared_count = 0;
        return 0;
    }
    
    /* Track which chunks have each block type - use heap allocation to avoid stack overflow */
    bool* block_in_chunk = (bool*)calloc(65536 * chunk_count, sizeof(bool));
    if (!block_in_chunk) return -1;
    
    for (int i = 0; i < chunk_count; i++) {
        int chunk_idx = chunk_indices[i];
        const CA_RawChunk* chunk = &collection->chunks[chunk_idx];
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    uint16_t block = chunk->blocks[x][z][y];
                    block_in_chunk[block * chunk_count + i] = true;
                }
            }
        }
    }
    
    /* Count block types appearing in multiple chunks */
    uint32_t shared_count = 0;
    for (int b = 0; b < 65536; b++) {
        int chunk_with_block = 0;
        for (int i = 0; i < chunk_count; i++) {
            if (block_in_chunk[b * chunk_count + i]) {
                chunk_with_block++;
            }
        }
        if (chunk_with_block >= 2) {
            shared_count++;
        }
    }
    
    free(block_in_chunk);
    *out_shared_count = shared_count;
    return 0;
}

/* Get block ID distribution for a superchunk */
int ca_block_id_distribution_superchunk(const CA_ChunkCollection* collection, const CA_Superchunk* sc,
                                       uint16_t* out_block_ids, uint32_t* out_chunk_counts, uint32_t* out_count) {
    if (!collection || !sc || !out_block_ids || !out_chunk_counts || !out_count) return -1;
    
    uint32_t block_counts[65536] = {0};
    uint32_t chunk_with_block[65536] = {0};
    
    for (int i = 0; i < sc->chunk_count; i++) {
        int chunk_idx = sc->chunk_indices[i];
        const CA_RawChunk* chunk = &collection->chunks[chunk_idx];
        
        bool chunk_has_block[65536] = {false};
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    uint16_t block = chunk->blocks[x][z][y];
                    block_counts[block]++;
                    chunk_has_block[block] = true;
                }
            }
        }
        
        /* Count chunks that have each block */
        for (int b = 0; b < 65536; b++) {
            if (chunk_has_block[b]) {
                chunk_with_block[b]++;
            }
        }
    }
    
    /* Collect non-zero block IDs */
    uint32_t count = 0;
    for (int b = 0; b < 65536; b++) {
        if (block_counts[b] > 0) {
            out_block_ids[count] = (uint16_t)b;
            out_chunk_counts[count] = chunk_with_block[b];
            count++;
        }
    }
    
    *out_count = count;
    return 0;
}

/* Get block ID distribution for a rectangular range */
int ca_block_id_distribution_range(const CA_ChunkCollection* collection, int cx1, int cz1,
                                 int cx2, int cz2, uint16_t* out_block_ids, uint32_t* out_chunk_counts, uint32_t* out_count) {
    if (!collection || !out_block_ids || !out_chunk_counts || !out_count) return -1;
    
    /* Ensure cx1 <= cx2 and cz1 <= cz2 */
    if (cx1 > cx2) { int temp = cx1; cx1 = cx2; cx2 = temp; }
    if (cz1 > cz2) { int temp = cz1; cz1 = cz2; cz2 = temp; }
    
    /* Collect chunk indices in range */
    int chunk_indices[256];
    int chunk_count = 0;
    
    for (int i = 0; i < collection->count && chunk_count < 256; i++) {
        int32_t cx = collection->chunks[i].cx;
        int32_t cz = collection->chunks[i].cz;
        
        if (cx >= cx1 && cx <= cx2 && cz >= cz1 && cz <= cz2) {
            chunk_indices[chunk_count++] = i;
        }
    }
    
    if (chunk_count == 0) {
        *out_count = 0;
        return 0;
    }
    
    /* Count block occurrences and which chunks have each block */
    uint32_t block_counts[65536] = {0};
    bool chunk_has_block[65536] = {false};
    uint32_t chunk_with_block[65536] = {0};
    
    for (int i = 0; i < chunk_count; i++) {
        int chunk_idx = chunk_indices[i];
        const CA_RawChunk* chunk = &collection->chunks[chunk_idx];
        
        /* Reset chunk_has_block for this chunk */
        memset(chunk_has_block, 0, sizeof(chunk_has_block));
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    uint16_t block = chunk->blocks[x][z][y];
                    block_counts[block]++;
                    chunk_has_block[block] = true;
                }
            }
        }
        
        for (int b = 0; b < 65536; b++) {
            if (chunk_has_block[b]) {
                chunk_with_block[b]++;
            }
        }
    }
    
    /* Collect non-zero block IDs */
    uint32_t count = 0;
    for (int b = 0; b < 65536; b++) {
        if (block_counts[b] > 0) {
            out_block_ids[count] = (uint16_t)b;
            out_chunk_counts[count] = chunk_with_block[b];
            count++;
        }
    }
    
    *out_count = count;
    return 0;
}

/* ============================================================================
 * Kernel-Based Pattern Analysis Implementation
 * ============================================================================ */

#define CA_MAX_KERNELS 64

static CA_Kernel g_kernel_registry[CA_MAX_KERNELS];
static int g_kernel_count = 0;

int ca_register_kernel(const CA_Kernel* kernel) {
    if (!kernel || g_kernel_count >= CA_MAX_KERNELS) return -1;
    
    /* Allocate and copy pattern to heap */
    g_kernel_registry[g_kernel_count] = *kernel;
    g_kernel_registry[g_kernel_count].pattern = (bool*)calloc(kernel->pattern_size, sizeof(bool));
    if (!g_kernel_registry[g_kernel_count].pattern) return -1;
    
    for (int i = 0; i < kernel->pattern_size; i++) {
        g_kernel_registry[g_kernel_count].pattern[i] = kernel->pattern[i];
    }
    
    g_kernel_count++;
    return g_kernel_count - 1;
}

const CA_Kernel* ca_get_kernel_by_name(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < g_kernel_count; i++) {
        if (strcmp(g_kernel_registry[i].name, name) == 0) {
            return &g_kernel_registry[i];
        }
    }
    return NULL;
}

int ca_get_all_kernels(const CA_Kernel** out_kernels, int max_kernels) {
    if (!out_kernels) return -1;
    int count = g_kernel_count < max_kernels ? g_kernel_count : max_kernels;
    for (int i = 0; i < count; i++) {
        out_kernels[i] = &g_kernel_registry[i];
    }
    return count;
}

void ca_init_default_kernels(void) {
    CA_Kernel k;
    memset(&k, 0, sizeof(k));
    k.type = KERNEL_TYPE_SPATIAL;
    
    /* 3x3 Cross */
    strcpy_s(k.name, sizeof(k.name), "cross_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 cross pattern (center + cardinal directions)");
    k.size = 3;
    k.pattern_size = 9;
    bool cross_3x3_pattern[9] = {false, true, false, true, true, true, false, true, false};
    k.pattern = cross_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 3x3 Checkerboard */
    strcpy_s(k.name, sizeof(k.name), "checkerboard_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 checkerboard pattern");
    k.size = 3;
    k.pattern_size = 9;
    bool checkerboard_3x3_pattern[9] = {true, false, true, false, true, false, true, false, true};
    k.pattern = checkerboard_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 3x3 Corners */
    strcpy_s(k.name, sizeof(k.name), "corners_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 corners pattern");
    k.size = 3;
    k.pattern_size = 9;
    bool corners_3x3_pattern[9] = {true, false, true, false, false, false, true, false, true};
    k.pattern = corners_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 3x3 Full */
    strcpy_s(k.name, sizeof(k.name), "full_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 full grid");
    k.size = 3;
    k.pattern_size = 9;
    bool full_3x3_pattern[9] = {true, true, true, true, true, true, true, true, true};
    k.pattern = full_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 3x3 Edges */
    strcpy_s(k.name, sizeof(k.name), "edges_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 edges pattern");
    k.size = 3;
    k.pattern_size = 9;
    bool edges_3x3_pattern[9] = {true, true, true, true, false, true, true, true, true};
    k.pattern = edges_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 3x3 Diagonal */
    strcpy_s(k.name, sizeof(k.name), "diagonal_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 main diagonal pattern");
    k.size = 3;
    k.pattern_size = 9;
    bool diagonal_3x3_pattern[9] = {true, false, false, false, true, false, false, false, true};
    k.pattern = diagonal_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 5x5 Cross */
    strcpy_s(k.name, sizeof(k.name), "cross_5x5");
    strcpy_s(k.description, sizeof(k.description), "5x5 cross pattern");
    k.size = 5;
    k.pattern_size = 25;
    bool cross_5x5_pattern[25] = {false, false, true, false, false, false, false, true, false, false,
                                   true, true, true, true, true, false, false, true, false, false,
                                   false, false, true, false, false};
    k.pattern = cross_5x5_pattern;
    ca_register_kernel(&k);
    
    /* 5x5 Checkerboard */
    strcpy_s(k.name, sizeof(k.name), "checkerboard_5x5");
    strcpy_s(k.description, sizeof(k.description), "5x5 checkerboard pattern");
    k.size = 5;
    k.pattern_size = 25;
    bool checkerboard_5x5_pattern[25];
    for (int i = 0; i < 25; i++) {
        int row = i / 5;
        int col = i % 5;
        checkerboard_5x5_pattern[i] = ((row + col) % 2 == 0);
    }
    k.pattern = checkerboard_5x5_pattern;
    ca_register_kernel(&k);
    
    /* 5x5 Full */
    strcpy_s(k.name, sizeof(k.name), "full_5x5");
    strcpy_s(k.description, sizeof(k.description), "5x5 full grid");
    k.size = 5;
    k.pattern_size = 25;
    bool full_5x5_pattern[25];
    for (int i = 0; i < 25; i++) full_5x5_pattern[i] = true;
    k.pattern = full_5x5_pattern;
    ca_register_kernel(&k);
    
    /* 3x3 Ring (hollow square) */
    strcpy_s(k.name, sizeof(k.name), "ring_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 ring (edges only, no center)");
    k.size = 3;
    k.pattern_size = 9;
    bool ring_3x3_pattern[9] = {true, true, true, true, false, true, true, true, true};
    k.pattern = ring_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 5x5 Ring */
    strcpy_s(k.name, sizeof(k.name), "ring_5x5");
    strcpy_s(k.description, sizeof(k.description), "5x5 ring (outer edges only)");
    k.size = 5;
    k.pattern_size = 25;
    bool ring_5x5_pattern[25];
    for (int i = 0; i < 25; i++) {
        int row = i / 5;
        int col = i % 5;
        ring_5x5_pattern[i] = (row == 0 || row == 4 || col == 0 || col == 4);
    }
    k.pattern = ring_5x5_pattern;
    ca_register_kernel(&k);
    
    /* 3x3 Plus (same as cross, but explicitly named) */
    strcpy_s(k.name, sizeof(k.name), "plus_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 plus pattern");
    k.size = 3;
    k.pattern_size = 9;
    bool plus_3x3_pattern[9] = {false, true, false, true, true, true, false, true, false};
    k.pattern = plus_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 5x5 Plus */
    strcpy_s(k.name, sizeof(k.name), "plus_5x5");
    strcpy_s(k.description, sizeof(k.description), "5x5 plus pattern");
    k.size = 5;
    k.pattern_size = 25;
    bool plus_5x5_pattern[25];
    for (int i = 0; i < 25; i++) {
        int row = i / 5;
        int col = i % 5;
        plus_5x5_pattern[i] = (row == 2 || col == 2);
    }
    k.pattern = plus_5x5_pattern;
    ca_register_kernel(&k);
    
    /* 3x3 X (diagonal cross) */
    strcpy_s(k.name, sizeof(k.name), "x_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 X pattern (both diagonals)");
    k.size = 3;
    k.pattern_size = 9;
    bool x_3x3_pattern[9] = {true, false, true, false, true, false, true, false, true};
    k.pattern = x_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 5x5 X */
    strcpy_s(k.name, sizeof(k.name), "x_5x5");
    strcpy_s(k.description, sizeof(k.description), "5x5 X pattern (both diagonals)");
    k.size = 5;
    k.pattern_size = 25;
    bool x_5x5_pattern[25];
    for (int i = 0; i < 25; i++) {
        int row = i / 5;
        int col = i % 5;
        x_5x5_pattern[i] = (row == col || row + col == 4);
    }
    k.pattern = x_5x5_pattern;
    ca_register_kernel(&k);
    
    /* 3x3 Box (same as ring, but explicitly named) */
    strcpy_s(k.name, sizeof(k.name), "box_3x3");
    strcpy_s(k.description, sizeof(k.description), "3x3 box pattern");
    k.size = 3;
    k.pattern_size = 9;
    bool box_3x3_pattern[9] = {true, true, true, true, false, true, true, true, true};
    k.pattern = box_3x3_pattern;
    ca_register_kernel(&k);
    
    /* 5x5 Box */
    strcpy_s(k.name, sizeof(k.name), "box_5x5");
    strcpy_s(k.description, sizeof(k.description), "5x5 box pattern");
    k.size = 5;
    k.pattern_size = 25;
    bool box_5x5_pattern[25];
    for (int i = 0; i < 25; i++) {
        int row = i / 5;
        int col = i % 5;
        box_5x5_pattern[i] = (row == 0 || row == 4 || col == 0 || col == 4);
    }
    k.pattern = box_5x5_pattern;
    ca_register_kernel(&k);
    
    /* Frequency-based kernels */
    k.type = KERNEL_TYPE_FREQUENCY;
    k.size = 0;  /* Not applicable for frequency kernels */
    k.pattern = NULL;
    k.pattern_size = 0;
    
    /* Rare blocks (<10% of chunks) */
    strcpy_s(k.name, sizeof(k.name), "rare_10");
    strcpy_s(k.description, sizeof(k.description), "Blocks appearing in <10% of chunks");
    k.threshold = 0.1f;
    ca_register_kernel(&k);
    
    /* Rare blocks (<25% of chunks) */
    strcpy_s(k.name, sizeof(k.name), "rare_25");
    strcpy_s(k.description, sizeof(k.description), "Blocks appearing in <25% of chunks");
    k.threshold = 0.25f;
    ca_register_kernel(&k);
    
    /* Common blocks (>50% of chunks) */
    strcpy_s(k.name, sizeof(k.name), "common_50");
    strcpy_s(k.description, sizeof(k.description), "Blocks appearing in >50% of chunks");
    k.threshold = 0.5f;
    ca_register_kernel(&k);
    
    /* Common blocks (>75% of chunks) */
    strcpy_s(k.name, sizeof(k.name), "common_75");
    strcpy_s(k.description, sizeof(k.description), "Blocks appearing in >75% of chunks");
    k.threshold = 0.75f;
    ca_register_kernel(&k);
    
    /* Unique blocks (exactly 1 chunk) */
    strcpy_s(k.name, sizeof(k.name), "unique");
    strcpy_s(k.description, sizeof(k.description), "Blocks appearing in exactly 1 chunk");
    k.threshold = 0.0f;  /* Not used for unique */
    ca_register_kernel(&k);
    
    /* Dominant blocks (>50% of chunks) */
    strcpy_s(k.name, sizeof(k.name), "dominant_50");
    strcpy_s(k.description, sizeof(k.description), "Dominant blocks appearing in >50% of chunks");
    k.threshold = 0.5f;
    ca_register_kernel(&k);
    
    /* Larger spatial kernels */
    k.type = KERNEL_TYPE_SPATIAL;
    k.pattern = NULL;
    k.pattern_size = 0;
    k.threshold = 0.0f;
    
    /* 7x7 Cross */
    strcpy_s(k.name, sizeof(k.name), "cross_7x7");
    strcpy_s(k.description, sizeof(k.description), "7x7 cross pattern");
    k.size = 7;
    k.pattern_size = 49;
    bool cross_7x7_pattern[49];
    for (int i = 0; i < 49; i++) {
        int row = i / 7;
        int col = i % 7;
        cross_7x7_pattern[i] = (row == 3 || col == 3);
    }
    k.pattern = cross_7x7_pattern;
    ca_register_kernel(&k);
    
    /* 7x7 Full */
    strcpy_s(k.name, sizeof(k.name), "full_7x7");
    strcpy_s(k.description, sizeof(k.description), "7x7 full grid");
    k.size = 7;
    k.pattern_size = 49;
    bool full_7x7_pattern[49];
    for (int i = 0; i < 49; i++) full_7x7_pattern[i] = true;
    k.pattern = full_7x7_pattern;
    ca_register_kernel(&k);
    
    /* 7x7 Ring */
    strcpy_s(k.name, sizeof(k.name), "ring_7x7");
    strcpy_s(k.description, sizeof(k.description), "7x7 ring (outer edges only)");
    k.size = 7;
    k.pattern_size = 49;
    bool ring_7x7_pattern[49];
    for (int i = 0; i < 49; i++) {
        int row = i / 7;
        int col = i % 7;
        ring_7x7_pattern[i] = (row == 0 || row == 6 || col == 0 || col == 6);
    }
    k.pattern = ring_7x7_pattern;
    ca_register_kernel(&k);
    
    /* 7x7 Plus */
    strcpy_s(k.name, sizeof(k.name), "plus_7x7");
    strcpy_s(k.description, sizeof(k.description), "7x7 plus pattern");
    k.size = 7;
    k.pattern_size = 49;
    bool plus_7x7_pattern[49];
    for (int i = 0; i < 49; i++) {
        int row = i / 7;
        int col = i % 7;
        plus_7x7_pattern[i] = (row == 3 || col == 3);
    }
    k.pattern = plus_7x7_pattern;
    ca_register_kernel(&k);
    
    /* 7x7 X */
    strcpy_s(k.name, sizeof(k.name), "x_7x7");
    strcpy_s(k.description, sizeof(k.description), "7x7 X pattern (both diagonals)");
    k.size = 7;
    k.pattern_size = 49;
    bool x_7x7_pattern[49];
    for (int i = 0; i < 49; i++) {
        int row = i / 7;
        int col = i % 7;
        x_7x7_pattern[i] = (row == col || row + col == 6);
    }
    k.pattern = x_7x7_pattern;
    ca_register_kernel(&k);
    
    /* 9x9 Cross */
    strcpy_s(k.name, sizeof(k.name), "cross_9x9");
    strcpy_s(k.description, sizeof(k.description), "9x9 cross pattern");
    k.size = 9;
    k.pattern_size = 81;
    bool cross_9x9_pattern[81];
    for (int i = 0; i < 81; i++) {
        int row = i / 9;
        int col = i % 9;
        cross_9x9_pattern[i] = (row == 4 || col == 4);
    }
    k.pattern = cross_9x9_pattern;
    ca_register_kernel(&k);
    
    /* 9x9 Full */
    strcpy_s(k.name, sizeof(k.name), "full_9x9");
    strcpy_s(k.description, sizeof(k.description), "9x9 full grid");
    k.size = 9;
    k.pattern_size = 81;
    bool full_9x9_pattern[81];
    for (int i = 0; i < 81; i++) full_9x9_pattern[i] = true;
    k.pattern = full_9x9_pattern;
    ca_register_kernel(&k);
    
    /* 9x9 Ring */
    strcpy_s(k.name, sizeof(k.name), "ring_9x9");
    strcpy_s(k.description, sizeof(k.description), "9x9 ring (outer edges only)");
    k.size = 9;
    k.pattern_size = 81;
    bool ring_9x9_pattern[81];
    for (int i = 0; i < 81; i++) {
        int row = i / 9;
        int col = i % 9;
        ring_9x9_pattern[i] = (row == 0 || row == 8 || col == 0 || col == 8);
    }
    k.pattern = ring_9x9_pattern;
    ca_register_kernel(&k);
    
    /* 11x11 Cross */
    strcpy_s(k.name, sizeof(k.name), "cross_11x11");
    strcpy_s(k.description, sizeof(k.description), "11x11 cross pattern");
    k.size = 11;
    k.pattern_size = 121;
    bool cross_11x11_pattern[121];
    for (int i = 0; i < 121; i++) {
        int row = i / 11;
        int col = i % 11;
        cross_11x11_pattern[i] = (row == 5 || col == 5);
    }
    k.pattern = cross_11x11_pattern;
    ca_register_kernel(&k);
    
    /* 11x11 Full */
    strcpy_s(k.name, sizeof(k.name), "full_11x11");
    strcpy_s(k.description, sizeof(k.description), "11x11 full grid");
    k.size = 11;
    k.pattern_size = 121;
    bool full_11x11_pattern[121];
    for (int i = 0; i < 121; i++) full_11x11_pattern[i] = true;
    k.pattern = full_11x11_pattern;
    ca_register_kernel(&k);
    
    /* 13x13 Cross */
    strcpy_s(k.name, sizeof(k.name), "cross_13x13");
    strcpy_s(k.description, sizeof(k.description), "13x13 cross pattern");
    k.size = 13;
    k.pattern_size = 169;
    bool cross_13x13_pattern[169];
    for (int i = 0; i < 169; i++) {
        int row = i / 13;
        int col = i % 13;
        cross_13x13_pattern[i] = (row == 6 || col == 6);
    }
    k.pattern = cross_13x13_pattern;
    ca_register_kernel(&k);
    
    /* 13x13 Full */
    strcpy_s(k.name, sizeof(k.name), "full_13x13");
    strcpy_s(k.description, sizeof(k.description), "13x13 full grid");
    k.size = 13;
    k.pattern_size = 169;
    bool full_13x13_pattern[169];
    for (int i = 0; i < 169; i++) full_13x13_pattern[i] = true;
    k.pattern = full_13x13_pattern;
    ca_register_kernel(&k);
}

int ca_extract_chunk_block_sets(const CA_ChunkCollection* collection, CA_ChunkBlockSet* out_sets) {
    if (!collection || !out_sets) return -1;
    
    for (int i = 0; i < collection->count; i++) {
        const CA_RawChunk* chunk = &collection->chunks[i];
        CA_ChunkBlockSet* set = &out_sets[i];
        
        bool seen[65536] = {false};
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    uint16_t block = chunk->blocks[x][z][y];
                    if (!seen[block]) {
                        seen[block] = true;
                        if (set->count < 256) {
                            set->block_ids[set->count++] = block;
                        }
                    }
                }
            }
        }
    }
    
    return 0;
}

int ca_apply_kernel_to_region(const CA_ChunkCollection* collection, const CA_Kernel* kernel,
                              const CA_ChunkBlockSet* block_sets,
                              int cx1, int cz1, int cx2, int cz2, int stride,
                              CA_KernelAnalysisResult* out_result) {
    if (!collection || !kernel || !block_sets || !out_result) return -1;
    
    if (cx1 > cx2) { int temp = cx1; cx1 = cx2; cx2 = temp; }
    if (cz1 > cz2) { int temp = cz1; cz1 = cz2; cz2 = temp; }
    
    if (stride < 1) stride = 1;
    
    int kernel_ones = 0;
    for (int i = 0; i < kernel->pattern_size; i++) {
        if (kernel->pattern[i]) kernel_ones++;
    }
    if (kernel_ones == 0) return -1;
    
    bool block_in_region[65536] = {false};
    uint32_t region_block_count = 0;
    
    for (int cx = cx1; cx <= cx2; cx++) {
        for (int cz = cz1; cz <= cz2; cz++) {
            for (int i = 0; i < collection->count; i++) {
                if (collection->chunks[i].cx == cx && collection->chunks[i].cz == cz) {
                    const CA_ChunkBlockSet* set = &block_sets[i];
                    for (uint32_t j = 0; j < set->count; j++) {
                        uint16_t block = set->block_ids[j];
                        if (!block_in_region[block]) {
                            block_in_region[block] = true;
                            region_block_count++;
                        }
                    }
                    break;
                }
            }
        }
    }
    
    if (region_block_count == 0) return 0;
    
    out_result->kernel = kernel;
    out_result->results = (CA_KernelBlockResult*)calloc(region_block_count, sizeof(CA_KernelBlockResult));
    if (!out_result->results) return -1;
    
    uint32_t idx = 0;
    for (int b = 0; b < 65536; b++) {
        if (block_in_region[b]) {
            out_result->results[idx].block_type = (uint16_t)b;
            out_result->results[idx].total_activations = 0;
            out_result->results[idx].score = 0.0;
            idx++;
        }
    }
    out_result->result_count = region_block_count;
    
    for (int start_cx = cx1; start_cx <= cx2 - kernel->size + 1; start_cx += stride) {
        for (int start_cz = cz1; start_cz <= cz2 - kernel->size + 1; start_cz += stride) {
            for (int ki = 0; ki < kernel->pattern_size; ki++) {
                if (!kernel->pattern[ki]) continue;
                
                int kx = ki % kernel->size;
                int kz = ki / kernel->size;
                int cx = start_cx + kx;
                int cz = start_cz + kz;
                
                for (int i = 0; i < collection->count; i++) {
                    if (collection->chunks[i].cx == cx && collection->chunks[i].cz == cz) {
                        const CA_ChunkBlockSet* set = &block_sets[i];
                        
                        for (uint32_t j = 0; j < out_result->result_count; j++) {
                            uint16_t block = out_result->results[j].block_type;
                            
                            for (uint32_t k = 0; k < set->count; k++) {
                                if (set->block_ids[k] == block) {
                                    out_result->results[j].total_activations++;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    
    uint32_t total_positions = 0;
    for (int start_cx = cx1; start_cx <= cx2 - kernel->size + 1; start_cx += stride) {
        for (int start_cz = cz1; start_cz <= cz2 - kernel->size + 1; start_cz += stride) {
            total_positions++;
        }
    }
    
    if (total_positions > 0) {
        for (uint32_t j = 0; j < out_result->result_count; j++) {
            out_result->results[j].score = (double)out_result->results[j].total_activations / 
                                           (total_positions * kernel_ones);
        }
    }
    
    return 0;
}

/* Apply frequency kernel to region (non-spatial, statistical) */
int ca_apply_frequency_kernel_to_region(const CA_ChunkCollection* collection, const CA_Kernel* kernel,
                                        const CA_ChunkBlockSet* block_sets,
                                        int cx1, int cz1, int cx2, int cz2,
                                        CA_KernelAnalysisResult* out_result) {
    if (!collection || !kernel || !block_sets || !out_result) return -1;
    if (kernel->type != KERNEL_TYPE_FREQUENCY) return -1;
    
    if (cx1 > cx2) { int temp = cx1; cx1 = cx2; cx2 = temp; }
    if (cz1 > cz2) { int temp = cz1; cz1 = cz2; cz2 = temp; }
    
    /* Count chunk occurrences for each block type */
    uint32_t block_chunk_count[65536] = {0};
    uint32_t total_chunks_in_region = 0;
    
    for (int cx = cx1; cx <= cx2; cx++) {
        for (int cz = cz1; cz <= cz2; cz++) {
            for (int i = 0; i < collection->count; i++) {
                if (collection->chunks[i].cx == cx && collection->chunks[i].cz == cz) {
                    total_chunks_in_region++;
                    const CA_ChunkBlockSet* set = &block_sets[i];
                    if (set) {
                        for (uint32_t j = 0; j < set->count; j++) {
                            uint16_t block = set->block_ids[j];
                            if (block < 65536) {
                                block_chunk_count[block]++;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    
    if (total_chunks_in_region == 0) return 0;
    
    /* Collect block types that meet threshold criteria */
    uint32_t region_block_count = 0;
    bool block_meets_criteria[65536] = {false};
    
    for (int b = 0; b < 65536; b++) {
        if (block_chunk_count[b] == 0) continue;
        
        float frequency = (float)block_chunk_count[b] / total_chunks_in_region;
        
        /* Apply threshold based on kernel name */
        if (strstr(kernel->name, "rare")) {
            /* Rarity: blocks appearing in less than threshold% of chunks */
            if (frequency < kernel->threshold) {
                block_meets_criteria[b] = true;
                region_block_count++;
            }
        } else if (strstr(kernel->name, "common")) {
            /* Commonality: blocks appearing in more than threshold% of chunks */
            if (frequency > kernel->threshold) {
                block_meets_criteria[b] = true;
                region_block_count++;
            }
        } else if (strstr(kernel->name, "unique")) {
            /* Unique: blocks appearing in exactly 1 chunk */
            if (block_chunk_count[b] == 1) {
                block_meets_criteria[b] = true;
                region_block_count++;
            }
        } else if (strstr(kernel->name, "dominant")) {
            /* Dominant: blocks appearing in more than threshold% of chunks */
            if (frequency > kernel->threshold) {
                block_meets_criteria[b] = true;
                region_block_count++;
            }
        } else {
            /* Default: include all blocks */
            block_meets_criteria[b] = true;
            region_block_count++;
        }
    }
    
    if (region_block_count == 0) return 0;
    
    /* Allocate result array */
    out_result->kernel = kernel;
    out_result->results = (CA_KernelBlockResult*)calloc(region_block_count, sizeof(CA_KernelBlockResult));
    if (!out_result->results) return -1;
    
    /* Initialize results */
    uint32_t idx = 0;
    for (int b = 0; b < 65536; b++) {
        if (block_meets_criteria[b]) {
            out_result->results[idx].block_type = (uint16_t)b;
            out_result->results[idx].total_activations = block_chunk_count[b];
            out_result->results[idx].score = (float)block_chunk_count[b] / total_chunks_in_region;
            idx++;
        }
    }
    out_result->result_count = region_block_count;
    
    return 0;
}

void ca_free_kernel_analysis_result(CA_KernelAnalysisResult* result) {
    if (result && result->results) {
        free(result->results);
        result->results = NULL;
        result->result_count = 0;
    }
    if (result && result->subchunk_analysis) {
        ca_free_subchunk_analysis(result->subchunk_analysis);
        free(result->subchunk_analysis);
        result->subchunk_analysis = NULL;
    }
}

int ca_analyze_region_full(const CA_ChunkCollection* collection, int cx1, int cz1, int cx2, int cz2,
                           int kernel_stride, int subchunk_divisor,
                           CA_KernelAnalysisResult** out_results, int* out_result_count) {
    if (!collection || !out_results || !out_result_count) return -1;
    
    ca_init_default_kernels();
    
    CA_ChunkBlockSet* block_sets = (CA_ChunkBlockSet*)calloc(collection->count, sizeof(CA_ChunkBlockSet));
    if (!block_sets) return -1;
    
    int result = ca_extract_chunk_block_sets(collection, block_sets);
    if (result != 0) {
        free(block_sets);
        return result;
    }
    
    /* Extract subchunk data if divisor is specified */
    CA_ChunkSubchunkData* subchunk_data = NULL;
    if (subchunk_divisor > 0) {
        subchunk_data = (CA_ChunkSubchunkData*)calloc(collection->count, sizeof(CA_ChunkSubchunkData));
        if (!subchunk_data) {
            free(block_sets);
            return -1;
        }
        
        for (int i = 0; i < collection->count; i++) {
            result = ca_extract_subchunk_data(collection, subchunk_divisor, &subchunk_data[i]);
            if (result != 0) {
                for (int j = 0; j < i; j++) {
                    ca_free_subchunk_data(&subchunk_data[j], 1);
                }
                free(subchunk_data);
                free(block_sets);
                return result;
            }
        }
    }
    
    const CA_Kernel* kernels[CA_MAX_KERNELS];
    int kernel_count = ca_get_all_kernels(kernels, CA_MAX_KERNELS);
    
    *out_results = (CA_KernelAnalysisResult*)calloc(kernel_count, sizeof(CA_KernelAnalysisResult));
    if (!*out_results) {
        free(block_sets);
        if (subchunk_data) {
            for (int i = 0; i < collection->count; i++) {
                ca_free_subchunk_data(&subchunk_data[i], 1);
            }
            free(subchunk_data);
        }
        return -1;
    }
    
    /* Initialize all results */
    for (int i = 0; i < kernel_count; i++) {
        (*out_results)[i].kernel = NULL;
        (*out_results)[i].results = NULL;
        (*out_results)[i].result_count = 0;
        (*out_results)[i].subchunk_analysis = NULL;
    }
    
    int actual_result_count = 0;
    
    for (int i = 0; i < kernel_count; i++) {
        if (kernels[i]->type == KERNEL_TYPE_SPATIAL) {
            result = ca_apply_kernel_to_region(collection, kernels[i], block_sets,
                                               cx1, cz1, cx2, cz2, kernel_stride,
                                               &(*out_results)[i]);
        } else if (kernels[i]->type == KERNEL_TYPE_FREQUENCY) {
            result = ca_apply_frequency_kernel_to_region(collection, kernels[i], block_sets,
                                                        cx1, cz1, cx2, cz2,
                                                        &(*out_results)[i]);
        } else {
            /* Unsupported kernel type - skip */
            continue;
        }
        
        if (result != 0) {
            /* Cleanup on error */
            for (int j = 0; j < kernel_count; j++) {
                ca_free_kernel_analysis_result(&(*out_results)[j]);
            }
            free(*out_results);
            *out_results = NULL;
            free(block_sets);
            if (subchunk_data) {
                for (int k = 0; k < collection->count; k++) {
                    ca_free_subchunk_data(&subchunk_data[k], 1);
                }
                free(subchunk_data);
            }
            return result;
        }
        
        /* Run subchunk analysis if enabled and kernel is spatial */
        if (subchunk_divisor > 0 && kernels[i]->type == KERNEL_TYPE_SPATIAL) {
            (*out_results)[i].subchunk_analysis = (CA_KernelSubchunkAnalysis*)calloc(1, sizeof(CA_KernelSubchunkAnalysis));
            if (!(*out_results)[i].subchunk_analysis) {
                /* Non-fatal error - continue without subchunk analysis */
            } else {
                /* Aggregate subchunk data across all chunks for analysis */
                CA_ChunkSubchunkData aggregated;
                aggregated.num_subchunks = subchunk_data[0].num_subchunks;
                aggregated.subchunks = (CA_SubchunkBlockSet*)calloc(collection->count * aggregated.num_subchunks, 
                                                                   sizeof(CA_SubchunkBlockSet));
                if (!aggregated.subchunks) {
                    free((*out_results)[i].subchunk_analysis);
                    (*out_results)[i].subchunk_analysis = NULL;
                } else {
                    /* Copy subchunk data from all chunks into aggregated structure */
                    for (int chunk_idx = 0; chunk_idx < collection->count; chunk_idx++) {
                        for (uint32_t sub_idx = 0; sub_idx < aggregated.num_subchunks; sub_idx++) {
                            int src_idx = chunk_idx * aggregated.num_subchunks + sub_idx;
                            int dst_idx = chunk_idx * aggregated.num_subchunks + sub_idx;
                            aggregated.subchunks[dst_idx] = subchunk_data[chunk_idx].subchunks[sub_idx];
                        }
                    }
                    
                    result = ca_analyze_subchunks_for_kernel(collection, kernels[i], &aggregated,
                                                              cx1, cz1, cx2, cz2, kernel_stride,
                                                              (*out_results)[i].subchunk_analysis);
                    free(aggregated.subchunks);
                    
                    if (result != 0) {
                        ca_free_subchunk_analysis((*out_results)[i].subchunk_analysis);
                        free((*out_results)[i].subchunk_analysis);
                        (*out_results)[i].subchunk_analysis = NULL;
                    }
                }
            }
        }
        
        actual_result_count++;
    }
    
    free(block_sets);
    if (subchunk_data) {
        for (int i = 0; i < collection->count; i++) {
            ca_free_subchunk_data(&subchunk_data[i], 1);
        }
        free(subchunk_data);
    }
    
    *out_result_count = kernel_count;  /* Return total registered kernels */
    return 0;
}

int ca_export_kernel_results_csv(const CA_KernelAnalysisResult* results, int result_count,
                                 const char* filename, int cx1, int cz1, int cx2, int cz2, int stride) {
    if (!results || result_count == 0 || !filename) return -1;
    
    FILE* f = fopen(filename, "w");
    if (!f) return -1;
    
    fprintf(f, "# Kernel Analysis Results\n");
    fprintf(f, "# Region: (%d,%d) to (%d,%d)\n", cx1, cz1, cx2, cz2);
    fprintf(f, "# Stride: %d\n", stride);
    fprintf(f, "# Kernels: %d\n", result_count);
    fprintf(f, "\n");
    
    fprintf(f, "BlockType");
    for (int i = 0; i < result_count; i++) {
        if (results[i].kernel) {
            fprintf(f, ",%s", results[i].kernel->name);
        } else {
            fprintf(f, ",(skipped)");
        }
    }
    fprintf(f, "\n");
    
    bool all_blocks[65536] = {false};
    for (int i = 0; i < result_count; i++) {
        if (!results[i].results) continue;
        for (uint32_t j = 0; j < results[i].result_count; j++) {
            all_blocks[results[i].results[j].block_type] = true;
        }
    }
    
    for (int b = 0; b < 65536; b++) {
        if (!all_blocks[b]) continue;
        
        fprintf(f, "%u", b);
        
        for (int i = 0; i < result_count; i++) {
            if (!results[i].results) {
                fprintf(f, ",");
                continue;
            }
            double score = 0.0;
            for (uint32_t j = 0; j < results[i].result_count; j++) {
                if (results[i].results[j].block_type == (uint16_t)b) {
                    score = results[i].results[j].score;
                    break;
                }
            }
            fprintf(f, ",%.6f", score);
        }
        fprintf(f, "\n");
    }
    
    fclose(f);
    return 0;
}

/* ============================================================================
 * Block Inventory Queries
 * ============================================================================ */

/* Generate ASCII grid visualization */
int ca_generate_wall_grid(const CA_ChunkCollection* collection, const CA_WallChunkInfo* wall_info,
                         int range, char* out_grid, size_t grid_size) {
    if (!collection || !wall_info || !out_grid) return -1;
    
    size_t grid_size_needed = (range * 2 + 1) * (range * 2 + 2); /* +1 for newline per row */
    if (grid_size < grid_size_needed) return -1;
    
    char* ptr = out_grid;
    
    for (int cz = -range; cz <= range; cz++) {
        for (int cx = -range; cx <= range; cx++) {
            /* Find if this position has a chunk */
            char cell = 'M';  /* Default to Missing (not in save file) */
            for (int i = 0; i < collection->count; i++) {
                if (collection->chunks[i].cx == cx && collection->chunks[i].cz == cz) {
                    const CA_WallChunkInfo* info = &wall_info[i];
                    
                    if (info->is_air_only) {
                        /* Air-only chunk - highest priority */
                        cell = 'A';
                    } else if (info->has_wall_blocks && !info->expected_wall) {
                        /* Unexpected wall - has blocks but not expected */
                        cell = 'X';
                    } else if (info->expected_wall && info->has_wall_blocks) {
                        /* Expected wall with blocks - correct */
                        cell = 'W';
                    } else if (info->expected_wall && !info->has_wall_blocks) {
                        /* Expected wall without blocks - missing wall blocks */
                        cell = 'w';
                    } else {
                        cell = '.';
                    }

                    /* Check for corner (only if not air-only and not missing chunk) */
                    if (cell != 'A' && cell != 'M' && info->expected_wall && strstr(info->side_name, "CORNER") != NULL) {
                        cell = '+';
                    }
                    break;
                }
            }
            
            *ptr++ = cell;
            *ptr++ = ' ';
        }
        *ptr++ = '\n';
    }
    *ptr = '\0';
    
    return 0;
}

int ca_get_blocks_in_region(const CA_ChunkCollection* collection, int cx1, int cz1, int cx2, int cz2,
                            uint16_t* out_blocks, uint32_t* out_count) {
    if (!collection || !out_blocks || !out_count) return -1;
    
    if (cx1 > cx2) { int temp = cx1; cx1 = cx2; cx2 = temp; }
    if (cz1 > cz2) { int temp = cz1; cz1 = cz2; cz2 = temp; }
    
    bool seen[65536] = {false};
    uint32_t count = 0;
    
    for (int cx = cx1; cx <= cx2; cx++) {
        for (int cz = cz1; cz <= cz2; cz++) {
            for (int i = 0; i < collection->count; i++) {
                if (collection->chunks[i].cx == cx && collection->chunks[i].cz == cz) {
                    const CA_RawChunk* chunk = &collection->chunks[i];
                    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
                        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                            for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                                uint16_t block = chunk->blocks[x][z][y];
                                if (!seen[block]) {
                                    seen[block] = true;
                                    if (count < 65536) {
                                        out_blocks[count] = block;
                                        count++;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    
    *out_count = count;
    return 0;
}

int ca_get_blocks_in_region_detailed(const CA_ChunkCollection* collection, int cx1, int cz1, int cx2, int cz2,
                                     CA_BlockStat* out_stats, uint32_t* out_count) {
    if (!collection || !out_stats || !out_count) return -1;
    
    if (cx1 > cx2) { int temp = cx1; cx1 = cx2; cx2 = temp; }
    if (cz1 > cz2) { int temp = cz1; cz1 = cz2; cz2 = temp; }
    
    uint32_t block_counts[65536] = {0};
    uint64_t total_blocks = 0;
    
    for (int cx = cx1; cx <= cx2; cx++) {
        for (int cz = cz1; cz <= cz2; cz++) {
            for (int i = 0; i < collection->count; i++) {
                if (collection->chunks[i].cx == cx && collection->chunks[i].cz == cz) {
                    const CA_RawChunk* chunk = &collection->chunks[i];
                    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
                        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                            for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                                uint16_t block = chunk->blocks[x][z][y];
                                block_counts[block]++;
                                total_blocks++;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    
    uint32_t count = 0;
    for (int b = 0; b < 65536; b++) {
        if (block_counts[b] > 0) {
            out_stats[count].block_id = (uint16_t)b;
            out_stats[count].count = block_counts[b];
            out_stats[count].percentage = (double)block_counts[b] / total_blocks;
            count++;
        }
    }
    
    *out_count = count;
    return 0;
}

/* ============================================================================
 * Reverse Kernel Lookup
 * ============================================================================ */

int ca_rank_kernels_for_block(const CA_ChunkCollection* collection, uint16_t block_id,
                              int cx1, int cz1, int cx2, int cz2, int kernel_stride,
                              CA_KernelRanking* out_rankings, uint32_t* out_count) {
    if (!collection || !out_rankings || !out_count) return -1;
    
    const CA_Kernel* kernels[CA_MAX_KERNELS];
    int kernel_count = ca_get_all_kernels(kernels, CA_MAX_KERNELS);
    if (kernel_count == 0) return -1;
    
    CA_ChunkBlockSet* block_sets = (CA_ChunkBlockSet*)calloc(collection->count, sizeof(CA_ChunkBlockSet));
    if (!block_sets) return -1;
    
    int result = ca_extract_chunk_block_sets(collection, block_sets);
    if (result != 0) {
        free(block_sets);
        return result;
    }
    
    *out_count = 0;
    for (int i = 0; i < kernel_count; i++) {
        if (kernels[i]->type != KERNEL_TYPE_SPATIAL) continue;
        
        CA_KernelAnalysisResult analysis;
        result = ca_apply_kernel_to_region(collection, kernels[i], block_sets,
                                           cx1, cz1, cx2, cz2, kernel_stride, &analysis);
        if (result != 0) continue;
        
        for (uint32_t j = 0; j < analysis.result_count; j++) {
            if (analysis.results[j].block_type == block_id) {
                out_rankings[*out_count].kernel = kernels[i];
                out_rankings[*out_count].score = analysis.results[j].score;
                out_rankings[*out_count].activations = analysis.results[j].total_activations;
                (*out_count)++;
                break;
            }
        }
        
        ca_free_kernel_analysis_result(&analysis);
    }
    
    free(block_sets);
    return 0;
}

int ca_rank_kernels_for_blocks(const CA_ChunkCollection* collection, const uint16_t* block_ids, uint32_t num_blocks,
                               int cx1, int cz1, int cx2, int cz2, int kernel_stride,
                               CA_KernelRanking* out_rankings, uint32_t* out_count) {
    if (!collection || !block_ids || num_blocks == 0 || !out_rankings || !out_count) return -1;
    
    const CA_Kernel* kernels[CA_MAX_KERNELS];
    int kernel_count = ca_get_all_kernels(kernels, CA_MAX_KERNELS);
    if (kernel_count == 0) return -1;
    
    CA_ChunkBlockSet* block_sets = (CA_ChunkBlockSet*)calloc(collection->count, sizeof(CA_ChunkBlockSet));
    if (!block_sets) return -1;
    
    int result = ca_extract_chunk_block_sets(collection, block_sets);
    if (result != 0) {
        free(block_sets);
        return result;
    }
    
    *out_count = 0;
    for (int i = 0; i < kernel_count; i++) {
        if (kernels[i]->type != KERNEL_TYPE_SPATIAL) continue;
        
        CA_KernelAnalysisResult analysis;
        result = ca_apply_kernel_to_region(collection, kernels[i], block_sets,
                                           cx1, cz1, cx2, cz2, kernel_stride, &analysis);
        if (result != 0) continue;
        
        double aggregate_score = 0.0;
        uint32_t total_activations = 0;
        uint32_t blocks_found = 0;
        
        for (uint32_t b = 0; b < num_blocks; b++) {
            for (uint32_t j = 0; j < analysis.result_count; j++) {
                if (analysis.results[j].block_type == block_ids[b]) {
                    aggregate_score += analysis.results[j].score;
                    total_activations += analysis.results[j].total_activations;
                    blocks_found++;
                    break;
                }
            }
        }
        
        if (blocks_found > 0) {
            out_rankings[*out_count].kernel = kernels[i];
            out_rankings[*out_count].score = aggregate_score / blocks_found;
            out_rankings[*out_count].activations = total_activations;
            (*out_count)++;
        }
        
        ca_free_kernel_analysis_result(&analysis);
    }
    
    free(block_sets);
    return 0;
}

/* ============================================================================
 * Coverage Optimization
 * ============================================================================ */

int ca_analyze_kernel_coverage_for_block(const CA_ChunkCollection* collection, uint16_t block_id,
                                        int cx1, int cz1, int cx2, int cz2, int kernel_stride,
                                        CA_KernelCoverage* out_coverage, uint32_t* out_count) {
    if (!collection || !out_coverage || !out_count) return -1;
    
    const CA_Kernel* kernels[CA_MAX_KERNELS];
    int kernel_count = ca_get_all_kernels(kernels, CA_MAX_KERNELS);
    if (kernel_count == 0) return -1;
    
    CA_ChunkBlockSet* block_sets = (CA_ChunkBlockSet*)calloc(collection->count, sizeof(CA_ChunkBlockSet));
    if (!block_sets) return -1;
    
    int result = ca_extract_chunk_block_sets(collection, block_sets);
    if (result != 0) {
        free(block_sets);
        return result;
    }
    
    *out_count = 0;
    for (int i = 0; i < kernel_count; i++) {
        if (kernels[i]->type != KERNEL_TYPE_SPATIAL) continue;
        
        CA_KernelAnalysisResult analysis;
        result = ca_apply_kernel_to_region(collection, kernels[i], block_sets,
                                           cx1, cz1, cx2, cz2, kernel_stride, &analysis);
        if (result != 0) continue;
        
        uint32_t chunks_tested = 0;
        uint32_t chunks_found = 0;
        
        /* Count chunks where block was tested */
        for (int cx = cx1; cx <= cx2 - kernels[i]->size + 1; cx += kernel_stride) {
            for (int cz = cz1; cz <= cz2 - kernels[i]->size + 1; cz += kernel_stride) {
                chunks_tested++;
                
                /* Check if block is in any chunk in kernel region */
                bool block_found = false;
                for (int kx = 0; kx < kernels[i]->size && !block_found; kx++) {
                    for (int kz = 0; kz < kernels[i]->size && !block_found; kz++) {
                        if (!kernels[i]->pattern[kz * kernels[i]->size + kx]) continue;
                        
                        int check_cx = cx + kx;
                        int check_cz = cz + kz;
                        
                        for (int c = 0; c < collection->count; c++) {
                            if (collection->chunks[c].cx == check_cx && collection->chunks[c].cz == check_cz) {
                                const CA_ChunkBlockSet* set = &block_sets[c];
                                for (uint32_t b = 0; b < set->count; b++) {
                                    if (set->block_ids[b] == block_id) {
                                        block_found = true;
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
                
                if (block_found) chunks_found++;
            }
        }
        
        out_coverage[*out_count].kernel = kernels[i];
        out_coverage[*out_count].chunks_tested = chunks_tested;
        out_coverage[*out_count].chunks_found = chunks_found;
        out_coverage[*out_count].pass_rate = chunks_tested > 0 ? (double)chunks_found / chunks_tested : 0.0;
        (*out_count)++;
        
        ca_free_kernel_analysis_result(&analysis);
    }
    
    free(block_sets);
    return 0;
}

int ca_find_minimal_kernel_set(const CA_ChunkCollection* collection, uint16_t block_id,
                              double min_pass_rate, int cx1, int cz1, int cx2, int cz2, int kernel_stride,
                              int time_budget_ms, const CA_Kernel** out_kernel_set, uint32_t* out_set_size) {
    if (!collection || !out_kernel_set || !out_set_size) return -1;
    
    clock_t start_time = clock();
    
    CA_KernelCoverage* coverage = (CA_KernelCoverage*)calloc(CA_MAX_KERNELS, sizeof(CA_KernelCoverage));
    if (!coverage) return -1;
    
    uint32_t coverage_count = 0;
    int result = ca_analyze_kernel_coverage_for_block(collection, block_id, cx1, cz1, cx2, cz2,
                                                       kernel_stride, coverage, &coverage_count);
    if (result != 0) {
        free(coverage);
        return result;
    }
    
    /* Sort kernels by pass rate (descending) */
    for (uint32_t i = 0; i < coverage_count; i++) {
        for (uint32_t j = i + 1; j < coverage_count; j++) {
            if (coverage[j].pass_rate > coverage[i].pass_rate) {
                CA_KernelCoverage temp = coverage[i];
                coverage[i] = coverage[j];
                coverage[j] = temp;
            }
        }
    }
    
    /* Greedy selection: add kernels with highest pass rate until target met */
    uint32_t selected_count = 0;
    double aggregate_pass_rate = 0.0;
    
    for (uint32_t i = 0; i < coverage_count; i++) {
        clock_t current_time = clock();
        double elapsed_ms = 1000.0 * (current_time - start_time) / CLOCKS_PER_SEC;
        if (time_budget_ms > 0 && elapsed_ms >= time_budget_ms) {
            break;  /* Time budget exhausted */
        }
        
        out_kernel_set[selected_count] = coverage[i].kernel;
        selected_count++;
        
        /* Calculate aggregate pass rate (simplified: average of selected) */
        aggregate_pass_rate = 0.0;
        for (uint32_t j = 0; j < selected_count; j++) {
            for (uint32_t k = 0; k < coverage_count; k++) {
                if (coverage[k].kernel == out_kernel_set[j]) {
                    aggregate_pass_rate += coverage[k].pass_rate;
                    break;
                }
            }
        }
        aggregate_pass_rate /= selected_count;
        
        if (aggregate_pass_rate >= min_pass_rate) {
            break;  /* Target met */
        }
    }
    
    *out_set_size = selected_count;
    free(coverage);
    return 0;
}

/* ============================================================================
 * Memory Management
 * ============================================================================ */

void ca_free_kernel_rankings(CA_KernelRanking* rankings) {
    /* Rankings contain pointers to registered kernels, no dynamic memory to free */
    (void)rankings;  /* Suppress unused warning */
}

void ca_free_coverage_stats(CA_KernelCoverage* coverage) {
    /* Coverage contains pointers to registered kernels, no dynamic memory to free */
    (void)coverage;  /* Suppress unused warning */
}

/* ============================================================================
 * 3D Subchunk Analysis
 * ============================================================================ */

int ca_extract_subchunk_data(const CA_ChunkCollection* collection, int y_divisor,
                             CA_ChunkSubchunkData* out_data) {
    if (!collection || !out_data) return -1;
    
    /* Validate divisor */
    if (y_divisor <= 0) return -1;
    if (CA_CHUNK_SIZE_Y % y_divisor != 0) {
        fprintf(stderr, "Error: Y divisor %d does not divide chunk height %d\n", 
                y_divisor, CA_CHUNK_SIZE_Y);
        return -1;
    }
    
    int subchunk_height = CA_CHUNK_SIZE_Y / y_divisor;
    
    /* Allocate subchunk data for each chunk */
    out_data->subchunks = (CA_SubchunkBlockSet*)calloc(collection->count * y_divisor, sizeof(CA_SubchunkBlockSet));
    if (!out_data->subchunks) return -1;
    out_data->num_subchunks = y_divisor;
    
    /* Extract blocks for each subchunk */
    for (int chunk_idx = 0; chunk_idx < collection->count; chunk_idx++) {
        const CA_RawChunk* chunk = &collection->chunks[chunk_idx];
        
        for (int sub_idx = 0; sub_idx < y_divisor; sub_idx++) {
            CA_SubchunkBlockSet* sub = &out_data->subchunks[chunk_idx * y_divisor + sub_idx];
            sub->subchunk_index = sub_idx;
            
            int y_start = sub_idx * subchunk_height;
            int y_end = (sub_idx + 1) * subchunk_height;
            
            /* Count unique blocks in this subchunk */
            bool seen[65536] = {false};
            uint32_t count = 0;
            
            for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
                for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                    for (int y = y_start; y < y_end; y++) {
                        uint16_t block = chunk->blocks[x][z][y];
                        if (!seen[block]) {
                            seen[block] = true;
                            count++;
                        }
                    }
                }
            }
            
            /* Allocate and fill block IDs */
            sub->count = count;
            if (count > 0) {
                sub->block_ids = (uint16_t*)calloc(count, sizeof(uint16_t));
                if (!sub->block_ids) {
                    /* Cleanup already allocated memory */
                    for (int i = 0; i < chunk_idx * y_divisor + sub_idx; i++) {
                        if (out_data->subchunks[i].block_ids) {
                            free(out_data->subchunks[i].block_ids);
                        }
                    }
                    free(out_data->subchunks);
                    return -1;
                }
                
                /* Fill block IDs (reset seen and use again) */
                memset(seen, 0, sizeof(seen));
                uint32_t idx = 0;
                for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
                    for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                        for (int y = y_start; y < y_end; y++) {
                            uint16_t block = chunk->blocks[x][z][y];
                            if (!seen[block]) {
                                seen[block] = true;
                                sub->block_ids[idx++] = block;
                            }
                        }
                    }
                }
            } else {
                sub->block_ids = NULL;
            }
        }
    }
    
    return 0;
}

int ca_apply_kernel_to_subchunk(const CA_ChunkCollection* collection, const CA_Kernel* kernel,
                               const CA_ChunkSubchunkData* subchunk_data, int subchunk_index,
                               int cx1, int cz1, int cx2, int cz2, int stride,
                               CA_KernelAnalysisResult* out_result) {
    if (!collection || !kernel || !subchunk_data || !out_result) return -1;
    
    if (kernel->type != KERNEL_TYPE_SPATIAL) return -1;
    if (subchunk_index < 0 || subchunk_index >= (int)subchunk_data->num_subchunks) return -1;
    
    if (cx1 > cx2) { int temp = cx1; cx1 = cx2; cx2 = temp; }
    if (cz1 > cz2) { int temp = cz1; cz1 = cz2; cz2 = temp; }
    if (stride < 1) stride = 1;
    
    int kernel_ones = 0;
    for (int i = 0; i < kernel->pattern_size; i++) {
        if (kernel->pattern[i]) kernel_ones++;
    }
    if (kernel_ones == 0) return -1;
    
    /* Collect all unique block IDs across all chunks for this subchunk index */
    bool block_in_region[65536] = {false};
    uint32_t region_block_count = 0;
    uint32_t total_blocks_in_slice = 0;
    
    for (int cx = cx1; cx <= cx2; cx++) {
        for (int cz = cz1; cz <= cz2; cz++) {
            for (int i = 0; i < collection->count; i++) {
                if (collection->chunks[i].cx == cx && collection->chunks[i].cz == cz) {
                    const CA_SubchunkBlockSet* sub = &subchunk_data->subchunks[i * subchunk_data->num_subchunks + subchunk_index];
                    for (uint32_t j = 0; j < sub->count; j++) {
                        uint16_t block = sub->block_ids[j];
                        if (!block_in_region[block]) {
                            block_in_region[block] = true;
                            region_block_count++;
                        }
                        total_blocks_in_slice++;
                    }
                    break;
                }
            }
        }
    }
    
    if (region_block_count == 0) return 0;
    
    out_result->kernel = kernel;
    out_result->results = (CA_KernelBlockResult*)calloc(region_block_count, sizeof(CA_KernelBlockResult));
    if (!out_result->results) return -1;
    
    uint32_t idx = 0;
    for (int b = 0; b < 65536; b++) {
        if (block_in_region[b]) {
            out_result->results[idx].block_type = (uint16_t)b;
            out_result->results[idx].total_activations = 0;
            out_result->results[idx].score = 0.0;
            idx++;
        }
    }
    out_result->result_count = region_block_count;
    
    /* Apply kernel across the region */
    for (int start_cx = cx1; start_cx <= cx2 - kernel->size + 1; start_cx += stride) {
        for (int start_cz = cz1; start_cz <= cz2 - kernel->size + 1; start_cz += stride) {
            for (int ki = 0; ki < kernel->pattern_size; ki++) {
                if (!kernel->pattern[ki]) continue;
                
                int kx = ki % kernel->size;
                int kz = ki / kernel->size;
                int cx = start_cx + kx;
                int cz = start_cz + kz;
                
                for (int i = 0; i < collection->count; i++) {
                    if (collection->chunks[i].cx == cx && collection->chunks[i].cz == cz) {
                        const CA_SubchunkBlockSet* sub = &subchunk_data->subchunks[i * subchunk_data->num_subchunks + subchunk_index];
                        
                        for (uint32_t j = 0; j < out_result->result_count; j++) {
                            uint16_t block = out_result->results[j].block_type;
                            
                            for (uint32_t k = 0; k < sub->count; k++) {
                                if (sub->block_ids[k] == block) {
                                    out_result->results[j].total_activations++;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }
    
    /* Calculate scores */
    uint32_t total_kernel_positions = 0;
    for (int start_cx = cx1; start_cx <= cx2 - kernel->size + 1; start_cx += stride) {
        for (int start_cz = cz1; start_cz <= cz2 - kernel->size + 1; start_cz += stride) {
            total_kernel_positions++;
        }
    }
    
    if (total_kernel_positions > 0) {
        for (uint32_t j = 0; j < out_result->result_count; j++) {
            out_result->results[j].score = (double)out_result->results[j].total_activations / 
                                           (total_kernel_positions * kernel_ones);
        }
    }
    
    return 0;
}

int ca_analyze_subchunks_for_kernel(const CA_ChunkCollection* collection, const CA_Kernel* kernel,
                                   const CA_ChunkSubchunkData* subchunk_data,
                                   int cx1, int cz1, int cx2, int cz2, int stride,
                                   CA_KernelSubchunkAnalysis* out_analysis) {
    if (!collection || !kernel || !subchunk_data || !out_analysis) return -1;
    
    if (kernel->type != KERNEL_TYPE_SPATIAL) return -1;
    
    out_analysis->kernel = kernel;
    out_analysis->num_subchunks = subchunk_data->num_subchunks;
    out_analysis->subchunk_results = (CA_SubchunkKernelResult*)calloc(subchunk_data->num_subchunks, 
                                                                     sizeof(CA_SubchunkKernelResult));
    if (!out_analysis->subchunk_results) return -1;
    
    out_analysis->best_score = -1.0;
    out_analysis->best_subchunk = -1;
    
    /* Analyze each subchunk */
    for (uint32_t sub_idx = 0; sub_idx < subchunk_data->num_subchunks; sub_idx++) {
        CA_KernelAnalysisResult result;
        memset(&result, 0, sizeof(result));
        
        int ret = ca_apply_kernel_to_subchunk(collection, kernel, subchunk_data, sub_idx,
                                             cx1, cz1, cx2, cz2, stride, &result);
        if (ret != 0) {
            out_analysis->subchunk_results[sub_idx].score = 0.0;
            out_analysis->subchunk_results[sub_idx].raw_score = 0.0;
            out_analysis->subchunk_results[sub_idx].subchunk_index = sub_idx;
            out_analysis->subchunk_results[sub_idx].total_activations = 0;
            out_analysis->subchunk_results[sub_idx].blocks_in_slice = 0;
            out_analysis->subchunk_results[sub_idx].unused_id_count = 0;
            out_analysis->subchunk_results[sub_idx].matched_block_count = 0;
            continue;
        }
        
        /* Calculate raw score (average of all block scores) */
        double raw_score = 0.0;
        if (result.result_count > 0) {
            for (uint32_t j = 0; j < result.result_count; j++) {
                raw_score += result.results[j].score;
            }
            raw_score /= result.result_count;
        }
        
        /* Count blocks in this slice (unique block types across all chunks) */
        uint32_t blocks_in_slice = 0;
        bool seen[65536] = {false};
        for (int i = 0; i < collection->count; i++) {
            CA_SubchunkBlockSet* sub = &subchunk_data->subchunks[i * subchunk_data->num_subchunks + sub_idx];
            for (uint32_t b = 0; b < sub->count; b++) {
                if (!seen[sub->block_ids[b]]) {
                    seen[sub->block_ids[b]] = true;
                    blocks_in_slice++;
                }
            }
        }
        
        /* Calculate matched and unused ID counts */
        uint32_t matched_block_count = result.result_count;  /* Block types matched by kernel */
        uint32_t unused_id_count = blocks_in_slice - matched_block_count;  /* Block types NOT matched */
        
        /* Adjust score: multiply by (total_chunks / subchunk_count) */
        /* This makes kernels more efficient when applied to smaller slices */
        double adjusted_score = raw_score * ((double)collection->count / subchunk_data->num_subchunks);
        
        out_analysis->subchunk_results[sub_idx].kernel = kernel;
        out_analysis->subchunk_results[sub_idx].subchunk_index = sub_idx;
        out_analysis->subchunk_results[sub_idx].score = adjusted_score;
        out_analysis->subchunk_results[sub_idx].raw_score = raw_score;
        out_analysis->subchunk_results[sub_idx].total_activations = 0;  /* Sum of activations not tracked per subchunk */
        out_analysis->subchunk_results[sub_idx].blocks_in_slice = blocks_in_slice;
        out_analysis->subchunk_results[sub_idx].unused_id_count = unused_id_count;
        out_analysis->subchunk_results[sub_idx].matched_block_count = matched_block_count;
        
        /* Track best subchunk by UNUSED ID count (not by score) */
        /* We want the subchunk with the MOST unused IDs (fewest matched blocks) */
        if (unused_id_count > out_analysis->best_score) {
            out_analysis->best_score = unused_id_count;
            out_analysis->best_subchunk = sub_idx;
        }
        
        ca_free_kernel_analysis_result(&result);
    }
    
    return 0;
}

void ca_free_subchunk_data(CA_ChunkSubchunkData* data, uint32_t count) {
    if (!data) return;
    
    for (uint32_t i = 0; i < count * data->num_subchunks; i++) {
        if (data->subchunks[i].block_ids) {
            free(data->subchunks[i].block_ids);
        }
    }
    free(data->subchunks);
    data->subchunks = NULL;
    data->num_subchunks = 0;
}

void ca_free_subchunk_analysis(CA_KernelSubchunkAnalysis* analysis) {
    if (!analysis) return;
    if (analysis->subchunk_results) {
        free(analysis->subchunk_results);
    }
}

int ca_analyze_block_kernel_efficiency(const CA_ChunkCollection* collection, int subchunk_divisor,
                                       CA_BlockKernelEfficiency** out_results, uint32_t* out_count) {
    if (!collection || !out_results || !out_count) return -1;
    
    if (subchunk_divisor <= 0) return -1;
    
    /* First, collect all unique block IDs across all chunks */
    bool seen[65536] = {false};
    uint16_t block_ids[65536];
    uint32_t block_count = 0;
    
    for (int i = 0; i < collection->count; i++) {
        const CA_RawChunk* chunk = &collection->chunks[i];
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    uint16_t block = chunk->blocks[x][z][y];
                    if (!seen[block]) {
                        seen[block] = true;
                        block_ids[block_count++] = block;
                    }
                }
            }
        }
    }
    
    if (block_count == 0) return -1;
    
    /* Extract subchunk data */
    CA_ChunkSubchunkData* subchunk_data = (CA_ChunkSubchunkData*)calloc(collection->count, sizeof(CA_ChunkSubchunkData));
    if (!subchunk_data) return -1;
    
    int result = 0;
    for (int i = 0; i < collection->count; i++) {
        result = ca_extract_subchunk_data(collection, subchunk_divisor, &subchunk_data[i]);
        if (result != 0) {
            for (int j = 0; j < i; j++) {
                ca_free_subchunk_data(&subchunk_data[j], 1);
            }
            free(subchunk_data);
            return result;
        }
    }
    
    /* Get all kernels */
    const CA_Kernel* kernels[CA_MAX_KERNELS];
    int kernel_count = ca_get_all_kernels(kernels, CA_MAX_KERNELS);
    
    /* Run subchunk analysis for all spatial kernels */
    CA_KernelSubchunkAnalysis* kernel_analyses = (CA_KernelSubchunkAnalysis*)calloc(kernel_count, sizeof(CA_KernelSubchunkAnalysis));
    if (!kernel_analyses) {
        for (int i = 0; i < collection->count; i++) {
            ca_free_subchunk_data(&subchunk_data[i], 1);
        }
        free(subchunk_data);
        return -1;
    }
    
    for (int i = 0; i < kernel_count; i++) {
        if (kernels[i]->type == KERNEL_TYPE_SPATIAL) {
            result = ca_analyze_subchunks_for_kernel(collection, kernels[i], subchunk_data,
                                                      0, 0, collection->count - 1, collection->count - 1, 1,
                                                      &kernel_analyses[i]);
        }
    }
    
    /* Allocate results for each block ID */
    *out_results = (CA_BlockKernelEfficiency*)calloc(block_count, sizeof(CA_BlockKernelEfficiency));
    if (!*out_results) {
        for (int i = 0; i < kernel_count; i++) {
            ca_free_subchunk_analysis(&kernel_analyses[i]);
        }
        free(kernel_analyses);
        for (int i = 0; i < collection->count; i++) {
            ca_free_subchunk_data(&subchunk_data[i], 1);
        }
        free(subchunk_data);
        return -1;
    }
    
    /* For each block ID, find the best kernel */
    for (uint32_t b = 0; b < block_count; b++) {
        uint16_t block_id = block_ids[b];
        (*out_results)[b].block_id = block_id;
        (*out_results)[b].best_kernel = NULL;
        (*out_results)[b].best_subchunk = -1;
        (*out_results)[b].best_unused_ids = 0;
        (*out_results)[b].best_matched_blocks = 0;
        (*out_results)[b].efficiency_score = 0.0;
        
        /* Find which subchunk(s) contain this block */
        for (uint32_t sub_idx = 0; sub_idx < subchunk_data[0].num_subchunks; sub_idx++) {
            bool block_in_subchunk = false;
            for (int i = 0; i < collection->count; i++) {
                CA_SubchunkBlockSet* sub = &subchunk_data[i].subchunks[sub_idx];
                for (uint32_t k = 0; k < sub->count; k++) {
                    if (sub->block_ids[k] == block_id) {
                        block_in_subchunk = true;
                        break;
                    }
                }
                if (block_in_subchunk) break;
            }
            
            if (!block_in_subchunk) continue;
            
            /* Check all kernels for this subchunk */
            for (int k = 0; k < kernel_count; k++) {
                if (kernels[k]->type != KERNEL_TYPE_SPATIAL) continue;
                if (!kernel_analyses[k].subchunk_results) continue;
                
                uint32_t unused = kernel_analyses[k].subchunk_results[sub_idx].unused_id_count;
                uint32_t matched = kernel_analyses[k].subchunk_results[sub_idx].matched_block_count;
                uint32_t total = kernel_analyses[k].subchunk_results[sub_idx].blocks_in_slice;
                
                if (total == 0) continue;
                
                double efficiency = (double)unused / total;
                
                if (unused > (*out_results)[b].best_unused_ids || 
                    (unused == (*out_results)[b].best_unused_ids && efficiency > (*out_results)[b].efficiency_score)) {
                    (*out_results)[b].best_kernel = kernels[k];
                    (*out_results)[b].best_subchunk = sub_idx;
                    (*out_results)[b].best_unused_ids = unused;
                    (*out_results)[b].best_matched_blocks = matched;
                    (*out_results)[b].efficiency_score = efficiency;
                }
            }
        }
    }
    
    /* Cleanup */
    for (int i = 0; i < kernel_count; i++) {
        ca_free_subchunk_analysis(&kernel_analyses[i]);
    }
    free(kernel_analyses);
    for (int i = 0; i < collection->count; i++) {
        ca_free_subchunk_data(&subchunk_data[i], 1);
    }
    free(subchunk_data);
    
    *out_count = block_count;
    return 0;
}

void ca_free_block_kernel_efficiency(CA_BlockKernelEfficiency* results, uint32_t count) {
    if (results) {
        free(results);
    }
}

/* ============================================================================
 * Wall Chunk Analysis Implementation
 * ============================================================================ */

/* Helper: floor division that rounds toward negative infinity (from debugger) */
static int floor_div_local(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return (value - denom + 1) / denom;
}

/* Detect wall blocks in a single chunk by counting wall-signature materials */
int ca_detect_wall_blocks_chunk(const CA_RawChunk* chunk, uint32_t* out_wall_block_count) {
    if (!chunk || !out_wall_block_count) return -1;
    
    uint32_t count = 0;
    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                if (ca_is_wall_signature_material(chunk->blocks[x][z][y])) {
                    count++;
                }
            }
        }
    }
    
    *out_wall_block_count = count;
    return 0;
}

/* Detect wall blocks in all chunks */
int ca_detect_wall_blocks_collection(const CA_ChunkCollection* collection, CA_WallChunkInfo* out_info,
                                     int air_threshold_percent) {
    if (!collection || !out_info) return -1;
    
    uint32_t total_blocks_per_chunk = CA_CHUNK_SIZE_X * CA_CHUNK_SIZE_Z * CA_CHUNK_SIZE_Y;
    uint32_t air_threshold = (total_blocks_per_chunk * air_threshold_percent) / 100;
    
    for (int i = 0; i < collection->count; i++) {
        out_info[i].cx = collection->chunks[i].cx;
        out_info[i].cz = collection->chunks[i].cz;
        out_info[i].expected_wall = false;
        out_info[i].is_problematic = false;
        out_info[i].is_air_only = false;
        out_info[i].side_name = "NONE";
        
        uint32_t wall_count = 0;
        ca_detect_wall_blocks_chunk(&collection->chunks[i], &wall_count);
        out_info[i].wall_block_count = wall_count;
        out_info[i].has_wall_blocks = (wall_count > 0);
        
        /* Check if chunk is air-only */
        uint32_t non_air_count = 0;
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    if (collection->chunks[i].blocks[x][z][y] != 0) { /* Block 0 is air */
                        non_air_count++;
                        if (non_air_count > air_threshold) break;
                    }
                }
                if (non_air_count > air_threshold) break;
            }
            if (non_air_count > air_threshold) break;
        }
        out_info[i].is_air_only = (non_air_count <= air_threshold);
    }
    
    return 0;
}

/* Load world configuration from meta.txt */
int ca_load_world_config(const char* save_path, CA_WorldConfig* out_config) {
    if (!save_path || !out_config) return -1;
    
    /* Initialize with defaults */
    out_config->chunk_span = 16;
    out_config->wall_grid_size = 18;
    out_config->wall_grid_offset_x = 0;
    out_config->wall_grid_offset_z = 0;
    out_config->wall_thickness_blocks = CHUNK_WIDTH;
    out_config->wall_rings_shared = true;
    out_config->coordinate_system = (uint8_t)SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    
    /* Build meta.txt path (same directory as save.json) */
    char meta_path[512];
    size_t path_len = strlen(save_path);
    if (path_len >= sizeof(meta_path)) return -1;
    
    /* Find last directory separator */
    const char* last_sep = strrchr(save_path, '\\');
    if (!last_sep) last_sep = strrchr(save_path, '/');
    if (!last_sep) return -1;
    
    /* Copy directory path */
    size_t dir_len = last_sep - save_path + 1;
    memcpy(meta_path, save_path, dir_len);
    strcpy_s(meta_path + dir_len, sizeof(meta_path) - dir_len, "meta.txt");
    
    /* Read and parse meta.txt */
    FILE* f = fopen(meta_path, "r");
    if (!f) {
        /* File not found, use defaults */
        return 0;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Parse key=value */
        char* eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        
        /* Trim whitespace */
        while (key[0] == ' ' || key[0] == '\t') key++;
        while (value[0] == ' ' || value[0] == '\t') value++;
        
        /* Trim newline */
        char* nl = strchr(value, '\n');
        if (nl) *nl = '\0';
        char* cr = strchr(value, '\r');
        if (cr) *cr = '\0';
        
        /* Parse values */
        if (strcmp(key, "superchunk_chunk_span") == 0) {
            out_config->chunk_span = atoi(value);
        } else if (strcmp(key, "coordinate_system") == 0) {
            out_config->coordinate_system = (uint8_t)atoi(value);
        } else if (strcmp(key, "wall_grid_size") == 0) {
            out_config->wall_grid_size = atoi(value);
        } else if (strcmp(key, "wall_grid_offset_x") == 0) {
            out_config->wall_grid_offset_x = atoi(value);
        } else if (strcmp(key, "wall_grid_offset_z") == 0) {
            out_config->wall_grid_offset_z = atoi(value);
        } else if (strcmp(key, "wall_thickness_blocks") == 0) {
            out_config->wall_thickness_blocks = atoi(value);
        } else if (strcmp(key, "wall_rings_shared") == 0) {
            out_config->wall_rings_shared = atoi(value) != 0;
        }
    }
    
    fclose(f);
    return 0;
}

static int ca_normalized_wall_grid_size(int chunk_span, uint8_t coordinate_system, int wall_grid_size)
{
    int default_size = chunk_span + 2;
    if (!sdk_world_coordinate_system_detaches_walls((SdkWorldCoordinateSystem)coordinate_system)) {
        return chunk_span + 2;
    }
    if (wall_grid_size <= 2 || wall_grid_size < default_size) return default_size;
    return wall_grid_size;
}

static int ca_wall_period(int chunk_span,
                          uint8_t coordinate_system,
                          int wall_grid_size,
                          bool wall_rings_shared)
{
    if (sdk_world_coordinate_system_detaches_walls((SdkWorldCoordinateSystem)coordinate_system)) {
        const int ring_size = ca_normalized_wall_grid_size(chunk_span, coordinate_system, wall_grid_size);
        return ring_size - (wall_rings_shared ? 1 : 0);
    }
    return chunk_span + 1;
}

/* Check if a chunk is expected to be a wall chunk (from debugger logic) */
int ca_is_expected_wall_chunk(int cx, int cz, int chunk_span,
                              int wall_grid_size, int wall_grid_offset_x, int wall_grid_offset_z,
                              bool wall_rings_shared,
                              uint8_t coordinate_system,
                              const char** out_side_name) {
    const int period = ca_wall_period(chunk_span, coordinate_system, wall_grid_size, wall_rings_shared);
    const int period_local_x = sdk_superchunk_floor_mod_i(cx - wall_grid_offset_x, period);
    const int period_local_z = sdk_superchunk_floor_mod_i(cz - wall_grid_offset_z, period);
    const bool is_west = (period_local_x == 0);
    const bool is_east = (!wall_rings_shared && period_local_x == period - 1);
    const bool is_north = (period_local_z == 0);
    const bool is_south = (!wall_rings_shared && period_local_z == period - 1);
    const bool is_wall = is_west || is_east || is_north || is_south;

    if (out_side_name) {
        if (is_west && is_north) *out_side_name = "CORNER_NW";
        else if (is_east && is_north) *out_side_name = "CORNER_NE";
        else if (is_west && is_south) *out_side_name = "CORNER_SW";
        else if (is_east && is_south) *out_side_name = "CORNER_SE";
        else if (is_west) *out_side_name = "WEST";
        else if (is_east) *out_side_name = "EAST";
        else if (is_north) *out_side_name = "NORTH";
        else if (is_south) *out_side_name = "SOUTH";
        else *out_side_name = "NONE";
    }
    
    return is_wall ? 1 : 0;
}

/* Calculate expected wall positions for all chunks */
int ca_calculate_expected_walls(const CA_ChunkCollection* collection, const CA_WorldConfig* config,
                                 CA_WallChunkInfo* wall_info) {
    if (!collection || !config || !wall_info) return -1;
    
    for (int i = 0; i < collection->count; i++) {
        wall_info[i].expected_wall = ca_is_expected_wall_chunk(
            wall_info[i].cx, wall_info[i].cz,
            config->chunk_span,
            config->wall_grid_size,
            config->wall_grid_offset_x,
            config->wall_grid_offset_z,
            config->wall_rings_shared,
            config->coordinate_system,
            &wall_info[i].side_name
        ) != 0;
    }
    
    return 0;
}

/* Compare expected vs actual wall mapping */
int ca_compare_wall_mapping(const CA_ChunkCollection* collection, CA_WallChunkInfo* wall_info,
                            uint32_t wall_threshold) {
    int missing_walls = 0;
    int unexpected_walls = 0;
    
    for (int i = 0; i < collection->count; i++) {
        CA_WallChunkInfo* info = &wall_info[i];
        
        /* Determine if this chunk is problematic */
        if (info->expected_wall && info->has_wall_blocks) {
            /* Expected wall has blocks - correct */
            info->is_problematic = false;
        } else if (info->expected_wall && !info->has_wall_blocks) {
            /* Expected wall but no blocks - missing wall */
            info->is_problematic = true;
            missing_walls++;
        } else if (!info->expected_wall && info->has_wall_blocks) {
            /* Not expected but has blocks - unexpected wall */
            info->is_problematic = true;
            unexpected_walls++;
        } else {
            /* Not expected, no blocks - normal chunk */
            info->is_problematic = false;
        }
    }
    
    return 0;
}

/* Export wall analysis to CSV */
int ca_export_wall_analysis_csv(const CA_ChunkCollection* collection, const CA_WallChunkInfo* wall_info,
                                const char* filename) {
    if (!collection || !wall_info || !filename) return -1;
    
    FILE* f = fopen(filename, "w");
    if (!f) return -1;
    
    /* Write header */
    fprintf(f, "cx,cz,wall_block_count,has_wall_blocks,expected_wall,is_problematic,side_name\n");
    
    /* Write data */
    for (int i = 0; i < collection->count; i++) {
        fprintf(f, "%d,%d,%u,%d,%d,%d,%s\n",
                wall_info[i].cx,
                wall_info[i].cz,
                wall_info[i].wall_block_count,
                wall_info[i].has_wall_blocks ? 1 : 0,
                wall_info[i].expected_wall ? 1 : 0,
                wall_info[i].is_problematic ? 1 : 0,
                wall_info[i].side_name);
    }
    
    fclose(f);
    return 0;
}

/* Print wall analysis summary */
void ca_print_wall_analysis_summary(const CA_ChunkCollection* collection, const CA_WallChunkInfo* wall_info,
                                    const CA_WorldConfig* config) {
    int expected_walls = 0;
    int actual_walls = 0;
    int air_only = 0;
    int correct_walls = 0;
    int missing_walls = 0;
    int unexpected_walls = 0;
    
    for (int i = 0; i < collection->count; i++) {
        const CA_WallChunkInfo* info = &wall_info[i];
        
        if (info->expected_wall) expected_walls++;
        if (info->has_wall_blocks) actual_walls++;
        if (info->is_air_only) air_only++;
        
        if (info->expected_wall && info->has_wall_blocks) {
            correct_walls++;
        } else if (info->expected_wall && !info->has_wall_blocks) {
            missing_walls++;
        } else if (!info->expected_wall && info->has_wall_blocks) {
            unexpected_walls++;
        }
    }

    printf("=== WALL CHUNK ANALYSIS ===\n");
    printf("Configuration from meta.txt:\n");
    printf("  Chunk Span: %d\n", config->chunk_span);
    printf("  Coordinate System: %d\n", config->coordinate_system);
    printf("  Wall Grid Size: %d\n", config->wall_grid_size);
    printf("  Offset X: %d\n", config->wall_grid_offset_x);
    printf("  Offset Z: %d\n", config->wall_grid_offset_z);
    printf("  Shared Walls: %s\n", config->wall_rings_shared ? "yes" : "no");
    printf("  Air Threshold: %d%%\n", config->air_threshold_percent);
    printf("\n");
    
    printf("Wall Block Detection:\n");
    printf("  Total chunks: %d\n", collection->count);
    printf("  Chunks with wall blocks: %d\n", actual_walls);
    printf("  Air-only chunks: %d\n", air_only);
    printf("  Expected wall chunks: %d\n", expected_walls);
    printf("  Correct walls (expected + has blocks): %d\n", correct_walls);
    printf("  Problematic wall chunks: %d\n", missing_walls + unexpected_walls);
    printf("    - Missing walls (expected, no blocks): %d\n", missing_walls);
    printf("    - Unexpected walls (has blocks, not expected): %d\n", unexpected_walls);
    printf("\n");
    
    if (unexpected_walls > 0) {
        printf("=== UNEXPECTED WALL CHUNKS ===\n");
        printf("Chunks with wall-signature materials at non-wall positions:\n");
        for (int i = 0; i < collection->count; i++) {
            const CA_WallChunkInfo* info = &wall_info[i];
            if (!info->expected_wall && info->has_wall_blocks) {
                printf("  Chunk (%d, %d): %u wall-signature blocks\n",
                       collection->chunks[i].cx, collection->chunks[i].cz, info->wall_block_count);
            }
        }
        printf("\n");
    }
    
    if (missing_walls > 0) {
        printf("=== MISSING WALL CHUNKS (first 20) ===\n");
        printf("Expected wall positions with no wall-signature materials:\n");
        int count = 0;
        for (int i = 0; i < collection->count && count < 20; i++) {
            const CA_WallChunkInfo* info = &wall_info[i];
            if (info->expected_wall && !info->has_wall_blocks) {
                printf("  Chunk (%d, %d): expected as %s, but has 0 wall-signature blocks\n",
                       collection->chunks[i].cx, collection->chunks[i].cz, info->side_name);
                count++;
            }
        }
        if (missing_walls > 20) {
            printf("  ... and %d more\n", missing_walls - 20);
        }
        printf("\n");
    }
    
    printf("Note: wall boundaries at period %d with offsets (%d,%d).\n",
           ca_wall_period(config->chunk_span,
                          config->coordinate_system,
                          config->wall_grid_size,
                          config->wall_rings_shared),
           config->wall_grid_offset_x, config->wall_grid_offset_z);
    printf("\n");
}

int ca_analyze_wall_summary(const char* save_path,
                            int max_chunks,
                            int air_threshold_percent,
                            uint32_t wall_threshold,
                            CA_WallAnalysisSummary* out_summary)
{
    CA_ChunkCollection collection;
    CA_WorldConfig config;
    CA_WallChunkInfo* wall_info = NULL;
    int i;

    if (!save_path || !out_summary) return -1;
    memset(out_summary, 0, sizeof(*out_summary));
    memset(&collection, 0, sizeof(collection));
    memset(&config, 0, sizeof(config));

    if (ca_load_json_save(save_path, &collection, max_chunks) != 0) {
        return -1;
    }
    if (ca_load_world_config(save_path, &config) != 0) {
        ca_free_collection(&collection);
        return -1;
    }
    if (air_threshold_percent > 0) {
        config.air_threshold_percent = air_threshold_percent;
    }

    wall_info = (CA_WallChunkInfo*)calloc((size_t)collection.count, sizeof(CA_WallChunkInfo));
    if (!wall_info) {
        ca_free_collection(&collection);
        return -1;
    }

    if (ca_detect_wall_blocks_collection(&collection, wall_info, config.air_threshold_percent) != 0 ||
        ca_calculate_expected_walls(&collection, &config, wall_info) != 0 ||
        ca_compare_wall_mapping(&collection, wall_info, wall_threshold) != 0) {
        free(wall_info);
        ca_free_collection(&collection);
        return -1;
    }

    out_summary->config = config;
    out_summary->analyzed_chunk_count = collection.count;

    for (i = 0; i < collection.count; ++i) {
        if (wall_info[i].expected_wall) {
            out_summary->expected_wall_chunk_count++;
        }
        if (wall_info[i].expected_wall && wall_info[i].has_wall_blocks) {
            out_summary->correct_wall_chunk_count++;
        }
        if (wall_info[i].expected_wall && !wall_info[i].has_wall_blocks) {
            out_summary->missing_wall_chunk_count++;
        }
        if (!wall_info[i].expected_wall && wall_info[i].has_wall_blocks) {
            out_summary->unexpected_wall_chunk_count++;
        }
        if (wall_info[i].is_problematic) {
            out_summary->problematic_wall_chunk_count++;
        }
    }

    out_summary->pass =
        (out_summary->missing_wall_chunk_count == 0) &&
        (out_summary->unexpected_wall_chunk_count == 0);

    free(wall_info);
    ca_free_collection(&collection);
    return 0;
}
