/**
 * sdk_space_world_block.h -- Absolute world block coordinate space.
 *
 * World block space is the canonical reference coordinate system.
 * All other spaces convert to/from world block coordinates.
 *
 * This space is aperiodic (no grid period) and has no offsets.
 */

#ifndef NQLSDK_SPACE_WORLD_BLOCK_H
#define NQLSDK_SPACE_WORLD_BLOCK_H

#include "sdk_coordinate_space.h"
#include "../Chunks/sdk_chunk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * World Block Space Constants
 * ============================================================================ */

#define SDK_SPACE_WORLD_BLOCK_NAME "world_block"
#define SDK_SPACE_WORLD_BLOCK_PIPELINE "raw"  /* No worldgen - reference space */

/* ============================================================================
 * World Block to Chunk Conversions
 * ============================================================================ */

/**
 * Convert world block X coordinate to chunk X coordinate.
 * This is the standard conversion used throughout the engine.
 */
static inline int sdk_space_world_block_to_chunk_x(int wx) {
    return sdk_world_to_chunk_x(wx);
}

/**
 * Convert world block Z coordinate to chunk Z coordinate.
 */
static inline int sdk_space_world_block_to_chunk_z(int wz) {
    return sdk_world_to_chunk_z(wz);
}

/**
 * Convert world block coordinates to chunk coordinates.
 * @param out_cx Output chunk X
 * @param out_cz Output chunk Z
 */
static inline void sdk_space_world_block_to_chunk(int wx, int wz,
                                                   int* out_cx, int* out_cz) {
    if (out_cx) *out_cx = sdk_space_world_block_to_chunk_x(wx);
    if (out_cz) *out_cz = sdk_space_world_block_to_chunk_z(wz);
}

/**
 * Convert world block coordinate to chunk-local coordinate.
 */
static inline int sdk_space_world_block_to_local_x(int wx, int cx) {
    return sdk_world_to_local_x(wx, cx);
}

static inline int sdk_space_world_block_to_local_z(int wz, int cz) {
    return sdk_world_to_local_z(wz, cz);
}

/* ============================================================================
 * World Block to Tagged Conversions
 * ============================================================================ */

/**
 * Convert world block coordinates to a tagged chunk coordinate.
 * The resulting coordinate is in SDK_SPACE_GLOBAL_CHUNK space.
 */
static inline SdkSpaceChunkCoord sdk_space_world_block_to_global_chunk_coord(int wx, int wz) {
    SdkSpaceChunkCoord coord;
    coord.cx = sdk_space_world_block_to_chunk_x(wx);
    coord.cz = sdk_space_world_block_to_chunk_z(wz);
    coord.space = SDK_SPACE_GLOBAL_CHUNK;
    return coord;
}

/**
 * Convert world block coordinates to a tagged block coordinate.
 */
static inline SdkSpaceBlockCoord sdk_space_world_block_coord(int wx, int wy, int wz) {
    SdkSpaceBlockCoord coord;
    coord.wx = wx;
    coord.wy = wy;
    coord.wz = wz;
    coord.space = SDK_SPACE_WORLD_BLOCK;
    return coord;
}

/* ============================================================================
 * From Other Spaces to World Block
 * ============================================================================ */

/**
 * Convert chunk coordinates + local coordinates to world block.
 */
static inline int sdk_space_chunk_local_to_world_block_x(int cx, int lx) {
    return sdk_chunk_to_world_x(lx, cx);
}

static inline int sdk_space_chunk_local_to_world_block_z(int cz, int lz) {
    return sdk_chunk_to_world_z(lz, cz);
}

/**
 * Convert tagged chunk coordinate to world block coordinate.
 * This is the canonical conversion that all spaces use internally.
 */
SdkSpaceBlockCoord sdk_space_chunk_to_world_block(const SdkSpaceChunkCoord* chunk_coord);

/**
 * Convert tagged chunk coordinate to the origin world block of that chunk.
 */
static inline SdkSpaceBlockCoord sdk_space_chunk_origin_to_world_block(int cx, int cz) {
    SdkSpaceBlockCoord coord;
    coord.wx = cx * CHUNK_WIDTH;
    coord.wy = 0;  /* Chunks span full height */
    coord.wz = cz * CHUNK_DEPTH;
    coord.space = SDK_SPACE_WORLD_BLOCK;
    return coord;
}

/* ============================================================================
 * Space Definition Accessor
 * ============================================================================ */

/**
 * Get the world block space definition.
 * This is a singleton; world block space always has the same parameters.
 */
const SdkCoordinateSpace* sdk_space_world_block_definition(void);

/**
 * Initialize world block space in the registry.
 * Called automatically by sdk_coordinate_space_registry_init().
 */
void sdk_space_world_block_register(void);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SPACE_WORLD_BLOCK_H */
