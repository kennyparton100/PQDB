#include "sdk_settlement_roads.h"
#include "../../Superchunks/Geometry/sdk_superchunk_geometry.h"
#include "../../Worldgen/Column/sdk_worldgen_column_internal.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SDK_SETTLEMENT_ROUTE_MAX_POINTS 4096

typedef enum {
    SDK_SETTLEMENT_ROUTE_PATH = 0,
    SDK_SETTLEMENT_ROUTE_ROAD,
    SDK_SETTLEMENT_ROUTE_CITY_ROAD
} SdkSettlementRouteSurface;

typedef enum {
    SDK_SETTLEMENT_ROUTE_ENDPOINT_UNKNOWN = 0,
    SDK_SETTLEMENT_ROUTE_ENDPOINT_HUB,
    SDK_SETTLEMENT_ROUTE_ENDPOINT_BUILDING,
    SDK_SETTLEMENT_ROUTE_ENDPOINT_GATE,
    SDK_SETTLEMENT_ROUTE_ENDPOINT_PEER_TOWN
} SdkSettlementRouteEndpointKind;

typedef struct {
    int wx;
    int wz;
    int y;
    uint8_t kind;
} SdkSettlementRouteEndpoint;

typedef struct {
    int wx;
    int wz;
    int surface_y;
    int target_y;
} SdkSettlementRoutePoint;

typedef struct {
    SdkSettlementRoutePoint points[SDK_SETTLEMENT_ROUTE_MAX_POINTS];
    int count;
    float score;
    int candidate_index;
} SdkSettlementRouteCandidate;

typedef struct {
    int cut_depth;
    int fill_depth;
} SdkSettlementRouteColumnStats;

typedef struct {
    int max_cut;
    int max_fill;
    int carved_columns;
} SdkSettlementRouteStampStats;

static int floor_div_settlement(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

static int floor_mod_settlement(int value, int denom)
{
    int div = floor_div_settlement(value, denom);
    return value - div * denom;
}

static int clampi_local(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int route_point_in_chunk(const SdkChunk* chunk, int wx, int wz, int* out_lx, int* out_lz)
{
    int chunk_wx0;
    int chunk_wz0;
    int lx;
    int lz;

    if (!chunk) return 0;
    chunk_wx0 = chunk->cx * CHUNK_WIDTH;
    chunk_wz0 = chunk->cz * CHUNK_DEPTH;
    lx = wx - chunk_wx0;
    lz = wz - chunk_wz0;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return 0;
    if (out_lx) *out_lx = lx;
    if (out_lz) *out_lz = lz;
    return 1;
}

static int route_chunk_intersects_bbox(const SdkChunk* chunk,
                                       int min_wx,
                                       int min_wz,
                                       int max_wx,
                                       int max_wz,
                                       int radius)
{
    int chunk_min_wx;
    int chunk_min_wz;
    int chunk_max_wx;
    int chunk_max_wz;

    if (!chunk) return 0;

    chunk_min_wx = chunk->cx * CHUNK_WIDTH;
    chunk_min_wz = chunk->cz * CHUNK_DEPTH;
    chunk_max_wx = chunk_min_wx + CHUNK_WIDTH - 1;
    chunk_max_wz = chunk_min_wz + CHUNK_DEPTH - 1;

    min_wx -= radius;
    min_wz -= radius;
    max_wx += radius;
    max_wz += radius;

    if (max_wx < chunk_min_wx || min_wx > chunk_max_wx) return 0;
    if (max_wz < chunk_min_wz || min_wz > chunk_max_wz) return 0;
    return 1;
}

static int building_contains_point(const BuildingPlacement* placement, int wx, int wz)
{
    if (!placement) return 0;
    return wx >= placement->wx &&
           wx < placement->wx + placement->footprint_x &&
           wz >= placement->wz &&
           wz < placement->wz + placement->footprint_z;
}

static int is_route_clear_block(BlockType block)
{
    switch (block) {
        case BLOCK_AIR:
        case BLOCK_WATER:
        case BLOCK_ICE:
        case BLOCK_SEA_ICE:
        case BLOCK_LOG:
        case BLOCK_LEAVES:
        case BLOCK_TALL_GRASS:
        case BLOCK_DRY_GRASS:
        case BLOCK_FERN:
        case BLOCK_SHRUB:
        case BLOCK_HEATHER:
        case BLOCK_CATTAILS:
        case BLOCK_WILDFLOWERS:
        case BLOCK_CACTUS:
        case BLOCK_SUCCULENT:
        case BLOCK_MOSS:
        case BLOCK_BERRY_BUSH:
        case BLOCK_REEDS:
            return 1;
        default:
            return 0;
    }
}

static int find_column_surface_y(const SdkChunk* chunk, int lx, int lz)
{
    int ly;

    if (!chunk) return -1;
    for (ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
        BlockType block = sdk_chunk_get_block((SdkChunk*)chunk, lx, ly, lz);
        if (!is_route_clear_block(block)) {
            return ly;
        }
    }
    return -1;
}

static SdkSettlementRouteEndpoint settlement_default_hub_from_metadata(const SettlementMetadata* settlement)
{
    SdkSettlementRouteEndpoint endpoint;

    memset(&endpoint, 0, sizeof(endpoint));
    if (!settlement) return endpoint;

    endpoint.wx = settlement->center_wx;
    endpoint.wz = settlement->center_wz;
    endpoint.y = (settlement->zone_count > 0) ? settlement->zones[0].base_elevation + 1 : 64;
    endpoint.kind = SDK_SETTLEMENT_ROUTE_ENDPOINT_HUB;

    if (settlement->type == SETTLEMENT_TYPE_VILLAGE) {
        endpoint.wx = settlement->center_wx + 1;
        endpoint.wz = settlement->center_wz + 3;
    } else if (settlement->type >= SETTLEMENT_TYPE_TOWN) {
        endpoint.wx = settlement->center_wx + 6;
        endpoint.wz = settlement->center_wz + 12;
    }

    if (endpoint.y < 1) endpoint.y = 1;
    if (endpoint.y >= CHUNK_HEIGHT) endpoint.y = CHUNK_HEIGHT - 1;
    return endpoint;
}

static SdkSettlementRouteEndpoint settlement_hub_endpoint(SdkWorldGen* wg,
                                                          const SettlementMetadata* settlement,
                                                          const SettlementLayout* layout)
{
    SdkSettlementRouteEndpoint endpoint;
    uint32_t i;

    endpoint = settlement_default_hub_from_metadata(settlement);

    if (layout) {
        for (i = 0; i < layout->building_count; ++i) {
            const BuildingPlacement* building = &layout->buildings[i];
            if ((layout->type == SETTLEMENT_TYPE_VILLAGE && building->type == BUILDING_TYPE_WELL) ||
                ((layout->type == SETTLEMENT_TYPE_TOWN || layout->type == SETTLEMENT_TYPE_CITY) &&
                 building->type == BUILDING_TYPE_MARKET)) {
                endpoint.wx = building->wx + building->footprint_x / 2;
                endpoint.wz = building->wz + building->footprint_z;
                endpoint.y = building->base_elevation + 1;
                endpoint.kind = SDK_SETTLEMENT_ROUTE_ENDPOINT_HUB;
                return endpoint;
            }
        }
    }

    if (wg && settlement) {
        endpoint.y = sdk_worldgen_get_surface_y_ctx(wg, endpoint.wx, endpoint.wz);
    }
    if (endpoint.y < 1) endpoint.y = 1;
    if (endpoint.y >= CHUNK_HEIGHT) endpoint.y = CHUNK_HEIGHT - 1;
    return endpoint;
}

static SdkSettlementRouteEndpoint building_entrance_endpoint(const SettlementMetadata* settlement,
                                                             const BuildingPlacement* building,
                                                             const SdkSettlementRouteEndpoint* hub)
{
    SdkSettlementRouteEndpoint endpoint;
    int center_x;
    int center_z;
    int dx;
    int dz;

    endpoint.wx = building ? building->wx : 0;
    endpoint.wz = building ? building->wz : 0;
    endpoint.y = building ? building->base_elevation + 1 : 64;
    endpoint.kind = SDK_SETTLEMENT_ROUTE_ENDPOINT_BUILDING;
    if (!building || !hub) return endpoint;

    center_x = building->wx + building->footprint_x / 2;
    center_z = building->wz + building->footprint_z / 2;
    dx = hub->wx - center_x;
    dz = hub->wz - center_z;

    if (abs(dx) >= abs(dz)) {
        if (dx >= 0) {
            endpoint.wx = building->wx + building->footprint_x;
            endpoint.wz = building->wz + building->footprint_z / 2;
        } else {
            endpoint.wx = building->wx - 1;
            endpoint.wz = building->wz + building->footprint_z / 2;
        }
    } else {
        if (dz >= 0) {
            endpoint.wx = building->wx + building->footprint_x / 2;
            endpoint.wz = building->wz + building->footprint_z;
        } else {
            endpoint.wx = building->wx + building->footprint_x / 2;
            endpoint.wz = building->wz - 1;
        }
    }

    if (settlement) {
        int min_x = settlement->center_wx - settlement->radius - 32;
        int max_x = settlement->center_wx + settlement->radius + 32;
        int min_z = settlement->center_wz - settlement->radius - 32;
        int max_z = settlement->center_wz + settlement->radius + 32;
        endpoint.wx = clampi_local(endpoint.wx, min_x, max_x);
        endpoint.wz = clampi_local(endpoint.wz, min_z, max_z);
    }
    return endpoint;
}

static int route_max_cut_for_surface(SdkSettlementRouteSurface surface)
{
    switch (surface) {
        case SDK_SETTLEMENT_ROUTE_CITY_ROAD: return 4;
        case SDK_SETTLEMENT_ROUTE_ROAD:      return 3;
        case SDK_SETTLEMENT_ROUTE_PATH:
        default:                             return 2;
    }
}

static int route_max_fill_for_surface(SdkSettlementRouteSurface surface)
{
    switch (surface) {
        case SDK_SETTLEMENT_ROUTE_CITY_ROAD: return 4;
        case SDK_SETTLEMENT_ROUTE_ROAD:      return 3;
        case SDK_SETTLEMENT_ROUTE_PATH:
        default:                             return 2;
    }
}

static int route_max_step_for_surface(SdkSettlementRouteSurface surface)
{
    switch (surface) {
        case SDK_SETTLEMENT_ROUTE_CITY_ROAD: return 3;
        case SDK_SETTLEMENT_ROUTE_ROAD:      return 2;
        case SDK_SETTLEMENT_ROUTE_PATH:
        default:                             return 2;
    }
}

static int route_ramp_span_for_surface(SdkSettlementRouteSurface surface)
{
    switch (surface) {
        case SDK_SETTLEMENT_ROUTE_CITY_ROAD: return 24;
        case SDK_SETTLEMENT_ROUTE_ROAD:      return 18;
        case SDK_SETTLEMENT_ROUTE_PATH:
        default:                             return 12;
    }
}

static int route_clearance_height_for_surface(SdkSettlementRouteSurface surface)
{
    switch (surface) {
        case SDK_SETTLEMENT_ROUTE_CITY_ROAD:
        case SDK_SETTLEMENT_ROUTE_ROAD:
            return 4;
        case SDK_SETTLEMENT_ROUTE_PATH:
        default:
            return 3;
    }
}

static int route_sample_surface_y(SdkWorldGen* wg, const SdkChunk* chunk, int wx, int wz)
{
    int lx;
    int lz;
    int surface_y;

    if (chunk && route_point_in_chunk(chunk, wx, wz, &lx, &lz)) {
        surface_y = find_column_surface_y(chunk, lx, lz);
        if (surface_y >= 0) return surface_y;
    }
    if (!wg) return -1;
    return sdk_worldgen_get_surface_y_ctx(wg, wx, wz);
}

static int route_surface_window_min(int surface_y, SdkSettlementRouteSurface surface)
{
    return surface_y - route_max_cut_for_surface(surface);
}

static int route_surface_window_max(int surface_y, SdkSettlementRouteSurface surface)
{
    return surface_y + route_max_fill_for_surface(surface);
}

static int route_is_natural_coarse_surface_block(BlockType block)
{
    return block == BLOCK_BEACH_GRAVEL ||
           block == BLOCK_COARSE_ALLUVIUM ||
           block == BLOCK_TALUS ||
           block == BLOCK_COLLUVIUM;
}

static int route_profile_prefers_soft_fill(const SdkTerrainColumnProfile* profile)
{
    if (!profile) return 0;
    if (profile->water_height > profile->surface_height) return 1;
    if ((profile->landform_flags & (SDK_LANDFORM_FLOODPLAIN |
                                    SDK_LANDFORM_LAKE_BASIN |
                                    SDK_LANDFORM_RIVER_CHANNEL)) != 0u) {
        return 1;
    }

    switch ((SdkBiomeEcology)profile->ecology) {
        case ECOLOGY_ESTUARY_WETLAND:
        case ECOLOGY_RIPARIAN_FOREST:
        case ECOLOGY_FLOODPLAIN_MEADOW:
        case ECOLOGY_FEN:
        case ECOLOGY_BOG:
        case ECOLOGY_MANGROVE_SWAMP:
            return 1;
        default:
            break;
    }

    switch ((SdkTerrainProvince)profile->terrain_province) {
        case TERRAIN_PROVINCE_ESTUARY_DELTA:
        case TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND:
        case TERRAIN_PROVINCE_PEAT_WETLAND:
            return 1;
        default:
            break;
    }

    return 0;
}

static int route_profile_prefers_sand_surface(const SdkTerrainColumnProfile* profile)
{
    if (!profile) return 0;

    switch ((SdkBiomeEcology)profile->ecology) {
        case ECOLOGY_HOT_DESERT:
        case ECOLOGY_DUNE_COAST:
        case ECOLOGY_SALT_DESERT:
        case ECOLOGY_SCRUB_BADLANDS:
            return 1;
        default:
            break;
    }

    switch ((SdkTerrainProvince)profile->terrain_province) {
        case TERRAIN_PROVINCE_OPEN_OCEAN:
        case TERRAIN_PROVINCE_CONTINENTAL_SHELF:
        case TERRAIN_PROVINCE_DYNAMIC_COAST:
        case TERRAIN_PROVINCE_ARID_FAN_STEPPE:
        case TERRAIN_PROVINCE_SALT_FLAT_PLAYA:
            return 1;
        default:
            break;
    }

    return 0;
}

static int route_profile_prefers_stone_surface(const SdkTerrainColumnProfile* profile)
{
    BlockType visual;

    if (!profile) return 0;
    visual = sdk_worldgen_visual_surface_block_for_profile(profile);
    if (sdk_block_get_tool_pref(visual) == BLOCK_TOOL_PICKAXE) {
        return 1;
    }

    switch ((SdkTerrainProvince)profile->terrain_province) {
        case TERRAIN_PROVINCE_CARBONATE_UPLAND:
        case TERRAIN_PROVINCE_HARDROCK_HIGHLAND:
        case TERRAIN_PROVINCE_UPLIFTED_PLATEAU:
        case TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT:
        case TERRAIN_PROVINCE_VOLCANIC_ARC:
        case TERRAIN_PROVINCE_BASALT_PLATEAU:
        case TERRAIN_PROVINCE_ALPINE_BELT:
        case TERRAIN_PROVINCE_BADLANDS_DISSECTED:
            return 1;
        default:
            break;
    }

    return 0;
}

static BlockType route_fallback_surface_block_for_profile(const SdkTerrainColumnProfile* profile)
{
    BlockType visual;

    if (!profile) return BLOCK_COMPACTED_FILL;
    visual = sdk_worldgen_visual_surface_block_for_profile(profile);
    if (route_is_natural_coarse_surface_block(visual)) {
        return visual;
    }
    if (route_profile_prefers_soft_fill(profile)) {
        return BLOCK_COMPACTED_FILL;
    }
    if (route_profile_prefers_sand_surface(profile)) {
        return (visual == BLOCK_MARINE_SAND) ? BLOCK_MARINE_SAND : BLOCK_SAND;
    }
    if (route_profile_prefers_stone_surface(profile)) {
        return BLOCK_CRUSHED_STONE;
    }
    return BLOCK_COMPACTED_FILL;
}

static BlockType route_select_local_surface_block(SdkWorldGen* wg,
                                                  const SdkChunk* chunk,
                                                  int wx,
                                                  int wz)
{
    static const int offsets[5][2] = {
        { 0, 0 }, { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 }
    };
    static const BlockType natural_blocks[4] = {
        BLOCK_BEACH_GRAVEL,
        BLOCK_COARSE_ALLUVIUM,
        BLOCK_TALUS,
        BLOCK_COLLUVIUM
    };
    int natural_counts[4] = { 0, 0, 0, 0 };
    SdkTerrainColumnProfile center_profile;
    int have_center_profile = 0;
    int best_index = -1;
    int best_count = 0;
    int i;

    (void)chunk;

    if (!wg) return BLOCK_COMPACTED_FILL;

    for (i = 0; i < 5; ++i) {
        SdkTerrainColumnProfile profile;
        BlockType visual;
        int j;

        if (!sdk_worldgen_sample_column_ctx(wg, wx + offsets[i][0], wz + offsets[i][1], &profile)) {
            continue;
        }
        if (i == 0) {
            center_profile = profile;
            have_center_profile = 1;
        }
        visual = sdk_worldgen_visual_surface_block_for_profile(&profile);
        for (j = 0; j < 4; ++j) {
            if (visual == natural_blocks[j]) {
                natural_counts[j]++;
                if (natural_counts[j] > best_count) {
                    best_count = natural_counts[j];
                    best_index = j;
                }
            }
        }
    }

    if (best_index >= 0) {
        return natural_blocks[best_index];
    }
    if (have_center_profile) {
        return route_fallback_surface_block_for_profile(&center_profile);
    }
    return BLOCK_COMPACTED_FILL;
}

static int route_endpoint_is_settlement_owned(const SdkSettlementRouteEndpoint* endpoint)
{
    if (!endpoint) return 0;

    return endpoint->kind == SDK_SETTLEMENT_ROUTE_ENDPOINT_HUB ||
           endpoint->kind == SDK_SETTLEMENT_ROUTE_ENDPOINT_BUILDING ||
           endpoint->kind == SDK_SETTLEMENT_ROUTE_ENDPOINT_PEER_TOWN;
}

static int route_endpoint_effective_anchor_y(SdkWorldGen* wg,
                                             const SdkChunk* chunk,
                                             const SdkSettlementRouteEndpoint* endpoint,
                                             SdkSettlementRouteSurface surface)
{
    int authored_y;
    int surface_y;
    int min_y;
    int max_y;

    if (!endpoint) return 64;

    authored_y = clampi_local(endpoint->y, 1, CHUNK_HEIGHT - 1);
    if (!route_endpoint_is_settlement_owned(endpoint)) {
        return authored_y;
    }

    surface_y = route_sample_surface_y(wg, chunk, endpoint->wx, endpoint->wz);
    if (surface_y < 0) {
        return authored_y;
    }

    min_y = surface_y - 1;
    max_y = surface_y + route_max_fill_for_surface(surface);
    if (min_y < 1) min_y = 1;
    if (max_y >= CHUNK_HEIGHT) max_y = CHUNK_HEIGHT - 1;
    if (min_y > max_y) {
        min_y = clampi_local(surface_y, 1, CHUNK_HEIGHT - 1);
        max_y = min_y;
    }
    return clampi_local(authored_y, min_y, max_y);
}

static int route_base_target_y(int start_y,
                               int end_y,
                               int point_index,
                               int point_count,
                               int surface_y,
                               SdkSettlementRouteSurface surface)
{
    int ramp_span;
    int dist_to_start;
    int dist_to_end;
    float start_weight;
    float end_weight;
    float surface_weight = 1.0f;
    float weighted_sum;
    float total_weight;

    if (point_count <= 0) return surface_y;
    if (point_index <= 0) return start_y;
    if (point_index >= point_count - 1) return end_y;

    ramp_span = route_ramp_span_for_surface(surface);
    dist_to_start = point_index;
    dist_to_end = (point_count - 1) - point_index;
    start_weight = (dist_to_start < ramp_span) ? (float)(ramp_span - dist_to_start) / (float)ramp_span : 0.0f;
    end_weight = (dist_to_end < ramp_span) ? (float)(ramp_span - dist_to_end) / (float)ramp_span : 0.0f;

    weighted_sum = (float)surface_y * surface_weight +
                   (float)start_y * (start_weight * 2.0f) +
                   (float)end_y * (end_weight * 2.0f);
    total_weight = surface_weight + start_weight * 2.0f + end_weight * 2.0f;
    return (int)lrintf(weighted_sum / total_weight);
}

static void clamp_route_candidate_targets(SdkWorldGen* wg,
                                          const SdkChunk* chunk,
                                          SdkSettlementRouteCandidate* candidate,
                                          const SdkSettlementRouteEndpoint* start,
                                          const SdkSettlementRouteEndpoint* end,
                                          SdkSettlementRouteSurface surface)
{
    int min_allowed[SDK_SETTLEMENT_ROUTE_MAX_POINTS];
    int max_allowed[SDK_SETTLEMENT_ROUTE_MAX_POINTS];
    int count;
    int step_limit;
    int start_anchor_y;
    int end_anchor_y;
    int arrival_span;
    int i;

    if (!candidate || !start || !end || candidate->count <= 0) return;

    count = candidate->count;
    step_limit = route_max_step_for_surface(surface);
    if (step_limit < 1) step_limit = 1;
    start_anchor_y = route_endpoint_effective_anchor_y(wg, chunk, start, surface);
    end_anchor_y = route_endpoint_effective_anchor_y(wg, chunk, end, surface);
    arrival_span = route_ramp_span_for_surface(surface) / 2;
    if (arrival_span < 4) arrival_span = 4;

    for (i = 0; i < count; ++i) {
        int surface_y = candidate->points[i].surface_y;
        if (i == 0) {
            min_allowed[i] = max_allowed[i] = start_anchor_y;
            candidate->points[i].target_y = start_anchor_y;
        } else if (i == count - 1) {
            min_allowed[i] = max_allowed[i] = end_anchor_y;
            candidate->points[i].target_y = end_anchor_y;
        } else {
            int dist_to_start = i;
            int dist_to_end = (count - 1) - i;
            int target_y = route_base_target_y(start_anchor_y, end_anchor_y, i, count, surface_y, surface);
            min_allowed[i] = route_surface_window_min(surface_y, surface);
            max_allowed[i] = route_surface_window_max(surface_y, surface);
            if (route_endpoint_is_settlement_owned(start) && dist_to_start < arrival_span) {
                int arrival_min = surface_y - 1;
                if (arrival_min > min_allowed[i]) min_allowed[i] = arrival_min;
            }
            if (route_endpoint_is_settlement_owned(end) && dist_to_end < arrival_span) {
                int arrival_min = surface_y - 1;
                if (arrival_min > min_allowed[i]) min_allowed[i] = arrival_min;
            }
            if (min_allowed[i] < 1) min_allowed[i] = 1;
            if (max_allowed[i] >= CHUNK_HEIGHT) max_allowed[i] = CHUNK_HEIGHT - 1;
            if (min_allowed[i] > max_allowed[i]) {
                int mid = clampi_local(surface_y, 1, CHUNK_HEIGHT - 1);
                min_allowed[i] = max_allowed[i] = mid;
            }
            candidate->points[i].target_y = clampi_local(target_y, min_allowed[i], max_allowed[i]);
        }
    }

    for (i = 1; i < count - 1; ++i) {
        int min_step = candidate->points[i - 1].target_y - step_limit;
        int max_step = candidate->points[i - 1].target_y + step_limit;
        int lo = (min_allowed[i] > min_step) ? min_allowed[i] : min_step;
        int hi = (max_allowed[i] < max_step) ? max_allowed[i] : max_step;
        if (lo <= hi) {
            candidate->points[i].target_y = clampi_local(candidate->points[i].target_y, lo, hi);
        }
    }

    for (i = count - 2; i > 0; --i) {
        int min_step = candidate->points[i + 1].target_y - step_limit;
        int max_step = candidate->points[i + 1].target_y + step_limit;
        int lo = (min_allowed[i] > min_step) ? min_allowed[i] : min_step;
        int hi = (max_allowed[i] < max_step) ? max_allowed[i] : max_step;
        if (lo <= hi) {
            candidate->points[i].target_y = clampi_local(candidate->points[i].target_y, lo, hi);
        }
    }
}

static int canonical_gate_endpoint_for_side(SdkWorldGen* wg,
                                            int super_origin_x,
                                            int super_origin_z,
                                            int side,
                                            SdkSettlementRouteEndpoint* out_endpoint)
{
    int gate_center;
    int wx;
    int wz;
    int gate_floor_y;

    if (!wg || !out_endpoint) return 0;

    gate_center = SDK_SUPERCHUNK_GATE_START_BLOCK + SDK_SUPERCHUNK_GATE_WIDTH_BLOCKS / 2;
    switch (side) {
        case 0:
            wx = super_origin_x + SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS;
            wz = super_origin_z + gate_center;
            break;
        case 1:
            wx = super_origin_x + gate_center;
            wz = super_origin_z + SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS;
            break;
        case 2:
            wx = super_origin_x + SDK_SUPERCHUNK_BLOCK_SPAN - 1;
            wz = super_origin_z + gate_center;
            break;
        case 3:
        default:
            wx = super_origin_x + gate_center;
            wz = super_origin_z + SDK_SUPERCHUNK_BLOCK_SPAN - 1;
            break;
    }

    gate_floor_y = sdk_superchunk_gate_floor_y_for_side_ctx(wg, super_origin_x, super_origin_z, side);

    out_endpoint->wx = wx;
    out_endpoint->wz = wz;
    out_endpoint->y = clampi_local(gate_floor_y, 1, CHUNK_HEIGHT - 1);
    out_endpoint->kind = SDK_SETTLEMENT_ROUTE_ENDPOINT_GATE;
    return 1;
}

static int resolve_settlement_hub_endpoint(SdkWorldGen* wg,
                                           const SettlementMetadata* settlement,
                                           SdkSettlementRouteEndpoint* out_endpoint)
{
    SettlementLayout* layout;
    SdkSettlementRouteEndpoint endpoint;

    if (!settlement || !out_endpoint) return 0;

    layout = wg ? sdk_settlement_generate_layout(wg, settlement) : NULL;
    endpoint = settlement_hub_endpoint(wg, settlement, layout);
    if (layout) sdk_settlement_free_layout(layout);
    *out_endpoint = endpoint;
    return 1;
}

static SdkSettlementRouteEndpoint nearest_gate_endpoint(SdkWorldGen* wg, const SettlementMetadata* settlement)
{
    SdkSettlementRouteEndpoint best;
    int super_origin_x;
    int super_origin_z;
    int i;
    float best_dist_sq = FLT_MAX;

    memset(&best, 0, sizeof(best));
    if (!settlement) return best;

    super_origin_x = floor_div_settlement(settlement->center_wx, SDK_SUPERCHUNK_BLOCK_SPAN) * SDK_SUPERCHUNK_BLOCK_SPAN;
    super_origin_z = floor_div_settlement(settlement->center_wz, SDK_SUPERCHUNK_BLOCK_SPAN) * SDK_SUPERCHUNK_BLOCK_SPAN;

    for (i = 0; i < 4; ++i) {
        SdkSettlementRouteEndpoint candidate;
        float dx;
        float dz;
        float dist_sq;

        if (!canonical_gate_endpoint_for_side(wg, super_origin_x, super_origin_z, i, &candidate)) {
            continue;
        }
        dx = (float)(candidate.wx - settlement->center_wx);
        dz = (float)(candidate.wz - settlement->center_wz);
        dist_sq = dx * dx + dz * dz;
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best = candidate;
        }
    }

    return best;
}

static int nearest_peer_town_endpoint(SdkWorldGen* wg,
                                      const SuperchunkSettlementData* settlement_data,
                                      const SettlementMetadata* settlement,
                                      SdkSettlementRouteEndpoint* out_endpoint,
                                      uint32_t* out_settlement_id)
{
    uint32_t i;
    float best_dist_sq = FLT_MAX;
    const SettlementMetadata* best_other = NULL;
    int found = 0;

    if (!wg || !settlement_data || !settlement || !out_endpoint) return 0;

    for (i = 0; i < settlement_data->settlement_count; ++i) {
        const SettlementMetadata* other = &settlement_data->settlements[i];
        float dx;
        float dz;
        float dist_sq;

        if (other->settlement_id == settlement->settlement_id) continue;
        if (other->type < SETTLEMENT_TYPE_TOWN) continue;
        dx = (float)(other->center_wx - settlement->center_wx);
        dz = (float)(other->center_wz - settlement->center_wz);
        dist_sq = dx * dx + dz * dz;
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best_other = other;
            if (out_settlement_id) *out_settlement_id = other->settlement_id;
            found = 1;
        }
    }

    if (found && best_other) {
        if (!resolve_settlement_hub_endpoint(wg, best_other, out_endpoint)) {
            *out_endpoint = settlement_default_hub_from_metadata(best_other);
        }
        out_endpoint->kind = SDK_SETTLEMENT_ROUTE_ENDPOINT_PEER_TOWN;
    }

    return found;
}

static int nearest_town_or_gate_endpoint(SdkWorldGen* wg,
                                         const SuperchunkSettlementData* settlement_data,
                                         const SettlementMetadata* settlement,
                                         SdkSettlementRouteEndpoint* out_endpoint)
{
    uint32_t i;
    float best_dist_sq = FLT_MAX;
    const SettlementMetadata* best_other = NULL;
    int found = 0;

    if (!wg || !settlement || !out_endpoint) return 0;

    if (settlement_data) {
        for (i = 0; i < settlement_data->settlement_count; ++i) {
            const SettlementMetadata* other = &settlement_data->settlements[i];
            float dx;
            float dz;
            float dist_sq;

            if (other->settlement_id == settlement->settlement_id) continue;
            if (other->type < SETTLEMENT_TYPE_TOWN) continue;
            dx = (float)(other->center_wx - settlement->center_wx);
            dz = (float)(other->center_wz - settlement->center_wz);
            dist_sq = dx * dx + dz * dz;
            if (dist_sq < best_dist_sq) {
                best_dist_sq = dist_sq;
                best_other = other;
                found = 1;
            }
        }
    }

    if (found && best_other) {
        if (!resolve_settlement_hub_endpoint(wg, best_other, out_endpoint)) {
            *out_endpoint = settlement_default_hub_from_metadata(best_other);
        }
        out_endpoint->kind = SDK_SETTLEMENT_ROUTE_ENDPOINT_PEER_TOWN;
    } else {
        *out_endpoint = nearest_gate_endpoint(wg, settlement);
        found = 1;
    }

    return found;
}

static int add_route_line_points(int x0,
                                 int z0,
                                 int x1,
                                 int z1,
                                 SdkSettlementRoutePoint* points,
                                 int start_index,
                                 int max_points,
                                 int skip_first)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dz = -abs(z1 - z0);
    int sz = z0 < z1 ? 1 : -1;
    int err = dx + dz;
    int x = x0;
    int z = z0;
    int index = start_index;
    int first = 1;

    for (;;) {
        if (!(skip_first && first)) {
            if (index >= max_points) return -1;
            points[index].wx = x;
            points[index].wz = z;
            points[index].surface_y = 0;
            points[index].target_y = 0;
            index++;
        }
        first = 0;
        if (x == x1 && z == z1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dz) {
                err += dz;
                x += sx;
            }
            if (e2 <= dx) {
                err += dx;
                z += sz;
            }
        }
    }

    return index;
}

static int build_candidate_points(const SdkSettlementRouteEndpoint* waypoints,
                                  int waypoint_count,
                                  SdkSettlementRouteCandidate* out_candidate)
{
    int i;
    int count = 0;

    if (!waypoints || waypoint_count < 2 || !out_candidate) return 0;
    memset(out_candidate, 0, sizeof(*out_candidate));

    for (i = 0; i < waypoint_count - 1; ++i) {
        count = add_route_line_points(waypoints[i].wx,
                                      waypoints[i].wz,
                                      waypoints[i + 1].wx,
                                      waypoints[i + 1].wz,
                                      out_candidate->points,
                                      count,
                                      SDK_SETTLEMENT_ROUTE_MAX_POINTS,
                                      i > 0);
        if (count < 0) return 0;
    }

    out_candidate->count = count;
    return count > 1;
}

static float ecology_route_penalty(SdkBiomeEcology ecology)
{
    switch (ecology) {
        case ECOLOGY_TEMPERATE_DECIDUOUS_FOREST:
        case ECOLOGY_TEMPERATE_CONIFER_FOREST:
        case ECOLOGY_BOREAL_TAIGA:
        case ECOLOGY_TROPICAL_SEASONAL_FOREST:
        case ECOLOGY_TROPICAL_RAINFOREST:
        case ECOLOGY_RIPARIAN_FOREST:
        case ECOLOGY_MANGROVE_SWAMP:
            return 3.5f;
        case ECOLOGY_BOG:
        case ECOLOGY_FEN:
        case ECOLOGY_ESTUARY_WETLAND:
            return 5.0f;
        case ECOLOGY_HOT_DESERT:
        case ECOLOGY_DUNE_COAST:
            return 2.5f;
        default:
            return 1.0f;
    }
}

static int point_violates_wall_band(int wx, int wz)
{
    int local_x = floor_mod_settlement(wx, SDK_SUPERCHUNK_BLOCK_SPAN);
    int local_z = floor_mod_settlement(wz, SDK_SUPERCHUNK_BLOCK_SPAN);
    int in_west_band = local_x < SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS;
    int in_north_band = local_z < SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS;
    int gate_x = local_x >= SDK_SUPERCHUNK_GATE_START_BLOCK && local_x <= SDK_SUPERCHUNK_GATE_END_BLOCK;
    int gate_z = local_z >= SDK_SUPERCHUNK_GATE_START_BLOCK && local_z <= SDK_SUPERCHUNK_GATE_END_BLOCK;

    if (in_west_band && !gate_z) return 1;
    if (in_north_band && !gate_x) return 1;
    return 0;
}

static int point_hits_layout_building(const SettlementLayout* layout,
                                      int wx,
                                      int wz,
                                      const SdkSettlementRouteEndpoint* start,
                                      const SdkSettlementRouteEndpoint* end)
{
    uint32_t i;

    if (!layout) return 0;
    for (i = 0; i < layout->building_count; ++i) {
        const BuildingPlacement* building = &layout->buildings[i];
        if (!building_contains_point(building, wx, wz)) continue;
        if (start && abs(wx - start->wx) <= 1 && abs(wz - start->wz) <= 1) continue;
        if (end && abs(wx - end->wx) <= 1 && abs(wz - end->wz) <= 1) continue;
        return 1;
    }
    return 0;
}

static float settlement_proximity_penalty(const SuperchunkSettlementData* settlement_data,
                                          const SettlementMetadata* source,
                                          int wx,
                                          int wz)
{
    uint32_t i;
    float penalty = 0.0f;

    if (!settlement_data || !source) return 0.0f;

    for (i = 0; i < settlement_data->settlement_count; ++i) {
        const SettlementMetadata* other = &settlement_data->settlements[i];
        int dx;
        int dz;
        int min_radius;

        if (other->settlement_id == source->settlement_id) continue;
        dx = wx - other->center_wx;
        dz = wz - other->center_wz;
        min_radius = (int)other->radius / 2;
        if (dx * dx + dz * dz < min_radius * min_radius) {
            penalty += 28.0f;
        }
    }

    return penalty;
}

static int score_route_candidate(SdkWorldGen* wg,
                                 const SdkChunk* chunk,
                                 const SuperchunkSettlementData* settlement_data,
                                 const SettlementMetadata* settlement,
                                 const SettlementLayout* layout,
                                 SdkSettlementRouteSurface surface,
                                 const SdkSettlementRouteEndpoint* start,
                                 const SdkSettlementRouteEndpoint* end,
                                 SdkSettlementRouteCandidate* candidate)
{
    int i;
    float score = 0.0f;

    if (!wg || !settlement || !candidate || candidate->count <= 1 || !start || !end) return 0;

    for (i = 0; i < candidate->count; ++i) {
        SdkTerrainColumnProfile profile;
        int surface_y;

        if (!sdk_worldgen_sample_column_ctx(wg, candidate->points[i].wx, candidate->points[i].wz, &profile)) {
            return 0;
        }
        if (point_violates_wall_band(candidate->points[i].wx, candidate->points[i].wz)) {
            return 0;
        }
        if (point_hits_layout_building(layout, candidate->points[i].wx, candidate->points[i].wz, start, end)) {
            return 0;
        }

        surface_y = route_sample_surface_y(wg, chunk, candidate->points[i].wx, candidate->points[i].wz);
        if (surface_y < 0) return 0;
        candidate->points[i].surface_y = surface_y;

        if (profile.water_height > profile.surface_height || profile.water_surface_class != SURFACE_WATER_NONE) {
            score += 34.0f;
        }
        if ((profile.landform_flags & SDK_LANDFORM_RIVER_CHANNEL) != 0u) score += 16.0f;
        if ((profile.landform_flags & SDK_LANDFORM_FLOODPLAIN) != 0u) score += 6.0f;
        if ((profile.landform_flags & SDK_LANDFORM_LAKE_BASIN) != 0u) score += 24.0f;
        if ((profile.landform_flags & SDK_LANDFORM_RAVINE) != 0u) score += 30.0f;
        if ((profile.landform_flags & SDK_LANDFORM_CALDERA) != 0u) score += 18.0f;

        switch (profile.terrain_province) {
            case TERRAIN_PROVINCE_OPEN_OCEAN:
            case TERRAIN_PROVINCE_CONTINENTAL_SHELF:
                score += 200.0f;
                break;
            case TERRAIN_PROVINCE_DYNAMIC_COAST:
            case TERRAIN_PROVINCE_ESTUARY_DELTA:
            case TERRAIN_PROVINCE_PEAT_WETLAND:
                score += 18.0f;
                break;
            default:
                break;
        }

        if (surface == SDK_SETTLEMENT_ROUTE_ROAD || surface == SDK_SETTLEMENT_ROUTE_CITY_ROAD) {
            if (profile.river_order >= 2u) score += 8.0f;
            if (profile.hydrocarbon_class == SDK_HYDROCARBON_NATURAL_GAS) score += 4.0f;
        }
    }

    clamp_route_candidate_targets(wg, chunk, candidate, start, end, surface);

    for (i = 0; i < candidate->count; ++i) {
        SdkTerrainColumnProfile profile;
        int cut_depth;
        int fill_depth;

        if (!sdk_worldgen_sample_column_ctx(wg, candidate->points[i].wx, candidate->points[i].wz, &profile)) {
            return 0;
        }

        cut_depth = candidate->points[i].surface_y - candidate->points[i].target_y;
        fill_depth = candidate->points[i].target_y - candidate->points[i].surface_y;
        if (cut_depth < 0) cut_depth = 0;
        if (fill_depth < 0) fill_depth = 0;

        score += 1.0f;
        score += (float)cut_depth * 6.0f;
        score += (float)fill_depth * 4.0f;
        if (i > 0) {
            score += (float)abs(candidate->points[i].target_y - candidate->points[i - 1].target_y) * 2.0f;
        }
        score += ecology_route_penalty(profile.ecology);
        score += settlement_proximity_penalty(settlement_data, settlement, candidate->points[i].wx, candidate->points[i].wz);
    }

    candidate->score = score;
    return 1;
}

static int choose_best_candidate(SdkWorldGen* wg,
                                 const SdkChunk* chunk,
                                 const SuperchunkSettlementData* settlement_data,
                                 const SettlementMetadata* settlement,
                                 const SettlementLayout* layout,
                                 SdkSettlementRouteSurface surface,
                                 const SdkSettlementRouteEndpoint* start,
                                 const SdkSettlementRouteEndpoint* end,
                                 SdkSettlementRouteCandidate* out_best)
{
    SdkSettlementRouteEndpoint waypoint_sets[5][4];
    int waypoint_counts[5];
    SdkSettlementRouteCandidate candidate;
    float best_score = FLT_MAX;
    int i;
    int mid_x;
    int mid_z;
    int found = 0;

    if (!wg || !settlement || !start || !end || !out_best) return 0;

    mid_x = (start->wx + end->wx) / 2;
    mid_z = (start->wz + end->wz) / 2;
    memset(waypoint_sets, 0, sizeof(waypoint_sets));
    memset(waypoint_counts, 0, sizeof(waypoint_counts));

    waypoint_sets[0][0] = *start;
    waypoint_sets[0][1] = *end;
    waypoint_counts[0] = 2;

    waypoint_sets[1][0] = *start;
    waypoint_sets[1][1].wx = end->wx;
    waypoint_sets[1][1].wz = start->wz;
    waypoint_sets[1][1].y = (start->y + end->y) / 2;
    waypoint_sets[1][2] = *end;
    waypoint_counts[1] = 3;

    waypoint_sets[2][0] = *start;
    waypoint_sets[2][1].wx = start->wx;
    waypoint_sets[2][1].wz = end->wz;
    waypoint_sets[2][1].y = (start->y + end->y) / 2;
    waypoint_sets[2][2] = *end;
    waypoint_counts[2] = 3;

    waypoint_sets[3][0] = *start;
    waypoint_sets[3][1].wx = mid_x;
    waypoint_sets[3][1].wz = start->wz;
    waypoint_sets[3][1].y = (start->y * 2 + end->y) / 3;
    waypoint_sets[3][2].wx = mid_x;
    waypoint_sets[3][2].wz = end->wz;
    waypoint_sets[3][2].y = (start->y + end->y * 2) / 3;
    waypoint_sets[3][3] = *end;
    waypoint_counts[3] = 4;

    waypoint_sets[4][0] = *start;
    waypoint_sets[4][1].wx = start->wx;
    waypoint_sets[4][1].wz = mid_z;
    waypoint_sets[4][1].y = (start->y * 2 + end->y) / 3;
    waypoint_sets[4][2].wx = end->wx;
    waypoint_sets[4][2].wz = mid_z;
    waypoint_sets[4][2].y = (start->y + end->y * 2) / 3;
    waypoint_sets[4][3] = *end;
    waypoint_counts[4] = 4;

    for (i = 0; i < 5; ++i) {
        if (!build_candidate_points(waypoint_sets[i], waypoint_counts[i], &candidate)) continue;
        if (!score_route_candidate(wg, chunk, settlement_data, settlement, layout, surface, start, end, &candidate)) continue;
        candidate.candidate_index = i;
        if (candidate.score < best_score) {
            best_score = candidate.score;
            *out_best = candidate;
            found = 1;
        }
    }

    return found;
}

static BlockType route_top_block_for_distance(SdkWorldGen* wg,
                                              const SdkChunk* chunk,
                                              int wx,
                                              int wz,
                                              SdkSettlementRouteSurface surface,
                                              int manhattan_dist)
{
    BlockType local_surface = route_select_local_surface_block(wg, chunk, wx, wz);

    switch (surface) {
        case SDK_SETTLEMENT_ROUTE_CITY_ROAD:
            if (manhattan_dist == 0) return BLOCK_CRUSHED_STONE;
            if (manhattan_dist == 1) return local_surface;
            return BLOCK_COBBLESTONE;
        case SDK_SETTLEMENT_ROUTE_ROAD:
            if (manhattan_dist <= 1) return BLOCK_CRUSHED_STONE;
            return local_surface;
        case SDK_SETTLEMENT_ROUTE_PATH:
        default:
            return local_surface;
    }
}

static int route_radius_for_surface(SdkSettlementRouteSurface surface)
{
    switch (surface) {
        case SDK_SETTLEMENT_ROUTE_CITY_ROAD: return 3;
        case SDK_SETTLEMENT_ROUTE_ROAD: return 2;
        case SDK_SETTLEMENT_ROUTE_PATH:
        default:
            return 1;
    }
}

static int route_base_layers_for_surface(SdkSettlementRouteSurface surface)
{
    switch (surface) {
        case SDK_SETTLEMENT_ROUTE_CITY_ROAD:
        case SDK_SETTLEMENT_ROUTE_ROAD:
            return 2;
        case SDK_SETTLEMENT_ROUTE_PATH:
        default:
            return 1;
    }
}

static SdkSettlementRouteColumnStats stamp_route_column(SdkWorldGen* wg,
                                                        SdkChunk* chunk,
                                                        int lx,
                                                        int lz,
                                                        int target_y,
                                                        SdkSettlementRouteSurface surface,
                                                        int manhattan_dist)
{
    SdkSettlementRouteColumnStats stats;
    int current_surface_y;
    int clear_top;
    int ly;
    int base_layers;
    int max_cut;
    int max_fill;
    int clearance_height;
    int fill_top;
    BlockType top_block;

    memset(&stats, 0, sizeof(stats));

    if (!chunk) return stats;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) return stats;
    if (target_y < 1) target_y = 1;
    if (target_y >= CHUNK_HEIGHT) target_y = CHUNK_HEIGHT - 1;

    current_surface_y = find_column_surface_y(chunk, lx, lz);
    if (current_surface_y < 0) current_surface_y = target_y - 1;

    max_cut = route_max_cut_for_surface(surface);
    max_fill = route_max_fill_for_surface(surface);
    clearance_height = route_clearance_height_for_surface(surface);
    if (target_y < current_surface_y - max_cut) {
        target_y = current_surface_y - max_cut;
    }
    if (target_y > current_surface_y + max_fill) {
        target_y = current_surface_y + max_fill;
    }
    if (target_y < 1) target_y = 1;
    if (target_y >= CHUNK_HEIGHT) target_y = CHUNK_HEIGHT - 1;

    stats.cut_depth = current_surface_y - target_y;
    stats.fill_depth = target_y - current_surface_y;
    if (stats.cut_depth < 0) stats.cut_depth = 0;
    if (stats.fill_depth < 0) stats.fill_depth = 0;

    clear_top = current_surface_y;
    if (clear_top < target_y + clearance_height) clear_top = target_y + clearance_height;
    if (clear_top >= CHUNK_HEIGHT) clear_top = CHUNK_HEIGHT - 1;

    for (ly = target_y + 1; ly <= clear_top; ++ly) {
        worldgen_set_block_fast(chunk, lx, ly, lz, BLOCK_AIR);
    }

    if (current_surface_y < target_y) {
        fill_top = target_y - 1;
        for (ly = current_surface_y + 1; ly <= fill_top; ++ly) {
            worldgen_set_block_fast(chunk, lx, ly, lz, BLOCK_COMPACTED_FILL);
        }
    }

    base_layers = route_base_layers_for_surface(surface);
    for (ly = target_y - base_layers; ly < target_y; ++ly) {
        if (ly >= 0) {
            worldgen_set_block_fast(chunk, lx, ly, lz, BLOCK_COMPACTED_FILL);
        }
    }

    top_block = route_top_block_for_distance(wg,
                                             chunk,
                                             chunk->cx * CHUNK_WIDTH + lx,
                                             chunk->cz * CHUNK_DEPTH + lz,
                                             surface,
                                             manhattan_dist);
    worldgen_set_block_fast(chunk, lx, target_y, lz, top_block);
    return stats;
}

static void stamp_route_candidate_in_chunk(SdkWorldGen* wg,
                                           SdkChunk* chunk,
                                           const SdkSettlementRouteCandidate* candidate,
                                           SdkSettlementRouteSurface surface,
                                           SdkSettlementRouteStampStats* out_stats)
{
    int carved_map[CHUNK_BLOCKS_PER_LAYER];
    int fill_map[CHUNK_BLOCKS_PER_LAYER];
    int radius;
    int chunk_wx0;
    int chunk_wz0;
    int i;

    if (out_stats) memset(out_stats, 0, sizeof(*out_stats));
    if (!chunk || !candidate || candidate->count <= 0) return;

    memset(carved_map, 0, sizeof(carved_map));
    memset(fill_map, 0, sizeof(fill_map));

    radius = route_radius_for_surface(surface);
    chunk_wx0 = chunk->cx * CHUNK_WIDTH;
    chunk_wz0 = chunk->cz * CHUNK_DEPTH;

    for (i = 0; i < candidate->count; ++i) {
        int dx;
        int dz;
        int wx = candidate->points[i].wx;
        int wz = candidate->points[i].wz;

        if (!route_chunk_intersects_bbox(chunk, wx, wz, wx, wz, radius)) continue;

        for (dx = -radius; dx <= radius; ++dx) {
            for (dz = -radius; dz <= radius; ++dz) {
                int manhattan = abs(dx) + abs(dz);
                int lx;
                int lz;
                int idx;
                SdkSettlementRouteColumnStats column_stats;

                if (manhattan > radius) continue;
                lx = (wx + dx) - chunk_wx0;
                lz = (wz + dz) - chunk_wz0;
                if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)lz >= CHUNK_DEPTH) continue;
                idx = lz * CHUNK_WIDTH + lx;
                column_stats = stamp_route_column(wg,
                                                 chunk,
                                                 lx,
                                                 lz,
                                                 candidate->points[i].target_y,
                                                 surface,
                                                 manhattan);
                if (column_stats.cut_depth > carved_map[idx]) carved_map[idx] = column_stats.cut_depth;
                if (column_stats.fill_depth > fill_map[idx]) fill_map[idx] = column_stats.fill_depth;
            }
        }
    }

    if (out_stats) {
        for (i = 0; i < CHUNK_BLOCKS_PER_LAYER; ++i) {
            if (carved_map[i] > out_stats->max_cut) out_stats->max_cut = carved_map[i];
            if (fill_map[i] > out_stats->max_fill) out_stats->max_fill = fill_map[i];
            if (carved_map[i] > 0) out_stats->carved_columns++;
        }
    }
}

static void maybe_stamp_route(SdkWorldGen* wg,
                              SdkChunk* chunk,
                              const SuperchunkSettlementData* settlement_data,
                              const SettlementMetadata* settlement,
                              const SettlementLayout* layout,
                              SdkSettlementRouteSurface surface,
                              const SdkSettlementRouteEndpoint* start,
                              const SdkSettlementRouteEndpoint* end)
{
    SdkSettlementRouteCandidate best;
    SdkSettlementRouteStampStats stamp_stats;
    int min_wx;
    int min_wz;
    int max_wx;
    int max_wz;
    int radius;

    if (!wg || !chunk || !settlement || !start || !end) return;

    min_wx = (start->wx < end->wx) ? start->wx : end->wx;
    min_wz = (start->wz < end->wz) ? start->wz : end->wz;
    max_wx = (start->wx > end->wx) ? start->wx : end->wx;
    max_wz = (start->wz > end->wz) ? start->wz : end->wz;
    radius = route_radius_for_surface(surface) + 4;

    if (!route_chunk_intersects_bbox(chunk, min_wx, min_wz, max_wx, max_wz, radius)) return;
    if (!choose_best_candidate(wg, chunk, settlement_data, settlement, layout, surface, start, end, &best)) return;

    stamp_route_candidate_in_chunk(wg, chunk, &best, surface, &stamp_stats);
    sdk_worldgen_debug_capture_note_route(surface,
                                          start->kind,
                                          end->kind,
                                          start->y,
                                          end->y,
                                          stamp_stats.max_cut,
                                          stamp_stats.max_fill,
                                          stamp_stats.carved_columns,
                                          best.candidate_index);
}

static void generate_internal_paths(SdkWorldGen* wg,
                                    SdkChunk* chunk,
                                    const SuperchunkSettlementData* settlement_data,
                                    const SettlementMetadata* settlement,
                                    const SettlementLayout* layout)
{
    SdkSettlementRouteEndpoint hub;
    uint32_t i;

    if (!wg || !chunk || !settlement || !layout) return;

    hub = settlement_hub_endpoint(wg, settlement, layout);
    for (i = 0; i < layout->building_count; ++i) {
        SdkSettlementRouteEndpoint entrance;
        const BuildingPlacement* building = &layout->buildings[i];
        if (abs((building->wx + building->footprint_x / 2) - hub.wx) <= 3 &&
            abs((building->wz + building->footprint_z / 2) - hub.wz) <= 3) {
            continue;
        }
        entrance = building_entrance_endpoint(settlement, building, &hub);
        maybe_stamp_route(wg,
                          chunk,
                          settlement_data,
                          settlement,
                          layout,
                          SDK_SETTLEMENT_ROUTE_PATH,
                          &entrance,
                          &hub);
    }
}

void sdk_settlement_generate_routes_for_chunk(SdkWorldGen* wg,
                                              SdkChunk* chunk,
                                              const SuperchunkSettlementData* settlement_data,
                                              const SettlementMetadata* settlement,
                                              const SettlementLayout* layout)
{
    SdkSettlementRouteEndpoint hub;

    if (!wg || !chunk || !settlement) return;

    hub = settlement_hub_endpoint(wg, settlement, layout);

    if (layout) {
        generate_internal_paths(wg, chunk, settlement_data, settlement, layout);
    }

    if (settlement->type >= SETTLEMENT_TYPE_TOWN) {
        SdkSettlementRouteEndpoint gate = nearest_gate_endpoint(wg, settlement);
        maybe_stamp_route(wg,
                          chunk,
                          settlement_data,
                          settlement,
                          layout,
                          (settlement->type == SETTLEMENT_TYPE_CITY) ? SDK_SETTLEMENT_ROUTE_CITY_ROAD : SDK_SETTLEMENT_ROUTE_ROAD,
                          &hub,
                          &gate);

        if (settlement_data) {
            SdkSettlementRouteEndpoint other_endpoint;
            uint32_t other_id = 0;
            if (nearest_peer_town_endpoint(wg, settlement_data, settlement, &other_endpoint, &other_id) &&
                settlement->settlement_id < other_id) {
                maybe_stamp_route(wg,
                                  chunk,
                                  settlement_data,
                                  settlement,
                                  layout,
                                  SDK_SETTLEMENT_ROUTE_ROAD,
                                  &hub,
                                  &other_endpoint);
            }
        }
    } else {
        SdkSettlementRouteEndpoint connector;
        if (nearest_town_or_gate_endpoint(wg, settlement_data, settlement, &connector)) {
            maybe_stamp_route(wg,
                              chunk,
                              settlement_data,
                              settlement,
                              layout,
                              SDK_SETTLEMENT_ROUTE_PATH,
                              &hub,
                              &connector);
        }
    }
}
