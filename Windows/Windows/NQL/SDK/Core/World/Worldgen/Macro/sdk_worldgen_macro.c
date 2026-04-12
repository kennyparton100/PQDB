/**
 * sdk_worldgen_macro.c -- Analytic macro world and lithologic provinces.
 */
#include "../Internal/sdk_worldgen_internal.h"
#include <math.h>
#include <string.h>

typedef struct {
    float x;
    float z;
    float vx;
    float vz;
    uint8_t plate_class;
    float hotspot;
} PlateSite;

static int floor_div_local(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return (value - denom + 1) / denom;
}

static int tile_index(int x, int z)
{
    return z * SDK_WORLDGEN_TILE_STRIDE + x;
}

static float ramp01(float v, float lo, float hi)
{
    if (hi <= lo) return (v >= hi) ? 1.0f : 0.0f;
    return sdk_worldgen_clampf((v - lo) / (hi - lo), 0.0f, 1.0f);
}

static void build_plate_site(int gx, int gz, uint32_t seed, PlateSite* out_site)
{
    uint32_t h0 = sdk_worldgen_hash2d(gx, gz, seed ^ 0x2143u);
    uint32_t h1 = sdk_worldgen_hash2d(gx, gz, seed ^ 0x6711u);
    uint32_t h2 = sdk_worldgen_hash2d(gx, gz, seed ^ 0xA817u);
    uint32_t h3 = sdk_worldgen_hash2d(gx, gz, seed ^ 0xC103u);
    float angle;
    float speed;
    float class_roll;

    out_site->x = (float)gx + 0.2f + sdk_worldgen_hashf(h0) * 0.6f;
    out_site->z = (float)gz + 0.2f + sdk_worldgen_hashf(h1) * 0.6f;

    angle = sdk_worldgen_hashf(h2) * 6.2831853f;
    speed = 0.25f + sdk_worldgen_hashf(h3) * 0.75f;
    out_site->vx = cosf(angle) * speed;
    out_site->vz = sinf(angle) * speed;

    class_roll = sdk_worldgen_hashf(sdk_worldgen_hash2d(gx, gz, seed ^ 0x55AAu));
    if (class_roll < 0.28f) out_site->plate_class = 0;      /* oceanic */
    else if (class_roll < 0.78f) out_site->plate_class = 1; /* continental */
    else out_site->plate_class = 2;                         /* mixed */

    out_site->hotspot = sdk_worldgen_hashf(sdk_worldgen_hash2d(gx, gz, seed ^ 0xCC33u));
}

static void sample_plate_frame(int macro_x, int macro_z, uint32_t seed,
                               uint8_t* out_plate_class, int8_t* out_boundary_class,
                               float* out_boundary_strength, float* out_hotspot)
{
    const int grid_size = 72;
    int gx = floor_div_local(macro_x, grid_size);
    int gz = floor_div_local(macro_z, grid_size);
    PlateSite nearest;
    PlateSite second;
    float best_d2 = 1.0e30f;
    float next_d2 = 1.0e30f;
    int ox;
    int oz;

    memset(&nearest, 0, sizeof(nearest));
    memset(&second, 0, sizeof(second));

    for (oz = -1; oz <= 1; ++oz) {
        for (ox = -1; ox <= 1; ++ox) {
            PlateSite site;
            float sx;
            float sz;
            float dx;
            float dz;
            float d2;

            build_plate_site(gx + ox, gz + oz, seed, &site);
            sx = site.x * (float)grid_size;
            sz = site.z * (float)grid_size;
            dx = (float)macro_x - sx;
            dz = (float)macro_z - sz;
            d2 = dx * dx + dz * dz;

            if (d2 < best_d2) {
                second = nearest;
                next_d2 = best_d2;
                nearest = site;
                best_d2 = d2;
            } else if (d2 < next_d2) {
                second = site;
                next_d2 = d2;
            }
        }
    }

    *out_plate_class = nearest.plate_class;
    *out_hotspot = nearest.hotspot;
    *out_boundary_strength = 0.0f;
    *out_boundary_class = 0;

    if (next_d2 < 1.0e20f) {
        float px = second.x - nearest.x;
        float pz = second.z - nearest.z;
        float plen = sqrtf(px * px + pz * pz);
        float nx;
        float nz;
        float rel_vx;
        float rel_vz;
        float normal_motion;
        float delta = sqrtf(next_d2) - sqrtf(best_d2);
        float strength;

        if (plen < 0.001f) plen = 1.0f;
        nx = px / plen;
        nz = pz / plen;
        rel_vx = second.vx - nearest.vx;
        rel_vz = second.vz - nearest.vz;
        normal_motion = rel_vx * nx + rel_vz * nz;

        strength = 1.0f - sdk_worldgen_clampf(delta / ((float)grid_size * 0.85f), 0.0f, 1.0f);
        *out_boundary_strength = strength;

        if (strength > 0.2f) {
            if (normal_motion < -0.18f) *out_boundary_class = 1;
            else if (normal_motion > 0.18f) *out_boundary_class = -1;
            else *out_boundary_class = 0;
        }
    }
}

static float pseudo_latitude(int macro_z, int macro_cell_size)
{
    float world_z = (float)(macro_z * macro_cell_size);
    float phase = fmodf(world_z / 131072.0f, 2.0f);
    if (phase < 0.0f) phase += 2.0f;
    return fabsf(phase - 1.0f); /* 0 = equator, 1 = pole */
}

static uint8_t classify_bedrock(uint8_t plate_class, int8_t boundary_class, float boundary_strength,
                                float hotspot, float continentality, float uplift, float warm_bias)
{
    if (continentality < -0.18f) return BEDROCK_PROVINCE_OCEANIC_BASALT;
    if (boundary_class > 0 && boundary_strength > 0.35f) {
        if (plate_class == 0) return BEDROCK_PROVINCE_VOLCANIC_ARC;
        if (sdk_worldgen_hashf(sdk_worldgen_hash2d((int)(continentality * 1000.0f), (int)(uplift * 1000.0f), 0xBEEFu)) > 0.55f)
            return BEDROCK_PROVINCE_GRANITIC_INTRUSIVE;
        return BEDROCK_PROVINCE_METAMORPHIC_BELT;
    }
    if (boundary_class < 0 && boundary_strength > 0.35f && continentality > 0.0f)
        return BEDROCK_PROVINCE_RIFT_SEDIMENTARY;
    if (hotspot > 0.92f && continentality > 0.05f)
        return BEDROCK_PROVINCE_FLOOD_BASALT;
    if (continentality > 0.24f && uplift < 0.35f && warm_bias > 0.45f)
        return BEDROCK_PROVINCE_CARBONATE_PLATFORM;
    if (continentality > 0.18f && plate_class == 1)
        return BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS;
    return BEDROCK_PROVINCE_SILICICLASTIC_BASIN;
}

static float compute_mountain_mask(uint8_t plate_class, int8_t boundary_class, float boundary_strength,
                                   float hotspot, float continentality, float uplift, float plateau_noise)
{
    float uplift_mask = ramp01(uplift, 18.0f, 135.0f);
    float convergent_mask = (boundary_class > 0) ? boundary_strength : 0.0f;
    float hotspot_mask = ramp01(hotspot, 0.88f, 1.0f) * 0.55f;
    float plateau_mask = (continentality > 0.08f) ? ramp01(plateau_noise, 0.08f, 0.70f) * 0.25f : 0.0f;
    float continental_mask = (continentality > 0.0f) ? ramp01(continentality, 0.02f, 0.60f) * 0.20f : 0.0f;
    float mask = uplift_mask * 0.58f + convergent_mask * 0.62f + hotspot_mask + plateau_mask + continental_mask;

    if (boundary_class < 0) mask *= 0.55f;
    if (plate_class == 0 && boundary_class <= 0) mask *= 0.40f;
    return sdk_worldgen_clampf(mask, 0.0f, 1.0f);
}

static float compute_mountain_surface_shape(int macro_x, int macro_z, uint32_t seed,
                                            float mountain_mask, float boundary_strength,
                                            float continentality, float hotspot)
{
    float warp_a;
    float warp_b;
    float warp_x;
    float warp_z;
    float crest;
    float breakup;
    float erosion_noise;
    float interior;
    float front_soften;
    float talus_margin;
    float shape;

    if (mountain_mask <= 0.01f) return 0.0f;

    warp_a = sdk_worldgen_fbm((float)macro_x * 0.011f, (float)macro_z * 0.011f, seed ^ 0x7411u, 2);
    warp_b = sdk_worldgen_fbm((float)macro_x * 0.013f, (float)macro_z * 0.013f, seed ^ 0x8422u, 2);
    warp_x = (float)macro_x * 0.028f + warp_a * 1.8f;
    warp_z = (float)macro_z * 0.028f + warp_b * 1.8f;

    crest = sdk_worldgen_ridged(warp_x, warp_z, seed ^ 0x7531u, 3);
    breakup = sdk_worldgen_fbm((float)macro_x * 0.084f, (float)macro_z * 0.084f, seed ^ 0x8642u, 3);
    erosion_noise = sdk_worldgen_fbm((float)macro_x * 0.048f + warp_a * 0.7f,
                                     (float)macro_z * 0.048f - warp_b * 0.7f,
                                     seed ^ 0x9753u, 3);

    interior = ramp01(mountain_mask, 0.25f, 0.82f);
    front_soften = 1.0f - ramp01(mountain_mask, 0.10f, 0.40f) * 0.35f;
    talus_margin = ramp01(mountain_mask, 0.18f, 0.52f) * (1.0f - interior);

    shape = ((crest - 0.52f) * 92.0f * (0.60f + interior * 0.40f) +
             breakup * 18.0f * front_soften) -
            ramp01(erosion_noise, 0.00f, 0.95f) * 24.0f * (0.45f + boundary_strength * 0.55f);

    shape *= mountain_mask;
    shape += talus_margin * (3.5f + fabsf(breakup) * 5.5f);

    if (continentality > 0.18f) {
        shape += ramp01(continentality, 0.18f, 0.52f) * 6.0f * mountain_mask;
    }
    if (hotspot > 0.94f) {
        shape += ramp01(hotspot, 0.94f, 1.0f) * 12.0f;
    }

    return shape;
}

static void assign_macro_cell(SdkWorldGen* wg, SdkWorldGenMacroTile* tile, int lx, int lz)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    int origin_x = tile->tile_x * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;
    int origin_z = tile->tile_z * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;
    int macro_x = origin_x + lx;
    int macro_z = origin_z + lz;
    int idx = tile_index(lx, lz);
    SdkWorldGenMacroCell* cell = &tile->cells[idx];
    float boundary_strength;
    float hotspot;
    uint8_t plate_class;
    int8_t boundary_class;
    SdkContinentalSample continent;
    int world_x;
    int world_z;
    float lat;
    float continental_noise;
    float archipelago_noise;
    float hotspot_chain;
    float continentality;
    float ridge_noise;
    float relief_noise;
    float plateau_noise;
    float uplift = 0.0f;
    float subsidence = 0.0f;
    float base;
    float continental_base;
    float shore_blend;
    float mountain_mask;
    float mountain_shape;
    float surface;
    float relief_strength;
    float warm_bias;
    float lake_strength;
    float closed_basin_strength;
    float sea = (float)impl->sea_level;

    memset(cell, 0, sizeof(*cell));
    sample_plate_frame(macro_x, macro_z, impl->seed, &plate_class, &boundary_class, &boundary_strength, &hotspot);
    world_x = macro_x * (int)impl->macro_cell_size + (int)impl->macro_cell_size / 2;
    world_z = macro_z * (int)impl->macro_cell_size + (int)impl->macro_cell_size / 2;
    sdk_worldgen_sample_continental_state(wg, world_x, world_z, &continent);
    lake_strength = sdk_worldgen_unpack_unorm8(continent.lake_mask);
    closed_basin_strength = sdk_worldgen_unpack_unorm8(continent.closed_basin_mask);

    lat = pseudo_latitude(macro_z, (int)impl->macro_cell_size);
    continental_noise = sdk_worldgen_fbm((float)macro_x * 0.018f, (float)macro_z * 0.018f, impl->seed ^ 0x1101u, 4);
    archipelago_noise = sdk_worldgen_ridged((float)macro_x * 0.052f, (float)macro_z * 0.052f, impl->seed ^ 0x2202u, 3);
    hotspot_chain = sdk_worldgen_ridged((float)macro_x * 0.094f, (float)macro_z * 0.031f, impl->seed ^ 0x3303u, 3);
    ridge_noise = sdk_worldgen_ridged((float)macro_x * 0.060f, (float)macro_z * 0.060f, impl->seed ^ 0x4404u, 3);
    relief_noise = sdk_worldgen_fbm((float)macro_x * 0.140f, (float)macro_z * 0.140f, impl->seed ^ 0x5505u, 2);
    plateau_noise = sdk_worldgen_fbm((float)macro_x * 0.026f, (float)macro_z * 0.026f, impl->seed ^ 0x6606u, 3);

    continentality = sdk_worldgen_clampf((continent.raw_height - sea) / 220.0f, -1.0f, 1.0f);
    continentality += continental_noise * 0.10f;
    continentality += (archipelago_noise - 0.55f) * 0.12f;
    if (plate_class == 1) continentality += 0.10f;
    else if (plate_class == 0) continentality -= 0.08f;
    if (hotspot > 0.92f) continentality += hotspot_chain * 0.08f;

    if (boundary_class > 0) {
        uplift += boundary_strength * (plate_class == 0 ? 80.0f : 130.0f) * (0.55f + ridge_noise * 0.45f);
    } else if (boundary_class < 0) {
        subsidence += boundary_strength * (plate_class == 1 ? 70.0f : 35.0f);
    } else {
        uplift += boundary_strength * 22.0f * fabsf(relief_noise);
    }

    if (hotspot > 0.95f) uplift += 70.0f * hotspot_chain;
    if (plate_class == 1 && plateau_noise > 0.25f) uplift += 24.0f * plateau_noise;

    continental_base = (continent.raw_height > 0.0f) ? continent.raw_height : sea;
    base = continental_base + uplift * 0.22f - subsidence * 0.24f;

    if (continentality > 0.20f) base += relief_noise * 8.0f;
    else if (continentality > -0.10f) base += relief_noise * 5.0f;
    else base += relief_noise * 3.0f;

    mountain_mask = compute_mountain_mask(plate_class, boundary_class, boundary_strength,
                                          hotspot, continentality, uplift, plateau_noise);
    mountain_shape = compute_mountain_surface_shape(macro_x, macro_z, impl->seed,
                                                    mountain_mask, boundary_strength,
                                                    continentality, hotspot);
    if (!continent.land_mask) {
        float island_gain = 0.0f;
        if (hotspot > 0.93f) island_gain += 18.0f + hotspot_chain * 30.0f;
        if (boundary_class > 0 && plate_class == 0) island_gain += boundary_strength * 24.0f;
        if (island_gain <= 0.0f) mountain_shape *= 0.28f;
        else mountain_shape = fmaxf(mountain_shape * 0.45f, island_gain);
    }
    surface = base + mountain_shape;
    shore_blend = continent.land_mask
        ? sdk_worldgen_clampf(1.0f - continent.coast_distance / 2.5f, 0.0f, 1.0f)
        : sdk_worldgen_clampf(1.0f - fabsf(continent.coast_distance) / 2.0f, 0.0f, 1.0f);
    if (shore_blend > 0.0f) {
        float shore_target = sea + continent.coast_distance * 10.0f + relief_noise * 2.0f;
        surface = surface * (1.0f - shore_blend * 0.35f) + shore_target * (shore_blend * 0.35f);
        base = base * (1.0f - shore_blend * 0.22f) + (shore_target - 6.0f) * (shore_blend * 0.22f);
    }
    if (!continent.land_mask) {
        float max_seafloor = sea - 2.0f;
        if (surface > max_seafloor && lake_strength < 0.28f) surface = max_seafloor;
        if (base > surface - 2.0f) base = surface - 2.0f;
    }
    if (lake_strength > 0.34f &&
        closed_basin_strength > 0.12f &&
        continent.lake_level > sea + 1.0f &&
        surface >= continent.lake_level &&
        continent.coast_distance > 1.0f) {
        surface = continent.lake_level - 1.0f;
    }
    warm_bias = 1.0f - lat;
    relief_strength = sdk_worldgen_clampf(mountain_mask * 0.65f +
                                          ramp01(fabsf(mountain_shape), 6.0f, 64.0f) * 0.35f,
                                          0.0f, 1.0f);

    cell->plate_class = plate_class;
    cell->boundary_class = boundary_class;
    cell->bedrock_province = classify_bedrock(plate_class, boundary_class, boundary_strength,
                                              hotspot, continentality, uplift, warm_bias);
    cell->base_height = (int16_t)sdk_worldgen_clampi((int)lrintf(base + mountain_shape * 0.35f), 8, CHUNK_HEIGHT - 32);
    cell->surface_height = (int16_t)sdk_worldgen_clampi((int)lrintf(surface), 8, CHUNK_HEIGHT - 32);
    if (cell->base_height >= cell->surface_height) {
        cell->base_height = (int16_t)sdk_worldgen_clampi((int)cell->surface_height - 1, 7, CHUNK_HEIGHT - 33);
    }
    cell->water_height = (cell->surface_height <= impl->sea_level) ? impl->sea_level : 0;
    cell->river_bed_height = cell->surface_height;
    cell->relief_strength = sdk_worldgen_pack_unorm8(relief_strength);
    cell->mountain_mask = sdk_worldgen_pack_unorm8(mountain_mask);
    cell->detail_amp = 0;
    cell->river_strength = 0;
    cell->wetness = 0;
    cell->province_family = SDK_WORLDGEN_PROVINCE_FAMILY_BASIN_UPLAND;
}

void sdk_worldgen_build_macro_tile(SdkWorldGen* wg, SdkWorldGenMacroTile* tile)
{
    int x;
    int z;
    if (!wg || !wg->impl || !tile) return;

    for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
            assign_macro_cell(wg, tile, x, z);
        }
    }
}
