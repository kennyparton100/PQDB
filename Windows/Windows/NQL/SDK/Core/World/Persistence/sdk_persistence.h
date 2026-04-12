/**
 * sdk_persistence.h -- Minimal local world/player persistence.
 */
#ifndef NQLSDK_PERSISTENCE_H
#define NQLSDK_PERSISTENCE_H

#include "../Chunks/sdk_chunk.h"
#include "../../sdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_PERSISTENCE_HOTBAR_SLOTS 10
#define SDK_PERSISTENCE_WORLDGEN_REVISION 13

typedef struct {
    int      wx;
    int      wy;
    int      wz;
    BlockType block_type;
    ItemType input_item;
    int      input_count;
    ItemType fuel_item;
    int      fuel_count;
    ItemType output_item;
    int      output_count;
    int      progress;
    int      burn_remaining;
} SdkPersistedStationState;

typedef struct {
    float    position[3];
    float    spawn[3];
    float    cam_yaw;
    float    cam_pitch;
    int      health;
    int      hunger;
    int      world_time;
    int      hotbar_selected;
    int      chunk_grid_size;
    int      level;
    int      xp;
    int      xp_to_next;
    ItemType hotbar_item[SDK_PERSISTENCE_HOTBAR_SLOTS];
    BlockType hotbar_block[SDK_PERSISTENCE_HOTBAR_SLOTS];
    int      hotbar_count[SDK_PERSISTENCE_HOTBAR_SLOTS];
    int      hotbar_durability[SDK_PERSISTENCE_HOTBAR_SLOTS];
    uint8_t  hotbar_payload_kind[SDK_PERSISTENCE_HOTBAR_SLOTS];
    SdkConstructionItemPayload hotbar_shaped[SDK_PERSISTENCE_HOTBAR_SLOTS];
    uint16_t hotbar_shaped_material[SDK_PERSISTENCE_HOTBAR_SLOTS];
    uint8_t  hotbar_shaped_profile_hint[SDK_PERSISTENCE_HOTBAR_SLOTS];
    char     hotbar_shaped_occupancy[SDK_PERSISTENCE_HOTBAR_SLOTS][SDK_PERSISTENCE_SHAPED_ITEM_B64_MAX];
    char     selected_character_id[64];
} SdkPersistedState;

typedef struct {
    void* impl;
} SdkPersistence;

void sdk_persistence_init(SdkPersistence* persistence, const SdkWorldDesc* requested_world, const char* save_path);
void sdk_persistence_shutdown(SdkPersistence* persistence);
const char* sdk_persistence_get_save_path(const SdkPersistence* persistence);
int  sdk_persistence_bind_construction_registry(SdkPersistence* persistence,
                                                SdkConstructionArchetypeRegistry* registry);

int  sdk_persistence_get_world_desc(const SdkPersistence* persistence, SdkWorldDesc* out_desc);
void sdk_persistence_set_world_desc(SdkPersistence* persistence, const SdkWorldDesc* world_desc);
int  sdk_persistence_get_state(const SdkPersistence* persistence, SdkPersistedState* out_state);
void sdk_persistence_set_state(SdkPersistence* persistence, const SdkPersistedState* state);
int  sdk_persistence_get_station_count(const SdkPersistence* persistence);
int  sdk_persistence_get_station_state(const SdkPersistence* persistence, int index, SdkPersistedStationState* out_state);
int  sdk_persistence_find_station_state(const SdkPersistence* persistence, int wx, int wy, int wz, SdkPersistedStationState* out_state);
void sdk_persistence_upsert_station_state(SdkPersistence* persistence, const SdkPersistedStationState* state);
void sdk_persistence_remove_station_state(SdkPersistence* persistence, int wx, int wy, int wz);
void sdk_persistence_clear_station_states(SdkPersistence* persistence);

int  sdk_persistence_load_chunk(SdkPersistence* persistence, int cx, int cz, SdkChunk* out_chunk);
int sdk_persistence_store_chunk(SdkPersistence* persistence, const SdkChunk* chunk);
int sdk_persistence_get_chunk_count(const SdkPersistence* persistence);
void sdk_persistence_save(SdkPersistence* persistence);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_PERSISTENCE_H */
