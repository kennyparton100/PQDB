#include "../Internal/sdk_api_internal.h"

int item_is_stackable(ItemType type)
{
    /* Returns 1 if the item type can be stacked in inventory */
    return !sdk_item_is_tool(type) && !sdk_item_is_firearm(type);
}

void clear_hotbar_entry(HotbarEntry* entry)
{
    /* Clears a hotbar entry, resetting all fields to default/empty state */
    if (!entry) return;
    entry->item = ITEM_NONE;
    entry->creative_block = BLOCK_AIR;
    entry->count = 0;
    entry->durability = 0;
    entry->payload_kind = SDK_ITEM_PAYLOAD_NONE;
    entry->reserved0 = 0u;
    entry->reserved1 = 0u;
    memset(&entry->shaped, 0, sizeof(entry->shaped));
}

void consume_hotbar_item(HotbarEntry* entry, int amount)
{
    /* Consumes a specified amount from a hotbar entry, clearing if depleted */
    if (!entry || amount <= 0 || entry->count <= 0) return;
    entry->count -= amount;
    if (entry->count <= 0) {
        clear_hotbar_entry(entry);
    }
}

void drop_loot_for_killed_mob(MobType killed_type, float kx, float ky, float kz)
{
    /* Spawns loot items at the specified location for a killed mob */
    if (killed_type == MOB_ZOMBIE) {
        sdk_entity_spawn_item(&g_sdk.entities, kx, ky + 0.5f, kz, ITEM_RAW_MEAT);
    } else if (killed_type == MOB_BOAR || killed_type == MOB_DEER) {
        sdk_entity_spawn_item(&g_sdk.entities, kx, ky + 0.5f, kz, ITEM_RAW_MEAT);
        sdk_entity_spawn_item(&g_sdk.entities, kx, ky + 0.5f, kz, ITEM_RAW_MEAT);
        if ((((int)floorf(kx) ^ (int)floorf(kz)) & 1) == 0) {
            sdk_entity_spawn_item(&g_sdk.entities, kx, ky + 0.5f, kz, ITEM_HIDE);
        }
    }
}

int firearm_damage(ItemType item)
{
    /* Returns damage value for a firearm item type */
    switch (item) {
        case ITEM_PISTOL: return 8;
        case ITEM_ASSAULT_RIFLE: return 5;
        case ITEM_SNIPER_RIFLE: return 18;
        default: return 1;
    }
}

float firearm_range(ItemType item)
{
    /* Returns effective range for a firearm item type */
    switch (item) {
        case ITEM_PISTOL: return 28.0f;
        case ITEM_ASSAULT_RIFLE: return 36.0f;
        case ITEM_SNIPER_RIFLE: return 72.0f;
        default: return 8.0f;
    }
}

float firearm_hit_radius(ItemType item)
{
    /* Returns hit radius for a firearm projectile */
    switch (item) {
        case ITEM_PISTOL: return 0.48f;
        case ITEM_ASSAULT_RIFLE: return 0.58f;
        case ITEM_SNIPER_RIFLE: return 0.38f;
        default: return 0.50f;
    }
}

int firearm_cooldown_frames(ItemType item)
{
    /* Returns cooldown frames between shots for a firearm */
    switch (item) {
        case ITEM_PISTOL: return 12;
        case ITEM_ASSAULT_RIFLE: return 4;
        case ITEM_SNIPER_RIFLE: return 24;
        default: return 8;
    }
}

float throwable_range(ItemType item)
{
    /* Returns throw range for a throwable item */
    switch (item) {
        case ITEM_SMOKE_GRANADE: return 16.0f;
        case ITEM_TACTICAL_GRANADE: return 18.0f;
        case ITEM_HAND_GRANADE:
        case ITEM_SEMTEX:
        default: return 20.0f;
    }
}

int throwable_cooldown_frames(ItemType item)
{
    /* Returns cooldown frames between throws */
    switch (item) {
        case ITEM_TACTICAL_GRANADE: return 16;
        case ITEM_SMOKE_GRANADE: return 16;
        case ITEM_HAND_GRANADE: return 18;
        case ITEM_SEMTEX: return 20;
        default: return 18;
    }
}

void apply_damage_to_player(int damage)
{
    /* Applies damage to player, handling invincibility frames and death */
    if (damage <= 0 || g_player_dead || g_invincible_frames > 0) return;
    g_player_health -= damage;
    g_invincible_frames = INVINCIBILITY_FRAMES;
    if (g_player_health <= 0) {
        g_player_health = 0;
        g_player_dead = true;
        g_death_timer = 0;
    }
}

void spawn_smoke_cloud(float x, float y, float z, float radius, int duration)
{
    /* Spawns or recycles a smoke cloud at the specified position */
    int best = 0;
    int best_timer = INT_MAX;

    for (int i = 0; i < MAX_SMOKE_CLOUDS; ++i) {
        if (!g_smoke_clouds[i].active) {
            best = i;
            best_timer = -1;
            break;
        }
        if (g_smoke_clouds[i].timer < best_timer) {
            best = i;
            best_timer = g_smoke_clouds[i].timer;
        }
    }

    g_smoke_clouds[best].active = true;
    g_smoke_clouds[best].x = x;
    g_smoke_clouds[best].y = y;
    g_smoke_clouds[best].z = z;
    g_smoke_clouds[best].radius = radius;
    g_smoke_clouds[best].timer = duration;
}

int apply_direct_hit_to_entity(SdkEntity* e,
                                      float knock_x,
                                      float knock_y,
                                      float knock_z,
                                      int damage,
                                      bool* killed,
                                      float* kill_x,
                                      float* kill_y,
                                      float* kill_z,
                                      MobType* killed_type)
{
    /* Applies damage with knockback to an entity, returns damage dealt */
    if (!e || !e->active || e->kind != ENTITY_MOB || damage <= 0) return 0;

    e->mob_health -= damage;
    e->mob_hurt_timer = MOB_HURT_FLASH;
    e->vx += knock_x;
    e->vy += knock_y;
    e->vz += knock_z;

    if (e->mob_health <= 0) {
        if (killed) *killed = true;
        if (kill_x) *kill_x = e->px;
        if (kill_y) *kill_y = e->py;
        if (kill_z) *kill_z = e->pz;
        if (killed_type) *killed_type = e->mob_type;
        e->active = false;
        g_sdk.entities.count--;
        if (g_sdk.entities.count < 0) g_sdk.entities.count = 0;
    }
    return damage;
}

int ranged_attack_mob(float px, float py, float pz,
                             float dir_x, float dir_y, float dir_z,
                             float max_dist, float hit_radius, int damage,
                             bool* killed, float* kill_x, float* kill_y, float* kill_z,
                             MobType* killed_type)
{
    /* Performs a ranged attack along direction, damaging closest mob in path */
    int best_idx = -1;
    float best_proj = max_dist + 1.0f;

    if (killed) *killed = false;
    if (killed_type) *killed_type = MOB_ZOMBIE;

    for (int i = 0; i < ENTITY_MAX; ++i) {
        SdkEntity* e = &g_sdk.entities.entities[i];
        float tx;
        float ty;
        float tz;
        float proj;
        float miss_x;
        float miss_y;
        float miss_z;
        float miss_sq;
        float target_radius;

        if (!e->active || e->kind != ENTITY_MOB) continue;

        tx = e->px - px;
        ty = (e->py + sdk_entity_mob_height(e->mob_type) * 0.55f) - py;
        tz = e->pz - pz;
        proj = tx * dir_x + ty * dir_y + tz * dir_z;
        if (proj < 0.0f || proj > max_dist) continue;

        miss_x = tx - dir_x * proj;
        miss_y = ty - dir_y * proj;
        miss_z = tz - dir_z * proj;
        miss_sq = miss_x * miss_x + miss_y * miss_y + miss_z * miss_z;
        target_radius = sdk_entity_mob_width(e->mob_type) + hit_radius;
        if (miss_sq > target_radius * target_radius) continue;

        if (proj < best_proj) {
            best_proj = proj;
            best_idx = i;
        }
    }

    if (best_idx < 0) return 0;
    return apply_direct_hit_to_entity(&g_sdk.entities.entities[best_idx],
                                      dir_x * 0.24f, 0.10f, dir_z * 0.24f,
                                      damage,
                                      killed, kill_x, kill_y, kill_z, killed_type);
}

void damage_mobs_in_radius(float x, float y, float z, float radius, int max_damage, int stun_frames)
{
    /* Damages all mobs within radius with falloff, applies stun effect */
    for (int i = 0; i < ENTITY_MAX; ++i) {
        SdkEntity* e = &g_sdk.entities.entities[i];
        float dx;
        float dy;
        float dz;
        float dist_sq;
        float dist;
        float t;
        int damage = 0;
        bool killed = false;
        float kill_x = 0.0f;
        float kill_y = 0.0f;
        float kill_z = 0.0f;
        MobType killed_type = MOB_ZOMBIE;

        if (!e->active || e->kind != ENTITY_MOB) continue;

        dx = e->px - x;
        dy = (e->py + sdk_entity_mob_height(e->mob_type) * 0.5f) - y;
        dz = e->pz - z;
        dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq > radius * radius) continue;
        dist = sqrtf(dist_sq);
        t = 1.0f - api_clampf(dist / radius, 0.0f, 1.0f);
        if (max_damage > 0) {
            damage = api_clampi((int)lrintf((float)max_damage * t), 1, max_damage);
        }

        if (damage > 0) {
            float inv = (dist > 0.001f) ? (1.0f / dist) : 0.0f;
            apply_direct_hit_to_entity(e,
                                       dx * inv * (0.18f + t * 0.20f),
                                       0.10f + t * 0.18f,
                                       dz * inv * (0.18f + t * 0.20f),
                                       damage,
                                       &killed, &kill_x, &kill_y, &kill_z, &killed_type);
        }
        if (e->active && stun_frames > 0) {
            e->mob_attack_cd = api_clampi(e->mob_attack_cd + stun_frames, 0, 600);
            e->mob_ai_timer = api_clampi(e->mob_ai_timer + stun_frames / 2, 0, 600);
            e->vx *= 0.35f;
            e->vz *= 0.35f;
            e->mob_hurt_timer = api_clampi(e->mob_hurt_timer + stun_frames / 20, MOB_HURT_FLASH, 120);
        }

        if (killed) {
            drop_loot_for_killed_mob(killed_type, kill_x, kill_y, kill_z);
        }
    }
}

void apply_player_radial_effect(float cam_x, float cam_y, float cam_z,
                                       float x, float y, float z,
                                       float radius, int max_damage, float flash_strength, int flash_frames)
{
    /* Applies radial damage and screen flash to player if within radius */
    float dx = cam_x - x;
    float dy = cam_y - y;
    float dz = cam_z - z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    float t;

    if (dist > radius) return;
    t = 1.0f - api_clampf(dist / radius, 0.0f, 1.0f);
    if (max_damage > 0) {
        int damage = api_clampi((int)lrintf((float)max_damage * t), 1, max_damage);
        apply_damage_to_player(damage);
    }
    if (flash_strength > 0.0f && flash_frames > 0) {
        float total_strength = flash_strength * t;
        if (total_strength > g_screen_flash_strength || g_screen_flash_timer <= 0) {
            g_screen_flash_strength = total_strength;
            g_screen_flash_timer = flash_frames;
            g_screen_flash_duration = flash_frames;
        }
    }
}

void tick_weapon_effects(float player_x, float player_y, float player_z)
{
    /* Updates smoke clouds and screen flash effects per frame */
    float smoke = 0.0f;

    if (g_weapon_use_cooldown > 0) g_weapon_use_cooldown--;
    if (g_screen_flash_timer > 0) g_screen_flash_timer--;
    if (g_screen_flash_timer <= 0) {
        g_screen_flash_timer = 0;
        g_screen_flash_duration = 0;
        g_screen_flash_strength = 0.0f;
    }

    for (int i = 0; i < MAX_SMOKE_CLOUDS; ++i) {
        SdkSmokeCloud* cloud = &g_smoke_clouds[i];
        float dx;
        float dy;
        float dz;
        float dist;
        float factor;

        if (!cloud->active) continue;
        cloud->timer--;
        if (cloud->timer <= 0) {
            cloud->active = false;
            continue;
        }

        dx = player_x - cloud->x;
        dy = player_y - cloud->y;
        dz = player_z - cloud->z;
        dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist > cloud->radius) continue;
        factor = 1.0f - api_clampf(dist / cloud->radius, 0.0f, 1.0f);
        smoke = fmaxf(smoke, factor);
    }

    g_player_smoke_obscurance = smoke;
}

void use_throwable_item(HotbarEntry* held,
                               float cam_x, float cam_y, float cam_z,
                               float dir_x, float dir_y, float dir_z)
{
    /* Throws a grenade/smoke item in the specified direction */
    float range;
    int hit_bx = 0;
    int hit_by = 0;
    int hit_bz = 0;
    int hit_face = 0;
    int prev_bx = 0;
    int prev_by = 0;
    int prev_bz = 0;
    float hit_dist = 0.0f;
    float impact_x;
    float impact_y;
    float impact_z;

    if (!held || held->count <= 0) return;
    range = throwable_range(held->item);
    if (raycast_block(cam_x, cam_y, cam_z, dir_x, dir_y, dir_z,
                      range, &hit_bx, &hit_by, &hit_bz, &hit_face,
                      &prev_bx, &prev_by, &prev_bz, &hit_dist)) {
        float dist = fmaxf(hit_dist - 0.15f, 0.5f);
        impact_x = cam_x + dir_x * dist;
        impact_y = cam_y + dir_y * dist;
        impact_z = cam_z + dir_z * dist;
    } else {
        impact_x = cam_x + dir_x * range;
        impact_y = cam_y + dir_y * range;
        impact_z = cam_z + dir_z * range;
    }

    switch (held->item) {
        case ITEM_HAND_GRANADE:
            damage_mobs_in_radius(impact_x, impact_y, impact_z, 5.2f, 20, 0);
            apply_player_radial_effect(cam_x, cam_y, cam_z, impact_x, impact_y, impact_z, 5.2f, 10, 0.25f, 16);
            break;
        case ITEM_SEMTEX:
            damage_mobs_in_radius(impact_x, impact_y, impact_z, 4.4f, 28, 0);
            apply_player_radial_effect(cam_x, cam_y, cam_z, impact_x, impact_y, impact_z, 4.4f, 14, 0.35f, 18);
            break;
        case ITEM_TACTICAL_GRANADE:
            damage_mobs_in_radius(impact_x, impact_y, impact_z, 7.0f, 2, 150);
            apply_player_radial_effect(cam_x, cam_y, cam_z, impact_x, impact_y, impact_z, 7.0f, 0, 1.0f, 50);
            break;
        case ITEM_SMOKE_GRANADE:
            spawn_smoke_cloud(impact_x, impact_y, impact_z, 7.5f, 420);
            break;
        default:
            break;
    }

    consume_hotbar_item(held, 1);
    g_weapon_use_cooldown = throwable_cooldown_frames(held->item);
}

void use_firearm_item(ItemType item,
                             float cam_x, float cam_y, float cam_z,
                             float dir_x, float dir_y, float dir_z)
{
    /* Fires a weapon in the specified direction, damaging mobs */
    float max_range = firearm_range(item);
    float block_dist = max_range;
    bool killed = false;
    float kill_x = 0.0f;
    float kill_y = 0.0f;
    float kill_z = 0.0f;
    MobType killed_type = MOB_ZOMBIE;
    int hit_bx;
    int hit_by;
    int hit_bz;
    int hit_face;
    int prev_bx;
    int prev_by;
    int prev_bz;

    if (!raycast_block(cam_x, cam_y, cam_z, dir_x, dir_y, dir_z,
                       max_range, &hit_bx, &hit_by, &hit_bz, &hit_face,
                       &prev_bx, &prev_by, &prev_bz, &block_dist)) {
        block_dist = max_range;
    }

    ranged_attack_mob(cam_x, cam_y, cam_z,
                      dir_x, dir_y, dir_z,
                      block_dist,
                      firearm_hit_radius(item),
                      firearm_damage(item),
                      &killed, &kill_x, &kill_y, &kill_z,
                      &killed_type);
    if (killed) {
        drop_loot_for_killed_mob(killed_type, kill_x, kill_y, kill_z);
    }
    g_weapon_use_cooldown = firearm_cooldown_frames(item);
}

int try_use_combat_item(HotbarEntry* held,
                               float cam_x, float cam_y, float cam_z,
                               float dir_x, float dir_y, float dir_z)
{
    /* Attempts to use a combat item (throwable or firearm) if conditions met */
    int wants_use;

    if (!held || held->count <= 0 || !sdk_item_is_combat_utility(held->item)) return 0;

    if (sdk_item_is_throwable(held->item)) {
        wants_use = sdk_input_action_pressed(&g_input_settings, SDK_INPUT_ACTION_BREAK_BLOCK) ||
                    sdk_window_was_mouse_pressed(g_sdk.window, 0);
        if (!wants_use) return 0;
        g_break_active = false;
        g_break_progress = 0;
        if (g_weapon_use_cooldown <= 0) {
            use_throwable_item(held, cam_x, cam_y, cam_z, dir_x, dir_y, dir_z);
        }
        return 1;
    }

    wants_use = (held->item == ITEM_ASSAULT_RIFLE)
        ? (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_BREAK_BLOCK) ||
           sdk_window_is_mouse_down(g_sdk.window, 0))
        : (sdk_input_action_pressed(&g_input_settings, SDK_INPUT_ACTION_BREAK_BLOCK) ||
           sdk_window_was_mouse_pressed(g_sdk.window, 0));
    if (!wants_use) return 0;
    g_break_active = false;
    g_break_progress = 0;
    if (g_weapon_use_cooldown <= 0) {
        use_firearm_item(held->item, cam_x, cam_y, cam_z, dir_x, dir_y, dir_z);
    }
    return 1;
}

/** Add an item to the hotbar. Stackable items stack; tools go in first empty slot. */
void hotbar_add(ItemType type)
{
    /* Adds an item to the hotbar, stacking if possible or finding empty slot */
    int stackable = item_is_stackable(type);

    if (stackable) {
        /* Try to stack on existing slot with same type */
        for (int i = 0; i < 10; i++) {
            if (g_hotbar[i].creative_block == BLOCK_AIR &&
                g_hotbar[i].payload_kind == SDK_ITEM_PAYLOAD_NONE &&
                g_hotbar[i].item == type && g_hotbar[i].count > 0) {
                g_hotbar[i].count++;
                return;
            }
        }
    }
    /* Find first empty slot */
    for (int i = 0; i < 10; i++) {
        if (g_hotbar[i].count <= 0) {
            g_hotbar[i].item = type;
            g_hotbar[i].creative_block = BLOCK_AIR;
            g_hotbar[i].count = 1;
            g_hotbar[i].durability = sdk_item_get_durability(type);
            return;
        }
    }
    /* Hotbar full ??? stack on selected slot anyway */
    clear_hotbar_entry(&g_hotbar[g_hotbar_selected]);
    g_hotbar[g_hotbar_selected].item = type;
    g_hotbar[g_hotbar_selected].creative_block = BLOCK_AIR;
    g_hotbar[g_hotbar_selected].count = 1;
    g_hotbar[g_hotbar_selected].durability = sdk_item_get_durability(type);
}

void hotbar_add_pickup(const SdkPickupItem* pickup)
{
    /* Adds a picked-up item to the hotbar with proper payload handling */
    int i;
    SdkConstructionItemPayload normalized_shaped;

    if (!pickup || pickup->count <= 0) return;

    if (pickup->payload_kind == SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION &&
        pickup->shaped.occupied_count > 0u) {
        normalized_shaped = pickup->shaped;
        sdk_construction_payload_refresh_metadata(&normalized_shaped);
        for (i = 0; i < 10; ++i) {
            if (g_hotbar[i].count <= 0) {
                clear_hotbar_entry(&g_hotbar[i]);
                g_hotbar[i].item = pickup->item;
                g_hotbar[i].creative_block = BLOCK_AIR;
                g_hotbar[i].count = 1;
                g_hotbar[i].durability = pickup->durability;
                g_hotbar[i].payload_kind = pickup->payload_kind;
                g_hotbar[i].shaped = normalized_shaped;
                return;
            }
        }
        clear_hotbar_entry(&g_hotbar[g_hotbar_selected]);
        g_hotbar[g_hotbar_selected].item = pickup->item;
        g_hotbar[g_hotbar_selected].count = 1;
        g_hotbar[g_hotbar_selected].durability = pickup->durability;
        g_hotbar[g_hotbar_selected].payload_kind = pickup->payload_kind;
        g_hotbar[g_hotbar_selected].shaped = normalized_shaped;
        return;
    }

    if (pickup->item != ITEM_NONE) {
        hotbar_add(pickup->item);
    }
}

void hotbar_set_creative_block(int slot, BlockType block)
{
    /* Sets a hotbar slot to place a specific block type in creative mode */
    if (slot < 0 || slot >= 10) return;
    clear_hotbar_entry(&g_hotbar[slot]);
    g_hotbar[slot].item = ITEM_NONE;
    g_hotbar[slot].creative_block = block;
    g_hotbar[slot].count = (block != BLOCK_AIR) ? 1 : 0;
    g_hotbar[slot].durability = 0;
}

void hotbar_set_shaped_payload(int slot, const SdkConstructionItemPayload* payload)
{
    /* Sets a shaped construction payload to a hotbar slot */
    SdkConstructionItemPayload normalized;

    if (slot < 0 || slot >= 10) return;
    if (!payload || payload->occupied_count == 0u) {
        clear_hotbar_entry(&g_hotbar[slot]);
        return;
    }

    normalized = *payload;
    sdk_construction_payload_refresh_metadata(&normalized);

    clear_hotbar_entry(&g_hotbar[slot]);
    g_hotbar[slot].item = ITEM_NONE;
    g_hotbar[slot].creative_block = BLOCK_AIR;
    g_hotbar[slot].count = 1;
    g_hotbar[slot].durability = 0;
    g_hotbar[slot].payload_kind = SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION;
    g_hotbar[slot].shaped = normalized;
}

void hotbar_set_item(int slot, ItemType item, int count)
{
    /* Sets a specific item and count to a hotbar slot */
    if (slot < 0 || slot >= 10) return;
    if (item == ITEM_NONE || count <= 0) {
        clear_hotbar_entry(&g_hotbar[slot]);
        return;
    }

    clear_hotbar_entry(&g_hotbar[slot]);
    g_hotbar[slot].item = item;
    g_hotbar[slot].creative_block = BLOCK_AIR;
    g_hotbar[slot].count = count;
    g_hotbar[slot].durability = sdk_item_get_durability(item);
}

int hotbar_get_place_block(const HotbarEntry* entry, BlockType* out_block, int* out_consume)
{
    /* Gets the block type and consumption flag for placement from a hotbar entry */
    BlockType block = BLOCK_AIR;
    int consume = 0;

    if (!entry || entry->count <= 0) return 0;
    if (entry->payload_kind == SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION) return 0;
    if (entry->creative_block != BLOCK_AIR) {
        block = entry->creative_block;
        consume = 0;
    } else if (sdk_item_is_block(entry->item)) {
        block = sdk_item_to_block(entry->item);
        consume = 1;
    }

    if (block == BLOCK_AIR) return 0;
    if (out_block) *out_block = block;
    if (out_consume) *out_consume = consume;
    return 1;
}

int spawn_item_to_mob_type(ItemType item, MobType* out_type)
{
    /* Converts a spawn item type to the corresponding mob type */
    MobType mob_type;

    switch (item) {
        case ITEM_SPAWNER_BUILDER:    mob_type = MOB_BUILDER; break;
        case ITEM_SPAWNER_BLACKSMITH: mob_type = MOB_BLACKSMITH; break;
        case ITEM_SPAWNER_MINER:      mob_type = MOB_MINER; break;
        case ITEM_SPAWNER_SOLDIER:    mob_type = MOB_SOLDIER; break;
        case ITEM_SPAWNER_GENERAL:    mob_type = MOB_GENERAL; break;
        case ITEM_SPAWNER_CAR:        mob_type = MOB_CAR; break;
        case ITEM_SPAWNER_MOTORBIKE:  mob_type = MOB_MOTORBIKE; break;
        case ITEM_SPAWNER_TANK:       mob_type = MOB_TANK; break;
        default:
            return 0;
    }

    if (out_type) *out_type = mob_type;
    return 1;
}

int use_spawn_item_at(HotbarEntry* held, int wx, int wy, int wz)
{
    /* Uses a spawn item to spawn a mob at world coordinates */
    MobType mob_type;

    if (!held || held->count <= 0) return 0;
    if (!spawn_item_to_mob_type(held->item, &mob_type)) return 0;
    if (get_block_at(wx, wy, wz) != BLOCK_AIR) return 0;
    if (!sdk_entity_spawn_mob(&g_sdk.entities, (float)wx + 0.5f, (float)wy, (float)wz + 0.5f, mob_type)) {
        return 0;
    }

    held->count--;
    if (held->count <= 0) {
        clear_hotbar_entry(held);
    }
    return 1;
}

SdkEntity* mounted_vehicle_entity(void)
{
    /* Returns pointer to currently mounted vehicle entity or NULL if none */
    if (g_mounted_vehicle_index < 0 || g_mounted_vehicle_index >= ENTITY_MAX) {
        g_mounted_vehicle_index = -1;
        return NULL;
    }

    if (!g_sdk.entities.entities[g_mounted_vehicle_index].active ||
        g_sdk.entities.entities[g_mounted_vehicle_index].kind != ENTITY_MOB ||
        !sdk_entity_is_vehicle(g_sdk.entities.entities[g_mounted_vehicle_index].mob_type)) {
        g_mounted_vehicle_index = -1;
        return NULL;
    }

    return &g_sdk.entities.entities[g_mounted_vehicle_index];
}

void sync_camera_to_vehicle(const SdkEntity* vehicle,
                                   float* cam_x, float* cam_y, float* cam_z,
                                   float* look_x, float* look_y, float* look_z)
{
    /* Syncs camera position to follow a vehicle entity */
    float eye_y;
    float cos_p;
    float sin_p;

    if (!vehicle) return;

    eye_y = vehicle->py + sdk_entity_vehicle_eye_height(vehicle->mob_type);
    cos_p = cosf(g_cam_pitch);
    sin_p = sinf(g_cam_pitch);

    if (cam_x) *cam_x = vehicle->px;
    if (cam_y) *cam_y = eye_y;
    if (cam_z) *cam_z = vehicle->pz;
    if (look_x) *look_x = vehicle->px + g_cam_look_dist * cos_p * sinf(g_cam_yaw);
    if (look_y) *look_y = eye_y + g_cam_look_dist * sin_p;
    if (look_z) *look_z = vehicle->pz + g_cam_look_dist * cos_p * cosf(g_cam_yaw);
}

int find_mountable_vehicle(float px, float py, float pz,
                                  float look_x, float look_y, float look_z)
{
    /* Finds closest mountable vehicle within interaction range and facing */
    float dir_x = look_x - px;
    float dir_y = look_y - py;
    float dir_z = look_z - pz;
    float dir_len = sqrtf(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
    float best_dist_sq = 999999.0f;
    int best_index = -1;

    if (dir_len > 0.0001f) {
        dir_x /= dir_len;
        dir_y /= dir_len;
        dir_z /= dir_len;
    } else {
        dir_x = 0.0f;
        dir_y = 0.0f;
        dir_z = 1.0f;
    }

    for (int i = 0; i < ENTITY_MAX; ++i) {
        const SdkEntity* entity = &g_sdk.entities.entities[i];
        float dx;
        float dy;
        float dz;
        float dist_sq;
        float dot;
        float interact_range;

        if (!entity->active || entity->kind != ENTITY_MOB || !sdk_entity_is_vehicle(entity->mob_type)) {
            continue;
        }

        interact_range = sdk_entity_vehicle_interact_range(entity->mob_type);
        dx = entity->px - px;
        dy = (entity->py + sdk_entity_mob_height(entity->mob_type) * 0.5f) - py;
        dz = entity->pz - pz;
        dist_sq = dx * dx + dy * dy + dz * dz;
        if (dist_sq > interact_range * interact_range) continue;

        dot = dx * dir_x + dy * dir_y + dz * dir_z;
        if (dot < 0.15f) continue;

        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best_index = i;
        }
    }

    return best_index;
}

void dismount_vehicle(float* cam_x, float* cam_y, float* cam_z,
                             float* look_x, float* look_y, float* look_z)
{
    /* Dismounts current vehicle and teleports player to side of vehicle */
    SdkEntity* vehicle = mounted_vehicle_entity();
    float right_x = cosf(g_cam_yaw);
    float right_z = -sinf(g_cam_yaw);
    float world_x;
    float world_y;
    float world_z;

    if (!vehicle) {
        g_mounted_vehicle_index = -1;
        return;
    }

    world_x = vehicle->px + right_x * VEHICLE_DISMOUNT_OFFSET;
    world_y = vehicle->py;
    world_z = vehicle->pz + right_z * VEHICLE_DISMOUNT_OFFSET;
    g_mounted_vehicle_index = -1;
    teleport_player_to(world_x, world_y, world_z, cam_x, cam_y, cam_z, look_x, look_y, look_z);
}
