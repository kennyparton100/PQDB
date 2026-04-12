#ifndef SDK_AUTOMATION_H
#define SDK_AUTOMATION_H

#include "../sdk_types.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_AUTOMATION_MAX_NAME 128
#define SDK_AUTOMATION_PROMPT_MAX 128

typedef enum {
    SDK_AUTOMATION_ACTION_WAIT_FRAMES = 0,
    SDK_AUTOMATION_ACTION_WAIT_UNTIL_WORLD_READY,
    SDK_AUTOMATION_ACTION_LOOK,
    SDK_AUTOMATION_ACTION_MOVE,
    SDK_AUTOMATION_ACTION_TOGGLE_FLIGHT,
    SDK_AUTOMATION_ACTION_TELEPORT,
    SDK_AUTOMATION_ACTION_CAPTURE_SCREENSHOT,
    SDK_AUTOMATION_ACTION_EXIT_SESSION
} SdkAutomationActionType;

typedef enum {
    SDK_AUTOMATION_READY_RESIDENT = 0,
    SDK_AUTOMATION_READY_GPU = 1
} SdkAutomationReadinessTarget;

typedef struct {
    int   type;
    int   frames;
    int   ready_target;
    int   use_target;
    float yaw_delta;
    float pitch_delta;
    float move_forward;
    float move_right;
    float move_up;
    float x;
    float y;
    float z;
    char  text[MAX_PATH];
} SdkAutomationAction;

typedef struct {
    char name[SDK_AUTOMATION_MAX_NAME];
    SdkAutomationAction* actions;
    int action_count;
    int action_capacity;
} SdkAutomationScript;

typedef struct {
    int completed;
    int success;
    int width;
    int height;
    char path[MAX_PATH];
    char failure_reason[128];
} SdkScreenshotResult;

typedef struct {
    bool active;
    float yaw_delta;
    float pitch_delta;
    float move_forward;
    float move_right;
    float move_up;
    bool toggle_flight;
    bool teleport_pending;
    float teleport_x;
    float teleport_y;
    float teleport_z;
} SdkAutomationFrameOverride;

typedef struct {
    int success;
    char world_id[64];
    char scenario[SDK_AUTOMATION_MAX_NAME];
    char script_name[SDK_AUTOMATION_MAX_NAME];
    char run_dir[MAX_PATH];
    char progress_path[MAX_PATH];
    char timing_path[MAX_PATH];
    char progress_stage[64];
    char failure_stage[64];
    int wall_analysis_pass;
    int wall_expected;
    int wall_correct;
    int wall_missing;
    int wall_unexpected;
    int wall_problematic;
    SdkScreenshotResult screenshot;
    uint64_t world_create_ms;
    uint64_t bootstrap_resident_ms;
    uint64_t bootstrap_gpu_ms;
    uint64_t persistence_save_ms;
    uint64_t wall_analysis_ms;
    uint64_t live_runtime_init_ms;
    uint64_t session_start_ms;
    uint64_t screenshot_capture_ms;
    uint64_t shutdown_ms;
    uint64_t total_elapsed_ms;
    char prompt[SDK_AUTOMATION_PROMPT_MAX];
    char failure_reason[256];
} SdkAutomationRunResult;

void sdk_automation_script_init(SdkAutomationScript* script, const char* name);
void sdk_automation_script_free(SdkAutomationScript* script);
int sdk_automation_script_append(SdkAutomationScript* script, const SdkAutomationAction* action);
int sdk_automation_script_load_file(const char* script_path, SdkAutomationScript* out_script);

void sdk_automation_clear_frame_override(void);
void sdk_automation_set_frame_override(const SdkAutomationFrameOverride* override_state);
const SdkAutomationFrameOverride* sdk_automation_frame_override(void);

#ifdef __cplusplus
}
#endif

#endif
