#ifndef NQLSDK_WORLDGEN_EDGE_CONFIG_RESOLVER_H
#define NQLSDK_WORLDGEN_EDGE_CONFIG_RESOLVER_H

#include <stdint.h>
#include "../../../Chunks/Topology/sdk_chunk_topology.h"
#include "../../../ConstructionCells/sdk_construction_cells.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_EDGE_RESOLVER_VOXEL_RESOLUTION 16

typedef struct SdkEdgeConfigVoxelGrid {
    uint16_t voxels[SDK_EDGE_RESOLVER_VOXEL_RESOLUTION]
                   [SDK_EDGE_RESOLVER_VOXEL_RESOLUTION]
                   [SDK_EDGE_RESOLVER_VOXEL_RESOLUTION];
} SdkEdgeConfigVoxelGrid;

void sdk_edge_config_resolver_init_grid(SdkEdgeConfigVoxelGrid* grid);
void sdk_edge_config_resolver_fill_from_config(const SdkTopologyEdgeConfig* config,
                                                SdkEdgeConfigVoxelGrid* out_grid);
void sdk_edge_config_resolver_fill_occupancy(const SdkEdgeConfigVoxelGrid* grid,
                                              uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT]);
BlockType sdk_edge_config_resolver_dominant_material(const SdkTopologyEdgeConfig* config);

#ifdef __cplusplus
}
#endif

#endif
