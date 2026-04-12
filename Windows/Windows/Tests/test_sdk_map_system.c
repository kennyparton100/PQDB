/**
 * test_sdk_map_system.c -- Map cache, coloring, and bulk scheduler checks.
 */
#include "../NQL/SDK/Core/sdk_api_internal.h"
#include "../NQL/SDK/Core/sdk_worldgen_column_internal.h"
#include "test_harness.h"

extern int build_superchunk_map_tile_cache_path(int origin_x, int origin_z, char* out_path, size_t out_path_len);
extern int load_superchunk_map_tile_from_disk(int origin_x, int origin_z, uint32_t* out_pixels);
extern void save_superchunk_map_tile_to_disk(int origin_x, int origin_z, const uint32_t* pixels);
extern int map_is_wall_band_local(int local_x, int local_z);
extern uint32_t map_wall_color_for_local(int local_x, int local_z);
extern SuperchunkMapCacheEntry* request_superchunk_map_cache_entry_async(int origin_x, int origin_z, bool allow_generation);

static void make_unique_world_id(char* out_id, size_t out_id_len, const char* tag)
{
    static uint32_t counter = 1u;
    sprintf_s(out_id, out_id_len, "map_%s_%lu_%u", tag ? tag : "test",
              (unsigned long)GetCurrentProcessId(), (unsigned)counter++);
}

static void set_test_cache_identity(const char* world_id, uint32_t seed)
{
    g_sdk.world_save_id[0] = '\0';
    if (world_id && world_id[0]) {
        strcpy_s(g_sdk.world_save_id, sizeof(g_sdk.world_save_id), world_id);
    }
    g_sdk.world_seed = seed;
}

static void init_test_world_desc(SdkWorldDesc* out_desc, uint32_t seed)
{
    memset(out_desc, 0, sizeof(*out_desc));
    out_desc->seed = seed;
    out_desc->sea_level = 192;
    out_desc->macro_cell_size = SDK_WORLDGEN_MACRO_CELL_BLOCKS;
}

static uint32_t opaque_face_color(BlockType type)
{
    return sdk_block_get_face_color(type, 3) | 0xFF000000u;
}

static uint32_t color_distance_sq(uint32_t lhs, uint32_t rhs)
{
    int dr = (int)(lhs & 0xFFu) - (int)(rhs & 0xFFu);
    int dg = (int)((lhs >> 8) & 0xFFu) - (int)((rhs >> 8) & 0xFFu);
    int db = (int)((lhs >> 16) & 0xFFu) - (int)((rhs >> 16) & 0xFFu);
    return (uint32_t)(dr * dr + dg * dg + db * db);
}

static void init_profile(SdkTerrainColumnProfile* profile)
{
    memset(profile, 0, sizeof(*profile));
    profile->surface_height = 192;
    profile->water_height = 192;
    profile->water_surface_class = SURFACE_WATER_NONE;
    profile->surface_sediment = SEDIMENT_RESIDUAL_SOIL;
    profile->terrain_province = TERRAIN_PROVINCE_DYNAMIC_COAST;
}

static int classify_generated_chunk_column(const SdkChunk* chunk,
                                           int lx,
                                           int lz,
                                           SdkMapExactColumnSample* out_sample)
{
    const uint8_t* blocks;
    uint32_t column_offset;
    int surface_y = -1;
    BlockType surface_block = BLOCK_AIR;

    if (!chunk || !chunk->blocks || !out_sample) return 0;
    memset(out_sample, 0, sizeof(*out_sample));
    blocks = chunk->blocks;
    column_offset = (uint32_t)lz * CHUNK_WIDTH + (uint32_t)lx;

    for (int ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
        BlockType block =
            (BlockType)blocks[(uint32_t)ly * CHUNK_BLOCKS_PER_LAYER + column_offset];
        uint32_t flags;

        if (block == BLOCK_AIR) continue;
        flags = sdk_block_get_behavior_flags(block);
        if ((flags & SDK_BLOCK_BEHAVIOR_FLUID) != 0u ||
            block == BLOCK_ICE ||
            block == BLOCK_SEA_ICE) {
            surface_y = ly;
            surface_block = block;
            break;
        }
        if (!sdk_block_is_solid(block)) continue;
        out_sample->kind = SDK_MAP_EXACT_COLUMN_DRY;
        out_sample->top_block = block;
        out_sample->land_height = (uint16_t)ly;
        return 1;
    }

    if (surface_y < 0) return 0;

    out_sample->water_height = (uint16_t)surface_y;
    out_sample->top_block = surface_block;
    if (surface_block == BLOCK_SEA_ICE) {
        out_sample->kind = SDK_MAP_EXACT_COLUMN_PERENNIAL_ICE;
    } else if (surface_block == BLOCK_ICE) {
        out_sample->kind = SDK_MAP_EXACT_COLUMN_SEASONAL_ICE;
    } else {
        out_sample->kind = SDK_MAP_EXACT_COLUMN_OPEN_WATER;
    }

    for (int ly = surface_y - 1; ly >= 0; --ly) {
        BlockType block =
            (BlockType)blocks[(uint32_t)ly * CHUNK_BLOCKS_PER_LAYER + column_offset];
        uint32_t flags;

        if (block == BLOCK_AIR) continue;
        flags = sdk_block_get_behavior_flags(block);
        if ((flags & SDK_BLOCK_BEHAVIOR_FLUID) != 0u) continue;
        if (!sdk_block_is_solid(block)) continue;
        out_sample->floor_block = block;
        out_sample->land_height = (uint16_t)ly;
        out_sample->water_depth = (uint16_t)(surface_y - ly);
        return 1;
    }

    out_sample->floor_block = BLOCK_AIR;
    out_sample->land_height = (uint16_t)surface_y;
    out_sample->water_depth = 1u;
    return 1;
}

TEST(cache_version_invalidates_old_tiles)
{
    char world_id[64];
    char tile_path[MAX_PATH];
    uint32_t pixels[HUD_MAP_PIXEL_COUNT];
    SdkPersistedMapTile tile;

    make_unique_world_id(world_id, sizeof(world_id), "cache");
    set_test_cache_identity(world_id, 0xA1B2C3D4u);
    ASSERT_TRUE(build_superchunk_map_tile_cache_path(0, 0, tile_path, sizeof(tile_path)));

    memset(&tile, 0, sizeof(tile));
    tile.magic = SDK_MAP_TILE_CACHE_MAGIC;
    tile.version = SDK_MAP_TILE_CACHE_VERSION - 1u;
    tile.build_kind = SDK_MAP_BUILD_EXACT_OFFLINE;
    tile.seed = g_sdk.world_seed;
    tile.origin_x = 0;
    tile.origin_z = 0;
    tile.dim = HUD_MAP_DIM;
    tile.pixels[0] = 0xFF123456u;
    {
        FILE* file = fopen(tile_path, "wb");
        ASSERT_TRUE(file != NULL);
        fwrite(&tile, 1u, sizeof(tile), file);
        fclose(file);
    }
    ASSERT_FALSE(load_superchunk_map_tile_from_disk(0, 0, pixels));

    tile.version = SDK_MAP_TILE_CACHE_VERSION;
    tile.build_kind = SDK_MAP_BUILD_INTERACTIVE_FALLBACK;
    tile.pixels[0] = 0xFF445566u;
    {
        FILE* file = fopen(tile_path, "wb");
        ASSERT_TRUE(file != NULL);
        fwrite(&tile, 1u, sizeof(tile), file);
        fclose(file);
    }
    ASSERT_FALSE(load_superchunk_map_tile_from_disk(0, 0, pixels));

    tile.build_kind = SDK_MAP_BUILD_EXACT_OFFLINE;
    tile.pixels[0] = 0xFF654321u;
    {
        FILE* file = fopen(tile_path, "wb");
        ASSERT_TRUE(file != NULL);
        fwrite(&tile, 1u, sizeof(tile), file);
        fclose(file);
    }
    ASSERT_TRUE(load_superchunk_map_tile_from_disk(0, 0, pixels));
    ASSERT_UINT_EQ(pixels[0], 0xFF654321u);
}

TEST(map_color_cases)
{
    SdkTerrainColumnProfile center;
    SdkTerrainColumnProfile west;
    SdkTerrainColumnProfile east;
    SdkTerrainColumnProfile north;
    SdkTerrainColumnProfile south;
    uint32_t dry_sand;
    uint32_t shallow_sand;
    uint32_t deep_water;
    uint32_t ice_water;
    uint32_t sand_color = opaque_face_color(BLOCK_SAND);
    uint32_t water_color = opaque_face_color(BLOCK_WATER);
    uint32_t ice_color = opaque_face_color(BLOCK_ICE);

    init_profile(&center);
    west = center;
    east = center;
    north = center;
    south = center;

    center.surface_sediment = SEDIMENT_BEACH_SAND;
    center.terrain_province = TERRAIN_PROVINCE_DYNAMIC_COAST;
    dry_sand = sdk_map_color_for_profiles(&center, &west, &east, &north, &south, 192);
    ASSERT_TRUE(color_distance_sq(dry_sand, sand_color) < color_distance_sq(dry_sand, water_color));

    center.water_height = center.surface_height + 2;
    center.water_surface_class = SURFACE_WATER_OPEN;
    shallow_sand = sdk_map_color_for_profiles(&center, &west, &east, &north, &south, 192);
    ASSERT_TRUE(color_distance_sq(shallow_sand, sand_color) < color_distance_sq(shallow_sand, water_color));
    ASSERT_TRUE(shallow_sand != water_color);

    center.water_height = center.surface_height + 16;
    deep_water = sdk_map_color_for_profiles(&center, &west, &east, &north, &south, 192);
    ASSERT_TRUE(color_distance_sq(deep_water, water_color) < color_distance_sq(deep_water, sand_color));

    center.water_height = center.surface_height + 4;
    center.water_surface_class = SURFACE_WATER_SEASONAL_ICE;
    ice_water = sdk_map_color_for_profiles(&center, &west, &east, &north, &south, 192);
    ASSERT_TRUE(color_distance_sq(ice_water, ice_color) < color_distance_sq(ice_water, water_color));

    ASSERT_TRUE(map_is_wall_band_local(0, 80));
    ASSERT_UINT_EQ(map_wall_color_for_local(0, 80), sdk_block_get_face_color(BLOCK_STONE_BRICKS, 3));
}

TEST(offline_bulk_queues_origin_page_first)
{
    char world_id[64];
    SdkMapSchedulerConfig config;

    make_unique_world_id(world_id, sizeof(world_id), "origin");
    set_test_cache_identity(world_id, 0x13579BDFu);
    memset(&config, 0, sizeof(config));
    init_test_world_desc(&config.world_desc, g_sdk.world_seed);
    config.world_seed = g_sdk.world_seed;
    config.worker_count = 1;
    config.mode = SDK_MAP_SCHED_MODE_OFFLINE_BULK;
    config.build_kind = SDK_MAP_BUILD_EXACT_OFFLINE;
    strcpy_s(config.world_save_id, sizeof(config.world_save_id), world_id);

    ASSERT_TRUE(init_superchunk_map_scheduler(&config));
    ASSERT_TRUE(SuspendThread(g_map_scheduler.workers[0].thread) != (DWORD)-1);
    pump_superchunk_map_scheduler_offline_bulk(1);

    EnterCriticalSection(&g_map_scheduler.lock);
    ASSERT_INT_EQ(g_map_scheduler.page_job_count, 1);
    ASSERT_INT_EQ(g_map_scheduler.page_jobs[g_map_scheduler.page_job_head].page_scx, 0);
    ASSERT_INT_EQ(g_map_scheduler.page_jobs[g_map_scheduler.page_job_head].page_scz, 0);
    ASSERT_INT_EQ(g_map_scheduler.page_jobs[g_map_scheduler.page_job_head].tile_count,
                  SDK_MAP_OFFLINE_BATCH_TILE_COUNT);
    LeaveCriticalSection(&g_map_scheduler.lock);

    ASSERT_TRUE(ResumeThread(g_map_scheduler.workers[0].thread) != (DWORD)-1);
    shutdown_superchunk_map_scheduler();
}

TEST(offline_bulk_skips_cached_origin_page)
{
    char world_id[64];
    SdkMapSchedulerConfig config;
    int step = SUPERCHUNK_BLOCKS;
    uint32_t pixels[HUD_MAP_PIXEL_COUNT];

    make_unique_world_id(world_id, sizeof(world_id), "skippage");
    set_test_cache_identity(world_id, 0x2468ACE0u);

    memset(pixels, 0x66, sizeof(pixels));
    for (int local_z = 0; local_z < SDK_MAP_MACRO_TILE_SUPERCHUNK_SPAN; ++local_z) {
        for (int local_x = 0; local_x < SDK_MAP_MACRO_TILE_SUPERCHUNK_SPAN; ++local_x) {
            save_superchunk_map_tile_to_disk(local_x * step, local_z * step, pixels);
            pixels[0]++;
        }
    }

    memset(&config, 0, sizeof(config));
    init_test_world_desc(&config.world_desc, g_sdk.world_seed);
    config.world_seed = g_sdk.world_seed;
    config.worker_count = 1;
    config.mode = SDK_MAP_SCHED_MODE_OFFLINE_BULK;
    config.build_kind = SDK_MAP_BUILD_EXACT_OFFLINE;
    strcpy_s(config.world_save_id, sizeof(config.world_save_id), world_id);

    ASSERT_TRUE(init_superchunk_map_scheduler(&config));
    ASSERT_TRUE(SuspendThread(g_map_scheduler.workers[0].thread) != (DWORD)-1);
    pump_superchunk_map_scheduler_offline_bulk(2);

    EnterCriticalSection(&g_map_scheduler.lock);
    ASSERT_INT_EQ(g_map_scheduler.page_job_count, 1);
    ASSERT_INT_EQ(g_map_scheduler.page_jobs[g_map_scheduler.page_job_head].page_scx, 1);
    ASSERT_INT_EQ(g_map_scheduler.page_jobs[g_map_scheduler.page_job_head].page_scz, 0);
    ASSERT_INT_EQ(g_map_scheduler.page_jobs[g_map_scheduler.page_job_head].tile_count,
                  SDK_MAP_OFFLINE_BATCH_TILE_COUNT);
    LeaveCriticalSection(&g_map_scheduler.lock);

    ASSERT_TRUE(ResumeThread(g_map_scheduler.workers[0].thread) != (DWORD)-1);
    shutdown_superchunk_map_scheduler();
}

TEST(interactive_fallback_does_not_persist_to_disk)
{
    char world_id[64];
    SdkMapSchedulerConfig config;
    SuperchunkMapCacheEntry* entry;
    uint32_t pixels[HUD_MAP_PIXEL_COUNT];

    make_unique_world_id(world_id, sizeof(world_id), "fallback");
    set_test_cache_identity(world_id, 0x10293847u);
    memset(&config, 0, sizeof(config));
    init_test_world_desc(&config.world_desc, g_sdk.world_seed);
    config.world_seed = g_sdk.world_seed;
    config.worker_count = 1;
    config.mode = SDK_MAP_SCHED_MODE_INTERACTIVE;
    config.build_kind = SDK_MAP_BUILD_INTERACTIVE_FALLBACK;
    strcpy_s(config.world_save_id, sizeof(config.world_save_id), world_id);

    ASSERT_TRUE(init_superchunk_map_scheduler(&config));
    request_superchunk_map_cache_entry_async(0, 0, true);
    entry = NULL;
    for (int i = 0; i < 10000; ++i) {
        entry = request_superchunk_map_cache_entry_async(0, 0, true);
        if (entry && entry->valid && entry->state == SDK_MAP_TILE_READY) {
            break;
        }
        Sleep(1);
    }
    ASSERT_TRUE(entry != NULL);
    ASSERT_TRUE(entry->valid && entry->state == SDK_MAP_TILE_READY);
    ASSERT_FALSE(load_superchunk_map_tile_from_disk(0, 0, pixels));

    shutdown_superchunk_map_scheduler();
}

TEST(worldgen_surface_sampler_matches_generated_chunk)
{
    SdkWorldGen wg;
    SdkChunk chunk;
    SdkTerrainColumnProfile* scratch_profiles;
    SdkWorldGenSurfaceColumn* scratch_columns;
    static const int chunk_coords[][2] = {
        { 5, 7 },
        { -11, 13 },
        { 24, -19 }
    };

    memset(&wg, 0, sizeof(wg));
    init_test_world_desc(&wg.desc, 0x5A17C3E1u);
    sdk_worldgen_init(&wg, &wg.desc);
    ASSERT_TRUE(wg.impl != NULL);

    memset(&chunk, 0, sizeof(chunk));
    sdk_chunk_init(&chunk, 0, 0);
    ASSERT_TRUE(chunk.blocks != NULL);

    scratch_profiles =
        (SdkTerrainColumnProfile*)malloc(sizeof(SdkTerrainColumnProfile) * CHUNK_BLOCKS_PER_LAYER);
    scratch_columns =
        (SdkWorldGenSurfaceColumn*)malloc(sizeof(SdkWorldGenSurfaceColumn) * CHUNK_BLOCKS_PER_LAYER);
    ASSERT_TRUE(scratch_profiles != NULL);
    ASSERT_TRUE(scratch_columns != NULL);

    for (int i = 0; i < (int)(sizeof(chunk_coords) / sizeof(chunk_coords[0])); ++i) {
        chunk.cx = (int16_t)chunk_coords[i][0];
        chunk.cz = (int16_t)chunk_coords[i][1];
        sdk_worldgen_generate_chunk_ctx(&wg, &chunk);
        ASSERT_TRUE(sdk_worldgen_generate_chunk_surface_ctx(&wg,
                                                            chunk.cx,
                                                            chunk.cz,
                                                            scratch_profiles,
                                                            scratch_columns));

        for (int lz = 0; lz < CHUNK_DEPTH; ++lz) {
            for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
                SdkMapExactColumnSample expected;
                const SdkWorldGenSurfaceColumn* actual =
                    &scratch_columns[lz * CHUNK_WIDTH + lx];

                ASSERT_TRUE(classify_generated_chunk_column(&chunk, lx, lz, &expected));
                switch (expected.kind) {
                    case SDK_MAP_EXACT_COLUMN_OPEN_WATER:
                        ASSERT_INT_EQ(actual->kind, SDK_WORLDGEN_SURFACE_COLUMN_OPEN_WATER);
                        ASSERT_INT_EQ(actual->top_block, expected.top_block);
                        ASSERT_INT_EQ(actual->top_height, expected.water_height);
                        ASSERT_INT_EQ(actual->land_block, expected.floor_block);
                        ASSERT_INT_EQ(actual->land_height, expected.land_height);
                        ASSERT_INT_EQ(actual->water_depth, expected.water_depth);
                        break;
                    case SDK_MAP_EXACT_COLUMN_SEASONAL_ICE:
                        ASSERT_INT_EQ(actual->kind, SDK_WORLDGEN_SURFACE_COLUMN_SEASONAL_ICE);
                        ASSERT_INT_EQ(actual->top_block, expected.top_block);
                        ASSERT_INT_EQ(actual->top_height, expected.water_height);
                        ASSERT_INT_EQ(actual->land_block, expected.floor_block);
                        ASSERT_INT_EQ(actual->land_height, expected.land_height);
                        ASSERT_INT_EQ(actual->water_depth, expected.water_depth);
                        break;
                    case SDK_MAP_EXACT_COLUMN_PERENNIAL_ICE:
                        ASSERT_INT_EQ(actual->kind, SDK_WORLDGEN_SURFACE_COLUMN_PERENNIAL_ICE);
                        ASSERT_INT_EQ(actual->top_block, expected.top_block);
                        ASSERT_INT_EQ(actual->top_height, expected.water_height);
                        ASSERT_INT_EQ(actual->land_block, expected.floor_block);
                        ASSERT_INT_EQ(actual->land_height, expected.land_height);
                        ASSERT_INT_EQ(actual->water_depth, expected.water_depth);
                        break;
                    case SDK_MAP_EXACT_COLUMN_DRY:
                    default:
                        ASSERT_TRUE(actual->kind == SDK_WORLDGEN_SURFACE_COLUMN_DRY ||
                                    actual->kind == SDK_WORLDGEN_SURFACE_COLUMN_LAVA);
                        ASSERT_INT_EQ(actual->top_block, expected.top_block);
                        ASSERT_INT_EQ(actual->top_height, expected.land_height);
                        break;
                }
            }
        }
    }

    free(scratch_profiles);
    free(scratch_columns);
    sdk_chunk_free(&chunk);
    sdk_worldgen_shutdown(&wg);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("SDK Map System Tests\n");
    RUN_TEST(cache_version_invalidates_old_tiles);
    RUN_TEST(map_color_cases);
    RUN_TEST(offline_bulk_queues_origin_page_first);
    RUN_TEST(offline_bulk_skips_cached_origin_page);
    RUN_TEST(interactive_fallback_does_not_persist_to_disk);
    RUN_TEST(worldgen_surface_sampler_matches_generated_chunk);
    test_print_summary();
    return g_test_failed ? 1 : 0;
}
