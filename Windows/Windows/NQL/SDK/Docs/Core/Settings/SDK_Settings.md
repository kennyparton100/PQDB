# SDK Settings Documentation

Comprehensive documentation for the SDK Settings module providing graphics and display configuration persistence with JSON-based settings storage.

**Module:** `SDK/Core/Settings/`  
**Output:** `SDK/Docs/Core/Settings/SDK_Settings.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Architecture](#architecture)
- [Graphics Presets](#graphics-presets)
- [Display Configuration](#display-configuration)
- [Rendering Settings](#rendering-settings)
- [Key Functions](#key-functions)
- [Global State](#global-state)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Settings module provides a minimal, robust graphics and display settings persistence system. It stores configuration in a human-readable JSON file (`settings.json`) with automatic version migration and value clamping for safety.

**Key Features:**
- JSON-based settings file (`settings.json`)
- Version migration support (current version 8)
- Three graphics presets (Performance, Balanced, High)
- Four display modes (Windowed, Borderless, Fullscreen)
- Resolution preset system with auto-detection
- Comprehensive rendering options (shadows, AA, water, LOD)
- Safe value clamping and validation
- Legacy setting migration (e.g., `texture_filter_linear` → `anisotropy_level`)

---

## Architecture

### Settings Flow

```
Startup:
           │
           ├──► sdk_graphics_settings_default(&settings)
           │       └──► Initialize all fields to defaults
           │
           ├──► sdk_graphics_settings_load(&settings)
           │       ├──► Read settings.json
           │       ├──► Parse key-value pairs
           │       ├──► Handle legacy migrations
           │       └──► clamp_and_fix() - validate all values
           │
           └──► Apply settings to renderer/window

Runtime Change:
           │
           ├──► User modifies setting in pause menu
           ├──► Update g_graphics_settings field
           ├──► Apply change immediately (e.g., rebuild chunk grid)
           └──► save_graphics_settings_now() → write JSON
```

### JSON Parsing

The module uses simple string-based JSON parsing (not a full JSON library):

```c
// Parse integer: "key": 123
parse_int_key(text, text_end, "chunk_grid_size", &value);

// Parse bool: "key": true/false  
parse_bool_key(text, text_end, "vsync", &value);

// Find key pattern: "key" followed by : and value
find_key_value(text, end, "preset");  // Returns pointer to value
```

This approach:
- **Pros:** Minimal dependencies, human-readable output, small code size
- **Cons:** No schema validation, limited JSON features (no arrays/objects), fragile to format changes

### Version Migration

```c
#define SDK_GRAPHICS_SETTINGS_VERSION 8

// On load, if version < current:
// - Apply defaults first
// - Parse available keys
// - Missing keys keep defaults (forward compatibility)
// - Legacy keys converted (e.g., texture_filter_linear → anisotropy_level)
```

### Value Clamping

The `clamp_and_fix()` function ensures all values are valid:

| Field | Range | Default on Invalid |
|-------|-------|-------------------|
| `preset` | 0-2 (PERFORMANCE/HIGH) | BALANCED |
| `display_mode` | 0-2 | WINDOWED |
| `chunk_grid_size` | 3-31 | Normalized to valid size |
| `window_*` | 320-16384 | 3840x2400 |
| `render_scale_percent` | 50-100 | 100 |
| `anti_aliasing_mode` | 0-1 | FXAA |
| `shadow_quality` | 0-3 | MEDIUM |
| `water_quality` | 0-1 | HIGH |
| `anisotropy_level` | Closest to {0,2,4,8,16} | Nearest valid |

---

## Graphics Presets

**SdkGraphicsPreset enum:**

| Preset | Value | Typical Configuration |
|--------|-------|----------------------|
| `SDK_GRAPHICS_PRESET_PERFORMANCE` | 0 | Lower shadows, reduced draw distance |
| `SDK_GRAPHICS_PRESET_BALANCED` | 1 | Medium quality, good FPS |
| `SDK_GRAPHICS_PRESET_HIGH` | 2 | Max quality, higher hardware requirements |

Presets are applied as a starting point; individual settings can be customized afterward.

---

## Display Configuration

### Display Modes

**SdkDisplayMode enum:**

| Mode | Value | Behavior |
|------|-------|----------|
| `SDK_DISPLAY_MODE_WINDOWED` | 0 | Resizable window with borders |
| `SDK_DISPLAY_MODE_BORDERLESS` | 1 | Fullscreen borderless window |
| `SDK_DISPLAY_MODE_FULLSCREEN` | 2 | Exclusive fullscreen mode |

### Resolution Presets

Static resolution table for preset selection:

```c
static const int k_resolution_preset_widths[]  = { 1280, 1920, 2560, 3840 };
static const int k_resolution_preset_heights[] = { 720,  1080, 1440, 2400 };
// Index:               0      1      2      3
```

**Auto-Detection:**
- On first run (or if resolution_preset_index invalid), closest preset is selected from current window size
- Window/fullscreen dimensions can differ (e.g., windowed at 1920x1080, fullscreen at 3840x2400)

### Related Fields

```c
typedef struct SdkGraphicsSettings {
    SdkDisplayMode      display_mode;           // Windowed/Borderless/Fullscreen
    int                 resolution_preset_index; // 0-3 (720p to 4K)
    int                 window_width;            // Windowed mode size
    int                 window_height;
    int                 fullscreen_width;        // Fullscreen mode size
    int                 fullscreen_height;
    int                 render_scale_percent;    // 50-100 (render scale)
    bool                vsync;                   // Vertical sync toggle
} SdkGraphicsSettings;
```

---

## Rendering Settings

### Anti-Aliasing

**SdkAntiAliasingMode:**
- `SDK_ANTI_ALIASING_OFF` - No anti-aliasing
- `SDK_ANTI_ALIASING_FXAA` - Fast Approximate Anti-Aliasing

### Shadow Quality

**SdkShadowQuality:**
- `SDK_SHADOW_QUALITY_OFF` - No shadows
- `SDK_SHADOW_QUALITY_LOW` - Low resolution shadow maps
- `SDK_SHADOW_QUALITY_MEDIUM` - Standard shadows
- `SDK_SHADOW_QUALITY_HIGH` - High resolution shadows

### Water Quality

**SdkWaterQuality:**
- `SDK_WATER_QUALITY_LOW` - Simple water rendering
- `SDK_WATER_QUALITY_HIGH` - Full water effects

### Anisotropic Filtering

```c
static const int k_anisotropy_levels[] = { 0, 2, 4, 8, 16 };
```

Values are snapped to nearest valid level (e.g., 6 → 4, 12 → 8).

### Level of Distance (LOD)

**Far Mesh Distance Presets:**

```c
static const int k_far_mesh_distance_levels[] = { 0, 2, 4, 6, 8, 10, 12, 16 };
```

| Field | Purpose | Max Value |
|-------|---------|-----------|
| `far_terrain_lod_distance_chunks` | Standard far proxy distance | ≤ chunk render distance |
| `experimental_far_mesh_distance_chunks` | Ultra-low-poly distance | ≤ `far_terrain_lod_distance_chunks` |

**Constraints:**
- Far distances cannot exceed chunk grid render distance
- Experimental distance cannot exceed standard far distance

### Additional Rendering Flags

| Field | Default | Purpose |
|-------|---------|---------|
| `smooth_lighting` | true | Enable smooth lighting on blocks |
| `black_superchunk_walls` | false | Debug: render walls as black |
| `fog_enabled` | true | Atmospheric fog effect |

---

## Key Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_graphics_settings_default` | `(SdkGraphicsSettings* out) → void` | Initialize all fields to defaults |
| `sdk_graphics_settings_load` | `(SdkGraphicsSettings* out) → int` | Load from `settings.json`, returns 1 on success |
| `sdk_graphics_settings_save` | `(const SdkGraphicsSettings*) → void` | Save to `settings.json` |

### Internal Helpers

| Function | Purpose |
|----------|---------|
| `read_text_file` | Read entire file into malloc'd buffer |
| `find_key_value` | Locate `"key"` in JSON text |
| `parse_int_key` | Parse integer value for key |
| `parse_bool_key` | Parse boolean value for key |
| `clamp_and_fix` | Validate and correct all settings |
| `closest_resolution_preset_index` | Find nearest resolution preset |
| `closest_anisotropy_level` | Snap to valid anisotropy level |
| `closest_far_mesh_distance_level` | Snap to valid LOD distance |

---

## Global State

```c
// Global graphics settings instance (sdk_api.c)
extern SdkGraphicsSettings g_graphics_settings;

// Chunk grid size mirror (for quick access)
extern int g_chunk_grid_size_setting;

// Settings file location
#define SDK_GRAPHICS_SETTINGS_PATH "settings.json"
#define SDK_GRAPHICS_SETTINGS_VERSION 8
```

---

## API Surface

### Public Header (sdk_settings.h)

```c
/* Graphics quality presets */
typedef enum SdkGraphicsPreset {
    SDK_GRAPHICS_PRESET_PERFORMANCE = 0,
    SDK_GRAPHICS_PRESET_BALANCED = 1,
    SDK_GRAPHICS_PRESET_HIGH = 2
} SdkGraphicsPreset;

/* Display mode */
typedef enum SdkDisplayMode {
    SDK_DISPLAY_MODE_WINDOWED = 0,
    SDK_DISPLAY_MODE_BORDERLESS = 1,
    SDK_DISPLAY_MODE_FULLSCREEN = 2
} SdkDisplayMode;

/* Anti-aliasing options */
typedef enum SdkAntiAliasingMode {
    SDK_ANTI_ALIASING_OFF = 0,
    SDK_ANTI_ALIASING_FXAA = 1
} SdkAntiAliasingMode;

/* Shadow quality levels */
typedef enum SdkShadowQuality {
    SDK_SHADOW_QUALITY_OFF = 0,
    SDK_SHADOW_QUALITY_LOW = 1,
    SDK_SHADOW_QUALITY_MEDIUM = 2,
    SDK_SHADOW_QUALITY_HIGH = 3
} SdkShadowQuality;

/* Water rendering quality */
typedef enum SdkWaterQuality {
    SDK_WATER_QUALITY_LOW = 0,
    SDK_WATER_QUALITY_HIGH = 1
} SdkWaterQuality;

/* Far mesh distance options (chunks) */
typedef enum SdkFarMeshDistancePreset {
    SDK_FAR_MESH_DISTANCE_OFF = 0,
    SDK_FAR_MESH_DISTANCE_2 = 2,
    SDK_FAR_MESH_DISTANCE_4 = 4,
    SDK_FAR_MESH_DISTANCE_6 = 6,
    SDK_FAR_MESH_DISTANCE_8 = 8,
    SDK_FAR_MESH_DISTANCE_10 = 10,
    SDK_FAR_MESH_DISTANCE_12 = 12,
    SDK_FAR_MESH_DISTANCE_1_SUPERCHUNK = 16
} SdkFarMeshDistancePreset;

/* Complete settings structure */
typedef struct SdkGraphicsSettings {
    int                 version;                    // Settings file version
    SdkGraphicsPreset    preset;                    // Quality preset
    int                 chunk_grid_size;            // Render distance (3-31)
    SdkDisplayMode      display_mode;               // Window mode
    int                 resolution_preset_index;    // 0-3 (720p-4K)
    int                 window_width;               // Window dimensions
    int                 window_height;
    int                 fullscreen_width;           // Fullscreen dimensions
    int                 fullscreen_height;
    int                 render_scale_percent;       // 50-100
    int                 anisotropy_level;             // 0, 2, 4, 8, 16
    SdkAntiAliasingMode anti_aliasing_mode;         // FXAA on/off
    bool                smooth_lighting;            // Block lighting
    SdkShadowQuality    shadow_quality;               // Shadow detail
    SdkWaterQuality     water_quality;                // Water effects
    int                 far_terrain_lod_distance_chunks;      // Far proxy distance
    int                 experimental_far_mesh_distance_chunks; // Ultra-LOD distance
    bool                black_superchunk_walls;     // Debug walls
    bool                vsync;                      // VSync toggle
    bool                fog_enabled;                // Fog effect
} SdkGraphicsSettings;

/* Core API */
void sdk_graphics_settings_default(SdkGraphicsSettings* out_settings);
int  sdk_graphics_settings_load(SdkGraphicsSettings* out_settings);
void sdk_graphics_settings_save(const SdkGraphicsSettings* settings);
```

---

## Integration Notes

### Initialization Sequence

```c
// In nqlsdk_init() or app startup:
SdkGraphicsSettings graphics;

// 1. Apply defaults
sdk_graphics_settings_default(&graphics);

// 2. Load saved settings (overrides defaults)
sdk_graphics_settings_load(&graphics);

// 3. Normalize values
graphics.chunk_grid_size = sdk_chunk_manager_normalize_grid_size(graphics.chunk_grid_size);
graphics.resolution_preset_index = clamp_resolution_preset_index(graphics.resolution_preset_index);
// ... etc

// 4. Store globally
g_graphics_settings = graphics;
g_chunk_grid_size_setting = graphics.chunk_grid_size;

// 5. Apply to subsystems
sdk_mesh_set_smooth_lighting_enabled(graphics.smooth_lighting ? 1 : 0);
```

### Runtime Changes

```c
// Change chunk render distance
void change_chunk_grid_size(int new_size) {
    new_size = sdk_chunk_manager_normalize_grid_size(new_size);
    
    // Update chunk manager
    rebuild_chunk_grid_for_current_camera(new_size);
    
    // Update settings
    g_graphics_settings.chunk_grid_size = new_size;
    g_chunk_grid_size_setting = new_size;
    
    // Persist
    save_graphics_settings_now();
}

// Change resolution
void change_resolution(int preset_index) {
    preset_index = clamp_resolution_preset_index(preset_index);
    g_graphics_settings.resolution_preset_index = preset_index;
    
    // Apply immediately
    if (g_graphics_settings.display_mode == SDK_DISPLAY_MODE_FULLSCREEN) {
        g_graphics_settings.fullscreen_width = 
            g_resolution_presets[preset_index].width;
        g_graphics_settings.fullscreen_height = 
            g_resolution_presets[preset_index].height;
        apply_display_mode_setting(true);  // save_now = true
    }
}
```

### Settings Reset

```c
void reset_graphics_settings_to_default() {
    sdk_graphics_settings_default(&g_graphics_settings);
    
    // Apply immediately
    apply_display_mode_setting(false);
    sdk_mesh_set_smooth_lighting_enabled(
        g_graphics_settings.smooth_lighting ? 1 : 0);
    
    // Save
    save_graphics_settings_now();
}
```

### Pause Menu Integration

```c
void draw_settings_menu() {
    // Display mode dropdown
    const char* modes[] = {"Windowed", "Borderless", "Fullscreen"};
    int mode = g_graphics_settings.display_mode;
    if (dropdown("Display Mode", modes, 3, &mode)) {
        g_graphics_settings.display_mode = mode;
        apply_display_mode_setting(true);
    }
    
    // Resolution preset
    const char* resolutions[] = {"1280x720", "1920x1080", "2560x1440", "3840x2400"};
    int res = g_graphics_settings.resolution_preset_index;
    if (dropdown("Resolution", resolutions, 4, &res)) {
        set_resolution_preset(res);
    }
    
    // Toggles
    bool vsync = g_graphics_settings.vsync;
    if (checkbox("VSync", &vsync)) {
        g_graphics_settings.vsync = vsync;
        apply_vsync_setting(true);
    }
}
```

---

## Input Settings (controls.cfg)

The SDK uses a separate configuration file for input bindings and controls: `controls.cfg`.

**Key Differences:**
- `settings.json` - Graphics and display settings (this document)
- `controls.cfg` - Input bindings, sensitivity, and look inversion

### controls.cfg Schema

| Field | Type | Range | Default |
|-------|------|-------|---------|
| `version` | int | 1 | 1 |
| `look_sensitivity_percent` | int | 25-300 | 100 |
| `invert_y` | bool | true/false | false |
| `[action_name]` | int | 0-1002 | varies |

### Action Binding Codes

- `0` = Unbound
- `1-255` = Win32 virtual-key codes (VK_* constants)
- `1000` = Mouse left button
- `1001` = Mouse right button  
- `1002` = Mouse middle button

### Example controls.cfg

```
version=1
look_sensitivity_percent=100
invert_y=false
move_forward=87
move_backward=83
break_block=1000
place_use=1001
pause_menu=80
```

### Loading Order

`nqlsdk_init` loads both files before creating the window:
1. `settings.json` (graphics settings)
2. `controls.cfg` (input settings)

`nqlsdk_shutdown` writes both files back through the runtime save helpers.

---

## AI Context Hints

### Adding New Settings

1. **Add field to SdkGraphicsSettings:**
   ```c
   typedef struct SdkGraphicsSettings {
       // ... existing fields ...
       bool my_new_feature_enabled;  // Add at end (before closing brace)
   } SdkGraphicsSettings;
   ```

2. **Add default value:**
   ```c
   void sdk_graphics_settings_default(SdkGraphicsSettings* out) {
       // ... existing defaults ...
       out->my_new_feature_enabled = true;  // Default ON
   }
   ```

3. **Add JSON parsing:**
   ```c
   int sdk_graphics_settings_load(SdkGraphicsSettings* out) {
       // ... existing parsing ...
       parse_bool_key(text, text + text_size, "my_new_feature_enabled", 
                      &out->my_new_feature_enabled);
   }
   ```

4. **Add JSON serialization:**
   ```c
   void sdk_graphics_settings_save(const SdkGraphicsSettings* settings) {
       // ... existing fields ...
       fprintf(file, "  \"my_new_feature_enabled\": %s,\n", 
               s.my_new_feature_enabled ? "true" : "false");
   }
   ```

5. **Add validation:**
   ```c
   bool clamp_and_fix(SdkGraphicsSettings* settings) {
       // ... existing clamping ...
       settings->my_new_feature_enabled = settings->my_new_feature_enabled ? true : false;
   }
   ```

6. **Bump version:**
   ```c
   #define SDK_GRAPHICS_SETTINGS_VERSION 9  // Increment
   ```

### Creating Preset Profiles

```c
void apply_graphics_preset(SdkGraphicsPreset preset) {
    switch (preset) {
        case SDK_GRAPHICS_PRESET_PERFORMANCE:
            g_graphics_settings.shadow_quality = SDK_SHADOW_QUALITY_LOW;
            g_graphics_settings.water_quality = SDK_WATER_QUALITY_LOW;
            g_graphics_settings.anti_aliasing_mode = SDK_ANTI_ALIASING_OFF;
            g_graphics_settings.anisotropy_level = 0;
            g_graphics_settings.chunk_grid_size = 7;  // Smaller radius
            break;
            
        case SDK_GRAPHICS_PRESET_HIGH:
            g_graphics_settings.shadow_quality = SDK_SHADOW_QUALITY_HIGH;
            g_graphics_settings.water_quality = SDK_WATER_QUALITY_HIGH;
            g_graphics_settings.anti_aliasing_mode = SDK_ANTI_ALIASING_FXAA;
            g_graphics_settings.anisotropy_level = 16;
            g_graphics_settings.chunk_grid_size = 15;  // Larger radius
            break;
            
        // BALANCED uses existing custom values or defaults
    }
    
    save_graphics_settings_now();
}
```

### Per-Platform Defaults

```c
void sdk_graphics_settings_default_for_platform(SdkGraphicsSettings* out) {
    sdk_graphics_settings_default(out);  // Base defaults
    
    #ifdef PLATFORM_LOW_END
    out->preset = SDK_GRAPHICS_PRESET_PERFORMANCE;
    out->shadow_quality = SDK_SHADOW_QUALITY_OFF;
    out->chunk_grid_size = 5;
    #endif
    
    #ifdef PLATFORM_HIGH_END
    out->preset = SDK_GRAPHICS_PRESET_HIGH;
    out->shadow_quality = SDK_SHADOW_QUALITY_HIGH;
    out->chunk_grid_size = 21;
    out->anisotropy_level = 16;
    #endif
}
```

### Auto-Detect Best Settings

```c
void auto_detect_optimal_settings() {
    // Query hardware capabilities
    int gpu_memory_mb = get_gpu_memory_mb();
    int cpu_cores = get_cpu_core_count();
    
    if (gpu_memory_mb > 8000 && cpu_cores >= 8) {
        apply_graphics_preset(SDK_GRAPHICS_PRESET_HIGH);
    } else if (gpu_memory_mb > 4000 && cpu_cores >= 4) {
        apply_graphics_preset(SDK_GRAPHICS_PRESET_BALANCED);
    } else {
        apply_graphics_preset(SDK_GRAPHICS_PRESET_PERFORMANCE);
    }
    
    // Auto-detect resolution from monitor
    int monitor_w, monitor_h;
    get_primary_monitor_resolution(&monitor_w, &monitor_h);
    g_graphics_settings.resolution_preset_index = 
        closest_resolution_preset_index(monitor_w, monitor_h);
    
    save_graphics_settings_now();
}
```

### Settings Backup/Restore

```c
#define SETTINGS_BACKUP_PATH "settings.json.bak"

void backup_settings() {
    // Copy settings.json to backup
    CopyFileA(SDK_GRAPHICS_SETTINGS_PATH, SETTINGS_BACKUP_PATH, FALSE);
}

void restore_settings_from_backup() {
    // Restore from backup
    if (CopyFileA(SETTINGS_BACKUP_PATH, SDK_GRAPHICS_SETTINGS_PATH, FALSE)) {
        // Reload
        sdk_graphics_settings_load(&g_graphics_settings);
        apply_all_settings();
    }
}
```

---

## Related Documentation

- `SDK/Docs/Core/Input/SDK_Input.md` - Input settings (separate `controls.cfg`)
- `SDK/Core/API/Session/` - Runtime settings application
- `SDK/Core/Renderer/` - Graphics implementation consuming settings
- `SDK/Core/Window/` - Window management (display modes, resolution)
- `SDK/Core/World/Chunks/` - Chunk grid size settings integration

---

**Source Files:**
- `SDK/Core/Settings/sdk_settings.h` (2,739 bytes) - Public API
- `SDK/Core/Settings/sdk_settings.c` (12,197 bytes) - JSON persistence implementation

**Settings File:**
- `settings.json` (generated at runtime in working directory)
