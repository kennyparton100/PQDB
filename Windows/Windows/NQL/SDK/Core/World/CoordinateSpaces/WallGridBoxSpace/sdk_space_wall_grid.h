/**
 * sdk_space_wall_grid.h -- Wall-grid coordinate-space helpers.
 *
 * This space owns wall-grid classification and cell math for worlds using the
 * Grid & Terrain Superchunk coordinate system. The canonical world position is
 * still the global chunk coordinate; this layer derives the wall-grid cell and
 * local coordinates from that global position plus the active wall settings.
 */

#ifndef NQLSDK_SPACE_WALL_GRID_H
#define NQLSDK_SPACE_WALL_GRID_H

#include "../sdk_coordinate_space.h"
#include "../../Chunks/sdk_chunk.h"
#include "../../Walls/Config/sdk_world_walls_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Wall Grid Space Constants
 * ============================================================================ */

#define SDK_SPACE_WALL_GRID_NAME "wall_grid"
#define SDK_SPACE_WALL_GRID_PIPELINE "wall"

/* ============================================================================
 * Wall Grid Configuration
 * ============================================================================ */

/**
 * Get the live wall-grid period from the active wall configuration.
 */
static inline int sdk_space_wall_grid_get_period(void) {
    return sdk_world_walls_get_period();
}

/**
 * Get the live wall-grid interior span (the space between wall boundaries).
 */
static inline int sdk_space_wall_grid_get_interior_span(void) {
    return sdk_world_walls_get_interior_span_chunks();
}

/**
 * Get the wall grid offsets.
 * These shift the entire wall grid relative to the world origin.
 */
static inline void sdk_space_wall_grid_get_offset(int* out_x, int* out_z) {
    sdk_world_walls_get_offset(out_x, out_z);
}

/**
 * Check whether chunks use the dedicated wall-grid space for classification.
 */
static inline bool sdk_space_wall_grid_is_detached(void) {
    return sdk_world_walls_uses_grid_space();
}

/**
 * Get the effective wall-grid offset used for classification.
 */
static inline void sdk_space_wall_grid_get_effective_offset(int* out_x, int* out_z) {
    sdk_world_walls_get_effective_offset(out_x, out_z);
}

/* ============================================================================
 * Cell Structure (Space-Tagged)
 * ============================================================================ */

/**
 * Wall grid cell with explicit space tagging.
 * Mirrors SdkSuperchunkWallGridCell but is self-contained for the coordinate space system.
 */
typedef struct SdkSpaceWallGridCell {
    int grid_x;                 /* Wall grid index X */
    int grid_z;                 /* Wall grid index Z */
    int period;                 /* Live wall-grid period in chunks */
    
    /* Chunk coordinate bounds */
    int origin_cx;              /* West-north wall chunk */
    int origin_cz;              /* West-north wall chunk */
    int east_cx;                /* East wall chunk */
    int south_cz;               /* South wall chunk */
    int interior;               /* Interior chunks per dimension */
    
    /* Offset tracking */
    int offset_x;               /* Applied X offset */
    int offset_z;               /* Applied Z offset */
    
    /* Space type (always SDK_SPACE_WALL_GRID) */
    SdkCoordinateSpaceType space;
} SdkSpaceWallGridCell;

/* ============================================================================
 * Cell Calculation
 * ============================================================================ */

/**
 * Calculate wall grid cell from chunk coordinates.
 * This applies the detached offsets when wall grid space is active.
 */
static inline void sdk_space_wall_grid_cell_from_chunk(int cx, int cz,
                                                        SdkSpaceWallGridCell* out_cell) {
    const int period = sdk_space_wall_grid_get_period();
    int offset_x, offset_z;
    sdk_space_wall_grid_get_effective_offset(&offset_x, &offset_z);
    
    const int grid_x = sdk_space_floor_div(cx - offset_x, period);
    const int grid_z = sdk_space_floor_div(cz - offset_z, period);
    
    if (!out_cell) return;
    
    out_cell->grid_x = grid_x;
    out_cell->grid_z = grid_z;
    out_cell->period = period;
    out_cell->space = SDK_SPACE_WALL_GRID;
    out_cell->offset_x = offset_x;
    out_cell->offset_z = offset_z;
    
    /* Calculate interior span */
    out_cell->interior = sdk_space_wall_grid_get_interior_span();
    
    /* Wall positions */
    out_cell->origin_cx = grid_x * period + offset_x;
    out_cell->origin_cz = grid_z * period + offset_z;
    
    /* East/south walls */
    out_cell->east_cx = out_cell->origin_cx + period;
    out_cell->south_cz = out_cell->origin_cz + period;
}

/**
 * Calculate wall grid cell from grid indices.
 */
static inline void sdk_space_wall_grid_cell_from_index(int grid_x, int grid_z,
                                                        SdkSpaceWallGridCell* out_cell) {
    if (!out_cell) return;
    
    out_cell->grid_x = grid_x;
    out_cell->grid_z = grid_z;
    out_cell->period = sdk_space_wall_grid_get_period();
    out_cell->space = SDK_SPACE_WALL_GRID;
    out_cell->interior = sdk_space_wall_grid_get_interior_span();
    
    int offset_x, offset_z;
    sdk_space_wall_grid_get_effective_offset(&offset_x, &offset_z);
    out_cell->offset_x = offset_x;
    out_cell->offset_z = offset_z;
    
    out_cell->origin_cx = grid_x * out_cell->period + offset_x;
    out_cell->origin_cz = grid_z * out_cell->period + offset_z;
    out_cell->east_cx = out_cell->origin_cx + out_cell->period;
    out_cell->south_cz = out_cell->origin_cz + out_cell->period;
}

/* ============================================================================
 * Chunk Classification
 * ============================================================================ */

/**
 * Check if a chunk is a wall chunk in the wall grid.
 * Accounts for detached offsets.
 */
static inline bool sdk_space_wall_grid_chunk_is_wall(int cx, int cz) {
    return sdk_world_walls_get_canonical_wall_chunk_owner(cx, cz, NULL, NULL, NULL, NULL, NULL) != 0;
}

/**
 * Check if a chunk is interior (not a wall) in wall grid space.
 */
static inline bool sdk_space_wall_grid_chunk_is_interior(int cx, int cz) {
    return !sdk_space_wall_grid_chunk_is_wall(cx, cz);
}

/**
 * Get the local position within the wall grid period.
 * This is the position relative to the cell origin (0 = wall position).
 */
static inline void sdk_space_wall_grid_chunk_local(int cx, int cz,
                                                    int* out_local_x, int* out_local_z) {
    if (out_local_x) *out_local_x = sdk_world_walls_chunk_local_x(cx);
    if (out_local_z) *out_local_z = sdk_world_walls_chunk_local_z(cz);
}

/**
 * Get the grid index (cell coordinates) for a chunk.
 */
static inline void sdk_space_wall_grid_chunk_to_grid(int cx, int cz,
                                                      int* out_grid_x, int* out_grid_z) {
    const int period = sdk_space_wall_grid_get_period();
    int offset_x, offset_z;
    sdk_space_wall_grid_get_effective_offset(&offset_x, &offset_z);
    
    if (out_grid_x) *out_grid_x = sdk_space_floor_div(cx - offset_x, period);
    if (out_grid_z) *out_grid_z = sdk_space_floor_div(cz - offset_z, period);
}

/**
 * Check which wall faces a chunk represents (if any).
 * Returns bitmask: bit 0 = west, bit 1 = north.
 */
static inline int sdk_space_wall_grid_chunk_wall_faces(int cx, int cz) {
    uint8_t wall_mask = 0u;
    (void)sdk_world_walls_get_canonical_wall_chunk_owner(cx, cz, &wall_mask, NULL, NULL, NULL, NULL);
    return (int)wall_mask;
}

/* ============================================================================
 * Coordinate Conversions
 * ============================================================================ */

/**
 * Convert wall grid cell to world block origin.
 */
static inline void sdk_space_wall_grid_cell_origin_to_world(const SdkSpaceWallGridCell* cell,
                                                             int* out_wx, int* out_wz) {
    if (!cell) return;
    if (out_wx) *out_wx = cell->origin_cx * CHUNK_WIDTH;
    if (out_wz) *out_wz = cell->origin_cz * CHUNK_DEPTH;
}

/**
 * Convert chunk coordinates to wall grid-tagged coordinate.
 */
static inline SdkSpaceChunkCoord sdk_space_wall_grid_chunk_coord(int cx, int cz) {
    SdkSpaceChunkCoord coord;
    coord.cx = cx;
    coord.cz = cz;
    coord.space = SDK_SPACE_WALL_GRID;
    return coord;
}

/**
 * Get the wall grid cell that contains a given chunk.
 */
static inline SdkSpaceWallGridCell sdk_space_wall_grid_cell_containing_chunk(int cx, int cz) {
    SdkSpaceWallGridCell cell;
    sdk_space_wall_grid_cell_from_chunk(cx, cz, &cell);
    return cell;
}

/* ============================================================================
 * Comparison with Terrain Space
 * ============================================================================ */

/**
 * Check if a chunk is a wall in terrain space but NOT in wall grid space.
 * This detects the misalignment when detached walls are enabled.
 */
bool sdk_space_wall_grid_misaligned_from_terrain(int cx, int cz);

/**
 * Get the set of chunks that are walls in both spaces.
 * These are the "agreement" positions where both grids align.
 */
void sdk_space_wall_grid_find_alignment_points(int* out_cx, int* out_cz, int max_results);

/* ============================================================================
 * Space Definition Accessor
 * ============================================================================ */

/**
 * Get the wall grid space definition.
 */
const SdkCoordinateSpace* sdk_space_wall_grid_definition(void);

/**
 * Initialize wall grid space in the registry.
 */
void sdk_space_wall_grid_register(void);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SPACE_WALL_GRID_H */
