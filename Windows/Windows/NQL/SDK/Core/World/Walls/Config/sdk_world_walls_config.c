/* ================================================================================
 * sdk_world_walls_config.c -- World-level wall configuration implementation
 *
 * Owns wall-grid sizing, offsets, sharing rules, and physical wall thickness.
 * Superchunk geometry remains terrain-focused and delegates wall-grid math here.
 * ================================================================================ */
#include "sdk_world_walls_config.h"
#include "../../CoordinateSpaces/sdk_coordinate_space.h"
#include "../../Chunks/sdk_chunk.h"
#include <string.h>

#define SDK_WORLD_WALL_FACE_WEST  (1u << 0)
#define SDK_WORLD_WALL_FACE_NORTH (1u << 1)
#define SDK_WORLD_WALL_FACE_EAST  (1u << 2)
#define SDK_WORLD_WALL_FACE_SOUTH (1u << 3)

static SdkWorldWallsConfig g_world_walls_config = {
    .enabled = false,
    .ring_size = 18,
    .wall_thickness_blocks = CHUNK_WIDTH,
    .wall_thickness_chunks = 1,
    .wall_rings_shared = true,
    .use_wall_grid_space = false,
    .offset_x = 0,
    .offset_z = 0,
    .initialized = false
};

static int wall_thickness_chunks_from_blocks(int wall_thickness_blocks)
{
    int clamped_blocks = wall_thickness_blocks;

    if (clamped_blocks <= 0) clamped_blocks = CHUNK_WIDTH;
    if (clamped_blocks > CHUNK_WIDTH) clamped_blocks = CHUNK_WIDTH;
    return (clamped_blocks + CHUNK_WIDTH - 1) / CHUNK_WIDTH;
}

static int detached_min_ring_size(int chunk_span, int wall_thickness_chunks)
{
    if (chunk_span <= 0) chunk_span = 16;
    if (wall_thickness_chunks <= 0) wall_thickness_chunks = 1;
    return chunk_span + wall_thickness_chunks + wall_thickness_chunks;
}

static int effective_period_from_config(const SdkWorldWallsConfig* config)
{
    int wall_thickness_chunks;
    int period;

    if (!config || !config->enabled) return 0;
    if (!config->use_wall_grid_space) {
        return sdk_superchunk_get_chunk_span() + 1;
    }

    wall_thickness_chunks = config->wall_thickness_chunks > 0 ? config->wall_thickness_chunks : 1;
    period = config->ring_size;
    if (config->wall_rings_shared) {
        period -= wall_thickness_chunks;
    }
    if (period <= 0) {
        period = config->ring_size > 0 ? config->ring_size : 1;
    }
    return period;
}

static int local_axis_with_offset(const SdkWorldWallsConfig* config, int chunk_coord)
{
    int offset = config ? config->offset_x : 0;
    int period = effective_period_from_config(config);
    if (period <= 0) return 0;
    return sdk_space_floor_mod(chunk_coord - offset, period);
}

static int axis_is_wall_local(const SdkWorldWallsConfig* config, int local)
{
    int wall_thickness_chunks;
    int period;

    if (!config || !config->enabled) return 0;
    wall_thickness_chunks = config->wall_thickness_chunks > 0 ? config->wall_thickness_chunks : 1;
    period = effective_period_from_config(config);
    if (local < wall_thickness_chunks) {
        return 1;
    }
    if (!config->wall_rings_shared && local >= period - wall_thickness_chunks) {
        return 1;
    }
    return 0;
}

static int axis_box_index_with_offset(const SdkWorldWallsConfig* config, int chunk_coord, int offset)
{
    int period = effective_period_from_config(config);
    if (period <= 0) return 0;
    return sdk_space_floor_div(chunk_coord - offset, period);
}

void sdk_world_walls_config_init(const SdkWorldSaveMeta* meta,
                                  const SdkSuperchunkConfig* sc_config,
                                  SdkWorldCoordinateSystem coord_sys)
{
    int chunk_span = 16;
    int wall_thickness_blocks = CHUNK_WIDTH;
    int wall_thickness_chunks = 1;
    int ring_size;
    bool enabled = false;
    bool use_wall_grid_space = false;
    bool wall_rings_shared = true;

    memset(&g_world_walls_config, 0, sizeof(g_world_walls_config));

    if (sc_config && sc_config->chunk_span > 0) {
        chunk_span = sc_config->chunk_span;
    } else if (meta && meta->superchunk_chunk_span > 0) {
        chunk_span = meta->superchunk_chunk_span;
    }

    if (meta) {
        enabled = meta->walls_enabled ? true : false;
        wall_thickness_blocks = meta->wall_thickness_blocks > 0 ? meta->wall_thickness_blocks : CHUNK_WIDTH;
        wall_rings_shared = meta->wall_rings_shared ? true : false;
        g_world_walls_config.offset_x = meta->wall_grid_offset_x;
        g_world_walls_config.offset_z = meta->wall_grid_offset_z;
    } else if (sc_config) {
        enabled = sc_config->walls_enabled ? true : false;
        wall_thickness_blocks =
            sc_config->wall_thickness_blocks > 0 ? sc_config->wall_thickness_blocks : CHUNK_WIDTH;
        wall_rings_shared = sc_config->wall_rings_shared ? true : false;
        g_world_walls_config.offset_x = sc_config->wall_grid_offset_x;
        g_world_walls_config.offset_z = sc_config->wall_grid_offset_z;
    }

    wall_thickness_chunks = wall_thickness_chunks_from_blocks(wall_thickness_blocks);
    use_wall_grid_space = enabled && sdk_world_coordinate_system_detaches_walls(coord_sys);

    if (use_wall_grid_space) {
        int min_ring_size = detached_min_ring_size(chunk_span, wall_thickness_chunks);
        int requested_ring_size = meta ? meta->wall_grid_size : (sc_config ? sc_config->wall_grid_size : 0);
        ring_size = requested_ring_size > min_ring_size ? requested_ring_size : min_ring_size;
    } else {
        /* Attached and chunk-space worlds keep the classic shared-boundary rhythm. */
        ring_size = chunk_span + 2;
        wall_rings_shared = true;
        g_world_walls_config.offset_x = 0;
        g_world_walls_config.offset_z = 0;
    }

    g_world_walls_config.enabled = enabled;
    g_world_walls_config.ring_size = ring_size;
    g_world_walls_config.wall_thickness_blocks = wall_thickness_blocks > 0 ? wall_thickness_blocks : CHUNK_WIDTH;
    if (g_world_walls_config.wall_thickness_blocks > CHUNK_WIDTH) {
        g_world_walls_config.wall_thickness_blocks = CHUNK_WIDTH;
    }
    g_world_walls_config.wall_thickness_chunks = wall_thickness_chunks_from_blocks(
        g_world_walls_config.wall_thickness_blocks);
    g_world_walls_config.wall_rings_shared = wall_rings_shared;
    g_world_walls_config.use_wall_grid_space = use_wall_grid_space;
    g_world_walls_config.initialized = true;
}

void sdk_world_walls_config_shutdown(void)
{
    memset(&g_world_walls_config, 0, sizeof(g_world_walls_config));
}

int sdk_world_walls_get_ring_size(void)
{
    if (!g_world_walls_config.initialized) {
        return 18;
    }
    return g_world_walls_config.ring_size;
}

int sdk_world_walls_get_interior_span_chunks(void)
{
    int ring_size = sdk_world_walls_get_ring_size();
    int wall_thickness_chunks = sdk_world_walls_get_thickness_chunks();
    int interior = ring_size - wall_thickness_chunks - wall_thickness_chunks;

    if (!g_world_walls_config.initialized || !g_world_walls_config.use_wall_grid_space) {
        return sdk_superchunk_get_chunk_span();
    }
    return interior > 0 ? interior : sdk_superchunk_get_chunk_span();
}

int sdk_world_walls_get_period(void)
{
    if (!g_world_walls_config.initialized) {
        return sdk_superchunk_get_chunk_span() + 1;
    }
    if (!g_world_walls_config.enabled) {
        return sdk_superchunk_get_chunk_span() + 1;
    }
    return effective_period_from_config(&g_world_walls_config);
}

bool sdk_world_walls_are_rings_shared(void)
{
    if (!g_world_walls_config.initialized) return true;
    return g_world_walls_config.wall_rings_shared;
}

int sdk_world_walls_get_thickness_chunks(void)
{
    if (!g_world_walls_config.initialized) return 1;
    return g_world_walls_config.wall_thickness_chunks > 0 ? g_world_walls_config.wall_thickness_chunks : 1;
}

int sdk_world_walls_get_thickness_blocks(void)
{
    if (!g_world_walls_config.initialized) return CHUNK_WIDTH;
    return g_world_walls_config.wall_thickness_blocks > 0
        ? g_world_walls_config.wall_thickness_blocks
        : CHUNK_WIDTH;
}

bool sdk_world_walls_uses_grid_space(void)
{
    if (!g_world_walls_config.initialized) return false;
    return g_world_walls_config.use_wall_grid_space;
}

void sdk_world_walls_get_offset(int* out_x, int* out_z)
{
    if (!g_world_walls_config.initialized) {
        if (out_x) *out_x = 0;
        if (out_z) *out_z = 0;
        return;
    }
    if (out_x) *out_x = g_world_walls_config.offset_x;
    if (out_z) *out_z = g_world_walls_config.offset_z;
}

void sdk_world_walls_get_effective_offset(int* out_x, int* out_z)
{
    if (!g_world_walls_config.initialized || !g_world_walls_config.use_wall_grid_space) {
        if (out_x) *out_x = 0;
        if (out_z) *out_z = 0;
        return;
    }
    sdk_world_walls_get_offset(out_x, out_z);
}

int sdk_world_walls_chunk_is_wall(int chunk_coord)
{
    int local;
    int offset_x = 0;

    if (!g_world_walls_config.initialized || !g_world_walls_config.enabled) return 0;
    sdk_world_walls_get_effective_offset(&offset_x, NULL);
    local = sdk_space_floor_mod(chunk_coord - offset_x, sdk_world_walls_get_period());
    return axis_is_wall_local(&g_world_walls_config, local);
}

int sdk_world_walls_chunk_is_west_wall(int cx)
{
    int local;
    int offset_x = 0;

    if (!g_world_walls_config.initialized || !g_world_walls_config.enabled) return 0;
    sdk_world_walls_get_effective_offset(&offset_x, NULL);
    local = sdk_space_floor_mod(cx - offset_x, sdk_world_walls_get_period());
    return local < sdk_world_walls_get_thickness_chunks();
}

int sdk_world_walls_chunk_is_north_wall(int cz)
{
    int local;
    int offset_z = 0;

    if (!g_world_walls_config.initialized || !g_world_walls_config.enabled) return 0;
    sdk_world_walls_get_effective_offset(NULL, &offset_z);
    local = sdk_space_floor_mod(cz - offset_z, sdk_world_walls_get_period());
    return local < sdk_world_walls_get_thickness_chunks();
}

int sdk_world_walls_get_box_index(int chunk_coord)
{
    int offset_x = 0;

    if (!g_world_walls_config.initialized) return 0;
    sdk_world_walls_get_effective_offset(&offset_x, NULL);
    return axis_box_index_with_offset(&g_world_walls_config, chunk_coord, offset_x);
}

int sdk_world_walls_get_local_offset(int chunk_coord)
{
    int offset_x = 0;

    if (!g_world_walls_config.initialized) return 0;
    sdk_world_walls_get_effective_offset(&offset_x, NULL);
    return sdk_space_floor_mod(chunk_coord - offset_x, sdk_world_walls_get_period());
}

int sdk_world_walls_get_box_origin(int chunk_coord)
{
    int offset_x = 0;
    int box_index;

    if (!g_world_walls_config.initialized) return 0;
    sdk_world_walls_get_effective_offset(&offset_x, NULL);
    box_index = axis_box_index_with_offset(&g_world_walls_config, chunk_coord, offset_x);
    return box_index * sdk_world_walls_get_period() + offset_x;
}

int sdk_world_walls_chunk_local_axis(int chunk_coord)
{
    return sdk_world_walls_get_local_offset(chunk_coord);
}

int sdk_world_walls_chunk_local_x(int cx)
{
    int offset_x = 0;
    sdk_world_walls_get_effective_offset(&offset_x, NULL);
    return sdk_space_floor_mod(cx - offset_x, sdk_world_walls_get_period());
}

int sdk_world_walls_chunk_local_z(int cz)
{
    int offset_z = 0;
    sdk_world_walls_get_effective_offset(NULL, &offset_z);
    return sdk_space_floor_mod(cz - offset_z, sdk_world_walls_get_period());
}

int sdk_world_walls_get_canonical_wall_chunk_owner(int cx,
                                                   int cz,
                                                   uint8_t* out_wall_mask,
                                                   int* out_origin_cx,
                                                   int* out_origin_cz,
                                                   int* out_period_local_x,
                                                   int* out_period_local_z)
{
    int offset_x = 0;
    int offset_z = 0;
    int local_x;
    int local_z;
    int period;
    int thickness_chunks;
    uint8_t wall_mask = 0u;

    if (!g_world_walls_config.initialized || !g_world_walls_config.enabled) {
        if (out_wall_mask) *out_wall_mask = 0u;
        return 0;
    }

    sdk_world_walls_get_effective_offset(&offset_x, &offset_z);
    period = sdk_world_walls_get_period();
    thickness_chunks = sdk_world_walls_get_thickness_chunks();
    local_x = sdk_space_floor_mod(cx - offset_x, period);
    local_z = sdk_space_floor_mod(cz - offset_z, period);

    if (local_x < thickness_chunks) {
        wall_mask |= SDK_WORLD_WALL_FACE_WEST;
    } else if (!g_world_walls_config.wall_rings_shared && local_x >= period - thickness_chunks) {
        wall_mask |= SDK_WORLD_WALL_FACE_EAST;
    }

    if (local_z < thickness_chunks) {
        wall_mask |= SDK_WORLD_WALL_FACE_NORTH;
    } else if (!g_world_walls_config.wall_rings_shared && local_z >= period - thickness_chunks) {
        wall_mask |= SDK_WORLD_WALL_FACE_SOUTH;
    }

    if (out_wall_mask) *out_wall_mask = wall_mask;
    if (out_origin_cx) {
        *out_origin_cx = axis_box_index_with_offset(&g_world_walls_config, cx, offset_x) * period + offset_x;
    }
    if (out_origin_cz) {
        *out_origin_cz = axis_box_index_with_offset(&g_world_walls_config, cz, offset_z) * period + offset_z;
    }
    if (out_period_local_x) *out_period_local_x = local_x;
    if (out_period_local_z) *out_period_local_z = local_z;
    return wall_mask != 0u;
}

int sdk_world_walls_get_ring_size_blocks(void)
{
    return sdk_world_walls_get_ring_size() * CHUNK_WIDTH;
}

bool sdk_world_walls_enabled(void)
{
    if (!g_world_walls_config.initialized) return false;
    return g_world_walls_config.enabled;
}

bool sdk_world_walls_config_is_initialized(void)
{
    return g_world_walls_config.initialized;
}
