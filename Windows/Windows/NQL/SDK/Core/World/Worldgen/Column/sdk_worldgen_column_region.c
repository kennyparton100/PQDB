/**
 * sdk_worldgen_column_region.c -- Macro/region sampling and interpolation helpers.
 */
#include "sdk_worldgen_column_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

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
    return sdk_worldgen_classify_temperature(temp);
}

static uint8_t classify_moisture(float moisture)
{
    return sdk_worldgen_classify_moisture(moisture);
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

    if (terrain_province == TERRAIN_PROVINCE_SALT_FLAT_PLAYA) {
        return ECOLOGY_SALT_DESERT;
    }

    if (terrain_province == TERRAIN_PROVINCE_BADLANDS_DISSECTED) {
        return ECOLOGY_SCRUB_BADLANDS;
    }

    if (coast_signed_distance >= 0.0f && coast_signed_distance < 16.0f &&
        temperature_band >= TEMP_SUBTROPICAL &&
        moisture_band >= MOISTURE_HUMID &&
        (surface_sediment == SEDIMENT_MARINE_MUD || surface_sediment == SEDIMENT_DELTAIC_SILT)) {
        return ECOLOGY_MANGROVE_SWAMP;
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
        if (soil_fertility >= SOIL_FERTILITY_MODERATE) {
            if (moisture_band >= MOISTURE_SUBHUMID && wetness >= 0.36f) return ECOLOGY_SAVANNA_GRASSLAND;
            return ECOLOGY_PRAIRIE;
        }
        return ECOLOGY_MEDITERRANEAN_SCRUB;
    }

    if (temperature_band >= TEMP_TROPICAL) {
        if (moisture_band >= MOISTURE_HUMID && drainage_class >= DRAINAGE_MODERATE) return ECOLOGY_TROPICAL_RAINFOREST;
        if (moisture_band >= MOISTURE_SUBHUMID) return ECOLOGY_TROPICAL_SEASONAL_FOREST;
        if (soil_fertility >= SOIL_FERTILITY_MODERATE) {
            if (moisture_band >= MOISTURE_SUBHUMID && wetness >= 0.32f) return ECOLOGY_SAVANNA_GRASSLAND;
            return ECOLOGY_PRAIRIE;
        }
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
    uint8_t vals[4];
    float weights[4];
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

void sdk_worldgen_sample_column_from_tile(SdkWorldGenMacroTile* tile, SdkWorldGen* wg,
                                          int wx, int wz, SdkTerrainColumnProfile* out_profile)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    int macro_x;
    int macro_z;
    int origin_macro_x;
    int origin_macro_z;
    int local_macro_x;
    int local_macro_z;
    int block_x_in_macro;
    int block_z_in_macro;
    float tx;
    float tz;
    int ix;
    int iz;
    const SdkWorldGenMacroCell* c00;
    const SdkWorldGenMacroCell* c10;
    const SdkWorldGenMacroCell* c01;
    const SdkWorldGenMacroCell* c11;
    float w00;
    float w10;
    float w01;
    float w11;
    float province_conf;
    uint8_t terrain_province;
    uint8_t bedrock_province;
    uint8_t ecology;
    uint8_t resource_province;
    uint8_t hydrocarbon_class;
    uint8_t temperature_band;
    uint8_t moisture_band;
    uint8_t surface_sediment;
    uint8_t parent_material;
    uint8_t drainage_class;
    uint8_t soil_reaction;
    uint8_t soil_fertility;
    uint8_t soil_salinity;
    uint8_t water_surface_class;
    float detail_amp;
    float relief_strength;
    float mountain_mask;
    float river_strength;
    float wetness;
    float slope_f;
    float river_order_f;
    float floodplain_width_f;
    float soil_depth_f;
    float sediment_depth_f;
    float drainage_index;
    float soil_reaction_value;
    float soil_fertility_value;
    float soil_salinity_value;
    float water_table_depth_f;
    float resource_grade_f;
    float lowland_weight;
    float marine_weight;
    float hardrock_weight;
    float volcanic_weight;
    float dir_x;
    float dir_z;
    float macro_surface;
    float detail;
    float mountain_detail;
    float detail_warp;
    float crest;
    float breakup;
    float erosion_noise;
    float lowland_flatten;
    float river_flatten;
    float base_relief_scale;
    int surface;
    int base;
    int river_bed;
    int water_height;
    float channel_dist;
    float width_blocks = 0.0f;

    if (!impl || !tile || !out_profile) return;

    macro_x = floor_div_local(wx, (int)impl->macro_cell_size);
    macro_z = floor_div_local(wz, (int)impl->macro_cell_size);
    block_x_in_macro = wx - macro_x * (int)impl->macro_cell_size;
    block_z_in_macro = wz - macro_z * (int)impl->macro_cell_size;
    if (block_x_in_macro < 0) block_x_in_macro += impl->macro_cell_size;
    if (block_z_in_macro < 0) block_z_in_macro += impl->macro_cell_size;

    origin_macro_x = tile->tile_x * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;
    origin_macro_z = tile->tile_z * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;
    local_macro_x = macro_x - origin_macro_x;
    local_macro_z = macro_z - origin_macro_z;

    tx = (float)block_x_in_macro / (float)impl->macro_cell_size;
    tz = (float)block_z_in_macro / (float)impl->macro_cell_size;
    w00 = (1.0f - tx) * (1.0f - tz);
    w10 = tx * (1.0f - tz);
    w01 = (1.0f - tx) * tz;
    w11 = tx * tz;
    ix = sdk_worldgen_clampi(local_macro_x, 0, SDK_WORLDGEN_TILE_STRIDE - 2);
    iz = sdk_worldgen_clampi(local_macro_z, 0, SDK_WORLDGEN_TILE_STRIDE - 2);

    c00 = macro_cell_at(tile, ix,     iz);
    c10 = macro_cell_at(tile, ix + 1, iz);
    c01 = macro_cell_at(tile, ix,     iz + 1);
    c11 = macro_cell_at(tile, ix + 1, iz + 1);

    terrain_province = weighted_vote4_u8(c00->terrain_province, c10->terrain_province,
                                         c01->terrain_province, c11->terrain_province,
                                         w00, w10, w01, w11, &province_conf);
    bedrock_province = weighted_vote4_u8(c00->bedrock_province, c10->bedrock_province,
                                         c01->bedrock_province, c11->bedrock_province,
                                         w00, w10, w01, w11, NULL);
    ecology = weighted_vote4_u8(c00->ecology, c10->ecology, c01->ecology, c11->ecology,
                                w00, w10, w01, w11, NULL);
    resource_province = weighted_vote4_u8(c00->resource_province, c10->resource_province,
                                          c01->resource_province, c11->resource_province,
                                          w00, w10, w01, w11, NULL);
    hydrocarbon_class = weighted_vote4_u8(c00->hydrocarbon_class, c10->hydrocarbon_class,
                                          c01->hydrocarbon_class, c11->hydrocarbon_class,
                                          w00, w10, w01, w11, NULL);
    temperature_band = weighted_vote4_u8(c00->temperature_band, c10->temperature_band,
                                         c01->temperature_band, c11->temperature_band,
                                         w00, w10, w01, w11, NULL);
    moisture_band = weighted_vote4_u8(c00->moisture_band, c10->moisture_band,
                                      c01->moisture_band, c11->moisture_band,
                                      w00, w10, w01, w11, NULL);
    surface_sediment = weighted_vote4_u8(c00->surface_sediment, c10->surface_sediment,
                                         c01->surface_sediment, c11->surface_sediment,
                                         w00, w10, w01, w11, NULL);
    parent_material = weighted_vote4_u8(c00->parent_material, c10->parent_material,
                                        c01->parent_material, c11->parent_material,
                                        w00, w10, w01, w11, NULL);
    water_surface_class = weighted_vote4_u8(c00->water_surface_class, c10->water_surface_class,
                                            c01->water_surface_class, c11->water_surface_class,
                                            w00, w10, w01, w11, NULL);

    detail_amp = bilerp4(sdk_worldgen_unpack_unorm8(c00->detail_amp), sdk_worldgen_unpack_unorm8(c10->detail_amp),
                         sdk_worldgen_unpack_unorm8(c01->detail_amp), sdk_worldgen_unpack_unorm8(c11->detail_amp),
                         tx, tz) * 22.0f;
    relief_strength = bilerp4(sdk_worldgen_unpack_unorm8(c00->relief_strength), sdk_worldgen_unpack_unorm8(c10->relief_strength),
                              sdk_worldgen_unpack_unorm8(c01->relief_strength), sdk_worldgen_unpack_unorm8(c11->relief_strength),
                              tx, tz);
    mountain_mask = bilerp4(sdk_worldgen_unpack_unorm8(c00->mountain_mask), sdk_worldgen_unpack_unorm8(c10->mountain_mask),
                            sdk_worldgen_unpack_unorm8(c01->mountain_mask), sdk_worldgen_unpack_unorm8(c11->mountain_mask),
                            tx, tz);
    river_strength = bilerp4(sdk_worldgen_unpack_unorm8(c00->river_strength), sdk_worldgen_unpack_unorm8(c10->river_strength),
                             sdk_worldgen_unpack_unorm8(c01->river_strength), sdk_worldgen_unpack_unorm8(c11->river_strength),
                             tx, tz);
    wetness = bilerp4(sdk_worldgen_unpack_unorm8(c00->wetness), sdk_worldgen_unpack_unorm8(c10->wetness),
                      sdk_worldgen_unpack_unorm8(c01->wetness), sdk_worldgen_unpack_unorm8(c11->wetness),
                      tx, tz);
    slope_f = bilerp4((float)c00->slope, (float)c10->slope, (float)c01->slope, (float)c11->slope, tx, tz);
    river_order_f = bilerp4((float)c00->river_order, (float)c10->river_order, (float)c01->river_order, (float)c11->river_order, tx, tz);
    floodplain_width_f = bilerp4((float)c00->floodplain_width, (float)c10->floodplain_width,
                                 (float)c01->floodplain_width, (float)c11->floodplain_width, tx, tz);
    soil_depth_f = bilerp4((float)c00->soil_depth, (float)c10->soil_depth, (float)c01->soil_depth, (float)c11->soil_depth, tx, tz);
    sediment_depth_f = bilerp4((float)c00->sediment_depth, (float)c10->sediment_depth,
                               (float)c01->sediment_depth, (float)c11->sediment_depth, tx, tz);
    drainage_index = bilerp4(drainage_index_for_class(c00->drainage_class), drainage_index_for_class(c10->drainage_class),
                             drainage_index_for_class(c01->drainage_class), drainage_index_for_class(c11->drainage_class),
                             tx, tz);
    soil_reaction_value = bilerp4(soil_reaction_value_for_class(c00->soil_reaction), soil_reaction_value_for_class(c10->soil_reaction),
                                  soil_reaction_value_for_class(c01->soil_reaction), soil_reaction_value_for_class(c11->soil_reaction),
                                  tx, tz);
    soil_fertility_value = bilerp4(soil_fertility_value_for_class(c00->soil_fertility), soil_fertility_value_for_class(c10->soil_fertility),
                                   soil_fertility_value_for_class(c01->soil_fertility), soil_fertility_value_for_class(c11->soil_fertility),
                                   tx, tz);
    soil_salinity_value = bilerp4(soil_salinity_value_for_class(c00->soil_salinity), soil_salinity_value_for_class(c10->soil_salinity),
                                  soil_salinity_value_for_class(c01->soil_salinity), soil_salinity_value_for_class(c11->soil_salinity),
                                  tx, tz);
    water_table_depth_f = bilerp4((float)c00->water_table_depth, (float)c10->water_table_depth,
                                  (float)c01->water_table_depth, (float)c11->water_table_depth, tx, tz);
    resource_grade_f = bilerp4((float)c00->resource_grade, (float)c10->resource_grade,
                               (float)c01->resource_grade, (float)c11->resource_grade, tx, tz);

    marine_weight = province_weight4(TERRAIN_PROVINCE_OPEN_OCEAN, c00, c10, c01, c11, w00, w10, w01, w11) +
                    province_weight4(TERRAIN_PROVINCE_CONTINENTAL_SHELF, c00, c10, c01, c11, w00, w10, w01, w11);
    lowland_weight = province_weight4(TERRAIN_PROVINCE_ESTUARY_DELTA, c00, c10, c01, c11, w00, w10, w01, w11) +
                     province_weight4(TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND, c00, c10, c01, c11, w00, w10, w01, w11) +
                     province_weight4(TERRAIN_PROVINCE_PEAT_WETLAND, c00, c10, c01, c11, w00, w10, w01, w11);
    hardrock_weight = family_weight4(SDK_WORLDGEN_PROVINCE_FAMILY_HARDROCK, c00, c10, c01, c11, w00, w10, w01, w11);
    volcanic_weight = family_weight4(SDK_WORLDGEN_PROVINCE_FAMILY_VOLCANIC, c00, c10, c01, c11, w00, w10, w01, w11);

    detail = sdk_worldgen_fbm((float)wx * 0.020f, (float)wz * 0.020f, impl->seed ^ 0x8811u, 3) * detail_amp;
    mountain_detail = 0.0f;
    if (mountain_mask > 0.05f || relief_strength > 0.28f || hardrock_weight > 0.35f || volcanic_weight > 0.35f) {
        detail_warp = sdk_worldgen_fbm((float)wx * 0.0057f, (float)wz * 0.0057f, impl->seed ^ 0x9711u, 2) * 1.35f;
        crest = sdk_worldgen_ridged((float)wx * 0.034f + detail_warp, (float)wz * 0.034f - detail_warp, impl->seed ^ 0x9912u, 3);
        breakup = sdk_worldgen_fbm((float)wx * 0.066f, (float)wz * 0.066f, impl->seed ^ 0xA513u, 2);
        erosion_noise = sdk_worldgen_fbm((float)wx * 0.043f, (float)wz * 0.043f, impl->seed ^ 0xB614u, 3);

        mountain_detail = ((crest - 0.52f) * detail_amp * 0.58f +
                           breakup * detail_amp * 0.22f) *
                          sdk_worldgen_clampf(mountain_mask * 0.70f + relief_strength * 0.30f, 0.0f, 1.0f);
        mountain_detail *= (0.55f + hardrock_weight * 0.35f + volcanic_weight * 0.20f);
        mountain_detail *= 1.0f - sdk_worldgen_clampf(river_strength * 0.60f + wetness * 0.20f, 0.0f, 0.75f);
        mountain_detail -= sdk_worldgen_clampf((erosion_noise + 1.0f) * 0.5f, 0.0f, 1.0f) *
                           relief_strength * river_strength * 6.0f;
        detail += mountain_detail;
    }

    base_relief_scale = 0.18f + relief_strength * 0.14f;
    base = (int)lrintf(bilerp4((float)c00->base_height, (float)c10->base_height,
                               (float)c01->base_height, (float)c11->base_height, tx, tz) +
                       detail * base_relief_scale);
    macro_surface = bilerp4((float)c00->surface_height, (float)c10->surface_height,
                            (float)c01->surface_height, (float)c11->surface_height, tx, tz);
    surface = (int)lrintf(macro_surface + detail);
    river_bed = (int)lrintf(bilerp4((float)c00->river_bed_height, (float)c10->river_bed_height,
                                    (float)c01->river_bed_height, (float)c11->river_bed_height, tx, tz));
    water_height = (int)lrintf(bilerp4((float)c00->water_height, (float)c10->water_height,
                                       (float)c01->water_height, (float)c11->water_height, tx, tz));

    if (river_strength > 0.06f && river_order_f > 0.20f) {
        blended_direction4(c00, c10, c01, c11, w00, w10, w01, w11, &dir_x, &dir_z);
        channel_dist = channel_distance_blocks_vec(block_x_in_macro, block_z_in_macro,
                                                   (int)impl->macro_cell_size, dir_x, dir_z);
        width_blocks = 1.6f + river_order_f * 2.25f + river_strength * 3.0f;
        if (channel_dist < width_blocks) {
            float t = 1.0f - sdk_worldgen_clampf(channel_dist / width_blocks, 0.0f, 1.0f);
            surface = (int)lrintf((float)surface * (1.0f - t) + (float)river_bed * t);
        }
        if (floodplain_width_f > 0.10f) {
            float flood_width = width_blocks + floodplain_width_f * 1.9f;
            if (channel_dist < flood_width) {
                float t = 1.0f - sdk_worldgen_clampf(channel_dist / flood_width, 0.0f, 1.0f);
                int flood_target = river_bed + 1 + (int)(river_order_f * 0.5f);
                if (surface > flood_target) {
                    surface = (int)lrintf((float)surface * (1.0f - t * 0.60f) + (float)flood_target * (t * 0.60f));
                }
            }
        }
    }

    river_flatten = river_strength * sdk_worldgen_clampf(1.0f - slope_f / 14.0f, 0.0f, 1.0f) * 0.55f;
    lowland_flatten = sdk_worldgen_clampf(lowland_weight * 0.30f + river_flatten, 0.0f, 0.45f);
    if (province_conf >= 0.60f &&
        (terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA ||
         terrain_province == TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND ||
         terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND)) {
        lowland_flatten = sdk_worldgen_clampf(lowland_flatten + 0.08f, 0.0f, 0.50f);
    }
    if (lowland_flatten > 0.01f) {
        surface = (int)lrintf((float)surface * (1.0f - lowland_flatten) + macro_surface * lowland_flatten);
    }

    surface = sdk_worldgen_clampi(surface, 4, CHUNK_HEIGHT - 8);
    base = sdk_worldgen_clampi(base, 3, surface - 1);
    river_bed = sdk_worldgen_clampi(river_bed, 3, surface);
    if (water_height > 0 && water_height <= surface && river_strength > 0.10f) {
        water_height = surface + 1;
    }
    if (province_conf >= 0.60f &&
        (terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN ||
         terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF)) {
        water_height = impl->sea_level;
    } else if (marine_weight > 0.15f) {
        int marine_height = (int)lrintf((float)water_height * (1.0f - marine_weight) +
                                        (float)impl->sea_level * marine_weight);
        if (marine_height > water_height) water_height = marine_height;
    }

    out_profile->base_height = (int16_t)base;
    out_profile->surface_height = (int16_t)surface;
    out_profile->water_height = (int16_t)water_height;
    out_profile->river_bed_height = (int16_t)river_bed;
    out_profile->soil_depth = (uint8_t)sdk_worldgen_clampi((int)lrintf(soil_depth_f), 0, 8);
    out_profile->sediment_depth = (uint8_t)sdk_worldgen_clampi((int)lrintf(sediment_depth_f), 0, 12);
    out_profile->sediment_thickness = out_profile->sediment_depth;
    out_profile->regolith_thickness = (uint8_t)sdk_worldgen_clampi((int)lrintf(2.0f + relief_strength * 4.0f + wetness * 2.0f), 0, 12);
    out_profile->river_order = (uint8_t)sdk_worldgen_clampi((int)lrintf(river_order_f), 0, 4);
    out_profile->floodplain_width = (uint8_t)sdk_worldgen_clampi((int)lrintf(floodplain_width_f), 0, 10);
    out_profile->terrain_province = (SdkTerrainProvince)terrain_province;
    out_profile->bedrock_province = (SdkBedrockProvince)bedrock_province;
    out_profile->temperature_band = (SdkTemperatureBand)temperature_band;
    out_profile->moisture_band = (SdkMoistureBand)moisture_band;
    out_profile->surface_sediment = (SdkSurfaceSediment)surface_sediment;
    out_profile->parent_material = (SdkParentMaterialClass)parent_material;
    out_profile->water_table_depth = (uint8_t)sdk_worldgen_clampi((int)lrintf(water_table_depth_f), 0, 15);
    drainage_class = classify_drainage_local(drainage_index, water_table_depth_f);
    soil_salinity = classify_soil_salinity_local(soil_salinity_value);
    soil_reaction = classify_soil_reaction_local(soil_reaction_value, soil_salinity_value);
    soil_fertility = classify_soil_fertility_local(soil_fertility_value);
    out_profile->drainage_class = (SdkDrainageClass)drainage_class;
    out_profile->soil_reaction = (SdkSoilReactionClass)soil_reaction;
    out_profile->soil_fertility = (SdkSoilFertilityClass)soil_fertility;
    out_profile->soil_salinity = (SdkSoilSalinityClass)soil_salinity;
    out_profile->water_surface_class = (SdkSurfaceWaterClass)water_surface_class;
    out_profile->landform_flags = landform_flags_for_state((float)surface,
                                                           (float)water_height,
                                                           river_strength,
                                                           width_blocks,
                                                           sdk_worldgen_clampf(remap_smooth(floodplain_width_f, 2.0f, 10.0f), 0.0f, 1.0f),
                                                           0.0f,
                                                           0.0f,
                                                           0.0f,
                                                           volcanic_weight * sdk_worldgen_clampf(mountain_mask, 0.0f, 1.0f),
                                                           0.0f,
                                                           0.0f,
                                                           0.0f);
    {
        uint8_t derived_ecology = coarse_ecology_for_state(surface, impl->sea_level,
                                                           terrain_province,
                                                           temperature_band,
                                                           moisture_band,
                                                           (uint8_t)out_profile->surface_sediment,
                                                           (uint8_t)out_profile->parent_material,
                                                           drainage_class,
                                                           soil_reaction,
                                                           soil_fertility,
                                                           soil_salinity,
                                                           out_profile->river_order,
                                                           0.0f,
                                                           river_strength,
                                                           wetness,
                                                           0.0f,
                                                           out_profile->landform_flags,
                                                           out_profile->water_table_depth);
        out_profile->ecology = (SdkBiomeEcology)((derived_ecology == ECOLOGY_BARREN && surface > impl->sea_level)
            ? ecology
            : derived_ecology);
    }
    out_profile->resource_province = (SdkResourceProvince)resource_province;
    out_profile->hydrocarbon_class = (SdkHydrocarbonClass)hydrocarbon_class;
    out_profile->resource_grade = (uint8_t)sdk_worldgen_clampi((int)lrintf(resource_grade_f), 0, 255);
    if (out_profile->resource_province != RESOURCE_PROVINCE_OIL_FIELD) {
        out_profile->hydrocarbon_class = SDK_HYDROCARBON_NONE;
    }
}

void sdk_worldgen_build_region_tile(SdkWorldGen* wg, SdkWorldGenRegionTile* tile)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    int origin_x;
    int origin_z;
    int sx;
    int sz;

    if (!wg || !impl || !tile) return;

    origin_x = tile->tile_x * SDK_WORLDGEN_REGION_TILE_BLOCKS - SDK_WORLDGEN_REGION_TILE_HALO_BLOCKS;
    origin_z = tile->tile_z * SDK_WORLDGEN_REGION_TILE_BLOCKS - SDK_WORLDGEN_REGION_TILE_HALO_BLOCKS;

    for (sz = 0; sz < SDK_WORLDGEN_REGION_STRIDE; ++sz) {
        for (sx = 0; sx < SDK_WORLDGEN_REGION_STRIDE; ++sx) {
            int wx = origin_x + sx * SDK_WORLDGEN_REGION_SAMPLE_SPACING;
            int wz = origin_z + sz * SDK_WORLDGEN_REGION_SAMPLE_SPACING;
            SdkRegionFieldSample* sample = &tile->samples[region_index(sx, sz)];
            SdkWorldGenMacroTile* macro_tile = sdk_worldgen_require_macro_tile(wg, wx, wz);
            MacroBlend blend;
            SdkTerrainColumnProfile macro_profile;
            float relief_strength;
            float detail_amp;
            float wetness;
            float river_strength;
            float river_order;
            float floodplain_width;
            float coast_signed_distance;
            float water_temp;
            float air_temp;
            float soil_depth;
            float sediment_thickness;
            float regolith_thickness;
            float ocean_depth;
            float floodplain_mask;
            float shelf_weight;
            float open_ocean_weight;
            float hardrock_weight;
            float volcanic_weight;
            float dir_x;
            float dir_z;
            float dir_len;
            float control;
            float coastal_noise;
            float shelf_noise;
            float slope_f;
            float shoreline_blend;
            float dip_angle;
            float dip_strength;
            float fault_angle;
            float fault_nx;
            float fault_nz;
            float fault_dist;
            float fault_center;
            float tectonic_factor;
            float strike_x;
            float strike_z;
            float strike_coord;
            float fold_coord;
            float fold_offset;
            float weathered_thickness;
            float upper_thickness;
            float lower_thickness;
            float structural_offset;
            float boundary_warp;
            float lens_warp;
            float basement_relief;
            uint8_t terrain_province;
            uint8_t bedrock_province;
            uint8_t ecology;
            uint8_t resource_province;
            uint8_t temperature_band;
            uint8_t moisture_band;
            uint8_t surface_sediment;
            uint8_t parent_material;
            uint8_t water_surface_class;
            uint8_t stratigraphy_province;
            float drainage_index;
            float soil_reaction_value;
            float soil_fertility_value;
            float soil_salinity_value;
            float soil_organic_value;
            float water_table_depth;
            float macro_lake_mask;
            float macro_closed_basin_mask;
            float macro_lake_level;

            memset(sample, 0, sizeof(*sample));
            if (!macro_tile) continue;

            macro_blend_init(macro_tile, wg, wx, wz, &blend);
            sdk_worldgen_sample_column_from_tile(macro_tile, wg, wx, wz, &macro_profile);

            terrain_province = weighted_vote4_u8(blend.c00->terrain_province, blend.c10->terrain_province,
                                                 blend.c01->terrain_province, blend.c11->terrain_province,
                                                 blend.w00, blend.w10, blend.w01, blend.w11, NULL);
            bedrock_province = weighted_vote4_u8(blend.c00->bedrock_province, blend.c10->bedrock_province,
                                                 blend.c01->bedrock_province, blend.c11->bedrock_province,
                                                 blend.w00, blend.w10, blend.w01, blend.w11, NULL);
            ecology = weighted_vote4_u8(blend.c00->ecology, blend.c10->ecology,
                                        blend.c01->ecology, blend.c11->ecology,
                                        blend.w00, blend.w10, blend.w01, blend.w11, NULL);
            resource_province = weighted_vote4_u8(blend.c00->resource_province, blend.c10->resource_province,
                                                  blend.c01->resource_province, blend.c11->resource_province,
                                                  blend.w00, blend.w10, blend.w01, blend.w11, NULL);
            temperature_band = weighted_vote4_u8(blend.c00->temperature_band, blend.c10->temperature_band,
                                                 blend.c01->temperature_band, blend.c11->temperature_band,
                                                 blend.w00, blend.w10, blend.w01, blend.w11, NULL);
            moisture_band = weighted_vote4_u8(blend.c00->moisture_band, blend.c10->moisture_band,
                                              blend.c01->moisture_band, blend.c11->moisture_band,
                                              blend.w00, blend.w10, blend.w01, blend.w11, NULL);
            surface_sediment = weighted_vote4_u8(blend.c00->surface_sediment, blend.c10->surface_sediment,
                                                 blend.c01->surface_sediment, blend.c11->surface_sediment,
                                                 blend.w00, blend.w10, blend.w01, blend.w11, NULL);
            parent_material = weighted_vote4_u8(blend.c00->parent_material, blend.c10->parent_material,
                                                blend.c01->parent_material, blend.c11->parent_material,
                                                blend.w00, blend.w10, blend.w01, blend.w11, NULL);
            water_surface_class = weighted_vote4_u8(blend.c00->water_surface_class, blend.c10->water_surface_class,
                                                    blend.c01->water_surface_class, blend.c11->water_surface_class,
                                                    blend.w00, blend.w10, blend.w01, blend.w11, NULL);
            stratigraphy_province = weighted_vote4_u8(blend.c00->stratigraphy_province, blend.c10->stratigraphy_province,
                                                      blend.c01->stratigraphy_province, blend.c11->stratigraphy_province,
                                                      blend.w00, blend.w10, blend.w01, blend.w11, NULL);

            relief_strength = bilerp4(sdk_worldgen_unpack_unorm8(blend.c00->relief_strength), sdk_worldgen_unpack_unorm8(blend.c10->relief_strength),
                                      sdk_worldgen_unpack_unorm8(blend.c01->relief_strength), sdk_worldgen_unpack_unorm8(blend.c11->relief_strength),
                                      blend.tx, blend.tz);
            detail_amp = bilerp4(sdk_worldgen_unpack_unorm8(blend.c00->detail_amp), sdk_worldgen_unpack_unorm8(blend.c10->detail_amp),
                                 sdk_worldgen_unpack_unorm8(blend.c01->detail_amp), sdk_worldgen_unpack_unorm8(blend.c11->detail_amp),
                                 blend.tx, blend.tz) * 22.0f;
            wetness = bilerp4(sdk_worldgen_unpack_unorm8(blend.c00->wetness), sdk_worldgen_unpack_unorm8(blend.c10->wetness),
                              sdk_worldgen_unpack_unorm8(blend.c01->wetness), sdk_worldgen_unpack_unorm8(blend.c11->wetness),
                              blend.tx, blend.tz);
            river_strength = bilerp4(sdk_worldgen_unpack_unorm8(blend.c00->river_strength), sdk_worldgen_unpack_unorm8(blend.c10->river_strength),
                                     sdk_worldgen_unpack_unorm8(blend.c01->river_strength), sdk_worldgen_unpack_unorm8(blend.c11->river_strength),
                                     blend.tx, blend.tz);
            river_order = bilerp4((float)blend.c00->river_order, (float)blend.c10->river_order,
                                  (float)blend.c01->river_order, (float)blend.c11->river_order,
                                  blend.tx, blend.tz);
            floodplain_width = bilerp4((float)blend.c00->floodplain_width, (float)blend.c10->floodplain_width,
                                       (float)blend.c01->floodplain_width, (float)blend.c11->floodplain_width,
                                       blend.tx, blend.tz);
            slope_f = bilerp4((float)blend.c00->slope, (float)blend.c10->slope,
                              (float)blend.c01->slope, (float)blend.c11->slope,
                              blend.tx, blend.tz);
            coast_signed_distance = bilerp4((float)blend.c00->coast_distance, (float)blend.c10->coast_distance,
                                            (float)blend.c01->coast_distance, (float)blend.c11->coast_distance,
                                            blend.tx, blend.tz) * (float)impl->macro_cell_size;
            water_temp = bilerp4(sdk_worldgen_unpack_unorm8(blend.c00->water_temp), sdk_worldgen_unpack_unorm8(blend.c10->water_temp),
                                 sdk_worldgen_unpack_unorm8(blend.c01->water_temp), sdk_worldgen_unpack_unorm8(blend.c11->water_temp),
                                 blend.tx, blend.tz);
            air_temp = bilerp4(temperature_band_midpoint(blend.c00->temperature_band), temperature_band_midpoint(blend.c10->temperature_band),
                               temperature_band_midpoint(blend.c01->temperature_band), temperature_band_midpoint(blend.c11->temperature_band),
                               blend.tx, blend.tz);
            soil_depth = bilerp4((float)blend.c00->soil_depth, (float)blend.c10->soil_depth,
                                 (float)blend.c01->soil_depth, (float)blend.c11->soil_depth,
                                 blend.tx, blend.tz);
            drainage_index = bilerp4(drainage_index_for_class(blend.c00->drainage_class), drainage_index_for_class(blend.c10->drainage_class),
                                     drainage_index_for_class(blend.c01->drainage_class), drainage_index_for_class(blend.c11->drainage_class),
                                     blend.tx, blend.tz);
            soil_reaction_value = bilerp4(soil_reaction_value_for_class(blend.c00->soil_reaction), soil_reaction_value_for_class(blend.c10->soil_reaction),
                                          soil_reaction_value_for_class(blend.c01->soil_reaction), soil_reaction_value_for_class(blend.c11->soil_reaction),
                                          blend.tx, blend.tz);
            soil_fertility_value = bilerp4(soil_fertility_value_for_class(blend.c00->soil_fertility), soil_fertility_value_for_class(blend.c10->soil_fertility),
                                           soil_fertility_value_for_class(blend.c01->soil_fertility), soil_fertility_value_for_class(blend.c11->soil_fertility),
                                           blend.tx, blend.tz);
            soil_salinity_value = bilerp4(soil_salinity_value_for_class(blend.c00->soil_salinity), soil_salinity_value_for_class(blend.c10->soil_salinity),
                                          soil_salinity_value_for_class(blend.c01->soil_salinity), soil_salinity_value_for_class(blend.c11->soil_salinity),
                                          blend.tx, blend.tz);
            soil_organic_value = bilerp4(soil_organic_value_for_state(blend.c00->surface_sediment, blend.c00->parent_material,
                                                                      blend.c00->terrain_province, sdk_worldgen_unpack_unorm8(blend.c00->wetness)),
                                         soil_organic_value_for_state(blend.c10->surface_sediment, blend.c10->parent_material,
                                                                      blend.c10->terrain_province, sdk_worldgen_unpack_unorm8(blend.c10->wetness)),
                                         soil_organic_value_for_state(blend.c01->surface_sediment, blend.c01->parent_material,
                                                                      blend.c01->terrain_province, sdk_worldgen_unpack_unorm8(blend.c01->wetness)),
                                         soil_organic_value_for_state(blend.c11->surface_sediment, blend.c11->parent_material,
                                                                      blend.c11->terrain_province, sdk_worldgen_unpack_unorm8(blend.c11->wetness)),
                                         blend.tx, blend.tz);
            water_table_depth = bilerp4((float)blend.c00->water_table_depth, (float)blend.c10->water_table_depth,
                                        (float)blend.c01->water_table_depth, (float)blend.c11->water_table_depth,
                                        blend.tx, blend.tz);
            macro_lake_mask = bilerp4(sdk_worldgen_unpack_unorm8(blend.c00->lake_mask), sdk_worldgen_unpack_unorm8(blend.c10->lake_mask),
                                      sdk_worldgen_unpack_unorm8(blend.c01->lake_mask), sdk_worldgen_unpack_unorm8(blend.c11->lake_mask),
                                      blend.tx, blend.tz);
            macro_closed_basin_mask = bilerp4(sdk_worldgen_unpack_unorm8(blend.c00->closed_basin_mask), sdk_worldgen_unpack_unorm8(blend.c10->closed_basin_mask),
                                              sdk_worldgen_unpack_unorm8(blend.c01->closed_basin_mask), sdk_worldgen_unpack_unorm8(blend.c11->closed_basin_mask),
                                              blend.tx, blend.tz);
            macro_lake_level = bilerp4((float)blend.c00->lake_level, (float)blend.c10->lake_level,
                                       (float)blend.c01->lake_level, (float)blend.c11->lake_level,
                                       blend.tx, blend.tz);
            open_ocean_weight = province_weight4(TERRAIN_PROVINCE_OPEN_OCEAN, blend.c00, blend.c10, blend.c01, blend.c11,
                                                 blend.w00, blend.w10, blend.w01, blend.w11);
            shelf_weight = province_weight4(TERRAIN_PROVINCE_CONTINENTAL_SHELF, blend.c00, blend.c10, blend.c01, blend.c11,
                                            blend.w00, blend.w10, blend.w01, blend.w11);
            hardrock_weight = family_weight4(SDK_WORLDGEN_PROVINCE_FAMILY_HARDROCK, blend.c00, blend.c10, blend.c01, blend.c11,
                                             blend.w00, blend.w10, blend.w01, blend.w11);
            volcanic_weight = family_weight4(SDK_WORLDGEN_PROVINCE_FAMILY_VOLCANIC, blend.c00, blend.c10, blend.c01, blend.c11,
                                             blend.w00, blend.w10, blend.w01, blend.w11);

            blended_direction4(blend.c00, blend.c10, blend.c01, blend.c11,
                               blend.w00, blend.w10, blend.w01, blend.w11,
                               &dir_x, &dir_z);
            dir_len = sqrtf(dir_x * dir_x + dir_z * dir_z);
            if (dir_len > 0.0001f) {
                dir_x /= dir_len;
                dir_z /= dir_len;
            } else {
                dir_x = 1.0f;
                dir_z = 0.0f;
            }

            control = sdk_worldgen_clampf((sdk_worldgen_fbm((float)wx * 0.0046f, (float)wz * 0.0046f, impl->seed ^ 0xD151u, 3) + 1.0f) * 0.5f, 0.0f, 1.0f);
            coastal_noise = sdk_worldgen_fbm((float)wx * 0.0090f, (float)wz * 0.0090f, impl->seed ^ 0xD252u, 2) * 7.0f;
            shelf_noise = sdk_worldgen_fbm((float)wx * 0.0064f, (float)wz * 0.0064f, impl->seed ^ 0xD353u, 2) * 10.0f;
            floodplain_mask = sdk_worldgen_clampf(river_strength * sdk_worldgen_clampf(1.0f - slope_f / 14.0f, 0.0f, 1.0f) +
                                                  remap_smooth(floodplain_width, 2.0f, 10.0f) * 0.40f,
                                                  0.0f, 1.0f);
            {
                float continentality = sdk_worldgen_clampf((fabsf(coast_signed_distance) - 24.0f) / 320.0f, 0.0f, 1.0f);
                float coastal_humidity = sdk_worldgen_clampf(1.0f - fabsf(coast_signed_distance) / 140.0f, 0.0f, 1.0f);
                float rain_shadow = sdk_worldgen_clampf((hardrock_weight * 0.20f + relief_strength * 0.24f + slope_f / 42.0f) *
                                                        continentality, 0.0f, 0.32f);
                float floodplain_bonus = floodplain_mask * 0.24f + river_strength * 0.10f;
                float cold_air_pooling = (temperature_band <= TEMP_COOL_TEMPERATE)
                    ? sdk_worldgen_clampf((1.0f - slope_f / 12.0f) * continentality * 0.16f, 0.0f, 0.16f)
                    : 0.0f;
                wetness = sdk_worldgen_clampf(wetness + coastal_humidity * 0.10f + floodplain_bonus - rain_shadow + cold_air_pooling, 0.0f, 1.0f);
                air_temp = sdk_worldgen_clampf(air_temp - hardrock_weight * 0.03f - cold_air_pooling * 0.45f +
                                               (coast_signed_distance < 0.0f ? 0.02f : 0.0f), 0.0f, 1.0f);
                temperature_band = classify_temperature(air_temp);
                moisture_band = classify_moisture(wetness);
            }

            sample->base_height = (float)macro_profile.base_height;
            sample->surface_base = (float)macro_profile.surface_height;
            sample->water_level = (float)macro_profile.water_height;
            sample->river_bed_height = (float)macro_profile.river_bed_height;
            sample->surface_relief = sdk_worldgen_clampf(1.5f + detail_amp * 0.35f + relief_strength * 12.0f, 0.8f, 20.0f);
            sample->coast_signed_distance = coast_signed_distance;
            sample->river_strength = river_strength;
            sample->floodplain_mask = floodplain_mask;
            sample->river_order = river_order;
            sample->floodplain_width = floodplain_width;
            sample->flow_dir_x = dir_x;
            sample->flow_dir_z = dir_z;
            sample->wetness = wetness;
            sample->air_temp = air_temp;
            sample->water_temp = water_temp;
            sample->soil_depth = sdk_worldgen_clampf(soil_depth, 0.0f, 6.0f);
            sample->drainage_index = drainage_index;
            sample->soil_reaction_value = soil_reaction_value;
            sample->soil_fertility_value = soil_fertility_value;
            sample->soil_salinity_value = soil_salinity_value;
            sample->soil_organic_value = soil_organic_value;
            sample->water_table_depth = water_table_depth;
            sample->stratigraphy_control = control;
            sample->terrain_province = terrain_province;
            sample->bedrock_province = bedrock_province;
            sample->temperature_band = temperature_band;
            sample->moisture_band = moisture_band;
            sample->surface_sediment = surface_sediment;
            sample->parent_material = parent_material;
            sample->resource_province = resource_province;
            sample->water_surface_class = water_surface_class;
            sample->stratigraphy_province = stratigraphy_province;
            sample->trap_strength = sdk_worldgen_clampf(sample->fault_mask * 0.30f +
                                                        sample->basin_axis_weight * 0.25f +
                                                        sample->stratigraphy_control * 0.20f +
                                                        sample->evaporite_bias * 0.10f +
                                                        sample->closed_basin_mask * 0.15f,
                                                        0.0f, 1.0f);
            sample->seal_quality = sdk_worldgen_clampf((1.0f - sample->channel_sand_bias) * 0.45f +
                                                       sample->evaporite_bias * 0.30f +
                                                       sample->basin_axis_weight * 0.15f +
                                                       sample->wetness * 0.10f,
                                                       0.0f, 1.0f);
            sample->gas_bias = sdk_worldgen_clampf((1.0f - sample->wetness) * 0.25f +
                                                   sample->fault_mask * 0.20f +
                                                   sample->evaporite_bias * 0.10f +
                                                   sample->stratigraphy_control * 0.15f +
                                                   (sample->bedrock_province == BEDROCK_PROVINCE_RIFT_SEDIMENTARY ? 0.18f : 0.0f),
                                                   0.0f, 1.0f);
            sample->source_richness = sdk_worldgen_clampf(sample->basin_axis_weight * 0.45f +
                                                          (1.0f - sample->channel_sand_bias) * 0.20f +
                                                          sample->stratigraphy_control * 0.15f +
                                                          sample->wetness * 0.10f +
                                                          ((sample->bedrock_province == BEDROCK_PROVINCE_RIFT_SEDIMENTARY ||
                                                            sample->bedrock_province == BEDROCK_PROVINCE_SILICICLASTIC_BASIN) ? 0.10f : 0.0f),
                                                          0.0f, 1.0f);
            sample->hydrocarbon_class = (uint8_t)macro_profile.hydrocarbon_class;
            sample->resource_grade = macro_profile.resource_grade;
            if (sample->resource_province == RESOURCE_PROVINCE_OIL_FIELD) {
                if (sample->trap_strength > 0.58f &&
                    sample->seal_quality > 0.54f &&
                    sample->channel_sand_bias > 0.55f) {
                    sample->hydrocarbon_class = (uint8_t)((sample->gas_bias > 0.60f)
                        ? SDK_HYDROCARBON_NATURAL_GAS
                        : SDK_HYDROCARBON_CRUDE_OIL);
                } else if (sample->channel_sand_bias > 0.60f) {
                    sample->hydrocarbon_class = SDK_HYDROCARBON_OIL_SAND;
                } else {
                    sample->hydrocarbon_class = SDK_HYDROCARBON_OIL_SHALE;
                }
                sample->resource_grade = sdk_worldgen_pack_unorm8(sdk_worldgen_clampf(sample->source_richness * 0.40f +
                                                                                      sample->trap_strength * 0.30f +
                                                                                      sample->seal_quality * 0.20f +
                                                                                      sample->gas_bias * 0.10f,
                                                                                      0.0f, 1.0f));
            } else {
                sample->hydrocarbon_class = SDK_HYDROCARBON_NONE;
                sample->resource_grade = macro_profile.resource_grade;
            }

            if (sample->surface_base <= (float)impl->sea_level || coast_signed_distance < 0.0f) {
                float target_depth = (open_ocean_weight > 0.40f)
                    ? (42.0f + remap_smooth(-coast_signed_distance, 0.0f, 1024.0f) * 132.0f + shelf_noise)
                    : (8.0f + remap_smooth(-coast_signed_distance, 0.0f, 320.0f) * 46.0f + shelf_noise * 0.45f);
                ocean_depth = fmaxf((float)impl->sea_level - sample->surface_base, target_depth);
                ocean_depth = sdk_worldgen_clampf(ocean_depth, 4.0f, 240.0f);
                sample->ocean_depth = ocean_depth;
                sample->surface_base = (float)impl->sea_level - ocean_depth;
                sample->water_level = (float)impl->sea_level;
                sample->river_bed_height = sample->surface_base;
                sample->surface_relief = sdk_worldgen_clampf(0.6f + open_ocean_weight * 1.5f + shelf_weight * 1.0f, 0.4f, 4.0f);
                sample->sediment_thickness = 5.0f + control * 4.0f + shelf_weight * 3.0f + open_ocean_weight * 4.0f;
                sample->regolith_thickness = 0.0f;
                sample->soil_depth = 0.0f;
                sample->water_table_depth = 0.0f;
                sample->drainage_index = fminf(sample->drainage_index, 0.10f);
                sample->soil_salinity_value = fmaxf(sample->soil_salinity_value, 0.76f);
                sample->soil_reaction_value = fmaxf(sample->soil_reaction_value, 0.68f);
                sample->soil_organic_value *= 0.35f;
                sample->terrain_province = (ocean_depth > 36.0f) ? TERRAIN_PROVINCE_OPEN_OCEAN : TERRAIN_PROVINCE_CONTINENTAL_SHELF;
            } else {
                shoreline_blend = 1.0f - sdk_worldgen_clampf(coast_signed_distance / 96.0f, 0.0f, 1.0f);
                if (shoreline_blend > 0.0f) {
                    float shore_target = (float)impl->sea_level + coast_signed_distance * 0.18f + coastal_noise - wetness * 2.0f;
                    sample->surface_base = sample->surface_base * (1.0f - shoreline_blend * 0.45f) + shore_target * (shoreline_blend * 0.45f);
                    sample->surface_relief *= (1.0f - shoreline_blend * 0.45f);
                }

                if (floodplain_mask > 0.05f) {
                    float river_target = sample->river_bed_height + 1.0f + river_order * 0.35f;
                    sample->surface_base = sample->surface_base * (1.0f - floodplain_mask * 0.18f) + river_target * (floodplain_mask * 0.18f);
                    sample->surface_relief *= (1.0f - floodplain_mask * 0.60f);
                    sample->water_table_depth = fminf(sample->water_table_depth, 3.0f);
                    sample->drainage_index = fminf(sample->drainage_index, 0.40f);
                }

                switch ((SdkTerrainProvince)sample->terrain_province) {
                    case TERRAIN_PROVINCE_ESTUARY_DELTA:
                    case TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND:
                        sediment_thickness = 8.0f + river_strength * 8.0f + control * 2.0f;
                        regolith_thickness = 2.0f + wetness * 3.0f;
                        break;
                    case TERRAIN_PROVINCE_PEAT_WETLAND:
                        sediment_thickness = 7.0f + wetness * 5.0f;
                        regolith_thickness = 3.0f + wetness * 2.0f;
                        break;
                    case TERRAIN_PROVINCE_DYNAMIC_COAST:
                        sediment_thickness = 3.0f + shoreline_blend * 3.0f + control;
                        regolith_thickness = 1.0f + wetness;
                        break;
                    case TERRAIN_PROVINCE_ARID_FAN_STEPPE:
                        sediment_thickness = 5.0f + control * 4.0f;
                        regolith_thickness = 2.0f + (1.0f - wetness) * 2.0f;
                        break;
                    default:
                        if (hardrock_weight > 0.45f) {
                            sediment_thickness = 1.0f + control * 2.0f;
                            regolith_thickness = 2.0f + wetness * 2.0f;
                        } else if (volcanic_weight > 0.45f) {
                            sediment_thickness = 2.0f + control * 2.0f;
                            regolith_thickness = 2.0f + wetness * 1.5f;
                        } else {
                            sediment_thickness = 3.0f + wetness * 3.0f + control * 2.0f;
                            regolith_thickness = 3.0f + wetness * 3.0f;
                        }
                        break;
                }

                sample->ocean_depth = 0.0f;
                sample->sediment_thickness = sdk_worldgen_clampf(sediment_thickness, 1.0f, 18.0f);
                sample->regolith_thickness = sdk_worldgen_clampf(regolith_thickness - sdk_worldgen_clampf(slope_f / 16.0f, 0.0f, 2.0f), 0.0f, 12.0f);
                if (sample->surface_base < (float)impl->sea_level - 1.0f && coast_signed_distance < 24.0f) {
                    sample->water_level = (float)impl->sea_level;
                    sample->water_surface_class = SURFACE_WATER_OPEN;
                    sample->terrain_province = TERRAIN_PROVINCE_CONTINENTAL_SHELF;
                }
            }

            sample->base_height = sdk_worldgen_clampf(sample->base_height, 3.0f, (float)CHUNK_HEIGHT - 16.0f);
            sample->surface_base = sdk_worldgen_clampf(sample->surface_base, 4.0f, (float)CHUNK_HEIGHT - 8.0f);
            if (sample->base_height >= sample->surface_base) {
                sample->base_height = sample->surface_base - 1.0f;
            }

            dip_angle = sdk_worldgen_fbm((float)wx * 0.00085f, (float)wz * 0.00085f, impl->seed ^ 0xD771u, 2) * 3.14159265f;
            strike_x = cosf(dip_angle);
            strike_z = sinf(dip_angle);
            dip_strength = 0.04f + sample->stratigraphy_control * 0.06f +
                           sample->basin_axis_weight * 0.05f +
                           hardrock_weight * 0.04f + volcanic_weight * 0.03f;
            if (sample->stratigraphy_province == SDK_STRAT_PROVINCE_HARDROCK_BASEMENT) dip_strength *= 0.7f;
            if (sample->stratigraphy_province == SDK_STRAT_PROVINCE_OCEANIC) dip_strength *= 0.45f;
            sample->dip_x = strike_x * dip_strength;
            sample->dip_z = strike_z * dip_strength;

            fault_angle = sdk_worldgen_fbm((float)wx * 0.00062f, (float)wz * 0.00062f, impl->seed ^ 0xD882u, 2) * 3.14159265f;
            fault_nx = -sinf(fault_angle);
            fault_nz = cosf(fault_angle);
            fault_dist = (((float)wx * fault_nx) + ((float)wz * fault_nz)) * 0.0105f;
            fault_dist += sdk_worldgen_fbm((float)wx * 0.00165f, (float)wz * 0.00165f, impl->seed ^ 0xD983u, 2) * 0.55f;
            fault_center = sdk_worldgen_value_noise((float)wx * 0.00042f, (float)wz * 0.00042f, impl->seed ^ 0xDA84u) * 0.80f;
            tectonic_factor = sdk_worldgen_clampf(0.18f + hardrock_weight * 0.38f + volcanic_weight * 0.34f +
                                                  sample->basin_axis_weight * 0.22f, 0.0f, 1.0f);
            sample->fault_mask = geology_band_weight(fault_dist - fault_center, 0.0f, 0.18f) * tectonic_factor;
            sample->fault_throw = ((fault_dist >= fault_center) ? 1.0f : -1.0f) *
                                  (4.0f + tectonic_factor * 12.0f) * sample->fault_mask;

            strike_coord = (((float)wx * strike_x) + ((float)wz * strike_z)) * 0.0085f;
            fold_coord = (((float)wx * -strike_z) + ((float)wz * strike_x)) * 0.0055f;
            fold_offset = 0.0f;
            if (hardrock_weight > 0.35f || sample->terrain_province == TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT) {
                fold_offset = sinf(fold_coord + sample->stratigraphy_control * 6.2831853f) * (3.0f + hardrock_weight * 5.0f);
            }
            structural_offset = strike_coord * dip_strength * 8.0f + fold_offset;

            sample->basin_axis_weight = sdk_worldgen_clampf(0.5f + 0.5f *
                sdk_worldgen_fbm(((float)wx * dir_x + (float)wz * dir_z) * 0.0042f,
                                 ((float)wx * -dir_z + (float)wz * dir_x) * 0.0024f,
                                 impl->seed ^ 0xDB95u, 2), 0.0f, 1.0f);
            if (sample->stratigraphy_province == SDK_STRAT_PROVINCE_RIFT_BASIN ||
                sample->stratigraphy_province == SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN) {
                sample->basin_axis_weight = sdk_worldgen_clampf(sample->basin_axis_weight * 0.70f + wetness * 0.20f + river_strength * 0.10f, 0.0f, 1.0f);
            } else if (sample->stratigraphy_province == SDK_STRAT_PROVINCE_CARBONATE_SHELF) {
                sample->basin_axis_weight = sdk_worldgen_clampf(sample->basin_axis_weight * 0.45f + shelf_weight * 0.40f, 0.0f, 1.0f);
            } else {
                sample->basin_axis_weight = sdk_worldgen_clampf(sample->basin_axis_weight * 0.35f, 0.0f, 1.0f);
            }

            sample->channel_sand_bias = sdk_worldgen_clampf(0.5f + 0.5f *
                sdk_worldgen_fbm(((float)wx * dir_x + (float)wz * dir_z) * 0.013f,
                                 ((float)wx * -dir_z + (float)wz * dir_x) * 0.0048f,
                                 impl->seed ^ 0xDCA6u, 2), 0.0f, 1.0f);
            sample->channel_sand_bias = sdk_worldgen_clampf(sample->channel_sand_bias * 0.65f + river_strength * 0.25f +
                                                            sample->basin_axis_weight * 0.10f, 0.0f, 1.0f);

            sample->carbonate_purity = sdk_worldgen_clampf(0.5f + 0.5f *
                sdk_worldgen_fbm((float)wx * 0.0054f, (float)wz * 0.0054f, impl->seed ^ 0xDDB7u, 2), 0.0f, 1.0f);
            if (sample->stratigraphy_province == SDK_STRAT_PROVINCE_CARBONATE_SHELF) {
                sample->carbonate_purity = sdk_worldgen_clampf(sample->carbonate_purity * 0.60f +
                                                               shelf_weight * 0.25f +
                                                               (1.0f - river_strength) * 0.15f, 0.0f, 1.0f);
            } else {
                sample->carbonate_purity *= 0.20f;
            }

            sample->vent_bias = sdk_worldgen_clampf(0.5f + 0.5f *
                sdk_worldgen_fbm((float)wx * 0.0046f + strike_x * 0.7f, (float)wz * 0.0046f + strike_z * 0.7f,
                                 impl->seed ^ 0xDEC8u, 2), 0.0f, 1.0f);
            sample->vent_bias *= sdk_worldgen_clampf(volcanic_weight * 1.35f + (sample->stratigraphy_province == SDK_STRAT_PROVINCE_FLOOD_BASALT ? 0.25f : 0.0f),
                                                     0.0f, 1.0f);

            sample->evaporite_bias = sdk_worldgen_clampf((1.0f - wetness) * 0.45f +
                                                         (sample->terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE ? 0.35f : 0.0f) +
                                                         (sample->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST ? 0.20f : 0.0f) +
                                                         (sample->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA ? 0.10f : 0.0f) +
                                                         sdk_worldgen_clampf(0.5f + 0.5f *
                                                            sdk_worldgen_fbm((float)wx * 0.0061f, (float)wz * 0.0061f, impl->seed ^ 0xDFD9u, 2),
                                                            0.0f, 1.0f) * 0.20f, 0.0f, 1.0f);
            sample->vent_mask = sdk_worldgen_clampf(sample->vent_bias *
                                                    (0.45f + volcanic_weight * 0.30f +
                                                     (sample->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ? 0.25f : 0.0f) +
                                                     (sample->terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU ? 0.12f : 0.0f)),
                                                    0.0f, 1.0f);
            sample->vent_distance = 1.0f - sample->vent_mask;
            sample->caldera_mask = sdk_worldgen_clampf(sample->vent_mask * (sample->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ? 0.85f : 0.55f) +
                                                       sample->fault_mask * 0.10f -
                                                       sdk_worldgen_clampf(sample->surface_relief / 18.0f, 0.0f, 1.0f) * 0.16f,
                                                       0.0f, 1.0f);
            {
                float edge_noise = sdk_worldgen_clampf(0.5f + 0.5f *
                    sdk_worldgen_fbm((float)wx * 0.0038f + dir_x * 0.7f,
                                     (float)wz * 0.0038f + dir_z * 0.7f,
                                     impl->seed ^ 0xE0EAu, 2), 0.0f, 1.0f);
                float shoreline_soften = sdk_worldgen_clampf(1.0f - slope_f / 16.0f, 0.0f, 1.0f);
                float depression_soften = remap_smooth(edge_noise, 0.48f, 0.92f);

                sample->lake_mask = sdk_worldgen_clampf(macro_lake_mask * (0.82f + depression_soften * 0.18f), 0.0f, 1.0f);
                sample->closed_basin_mask = sdk_worldgen_clampf(macro_closed_basin_mask * (0.80f + shoreline_soften * 0.20f), 0.0f, 1.0f);
                sample->lake_level = (macro_lake_mask > 0.02f)
                    ? fmaxf(macro_lake_level, sample->surface_base)
                    : sample->surface_base;
                if (sample->water_level > sample->surface_base && macro_lake_mask > 0.02f) {
                    sample->lake_mask = fmaxf(sample->lake_mask, macro_lake_mask * 0.65f);
                    sample->lake_level = fmaxf(sample->lake_level, sample->water_level);
                }
            }
            sample->ravine_mask = sdk_worldgen_clampf(river_strength *
                                                      sdk_worldgen_clampf((slope_f - 5.0f) / 10.0f, 0.0f, 1.0f) *
                                                      (0.45f + hardrock_weight * 0.55f),
                                                      0.0f, 1.0f);
            sample->ravine_depth = sample->ravine_mask * (6.0f + slope_f * 0.60f + river_order * 2.0f);
            sample->ravine_width = sample->ravine_mask * (4.0f + (1.0f - hardrock_weight) * 8.0f + river_order * 1.5f);
            sample->lava_flow_bias = sdk_worldgen_clampf(sample->vent_mask * 0.54f +
                                                         sample->fault_mask * 0.14f +
                                                         sdk_worldgen_clampf(slope_f / 18.0f, 0.0f, 1.0f) * 0.18f,
                                                         0.0f, 1.0f);
            sample->ash_bias = sdk_worldgen_clampf(sample->vent_mask * 0.42f +
                                                   sample->fault_mask * 0.18f + wetness * 0.10f,
                                                   0.0f, 1.0f);
            sample->river_channel_width = sdk_worldgen_clampf(1.2f + river_order * 1.55f + river_strength * 4.0f +
                                                              floodplain_mask * 2.0f,
                                                              0.0f, 14.0f);
            sample->braid_mask = sdk_worldgen_clampf((sample->terrain_province == TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND ||
                                                      sample->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA ||
                                                      sample->terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE ? 0.18f : 0.0f) +
                                                     river_strength * 0.55f +
                                                     remap_smooth(sample->river_channel_width, 5.0f, 11.0f) * 0.25f -
                                                     sdk_worldgen_clampf(slope_f / 18.0f, 0.0f, 0.35f) -
                                                     hardrock_weight * 0.22f,
                                                     0.0f, 1.0f);
            sample->river_channel_depth = sdk_worldgen_clampf(1.0f + river_strength * 5.2f + river_order * 0.85f +
                                                              hardrock_weight * 1.4f - sample->braid_mask * 1.1f,
                                                              0.6f, 10.0f);
            sample->valley_mask = sdk_worldgen_clampf(river_strength * 0.52f + slope_f / 34.0f +
                                                      hardrock_weight * 0.12f +
                                                      remap_smooth(floodplain_width, 2.0f, 8.0f) * 0.18f,
                                                      0.0f, 1.0f);
            sample->waterfall_mask = sdk_worldgen_clampf(river_strength *
                                                         sdk_worldgen_clampf((slope_f - 10.0f) / 12.0f, 0.0f, 1.0f) *
                                                         (0.30f + hardrock_weight * 0.70f),
                                                         0.0f, 1.0f);
            sample->stratovolcano_mask = sdk_worldgen_clampf(sample->vent_bias *
                                                             volcanic_weight *
                                                             (0.35f + relief_strength * 0.65f) *
                                                             (sample->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ? 1.0f : 0.35f),
                                                             0.0f, 1.0f);
            sample->shield_mask = sdk_worldgen_clampf(sample->vent_bias *
                                                      (sample->terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU ? 0.82f : 0.24f) *
                                                      (0.70f + volcanic_weight * 0.30f),
                                                      0.0f, 1.0f);
            sample->fissure_mask = sdk_worldgen_clampf(sample->vent_bias * 0.40f + sample->fault_mask * 0.40f +
                                                       (sample->terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU ? 0.24f : 0.0f),
                                                       0.0f, 1.0f);
            sample->ashfall_mask = sdk_worldgen_clampf(sample->ash_bias * 0.65f + sample->stratovolcano_mask * 0.30f +
                                                       wetness * 0.10f,
                                                       0.0f, 1.0f);
            sample->karst_mask = sdk_worldgen_clampf((sample->terrain_province == TERRAIN_PROVINCE_CARBONATE_UPLAND ? 0.24f : 0.0f) +
                                                     sample->carbonate_purity * 0.42f +
                                                     (sample->parent_material == PARENT_MATERIAL_CARBONATE ? 0.20f : 0.0f) -
                                                     wetness * 0.14f + sample->fault_mask * 0.12f,
                                                     0.0f, 1.0f);
            sample->fracture_cave_mask = sdk_worldgen_clampf(hardrock_weight * 0.28f + sample->fault_mask * 0.44f +
                                                             sample->ravine_mask * 0.18f +
                                                             (sample->terrain_province == TERRAIN_PROVINCE_RIFT_VALLEY ? 0.15f : 0.0f),
                                                             0.0f, 1.0f);
            sample->lava_tube_mask = sdk_worldgen_clampf(sample->lava_flow_bias * 0.52f + sample->shield_mask * 0.28f +
                                                         sample->fissure_mask * 0.20f,
                                                         0.0f, 1.0f);
            sample->cave_depth_bias = sdk_worldgen_clampf(0.24f + relief_strength * 0.32f + hardrock_weight * 0.16f +
                                                          sample->karst_mask * 0.15f + sample->lava_tube_mask * 0.12f,
                                                          0.0f, 1.0f);
            sample->cave_entrance_mask = sdk_worldgen_clampf(sample->ravine_mask * 0.28f + sample->valley_mask * 0.14f +
                                                             sample->waterfall_mask * 0.20f + sample->caldera_mask * 0.18f +
                                                             sample->fault_mask * 0.12f + slope_f / 48.0f,
                                                             0.0f, 1.0f);
            if (sample->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN ||
                sample->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
                (sample->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST && coast_signed_distance < 18.0f)) {
                sample->karst_mask *= 0.08f;
                sample->fracture_cave_mask *= 0.12f;
                sample->lava_tube_mask *= 0.12f;
                sample->cave_entrance_mask = 0.0f;
            }

            if (sample->lake_mask > 0.24f && sample->closed_basin_mask > 0.20f && sample->water_level >= sample->surface_base) {
                sample->water_table_depth = fminf(sample->water_table_depth, 1.0f);
                sample->drainage_index = fminf(sample->drainage_index, 0.26f);
                sample->soil_organic_value = fmaxf(sample->soil_organic_value, 0.30f + wetness * 0.18f);
            }

            sample->soil_salinity = classify_soil_salinity_local(sample->soil_salinity_value);
            sample->soil_reaction = classify_soil_reaction_local(sample->soil_reaction_value, sample->soil_salinity_value);
            sample->soil_fertility = classify_soil_fertility_local(sample->soil_fertility_value);
            sample->drainage_class = classify_drainage_local(sample->drainage_index, sample->water_table_depth);
            sample->surface_sediment = refine_surface_sediment_for_state(sample->terrain_province,
                                                                         sample->parent_material,
                                                                         sample->drainage_class,
                                                                         sample->soil_reaction,
                                                                         sample->soil_fertility,
                                                                         sample->surface_sediment,
                                                                         sample->moisture_band,
                                                                         sample->temperature_band,
                                                                         sample->surface_base,
                                                                         sample->water_level,
                                                                         sample->coast_signed_distance,
                                                                         sample->river_strength,
                                                                         sample->floodplain_mask,
                                                                         sample->lake_mask,
                                                                         sample->closed_basin_mask,
                                                                         open_ocean_weight + shelf_weight,
                                                                         impl->sea_level);
            sample->landform_flags = landform_flags_for_state(sample->surface_base,
                                                              sample->water_level,
                                                              sample->river_strength,
                                                              sample->river_channel_width,
                                                              sample->floodplain_mask,
                                                              sample->lake_mask,
                                                              sample->closed_basin_mask,
                                                              sample->ravine_mask,
                                                              sample->vent_mask,
                                                              sample->caldera_mask,
                                                              sample->lava_flow_bias,
                                                              sample->cave_entrance_mask);
            {
                uint8_t derived_ecology = coarse_ecology_for_state((int)lrintf(sample->surface_base), impl->sea_level,
                                                                   sample->terrain_province,
                                                                   sample->temperature_band,
                                                                   sample->moisture_band,
                                                                   sample->surface_sediment,
                                                                   sample->parent_material,
                                                                   sample->drainage_class,
                                                                   sample->soil_reaction,
                                                                   sample->soil_fertility,
                                                                   sample->soil_salinity,
                                                                   (uint8_t)sdk_worldgen_clampi((int)lrintf(sample->river_order), 0, 4),
                                                                   sample->coast_signed_distance,
                                                                   sample->river_strength,
                                                                   sample->wetness,
                                                                   sample->ash_bias,
                                                                   sample->landform_flags,
                                                                   (uint8_t)sdk_worldgen_clampi((int)lrintf(sample->water_table_depth), 0, 15));
                sample->ecology = (derived_ecology == ECOLOGY_BARREN && sample->surface_base > (float)impl->sea_level)
                    ? ecology
                    : derived_ecology;
            }

            weathered_thickness = sample->regolith_thickness + 1.5f + wetness * 2.0f;
            switch ((SdkStratigraphyProvince)sample->stratigraphy_province) {
                case SDK_STRAT_PROVINCE_OCEANIC:
                    upper_thickness = 18.0f + sample->channel_sand_bias * 10.0f;
                    lower_thickness = 40.0f + shelf_weight * 18.0f;
                    break;
                case SDK_STRAT_PROVINCE_CARBONATE_SHELF:
                    upper_thickness = 18.0f + sample->carbonate_purity * 18.0f;
                    lower_thickness = 34.0f + sample->carbonate_purity * 26.0f;
                    break;
                case SDK_STRAT_PROVINCE_HARDROCK_BASEMENT:
                    upper_thickness = 8.0f + sample->fault_mask * 8.0f + sample->stratigraphy_control * 6.0f;
                    lower_thickness = 18.0f + hardrock_weight * 14.0f;
                    break;
                case SDK_STRAT_PROVINCE_RIFT_BASIN:
                    upper_thickness = 20.0f + sample->channel_sand_bias * 18.0f;
                    lower_thickness = 34.0f + sample->basin_axis_weight * 42.0f;
                    break;
                case SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX:
                    upper_thickness = 14.0f + sample->vent_bias * 14.0f;
                    lower_thickness = 28.0f + sample->fault_mask * 12.0f + sample->vent_bias * 20.0f;
                    break;
                case SDK_STRAT_PROVINCE_FLOOD_BASALT:
                    upper_thickness = 16.0f + sample->stratigraphy_control * 12.0f;
                    lower_thickness = 36.0f + sample->basin_axis_weight * 18.0f;
                    break;
                case SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN:
                default:
                    upper_thickness = 20.0f + sample->channel_sand_bias * 18.0f;
                    lower_thickness = 40.0f + sample->basin_axis_weight * 34.0f;
                    break;
            }

            boundary_warp = sdk_worldgen_fbm((float)wx * 0.0034f + strike_x * 0.8f,
                                             (float)wz * 0.0030f + strike_z * 0.8f,
                                             impl->seed ^ 0xD0E1u, 3);
            lens_warp = sdk_worldgen_fbm(((float)wx * dir_x + (float)wz * dir_z) * 0.0052f,
                                         ((float)wx * -dir_z + (float)wz * dir_x) * 0.0074f,
                                         impl->seed ^ 0xD1E2u, 2);
            basement_relief = sdk_worldgen_fbm((float)wx * 0.0018f, (float)wz * 0.0018f,
                                               impl->seed ^ 0xD2E3u, 2);

            sample->weathered_base = sample->surface_base - sample->soil_depth - sample->sediment_thickness * 0.55f -
                                     weathered_thickness + structural_offset * 0.20f + sample->fault_throw * 0.18f +
                                     boundary_warp * (1.2f + wetness * 1.6f);
            sample->upper_top = sample->weathered_base - upper_thickness + structural_offset * 0.55f +
                                sample->fault_throw * 0.45f + boundary_warp * (3.4f + sample->channel_sand_bias * 2.5f);
            sample->lower_top = sample->upper_top - lower_thickness + structural_offset + sample->fault_throw +
                                boundary_warp * (4.8f + sample->basin_axis_weight * 3.0f) +
                                lens_warp * (3.0f + sample->channel_sand_bias * 2.2f);
            sample->basement_top = sample->lower_top + basement_relief * (2.5f + hardrock_weight * 3.0f + volcanic_weight * 2.0f);
            {
                float deep_thickness;
                float deep_warp;

                switch ((SdkStratigraphyProvince)sample->stratigraphy_province) {
                    case SDK_STRAT_PROVINCE_OCEANIC:
                        deep_thickness = 96.0f + shelf_weight * 18.0f + sample->stratigraphy_control * 10.0f;
                        break;
                    case SDK_STRAT_PROVINCE_CARBONATE_SHELF:
                        deep_thickness = 102.0f + sample->carbonate_purity * 18.0f;
                        break;
                    case SDK_STRAT_PROVINCE_HARDROCK_BASEMENT:
                        deep_thickness = 124.0f + hardrock_weight * 44.0f + sample->fault_mask * 10.0f;
                        break;
                    case SDK_STRAT_PROVINCE_RIFT_BASIN:
                        deep_thickness = 112.0f + sample->basin_axis_weight * 28.0f + sample->vent_bias * 14.0f;
                        break;
                    case SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX:
                        deep_thickness = 108.0f + sample->vent_bias * 26.0f + sample->fault_mask * 10.0f;
                        break;
                    case SDK_STRAT_PROVINCE_FLOOD_BASALT:
                        deep_thickness = 104.0f + sample->stratigraphy_control * 16.0f + volcanic_weight * 12.0f;
                        break;
                    case SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN:
                    default:
                        deep_thickness = 110.0f + sample->basin_axis_weight * 24.0f;
                        break;
                }

                deep_warp = sdk_worldgen_fbm((float)wx * 0.0011f + strike_x * 0.4f,
                                             (float)wz * 0.0011f + strike_z * 0.4f,
                                             impl->seed ^ 0xD3F4u, 2);
                sample->deep_basement_top = sample->basement_top - deep_thickness +
                                            structural_offset * 0.35f + sample->fault_throw * 0.65f +
                                            deep_warp * (5.0f + hardrock_weight * 3.0f + volcanic_weight * 2.0f);
            }

            sample->weathered_base = sdk_worldgen_clampf(sample->weathered_base, 3.0f, sample->surface_base - 1.0f);
            sample->upper_top = sdk_worldgen_clampf(sample->upper_top, 2.0f, sample->weathered_base - 1.0f);
            sample->lower_top = sdk_worldgen_clampf(sample->lower_top, 2.0f, sample->upper_top - 1.0f);
            {
                float basement_hi = sample->lower_top - 1.0f;
                float deep_hi;
                if (basement_hi < 2.0f) basement_hi = 2.0f;
                sample->basement_top = sdk_worldgen_clampf(sample->basement_top, 2.0f, basement_hi);
                deep_hi = sample->basement_top - 1.0f;
                if (deep_hi < 2.0f) deep_hi = 2.0f;
                sample->deep_basement_top = sdk_worldgen_clampf(sample->deep_basement_top, 2.0f, deep_hi);
            }
        }
    }
}

void sdk_worldgen_sample_region_fields(SdkWorldGenRegionTile* tile, SdkWorldGen* wg,
                                       int wx, int wz, SdkRegionFieldSample* out_sample)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    int origin_x;
    int origin_z;
    float sample_fx;
    float sample_fz;
    int ix;
    int iz;
    float tx;
    float tz;
    float w00;
    float w10;
    float w01;
    float w11;
    const SdkRegionFieldSample* s00;
    const SdkRegionFieldSample* s10;
    const SdkRegionFieldSample* s01;
    const SdkRegionFieldSample* s11;
    float dir_len;
    float dip_len;

    if (!impl || !tile || !out_sample) return;

    origin_x = tile->tile_x * SDK_WORLDGEN_REGION_TILE_BLOCKS - SDK_WORLDGEN_REGION_TILE_HALO_BLOCKS;
    origin_z = tile->tile_z * SDK_WORLDGEN_REGION_TILE_BLOCKS - SDK_WORLDGEN_REGION_TILE_HALO_BLOCKS;
    sample_fx = ((float)wx - (float)origin_x) / (float)SDK_WORLDGEN_REGION_SAMPLE_SPACING;
    sample_fz = ((float)wz - (float)origin_z) / (float)SDK_WORLDGEN_REGION_SAMPLE_SPACING;
    ix = sdk_worldgen_clampi((int)floorf(sample_fx), 0, SDK_WORLDGEN_REGION_STRIDE - 2);
    iz = sdk_worldgen_clampi((int)floorf(sample_fz), 0, SDK_WORLDGEN_REGION_STRIDE - 2);
    tx = sdk_worldgen_clampf(sample_fx - (float)ix, 0.0f, 1.0f);
    tz = sdk_worldgen_clampf(sample_fz - (float)iz, 0.0f, 1.0f);
    w00 = (1.0f - tx) * (1.0f - tz);
    w10 = tx * (1.0f - tz);
    w01 = (1.0f - tx) * tz;
    w11 = tx * tz;

    s00 = region_sample_at(tile, ix,     iz);
    s10 = region_sample_at(tile, ix + 1, iz);
    s01 = region_sample_at(tile, ix,     iz + 1);
    s11 = region_sample_at(tile, ix + 1, iz + 1);

    memset(out_sample, 0, sizeof(*out_sample));
    out_sample->terrain_province = weighted_vote4_u8(s00->terrain_province, s10->terrain_province,
                                                     s01->terrain_province, s11->terrain_province,
                                                     w00, w10, w01, w11, NULL);
    out_sample->bedrock_province = weighted_vote4_u8(s00->bedrock_province, s10->bedrock_province,
                                                     s01->bedrock_province, s11->bedrock_province,
                                                     w00, w10, w01, w11, NULL);
    out_sample->temperature_band = weighted_vote4_u8(s00->temperature_band, s10->temperature_band,
                                                     s01->temperature_band, s11->temperature_band,
                                                     w00, w10, w01, w11, NULL);
    out_sample->moisture_band = weighted_vote4_u8(s00->moisture_band, s10->moisture_band,
                                                  s01->moisture_band, s11->moisture_band,
                                                  w00, w10, w01, w11, NULL);
    out_sample->surface_sediment = weighted_vote4_u8(s00->surface_sediment, s10->surface_sediment,
                                                     s01->surface_sediment, s11->surface_sediment,
                                                     w00, w10, w01, w11, NULL);
    out_sample->parent_material = weighted_vote4_u8(s00->parent_material, s10->parent_material,
                                                    s01->parent_material, s11->parent_material,
                                                    w00, w10, w01, w11, NULL);
    out_sample->ecology = weighted_vote4_u8(s00->ecology, s10->ecology, s01->ecology, s11->ecology,
                                            w00, w10, w01, w11, NULL);
    out_sample->resource_province = weighted_vote4_u8(s00->resource_province, s10->resource_province,
                                                      s01->resource_province, s11->resource_province,
                                                      w00, w10, w01, w11, NULL);
    out_sample->hydrocarbon_class = weighted_vote4_u8(s00->hydrocarbon_class, s10->hydrocarbon_class,
                                                      s01->hydrocarbon_class, s11->hydrocarbon_class,
                                                      w00, w10, w01, w11, NULL);
    out_sample->water_surface_class = weighted_vote4_u8(s00->water_surface_class, s10->water_surface_class,
                                                        s01->water_surface_class, s11->water_surface_class,
                                                        w00, w10, w01, w11, NULL);
    out_sample->stratigraphy_province = weighted_vote4_u8(s00->stratigraphy_province, s10->stratigraphy_province,
                                                          s01->stratigraphy_province, s11->stratigraphy_province,
                                                          w00, w10, w01, w11, NULL);

    out_sample->base_height = bilerp4(s00->base_height, s10->base_height, s01->base_height, s11->base_height, tx, tz);
    out_sample->surface_base = bilerp4(s00->surface_base, s10->surface_base, s01->surface_base, s11->surface_base, tx, tz);
    out_sample->surface_relief = bilerp4(s00->surface_relief, s10->surface_relief, s01->surface_relief, s11->surface_relief, tx, tz);
    out_sample->water_level = bilerp4(s00->water_level, s10->water_level, s01->water_level, s11->water_level, tx, tz);
    out_sample->river_bed_height = bilerp4(s00->river_bed_height, s10->river_bed_height, s01->river_bed_height, s11->river_bed_height, tx, tz);
    out_sample->coast_signed_distance = bilerp4(s00->coast_signed_distance, s10->coast_signed_distance, s01->coast_signed_distance, s11->coast_signed_distance, tx, tz);
    out_sample->ocean_depth = bilerp4(s00->ocean_depth, s10->ocean_depth, s01->ocean_depth, s11->ocean_depth, tx, tz);
    out_sample->river_strength = bilerp4(s00->river_strength, s10->river_strength, s01->river_strength, s11->river_strength, tx, tz);
    out_sample->floodplain_mask = bilerp4(s00->floodplain_mask, s10->floodplain_mask, s01->floodplain_mask, s11->floodplain_mask, tx, tz);
    out_sample->river_order = bilerp4(s00->river_order, s10->river_order, s01->river_order, s11->river_order, tx, tz);
    out_sample->floodplain_width = bilerp4(s00->floodplain_width, s10->floodplain_width, s01->floodplain_width, s11->floodplain_width, tx, tz);
    out_sample->river_channel_width = bilerp4(s00->river_channel_width, s10->river_channel_width, s01->river_channel_width, s11->river_channel_width, tx, tz);
    out_sample->river_channel_depth = bilerp4(s00->river_channel_depth, s10->river_channel_depth, s01->river_channel_depth, s11->river_channel_depth, tx, tz);
    out_sample->valley_mask = bilerp4(s00->valley_mask, s10->valley_mask, s01->valley_mask, s11->valley_mask, tx, tz);
    out_sample->braid_mask = bilerp4(s00->braid_mask, s10->braid_mask, s01->braid_mask, s11->braid_mask, tx, tz);
    out_sample->waterfall_mask = bilerp4(s00->waterfall_mask, s10->waterfall_mask, s01->waterfall_mask, s11->waterfall_mask, tx, tz);
    out_sample->flow_dir_x = bilerp4(s00->flow_dir_x, s10->flow_dir_x, s01->flow_dir_x, s11->flow_dir_x, tx, tz);
    out_sample->flow_dir_z = bilerp4(s00->flow_dir_z, s10->flow_dir_z, s01->flow_dir_z, s11->flow_dir_z, tx, tz);
    out_sample->wetness = bilerp4(s00->wetness, s10->wetness, s01->wetness, s11->wetness, tx, tz);
    out_sample->air_temp = bilerp4(s00->air_temp, s10->air_temp, s01->air_temp, s11->air_temp, tx, tz);
    out_sample->water_temp = bilerp4(s00->water_temp, s10->water_temp, s01->water_temp, s11->water_temp, tx, tz);
    out_sample->soil_depth = bilerp4(s00->soil_depth, s10->soil_depth, s01->soil_depth, s11->soil_depth, tx, tz);
    out_sample->sediment_thickness = bilerp4(s00->sediment_thickness, s10->sediment_thickness, s01->sediment_thickness, s11->sediment_thickness, tx, tz);
    out_sample->regolith_thickness = bilerp4(s00->regolith_thickness, s10->regolith_thickness, s01->regolith_thickness, s11->regolith_thickness, tx, tz);
    out_sample->drainage_index = bilerp4(s00->drainage_index, s10->drainage_index, s01->drainage_index, s11->drainage_index, tx, tz);
    out_sample->soil_reaction_value = bilerp4(s00->soil_reaction_value, s10->soil_reaction_value,
                                              s01->soil_reaction_value, s11->soil_reaction_value, tx, tz);
    out_sample->soil_fertility_value = bilerp4(s00->soil_fertility_value, s10->soil_fertility_value,
                                               s01->soil_fertility_value, s11->soil_fertility_value, tx, tz);
    out_sample->soil_salinity_value = bilerp4(s00->soil_salinity_value, s10->soil_salinity_value,
                                              s01->soil_salinity_value, s11->soil_salinity_value, tx, tz);
    out_sample->soil_organic_value = bilerp4(s00->soil_organic_value, s10->soil_organic_value,
                                             s01->soil_organic_value, s11->soil_organic_value, tx, tz);
    out_sample->water_table_depth = bilerp4(s00->water_table_depth, s10->water_table_depth,
                                            s01->water_table_depth, s11->water_table_depth, tx, tz);
    out_sample->stratigraphy_control = bilerp4(s00->stratigraphy_control, s10->stratigraphy_control, s01->stratigraphy_control, s11->stratigraphy_control, tx, tz);
    out_sample->weathered_base = bilerp4(s00->weathered_base, s10->weathered_base, s01->weathered_base, s11->weathered_base, tx, tz);
    out_sample->upper_top = bilerp4(s00->upper_top, s10->upper_top, s01->upper_top, s11->upper_top, tx, tz);
    out_sample->lower_top = bilerp4(s00->lower_top, s10->lower_top, s01->lower_top, s11->lower_top, tx, tz);
    out_sample->basement_top = bilerp4(s00->basement_top, s10->basement_top, s01->basement_top, s11->basement_top, tx, tz);
    out_sample->deep_basement_top = bilerp4(s00->deep_basement_top, s10->deep_basement_top,
                                            s01->deep_basement_top, s11->deep_basement_top, tx, tz);
    out_sample->dip_x = bilerp4(s00->dip_x, s10->dip_x, s01->dip_x, s11->dip_x, tx, tz);
    out_sample->dip_z = bilerp4(s00->dip_z, s10->dip_z, s01->dip_z, s11->dip_z, tx, tz);
    out_sample->fault_mask = bilerp4(s00->fault_mask, s10->fault_mask, s01->fault_mask, s11->fault_mask, tx, tz);
    out_sample->fault_throw = bilerp4(s00->fault_throw, s10->fault_throw, s01->fault_throw, s11->fault_throw, tx, tz);
    out_sample->basin_axis_weight = bilerp4(s00->basin_axis_weight, s10->basin_axis_weight, s01->basin_axis_weight, s11->basin_axis_weight, tx, tz);
    out_sample->channel_sand_bias = bilerp4(s00->channel_sand_bias, s10->channel_sand_bias, s01->channel_sand_bias, s11->channel_sand_bias, tx, tz);
    out_sample->trap_strength = bilerp4(s00->trap_strength, s10->trap_strength, s01->trap_strength, s11->trap_strength, tx, tz);
    out_sample->seal_quality = bilerp4(s00->seal_quality, s10->seal_quality, s01->seal_quality, s11->seal_quality, tx, tz);
    out_sample->gas_bias = bilerp4(s00->gas_bias, s10->gas_bias, s01->gas_bias, s11->gas_bias, tx, tz);
    out_sample->source_richness = bilerp4(s00->source_richness, s10->source_richness, s01->source_richness, s11->source_richness, tx, tz);
    out_sample->carbonate_purity = bilerp4(s00->carbonate_purity, s10->carbonate_purity, s01->carbonate_purity, s11->carbonate_purity, tx, tz);
    out_sample->vent_bias = bilerp4(s00->vent_bias, s10->vent_bias, s01->vent_bias, s11->vent_bias, tx, tz);
    out_sample->evaporite_bias = bilerp4(s00->evaporite_bias, s10->evaporite_bias, s01->evaporite_bias, s11->evaporite_bias, tx, tz);
    out_sample->lake_mask = bilerp4(s00->lake_mask, s10->lake_mask, s01->lake_mask, s11->lake_mask, tx, tz);
    out_sample->lake_level = bilerp4(s00->lake_level, s10->lake_level, s01->lake_level, s11->lake_level, tx, tz);
    out_sample->closed_basin_mask = bilerp4(s00->closed_basin_mask, s10->closed_basin_mask, s01->closed_basin_mask, s11->closed_basin_mask, tx, tz);
    out_sample->ravine_mask = bilerp4(s00->ravine_mask, s10->ravine_mask, s01->ravine_mask, s11->ravine_mask, tx, tz);
    out_sample->ravine_depth = bilerp4(s00->ravine_depth, s10->ravine_depth, s01->ravine_depth, s11->ravine_depth, tx, tz);
    out_sample->ravine_width = bilerp4(s00->ravine_width, s10->ravine_width, s01->ravine_width, s11->ravine_width, tx, tz);
    out_sample->vent_mask = bilerp4(s00->vent_mask, s10->vent_mask, s01->vent_mask, s11->vent_mask, tx, tz);
    out_sample->vent_distance = bilerp4(s00->vent_distance, s10->vent_distance, s01->vent_distance, s11->vent_distance, tx, tz);
    out_sample->caldera_mask = bilerp4(s00->caldera_mask, s10->caldera_mask, s01->caldera_mask, s11->caldera_mask, tx, tz);
    out_sample->lava_flow_bias = bilerp4(s00->lava_flow_bias, s10->lava_flow_bias, s01->lava_flow_bias, s11->lava_flow_bias, tx, tz);
    out_sample->ash_bias = bilerp4(s00->ash_bias, s10->ash_bias, s01->ash_bias, s11->ash_bias, tx, tz);
    out_sample->karst_mask = bilerp4(s00->karst_mask, s10->karst_mask, s01->karst_mask, s11->karst_mask, tx, tz);
    out_sample->fracture_cave_mask = bilerp4(s00->fracture_cave_mask, s10->fracture_cave_mask, s01->fracture_cave_mask, s11->fracture_cave_mask, tx, tz);
    out_sample->lava_tube_mask = bilerp4(s00->lava_tube_mask, s10->lava_tube_mask, s01->lava_tube_mask, s11->lava_tube_mask, tx, tz);
    out_sample->cave_depth_bias = bilerp4(s00->cave_depth_bias, s10->cave_depth_bias, s01->cave_depth_bias, s11->cave_depth_bias, tx, tz);
    out_sample->cave_entrance_mask = bilerp4(s00->cave_entrance_mask, s10->cave_entrance_mask, s01->cave_entrance_mask, s11->cave_entrance_mask, tx, tz);
    out_sample->stratovolcano_mask = bilerp4(s00->stratovolcano_mask, s10->stratovolcano_mask, s01->stratovolcano_mask, s11->stratovolcano_mask, tx, tz);
    out_sample->shield_mask = bilerp4(s00->shield_mask, s10->shield_mask, s01->shield_mask, s11->shield_mask, tx, tz);
    out_sample->fissure_mask = bilerp4(s00->fissure_mask, s10->fissure_mask, s01->fissure_mask, s11->fissure_mask, tx, tz);
    out_sample->ashfall_mask = bilerp4(s00->ashfall_mask, s10->ashfall_mask, s01->ashfall_mask, s11->ashfall_mask, tx, tz);
    out_sample->resource_grade = (uint8_t)sdk_worldgen_clampi((int)lrintf(bilerp4((float)s00->resource_grade, (float)s10->resource_grade,
                                                                                   (float)s01->resource_grade, (float)s11->resource_grade,
                                                                                   tx, tz)),
                                                              0, 255);
    out_sample->soil_salinity = classify_soil_salinity_local(out_sample->soil_salinity_value);
    out_sample->soil_reaction = classify_soil_reaction_local(out_sample->soil_reaction_value, out_sample->soil_salinity_value);
    out_sample->soil_fertility = classify_soil_fertility_local(out_sample->soil_fertility_value);
    out_sample->drainage_class = classify_drainage_local(out_sample->drainage_index, out_sample->water_table_depth);
    out_sample->surface_sediment = refine_surface_sediment_for_state(out_sample->terrain_province,
                                                                     out_sample->parent_material,
                                                                     out_sample->drainage_class,
                                                                     out_sample->soil_reaction,
                                                                     out_sample->soil_fertility,
                                                                     out_sample->surface_sediment,
                                                                     out_sample->moisture_band,
                                                                     out_sample->temperature_band,
                                                                     out_sample->surface_base,
                                                                     out_sample->water_level,
                                                                     out_sample->coast_signed_distance,
                                                                     out_sample->river_strength,
                                                                     out_sample->floodplain_mask,
                                                                     out_sample->lake_mask,
                                                                     out_sample->closed_basin_mask,
                                                                     province_weight4_region(TERRAIN_PROVINCE_OPEN_OCEAN, s00, s10, s01, s11, w00, w10, w01, w11) +
                                                                     province_weight4_region(TERRAIN_PROVINCE_CONTINENTAL_SHELF, s00, s10, s01, s11, w00, w10, w01, w11),
                                                                     impl->sea_level);
    out_sample->landform_flags = landform_flags_for_state(out_sample->surface_base,
                                                          out_sample->water_level,
                                                          out_sample->river_strength,
                                                          out_sample->river_channel_width,
                                                          out_sample->floodplain_mask,
                                                          out_sample->lake_mask,
                                                          out_sample->closed_basin_mask,
                                                          out_sample->ravine_mask,
                                                          out_sample->vent_mask,
                                                          out_sample->caldera_mask,
                                                          out_sample->lava_flow_bias,
                                                          out_sample->cave_entrance_mask);
    {
        uint8_t derived_ecology = coarse_ecology_for_state((int)lrintf(out_sample->surface_base), impl->sea_level,
                                                           out_sample->terrain_province,
                                                           out_sample->temperature_band,
                                                           out_sample->moisture_band,
                                                           out_sample->surface_sediment,
                                                           out_sample->parent_material,
                                                           out_sample->drainage_class,
                                                           out_sample->soil_reaction,
                                                           out_sample->soil_fertility,
                                                           out_sample->soil_salinity,
                                                           (uint8_t)sdk_worldgen_clampi((int)lrintf(out_sample->river_order), 0, 4),
                                                           out_sample->coast_signed_distance,
                                                           out_sample->river_strength,
                                                           out_sample->wetness,
                                                           out_sample->ash_bias,
                                                           out_sample->landform_flags,
                                                           (uint8_t)sdk_worldgen_clampi((int)lrintf(out_sample->water_table_depth), 0, 15));
        if (!(derived_ecology == ECOLOGY_BARREN && out_sample->surface_base > (float)impl->sea_level)) {
            out_sample->ecology = derived_ecology;
        }
    }

    dir_len = sqrtf(out_sample->flow_dir_x * out_sample->flow_dir_x + out_sample->flow_dir_z * out_sample->flow_dir_z);
    if (dir_len > 0.0001f) {
        out_sample->flow_dir_x /= dir_len;
        out_sample->flow_dir_z /= dir_len;
    }

    dip_len = sqrtf(out_sample->dip_x * out_sample->dip_x + out_sample->dip_z * out_sample->dip_z);
    if (dip_len > 0.0001f) {
        float mag = dip_len;
        out_sample->dip_x /= mag;
        out_sample->dip_z /= mag;
        out_sample->dip_x *= dip_len;
        out_sample->dip_z *= dip_len;
    }
}

void sdk_worldgen_sample_column_from_region_tile(SdkWorldGenRegionTile* tile, SdkWorldGen* wg,
                                                 int wx, int wz, SdkTerrainColumnProfile* out_profile)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    int origin_x;
    int origin_z;
    float sample_fx;
    float sample_fz;
    int ix;
    int iz;
    float tx;
    float tz;
    float w00;
    float w10;
    float w01;
    float w11;
    const SdkRegionFieldSample* s00;
    const SdkRegionFieldSample* s10;
    const SdkRegionFieldSample* s01;
    const SdkRegionFieldSample* s11;
    float surface_base;
    float base_height;
    float water_level;
    float river_bed_height;
    float surface_relief;
    float river_strength;
    float floodplain_mask;
    float river_channel_width;
    float river_channel_depth;
    float valley_mask;
    float braid_mask;
    float waterfall_mask;
    float wetness;
    float coast_signed_distance;
    float soil_depth;
    float sediment_thickness;
    float regolith_thickness;
    float flow_dir_x;
    float flow_dir_z;
    float lake_mask;
    float lake_level;
    float closed_basin_mask;
    float ravine_mask;
    float ravine_depth;
    float ravine_width;
    float vent_mask;
    float vent_distance;
    float caldera_mask;
    float lava_flow_bias;
    float ash_bias;
    float karst_mask;
    float fracture_cave_mask;
    float lava_tube_mask;
    float cave_depth_bias;
    float cave_entrance_mask;
    float stratovolcano_mask;
    float shield_mask;
    float fissure_mask;
    float ashfall_mask;
    float dir_len;
    float province_weight;
    float marine_weight;
    float volcanic_weight;
    float micro_relief;
    float surface;
    float base;
    float river_order_f;
    uint32_t landform_flags;
    uint8_t terrain_province;
    uint8_t bedrock_province;
    uint8_t ecology;
    uint8_t resource_province;
    uint8_t hydrocarbon_class;
    uint8_t temperature_band;
    uint8_t moisture_band;
    uint8_t surface_sediment;
    uint8_t parent_material;
    float drainage_index;
    float soil_reaction_value;
    float soil_fertility_value;
    float soil_salinity_value;
    float water_table_depth_f;
    float resource_grade_f;
    uint8_t water_surface_class;

    if (!impl || !tile || !out_profile) return;

    origin_x = tile->tile_x * SDK_WORLDGEN_REGION_TILE_BLOCKS - SDK_WORLDGEN_REGION_TILE_HALO_BLOCKS;
    origin_z = tile->tile_z * SDK_WORLDGEN_REGION_TILE_BLOCKS - SDK_WORLDGEN_REGION_TILE_HALO_BLOCKS;
    sample_fx = ((float)wx - (float)origin_x) / (float)SDK_WORLDGEN_REGION_SAMPLE_SPACING;
    sample_fz = ((float)wz - (float)origin_z) / (float)SDK_WORLDGEN_REGION_SAMPLE_SPACING;
    ix = sdk_worldgen_clampi((int)floorf(sample_fx), 0, SDK_WORLDGEN_REGION_STRIDE - 2);
    iz = sdk_worldgen_clampi((int)floorf(sample_fz), 0, SDK_WORLDGEN_REGION_STRIDE - 2);
    tx = sdk_worldgen_clampf(sample_fx - (float)ix, 0.0f, 1.0f);
    tz = sdk_worldgen_clampf(sample_fz - (float)iz, 0.0f, 1.0f);
    w00 = (1.0f - tx) * (1.0f - tz);
    w10 = tx * (1.0f - tz);
    w01 = (1.0f - tx) * tz;
    w11 = tx * tz;

    s00 = region_sample_at(tile, ix,     iz);
    s10 = region_sample_at(tile, ix + 1, iz);
    s01 = region_sample_at(tile, ix,     iz + 1);
    s11 = region_sample_at(tile, ix + 1, iz + 1);

    terrain_province = weighted_vote4_u8(s00->terrain_province, s10->terrain_province,
                                         s01->terrain_province, s11->terrain_province,
                                         w00, w10, w01, w11, &province_weight);
    bedrock_province = weighted_vote4_u8(s00->bedrock_province, s10->bedrock_province,
                                         s01->bedrock_province, s11->bedrock_province,
                                         w00, w10, w01, w11, NULL);
    ecology = weighted_vote4_u8(s00->ecology, s10->ecology, s01->ecology, s11->ecology,
                                w00, w10, w01, w11, NULL);
    resource_province = weighted_vote4_u8(s00->resource_province, s10->resource_province,
                                          s01->resource_province, s11->resource_province,
                                          w00, w10, w01, w11, NULL);
    hydrocarbon_class = weighted_vote4_u8(s00->hydrocarbon_class, s10->hydrocarbon_class,
                                          s01->hydrocarbon_class, s11->hydrocarbon_class,
                                          w00, w10, w01, w11, NULL);
    temperature_band = weighted_vote4_u8(s00->temperature_band, s10->temperature_band,
                                         s01->temperature_band, s11->temperature_band,
                                         w00, w10, w01, w11, NULL);
    moisture_band = weighted_vote4_u8(s00->moisture_band, s10->moisture_band,
                                      s01->moisture_band, s11->moisture_band,
                                      w00, w10, w01, w11, NULL);
    surface_sediment = weighted_vote4_u8(s00->surface_sediment, s10->surface_sediment,
                                         s01->surface_sediment, s11->surface_sediment,
                                         w00, w10, w01, w11, NULL);
    parent_material = weighted_vote4_u8(s00->parent_material, s10->parent_material,
                                        s01->parent_material, s11->parent_material,
                                        w00, w10, w01, w11, NULL);
    water_surface_class = weighted_vote4_u8(s00->water_surface_class, s10->water_surface_class,
                                            s01->water_surface_class, s11->water_surface_class,
                                            w00, w10, w01, w11, NULL);
    river_order_f = bilerp4(s00->river_order, s10->river_order, s01->river_order, s11->river_order, tx, tz);

    base_height = bilerp4(s00->base_height, s10->base_height, s01->base_height, s11->base_height, tx, tz);
    surface_base = bilerp4(s00->surface_base, s10->surface_base, s01->surface_base, s11->surface_base, tx, tz);
    water_level = bilerp4(s00->water_level, s10->water_level, s01->water_level, s11->water_level, tx, tz);
    river_bed_height = bilerp4(s00->river_bed_height, s10->river_bed_height, s01->river_bed_height, s11->river_bed_height, tx, tz);
    surface_relief = bilerp4(s00->surface_relief, s10->surface_relief, s01->surface_relief, s11->surface_relief, tx, tz);
    river_strength = bilerp4(s00->river_strength, s10->river_strength, s01->river_strength, s11->river_strength, tx, tz);
    floodplain_mask = bilerp4(s00->floodplain_mask, s10->floodplain_mask, s01->floodplain_mask, s11->floodplain_mask, tx, tz);
    wetness = bilerp4(s00->wetness, s10->wetness, s01->wetness, s11->wetness, tx, tz);
    coast_signed_distance = bilerp4(s00->coast_signed_distance, s10->coast_signed_distance,
                                    s01->coast_signed_distance, s11->coast_signed_distance, tx, tz);
    soil_depth = bilerp4(s00->soil_depth, s10->soil_depth, s01->soil_depth, s11->soil_depth, tx, tz);
    sediment_thickness = bilerp4(s00->sediment_thickness, s10->sediment_thickness,
                                 s01->sediment_thickness, s11->sediment_thickness, tx, tz);
    regolith_thickness = bilerp4(s00->regolith_thickness, s10->regolith_thickness,
                                 s01->regolith_thickness, s11->regolith_thickness, tx, tz);
    drainage_index = bilerp4(s00->drainage_index, s10->drainage_index, s01->drainage_index, s11->drainage_index, tx, tz);
    soil_reaction_value = bilerp4(s00->soil_reaction_value, s10->soil_reaction_value,
                                  s01->soil_reaction_value, s11->soil_reaction_value, tx, tz);
    soil_fertility_value = bilerp4(s00->soil_fertility_value, s10->soil_fertility_value,
                                   s01->soil_fertility_value, s11->soil_fertility_value, tx, tz);
    soil_salinity_value = bilerp4(s00->soil_salinity_value, s10->soil_salinity_value,
                                  s01->soil_salinity_value, s11->soil_salinity_value, tx, tz);
    water_table_depth_f = bilerp4(s00->water_table_depth, s10->water_table_depth,
                                  s01->water_table_depth, s11->water_table_depth, tx, tz);
    resource_grade_f = bilerp4((float)s00->resource_grade, (float)s10->resource_grade,
                               (float)s01->resource_grade, (float)s11->resource_grade, tx, tz);
    flow_dir_x = bilerp4(s00->flow_dir_x, s10->flow_dir_x, s01->flow_dir_x, s11->flow_dir_x, tx, tz);
    flow_dir_z = bilerp4(s00->flow_dir_z, s10->flow_dir_z, s01->flow_dir_z, s11->flow_dir_z, tx, tz);
    river_channel_width = bilerp4(s00->river_channel_width, s10->river_channel_width, s01->river_channel_width, s11->river_channel_width, tx, tz);
    river_channel_depth = bilerp4(s00->river_channel_depth, s10->river_channel_depth, s01->river_channel_depth, s11->river_channel_depth, tx, tz);
    valley_mask = bilerp4(s00->valley_mask, s10->valley_mask, s01->valley_mask, s11->valley_mask, tx, tz);
    braid_mask = bilerp4(s00->braid_mask, s10->braid_mask, s01->braid_mask, s11->braid_mask, tx, tz);
    waterfall_mask = bilerp4(s00->waterfall_mask, s10->waterfall_mask, s01->waterfall_mask, s11->waterfall_mask, tx, tz);
    lake_mask = bilerp4(s00->lake_mask, s10->lake_mask, s01->lake_mask, s11->lake_mask, tx, tz);
    lake_level = bilerp4(s00->lake_level, s10->lake_level, s01->lake_level, s11->lake_level, tx, tz);
    closed_basin_mask = bilerp4(s00->closed_basin_mask, s10->closed_basin_mask, s01->closed_basin_mask, s11->closed_basin_mask, tx, tz);
    ravine_mask = bilerp4(s00->ravine_mask, s10->ravine_mask, s01->ravine_mask, s11->ravine_mask, tx, tz);
    ravine_depth = bilerp4(s00->ravine_depth, s10->ravine_depth, s01->ravine_depth, s11->ravine_depth, tx, tz);
    ravine_width = bilerp4(s00->ravine_width, s10->ravine_width, s01->ravine_width, s11->ravine_width, tx, tz);
    vent_mask = bilerp4(s00->vent_mask, s10->vent_mask, s01->vent_mask, s11->vent_mask, tx, tz);
    vent_distance = bilerp4(s00->vent_distance, s10->vent_distance, s01->vent_distance, s11->vent_distance, tx, tz);
    caldera_mask = bilerp4(s00->caldera_mask, s10->caldera_mask, s01->caldera_mask, s11->caldera_mask, tx, tz);
    lava_flow_bias = bilerp4(s00->lava_flow_bias, s10->lava_flow_bias, s01->lava_flow_bias, s11->lava_flow_bias, tx, tz);
    ash_bias = bilerp4(s00->ash_bias, s10->ash_bias, s01->ash_bias, s11->ash_bias, tx, tz);
    karst_mask = bilerp4(s00->karst_mask, s10->karst_mask, s01->karst_mask, s11->karst_mask, tx, tz);
    fracture_cave_mask = bilerp4(s00->fracture_cave_mask, s10->fracture_cave_mask, s01->fracture_cave_mask, s11->fracture_cave_mask, tx, tz);
    lava_tube_mask = bilerp4(s00->lava_tube_mask, s10->lava_tube_mask, s01->lava_tube_mask, s11->lava_tube_mask, tx, tz);
    cave_depth_bias = bilerp4(s00->cave_depth_bias, s10->cave_depth_bias, s01->cave_depth_bias, s11->cave_depth_bias, tx, tz);
    cave_entrance_mask = bilerp4(s00->cave_entrance_mask, s10->cave_entrance_mask, s01->cave_entrance_mask, s11->cave_entrance_mask, tx, tz);
    stratovolcano_mask = bilerp4(s00->stratovolcano_mask, s10->stratovolcano_mask, s01->stratovolcano_mask, s11->stratovolcano_mask, tx, tz);
    shield_mask = bilerp4(s00->shield_mask, s10->shield_mask, s01->shield_mask, s11->shield_mask, tx, tz);
    fissure_mask = bilerp4(s00->fissure_mask, s10->fissure_mask, s01->fissure_mask, s11->fissure_mask, tx, tz);
    ashfall_mask = bilerp4(s00->ashfall_mask, s10->ashfall_mask, s01->ashfall_mask, s11->ashfall_mask, tx, tz);
    marine_weight = province_weight4_region(TERRAIN_PROVINCE_OPEN_OCEAN, s00, s10, s01, s11, w00, w10, w01, w11) +
                    province_weight4_region(TERRAIN_PROVINCE_CONTINENTAL_SHELF, s00, s10, s01, s11, w00, w10, w01, w11);
    volcanic_weight = province_weight4_region(TERRAIN_PROVINCE_VOLCANIC_ARC, s00, s10, s01, s11, w00, w10, w01, w11) +
                      province_weight4_region(TERRAIN_PROVINCE_BASALT_PLATEAU, s00, s10, s01, s11, w00, w10, w01, w11);

    dir_len = sqrtf(flow_dir_x * flow_dir_x + flow_dir_z * flow_dir_z);
    if (dir_len > 0.0001f) {
        flow_dir_x /= dir_len;
        flow_dir_z /= dir_len;
    } else {
        flow_dir_x = 1.0f;
        flow_dir_z = 0.0f;
    }

    micro_relief = sdk_worldgen_fbm((float)wx * 0.024f, (float)wz * 0.024f, impl->seed ^ 0xE161u, 2) * surface_relief * 0.34f;
    if (terrain_province == TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT ||
        terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ||
        terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND ||
        terrain_province == TERRAIN_PROVINCE_ALPINE_BELT) {
        micro_relief += sdk_worldgen_ridged((float)wx * 0.041f + flow_dir_x * 0.7f,
                                            (float)wz * 0.041f + flow_dir_z * 0.7f,
                                            impl->seed ^ 0xE262u, 2) * surface_relief * 0.18f;
    }
    micro_relief *= 1.0f - sdk_worldgen_clampf(floodplain_mask * 0.78f + wetness * 0.20f, 0.0f, 0.85f);
    micro_relief *= 1.0f - remap_smooth(fabsf(coast_signed_distance), 0.0f, 72.0f) * 0.35f;
    if (province_weight < 0.60f) micro_relief *= 0.85f;

    surface = surface_base + micro_relief;
    base = base_height + micro_relief * 0.12f;

    if (lake_mask > 0.18f && closed_basin_mask > 0.24f && marine_weight < 0.20f) {
        float lake_blend = remap_smooth(lake_mask, 0.18f, 0.78f) *
                           remap_smooth(closed_basin_mask, 0.24f, 0.78f) *
                           sdk_worldgen_clampf(1.0f - remap_smooth(fabsf(coast_signed_distance), 0.0f, 88.0f), 0.0f, 1.0f);
        float lake_floor = lake_level - (1.5f + closed_basin_mask * 6.0f + (1.0f - wetness) * 1.5f);
        lake_floor = fminf(lake_floor, surface - 1.0f);
        surface = surface * (1.0f - lake_blend * 0.82f) + lake_floor * (lake_blend * 0.82f);
        base = fminf(base, surface - 1.0f - closed_basin_mask * 2.0f);
        if (water_level < lake_level) water_level = lake_level;
        floodplain_mask = fmaxf(floodplain_mask, lake_blend * 0.55f);
        sediment_thickness += lake_blend * (2.0f + closed_basin_mask * 3.0f);
    } else if (water_level > surface &&
               marine_weight < 0.20f &&
               river_strength < 0.22f &&
               closed_basin_mask < 0.24f) {
        water_level = surface;
    }

    if (ravine_mask > 0.08f || valley_mask > 0.16f) {
        float ravine_blend = sdk_worldgen_clampf(ravine_mask * 0.80f + valley_mask * 0.45f, 0.0f, 1.0f);
        float incision = ravine_depth * remap_smooth(ravine_blend, 0.08f, 0.92f) *
                         (0.82f + waterfall_mask * 0.35f);
        float width_factor = sdk_worldgen_clampf(ravine_width / 12.0f, 0.35f, 1.35f);
        float ravine_target = river_bed_height - incision;
        surface = surface * (1.0f - ravine_blend * 0.88f) + ravine_target * (ravine_blend * 0.88f);
        base -= incision * 0.16f * width_factor;
        river_bed_height = fminf(river_bed_height, surface - 1.0f);
        sediment_thickness *= 1.0f - ravine_blend * 0.28f;
        regolith_thickness *= 1.0f - ravine_blend * 0.22f;
    }

    if (vent_mask > 0.14f && volcanic_weight > 0.12f) {
        float vent_focus = sdk_worldgen_clampf(1.0f - vent_distance, 0.0f, 1.0f);
        float cone_gain = stratovolcano_mask * (12.0f + surface_relief * 1.5f + volcanic_weight * 18.0f);
        float shield_gain = shield_mask * (5.0f + volcanic_weight * 12.0f +
                                           sdk_worldgen_clampf(1.0f - valley_mask * 0.65f, 0.0f, 1.0f) * 6.0f);
        float fissure_gain = fissure_mask * (1.5f + lava_flow_bias * 5.0f);
        float crater_cut = caldera_mask * (4.0f + stratovolcano_mask * 18.0f + shield_mask * 9.0f);
        float flow_build = lava_flow_bias * (1.0f + vent_mask * 5.0f + fissure_mask * 3.0f);
        float ash_fill = ashfall_mask * 3.2f + ash_bias * 1.4f;

        surface += cone_gain + shield_gain + fissure_gain;
        base += cone_gain * 0.18f + shield_gain * 0.10f;
        surface += flow_build;
        surface -= crater_cut * (0.65f + vent_focus * 0.35f);
        surface += ash_fill;
        sediment_thickness += ashfall_mask * 2.4f + ash_bias * 1.1f;
        regolith_thickness += ashfall_mask * 1.5f + ash_bias * 0.8f;
        if (caldera_mask > 0.18f) {
            water_level = fminf(water_level, surface + 1.0f);
        }
    }

    if (river_strength > 0.08f) {
        float channel_mask = sdk_worldgen_clampf(river_strength * 0.88f +
                                                 remap_smooth(river_channel_width, 1.4f, 10.0f) * 0.22f +
                                                 valley_mask * 0.08f,
                                                 0.0f, 1.0f);
        float braid_variation = pseudo_noise3_local(wx, (int)lrintf(surface), wz,
                                                    impl->seed ^ 0xE373u,
                                                    0.037f,
                                                    2) * braid_mask * 1.4f;
        float target_surface = river_bed_height + 1.0f + river_order_f * 0.40f -
                               river_channel_depth * (0.35f + waterfall_mask * 0.25f) +
                               braid_variation;
        surface = surface * (1.0f - channel_mask * 0.90f) + target_surface * (channel_mask * 0.90f);
        base -= river_channel_depth * channel_mask * 0.10f;
        if (floodplain_mask > 0.08f) {
            float floodplain_surface = river_bed_height + 1.0f + river_order_f * 0.35f + braid_variation * 0.35f;
            surface = surface * (1.0f - floodplain_mask * 0.18f) + floodplain_surface * (floodplain_mask * 0.18f);
        }
        if (waterfall_mask > 0.16f) {
            surface -= waterfall_mask * (0.8f + river_channel_depth * 0.45f);
        }
    }

    surface = (float)sdk_worldgen_clampi((int)lrintf(surface), 4, CHUNK_HEIGHT - 8);
    base = (float)sdk_worldgen_clampi((int)lrintf(base), 3, (int)surface - 1);
    river_bed_height = (float)sdk_worldgen_clampi((int)lrintf(river_bed_height), 3, (int)surface);
    if (water_level > 0.0f && water_level <= surface && river_strength > 0.08f) {
        water_level = surface + 1.0f;
    }

    if (lake_mask > 0.24f && closed_basin_mask > 0.20f && water_level >= surface) {
        water_table_depth_f = fminf(water_table_depth_f, 1.0f);
        drainage_index = fminf(drainage_index, 0.26f);
    } else if (floodplain_mask > 0.18f && river_strength > 0.08f) {
        water_table_depth_f = fminf(water_table_depth_f, 3.0f);
        drainage_index = fminf(drainage_index, 0.40f);
    }

    out_profile->parent_material = (SdkParentMaterialClass)parent_material;
    out_profile->water_table_depth = (uint8_t)sdk_worldgen_clampi((int)lrintf(water_table_depth_f), 0, 15);
    out_profile->soil_salinity = (SdkSoilSalinityClass)classify_soil_salinity_local(soil_salinity_value);
    out_profile->soil_reaction = (SdkSoilReactionClass)classify_soil_reaction_local(soil_reaction_value, soil_salinity_value);
    out_profile->soil_fertility = (SdkSoilFertilityClass)classify_soil_fertility_local(soil_fertility_value);
    out_profile->drainage_class = (SdkDrainageClass)classify_drainage_local(drainage_index, water_table_depth_f);
    surface_sediment = refine_surface_sediment_for_state(terrain_province,
                                                         parent_material,
                                                         (uint8_t)out_profile->drainage_class,
                                                         (uint8_t)out_profile->soil_reaction,
                                                         (uint8_t)out_profile->soil_fertility,
                                                         surface_sediment,
                                                         moisture_band,
                                                         temperature_band,
                                                         surface,
                                                         water_level,
                                                         coast_signed_distance,
                                                         river_strength,
                                                         floodplain_mask,
                                                         lake_mask,
                                                         closed_basin_mask,
                                                         marine_weight,
                                                         impl->sea_level);

    out_profile->base_height = (int16_t)base;
    out_profile->surface_height = (int16_t)surface;
    out_profile->water_height = (int16_t)sdk_worldgen_clampi((int)lrintf(water_level), 0, CHUNK_HEIGHT - 1);
    out_profile->river_bed_height = (int16_t)river_bed_height;
    out_profile->soil_depth = (uint8_t)sdk_worldgen_clampi((int)lrintf(soil_depth), 0, 6);
    out_profile->sediment_depth = (uint8_t)sdk_worldgen_clampi((int)lrintf(sediment_thickness * 0.55f), 0, 16);
    out_profile->sediment_thickness = (uint8_t)sdk_worldgen_clampi((int)lrintf(sediment_thickness), 0, 24);
    out_profile->regolith_thickness = (uint8_t)sdk_worldgen_clampi((int)lrintf(regolith_thickness), 0, 16);
    out_profile->river_order = (uint8_t)sdk_worldgen_clampi((int)lrintf(bilerp4(s00->river_order, s10->river_order,
                                                                                 s01->river_order, s11->river_order,
                                                                                 tx, tz)), 0, 4);
    out_profile->floodplain_width = (uint8_t)sdk_worldgen_clampi((int)lrintf(bilerp4(s00->floodplain_width, s10->floodplain_width,
                                                                                      s01->floodplain_width, s11->floodplain_width,
                                                                                      tx, tz)), 0, 10);
    out_profile->terrain_province = (SdkTerrainProvince)terrain_province;
    out_profile->bedrock_province = (SdkBedrockProvince)bedrock_province;
    out_profile->temperature_band = (SdkTemperatureBand)temperature_band;
    out_profile->moisture_band = (SdkMoistureBand)moisture_band;
    out_profile->surface_sediment = (SdkSurfaceSediment)surface_sediment;
    out_profile->water_surface_class = (SdkSurfaceWaterClass)water_surface_class;
    landform_flags = landform_flags_for_state(surface,
                                              water_level,
                                              river_strength,
                                              river_channel_width,
                                              floodplain_mask,
                                              lake_mask,
                                              closed_basin_mask,
                                              ravine_mask,
                                              vent_mask,
                                              caldera_mask,
                                              lava_flow_bias,
                                              cave_entrance_mask);
    out_profile->landform_flags = landform_flags;
    {
        uint8_t derived_ecology = coarse_ecology_for_state((int)lrintf(surface), impl->sea_level,
                                                           terrain_province,
                                                           temperature_band,
                                                           moisture_band,
                                                           surface_sediment,
                                                           (uint8_t)out_profile->parent_material,
                                                           (uint8_t)out_profile->drainage_class,
                                                           (uint8_t)out_profile->soil_reaction,
                                                           (uint8_t)out_profile->soil_fertility,
                                                           (uint8_t)out_profile->soil_salinity,
                                                           (uint8_t)sdk_worldgen_clampi((int)lrintf(river_order_f), 0, 4),
                                                           coast_signed_distance,
                                                           river_strength,
                                                           wetness,
                                                           ashfall_mask,
                                                           landform_flags,
                                                           out_profile->water_table_depth);
        out_profile->ecology = (SdkBiomeEcology)((derived_ecology == ECOLOGY_BARREN && surface > (float)impl->sea_level)
            ? ecology
            : derived_ecology);
    }
    out_profile->resource_province = (SdkResourceProvince)resource_province;
    out_profile->hydrocarbon_class = (SdkHydrocarbonClass)hydrocarbon_class;
    out_profile->resource_grade = (uint8_t)sdk_worldgen_clampi((int)lrintf(resource_grade_f), 0, 255);
    if (out_profile->resource_province != RESOURCE_PROVINCE_OIL_FIELD) {
        out_profile->hydrocarbon_class = SDK_HYDROCARBON_NONE;
    }
    if (out_profile->water_height > out_profile->surface_height && out_profile->water_surface_class == SURFACE_WATER_NONE) {
        if (temperature_band <= TEMP_POLAR) {
            out_profile->water_surface_class = SURFACE_WATER_PERENNIAL_ICE;
        } else if (temperature_band <= TEMP_SUBPOLAR) {
            out_profile->water_surface_class = SURFACE_WATER_SEASONAL_ICE;
        } else {
            out_profile->water_surface_class = SURFACE_WATER_OPEN;
        }
    }
    if (out_profile->water_height <= out_profile->surface_height) {
        out_profile->water_surface_class = SURFACE_WATER_NONE;
    }
}

