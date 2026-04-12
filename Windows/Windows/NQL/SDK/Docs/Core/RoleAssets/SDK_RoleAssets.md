# SDK RoleAssets Documentation

Comprehensive documentation for the SDK RoleAssets module providing runtime resolution of NPC roles and building types to authored character and prop assets.

**Module:** `SDK/Core/RoleAssets/`  
**Output:** `SDK/Docs/Core/RoleAssets/SDK_RoleAssets.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Architecture](#architecture)
- [Role-to-Asset Bindings](#role-to-asset-bindings)
- [Building-to-Prop Bindings](#building-to-prop-bindings)
- [Key Functions](#key-functions)
- [Global State](#global-state)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The RoleAssets module bridges game logic (NPC roles, building types) with authored asset content (characters, props). It maintains static binding tables that map runtime enums to asset IDs, then validates those assets exist in the loaded asset libraries.

**Key Features:**
- Static role-to-character bindings (6 NPC roles)
- Dynamic building-to-prop bindings (via BuildingType)
- Asset existence validation
- Missing asset reporting
- Integration with settlement and entity systems

---

## Architecture

### Resolution Flow

```
Settlement spawns NPC with role:
           │
           ├──► sdk_role_assets_resolve_character(SDK_NPC_ROLE_BLACKSMITH, &missing)
           │           │
           │           ├──► Lookup in g_role_asset_bindings table
           │           │       └──► Returns "blacksmith"
           │           │
           │           ├──► Calls refresh_character_assets()
           │           │
           │           └──► Searches g_character_assets[] for "blacksmith"
           │                   ├──► Found: *missing = 0, return "blacksmith"
           │                   └──► Not found: *missing = 1, return "blacksmith"
           │
           └──► Use asset ID for character instantiation
```

### Building Prop Resolution

```
Building system needs prop for building type:
           │
           ├──► sdk_role_assets_resolve_prop(BUILDING_TYPE_SMITHY, &missing)
           │           │
           │           ├──► Calls sdk_building_default_prop_id(type)
           │           │       └──► Returns prop asset ID from building definitions
           │           │
           │           ├──► Calls refresh_prop_assets()
           │           │
           │           └──► Searches g_prop_assets[] for asset ID
           │                   ├──► Found: *missing = 0
           │                   └──► Not found: *missing = 1
           │
           └──► Use prop asset ID for building visualization
```

---

## Role-to-Asset Bindings

### Character Role Bindings

| SDKNpcRole | Asset ID | Description |
|------------|----------|-------------|
| `SDK_NPC_ROLE_COMMONER` | `settler_commoner` | Default villager |
| `SDK_NPC_ROLE_BUILDER` | `builder` | Construction worker |
| `SDK_NPC_ROLE_BLACKSMITH` | `blacksmith` | Smithing NPC |
| `SDK_NPC_ROLE_MINER` | `miner` | Mining worker |
| `SDK_NPC_ROLE_FOREMAN` | `foreman` | Work supervisor |
| `SDK_NPC_ROLE_SOLDIER` | `soldier_placeholder` | Combat NPC |

**Binding Table Definition:**

```c
static const SdkRoleAssetEntry g_role_asset_bindings[] = {
    { SDK_NPC_ROLE_COMMONER,   "settler_commoner" },
    { SDK_NPC_ROLE_BUILDER,    "builder" },
    { SDK_NPC_ROLE_BLACKSMITH, "blacksmith" },
    { SDK_NPC_ROLE_MINER,      "miner" },
    { SDK_NPC_ROLE_FOREMAN,    "foreman" },
    { SDK_NPC_ROLE_SOLDIER,    "soldier_placeholder" }
};
```

### Asset Resolution Behavior

```c
const char* sdk_role_assets_resolve_character(SdkNpcRole role, int* out_missing)
```

**Parameters:**
- `role` - The NPC role to resolve
- `out_missing` - Output flag: 1 if asset not found, 0 if found (can be NULL)

**Returns:**
- Asset ID string (always valid, even if asset missing)
- Empty string "" if role has no binding

**Usage Pattern:**

```c
int missing;
const char* asset_id = sdk_role_assets_resolve_character(npc->role, &missing);

if (missing) {
    // Log warning, use fallback
    log_warning("Missing character asset: %s", asset_id);
    asset_id = "settler_commoner";  // Fallback
}

// Instantiate character with asset_id
spawn_character(asset_id, position);
```

---

## Building-to-Prop Bindings

### Building Types

Building-to-prop resolution is delegated to the Building module:

```c
const char* sdk_building_default_prop_id(BuildingType type);
```

Common building types include:
- `BUILDING_TYPE_HOUSE` - Residential building
- `BUILDING_TYPE_SMITHY` - Blacksmith workshop
- `BUILDING_TYPE_MINE` - Mining operation
- `BUILDING_TYPE_FARM` - Agricultural building
- `BUILDING_TYPE_BARRACKS` - Military building

### Prop Resolution Behavior

```c
const char* sdk_role_assets_resolve_prop(BuildingType type, int* out_missing)
```

**Flow:**
1. Query building module for default prop ID for building type
2. Refresh prop asset library
3. Search for prop asset by ID
4. Set `*out_missing` based on existence
5. Return asset ID (or empty string if no prop defined)

---

## Key Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_role_assets_resolve_character` | `(SdkNpcRole, int* out_missing) → const char*` | Resolve role to character asset ID |
| `sdk_role_assets_resolve_prop` | `(BuildingType, int* out_missing) → const char*` | Resolve building type to prop asset ID |

---

## Global State

```c
// Static binding table (read-only)
static const SdkRoleAssetEntry g_role_asset_bindings[];

// Asset libraries (external, from Frontend)
extern SdkCharacterAsset g_character_assets[];  // From sdk_frontend_assets.c
extern int g_character_asset_count;
extern SdkPropAsset g_prop_assets[];
extern int g_prop_asset_count;
```

---

## API Surface

### Public Header (sdk_role_assets.h)

```c
/**
 * Resolve runtime role to authored character asset ID.
 * 
 * @param role       The NPC role to resolve
 * @param out_missing Optional output: set to 1 if asset not found, 0 if found
 * @return           Asset ID string (empty if role unbound)
 */
const char* sdk_role_assets_resolve_character(SdkNpcRole role, int* out_missing);

/**
 * Resolve building type to authored prop asset ID.
 * 
 * @param type       The building type
 * @param out_missing Optional output: set to 1 if asset not found, 0 if found  
 * @return           Asset ID string (empty if no prop defined)
 */
const char* sdk_role_assets_resolve_prop(BuildingType type, int* out_missing);
```

### Types Used

```c
// From sdk_entity.h
typedef enum SdkNpcRole {
    SDK_NPC_ROLE_COMMONER,
    SDK_NPC_ROLE_BUILDER,
    SDK_NPC_ROLE_BLACKSMITH,
    SDK_NPC_ROLE_MINER,
    SDK_NPC_ROLE_FOREMAN,
    SDK_NPC_ROLE_SOLDIER,
    // ... potentially more
} SdkNpcRole;

// From sdk_settlement_types.h (or sdk_building_family.h)
typedef enum BuildingType {
    BUILDING_TYPE_HOUSE,
    BUILDING_TYPE_SMITHY,
    // ... etc
} BuildingType;
```

---

## Integration Notes

### Settlement System Integration

```c
// When settlement spawns an NPC
void settlement_spawn_npc(Settlement* s, SdkNpcRole role, SdkVec3 pos) {
    int missing;
    const char* asset_id = sdk_role_assets_resolve_character(role, &missing);
    
    if (missing) {
        log_settlement(s, "Missing character asset for role %d: %s", role, asset_id);
        // Use fallback or skip spawning
        asset_id = "settler_commoner";
    }
    
    SdkEntity* npc = entity_manager_spawn_npc(asset_id, pos);
    npc->settlement = s;
    npc->role = role;
}
```

### Building System Integration

```c
// When constructing a building
void building_construct(BuildingType type, SdkVec3 pos, SdkVec3 size) {
    int missing;
    const char* prop_id = sdk_role_assets_resolve_prop(type, &missing);
    
    if (missing) {
        log_warning("Missing prop asset for building type %d: %s", type, prop_id);
        // Use placeholder or basic block representation
    }
    
    // Create building entity
    SdkEntity* building = entity_manager_spawn_prop(prop_id, pos);
    building->building_type = type;
    
    // Add to settlement
    settlement_add_building(g_player_settlement, building);
}
```

### Asset Loading Integration

```c
// Ensure assets are loaded before resolution
void game_init() {
    // Load character assets from %APPDATA%/NQL/Characters/
    refresh_character_assets();
    
    // Load prop assets from asset library
    refresh_prop_assets();
    
    // Now role resolution will find assets
}
```

---

## AI Context Hints

### Adding New Roles

1. **Add to SdkNpcRole enum** (in sdk_entity.h or appropriate header):
   ```c
   typedef enum SdkNpcRole {
       // ... existing roles ...
       SDK_NPC_ROLE_MERCCHANT,  // New role
       SDK_NPC_ROLE_COUNT       // Keep last
   } SdkNpcRole;
   ```

2. **Add binding in sdk_role_assets.c:**
   ```c
   static const SdkRoleAssetEntry g_role_asset_bindings[] = {
       // ... existing bindings ...
       { SDK_NPC_ROLE_MERCCHANT, "merchant" },
   };
   ```

3. **Create character asset:**
   - Create folder `%APPDATA%/NQL/Characters/merchant/`
   - Add `character.txt` metadata
   - Add `model.obj` mesh
   - Add `animations/` folder with `.anim` files

4. **Refresh assets** or restart game

### Adding Role Variants

For multiple visual variants of the same role:

```c
// Extend resolution to support variants
const char* sdk_role_assets_resolve_character_variant(
    SdkNpcRole role, 
    int variant_index, 
    int* out_missing)
{
    static const char* commoner_variants[] = {
        "settler_commoner",
        "settler_commoner_2",
        "settler_female"
    };
    
    if (role == SDK_NPC_ROLE_COMMONER) {
        variant_index %= ARRAY_SIZE(commoner_variants);
        const char* asset_id = commoner_variants[variant_index];
        *out_missing = (find_character_asset(asset_id) < 0);
        return asset_id;
    }
    
    // Fall back to standard resolution
    return sdk_role_assets_resolve_character(role, out_missing);
}
```

### Conditional Asset Selection

Select assets based on settlement properties:

```c
const char* select_character_for_settlement(SdkNpcRole role, Settlement* s) {
    int missing;
    const char* base_id = sdk_role_assets_resolve_character(role, &missing);
    
    if (s->population > 100) {
        // Try high-tier variant for large settlements
        char tier2_id[64];
        snprintf(tier2_id, sizeof(tier2_id), "%s_tier2", base_id);
        
        if (find_character_asset(tier2_id) >= 0) {
            return tier2_id;  // Use tier 2 if available
        }
    }
    
    return base_id;  // Use standard
}
```

### Missing Asset Fallback Chain

```c
const char* resolve_character_with_fallback(SdkNpcRole role) {
    int missing;
    const char* primary = sdk_role_assets_resolve_character(role, &missing);
    
    if (!missing) return primary;
    
    // Try role-specific fallback
    switch (role) {
        case SDK_NPC_ROLE_BLACKSMITH:
            if (find_character_asset("worker") >= 0) return "worker";
            break;
        case SDK_NPC_ROLE_SOLDIER:
            if (find_character_asset("guard") >= 0) return "guard";
            break;
        // ... etc
    }
    
    // Universal fallback
    return "settler_commoner";
}
```

### Runtime Asset Reloading

```c
void reload_role_assets() {
    // Re-scan asset directories
    refresh_character_assets();
    refresh_prop_assets();
    
    // Re-validate existing NPCs
    for (int i = 0; i < g_entity_count; i++) {
        SdkEntity* e = &g_entities[i];
        if (e->type == SDK_ENTITY_NPC) {
            int missing;
            sdk_role_assets_resolve_character(e->role, &missing);
            
            if (missing) {
                log_entity(e, "NPC has missing character asset after reload");
            }
        }
    }
}
```

---

## Related Documentation

- `SDK/Core/Entities/` - Entity system, NPC roles
- `SDK/Core/World/Settlements/` - Settlement system
- `SDK/Core/World/Buildings/` - Building types and definitions
- `SDK/Docs/Core/Frontend/SDK_Frontend.md` - Asset browser, character/prop loading
- `SDK/Core/API/Session/` - Character asset runtime functions

---

**Source Files:**
- `SDK/Core/RoleAssets/sdk_role_assets.h` (511 bytes) - Public API
- `SDK/Core/RoleAssets/sdk_role_assets.c` (2,156 bytes) - Implementation with 6 role bindings
