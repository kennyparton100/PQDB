/**
 * sdk_coordinate_space.h -- Core coordinate space types and registry.
 *
 * This header defines the abstraction layer for multiple parallel coordinate
 * spaces in the world engine. Each space has its own grid period, offsets,
 * and worldgen pipeline association.
 *
 * Design principle: Coordinates are tagged with their space type to prevent
 * accidental cross-space mixing. Conversions must be explicit.
 */

#ifndef NQLSDK_COORDINATE_SPACE_H
#define NQLSDK_COORDINATE_SPACE_H

#include <stdint.h>
#include <stdbool.h>
#include "../Superchunks/Config/sdk_superchunk_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Coordinate Space Types
 * ============================================================================ */

typedef enum SdkCoordinateSpaceType {
    SDK_SPACE_INVALID = 0,           /* Uninitialized/invalid space */
    SDK_SPACE_WORLD_BLOCK,         /* Absolute world block coordinates */
    SDK_SPACE_GLOBAL_CHUNK,        /* Raw chunk grid (no grouping) */
    SDK_SPACE_SUPERCHUNK_TERRAIN,  /* Attached terrain superchunk grid */
    SDK_SPACE_WALL_GRID,           /* Detached wall grid */
    SDK_SPACE_COUNT                /* Number of defined spaces */
} SdkCoordinateSpaceType;

/* ============================================================================
 * Tagged Coordinate Types
 * ============================================================================ */

/**
 * Chunk coordinate tagged with its coordinate space.
 * Prevents accidental mixing of coordinates from different spaces.
 */
typedef struct SdkSpaceChunkCoord {
    int cx;
    int cz;
    SdkCoordinateSpaceType space;
} SdkSpaceChunkCoord;

/**
 * Block coordinate tagged with its coordinate space.
 * World block space is the canonical reference; other spaces convert to/from it.
 */
typedef struct SdkSpaceBlockCoord {
    int wx;
    int wy;
    int wz;
    SdkCoordinateSpaceType space;  /* Usually SDK_SPACE_WORLD_BLOCK */
} SdkSpaceBlockCoord;

/**
 * Local coordinate within a cell/grid cell, tagged with source space.
 */
typedef struct SdkSpaceLocalCoord {
    int lx;
    int ly;
    int lz;
    SdkCoordinateSpaceType space;
} SdkSpaceLocalCoord;

/* ============================================================================
 * Coordinate Space Definition
 * ============================================================================ */

/**
 * Complete definition of a coordinate space.
 * Used by the registry to look up space parameters.
 */
typedef struct SdkCoordinateSpace {
    SdkCoordinateSpaceType type;
    const char* name;              /* "world_block", "terrain", "wall_grid" */
    
    /* Grid parameters (for periodic spaces) */
    int period_chunks;             /* Grid period in chunk coordinates (0 if aperiodic) */
    int offset_x;                  /* Grid offset in chunks */
    int offset_z;
    
    /* Block parameters */
    int block_width;               /* CHUNK_WIDTH (64) for chunk-based spaces */
    int block_height;              /* CHUNK_HEIGHT (1024) */
    
    /* Worldgen association */
    const char* worldgen_pipeline; /* "terrain", "wall", "custom_a", etc. */
    
    /* Space capabilities */
    bool has_terrain;              /* Does this space generate terrain? */
    bool has_walls;                /* Does this space generate walls? */
    bool renderable;               /* Can chunks from this space be rendered? */
} SdkCoordinateSpace;

/* ============================================================================
 * Space Registry
 * ============================================================================ */

/**
 * Initialize the coordinate space registry from current SDK configuration.
 * Must be called before using space lookup functions.
 */
void sdk_coordinate_space_registry_init(void);

/**
 * Get a coordinate space definition by type.
 * @return Pointer to space definition, or NULL if not found.
 */
const SdkCoordinateSpace* sdk_coordinate_space_get(SdkCoordinateSpaceType type);

/**
 * Get coordinate space by name (case-sensitive).
 * @return Space type, or SDK_SPACE_INVALID if not found.
 */
SdkCoordinateSpaceType sdk_coordinate_space_type_by_name(const char* name);

/**
 * Check if a space type is valid and registered.
 */
static inline bool sdk_coordinate_space_is_valid(SdkCoordinateSpaceType type) {
    return type > SDK_SPACE_INVALID && type < SDK_SPACE_COUNT;
}

/* ============================================================================
 * Space Query Functions
 * ============================================================================ */

/**
 * Get the effective period for a coordinate space.
 * For aperiodic spaces (world block), returns 0.
 */
int sdk_coordinate_space_get_period(SdkCoordinateSpaceType type);

/**
 * Get the effective offset for a coordinate space.
 */
void sdk_coordinate_space_get_offset(SdkCoordinateSpaceType type, int* out_x, int* out_z);

/**
 * Get the worldgen pipeline name for a space.
 */
const char* sdk_coordinate_space_get_pipeline(SdkCoordinateSpaceType type);

/**
 * Check if two coordinates in potentially different spaces refer to the same
 * chunk in world space.
 */
bool sdk_coordinate_space_same_world_chunk(const SdkSpaceChunkCoord* a,
                                            const SdkSpaceChunkCoord* b);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Create a tagged chunk coordinate.
 */
static inline SdkSpaceChunkCoord sdk_space_chunk_coord_make(int cx, int cz,
                                                            SdkCoordinateSpaceType space) {
    SdkSpaceChunkCoord coord = { cx, cz, space };
    return coord;
}

/**
 * Create a tagged block coordinate.
 */
static inline SdkSpaceBlockCoord sdk_space_block_coord_make(int wx, int wy, int wz,
                                                            SdkCoordinateSpaceType space) {
    SdkSpaceBlockCoord coord = { wx, wy, wz, space };
    return coord;
}

/**
 * Check if a chunk coordinate is valid (non-null space).
 */
static inline bool sdk_space_chunk_coord_is_valid(const SdkSpaceChunkCoord* coord) {
    return coord != NULL && sdk_coordinate_space_is_valid(coord->space);
}

/**
 * Check if a block coordinate is valid.
 */
static inline bool sdk_space_block_coord_is_valid(const SdkSpaceBlockCoord* coord) {
    return coord != NULL && sdk_coordinate_space_is_valid(coord->space);
}

/**
 * Compare two chunk coordinates for equality (same space and position).
 */
static inline bool sdk_space_chunk_coord_equal(const SdkSpaceChunkCoord* a,
                                                const SdkSpaceChunkCoord* b) {
    if (a == NULL || b == NULL) return false;
    return a->cx == b->cx && a->cz == b->cz && a->space == b->space;
}

/* ============================================================================
 * Floor Division Helpers (consistent with existing SDK)
 * ============================================================================ */

/**
 * Floor division for coordinate calculations.
 * Handles negative numbers correctly (rounds toward -inf).
 */
static inline int sdk_space_floor_div(int value, int denom) {
    if (denom == 0) return 0;
    if (value >= 0) return value / denom;
    return -((-value + denom - 1) / denom);
}

/**
 * Floor modulo for coordinate calculations.
 * Result is always non-negative.
 */
static inline int sdk_space_floor_mod(int value, int denom) {
    if (denom == 0) return 0;
    int result = value % denom;
    if (result < 0) result += denom;
    return result;
}

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_COORDINATE_SPACE_H */
