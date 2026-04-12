# SDK Entity System

## Overview

The Entity system manages item drops (pickups), mobs (zombies, animals), NPCs (settlement workers), and vehicles. It handles physics, AI behavior, spawning, combat, and auto-pickup. Entities are organized into an `SdkEntityList` with a fixed maximum capacity.

**Files:**
- `SDK/Core/Entities/sdk_entity.h` (215 lines) - Type definitions and API
- `SDK/Core/Entities/sdk_entity.c` (873 lines) - Implementation

**Public API:** `sdk_entity_spawn_item()`, `sdk_entity_spawn_mob()`, `sdk_entity_tick_all()`, `sdk_entity_player_attack()`  
**Dependencies:** Item system, Block system, Construction Cells, Hotbar, Settlement system

## Architecture

### Data Flow

```
Item Drops:
    Block Break / Mob Kill / Station Removal
              │
              ├──► sdk_entity_spawn_item() ──► SdkEntityList
              │      - Position + random scatter velocity
              │      - ITEM_DROP_VEL_Y upward pop (0.15f)
              │      - drop_item, drop_display_block
              │
              ├──► sdk_entity_spawn_shaped_item() ──► Construction payload
              │      - BlockType material + SdkConstructionItemPayload
              │
              └──► sdk_entity_tick_all() per frame
                        │
                        ├──► Gravity (0.02f)
                        ├──► Ground collision (solid_fn callback)
                        ├──► Horizontal friction (0.9x)
                        ├──► Magnet pull (within 2.5 blocks)
                        └──► Auto-pickup (within 1.5 blocks)
                                    │
                                    ▼
                        Add to pickup_buf → hotbar_add_pickup()

Mob Lifecycle:
    Spawner Item Use / World Generation
              │
              ├──► sdk_entity_spawn_mob() ──► SdkEntityList
              │      - MobType, health, colors
              │      - Role (for NPCs)
              │      - mob_dir_x/z = 0, 1 (facing +Z)
              │
              └──► tick_mob() per frame (in sdk_entity_tick_all)
                        │
                        ├──► AI Behavior (mob-specific)
                        │    - Zombie: Aggro to player, pathfinding, jump
                        │    - Animals: Wander, flee when hurt/nearby
                        │    - Humanoid NPCs: Wander, flee when hurt
                        │    - Settlement NPCs: Task-based to target_wx/y/z
                        │    - Vehicles: Inherit velocity, drift damping
                        ├──► Physics: Gravity, ground collision
                        ├──► Despawn (distance-based, type-specific)
                        └──► Contact damage (zombies only)

Combat:
    Player Attack (left-click)
          │
          ├──► sdk_entity_player_attack()
          │    - Hit detection (range + facing cone, dot > 0)
          │    - Damage application
          │    - Knockback (MOB_KNOCKBACK = 0.3f)
          │    - Death check → sdk_entity_remove() → spawn drops
          └──► Mob Contact Damage (zombies)
               - 1.2 block range
               - MOB_ZOMBIE_DAMAGE = 3 HP
               - Cooldown 60 ticks
```

### SdkEntity Structure

```c
typedef struct {
    /* Core */
    EntityKind kind;           // ENTITY_NONE, ENTITY_ITEM, ENTITY_MOB
    float px, py, pz;          // World position
    float vx, vy, vz;          // Velocity
    int age;                   // Ticks since spawn
    bool active;               // Slot in use flag

    /* Item-specific */
    ItemType drop_item;        // ITEM_NONE for shaped
    uint16_t drop_display_block; // Block visual for rendering
    uint8_t drop_payload_kind;   // SDK_ITEM_PAYLOAD_*
    uint8_t drop_reserved0;
    SdkConstructionItemPayload drop_shaped; // Construction data
    float spin;                // Visual Y rotation

    /* Mob-specific */
    MobType mob_type;          // MOB_ZOMBIE, MOB_BOAR, etc.
    int mob_health, mob_max_health;
    int mob_attack_cd;         // Ticks until next attack
    int mob_hurt_timer;        // Red flash duration (MOB_HURT_FLASH)
    uint32_t mob_color;        // Primary color (ARGB)
    uint32_t mob_color_secondary; // Secondary color
    int mob_ai_timer;          // State change countdown
    float mob_dir_x, mob_dir_z; // Facing direction (normalized)

    /* NPC/Settlement */
    uint8_t mob_behavior;      // SDK_ENTITY_BEHAVIOR_*
    uint8_t mob_role;          // SDK_NPC_ROLE_*
    uint8_t mob_task;          // SDK_NPC_TASK_*
    uint8_t mob_flags;
    uint32_t settlement_id;    // Owning settlement
    uint32_t resident_id;      // Settlement resident index

    /* Locations (block coordinates) */
    int32_t home_wx, home_wy, home_wz;   // Home position
    int32_t work_wx, work_wy, work_wz;   // Work position
    int32_t target_wx, target_wy, target_wz; // Current target
    int32_t action_wx, action_wy, action_wz; // Action position

    /* Task state */
    int task_stage;            // Sub-state of current task
    int task_timer;            // Task timing
    int task_cooldown;         // Post-task delay
    int work_building_index;   // Work building reference
    int home_building_index;   // Home building reference

    /* NPC Inventory */
    int inventory_selected;    // Active slot (0-7)
    SdkNpcInventorySlot inventory[SDK_NPC_INVENTORY_SLOTS];
} SdkEntity;
```

### EntityKind Enum

```c
typedef enum {
    ENTITY_NONE = 0,
    ENTITY_ITEM,    // Dropped items with physics
    ENTITY_MOB,     // Living entities with AI
} EntityKind;
```

### SdkEntityList Structure

```c
#define ENTITY_MAX 256

typedef struct {
    SdkEntity entities[ENTITY_MAX];
    int count;  // Active entity count
} SdkEntityList;
```

**Note:** `SdkEntityList` is passed as a parameter (not global). The main world entity list is `g_sdk.entities` in `SdkApiState`.

## Key Subsystems

### 1. Item Physics System

**Physics Constants:**

| Constant | Value | Description |
|----------|-------|-------------|
| `ENTITY_GRAVITY` | 0.02f | Vertical acceleration per tick |
| `ENTITY_TERMINAL` | 1.0f | Max fall speed (clamped) |
| `ENTITY_FRICTION` | 0.9f | Horizontal velocity multiplier |
| `ITEM_DROP_VEL_Y` | 0.15f | Initial upward velocity on spawn |
| `ITEM_BOB_SPEED` | 0.05f | Visual bob frequency |
| `ITEM_BOB_AMP` | 0.1f | Visual bob amplitude |
| `ENTITY_MAGNET_SPEED` | 0.12f | Pull strength toward player |

**Spawn Velocity Pattern:**
```c
float hash = sinf(wx * 13.7f + wz * 7.3f + wy * 3.1f);
e->vx = hash * 0.06f;           // Random -0.06 to +0.06
e->vz = cosf(wx * 5.1f + wz * 11.3f) * 0.06f;
e->vy = ITEM_DROP_VEL_Y;        // 0.15f upward pop
```
Creates natural scatter pattern based on position hash.

**Pickup Ranges:**
- **Magnet:** `ITEM_MAGNET_RANGE = 2.5f` blocks - items drift toward player
- **Auto-pickup:** `ITEM_PICKUP_RANGE = 1.5f` blocks - immediate collection

**Lifetime:**
- `ITEM_LIFETIME = 600` ticks (~10 seconds at 60fps)
- Items despawn after lifetime expires if not collected

### 2. Mob Type System

**MobType Enum:**

```c
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
```

**Mob Stats Table:**

| Mob | Health | Speed | Width | Height | Hit Range | Despawn |
|-----|--------|-------|-------|--------|-----------|---------|
| Zombie | 20 | 0.03 | 0.3 | 1.8 | 1.5 | 32 |
| Boar | 14 | 0.024 | 0.45 | 0.9 | 1.8 | 40 |
| Deer | 12 | 0.032 | 0.35 | 1.5 | 1.8 | 40 |
| Commoner | 20 | 0.022 | 0.3 | 1.8 | 1.6 | 96 |
| Builder | 20 | 0.022 | 0.3 | 1.8 | 1.6 | 96 |
| Blacksmith | 20 | 0.022 | 0.3 | 1.8 | 1.6 | 96 |
| Miner | 20 | 0.022 | 0.3 | 1.8 | 1.6 | 96 |
| Foreman | 24 | 0.022 | 0.3 | 1.8 | 1.6 | 96 |
| Soldier | 28 | 0.022 | 0.3 | 1.8 | 1.6 | 96 |
| General | 32 | 0.022 | 0.3 | 1.8 | 1.6 | 96 |
| Car | 48 | 0.18 | 0.9 | 1.25 | 2.2 | 128 |
| Motorbike | 28 | 0.22 | 0.45 | 1.1 | 1.8 | 128 |
| Tank | 96 | 0.12 | 1.15 | 1.55 | 2.8 | 128 |

**Mob Classification:**

```c
bool sdk_entity_is_humanoid(MobType type);   // Zombie, Commoner-Generals
bool sdk_entity_is_vehicle(MobType type);    // Car, Motorbike, Tank
```

**Vehicle Properties:**

| Vehicle | Eye Height | Interact Range |
|---------|------------|----------------|
| Car | 1.35f | 3.0f |
| Motorbike | 1.2f | 2.75f |
| Tank | 1.7f | 3.5f |

**Mob Colors:** Each mob has ARGB primary and secondary colors for rendering.

### 3. AI Behavior System

**Zombie AI:**
```
if distance to player < MOB_ZOMBIE_AGGRO (16 blocks):
    Walk toward player
    if obstacle ahead and space above:
        Jump (vy = 0.3f)
else:
    Decelerate (0.8x damping)

if contact with player and cooldown expired:
    Deal MOB_ZOMBIE_DAMAGE (3 HP)
    Reset cooldown (60 ticks)
```

**Animal AI (Boar, Deer):**
```
if player nearby (8 blocks) or recently hurt:
    Flee from player (1.25x speed)
    mob_ai_timer = 40
else if mob_ai_timer <= 0:
    Pick random wander direction
    mob_ai_timer = 90-180 (random)
else:
    Wander in current direction (0.55x speed)
    mob_ai_timer--
```

**Humanoid NPC AI (Non-Settlement):**
```
if hurt_timer > 0 and player far enough:
    Flee from player (1.15x speed)
    mob_ai_timer = 30
else if mob_ai_timer <= 0:
    Pick random wander direction
    mob_ai_timer = 100-180 (random)
else:
    Wander in current direction (0.45x speed)
    mob_ai_timer--

if obstacle ahead and space above:
    Jump (vy = 0.28f)
```

**Settlement NPC AI:**
```
if target set and not at target:
    Walk toward target_wx/y/z (0.82x speed)
    if obstacle ahead and clear above:
        Jump (vy = 0.30f)
    else if blocked at head level:
        Strafe perpendicular (0.55x speed)
else:
    Slow down (0.55x damping)
    
Settlement NPCs never despawn (settlement_controlled flag)
```

**Vehicle Physics:**
```
// No AI - purely physics-based
if moving:
    mob_dir = normalize(vx, vz)  // Face movement direction
else:
    vx, vz *= 0.82f  // Drift/damping
```

**Pathfinding Notes:**
- Simple steering toward target/player
- Auto-jump when obstacle detected ahead
- No path planning - direct movement with obstacle avoidance
- Obstacle detection: `is_solid_fn(ahead_x, feet_y, ahead_z)`

### 4. NPC Settlement System

**NPC Roles:**

```c
typedef enum {
    SDK_NPC_ROLE_NONE = 0,
    SDK_NPC_ROLE_COMMONER,
    SDK_NPC_ROLE_BUILDER,
    SDK_NPC_ROLE_BLACKSMITH,
    SDK_NPC_ROLE_MINER,
    SDK_NPC_ROLE_FOREMAN,
    SDK_NPC_ROLE_SOLDIER
} SdkNpcRole;
```

**Role Assignment on Spawn:**
- Commoner → `SDK_NPC_ROLE_COMMONER`
- Builder → `SDK_NPC_ROLE_BUILDER`
- Blacksmith → `SDK_NPC_ROLE_BLACKSMITH`
- Miner → `SDK_NPC_ROLE_MINER`
- Foreman → `SDK_NPC_ROLE_FOREMAN`
- Soldier → `SDK_NPC_ROLE_SOLDIER`
- General → (no specific role, higher HP)

**NPC Tasks:**

```c
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
```

**Behavior Modes:**
```c
typedef enum {
    SDK_ENTITY_BEHAVIOR_DEFAULT = 0,
    SDK_ENTITY_BEHAVIOR_SETTLEMENT
} SdkEntityBehaviorMode;
```

**Settlement Control Functions:**

| Function | Purpose |
|----------|---------|
| `sdk_entity_set_settlement_control()` | Assign NPC to settlement with role |
| `sdk_entity_clear_settlement_control()` | Release NPC, reset to default behavior |
| `sdk_entity_set_controlled_target()` | Set target position and task |
| `sdk_entity_clear_controlled_target()` | Clear target, set fallback task |
| `sdk_entity_is_at_target()` | Check if within radius of target |

**NPC Inventory:**

```c
typedef struct {
    ItemType item;
    int count;
    int durability;
} SdkNpcInventorySlot;

#define SDK_NPC_INVENTORY_SLOTS 8
```

- `inventory_selected` tracks active slot
- `sdk_entity_try_pickup_near()` for auto-collection

### 5. Combat System

**Player Attack (`sdk_entity_player_attack`):**

```
Inputs:
- px, py, pz: Player position
- look_x/y/z: Camera target (attack direction)
- attack_damage: Weapon damage

Algorithm:
1. Calculate attack direction vector
2. For each active mob:
   a. Check distance < mob_hit_range()
   b. Check roughly in front (dot(attack_dir, mob_dir) > 0)
   c. Track nearest mob
3. If mob found:
   a. Apply damage
   b. Set mob_hurt_timer = MOB_HURT_FLASH (10)
   c. Apply knockback away from player
   d. If health <= 0: remove and report kill
4. Return damage dealt
```

**Combat Constants:**
- `MOB_HURT_FLASH = 10` ticks (red flash duration)
- `MOB_KNOCKBACK = 0.3f` horizontal velocity push
- `MOB_ZOMBIE_DAMAGE = 3` contact damage
- `MOB_ZOMBIE_ATTACK_CD = 60` ticks between attacks

**Mob Death:**
- `sdk_entity_remove()` clears slot
- Kill position reported for loot spawning
- `killed_type` reported for statistics/achievements

## API Surface

### Initialization and Spawning

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_entity_init` | `(SdkEntityList*) → void` | Zero all entities, reset count |
| `sdk_entity_spawn_item` | `(list, wx, wy, wz, ItemType) → SdkEntity*` | Spawn item drop with scatter velocity |
| `sdk_entity_spawn_shaped_item` | `(list, wx, wy, wz, BlockType, payload*) → SdkEntity*` | Spawn construction item |
| `sdk_entity_spawn_mob` | `(list, wx, wy, wz, MobType) → SdkEntity*` | Spawn mob with type-specific stats |
| `sdk_entity_remove` | `(list, index) → void` | Despawn entity, compact count |

### Ticking and Updates

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_entity_tick_all` | `(list, player_x/y/z, pickup_buf, pickup_count, buf_cap, is_solid_fn, mob_damage_out) → void` | Update all entities per frame |

### Queries

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_entity_is_humanoid` | `(MobType) → bool` | Check if type is humanoid (zombie, NPCs) |
| `sdk_entity_is_vehicle` | `(MobType) → bool` | Check if type is vehicle |
| `sdk_entity_mob_width` | `(MobType) → float` | Collision width |
| `sdk_entity_mob_height` | `(MobType) → float` | Collision height |
| `sdk_entity_vehicle_speed` | `(MobType) → float` | Max vehicle speed |
| `sdk_entity_vehicle_eye_height` | `(MobType) → float` | Camera offset when driving |
| `sdk_entity_vehicle_interact_range` | `(MobType) → float` | Mounting range |
| `sdk_entity_mob_name` | `(MobType) → const char*` | Type name string |
| `sdk_entity_npc_role_name` | `(SdkNpcRole) → const char*` | Role name string |
| `sdk_entity_npc_task_name` | `(SdkNpcTaskKind) → const char*` | Task name string |

### Combat

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_entity_player_attack` | `(list, px, py, pz, look_x/y/z, damage, killed, kill_x/y/z, killed_type) → int` | Melee attack nearest mob in front of player |

### Settlement/NPC Control

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_entity_set_settlement_control` | `(entity, settlement_id, resident_id, role) → void` | Assign NPC to settlement |
| `sdk_entity_clear_settlement_control` | `(entity) → void` | Release from settlement |
| `sdk_entity_set_controlled_target` | `(entity, wx, wy, wz, task) → void` | Set task target |
| `sdk_entity_clear_controlled_target` | `(entity, fallback_task) → void` | Clear target |
| `sdk_entity_is_at_target` | `(entity, radius_xy, radius_y) → int` | Check if at target position |
| `sdk_entity_try_pickup_near` | `(list, collector, out_item) → int` | NPC auto-collect nearby items |

## Integration Notes

### World Integration

**Collision Callback:**
```c
typedef bool (*EntitySolidFn)(int wx, int wy, int wz);
```
Passed to `sdk_entity_tick_all()` for:
- Ground collision (snap to block top)
- Pathfinding obstacle detection
- Jump decisions

**Typical Implementation:**
```c
bool is_solid_at(int wx, int wy, int wz) {
    BlockType b = get_block_at(wx, wy, wz);
    return sdk_block_is_solid(b);  // From Block system
}
```

**Void Kill:** Entities with `py < 0.0f` are automatically removed.

### Hotbar Integration

**Auto-pickup Flow:**
```c
SdkPickupItem pickup_buf[32];
int pickup_count;
int mob_damage;

sdk_entity_tick_all(&g_sdk.entities, player_x, player_y, player_z,
                    pickup_buf, &pickup_count, 32,
                    is_solid_at, &mob_damage);

for (int i = 0; i < pickup_count; i++) {
    hotbar_add_pickup(&pickup_buf[i]);
}
```

**Shaped Items:** Construction payloads are preserved through `drop_shaped` → `SdkPickupItem.shaped`.

### Settlement Integration

**Settlement System Usage:**
```c
// On settlement spawn
SdkEntity* npc = sdk_entity_spawn_mob(&g_sdk.entities, x, y, z, MOB_BUILDER);
sdk_entity_set_settlement_control(npc, settlement_id, resident_idx, SDK_NPC_ROLE_BUILDER);

// On task assignment
sdk_entity_set_controlled_target(npc, target_wx, target_wy, target_wz, SDK_NPC_TASK_BUILD);

// Per-frame check
if (sdk_entity_is_at_target(npc, 0.5f, 1.0f)) {
    // Task complete, next state
}
```

**Home/Work Positions:** Set during settlement initialization for NPC routing.

### Vehicle Integration

**Mounting:**
```c
if (sdk_entity_is_vehicle(mob_type)) {
    float range = sdk_entity_vehicle_interact_range(mob_type);
    // Check distance < range, then mount
}
```

**Camera Sync:**
```c
if (driving_vehicle) {
    eye_y += sdk_entity_vehicle_eye_height(vehicle_type);
}
```

**Vehicle Physics:**
- Acceleration applied via `vx, vz`
- Natural damping (0.82x per tick)
- Direction derived from velocity

### Spawner Items

**Item to Mob Mapping:**
- `ITEM_BUILDER_SPAWNER` → `sdk_entity_spawn_mob(MOB_BUILDER)`
- Role auto-set from mob type
- Colors initialized per mob definition

## AI Context Hints

### Adding New Mob Types

1. **Add to `MobType` enum** in `sdk_entity.h`
2. **Add stats** in `sdk_entity.c` switch statements:
   - `mob_speed()` - Movement speed
   - `mob_despawn_range()` - Despawn distance
   - `mob_hit_range()` - Attack range
3. **Add dimensions** in `sdk_entity_mob_width/height()`
4. **Add colors** in `sdk_entity_spawn_mob()`:
   ```c
   case MOB_NEWTYPE:
       e->mob_health = e->mob_max_health = NEW_HEALTH;
       e->mob_color = 0xFFRRGGBB;
       e->mob_color_secondary = 0xFFRRGGBB;
       break;
   ```
5. **Add AI** in `tick_mob()`:
   - Check `sdk_entity_is_humanoid()` for shared behavior
   - Check `sdk_entity_is_vehicle()` for physics
   - Add custom behavior in else branch
6. **Add name** in `sdk_entity_mob_name()`

### Adding New NPC Roles

1. **Add to `SdkNpcRole` enum**
2. **Add name** in `sdk_entity_npc_role_name()`
3. **Set on spawn** for appropriate mob types in `sdk_entity_spawn_mob()`

### AI Pathfinding Algorithm

NPCs use simple steering without path planning:
```
if distance_to_target > threshold:
    direction = normalize(target - position)
    velocity = direction * speed * dampening
    
    // Obstacle avoidance
    ahead = position + direction * 0.55f
    if is_solid(ahead_x, feet_y, ahead_z):
        if !is_solid(ahead_x, feet_y+1, ahead_z):
            jump()  // vy = 0.28-0.30
        else:
            // Blocked, strafe perpendicular
            side = perpendicular(direction)
            velocity = side * speed * 0.55
else:
    // At target, slow down
    velocity *= 0.55
```

### Performance Considerations

- **O(n) iteration:** `sdk_entity_tick_all()` scans all `ENTITY_MAX` slots
- **Early exit:** Inactive entities skip immediately
- **Distance culling:** Despawn ranges vary (vehicles 128, NPCs 96, animals 40, zombies 32)
- **Hard limit:** `ENTITY_MAX = 256` total entities

### Shaped Item Serialization

When spawning shaped construction items:
```c
SdkConstructionItemPayload payload;
// ... populate payload ...
sdk_construction_payload_refresh_metadata(&payload);
SdkEntity* e = sdk_entity_spawn_shaped_item(list, x, y, z, material, &payload);
```
The `refresh_metadata()` call recalculates `occupied_count` and hints.

### Collision Detection

Ground collision uses block lookup:
```c
int bx = (int)floorf(e->px);
int by = (int)floorf(e->py - 0.01f);  // Slightly below entity
int bz = (int)floorf(e->pz);
if (is_solid_fn(bx, by, bz)) {
    e->py = (float)(by + 1);  // Snap to block top
    e->vy = 0;  // Reset vertical velocity
}
```

---

**Related Documentation:**
- `SDK/Core/Items/` - Item types and spawner items
- `SDK/Core/World/Blocks/` - Block collision and solidity
- `SDK/Core/World/ConstructionCells/` - Shaped construction items
- `SDK/Core/Hotbar/` - Pickup buffer integration
- `SDK/Core/Settlements/` - Settlement task system and building assignments
