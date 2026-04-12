/**
 * sdk_entity.c — Entity system implementation
 *
 * Handles item drop spawning, physics (gravity, bobbing, magnet pull),
 * auto-pickup, and despawn.
 */
#include "sdk_entity.h"
#include "../World/ConstructionCells/sdk_construction_cells.h"
#include "../Items/sdk_item.h"
#include <math.h>
#include <string.h>

/* Physics constants for item entities */
#define ENTITY_GRAVITY  0.02f
#define ENTITY_TERMINAL 1.0f
#define ENTITY_FRICTION 0.9f    /* Horizontal damping */
#define ENTITY_MAGNET_SPEED 0.12f

bool sdk_entity_is_humanoid(MobType mob_type)
{
    /* Returns true if mob type is a humanoid (zombie, commoner, builder, etc.) */
    switch (mob_type) {
        case MOB_ZOMBIE:
        case MOB_COMMONER:
        case MOB_BUILDER:
        case MOB_BLACKSMITH:
        case MOB_MINER:
        case MOB_FOREMAN:
        case MOB_SOLDIER:
        case MOB_GENERAL:
            return true;
        default:
            return false;
    }
}

bool sdk_entity_is_vehicle(MobType mob_type)
{
    /* Returns true if mob type is a vehicle (car, motorbike, tank) */
    return mob_type == MOB_CAR || mob_type == MOB_MOTORBIKE || mob_type == MOB_TANK;
}

float sdk_entity_mob_width(MobType mob_type)
{
    /* Returns collision width for a mob type */
    switch (mob_type) {
        case MOB_CAR: return MOB_CAR_WIDTH;
        case MOB_MOTORBIKE: return MOB_MOTORBIKE_WIDTH;
        case MOB_TANK: return MOB_TANK_WIDTH;
        case MOB_BOAR: return MOB_BOAR_WIDTH;
        case MOB_DEER: return MOB_DEER_WIDTH;
        case MOB_COMMONER:
        case MOB_BUILDER:
        case MOB_BLACKSMITH:
        case MOB_MINER:
        case MOB_FOREMAN:
        case MOB_SOLDIER:
        case MOB_GENERAL:
            return MOB_HUMAN_WIDTH;
        case MOB_ZOMBIE:
        default: return MOB_ZOMBIE_WIDTH;
    }
}

float sdk_entity_mob_height(MobType mob_type)
{
    /* Returns collision height for a mob type */
    switch (mob_type) {
        case MOB_CAR: return MOB_CAR_HEIGHT;
        case MOB_MOTORBIKE: return MOB_MOTORBIKE_HEIGHT;
        case MOB_TANK: return MOB_TANK_HEIGHT;
        case MOB_BOAR: return MOB_BOAR_HEIGHT;
        case MOB_DEER: return MOB_DEER_HEIGHT;
        case MOB_COMMONER:
        case MOB_BUILDER:
        case MOB_BLACKSMITH:
        case MOB_MINER:
        case MOB_FOREMAN:
        case MOB_SOLDIER:
        case MOB_GENERAL:
            return MOB_HUMAN_HEIGHT;
        case MOB_ZOMBIE:
        default: return MOB_ZOMBIE_HEIGHT;
    }
}

float sdk_entity_vehicle_speed(MobType mob_type)
{
    /* Returns movement speed for a vehicle mob type */
    switch (mob_type) {
        case MOB_CAR: return MOB_CAR_SPEED;
        case MOB_MOTORBIKE: return MOB_MOTORBIKE_SPEED;
        case MOB_TANK: return MOB_TANK_SPEED;
        default: return 0.0f;
    }
}

float sdk_entity_vehicle_eye_height(MobType mob_type)
{
    /* Returns camera eye height for a vehicle mob type */
    switch (mob_type) {
        case MOB_CAR: return 1.35f;
        case MOB_MOTORBIKE: return 1.2f;
        case MOB_TANK: return 1.7f;
        default: return MOB_HUMAN_HEIGHT;
    }
}

float sdk_entity_vehicle_interact_range(MobType mob_type)
{
    /* Returns interaction range for a vehicle mob type */
    switch (mob_type) {
        case MOB_CAR: return 3.0f;
        case MOB_MOTORBIKE: return 2.75f;
        case MOB_TANK: return 3.5f;
        default: return 0.0f;
    }
}

static float mob_speed(MobType mob_type)
{
    /* Returns movement speed for a mob type (internal helper) */
    switch (mob_type) {
        case MOB_CAR:
        case MOB_MOTORBIKE:
        case MOB_TANK:
            return sdk_entity_vehicle_speed(mob_type);
        case MOB_BOAR: return MOB_BOAR_SPEED;
        case MOB_DEER: return MOB_DEER_SPEED;
        case MOB_COMMONER:
        case MOB_BUILDER:
        case MOB_BLACKSMITH:
        case MOB_MINER:
        case MOB_FOREMAN:
        case MOB_SOLDIER:
        case MOB_GENERAL:
            return MOB_HUMAN_SPEED;
        case MOB_ZOMBIE:
        default: return MOB_ZOMBIE_SPEED;
    }
}

static float mob_despawn_range(MobType mob_type)
{
    /* Returns despawn range for a mob type (internal helper) */
    switch (mob_type) {
        case MOB_CAR:
        case MOB_MOTORBIKE:
        case MOB_TANK:
            return 128.0f;
        case MOB_COMMONER:
        case MOB_BUILDER:
        case MOB_BLACKSMITH:
        case MOB_MINER:
        case MOB_FOREMAN:
        case MOB_SOLDIER:
        case MOB_GENERAL:
            return 96.0f;
        case MOB_BOAR:
        case MOB_DEER:
            return 40.0f;
        case MOB_ZOMBIE:
        default:
            return MOB_ZOMBIE_DESPAWN;
    }
}

static float mob_hit_range(MobType mob_type)
{
    /* Returns attack hit range for a mob type (internal helper) */
    switch (mob_type) {
        case MOB_CAR:
            return 2.2f;
        case MOB_MOTORBIKE:
            return 1.8f;
        case MOB_TANK:
            return 2.8f;
        case MOB_BOAR:
        case MOB_DEER:
            return 1.8f;
        case MOB_COMMONER:
        case MOB_BUILDER:
        case MOB_BLACKSMITH:
        case MOB_MINER:
        case MOB_FOREMAN:
        case MOB_SOLDIER:
        case MOB_GENERAL:
            return 1.6f;
        case MOB_ZOMBIE:
        default:
            return MOB_ZOMBIE_HIT_RANGE;
    }
}

void sdk_entity_init(SdkEntityList* list)
{
    /* Initializes entity list, clearing all entities and count */
    if (!list) return;
    memset(list, 0, sizeof(*list));
}

const char* sdk_entity_mob_name(MobType mob_type)
{
    /* Returns string name for a mob type (e.g., "zombie", "car") */
    switch (mob_type) {
        case MOB_ZOMBIE: return "zombie";
        case MOB_BOAR: return "boar";
        case MOB_DEER: return "deer";
        case MOB_COMMONER: return "commoner";
        case MOB_BUILDER: return "builder";
        case MOB_BLACKSMITH: return "blacksmith";
        case MOB_MINER: return "miner";
        case MOB_FOREMAN: return "foreman";
        case MOB_SOLDIER: return "soldier";
        case MOB_GENERAL: return "general";
        case MOB_CAR: return "car";
        case MOB_MOTORBIKE: return "motorbike";
        case MOB_TANK: return "tank";
        default: return "unknown";
    }
}

const char* sdk_entity_npc_role_name(SdkNpcRole role)
{
    /* Returns string name for an NPC role */
    switch (role) {
        case SDK_NPC_ROLE_COMMONER: return "commoner";
        case SDK_NPC_ROLE_BUILDER: return "builder";
        case SDK_NPC_ROLE_BLACKSMITH: return "blacksmith";
        case SDK_NPC_ROLE_MINER: return "miner";
        case SDK_NPC_ROLE_FOREMAN: return "foreman";
        case SDK_NPC_ROLE_SOLDIER: return "soldier";
        default: return "none";
    }
}

const char* sdk_entity_npc_task_name(SdkNpcTaskKind task)
{
    /* Returns string name for an NPC task kind */
    switch (task) {
        case SDK_NPC_TASK_MOVE: return "move";
        case SDK_NPC_TASK_GATHER: return "gather";
        case SDK_NPC_TASK_HAUL_LOCAL: return "haul";
        case SDK_NPC_TASK_USE_STATION: return "use_station";
        case SDK_NPC_TASK_REPAIR: return "repair";
        case SDK_NPC_TASK_PLACE_BLOCK: return "place_block";
        case SDK_NPC_TASK_BREAK_BLOCK: return "break_block";
        case SDK_NPC_TASK_SLEEP: return "sleep";
        case SDK_NPC_TASK_REST: return "rest";
        case SDK_NPC_TASK_PATROL: return "patrol";
        case SDK_NPC_TASK_IDLE:
        default:
            return "idle";
    }
}

SdkEntity* sdk_entity_spawn_item(SdkEntityList* list,
                                  float wx, float wy, float wz,
                                  ItemType item_type)
{
    /* Spawns a dropped item entity at world position with velocity scatter */
    if (!list) return NULL;
    for (int i = 0; i < ENTITY_MAX; i++) {
        if (!list->entities[i].active) {
            SdkEntity* e = &list->entities[i];
            memset(e, 0, sizeof(*e));
            e->kind       = ENTITY_ITEM;
            e->active     = true;
            e->px         = wx;
            e->py         = wy;
            e->pz         = wz;
            /* Small random-ish horizontal scatter + upward pop */
            float hash = sinf(wx * 13.7f + wz * 7.3f + wy * 3.1f);
            e->vx         = hash * 0.06f;
            e->vz         = cosf(wx * 5.1f + wz * 11.3f) * 0.06f;
            e->vy         = ITEM_DROP_VEL_Y;
            e->drop_item  = item_type;
            e->drop_display_block = (uint16_t)sdk_item_to_block(item_type);
            e->drop_payload_kind = SDK_ITEM_PAYLOAD_NONE;
            e->age        = 0;
            e->spin       = 0.0f;
            list->count++;
            return e;
        }
    }
    return NULL; /* Full */
}

SdkEntity* sdk_entity_spawn_shaped_item(SdkEntityList* list,
                                        float wx, float wy, float wz,
                                        BlockType material,
                                        const SdkConstructionItemPayload* payload)
{
    /* Spawns a shaped construction item as a dropped entity */
    SdkEntity* e;
    SdkConstructionItemPayload normalized;

    if (!payload || payload->occupied_count == 0u) return NULL;
    normalized = *payload;
    sdk_construction_payload_refresh_metadata(&normalized);
    e = sdk_entity_spawn_item(list, wx, wy, wz, sdk_block_to_item(material));
    if (!e) return NULL;
    e->drop_item = ITEM_NONE;
    e->drop_display_block = (uint16_t)material;
    e->drop_payload_kind = SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION;
    e->drop_shaped = normalized;
    return e;
}

void sdk_entity_remove(SdkEntityList* list, int index)
{
    /* Removes (deactivates) an entity at the specified index */
    if (!list || index < 0 || index >= ENTITY_MAX) return;
    if (list->entities[index].active) {
        list->entities[index].active = false;
        list->count--;
        if (list->count < 0) list->count = 0;
    }
}

SdkEntity* sdk_entity_spawn_mob(SdkEntityList* list,
                                 float wx, float wy, float wz,
                                 MobType mob_type)
{
    /* Spawns a mob entity at world position with type-specific health and color */
    if (!list) return NULL;
    for (int i = 0; i < ENTITY_MAX; i++) {
        if (!list->entities[i].active) {
            SdkEntity* e = &list->entities[i];
            memset(e, 0, sizeof(*e));
            e->kind       = ENTITY_MOB;
            e->active     = true;
            e->px         = wx;
            e->py         = wy;
            e->pz         = wz;
            e->mob_type   = mob_type;
            e->age        = 0;
            e->mob_dir_x  = 0.0f;
            e->mob_dir_z  = 1.0f;
            switch (mob_type) {
                case MOB_ZOMBIE:
                    e->mob_health     = MOB_ZOMBIE_HEALTH;
                    e->mob_max_health = MOB_ZOMBIE_HEALTH;
                    e->mob_color      = 0xFF208040; /* Green-ish zombie */
                    e->mob_color_secondary = 0xFF164E28;
                    break;
                case MOB_BOAR:
                    e->mob_health     = MOB_BOAR_HEALTH;
                    e->mob_max_health = MOB_BOAR_HEALTH;
                    e->mob_color      = 0xFF4A4A82;
                    e->mob_color_secondary = 0xFF2A2A40;
                    break;
                case MOB_DEER:
                    e->mob_health     = MOB_DEER_HEALTH;
                    e->mob_max_health = MOB_DEER_HEALTH;
                    e->mob_color      = 0xFF72A0C6;
                    e->mob_color_secondary = 0xFF4C6E8A;
                    break;
                case MOB_COMMONER:
                    e->mob_health     = MOB_HUMAN_HEALTH;
                    e->mob_max_health = MOB_HUMAN_HEALTH;
                    e->mob_color      = 0xFFB0A080;
                    e->mob_color_secondary = 0xFF705C48;
                    e->mob_role       = SDK_NPC_ROLE_COMMONER;
                    break;
                case MOB_BUILDER:
                    e->mob_health     = MOB_HUMAN_HEALTH;
                    e->mob_max_health = MOB_HUMAN_HEALTH;
                    e->mob_color      = 0xFF4F86C8;
                    e->mob_color_secondary = 0xFF58D6F4;
                    e->mob_role       = SDK_NPC_ROLE_BUILDER;
                    break;
                case MOB_BLACKSMITH:
                    e->mob_health     = MOB_HUMAN_HEALTH;
                    e->mob_max_health = MOB_HUMAN_HEALTH;
                    e->mob_color      = 0xFF404652;
                    e->mob_color_secondary = 0xFF3A77CC;
                    e->mob_role       = SDK_NPC_ROLE_BLACKSMITH;
                    break;
                case MOB_MINER:
                    e->mob_health     = MOB_HUMAN_HEALTH;
                    e->mob_max_health = MOB_HUMAN_HEALTH;
                    e->mob_color      = 0xFF7A5E36;
                    e->mob_color_secondary = 0xFF48D0F2;
                    e->mob_role       = SDK_NPC_ROLE_MINER;
                    break;
                case MOB_FOREMAN:
                    e->mob_health     = MOB_HUMAN_HEALTH + 4;
                    e->mob_max_health = MOB_HUMAN_HEALTH + 4;
                    e->mob_color      = 0xFF866042;
                    e->mob_color_secondary = 0xFFD0C090;
                    e->mob_role       = SDK_NPC_ROLE_FOREMAN;
                    break;
                case MOB_SOLDIER:
                    e->mob_health     = MOB_HUMAN_HEALTH + 8;
                    e->mob_max_health = MOB_HUMAN_HEALTH + 8;
                    e->mob_color      = 0xFF3C6A38;
                    e->mob_color_secondary = 0xFF244424;
                    e->mob_role       = SDK_NPC_ROLE_SOLDIER;
                    break;
                case MOB_GENERAL:
                    e->mob_health     = MOB_HUMAN_HEALTH + 12;
                    e->mob_max_health = MOB_HUMAN_HEALTH + 12;
                    e->mob_color      = 0xFF6A3C30;
                    e->mob_color_secondary = 0xFF40C8F0;
                    break;
                case MOB_CAR:
                    e->mob_health     = MOB_CAR_HEALTH;
                    e->mob_max_health = MOB_CAR_HEALTH;
                    e->mob_color      = 0xFF2C5CE0;
                    e->mob_color_secondary = 0xFFC8D4E0;
                    break;
                case MOB_MOTORBIKE:
                    e->mob_health     = MOB_MOTORBIKE_HEALTH;
                    e->mob_max_health = MOB_MOTORBIKE_HEALTH;
                    e->mob_color      = 0xFF14A0F0;
                    e->mob_color_secondary = 0xFF202020;
                    break;
                case MOB_TANK:
                    e->mob_health     = MOB_TANK_HEALTH;
                    e->mob_max_health = MOB_TANK_HEALTH;
                    e->mob_color      = 0xFF486848;
                    e->mob_color_secondary = 0xFF283828;
                    break;
            }
            list->count++;
            return e;
        }
    }
    return NULL;
}

/**
 * Performs melee attack from player position, damaging closest mob in range.
 * 
 * @param list Entity list to search for mobs.
 * @param px Player X position.
 * @param py Player Y position.
 * @param pz Player Z position.
 * @param look_x Player look X direction.
 * @param look_y Player look Y direction.
 * @param look_z Player look Z direction.
 * @param attack_damage Damage to deal to mob.
 * @param killed Whether a mob was killed.
 * @param kill_x X position of killed mob.
 * @param kill_y Y position of killed mob.
 * @param kill_z Z position of killed mob.
 * @param killed_type Type of killed mob.
 * 
 * @return Damage dealt to mob.
 */
int sdk_entity_player_attack(SdkEntityList* list,
                              float px, float py, float pz,
                              float look_x, float look_y, float look_z,
                              int attack_damage,
                              bool* killed, float* kill_x, float* kill_y, float* kill_z,
                              MobType* killed_type)
{
    if (!list) return 0;
    if (killed) *killed = false;
    if (killed_type) *killed_type = MOB_ZOMBIE;

    /* Find nearest mob within hit range and roughly in front of player */
    float best_dist = 2.0f * 2.0f;
    int best_idx = -1;

    float dir_x = look_x - px, dir_y = look_y - py, dir_z = look_z - pz;
    float dir_len = sqrtf(dir_x*dir_x + dir_y*dir_y + dir_z*dir_z);
    if (dir_len > 0.001f) { dir_x /= dir_len; dir_y /= dir_len; dir_z /= dir_len; }

    for (int i = 0; i < ENTITY_MAX; i++) {
        SdkEntity* e = &list->entities[i];
        if (!e->active || e->kind != ENTITY_MOB) continue;

        float dx = e->px - px;
        float dy = (e->py + sdk_entity_mob_height(e->mob_type) * 0.5f) - py;
        float dz = e->pz - pz;
        float dist_sq = dx*dx + dy*dy + dz*dz;
        if (dist_sq > mob_hit_range(e->mob_type) * mob_hit_range(e->mob_type)) continue;

        /* Check roughly in front (dot product > 0) */
        float dot = dx * dir_x + dy * dir_y + dz * dir_z;
        if (dot < 0.0f) continue;

        if (dist_sq < best_dist) {
            best_dist = dist_sq;
            best_idx = i;
        }
    }

    if (best_idx < 0) return 0;

    SdkEntity* e = &list->entities[best_idx];
    e->mob_health -= attack_damage;
    e->mob_hurt_timer = MOB_HURT_FLASH;

    /* Knockback away from player */
    float dx = e->px - px, dz = e->pz - pz;
    float hlen = sqrtf(dx*dx + dz*dz);
    if (hlen > 0.01f) {
        e->vx += (dx / hlen) * MOB_KNOCKBACK;
        e->vz += (dz / hlen) * MOB_KNOCKBACK;
    }
    e->vy += 0.15f;

    if (e->mob_health <= 0) {
        if (killed) *killed = true;
        if (kill_x) *kill_x = e->px;
        if (kill_y) *kill_y = e->py;
        if (kill_z) *kill_z = e->pz;
        if (killed_type) *killed_type = e->mob_type;
        sdk_entity_remove(list, best_idx);
    }

    return attack_damage;
}

void sdk_entity_set_settlement_control(SdkEntity* entity,
                                       uint32_t settlement_id,
                                       uint32_t resident_id,
                                       SdkNpcRole role)
{
    /* Assigns entity to settlement control with role and resident ID */
    if (!entity) return;
    entity->mob_behavior = SDK_ENTITY_BEHAVIOR_SETTLEMENT;
    entity->settlement_id = settlement_id;
    entity->resident_id = resident_id;
    entity->mob_role = (uint8_t)role;
}

void sdk_entity_clear_settlement_control(SdkEntity* entity)
{
    /* Removes entity from settlement control, reverting to default behavior */
    if (!entity) return;
    entity->mob_behavior = SDK_ENTITY_BEHAVIOR_DEFAULT;
    entity->settlement_id = 0u;
    entity->resident_id = 0u;
    entity->mob_task = SDK_NPC_TASK_IDLE;
    entity->target_wx = 0;
    entity->target_wy = 0;
    entity->target_wz = 0;
}

void sdk_entity_set_controlled_target(SdkEntity* entity, int wx, int wy, int wz, SdkNpcTaskKind task_kind)
{
    /* Sets target destination and task for a settlement-controlled entity */
    if (!entity) return;
    entity->target_wx = wx;
    entity->target_wy = wy;
    entity->target_wz = wz;
    entity->mob_task = (uint8_t)task_kind;
}

void sdk_entity_clear_controlled_target(SdkEntity* entity, SdkNpcTaskKind fallback_task)
{
    /* Clears target, setting entity to idle at current position */
    if (!entity) return;
    entity->target_wx = (int32_t)floorf(entity->px);
    entity->target_wy = (int32_t)floorf(entity->py);
    entity->target_wz = (int32_t)floorf(entity->pz);
    entity->mob_task = (uint8_t)fallback_task;
}

int sdk_entity_is_at_target(const SdkEntity* entity, float radius_xy, float radius_y)
{
    /* Returns 1 if entity is within specified radius of its target */
    float dx;
    float dy;
    float dz;

    if (!entity) return 0;
    dx = ((float)entity->target_wx + 0.5f) - entity->px;
    dy = (float)entity->target_wy - entity->py;
    dz = ((float)entity->target_wz + 0.5f) - entity->pz;
    return (dx * dx + dz * dz) <= radius_xy * radius_xy && fabsf(dy) <= radius_y;
}

int sdk_entity_try_pickup_near(SdkEntityList* list,
                               SdkEntity* collector,
                               ItemType* out_item)
{
    /* Attempts to pick up nearest item entity within range of collector */
    int best_index = -1;
    float best_dist_sq = ITEM_PICKUP_RANGE * ITEM_PICKUP_RANGE;
    int i;

    if (out_item) *out_item = ITEM_NONE;
    if (!list || !collector) return 0;

    for (i = 0; i < ENTITY_MAX; ++i) {
        SdkEntity* e = &list->entities[i];
        float dx;
        float dy;
        float dz;
        float dist_sq;

        if (!e->active || e->kind != ENTITY_ITEM) continue;
        dx = e->px - collector->px;
        dy = (e->py + ITEM_SIZE) - (collector->py + sdk_entity_mob_height(collector->mob_type) * 0.5f);
        dz = e->pz - collector->pz;
        dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best_index = i;
        }
    }

    if (best_index < 0) return 0;
    if (out_item) *out_item = list->entities[best_index].drop_item;
    sdk_entity_remove(list, best_index);
    return 1;
}

/* Tick a single mob entity */
static void tick_mob(SdkEntity* e, int idx, SdkEntityList* list,
                     float player_x, float player_y, float player_z,
                     int* mob_damage_out, EntitySolidFn is_solid_fn)
{
    e->age++;
    if (e->mob_hurt_timer > 0) e->mob_hurt_timer--;
    if (e->mob_attack_cd > 0) e->mob_attack_cd--;

    float pfeet_y = player_y - 1.62f;
    float dx = player_x - e->px;
    float dz = player_z - e->pz;
    float horiz_dist_sq = dx*dx + dz*dz;
    float height = sdk_entity_mob_height(e->mob_type);
    float speed = mob_speed(e->mob_type);
    float despawn = mob_despawn_range(e->mob_type);
    int settlement_controlled = (e->mob_behavior == SDK_ENTITY_BEHAVIOR_SETTLEMENT);

    /* Despawn if too far */
    if (!settlement_controlled && horiz_dist_sq > despawn * despawn) {
        sdk_entity_remove(list, idx);
        return;
    }

    if (settlement_controlled && sdk_entity_is_humanoid(e->mob_type)) {
        float tx = (float)e->target_wx + 0.5f;
        float tz = (float)e->target_wz + 0.5f;
        float tdx = tx - e->px;
        float tdz = tz - e->pz;
        float target_dist_sq = tdx * tdx + tdz * tdz;

        if (target_dist_sq > 0.20f * 0.20f) {
            float target_dist = sqrtf(target_dist_sq);
            float nx = tdx / target_dist;
            float nz = tdz / target_dist;
            e->mob_dir_x = nx;
            e->mob_dir_z = nz;
            e->vx = nx * speed * 0.82f;
            e->vz = nz * speed * 0.82f;

            if (is_solid_fn) {
                int ahead_x = (int)floorf(e->px + nx * 0.55f);
                int ahead_z = (int)floorf(e->pz + nz * 0.55f);
                int feet_by = (int)floorf(e->py);
                if (is_solid_fn(ahead_x, feet_by, ahead_z) && e->vy == 0.0f &&
                    !is_solid_fn(ahead_x, feet_by + 1, ahead_z)) {
                    e->vy = 0.30f;
                } else if (is_solid_fn(ahead_x, feet_by + 1, ahead_z)) {
                    float side_x = -nz;
                    float side_z = nx;
                    e->vx = side_x * speed * 0.55f;
                    e->vz = side_z * speed * 0.55f;
                }
            }
        } else {
            e->vx *= 0.55f;
            e->vz *= 0.55f;
        }
    } else if (e->mob_type == MOB_ZOMBIE) {
        /* AI: walk toward player if within aggro range */
        if (horiz_dist_sq < MOB_ZOMBIE_AGGRO * MOB_ZOMBIE_AGGRO && horiz_dist_sq > 0.5f) {
            float horiz_dist = sqrtf(horiz_dist_sq);
            float nx = dx / horiz_dist;
            float nz = dz / horiz_dist;
            e->mob_dir_x = nx;
            e->mob_dir_z = nz;
            e->vx = nx * speed;
            e->vz = nz * speed;

            if (is_solid_fn) {
                int ahead_x = (int)floorf(e->px + nx * 0.5f);
                int ahead_z = (int)floorf(e->pz + nz * 0.5f);
                int feet_by = (int)floorf(e->py);
                if (is_solid_fn(ahead_x, feet_by, ahead_z) && e->vy == 0.0f) {
                    if (!is_solid_fn(ahead_x, feet_by + 1, ahead_z)) {
                        e->vy = 0.3f;
                    }
                }
            }
        } else {
            e->vx *= 0.8f;
            e->vz *= 0.8f;
        }
    } else if (sdk_entity_is_vehicle(e->mob_type)) {
        float dir_len = sqrtf(e->vx * e->vx + e->vz * e->vz);
        if (dir_len > 0.001f) {
            e->mob_dir_x = e->vx / dir_len;
            e->mob_dir_z = e->vz / dir_len;
        } else {
            e->vx *= 0.82f;
            e->vz *= 0.82f;
        }
    } else if (sdk_entity_is_humanoid(e->mob_type)) {
        if (e->mob_hurt_timer > 0 && horiz_dist_sq > 0.2f) {
            float horiz_dist = sqrtf(horiz_dist_sq);
            float nx = -dx / (horiz_dist > 0.001f ? horiz_dist : 1.0f);
            float nz = -dz / (horiz_dist > 0.001f ? horiz_dist : 1.0f);
            e->mob_dir_x = nx;
            e->mob_dir_z = nz;
            e->mob_ai_timer = 30;
            e->vx = nx * speed * 1.15f;
            e->vz = nz * speed * 1.15f;
        } else {
            if (e->mob_ai_timer <= 0) {
                float seed = sinf(e->px * 0.19f + e->pz * 0.11f + (float)e->age * 0.03f);
                float angle = (seed * 0.5f + 0.5f) * 6.2831853f;
                e->mob_dir_x = cosf(angle);
                e->mob_dir_z = sinf(angle);
                e->mob_ai_timer = 100 + (int)((seed * seed) * 80.0f);
            }
            e->mob_ai_timer--;
            e->vx = e->mob_dir_x * speed * 0.45f;
            e->vz = e->mob_dir_z * speed * 0.45f;
        }

        if (is_solid_fn) {
            int ahead_x = (int)floorf(e->px + e->vx * 10.0f);
            int ahead_z = (int)floorf(e->pz + e->vz * 10.0f);
            int feet_by = (int)floorf(e->py);
            if (is_solid_fn(ahead_x, feet_by, ahead_z) && e->vy == 0.0f &&
                !is_solid_fn(ahead_x, feet_by + 1, ahead_z)) {
                e->vy = 0.28f;
            }
        }
    } else {
        float flee_range = 8.0f;
        if ((horiz_dist_sq < flee_range * flee_range && horiz_dist_sq > 0.2f) || e->mob_hurt_timer > 0) {
            float horiz_dist = sqrtf(horiz_dist_sq);
            float nx = -dx / (horiz_dist > 0.001f ? horiz_dist : 1.0f);
            float nz = -dz / (horiz_dist > 0.001f ? horiz_dist : 1.0f);
            e->mob_dir_x = nx;
            e->mob_dir_z = nz;
            e->mob_ai_timer = 40;
            e->vx = nx * speed * 1.25f;
            e->vz = nz * speed * 1.25f;
        } else {
            if (e->mob_ai_timer <= 0) {
                float seed = sinf(e->px * 0.37f + e->pz * 0.23f + (float)e->age * 0.07f);
                float angle = (seed * 0.5f + 0.5f) * 6.2831853f;
                e->mob_dir_x = cosf(angle);
                e->mob_dir_z = sinf(angle);
                e->mob_ai_timer = 90 + (int)((seed * seed) * 90.0f);
            }
            e->mob_ai_timer--;
            e->vx = e->mob_dir_x * speed * 0.55f;
            e->vz = e->mob_dir_z * speed * 0.55f;
        }

        if (is_solid_fn) {
            int ahead_x = (int)floorf(e->px + e->vx * 8.0f);
            int ahead_z = (int)floorf(e->pz + e->vz * 8.0f);
            int feet_by = (int)floorf(e->py);
            if (is_solid_fn(ahead_x, feet_by, ahead_z) && e->vy == 0.0f &&
                !is_solid_fn(ahead_x, feet_by + 1, ahead_z)) {
                e->vy = 0.28f;
            }
        }
    }

    /* Gravity */
    e->vy -= ENTITY_GRAVITY;
    if (e->vy < -ENTITY_TERMINAL) e->vy = -ENTITY_TERMINAL;

    /* Apply velocity */
    e->px += e->vx;
    e->py += e->vy;
    e->pz += e->vz;

    if (sdk_entity_is_vehicle(e->mob_type)) {
        e->vx *= 0.9f;
        e->vz *= 0.9f;
    }

    /* Ground collision */
    if (is_solid_fn) {
        int bx = (int)floorf(e->px);
        int by = (int)floorf(e->py - 0.01f);
        int bz = (int)floorf(e->pz);
        if (by >= 0 && is_solid_fn(bx, by, bz)) {
            e->py = (float)(by + 1);
            if (e->vy < 0.0f) e->vy = 0.0f;
        }
    }

    /* Void kill */
    if (e->py < 0.0f) {
        sdk_entity_remove(list, idx);
        return;
    }

    /* Contact damage to player */
    float contact_dx = e->px - player_x;
    float contact_dy = (e->py + height * 0.5f) - (pfeet_y + 0.9f);
    float contact_dz = e->pz - player_z;
    float contact_dist_sq = contact_dx*contact_dx + contact_dy*contact_dy + contact_dz*contact_dz;
    if (!settlement_controlled &&
        e->mob_type == MOB_ZOMBIE &&
        contact_dist_sq < 1.2f * 1.2f &&
        e->mob_attack_cd <= 0) {
        if (mob_damage_out) *mob_damage_out += MOB_ZOMBIE_DAMAGE;
        e->mob_attack_cd = MOB_ZOMBIE_ATTACK_CD;
    }
}

void sdk_entity_tick_all(SdkEntityList* list,
                         float player_x, float player_y, float player_z,
                         SdkPickupItem* pickup_buf, int* pickup_count,
                         int pickup_buf_cap,
                         EntitySolidFn is_solid_fn,
                         int* mob_damage_out)
{
    /* Ticks all entities: mob AI, item physics, auto-pickup, damage, despawn */
    if (!list || !pickup_count) return;
    *pickup_count = 0;
    if (mob_damage_out) *mob_damage_out = 0;

    /* Player feet position for distance checks */
    float pfeet_y = player_y - 1.62f;

    for (int i = 0; i < ENTITY_MAX; i++) {
        SdkEntity* e = &list->entities[i];
        if (!e->active) continue;

        if (e->kind == ENTITY_MOB) {
            tick_mob(e, i, list, player_x, player_y, player_z, mob_damage_out, is_solid_fn);
            continue;
        }

        if (e->kind != ENTITY_ITEM) continue;

        e->age++;
        e->spin += 0.08f;

        /* Despawn if too old */
        if (e->age > ITEM_LIFETIME) {
            sdk_entity_remove(list, i);
            continue;
        }

        /* Gravity */
        e->vy -= ENTITY_GRAVITY;
        if (e->vy < -ENTITY_TERMINAL) e->vy = -ENTITY_TERMINAL;

        /* Apply velocity */
        e->px += e->vx;
        e->py += e->vy;
        e->pz += e->vz;

        /* Horizontal friction */
        e->vx *= ENTITY_FRICTION;
        e->vz *= ENTITY_FRICTION;

        /* Ground collision: check block below entity */
        if (is_solid_fn) {
            int bx = (int)floorf(e->px);
            int by = (int)floorf(e->py - 0.01f);
            int bz = (int)floorf(e->pz);
            if (by >= 0 && is_solid_fn(bx, by, bz)) {
                e->py = (float)(by + 1);
                if (e->vy < 0.0f) e->vy = 0.0f;
            }
        }

        /* Prevent falling into void */
        if (e->py < 0.0f) {
            sdk_entity_remove(list, i);
            continue;
        }

        /* Distance to player (center-ish) */
        float dx = e->px - player_x;
        float dy = (e->py + ITEM_SIZE) - (pfeet_y + 0.9f); /* Roughly player center */
        float dz = e->pz - player_z;
        float dist_sq = dx * dx + dy * dy + dz * dz;

        /* Magnet pull: drift toward player when close-ish */
        if (dist_sq < ITEM_MAGNET_RANGE * ITEM_MAGNET_RANGE && dist_sq > 0.01f) {
            float dist = sqrtf(dist_sq);
            float pull = ENTITY_MAGNET_SPEED / dist;
            e->vx -= dx * pull;
            e->vy -= dy * pull;
            e->vz -= dz * pull;
        }

        /* Auto-pickup */
        if (dist_sq < ITEM_PICKUP_RANGE * ITEM_PICKUP_RANGE) {
            if (pickup_buf && *pickup_count < pickup_buf_cap) {
                memset(&pickup_buf[*pickup_count], 0, sizeof(pickup_buf[*pickup_count]));
                pickup_buf[*pickup_count].item = e->drop_item;
                pickup_buf[*pickup_count].display_block_type = e->drop_display_block;
                pickup_buf[*pickup_count].payload_kind = e->drop_payload_kind;
                pickup_buf[*pickup_count].count = 1;
                pickup_buf[*pickup_count].durability = 0;
                if (e->drop_payload_kind == SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION) {
                    pickup_buf[*pickup_count].shaped = e->drop_shaped;
                }
                (*pickup_count)++;
            }
            sdk_entity_remove(list, i);
            continue;
        }
    }
}
