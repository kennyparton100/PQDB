/* ======================================================================
 * sdk_world_config.h - Unified World Configuration
 * ====================================================================== */
#ifndef SDK_WORLD_CONFIG_H
#define SDK_WORLD_CONFIG_H

#include "../../sdk_types.h"
#include "../Persistence/sdk_world_tooling.h"

typedef struct SdkWorldConfig {
    SdkWorldCoordinateSystem coordinate_system;
    bool has_walls;    
    bool initialized;
} SdkWorldConfig;

/**
 * Initialize world configuration from save metadata.
 * Called during world load/creation.
 */
void sdk_world_config_init(const SdkWorldSaveMeta* meta);

/**
 * Shutdown world configuration.
 * Called during world unload.
 */
void sdk_world_config_shutdown(void);

/**
 * Get the world's coordinate system.
 * This is the authoritative accessor for world topology.
 */
SdkWorldCoordinateSystem sdk_world_get_coordinate_system(void);

/**
 * Check if the world has walls.
 */
bool sdk_world_has_walls(void);

/**
 * Check if world configuration is initialized.
 */
bool sdk_world_config_is_initialized(void);

/**
 * Check if the current world uses superchunks.
 * This is a convenience helper that checks the coordinate system.
 */
bool sdk_world_has_superchunks(void);

#endif /* SDK_WORLD_CONFIG_H */
