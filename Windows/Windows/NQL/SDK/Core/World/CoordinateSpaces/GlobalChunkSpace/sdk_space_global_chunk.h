/**
 * sdk_space_global_chunk.h -- Raw global chunk coordinate space.
 *
 * The global chunk space is the simplest chunk coordinate system:
 * just chunk coordinates without any grouping, periods, or offsets.
 * It's the "no space" space—coordinates are exactly what you specify.
 *
 * This space is useful for:
 * - Space-agnostic operations that work on raw chunk coords
 * - Converting between periodic spaces (terrain/wall) via global chunk
 * - Debug/visualization that doesn't care about superchunk grouping
 */

#ifndef NQLSDK_SPACE_GLOBAL_CHUNK_H
#define NQLSDK_SPACE_GLOBAL_CHUNK_H

#include "sdk_coordinate_space.h"
#include "../Chunks/sdk_chunk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Global Chunk Space Constants
 * ============================================================================ */

#define SDK_SPACE_GLOBAL_CHUNK_NAME "global_chunk"
#define SDK_SPACE_GLOBAL_CHUNK_PIPELINE "raw"

/* ============================================================================
 * Global Chunk Conversions
 * ============================================================================ */

/**
 * Convert global chunk coordinates to world block origin.
 */
static inline void sdk_space_global_chunk_to_world_block_origin(int cx, int cz,
                                                                 int* out_wx, int* out_wz) {
    if (out_wx) *out_wx = cx * CHUNK_WIDTH;
    if (out_wz) *out_wz = cz * CHUNK_DEPTH;
}

/**
 * Convert world block coordinates to global chunk coordinates.
 */
static inline void sdk_space_world_block_to_global_chunk(int wx, int wz,
                                                          int* out_cx, int* out_cz) {
    if (out_cx) *out_cx = sdk_world_to_chunk_x(wx);
    if (out_cz) *out_cz = sdk_world_to_chunk_z(wz);
}

/**
 * Create a tagged global chunk coordinate.
 */
static inline SdkSpaceChunkCoord sdk_space_global_chunk_coord(int cx, int cz) {
    SdkSpaceChunkCoord coord;
    coord.cx = cx;
    coord.cz = cz;
    coord.space = SDK_SPACE_GLOBAL_CHUNK;
    return coord;
}

/* ============================================================================
 * Global Chunk as Intermediary
 * ============================================================================ */

/**
 * Convert any space chunk coordinate to global chunk.
 * This strips the space-specific grouping, leaving raw chunk coords.
 */
SdkSpaceChunkCoord sdk_space_to_global_chunk(const SdkSpaceChunkCoord* coord);

/**
 * Convert global chunk coordinate to a specific target space.
 * The resulting coordinate will have the target space's period/offset applied.
 */
SdkSpaceChunkCoord sdk_space_global_to_target_space(const SdkSpaceChunkCoord* global_coord,
                                                    SdkCoordinateSpaceType target_space);

/* ============================================================================
 * Distance Calculations
 * ============================================================================ */

/**
 * Calculate Manhattan distance between two global chunk coordinates.
 */
static inline int sdk_space_global_chunk_distance_manhattan(int cx1, int cz1,
                                                             int cx2, int cz2) {
    return abs(cx1 - cx2) + abs(cz1 - cz2);
}

/**
 * Calculate Chebyshev distance between two global chunk coordinates.
 */
static inline int sdk_space_global_chunk_distance_chebyshev(int cx1, int cz1,
                                                             int cx2, int cz2) {
    int dx = abs(cx1 - cx2);
    int dz = abs(cz1 - cz2);
    return (dx > dz) ? dx : dz;
}

/**
 * Calculate Euclidean distance squared (for comparisons, avoids sqrt).
 */
static inline int sdk_space_global_chunk_distance_squared(int cx1, int cz1,
                                                           int cx2, int cz2) {
    int dx = cx1 - cx2;
    int dz = cz1 - cz2;
    return dx * dx + dz * dz;
}

/* ============================================================================
 * Space Definition Accessor
 * ============================================================================ */

/**
 * Get the global chunk space definition.
 */
const SdkCoordinateSpace* sdk_space_global_chunk_definition(void);

/**
 * Initialize global chunk space in the registry.
 */
void sdk_space_global_chunk_register(void);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SPACE_GLOBAL_CHUNK_H */
