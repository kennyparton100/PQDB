<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../SDK_Overview.md) > [Core](../SDK_CoreOverview.md) > Input

---

# SDK Input Documentation

Comprehensive documentation for the SDK Input system handling keyboard, mouse, and action-based input with configurable key bindings.

**Module:** `SDK/Core/Input/`  
**Output:** `SDK/Docs/Core/Input/SDK_Input.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Architecture](#architecture)
- [Input Actions](#input-actions)
- [Settings Management](#settings-management)
- [Key Functions](#key-functions)
- [Global State](#global-state)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Input system provides a high-level action-based input API that abstracts raw keyboard and mouse input into semantic game actions. Each action can be bound to any key or mouse button, with automatic conflict resolution. Settings persist to `controls.cfg` in key=value format.

**Key Features:**
- 46 configurable input actions across movement, gameplay, UI, and editor
- Frame-based input tracking with pressed/released detection
- Configurable look sensitivity (25-300%) and Y-axis inversion
- Persistent settings with version migration support
- Raw input access for custom handling

---

## Architecture

### Frame-Based Input Flow

```
Per Frame:
    sdk_input_frame_begin(window)
           │
           ├──► Update g_prev_key_down from g_key_down
           ├──► Update g_key_down from window state
           ├──► Update g_prev_mouse_down from g_mouse_down
           └──► Update g_mouse_down from window state
           
    Query Input:
           ├──► sdk_input_action_down() - is action held?
           ├──► sdk_input_action_pressed() - was action just pressed?
           ├──► sdk_input_action_released() - was action just released?
           └──► sdk_input_raw_key_down() - direct key query
```

### Settings Persistence Flow

```
Load:
    sdk_input_settings_load()
           │
           ├──► Start with sdk_input_settings_default()
           ├──► Parse controls.cfg line by line
           ├──► Extract key=value pairs
           ├──► Map keys to SdkInputAction via k_input_action_specs
           └──► Clamp sensitivity to [25, 300]

Save:
    sdk_input_settings_save()
           │
           ├──► Write version, sensitivity, invert_y
           └──► Write all action bindings by key_name
```

### Binding Assignment Flow

```
Assign Binding:
    sdk_input_assign_binding(settings, action, binding_code)
           │
           ├──► If binding_code != 0
           │       └──► Clear same binding from other actions
           └──► Set binding_code[action] = new_code
```

---

## Input Actions

**SdkInputAction enum** defines 46 semantic actions:

### Movement Actions

| Action | Default | Description |
|--------|---------|-------------|
| `SDK_INPUT_ACTION_MOVE_FORWARD` | W | Move forward |
| `SDK_INPUT_ACTION_MOVE_BACKWARD` | S | Move backward |
| `SDK_INPUT_ACTION_MOVE_LEFT` | A | Strafe left |
| `SDK_INPUT_ACTION_MOVE_RIGHT` | D | Strafe right |
| `SDK_INPUT_ACTION_JUMP_TOGGLE_FLIGHT` | Space | Jump / toggle flight |
| `SDK_INPUT_ACTION_DESCEND` | Ctrl | Descend/fly down |
| `SDK_INPUT_ACTION_SPRINT` | Shift | Sprint modifier |

### Look/Camera Actions

| Action | Default | Description |
|--------|---------|-------------|
| `SDK_INPUT_ACTION_LOOK_LEFT` | Left Arrow | Look left |
| `SDK_INPUT_ACTION_LOOK_RIGHT` | Right Arrow | Look right |
| `SDK_INPUT_ACTION_LOOK_UP` | Up Arrow | Look up |
| `SDK_INPUT_ACTION_LOOK_DOWN` | Down Arrow | Look down |

### Gameplay Actions

| Action | Default | Description |
|--------|---------|-------------|
| `SDK_INPUT_ACTION_BREAK_BLOCK` | Mouse 1 | Break/attack |
| `SDK_INPUT_ACTION_PLACE_USE` | Mouse 2 | Place/use |
| `SDK_INPUT_ACTION_VEHICLE_USE` | F | Vehicle interaction |

### UI Actions

| Action | Default | Description |
|--------|---------|-------------|
| `SDK_INPUT_ACTION_PAUSE_MENU` | P | Open pause menu |
| `SDK_INPUT_ACTION_OPEN_MAP` | M | Open world map |
| `SDK_INPUT_ACTION_OPEN_SKILLS` | Tab | Open skills |
| `SDK_INPUT_ACTION_OPEN_CRAFTING` | C | Open crafting |
| `SDK_INPUT_ACTION_OPEN_COMMAND` | / | Open command line |

### Hotbar Actions

| Action | Default | Description |
|--------|---------|-------------|
| `SDK_INPUT_ACTION_HOTBAR_1` | 1 | Select hotbar slot 1 |
| `SDK_INPUT_ACTION_HOTBAR_2` | 2 | Select hotbar slot 2 |
| ... | ... | ... |
| `SDK_INPUT_ACTION_HOTBAR_10` | 0 | Select hotbar slot 10 |

### Menu Navigation

| Action | Default | Description |
|--------|---------|-------------|
| `SDK_INPUT_ACTION_MENU_UP` | Up Arrow | Navigate up |
| `SDK_INPUT_ACTION_MENU_DOWN` | Down Arrow | Navigate down |
| `SDK_INPUT_ACTION_MENU_LEFT` | Left Arrow | Navigate left |
| `SDK_INPUT_ACTION_MENU_RIGHT` | Right Arrow | Navigate right |
| `SDK_INPUT_ACTION_MENU_CONFIRM` | Enter | Confirm selection |
| `SDK_INPUT_ACTION_MENU_BACK` | Backspace | Go back |

### Map Navigation

| Action | Default | Description |
|--------|---------|-------------|
| `SDK_INPUT_ACTION_MAP_PAN_UP` | W | Pan map up |
| `SDK_INPUT_ACTION_MAP_PAN_DOWN` | S | Pan map down |
| `SDK_INPUT_ACTION_MAP_PAN_LEFT` | A | Pan map left |
| `SDK_INPUT_ACTION_MAP_PAN_RIGHT` | D | Pan map right |
| `SDK_INPUT_ACTION_MAP_ZOOM_OUT` | Left Arrow | Zoom out |
| `SDK_INPUT_ACTION_MAP_ZOOM_IN` | Right Arrow | Zoom in |

### Construction & Editor

| Action | Default | Description |
|--------|---------|-------------|
| `SDK_INPUT_ACTION_CONSTRUCTION_ROTATE` | R | Rotate construction |
| `SDK_INPUT_ACTION_EDITOR_TOGGLE_PLAYBACK` | F9 | Play/pause animation |
| `SDK_INPUT_ACTION_EDITOR_PREV_FRAME` | Page Up | Previous frame |
| `SDK_INPUT_ACTION_EDITOR_NEXT_FRAME` | Page Down | Next frame |

---

## Settings Management

### SdkInputSettings Structure

```c
typedef struct {
    int version;                          // Settings format version
    int look_sensitivity_percent;         // 25-300, default 100
    bool invert_y;                        // Invert look Y-axis
    int binding_code[SDK_INPUT_ACTION_COUNT];  // Action bindings
} SdkInputSettings;
```

### controls.cfg Format

```
version=1
look_sensitivity_percent=100
invert_y=false
move_forward=87
move_backward=83
break_block=1000
place_use=1001
...
```

**Binding Code Values:**
- `0` = Unbound
- `1-255` = Virtual-key codes (VK_* constants)
- `1000` = Mouse left button (`SDK_INPUT_MOUSE_LEFT`)
- `1001` = Mouse right button (`SDK_INPUT_MOUSE_RIGHT`)
- `1002` = Mouse middle button (`SDK_INPUT_MOUSE_MIDDLE`)

---

## Key Functions

### Settings Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_input_settings_default` | `(SdkInputSettings*) → void` | Initialize to defaults |
| `sdk_input_settings_load` | `(SdkInputSettings*) → int` | Load from controls.cfg (returns success) |
| `sdk_input_settings_save` | `(const SdkInputSettings*) → void` | Save to controls.cfg |
| `sdk_input_restore_defaults` | `(SdkInputSettings*) → void` | Reset all to default |
| `sdk_input_restore_default_binding` | `(SdkInputSettings*, SdkInputAction) → void` | Reset one action |

### Binding Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_input_assign_binding` | `(SdkInputSettings*, SdkInputAction, int) → void` | Assign binding, clears conflicts |
| `sdk_input_clear_binding` | `(SdkInputSettings*, SdkInputAction) → void` | Unbind action |
| `sdk_input_capture_binding_code` | `() → int` | Wait for key/mouse press, return code |
| `sdk_input_action_label` | `(SdkInputAction) → const char*` | Get display label (e.g., "MOVE FORWARD") |
| `sdk_input_binding_name` | `(int, char*, size_t) → void` | Get binding display name (e.g., "W", "MOUSE 1") |

### Input Queries

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_input_frame_begin` | `(SdkWindow*) → void` | Update input state (call once per frame) |
| `sdk_input_action_down` | `(const SdkInputSettings*, SdkInputAction) → bool` | Is action currently held? |
| `sdk_input_action_pressed` | `(const SdkInputSettings*, SdkInputAction) → bool` | Was action just pressed this frame? |
| `sdk_input_action_released` | `(const SdkInputSettings*, SdkInputAction) → bool` | Was action just released this frame? |
| `sdk_input_raw_key_down` | `(uint8_t) → bool` | Direct key state query |
| `sdk_input_raw_key_pressed` | `(uint8_t) → bool` | Direct key press edge |
| `sdk_input_raw_key_released` | `(uint8_t) → bool` | Direct key release edge |
| `sdk_input_raw_mouse_down` | `(int) → bool` | Direct mouse button state |
| `sdk_input_raw_mouse_pressed` | `(int) → bool` | Direct mouse press edge |

---

## Global State

```c
// Per-frame key state arrays
static bool g_key_down[256];        // Current frame
static bool g_prev_key_down[256];   // Previous frame

// Mouse state (0=left, 1=right, 2=middle)
static bool g_mouse_down[3];
static bool g_prev_mouse_down[3];

// Action specifications table (internal)
static const SdkInputActionSpec k_input_action_specs[SDK_INPUT_ACTION_COUNT];
```

---

## API Surface

### Public Header (sdk_input.h)

```c
// Mouse button constants
#define SDK_INPUT_MOUSE_LEFT    1000
#define SDK_INPUT_MOUSE_RIGHT   1001
#define SDK_INPUT_MOUSE_MIDDLE  1002

// Settings file path
#define SDK_INPUT_SETTINGS_PATH "controls.cfg"

// Action enum (46 actions)
typedef enum SdkInputAction { ... } SdkInputAction;
#define SDK_INPUT_ACTION_COUNT  (46)

// Settings struct
typedef struct SdkInputSettings { ... } SdkInputSettings;

// Core functions
void sdk_input_frame_begin(SdkWindow* win);
bool sdk_input_action_down(const SdkInputSettings* settings, SdkInputAction action);
bool sdk_input_action_pressed(const SdkInputSettings* settings, SdkInputAction action);
bool sdk_input_action_released(const SdkInputSettings* settings, SdkInputAction action);

// Raw input
bool sdk_input_raw_key_down(uint8_t vk_code);
bool sdk_input_raw_key_pressed(uint8_t vk_code);
bool sdk_input_raw_key_released(uint8_t vk_code);
bool sdk_input_raw_mouse_down(int button);
bool sdk_input_raw_mouse_pressed(int button);

// Settings
void sdk_input_settings_default(SdkInputSettings* out_settings);
int sdk_input_settings_load(SdkInputSettings* out_settings);
void sdk_input_settings_save(const SdkInputSettings* settings);
void sdk_input_restore_defaults(SdkInputSettings* settings);

// Binding management
int sdk_input_capture_binding_code(void);
const char* sdk_input_action_label(SdkInputAction action);
void sdk_input_binding_name(int binding_code, char* out_text, size_t out_text_size);
void sdk_input_assign_binding(SdkInputSettings* settings, SdkInputAction action, int binding_code);
void sdk_input_clear_binding(SdkInputSettings* settings, SdkInputAction action);
void sdk_input_restore_default_binding(SdkInputSettings* settings, SdkInputAction action);
```

---

## Integration Notes

### Frame Update Pattern

```c
// In game loop
void game_update() {
    // 1. Update input state
    sdk_input_frame_begin(g_window);
    
    // 2. Query actions
    if (sdk_input_action_pressed(&g_settings, SDK_INPUT_ACTION_JUMP_TOGGLE_FLIGHT)) {
        player_jump();
    }
    
    // 3. Continuous actions
    if (sdk_input_action_down(&g_settings, SDK_INPUT_ACTION_MOVE_FORWARD)) {
        player_move_forward();
    }
}
```

### Settings UI Pattern

```c
// Rebinding flow
void start_rebind(SdkInputAction action) {
    g_rebinding_action = action;
    g_rebinding_active = true;
}

void update_rebinding() {
    if (!g_rebinding_active) return;
    
    int code = sdk_input_capture_binding_code();
    if (code != 0) {
        sdk_input_assign_binding(&g_settings, g_rebinding_action, code);
        sdk_input_settings_save(&g_settings);
        g_rebinding_active = false;
    }
}
```

### Window Integration

The Input module depends on the Window module for raw input:
- `sdk_window_is_key_down()` - queries key state
- `sdk_window_is_mouse_down()` - queries mouse button state
- `SdkWindow*` passed to `sdk_input_frame_begin()`

---

## AI Context Hints

### Adding New Input Actions

1. **Add to `SdkInputAction` enum** in `sdk_input.h`:
   ```c
   SDK_INPUT_ACTION_MY_NEW_ACTION,
   SDK_INPUT_ACTION_COUNT  // Keep last
   ```

2. **Add to `k_input_action_specs` array** in `sdk_input.c`:
   ```c
   { SDK_INPUT_ACTION_MY_NEW_ACTION, "my_action", "MY ACTION", 'X' },
   ```

3. **Default binding** is the last field (use VK_* constants or 'A'-'Z', '0'-'9')

4. **Update documentation** - add to action tables above

### Creating Custom Bindings UI

```c
void draw_binding_button(SdkInputAction action, int x, int y) {
    char name[32];
    int code = g_settings.binding_code[action];
    
    sdk_input_binding_name(code, name, sizeof(name));
    draw_button(x, y, "%s: %s", sdk_input_action_label(action), name);
    
    if (button_clicked() && !g_rebinding_active) {
        start_rebind(action);
    }
}
```

### Handling Modifier Keys

The input system automatically handles modifier key variants:
- `VK_SHIFT` matches either `VK_LSHIFT` or `VK_RSHIFT`
- `VK_CONTROL` matches either `VK_LCONTROL` or `VK_RCONTROL`
- `VK_MENU` (Alt) matches either `VK_LMENU` or `VK_RMENU`

### Sensitivity Handling

For mouse look with sensitivity:
```c
SdkVec3 process_look_input(const SdkInputSettings* settings, float raw_delta_x, float raw_delta_y) {
    float sensitivity = settings->look_sensitivity_percent / 100.0f;
    float look_x = raw_delta_x * sensitivity;
    float look_y = raw_delta_y * sensitivity * (settings->invert_y ? -1 : 1);
    
    return sdk_vec3(look_x, look_y, 0);
}
```

---

## Construction Authoring State

The construction authoring UI maintains additional transient state used by the creative inventory and placement preview:

### State Fields

| Field | Type | Description |
|-------|------|-------------|
| `creative_shape_focus` | int | Currently selected shape category |
| `creative_shape_row` | int | Scroll position in shape list |
| `creative_shape_width` | int | Placement width in blocks |
| `creative_shape_height` | int | Placement height in blocks |
| `creative_shape_depth` | int | Placement depth in blocks |
| `construction_rotate_state` | int | Current rotation (0-3) for shaped placement |

### Important Boundary

These values are **runtime UI/session state only**:
- They are NOT stored in `controls.cfg`
- They are NOT stored in `settings.json`
- They reset when the game restarts

Only input bindings are persisted. The current authored shape selection is not saved.

### Example: Shape Placement Flow

```c
void place_construction_shape() {
    // Read transient UI state
    int width = g_creative_shape_width;
    int height = g_creative_shape_height;
    int depth = g_creative_shape_depth;
    int rotation = g_construction_rotate_state;
    
    // Get shape from creative inventory
    ConstructionShape* shape = get_shape_at_row(g_creative_shape_row);
    
    // Apply rotation and place
    place_shape_rotated(shape, width, height, depth, rotation);
}

void rotate_construction() {
    // Increment rotation (0-3)
    g_construction_rotate_state = (g_construction_rotate_state + 1) % 4;
}
```

---

## Related Documentation

### Up to Parent
- [SDK Core Overview](../SDK_CoreOverview.md) - Core subsystems hub
- [SDK Overview](../../SDK_Overview.md) - Documentation root

### Related Systems
- [../Settings/SDK_SettingsAndControls.md](../Settings/SDK_SettingsAndControls.md) - Settings and controls
- [../Frontend/SDK_Frontend.md](../Frontend/SDK_Frontend.md) - Frontend UI (uses input actions)
- [../../Platform/SDK_PlatformWin32.md](../../Platform/SDK_PlatformWin32.md) - Win32 platform layer

### World & App
- [../../App/SDK_AppOverview.md](../../App/SDK_AppOverview.md) - App entry point
- [../World/SDK_WorldOverview.md](../World/SDK_WorldOverview.md) - World systems

---

**Source Files:**
- `SDK/Core/Input/sdk_input.h` (3,399 bytes) - Public API
- `SDK/Core/Input/sdk_input.c` (18,074 bytes) - Implementation with 46 action specs

---
*Documentation for `SDK/Core/Input/`*
