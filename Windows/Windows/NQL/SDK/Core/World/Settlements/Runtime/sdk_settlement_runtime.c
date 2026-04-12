/**
 * sdk_settlement_runtime.c -- Loaded settlement runtime and visible NPC loops.
 */
#include "../../../API/Internal/sdk_api_internal.h"
#include "../../../Items/sdk_item.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE_VISIBLE 8
#define SDK_SETTLEMENT_RUNTIME_BLOCK_MUTATIONS_PER_TICK 2
#define SDK_SETTLEMENT_RUNTIME_TICK_INTERVAL_MS 100u
#define SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE_SUPERCHUNKS 4

typedef struct {
    int32_t scx;
    int32_t scz;
    uint32_t loaded_chunk_count;
} ActiveSuperchunkEntry;

static SdkSettlementRuntime g_settlement_runtimes[SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE];
static uint8_t g_runtime_should_activate[SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE];
static int g_runtime_block_mutation_budget = 0;
static int g_runtime_block_mutations_last_tick = 0;
static ULONGLONG g_runtime_last_sim_tick_ms = 0u;
static ActiveSuperchunkEntry g_active_superchunks[SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE_SUPERCHUNKS];
static int g_active_superchunk_count = 0;

typedef struct {
    uint32_t settlement_id;
    const SettlementMetadata* settlement;
    int runtime_index;
    int distance_sq;
} SdkSettlementScanCandidate;

static const char* runtime_purpose_name(SettlementPurpose purpose)
{
    switch (purpose) {
        case SETTLEMENT_PURPOSE_FARMING:     return "FARMING";
        case SETTLEMENT_PURPOSE_FISHING:     return "FISHING";
        case SETTLEMENT_PURPOSE_MINING:      return "MINING";
        case SETTLEMENT_PURPOSE_LOGISTICS:   return "LOGISTICS";
        case SETTLEMENT_PURPOSE_PROCESSING:  return "PROCESSING";
        case SETTLEMENT_PURPOSE_PORT:        return "PORT";
        case SETTLEMENT_PURPOSE_CAPITAL:     return "CAPITAL";
        case SETTLEMENT_PURPOSE_FORTRESS:    return "FORTRESS";
        case SETTLEMENT_PURPOSE_HYDROCARBON: return "HYDROCARBON";
        case SETTLEMENT_PURPOSE_CEMENT:      return "CEMENT";
        case SETTLEMENT_PURPOSE_TIMBER:      return "TIMBER";
        default:                             return "UNKNOWN";
    }
}

static int runtime_floor_div(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

static int runtime_chunk_to_superchunk(int chunk_coord)
{
    return sdk_superchunk_floor_div_i(chunk_coord, SDK_SUPERCHUNK_WALL_PERIOD);
}

static int runtime_find_active_superchunk(int32_t scx, int32_t scz)
{
    int i;
    for (i = 0; i < g_active_superchunk_count; ++i) {
        if (g_active_superchunks[i].scx == scx && g_active_superchunks[i].scz == scz) {
            return i;
        }
    }
    return -1;
}

static int runtime_add_active_superchunk(int32_t scx, int32_t scz)
{
    int idx = runtime_find_active_superchunk(scx, scz);
    if (idx >= 0) {
        g_active_superchunks[idx].loaded_chunk_count++;
        return idx;
    }
    
    if (g_active_superchunk_count >= SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE_SUPERCHUNKS) {
        return -1;
    }
    
    idx = g_active_superchunk_count++;
    g_active_superchunks[idx].scx = scx;
    g_active_superchunks[idx].scz = scz;
    g_active_superchunks[idx].loaded_chunk_count = 1;
    return idx;
}

static void runtime_remove_active_superchunk(int32_t scx, int32_t scz)
{
    int idx = runtime_find_active_superchunk(scx, scz);
    if (idx < 0) return;
    
    if (g_active_superchunks[idx].loaded_chunk_count > 0) {
        g_active_superchunks[idx].loaded_chunk_count--;
    }
    
    if (g_active_superchunks[idx].loaded_chunk_count == 0) {
        if (idx < g_active_superchunk_count - 1) {
            g_active_superchunks[idx] = g_active_superchunks[g_active_superchunk_count - 1];
        }
        g_active_superchunk_count--;
    }
}

static void runtime_zero(SdkSettlementRuntime* runtime)
{
    if (!runtime) return;
    memset(runtime, 0, sizeof(*runtime));
}

static int runtime_loaded_chunk_count(const SdkChunkManager* cm)
{
    int count = 0;

    if (!cm) return 0;
    for (int slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        const SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at_const(cm, slot_index);
        if (slot && slot->occupied) {
            count++;
        }
    }
    return count;
}

static int runtime_known_settlement_count(void)
{
    int count = 0;

    for (int i = 0; i < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE; ++i) {
        if (g_settlement_runtimes[i].in_use) {
            count++;
        }
    }
    return count;
}

static int runtime_active_settlement_count(void)
{
    int count = 0;

    for (int i = 0; i < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE; ++i) {
        if (g_settlement_runtimes[i].in_use && g_settlement_runtimes[i].active) {
            count++;
        }
    }
    return count;
}

static int runtime_active_resident_count(void)
{
    int count = 0;

    for (int i = 0; i < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE; ++i) {
        if (!g_settlement_runtimes[i].in_use || !g_settlement_runtimes[i].active) continue;
        count += g_settlement_runtimes[i].active_resident_count;
    }
    return count;
}

static int runtime_find_slot(uint32_t settlement_id)
{
    int i;

    if (settlement_id == 0u) return -1;
    for (i = 0; i < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE; ++i) {
        if (g_settlement_runtimes[i].in_use &&
            g_settlement_runtimes[i].settlement_id == settlement_id) {
            return i;
        }
    }
    return -1;
}

static int runtime_find_free_slot(void)
{
    int i;

    for (i = 0; i < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE; ++i) {
        if (!g_settlement_runtimes[i].in_use) return i;
    }
    return -1;
}

static int runtime_find_entity_index(const SdkEntityList* entities,
                                     uint32_t settlement_id,
                                     uint32_t resident_id)
{
    int i;

    if (!entities || settlement_id == 0u || resident_id == 0u) return -1;
    for (i = 0; i < ENTITY_MAX; ++i) {
        const SdkEntity* entity = &entities->entities[i];
        if (!entity->active || entity->kind != ENTITY_MOB) continue;
        if (entity->settlement_id == settlement_id &&
            entity->resident_id == resident_id) {
            return i;
        }
    }
    return -1;
}

static int runtime_entity_index_matches(const SdkEntityList* entities,
                                        uint32_t settlement_id,
                                        uint32_t resident_id,
                                        int entity_index)
{
    const SdkEntity* entity;

    if (!entities || settlement_id == 0u || resident_id == 0u) return 0;
    if (entity_index < 0 || entity_index >= ENTITY_MAX) return 0;
    entity = &entities->entities[entity_index];
    return entity->active &&
           entity->kind == ENTITY_MOB &&
           entity->settlement_id == settlement_id &&
           entity->resident_id == resident_id;
}

static void runtime_consider_settlement_candidate(const SettlementMetadata* settlement,
                                                  int runtime_index,
                                                  int cam_wx,
                                                  int cam_wz,
                                                  SdkSettlementScanCandidate* best_candidates,
                                                  int candidate_count)
{
    int i;
    int dx;
    int dz;
    int dist_sq;
    int worst_index = -1;
    int worst_dist = -1;

    if (!settlement || !best_candidates || candidate_count <= 0) return;

    dx = settlement->center_wx - cam_wx;
    dz = settlement->center_wz - cam_wz;
    dist_sq = dx * dx + dz * dz;

    for (i = 0; i < candidate_count; ++i) {
        if (best_candidates[i].settlement_id == settlement->settlement_id) {
            if (runtime_index >= 0) {
                best_candidates[i].runtime_index = runtime_index;
            }
            if (dist_sq < best_candidates[i].distance_sq) {
                best_candidates[i].settlement = settlement;
                best_candidates[i].distance_sq = dist_sq;
            }
            return;
        }
    }

    for (i = 0; i < candidate_count; ++i) {
        if (best_candidates[i].settlement_id == 0u) {
            best_candidates[i].settlement_id = settlement->settlement_id;
            best_candidates[i].settlement = settlement;
            best_candidates[i].runtime_index = runtime_index;
            best_candidates[i].distance_sq = dist_sq;
            return;
        }
        if (best_candidates[i].distance_sq > worst_dist) {
            worst_dist = best_candidates[i].distance_sq;
            worst_index = i;
        }
    }

    if (worst_index >= 0 && dist_sq < worst_dist) {
        best_candidates[worst_index].settlement_id = settlement->settlement_id;
        best_candidates[worst_index].settlement = settlement;
        best_candidates[worst_index].runtime_index = runtime_index;
        best_candidates[worst_index].distance_sq = dist_sq;
    }
}

static int runtime_intersects_chunk_metadata(const SettlementMetadata* settlement, int cx, int cz)
{
    int chunk_min_x;
    int chunk_min_z;
    int chunk_max_x;
    int chunk_max_z;
    int settle_min_x;
    int settle_min_z;
    int settle_max_x;
    int settle_max_z;

    if (!settlement) return 0;

    chunk_min_x = cx * CHUNK_WIDTH;
    chunk_min_z = cz * CHUNK_DEPTH;
    chunk_max_x = chunk_min_x + CHUNK_WIDTH - 1;
    chunk_max_z = chunk_min_z + CHUNK_DEPTH - 1;
    settle_min_x = settlement->center_wx - (int)settlement->radius - 24;
    settle_min_z = settlement->center_wz - (int)settlement->radius - 24;
    settle_max_x = settlement->center_wx + (int)settlement->radius + 24;
    settle_max_z = settlement->center_wz + (int)settlement->radius + 24;

    return !(chunk_max_x < settle_min_x ||
             chunk_min_x > settle_max_x ||
             chunk_max_z < settle_min_z ||
             chunk_min_z > settle_max_z);
}

static int runtime_intersects_chunk(const SdkSettlementRuntime* runtime, int cx, int cz)
{
    int chunk_min_x;
    int chunk_min_z;
    int chunk_max_x;
    int chunk_max_z;

    if (!runtime || !runtime->in_use) return 0;

    chunk_min_x = cx * CHUNK_WIDTH;
    chunk_min_z = cz * CHUNK_DEPTH;
    chunk_max_x = chunk_min_x + CHUNK_WIDTH - 1;
    chunk_max_z = chunk_min_z + CHUNK_DEPTH - 1;

    return !(chunk_max_x < runtime->bounds_min_wx ||
             chunk_min_x > runtime->bounds_max_wx ||
             chunk_max_z < runtime->bounds_min_wz ||
             chunk_min_z > runtime->bounds_max_wz);
}

static void runtime_update_bounds(SdkSettlementRuntime* runtime)
{
    int i;

    if (!runtime) return;

    runtime->bounds_min_wx = runtime->center_wx - (int)runtime->radius - 24;
    runtime->bounds_min_wz = runtime->center_wz - (int)runtime->radius - 24;
    runtime->bounds_max_wx = runtime->center_wx + (int)runtime->radius + 24;
    runtime->bounds_max_wz = runtime->center_wz + (int)runtime->radius + 24;

    for (i = 0; i < (int)runtime->building_count; ++i) {
        const SdkSettlementBuildingInstance* building = &runtime->buildings[i];
        int min_x = building->wx - 8;
        int min_z = building->wz - 8;
        int max_x = building->wx + (int)building->footprint_x + 8;
        int max_z = building->wz + (int)building->footprint_z + 8;

        if (min_x < runtime->bounds_min_wx) runtime->bounds_min_wx = min_x;
        if (min_z < runtime->bounds_min_wz) runtime->bounds_min_wz = min_z;
        if (max_x > runtime->bounds_max_wx) runtime->bounds_max_wx = max_x;
        if (max_z > runtime->bounds_max_wz) runtime->bounds_max_wz = max_z;
    }
}

static int runtime_find_building_by_type(const SdkSettlementRuntime* runtime, BuildingType type)
{
    int i;

    if (!runtime) return -1;
    for (i = 0; i < (int)runtime->building_count; ++i) {
        if (runtime->buildings[i].type == type) return i;
    }
    return -1;
}

static int runtime_find_building_by_family(const SdkSettlementRuntime* runtime, SdkBuildingFamily family)
{
    int i;

    if (!runtime) return -1;
    for (i = 0; i < (int)runtime->building_count; ++i) {
        if (runtime->buildings[i].family == family) return i;
    }
    return -1;
}

static const SdkBuildingRuntimeMarker* runtime_find_marker(const SdkSettlementBuildingInstance* building,
                                                           SdkBuildingMarkerType marker_type)
{
    int i;

    if (!building) return NULL;
    for (i = 0; i < (int)building->marker_count; ++i) {
        if (building->markers[i].marker_type == (uint8_t)marker_type) {
            return &building->markers[i];
        }
    }
    return NULL;
}

static const SdkBuildingRuntimeMarker* runtime_find_first_marker(const SdkSettlementRuntime* runtime,
                                                                 int building_index,
                                                                 SdkBuildingMarkerType marker_type)
{
    if (!runtime || building_index < 0 || building_index >= (int)runtime->building_count) return NULL;
    return runtime_find_marker(&runtime->buildings[building_index], marker_type);
}

static MobType runtime_role_to_mob_type(SdkNpcRole role)
{
    switch (role) {
        case SDK_NPC_ROLE_COMMONER:   return MOB_COMMONER;
        case SDK_NPC_ROLE_BUILDER:    return MOB_BUILDER;
        case SDK_NPC_ROLE_BLACKSMITH: return MOB_BLACKSMITH;
        case SDK_NPC_ROLE_MINER:      return MOB_MINER;
        case SDK_NPC_ROLE_FOREMAN:    return MOB_FOREMAN;
        case SDK_NPC_ROLE_SOLDIER:    return MOB_SOLDIER;
        default:                      return MOB_COMMONER;
    }
}

static uint16_t* runtime_item_slot(SdkSettlementRuntime* runtime, ItemType item)
{
    if (!runtime || item <= ITEM_NONE || item >= ITEM_TYPE_COUNT) return NULL;
    return &runtime->local_items[(int)item];
}

static int runtime_has_item(const SdkSettlementRuntime* runtime, ItemType item, int count)
{
    if (!runtime || count <= 0 || item <= ITEM_NONE || item >= ITEM_TYPE_COUNT) return 0;
    return runtime->local_items[(int)item] >= (uint16_t)count;
}

static int runtime_take_item(SdkSettlementRuntime* runtime, ItemType item, int count)
{
    uint16_t* slot;

    if (!runtime || count <= 0) return 0;
    slot = runtime_item_slot(runtime, item);
    if (!slot || *slot < (uint16_t)count) return 0;
    *slot = (uint16_t)(*slot - count);
    return 1;
}

static void runtime_add_item(SdkSettlementRuntime* runtime, ItemType item, int count)
{
    uint16_t* slot;
    uint32_t total;

    if (!runtime || count <= 0) return;
    slot = runtime_item_slot(runtime, item);
    if (!slot) return;
    total = (uint32_t)(*slot) + (uint32_t)count;
    if (total > 65535u) total = 65535u;
    *slot = (uint16_t)total;
}

static int runtime_first_storage_building(const SdkSettlementRuntime* runtime)
{
    int index;

    if (!runtime) return -1;
    index = runtime_find_building_by_family(runtime, SDK_BUILDING_FAMILY_STORAGE_LOGISTICS);
    if (index >= 0) return index;
    index = runtime_find_building_by_type(runtime, BUILDING_TYPE_MARKET);
    if (index >= 0) return index;
    return runtime_find_building_by_type(runtime, BUILDING_TYPE_BARN);
}

static int runtime_first_home_building(const SdkSettlementRuntime* runtime)
{
    int index;

    if (!runtime) return -1;
    index = runtime_find_building_by_type(runtime, BUILDING_TYPE_HOUSE);
    if (index >= 0) return index;
    index = runtime_find_building_by_type(runtime, BUILDING_TYPE_HUT);
    if (index >= 0) return index;
    index = runtime_find_building_by_type(runtime, BUILDING_TYPE_MANOR);
    if (index >= 0) return index;
    return runtime_find_building_by_type(runtime, BUILDING_TYPE_BARRACKS);
}

static int runtime_first_work_building(const SdkSettlementRuntime* runtime, SdkNpcRole role)
{
    int index = -1;

    if (!runtime) return -1;

    switch (role) {
        case SDK_NPC_ROLE_BUILDER:
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_WORKSHOP);
            if (index >= 0) return index;
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_STOREHOUSE);
            if (index >= 0) return index;
            return runtime_first_storage_building(runtime);
        case SDK_NPC_ROLE_BLACKSMITH:
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_FORGE);
            if (index >= 0) return index;
            return runtime_find_building_by_type(runtime, BUILDING_TYPE_WORKSHOP);
        case SDK_NPC_ROLE_MINER:
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_WORKSHOP);
            if (index >= 0) return index;
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_FORGE);
            if (index >= 0) return index;
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_BARN);
            if (index >= 0) return index;
            return runtime_first_storage_building(runtime);
        case SDK_NPC_ROLE_FOREMAN:
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_MANOR);
            if (index >= 0) return index;
            return runtime_find_building_by_type(runtime, BUILDING_TYPE_MARKET);
        case SDK_NPC_ROLE_SOLDIER:
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_BARRACKS);
            if (index >= 0) return index;
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_WATCHTOWER);
            if (index >= 0) return index;
            return runtime_find_building_by_type(runtime, BUILDING_TYPE_WALL_SECTION);
        case SDK_NPC_ROLE_COMMONER:
        default:
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_MARKET);
            if (index >= 0) return index;
            index = runtime_find_building_by_type(runtime, BUILDING_TYPE_FARM);
            if (index >= 0) return index;
            return runtime_first_storage_building(runtime);
    }
}

static int runtime_add_resident(SdkSettlementRuntime* runtime,
                                SdkNpcRole role,
                                int home_building_index,
                                int work_building_index)
{
    SdkSettlementResident* resident;
    uint16_t next_id;

    if (!runtime) return -1;
    if (runtime->resident_count >= SDK_SETTLEMENT_RUNTIME_MAX_RESIDENTS) return -1;

    resident = &runtime->residents[runtime->resident_count];
    memset(resident, 0, sizeof(*resident));
    next_id = (uint16_t)(runtime->resident_count + 1);
    resident->resident_id = ((uint32_t)runtime->settlement_id << 8) ^ (uint32_t)next_id;
    if (resident->resident_id == 0u) resident->resident_id = (uint32_t)next_id;
    resident->active = 1;
    resident->spawned = 0;
    resident->role = (uint8_t)role;
    resident->mob_type = (uint8_t)runtime_role_to_mob_type(role);
    resident->task_kind = SDK_NPC_TASK_IDLE;
    resident->task_stage = 0;
    resident->entity_index = -1;
    resident->home_building_index = (int16_t)home_building_index;
    resident->work_building_index = (int16_t)work_building_index;
    runtime->resident_count++;
    return (int)(runtime->resident_count - 1);
}

static void runtime_seed_local_stock(SdkSettlementRuntime* runtime)
{
    int i;

    if (!runtime) return;
    for (i = 0; i < ITEM_TYPE_COUNT; ++i) {
        runtime->local_items[i] = 0;
    }

    runtime_add_item(runtime, ITEM_BLOCK_PLANKS, 24);
    runtime_add_item(runtime, ITEM_BLOCK_COBBLESTONE, 12);
    runtime_add_item(runtime, ITEM_BLOCK_COMPACTED_FILL, 18);
    runtime_add_item(runtime, ITEM_BERRIES, 10);
    runtime_add_item(runtime, ITEM_RAW_MEAT, 4);

    switch ((SettlementPurpose)runtime->purpose) {
        case SETTLEMENT_PURPOSE_MINING:
            runtime_add_item(runtime, ITEM_IRONSTONE, 18);
            runtime_add_item(runtime, ITEM_COAL, 16);
            runtime_add_item(runtime, ITEM_AGGREGATE, 10);
            break;
        case SETTLEMENT_PURPOSE_HYDROCARBON:
            runtime_add_item(runtime, ITEM_SULFUR, 12);
            runtime_add_item(runtime, ITEM_COAL, 8);
            runtime_add_item(runtime, ITEM_AGGREGATE, 8);
            break;
        case SETTLEMENT_PURPOSE_CEMENT:
            runtime_add_item(runtime, ITEM_LIMESTONE, 18);
            runtime_add_item(runtime, ITEM_AGGREGATE, 18);
            runtime_add_item(runtime, ITEM_CLAY, 10);
            break;
        case SETTLEMENT_PURPOSE_TIMBER:
            runtime_add_item(runtime, ITEM_BLOCK_LOG, 28);
            runtime_add_item(runtime, ITEM_BLOCK_PLANKS, 20);
            runtime_add_item(runtime, ITEM_STICK, 12);
            break;
        case SETTLEMENT_PURPOSE_LOGISTICS:
        case SETTLEMENT_PURPOSE_PORT:
            runtime_add_item(runtime, ITEM_BLOCK_PLANKS, 14);
            runtime_add_item(runtime, ITEM_COAL, 6);
            runtime_add_item(runtime, ITEM_STICK, 10);
            break;
        case SETTLEMENT_PURPOSE_PROCESSING:
            runtime_add_item(runtime, ITEM_IRONSTONE, 10);
            runtime_add_item(runtime, ITEM_COAL, 12);
            runtime_add_item(runtime, ITEM_LIMESTONE, 6);
            break;
        default:
            runtime_add_item(runtime, ITEM_BLOCK_LOG, 10);
            break;
    }
}

static void runtime_plan_residents(SdkSettlementRuntime* runtime)
{
    int home_index;
    int commoner_count;
    int i;

    if (!runtime) return;

    runtime->resident_count = 0;
    runtime->active_resident_count = 0;
    home_index = runtime_first_home_building(runtime);

    switch ((SettlementType)runtime->type) {
        case SETTLEMENT_TYPE_CITY:    commoner_count = 6; break;
        case SETTLEMENT_TYPE_TOWN:    commoner_count = 4; break;
        case SETTLEMENT_TYPE_VILLAGE: commoner_count = 2; break;
        default:                      commoner_count = 1; break;
    }
    commoner_count += (int)runtime->building_count / 8;
    if (commoner_count > 8) commoner_count = 8;
    if (commoner_count < 1) commoner_count = 1;

    for (i = 0; i < commoner_count; ++i) {
        runtime_add_resident(runtime,
                             SDK_NPC_ROLE_COMMONER,
                             home_index,
                             runtime_first_work_building(runtime, SDK_NPC_ROLE_COMMONER));
    }

    runtime_add_resident(runtime,
                         SDK_NPC_ROLE_BUILDER,
                         home_index,
                         runtime_first_work_building(runtime, SDK_NPC_ROLE_BUILDER));

    if (runtime_find_building_by_type(runtime, BUILDING_TYPE_FORGE) >= 0 ||
        runtime_find_building_by_type(runtime, BUILDING_TYPE_WORKSHOP) >= 0) {
        runtime_add_resident(runtime,
                             SDK_NPC_ROLE_BLACKSMITH,
                             home_index,
                             runtime_first_work_building(runtime, SDK_NPC_ROLE_BLACKSMITH));
    }

    if (runtime->purpose == SETTLEMENT_PURPOSE_MINING ||
        runtime->purpose == SETTLEMENT_PURPOSE_HYDROCARBON ||
        runtime->purpose == SETTLEMENT_PURPOSE_CEMENT ||
        runtime->purpose == SETTLEMENT_PURPOSE_TIMBER ||
        runtime_find_building_by_type(runtime, BUILDING_TYPE_BARN) >= 0) {
        runtime_add_resident(runtime,
                             SDK_NPC_ROLE_MINER,
                             home_index,
                             runtime_first_work_building(runtime, SDK_NPC_ROLE_MINER));
        if ((SettlementType)runtime->type >= SETTLEMENT_TYPE_TOWN) {
            runtime_add_resident(runtime,
                                 SDK_NPC_ROLE_MINER,
                                 home_index,
                                 runtime_first_work_building(runtime, SDK_NPC_ROLE_MINER));
        }
    }

    if ((SettlementType)runtime->type >= SETTLEMENT_TYPE_TOWN ||
        runtime_find_building_by_type(runtime, BUILDING_TYPE_MANOR) >= 0 ||
        runtime_find_building_by_type(runtime, BUILDING_TYPE_MARKET) >= 0) {
        runtime_add_resident(runtime,
                             SDK_NPC_ROLE_FOREMAN,
                             home_index,
                             runtime_first_work_building(runtime, SDK_NPC_ROLE_FOREMAN));
    }

    if (runtime_find_building_by_type(runtime, BUILDING_TYPE_BARRACKS) >= 0 ||
        runtime_find_building_by_type(runtime, BUILDING_TYPE_WATCHTOWER) >= 0 ||
        runtime_find_building_by_type(runtime, BUILDING_TYPE_WALL_SECTION) >= 0) {
        runtime_add_resident(runtime,
                             SDK_NPC_ROLE_SOLDIER,
                             runtime_find_building_by_type(runtime, BUILDING_TYPE_BARRACKS),
                             runtime_first_work_building(runtime, SDK_NPC_ROLE_SOLDIER));
    }
}

static void runtime_initialize_from_metadata(SdkWorldGen* wg,
                                             SdkSettlementRuntime* runtime,
                                             const SettlementMetadata* settlement)
{
    SettlementLayout* layout;
    uint32_t i;

    if (!runtime || !settlement) return;

    runtime_zero(runtime);
    runtime->in_use = 1;
    runtime->active = 0;
    runtime->settlement_id = settlement->settlement_id;
    runtime->purpose = (uint8_t)settlement->purpose;
    runtime->type = (uint8_t)settlement->type;
    runtime->center_wx = settlement->center_wx;
    runtime->center_wz = settlement->center_wz;
    runtime->radius = settlement->radius;

    layout = sdk_settlement_generate_layout(wg, settlement);
    if (!layout) {
        runtime_update_bounds(runtime);
        runtime_seed_local_stock(runtime);
        runtime_plan_residents(runtime);
        return;
    }

    for (i = 0; i < layout->building_count && runtime->building_count < SDK_SETTLEMENT_RUNTIME_MAX_BUILDINGS; ++i) {
        const BuildingPlacement* placement = &layout->buildings[i];
        SdkSettlementBuildingInstance* building = &runtime->buildings[runtime->building_count++];
        const char* prop_id;
        int prop_missing = 0;

        memset(building, 0, sizeof(*building));
        building->type = placement->type;
        building->family = sdk_building_family_for_type(placement->type);
        building->wx = placement->wx;
        building->wz = placement->wz;
        building->base_elevation = placement->base_elevation;
        building->rotation = placement->rotation;
        building->footprint_x = placement->footprint_x;
        building->footprint_z = placement->footprint_z;
        building->height = placement->height;
        building->marker_count = (uint8_t)sdk_building_compute_runtime_markers(
            placement,
            building->markers,
            SDK_SETTLEMENT_RUNTIME_MAX_MARKERS);
        prop_id = sdk_role_assets_resolve_prop(placement->type, &prop_missing);
        building->prop_missing = (uint8_t)(prop_missing ? 1 : 0);
        if (prop_id && prop_id[0]) {
            strcpy_s(building->desired_prop_id, sizeof(building->desired_prop_id), prop_id);
        }
    }

    sdk_settlement_free_layout(layout);
    runtime_update_bounds(runtime);
    runtime_seed_local_stock(runtime);
    runtime_plan_residents(runtime);
}

static void runtime_sync_resident_to_entity(const SdkSettlementResident* resident,
                                            const SdkSettlementRuntime* runtime,
                                            SdkEntity* entity)
{
    int slot_index;

    if (!resident || !runtime || !entity) return;

    sdk_entity_set_settlement_control(entity,
                                      runtime->settlement_id,
                                      resident->resident_id,
                                      (SdkNpcRole)resident->role);
    entity->mob_task = resident->task_kind;
    entity->task_stage = resident->task_stage;
    entity->task_timer = resident->task_timer;
    entity->task_cooldown = resident->cooldown_ticks;
    entity->home_building_index = resident->home_building_index;
    entity->work_building_index = resident->work_building_index;
    entity->inventory_selected = 0;
    for (slot_index = 0; slot_index < SDK_NPC_INVENTORY_SLOTS; ++slot_index) {
        entity->inventory[slot_index] = resident->inventory[slot_index];
    }
}

static void runtime_clear_carry_visual(SdkSettlementResident* resident)
{
    if (!resident) return;
    resident->carrying_item = ITEM_NONE;
    resident->carrying_count = 0;
}

static void runtime_sync_entity_to_resident(const SdkEntity* entity, SdkSettlementResident* resident)
{
    int slot_index;

    if (!entity || !resident) return;
    resident->entity_index = -1;
    resident->spawned = 0;
    if (!entity->active) return;

    resident->spawned = 1;
    resident->task_kind = entity->mob_task;
    resident->task_stage = (uint8_t)entity->task_stage;
    resident->task_timer = entity->task_timer;
    resident->cooldown_ticks = entity->task_cooldown;
    for (slot_index = 0; slot_index < SDK_NPC_INVENTORY_SLOTS; ++slot_index) {
        resident->inventory[slot_index] = entity->inventory[slot_index];
    }
}

static void runtime_spawn_resident(SdkSettlementRuntime* runtime,
                                   SdkSettlementResident* resident,
                                   SdkEntityList* entities)
{
    const SdkBuildingRuntimeMarker* marker = NULL;
    SdkEntity* entity;
    int spawn_wx;
    int spawn_wy;
    int spawn_wz;
    int home_index;
    int existing_index;

    if (!runtime || !resident || !entities || !resident->active) return;

    existing_index = resident->entity_index;
    if (!runtime_entity_index_matches(entities,
                                      runtime->settlement_id,
                                      resident->resident_id,
                                      existing_index)) {
        existing_index = runtime_find_entity_index(entities, runtime->settlement_id, resident->resident_id);
    }
    if (existing_index >= 0) {
        resident->entity_index = (int16_t)existing_index;
        resident->spawned = 1;
        runtime_sync_resident_to_entity(resident, runtime, &entities->entities[existing_index]);
        return;
    }

    home_index = resident->home_building_index;
    marker = runtime_find_first_marker(runtime, home_index, SDK_BUILDING_MARKER_SLEEP);
    if (!marker) marker = runtime_find_first_marker(runtime, home_index, SDK_BUILDING_MARKER_ENTRANCE);

    spawn_wx = marker ? marker->wx : runtime->center_wx;
    spawn_wy = marker ? marker->wy : (int)runtime->buildings[0].base_elevation + 1;
    spawn_wz = marker ? marker->wz : runtime->center_wz;

    entity = sdk_entity_spawn_mob(entities,
                                  (float)spawn_wx + 0.5f,
                                  (float)spawn_wy,
                                  (float)spawn_wz + 0.5f,
                                  (MobType)resident->mob_type);
    if (!entity) return;

    resident->entity_index = (int16_t)(entity - entities->entities);
    resident->spawned = 1;
    runtime_sync_resident_to_entity(resident, runtime, entity);
    sdk_entity_clear_controlled_target(entity, resident->task_kind);
}

static void runtime_deactivate(SdkSettlementRuntime* runtime, SdkEntityList* entities)
{
    int i;

    if (!runtime || !entities) return;
    runtime->active = 0;
    runtime->active_resident_count = 0;

    for (i = 0; i < ENTITY_MAX; ++i) {
        SdkEntity* entity = &entities->entities[i];
        if (!entity->active || entity->kind != ENTITY_MOB) continue;
        if (entity->settlement_id != runtime->settlement_id) continue;
        sdk_entity_remove(entities, i);
    }

    for (i = 0; i < (int)runtime->resident_count; ++i) {
        runtime->residents[i].spawned = 0;
        runtime->residents[i].entity_index = -1;
    }
}

static void runtime_seed_missing_support_blocks(SdkSettlementRuntime* runtime)
{
    int i;
    int m;
    int all_present = 1;

    if (!runtime) return;
    for (i = 0; i < (int)runtime->building_count; ++i) {
        SdkSettlementBuildingInstance* building = &runtime->buildings[i];
        for (m = 0; m < (int)building->marker_count; ++m) {
            const SdkBuildingRuntimeMarker* marker = &building->markers[m];

            if (marker->required_block == BLOCK_AIR ||
                marker->required_block == BLOCK_WATER) {
                continue;
            }
            if (get_block_at(marker->wx, marker->wy, marker->wz) != marker->required_block &&
                g_runtime_block_mutation_budget > 0) {
                set_block_at(marker->wx, marker->wy, marker->wz, marker->required_block);
                g_runtime_block_mutation_budget--;
                g_runtime_block_mutations_last_tick++;
            }
            if (get_block_at(marker->wx, marker->wy, marker->wz) != marker->required_block) {
                all_present = 0;
            }
        }
    }
    runtime->supports_seeded = all_present ? 1u : 0u;
}

static void runtime_ensure_support_state(SdkSettlementRuntime* runtime)
{
    int i;
    int m;

    if (!runtime) return;
    for (i = 0; i < (int)runtime->building_count; ++i) {
        const SdkSettlementBuildingInstance* building = &runtime->buildings[i];
        for (m = 0; m < (int)building->marker_count; ++m) {
            const SdkBuildingRuntimeMarker* marker = &building->markers[m];

            if (marker->marker_type == SDK_BUILDING_MARKER_STATION &&
                marker->required_block != BLOCK_AIR &&
                marker->required_block != BLOCK_WATER &&
                get_block_at(marker->wx, marker->wy, marker->wz) == marker->required_block) {
                station_ensure_state_public(marker->wx, marker->wy, marker->wz, marker->required_block);
            }
        }
    }
}

static void runtime_activate(SdkSettlementRuntime* runtime, SdkEntityList* entities)
{
    int i;

    if (!runtime || !entities) return;
    if (runtime->active) return;

    runtime->active = 1;
    runtime->active_resident_count = 0;
    if (!runtime->supports_seeded) {
        runtime_seed_missing_support_blocks(runtime);
    }
    runtime_ensure_support_state(runtime);

    for (i = 0; i < (int)runtime->resident_count; ++i) {
        runtime_spawn_resident(runtime, &runtime->residents[i], entities);
        if (runtime->residents[i].spawned) {
            runtime->active_resident_count++;
        }
    }
}

static void runtime_set_target_from_marker(SdkSettlementResident* resident,
                                           SdkEntity* entity,
                                           const SdkBuildingRuntimeMarker* marker,
                                           SdkNpcTaskKind task_kind)
{
    if (!resident || !entity || !marker) return;
    resident->target_wx = marker->wx;
    resident->target_wy = marker->wy;
    resident->target_wz = marker->wz;
    resident->action_wx = marker->wx;
    resident->action_wy = marker->wy;
    resident->action_wz = marker->wz;
    resident->task_kind = (uint8_t)task_kind;
    sdk_entity_set_controlled_target(entity, marker->wx, marker->wy, marker->wz, task_kind);
}

static void runtime_deposit_carried_item(SdkSettlementRuntime* runtime, SdkSettlementResident* resident)
{
    if (!runtime || !resident) return;
    if (resident->carrying_item == ITEM_NONE || resident->carrying_count == 0) return;
    runtime_add_item(runtime, resident->carrying_item, resident->carrying_count);
    runtime_clear_carry_visual(resident);
}

static void runtime_pickup_to_carry(SdkSettlementResident* resident, ItemType item, int count)
{
    if (!resident || item == ITEM_NONE || count <= 0) return;
    resident->carrying_item = item;
    resident->carrying_count = (uint8_t)api_clampi(count, 0, 255);
}

static void runtime_assign_idle_cycle(const SdkSettlementRuntime* runtime,
                                      SdkSettlementResident* resident,
                                      SdkEntity* entity)
{
    const SdkBuildingRuntimeMarker* marker = NULL;

    if (!runtime || !resident || !entity) return;

    if (resident->home_building_index >= 0) {
        marker = runtime_find_first_marker(runtime, resident->home_building_index, SDK_BUILDING_MARKER_SLEEP);
        if (!marker) {
            marker = runtime_find_first_marker(runtime, resident->home_building_index, SDK_BUILDING_MARKER_ENTRANCE);
        }
    }
    if (!marker && resident->work_building_index >= 0) {
        marker = runtime_find_first_marker(runtime, resident->work_building_index, SDK_BUILDING_MARKER_WORK);
        if (!marker) {
            marker = runtime_find_first_marker(runtime, resident->work_building_index, SDK_BUILDING_MARKER_ENTRANCE);
        }
    }

    if (marker) {
        runtime_set_target_from_marker(resident, entity, marker, SDK_NPC_TASK_IDLE);
    } else {
        sdk_entity_set_controlled_target(entity,
                                         runtime->center_wx,
                                         (runtime->building_count > 0)
                                            ? runtime->buildings[0].base_elevation + 1
                                            : 65,
                                         runtime->center_wz,
                                         SDK_NPC_TASK_IDLE);
    }
    resident->task_timer = 90 + ((int)resident->resident_id & 63);
}

static int runtime_repair_item_for_building(BuildingType type)
{
    switch (type) {
        case BUILDING_TYPE_HUT:
        case BUILDING_TYPE_HOUSE:
        case BUILDING_TYPE_FARM:
        case BUILDING_TYPE_BARN:
        case BUILDING_TYPE_WORKSHOP:
        case BUILDING_TYPE_STOREHOUSE:
        case BUILDING_TYPE_WAREHOUSE:
        case BUILDING_TYPE_SILO:
        case BUILDING_TYPE_DOCK:
            return ITEM_BLOCK_PLANKS;
        case BUILDING_TYPE_WELL:
        case BUILDING_TYPE_WATCHTOWER:
        case BUILDING_TYPE_BARRACKS:
        case BUILDING_TYPE_WALL_SECTION:
        case BUILDING_TYPE_MANOR:
        case BUILDING_TYPE_MARKET:
        case BUILDING_TYPE_FORGE:
        case BUILDING_TYPE_MILL:
        default:
            return ITEM_BLOCK_STONE_BRICKS;
    }
}

static int runtime_find_repair_target(const SdkSettlementRuntime* runtime,
                                      int building_index,
                                      int* out_wx,
                                      int* out_wy,
                                      int* out_wz,
                                      ItemType* out_item)
{
    const SdkSettlementBuildingInstance* building;
    int x;
    int z;
    int y;

    if (!runtime || building_index < 0 || building_index >= (int)runtime->building_count) return 0;
    building = &runtime->buildings[building_index];

    for (y = building->base_elevation + 1; y < building->base_elevation + (int)building->height; ++y) {
        for (z = building->wz; z < building->wz + (int)building->footprint_z; ++z) {
            for (x = building->wx; x < building->wx + (int)building->footprint_x; ++x) {
                int edge = (x == building->wx ||
                            x == building->wx + (int)building->footprint_x - 1 ||
                            z == building->wz ||
                            z == building->wz + (int)building->footprint_z - 1);
                if (!edge) continue;
                if (get_block_at(x, y, z) == BLOCK_AIR &&
                    get_block_at(x, y - 1, z) != BLOCK_AIR &&
                    get_block_at(x, y - 1, z) != BLOCK_WATER) {
                    if (out_wx) *out_wx = x;
                    if (out_wy) *out_wy = y;
                    if (out_wz) *out_wz = z;
                    if (out_item) *out_item = (ItemType)runtime_repair_item_for_building(building->type);
                    return 1;
                }
            }
        }
    }
    return 0;
}

static ItemType runtime_miner_output_for_purpose(const SdkSettlementRuntime* runtime)
{
    if (!runtime) return ITEM_BLOCK_STONE;

    switch ((SettlementPurpose)runtime->purpose) {
        case SETTLEMENT_PURPOSE_MINING:
            return runtime_has_item(runtime, ITEM_COAL, 1) ? ITEM_IRONSTONE : ITEM_COAL;
        case SETTLEMENT_PURPOSE_HYDROCARBON:
            return ITEM_SULFUR;
        case SETTLEMENT_PURPOSE_CEMENT:
            return ITEM_LIMESTONE;
        case SETTLEMENT_PURPOSE_TIMBER:
            return ITEM_BLOCK_LOG;
        default:
            return ITEM_AGGREGATE;
    }
}

static void runtime_tick_resident(SdkSettlementRuntime* runtime,
                                  SdkSettlementResident* resident,
                                  SdkEntityList* entities)
{
    SdkEntity* entity;
    int entity_index;

    if (!runtime || !resident || !entities || !resident->active) return;

    entity_index = resident->entity_index;
    if (!runtime_entity_index_matches(entities,
                                      runtime->settlement_id,
                                      resident->resident_id,
                                      entity_index)) {
        entity_index = runtime_find_entity_index(entities, runtime->settlement_id, resident->resident_id);
    }
    if (entity_index < 0) {
        resident->spawned = 0;
        resident->entity_index = -1;
        if (runtime->active) {
            runtime_spawn_resident(runtime, resident, entities);
        }
        return;
    }

    resident->spawned = 1;
    resident->entity_index = (int16_t)entity_index;
    entity = &entities->entities[entity_index];

    if (resident->cooldown_ticks > 0) resident->cooldown_ticks--;
    if (resident->task_timer > 0) resident->task_timer--;

    switch ((SdkNpcRole)resident->role) {
        case SDK_NPC_ROLE_BLACKSMITH:
        {
            const SdkBuildingRuntimeMarker* station_marker =
                runtime_find_first_marker(runtime, resident->work_building_index, SDK_BUILDING_MARKER_STATION);
            const SdkBuildingRuntimeMarker* storage_marker =
                runtime_find_first_marker(runtime, runtime_first_storage_building(runtime), SDK_BUILDING_MARKER_STORAGE);
            int station_index = -1;
            const StationState* state = NULL;

            if (!station_marker) {
                runtime_assign_idle_cycle(runtime, resident, entity);
                break;
            }

            if (resident->task_stage == 0) {
                if (storage_marker) {
                    runtime_set_target_from_marker(resident, entity, storage_marker, SDK_NPC_TASK_HAUL_LOCAL);
                }
                resident->task_stage = 1;
                break;
            }
            if (resident->task_stage == 1) {
                if (!sdk_entity_is_at_target(entity, 1.2f, 2.5f)) break;
                if (!runtime_take_item(runtime, ITEM_IRONSTONE, 1) ||
                    !runtime_take_item(runtime, ITEM_COAL, 1)) {
                    resident->cooldown_ticks = 180;
                    resident->task_stage = 0;
                    runtime_assign_idle_cycle(runtime, resident, entity);
                    break;
                }
                resident->inventory[0].item = ITEM_IRONSTONE;
                resident->inventory[0].count = 1;
                resident->inventory[1].item = ITEM_COAL;
                resident->inventory[1].count = 1;
                runtime_set_target_from_marker(resident, entity, station_marker, SDK_NPC_TASK_USE_STATION);
                resident->task_stage = 2;
                break;
            }
            if (resident->task_stage == 2) {
                if (!sdk_entity_is_at_target(entity, 1.2f, 2.5f)) break;
                station_index = station_ensure_state_public(station_marker->wx,
                                                           station_marker->wy,
                                                           station_marker->wz,
                                                           station_marker->required_block);
                if (station_index < 0) {
                    resident->task_stage = 0;
                    break;
                }
                station_npc_place_item(station_index, 0, ITEM_IRONSTONE);
                station_npc_place_item(station_index, 1, ITEM_COAL);
                resident->inventory[0].item = ITEM_NONE;
                resident->inventory[0].count = 0;
                resident->inventory[1].item = ITEM_NONE;
                resident->inventory[1].count = 0;
                resident->task_stage = 3;
                resident->task_timer = 320;
                break;
            }
            if (resident->task_stage == 3) {
                station_index = station_find_state_public(station_marker->wx, station_marker->wy, station_marker->wz);
                state = station_get_state_const(station_index);
                if (state && state->output_count > 0) {
                    ItemType out_item = ITEM_NONE;
                    if (station_npc_take_output(station_index, &out_item)) {
                        runtime_pickup_to_carry(resident, out_item, 1);
                        resident->task_stage = 4;
                        if (storage_marker) {
                            runtime_set_target_from_marker(resident, entity, storage_marker, SDK_NPC_TASK_HAUL_LOCAL);
                        }
                    }
                    break;
                }
                if (resident->task_timer <= 0) {
                    resident->task_stage = 0;
                    runtime_assign_idle_cycle(runtime, resident, entity);
                }
                break;
            }
            if (resident->task_stage == 4) {
                if (!storage_marker || !sdk_entity_is_at_target(entity, 1.2f, 2.5f)) break;
                runtime_deposit_carried_item(runtime, resident);
                resident->cooldown_ticks = 60;
                resident->task_stage = 0;
                runtime_assign_idle_cycle(runtime, resident, entity);
                break;
            }
            break;
        }
        case SDK_NPC_ROLE_MINER:
        {
            const SdkBuildingRuntimeMarker* work_marker =
                runtime_find_first_marker(runtime, resident->work_building_index, SDK_BUILDING_MARKER_WORK);
            const SdkBuildingRuntimeMarker* storage_marker =
                runtime_find_first_marker(runtime, runtime_first_storage_building(runtime), SDK_BUILDING_MARKER_STORAGE);

            if (!work_marker) {
                runtime_assign_idle_cycle(runtime, resident, entity);
                break;
            }
            if (resident->task_stage == 0) {
                runtime_set_target_from_marker(resident, entity, work_marker, SDK_NPC_TASK_GATHER);
                resident->task_stage = 1;
                break;
            }
            if (resident->task_stage == 1) {
                if (!sdk_entity_is_at_target(entity, 1.2f, 2.5f)) break;
                resident->task_timer = 180;
                resident->task_stage = 2;
                break;
            }
            if (resident->task_stage == 2) {
                if (resident->task_timer > 0) break;
                sdk_entity_spawn_item(entities,
                                      (float)work_marker->wx + 0.5f,
                                      (float)work_marker->wy + 0.5f,
                                      (float)work_marker->wz + 0.5f,
                                      runtime_miner_output_for_purpose(runtime));
                resident->task_stage = 3;
                break;
            }
            if (resident->task_stage == 3) {
                ItemType picked = ITEM_NONE;
                if (sdk_entity_try_pickup_near(entities, entity, &picked)) {
                    runtime_pickup_to_carry(resident, picked, 1);
                } else if (resident->carrying_item == ITEM_NONE) {
                    runtime_pickup_to_carry(resident, runtime_miner_output_for_purpose(runtime), 1);
                }
                if (resident->carrying_item != ITEM_NONE) {
                    resident->task_stage = 4;
                    if (storage_marker) {
                        runtime_set_target_from_marker(resident, entity, storage_marker, SDK_NPC_TASK_HAUL_LOCAL);
                    }
                }
                break;
            }
            if (resident->task_stage == 4) {
                if (!storage_marker || !sdk_entity_is_at_target(entity, 1.2f, 2.5f)) break;
                runtime_deposit_carried_item(runtime, resident);
                resident->cooldown_ticks = 45;
                resident->task_stage = 0;
                runtime_assign_idle_cycle(runtime, resident, entity);
                break;
            }
            break;
        }
        case SDK_NPC_ROLE_BUILDER:
        {
            int target_wx = 0;
            int target_wy = 0;
            int target_wz = 0;
            ItemType repair_item = ITEM_BLOCK_COMPACTED_FILL;

            if (resident->task_stage == 0) {
                if (!runtime_find_repair_target(runtime, resident->work_building_index,
                                                &target_wx, &target_wy, &target_wz, &repair_item)) {
                    resident->cooldown_ticks = 90;
                    runtime_assign_idle_cycle(runtime, resident, entity);
                    break;
                }
                resident->action_wx = target_wx;
                resident->action_wy = target_wy;
                resident->action_wz = target_wz;
                resident->inventory[0].item = repair_item;
                resident->inventory[0].count = 1;
                sdk_entity_set_controlled_target(entity, target_wx, target_wy, target_wz, SDK_NPC_TASK_REPAIR);
                resident->task_kind = SDK_NPC_TASK_REPAIR;
                resident->task_stage = 1;
                break;
            }
            if (resident->task_stage == 1) {
                HotbarEntry held;
                if (!sdk_entity_is_at_target(entity, 1.3f, 2.5f)) break;
                if (g_runtime_block_mutation_budget <= 0) break;
                memset(&held, 0, sizeof(held));
                held.item = resident->inventory[0].item;
                held.count = resident->inventory[0].count;
                if (!sdk_actor_place_block(&held,
                                           resident->action_wx, resident->action_wy, resident->action_wz,
                                           entity->px, entity->py, entity->pz,
                                           sdk_entity_mob_width(entity->mob_type) * 0.5f,
                                           sdk_entity_mob_height(entity->mob_type),
                                           0)) {
                    resident->cooldown_ticks = 30;
                    resident->task_stage = 0;
                    runtime_assign_idle_cycle(runtime, resident, entity);
                    break;
                }
                g_runtime_block_mutation_budget--;
                g_runtime_block_mutations_last_tick++;
                resident->inventory[0].item = ITEM_NONE;
                resident->inventory[0].count = 0;
                resident->cooldown_ticks = 90;
                resident->task_stage = 0;
                runtime_assign_idle_cycle(runtime, resident, entity);
                break;
            }
            break;
        }
        case SDK_NPC_ROLE_FOREMAN:
        case SDK_NPC_ROLE_SOLDIER:
        case SDK_NPC_ROLE_COMMONER:
        default:
        {
            if (resident->task_timer <= 0 || sdk_entity_is_at_target(entity, 1.0f, 2.5f)) {
                runtime_assign_idle_cycle(runtime, resident, entity);
            }
            break;
        }
    }

    runtime_sync_resident_to_entity(resident, runtime, entity);
}

static void runtime_update_from_active_superchunks(SdkWorldGen* wg, uint8_t* should_activate)
{
    int i;
    
    if (!wg || !should_activate) return;
    
    for (i = 0; i < g_active_superchunk_count; ++i) {
        SuperchunkSettlementData* data;
        uint32_t s;
        int32_t scx = g_active_superchunks[i].scx;
        int32_t scz = g_active_superchunks[i].scz;
        SdkSuperchunkCell cell;

        sdk_superchunk_cell_from_index(scx, scz, &cell);
        data = sdk_settlement_get_or_create_data(wg,
                                                 cell.interior_min_cx,
                                                 cell.interior_min_cz);
        if (!data) continue;
        
        for (s = 0; s < data->settlement_count; ++s) {
            const SettlementMetadata* settlement = &data->settlements[s];
            int runtime_index = runtime_find_slot(settlement->settlement_id);
            
            if (runtime_index < 0) {
                runtime_index = runtime_find_free_slot();
                if (runtime_index >= 0) {
                    runtime_initialize_from_metadata(wg, 
                                                    &g_settlement_runtimes[runtime_index], 
                                                    settlement);
                }
            }
            
            if (runtime_index >= 0 && runtime_index < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE) {
                should_activate[runtime_index] = 1u;
            }
        }
    }
}

void sdk_settlement_runtime_init(void)
{
    memset(g_settlement_runtimes, 0, sizeof(g_settlement_runtimes));
    memset(g_runtime_should_activate, 0, sizeof(g_runtime_should_activate));
    g_runtime_block_mutation_budget = 0;
    g_runtime_block_mutations_last_tick = 0;
    g_runtime_last_sim_tick_ms = 0u;
    memset(g_active_superchunks, 0, sizeof(g_active_superchunks));
    g_active_superchunk_count = 0;
}

void sdk_settlement_runtime_shutdown(SdkEntityList* entities)
{
    int i;

    for (i = 0; i < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE; ++i) {
        if (g_settlement_runtimes[i].in_use) {
            runtime_deactivate(&g_settlement_runtimes[i], entities);
            runtime_zero(&g_settlement_runtimes[i]);
        }
    }
    memset(g_runtime_should_activate, 0, sizeof(g_runtime_should_activate));
    g_runtime_last_sim_tick_ms = 0u;
    g_runtime_block_mutation_budget = 0;
    g_runtime_block_mutations_last_tick = 0;
    memset(g_active_superchunks, 0, sizeof(g_active_superchunks));
    g_active_superchunk_count = 0;
}

void sdk_settlement_runtime_tick_loaded(SdkWorldGen* wg,
                                        SdkChunkManager* cm,
                                        SdkEntityList* entities)
{
    int i;
    ULONGLONG now_ms;
    int run_sim_tick = 0;

    if (!wg || !cm || !entities) return;

    now_ms = GetTickCount64();

    PROF_ZONE_BEGIN(PROF_ZONE_SETTLEMENT_SCAN);
    memset(g_runtime_should_activate, 0, sizeof(g_runtime_should_activate));
    runtime_update_from_active_superchunks(wg, g_runtime_should_activate);
    PROF_ZONE_END(PROF_ZONE_SETTLEMENT_SCAN);

    if (g_runtime_last_sim_tick_ms == 0u ||
        now_ms - g_runtime_last_sim_tick_ms >= SDK_SETTLEMENT_RUNTIME_TICK_INTERVAL_MS) {
        run_sim_tick = 1;
    }

    g_runtime_block_mutation_budget = run_sim_tick ? SDK_SETTLEMENT_RUNTIME_BLOCK_MUTATIONS_PER_TICK : 0;
    g_runtime_block_mutations_last_tick = 0;

    PROF_ZONE_BEGIN(PROF_ZONE_SETTLEMENT_RUNTIME);
    for (i = 0; i < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE; ++i) {
        SdkSettlementRuntime* runtime = &g_settlement_runtimes[i];
        if (!runtime->in_use) continue;

        if (g_runtime_should_activate[i]) {
            if (!runtime->active) {
                runtime_activate(runtime, entities);
            }
        } else if (runtime->active) {
            runtime_deactivate(runtime, entities);
        }
    }

    for (i = 0; i < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE; ++i) {
        SdkSettlementRuntime* runtime = &g_settlement_runtimes[i];
        int resident_index;

        if (!runtime->in_use || !runtime->active) continue;
        if (!run_sim_tick) continue;
        runtime->active_resident_count = 0;

        for (resident_index = 0; resident_index < (int)runtime->resident_count; ++resident_index) {
            runtime_tick_resident(runtime, &runtime->residents[resident_index], entities);
            if (runtime->residents[resident_index].spawned) {
                runtime->active_resident_count++;
            }
        }
    }
    PROF_ZONE_END(PROF_ZONE_SETTLEMENT_RUNTIME);

    if (run_sim_tick) {
        g_runtime_last_sim_tick_ms = GetTickCount64();
    }
}

void sdk_settlement_runtime_query_debug(int wx, int wz, SdkSettlementRuntimeDebugInfo* out_info)
{
    int i;
    const SdkSettlementRuntime* runtime = NULL;
    const SdkSettlementResident* lead = NULL;

    if (!out_info) return;
    memset(out_info, 0, sizeof(*out_info));

    for (i = 0; i < SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE; ++i) {
        const SdkSettlementRuntime* candidate = &g_settlement_runtimes[i];
        int dx;
        int dz;

        if (!candidate->in_use) continue;
        dx = wx - candidate->center_wx;
        dz = wz - candidate->center_wz;
        if (dx * dx + dz * dz > (int)candidate->radius * (int)candidate->radius) continue;
        runtime = candidate;
        break;
    }

    if (!runtime) return;

    out_info->found = 1;
    out_info->active = runtime->active;
    out_info->resident_count = (uint8_t)runtime->resident_count;
    out_info->active_resident_count = (uint8_t)runtime->active_resident_count;
    out_info->settlement_id = runtime->settlement_id;

    for (i = 0; i < (int)runtime->resident_count; ++i) {
        if (runtime->residents[i].active) {
            lead = &runtime->residents[i];
            break;
        }
    }

    sprintf_s(out_info->summary, sizeof(out_info->summary),
              "LIVE %s  RES %u/%u  BLD %u  PURPOSE %s",
              runtime->active ? "YES" : "NO",
              (unsigned int)runtime->active_resident_count,
              (unsigned int)runtime->resident_count,
              (unsigned int)runtime->building_count,
              runtime_purpose_name((SettlementPurpose)runtime->purpose));
    sprintf_s(out_info->runtime, sizeof(out_info->runtime),
              "BOUNDS %d,%d -> %d,%d  STOCK LOG %u COAL %u IRONSTONE %u STONE %u",
              runtime->bounds_min_wx,
              runtime->bounds_min_wz,
              runtime->bounds_max_wx,
              runtime->bounds_max_wz,
              (unsigned int)runtime->local_items[ITEM_BLOCK_LOG],
              (unsigned int)runtime->local_items[ITEM_COAL],
              (unsigned int)runtime->local_items[ITEM_IRONSTONE],
              (unsigned int)runtime->local_items[ITEM_AGGREGATE]);

    if (lead) {
        int character_missing = 1;
        const char* character_asset = sdk_role_assets_resolve_character((SdkNpcRole)lead->role, &character_missing);
        sprintf_s(out_info->resident, sizeof(out_info->resident),
                  "LEAD %s  TASK %s  HOME %d  WORK %d  CARRY %s x%u",
                  sdk_entity_npc_role_name((SdkNpcRole)lead->role),
                  sdk_entity_npc_task_name((SdkNpcTaskKind)lead->task_kind),
                  (int)lead->home_building_index,
                  (int)lead->work_building_index,
                  sdk_item_get_name(lead->carrying_item),
                  (unsigned int)lead->carrying_count);
        if (runtime->building_count > 0) {
            const SdkSettlementBuildingInstance* building = &runtime->buildings[0];
            sprintf_s(out_info->assets, sizeof(out_info->assets),
                      "PROP %s %s  CHAR %s %s",
                      building->desired_prop_id[0] ? building->desired_prop_id : "shell_fallback",
                      building->prop_missing ? "MISSING" : "READY",
                      (character_asset && character_asset[0]) ? character_asset : "role_unbound",
                      character_missing ? "MISSING" : "READY");
        } else {
            sprintf_s(out_info->assets, sizeof(out_info->assets),
                      "CHAR %s %s  NO BUILDING INSTANCES",
                      (character_asset && character_asset[0]) ? character_asset : "role_unbound",
                      character_missing ? "MISSING" : "READY");
        }
    } else {
        strcpy_s(out_info->resident, sizeof(out_info->resident), "NO ACTIVE RESIDENT");
        if (runtime->building_count > 0) {
            const SdkSettlementBuildingInstance* building = &runtime->buildings[0];
            sprintf_s(out_info->assets, sizeof(out_info->assets),
                      "PROP %s  FAMILY %s  PROP_ASSET %s",
                      building->desired_prop_id[0] ? building->desired_prop_id : "shell_fallback",
                      sdk_building_family_name(building->family),
                      building->prop_missing ? "MISSING" : "READY");
        } else {
            strcpy_s(out_info->assets, sizeof(out_info->assets), "NO BUILDING INSTANCES");
        }
    }
}

void sdk_settlement_runtime_get_perf_counters(SdkSettlementRuntimePerfCounters* out_counters)
{
    if (!out_counters) return;

    memset(out_counters, 0, sizeof(*out_counters));
    out_counters->known_settlements = runtime_known_settlement_count();
    out_counters->active_settlements = runtime_active_settlement_count();
    out_counters->active_residents = runtime_active_resident_count();
    out_counters->last_scan_initialized = 0;
    out_counters->block_mutations_last_tick = g_runtime_block_mutations_last_tick;
    out_counters->scanned_loaded_chunks = 0;
    out_counters->scan_interval_ms = 0;
    out_counters->tick_interval_ms = (int)SDK_SETTLEMENT_RUNTIME_TICK_INTERVAL_MS;
}

void sdk_settlement_runtime_notify_chunk_loaded(int cx, int cz)
{
    int32_t scx = runtime_chunk_to_superchunk(cx);
    int32_t scz = runtime_chunk_to_superchunk(cz);
    runtime_add_active_superchunk(scx, scz);
}

void sdk_settlement_runtime_notify_chunk_unloaded(int cx, int cz)
{
    int32_t scx = runtime_chunk_to_superchunk(cx);
    int32_t scz = runtime_chunk_to_superchunk(cz);
    runtime_remove_active_superchunk(scx, scz);
}
