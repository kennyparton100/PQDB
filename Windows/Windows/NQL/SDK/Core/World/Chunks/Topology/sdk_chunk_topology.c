#include "sdk_chunk_topology.h"
#include "../../ConstructionCells/sdk_construction_cells.h"
#include <string.h>
#include <stdlib.h>

static int is_solid_surface_material(BlockType block)
{
    return block != BLOCK_AIR && block != BLOCK_WATER && block != BLOCK_ICE && 
           block != BLOCK_SEA_ICE && block != BLOCK_LAVA;
}

static BlockType get_cell_surface_material(const SdkChunk* chunk, int lx, int ly, int lz)
{
    BlockType block = sdk_chunk_get_block(chunk, lx, ly, lz);
    if (block != BLOCK_AIR) {
        return block;
    }
    return sdk_construction_chunk_get_display_material(chunk, lx, ly, lz);
}

static int find_surface_y(const SdkChunk* chunk, int lx, int lz)
{
    int ly;
    for (ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
        BlockType block = get_cell_surface_material(chunk, lx, ly, lz);
        if (is_solid_surface_material(block)) {
            return ly;
        }
    }
    return -1;
}

static BlockType get_surface_block(const SdkChunk* chunk, int lx, int lz, int surface_y)
{
    if (surface_y < 0 || surface_y >= CHUNK_HEIGHT) return BLOCK_AIR;
    return get_cell_surface_material(chunk, lx, surface_y, lz);
}

void sdk_chunk_topology_analyze(const SdkChunk* chunk, SdkChunkTopologyMap* out_map)
{
    int lx, lz;
    int16_t min_h = CHUNK_HEIGHT;
    int16_t max_h = -1;

    if (!chunk || !out_map) return;

    memset(out_map, 0, sizeof(SdkChunkTopologyMap));

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int surface_y = find_surface_y(chunk, lx, lz);
            out_map->surface_heights[lx][lz] = (uint16_t)(surface_y >= 0 ? surface_y : 0);
            out_map->surface_blocks[lx][lz] = get_surface_block(chunk, lx, lz, surface_y);
            
            if (surface_y >= 0) {
                if (surface_y < min_h) min_h = (int16_t)surface_y;
                if (surface_y > max_h) max_h = (int16_t)surface_y;
            }
        }
    }

    out_map->min_height = min_h;
    out_map->max_height = max_h;

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int current_h = (int)out_map->surface_heights[lx][lz];
            uint8_t flags = 0;

            int up_h = (lz > 0) ? (int)out_map->surface_heights[lx][lz - 1] : current_h;
            int down_h = (lz < CHUNK_DEPTH - 1) ? (int)out_map->surface_heights[lx][lz + 1] : current_h;
            int left_h = (lx > 0) ? (int)out_map->surface_heights[lx - 1][lz] : current_h;
            int right_h = (lx < CHUNK_WIDTH - 1) ? (int)out_map->surface_heights[lx + 1][lz] : current_h;

            if (up_h != current_h) {
                flags |= SDK_CHUNK_TOPOLOGY_EDGE_UP;
                flags |= SDK_CHUNK_TOPOLOGY_IS_EDGE;
            }
            if (down_h != current_h) {
                flags |= SDK_CHUNK_TOPOLOGY_EDGE_DOWN;
                flags |= SDK_CHUNK_TOPOLOGY_IS_EDGE;
            }
            if (left_h != current_h) {
                flags |= SDK_CHUNK_TOPOLOGY_EDGE_LEFT;
                flags |= SDK_CHUNK_TOPOLOGY_IS_EDGE;
            }
            if (right_h != current_h) {
                flags |= SDK_CHUNK_TOPOLOGY_EDGE_RIGHT;
                flags |= SDK_CHUNK_TOPOLOGY_IS_EDGE;
            }

            out_map->edge_flags[lx][lz] = flags;
        }
    }
}

int sdk_chunk_topology_is_edge(const SdkChunkTopologyMap* map, int lx, int lz)
{
    if (!map) return 0;
    if (lx < 0 || lx >= CHUNK_WIDTH) return 0;
    if (lz < 0 || lz >= CHUNK_DEPTH) return 0;
    return (map->edge_flags[lx][lz] & SDK_CHUNK_TOPOLOGY_IS_EDGE) != 0;
}

int sdk_chunk_topology_height_at(const SdkChunkTopologyMap* map, int lx, int lz)
{
    if (!map) return 0;
    if (lx < 0 || lx >= CHUNK_WIDTH) return -1;
    if (lz < 0 || lz >= CHUNK_DEPTH) return -1;
    return (int)map->surface_heights[lx][lz];
}

void sdk_chunk_topology_get_height_range(const SdkChunkTopologyMap* map, int16_t* out_min, int16_t* out_max)
{
    if (!map) {
        if (out_min) *out_min = 0;
        if (out_max) *out_max = 0;
        return;
    }
    if (out_min) *out_min = map->min_height;
    if (out_max) *out_max = map->max_height;
}

uint32_t sdk_chunk_topology_count_edge_blocks(const SdkChunkTopologyMap* map)
{
    uint32_t count = 0;
    int lx, lz;

    if (!map) return 0;

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            if (map->edge_flags[lx][lz] & SDK_CHUNK_TOPOLOGY_IS_EDGE) {
                count++;
            }
        }
    }
    return count;
}

int sdk_chunk_topology_edge_config_equals(const SdkTopologyEdgeConfig* a, const SdkTopologyEdgeConfig* b)
{
    if (!a || !b) return 0;
    return (a->center_block == b->center_block &&
            a->up_block == b->up_block &&
            a->down_block == b->down_block &&
            a->left_block == b->left_block &&
            a->right_block == b->right_block &&
            a->up_delta == b->up_delta &&
            a->down_delta == b->down_delta &&
            a->left_delta == b->left_delta &&
            a->right_delta == b->right_delta);
}

static int unique_edges_find_or_add(SdkTopologyUniqueEdges* edges, const SdkTopologyEdgeConfig* config)
{
    uint32_t i;
    SdkTopologyEdgeConfig* grown;

    if (!edges || !config) return -1;

    for (i = 0; i < edges->count; ++i) {
        if (sdk_chunk_topology_edge_config_equals(&edges->configs[i], config)) {
            return (int)i;
        }
    }

    if (edges->count >= edges->capacity) {
        uint32_t new_cap = edges->capacity ? edges->capacity * 2 : 16;
        grown = (SdkTopologyEdgeConfig*)realloc(edges->configs, new_cap * sizeof(SdkTopologyEdgeConfig));
        if (!grown) return -1;
        edges->configs = grown;
        edges->capacity = new_cap;
    }

    edges->configs[edges->count] = *config;
    edges->count++;
    return (int)(edges->count - 1);
}

void sdk_chunk_topology_find_unique_edges(const SdkChunkTopologyMap* map, SdkTopologyUniqueEdges* out_edges)
{
    int lx, lz;
    SdkTopologyEdgeConfig config;

    if (!map || !out_edges) return;

    memset(out_edges, 0, sizeof(SdkTopologyUniqueEdges));

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            if (!(map->edge_flags[lx][lz] & SDK_CHUNK_TOPOLOGY_IS_EDGE)) {
                continue;
            }

            int current_h = (int)map->surface_heights[lx][lz];

            int up_h = (lz > 0) ? (int)map->surface_heights[lx][lz - 1] : current_h;
            int down_h = (lz < CHUNK_DEPTH - 1) ? (int)map->surface_heights[lx][lz + 1] : current_h;
            int left_h = (lx > 0) ? (int)map->surface_heights[lx - 1][lz] : current_h;
            int right_h = (lx < CHUNK_WIDTH - 1) ? (int)map->surface_heights[lx + 1][lz] : current_h;

            config.center_block = map->surface_blocks[lx][lz];
            config.up_delta = (int8_t)(up_h - current_h);
            config.down_delta = (int8_t)(down_h - current_h);
            config.left_delta = (int8_t)(left_h - current_h);
            config.right_delta = (int8_t)(right_h - current_h);

            config.up_block = (lz > 0) ? map->surface_blocks[lx][lz - 1] : BLOCK_AIR;
            config.down_block = (lz < CHUNK_DEPTH - 1) ? map->surface_blocks[lx][lz + 1] : BLOCK_AIR;
            config.left_block = (lx > 0) ? map->surface_blocks[lx - 1][lz] : BLOCK_AIR;
            config.right_block = (lx < CHUNK_WIDTH - 1) ? map->surface_blocks[lx + 1][lz] : BLOCK_AIR;

            unique_edges_find_or_add(out_edges, &config);
        }
    }
}

void sdk_chunk_topology_unique_edges_free(SdkTopologyUniqueEdges* edges)
{
    if (!edges) return;
    free(edges->configs);
    edges->configs = NULL;
    edges->count = 0;
    edges->capacity = 0;
}
