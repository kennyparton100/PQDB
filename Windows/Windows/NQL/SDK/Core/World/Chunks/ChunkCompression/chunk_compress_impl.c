/* ============================================================================
 * Binary Encoding/Decoding and High-Level Compress/Decompress
 * ============================================================================ */

#include "chunk_compress.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations from chunk_compress.c */
extern int base64_encode(const uint8_t* data, size_t len, char** out_str, size_t* out_str_len);
extern int base64_decode(const char* str, size_t str_len, uint8_t** out_data, size_t* out_data_len);

/* ============================================================================
 * Binary Encoding Format
 * 
 * Header: version(2), cx(4), cz(4), top_y(2), volume_count(4), layer_count(4)
 * Volumes: [x1,y1,z1,x2,y2,z2,block_id] * volume_count (each 14 bytes)
 * Layers: base_block_id(2), patch_count(4), [x,y,z,block_id] * patch_count
 * Residual: size(4), data[size]
 * ============================================================================ */

/* Encode compressed chunk to binary buffer */
int compress_encode_binary(const CompressedChunk* cc, uint8_t** out_data, size_t* out_size) {
    if (!cc || !out_data) return -1;
    
    /* Calculate required size */
    size_t size = 20;  /* Header */
    size += cc->volume_count * 14;  /* Volumes */
    
    for (uint32_t i = 0; i < cc->layer_count; i++) {
        size += 6 + cc->layers[i].patch_count * 8;  /* Layer header + patches */
    }
    
    size += 4 + cc->residual_size;  /* Residual */
    
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) return -1;
    
    size_t pos = 0;
    
    /* Header */
    (*out_data)[pos++] = cc->version & 0xFF;
    (*out_data)[pos++] = (cc->version >> 8) & 0xFF;
    
    *(int32_t*)(&(*out_data)[pos]) = cc->cx; pos += 4;
    *(int32_t*)(&(*out_data)[pos]) = cc->cz; pos += 4;
    
    (*out_data)[pos++] = cc->top_y & 0xFF;
    (*out_data)[pos++] = (cc->top_y >> 8) & 0xFF;
    
    *(uint32_t*)(&(*out_data)[pos]) = cc->volume_count; pos += 4;
    *(uint32_t*)(&(*out_data)[pos]) = cc->layer_count; pos += 4;
    
    /* Volumes */
    for (uint32_t i = 0; i < cc->volume_count; i++) {
        *(uint16_t*)(&(*out_data)[pos]) = cc->volumes[i].x1; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = cc->volumes[i].y1; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = cc->volumes[i].z1; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = cc->volumes[i].x2; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = cc->volumes[i].y2; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = cc->volumes[i].z2; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = cc->volumes[i].block_id; pos += 2;
    }
    
    /* Layers */
    for (uint32_t i = 0; i < cc->layer_count; i++) {
        *(uint16_t*)(&(*out_data)[pos]) = cc->layers[i].base_block_id; pos += 2;
        *(uint32_t*)(&(*out_data)[pos]) = cc->layers[i].patch_count; pos += 4;
        
        for (uint32_t j = 0; j < cc->layers[i].patch_count; j++) {
            *(uint16_t*)(&(*out_data)[pos]) = cc->layers[i].patches[j].x; pos += 2;
            *(uint16_t*)(&(*out_data)[pos]) = cc->layers[i].patches[j].y; pos += 2;
            *(uint16_t*)(&(*out_data)[pos]) = cc->layers[i].patches[j].z; pos += 2;
            *(uint16_t*)(&(*out_data)[pos]) = cc->layers[i].patches[j].block_id; pos += 2;
        }
    }
    
    /* Residual */
    *(uint32_t*)(&(*out_data)[pos]) = (uint32_t)cc->residual_size; pos += 4;
    if (cc->residual_size > 0 && cc->residual_data) {
        memcpy(&(*out_data)[pos], cc->residual_data, cc->residual_size);
        pos += cc->residual_size;
    }
    
    *out_size = pos;
    return 0;
}

/* Decode binary buffer to compressed chunk */
int compress_decode_binary(const uint8_t* data, size_t size, CompressedChunk* out) {
    if (!data || !out || size < 20) return -1;
    
    memset(out, 0, sizeof(CompressedChunk));
    
    size_t pos = 0;
    
    /* Header */
    out->version = data[pos] | (data[pos+1] << 8); pos += 2;
    out->cx = *(int32_t*)(&data[pos]); pos += 4;
    out->cz = *(int32_t*)(&data[pos]); pos += 4;
    out->top_y = data[pos] | (data[pos+1] << 8); pos += 2;
    out->volume_count = *(uint32_t*)(&data[pos]); pos += 4;
    out->layer_count = *(uint32_t*)(&data[pos]); pos += 4;
    
    /* Volumes */
    if (out->volume_count > 0) {
        out->volumes = (Volume*)malloc(sizeof(Volume) * out->volume_count);
        if (!out->volumes) return -1;
        
        for (uint32_t i = 0; i < out->volume_count; i++) {
            out->volumes[i].x1 = *(uint16_t*)(&data[pos]); pos += 2;
            out->volumes[i].y1 = *(uint16_t*)(&data[pos]); pos += 2;
            out->volumes[i].z1 = *(uint16_t*)(&data[pos]); pos += 2;
            out->volumes[i].x2 = *(uint16_t*)(&data[pos]); pos += 2;
            out->volumes[i].y2 = *(uint16_t*)(&data[pos]); pos += 2;
            out->volumes[i].z2 = *(uint16_t*)(&data[pos]); pos += 2;
            out->volumes[i].block_id = *(uint16_t*)(&data[pos]); pos += 2;
        }
    }
    
    /* Layers */
    if (out->layer_count > 0) {
        out->layers = (Layer*)malloc(sizeof(Layer) * out->layer_count);
        if (!out->layers) {
            free(out->volumes);
            return -1;
        }
        
        for (uint32_t i = 0; i < out->layer_count; i++) {
            out->layers[i].base_block_id = *(uint16_t*)(&data[pos]); pos += 2;
            out->layers[i].patch_count = *(uint32_t*)(&data[pos]); pos += 4;
            
            if (out->layers[i].patch_count > 0) {
                out->layers[i].patches = (Patch*)malloc(sizeof(Patch) * out->layers[i].patch_count);
                if (!out->layers[i].patches) {
                    /* Cleanup */
                    for (uint32_t j = 0; j < i; j++) {
                        free(out->layers[j].patches);
                    }
                    free(out->layers);
                    free(out->volumes);
                    return -1;
                }
                
                for (uint32_t j = 0; j < out->layers[i].patch_count; j++) {
                    out->layers[i].patches[j].x = *(uint16_t*)(&data[pos]); pos += 2;
                    out->layers[i].patches[j].y = *(uint16_t*)(&data[pos]); pos += 2;
                    out->layers[i].patches[j].z = *(uint16_t*)(&data[pos]); pos += 2;
                    out->layers[i].patches[j].block_id = *(uint16_t*)(&data[pos]); pos += 2;
                }
            } else {
                out->layers[i].patches = NULL;
            }
        }
    }
    
    /* Residual */
    if (pos + 4 <= size) {
        uint32_t residual_size = *(uint32_t*)(&data[pos]); pos += 4;
        out->residual_size = residual_size;
        
        if (residual_size > 0) {
            if (pos + residual_size > size) {
                compress_chunk_free(out);
                return -1;
            }
            out->residual_data = (uint8_t*)malloc(residual_size);
            if (!out->residual_data) {
                compress_chunk_free(out);
                return -1;
            }
            memcpy(out->residual_data, &data[pos], residual_size);
        }
    }
    
    return 0;
}

/* ============================================================================
 * High-Level Compression/Decompression
 * ============================================================================ */

#define CHUNK_RESIDUAL_RLE_MAGIC 0x36315243U  /* "CR16" */

static uint32_t chunk_count_runs(const RawChunk* raw) {
    const uint16_t* flat_blocks = &raw->blocks[0][0][0];
    uint32_t run_count = 1;

    for (size_t i = 1; i < CHUNK_TOTAL_BLOCKS; i++) {
        if (flat_blocks[i] != flat_blocks[i - 1]) {
            run_count++;
        }
    }

    return run_count;
}

static int chunk_encode_residual_rle(const RawChunk* raw, uint8_t** out_data, size_t* out_size) {
    const uint16_t* flat_blocks = &raw->blocks[0][0][0];
    uint32_t run_count = 0;
    size_t pos = 0;
    uint8_t* data = NULL;

    if (!raw || !out_data || !out_size) return -1;

    run_count = chunk_count_runs(raw);
    *out_size = sizeof(uint32_t) * 2 + (size_t)run_count * (sizeof(uint16_t) + sizeof(uint32_t));
    data = (uint8_t*)malloc(*out_size);
    if (!data) return -1;

    *(uint32_t*)(&data[pos]) = CHUNK_RESIDUAL_RLE_MAGIC; pos += sizeof(uint32_t);
    *(uint32_t*)(&data[pos]) = run_count; pos += sizeof(uint32_t);

    uint16_t current_block = flat_blocks[0];
    uint32_t run_length = 1;

    for (size_t i = 1; i < CHUNK_TOTAL_BLOCKS; i++) {
        if (flat_blocks[i] == current_block && run_length < UINT32_MAX) {
            run_length++;
            continue;
        }

        *(uint16_t*)(&data[pos]) = current_block; pos += sizeof(uint16_t);
        *(uint32_t*)(&data[pos]) = run_length; pos += sizeof(uint32_t);

        current_block = flat_blocks[i];
        run_length = 1;
    }

    *(uint16_t*)(&data[pos]) = current_block; pos += sizeof(uint16_t);
    *(uint32_t*)(&data[pos]) = run_length; pos += sizeof(uint32_t);

    *out_data = data;
    *out_size = pos;
    return 0;
}

static int chunk_decode_residual_rle(const uint8_t* data, size_t size, RawChunk* out) {
    size_t pos = 0;
    uint32_t run_count = 0;
    uint32_t block_pos = 0;
    uint16_t* flat_blocks = &out->blocks[0][0][0];

    if (!data || !out || size < sizeof(uint32_t) * 2) return -1;
    if (*(const uint32_t*)data != CHUNK_RESIDUAL_RLE_MAGIC) return -1;

    pos += sizeof(uint32_t);
    run_count = *(const uint32_t*)(&data[pos]);
    pos += sizeof(uint32_t);

    for (uint32_t run = 0; run < run_count; run++) {
        uint16_t block_id;
        uint32_t run_length;

        if (pos + sizeof(uint16_t) + sizeof(uint32_t) > size) return -1;

        block_id = *(const uint16_t*)(&data[pos]);
        pos += sizeof(uint16_t);
        run_length = *(const uint32_t*)(&data[pos]);
        pos += sizeof(uint32_t);

        if (run_length == 0 || block_pos + run_length > CHUNK_TOTAL_BLOCKS) return -1;

        for (uint32_t i = 0; i < run_length; i++) {
            flat_blocks[block_pos++] = block_id;
        }
    }

    return (block_pos == CHUNK_TOTAL_BLOCKS) ? 0 : -1;
}

int compress_chunk(const RawChunk* raw, CompressedChunk* out) {
    if (!raw || !out) return -1;
    
    /* Initialize output */
    compress_chunk_init(out, raw->cx, raw->cz, raw->top_y);

    return chunk_encode_residual_rle(raw, &out->residual_data, &out->residual_size);
}

int decompress_chunk(const CompressedChunk* cc, RawChunk* out) {
    if (!cc || !out) return -1;
    
    /* Initialize output */
    memset(out, 0, sizeof(RawChunk));
    out->cx = cc->cx;
    out->cz = cc->cz;
    out->top_y = cc->top_y;
    
    /* Fill with air first */
    memset(out->blocks, 0, sizeof(out->blocks));

    if (cc->residual_size > 0 && cc->residual_data &&
        chunk_decode_residual_rle(cc->residual_data, cc->residual_size, out) == 0) {
        return 0;
    }
    
    /* Apply volumes */
    for (uint32_t i = 0; i < cc->volume_count; i++) {
        const Volume* v = &cc->volumes[i];
        for (uint16_t x = v->x1; x <= v->x2 && x < CHUNK_SIZE_X; x++) {
            for (uint16_t z = v->z1; z <= v->z2 && z < CHUNK_SIZE_Z; z++) {
                for (uint16_t y = v->y1; y <= v->y2 && y < CHUNK_SIZE_Y; y++) {
                    out->blocks[x][z][y] = v->block_id;
                }
            }
        }
    }
    
    /* Apply layers */
    for (uint32_t i = 0; i < cc->layer_count; i++) {
        const Layer* l = &cc->layers[i];
        
        /* Apply base block (fill entire chunk - volumes take precedence) */
        /* In a more sophisticated implementation, we'd track what's already set */
        
        /* Apply patches */
        for (uint32_t j = 0; j < l->patch_count; j++) {
            const Patch* p = &l->patches[j];
            if (p->x < CHUNK_SIZE_X && p->z < CHUNK_SIZE_Z && p->y < CHUNK_SIZE_Y) {
                out->blocks[p->x][p->z][p->y] = p->block_id;
            }
        }
    }
    
    /* Apply residual RLE data if any */
    if (cc->residual_size > 0 && cc->residual_data) {
        /* Parse residual and fill remaining blocks */
        /* Placeholder - full implementation would decode RLE */
    }
    
    return 0;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

double compress_ratio(size_t original_size, size_t compressed_size) {
    if (original_size == 0) return 0.0;
    return 1.0 - ((double)compressed_size / (double)original_size);
}

void compress_print_stats(const CompressedChunk* cc, size_t original_bytes) {
    if (!cc) return;
    
    printf("Compression Statistics:\n");
    printf("  Chunk: (%d, %d), top_y: %d\n", cc->cx, cc->cz, cc->top_y);
    printf("  Volumes: %u\n", cc->volume_count);
    
    uint32_t volume_blocks = 0;
    for (uint32_t i = 0; i < cc->volume_count; i++) {
        uint32_t vol_size = (cc->volumes[i].x2 - cc->volumes[i].x1 + 1) 
                          * (cc->volumes[i].z2 - cc->volumes[i].z1 + 1) 
                          * (cc->volumes[i].y2 - cc->volumes[i].y1 + 1);
        volume_blocks += vol_size;
    }
    printf("  Volume coverage: %u blocks (%.1f%%)\n", 
           volume_blocks, 100.0 * volume_blocks / CHUNK_TOTAL_BLOCKS);
    
    printf("  Layers: %u\n", cc->layer_count);
    uint32_t patch_count = 0;
    for (uint32_t i = 0; i < cc->layer_count; i++) {
        patch_count += cc->layers[i].patch_count;
    }
    printf("  Total patches: %u\n", patch_count);
    
    /* Estimate compressed size */
    size_t estimated_size = 20;  /* Header */
    estimated_size += cc->volume_count * 14;
    for (uint32_t i = 0; i < cc->layer_count; i++) {
        estimated_size += 6 + cc->layers[i].patch_count * 8;
    }
    estimated_size += 4 + cc->residual_size;
    
    printf("  Estimated binary size: %zu bytes\n", estimated_size);
    printf("  Original size: %zu bytes\n", original_bytes);
    printf("  Compression ratio: %.1f%%\n", 100.0 * compress_ratio(original_bytes, estimated_size));
}

bool compress_verify(const RawChunk* original, const RawChunk* decompressed) {
    if (!original || !decompressed) return false;
    
    /* Check coordinates */
    if (original->cx != decompressed->cx || original->cz != decompressed->cz) {
        printf("Verify failed: coordinates mismatch\n");
        return false;
    }
    
    /* Check all blocks */
    uint32_t mismatches = 0;
    for (uint16_t x = 0; x < CHUNK_SIZE_X; x++) {
        for (uint16_t z = 0; z < CHUNK_SIZE_Z; z++) {
            for (uint16_t y = 0; y < CHUNK_SIZE_Y; y++) {
                if (original->blocks[x][z][y] != decompressed->blocks[x][z][y]) {
                    mismatches++;
                    if (mismatches <= 5) {
                        printf("Mismatch at (%u, %u, %u): original=%u, decompressed=%u\n",
                               x, z, y, 
                               original->blocks[x][z][y], 
                               decompressed->blocks[x][z][y]);
                    }
                }
            }
        }
    }
    
    if (mismatches > 0) {
        printf("Verify failed: %u blocks mismatched\n", mismatches);
        return false;
    }
    
    return true;
}

/* ============================================================================
 * Superchunk Compression Implementation
 * ============================================================================ */

void superchunk_init(CompressedSuperchunk* sc, int32_t scx, int32_t scz) {
    if (!sc) return;
    memset(sc, 0, sizeof(CompressedSuperchunk));
    sc->scx = scx;
    sc->scz = scz;
}

void superchunk_free(CompressedSuperchunk* sc) {
    if (!sc) return;
    
    /* Free all sub-chunks */
    for (int i = 0; i < SUPERCHUNK_CHUNKS; i++) {
        if (sc->chunks[i]) {
            compress_chunk_free(sc->chunks[i]);
            free(sc->chunks[i]);
            sc->chunks[i] = NULL;
        }
    }
    
    /* Free mega volumes */
    free(sc->mega_volumes);
    sc->mega_volumes = NULL;
    sc->mega_volume_count = 0;
    
    /* Free wall grid */
    free(sc->wall_grid);
    sc->wall_grid = NULL;
    sc->wall_grid_size = 0;
}

/* Build wall grid - stores boundary data between chunks in compressed form */
int wall_grid_build(const RawSuperchunk* raw, uint8_t** out_grid, size_t* out_size) {
    if (!raw || !out_grid || !out_size) return -1;
    
    /* 
     * Wall grid stores boundary blocks at chunk boundaries.
     * For a 16x16 superchunk, there are 17x17 grid lines in X-Z plane.
     * Each grid line is 1024 blocks tall (Y axis).
     * We compress using RLE for each vertical column.
     */
    
    #define WALL_GRID_X (SUPERCHUNK_SIZE_CHUNKS + 1)
    #define WALL_GRID_Z (SUPERCHUNK_SIZE_CHUNKS + 1)
    #define WALL_GRID_COLS (WALL_GRID_X * WALL_GRID_Z)
    
    /* Allocate buffer for compressed wall data */
    /* Each column: header (2 bytes for column index) + RLE data */
    size_t max_size = WALL_GRID_COLS * (2 + CHUNK_SIZE_Y * 2);  /* Conservative estimate */
    uint8_t* grid = (uint8_t*)malloc(max_size);
    if (!grid) return -1;
    
    size_t pos = 0;
    
    /* For each grid column */
    for (int gx = 0; gx < WALL_GRID_X; gx++) {
        for (int gz = 0; gz < WALL_GRID_Z; gz++) {
            /* Determine which chunks touch this grid line */
            int chunk_left = gx - 1;   /* -1 means outside superchunk */
            int chunk_front = gz - 1;
            
            /* Check if this grid line is at superchunk boundary */
            bool is_boundary = (gx == 0 || gx == SUPERCHUNK_SIZE_CHUNKS ||
                               gz == 0 || gz == SUPERCHUNK_SIZE_CHUNKS);
            
            if (!is_boundary) {
                /* Internal grid line - skip if no walls needed */
                continue;
            }
            
            /* Store column index */
            grid[pos++] = (uint8_t)gx;
            grid[pos++] = (uint8_t)gz;
            
            /* Collect boundary blocks along Y axis */
            /* Simplified: store RLE of (block_id, count) pairs */
            uint8_t current_block = 0;
            uint16_t run_length = 0;
            
            for (int y = 0; y < CHUNK_SIZE_Y && pos < max_size - 4; y++) {
                /* Get block at this wall position */
                uint16_t block = 0;  /* Default to air */
                
                /* Check adjacent chunks for wall blocks */
                if (chunk_left >= 0 && chunk_left < SUPERCHUNK_SIZE_CHUNKS &&
                    chunk_front >= 0 && chunk_front < SUPERCHUNK_SIZE_CHUNKS) {
                    RawChunk* chunk = raw->chunks[chunk_left * SUPERCHUNK_SIZE_CHUNKS + chunk_front];
                    if (chunk && gx == chunk_left + 1) {
                        /* Get block at chunk boundary */
                        block = (uint16_t)chunk->blocks[CHUNK_SIZE_X - 1][0][y];  /* Right edge */
                    }
                }
                
                /* RLE encode */
                if (block == current_block && run_length < 65535) {
                    run_length++;
                } else {
                    /* Store previous run */
                    if (run_length > 0) {
                        grid[pos++] = current_block;
                        grid[pos++] = (uint8_t)(run_length & 0xFF);
                        grid[pos++] = (uint8_t)((run_length >> 8) & 0xFF);
                    }
                    current_block = (uint8_t)block;
                    run_length = 1;
                }
            }
            
            /* Store final run */
            if (run_length > 0 && pos < max_size - 4) {
                grid[pos++] = (uint8_t)current_block;
                grid[pos++] = (uint8_t)(run_length & 0xFF);
                grid[pos++] = (uint8_t)((run_length >> 8) & 0xFF);
            }
            
            /* End of column marker */
            grid[pos++] = 0xFF;
        }
    }
    
    *out_grid = grid;
    *out_size = pos;
    return 0;
}

/* Extract wall grid cell */
WallGridCell wall_grid_get(const uint8_t* grid, size_t grid_size, uint16_t x, uint16_t z, uint16_t y) {
    WallGridCell cell = {0, 0, 0};
    if (!grid || x > SUPERCHUNK_SIZE_CHUNKS || z > SUPERCHUNK_SIZE_CHUNKS || y >= CHUNK_SIZE_Y) {
        return cell;
    }
    
    /* Search for column in grid data */
    size_t pos = 0;
    while (pos < grid_size - 2) {
        uint8_t col_x = grid[pos++];
        uint8_t col_z = grid[pos++];
        
        if (col_x == 0xFF) break;  /* End marker */
        
        if (col_x == x && col_z == z) {
            /* Found our column, decode RLE to find block at y */
            uint16_t current_y = 0;
            while (pos < grid_size - 3) {
                uint8_t block_id = grid[pos++];
                if (block_id == 0xFF) break;  /* End of column */
                
                uint16_t run_length = grid[pos] | (grid[pos + 1] << 8);
                pos += 2;
                
                if (y >= current_y && y < current_y + run_length) {
                    cell.block_id = block_id;
                    cell.wall_type = (block_id != 0) ? 1 : 0;
                    return cell;
                }
                current_y += run_length;
            }
            return cell;
        } else {
            /* Skip this column's data */
            while (pos < grid_size && grid[pos] != 0xFF) {
                if (grid[pos] == 0xFF) {
                    pos++;
                    break;
                }
                pos += 3;  /* Skip block_id + 2-byte length */
            }
            if (pos < grid_size && grid[pos] == 0xFF) pos++;
        }
    }
    
    return cell;
}

/* Find mega-volumes - structures that span multiple chunks */
int mega_volume_detect(const RawSuperchunk* raw, Volume** out_volumes, uint32_t* out_count) {
    if (!raw || !out_volumes || !out_count) return -1;
    
    /* 
     * Strategy: Look for uniform regions across the entire superchunk.
     * These are volumes larger than single chunks that can be stored once
     * instead of being split across 256 chunk compressions.
     */
    
    *out_volumes = NULL;
    *out_count = 0;
    
    /* Allocate space for mega volumes */
    uint32_t max_volumes = 1000;
    Volume* volumes = (Volume*)malloc(sizeof(Volume) * max_volumes);
    if (!volumes) return -1;
    
    /* 
     * Simple approach: Scan in large blocks (e.g., 256x256x256) 
     * to find uniform regions.
     */
    uint32_t volume_count = 0;
    
    /* Scan at 256-block granularity */
    for (uint16_t x = 0; x < SUPERCHUNK_SIZE_X && volume_count < max_volumes; x += 256) {
        for (uint16_t z = 0; z < SUPERCHUNK_SIZE_Z && volume_count < max_volumes; z += 256) {
            for (uint16_t y = 0; y < CHUNK_SIZE_Y && volume_count < max_volumes; y += 256) {
                
                uint16_t x2 = x + 255 < SUPERCHUNK_SIZE_X ? x + 255 : SUPERCHUNK_SIZE_X - 1;
                uint16_t z2 = z + 255 < SUPERCHUNK_SIZE_Z ? z + 255 : SUPERCHUNK_SIZE_Z - 1;
                uint16_t y2 = y + 255 < CHUNK_SIZE_Y ? y + 255 : CHUNK_SIZE_Y - 1;
                                /* Check if region is uniform */
                uint16_t first_block = 0xFF;
                bool uniform = true;
                
                for (uint16_t cx = x; cx <= x2 && uniform; cx++) {
                    for (uint16_t cz = z; cz <= z2 && uniform; cz++) {
                        for (uint16_t cy = y; cy <= y2 && uniform; cy++) {
                            /* Get chunk and local coords */
                            uint16_t chunk_x = cx / CHUNK_SIZE_X;
                            uint16_t chunk_z = cz / CHUNK_SIZE_Z;
                            uint16_t local_x = cx % CHUNK_SIZE_X;
                            uint16_t local_z = cz % CHUNK_SIZE_Z;
                            
                            RawChunk* chunk = raw->chunks[chunk_x * SUPERCHUNK_SIZE_CHUNKS + chunk_z];
                            if (!chunk) {
                                uniform = false;
                                break;
                            }
                            
                            uint16_t block = chunk->blocks[local_x][local_z][cy];
                            if (first_block == 0xFF) {
                                first_block = block;
                            } else if (block != first_block) {
                                uniform = false;
                            }
                        }
                    }
                }
                
                if (uniform && first_block != 0 && volume_count < max_volumes) {
                    /* Store as mega volume */
                    volumes[volume_count].x1 = x;
                    volumes[volume_count].y1 = y;
                    volumes[volume_count].z1 = z;
                    volumes[volume_count].x2 = x2;
                    volumes[volume_count].y2 = y2;
                    volumes[volume_count].z2 = z2;
                    volumes[volume_count].block_id = first_block;
                    volume_count++;
                }
            }
        }
    }
    
    if (volume_count > 0) {
        /* Reallocate to actual size */
        Volume* final_volumes = (Volume*)realloc(volumes, sizeof(Volume) * volume_count);
        if (final_volumes) volumes = final_volumes;
        *out_volumes = volumes;
        *out_count = volume_count;
    } else {
        free(volumes);
        *out_volumes = NULL;
        *out_count = 0;
    }
    
    return 0;
}

/* Compress superchunk using mega-volumes and wall grid */
int compress_superchunk(const RawSuperchunk* raw, CompressedSuperchunk* out) {
    if (!raw || !out) return -1;
    
    superchunk_init(out, raw->scx, raw->scz);
    out->max_top_y = raw->max_top_y;
    
    printf("Debug: Finding mega-volumes...\n");
    
    /* Step 1: Find mega-volumes */
    mega_volume_detect(raw, &out->mega_volumes, &out->mega_volume_count);
    
    printf("Debug: Found %u mega-volumes\n", out->mega_volume_count);
    printf("Debug: Building wall grid...\n");
    
    /* Step 2: Build wall grid */
    wall_grid_build(raw, &out->wall_grid, &out->wall_grid_size);
    
    printf("Debug: Wall grid size: %zu bytes\n", out->wall_grid_size);
    printf("Debug: Compressing individual chunks...\n");
    
    /* Step 3: Compress individual chunks, skipping mega-volume blocks */
    int compressed_count = 0;
    for (int cx = 0; cx < SUPERCHUNK_SIZE_CHUNKS; cx++) {
        for (int cz = 0; cz < SUPERCHUNK_SIZE_CHUNKS; cz++) {
            int idx = cx * SUPERCHUNK_SIZE_CHUNKS + cz;
            RawChunk* raw_chunk = raw->chunks[idx];
            
            if (!raw_chunk) continue;
            
            /* Allocate modified chunk on heap to avoid stack overflow */
            RawChunk* modified_chunk = (RawChunk*)malloc(sizeof(RawChunk));
            if (!modified_chunk) continue;
            
            memcpy(modified_chunk, raw_chunk, sizeof(RawChunk));
            
            /* Clear blocks covered by mega volumes */
            for (uint32_t v = 0; v < out->mega_volume_count; v++) {
                Volume* mv = &out->mega_volumes[v];
                
                /* Check if this volume affects this chunk */
                uint16_t vol_chunk_x1 = mv->x1 / CHUNK_SIZE_X;
                uint16_t vol_chunk_z1 = mv->z1 / CHUNK_SIZE_Z;
                uint16_t vol_chunk_x2 = mv->x2 / CHUNK_SIZE_X;
                uint16_t vol_chunk_z2 = mv->z2 / CHUNK_SIZE_Z;
                
                if (cx >= vol_chunk_x1 && cx <= vol_chunk_x2 &&
                    cz >= vol_chunk_z1 && cz <= vol_chunk_z2) {
                    /* Clear overlapping region */
                    uint16_t local_x1 = (cx == vol_chunk_x1) ? (mv->x1 % CHUNK_SIZE_X) : 0;
                    uint16_t local_z1 = (cz == vol_chunk_z1) ? (mv->z1 % CHUNK_SIZE_Z) : 0;
                    uint16_t local_x2 = (cx == vol_chunk_x2) ? (mv->x2 % CHUNK_SIZE_X) : (CHUNK_SIZE_X - 1);
                    uint16_t local_z2 = (cz == vol_chunk_z2) ? (mv->z2 % CHUNK_SIZE_Z) : (CHUNK_SIZE_Z - 1);
                    
                    for (uint16_t x = local_x1; x <= local_x2; x++) {
                        for (uint16_t z = local_z1; z <= local_z2; z++) {
                            for (uint16_t y = mv->y1; y <= mv->y2 && y < CHUNK_SIZE_Y; y++) {
                                modified_chunk->blocks[x][z][y] = 0;  /* Mark as covered */
                            }
                        }
                    }
                }
            }
            
            /* Compress modified chunk */
            CompressedChunk* comp = (CompressedChunk*)malloc(sizeof(CompressedChunk));
            if (!comp) {
                free(modified_chunk);
                continue;
            }
            
            memset(comp, 0, sizeof(CompressedChunk));
            if (compress_chunk(modified_chunk, comp) == 0) {
                out->chunks[idx] = comp;
                compressed_count++;
            } else {
                free(comp);
            }
            
            free(modified_chunk);
        }
    }
    
    printf("Debug: Compressed %d chunks\n", compressed_count);
    
    return 0;
}

/* Decompress superchunk */
int decompress_superchunk(const CompressedSuperchunk* sc, RawSuperchunk* out) {
    if (!sc || !out) return -1;
    
    memset(out, 0, sizeof(RawSuperchunk));
    out->scx = sc->scx;
    out->scz = sc->scz;
    out->max_top_y = sc->max_top_y;
    
    /* Step 1: Allocate all chunks */
    for (int i = 0; i < SUPERCHUNK_CHUNKS; i++) {
        out->chunks[i] = (RawChunk*)calloc(1, sizeof(RawChunk));
        if (!out->chunks[i]) {
            /* Cleanup */
            for (int j = 0; j < i; j++) {
                free(out->chunks[j]);
            }
            return -1;
        }
    }
    
    /* Step 2: Apply mega-volumes first */
    for (uint32_t v = 0; v < sc->mega_volume_count; v++) {
        Volume* mv = &sc->mega_volumes[v];
        
        for (uint16_t x = mv->x1; x <= mv->x2; x++) {
            for (uint16_t z = mv->z1; z <= mv->z2; z++) {
                for (uint16_t y = mv->y1; y <= mv->y2 && y < CHUNK_SIZE_Y; y++) {
                    uint16_t chunk_x = x / CHUNK_SIZE_X;
                    uint16_t chunk_z = z / CHUNK_SIZE_Z;
                    uint16_t local_x = x % CHUNK_SIZE_X;
                    uint16_t local_z = z % CHUNK_SIZE_Z;
                    
                    RawChunk* chunk = out->chunks[chunk_x * SUPERCHUNK_SIZE_CHUNKS + chunk_z];
                    if (chunk) {
                        chunk->blocks[local_x][local_z][y] = mv->block_id;
                    }
                }
            }
        }
    }
    
    /* Step 3: Decompress individual chunks */
    for (int cx = 0; cx < SUPERCHUNK_SIZE_CHUNKS; cx++) {
        for (int cz = 0; cz < SUPERCHUNK_SIZE_CHUNKS; cz++) {
            int idx = cx * SUPERCHUNK_SIZE_CHUNKS + cz;
            CompressedChunk* comp = sc->chunks[idx];
            RawChunk* raw = out->chunks[idx];
            
            if (!comp || !raw) continue;
            
            raw->cx = cx + sc->scx * SUPERCHUNK_SIZE_CHUNKS;
            raw->cz = cz + sc->scz * SUPERCHUNK_SIZE_CHUNKS;
            
            /* Decompress on top of mega-volumes */
            RawChunk temp;
            if (decompress_chunk(comp, &temp) == 0) {
                /* Merge - temp overwrites where non-zero */
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                        for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                            if (temp.blocks[x][z][y] != 0) {
                                raw->blocks[x][z][y] = temp.blocks[x][z][y];
                            }
                        }
                    }
                }
            }
        }
    }
    
    return 0;
}

/* Encode superchunk to binary */
int superchunk_encode_binary(const CompressedSuperchunk* sc, uint8_t** out_data, size_t* out_size) {
    if (!sc || !out_data || !out_size) return -1;
    
    /* Calculate size */
    size_t size = 32;  /* Header */
    
    /* Mega volumes */
    size += 4 + sc->mega_volume_count * 14;
    
    /* Wall grid */
    size += 4 + sc->wall_grid_size;
    
    /* Individual chunks - count non-null */
    uint32_t chunk_count = 0;
    for (int i = 0; i < SUPERCHUNK_CHUNKS; i++) {
        if (sc->chunks[i]) chunk_count++;
    }
    size += 4 + chunk_count * (4 + 4);  /* chunk index + offset */
    
    /* Encode chunks to get sizes */
    uint8_t* chunk_data[SUPERCHUNK_CHUNKS] = {0};
    size_t chunk_sizes[SUPERCHUNK_CHUNKS] = {0};
    
    for (int i = 0; i < SUPERCHUNK_CHUNKS; i++) {
        if (sc->chunks[i]) {
            compress_encode_binary(sc->chunks[i], &chunk_data[i], &chunk_sizes[i]);
            size += chunk_sizes[i];
        }
    }
    
    /* Allocate */
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) {
        for (int i = 0; i < SUPERCHUNK_CHUNKS; i++) {
            free(chunk_data[i]);
        }
        return -1;
    }
    
    /* Write */
    size_t pos = 0;
    
    /* Header */
    *(uint32_t*)(&(*out_data)[pos]) = 2; pos += 4;  /* Version 2 for superchunk */
    *(int32_t*)(&(*out_data)[pos]) = sc->scx; pos += 4;
    *(int32_t*)(&(*out_data)[pos]) = sc->scz; pos += 4;
    *(uint16_t*)(&(*out_data)[pos]) = sc->max_top_y; pos += 2;
    
    /* Padding to align */
    pos += 18;
    
    /* Mega volumes */
    *(uint32_t*)(&(*out_data)[pos]) = sc->mega_volume_count; pos += 4;
    for (uint32_t i = 0; i < sc->mega_volume_count; i++) {
        *(uint16_t*)(&(*out_data)[pos]) = sc->mega_volumes[i].x1; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = sc->mega_volumes[i].y1; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = sc->mega_volumes[i].z1; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = sc->mega_volumes[i].x2; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = sc->mega_volumes[i].y2; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = sc->mega_volumes[i].z2; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = sc->mega_volumes[i].block_id; pos += 2;
    }
    
    /* Wall grid */
    *(uint32_t*)(&(*out_data)[pos]) = (uint32_t)sc->wall_grid_size; pos += 4;
    if (sc->wall_grid_size > 0 && sc->wall_grid) {
        memcpy(&(*out_data)[pos], sc->wall_grid, sc->wall_grid_size);
        pos += sc->wall_grid_size;
    }
    
    /* Chunk table */
    *(uint32_t*)(&(*out_data)[pos]) = chunk_count; pos += 4;
    size_t chunk_table_pos = pos;
    pos += chunk_count * 8;  /* Reserve space for table */
    
    /* Write chunks */
    int written = 0;
    for (int i = 0; i < (int)SUPERCHUNK_CHUNKS && written < (int)chunk_count; i++) {
        if (chunk_data[i]) {
            /* Update table */
            *(uint32_t*)(&(*out_data)[chunk_table_pos]) = i; chunk_table_pos += 4;
            *(uint32_t*)(&(*out_data)[chunk_table_pos]) = (uint32_t)pos; chunk_table_pos += 4;
            
            /* Write chunk data */
            memcpy(&(*out_data)[pos], chunk_data[i], chunk_sizes[i]);
            pos += chunk_sizes[i];
            
            free(chunk_data[i]);
            written++;
        }
    }
    
    *out_size = pos;
    return 0;
}

/* Print superchunk stats */
void superchunk_print_stats(const CompressedSuperchunk* sc, size_t original_bytes) {
    if (!sc) return;
    
    printf("Superchunk Statistics (%d, %d):\n", sc->scx, sc->scz);
    printf("  Max top_y: %d\n", sc->max_top_y);
    printf("  Mega volumes: %u\n", sc->mega_volume_count);
    
    uint64_t mega_blocks = 0;
    if (sc->mega_volumes && sc->mega_volume_count > 0) {
        for (uint32_t i = 0; i < sc->mega_volume_count; i++) {
            uint64_t vol_size = (uint64_t)(sc->mega_volumes[i].x2 - sc->mega_volumes[i].x1 + 1)
                              * (sc->mega_volumes[i].y2 - sc->mega_volumes[i].y1 + 1)
                              * (sc->mega_volumes[i].z2 - sc->mega_volumes[i].z1 + 1);
            mega_blocks += vol_size;
        }
    }
    printf("  Mega volume coverage: %llu blocks (%.1f%% of superchunk)\n", 
           (unsigned long long)mega_blocks,
           100.0 * mega_blocks / (uint64_t)(SUPERCHUNK_SIZE_X * SUPERCHUNK_SIZE_Z * CHUNK_SIZE_Y));
    
    printf("  Wall grid size: %zu bytes\n", sc->wall_grid_size);
    
    uint32_t chunk_count = 0;
    uint64_t chunk_volume_total = 0;
    for (int i = 0; i < SUPERCHUNK_CHUNKS; i++) {
        if (sc->chunks[i]) {
            chunk_count++;
            chunk_volume_total += sc->chunks[i]->volume_count;
        }
    }
    printf("  Active chunks: %u / %d\n", chunk_count, SUPERCHUNK_CHUNKS);
    printf("  Total chunk volumes: %llu\n", (unsigned long long)chunk_volume_total);
    
    /* Estimate compressed size */
    size_t estimated = 32;  /* Header */
    estimated += 4 + sc->mega_volume_count * 14;
    estimated += 4 + sc->wall_grid_size;
    estimated += 4 + chunk_count * 8;
    for (int i = 0; i < SUPERCHUNK_CHUNKS; i++) {
        if (sc->chunks[i]) {
            estimated += 20;  /* Chunk header */
            estimated += sc->chunks[i]->volume_count * 14;
            for (uint32_t j = 0; j < sc->chunks[i]->layer_count; j++) {
                estimated += 6 + sc->chunks[i]->layers[j].patch_count * 8;
            }
            estimated += 4 + sc->chunks[i]->residual_size;
        }
    }
    
    printf("  Estimated binary size: %zu bytes\n", estimated);
    printf("  Original size: %zu bytes\n", original_bytes);
    printf("  Compression ratio: %.1f%%\n", 100.0 * compress_ratio(original_bytes, estimated));
}

/* ============================================================================
 * Terrain Template Compression Implementation (Phase 1)
 * ============================================================================ */

/* Initialize standard terrain templates for natural terrain */
void template_init_defaults(TerrainTemplate* templates, int* out_count) {
    if (!templates || !out_count) return;
    
    int count = 0;
    
    /* Template 0: Deep Stone (bedrock layer)
     * Solid stone from Y=0 to Y=50
     */
    templates[count].template_id = (uint8_t)count;
    strcpy_s(templates[count].name, sizeof(templates[count].name), "Deep Stone");
    templates[count].layer_count = 1;
    templates[count].layers[0].block_id = 1;  /* Stone */
    templates[count].layers[0].y_start = 0;
    templates[count].layers[0].y_end = 50;
    count++;
    
    /* Template 1: Stone Layers
     * Stone from Y=0 to Y=200 with some variation
     */
    templates[count].template_id = (uint8_t)count;
    strcpy_s(templates[count].name, sizeof(templates[count].name), "Stone Layers");
    templates[count].layer_count = 2;
    templates[count].layers[0].block_id = 1;  /* Stone deep */
    templates[count].layers[0].y_start = 0;
    templates[count].layers[0].y_end = 100;
    templates[count].layers[1].block_id = 1;  /* Stone upper */
    templates[count].layers[1].y_start = 101;
    templates[count].layers[1].y_end = 200;
    count++;
    
    /* Template 2: Dirt Surface
     * Stone base with dirt layer and grass top
     */
    templates[count].template_id = (uint8_t)count;
    strcpy_s(templates[count].name, sizeof(templates[count].name), "Dirt Surface");
    templates[count].layer_count = 3;
    templates[count].layers[0].block_id = 1;  /* Stone */
    templates[count].layers[0].y_start = 0;
    templates[count].layers[0].y_end = 180;
    templates[count].layers[1].block_id = 2;  /* Dirt */
    templates[count].layers[1].y_start = 181;
    templates[count].layers[1].y_end = 192;
    templates[count].layers[2].block_id = 3;  /* Grass */
    templates[count].layers[2].y_start = 193;
    templates[count].layers[2].y_end = 193;
    count++;
    
    /* Template 3: Underground Caves
     * Stone with large cave voids
     */
    templates[count].template_id = (uint8_t)count;
    strcpy_s(templates[count].name, sizeof(templates[count].name), "Cave System");
    templates[count].layer_count = 3;
    templates[count].layers[0].block_id = 1;  /* Stone floor */
    templates[count].layers[0].y_start = 0;
    templates[count].layers[0].y_end = 50;
    templates[count].layers[1].block_id = 0;  /* Air - cave void */
    templates[count].layers[1].y_start = 51;
    templates[count].layers[1].y_end = 150;
    templates[count].layers[2].block_id = 1;  /* Stone ceiling */
    templates[count].layers[2].y_start = 151;
    templates[count].layers[2].y_end = 200;
    count++;
    
    /* Template 4: Mountain Terrain
     * Stone with dirt/grass at varying heights
     */
    templates[count].template_id = (uint8_t)count;
    strcpy_s(templates[count].name, sizeof(templates[count].name), "Mountain");
    templates[count].layer_count = 3;
    templates[count].layers[0].block_id = 1;  /* Stone core */
    templates[count].layers[0].y_start = 0;
    templates[count].layers[0].y_end = 150;
    templates[count].layers[1].block_id = 1;  /* Stone surface */
    templates[count].layers[1].y_start = 151;
    templates[count].layers[1].y_end = 250;
    templates[count].layers[2].block_id = 3;  /* Grass cap */
    templates[count].layers[2].y_start = 251;
    templates[count].layers[2].y_end = 251;
    count++;
    
    /* Template 5: Desert
     * Sand and sandstone layers
     */
    templates[count].template_id = (uint8_t)count;
    strcpy_s(templates[count].name, sizeof(templates[count].name), "Desert");
    templates[count].layer_count = 2;
    templates[count].layers[0].block_id = 12;  /* Sandstone base */
    templates[count].layers[0].y_start = 0;
    templates[count].layers[0].y_end = 185;
    templates[count].layers[1].block_id = 11;  /* Sand */
    templates[count].layers[1].y_start = 186;
    templates[count].layers[1].y_end = 192;
    count++;
    
    /* Template 6: Ocean Floor
     * Gravel and sand with water above
     */
    templates[count].template_id = (uint8_t)count;
    strcpy_s(templates[count].name, sizeof(templates[count].name), "Ocean Floor");
    templates[count].layer_count = 3;
    templates[count].layers[0].block_id = 1;   /* Stone base */
    templates[count].layers[0].y_start = 0;
    templates[count].layers[0].y_end = 160;
    templates[count].layers[1].block_id = 13;  /* Gravel */
    templates[count].layers[1].y_start = 161;
    templates[count].layers[1].y_end = 175;
    templates[count].layers[2].block_id = 10; /* Water */
    templates[count].layers[2].y_start = 176;
    templates[count].layers[2].y_end = 192;
    count++;
    
    *out_count = count;
}

/* Calculate how well a template matches a chunk */
static float template_calculate_match(const RawChunk* raw, const TerrainTemplate* tmpl) {
    if (!raw || !tmpl) return 0.0f;
    
    uint32_t matching = 0;
    uint32_t total = 0;
    
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                uint16_t block = raw->blocks[x][z][y];
                
                /* Find expected block from template */
                uint16_t expected = 0;
                for (uint32_t l = 0; l < tmpl->layer_count; l++) {
                    if (y >= tmpl->layers[l].y_start && y <= tmpl->layers[l].y_end) {
                        expected = (uint16_t)tmpl->layers[l].block_id;
                        break;
                    }
                }
                
                if (block == expected) {
                    matching++;
                }
                total++;
            }
        }
    }
    
    return (total > 0) ? ((float)matching / (float)total) : 0.0f;
}

/* Find best matching template for a chunk */
int template_find_best_match(const RawChunk* raw, const TerrainTemplate* templates, 
                              int template_count, float* out_score) {
    if (!raw || !templates || template_count <= 0) return -1;
    
    int best_idx = -1;
    float best_score = 0.0f;
    
    for (int i = 0; i < template_count; i++) {
        float score = template_calculate_match(raw, &templates[i]);
        if (score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }
    
    if (out_score) {
        *out_score = best_score;
    }
    
    return best_idx;
}

/* Compress chunk using template + patches - with dynamic growth */
int compress_template(const RawChunk* raw, uint8_t template_id, 
                      const TerrainTemplate* template_def,
                      TemplateCompressedChunk* out) {
    if (!raw || !template_def || !out) return -1;
    
    memset(out, 0, sizeof(TemplateCompressedChunk));
    out->template_id = template_id;
    
    /* Start with capacity for 100k patches, grow dynamically */
    uint32_t patch_capacity = 100000;
    Patch* patches = (Patch*)malloc(sizeof(Patch) * patch_capacity);
    if (!patches) return -1;
    
    uint32_t patch_count = 0;
    
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                uint16_t actual = raw->blocks[x][z][y];
                
                /* Find expected block from template */
                uint16_t expected = 0;
                for (uint32_t l = 0; l < template_def->layer_count; l++) {
                    if (y >= template_def->layers[l].y_start && 
                        y <= template_def->layers[l].y_end) {
                        expected = (uint16_t)template_def->layers[l].block_id;
                        break;
                    }
                }
                
                /* If different, add as patch */
                if (actual != expected) {
                    /* Grow array if needed */
                    if (patch_count >= patch_capacity) {
                        patch_capacity *= 2;  /* Double capacity */
                        Patch* new_patches = (Patch*)realloc(patches, sizeof(Patch) * patch_capacity);
                        if (!new_patches) {
                            free(patches);
                            return -1;
                        }
                        patches = new_patches;
                    }
                    
                    patches[patch_count].x = (uint16_t)x;
                    patches[patch_count].z = (uint16_t)z;
                    patches[patch_count].y = (uint16_t)y;
                    patches[patch_count].block_id = (uint16_t)actual;
                    patch_count++;
                }
            }
        }
    }
    
    /* Store patches - trim to exact size */
    if (patch_count > 0) {
        out->patches = (Patch*)realloc(patches, sizeof(Patch) * patch_count);
        if (!out->patches) {
            free(patches);
            return -1;
        }
        out->patch_count = patch_count;
    } else {
        free(patches);
        out->patches = NULL;
        out->patch_count = 0;
    }
    
    return 0;
}

/* Decompress template-compressed chunk */
int decompress_template(const TemplateCompressedChunk* tc, const TerrainTemplate* template_def,
                        RawChunk* out, int32_t cx, int32_t cz) {
    if (!tc || !template_def || !out) return -1;
    
    memset(out, 0, sizeof(RawChunk));
    out->cx = cx;
    out->cz = cz;
    
    /* Fill with template base */
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                uint16_t block = 0;
                for (uint32_t l = 0; l < template_def->layer_count; l++) {
                    if (y >= template_def->layers[l].y_start && 
                        y <= template_def->layers[l].y_end) {
                        block = (uint16_t)template_def->layers[l].block_id;
                        break;
                    }
                }
                out->blocks[x][z][y] = block;
            }
        }
    }
    
    /* Apply patches */
    for (uint32_t i = 0; i < tc->patch_count; i++) {
        const Patch* p = &tc->patches[i];
        if (p->x < CHUNK_SIZE_X && p->z < CHUNK_SIZE_Z && p->y < CHUNK_SIZE_Y) {
            out->blocks[p->x][p->z][p->y] = p->block_id;
        }
    }
    
    return 0;
}

/* Encode template-compressed chunk to binary */
int template_encode_binary(const TemplateCompressedChunk* tc, uint8_t** out_data, size_t* out_size) {
    if (!tc || !out_data || !out_size) return -1;
    
    /* Calculate size: header + patch table + patches */
    size_t size = 12;  /* Header: method(1) + template_id(1) + patch_count(4) + reserved(6) */
    size += tc->patch_count * 8;  /* Each patch: x(2) + z(2) + y(2) + block_id(2) */
    
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) return -1;
    
    size_t pos = 0;
    
    /* Header */
    (*out_data)[pos++] = COMPRESS_METHOD_TEMPLATE;  /* Method ID */
    (*out_data)[pos++] = tc->template_id;
    *(uint32_t*)(&(*out_data)[pos]) = tc->patch_count; pos += 4;
    pos += 6;  /* Reserved padding */
    
    /* Patches */
    for (uint32_t i = 0; i < tc->patch_count; i++) {
        *(uint16_t*)(&(*out_data)[pos]) = tc->patches[i].x; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = tc->patches[i].z; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = tc->patches[i].y; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = tc->patches[i].block_id; pos += 2;
    }
    
    *out_size = pos;
    return 0;
}

/* Print template compression statistics */
void template_print_stats(const TemplateCompressedChunk* tc, size_t original_bytes) {
    if (!tc) return;
    
    printf("Template Compression Statistics:\n");
    printf("  Template ID: %u\n", tc->template_id);
    printf("  Patches: %u\n", tc->patch_count);
    
    /* Estimate compressed size */
    size_t estimated = 12 + tc->patch_count * 8;
    
    printf("  Estimated binary size: %zu bytes\n", estimated);
    printf("  Original size: %zu bytes\n", original_bytes);
    printf("  Compression ratio: %.1f%%\n", 100.0 * compress_ratio(original_bytes, estimated));
    
    /* Patch breakdown */
    if (tc->patch_count > 0) {
        uint32_t block_counts[256] = {0};
        for (uint32_t i = 0; i < tc->patch_count; i++) {
            uint8_t block_id = (uint8_t)(tc->patches[i].block_id & 0xFF);
            block_counts[block_id]++;
        }
        
        printf("  Top patch blocks:\n");
        /* Find top 3 */
        for (int j = 0; j < 3; j++) {
            uint32_t max_count = 0;
            int max_id = -1;
            for (int i = 0; i < 256; i++) {
                if (block_counts[i] > max_count) {
                    max_count = block_counts[i];
                    max_id = i;
                }
            }
            if (max_count > 0 && max_id >= 0) {
                printf("    Block %d: %u patches (%.1f%%)\n", 
                       max_id, max_count, 100.0 * max_count / tc->patch_count);
                block_counts[max_id] = 0;  /* Clear for next iteration */
            }
        }
    }
}

/* ============================================================================
 * Octree Compression Implementation (Phase 2)
 * ============================================================================ */

/* Forward declarations */
static void octree_free(OctreeNode* node);
static void octree_count_nodes(const OctreeNode* node, uint32_t* count);

/* Check if a region is uniform (all same block type) */
static int octree_check_uniform(const RawChunk* raw, 
                                 uint16_t x, uint16_t y, uint16_t z, 
                                 uint16_t sx, uint16_t sy, uint16_t sz,
                                 uint16_t* out_block_id) {
    uint16_t first_block = 0xFF;
    int uniform = 1;
    
    for (uint16_t dx = 0; dx < sx && uniform; dx++) {
        for (uint16_t dz = 0; dz < sz && uniform; dz++) {
            for (uint16_t dy = 0; dy < sy && uniform; dy++) {
                uint16_t bx = (uint16_t)(x + dx);
                uint16_t bz = (uint16_t)(z + dz);
                uint16_t by = (uint16_t)(y + dy);
                
                if (bx >= CHUNK_SIZE_X || bz >= CHUNK_SIZE_Z || by >= CHUNK_SIZE_Y) {
                    continue;
                }
                
                uint16_t block = raw->blocks[bx][bz][by];
                if (first_block == 0xFF) {
                    first_block = block;
                } else if (block != first_block) {
                    uniform = 0;
                }
            }
        }
    }
    
    if (uniform && out_block_id) {
        *out_block_id = (first_block == 0xFF) ? 0 : first_block;
    }
    
    return uniform;
}

/* Create octree node with per-dimension sizes */
static OctreeNode* octree_create_node(uint16_t x, uint16_t y, uint16_t z, 
                                       uint16_t sx, uint16_t sy, uint16_t sz) {
    OctreeNode* node = (OctreeNode*)calloc(1, sizeof(OctreeNode));
    if (node) {
        node->x = x;
        node->y = y;
        node->z = z;
        node->size_x = sx;
        node->size_y = sy;
        node->size_z = sz;
        node->is_uniform = 0;
    }
    return node;
}

/* Recursively build octree with non-cubic support and min size threshold */
static int octree_build_recursive(const RawChunk* raw, OctreeNode* node, uint32_t* node_count) {
    if (!node || !node_count) return -1;
    
    /* Check if current node region is uniform */
    uint16_t block_id;
    if (octree_check_uniform(raw, node->x, node->y, node->z, 
                              node->size_x, node->size_y, node->size_z, &block_id)) {
        node->is_uniform = 1;
        node->block_id = block_id;
        (*node_count)++;
        return 0;
    }
    
    /* Minimum node size threshold - don't subdivide below 4x4x4 for efficiency */
    #define MIN_NODE_SIZE 4
    if (node->size_x <= MIN_NODE_SIZE && node->size_y <= MIN_NODE_SIZE && node->size_z <= MIN_NODE_SIZE) {
        /* Store actual raw block data for this non-uniform region */
        uint32_t data_size = node->size_x * node->size_y * node->size_z;
        node->raw_data = (uint16_t*)malloc(sizeof(uint16_t) * data_size);
        if (!node->raw_data) return -1;
        
        /* Copy actual block data from the raw chunk */
        uint32_t idx = 0;
        for (uint16_t dx = 0; dx < node->size_x; dx++) {
            for (uint16_t dz = 0; dz < node->size_z; dz++) {
                for (uint16_t dy = 0; dy < node->size_y; dy++) {
                    uint16_t bx = node->x + dx;
                    uint16_t bz = node->z + dz;
                    uint16_t by = node->y + dy;
                    
                    if (bx < CHUNK_SIZE_X && bz < CHUNK_SIZE_Z && by < CHUNK_SIZE_Y) {
                        node->raw_data[idx++] = raw->blocks[bx][bz][by];
                    } else {
                        node->raw_data[idx++] = 0; /* Out of bounds = air */
                    }
                }
            }
        }
        
        node->is_uniform = 0;  /* Not uniform - we have raw data */
        (*node_count)++;
        return 0;
    }
    
    /* Calculate half sizes for each dimension */
    uint16_t hx = node->size_x / 2;
    uint16_t hy = node->size_y / 2;
    uint16_t hz = node->size_z / 2;
    if (hx < 1) hx = 1;
    if (hy < 1) hy = 1;
    if (hz < 1) hz = 1;
    
    /* Create 8 children with proper non-cubic subdivision */
    for (int i = 0; i < 8; i++) {
        uint16_t cx = node->x + ((i & 1) ? hx : 0);
        uint16_t cy = node->y + ((i & 2) ? hy : 0);
        uint16_t cz = node->z + ((i & 4) ? hz : 0);
        
        /* Calculate child sizes - handle odd dimensions */
        uint16_t csx = (i & 1) ? (node->size_x - hx) : hx;
        uint16_t csy = (i & 2) ? (node->size_y - hy) : hy;
        uint16_t csz = (i & 4) ? (node->size_z - hz) : hz;
        
        node->children[i] = octree_create_node(cx, cy, cz, csx, csy, csz);
        if (!node->children[i]) {
            /* Cleanup */
            for (int j = 0; j < i; j++) {
                free(node->children[j]);
            }
            return -1;
        }
        
        /* Recursively build child */
        if (octree_build_recursive(raw, node->children[i], node_count) < 0) {
            return -1;
        }
    }
    
    (*node_count)++;
    return 0;
}

/* Build octree from raw chunk - handles full 64x64x1024 dimensions */
int octree_build(const RawChunk* raw, OctreeCompressedChunk* out) {
    if (!raw || !out) return -1;
    
    memset(out, 0, sizeof(OctreeCompressedChunk));
    
    /* Create single root node covering full 64x64x1024 chunk */
    out->roots[0] = octree_create_node(0, 0, 0, CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z);
    if (!out->roots[0]) return -1;
    out->slice_count = 1;
    
    /* Build tree */
    if (octree_build_recursive(raw, out->roots[0], &out->node_count) < 0) {
        octree_free(out->roots[0]);
        out->roots[0] = NULL;
        return -1;
    }
    
    return 0;
}

/* Free octree node recursively */
static void octree_free(OctreeNode* node) {
    if (!node) return;
    
    /* Free children first */
    for (int i = 0; i < 8; i++) {
        if (node->children[i]) {
            octree_free(node->children[i]);
            node->children[i] = NULL;
        }
    }
    
    /* Free raw data if present */
    free(node->raw_data);
    node->raw_data = NULL;
    
    free(node);
}

/* Free octree forest */
void octree_free_forest(OctreeCompressedChunk* oc) {
    if (!oc) return;
    for (int i = 0; i < oc->slice_count; i++) {
        if (oc->roots[i]) {
            octree_free(oc->roots[i]);
            oc->roots[i] = NULL;
        }
    }
    oc->slice_count = 0;
    oc->node_count = 0;
}

/* Recursively decompress octree with non-cubic support */
static void octree_decompress_recursive(const OctreeNode* node, RawChunk* out) {
    if (!node || !out) return;
    
    if (node->is_uniform) {
        /* Fill region with uniform block */
        for (uint16_t x = 0; x < node->size_x; x++) {
            for (uint16_t z = 0; z < node->size_z; z++) {
                for (uint16_t y = 0; y < node->size_y; y++) {
                    uint16_t bx = node->x + x;
                    uint16_t bz = node->z + z;
                    uint16_t by = node->y + y;
                    
                    if (bx < CHUNK_SIZE_X && bz < CHUNK_SIZE_Z && by < CHUNK_SIZE_Y) {
                        out->blocks[bx][bz][by] = node->block_id;
                    }
                }
            }
        }
    } else if (node->raw_data) {
        /* Leaf node with raw block data - copy directly */
        uint32_t idx = 0;
        for (uint16_t x = 0; x < node->size_x; x++) {
            for (uint16_t z = 0; z < node->size_z; z++) {
                for (uint16_t y = 0; y < node->size_y; y++) {
                    uint16_t bx = node->x + x;
                    uint16_t bz = node->z + z;
                    uint16_t by = node->y + y;
                    
                    if (bx < CHUNK_SIZE_X && bz < CHUNK_SIZE_Z && by < CHUNK_SIZE_Y) {
                        out->blocks[bx][bz][by] = node->raw_data[idx++];
                    } else {
                        idx++; /* Skip out-of-bounds */
                    }
                }
            }
        }
    } else {
        /* Process children */
        for (int i = 0; i < 8; i++) {
            octree_decompress_recursive(node->children[i], out);
        }
    }
}

/* Decompress octree forest to raw chunk */
int decompress_octree(const OctreeCompressedChunk* oc, RawChunk* out) {
    if (!oc || !out) return -1;
    
    memset(out, 0, sizeof(RawChunk));
    
    for (int i = 0; i < oc->slice_count; i++) {
        if (oc->roots[i]) {
            octree_decompress_recursive(oc->roots[i], out);
        }
    }
    
    return 0;
}

/* Count octree nodes for encoding */
static void octree_count_nodes(const OctreeNode* node, uint32_t* count) {
    if (!node || !count) return;
    (*count)++;
    
    if (!node->is_uniform) {
        for (int i = 0; i < 8; i++) {
            octree_count_nodes(node->children[i], count);
        }
    }
}

/* Encode octree to binary */
int octree_encode_binary(const OctreeCompressedChunk* oc, uint8_t** out_data, size_t* out_size) {
    if (!oc || !out_data || !out_size) return -1;
    
    /* Count nodes to allocate space */
    uint32_t node_count = 0;
    for (int i = 0; i < oc->slice_count; i++) {
        octree_count_nodes(oc->roots[i], &node_count);
    }
    
    /* Each node: x(2) + y(2) + z(2) + size_x(2) + size_y(2) + size_z(2) + flags(1) + block_id(1) = 14 bytes */
    size_t size = 4 + node_count * 14;  /* header + nodes */
    
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) return -1;
    
    size_t pos = 0;
    
    /* Header */
    *(uint32_t*)(&(*out_data)[pos]) = node_count; pos += 4;
    
    /* Encode nodes (simplified - just store all nodes) */
    /* Full implementation would use a stack to traverse */
    
    *out_size = pos;
    return 0;
}

/* Print octree compression statistics */
void octree_print_stats(const OctreeCompressedChunk* oc, size_t original_bytes) {
    if (!oc) return;
    
    printf("Octree Compression Statistics:\n");
    printf("  Total nodes: %u\n", oc->node_count);
    
    /* Estimate compressed size */
    size_t estimated = 4 + oc->node_count * 10;
    
    printf("  Estimated binary size: %zu bytes\n", estimated);
    printf("  Original size: %zu bytes\n", original_bytes);
    printf("  Compression ratio: %.1f%%\n", 100.0 * compress_ratio(original_bytes, estimated));
}

/* ============================================================================
 * Inter-Chunk Delta Compression Implementation (Phase 4 - Robust)
 * ============================================================================ */

/* Simple hash function for chunk data validation */
uint32_t chunk_compute_hash(const RawChunk* raw) {
    if (!raw) return 0;
    
    /* FNV-1a hash algorithm - fast and good distribution */
    uint32_t hash = 2166136261U;  /* FNV offset basis */
    
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                uint16_t block = raw->blocks[x][z][y];
                hash ^= (block & 0xFF);
                hash *= 16777619U;  /* FNV prime */
                hash ^= (block >> 8);
                hash *= 16777619U;  /* FNV prime */
            }
        }
    }
    
    /* Also hash coordinates for extra safety */
    hash ^= (uint32_t)raw->cx;
    hash *= 16777619U;
    hash ^= (uint32_t)raw->cz;
    hash *= 16777619U;
    
    return hash;
}

/* Analyze similarity between two chunks (0.0 = completely different, 1.0 = identical) */
float chunk_analyze_similarity(const RawChunk* chunk1, const RawChunk* chunk2, 
                                uint32_t* out_diff_count) {
    if (!chunk1 || !chunk2) return 0.0f;
    
    uint32_t matching = 0;
    uint32_t total = 0;
    uint32_t diffs = 0;
    
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                if (chunk1->blocks[x][z][y] == chunk2->blocks[x][z][y]) {
                    matching++;
                } else {
                    diffs++;
                }
                total++;
            }
        }
    }
    
    if (out_diff_count) {
        *out_diff_count = diffs;
    }
    
    return (total > 0) ? ((float)matching / (float)total) : 0.0f;
}

/* Find most similar chunk pairs in a superchunk */
int superchunk_find_similar_pairs(const RawSuperchunk* raw, 
                                   ChunkSimilarity* out_pairs, int max_pairs) {
    if (!raw || !out_pairs || max_pairs <= 0) return 0;
    
    int pair_count = 0;
    
    /* Compare all non-null chunks */
    for (int i = 0; i < SUPERCHUNK_CHUNKS && pair_count < max_pairs; i++) {
        if (!raw->chunks[i]) continue;

        for (int j = i + 1; j < SUPERCHUNK_CHUNKS && pair_count < max_pairs; j++) {
            if (!raw->chunks[j]) continue;

            /* Calculate similarity */
            uint32_t diff_count;
            float similarity = chunk_analyze_similarity(raw->chunks[i], raw->chunks[j], &diff_count);
            
            /* Only record if similarity is high enough (> 70%) */
            if (similarity > 0.70f) {
                out_pairs[pair_count].chunk1_cx = raw->chunks[i]->cx;
                out_pairs[pair_count].chunk1_cz = raw->chunks[i]->cz;
                out_pairs[pair_count].chunk2_cx = raw->chunks[j]->cx;
                out_pairs[pair_count].chunk2_cz = raw->chunks[j]->cz;
                out_pairs[pair_count].similarity = similarity;
                out_pairs[pair_count].diff_count = diff_count;
                pair_count++;
            }
        }
    }
    
    /* Sort by similarity (highest first) */
    for (int i = 0; i < pair_count - 1; i++) {
        for (int j = i + 1; j < pair_count; j++) {
            if (out_pairs[j].similarity > out_pairs[i].similarity) {
                ChunkSimilarity temp = out_pairs[i];
                out_pairs[i] = out_pairs[j];
                out_pairs[j] = temp;
            }
        }
    }
    
    return pair_count;
}

static int delta_decode_backup(const DeltaCompressedChunk* dc, RawChunk* out) {
    CompressedChunk backup;
    int result;

    if (!dc || !out || !dc->backup_data || dc->backup_size == 0) return -1;
    if (dc->backup_method != COMPRESS_METHOD_VOLUME_LAYER) return -1;

    memset(&backup, 0, sizeof(backup));
    if (compress_decode_binary(dc->backup_data, dc->backup_size, &backup) != 0) {
        return -1;
    }

    result = decompress_chunk(&backup, out);
    compress_chunk_free(&backup);
    return result;
}

/* Compress chunk as delta from reference - with hash validation and backup */
int compress_delta(const RawChunk* raw, const RawChunk* reference,
                    DeltaCompressedChunk* out) {
    if (!raw || !reference || !out) return -1;
    
    memset(out, 0, sizeof(DeltaCompressedChunk));
    out->ref_cx = reference->cx;
    out->ref_cz = reference->cz;
    
    /* Compute hash of reference chunk for validation */
    out->ref_hash = chunk_compute_hash(reference);

    uint32_t diff_count = 0;

    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                uint16_t actual = raw->blocks[x][z][y];
                uint16_t expected = reference->blocks[x][z][y];

                if (actual != expected) {
                    diff_count++;
                }
            }
        }
    }

    if (diff_count > 0) {
        uint32_t diff_index = 0;
        out->differences = (Patch*)malloc(sizeof(Patch) * diff_count);
        if (!out->differences) return -1;

        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                    uint16_t actual = raw->blocks[x][z][y];
                    uint16_t expected = reference->blocks[x][z][y];

                    if (actual != expected) {
                        out->differences[diff_index].x = (uint16_t)x;
                        out->differences[diff_index].z = (uint16_t)z;
                        out->differences[diff_index].y = (uint16_t)y;
                        out->differences[diff_index].block_id = actual;
                        diff_index++;
                    }
                }
            }
        }
        out->diff_count = diff_count;
    } else {
        out->differences = NULL;
        out->diff_count = 0;
    }

    {
        CompressedChunk backup;
        memset(&backup, 0, sizeof(backup));

        if (compress_chunk(raw, &backup) == 0 &&
            compress_encode_binary(&backup, &out->backup_data, &out->backup_size) == 0) {
            out->backup_method = COMPRESS_METHOD_VOLUME_LAYER;
            compress_chunk_free(&backup);
        } else {
            compress_chunk_free(&backup);
            free(out->differences);
            out->differences = NULL;
            out->diff_count = 0;
            return -1;
        }
    }
    
    return 0;
}

/* Decompress delta chunk using reference - with hash validation and backup fallback */
int decompress_delta(const DeltaCompressedChunk* dc, const RawChunk* reference,
                      RawChunk* out) {
    if (!dc || !out) return -1;
    
    /* Verify reference chunk hash if provided */
    if (reference) {
        uint32_t current_hash = chunk_compute_hash(reference);
        if (current_hash != dc->ref_hash) {
            /* Hash mismatch - reference chunk has changed! Use backup instead */
            printf("Warning: Delta reference chunk hash mismatch (expected %u, got %u)\n",
                   dc->ref_hash, current_hash);
            printf("Using backup compression method %d instead\n", dc->backup_method);
            
            return delta_decode_backup(dc, out);
        }
        
        /* Hash matches - safe to use delta */
        memcpy(out, reference, sizeof(RawChunk));
        out->cx = dc->ref_cx;  /* Will be overridden by caller */
        out->cz = dc->ref_cz;
        
        /* Apply differences */
        for (uint32_t i = 0; i < dc->diff_count; i++) {
            const Patch* p = &dc->differences[i];
            if (p->x < CHUNK_SIZE_X && p->z < CHUNK_SIZE_Z && p->y < CHUNK_SIZE_Y) {
                out->blocks[p->x][p->z][p->y] = p->block_id;
            }
        }
        return 0;
    } else {
        /* No reference provided - must use backup */
        printf("No reference chunk provided - using backup\n");
        return delta_decode_backup(dc, out);
    }
}

/* Free delta compressed chunk including backup data */
void delta_free(DeltaCompressedChunk* dc) {
    if (!dc) return;
    
    if (dc->differences) {
        free(dc->differences);
        dc->differences = NULL;
    }
    
    if (dc->backup_data) {
        free(dc->backup_data);
        dc->backup_data = NULL;
    }
    
    dc->diff_count = 0;
    dc->backup_size = 0;
}

/* Encode delta-compressed chunk to binary */
int delta_encode_binary(const DeltaCompressedChunk* dc, uint8_t** out_data, size_t* out_size) {
    if (!dc || !out_data || !out_size) return -1;
    
    /* Calculate size: header + ref_hash + backup_method + backup_size + backup_data + differences */
    size_t size = 24 + dc->backup_size + dc->diff_count * 8;  /* header + backup + diffs */
    
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) return -1;
    
    size_t pos = 0;
    
    /* Header */
    (*out_data)[pos++] = COMPRESS_METHOD_INTER_CHUNK_DELTA;
    *(int32_t*)(&(*out_data)[pos]) = dc->ref_cx; pos += 4;
    *(int32_t*)(&(*out_data)[pos]) = dc->ref_cz; pos += 4;
    *(uint32_t*)(&(*out_data)[pos]) = dc->ref_hash; pos += 4;
    *(uint32_t*)(&(*out_data)[pos]) = dc->diff_count; pos += 4;
    (*out_data)[pos++] = dc->backup_method;
    *(uint32_t*)(&(*out_data)[pos]) = (uint32_t)dc->backup_size; pos += 4;
    
    /* Backup data */
    if (dc->backup_data && dc->backup_size > 0) {
        memcpy(&(*out_data)[pos], dc->backup_data, dc->backup_size);
        pos += dc->backup_size;
    }
    
    /* Differences */
    for (uint32_t i = 0; i < dc->diff_count; i++) {
        *(uint16_t*)(&(*out_data)[pos]) = dc->differences[i].x; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = dc->differences[i].z; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = dc->differences[i].y; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = dc->differences[i].block_id; pos += 2;
    }
    
    *out_size = pos;
    return 0;
}

/* Print delta compression statistics */
void delta_print_stats(const DeltaCompressedChunk* dc, size_t original_bytes) {
    if (!dc) return;
    
    printf("Inter-Chunk Delta Compression Statistics:\n");
    printf("  Reference chunk: (%d, %d)\n", dc->ref_cx, dc->ref_cz);
    printf("  Differences: %u\n", dc->diff_count);
    
    /* Estimate compressed size */
    size_t estimated = 24 + dc->backup_size + dc->diff_count * 8;
    
    printf("  Estimated binary size: %zu bytes\n", estimated);
    printf("  Original size: %zu bytes\n", original_bytes);
    printf("  Compression ratio: %.1f%%\n", 100.0 * compress_ratio(original_bytes, estimated));
    printf("  Backup chunk size: %zu bytes\n", dc->backup_size);
    
    /* Difference breakdown */
    if (dc->diff_count > 0) {
        uint32_t block_counts[256] = {0};
        for (uint32_t i = 0; i < dc->diff_count; i++) {
            uint8_t block_id = (uint8_t)(dc->differences[i].block_id & 0xFF);
            block_counts[block_id]++;
        }
        
        printf("  Top difference blocks:\n");
        /* Find top 3 */
        for (int j = 0; j < 3; j++) {
            uint32_t max_count = 0;
            int max_id = -1;
            for (int i = 0; i < 256; i++) {
                if (block_counts[i] > max_count) {
                    max_count = block_counts[i];
                    max_id = i;
                }
            }
            if (max_count > 0 && max_id >= 0) {
                printf("    Block %d: %u diffs (%.1f%%)\n", 
                       max_id, max_count, 100.0 * max_count / dc->diff_count);
                block_counts[max_id] = 0;
            }
        }
    }
}

/* ============================================================================
 * Automatic Template Generation Implementation
 * ============================================================================ */

/* Extract template from a single chunk by analyzing its layer structure */
int template_extract_from_chunk(const RawChunk* raw, TerrainTemplate* out_template, char* name) {
    if (!raw || !out_template) return -1;
    
    memset(out_template, 0, sizeof(TerrainTemplate));
    if (name) {
        strncpy_s(out_template->name, sizeof(out_template->name), name, 31);
    }
    
    /* 
     * Find the actual surface by looking at top_y and sampling columns.
     * The chunk's top_y field tells us the highest Y with any blocks.
     */
    uint32_t max_surface_y = raw->top_y;
    
    /* If top_y is 0 or very low, scan to find actual surface */
    if (max_surface_y < 10) {
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int y = CHUNK_SIZE_Y - 1; y >= 0; y--) {
                    if (raw->blocks[x][z][y] != 0) {
                        if ((uint32_t)y > max_surface_y) {
                            max_surface_y = (uint32_t)y;
                        }
                        break;
                    }
                }
            }
        }
    }
    
    /* Determine most common block types at different depths */
    uint32_t block_counts_surface[256] = {0};
    uint32_t block_counts_deep[256] = {0};
    uint32_t surface_samples = 0, deep_samples = 0;
    
    /* Sample at surface and deep levels */
    for (int x = 0; x < CHUNK_SIZE_X; x += 4) {
        for (int z = 0; z < CHUNK_SIZE_Z; z += 4) {
            /* Find surface for this column */
            int col_surface = -1;
            for (int y = (int)max_surface_y; y >= 0; y--) {
                if (raw->blocks[x][z][y] != 0) {
                    col_surface = y;
                    break;
                }
            }
            
            if (col_surface >= 0) {
                uint16_t surf_block = raw->blocks[x][z][col_surface];
                block_counts_surface[surf_block]++;
                surface_samples++;
                
                /* Sample deep block (20 below surface, or at Y=10 if surface is low) */
                int deep_y = col_surface - 20;
                if (deep_y < 0) deep_y = 10;
                uint16_t deep_block = raw->blocks[x][z][deep_y];
                if (deep_block != 0) {
                    block_counts_deep[deep_block]++;
                    deep_samples++;
                }
            }
        }
    }
    
    /* Find most common blocks */
    uint8_t surface_block = 2;  /* Default dirt */
    uint8_t deep_block = 1;     /* Default stone */
    
    uint32_t max_surf = 0, max_deep = 0;
    for (int b = 1; b < 256; b++) {
        if (block_counts_surface[b] > max_surf) {
            max_surf = block_counts_surface[b];
            surface_block = (uint8_t)b;
        }
        if (block_counts_deep[b] > max_deep) {
            max_deep = block_counts_deep[b];
            deep_block = (uint8_t)b;
        }
    }
    
    /* Build surface-relative template */
    uint32_t s = max_surface_y;
    int layer = 0;
    
    /* Layer 0: Bedrock at bottom */
    out_template->layers[layer].block_id = 7;
    out_template->layers[layer].y_start = 0;
    out_template->layers[layer].y_end = 4;
    layer++;
    
    /* Layer 1: Deep stone */
    if (s > 30) {
        out_template->layers[layer].block_id = deep_block;
        out_template->layers[layer].y_start = 5;
        out_template->layers[layer].y_end = (uint16_t)(s - 25);
        layer++;
    }
    
    /* Layer 2: Stone near surface */
    if (s > 10) {
        out_template->layers[layer].block_id = 1;  /* Stone */
        out_template->layers[layer].y_start = (s > 30) ? (uint16_t)(s - 24) : 5;
        out_template->layers[layer].y_end = (uint16_t)(s - 5);
        layer++;
    }
    
    /* Layer 3: Dirt transition */
    if (s > 5) {
        out_template->layers[layer].block_id = 2;  /* Dirt */
        out_template->layers[layer].y_start = (uint16_t)(s - 4);
        out_template->layers[layer].y_end = (uint16_t)(s - 1);
        layer++;
    }
    
    /* Layer 4: Surface block */
    out_template->layers[layer].block_id = surface_block;
    out_template->layers[layer].y_start = (uint16_t)s;
    out_template->layers[layer].y_end = (uint16_t)s;
    layer++;
    
    out_template->layer_count = layer;
    
    return 0;
}

/* ============================================================================
 * Compression Method Selection Implementation (Phase 5)
 * ============================================================================ */

static void maybe_update_recommendation(CompressionRecommendation* out,
                                        int method_id,
                                        const char* method_name,
                                        size_t encoded_size,
                                        int compress_ms,
                                        int decompress_ms) {
    if (!out || !method_name) return;

    if (encoded_size < out->estimated_size) {
        out->method_id = method_id;
        out->method_name = method_name;
        out->estimated_size = encoded_size;
        out->compression_ratio = (float)encoded_size / (float)CHUNK_RAW_BYTES;
        out->compress_ms = compress_ms;
        out->decompress_ms = decompress_ms;
    }
}

/* Analyze chunk and recommend best compression method */
void compress_analyze_and_recommend(const RawChunk* raw, 
                                     CompressionRecommendation* out) {
    if (!raw || !out) return;
    
    /* Initialize defaults to worst case */
    out->method_id = COMPRESS_METHOD_VOLUME_LAYER;
    out->method_name = "Volume+Layer";
    out->compression_ratio = 1.0f;  /* 100% = no compression */
    out->estimated_size = CHUNK_RAW_BYTES;
    out->compress_ms = 100;
    out->decompress_ms = 10;

    {
        CompressedChunk cc;
        uint8_t* encoded = NULL;
        size_t encoded_size = 0;
        memset(&cc, 0, sizeof(cc));
        if (compress_chunk(raw, &cc) == 0 &&
            compress_encode_binary(&cc, &encoded, &encoded_size) == 0) {
            maybe_update_recommendation(out, COMPRESS_METHOD_VOLUME_LAYER,
                                        "Volume+Layer", encoded_size, 12, 6);
        }
        free(encoded);
        compress_chunk_free(&cc);
    }

    {
        TerrainTemplate templates[32];
        int template_count = 0;
        float score = 0.0f;
        int best_template_idx;
        template_init_defaults(templates, &template_count);
        best_template_idx = template_find_best_match(raw, templates, template_count, &score);

        if (best_template_idx >= 0 && score > 0.5f) {
            TemplateCompressedChunk tc;
            uint8_t* encoded = NULL;
            size_t encoded_size = 0;
            memset(&tc, 0, sizeof(tc));
            if (compress_template(raw, (uint8_t)best_template_idx, &templates[best_template_idx], &tc) == 0 &&
                template_encode_binary(&tc, &encoded, &encoded_size) == 0) {
                maybe_update_recommendation(out, COMPRESS_METHOD_TEMPLATE,
                                            templates[best_template_idx].name,
                                            encoded_size, 10, 5);
            }
            free(encoded);
            free(tc.patches);
        }
    }

    {
        BitPackedCompressedChunk bc;
        uint8_t* encoded = NULL;
        size_t encoded_size = 0;
        memset(&bc, 0, sizeof(bc));
        if (compress_bitpack(raw, &bc) == 0 &&
            bitpack_encode_binary(&bc, &encoded, &encoded_size) == 0) {
            maybe_update_recommendation(out, COMPRESS_METHOD_BITPACK,
                                        "Bit-pack", encoded_size, 4, 2);
        }
        free(encoded);
        bitpack_free(&bc);
    }

    {
        SparseColumnCompressedChunk sc;
        uint8_t* encoded = NULL;
        size_t encoded_size = 0;
        memset(&sc, 0, sizeof(sc));
        if (compress_sparse_column(raw, &sc) == 0 &&
            sparse_column_encode_binary(&sc, &encoded, &encoded_size) == 0) {
            maybe_update_recommendation(out, COMPRESS_METHOD_SPARSE_COLUMN,
                                        "Sparse column", encoded_size, 5, 2);
        }
        free(encoded);
        sparse_column_free(&sc);
    }

    {
        RLEBitmaskCompressedChunk rc;
        uint8_t* encoded = NULL;
        size_t encoded_size = 0;
        memset(&rc, 0, sizeof(rc));
        if (compress_rle_bitmask(raw, &rc) == 0 &&
            rle_bitmask_encode_binary(&rc, &encoded, &encoded_size) == 0) {
            maybe_update_recommendation(out, COMPRESS_METHOD_RLE_BITMASK,
                                        "RLE+Bitmask", encoded_size, 11, 7);
        }
        free(encoded);
        rle_bitmask_free(&rc);
    }

    {
        HierarchicalRLECompressedChunk hc;
        uint8_t* encoded = NULL;
        size_t encoded_size = 0;
        memset(&hc, 0, sizeof(hc));
        if (compress_hierarchical_rle(raw, &hc) == 0 &&
            hierarchical_rle_encode_binary(&hc, &encoded, &encoded_size) == 0) {
            maybe_update_recommendation(out, COMPRESS_METHOD_HIERARCHICAL_RLE,
                                        "Hierarchical RLE V2", encoded_size, 16, 7);
        }
        free(encoded);
        hierarchical_rle_free(&hc);
    }
}

/* Auto-compress chunk using best method */
int compress_auto(const RawChunk* raw, uint8_t** out_data, size_t* out_size,
                  CompressionRecommendation* out_info) {
    if (!raw || !out_data || !out_size) return -1;
    
    /* Analyze and get recommendation */
    CompressionRecommendation rec;
    compress_analyze_and_recommend(raw, &rec);
    
    /* Use recommended method */
    int result = -1;
    
    switch (rec.method_id) {
        case COMPRESS_METHOD_RLE_BITMASK: {
            RLEBitmaskCompressedChunk rc;
            if (compress_rle_bitmask(raw, &rc) == 0) {
                result = rle_bitmask_encode_binary(&rc, out_data, out_size);
                rle_bitmask_free(&rc);
            }
            break;
        }
        
        case COMPRESS_METHOD_TEMPLATE: {
            /* Initialize templates and find best match */
            TerrainTemplate templates[32];
            int template_count = 0;
            template_init_defaults(templates, &template_count);
            
            float score;
            int best = template_find_best_match(raw, templates, template_count, &score);
            if (best >= 0) {
                TemplateCompressedChunk tc;
                if (compress_template(raw, (uint8_t)best, &templates[best], &tc) == 0) {
                    result = template_encode_binary(&tc, out_data, out_size);
                    free(tc.patches);
                }
            }
            break;
        }

        case COMPRESS_METHOD_BITPACK: {
            BitPackedCompressedChunk bc;
            if (compress_bitpack(raw, &bc) == 0) {
                result = bitpack_encode_binary(&bc, out_data, out_size);
                bitpack_free(&bc);
            }
            break;
        }

        case COMPRESS_METHOD_SPARSE_COLUMN: {
            SparseColumnCompressedChunk sc;
            if (compress_sparse_column(raw, &sc) == 0) {
                result = sparse_column_encode_binary(&sc, out_data, out_size);
                sparse_column_free(&sc);
            }
            break;
        }

        case COMPRESS_METHOD_HIERARCHICAL_RLE: {
            HierarchicalRLECompressedChunk hc;
            if (compress_hierarchical_rle(raw, &hc) == 0) {
                result = hierarchical_rle_encode_binary(&hc, out_data, out_size);
                hierarchical_rle_free(&hc);
            }
            break;
        }
        
        case COMPRESS_METHOD_VOLUME_LAYER:
        default: {
            /* Fall back to volume+layer */
            CompressedChunk cc;
            memset(&cc, 0, sizeof(cc));
            if (compress_chunk(raw, &cc) == 0) {
                result = compress_encode_binary(&cc, out_data, out_size);
                compress_chunk_free(&cc);
            }
            break;
        }
    }
    
    /* Return recommendation if requested */
    if (out_info) {
        *out_info = rec;
    }
    
    return result;
}

/* ============================================================================
 * Compression Header Format Implementation
 * ============================================================================ */

/* Initialize header with default values */
void compress_header_init(ChunkCompressionHeader* header, int method_id, size_t uncompressed) {
    if (!header) return;
    
    header->method_id = (uint8_t)method_id;
    header->version = 1;  /* Current format version */
    header->flags = 0;
    header->uncompressed_size = (uint32_t)uncompressed;
    header->compressed_size = 0;
    header->checksum = 0;  /* To be computed */
}

/* Write header to binary buffer */
int compress_write_header(const ChunkCompressionHeader* header, uint8_t* buffer, size_t* pos) {
    if (!header || !buffer || !pos) return -1;
    
    size_t p = *pos;
    
    /* Ensure enough space */
    if (p + 16 > 4096) return -1;  /* Assuming reasonable buffer size */
    
    buffer[p++] = header->method_id;
    buffer[p++] = header->version;
    *(uint16_t*)(&buffer[p]) = header->flags; p += 2;
    *(uint32_t*)(&buffer[p]) = header->uncompressed_size; p += 4;
    *(uint32_t*)(&buffer[p]) = header->compressed_size; p += 4;
    *(uint32_t*)(&buffer[p]) = header->checksum; p += 4;
    
    *pos = p;
    return 0;
}

/* Read header from binary buffer */
int compress_read_header(const uint8_t* buffer, size_t size, ChunkCompressionHeader* header) {
    if (!buffer || !header || size < 16) return -1;
    
    size_t p = 0;
    
    header->method_id = buffer[p++];
    header->version = buffer[p++];
    header->flags = *(uint16_t*)(&buffer[p]); p += 2;
    header->uncompressed_size = *(uint32_t*)(&buffer[p]); p += 4;
    header->compressed_size = *(uint32_t*)(&buffer[p]); p += 4;
    header->checksum = *(uint32_t*)(&buffer[p]); p += 4;
    
    /* Validate */
    if (header->version != 1) return -1;
    if (header->method_id > COMPRESS_METHOD_SUPERCHUNK_HIER) return -1;
    
    return 0;
}

/* ============================================================================
 * Bit-Packed Palette Compression Implementation (NEW - Phase 6)
 * ============================================================================ */

/* Count unique block types in chunk and build palette */
static int bitpack_build_palette(const RawChunk* raw, uint8_t* palette, uint8_t* palette_size) {
    uint32_t block_counts[256] = {0};
    
    /* Count all blocks */
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                block_counts[raw->blocks[x][z][y]]++;
            }
        }
    }
    
    /* Sort by frequency and build palette */
    *palette_size = 0;
    while (*palette_size < 64) {  /* Max 64 entries for 6-bit mode */
        uint32_t max_count = 0;
        int max_block = -1;
        
        for (int b = 0; b < 256; b++) {
            if (block_counts[b] > max_count) {
                max_count = block_counts[b];
                max_block = b;
            }
        }
        
        if (max_block < 0 || max_count == 0) break;
        
        palette[*palette_size] = (uint8_t)max_block;
        block_counts[max_block] = 0;  /* Clear for next iteration */
        (*palette_size)++;
    }
    
    return 0;
}

/* Find palette index for a block ID */
static int bitpack_find_index(const uint8_t* palette, uint8_t palette_size, uint8_t block_id) {
    for (int i = 0; i < palette_size; i++) {
        if (palette[i] == block_id) return i;
    }
    return -1;  /* Not found */
}

/* Compress chunk using bit-packed palette */
int compress_bitpack(const RawChunk* raw, BitPackedCompressedChunk* out) {
    if (!raw || !out) return -1;
    
    memset(out, 0, sizeof(BitPackedCompressedChunk));
    
    /* Build palette */
    uint8_t palette[64];
    uint8_t palette_size;
    if (bitpack_build_palette(raw, palette, &palette_size) < 0) return -1;
    
    /* Determine bits per block based on palette size */
    if (palette_size <= 32) {
        out->bits_per_block = 5;
    } else if (palette_size <= 64) {
        out->bits_per_block = 6;
    } else {
        out->bits_per_block = 8;  /* Fall back to 8 bits if >64 unique blocks */
        palette_size = 0;  /* No palette, store raw */
    }
    
    out->palette_size = palette_size;
    if (palette_size > 0) {
        memcpy(out->palette, palette, palette_size);
    }
    
    /* Calculate packed data size */
    uint32_t total_blocks = CHUNK_TOTAL_BLOCKS;
    uint64_t total_bits = (uint64_t)total_blocks * out->bits_per_block;
    out->data_size = (uint32_t)((total_bits + 7) / 8);  /* Round up to bytes */
    
    /* Allocate packed data */
    out->data = (uint8_t*)calloc(1, out->data_size);
    if (!out->data) return -1;
    
    /* Pack blocks into bits */
    uint64_t bit_pos = 0;
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                uint16_t block_id = raw->blocks[x][z][y];
                uint8_t index = (uint8_t)block_id;  /* Default: use block_id directly (8-bit mode) */
                
                if (palette_size > 0) {
                    int idx = bitpack_find_index(palette, palette_size, (uint8_t)block_id);
                    if (idx < 0) {
                        /* Block not in palette - fall back to 8-bit mode */
                        free(out->data);
                        out->data = NULL;
                        out->palette_size = 0;
                        out->bits_per_block = 8;
                        out->data_size = CHUNK_TOTAL_BLOCKS;
                        out->data = (uint8_t*)malloc(out->data_size);
                        if (!out->data) return -1;
                        
                        /* Simple copy fallback */
                        uint32_t pos = 0;
                        for (int xx = 0; xx < CHUNK_SIZE_X; xx++) {
                            for (int zz = 0; zz < CHUNK_SIZE_Z; zz++) {
                                for (int yy = 0; yy < CHUNK_SIZE_Y; yy++) {
                                    out->data[pos++] = (uint8_t)raw->blocks[xx][zz][yy];
                                }
                            }
                        }
                        return 0;
                    }
                    index = (uint8_t)idx;
                }
                
                /* Write index to packed data */
                uint64_t byte_pos = bit_pos / 8;
                uint8_t bit_offset = (uint8_t)(bit_pos % 8);
                
                /* Write bits across byte boundaries */
                uint8_t bits_remaining = out->bits_per_block;
                uint8_t bits_to_write = index;
                
                while (bits_remaining > 0) {
                    uint8_t bits_in_current_byte = (uint8_t)(8 - bit_offset);
                    uint8_t bits_now = (bits_remaining < bits_in_current_byte) ? 
                                       bits_remaining : bits_in_current_byte;
                    
                    uint8_t mask = (uint8_t)((1 << bits_now) - 1);
                    out->data[byte_pos] |= (bits_to_write & mask) << bit_offset;
                    
                    bits_to_write >>= bits_now;
                    bit_offset += bits_now;
                    bits_remaining -= bits_now;
                    
                    if (bit_offset >= 8) {
                        byte_pos++;
                        bit_offset = 0;
                    }
                }
                
                bit_pos += out->bits_per_block;
            }
        }
    }
    
    return 0;
}

/* Decompress bit-packed chunk */
int decompress_bitpack(const BitPackedCompressedChunk* bc, RawChunk* out) {
    if (!bc || !out) return -1;
    
    memset(out, 0, sizeof(RawChunk));
    
    /* If 8-bit mode without palette, simple copy */
    if (bc->bits_per_block == 8 && bc->palette_size == 0) {
        if (!bc->data || bc->data_size < CHUNK_TOTAL_BLOCKS) return -1;
        
        uint32_t pos = 0;
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                    out->blocks[x][z][y] = bc->data[pos++];
                }
            }
        }
        return 0;
    }
    
    /* Palette mode - unpack bits */
    uint64_t bit_pos = 0;
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                /* Read index from packed data */
                uint64_t byte_pos = bit_pos / 8;
                uint8_t bit_offset = (uint8_t)(bit_pos % 8);
                
                uint8_t index = 0;
                uint8_t bits_remaining = bc->bits_per_block;
                uint8_t bits_read = 0;
                
                while (bits_remaining > 0) {
                    uint8_t bits_in_current_byte = (uint8_t)(8 - bit_offset);
                    uint8_t bits_now = (bits_remaining < bits_in_current_byte) ? 
                                       bits_remaining : bits_in_current_byte;
                    
                    uint8_t mask = (uint8_t)((1 << bits_now) - 1);
                    index |= ((bc->data[byte_pos] >> bit_offset) & mask) << bits_read;
                    
                    bits_read += bits_now;
                    bit_offset += bits_now;
                    bits_remaining -= bits_now;
                    
                    if (bit_offset >= 8) {
                        byte_pos++;
                        bit_offset = 0;
                    }
                }
                
                /* Map index to block ID via palette */
                if (index < bc->palette_size) {
                    out->blocks[x][z][y] = bc->palette[index];
                } else {
                    return -1;  /* Invalid index */
                }
                
                bit_pos += bc->bits_per_block;
            }
        }
    }
    
    return 0;
}

/* Encode bit-packed chunk to binary */
int bitpack_encode_binary(const BitPackedCompressedChunk* bc, uint8_t** out_data, size_t* out_size) {
    if (!bc || !out_data || !out_size) return -1;
    
    /* Size: header + palette + packed data */
    size_t size = 4 + bc->palette_size + bc->data_size;
    
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) return -1;
    
    size_t pos = 0;
    
    /* Header: method_id(1) + bits_per_block(1) + palette_size(1) + reserved(1) */
    (*out_data)[pos++] = COMPRESS_METHOD_BITPACK;
    (*out_data)[pos++] = bc->bits_per_block;
    (*out_data)[pos++] = bc->palette_size;
    (*out_data)[pos++] = 0;  /* Reserved */
    
    /* Palette */
    if (bc->palette_size > 0) {
        memcpy(&(*out_data)[pos], bc->palette, bc->palette_size);
        pos += bc->palette_size;
    }
    
    /* Packed data */
    memcpy(&(*out_data)[pos], bc->data, bc->data_size);
    pos += bc->data_size;
    
    *out_size = pos;
    return 0;
}

/* Free bit-packed chunk memory */
void bitpack_free(BitPackedCompressedChunk* bc) {
    if (!bc) return;
    if (bc->data) {
        free(bc->data);
        bc->data = NULL;
    }
    bc->data_size = 0;
    bc->palette_size = 0;
}

/* Print bit-pack compression statistics */
void bitpack_print_stats(const BitPackedCompressedChunk* bc, size_t original_bytes) {
    if (!bc) return;
    
    printf("Bit-Packed Palette Compression Statistics:\n");
    printf("  Bits per block: %u\n", bc->bits_per_block);
    printf("  Palette size: %u\n", bc->palette_size);
    
    /* Calculate compressed size */
    size_t compressed = 4 + bc->palette_size + bc->data_size;
    
    printf("  Compressed size: %zu bytes\n", compressed);
    printf("  Original size: %zu bytes\n", original_bytes);
    printf("  Compression ratio: %.1f%%\n", 100.0 * compress_ratio(original_bytes, compressed));
    
    if (bc->palette_size > 0) {
        printf("  Palette entries:\n");
        for (int i = 0; i < bc->palette_size && i < 8; i++) {
            printf("    [%d] -> Block %d\n", i, bc->palette[i]);
        }
        if (bc->palette_size > 8) {
            printf("    ... and %d more\n", bc->palette_size - 8);
        }
    }
}

/* ============================================================================
 * Sparse Column Storage Compression Implementation (NEW - Phase 6)
 * ============================================================================ */

/* Compress chunk using sparse column storage - only stores non-air columns */
int compress_sparse_column(const RawChunk* raw, SparseColumnCompressedChunk* out) {
    if (!raw || !out) return -1;
    
    memset(out, 0, sizeof(SparseColumnCompressedChunk));
    out->total_stored_blocks = 0;
    
    /* Find surface height and column data for each X,Z column */
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            /* Find highest non-air block in this column */
            int surface_y = -1;
            for (int y = CHUNK_SIZE_Y - 1; y >= 0; y--) {
                if (raw->blocks[x][z][y] != 0) {
                    surface_y = y;
                    break;
                }
            }
            
            out->surface_height[x][z] = (uint16_t)(surface_y + 1);  /* 0 if empty, else height+1 */
            
            /* If column has blocks, store them */
            if (surface_y >= 0) {
                out->has_column[x][z] = 1;
                uint16_t height = (uint16_t)(surface_y + 1);
                out->columns[x][z].height = height;
                
                /* Allocate and copy column data */
                out->columns[x][z].blocks = (uint8_t*)malloc(height);
                if (!out->columns[x][z].blocks) {
                    /* Cleanup on failure */
                    for (int cx = 0; cx <= x; cx++) {
                        for (int cz = 0; cz < CHUNK_SIZE_Z; cz++) {
                            if (cx < x || cz <= z) {
                                free(out->columns[cx][cz].blocks);
                            }
                        }
                    }
                    return -1;
                }
                
                /* Copy blocks from Y=0 to surface */
                for (int y = 0; y < height; y++) {
                    out->columns[x][z].blocks[y] = (uint8_t)raw->blocks[x][z][y];
                }
                
                out->total_stored_blocks += height;
            }
        }
    }
    
    return 0;
}

/* Decompress sparse column chunk */
int decompress_sparse_column(const SparseColumnCompressedChunk* sc, RawChunk* out) {
    if (!sc || !out) return -1;
    
    memset(out, 0, sizeof(RawChunk));
    
    /* Reconstruct chunk from column data */
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            if (sc->has_column[x][z] && sc->columns[x][z].blocks) {
                uint16_t height = sc->columns[x][z].height;
                for (int y = 0; y < height && y < CHUNK_SIZE_Y; y++) {
                    out->blocks[x][z][y] = sc->columns[x][z].blocks[y];
                }
                /* Above surface is already 0 (air) from memset */
            }
            /* If no column, entire column is air (0) */
        }
    }
    
    return 0;
}

/* Encode sparse column chunk to binary */
int sparse_column_encode_binary(const SparseColumnCompressedChunk* sc, uint8_t** out_data, size_t* out_size) {
    if (!sc || !out_data || !out_size) return -1;
    
    /* Calculate size: header + heightmap + column data */
    size_t size = 8;  /* Header */
    size += 64 * 64 * 2;  /* Heightmap (64x64 uint16) */
    size += 64 * 64;      /* has_column bitmask */
    
    /* Column data */
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            if (sc->has_column[x][z]) {
                size += 2;  /* height uint16 */
                size += sc->columns[x][z].height;  /* block data */
            }
        }
    }
    
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) return -1;
    
    size_t pos = 0;
    
    /* Header: method_id(1) + reserved(3) + total_stored_blocks(4) */
    (*out_data)[pos++] = COMPRESS_METHOD_SPARSE_COLUMN;
    (*out_data)[pos++] = 0;
    (*out_data)[pos++] = 0;
    (*out_data)[pos++] = 0;
    *(uint32_t*)(&(*out_data)[pos]) = sc->total_stored_blocks; pos += 4;
    
    /* Heightmap */
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            *(uint16_t*)(&(*out_data)[pos]) = sc->surface_height[x][z]; pos += 2;
        }
    }
    
    /* has_column bitmask */
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            (*out_data)[pos++] = sc->has_column[x][z];
        }
    }
    
    /* Column data */
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            if (sc->has_column[x][z]) {
                *(uint16_t*)(&(*out_data)[pos]) = sc->columns[x][z].height; pos += 2;
                memcpy(&(*out_data)[pos], sc->columns[x][z].blocks, sc->columns[x][z].height);
                pos += sc->columns[x][z].height;
            }
        }
    }
    
    *out_size = pos;
    return 0;
}

/* Free sparse column chunk memory */
void sparse_column_free(SparseColumnCompressedChunk* sc) {
    if (!sc) return;
    
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            if (sc->columns[x][z].blocks) {
                free(sc->columns[x][z].blocks);
                sc->columns[x][z].blocks = NULL;
            }
        }
    }
    sc->total_stored_blocks = 0;
}

/* Print sparse column compression statistics */
void sparse_column_print_stats(const SparseColumnCompressedChunk* sc, size_t original_bytes) {
    if (!sc) return;
    
    printf("Sparse Column Storage Compression Statistics:\n");
    
    /* Count non-empty columns */
    uint32_t non_empty_cols = 0;
    uint32_t max_height = 0;
    uint32_t total_height = 0;
    
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            if (sc->has_column[x][z]) {
                non_empty_cols++;
                total_height += sc->surface_height[x][z];
                if (sc->surface_height[x][z] > max_height) {
                    max_height = sc->surface_height[x][z];
                }
            }
        }
    }
    
    printf("  Non-empty columns: %u / %d (%.1f%%)\n", 
           non_empty_cols, 64*64, 100.0 * non_empty_cols / (64*64));
    printf("  Total stored blocks: %u\n", sc->total_stored_blocks);
    printf("  Average column height: %.1f\n", 
           non_empty_cols > 0 ? (float)total_height / non_empty_cols : 0.0f);
    printf("  Max column height: %u\n", max_height);
    
    /* Calculate compressed size estimate */
    size_t heightmap_size = 64 * 64 * 2;  /* uint16 per column */
    size_t bitmask_size = 64 * 64;        /* byte per column */
    size_t column_headers = non_empty_cols * 2;  /* uint16 height per non-empty */
    size_t column_data = sc->total_stored_blocks;
    size_t header_size = 8;
    
    size_t compressed = header_size + heightmap_size + bitmask_size + column_headers + column_data;
    
    printf("  Compressed size: %zu bytes\n", compressed);
    printf("  Original size: %zu bytes\n", original_bytes);
    printf("  Compression ratio: %.1f%%\n", 100.0 * compress_ratio(original_bytes, compressed));
}

/* ============================================================================
 * Zero-Run RLE + Bitmask Compression Implementation (NEW - Phase 6)
 * ============================================================================ */

#define RLE_SEGMENT_HEIGHT 16  /* Process chunk in 16-block Y segments */

/* ============================================================================
 * Hierarchical RLE + Bitmask V2 Implementation (NEW - Phase 7)
 * ============================================================================ */

#define HIERARCHICAL_SEGMENT_HEIGHT 16  /* 64 segments for 1024 Y blocks */

/* Compress chunk using zero-run RLE + bitmask with PALETTE compression for non-air blocks */
int compress_rle_bitmask(const RawChunk* raw, RLEBitmaskCompressedChunk* out) {
    if (!raw || !out) return -1;
    
    memset(out, 0, sizeof(RLEBitmaskCompressedChunk));
    
    /* Calculate number of Y-segments */
    out->num_segments = (CHUNK_SIZE_Y + RLE_SEGMENT_HEIGHT - 1) / RLE_SEGMENT_HEIGHT;
    
    /* Allocate segment arrays */
    out->presence_masks = (uint8_t**)calloc(out->num_segments, sizeof(uint8_t*));
    out->mask_sizes = (uint32_t*)calloc(out->num_segments, sizeof(uint32_t));
    if (!out->presence_masks || !out->mask_sizes) {
        free(out->presence_masks);
        free(out->mask_sizes);
        return -1;
    }
    
    /* Temporary buffer for raw non-air block data (before palette compression) */
    uint16_t* temp_block_data = (uint16_t*)malloc(CHUNK_TOTAL_BLOCKS * sizeof(uint16_t));
    if (!temp_block_data) {
        free(out->presence_masks);
        free(out->mask_sizes);
        return -1;
    }
    
    uint32_t block_data_pos = 0;
    
    /* Process each segment */
    for (uint32_t seg = 0; seg < out->num_segments; seg++) {
        uint32_t y_start = seg * RLE_SEGMENT_HEIGHT;
        uint32_t y_end = (y_start + RLE_SEGMENT_HEIGHT < CHUNK_SIZE_Y) ? 
                         (y_start + RLE_SEGMENT_HEIGHT) : CHUNK_SIZE_Y;
        uint32_t seg_height = y_end - y_start;
        
        /* Calculate mask size: 64*64*seg_height bits = 64*64*seg_height/8 bytes */
        uint32_t mask_bits = CHUNK_SIZE_X * CHUNK_SIZE_Z * seg_height;
        uint32_t mask_bytes = (mask_bits + 7) / 8;
        out->mask_sizes[seg] = mask_bytes;
        out->presence_masks[seg] = (uint8_t*)calloc(1, mask_bytes);
        if (!out->presence_masks[seg]) {
            /* Cleanup and fail */
            for (uint32_t s = 0; s <= seg; s++) free(out->presence_masks[s]);
            free(out->presence_masks);
            free(out->mask_sizes);
            free(temp_block_data);
            return -1;
        }
        
        /* Build mask and collect block data */
        uint32_t bit_pos = 0;
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (uint32_t y = y_start; y < y_end; y++) {
                    uint16_t block = raw->blocks[x][z][y];
                    if (block != 0) {
                        /* Set bit in mask */
                        uint32_t byte_pos = bit_pos / 8;
                        uint8_t bit_offset = bit_pos % 8;
                        out->presence_masks[seg][byte_pos] |= (1 << bit_offset);
                        /* Store block data for palette analysis */
                        temp_block_data[block_data_pos++] = block;
                    }
                    bit_pos++;
                }
            }
        }
    }
    
    out->total_non_air_blocks = block_data_pos;
    
    /* Build palette from non-air blocks */
    if (block_data_pos > 0) {
        int block_counts[256] = {0};
        for (uint32_t i = 0; i < block_data_pos; i++) {
            uint8_t block_id = (uint8_t)(temp_block_data[i] & 0xFF);
            block_counts[block_id]++;
        }
        
        /* Build palette */
        out->palette_size = 0;
        for (int b = 0; b < 256 && out->palette_size < 64; b++) {
            if (block_counts[b] > 0) {
                out->palette[out->palette_size++] = (uint8_t)b;
            }
        }
        
        /* Determine bits per index */
        if (out->palette_size <= 2) out->bits_per_index = 1;
        else if (out->palette_size <= 4) out->bits_per_index = 2;
        else if (out->palette_size <= 8) out->bits_per_index = 3;
        else if (out->palette_size <= 16) out->bits_per_index = 4;
        else if (out->palette_size <= 32) out->bits_per_index = 5;
        else if (out->palette_size <= 64) out->bits_per_index = 6;
        else out->bits_per_index = 8;
        
        /* Build reverse lookup */
        uint8_t block_to_palette[256];
        for (uint8_t i = 0; i < out->palette_size; i++) {
            block_to_palette[out->palette[i]] = i;
        }
        
        /* Compress block data */
        if (out->bits_per_index < 8) {
            uint32_t num_indices = block_data_pos;
            out->compressed_block_data_size = (num_indices * out->bits_per_index + 7) / 8;
            out->compressed_block_data = (uint8_t*)calloc(1, out->compressed_block_data_size);
            if (out->compressed_block_data) {
                uint64_t bit_buffer = 0;
                int bits_in_buffer = 0;
                size_t out_pos = 0;
                uint8_t bits = out->bits_per_index;
                uint8_t mask = (1 << bits) - 1;
                
                for (uint32_t i = 0; i < num_indices; i++) {
                    uint8_t palette_idx = block_to_palette[(uint8_t)(temp_block_data[i] & 0xFF)];
                    bit_buffer = (bit_buffer << bits) | (palette_idx & mask);
                    bits_in_buffer += bits;
                    
                    while (bits_in_buffer >= 8) {
                        bits_in_buffer -= 8;
                        out->compressed_block_data[out_pos++] = (bit_buffer >> bits_in_buffer) & 0xFF;
                    }
                }
                
                if (bits_in_buffer > 0) {
                    out->compressed_block_data[out_pos++] = (bit_buffer << (8 - bits_in_buffer)) & 0xFF;
                }
            }
        } else {
            out->compressed_block_data_size = block_data_pos;
            out->compressed_block_data = (uint8_t*)malloc(block_data_pos);
            if (out->compressed_block_data) {
                memcpy(out->compressed_block_data, temp_block_data, block_data_pos * sizeof(uint16_t));
            }
        }
    }
    
    free(temp_block_data);
    return 0;
}

/* Decompress hierarchical RLE V2 chunk */
int decompress_hierarchical_rle(const HierarchicalRLECompressedChunk* hc, RawChunk* out) {
    if (!hc || !out) return -1;
    
    memset(out, 0, sizeof(RawChunk));
    
    /* Decompress palette-compressed block data to temp buffer */
    uint16_t* decompressed_blocks = NULL;
    uint32_t decompressed_count = 0;
    
    if (hc->total_non_air_blocks > 0) {
        decompressed_blocks = (uint16_t*)malloc(hc->total_non_air_blocks * sizeof(uint16_t));
        if (!decompressed_blocks) return -1;
        
        if (hc->palette_size <= 1) {
            for (uint32_t i = 0; i < hc->total_non_air_blocks; i++) {
                decompressed_blocks[i] = hc->palette[0];
            }
            decompressed_count = hc->total_non_air_blocks;
        } else if (hc->bits_per_index < 8 && hc->compressed_block_data) {
            uint64_t bit_buffer = 0;
            int bits_in_buffer = 0;
            size_t in_pos = 0;
            uint8_t bits = hc->bits_per_index;
            uint8_t mask = (1 << bits) - 1;
            
            for (uint32_t i = 0; i < hc->total_non_air_blocks; i++) {
                while (bits_in_buffer < bits && in_pos < hc->compressed_block_data_size) {
                    bit_buffer = (bit_buffer << 8) | hc->compressed_block_data[in_pos++];
                    bits_in_buffer += 8;
                }
                
                bits_in_buffer -= bits;
                uint8_t palette_idx = (bit_buffer >> bits_in_buffer) & mask;
                
                if (palette_idx < hc->palette_size) {
                    decompressed_blocks[decompressed_count++] = hc->palette[palette_idx];
                } else {
                    decompressed_blocks[decompressed_count++] = 0;
                }
            }
        } else if (hc->compressed_block_data) {
            memcpy(decompressed_blocks, hc->compressed_block_data, hc->total_non_air_blocks * sizeof(uint16_t));
            decompressed_count = hc->total_non_air_blocks;
        }
    }
    
    /* Reconstruct chunk segment by segment */
    uint32_t data_pos = 0;
    
    for (uint32_t seg = 0; seg < hc->num_segments; seg++) {
        uint32_t y_start = seg * HIERARCHICAL_SEGMENT_HEIGHT;
        uint32_t y_end = (y_start + HIERARCHICAL_SEGMENT_HEIGHT < CHUNK_SIZE_Y) ?
                         (y_start + HIERARCHICAL_SEGMENT_HEIGHT) : CHUNK_SIZE_Y;
        
        /* Check if segment is in summary */
        if ((hc->segment_summary & (1ULL << seg)) == 0) {
            /* All air - already zero from memset, skip */
            continue;
        }
        
        const HierarchicalSegment* segp = &hc->segments[seg];
        
        if (segp->type == SEG_TYPE_UNIFORM) {
            /* Fill entire segment with uniform block */
            for (int x = 0; x < CHUNK_SIZE_X; x++) {
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (uint32_t y = y_start; y < y_end; y++) {
                        out->blocks[x][z][y] = segp->uniform_block;
                    }
                }
            }
        } else if (segp->type == SEG_TYPE_MIXED && segp->mask) {
            /* Decode mask and place blocks */
            uint32_t bit_pos = 0;
            for (int x = 0; x < CHUNK_SIZE_X; x++) {
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (uint32_t y = y_start; y < y_end; y++) {
                        uint32_t byte_pos = bit_pos / 8;
                        uint8_t bit_offset = bit_pos % 8;
                        
                        if (segp->mask[byte_pos] & (1 << bit_offset)) {
                            if (data_pos < decompressed_count) {
                                out->blocks[x][z][y] = decompressed_blocks[data_pos++];
                            }
                        }
                        bit_pos++;
                    }
                }
            }
        }
    }
    
    free(decompressed_blocks);
    return 0;
}

/* Encode hierarchical RLE chunk to binary */
int hierarchical_rle_encode_binary(const HierarchicalRLECompressedChunk* hc, uint8_t** out_data, size_t* out_size) {
    if (!hc || !out_data || !out_size) return -1;
    
    /* Calculate size: header + segment_summary + segment data + palette + block data */
    size_t size = 32; /* Header */
    size += 8; /* segment_summary */
    
    /* Count non-empty segments */
    uint32_t non_empty = 0;
    for (uint32_t seg = 0; seg < hc->num_segments; seg++) {
        if (hc->segment_summary & (1ULL << seg)) {
            non_empty++;
            size += 2; /* type + uniform_block */
            if (hc->segments[seg].type == SEG_TYPE_MIXED) {
                size += 4 + hc->segments[seg].mask_size; /* mask_size + mask */
            }
        }
    }
    
    size += hc->palette_size;
    size += hc->compressed_block_data_size;
    
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) return -1;
    
    size_t pos = 0;
    
    /* Header */
    (*out_data)[pos++] = COMPRESS_METHOD_HIERARCHICAL_RLE;
    *(uint32_t*)(&(*out_data)[pos]) = hc->num_segments; pos += 4;
    *(uint64_t*)(&(*out_data)[pos]) = hc->segment_summary; pos += 8;
    (*out_data)[pos++] = hc->palette_size;
    (*out_data)[pos++] = hc->bits_per_index;
    *(uint32_t*)(&(*out_data)[pos]) = hc->total_non_air_blocks; pos += 4;
    *(uint32_t*)(&(*out_data)[pos]) = hc->compressed_block_data_size; pos += 4;
    pos += 8; /* Reserved */
    
    /* Segment data */
    for (uint32_t seg = 0; seg < hc->num_segments; seg++) {
        if (hc->segment_summary & (1ULL << seg)) {
            (*out_data)[pos++] = hc->segments[seg].type;
            if (hc->segments[seg].type == SEG_TYPE_UNIFORM) {
                (*out_data)[pos++] = (uint8_t)hc->segments[seg].uniform_block;
            } else if (hc->segments[seg].type == SEG_TYPE_MIXED) {
                *(uint32_t*)(&(*out_data)[pos]) = hc->segments[seg].mask_size; pos += 4;
                if (hc->segments[seg].mask && hc->segments[seg].mask_size > 0) {
                    memcpy(&(*out_data)[pos], hc->segments[seg].mask, hc->segments[seg].mask_size);
                    pos += hc->segments[seg].mask_size;
                }
            }
        }
    }
    
    /* Palette */
    if (hc->palette_size > 0) {
        memcpy(&(*out_data)[pos], hc->palette, hc->palette_size);
        pos += hc->palette_size;
    }
    
    /* Compressed block data */
    if (hc->compressed_block_data_size > 0 && hc->compressed_block_data) {
        memcpy(&(*out_data)[pos], hc->compressed_block_data, hc->compressed_block_data_size);
        pos += hc->compressed_block_data_size;
    }
    
    *out_size = pos;
    return 0;
}

/* Free hierarchical RLE chunk memory */
void hierarchical_rle_free(HierarchicalRLECompressedChunk* hc) {
    if (!hc) return;
    
    if (hc->segments) {
        for (uint32_t seg = 0; seg < hc->num_segments; seg++) {
            free(hc->segments[seg].mask);
            hc->segments[seg].mask = NULL;
        }
        free(hc->segments);
        hc->segments = NULL;
    }
    
    free(hc->compressed_block_data);
    hc->compressed_block_data = NULL;
    hc->compressed_block_data_size = 0;
    
    hc->num_segments = 0;
    hc->segment_summary = 0;
    hc->total_non_air_blocks = 0;
    hc->palette_size = 0;
}

/* Print hierarchical RLE compression statistics */
void hierarchical_rle_print_stats(const HierarchicalRLECompressedChunk* hc, size_t original_bytes) {
    if (!hc) return;
    
    printf("Hierarchical RLE V2 Compression Statistics:\n");
    printf("  Segments: %u\n", hc->num_segments);
    
    /* Count segment types */
    uint32_t air_count = 0, uniform_count = 0, mixed_count = 0;
    size_t mask_total = 0;
    for (uint32_t seg = 0; seg < hc->num_segments; seg++) {
        if ((hc->segment_summary & (1ULL << seg)) == 0) {
            air_count++;
        } else if (hc->segments[seg].type == SEG_TYPE_UNIFORM) {
            uniform_count++;
        } else {
            mixed_count++;
            mask_total += hc->segments[seg].mask_size;
        }
    }
    
    printf("  All-air segments: %u (skipped)\n", air_count);
    printf("  Uniform segments: %u (2 bytes each)\n", uniform_count);
    printf("  Mixed segments: %u (full mask)\n", mixed_count);
    printf("  Non-air blocks: %u\n", hc->total_non_air_blocks);
    printf("  Palette: %u entries (%u bits/index)\n", hc->palette_size, hc->bits_per_index);
    
    /* Calculate compressed size */
    size_t header = 32;
    size_t segment_data = 8; /* summary mask */
    segment_data += (uniform_count + mixed_count) * 2; /* type + data */
    for (uint32_t seg = 0; seg < hc->num_segments; seg++) {
        if (hc->segments[seg].type == SEG_TYPE_MIXED) {
            segment_data += 4 + hc->segments[seg].mask_size;
        }
    }
    size_t palette_data = hc->palette_size;
    size_t compressed = header + segment_data + palette_data + hc->compressed_block_data_size;
    
    printf("  Header: %zu bytes\n", header);
    printf("  Segment data: %zu bytes\n", segment_data);
    printf("  Palette: %zu bytes\n", palette_data);
    printf("  Block data (compressed): %u bytes\n", hc->compressed_block_data_size);
    printf("  Total compressed: %zu bytes\n", compressed);
    printf("  Original size: %zu bytes\n", original_bytes);
    printf("  Compression ratio: %.1f%%\n", 100.0 * compress_ratio(original_bytes, compressed));
}

/* ============================================================================
 * Superchunk Hierarchical RLE V3 Implementation (Phase 8: 96%+ Compression Target)
 * ============================================================================ */

#define BLOCK_STONE_BRICKS 114
#define WALL_THRESHOLD 0.95f  /* 95% stone bricks = wall chunk */

/* Check if chunk is a wall chunk (mostly stone bricks) */
int chunk_is_wall(const RawChunk* raw, float threshold) {
    if (!raw) return 0;
    
    uint32_t stone_brick_count = 0;
    uint32_t total_blocks = 0;
    
    for (int x = 0; x < CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                uint16_t block = raw->blocks[x][z][y];
                if (block != 0) {
                    total_blocks++;
                    if (block == BLOCK_STONE_BRICKS) {
                        stone_brick_count++;
                    }
                }
            }
        }
    }
    
    if (total_blocks == 0) return 0;
    float ratio = (float)stone_brick_count / (float)total_blocks;
    return (ratio >= threshold) ? 1 : 0;
}

/* Detect full superchunks from loaded chunks */
static int32_t floor_div_chunks(int32_t value, int32_t divisor) {
    if (divisor <= 0) return 0;
    if (value >= 0) return value / divisor;
    return -(((-value) + divisor - 1) / divisor);
}

static RawChunk* find_chunk_by_coords(RawChunk** chunks, int num_chunks, int32_t cx, int32_t cz) {
    for (int i = 0; i < num_chunks; i++) {
        if (chunks[i] && chunks[i]->cx == cx && chunks[i]->cz == cz) {
            return chunks[i];
        }
    }
    return NULL;
}

int detect_superchunks(RawChunk** chunks, int num_chunks, 
                        DetectedSuperchunk** out_superchunks, int* out_count) {
    if (!chunks || num_chunks <= 0 || !out_superchunks || !out_count) return -1;
    
    /* Find chunk coordinate bounds */
    int32_t min_cx = INT32_MAX, max_cx = INT32_MIN;
    int32_t min_cz = INT32_MAX, max_cz = INT32_MIN;
    
    for (int i = 0; i < num_chunks; i++) {
        if (chunks[i]) {
            if (chunks[i]->cx < min_cx) min_cx = chunks[i]->cx;
            if (chunks[i]->cx > max_cx) max_cx = chunks[i]->cx;
            if (chunks[i]->cz < min_cz) min_cz = chunks[i]->cz;
            if (chunks[i]->cz > max_cz) max_cz = chunks[i]->cz;
        }
    }
    
    /* Calculate candidate superchunk bounds */
    int32_t min_scx = floor_div_chunks(min_cx, SUPERCHUNK_SIZE_CHUNKS);
    int32_t max_scx = floor_div_chunks(max_cx, SUPERCHUNK_SIZE_CHUNKS);
    int32_t min_scz = floor_div_chunks(min_cz, SUPERCHUNK_SIZE_CHUNKS);
    int32_t max_scz = floor_div_chunks(max_cz, SUPERCHUNK_SIZE_CHUNKS);
    
    /* Allocate superchunk array */
    int max_superchunks = (max_scx - min_scx + 1) * (max_scz - min_scz + 1);
    DetectedSuperchunk* superchunks = (DetectedSuperchunk*)calloc(max_superchunks, sizeof(DetectedSuperchunk));
    if (!superchunks) return -1;
    
    int sc_count = 0;
    
    /* For each potential superchunk */
    for (int32_t scz = min_scz; scz <= max_scz; scz++) {
        for (int32_t scx = min_scx; scx <= max_scx; scx++) {
            DetectedSuperchunk* sc = &superchunks[sc_count];
            sc->scx = scx;
            sc->scz = scz;
            sc->num_terrain = 0;
            sc->num_wall = 0;
            
            for (int slot = 0; slot < SUPERCHUNK_CHUNKS; slot++) {
                int local_x = slot % SUPERCHUNK_SIZE_CHUNKS;
                int local_z = slot / SUPERCHUNK_SIZE_CHUNKS;
                int32_t chunk_cx = scx * SUPERCHUNK_SIZE_CHUNKS + local_x;
                int32_t chunk_cz = scz * SUPERCHUNK_SIZE_CHUNKS + local_z;
                RawChunk* found = find_chunk_by_coords(chunks, num_chunks, chunk_cx, chunk_cz);

                if (found) {
                    sc->chunks[slot] = found;
                    sc->num_terrain++;
                }
            }

            for (int rel_z = -SUPERCHUNK_BOUNDARY_RADIUS;
                 rel_z < SUPERCHUNK_SIZE_CHUNKS + SUPERCHUNK_BOUNDARY_RADIUS;
                 rel_z++) {
                for (int rel_x = -SUPERCHUNK_BOUNDARY_RADIUS;
                     rel_x < SUPERCHUNK_SIZE_CHUNKS + SUPERCHUNK_BOUNDARY_RADIUS;
                     rel_x++) {
                    int32_t chunk_cx;
                    int32_t chunk_cz;
                    RawChunk* found;

                    if (rel_x >= 0 && rel_x < SUPERCHUNK_SIZE_CHUNKS &&
                        rel_z >= 0 && rel_z < SUPERCHUNK_SIZE_CHUNKS) {
                        continue;
                    }

                    if (sc->num_wall >= SUPERCHUNK_BOUNDARY_CHUNKS) {
                        continue;
                    }

                    chunk_cx = scx * SUPERCHUNK_SIZE_CHUNKS + rel_x;
                    chunk_cz = scz * SUPERCHUNK_SIZE_CHUNKS + rel_z;
                    found = find_chunk_by_coords(chunks, num_chunks, chunk_cx, chunk_cz);

                    if (found) {
                        sc->wall_chunks[sc->num_wall] = found;
                        sc->wall_rel_x[sc->num_wall] = (int8_t)rel_x;
                        sc->wall_rel_z[sc->num_wall] = (int8_t)rel_z;
                        sc->num_wall++;
                    }
                }
            }

            /* Keep only meaningful candidates to avoid duplicate edge-only neighborhoods. */
            if (sc->num_terrain >= (SUPERCHUNK_SIZE_CHUNKS * 4)) {
                sc_count++;
            }
        }
    }
    
    *out_superchunks = superchunks;
    *out_count = sc_count;
    return 0;
}

/* Analyze superchunk-level segment (across all chunks) */
static int analyze_superchunk_segment(const DetectedSuperchunk* sc, uint32_t seg,
                                       uint32_t* out_non_air, uint8_t* out_uniform_block,
                                       int* out_is_uniform) {
    uint32_t y_start = seg * 16;
    uint32_t y_end = (y_start + 16 < CHUNK_SIZE_Y) ? (y_start + 16) : CHUNK_SIZE_Y;
    
    uint32_t non_air = 0;
    uint16_t first_block = 0xFF;
    int all_same = 1;
    int found_first = 0;
    
    /* Scan across all terrain chunks in superchunk */
    for (int slot = 0; slot < 256; slot++) {
        RawChunk* chunk = sc->chunks[slot];
        if (!chunk || sc->is_wall[slot]) continue;
        
        for (int cx = 0; cx < CHUNK_SIZE_X; cx++) {
            for (int cz = 0; cz < CHUNK_SIZE_Z; cz++) {
                for (uint32_t y = y_start; y < y_end; y++) {
                    uint16_t block = chunk->blocks[cx][cz][y];
                    if (block != 0) {
                        non_air++;
                        if (!found_first) {
                            first_block = block;
                            found_first = 1;
                        } else if (block != first_block) {
                            all_same = 0;
                        }
                    }
                }
            }
        }
    }
    
    *out_non_air = non_air;
    *out_uniform_block = (uint8_t)(found_first ? first_block : 0);
    *out_is_uniform = all_same && found_first;
    
    if (non_air == 0) return SEG_TYPE_ALL_AIR;
    if (all_same && found_first) return SEG_TYPE_UNIFORM;
    return SEG_TYPE_MIXED;
}

/* Compress superchunk using hierarchical V3 method */
int compress_superchunk_hierarchical(const DetectedSuperchunk* sc, 
                                      SuperchunkHierarchicalCompressedChunk* out) {
    if (!sc || !out) return -1;
    
    memset(out, 0, sizeof(SuperchunkHierarchicalCompressedChunk));
    out->scx = sc->scx;
    out->scz = sc->scz;
    out->wall_ring_present = (sc->num_wall > 0) ? 1 : 0;
    out->num_wall_chunks =
        (uint8_t)((sc->num_wall > SUPERCHUNK_BOUNDARY_CHUNKS) ? SUPERCHUNK_BOUNDARY_CHUNKS : sc->num_wall);
    
    /* Count terrain chunks */
    out->num_chunks = sc->num_terrain;
    if (out->num_chunks == 0) return -1;
    
    /* Allocate per-chunk segment array */
    out->chunk_segments = (SuperchunkChunkSegments*)calloc(out->num_chunks, sizeof(SuperchunkChunkSegments));
    if (!out->chunk_segments) return -1;
    
    /* Temporary buffer for ALL non-air block data across superchunk - now 16-bit */
    uint16_t* temp_block_data = (uint16_t*)malloc((size_t)out->num_chunks * CHUNK_TOTAL_BLOCKS * sizeof(uint16_t));
    if (!temp_block_data) {
        free(out->chunk_segments);
        out->chunk_segments = NULL;
        return -1;
    }
    
    uint32_t block_data_pos = 0;
    uint16_t chunk_idx = 0;
    
    /* Process each terrain chunk */
    for (int slot = 0; slot < 256 && chunk_idx < out->num_chunks; slot++) {
        RawChunk* chunk = sc->chunks[slot];
        if (!chunk || sc->is_wall[slot]) continue;
        
        int local_x = slot % 16;
        int local_z = slot / 16;
        
        SuperchunkChunkSegments* cseg = &out->chunk_segments[chunk_idx];
        cseg->chunk_x = (uint16_t)local_x;
        cseg->chunk_z = (uint16_t)local_z;
        cseg->segment_summary = 0;
        
        /* Process 64 segments per chunk */
        for (uint32_t seg = 0; seg < 64; seg++) {
            uint32_t y_start = seg * 16;
            uint32_t y_end = (y_start + 16 < CHUNK_SIZE_Y) ? (y_start + 16) : CHUNK_SIZE_Y;
            
            /* Analyze this segment */
            uint32_t non_air = 0;
            uint16_t first_block = 0;
            int is_uniform = 1;  /* Assume uniform until proven otherwise */
            int found_first = 0;
            
            for (int cx = 0; cx < CHUNK_SIZE_X; cx++) {
                for (int cz = 0; cz < CHUNK_SIZE_Z; cz++) {
                    for (uint32_t y = y_start; y < y_end; y++) {
                        uint16_t block = chunk->blocks[cx][cz][y];
                        if (!found_first) {
                            first_block = block;
                            found_first = 1;
                        } else if (block != first_block) {
                            is_uniform = 0;
                        }
                        if (block != 0) {
                            non_air++;
                        }
                    }
                }
            }
            
            HierarchicalSegment* hs = &cseg->segments[seg];
            
            if (non_air == 0) {
                hs->type = SEG_TYPE_ALL_AIR;
            } else if (is_uniform && found_first) {
                hs->type = SEG_TYPE_UNIFORM;
                hs->uniform_block = first_block;
                cseg->segment_summary |= (1ULL << seg);  /* Mark segment as having data */
            } else {
                hs->type = SEG_TYPE_MIXED;
                cseg->segment_summary |= (1ULL << seg);  /* Mark segment as having data */
                
                /* Count blocks to decide: bitmap vs sparse */
                uint32_t non_air_count = 0;
                for (int cx = 0; cx < CHUNK_SIZE_X; cx++) {
                    for (int cz = 0; cz < CHUNK_SIZE_Z; cz++) {
                        for (uint32_t y = y_start; y < y_end; y++) {
                            if (chunk->blocks[cx][cz][y] != 0) non_air_count++;
                        }
                    }
                }
                
                /* Threshold: sparse if < 4096 blocks (half the segment) */
                if (non_air_count < 4096 && non_air_count > 0) {
                    /* SPARSE: Store position list */
                    hs->num_positions = non_air_count;
                    hs->positions = (uint16_t*)malloc(non_air_count * sizeof(uint16_t));
                    hs->mask_size = non_air_count * sizeof(uint16_t); /* For size tracking */
                    
                    if (hs->positions) {
                        uint32_t bit_pos = 0;
                        uint32_t pos_idx = 0;
                        for (int cx = 0; cx < CHUNK_SIZE_X; cx++) {
                            for (int cz = 0; cz < CHUNK_SIZE_Z; cz++) {
                                for (uint32_t y = y_start; y < y_end; y++) {
                                    uint16_t block = chunk->blocks[cx][cz][y];
                                    if (block != 0) {
                                        hs->positions[pos_idx++] = (uint16_t)bit_pos;
                                        ((uint16_t*)temp_block_data)[block_data_pos++] = block;
                                    }
                                    bit_pos++;
                                }
                            }
                        }
                    }
                } else {
                    /* BITMAP: Full mask for dense segments */
                    uint32_t mask_bits = CHUNK_SIZE_X * CHUNK_SIZE_Z * (y_end - y_start);
                    hs->mask_size = (mask_bits + 7) / 8;
                    hs->num_positions = 0; /* Indicates bitmap mode */
                    hs->mask = (uint8_t*)calloc(1, hs->mask_size);
                    
                    if (hs->mask) {
                        uint32_t bit_pos = 0;
                        for (int cx = 0; cx < CHUNK_SIZE_X; cx++) {
                            for (int cz = 0; cz < CHUNK_SIZE_Z; cz++) {
                                for (uint32_t y = y_start; y < y_end; y++) {
                                    uint16_t block = chunk->blocks[cx][cz][y];
                                    if (block != 0) {
                                        uint32_t byte_pos = bit_pos / 8;
                                        uint8_t bit_offset = bit_pos % 8;
                                        hs->mask[byte_pos] |= (1 << bit_offset);
                                        ((uint16_t*)temp_block_data)[block_data_pos++] = block;
                                    }
                                    bit_pos++;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        chunk_idx++;
    }
    
    out->total_non_air_blocks = block_data_pos;
    
    /* Build GLOBAL palette from ALL non-air blocks in superchunk */
    if (block_data_pos > 0) {
        /* Use hash map for 16-bit block counting (65536 possible values) */
        typedef struct { uint16_t block; uint32_t count; } BlockCount;
        BlockCount counts[256];
        int unique_count = 0;
        
        for (uint32_t i = 0; i < block_data_pos; i++) {
            uint16_t block = ((uint16_t*)temp_block_data)[i];
            int found = 0;
            for (int j = 0; j < unique_count; j++) {
                if (counts[j].block == block) {
                    counts[j].count++;
                    found = 1;
                    break;
                }
            }
            if (!found && unique_count < 256) {
                counts[unique_count].block = block;
                counts[unique_count].count = 1;
                unique_count++;
            }
        }
        
        /* Build palette (up to 256 entries for 16-bit blocks) */
        out->palette_size = (uint16_t)(unique_count > 256 ? 256 : unique_count);
        for (int i = 0; i < out->palette_size; i++) {
            out->palette[i] = counts[i].block;
        }
        
        /* Determine bits per index (up to 8 bits for 256 entries) */
        if (out->palette_size <= 2) out->bits_per_index = 1;
        else if (out->palette_size <= 4) out->bits_per_index = 2;
        else if (out->palette_size <= 8) out->bits_per_index = 3;
        else if (out->palette_size <= 16) out->bits_per_index = 4;
        else if (out->palette_size <= 32) out->bits_per_index = 5;
        else if (out->palette_size <= 64) out->bits_per_index = 6;
        else if (out->palette_size <= 128) out->bits_per_index = 7;
        else out->bits_per_index = 8;
        
        /* Build reverse lookup hash map */
        typedef struct { uint16_t block; uint8_t idx; } BlockToPal;
        BlockToPal block_to_pal[256];
        for (uint16_t i = 0; i < out->palette_size; i++) {
            block_to_pal[i].block = out->palette[i];
            block_to_pal[i].idx = (uint8_t)i;
        }
        
        /* Compress ALL block data with single global palette */
        if (out->bits_per_index < 16) {
            uint32_t num_indices = block_data_pos;
            out->compressed_block_data_size = (num_indices * out->bits_per_index + 7) / 8;
            out->compressed_block_data = (uint8_t*)calloc(1, out->compressed_block_data_size);
            if (out->compressed_block_data) {
                uint64_t bit_buffer = 0;
                int bits_in_buffer = 0;
                size_t out_pos = 0;
                uint8_t bits = out->bits_per_index;
                uint8_t mask = (1 << bits) - 1;
                
                for (uint32_t i = 0; i < num_indices; i++) {
                    uint16_t block = ((uint16_t*)temp_block_data)[i];
                    /* Find palette index */
                    uint8_t palette_idx = 0;
                    for (int j = 0; j < out->palette_size; j++) {
                        if (block_to_pal[j].block == block) {
                            palette_idx = block_to_pal[j].idx;
                            break;
                        }
                    }
                    bit_buffer = (bit_buffer << bits) | (palette_idx & mask);
                    bits_in_buffer += bits;
                    
                    while (bits_in_buffer >= 8) {
                        bits_in_buffer -= 8;
                        out->compressed_block_data[out_pos++] = (bit_buffer >> bits_in_buffer) & 0xFF;
                    }
                }
                
                if (bits_in_buffer > 0) {
                    out->compressed_block_data[out_pos++] = (bit_buffer << (8 - bits_in_buffer)) & 0xFF;
                }
            }
        } else {
            /* Store as raw 16-bit data */
            out->compressed_block_data_size = block_data_pos * 2;
            out->compressed_block_data = (uint8_t*)malloc(out->compressed_block_data_size);
            if (out->compressed_block_data) {
                memcpy(out->compressed_block_data, temp_block_data, out->compressed_block_data_size);
            }
        }
    }
    
    if (out->num_wall_chunks > 0) {
        out->wall_chunks = calloc(out->num_wall_chunks, sizeof(*out->wall_chunks));
        if (!out->wall_chunks) {
            free(temp_block_data);
            superchunk_hierarchical_free(out);
            return -1;
        }

        for (uint8_t i = 0; i < out->num_wall_chunks; i++) {
            CompressedChunk wall_comp;
            size_t wall_size = 0;
            memset(&wall_comp, 0, sizeof(wall_comp));

            out->wall_chunks[i].chunk_x = sc->wall_rel_x[i];
            out->wall_chunks[i].chunk_z = sc->wall_rel_z[i];

            if (!sc->wall_chunks[i] ||
                compress_chunk(sc->wall_chunks[i], &wall_comp) != 0 ||
                compress_encode_binary(&wall_comp,
                                       &out->wall_chunks[i].compressed_data,
                                       &wall_size) != 0) {
                compress_chunk_free(&wall_comp);
                free(temp_block_data);
                superchunk_hierarchical_free(out);
                return -1;
            }

            out->wall_chunks[i].compressed_size = (uint32_t)wall_size;
            compress_chunk_free(&wall_comp);
        }
    }

    free(temp_block_data);
    return 0;
}

/* Decompress superchunk hierarchical V3 */
int decompress_superchunk_hierarchical(const SuperchunkHierarchicalCompressedChunk* sc,
                                        RawChunk** out_chunks, int* out_count) {
    if (!sc || !out_chunks || !out_count) return -1;
    
    /* Allocate output chunks */
    *out_count = sc->num_chunks + sc->num_wall_chunks;
    for (int i = 0; i < *out_count; i++) {
        out_chunks[i] = (RawChunk*)calloc(1, sizeof(RawChunk));
        if (!out_chunks[i]) {
            for (int j = 0; j < i; j++) free(out_chunks[j]);
            *out_count = 0;
            return -1;
        }
    }
    
    /* Decompress palette data to temp buffer */
    uint16_t* decompressed_blocks = NULL;
    uint32_t decompressed_count = 0;
    
    if (sc->total_non_air_blocks > 0) {
        decompressed_blocks = (uint16_t*)malloc(sc->total_non_air_blocks * sizeof(uint16_t));
        if (!decompressed_blocks) {
            for (int i = 0; i < *out_count; i++) free(out_chunks[i]);
            *out_count = 0;
            return -1;
        }
        
        if (sc->palette_size <= 1) {
            for (uint32_t i = 0; i < sc->total_non_air_blocks; i++) {
                decompressed_blocks[i] = sc->palette[0];
            }
            decompressed_count = sc->total_non_air_blocks;
        } else if (sc->bits_per_index < 16 && sc->compressed_block_data) {
            uint64_t bit_buffer = 0;
            int bits_in_buffer = 0;
            size_t in_pos = 0;
            uint8_t bits = sc->bits_per_index;
            uint8_t mask = (1 << bits) - 1;
            
            for (uint32_t i = 0; i < sc->total_non_air_blocks; i++) {
                while (bits_in_buffer < bits && in_pos < sc->compressed_block_data_size) {
                    bit_buffer = (bit_buffer << 8) | sc->compressed_block_data[in_pos++];
                    bits_in_buffer += 8;
                }
                
                bits_in_buffer -= bits;
                uint8_t palette_idx = (bit_buffer >> bits_in_buffer) & mask;
                
                if (palette_idx < sc->palette_size) {
                    decompressed_blocks[decompressed_count++] = sc->palette[palette_idx];
                } else {
                    decompressed_blocks[decompressed_count++] = 0;
                }
            }
        } else if (sc->compressed_block_data) {
            /* Raw 16-bit data */
            memcpy(decompressed_blocks, sc->compressed_block_data, sc->total_non_air_blocks * sizeof(uint16_t));
            decompressed_count = sc->total_non_air_blocks;
        }
    }
    
    /* Reconstruct each chunk */
    uint32_t global_data_pos = 0;
    
    for (int ci = 0; ci < sc->num_chunks; ci++) {
        const SuperchunkChunkSegments* cseg = &sc->chunk_segments[ci];
        RawChunk* chunk = out_chunks[ci];
        
        chunk->cx = sc->scx * 16 + cseg->chunk_x;
        chunk->cz = sc->scz * 16 + cseg->chunk_z;
        memset(chunk->blocks, 0, sizeof(chunk->blocks));

        /* Process each segment */
        for (uint32_t seg = 0; seg < 64; seg++) {
            uint32_t y_start = seg * 16;
            uint32_t y_end = (y_start + 16 < CHUNK_SIZE_Y) ? (y_start + 16) : CHUNK_SIZE_Y;
            
            int has_summary = (cseg->segment_summary & (1ULL << seg)) != 0;
            if (!has_summary) continue;
            
            const HierarchicalSegment* hs = &cseg->segments[seg];
            
            if (hs->type == SEG_TYPE_UNIFORM) {
                /* Only fill if non-air */
                if (hs->uniform_block != 0) {
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                            for (uint32_t y = y_start; y < y_end; y++) {
                                chunk->blocks[x][z][y] = hs->uniform_block;
                            }
                        }
                    }
                }
            } else if (hs->type == SEG_TYPE_MIXED) {
                if (hs->num_positions > 0) {
                    /* SPARSE: Use position list */
                    for (uint32_t i = 0; i < hs->num_positions; i++) {
                        if (global_data_pos < decompressed_count) {
                            uint16_t pos = hs->positions[i];
                            uint32_t bit_pos = pos;
                            
                            /* Convert bit_pos to x,z,y */
                            uint32_t y = (bit_pos % 16) + y_start;
                            uint32_t xz = bit_pos / 16;
                            uint32_t z = xz % CHUNK_SIZE_Z;
                            uint32_t x = xz / CHUNK_SIZE_Z;
                            
                            if (x < CHUNK_SIZE_X && z < CHUNK_SIZE_Z && y < y_end) {
                                chunk->blocks[x][z][y] = decompressed_blocks[global_data_pos++];
                            }
                        }
                    }
                } else if (hs->mask) {
                    /* BITMAP: Use traditional mask */
                    uint32_t bit_pos = 0;
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                            for (uint32_t y = y_start; y < y_end; y++) {
                                uint32_t byte_pos = bit_pos / 8;
                                uint8_t bit_offset = bit_pos % 8;
                                
                                if (hs->mask[byte_pos] & (1 << bit_offset)) {
                                    if (global_data_pos < decompressed_count) {
                                        chunk->blocks[x][z][y] = decompressed_blocks[global_data_pos++];
                                    }
                                }
                                bit_pos++;
                            }
                        }
                    }
                }
            }
        }
    }

    for (int wi = 0; wi < sc->num_wall_chunks; wi++) {
        CompressedChunk wall_comp;
        RawChunk* wall_chunk = out_chunks[sc->num_chunks + wi];

        memset(&wall_comp, 0, sizeof(wall_comp));
        if (!sc->wall_chunks[wi].compressed_data ||
            compress_decode_binary(sc->wall_chunks[wi].compressed_data,
                                   sc->wall_chunks[wi].compressed_size,
                                   &wall_comp) != 0 ||
            decompress_chunk(&wall_comp, wall_chunk) != 0) {
            compress_chunk_free(&wall_comp);
            free(decompressed_blocks);
            for (int i = 0; i < *out_count; i++) {
                free(out_chunks[i]);
                out_chunks[i] = NULL;
            }
            *out_count = 0;
            return -1;
        }

        wall_chunk->cx = sc->scx * SUPERCHUNK_SIZE_CHUNKS + sc->wall_chunks[wi].chunk_x;
        wall_chunk->cz = sc->scz * SUPERCHUNK_SIZE_CHUNKS + sc->wall_chunks[wi].chunk_z;
        compress_chunk_free(&wall_comp);
    }
    
    free(decompressed_blocks);
    return 0;
}

/* Calculate superchunk compressed size */
static size_t calculate_superchunk_size(const SuperchunkHierarchicalCompressedChunk* sc) {
    if (!sc) return 0;
    
    size_t size = 48; /* Header */
    size += sc->palette_size * sizeof(uint16_t);
    size += sc->compressed_block_data_size;
    
    /* Per-chunk segment data */
    for (int i = 0; i < sc->num_chunks; i++) {
        size += 10; /* chunk_x, chunk_z, segment_summary */
        const SuperchunkChunkSegments* cseg = &sc->chunk_segments[i];
        
        for (int seg = 0; seg < 64; seg++) {
            if (cseg->segment_summary & (1ULL << seg)) {
                const HierarchicalSegment* hs = &cseg->segments[seg];
                size += 1; /* type */
                if (hs->type == SEG_TYPE_UNIFORM) {
                    size += sizeof(uint16_t); /* uniform_block */
                } else if (hs->type == SEG_TYPE_MIXED) {
                    if (hs->num_positions > 0) {
                        size += 4 + hs->num_positions * sizeof(uint16_t); /* num_positions + position array */
                    } else {
                        size += 4 + hs->mask_size; /* mask_size + mask */
                    }
                }
            }
        }
    }

    size += 1; /* num_wall_chunks */
    for (int i = 0; i < sc->num_wall_chunks; i++) {
        size += 2 + 4 + sc->wall_chunks[i].compressed_size;
    }
    
    return size;
}

/* Print superchunk hierarchical statistics */
void superchunk_hierarchical_print_stats(const SuperchunkHierarchicalCompressedChunk* sc, 
                                          size_t original_bytes) {
    if (!sc) return;
    
    printf("Superchunk Hierarchical V3 Compression Statistics:\n");
    printf("  Superchunk: (%d, %d)\n", sc->scx, sc->scz);
    printf("  Terrain chunks: %u\n", sc->num_chunks);
    printf("  Boundary chunks: %u\n", sc->num_wall_chunks);
    printf("  Wall ring present: %s\n", sc->wall_ring_present ? "yes" : "no");
    printf("  Global palette: %u entries (%u bits/index)\n", sc->palette_size, sc->bits_per_index);
    printf("  Total non-air blocks: %u\n", sc->total_non_air_blocks);
    
    /* Count segment types across all chunks */
    uint64_t air_segments = 0, uniform_segments = 0, mixed_segments = 0;
    uint64_t sparse_segments = 0, bitmap_segments = 0;
    size_t total_mask_size = 0;
    
    for (int ci = 0; ci < sc->num_chunks; ci++) {
        const SuperchunkChunkSegments* cseg = &sc->chunk_segments[ci];
        for (int seg = 0; seg < 64; seg++) {
            if ((cseg->segment_summary & (1ULL << seg)) == 0) {
                air_segments++;
            } else if (cseg->segments[seg].type == SEG_TYPE_UNIFORM) {
                uniform_segments++;
            } else {
                mixed_segments++;
                if (cseg->segments[seg].num_positions > 0) {
                    sparse_segments++;
                    total_mask_size += cseg->segments[seg].num_positions * sizeof(uint16_t);
                } else {
                    bitmap_segments++;
                    total_mask_size += cseg->segments[seg].mask_size;
                }
            }
        }
    }
    
    printf("  All-air segments: %llu (skipped)\n", air_segments);
    printf("  Uniform segments: %llu (2 bytes each)\n", uniform_segments);
    printf("  Mixed segments: %llu (%llu sparse, %llu bitmap)\n", 
           mixed_segments, sparse_segments, bitmap_segments);
    
    size_t compressed = calculate_superchunk_size(sc);
    size_t wall_bytes = 0;
    for (int i = 0; i < sc->num_wall_chunks; i++) {
        wall_bytes += sc->wall_chunks[i].compressed_size;
    }
    printf("  Header: 48 bytes\n");
    printf("  Palette: %zu bytes\n", (size_t)sc->palette_size * sizeof(uint16_t));
    printf("  Block data (compressed): %u bytes\n", sc->compressed_block_data_size);
    printf("  Boundary chunk payloads: %zu bytes\n", wall_bytes);
    printf("  Segment data: %zu bytes\n",
           compressed - 48 - ((size_t)sc->palette_size * sizeof(uint16_t)) -
           sc->compressed_block_data_size - wall_bytes);
    printf("  Total compressed: %zu bytes\n", compressed);
    printf("  Original size: %zu bytes\n", original_bytes);
    printf("  Compression ratio: %.1f%%\n", 100.0 * compress_ratio(original_bytes, compressed));
    
    /* Per-chunk average */
    if ((sc->num_chunks + sc->num_wall_chunks) > 0) {
        printf("  Average per chunk: %.0f bytes\n",
               (double)compressed / (sc->num_chunks + sc->num_wall_chunks));
    }
}

/* Encode superchunk hierarchical to binary */
int superchunk_hierarchical_encode_binary(const SuperchunkHierarchicalCompressedChunk* sc,
                                           uint8_t** out_data, size_t* out_size) {
    if (!sc || !out_data || !out_size) return -1;
    
    size_t size = calculate_superchunk_size(sc);
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) return -1;
    
    size_t pos = 0;
    
    /* Header */
    (*out_data)[pos++] = COMPRESS_METHOD_SUPERCHUNK_HIER;
    *(int32_t*)(&(*out_data)[pos]) = sc->scx; pos += 4;
    *(int32_t*)(&(*out_data)[pos]) = sc->scz; pos += 4;
    *(uint16_t*)(&(*out_data)[pos]) = sc->num_chunks; pos += 2;
    (*out_data)[pos++] = sc->wall_ring_present;
    (*out_data)[pos++] = sc->num_wall_chunks;
    *(uint16_t*)(&(*out_data)[pos]) = sc->palette_size; pos += 2;
    (*out_data)[pos++] = sc->bits_per_index;
    pos += 4; /* Reserved */
    *(uint32_t*)(&(*out_data)[pos]) = sc->total_non_air_blocks; pos += 4;
    *(uint32_t*)(&(*out_data)[pos]) = sc->compressed_block_data_size; pos += 4;
    pos += 16; /* More reserved */
    
    /* Palette */
    if (sc->palette_size > 0) {
        memcpy(&(*out_data)[pos], sc->palette, sc->palette_size * sizeof(uint16_t));
        pos += sc->palette_size * sizeof(uint16_t);
    }
    
    /* Chunk segment data */
    for (int ci = 0; ci < sc->num_chunks; ci++) {
        const SuperchunkChunkSegments* cseg = &sc->chunk_segments[ci];
        *(uint16_t*)(&(*out_data)[pos]) = cseg->chunk_x; pos += 2;
        *(uint16_t*)(&(*out_data)[pos]) = cseg->chunk_z; pos += 2;
        *(uint64_t*)(&(*out_data)[pos]) = cseg->segment_summary; pos += 8;
        
        for (int seg = 0; seg < 64; seg++) {
            if (cseg->segment_summary & (1ULL << seg)) {
                const HierarchicalSegment* hs = &cseg->segments[seg];
                (*out_data)[pos++] = hs->type;
                if (hs->type == SEG_TYPE_UNIFORM) {
                    *(uint16_t*)(&(*out_data)[pos]) = hs->uniform_block; pos += sizeof(uint16_t);
                } else if (hs->type == SEG_TYPE_MIXED) {
                    *(uint32_t*)(&(*out_data)[pos]) = hs->num_positions; pos += 4; /* 0 = bitmap, >0 = sparse count */
                    if (hs->num_positions > 0) {
                        /* Sparse: store position array */
                        memcpy(&(*out_data)[pos], hs->positions, hs->num_positions * sizeof(uint16_t));
                        pos += hs->num_positions * sizeof(uint16_t);
                    } else if (hs->mask && hs->mask_size > 0) {
                        /* Bitmap: store full mask */
                        memcpy(&(*out_data)[pos], hs->mask, hs->mask_size);
                        pos += hs->mask_size;
                    }
                }
            }
        }
    }

    /* Boundary chunks */
    for (int wi = 0; wi < sc->num_wall_chunks; wi++) {
        * (int8_t*)(&(*out_data)[pos]) = sc->wall_chunks[wi].chunk_x; pos += 1;
        * (int8_t*)(&(*out_data)[pos]) = sc->wall_chunks[wi].chunk_z; pos += 1;
        *(uint32_t*)(&(*out_data)[pos]) = sc->wall_chunks[wi].compressed_size; pos += 4;
        memcpy(&(*out_data)[pos], sc->wall_chunks[wi].compressed_data, sc->wall_chunks[wi].compressed_size);
        pos += sc->wall_chunks[wi].compressed_size;
    }
    
    /* Compressed block data */
    if (sc->compressed_block_data_size > 0 && sc->compressed_block_data) {
        memcpy(&(*out_data)[pos], sc->compressed_block_data, sc->compressed_block_data_size);
        pos += sc->compressed_block_data_size;
    }
    
    *out_size = pos;
    return 0;
}

/* Free superchunk hierarchical memory */
void superchunk_hierarchical_free(SuperchunkHierarchicalCompressedChunk* sc) {
    if (!sc) return;
    
    if (sc->chunk_segments) {
        for (int i = 0; i < sc->num_chunks; i++) {
            for (int seg = 0; seg < 64; seg++) {
                if (sc->chunk_segments[i].segments[seg].num_positions > 0) {
                    free(sc->chunk_segments[i].segments[seg].positions);
                } else {
                    free(sc->chunk_segments[i].segments[seg].mask);
                }
            }
        }
        free(sc->chunk_segments);
        sc->chunk_segments = NULL;
    }
    
    if (sc->wall_chunks) {
        for (int i = 0; i < sc->num_wall_chunks; i++) {
            free(sc->wall_chunks[i].compressed_data);
        }
        free(sc->wall_chunks);
        sc->wall_chunks = NULL;
    }
    
    free(sc->compressed_block_data);
    sc->compressed_block_data = NULL;
    sc->compressed_block_data_size = 0;
    sc->num_chunks = 0;
    sc->num_wall_chunks = 0;
}

/* Portable popcount for MSVC compatibility */
static int popcount(unsigned int x) {
#if defined(_MSC_VER)
    int count = 0;
    while (x) {
        count++;
        x &= x - 1;
    }
    return count;
#else
    return __builtin_popcount(x);
#endif
}

/* ============================================================================
 * Lazy Loading API - For fast partial chunk loading (NEW)
 * ============================================================================ */

/* Load a single chunk from superchunk without decompressing all 256 chunks
 * OPTIMIZED VERSION: Only decompresses block data for the requested chunk */
RawChunk* superchunk_load_single_chunk(const SuperchunkHierarchicalCompressedChunk* sc, 
                                        int chunk_x, int chunk_z) {
    if (!sc || chunk_x < 0 || chunk_x >= 16 || chunk_z < 0 || chunk_z >= 16) return NULL;
    
    /* Find the chunk in the superchunk */
    int chunk_idx = -1;
    for (int i = 0; i < sc->num_chunks; i++) {
        if (sc->chunk_segments[i].chunk_x == chunk_x && 
            sc->chunk_segments[i].chunk_z == chunk_z) {
            chunk_idx = i;
            break;
        }
    }
    
    /* If not found in terrain chunks, check if it's a wall chunk */
    if (chunk_idx < 0) {
        for (int wi = 0; wi < sc->num_wall_chunks; wi++) {
            if (sc->wall_chunks[wi].chunk_x == chunk_x && 
                sc->wall_chunks[wi].chunk_z == chunk_z) {
                /* Wall chunk - decompress individually */
                CompressedChunk wall_comp;
                RawChunk* chunk = (RawChunk*)calloc(1, sizeof(RawChunk));
                if (!chunk) return NULL;
                
                memset(&wall_comp, 0, sizeof(wall_comp));
                if (compress_decode_binary(sc->wall_chunks[wi].compressed_data,
                                           sc->wall_chunks[wi].compressed_size,
                                           &wall_comp) == 0 &&
                    decompress_chunk(&wall_comp, chunk) == 0) {
                    chunk->cx = sc->scx * SUPERCHUNK_SIZE_CHUNKS + chunk_x;
                    chunk->cz = sc->scz * SUPERCHUNK_SIZE_CHUNKS + chunk_z;
                    compress_chunk_free(&wall_comp);
                    return chunk;
                }
                compress_chunk_free(&wall_comp);
                free(chunk);
                return NULL;
            }
        }
        return NULL;
    }
    
    /* Allocate output chunk */
    RawChunk* chunk = (RawChunk*)calloc(1, sizeof(RawChunk));
    if (!chunk) return NULL;
    
    chunk->cx = sc->scx * SUPERCHUNK_SIZE_CHUNKS + chunk_x;
    chunk->cz = sc->scz * SUPERCHUNK_SIZE_CHUNKS + chunk_z;
    
    const SuperchunkChunkSegments* cseg = &sc->chunk_segments[chunk_idx];
    
    /* OPTIMIZATION: Calculate data_pos for all segments in this chunk only */
    /* This avoids decompressing the entire superchunk palette */
    
    /* First pass: count how many non-air blocks are in this chunk */
    uint32_t chunk_non_air_count = 0;
    for (uint32_t seg = 0; seg < 64; seg++) {
        if ((cseg->segment_summary & (1ULL << seg)) == 0) continue;
        const HierarchicalSegment* hs = &cseg->segments[seg];
        if (hs->type == SEG_TYPE_MIXED) {
            if (hs->num_positions > 0) {
                chunk_non_air_count += hs->num_positions;
            } else if (hs->mask) {
                for (uint32_t b = 0; b < hs->mask_size; b++) {
                    chunk_non_air_count += popcount(hs->mask[b]);
                }
            }
        }
    }
    
    /* Calculate global_data_pos by counting blocks in all previous chunks */
    uint32_t global_data_pos = 0;
    for (int ci = 0; ci < chunk_idx; ci++) {
        const SuperchunkChunkSegments* prev_cseg = &sc->chunk_segments[ci];
        for (uint32_t seg = 0; seg < 64; seg++) {
            if ((prev_cseg->segment_summary & (1ULL << seg)) == 0) continue;
            const HierarchicalSegment* hs = &prev_cseg->segments[seg];
            if (hs->type == SEG_TYPE_MIXED) {
                if (hs->num_positions > 0) {
                    global_data_pos += hs->num_positions;
                } else if (hs->mask) {
                    for (uint32_t b = 0; b < hs->mask_size; b++) {
                        global_data_pos += popcount(hs->mask[b]);
                    }
                }
            }
        }
    }
    
    /* OPTIMIZATION: Only decompress the block data for THIS CHUNK */
    uint16_t* chunk_block_data = NULL;
    if (chunk_non_air_count > 0 && sc->palette_size > 0) {
        chunk_block_data = (uint16_t*)malloc(chunk_non_air_count * sizeof(uint16_t));
        if (!chunk_block_data) {
            free(chunk);
            return NULL;
        }
        
        /* For single palette entry, all blocks are the same */
        if (sc->palette_size <= 1) {
            for (uint32_t i = 0; i < chunk_non_air_count; i++) {
                chunk_block_data[i] = sc->palette[0];
            }
        } else if (sc->bits_per_index < 16 && sc->compressed_block_data) {
            /* OPTIMIZATION: Skip to the relevant section of compressed data
             * We need to decompress from global_data_pos to global_data_pos + chunk_non_air_count */
            uint64_t bit_buffer = 0;
            int bits_in_buffer = 0;
            size_t in_pos = 0;
            uint8_t bits = sc->bits_per_index;
            uint8_t mask = (1 << bits) - 1;
            
            /* Calculate byte position to start reading from */
            /* Each block index uses 'bits' bits, so global_data_pos uses global_data_pos * bits bits */
            size_t bits_to_skip = (size_t)global_data_pos * bits;
            in_pos = bits_to_skip / 8;
            int bits_to_discard = bits_to_skip % 8;
            
            /* Prime the bit buffer from the starting position */
            if (in_pos < sc->compressed_block_data_size) {
                while (bits_in_buffer < bits_to_discard && in_pos < sc->compressed_block_data_size) {
                    bit_buffer = (bit_buffer << 8) | sc->compressed_block_data[in_pos++];
                    bits_in_buffer += 8;
                }
                if (bits_in_buffer >= bits_to_discard) {
                    bits_in_buffer -= bits_to_discard;
                    bit_buffer &= ((1ULL << bits_in_buffer) - 1);
                }
            }
            
            /* Now decompress only the blocks we need */
            uint32_t out_idx = 0;
            while (out_idx < chunk_non_air_count && in_pos < sc->compressed_block_data_size) {
                while (bits_in_buffer < bits && in_pos < sc->compressed_block_data_size) {
                    bit_buffer = (bit_buffer << 8) | sc->compressed_block_data[in_pos++];
                    bits_in_buffer += 8;
                }
                
                bits_in_buffer -= bits;
                uint8_t palette_idx = (bit_buffer >> bits_in_buffer) & mask;
                
                if (palette_idx < sc->palette_size) {
                    chunk_block_data[out_idx++] = sc->palette[palette_idx];
                } else {
                    chunk_block_data[out_idx++] = 0;
                }
            }
        } else if (sc->compressed_block_data) {
            /* Raw 16-bit data - direct copy */
            size_t bytes_to_skip = global_data_pos * sizeof(uint16_t);
            size_t bytes_to_copy = chunk_non_air_count * sizeof(uint16_t);
            if (bytes_to_skip + bytes_to_copy <= sc->compressed_block_data_size) {
                memcpy(chunk_block_data, 
                       (uint16_t*)sc->compressed_block_data + global_data_pos,
                       bytes_to_copy);
            }
        }
    }
    
    /* Now decode this chunk's segments using the chunk-specific block data */
    uint32_t chunk_data_pos = 0;
    
    for (uint32_t seg = 0; seg < 64; seg++) {
        uint32_t y_start = seg * 16;
        uint32_t y_end = (y_start + 16 < CHUNK_SIZE_Y) ? (y_start + 16) : CHUNK_SIZE_Y;
        
        if ((cseg->segment_summary & (1ULL << seg)) == 0) continue;
        
        const HierarchicalSegment* hs = &cseg->segments[seg];
        
        if (hs->type == SEG_TYPE_UNIFORM) {
            if (hs->uniform_block != 0) {
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                        for (uint32_t y = y_start; y < y_end; y++) {
                            chunk->blocks[x][z][y] = hs->uniform_block;
                        }
                    }
                }
            }
        } else if (hs->type == SEG_TYPE_MIXED) {
            if (hs->num_positions > 0 && hs->positions) {
                for (uint32_t i = 0; i < hs->num_positions; i++) {
                    if (chunk_data_pos < chunk_non_air_count) {
                        uint16_t pos = hs->positions[i];
                        uint32_t bit_pos = pos;
                        uint32_t y = (bit_pos % 16) + y_start;
                        uint32_t xz = bit_pos / 16;
                        uint32_t z = xz % CHUNK_SIZE_Z;
                        uint32_t x = xz / CHUNK_SIZE_Z;
                        
                        if (x < CHUNK_SIZE_X && z < CHUNK_SIZE_Z && y < y_end) {
                            chunk->blocks[x][z][y] = chunk_block_data[chunk_data_pos++];
                        }
                    }
                }
            } else if (hs->mask) {
                uint32_t bit_pos = 0;
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                        for (uint32_t y = y_start; y < y_end; y++) {
                            uint32_t byte_pos = bit_pos / 8;
                            uint8_t bit_offset = bit_pos % 8;
                            
                            if (byte_pos < hs->mask_size &&
                                (hs->mask[byte_pos] & (1 << bit_offset))) {
                                if (chunk_data_pos < chunk_non_air_count) {
                                    chunk->blocks[x][z][y] = chunk_block_data[chunk_data_pos++];
                                }
                            }
                            bit_pos++;
                        }
                    }
                }
            }
        }
    }
    
    free(chunk_block_data);
    return chunk;
}

/* Load chunks within radius from superchunk center
 * OPTIMIZED VERSION: Pre-calculates data positions and shares work across chunks */
int superchunk_load_chunks_radius(const SuperchunkHierarchicalCompressedChunk* sc,
                                   int center_x, int center_z, int radius,
                                   RawChunk** out_chunks, int max_chunks) {
    if (!sc || !out_chunks || radius < 0 || max_chunks <= 0) return 0;
    
    /* OPTIMIZATION: Pre-calculate which chunks to load and their data positions */
    typedef struct {
        int chunk_x, chunk_z;
        int chunk_idx;
        uint32_t global_data_pos;
        uint32_t chunk_non_air_count;
    } ChunkLoadInfo;
    
    ChunkLoadInfo* load_info = (ChunkLoadInfo*)calloc(max_chunks, sizeof(ChunkLoadInfo));
    if (!load_info) return 0;
    
    int chunks_to_load = 0;
    
    /* First pass: identify chunks to load and calculate their block counts */
    for (int cx = center_x - radius; cx <= center_x + radius && chunks_to_load < max_chunks; cx++) {
        for (int cz = center_z - radius; cz <= center_z + radius && chunks_to_load < max_chunks; cz++) {
            if (cx < 0 || cx >= 16 || cz < 0 || cz >= 16) continue;
            
            /* Find chunk index */
            int chunk_idx = -1;
            for (int i = 0; i < sc->num_chunks; i++) {
                if (sc->chunk_segments[i].chunk_x == cx && 
                    sc->chunk_segments[i].chunk_z == cz) {
                    chunk_idx = i;
                    break;
                }
            }
            
            if (chunk_idx < 0) continue; /* Skip wall chunks for now */
            
            const SuperchunkChunkSegments* cseg = &sc->chunk_segments[chunk_idx];
            
            /* Count non-air blocks in this chunk */
            uint32_t chunk_non_air = 0;
            for (uint32_t seg = 0; seg < 64; seg++) {
                if ((cseg->segment_summary & (1ULL << seg)) == 0) continue;
                const HierarchicalSegment* hs = &cseg->segments[seg];
                if (hs->type == SEG_TYPE_MIXED) {
                    if (hs->num_positions > 0) {
                        chunk_non_air += hs->num_positions;
                    } else if (hs->mask) {
                        for (uint32_t b = 0; b < hs->mask_size; b++) {
                            chunk_non_air += popcount(hs->mask[b]);
                        }
                    }
                }
            }
            
            load_info[chunks_to_load].chunk_x = cx;
            load_info[chunks_to_load].chunk_z = cz;
            load_info[chunks_to_load].chunk_idx = chunk_idx;
            load_info[chunks_to_load].chunk_non_air_count = chunk_non_air;
            chunks_to_load++;
        }
    }
    
    /* OPTIMIZATION: Sort by chunk_idx and calculate cumulative data positions */
    /* Simple bubble sort by chunk_idx */
    for (int i = 0; i < chunks_to_load - 1; i++) {
        for (int j = i + 1; j < chunks_to_load; j++) {
            if (load_info[j].chunk_idx < load_info[i].chunk_idx) {
                ChunkLoadInfo tmp = load_info[i];
                load_info[i] = load_info[j];
                load_info[j] = tmp;
            }
        }
    }
    
    /* Calculate global_data_pos for each chunk */
    uint32_t cumulative_pos = 0;
    int current_idx = 0;
    for (int i = 0; i < chunks_to_load; i++) {
        /* Fill in positions for chunks between current and target */
        while (current_idx < load_info[i].chunk_idx) {
            const SuperchunkChunkSegments* cseg = &sc->chunk_segments[current_idx];
            for (uint32_t seg = 0; seg < 64; seg++) {
                if ((cseg->segment_summary & (1ULL << seg)) == 0) continue;
                const HierarchicalSegment* hs = &cseg->segments[seg];
                if (hs->type == SEG_TYPE_MIXED) {
                    if (hs->num_positions > 0) {
                        cumulative_pos += hs->num_positions;
                    } else if (hs->mask) {
                        for (uint32_t b = 0; b < hs->mask_size; b++) {
                            cumulative_pos += popcount(hs->mask[b]);
                        }
                    }
                }
            }
            current_idx++;
        }
        load_info[i].global_data_pos = cumulative_pos;
        cumulative_pos += load_info[i].chunk_non_air_count;
        current_idx++;
    }
    
    /* OPTIMIZATION: Batch decompress block data for all chunks at once */
    /* Calculate total blocks needed */
    uint32_t total_blocks_needed = 0;
    for (int i = 0; i < chunks_to_load; i++) {
        total_blocks_needed += load_info[i].chunk_non_air_count;
    }
    
    /* Allocate buffer for all block data */
    uint16_t* all_block_data = NULL;
    if (total_blocks_needed > 0 && sc->palette_size > 0) {
        all_block_data = (uint16_t*)malloc(total_blocks_needed * sizeof(uint16_t));
        if (!all_block_data) {
            free(load_info);
            return 0;
        }
        
        /* Find minimum data position */
        uint32_t min_data_pos = (chunks_to_load > 0) ? load_info[0].global_data_pos : 0;
        
        /* OPTIMIZATION: Decompress from first needed block to last needed block */
        if (sc->palette_size <= 1) {
            for (uint32_t i = 0; i < total_blocks_needed; i++) {
                all_block_data[i] = sc->palette[0];
            }
        } else if (sc->bits_per_index < 16 && sc->compressed_block_data) {
            uint64_t bit_buffer = 0;
            int bits_in_buffer = 0;
            size_t in_pos = 0;
            uint8_t bits = sc->bits_per_index;
            uint8_t mask = (1 << bits) - 1;
            
            /* Skip to first needed block */
            size_t bits_to_skip = (size_t)min_data_pos * bits;
            in_pos = bits_to_skip / 8;
            int bits_to_discard = bits_to_skip % 8;
            
            if (in_pos < sc->compressed_block_data_size) {
                while (bits_in_buffer < bits_to_discard && in_pos < sc->compressed_block_data_size) {
                    bit_buffer = (bit_buffer << 8) | sc->compressed_block_data[in_pos++];
                    bits_in_buffer += 8;
                }
                if (bits_in_buffer >= bits_to_discard) {
                    bits_in_buffer -= bits_to_discard;
                    bit_buffer &= ((1ULL << bits_in_buffer) - 1);
                }
            }
            
            /* Decompress all needed blocks */
            uint32_t out_idx = 0;
            while (out_idx < total_blocks_needed && in_pos < sc->compressed_block_data_size) {
                while (bits_in_buffer < bits && in_pos < sc->compressed_block_data_size) {
                    bit_buffer = (bit_buffer << 8) | sc->compressed_block_data[in_pos++];
                    bits_in_buffer += 8;
                }
                
                bits_in_buffer -= bits;
                uint8_t palette_idx = (bit_buffer >> bits_in_buffer) & mask;
                
                if (palette_idx < sc->palette_size) {
                    all_block_data[out_idx++] = sc->palette[palette_idx];
                } else {
                    all_block_data[out_idx++] = 0;
                }
            }
        } else if (sc->compressed_block_data) {
            /* Raw 16-bit data */
            size_t bytes_to_skip = min_data_pos * sizeof(uint16_t);
            size_t bytes_to_copy = total_blocks_needed * sizeof(uint16_t);
            if (bytes_to_skip + bytes_to_copy <= sc->compressed_block_data_size) {
                memcpy(all_block_data, 
                       (uint16_t*)sc->compressed_block_data + min_data_pos,
                       bytes_to_copy);
            }
        }
    }
    
    /* Now load each chunk using the pre-calculated data */
    int loaded = 0;
    uint32_t current_block_offset = 0;
    
    for (int i = 0; i < chunks_to_load; i++) {
        RawChunk* chunk = (RawChunk*)calloc(1, sizeof(RawChunk));
        if (!chunk) continue;
        
        chunk->cx = sc->scx * SUPERCHUNK_SIZE_CHUNKS + load_info[i].chunk_x;
        chunk->cz = sc->scz * SUPERCHUNK_SIZE_CHUNKS + load_info[i].chunk_z;
        
        const SuperchunkChunkSegments* cseg = &sc->chunk_segments[load_info[i].chunk_idx];
        uint16_t* chunk_block_data = all_block_data + current_block_offset;
        uint32_t chunk_data_pos = 0;
        
        /* Decode segments */
        for (uint32_t seg = 0; seg < 64; seg++) {
            uint32_t y_start = seg * 16;
            uint32_t y_end = (y_start + 16 < CHUNK_SIZE_Y) ? (y_start + 16) : CHUNK_SIZE_Y;
            
            if ((cseg->segment_summary & (1ULL << seg)) == 0) continue;
            
            const HierarchicalSegment* hs = &cseg->segments[seg];
            
            if (hs->type == SEG_TYPE_UNIFORM) {
                if (hs->uniform_block != 0) {
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                            for (uint32_t y = y_start; y < y_end; y++) {
                                chunk->blocks[x][z][y] = hs->uniform_block;
                            }
                        }
                    }
                }
            } else if (hs->type == SEG_TYPE_MIXED) {
                if (hs->num_positions > 0 && hs->positions) {
                    for (uint32_t p = 0; p < hs->num_positions; p++) {
                        if (chunk_data_pos < load_info[i].chunk_non_air_count) {
                            uint16_t pos = hs->positions[p];
                            uint32_t bit_pos = pos;
                            uint32_t y = (bit_pos % 16) + y_start;
                            uint32_t xz = bit_pos / 16;
                            uint32_t z = xz % CHUNK_SIZE_Z;
                            uint32_t x = xz / CHUNK_SIZE_Z;
                            
                            if (x < CHUNK_SIZE_X && z < CHUNK_SIZE_Z && y < y_end) {
                                chunk->blocks[x][z][y] = chunk_block_data[chunk_data_pos++];
                            }
                        }
                    }
                } else if (hs->mask) {
                    uint32_t bit_pos = 0;
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                            for (uint32_t y = y_start; y < y_end; y++) {
                                uint32_t byte_pos = bit_pos / 8;
                                uint8_t bit_offset = bit_pos % 8;
                                
                                if (byte_pos < hs->mask_size &&
                                    (hs->mask[byte_pos] & (1 << bit_offset))) {
                                    if (chunk_data_pos < load_info[i].chunk_non_air_count) {
                                        chunk->blocks[x][z][y] = chunk_block_data[chunk_data_pos++];
                                    }
                                }
                                bit_pos++;
                            }
                        }
                    }
                }
            }
        }
        
        out_chunks[loaded++] = chunk;
        current_block_offset += load_info[i].chunk_non_air_count;
    }
    
    free(all_block_data);
    free(load_info);
    return loaded;
}

/* Get block at specific position without full chunk decompression */
uint16_t superchunk_get_block(const SuperchunkHierarchicalCompressedChunk* sc,
                               int chunk_x, int chunk_z, int x, int y, int z) {
    if (!sc || chunk_x < 0 || chunk_x >= 16 || chunk_z < 0 || chunk_z >= 16) return 0;
    if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z) return 0;
    
    /* Find the chunk */
    int chunk_idx = -1;
    for (int i = 0; i < sc->num_chunks; i++) {
        if (sc->chunk_segments[i].chunk_x == chunk_x && 
            sc->chunk_segments[i].chunk_z == chunk_z) {
            chunk_idx = i;
            break;
        }
    }
    
    if (chunk_idx < 0) return 0; /* Not found or all air */
    
    const SuperchunkChunkSegments* cseg = &sc->chunk_segments[chunk_idx];
    
    /* Calculate segment index from Y */
    int seg_idx = y / 16;
    if (seg_idx < 0 || seg_idx >= 64) return 0;
    
    /* Check segment summary */
    if ((cseg->segment_summary & (1ULL << seg_idx)) == 0) return 0; /* All air */
    
    const HierarchicalSegment* hs = &cseg->segments[seg_idx];
    
    if (hs->type == SEG_TYPE_UNIFORM) {
        return hs->uniform_block;
    } else if (hs->type == SEG_TYPE_MIXED) {
        /* Calculate position within segment */
        uint32_t y_in_seg = y % 16;
        uint32_t bit_pos = (x * CHUNK_SIZE_Z + z) * 16 + y_in_seg;
        
        if (hs->num_positions > 0 && hs->positions) {
            /* Sparse: search position list */
            for (uint32_t i = 0; i < hs->num_positions; i++) {
                if (hs->positions[i] == bit_pos) {
                    /* For single block query, return placeholder indicating block exists */
                    return 1; /* Block exists */
                }
            }
            return 0; /* Not in position list = air */
        } else if (hs->mask) {
            /* Bitmap: check bit */
            uint32_t byte_pos = bit_pos / 8;
            uint8_t bit_offset = bit_pos % 8;
            
            if (byte_pos < hs->mask_size && (hs->mask[byte_pos] & (1 << bit_offset))) {
                /* Block exists - getting exact ID requires global block data decompression */
                /* For performance, could return a placeholder or decompress just this block's palette entry */
                return 1; /* Block exists */
            }
            return 0; /* Air */
        }
    }
    
    return 0;
}

/* ============================================================================
 * Segment-Level Partial Decode API (Hierarchical RLE V2)
 * ============================================================================ */

int decompress_hierarchical_segments(const HierarchicalRLECompressedChunk* hc,
                                     int seg_start, int seg_count, RawChunk* out) {
    if (!hc || !out || seg_start < 0 || seg_count <= 0 || seg_start + seg_count > 64) return -1;
    
    /* Initialize output if not already */
    /* Note: we assume caller has memset out to 0 for air blocks */
    
    /* Decompress palette data */
    uint16_t* decompressed_blocks = NULL;
    uint32_t decompressed_count = 0;
    
    if (hc->total_non_air_blocks > 0) {
        decompressed_blocks = (uint16_t*)malloc(hc->total_non_air_blocks * sizeof(uint16_t));
        if (!decompressed_blocks) return -1;
        
        if (hc->palette_size <= 1) {
            for (uint32_t i = 0; i < hc->total_non_air_blocks; i++) {
                decompressed_blocks[i] = hc->palette[0];
            }
            decompressed_count = hc->total_non_air_blocks;
        } else if (hc->bits_per_index < 8 && hc->compressed_block_data) {
            uint64_t bit_buffer = 0;
            int bits_in_buffer = 0;
            size_t in_pos = 0;
            uint8_t bits = hc->bits_per_index;
            uint8_t mask = (1 << bits) - 1;
            
            for (uint32_t i = 0; i < hc->total_non_air_blocks; i++) {
                while (bits_in_buffer < bits && in_pos < hc->compressed_block_data_size) {
                    bit_buffer = (bit_buffer << 8) | hc->compressed_block_data[in_pos++];
                    bits_in_buffer += 8;
                }
                
                bits_in_buffer -= bits;
                uint8_t palette_idx = (bit_buffer >> bits_in_buffer) & mask;
                
                if (palette_idx < hc->palette_size) {
                    decompressed_blocks[decompressed_count++] = hc->palette[palette_idx];
                } else {
                    decompressed_blocks[decompressed_count++] = 0;
                }
            }
        } else if (hc->compressed_block_data) {
            memcpy(decompressed_blocks, hc->compressed_block_data, hc->total_non_air_blocks * sizeof(uint16_t));
            decompressed_count = hc->total_non_air_blocks;
        }
    }
    
    /* Process only requested segments */
    uint32_t data_pos = 0;
    
    /* Calculate data_pos for segments before seg_start */
    for (int seg = 0; seg < seg_start && seg < 64; seg++) {
        if ((hc->segment_summary & (1ULL << seg)) == 0) continue;
        const HierarchicalSegment* hs = &hc->segments[seg];
        if (hs->type == SEG_TYPE_MIXED && hs->mask) {
            /* Count bits in mask */
            for (uint32_t b = 0; b < hs->mask_size; b++) {
                data_pos += popcount(hs->mask[b]);
            }
        }
    }
    
    /* Decode requested segments */
    for (int seg = seg_start; seg < seg_start + seg_count && seg < 64; seg++) {
        uint32_t y_start = seg * HIERARCHICAL_SEGMENT_HEIGHT;
        uint32_t y_end = (y_start + HIERARCHICAL_SEGMENT_HEIGHT < CHUNK_SIZE_Y) ?
                         (y_start + HIERARCHICAL_SEGMENT_HEIGHT) : CHUNK_SIZE_Y;
        
        if ((hc->segment_summary & (1ULL << seg)) == 0) continue;
        
        const HierarchicalSegment* hs = &hc->segments[seg];
        
        if (hs->type == SEG_TYPE_UNIFORM) {
            for (int x = 0; x < CHUNK_SIZE_X; x++) {
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (uint32_t y = y_start; y < y_end; y++) {
                        out->blocks[x][z][y] = hs->uniform_block;
                    }
                }
            }
        } else if (hs->type == SEG_TYPE_MIXED && hs->mask) {
            uint32_t bit_pos = 0;
            for (int x = 0; x < CHUNK_SIZE_X; x++) {
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (uint32_t y = y_start; y < y_end; y++) {
                        uint32_t byte_pos = bit_pos / 8;
                        uint8_t bit_offset = bit_pos % 8;
                        
                        if (byte_pos < hs->mask_size &&
                            (hs->mask[byte_pos] & (1 << bit_offset))) {
                            if (data_pos < decompressed_count) {
                                out->blocks[x][z][y] = decompressed_blocks[data_pos++];
                            }
                        }
                        bit_pos++;
                    }
                }
            }
        }
    }
    
    free(decompressed_blocks);
    return 0;
}

uint16_t hierarchical_rle_get_block(const HierarchicalRLECompressedChunk* hc, 
                                    int x, int y, int z) {
    if (!hc || x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z) return 0;
    
    int seg_idx = y / HIERARCHICAL_SEGMENT_HEIGHT;
    if (seg_idx < 0 || seg_idx >= 64) return 0;
    
    /* Check segment summary */
    if ((hc->segment_summary & (1ULL << seg_idx)) == 0) return 0;
    
    const HierarchicalSegment* hs = &hc->segments[seg_idx];
    
    if (hs->type == SEG_TYPE_UNIFORM) {
        return hs->uniform_block;
    } else if (hs->type == SEG_TYPE_MIXED && hs->mask) {
        uint32_t y_in_seg = y % HIERARCHICAL_SEGMENT_HEIGHT;
        uint32_t bit_pos = (x * CHUNK_SIZE_Z + z) * HIERARCHICAL_SEGMENT_HEIGHT + y_in_seg;
        uint32_t byte_pos = bit_pos / 8;
        uint8_t bit_offset = bit_pos % 8;
        
        if (byte_pos < hs->mask_size && (hs->mask[byte_pos] & (1 << bit_offset))) {
            /* Block exists - getting exact ID would require knowing its index in compressed data */
            /* For now, return placeholder indicating non-air block exists */
            return 1;
        }
    }
    
    return 0;
}

int hierarchical_rle_segment_has_blocks(const HierarchicalRLECompressedChunk* hc, int seg_idx) {
    if (!hc || seg_idx < 0 || seg_idx >= 64) return 0;
    return (hc->segment_summary & (1ULL << seg_idx)) != 0;
}

/* ============================================================================
 * Segment-Level Partial Decode API (RLE + Bitmask)
 * ============================================================================ */

int decompress_rle_bitmask_segments(const RLEBitmaskCompressedChunk* rc,
                                     int seg_start, int seg_count, RawChunk* out) {
    if (!rc || !out || seg_start < 0 || seg_count <= 0 || seg_start + seg_count > (int)rc->num_segments) return -1;
    
    /* Decompress palette data */
    uint16_t* decompressed_blocks = NULL;
    uint32_t decompressed_count = 0;
    
    if (rc->total_non_air_blocks > 0) {
        decompressed_blocks = (uint16_t*)malloc(rc->total_non_air_blocks * sizeof(uint16_t));
        if (!decompressed_blocks) return -1;
        
        if (rc->palette_size <= 1) {
            for (uint32_t i = 0; i < rc->total_non_air_blocks; i++) {
                decompressed_blocks[i] = rc->palette[0];
            }
            decompressed_count = rc->total_non_air_blocks;
        } else if (rc->bits_per_index < 8 && rc->compressed_block_data) {
            uint64_t bit_buffer = 0;
            int bits_in_buffer = 0;
            size_t in_pos = 0;
            uint8_t bits = rc->bits_per_index;
            uint8_t mask = (1 << bits) - 1;
            
            for (uint32_t i = 0; i < rc->total_non_air_blocks; i++) {
                while (bits_in_buffer < bits && in_pos < rc->compressed_block_data_size) {
                    bit_buffer = (bit_buffer << 8) | rc->compressed_block_data[in_pos++];
                    bits_in_buffer += 8;
                }
                
                bits_in_buffer -= bits;
                uint8_t palette_idx = (bit_buffer >> bits_in_buffer) & mask;
                
                if (palette_idx < rc->palette_size) {
                    decompressed_blocks[decompressed_count++] = rc->palette[palette_idx];
                } else {
                    decompressed_blocks[decompressed_count++] = 0;
                }
            }
        } else if (rc->block_data) {
            for (uint32_t i = 0; i < rc->total_non_air_blocks && i < rc->block_data_size; i++) {
                decompressed_blocks[decompressed_count++] = rc->block_data[i];
            }
        }
    }
    
    /* Calculate data_pos for segments before seg_start */
    uint32_t data_pos = 0;
    for (int seg = 0; seg < seg_start && seg < (int)rc->num_segments; seg++) {
        if (!rc->presence_masks[seg]) continue;
        for (uint32_t b = 0; b < rc->mask_sizes[seg]; b++) {
            data_pos += popcount(rc->presence_masks[seg][b]);
        }
    }
    
    /* Decode requested segments */
    for (int seg = seg_start; seg < seg_start + seg_count && seg < (int)rc->num_segments; seg++) {
        uint32_t y_start = seg * RLE_SEGMENT_HEIGHT;
        uint32_t y_end = (y_start + RLE_SEGMENT_HEIGHT < CHUNK_SIZE_Y) ?
                         (y_start + RLE_SEGMENT_HEIGHT) : CHUNK_SIZE_Y;
        
        if (!rc->presence_masks[seg]) continue;
        
        uint32_t bit_pos = 0;
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (uint32_t y = y_start; y < y_end; y++) {
                    uint32_t byte_pos = bit_pos / 8;
                    uint8_t bit_offset = bit_pos % 8;
                    
                    if (byte_pos < rc->mask_sizes[seg] &&
                        (rc->presence_masks[seg][byte_pos] & (1 << bit_offset))) {
                        if (data_pos < decompressed_count) {
                            out->blocks[x][z][y] = decompressed_blocks[data_pos++];
                        }
                    }
                    bit_pos++;
                }
            }
        }
    }
    
    free(decompressed_blocks);
    return 0;
}

uint16_t rle_bitmask_get_block(const RLEBitmaskCompressedChunk* rc, int x, int y, int z) {
    if (!rc || x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z) return 0;
    
    int seg_idx = y / RLE_SEGMENT_HEIGHT;
    if (seg_idx < 0 || seg_idx >= (int)rc->num_segments) return 0;
    if (!rc->presence_masks[seg_idx]) return 0;
    
    uint32_t y_in_seg = y % RLE_SEGMENT_HEIGHT;
    uint32_t bit_pos = (x * CHUNK_SIZE_Z + z) * RLE_SEGMENT_HEIGHT + y_in_seg;
    uint32_t byte_pos = bit_pos / 8;
    uint8_t bit_offset = bit_pos % 8;
    
    if (byte_pos < rc->mask_sizes[seg_idx] &&
        (rc->presence_masks[seg_idx][byte_pos] & (1 << bit_offset))) {
        return 1; /* Block exists */
    }
    
    return 0;
}

int rle_bitmask_segment_has_blocks(const RLEBitmaskCompressedChunk* rc, int seg_idx) {
    if (!rc || seg_idx < 0 || seg_idx >= (int)rc->num_segments) return 0;
    if (!rc->presence_masks[seg_idx]) return 0;
    
    /* Check if any bit is set in the mask */
    for (uint32_t i = 0; i < rc->mask_sizes[seg_idx]; i++) {
        if (rc->presence_masks[seg_idx][i] != 0) return 1;
    }
    return 0;
}

/* ============================================================================
 * Sparse Column Direct Access API
 * ============================================================================ */

const uint8_t* sparse_column_get_column_ptr(const SparseColumnCompressedChunk* sc,
                                            int x, int z, int* height) {
    if (!sc || !height || x < 0 || x >= CHUNK_SIZE_X || z < 0 || z >= CHUNK_SIZE_Z) {
        if (height) *height = 0;
        return NULL;
    }
    
    if (!sc->has_column[x][z] || !sc->columns[x][z].blocks) {
        *height = 0;
        return NULL;
    }
    
    *height = sc->columns[x][z].height;
    return sc->columns[x][z].blocks;
}

int sparse_column_get_surface_height(const SparseColumnCompressedChunk* sc, int x, int z) {
    if (!sc || x < 0 || x >= CHUNK_SIZE_X || z < 0 || z >= CHUNK_SIZE_Z) return 0;
    return sc->surface_height[x][z];
}

int compress_hierarchical_rle(const RawChunk* raw, HierarchicalRLECompressedChunk* out) {
    if (!raw || !out) return -1;
    
    memset(out, 0, sizeof(HierarchicalRLECompressedChunk));
    
    out->num_segments = 64; /* 64 segments of 16 Y-blocks each */
    out->segments = (HierarchicalSegment*)calloc(out->num_segments, sizeof(HierarchicalSegment));
    if (!out->segments) return -1;
    
    /* Temporary buffer for block data */
    uint16_t* temp_blocks = (uint16_t*)malloc(CHUNK_TOTAL_BLOCKS * sizeof(uint16_t));
    if (!temp_blocks) {
        free(out->segments);
        return -1;
    }
    
    uint32_t temp_pos = 0;
    
    /* Process each segment */
    for (uint32_t seg = 0; seg < out->num_segments; seg++) {
        uint32_t y_start = seg * HIERARCHICAL_SEGMENT_HEIGHT;
        uint32_t y_end = (y_start + HIERARCHICAL_SEGMENT_HEIGHT < CHUNK_SIZE_Y) ? 
                         (y_start + HIERARCHICAL_SEGMENT_HEIGHT) : CHUNK_SIZE_Y;
        
        uint32_t non_air = 0;
        uint16_t first_block = 0;
        int is_uniform = 1;
        int found_first = 0;
        
        /* Analyze segment */
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (uint32_t y = y_start; y < y_end; y++) {
                    uint16_t block = raw->blocks[x][z][y];
                    if (block != 0) {
                        non_air++;
                        if (!found_first) {
                            first_block = block;
                            found_first = 1;
                        } else if (block != first_block) {
                            is_uniform = 0;
                        }
                    }
                }
            }
        }
        
        HierarchicalSegment* hs = &out->segments[seg];
        
        if (non_air == 0) {
            /* All air - mark in summary, no data needed */
            hs->type = SEG_TYPE_ALL_AIR;
        } else if (is_uniform && found_first) {
            /* Uniform segment */
            hs->type = SEG_TYPE_UNIFORM;
            hs->uniform_block = first_block;
            out->segment_summary |= (1ULL << seg);
        } else {
            /* Mixed segment - need mask */
            hs->type = SEG_TYPE_MIXED;
            out->segment_summary |= (1ULL << seg);
            
            uint32_t seg_blocks = CHUNK_SIZE_X * CHUNK_SIZE_Z * (y_end - y_start);
            uint32_t mask_bytes = (seg_blocks + 7) / 8;
            hs->mask_size = mask_bytes;
            hs->mask = (uint8_t*)calloc(1, mask_bytes);
            
            if (hs->mask) {
                uint32_t bit_pos = 0;
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                        for (uint32_t y = y_start; y < y_end; y++) {
                            uint16_t block = raw->blocks[x][z][y];
                            if (block != 0) {
                                uint32_t byte_pos = bit_pos / 8;
                                uint8_t bit_offset = bit_pos % 8;
                                hs->mask[byte_pos] |= (1 << bit_offset);
                                temp_blocks[temp_pos++] = block;
                            }
                            bit_pos++;
                        }
                    }
                }
            }
        }
    }
    
    out->total_non_air_blocks = temp_pos;
    
    /* Build palette from non-air blocks */
    if (temp_pos > 0) {
        int block_counts[65536] = {0};
        for (uint32_t i = 0; i < temp_pos; i++) {
            block_counts[temp_blocks[i]]++;
        }
        
        /* Build palette (up to 64 entries) */
        out->palette_size = 0;
        for (int b = 0; b < 65536 && out->palette_size < 64; b++) {
            if (block_counts[b] > 0) {
                out->palette[out->palette_size++] = (uint16_t)b;
            }
        }
        
        /* Determine bits per index */
        if (out->palette_size <= 2) out->bits_per_index = 1;
        else if (out->palette_size <= 4) out->bits_per_index = 2;
        else if (out->palette_size <= 8) out->bits_per_index = 3;
        else if (out->palette_size <= 16) out->bits_per_index = 4;
        else if (out->palette_size <= 32) out->bits_per_index = 5;
        else out->bits_per_index = 6;
        
        /* Build reverse lookup */
        uint8_t block_to_palette[65536];
        memset(block_to_palette, 0xFF, sizeof(block_to_palette));
        for (uint8_t i = 0; i < out->palette_size; i++) {
            block_to_palette[out->palette[i]] = i;
        }
        
        /* Compress block data */
        if (out->bits_per_index < 8) {
            uint32_t num_indices = temp_pos;
            out->compressed_block_data_size = (num_indices * out->bits_per_index + 7) / 8;
            out->compressed_block_data = (uint8_t*)calloc(1, out->compressed_block_data_size);
            if (out->compressed_block_data) {
                uint64_t bit_buffer = 0;
                int bits_in_buffer = 0;
                size_t out_pos = 0;
                uint8_t bits = out->bits_per_index;
                uint8_t mask = (1 << bits) - 1;
                
                for (uint32_t i = 0; i < num_indices; i++) {
                    uint8_t palette_idx = block_to_palette[temp_blocks[i]];
                    bit_buffer = (bit_buffer << bits) | (palette_idx & mask);
                    bits_in_buffer += bits;
                    
                    while (bits_in_buffer >= 8) {
                        bits_in_buffer -= 8;
                        out->compressed_block_data[out_pos++] = (bit_buffer >> bits_in_buffer) & 0xFF;
                    }
                }
                
                if (bits_in_buffer > 0) {
                    out->compressed_block_data[out_pos++] = (bit_buffer << (8 - bits_in_buffer)) & 0xFF;
                }
            }
        } else {
            /* Store raw 16-bit data */
            out->compressed_block_data_size = temp_pos * 2;
            out->compressed_block_data = (uint8_t*)malloc(out->compressed_block_data_size);
            if (out->compressed_block_data) {
                memcpy(out->compressed_block_data, temp_blocks, out->compressed_block_data_size);
            }
        }
    }
    
    free(temp_blocks);
    return 0;
}

int decompress_rle_bitmask(const RLEBitmaskCompressedChunk* rc, RawChunk* out) {
    if (!rc || !out) return -1;
    
    memset(out, 0, sizeof(RawChunk));
    
    if (rc->num_segments == 0) return 0;
    
    /* Decompress palette-compressed block data */
    uint16_t* decompressed_blocks = NULL;
    uint32_t decompressed_count = 0;
    
    if (rc->total_non_air_blocks > 0) {
        decompressed_blocks = (uint16_t*)malloc(rc->total_non_air_blocks * sizeof(uint16_t));
        if (!decompressed_blocks) return -1;
        
        if (rc->palette_size <= 1) {
            /* Single block type - fill all with palette[0] */
            for (uint32_t i = 0; i < rc->total_non_air_blocks; i++) {
                decompressed_blocks[i] = rc->palette[0];
            }
            decompressed_count = rc->total_non_air_blocks;
        } else if (rc->bits_per_index < 8 && rc->compressed_block_data) {
            /* Bit-packed palette indices */
            uint64_t bit_buffer = 0;
            int bits_in_buffer = 0;
            size_t in_pos = 0;
            uint8_t bits = rc->bits_per_index;
            uint8_t mask = (1 << bits) - 1;
            
            for (uint32_t i = 0; i < rc->total_non_air_blocks; i++) {
                while (bits_in_buffer < bits && in_pos < rc->compressed_block_data_size) {
                    bit_buffer = (bit_buffer << 8) | rc->compressed_block_data[in_pos++];
                    bits_in_buffer += 8;
                }
                
                bits_in_buffer -= bits;
                uint8_t palette_idx = (bit_buffer >> bits_in_buffer) & mask;
                
                if (palette_idx < rc->palette_size) {
                    decompressed_blocks[decompressed_count++] = rc->palette[palette_idx];
                } else {
                    decompressed_blocks[decompressed_count++] = 0;
                }
            }
        } else if (rc->block_data) {
            /* Legacy 8-bit block data */
            for (uint32_t i = 0; i < rc->total_non_air_blocks && i < rc->block_data_size; i++) {
                decompressed_blocks[decompressed_count++] = rc->block_data[i];
            }
        }
    }
    
    /* Reconstruct chunk segment by segment */
    uint32_t data_pos = 0;
    
    for (uint32_t seg = 0; seg < rc->num_segments; seg++) {
        uint32_t y_start = seg * RLE_SEGMENT_HEIGHT;
        uint32_t y_end = (y_start + RLE_SEGMENT_HEIGHT < CHUNK_SIZE_Y) ?
                         (y_start + RLE_SEGMENT_HEIGHT) : CHUNK_SIZE_Y;
        
        if (!rc->presence_masks[seg]) continue;
        
        uint32_t bit_pos = 0;
        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (uint32_t y = y_start; y < y_end; y++) {
                    uint32_t byte_pos = bit_pos / 8;
                    uint8_t bit_offset = bit_pos % 8;
                    
                    if (byte_pos < rc->mask_sizes[seg] &&
                        (rc->presence_masks[seg][byte_pos] & (1 << bit_offset))) {
                        /* Non-air block */
                        if (data_pos < decompressed_count) {
                            out->blocks[x][z][y] = decompressed_blocks[data_pos++];
                        }
                    }
                    bit_pos++;
                }
            }
        }
    }
    
    free(decompressed_blocks);
    return 0;
}

void rle_bitmask_free(RLEBitmaskCompressedChunk* rc) {
    if (!rc) return;
    for (uint32_t i = 0; i < rc->num_segments; i++) {
        free(rc->presence_masks[i]);
    }
    free(rc->presence_masks);
    free(rc->mask_sizes);
    free(rc->compressed_block_data);
    free(rc->air_run_lengths);
    memset(rc, 0, sizeof(RLEBitmaskCompressedChunk));
}

int rle_bitmask_encode_binary(const RLEBitmaskCompressedChunk* rc, uint8_t** out_data, size_t* out_size) {
    if (!rc || !out_data || !out_size) return -1;
    
    /* Calculate size */
    size_t size = 24; /* Header */
    size += rc->num_segments * 4; /* mask_sizes array */
    for (uint32_t i = 0; i < rc->num_segments; i++) {
        size += rc->mask_sizes[i]; /* masks */
    }
    size += rc->palette_size;
    size += rc->compressed_block_data_size;
    size += 4; /* total_non_air_blocks */
    
    *out_data = (uint8_t*)malloc(size);
    if (!*out_data) return -1;
    
    size_t pos = 0;
    
    /* Header */
    (*out_data)[pos++] = (uint8_t)COMPRESS_METHOD_RLE_BITMASK;
    (*out_data)[pos++] = (uint8_t)rc->num_segments;
    (*out_data)[pos++] = (uint8_t)rc->palette_size;
    (*out_data)[pos++] = rc->bits_per_index;
    
    /* Mask sizes */
    for (uint32_t i = 0; i < rc->num_segments; i++) {
        *(uint32_t*)(&(*out_data)[pos]) = rc->mask_sizes[i];
        pos += 4;
    }
    
    /* Masks */
    for (uint32_t i = 0; i < rc->num_segments; i++) {
        if (rc->presence_masks[i] && rc->mask_sizes[i] > 0) {
            memcpy(&(*out_data)[pos], rc->presence_masks[i], rc->mask_sizes[i]);
            pos += rc->mask_sizes[i];
        }
    }
    
    /* Palette */
    if (rc->palette_size > 0) {
        memcpy(&(*out_data)[pos], rc->palette, rc->palette_size);
        pos += rc->palette_size;
    }
    
    /* Compressed block data */
    if (rc->compressed_block_data_size > 0 && rc->compressed_block_data) {
        memcpy(&(*out_data)[pos], rc->compressed_block_data, rc->compressed_block_data_size);
        pos += rc->compressed_block_data_size;
    }
    
    /* Non-air count */
    *(uint32_t*)(&(*out_data)[pos]) = rc->total_non_air_blocks;
    pos += 4;
    
    *out_size = pos;
    return 0;
}
