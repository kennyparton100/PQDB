/**
 * sdk_settlement_terrain.c -- Terrain preparation for settlement zones
 */
#include "../sdk_settlement.h"
#include "../../Chunks/sdk_chunk.h"
#include <stdlib.h>
#include <math.h>

static int is_tree_cover_block(BlockType block)
{
    return block == BLOCK_LOG || block == BLOCK_LEAVES;
}

static int is_invalid_surface_block(BlockType block)
{
    return block == BLOCK_AIR ||
           block == BLOCK_WATER ||
           block == BLOCK_DIRT ||
           is_tree_cover_block(block);
}

static BlockType find_neighbor_surface_block(SdkChunk* chunk, int lx, int lz, BlockType fallback)
{
    int block_counts[256] = {0};
    int max_count = 0;
    BlockType best = fallback;
    int dx, dz;

    if (!chunk) return fallback;

    for (dx = -2; dx <= 2; ++dx) {
        for (dz = -2; dz <= 2; ++dz) {
            int sx = lx + dx;
            int sz = lz + dz;
            int sy;

            if ((dx == 0 && dz == 0) ||
                sx < 0 || sx >= CHUNK_WIDTH ||
                sz < 0 || sz >= CHUNK_DEPTH) {
                continue;
            }

            for (sy = CHUNK_HEIGHT - 1; sy >= 0; --sy) {
                BlockType block = sdk_chunk_get_block(chunk, sx, sy, sz);
                if (is_invalid_surface_block(block)) continue;
                if ((int)block >= 0 && (int)block < 256) {
                    block_counts[(int)block]++;
                }
                break;
            }
        }
    }

    for (dx = 0; dx < 256; ++dx) {
        if (block_counts[dx] > max_count) {
            max_count = block_counts[dx];
            best = (BlockType)dx;
        }
    }

    if (is_invalid_surface_block(best)) {
        best = BLOCK_GRASS;
    }
    return best;
}

static BlockType get_most_common_surface_block(SdkChunk* chunk, const BuildingZone* zone)
{
    int block_counts[256] = {0};
    int lx, lz, ly;
    int zone_min_x, zone_max_x, zone_min_z, zone_max_z;
    int chunk_min_x, chunk_max_x, chunk_min_z, chunk_max_z;
    BlockType most_common = BLOCK_GRASS;
    int max_count = 0;
    
    zone_min_x = zone->center_wx - zone->radius_x;
    zone_max_x = zone->center_wx + zone->radius_x;
    zone_min_z = zone->center_wz - zone->radius_z;
    zone_max_z = zone->center_wz + zone->radius_z;
    
    chunk_min_x = chunk->cx * CHUNK_WIDTH;
    chunk_max_x = chunk_min_x + CHUNK_WIDTH - 1;
    chunk_min_z = chunk->cz * CHUNK_DEPTH;
    chunk_max_z = chunk_min_z + CHUNK_DEPTH - 1;
    
    if (zone_max_x < chunk_min_x || zone_min_x > chunk_max_x ||
        zone_max_z < chunk_min_z || zone_min_z > chunk_max_z) {
        return BLOCK_GRASS;
    }
    
    for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
        for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
            int wx = chunk_min_x + lx;
            int wz = chunk_min_z + lz;
            
            if (wx < zone_min_x || wx > zone_max_x || wz < zone_min_z || wz > zone_max_z) {
                continue;
            }
            
            for (ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
                BlockType b = sdk_chunk_get_block(chunk, lx, ly, lz);
                if (!is_invalid_surface_block(b)) {
                    if (b < 256) block_counts[b]++;
                    break;
                }
            }
        }
    }
    
    for (int i = 0; i < 256; i++) {
        if (block_counts[i] > max_count) {
            max_count = block_counts[i];
            most_common = (BlockType)i;
        }
    }
    
    if (is_invalid_surface_block(most_common)) {
        most_common = BLOCK_GRASS;
    }
    
    return most_common;
}

static void sample_zone_terrain(SdkChunk* chunk, const BuildingZone* zone, int* min_y, int* max_y, int* median_y)
{
    int sample_count = 0;
    int sum_y = 0;
    int lx, lz, ly;
    int zone_min_x, zone_max_x, zone_min_z, zone_max_z;
    int chunk_min_x, chunk_max_x, chunk_min_z, chunk_max_z;
    
    *min_y = CHUNK_HEIGHT;
    *max_y = 0;
    *median_y = zone->base_elevation;
    
    zone_min_x = zone->center_wx - zone->radius_x;
    zone_max_x = zone->center_wx + zone->radius_x;
    zone_min_z = zone->center_wz - zone->radius_z;
    zone_max_z = zone->center_wz + zone->radius_z;
    
    chunk_min_x = chunk->cx * CHUNK_WIDTH;
    chunk_max_x = chunk_min_x + CHUNK_WIDTH - 1;
    chunk_min_z = chunk->cz * CHUNK_DEPTH;
    chunk_max_z = chunk_min_z + CHUNK_DEPTH - 1;
    
    if (zone_max_x < chunk_min_x || zone_min_x > chunk_max_x ||
        zone_max_z < chunk_min_z || zone_min_z > chunk_max_z) {
        return;
    }
    
    for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
        for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
            int wx = chunk_min_x + lx;
            int wz = chunk_min_z + lz;
            
            if (wx < zone_min_x || wx > zone_max_x || wz < zone_min_z || wz > zone_max_z) {
                continue;
            }
            
            for (ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
                BlockType b = sdk_chunk_get_block(chunk, lx, ly, lz);
                if (b != BLOCK_AIR && b != BLOCK_WATER) {
                    if (ly < *min_y) *min_y = ly;
                    if (ly > *max_y) *max_y = ly;
                    sum_y += ly;
                    sample_count++;
                    break;
                }
            }
        }
    }
    
    if (sample_count > 0) {
        *median_y = sum_y / sample_count;
    }
}

static void flatten_zone_terrain(SdkChunk* chunk, const BuildingZone* zone, int target_y, BlockType surface_block)
{
    int lx, lz, ly;
    int zone_min_x, zone_max_x, zone_min_z, zone_max_z;
    int chunk_min_x, chunk_max_x, chunk_min_z, chunk_max_z;
    
    zone_min_x = zone->center_wx - zone->radius_x;
    zone_max_x = zone->center_wx + zone->radius_x;
    zone_min_z = zone->center_wz - zone->radius_z;
    zone_max_z = zone->center_wz + zone->radius_z;
    
    chunk_min_x = chunk->cx * CHUNK_WIDTH;
    chunk_max_x = chunk_min_x + CHUNK_WIDTH - 1;
    chunk_min_z = chunk->cz * CHUNK_DEPTH;
    chunk_max_z = chunk_min_z + CHUNK_DEPTH - 1;
    
    if (zone_max_x < chunk_min_x || zone_min_x > chunk_max_x ||
        zone_max_z < chunk_min_z || zone_min_z > chunk_max_z) {
        return;
    }
    
    for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
        for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
            int wx = chunk_min_x + lx;
            int wz = chunk_min_z + lz;
            int current_surface_y = -1;
            BlockType replacement_block;
            int mantle_start;
            
            if (wx < zone_min_x || wx > zone_max_x || wz < zone_min_z || wz > zone_max_z) {
                continue;
            }
            
            for (ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
                BlockType b = sdk_chunk_get_block(chunk, lx, ly, lz);
                if (b != BLOCK_AIR && b != BLOCK_WATER) {
                    current_surface_y = ly;
                    break;
                }
            }
            
            if (current_surface_y < 0) continue;
            replacement_block = find_neighbor_surface_block(chunk, lx, lz, surface_block);
            
            if (current_surface_y < target_y) {
                for (ly = current_surface_y + 1; ly <= target_y && ly < CHUNK_HEIGHT; ++ly) {
                    sdk_chunk_set_block(chunk, lx, ly, lz, BLOCK_DIRT);
                }
            } else if (current_surface_y > target_y) {
                for (ly = target_y + 1; ly <= current_surface_y && ly < CHUNK_HEIGHT; ++ly) {
                    sdk_chunk_set_block(chunk, lx, ly, lz, BLOCK_AIR);
                }
            }

            mantle_start = target_y - 4;
            if (mantle_start < 0) mantle_start = 0;
            if (target_y >= 0 && target_y < CHUNK_HEIGHT) {
                for (ly = mantle_start; ly <= target_y && ly < CHUNK_HEIGHT; ++ly) {
                    sdk_chunk_set_block(chunk, lx, ly, lz, replacement_block);
                }
            }
        }
    }
}

void sdk_settlement_prepare_zone_terrain(SdkChunk* chunk, const BuildingZone* zone)
{
    int min_y, max_y, median_y, target_y;
    BlockType surface_block;
    
    if (!chunk || !zone || zone->terrain_modification == 0) return;
    
    surface_block = get_most_common_surface_block(chunk, zone);
    sample_zone_terrain(chunk, zone, &min_y, &max_y, &median_y);
    
    if (min_y >= CHUNK_HEIGHT || max_y <= 0) return;
    
    target_y = zone->base_elevation;
    if (target_y < 0 || target_y >= CHUNK_HEIGHT) {
        target_y = median_y;
    }
    
    if (abs(median_y - zone->base_elevation) <= 2) {
        target_y = median_y;
    } else if (median_y < zone->base_elevation) {
        if (zone->base_elevation - median_y <= 3) {
            target_y = zone->base_elevation;
        } else {
            target_y = median_y;
        }
    } else {
        if (median_y - zone->base_elevation <= 3) {
            target_y = zone->base_elevation;
        } else {
            target_y = median_y;
        }
    }
    
    flatten_zone_terrain(chunk, zone, target_y, surface_block);
}

