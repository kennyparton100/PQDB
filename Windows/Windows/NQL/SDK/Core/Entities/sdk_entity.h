/**
 * sdk_entity.h -- Entity system for item drops and mobs.
 */
#ifndef NQLSDK_ENTITY_H
#define NQLSDK_ENTITY_H

#include "../World/Blocks/sdk_block.h"
#include "../sdk_types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENTITY_MAX         256
#define ITEM_PICKUP_RANGE  1.5f
#define ITEM_MAGNET_RANGE  2.5f
#define ITEM_LIFETIME      600
#define ITEM_DROP_VEL_Y    0.15f
#define ITEM_BOB_SPEED     0.05f
#define ITEM_BOB_AMP       0.1f
#define ITEM_SIZE          0.25f

#define MOB_ZOMBIE_HEALTH    20
#define MOB_ZOMBIE_SPEED     0.03f
#define MOB_ZOMBIE_DAMAGE    3
#define MOB_ZOMBIE_ATTACK_CD 60
#define MOB_ZOMBIE_AGGRO     16.0f
#define MOB_ZOMBIE_DESPAWN   32.0f
#define MOB_ZOMBIE_WIDTH     0.3f
#define MOB_ZOMBIE_HEIGHT    1.8f
#define MOB_ZOMBIE_HIT_RANGE 1.5f
#define MOB_BOAR_HEALTH      14
#define MOB_BOAR_SPEED       0.024f
#define MOB_BOAR_WIDTH       0.45f
#define MOB_BOAR_HEIGHT      0.9f
#define MOB_DEER_HEALTH      12
#define MOB_DEER_SPEED       0.032f
#define MOB_DEER_WIDTH       0.35f
#define MOB_DEER_HEIGHT      1.5f
#define MOB_HUMAN_HEALTH     20
#define MOB_HUMAN_SPEED      0.022f
#define MOB_HUMAN_WIDTH      0.3f
#define MOB_HUMAN_HEIGHT     1.8f
#define MOB_CAR_HEALTH       48
#define MOB_CAR_SPEED        0.18f
#define MOB_CAR_WIDTH        0.9f
#define MOB_CAR_HEIGHT       1.25f
#define MOB_MOTORBIKE_HEALTH 28
#define MOB_MOTORBIKE_SPEED  0.22f
#define MOB_MOTORBIKE_WIDTH  0.45f
#define MOB_MOTORBIKE_HEIGHT 1.1f
#define MOB_TANK_HEALTH      96
#define MOB_TANK_SPEED       0.12f
#define MOB_TANK_WIDTH       1.15f
#define MOB_TANK_HEIGHT      1.55f
#define MOB_HURT_FLASH       10
#define MOB_KNOCKBACK        0.3f

typedef enum {
    ENTITY_NONE = 0,
    ENTITY_ITEM,
    ENTITY_MOB,
} EntityKind;

typedef enum {
    MOB_ZOMBIE = 0,
    MOB_BOAR,
    MOB_DEER,
    MOB_COMMONER,
    MOB_BUILDER,
    MOB_BLACKSMITH,
    MOB_MINER,
    MOB_FOREMAN,
    MOB_SOLDIER,
    MOB_GENERAL,
    MOB_CAR,
    MOB_MOTORBIKE,
    MOB_TANK,
} MobType;

typedef enum {
    SDK_NPC_ROLE_NONE = 0,
    SDK_NPC_ROLE_COMMONER,
    SDK_NPC_ROLE_BUILDER,
    SDK_NPC_ROLE_BLACKSMITH,
    SDK_NPC_ROLE_MINER,
    SDK_NPC_ROLE_FOREMAN,
    SDK_NPC_ROLE_SOLDIER
} SdkNpcRole;

typedef enum {
    SDK_NPC_TASK_IDLE = 0,
    SDK_NPC_TASK_MOVE,
    SDK_NPC_TASK_GATHER,
    SDK_NPC_TASK_HAUL_LOCAL,
    SDK_NPC_TASK_USE_STATION,
    SDK_NPC_TASK_REPAIR,
    SDK_NPC_TASK_PLACE_BLOCK,
    SDK_NPC_TASK_BREAK_BLOCK,
    SDK_NPC_TASK_SLEEP,
    SDK_NPC_TASK_REST,
    SDK_NPC_TASK_PATROL
} SdkNpcTaskKind;

typedef enum {
    SDK_ENTITY_BEHAVIOR_DEFAULT = 0,
    SDK_ENTITY_BEHAVIOR_SETTLEMENT
} SdkEntityBehaviorMode;

typedef struct {
    ItemType item;
    int count;
    int durability;
} SdkNpcInventorySlot;

#define SDK_NPC_INVENTORY_SLOTS 8

typedef struct {
    EntityKind kind;
    float px, py, pz;
    float vx, vy, vz;
    int   age;
    bool  active;
    ItemType drop_item;
    uint16_t drop_display_block;
    uint8_t drop_payload_kind;
    uint8_t drop_reserved0;
    SdkConstructionItemPayload drop_shaped;
    MobType mob_type;
    int     mob_health;
    int     mob_max_health;
    int     mob_attack_cd;
    int     mob_hurt_timer;
    uint32_t mob_color;
    uint32_t mob_color_secondary;
    int     mob_ai_timer;
    float   mob_dir_x;
    float   mob_dir_z;
    float spin;
    uint8_t  mob_behavior;
    uint8_t  mob_role;
    uint8_t  mob_task;
    uint8_t  mob_flags;
    uint32_t settlement_id;
    uint32_t resident_id;
    int32_t  home_wx, home_wy, home_wz;
    int32_t  work_wx, work_wy, work_wz;
    int32_t  target_wx, target_wy, target_wz;
    int32_t  action_wx, action_wy, action_wz;
    int      task_stage;
    int      task_timer;
    int      task_cooldown;
    int      work_building_index;
    int      home_building_index;
    int      inventory_selected;
    SdkNpcInventorySlot inventory[SDK_NPC_INVENTORY_SLOTS];
} SdkEntity;

typedef struct {
    SdkEntity entities[ENTITY_MAX];
    int       count;
} SdkEntityList;

typedef bool (*EntitySolidFn)(int wx, int wy, int wz);

void sdk_entity_init(SdkEntityList* list);
SdkEntity* sdk_entity_spawn_item(SdkEntityList* list, float wx, float wy, float wz, ItemType item_type);
SdkEntity* sdk_entity_spawn_shaped_item(SdkEntityList* list,
                                        float wx, float wy, float wz,
                                        BlockType material,
                                        const SdkConstructionItemPayload* payload);
void sdk_entity_tick_all(SdkEntityList* list,
                         float player_x, float player_y, float player_z,
                         SdkPickupItem* pickup_buf, int* pickup_count,
                         int pickup_buf_cap,
                         EntitySolidFn is_solid_fn,
                         int* mob_damage_out);
SdkEntity* sdk_entity_spawn_mob(SdkEntityList* list, float wx, float wy, float wz, MobType mob_type);
void sdk_entity_remove(SdkEntityList* list, int index);
float sdk_entity_mob_width(MobType mob_type);
float sdk_entity_mob_height(MobType mob_type);
bool sdk_entity_is_humanoid(MobType mob_type);
bool sdk_entity_is_vehicle(MobType mob_type);
float sdk_entity_vehicle_speed(MobType mob_type);
float sdk_entity_vehicle_eye_height(MobType mob_type);
float sdk_entity_vehicle_interact_range(MobType mob_type);
const char* sdk_entity_mob_name(MobType mob_type);
const char* sdk_entity_npc_role_name(SdkNpcRole role);
const char* sdk_entity_npc_task_name(SdkNpcTaskKind task);
int sdk_entity_player_attack(SdkEntityList* list,
                             float px, float py, float pz,
                             float look_x, float look_y, float look_z,
                             int attack_damage,
                             bool* killed, float* kill_x, float* kill_y, float* kill_z,
                             MobType* killed_type);
void sdk_entity_set_settlement_control(SdkEntity* entity,
                                       uint32_t settlement_id,
                                       uint32_t resident_id,
                                       SdkNpcRole role);
void sdk_entity_clear_settlement_control(SdkEntity* entity);
void sdk_entity_set_controlled_target(SdkEntity* entity, int wx, int wy, int wz, SdkNpcTaskKind task_kind);
void sdk_entity_clear_controlled_target(SdkEntity* entity, SdkNpcTaskKind fallback_task);
int sdk_entity_is_at_target(const SdkEntity* entity, float radius_xy, float radius_y);
int sdk_entity_try_pickup_near(SdkEntityList* list,
                               SdkEntity* collector,
                               ItemType* out_item);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_ENTITY_H */
