/**
 * sdk_settlement.c -- Core settlement generation and management
 */
#include "sdk_settlement.h"
#include "Roads/sdk_settlement_roads.h"
#include "../Worldgen/Types/sdk_worldgen_types.h"
#include "../Worldgen/Internal/sdk_worldgen_internal.h"
#include "../Superchunks/Geometry/sdk_superchunk_geometry.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static int g_sdk_settlement_diagnostics_enabled = 1;

typedef struct {
    uint16_t residential_count;
    uint16_t production_count;
    uint16_t storage_count;
    uint16_t defense_count;
    uint16_t farm_count;
    uint16_t dock_count;
} SettlementLayoutStats;

static int floor_div_superchunk(int chunk_coord)
{
    return sdk_superchunk_floor_div_i(chunk_coord, SDK_SUPERCHUNK_WALL_PERIOD);
}

static int floor_div_world(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

static int floor_mod_world(int value, int denom)
{
    int div = floor_div_world(value, denom);
    return value - div * denom;
}

static uint16_t determine_radius(SettlementType type);

static SettlementType infer_settlement_type_from_radius(uint16_t radius)
{
    if (radius >= 300) return SETTLEMENT_TYPE_CITY;
    if (radius >= 150) return SETTLEMENT_TYPE_TOWN;
    return SETTLEMENT_TYPE_VILLAGE;
}

static float clamp01f(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static int clampi(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void sdk_settlement_set_diagnostics_enabled(bool enabled)
{
    g_sdk_settlement_diagnostics_enabled = enabled ? 1 : 0;
}

static int chunk_intersects_settlement_radius(const SdkChunk* chunk, int center_wx, int center_wz, int radius)
{
    int chunk_min_x;
    int chunk_min_z;
    int chunk_max_x;
    int chunk_max_z;
    int nearest_x;
    int nearest_z;
    int dx;
    int dz;

    if (!chunk) return 0;

    chunk_min_x = chunk->cx * CHUNK_WIDTH;
    chunk_min_z = chunk->cz * CHUNK_DEPTH;
    chunk_max_x = chunk_min_x + CHUNK_WIDTH - 1;
    chunk_max_z = chunk_min_z + CHUNK_DEPTH - 1;
    nearest_x = clampi(center_wx, chunk_min_x, chunk_max_x);
    nearest_z = clampi(center_wz, chunk_min_z, chunk_max_z);
    dx = center_wx - nearest_x;
    dz = center_wz - nearest_z;
    return dx * dx + dz * dz <= radius * radius;
}

static int settlement_intersects_chunk(const SettlementMetadata* settlement, int cx, int cz)
{
    int chunk_min_x = cx * CHUNK_WIDTH;
    int chunk_min_z = cz * CHUNK_DEPTH;
    int chunk_max_x = chunk_min_x + CHUNK_WIDTH - 1;
    int chunk_max_z = chunk_min_z + CHUNK_DEPTH - 1;
    int nearest_x = clampi(settlement->center_wx, chunk_min_x, chunk_max_x);
    int nearest_z = clampi(settlement->center_wz, chunk_min_z, chunk_max_z);
    int dx = settlement->center_wx - nearest_x;
    int dz = settlement->center_wz - nearest_z;
    int radius = settlement->radius;
    return dx * dx + dz * dz <= radius * radius;
}

static void compute_settlement_chunk_intersections(SettlementMetadata* settlement, int scx, int scz)
{
    int chunk_span = (settlement->radius / CHUNK_WIDTH) + 2;
    int center_cx = floor_div_world(settlement->center_wx, CHUNK_WIDTH);
    int center_cz = floor_div_world(settlement->center_wz, CHUNK_DEPTH);
    SdkSuperchunkCell cell;
    int dx, dz, cx, cz;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    settlement->chunk_count = 0;

    for (dx = -chunk_span; dx <= chunk_span && settlement->chunk_count < SDK_MAX_CHUNKS_PER_SETTLEMENT; dx++) {
        for (dz = -chunk_span; dz <= chunk_span && settlement->chunk_count < SDK_MAX_CHUNKS_PER_SETTLEMENT; dz++) {
            cx = center_cx + dx;
            cz = center_cz + dz;

            if (cx < cell.interior_min_cx || cx > cell.interior_max_cx) continue;
            if (cz < cell.interior_min_cz || cz > cell.interior_max_cz) continue;

            if (settlement_intersects_chunk(settlement, cx, cz)) {
                int idx = settlement->chunk_count++;
                settlement->chunks[idx].cx = (int16_t)(cx - cell.interior_min_cx);
                settlement->chunks[idx].cz = (int16_t)(cz - cell.interior_min_cz);
            }
        }
    }
}

static void build_chunk_settlement_index(SuperchunkSettlementData* data)
{
    uint32_t s;
    uint16_t c;
    int cx, cz;
    
    memset(data->chunk_settlement_count, 0, sizeof(data->chunk_settlement_count));
    memset(data->chunk_settlement_indices, 0xFF, sizeof(data->chunk_settlement_indices));
    
    for (s = 0; s < data->settlement_count; s++) {
        SettlementMetadata* settlement = &data->settlements[s];
        
        for (c = 0; c < settlement->chunk_count; c++) {
            cx = settlement->chunks[c].cx;
            cz = settlement->chunks[c].cz;
            
            if (cx >= 0 && cx < 16 && cz >= 0 && cz < 16) {
                if (data->chunk_settlement_count[cx][cz] < 8) {
                    data->chunk_settlement_indices[cx][cz][data->chunk_settlement_count[cx][cz]] = (uint8_t)s;
                    data->chunk_settlement_count[cx][cz]++;
                }
            }
        }
    }
}

static int settlement_wall_clearance_blocks(uint16_t radius)
{
    (void)radius;
    return SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS + 16;
}

static int settlement_center_fits_inside_wall_bounds(int wx, int wz, uint16_t radius)
{
    int local_x = floor_mod_world(wx, SDK_SUPERCHUNK_BLOCK_SPAN);
    int local_z = floor_mod_world(wz, SDK_SUPERCHUNK_BLOCK_SPAN);
    int clearance = settlement_wall_clearance_blocks(radius);
    int max_local = SDK_SUPERCHUNK_BLOCK_SPAN - clearance - 1;

    return local_x >= clearance &&
           local_x <= max_local &&
           local_z >= clearance &&
           local_z <= max_local;
}

static int clamp_settlement_center_inside_wall_bounds(SettlementMetadata* settlement)
{
    int scx;
    int scz;
    int local_x;
    int local_z;
    int clearance;
    int max_local;
    int clamped_x;
    int clamped_z;
    uint16_t radius;

    if (!settlement) return 0;

    radius = settlement->radius;
    if (radius == 0) {
        radius = determine_radius(settlement->type);
    }

    clearance = settlement_wall_clearance_blocks(radius);
    max_local = SDK_SUPERCHUNK_BLOCK_SPAN - clearance - 1;
    if (max_local < clearance) {
        clearance = SDK_SUPERCHUNK_BLOCK_SPAN / 2;
        max_local = clearance;
    }

    scx = floor_div_world(settlement->center_wx, SDK_SUPERCHUNK_BLOCK_SPAN);
    scz = floor_div_world(settlement->center_wz, SDK_SUPERCHUNK_BLOCK_SPAN);
    local_x = floor_mod_world(settlement->center_wx, SDK_SUPERCHUNK_BLOCK_SPAN);
    local_z = floor_mod_world(settlement->center_wz, SDK_SUPERCHUNK_BLOCK_SPAN);
    clamped_x = clampi(local_x, clearance, max_local);
    clamped_z = clampi(local_z, clearance, max_local);

    if (clamped_x == local_x && clamped_z == local_z) {
        return 0;
    }

    settlement->center_wx = scx * SDK_SUPERCHUNK_BLOCK_SPAN + clamped_x;
    settlement->center_wz = scz * SDK_SUPERCHUNK_BLOCK_SPAN + clamped_z;
    return 1;
}

static float estimate_defensibility(const SettlementMetadata* settlement)
{
    float score = 0.25f;

    if (!settlement) return 0.0f;

    switch (settlement->geographic_variant) {
        case GEOGRAPHIC_VARIANT_MOUNTAIN:  score += 0.40f; break;
        case GEOGRAPHIC_VARIANT_COASTAL:   score += 0.18f; break;
        case GEOGRAPHIC_VARIANT_RIVERSIDE: score += 0.12f; break;
        case GEOGRAPHIC_VARIANT_JUNCTION:  score += 0.10f; break;
        default: break;
    }

    score += settlement->water_access * 0.05f;
    score += (1.0f - settlement->flatness) * 0.10f;
    return clamp01f(score);
}

static void accumulate_layout_stats(const SettlementLayout* layout, SettlementLayoutStats* stats)
{
    uint32_t i;

    if (!layout || !stats) return;

    memset(stats, 0, sizeof(*stats));
    for (i = 0; i < layout->building_count; ++i) {
        switch (layout->buildings[i].type) {
            case BUILDING_TYPE_HUT:
            case BUILDING_TYPE_HOUSE:
            case BUILDING_TYPE_MANOR:
                stats->residential_count++;
                break;
            case BUILDING_TYPE_FARM:
                stats->production_count++;
                stats->farm_count++;
                break;
            case BUILDING_TYPE_BARN:
            case BUILDING_TYPE_WORKSHOP:
            case BUILDING_TYPE_FORGE:
            case BUILDING_TYPE_MILL:
            case BUILDING_TYPE_WELL:
            case BUILDING_TYPE_MARKET:
                stats->production_count++;
                break;
            case BUILDING_TYPE_DOCK:
                stats->production_count++;
                stats->dock_count++;
                break;
            case BUILDING_TYPE_STOREHOUSE:
            case BUILDING_TYPE_WAREHOUSE:
            case BUILDING_TYPE_SILO:
                stats->storage_count++;
                break;
            case BUILDING_TYPE_WATCHTOWER:
            case BUILDING_TYPE_BARRACKS:
            case BUILDING_TYPE_WALL_SECTION:
                stats->defense_count++;
                break;
            default:
                break;
        }
    }
}

static void populate_settlement_summary(SdkWorldGen* wg, SettlementMetadata* settlement)
{
    SettlementLayout* layout;
    SettlementLayoutStats stats;

    if (!wg || !settlement) return;

    layout = sdk_settlement_generate_layout(wg, settlement);
    if (!layout) return;

    accumulate_layout_stats(layout, &stats);
    settlement->residential_count = stats.residential_count;
    settlement->production_count = stats.production_count;
    settlement->storage_count = stats.storage_count;
    settlement->defense_count = stats.defense_count;
    settlement->max_population = sdk_settlement_calculate_population(settlement, stats.residential_count);
    settlement->population = settlement->max_population;
    settlement->food_production = sdk_settlement_calculate_food_production(
        settlement,
        settlement->fertility,
        stats.farm_count,
        stats.dock_count);
    settlement->resource_output = (float)stats.production_count * settlement->integrity;
    settlement->defensibility = estimate_defensibility(settlement);

    sdk_settlement_free_layout(layout);
}

static void sanitize_settlement_metadata(SdkWorldGen* wg, SettlementMetadata* settlement)
{
    SdkTerrainColumnProfile profile;
    SdkContinentalSample continental;
    SettlementSuitability suit;
    int surface_y;
    int needs_foundation = 0;

    if (!wg || !settlement) return;

    if (settlement->type <= SETTLEMENT_TYPE_NONE || settlement->type > SETTLEMENT_TYPE_CITY) {
        settlement->type = infer_settlement_type_from_radius(settlement->radius);
        needs_foundation = 1;
    }

    if (settlement->radius == 0) {
        settlement->radius = determine_radius(settlement->type);
        needs_foundation = 1;
    }

    if (!sdk_worldgen_sample_column_ctx(wg, settlement->center_wx, settlement->center_wz, &profile)) return;

    sdk_settlement_sample_continental_approximation(wg, settlement->center_wx, settlement->center_wz, &continental);
    surface_y = sdk_worldgen_get_surface_y_ctx(wg, settlement->center_wx, settlement->center_wz);
    suit = sdk_settlement_evaluate_suitability_full(wg, settlement->center_wx, settlement->center_wz, &profile, &continental);

    if (settlement->zone_count == 0) {
        needs_foundation = 1;
    }

    if (needs_foundation && clamp_settlement_center_inside_wall_bounds(settlement)) {
        needs_foundation = 1;
        if (!sdk_worldgen_sample_column_ctx(wg, settlement->center_wx, settlement->center_wz, &profile)) return;
        sdk_settlement_sample_continental_approximation(wg, settlement->center_wx, settlement->center_wz, &continental);
        surface_y = sdk_worldgen_get_surface_y_ctx(wg, settlement->center_wx, settlement->center_wz);
        suit = sdk_settlement_evaluate_suitability_full(wg, settlement->center_wx, settlement->center_wz, &profile, &continental);
    }

    if (needs_foundation) {
        settlement->purpose = sdk_settlement_determine_purpose_for_type(
            wg,
            settlement->center_wx,
            settlement->center_wz,
            &profile,
            &continental,
            settlement->type);
        settlement->geographic_variant = sdk_settlement_determine_variant(
            wg, &profile, &continental, settlement->center_wx, settlement->center_wz, surface_y);
    }

    settlement->integrity = clamp01f(settlement->integrity);

    if (settlement->state < SETTLEMENT_STATE_PRISTINE || settlement->state > SETTLEMENT_STATE_REBUILDING) {
        settlement->state = SETTLEMENT_STATE_PRISTINE;
    }

    if (needs_foundation) {
        sdk_settlement_generate_foundation(settlement, surface_y);
    }

    settlement->water_access = continental.water_access;
    settlement->fertility = (float)profile.soil_fertility / 5.0f;
    settlement->flatness = continental.buildable_flatness;
    populate_settlement_summary(wg, settlement);
}

static void generate_settlement_buildings_in_chunk(SdkWorldGen* wg,
                                                   SdkChunk* chunk,
                                                   SettlementMetadata* settlement,
                                                   const SettlementLayout* layout)
{
    uint32_t i;
    SdkTerrainColumnProfile profile;
    int chunk_wx_min, chunk_wz_min, chunk_wx_max, chunk_wz_max;
    
    if (!wg || !chunk || !settlement) return;
    if (!layout) return;
    
    chunk_wx_min = chunk->cx * CHUNK_WIDTH;
    chunk_wz_min = chunk->cz * CHUNK_DEPTH;
    chunk_wx_max = chunk_wx_min + CHUNK_WIDTH;
    chunk_wz_max = chunk_wz_min + CHUNK_DEPTH;
    
    for (i = 0; i < layout->building_count; ++i) {
        BuildingPlacement* placement = &layout->buildings[i];
        const BuildingTemplate* template = sdk_settlement_get_building_template(placement->type);
        int bldg_wx_min, bldg_wz_min, bldg_wx_max, bldg_wz_max;
        
        if (!template) continue;
        
        bldg_wx_min = placement->wx;
        bldg_wz_min = placement->wz;
        bldg_wx_max = bldg_wx_min + placement->footprint_x;
        bldg_wz_max = bldg_wz_min + placement->footprint_z;
        
        if (bldg_wx_max < chunk_wx_min || bldg_wx_min >= chunk_wx_max) continue;
        if (bldg_wz_max < chunk_wz_min || bldg_wz_min >= chunk_wz_max) continue;

        if (!sdk_worldgen_sample_column_ctx(wg, placement->wx, placement->wz, &profile)) {
            continue;
        }
        
        sdk_settlement_generate_building(chunk, placement, template, settlement->integrity, profile.bedrock_province);
    }
}

void sdk_settlement_generate_for_chunk(SdkWorldGen* wg, SdkChunk* chunk, SuperchunkSettlementData* settlement_data)
{
    uint32_t i, z;
    
    if (!wg || !chunk || !settlement_data) return;
    
    for (i = 0; i < settlement_data->settlement_count; ++i) {
        SettlementMetadata* settlement = &settlement_data->settlements[i];
        int radius = settlement->radius;
        SettlementLayout* layout = sdk_settlement_generate_layout(wg, settlement);
        int chunk_near_settlement = chunk_intersects_settlement_radius(chunk,
                                                                       settlement->center_wx,
                                                                       settlement->center_wz,
                                                                       radius + CHUNK_WIDTH);
        
        if (chunk_near_settlement) {
            for (z = 0; z < settlement->zone_count; ++z) {
                sdk_settlement_prepare_zone_terrain(chunk, &settlement->zones[z]);
            }
        }

        sdk_settlement_generate_routes_for_chunk(wg, chunk, settlement_data, settlement, layout);

        if (chunk_near_settlement) {
            generate_settlement_buildings_in_chunk(wg, chunk, settlement, layout);
        }

        if (layout) {
            sdk_settlement_free_layout(layout);
        }
    }
}

static int find_settlement_cache_slot(SdkWorldGen* wg, int scx, int scz)
{
    SdkWorldGenImpl* impl;
    int i;
    
    if (!wg || !wg->impl) return -1;
    impl = (SdkWorldGenImpl*)wg->impl;
    
    for (i = 0; i < SDK_SETTLEMENT_CACHE_SLOTS; ++i) {
        if (impl->settlement_cache[i] &&
            impl->settlement_cache_scx[i] == scx &&
            impl->settlement_cache_scz[i] == scz) {
            return i;
        }
    }
    return -1;
}

static int find_lru_settlement_slot(SdkWorldGen* wg)
{
    SdkWorldGenImpl* impl;
    int i, oldest_slot;
    uint32_t oldest_stamp;
    
    if (!wg || !wg->impl) return 0;
    impl = (SdkWorldGenImpl*)wg->impl;
    
    oldest_slot = 0;
    oldest_stamp = impl->settlement_cache_stamps[0];
    
    for (i = 1; i < SDK_SETTLEMENT_CACHE_SLOTS; ++i) {
        if (impl->settlement_cache_stamps[i] < oldest_stamp) {
            oldest_stamp = impl->settlement_cache_stamps[i];
            oldest_slot = i;
        }
    }
    
    return oldest_slot;
}

static int point_in_zone(const BuildingZone* zone, int wx, int wz)
{
    if (!zone) return 0;
    return wx >= zone->center_wx - zone->radius_x &&
           wx <= zone->center_wx + zone->radius_x &&
           wz >= zone->center_wz - zone->radius_z &&
           wz <= zone->center_wz + zone->radius_z;
}

static int point_in_building(const BuildingPlacement* placement, int wx, int wz)
{
    if (!placement) return 0;
    return wx >= placement->wx &&
           wx < placement->wx + placement->footprint_x &&
           wz >= placement->wz &&
           wz < placement->wz + placement->footprint_z;
}

typedef struct {
    int wx, wz;
    float score;
    float priority_score;
    SettlementType type;
    SettlementPurpose purpose;
    GeographicVariant variant;
    float water_access;
    float fertility;
    float flatness;
} SettlementCandidate;

static uint32_t generate_settlement_id(uint32_t seed, int wx, int wz)
{
    return sdk_worldgen_hash2d(wx, wz, seed ^ 0x53455454u);
}

static uint16_t determine_radius(SettlementType type)
{
    switch (type) {
        case SETTLEMENT_TYPE_CITY: return 400;
        case SETTLEMENT_TYPE_TOWN: return 200;
        case SETTLEMENT_TYPE_VILLAGE: return 80;
        default: return 50;
    }
}

static int is_too_close_to_gate(int wx, int wz, SettlementType type, int scx, int scz)
{
    int exclusion_radius;
    int sc_base_x = scx * SDK_SUPERCHUNK_BLOCK_SPAN;
    int sc_base_z = scz * SDK_SUPERCHUNK_BLOCK_SPAN;
    int gate_center_x, gate_center_z;
    int dx, dz, dist_sq;
    
    if (type == SETTLEMENT_TYPE_CITY) {
        exclusion_radius = 64;
    } else if (type == SETTLEMENT_TYPE_TOWN) {
        exclusion_radius = 48;
    } else {
        exclusion_radius = 32;
    }
    
    gate_center_x = SDK_SUPERCHUNK_GATE_START_BLOCK + SDK_SUPERCHUNK_GATE_WIDTH_BLOCKS / 2;
    gate_center_z = SDK_SUPERCHUNK_GATE_START_BLOCK + SDK_SUPERCHUNK_GATE_WIDTH_BLOCKS / 2;
    
    dx = wx - (sc_base_x + gate_center_x);
    dz = wz - (sc_base_z + 0);
    dist_sq = dx * dx + dz * dz;
    if (dist_sq < exclusion_radius * exclusion_radius) return 1;
    
    dx = wx - (sc_base_x + gate_center_x);
    dz = wz - (sc_base_z + SDK_SUPERCHUNK_BLOCK_SPAN - 1);
    dist_sq = dx * dx + dz * dz;
    if (dist_sq < exclusion_radius * exclusion_radius) return 1;
    
    dx = wx - (sc_base_x + 0);
    dz = wz - (sc_base_z + gate_center_z);
    dist_sq = dx * dx + dz * dz;
    if (dist_sq < exclusion_radius * exclusion_radius) return 1;
    
    dx = wx - (sc_base_x + SDK_SUPERCHUNK_BLOCK_SPAN - 1);
    dz = wz - (sc_base_z + gate_center_z);
    dist_sq = dx * dx + dz * dz;
    if (dist_sq < exclusion_radius * exclusion_radius) return 1;
    
    return 0;
}

static int is_too_close_to_wall_bounds(int wx, int wz, SettlementType type)
{
    return !settlement_center_fits_inside_wall_bounds(wx, wz, determine_radius(type));
}

static int settlement_pair_spacing(SettlementType a, SettlementType b)
{
    if (a == SETTLEMENT_TYPE_CITY || b == SETTLEMENT_TYPE_CITY) return 2048;
    if (a == SETTLEMENT_TYPE_TOWN && b == SETTLEMENT_TYPE_TOWN) return 512;
    if ((a == SETTLEMENT_TYPE_TOWN && b == SETTLEMENT_TYPE_VILLAGE) ||
        (a == SETTLEMENT_TYPE_VILLAGE && b == SETTLEMENT_TYPE_TOWN)) {
        return 320;
    }
    return 256;
}

static int too_close_to_existing(int wx, int wz, SettlementType type, const SettlementMetadata* settlements, uint32_t count)
{
    uint32_t i;
    int min_dist;
    
    for (i = 0; i < count; ++i) {
        int dx = wx - settlements[i].center_wx;
        int dz = wz - settlements[i].center_wz;
        int dist_sq = dx * dx + dz * dz;
        
        min_dist = settlement_pair_spacing(type, settlements[i].type);
        
        if (dist_sq < min_dist * min_dist) {
            return 1;
        }
    }
    return 0;
}

static float settlement_candidate_competitiveness(SettlementType type, float score)
{
    if (type == SETTLEMENT_TYPE_TOWN) {
        return clamp01f((score - 0.45f) / 0.55f);
    }
    if (type == SETTLEMENT_TYPE_VILLAGE) {
        return clamp01f((score - 0.35f) / 0.65f);
    }
    return clamp01f((score - 0.70f) / 0.30f);
}

static void generate_superchunk_settlements(SdkWorldGen* wg, int scx, int scz, SuperchunkSettlementData* data)
{
    SettlementCandidate candidates[512];
    uint32_t candidate_count = 0;
    uint32_t candidate_capacity = (uint32_t)(sizeof(candidates) / sizeof(candidates[0]));
    int gx, gz;
    uint32_t rng_seed;
    int rejected_wall = 0;
    int rejected_gate = 0;
    int rejected_existing = 0;
    int rejected_underwater = 0;
    int rejected_threshold_village = 0;
    int rejected_threshold_town = 0;
    int rejected_cap = 0;
    int accepted = 0;
    int accepted_village = 0;
    int accepted_town = 0;
    uint32_t village_candidate_count = 0;
    uint32_t town_candidate_count = 0;
    static const int probe_offsets[4][2] = {
        { 16, 16 },
        { 16, 48 },
        { 48, 16 },
        { 48, 48 }
    };
    
    if (!wg || !data) return;
    
    memset(data, 0, sizeof(SuperchunkSettlementData));
    data->superchunk_x = scx;
    data->superchunk_z = scz;
    data->settlement_count = 0;
    
    rng_seed = sdk_worldgen_hash2d(scx, scz, ((SdkWorldGenImpl*)wg->impl)->seed ^ 0x53455454u);
    
    for (gx = 0; gx < 16; ++gx) {
        for (gz = 0; gz < 16; ++gz) {
            SettlementCandidate best_village;
            SettlementCandidate best_town;
            int have_village = 0;
            int have_town = 0;
            int probe_index;
            int cell_origin_x = scx * SDK_SUPERCHUNK_BLOCK_SPAN + gx * 64;
            int cell_origin_z = scz * SDK_SUPERCHUNK_BLOCK_SPAN + gz * 64;

            memset(&best_village, 0, sizeof(best_village));
            memset(&best_town, 0, sizeof(best_town));

            for (probe_index = 0; probe_index < 4; ++probe_index) {
                SdkTerrainColumnProfile profile;
                SdkContinentalSample continental;
                SettlementSuitability suit;
                GeographicVariant variant;
                int wx = cell_origin_x + probe_offsets[probe_index][0];
                int wz = cell_origin_z + probe_offsets[probe_index][1];
                int surface_y;

                if (!sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) continue;

                sdk_settlement_sample_continental_approximation(wg, wx, wz, &continental);
                surface_y = sdk_worldgen_get_surface_y_ctx(wg, wx, wz);
                variant = sdk_settlement_determine_variant(wg, &profile, &continental, wx, wz, surface_y);
                suit = sdk_settlement_evaluate_suitability_full(wg, wx, wz, &profile, &continental);

                if (!have_village || suit.village_score > best_village.score) {
                    have_village = 1;
                    best_village.wx = wx;
                    best_village.wz = wz;
                    best_village.score = suit.village_score;
                    best_village.priority_score = settlement_candidate_competitiveness(
                        SETTLEMENT_TYPE_VILLAGE,
                        suit.village_score);
                    best_village.type = SETTLEMENT_TYPE_VILLAGE;
                    best_village.purpose = sdk_settlement_determine_purpose_for_type(
                        wg,
                        wx,
                        wz,
                        &profile,
                        &continental,
                        SETTLEMENT_TYPE_VILLAGE);
                    best_village.variant = variant;
                    best_village.water_access = continental.water_access;
                    best_village.fertility = (float)profile.soil_fertility / 5.0f;
                    best_village.flatness = continental.buildable_flatness;
                }

                if (!have_town || suit.town_score > best_town.score) {
                    have_town = 1;
                    best_town.wx = wx;
                    best_town.wz = wz;
                    best_town.score = suit.town_score;
                    best_town.priority_score = settlement_candidate_competitiveness(
                        SETTLEMENT_TYPE_TOWN,
                        suit.town_score);
                    best_town.type = SETTLEMENT_TYPE_TOWN;
                    best_town.purpose = sdk_settlement_determine_purpose_for_type(
                        wg,
                        wx,
                        wz,
                        &profile,
                        &continental,
                        SETTLEMENT_TYPE_TOWN);
                    best_town.variant = variant;
                    best_town.water_access = continental.water_access;
                    best_town.fertility = (float)profile.soil_fertility / 5.0f;
                    best_town.flatness = continental.buildable_flatness;
                }
            }

            if (have_village && best_village.score >= 0.35f) {
                if (candidate_count < candidate_capacity) {
                    candidates[candidate_count++] = best_village;
                    village_candidate_count++;
                } else {
                    rejected_cap++;
                }
            } else {
                rejected_threshold_village++;
            }

            if (have_town && best_town.score >= 0.45f) {
                if (candidate_count < candidate_capacity) {
                    candidates[candidate_count++] = best_town;
                    town_candidate_count++;
                } else {
                    rejected_cap++;
                }
            } else {
                rejected_threshold_town++;
            }
        }
    }
    
    {
        uint32_t i, j;
        for (i = 0; i < candidate_count; ++i) {
            for (j = i + 1; j < candidate_count; ++j) {
                if (candidates[j].priority_score > candidates[i].priority_score) {
                    SettlementCandidate temp = candidates[i];
                    candidates[i] = candidates[j];
                    candidates[j] = temp;
                }
            }
        }
    }
    
    {
        uint32_t i;
        for (i = 0; i < candidate_count; ++i) {
            SettlementCandidate* c = &candidates[i];
            SdkTerrainColumnProfile candidate_profile;
            int is_underwater = 0;

            if (data->settlement_count >= SDK_MAX_SETTLEMENTS_PER_SUPERCHUNK) {
                rejected_cap += (int)(candidate_count - i);
                break;
            }
            
            /* Check if location is underwater */
            if (sdk_worldgen_sample_column_ctx(wg, c->wx, c->wz, &candidate_profile)) {
                if (candidate_profile.water_height > candidate_profile.surface_height + 2) {
                    is_underwater = 1;
                }
            }
            
            if (is_underwater) {
                rejected_underwater++;
                continue;
            }
            
            if (is_too_close_to_wall_bounds(c->wx, c->wz, c->type)) {
                rejected_wall++;
                continue;
            }

            if (is_too_close_to_gate(c->wx, c->wz, c->type, scx, scz)) {
                rejected_gate++;
                continue;
            }
            
            if (too_close_to_existing(c->wx, c->wz, c->type, data->settlements, data->settlement_count)) {
                rejected_existing++;
                continue;
            }
            
            {
                SettlementMetadata* settlement = &data->settlements[data->settlement_count++];
                settlement->settlement_id = generate_settlement_id(rng_seed, c->wx, c->wz);
                settlement->type = c->type;
                settlement->purpose = c->purpose;
                settlement->geographic_variant = c->variant;
                settlement->center_wx = c->wx;
                settlement->center_wz = c->wz;
                settlement->radius = determine_radius(c->type);
                settlement->state = SETTLEMENT_STATE_PRISTINE;
                settlement->integrity = 1.0f;
                settlement->water_access = c->water_access;
                settlement->fertility = c->fertility;
                settlement->flatness = c->flatness;
                settlement->population = 0;
                settlement->max_population = 0;
                settlement->food_production = 0.0f;
                settlement->resource_output = 0.0f;
                settlement->residential_count = 0;
                settlement->production_count = 0;
                settlement->storage_count = 0;
                settlement->defense_count = 0;
                settlement->last_damage_tick = 0;
                settlement->rebuild_start_tick = 0;
                settlement->zone_count = 0;
                
                {
                    int est_surface_y = sdk_worldgen_get_surface_y_ctx(wg, c->wx, c->wz);
                    sdk_settlement_generate_foundation(settlement, est_surface_y);
                }
                populate_settlement_summary(wg, settlement);
                compute_settlement_chunk_intersections(settlement, scx, scz);
                accepted++;
                if (c->type == SETTLEMENT_TYPE_TOWN) {
                    accepted_town++;
                } else if (c->type == SETTLEMENT_TYPE_VILLAGE) {
                    accepted_village++;
                }
            }
        }
    }
    
    build_chunk_settlement_index(data);
    
    if (g_sdk_settlement_diagnostics_enabled) {
        printf("Settlement generation SC(%d,%d): candidates(v=%u,t=%u,total=%u) accepted(v=%d,t=%d,total=%d) rejected_threshold(v=%d,t=%d) rejected_wall=%d rejected_gate=%d rejected_existing=%d rejected_underwater=%d rejected_cap=%d final_count=%u\n",
                 scx, scz,
                 village_candidate_count, town_candidate_count, candidate_count,
                 accepted_village, accepted_town, accepted,
                 rejected_threshold_village, rejected_threshold_town,
                 rejected_wall, rejected_gate, rejected_existing, rejected_underwater, rejected_cap,
                 data->settlement_count);
    }
}

SuperchunkSettlementData* sdk_settlement_get_or_create_data(SdkWorldGen* wg, int cx, int cz)
{
    SdkWorldGenImpl* impl;
    SuperchunkSettlementData* data;
    int scx, scz, slot;
    
    if (!wg || !wg->impl) {
        if (g_sdk_settlement_diagnostics_enabled) {
            printf("[SETTLEMENT] sdk_settlement_get_or_create_data: NULL wg or impl\n");
        }
        return NULL;
    }
    impl = (SdkWorldGenImpl*)wg->impl;
    
    scx = floor_div_superchunk(cx);
    scz = floor_div_superchunk(cz);
    
    if (g_sdk_settlement_diagnostics_enabled) {
        printf("[SETTLEMENT] sdk_settlement_get_or_create_data called for chunk(%d,%d) -> superchunk(%d,%d)\n", cx, cz, scx, scz);
    }
    
    slot = find_settlement_cache_slot(wg, scx, scz);
    if (slot >= 0) {
        if (g_sdk_settlement_diagnostics_enabled) {
            printf("[SETTLEMENT] Cache hit at slot %d, returning cached data\n", slot);
        }
        impl->settlement_cache_stamps[slot] = ++impl->stamp_clock;
        return impl->settlement_cache[slot];
    }
    
    if (g_sdk_settlement_diagnostics_enabled) {
        printf("[SETTLEMENT] Cache miss, allocating new data. world_path='%s'\n", impl->world_path);
    }
    
    data = (SuperchunkSettlementData*)malloc(sizeof(SuperchunkSettlementData));
    if (!data) {
        if (g_sdk_settlement_diagnostics_enabled) {
            printf("[SETTLEMENT] malloc failed!\n");
        }
        return NULL;
    }
    
    if (impl->world_path[0] != '\0' && sdk_settlement_load_superchunk(impl->world_path, scx, scz, data) && data->settlement_count > 0) {
        uint32_t i;
        int needs_migration = 0;
        if (g_sdk_settlement_diagnostics_enabled) {
            printf("[SETTLEMENT] Loaded %u settlements from save file\n", data->settlement_count);
        }
        for (i = 0; i < data->settlement_count; ++i) {
            sanitize_settlement_metadata(wg, &data->settlements[i]);
            if (data->settlements[i].chunk_count == 0) {
                needs_migration = 1;
            }
        }
        if (needs_migration) {
            for (i = 0; i < data->settlement_count; ++i) {
                if (data->settlements[i].chunk_count == 0) {
                    compute_settlement_chunk_intersections(&data->settlements[i], scx, scz);
                }
            }
            build_chunk_settlement_index(data);
        }
    } else {
        if (g_sdk_settlement_diagnostics_enabled) {
            printf("[SETTLEMENT] No save data found, calling generate_superchunk_settlements for SC(%d,%d)\n", scx, scz);
        }
        memset(data, 0, sizeof(SuperchunkSettlementData));
        generate_superchunk_settlements(wg, scx, scz, data);
    }
    
    slot = find_lru_settlement_slot(wg);
    if (impl->settlement_cache[slot]) {
        if (impl->world_path[0] != '\0') {
            sdk_settlement_save_superchunk(
                impl->world_path,
                impl->settlement_cache_scx[slot],
                impl->settlement_cache_scz[slot],
                impl->settlement_cache[slot]
            );
        }
        free(impl->settlement_cache[slot]);
    }
    
    impl->settlement_cache[slot] = data;
    impl->settlement_cache_scx[slot] = scx;
    impl->settlement_cache_scz[slot] = scz;
    impl->settlement_cache_stamps[slot] = ++impl->stamp_clock;
    
    return data;
}

void sdk_settlement_query_debug_at(SdkWorldGen* wg, int wx, int wz, SettlementDebugInfo* out_info)
{
    int base_cx, base_cz;
    int best_dist_sq = 0;
    int found = 0;
    int sc_dx, sc_dz;

    if (!out_info) return;
    memset(out_info, 0, sizeof(*out_info));
    out_info->zone_index = -1;
    out_info->building_index = -1;
    out_info->zone_type = (BuildingZoneType)-1;
    out_info->building_type = BUILDING_TYPE_NONE;

    if (!wg || !wg->impl) return;

    base_cx = sdk_world_to_chunk_x(wx);
    base_cz = sdk_world_to_chunk_z(wz);

    for (sc_dx = -1; sc_dx <= 1; ++sc_dx) {
        for (sc_dz = -1; sc_dz <= 1; ++sc_dz) {
            SdkSuperchunkCell cell;
            SuperchunkSettlementData* data =
                NULL;
            uint32_t i;

            sdk_superchunk_cell_from_index(floor_div_superchunk(base_cx) + sc_dx,
                                           floor_div_superchunk(base_cz) + sc_dz,
                                           &cell);
            data = sdk_settlement_get_or_create_data(wg, cell.interior_min_cx, cell.interior_min_cz);

            if (!data) continue;

            for (i = 0; i < data->settlement_count; ++i) {
                SettlementMetadata* settlement = &data->settlements[i];
                int dx = wx - settlement->center_wx;
                int dz = wz - settlement->center_wz;
                int dist_sq = dx * dx + dz * dz;
                SettlementLayout* layout;
                int zone_index = -1;
                int building_index = -1;
                uint32_t z;
                uint32_t b;

                if (dist_sq > settlement->radius * settlement->radius) {
                    continue;
                }
                if (found && dist_sq >= best_dist_sq) {
                    continue;
                }

                layout = sdk_settlement_generate_layout(wg, settlement);
                if (layout) {
                    for (b = 0; b < layout->building_count; ++b) {
                        if (point_in_building(&layout->buildings[b], wx, wz)) {
                            building_index = (int)b;
                            break;
                        }
                    }
                }

                for (z = 0; z < settlement->zone_count; ++z) {
                    if (point_in_zone(&settlement->zones[z], wx, wz)) {
                        zone_index = (int)z;
                        break;
                    }
                }

                found = 1;
                best_dist_sq = dist_sq;
                out_info->found = 1;
                out_info->settlement_id = settlement->settlement_id;
                out_info->type = settlement->type;
                out_info->purpose = settlement->purpose;
                out_info->state = settlement->state;
                out_info->geographic_variant = settlement->geographic_variant;
                out_info->center_wx = settlement->center_wx;
                out_info->center_wz = settlement->center_wz;
                out_info->radius = settlement->radius;
                out_info->population = settlement->population;
                out_info->max_population = settlement->max_population;
                out_info->integrity = settlement->integrity;
                out_info->water_access = settlement->water_access;
                out_info->fertility = settlement->fertility;
                out_info->defensibility = settlement->defensibility;
                out_info->flatness = settlement->flatness;
                out_info->food_production = settlement->food_production;
                out_info->resource_output = settlement->resource_output;
                out_info->in_zone = (uint8_t)(zone_index >= 0);
                out_info->zone_index = zone_index;
                out_info->zone_type = (BuildingZoneType)-1;
                out_info->zone_base_elevation = 0;
                if (zone_index >= 0) {
                    out_info->zone_type = settlement->zones[zone_index].zone_type;
                    out_info->zone_base_elevation = settlement->zones[zone_index].base_elevation;
                }
                out_info->in_building = (uint8_t)(building_index >= 0);
                out_info->building_index = building_index;
                out_info->building_type = BUILDING_TYPE_NONE;
                out_info->building_wx = 0;
                out_info->building_wz = 0;
                out_info->building_base_elevation = 0;
                out_info->building_footprint_x = 0;
                out_info->building_footprint_z = 0;
                out_info->building_height = 0;
                if (layout && building_index >= 0) {
                    const BuildingPlacement* placement = &layout->buildings[building_index];
                    out_info->building_type = placement->type;
                    out_info->building_wx = placement->wx;
                    out_info->building_wz = placement->wz;
                    out_info->building_base_elevation = placement->base_elevation;
                    out_info->building_footprint_x = placement->footprint_x;
                    out_info->building_footprint_z = placement->footprint_z;
                    out_info->building_height = placement->height;
                }

                sdk_settlement_free_layout(layout);
            }
        }
    }
}

SettlementSuitability sdk_settlement_evaluate_suitability(SdkWorldGen* wg, int wx, int wz)
{
    SettlementSuitability result = {0};
    SdkTerrainColumnProfile profile;
    SdkContinentalSample continental;
    
    if (!wg) return result;

    if (!sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) return result;
    sdk_settlement_sample_continental_approximation(wg, wx, wz, &continental);
    result = sdk_settlement_evaluate_suitability_full(wg, wx, wz, &profile, &continental);

    return result;
}

void sdk_settlement_apply_damage(SettlementMetadata* settlement, float damage_amount, uint32_t current_tick)
{
    if (!settlement) return;
    
    settlement->integrity -= damage_amount;
    if (settlement->integrity < 0.0f) settlement->integrity = 0.0f;
    
    settlement->last_damage_tick = current_tick;
    
    if (settlement->integrity >= 0.8f) {
        settlement->state = SETTLEMENT_STATE_PRISTINE;
    } else if (settlement->integrity >= 0.5f) {
        settlement->state = SETTLEMENT_STATE_DAMAGED;
    } else if (settlement->integrity >= 0.2f) {
        settlement->state = SETTLEMENT_STATE_RUINS;
    } else {
        settlement->state = SETTLEMENT_STATE_ABANDONED;
    }
}

uint32_t sdk_settlement_calculate_population(const SettlementMetadata* settlement, uint32_t residential_count)
{
    uint32_t base_pop = 0;
    
    if (!settlement) return 0;
    
    switch (settlement->type) {
        case SETTLEMENT_TYPE_VILLAGE: base_pop = residential_count * 4; break;
        case SETTLEMENT_TYPE_TOWN:    base_pop = residential_count * 8; break;
        case SETTLEMENT_TYPE_CITY:    base_pop = residential_count * 12; break;
        default: break;
    }
    
    return (uint32_t)(base_pop * settlement->integrity);
}

float sdk_settlement_calculate_food_production(const SettlementMetadata* settlement,
                                               float soil_fertility,
                                               uint32_t farm_count,
                                               uint32_t dock_count)
{
    float base_production, fertility_multiplier, integrity_multiplier;
    
    if (!settlement) return 0.0f;
    
    if (settlement->purpose != SETTLEMENT_PURPOSE_FARMING &&
        settlement->purpose != SETTLEMENT_PURPOSE_FISHING) {
        return 0.0f;
    }
    
    base_production = farm_count * 10.0f + dock_count * 8.0f;
    if (settlement->purpose == SETTLEMENT_PURPOSE_FISHING && dock_count > 0) {
        base_production += dock_count * 4.0f;
    }
    fertility_multiplier = soil_fertility;
    integrity_multiplier = settlement->integrity;
    
    return base_production * fertility_multiplier * integrity_multiplier;
}

int sdk_settlement_get_for_chunk(SdkWorldGen* wg, int cx, int cz,
                                 SettlementMetadata** out_settlements,
                                 int max_count)
{
    int scx = floor_div_superchunk(cx);
    int scz = floor_div_superchunk(cz);
    SdkSuperchunkCell cell;
    int local_cx;
    int local_cz;
    SuperchunkSettlementData* data;
    int count = 0;
    int i;

    if (!wg || !out_settlements || max_count <= 0) return 0;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    local_cx = cx - cell.interior_min_cx;
    local_cz = cz - cell.interior_min_cz;
    data = sdk_settlement_get_or_create_data(wg, cx, cz);
    if (!data) return 0;

    if (local_cx < 0 || local_cx >= 16 || local_cz < 0 || local_cz >= 16) return 0;

    for (i = 0; i < data->chunk_settlement_count[local_cx][local_cz] && count < max_count; i++) {
        int settlement_idx = data->chunk_settlement_indices[local_cx][local_cz][i];
        if (settlement_idx >= 0 && settlement_idx < (int)data->settlement_count) {
            out_settlements[count++] = &data->settlements[settlement_idx];
        }
    }
    
    return count;
}

void sdk_settlement_set_world_path(SdkWorldGen* wg, const char* path)
{
    SdkWorldGenImpl* impl;
    
    if (!wg || !wg->impl || !path) return;
    
    impl = (SdkWorldGenImpl*)wg->impl;
    strncpy(impl->world_path, path, sizeof(impl->world_path) - 1);
    impl->world_path[sizeof(impl->world_path) - 1] = '\0';
}

void sdk_settlement_flush_cache(SdkWorldGen* wg)
{
    SdkWorldGenImpl* impl;
    int i;
    
    if (!wg || !wg->impl) return;
    
    impl = (SdkWorldGenImpl*)wg->impl;
    
    if (impl->world_path[0] == '\0') return;
    
    for (i = 0; i < SDK_SETTLEMENT_CACHE_SLOTS; ++i) {
        if (impl->settlement_cache[i]) {
            sdk_settlement_save_superchunk(
                impl->world_path,
                impl->settlement_cache_scx[i],
                impl->settlement_cache_scz[i],
                impl->settlement_cache[i]
            );
        }
    }
}
