/**
 * sdk_coordinate_space_conversions.h -- Cross-space coordinate conversions.
 *
 * This header provides utilities for converting coordinates between different
 * coordinate spaces. It's the glue that allows the engine to reason about
 * positions across multiple parallel grids.
 *
 * Key capabilities:
 * - Convert chunk coordinates between any two spaces
 * - Calculate world block positions from any space
 * - Determine if coordinates in different spaces refer to the same world position
 * - Find distances and relationships across space boundaries
 */

#ifndef NQLSDK_COORDINATE_SPACE_CONVERSIONS_H
#define NQLSDK_COORDINATE_SPACE_CONVERSIONS_H

#include "sdk_coordinate_space.h"
#include "sdk_space_world_block.h"
#include "sdk_space_global_chunk.h"
#include "sdk_space_superchunk_terrain.h"
#include "sdk_space_wall_grid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Space-to-Space Conversions
 * ============================================================================ */

/**
 * Convert a chunk coordinate from one space to another.
 *
 * The conversion path is:
 *   source space -> global chunk (world block intermediate) -> target space
 *
 * @param source_coord The coordinate to convert (tagged with source space)
 * @param target_space The desired target space
 * @param out_result   Output converted coordinate (tagged with target space)
 * @return 1 on success, 0 if conversion not possible
 */
int sdk_space_convert_chunk(const SdkSpaceChunkCoord* source_coord,
                            SdkCoordinateSpaceType target_space,
                            SdkSpaceChunkCoord* out_result);

/**
 * Convert a block coordinate from one space to another.
 *
 * @param source_coord The block coordinate to convert
 * @param target_space The desired target space
 * @param out_result   Output converted coordinate
 * @return 1 on success, 0 if conversion not possible
 */
int sdk_space_convert_block(const SdkSpaceBlockCoord* source_coord,
                            SdkCoordinateSpaceType target_space,
                            SdkSpaceBlockCoord* out_result);

/**
 * Convert any space chunk coordinate to world block origin.
 * This is the canonical conversion used by all other operations.
 *
 * @param chunk_coord Source chunk coordinate (any space)
 * @param out_block   Output world block coordinate (SDK_SPACE_WORLD_BLOCK)
 * @return 1 on success, 0 on failure
 */
int sdk_space_chunk_to_world_block_any(const SdkSpaceChunkCoord* chunk_coord,
                                        SdkSpaceBlockCoord* out_block);

/**
 * Convert world block coordinate to any target space.
 *
 * @param block_coord Source block coordinate (usually SDK_SPACE_WORLD_BLOCK)
 * @param target_space Desired space for output
 * @param out_chunk Output chunk coordinate in target space
 * @return 1 on success, 0 on failure
 */
int sdk_space_world_block_to_chunk_any(const SdkSpaceBlockCoord* block_coord,
                                        SdkCoordinateSpaceType target_space,
                                        SdkSpaceChunkCoord* out_chunk);

/* ============================================================================
 * Space Relationship Queries
 * ============================================================================ */

/**
 * Check if two coordinates in potentially different spaces refer to the same
 * chunk in world space.
 *
 * @param a First coordinate
 * @param b Second coordinate
 * @return true if both refer to the same world chunk
 */
bool sdk_space_chunks_equivalent(const SdkSpaceChunkCoord* a,
                                  const SdkSpaceChunkCoord* b);

/**
 * Check if two block coordinates refer to the same world block.
 *
 * @param a First coordinate
 * @param b Second coordinate
 * @return true if both refer to the same world block
 */
bool sdk_space_blocks_equivalent(const SdkSpaceBlockCoord* a,
                                  const SdkSpaceBlockCoord* b);

/**
 * Calculate the distance in chunks between two coordinates (any spaces).
 * Distance is measured in global chunk space.
 *
 * @param a First coordinate
 * @param b Second coordinate
 * @return Manhattan distance in chunks
 */
int sdk_space_chunk_distance_manhattan(const SdkSpaceChunkCoord* a,
                                        const SdkSpaceChunkCoord* b);

/**
 * Calculate Chebyshev distance between two coordinates (any spaces).
 *
 * @param a First coordinate
 * @param b Second coordinate
 * @return Chebyshev distance in chunks
 */
int sdk_space_chunk_distance_chebyshev(const SdkSpaceChunkCoord* a,
                                        const SdkSpaceChunkCoord* b);

/**
 * Calculate Euclidean distance squared between two coordinates.
 * Useful for comparison without sqrt.
 *
 * @param a First coordinate
 * @param b Second coordinate
 * @return Euclidean distance squared
 */
int sdk_space_chunk_distance_squared(const SdkSpaceChunkCoord* a,
                                      const SdkSpaceChunkCoord* b);

/* ============================================================================
 * Space Alignment Analysis
 * ============================================================================ */

/**
 * Calculate the "grid alignment" between two spaces at a given position.
 * Returns how well the periodic grids align at the specified chunk.
 *
 * @param space_a First space to compare
 * @param space_b Second space to compare
 * @param cx Chunk X to test
 * @param cz Chunk Z to test
 * @return Alignment metric (0 = perfect alignment, higher = more misaligned)
 */
int sdk_space_grid_alignment_at(SdkCoordinateSpaceType space_a,
                                  SdkCoordinateSpaceType space_b,
                                  int cx, int cz);

/**
 * Find the nearest position where two periodic spaces align perfectly.
 * For two spaces with periods p1 and p2, alignment occurs at multiples of LCM(p1, p2).
 *
 * @param space_a First space
 * @param space_b Second space
 * @param near_cx Search near this X coordinate
 * @param near_cz Search near this Z coordinate
 * @param out_align_cx Output aligned X
 * @param out_align_cz Output aligned Z
 * @return 1 if alignment exists, 0 if one or both spaces are aperiodic
 */
int sdk_space_find_nearest_alignment(SdkCoordinateSpaceType space_a,
                                     SdkCoordinateSpaceType space_b,
                                     int near_cx, int near_cz,
                                     int* out_align_cx, int* out_align_cz);

/**
 * Check if two spaces can ever align (both have periods).
 *
 * @param space_a First space
 * @param space_b Second space
 * @return true if both have periods and can align
 */
bool sdk_space_can_align(SdkCoordinateSpaceType space_a,
                         SdkCoordinateSpaceType space_b);

/* ============================================================================
 * Multi-Space Operations
 * ============================================================================ */

/**
 * For a given world position, get the chunk coordinate in ALL defined spaces.
 *
 * @param wx World block X
 * @param wz World block Z
 * @param out_coords Array of space-tagged coordinates (size SDK_SPACE_COUNT)
 * @return Number of valid spaces written
 */
int sdk_space_get_all_chunk_coords(int wx, int wz,
                                   SdkSpaceChunkCoord* out_coords);

/**
 * Find which spaces have a wall at the given chunk position.
 *
 * @param cx Chunk X
 * @param cz Chunk Z
 * @param out_wall_spaces Array to fill with spaces that have walls here
 * @param max_spaces Size of output array
 * @return Number of spaces that have walls at this position
 */
int sdk_space_find_wall_spaces(int cx, int cz,
                               SdkCoordinateSpaceType* out_wall_spaces,
                               int max_spaces);

/**
 * Determine the "dominant" space at a chunk position.
 * When multiple spaces claim a chunk, this returns which one should
 * take precedence (based on engine rules).
 *
 * Current precedence:
 *   1. Wall spaces (if wall present)
 *   2. Terrain space
 *   3. Global chunk space
 *
 * @param cx Chunk X
 * @param cz Chunk Z
 * @return The dominant space type for this chunk
 */
SdkCoordinateSpaceType sdk_space_get_dominant_at(int cx, int cz);

/* ============================================================================
 * Space-Aware Iterators
 * ============================================================================ */

/**
 * Callback for iterating over a region in a specific space.
 * Return 0 to stop iteration, non-zero to continue.
 */
typedef int (*SdkSpaceChunkCallback)(const SdkSpaceChunkCoord* coord, void* user_data);

/**
 * Iterate over all chunks in a rectangular region within a specific space.
 *
 * @param space The space to iterate in
 * @param min_cx Minimum chunk X (inclusive, in space coordinates)
 * @param min_cz Minimum chunk Z (inclusive, in space coordinates)
 * @param max_cx Maximum chunk X (inclusive, in space coordinates)
 * @param max_cz Maximum chunk Z (inclusive, in space coordinates)
 * @param callback Function to call for each chunk
 * @param user_data Opaque pointer passed to callback
 * @return Number of chunks visited
 */
int sdk_space_iterate_region(SdkCoordinateSpaceType space,
                             int min_cx, int min_cz,
                             int max_cx, int max_cz,
                             SdkSpaceChunkCallback callback,
                             void* user_data);

/**
 * Iterate over wall chunks in a specific space within a region.
 *
 * @param space The space to iterate in
 * @param min_cx Minimum chunk X
 * @param min_cz Minimum chunk Z
 * @param max_cx Maximum chunk X
 * @param max_cz Maximum chunk Z
 * @param callback Function to call for each wall chunk
 * @param user_data Opaque pointer passed to callback
 * @return Number of wall chunks visited
 */
int sdk_space_iterate_walls(SdkCoordinateSpaceType space,
                            int min_cx, int min_cz,
                            int max_cx, int max_cz,
                            SdkSpaceChunkCallback callback,
                            void* user_data);

/* ============================================================================
 * Debugging and Diagnostics
 * ============================================================================ */

/**
 * Get a human-readable description of a space-tagged chunk coordinate.
 *
 * @param coord The coordinate to describe
 * @param out_buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of characters written (not including null terminator)
 */
int sdk_space_chunk_coord_describe(const SdkSpaceChunkCoord* coord,
                                   char* out_buffer, int buffer_size);

/**
 * Get a human-readable description of the relationship between two coordinates.
 *
 * @param a First coordinate
 * @param b Second coordinate
 * @param out_buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of characters written
 */
int sdk_space_describe_relationship(const SdkSpaceChunkCoord* a,
                                      const SdkSpaceChunkCoord* b,
                                      char* out_buffer, int buffer_size);

/**
 * Print a debug representation of all spaces at a world position.
 *
 * @param wx World block X
 * @param wy World block Y
 * @param wz World block Z
 */
void sdk_space_debug_print_all_at(int wx, int wy, int wz);

/* ============================================================================
 * Specialized Conversions
 * ============================================================================ */

/**
 * Convert terrain space chunk to wall grid space chunk.
 * This is a common operation when determining if a terrain chunk
 * is affected by a wall in the detached wall grid.
 */
static inline SdkSpaceChunkCoord sdk_space_terrain_to_wall_grid(int terrain_cx, int terrain_cz) {
    SdkSpaceChunkCoord source = sdk_space_terrain_chunk_coord(terrain_cx, terrain_cz);
    SdkSpaceChunkCoord result;
    sdk_space_convert_chunk(&source, SDK_SPACE_WALL_GRID, &result);
    return result;
}

/**
 * Convert wall grid space chunk to terrain space chunk.
 */
static inline SdkSpaceChunkCoord sdk_space_wall_grid_to_terrain(int wall_cx, int wall_cz) {
    SdkSpaceChunkCoord source = sdk_space_wall_grid_chunk_coord(wall_cx, wall_cz);
    SdkSpaceChunkCoord result;
    sdk_space_convert_chunk(&source, SDK_SPACE_SUPERCHUNK_TERRAIN, &result);
    return result;
}

/**
 * Check if a terrain chunk is aligned with the wall grid at its position.
 * When detached walls are enabled, terrain and wall grids may diverge.
 */
static inline bool sdk_space_terrain_wall_aligned(int cx, int cz) {
    SdkSpaceChunkCoord terrain = sdk_space_terrain_chunk_coord(cx, cz);
    SdkSpaceChunkCoord wall;
    if (!sdk_space_convert_chunk(&terrain, SDK_SPACE_WALL_GRID, &wall)) {
        return false;
    }
    return terrain.cx == wall.cx && terrain.cz == wall.cz;
}

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_COORDINATE_SPACE_CONVERSIONS_H */
