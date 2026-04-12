#include "sdk_input.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDK_INPUT_SETTINGS_VERSION 1

typedef struct SdkInputActionSpec {
    SdkInputAction action;
    const char* key_name;
    const char* label;
    int default_binding;
} SdkInputActionSpec;

static const SdkInputActionSpec k_input_action_specs[SDK_INPUT_ACTION_COUNT] = {
    { SDK_INPUT_ACTION_MOVE_FORWARD,          "move_forward",          "MOVE FORWARD",          'W' },
    { SDK_INPUT_ACTION_MOVE_BACKWARD,         "move_backward",         "MOVE BACKWARD",         'S' },
    { SDK_INPUT_ACTION_MOVE_LEFT,             "move_left",             "MOVE LEFT",             'A' },
    { SDK_INPUT_ACTION_MOVE_RIGHT,            "move_right",            "MOVE RIGHT",            'D' },
    { SDK_INPUT_ACTION_LOOK_LEFT,             "look_left",             "LOOK LEFT",             VK_LEFT },
    { SDK_INPUT_ACTION_LOOK_RIGHT,            "look_right",            "LOOK RIGHT",            VK_RIGHT },
    { SDK_INPUT_ACTION_LOOK_UP,               "look_up",               "LOOK UP",               VK_UP },
    { SDK_INPUT_ACTION_LOOK_DOWN,             "look_down",             "LOOK DOWN",             VK_DOWN },
    { SDK_INPUT_ACTION_JUMP_TOGGLE_FLIGHT,    "jump_toggle_flight",    "JUMP / TOGGLE FLIGHT", VK_SPACE },
    { SDK_INPUT_ACTION_DESCEND,               "descend",               "DESCEND",               VK_CONTROL },
    { SDK_INPUT_ACTION_SPRINT,                "sprint",                "SPRINT",                VK_SHIFT },
    { SDK_INPUT_ACTION_BREAK_BLOCK,           "break_block",           "BREAK / ATTACK",        SDK_INPUT_MOUSE_LEFT },
    { SDK_INPUT_ACTION_PLACE_USE,             "place_use",             "PLACE / USE",           SDK_INPUT_MOUSE_RIGHT },
    { SDK_INPUT_ACTION_VEHICLE_USE,           "vehicle_use",           "VEHICLE USE",           'F' },
    { SDK_INPUT_ACTION_PAUSE_MENU,            "pause_menu",            "PAUSE MENU",            'P' },
    { SDK_INPUT_ACTION_OPEN_MAP,              "open_map",              "OPEN MAP",              'M' },
    { SDK_INPUT_ACTION_OPEN_SKILLS,           "open_skills",           "OPEN SKILLS",           VK_TAB },
    { SDK_INPUT_ACTION_OPEN_CRAFTING,         "open_crafting",         "OPEN CRAFTING",         'C' },
    { SDK_INPUT_ACTION_OPEN_COMMAND,          "open_command",          "OPEN COMMAND",          VK_OEM_2 },
    { SDK_INPUT_ACTION_HOTBAR_1,              "hotbar_1",              "HOTBAR 1",              '1' },
    { SDK_INPUT_ACTION_HOTBAR_2,              "hotbar_2",              "HOTBAR 2",              '2' },
    { SDK_INPUT_ACTION_HOTBAR_3,              "hotbar_3",              "HOTBAR 3",              '3' },
    { SDK_INPUT_ACTION_HOTBAR_4,              "hotbar_4",              "HOTBAR 4",              '4' },
    { SDK_INPUT_ACTION_HOTBAR_5,              "hotbar_5",              "HOTBAR 5",              '5' },
    { SDK_INPUT_ACTION_HOTBAR_6,              "hotbar_6",              "HOTBAR 6",              '6' },
    { SDK_INPUT_ACTION_HOTBAR_7,              "hotbar_7",              "HOTBAR 7",              '7' },
    { SDK_INPUT_ACTION_HOTBAR_8,              "hotbar_8",              "HOTBAR 8",              '8' },
    { SDK_INPUT_ACTION_HOTBAR_9,              "hotbar_9",              "HOTBAR 9",              '9' },
    { SDK_INPUT_ACTION_HOTBAR_10,             "hotbar_10",             "HOTBAR 10",             '0' },
    { SDK_INPUT_ACTION_MENU_UP,               "menu_up",               "MENU UP",               VK_UP },
    { SDK_INPUT_ACTION_MENU_DOWN,             "menu_down",             "MENU DOWN",             VK_DOWN },
    { SDK_INPUT_ACTION_MENU_LEFT,             "menu_left",             "MENU LEFT",             VK_LEFT },
    { SDK_INPUT_ACTION_MENU_RIGHT,            "menu_right",            "MENU RIGHT",            VK_RIGHT },
    { SDK_INPUT_ACTION_MENU_CONFIRM,          "menu_confirm",          "MENU CONFIRM",          VK_RETURN },
    { SDK_INPUT_ACTION_MENU_BACK,             "menu_back",             "MENU BACK",             VK_BACK },
    { SDK_INPUT_ACTION_MAP_PAN_UP,            "map_pan_up",            "MAP PAN UP",            'W' },
    { SDK_INPUT_ACTION_MAP_PAN_DOWN,          "map_pan_down",          "MAP PAN DOWN",          'S' },
    { SDK_INPUT_ACTION_MAP_PAN_LEFT,          "map_pan_left",          "MAP PAN LEFT",          'A' },
    { SDK_INPUT_ACTION_MAP_PAN_RIGHT,         "map_pan_right",         "MAP PAN RIGHT",         'D' },
    { SDK_INPUT_ACTION_MAP_ZOOM_OUT,          "map_zoom_out",          "MAP ZOOM OUT",          VK_LEFT },
    { SDK_INPUT_ACTION_MAP_ZOOM_IN,           "map_zoom_in",           "MAP ZOOM IN",           VK_RIGHT },
    { SDK_INPUT_ACTION_CONSTRUCTION_ROTATE,   "construction_rotate",   "CONSTRUCTION ROTATE",   'R' },
    { SDK_INPUT_ACTION_EDITOR_TOGGLE_PLAYBACK,"editor_toggle_playback","EDITOR PLAY / PAUSE",  VK_F9 },
    { SDK_INPUT_ACTION_EDITOR_PREV_FRAME,     "editor_prev_frame",     "EDITOR PREV FRAME",     VK_PRIOR },
    { SDK_INPUT_ACTION_EDITOR_NEXT_FRAME,     "editor_next_frame",     "EDITOR NEXT FRAME",     VK_NEXT }
};

static bool g_key_down[256];
static bool g_prev_key_down[256];
static bool g_mouse_down[3];
static bool g_prev_mouse_down[3];

static const SdkInputActionSpec* find_action_spec(SdkInputAction action)
{
    /* Finds action specification by action enum value */
    if (action < 0 || action >= SDK_INPUT_ACTION_COUNT) return NULL;
    return &k_input_action_specs[action];
}

static const SdkInputActionSpec* find_action_spec_by_key(const char* key_name)
{
    /* Finds action specification by its key name string */
    int i;

    if (!key_name) return NULL;
    for (i = 0; i < SDK_INPUT_ACTION_COUNT; ++i) {
        if (_stricmp(k_input_action_specs[i].key_name, key_name) == 0) {
            return &k_input_action_specs[i];
        }
    }
    return NULL;
}

static const char* skip_ws(const char* p)
{
    /* Skips whitespace characters in string */
    while (p && *p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static void trim_ascii(char* text)
{
    /* Trims leading and trailing whitespace from ASCII string */
    size_t len;
    size_t start = 0;

    if (!text) return;
    len = strlen(text);
    while (start < len && isspace((unsigned char)text[start])) {
        ++start;
    }
    while (len > start && isspace((unsigned char)text[len - 1])) {
        --len;
    }
    if (start > 0) {
        memmove(text, text + start, len - start);
    }
    text[len - start] = '\0';
}

static bool parse_bool_text(const char* value, bool* out_value)
{
    /* Parses boolean value from text (true/yes/1 vs false/no/0) */
    if (!value || !out_value) return false;
    if (_stricmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
        _stricmp(value, "yes") == 0 || _stricmp(value, "on") == 0) {
        *out_value = true;
        return true;
    }
    if (_stricmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
        _stricmp(value, "no") == 0 || _stricmp(value, "off") == 0) {
        *out_value = false;
        return true;
    }
    return false;
}

static bool input_code_down(int binding_code)
{
    /* Checks if a binding code (key or mouse) is currently held down */
    if (binding_code == VK_SHIFT || binding_code == VK_LSHIFT || binding_code == VK_RSHIFT) {
        return g_key_down[VK_SHIFT] || g_key_down[VK_LSHIFT] || g_key_down[VK_RSHIFT];
    }
    if (binding_code == VK_CONTROL || binding_code == VK_LCONTROL || binding_code == VK_RCONTROL) {
        return g_key_down[VK_CONTROL] || g_key_down[VK_LCONTROL] || g_key_down[VK_RCONTROL];
    }
    if (binding_code == VK_MENU || binding_code == VK_LMENU || binding_code == VK_RMENU) {
        return g_key_down[VK_MENU] || g_key_down[VK_LMENU] || g_key_down[VK_RMENU];
    }
    if (binding_code >= 0 && binding_code < 256) {
        return g_key_down[binding_code];
    }
    if (binding_code == SDK_INPUT_MOUSE_LEFT) return g_mouse_down[0];
    if (binding_code == SDK_INPUT_MOUSE_RIGHT) return g_mouse_down[1];
    if (binding_code == SDK_INPUT_MOUSE_MIDDLE) return g_mouse_down[2];
    return false;
}

static bool input_code_prev_down(int binding_code)
{
    /* Checks if a binding code was held down in previous frame */
    if (binding_code == VK_SHIFT || binding_code == VK_LSHIFT || binding_code == VK_RSHIFT) {
        return g_prev_key_down[VK_SHIFT] || g_prev_key_down[VK_LSHIFT] || g_prev_key_down[VK_RSHIFT];
    }
    if (binding_code == VK_CONTROL || binding_code == VK_LCONTROL || binding_code == VK_RCONTROL) {
        return g_prev_key_down[VK_CONTROL] || g_prev_key_down[VK_LCONTROL] || g_prev_key_down[VK_RCONTROL];
    }
    if (binding_code == VK_MENU || binding_code == VK_LMENU || binding_code == VK_RMENU) {
        return g_prev_key_down[VK_MENU] || g_prev_key_down[VK_LMENU] || g_prev_key_down[VK_RMENU];
    }
    if (binding_code >= 0 && binding_code < 256) {
        return g_prev_key_down[binding_code];
    }
    if (binding_code == SDK_INPUT_MOUSE_LEFT) return g_prev_mouse_down[0];
    if (binding_code == SDK_INPUT_MOUSE_RIGHT) return g_prev_mouse_down[1];
    if (binding_code == SDK_INPUT_MOUSE_MIDDLE) return g_prev_mouse_down[2];
    return false;
}

void sdk_input_settings_default(SdkInputSettings* out_settings)
{
    /* Initializes input settings with default key bindings and sensitivity */
    int i;

    if (!out_settings) return;
    memset(out_settings, 0, sizeof(*out_settings));
    out_settings->version = SDK_INPUT_SETTINGS_VERSION;
    out_settings->look_sensitivity_percent = 100;
    out_settings->invert_y = false;
    for (i = 0; i < SDK_INPUT_ACTION_COUNT; ++i) {
        out_settings->binding_code[i] = k_input_action_specs[i].default_binding;
    }
}

void sdk_input_restore_defaults(SdkInputSettings* settings)
{
    /* Restores all input settings to their default values */
    int i;

    if (!settings) return;
    settings->look_sensitivity_percent = 100;
    settings->invert_y = false;
    for (i = 0; i < SDK_INPUT_ACTION_COUNT; ++i) {
        settings->binding_code[i] = k_input_action_specs[i].default_binding;
    }
}

void sdk_input_restore_default_binding(SdkInputSettings* settings, SdkInputAction action)
{
    /* Restores a single action binding to its default value */
    const SdkInputActionSpec* spec;

    if (!settings) return;
    spec = find_action_spec(action);
    if (!spec) return;
    sdk_input_assign_binding(settings, action, spec->default_binding);
}

void sdk_input_assign_binding(SdkInputSettings* settings, SdkInputAction action, int binding_code)
{
    /* Assigns a binding code to an action, clearing conflicts */
    int i;

    if (!settings || action < 0 || action >= SDK_INPUT_ACTION_COUNT) return;
    if (binding_code < 0) binding_code = 0;
    for (i = 0; i < SDK_INPUT_ACTION_COUNT; ++i) {
        if (i == (int)action) continue;
        if (binding_code != 0 && settings->binding_code[i] == binding_code) {
            settings->binding_code[i] = 0;
        }
    }
    settings->binding_code[action] = binding_code;
}

void sdk_input_clear_binding(SdkInputSettings* settings, SdkInputAction action)
{
    /* Clears (unbinds) an action's binding */
    if (!settings || action < 0 || action >= SDK_INPUT_ACTION_COUNT) return;
    settings->binding_code[action] = 0;
}

int sdk_input_settings_load(SdkInputSettings* out_settings)
{
    /* Loads input settings from file, applying defaults for missing values */
    FILE* file;
    char line[256];

    if (!out_settings) return 0;
    sdk_input_settings_default(out_settings);

    file = fopen(SDK_INPUT_SETTINGS_PATH, "rb");
    if (!file) return 0;

    while (fgets(line, sizeof(line), file)) {
        char* equals = strchr(line, '=');
        char* key;
        char* value;
        int parsed_int;
        bool parsed_bool;
        const SdkInputActionSpec* spec;

        if (!equals) continue;
        *equals = '\0';
        key = line;
        value = equals + 1;
        trim_ascii(key);
        trim_ascii(value);
        if (key[0] == '\0') continue;

        if (_stricmp(key, "version") == 0) {
            out_settings->version = atoi(value);
            continue;
        }
        if (_stricmp(key, "look_sensitivity_percent") == 0) {
            parsed_int = atoi(value);
            if (parsed_int < 25) parsed_int = 25;
            if (parsed_int > 300) parsed_int = 300;
            out_settings->look_sensitivity_percent = parsed_int;
            continue;
        }
        if (_stricmp(key, "invert_y") == 0 && parse_bool_text(value, &parsed_bool)) {
            out_settings->invert_y = parsed_bool;
            continue;
        }

        spec = find_action_spec_by_key(key);
        if (spec) {
            parsed_int = atoi(value);
            if (parsed_int < 0) parsed_int = 0;
            out_settings->binding_code[spec->action] = parsed_int;
        }
    }

    fclose(file);
    if (out_settings->look_sensitivity_percent < 25) out_settings->look_sensitivity_percent = 25;
    if (out_settings->look_sensitivity_percent > 300) out_settings->look_sensitivity_percent = 300;
    return 1;
}

void sdk_input_settings_save(const SdkInputSettings* settings)
{
    /* Saves input settings to file in key=value format */
    FILE* file;
    SdkInputSettings local;
    int i;

    if (settings) {
        local = *settings;
    } else {
        sdk_input_settings_default(&local);
    }

    if (local.look_sensitivity_percent < 25) local.look_sensitivity_percent = 25;
    if (local.look_sensitivity_percent > 300) local.look_sensitivity_percent = 300;

    file = fopen(SDK_INPUT_SETTINGS_PATH, "wb");
    if (!file) return;

    fprintf(file, "version=%d\n", SDK_INPUT_SETTINGS_VERSION);
    fprintf(file, "look_sensitivity_percent=%d\n", local.look_sensitivity_percent);
    fprintf(file, "invert_y=%s\n", local.invert_y ? "true" : "false");
    for (i = 0; i < SDK_INPUT_ACTION_COUNT; ++i) {
        fprintf(file, "%s=%d\n",
                k_input_action_specs[i].key_name,
                local.binding_code[i]);
    }

    fclose(file);
}

void sdk_input_frame_begin(SdkWindow* win)
{
    /* Updates key/mouse state at start of frame, capturing current input */
    int i;

    for (i = 0; i < 256; ++i) {
        g_prev_key_down[i] = g_key_down[i];
        g_key_down[i] = win ? sdk_window_is_key_down(win, (uint8_t)i) : false;
    }
    for (i = 0; i < 3; ++i) {
        g_prev_mouse_down[i] = g_mouse_down[i];
        g_mouse_down[i] = win ? sdk_window_is_mouse_down(win, i) : false;
    }
}

bool sdk_input_action_down(const SdkInputSettings* settings, SdkInputAction action)
{
    /* Returns true if action's bound key/mouse button is currently held down */
    if (!settings || action < 0 || action >= SDK_INPUT_ACTION_COUNT) return false;
    return input_code_down(settings->binding_code[action]);
}

bool sdk_input_action_pressed(const SdkInputSettings* settings, SdkInputAction action)
{
    /* Returns true if action was just pressed this frame (transition) */
    int binding_code;

    if (!settings || action < 0 || action >= SDK_INPUT_ACTION_COUNT) return false;
    binding_code = settings->binding_code[action];
    return input_code_down(binding_code) && !input_code_prev_down(binding_code);
}

bool sdk_input_action_released(const SdkInputSettings* settings, SdkInputAction action)
{
    /* Returns true if action was just released this frame (transition) */
    int binding_code;

    if (!settings || action < 0 || action >= SDK_INPUT_ACTION_COUNT) return false;
    binding_code = settings->binding_code[action];
    return !input_code_down(binding_code) && input_code_prev_down(binding_code);
}

bool sdk_input_raw_key_down(uint8_t vk_code)
{
    /* Returns true if virtual key is currently held down (raw, no binding) */
    return g_key_down[vk_code];
}

bool sdk_input_raw_key_pressed(uint8_t vk_code)
{
    /* Returns true if virtual key was just pressed this frame */
    return g_key_down[vk_code] && !g_prev_key_down[vk_code];
}

bool sdk_input_raw_key_released(uint8_t vk_code)
{
    /* Returns true if virtual key was just released this frame */
    return !g_key_down[vk_code] && g_prev_key_down[vk_code];
}

bool sdk_input_raw_mouse_down(int button)
{
    /* Returns true if mouse button is currently held down (raw) */
    if (button < 0 || button > 2) return false;
    return g_mouse_down[button];
}

bool sdk_input_raw_mouse_pressed(int button)
{
    /* Returns true if mouse button was just pressed this frame */
    if (button < 0 || button > 2) return false;
    return g_mouse_down[button] && !g_prev_mouse_down[button];
}

int sdk_input_capture_binding_code(void)
{
    /* Captures the next key or mouse press for binding assignment */
    int vk;

    if (sdk_input_raw_mouse_pressed(0)) return SDK_INPUT_MOUSE_LEFT;
    if (sdk_input_raw_mouse_pressed(1)) return SDK_INPUT_MOUSE_RIGHT;
    if (sdk_input_raw_mouse_pressed(2)) return SDK_INPUT_MOUSE_MIDDLE;

    for (vk = 1; vk < 256; ++vk) {
        if (sdk_input_raw_key_pressed((uint8_t)vk)) {
            return vk;
        }
    }
    return 0;
}

const char* sdk_input_action_label(SdkInputAction action)
{
    /* Returns human-readable label for an input action */
    const SdkInputActionSpec* spec = find_action_spec(action);
    return spec ? spec->label : "";
}

void sdk_input_binding_name(int binding_code, char* out_text, size_t out_text_size)
{
    /* Converts binding code to human-readable name (e.g., "W", "SPACE", "MOUSE 1") */
    if (!out_text || out_text_size == 0u) return;
    out_text[0] = '\0';

    if (binding_code == 0) {
        strcpy_s(out_text, out_text_size, "UNBOUND");
        return;
    }
    if (binding_code >= 'A' && binding_code <= 'Z') {
        snprintf(out_text, out_text_size, "%c", (char)binding_code);
        return;
    }
    if (binding_code >= '0' && binding_code <= '9') {
        snprintf(out_text, out_text_size, "%c", (char)binding_code);
        return;
    }
    if (binding_code == SDK_INPUT_MOUSE_LEFT) {
        strcpy_s(out_text, out_text_size, "MOUSE 1");
        return;
    }
    if (binding_code == SDK_INPUT_MOUSE_RIGHT) {
        strcpy_s(out_text, out_text_size, "MOUSE 2");
        return;
    }
    if (binding_code == SDK_INPUT_MOUSE_MIDDLE) {
        strcpy_s(out_text, out_text_size, "MOUSE 3");
        return;
    }

    switch (binding_code) {
        case VK_UP: strcpy_s(out_text, out_text_size, "UP"); break;
        case VK_DOWN: strcpy_s(out_text, out_text_size, "DOWN"); break;
        case VK_LEFT: strcpy_s(out_text, out_text_size, "LEFT"); break;
        case VK_RIGHT: strcpy_s(out_text, out_text_size, "RIGHT"); break;
        case VK_SPACE: strcpy_s(out_text, out_text_size, "SPACE"); break;
        case VK_RETURN: strcpy_s(out_text, out_text_size, "ENTER"); break;
        case VK_BACK: strcpy_s(out_text, out_text_size, "BACKSPACE"); break;
        case VK_ESCAPE: strcpy_s(out_text, out_text_size, "ESC"); break;
        case VK_SHIFT: strcpy_s(out_text, out_text_size, "SHIFT"); break;
        case VK_CONTROL: strcpy_s(out_text, out_text_size, "CTRL"); break;
        case VK_MENU: strcpy_s(out_text, out_text_size, "ALT"); break;
        case VK_TAB: strcpy_s(out_text, out_text_size, "TAB"); break;
        case VK_PRIOR: strcpy_s(out_text, out_text_size, "PGUP"); break;
        case VK_NEXT: strcpy_s(out_text, out_text_size, "PGDN"); break;
        case VK_DIVIDE: strcpy_s(out_text, out_text_size, "NUM /"); break;
        case VK_OEM_2: strcpy_s(out_text, out_text_size, "/"); break;
        default:
            if (binding_code >= VK_F1 && binding_code <= VK_F12) {
                snprintf(out_text, out_text_size, "F%d", binding_code - VK_F1 + 1);
            } else {
                snprintf(out_text, out_text_size, "VK %d", binding_code);
            }
            break;
    }
}
