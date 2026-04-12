# SDK Items Documentation

Comprehensive documentation for the SDK Items module providing item type classification, tool properties, and consumable helpers.

**Module:** `SDK/Core/Items/`  
**Header:** `SDK/Core/Items/sdk_item.h`

## Table of Contents

- [Module Overview](#module-overview)
- [Item Categories](#item-categories)
- [Block Items](#block-items)
- [Tools](#tools)
- [Combat Items](#combat-items)
- [Consumables](#consumables)
- [Materials](#materials)
- [Spawners](#spawners)
- [Key Functions](#key-functions)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Items module provides lightweight inline helper functions for working with the `ItemType` enum. It handles classification (is this a block? a tool? food?), property queries (durability, speed, attack damage), and conversions between items and blocks.

**Key Features:**
- Item-to-block and block-to-item conversion
- Tool classification and tier detection
- Weapon and combat utility detection
- Food and nutrition values
- Material and resource categorization
- NPC spawner item helpers

---

## Item Categories

### Item Type Ranges

| Category | Range Start | Range End | Detection Function |
|----------|-------------|-----------|-------------------|
| Block items | `ITEM_BLOCK_GRASS` | `ITEM_BLOCK_MAX` | `sdk_item_is_block()` |
| Wood tools | `ITEM_WOOD_PICKAXE` | `ITEM_WOOD_SWORD` | `sdk_item_get_tool_tier() == TIER_WOOD` |
| Stone tools | `ITEM_STONE_PICKAXE` | `ITEM_STONE_SWORD` | `sdk_item_get_tool_tier() == TIER_STONE` |
| Iron tools | `ITEM_IRON_PICKAXE` | `ITEM_IRON_CHISEL` | `sdk_item_get_tool_tier() == TIER_IRON` |
| Firearms | `ITEM_PISTOL` | `ITEM_SNIPER_RIFLE` | `sdk_item_is_firearm()` |
| Throwables | `ITEM_HAND_GRANADE` | `ITEM_SMOKE_GRANADE` | `sdk_item_is_throwable()` |
| Food | `ITEM_RAW_MEAT` | `ITEM_BERRIES` | `sdk_item_is_food()` |
| Spawners | `ITEM_SPAWNER_BUILDER` | `ITEM_SPAWNER_TANK` | `sdk_item_is_spawn_item()` |

---

## Block Items

### Block Item Conversion

Block items are items that place blocks in the world. The module provides bidirectional conversion:

```c
// Check if an item is a block item
int sdk_item_is_block(ItemType type);

// Convert item type to block type
BlockType sdk_item_to_block(ItemType type);

// Convert block type to item type (drop item)
ItemType sdk_block_to_item(BlockType type);
```

### Supported Block Items

The following block types have corresponding item types:

**Natural/Terrain:**
- `ITEM_BLOCK_GRASS` → `BLOCK_GRASS`
- `ITEM_BLOCK_DIRT` → `BLOCK_DIRT`
- `ITEM_BLOCK_STONE` → `BLOCK_STONE`
- `ITEM_BLOCK_SAND` → `BLOCK_SAND`
- `ITEM_BLOCK_WATER` → `BLOCK_WATER`
- `ITEM_BLOCK_SNOW` → `BLOCK_SNOW`
- `ITEM_BLOCK_GRAVEL` → `BLOCK_GRAVEL`
- `ITEM_BLOCK_BEDROCK` → `BLOCK_BEDROCK`

**Vegetation:**
- `ITEM_BLOCK_LOG` → `BLOCK_LOG`
- `ITEM_BLOCK_LEAVES` → `BLOCK_LEAVES`
- `ITEM_BLOCK_BERRY_BUSH` → `BLOCK_BERRY_BUSH`
- `ITEM_BLOCK_REEDS` → `BLOCK_REEDS`

**Crafting/Processing:**
- `ITEM_BLOCK_CRAFTING_TABLE` → `BLOCK_CRAFTING_TABLE`
- `ITEM_BLOCK_FURNACE` → `BLOCK_FURNACE`
- `ITEM_BLOCK_CAMPFIRE` → `BLOCK_CAMPFIRE`
- `ITEM_BLOCK_ANVIL` → `BLOCK_ANVIL`
- `ITEM_BLOCK_BLACKSMITHING_TABLE` → `BLOCK_BLACKSMITHING_TABLE`
- `ITEM_BLOCK_LEATHERWORKING_TABLE` → `BLOCK_LEATHERWORKING_TABLE`

**Building Materials:**
- `ITEM_BLOCK_PLANKS`, `ITEM_BLOCK_COBBLESTONE`
- `ITEM_BLOCK_BRICK`, `ITEM_BLOCK_STONE_BRICKS`
- `ITEM_BLOCK_CONCRETE`, `ITEM_BLOCK_REINFORCED_CONCRETE`
- `ITEM_BLOCK_THATCH`, `ITEM_BLOCK_WOOD_SHINGLES`
- `ITEM_BLOCK_WATTLE_DAUB`, `ITEM_BLOCK_ADOBE`, `ITEM_BLOCK_MUDBRICK`
- `ITEM_BLOCK_PLASTER_WHITE`, `ITEM_BLOCK_PLASTER_OCHRE`
- And more...

**Ores and Resources:**
- `ITEM_BLOCK_COAL_ORE` → `BLOCK_COAL_ORE`
- `ITEM_BLOCK_IRON_ORE` → `BLOCK_IRON_ORE`
- `ITEM_BLOCK_CRUSHED_STONE` → `BLOCK_CRUSHED_STONE`
- `ITEM_BLOCK_COMPACTED_FILL` → `BLOCK_COMPACTED_FILL`

---

## Tools

### Tool Classification

```c
// Check if item is any tool
int sdk_item_is_tool(ItemType type);

// Get tool class (pickaxe, axe, shovel, sword, saw, chisel)
ToolClass sdk_item_get_tool_class(ItemType type);

// Get tool material tier
ToolTier sdk_item_get_tool_tier(ItemType type);
```

### Tool Classes

| Class | Items | Purpose |
|-------|-------|---------|
| `TOOL_PICKAXE` | Wood, Stone, Iron pickaxes | Mining stone, ores |
| `TOOL_AXE` | Wood, Stone, Iron axes | Chopping wood |
| `TOOL_SHOVEL` | Wood, Stone, Iron shovels | Digging dirt, sand |
| `TOOL_SWORD` | Wood, Stone swords | Combat |
| `TOOL_SAW` | Iron saw | Crafting, construction |
| `TOOL_CHISEL` | Iron chisel | Stone working |

### Tool Tiers

| Tier | Tools | Durability | Speed Multiplier |
|------|-------|------------|------------------|
| `TIER_WOOD` | Wood pickaxe, axe, shovel, sword | 60 | 2.0x |
| `TIER_STONE` | Stone pickaxe, axe, shovel, sword | 132 | 4.0x |
| `TIER_IRON` | Iron pickaxe, axe, shovel, saw, chisel | 280 | 6.0x |
| `TIER_HAND` | None (fallback) | 0 | 1.0x |

### Tool Properties

```c
// Get tool durability (uses before breaking)
int sdk_item_get_durability(ItemType type);

// Get mining speed multiplier
float sdk_item_get_speed(ItemType type);

// Check if tool matches block preference
int sdk_tool_matches_block(ToolClass tool, BlockToolPref pref);
```

### Tool-Block Matching

```c
// Example: Check if player has right tool for block
ToolClass player_tool = sdk_item_get_tool_class(held_item);
BlockToolPref block_pref = sdk_block_get_tool_preference(target_block);

if (sdk_tool_matches_block(player_tool, block_pref)) {
    // Tool is effective against this block
    float speed = sdk_item_get_speed(held_item);
}
```

---

## Combat Items

### Firearms

```c
// Check if item is a firearm
int sdk_item_is_firearm(ItemType type);

// Firearm attack damage:
// - Pistol: 2 damage
// - Assault Rifle: 3 damage
// - Sniper Rifle: 4 damage
```

### Throwables

```c
// Check if item is throwable
int sdk_item_is_throwable(ItemType type);

// Throwable types:
// - ITEM_HAND_GRANADE
// - ITEM_SEMTEX
// - ITEM_TACTICAL_GRANADE
// - ITEM_SMOKE_GRANADE
```

### Combat Utility

```c
// Check if item is any combat item (firearm OR throwable)
int sdk_item_is_combat_utility(ItemType type);
```

### Attack Damage

```c
// Get base attack damage for item
int sdk_item_get_attack(ItemType type);

// Damage values:
// - Wood sword: 4
// - Stone sword: 5
// - Pistols: 2
// - Assault rifles: 3
// - Sniper rifles: 4
// - Default: 1
```

---

## Consumables

### Food Items

```c
// Check if item is food
int sdk_item_is_food(ItemType type);

// Get nutrition value (hunger restored)
int sdk_item_get_nutrition(ItemType type);
```

### Food Types

| Food | Nutrition | Notes |
|------|-----------|-------|
| `ITEM_RAW_MEAT` | 3 | From animals |
| `ITEM_COOKED_MEAT` | 8 | Cooked version |
| `ITEM_BERRIES` | 2 | Foraged |

---

## Materials

### Raw Materials

The following materials don't place blocks but are used in crafting:

| Item | Typical Source | Use |
|------|---------------|-----|
| `ITEM_STICK` | Wood processing | Tool handles |
| `ITEM_COAL` | Mining, smelting | Fuel, torches |
| `ITEM_IRON_INGOT` | Smelting iron ore | Tools, weapons |
| `ITEM_IRONSTONE` | Mining | Raw iron source |
| `ITEM_CLAY` | Digging | Bricks, pottery |
| `ITEM_LIMESTONE` | Mining | Construction |
| `ITEM_AGGREGATE` | Crushing stone | Concrete |
| `ITEM_COPPER_ORE` | Mining | Smelting |
| `ITEM_SULFUR` | Mining | Gunpowder |
| `ITEM_TUNGSTEN_ORE` | Mining | Advanced tools |
| `ITEM_BAUXITE` | Mining | Aluminum source |
| `ITEM_LEAD_ZINC_ORE` | Mining | Bullets, batteries |
| `ITEM_SALT` | Evaporation | Preserving food |
| `ITEM_HIDE` | Hunting | Leather working |

---

## Spawners

### NPC Spawner Items

```c
// Check if item is a spawner
int sdk_item_is_spawn_item(ItemType type);
```

### Spawner Types

| Spawner | Spawns | Profession |
|---------|--------|------------|
| `ITEM_SPAWNER_BUILDER` | Builder NPC | Construction |
| `ITEM_SPAWNER_BLACKSMITH` | Blacksmith NPC | Metal working |
| `ITEM_SPAWNER_MINER` | Miner NPC | Resource gathering |
| `ITEM_SPAWNER_SOLDIER` | Soldier NPC | Combat |
| `ITEM_SPAWNER_GENERAL` | General NPC | Command |

### Vehicle Spawners

| Spawner | Spawns |
|---------|--------|
| `ITEM_SPAWNER_CAR` | Automobile |
| `ITEM_SPAWNER_MOTORBIKE` | Motorcycle |
| `ITEM_SPAWNER_TANK` | Military tank |

---

## Key Functions

### Classification Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_item_is_block` | `(ItemType) → int` | Check if item places a block |
| `sdk_item_is_tool` | `(ItemType) → int` | Check if item is a tool |
| `sdk_item_is_firearm` | `(ItemType) → int` | Check if item is a gun |
| `sdk_item_is_throwable` | `(ItemType) → int` | Check if item can be thrown |
| `sdk_item_is_combat_utility` | `(ItemType) → int` | Check if firearm or throwable |
| `sdk_item_is_food` | `(ItemType) → int` | Check if item is edible |
| `sdk_item_is_spawn_item` | `(ItemType) → int` | Check if item spawns NPCs/vehicles |

### Property Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_item_to_block` | `(ItemType) → BlockType` | Convert item to block type |
| `sdk_block_to_item` | `(BlockType) → ItemType` | Get item that drops from block |
| `sdk_item_get_tool_class` | `(ItemType) → ToolClass` | Get tool category |
| `sdk_item_get_tool_tier` | `(ItemType) → ToolTier` | Get material tier |
| `sdk_item_get_durability` | `(ItemType) → int` | Get uses before breaking |
| `sdk_item_get_speed` | `(ItemType) → float` | Get mining speed multiplier |
| `sdk_item_get_attack` | `(ItemType) → int` | Get attack damage |
| `sdk_item_get_nutrition` | `(ItemType) → int` | Get food value |
| `sdk_item_get_name` | `(ItemType) → const char*` | Get human-readable name |
| `sdk_item_get_color` | `(ItemType) → uint32_t` | Get display color (ARGB) |

### Utility Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_tool_matches_block` | `(ToolClass, BlockToolPref) → int` | Check tool effectiveness |

---

## API Surface

### Public Header

```c
#ifndef NQLSDK_ITEM_H
#define NQLSDK_ITEM_H

#include "../World/Blocks/sdk_block.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Classification */
static inline int sdk_item_is_block(ItemType type);
static inline int sdk_item_is_tool(ItemType type);
static inline int sdk_item_is_firearm(ItemType type);
static inline int sdk_item_is_throwable(ItemType type);
static inline int sdk_item_is_combat_utility(ItemType type);
static inline int sdk_item_is_food(ItemType type);
static inline int sdk_item_is_spawn_item(ItemType type);

/* Conversion */
static inline BlockType sdk_item_to_block(ItemType type);
static inline ItemType sdk_block_to_item(BlockType type);

/* Tool properties */
static inline ToolClass sdk_item_get_tool_class(ItemType type);
static inline ToolTier sdk_item_get_tool_tier(ItemType type);
static inline int sdk_item_get_durability(ItemType type);
static inline float sdk_item_get_speed(ItemType type);

/* Combat properties */
static inline int sdk_item_get_attack(ItemType type);
static inline int sdk_tool_matches_block(ToolClass tool, BlockToolPref pref);

/* Consumable properties */
static inline int sdk_item_get_nutrition(ItemType type);

/* Display */
static inline const char* sdk_item_get_name(ItemType type);
static inline uint32_t sdk_item_get_color(ItemType type);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_ITEM_H */
```

---

## Integration Notes

### Hotbar Implementation

```c
void update_hotbar_display(ItemType hotbar[10], int selected) {
    for (int i = 0; i < 10; i++) {
        ItemType item = hotbar[i];
        
        // Get display info
        const char* name = sdk_item_get_name(item);
        uint32_t color = sdk_item_get_color(item);
        
        // Draw slot
        draw_hotbar_slot(i, name, color, i == selected);
    }
}
```

### Tool Effectiveness Check

```c
float compute_mining_speed(ItemType held_item, BlockType target_block) {
    // Get tool info
    ToolClass tool = sdk_item_get_tool_class(held_item);
    BlockToolPref pref = sdk_block_get_tool_preference(target_block);
    
    // Check if right tool
    if (!sdk_tool_matches_block(tool, pref)) {
        return 1.0f; // Base hand speed
    }
    
    // Return tool speed
    return sdk_item_get_speed(held_item);
}
```

### Durability Tracking

```c
typedef struct {
    ItemType type;
    int remaining_durability;
} ToolInstance;

void on_tool_use(ToolInstance* tool) {
    int max_durability = sdk_item_get_durability(tool->type);
    
    tool->remaining_durability--;
    
    if (tool->remaining_durability <= 0) {
        // Tool breaks
        play_break_sound();
        remove_from_inventory(tool);
    }
}
```

### Item Comparison

```c
int items_are_similar(ItemType a, ItemType b) {
    // Same type
    if (a == b) return 1;
    
    // Both blocks
    if (sdk_item_is_block(a) && sdk_item_is_block(b)) {
        // Compare block properties
        BlockType block_a = sdk_item_to_block(a);
        BlockType block_b = sdk_item_to_block(b);
        return sdk_block_get_class(block_a) == sdk_block_get_class(block_b);
    }
    
    // Both tools of same class
    if (sdk_item_is_tool(a) && sdk_item_is_tool(b)) {
        return sdk_item_get_tool_class(a) == sdk_item_get_tool_class(b);
    }
    
    return 0;
}
```

---

## AI Context Hints

### Custom Item Type

```c
// Add a new item type to the enum (requires recompilation)
typedef enum {
    // ... existing items ...
    ITEM_DIAMOND_PICKAXE,  // New: beyond iron tier
    ITEM_BLOCK_MAX
} ItemType;

// Extend helper functions for new item
static inline ToolTier sdk_item_get_tool_tier(ItemType type) {
    // ... existing tiers ...
    if (type == ITEM_DIAMOND_PICKAXE) return TIER_DIAMOND;
    return TIER_HAND;
}

static inline int sdk_item_get_durability(ItemType type) {
    switch (sdk_item_get_tool_tier(type)) {
        // ... existing cases ...
        case TIER_DIAMOND: return 800;
        default: return 0;
    }
}

static inline float sdk_item_get_speed(ItemType type) {
    switch (sdk_item_get_tool_tier(type)) {
        // ... existing cases ...
        case TIER_DIAMOND: return 10.0f;
        default: return 1.0f;
    }
}
```

### Item Database Extension

```c
// Extended item info beyond what sdk_item.h provides
typedef struct {
    ItemType type;
    const char* description;
    const char* category;
    int stack_size;
    bool craftable;
    ItemType recipe[9];  // 3x3 crafting grid
} ExtendedItemInfo;

ExtendedItemInfo* get_extended_info(ItemType type) {
    static ExtendedItemInfo database[ITEM_BLOCK_MAX] = {
        [ITEM_IRON_PICKAXE] = {
            .type = ITEM_IRON_PICKAXE,
            .description = "A sturdy iron pickaxe for mining stone and ores.",
            .category = "Tools",
            .stack_size = 1,
            .craftable = true,
            .recipe = {
                ITEM_IRON_INGOT, ITEM_IRON_INGOT, ITEM_IRON_INGOT,
                ITEM_NONE, ITEM_STICK, ITEM_NONE,
                ITEM_NONE, ITEM_STICK, ITEM_NONE
            }
        },
        // ... more items ...
    };
    
    return &database[type];
}
```

### Inventory Sorting

```c
int compare_items_by_category(const void* a, const void* b) {
    ItemType item_a = *(ItemType*)a;
    ItemType item_b = *(ItemType*)b;
    
    // Define category priority
    int get_category_priority(ItemType type) {
        if (sdk_item_is_block(type)) return 1;
        if (sdk_item_is_tool(type)) return 2;
        if (sdk_item_is_food(type)) return 3;
        if (sdk_item_is_combat_utility(type)) return 4;
        return 5; // Materials
    }
    
    int prio_a = get_category_priority(item_a);
    int prio_b = get_category_priority(item_b);
    
    return prio_a - prio_b;
}

void sort_inventory(ItemType* inventory, int count) {
    qsort(inventory, count, sizeof(ItemType), compare_items_by_category);
}
```

### Tool Comparison Helper

```c
typedef struct {
    ItemType type;
    float efficiency;  // durability * speed
} ToolRanking;

void rank_tools_by_efficiency(ToolRanking* rankings, int* count) {
    *count = 0;
    
    ItemType tools[] = {
        ITEM_WOOD_PICKAXE, ITEM_STONE_PICKAXE, ITEM_IRON_PICKAXE,
        ITEM_WOOD_AXE, ITEM_STONE_AXE, ITEM_IRON_AXE,
        // ... etc
    };
    
    for (int i = 0; i < sizeof(tools)/sizeof(tools[0]); i++) {
        ItemType tool = tools[i];
        int durability = sdk_item_get_durability(tool);
        float speed = sdk_item_get_speed(tool);
        
        rankings[*count] = (ToolRanking){
            .type = tool,
            .efficiency = durability * speed
        };
        (*count)++;
    }
    
    // Sort by efficiency descending
    qsort(rankings, *count, sizeof(ToolRanking), 
          compare_by_efficiency_desc);
}
```

---

## Related Documentation

- [SDK_Blocks.md](../World/Blocks/SDK_Blocks.md) - Block types and properties
- [SDK_Crafting.md](../Crafting/SDK_Crafting.md) - Recipe system
- [SDK_CreativeInventory.md](../CreativeModeInventory/SDK_CreativeInventory.md) - Item selection UI

---

**Source File:**
- `SDK/Core/Items/sdk_item.h` (13KB) - All inline helper functions
