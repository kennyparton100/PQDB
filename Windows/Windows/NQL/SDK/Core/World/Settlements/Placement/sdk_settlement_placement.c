/**
 * sdk_settlement_placement.c -- Settlement suitability scoring and placement
 */
#include "../sdk_settlement.h"
#include "../../Worldgen/Types/sdk_worldgen_types.h"
#include "../../Worldgen/Internal/sdk_worldgen_internal.h"
#include "../../Superchunks/Geometry/sdk_superchunk_geometry.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

static float estimate_local_slope(SdkWorldGen* wg, int wx, int wz, int surface_y)
{
    int sample_step = 12;
    int east_y, west_y, north_y, south_y;
    int dx, dz;
    float grade_x, grade_z;

    if (!wg) return 0.0f;

    east_y = sdk_worldgen_get_surface_y_ctx(wg, wx + sample_step, wz);
    west_y = sdk_worldgen_get_surface_y_ctx(wg, wx - sample_step, wz);
    north_y = sdk_worldgen_get_surface_y_ctx(wg, wx, wz - sample_step);
    south_y = sdk_worldgen_get_surface_y_ctx(wg, wx, wz + sample_step);

    dx = east_y - west_y;
    dz = south_y - north_y;
    grade_x = fabsf((float)dx / (float)(sample_step * 2));
    grade_z = fabsf((float)dz / (float)(sample_step * 2));

    if (surface_y > 180) {
        grade_x += 0.08f;
        grade_z += 0.08f;
    }

    return sdk_worldgen_clampf((grade_x + grade_z) * 0.5f, 0.0f, 1.0f);
}

static GeographicVariant determine_geographic_variant(
    SdkWorldGen* wg,
    const SdkTerrainColumnProfile* profile,
    const SdkContinentalSample* continental,
    int wx,
    int wz,
    int surface_y)
{
    float coast_distance = continental->coast_distance;
    float river_order = continental->trunk_river_order;
    float confluence = continental->confluence_score;
    float slope = estimate_local_slope(wg, wx, wz, surface_y);
    
    if (coast_distance < 200.0f && continental->ocean_mask == 0 && continental->land_mask > 0) {
        return GEOGRAPHIC_VARIANT_COASTAL;
    }
    
    if (river_order >= 4.0f) {
        return GEOGRAPHIC_VARIANT_RIVERSIDE;
    }
    
    if (surface_y > 150 && slope > 0.4f) {
        return GEOGRAPHIC_VARIANT_MOUNTAIN;
    }
    
    if (profile->terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE ||
        profile->terrain_province == TERRAIN_PROVINCE_SALT_FLAT_PLAYA ||
        profile->terrain_province == TERRAIN_PROVINCE_BADLANDS_DISSECTED) {
        return GEOGRAPHIC_VARIANT_DESERT;
    }
    
    if (profile->ecology == ECOLOGY_TEMPERATE_DECIDUOUS_FOREST ||
        profile->ecology == ECOLOGY_TEMPERATE_CONIFER_FOREST ||
        profile->ecology == ECOLOGY_BOREAL_TAIGA ||
        profile->ecology == ECOLOGY_TROPICAL_RAINFOREST ||
        profile->ecology == ECOLOGY_TROPICAL_SEASONAL_FOREST) {
        return GEOGRAPHIC_VARIANT_FOREST;
    }
    
    if (confluence > 0.6f) {
        return GEOGRAPHIC_VARIANT_JUNCTION;
    }
    
    return GEOGRAPHIC_VARIANT_PLAINS;
}

static float clampf(float v, float min, float max)
{
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

static int floor_mod_positive(int value, int denom)
{
    int mod;

    if (denom <= 0) return 0;
    mod = value % denom;
    if (mod < 0) mod += denom;
    return mod;
}

static int is_forest_ecology(SdkBiomeEcology ecology)
{
    return ecology == ECOLOGY_RIPARIAN_FOREST ||
           ecology == ECOLOGY_TEMPERATE_DECIDUOUS_FOREST ||
           ecology == ECOLOGY_TEMPERATE_CONIFER_FOREST ||
           ecology == ECOLOGY_BOREAL_TAIGA ||
           ecology == ECOLOGY_TROPICAL_SEASONAL_FOREST ||
           ecology == ECOLOGY_TROPICAL_RAINFOREST;
}

static int is_core_village_ecology(SdkBiomeEcology ecology)
{
    return ecology == ECOLOGY_PRAIRIE ||
           ecology == ECOLOGY_FLOODPLAIN_MEADOW ||
           ecology == ECOLOGY_RIPARIAN_FOREST ||
           ecology == ECOLOGY_TEMPERATE_DECIDUOUS_FOREST ||
           ecology == ECOLOGY_TEMPERATE_CONIFER_FOREST;
}

static int is_frontier_village_ecology(SdkBiomeEcology ecology)
{
    return ecology == ECOLOGY_STEPPE ||
           ecology == ECOLOGY_SAVANNA_GRASSLAND ||
           ecology == ECOLOGY_MEDITERRANEAN_SCRUB ||
           ecology == ECOLOGY_TROPICAL_SEASONAL_FOREST;
}

static int is_extreme_dry_town_profile(const SdkTerrainColumnProfile* profile)
{
    if (!profile) return 0;

    return profile->terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE ||
           profile->terrain_province == TERRAIN_PROVINCE_SALT_FLAT_PLAYA ||
           profile->terrain_province == TERRAIN_PROVINCE_BADLANDS_DISSECTED ||
           profile->ecology == ECOLOGY_HOT_DESERT ||
           profile->ecology == ECOLOGY_SALT_DESERT ||
           profile->ecology == ECOLOGY_SCRUB_BADLANDS;
}

static float strongest_industrial_signal(const SdkResourceSignature* signature)
{
    float strongest;

    if (!signature) return 0.0f;
    strongest = signature->hydrocarbon_score;
    if (signature->coal_score > strongest) strongest = signature->coal_score;
    if (signature->iron_score > strongest) strongest = signature->iron_score;
    if (signature->carbonate_score > strongest) strongest = signature->carbonate_score;
    if (signature->clay_score > strongest) strongest = signature->clay_score;
    if (signature->timber_score > strongest) strongest = signature->timber_score;
    if (signature->sulfur_score > strongest) strongest = signature->sulfur_score;
    return strongest;
}

static float evaluate_city_suitability(const SdkTerrainColumnProfile* profile, const SdkContinentalSample* continental, int surface_y)
{
    float score = 0.0f;
    float water_score, height_score, flat_score, harbor_bonus, flood_penalty;
    
    if (!profile || !continental) return 0.0f;
    
    water_score = continental->water_access;
    if (water_score < 0.7f) return 0.0f;
    
    height_score = 1.0f - clampf(fabsf((float)surface_y - 128.0f) / 30.0f, 0.0f, 1.0f);
    if (height_score < 0.3f) return 0.0f;
    
    flat_score = continental->buildable_flatness;
    if (flat_score < 0.5f) return 0.0f;
    
    flood_penalty = continental->flood_risk;
    if (flood_penalty > 0.4f) return 0.0f;
    
    score = water_score * 0.4f + height_score * 0.2f + flat_score * 0.3f;
    
    harbor_bonus = continental->harbor_score;
    if (harbor_bonus > 0.6f) {
        score += 0.2f;
    } else if (continental->trunk_river_order >= 5.0f) {
        score += 0.15f;
    }
    
    score -= flood_penalty * 0.3f;
    
    return clampf(score, 0.0f, 1.0f);
}

static float evaluate_town_suitability(SdkWorldGen* wg, const SdkTerrainColumnProfile* profile, const SdkContinentalSample* continental, int wx, int wz)
{
    float score = 0.0f;
    float water_score, flat_score, confluence_bonus, resource_bonus;
    float interior_bonus;
    int wall_clearance;
    int local_x, local_z;
    int dist_to_sc_boundary_x, dist_to_sc_boundary_z, dist_to_boundary;
    SdkResourceSignature signature;
    
    if (!profile || !continental) return 0.0f;
    memset(&signature, 0, sizeof(signature));
    
    local_x = floor_mod_positive(wx, SDK_SUPERCHUNK_BLOCK_SPAN);
    local_z = floor_mod_positive(wz, SDK_SUPERCHUNK_BLOCK_SPAN);
    dist_to_sc_boundary_x = local_x;
    if (SDK_SUPERCHUNK_BLOCK_SPAN - 1 - local_x < dist_to_sc_boundary_x) {
        dist_to_sc_boundary_x = SDK_SUPERCHUNK_BLOCK_SPAN - 1 - local_x;
    }
    
    dist_to_sc_boundary_z = local_z;
    if (SDK_SUPERCHUNK_BLOCK_SPAN - 1 - local_z < dist_to_sc_boundary_z) {
        dist_to_sc_boundary_z = SDK_SUPERCHUNK_BLOCK_SPAN - 1 - local_z;
    }
    
    dist_to_boundary = (dist_to_sc_boundary_x < dist_to_sc_boundary_z) ? dist_to_sc_boundary_x : dist_to_sc_boundary_z;
    
    wall_clearance = SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS + 16;
    if (dist_to_boundary < wall_clearance) {
        return 0.0f;
    }

    interior_bonus = clampf(
        ((float)dist_to_boundary - (float)wall_clearance) / 192.0f,
        0.0f, 1.0f);
    score += interior_bonus * 0.15f;
    
    confluence_bonus = continental->confluence_score;
    if (confluence_bonus > 0.5f) {
        score += confluence_bonus * 0.3f;
    }

    sdk_worldgen_scan_resource_signature(wg, wx, wz, 96, &signature);
    resource_bonus = strongest_industrial_signal(&signature) * 0.18f +
                     signature.hydrocarbon_score * 0.12f +
                     signature.coal_score * 0.08f +
                     signature.iron_score * 0.08f +
                     signature.carbonate_score * 0.06f +
                     signature.clay_score * 0.05f +
                     signature.sand_score * 0.04f +
                     signature.timber_score * 0.05f +
                     signature.sulfur_score * 0.05f;
    score += resource_bonus;
    
    water_score = continental->water_access;
    if (water_score < 0.4f) return 0.0f;
    
    flat_score = continental->buildable_flatness;
    if (flat_score < 0.4f) return 0.0f;
    
    score += water_score * 0.15f + flat_score * 0.2f;
    
    return clampf(score, 0.0f, 1.0f);
}

static float evaluate_village_suitability(const SdkTerrainColumnProfile* profile, const SdkContinentalSample* continental)
{
    float score = 0.0f;
    float fertility_score, water_score, flat_score, drainage_penalty, moisture_score;
    int core_ecology;
    int frontier_ecology;
    int has_river_support;
    int has_frontier_water_support;
    
    if (!profile || !continental) return 0.0f;

    core_ecology = is_core_village_ecology(profile->ecology);
    frontier_ecology = is_frontier_village_ecology(profile->ecology);
    if (!core_ecology && !frontier_ecology) {
        return 0.0f;
    }

    if (profile->drainage_class == DRAINAGE_POOR || profile->drainage_class == DRAINAGE_WATERLOGGED) {
        return 0.0f;
    }

    flat_score = continental->buildable_flatness;
    if (flat_score < 0.2f) return 0.0f;

    has_river_support = profile->river_order > 0;
    water_score = continental->water_access;
    if (has_river_support && water_score < 0.45f) {
        water_score = 0.45f;
    }
    has_frontier_water_support = has_river_support || water_score >= 0.45f;

    if (core_ecology) {
        if (profile->soil_fertility < SOIL_FERTILITY_MODERATE) return 0.0f;
        if (profile->moisture_band < MOISTURE_SUBHUMID) return 0.0f;
        if (water_score < 0.25f && !has_river_support) return 0.0f;
    } else {
        if (profile->soil_fertility < SOIL_FERTILITY_LOW) return 0.0f;
        if (profile->moisture_band < MOISTURE_SEMI_ARID) return 0.0f;
        if ((profile->soil_fertility == SOIL_FERTILITY_LOW ||
             profile->moisture_band == MOISTURE_SEMI_ARID) &&
            !has_frontier_water_support) {
            return 0.0f;
        }
        if (water_score < 0.30f && !has_river_support) return 0.0f;
    }

    fertility_score = (float)profile->soil_fertility / (float)SOIL_FERTILITY_VERY_HIGH;
    moisture_score = (float)profile->moisture_band / (float)MOISTURE_WATERLOGGED;

    score = fertility_score * 0.35f +
            water_score * 0.25f +
            flat_score * 0.20f +
            moisture_score * 0.15f;

    if (has_river_support) {
        score += 0.05f;
    }
    if (core_ecology) {
        score += 0.06f;
    } else {
        score -= 0.02f;
        if (has_frontier_water_support) {
            score += 0.04f;
        }
    }
    
    drainage_penalty = 0.0f;
    if (profile->drainage_class == DRAINAGE_IMPERFECT) {
        drainage_penalty = 0.08f;
    }
    score -= drainage_penalty;
    
    return clampf(score, 0.0f, 1.0f);
}

SettlementPurpose sdk_settlement_determine_purpose_for_type(SdkWorldGen* wg,
                                                            int wx,
                                                            int wz,
                                                            const SdkTerrainColumnProfile* profile,
                                                            const SdkContinentalSample* continental,
                                                            SettlementType type)
{
    SdkResourceSignature signature;
    float mining_score;
    float hydrocarbon_score;
    float cement_score;
    float timber_score;
    float logistics_score;
    float processing_score;
    int forest_ecology;
    int harsh_dry_profile;
    int variant_hint;

    if (!profile || !continental) {
        return SETTLEMENT_PURPOSE_FARMING;
    }

    memset(&signature, 0, sizeof(signature));
    if (wg) {
        sdk_worldgen_scan_resource_signature(wg, wx, wz, (type == SETTLEMENT_TYPE_TOWN) ? 96 : 128, &signature);
    }

    if (type == SETTLEMENT_TYPE_VILLAGE) {
        if (continental->harbor_score > 0.28f ||
            (profile->landform_flags & SDK_LANDFORM_LAKE_BASIN) != 0 ||
            (profile->river_order >= 3 && continental->water_access >= 0.50f)) {
            return SETTLEMENT_PURPOSE_FISHING;
        }
        return SETTLEMENT_PURPOSE_FARMING;
    }
    
    if (type == SETTLEMENT_TYPE_TOWN) {
        mining_score = signature.coal_score;
        if (signature.iron_score > mining_score) mining_score = signature.iron_score;
        if (signature.sulfur_score > mining_score) mining_score = signature.sulfur_score;

        forest_ecology = is_forest_ecology(profile->ecology);
        harsh_dry_profile = is_extreme_dry_town_profile(profile);
        variant_hint = (continental->harbor_score > 0.55f || continental->water_access > 0.60f) ? 1 : 0;

        hydrocarbon_score = signature.hydrocarbon_score;
        cement_score = signature.carbonate_score;
        if (signature.clay_score * 0.75f + signature.sand_score * 0.50f > cement_score) {
            cement_score = signature.clay_score * 0.75f + signature.sand_score * 0.50f;
        }
        timber_score = signature.timber_score;
        logistics_score = continental->confluence_score * 0.65f +
                          continental->water_access * 0.25f;
        processing_score = 0.28f +
                           continental->buildable_flatness * 0.08f +
                           continental->water_access * 0.08f;

        if (continental->trunk_river_order >= 4.0f) {
            logistics_score += 0.10f;
        }
        if (variant_hint || profile->river_order >= 3 || profile->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA) {
            logistics_score += 0.12f;
        }
        if (forest_ecology) {
            timber_score += 0.12f;
        } else {
            timber_score -= 0.10f;
        }
        if (profile->terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND ||
            profile->terrain_province == TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT ||
            profile->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC) {
            mining_score += 0.08f;
        }
        if (harsh_dry_profile) {
            processing_score -= 0.20f;
            if (continental->water_access >= 0.55f || profile->river_order >= 2) {
                logistics_score += 0.08f;
            }
        }

        hydrocarbon_score = clampf(hydrocarbon_score, 0.0f, 1.0f);
        cement_score = clampf(cement_score, 0.0f, 1.0f);
        timber_score = clampf(timber_score, 0.0f, 1.0f);
        logistics_score = clampf(logistics_score, 0.0f, 1.0f);
        processing_score = clampf(processing_score, 0.0f, 1.0f);

        if (hydrocarbon_score >= 0.34f &&
            hydrocarbon_score >= mining_score &&
            hydrocarbon_score >= cement_score &&
            hydrocarbon_score >= timber_score &&
            hydrocarbon_score >= logistics_score - 0.05f) {
            return SETTLEMENT_PURPOSE_HYDROCARBON;
        }
        if (cement_score >= 0.40f &&
            cement_score >= mining_score &&
            cement_score >= timber_score) {
            return SETTLEMENT_PURPOSE_CEMENT;
        }
        if (timber_score >= 0.42f &&
            timber_score >= mining_score &&
            timber_score >= hydrocarbon_score &&
            timber_score >= logistics_score &&
            (forest_ecology ||
             (timber_score >= mining_score + 0.15f &&
              timber_score >= hydrocarbon_score + 0.15f))) {
            return SETTLEMENT_PURPOSE_TIMBER;
        }
        if (mining_score >= 0.36f &&
            mining_score >= logistics_score - 0.05f) {
            return SETTLEMENT_PURPOSE_MINING;
        }
        if (logistics_score >= 0.45f) {
            return SETTLEMENT_PURPOSE_LOGISTICS;
        }
        if (harsh_dry_profile && processing_score < 0.24f &&
            (continental->water_access >= 0.45f || profile->river_order > 0)) {
            return SETTLEMENT_PURPOSE_LOGISTICS;
        }
        return SETTLEMENT_PURPOSE_PROCESSING;
    }
    
    if (type == SETTLEMENT_TYPE_CITY) {
        if (continental->harbor_score > 0.7f) {
            return SETTLEMENT_PURPOSE_PORT;
        }
        return SETTLEMENT_PURPOSE_CAPITAL;
    }
    
    return SETTLEMENT_PURPOSE_FARMING;
}

GeographicVariant sdk_settlement_determine_variant(
    SdkWorldGen* wg,
    const SdkTerrainColumnProfile* profile,
    const SdkContinentalSample* continental,
    int wx,
    int wz,
    int surface_y)
{
    return determine_geographic_variant(wg, profile, continental, wx, wz, surface_y);
}

SettlementSuitability sdk_settlement_evaluate_suitability_full(SdkWorldGen* wg, int wx, int wz, const SdkTerrainColumnProfile* profile, const SdkContinentalSample* continental)
{
    SettlementSuitability result;
    int surface_y;
    
    memset(&result, 0, sizeof(result));
    
    if (!wg || !profile || !continental) return result;
    
    surface_y = profile->surface_height;
    
    result.city_score = evaluate_city_suitability(profile, continental, surface_y);
    result.town_score = evaluate_town_suitability(wg, profile, continental, wx, wz);
    result.village_score = evaluate_village_suitability(profile, continental);
    
    if (result.city_score >= result.town_score && result.city_score >= result.village_score) {
        result.recommended_purpose = sdk_settlement_determine_purpose_for_type(
            wg, wx, wz, profile, continental, SETTLEMENT_TYPE_CITY);
    } else if (result.town_score >= result.village_score) {
        result.recommended_purpose = sdk_settlement_determine_purpose_for_type(
            wg, wx, wz, profile, continental, SETTLEMENT_TYPE_TOWN);
    } else {
        result.recommended_purpose = sdk_settlement_determine_purpose_for_type(
            wg, wx, wz, profile, continental, SETTLEMENT_TYPE_VILLAGE);
    }
    
    return result;
}

void sdk_settlement_sample_continental_approximation(SdkWorldGen* wg, int wx, int wz, SdkContinentalSample* out_sample)
{
    if (!wg || !out_sample) return;

    sdk_worldgen_sample_continental_state(wg, wx, wz, out_sample);
}

