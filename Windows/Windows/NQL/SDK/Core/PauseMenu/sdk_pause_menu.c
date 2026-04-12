#include "../Runtime/sdk_runtime_internal.h"

static const char* profiler_graphics_preset_name(SdkGraphicsPreset preset)
{
    switch (preset) {
        case SDK_GRAPHICS_PRESET_PERFORMANCE: return "performance";
        case SDK_GRAPHICS_PRESET_BALANCED:    return "balanced";
        case SDK_GRAPHICS_PRESET_HIGH:        return "high";
        default:                              return "unknown";
    }
}

static const char* profiler_display_mode_name(SdkDisplayMode mode)
{
    switch (mode) {
        case SDK_DISPLAY_MODE_WINDOWED:   return "windowed";
        case SDK_DISPLAY_MODE_BORDERLESS: return "borderless";
        case SDK_DISPLAY_MODE_FULLSCREEN: return "fullscreen";
        default:                          return "unknown";
    }
}

static void profiler_log_current_graphics_snapshot(const char* tag)
{
    char note[768];
    int render_distance_chunks =
        sdk_chunk_manager_radius_from_grid_size(g_graphics_settings.chunk_grid_size);
    int far_lod_chunks =
        normalize_far_mesh_lod_distance(render_distance_chunks,
                                        g_graphics_settings.far_terrain_lod_distance_chunks);
    int experimental_far_chunks =
        normalize_experimental_far_mesh_distance(render_distance_chunks,
                                                 far_lod_chunks,
                                                 g_graphics_settings.experimental_far_mesh_distance_chunks);

    sprintf_s(note, sizeof(note),
              "preset=%s display=%s grid=%d render_distance=%d far_lod=%d experimental_far=%d "
              "render_scale=%d anisotropy=%d aa=%d smooth=%d shadow=%d water=%d black_walls=%d "
              "vsync=%d fog=%d window=%dx%d fullscreen=%dx%d overlays[F6=%d F7=%d F8=%d] pause_view=%d",
              profiler_graphics_preset_name(g_graphics_settings.preset),
              profiler_display_mode_name(g_graphics_settings.display_mode),
              g_graphics_settings.chunk_grid_size,
              render_distance_chunks,
              far_lod_chunks,
              experimental_far_chunks,
              g_graphics_settings.render_scale_percent,
              g_graphics_settings.anisotropy_level,
              (int)g_graphics_settings.anti_aliasing_mode,
              g_graphics_settings.smooth_lighting ? 1 : 0,
              (int)g_graphics_settings.shadow_quality,
              (int)g_graphics_settings.water_quality,
              g_graphics_settings.black_superchunk_walls ? 1 : 0,
              g_graphics_settings.vsync ? 1 : 0,
              g_graphics_settings.fog_enabled ? 1 : 0,
              g_graphics_settings.window_width,
              g_graphics_settings.window_height,
              g_graphics_settings.fullscreen_width,
              g_graphics_settings.fullscreen_height,
              g_settlement_debug_overlay ? 1 : 0,
              g_fluid_debug_overlay ? 1 : 0,
              g_perf_debug_overlay ? 1 : 0,
              g_pause_menu_view);
    sdk_profiler_log_note(&g_profiler, tag, note);
}

void command_close(void)
{
    g_command_open = false;
    g_command_text[0] = '\0';
    g_command_text_len = 0;
    g_command_enter_was_down = false;
    g_command_backspace_was_down = false;
    sdk_window_clear_char_queue(g_sdk.window);
}

void command_open(void)
{
    g_command_open = true;
    strcpy_s(g_command_text, sizeof(g_command_text), "/");
    g_command_text_len = 1;
    g_command_enter_was_down = false;
    g_command_backspace_was_down = false;
    sdk_window_clear_char_queue(g_sdk.window);
}

void teleport_player_to(float world_x, float world_y, float world_z,
                               float* cam_x, float* cam_y, float* cam_z,
                               float* look_x, float* look_y, float* look_z)
{
    float eye_y = world_y + PLAYER_EYE_H;
    float cos_p = cosf(g_cam_pitch);
    float sin_p = sinf(g_cam_pitch);

    if (cam_x) *cam_x = world_x;
    if (cam_y) *cam_y = eye_y;
    if (cam_z) *cam_z = world_z;
    if (look_x) *look_x = world_x + g_cam_look_dist * cos_p * sinf(g_cam_yaw);
    if (look_y) *look_y = eye_y + g_cam_look_dist * sin_p;
    if (look_z) *look_z = world_z + g_cam_look_dist * cos_p * cosf(g_cam_yaw);

    g_vel_y = 0.0f;
    g_on_ground = false;
    g_was_on_ground = false;
    g_fall_start_y = world_y;
}

void command_execute(float* cam_x, float* cam_y, float* cam_z,
                            float* look_x, float* look_y, float* look_z)
{
    char command[16];
    float x, y, z;

    if (sscanf(g_command_text, "/%15s %f %f %f", command, &x, &y, &z) == 4) {
        if (_stricmp(command, "TP") == 0) {
            teleport_player_to(x, y, z, cam_x, cam_y, cam_z, look_x, look_y, look_z);
        }
    }
    command_close();
}

void command_line_handle_input(float* cam_x, float* cam_y, float* cam_z,
                                      float* look_x, float* look_y, float* look_z)
{
    uint32_t ch;
    bool enter_down = sdk_window_is_key_down(g_sdk.window, VK_RETURN);
    bool backspace_down = sdk_window_is_key_down(g_sdk.window, VK_BACK);

    while (sdk_window_pop_char(g_sdk.window, &ch)) {
        if (ch >= 32u && ch < 127u && g_command_text_len + 1 < SDK_COMMAND_LINE_TEXT_MAX) {
            g_command_text[g_command_text_len++] = (char)ch;
            g_command_text[g_command_text_len] = '\0';
        }
    }

    if (backspace_down && !g_command_backspace_was_down) {
        if (g_command_text_len > 1) {
            g_command_text[--g_command_text_len] = '\0';
        } else {
            command_close();
        }
    }
    if (enter_down && !g_command_enter_was_down) {
        command_execute(cam_x, cam_y, cam_z, look_x, look_y, look_z);
    }

    g_command_backspace_was_down = backspace_down;
    g_command_enter_was_down = enter_down;
}

static void leave_world_from_pause_menu(void)
{
    if (g_client_connection.connected &&
        g_client_connection.kind == SDK_CLIENT_CONNECTION_LOCAL_HOST &&
        g_local_host_manager.active) {
        g_online_section_focus = 1;
        sdk_client_disconnect_to_frontend(SDK_START_MENU_VIEW_ONLINE);
    } else {
        begin_async_return_to_start();
    }
}

static void pause_character_clamp_selection(void)
{
    int max_scroll;

    if (g_character_asset_count <= 0) {
        g_pause_character_selected = 0;
        g_pause_character_scroll = 0;
        return;
    }

    g_pause_character_selected = api_clampi(g_pause_character_selected, 0, g_character_asset_count - 1);
    max_scroll = api_clampi(g_character_asset_count - SDK_START_MENU_ASSET_VISIBLE_MAX, 0, g_character_asset_count);
    g_pause_character_scroll = api_clampi(g_pause_character_scroll, 0, max_scroll);
    if (g_pause_character_selected < g_pause_character_scroll) {
        g_pause_character_scroll = g_pause_character_selected;
    }
    if (g_pause_character_selected >= g_pause_character_scroll + SDK_START_MENU_ASSET_VISIBLE_MAX) {
        g_pause_character_scroll = g_pause_character_selected - SDK_START_MENU_ASSET_VISIBLE_MAX + 1;
    }
}

static int keybind_menu_row_count(void)
{
    return SDK_INPUT_ACTION_COUNT + 3;
}

static void pause_menu_clamp_scroll_selection(int total_rows, int visible_rows, int* io_selected, int* io_scroll)
{
    int max_scroll;

    if (!io_selected || !io_scroll) return;
    if (total_rows <= 0) {
        *io_selected = 0;
        *io_scroll = 0;
        return;
    }

    *io_selected = api_clampi(*io_selected, 0, total_rows - 1);
    max_scroll = api_clampi(total_rows - visible_rows, 0, total_rows);
    *io_scroll = api_clampi(*io_scroll, 0, max_scroll);
    if (*io_selected < *io_scroll) {
        *io_scroll = *io_selected;
    }
    if (*io_selected >= *io_scroll + visible_rows) {
        *io_scroll = *io_selected - visible_rows + 1;
    }
}

static void keybind_menu_clamp_selection(void)
{
    pause_menu_clamp_scroll_selection(keybind_menu_row_count(), SDK_KEYBIND_MENU_VISIBLE_ROWS,
                                      &g_keybind_menu_selected, &g_keybind_menu_scroll);
}

enum {
    SDK_CREATIVE_SHAPE_FOCUS_RESULTS = 0,
    SDK_CREATIVE_SHAPE_FOCUS_PANEL = 1
};

enum {
    SDK_CREATIVE_SHAPE_ROW_WIDTH = 0,
    SDK_CREATIVE_SHAPE_ROW_HEIGHT = 1,
    SDK_CREATIVE_SHAPE_ROW_DEPTH = 2,
    SDK_CREATIVE_SHAPE_ROW_COUNT = 3
};

static void creative_reset_shape_state(void)
{
    g_creative_shape_focus = SDK_CREATIVE_SHAPE_FOCUS_RESULTS;
    g_creative_shape_row = SDK_CREATIVE_SHAPE_ROW_WIDTH;
    g_creative_shape_width = 16;
    g_creative_shape_height = 16;
    g_creative_shape_depth = 16;
}

static void creative_clamp_shape_state(void)
{
    CreativeEntry selected = creative_entry_for_filtered_index(g_creative_menu_selected);

    g_creative_shape_row = api_clampi(g_creative_shape_row, 0, SDK_CREATIVE_SHAPE_ROW_COUNT - 1);
    g_creative_shape_width = api_clampi(g_creative_shape_width, 1, 16);
    g_creative_shape_height = api_clampi(g_creative_shape_height, 1, 16);
    g_creative_shape_depth = api_clampi(g_creative_shape_depth, 1, 16);
    if (selected.kind != SDK_CREATIVE_ENTRY_BLOCK || selected.id == BLOCK_AIR) {
        g_creative_shape_focus = SDK_CREATIVE_SHAPE_FOCUS_RESULTS;
    } else {
        g_creative_shape_focus = api_clampi(g_creative_shape_focus,
                                            SDK_CREATIVE_SHAPE_FOCUS_RESULTS,
                                            SDK_CREATIVE_SHAPE_FOCUS_PANEL);
    }
}

static void creative_grant_selected_entry(void)
{
    CreativeEntry selected = creative_entry_for_filtered_index(g_creative_menu_selected);

    if (selected.kind == SDK_CREATIVE_ENTRY_BLOCK && selected.id != BLOCK_AIR) {
        if (g_creative_shape_width == 16 &&
            g_creative_shape_height == 16 &&
            g_creative_shape_depth == 16) {
            hotbar_set_creative_block(g_hotbar_selected, (BlockType)selected.id);
        } else {
            SdkConstructionItemPayload payload;
            sdk_construction_payload_make_box((BlockType)selected.id,
                                              g_creative_shape_width,
                                              g_creative_shape_height,
                                              g_creative_shape_depth,
                                              &payload);
            hotbar_set_shaped_payload(g_hotbar_selected, &payload);
        }
    } else if (selected.kind == SDK_CREATIVE_ENTRY_ITEM && selected.id != ITEM_NONE) {
        hotbar_set_item(g_hotbar_selected, (ItemType)selected.id,
                        creative_item_grant_count((ItemType)selected.id));
    }
}

static void apply_graphics_preset_defaults(SdkGraphicsPreset preset)
{
    g_graphics_settings.preset = preset;
    switch (preset) {
        case SDK_GRAPHICS_PRESET_PERFORMANCE:
            g_graphics_settings.render_scale_percent = 75;
            g_graphics_settings.anti_aliasing_mode = SDK_ANTI_ALIASING_OFF;
            g_graphics_settings.smooth_lighting = false;
            g_graphics_settings.shadow_quality = SDK_SHADOW_QUALITY_OFF;
            g_graphics_settings.water_quality = SDK_WATER_QUALITY_LOW;
            g_graphics_settings.anisotropy_level = 0;
            g_graphics_settings.experimental_far_mesh_distance_chunks = 6;
            g_graphics_settings.far_terrain_lod_distance_chunks = 10;
            break;
        case SDK_GRAPHICS_PRESET_HIGH:
            g_graphics_settings.render_scale_percent = 100;
            g_graphics_settings.anti_aliasing_mode = SDK_ANTI_ALIASING_FXAA;
            g_graphics_settings.smooth_lighting = true;
            g_graphics_settings.shadow_quality = SDK_SHADOW_QUALITY_HIGH;
            g_graphics_settings.water_quality = SDK_WATER_QUALITY_HIGH;
            g_graphics_settings.anisotropy_level = 16;
            g_graphics_settings.far_terrain_lod_distance_chunks = 0;
            g_graphics_settings.experimental_far_mesh_distance_chunks = 0;
            break;
        case SDK_GRAPHICS_PRESET_BALANCED:
        default:
            g_graphics_settings.render_scale_percent = 100;
            g_graphics_settings.anti_aliasing_mode = SDK_ANTI_ALIASING_FXAA;
            g_graphics_settings.smooth_lighting = true;
            g_graphics_settings.shadow_quality = SDK_SHADOW_QUALITY_MEDIUM;
            g_graphics_settings.water_quality = SDK_WATER_QUALITY_HIGH;
            g_graphics_settings.anisotropy_level = 4;
            g_graphics_settings.far_terrain_lod_distance_chunks = 8;
            g_graphics_settings.experimental_far_mesh_distance_chunks = 0;
            break;
    }
    g_graphics_settings.far_terrain_lod_distance_chunks =
        normalize_far_mesh_lod_distance(sdk_chunk_manager_radius_from_grid_size(g_chunk_grid_size_setting),
                                        g_graphics_settings.far_terrain_lod_distance_chunks);
    g_graphics_settings.experimental_far_mesh_distance_chunks =
        normalize_experimental_far_mesh_distance(
            sdk_chunk_manager_radius_from_grid_size(g_chunk_grid_size_setting),
            g_graphics_settings.far_terrain_lod_distance_chunks,
            g_graphics_settings.experimental_far_mesh_distance_chunks);
    sdk_mesh_set_smooth_lighting_enabled(g_graphics_settings.smooth_lighting ? 1 : 0);
    mark_all_loaded_chunks_dirty();
    save_graphics_settings_now();
}

void pause_menu_handle_input(void)
{
    bool nav_up = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_UP) ||
                  sdk_input_raw_key_down(VK_UP) || sdk_input_raw_key_down((uint8_t)'W');
    bool nav_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_DOWN) ||
                    sdk_input_raw_key_down(VK_DOWN) || sdk_input_raw_key_down((uint8_t)'S');
    bool nav_left = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_LEFT) ||
                    sdk_input_raw_key_down(VK_LEFT) || sdk_input_raw_key_down((uint8_t)'A');
    bool nav_right = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_RIGHT) ||
                     sdk_input_raw_key_down(VK_RIGHT) || sdk_input_raw_key_down((uint8_t)'D');
    bool activate = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_CONFIRM) ||
                    sdk_input_raw_key_down(VK_RETURN);
    bool backspace = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_BACK) ||
                     sdk_input_raw_key_down(VK_BACK) || sdk_input_raw_key_down(VK_ESCAPE);
    bool raw_r_pressed = sdk_input_raw_key_pressed((uint8_t)'R');
    bool raw_escape_pressed = sdk_input_raw_key_pressed(VK_ESCAPE);
    uint32_t ch;

    g_pause_menu_selected = api_clampi(g_pause_menu_selected, 0, SDK_PAUSE_MENU_OPTION_COUNT - 1);
    g_graphics_menu_selected = api_clampi(g_graphics_menu_selected, 0, SDK_GRAPHICS_MENU_ROW_COUNT - 1);
    g_graphics_settings.preset = (SdkGraphicsPreset)api_clampi((int)g_graphics_settings.preset,
                                                               SDK_GRAPHICS_PRESET_PERFORMANCE,
                                                               SDK_GRAPHICS_PRESET_HIGH);
    g_graphics_settings.resolution_preset_index =
        clamp_resolution_preset_index(g_graphics_settings.resolution_preset_index);
    g_graphics_settings.far_terrain_lod_distance_chunks =
        normalize_far_mesh_lod_distance(sdk_chunk_manager_radius_from_grid_size(g_chunk_grid_size_setting),
                                        g_graphics_settings.far_terrain_lod_distance_chunks);
    g_graphics_settings.experimental_far_mesh_distance_chunks =
        normalize_experimental_far_mesh_distance(
            sdk_chunk_manager_radius_from_grid_size(g_chunk_grid_size_setting),
            g_graphics_settings.far_terrain_lod_distance_chunks,
            g_graphics_settings.experimental_far_mesh_distance_chunks);

    if (g_pause_menu_view == SDK_PAUSE_MENU_VIEW_MAIN) {
        if (nav_up && !g_pause_menu_nav_was_down[0] && g_pause_menu_selected > 0) {
            g_pause_menu_selected--;
        }
        if (nav_down && !g_pause_menu_nav_was_down[1] && g_pause_menu_selected < SDK_PAUSE_MENU_OPTION_COUNT - 1) {
            g_pause_menu_selected++;
        }
        if (activate && !g_pause_menu_nav_was_down[4]) {
            if (g_pause_menu_selected == 0) {
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_GRAPHICS;
                g_graphics_menu_selected = 0;
            } else if (g_pause_menu_selected == 1) {
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_KEY_BINDINGS;
                g_keybind_menu_selected = 0;
                g_keybind_menu_scroll = 0;
                g_keybind_capture_active = false;
            } else if (g_pause_menu_selected == 2) {
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_CREATIVE;
                creative_clamp_selection();
                creative_reset_shape_state();
                sdk_window_clear_char_queue(g_sdk.window);
            } else if (g_pause_menu_selected == 3) {
                refresh_character_assets();
                g_pause_character_selected = (g_selected_character_index >= 0) ? g_selected_character_index : 0;
                g_pause_character_scroll = 0;
                pause_character_clamp_selection();
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_SELECT_CHARACTER;
            } else if (g_pause_menu_selected == 4) {
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER;
                g_chunk_manager_selected = 0;
                g_chunk_manager_scroll = 0;
            } else if (g_pause_menu_selected == 5) {
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_DEBUG_PROFILER;
            } else if (g_pause_menu_selected == 6) {
                leave_world_from_pause_menu();
            } else if (g_pause_menu_selected == 7) {
                g_pause_menu_open = false;
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
                sdk_window_request_close(g_sdk.window);
            }
        }
    } else if (g_pause_menu_view == SDK_PAUSE_MENU_VIEW_GRAPHICS) {
        if (nav_up && !g_pause_menu_nav_was_down[0] && g_graphics_menu_selected > 0) {
            g_graphics_menu_selected--;
        }
        if (nav_down && !g_pause_menu_nav_was_down[1] &&
            g_graphics_menu_selected < SDK_GRAPHICS_MENU_ROW_COUNT - 1) {
            g_graphics_menu_selected++;
        }
        if ((nav_left && !g_pause_menu_nav_was_down[2]) ||
            (nav_right && !g_pause_menu_nav_was_down[3])) {
            const int dir = nav_right ? 1 : -1;
            static const int anisotropy_presets[SDK_ANISOTROPY_PRESET_COUNT] = { 0, 2, 4, 8, 16 };
            switch (g_graphics_menu_selected) {
                case SDK_GRAPHICS_MENU_ROW_QUALITY_PRESET:
                    if (dir < 0 && g_graphics_settings.preset > SDK_GRAPHICS_PRESET_PERFORMANCE) {
                        apply_graphics_preset_defaults((SdkGraphicsPreset)((int)g_graphics_settings.preset - 1));
                    } else if (dir > 0 && g_graphics_settings.preset < SDK_GRAPHICS_PRESET_HIGH) {
                        apply_graphics_preset_defaults((SdkGraphicsPreset)((int)g_graphics_settings.preset + 1));
                    }
                    break;
                case SDK_GRAPHICS_MENU_ROW_DISPLAY_MODE:
                    g_graphics_settings.display_mode = (SdkDisplayMode)
                        ((int)(g_graphics_settings.display_mode + dir + 3) % 3);
                    apply_display_mode_setting(true);
                    break;
                case SDK_GRAPHICS_MENU_ROW_RESOLUTION:
                {
                    if (g_graphics_settings.display_mode != SDK_DISPLAY_MODE_BORDERLESS) {
                        int preset_index =
                            clamp_resolution_preset_index(g_graphics_settings.resolution_preset_index + dir);
                        apply_resolution_preset_index(preset_index, true);
                    }
                    break;
                }
                case SDK_GRAPHICS_MENU_ROW_RENDER_SCALE:
                    g_graphics_settings.render_scale_percent =
                        clamp_render_scale_percent(g_graphics_settings.render_scale_percent + dir * 5);
                    save_graphics_settings_now();
                    break;
                case SDK_GRAPHICS_MENU_ROW_ANTI_ALIASING:
                    g_graphics_settings.anti_aliasing_mode =
                        (g_graphics_settings.anti_aliasing_mode == SDK_ANTI_ALIASING_OFF)
                            ? SDK_ANTI_ALIASING_FXAA
                            : SDK_ANTI_ALIASING_OFF;
                    save_graphics_settings_now();
                    break;
                case SDK_GRAPHICS_MENU_ROW_SMOOTH_LIGHTING:
                    g_graphics_settings.smooth_lighting = !g_graphics_settings.smooth_lighting;
                    sdk_mesh_set_smooth_lighting_enabled(g_graphics_settings.smooth_lighting ? 1 : 0);
                    mark_all_loaded_chunks_dirty();
                    save_graphics_settings_now();
                    break;
                case SDK_GRAPHICS_MENU_ROW_SHADOW_QUALITY:
                    g_graphics_settings.shadow_quality = (SdkShadowQuality)
                        ((int)(g_graphics_settings.shadow_quality + dir + 4) % 4);
                    save_graphics_settings_now();
                    break;
                case SDK_GRAPHICS_MENU_ROW_WATER_QUALITY:
                    g_graphics_settings.water_quality =
                        (g_graphics_settings.water_quality == SDK_WATER_QUALITY_LOW)
                            ? SDK_WATER_QUALITY_HIGH
                            : SDK_WATER_QUALITY_LOW;
                    save_graphics_settings_now();
                    break;
                case SDK_GRAPHICS_MENU_ROW_RENDER_DISTANCE:
                {
                    int preset_index = render_distance_preset_index(
                        sdk_chunk_manager_radius_from_grid_size(g_chunk_grid_size_setting));
                    preset_index = (preset_index + dir + SDK_RENDER_DISTANCE_PRESET_COUNT) %
                                   SDK_RENDER_DISTANCE_PRESET_COUNT;
                    rebuild_chunk_grid_for_current_camera(
                        sdk_chunk_manager_grid_size_from_radius(g_render_distance_presets[preset_index]));
                    g_graphics_settings.far_terrain_lod_distance_chunks =
                        normalize_far_mesh_lod_distance(g_render_distance_presets[preset_index],
                                                        g_graphics_settings.far_terrain_lod_distance_chunks);
                    g_graphics_settings.experimental_far_mesh_distance_chunks =
                        normalize_experimental_far_mesh_distance(
                            g_render_distance_presets[preset_index],
                            g_graphics_settings.far_terrain_lod_distance_chunks,
                            g_graphics_settings.experimental_far_mesh_distance_chunks);
                    break;
                }
                case SDK_GRAPHICS_MENU_ROW_ANISOTROPIC_SAMPLING:
                {
                    int preset_index = anisotropy_preset_index(g_graphics_settings.anisotropy_level);
                    preset_index = (preset_index + dir + SDK_ANISOTROPY_PRESET_COUNT) %
                                   SDK_ANISOTROPY_PRESET_COUNT;
                    g_graphics_settings.anisotropy_level = anisotropy_presets[preset_index];
                    save_graphics_settings_now();
                    break;
                }
                case SDK_GRAPHICS_MENU_ROW_FAR_TERRAIN_LOD:
                {
                    int preset_index = far_mesh_distance_preset_index(g_graphics_settings.far_terrain_lod_distance_chunks);
                    preset_index = (preset_index + dir + SDK_FAR_MESH_DISTANCE_PRESET_COUNT) %
                                   SDK_FAR_MESH_DISTANCE_PRESET_COUNT;
                    g_graphics_settings.far_terrain_lod_distance_chunks =
                        g_far_mesh_distance_presets[preset_index];
                    g_graphics_settings.far_terrain_lod_distance_chunks =
                        normalize_far_mesh_lod_distance(
                            sdk_chunk_manager_radius_from_grid_size(g_chunk_grid_size_setting),
                            g_graphics_settings.far_terrain_lod_distance_chunks);
                    g_graphics_settings.experimental_far_mesh_distance_chunks =
                        normalize_experimental_far_mesh_distance(
                            sdk_chunk_manager_radius_from_grid_size(g_chunk_grid_size_setting),
                            g_graphics_settings.far_terrain_lod_distance_chunks,
                            g_graphics_settings.experimental_far_mesh_distance_chunks);
                    mark_all_loaded_chunks_dirty();
                    save_graphics_settings_now();
                    break;
                }
                case SDK_GRAPHICS_MENU_ROW_EXPERIMENTAL_FAR_MESHES:
                {
                    int preset_index =
                        far_mesh_distance_preset_index(g_graphics_settings.experimental_far_mesh_distance_chunks);
                    preset_index = (preset_index + dir + SDK_FAR_MESH_DISTANCE_PRESET_COUNT) %
                                   SDK_FAR_MESH_DISTANCE_PRESET_COUNT;
                    g_graphics_settings.experimental_far_mesh_distance_chunks =
                        g_far_mesh_distance_presets[preset_index];
                    g_graphics_settings.experimental_far_mesh_distance_chunks =
                        normalize_experimental_far_mesh_distance(
                            sdk_chunk_manager_radius_from_grid_size(g_chunk_grid_size_setting),
                            g_graphics_settings.far_terrain_lod_distance_chunks,
                            g_graphics_settings.experimental_far_mesh_distance_chunks);
                    mark_all_loaded_chunks_dirty();
                    save_graphics_settings_now();
                    break;
                }
                case SDK_GRAPHICS_MENU_ROW_BLACK_SUPERCHUNK_WALLS:
                    g_graphics_settings.black_superchunk_walls =
                        g_graphics_settings.black_superchunk_walls ? false : true;
                    save_graphics_settings_now();
                    break;
                case SDK_GRAPHICS_MENU_ROW_SUPERCHUNK_LOAD_MODE:
                {
                    int mode = (int)g_graphics_settings.superchunk_load_mode;
                    mode = (mode + 1) % 2; // Toggle between 0 (SYNC) and 1 (ASYNC)
                    g_graphics_settings.superchunk_load_mode = (SdkSuperchunkLoadMode)mode;
                    save_graphics_settings_now();
                    break;
                }
                case SDK_GRAPHICS_MENU_ROW_VSYNC:
                    g_graphics_settings.vsync = !g_graphics_settings.vsync;
                    sdk_renderer_set_vsync(g_graphics_settings.vsync);
                    save_graphics_settings_now();
                    break;
                case SDK_GRAPHICS_MENU_ROW_FOG:
                    g_graphics_settings.fog_enabled = !g_graphics_settings.fog_enabled;
                    save_graphics_settings_now();
                    break;
                default:
                    break;
            }
        }
        if (backspace && !g_pause_menu_nav_was_down[5]) {
            g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
        }
    } else if (g_pause_menu_view == SDK_PAUSE_MENU_VIEW_KEY_BINDINGS) {
        int selected_row = 0;
        keybind_menu_clamp_selection();
        selected_row = api_clampi(g_keybind_menu_selected, 0, keybind_menu_row_count() - 1);

        if (g_keybind_capture_active) {
            if (selected_row >= 0 && selected_row < SDK_INPUT_ACTION_COUNT) {
                int captured_code = 0;

                if (sdk_input_raw_key_pressed(VK_BACK)) {
                    sdk_input_clear_binding(&g_input_settings, (SdkInputAction)selected_row);
                    save_input_settings_now();
                    g_keybind_capture_active = false;
                } else if (raw_r_pressed) {
                    sdk_input_restore_default_binding(&g_input_settings, (SdkInputAction)selected_row);
                    save_input_settings_now();
                    g_keybind_capture_active = false;
                } else if (raw_escape_pressed) {
                    g_keybind_capture_active = false;
                } else {
                    captured_code = sdk_input_capture_binding_code();
                    if (captured_code != 0) {
                        sdk_input_assign_binding(&g_input_settings, (SdkInputAction)selected_row, captured_code);
                        save_input_settings_now();
                        g_keybind_capture_active = false;
                    }
                }
            } else {
                g_keybind_capture_active = false;
            }
        } else {
            if (nav_up && !g_pause_menu_nav_was_down[0]) {
                g_keybind_menu_selected--;
            }
            if (nav_down && !g_pause_menu_nav_was_down[1]) {
                g_keybind_menu_selected++;
            }
            keybind_menu_clamp_selection();
            selected_row = api_clampi(g_keybind_menu_selected, 0, keybind_menu_row_count() - 1);

            if ((nav_left && !g_pause_menu_nav_was_down[2]) ||
                (nav_right && !g_pause_menu_nav_was_down[3])) {
                int dir = nav_right ? 1 : -1;
                if (selected_row == SDK_INPUT_ACTION_COUNT) {
                    g_input_settings.look_sensitivity_percent =
                        api_clampi(g_input_settings.look_sensitivity_percent + dir * 5, 25, 300);
                    save_input_settings_now();
                } else if (selected_row == SDK_INPUT_ACTION_COUNT + 1) {
                    g_input_settings.invert_y = !g_input_settings.invert_y;
                    save_input_settings_now();
                } else if (selected_row == SDK_INPUT_ACTION_COUNT + 2) {
                    sdk_input_restore_defaults(&g_input_settings);
                    save_input_settings_now();
                }
            }

            if (raw_r_pressed && selected_row >= 0 && selected_row < SDK_INPUT_ACTION_COUNT) {
                sdk_input_restore_default_binding(&g_input_settings, (SdkInputAction)selected_row);
                save_input_settings_now();
            }

            if (activate && !g_pause_menu_nav_was_down[4]) {
                if (selected_row >= 0 && selected_row < SDK_INPUT_ACTION_COUNT) {
                    g_keybind_capture_active = true;
                } else if (selected_row == SDK_INPUT_ACTION_COUNT + 1) {
                    g_input_settings.invert_y = !g_input_settings.invert_y;
                    save_input_settings_now();
                } else if (selected_row == SDK_INPUT_ACTION_COUNT + 2) {
                    sdk_input_restore_defaults(&g_input_settings);
                    save_input_settings_now();
                }
            }

            if (backspace && !g_pause_menu_nav_was_down[5]) {
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
            }
        }
    } else if (g_pause_menu_view == SDK_PAUSE_MENU_VIEW_CREATIVE) {
        while (sdk_window_pop_char(g_sdk.window, &ch)) {
            if (g_creative_shape_focus == SDK_CREATIVE_SHAPE_FOCUS_RESULTS &&
                ch >= 32u && ch < 127u && g_creative_menu_search_len + 1 < SDK_PAUSE_MENU_SEARCH_MAX) {
                char c = (char)ch;
                if (c == '_') c = ' ';
                g_creative_menu_search[g_creative_menu_search_len++] = c;
                g_creative_menu_search[g_creative_menu_search_len] = '\0';
            }
        }

        creative_clamp_shape_state();
        if (g_creative_shape_focus == SDK_CREATIVE_SHAPE_FOCUS_PANEL) {
            if (nav_up && !g_pause_menu_nav_was_down[0]) {
                g_creative_shape_row--;
            }
            if (nav_down && !g_pause_menu_nav_was_down[1]) {
                g_creative_shape_row++;
            }
            if ((nav_left && !g_pause_menu_nav_was_down[2]) ||
                (nav_right && !g_pause_menu_nav_was_down[3])) {
                int delta = nav_right ? 1 : -1;
                switch (g_creative_shape_row) {
                    case SDK_CREATIVE_SHAPE_ROW_WIDTH:
                        g_creative_shape_width = api_clampi(g_creative_shape_width + delta, 1, 16);
                        break;
                    case SDK_CREATIVE_SHAPE_ROW_HEIGHT:
                        g_creative_shape_height = api_clampi(g_creative_shape_height + delta, 1, 16);
                        break;
                    case SDK_CREATIVE_SHAPE_ROW_DEPTH:
                    default:
                        g_creative_shape_depth = api_clampi(g_creative_shape_depth + delta, 1, 16);
                        break;
                }
            }
            if (backspace && !g_pause_menu_nav_was_down[5]) {
                g_creative_shape_focus = SDK_CREATIVE_SHAPE_FOCUS_RESULTS;
            } else if (activate && !g_pause_menu_nav_was_down[4]) {
                creative_grant_selected_entry();
            }
        } else {
            if (nav_up && !g_pause_menu_nav_was_down[0]) {
                g_creative_menu_selected--;
            }
            if (nav_down && !g_pause_menu_nav_was_down[1]) {
                g_creative_menu_selected++;
            }
            if ((nav_left && !g_pause_menu_nav_was_down[2]) ||
                (nav_right && !g_pause_menu_nav_was_down[3])) {
                if (g_session_kind == SDK_SESSION_KIND_PROP_EDITOR) {
                    g_creative_menu_filter = SDK_CREATIVE_FILTER_BUILDING_BLOCKS;
                } else {
                    int delta = nav_right ? 1 : -1;
                    g_creative_menu_filter =
                        (g_creative_menu_filter + delta + 4) % 4;
                }
                g_creative_menu_selected = 0;
                g_creative_menu_scroll = 0;
            }
            if (backspace && !g_pause_menu_nav_was_down[5]) {
                if (g_creative_menu_search_len > 0) {
                    g_creative_menu_search[--g_creative_menu_search_len] = '\0';
                } else {
                    g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
                }
            } else if (activate && !g_pause_menu_nav_was_down[4]) {
                CreativeEntry selected = creative_entry_for_filtered_index(g_creative_menu_selected);
                if (selected.kind == SDK_CREATIVE_ENTRY_BLOCK && selected.id != BLOCK_AIR) {
                    g_creative_shape_focus = SDK_CREATIVE_SHAPE_FOCUS_PANEL;
                    g_creative_shape_row = SDK_CREATIVE_SHAPE_ROW_WIDTH;
                } else if (selected.kind == SDK_CREATIVE_ENTRY_ITEM && selected.id != ITEM_NONE) {
                    creative_grant_selected_entry();
                }
            }
        }

        if (g_session_kind == SDK_SESSION_KIND_PROP_EDITOR) {
            g_creative_menu_filter = SDK_CREATIVE_FILTER_BUILDING_BLOCKS;
        }
        creative_clamp_selection();
        creative_clamp_shape_state();
        if (g_creative_shape_focus == SDK_CREATIVE_SHAPE_FOCUS_PANEL) {
            CreativeEntry selected = creative_entry_for_filtered_index(g_creative_menu_selected);
            if (selected.kind != SDK_CREATIVE_ENTRY_BLOCK || selected.id == BLOCK_AIR) {
                g_creative_shape_focus = SDK_CREATIVE_SHAPE_FOCUS_RESULTS;
            } else {
                g_creative_shape_row = api_clampi(g_creative_shape_row, 0, SDK_CREATIVE_SHAPE_ROW_COUNT - 1);
            }
        }
    } else if (g_pause_menu_view == SDK_PAUSE_MENU_VIEW_SELECT_CHARACTER) {
        pause_character_clamp_selection();
        if (nav_up && !g_pause_menu_nav_was_down[0] && g_pause_character_selected > 0) {
            g_pause_character_selected--;
        }
        if (nav_down && !g_pause_menu_nav_was_down[1] && g_character_asset_count > 0 &&
            g_pause_character_selected < g_character_asset_count - 1) {
            g_pause_character_selected++;
        }
        pause_character_clamp_selection();
        if (activate && !g_pause_menu_nav_was_down[4] && g_character_asset_count > 0) {
            select_gameplay_character_index(g_pause_character_selected, true);
            g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
        }
        if (backspace && !g_pause_menu_nav_was_down[5]) {
            g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
        }
    } else if (g_pause_menu_view == SDK_PAUSE_MENU_VIEW_DEBUG_PROFILER) {
        if (activate && !g_pause_menu_nav_was_down[4]) {
            if (!g_profiler.enabled) {
                char log_dir[512];
                if (g_sdk.world_save_id && g_sdk.world_save_id[0]) {
                    snprintf(log_dir, sizeof(log_dir), "%s\\%s", SDK_WORLD_SAVE_ROOT, g_sdk.world_save_id);
                    if (sdk_profiler_enable(&g_profiler, log_dir)) {
                        profiler_log_current_graphics_snapshot("StartGraphics");
                        OutputDebugStringA("[Profiler] Enabled\n");
                    } else {
                        OutputDebugStringA("[Profiler] Failed to enable - could not create log file\n");
                    }
                } else {
                    OutputDebugStringA("[Profiler] Cannot enable - no active world session\n");
                }
            } else {
                profiler_log_current_graphics_snapshot("EndGraphics");
                sdk_profiler_disable(&g_profiler);
                OutputDebugStringA("[Profiler] Disabled\n");
            }
        }
        if (backspace && !g_pause_menu_nav_was_down[5]) {
            g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
        }
    } else if (g_pause_menu_view == SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER) {
        int chunk_count = (int)g_sdk.chunk_mgr.resident_count;
        int visible_rows = 18;
        
        if (nav_up && !g_pause_menu_nav_was_down[0] && g_chunk_manager_selected > 0) {
            g_chunk_manager_selected--;
            if (g_chunk_manager_selected < g_chunk_manager_scroll) {
                g_chunk_manager_scroll--;
            }
        }
        if (nav_down && !g_pause_menu_nav_was_down[1] && g_chunk_manager_selected < chunk_count - 1) {
            g_chunk_manager_selected++;
            if (g_chunk_manager_selected >= g_chunk_manager_scroll + visible_rows) {
                g_chunk_manager_scroll++;
            }
        }
        if (backspace && !g_pause_menu_nav_was_down[5]) {
            g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
        }
    }

    g_pause_menu_nav_was_down[0] = nav_up;
    g_pause_menu_nav_was_down[1] = nav_down;
    g_pause_menu_nav_was_down[2] = nav_left;
    g_pause_menu_nav_was_down[3] = nav_right;
    g_pause_menu_nav_was_down[4] = activate;
    g_pause_menu_nav_was_down[5] = backspace;
}

