# SDK Creative Inventory System

## Overview

The Creative Inventory system provides searchable, filterable access to all blocks and items for creative mode gameplay. It supports text search, category filtering, and configurable shape dimensions for construction payloads.

**File Location:** `SDK/Core/CreativeModeInventory/sdk_creative_inventory.c` (150 lines)  
**Public API:** `creative_entry_for_filtered_index()`, `creative_visible_entry_count()`, `creative_item_grant_count()`  
**Dependencies:** Block system, Item system, Pause Menu (UI state), Hotbar system

## Architecture

### Data Flow

```
Pause Menu UI (Creative Mode view)
       │
       ├──► Text Search ──► g_creative_menu_search
       ├──► Filter Mode ──► g_creative_menu_filter
       └──► Scroll/Select ──► g_creative_menu_selected
                              │
                              ▼
              creative_entry_for_filtered_index(index)
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
    block_matches_creative_search()   item_matches_creative_search()
              │                               │
              ▼                               ▼
       Iterate BLOCK_COUNT             Iterate g_creative_spawn_items[]
       (all block types)               (15 predefined spawn items)
              │                               │
              └───────────────┬───────────────┘
                              ▼
                    Return CreativeEntry {kind, id}
                              │
                              ▼
              creative_grant_selected_entry() (in Pause Menu)
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
       hotbar_set_creative_block()      hotbar_set_item()
       hotbar_set_shaped_payload()      (with count from
                                        creative_item_grant_count())
```

### CreativeEntry Structure

```c
typedef struct {
    int kind;   // SDK_CREATIVE_ENTRY_BLOCK (0) or SDK_CREATIVE_ENTRY_ITEM (1)
    int id;     // BlockType or ItemType value
} CreativeEntry;
```

## Key Functions

### Entry Enumeration

**`CreativeEntry creative_entry_for_filtered_index(int index)`**
- Maps a UI list index to a filtered entry
- Iterates spawn items first, then blocks
- Skips entries that don't match current filter/search
- Returns `entry.kind = BLOCK_AIR / ITEM_NONE` for invalid indices

**`int creative_visible_entry_count(void)`**
- Returns total count of visible entries with current filter+search
- Used for scroll clamping and UI sizing
- Sum of: matching spawn items + matching blocks

### Search and Filter Logic

**`int block_matches_creative_search(BlockType block)`**

Checks if a block should be visible:
1. Validates block is in range (`BLOCK_AIR < block < BLOCK_COUNT`)
2. Applies filter mode:
   - `SDK_CREATIVE_FILTER_BUILDING_BLOCKS`: excludes color blocks (`sdk_block_is_color()`)
   - `SDK_CREATIVE_FILTER_COLORS`: only includes color blocks
   - `SDK_CREATIVE_FILTER_ITEMS`: blocks invisible (returns 0)
3. If search text exists: case-insensitive substring match on block name

**`int item_matches_creative_search(ItemType item)`**

Checks if a spawn item should be visible:
1. Validates item is not `ITEM_NONE`
2. Applies filter mode:
   - `SDK_CREATIVE_FILTER_BUILDING_BLOCKS`/`COLORS`: items invisible
   - `SDK_CREATIVE_FILTER_ITEMS`: shows items
3. If search text exists: case-insensitive substring match on item name

### Item Grant Quantities

**`int creative_item_grant_count(ItemType item)`**

Returns stack size for creative mode granting:

| Item Category | Count |
|--------------|-------|
| Throwable (grenades, etc.) | 8 |
| Firearm (pistol, rifle, etc.) | 1 |
| Spawn item (builder, soldier, tank, etc.) | 32 (`CREATIVE_SPAWNER_STACK`) |
| Other | 1 |

## Filter Modes

| Mode | Value | Shows Blocks | Shows Items | Description |
|------|-------|--------------|-------------|-------------|
| `SDK_CREATIVE_FILTER_ALL` | 0 | All non-color | All | Default mode |
| `SDK_CREATIVE_FILTER_BUILDING_BLOCKS` | 1 | Non-color only | None | Excludes color palette blocks |
| `SDK_CREATIVE_FILTER_COLORS` | 2 | Color blocks only | None | Only color palette blocks |
| `SDK_CREATIVE_FILTER_ITEMS` | 3 | None | All | Spawn items and tools only |

**Prop Editor Behavior:**
When `g_session_kind == SDK_SESSION_KIND_PROP_EDITOR`, the filter is locked to `SDK_CREATIVE_FILTER_BUILDING_BLOCKS` (non-color blocks only).

## Search System

**Text Normalization:**
- `normalize_label()` converts search and item names:
  - Underscores (`_`) → spaces (` `)
  - Uppercase → lowercase
- Enables matching "stone_bricks" with "stone bricks" query

**Search Buffer:**
- `g_creative_menu_search[SDK_PAUSE_MENU_SEARCH_MAX]` (32 chars)
- Populated by Pause Menu UI from window char queue
- Empty search shows all items in current filter

## Spawn Items List

The 15 predefined spawn items available in creative mode:

```c
const ItemType g_creative_spawn_items[] = {
    ITEM_PISTOL,
    ITEM_ASSAULT_RIFLE,
    ITEM_SNIPER_RIFLE,
    ITEM_HAND_GRANADE,
    ITEM_SEMTEX,
    ITEM_TACTICAL_GRANADE,
    ITEM_SMOKE_GRANADE,
    ITEM_SPAWNER_BUILDER,
    ITEM_SPAWNER_BLACKSMITH,
    ITEM_SPAWNER_MINER,
    ITEM_SPAWNER_SOLDIER,
    ITEM_SPAWNER_GENERAL,
    ITEM_SPAWNER_CAR,
    ITEM_SPAWNER_MOTORBIKE,
    ITEM_SPAWNER_TANK
};
```

These are the only items visible in creative mode (along with all blocks).

## Integration with Pause Menu

### UI State Variables

```c
// Selection state
int g_creative_menu_selected;      // Highlighted entry index
int g_creative_menu_scroll;        // Scroll offset for long lists

// Filter and search
int  g_creative_menu_filter;       // Current filter mode (0-3)
char g_creative_menu_search[32];   // Search text buffer
int  g_creative_menu_search_len;   // Current search length

// Shape configuration (for construction payloads)
int g_creative_shape_focus;        // 0=list panel, 1=shape panel
int g_creative_shape_row;          // 0=width, 1=height, 2=depth
int g_creative_shape_width;        // 1-16 blocks
int g_creative_shape_height;       // 1-16 blocks
int g_creative_shape_depth;        // 1-16 blocks
```

### Granting Flow

From `PauseMenu/sdk_pause_menu.c`:

```c
void creative_grant_selected_entry(void) {
    CreativeEntry selected = creative_entry_for_filtered_index(g_creative_menu_selected);
    
    if (selected.kind == SDK_CREATIVE_ENTRY_BLOCK && selected.id != BLOCK_AIR) {
        if (shape is 16x16x16) {
            // Single block to hotbar
            hotbar_set_creative_block(g_hotbar_selected, (BlockType)selected.id);
        } else {
            // Construction payload with dimensions
            SdkConstructionItemPayload payload;
            sdk_construction_payload_make_box(
                (BlockType)selected.id,
                g_creative_shape_width,
                g_creative_shape_height,
                g_creative_shape_depth,
                &payload);
            hotbar_set_shaped_payload(g_hotbar_selected, &payload);
        }
    } else if (selected.kind == SDK_CREATIVE_ENTRY_ITEM && selected.id != ITEM_NONE) {
        // Item with appropriate count
        hotbar_set_item(g_hotbar_selected, (ItemType)selected.id,
                        creative_item_grant_count((ItemType)selected.id));
    }
}
```

## Global State

### Owned by Pause Menu (declared in `sdk_api.c`, extern in `sdk_api_internal.h`)

```c
extern int g_creative_menu_selected;
extern int g_creative_menu_scroll;
extern int g_creative_menu_filter;
extern char g_creative_menu_search[SDK_PAUSE_MENU_SEARCH_MAX];
extern int g_creative_menu_search_len;
extern int g_creative_shape_focus;
extern int g_creative_shape_row;
extern int g_creative_shape_width;
extern int g_creative_shape_height;
extern int g_creative_shape_depth;
```

### Cross-Module Dependencies

```c
// Block system
extern BlockType;                          // Block enum from sdk_block.h
extern sdk_block_get_name(BlockType);      // Name lookup
extern sdk_block_is_color(BlockType);      // Color block check

// Item system  
extern ItemType;                           // Item enum from sdk_item.h
extern sdk_item_get_name(ItemType);        // Name lookup
extern sdk_item_is_throwable(ItemType);    // Throwable check
extern sdk_item_is_firearm(ItemType);      // Firearm check
extern sdk_item_is_spawn_item(ItemType);   // Spawner check

// Hotbar system (declared in sdk_api.c)
extern void hotbar_set_creative_block(int slot, BlockType block);
extern void hotbar_set_item(int slot, ItemType item, int count);
extern void hotbar_set_shaped_payload(int slot, const SdkConstructionItemPayload* payload);

// Session kind (for Prop Editor filter lock)
extern SdkSessionKind g_session_kind;
```

## AI Context Hints

### Adding New Creative Items

1. **Add item to `g_creative_spawn_items[]`** in `sdk_creative_inventory.c`
2. **Ensure item has name** in `sdk_item_get_name()` (Items/sdk_item.h)
3. **Grant count** is automatically determined by `creative_item_grant_count()`

### Adding New Filter Modes

1. **Add new filter constant** (convention: `SDK_CREATIVE_FILTER_*`)
2. **Update `block_matches_creative_search()`** - add filter logic
3. **Update `item_matches_creative_search()`** - add filter logic
4. **Update Pause Menu UI** - add filter option to navigation

### Performance Considerations

- `creative_entry_for_filtered_index()` iterates from start each call
- For large lists, this is O(n) per visible entry
- Current implementation is fine for ~100 blocks + 15 items
- If expanding to thousands of items, consider caching filtered results

### Shape Configuration

The 16x16x16 default shape maps to single block placement. Smaller dimensions create `SdkConstructionItemPayload` for the construction system (see `ConstructionCells` module).

**Shape clamping:**
- `creative_clamp_shape_state()` ensures 1-16 range
- `creative_clamp_selection()` ensures valid selection index

---

**Related Documentation:**
- `SDK/Docs/Core/PauseMenu/SDK_PauseMenu.md` - UI presentation and input handling
- `SDK/Docs/Core/Items/SDK_Items.md` - Item type definitions and helpers
- `SDK/Docs/Core/World/Blocks/SDK_Blocks.md` - Block type definitions
- `SDK/Docs/Core/World/ConstructionCells/SDK_ConstructionSystem.md` - Construction payload system
