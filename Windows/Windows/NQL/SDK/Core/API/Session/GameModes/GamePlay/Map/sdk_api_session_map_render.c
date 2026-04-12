#include "sdk_api_session_internal.h"

/* ============================================================================
 * Map Rendering - Color Utilities
 * ============================================================================ */

/**
 * Clamps integer value to uint8_t range (0-255)
 *
 * @param value The value to clamp
 * @return The clamped value
 */
uint8_t map_clamp_u8(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

/**
 * Applies shade multiplier to RGB color values
 *
 * @param color The color to shade
 * @param shade The shade multiplier (0.55-1.30)
 * @return The shaded color
 */
uint32_t map_shade_color(uint32_t color, float shade)
{
    uint32_t alpha = color & 0xFF000000u;
    int r, g, b;

    if (shade < 0.55f) shade = 0.55f;
    if (shade > 1.30f) shade = 1.30f;

    r = (int)((float)(color & 0xFFu) * shade + 0.5f);
    g = (int)((float)((color >> 8) & 0xFFu) * shade + 0.5f);
    b = (int)((float)((color >> 16) & 0xFFu) * shade + 0.5f);

    return alpha |
        ((uint32_t)map_clamp_u8(b) << 16) |
        ((uint32_t)map_clamp_u8(g) << 8) |
        (uint32_t)map_clamp_u8(r);
}

/**
 * Forces alpha channel to fully opaque (255)
 *
 * @param color The color to make opaque
 * @return The opaque color
 */
uint32_t map_opaque_color(uint32_t color)
{
    return color | 0xFF000000u;
}

/**
 * Blends two colors with given overlay mix ratio (0.0-1.0)
 *
 * @param base_color The base color
 * @param overlay_color The overlay color
 * @param overlay_mix The overlay mix ratio
 * @return The blended color
 */
uint32_t map_blend_color(uint32_t base_color, uint32_t overlay_color, float overlay_mix)
{
    int base_r;
    int base_g;
    int base_b;
    int over_r;
    int over_g;
    int over_b;
    int out_r;
    int out_g;
    int out_b;

    if (overlay_mix < 0.0f) overlay_mix = 0.0f;
    if (overlay_mix > 1.0f) overlay_mix = 1.0f;

    base_color = map_opaque_color(base_color);
    overlay_color = map_opaque_color(overlay_color);
    base_r = (int)(base_color & 0xFFu);
    base_g = (int)((base_color >> 8) & 0xFFu);
    base_b = (int)((base_color >> 16) & 0xFFu);
    over_r = (int)(overlay_color & 0xFFu);
    over_g = (int)((overlay_color >> 8) & 0xFFu);
    over_b = (int)((overlay_color >> 16) & 0xFFu);

    out_r = (int)((float)base_r * (1.0f - overlay_mix) + (float)over_r * overlay_mix + 0.5f);
    out_g = (int)((float)base_g * (1.0f - overlay_mix) + (float)over_g * overlay_mix + 0.5f);
    out_b = (int)((float)base_b * (1.0f - overlay_mix) + (float)over_b * overlay_mix + 0.5f);

    return 0xFF000000u |
        ((uint32_t)map_clamp_u8(out_b) << 16) |
        ((uint32_t)map_clamp_u8(out_g) << 8) |
        (uint32_t)map_clamp_u8(out_r);
}

/* ============================================================================
 * Map Rendering - World/Grid Helpers
 * ============================================================================ */

/**
 * Calculates superchunk origin for world coordinate
 *
 * @param world_coord The world coordinate
 * @return The superchunk origin
 */
int superchunk_origin_for_world(int world_coord)
{
    /* Calculates superchunk origin for world coordinate */
    const int tile_blocks = session_map_superchunk_tile_blocks();
    return spawn_floor_div_i(world_coord, tile_blocks) * tile_blocks;
}

/**
 * Checks if local coordinate is within gate run range
 *
 * @param local_coord The local coordinate
 * @return True if within gate run range, false otherwise
 */
int map_is_gate_run_coord(int local_coord)
{
    /* Checks if local coordinate is within gate run range */
    int run_coord = local_coord - SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS;
    return run_coord >= SDK_SUPERCHUNK_GATE_START_BLOCK &&
           run_coord <= SDK_SUPERCHUNK_GATE_END_BLOCK;
}

/**
 * Returns true if local coordinates are in wall band (excluding gates)
 *
 * @param local_x The local x coordinate
 * @param local_z The local z coordinate
 * @return True if in wall band, false otherwise
 */
int map_is_wall_band_local(int local_x, int local_z)
{
    int west = (local_x < 64);
    int north = (local_z < 64);

    if (west && map_is_gate_run_coord(local_z)) west = 0;
    if (north && map_is_gate_run_coord(local_x)) north = 0;

    return west || north;
}

/**
 * Returns wall color for local coords, with buttress variant if applicable
 *
 * @param local_x The local x coordinate
 * @param local_z The local z coordinate
 * @return The wall color
 */
uint32_t map_wall_color_for_local(int local_x, int local_z)
{
    /* Returns wall color for local coords, with buttress variant if applicable */
    int buttress = 0;
    int run_z = local_z - SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS;
    int run_x = local_x - SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS;
    uint32_t color = sdk_block_get_face_color(BLOCK_STONE_BRICKS, 3);

    if ((local_x < 64) &&
        !map_is_gate_run_coord(local_z) &&
        (((run_z % 128) + 128) % 128 < 24 || ((run_z % 128) + 128) % 128 >= 104)) {
        buttress = 1;
    }
    if ((local_z < 64) &&
        !map_is_gate_run_coord(local_x) &&
        (((run_x % 128) + 128) % 128 < 24 || ((run_x % 128) + 128) % 128 >= 104)) {
        buttress = 1;
    }

    if (buttress) {
        color = sdk_block_get_face_color(BLOCK_COBBLESTONE, 3);
    }
    return color;
}

/* ============================================================================
 * Map Rendering - Block Profile Helpers
 * ============================================================================ */

/**
 * Returns bedrock block type based on terrain column profile province
 *
 * @param profile The terrain column profile
 * @return The bedrock block type
 */
BlockType map_bedrock_block_for_profile(const SdkTerrainColumnProfile* profile)
{
    if (!profile) return BLOCK_STONE;
    switch (profile->bedrock_province) {
        case BEDROCK_PROVINCE_OCEANIC_BASALT:         return BLOCK_BASALT;
        case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:  return BLOCK_GNEISS;
        case BEDROCK_PROVINCE_METAMORPHIC_BELT:       return BLOCK_SCHIST;
        case BEDROCK_PROVINCE_GRANITIC_INTRUSIVE:     return BLOCK_GRANITE;
        case BEDROCK_PROVINCE_SILICICLASTIC_BASIN:    return BLOCK_SHALE;
        case BEDROCK_PROVINCE_CARBONATE_PLATFORM:     return BLOCK_LIMESTONE;
        case BEDROCK_PROVINCE_RIFT_SEDIMENTARY:       return BLOCK_SANDSTONE;
        case BEDROCK_PROVINCE_VOLCANIC_ARC:           return BLOCK_ANDESITE;
        case BEDROCK_PROVINCE_FLOOD_BASALT:           return BLOCK_BASALT;
        default:                                      return BLOCK_STONE;
    }
}

/**
 * Returns string name for map build kind enum
 *
 * @param build_kind The map build kind
 * @return The string name
 */
const char* map_build_kind_name(uint8_t build_kind)
{
    switch ((SdkMapBuildKind)build_kind) {
        case SDK_MAP_BUILD_EXACT_OFFLINE:         return "EXACT";
        case SDK_MAP_BUILD_INTERACTIVE_FALLBACK:  return "FALLBACK";
        default:                                  return "UNKNOWN";
    }
}

BlockType map_ground_block_for_profile(const SdkTerrainColumnProfile* profile)
{
    SdkTerrainColumnProfile dry_profile;

    if (!profile) return BLOCK_AIR;
    dry_profile = *profile;
    if (dry_profile.water_height > dry_profile.surface_height) {
        dry_profile.water_height = dry_profile.surface_height;
        dry_profile.water_surface_class = SURFACE_WATER_NONE;
    }
    return sdk_worldgen_visual_surface_block_for_profile(&dry_profile);
}

/* ============================================================================
 * Map Rendering - Surface Color Computation
 * ============================================================================ */

uint32_t map_color_for_surface_state(BlockType land_block,
                                     int center_height,
                                     int west_height,
                                     int east_height,
                                     int north_height,
                                     int south_height,
                                     int sea_level,
                                     int water_depth,
                                     int dry_count,
                                     int water_count,
                                     int submerged_count,
                                     int seasonal_ice_count,
                                     int perennial_ice_count,
                                     int alpine_bonus)
{
    uint32_t land_color;
    uint32_t water_color;
    float land_shade;
    float water_shade;
    int total_count;
    int relief;

    if (land_block <= BLOCK_AIR || land_block >= BLOCK_COUNT) {
        land_block = BLOCK_STONE;
    }

    land_color = map_opaque_color(sdk_block_get_face_color(land_block, 3));
    water_color = map_opaque_color(sdk_block_get_face_color(BLOCK_WATER, 3));
    relief = (west_height - east_height) + (north_height - south_height);
    land_shade = 0.92f + (float)relief * 0.015f;
    if (center_height > sea_level + 120) land_shade += 0.05f;
    if (alpine_bonus) land_shade += 0.05f;

    if (water_count <= 0 || dry_count >= water_count) {
        return map_shade_color(land_color, land_shade);
    }

    if (water_depth < 0) water_depth = 0;
    water_shade = 1.00f - (float)(water_depth < 12 ? water_depth : 12) * 0.02f;
    if (perennial_ice_count > 0 &&
        perennial_ice_count >= seasonal_ice_count &&
        perennial_ice_count * 2 >= water_count &&
        perennial_ice_count >= dry_count) {
        return map_shade_color(map_opaque_color(sdk_block_get_face_color(BLOCK_SEA_ICE, 3)),
                               water_shade + 0.06f);
    }
    if (seasonal_ice_count > 0 &&
        seasonal_ice_count * 2 >= water_count &&
        seasonal_ice_count >= dry_count) {
        return map_shade_color(map_opaque_color(sdk_block_get_face_color(BLOCK_ICE, 3)),
                               water_shade + 0.03f);
    }

    if (water_depth <= 8 && submerged_count > 0) {
        float water_mix;
        float coverage;

        total_count = dry_count + water_count;
        if (total_count <= 0) total_count = 1;
        coverage = (float)water_count / (float)total_count;
        if (water_depth <= 2) {
            water_mix = 0.18f + (float)water_depth * 0.05f;
        } else if (water_depth <= 5) {
            water_mix = 0.28f + (float)(water_depth - 2) * 0.08f;
        } else {
            water_mix = 0.52f + (float)(water_depth - 5) * 0.12f;
        }
        water_mix *= 0.45f + 0.55f * coverage;
        return map_shade_color(map_blend_color(land_color, water_color, water_mix), water_shade);
    }

    return map_shade_color(water_color, water_shade);
}

/* ============================================================================
 * Map Rendering - Public API
 * ============================================================================ */

uint32_t sdk_map_color_for_profiles(const SdkTerrainColumnProfile* center,
                                    const SdkTerrainColumnProfile* west,
                                    const SdkTerrainColumnProfile* east,
                                    const SdkTerrainColumnProfile* north,
                                    const SdkTerrainColumnProfile* south,
                                    int sea_level)
{
    BlockType land_block;
    int west_h;
    int east_h;
    int north_h;
    int south_h;
    int water_depth;
    int water_count;
    int submerged_count;
    int seasonal_ice_count;
    int perennial_ice_count;
    int dry_count;

    if (!center) return 0xFF202020u;

    land_block = map_ground_block_for_profile(center);
    west_h = west ? west->surface_height : center->surface_height;
    east_h = east ? east->surface_height : center->surface_height;
    north_h = north ? north->surface_height : center->surface_height;
    south_h = south ? south->surface_height : center->surface_height;
    water_depth = (center->water_height > center->surface_height)
                      ? ((int)center->water_height - (int)center->surface_height)
                      : 0;
    water_count = (water_depth > 0) ? 1 : 0;
    submerged_count = (water_depth > 0 && land_block > BLOCK_AIR) ? 1 : 0;
    seasonal_ice_count = (center->water_surface_class == SURFACE_WATER_SEASONAL_ICE) ? 1 : 0;
    perennial_ice_count = (center->water_surface_class == SURFACE_WATER_PERENNIAL_ICE) ? 1 : 0;
    dry_count = (water_count > 0) ? 0 : 1;

    return map_color_for_surface_state(
        land_block,
        center->surface_height,
        west_h,
        east_h,
        north_h,
        south_h,
        sea_level,
        water_depth,
        dry_count,
        water_count,
        submerged_count,
        seasonal_ice_count,
        perennial_ice_count,
        center->terrain_province == TERRAIN_PROVINCE_ALPINE_BELT);
}
