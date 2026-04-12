#include "../Runtime/sdk_runtime_internal.h"

void map_handle_input(float player_world_x, float player_world_z)
{
    /* Handles map pan and zoom input from keyboard actions */
    bool w_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MAP_PAN_UP) ||
                  sdk_input_raw_key_down((uint8_t)'W');
    bool a_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MAP_PAN_LEFT) ||
                  sdk_input_raw_key_down((uint8_t)'A');
    bool s_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MAP_PAN_DOWN) ||
                  sdk_input_raw_key_down((uint8_t)'S');
    bool d_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MAP_PAN_RIGHT) ||
                  sdk_input_raw_key_down((uint8_t)'D');
    bool left_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MAP_ZOOM_OUT) ||
                     sdk_input_raw_key_down(VK_LEFT);
    bool right_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MAP_ZOOM_IN) ||
                      sdk_input_raw_key_down(VK_RIGHT);
    float visible_span_blocks;
    float pan_speed_blocks;

    if (!g_map_focus_initialized) {
        g_map_focus_world_x = player_world_x;
        g_map_focus_world_z = player_world_z;
        g_map_focus_initialized = true;
    }

    if (left_down) {
        int zoom_step = 1;
        g_map_zoom_left_hold_frames++;
        if (g_map_zoom_left_hold_frames > 50) zoom_step = 25;
        else if (g_map_zoom_left_hold_frames > 20) zoom_step = 5;
        if (g_map_zoom_tenths > 1 &&
            (g_map_zoom_left_hold_frames == 1 ||
             (g_map_zoom_left_hold_frames > 10 &&
              ((g_map_zoom_left_hold_frames - 10) % 2) == 0))) {
            g_map_zoom_tenths -= zoom_step;
            if (g_map_zoom_tenths < 1) g_map_zoom_tenths = 1;
        }
    } else {
        g_map_zoom_left_hold_frames = 0;
    }
    if (right_down) {
        int zoom_step = 1;
        g_map_zoom_right_hold_frames++;
        if (g_map_zoom_right_hold_frames > 50) zoom_step = 25;
        else if (g_map_zoom_right_hold_frames > 20) zoom_step = 5;
        if (g_map_zoom_tenths < HUD_MAP_MAX_ZOOM_TENTHS &&
            (g_map_zoom_right_hold_frames == 1 ||
             (g_map_zoom_right_hold_frames > 10 &&
              ((g_map_zoom_right_hold_frames - 10) % 2) == 0))) {
            g_map_zoom_tenths += zoom_step;
            if (g_map_zoom_tenths > HUD_MAP_MAX_ZOOM_TENTHS) {
                g_map_zoom_tenths = HUD_MAP_MAX_ZOOM_TENTHS;
            }
        }
    } else {
        g_map_zoom_right_hold_frames = 0;
    }
    g_map_zoom_left_was_down = left_down;
    g_map_zoom_right_was_down = right_down;

    visible_span_blocks = (float)SUPERCHUNK_BLOCKS * ((float)g_map_zoom_tenths / 10.0f);
    if (visible_span_blocks < (float)HUD_MAP_CELL_BLOCKS) visible_span_blocks = (float)HUD_MAP_CELL_BLOCKS;
    pan_speed_blocks = visible_span_blocks * 0.03f;
    if (pan_speed_blocks < 16.0f) pan_speed_blocks = 16.0f;

    if (w_down) g_map_focus_world_z -= pan_speed_blocks;
    if (s_down) g_map_focus_world_z += pan_speed_blocks;
    if (a_down) g_map_focus_world_x -= pan_speed_blocks;
    if (d_down) g_map_focus_world_x += pan_speed_blocks;
}

