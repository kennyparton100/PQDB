/**
 * sdk_settings.h -- Minimal graphics/settings persistence.
 */
#ifndef NQLSDK_SETTINGS_H
#define NQLSDK_SETTINGS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SdkGraphicsPreset {
    SDK_GRAPHICS_PRESET_PERFORMANCE = 0,
    SDK_GRAPHICS_PRESET_BALANCED = 1,
    SDK_GRAPHICS_PRESET_HIGH = 2
} SdkGraphicsPreset;

typedef enum SdkDisplayMode {
    SDK_DISPLAY_MODE_WINDOWED = 0,
    SDK_DISPLAY_MODE_BORDERLESS = 1,
    SDK_DISPLAY_MODE_FULLSCREEN = 2
} SdkDisplayMode;

typedef enum SdkAntiAliasingMode {
    SDK_ANTI_ALIASING_OFF = 0,
    SDK_ANTI_ALIASING_FXAA = 1
} SdkAntiAliasingMode;

typedef enum SdkShadowQuality {
    SDK_SHADOW_QUALITY_OFF = 0,
    SDK_SHADOW_QUALITY_LOW = 1,
    SDK_SHADOW_QUALITY_MEDIUM = 2,
    SDK_SHADOW_QUALITY_HIGH = 3
} SdkShadowQuality;

typedef enum SdkWaterQuality {
    SDK_WATER_QUALITY_LOW = 0,
    SDK_WATER_QUALITY_HIGH = 1
} SdkWaterQuality;

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

typedef enum SdkSuperchunkLoadMode {
    SDK_SUPERCHUNK_LOAD_SYNC = 0,
    SDK_SUPERCHUNK_LOAD_ASYNC = 1
} SdkSuperchunkLoadMode;

typedef struct SdkGraphicsSettings {
    int                 version;
    SdkGraphicsPreset    preset;
    int                 chunk_grid_size;
    SdkDisplayMode      display_mode;
    int                 resolution_preset_index;
    int                 window_width;
    int                 window_height;
    int                 fullscreen_width;
    int                 fullscreen_height;
    int                 render_scale_percent;
    int                 anisotropy_level;
    SdkAntiAliasingMode anti_aliasing_mode;
    bool                smooth_lighting;
    SdkShadowQuality    shadow_quality;
    SdkWaterQuality     water_quality;
    int                 far_terrain_lod_distance_chunks;
    int                 experimental_far_mesh_distance_chunks;
    bool                black_superchunk_walls;
    bool                vsync;
    bool                fog_enabled;
    SdkSuperchunkLoadMode superchunk_load_mode;
} SdkGraphicsSettings;

void sdk_graphics_settings_default(SdkGraphicsSettings* out_settings);
int  sdk_graphics_settings_load(SdkGraphicsSettings* out_settings);
void sdk_graphics_settings_save(const SdkGraphicsSettings* settings);

extern SdkGraphicsSettings g_graphics_settings;

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SETTINGS_H */
