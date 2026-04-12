#include "sdk_worldgen_construction_cells_edge_config_resolver.h"
#include <string.h>

void sdk_edge_config_resolver_init_grid(SdkEdgeConfigVoxelGrid* grid)
{
    if (!grid) return;
    memset(grid, 0, sizeof(SdkEdgeConfigVoxelGrid));
}

static int resolve_voxel_height_at_position(const SdkTopologyEdgeConfig* config,
                                             int vx, int vy, int vz)
{
    float x_ratio = (float)vx / (float)(SDK_EDGE_RESOLVER_VOXEL_RESOLUTION - 1);
    float z_ratio = (float)vz / (float)(SDK_EDGE_RESOLVER_VOXEL_RESOLUTION - 1);
    
    float left_factor = 1.0f - x_ratio;
    float right_factor = x_ratio;
    float up_factor = 1.0f - z_ratio;
    float down_factor = z_ratio;
    
    float height_offset = 0.0f;
    
    height_offset += left_factor * (float)config->left_delta;
    height_offset += right_factor * (float)config->right_delta;
    height_offset += up_factor * (float)config->up_delta;
    height_offset += down_factor * (float)config->down_delta;
    
    height_offset *= 0.25f;
    
    int base_height = SDK_EDGE_RESOLVER_VOXEL_RESOLUTION / 2;
    int adjusted_height = base_height + (int)(height_offset * (float)SDK_EDGE_RESOLVER_VOXEL_RESOLUTION / 4.0f);
    
    if (adjusted_height < 0) adjusted_height = 0;
    if (adjusted_height >= SDK_EDGE_RESOLVER_VOXEL_RESOLUTION) adjusted_height = SDK_EDGE_RESOLVER_VOXEL_RESOLUTION - 1;
    
    return adjusted_height;
}

static BlockType resolve_voxel_material_at_position(const SdkTopologyEdgeConfig* config,
                                                     int vx, int vy, int vz)
{
    float x_ratio = (float)vx / (float)(SDK_EDGE_RESOLVER_VOXEL_RESOLUTION - 1);
    float z_ratio = (float)vz / (float)(SDK_EDGE_RESOLVER_VOXEL_RESOLUTION - 1);
    
    float center_weight = 1.0f;
    float left_weight = (1.0f - x_ratio) * 0.5f;
    float right_weight = x_ratio * 0.5f;
    float up_weight = (1.0f - z_ratio) * 0.5f;
    float down_weight = z_ratio * 0.5f;
    
    if (config->left_delta < 0) left_weight *= 0.5f;
    if (config->right_delta < 0) right_weight *= 0.5f;
    if (config->up_delta < 0) up_weight *= 0.5f;
    if (config->down_delta < 0) down_weight *= 0.5f;
    
    float max_weight = center_weight;
    BlockType selected = config->center_block;
    
    if (left_weight > max_weight) {
        max_weight = left_weight;
        selected = config->left_block;
    }
    if (right_weight > max_weight) {
        max_weight = right_weight;
        selected = config->right_block;
    }
    if (up_weight > max_weight) {
        max_weight = up_weight;
        selected = config->up_block;
    }
    if (down_weight > max_weight) {
        selected = config->down_block;
    }
    
    return selected;
}

void sdk_edge_config_resolver_fill_from_config(const SdkTopologyEdgeConfig* config,
                                                SdkEdgeConfigVoxelGrid* out_grid)
{
    int vx, vy, vz;
    
    if (!config || !out_grid) return;
    
    sdk_edge_config_resolver_init_grid(out_grid);
    
    for (vz = 0; vz < SDK_EDGE_RESOLVER_VOXEL_RESOLUTION; ++vz) {
        for (vx = 0; vx < SDK_EDGE_RESOLVER_VOXEL_RESOLUTION; ++vx) {
            int surface_height = resolve_voxel_height_at_position(config, vx, 0, vz);
            
            for (vy = 0; vy < SDK_EDGE_RESOLVER_VOXEL_RESOLUTION; ++vy) {
                if (vy <= surface_height) {
                    BlockType mat = resolve_voxel_material_at_position(config, vx, vy, vz);
                    out_grid->voxels[vx][vy][vz] = (uint16_t)mat;
                } else {
                    out_grid->voxels[vx][vy][vz] = (uint16_t)BLOCK_AIR;
                }
            }
        }
    }
}

void sdk_edge_config_resolver_fill_occupancy(const SdkEdgeConfigVoxelGrid* grid,
                                              uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    int vx, vy, vz;
    
    if (!grid || !occupancy) return;
    
    sdk_construction_clear_occupancy(occupancy);
    
    for (vz = 0; vz < SDK_EDGE_RESOLVER_VOXEL_RESOLUTION; ++vz) {
        for (vy = 0; vy < SDK_EDGE_RESOLVER_VOXEL_RESOLUTION; ++vy) {
            for (vx = 0; vx < SDK_EDGE_RESOLVER_VOXEL_RESOLUTION; ++vx) {
                if (grid->voxels[vx][vy][vz] != BLOCK_AIR) {
                    sdk_construction_occupancy_set(occupancy, vx, vy, vz, 1);
                }
            }
        }
    }
}

BlockType sdk_edge_config_resolver_dominant_material(const SdkTopologyEdgeConfig* config)
{
    if (!config) return BLOCK_AIR;
    return config->center_block;
}
