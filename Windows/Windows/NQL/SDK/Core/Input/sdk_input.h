#ifndef NQLSDK_INPUT_H
#define NQLSDK_INPUT_H

#include "../../Platform/Win32/sdk_window.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_INPUT_SETTINGS_PATH "controls.cfg"
#define SDK_INPUT_MOUSE_LEFT 1000
#define SDK_INPUT_MOUSE_RIGHT 1001
#define SDK_INPUT_MOUSE_MIDDLE 1002

typedef enum SdkInputAction {
    SDK_INPUT_ACTION_MOVE_FORWARD = 0,
    SDK_INPUT_ACTION_MOVE_BACKWARD,
    SDK_INPUT_ACTION_MOVE_LEFT,
    SDK_INPUT_ACTION_MOVE_RIGHT,
    SDK_INPUT_ACTION_LOOK_LEFT,
    SDK_INPUT_ACTION_LOOK_RIGHT,
    SDK_INPUT_ACTION_LOOK_UP,
    SDK_INPUT_ACTION_LOOK_DOWN,
    SDK_INPUT_ACTION_JUMP_TOGGLE_FLIGHT,
    SDK_INPUT_ACTION_DESCEND,
    SDK_INPUT_ACTION_SPRINT,
    SDK_INPUT_ACTION_BREAK_BLOCK,
    SDK_INPUT_ACTION_PLACE_USE,
    SDK_INPUT_ACTION_VEHICLE_USE,
    SDK_INPUT_ACTION_PAUSE_MENU,
    SDK_INPUT_ACTION_OPEN_MAP,
    SDK_INPUT_ACTION_OPEN_SKILLS,
    SDK_INPUT_ACTION_OPEN_CRAFTING,
    SDK_INPUT_ACTION_OPEN_COMMAND,
    SDK_INPUT_ACTION_HOTBAR_1,
    SDK_INPUT_ACTION_HOTBAR_2,
    SDK_INPUT_ACTION_HOTBAR_3,
    SDK_INPUT_ACTION_HOTBAR_4,
    SDK_INPUT_ACTION_HOTBAR_5,
    SDK_INPUT_ACTION_HOTBAR_6,
    SDK_INPUT_ACTION_HOTBAR_7,
    SDK_INPUT_ACTION_HOTBAR_8,
    SDK_INPUT_ACTION_HOTBAR_9,
    SDK_INPUT_ACTION_HOTBAR_10,
    SDK_INPUT_ACTION_MENU_UP,
    SDK_INPUT_ACTION_MENU_DOWN,
    SDK_INPUT_ACTION_MENU_LEFT,
    SDK_INPUT_ACTION_MENU_RIGHT,
    SDK_INPUT_ACTION_MENU_CONFIRM,
    SDK_INPUT_ACTION_MENU_BACK,
    SDK_INPUT_ACTION_MAP_PAN_UP,
    SDK_INPUT_ACTION_MAP_PAN_DOWN,
    SDK_INPUT_ACTION_MAP_PAN_LEFT,
    SDK_INPUT_ACTION_MAP_PAN_RIGHT,
    SDK_INPUT_ACTION_MAP_ZOOM_OUT,
    SDK_INPUT_ACTION_MAP_ZOOM_IN,
    SDK_INPUT_ACTION_CONSTRUCTION_ROTATE,
    SDK_INPUT_ACTION_EDITOR_TOGGLE_PLAYBACK,
    SDK_INPUT_ACTION_EDITOR_PREV_FRAME,
    SDK_INPUT_ACTION_EDITOR_NEXT_FRAME,
    SDK_INPUT_ACTION_COUNT
} SdkInputAction;

typedef struct SdkInputSettings {
    int version;
    int look_sensitivity_percent;
    bool invert_y;
    int binding_code[SDK_INPUT_ACTION_COUNT];
} SdkInputSettings;

void sdk_input_settings_default(SdkInputSettings* out_settings);
int sdk_input_settings_load(SdkInputSettings* out_settings);
void sdk_input_settings_save(const SdkInputSettings* settings);

void sdk_input_frame_begin(SdkWindow* win);
bool sdk_input_action_down(const SdkInputSettings* settings, SdkInputAction action);
bool sdk_input_action_pressed(const SdkInputSettings* settings, SdkInputAction action);
bool sdk_input_action_released(const SdkInputSettings* settings, SdkInputAction action);

bool sdk_input_raw_key_down(uint8_t vk_code);
bool sdk_input_raw_key_pressed(uint8_t vk_code);
bool sdk_input_raw_key_released(uint8_t vk_code);
bool sdk_input_raw_mouse_down(int button);
bool sdk_input_raw_mouse_pressed(int button);

int sdk_input_capture_binding_code(void);
const char* sdk_input_action_label(SdkInputAction action);
void sdk_input_binding_name(int binding_code, char* out_text, size_t out_text_size);
void sdk_input_assign_binding(SdkInputSettings* settings, SdkInputAction action, int binding_code);
void sdk_input_clear_binding(SdkInputSettings* settings, SdkInputAction action);
void sdk_input_restore_default_binding(SdkInputSettings* settings, SdkInputAction action);
void sdk_input_restore_defaults(SdkInputSettings* settings);

#ifdef __cplusplus
}
#endif

#endif
