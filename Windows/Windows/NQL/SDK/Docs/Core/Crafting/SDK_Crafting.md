# SDK Crafting and Station System

## Overview

The Crafting system provides shaped recipe matching, player crafting UI (2x2/3x3 grids), and processing stations (Furnace, Campfire, Anvil, Blacksmithing/Leatherworking tables). It supports recipe offset matching, bulk crafting, fuel-based processing, and full persistence.

**Files:**
- `SDK/Core/Crafting/sdk_crafting.h` (158 lines) - Recipe definitions and matching
- `SDK/Core/Crafting/sdk_crafting_ui.c` (280 lines) - Crafting UI and interactions
- `SDK/Core/Crafting/sdk_station_internal.h` (24 lines) - Station internal API
- `SDK/Core/Crafting/sdk_station_runtime.c` (520 lines) - Station logic and persistence

**Public API:** `sdk_crafting_match()`, `craft_update_match()`, `station_tick_all()`  
**Dependencies:** Item system, Block system, Persistence, Hotbar

## Architecture

### Data Flow

```
Player Input (C key)
       │
       ▼
Open Crafting UI ──► g_craft_open = true
       │
       ├──► 2x2 hand crafting (g_craft_is_table = false)
       └──► 3x3 table crafting (g_craft_is_table = true)
              │
              ▼
    Grid Items + Counts
    ┌─────────────────────┐
    │ g_craft_grid[9]     │ ItemType in each slot
    │ g_craft_grid_count[9]│ Stack count per slot
    └─────────────────────┘
              │
              ▼
    craft_update_match() ──► sdk_crafting_match()
              │
              ▼
    Recipe Database (g_recipes[])
    - 17 craftable recipes
    - 1x2, 3x3 patterns
    - Offset matching for smaller patterns
              │
              ▼
    g_craft_result_idx = recipe index or -1
              │
              ▼
    craft_take_result() ──► Consume grid items ──► hotbar_add()

Processing Stations:
    Block Right-Click ──► station_open_for_block()
       │
       ├──► Furnace ──► SDK_STATION_UI_FURNACE
       ├──► Campfire ──► SDK_STATION_UI_CAMPFIRE
       ├──► Anvil ──► SDK_STATION_UI_ANVIL
       ├──► Blacksmithing Table ──► SDK_STATION_UI_BLACKSMITHING
       └──► Leatherworking Table ──► SDK_STATION_UI_LEATHERWORKING
              │
              ▼
    StationState (per-block instance)
    ┌────────────────────────────────┐
    │ active, wx/wy/wz, block_type   │
    │ input_item, input_count        │
    │ fuel_item, fuel_count          │
    │ output_item, output_count      │
    │ progress, burn_remaining, burn_max│
    └────────────────────────────────┘
              │
              ├──► station_tick_all() ──► Process smelting/cooking
              └──► Persistence ──► save/load with world
```

### CraftRecipe Structure

```c
typedef struct {
    ItemType pattern[9];   // Row-major 3x3 (unused = ITEM_NONE)
    int      width;        // Pattern width 1-3
    int      height;       // Pattern height 1-3
    ItemType result;       // Output item
    int      result_count; // Stack size
} CraftRecipe;
```

### Recipe Database

`g_recipes[]` contains 17 recipes:

| Recipe | Pattern | Size | Result | Count |
|--------|---------|------|--------|-------|
| Sticks | 2 Planks vertical | 1x2 | ITEM_STICK | 4 |
| Furnace | Cobblestone ring | 3x3 | ITEM_BLOCK_FURNACE | 1 |
| Campfire | Logs/Sticks/Coal | 3x3 | ITEM_BLOCK_CAMPFIRE | 1 |
| Anvil | Iron/Cobblestone | 3x3 | ITEM_BLOCK_ANVIL | 1 |
| Blacksmithing Table | Iron + Planks | 3x3 | ITEM_BLOCK_BLACKSMITHING_TABLE | 1 |
| Leatherworking Table | Hide + Planks | 3x3 | ITEM_BLOCK_LEATHERWORKING_TABLE | 1 |
| Wood Pickaxe | PPP/_S_/_S_ | 3x3 | ITEM_WOOD_PICKAXE | 1 |
| Wood Axe | PP_/PS_/_S_ | 3x3 | ITEM_WOOD_AXE | 1 |
| Wood Shovel | _P_/_S_/_S_ | 3x3 | ITEM_WOOD_SHOVEL | 1 |
| Wood Sword | _P_/_P_/_S_ | 3x3 | ITEM_WOOD_SWORD | 1 |
| Stone Pickaxe | CCC/_S_/_S_ | 3x3 | ITEM_STONE_PICKAXE | 1 |
| Stone Axe | CC_/CS_/_S_ | 3x3 | ITEM_STONE_AXE | 1 |
| Stone Shovel | _C_/_S_/_S_ | 3x3 | ITEM_STONE_SHOVEL | 1 |
| Stone Sword | _C_/_C_/_S_ | 3x3 | ITEM_STONE_SWORD | 1 |
| Iron Pickaxe | III/_S_/_S_ | 3x3 | ITEM_IRON_PICKAXE | 1 |
| Iron Axe | II_/IS_/_S_ | 3x3 | ITEM_IRON_AXE | 1 |
| Iron Shovel | _I_/_S_/_S_ | 3x3 | ITEM_IRON_SHOVEL | 1 |

Pattern notation: P=Planks, C=Cobblestone, I=Iron Ingot, S=Stick, _=empty

### StationState Structure

```c
typedef struct {
    bool active;           // Slot in use
    int wx, wy, wz;        // World position
    BlockType block_type;  // FURNACE, CAMPFIRE, etc.
    ItemType input_item;   // Item being processed
    int input_count;
    ItemType fuel_item;    // Coal, Log, Planks, Reeds
    int fuel_count;
    ItemType output_item;  // Result item
    int output_count;
    int progress;          // 0 to progress_max
    int burn_remaining;    // Fuel ticks remaining
    int burn_max;          // Initial fuel value
} StationState;
```

## Key Functions

### Recipe Matching

**`int sdk_crafting_match(const ItemType* grid, int grid_w, int grid_h)`**

Matches a crafting grid against all recipes:
1. Iterates `g_recipes[]` array
2. For each recipe, tries all valid (ox, oy) offsets within grid
3. Checks that pattern matches at offset, empty cells match ITEM_NONE
4. Returns recipe index on first match, -1 if no match

**Algorithm:**
```c
for each recipe:
    for oy = 0 to (grid_h - recipe.height):
        for ox = 0 to (grid_w - recipe.width):
            if pattern matches at (ox, oy):
                return recipe_index
return -1
```

**`void craft_update_match(void)`**
- Normalizes grid (converts zero-count slots to ITEM_NONE)
- Calls `sdk_crafting_match()` with 2x2 or 3x3 depending on `g_craft_is_table`
- Updates `g_craft_result_idx`

### Crafting Operations

**`void craft_take_result(void)`**
- Validates `g_craft_result_idx >= 0`
- Re-runs match to find exact pattern offset
- Consumes one item per pattern slot
- Clears grid slot if count reaches zero
- Adds result to hotbar via `hotbar_add()`
- Re-runs `craft_update_match()` for bulk crafting

**`void craft_take_result_bulk(void)`**
- Repeatedly calls `craft_take_result()` until no match remains
- Efficient for crafting many items

**Grid Management:**
- `craft_place_from_hotbar()` - Move from hotbar to cursor position
- `craft_remove_to_hotbar()` - Return items to hotbar
- `craft_close()` - Close UI, clear grid to hotbar or drops

### Station Processing

**`int is_station_block(BlockType type)`**
Returns true for: `BLOCK_FURNACE`, `BLOCK_CAMPFIRE`, `BLOCK_ANVIL`, `BLOCK_BLACKSMITHING_TABLE`, `BLOCK_LEATHERWORKING_TABLE`

**`int is_processing_station_block(BlockType type)`**
Returns true for time-based processing: `BLOCK_FURNACE`, `BLOCK_CAMPFIRE`

**`void station_tick_all(void)`**

Called each frame to update all active stations:
```
for each active StationState:
    if processing station and has input:
        if burn_remaining > 0:
            burn_remaining--
            progress++
            if progress >= progress_max:
                produce output
                consume input
                reset progress
        else if has fuel in fuel slot:
            consume fuel
            burn_max = fuel_value
            burn_remaining = burn_max
    sync to persistence if changed
```

**`int station_fuel_value(ItemType item)`**

| Fuel | Ticks | Real-time |
|------|-------|-----------|
| Coal | 1200 | ~20 seconds |
| Log | 400 | ~6.7 seconds |
| Planks | 160 | ~2.7 seconds |
| Reeds | 80 | ~1.3 seconds |

**Processing Recipes:**

| Station | Input | Output | Time |
|---------|-------|--------|------|
| Furnace | Ironstone | Iron Ingot | 240 ticks (4s) |
| Furnace | Iron Ore | Iron Ingot | 240 ticks (4s) |
| Furnace | Raw Meat | Cooked Meat | 240 ticks (4s) |
| Campfire | Raw Meat | Cooked Meat | 420 ticks (7s) |

Constants: `FURNACE_SMELT_TIME = 240`, `CAMPFIRE_COOK_TIME = 420`

### Persistence

**`void station_sync_to_persistence(int index)`**
- Copies `StationState` to `SdkPersistedStationState`
- Calls `sdk_persistence_upsert_station_state()`
- Called automatically when state changes meaningfully

**`void station_load_all_from_persistence(void)`**
- Loads station count from persistence
- Iterates and populates `g_station_states[]`
- Called at world load

**`void station_remove_at(int wx, int wy, int wz)`**
- Called when station block is broken
- Spawns all items (input, fuel, output) as world entities
- Removes from persistence via `sdk_persistence_remove_station_state()`

## Global State

### Crafting State

```c
// UI state
extern bool g_craft_open;               // UI is visible
extern bool g_craft_is_table;           // 3x3 vs 2x2 grid

// Grid contents
extern ItemType g_craft_grid[9];        // Items in each slot
extern int g_craft_grid_count[9];       // Count per slot
extern int g_craft_cursor;              // Nav cursor position (0-8)
extern int g_craft_result_idx;          // Matched recipe (-1 = none)

// Input tracking
extern bool g_craft_key_was_down;
extern bool g_craft_lmb_was_down;
extern bool g_craft_rmb_was_down;
extern bool g_craft_nav_was_down[6];  // Up, down, left, right, place, take
extern bool g_craft_result_lmb_was_down;
extern bool g_craft_result_rmb_was_down;
```

Grid indexing (row-major):
```
0 1 2
3 4 5
6 7 8
```

### Station State

```c
#define MAX_STATION_STATES 256

extern StationState g_station_states[MAX_STATION_STATES];
extern int g_station_state_count;
extern bool g_station_open;
extern SdkStationUIKind g_station_open_kind;
extern BlockType g_station_open_block_type;
extern int g_station_open_index;        // Index into g_station_states[]
extern int g_station_hovered_slot;        // 0=input, 1=fuel, 2=output, -1=none
extern bool g_station_lmb_was_down;
extern bool g_station_rmb_was_down;
```

**Station UI Kind Values:**
- `SDK_STATION_UI_NONE = 0`
- `SDK_STATION_UI_FURNACE`
- `SDK_STATION_UI_CAMPFIRE`
- `SDK_STATION_UI_ANVIL`
- `SDK_STATION_UI_BLACKSMITHING`
- `SDK_STATION_UI_LEATHERWORKING`

## API Surface

### Recipe Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_crafting_match` | `(grid, w, h) → int` | Match grid against all recipes |

### Crafting UI Functions

| Function | Description |
|----------|-------------|
| `craft_grid_w/h` | Get current grid dimensions (2 or 3) |
| `craft_update_match` | Recompute result from current grid state |
| `craft_take_result` | Take one result, consume ingredients |
| `craft_take_result_bulk` | Take all possible results |
| `craft_place_from_hotbar` | Place held hotbar item into grid |
| `craft_remove_to_hotbar` | Return grid item to hotbar |
| `craft_close` | Close crafting UI |
| `craft_mouse_over_result_slot` | Hit test for result slot (mouse x,y) |

### Station Functions

| Function | Description |
|----------|-------------|
| `is_station_block` | Check if block type is a station |
| `is_processing_station_block` | Check if station processes over time |
| `station_fuel_value` | Get burn ticks for fuel item |
| `station_process_result` | Get output for input+station combo |
| `station_progress_max` | Get processing time for station |
| `station_open_for_block` | Open UI for station at position |
| `station_close_ui` | Close station UI |
| `station_tick_all` | Update all stations (call per frame) |
| `station_place_from_hotbar` | Place item into station slot |
| `station_take_output` | Take item from output slot |
| `station_handle_ui_input` | Process station UI input |
| `station_handle_block_change` | Handle station block break/replace |
| `station_sync_to_persistence` | Save station to disk |
| `station_load_all_from_persistence` | Load all stations |
| `station_find_state_public` | Get station index at position |
| `station_get_state_const` | Get station state by index |

## Integration Notes

### Input Binding

Crafting key bound via input system:
```c
if (sdk_input_action_pressed(&g_input_settings, SDK_INPUT_ACTION_CRAFT)) {
    if (g_craft_open) {
        craft_close();
    } else {
        g_craft_open = true;
        g_craft_is_table = /* check if near crafting table */;
    }
}
```

### Block Breaking

When station block broken, `station_handle_block_change()`:
1. Looks up station at position
2. Spawns all items as `SdkPickupItem` entities
3. Removes from persistence
4. Closes UI if open

### NPC Integration

NPCs can use stations programmatically:
```c
int index = station_find_state_public(wx, wy, wz);
if (index >= 0) {
    station_npc_place_item(index, 0, ITEM_RAW_MEAT);  // Input slot
    station_npc_place_item(index, 1, ITEM_COAL);      // Fuel slot
}
// Later...
ItemType result;
if (station_npc_take_output(index, &result)) {
    // Got cooked meat
}
```

### Hotbar Integration

- `craft_place_from_hotbar()` uses `g_hotbar_selected`
- `craft_remove_to_hotbar()` calls `hotbar_add()`
- `craft_take_result()` calls `hotbar_add()` for results

## AI Context Hints

### Recipe Matching Details

- Pattern `ITEM_NONE` cells are "don't care" for matching
- Grid cells with `count == 0` are treated as `ITEM_NONE`
- Matcher tries ALL offsets, allowing small patterns in big grids
- First match wins (order in `g_recipes[]` matters for overlapping patterns)

### Adding New Recipes

1. Add entry to `g_recipes[]` in `sdk_crafting.h`:
```c
{ { ITEM_A, ITEM_B, ITEM_NONE, ... }, width, height, ITEM_RESULT, count }
```
2. Pattern is row-major: index = `y * width + x`
3. Use `ITEM_NONE` for empty pattern cells
4. Result item must exist in item system

### Adding New Stations

1. Add block type to `is_station_block()`
2. If processing: add to `is_processing_station_block()`, define fuel/process logic
3. Add `SDK_STATION_UI_*` value
4. Handle in `station_open_for_block()`
5. Add persistence sync in appropriate places
6. Handle UI in `station_ui_slot_at()` for hit-testing

### Fuel System Notes

- Fuel is consumed one item at a time from fuel slot
- `burn_remaining` counts down each tick
- When fuel runs out, processing pauses (doesn't fail)
- Adding fuel resumes processing automatically

### Station Capacity Limits

- Maximum 256 simultaneous stations (`MAX_STATION_STATES`)
- Each station block in world needs a state
- States are reused (array compacted on removal)

### Persistence Key

Stations are keyed by world coordinates `(wx, wy, wz)`. Moving a station (via construction cells) will orphan its persistence entry.

---

**Related Documentation:**
- `SDK/Core/Items/` - Item types and definitions
- `SDK/Core/World/Blocks/` - Block types including stations
- `SDK/Core/Persistence/` - Save/load system
- `SDK/Core/Hotbar/` - Hotbar integration
