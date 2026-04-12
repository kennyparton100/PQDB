/**
 * sdk_superchunk_config.c -- Runtime superchunk configuration implementation.
 */

#include "../Config/sdk_superchunk_config.h"
#include "../Geometry/sdk_superchunk_geometry.h"
#include "../../Chunks/sdk_chunk.h"
#include "../../Config/sdk_world_config.h"
#include "../../Walls/Config/sdk_world_walls_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Global configuration instance */
static SdkSuperchunkConfig g_superchunk_config = {
    .enabled = true,
    .chunk_span = 16,
    .coordinate_system = SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM,
    .walls_enabled = true,
    .wall_grid_size = 18,
    .wall_grid_offset_x = 0,
    .wall_grid_offset_z = 0,
    .wall_thickness_blocks = CHUNK_WIDTH,
    .wall_rings_shared = true
};
#define DEFAULT_GATE_WIDTH_BLOCKS 64

static int normalize_detached_wall_grid_size(int chunk_span, int wall_grid_size)
{
    const int default_size = chunk_span + 2;
    if (wall_grid_size <= 2) return default_size;
    if (wall_grid_size < default_size) return default_size;
    return wall_grid_size;
}

int sdk_world_coordinate_system_is_valid(SdkWorldCoordinateSystem coordinate_system)
{
    return coordinate_system == SDK_WORLD_COORDSYS_CHUNK_SYSTEM ||
           coordinate_system == SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM ||
           coordinate_system == SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
}

const char* sdk_world_coordinate_system_name(SdkWorldCoordinateSystem coordinate_system)
{
    switch (coordinate_system) {
        case SDK_WORLD_COORDSYS_CHUNK_SYSTEM:
            return "chunk_system";
        case SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM:
            return "superchunk_system";
        case SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM:
            return "grid_and_terrain_superchunk_system";
        default:
            return "chunk_system";
    }
}

const char* sdk_world_coordinate_system_display_name(SdkWorldCoordinateSystem coordinate_system)
{
    switch (coordinate_system) {
        case SDK_WORLD_COORDSYS_CHUNK_SYSTEM:
            return "Chunk System";
        case SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM:
            return "Superchunk System";
        case SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM:
            return "Grid & Terrain Superchunk System";
        default:
            return "Chunk System";
    }
}

int sdk_world_coordinate_system_uses_superchunks(SdkWorldCoordinateSystem coordinate_system)
{
    return coordinate_system == SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM ||
           coordinate_system == SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
}

int sdk_world_coordinate_system_detaches_walls(SdkWorldCoordinateSystem coordinate_system)
{
    return coordinate_system == SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
}

int sdk_superchunk_validate_chunk_span(int span)
{
    /* Must be power of 2, between 2 and 128 */
    if (span < 2 || span > 128) return 0;
    
    /* Check if power of 2 */
    return (span & (span - 1)) == 0;
}

void sdk_superchunk_normalize_config(SdkSuperchunkConfig* config)
{
    SdkWorldCoordinateSystem coordinate_system;

    if (!config) return;

    if (!sdk_superchunk_validate_chunk_span(config->chunk_span)) {
        fprintf(stderr, "Error: Invalid chunk span %d (must be power of 2, 2-128)\n", config->chunk_span);
        exit(1);
    }

    coordinate_system = (SdkWorldCoordinateSystem)config->coordinate_system;
    if (!sdk_world_coordinate_system_is_valid(coordinate_system)) {
        fprintf(stderr, "Error: Invalid coordinate system %d\n", coordinate_system);
        exit(1);
    }

    config->coordinate_system = (uint8_t)coordinate_system;
    config->enabled = sdk_world_coordinate_system_uses_superchunks(coordinate_system) ? true : false;
    config->walls_enabled = config->walls_enabled ? true : false;
    config->wall_rings_shared = config->wall_rings_shared ? true : false;
    if (config->wall_thickness_blocks <= 0) {
        config->wall_thickness_blocks = CHUNK_WIDTH;
    }
    if (config->wall_thickness_blocks > CHUNK_WIDTH) {
        config->wall_thickness_blocks = CHUNK_WIDTH;
    }

    if (config->walls_enabled && sdk_world_coordinate_system_detaches_walls(coordinate_system)) {
        config->wall_grid_size = normalize_detached_wall_grid_size(config->chunk_span, config->wall_grid_size);
    } else {
        config->wall_grid_size = config->chunk_span + 2;
        config->wall_grid_offset_x = 0;
        config->wall_grid_offset_z = 0;
        config->wall_rings_shared = true;
    }
}

const SdkSuperchunkConfig* sdk_superchunk_get_config(void)
{
    return &g_superchunk_config;
}

void sdk_superchunk_set_config(const SdkSuperchunkConfig* config)
{
    SdkSuperchunkConfig normalized;

    if (!config) return;

    normalized = *config;
    sdk_superchunk_normalize_config(&normalized);
    g_superchunk_config = normalized;
}

SdkWorldCoordinateSystem sdk_superchunk_get_coordinate_system(void)
{
    /* Delegate to world config - this is the authoritative source for coordinate system */
    SdkWorldCoordinateSystem world_coord = sdk_world_get_coordinate_system();
    if (sdk_world_coordinate_system_is_valid(world_coord)) {
        return world_coord;
    }
    /* Fallback to local config if world config not initialized */
    return (SdkWorldCoordinateSystem)g_superchunk_config.coordinate_system;
}

void sdk_superchunk_set_coordinate_system(SdkWorldCoordinateSystem coordinate_system)
{
    g_superchunk_config.coordinate_system = (uint8_t)coordinate_system;
    sdk_superchunk_normalize_config(&g_superchunk_config);
}

int sdk_superchunk_get_chunk_span(void)
{
    return g_superchunk_config.chunk_span > 0 ? g_superchunk_config.chunk_span : 16;
}

int sdk_superchunk_get_block_span(void)
{
    return sdk_superchunk_get_chunk_span() * CHUNK_WIDTH;
}

int sdk_superchunk_get_wall_period(void)
{
    return sdk_superchunk_get_chunk_span() + 1;
}

int sdk_superchunk_get_wall_grid_period(void)
{
    if (sdk_world_walls_config_is_initialized()) {
        return sdk_world_walls_get_period();
    }
    if (g_superchunk_config.walls_enabled &&
        sdk_world_coordinate_system_detaches_walls(
            (SdkWorldCoordinateSystem)g_superchunk_config.coordinate_system)) {
        int size = normalize_detached_wall_grid_size(g_superchunk_config.chunk_span,
                                                     g_superchunk_config.wall_grid_size);
        return size - (g_superchunk_config.wall_rings_shared ? 1 : 0);
    }
    return sdk_superchunk_get_wall_period();
}

int sdk_superchunk_get_wall_grid_interior_span(void)
{
    if (sdk_world_walls_config_is_initialized()) {
        return sdk_world_walls_get_interior_span_chunks();
    }
    if (g_superchunk_config.walls_enabled &&
        sdk_world_coordinate_system_detaches_walls(
            (SdkWorldCoordinateSystem)g_superchunk_config.coordinate_system)) {
        return normalize_detached_wall_grid_size(g_superchunk_config.chunk_span,
                                                 g_superchunk_config.wall_grid_size) - 2;
    }
    return sdk_superchunk_get_chunk_span();
}

int sdk_superchunk_get_wall_thickness_chunks(void)
{
    if (sdk_world_walls_config_is_initialized()) {
        return sdk_world_walls_get_thickness_chunks();
    }
    return (g_superchunk_config.wall_thickness_blocks + CHUNK_WIDTH - 1) / CHUNK_WIDTH;
}

int sdk_superchunk_get_wall_thickness_blocks(void)
{
    if (sdk_world_walls_config_is_initialized()) {
        return sdk_world_walls_get_thickness_blocks();
    }
    return g_superchunk_config.wall_thickness_blocks > 0
        ? g_superchunk_config.wall_thickness_blocks
        : CHUNK_WIDTH;
}

int sdk_superchunk_get_gate_start_block(void)
{
    /* Center the gate in the superchunk */
    int block_span = sdk_superchunk_get_block_span();
    int gate_width = DEFAULT_GATE_WIDTH_BLOCKS;
    return (block_span - gate_width) / 2;
}

int sdk_superchunk_get_gate_end_block(void)
{
    return sdk_superchunk_get_gate_start_block() + DEFAULT_GATE_WIDTH_BLOCKS - 1;
}

int sdk_superchunk_get_gate_width_blocks(void)
{
    return DEFAULT_GATE_WIDTH_BLOCKS;
}

int sdk_superchunk_get_gate_support_width_chunks(void)
{
    return 4; /* Fixed support width for now */
}

int sdk_superchunk_get_gate_support_start_chunk(void)
{
    int chunk_span = sdk_superchunk_get_chunk_span();
    int support_width = sdk_superchunk_get_gate_support_width_chunks();
    return (chunk_span - support_width) / 2;
}

int sdk_superchunk_get_gate_support_end_chunk(void)
{
    return sdk_superchunk_get_gate_support_start_chunk() + 
           sdk_superchunk_get_gate_support_width_chunks() - 1;
}

int sdk_superchunk_get_walls_enabled(void)
{
    return g_superchunk_config.walls_enabled ? 1 : 0;
}

void sdk_superchunk_set_walls_enabled(int enabled)
{
    g_superchunk_config.walls_enabled = (enabled != 0);
}

int sdk_superchunk_get_enabled(void)
{
    return sdk_world_coordinate_system_uses_superchunks(
        (SdkWorldCoordinateSystem)g_superchunk_config.coordinate_system) ? 1 : 0;
}

int sdk_superchunk_get_wall_grid_size(void)
{
    return g_superchunk_config.wall_grid_size;
}

void sdk_superchunk_set_wall_grid_size(int size)
{
    g_superchunk_config.wall_grid_size = size;
}

int sdk_superchunk_get_wall_grid_offset_x(void)
{
    return g_superchunk_config.wall_grid_offset_x;
}

void sdk_superchunk_set_wall_grid_offset_x(int offset)
{
    g_superchunk_config.wall_grid_offset_x = offset;
}

int sdk_superchunk_get_wall_grid_offset_z(void)
{
    return g_superchunk_config.wall_grid_offset_z;
}

void sdk_superchunk_set_wall_grid_offset_z(int offset)
{
    g_superchunk_config.wall_grid_offset_z = offset;
}
