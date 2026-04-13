/**
 * sdk_space_grid_box.h -- Grid box coordinate space for wall grid terrain system.
 *
 * This space represents grid boxes that can contain 1x1, 2x2, or NxN superchunks.
 * Walls appear at chunk positions that are multiples of the grid box period.
 *
 * Key characteristics:
 * - Period: interior_span + wall_chunk_span (e.g., 17 when interior_span = 16, wall_chunk_span = 1)
 * - Offset: 0 (grid is anchored at world origin)
 * - Wall chunks are at local positions 0 within the period
 * - Grid box interior is positions 1..interior_span within the period
 */

#ifndef NQLSDK_SPACE_GRID_BOX_H
#define NQLSDK_SPACE_GRID_BOX_H

#include "../../CoordinateSpaces/sdk_coordinate_space.h"
#include "../../Chunks/sdk_chunk.h"
#include "../../Superchunks/Config/sdk_superchunk_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Grid Box Space Constants
 * ============================================================================ */

#define SDK_SPACE_GRID_BOX_NAME "grid_box"
#define SDK_SPACE_GRID_BOX_PIPELINE "grid_box"
#define SDK_SPACE_GRID_BOX_SUPERCHUNK_SPAN 1
#define SDK_SPACE_WALL_GRID_WALL_CHUNK_SPAN 1

/* ============================================================================
 * Grid Box Space Configuration
 * ============================================================================ */

/**
 * Get the grid box interior span (chunk_span * superchunk_span).
 */
static inline int sdk_space_grid_box_get_interior_span(void) {
    return sdk_superchunk_get_chunk_span() * SDK_SPACE_GRID_BOX_SUPERCHUNK_SPAN;
}

/**
 * Get the grid box period from SDK configuration.
 */
static inline int sdk_space_grid_box_get_period(void) {
    return sdk_space_grid_box_get_interior_span() + SDK_SPACE_WALL_GRID_WALL_CHUNK_SPAN;
}

/**
 * Get the grid box offset (always 0 for attached mode).
 */
static inline void sdk_space_grid_box_get_offset(int* out_x, int* out_z) {
    if (out_x) *out_x = 0;
    if (out_z) *out_z = 0;
}

/* ============================================================================
 * Grid Box Structure (Space-Tagged)
 * ============================================================================ */

/**
 * Grid box with explicit space tagging.
 * Mirrors SdkSuperchunkCell but is self-contained for the coordinate space system.
 */
typedef struct SdkSpaceGridBox {
    int grid_x;                 /* Grid box index X */
    int grid_z;                 /* Grid box index Z */
    int period;                 /* Period in chunks (interior_span + wall_chunk_span) */
    int interior_span;          /* Interior span in chunks */
    
    /* Chunk coordinate bounds */
    int origin_cx;              /* West-north corner chunk (wall position) */
    int origin_cz;              /* West-north corner chunk (wall position) */
    int east_cx;                /* East wall chunk */
    int south_cz;               /* South wall chunk */
    int interior_min_cx;        /* First interior chunk X */
    int interior_max_cx;        /* Last interior chunk X */
    int interior_min_cz;        /* First interior chunk Z */
    int interior_max_cz;        /* Last interior chunk Z */
    
    /* Space type */
    SdkCoordinateSpaceType space;
} SdkSpaceGridBox;

/* ============================================================================
 * Grid Box Calculation
 * ============================================================================ */

/**
 * Calculate grid box from chunk coordinates.
 */
static inline void sdk_space_grid_box_from_chunk(int cx, int cz,
                                                  SdkSpaceGridBox* out_box) {
    const int period = sdk_space_grid_box_get_period();
    const int interior_span = sdk_space_grid_box_get_interior_span();
    const int grid_x = sdk_space_floor_div(cx, period);
    const int grid_z = sdk_space_floor_div(cz, period);
    
    if (!out_box) return;
    
    out_box->grid_x = grid_x;
    out_box->grid_z = grid_z;
    out_box->period = period;
    out_box->interior_span = interior_span;
    
    /* Wall positions are at origin (local 0) */
    out_box->origin_cx = grid_x * period;
    out_box->origin_cz = grid_z * period;
    
    /* East and south walls */
    out_box->east_cx = out_box->origin_cx + period;
    out_box->south_cz = out_box->origin_cz + period;
    
    /* Interior bounds */
    out_box->interior_min_cx = out_box->origin_cx + 1;
    out_box->interior_max_cx = out_box->origin_cx + interior_span;
    out_box->interior_min_cz = out_box->origin_cz + 1;
    out_box->interior_max_cz = out_box->origin_cz + interior_span;
}

/**
 * Calculate grid box from grid box indices.
 */
static inline void sdk_space_grid_box_from_index(int grid_x, int grid_z,
                                                  SdkSpaceGridBox* out_box) {
    if (!out_box) return;
    out_box->grid_x = grid_x;
    out_box->grid_z = grid_z;
    out_box->period = sdk_space_grid_box_get_period();
    out_box->interior_span = sdk_space_grid_box_get_interior_span();
    out_box->origin_cx = grid_x * out_box->period;
    out_box->origin_cz = grid_z * out_box->period;
    out_box->east_cx = out_box->origin_cx + out_box->period;
    out_box->south_cz = out_box->origin_cz + out_box->period;
    out_box->interior_min_cx = out_box->origin_cx + 1;
    out_box->interior_max_cx = out_box->origin_cx + out_box->interior_span;
    out_box->interior_min_cz = out_box->origin_cz + 1;
    out_box->interior_max_cz = out_box->origin_cz + out_box->interior_span;
}

/* ============================================================================
 * Chunk Classification
 * ============================================================================ */

/**
 * Check if a chunk is a wall chunk in grid box space.
 * A chunk is a wall if its local position within the period is 0.
 */
static inline bool sdk_space_grid_box_chunk_is_wall(int cx, int cz) {
    const int period = sdk_space_grid_box_get_period();
    const int local_x = sdk_space_floor_mod(cx, period);
    const int local_z = sdk_space_floor_mod(cz, period);
    return local_x == 0 || local_z == 0;
}

/**
 * Check if a chunk is interior (not a wall).
 */
static inline bool sdk_space_grid_box_chunk_is_interior(int cx, int cz) {
    return !sdk_space_grid_box_chunk_is_wall(cx, cz);
}

/**
 * Get the local position within the grid box period.
 */
static inline void sdk_space_grid_box_chunk_local(int cx, int cz,
                                                  int* out_local_x, int* out_local_z) {
    const int period = sdk_space_grid_box_get_period();
    if (out_local_x) *out_local_x = sdk_space_floor_mod(cx, period);
    if (out_local_z) *out_local_z = sdk_space_floor_mod(cz, period);
}

/**
 * Check which wall faces a chunk represents (if any).
 * Returns bitmask: bit 0 = west, bit 1 = north.
 */
static inline int sdk_space_grid_box_chunk_wall_faces(int cx, int cz) {
    const int period = sdk_space_grid_box_get_period();
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
 * Get the grid box origin for given superchunk indices.
 * This function accounts for wall grid offsets when wall grid space is active.
 * 
 * @param scx Superchunk grid index X
 * @param scz Superchunk grid index Z
 * @param out_origin_cx Output: global chunk coordinate of northwest corner
 * @param out_origin_cz Output: global chunk coordinate of northwest corner
 */
static inline void sdk_space_grid_box_get_origin(int scx, int scz, 
                                                 int* out_origin_cx, int* out_origin_cz) {
    int period;
    int offset_x = 0;
    int offset_z = 0;
    int grid_box_x, grid_box_z;
    
    if (sdk_world_walls_uses_grid_space()) {
        period = sdk_world_walls_get_period();
        sdk_world_walls_get_effective_offset(&offset_x, &offset_z);
        /* Convert superchunk index to grid box index */
        grid_box_x = sdk_world_walls_get_box_index(scx * sdk_space_grid_box_get_period());
        grid_box_z = sdk_world_walls_get_box_index(scz * sdk_space_grid_box_get_period());
    } else {
        period = sdk_space_grid_box_get_period();
        sdk_space_grid_box_get_offset(&offset_x, &offset_z);
        grid_box_x = scx;
        grid_box_z = scz;
    }
    
    if (out_origin_cx) *out_origin_cx = grid_box_x * period + offset_x;
    if (out_origin_cz) *out_origin_cz = grid_box_z * period + offset_z;
}

/**
 * Convert grid box to world block origin.
 */
static inline void sdk_space_grid_box_origin_to_world(const SdkSpaceGridBox* box,
                                                      int* out_wx, int* out_wz) {
    if (!box) return;
    if (out_wx) *out_wx = box->origin_cx * CHUNK_WIDTH;
    if (out_wz) *out_wz = box->origin_cz * CHUNK_DEPTH;
}

/**
 * Convert chunk coordinates to grid box coordinate.
 */
static inline SdkSpaceChunkCoord sdk_space_grid_box_chunk_coord(int cx, int cz) {
    SdkSpaceChunkCoord coord = {0};
    coord.cx = cx;
    coord.cz = cz;
    return coord;
}

/**
 * Get the grid box that contains a given chunk.
 */
static inline SdkSpaceGridBox sdk_space_grid_box_containing_chunk(int cx, int cz) {
    SdkSpaceGridBox box;
    sdk_space_grid_box_from_chunk(cx, cz, &box);
    return box;
}

/* ============================================================================
 * Space Definition Accessor
 * ============================================================================ */

/**
 * Get the grid box space definition (period, offset, etc. from current config).
 */
const SdkCoordinateSpace* sdk_space_grid_box_definition(void);

/**
 * Initialize grid box space in the registry.
 */
void sdk_space_grid_box_register(void);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SPACE_GRID_BOX_H */
