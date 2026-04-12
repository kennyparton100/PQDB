/**
 * sdk_settlement_runtime.h -- Loaded settlement runtime and NPC tasking.
 */
#ifndef NQLSDK_SETTLEMENT_RUNTIME_H
#define NQLSDK_SETTLEMENT_RUNTIME_H

#include "../sdk_settlement.h"
#include "../../Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../../../Entities/sdk_entity.h"
#include "../../Buildings/sdk_building_family.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE    64
#define SDK_SETTLEMENT_RUNTIME_MAX_BUILDINGS 96
#define SDK_SETTLEMENT_RUNTIME_MAX_RESIDENTS 16
#define SDK_SETTLEMENT_RUNTIME_MAX_MARKERS   8

typedef struct {
    BuildingType type;
    SdkBuildingFamily family;
    int32_t wx;
    int32_t wz;
    int16_t base_elevation;
    uint8_t rotation;
    uint8_t footprint_x;
    uint8_t footprint_z;
    uint8_t height;
    uint8_t marker_count;
    uint8_t prop_missing;
    uint8_t reserved0[3];
    char desired_prop_id[64];
    SdkBuildingRuntimeMarker markers[SDK_SETTLEMENT_RUNTIME_MAX_MARKERS];
} SdkSettlementBuildingInstance;

typedef struct {
    uint32_t resident_id;
    uint8_t active;
    uint8_t spawned;
    uint8_t role;
    uint8_t mob_type;
    uint8_t task_kind;
    uint8_t task_stage;
    uint8_t carrying_count;
    uint8_t reserved0;
    int16_t entity_index;
    int16_t home_building_index;
    int16_t work_building_index;
    int32_t target_wx;
    int32_t target_wy;
    int32_t target_wz;
    int32_t action_wx;
    int32_t action_wy;
    int32_t action_wz;
    int32_t cooldown_ticks;
    int32_t task_timer;
    ItemType carrying_item;
    SdkNpcInventorySlot inventory[SDK_NPC_INVENTORY_SLOTS];
} SdkSettlementResident;

typedef struct {
    uint32_t settlement_id;
    uint8_t in_use;
    uint8_t active;
    uint8_t purpose;
    uint8_t type;
    uint8_t supports_seeded;
    uint8_t reserved0[3];
    int32_t center_wx;
    int32_t center_wz;
    uint16_t radius;
    uint16_t building_count;
    uint16_t resident_count;
    uint16_t active_resident_count;
    int32_t bounds_min_wx;
    int32_t bounds_min_wz;
    int32_t bounds_max_wx;
    int32_t bounds_max_wz;
    uint16_t local_items[ITEM_TYPE_COUNT];
    SdkSettlementBuildingInstance buildings[SDK_SETTLEMENT_RUNTIME_MAX_BUILDINGS];
    SdkSettlementResident residents[SDK_SETTLEMENT_RUNTIME_MAX_RESIDENTS];
} SdkSettlementRuntime;

typedef struct {
    uint8_t found;
    uint8_t active;
    uint8_t resident_count;
    uint8_t active_resident_count;
    uint32_t settlement_id;
    char summary[128];
    char runtime[128];
    char resident[128];
    char assets[128];
} SdkSettlementRuntimeDebugInfo;

typedef struct {
    int known_settlements;
    int active_settlements;
    int active_residents;
    int last_scan_initialized;
    int block_mutations_last_tick;
    int scanned_loaded_chunks;
    int scan_interval_ms;
    int tick_interval_ms;
} SdkSettlementRuntimePerfCounters;

void sdk_settlement_runtime_init(void);
void sdk_settlement_runtime_shutdown(SdkEntityList* entities);
void sdk_settlement_runtime_tick_loaded(SdkWorldGen* wg,
                                        SdkChunkManager* cm,
                                        SdkEntityList* entities);
void sdk_settlement_runtime_query_debug(int wx, int wz, SdkSettlementRuntimeDebugInfo* out_info);
void sdk_settlement_runtime_get_perf_counters(SdkSettlementRuntimePerfCounters* out_counters);

void sdk_settlement_runtime_notify_chunk_loaded(int cx, int cz);
void sdk_settlement_runtime_notify_chunk_unloaded(int cx, int cz);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SETTLEMENT_RUNTIME_H */

