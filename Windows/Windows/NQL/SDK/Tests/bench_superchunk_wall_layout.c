#include <assert.h>
#include <stdio.h>

#include "../Core/API/Session/sdk_api_session_bootstrap_policy.h"
#include "../Core/World/Superchunks/Config/sdk_superchunk_config.h"
#include "../Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h"

static void expect_wall_layout_16(void)
{
    SdkSuperchunkCell cell;
    uint8_t wall_mask = 0u;
    int local_x = 0;
    int local_z = 0;

    sdk_superchunk_cell_from_chunk(-1, -1, &cell);
    assert(cell.scx == -1);
    assert(cell.scz == -1);
    assert(cell.origin_cx == -17);
    assert(cell.origin_cz == -17);
    assert(cell.interior_max_cx == -1);
    assert(cell.interior_max_cz == -1);

    assert(sdk_superchunk_get_wall_period() == 17);
    assert(sdk_superchunk_chunk_is_wall_anywhere(0, 1));
    assert(sdk_superchunk_chunk_is_wall_anywhere(17, 1));
    assert(sdk_superchunk_chunk_is_wall_anywhere(1, 17));
    assert(sdk_superchunk_chunk_is_wall_anywhere(-17, 1));
    assert(!sdk_superchunk_chunk_is_wall_anywhere(16, 16));
    assert(!sdk_superchunk_chunk_is_wall_anywhere(18, 18));

    sdk_superchunk_chunk_local_interior_coords(1, 1, &local_x, &local_z);
    assert(local_x == 0);
    assert(local_z == 0);
    sdk_superchunk_chunk_local_interior_coords(1, 0, &local_x, &local_z);
    assert(local_x == 0);
    assert(local_z == -1);
    sdk_superchunk_chunk_local_interior_coords(0, 0, &local_x, &local_z);
    assert(local_x == -1);
    assert(local_z == -1);
    sdk_superchunk_chunk_local_interior_coords(-16, -16, &local_x, &local_z);
    assert(local_x == 0);
    assert(local_z == 0);
    sdk_superchunk_chunk_local_interior_coords(-17, -17, &local_x, &local_z);
    assert(local_x == -1);
    assert(local_z == -1);

    assert(sdk_superchunk_chunk_in_interior(0, 0, 1, 1));
    assert(sdk_superchunk_chunk_in_interior(0, 0, 16, 16));
    assert(!sdk_superchunk_chunk_in_interior(0, 0, 0, 1));
    assert(!sdk_superchunk_chunk_in_interior(0, 0, 17, 16));

    assert(sdk_superchunk_chunk_is_active_wall(0, 0, 0, 8));
    assert(sdk_superchunk_chunk_is_active_wall(0, 0, 17, 8));
    assert(sdk_superchunk_chunk_is_active_wall(0, 0, 8, 0));
    assert(sdk_superchunk_chunk_is_active_wall(0, 0, 8, 17));
    assert(sdk_superchunk_chunk_is_active_wall_corner(0, 0, 0, 0));
    assert(sdk_superchunk_chunk_is_active_wall_edge(0, 0, 0, 8));
    assert(!sdk_superchunk_chunk_is_active_wall(0, 0, 8, 8));

    assert(sdk_superchunk_chunk_is_active_wall_support(0, 0, -1, 8));
    assert(sdk_superchunk_chunk_is_active_wall_support(0, 0, 1, 8));
    assert(sdk_superchunk_chunk_is_active_wall_support(0, 0, 16, 8));
    assert(sdk_superchunk_chunk_is_active_wall_support(0, 0, 18, 8));
    assert(!sdk_superchunk_chunk_is_active_wall_support(0, 0, 8, 8));

    assert(sdk_superchunk_gate_support_contains_chunk_run(6));
    assert(sdk_superchunk_gate_support_contains_chunk_run(7));
    assert(sdk_superchunk_gate_support_contains_chunk_run(8));
    assert(sdk_superchunk_gate_support_contains_chunk_run(9));
    assert(!sdk_superchunk_gate_support_contains_chunk_run(5));
    assert(!sdk_superchunk_gate_support_contains_chunk_run(10));
    assert(sdk_superchunk_gate_intersects_chunk_run(7));
    assert(sdk_superchunk_gate_intersects_chunk_run(8));
    assert(!sdk_superchunk_gate_intersects_chunk_run(6));
    assert(!sdk_superchunk_gate_intersects_chunk_run(9));
    assert(sdk_superchunk_gate_contains_block_run(sdk_superchunk_get_gate_start_block()));
    assert(sdk_superchunk_gate_contains_block_run(sdk_superchunk_get_gate_end_block()));
    assert(!sdk_superchunk_gate_contains_block_run(sdk_superchunk_get_gate_start_block() - 1));
    assert(!sdk_superchunk_gate_contains_block_run(sdk_superchunk_get_gate_end_block() + 1));

    assert(sdk_superchunk_get_canonical_wall_chunk_owner(0, 0, &wall_mask, NULL, NULL, NULL, NULL));
    assert((wall_mask & SDK_SUPERCHUNK_WALL_FACE_WEST) != 0u);
    assert((wall_mask & SDK_SUPERCHUNK_WALL_FACE_NORTH) != 0u);
    assert(sdk_superchunk_get_canonical_wall_chunk_owner(-17, 0, &wall_mask, NULL, NULL, NULL, NULL));
    assert((wall_mask & SDK_SUPERCHUNK_WALL_FACE_WEST) != 0u);
}

static void expect_wall_layout_32(void)
{
    SdkSuperchunkCell cell;

    sdk_superchunk_cell_from_index(0, 0, &cell);
    assert(cell.origin_cx == 0);
    assert(cell.origin_cz == 0);
    assert(cell.east_cx == 33);
    assert(cell.south_cz == 33);
    assert(cell.interior_min_cx == 1);
    assert(cell.interior_max_cx == 32);

    assert(sdk_superchunk_get_wall_period() == 33);
    assert(sdk_superchunk_chunk_is_wall_anywhere(0, 1));
    assert(sdk_superchunk_chunk_is_wall_anywhere(33, 1));
    assert(sdk_superchunk_chunk_is_wall_anywhere(66, 1));
    assert(sdk_superchunk_chunk_is_wall_anywhere(-33, 1));
    assert(!sdk_superchunk_chunk_is_wall_anywhere(32, 32));
    assert(!sdk_superchunk_chunk_is_wall_anywhere(34, 34));

    assert(sdk_superchunk_chunk_in_interior(0, 0, 1, 1));
    assert(sdk_superchunk_chunk_in_interior(0, 0, 32, 32));
    assert(!sdk_superchunk_chunk_in_interior(0, 0, 0, 1));
    assert(!sdk_superchunk_chunk_in_interior(0, 0, 33, 1));
}

static void expect_config_normalization(void)
{
    SdkSuperchunkConfig config = {0};

    config.enabled = false;
    config.chunk_span = 0;
    config.walls_enabled = true;
    config.walls_detached = true;
    config.wall_grid_size = 0;
    sdk_superchunk_normalize_config(&config);
    assert(!config.enabled);
    assert(config.chunk_span == 16);
    assert(!config.walls_enabled);
    assert(!config.walls_detached);
    assert(config.wall_grid_size == 16);

    config.enabled = true;
    config.chunk_span = 16;
    config.walls_enabled = true;
    config.walls_detached = true;
    config.wall_grid_size = 0;
    sdk_superchunk_normalize_config(&config);
    assert(config.enabled);
    assert(config.walls_enabled);
    assert(config.walls_detached);
    assert(config.wall_grid_size == 18);

    config.enabled = true;
    config.chunk_span = 32;
    config.walls_enabled = true;
    config.walls_detached = false;
    config.wall_grid_size = 123;
    sdk_superchunk_normalize_config(&config);
    assert(config.wall_grid_size == 32);
}

static void expect_bootstrap_sync_policy(void)
{
    SdkChunkResidencyTarget target = {0};

    target.cx = 8;
    target.cz = 8;
    target.role = (uint8_t)SDK_CHUNK_ROLE_PRIMARY;

    assert(sdk_bootstrap_target_is_sync_primary(&target, 0, 0, 1, 1));
    assert(!sdk_bootstrap_target_is_sync_primary(&target, 0, 0, 1, 0));

    target.cx = 1;
    target.cz = 0;
    assert(sdk_bootstrap_target_is_sync_primary(&target, 0, 0, 1, 0));
    assert(sdk_bootstrap_target_is_sync_safety(&target, 0, 0, 1));

    target.role = (uint8_t)SDK_CHUNK_ROLE_WALL_SUPPORT;
    assert(!sdk_bootstrap_target_is_sync_primary(&target, 0, 0, 1, 0));
    assert(sdk_bootstrap_target_is_sync_safety(&target, 0, 0, 1));

    target.cx = 3;
    target.cz = 0;
    assert(!sdk_bootstrap_target_is_sync_safety(&target, 0, 0, 1));
}

int main(void)
{
    SdkSuperchunkConfig config = {
        true, 16, true, true, 18, 123, -456
    };

    expect_config_normalization();
    expect_bootstrap_sync_policy();

    sdk_superchunk_set_config(&config);
    expect_wall_layout_16();

    config.chunk_span = 32;
    sdk_superchunk_set_config(&config);
    expect_wall_layout_32();

    puts("bench_superchunk_wall_layout: PASS");
    return 0;
}
