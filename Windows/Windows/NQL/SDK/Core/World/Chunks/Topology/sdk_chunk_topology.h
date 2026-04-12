#ifndef NQLSDK_CHUNK_TOPOLOGY_H
#define NQLSDK_CHUNK_TOPOLOGY_H

#include <stdint.h>
#include "../sdk_chunk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_CHUNK_TOPOLOGY_EDGE_UP     0x01
#define SDK_CHUNK_TOPOLOGY_EDGE_DOWN   0x02
#define SDK_CHUNK_TOPOLOGY_EDGE_LEFT   0x04
#define SDK_CHUNK_TOPOLOGY_EDGE_RIGHT  0x08
#define SDK_CHUNK_TOPOLOGY_IS_EDGE     0x10

typedef struct SdkChunkTopologyMap {
    uint16_t surface_heights[CHUNK_WIDTH][CHUNK_DEPTH];
    uint8_t  edge_flags[CHUNK_WIDTH][CHUNK_DEPTH];
    BlockType surface_blocks[CHUNK_WIDTH][CHUNK_DEPTH];
    int16_t  min_height;
    int16_t  max_height;
} SdkChunkTopologyMap;

typedef struct SdkTopologyEdgeConfig {
    BlockType center_block;
    BlockType up_block;
    BlockType down_block;
    BlockType left_block;
    BlockType right_block;
    int8_t up_delta;
    int8_t down_delta;
    int8_t left_delta;
    int8_t right_delta;
} SdkTopologyEdgeConfig;

typedef struct SdkTopologyUniqueEdges {
    SdkTopologyEdgeConfig* configs;
    uint32_t count;
    uint32_t capacity;
} SdkTopologyUniqueEdges;

void sdk_chunk_topology_analyze(const SdkChunk* chunk, SdkChunkTopologyMap* out_map);
int sdk_chunk_topology_is_edge(const SdkChunkTopologyMap* map, int lx, int lz);
int sdk_chunk_topology_height_at(const SdkChunkTopologyMap* map, int lx, int lz);
void sdk_chunk_topology_get_height_range(const SdkChunkTopologyMap* map, int16_t* out_min, int16_t* out_max);

uint32_t sdk_chunk_topology_count_edge_blocks(const SdkChunkTopologyMap* map);
void sdk_chunk_topology_find_unique_edges(const SdkChunkTopologyMap* map, SdkTopologyUniqueEdges* out_edges);
void sdk_chunk_topology_unique_edges_free(SdkTopologyUniqueEdges* edges);
int sdk_chunk_topology_edge_config_equals(const SdkTopologyEdgeConfig* a, const SdkTopologyEdgeConfig* b);

#ifdef __cplusplus
}
#endif

#endif
