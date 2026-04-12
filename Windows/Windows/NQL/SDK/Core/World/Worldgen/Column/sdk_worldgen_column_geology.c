/**
 * sdk_worldgen_column_geology.c -- Stratigraphy, resources, and cave carving helpers.
 */
#include "sdk_worldgen_column_internal.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
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

BlockType rock_block_for_profile(const SdkTerrainColumnProfile* profile, int wx, int wy, int wz)
{
    float roll = sdk_worldgen_hashf(sdk_worldgen_hash2d(wx + wy * 17, wz - wy * 11, 0x4F1Du));

    switch (profile->bedrock_province) {
        case BEDROCK_PROVINCE_OCEANIC_BASALT:
            return BLOCK_BASALT;
        case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:
            return (roll > 0.55f) ? BLOCK_GNEISS : BLOCK_GRANITE;
        case BEDROCK_PROVINCE_METAMORPHIC_BELT:
            return (roll > 0.45f) ? BLOCK_SCHIST : BLOCK_GNEISS;
        case BEDROCK_PROVINCE_GRANITIC_INTRUSIVE:
            return BLOCK_GRANITE;
        case BEDROCK_PROVINCE_SILICICLASTIC_BASIN:
            return (roll > 0.55f) ? BLOCK_SHALE : BLOCK_SANDSTONE;
        case BEDROCK_PROVINCE_CARBONATE_PLATFORM:
            return (roll > 0.60f) ? BLOCK_DOLOSTONE : BLOCK_LIMESTONE;
        case BEDROCK_PROVINCE_RIFT_SEDIMENTARY:
            return (roll > 0.72f) ? BLOCK_BASALT : ((roll > 0.40f) ? BLOCK_SHALE : BLOCK_SANDSTONE);
        case BEDROCK_PROVINCE_VOLCANIC_ARC:
            return (roll > 0.45f) ? BLOCK_VOLCANIC_ROCK : BLOCK_BASALT;
        case BEDROCK_PROVINCE_FLOOD_BASALT:
            return BLOCK_BASALT;
        default:
            return BLOCK_STONE;
    }
}

BlockType soil_block_for_profile(const SdkTerrainColumnProfile* profile)
{
    if (profile->water_height > profile->surface_height) {
        if (profile->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN) return BLOCK_MARINE_MUD;
        if (profile->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF) return BLOCK_MARINE_SAND;
        if (profile->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) return BLOCK_BEACH_GRAVEL;
    }

    switch (profile->surface_sediment) {
        case SEDIMENT_PEAT: return BLOCK_PEAT;
        case SEDIMENT_BEACH_SAND:
        case SEDIMENT_EOILIAN_SAND: return BLOCK_SAND;
        case SEDIMENT_DELTAIC_SILT: return BLOCK_SILT;
        case SEDIMENT_LACUSTRINE_CLAY: return BLOCK_CLAY;
        case SEDIMENT_COARSE_ALLUVIUM: return BLOCK_COARSE_ALLUVIUM;
        case SEDIMENT_FINE_ALLUVIUM: return BLOCK_FINE_ALLUVIUM;
        case SEDIMENT_COLLUVIUM: return BLOCK_COLLUVIUM;
        case SEDIMENT_TALUS: return BLOCK_TALUS;
        case SEDIMENT_VOLCANIC_ASH: return BLOCK_TUFF;
        case SEDIMENT_MARINE_MUD: return BLOCK_MARINE_MUD;
        case SEDIMENT_MARINE_SAND: return BLOCK_MARINE_SAND;
        case SEDIMENT_LOESS: return BLOCK_LOESS;
        case SEDIMENT_CALCAREOUS_RESIDUAL: return BLOCK_CALCAREOUS_SOIL;
        case SEDIMENT_SAPROLITE: return BLOCK_SAPROLITE;
        default:
            if (profile->terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND) return BLOCK_WETLAND_SOD;
            if (sdk_worldgen_ecology_is_wetland(profile->ecology)) return BLOCK_WETLAND_SOD;
            if (profile->ecology == ECOLOGY_HOT_DESERT || profile->ecology == ECOLOGY_DUNE_COAST) return BLOCK_SAND;
            if (profile->ecology == ECOLOGY_VOLCANIC_BARRENS) return BLOCK_TUFF;
            if (sdk_worldgen_ecology_prefers_turf(profile->ecology)) return BLOCK_TURF;
            return BLOCK_TOPSOIL;
    }
}

BlockType subsoil_block_for_profile(const SdkTerrainColumnProfile* profile)
{
    if (profile->water_height > profile->surface_height) {
        if (profile->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN) return BLOCK_MARINE_MUD;
        if (profile->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF) return BLOCK_MARINE_SAND;
        if (profile->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) return BLOCK_BEACH_GRAVEL;
    }

    switch (profile->surface_sediment) {
        case SEDIMENT_PEAT: return BLOCK_PEAT;
        case SEDIMENT_BEACH_SAND:
        case SEDIMENT_EOILIAN_SAND: return BLOCK_SAND;
        case SEDIMENT_DELTAIC_SILT: return BLOCK_SILT;
        case SEDIMENT_LACUSTRINE_CLAY: return BLOCK_CLAY;
        case SEDIMENT_COARSE_ALLUVIUM: return BLOCK_COARSE_ALLUVIUM;
        case SEDIMENT_FINE_ALLUVIUM: return BLOCK_FINE_ALLUVIUM;
        case SEDIMENT_COLLUVIUM: return BLOCK_COLLUVIUM;
        case SEDIMENT_TALUS: return BLOCK_TALUS;
        case SEDIMENT_VOLCANIC_ASH: return BLOCK_TUFF;
        case SEDIMENT_MARINE_MUD: return BLOCK_MARINE_MUD;
        case SEDIMENT_MARINE_SAND: return BLOCK_MARINE_SAND;
        case SEDIMENT_LOESS: return BLOCK_LOESS;
        case SEDIMENT_CALCAREOUS_RESIDUAL: return BLOCK_CALCAREOUS_SOIL;
        case SEDIMENT_SAPROLITE: return BLOCK_SAPROLITE;
        case SEDIMENT_RESIDUAL_SOIL:
            if (profile->soil_reaction >= SOIL_REACTION_CALCAREOUS ||
                profile->bedrock_province == BEDROCK_PROVINCE_CARBONATE_PLATFORM) {
                return BLOCK_CALCAREOUS_SOIL;
            }
            if (profile->parent_material == PARENT_MATERIAL_GRANITIC ||
                profile->parent_material == PARENT_MATERIAL_METAMORPHIC ||
                profile->parent_material == PARENT_MATERIAL_MAFIC_VOLCANIC ||
                profile->parent_material == PARENT_MATERIAL_INTERMEDIATE_VOLCANIC) {
                return BLOCK_SAPROLITE;
            }
            return BLOCK_SUBSOIL;
        default: return BLOCK_SUBSOIL;
    }
}

BlockType sdk_worldgen_visual_surface_block_for_profile(const SdkTerrainColumnProfile* profile)
{
    if (!profile) return BLOCK_AIR;

    if (profile->water_height > profile->surface_height) {
        if (profile->water_surface_class == SURFACE_WATER_PERENNIAL_ICE) return BLOCK_SEA_ICE;
        if (profile->water_surface_class == SURFACE_WATER_SEASONAL_ICE) return BLOCK_ICE;
        return BLOCK_WATER;
    }

    if (profile->ecology == ECOLOGY_NIVAL_ICE || profile->terrain_province == TERRAIN_PROVINCE_ALPINE_BELT) {
        return BLOCK_SNOW;
    }

    switch (profile->surface_sediment) {
        case SEDIMENT_COLLUVIUM:       return BLOCK_COLLUVIUM;
        case SEDIMENT_TALUS:           return BLOCK_TALUS;
        case SEDIMENT_COARSE_ALLUVIUM: return BLOCK_COARSE_ALLUVIUM;
        case SEDIMENT_FINE_ALLUVIUM:   return BLOCK_FINE_ALLUVIUM;
        case SEDIMENT_DELTAIC_SILT:    return BLOCK_TIDAL_SILT;
        case SEDIMENT_LACUSTRINE_CLAY: return BLOCK_CLAY;
        case SEDIMENT_BEACH_SAND:
        case SEDIMENT_EOILIAN_SAND:    return BLOCK_SAND;
        case SEDIMENT_PEAT:            return BLOCK_PEAT;
        case SEDIMENT_VOLCANIC_ASH:    return BLOCK_TUFF;
        case SEDIMENT_MARINE_MUD:      return BLOCK_MARINE_MUD;
        case SEDIMENT_MARINE_SAND:     return BLOCK_MARINE_SAND;
        case SEDIMENT_LOESS:           return BLOCK_LOESS;
        case SEDIMENT_CALCAREOUS_RESIDUAL:
                                          return BLOCK_CALCAREOUS_SOIL;
        case SEDIMENT_SAPROLITE:       return BLOCK_SAPROLITE;
        case SEDIMENT_RESIDUAL_SOIL:
        case SEDIMENT_NONE:
        default:                       break;
    }

    switch (profile->terrain_province) {
        case TERRAIN_PROVINCE_PEAT_WETLAND:
            return BLOCK_WETLAND_SOD;
        case TERRAIN_PROVINCE_DYNAMIC_COAST:
            return BLOCK_BEACH_GRAVEL;
        case TERRAIN_PROVINCE_OPEN_OCEAN:
            return BLOCK_MARINE_MUD;
        case TERRAIN_PROVINCE_CONTINENTAL_SHELF:
            return BLOCK_MARINE_SAND;
        case TERRAIN_PROVINCE_VOLCANIC_ARC:
            return BLOCK_VOLCANIC_ROCK;
        case TERRAIN_PROVINCE_BASALT_PLATEAU:
            return BLOCK_BASALT;
        case TERRAIN_PROVINCE_HARDROCK_HIGHLAND:
        case TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT:
            return rock_block_for_profile(profile, 0, 0, 0);
        case TERRAIN_PROVINCE_ARID_FAN_STEPPE:
            return BLOCK_SAND;
        default:
            return soil_block_for_profile(profile);
    }
}

int sdk_worldgen_profile_is_passive_spawn_habitat(const SdkTerrainColumnProfile* profile, int relief)
{
    if (!profile) return 0;
    if (profile->water_height > profile->surface_height) return 0;
    if (relief > 5) return 0;
    if (!sdk_worldgen_ecology_supports_passive_fauna(profile->ecology)) return 0;

    switch (profile->temperature_band) {
        case TEMP_COOL_TEMPERATE:
        case TEMP_WARM_TEMPERATE:
        case TEMP_SUBTROPICAL:
            break;
        default:
            return 0;
    }

    switch (profile->terrain_province) {
        case TERRAIN_PROVINCE_OPEN_OCEAN:
        case TERRAIN_PROVINCE_CONTINENTAL_SHELF:
        case TERRAIN_PROVINCE_PEAT_WETLAND:
        case TERRAIN_PROVINCE_ALPINE_BELT:
        case TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT:
        case TERRAIN_PROVINCE_VOLCANIC_ARC:
            return 0;
        default:
            return 1;
    }
}

int sdk_worldgen_score_spawn_candidate_profile(int sea_level,
                                               const SdkTerrainColumnProfile* profile,
                                               int relief)
{
    static const int k_spawn_province_scores[] = {
        INT_MIN, /* OPEN_OCEAN */
        INT_MIN, /* CONTINENTAL_SHELF */
        INT_MIN, /* DYNAMIC_COAST */
        INT_MIN, /* ESTUARY_DELTA */
        18,      /* FLOODPLAIN_ALLUVIAL_LOWLAND */
        INT_MIN, /* PEAT_WETLAND */
        42,      /* SILICICLASTIC_HILLS */
        42,      /* CARBONATE_UPLAND */
        42,      /* HARDROCK_HIGHLAND */
        42,      /* UPLIFTED_PLATEAU */
        24,      /* RIFT_VALLEY */
        INT_MIN, /* FOLD_MOUNTAIN_BELT */
        INT_MIN, /* VOLCANIC_ARC */
        30,      /* BASALT_PLATEAU */
        24,      /* ARID_FAN_STEPPE */
        INT_MIN  /* ALPINE_BELT */
    };
    int score;

    if (!profile) return INT_MIN;
    if (profile->water_height > profile->surface_height) return INT_MIN;
    if ((unsigned)profile->terrain_province >= (sizeof(k_spawn_province_scores) / sizeof(k_spawn_province_scores[0]))) {
        return INT_MIN;
    }

    score = k_spawn_province_scores[profile->terrain_province];
    if (score == INT_MIN) return INT_MIN;

    if (profile->surface_height <= sea_level + 3) {
        score -= 40;
    } else if (profile->surface_height <= sea_level + 24) {
        score += 8;
    } else if (profile->surface_height <= sea_level + 96) {
        score += 20;
    } else {
        score -= 10;
    }

    if (relief <= 2) score += 24;
    else if (relief <= 4) score += 12;
    else if (relief <= 7) score += 2;
    else score -= relief * 8;

    switch (profile->temperature_band) {
        case TEMP_COOL_TEMPERATE:
        case TEMP_WARM_TEMPERATE:
            score += 18;
            break;
        case TEMP_SUBTROPICAL:
            score += 10;
            break;
        case TEMP_SUBPOLAR:
        case TEMP_TROPICAL:
            score -= 4;
            break;
        case TEMP_POLAR:
            score -= 30;
            break;
        default:
            break;
    }

    switch (profile->moisture_band) {
        case MOISTURE_SUBHUMID:
        case MOISTURE_HUMID:
            score += 16;
            break;
        case MOISTURE_SEMI_ARID:
            score += 4;
            break;
        case MOISTURE_PERHUMID:
            score -= 6;
            break;
        case MOISTURE_WATERLOGGED:
            score -= 40;
            break;
        default:
            break;
    }

    return score;
}

static SdkStratigraphyProvince stratigraphy_province_for_profile(const SdkTerrainColumnProfile* profile)
{
    if (!profile) return SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN;

    switch (profile->bedrock_province) {
        case BEDROCK_PROVINCE_OCEANIC_BASALT:
            return SDK_STRAT_PROVINCE_OCEANIC;
        case BEDROCK_PROVINCE_CARBONATE_PLATFORM:
            return SDK_STRAT_PROVINCE_CARBONATE_SHELF;
        case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:
        case BEDROCK_PROVINCE_METAMORPHIC_BELT:
        case BEDROCK_PROVINCE_GRANITIC_INTRUSIVE:
            return SDK_STRAT_PROVINCE_HARDROCK_BASEMENT;
        case BEDROCK_PROVINCE_RIFT_SEDIMENTARY:
            return SDK_STRAT_PROVINCE_RIFT_BASIN;
        case BEDROCK_PROVINCE_VOLCANIC_ARC:
            return SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX;
        case BEDROCK_PROVINCE_FLOOD_BASALT:
            return SDK_STRAT_PROVINCE_FLOOD_BASALT;
        default:
            return SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN;
    }
}

static BlockType regolith_block_for_profile(const SdkTerrainColumnProfile* profile)
{
    if (!profile) return BLOCK_SUBSOIL;

    if (profile->water_height > profile->surface_height) {
        if (profile->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN) return BLOCK_MARINE_MUD;
        if (profile->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF) return BLOCK_MARINE_SAND;
    }

    switch (profile->surface_sediment) {
        case SEDIMENT_MARINE_MUD: return BLOCK_MARINE_MUD;
        case SEDIMENT_MARINE_SAND: return BLOCK_MARINE_SAND;
        case SEDIMENT_LOESS: return BLOCK_LOESS;
        case SEDIMENT_CALCAREOUS_RESIDUAL: return BLOCK_CALCAREOUS_SOIL;
        case SEDIMENT_SAPROLITE: return BLOCK_SAPROLITE;
        case SEDIMENT_VOLCANIC_ASH: return BLOCK_TUFF;
        default: break;
    }

    switch (stratigraphy_province_for_profile(profile)) {
        case SDK_STRAT_PROVINCE_CARBONATE_SHELF:
            return BLOCK_CALCAREOUS_SOIL;
        case SDK_STRAT_PROVINCE_HARDROCK_BASEMENT:
            if (profile->surface_sediment == SEDIMENT_TALUS) return BLOCK_TALUS;
            if (profile->surface_sediment == SEDIMENT_COLLUVIUM) return BLOCK_COLLUVIUM;
            if (profile->drainage_class <= DRAINAGE_MODERATE) return BLOCK_SAPROLITE;
            return BLOCK_COLLUVIUM;
        case SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX:
            return (profile->surface_sediment == SEDIMENT_SAPROLITE) ? BLOCK_SAPROLITE : BLOCK_TUFF;
        case SDK_STRAT_PROVINCE_FLOOD_BASALT:
            return (profile->surface_sediment == SEDIMENT_SAPROLITE) ? BLOCK_SAPROLITE : BLOCK_LOESS;
        case SDK_STRAT_PROVINCE_OCEANIC:
            return BLOCK_MARINE_MUD;
        default:
            if (profile->surface_sediment == SEDIMENT_EOILIAN_SAND) return BLOCK_LOESS;
            return BLOCK_SUBSOIL;
    }
}

float geology_band_weight(float distance, float inner, float outer)
{
    float ad = fabsf(distance);
    if (ad <= inner) return 1.0f;
    if (ad >= outer) return 0.0f;
    return 1.0f - remap_smooth(ad, inner, outer);
}

static BlockType coherent_basement_block(const SdkTerrainColumnProfile* profile,
                                         const SdkRegionFieldSample* geology,
                                         int wx, int wz)
{
    float t = geology ? geology->stratigraphy_control : 0.5f;

    if (!profile) return BLOCK_STONE;

    switch (profile->bedrock_province) {
        case BEDROCK_PROVINCE_OCEANIC_BASALT:
            return BLOCK_BASALT;
        case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:
            return (t > 0.56f) ? BLOCK_GNEISS : BLOCK_GRANITE;
        case BEDROCK_PROVINCE_METAMORPHIC_BELT:
            if (geology && geology->fault_mask > 0.70f) return BLOCK_SCHIST;
            return (t > 0.48f) ? BLOCK_SCHIST : BLOCK_GNEISS;
        case BEDROCK_PROVINCE_GRANITIC_INTRUSIVE:
            return (geology && geology->fault_mask > 0.82f) ? BLOCK_VEIN_QUARTZ : BLOCK_GRANITE;
        case BEDROCK_PROVINCE_SILICICLASTIC_BASIN:
            if (geology && geology->fault_mask > 0.64f) return BLOCK_GNEISS;
            return (t > 0.58f) ? BLOCK_GNEISS : BLOCK_GRANITE;
        case BEDROCK_PROVINCE_CARBONATE_PLATFORM:
            if (geology && geology->fault_mask > 0.62f) return BLOCK_GNEISS;
            return (geology && geology->carbonate_purity > 0.72f) ? BLOCK_GRANITE : BLOCK_GNEISS;
        case BEDROCK_PROVINCE_RIFT_SEDIMENTARY:
            if (geology && geology->vent_bias > 0.58f) return BLOCK_BASALT;
            return (t > 0.52f) ? BLOCK_GNEISS : BLOCK_GRANITE;
        case BEDROCK_PROVINCE_VOLCANIC_ARC:
            if (geology && geology->vent_bias > 0.54f) return BLOCK_ANDESITE;
            return BLOCK_BASALT;
        case BEDROCK_PROVINCE_FLOOD_BASALT:
            return BLOCK_BASALT;
        default:
            return rock_block_for_profile(profile, wx, 32, wz);
    }
}

static BlockType deep_basement_block_for_profile(const SdkTerrainColumnProfile* profile,
                                                 const SdkRegionFieldSample* geology,
                                                 int wx, int wz)
{
    float t = geology ? geology->stratigraphy_control : 0.5f;

    (void)wx;
    (void)wz;

    if (!profile) return BLOCK_STONE;

    switch (profile->bedrock_province) {
        case BEDROCK_PROVINCE_OCEANIC_BASALT:
            return BLOCK_BASALT;
        case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:
            return (t > 0.40f) ? BLOCK_GNEISS : BLOCK_GRANITE;
        case BEDROCK_PROVINCE_METAMORPHIC_BELT:
            return (geology && geology->fault_mask > 0.56f) ? BLOCK_SCHIST : BLOCK_GNEISS;
        case BEDROCK_PROVINCE_GRANITIC_INTRUSIVE:
            return BLOCK_GRANITE;
        case BEDROCK_PROVINCE_SILICICLASTIC_BASIN:
            return (geology && geology->fault_mask > 0.56f) ? BLOCK_GNEISS : BLOCK_GRANITE;
        case BEDROCK_PROVINCE_CARBONATE_PLATFORM:
            return (geology && geology->fault_mask > 0.60f) ? BLOCK_GNEISS : BLOCK_GRANITE;
        case BEDROCK_PROVINCE_RIFT_SEDIMENTARY:
            return (geology && geology->vent_bias > 0.68f) ? BLOCK_BASALT : BLOCK_GNEISS;
        case BEDROCK_PROVINCE_VOLCANIC_ARC:
            return (geology && geology->vent_bias > 0.48f) ? BLOCK_ANDESITE : BLOCK_BASALT;
        case BEDROCK_PROVINCE_FLOOD_BASALT:
            return BLOCK_BASALT;
        default:
            return rock_block_for_profile(profile, wx, 16, wz);
    }
}

static BlockType weathered_block_for_profile(const SdkTerrainColumnProfile* profile,
                                             const SdkRegionFieldSample* geology)
{
    if (!profile) return BLOCK_SUBSOIL;
    if (profile->water_height > profile->surface_height) {
        if (profile->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN) return BLOCK_MARINE_MUD;
        if (profile->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF) return BLOCK_MARINE_SAND;
    }
    if (profile->bedrock_province == BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS ||
        profile->bedrock_province == BEDROCK_PROVINCE_METAMORPHIC_BELT ||
        profile->bedrock_province == BEDROCK_PROVINCE_GRANITIC_INTRUSIVE) {
        if (profile->surface_sediment == SEDIMENT_TALUS) return BLOCK_TALUS;
        if (profile->surface_sediment == SEDIMENT_COLLUVIUM) return BLOCK_COLLUVIUM;
        if (profile->surface_sediment == SEDIMENT_SAPROLITE) return BLOCK_SAPROLITE;
        return (geology && geology->wetness > 0.46f) ? BLOCK_SAPROLITE : BLOCK_COLLUVIUM;
    }
    if (profile->bedrock_province == BEDROCK_PROVINCE_CARBONATE_PLATFORM) {
        if (profile->surface_sediment == SEDIMENT_CALCAREOUS_RESIDUAL) return BLOCK_CALCAREOUS_SOIL;
        return BLOCK_CALCAREOUS_SOIL;
    }
    if (profile->bedrock_province == BEDROCK_PROVINCE_VOLCANIC_ARC) {
        if (profile->surface_sediment == SEDIMENT_SAPROLITE) return BLOCK_SAPROLITE;
        return BLOCK_TUFF;
    }
    if (profile->bedrock_province == BEDROCK_PROVINCE_FLOOD_BASALT) {
        if (profile->surface_sediment == SEDIMENT_LOESS ||
            profile->surface_sediment == SEDIMENT_EOILIAN_SAND) {
            return BLOCK_LOESS;
        }
        return BLOCK_SAPROLITE;
    }
    return regolith_block_for_profile(profile);
}

void build_strata_column(SdkWorldGen* wg, const SdkTerrainColumnProfile* profile,
                                const SdkRegionFieldSample* geology,
                                int wx, int wz, int surface_y, SdkStrataColumn* out_column)
{
    SdkStratigraphyProvince province;
    float weathered_base;
    float upper_top;
    float lower_top;
    float basement_top;
    float deep_basement_top;
    float control;

    (void)wg;

    if (!profile || !out_column) return;

    province = stratigraphy_province_for_profile(profile);
    control = geology ? geology->stratigraphy_control : 0.5f;

    memset(out_column, 0, sizeof(*out_column));
    out_column->fault_block = BLOCK_BRECCIA;
    out_column->vein_block = BLOCK_VEIN_QUARTZ;
    out_column->regolith_block = weathered_block_for_profile(profile, geology);
    out_column->basement_block = coherent_basement_block(profile, geology, wx, wz);
    out_column->deep_basement_block = deep_basement_block_for_profile(profile, geology, wx, wz);

    switch (province) {
        case SDK_STRAT_PROVINCE_OCEANIC:
            out_column->lower_block = BLOCK_BASALT;
            out_column->upper_block = BLOCK_MARINE_MUD;
            break;
        case SDK_STRAT_PROVINCE_CARBONATE_SHELF:
            out_column->lower_block = (geology && geology->carbonate_purity > 0.56f) ? BLOCK_LIMESTONE : BLOCK_MARL;
            out_column->upper_block = (geology && geology->carbonate_purity > 0.74f) ? BLOCK_CHALK : BLOCK_DOLOSTONE;
            break;
        case SDK_STRAT_PROVINCE_HARDROCK_BASEMENT:
            out_column->lower_block = out_column->basement_block;
            out_column->upper_block = (profile->surface_sediment == SEDIMENT_TALUS) ? BLOCK_TALUS :
                                      ((profile->surface_sediment == SEDIMENT_COLLUVIUM) ? BLOCK_COLLUVIUM : out_column->basement_block);
            break;
        case SDK_STRAT_PROVINCE_RIFT_BASIN:
            out_column->lower_block = (geology && geology->basin_axis_weight > 0.56f) ? BLOCK_MUDSTONE : BLOCK_SHALE;
            out_column->upper_block = (geology && geology->channel_sand_bias > 0.64f) ? BLOCK_SANDSTONE : BLOCK_SILTSTONE;
            break;
        case SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX:
            out_column->lower_block = (geology && geology->vent_bias > 0.56f) ? BLOCK_ANDESITE : BLOCK_VOLCANIC_ROCK;
            out_column->upper_block = (geology && geology->vent_bias > 0.72f) ? BLOCK_SCORIA : BLOCK_TUFF;
            break;
        case SDK_STRAT_PROVINCE_FLOOD_BASALT:
            out_column->lower_block = BLOCK_BASALT;
            out_column->upper_block = (geology && geology->basin_axis_weight > 0.52f) ? BLOCK_SILTSTONE : BLOCK_BASALT;
            break;
        case SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN:
        default:
            out_column->lower_block = (geology && geology->basin_axis_weight > 0.54f) ? BLOCK_MUDSTONE : BLOCK_SHALE;
            out_column->upper_block = (geology && geology->channel_sand_bias > 0.62f) ? BLOCK_SANDSTONE : BLOCK_SILTSTONE;
            break;
    }

    if (geology) {
        weathered_base = geology->weathered_base;
        upper_top = geology->upper_top;
        lower_top = geology->lower_top;
        basement_top = geology->basement_top;
        deep_basement_top = geology->deep_basement_top;
    } else {
        int soil_cover = (int)profile->soil_depth + (int)profile->sediment_depth;
        int weathered_thickness = sdk_worldgen_clampi((int)profile->regolith_thickness, 2, 16);
        int fallback_upper = 18 + (int)(control * 18.0f);
        int fallback_lower = 32 + (int)(control * 30.0f);
        int fallback_deep = 72 + (int)(control * 40.0f);
        weathered_base = (float)(surface_y - soil_cover - weathered_thickness);
        upper_top = weathered_base - (float)fallback_upper;
        lower_top = upper_top - (float)fallback_lower;
        basement_top = lower_top - (12.0f + control * 10.0f);
        deep_basement_top = basement_top - (float)fallback_deep;
    }

    {
        int upper_hi;
        int lower_hi;
        int basement_hi;
        int deep_hi;

        out_column->weathered_base = (int16_t)sdk_worldgen_clampi((int)lrintf(weathered_base), 2, surface_y - 1);
        upper_hi = out_column->weathered_base - 1;
        if (upper_hi < 2) upper_hi = 2;
        out_column->upper_top = (int16_t)sdk_worldgen_clampi((int)lrintf(upper_top), 2, upper_hi);
        lower_hi = out_column->upper_top - 1;
        if (lower_hi < 2) lower_hi = 2;
        out_column->lower_top = (int16_t)sdk_worldgen_clampi((int)lrintf(lower_top), 2, lower_hi);
        basement_hi = out_column->lower_top - 1;
        if (basement_hi < 2) basement_hi = 2;
        out_column->basement_top = (int16_t)sdk_worldgen_clampi((int)lrintf(basement_top), 2, basement_hi);
        deep_hi = out_column->basement_top - 1;
        if (deep_hi < 2) deep_hi = 2;
        out_column->deep_basement_top = (int16_t)sdk_worldgen_clampi((int)lrintf(deep_basement_top), 2, deep_hi);
    }
}

BlockType stratigraphic_block_for_y(const SdkTerrainColumnProfile* profile,
                                           const SdkRegionFieldSample* geology,
                                           const SdkStrataColumn* strata,
                                           int wx, int wy, int wz)
{
    SdkStratigraphyProvince province;
    float zone_t;
    float contact_weight;
    float primary_phase;
    float secondary_phase;
    float basement_phase;
    float deep_phase;
    int primary_band;
    int secondary_band;
    int basement_band;
    int deep_band;

    if (!profile || !geology || !strata) return BLOCK_STONE;

    province = stratigraphy_province_for_profile(profile);
    primary_phase = strata_structural_phase(geology, wx, wy, wz, 0.029f, 1.05f, 0x5A11u);
    secondary_phase = strata_structural_phase(geology, wx, wy, wz, 0.017f, 0.82f, 0x5A22u);
    basement_phase = strata_structural_phase(geology, wx, wy, wz, 0.011f, 0.58f, 0x5A33u);
    deep_phase = strata_structural_phase(geology, wx, wy, wz, 0.0065f, 0.38f, 0x5A44u);
    primary_band = strata_band_index(primary_phase + geology->channel_sand_bias * 11.0f + geology->fault_mask * 4.0f, 4.6f, 4);
    secondary_band = strata_band_index(secondary_phase + geology->basin_axis_weight * 13.0f + geology->carbonate_purity * 9.0f, 6.4f, 4);
    basement_band = strata_band_index(basement_phase + geology->fault_mask * 3.0f +
                                      geology->stratigraphy_control * 4.0f, 12.0f, 3);
    deep_band = strata_band_index(deep_phase + geology->fault_mask * 2.0f +
                                  geology->vent_bias * 4.0f, 20.0f, 2);

    if (wy > strata->weathered_base) {
        return strata->regolith_block;
    }

    if (wy > strata->upper_top) {
        zone_t = (float)(wy - strata->upper_top) / fmaxf(1.0f, (float)(strata->weathered_base - strata->upper_top));
        switch (province) {
            case SDK_STRAT_PROVINCE_OCEANIC:
                if (geology->channel_sand_bias > 0.66f && primary_band == 0 && zone_t < 0.65f) return BLOCK_MARINE_SAND;
                return (secondary_band == 0 && zone_t > 0.55f) ? BLOCK_TIDAL_SILT : BLOCK_MARINE_MUD;
            case SDK_STRAT_PROVINCE_CARBONATE_SHELF:
                if (geology->carbonate_purity > 0.82f && zone_t > 0.58f && primary_band == 0) return BLOCK_CHALK;
                switch (primary_band) {
                    case 0: return BLOCK_LIMESTONE;
                    case 1: return (geology->carbonate_purity > 0.52f) ? BLOCK_DOLOSTONE : BLOCK_LIMESTONE;
                    case 2: return BLOCK_MARL;
                    default: return (geology->carbonate_purity > 0.66f) ? BLOCK_LIMESTONE : BLOCK_MARL;
                }
            case SDK_STRAT_PROVINCE_HARDROCK_BASEMENT:
                return (zone_t > 0.74f && profile->surface_sediment == SEDIMENT_TALUS) ? BLOCK_TALUS : strata->upper_block;
            case SDK_STRAT_PROVINCE_RIFT_BASIN:
                if (geology->channel_sand_bias > 0.72f && zone_t > 0.28f && zone_t < 0.78f) {
                    return (primary_band == 0) ? BLOCK_CONGLOMERATE : BLOCK_SANDSTONE;
                }
                switch (primary_band) {
                    case 0: return BLOCK_SANDSTONE;
                    case 1: return BLOCK_SILTSTONE;
                    case 2: return BLOCK_MUDSTONE;
                    default: return BLOCK_SHALE;
                }
            case SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX:
                if (geology->vent_bias > 0.74f && primary_band == 0 && zone_t > 0.40f) return BLOCK_SCORIA;
                switch (primary_band) {
                    case 0: return BLOCK_TUFF;
                    case 1: return BLOCK_VOLCANIC_ROCK;
                    case 2: return BLOCK_ANDESITE;
                    default: return BLOCK_TUFF;
                }
            case SDK_STRAT_PROVINCE_FLOOD_BASALT:
                switch (primary_band) {
                    case 0: return BLOCK_SILTSTONE;
                    case 1: return BLOCK_TUFF;
                    case 2: return BLOCK_ANDESITE;
                    default: return BLOCK_BASALT;
                }
            case SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN:
            default:
                if (geology->channel_sand_bias > 0.70f && zone_t > 0.25f && zone_t < 0.78f) {
                    return (primary_band == 0) ? BLOCK_CONGLOMERATE : BLOCK_SANDSTONE;
                }
                switch (primary_band) {
                    case 0: return BLOCK_SANDSTONE;
                    case 1: return BLOCK_SILTSTONE;
                    case 2: return BLOCK_MUDSTONE;
                    default: return BLOCK_SHALE;
                }
        }
    }

    if (wy > strata->lower_top) {
        zone_t = (float)(wy - strata->lower_top) / fmaxf(1.0f, (float)(strata->upper_top - strata->lower_top));
        switch (province) {
            case SDK_STRAT_PROVINCE_OCEANIC:
                return (zone_t > 0.82f && secondary_band == 0) ? BLOCK_MARINE_MUD : BLOCK_BASALT;
            case SDK_STRAT_PROVINCE_CARBONATE_SHELF:
                switch (secondary_band) {
                    case 0: return BLOCK_LIMESTONE;
                    case 1: return (geology->carbonate_purity > 0.50f) ? BLOCK_DOLOSTONE : BLOCK_LIMESTONE;
                    case 2: return BLOCK_MARL;
                    default: return BLOCK_LIMESTONE;
                }
            case SDK_STRAT_PROVINCE_HARDROCK_BASEMENT:
                if (profile->bedrock_province == BEDROCK_PROVINCE_METAMORPHIC_BELT) {
                    return (secondary_band == 0 || secondary_band == 3) ? BLOCK_SCHIST : BLOCK_GNEISS;
                }
                if (profile->bedrock_province == BEDROCK_PROVINCE_GRANITIC_INTRUSIVE) {
                    return (secondary_band == 0) ? BLOCK_GNEISS : BLOCK_GRANITE;
                }
                return (secondary_band == 0) ? BLOCK_GRANITE : ((secondary_band == 1) ? BLOCK_GNEISS : strata->lower_block);
            case SDK_STRAT_PROVINCE_RIFT_BASIN:
                if (geology->fault_mask > 0.60f && secondary_band == 0) return BLOCK_BRECCIA;
                switch (secondary_band) {
                    case 0: return (geology->channel_sand_bias > 0.58f) ? BLOCK_SANDSTONE : BLOCK_SILTSTONE;
                    case 1: return BLOCK_SILTSTONE;
                    case 2: return (geology->basin_axis_weight > 0.50f) ? BLOCK_MUDSTONE : BLOCK_SHALE;
                    default: return (geology->basin_axis_weight > 0.62f) ? BLOCK_SHALE : BLOCK_MUDSTONE;
                }
            case SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX:
                switch (secondary_band) {
                    case 0: return (geology->vent_bias > 0.56f) ? BLOCK_ANDESITE : BLOCK_VOLCANIC_ROCK;
                    case 1: return BLOCK_BASALT;
                    case 2: return BLOCK_VOLCANIC_ROCK;
                    default: return BLOCK_TUFF;
                }
            case SDK_STRAT_PROVINCE_FLOOD_BASALT:
                switch (secondary_band) {
                    case 0: return BLOCK_BASALT;
                    case 1: return BLOCK_ANDESITE;
                    case 2: return BLOCK_SILTSTONE;
                    default: return BLOCK_TUFF;
                }
            case SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN:
            default:
                switch (secondary_band) {
                    case 0: return (geology->channel_sand_bias > 0.55f && zone_t < 0.46f) ? BLOCK_SANDSTONE : BLOCK_SILTSTONE;
                    case 1: return BLOCK_SILTSTONE;
                    case 2: return (geology->basin_axis_weight > 0.52f) ? BLOCK_MUDSTONE : BLOCK_SHALE;
                    default: return (geology->basin_axis_weight > 0.66f) ? BLOCK_SHALE : BLOCK_MUDSTONE;
                }
        }
    }

    if (wy > strata->basement_top) {
        zone_t = (float)(wy - strata->basement_top) / fmaxf(1.0f, (float)(strata->lower_top - strata->basement_top));
        contact_weight = geology_band_weight((float)wy - (float)strata->basement_top, 0.0f, 6.0f);

        if (province == SDK_STRAT_PROVINCE_CARBONATE_SHELF && contact_weight > 0.45f && geology->fault_mask > 0.40f) {
            return BLOCK_MARL;
        }
        if (province == SDK_STRAT_PROVINCE_RIFT_BASIN && geology->fault_mask > 0.66f && contact_weight > 0.25f) {
            return BLOCK_BRECCIA;
        }

        switch (province) {
            case SDK_STRAT_PROVINCE_OCEANIC:
                if (zone_t > 0.72f && geology->channel_sand_bias > 0.62f && basement_band == 0) return BLOCK_MARINE_MUD;
                return (basement_band == 0 && geology->fault_mask > 0.58f) ? BLOCK_ANDESITE : BLOCK_BASALT;
            case SDK_STRAT_PROVINCE_CARBONATE_SHELF:
                if (zone_t < 0.28f) return strata->basement_block;
                switch (basement_band) {
                    case 0: return BLOCK_LIMESTONE;
                    case 1: return BLOCK_DOLOSTONE;
                    case 2: return BLOCK_MARL;
                    default: return strata->basement_block;
                }
            case SDK_STRAT_PROVINCE_HARDROCK_BASEMENT:
                if (geology->fault_mask > 0.84f && contact_weight > 0.20f && basement_band == 0) return BLOCK_VEIN_QUARTZ;
                switch (profile->bedrock_province) {
                    case BEDROCK_PROVINCE_METAMORPHIC_BELT:
                        return (basement_band == 0) ? BLOCK_SCHIST : BLOCK_GNEISS;
                    case BEDROCK_PROVINCE_GRANITIC_INTRUSIVE:
                        return (basement_band == 0) ? BLOCK_GRANITE : BLOCK_GNEISS;
                    case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:
                    default:
                        return (basement_band == 0) ? BLOCK_GRANITE : BLOCK_GNEISS;
                }
            case SDK_STRAT_PROVINCE_RIFT_BASIN:
                if (zone_t < 0.30f) return strata->basement_block;
                if (geology->fault_mask > 0.68f && basement_band == 0) return BLOCK_BRECCIA;
                switch (basement_band) {
                    case 0: return BLOCK_SANDSTONE;
                    case 1: return BLOCK_SILTSTONE;
                    case 2: return BLOCK_MUDSTONE;
                    default: return strata->basement_block;
                }
            case SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX:
                if (zone_t < 0.24f) return strata->basement_block;
                switch (basement_band) {
                    case 0: return BLOCK_ANDESITE;
                    case 1: return BLOCK_BASALT;
                    case 2: return BLOCK_VOLCANIC_ROCK;
                    default: return BLOCK_TUFF;
                }
            case SDK_STRAT_PROVINCE_FLOOD_BASALT:
                if (zone_t < 0.24f) return strata->basement_block;
                switch (basement_band) {
                    case 0: return BLOCK_BASALT;
                    case 1: return BLOCK_ANDESITE;
                    case 2: return BLOCK_TUFF;
                    default: return strata->basement_block;
                }
            case SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN:
            default:
                if (zone_t < 0.32f) return strata->basement_block;
                switch (basement_band) {
                    case 0: return BLOCK_SANDSTONE;
                    case 1: return BLOCK_SILTSTONE;
                    case 2: return BLOCK_MUDSTONE;
                    default: return strata->basement_block;
                }
        }
    }

    if (wy > strata->deep_basement_top) {
        switch (profile->bedrock_province) {
            case BEDROCK_PROVINCE_OCEANIC_BASALT:
                return (geology->fault_mask > 0.74f && deep_band == 0) ? BLOCK_ANDESITE : BLOCK_BASALT;
            case BEDROCK_PROVINCE_METAMORPHIC_BELT:
                return (deep_band == 0) ? BLOCK_GNEISS : BLOCK_SCHIST;
            case BEDROCK_PROVINCE_GRANITIC_INTRUSIVE:
                if (geology->fault_mask > 0.88f && deep_band == 0) return BLOCK_VEIN_QUARTZ;
                return (deep_band == 0) ? BLOCK_GRANITE : BLOCK_GNEISS;
            case BEDROCK_PROVINCE_RIFT_SEDIMENTARY:
                if (geology->vent_bias > 0.64f && deep_band == 0) return BLOCK_BASALT;
                return (deep_band == 0) ? strata->deep_basement_block : BLOCK_GNEISS;
            case BEDROCK_PROVINCE_VOLCANIC_ARC:
                return (deep_band == 0) ? BLOCK_BASALT : strata->deep_basement_block;
            case BEDROCK_PROVINCE_FLOOD_BASALT:
                return (geology->fault_mask > 0.68f && deep_band == 0) ? BLOCK_ANDESITE : BLOCK_BASALT;
            case BEDROCK_PROVINCE_SILICICLASTIC_BASIN:
            case BEDROCK_PROVINCE_CARBONATE_PLATFORM:
            case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:
            default:
                if (geology->fault_mask > 0.88f && deep_band == 0) return BLOCK_VEIN_QUARTZ;
                return (deep_band == 0) ? strata->deep_basement_block :
                       ((strata->deep_basement_block == BLOCK_GRANITE) ? BLOCK_GNEISS : strata->deep_basement_block);
        }
    }

    if (geology->fault_mask > 0.90f && deep_band == 0) {
        return strata->vein_block;
    }
    return strata->deep_basement_block;
}

BlockType apply_fault_zone_override(const SdkRegionFieldSample* geology,
                                           const SdkStrataColumn* strata,
                                           BlockType host_block, int wy)
{
    float contact_weight;

    if (!geology || !strata) return host_block;
    if (geology->fault_mask < 0.46f) return host_block;
    if (wy > strata->weathered_base) return host_block;
    if (!sdk_block_is_solid(host_block) || !sdk_block_is_opaque(host_block)) return host_block;

    contact_weight = geology_band_weight((float)wy - (float)strata->basement_top, 0.0f, 7.0f);
    if (geology->fault_mask > 0.84f && contact_weight > 0.20f) {
        return strata->vein_block;
    }
    if (geology->fault_mask > 0.62f) {
        return strata->fault_block;
    }
    return host_block;
}

SdkResourceBodyKind resource_body_kind_at(const SdkTerrainColumnProfile* profile,
                                                 const SdkRegionFieldSample* geology,
                                                 const SdkStrataColumn* strata,
                                                 BlockType host_block,
                                                 int wy, int surface_y)
{
    float coal_center;
    float iron_center;
    float bauxite_center;
    float copper_center;
    float sulfur_center;
    float carbonate_center;
    float salt_center;
    float hydrocarbon_center;
    float trap_support;
    float body;

    if (!profile || !geology || !strata) return SDK_RESOURCE_BODY_NONE;

    if (profile->resource_province == RESOURCE_PROVINCE_COALFIELD &&
        (host_block == BLOCK_SHALE || host_block == BLOCK_MUDSTONE ||
         host_block == BLOCK_SILTSTONE || host_block == BLOCK_PEAT)) {
        coal_center = (float)strata->lower_top + ((float)(strata->upper_top - strata->lower_top) *
                      (0.30f + geology->basin_axis_weight * 0.28f)) +
                      sdk_worldgen_fbm((float)(surface_y) * 0.012f + geology->dip_x,
                                       geology->channel_sand_bias * 0.7f, 0x7B11u, 2) * 4.0f;
        body = geology_band_weight((float)wy - coal_center, 0.0f, 3.2f) *
               geology->basin_axis_weight * (1.0f - geology->channel_sand_bias * 0.55f);
        if (body > 0.46f) return SDK_RESOURCE_BODY_COAL;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_CLAY_DISTRICT &&
        (host_block == BLOCK_FINE_ALLUVIUM || host_block == BLOCK_CLAY || host_block == BLOCK_SILT ||
         host_block == BLOCK_MARINE_MUD || host_block == BLOCK_TIDAL_SILT || host_block == BLOCK_MUDSTONE)) {
        body = geology_band_weight((float)wy - ((float)surface_y - 5.0f - geology->wetness * 4.0f), 0.0f, 4.0f) *
               sdk_worldgen_clampf(geology->wetness * 0.75f + geology->basin_axis_weight * 0.35f, 0.0f, 1.0f);
        if (body > 0.45f) return SDK_RESOURCE_BODY_CLAY;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_IRON_BELT &&
        (host_block == BLOCK_SHALE || host_block == BLOCK_MUDSTONE ||
         host_block == BLOCK_SILTSTONE || host_block == BLOCK_CONGLOMERATE ||
         host_block == BLOCK_SAPROLITE || host_block == BLOCK_CALCAREOUS_SOIL)) {
        iron_center = (float)strata->weathered_base - 2.0f - geology->wetness * 2.5f + geology->fault_throw * 0.15f;
        body = geology_band_weight((float)wy - iron_center, 0.0f, 5.0f) *
               sdk_worldgen_clampf((1.0f - geology->wetness) * 0.25f +
                                   geology->basin_axis_weight * 0.25f +
                                   geology->fault_mask * 0.20f +
                                   (profile->terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND ? 0.12f : 0.0f) +
                                   (profile->terrain_province == TERRAIN_PROVINCE_UPLIFTED_PLATEAU ? 0.10f : 0.0f),
                                   0.0f, 1.0f);
        if (body > 0.46f) return SDK_RESOURCE_BODY_IRONSTONE;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_IRON_BELT &&
        profile->temperature_band >= TEMP_SUBTROPICAL &&
        profile->moisture_band >= MOISTURE_HUMID &&
        (profile->parent_material == PARENT_MATERIAL_MAFIC_VOLCANIC ||
         profile->parent_material == PARENT_MATERIAL_INTERMEDIATE_VOLCANIC ||
         profile->parent_material == PARENT_MATERIAL_CARBONATE) &&
        (host_block == BLOCK_SAPROLITE || host_block == BLOCK_CALCAREOUS_SOIL || host_block == BLOCK_TUFF)) {
        bauxite_center = (float)strata->weathered_base - 1.5f - geology->wetness * 1.8f;
        body = geology_band_weight((float)wy - bauxite_center, 0.0f, 4.0f) *
               sdk_worldgen_clampf(geology->wetness * 0.55f +
                                   geology->vent_bias * 0.15f +
                                   (profile->terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU ? 0.25f : 0.0f) +
                                   (profile->terrain_province == TERRAIN_PROVINCE_UPLIFTED_PLATEAU ? 0.15f : 0.0f),
                                   0.0f, 1.0f);
        if (body > 0.50f) return SDK_RESOURCE_BODY_BAUXITE;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_VOLCANIC_METALS &&
        (host_block == BLOCK_BASALT || host_block == BLOCK_ANDESITE ||
         host_block == BLOCK_VOLCANIC_ROCK || host_block == BLOCK_TUFF ||
         host_block == BLOCK_SCORIA)) {
        copper_center = (float)strata->lower_top + ((float)(strata->weathered_base - strata->lower_top) * 0.48f) + geology->fault_throw * 0.25f;
        body = geology_band_weight((float)wy - copper_center, 0.0f, 8.0f) *
               sdk_worldgen_clampf(geology->fault_mask * 0.70f + geology->vent_bias * 0.80f, 0.0f, 1.0f);
        if (body > 0.52f) return SDK_RESOURCE_BODY_COPPER;

        sulfur_center = (float)strata->weathered_base - 5.0f - geology->vent_bias * 8.0f;
        body = geology_band_weight((float)wy - sulfur_center, 0.0f, 5.0f) *
               sdk_worldgen_clampf(geology->vent_bias * 0.95f + geology->fault_mask * 0.25f, 0.0f, 1.0f);
        if (body > 0.54f) return SDK_RESOURCE_BODY_SULFUR;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_CARBONATE_CEMENT_DISTRICT &&
        (host_block == BLOCK_LIMESTONE || host_block == BLOCK_DOLOSTONE ||
         host_block == BLOCK_MARL || host_block == BLOCK_CHALK ||
         host_block == BLOCK_SCHIST || host_block == BLOCK_GNEISS || host_block == BLOCK_VEIN_QUARTZ)) {
        carbonate_center = (float)strata->basement_top + 3.0f + geology->fault_throw * 0.20f;
        body = geology_band_weight((float)wy - carbonate_center, 0.0f, 7.0f) *
               sdk_worldgen_clampf(geology->fault_mask * 0.90f + geology->carbonate_purity * 0.30f, 0.0f, 1.0f);
        if (body > 0.60f) return SDK_RESOURCE_BODY_LEAD_ZINC;
        if (host_block != BLOCK_SCHIST && host_block != BLOCK_GNEISS &&
            body > 0.46f && geology->fault_mask > 0.52f) {
            return SDK_RESOURCE_BODY_TUNGSTEN;
        }
    }

    if ((profile->terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE ||
         profile->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST ||
         profile->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA) &&
        (host_block == BLOCK_MARINE_MUD || host_block == BLOCK_TIDAL_SILT ||
         host_block == BLOCK_FINE_ALLUVIUM || host_block == BLOCK_MARL ||
         host_block == BLOCK_CHALK)) {
        salt_center = (float)surface_y - 6.0f - geology->evaporite_bias * 6.0f;
        body = geology_band_weight((float)wy - salt_center, 0.0f, 5.0f) *
               geology->evaporite_bias * sdk_worldgen_clampf(1.0f - geology->wetness, 0.0f, 1.0f);
        if (body > 0.50f) return SDK_RESOURCE_BODY_SALT;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_OIL_FIELD &&
        (host_block == BLOCK_SHALE || host_block == BLOCK_MUDSTONE ||
         host_block == BLOCK_SILTSTONE || host_block == BLOCK_SANDSTONE)) {
        hydrocarbon_center = (float)strata->lower_top + ((float)(strata->upper_top - strata->lower_top) *
                             (0.38f + geology->basin_axis_weight * 0.10f));
        trap_support = sdk_worldgen_clampf(geology->source_richness * 0.40f +
                                           geology->trap_strength * 0.35f +
                                           geology->seal_quality * 0.25f,
                                           0.0f, 1.0f);

        if (host_block == BLOCK_SANDSTONE &&
            geology->trap_strength > 0.56f &&
            geology->seal_quality > 0.52f) {
            float reservoir_center = hydrocarbon_center + geology->channel_sand_bias * 2.0f - geology->fault_throw * 0.05f;
            float reservoir_body = geology_band_weight((float)wy - reservoir_center, 0.0f, 5.5f) *
                                   sdk_worldgen_clampf(trap_support * 0.75f +
                                                       geology->channel_sand_bias * 0.25f,
                                                       0.0f, 1.0f);
            if (geology->gas_bias > 0.60f &&
                geology->hydrocarbon_class == SDK_HYDROCARBON_NATURAL_GAS &&
                reservoir_body > 0.54f) {
                return SDK_RESOURCE_BODY_GAS;
            }
            if (reservoir_body > 0.52f &&
                (geology->hydrocarbon_class == SDK_HYDROCARBON_CRUDE_OIL ||
                 geology->hydrocarbon_class == SDK_HYDROCARBON_NATURAL_GAS)) {
                return SDK_RESOURCE_BODY_OIL;
            }
        }

        body = geology_band_weight((float)wy - hydrocarbon_center, 0.0f, 6.0f) *
               sdk_worldgen_clampf(geology->source_richness * 0.55f +
                                   geology->basin_axis_weight * 0.25f +
                                   (1.0f - geology->channel_sand_bias) * 0.20f,
                                   0.0f, 1.0f);
        if (body > 0.50f &&
            (geology->hydrocarbon_class == SDK_HYDROCARBON_OIL_SHALE ||
             geology->hydrocarbon_class == SDK_HYDROCARBON_OIL_SAND ||
             geology->hydrocarbon_class == SDK_HYDROCARBON_CRUDE_OIL)) {
            return SDK_RESOURCE_BODY_OIL;
        }
    }

    if (profile->resource_province == RESOURCE_PROVINCE_COPPER_PORPHYRY_BELT &&
        (host_block == BLOCK_GRANITE || host_block == BLOCK_ANDESITE ||
         host_block == BLOCK_VOLCANIC_ROCK || host_block == BLOCK_TUFF)) {
        copper_center = (float)strata->basement_top + ((float)(strata->weathered_base - strata->basement_top) * 0.65f);
        body = geology_band_weight((float)wy - copper_center, 0.0f, 10.0f) *
               sdk_worldgen_clampf(geology->fault_mask * 0.55f + geology->vent_bias * 0.45f, 0.0f, 1.0f);
        if (body > 0.55f) return SDK_RESOURCE_BODY_COPPER;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_BAUXITE_DEPOSIT &&
        (host_block == BLOCK_SAPROLITE || host_block == BLOCK_CALCAREOUS_SOIL ||
         host_block == BLOCK_TOPSOIL || host_block == BLOCK_SUBSOIL)) {
        bauxite_center = (float)strata->weathered_base - 2.0f;
        body = geology_band_weight((float)wy - bauxite_center, 0.0f, 3.5f) *
               sdk_worldgen_clampf(geology->wetness * 0.75f, 0.0f, 1.0f);
        if (body > 0.48f) return SDK_RESOURCE_BODY_ALUMINUM;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_RARE_EARTH_DISTRICT &&
        (host_block == BLOCK_GRANITE || host_block == BLOCK_GNEISS ||
         host_block == BLOCK_SCHIST || host_block == BLOCK_VEIN_QUARTZ)) {
        float rare_earth_center = (float)strata->basement_top + 5.0f;
        body = geology_band_weight((float)wy - rare_earth_center, 0.0f, 8.0f) *
               sdk_worldgen_clampf(geology->fault_mask * 0.85f, 0.0f, 1.0f);
        if (body > 0.62f) return SDK_RESOURCE_BODY_RARE_EARTH;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_URANIUM_GRANITE_BELT &&
        (host_block == BLOCK_GRANITE || host_block == BLOCK_GNEISS ||
         host_block == BLOCK_VEIN_QUARTZ)) {
        float uranium_center = (float)strata->basement_top + 3.0f;
        body = geology_band_weight((float)wy - uranium_center, 0.0f, 7.0f) *
               sdk_worldgen_clampf(geology->fault_mask * 0.70f + geology->carbonate_purity * 0.20f, 0.0f, 1.0f);
        if (body > 0.58f) return SDK_RESOURCE_BODY_URANIUM;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_SULFUR_VOLCANIC_DISTRICT &&
        (host_block == BLOCK_BASALT || host_block == BLOCK_VOLCANIC_ROCK ||
         host_block == BLOCK_ANDESITE || host_block == BLOCK_TUFF || host_block == BLOCK_SCORIA)) {
        sulfur_center = (float)strata->weathered_base - 4.0f;
        body = geology_band_weight((float)wy - sulfur_center, 0.0f, 6.0f) *
               sdk_worldgen_clampf(geology->vent_bias * 0.90f, 0.0f, 1.0f);
        if (body > 0.56f) return SDK_RESOURCE_BODY_SULFUR;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_SALTPETER_NITRATE_FIELD &&
        (host_block == BLOCK_SANDSTONE || host_block == BLOCK_SILTSTONE ||
         host_block == BLOCK_FINE_ALLUVIUM || host_block == BLOCK_LOESS)) {
        float saltpeter_center = (float)surface_y - 4.0f;
        body = geology_band_weight((float)wy - saltpeter_center, 0.0f, 3.0f) *
               sdk_worldgen_clampf((1.0f - geology->wetness) * 0.85f, 0.0f, 1.0f);
        if (body > 0.50f) return SDK_RESOURCE_BODY_SALTPETER;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_PHOSPHATE_DEPOSIT &&
        (host_block == BLOCK_LIMESTONE || host_block == BLOCK_DOLOSTONE ||
         host_block == BLOCK_MARL || host_block == BLOCK_MARINE_MUD)) {
        float phosphate_center = (float)strata->lower_top + ((float)(strata->upper_top - strata->lower_top) * 0.55f);
        body = geology_band_weight((float)wy - phosphate_center, 0.0f, 5.0f) *
               sdk_worldgen_clampf(geology->carbonate_purity * 0.70f + geology->basin_axis_weight * 0.30f, 0.0f, 1.0f);
        if (body > 0.54f) return SDK_RESOURCE_BODY_PHOSPHATE;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_STRATEGIC_ALLOY_BELT &&
        (host_block == BLOCK_SCHIST || host_block == BLOCK_GNEISS ||
         host_block == BLOCK_BASALT || host_block == BLOCK_ANDESITE)) {
        float chromium_center = (float)strata->basement_top + 8.0f;
        body = geology_band_weight((float)wy - chromium_center, 0.0f, 9.0f) *
               sdk_worldgen_clampf(geology->fault_mask * 0.65f + geology->vent_bias * 0.25f, 0.0f, 1.0f);
        if (body > 0.60f) return SDK_RESOURCE_BODY_CHROMIUM;
    }

    if (profile->resource_province == RESOURCE_PROVINCE_LEAD_ZINC_DISTRICT &&
        (host_block == BLOCK_LIMESTONE || host_block == BLOCK_DOLOSTONE ||
         host_block == BLOCK_SANDSTONE || host_block == BLOCK_SHALE)) {
        carbonate_center = (float)strata->basement_top + 4.0f;
        body = geology_band_weight((float)wy - carbonate_center, 0.0f, 6.0f) *
               sdk_worldgen_clampf(geology->fault_mask * 0.75f + geology->carbonate_purity * 0.35f, 0.0f, 1.0f);
        if (body > 0.58f) return SDK_RESOURCE_BODY_LEAD_ZINC;
    }

    return SDK_RESOURCE_BODY_NONE;
}

BlockType maybe_resource_block(SdkWorldGen* wg, const SdkTerrainColumnProfile* profile,
                                      const SdkRegionFieldSample* geology,
                                      const SdkStrataColumn* strata,
                                      BlockType host_block, int wx, int wy, int wz, int surface_y)
{
    SdkResourceBodyKind kind;
    (void)wg;
    (void)wx;
    (void)wz;

    kind = resource_body_kind_at(profile, geology, strata, host_block, wy, surface_y);
    switch (kind) {
        case SDK_RESOURCE_BODY_COAL: return BLOCK_COAL_SEAM;
        case SDK_RESOURCE_BODY_CLAY: return BLOCK_CLAY_DEPOSIT;
        case SDK_RESOURCE_BODY_IRONSTONE: return BLOCK_IRONSTONE;
        case SDK_RESOURCE_BODY_COPPER: return BLOCK_COPPER_BEARING_ROCK;
        case SDK_RESOURCE_BODY_SULFUR: return BLOCK_SULFUR_BEARING_VOLCANIC_ROCK;
        case SDK_RESOURCE_BODY_LEAD_ZINC: return BLOCK_LEAD_ZINC_VEIN_ROCK;
        case SDK_RESOURCE_BODY_TUNGSTEN: return BLOCK_TUNGSTEN_SKARN;
        case SDK_RESOURCE_BODY_BAUXITE: return BLOCK_BAUXITE;
        case SDK_RESOURCE_BODY_SALT: return BLOCK_SALT_EVAPORITE;
        case SDK_RESOURCE_BODY_OIL:
            if (host_block == BLOCK_SANDSTONE &&
                geology &&
                geology->trap_strength > 0.56f &&
                geology->seal_quality > 0.52f) {
                return BLOCK_CRUDE_BEARING_SANDSTONE;
            }
            return (host_block == BLOCK_SANDSTONE) ? BLOCK_OIL_SAND : BLOCK_OIL_SHALE;
        case SDK_RESOURCE_BODY_GAS: return BLOCK_GAS_BEARING_SANDSTONE;
        case SDK_RESOURCE_BODY_RARE_EARTH: return BLOCK_RARE_EARTH_ORE;
        case SDK_RESOURCE_BODY_URANIUM: return BLOCK_URANIUM_ORE;
        case SDK_RESOURCE_BODY_SALTPETER: return BLOCK_SALTPETER_BEARING_ROCK;
        case SDK_RESOURCE_BODY_PHOSPHATE: return BLOCK_PHOSPHATE_ROCK;
        case SDK_RESOURCE_BODY_CHROMIUM: return BLOCK_CHROMITE;
        case SDK_RESOURCE_BODY_ALUMINUM: return BLOCK_ALUMINUM_ORE;
        case SDK_RESOURCE_BODY_NONE:
        default:
            return host_block;
    }
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

void carve_geology_caves_in_column(SdkChunk* chunk,
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

