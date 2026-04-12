/**
 * sdk_settings.c -- Minimal graphics/settings persistence.
 */
#include "sdk_settings.h"
#include "../World/Chunks/ChunkManager/sdk_chunk_manager.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDK_GRAPHICS_SETTINGS_VERSION 2
#define SDK_GRAPHICS_SETTINGS_PATH "settings.json"
#define SDK_GRAPHICS_DEFAULT_WINDOW_WIDTH 3840
#define SDK_GRAPHICS_DEFAULT_WINDOW_HEIGHT 2400

static const int k_resolution_preset_widths[] = { 1280, 1920, 2560, 3840 };
static const int k_resolution_preset_heights[] = { 720, 1080, 1440, 2400 };
static const int k_anisotropy_levels[] = { 0, 2, 4, 8, 16 };
static const int k_far_mesh_distance_levels[] = { 0, 2, 4, 6, 8, 10, 12, 16 };

static int closest_resolution_preset_index(int width, int height)
{
    /* Finds closest resolution preset index by Euclidean distance */
    int best_index = 0;
    uint64_t best_distance = UINT64_MAX;
    int i;

    for (i = 0; i < (int)(sizeof(k_resolution_preset_widths) / sizeof(k_resolution_preset_widths[0])); ++i) {
        int64_t dx = (int64_t)width - (int64_t)k_resolution_preset_widths[i];
        int64_t dy = (int64_t)height - (int64_t)k_resolution_preset_heights[i];
        uint64_t distance = (uint64_t)(dx * dx + dy * dy);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return best_index;
}

static const char* skip_ws(const char* p, const char* end)
{
    /* Skips whitespace characters in JSON text */
    while (p && p < end && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static int closest_anisotropy_level(int level)
{
    /* Finds closest valid anisotropy level to requested value */
    int best_index = 0;
    int best_distance = INT_MAX;
    int i;

    for (i = 0; i < (int)(sizeof(k_anisotropy_levels) / sizeof(k_anisotropy_levels[0])); ++i) {
        int distance = level - k_anisotropy_levels[i];
        if (distance < 0) distance = -distance;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return k_anisotropy_levels[best_index];
}

static int closest_far_mesh_distance_level(int distance)
{
    /* Finds closest valid far mesh distance level to requested value */
    int best_index = 0;
    int best_distance = INT_MAX;
    int i;

    for (i = 0; i < (int)(sizeof(k_far_mesh_distance_levels) / sizeof(k_far_mesh_distance_levels[0])); ++i) {
        int current_distance = distance - k_far_mesh_distance_levels[i];
        if (current_distance < 0) current_distance = -current_distance;
        if (current_distance < best_distance) {
            best_distance = current_distance;
            best_index = i;
        }
    }

    return k_far_mesh_distance_levels[best_index];
}

static const char* find_key_value(const char* start, const char* end, const char* key)
{
    /* Locates JSON key and returns pointer to its value */
    char pattern[128];
    const char* p;
    const char* value;
    size_t key_len;

    if (!start || !end || !key) return NULL;
    key_len = strlen(key);
    if (key_len + 3u >= sizeof(pattern)) return NULL;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    p = strstr(start, pattern);
    if (!p || p >= end) return NULL;
    value = p + strlen(pattern);
    value = skip_ws(value, end);
    if (!value || value >= end || *value != ':') return NULL;
    value++;
    return skip_ws(value, end);
}

static int parse_int_key(const char* start, const char* end, const char* key, int* out_value)
{
    /* Parses integer value from JSON key, returns 1 on success */
    const char* value;
    char* parse_end = NULL;
    long parsed;

    value = find_key_value(start, end, key);
    if (!value) return 0;
    parsed = strtol(value, &parse_end, 10);
    if (parse_end == value) return 0;
    if (out_value) *out_value = (int)parsed;
    return 1;
}

static int parse_bool_key(const char* start, const char* end, const char* key, bool* out_value)
{
    /* Parses boolean value from JSON key, returns 1 on success */
    const char* value;

    value = find_key_value(start, end, key);
    if (!value) return 0;
    if ((size_t)(end - value) >= 4u && strncmp(value, "true", 4) == 0) {
        if (out_value) *out_value = true;
        return 1;
    }
    if ((size_t)(end - value) >= 5u && strncmp(value, "false", 5) == 0) {
        if (out_value) *out_value = false;
        return 1;
    }
    return 0;
}

static char* read_text_file(const char* path, size_t* out_size)
{
    /* Reads entire text file into allocated buffer, returns NULL on error */
    FILE* file = NULL;
    long len;
    char* text = NULL;
    size_t read_count;

    if (out_size) *out_size = 0;
    file = fopen(path, "rb");
    if (!file) return NULL;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    len = ftell(file);
    if (len < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    text = (char*)malloc((size_t)len + 1u);
    if (!text) {
        fclose(file);
        return NULL;
    }
    read_count = fread(text, 1u, (size_t)len, file);
    fclose(file);
    if (read_count != (size_t)len) {
        free(text);
        return NULL;
    }
    text[len] = '\0';
    if (out_size) *out_size = (size_t)len;
    return text;
}

static bool clamp_and_fix(SdkGraphicsSettings* settings)
{
    /* Validates and clamps all settings to valid ranges, returns true on success */
    if (!settings) return false;
    if (settings->preset < SDK_GRAPHICS_PRESET_PERFORMANCE || settings->preset > SDK_GRAPHICS_PRESET_HIGH) {
        settings->preset = SDK_GRAPHICS_PRESET_BALANCED;
    }
    settings->chunk_grid_size = sdk_chunk_manager_normalize_grid_size(settings->chunk_grid_size);
    if (settings->display_mode < SDK_DISPLAY_MODE_WINDOWED || settings->display_mode > SDK_DISPLAY_MODE_FULLSCREEN) {
        settings->display_mode = SDK_DISPLAY_MODE_WINDOWED;
    }
    if (settings->window_width < 320 || settings->window_width > 16384) {
        settings->window_width = SDK_GRAPHICS_DEFAULT_WINDOW_WIDTH;
    }
    if (settings->window_height < 240 || settings->window_height > 16384) {
        settings->window_height = SDK_GRAPHICS_DEFAULT_WINDOW_HEIGHT;
    }
    if (settings->fullscreen_width < 320 || settings->fullscreen_width > 16384) {
        settings->fullscreen_width = SDK_GRAPHICS_DEFAULT_WINDOW_WIDTH;
    }
    if (settings->fullscreen_height < 240 || settings->fullscreen_height > 16384) {
        settings->fullscreen_height = SDK_GRAPHICS_DEFAULT_WINDOW_HEIGHT;
    }
    if (settings->resolution_preset_index < 0 ||
        settings->resolution_preset_index >= (int)(sizeof(k_resolution_preset_widths) / sizeof(k_resolution_preset_widths[0]))) {
        settings->resolution_preset_index = closest_resolution_preset_index(settings->window_width, settings->window_height);
    }
    if (settings->version <= 0) {
        settings->version = SDK_GRAPHICS_SETTINGS_VERSION;
    }
    if (settings->render_scale_percent < 50 || settings->render_scale_percent > 100) {
        settings->render_scale_percent = 100;
    }
    settings->anisotropy_level = closest_anisotropy_level(settings->anisotropy_level);
    if (settings->anti_aliasing_mode < SDK_ANTI_ALIASING_OFF ||
        settings->anti_aliasing_mode > SDK_ANTI_ALIASING_FXAA) {
        settings->anti_aliasing_mode = SDK_ANTI_ALIASING_FXAA;
    }
    settings->smooth_lighting = settings->smooth_lighting ? true : false;
    if (settings->shadow_quality < SDK_SHADOW_QUALITY_OFF || settings->shadow_quality > SDK_SHADOW_QUALITY_HIGH) {
        settings->shadow_quality = SDK_SHADOW_QUALITY_MEDIUM;
    }
    if (settings->water_quality < SDK_WATER_QUALITY_LOW || settings->water_quality > SDK_WATER_QUALITY_HIGH) {
        settings->water_quality = SDK_WATER_QUALITY_HIGH;
    }
    {
        int render_distance = sdk_chunk_manager_radius_from_grid_size(settings->chunk_grid_size);
        settings->far_terrain_lod_distance_chunks =
            closest_far_mesh_distance_level(settings->far_terrain_lod_distance_chunks);
        settings->experimental_far_mesh_distance_chunks =
            closest_far_mesh_distance_level(settings->experimental_far_mesh_distance_chunks);
        if (settings->far_terrain_lod_distance_chunks > render_distance) {
            int i;
            int best = 0;
            for (i = 0; i < (int)(sizeof(k_far_mesh_distance_levels) / sizeof(k_far_mesh_distance_levels[0])); ++i) {
                int candidate = k_far_mesh_distance_levels[i];
                if (candidate <= render_distance) {
                    best = candidate;
                }
            }
            settings->far_terrain_lod_distance_chunks = best;
        }
    }
    {
        int render_distance = sdk_chunk_manager_radius_from_grid_size(settings->chunk_grid_size);
        if (settings->far_terrain_lod_distance_chunks > render_distance) {
            settings->far_terrain_lod_distance_chunks = closest_far_mesh_distance_level(render_distance);
        }
        if (settings->experimental_far_mesh_distance_chunks > render_distance) {
            settings->experimental_far_mesh_distance_chunks = closest_far_mesh_distance_level(render_distance);
        }
        if (settings->far_terrain_lod_distance_chunks > 0 &&
            settings->experimental_far_mesh_distance_chunks > settings->far_terrain_lod_distance_chunks) {
            settings->experimental_far_mesh_distance_chunks = settings->far_terrain_lod_distance_chunks;
        }
    }
    settings->black_superchunk_walls = settings->black_superchunk_walls ? true : false;
    settings->vsync = settings->vsync ? true : false;
    settings->fog_enabled = settings->fog_enabled ? true : false;
    return true;
}

void sdk_graphics_settings_default(SdkGraphicsSettings* out_settings)
{
    /* Fills settings with default values (balanced preset, high res window) */
    if (!out_settings) return;
    out_settings->version = SDK_GRAPHICS_SETTINGS_VERSION;
    out_settings->preset = SDK_GRAPHICS_PRESET_BALANCED;
    out_settings->chunk_grid_size = CHUNK_GRID_DEFAULT_SIZE;
    out_settings->display_mode = SDK_DISPLAY_MODE_WINDOWED;
    out_settings->resolution_preset_index = (int)(sizeof(k_resolution_preset_widths) / sizeof(k_resolution_preset_widths[0])) - 1;
    out_settings->window_width = SDK_GRAPHICS_DEFAULT_WINDOW_WIDTH;
    out_settings->window_height = SDK_GRAPHICS_DEFAULT_WINDOW_HEIGHT;
    out_settings->fullscreen_width = SDK_GRAPHICS_DEFAULT_WINDOW_WIDTH;
    out_settings->fullscreen_height = SDK_GRAPHICS_DEFAULT_WINDOW_HEIGHT;
    out_settings->render_scale_percent = 100;
    out_settings->anisotropy_level = 0;
    out_settings->anti_aliasing_mode = SDK_ANTI_ALIASING_FXAA;
    out_settings->smooth_lighting = true;
    out_settings->shadow_quality = SDK_SHADOW_QUALITY_MEDIUM;
    out_settings->water_quality = SDK_WATER_QUALITY_HIGH;
    out_settings->far_terrain_lod_distance_chunks = 8;
    out_settings->experimental_far_mesh_distance_chunks = 0;
    out_settings->black_superchunk_walls = false;
    out_settings->vsync = true;
    out_settings->fog_enabled = true;
    out_settings->superchunk_load_mode = SDK_SUPERCHUNK_LOAD_SYNC;
}

int sdk_graphics_settings_load(SdkGraphicsSettings* out_settings)
{
    /* Loads graphics settings from settings.json, applies defaults if missing/invalid */
    char* text;
    size_t text_size = 0;
    int version = SDK_GRAPHICS_SETTINGS_VERSION;
    int have_resolution_preset_index = 0;
    bool legacy_linear_filter = false;

    if (!out_settings) return 0;
    sdk_graphics_settings_default(out_settings);

    text = read_text_file(SDK_GRAPHICS_SETTINGS_PATH, &text_size);
    if (!text) return 0;

    if (parse_int_key(text, text + text_size, "version", &version) && version != SDK_GRAPHICS_SETTINGS_VERSION) {
        /* Accept older/newer simple files, but keep the defaults if parsing fails later. */
    }

    parse_int_key(text, text + text_size, "preset", (int*)&out_settings->preset);
    parse_int_key(text, text + text_size, "chunk_grid_size", &out_settings->chunk_grid_size);
    parse_int_key(text, text + text_size, "display_mode", (int*)&out_settings->display_mode);
    have_resolution_preset_index = parse_int_key(text, text + text_size, "resolution_preset_index",
                                                 &out_settings->resolution_preset_index);
    parse_int_key(text, text + text_size, "window_width", &out_settings->window_width);
    parse_int_key(text, text + text_size, "window_height", &out_settings->window_height);
    parse_int_key(text, text + text_size, "fullscreen_width", &out_settings->fullscreen_width);
    parse_int_key(text, text + text_size, "fullscreen_height", &out_settings->fullscreen_height);
    parse_int_key(text, text + text_size, "render_scale_percent", &out_settings->render_scale_percent);
    if (!parse_int_key(text, text + text_size, "anisotropy_level", &out_settings->anisotropy_level)) {
        if (parse_bool_key(text, text + text_size, "texture_filter_linear", &legacy_linear_filter)) {
            out_settings->anisotropy_level = legacy_linear_filter ? 4 : 0;
        }
    }
    parse_int_key(text, text + text_size, "anti_aliasing_mode", (int*)&out_settings->anti_aliasing_mode);
    parse_bool_key(text, text + text_size, "smooth_lighting", &out_settings->smooth_lighting);
    parse_int_key(text, text + text_size, "shadow_quality", (int*)&out_settings->shadow_quality);
    parse_int_key(text, text + text_size, "water_quality", (int*)&out_settings->water_quality);
    if (!parse_int_key(text, text + text_size, "far_terrain_lod_distance_chunks",
                       &out_settings->far_terrain_lod_distance_chunks)) {
        bool legacy_far_lod = false;
        if (!parse_bool_key(text, text + text_size, "far_terrain_lod_enabled", &legacy_far_lod)) {
            parse_bool_key(text, text + text_size, "far_subchunk_cull_enabled", &legacy_far_lod);
        }
        out_settings->far_terrain_lod_distance_chunks = legacy_far_lod ? 8 : 0;
    }
    parse_int_key(text, text + text_size, "experimental_far_mesh_distance_chunks",
                  &out_settings->experimental_far_mesh_distance_chunks);
    parse_bool_key(text, text + text_size, "black_superchunk_walls", &out_settings->black_superchunk_walls);
    parse_bool_key(text, text + text_size, "vsync", &out_settings->vsync);
    parse_bool_key(text, text + text_size, "fog_enabled", &out_settings->fog_enabled);
    parse_int_key(text, text + text_size, "superchunk_load_mode", (int*)&out_settings->superchunk_load_mode);
    out_settings->version = version;
    if (!have_resolution_preset_index || version < SDK_GRAPHICS_SETTINGS_VERSION) {
        out_settings->resolution_preset_index =
            closest_resolution_preset_index(out_settings->window_width, out_settings->window_height);
    }
    clamp_and_fix(out_settings);

    free(text);
    return 1;
}

void sdk_graphics_settings_save(const SdkGraphicsSettings* settings)
{
    /* Saves graphics settings to settings.json in JSON format */
    FILE* file;
    SdkGraphicsSettings s;

    s = settings ? *settings : (SdkGraphicsSettings){0};
    clamp_and_fix(&s);

    file = fopen(SDK_GRAPHICS_SETTINGS_PATH, "wb");
    if (!file) return;

    fprintf(file, "{\n");
    fprintf(file, "  \"version\": %d,\n", SDK_GRAPHICS_SETTINGS_VERSION);
    fprintf(file, "  \"preset\": %d,\n", (int)s.preset);
    fprintf(file, "  \"chunk_grid_size\": %d,\n", s.chunk_grid_size);
    fprintf(file, "  \"display_mode\": %d,\n", (int)s.display_mode);
    fprintf(file, "  \"resolution_preset_index\": %d,\n", s.resolution_preset_index);
    fprintf(file, "  \"window_width\": %d,\n", s.window_width);
    fprintf(file, "  \"window_height\": %d,\n", s.window_height);
    fprintf(file, "  \"fullscreen_width\": %d,\n", s.fullscreen_width);
    fprintf(file, "  \"fullscreen_height\": %d,\n", s.fullscreen_height);
    fprintf(file, "  \"render_scale_percent\": %d,\n", s.render_scale_percent);
    fprintf(file, "  \"anisotropy_level\": %d,\n", s.anisotropy_level);
    fprintf(file, "  \"anti_aliasing_mode\": %d,\n", (int)s.anti_aliasing_mode);
    fprintf(file, "  \"smooth_lighting\": %s,\n", s.smooth_lighting ? "true" : "false");
    fprintf(file, "  \"shadow_quality\": %d,\n", (int)s.shadow_quality);
    fprintf(file, "  \"water_quality\": %d,\n", (int)s.water_quality);
    fprintf(file, "  \"far_terrain_lod_distance_chunks\": %d,\n", s.far_terrain_lod_distance_chunks);
    fprintf(file, "  \"experimental_far_mesh_distance_chunks\": %d,\n", s.experimental_far_mesh_distance_chunks);
    fprintf(file, "  \"black_superchunk_walls\": %s,\n", s.black_superchunk_walls ? "true" : "false");
    fprintf(file, "  \"vsync\": %s,\n", s.vsync ? "true" : "false");
    fprintf(file, "  \"fog_enabled\": %s,\n", s.fog_enabled ? "true" : "false");
    fprintf(file, "  \"superchunk_load_mode\": %d\n", (int)s.superchunk_load_mode);
    fprintf(file, "}\n");
    fclose(file);
}
