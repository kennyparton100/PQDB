/**
 * sdk_superchunk_config.h -- Runtime superchunk configuration.
 *
 * This header provides runtime-configurable superchunk settings,
 * replacing the hardcoded 16x16 chunk span with dynamic values.
 */
#ifndef NQLSDK_SUPERCHUNK_CONFIG_H
#define NQLSDK_SUPERCHUNK_CONFIG_H

#include "../../../sdk_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    int  chunk_span;  /* Terrain interior span: 2, 4, 8, 16, 32, 64, 128 */
    uint8_t coordinate_system;  /* SdkWorldCoordinateSystem */
    bool walls_enabled;  /* Whether to generate walls around superchunks */
    int  wall_grid_size;  /* Wall ring size metadata/runtime input for detached/grid systems */
    int  wall_grid_offset_x;  /* Detached wall grid offset X (-1000 to 1000, default 0) */
    int  wall_grid_offset_z;  /* Detached wall grid offset Z (-1000 to 1000, default 0) */
    int  wall_thickness_blocks;  /* Physical wall thickness in blocks */
    bool wall_rings_shared;  /* Whether adjacent grid cells share their wall boundary */
} SdkSuperchunkConfig;

int sdk_world_coordinate_system_is_valid(SdkWorldCoordinateSystem coordinate_system);
const char* sdk_world_coordinate_system_name(SdkWorldCoordinateSystem coordinate_system);
const char* sdk_world_coordinate_system_display_name(SdkWorldCoordinateSystem coordinate_system);
int sdk_world_coordinate_system_uses_superchunks(SdkWorldCoordinateSystem coordinate_system);
int sdk_world_coordinate_system_detaches_walls(SdkWorldCoordinateSystem coordinate_system);

/**
 * Validate chunk span value.
 * @param span The chunk span to validate.
 * @return 1 if valid (power of 2, >= 2, <= 128), 0 otherwise.
 */
int sdk_superchunk_validate_chunk_span(int span);

/**
 * Normalize a superchunk configuration in place.
 * Invalid spans fall back to 16. Walls require superchunks. Grid wall sizing
 * is normalized through the world-wall config rules, while attached/superchunk
 * mode keeps wall layout aligned to the terrain superchunk rhythm.
 * @param config The configuration to normalize.
 */
void sdk_superchunk_normalize_config(SdkSuperchunkConfig* config);

/**
 * Get the current superchunk configuration.
 * @return Pointer to the global superchunk configuration.
 */
const SdkSuperchunkConfig* sdk_superchunk_get_config(void);

/**
 * Set the superchunk configuration.
 * @param config The new configuration to apply.
 */
void sdk_superchunk_set_config(const SdkSuperchunkConfig* config);
SdkWorldCoordinateSystem sdk_superchunk_get_coordinate_system(void);
void sdk_superchunk_set_coordinate_system(SdkWorldCoordinateSystem coordinate_system);

/**
 * Get the current chunk span.
 * @return The number of chunks per superchunk dimension (2-128).
 */
int sdk_superchunk_get_chunk_span(void);

/**
 * Get the block span (chunks * CHUNK_WIDTH).
 * @return The number of blocks per superchunk dimension.
 */
int sdk_superchunk_get_block_span(void);

/**
 * Get the wall period (chunk_span + 1).
 * @return The period at which walls appear in chunk coordinates.
 */
int sdk_superchunk_get_wall_period(void);

/**
 * Get the wall-grid period used by grid-wall classification.
 * Shared rings and explicit wall thickness are applied by the world-wall
 * configuration before this value is returned.
 * @return The wall-grid period in chunk coordinates.
 */
int sdk_superchunk_get_wall_grid_period(void);

/**
 * Get the wall-grid interior span in chunks.
 * @return The interior span addressed by the wall grid.
 */
int sdk_superchunk_get_wall_grid_interior_span(void);

/**
 * Get the wall thickness in chunks.
 * @return The wall thickness in chunks.
 */
int sdk_superchunk_get_wall_thickness_chunks(void);

/**
 * Get the wall thickness in blocks.
 * @return The wall thickness in blocks.
 */
int sdk_superchunk_get_wall_thickness_blocks(void);

/**
 * Get the gate start block position.
 * @return The starting block position for gates.
 */
int sdk_superchunk_get_gate_start_block(void);

/**
 * Get the gate end block position.
 * @return The ending block position for gates.
 */
int sdk_superchunk_get_gate_end_block(void);

/**
 * Get the gate width in blocks.
 * @return The gate width in blocks.
 */
int sdk_superchunk_get_gate_width_blocks(void);

/**
 * Get the gate support width in chunks.
 * @return The gate support width in chunks.
 */
int sdk_superchunk_get_gate_support_width_chunks(void);

/**
 * Get the gate support start chunk position.
 * @return The starting chunk position for gate supports.
 */
int sdk_superchunk_get_gate_support_start_chunk(void);

/**
 * Get the gate support end chunk position.
 * @return The ending chunk position for gate supports.
 */
int sdk_superchunk_get_gate_support_end_chunk(void);

/**
 * Get whether walls are enabled.
 * @return 1 if walls are enabled, 0 otherwise.
 */
int sdk_superchunk_get_walls_enabled(void);

/**
 * Set whether walls are enabled.
 * @param enabled 1 to enable walls, 0 to disable.
 */
void sdk_superchunk_set_walls_enabled(int enabled);

/**
 * Get whether superchunks are enabled.
 * @return 1 if superchunks are enabled, 0 otherwise.
 */
int sdk_superchunk_get_enabled(void);

/**
 * Get detached wall grid size.
 * @return Grid size (2-100).
 */
int sdk_superchunk_get_wall_grid_size(void);

/**
 * Set detached wall grid size.
 * @param size Grid size (2-100).
 */
void sdk_superchunk_set_wall_grid_size(int size);

/**
 * Get detached wall grid offset X.
 * @return Grid offset X (-1000 to 1000).
 */
int sdk_superchunk_get_wall_grid_offset_x(void);

/**
 * Set detached wall grid offset X.
 * @param offset Grid offset X (-1000 to 1000).
 */
void sdk_superchunk_set_wall_grid_offset_x(int offset);

/**
 * Get detached wall grid offset Z.
 * @return Grid offset Z (-1000 to 1000).
 */
int sdk_superchunk_get_wall_grid_offset_z(void);

/**
 * Set detached wall grid offset Z.
 * @param offset Grid offset Z (-1000 to 1000).
 */
void sdk_superchunk_set_wall_grid_offset_z(int offset);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SUPERCHUNK_CONFIG_H */
