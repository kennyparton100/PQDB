/**
 * sdk_space_superchunk_terrain.h -- Attached terrain superchunk coordinate space.
 *
 * This space represents the classic superchunk grid where walls are "attached"
 * to terrain superchunks. Walls appear at chunk positions that are multiples
 * of (chunk_span + 1).
 *
 * Key characteristics:
 * - Period: chunk_span + 1 (e.g., 17 when chunk_span = 16)
 * - Offset: 0 (grid is anchored at world origin)
 * - Wall chunks are at local positions 0 within the period
 * - Terrain interior is positions 1..chunk_span within the period
 */

#ifndef NQLSDK_SPACE_SUPERCHUNK_TERRAIN_H
#define NQLSDK_SPACE_SUPERCHUNK_TERRAIN_H

#include "sdk_coordinate_space.h"
#include "../Chunks/sdk_chunk.h"
#include "../Superchunks/Geometry/sdk_superchunk_geometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Terrain Space Constants
 * ============================================================================ */

#define SDK_SPACE_TERRAIN_NAME "superchunk_terrain"
#define SDK_SPACE_TERRAIN_PIPELINE "terrain"

/* ============================================================================
 * Terrain Space Configuration
 * ============================================================================ */

/**
 * Get the terrain grid period from SDK configuration.
 * This is chunk_span + 1 for attached walls.
 */
static inline int sdk_space_terrain_get_period(void) {
    return sdk_superchunk_get_wall_period();  /* chunk_span + 1 */
}

/**
 * Get the terrain interior span (chunk_span).
 */
static inline int sdk_space_terrain_get_interior_span(void) {
    return sdk_superchunk_get_chunk_span();
}

/**
 * Get the terrain grid offset (always 0 for attached mode).
 */
static inline void sdk_space_terrain_get_offset(int* out_x, int* out_z) {
    if (out_x) *out_x = 0;
    if (out_z) *out_z = 0;
}

/* ============================================================================
 * Cell Structure (Space-Tagged)
 * ============================================================================ */

/**
 * Terrain cell with explicit space tagging.
 * Mirrors SdkSuperchunkCell but is self-contained for the coordinate space system.
 */
typedef struct SdkSpaceTerrainCell {
    int scx;                    /* Superchunk grid index X */
    int scz;                    /* Superchunk grid index Z */
    int period;                 /* Period in chunks (chunk_span + 1) */
    int chunk_span;             /* Interior span in chunks */
    
    /* Chunk coordinate bounds */
    int origin_cx;              /* West-north corner chunk (wall position) */
    int origin_cz;              /* West-north corner chunk (wall position) */
    int east_cx;                /* East wall chunk */
    int south_cz;               /* South wall chunk */
    int interior_min_cx;        /* First interior chunk X */
    int interior_max_cx;        /* Last interior chunk X */
    int interior_min_cz;        /* First interior chunk Z */
    int interior_max_cz;        /* Last interior chunk Z */
    
    /* Space type (always SDK_SPACE_SUPERCHUNK_TERRAIN) */
    SdkCoordinateSpaceType space;
} SdkSpaceTerrainCell;

/* ============================================================================
 * Cell Calculation
 * ============================================================================ */

/**
 * Calculate terrain cell from chunk coordinates.
 * This is the space-tagged equivalent of sdk_superchunk_cell_from_chunk().
 */
static inline void sdk_space_terrain_cell_from_chunk(int cx, int cz,
                                                      SdkSpaceTerrainCell* out_cell) {
    const int period = sdk_space_terrain_get_period();
    const int chunk_span = sdk_space_terrain_get_interior_span();
    const int scx = sdk_space_floor_div(cx, period);
    const int scz = sdk_space_floor_div(cz, period);
    
    if (!out_cell) return;
    
    out_cell->scx = scx;
    out_cell->scz = scz;
    out_cell->period = period;
    out_cell->chunk_span = chunk_span;
    out_cell->space = SDK_SPACE_SUPERCHUNK_TERRAIN;
    
    /* Wall positions are at origin (local 0) */
    out_cell->origin_cx = scx * period;
    out_cell->origin_cz = scz * period;
    
    /* East and south walls */
    out_cell->east_cx = out_cell->origin_cx + period;
    out_cell->south_cz = out_cell->origin_cz + period;
    
    /* Interior bounds */
    out_cell->interior_min_cx = out_cell->origin_cx + 1;
    out_cell->interior_max_cx = out_cell->origin_cx + chunk_span;
    out_cell->interior_min_cz = out_cell->origin_cz + 1;
    out_cell->interior_max_cz = out_cell->origin_cz + chunk_span;
}

/**
 * Calculate terrain cell from superchunk grid indices.
 */
static inline void sdk_space_terrain_cell_from_index(int scx, int scz,
                                                      SdkSpaceTerrainCell* out_cell) {
    if (!out_cell) return;
    out_cell->scx = scx;
    out_cell->scz = scz;
    out_cell->period = sdk_space_terrain_get_period();
    out_cell->chunk_span = sdk_space_terrain_get_interior_span();
    out_cell->space = SDK_SPACE_SUPERCHUNK_TERRAIN;
    out_cell->origin_cx = scx * out_cell->period;
    out_cell->origin_cz = scz * out_cell->period;
    out_cell->east_cx = out_cell->origin_cx + out_cell->period;
    out_cell->south_cz = out_cell->origin_cz + out_cell->period;
    out_cell->interior_min_cx = out_cell->origin_cx + 1;
    out_cell->interior_max_cx = out_cell->origin_cx + out_cell->chunk_span;
    out_cell->interior_min_cz = out_cell->origin_cz + 1;
    out_cell->interior_max_cz = out_cell->origin_cz + out_cell->chunk_span;
}

/* ============================================================================
 * Chunk Classification
 * ============================================================================ */

/**
 * Check if a chunk is a wall chunk in terrain space.
 * A chunk is a wall if its local position within the period is 0.
 */
static inline bool sdk_space_terrain_chunk_is_wall(int cx, int cz) {
    const int period = sdk_space_terrain_get_period();
    const int local_x = sdk_space_floor_mod(cx, period);
    const int local_z = sdk_space_floor_mod(cz, period);
    return local_x == 0 || local_z == 0;
}

/**
 * Check if a chunk is interior terrain (not a wall).
 */
static inline bool sdk_space_terrain_chunk_is_interior(int cx, int cz) {
    return !sdk_space_terrain_chunk_is_wall(cx, cz);
}

/**
 * Get the local position within the terrain period.
 */
static inline void sdk_space_terrain_chunk_local(int cx, int cz,
                                                    int* out_local_x, int* out_local_z) {
    const int period = sdk_space_terrain_get_period();
    if (out_local_x) *out_local_x = sdk_space_floor_mod(cx, period);
    if (out_local_z) *out_local_z = sdk_space_floor_mod(cz, period);
}

/**
 * Check which wall faces a chunk represents (if any).
 * Returns bitmask: bit 0 = west, bit 1 = north.
 */
static inline int sdk_space_terrain_chunk_wall_faces(int cx, int cz) {
    const int period = sdk_space_terrain_get_period();
    const int local_x = sdk_space_floor_mod(cx, period);
    const int local_z = sdk_space_floor_mod(cz, period);
    int faces = 0;
    if (local_x == 0) faces |= 1;  /* West wall */
    if (local_z == 0) faces |= 2;  /* North wall */
    return faces;
}

/* ============================================================================
 * Coordinate Conversions
 * ============================================================================ */

/**
 * Convert terrain cell to world block origin.
 */
static inline void sdk_space_terrain_cell_origin_to_world(const SdkSpaceTerrainCell* cell,
                                                           int* out_wx, int* out_wz) {
    if (!cell) return;
    if (out_wx) *out_wx = cell->origin_cx * CHUNK_WIDTH;
    if (out_wz) *out_wz = cell->origin_cz * CHUNK_DEPTH;
}

/**
 * Convert chunk coordinates to terrain-tagged coordinate.
 */
static inline SdkSpaceChunkCoord sdk_space_terrain_chunk_coord(int cx, int cz) {
    SdkSpaceChunkCoord coord;
    coord.cx = cx;
    coord.cz = cz;
    coord.space = SDK_SPACE_SUPERCHUNK_TERRAIN;
    return coord;
}

/**
 * Get the terrain cell that contains a given chunk.
 */
static inline SdkSpaceTerrainCell sdk_space_terrain_cell_containing_chunk(int cx, int cz) {
    SdkSpaceTerrainCell cell;
    sdk_space_terrain_cell_from_chunk(cx, cz, &cell);
    return cell;
}

/* ============================================================================
 * Space Definition Accessor
 * ============================================================================ */

/**
 * Get the terrain space definition (period, offset, etc. from current config).
 */
const SdkCoordinateSpace* sdk_space_terrain_definition(void);

/**
 * Initialize terrain space in the registry.
 */
void sdk_space_terrain_register(void);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SPACE_SUPERCHUNK_TERRAIN_H */
