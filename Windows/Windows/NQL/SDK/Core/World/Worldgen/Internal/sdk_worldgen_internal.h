/**
 * sdk_worldgen_internal.h -- Internal worldgen cache structures.
 *
 * The current terrain pipeline is layered:
 * - continental tiles provide macro climate, hydrology, basin, and coast state
 * - macro tiles provide terrain province and soil-scale classifications
 * - region tiles provide denser structural, landform, and geology fields
 * - column and chunk generation consume those layers to fill blocks
 *
 * Cache ownership lives on `SdkWorldGenImpl`. Each worldgen context owns its
 * own persistent continental scratch buffers plus continental, macro, and
 * region caches.
 *
 * `SdkTerrainColumnProfile` remains the stable gameplay/render/map output
 * surface even though the internal pipeline carries richer state.
 */
#ifndef NQLSDK_WORLDGEN_INTERNAL_H
#define NQLSDK_WORLDGEN_INTERNAL_H

#include "../Types/sdk_worldgen_types.h"
#include "../sdk_worldgen.h"
#include "../../Chunks/sdk_chunk.h"
#include "../../Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../../Blocks/sdk_block.h"
#include "../../Settlements/Types/sdk_settlement_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_WORLDGEN_TILE_STRIDE (SDK_WORLDGEN_MACRO_TILE_SIZE + SDK_WORLDGEN_MACRO_TILE_HALO * 2)
#define SDK_WORLDGEN_TILE_CELL_COUNT (SDK_WORLDGEN_TILE_STRIDE * SDK_WORLDGEN_TILE_STRIDE)
#define SDK_WORLDGEN_CONTINENT_CELL_BLOCKS 256
#define SDK_WORLDGEN_CONTINENT_TILE_SIZE 64
#define SDK_WORLDGEN_CONTINENT_TILE_HALO 8
#define SDK_WORLDGEN_CONTINENT_STRIDE (SDK_WORLDGEN_CONTINENT_TILE_SIZE + SDK_WORLDGEN_CONTINENT_TILE_HALO * 2)
#define SDK_WORLDGEN_CONTINENT_CELL_COUNT (SDK_WORLDGEN_CONTINENT_STRIDE * SDK_WORLDGEN_CONTINENT_STRIDE)
#define SDK_WORLDGEN_CONTINENT_ANALYSIS_MARGIN 24
#define SDK_WORLDGEN_CONTINENT_ANALYSIS_HALO (SDK_WORLDGEN_CONTINENT_TILE_HALO + SDK_WORLDGEN_CONTINENT_ANALYSIS_MARGIN)
#define SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE (SDK_WORLDGEN_CONTINENT_TILE_SIZE + SDK_WORLDGEN_CONTINENT_ANALYSIS_HALO * 2)
#define SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT (SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE * SDK_WORLDGEN_CONTINENT_ANALYSIS_STRIDE)
#define SDK_WORLDGEN_CONTINENT_CACHE_SLOTS 64
#define SDK_WORLDGEN_REGION_TILE_BLOCKS 512
#define SDK_WORLDGEN_REGION_TILE_HALO_BLOCKS 64
#define SDK_WORLDGEN_REGION_SAMPLE_SPACING 8
#define SDK_WORLDGEN_REGION_STRIDE (((SDK_WORLDGEN_REGION_TILE_BLOCKS + SDK_WORLDGEN_REGION_TILE_HALO_BLOCKS * 2) / SDK_WORLDGEN_REGION_SAMPLE_SPACING) + 1)
#define SDK_WORLDGEN_REGION_SAMPLE_COUNT (SDK_WORLDGEN_REGION_STRIDE * SDK_WORLDGEN_REGION_STRIDE)
#define SDK_WORLDGEN_REGION_CACHE_SLOTS 32
#define SDK_SETTLEMENT_CACHE_SLOTS 8

typedef enum {
    SDK_WORLDGEN_PROVINCE_FAMILY_MARINE_COAST = 0,
    SDK_WORLDGEN_PROVINCE_FAMILY_WET_LOWLAND,
    SDK_WORLDGEN_PROVINCE_FAMILY_BASIN_UPLAND,
    SDK_WORLDGEN_PROVINCE_FAMILY_HARDROCK,
    SDK_WORLDGEN_PROVINCE_FAMILY_VOLCANIC
} SdkWorldGenProvinceFamily;

typedef enum {
    SDK_WATER_THERMAL_FRIGID = 0,
    SDK_WATER_THERMAL_COLD,
    SDK_WATER_THERMAL_COOL,
    SDK_WATER_THERMAL_TEMPERATE,
    SDK_WATER_THERMAL_WARM
} SdkWaterThermalBand;

typedef enum {
    SDK_STRAT_PROVINCE_OCEANIC = 0,
    SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN,
    SDK_STRAT_PROVINCE_CARBONATE_SHELF,
    SDK_STRAT_PROVINCE_HARDROCK_BASEMENT,
    SDK_STRAT_PROVINCE_RIFT_BASIN,
    SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX,
    SDK_STRAT_PROVINCE_FLOOD_BASALT
} SdkStratigraphyProvince;

typedef enum {
    SDK_RESOURCE_BODY_NONE = 0,
    SDK_RESOURCE_BODY_COAL,
    SDK_RESOURCE_BODY_CLAY,
    SDK_RESOURCE_BODY_IRONSTONE,
    SDK_RESOURCE_BODY_COPPER,
    SDK_RESOURCE_BODY_SULFUR,
    SDK_RESOURCE_BODY_LEAD_ZINC,
    SDK_RESOURCE_BODY_TUNGSTEN,
    SDK_RESOURCE_BODY_BAUXITE,
    SDK_RESOURCE_BODY_SALT,
    SDK_RESOURCE_BODY_OIL,
    SDK_RESOURCE_BODY_GAS,
    SDK_RESOURCE_BODY_RARE_EARTH,
    SDK_RESOURCE_BODY_URANIUM,
    SDK_RESOURCE_BODY_SALTPETER,
    SDK_RESOURCE_BODY_PHOSPHATE,
    SDK_RESOURCE_BODY_CHROMIUM,
    SDK_RESOURCE_BODY_ALUMINUM
} SdkResourceBodyKind;

typedef struct {
    int16_t  base_height;
    int16_t  surface_height;
    int16_t  water_height;
    int16_t  river_bed_height;
    int16_t  lake_level;
    int16_t  coast_distance;
    uint32_t flow_accum;
    uint32_t basin_id;
    uint8_t  soil_depth;
    uint8_t  sediment_depth;
    uint8_t  river_order;
    uint8_t  floodplain_width;
    uint8_t  terrain_province;
    uint8_t  bedrock_province;
    uint8_t  temperature_band;
    uint8_t  moisture_band;
    uint8_t  surface_sediment;
    uint8_t  parent_material;
    uint8_t  drainage_class;
    uint8_t  soil_reaction;
    uint8_t  soil_fertility;
    uint8_t  soil_salinity;
    uint8_t  water_table_depth;
    uint8_t  ecology;
    uint8_t  resource_province;
    uint8_t  hydrocarbon_class;
    uint8_t  resource_grade;
    uint8_t  downstream_dir;
    uint8_t  plate_class;
    uint8_t  relief_strength;
    uint8_t  detail_amp;
    uint8_t  mountain_mask;
    uint8_t  river_strength;
    uint8_t  wetness;
    uint8_t  water_temp;
    uint8_t  water_surface_class;
    uint8_t  stratigraphy_province;
    uint8_t  province_family;
    uint8_t  lake_mask;
    uint8_t  closed_basin_mask;
    uint8_t  ravine_mask;
    uint8_t  vent_mask;
    uint8_t  caldera_mask;
    uint16_t lake_id;
    uint32_t landform_flags;
    int8_t   boundary_class;
    int16_t  slope;
} SdkWorldGenMacroCell;

typedef struct {
    int16_t  raw_height;
    int16_t  filled_height;
    int16_t  lake_level;
    int16_t  coast_distance;
    int32_t  downstream_cx;
    int32_t  downstream_cz;
    uint32_t flow_accum;
    uint32_t basin_id;
    uint16_t lake_id;
    uint8_t  land_mask;
    uint8_t  ocean_mask;
    uint8_t  lake_mask;
    uint8_t  closed_basin_mask;
    uint8_t  precipitation;
    uint8_t  runoff;
    uint8_t  trunk_river_order;
    uint8_t  water_access;
    uint8_t  harbor_score;
    uint8_t  confluence_score;
    uint8_t  flood_risk;
    uint8_t  buildable_flatness;
} SdkContinentalCell;

typedef struct {
    float    raw_height;
    float    filled_height;
    float    lake_level;
    float    coast_distance;
    float    flow_accum;
    float    precipitation;
    float    runoff;
    float    trunk_river_order;
    float    water_access;
    float    harbor_score;
    float    confluence_score;
    float    flood_risk;
    float    buildable_flatness;
    uint32_t basin_id;
    uint16_t lake_id;
    uint8_t  land_mask;
    uint8_t  ocean_mask;
    uint8_t  lake_mask;
    uint8_t  closed_basin_mask;
    int32_t  downstream_cx;
    int32_t  downstream_cz;
} SdkContinentalSample;

typedef struct {
    float    base_height;
    float    surface_base;
    float    surface_relief;
    float    water_level;
    float    river_bed_height;
    float    coast_signed_distance;
    float    ocean_depth;
    float    river_strength;
    float    floodplain_mask;
    float    river_order;
    float    floodplain_width;
    float    river_channel_width;
    float    river_channel_depth;
    float    valley_mask;
    float    braid_mask;
    float    waterfall_mask;
    float    flow_dir_x;
    float    flow_dir_z;
    float    wetness;
    float    air_temp;
    float    water_temp;
    float    soil_depth;
    float    sediment_thickness;
    float    regolith_thickness;
    float    drainage_index;
    float    soil_reaction_value;
    float    soil_fertility_value;
    float    soil_salinity_value;
    float    soil_organic_value;
    float    water_table_depth;
    float    stratigraphy_control;
    float    weathered_base;
    float    upper_top;
    float    lower_top;
    float    basement_top;
    float    deep_basement_top;
    float    dip_x;
    float    dip_z;
    float    fault_mask;
    float    fault_throw;
    float    basin_axis_weight;
    float    channel_sand_bias;
    float    trap_strength;
    float    seal_quality;
    float    gas_bias;
    float    source_richness;
    float    carbonate_purity;
    float    vent_bias;
    float    evaporite_bias;
    float    lake_mask;
    float    lake_level;
    float    closed_basin_mask;
    float    ravine_mask;
    float    ravine_depth;
    float    ravine_width;
    float    vent_mask;
    float    vent_distance;
    float    caldera_mask;
    float    lava_flow_bias;
    float    ash_bias;
    float    karst_mask;
    float    fracture_cave_mask;
    float    lava_tube_mask;
    float    cave_depth_bias;
    float    cave_entrance_mask;
    float    stratovolcano_mask;
    float    shield_mask;
    float    fissure_mask;
    float    ashfall_mask;
    uint8_t  terrain_province;
    uint8_t  bedrock_province;
    uint8_t  temperature_band;
    uint8_t  moisture_band;
    uint8_t  surface_sediment;
    uint8_t  parent_material;
    uint8_t  drainage_class;
    uint8_t  soil_reaction;
    uint8_t  soil_fertility;
    uint8_t  soil_salinity;
    uint8_t  ecology;
    uint8_t  resource_province;
    uint8_t  hydrocarbon_class;
    uint8_t  resource_grade;
    uint8_t  water_surface_class;
    uint8_t  stratigraphy_province;
    uint32_t landform_flags;
} SdkRegionFieldSample;

typedef struct {
    bool                 valid;
    int32_t              tile_x;
    int32_t              tile_z;
    uint32_t             stamp;
    SdkRegionFieldSample samples[SDK_WORLDGEN_REGION_SAMPLE_COUNT];
} SdkWorldGenRegionTile;

typedef struct {
    int16_t*       raw_height;
    int16_t*       filled_height;
    int16_t*       coast_distance;
    uint8_t*       ocean_mask;
    uint8_t*       precipitation;
    uint8_t*       runoff;
    uint8_t*       flatness;
    int*           downstream;
    uint32_t*      flow_accum;
    uint32_t*      basin_id;
    uint8_t*       trunk_order;
    uint8_t*       lake_mask;
    uint8_t*       closed_mask;
    int16_t*       lake_level;
    uint16_t*      lake_id;
    uint8_t*       water_access;
    uint8_t*       harbor_score;
    uint8_t*       confluence;
    uint8_t*       flood_risk;
    int*           queue;
    uint8_t*       visited;
    void*          heap;
    void*          sort_entries;
    int*           stack;
} SdkWorldGenContinentScratch;

typedef struct {
    bool                valid;
    int32_t             tile_x;
    int32_t             tile_z;
    uint32_t            stamp;
    SdkContinentalCell  cells[SDK_WORLDGEN_CONTINENT_CELL_COUNT];
} SdkWorldGenContinentalTile;

typedef struct {
    BlockType basement_block;
    BlockType deep_basement_block;
    BlockType lower_block;
    BlockType upper_block;
    BlockType regolith_block;
    BlockType fault_block;
    BlockType vein_block;
    int16_t   deep_basement_top;
    int16_t   basement_top;
    int16_t   lower_top;
    int16_t   upper_top;
    int16_t   weathered_base;
} SdkStrataColumn;

typedef struct {
    bool                  valid;
    int32_t               tile_x;
    int32_t               tile_z;
    uint32_t              stamp;
    SdkWorldGenMacroCell  cells[SDK_WORLDGEN_TILE_CELL_COUNT];
} SdkWorldGenMacroTile;

typedef struct {
    uint32_t seed;
    int16_t  sea_level;
    uint16_t macro_cell_size;
    char world_path[512];
    void* tile_cache;
    SdkWorldGenContinentScratch* continent_scratch;
    uint32_t stamp_clock;
    bool settlements_enabled;
    bool construction_cells_enabled;

    SuperchunkSettlementData* settlement_cache[SDK_SETTLEMENT_CACHE_SLOTS];
    int32_t settlement_cache_scx[SDK_SETTLEMENT_CACHE_SLOTS];
    int32_t settlement_cache_scz[SDK_SETTLEMENT_CACHE_SLOTS];
    uint32_t settlement_cache_stamps[SDK_SETTLEMENT_CACHE_SLOTS];
} SdkWorldGenImpl;

typedef struct SdkWorldGenChunkDebugCapture SdkWorldGenChunkDebugCapture;

static inline uint8_t sdk_worldgen_classify_temperature(float temp)
{
    if (temp < 0.12f) return TEMP_POLAR;
    if (temp < 0.26f) return TEMP_SUBPOLAR;
    if (temp < 0.45f) return TEMP_COOL_TEMPERATE;
    if (temp < 0.63f) return TEMP_WARM_TEMPERATE;
    if (temp < 0.82f) return TEMP_SUBTROPICAL;
    return TEMP_TROPICAL;
}

static inline uint8_t sdk_worldgen_classify_moisture(float moisture)
{
    if (moisture < 0.12f) return MOISTURE_ARID;
    if (moisture < 0.28f) return MOISTURE_SEMI_ARID;
    if (moisture < 0.48f) return MOISTURE_SUBHUMID;
    if (moisture < 0.66f) return MOISTURE_HUMID;
    if (moisture < 0.82f) return MOISTURE_PERHUMID;
    return MOISTURE_WATERLOGGED;
}

static inline int sdk_worldgen_ecology_is_wetland(uint8_t ecology)
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

static inline int sdk_worldgen_ecology_prefers_turf(uint8_t ecology)
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

static inline int sdk_worldgen_ecology_supports_passive_fauna(uint8_t ecology)
{
    switch ((SdkBiomeEcology)ecology) {
        case ECOLOGY_PRAIRIE:
        case ECOLOGY_FLOODPLAIN_MEADOW:
        case ECOLOGY_STEPPE:
        case ECOLOGY_TEMPERATE_DECIDUOUS_FOREST:
        case ECOLOGY_TEMPERATE_CONIFER_FOREST:
        case ECOLOGY_BOREAL_TAIGA:
        case ECOLOGY_RIPARIAN_FOREST:
        case ECOLOGY_MEDITERRANEAN_SCRUB:
        case ECOLOGY_TUNDRA:
            return 1;
        default:
            return 0;
    }
}

uint32_t sdk_worldgen_hash32(uint32_t x);
uint32_t sdk_worldgen_hash2d(int x, int z, uint32_t seed);
float sdk_worldgen_hashf(uint32_t h);
float sdk_worldgen_value_noise(float x, float z, uint32_t seed);
float sdk_worldgen_fbm(float x, float z, uint32_t seed, int octaves);
float sdk_worldgen_ridged(float x, float z, uint32_t seed, int octaves);
float sdk_worldgen_clampf(float v, float lo, float hi);
int sdk_worldgen_clampi(int v, int lo, int hi);

static inline uint8_t sdk_worldgen_pack_unorm8(float v)
{
    float clamped = sdk_worldgen_clampf(v, 0.0f, 1.0f);
    return (uint8_t)(clamped * 255.0f + 0.5f);
}

static inline float sdk_worldgen_unpack_unorm8(uint8_t v)
{
    return (float)v / 255.0f;
}

SdkWorldGenMacroTile* sdk_worldgen_require_macro_tile(SdkWorldGen* wg, int wx, int wz);
SdkWorldGenRegionTile* sdk_worldgen_require_region_tile(SdkWorldGen* wg, int wx, int wz);
SdkWorldGenContinentalTile* sdk_worldgen_require_continental_tile(SdkWorldGen* wg, int wx, int wz);
void sdk_worldgen_build_macro_tile(SdkWorldGen* wg, SdkWorldGenMacroTile* tile);
void sdk_worldgen_build_region_tile(SdkWorldGen* wg, SdkWorldGenRegionTile* tile);
void sdk_worldgen_build_continental_tile(SdkWorldGen* wg, SdkWorldGenContinentalTile* tile);
void sdk_worldgen_run_hydrology(SdkWorldGen* wg, SdkWorldGenMacroTile* tile);
const SdkContinentalCell* sdk_worldgen_get_continental_cell(SdkWorldGen* wg, int cell_x, int cell_z);
void sdk_worldgen_sample_continental_state(SdkWorldGen* wg, int wx, int wz, SdkContinentalSample* out_sample);
void sdk_worldgen_sample_column_from_tile(SdkWorldGenMacroTile* tile, SdkWorldGen* wg,
                                          int wx, int wz, SdkTerrainColumnProfile* out_profile);
void sdk_worldgen_sample_column_from_region_tile(SdkWorldGenRegionTile* tile, SdkWorldGen* wg,
                                                 int wx, int wz, SdkTerrainColumnProfile* out_profile);
void sdk_worldgen_sample_region_fields(SdkWorldGenRegionTile* tile, SdkWorldGen* wg,
                                       int wx, int wz, SdkRegionFieldSample* out_sample);
uint32_t sdk_worldgen_debug_color_ctx_impl(SdkWorldGen* wg, int wx, int wy, int wz, BlockType actual_block);
int sdk_worldgen_profile_is_passive_spawn_habitat(const SdkTerrainColumnProfile* profile, int relief);
void sdk_worldgen_fill_chunk(SdkWorldGen* wg, SdkChunk* chunk);
SdkWorldGenChunkDebugCapture* sdk_worldgen_debug_capture_get_thread_current(void);
void sdk_worldgen_debug_capture_begin(SdkWorldGenChunkDebugCapture* capture);
void sdk_worldgen_debug_capture_end(void);
void sdk_worldgen_debug_capture_note_tree(int lx, int lz, uint8_t archetype, int trunk_height);
void sdk_worldgen_debug_capture_note_plant(int lx, int lz, BlockType plant_block);
void sdk_worldgen_debug_capture_note_water_seal(int lx, int lz, int waterline, BlockType cap_block, int banked);
void sdk_worldgen_debug_capture_note_wall_column(int lx, int lz,
                                                 uint8_t wall_mask,
                                                 uint8_t gate_mask,
                                                 int wall_top_y,
                                                 int gate_floor_y);
void sdk_worldgen_debug_capture_note_settlement_stage(void);
void sdk_worldgen_debug_capture_note_route(int route_surface,
                                           int start_kind,
                                           int end_kind,
                                           int start_y,
                                           int end_y,
                                           int max_cut,
                                           int max_fill,
                                           int carved_columns,
                                           int candidate_index);
void sdk_worldgen_debug_capture_note_custom(const char* message);
void sdk_worldgen_emit_chunk_debug_report(SdkWorldGen* wg, SdkChunkManager* cm, int cx, int cz);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_WORLDGEN_INTERNAL_H */
