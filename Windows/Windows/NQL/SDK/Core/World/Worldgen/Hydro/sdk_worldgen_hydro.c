/**
 * sdk_worldgen_hydro.c -- Macro-tile drainage, coast distance, and province outputs.
 */
#include "../Internal/sdk_worldgen_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const int g_dir8_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int g_dir8_dz[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static int idx_of(int x, int z)
{
    return z * SDK_WORLDGEN_TILE_STRIDE + x;
}

static int in_bounds(int x, int z)
{
    return x >= 0 && x < SDK_WORLDGEN_TILE_STRIDE && z >= 0 && z < SDK_WORLDGEN_TILE_STRIDE;
}

static int floor_div_local(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return (value - denom + 1) / denom;
}

static int is_land(const SdkWorldGenMacroCell* cell, int sea_level)
{
    return cell->surface_height > sea_level;
}

static uint8_t direction_from_vector(float dx, float dz)
{
    float best_dot = -1.0e30f;
    uint8_t best_dir = 255u;
    int i;

    for (i = 0; i < 8; ++i) {
        float len = sqrtf((float)(g_dir8_dx[i] * g_dir8_dx[i] + g_dir8_dz[i] * g_dir8_dz[i]));
        float ndx;
        float ndz;
        float dot;
        if (len <= 0.0001f) continue;
        ndx = (float)g_dir8_dx[i] / len;
        ndz = (float)g_dir8_dz[i] / len;
        dot = dx * ndx + dz * ndz;
        if (dot > best_dot) {
            best_dot = dot;
            best_dir = (uint8_t)i;
        }
    }

    return best_dir;
}

static uint8_t classify_temperature(float temp)
{
    return sdk_worldgen_classify_temperature(temp);
}

static uint8_t classify_moisture(float moisture)
{
    return sdk_worldgen_classify_moisture(moisture);
}

static float detail_amp_for_province(uint8_t province)
{
    switch ((SdkTerrainProvince)province) {
        case TERRAIN_PROVINCE_OPEN_OCEAN: return 2.0f;
        case TERRAIN_PROVINCE_CONTINENTAL_SHELF: return 3.0f;
        case TERRAIN_PROVINCE_DYNAMIC_COAST: return 4.0f;
        case TERRAIN_PROVINCE_ESTUARY_DELTA: return 2.0f;
        case TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND: return 2.0f;
        case TERRAIN_PROVINCE_PEAT_WETLAND: return 1.0f;
        case TERRAIN_PROVINCE_SILICICLASTIC_HILLS: return 10.0f;
        case TERRAIN_PROVINCE_CARBONATE_UPLAND: return 8.0f;
        case TERRAIN_PROVINCE_HARDROCK_HIGHLAND: return 14.0f;
        case TERRAIN_PROVINCE_UPLIFTED_PLATEAU: return 6.0f;
        case TERRAIN_PROVINCE_RIFT_VALLEY: return 7.0f;
        case TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT: return 22.0f;
        case TERRAIN_PROVINCE_VOLCANIC_ARC: return 18.0f;
        case TERRAIN_PROVINCE_BASALT_PLATEAU: return 7.0f;
        case TERRAIN_PROVINCE_ARID_FAN_STEPPE: return 5.0f;
        case TERRAIN_PROVINCE_ALPINE_BELT: return 10.0f;
        default: return 6.0f;
    }
}

static uint8_t province_family_for(uint8_t province)
{
    switch ((SdkTerrainProvince)province) {
        case TERRAIN_PROVINCE_OPEN_OCEAN:
        case TERRAIN_PROVINCE_CONTINENTAL_SHELF:
        case TERRAIN_PROVINCE_DYNAMIC_COAST:
        case TERRAIN_PROVINCE_ESTUARY_DELTA:
            return SDK_WORLDGEN_PROVINCE_FAMILY_MARINE_COAST;
        case TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND:
        case TERRAIN_PROVINCE_PEAT_WETLAND:
            return SDK_WORLDGEN_PROVINCE_FAMILY_WET_LOWLAND;
        case TERRAIN_PROVINCE_VOLCANIC_ARC:
        case TERRAIN_PROVINCE_BASALT_PLATEAU:
            return SDK_WORLDGEN_PROVINCE_FAMILY_VOLCANIC;
        case TERRAIN_PROVINCE_HARDROCK_HIGHLAND:
        case TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT:
        case TERRAIN_PROVINCE_ALPINE_BELT:
            return SDK_WORLDGEN_PROVINCE_FAMILY_HARDROCK;
        default:
            return SDK_WORLDGEN_PROVINCE_FAMILY_BASIN_UPLAND;
    }
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

static uint8_t stratigraphy_province_for_cell(const SdkWorldGenMacroCell* cell, int sea_level)
{
    if (!cell) return SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN;

    if (cell->surface_height <= sea_level &&
        cell->bedrock_province == BEDROCK_PROVINCE_OCEANIC_BASALT) {
        return SDK_STRAT_PROVINCE_OCEANIC;
    }

    switch ((SdkBedrockProvince)cell->bedrock_province) {
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
        case BEDROCK_PROVINCE_OCEANIC_BASALT:
            return SDK_STRAT_PROVINCE_OCEANIC;
        default:
            return SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN;
    }
}

static float derive_water_temperature(const SdkWorldGenMacroCell* cell, int sea_level)
{
    float temp_mid;
    float depth_blocks;
    float marine;
    float shelf;
    float enclosure;
    float water_temp;

    if (!cell) return 0.0f;
    if (cell->water_height <= cell->surface_height) return 0.0f;

    temp_mid = temperature_band_midpoint(cell->temperature_band);
    depth_blocks = (cell->surface_height <= sea_level) ? (float)(sea_level - cell->surface_height) : 0.0f;
    marine = (cell->surface_height <= sea_level) ? 1.0f : 0.0f;
    shelf = (cell->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
             cell->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) ? 1.0f : 0.0f;
    enclosure = sdk_worldgen_clampf((4.0f - (float)abs(cell->coast_distance)) / 4.0f, 0.0f, 1.0f);

    if (marine > 0.5f) {
        water_temp = temp_mid * 0.80f + 0.18f;
        water_temp -= sdk_worldgen_clampf(depth_blocks / 220.0f, 0.0f, 1.0f) * 0.14f;
        water_temp -= shelf * enclosure * sdk_worldgen_clampf((0.42f - water_temp) * 0.65f, 0.0f, 0.12f);
    } else {
        water_temp = temp_mid * 0.88f - 0.06f;
        water_temp -= sdk_worldgen_clampf((float)cell->water_height / 800.0f, 0.0f, 1.0f) * 0.05f;
    }

    return sdk_worldgen_clampf(water_temp, 0.0f, 1.0f);
}

static uint8_t classify_surface_water(const SdkWorldGenMacroCell* cell, int sea_level, float water_temp)
{
    float depth_blocks;
    int marine;

    if (!cell || cell->water_height <= cell->surface_height) {
        return SURFACE_WATER_NONE;
    }

    depth_blocks = (cell->surface_height <= sea_level) ? (float)(sea_level - cell->surface_height) : 0.0f;
    marine = (cell->surface_height <= sea_level);

    if (marine) {
        if (water_temp <= 0.09f && depth_blocks <= 48.0f) return SURFACE_WATER_PERENNIAL_ICE;
        if (water_temp <= 0.16f && depth_blocks <= 30.0f) return SURFACE_WATER_SEASONAL_ICE;
        if (water_temp <= 0.12f &&
            (cell->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
             cell->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST)) {
            return SURFACE_WATER_SEASONAL_ICE;
        }
        return SURFACE_WATER_OPEN;
    }

    if (water_temp <= 0.10f) return SURFACE_WATER_SEASONAL_ICE;
    return SURFACE_WATER_OPEN;
}

static int is_hydrology_critical_province(uint8_t province)
{
    switch ((SdkTerrainProvince)province) {
        case TERRAIN_PROVINCE_OPEN_OCEAN:
        case TERRAIN_PROVINCE_CONTINENTAL_SHELF:
        case TERRAIN_PROVINCE_DYNAMIC_COAST:
        case TERRAIN_PROVINCE_ESTUARY_DELTA:
        case TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND:
        case TERRAIN_PROVINCE_PEAT_WETLAND:
            return 1;
        default:
            return 0;
    }
}

static uint8_t classify_drainage(float drainage_index, uint8_t water_table_depth)
{
    if (water_table_depth == 0u && drainage_index < 0.36f) return DRAINAGE_WATERLOGGED;
    if (drainage_index >= 0.80f) return DRAINAGE_EXCESSIVE;
    if (drainage_index >= 0.62f) return DRAINAGE_WELL;
    if (drainage_index >= 0.44f) return DRAINAGE_MODERATE;
    if (drainage_index >= 0.28f) return DRAINAGE_IMPERFECT;
    if (drainage_index >= 0.16f) return DRAINAGE_POOR;
    return DRAINAGE_WATERLOGGED;
}

static uint8_t classify_soil_reaction(float reaction_value, float salinity_value)
{
    if (salinity_value >= 0.72f) return SOIL_REACTION_SALINE_ALKALINE;
    if (reaction_value < 0.15f) return SOIL_REACTION_STRONGLY_ACID;
    if (reaction_value < 0.30f) return SOIL_REACTION_ACID;
    if (reaction_value < 0.48f) return SOIL_REACTION_SLIGHTLY_ACID;
    if (reaction_value < 0.64f) return SOIL_REACTION_NEUTRAL;
    if (reaction_value < 0.82f) return SOIL_REACTION_CALCAREOUS;
    return SOIL_REACTION_SALINE_ALKALINE;
}

static uint8_t classify_soil_fertility(float fertility_value)
{
    if (fertility_value < 0.18f) return SOIL_FERTILITY_VERY_LOW;
    if (fertility_value < 0.34f) return SOIL_FERTILITY_LOW;
    if (fertility_value < 0.54f) return SOIL_FERTILITY_MODERATE;
    if (fertility_value < 0.74f) return SOIL_FERTILITY_HIGH;
    return SOIL_FERTILITY_VERY_HIGH;
}

static uint8_t classify_soil_salinity(float salinity_value)
{
    if (salinity_value < 0.18f) return SOIL_SALINITY_NONE;
    if (salinity_value < 0.38f) return SOIL_SALINITY_SLIGHT;
    if (salinity_value < 0.62f) return SOIL_SALINITY_MODERATE;
    return SOIL_SALINITY_HIGH;
}

static uint8_t parent_material_for_cell(const SdkWorldGenMacroCell* cell, uint8_t surface_sediment)
{
    if (!cell) return PARENT_MATERIAL_NONE;

    switch ((SdkSurfaceSediment)surface_sediment) {
        case SEDIMENT_PEAT:
            return PARENT_MATERIAL_ORGANIC;
        case SEDIMENT_FINE_ALLUVIUM:
        case SEDIMENT_COARSE_ALLUVIUM:
        case SEDIMENT_DELTAIC_SILT:
        case SEDIMENT_LACUSTRINE_CLAY:
        case SEDIMENT_MARINE_MUD:
        case SEDIMENT_MARINE_SAND:
            return PARENT_MATERIAL_ALLUVIAL;
        case SEDIMENT_EOILIAN_SAND:
        case SEDIMENT_LOESS:
            return PARENT_MATERIAL_AEOLIAN;
        case SEDIMENT_CALCAREOUS_RESIDUAL:
            return PARENT_MATERIAL_CARBONATE;
        case SEDIMENT_VOLCANIC_ASH:
            if (cell->bedrock_province == BEDROCK_PROVINCE_FLOOD_BASALT ||
                cell->bedrock_province == BEDROCK_PROVINCE_OCEANIC_BASALT) {
                return PARENT_MATERIAL_MAFIC_VOLCANIC;
            }
            return PARENT_MATERIAL_INTERMEDIATE_VOLCANIC;
        case SEDIMENT_SAPROLITE:
            if (cell->bedrock_province == BEDROCK_PROVINCE_FLOOD_BASALT ||
                cell->bedrock_province == BEDROCK_PROVINCE_OCEANIC_BASALT) {
                return PARENT_MATERIAL_MAFIC_VOLCANIC;
            }
            if (cell->bedrock_province == BEDROCK_PROVINCE_VOLCANIC_ARC) {
                return PARENT_MATERIAL_INTERMEDIATE_VOLCANIC;
            }
            break;
        default:
            break;
    }

    switch ((SdkBedrockProvince)cell->bedrock_province) {
        case BEDROCK_PROVINCE_OCEANIC_BASALT:
        case BEDROCK_PROVINCE_FLOOD_BASALT:
            return PARENT_MATERIAL_MAFIC_VOLCANIC;
        case BEDROCK_PROVINCE_VOLCANIC_ARC:
            return PARENT_MATERIAL_INTERMEDIATE_VOLCANIC;
        case BEDROCK_PROVINCE_METAMORPHIC_BELT:
            return PARENT_MATERIAL_METAMORPHIC;
        case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:
        case BEDROCK_PROVINCE_GRANITIC_INTRUSIVE:
            return PARENT_MATERIAL_GRANITIC;
        case BEDROCK_PROVINCE_CARBONATE_PLATFORM:
            return PARENT_MATERIAL_CARBONATE;
        case BEDROCK_PROVINCE_RIFT_SEDIMENTARY:
        case BEDROCK_PROVINCE_SILICICLASTIC_BASIN:
            return PARENT_MATERIAL_SILICICLASTIC;
        default:
            return PARENT_MATERIAL_SILICICLASTIC;
    }
}

static uint8_t water_table_depth_for_cell(const SdkWorldGenMacroCell* cell, int sea_level, uint8_t surface_sediment)
{
    int depth;

    if (!cell) return 8u;
    if (cell->surface_height <= sea_level) return 0u;

    switch ((SdkMoistureBand)cell->moisture_band) {
        case MOISTURE_ARID:        depth = 14; break;
        case MOISTURE_SEMI_ARID:   depth = 12; break;
        case MOISTURE_SUBHUMID:    depth = 8;  break;
        case MOISTURE_HUMID:       depth = 5;  break;
        case MOISTURE_PERHUMID:    depth = 3;  break;
        case MOISTURE_WATERLOGGED: depth = 0;  break;
        default:                   depth = 8;  break;
    }

    switch ((SdkTerrainProvince)cell->terrain_province) {
        case TERRAIN_PROVINCE_PEAT_WETLAND:
            depth = 0;
            break;
        case TERRAIN_PROVINCE_ESTUARY_DELTA:
            if (depth > 1) depth = 1;
            break;
        case TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND:
            if (depth > 2) depth = 2;
            break;
        case TERRAIN_PROVINCE_DYNAMIC_COAST:
            if (depth > 2) depth = 2;
            break;
        default:
            break;
    }

    if (cell->river_order >= 2 && cell->floodplain_width > 0 && depth > 3) depth = 3;

    switch ((SdkSurfaceSediment)surface_sediment) {
        case SEDIMENT_PEAT:
        case SEDIMENT_LACUSTRINE_CLAY:
        case SEDIMENT_DELTAIC_SILT:
        case SEDIMENT_MARINE_MUD:
            depth -= 2;
            break;
        case SEDIMENT_TALUS:
        case SEDIMENT_COARSE_ALLUVIUM:
        case SEDIMENT_EOILIAN_SAND:
        case SEDIMENT_BEACH_SAND:
        case SEDIMENT_MARINE_SAND:
            depth += 2;
            break;
        default:
            break;
    }

    if (cell->slope >= 18) depth += 2;
    if (cell->slope <= 4 && cell->moisture_band >= MOISTURE_HUMID) depth -= 1;

    return (uint8_t)sdk_worldgen_clampi(depth, 0, 15);
}

static float drainage_index_for_cell(const SdkWorldGenMacroCell* cell,
                                     uint8_t surface_sediment,
                                     uint8_t parent_material,
                                     uint8_t water_table_depth)
{
    float drainage = 0.42f;

    if (!cell) return drainage;

    if (cell->slope >= 26) drainage += 0.30f;
    else if (cell->slope >= 18) drainage += 0.20f;
    else if (cell->slope >= 10) drainage += 0.08f;

    switch ((SdkSurfaceSediment)surface_sediment) {
        case SEDIMENT_TALUS:
        case SEDIMENT_COARSE_ALLUVIUM:
            drainage += 0.28f;
            break;
        case SEDIMENT_COLLUVIUM:
            drainage += 0.18f;
            break;
        case SEDIMENT_BEACH_SAND:
        case SEDIMENT_EOILIAN_SAND:
        case SEDIMENT_MARINE_SAND:
            drainage += 0.16f;
            break;
        case SEDIMENT_FINE_ALLUVIUM:
        case SEDIMENT_DELTAIC_SILT:
            drainage -= 0.12f;
            break;
        case SEDIMENT_LACUSTRINE_CLAY:
        case SEDIMENT_MARINE_MUD:
            drainage -= 0.22f;
            break;
        case SEDIMENT_PEAT:
            drainage -= 0.30f;
            break;
        case SEDIMENT_SAPROLITE:
            drainage -= 0.06f;
            break;
        default:
            break;
    }

    if (cell->terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND) drainage -= 0.35f;
    if (cell->terrain_province == TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND ||
        cell->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA) {
        drainage -= 0.20f;
    }
    if (cell->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) drainage -= 0.08f;

    if (water_table_depth <= 1u) drainage -= 0.40f;
    else if (water_table_depth <= 3u) drainage -= 0.22f;
    else if (water_table_depth >= 9u) drainage += 0.12f;

    if (cell->moisture_band <= MOISTURE_SEMI_ARID) drainage += 0.10f;
    else if (cell->moisture_band >= MOISTURE_PERHUMID) drainage -= 0.14f;

    if (parent_material == PARENT_MATERIAL_AEOLIAN) drainage += 0.06f;
    if (parent_material == PARENT_MATERIAL_ORGANIC) drainage -= 0.20f;

    return sdk_worldgen_clampf(drainage, 0.0f, 1.0f);
}

static float soil_salinity_value_for_cell(const SdkWorldGenMacroCell* cell, int sea_level, uint8_t surface_sediment)
{
    float salinity = 0.0f;

    if (!cell) return salinity;
    if (cell->surface_height <= sea_level) salinity += 0.25f;

    switch ((SdkSurfaceSediment)surface_sediment) {
        case SEDIMENT_MARINE_MUD:
        case SEDIMENT_MARINE_SAND:
            salinity += 0.55f;
            break;
        case SEDIMENT_BEACH_SAND:
            salinity += 0.18f;
            break;
        case SEDIMENT_PEAT:
            salinity -= 0.10f;
            break;
        default:
            break;
    }

    if (cell->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) salinity += 0.25f;
    if (cell->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA) salinity += 0.18f;
    if (cell->terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE) salinity += 0.22f;
    if (cell->moisture_band <= MOISTURE_SEMI_ARID) salinity += 0.12f;

    return sdk_worldgen_clampf(salinity, 0.0f, 1.0f);
}

static float soil_reaction_value_for_cell(const SdkWorldGenMacroCell* cell,
                                          uint8_t surface_sediment,
                                          uint8_t parent_material,
                                          float salinity_value)
{
    float reaction;

    (void)cell;

    switch ((SdkParentMaterialClass)parent_material) {
        case PARENT_MATERIAL_GRANITIC:              reaction = 0.20f; break;
        case PARENT_MATERIAL_METAMORPHIC:           reaction = 0.28f; break;
        case PARENT_MATERIAL_MAFIC_VOLCANIC:        reaction = 0.60f; break;
        case PARENT_MATERIAL_INTERMEDIATE_VOLCANIC: reaction = 0.52f; break;
        case PARENT_MATERIAL_SILICICLASTIC:         reaction = 0.36f; break;
        case PARENT_MATERIAL_CARBONATE:             reaction = 0.74f; break;
        case PARENT_MATERIAL_ORGANIC:               reaction = 0.12f; break;
        case PARENT_MATERIAL_ALLUVIAL:              reaction = 0.48f; break;
        case PARENT_MATERIAL_AEOLIAN:               reaction = 0.45f; break;
        case PARENT_MATERIAL_EVAPORITIC:            reaction = 0.88f; break;
        case PARENT_MATERIAL_NONE:
        default:                                    reaction = 0.40f; break;
    }

    if (surface_sediment == SEDIMENT_PEAT) reaction -= 0.12f;
    if (surface_sediment == SEDIMENT_CALCAREOUS_RESIDUAL || surface_sediment == SEDIMENT_LOESS) reaction += 0.10f;
    if (surface_sediment == SEDIMENT_SAPROLITE &&
        (parent_material == PARENT_MATERIAL_GRANITIC || parent_material == PARENT_MATERIAL_METAMORPHIC)) {
        reaction -= 0.08f;
    }
    if (surface_sediment == SEDIMENT_MARINE_MUD || surface_sediment == SEDIMENT_MARINE_SAND) reaction += 0.08f;

    reaction += salinity_value * 0.25f;
    return sdk_worldgen_clampf(reaction, 0.0f, 1.0f);
}

static float soil_fertility_value_for_cell(const SdkWorldGenMacroCell* cell,
                                           uint8_t surface_sediment,
                                           uint8_t parent_material,
                                           float salinity_value,
                                           float reaction_value)
{
    float fertility;

    switch ((SdkParentMaterialClass)parent_material) {
        case PARENT_MATERIAL_GRANITIC:              fertility = 0.18f; break;
        case PARENT_MATERIAL_METAMORPHIC:           fertility = 0.28f; break;
        case PARENT_MATERIAL_MAFIC_VOLCANIC:        fertility = 0.68f; break;
        case PARENT_MATERIAL_INTERMEDIATE_VOLCANIC: fertility = 0.58f; break;
        case PARENT_MATERIAL_SILICICLASTIC:         fertility = 0.34f; break;
        case PARENT_MATERIAL_CARBONATE:             fertility = 0.56f; break;
        case PARENT_MATERIAL_ORGANIC:               fertility = 0.60f; break;
        case PARENT_MATERIAL_ALLUVIAL:              fertility = 0.62f; break;
        case PARENT_MATERIAL_AEOLIAN:               fertility = 0.48f; break;
        case PARENT_MATERIAL_EVAPORITIC:            fertility = 0.12f; break;
        case PARENT_MATERIAL_NONE:
        default:                                    fertility = 0.36f; break;
    }

    switch ((SdkSurfaceSediment)surface_sediment) {
        case SEDIMENT_FINE_ALLUVIUM:
        case SEDIMENT_DELTAIC_SILT:
            fertility += 0.12f;
            break;
        case SEDIMENT_LACUSTRINE_CLAY:
            fertility += 0.10f;
            break;
        case SEDIMENT_PEAT:
            fertility += 0.04f;
            if (reaction_value < 0.25f) fertility -= 0.08f;
            break;
        case SEDIMENT_TALUS:
        case SEDIMENT_COLLUVIUM:
            fertility -= 0.12f;
            break;
        case SEDIMENT_BEACH_SAND:
        case SEDIMENT_EOILIAN_SAND:
        case SEDIMENT_MARINE_SAND:
            fertility -= 0.18f;
            break;
        case SEDIMENT_LOESS:
            fertility += 0.16f;
            break;
        case SEDIMENT_CALCAREOUS_RESIDUAL:
            fertility += 0.06f;
            break;
        case SEDIMENT_SAPROLITE:
            if (parent_material == PARENT_MATERIAL_MAFIC_VOLCANIC ||
                parent_material == PARENT_MATERIAL_INTERMEDIATE_VOLCANIC) {
                fertility += 0.10f;
            } else {
                fertility -= 0.02f;
            }
            break;
        default:
            break;
    }

    if (cell->slope >= 16) fertility -= 0.10f;

    switch ((SdkMoistureBand)cell->moisture_band) {
        case MOISTURE_ARID:      fertility -= 0.12f; break;
        case MOISTURE_SEMI_ARID: fertility -= 0.05f; break;
        case MOISTURE_HUMID:
        case MOISTURE_PERHUMID:  fertility += 0.05f; break;
        default: break;
    }

    if (salinity_value > 0.40f) fertility -= 0.22f;
    return sdk_worldgen_clampf(fertility, 0.0f, 1.0f);
}

static uint8_t pick_surface_sediment(const SdkWorldGenMacroCell* cell, int sea_level)
{
    float roll;

    if (!cell) return SEDIMENT_NONE;

    roll = sdk_worldgen_hashf(sdk_worldgen_hash32((uint32_t)(cell->surface_height * 17 + cell->base_height * 11)));

    if (cell->surface_height <= sea_level) {
        if (cell->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
            cell->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) {
            return (cell->slope <= 4) ? SEDIMENT_MARINE_SAND : SEDIMENT_MARINE_MUD;
        }
        return SEDIMENT_MARINE_MUD;
    }

    if (cell->terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND) return SEDIMENT_PEAT;
    if (cell->river_order >= 3 && cell->coast_distance <= 2) return SEDIMENT_DELTAIC_SILT;
    if (cell->terrain_province == TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND && cell->river_order >= 2) {
        return (cell->slope <= 3) ? SEDIMENT_FINE_ALLUVIUM : SEDIMENT_COARSE_ALLUVIUM;
    }
    if (cell->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) return SEDIMENT_BEACH_SAND;
    if (cell->terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE) {
        if (cell->moisture_band <= MOISTURE_SEMI_ARID && cell->slope <= 6) return SEDIMENT_EOILIAN_SAND;
        return SEDIMENT_COARSE_ALLUVIUM;
    }
    if (cell->bedrock_province == BEDROCK_PROVINCE_CARBONATE_PLATFORM &&
        cell->slope <= 10 &&
        cell->moisture_band >= MOISTURE_HUMID &&
        cell->river_order == 0) {
        return SEDIMENT_CALCAREOUS_RESIDUAL;
    }
    if ((cell->bedrock_province == BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS ||
         cell->bedrock_province == BEDROCK_PROVINCE_METAMORPHIC_BELT ||
         cell->bedrock_province == BEDROCK_PROVINCE_GRANITIC_INTRUSIVE ||
         cell->bedrock_province == BEDROCK_PROVINCE_FLOOD_BASALT ||
         cell->bedrock_province == BEDROCK_PROVINCE_VOLCANIC_ARC) &&
        cell->moisture_band >= MOISTURE_HUMID &&
        cell->temperature_band >= TEMP_WARM_TEMPERATE &&
        cell->slope <= 12 &&
        cell->terrain_province != TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND &&
        cell->terrain_province != TERRAIN_PROVINCE_ESTUARY_DELTA) {
        return SEDIMENT_SAPROLITE;
    }
    if ((cell->bedrock_province == BEDROCK_PROVINCE_FLOOD_BASALT ||
         cell->bedrock_province == BEDROCK_PROVINCE_VOLCANIC_ARC) &&
        cell->moisture_band <= MOISTURE_SUBHUMID &&
        cell->slope <= 8 &&
        roll > 0.58f) {
        return SEDIMENT_LOESS;
    }
    if (cell->river_order >= 2 && cell->floodplain_width > 0) return SEDIMENT_FINE_ALLUVIUM;
    if (cell->slope >= 28) return SEDIMENT_TALUS;
    if (cell->slope >= 16) return SEDIMENT_COLLUVIUM;
    if (cell->bedrock_province == BEDROCK_PROVINCE_VOLCANIC_ARC && roll > 0.72f) {
        return SEDIMENT_VOLCANIC_ASH;
    }
    if (cell->terrain_province == TERRAIN_PROVINCE_RIFT_VALLEY &&
        cell->moisture_band >= MOISTURE_HUMID &&
        cell->slope <= 6) {
        return SEDIMENT_LACUSTRINE_CLAY;
    }
    return SEDIMENT_RESIDUAL_SOIL;
}

static uint8_t pick_ecology(const SdkWorldGenMacroCell* cell, int sea_level,
                            uint8_t surface_sediment,
                            uint8_t parent_material,
                            uint8_t drainage_class,
                            uint8_t soil_reaction,
                            uint8_t soil_fertility,
                            uint8_t soil_salinity,
                            uint8_t water_table_depth)
{
    if (cell->surface_height <= sea_level) return ECOLOGY_BARREN;
    if (cell->terrain_province == TERRAIN_PROVINCE_SALT_FLAT_PLAYA &&
        cell->moisture_band <= MOISTURE_SEMI_ARID) {
        return ECOLOGY_SALT_DESERT;
    }
    if (cell->terrain_province == TERRAIN_PROVINCE_ALPINE_BELT) {
        if (cell->temperature_band <= TEMP_POLAR) return ECOLOGY_NIVAL_ICE;
        if (cell->temperature_band <= TEMP_SUBPOLAR) return ECOLOGY_TUNDRA;
        return ECOLOGY_ALPINE_MEADOW;
    }

    if ((cell->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA ||
         cell->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) &&
        cell->temperature_band >= TEMP_SUBTROPICAL &&
        water_table_depth <= 1u &&
        cell->moisture_band >= MOISTURE_HUMID) {
        return ECOLOGY_MANGROVE_SWAMP;
    }

    if (drainage_class == DRAINAGE_WATERLOGGED || water_table_depth <= 1u) {
        if (cell->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA ||
            (cell->coast_distance >= -1 && cell->coast_distance <= 2)) {
            return ECOLOGY_ESTUARY_WETLAND;
        }
        if (surface_sediment == SEDIMENT_PEAT ||
            parent_material == PARENT_MATERIAL_ORGANIC ||
            soil_reaction <= SOIL_REACTION_ACID) {
            return ECOLOGY_BOG;
        }
        return ECOLOGY_FEN;
    }

    if (cell->river_order >= 2 &&
        parent_material == PARENT_MATERIAL_ALLUVIAL &&
        water_table_depth <= 3u &&
        cell->moisture_band >= MOISTURE_SUBHUMID) {
        return (cell->moisture_band >= MOISTURE_HUMID) ? ECOLOGY_RIPARIAN_FOREST : ECOLOGY_FLOODPLAIN_MEADOW;
    }

    if (cell->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST &&
        cell->moisture_band <= MOISTURE_SUBHUMID) {
        return ECOLOGY_DUNE_COAST;
    }
    if (cell->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ||
        cell->terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU) {
        if (cell->moisture_band <= MOISTURE_SUBHUMID || soil_fertility <= SOIL_FERTILITY_LOW) {
            return ECOLOGY_VOLCANIC_BARRENS;
        }
    }
    if (soil_salinity >= SOIL_SALINITY_HIGH) return ECOLOGY_SALT_DESERT;
    if (cell->terrain_province == TERRAIN_PROVINCE_BADLANDS_DISSECTED) return ECOLOGY_SCRUB_BADLANDS;
    if (soil_salinity >= SOIL_SALINITY_MODERATE) return ECOLOGY_STEPPE;

    if (cell->moisture_band <= MOISTURE_ARID) return ECOLOGY_HOT_DESERT;

    if (cell->moisture_band >= MOISTURE_HUMID) {
        if (cell->temperature_band <= TEMP_SUBPOLAR) return ECOLOGY_BOREAL_TAIGA;
        if (cell->temperature_band <= TEMP_WARM_TEMPERATE) {
            return (soil_fertility >= SOIL_FERTILITY_MODERATE) ? ECOLOGY_TEMPERATE_DECIDUOUS_FOREST : ECOLOGY_TEMPERATE_CONIFER_FOREST;
        }
        if (cell->temperature_band == TEMP_SUBTROPICAL) return ECOLOGY_TROPICAL_SEASONAL_FOREST;
        return ECOLOGY_TROPICAL_RAINFOREST;
    }
    if (cell->moisture_band <= MOISTURE_SEMI_ARID ||
        soil_fertility <= SOIL_FERTILITY_LOW ||
        drainage_class == DRAINAGE_EXCESSIVE) {
        if (cell->temperature_band >= TEMP_SUBTROPICAL &&
            soil_fertility >= SOIL_FERTILITY_MODERATE &&
            drainage_class >= DRAINAGE_MODERATE) {
            return ECOLOGY_MEDITERRANEAN_SCRUB;
        }
        return ECOLOGY_STEPPE;
    }
    if (cell->temperature_band <= TEMP_SUBPOLAR) return ECOLOGY_TUNDRA;
    if (cell->temperature_band >= TEMP_SUBTROPICAL &&
        soil_fertility >= SOIL_FERTILITY_MODERATE &&
        drainage_class >= DRAINAGE_MODERATE) {
        return ECOLOGY_SAVANNA_GRASSLAND;
    }
    if (cell->temperature_band >= TEMP_SUBTROPICAL) return ECOLOGY_TROPICAL_SEASONAL_FOREST;
    return ECOLOGY_PRAIRIE;
}

static uint32_t macro_landform_flags_for_cell(const SdkWorldGenMacroCell* cell, int sea_level)
{
    uint32_t flags = SDK_LANDFORM_NONE;

    if (!cell) return flags;
    if (cell->river_order >= 1u && cell->surface_height > sea_level) flags |= SDK_LANDFORM_RIVER_CHANNEL;
    if (cell->floodplain_width >= 2u) flags |= SDK_LANDFORM_FLOODPLAIN;
    if (cell->terrain_province == TERRAIN_PROVINCE_RIFT_VALLEY ||
        cell->terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND ||
        cell->terrain_province == TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT) {
        flags |= SDK_LANDFORM_RAVINE;
    }
    if (cell->terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND ||
        cell->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA) {
        flags |= SDK_LANDFORM_LAKE_BASIN;
    }
    if (cell->terrain_province == TERRAIN_PROVINCE_CARBONATE_UPLAND ||
        cell->terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND ||
        cell->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC) {
        flags |= SDK_LANDFORM_CAVE_ENTRANCE;
    }
    if (cell->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ||
        cell->terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU) {
        flags |= SDK_LANDFORM_VOLCANIC_VENT;
        if (cell->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC) flags |= SDK_LANDFORM_CALDERA;
        flags |= SDK_LANDFORM_LAVA_FIELD;
    }
    return flags;
}

static SdkResourceProvince pick_resource_province(const SdkWorldGenMacroCell* cell)
{
    if (cell->bedrock_province == BEDROCK_PROVINCE_SILICICLASTIC_BASIN ||
        cell->bedrock_province == BEDROCK_PROVINCE_RIFT_SEDIMENTARY) {
        if (cell->terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND) {
            return RESOURCE_PROVINCE_COALFIELD;
        }
        if (cell->surface_height > 20 && cell->surface_height < 100 &&
            cell->temperature_band >= TEMP_WARM_TEMPERATE && cell->temperature_band <= TEMP_SUBTROPICAL) {
            return RESOURCE_PROVINCE_OIL_FIELD;
        }
    }
    
    if (cell->bedrock_province == BEDROCK_PROVINCE_CARBONATE_PLATFORM) {
        if (cell->temperature_band >= TEMP_WARM_TEMPERATE && cell->moisture_band <= MOISTURE_SEMI_ARID) {
            return RESOURCE_PROVINCE_PHOSPHATE_DEPOSIT;
        }
        return RESOURCE_PROVINCE_CARBONATE_CEMENT_DISTRICT;
    }
    
    if (cell->bedrock_province == BEDROCK_PROVINCE_VOLCANIC_ARC) {
        if (cell->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ||
            cell->terrain_province == TERRAIN_PROVINCE_BASALT_PLATEAU) {
            return RESOURCE_PROVINCE_SULFUR_VOLCANIC_DISTRICT;
        }
        return RESOURCE_PROVINCE_VOLCANIC_METALS;
    }
    
    if (cell->bedrock_province == BEDROCK_PROVINCE_GRANITIC_INTRUSIVE) {
        if (cell->terrain_province == TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT ||
            cell->terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND) {
            return RESOURCE_PROVINCE_COPPER_PORPHYRY_BELT;
        }
    }
    
    if (cell->bedrock_province == BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS) {
        if (cell->surface_height > 100 && cell->temperature_band <= TEMP_COOL_TEMPERATE) {
            return RESOURCE_PROVINCE_URANIUM_GRANITE_BELT;
        }
    }
    
    if (cell->bedrock_province == BEDROCK_PROVINCE_METAMORPHIC_BELT) {
        if (cell->terrain_province == TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT ||
            cell->terrain_province == TERRAIN_PROVINCE_ALPINE_BELT) {
            return RESOURCE_PROVINCE_STRATEGIC_ALLOY_BELT;
        }
    }
    
    if (cell->terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE && 
        cell->moisture_band == MOISTURE_ARID) {
        return RESOURCE_PROVINCE_SALTPETER_NITRATE_FIELD;
    }
    
    if ((cell->bedrock_province == BEDROCK_PROVINCE_GRANITIC_INTRUSIVE ||
         cell->bedrock_province == BEDROCK_PROVINCE_METAMORPHIC_BELT) &&
        cell->terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND) {
        return RESOURCE_PROVINCE_RARE_EARTH_DISTRICT;
    }
    
    if (cell->terrain_province == TERRAIN_PROVINCE_SILICICLASTIC_HILLS &&
        cell->temperature_band >= TEMP_SUBTROPICAL &&
        cell->moisture_band >= MOISTURE_HUMID) {
        return RESOURCE_PROVINCE_BAUXITE_DEPOSIT;
    }
    
    if ((cell->bedrock_province == BEDROCK_PROVINCE_CARBONATE_PLATFORM ||
         cell->bedrock_province == BEDROCK_PROVINCE_SILICICLASTIC_BASIN) &&
        cell->terrain_province == TERRAIN_PROVINCE_CARBONATE_UPLAND) {
        return RESOURCE_PROVINCE_LEAD_ZINC_DISTRICT;
    }
    
    if (cell->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA ||
        cell->terrain_province == TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND) {
        return RESOURCE_PROVINCE_CLAY_DISTRICT;
    }
    
    if ((cell->bedrock_province == BEDROCK_PROVINCE_METAMORPHIC_BELT ||
         cell->terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND ||
         cell->terrain_province == TERRAIN_PROVINCE_UPLIFTED_PLATEAU) &&
        cell->temperature_band >= TEMP_SUBTROPICAL &&
        cell->moisture_band >= MOISTURE_HUMID) {
        return RESOURCE_PROVINCE_IRON_BELT;
    }
    
    if (cell->terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND ||
        cell->terrain_province == TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT ||
        cell->terrain_province == TERRAIN_PROVINCE_ALPINE_BELT) {
        return RESOURCE_PROVINCE_IRON_BELT;
    }
    
    if (cell->terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND ||
        cell->bedrock_province == BEDROCK_PROVINCE_SILICICLASTIC_BASIN) {
        return RESOURCE_PROVINCE_COALFIELD;
    }
    
    return RESOURCE_PROVINCE_AGGREGATE_DISTRICT;
}

static uint8_t pick_hydrocarbon_class(const SdkWorldGenMacroCell* cell, SdkResourceProvince province)
{
    if (!cell || province != RESOURCE_PROVINCE_OIL_FIELD) return SDK_HYDROCARBON_NONE;

    if (cell->surface_sediment == SEDIMENT_BEACH_SAND ||
        cell->surface_sediment == SEDIMENT_EOILIAN_SAND ||
        cell->surface_sediment == SEDIMENT_MARINE_SAND ||
        cell->terrain_province == TERRAIN_PROVINCE_ARID_FAN_STEPPE) {
        return SDK_HYDROCARBON_OIL_SAND;
    }
    return SDK_HYDROCARBON_OIL_SHALE;
}

static uint8_t pick_resource_grade(const SdkWorldGenMacroCell* cell, SdkResourceProvince province)
{
    float grade = 0.15f;

    if (!cell || province == RESOURCE_PROVINCE_NONE) return 0u;

    switch (province) {
        case RESOURCE_PROVINCE_OIL_FIELD:
            grade += (cell->bedrock_province == BEDROCK_PROVINCE_RIFT_SEDIMENTARY) ? 0.28f : 0.18f;
            grade += (cell->bedrock_province == BEDROCK_PROVINCE_SILICICLASTIC_BASIN) ? 0.16f : 0.0f;
            grade += (cell->terrain_province == TERRAIN_PROVINCE_RIFT_VALLEY) ? 0.14f : 0.0f;
            grade += (cell->temperature_band >= TEMP_WARM_TEMPERATE && cell->temperature_band <= TEMP_SUBTROPICAL) ? 0.08f : 0.0f;
            grade += (cell->surface_height > 20 && cell->surface_height < 110) ? 0.08f : 0.0f;
            grade += (cell->moisture_band <= MOISTURE_SUBHUMID) ? 0.10f : 0.0f;
            grade += (cell->surface_sediment == SEDIMENT_MARINE_SAND || cell->surface_sediment == SEDIMENT_EOILIAN_SAND) ? 0.08f : 0.0f;
            break;
        case RESOURCE_PROVINCE_COALFIELD:
        case RESOURCE_PROVINCE_IRON_BELT:
        case RESOURCE_PROVINCE_CARBONATE_CEMENT_DISTRICT:
        case RESOURCE_PROVINCE_SULFUR_VOLCANIC_DISTRICT:
            grade += 0.30f;
            break;
        default:
            grade += 0.20f;
            break;
    }

    return sdk_worldgen_pack_unorm8(sdk_worldgen_clampf(grade, 0.0f, 1.0f));
}

static void apply_surface_outputs(SdkWorldGenMacroCell* cell, int sea_level)
{
    uint8_t surface_sediment;
    uint8_t parent_material;
    uint8_t water_table_depth;
    float drainage_index;
    float soil_salinity_value;
    float soil_reaction_value;
    float soil_fertility_value;
    float detail_norm;
    float relief;
    float water_temp;

    cell->province_family = province_family_for(cell->terrain_province);
    detail_norm = sdk_worldgen_clampf(detail_amp_for_province(cell->terrain_province) / 22.0f, 0.0f, 1.0f);
    cell->detail_amp = sdk_worldgen_pack_unorm8(detail_norm);
    cell->stratigraphy_province = stratigraphy_province_for_cell(cell, sea_level);

    surface_sediment = pick_surface_sediment(cell, sea_level);
    parent_material = parent_material_for_cell(cell, surface_sediment);
    water_table_depth = water_table_depth_for_cell(cell, sea_level, surface_sediment);
    drainage_index = drainage_index_for_cell(cell, surface_sediment, parent_material, water_table_depth);
    soil_salinity_value = soil_salinity_value_for_cell(cell, sea_level, surface_sediment);
    soil_reaction_value = soil_reaction_value_for_cell(cell, surface_sediment, parent_material, soil_salinity_value);
    soil_fertility_value = soil_fertility_value_for_cell(cell, surface_sediment, parent_material,
                                                         soil_salinity_value, soil_reaction_value);

    cell->surface_sediment = surface_sediment;
    cell->parent_material = parent_material;
    cell->water_table_depth = water_table_depth;
    cell->drainage_class = classify_drainage(drainage_index, water_table_depth);
    cell->soil_salinity = classify_soil_salinity(soil_salinity_value);
    cell->soil_reaction = classify_soil_reaction(soil_reaction_value, soil_salinity_value);
    cell->soil_fertility = classify_soil_fertility(soil_fertility_value);

    if (cell->surface_height <= sea_level) {
        cell->ecology = ECOLOGY_BARREN;
        cell->resource_province = RESOURCE_PROVINCE_NONE;
        cell->hydrocarbon_class = SDK_HYDROCARBON_NONE;
        cell->resource_grade = 0u;
        cell->soil_depth = 0;
        cell->sediment_depth = (surface_sediment == SEDIMENT_MARINE_MUD) ? 6 : 4;
    } else {
        cell->resource_province = pick_resource_province(cell);
        cell->hydrocarbon_class = pick_hydrocarbon_class(cell, (SdkResourceProvince)cell->resource_province);
        cell->resource_grade = pick_resource_grade(cell, (SdkResourceProvince)cell->resource_province);
        cell->ecology = pick_ecology(cell, sea_level,
                                     surface_sediment,
                                     parent_material,
                                     cell->drainage_class,
                                     cell->soil_reaction,
                                     cell->soil_fertility,
                                     cell->soil_salinity,
                                     water_table_depth);

        switch ((SdkSurfaceSediment)surface_sediment) {
            case SEDIMENT_BEACH_SAND:
            case SEDIMENT_EOILIAN_SAND:
            case SEDIMENT_MARINE_SAND:
                cell->soil_depth = 1;
                cell->sediment_depth = 4;
                break;
            case SEDIMENT_DELTAIC_SILT:
            case SEDIMENT_FINE_ALLUVIUM:
            case SEDIMENT_LACUSTRINE_CLAY:
            case SEDIMENT_MARINE_MUD:
                cell->soil_depth = 2;
                cell->sediment_depth = 5;
                break;
            case SEDIMENT_PEAT:
                cell->soil_depth = 1;
                cell->sediment_depth = 4;
                break;
            case SEDIMENT_LOESS:
                cell->soil_depth = 3;
                cell->sediment_depth = 4;
                break;
            case SEDIMENT_CALCAREOUS_RESIDUAL:
            case SEDIMENT_SAPROLITE:
                cell->soil_depth = 2;
                cell->sediment_depth = 4;
                break;
            case SEDIMENT_TALUS:
            case SEDIMENT_COARSE_ALLUVIUM:
                cell->soil_depth = 1;
                cell->sediment_depth = 3;
                break;
            default:
                cell->soil_depth = (cell->slope >= 16) ? 1 : 3;
                cell->sediment_depth = 3;
                break;
        }
    }

    water_temp = derive_water_temperature(cell, sea_level);
    cell->water_temp = sdk_worldgen_pack_unorm8(water_temp);
    cell->water_surface_class = classify_surface_water(cell, sea_level, water_temp);
    cell->landform_flags = macro_landform_flags_for_cell(cell, sea_level);

    relief = sdk_worldgen_unpack_unorm8(cell->relief_strength);
    relief = fmaxf(relief, sdk_worldgen_clampf((float)cell->slope / 34.0f, 0.0f, 1.0f) * 0.60f +
                           detail_norm * 0.25f);
    if (cell->terrain_province == TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT ||
        cell->terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC ||
        cell->terrain_province == TERRAIN_PROVINCE_HARDROCK_HIGHLAND ||
        cell->terrain_province == TERRAIN_PROVINCE_ALPINE_BELT) {
        relief = sdk_worldgen_clampf(relief + 0.12f, 0.0f, 1.0f);
    }
    cell->relief_strength = sdk_worldgen_pack_unorm8(relief);
}

static void smooth_terrain_provinces(SdkWorldGenMacroTile* tile)
{
    uint8_t* src;
    uint8_t* dst;
    int pass;
    int x;
    int z;

    src = (uint8_t*)malloc(SDK_WORLDGEN_TILE_CELL_COUNT);
    dst = (uint8_t*)malloc(SDK_WORLDGEN_TILE_CELL_COUNT);
    if (!src || !dst) {
        free(src);
        free(dst);
        return;
    }

    for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
            src[idx_of(x, z)] = tile->cells[idx_of(x, z)].terrain_province;
        }
    }

    for (pass = 0; pass < 2; ++pass) {
        for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
            for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
                int idx = idx_of(x, z);
                const SdkWorldGenMacroCell* cell = &tile->cells[idx];
                uint8_t current = src[idx];
                uint8_t current_family = province_family_for(current);
                uint8_t candidate_vals[9];
                int candidate_weights[9];
                int candidate_count = 0;
                int current_weight = 0;
                int best_weight = -1;
                uint8_t best_province = current;
                int same_cardinals = 0;
                int dx;
                int dz;
                int i;

                dst[idx] = current;

                if (is_hydrology_critical_province(current)) continue;
                if (cell->river_order >= 2 || abs(cell->coast_distance) <= 2) continue;

                for (dz = -1; dz <= 1; ++dz) {
                    for (dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx;
                        int nz = z + dz;
                        int weight;
                        uint8_t neighbor;
                        int found = -1;

                        if (!in_bounds(nx, nz)) continue;
                        neighbor = src[idx_of(nx, nz)];
                        if (province_family_for(neighbor) != current_family) continue;

                        weight = (dx == 0 && dz == 0) ? 4 : ((dx == 0 || dz == 0) ? 2 : 1);
                        if ((dx == 0 || dz == 0) && !(dx == 0 && dz == 0) && neighbor == current) {
                            ++same_cardinals;
                        }

                        for (i = 0; i < candidate_count; ++i) {
                            if (candidate_vals[i] == neighbor) {
                                found = i;
                                break;
                            }
                        }

                        if (found < 0 && candidate_count < 9) {
                            found = candidate_count++;
                            candidate_vals[found] = neighbor;
                            candidate_weights[found] = 0;
                        }
                        if (found >= 0) candidate_weights[found] += weight;
                    }
                }

                for (i = 0; i < candidate_count; ++i) {
                    if (candidate_vals[i] == current) current_weight = candidate_weights[i];
                    if (candidate_weights[i] > best_weight ||
                        (candidate_weights[i] == best_weight && candidate_vals[i] < best_province)) {
                        best_weight = candidate_weights[i];
                        best_province = candidate_vals[i];
                    }
                }

                if (best_province != current) {
                    if ((current_weight <= 4 && best_weight >= 6) ||
                        (same_cardinals <= 1 && best_weight >= current_weight + 2) ||
                        (best_weight >= current_weight + 5)) {
                        dst[idx] = best_province;
                    }
                }
            }
        }

        for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
            for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
                src[idx_of(x, z)] = dst[idx_of(x, z)];
            }
        }
    }

    for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
            tile->cells[idx_of(x, z)].terrain_province = src[idx_of(x, z)];
        }
    }

    free(src);
    free(dst);
}

static void derive_province_outputs(SdkWorldGen* wg, SdkWorldGenMacroTile* tile)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    int x;
    int z;

    for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
            SdkWorldGenMacroCell* cell = &tile->cells[idx_of(x, z)];
            int land = is_land(cell, impl->sea_level);
            int relief = cell->surface_height - impl->sea_level;
            float coast_factor = 1.0f - sdk_worldgen_clampf((float)abs(cell->coast_distance) / 48.0f, 0.0f, 1.0f);
            float river_factor = sdk_worldgen_clampf((float)cell->flow_accum / 2400.0f, 0.0f, 1.0f);
            float continental_wetness = sdk_worldgen_unpack_unorm8(cell->wetness);
            float continental_temp = temperature_band_midpoint(cell->temperature_band);
            float wetness = continental_wetness + coast_factor * 0.18f + river_factor * 0.20f;
            float maritime = sdk_worldgen_clampf(1.0f - (float)abs(cell->coast_distance) / 96.0f, 0.0f, 1.0f);
            float rain_shadow = 0.0f;
            float temp_val;
            rain_shadow = sdk_worldgen_clampf((sdk_worldgen_unpack_unorm8(cell->relief_strength) * 0.24f +
                                               (float)cell->slope / 42.0f) *
                                              sdk_worldgen_clampf((float)cell->coast_distance / 48.0f, 0.0f, 1.0f),
                                              0.0f, 0.24f);
            temp_val = continental_temp + maritime * 0.04f -
                       sdk_worldgen_clampf((float)relief / 320.0f, 0.0f, 0.72f);
            if (cell->boundary_class > 0 && cell->surface_height > impl->sea_level + 180) temp_val -= 0.04f;
            wetness -= sdk_worldgen_clampf((float)cell->slope / 42.0f, 0.0f, 0.25f);
            wetness -= rain_shadow;
            wetness += (cell->river_order >= 2) ? 0.08f : 0.0f;
            wetness += sdk_worldgen_unpack_unorm8(cell->lake_mask) * 0.06f;
            wetness = sdk_worldgen_clampf(wetness, 0.0f, 1.0f);

            cell->temperature_band = classify_temperature(temp_val);
            cell->moisture_band = classify_moisture(wetness);
            cell->wetness = sdk_worldgen_pack_unorm8(wetness);
            cell->river_strength = sdk_worldgen_pack_unorm8(sdk_worldgen_clampf(fmaxf(river_factor,
                (float)cell->river_order * 0.18f + sdk_worldgen_unpack_unorm8(cell->lake_mask) * 0.08f), 0.0f, 1.0f));

            if (!land) {
                cell->terrain_province = (cell->surface_height < impl->sea_level - 42)
                    ? TERRAIN_PROVINCE_OPEN_OCEAN
                    : TERRAIN_PROVINCE_CONTINENTAL_SHELF;
                continue;
            }

            if (cell->river_order >= 3 && cell->coast_distance <= 2) {
                cell->terrain_province = TERRAIN_PROVINCE_ESTUARY_DELTA;
            } else if (cell->coast_distance <= 1 && cell->slope >= 10) {
                cell->terrain_province = TERRAIN_PROVINCE_DYNAMIC_COAST;
            } else if (cell->moisture_band == MOISTURE_WATERLOGGED && cell->slope <= 4) {
                cell->terrain_province = TERRAIN_PROVINCE_PEAT_WETLAND;
            } else if (cell->river_order >= 2 && cell->floodplain_width >= 3) {
                cell->terrain_province = TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND;
            } else if (cell->boundary_class < 0 && cell->surface_height > impl->sea_level + 12 && cell->slope <= 12) {
                cell->terrain_province = TERRAIN_PROVINCE_RIFT_VALLEY;
            } else if ((cell->bedrock_province == BEDROCK_PROVINCE_VOLCANIC_ARC) &&
                       cell->surface_height > impl->sea_level + 90) {
                cell->terrain_province = TERRAIN_PROVINCE_VOLCANIC_ARC;
            } else if (cell->bedrock_province == BEDROCK_PROVINCE_FLOOD_BASALT &&
                       cell->surface_height > impl->sea_level + 60) {
                cell->terrain_province = TERRAIN_PROVINCE_BASALT_PLATEAU;
            } else if (cell->boundary_class > 0 && cell->surface_height > impl->sea_level + 150) {
                cell->terrain_province = TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT;
            } else if (cell->surface_height > impl->sea_level + 170 &&
                       cell->temperature_band <= TEMP_COOL_TEMPERATE) {
                cell->terrain_province = TERRAIN_PROVINCE_ALPINE_BELT;
            } else if (cell->surface_height > impl->sea_level + 120 && cell->slope <= 12) {
                cell->terrain_province = TERRAIN_PROVINCE_UPLIFTED_PLATEAU;
            } else if (cell->bedrock_province == BEDROCK_PROVINCE_CARBONATE_PLATFORM) {
                cell->terrain_province = TERRAIN_PROVINCE_CARBONATE_UPLAND;
            } else if (cell->bedrock_province == BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS ||
                       cell->bedrock_province == BEDROCK_PROVINCE_METAMORPHIC_BELT ||
                       cell->bedrock_province == BEDROCK_PROVINCE_GRANITIC_INTRUSIVE) {
                cell->terrain_province = TERRAIN_PROVINCE_HARDROCK_HIGHLAND;
            } else if (cell->moisture_band <= MOISTURE_SEMI_ARID &&
                       cell->slope >= 4 && cell->slope <= 14) {
                cell->terrain_province = TERRAIN_PROVINCE_ARID_FAN_STEPPE;
            } else {
                cell->terrain_province = TERRAIN_PROVINCE_SILICICLASTIC_HILLS;
            }
        }
    }

    smooth_terrain_provinces(tile);

    for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
            apply_surface_outputs(&tile->cells[idx_of(x, z)], impl->sea_level);
        }
    }
}

void sdk_worldgen_run_hydrology(SdkWorldGen* wg, SdkWorldGenMacroTile* tile)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    int origin_x;
    int origin_z;
    int x;
    int z;

    if (!wg || !impl || !tile) return;

    origin_x = tile->tile_x * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;
    origin_z = tile->tile_z * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;

    for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
            int idx = idx_of(x, z);
            SdkWorldGenMacroCell* cell = &tile->cells[idx];
            int macro_x = origin_x + x;
            int macro_z = origin_z + z;
            int wx = macro_x * (int)impl->macro_cell_size + (int)impl->macro_cell_size / 2;
            int wz = macro_z * (int)impl->macro_cell_size + (int)impl->macro_cell_size / 2;
            SdkContinentalSample continent;
            float lat_phase;
            float temp_equator;
            float temp_val;
            float macro_wetness;
            float flow_scaled;

            sdk_worldgen_sample_continental_state(wg, wx, wz, &continent);

            lat_phase = fmodf((float)wz / 131072.0f, 2.0f);
            if (lat_phase < 0.0f) lat_phase += 2.0f;
            temp_equator = 1.0f - fabsf(lat_phase - 1.0f);
            temp_val = temp_equator -
                       sdk_worldgen_clampf((float)(cell->surface_height - impl->sea_level) / 320.0f, 0.0f, 0.74f);
            temp_val += sdk_worldgen_clampf(1.0f - fabsf(continent.coast_distance) / 8.0f, 0.0f, 1.0f) * 0.04f;
            macro_wetness = sdk_worldgen_clampf(continent.precipitation, 0.0f, 1.0f);
            flow_scaled = continent.flow_accum * (0.32f + continent.runoff * 0.58f);

            cell->flow_accum = (uint32_t)sdk_worldgen_clampi((int)lrintf(flow_scaled), 0, 65535);
            cell->downstream_dir = 255u;
            cell->slope = 0;
            cell->coast_distance = (int16_t)lrintf(continent.coast_distance *
                                                   ((float)SDK_WORLDGEN_CONTINENT_CELL_BLOCKS / (float)impl->macro_cell_size));
            cell->temperature_band = classify_temperature(temp_val);
            cell->moisture_band = classify_moisture(macro_wetness);
            cell->wetness = sdk_worldgen_pack_unorm8(macro_wetness);
            cell->river_strength = sdk_worldgen_pack_unorm8(sdk_worldgen_clampf(flow_scaled / 3800.0f, 0.0f, 1.0f));
            cell->river_order = (uint8_t)sdk_worldgen_clampi((int)lrintf(continent.trunk_river_order), 0, 4);
            if (cell->river_order == 0u) {
                if (cell->flow_accum > 3200u) cell->river_order = 4u;
                else if (cell->flow_accum > 1200u) cell->river_order = 3u;
                else if (cell->flow_accum > 360u) cell->river_order = 2u;
                else if (cell->flow_accum > 120u) cell->river_order = 1u;
            }
            cell->floodplain_width = (cell->river_order > 0u)
                ? (uint8_t)sdk_worldgen_clampi(1 + (int)cell->river_order * 2 +
                                               (int)(continent.flood_risk * 4.0f) +
                                               (int)(cell->flow_accum / 1400u), 0, 10)
                : 0u;
            if (sdk_worldgen_unpack_unorm8(continent.lake_mask) >= 0.24f && continent.lake_level > (float)impl->sea_level + 1.0f) {
                cell->lake_mask = continent.lake_mask;
                cell->closed_basin_mask = continent.closed_basin_mask;
                cell->lake_level = (int16_t)sdk_worldgen_clampi((int)lrintf(continent.lake_level), 0, CHUNK_HEIGHT - 1);
            } else {
                cell->lake_mask = 0u;
                cell->closed_basin_mask = 0u;
                cell->lake_level = 0;
            }
            cell->basin_id = continent.basin_id;
            cell->lake_id = continent.lake_id;
        }
    }

    for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
            SdkWorldGenMacroCell* cell = &tile->cells[idx_of(x, z)];
            int macro_x = origin_x + x;
            int macro_z = origin_z + z;
            int wx = macro_x * (int)impl->macro_cell_size + (int)impl->macro_cell_size / 2;
            int wz = macro_z * (int)impl->macro_cell_size + (int)impl->macro_cell_size / 2;
            SdkContinentalSample continent;
            int best_dir = 255;
            int best_drop = 0;
            int i;

            if (!is_land(cell, impl->sea_level)) continue;

            sdk_worldgen_sample_continental_state(wg, wx, wz, &continent);
            if (!(cell->closed_basin_mask > 0u && cell->lake_mask > 0u)) {
                int current_ccx = floor_div_local(wx, SDK_WORLDGEN_CONTINENT_CELL_BLOCKS);
                int current_ccz = floor_div_local(wz, SDK_WORLDGEN_CONTINENT_CELL_BLOCKS);
                if (continent.downstream_cx != current_ccx || continent.downstream_cz != current_ccz) {
                    float target_wx = (float)(continent.downstream_cx * SDK_WORLDGEN_CONTINENT_CELL_BLOCKS +
                                              SDK_WORLDGEN_CONTINENT_CELL_BLOCKS / 2);
                    float target_wz = (float)(continent.downstream_cz * SDK_WORLDGEN_CONTINENT_CELL_BLOCKS +
                                              SDK_WORLDGEN_CONTINENT_CELL_BLOCKS / 2);
                    best_dir = direction_from_vector(target_wx - (float)wx, target_wz - (float)wz);
                }
            }

            if (best_dir >= 8u) {
                for (i = 0; i < 8; ++i) {
                    int nx = x + g_dir8_dx[i];
                    int nz = z + g_dir8_dz[i];
                    int drop;
                    if (!in_bounds(nx, nz)) continue;
                    drop = (int)cell->surface_height - (int)tile->cells[idx_of(nx, nz)].surface_height;
                    if (drop > best_drop) {
                        best_drop = drop;
                        best_dir = i;
                    }
                }
            }

            if (best_dir >= 8u && cell->coast_distance > 0) {
                int best_coast = cell->coast_distance;
                for (i = 0; i < 8; ++i) {
                    int nx = x + g_dir8_dx[i];
                    int nz = z + g_dir8_dz[i];
                    int nidx;
                    if (!in_bounds(nx, nz)) continue;
                    nidx = idx_of(nx, nz);
                    if (tile->cells[nidx].coast_distance < best_coast) {
                        best_coast = tile->cells[nidx].coast_distance;
                        best_dir = i;
                    }
                }
            }

            if (best_dir < 8u) {
                int nx = x + g_dir8_dx[best_dir];
                int nz = z + g_dir8_dz[best_dir];
                if (in_bounds(nx, nz)) {
                    best_drop = (int)cell->surface_height - (int)tile->cells[idx_of(nx, nz)].surface_height;
                    if (best_drop < 0) best_drop = 0;
                }
            }

            cell->downstream_dir = (uint8_t)best_dir;
            cell->slope = (int16_t)best_drop;
        }
    }

    for (z = 0; z < SDK_WORLDGEN_TILE_STRIDE; ++z) {
        for (x = 0; x < SDK_WORLDGEN_TILE_STRIDE; ++x) {
            SdkWorldGenMacroCell* cell = &tile->cells[idx_of(x, z)];
            int land = is_land(cell, impl->sea_level);
            float lake_strength = sdk_worldgen_unpack_unorm8(cell->lake_mask);
            float closed_basin_strength = sdk_worldgen_unpack_unorm8(cell->closed_basin_mask);

            if (!land) {
                cell->water_height = impl->sea_level;
                cell->river_bed_height = cell->surface_height;
            } else if (lake_strength > 0.26f &&
                       closed_basin_strength > 0.12f &&
                       cell->lake_level > cell->surface_height + 1) {
                cell->river_bed_height = (int16_t)sdk_worldgen_clampi((int)cell->lake_level - 2, 4, CHUNK_HEIGHT - 8);
                cell->water_height = (int16_t)sdk_worldgen_clampi((int)cell->lake_level, 0, CHUNK_HEIGHT - 1);
                if (cell->floodplain_width < 2u) cell->floodplain_width = 2u;
            } else if (cell->river_order > 0) {
                int depth = 1 + (int)cell->river_order;
                if (cell->river_order >= 3) depth += 1;
                cell->river_bed_height = (int16_t)sdk_worldgen_clampi((int)cell->surface_height - depth, 4, CHUNK_HEIGHT - 8);
                cell->water_height = (int16_t)(cell->river_bed_height + 1 + (cell->river_order >= 3 ? 1 : 0));
            } else {
                cell->river_bed_height = cell->surface_height;
                cell->water_height = 0;
            }
        }
    }

    derive_province_outputs(wg, tile);
}
