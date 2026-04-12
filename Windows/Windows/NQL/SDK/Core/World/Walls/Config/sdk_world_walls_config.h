/* ================================================================================
 * sdk_world_walls_config.h -- World-level wall configuration
 *
 * Separates physical wall generation from wall-grid coordinate classification.
 * Walls can exist in any coordinate system; the grid-wall space is only one
 * possible classification mode.
 *
 * When wall grid coordinate space is active, these params must be consistent
 * with the grid configuration (ring_size == wall_grid_size).
 * ================================================================================ */

#ifndef NQLSDK_WORLD_WALLS_CONFIG_H
#define NQLSDK_WORLD_WALLS_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "../../Config/sdk_world_config.h"
#include "../../Superchunks/Config/sdk_superchunk_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================================
 * Wall Configuration Structure
 * ================================================================================ */

typedef struct SdkWorldWallsConfig {
    bool enabled;

    /* Wall ring size: span between walls including the walls themselves.
     * Grid-wall systems treat this as the full box width including both wall
     * sides. Shared-wall grids repeat every
     * (ring_size - wall_thickness_chunks).
     */
    int ring_size;
    
    /* Physical wall thickness in blocks and chunk-space classification width. */
    int wall_thickness_blocks;
    int wall_thickness_chunks;
    
    /* If true, walls are shared between adjacent grid boxes
     * If false, each grid box has its own wall (walls double up at boundaries)
     */
    bool wall_rings_shared;
    
    /* If true, walls use the dedicated wall-grid coordinate space for chunk
     * classification. If false, walls still exist but chunk classification is
     * owned by the active non-grid coordinate system.
     */
    bool use_wall_grid_space;

    int offset_x;
    int offset_z;
    
    /* Initialized flag */
    bool initialized;
    
} SdkWorldWallsConfig;

/* ================================================================================
 * Configuration Lifecycle
 * ================================================================================ */

/**
 * Initialize wall configuration from world save meta.
 * Derives ring_size from superchunk config if not explicitly set and resolves
 * whether the world uses dedicated wall-grid classification.
 */
void sdk_world_walls_config_init(const SdkWorldSaveMeta* meta,
                                  const SdkSuperchunkConfig* sc_config,
                                  SdkWorldCoordinateSystem coord_sys);

/**
 * Shutdown wall configuration.
 */
void sdk_world_walls_config_shutdown(void);

/* ================================================================================
 * Wall Ring Queries (Physical Wall Positioning)
 * ================================================================================ */

/**
 * Get the configured wall ring size in chunks.
 */
int sdk_world_walls_get_ring_size(void);
int sdk_world_walls_get_interior_span_chunks(void);
int sdk_world_walls_get_period(void);

/**
 * Check if wall rings are shared between grid boxes.
 * @return true if shared, false if each box has its own wall.
 */
bool sdk_world_walls_are_rings_shared(void);

/**
 * Get wall thickness in chunks.
 * @return Thickness (typically 1).
 */
int sdk_world_walls_get_thickness_chunks(void);
int sdk_world_walls_get_thickness_blocks(void);

/**
 * Check if wall grid coordinate space is used for chunk classification.
 * @return true if wall grid space is active.
 */
bool sdk_world_walls_uses_grid_space(void);
void sdk_world_walls_get_offset(int* out_x, int* out_z);
void sdk_world_walls_get_effective_offset(int* out_x, int* out_z);

/* ================================================================================
 * Wall Position Queries
 * ================================================================================ */

/**
 * Check if a chunk coordinate is on a wall boundary after wall-sharing and
 * thickness rules are applied.
 * @param chunk_coord Chunk coordinate (cx or cz)
 * @return 1 if this chunk is a wall, 0 otherwise
 */
int sdk_world_walls_chunk_is_wall(int chunk_coord);

/**
 * Check if a chunk coordinate is on the west wall of its grid box.
 * @param cx Chunk X coordinate
 * @return 1 if west wall, 0 otherwise
 */
int sdk_world_walls_chunk_is_west_wall(int cx);

/**
 * Check if a chunk coordinate is on the north wall of its grid box.
 * @param cz Chunk Z coordinate  
 * @return 1 if north wall, 0 otherwise
 */
int sdk_world_walls_chunk_is_north_wall(int cz);

/**
 * Get the grid box (wall ring) index for a chunk coordinate.
 * @param chunk_coord Chunk coordinate (cx or cz)
 * @return Grid box index (which ring this chunk belongs to)
 */
int sdk_world_walls_get_box_index(int chunk_coord);

/**
 * Get the local offset within the current grid box.
 * @param chunk_coord Chunk coordinate (cx or cz)
 * @return Position within box [0, ring_size-1]
 */
int sdk_world_walls_get_local_offset(int chunk_coord);

/**
 * Get the origin chunk coordinate of the grid box containing this chunk.
 * @param chunk_coord Chunk coordinate (cx or cz)
 * @return Origin chunk of the containing grid box
 */
int sdk_world_walls_get_box_origin(int chunk_coord);
int sdk_world_walls_chunk_local_axis(int chunk_coord);
int sdk_world_walls_chunk_local_x(int cx);
int sdk_world_walls_chunk_local_z(int cz);
int sdk_world_walls_get_canonical_wall_chunk_owner(int cx,
                                                   int cz,
                                                   uint8_t* out_wall_mask,
                                                   int* out_origin_cx,
                                                   int* out_origin_cz,
                                                   int* out_period_local_x,
                                                   int* out_period_local_z);

/* ================================================================================
 * Convenience Functions
 * ================================================================================ */

/**
 * Get wall ring size in blocks.
 * @return Ring size in blocks (ring_size * CHUNK_WIDTH).
 */
int sdk_world_walls_get_ring_size_blocks(void);

/**
 * Check if walls are enabled for this world.
 * @return true if walls should be generated.
 */
bool sdk_world_walls_enabled(void);

/**
 * Check if wall configuration is initialized.
 * @return true if initialized.
 */
bool sdk_world_walls_config_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_WORLD_WALLS_CONFIG_H */
