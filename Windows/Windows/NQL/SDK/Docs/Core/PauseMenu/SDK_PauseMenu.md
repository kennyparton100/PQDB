# SDK Pause Menu System

## Overview

The Pause Menu system implements the in-game pause overlay, providing access to graphics settings, key binding configuration, creative mode inventory, character selection, chunk management, and debugging tools. It is a multi-view UI state machine integrated into the SDK's frame loop.

**File Location:** `SDK/Core/PauseMenu/sdk_pause_menu.c` (787 lines)  
**Public API:** `pause_menu_handle_input()` - called from `sdk_api.c` during frame processing  
**Dependencies:** Settings, Input, CreativeModeInventory, Chunk Manager, Profiler, Renderer

## Architecture

### Frame Loop Integration

```
nqlsdk_frame()
  └── Player Input Processing
       └── pause_menu_handle_input()   [this module]
            ├── Reads: g_input_settings (key states)
            ├── Writes: g_graphics_settings (live changes)
            └── Calls: save_graphics_settings_now() (persistence)
```

### Multi-View State Machine

The pause menu uses `g_pause_menu_view` to track the current UI view:

| View | Value | Description |
|------|-------|-------------|
| `SDK_PAUSE_MENU_VIEW_MAIN` | 0 | Root menu with 8 options |
| `SDK_PAUSE_MENU_VIEW_GRAPHICS` | 1 | 15+ graphics settings rows |
| `SDK_PAUSE_MENU_VIEW_KEY_BINDINGS` | 2 | Input capture and binding UI |
| `SDK_PAUSE_MENU_VIEW_CREATIVE` | 3 | Block/item search and grant |
| `SDK_PAUSE_MENU_VIEW_SELECT_CHARACTER` | 4 | Character asset selection |
| `SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER` | 5 | Resident chunk list view |
| `SDK_PAUSE_MENU_VIEW_DEBUG_PROFILER` | 6 | Performance profiler toggle |

Navigation uses edge-triggered input detection (`g_pause_menu_nav_was_down[6]`) to prevent rapid-fire selection:
- `[0]` - Up pressed this frame
- `[1]` - Down pressed this frame  
- `[2]` - Left pressed this frame
- `[3]` - Right pressed this frame
- `[4]` - Activate (Enter/Confirm) pressed this frame
- `[5]` - Back (Escape/Backspace) pressed this frame

### Input Handling Strategy

All pause menu input is processed **while the game is paused** (`g_pause_menu_open = true`). The main loop skips world interaction when the pause menu is open:

```c
if (g_pause_menu_open || g_craft_open || g_skills_open || ...) {
    // Skip player movement, block breaking, etc.
}
```

## Key Subsystems

### 1. Main Menu (SDK_PAUSE_MENU_VIEW_MAIN)

**8 Menu Options:**
1. **Graphics** → Opens graphics settings view
2. **Key Bindings** → Opens input remapping view
3. **Creative Mode** → Opens creative inventory (blocks + items)
4. **Select Character** → Character asset browser
5. **Chunk Manager** → Debug view of loaded chunks
6. **Debug Profiler** → Enable/disable performance logging
7. **Leave World** → Disconnect/return to start menu
8. **Quit Game** → Close application

**Navigation:** Up/Down to change selection, Activate to enter submenu, Back to close pause menu.

### 2. Graphics Settings (SDK_PAUSE_MENU_VIEW_GRAPHICS)

**15 Configurable Settings (SDK_GRAPHICS_MENU_ROW_COUNT):**

| Row | Setting | Values | Persistence |
|-----|---------|--------|-------------|
| 0 | Quality Preset | Performance/Balanced/High | Immediate apply |
| 1 | Display Mode | Windowed/Borderless/Fullscreen | Immediate apply |
| 2 | Resolution | 4 presets (720p-4K) | On mode change |
| 3 | Render Scale | 50-100% (5% steps) | Immediate |
| 4 | Anti-Aliasing | Off/FXAA | Immediate |
| 5 | Smooth Lighting | On/Off | Immediate + mesh rebuild |
| 6 | Shadow Quality | Off/Low/Medium/High | Immediate |
| 7 | Water Quality | Low/High | Immediate |
| 8 | Render Distance | 6 presets (2-32 chunks) | Rebuild chunk grid |
| 9 | Anisotropic Filtering | 0/2/4/8/16x | Immediate |
| 10 | Far Terrain LOD | 8 distance presets | Immediate + rebuild |
| 11 | Experimental Far Meshes | 8 distance presets | Immediate + rebuild |
| 12 | Black Superchunk Walls | On/Off | Immediate |
| 13 | VSync | On/Off | Immediate |
| 14 | Fog | On/Off | Immediate |

**Preset System:**
- `apply_graphics_preset_defaults(preset)` applies bundled settings
- Presets: Performance (low quality, 75% scale), Balanced (default), High (full quality)

**Normalization Functions:**
- `normalize_far_mesh_lod_distance()` - clamps LOD to render distance
- `normalize_experimental_far_mesh_distance()` - validates experimental range

### 3. Key Bindings (SDK_PAUSE_MENU_VIEW_KEY_BINDINGS)

**UI Structure:**
- `SDK_INPUT_ACTION_COUNT` action rows (input actions from `sdk_input.h`)
- +3 additional rows: Look Sensitivity, Invert Y, Restore Defaults

**Binding Capture Flow:**
1. User selects action row, presses Activate
2. `g_keybind_capture_active = true` enters capture mode
3. `sdk_input_capture_binding_code()` polls for key press
4. On capture: `sdk_input_assign_binding()` updates, `save_input_settings_now()` persists
5. Special: VK_BACK clears binding, 'R' restores default, ESC cancels

**Functions:**
- `sdk_input_assign_binding()` - set custom binding
- `sdk_input_restore_default_binding()` - reset single action
- `sdk_input_restore_defaults()` - reset all actions
- `sdk_input_clear_binding()` - unbind action

### 4. Creative Mode (SDK_PAUSE_MENU_VIEW_CREATIVE)

**Two-Panel UI:**
- **Left Panel:** Searchable block/item list
- **Right Panel:** Shape dimensions (Width/Height/Depth)

**Search System:**
- `g_creative_menu_search[SDK_PAUSE_MENU_SEARCH_MAX]` - search text buffer
- Case-insensitive, underscore→space normalization
- Filters: All, Building Blocks, Colors, Items only

**Shape Configuration:**
- `g_creative_shape_width/height/depth` (1-16 blocks)
- When all 16x16x16: grants single block to hotbar
- When smaller: creates `SdkConstructionItemPayload` box shape

**Entry Granting:**
```c
creative_grant_selected_entry()
  ├── Block entry → hotbar_set_creative_block()
  │   └── With shape → hotbar_set_shaped_payload()
  └── Item entry → hotbar_set_item(count)
       └── Count from creative_item_grant_count()
           ├── Throwable items: 8
           ├── Firearms: 1
           └── Spawners: 32 (CREATIVE_SPAWNER_STACK)
```

**Creative Inventory Filtering:**
- Functions in `CreativeModeInventory/sdk_creative_inventory.c`:
  - `block_matches_creative_search()` - block name + filter check
  - `item_matches_creative_search()` - item name + filter check
  - `creative_entry_for_filtered_index()` - maps UI index to entry

### 5. Character Selection (SDK_PAUSE_MENU_VIEW_SELECT_CHARACTER)

**Data Source:**
- `g_character_assets[]` - populated by `refresh_character_assets()`
- `g_character_asset_count` - available characters

**UI State:**
- `g_pause_character_selected` - current selection
- `g_pause_character_scroll` - scroll offset for long lists

**Selection Flow:**
1. User browses with Up/Down
2. Activate calls `select_gameplay_character_index(index, persist_now=true)`
3. Selection persists to settings, triggers mesh rebuild
4. Returns to main menu view

### 6. Chunk Manager (SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER)

**Debug Display:**
- Shows `g_sdk.chunk_mgr.resident_count` loaded chunks
- `g_chunk_manager_selected` - highlighted chunk index
- `g_chunk_manager_scroll` - visible window offset

**Navigation:**
- Up/Down scrolls through resident chunk list
- No actions available (read-only debug view)

### 7. Debug Profiler (SDK_PAUSE_MENU_VIEW_DEBUG_PROFILER)

**Profiler Toggle:**
- Activate enables/disables `g_profiler`
- On enable: creates log file in world save directory
- On disable: closes log, flushes data

**Logging:**
- `profiler_log_current_graphics_snapshot("StartGraphics")` - dumps settings
- Logs to `%WORLD_SAVE_ROOT%\%world_id%\profiler_*.csv`

### 8. Command Console

**Activation:**
- Slash key (`/`) opens command line
- `g_command_open` tracks state
- `g_command_text[SDK_COMMAND_LINE_TEXT_MAX]` input buffer

**Commands:**
```
/TP <x> <y> <z>    Teleport player to coordinates
```

**Functions:**
- `command_open()` - init with "/" prefix
- `command_close()` - clear state, release char queue
- `command_execute()` - parse and execute
- `command_line_handle_input()` - char input, backspace, enter
- `teleport_player_to()` - updates camera position, resets physics

## Global State

### Pause Menu Owned State

```c
bool  g_pause_menu_open;                    // Is pause menu visible
bool  g_pause_menu_key_was_down;            // Edge detect for toggle key
int   g_pause_menu_view;                    // Current view enum
int   g_pause_menu_selected;                // Main menu selection (0-7)
bool  g_pause_menu_nav_was_down[6];         // Edge detection flags
```

### View-Specific State

```c
// Graphics
int   g_graphics_menu_selected;             // 0-14 row selection

// Key Bindings
int   g_keybind_menu_selected;              // Action row selection
int   g_keybind_menu_scroll;                // Scroll offset
bool  g_keybind_capture_active;             // Waiting for key press

// Creative Mode
int   g_creative_menu_selected;             // Entry selection
int   g_creative_menu_scroll;               // Scroll offset
int   g_creative_menu_filter;               // Filter mode enum
char  g_creative_menu_search[32];           // Search text
int   g_creative_menu_search_len;           // Text length
int   g_creative_shape_focus;               // 0=list, 1=panel
int   g_creative_shape_row;                 // 0=width, 1=height, 2=depth
int   g_creative_shape_width;               // 1-16
int   g_creative_shape_height;              // 1-16
int   g_creative_shape_depth;               // 1-16

// Character Selection
int   g_pause_character_selected;           // Character index
int   g_pause_character_scroll;               // Scroll offset

// Chunk Manager
int   g_chunk_manager_selected;               // Chunk list index
int   g_chunk_manager_scroll;                 // Scroll offset

// Command Console
bool  g_command_open;                         // Is command line active
char  g_command_text[64];                     // Input buffer
int   g_command_text_len;                     // Input length
bool  g_command_enter_was_down;               // Edge detect
bool  g_command_backspace_was_down;           // Edge detect
```

### Cross-Module State (Read/Write)

```c
// Settings (reads/writes live settings)
SdkGraphicsSettings g_graphics_settings;
SdkInputSettings    g_input_settings;

// Input (reads key states)
bool sdk_input_action_down(&g_input_settings, action);
bool sdk_input_raw_key_down(VK_CODE);
bool sdk_input_raw_key_pressed(VK_CODE);
int  sdk_input_capture_binding_code();

// Character (reads assets, writes selection)
int   g_selected_character_index;
void  select_gameplay_character_index(idx, persist);
void  refresh_character_assets();

// Chunk Manager (reads for debug view)
SdkChunkManager g_sdk.chunk_mgr;

// Profiler (enables/disables)
SdkProfiler g_profiler;

// Window (for display mode changes)
SdkWindow g_sdk.window;
```

## Integration Points

### Settings Persistence

All graphics and input changes call `save_*_settings_now()`:
- `save_graphics_settings_now()` → `sdk_graphics_settings_save()`
- `save_input_settings_now()` → `sdk_input_settings_save()`

### Renderer Synchronization

Settings that affect rendering trigger updates:
- `sdk_renderer_set_vsync()` - VSync changes
- `sdk_renderer_set_display_mode()` - Fullscreen/windowed
- `sdk_renderer_resize()` - Resolution changes
- `sdk_mesh_set_smooth_lighting_enabled()` - Lighting mode
- `mark_all_loaded_chunks_dirty()` - Settings needing remesh

### World State Changes

- `rebuild_chunk_grid_for_current_camera()` - Render distance changes
- `evict_undesired_loaded_chunks()` - Grid size reduction
- `leave_world_from_pause_menu()` - Disconnect/return to start

### Hotbar Integration

Creative mode grants items via hotbar system:
- `hotbar_set_creative_block(slot, BlockType)`
- `hotbar_set_item(slot, ItemType, count)`
- `hotbar_set_shaped_payload(slot, SdkConstructionItemPayload*)`

## AI Context Hints

### Adding a New Menu View

1. **Add view enum value** (search for `SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER` pattern)
2. **Add UI state variables** in `sdk_api.c` and `sdk_api_internal.h`
3. **Add view handler** in `pause_menu_handle_input()` switch statement
4. **Add menu entry** in `SDK_PAUSE_MENU_VIEW_MAIN` activate handler
5. **Add back navigation** with `g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN`
6. **Add renderer UI** in the presentation layer (separate from input handling)

### Finding Related Code

| Feature | Where to Look |
|---------|---------------|
| Input actions | `SDK/Core/Input/sdk_input.h` - `SdkInputAction` enum |
| Graphics settings struct | `SDK/Core/Settings/sdk_settings.h` |
| Creative inventory | `SDK/Core/CreativeModeInventory/sdk_creative_inventory.c` |
| Character assets | `SDK/Core/RoleAssets/sdk_role_assets.c` |
| Profiler | `SDK/Core/Profiler/sdk_profiler.h` |
| Hotbar system | `SDK/Core/API/sdk_api.c` - hotbar_* functions |
| Settings persistence | `SDK/Core/Settings/sdk_settings.c` |

### Common Patterns

**Edge-Triggered Input:**
```c
bool key_down = sdk_input_action_down(&g_input_settings, ACTION);
if (key_down && !g_was_down) { /* handle press */ }
g_was_down = key_down;
```

**Menu Clamp Pattern:**
```c
selection = api_clampi(selection, 0, max_row - 1);
max_scroll = api_clampi(total_rows - visible_rows, 0, total_rows);
scroll = api_clampi(scroll, 0, max_scroll);
```

**View State Reset:**
```c
// When entering a view, reset its state
if (activate && !was_down) {
    g_view_state = initial_value;
    g_pause_menu_view = NEW_VIEW;
}
```

### Thread Safety Notes

The pause menu runs on the main thread only. Settings persistence uses critical sections internally. No explicit locking needed in pause menu code.

---

**Related Documentation:**
- `SDK/Core/Settings/` - Settings persistence and defaults
- `SDK/Core/Input/` - Input action system and binding
- `SDK/Core/CreativeModeInventory/` - Creative mode filtering
- `SDK/Core/Profiler/` - Performance profiler
