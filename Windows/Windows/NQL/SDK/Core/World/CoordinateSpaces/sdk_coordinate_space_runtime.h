#ifndef NQLSDK_COORDINATE_SPACE_RUNTIME_H
#define NQLSDK_COORDINATE_SPACE_RUNTIME_H

#include "sdk_coordinate_space.h"
#include "WallGridTerrainSuperchunkSpace/sdk_space_superchunk_terrain.h"
#include "WallGridTerrainSuperchunkSpace/sdk_space_wall_grid.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SdkDerivedChunkSpaceInfo {
    int cx;
    int cz;
    uint8_t coordinate_system; /* SdkWorldCoordinateSystem */
    uint8_t space_type;        /* SdkCoordinateSpaceType */
    uint8_t is_wall_position;
    uint8_t reserved0;
    int group_x;
    int group_z;
    int local_x;
    int local_z;
} SdkDerivedChunkSpaceInfo;

static inline const char* sdk_coordinate_space_type_name(SdkCoordinateSpaceType space_type)
{
    switch (space_type) {
        case SDK_SPACE_GLOBAL_CHUNK:
            return "global_chunk";
        case SDK_SPACE_SUPERCHUNK_TERRAIN:
            return "superchunk_terrain";
        case SDK_SPACE_WALL_GRID:
            return "wall_grid";
        case SDK_SPACE_WORLD_BLOCK:
            return "world_block";
        case SDK_SPACE_INVALID:
        case SDK_SPACE_COUNT:
        default:
            return "invalid";
    }
}

static inline SdkCoordinateSpaceType sdk_coordinate_space_resolve_chunk_type_from_config(
    const SdkSuperchunkConfig* config,
    int cx,
    int cz)
{
    SdkWorldCoordinateSystem coordinate_system;

    if (!config) return SDK_SPACE_GLOBAL_CHUNK;
    coordinate_system = (SdkWorldCoordinateSystem)config->coordinate_system;

    switch (coordinate_system) {
        case SDK_WORLD_COORDSYS_CHUNK_SYSTEM:
            return SDK_SPACE_GLOBAL_CHUNK;
        case SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM:
            return SDK_SPACE_SUPERCHUNK_TERRAIN;
        case SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM:
            if (config->walls_enabled && sdk_space_wall_grid_chunk_is_wall(cx, cz)) {
                return SDK_SPACE_WALL_GRID;
            }
            return SDK_SPACE_SUPERCHUNK_TERRAIN;
        default:
            return SDK_SPACE_GLOBAL_CHUNK;
    }
}

static inline SdkCoordinateSpaceType sdk_coordinate_space_resolve_chunk_type(int cx, int cz)
{
    return sdk_coordinate_space_resolve_chunk_type_from_config(sdk_superchunk_get_config(), cx, cz);
}

static inline void sdk_coordinate_space_describe_chunk_from_config(
    const SdkSuperchunkConfig* config,
    int cx,
    int cz,
    SdkDerivedChunkSpaceInfo* out_info)
{
    SdkWorldCoordinateSystem coordinate_system;

    if (!out_info) return;
    memset(out_info, 0, sizeof(*out_info));
    out_info->cx = cx;
    out_info->cz = cz;
    if (!config) {
        out_info->coordinate_system = (uint8_t)SDK_WORLD_COORDSYS_CHUNK_SYSTEM;
        out_info->space_type = (uint8_t)SDK_SPACE_GLOBAL_CHUNK;
        return;
    }

    coordinate_system = (SdkWorldCoordinateSystem)config->coordinate_system;
    out_info->coordinate_system = (uint8_t)coordinate_system;
    out_info->space_type =
        (uint8_t)sdk_coordinate_space_resolve_chunk_type_from_config(config, cx, cz);
    out_info->is_wall_position = (config->walls_enabled && sdk_space_wall_grid_chunk_is_wall(cx, cz)) ? 1u : 0u;

    if (out_info->space_type == SDK_SPACE_GLOBAL_CHUNK) {
        out_info->group_x = cx;
        out_info->group_z = cz;
        out_info->local_x = 0;
        out_info->local_z = 0;
        return;
    }

    if (out_info->space_type == SDK_SPACE_WALL_GRID) {
        SdkSpaceWallGridCell cell;
        sdk_space_wall_grid_cell_from_chunk(cx, cz, &cell);
        out_info->group_x = cell.grid_x;
        out_info->group_z = cell.grid_z;
        sdk_space_wall_grid_chunk_local(cx, cz, &out_info->local_x, &out_info->local_z);
        return;
    }

    {
        SdkSpaceTerrainCell cell;
        sdk_space_terrain_cell_from_chunk(cx, cz, &cell);
        out_info->group_x = cell.scx;
        out_info->group_z = cell.scz;
        out_info->local_x = cx - cell.interior_min_cx;
        out_info->local_z = cz - cell.interior_min_cz;
    }
}

static inline void sdk_coordinate_space_describe_chunk(int cx,
                                                       int cz,
                                                       SdkDerivedChunkSpaceInfo* out_info)
{
    sdk_coordinate_space_describe_chunk_from_config(sdk_superchunk_get_config(), cx, cz, out_info);
}

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_COORDINATE_SPACE_RUNTIME_H */
