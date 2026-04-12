/**
 * test_sdk_water_simulation.c -- Focused regression checks for runtime water simulation.
 *
 * Build (MSVC, from the Windows/ directory):
 *   cl /O2 /W4 /I NQL/SDK /I NQL/SDK/Core ^
 *      Tests/test_sdk_water_simulation.c ^
 *      NQL/SDK/Core/sdk_block.c ^
 *      NQL/SDK/Core/sdk_chunk.c ^
 *      NQL/SDK/Core/sdk_chunk_manager.c ^
 *      NQL/SDK/Core/sdk_simulation.c ^
 *      /Fe:test_sdk_water_simulation.exe
 */
#include "../NQL/SDK/Core/sdk_block.h"
#include "../NQL/SDK/Core/sdk_chunk_manager.h"
#include "../NQL/SDK/Core/sdk_simulation.h"
#include "test_harness.h"

#include <stdlib.h>

static SdkChunk* reserve_test_chunk(SdkChunkManager* cm, int cx, int cz, SdkChunkResidencyRole role)
{
    SdkChunkResidentSlot* slot;

    ASSERT_TRUE(cm != NULL);
    slot = sdk_chunk_manager_reserve_slot(cm, cx, cz, role);
    ASSERT_TRUE(slot != NULL);
    if (!slot) return NULL;

    sdk_chunk_init(&slot->chunk, cx, cz);
    sdk_chunk_manager_rebuild_lookup(cm);
    return &slot->chunk;
}

static void seed_support_plane(SdkChunk* chunk, int y, int min_x, int max_x, int min_z, int max_z)
{
    int x;
    int z;

    ASSERT_TRUE(chunk != NULL);
    if (!chunk) return;
    for (z = min_z; z <= max_z; ++z) {
        for (x = min_x; x <= max_x; ++x) {
            sdk_chunk_set_block(chunk, x, y, z, BLOCK_STONE);
        }
    }
}

static SdkChunkManager* create_test_chunk_manager(void)
{
    SdkChunkManager* cm = (SdkChunkManager*)malloc(sizeof(SdkChunkManager));
    ASSERT_TRUE(cm != NULL);
    if (!cm) return NULL;
    sdk_chunk_manager_init(cm);
    cm->cam_cx = 0;
    cm->cam_cz = 0;
    return cm;
}

TEST(partial_fill_roundtrip)
{
    SdkChunk chunk;
    SdkSimCellKey a = sdk_simulation_pack_local_key(2, 5, 3);
    SdkSimCellKey b = sdk_simulation_pack_local_key(10, 5, 8);
    char encoded[128];
    char* roundtrip;
    int material = (int)sdk_block_get_runtime_material(BLOCK_WATER);

    sdk_chunk_init(&chunk, 0, 0);
    sdk_chunk_set_block(&chunk, 2, 5, 3, BLOCK_WATER);
    sdk_chunk_set_block(&chunk, 10, 5, 8, BLOCK_WATER);
    sprintf_s(encoded, sizeof(encoded),
              "%u:%u:%u:%u;%u:%u:%u:%u;",
              (unsigned)a, 96u, (unsigned)material, 3u,
              (unsigned)b, 180u, (unsigned)material, 5u);

    ASSERT_TRUE(sdk_simulation_decode_chunk_fluids(&chunk, encoded));
    ASSERT_UINT_EQ(sdk_simulation_get_fluid_fill(&chunk, 2, 5, 3), 96u);
    ASSERT_UINT_EQ(sdk_simulation_get_fluid_fill(&chunk, 10, 5, 8), 180u);

    roundtrip = sdk_simulation_encode_chunk_fluids(&chunk);
    ASSERT_TRUE(roundtrip != NULL);
    if (roundtrip) {
        ASSERT_STR_CONTAINS(roundtrip, "96");
        ASSERT_STR_CONTAINS(roundtrip, "180");
        free(roundtrip);
    }

    sdk_chunk_free(&chunk);
}

TEST(lateral_equalization_moves_water)
{
    SdkChunkManager* cm;
    SdkChunk* chunk;

    cm = create_test_chunk_manager();
    ASSERT_TRUE(cm != NULL);
    if (!cm) return;

    chunk = reserve_test_chunk(cm, 0, 0, SDK_CHUNK_ROLE_PRIMARY);
    seed_support_plane(chunk, 0, 0, 2, 0, 2);
    sdk_chunk_set_block(chunk, 0, 1, 1, BLOCK_STONE);
    sdk_chunk_set_block(chunk, 1, 1, 0, BLOCK_STONE);
    sdk_chunk_set_block(chunk, 1, 1, 2, BLOCK_STONE);
    sdk_chunk_set_block(chunk, 1, 1, 1, BLOCK_WATER);

    sdk_simulation_enqueue_world(cm, 1, 1, 1);
    sdk_simulation_tick_chunk_manager(cm, 1);

    ASSERT_TRUE(sdk_simulation_get_fluid_fill(chunk, 2, 1, 1) > 0u);

    sdk_chunk_manager_shutdown(cm);
    free(cm);
}

TEST(cross_chunk_transfer_updates_neighbor)
{
    SdkChunkManager* cm;
    SdkChunk* west;
    SdkChunk* east;

    cm = create_test_chunk_manager();
    ASSERT_TRUE(cm != NULL);
    if (!cm) return;

    west = reserve_test_chunk(cm, 0, 0, SDK_CHUNK_ROLE_PRIMARY);
    east = reserve_test_chunk(cm, 1, 0, SDK_CHUNK_ROLE_PRIMARY);
    seed_support_plane(west, 0, CHUNK_WIDTH - 2, CHUNK_WIDTH - 1, 0, 2);
    seed_support_plane(east, 0, 0, 1, 0, 2);
    sdk_chunk_set_block(west, CHUNK_WIDTH - 2, 1, 1, BLOCK_STONE);
    sdk_chunk_set_block(west, CHUNK_WIDTH - 1, 1, 0, BLOCK_STONE);
    sdk_chunk_set_block(west, CHUNK_WIDTH - 1, 1, 2, BLOCK_STONE);
    sdk_chunk_set_block(west, CHUNK_WIDTH - 1, 1, 1, BLOCK_WATER);

    sdk_simulation_enqueue_world(cm, CHUNK_WIDTH - 1, 1, 1);
    sdk_simulation_tick_chunk_manager(cm, 1);

    ASSERT_TRUE(sdk_simulation_get_fluid_fill(east, 0, 1, 1) > 0u);

    sdk_chunk_manager_shutdown(cm);
    free(cm);
}

TEST(adaptive_budget_processes_more_than_base_budget)
{
    SdkChunkManager* cm;
    SdkChunk* chunk;
    SdkFluidDebugInfo info;
    int x;

    cm = create_test_chunk_manager();
    ASSERT_TRUE(cm != NULL);
    if (!cm) return;

    chunk = reserve_test_chunk(cm, 0, 0, SDK_CHUNK_ROLE_PRIMARY);
    seed_support_plane(chunk, 0, 0, 31, 0, 0);
    for (x = 0; x < 32; ++x) {
        sdk_chunk_set_block(chunk, x, 1, 0, BLOCK_WATER);
        sdk_simulation_enqueue_world(cm, x, 1, 0);
    }

    sdk_simulation_tick_chunk_manager(cm, 4);
    sdk_simulation_get_debug_info(cm, &info);

    ASSERT_TRUE(info.tick_processed > 4u);

    sdk_chunk_manager_shutdown(cm);
    free(cm);
}

TEST(large_reservoir_prefers_bulk_solver)
{
    SdkChunkManager* cm;
    SdkChunk* chunk;
    SdkFluidDebugInfo info;
    int x;
    int z;

    cm = create_test_chunk_manager();
    ASSERT_TRUE(cm != NULL);
    if (!cm) return;
    sdk_simulation_invalidate_reservoirs();

    chunk = reserve_test_chunk(cm, 0, 0, SDK_CHUNK_ROLE_PRIMARY);
    seed_support_plane(chunk, 0, 0, 15, 0, 15);
    for (z = 0; z < 16; ++z) {
        for (x = 0; x < 16; ++x) {
            sdk_chunk_set_block(chunk, x, 1, z, BLOCK_WATER);
        }
    }

    sdk_chunk_set_block(chunk, 0, 1, 0, BLOCK_AIR);
    sdk_simulation_on_block_changed(cm, 0, 1, 0, BLOCK_WATER, BLOCK_AIR);
    sdk_simulation_get_debug_info(cm, &info);

    ASSERT_INT_EQ(info.mechanism, SDK_FLUID_DEBUG_MECH_BULK_RESERVOIR);
    ASSERT_STR_CONTAINS(info.reason, "BULK");

    sdk_simulation_shutdown();
    sdk_chunk_manager_shutdown(cm);
    free(cm);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("SDK Water Simulation Tests\n");
    RUN_TEST(partial_fill_roundtrip);
    RUN_TEST(lateral_equalization_moves_water);
    RUN_TEST(cross_chunk_transfer_updates_neighbor);
    RUN_TEST(adaptive_budget_processes_more_than_base_budget);
    RUN_TEST(large_reservoir_prefers_bulk_solver);
    test_print_summary();
    return g_test_failed ? 1 : 0;
}
