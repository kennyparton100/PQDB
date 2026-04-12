/**
 * sdk_worldgen_column_debug.c -- Worldgen debug color overlays.
 */
#include "sdk_worldgen_column_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static uint32_t darken_color(uint32_t color, float factor)
{
    uint8_t r = (uint8_t)(color & 0xFFu);
    uint8_t g = (uint8_t)((color >> 8) & 0xFFu);
    uint8_t b = (uint8_t)((color >> 16) & 0xFFu);
    factor = sdk_worldgen_clampf(factor, 0.0f, 1.0f);
    r = (uint8_t)((float)r * factor);
    g = (uint8_t)((float)g * factor);
    b = (uint8_t)((float)b * factor);
    return pack_rgb(r, g, b);
}

static bool debug_on_sample_grid(int world_coord)
{
    int rem = world_coord % SDK_WORLDGEN_REGION_SAMPLE_SPACING;
    if (rem < 0) rem += SDK_WORLDGEN_REGION_SAMPLE_SPACING;
    return rem == 0;
}

static bool debug_on_region_boundary(int world_coord)
{
    int rem = world_coord % SDK_WORLDGEN_REGION_TILE_BLOCKS;
    if (rem < 0) rem += SDK_WORLDGEN_REGION_TILE_BLOCKS;
    return rem == 0;
}

uint32_t sdk_worldgen_debug_color_ctx_impl(SdkWorldGen* wg, int wx, int wy, int wz, BlockType actual_block)
{
    SdkWorldGenDebugMode mode;
    SdkTerrainColumnProfile profile;
    SdkWorldGenRegionTile* region_tile;
    SdkRegionFieldSample geology;
    SdkStrataColumn strata;
    BlockType formation_block;
    SdkResourceBodyKind body_kind;
    bool on_sample_grid;
    bool on_region_boundary;
    bool on_boundary;

    if (!wg || !wg->impl) return sdk_block_get_color(actual_block);

    mode = sdk_worldgen_get_debug_mode_ctx(wg);
    if (mode == SDK_WORLDGEN_DEBUG_OFF) return sdk_block_get_color(actual_block);
    if (!sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) return sdk_block_get_color(actual_block);

    region_tile = sdk_worldgen_require_region_tile(wg, wx, wz);
    if (!region_tile) return sdk_block_get_color(actual_block);

    sdk_worldgen_sample_region_fields(region_tile, wg, wx, wz, &geology);
    build_strata_column(wg, &profile, &geology, wx, wz, profile.surface_height, &strata);
    formation_block = stratigraphic_block_for_y(&profile, &geology, &strata, wx, wy, wz);
    body_kind = resource_body_kind_at(&profile, &geology, &strata, formation_block, wy, profile.surface_height);

    on_sample_grid = debug_on_sample_grid(wx) || debug_on_sample_grid(wz);
    on_region_boundary = debug_on_region_boundary(wx) || debug_on_region_boundary(wz);
    on_boundary = (abs(wy - strata.weathered_base) <= 1) ||
                  (abs(wy - strata.upper_top) <= 1) ||
                  (abs(wy - strata.lower_top) <= 1) ||
                  (abs(wy - strata.basement_top) <= 1) ||
                  (abs(wy - strata.deep_basement_top) <= 1);

    if (on_region_boundary) return pack_rgb(255, 255, 255);
    if (on_sample_grid && mode != SDK_WORLDGEN_DEBUG_BODIES) return pack_rgb(210, 235, 255);

    switch (mode) {
        case SDK_WORLDGEN_DEBUG_FORMATIONS:
            if (on_boundary) return pack_rgb(255, 250, 210);
            return sdk_block_get_color(formation_block);

        case SDK_WORLDGEN_DEBUG_STRUCTURES: {
            SdkSuperChunkWallProfile wall_profile;
            uint32_t base = sdk_block_get_color(formation_block);
            float structure_strength = sdk_worldgen_clampf(0.35f + geology.fault_mask * 0.65f, 0.35f, 1.0f);
            if (compute_superchunk_wall_profile(wg, &profile, wx, wz, &wall_profile) &&
                wy <= wall_profile.wall_top_y) {
                int local_super_x = floor_mod_superchunk(wx, SDK_SUPERCHUNK_WALL_PERIOD * CHUNK_WIDTH);
                int local_super_z = floor_mod_superchunk(wz, SDK_SUPERCHUNK_WALL_PERIOD * CHUNK_WIDTH);
                if (superchunk_wall_gate_open_at(&wall_profile, local_super_x, local_super_z, wy)) {
                    return pack_rgb(255, 214, 128);
                }
                if (wy >= wall_profile.wall_top_y - 2) {
                    return pack_rgb(228, 228, 236);
                }
                return pack_rgb(158, 155, 150);
            }
            if (geology.fault_mask > 0.72f && wy <= strata.weathered_base) {
                return pack_rgb(255, 96, 48);
            }
            if (on_boundary) return pack_rgb(255, 210, 96);
            if (actual_block == BLOCK_VEIN_QUARTZ) return pack_rgb(232, 232, 255);
            return darken_color(base, 0.45f + structure_strength * 0.35f);
        }

        case SDK_WORLDGEN_DEBUG_BODIES:
            switch (body_kind) {
                case SDK_RESOURCE_BODY_COAL: return pack_rgb(24, 24, 24);
                case SDK_RESOURCE_BODY_CLAY: return pack_rgb(184, 118, 88);
                case SDK_RESOURCE_BODY_IRONSTONE: return pack_rgb(132, 92, 72);
                case SDK_RESOURCE_BODY_COPPER: return pack_rgb(196, 122, 54);
                case SDK_RESOURCE_BODY_SULFUR: return pack_rgb(228, 210, 58);
                case SDK_RESOURCE_BODY_LEAD_ZINC: return pack_rgb(168, 148, 196);
                case SDK_RESOURCE_BODY_TUNGSTEN: return pack_rgb(86, 120, 152);
                case SDK_RESOURCE_BODY_BAUXITE: return pack_rgb(138, 126, 88);
                case SDK_RESOURCE_BODY_SALT: return pack_rgb(240, 244, 248);
                case SDK_RESOURCE_BODY_OIL: return pack_rgb(72, 56, 40);
                case SDK_RESOURCE_BODY_GAS: return pack_rgb(116, 138, 160);
                case SDK_RESOURCE_BODY_RARE_EARTH: return pack_rgb(164, 132, 112);
                case SDK_RESOURCE_BODY_URANIUM: return pack_rgb(96, 142, 92);
                case SDK_RESOURCE_BODY_SALTPETER: return pack_rgb(212, 202, 180);
                case SDK_RESOURCE_BODY_PHOSPHATE: return pack_rgb(146, 140, 112);
                case SDK_RESOURCE_BODY_CHROMIUM: return pack_rgb(90, 102, 120);
                case SDK_RESOURCE_BODY_ALUMINUM: return pack_rgb(122, 138, 162);
                case SDK_RESOURCE_BODY_NONE:
                default:
                    if (geology.fault_mask > 0.78f && wy <= strata.weathered_base) return pack_rgb(228, 96, 80);
                    if (on_sample_grid) return pack_rgb(205, 230, 255);
                    return darken_color(sdk_block_get_color(actual_block), 0.48f);
            }

        case SDK_WORLDGEN_DEBUG_OFF:
        default:
            return sdk_block_get_color(actual_block);
    }
}
