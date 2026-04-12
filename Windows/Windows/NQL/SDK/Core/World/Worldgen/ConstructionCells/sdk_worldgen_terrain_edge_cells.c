#include "sdk_worldgen_terrain_edge_cells.h"
#include "../../Chunks/Topology/sdk_chunk_topology.h"
#include "EdgeConfigResolver/sdk_worldgen_construction_cells_edge_config_resolver.h"
#include "../../ConstructionCells/sdk_construction_cells.h"
#include "../Internal/sdk_worldgen_internal.h"
#include <string.h>

static int find_unique_config_index(const SdkTopologyUniqueEdges* unique_edges,
                                    const SdkTopologyEdgeConfig* config)
{
    uint32_t i;
    if (!unique_edges || !config) return -1;
    
    for (i = 0; i < unique_edges->count; ++i) {
        if (sdk_chunk_topology_edge_config_equals(&unique_edges->configs[i], config)) {
            return (int)i;
        }
    }
    return -1;
}

static void build_edge_config_for_position(const SdkChunkTopologyMap* map,
                                           int lx, int lz,
                                           SdkTopologyEdgeConfig* out_config)
{
    int current_h, up_h, down_h, left_h, right_h;
    
    if (!map || !out_config) return;
    
    memset(out_config, 0, sizeof(SdkTopologyEdgeConfig));
    
    current_h = sdk_chunk_topology_height_at(map, lx, lz);
    up_h = (lz > 0) ? sdk_chunk_topology_height_at(map, lx, lz - 1) : current_h;
    down_h = (lz < CHUNK_DEPTH - 1) ? sdk_chunk_topology_height_at(map, lx, lz + 1) : current_h;
    left_h = (lx > 0) ? sdk_chunk_topology_height_at(map, lx - 1, lz) : current_h;
    right_h = (lx < CHUNK_WIDTH - 1) ? sdk_chunk_topology_height_at(map, lx + 1, lz) : current_h;
    
    out_config->center_block = map->surface_blocks[lx][lz];
    out_config->up_block = (lz > 0) ? map->surface_blocks[lx][lz - 1] : out_config->center_block;
    out_config->down_block = (lz < CHUNK_DEPTH - 1) ? map->surface_blocks[lx][lz + 1] : out_config->center_block;
    out_config->left_block = (lx > 0) ? map->surface_blocks[lx - 1][lz] : out_config->center_block;
    out_config->right_block = (lx < CHUNK_WIDTH - 1) ? map->surface_blocks[lx + 1][lz] : out_config->center_block;
    
    out_config->up_delta = (int8_t)(up_h - current_h);
    out_config->down_delta = (int8_t)(down_h - current_h);
    out_config->left_delta = (int8_t)(left_h - current_h);
    out_config->right_delta = (int8_t)(right_h - current_h);
}

void generate_terrain_edge_cells(SdkWorldGen* wg, SdkChunk* chunk)
{
    SdkChunkTopologyMap topology_map;
    SdkTopologyUniqueEdges unique_edges;
    SdkTopologyEdgeConfig edge_config;
    SdkEdgeConfigVoxelGrid voxel_grid;
    uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    int lx, lz;
    int surface_y;
    BlockType dominant_material;
    
    (void)wg;
    if (!chunk) return;
    if (!chunk->construction_registry) return;
    
    sdk_chunk_topology_analyze(chunk, &topology_map);
    
    uint32_t edge_count = sdk_chunk_topology_count_edge_blocks(&topology_map);
    if (edge_count == 0) return;
    
    sdk_chunk_topology_find_unique_edges(&topology_map, &unique_edges);
    
    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            if (!sdk_chunk_topology_is_edge(&topology_map, lx, lz)) {
                continue;
            }
            
            surface_y = sdk_chunk_topology_height_at(&topology_map, lx, lz);
            if (surface_y < 0 || surface_y >= CHUNK_HEIGHT) continue;
            
            /* Replace the existing terrain block with a construction cell.
             * Note: We intentionally don't check for occupancy here because
             * terrain edge cells are meant to replace existing terrain blocks.
             * The surface positions were chosen specifically because blocks exist there. */
            BlockType existing_block = sdk_chunk_get_block(chunk, lx, surface_y, lz);
            if (existing_block != BLOCK_AIR) {
                sdk_chunk_set_block(chunk, lx, surface_y, lz, BLOCK_AIR);
            }
            
            build_edge_config_for_position(&topology_map, lx, lz, &edge_config);
            
            if (find_unique_config_index(&unique_edges, &edge_config) < 0) {
                continue;
            }
            
            sdk_edge_config_resolver_fill_from_config(&edge_config, &voxel_grid);
            sdk_edge_config_resolver_fill_occupancy(&voxel_grid, occupancy);
            dominant_material = sdk_edge_config_resolver_dominant_material(&edge_config);
            
            sdk_construction_chunk_set_cell_payload(chunk, lx, surface_y, lz,
                                                    dominant_material, occupancy);
        }
    }

    sdk_chunk_topology_unique_edges_free(&unique_edges);
}
