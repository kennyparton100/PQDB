/**
 * sdk_worldgen_column.c -- Column synthesis from macro tiles.
 */
#include "sdk_worldgen_column_internal.h"
#include "../ConstructionCells/sdk_worldgen_construction_cells.h"
#include "../../Superchunks/Geometry/sdk_superchunk_geometry.h"
#include "../../Superchunks/Config/sdk_superchunk_config.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <windows.h>

#define SDK_VERBOSE_WALL_DEBUG_LOGS 0


static int floor_div_local(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return (value - denom + 1) / denom;
}

/* Wall mask flags - must be defined before helper functions */
enum {
    SDK_SUPER_WALL_WEST = SDK_SUPERCHUNK_WALL_FACE_WEST,
    SDK_SUPER_WALL_NORTH = SDK_SUPERCHUNK_WALL_FACE_NORTH,
    SDK_SUPER_WALL_EAST = SDK_SUPERCHUNK_WALL_FACE_EAST,
    SDK_SUPER_WALL_SOUTH = SDK_SUPERCHUNK_WALL_FACE_SOUTH
};

int sdk_worldgen_get_canonical_wall_chunk_owner(int cx,
                                                int cz,
                                                uint8_t* out_wall_mask,
                                                int* out_origin_cx,
                                                int* out_origin_cz,
                                                int* out_period_local_x,
                                                int* out_period_local_z)
{
    return sdk_superchunk_get_canonical_wall_chunk_owner(cx,
                                                         cz,
                                                         out_wall_mask,
                                                         out_origin_cx,
                                                         out_origin_cz,
                                                         out_period_local_x,
                                                         out_period_local_z);
}

static int chunk_is_wall_chunk(int cx, int cz, uint8_t* out_wall_mask)
{
    return sdk_worldgen_get_canonical_wall_chunk_owner(cx,
                                                       cz,
                                                       out_wall_mask,
                                                       NULL,
                                                       NULL,
                                                       NULL,
                                                       NULL);
}

/* Check if chunk intersects the configured gate run along a wall edge. */
static int chunk_is_arch_gate(int cx, int cz)
{
    uint8_t wall_mask = 0u;
    int period_local_x = 0;
    int period_local_z = 0;

    if (!sdk_worldgen_get_canonical_wall_chunk_owner(cx,
                                                     cz,
                                                     &wall_mask,
                                                     NULL,
                                                     NULL,
                                                     &period_local_x,
                                                     &period_local_z)) {
        return 0;
    }

    if ((wall_mask & SDK_SUPER_WALL_WEST) != 0u &&
        sdk_superchunk_gate_intersects_chunk_run(period_local_z)) {
        return 1;
    }
    if ((wall_mask & SDK_SUPER_WALL_EAST) != 0u &&
        sdk_superchunk_gate_intersects_chunk_run(period_local_z)) {
        return 1;
    }
    if ((wall_mask & SDK_SUPER_WALL_NORTH) != 0u &&
        sdk_superchunk_gate_intersects_chunk_run(period_local_x)) {
        return 1;
    }
    if ((wall_mask & SDK_SUPER_WALL_SOUTH) != 0u &&
        sdk_superchunk_gate_intersects_chunk_run(period_local_x)) {
        return 1;
    }

    return 0;
}

/* Calculate period-based superchunk origin for a wall chunk */
static void superchunk_origin_for_chunk(int cx, int cz, int* out_origin_x, int* out_origin_z)
{
    SdkSuperchunkCell cell;
    sdk_superchunk_cell_from_chunk(cx, cz, &cell);
    sdk_superchunk_cell_origin_blocks(&cell, out_origin_x, out_origin_z);
}

static int tile_index(int x, int z)
{
    return z * SDK_WORLDGEN_TILE_STRIDE + x;
}

static const SdkWorldGenMacroCell* macro_cell_at(const SdkWorldGenMacroTile* tile, int x, int z)
{
    if (x < 0) x = 0;
    if (z < 0) z = 0;
    if (x >= SDK_WORLDGEN_TILE_STRIDE) x = SDK_WORLDGEN_TILE_STRIDE - 1;
    if (z >= SDK_WORLDGEN_TILE_STRIDE) z = SDK_WORLDGEN_TILE_STRIDE - 1;
    return &tile->cells[tile_index(x, z)];
}

static float bilerp4(float a, float b, float c, float d, float tx, float tz)
{
    float ab = a * (1.0f - tx) + b * tx;
    float cd = c * (1.0f - tx) + d * tx;
    return ab * (1.0f - tz) + cd * tz;
}

typedef struct {
    const SdkWorldGenMacroCell* c00;
    const SdkWorldGenMacroCell* c10;
    const SdkWorldGenMacroCell* c01;
    const SdkWorldGenMacroCell* c11;
    float tx;
    float tz;
    float w00;
    float w10;
    float w01;
    float w11;
    int block_x_in_macro;
    int block_z_in_macro;
} MacroBlend;

static int region_index(int x, int z)
{
    return z * SDK_WORLDGEN_REGION_STRIDE + x;
}

static const SdkRegionFieldSample* region_sample_at(const SdkWorldGenRegionTile* tile, int x, int z)
{
    if (x < 0) x = 0;
    if (z < 0) z = 0;
    if (x >= SDK_WORLDGEN_REGION_STRIDE) x = SDK_WORLDGEN_REGION_STRIDE - 1;
    if (z >= SDK_WORLDGEN_REGION_STRIDE) z = SDK_WORLDGEN_REGION_STRIDE - 1;
    return &tile->samples[region_index(x, z)];
}

static float smoothstep_local(float t)
{
    t = sdk_worldgen_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float remap_smooth(float v, float lo, float hi)
{
    if (hi <= lo) return (v >= hi) ? 1.0f : 0.0f;
    return smoothstep_local((v - lo) / (hi - lo));
}

static float province_weight4_region(uint8_t province, const SdkRegionFieldSample* s00,
                                     const SdkRegionFieldSample* s10, const SdkRegionFieldSample* s01,
                                     const SdkRegionFieldSample* s11,
                                     float w00, float w10, float w01, float w11)
{
    float total = 0.0f;
    if (s00->terrain_province == province) total += w00;
    if (s10->terrain_province == province) total += w10;
    if (s01->terrain_province == province) total += w01;
    if (s11->terrain_province == province) total += w11;
    return total;
}

static float strata_structural_phase(const SdkRegionFieldSample* geology,
                                     int wx, int wy, int wz,
                                     float xy_scale, float vertical_scale,
                                     uint32_t seed)
{
    float phase = (float)wy * vertical_scale;
    float dip_term = 0.0f;
    float fold_term = 0.0f;
    float noise_term = 0.0f;
    float fold_scale = 0.35f;
    float noise_scale = 1.0f;

    phase += (float)wx * xy_scale;
    phase += (float)wz * xy_scale * 0.82f;

    if (!geology) return phase;

    switch ((SdkStratigraphyProvince)geology->stratigraphy_province) {
        case SDK_STRAT_PROVINCE_HARDROCK_BASEMENT:
            fold_scale = 0.90f;
            noise_scale = 1.20f;
            break;
        case SDK_STRAT_PROVINCE_CARBONATE_SHELF:
            fold_scale = 0.55f;
            noise_scale = 0.95f;
            break;
        case SDK_STRAT_PROVINCE_RIFT_BASIN:
            fold_scale = 0.45f;
            noise_scale = 1.15f;
            break;
        case SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN:
            fold_scale = 0.40f;
            noise_scale = 1.10f;
            break;
        case SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX:
            fold_scale = 0.30f;
            noise_scale = 1.00f;
            break;
        case SDK_STRAT_PROVINCE_FLOOD_BASALT:
            fold_scale = 0.18f;
            noise_scale = 0.90f;
            break;
        case SDK_STRAT_PROVINCE_OCEANIC:
        default:
            fold_scale = 0.15f;
            noise_scale = 0.85f;
            break;
    }

    dip_term = ((float)wx * geology->dip_x + (float)wz * geology->dip_z) * 22.0f;
    fold_term = sinf((float)wx * xy_scale * 0.57f + geology->basin_axis_weight * 6.2831853f) * 1.9f +
                cosf((float)wz * xy_scale * 0.61f - geology->carbonate_purity * 6.2831853f) * 1.6f;
    noise_term = sdk_worldgen_fbm((float)wx * (xy_scale * 0.62f) + geology->fault_mask * 0.9f,
                                  (float)wz * (xy_scale * 0.66f) + geology->channel_sand_bias * 0.9f,
                                  seed, 2) * 3.2f;

    dip_term *= 0.85f + fold_scale * 0.15f;
    fold_term *= fold_scale;
    noise_term *= noise_scale;

    return phase + dip_term + fold_term + noise_term + geology->fault_throw * 0.28f;
}

static int strata_band_index(float phase, float spacing, int count)
{
    int idx;

    if (count <= 0) return 0;
    if (spacing < 0.5f) spacing = 0.5f;
    idx = (int)floorf((phase + 8192.0f) / spacing);
    if (idx < 0) idx = -idx;
    return idx % count;
}

static float temperature_band_midpoint(uint8_t band)
{
    switch ((SdkTemperatureBand)band) {
        case TEMP_POLAR: return 0.06f;
        case TEMP_SUBPOLAR: return 0.19f;
        case TEMP_COOL_TEMPERATE: return 0.36f;
        case TEMP_WARM_TEMPERATE: return 0.54f;
        case TEMP_SUBTROPICAL: return 0.73f;
        case TEMP_TROPICAL: return 0.90f;
        default: return 0.45f;
    }
}

static float drainage_index_for_class(uint8_t drainage_class)
{
    switch ((SdkDrainageClass)drainage_class) {
        case DRAINAGE_EXCESSIVE:   return 0.88f;
        case DRAINAGE_WELL:        return 0.70f;
        case DRAINAGE_MODERATE:    return 0.52f;
        case DRAINAGE_IMPERFECT:   return 0.36f;
        case DRAINAGE_POOR:        return 0.22f;
        case DRAINAGE_WATERLOGGED:
        default:                   return 0.08f;
    }
}

static float soil_reaction_value_for_class(uint8_t soil_reaction)
{
    switch ((SdkSoilReactionClass)soil_reaction) {
        case SOIL_REACTION_STRONGLY_ACID:   return 0.08f;
        case SOIL_REACTION_ACID:            return 0.22f;
        case SOIL_REACTION_SLIGHTLY_ACID:   return 0.39f;
        case SOIL_REACTION_NEUTRAL:         return 0.56f;
        case SOIL_REACTION_CALCAREOUS:      return 0.73f;
        case SOIL_REACTION_SALINE_ALKALINE:
        default:                            return 0.90f;
    }
}

static float soil_fertility_value_for_class(uint8_t soil_fertility)
{
    switch ((SdkSoilFertilityClass)soil_fertility) {
        case SOIL_FERTILITY_VERY_LOW:   return 0.08f;
        case SOIL_FERTILITY_LOW:        return 0.26f;
        case SOIL_FERTILITY_MODERATE:   return 0.44f;
        case SOIL_FERTILITY_HIGH:       return 0.64f;
        case SOIL_FERTILITY_VERY_HIGH:
        default:                        return 0.86f;
    }
}

static float soil_salinity_value_for_class(uint8_t soil_salinity)
{
    switch ((SdkSoilSalinityClass)soil_salinity) {
        case SOIL_SALINITY_NONE:      return 0.08f;
        case SOIL_SALINITY_SLIGHT:    return 0.28f;
        case SOIL_SALINITY_MODERATE:  return 0.50f;
        case SOIL_SALINITY_HIGH:
        default:                      return 0.78f;
    }
}

static float soil_organic_value_for_state(uint8_t surface_sediment,
                                          uint8_t parent_material,
                                          uint8_t terrain_province,
                                          float wetness)
{
    float organic = 0.08f + wetness * 0.14f;

    if (surface_sediment == SEDIMENT_PEAT || parent_material == PARENT_MATERIAL_ORGANIC) {
        organic = 0.94f;
    } else if (terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND) {
        organic = fmaxf(organic, 0.78f);
    } else if (surface_sediment == SEDIMENT_LACUSTRINE_CLAY ||
               surface_sediment == SEDIMENT_FINE_ALLUVIUM ||
               surface_sediment == SEDIMENT_DELTAIC_SILT) {
        organic = fmaxf(organic, 0.26f + wetness * 0.18f);
    } else if (surface_sediment == SEDIMENT_MARINE_MUD) {
        organic = fmaxf(organic, 0.18f + wetness * 0.10f);
    }

    return sdk_worldgen_clampf(organic, 0.0f, 1.0f);
}

static uint8_t classify_drainage_local(float drainage_index, float water_table_depth)
{
    if (water_table_depth <= 0.5f && drainage_index < 0.36f) return DRAINAGE_WATERLOGGED;
    if (drainage_index >= 0.80f) return DRAINAGE_EXCESSIVE;
    if (drainage_index >= 0.62f) return DRAINAGE_WELL;
    if (drainage_index >= 0.44f) return DRAINAGE_MODERATE;
    if (drainage_index >= 0.28f) return DRAINAGE_IMPERFECT;
    if (drainage_index >= 0.16f) return DRAINAGE_POOR;
    return DRAINAGE_WATERLOGGED;
}

static uint8_t classify_soil_reaction_local(float reaction_value, float salinity_value)
{
    if (salinity_value >= 0.72f) return SOIL_REACTION_SALINE_ALKALINE;
    if (reaction_value < 0.15f) return SOIL_REACTION_STRONGLY_ACID;
    if (reaction_value < 0.30f) return SOIL_REACTION_ACID;
    if (reaction_value < 0.48f) return SOIL_REACTION_SLIGHTLY_ACID;
    if (reaction_value < 0.64f) return SOIL_REACTION_NEUTRAL;
    if (reaction_value < 0.82f) return SOIL_REACTION_CALCAREOUS;
    return SOIL_REACTION_SALINE_ALKALINE;
}

static uint8_t classify_soil_fertility_local(float fertility_value)
{
    if (fertility_value < 0.18f) return SOIL_FERTILITY_VERY_LOW;
    if (fertility_value < 0.34f) return SOIL_FERTILITY_LOW;
    if (fertility_value < 0.54f) return SOIL_FERTILITY_MODERATE;
    if (fertility_value < 0.74f) return SOIL_FERTILITY_HIGH;
    return SOIL_FERTILITY_VERY_HIGH;
}

static uint8_t classify_soil_salinity_local(float salinity_value)
{
    if (salinity_value < 0.18f) return SOIL_SALINITY_NONE;
    if (salinity_value < 0.38f) return SOIL_SALINITY_SLIGHT;
    if (salinity_value < 0.62f) return SOIL_SALINITY_MODERATE;
    return SOIL_SALINITY_HIGH;
}

static uint8_t classify_temperature(float temp)
{
    if (temp < 0.12f) return TEMP_POLAR;
    if (temp < 0.26f) return TEMP_SUBPOLAR;
    if (temp < 0.45f) return TEMP_COOL_TEMPERATE;
    if (temp < 0.63f) return TEMP_WARM_TEMPERATE;
    if (temp < 0.82f) return TEMP_SUBTROPICAL;
    return TEMP_TROPICAL;
}

static uint8_t classify_moisture(float moisture)
{
    if (moisture < 0.12f) return MOISTURE_ARID;
    if (moisture < 0.28f) return MOISTURE_SEMI_ARID;
    if (moisture < 0.48f) return MOISTURE_SUBHUMID;
    if (moisture < 0.66f) return MOISTURE_HUMID;
    if (moisture < 0.82f) return MOISTURE_PERHUMID;
    return MOISTURE_WATERLOGGED;
}

static float pseudo_noise3_local(int wx, int wy, int wz, uint32_t seed, float scale, int octaves)
{
    float y = (float)wy;
    float a = sdk_worldgen_fbm((float)wx * scale + y * scale * 0.37f,
                               (float)wz * scale - y * scale * 0.29f,
                               seed, octaves);
    float b = sdk_worldgen_fbm((float)wx * scale * 0.71f - y * scale * 0.21f,
                               (float)wz * scale * 0.71f + y * scale * 0.33f,
                               seed ^ 0x9E3779B9u,
                               (octaves > 1) ? (octaves - 1) : 1);
    return a * 0.65f + b * 0.35f;
}

static uint32_t landform_flags_for_state(float surface_height,
                                         float water_level,
                                         float river_strength,
                                         float river_channel_width,
                                         float floodplain_mask,
                                         float lake_mask,
                                         float closed_basin_mask,
                                         float ravine_mask,
                                         float vent_mask,
                                         float caldera_mask,
                                         float lava_flow_bias,
                                         float cave_entrance_mask)
{
    uint32_t flags = SDK_LANDFORM_NONE;

    if (river_strength > 0.08f && river_channel_width > 1.2f) flags |= SDK_LANDFORM_RIVER_CHANNEL;
    if (floodplain_mask > 0.18f) flags |= SDK_LANDFORM_FLOODPLAIN;
    if (ravine_mask > 0.10f) flags |= SDK_LANDFORM_RAVINE;
    if (lake_mask > 0.22f && closed_basin_mask > 0.16f && water_level >= surface_height) flags |= SDK_LANDFORM_LAKE_BASIN;
    if (cave_entrance_mask > 0.32f) flags |= SDK_LANDFORM_CAVE_ENTRANCE;
    if (vent_mask > 0.24f) flags |= SDK_LANDFORM_VOLCANIC_VENT;
    if (caldera_mask > 0.18f) flags |= SDK_LANDFORM_CALDERA;
    if (lava_flow_bias > 0.18f) flags |= SDK_LANDFORM_LAVA_FIELD;
    return flags;
}

static int ecology_is_wetland(uint8_t ecology)
{
    switch ((SdkBiomeEcology)ecology) {
        case ECOLOGY_ESTUARY_WETLAND:
        case ECOLOGY_FEN:
        case ECOLOGY_BOG:
            return 1;
        default:
            return 0;
    }
}

static int ecology_prefers_turf(uint8_t ecology)
{
    switch ((SdkBiomeEcology)ecology) {
        case ECOLOGY_RIPARIAN_FOREST:
        case ECOLOGY_FLOODPLAIN_MEADOW:
        case ECOLOGY_PRAIRIE:
        case ECOLOGY_TEMPERATE_DECIDUOUS_FOREST:
        case ECOLOGY_TEMPERATE_CONIFER_FOREST:
        case ECOLOGY_BOREAL_TAIGA:
        case ECOLOGY_TROPICAL_SEASONAL_FOREST:
        case ECOLOGY_TROPICAL_RAINFOREST:
        case ECOLOGY_TUNDRA:
        case ECOLOGY_ALPINE_MEADOW:
            return 1;
        default:
            return 0;
    }
}

static uint8_t coarse_ecology_for_state(int surface_height,
                                        int sea_level,
                                        uint8_t terrain_province,
                                        uint8_t temperature_band,
                                        uint8_t moisture_band,
                                        uint8_t surface_sediment,
                                        uint8_t parent_material,
                                        uint8_t drainage_class,
                                        uint8_t soil_reaction,
                                        uint8_t soil_fertility,
                                        uint8_t soil_salinity,
                                        uint8_t river_order,
                                        float coast_signed_distance,
                                        float river_strength,
                                        float wetness,
                                        float ash_bias,
                                        uint32_t landform_flags,
                                        uint8_t water_table_depth)
{
    int high_relief_cold = (terrain_province == TERRAIN_PROVINCE_ALPINE_BELT) ||
                           (surface_height > sea_level + 190 && temperature_band <= TEMP_SUBPOLAR);

    if (surface_height <= sea_level) return ECOLOGY_BARREN;

    if (high_relief_cold) {
        if (temperature_band <= TEMP_POLAR || surface_height > sea_level + 240) return ECOLOGY_NIVAL_ICE;
        if (temperature_band <= TEMP_SUBPOLAR) return ECOLOGY_TUNDRA;
        return ECOLOGY_ALPINE_MEADOW;
    }

    if ((landform_flags & SDK_LANDFORM_LAKE_BASIN) != 0u ||
        drainage_class == DRAINAGE_WATERLOGGED ||
        water_table_depth <= 1u) {
        if (terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA ||
            (coast_signed_distance >= 0.0f && coast_signed_distance < 32.0f)) {
            return ECOLOGY_ESTUARY_WETLAND;
        }
        if (surface_sediment == SEDIMENT_PEAT ||
            parent_material == PARENT_MATERIAL_ORGANIC ||
            soil_reaction <= SOIL_REACTION_ACID) {
            return ECOLOGY_BOG;
        }
        return ECOLOGY_FEN;
    }

    if ((landform_flags & SDK_LANDFORM_RIVER_CHANNEL) != 0u &&
        parent_material == PARENT_MATERIAL_ALLUVIAL &&
        water_table_depth <= 3u &&
        river_order >= 2u) {
        if (moisture_band >= MOISTURE_HUMID || wetness >= 0.54f) return ECOLOGY_RIPARIAN_FOREST;
        return ECOLOGY_FLOODPLAIN_MEADOW;
    }

    if (terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST &&
        (surface_sediment == SEDIMENT_BEACH_SAND || surface_sediment == SEDIMENT_MARINE_SAND) &&
        moisture_band <= MOISTURE_SUBHUMID) {
        return ECOLOGY_DUNE_COAST;
    }

    if ((terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ||
         terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU) &&
        (ash_bias > 0.18f || (landform_flags & (SDK_LANDFORM_VOLCANIC_VENT | SDK_LANDFORM_CALDERA | SDK_LANDFORM_LAVA_FIELD)) != 0u)) {
        return ECOLOGY_VOLCANIC_BARRENS;
    }

    if (soil_salinity >= SOIL_SALINITY_MODERATE) {
        if (coast_signed_distance >= 0.0f && coast_signed_distance < 48.0f &&
            (surface_sediment == SEDIMENT_BEACH_SAND || surface_sediment == SEDIMENT_EOILIAN_SAND)) {
            return ECOLOGY_DUNE_COAST;
        }
        return ECOLOGY_STEPPE;
    }

    if (moisture_band <= MOISTURE_ARID ||
        (moisture_band <= MOISTURE_SEMI_ARID &&
         parent_material == PARENT_MATERIAL_AEOLIAN &&
         drainage_class <= DRAINAGE_WELL &&
         soil_fertility <= SOIL_FERTILITY_LOW)) {
        return ECOLOGY_HOT_DESERT;
    }

    if (temperature_band <= TEMP_SUBPOLAR) {
        if (moisture_band >= MOISTURE_SUBHUMID && drainage_class >= DRAINAGE_MODERATE) return ECOLOGY_BOREAL_TAIGA;
        return ECOLOGY_TUNDRA;
    }

    if (moisture_band <= MOISTURE_SEMI_ARID) {
        if (temperature_band >= TEMP_SUBTROPICAL &&
            soil_fertility >= SOIL_FERTILITY_MODERATE &&
            drainage_class <= DRAINAGE_WELL &&
            wetness >= 0.24f) {
            return ECOLOGY_MEDITERRANEAN_SCRUB;
        }
        return ECOLOGY_STEPPE;
    }

    if (temperature_band == TEMP_COOL_TEMPERATE) {
        if (moisture_band >= MOISTURE_HUMID && soil_fertility >= SOIL_FERTILITY_MODERATE) {
            return ECOLOGY_TEMPERATE_DECIDUOUS_FOREST;
        }
        return ECOLOGY_TEMPERATE_CONIFER_FOREST;
    }

    if (temperature_band == TEMP_WARM_TEMPERATE) {
        if (moisture_band >= MOISTURE_HUMID) return ECOLOGY_TEMPERATE_DECIDUOUS_FOREST;
        if (soil_fertility >= SOIL_FERTILITY_MODERATE && drainage_class >= DRAINAGE_MODERATE) return ECOLOGY_PRAIRIE;
        return ECOLOGY_MEDITERRANEAN_SCRUB;
    }

    if (temperature_band == TEMP_SUBTROPICAL) {
        if (moisture_band >= MOISTURE_PERHUMID && drainage_class >= DRAINAGE_MODERATE) return ECOLOGY_TROPICAL_RAINFOREST;
        if (moisture_band >= MOISTURE_HUMID) return ECOLOGY_TROPICAL_SEASONAL_FOREST;
        if (soil_fertility >= SOIL_FERTILITY_MODERATE) return ECOLOGY_PRAIRIE;
        return ECOLOGY_MEDITERRANEAN_SCRUB;
    }

    if (temperature_band >= TEMP_TROPICAL) {
        if (moisture_band >= MOISTURE_HUMID && drainage_class >= DRAINAGE_MODERATE) return ECOLOGY_TROPICAL_RAINFOREST;
        if (moisture_band >= MOISTURE_SUBHUMID) return ECOLOGY_TROPICAL_SEASONAL_FOREST;
        if (soil_fertility >= SOIL_FERTILITY_MODERATE) return ECOLOGY_PRAIRIE;
        return ECOLOGY_STEPPE;
    }

    if (soil_fertility >= SOIL_FERTILITY_MODERATE && drainage_class >= DRAINAGE_MODERATE) return ECOLOGY_PRAIRIE;
    return ECOLOGY_STEPPE;
}

static uint8_t refine_surface_sediment_for_state(uint8_t terrain_province,
                                                 uint8_t parent_material,
                                                 uint8_t drainage_class,
                                                 uint8_t soil_reaction,
                                                 uint8_t soil_fertility,
                                                 uint8_t surface_sediment,
                                                 uint8_t moisture_band,
                                                 uint8_t temperature_band,
                                                 float surface_height,
                                                 float water_level,
                                                 float coast_signed_distance,
                                                 float river_strength,
                                                 float floodplain_mask,
                                                 float lake_mask,
                                                 float closed_basin_mask,
                                                 float marine_weight,
                                                 int sea_level)
{
    if (surface_height <= (float)sea_level ||
        (water_level > surface_height && marine_weight > 0.25f && coast_signed_distance < 0.0f)) {
        if (terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
            terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST ||
            coast_signed_distance > -48.0f) {
            return SEDIMENT_MARINE_SAND;
        }
        return SEDIMENT_MARINE_MUD;
    }

    if (terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND ||
        parent_material == PARENT_MATERIAL_ORGANIC) {
        return SEDIMENT_PEAT;
    }
    if (lake_mask > 0.24f && closed_basin_mask > 0.20f && water_level >= surface_height) {
        return SEDIMENT_LACUSTRINE_CLAY;
    }
    if (terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA) return SEDIMENT_DELTAIC_SILT;
    if (terrain_province == TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND &&
        (river_strength > 0.12f || floodplain_mask > 0.18f)) {
        return (drainage_class <= DRAINAGE_WELL) ? SEDIMENT_COARSE_ALLUVIUM : SEDIMENT_FINE_ALLUVIUM;
    }
    if (terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) return SEDIMENT_BEACH_SAND;
    if (terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE) {
        if (parent_material == PARENT_MATERIAL_AEOLIAN || drainage_class <= DRAINAGE_WELL) {
            return SEDIMENT_EOILIAN_SAND;
        }
        return SEDIMENT_COARSE_ALLUVIUM;
    }
    if (surface_sediment == SEDIMENT_VOLCANIC_ASH) return surface_sediment;
    if (parent_material == PARENT_MATERIAL_CARBONATE &&
        soil_reaction >= SOIL_REACTION_CALCAREOUS &&
        terrain_province == TERRAIN_PROVINCE_CARBONATE_UPLAND) {
        return SEDIMENT_CALCAREOUS_RESIDUAL;
    }
    if ((parent_material == PARENT_MATERIAL_GRANITIC ||
         parent_material == PARENT_MATERIAL_METAMORPHIC ||
         parent_material == PARENT_MATERIAL_MAFIC_VOLCANIC ||
         parent_material == PARENT_MATERIAL_INTERMEDIATE_VOLCANIC) &&
        moisture_band >= MOISTURE_HUMID &&
        temperature_band >= TEMP_WARM_TEMPERATE &&
        drainage_class <= DRAINAGE_MODERATE) {
        return SEDIMENT_SAPROLITE;
    }
    if (parent_material == PARENT_MATERIAL_AEOLIAN) {
        if (soil_fertility >= SOIL_FERTILITY_HIGH || moisture_band >= MOISTURE_SUBHUMID) {
            return SEDIMENT_LOESS;
        }
        return SEDIMENT_EOILIAN_SAND;
    }

    return surface_sediment;
}

static void macro_blend_init(SdkWorldGenMacroTile* tile, SdkWorldGen* wg, int wx, int wz, MacroBlend* out)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    int macro_x;
    int macro_z;
    int origin_macro_x;
    int origin_macro_z;
    int local_macro_x;
    int local_macro_z;
    int ix;
    int iz;

    if (!impl || !tile || !out) return;

    macro_x = floor_div_local(wx, (int)impl->macro_cell_size);
    macro_z = floor_div_local(wz, (int)impl->macro_cell_size);
    out->block_x_in_macro = wx - macro_x * (int)impl->macro_cell_size;
    out->block_z_in_macro = wz - macro_z * (int)impl->macro_cell_size;
    if (out->block_x_in_macro < 0) out->block_x_in_macro += impl->macro_cell_size;
    if (out->block_z_in_macro < 0) out->block_z_in_macro += impl->macro_cell_size;

    origin_macro_x = tile->tile_x * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;
    origin_macro_z = tile->tile_z * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;
    local_macro_x = macro_x - origin_macro_x;
    local_macro_z = macro_z - origin_macro_z;

    out->tx = (float)out->block_x_in_macro / (float)impl->macro_cell_size;
    out->tz = (float)out->block_z_in_macro / (float)impl->macro_cell_size;
    out->w00 = (1.0f - out->tx) * (1.0f - out->tz);
    out->w10 = out->tx * (1.0f - out->tz);
    out->w01 = (1.0f - out->tx) * out->tz;
    out->w11 = out->tx * out->tz;

    ix = sdk_worldgen_clampi(local_macro_x, 0, SDK_WORLDGEN_TILE_STRIDE - 2);
    iz = sdk_worldgen_clampi(local_macro_z, 0, SDK_WORLDGEN_TILE_STRIDE - 2);

    out->c00 = macro_cell_at(tile, ix,     iz);
    out->c10 = macro_cell_at(tile, ix + 1, iz);
    out->c01 = macro_cell_at(tile, ix,     iz + 1);
    out->c11 = macro_cell_at(tile, ix + 1, iz + 1);
}

static uint8_t weighted_vote4_u8(uint8_t v00, uint8_t v10, uint8_t v01, uint8_t v11,
                                 float w00, float w10, float w01, float w11, float* out_weight)
{
    uint8_t vals[4] = { 0 };
    float weights[4] = { 0.0f };
    uint8_t best = v00;
    float best_weight = -1.0f;
    int i;
    int j;

    vals[0] = v00; vals[1] = v10; vals[2] = v01; vals[3] = v11;
    weights[0] = w00; weights[1] = w10; weights[2] = w01; weights[3] = w11;

    for (i = 0; i < 4; ++i) {
        float total = 0.0f;
        for (j = 0; j < 4; ++j) {
            if (vals[j] == vals[i]) total += weights[j];
        }
        if (total > best_weight || (fabsf(total - best_weight) < 0.0001f && vals[i] < best)) {
            best_weight = total;
            best = vals[i];
        }
    }

    if (out_weight) *out_weight = (best_weight < 0.0f) ? 0.0f : best_weight;
    return best;
}

static float province_weight4(uint8_t province, const SdkWorldGenMacroCell* c00,
                              const SdkWorldGenMacroCell* c10, const SdkWorldGenMacroCell* c01,
                              const SdkWorldGenMacroCell* c11,
                              float w00, float w10, float w01, float w11)
{
    float total = 0.0f;
    if (c00->terrain_province == province) total += w00;
    if (c10->terrain_province == province) total += w10;
    if (c01->terrain_province == province) total += w01;
    if (c11->terrain_province == province) total += w11;
    return total;
}

static float family_weight4(uint8_t family, const SdkWorldGenMacroCell* c00,
                            const SdkWorldGenMacroCell* c10, const SdkWorldGenMacroCell* c01,
                            const SdkWorldGenMacroCell* c11,
                            float w00, float w10, float w01, float w11)
{
    float total = 0.0f;
    if (c00->province_family == family) total += w00;
    if (c10->province_family == family) total += w10;
    if (c01->province_family == family) total += w01;
    if (c11->province_family == family) total += w11;
    return total;
}

static void direction_vector(uint8_t dir, float* out_dx, float* out_dz)
{
    static const float dirx[8] = { 0.0f, 0.7071f, 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f };
    static const float dirz[8] = { -1.0f, -0.7071f, 0.0f, 0.7071f, 1.0f, 0.7071f, 0.0f, -0.7071f };
    if (dir < 8u) {
        *out_dx = dirx[dir];
        *out_dz = dirz[dir];
    } else {
        *out_dx = 1.0f;
        *out_dz = 0.0f;
    }
}

static void blended_direction4(const SdkWorldGenMacroCell* c00, const SdkWorldGenMacroCell* c10,
                               const SdkWorldGenMacroCell* c01, const SdkWorldGenMacroCell* c11,
                               float w00, float w10, float w01, float w11,
                               float* out_dx, float* out_dz)
{
    const SdkWorldGenMacroCell* cells[4] = { c00, c10, c01, c11 };
    float weights[4] = { w00, w10, w01, w11 };
    float sum_x = 0.0f;
    float sum_z = 0.0f;
    float total = 0.0f;
    int i;

    for (i = 0; i < 4; ++i) {
        float dx;
        float dz;
        float river_weight;

        if (cells[i]->downstream_dir >= 8u) continue;
        direction_vector(cells[i]->downstream_dir, &dx, &dz);
        river_weight = weights[i] * (0.15f + sdk_worldgen_unpack_unorm8(cells[i]->river_strength) * 0.85f);
        sum_x += dx * river_weight;
        sum_z += dz * river_weight;
        total += river_weight;
    }

    if (total <= 0.0001f) {
        *out_dx = 1.0f;
        *out_dz = 0.0f;
        return;
    }

    sum_x /= total;
    sum_z /= total;
    total = sqrtf(sum_x * sum_x + sum_z * sum_z);
    if (total <= 0.0001f) {
        *out_dx = 1.0f;
        *out_dz = 0.0f;
        return;
    }

    *out_dx = sum_x / total;
    *out_dz = sum_z / total;
}

static float channel_distance_blocks_vec(int local_x, int local_z, int macro_cell_size, float dir_x, float dir_z)
{
    float px;
    float pz;
    float nx;
    float nz;

    nx = -dir_z;
    nz = dir_x;
    px = ((float)local_x + 0.5f) - (float)macro_cell_size * 0.5f;
    pz = ((float)local_z + 0.5f) - (float)macro_cell_size * 0.5f;
    return fabsf(px * nx + pz * nz);
}


static int is_worldgen_water_block(BlockType block)
{
    return block == BLOCK_WATER || block == BLOCK_ICE || block == BLOCK_SEA_ICE;
}

static int is_worldgen_water_gap(BlockType block)
{
    return block == BLOCK_AIR || block == BLOCK_SNOW;
}

static bool fill_generated_water_column_to_line(SdkChunk* chunk,
                                                int lx,
                                                int lz,
                                                int waterline,
                                                BlockType cap_block)
{
    bool changed = false;
    int ly;

    if (!chunk || waterline < 0) return false;
    if (waterline >= CHUNK_HEIGHT) waterline = CHUNK_HEIGHT - 1;

    if (cap_block == BLOCK_ICE || cap_block == BLOCK_SEA_ICE) {
        BlockType top = sdk_chunk_get_block(chunk, lx, waterline, lz);
        if (is_worldgen_water_gap(top) || top == BLOCK_WATER) {
            sdk_chunk_set_block(chunk, lx, waterline, lz, cap_block);
            changed = true;
        }
    } else {
        BlockType top = sdk_chunk_get_block(chunk, lx, waterline, lz);
        if (is_worldgen_water_gap(top)) {
            sdk_chunk_set_block(chunk, lx, waterline, lz, BLOCK_WATER);
            changed = true;
        }
    }

    for (ly = waterline - 1; ly >= 0; --ly) {
        BlockType block = sdk_chunk_get_block(chunk, lx, ly, lz);
        if (is_worldgen_water_block(block)) continue;
        if (is_worldgen_water_gap(block)) {
            sdk_chunk_set_block(chunk, lx, ly, lz, BLOCK_WATER);
            changed = true;
            continue;
        }
        break;
    }

    return changed;
}

typedef struct GeneratedWaterHaloSample {
    int surface_y;
    int waterline;
    uint8_t has_water;
    uint8_t inland;
    uint8_t cap_block;
} GeneratedWaterHaloSample;

static int generated_water_is_open_water(const SdkTerrainColumnProfile* profile)
{
    if (!profile) return 0;
    return profile->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN ||
           profile->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
           profile->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST;
}

static int sample_generated_water_expectation(const SdkWorldGen* wg,
                                              const SdkChunk* chunk,
                                              int lx,
                                              int lz,
                                              GeneratedWaterHaloSample* out_sample)
{
    int wx;
    int wz;
    SdkTerrainColumnProfile profile;

    if (!wg || !chunk || !out_sample) return 0;

    memset(out_sample, 0, sizeof(*out_sample));
    out_sample->surface_y = -1;
    out_sample->waterline = -1;
    out_sample->cap_block = (uint8_t)BLOCK_AIR;

    wx = chunk->cx * CHUNK_WIDTH + lx;
    wz = chunk->cz * CHUNK_DEPTH + lz;
    if (!sdk_worldgen_sample_column_ctx((SdkWorldGen*)wg, wx, wz, &profile)) return 0;

    out_sample->surface_y = profile.surface_height;
    if (out_sample->surface_y < 0) out_sample->surface_y = 0;
    if (out_sample->surface_y >= CHUNK_HEIGHT) out_sample->surface_y = CHUNK_HEIGHT - 1;
    out_sample->inland = generated_water_is_open_water(&profile) ? 0u : 1u;

    if (profile.water_height <= profile.surface_height) return 1;

    out_sample->has_water = 1u;
    out_sample->waterline = profile.water_height;
    if (out_sample->waterline < 0) out_sample->waterline = 0;
    if (out_sample->waterline >= CHUNK_HEIGHT) out_sample->waterline = CHUNK_HEIGHT - 1;

    if (profile.water_surface_class == SURFACE_WATER_SEASONAL_ICE ||
        profile.water_surface_class == SURFACE_WATER_PERENNIAL_ICE) {
        out_sample->cap_block = (uint8_t)(out_sample->inland ? BLOCK_ICE : BLOCK_SEA_ICE);
    }

    return 1;
}

static void sample_generated_water_halo(const SdkWorldGen* wg,
                                        const SdkChunk* chunk,
                                        GeneratedWaterHaloSample* halo)
{
    int sample_lx;
    int sample_lz;
    int halo_width;

    if (!wg || !chunk || !halo) return;
    halo_width = CHUNK_WIDTH + 2;

    for (sample_lz = -1; sample_lz <= CHUNK_DEPTH; ++sample_lz) {
        for (sample_lx = -1; sample_lx <= CHUNK_WIDTH; ++sample_lx) {
            sample_generated_water_expectation(wg,
                                               chunk,
                                               sample_lx,
                                               sample_lz,
                                               &halo[(sample_lz + 1) * halo_width + (sample_lx + 1)]);
        }
    }
}

int floor_mod_superchunk(int value, int denom)
{
    int div = floor_div_local(value, denom);
    return value - div * denom;
}

void worldgen_set_block_fast(SdkChunk* chunk, int lx, int ly, int lz, BlockType type)
{
    uint32_t idx;
    BlockType old_type;

    if (!chunk || !chunk->blocks) return;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)ly >= CHUNK_HEIGHT || (unsigned)lz >= CHUNK_DEPTH) return;

    idx = (uint32_t)ly * CHUNK_BLOCKS_PER_LAYER + (uint32_t)lz * CHUNK_WIDTH + (uint32_t)lx;
    old_type = sdk_world_cell_decode_full_block(chunk->blocks[idx]);
    if (old_type == type) return;
    if (sdk_chunk_block_excluded_from_far_mesh(old_type) &&
        chunk->far_mesh_excluded_block_count > 0u) {
        chunk->far_mesh_excluded_block_count--;
    }
    chunk->blocks[idx] = sdk_world_cell_encode_full_block(type);
    if (sdk_chunk_block_excluded_from_far_mesh(type)) {
        chunk->far_mesh_excluded_block_count++;
    }
    if (type != BLOCK_AIR) chunk->empty = false;
}

static int sample_surface_y_clamped_ctx(SdkWorldGen* wg, int wx, int wz)
{
    int y = sdk_worldgen_get_surface_y_ctx(wg, wx, wz);
    if (y < 0) y = 0;
    if (y >= CHUNK_HEIGHT) y = CHUNK_HEIGHT - 1;
    return y;
}

static int sample_gate_approach_level_ctx(SdkWorldGen* wg, int wx, int wz)
{
    SdkTerrainColumnProfile profile;
    int level;

    if (!wg || !sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) {
        return sample_surface_y_clamped_ctx(wg, wx, wz);
    }

    level = (int)profile.surface_height;
    if ((int)profile.water_height > level) {
        level = (int)profile.water_height;
    }
    if (level < 0) level = 0;
    if (level >= CHUNK_HEIGHT) level = CHUNK_HEIGHT - 1;
    return level;
}

/**
 * Helper: compute wall grid cell info for a chunk coordinate pair.
 * Runtime wall placement now always follows the shared superchunk wall period.
 * Detached wall settings remain in config/save metadata for future work, but
 * they do not shift gameplay wall coordinates here.
 */
typedef struct {
    int period;      /* Wall grid period in chunks */
    int origin_cx;   /* Origin chunk X of this grid cell */
    int origin_cz;   /* Origin chunk Z of this grid cell */
    int interior;    /* Interior chunks per dimension (period - 1) */
    int block_span;  /* Interior blocks per dimension */
} WallGridCell;

static void wall_grid_cell_for_chunk(int cx, int cz, WallGridCell* cell)
{
    SdkSuperchunkWallGridCell wall_cell;
    int interior_chunks;

    if (!cell) return;
    sdk_superchunk_wall_cell_from_chunk(cx, cz, &wall_cell);
    interior_chunks = sdk_superchunk_get_wall_grid_interior_span();
    cell->period = wall_cell.period;
    cell->origin_cx = wall_cell.origin_cx;
    cell->origin_cz = wall_cell.origin_cz;
    cell->interior = interior_chunks;
    cell->block_span = interior_chunks * CHUNK_WIDTH;
}

static int superchunk_origin_for_coord_axis(int world_coord, int chunk_offset)
{
    const int block_offset = chunk_offset * CHUNK_WIDTH;
    const int period_blocks = SDK_SUPERCHUNK_WALL_PERIOD * CHUNK_WIDTH;
    return sdk_superchunk_floor_div_i(world_coord - block_offset, period_blocks) * period_blocks + block_offset;
}

static int superchunk_origin_for_coord(int world_coord)
{
    return superchunk_origin_for_coord_axis(world_coord, 0);
}

static int superchunk_wall_face_strip_x(int local_x)
{
    return local_x < 4 || local_x >= CHUNK_WIDTH - 4;
}

static int superchunk_wall_face_strip_z(int local_z)
{
    return local_z < 4 || local_z >= CHUNK_DEPTH - 4;
}

static int superchunk_gate_center_world(int super_origin)
{
    return super_origin + SDK_SUPERCHUNK_GATE_START + (SDK_SUPERCHUNK_GATE_WIDTH / 2);
}

static int superchunk_gate_contains(int run_local)
{
    return sdk_superchunk_gate_contains_block_run(run_local);
}

static int superchunk_gate_exclusion(int run_local)
{
    return run_local >= (SDK_SUPERCHUNK_GATE_START - 24) &&
           run_local <= (SDK_SUPERCHUNK_GATE_END + 24);
}

static int superchunk_buttress_zone(int run_local)
{
    int rem;
    int dist;
    if (superchunk_gate_exclusion(run_local)) return 0;
    rem = run_local % SDK_SUPERCHUNK_BUTTRESS_INTERVAL;
    if (rem < 0) rem += SDK_SUPERCHUNK_BUTTRESS_INTERVAL;
    dist = abs(rem - (SDK_SUPERCHUNK_BUTTRESS_INTERVAL / 2));
    return dist <= 7;
}

static int superchunk_gate_pier_zone(int run_local)
{
    return abs(run_local - SDK_SUPERCHUNK_GATE_START) <= 7 ||
           abs(run_local - SDK_SUPERCHUNK_GATE_END) <= 7;
}

int sdk_superchunk_gate_arch_top_y(int run_local, int gate_floor_y)
{
    float center = (float)SDK_SUPERCHUNK_GATE_START + ((float)SDK_SUPERCHUNK_GATE_WIDTH - 1.0f) * 0.5f;
    float nx = ((float)run_local - center) / ((float)SDK_SUPERCHUNK_GATE_WIDTH * 0.5f);
    float cap = 1.0f - nx * nx;

    if (cap < 0.0f) cap = 0.0f;
    return gate_floor_y + 13 + (int)floorf(sqrtf(cap) * 16.0f);
}

static int superchunk_wall_gate_floor_y(SdkWorldGen* wg, int super_origin_x, int super_origin_z, int wall_mask)
{
    int inside_y;
    int outside_y;
    int gate_floor_y;
    const int wall_period_blocks = sdk_superchunk_get_wall_grid_period() * CHUNK_WIDTH;

    if (wall_mask == SDK_SUPER_WALL_WEST) {
        int gate_center_z = superchunk_gate_center_world(super_origin_z);
        outside_y = sample_gate_approach_level_ctx(wg, super_origin_x - 1, gate_center_z);
        inside_y = sample_gate_approach_level_ctx(wg, super_origin_x + SDK_SUPERCHUNK_WALL_THICKNESS, gate_center_z);
    } else if (wall_mask == SDK_SUPER_WALL_NORTH) {
        int gate_center_x = superchunk_gate_center_world(super_origin_x);
        outside_y = sample_gate_approach_level_ctx(wg, gate_center_x, super_origin_z - 1);
        inside_y = sample_gate_approach_level_ctx(wg, gate_center_x, super_origin_z + SDK_SUPERCHUNK_WALL_THICKNESS);
    } else if (wall_mask == SDK_SUPER_WALL_EAST) {
        int gate_center_z = superchunk_gate_center_world(super_origin_z);
        outside_y = sample_gate_approach_level_ctx(wg, super_origin_x + wall_period_blocks, gate_center_z);
        inside_y = sample_gate_approach_level_ctx(wg, super_origin_x + wall_period_blocks - SDK_SUPERCHUNK_WALL_THICKNESS, gate_center_z);
    } else if (wall_mask == SDK_SUPER_WALL_SOUTH) {
        int gate_center_x = superchunk_gate_center_world(super_origin_x);
        outside_y = sample_gate_approach_level_ctx(wg, gate_center_x, super_origin_z + wall_period_blocks);
        inside_y = sample_gate_approach_level_ctx(wg, gate_center_x, super_origin_z + wall_period_blocks - SDK_SUPERCHUNK_WALL_THICKNESS);
    } else {
        return 3;
    }

    gate_floor_y = inside_y;
    if (outside_y > gate_floor_y) gate_floor_y = outside_y;
    gate_floor_y += 1;
    if (gate_floor_y < 3) gate_floor_y = 3;
    if (gate_floor_y > CHUNK_HEIGHT - SDK_SUPERCHUNK_GATE_HEIGHT - 2) {
        gate_floor_y = CHUNK_HEIGHT - SDK_SUPERCHUNK_GATE_HEIGHT - 2;
    }
    return gate_floor_y;
}

int sdk_superchunk_gate_floor_y_for_side_ctx(SdkWorldGen* wg, int super_origin_x, int super_origin_z, int side)
{
    switch (side) {
        case 0:
            return superchunk_wall_gate_floor_y(wg, super_origin_x, super_origin_z, SDK_SUPER_WALL_WEST);
        case 1:
            return superchunk_wall_gate_floor_y(wg, super_origin_x, super_origin_z, SDK_SUPER_WALL_NORTH);
        case 2:
            return superchunk_wall_gate_floor_y(wg, super_origin_x, super_origin_z, SDK_SUPER_WALL_EAST);
        case 3:
            return superchunk_wall_gate_floor_y(wg, super_origin_x, super_origin_z, SDK_SUPER_WALL_SOUTH);
        default:
            return 3;
    }
}

static int superchunk_wall_top_y(SdkWorldGen* wg,
                                 int wx,
                                 int wz,
                                 int super_origin_x,
                                 int super_origin_z,
                                 uint8_t wall_mask)
{
    int adjacent_surface_max = 0;
    int wall_top = SDK_SUPERCHUNK_WALL_DEFAULT_TOP;
    const int wall_period_blocks = sdk_superchunk_get_wall_grid_period() * CHUNK_WIDTH;

    if (wall_mask & SDK_SUPER_WALL_WEST) {
        int inside_y = sample_surface_y_clamped_ctx(wg, super_origin_x + SDK_SUPERCHUNK_WALL_THICKNESS, wz);
        int outside_y = sample_surface_y_clamped_ctx(wg, super_origin_x - 1, wz);
        if (inside_y > adjacent_surface_max) adjacent_surface_max = inside_y;
        if (outside_y > adjacent_surface_max) adjacent_surface_max = outside_y;
    }
    if (wall_mask & SDK_SUPER_WALL_NORTH) {
        int inside_y = sample_surface_y_clamped_ctx(wg, wx, super_origin_z + SDK_SUPERCHUNK_WALL_THICKNESS);
        int outside_y = sample_surface_y_clamped_ctx(wg, wx, super_origin_z - 1);
        if (inside_y > adjacent_surface_max) adjacent_surface_max = inside_y;
        if (outside_y > adjacent_surface_max) adjacent_surface_max = outside_y;
    }
    if (wall_mask & SDK_SUPER_WALL_EAST) {
        int inside_y = sample_surface_y_clamped_ctx(wg, super_origin_x + wall_period_blocks - SDK_SUPERCHUNK_WALL_THICKNESS, wz);
        int outside_y = sample_surface_y_clamped_ctx(wg, super_origin_x + wall_period_blocks, wz);
        if (inside_y > adjacent_surface_max) adjacent_surface_max = inside_y;
        if (outside_y > adjacent_surface_max) adjacent_surface_max = outside_y;
    }
    if (wall_mask & SDK_SUPER_WALL_SOUTH) {
        int inside_y = sample_surface_y_clamped_ctx(wg, wx, super_origin_z + wall_period_blocks - SDK_SUPERCHUNK_WALL_THICKNESS);
        int outside_y = sample_surface_y_clamped_ctx(wg, wx, super_origin_z + wall_period_blocks);
        if (inside_y > adjacent_surface_max) adjacent_surface_max = inside_y;
        if (outside_y > adjacent_surface_max) adjacent_surface_max = outside_y;
    }

    if (adjacent_surface_max > SDK_SUPERCHUNK_WALL_RAISE_TRIGGER) {
        wall_top = adjacent_surface_max + SDK_SUPERCHUNK_WALL_CLEARANCE;
    }
    if (wall_top >= CHUNK_HEIGHT) wall_top = CHUNK_HEIGHT - 1;
    return wall_top;
}

static int superchunk_wall_top_y_from_adjacent_max(int adjacent_surface_max)
{
    int wall_top = SDK_SUPERCHUNK_WALL_DEFAULT_TOP;

    if (adjacent_surface_max > SDK_SUPERCHUNK_WALL_RAISE_TRIGGER) {
        wall_top = adjacent_surface_max + SDK_SUPERCHUNK_WALL_CLEARANCE;
    }
    if (wall_top >= CHUNK_HEIGHT) wall_top = CHUNK_HEIGHT - 1;
    return wall_top;
}

static void populate_superchunk_wall_materials(const SdkTerrainColumnProfile* terrain_profile,
                                               int wx,
                                               int wz,
                                               int wall_top_y,
                                               SdkSuperChunkWallProfile* out_profile)
{
    if (!terrain_profile || !out_profile) return;

    out_profile->face_block = BLOCK_STONE_BRICKS;
    out_profile->core_block = rock_block_for_profile(terrain_profile, wx, wall_top_y / 3, wz);
    if (out_profile->core_block == BLOCK_STONE_BRICKS ||
        out_profile->core_block == BLOCK_BRICK ||
        out_profile->core_block == BLOCK_AIR) {
        out_profile->core_block = BLOCK_COBBLESTONE;
    }
    switch (terrain_profile->terrain_province) {
        case TERRAIN_PROVINCE_OPEN_OCEAN:
        case TERRAIN_PROVINCE_CONTINENTAL_SHELF:
        case TERRAIN_PROVINCE_DYNAMIC_COAST:
        case TERRAIN_PROVINCE_ESTUARY_DELTA:
        case TERRAIN_PROVINCE_PEAT_WETLAND:
            out_profile->foundation_block = BLOCK_COMPACTED_FILL;
            break;
        default:
            out_profile->foundation_block = BLOCK_CRUSHED_STONE;
            break;
    }
}

int compute_superchunk_wall_profile(SdkWorldGen* wg,
                                     const SdkTerrainColumnProfile* terrain_profile,
                                     int wx,
                                     int wz,
                                     SdkSuperChunkWallProfile* out_profile)
{
    int cx;
    int cz;
    int origin_cx;
    int origin_cz;
    int is_arch_gate;
    int super_origin_x;
    int super_origin_z;
    int gate_floor_y = -1;
    int local_chunk_x;
    int local_chunk_z;

    if (!wg || !terrain_profile || !out_profile) return 0;

    memset(out_profile, 0, sizeof(*out_profile));
    out_profile->west_gate_floor_y = -1;
    out_profile->north_gate_floor_y = -1;
    out_profile->east_gate_floor_y = -1;
    out_profile->south_gate_floor_y = -1;

    /* Convert world block coordinates to chunk coordinates */
    cx = floor_div_local(wx, CHUNK_WIDTH);
    cz = floor_div_local(wz, CHUNK_DEPTH);

    if (!sdk_worldgen_get_canonical_wall_chunk_owner(cx,
                                                     cz,
                                                     &out_profile->wall_mask,
                                                     &origin_cx,
                                                     &origin_cz,
                                                     NULL,
                                                     NULL)) {
        return 0;
    }

    is_arch_gate = chunk_is_arch_gate(cx, cz);
    super_origin_x = origin_cx * CHUNK_WIDTH;
    super_origin_z = origin_cz * CHUNK_DEPTH;

    if (is_arch_gate) {
        if ((out_profile->wall_mask & SDK_SUPER_WALL_WEST) != 0u) {
            out_profile->gate_mask |= SDK_SUPER_WALL_WEST;
            out_profile->west_gate_floor_y = superchunk_wall_gate_floor_y(wg,
                                                                          super_origin_x,
                                                                          super_origin_z,
                                                                          SDK_SUPER_WALL_WEST);
        }
        if ((out_profile->wall_mask & SDK_SUPER_WALL_NORTH) != 0u) {
            out_profile->gate_mask |= SDK_SUPER_WALL_NORTH;
            out_profile->north_gate_floor_y = superchunk_wall_gate_floor_y(wg,
                                                                           super_origin_x,
                                                                           super_origin_z,
                                                                           SDK_SUPER_WALL_NORTH);
        }
        if ((out_profile->wall_mask & SDK_SUPER_WALL_EAST) != 0u) {
            out_profile->gate_mask |= SDK_SUPER_WALL_EAST;
            out_profile->east_gate_floor_y = superchunk_wall_gate_floor_y(wg,
                                                                          super_origin_x,
                                                                          super_origin_z,
                                                                          SDK_SUPER_WALL_EAST);
        }
        if ((out_profile->wall_mask & SDK_SUPER_WALL_SOUTH) != 0u) {
            out_profile->gate_mask |= SDK_SUPER_WALL_SOUTH;
            out_profile->south_gate_floor_y = superchunk_wall_gate_floor_y(wg,
                                                                           super_origin_x,
                                                                           super_origin_z,
                                                                           SDK_SUPER_WALL_SOUTH);
        }
    }

    out_profile->wall_top_y = superchunk_wall_top_y(wg,
                                                    wx,
                                                    wz,
                                                    super_origin_x,
                                                    super_origin_z,
                                                    out_profile->wall_mask);
    populate_superchunk_wall_materials(terrain_profile, wx, wz, out_profile->wall_top_y, out_profile);

    if ((out_profile->gate_mask & SDK_SUPER_WALL_WEST) != 0u) {
        gate_floor_y = out_profile->west_gate_floor_y;
    } else if ((out_profile->gate_mask & SDK_SUPER_WALL_NORTH) != 0u) {
        gate_floor_y = out_profile->north_gate_floor_y;
    } else if ((out_profile->gate_mask & SDK_SUPER_WALL_EAST) != 0u) {
        gate_floor_y = out_profile->east_gate_floor_y;
    } else if ((out_profile->gate_mask & SDK_SUPER_WALL_SOUTH) != 0u) {
        gate_floor_y = out_profile->south_gate_floor_y;
    }

    local_chunk_x = wx - cx * CHUNK_WIDTH;
    local_chunk_z = wz - cz * CHUNK_DEPTH;
    sdk_worldgen_debug_capture_note_wall_column(local_chunk_x,
                                                local_chunk_z,
                                                out_profile->wall_mask,
                                                out_profile->gate_mask,
                                                out_profile->wall_top_y,
                                                gate_floor_y);

    return 1;
}

int superchunk_wall_gate_open_at(const SdkSuperChunkWallProfile* wall_profile,
                                 int local_super_x,
                                 int local_super_z,
                                 int y)
{
    int arch_top_y;
    const int wall_period_blocks = sdk_superchunk_get_wall_grid_period() * CHUNK_WIDTH;

    if (!wall_profile) return 0;

    /* West wall gate check - position must be on west wall AND within gate Z range */
    if ((wall_profile->wall_mask & SDK_SUPER_WALL_WEST) != 0u &&
        (wall_profile->gate_mask & SDK_SUPER_WALL_WEST) != 0u) {
        if (local_super_x < SDK_SUPERCHUNK_WALL_THICKNESS &&
            local_super_z >= 0 && local_super_z < SDK_SUPERCHUNK_BLOCK_SPAN &&
            superchunk_gate_contains(local_super_z)) {
            arch_top_y = sdk_superchunk_gate_arch_top_y(local_super_z, wall_profile->west_gate_floor_y);
            return y >= wall_profile->west_gate_floor_y && y <= arch_top_y;
        }
    }

    /* North wall gate check - position must be on north wall AND within gate X range */
    if ((wall_profile->wall_mask & SDK_SUPER_WALL_NORTH) != 0u &&
        (wall_profile->gate_mask & SDK_SUPER_WALL_NORTH) != 0u) {
        if (local_super_z < SDK_SUPERCHUNK_WALL_THICKNESS &&
            local_super_x >= 0 && local_super_x < SDK_SUPERCHUNK_BLOCK_SPAN &&
            superchunk_gate_contains(local_super_x)) {
            arch_top_y = sdk_superchunk_gate_arch_top_y(local_super_x, wall_profile->north_gate_floor_y);
            return y >= wall_profile->north_gate_floor_y && y <= arch_top_y;
        }
    }

    if ((wall_profile->wall_mask & SDK_SUPER_WALL_EAST) != 0u &&
        (wall_profile->gate_mask & SDK_SUPER_WALL_EAST) != 0u) {
        if (local_super_x >= wall_period_blocks &&
            local_super_x < wall_period_blocks + SDK_SUPERCHUNK_WALL_THICKNESS &&
            local_super_z >= 0 && local_super_z < SDK_SUPERCHUNK_BLOCK_SPAN &&
            superchunk_gate_contains(local_super_z)) {
            arch_top_y = sdk_superchunk_gate_arch_top_y(local_super_z, wall_profile->east_gate_floor_y);
            return y >= wall_profile->east_gate_floor_y && y <= arch_top_y;
        }
    }

    if ((wall_profile->wall_mask & SDK_SUPER_WALL_SOUTH) != 0u &&
        (wall_profile->gate_mask & SDK_SUPER_WALL_SOUTH) != 0u) {
        if (local_super_z >= wall_period_blocks &&
            local_super_z < wall_period_blocks + SDK_SUPERCHUNK_WALL_THICKNESS &&
            local_super_x >= 0 && local_super_x < SDK_SUPERCHUNK_BLOCK_SPAN &&
            superchunk_gate_contains(local_super_x)) {
            arch_top_y = sdk_superchunk_gate_arch_top_y(local_super_x, wall_profile->south_gate_floor_y);
            return y >= wall_profile->south_gate_floor_y && y <= arch_top_y;
        }
    }

    return 0;
}

static int superchunk_wall_gate_open_at_adjacent(SdkWorldGen* wg,
                                                 int wx,
                                                 int wz,
                                                 int y,
                                                 int local_super_x,
                                                 int local_super_z)
{
    int super_origin_x = superchunk_origin_for_coord_axis(wx, 0);
    int super_origin_z = superchunk_origin_for_coord_axis(wz, 0);
    int gate_floor_y;
    int arch_top_y;

    /* West wall gate (blocks 0-63) */
    if (local_super_x < SDK_SUPERCHUNK_WALL_THICKNESS) {
        if (local_super_z >= 0 && local_super_z < SDK_SUPERCHUNK_BLOCK_SPAN &&
            superchunk_gate_contains(local_super_z)) {
            gate_floor_y = sdk_superchunk_gate_floor_y_for_side_ctx(wg, super_origin_x, super_origin_z, 0);
            arch_top_y = sdk_superchunk_gate_arch_top_y(local_super_z, gate_floor_y);
            if (y >= gate_floor_y && y <= arch_top_y) {
                return 1;
            }
        }
    }

    /* North wall gate (blocks 0-63) */
    if (local_super_z < SDK_SUPERCHUNK_WALL_THICKNESS) {
        if (local_super_x >= 0 && local_super_x < SDK_SUPERCHUNK_BLOCK_SPAN &&
            superchunk_gate_contains(local_super_x)) {
            gate_floor_y = sdk_superchunk_gate_floor_y_for_side_ctx(wg, super_origin_x, super_origin_z, 1);
            arch_top_y = sdk_superchunk_gate_arch_top_y(local_super_x, gate_floor_y);
            if (y >= gate_floor_y && y <= arch_top_y) {
                return 1;
            }
        }
    }

    return 0;
}

static int superchunk_wall_cremnel_gap(int run_local, int y, int wall_top_y, int face_strip, int gate_pier)
{
    if (!face_strip || gate_pier) return 0;
    if (y < wall_top_y - 2 || y > wall_top_y) return 0;
    return ((run_local / 8) & 1) != 0;
}

static BlockType superchunk_wall_material_for_y(const SdkSuperChunkWallProfile* wall_profile,
                                                int wx,
                                                int wz,
                                                int run_local,
                                                int outer_face,
                                                int buttress_zone,
                                                int gate_pier_zone,
                                                int y)
{
    uint32_t hash;

    if (y <= 7) return wall_profile->foundation_block;
    if (y <= 11) return BLOCK_CRUSHED_STONE;
    if (outer_face || buttress_zone || gate_pier_zone) return wall_profile->face_block;
    if (y >= wall_profile->wall_top_y - 8) return BLOCK_COBBLESTONE;

    hash = sdk_worldgen_hash2d(wx + y * 13, wz - y * 7, 0x7123u);
    return ((hash & 3u) == 0u) ? BLOCK_COBBLESTONE : wall_profile->core_block;
}

static int cave_margin_for_column(const SdkTerrainColumnProfile* profile, int wx, int wz)
{
    int margin = 4;
    int cx;
    int cz;
    int chunk_local_super_x;
    int chunk_local_super_z;

    if (!profile) return margin;

    if (profile->water_height > profile->surface_height) margin += 5;
    if (profile->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN ||
        profile->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
        profile->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) {
        margin += 7;
    } else if (fabsf((float)profile->surface_height - (float)profile->water_height) <= 1.0f &&
               profile->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA) {
        margin += 4;
    }

    /* Check if this block is in a wall chunk */
    cx = wx / CHUNK_WIDTH;
    cz = wz / CHUNK_DEPTH;
    chunk_local_super_x = floor_mod_superchunk(cx, SDK_SUPERCHUNK_WALL_PERIOD);
    chunk_local_super_z = floor_mod_superchunk(cz, SDK_SUPERCHUNK_WALL_PERIOD);
    if (chunk_local_super_x == 0 || chunk_local_super_z == 0) {
        margin += 14;
    }

    if ((profile->landform_flags & SDK_LANDFORM_CAVE_ENTRANCE) != 0u ||
        (profile->landform_flags & SDK_LANDFORM_RAVINE) != 0u ||
        (profile->landform_flags & SDK_LANDFORM_CALDERA) != 0u) {
        margin -= 2;
    }

    return sdk_worldgen_clampi(margin, 3, 20);
}

static void carve_geology_caves_in_column(SdkChunk* chunk,
                                          SdkWorldGen* wg,
                                          const SdkTerrainColumnProfile* profile,
                                          const SdkRegionFieldSample* geology,
                                          int lx,
                                          int lz,
                                          int wx,
                                          int wz)
{
    SdkWorldGenImpl* impl;
    float cave_strength;
    int roof_margin;
    int carve_top;
    int surface_y;
    int ly;

    if (!chunk || !wg || !wg->impl || !profile || !geology) return;
    impl = (SdkWorldGenImpl*)wg->impl;
    cave_strength = fmaxf(geology->karst_mask, fmaxf(geology->fracture_cave_mask, geology->lava_tube_mask));
    if (cave_strength < 0.08f) return;

    surface_y = profile->surface_height;
    roof_margin = cave_margin_for_column(profile, wx, wz);
    carve_top = surface_y - roof_margin;
    if (carve_top <= 6) return;

    for (ly = 5; ly <= carve_top; ++ly) {
        BlockType block = sdk_chunk_get_block(chunk, lx, ly, lz);
        float roof_depth = (float)(surface_y - ly);
        float chamber_noise;
        float fracture_noise;
        float tube_noise;
        float karst_window;
        float fracture_window;
        float lava_window;
        float threshold;
        float openness = 0.0f;
        int make_lava = 0;

        if (block == BLOCK_BEDROCK || block == BLOCK_WATER || block == BLOCK_ICE ||
            block == BLOCK_SEA_ICE || block == BLOCK_LAVA || !sdk_block_is_solid(block)) {
            continue;
        }

        chamber_noise = pseudo_noise3_local(wx, ly, wz, impl->seed ^ 0xE4A1u, 0.052f, 3);
        fracture_noise = pseudo_noise3_local(wx + 37, ly - 11, wz - 19, impl->seed ^ 0xE5B2u, 0.075f, 2);
        tube_noise = pseudo_noise3_local(wx - 23, ly + 7, wz + 41, impl->seed ^ 0xE6C3u, 0.068f, 2);

        karst_window = 1.0f - fabsf((roof_depth - (10.0f + geology->cave_depth_bias * 18.0f)) /
                                    (8.0f + geology->karst_mask * 18.0f));
        if (karst_window < 0.0f) karst_window = 0.0f;
        fracture_window = 1.0f - fabsf((roof_depth - (14.0f + geology->cave_depth_bias * 20.0f)) /
                                       (10.0f + geology->fracture_cave_mask * 18.0f));
        if (fracture_window < 0.0f) fracture_window = 0.0f;
        lava_window = 1.0f - fabsf((roof_depth - (6.0f + geology->lava_tube_mask * 10.0f + geology->shield_mask * 6.0f)) /
                                   (4.0f + geology->lava_tube_mask * 7.0f));
        if (lava_window < 0.0f) lava_window = 0.0f;

        if (geology->karst_mask > 0.08f) {
            float karst_score = chamber_noise * 0.72f + fracture_noise * 0.28f;
            if (karst_score > (0.32f - geology->karst_mask * 0.28f) && karst_window > 0.0f) {
                openness = fmaxf(openness, karst_window * geology->karst_mask);
            }
        }

        if (geology->fracture_cave_mask > 0.08f) {
            float fracture_score = 1.0f - fabsf(fracture_noise + geology->fault_mask * 0.35f);
            if (fracture_score > (0.58f - geology->fracture_cave_mask * 0.14f) && fracture_window > 0.0f) {
                openness = fmaxf(openness, fracture_window * geology->fracture_cave_mask);
            }
        }

        if (geology->lava_tube_mask > 0.08f) {
            float lava_score = tube_noise * 0.60f + chamber_noise * 0.40f;
            if (lava_score > (0.36f - geology->lava_tube_mask * 0.20f) && lava_window > 0.0f) {
                openness = fmaxf(openness, lava_window * geology->lava_tube_mask);
                if (geology->vent_mask > 0.68f && geology->lava_flow_bias > 0.66f &&
                    roof_depth > 14.0f && lava_score > 0.60f) {
                    make_lava = 1;
                }
            }
        }

        threshold = 0.20f + sdk_worldgen_clampf(roof_depth / 48.0f, 0.0f, 0.18f);
        if ((profile->landform_flags & SDK_LANDFORM_CAVE_ENTRANCE) != 0u && roof_depth < 9.0f) {
            threshold -= geology->cave_entrance_mask * 0.16f;
        }
        if ((profile->landform_flags & SDK_LANDFORM_RAVINE) != 0u && roof_depth < 12.0f) {
            threshold -= geology->ravine_mask * 0.10f;
        }

        if (openness < threshold) continue;

        if (make_lava && ly <= profile->surface_height - 12) {
            worldgen_set_block_fast(chunk, lx, ly, lz, BLOCK_LAVA);
        } else {
            worldgen_set_block_fast(chunk, lx, ly, lz, BLOCK_AIR);
        }
    }
}

static void apply_superchunk_walls(SdkWorldGen* wg, SdkChunk* chunk)
{
    const SdkSuperchunkConfig* config;
    int chunk_wx0;
    int chunk_wz0;
    uint8_t wall_mask;
    int origin_cx;
    int origin_cz;
    int super_origin_x;
    int super_origin_z;
    int lx;
    int lz;
    int period_local_x;  /* Position within 17-chunk period (0-16) */
    int period_local_z;  /* Position within 17-chunk period (0-16) */
    int is_arch_gate;

    if (!wg || !chunk || !chunk->blocks) return;

    config = sdk_superchunk_get_config();
    if (!config || !config->enabled || !config->walls_enabled) return;

    if (!sdk_worldgen_get_canonical_wall_chunk_owner(chunk->cx,
                                                     chunk->cz,
                                                     &wall_mask,
                                                     &origin_cx,
                                                     &origin_cz,
                                                     &period_local_x,
                                                     &period_local_z)) {
        return;
    }

    chunk_wx0 = chunk->cx * CHUNK_WIDTH;
    chunk_wz0 = chunk->cz * CHUNK_DEPTH;
    super_origin_x = origin_cx * CHUNK_WIDTH;
    super_origin_z = origin_cz * CHUNK_DEPTH;

    /* Check if this wall chunk intersects the configured gate support run. */
    is_arch_gate = chunk_is_arch_gate(chunk->cx, chunk->cz);

    if (SDK_VERBOSE_WALL_DEBUG_LOGS) {
        char buf[512];
        sprintf_s(buf, sizeof(buf),
                 "WALL_APPLY: chunk=(%d,%d) wall_mask=0x%02x period=(%d,%d) arch_gate=%d",
                 chunk->cx, chunk->cz, wall_mask, period_local_x, period_local_z, is_arch_gate);
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
    }

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int wx = chunk_wx0 + lx;
            int wz = chunk_wz0 + lz;
            SdkTerrainColumnProfile terrain_profile;
            SdkSuperChunkWallProfile wall_profile;
            int local_super_x;
            int local_super_z;
            int run_local;
            int face_strip = 0;
            int buttress_zone = 0;
            int gate_pier_zone = 0;
            int ly;

            if (!sdk_worldgen_sample_column_ctx(wg, wx, wz, &terrain_profile)) continue;
            if (!compute_superchunk_wall_profile(wg, &terrain_profile, wx, wz, &wall_profile)) continue;

            local_super_x = wx - super_origin_x;
            local_super_z = wz - super_origin_z;

            /* Determine wall orientation and calculate run_local */
            if ((wall_profile.wall_mask & SDK_SUPER_WALL_WEST) != 0u &&
                (wall_profile.wall_mask & SDK_SUPER_WALL_NORTH) == 0u) {
                run_local = local_super_z;
                face_strip = superchunk_wall_face_strip_x(lx);
                if (run_local >= 0 && run_local < SDK_SUPERCHUNK_BLOCK_SPAN) {
                    buttress_zone = superchunk_buttress_zone(run_local);
                    gate_pier_zone = superchunk_gate_pier_zone(run_local);
                }
            } else if ((wall_profile.wall_mask & SDK_SUPER_WALL_NORTH) != 0u &&
                       (wall_profile.wall_mask & SDK_SUPER_WALL_WEST) == 0u) {
                run_local = local_super_x;
                face_strip = superchunk_wall_face_strip_z(lz);
                if (run_local >= 0 && run_local < SDK_SUPERCHUNK_BLOCK_SPAN) {
                    buttress_zone = superchunk_buttress_zone(run_local);
                    gate_pier_zone = superchunk_gate_pier_zone(run_local);
                }
            } else {
                run_local = local_super_x;
                face_strip = superchunk_wall_face_strip_x(lx) || superchunk_wall_face_strip_z(lz);
            }

            for (ly = wall_profile.wall_top_y + 1; ly < CHUNK_HEIGHT; ++ly) {
                if (sdk_chunk_get_block(chunk, lx, ly, lz) != BLOCK_AIR) {
                    worldgen_set_block_fast(chunk, lx, ly, lz, BLOCK_AIR);
                }
            }

            for (ly = 0; ly <= wall_profile.wall_top_y; ++ly) {
                if (superchunk_wall_gate_open_at(&wall_profile, local_super_x, local_super_z, ly)) {
                    worldgen_set_block_fast(chunk, lx, ly, lz, BLOCK_AIR);
                    continue;
                }

                if (superchunk_wall_cremnel_gap(run_local,
                                                ly,
                                                wall_profile.wall_top_y,
                                                face_strip,
                                                gate_pier_zone)) {
                    worldgen_set_block_fast(chunk, lx, ly, lz, BLOCK_AIR);
                    continue;
                }

                worldgen_set_block_fast(chunk,
                                        lx,
                                        ly,
                                        lz,
                                        superchunk_wall_material_for_y(&wall_profile,
                                                                       wx,
                                                                       wz,
                                                                       run_local,
                                                                       face_strip,
                                                                       buttress_zone,
                                                                       gate_pier_zone,
                                                                       ly));
            }
        }
    }
}

void sdk_worldgen_finalize_chunk_walls_ctx(SdkWorldGen* wg, SdkChunk* chunk)
{
    apply_superchunk_walls(wg, chunk);
}

static void create_lake_banking(SdkWorldGen* wg, SdkChunk* chunk, int lx, int lz, int wx, int wz, int water_level, int surface_y)
{
    int bank_height;
    int ly;
    SdkTerrainColumnProfile profile;
    BlockType bank_block;
    
    if (surface_y >= water_level - 1) return;
    if (!sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) return;
    
    bank_block = soil_block_for_profile(&profile);
    if (bank_block == BLOCK_AIR || bank_block == BLOCK_WATER) {
        bank_block = BLOCK_DIRT;
    }
    
    bank_height = water_level - 1;
    if (bank_height > surface_y + 3) bank_height = surface_y + 3;
    
    for (ly = surface_y + 1; ly <= bank_height && ly < CHUNK_HEIGHT; ++ly) {
        BlockType current = sdk_chunk_get_block(chunk, lx, ly, lz);
        if (current == BLOCK_AIR || current == BLOCK_WATER) {
            sdk_chunk_set_block(chunk, lx, ly, lz, bank_block);
        }
    }
}

static void seal_generated_water_chunk(SdkWorldGen* wg, SdkChunk* chunk)
{
    GeneratedWaterHaloSample* halo;
    int halo_width;
    int lx;
    int lz;
    int chunk_wx0;
    int chunk_wz0;
    static const int dx[4] = { -1, 1, 0, 0 };
    static const int dz[4] = { 0, 0, -1, 1 };

    if (!wg || !chunk || !chunk->blocks) return;

    halo_width = CHUNK_WIDTH + 2;
    halo = (GeneratedWaterHaloSample*)malloc((size_t)(CHUNK_DEPTH + 2) *
                                             (size_t)halo_width *
                                             sizeof(*halo));
    if (!halo) return;

    chunk_wx0 = chunk->cx * CHUNK_WIDTH;
    chunk_wz0 = chunk->cz * CHUNK_DEPTH;
    sample_generated_water_halo(wg, chunk, halo);

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int wx = chunk_wx0 + lx;
            int wz = chunk_wz0 + lz;
            const GeneratedWaterHaloSample* sample = &halo[(lz + 1) * halo_width + (lx + 1)];

            if (!sample->has_water || sample->waterline < 0) continue;

            if (sample->inland &&
                sample->surface_y >= 0 &&
                sample->surface_y < sample->waterline - 2) {
                int dir;
                int has_higher_neighbor = 0;

                for (dir = 0; dir < 4; ++dir) {
                    const GeneratedWaterHaloSample* neighbor =
                        &halo[(lz + 1 + dz[dir]) * halo_width + (lx + 1 + dx[dir])];
                    if (neighbor->surface_y > sample->waterline) {
                        has_higher_neighbor = 1;
                        break;
                    }
                }

                if (has_higher_neighbor) {
                    create_lake_banking(wg,
                                        chunk,
                                        lx,
                                        lz,
                                        wx,
                                        wz,
                                        sample->waterline,
                                        sample->surface_y);
                }

                sdk_worldgen_debug_capture_note_water_seal(lx,
                                                           lz,
                                                           sample->waterline,
                                                           (BlockType)sample->cap_block,
                                                           has_higher_neighbor);
            } else {
                sdk_worldgen_debug_capture_note_water_seal(lx,
                                                           lz,
                                                           sample->waterline,
                                                           (BlockType)sample->cap_block,
                                                           0);
            }

            fill_generated_water_column_to_line(chunk,
                                                lx,
                                                lz,
                                                sample->waterline,
                                                (BlockType)sample->cap_block);
        }
    }
    free(halo);
}
static int chunk_is_full_superchunk_wall_chunk(const SdkChunk* chunk)
{
    int is_wall;
    int origin_cx = 0;
    int origin_cz = 0;
    int period_local_x = 0;
    int period_local_z = 0;
    uint8_t wall_mask = 0u;

    if (!chunk) return 0;

    is_wall = sdk_worldgen_get_canonical_wall_chunk_owner(chunk->cx,
                                                          chunk->cz,
                                                          &wall_mask,
                                                          &origin_cx,
                                                          &origin_cz,
                                                          &period_local_x,
                                                          &period_local_z);

    /* Debug: log all wall detection values */
    {
        char buf[512];
        sprintf_s(buf, sizeof(buf),
            "WALL_CHECK: chunk=(%d,%d) local=(%d,%d) origin=(%d,%d) "
            "mask=0x%02x is_wall=%d",
            chunk->cx, chunk->cz,
            period_local_x, period_local_z,
            origin_cx, origin_cz,
            wall_mask, is_wall);
        sdk_worldgen_debug_capture_note_custom(buf);
    }
    
    /* Debug: log wall chunk detection */
    if (is_wall) {
        char buf[256];
        char side[32] = "";
        if ((wall_mask & (SDK_SUPER_WALL_WEST | SDK_SUPER_WALL_NORTH)) ==
            (SDK_SUPER_WALL_WEST | SDK_SUPER_WALL_NORTH)) {
            strcpy_s(side, sizeof(side), "WEST|NORTH");
        } else if ((wall_mask & SDK_SUPER_WALL_WEST) != 0u) {
            strcpy_s(side, sizeof(side), "WEST");
        } else if ((wall_mask & SDK_SUPER_WALL_NORTH) != 0u) {
            strcpy_s(side, sizeof(side), "NORTH");
        }
        
        sprintf_s(buf, sizeof(buf), "WALL_CHUNK_DETECTED: chunk=(%d,%d) side=%s",
                 chunk->cx, chunk->cz, side);
        sdk_worldgen_debug_capture_note_custom(buf);
    }
    
    return is_wall;
}

static int chunk_uses_wall_only_generation(const SdkWorldGen* wg, const SdkChunk* chunk)
{
    if (!wg || !chunk || !wg->desc.walls_enabled) return 0;

    switch ((SdkWorldCoordinateSystem)wg->desc.coordinate_system) {
        case SDK_WORLD_COORDSYS_CHUNK_SYSTEM:
        case SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM:
            return chunk_is_full_superchunk_wall_chunk(chunk);
        case SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM:
            return (SdkCoordinateSpaceType)chunk->space_type == SDK_SPACE_WALL_GRID;
        default:
            return 0;
    }
}

void sdk_worldgen_fill_chunk(SdkWorldGen* wg, SdkChunk* chunk)
{
    int lx;
    int lz;

    /* Debug: log entry to verify function is being called */
    {
        char buf[256];
        sprintf_s(buf, sizeof(buf), "FILL_CHUNK_ENTRY: chunk=(%d,%d) blocks=%p",
                  chunk ? chunk->cx : -999, chunk ? chunk->cz : -999,
                  chunk ? chunk->blocks : NULL);
        sdk_worldgen_debug_capture_note_custom(buf);
    }

    if (!wg || !wg->impl || !chunk || !chunk->blocks) return;

    sdk_chunk_clear(chunk);

    /*
     * A one-chunk-thick perimeter wall fully occupies these edge chunks in
     * block space. Generating normal terrain first is wasted work because the
     * wall pass overwrites the chunk almost completely.
     */
    if (chunk_uses_wall_only_generation(wg, chunk)) {
        apply_superchunk_walls(wg, chunk);
        /* Generate settlements even for wall chunks */
        if (wg->impl && ((SdkWorldGenImpl*)wg->impl)->settlements_enabled) {
            SuperchunkSettlementData* settlement_data = sdk_settlement_get_or_create_data(wg, chunk->cx, chunk->cz);
            if (settlement_data) {
                sdk_worldgen_debug_capture_note_settlement_stage();
                sdk_settlement_generate_for_chunk(wg, chunk, settlement_data);
            }
        }
        if (wg->impl && ((SdkWorldGenImpl*)wg->impl)->construction_cells_enabled) {
            generate_world_cells(wg, chunk);
        }
        chunk->dirty = true;
        chunk->empty = false;
        return;
    }

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int wx = chunk->cx * CHUNK_WIDTH + lx;
            int wz = chunk->cz * CHUNK_DEPTH + lz;
            SdkTerrainColumnProfile profile;
            SdkRegionFieldSample geology;
            SdkStrataColumn strata;
            BlockType top_block;
            BlockType fill_block;
            SdkWorldGenRegionTile* region_tile;
            int surface_y;
            int water_y;
            int soil_start;
            int sed_start;
            int ly;

            if (!sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) continue;
            region_tile = sdk_worldgen_require_region_tile(wg, wx, wz);
            if (region_tile) {
                sdk_worldgen_sample_region_fields(region_tile, wg, wx, wz, &geology);
            } else {
                memset(&geology, 0, sizeof(geology));
                geology.weathered_base = (float)(profile.surface_height - profile.soil_depth - profile.sediment_depth - profile.regolith_thickness);
                geology.upper_top = geology.weathered_base - 18.0f;
                geology.lower_top = geology.upper_top - 32.0f;
                geology.basement_top = geology.lower_top - 14.0f;
                geology.deep_basement_top = geology.basement_top - 84.0f;
                geology.stratigraphy_control = 0.5f;
                geology.terrain_province = (uint8_t)profile.terrain_province;
                geology.bedrock_province = (uint8_t)profile.bedrock_province;
                geology.temperature_band = (uint8_t)profile.temperature_band;
                geology.moisture_band = (uint8_t)profile.moisture_band;
                geology.surface_sediment = (uint8_t)profile.surface_sediment;
                geology.parent_material = (uint8_t)profile.parent_material;
                geology.drainage_class = (uint8_t)profile.drainage_class;
                geology.soil_reaction = (uint8_t)profile.soil_reaction;
                geology.soil_fertility = (uint8_t)profile.soil_fertility;
                geology.soil_salinity = (uint8_t)profile.soil_salinity;
                geology.drainage_index = drainage_index_for_class(profile.drainage_class);
                geology.soil_reaction_value = soil_reaction_value_for_class(profile.soil_reaction);
                geology.soil_fertility_value = soil_fertility_value_for_class(profile.soil_fertility);
                geology.soil_salinity_value = soil_salinity_value_for_class(profile.soil_salinity);
                geology.soil_organic_value = soil_organic_value_for_state(profile.surface_sediment,
                                                                          profile.parent_material,
                                                                          profile.terrain_province,
                                                                          profile.moisture_band >= MOISTURE_HUMID ? 0.75f : 0.35f);
                geology.water_table_depth = (float)profile.water_table_depth;
                geology.trap_strength = sdk_worldgen_clampf(profile.hydrocarbon_class >= SDK_HYDROCARBON_CRUDE_OIL ? 0.68f : 0.30f, 0.0f, 1.0f);
                geology.seal_quality = sdk_worldgen_clampf(profile.hydrocarbon_class == SDK_HYDROCARBON_NATURAL_GAS ? 0.72f :
                                                           (profile.hydrocarbon_class == SDK_HYDROCARBON_CRUDE_OIL ? 0.64f : 0.28f),
                                                           0.0f, 1.0f);
                geology.gas_bias = sdk_worldgen_clampf(profile.hydrocarbon_class == SDK_HYDROCARBON_NATURAL_GAS ? 0.75f : 0.25f, 0.0f, 1.0f);
                geology.source_richness = sdk_worldgen_unpack_unorm8(profile.resource_grade);
                geology.ecology = (uint8_t)profile.ecology;
                geology.resource_province = (uint8_t)profile.resource_province;
                geology.hydrocarbon_class = (uint8_t)profile.hydrocarbon_class;
                geology.resource_grade = profile.resource_grade;
                geology.water_surface_class = (uint8_t)profile.water_surface_class;
            }

            surface_y = profile.surface_height;
            water_y = profile.water_height;
            top_block = soil_block_for_profile(&profile);
            fill_block = subsoil_block_for_profile(&profile);
            soil_start = surface_y - (int)profile.soil_depth;
            sed_start = soil_start - (int)profile.sediment_depth;
            build_strata_column(wg, &profile, &geology, wx, wz, surface_y, &strata);

            for (ly = 0; ly <= surface_y; ++ly) {
                BlockType block;

                if (ly <= 1) {
                    block = BLOCK_BEDROCK;
                } else if (ly == surface_y) {
                    block = top_block;
                } else if (ly > soil_start) {
                    block = (top_block == BLOCK_TURF || top_block == BLOCK_WETLAND_SOD) ? BLOCK_TOPSOIL : fill_block;
                } else if (ly > sed_start) {
                    block = fill_block;
                } else {
                    block = stratigraphic_block_for_y(&profile, &geology, &strata, wx, ly, wz);
                    block = apply_fault_zone_override(&geology, &strata, block, ly);
                    block = maybe_resource_block(wg, &profile, &geology, &strata, block, wx, ly, wz, surface_y);
                }

                sdk_chunk_set_block(chunk, lx, ly, lz, block);
            }

            carve_geology_caves_in_column(chunk, wg, &profile, &geology, lx, lz, wx, wz);

            if (water_y > surface_y) {
                int icy = -1;
                BlockType ice_block = BLOCK_ICE;
                if (profile.water_surface_class == SURFACE_WATER_SEASONAL_ICE ||
                    profile.water_surface_class == SURFACE_WATER_PERENNIAL_ICE) {
                    icy = water_y;
                    if (profile.terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN ||
                        profile.terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
                        profile.terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) {
                        ice_block = BLOCK_SEA_ICE;
                    }
                }
                for (ly = surface_y + 1; ly <= water_y && ly < CHUNK_HEIGHT; ++ly) {
                    sdk_chunk_set_block(chunk, lx, ly, lz, (ly == icy) ? ice_block : BLOCK_WATER);
                }
            } else if (profile.river_order > 0 && profile.river_bed_height < profile.surface_height) {
                int river_water_top = profile.river_bed_height + 1 + (profile.river_order >= 3 ? 1 : 0);
                if (river_water_top > surface_y) river_water_top = surface_y;
                
                for (ly = profile.river_bed_height + 1; ly <= river_water_top && ly < CHUNK_HEIGHT; ++ly) {
                    BlockType current = sdk_chunk_get_block(chunk, lx, ly, lz);
                    if (current == BLOCK_AIR || current == BLOCK_GRASS || current == BLOCK_TURF) {
                        sdk_chunk_set_block(chunk, lx, ly, lz, BLOCK_WATER);
                    }
                }
            }

            if (water_y <= surface_y &&
                geology.vent_mask > 0.82f &&
                geology.caldera_mask > 0.28f &&
                geology.lava_flow_bias > 0.18f &&
                (profile.terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ||
                 profile.terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU)) {
                int lava_top = surface_y + 1 + sdk_worldgen_clampi((int)lrintf(geology.caldera_mask * 2.5f + geology.lava_flow_bias * 1.5f), 0, 3);
                if (lava_top > CHUNK_HEIGHT - 1) lava_top = CHUNK_HEIGHT - 1;
                for (ly = surface_y + 1; ly <= lava_top; ++ly) {
                    sdk_chunk_set_block(chunk, lx, ly, lz, BLOCK_LAVA);
                }
            }

            if (surface_y > 0 &&
                water_y <= surface_y &&
                sdk_worldgen_top_block_supports_surface_flora(top_block)) {
                maybe_place_tree(chunk, wg, lx, surface_y, lz, &profile);
                maybe_place_surface_plant(chunk, wg, lx, surface_y, lz, &profile, top_block);
            }
        }
    }

    if (wg->impl && ((SdkWorldGenImpl*)wg->impl)->settlements_enabled) {
        SuperchunkSettlementData* settlement_data = sdk_settlement_get_or_create_data(wg, chunk->cx, chunk->cz);
        if (settlement_data) {
            sdk_worldgen_debug_capture_note_settlement_stage();
            sdk_settlement_generate_for_chunk(wg, chunk, settlement_data);
        }
    }

    if (wg->impl && ((SdkWorldGenImpl*)wg->impl)->construction_cells_enabled) {
        generate_world_cells(wg, chunk);
    }

    seal_generated_water_chunk(wg, chunk);
    apply_superchunk_walls(wg, chunk);
    chunk->dirty = true;
    chunk->empty = false;
}
