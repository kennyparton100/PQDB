<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../SDK_Overview.md) > [Core](../SDK_CoreOverview.md) > Settings

---

# NQL SDK - Settings And Controls

This document describes the persisted graphics settings and input bindings used by the runtime.

## Files

- `settings.json`
  - graphics and display settings
- `controls.cfg`
  - input bindings and look settings

`nqlsdk_init` loads both files before creating the window. `nqlsdk_shutdown` writes both files back out through the current runtime save helpers.

## Graphics Settings

The graphics settings API lives in `Core/sdk_settings.h` and `Core/sdk_settings.c`.

Current file:

- `settings.json`
- schema version: `8`

The persisted struct is `SdkGraphicsSettings`.

Current fields:

- `version`
- `preset`
- `chunk_grid_size`
- `display_mode`
- `resolution_preset_index`
- `window_width`
- `window_height`
- `fullscreen_width`
- `fullscreen_height`
- `render_scale_percent`
- `anisotropy_level`
- `anti_aliasing_mode`
- `smooth_lighting`
- `shadow_quality`
- `water_quality`
- `far_terrain_lod_distance_chunks`
- `experimental_far_mesh_distance_chunks`
- `black_superchunk_walls`
- `vsync`
- `fog_enabled`

## Current Defaults

`sdk_graphics_settings_default` currently initializes:

- preset: `SDK_GRAPHICS_PRESET_BALANCED`
- chunk grid size: `CHUNK_GRID_DEFAULT_SIZE` (`17`)
- display mode: `SDK_DISPLAY_MODE_WINDOWED`
- default window/fullscreen size: `3840 x 2400`
- render scale: `100`
- anisotropy: `0`
- anti-aliasing: `SDK_ANTI_ALIASING_FXAA`
- smooth lighting: `true`
- shadow quality: `SDK_SHADOW_QUALITY_MEDIUM`
- water quality: `SDK_WATER_QUALITY_HIGH`
- far terrain LOD distance: `8`
- experimental far mesh distance: `0`
- black superchunk walls: `false`
- vsync: `true`
- fog: `true`

## Normalization Rules

The loader does not trust file contents directly. `clamp_and_fix` in `sdk_settings.c` normalizes:

- display mode enum range
- window and fullscreen dimensions
- resolution preset index
- render scale to `50..100`
- anisotropy to the supported discrete levels `0, 2, 4, 8, 16`
- far-mesh distances to supported discrete levels `0, 2, 4, 6, 8, 10, 12, 16`
- far-mesh distances so they never exceed the effective render distance implied by `chunk_grid_size`
- `experimental_far_mesh_distance_chunks` so it never exceeds `far_terrain_lod_distance_chunks`

The loader also accepts some legacy keys and maps them onto the current schema.

## Input Settings

The input settings API lives in `Core/sdk_input.h` and `Core/sdk_input.c`.

Current file:

- `controls.cfg`
- schema version: `1`

The persisted struct is `SdkInputSettings`.

Current non-binding fields:

- `version`
- `look_sensitivity_percent`
- `invert_y`

The runtime clamps `look_sensitivity_percent` to `25..300`.

## Binding Model

Bindings are keyed by `SdkInputAction` and stored as integer codes:

- Win32 virtual-key codes for keyboard input
- `1000`, `1001`, `1002` for left, right, and middle mouse buttons

`sdk_input_assign_binding` enforces a one-binding-per-action model by clearing duplicate bindings from other actions when a new binding is assigned.

The config file uses the action spec key names from `k_input_action_specs` in `sdk_input.c`.

Current action groups:

- movement and view
  - `move_forward`, `move_backward`, `move_left`, `move_right`
  - `look_left`, `look_right`, `look_up`, `look_down`
  - `jump_toggle_flight`, `descend`, `sprint`
- interaction
  - `break_block`, `place_use`, `vehicle_use`, `construction_rotate`
- frontend and in-game menus
  - `pause_menu`, `open_map`, `open_skills`, `open_crafting`, `open_command`
- hotbar
  - `hotbar_1` through `hotbar_10`
- menu navigation
  - `menu_up`, `menu_down`, `menu_left`, `menu_right`, `menu_confirm`, `menu_back`
- map navigation
  - `map_pan_up`, `map_pan_down`, `map_pan_left`, `map_pan_right`, `map_zoom_out`, `map_zoom_in`
- editor controls
  - `editor_toggle_playback`, `editor_prev_frame`, `editor_next_frame`

## Current Default Bindings

The default action map is defined in `k_input_action_specs` and mirrored by the checked-in `Build/controls.cfg`.

Representative defaults:

- `W`, `A`, `S`, `D` for movement
- arrow keys for look and menu navigation
- `Space` for jump / test-flight toggle
- `Ctrl` for descend
- `Shift` for sprint
- left/right mouse for break and place/use
- `F` for vehicle use
- `R` for construction rotate
- `P` for pause
- `M` for map
- `Tab` for skills
- `C` for crafting
- `/` for command
- `1..0` for hotbar selection
- `F9`, `Page Up`, `Page Down` for editor playback and frame stepping

## Runtime Use

At frame start the runtime snapshots the current input state with `sdk_input_frame_begin`.

The rest of the frame uses:

- `sdk_input_action_down`
- `sdk_input_action_pressed`
- `sdk_input_action_released`

The runtime also uses raw key and raw mouse helpers where action indirection would be too coarse.

## Construction Authoring State

The construction authoring UI now has additional transient state used by the creative inventory and placement preview:

- creative shape focus
- creative shape row
- creative shape width
- creative shape height
- creative shape depth
- current construction rotate state used during shaped placement

Important boundary:

- these values are runtime UI/session state
- they are not stored in `controls.cfg`
- they are not stored in `settings.json`

Only the input binding itself is persisted. The current authored shape selection is not.

---

## Related Documentation

### Up to Parent
- [SDK Core Overview](../SDK_CoreOverview.md) - Core subsystems hub
- [SDK Overview](../../SDK_Overview.md) - Documentation root

### Related Systems
- [SDK_Settings.md](SDK_Settings.md) - Settings system
- [../Input/SDK_Input.md](../Input/SDK_Input.md) - Input system
- [../World/ConstructionCells/SDK_ConstructionSystem.md](../World/ConstructionCells/SDK_ConstructionSystem.md) - Construction
- [../API/Session/SDK_RuntimeSessionAndFrontend.md](../API/Session/SDK_RuntimeSessionAndFrontend.md) - Session/bootstrap

### Build & Platform
- [../../Build/SDK_BuildGuide.md](../../Build/SDK_BuildGuide.md) - Build guide
- [../../Platform/SDK_PlatformWin32.md](../../Platform/SDK_PlatformWin32.md) - Win32 platform

---
*Documentation for `SDK/Core/Settings/`*
