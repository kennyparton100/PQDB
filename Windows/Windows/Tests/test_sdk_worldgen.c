/**
 * test_sdk_worldgen.c -- Terrain regression checks for SDK world generation.
 *
 * Build (MSVC, from the Windows/ directory):
 *   cl /O2 /W4 /I NQL/SDK /I NQL/SDK/Core ^
 *      Tests/test_sdk_worldgen.c ^
 *      NQL/SDK/Core/sdk_block.c ^
 *      NQL/SDK/Core/sdk_chunk.c ^
 *      NQL/SDK/Core/sdk_worldgen.c ^
 *      NQL/SDK/Core/sdk_worldgen_macro.c ^
 *      NQL/SDK/Core/sdk_worldgen_continental.c ^
 *      NQL/SDK/Core/sdk_worldgen_hydro.c ^
 *      NQL/SDK/Core/sdk_worldgen_column_region.c ^
 *      /Fe:test_sdk_worldgen.exe
 */
#include "../NQL/SDK/Core/sdk_worldgen_internal.h"
#include "../NQL/SDK/Core/sdk_worldgen_column_internal.h"
#include "test_harness.h"
#include <stdlib.h>

typedef enum {
    TEST_SURFACE_DRY = 0,
    TEST_SURFACE_OPEN_WATER,
    TEST_SURFACE_SEASONAL_ICE,
    TEST_SURFACE_PERENNIAL_ICE
} TestSurfaceKind;

typedef struct {
    uint8_t kind;
    BlockType top_block;
    BlockType floor_block;
    uint16_t land_height;
    uint16_t water_height;
    uint16_t water_depth;
} TestSurfaceSample;

static void init_world(SdkWorldGen* wg, uint32_t seed)
{
    SdkWorldDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.seed = seed;
    desc.sea_level = 192;
    desc.macro_cell_size = SDK_WORLDGEN_MACRO_CELL_BLOCKS;
    sdk_worldgen_init(wg, &desc);
}

static int terrain_group_marine(SdkTerrainProvince province)
{
    return province == TERRAIN_PROVINCE_OPEN_OCEAN ||
           province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
           province == TERRAIN_PROVINCE_DYNAMIC_COAST;
}

static int classify_generated_chunk_column(const SdkChunk* chunk,
                                           int lx,
                                           int lz,
                                           TestSurfaceSample* out_sample)
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
        out_sample->kind = TEST_SURFACE_DRY;
        out_sample->top_block = block;
        out_sample->land_height = (uint16_t)ly;
        return 1;
    }

    if (surface_y < 0) return 0;

    out_sample->water_height = (uint16_t)surface_y;
    out_sample->top_block = surface_block;
    if (surface_block == BLOCK_SEA_ICE) {
        out_sample->kind = TEST_SURFACE_PERENNIAL_ICE;
    } else if (surface_block == BLOCK_ICE) {
        out_sample->kind = TEST_SURFACE_SEASONAL_ICE;
    } else {
        out_sample->kind = TEST_SURFACE_OPEN_WATER;
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

TEST(worldgen_determinism)
{
    static const int coords[][2] = {
        { 0, 0 }, { 256, 256 }, { -512, 2048 }, { 4096, -8192 },
        { 16384, 96 }, { -24576, 12288 }, { 32768, -32768 }, { -65536, 16384 }
    };
    SdkWorldGen wg;
    int i;

    memset(&wg, 0, sizeof(wg));
    init_world(&wg, 0x12345678u);

    for (i = 0; i < (int)(sizeof(coords) / sizeof(coords[0])); ++i) {
        SdkTerrainColumnProfile a;
        SdkTerrainColumnProfile b;
        SdkContinentalSample ca;
        SdkContinentalSample cb;

        ASSERT_TRUE(sdk_worldgen_sample_column_ctx(&wg, coords[i][0], coords[i][1], &a));
        ASSERT_TRUE(sdk_worldgen_sample_column_ctx(&wg, coords[i][0], coords[i][1], &b));
        ASSERT_TRUE(memcmp(&a, &b, sizeof(a)) == 0);

        sdk_worldgen_sample_continental_state(&wg, coords[i][0], coords[i][1], &ca);
        sdk_worldgen_sample_continental_state(&wg, coords[i][0], coords[i][1], &cb);
        ASSERT_FLOAT_NEAR(ca.raw_height, cb.raw_height, 0.001);
        ASSERT_FLOAT_NEAR(ca.filled_height, cb.filled_height, 0.001);
        ASSERT_FLOAT_NEAR(ca.coast_distance, cb.coast_distance, 0.001);
        ASSERT_UINT_EQ(ca.basin_id, cb.basin_id);
    }

    sdk_worldgen_shutdown(&wg);
}

TEST(worldgen_boundary_continuity)
{
    SdkWorldGen wg;
    int boundary = SDK_WORLDGEN_CONTINENT_TILE_SIZE * SDK_WORLDGEN_CONTINENT_CELL_BLOCKS;
    int offsets[] = { -128, -96, -64, -32, 0, 32, 64, 96, 128 };
    int rows[] = { -8192, -2048, 0, 2048, 8192 };
    int i;
    int j;

    memset(&wg, 0, sizeof(wg));
    init_world(&wg, 0xA17F42C3u);

    for (j = 0; j < (int)(sizeof(rows) / sizeof(rows[0])); ++j) {
        for (i = 0; i < (int)(sizeof(offsets) / sizeof(offsets[0])) - 1; ++i) {
            SdkTerrainColumnProfile a;
            SdkTerrainColumnProfile b;
            int x0 = boundary + offsets[i];
            int x1 = boundary + offsets[i + 1];
            int z = rows[j];

            ASSERT_TRUE(sdk_worldgen_sample_column_ctx(&wg, x0, z, &a));
            ASSERT_TRUE(sdk_worldgen_sample_column_ctx(&wg, x1, z, &b));
            ASSERT_TRUE(abs((int)a.surface_height - (int)b.surface_height) < 96);
            ASSERT_TRUE(abs((int)a.water_height - (int)b.water_height) < 96);
        }
    }

    sdk_worldgen_shutdown(&wg);
}

TEST(worldgen_hydrology_invariants)
{
    SdkWorldGen wg;
    int cx;
    int cz;

    memset(&wg, 0, sizeof(wg));
    init_world(&wg, 0x5EED1234u);

    for (cz = -40; cz <= 40; ++cz) {
        for (cx = -40; cx <= 40; ++cx) {
            const SdkContinentalCell* cell = sdk_worldgen_get_continental_cell(&wg, cx, cz);
            ASSERT_TRUE(cell != NULL);
            if (!cell) continue;

            if (cell->downstream_cx == cx && cell->downstream_cz == cz) {
                ASSERT_TRUE(cell->ocean_mask || cell->closed_basin_mask > 0u || cell->lake_mask > 0u);
            } else {
                const SdkContinentalCell* down = sdk_worldgen_get_continental_cell(&wg, cell->downstream_cx, cell->downstream_cz);
                ASSERT_TRUE(down != NULL);
                if (!down) continue;
                ASSERT_TRUE(down->filled_height <= cell->filled_height);
                if (down->filled_height == cell->filled_height) {
                    ASSERT_TRUE(down->raw_height <= cell->raw_height ||
                                down->coast_distance <= cell->coast_distance);
                }
            }

            if (cell->trunk_river_order >= 2u) {
                int step = 0;
                int tx = cx;
                int tz = cz;
                int reached = 0;

                while (step++ < 256) {
                    const SdkContinentalCell* cur = sdk_worldgen_get_continental_cell(&wg, tx, tz);
                    ASSERT_TRUE(cur != NULL);
                    if (!cur) break;
                    if (cur->ocean_mask || cur->closed_basin_mask > 0u || cur->lake_mask > 0u) {
                        reached = 1;
                        break;
                    }
                    if (cur->downstream_cx == tx && cur->downstream_cz == tz) break;
                    tx = cur->downstream_cx;
                    tz = cur->downstream_cz;
                }

                ASSERT_TRUE(reached);
            }
        }
    }

    sdk_worldgen_shutdown(&wg);
}

TEST(worldgen_diversity_and_realism)
{
    const uint32_t seeds[] = {
        0x12345678u, 0xCAFEBABEu, 0x0BADF00Du, 0xA17F42C3u, 0x5EED1234u
    };
    int saw_marine = 0;
    int saw_coast = 0;
    int saw_wet = 0;
    int saw_temperate = 0;
    int saw_arid = 0;
    int saw_alpine = 0;
    int saw_volcanic_high = 0;
    int s;

    for (s = 0; s < (int)(sizeof(seeds) / sizeof(seeds[0])); ++s) {
        SdkWorldGen wg;
        int x;
        int z;

        memset(&wg, 0, sizeof(wg));
        init_world(&wg, seeds[s]);

        for (z = -32768; z <= 32768; z += 4096) {
            for (x = -32768; x <= 32768; x += 4096) {
                SdkTerrainColumnProfile profile;
                SdkWorldGenRegionTile* region_tile;
                SdkRegionFieldSample geology;

                ASSERT_TRUE(sdk_worldgen_sample_column_ctx(&wg, x, z, &profile));
                if (terrain_group_marine(profile.terrain_province)) saw_marine = 1;
                if (profile.terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) saw_coast = 1;
                if (profile.terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA ||
                    profile.terrain_province == TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND ||
                    profile.terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND ||
                    profile.ecology == ECOLOGY_FEN ||
                    profile.ecology == ECOLOGY_BOG ||
                    profile.ecology == ECOLOGY_ESTUARY_WETLAND) {
                    saw_wet = 1;
                }
                if (profile.ecology == ECOLOGY_TEMPERATE_DECIDUOUS_FOREST ||
                    profile.ecology == ECOLOGY_TEMPERATE_CONIFER_FOREST ||
                    profile.ecology == ECOLOGY_PRAIRIE) {
                    saw_temperate = 1;
                }
                if (profile.ecology == ECOLOGY_HOT_DESERT ||
                    profile.ecology == ECOLOGY_STEPPE ||
                    profile.terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE) {
                    saw_arid = 1;
                }
                if (profile.terrain_province == TERRAIN_PROVINCE_ALPINE_BELT ||
                    profile.ecology == ECOLOGY_TUNDRA ||
                    profile.ecology == ECOLOGY_NIVAL_ICE) {
                    saw_alpine = 1;
                }
                if (profile.terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ||
                    profile.terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU ||
                    profile.terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND ||
                    profile.terrain_province == TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT) {
                    saw_volcanic_high = 1;
                }

                if (profile.terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA) {
                    region_tile = sdk_worldgen_require_region_tile(&wg, x, z);
                    ASSERT_TRUE(region_tile != NULL);
                    if (!region_tile) continue;
                    memset(&geology, 0, sizeof(geology));
                    sdk_worldgen_sample_region_fields(region_tile, &wg, x, z, &geology);
                    ASSERT_TRUE(geology.river_order >= 2.0f);
                    ASSERT_TRUE(geology.coast_signed_distance <= 64.0f);
                }
            }
        }

        sdk_worldgen_shutdown(&wg);
    }

    ASSERT_TRUE(saw_marine);
    ASSERT_TRUE(saw_coast);
    ASSERT_TRUE(saw_wet);
    ASSERT_TRUE(saw_temperate);
    ASSERT_TRUE(saw_arid);
    ASSERT_TRUE(saw_alpine);
    ASSERT_TRUE(saw_volcanic_high);
}

TEST(worldgen_chunk_surface_matches_generated_chunk)
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
    init_world(&wg, 0x5A17C3E1u);

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
                TestSurfaceSample expected;
                const SdkWorldGenSurfaceColumn* actual =
                    &scratch_columns[lz * CHUNK_WIDTH + lx];

                ASSERT_TRUE(classify_generated_chunk_column(&chunk, lx, lz, &expected));
                switch (expected.kind) {
                    case TEST_SURFACE_OPEN_WATER:
                        ASSERT_INT_EQ(actual->kind, SDK_WORLDGEN_SURFACE_COLUMN_OPEN_WATER);
                        ASSERT_INT_EQ(actual->top_block, expected.top_block);
                        ASSERT_INT_EQ(actual->top_height, expected.water_height);
                        ASSERT_INT_EQ(actual->land_block, expected.floor_block);
                        ASSERT_INT_EQ(actual->land_height, expected.land_height);
                        ASSERT_INT_EQ(actual->water_depth, expected.water_depth);
                        break;
                    case TEST_SURFACE_SEASONAL_ICE:
                        ASSERT_INT_EQ(actual->kind, SDK_WORLDGEN_SURFACE_COLUMN_SEASONAL_ICE);
                        ASSERT_INT_EQ(actual->top_block, expected.top_block);
                        ASSERT_INT_EQ(actual->top_height, expected.water_height);
                        ASSERT_INT_EQ(actual->land_block, expected.floor_block);
                        ASSERT_INT_EQ(actual->land_height, expected.land_height);
                        ASSERT_INT_EQ(actual->water_depth, expected.water_depth);
                        break;
                    case TEST_SURFACE_PERENNIAL_ICE:
                        ASSERT_INT_EQ(actual->kind, SDK_WORLDGEN_SURFACE_COLUMN_PERENNIAL_ICE);
                        ASSERT_INT_EQ(actual->top_block, expected.top_block);
                        ASSERT_INT_EQ(actual->top_height, expected.water_height);
                        ASSERT_INT_EQ(actual->land_block, expected.floor_block);
                        ASSERT_INT_EQ(actual->land_height, expected.land_height);
                        ASSERT_INT_EQ(actual->water_depth, expected.water_depth);
                        break;
                    case TEST_SURFACE_DRY:
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
    printf("SDK Worldgen Regression Tests\n");
    RUN_TEST(worldgen_determinism);
    RUN_TEST(worldgen_boundary_continuity);
    RUN_TEST(worldgen_hydrology_invariants);
    RUN_TEST(worldgen_diversity_and_realism);
    RUN_TEST(worldgen_chunk_surface_matches_generated_chunk);
    test_print_summary();
    return g_test_failed ? 1 : 0;
}
