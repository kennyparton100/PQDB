/**
 * sdk_worldgen_debug_report.c -- One-shot chunk worldgen report for F9.
 */
#include "../Internal/sdk_worldgen_internal.h"
#include "../Column/sdk_worldgen_column_internal.h"
#include "../../Superchunks/Geometry/sdk_superchunk_geometry.h"
#include "../../Settlements/sdk_settlement.h"
#include "../../Blocks/sdk_block.h"
#include "../../../API/Internal/sdk_load_trace.h"

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WG_CHUNK_REPORT_DIFF_CAP 64
#define WG_ROUTE_SUMMARY_CAP 64
#define WG_CUSTOM_NOTE_CAP 32
#define WG_CUSTOM_NOTE_LEN 256

enum {
    WG_COL_FLAG_WATER      = 1u << 0,
    WG_COL_FLAG_RIVER      = 1u << 1,
    WG_COL_FLAG_LAKE       = 1u << 2,
    WG_COL_FLAG_COAST      = 1u << 3,
    WG_COL_FLAG_WALL       = 1u << 4,
    WG_COL_FLAG_GATE       = 1u << 5,
    WG_COL_FLAG_SETTLEMENT = 1u << 6,
    WG_COL_FLAG_ROAD       = 1u << 7,
    WG_COL_FLAG_TREE       = 1u << 8,
    WG_COL_FLAG_PLANT      = 1u << 9,
    WG_COL_FLAG_WATER_SEAL = 1u << 10,
    WG_COL_FLAG_BUILDING   = 1u << 11
};

enum {
    WG_SUPER_WALL_WEST  = 1u << 0,
    WG_SUPER_WALL_NORTH = 1u << 1,
    WG_SUPER_WALL_EAST  = 1u << 2,
    WG_SUPER_WALL_SOUTH = 1u << 3
};

struct SdkWorldGenChunkDebugCapture {
    int active;
    int cx;
    int cz;
    uint16_t column_flags[CHUNK_BLOCKS_PER_LAYER];
    uint8_t tree_style[CHUNK_BLOCKS_PER_LAYER];
    uint8_t tree_trunk_height[CHUNK_BLOCKS_PER_LAYER];
    BlockType plant_block[CHUNK_BLOCKS_PER_LAYER];
    int16_t waterline[CHUNK_BLOCKS_PER_LAYER];
    uint8_t water_cap_block[CHUNK_BLOCKS_PER_LAYER];
    uint8_t water_banked[CHUNK_BLOCKS_PER_LAYER];
    uint8_t wall_mask[CHUNK_BLOCKS_PER_LAYER];
    uint8_t gate_mask[CHUNK_BLOCKS_PER_LAYER];
    int16_t wall_top_y[CHUNK_BLOCKS_PER_LAYER];
    int16_t gate_floor_y[CHUNK_BLOCKS_PER_LAYER];
    uint32_t tree_columns;
    uint32_t plant_columns;
    uint32_t water_seal_columns;
    uint32_t banked_columns;
    uint32_t wall_columns;
    uint32_t gate_columns;
    uint32_t settlement_stage_runs;
    uint32_t route_summary_count;
    uint32_t route_summary_dropped;
    uint32_t custom_note_count;
    uint32_t custom_note_dropped;
    struct {
        uint8_t route_surface;
        uint8_t start_kind;
        uint8_t end_kind;
        int16_t start_y;
        int16_t end_y;
        int16_t max_cut;
        int16_t max_fill;
        uint16_t carved_columns;
        uint8_t candidate_index;
    } route_summaries[WG_ROUTE_SUMMARY_CAP];
    char custom_notes[WG_CUSTOM_NOTE_CAP][WG_CUSTOM_NOTE_LEN];
};

typedef struct {
    int line_count;
} WgChunkReportEmitter;

typedef struct {
    int total_changed;
    int added_blocks;
    int removed_blocks;
    int changed_blocks;
    int captured;
    struct {
        int lx;
        int ly;
        int lz;
        BlockType generated_block;
        BlockType live_block;
    } entries[WG_CHUNK_REPORT_DIFF_CAP];
} WgLiveDiffSummary;

static __declspec(thread) SdkWorldGenChunkDebugCapture* g_worldgen_debug_capture_tls = NULL;

static const char* const g_terrain_province_names[] = {
    "OPEN_OCEAN", "CONTINENTAL_SHELF", "DYNAMIC_COAST", "ESTUARY_DELTA",
    "FLOODPLAIN_LOWLAND", "PEAT_WETLAND", "SILICICLASTIC_HILLS", "CARBONATE_UPLAND",
    "HARDROCK_HIGHLAND", "UPLIFTED_PLATEAU", "RIFT_VALLEY", "FOLD_MOUNTAIN_BELT",
    "VOLCANIC_ARC", "BASALT_PLATEAU", "ARID_FAN_STEPPE", "ALPINE_BELT",
    "SALT_FLAT_PLAYA", "BADLANDS_DISSECTED"
};

static const char* const g_terrain_province_codes[] = {
    "OO", "CS", "DC", "ED", "FL", "PW", "SH", "CU", "HH",
    "UP", "RV", "FM", "VA", "BP", "AF", "AB", "SP", "BD"
};

static const char* const g_bedrock_names[] = {
    "OCEANIC_BASALT", "CRATON_GRANITE_GNEISS", "METAMORPHIC_BELT",
    "GRANITIC_INTRUSIVE", "SILICICLASTIC_BASIN", "CARBONATE_PLATFORM",
    "RIFT_SEDIMENTARY", "VOLCANIC_ARC", "FLOOD_BASALT"
};

static const char* const g_temperature_names[] = {
    "POLAR", "SUBPOLAR", "COOL_TEMPERATE", "WARM_TEMPERATE", "SUBTROPICAL", "TROPICAL"
};

static const char* const g_moisture_names[] = {
    "ARID", "SEMI_ARID", "SUBHUMID", "HUMID", "PERHUMID", "WATERLOGGED"
};

static const char* const g_sediment_names[] = {
    "NONE", "RESIDUAL_SOIL", "COLLUVIUM", "TALUS", "COARSE_ALLUVIUM",
    "FINE_ALLUVIUM", "DELTAIC_SILT", "LACUSTRINE_CLAY", "BEACH_SAND",
    "EOLIAN_SAND", "PEAT", "VOLCANIC_ASH", "MARINE_MUD", "MARINE_SAND",
    "LOESS", "CALCAREOUS_RESIDUAL", "SAPROLITE"
};

static const char* const g_parent_material_names[] = {
    "NONE", "GRANITIC", "METAMORPHIC", "MAFIC_VOLCANIC", "INTERMEDIATE_VOLCANIC",
    "SILICICLASTIC", "CARBONATE", "ORGANIC", "ALLUVIAL", "AEOLIAN", "EVAPORITIC"
};

static const char* const g_drainage_names[] = {
    "EXCESSIVE", "WELL", "MODERATE", "IMPERFECT", "POOR", "WATERLOGGED"
};

static const char* const g_soil_reaction_names[] = {
    "STRONGLY_ACID", "ACID", "SLIGHTLY_ACID", "NEUTRAL", "CALCAREOUS", "SALINE_ALKALINE"
};

static const char* const g_soil_fertility_names[] = {
    "VERY_LOW", "LOW", "MODERATE", "HIGH", "VERY_HIGH"
};

static const char* const g_soil_salinity_names[] = {
    "NONE", "SLIGHT", "MODERATE", "HIGH"
};

static const char* const g_ecology_names[] = {
    "BARREN", "DUNE_COAST", "ESTUARY_WETLAND", "RIPARIAN_FOREST",
    "FLOODPLAIN_MEADOW", "FEN", "BOG", "PRAIRIE", "STEPPE", "MEDITERRANEAN_SCRUB",
    "TEMPERATE_DECIDUOUS_FOREST", "TEMPERATE_CONIFER_FOREST", "BOREAL_TAIGA",
    "TROPICAL_SEASONAL_FOREST", "TROPICAL_RAINFOREST", "HOT_DESERT", "TUNDRA",
    "ALPINE_MEADOW", "VOLCANIC_BARRENS", "NIVAL_ICE", "SALT_DESERT",
    "SCRUB_BADLANDS", "SAVANNA_GRASSLAND", "MANGROVE_SWAMP"
};

static const char* const g_resource_names[] = {
    "NONE", "AGGREGATE_DISTRICT", "CLAY_DISTRICT", "CARBONATE_CEMENT_DISTRICT",
    "COALFIELD", "IRON_BELT", "VOLCANIC_METALS", "OIL_FIELD", "COPPER_PORPHYRY_BELT",
    "BAUXITE_DEPOSIT", "RARE_EARTH_DISTRICT", "URANIUM_GRANITE_BELT",
    "SULFUR_VOLCANIC_DISTRICT", "SALTPETER_NITRATE_FIELD", "PHOSPHATE_DEPOSIT",
    "STRATEGIC_ALLOY_BELT", "LEAD_ZINC_DISTRICT"
};

static const char* const g_hydrocarbon_names[] = {
    "NONE", "OIL_SHALE", "OIL_SAND", "CRUDE_OIL", "NATURAL_GAS"
};

static const char* const g_surface_water_names[] = {
    "NONE", "OPEN", "SEASONAL_ICE", "PERENNIAL_ICE"
};

static const char* const g_settlement_type_names[] = {
    "NONE", "VILLAGE", "TOWN", "CITY"
};

static const char* const g_settlement_purpose_names[] = {
    "FARMING", "FISHING", "MINING", "LOGISTICS", "PROCESSING",
    "PORT", "CAPITAL", "FORTRESS", "HYDROCARBON", "CEMENT", "TIMBER"
};

static const char* const g_settlement_state_names[] = {
    "PRISTINE", "DAMAGED", "RUINS", "ABANDONED", "REBUILDING"
};

static const char* const g_building_type_names[] = {
    "NONE", "HUT", "HOUSE", "MANOR", "FARM", "BARN", "WORKSHOP", "FORGE",
    "MILL", "STOREHOUSE", "WAREHOUSE", "SILO", "WATCHTOWER", "BARRACKS",
    "WALL_SECTION", "WELL", "MARKET", "DOCK"
};

static const char* const g_zone_type_names[] = {
    "RESIDENTIAL", "COMMERCIAL", "INDUSTRIAL", "AGRICULTURAL",
    "DEFENSIVE", "CIVIC", "HARBOR"
};

static const char* const g_tree_style_names[] = {
    "NONE", "ROUND", "CONIFER", "TROPICAL", "SHRUB"
};

static const char* const g_route_surface_names[] = {
    "PATH", "ROAD", "CITY_ROAD"
};

static const char* const g_route_endpoint_names[] = {
    "UNKNOWN", "HUB", "BUILDING", "GATE", "PEER_TOWN"
};

static int wg_column_index(int lx, int lz)
{
    return lz * CHUNK_WIDTH + lx;
}

static const char* table_name(const char* const* table, int count, int index)
{
    if (!table || count <= 0 || index < 0 || index >= count) return "?";
    return table[index];
}

static void append_token(char* buffer, size_t buffer_size, const char* token)
{
    size_t used;

    if (!buffer || buffer_size == 0 || !token || token[0] == '\0') return;
    used = strlen(buffer);
    if (used >= buffer_size - 1) return;
    if (used > 0) {
        _snprintf(buffer + used, buffer_size - used, "|%s", token);
    } else {
        _snprintf(buffer + used, buffer_size - used, "%s", token);
    }
}

static void emit_line(WgChunkReportEmitter* emitter, const char* fmt, ...)
{
    char user_line[1900];
    char final_line[1984];
    va_list args;

    if (!emitter || !fmt) return;

    va_start(args, fmt);
    _vsnprintf(user_line, sizeof(user_line) - 1, fmt, args);
    va_end(args);
    user_line[sizeof(user_line) - 1] = '\0';

    _snprintf(final_line, sizeof(final_line) - 1, "[WG-CHUNK] %s\n", user_line);
    final_line[sizeof(final_line) - 1] = '\0';
    sdk_debug_log_output(final_line);
    emitter->line_count++;
}

static int is_waterlike_block(BlockType block)
{
    return block == BLOCK_WATER || block == BLOCK_ICE || block == BLOCK_SEA_ICE;
}

static uint8_t surface_kind_for_top_block(BlockType block)
{
    switch (block) {
        case BLOCK_WATER:   return SDK_WORLDGEN_SURFACE_COLUMN_OPEN_WATER;
        case BLOCK_ICE:     return SDK_WORLDGEN_SURFACE_COLUMN_SEASONAL_ICE;
        case BLOCK_SEA_ICE: return SDK_WORLDGEN_SURFACE_COLUMN_PERENNIAL_ICE;
        case BLOCK_LAVA:    return SDK_WORLDGEN_SURFACE_COLUMN_LAVA;
        case BLOCK_AIR:     return SDK_WORLDGEN_SURFACE_COLUMN_VOID;
        default:            return SDK_WORLDGEN_SURFACE_COLUMN_DRY;
    }
}

static int is_road_block(BlockType block)
{
    return block == BLOCK_COMPACTED_FILL ||
           block == BLOCK_CRUSHED_STONE ||
           block == BLOCK_COBBLESTONE ||
           block == BLOCK_GRAVEL;
}

static const char* terrain_province_name(int value)
{
    return table_name(g_terrain_province_names,
                      (int)(sizeof(g_terrain_province_names) / sizeof(g_terrain_province_names[0])),
                      value);
}

static const char* terrain_province_code(int value)
{
    return table_name(g_terrain_province_codes,
                      (int)(sizeof(g_terrain_province_codes) / sizeof(g_terrain_province_codes[0])),
                      value);
}

static const char* bedrock_name(int value)
{
    return table_name(g_bedrock_names, (int)(sizeof(g_bedrock_names) / sizeof(g_bedrock_names[0])), value);
}

static const char* temperature_name(int value)
{
    return table_name(g_temperature_names, (int)(sizeof(g_temperature_names) / sizeof(g_temperature_names[0])), value);
}

static const char* moisture_name(int value)
{
    return table_name(g_moisture_names, (int)(sizeof(g_moisture_names) / sizeof(g_moisture_names[0])), value);
}

static const char* sediment_name(int value)
{
    return table_name(g_sediment_names, (int)(sizeof(g_sediment_names) / sizeof(g_sediment_names[0])), value);
}

static const char* parent_material_name(int value)
{
    return table_name(g_parent_material_names,
                      (int)(sizeof(g_parent_material_names) / sizeof(g_parent_material_names[0])),
                      value);
}

static const char* drainage_name(int value)
{
    return table_name(g_drainage_names, (int)(sizeof(g_drainage_names) / sizeof(g_drainage_names[0])), value);
}

static const char* soil_reaction_name(int value)
{
    return table_name(g_soil_reaction_names,
                      (int)(sizeof(g_soil_reaction_names) / sizeof(g_soil_reaction_names[0])),
                      value);
}

static const char* soil_fertility_name(int value)
{
    return table_name(g_soil_fertility_names,
                      (int)(sizeof(g_soil_fertility_names) / sizeof(g_soil_fertility_names[0])),
                      value);
}

static const char* soil_salinity_name(int value)
{
    return table_name(g_soil_salinity_names,
                      (int)(sizeof(g_soil_salinity_names) / sizeof(g_soil_salinity_names[0])),
                      value);
}

static const char* ecology_name(int value)
{
    return table_name(g_ecology_names, (int)(sizeof(g_ecology_names) / sizeof(g_ecology_names[0])), value);
}

static const char* resource_name(int value)
{
    return table_name(g_resource_names, (int)(sizeof(g_resource_names) / sizeof(g_resource_names[0])), value);
}

static const char* hydrocarbon_name(int value)
{
    return table_name(g_hydrocarbon_names, (int)(sizeof(g_hydrocarbon_names) / sizeof(g_hydrocarbon_names[0])), value);
}

static const char* surface_water_name(int value)
{
    return table_name(g_surface_water_names,
                      (int)(sizeof(g_surface_water_names) / sizeof(g_surface_water_names[0])),
                      value);
}

static const char* settlement_type_name_local(int value)
{
    return table_name(g_settlement_type_names,
                      (int)(sizeof(g_settlement_type_names) / sizeof(g_settlement_type_names[0])),
                      value);
}

static const char* settlement_purpose_name_local(int value)
{
    return table_name(g_settlement_purpose_names,
                      (int)(sizeof(g_settlement_purpose_names) / sizeof(g_settlement_purpose_names[0])),
                      value);
}

static const char* settlement_state_name_local(int value)
{
    return table_name(g_settlement_state_names,
                      (int)(sizeof(g_settlement_state_names) / sizeof(g_settlement_state_names[0])),
                      value);
}

static const char* building_type_name_local(int value)
{
    return table_name(g_building_type_names,
                      (int)(sizeof(g_building_type_names) / sizeof(g_building_type_names[0])),
                      value);
}

static const char* zone_type_name_local(int value)
{
    return table_name(g_zone_type_names,
                      (int)(sizeof(g_zone_type_names) / sizeof(g_zone_type_names[0])),
                      value);
}

static const char* tree_style_name_local(int value)
{
    return table_name(g_tree_style_names, (int)(sizeof(g_tree_style_names) / sizeof(g_tree_style_names[0])), value);
}

static const char* route_surface_name(int value)
{
    return table_name(g_route_surface_names,
                      (int)(sizeof(g_route_surface_names) / sizeof(g_route_surface_names[0])),
                      value);
}

static const char* route_endpoint_name(int value)
{
    return table_name(g_route_endpoint_names,
                      (int)(sizeof(g_route_endpoint_names) / sizeof(g_route_endpoint_names[0])),
                      value);
}

static const char* chunk_role_name(int role)
{
    switch ((SdkChunkResidencyRole)role) {
        case SDK_CHUNK_ROLE_PRIMARY:            return "PRIMARY";
        case SDK_CHUNK_ROLE_WALL_SUPPORT:       return "WALL_SUPPORT";
        case SDK_CHUNK_ROLE_FRONTIER:           return "FRONTIER";
        case SDK_CHUNK_ROLE_TRANSITION_PRELOAD: return "TRANSITION_PRELOAD";
        case SDK_CHUNK_ROLE_EVICT_PENDING:      return "EVICT_PENDING";
        case SDK_CHUNK_ROLE_NONE:
        default:                                return "NONE";
    }
}

static const char* render_representation_name(int value)
{
    switch ((SdkChunkRenderRepresentation)value) {
        case SDK_CHUNK_RENDER_REPRESENTATION_FULL:  return "FULL";
        case SDK_CHUNK_RENDER_REPRESENTATION_FAR:   return "FAR";
        case SDK_CHUNK_RENDER_REPRESENTATION_PROXY: return "PROXY";
        default:                                    return "UNKNOWN";
    }
}

static const char* surface_kind_name(uint8_t kind)
{
    switch ((SdkWorldGenSurfaceColumnKind)kind) {
        case SDK_WORLDGEN_SURFACE_COLUMN_VOID:          return "VOID";
        case SDK_WORLDGEN_SURFACE_COLUMN_DRY:           return "DRY";
        case SDK_WORLDGEN_SURFACE_COLUMN_OPEN_WATER:    return "OPEN_WATER";
        case SDK_WORLDGEN_SURFACE_COLUMN_SEASONAL_ICE:  return "SEASONAL_ICE";
        case SDK_WORLDGEN_SURFACE_COLUMN_PERENNIAL_ICE: return "PERENNIAL_ICE";
        case SDK_WORLDGEN_SURFACE_COLUMN_LAVA:          return "LAVA";
        default:                                        return "UNKNOWN";
    }
}

static void scan_generated_surface_columns(const SdkChunk* chunk, SdkWorldGenSurfaceColumn* out_columns)
{
    int lx;
    int lz;

    if (!chunk || !out_columns) return;
    memset(out_columns, 0, sizeof(*out_columns) * CHUNK_BLOCKS_PER_LAYER);

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int idx = wg_column_index(lx, lz);
            SdkWorldGenSurfaceColumn* column = &out_columns[idx];
            int y;

            column->kind = SDK_WORLDGEN_SURFACE_COLUMN_VOID;
            column->top_block = BLOCK_AIR;
            column->land_block = BLOCK_AIR;

            for (y = CHUNK_HEIGHT - 1; y >= 0; --y) {
                BlockType block = sdk_chunk_get_block(chunk, lx, y, lz);

                if (block == BLOCK_AIR) continue;
                if (column->top_block == BLOCK_AIR) {
                    column->top_block = block;
                    column->top_height = (uint16_t)y;
                    column->kind = surface_kind_for_top_block(block);
                }
                if (is_waterlike_block(block)) {
                    if (column->water_height == 0u && column->top_block != BLOCK_AIR) {
                        column->water_height = (uint16_t)y;
                    }
                    continue;
                }

                column->land_block = block;
                column->land_height = (uint16_t)y;
                break;
            }

            if (column->top_block == BLOCK_AIR) continue;
            if (is_waterlike_block(column->top_block)) {
                if (column->water_height < column->top_height) {
                    column->water_height = column->top_height;
                }
                if (column->water_height > column->land_height) {
                    column->water_depth = (uint16_t)(column->water_height - column->land_height);
                }
            } else {
                column->land_block = column->top_block;
                column->land_height = column->top_height;
            }
        }
    }
}

SdkWorldGenChunkDebugCapture* sdk_worldgen_debug_capture_get_thread_current(void)
{
    return g_worldgen_debug_capture_tls;
}

void sdk_worldgen_debug_capture_begin(SdkWorldGenChunkDebugCapture* capture)
{
    if (!capture) return;
    memset(capture, 0, sizeof(*capture));
    capture->active = 1;
    g_worldgen_debug_capture_tls = capture;
}

void sdk_worldgen_debug_capture_end(void)
{
    if (g_worldgen_debug_capture_tls) {
        g_worldgen_debug_capture_tls->active = 0;
    }
    g_worldgen_debug_capture_tls = NULL;
}

void sdk_worldgen_debug_capture_note_tree(int lx, int lz, uint8_t archetype, int trunk_height)
{
    SdkWorldGenChunkDebugCapture* capture = g_worldgen_debug_capture_tls;
    int idx;

    if (!capture || !capture->active) return;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return;
    idx = wg_column_index(lx, lz);
    if ((capture->column_flags[idx] & WG_COL_FLAG_TREE) == 0u) {
        capture->tree_columns++;
    }
    capture->column_flags[idx] |= WG_COL_FLAG_TREE;
    capture->tree_style[idx] = archetype;
    capture->tree_trunk_height[idx] = (uint8_t)((trunk_height < 0) ? 0 : (trunk_height > 255 ? 255 : trunk_height));
}

void sdk_worldgen_debug_capture_note_plant(int lx, int lz, BlockType plant_block)
{
    SdkWorldGenChunkDebugCapture* capture = g_worldgen_debug_capture_tls;
    int idx;

    if (!capture || !capture->active) return;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return;
    idx = wg_column_index(lx, lz);
    if ((capture->column_flags[idx] & WG_COL_FLAG_PLANT) == 0u) {
        capture->plant_columns++;
    }
    capture->column_flags[idx] |= WG_COL_FLAG_PLANT;
    capture->plant_block[idx] = plant_block;
}

void sdk_worldgen_debug_capture_note_water_seal(int lx, int lz, int waterline, BlockType cap_block, int banked)
{
    SdkWorldGenChunkDebugCapture* capture = g_worldgen_debug_capture_tls;
    int idx;

    if (!capture || !capture->active) return;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return;
    idx = wg_column_index(lx, lz);
    if ((capture->column_flags[idx] & WG_COL_FLAG_WATER_SEAL) == 0u) {
        capture->water_seal_columns++;
    }
    capture->column_flags[idx] |= WG_COL_FLAG_WATER_SEAL;
    capture->waterline[idx] = (int16_t)waterline;
    capture->water_cap_block[idx] = (uint8_t)cap_block;
    if (banked && capture->water_banked[idx] == 0u) {
        capture->banked_columns++;
        capture->water_banked[idx] = 1u;
    }
}

void sdk_worldgen_debug_capture_note_wall_column(int lx,
                                                 int lz,
                                                 uint8_t wall_mask,
                                                 uint8_t gate_mask,
                                                 int wall_top_y,
                                                 int gate_floor_y)
{
    SdkWorldGenChunkDebugCapture* capture = g_worldgen_debug_capture_tls;
    int idx;

    if (!capture || !capture->active) return;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return;
    idx = wg_column_index(lx, lz);
    if (wall_mask != 0u && (capture->column_flags[idx] & WG_COL_FLAG_WALL) == 0u) {
        capture->wall_columns++;
    }
    if (gate_mask != 0u && (capture->column_flags[idx] & WG_COL_FLAG_GATE) == 0u) {
        capture->gate_columns++;
    }
    if (wall_mask != 0u) capture->column_flags[idx] |= WG_COL_FLAG_WALL;
    if (gate_mask != 0u) capture->column_flags[idx] |= WG_COL_FLAG_GATE;
    capture->wall_mask[idx] = wall_mask;
    capture->gate_mask[idx] = gate_mask;
    capture->wall_top_y[idx] = (int16_t)wall_top_y;
    capture->gate_floor_y[idx] = (int16_t)gate_floor_y;
}

void sdk_worldgen_debug_capture_note_settlement_stage(void)
{
    SdkWorldGenChunkDebugCapture* capture = g_worldgen_debug_capture_tls;
    if (!capture || !capture->active) return;
    capture->settlement_stage_runs++;
}

void sdk_worldgen_debug_capture_note_route(int route_surface,
                                           int start_kind,
                                           int end_kind,
                                           int start_y,
                                           int end_y,
                                           int max_cut,
                                           int max_fill,
                                           int carved_columns,
                                           int candidate_index)
{
    SdkWorldGenChunkDebugCapture* capture = g_worldgen_debug_capture_tls;
    uint32_t index;

    if (!capture || !capture->active) return;

    index = capture->route_summary_count;
    if (index >= WG_ROUTE_SUMMARY_CAP) {
        capture->route_summary_dropped++;
        return;
    }

    capture->route_summaries[index].route_surface = (uint8_t)route_surface;
    capture->route_summaries[index].start_kind = (uint8_t)start_kind;
    capture->route_summaries[index].end_kind = (uint8_t)end_kind;
    capture->route_summaries[index].start_y = (int16_t)start_y;
    capture->route_summaries[index].end_y = (int16_t)end_y;
    capture->route_summaries[index].max_cut = (int16_t)max_cut;
    capture->route_summaries[index].max_fill = (int16_t)max_fill;
    capture->route_summaries[index].carved_columns = (uint16_t)((carved_columns < 0) ? 0 :
                                                                 (carved_columns > 65535 ? 65535 : carved_columns));
    capture->route_summaries[index].candidate_index = (uint8_t)((candidate_index < 0) ? 255 :
                                                                 (candidate_index > 255 ? 255 : candidate_index));
    capture->route_summary_count++;
}

void sdk_worldgen_debug_capture_note_custom(const char* message)
{
    SdkWorldGenChunkDebugCapture* capture = g_worldgen_debug_capture_tls;
    uint32_t index;
    
    if (!capture || !capture->active || !message) return;
    
    index = capture->custom_note_count;
    if (index >= WG_CUSTOM_NOTE_CAP) {
        capture->custom_note_dropped++;
        return;
    }
    
    strncpy(capture->custom_notes[index], message, WG_CUSTOM_NOTE_LEN - 1);
    capture->custom_notes[index][WG_CUSTOM_NOTE_LEN - 1] = '\0';
    capture->custom_note_count++;
}

static void emit_height_grid(WgChunkReportEmitter* emitter,
                             const char* label,
                             const SdkWorldGenSurfaceColumn* columns,
                             int use_water_height,
                             int use_water_depth)
{
    int lz;
    int lx;

    if (!emitter || !label || !columns) return;
    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        char line[1800];
        int written = 0;

        written += _snprintf(line + written, sizeof(line) - written, "%s[%02d] ", label, lz);
        for (lx = 0; lx < CHUNK_WIDTH && written < (int)sizeof(line) - 8; ++lx) {
            const SdkWorldGenSurfaceColumn* column = &columns[wg_column_index(lx, lz)];
            if (use_water_depth) {
                written += _snprintf(line + written, sizeof(line) - written, "%03u ", (unsigned)column->water_depth);
            } else if (use_water_height) {
                if (column->water_height > 0u || is_waterlike_block(column->top_block)) {
                    written += _snprintf(line + written, sizeof(line) - written, "%03u ", (unsigned)column->water_height);
                } else {
                    written += _snprintf(line + written, sizeof(line) - written, "--- ");
                }
            } else {
                written += _snprintf(line + written, sizeof(line) - written, "%03u ", (unsigned)column->top_height);
            }
        }
        emit_line(emitter, "%s", line);
    }
}

static void emit_province_grid(WgChunkReportEmitter* emitter, const SdkTerrainColumnProfile* profiles)
{
    int lz;
    int lx;

    if (!emitter || !profiles) return;
    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        char line[1800];
        int written = 0;

        written += _snprintf(line + written, sizeof(line) - written, "PROV[%02d] ", lz);
        for (lx = 0; lx < CHUNK_WIDTH && written < (int)sizeof(line) - 8; ++lx) {
            const SdkTerrainColumnProfile* profile = &profiles[wg_column_index(lx, lz)];
            written += _snprintf(line + written, sizeof(line) - written, "%s ", terrain_province_code((int)profile->terrain_province));
        }
        emit_line(emitter, "%s", line);
    }
}

static void emit_modifier_grid(WgChunkReportEmitter* emitter, const uint16_t* modifier_flags)
{
    int lz;
    int lx;

    if (!emitter || !modifier_flags) return;
    emit_line(emitter, "MODIFIER LEGEND water=001 river=002 lake=004 coast=008 wall=010 gate=020 settlement=040 road=080 tree=100 plant=200 seal=400 building=800");
    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        char line[1800];
        int written = 0;

        written += _snprintf(line + written, sizeof(line) - written, "MOD[%02d] ", lz);
        for (lx = 0; lx < CHUNK_WIDTH && written < (int)sizeof(line) - 8; ++lx) {
            written += _snprintf(line + written,
                                 sizeof(line) - written,
                                 "%03X ",
                                 (unsigned)modifier_flags[wg_column_index(lx, lz)] & 0xFFFu);
        }
        emit_line(emitter, "%s", line);
    }
}

static void summarize_landform_flags(uint32_t flags, char* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    buffer[0] = '\0';
    if ((flags & SDK_LANDFORM_RIVER_CHANNEL) != 0u) append_token(buffer, buffer_size, "RIVER_CHANNEL");
    if ((flags & SDK_LANDFORM_FLOODPLAIN) != 0u) append_token(buffer, buffer_size, "FLOODPLAIN");
    if ((flags & SDK_LANDFORM_RAVINE) != 0u) append_token(buffer, buffer_size, "RAVINE");
    if ((flags & SDK_LANDFORM_LAKE_BASIN) != 0u) append_token(buffer, buffer_size, "LAKE_BASIN");
    if ((flags & SDK_LANDFORM_CAVE_ENTRANCE) != 0u) append_token(buffer, buffer_size, "CAVE_ENTRANCE");
    if ((flags & SDK_LANDFORM_VOLCANIC_VENT) != 0u) append_token(buffer, buffer_size, "VOLCANIC_VENT");
    if ((flags & SDK_LANDFORM_CALDERA) != 0u) append_token(buffer, buffer_size, "CALDERA");
    if ((flags & SDK_LANDFORM_LAVA_FIELD) != 0u) append_token(buffer, buffer_size, "LAVA_FIELD");
    if (buffer[0] == '\0') {
        _snprintf(buffer, buffer_size, "NONE");
    }
}

static void summarize_column_influence(uint16_t flags,
                                       const SdkWorldGenChunkDebugCapture* capture,
                                       int idx,
                                       uint8_t building_type,
                                       char* buffer,
                                       size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    buffer[0] = '\0';
    if ((flags & WG_COL_FLAG_WALL) != 0u) append_token(buffer, buffer_size, "WALL");
    if ((flags & WG_COL_FLAG_GATE) != 0u) append_token(buffer, buffer_size, "GATE");
    if ((flags & WG_COL_FLAG_BUILDING) != 0u) append_token(buffer, buffer_size, building_type_name_local((int)building_type));
    if ((flags & WG_COL_FLAG_ROAD) != 0u) append_token(buffer, buffer_size, "ROAD");
    if ((flags & WG_COL_FLAG_TREE) != 0u && capture) append_token(buffer, buffer_size, tree_style_name_local((int)capture->tree_style[idx]));
    if ((flags & WG_COL_FLAG_PLANT) != 0u && capture) append_token(buffer, buffer_size, sdk_block_get_name(capture->plant_block[idx]));
    if ((flags & WG_COL_FLAG_WATER_SEAL) != 0u) append_token(buffer, buffer_size, "WATER_SEAL");
    if ((flags & WG_COL_FLAG_SETTLEMENT) != 0u && (flags & (WG_COL_FLAG_BUILDING | WG_COL_FLAG_ROAD)) == 0u) {
        append_token(buffer, buffer_size, "SETTLEMENT");
    }
    if (buffer[0] == '\0') {
        _snprintf(buffer, buffer_size, "NONE");
    }
}

static int building_overlaps_chunk(const BuildingPlacement* placement, int chunk_wx0, int chunk_wz0)
{
    int chunk_wx1;
    int chunk_wz1;
    int build_wx1;
    int build_wz1;

    if (!placement) return 0;
    chunk_wx1 = chunk_wx0 + CHUNK_WIDTH - 1;
    chunk_wz1 = chunk_wz0 + CHUNK_DEPTH - 1;
    build_wx1 = placement->wx + placement->footprint_x - 1;
    build_wz1 = placement->wz + placement->footprint_z - 1;

    return !(build_wx1 < chunk_wx0 || placement->wx > chunk_wx1 ||
             build_wz1 < chunk_wz0 || placement->wz > chunk_wz1);
}

static void mark_building_columns(uint8_t* building_mask,
                                  uint8_t* building_types,
                                  const BuildingPlacement* placement,
                                  int chunk_wx0,
                                  int chunk_wz0)
{
    int wx0;
    int wz0;
    int wx1;
    int wz1;
    int wx;
    int wz;

    if (!building_mask || !building_types || !placement) return;
    wx0 = placement->wx;
    wz0 = placement->wz;
    wx1 = placement->wx + placement->footprint_x - 1;
    wz1 = placement->wz + placement->footprint_z - 1;
    if (wx0 < chunk_wx0) wx0 = chunk_wx0;
    if (wz0 < chunk_wz0) wz0 = chunk_wz0;
    if (wx1 > chunk_wx0 + CHUNK_WIDTH - 1) wx1 = chunk_wx0 + CHUNK_WIDTH - 1;
    if (wz1 > chunk_wz0 + CHUNK_DEPTH - 1) wz1 = chunk_wz0 + CHUNK_DEPTH - 1;

    for (wz = wz0; wz <= wz1; ++wz) {
        for (wx = wx0; wx <= wx1; ++wx) {
            int lx = wx - chunk_wx0;
            int lz = wz - chunk_wz0;
            int idx = wg_column_index(lx, lz);
            building_mask[idx] = 1u;
            building_types[idx] = (uint8_t)placement->type;
        }
    }
}

static void analyze_live_diff(const SdkChunk* generated_chunk,
                              const SdkChunk* live_chunk,
                              WgLiveDiffSummary* out_summary)
{
    uint32_t idx;

    if (!out_summary) return;
    memset(out_summary, 0, sizeof(*out_summary));
    if (!generated_chunk || !generated_chunk->blocks || !live_chunk || !live_chunk->blocks) return;

    for (idx = 0; idx < CHUNK_TOTAL_BLOCKS; ++idx) {
        BlockType generated_block = sdk_world_cell_decode_full_block(generated_chunk->blocks[idx]);
        BlockType live_block = sdk_world_cell_decode_full_block(live_chunk->blocks[idx]);

        if (generated_block == live_block) continue;
        out_summary->total_changed++;
        if (generated_block == BLOCK_AIR && live_block != BLOCK_AIR) {
            out_summary->added_blocks++;
        } else if (generated_block != BLOCK_AIR && live_block == BLOCK_AIR) {
            out_summary->removed_blocks++;
        } else {
            out_summary->changed_blocks++;
        }

        if (out_summary->captured < WG_CHUNK_REPORT_DIFF_CAP) {
            uint32_t local = idx % CHUNK_BLOCKS_PER_LAYER;
            out_summary->entries[out_summary->captured].ly = (int)(idx / CHUNK_BLOCKS_PER_LAYER);
            out_summary->entries[out_summary->captured].lz = (int)(local / CHUNK_WIDTH);
            out_summary->entries[out_summary->captured].lx = (int)(local % CHUNK_WIDTH);
            out_summary->entries[out_summary->captured].generated_block = generated_block;
            out_summary->entries[out_summary->captured].live_block = live_block;
            out_summary->captured++;
        }
    }
}

static void build_block_totals(const SdkChunk* chunk,
                               uint32_t* out_non_air,
                               uint32_t* out_water,
                               uint32_t* out_ice,
                               uint32_t* out_lava,
                               uint32_t* out_logs,
                               uint32_t* out_leaves,
                               uint32_t* out_road)
{
    uint32_t idx;

    if (out_non_air) *out_non_air = 0u;
    if (out_water) *out_water = 0u;
    if (out_ice) *out_ice = 0u;
    if (out_lava) *out_lava = 0u;
    if (out_logs) *out_logs = 0u;
    if (out_leaves) *out_leaves = 0u;
    if (out_road) *out_road = 0u;
    if (!chunk || !chunk->blocks) return;

    for (idx = 0; idx < CHUNK_TOTAL_BLOCKS; ++idx) {
        BlockType block = sdk_world_cell_decode_full_block(chunk->blocks[idx]);
        if (block == BLOCK_AIR) continue;
        if (out_non_air) (*out_non_air)++;
        if (out_water && block == BLOCK_WATER) (*out_water)++;
        if (out_ice && (block == BLOCK_ICE || block == BLOCK_SEA_ICE)) (*out_ice)++;
        if (out_lava && block == BLOCK_LAVA) (*out_lava)++;
        if (out_logs && block == BLOCK_LOG) (*out_logs)++;
        if (out_leaves && block == BLOCK_LEAVES) (*out_leaves)++;
        if (out_road && is_road_block(block)) (*out_road)++;
    }
}

void sdk_worldgen_emit_chunk_debug_report(SdkWorldGen* wg, SdkChunkManager* cm, int cx, int cz)
{
    WgChunkReportEmitter emitter = { 0 };
    SdkChunk generated_chunk;
    SdkChunk* live_chunk;
    SdkChunkResidentSlot* live_slot;
    SdkWorldGenChunkDebugCapture capture;
    SdkTerrainColumnProfile* profiles;
    SdkWorldGenSurfaceColumn* model_columns;
    SdkWorldGenSurfaceColumn* final_columns;
    uint16_t* modifier_flags;
    uint8_t* building_mask;
    uint8_t* building_types;
    SettlementMetadata* settlements[8];
    int settlement_count;
    int chunk_wx0;
    int chunk_wz0;
    int model_drift_columns = 0;
    int road_columns = 0;
    uint32_t terrain_counts[32];
    uint32_t ecology_counts[32];
    uint32_t resource_counts[32];
    uint32_t non_air_blocks;
    uint32_t water_blocks;
    uint32_t ice_blocks;
    uint32_t lava_blocks;
    uint32_t log_blocks;
    uint32_t leaf_blocks;
    uint32_t road_blocks;
    WgLiveDiffSummary live_diff;
    int dominant_terrain = 0;
    int dominant_ecology = 0;
    int dominant_resource = 0;
    int building_column_count = 0;
    int lx;
    int lz;

    if (!wg || !wg->impl) {
        sdk_debug_log_output("[WG-CHUNK] REPORT unavailable: worldgen inactive\n");
        return;
    }

    emit_line(&emitter, "==== REPORT BEGIN chunk=(%d,%d) ====", cx, cz);

    profiles = (SdkTerrainColumnProfile*)calloc(CHUNK_BLOCKS_PER_LAYER, sizeof(*profiles));
    model_columns = (SdkWorldGenSurfaceColumn*)calloc(CHUNK_BLOCKS_PER_LAYER, sizeof(*model_columns));
    final_columns = (SdkWorldGenSurfaceColumn*)calloc(CHUNK_BLOCKS_PER_LAYER, sizeof(*final_columns));
    modifier_flags = (uint16_t*)calloc(CHUNK_BLOCKS_PER_LAYER, sizeof(*modifier_flags));
    building_mask = (uint8_t*)calloc(CHUNK_BLOCKS_PER_LAYER, sizeof(*building_mask));
    building_types = (uint8_t*)calloc(CHUNK_BLOCKS_PER_LAYER, sizeof(*building_types));
    if (!profiles || !model_columns || !final_columns || !modifier_flags || !building_mask || !building_types) {
        emit_line(&emitter, "REPORT aborted: allocation failure");
        free(profiles);
        free(model_columns);
        free(final_columns);
        free(modifier_flags);
        free(building_mask);
        free(building_types);
        emit_line(&emitter, "==== REPORT END lines=%d live_diff=0 ====", emitter.line_count);
        return;
    }

    sdk_chunk_init(&generated_chunk, cx, cz, NULL);
    sdk_worldgen_debug_capture_begin(&capture);
    capture.cx = cx;
    capture.cz = cz;
    sdk_worldgen_generate_chunk_ctx(wg, &generated_chunk);
    sdk_worldgen_debug_capture_end();

    if (!sdk_worldgen_generate_chunk_surface_ctx(wg, cx, cz, profiles, model_columns)) {
        memset(profiles, 0, sizeof(*profiles) * CHUNK_BLOCKS_PER_LAYER);
        memset(model_columns, 0, sizeof(*model_columns) * CHUNK_BLOCKS_PER_LAYER);
    }
    scan_generated_surface_columns(&generated_chunk, final_columns);

    live_chunk = cm ? sdk_chunk_manager_get_chunk(cm, cx, cz) : NULL;
    live_slot = cm ? sdk_chunk_manager_find_slot(cm, cx, cz) : NULL;
    analyze_live_diff(&generated_chunk, live_chunk, &live_diff);

    memset(terrain_counts, 0, sizeof(terrain_counts));
    memset(ecology_counts, 0, sizeof(ecology_counts));
    memset(resource_counts, 0, sizeof(resource_counts));

    settlement_count = sdk_settlement_get_for_chunk(wg, cx, cz, settlements, (int)(sizeof(settlements) / sizeof(settlements[0])));
    chunk_wx0 = cx * CHUNK_WIDTH;
    chunk_wz0 = cz * CHUNK_DEPTH;

    for (int settlement_index = 0; settlement_index < settlement_count; ++settlement_index) {
        SettlementLayout* layout = sdk_settlement_generate_layout(wg, settlements[settlement_index]);
        if (!layout) continue;
        for (uint32_t building_index = 0; building_index < layout->building_count; ++building_index) {
            const BuildingPlacement* placement = &layout->buildings[building_index];
            if (building_overlaps_chunk(placement, chunk_wx0, chunk_wz0)) {
                mark_building_columns(building_mask, building_types, placement, chunk_wx0, chunk_wz0);
            }
        }
        sdk_settlement_free_layout(layout);
    }

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int idx = wg_column_index(lx, lz);
            const SdkTerrainColumnProfile* profile = &profiles[idx];
            const SdkWorldGenSurfaceColumn* model_column = &model_columns[idx];
            const SdkWorldGenSurfaceColumn* final_column = &final_columns[idx];
            uint16_t flags = capture.column_flags[idx];

            if ((int)profile->terrain_province >= 0 && (int)profile->terrain_province < (int)(sizeof(terrain_counts) / sizeof(terrain_counts[0]))) {
                terrain_counts[(int)profile->terrain_province]++;
            }
            if ((int)profile->ecology >= 0 && (int)profile->ecology < (int)(sizeof(ecology_counts) / sizeof(ecology_counts[0]))) {
                ecology_counts[(int)profile->ecology]++;
            }
            if ((int)profile->resource_province >= 0 && (int)profile->resource_province < (int)(sizeof(resource_counts) / sizeof(resource_counts[0]))) {
                resource_counts[(int)profile->resource_province]++;
            }

            if (final_column->water_height > 0u || is_waterlike_block(final_column->top_block)) flags |= WG_COL_FLAG_WATER;
            if (profile->river_order > 0u) flags |= WG_COL_FLAG_RIVER;
            if ((profile->landform_flags & SDK_LANDFORM_LAKE_BASIN) != 0u ||
                profile->terrain_province == TERRAIN_PROVINCE_PEAT_WETLAND ||
                profile->terrain_province == TERRAIN_PROVINCE_ESTUARY_DELTA) {
                flags |= WG_COL_FLAG_LAKE;
            }
            if (profile->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN ||
                profile->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF ||
                profile->terrain_province == TERRAIN_PROVINCE_DYNAMIC_COAST) {
                flags |= WG_COL_FLAG_COAST;
            }
            if (building_mask[idx]) {
                flags |= WG_COL_FLAG_BUILDING | WG_COL_FLAG_SETTLEMENT;
                building_column_count++;
            }
            if (is_road_block(final_column->top_block)) {
                flags |= WG_COL_FLAG_ROAD | WG_COL_FLAG_SETTLEMENT;
                road_columns++;
            }
            if (model_column->top_block != final_column->top_block ||
                model_column->top_height != final_column->top_height) {
                model_drift_columns++;
            }
            modifier_flags[idx] = flags;
        }
    }

    for (int i = 1; i < (int)(sizeof(terrain_counts) / sizeof(terrain_counts[0])); ++i) {
        if (terrain_counts[i] > terrain_counts[dominant_terrain]) dominant_terrain = i;
        if (ecology_counts[i] > ecology_counts[dominant_ecology]) dominant_ecology = i;
        if (resource_counts[i] > resource_counts[dominant_resource]) dominant_resource = i;
    }

    build_block_totals(&generated_chunk,
                       &non_air_blocks,
                       &water_blocks,
                       &ice_blocks,
                       &lava_blocks,
                       &log_blocks,
                       &leaf_blocks,
                       &road_blocks);

    emit_line(&emitter, "HEADER");
    emit_line(&emitter,
              "chunk=(%d,%d) world_bounds=[%d..%d,%d..%d] seed=0x%08X sea_level=%d primary_sc=(%d,%d) desired_sc=(%d,%d) loaded=%s role=%s render=%s dirty=%u upload_pending=%u",
              cx, cz,
              chunk_wx0, chunk_wx0 + CHUNK_WIDTH - 1,
              chunk_wz0, chunk_wz0 + CHUNK_DEPTH - 1,
              wg->desc.seed,
              (int)wg->desc.sea_level,
              cm ? cm->primary_scx : 0, cm ? cm->primary_scz : 0,
              cm ? cm->desired_scx : 0, cm ? cm->desired_scz : 0,
              live_chunk ? "yes" : "no",
              live_slot ? chunk_role_name(live_slot->role) : "NONE",
              live_slot ? render_representation_name(live_slot->render_representation) : "NONE",
              live_chunk ? (unsigned)live_chunk->dirty : 0u,
              live_chunk ? (unsigned)live_chunk->upload_pending : 0u);

    emit_line(&emitter, "GENERATION SUMMARY");
    emit_line(&emitter,
              "dominant terrain=%s ecology=%s resource=%s settlement_count=%d settlement_stage_runs=%u",
              terrain_province_name(dominant_terrain),
              ecology_name(dominant_ecology),
              resource_name(dominant_resource),
              settlement_count,
              capture.settlement_stage_runs);
    emit_line(&emitter,
              "blocks non_air=%u water=%u ice=%u lava=%u logs=%u leaves=%u road_blocks=%u",
              non_air_blocks, water_blocks, ice_blocks, lava_blocks, log_blocks, leaf_blocks, road_blocks);
    emit_line(&emitter,
              "columns wall=%u gate=%u tree=%u plant=%u water_seal=%u banked=%u building=%d road=%d model_drift=%d",
              capture.wall_columns,
              capture.gate_columns,
              capture.tree_columns,
              capture.plant_columns,
              capture.water_seal_columns,
              capture.banked_columns,
              building_column_count,
              road_columns,
              model_drift_columns);

    emit_line(&emitter, "OVERVIEW GRIDS");
    emit_height_grid(&emitter, "TOPY", final_columns, 0, 0);
    emit_height_grid(&emitter, "WATY", final_columns, 1, 0);
    emit_height_grid(&emitter, "WDEP", final_columns, 0, 1);
    emit_province_grid(&emitter, profiles);
    emit_modifier_grid(&emitter, modifier_flags);

    emit_line(&emitter, "PER-COLUMN APPENDIX");
    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            int idx = wg_column_index(lx, lz);
            int wx = chunk_wx0 + lx;
            int wz = chunk_wz0 + lz;
            const SdkTerrainColumnProfile* profile = &profiles[idx];
            const SdkWorldGenSurfaceColumn* final_column = &final_columns[idx];
            SdkContinentalSample continental;
            SdkRegionFieldSample geology;
            char landform_text[192];
            char influence_text[192];
            const char* wall_desc = "NONE";
            const char* gate_desc = "NONE";

            sdk_worldgen_sample_continental_state(wg, wx, wz, &continental);
            memset(&geology, 0, sizeof(geology));
            {
                SdkWorldGenRegionTile* region_tile = sdk_worldgen_require_region_tile(wg, wx, wz);
                if (region_tile) {
                    sdk_worldgen_sample_region_fields(region_tile, wg, wx, wz, &geology);
                }
            }
            summarize_landform_flags(profile->landform_flags, landform_text, sizeof(landform_text));
            summarize_column_influence(modifier_flags[idx], &capture, idx, building_types[idx], influence_text, sizeof(influence_text));

            if (capture.wall_mask[idx] != 0u) {
                wall_desc = (capture.wall_mask[idx] & WG_SUPER_WALL_WEST) ? "WEST" :
                            (capture.wall_mask[idx] & WG_SUPER_WALL_NORTH) ? "NORTH" :
                            (capture.wall_mask[idx] & WG_SUPER_WALL_EAST) ? "EAST" :
                            (capture.wall_mask[idx] & WG_SUPER_WALL_SOUTH) ? "SOUTH" : "MULTI";
            }
            if (capture.gate_mask[idx] != 0u) {
                gate_desc = (capture.gate_mask[idx] & WG_SUPER_WALL_WEST) ? "WEST_GATE" :
                            (capture.gate_mask[idx] & WG_SUPER_WALL_NORTH) ? "NORTH_GATE" :
                            (capture.gate_mask[idx] & WG_SUPER_WALL_EAST) ? "EAST_GATE" :
                            (capture.gate_mask[idx] & WG_SUPER_WALL_SOUTH) ? "SOUTH_GATE" : "MULTI_GATE";
            }

            emit_line(&emitter,
                      "COL (%02d,%02d) W(%d,%d) h=%d/%d/%d/%d prov=%s bed=%s temp=%s moist=%s sed=%s parent=%s drain=%s react=%s fert=%s sal=%s eco=%s res=%s hc=%s grade=%u water=%s cont{basin=%u flow=%.2f harbor=%.2f flood=%.2f build=%.2f} geo{wb=%.1f up=%.1f lo=%.1f base=%.1f trap=%.2f seal=%.2f vent=%.2f} landform=%s final=%s top=%s@%u land=%s@%u wall=%s gate=%s wallTop=%d gateFloor=%d influence=%s",
                      lx, lz, wx, wz,
                      profile->base_height, profile->surface_height, profile->water_height, profile->river_bed_height,
                      terrain_province_name((int)profile->terrain_province),
                      bedrock_name((int)profile->bedrock_province),
                      temperature_name((int)profile->temperature_band),
                      moisture_name((int)profile->moisture_band),
                      sediment_name((int)profile->surface_sediment),
                      parent_material_name((int)profile->parent_material),
                      drainage_name((int)profile->drainage_class),
                      soil_reaction_name((int)profile->soil_reaction),
                      soil_fertility_name((int)profile->soil_fertility),
                      soil_salinity_name((int)profile->soil_salinity),
                      ecology_name((int)profile->ecology),
                      resource_name((int)profile->resource_province),
                      hydrocarbon_name((int)profile->hydrocarbon_class),
                      (unsigned)profile->resource_grade,
                      surface_water_name((int)profile->water_surface_class),
                      continental.basin_id,
                      continental.flow_accum,
                      continental.harbor_score,
                      continental.flood_risk,
                      continental.buildable_flatness,
                      geology.weathered_base,
                      geology.upper_top,
                      geology.lower_top,
                      geology.basement_top,
                      geology.trap_strength,
                      geology.seal_quality,
                      geology.vent_mask,
                      landform_text,
                      surface_kind_name(final_column->kind),
                      sdk_block_get_name(final_column->top_block),
                      (unsigned)final_column->top_height,
                      sdk_block_get_name(final_column->land_block),
                      (unsigned)final_column->land_height,
                      wall_desc,
                      gate_desc,
                      (int)capture.wall_top_y[idx],
                      (int)capture.gate_floor_y[idx],
                      influence_text);
        }
    }

    emit_line(&emitter, "SETTLEMENT / ROAD / WALL");
    {
        uint8_t owner_mask = 0u;
        int period_local_x = sdk_superchunk_wall_grid_chunk_local_period_x(cx);
        int period_local_z = sdk_superchunk_wall_grid_chunk_local_period_z(cz);

        sdk_worldgen_get_canonical_wall_chunk_owner(cx,
                                                    cz,
                                                    &owner_mask,
                                                    NULL,
                                                    NULL,
                                                    &period_local_x,
                                                    &period_local_z);

        emit_line(&emitter,
                  "chunk_local_super=(%d,%d) canonical_owner_faces=%s%s%s wall_columns=%u gate_columns=%u transition_active=%d road_columns=%d settlement_stage_runs=%u route_summaries=%u dropped=%u",
                  period_local_x,
                  period_local_z,
                  (owner_mask & WG_SUPER_WALL_WEST) ? "WEST" : "",
                  ((owner_mask & WG_SUPER_WALL_WEST) && (owner_mask & WG_SUPER_WALL_NORTH)) ? "|" : "",
                  (owner_mask & WG_SUPER_WALL_NORTH) ? "NORTH" : "",
                  capture.wall_columns,
                  capture.gate_columns,
                  cm ? cm->transition_active : 0,
                  road_columns,
                  capture.settlement_stage_runs,
                  capture.route_summary_count,
                  capture.route_summary_dropped);
    }

    for (uint32_t route_index = 0; route_index < capture.route_summary_count; ++route_index) {
        emit_line(&emitter,
                  "route[%u] surface=%s start=%s@%d end=%s@%d max_cut=%d max_fill=%d carved_columns=%u candidate=%u",
                  route_index,
                  route_surface_name((int)capture.route_summaries[route_index].route_surface),
                  route_endpoint_name((int)capture.route_summaries[route_index].start_kind),
                  (int)capture.route_summaries[route_index].start_y,
                  route_endpoint_name((int)capture.route_summaries[route_index].end_kind),
                  (int)capture.route_summaries[route_index].end_y,
                  (int)capture.route_summaries[route_index].max_cut,
                  (int)capture.route_summaries[route_index].max_fill,
                  (unsigned)capture.route_summaries[route_index].carved_columns,
                  (unsigned)capture.route_summaries[route_index].candidate_index);
    }

    if (capture.custom_note_count > 0 || capture.custom_note_dropped > 0) {
        emit_line(&emitter, "CUSTOM_NOTES count=%u dropped=%u",
                  capture.custom_note_count, capture.custom_note_dropped);
        for (uint32_t note_index = 0; note_index < capture.custom_note_count; ++note_index) {
            emit_line(&emitter, "custom_note[%u] %s", note_index, capture.custom_notes[note_index]);
        }
    }

    if (settlement_count <= 0) {
        emit_line(&emitter, "no settlements intersect this chunk");
    } else {
        for (int settlement_index = 0; settlement_index < settlement_count; ++settlement_index) {
            SettlementMetadata* settlement = settlements[settlement_index];
            SettlementLayout* layout = sdk_settlement_generate_layout(wg, settlement);
            int intersecting_buildings = 0;

            emit_line(&emitter,
                      "settlement id=%u type=%s purpose=%s state=%s center=(%d,%d) radius=%u pop=%u/%u zones=%u water=%.2f fert=%.2f defend=%.2f flat=%.2f food=%.2f output=%.2f",
                      settlement->settlement_id,
                      settlement_type_name_local((int)settlement->type),
                      settlement_purpose_name_local((int)settlement->purpose),
                      settlement_state_name_local((int)settlement->state),
                      settlement->center_wx,
                      settlement->center_wz,
                      (unsigned)settlement->radius,
                      settlement->population,
                      settlement->max_population,
                      settlement->zone_count,
                      settlement->water_access,
                      settlement->fertility,
                      settlement->defensibility,
                      settlement->flatness,
                      settlement->food_production,
                      settlement->resource_output);
            for (uint32_t zone_index = 0; zone_index < settlement->zone_count; ++zone_index) {
                const BuildingZone* zone = &settlement->zones[zone_index];
                emit_line(&emitter,
                          "zone settlement=%u idx=%u type=%s center=(%d,%d) radii=%d,%d base=%d terrain_mod=%u",
                          settlement->settlement_id,
                          (unsigned)zone_index,
                          zone_type_name_local((int)zone->zone_type),
                          zone->center_wx,
                          zone->center_wz,
                          zone->radius_x,
                          zone->radius_z,
                          zone->base_elevation,
                          zone->terrain_modification);
            }

            if (layout) {
                for (uint32_t building_index = 0; building_index < layout->building_count; ++building_index) {
                    const BuildingPlacement* placement = &layout->buildings[building_index];
                    if (!building_overlaps_chunk(placement, chunk_wx0, chunk_wz0)) continue;
                    intersecting_buildings++;
                    emit_line(&emitter,
                              "building settlement=%u idx=%u type=%s pos=(%d,%d) base=%d footprint=%ux%u height=%u rot=%u",
                              settlement->settlement_id,
                              (unsigned)building_index,
                              building_type_name_local((int)placement->type),
                              placement->wx,
                              placement->wz,
                              placement->base_elevation,
                              placement->footprint_x,
                              placement->footprint_z,
                              placement->height,
                              placement->rotation);
                }
                emit_line(&emitter,
                          "settlement id=%u intersecting_buildings=%d total_layout_buildings=%u",
                          settlement->settlement_id,
                          intersecting_buildings,
                          layout->building_count);
                sdk_settlement_free_layout(layout);
            }
        }
    }

    emit_line(&emitter, "LIVE DIFF");
    if (!live_chunk) {
        emit_line(&emitter, "live resident chunk unavailable");
    } else if (live_diff.total_changed <= 0) {
        emit_line(&emitter, "resident chunk matches deterministic regeneration");
    } else {
        emit_line(&emitter,
                  "changed=%d added=%d removed=%d mutated=%d shown=%d",
                  live_diff.total_changed,
                  live_diff.added_blocks,
                  live_diff.removed_blocks,
                  live_diff.changed_blocks,
                  live_diff.captured);
        for (int diff_index = 0; diff_index < live_diff.captured; ++diff_index) {
            const int wx = chunk_wx0 + live_diff.entries[diff_index].lx;
            const int wz = chunk_wz0 + live_diff.entries[diff_index].lz;
            emit_line(&emitter,
                      "diff[%02d] local=(%d,%d,%d) world=(%d,%d,%d) generated=%s live=%s",
                      diff_index,
                      live_diff.entries[diff_index].lx,
                      live_diff.entries[diff_index].ly,
                      live_diff.entries[diff_index].lz,
                      wx,
                      live_diff.entries[diff_index].ly,
                      wz,
                      sdk_block_get_name(live_diff.entries[diff_index].generated_block),
                      sdk_block_get_name(live_diff.entries[diff_index].live_block));
        }
    }

    sdk_chunk_free(&generated_chunk);
    free(profiles);
    free(model_columns);
    free(final_columns);
    free(modifier_flags);
    free(building_mask);
    free(building_types);

    emit_line(&emitter, "==== REPORT END lines=%d live_diff=%s ====", emitter.line_count, live_chunk ? "yes" : "no");
}
