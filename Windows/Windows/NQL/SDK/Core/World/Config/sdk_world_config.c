/* ======================================================================
 * sdk_world_config.c - Unified World Configuration Implementation
 * ====================================================================== */
#include "sdk_world_config.h"
#include "../Superchunks/Config/sdk_superchunk_config.h"
#include "../Persistence/sdk_world_tooling.h"
#include "../Walls/Config/sdk_world_walls_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static SdkWorldConfig g_world_config = {
    .coordinate_system = SDK_WORLD_COORDSYS_CHUNK_SYSTEM,
    .has_walls = false,
    .initialized = false
};

void sdk_world_config_init(const SdkWorldSaveMeta* meta)
{
    SdkWorldCoordinateSystem coord_sys;
    SdkSuperchunkConfig sc_config;
    
    if (!meta) {
        fprintf(stderr, "Error: sdk_world_config_init called with NULL meta\n");
        exit(1);
    }
    
    coord_sys = (SdkWorldCoordinateSystem)meta->coordinate_system;
    if (!sdk_world_coordinate_system_is_valid(coord_sys)) {
        fprintf(stderr, "Error: Invalid coordinate system %d in world meta\n", coord_sys);
        exit(1);
    }
    
    g_world_config.coordinate_system = coord_sys;
    g_world_config.has_walls = meta->walls_enabled;
    
    /* Convert meta to superchunk config */
    sdk_world_meta_to_superchunk_config(meta, &sc_config);
    /* Initialize superchunk config from meta */
    sdk_superchunk_set_config(&sc_config);
    sdk_world_walls_config_init(meta, &sc_config, coord_sys);
    
    g_world_config.initialized = true;
}

void sdk_world_config_shutdown(void)
{
    sdk_world_walls_config_shutdown();
    g_world_config.coordinate_system = SDK_WORLD_COORDSYS_CHUNK_SYSTEM;
    g_world_config.has_walls = false;
    g_world_config.initialized = false;
}

SdkWorldCoordinateSystem sdk_world_get_coordinate_system(void)
{
    if (!g_world_config.initialized) {
        return SDK_WORLD_COORDSYS_CHUNK_SYSTEM;
    }
    return g_world_config.coordinate_system;
}

bool sdk_world_config_is_initialized(void)
{
    return g_world_config.initialized;
}

bool sdk_world_has_walls(void)
{
    return g_world_config.has_walls;
}

bool sdk_world_has_superchunks(void)
{
    SdkWorldCoordinateSystem coord_sys = sdk_world_get_coordinate_system();
    /* Explicitly check for known superchunk coordinate systems.
     * This is future-proof: new coordinate systems won't automatically
     * be treated as superchunk systems. */
    return coord_sys == SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM ||
           coord_sys == SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
}
