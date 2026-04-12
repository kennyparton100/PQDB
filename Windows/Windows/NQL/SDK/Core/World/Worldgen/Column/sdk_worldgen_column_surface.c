/**
 * sdk_worldgen_column_surface.c -- Surface vegetation placement helpers.
 */
#include "sdk_worldgen_column_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const SdkWorldGenTreeRule g_tree_rules[] = {
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_BARREN */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_DUNE_COAST */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_ESTUARY_WETLAND */
    { 0.020f, SDK_WORLDGEN_TREE_ROUND },    /* ECOLOGY_RIPARIAN_FOREST */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_FLOODPLAIN_MEADOW */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_FEN */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_BOG */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_PRAIRIE */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_STEPPE */
    { 0.005f, SDK_WORLDGEN_TREE_SHRUB },    /* ECOLOGY_MEDITERRANEAN_SCRUB */
    { 0.013f, SDK_WORLDGEN_TREE_ROUND },    /* ECOLOGY_TEMPERATE_DECIDUOUS_FOREST */
    { 0.014f, SDK_WORLDGEN_TREE_CONIFER },  /* ECOLOGY_TEMPERATE_CONIFER_FOREST */
    { 0.014f, SDK_WORLDGEN_TREE_CONIFER },  /* ECOLOGY_BOREAL_TAIGA */
    { 0.017f, SDK_WORLDGEN_TREE_TROPICAL }, /* ECOLOGY_TROPICAL_SEASONAL_FOREST */
    { 0.021f, SDK_WORLDGEN_TREE_TROPICAL }, /* ECOLOGY_TROPICAL_RAINFOREST */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_HOT_DESERT */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_TUNDRA */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_ALPINE_MEADOW */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_VOLCANIC_BARRENS */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_NIVAL_ICE */
    { 0.000f, SDK_WORLDGEN_TREE_NONE },     /* ECOLOGY_SALT_DESERT */
    { 0.006f, SDK_WORLDGEN_TREE_SHRUB },    /* ECOLOGY_SCRUB_BADLANDS */
    { 0.009f, SDK_WORLDGEN_TREE_ROUND },    /* ECOLOGY_SAVANNA_GRASSLAND */
    { 0.015f, SDK_WORLDGEN_TREE_TROPICAL }  /* ECOLOGY_MANGROVE_SWAMP */
};

static const SdkWorldGenPlantRuleSet g_plant_rules[] = {
    { 0, {{ BLOCK_AIR, 0.0f, 0 }, { BLOCK_AIR, 0.0f, 0 }, { BLOCK_AIR, 0.0f, 0 }} },                             /* BARREN */
    { 3, {{ BLOCK_CACTUS, 0.018f, MOISTURE_ARID }, { BLOCK_SUCCULENT, 0.035f, MOISTURE_ARID }, { BLOCK_DRY_GRASS, 0.050f, MOISTURE_ARID }} }, /* DUNE_COAST */
    { 3, {{ BLOCK_CATTAILS, 0.030f, MOISTURE_ARID }, { BLOCK_REEDS, 0.055f, MOISTURE_ARID }, { BLOCK_FERN, 0.075f, MOISTURE_ARID }} },          /* ESTUARY_WETLAND */
    { 3, {{ BLOCK_CATTAILS, 0.022f, MOISTURE_ARID }, { BLOCK_FERN, 0.045f, MOISTURE_ARID }, { BLOCK_BERRY_BUSH, 0.060f, MOISTURE_ARID }} },     /* RIPARIAN_FOREST */
    { 3, {{ BLOCK_TALL_GRASS, 0.035f, MOISTURE_ARID }, { BLOCK_WILDFLOWERS, 0.055f, MOISTURE_ARID }, { BLOCK_BERRY_BUSH, 0.070f, MOISTURE_SUBHUMID }} }, /* FLOODPLAIN_MEADOW */
    { 3, {{ BLOCK_CATTAILS, 0.030f, MOISTURE_ARID }, { BLOCK_REEDS, 0.055f, MOISTURE_ARID }, { BLOCK_FERN, 0.075f, MOISTURE_ARID }} },          /* FEN */
    { 2, {{ BLOCK_MOSS, 0.035f, MOISTURE_ARID }, { BLOCK_HEATHER, 0.055f, MOISTURE_ARID }, { BLOCK_AIR, 0.0f, 0 }} },                           /* BOG */
    { 3, {{ BLOCK_TALL_GRASS, 0.035f, MOISTURE_ARID }, { BLOCK_WILDFLOWERS, 0.055f, MOISTURE_ARID }, { BLOCK_BERRY_BUSH, 0.070f, MOISTURE_SUBHUMID }} }, /* PRAIRIE */
    { 3, {{ BLOCK_DRY_GRASS, 0.032f, MOISTURE_ARID }, { BLOCK_SHRUB, 0.050f, MOISTURE_ARID }, { BLOCK_HEATHER, 0.062f, MOISTURE_ARID }} },      /* STEPPE */
    { 3, {{ BLOCK_DRY_GRASS, 0.032f, MOISTURE_ARID }, { BLOCK_SHRUB, 0.050f, MOISTURE_ARID }, { BLOCK_HEATHER, 0.062f, MOISTURE_ARID }} },      /* MEDITERRANEAN_SCRUB */
    { 3, {{ BLOCK_FERN, 0.025f, MOISTURE_ARID }, { BLOCK_WILDFLOWERS, 0.045f, MOISTURE_ARID }, { BLOCK_BERRY_BUSH, 0.060f, MOISTURE_ARID }} },  /* TEMPERATE_DECIDUOUS_FOREST */
    { 2, {{ BLOCK_MOSS, 0.028f, MOISTURE_ARID }, { BLOCK_FERN, 0.048f, MOISTURE_ARID }, { BLOCK_AIR, 0.0f, 0 }} },                              /* TEMPERATE_CONIFER_FOREST */
    { 2, {{ BLOCK_MOSS, 0.028f, MOISTURE_ARID }, { BLOCK_FERN, 0.048f, MOISTURE_ARID }, { BLOCK_AIR, 0.0f, 0 }} },                              /* BOREAL_TAIGA */
    { 3, {{ BLOCK_FERN, 0.030f, MOISTURE_ARID }, { BLOCK_MOSS, 0.055f, MOISTURE_ARID }, { BLOCK_TALL_GRASS, 0.072f, MOISTURE_ARID }} },         /* TROPICAL_SEASONAL_FOREST */
    { 3, {{ BLOCK_FERN, 0.030f, MOISTURE_ARID }, { BLOCK_MOSS, 0.055f, MOISTURE_ARID }, { BLOCK_TALL_GRASS, 0.072f, MOISTURE_ARID }} },         /* TROPICAL_RAINFOREST */
    { 3, {{ BLOCK_CACTUS, 0.018f, MOISTURE_ARID }, { BLOCK_SUCCULENT, 0.035f, MOISTURE_ARID }, { BLOCK_AIR, 0.0f, 0 }} },                       /* HOT_DESERT */
    { 3, {{ BLOCK_HEATHER, 0.028f, MOISTURE_ARID }, { BLOCK_MOSS, 0.045f, MOISTURE_ARID }, { BLOCK_SHRUB, 0.060f, MOISTURE_ARID }} },           /* TUNDRA */
    { 3, {{ BLOCK_HEATHER, 0.028f, MOISTURE_ARID }, { BLOCK_MOSS, 0.045f, MOISTURE_ARID }, { BLOCK_SHRUB, 0.060f, MOISTURE_ARID }} },           /* ALPINE_MEADOW */
    { 2, {{ BLOCK_SHRUB, 0.018f, MOISTURE_ARID }, { BLOCK_MOSS, 0.030f, MOISTURE_ARID }, { BLOCK_AIR, 0.0f, 0 }} },                             /* VOLCANIC_BARRENS */
    { 0, {{ BLOCK_AIR, 0.0f, 0 }, { BLOCK_AIR, 0.0f, 0 }, { BLOCK_AIR, 0.0f, 0 }} },                             /* NIVAL_ICE */
    { 2, {{ BLOCK_SUCCULENT, 0.028f, MOISTURE_ARID }, { BLOCK_DRY_GRASS, 0.050f, MOISTURE_ARID }, { BLOCK_AIR, 0.0f, 0 }} },                   /* SALT_DESERT */
    { 3, {{ BLOCK_SHRUB, 0.028f, MOISTURE_ARID }, { BLOCK_DRY_GRASS, 0.048f, MOISTURE_ARID }, { BLOCK_HEATHER, 0.064f, MOISTURE_ARID }} },     /* SCRUB_BADLANDS */
    { 3, {{ BLOCK_TALL_GRASS, 0.036f, MOISTURE_SEMI_ARID }, { BLOCK_WILDFLOWERS, 0.056f, MOISTURE_SEMI_ARID }, { BLOCK_SHRUB, 0.070f, MOISTURE_ARID }} }, /* SAVANNA_GRASSLAND */
    { 3, {{ BLOCK_CATTAILS, 0.032f, MOISTURE_SUBHUMID }, { BLOCK_REEDS, 0.054f, MOISTURE_SUBHUMID }, { BLOCK_FERN, 0.074f, MOISTURE_SUBHUMID }} } /* MANGROVE_SWAMP */
};

const SdkWorldGenTreeRule* sdk_worldgen_tree_rule_for_profile(const SdkTerrainColumnProfile* profile)
{
    int ecology;

    if (!profile) return NULL;
    ecology = (int)profile->ecology;
    if (ecology < 0 || ecology >= (int)(sizeof(g_tree_rules) / sizeof(g_tree_rules[0]))) return NULL;
    return &g_tree_rules[ecology];
}

const SdkWorldGenPlantRuleSet* sdk_worldgen_plant_rules_for_profile(const SdkTerrainColumnProfile* profile)
{
    int ecology;

    if (!profile) return NULL;
    ecology = (int)profile->ecology;
    if (ecology < 0 || ecology >= (int)(sizeof(g_plant_rules) / sizeof(g_plant_rules[0]))) return NULL;
    return &g_plant_rules[ecology];
}

int sdk_worldgen_top_block_supports_surface_flora(BlockType top_block)
{
    return top_block == BLOCK_TURF ||
           top_block == BLOCK_GRASS ||
           top_block == BLOCK_WETLAND_SOD ||
           top_block == BLOCK_TOPSOIL ||
           top_block == BLOCK_SAND ||
           top_block == BLOCK_TUFF ||
           top_block == BLOCK_CALCAREOUS_SOIL ||
           top_block == BLOCK_LOESS ||
           top_block == BLOCK_PEAT;
}

static void set_leaf_if_air(SdkChunk* chunk, int lx, int ly, int lz)
{
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH || (unsigned)ly >= CHUNK_HEIGHT) return;
    if (sdk_chunk_get_block(chunk, lx, ly, lz) == BLOCK_AIR) {
        sdk_chunk_set_block(chunk, lx, ly, lz, BLOCK_LEAVES);
    }
}

static void place_round_canopy(SdkChunk* chunk, int lx, int ly, int lz, int trunk)
{
    int dy;
    for (dy = 0; dy <= 3; ++dy) {
        int radius = (dy <= 1) ? 2 : 1;
        int dx;
        int dz;
        int cy = ly + trunk - 1 + dy;
        for (dx = -radius; dx <= radius; ++dx) {
            for (dz = -radius; dz <= radius; ++dz) {
                if (dx == 0 && dz == 0 && dy < 2) continue;
                if (dx * dx + dz * dz > radius * radius + 1) continue;
                set_leaf_if_air(chunk, lx + dx, cy, lz + dz);
            }
        }
    }
}

static void place_conifer_canopy(SdkChunk* chunk, int lx, int ly, int lz, int trunk)
{
    int level;
    for (level = 0; level < trunk; ++level) {
        int radius = (level < 2) ? 2 : ((level < trunk - 2) ? 1 : 0);
        int cy = ly + trunk - level;
        int dx;
        int dz;
        for (dx = -radius; dx <= radius; ++dx) {
            for (dz = -radius; dz <= radius; ++dz) {
                if (abs(dx) + abs(dz) > radius + 1) continue;
                if (dx == 0 && dz == 0) continue;
                set_leaf_if_air(chunk, lx + dx, cy, lz + dz);
            }
        }
    }
    set_leaf_if_air(chunk, lx, ly + trunk + 1, lz);
}

static void place_tropical_canopy(SdkChunk* chunk, int lx, int ly, int lz, int trunk)
{
    int dy;
    for (dy = 0; dy <= 2; ++dy) {
        int radius = 2 + (dy == 1);
        int dx;
        int dz;
        int cy = ly + trunk - 1 + dy;
        for (dx = -radius; dx <= radius; ++dx) {
            for (dz = -radius; dz <= radius; ++dz) {
                if (dx == 0 && dz == 0 && dy == 0) continue;
                if (dx * dx + dz * dz > radius * radius + 1) continue;
                set_leaf_if_air(chunk, lx + dx, cy, lz + dz);
            }
        }
    }
    set_leaf_if_air(chunk, lx, ly + trunk + 2, lz);
}

void maybe_place_tree(SdkChunk* chunk, SdkWorldGen* wg, int lx, int ly, int lz,
                             const SdkTerrainColumnProfile* profile)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    const SdkWorldGenTreeRule* rule;
    int wx;
    int wz;
    uint32_t h;
    float crown_roll;
    int trunk;
    int t;
    int style;

    if (!impl) return;
    if (lx < 2 || lz < 2 || lx >= CHUNK_WIDTH - 2 || lz >= CHUNK_DEPTH - 2) return;
    if (ly + 12 >= CHUNK_HEIGHT) return;

    rule = sdk_worldgen_tree_rule_for_profile(profile);
    if (!rule || rule->archetype == SDK_WORLDGEN_TREE_NONE || rule->chance <= 0.0f) return;
    style = (int)rule->archetype;

    wx = chunk->cx * CHUNK_WIDTH + lx;
    wz = chunk->cz * CHUNK_DEPTH + lz;
    h = sdk_worldgen_hash2d(wx * 13 + 17, wz * 7 + 53, impl->seed ^ 0xCAF1u);
    if (sdk_worldgen_hashf(h) > rule->chance) return;

    crown_roll = sdk_worldgen_hashf(sdk_worldgen_hash2d(wx, wz, impl->seed ^ 0x11AAu));
    switch (style) {
        case 2: trunk = 6 + (int)(crown_roll * 4.0f); break;
        case 3: trunk = 7 + (int)(crown_roll * 5.0f); break;
        case 4: trunk = 2 + (int)(crown_roll * 2.0f); break;
        case 1:
        default: trunk = 4 + (int)(crown_roll * 3.0f); break;
    }
    for (t = 1; t <= trunk; ++t) {
        sdk_chunk_set_block(chunk, lx, ly + t, lz, BLOCK_LOG);
    }

    switch (style) {
        case 2:
            place_conifer_canopy(chunk, lx, ly, lz, trunk);
            break;
        case 3:
            place_tropical_canopy(chunk, lx, ly, lz, trunk);
            break;
        case 4:
            place_round_canopy(chunk, lx, ly, lz, trunk);
            break;
        case 1:
        default:
            place_round_canopy(chunk, lx, ly, lz, trunk);
            break;
    }

    sdk_worldgen_debug_capture_note_tree(lx, lz, (uint8_t)style, trunk);
}

void maybe_place_surface_plant(SdkChunk* chunk, SdkWorldGen* wg, int lx, int ly, int lz,
                                      const SdkTerrainColumnProfile* profile, BlockType top_block)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    const SdkWorldGenPlantRuleSet* rule_set;
    uint32_t h;
    float hf;
    int wx;
    int wz;
    int plant_y;

    if (!chunk || !wg || !impl || !profile) return;
    if (ly <= 0 || ly + 1 >= CHUNK_HEIGHT) return;
    if (profile->water_height > profile->surface_height) return;
    if (!sdk_worldgen_top_block_supports_surface_flora(top_block)) return;

    plant_y = ly + 1;
    if (sdk_chunk_get_block(chunk, lx, plant_y, lz) != BLOCK_AIR) return;

    wx = chunk->cx * CHUNK_WIDTH + lx;
    wz = chunk->cz * CHUNK_DEPTH + lz;
    h = sdk_worldgen_hash2d(wx * 29 + 7, wz * 17 + 11, impl->seed ^ 0x71C3u);
    hf = sdk_worldgen_hashf(h);
    rule_set = sdk_worldgen_plant_rules_for_profile(profile);
    if (!rule_set) return;

    for (int rule_index = 0; rule_index < (int)rule_set->count; ++rule_index) {
        const SdkWorldGenPlantEntry* rule = &rule_set->entries[rule_index];
        if (rule->block == BLOCK_AIR || rule->roll_limit <= 0.0f) continue;
        if (profile->moisture_band < rule->min_moisture_band) continue;
        if (hf < rule->roll_limit) {
            sdk_chunk_set_block(chunk, lx, plant_y, lz, rule->block);
            sdk_worldgen_debug_capture_note_plant(lx, lz, rule->block);
            break;
        }
    }
}

static int surface_column_index(int lx, int lz)
{
    return lz * CHUNK_WIDTH + lx;
}

static uint8_t surface_kind_for_block(BlockType block)
{
    switch (block) {
        case BLOCK_WATER:   return SDK_WORLDGEN_SURFACE_COLUMN_OPEN_WATER;
        case BLOCK_ICE:     return SDK_WORLDGEN_SURFACE_COLUMN_SEASONAL_ICE;
        case BLOCK_SEA_ICE: return SDK_WORLDGEN_SURFACE_COLUMN_PERENNIAL_ICE;
        case BLOCK_LAVA:    return SDK_WORLDGEN_SURFACE_COLUMN_LAVA;
        default:            return SDK_WORLDGEN_SURFACE_COLUMN_DRY;
    }
}

static void surface_set_top_if_visible(SdkWorldGenSurfaceColumn* column,
                                       BlockType block,
                                       int y,
                                       int replace_equal)
{
    uint8_t kind;

    if (!column || block == BLOCK_AIR) return;
    if (y < 0 || y >= CHUNK_HEIGHT) return;
    if (y < (int)column->top_height) return;
    if (y == (int)column->top_height && !replace_equal) return;

    kind = surface_kind_for_block(block);
    column->top_height = (uint16_t)y;
    column->top_block = block;
    column->kind = kind;
    if (kind == SDK_WORLDGEN_SURFACE_COLUMN_OPEN_WATER ||
        kind == SDK_WORLDGEN_SURFACE_COLUMN_SEASONAL_ICE ||
        kind == SDK_WORLDGEN_SURFACE_COLUMN_PERENNIAL_ICE) {
        column->water_height = (uint16_t)y;
    } else {
        column->land_height = (uint16_t)y;
    }
}

static int surface_lava_top_for_profile(const SdkTerrainColumnProfile* profile,
                                        const SdkRegionFieldSample* geology)
{
    int lava_top;

    if (!profile || !geology) return -1;
    if (profile->water_height > profile->surface_height) return -1;
    if (geology->vent_mask <= 0.82f ||
        geology->caldera_mask <= 0.28f ||
        geology->lava_flow_bias <= 0.18f) {
        return -1;
    }
    if (profile->terrain_province != TERRAIN_PROVINCE_VOLCANIC_ARC &&
        profile->terrain_province != TERRAIN_PROVINCE_BASALT_PLATEAU) {
        return -1;
    }

    lava_top = profile->surface_height + 1 +
               sdk_worldgen_clampi((int)lrintf(geology->caldera_mask * 2.5f +
                                               geology->lava_flow_bias * 1.5f),
                                   0,
                                   3);
    if (lava_top >= CHUNK_HEIGHT) lava_top = CHUNK_HEIGHT - 1;
    return lava_top;
}

static void surface_init_from_profile(const SdkTerrainColumnProfile* profile,
                                      const SdkRegionFieldSample* geology,
                                      SdkWorldGenSurfaceColumn* out_column)
{
    BlockType land_block;
    BlockType water_block = BLOCK_WATER;
    int surface_y;
    int water_y;
    int lava_top;

    if (!profile || !out_column) return;

    memset(out_column, 0, sizeof(*out_column));
    surface_y = sdk_worldgen_clampi((int)profile->surface_height, 0, CHUNK_HEIGHT - 1);
    water_y = sdk_worldgen_clampi((int)profile->water_height, 0, CHUNK_HEIGHT - 1);
    land_block = soil_block_for_profile(profile);

    out_column->top_height = (uint16_t)surface_y;
    out_column->land_height = (uint16_t)surface_y;
    out_column->water_height = (uint16_t)water_y;
    out_column->top_block = land_block;
    out_column->land_block = land_block;
    out_column->kind = SDK_WORLDGEN_SURFACE_COLUMN_DRY;

    if (water_y > surface_y) {
        if (profile->water_surface_class == SURFACE_WATER_SEASONAL_ICE ||
            profile->water_surface_class == SURFACE_WATER_PERENNIAL_ICE) {
            water_block = BLOCK_ICE;
            if (profile->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN ||
                profile->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
                profile->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) {
                water_block = BLOCK_SEA_ICE;
            }
        }
        out_column->top_height = (uint16_t)water_y;
        out_column->top_block = water_block;
        out_column->water_depth = (uint16_t)(water_y - surface_y);
        out_column->kind = surface_kind_for_block(water_block);
        return;
    }

    lava_top = surface_lava_top_for_profile(profile, geology);
    if (lava_top > surface_y) {
        surface_set_top_if_visible(out_column, BLOCK_LAVA, lava_top, 0);
    }
}

static int surface_tree_trunk_height(const SdkWorldGen* wg,
                                     int wx,
                                     int wz,
                                     int style)
{
    const SdkWorldGenImpl* impl = (const SdkWorldGenImpl*)wg->impl;
    float crown_roll;

    if (!impl) return 0;
    crown_roll = sdk_worldgen_hashf(sdk_worldgen_hash2d(wx, wz, impl->seed ^ 0x11AAu));
    switch (style) {
        case SDK_WORLDGEN_TREE_CONIFER:  return 6 + (int)(crown_roll * 4.0f);
        case SDK_WORLDGEN_TREE_TROPICAL: return 7 + (int)(crown_roll * 5.0f);
        case SDK_WORLDGEN_TREE_SHRUB:    return 2 + (int)(crown_roll * 2.0f);
        case SDK_WORLDGEN_TREE_ROUND:
        default:                         return 4 + (int)(crown_roll * 3.0f);
    }
}

static void surface_apply_round_canopy(SdkWorldGenSurfaceColumn* columns,
                                       int lx,
                                       int lz,
                                       int base_y,
                                       int trunk)
{
    for (int dy = 0; dy <= 3; ++dy) {
        int radius = (dy <= 1) ? 2 : 1;
        int cy = base_y + trunk - 1 + dy;

        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                int tx = lx + dx;
                int tz = lz + dz;

                if ((unsigned)tx >= CHUNK_WIDTH || (unsigned)tz >= CHUNK_DEPTH) continue;
                if (dx == 0 && dz == 0 && dy < 2) continue;
                if (dx * dx + dz * dz > radius * radius + 1) continue;
                surface_set_top_if_visible(&columns[surface_column_index(tx, tz)], BLOCK_LEAVES, cy, 0);
            }
        }
    }
}

static void surface_apply_conifer_canopy(SdkWorldGenSurfaceColumn* columns,
                                         int lx,
                                         int lz,
                                         int base_y,
                                         int trunk)
{
    for (int level = 0; level < trunk; ++level) {
        int radius = (level < 2) ? 2 : ((level < trunk - 2) ? 1 : 0);
        int cy = base_y + trunk - level;

        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                int tx = lx + dx;
                int tz = lz + dz;

                if ((unsigned)tx >= CHUNK_WIDTH || (unsigned)tz >= CHUNK_DEPTH) continue;
                if (abs(dx) + abs(dz) > radius + 1) continue;
                if (dx == 0 && dz == 0) continue;
                surface_set_top_if_visible(&columns[surface_column_index(tx, tz)], BLOCK_LEAVES, cy, 0);
            }
        }
    }

    surface_set_top_if_visible(&columns[surface_column_index(lx, lz)], BLOCK_LEAVES, base_y + trunk + 1, 0);
}

static void surface_apply_tropical_canopy(SdkWorldGenSurfaceColumn* columns,
                                          int lx,
                                          int lz,
                                          int base_y,
                                          int trunk)
{
    for (int dy = 0; dy <= 2; ++dy) {
        int radius = 2 + (dy == 1);
        int cy = base_y + trunk - 1 + dy;

        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                int tx = lx + dx;
                int tz = lz + dz;

                if ((unsigned)tx >= CHUNK_WIDTH || (unsigned)tz >= CHUNK_DEPTH) continue;
                if (dx == 0 && dz == 0 && dy == 0) continue;
                if (dx * dx + dz * dz > radius * radius + 1) continue;
                surface_set_top_if_visible(&columns[surface_column_index(tx, tz)], BLOCK_LEAVES, cy, 0);
            }
        }
    }

    surface_set_top_if_visible(&columns[surface_column_index(lx, lz)], BLOCK_LEAVES, base_y + trunk + 2, 0);
}

static void surface_apply_tree_overlay(SdkWorldGenSurfaceColumn* columns,
                                       const SdkWorldGen* wg,
                                       int chunk_wx0,
                                       int chunk_wz0,
                                       int lx,
                                       int lz,
                                       const SdkTerrainColumnProfile* profile)
{
    const SdkWorldGenImpl* impl = (const SdkWorldGenImpl*)wg->impl;
    const SdkWorldGenTreeRule* rule;
    BlockType top_block;
    uint32_t h;
    int wx;
    int wz;
    int trunk;
    int style;
    int base_y;
    if (!columns || !wg || !impl || !profile) return;
    if (lx < 2 || lz < 2 || lx >= CHUNK_WIDTH - 2 || lz >= CHUNK_DEPTH - 2) return;

    top_block = soil_block_for_profile(profile);
    if (profile->water_height > profile->surface_height) return;
    if (!sdk_worldgen_top_block_supports_surface_flora(top_block)) return;

    base_y = profile->surface_height;
    if (base_y + 12 >= CHUNK_HEIGHT) return;

    rule = sdk_worldgen_tree_rule_for_profile(profile);
    if (!rule || rule->archetype == SDK_WORLDGEN_TREE_NONE || rule->chance <= 0.0f) return;

    wx = chunk_wx0 + lx;
    wz = chunk_wz0 + lz;
    h = sdk_worldgen_hash2d(wx * 13 + 17, wz * 7 + 53, impl->seed ^ 0xCAF1u);
    if (sdk_worldgen_hashf(h) > rule->chance) return;

    style = (int)rule->archetype;
    trunk = surface_tree_trunk_height(wg, wx, wz, style);
    for (int t = 1; t <= trunk; ++t) {
        surface_set_top_if_visible(&columns[surface_column_index(lx, lz)], BLOCK_LOG, base_y + t, 1);
    }

    switch (style) {
        case SDK_WORLDGEN_TREE_CONIFER:
            surface_apply_conifer_canopy(columns, lx, lz, base_y, trunk);
            break;
        case SDK_WORLDGEN_TREE_TROPICAL:
            surface_apply_tropical_canopy(columns, lx, lz, base_y, trunk);
            break;
        case SDK_WORLDGEN_TREE_SHRUB:
        case SDK_WORLDGEN_TREE_ROUND:
        default:
            surface_apply_round_canopy(columns, lx, lz, base_y, trunk);
            break;
    }
}

int sdk_worldgen_generate_chunk_surface_ctx(SdkWorldGen* wg,
                                            int cx,
                                            int cz,
                                            SdkTerrainColumnProfile* scratch_profiles,
                                            SdkWorldGenSurfaceColumn* out_columns)
{
    SdkWorldGenRegionTile* region_tile;
    int chunk_wx0;
    int chunk_wz0;

    if (!wg || !wg->impl || !scratch_profiles || !out_columns) return 0;
    region_tile = sdk_worldgen_require_region_tile(wg, cx * CHUNK_WIDTH, cz * CHUNK_DEPTH);
    if (!region_tile) return 0;

    chunk_wx0 = cx * CHUNK_WIDTH;
    chunk_wz0 = cz * CHUNK_DEPTH;
    memset(out_columns, 0, sizeof(*out_columns) * CHUNK_BLOCKS_PER_LAYER);

    for (int lz = 0; lz < CHUNK_DEPTH; ++lz) {
        int wz = chunk_wz0 + lz;
        for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
            SdkTerrainColumnProfile* profile = &scratch_profiles[surface_column_index(lx, lz)];
            SdkWorldGenSurfaceColumn* column = &out_columns[surface_column_index(lx, lz)];
            SdkRegionFieldSample geology;
            int wx = chunk_wx0 + lx;

            sdk_worldgen_sample_column_from_region_tile(region_tile, wg, wx, wz, profile);
            memset(&geology, 0, sizeof(geology));
            if (profile->water_height <= profile->surface_height &&
                (profile->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ||
                 profile->terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU)) {
                sdk_worldgen_sample_region_fields(region_tile, wg, wx, wz, &geology);
            }
            surface_init_from_profile(profile, &geology, column);
        }
    }

    for (int lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
            surface_apply_tree_overlay(out_columns,
                                       wg,
                                       chunk_wx0,
                                       chunk_wz0,
                                       lx,
                                       lz,
                                       &scratch_profiles[surface_column_index(lx, lz)]);
        }
    }

    return 1;
}

