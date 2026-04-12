#include "sdk_automation.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SdkAutomationFrameOverride g_sdk_automation_frame_override;

static char* sdk_automation_ltrim(char* text)
{
    /* Strips leading whitespace from text in place */
    while (text && *text && isspace((unsigned char)*text)) {
        ++text;
    }
    return text;
}

static void sdk_automation_rtrim(char* text)
{
    /* Strips trailing whitespace from text in place */
    size_t len;

    if (!text) return;
    len = strlen(text);
    while (len > 0u && isspace((unsigned char)text[len - 1u])) {
        text[--len] = '\0';
    }
}

static int sdk_automation_parse_ready_target(const char* text, int* out_target)
{
    /* Parses ready target string (resident/gpu) to enum value */
    if (!text || !out_target) return 0;
    if (_stricmp(text, "resident") == 0) {
        *out_target = SDK_AUTOMATION_READY_RESIDENT;
        return 1;
    }
    if (_stricmp(text, "gpu") == 0 || _stricmp(text, "gpu_ready") == 0) {
        *out_target = SDK_AUTOMATION_READY_GPU;
        return 1;
    }
    return 0;
}

void sdk_automation_script_init(SdkAutomationScript* script, const char* name)
{
    /* Initializes automation script with optional name */
    if (!script) return;
    memset(script, 0, sizeof(*script));
    if (name && name[0]) {
        strncpy_s(script->name, sizeof(script->name), name, _TRUNCATE);
    }
}

void sdk_automation_script_free(SdkAutomationScript* script)
{
    /* Frees automation script actions and clears state */
    if (!script) return;
    free(script->actions);
    memset(script, 0, sizeof(*script));
}

int sdk_automation_script_append(SdkAutomationScript* script, const SdkAutomationAction* action)
{
    /* Appends action to script, growing buffer if needed */
    SdkAutomationAction* new_actions;
    int new_capacity;

    if (!script || !action) return 0;
    if (script->action_count >= script->action_capacity) {
        new_capacity = script->action_capacity > 0 ? script->action_capacity * 2 : 16;
        new_actions = (SdkAutomationAction*)realloc(script->actions,
                                                    (size_t)new_capacity * sizeof(SdkAutomationAction));
        if (!new_actions) return 0;
        script->actions = new_actions;
        script->action_capacity = new_capacity;
    }
    script->actions[script->action_count++] = *action;
    return 1;
}

int sdk_automation_script_load_file(const char* script_path, SdkAutomationScript* out_script)
{
    /* Loads automation script from text file, parses commands */
    FILE* file = NULL;
    char line[512];
    SdkAutomationScript local_script;
    int line_number = 0;

    if (!script_path || !out_script) return 0;
    file = fopen(script_path, "rb");
    if (!file) return 0;

    sdk_automation_script_init(&local_script, script_path);
    while (fgets(line, sizeof(line), file)) {
        char* cursor;
        char* command;
        SdkAutomationAction action;

        ++line_number;
        sdk_automation_rtrim(line);
        cursor = sdk_automation_ltrim(line);
        if (!cursor || !cursor[0] || cursor[0] == '#') continue;

        memset(&action, 0, sizeof(action));
        command = strtok(cursor, " \t");
        if (!command) continue;

        if (_stricmp(command, "wait_frames") == 0) {
            char* frames_text = strtok(NULL, " \t");
            if (!frames_text) goto parse_fail;
            action.type = SDK_AUTOMATION_ACTION_WAIT_FRAMES;
            action.frames = atoi(frames_text);
        } else if (_stricmp(command, "wait_until_world_ready") == 0) {
            char* target_text = strtok(NULL, " \t");
            action.type = SDK_AUTOMATION_ACTION_WAIT_UNTIL_WORLD_READY;
            if (!sdk_automation_parse_ready_target(target_text, &action.ready_target)) goto parse_fail;
        } else if (_stricmp(command, "look") == 0) {
            char* yaw_text = strtok(NULL, " \t");
            char* pitch_text = strtok(NULL, " \t");
            if (!yaw_text || !pitch_text) goto parse_fail;
            action.type = SDK_AUTOMATION_ACTION_LOOK;
            action.yaw_delta = (float)atof(yaw_text);
            action.pitch_delta = (float)atof(pitch_text);
        } else if (_stricmp(command, "move") == 0) {
            char* forward_text = strtok(NULL, " \t");
            char* right_text = strtok(NULL, " \t");
            char* up_text = strtok(NULL, " \t");
            char* frames_text = strtok(NULL, " \t");
            if (!forward_text || !right_text || !up_text || !frames_text) goto parse_fail;
            action.type = SDK_AUTOMATION_ACTION_MOVE;
            action.move_forward = (float)atof(forward_text);
            action.move_right = (float)atof(right_text);
            action.move_up = (float)atof(up_text);
            action.frames = atoi(frames_text);
        } else if (_stricmp(command, "toggle_flight") == 0) {
            action.type = SDK_AUTOMATION_ACTION_TOGGLE_FLIGHT;
        } else if (_stricmp(command, "teleport") == 0) {
            char* x_text = strtok(NULL, " \t");
            char* y_text = strtok(NULL, " \t");
            char* z_text = strtok(NULL, " \t");
            if (!x_text || !y_text || !z_text) goto parse_fail;
            action.type = SDK_AUTOMATION_ACTION_TELEPORT;
            action.x = (float)atof(x_text);
            action.y = (float)atof(y_text);
            action.z = (float)atof(z_text);
        } else if (_stricmp(command, "capture_screenshot") == 0) {
            char* path_text = strtok(NULL, "");
            action.type = SDK_AUTOMATION_ACTION_CAPTURE_SCREENSHOT;
            if (!path_text) goto parse_fail;
            path_text = sdk_automation_ltrim(path_text);
            strncpy_s(action.text, sizeof(action.text), path_text, _TRUNCATE);
        } else if (_stricmp(command, "exit_session") == 0) {
            action.type = SDK_AUTOMATION_ACTION_EXIT_SESSION;
        } else {
            goto parse_fail;
        }

        if (!sdk_automation_script_append(&local_script, &action)) {
            sdk_automation_script_free(&local_script);
            fclose(file);
            return 0;
        }
        continue;

    parse_fail:
        fprintf(stderr, "Automation script parse error at line %d: %s\n", line_number, line);
        sdk_automation_script_free(&local_script);
        fclose(file);
        return 0;
    }

    fclose(file);
    *out_script = local_script;
    return 1;
}

void sdk_automation_clear_frame_override(void)
{
    /* Clears frame override state */
    memset(&g_sdk_automation_frame_override, 0, sizeof(g_sdk_automation_frame_override));
}

void sdk_automation_set_frame_override(const SdkAutomationFrameOverride* override_state)
{
    /* Sets frame override state from given pointer */
    if (!override_state) {
        sdk_automation_clear_frame_override();
        return;
    }
    g_sdk_automation_frame_override = *override_state;
}

const SdkAutomationFrameOverride* sdk_automation_frame_override(void)
{
    /* Returns pointer to current frame override state */
    return &g_sdk_automation_frame_override;
}
