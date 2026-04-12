/* ============================================================================
 * Core Compression Logic
 * 
 * Volume-based and layer-based compression for 64x64x1024 chunks
 * ============================================================================ */

#include "chunk_compress.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ============================================================================
 * Base64 Encoding/Decoding (for compressed format)
 * ============================================================================ */

static const char base64_alphabet[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_encode(const uint8_t* data, size_t len, char** out_str, size_t* out_str_len) {
    if (!out_str) return -1;
    if (len == 0u) {
        *out_str = (char*)malloc(1u);
        if (!*out_str) return -1;
        (*out_str)[0] = '\0';
        if (out_str_len) *out_str_len = 0u;
        return 0;
    }
    if (!data) return -1;
    
    size_t out_len = ((len + 2) / 3) * 4;
    *out_str = (char*)malloc(out_len + 1);
    if (!*out_str) return -1;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = data[i] << 16;
        if (i + 1 < len) val |= data[i + 1] << 8;
        if (i + 2 < len) val |= data[i + 2];
        
        (*out_str)[j++] = base64_alphabet[(val >> 18) & 0x3F];
        (*out_str)[j++] = base64_alphabet[(val >> 12) & 0x3F];
        (*out_str)[j++] = (i + 1 < len) ? base64_alphabet[(val >> 6) & 0x3F] : '=';
        (*out_str)[j++] = (i + 2 < len) ? base64_alphabet[val & 0x3F] : '=';
    }
    
    (*out_str)[j] = '\0';
    if (out_str_len) *out_str_len = j;
    return 0;
}

int base64_decode(const char* str, size_t str_len, uint8_t** out_data, size_t* out_data_len) {
    if (!str || !out_data) return -1;
    
    /* Build decode table */
    int8_t decode_table[256];
    memset(decode_table, -1, sizeof(decode_table));
    for (int i = 0; i < 64; i++) {
        decode_table[(unsigned char)base64_alphabet[i]] = (int8_t)i;
    }
    
    size_t max_out = (str_len * 3) / 4 + 4;
    *out_data = (uint8_t*)malloc(max_out);
    if (!*out_data) return -1;
    
    size_t out_len = 0;
    uint32_t buffer = 0;
    int bits = 0;
    
    for (size_t i = 0; i < str_len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '=') break;
        
        int8_t val = decode_table[c];
        if (val < 0) continue;
        
        buffer = (buffer << 6) | val;
        bits += 6;
        
        while (bits >= 8) {
            bits -= 8;
            (*out_data)[out_len++] = (buffer >> bits) & 0xFF;
        }
    }
    
    if (out_data_len) *out_data_len = out_len;
    return 0;
}

/* ============================================================================
 * Compressed Chunk Management
 * ============================================================================ */

void compress_chunk_init(CompressedChunk* cc, int32_t cx, int32_t cz, uint16_t top_y) {
    memset(cc, 0, sizeof(CompressedChunk));
    cc->cx = cx;
    cc->cz = cz;
    cc->top_y = top_y;
    cc->version = COMPRESS_FORMAT_VERSION;
}

void compress_chunk_free(CompressedChunk* cc) {
    if (!cc) return;
    
    free(cc->volumes);
    cc->volumes = NULL;
    cc->volume_count = 0;
    
    if (cc->layers) {
        for (uint32_t i = 0; i < cc->layer_count; i++) {
            free(cc->layers[i].patches);
        }
        free(cc->layers);
        cc->layers = NULL;
        cc->layer_count = 0;
    }
    
    free(cc->residual_data);
    cc->residual_data = NULL;
    cc->residual_size = 0;
}

/* ============================================================================
 * Volume Detection
 * 
 * Finds large 3D regions of the same block type using greedy approach:
 * 1. Scan for unmarked blocks
 * 2. Grow maximally in X, Z, Y directions
 * 3. Mark covered blocks
 * 4. Continue until all blocks are covered or minimum volume size not met
 * ============================================================================ */

#define MIN_VOLUME_SIZE 64  /* Minimum blocks to form a volume */
#define MIN_VOLUME_DIMENSION 2  /* Min size in any dimension */

/* Check if a region is uniform (all same block type) */
static bool is_uniform_region(const RawChunk* raw, 
                              uint16_t x1, uint16_t y1, uint16_t z1,
                              uint16_t x2, uint16_t y2, uint16_t z2,
                              uint16_t* out_block_id) {
    uint16_t first = raw->blocks[x1][z1][y1];
    
    for (uint16_t x = x1; x <= x2; x++) {
        for (uint16_t z = z1; z <= z2; z++) {
            for (uint16_t y = y1; y <= y2; y++) {
                if (raw->blocks[x][z][y] != first) {
                    return false;
                }
            }
        }
    }
    
    *out_block_id = first;
    return true;
}

/* Grow a volume from seed point */
static void grow_volume(const RawChunk* raw, uint8_t* covered,
                        uint16_t seed_x, uint16_t seed_y, uint16_t seed_z,
                        Volume* out_volume) {
    uint16_t block_id = raw->blocks[seed_x][seed_z][seed_y];
    
    uint16_t x1 = seed_x, x2 = seed_x;
    uint16_t y1 = seed_y, y2 = seed_y;
    uint16_t z1 = seed_z, z2 = seed_z;
    
    /* Greedy expansion: try to grow in each direction */
    bool expanded;
    do {
        expanded = false;
        
        /* Try to expand +X */
        if (x2 + 1 < CHUNK_SIZE_X) {
            bool can_expand = true;
            for (uint16_t z = z1; z <= z2 && can_expand; z++) {
                for (uint16_t y = y1; y <= y2 && can_expand; y++) {
                    if (raw->blocks[x2 + 1][z][y] != block_id ||
                        covered[(x2 + 1) * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + y]) {
                        can_expand = false;
                    }
                }
            }
            if (can_expand) { x2++; expanded = true; }
        }
        
        /* Try to expand -X */
        if (x1 > 0) {
            bool can_expand = true;
            for (uint16_t z = z1; z <= z2 && can_expand; z++) {
                for (uint16_t y = y1; y <= y2 && can_expand; y++) {
                    if (raw->blocks[x1 - 1][z][y] != block_id ||
                        covered[(x1 - 1) * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + y]) {
                        can_expand = false;
                    }
                }
            }
            if (can_expand) { x1--; expanded = true; }
        }
        
        /* Try to expand +Z */
        if (z2 + 1 < CHUNK_SIZE_Z) {
            bool can_expand = true;
            for (uint16_t x = x1; x <= x2 && can_expand; x++) {
                for (uint16_t y = y1; y <= y2 && can_expand; y++) {
                    if (raw->blocks[x][z2 + 1][y] != block_id ||
                        covered[x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + (z2 + 1) * CHUNK_SIZE_Y + y]) {
                        can_expand = false;
                    }
                }
            }
            if (can_expand) { z2++; expanded = true; }
        }
        
        /* Try to expand -Z */
        if (z1 > 0) {
            bool can_expand = true;
            for (uint16_t x = x1; x <= x2 && can_expand; x++) {
                for (uint16_t y = y1; y <= y2 && can_expand; y++) {
                    if (raw->blocks[x][z1 - 1][y] != block_id ||
                        covered[x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + (z1 - 1) * CHUNK_SIZE_Y + y]) {
                        can_expand = false;
                    }
                }
            }
            if (can_expand) { z1--; expanded = true; }
        }
        
        /* Try to expand +Y (up) */
        if (y2 + 1 < CHUNK_SIZE_Y) {
            bool can_expand = true;
            for (uint16_t x = x1; x <= x2 && can_expand; x++) {
                for (uint16_t z = z1; z <= z2 && can_expand; z++) {
                    if (raw->blocks[x][z][y2 + 1] != block_id ||
                        covered[x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + (y2 + 1)]) {
                        can_expand = false;
                    }
                }
            }
            if (can_expand) { y2++; expanded = true; }
        }
        
        /* Try to expand -Y (down) */
        if (y1 > 0) {
            bool can_expand = true;
            for (uint16_t x = x1; x <= x2 && can_expand; x++) {
                for (uint16_t z = z1; z <= z2 && can_expand; z++) {
                    if (raw->blocks[x][z][y1 - 1] != block_id ||
                        covered[x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + (y1 - 1)]) {
                        can_expand = false;
                    }
                }
            }
            if (can_expand) { y1--; expanded = true; }
        }
    } while (expanded);
    
    out_volume->x1 = x1;
    out_volume->y1 = y1;
    out_volume->z1 = z1;
    out_volume->x2 = x2;
    out_volume->y2 = y2;
    out_volume->z2 = z2;
    out_volume->block_id = block_id;
}

int volume_detect(const RawChunk* raw, Volume** out_volumes, uint32_t* out_count) {
    /* Allocate tracking array for covered blocks */
    size_t total_blocks = CHUNK_TOTAL_BLOCKS;
    uint8_t* covered = (uint8_t*)calloc(total_blocks, 1);
    if (!covered) return -1;
    
    /* Allocate initial volume array */
    uint32_t max_volumes = 10000;
    Volume* volumes = (Volume*)malloc(sizeof(Volume) * max_volumes);
    if (!volumes) {
        free(covered);
        return -1;
    }
    
    uint32_t volume_count = 0;
    
    /* Scan for volumes */
    for (uint16_t x = 0; x < CHUNK_SIZE_X; x++) {
        for (uint16_t z = 0; z < CHUNK_SIZE_Z; z++) {
            for (uint16_t y = 0; y < CHUNK_SIZE_Y; y++) {
                size_t idx = x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + y;
                
                if (covered[idx]) continue;
                
                /* Found uncovered block - try to grow volume */
                Volume vol;
                grow_volume(raw, covered, x, y, z, &vol);
                
                /* Calculate volume size */
                uint32_t vol_size = (vol.x2 - vol.x1 + 1) * (vol.z2 - vol.z1 + 1) * (vol.y2 - vol.y1 + 1);
                
                /* Only keep volumes above minimum size */
                if (vol_size >= MIN_VOLUME_SIZE) {
                    if (volume_count >= max_volumes) {
                        max_volumes *= 2;
                        Volume* new_volumes = (Volume*)realloc(volumes, sizeof(Volume) * max_volumes);
                        if (!new_volumes) {
                            free(volumes);
                            free(covered);
                            return -1;
                        }
                        volumes = new_volumes;
                    }
                    
                    volumes[volume_count++] = vol;
                    
                    /* Mark all blocks in this volume as covered */
                    for (uint16_t vx = vol.x1; vx <= vol.x2; vx++) {
                        for (uint16_t vz = vol.z1; vz <= vol.z2; vz++) {
                            for (uint16_t vy = vol.y1; vy <= vol.y2; vy++) {
                                size_t vidx = vx * CHUNK_SIZE_Z * CHUNK_SIZE_Y + vz * CHUNK_SIZE_Y + vy;
                                covered[vidx] = 1;
                            }
                        }
                    }
                }
            }
        }
    }
    
    free(covered);
    
    /* Trim volume array */
    if (volume_count > 0) {
        Volume* trimmed = (Volume*)realloc(volumes, sizeof(Volume) * volume_count);
        if (trimmed) volumes = trimmed;
    } else {
        free(volumes);
        volumes = NULL;
    }
    
    *out_volumes = volumes;
    *out_count = volume_count;
    return 0;
}

void volume_free(Volume* volumes) {
    free(volumes);
}

/* ============================================================================
 * Layer Building
 * 
 * After volume extraction, build layers from remaining blocks:
 * 1. Find dominant block type among uncovered blocks
 * 2. Create base layer with patches for non-dominant blocks
 * 3. Repeat if beneficial
 * ============================================================================ */

#define LAYER_MIN_DOMINANT_RATIO 0.70  /* 70% to form a layer base */
#define LAYER_MIN_BLOCKS 1000

typedef struct {
    uint32_t count;
    uint16_t block_id;
} BlockCount;

static int compare_block_count(const void* a, const void* b) {
    const BlockCount* ba = (const BlockCount*)a;
    const BlockCount* bb = (const BlockCount*)b;
    return (int)bb->count - (int)ba->count;  /* Descending */
}

int layer_build(const RawChunk* raw, const Volume* volumes, uint32_t volume_count,
                Layer** out_layers, uint32_t* out_layer_count) {
    
    /* Build coverage map from volumes */
    size_t total_blocks = CHUNK_TOTAL_BLOCKS;
    uint8_t* covered = (uint8_t*)calloc(total_blocks, 1);
    if (!covered) return -1;
    
    for (uint32_t i = 0; i < volume_count; i++) {
        for (uint16_t x = volumes[i].x1; x <= volumes[i].x2; x++) {
            for (uint16_t z = volumes[i].z1; z <= volumes[i].z2; z++) {
                for (uint16_t y = volumes[i].y1; y <= volumes[i].y2; y++) {
                    size_t idx = x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + y;
                    covered[idx] = 1;
                }
            }
        }
    }
    
    /* Count uncovered blocks */
    uint32_t uncovered_count = 0;
    BlockCount block_counts[256] = {0};
    
    for (uint16_t x = 0; x < CHUNK_SIZE_X; x++) {
        for (uint16_t z = 0; z < CHUNK_SIZE_Z; z++) {
            for (uint16_t y = 0; y < CHUNK_SIZE_Y; y++) {
                size_t idx = x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + y;
                if (!covered[idx]) {
                    uncovered_count++;
                    block_counts[raw->blocks[x][z][y]].count++;
                }
            }
        }
    }
    
    /* Add block IDs to counts for sorting */
    for (int i = 0; i < 256; i++) {
        block_counts[i].block_id = (uint16_t)i;
    }
    
    /* Sort by count descending */
    qsort(block_counts, 256, sizeof(BlockCount), compare_block_count);
    
    /* Allocate layers */
    uint32_t max_layers = 10;
    Layer* layers = (Layer*)malloc(sizeof(Layer) * max_layers);
    if (!layers) {
        free(covered);
        return -1;
    }
    
    uint32_t layer_count = 0;
    
    /* Build layers while dominant block exists */
    while (uncovered_count >= LAYER_MIN_BLOCKS && layer_count < max_layers) {
        /* Find most common uncovered block */
        if (block_counts[0].count < uncovered_count * LAYER_MIN_DOMINANT_RATIO) {
            break;  /* No dominant block */
        }
        
        uint16_t base_id = block_counts[0].block_id;
        
        /* Count patches needed (non-base blocks) */
        uint32_t patch_count = 0;
        for (uint16_t x = 0; x < CHUNK_SIZE_X; x++) {
            for (uint16_t z = 0; z < CHUNK_SIZE_Z; z++) {
                for (uint16_t y = 0; y < CHUNK_SIZE_Y; y++) {
                    size_t idx = x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + y;
                    if (!covered[idx] && raw->blocks[x][z][y] != base_id) {
                        patch_count++;
                    }
                }
            }
        }
        
        /* Allocate patches */
        Patch* patches = (Patch*)malloc(sizeof(Patch) * patch_count);
        if (!patches) break;
        
        /* Fill patches */
        uint32_t p = 0;
        for (uint16_t x = 0; x < CHUNK_SIZE_X; x++) {
            for (uint16_t z = 0; z < CHUNK_SIZE_Z; z++) {
                for (uint16_t y = 0; y < CHUNK_SIZE_Y; y++) {
                    size_t idx = x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + y;
                    if (!covered[idx] && raw->blocks[x][z][y] != base_id) {
                        patches[p].x = x;
                        patches[p].z = z;
                        patches[p].y = y;
                        patches[p].block_id = raw->blocks[x][z][y];
                        p++;
                        
                        /* Mark as covered for next layer */
                        covered[idx] = 1;
                    } else if (!covered[idx] && raw->blocks[x][z][y] == base_id) {
                        /* Mark base blocks as covered */
                        covered[idx] = 1;
                    }
                }
            }
        }
        
        /* Create layer */
        layers[layer_count].base_block_id = base_id;
        layers[layer_count].patch_count = patch_count;
        layers[layer_count].patches = patches;
        layer_count++;
        
        /* Recount for next iteration */
        uncovered_count = 0;
        memset(block_counts, 0, sizeof(block_counts));
        for (int i = 0; i < 256; i++) block_counts[i].block_id = (uint16_t)i;
        
        for (uint16_t x = 0; x < CHUNK_SIZE_X; x++) {
            for (uint16_t z = 0; z < CHUNK_SIZE_Z; z++) {
                for (uint16_t y = 0; y < CHUNK_SIZE_Y; y++) {
                    size_t idx = x * CHUNK_SIZE_Z * CHUNK_SIZE_Y + z * CHUNK_SIZE_Y + y;
                    if (!covered[idx]) {
                        uncovered_count++;
                        block_counts[raw->blocks[x][z][y]].count++;
                    }
                }
            }
        }
        
        qsort(block_counts, 256, sizeof(BlockCount), compare_block_count);
    }
    
    free(covered);
    
    /* Trim layers array */
    if (layer_count > 0) {
        Layer* trimmed = (Layer*)realloc(layers, sizeof(Layer) * layer_count);
        if (trimmed) layers = trimmed;
    } else {
        free(layers);
        layers = NULL;
    }
    
    *out_layers = layers;
    *out_layer_count = layer_count;
    return 0;
}

void layer_free(Layer* layers, uint32_t count) {
    if (!layers) return;
    for (uint32_t i = 0; i < count; i++) {
        free(layers[i].patches);
    }
    free(layers);
}
