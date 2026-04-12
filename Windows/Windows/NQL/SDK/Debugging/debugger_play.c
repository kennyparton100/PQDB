#include "debugger_cli.h"

#include "../Core/API/Internal/sdk_api_internal.h"
#include "../Core/API/Session/Headless/sdk_session_headless.h"
#include "../Core/Automation/sdk_automation.h"
#include "../Core/Frontend/sdk_frontend_internal.h"
#include "../Core/Runtime/sdk_runtime_host.h"
#include "../Core/World/Chunks/ChunkAnalysis/chunk_analysis.h"
#include "../Core/World/Persistence/sdk_world_tooling.h"
#include "../Core/World/Worldgen/sdk_worldgen.h"
#include "../Core/World/Worldgen/SharedCache/sdk_worldgen_shared_cache.h"
#include "../Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUGGER_DEFAULT_WALL_THRESHOLD 1u

typedef struct {
    SdkWorldTarget target;
    SdkWorldSaveMeta meta;
    SdkAutomationScript script;
    SdkAutomationRunResult result;
    int action_index;
    int action_frames_remaining;
    int screenshot_timeout_remaining;
    int session_started;
    int failed;
    int completed;
    int screenshot_requested;
    int require_screenshot;
    int session_start_requested;
    uint64_t run_started_ms;
    uint64_t live_runtime_started_ms;
    uint64_t session_start_begin_ms;
    uint64_t session_start_deadline_ms;
    uint64_t shutdown_started_ms;
    uint64_t screenshot_requested_ms;
    char manifest_path[MAX_PATH];
} DebuggerPlayRunner;

static uint64_t debugger_play_now_ms(void)
{
    return GetTickCount64();
}

static void debugger_play_print_json_string(const char* text)
{
    const char* cursor = text ? text : "";

    putchar('"');
    while (*cursor) {
        char ch = *cursor++;
        if (ch == '\\') fputs("\\\\", stdout);
        else if (ch == '"') fputs("\\\"", stdout);
        else if (ch == '\n') fputs("\\n", stdout);
        else if (ch == '\r') fputs("\\r", stdout);
        else putchar(ch);
    }
    putchar('"');
}

static void debugger_play_fprint_json_string(FILE* file, const char* text)
{
    const char* cursor = text ? text : "";

    if (!file) return;
    fputc('"', file);
    while (*cursor) {
        char ch = *cursor++;
        if (ch == '\\') fputs("\\\\", file);
        else if (ch == '"') fputs("\\\"", file);
        else if (ch == '\n') fputs("\\n", file);
        else if (ch == '\r') fputs("\\r", file);
        else fputc(ch, file);
    }
    fputc('"', file);
}

static int debugger_play_write_progress(const DebuggerPlayRunner* runner)
{
    FILE* file;

    if (!runner || !runner->result.progress_path[0]) return 0;
    file = fopen(runner->result.progress_path, "wb");
    if (!file) return 0;
    fprintf(file, "{\n");
    fprintf(file, "  \"world_id\": ");
    debugger_play_fprint_json_string(file, runner->result.world_id);
    fprintf(file, ",\n  \"scenario\": ");
    debugger_play_fprint_json_string(file, runner->result.scenario);
    fprintf(file, ",\n  \"script_name\": ");
    debugger_play_fprint_json_string(file, runner->result.script_name);
    fprintf(file, ",\n  \"stage\": ");
    debugger_play_fprint_json_string(file, runner->result.progress_stage);
    fprintf(file, ",\n  \"completed\": %s,\n", runner->completed ? "true" : "false");
    fprintf(file, "  \"failed\": %s,\n", runner->failed ? "true" : "false");
    fprintf(file, "  \"session_started\": %s,\n", runner->session_started ? "true" : "false");
    fprintf(file, "  \"action_index\": %d,\n", runner->action_index);
    fprintf(file, "  \"action_count\": %d,\n", runner->script.action_count);
    fprintf(file, "  \"screenshot_requested\": %s,\n", runner->screenshot_requested ? "true" : "false");
    fprintf(file, "  \"failure_stage\": ");
    debugger_play_fprint_json_string(file, runner->result.failure_stage);
    fprintf(file, ",\n  \"failure_reason\": ");
    debugger_play_fprint_json_string(file, runner->result.failure_reason);
    fprintf(file, "\n}\n");
    fclose(file);
    return 1;
}

static int debugger_play_write_timings(const DebuggerPlayRunner* runner)
{
    FILE* file;

    if (!runner || !runner->result.timing_path[0]) return 0;
    file = fopen(runner->result.timing_path, "wb");
    if (!file) return 0;
    fprintf(file, "{\n");
    fprintf(file, "  \"world_create_ms\": %llu,\n", (unsigned long long)runner->result.world_create_ms);
    fprintf(file, "  \"bootstrap_resident_ms\": %llu,\n", (unsigned long long)runner->result.bootstrap_resident_ms);
    fprintf(file, "  \"bootstrap_gpu_ms\": %llu,\n", (unsigned long long)runner->result.bootstrap_gpu_ms);
    fprintf(file, "  \"persistence_save_ms\": %llu,\n", (unsigned long long)runner->result.persistence_save_ms);
    fprintf(file, "  \"wall_analysis_ms\": %llu,\n", (unsigned long long)runner->result.wall_analysis_ms);
    fprintf(file, "  \"live_runtime_init_ms\": %llu,\n", (unsigned long long)runner->result.live_runtime_init_ms);
    fprintf(file, "  \"session_start_ms\": %llu,\n", (unsigned long long)runner->result.session_start_ms);
    fprintf(file, "  \"screenshot_capture_ms\": %llu,\n", (unsigned long long)runner->result.screenshot_capture_ms);
    fprintf(file, "  \"shutdown_ms\": %llu,\n", (unsigned long long)runner->result.shutdown_ms);
    fprintf(file, "  \"total_elapsed_ms\": %llu\n", (unsigned long long)runner->result.total_elapsed_ms);
    fprintf(file, "}\n");
    fclose(file);
    return 1;
}

static void debugger_play_store_failure_stage(DebuggerPlayRunner* runner,
                                              const char* stage,
                                              const char* reason)
{
    if (!runner) return;
    runner->failed = 1;
    runner->completed = 1;
    runner->result.success = 0;
    if (stage && stage[0]) {
        strncpy_s(runner->result.failure_stage, sizeof(runner->result.failure_stage), stage, _TRUNCATE);
        strncpy_s(runner->result.progress_stage, sizeof(runner->result.progress_stage), stage, _TRUNCATE);
    } else if (runner->result.progress_stage[0]) {
        strncpy_s(runner->result.failure_stage, sizeof(runner->result.failure_stage),
                  runner->result.progress_stage, _TRUNCATE);
    } else {
        strncpy_s(runner->result.progress_stage, sizeof(runner->result.progress_stage), "failed", _TRUNCATE);
        strncpy_s(runner->result.failure_stage, sizeof(runner->result.failure_stage), "failed", _TRUNCATE);
    }
    if (reason && reason[0]) {
        strncpy_s(runner->result.failure_reason,
                  sizeof(runner->result.failure_reason),
                  reason,
                  _TRUNCATE);
    }
    if (runner->live_runtime_started_ms > 0u && runner->shutdown_started_ms == 0u) {
        runner->shutdown_started_ms = debugger_play_now_ms();
    }
    runner->result.total_elapsed_ms = debugger_play_now_ms() - runner->run_started_ms;
    debugger_play_write_progress(runner);
    debugger_play_write_timings(runner);
}

static void debugger_play_store_failure(DebuggerPlayRunner* runner, const char* reason)
{
    debugger_play_store_failure_stage(runner, NULL, reason);
}

static void debugger_play_set_stage(DebuggerPlayRunner* runner, const char* stage)
{
    if (!runner || !stage || !stage[0]) return;
    if (strcmp(runner->result.progress_stage, stage) == 0) return;
    strncpy_s(runner->result.progress_stage, sizeof(runner->result.progress_stage), stage, _TRUNCATE);
    debugger_play_write_progress(runner);
    debugger_play_write_timings(runner);
}

static int debugger_play_parse_int(const char* text, int* out_value)
{
    char* end_ptr = NULL;
    long value;

    if (!text || !out_value) return 0;
    value = strtol(text, &end_ptr, 10);
    if (end_ptr == text || (end_ptr && *end_ptr != '\0')) return 0;
    *out_value = (int)value;
    return 1;
}

static int debugger_play_parse_u32(const char* text, uint32_t* out_value)
{
    char* end_ptr = NULL;
    unsigned long value;

    if (!text || !out_value) return 0;
    value = strtoul(text, &end_ptr, 10);
    if (end_ptr == text || (end_ptr && *end_ptr != '\0')) return 0;
    *out_value = (uint32_t)value;
    return 1;
}

static int debugger_play_parse_bool(const char* text, bool* out_value)
{
    if (!text || !out_value) return 0;
    if (_stricmp(text, "1") == 0 || _stricmp(text, "true") == 0 ||
        _stricmp(text, "yes") == 0 || _stricmp(text, "on") == 0) {
        *out_value = true;
        return 1;
    }
    if (_stricmp(text, "0") == 0 || _stricmp(text, "false") == 0 ||
        _stricmp(text, "no") == 0 || _stricmp(text, "off") == 0) {
        *out_value = false;
        return 1;
    }
    return 0;
}

static int debugger_play_parse_spawn_mode(const char* text, int* out_value)
{
    if (!text || !out_value) return 0;
    if (_stricmp(text, "random") == 0) { *out_value = 0; return 1; }
    if (_stricmp(text, "center") == 0) { *out_value = 1; return 1; }
    if (_stricmp(text, "safe") == 0) { *out_value = 2; return 1; }
    return debugger_play_parse_int(text, out_value);
}

static int debugger_play_parse_coordinate_system(const char* text, uint8_t* out_value)
{
    int parsed_value = 0;

    if (!text || !out_value) return 0;
    if (_stricmp(text, "chunk") == 0 ||
        _stricmp(text, "chunks") == 0 ||
        _stricmp(text, "chunk-system") == 0) {
        *out_value = (uint8_t)SDK_WORLD_COORDSYS_CHUNK_SYSTEM;
        return 1;
    }
    if (_stricmp(text, "superchunk") == 0 ||
        _stricmp(text, "superchunks") == 0 ||
        _stricmp(text, "superchunk-system") == 0) {
        *out_value = (uint8_t)SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM;
        return 1;
    }
    if (_stricmp(text, "grid-terrain") == 0 ||
        _stricmp(text, "grid_and_terrain_superchunk") == 0 ||
        _stricmp(text, "grid-terrain-superchunk-system") == 0 ||
        _stricmp(text, "grid") == 0) {
        *out_value = (uint8_t)SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
        return 1;
    }
    if (!debugger_play_parse_int(text, &parsed_value)) return 0;
    if (!sdk_world_coordinate_system_is_valid((SdkWorldCoordinateSystem)parsed_value)) return 0;
    *out_value = (uint8_t)parsed_value;
    return 1;
}

static uint8_t debugger_play_coordinate_system_from_legacy_flags(int superchunks_enabled,
                                                                 int walls_detached)
{
    if (!superchunks_enabled) {
        return (uint8_t)SDK_WORLD_COORDSYS_CHUNK_SYSTEM;
    }
    return (uint8_t)(walls_detached
        ? SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM
        : SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM);
}

static void debugger_play_set_coordinate_system(SdkWorldCreateRequest* request,
                                                uint8_t coordinate_system)
{
    if (!request) return;
    request->coordinate_system = coordinate_system;
    request->superchunks_enabled =
        sdk_world_coordinate_system_uses_superchunks(
            (SdkWorldCoordinateSystem)coordinate_system) ? true : false;
}

static void debugger_play_apply_legacy_walls_detached(SdkWorldCreateRequest* request,
                                                      int walls_detached)
{
    if (!request) return;
    debugger_play_set_coordinate_system(
        request,
        debugger_play_coordinate_system_from_legacy_flags(
            request->superchunks_enabled ? 1 : 0,
            walls_detached));
}

static void debugger_play_fill_absolute_path(char* path, size_t path_len)
{
    char full_path[MAX_PATH];

    if (!path || !path[0]) return;
    if (GetFullPathNameA(path, (DWORD)sizeof(full_path), full_path, NULL) == 0u) return;
    strncpy_s(path, path_len, full_path, _TRUNCATE);
}

static int debugger_play_ensure_dir_chain(const char* path)
{
    char scratch[MAX_PATH];
    size_t i;

    if (!path || !path[0]) return 0;
    strncpy_s(scratch, sizeof(scratch), path, _TRUNCATE);
    for (i = 0; scratch[i] != '\0'; ++i) {
        if (scratch[i] == '\\' || scratch[i] == '/') {
            char saved = scratch[i];
            scratch[i] = '\0';
            if (scratch[0]) sdk_world_ensure_directory_exists(scratch);
            scratch[i] = saved;
        }
    }
    return sdk_world_ensure_directory_exists(path);
}

static void debugger_play_apply_wall_summary(SdkAutomationRunResult* result,
                                             const CA_WallAnalysisSummary* summary)
{
    if (!result || !summary) return;
    result->wall_analysis_pass = summary->pass ? 1 : 0;
    result->wall_expected = summary->expected_wall_chunk_count;
    result->wall_correct = summary->correct_wall_chunk_count;
    result->wall_missing = summary->missing_wall_chunk_count;
    result->wall_unexpected = summary->unexpected_wall_chunk_count;
    result->wall_problematic = summary->problematic_wall_chunk_count;
}

static int debugger_play_write_manifest(const DebuggerPlayRunner* runner)
{
    FILE* file;

    if (!runner || !runner->manifest_path[0]) return 0;
    file = fopen(runner->manifest_path, "wb");
    if (!file) return 0;

    fprintf(file, "{\n");
    fprintf(file, "  \"success\": %s,\n", runner->result.success ? "true" : "false");
    fprintf(file, "  \"world_id\": ");
    debugger_play_fprint_json_string(file, runner->result.world_id);
    fprintf(file, ",\n  \"scenario\": ");
    debugger_play_fprint_json_string(file, runner->result.scenario);
    fprintf(file, ",\n  \"script_name\": ");
    debugger_play_fprint_json_string(file, runner->result.script_name);
    fprintf(file, ",\n  \"run_dir\": ");
    debugger_play_fprint_json_string(file, runner->result.run_dir);
    fprintf(file, ",\n  \"progress_path\": ");
    debugger_play_fprint_json_string(file, runner->result.progress_path);
    fprintf(file, ",\n  \"timing_path\": ");
    debugger_play_fprint_json_string(file, runner->result.timing_path);
    fprintf(file, ",\n  \"progress_stage\": ");
    debugger_play_fprint_json_string(file, runner->result.progress_stage);
    fprintf(file, ",\n  \"failure_stage\": ");
    debugger_play_fprint_json_string(file, runner->result.failure_stage);
    fprintf(file, ",\n");
    fprintf(file, "  \"wall_analysis\": {\n");
    fprintf(file, "    \"pass\": %s,\n", runner->result.wall_analysis_pass ? "true" : "false");
    fprintf(file, "    \"expected_wall_chunk_count\": %d,\n", runner->result.wall_expected);
    fprintf(file, "    \"correct_wall_chunk_count\": %d,\n", runner->result.wall_correct);
    fprintf(file, "    \"missing_wall_chunk_count\": %d,\n", runner->result.wall_missing);
    fprintf(file, "    \"unexpected_wall_chunk_count\": %d,\n", runner->result.wall_unexpected);
    fprintf(file, "    \"problematic_wall_chunk_count\": %d\n", runner->result.wall_problematic);
    fprintf(file, "  },\n");
    fprintf(file, "  \"screenshot\": {\n");
    fprintf(file, "    \"completed\": %s,\n", runner->result.screenshot.completed ? "true" : "false");
    fprintf(file, "    \"success\": %s,\n", runner->result.screenshot.success ? "true" : "false");
    fprintf(file, "    \"path\": ");
    debugger_play_fprint_json_string(file, runner->result.screenshot.path);
    fprintf(file, ",\n");
    fprintf(file, "    \"width\": %d,\n", runner->result.screenshot.width);
    fprintf(file, "    \"height\": %d,\n", runner->result.screenshot.height);
    fprintf(file, "    \"failure_reason\": ");
    debugger_play_fprint_json_string(file, runner->result.screenshot.failure_reason);
    fprintf(file, "\n");
    fprintf(file, "  },\n");
    fprintf(file, "  \"timings_ms\": {\n");
    fprintf(file, "    \"world_create\": %llu,\n", (unsigned long long)runner->result.world_create_ms);
    fprintf(file, "    \"bootstrap_resident\": %llu,\n", (unsigned long long)runner->result.bootstrap_resident_ms);
    fprintf(file, "    \"bootstrap_gpu\": %llu,\n", (unsigned long long)runner->result.bootstrap_gpu_ms);
    fprintf(file, "    \"persistence_save\": %llu,\n", (unsigned long long)runner->result.persistence_save_ms);
    fprintf(file, "    \"wall_analysis\": %llu,\n", (unsigned long long)runner->result.wall_analysis_ms);
    fprintf(file, "    \"live_runtime_init\": %llu,\n", (unsigned long long)runner->result.live_runtime_init_ms);
    fprintf(file, "    \"session_start\": %llu,\n", (unsigned long long)runner->result.session_start_ms);
    fprintf(file, "    \"screenshot_capture\": %llu,\n", (unsigned long long)runner->result.screenshot_capture_ms);
    fprintf(file, "    \"shutdown\": %llu,\n", (unsigned long long)runner->result.shutdown_ms);
    fprintf(file, "    \"total\": %llu\n", (unsigned long long)runner->result.total_elapsed_ms);
    fprintf(file, "  },\n");
    fprintf(file, "  \"prompt\": ");
    debugger_play_fprint_json_string(file, runner->result.prompt);
    fprintf(file, ",\n  \"failure_reason\": ");
    debugger_play_fprint_json_string(file, runner->result.failure_reason);
    fprintf(file, "\n");
    fprintf(file, "}\n");
    fclose(file);
    return 1;
}

static void debugger_play_print_result_json(const DebuggerPlayRunner* runner)
{
    if (!runner) return;
    printf("{\"success\":%s,\"world_id\":", runner->result.success ? "true" : "false");
    debugger_play_print_json_string(runner->result.world_id);
    printf(",\"scenario\":");
    debugger_play_print_json_string(runner->result.scenario);
    printf(",\"script_name\":");
    debugger_play_print_json_string(runner->result.script_name);
    printf(",\"run_dir\":");
    debugger_play_print_json_string(runner->result.run_dir);
    printf(",\"progress_path\":");
    debugger_play_print_json_string(runner->result.progress_path);
    printf(",\"timing_path\":");
    debugger_play_print_json_string(runner->result.timing_path);
    printf(",\"manifest_path\":");
    debugger_play_print_json_string(runner->manifest_path);
    printf(",\"progress_stage\":");
    debugger_play_print_json_string(runner->result.progress_stage);
    printf(",\"failure_stage\":");
    debugger_play_print_json_string(runner->result.failure_stage);
    printf(",\"wall_analysis\":{\"pass\":%s,\"expected_wall_chunk_count\":%d,\"correct_wall_chunk_count\":%d,"
           "\"missing_wall_chunk_count\":%d,\"unexpected_wall_chunk_count\":%d,\"problematic_wall_chunk_count\":%d},"
           "\"screenshot\":{\"completed\":%s,\"success\":%s,\"path\":",
           runner->result.wall_analysis_pass ? "true" : "false",
           runner->result.wall_expected,
           runner->result.wall_correct,
           runner->result.wall_missing,
           runner->result.wall_unexpected,
           runner->result.wall_problematic,
           runner->result.screenshot.completed ? "true" : "false",
           runner->result.screenshot.success ? "true" : "false");
    debugger_play_print_json_string(runner->result.screenshot.path);
    printf(",\"width\":%d,\"height\":%d,\"failure_reason\":",
           runner->result.screenshot.width,
           runner->result.screenshot.height);
    debugger_play_print_json_string(runner->result.screenshot.failure_reason);
    printf("},\"timings_ms\":{\"world_create\":%llu,\"bootstrap_resident\":%llu,\"bootstrap_gpu\":%llu,"
           "\"persistence_save\":%llu,\"wall_analysis\":%llu,\"live_runtime_init\":%llu,"
           "\"session_start\":%llu,\"screenshot_capture\":%llu,\"shutdown\":%llu,\"total\":%llu},\"prompt\":",
           (unsigned long long)runner->result.world_create_ms,
           (unsigned long long)runner->result.bootstrap_resident_ms,
           (unsigned long long)runner->result.bootstrap_gpu_ms,
           (unsigned long long)runner->result.persistence_save_ms,
           (unsigned long long)runner->result.wall_analysis_ms,
           (unsigned long long)runner->result.live_runtime_init_ms,
           (unsigned long long)runner->result.session_start_ms,
           (unsigned long long)runner->result.screenshot_capture_ms,
           (unsigned long long)runner->result.shutdown_ms,
           (unsigned long long)runner->result.total_elapsed_ms);
    debugger_play_print_json_string(runner->result.prompt);
    printf(",\"failure_reason\":");
    debugger_play_print_json_string(runner->result.failure_reason);
    printf("}\n");
}

static void debugger_play_make_run_paths(const char* world_id,
                                         char* out_run_dir,
                                         size_t out_run_dir_len,
                                         char* out_screenshot_path,
                                         size_t out_screenshot_path_len,
                                         char* out_progress_path,
                                         size_t out_progress_path_len,
                                         char* out_timing_path,
                                         size_t out_timing_path_len,
                                         char* out_manifest_path,
                                         size_t out_manifest_path_len)
{
    SYSTEMTIME st;
    char run_id[64];

    GetLocalTime(&st);
    snprintf(run_id, sizeof(run_id), "%04u%02u%02u_%02u%02u%02u",
             (unsigned int)st.wYear,
             (unsigned int)st.wMonth,
             (unsigned int)st.wDay,
             (unsigned int)st.wHour,
             (unsigned int)st.wMinute,
             (unsigned int)st.wSecond);
    snprintf(out_run_dir, out_run_dir_len, "Build\\AutomationRuns\\%s\\%s", world_id, run_id);
    snprintf(out_screenshot_path, out_screenshot_path_len, "%s\\screenshot.png", out_run_dir);
    snprintf(out_progress_path, out_progress_path_len, "%s\\progress.json", out_run_dir);
    snprintf(out_timing_path, out_timing_path_len, "%s\\load_timing.json", out_run_dir);
    snprintf(out_manifest_path, out_manifest_path_len, "%s\\run_result.json", out_run_dir);
    debugger_play_fill_absolute_path(out_run_dir, out_run_dir_len);
    debugger_play_fill_absolute_path(out_screenshot_path, out_screenshot_path_len);
    debugger_play_fill_absolute_path(out_progress_path, out_progress_path_len);
    debugger_play_fill_absolute_path(out_timing_path, out_timing_path_len);
    debugger_play_fill_absolute_path(out_manifest_path, out_manifest_path_len);
}

static int debugger_play_compute_wall_viewpoint(const SdkWorldSaveMeta* meta,
                                                float out_cam[3],
                                                float out_target[3])
{
    SdkWorldDesc world_desc;
    SdkSuperchunkConfig config;
    SdkWorldGen worldgen;
    SdkSuperchunkWallGridCell wall_cell;
    SdkTerrainColumnProfile profile;
    int gate_center_block;
    int origin_x;
    int origin_z;
    int cam_wx;
    int cam_wz;
    int target_wx;
    int target_wz;
    int ok = 0;

    if (!meta || !out_cam || !out_target) return 0;

    memset(&world_desc, 0, sizeof(world_desc));
    memset(&config, 0, sizeof(config));
    memset(&worldgen, 0, sizeof(worldgen));

    sdk_world_meta_to_world_desc(meta, &world_desc);
    sdk_world_meta_to_superchunk_config(meta, &config);

    sdk_worldgen_shared_cache_init();
    sdk_superchunk_set_config(&config);
    sdk_worldgen_init(&worldgen, &world_desc);

    sdk_superchunk_wall_cell_from_index(0, 0, &wall_cell);
    gate_center_block = SDK_SUPERCHUNK_GATE_START_BLOCK + (SDK_SUPERCHUNK_GATE_WIDTH_BLOCKS / 2);
    origin_x = wall_cell.origin_cx * CHUNK_WIDTH;
    origin_z = wall_cell.origin_cz * CHUNK_DEPTH;
    cam_wx = origin_x + 160;
    cam_wz = origin_z + gate_center_block;
    target_wx = origin_x + 8;
    target_wz = cam_wz;

    if (sdk_worldgen_sample_column_ctx(&worldgen, cam_wx, cam_wz, &profile)) {
        out_cam[0] = (float)cam_wx + 0.5f;
        out_cam[1] = (float)profile.surface_height + 12.0f;
        out_cam[2] = (float)cam_wz + 0.5f;
        out_target[0] = (float)target_wx + 0.5f;
        out_target[1] = (float)profile.surface_height + 8.0f;
        out_target[2] = (float)target_wz + 0.5f;
        ok = 1;
    }

    sdk_worldgen_shutdown(&worldgen);
    sdk_worldgen_shared_cache_shutdown();
    return ok;
}

static int debugger_play_append_wall_smoke_script(SdkAutomationScript* script,
                                                  const char* screenshot_path,
                                                  const float cam_pos[3],
                                                  const float look_target[3])
{
    SdkAutomationAction action;

    if (!script || !screenshot_path || !cam_pos || !look_target) return 0;

    memset(&action, 0, sizeof(action));
    action.type = SDK_AUTOMATION_ACTION_WAIT_UNTIL_WORLD_READY;
    action.ready_target = SDK_AUTOMATION_READY_RESIDENT;
    action.frames = 3600;
    if (!sdk_automation_script_append(script, &action)) return 0;

    memset(&action, 0, sizeof(action));
    action.type = SDK_AUTOMATION_ACTION_TOGGLE_FLIGHT;
    if (!sdk_automation_script_append(script, &action)) return 0;

    memset(&action, 0, sizeof(action));
    action.type = SDK_AUTOMATION_ACTION_TELEPORT;
    action.x = cam_pos[0];
    action.y = cam_pos[1];
    action.z = cam_pos[2];
    if (!sdk_automation_script_append(script, &action)) return 0;

    memset(&action, 0, sizeof(action));
    action.type = SDK_AUTOMATION_ACTION_LOOK;
    action.use_target = 1;
    action.x = look_target[0];
    action.y = look_target[1];
    action.z = look_target[2];
    if (!sdk_automation_script_append(script, &action)) return 0;

    memset(&action, 0, sizeof(action));
    action.type = SDK_AUTOMATION_ACTION_WAIT_FRAMES;
    action.frames = 120;
    if (!sdk_automation_script_append(script, &action)) return 0;

    memset(&action, 0, sizeof(action));
    action.type = SDK_AUTOMATION_ACTION_CAPTURE_SCREENSHOT;
    action.frames = 600;
    strncpy_s(action.text, sizeof(action.text), screenshot_path, _TRUNCATE);
    if (!sdk_automation_script_append(script, &action)) return 0;

    memset(&action, 0, sizeof(action));
    action.type = SDK_AUTOMATION_ACTION_WAIT_FRAMES;
    action.frames = 10;
    if (!sdk_automation_script_append(script, &action)) return 0;

    memset(&action, 0, sizeof(action));
    action.type = SDK_AUTOMATION_ACTION_EXIT_SESSION;
    return sdk_automation_script_append(script, &action);
}

static int debugger_play_world_ready(int ready_target)
{
    SdkStartupChunkReadiness readiness;

    if (!g_sdk.world_session_active) return 0;
    collect_startup_chunk_readiness(&readiness);
    if (readiness.desired_primary <= 0) {
        return startup_safe_mode_active() ? 0 : 1;
    }
    if (ready_target == SDK_AUTOMATION_READY_RESIDENT) {
        return readiness.resident_primary >= readiness.desired_primary;
    }
    return readiness.gpu_ready_primary >= readiness.desired_primary;
}

static void debugger_play_compute_look_delta(const SdkAutomationAction* action,
                                             float* out_yaw_delta,
                                             float* out_pitch_delta)
{
    float cam_x, cam_y, cam_z;
    float look_x, look_y, look_z;
    float current_dx;
    float current_dy;
    float current_dz;
    float desired_dx;
    float desired_dy;
    float desired_dz;
    float current_yaw;
    float current_pitch;
    float desired_yaw;
    float desired_pitch;
    float yaw_delta;

    if (!out_yaw_delta || !out_pitch_delta || !action) return;
    *out_yaw_delta = action->yaw_delta;
    *out_pitch_delta = action->pitch_delta;
    if (!action->use_target) return;

    sdk_renderer_get_camera_pos(&cam_x, &cam_y, &cam_z);
    sdk_renderer_get_camera_target(&look_x, &look_y, &look_z);
    current_dx = look_x - cam_x;
    current_dy = look_y - cam_y;
    current_dz = look_z - cam_z;
    desired_dx = action->x - cam_x;
    desired_dy = action->y - cam_y;
    desired_dz = action->z - cam_z;
    current_yaw = atan2f(current_dx, current_dz);
    current_pitch = atan2f(current_dy, sqrtf(current_dx * current_dx + current_dz * current_dz));
    desired_yaw = atan2f(desired_dx, desired_dz);
    desired_pitch = atan2f(desired_dy, sqrtf(desired_dx * desired_dx + desired_dz * desired_dz));
    yaw_delta = desired_yaw - current_yaw;
    while (yaw_delta > 3.14159265f) yaw_delta -= 6.28318531f;
    while (yaw_delta < -3.14159265f) yaw_delta += 6.28318531f;
    *out_yaw_delta = yaw_delta;
    *out_pitch_delta = desired_pitch - current_pitch;
}

static int debugger_play_pre_frame(void* user_data)
{
    DebuggerPlayRunner* runner = (DebuggerPlayRunner*)user_data;
    SdkAutomationFrameOverride override_state;
    uint64_t now_ms;

    if (!runner) return 0;
    memset(&override_state, 0, sizeof(override_state));
    now_ms = debugger_play_now_ms();

    if (runner->failed || runner->completed) return 0;

    if (!runner->session_started) {
        if (runner->result.live_runtime_init_ms == 0u && runner->live_runtime_started_ms > 0u) {
            runner->result.live_runtime_init_ms = now_ms - runner->live_runtime_started_ms;
        }
        debugger_play_set_stage(runner, "session_start");
        if (!runner->session_start_requested) {
            runner->shutdown_started_ms = 0u;
            runner->result.session_start_ms = 0u;
            runner->session_start_requested = 1;
            runner->session_start_begin_ms = debugger_play_now_ms();
            runner->session_start_deadline_ms = runner->session_start_begin_ms + 180000u;
            begin_async_world_session_load(&runner->meta);
            sdk_automation_clear_frame_override();
            return 1;
        }
        if (g_sdk.world_session_active) {
            runner->result.session_start_ms = debugger_play_now_ms() - runner->session_start_begin_ms;
            runner->session_started = 1;
            debugger_play_set_stage(runner, "waiting_for_world_ready");
            sdk_automation_clear_frame_override();
            return 1;
        }
        if (!g_world_generation_active) {
            debugger_play_store_failure_stage(runner,
                                              "session_start",
                                              "world session startup ended without activating a session");
            return 0;
        }
        if (runner->session_start_deadline_ms > 0u && debugger_play_now_ms() >= runner->session_start_deadline_ms) {
            debugger_play_store_failure_stage(runner,
                                              "session_start",
                                              "timed out waiting for live world session startup");
            return 0;
        }
        sdk_automation_clear_frame_override();
        return 1;
    }

    while (!runner->failed && !runner->completed && runner->action_index < runner->script.action_count) {
        const SdkAutomationAction* action = &runner->script.actions[runner->action_index];

        switch ((SdkAutomationActionType)action->type) {
            case SDK_AUTOMATION_ACTION_WAIT_FRAMES:
                if (runner->action_frames_remaining <= 0) {
                    runner->action_frames_remaining = action->frames > 0 ? action->frames : 1;
                }
                runner->action_frames_remaining--;
                if (runner->action_frames_remaining <= 0) {
                    runner->action_index++;
                }
                sdk_automation_clear_frame_override();
                return 1;

            case SDK_AUTOMATION_ACTION_WAIT_UNTIL_WORLD_READY:
                debugger_play_set_stage(runner, "waiting_for_world_ready");
                if (debugger_play_world_ready(action->ready_target)) {
                    runner->action_frames_remaining = 0;
                    runner->action_index++;
                    continue;
                }
                if (action->frames > 0) {
                    if (runner->action_frames_remaining <= 0) {
                        runner->action_frames_remaining = action->frames;
                    }
                    runner->action_frames_remaining--;
                    if (runner->action_frames_remaining <= 0) {
                        debugger_play_store_failure(runner, "timed out waiting for world readiness");
                        return 0;
                    }
                }
                sdk_automation_clear_frame_override();
                return 1;

            case SDK_AUTOMATION_ACTION_LOOK:
                debugger_play_set_stage(runner, "orienting_camera");
                override_state.active = true;
                debugger_play_compute_look_delta(action,
                                                 &override_state.yaw_delta,
                                                 &override_state.pitch_delta);
                sdk_automation_set_frame_override(&override_state);
                runner->action_index++;
                return 1;

            case SDK_AUTOMATION_ACTION_MOVE:
                debugger_play_set_stage(runner, "moving_camera");
                if (runner->action_frames_remaining <= 0) {
                    runner->action_frames_remaining = action->frames > 0 ? action->frames : 1;
                }
                override_state.active = true;
                override_state.move_forward = action->move_forward;
                override_state.move_right = action->move_right;
                override_state.move_up = action->move_up;
                sdk_automation_set_frame_override(&override_state);
                runner->action_frames_remaining--;
                if (runner->action_frames_remaining <= 0) {
                    runner->action_index++;
                }
                return 1;

            case SDK_AUTOMATION_ACTION_TOGGLE_FLIGHT:
                debugger_play_set_stage(runner, "toggle_flight");
                override_state.active = true;
                override_state.toggle_flight = true;
                sdk_automation_set_frame_override(&override_state);
                runner->action_index++;
                return 1;

            case SDK_AUTOMATION_ACTION_TELEPORT:
                debugger_play_set_stage(runner, "teleporting_viewpoint");
                override_state.active = true;
                override_state.teleport_pending = true;
                override_state.teleport_x = action->x;
                override_state.teleport_y = action->y;
                override_state.teleport_z = action->z;
                sdk_automation_set_frame_override(&override_state);
                runner->action_index++;
                return 1;

            case SDK_AUTOMATION_ACTION_CAPTURE_SCREENSHOT:
                debugger_play_set_stage(runner, "capture_screenshot");
                if (!runner->screenshot_requested) {
                    if (sdk_renderer_request_screenshot(action->text) != SDK_OK) {
                        debugger_play_store_failure_stage(runner, "capture_screenshot",
                                                          "failed to request screenshot capture");
                        return 0;
                    }
                    runner->screenshot_requested = 1;
                    runner->screenshot_timeout_remaining = action->frames > 0 ? action->frames : 600;
                    runner->screenshot_requested_ms = debugger_play_now_ms();
                } else if (runner->screenshot_timeout_remaining > 0) {
                    runner->screenshot_timeout_remaining--;
                    if (runner->screenshot_timeout_remaining <= 0) {
                        debugger_play_store_failure_stage(runner, "capture_screenshot",
                                                          "timed out waiting for screenshot capture");
                        return 0;
                    }
                }
                sdk_automation_clear_frame_override();
                return 1;

            case SDK_AUTOMATION_ACTION_EXIT_SESSION:
                debugger_play_set_stage(runner, "shutdown");
                runner->shutdown_started_ms = debugger_play_now_ms();
                runner->completed = 1;
                runner->result.success = runner->failed ? 0 : 1;
                sdk_automation_clear_frame_override();
                return 0;

            default:
                debugger_play_store_failure(runner, "unsupported automation action");
                return 0;
        }
    }

    if (runner->action_index >= runner->script.action_count) {
        runner->completed = 1;
        runner->result.success = runner->failed ? 0 : 1;
        return 0;
    }
    return 1;
}

static int debugger_play_post_frame(void* user_data)
{
    DebuggerPlayRunner* runner = (DebuggerPlayRunner*)user_data;
    SdkScreenshotResult screenshot;

    if (!runner) return 0;
    if (runner->failed || runner->completed) return 0;

    memset(&screenshot, 0, sizeof(screenshot));
    if (runner->screenshot_requested && sdk_renderer_poll_screenshot_result(&screenshot)) {
        runner->result.screenshot = screenshot;
        if (runner->screenshot_requested_ms > 0u) {
            runner->result.screenshot_capture_ms =
                debugger_play_now_ms() - runner->screenshot_requested_ms;
        }
        runner->screenshot_requested = 0;
        runner->screenshot_timeout_remaining = 0;
        if (!screenshot.success) {
            debugger_play_store_failure_stage(runner,
                                              "capture_screenshot",
                                              screenshot.failure_reason[0]
                                                  ? screenshot.failure_reason
                                                  : "screenshot capture failed");
            return 0;
        }
        debugger_play_set_stage(runner, "screenshot_captured");
        runner->action_index++;
    }
    return 1;
}

static int debugger_play_bootstrap_world(const SdkSessionStartRequest* request,
                                         SdkSessionStartResult* out_result)
{
    return sdk_session_start_headless(request, out_result) ? 1 : 0;
}

static void debugger_play_apply_wall_smoke_defaults(SdkWorldCreateRequest* request)
{
    if (!request) return;
    memset(request, 0, sizeof(*request));
    request->seed = 0x0BADC0DEu;
    request->render_distance_chunks = 16;
    request->spawn_mode = 2;
    request->settlements_enabled = false;
    request->construction_cells_enabled = false;
    request->coordinate_system = (uint8_t)SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    request->superchunks_enabled = true;
    request->superchunk_chunk_span = 16;
    request->walls_enabled = true;
    request->wall_grid_size = 18;
    request->wall_grid_offset_x = 0;
    request->wall_grid_offset_z = 0;
}

int debugger_cmd_session_play(int argc, char** argv)
{
    SdkSessionStartRequest bootstrap_request;
    SdkSessionStartResult bootstrap_result;
    CA_WallAnalysisSummary wall_summary;
    DebuggerPlayRunner runner;
    SdkRuntimeHostDesc host_desc;
    char world_id[64] = {0};
    char world_dir[MAX_PATH] = {0};
    char script_path[MAX_PATH] = {0};
    char scenario[SDK_AUTOMATION_MAX_NAME] = {0};
    int json = 0;
    int create_if_missing = 0;
    int i;
    uint64_t wall_analysis_started_ms = 0u;
    uint64_t host_returned_ms = 0u;

    memset(&bootstrap_request, 0, sizeof(bootstrap_request));
    memset(&bootstrap_result, 0, sizeof(bootstrap_result));
    memset(&wall_summary, 0, sizeof(wall_summary));
    memset(&runner, 0, sizeof(runner));
    memset(&host_desc, 0, sizeof(host_desc));
    runner.run_started_ms = debugger_play_now_ms();

    bootstrap_request.spawn_mode = -1;
    bootstrap_request.stop_at = SDK_SESSION_STOP_AT_GPU_READY;
    bootstrap_request.safety_radius = 2;
    bootstrap_request.max_iterations = 800;
    bootstrap_request.save_on_success = 1;
    bootstrap_request.create_request.render_distance_chunks = sdk_world_clamp_render_distance_chunks(8);
    bootstrap_request.create_request.spawn_mode = 2;
    bootstrap_request.create_request.settlements_enabled = true;
    bootstrap_request.create_request.construction_cells_enabled = false;
    bootstrap_request.create_request.coordinate_system =
        (uint8_t)SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    bootstrap_request.create_request.superchunks_enabled = true;
    bootstrap_request.create_request.superchunk_chunk_span = 16;
    bootstrap_request.create_request.walls_enabled = true;
    bootstrap_request.create_request.wall_grid_size = 18;

    for (i = 0; i < argc; ++i) {
        if ((strcmp(argv[i], "--world") == 0 || strcmp(argv[i], "--world-id") == 0) && i + 1 < argc) {
            strncpy_s(world_id, sizeof(world_id), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--world-dir") == 0 && i + 1 < argc) {
            strncpy_s(world_dir, sizeof(world_dir), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--create-if-missing") == 0) {
            create_if_missing = 1;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            debugger_play_parse_u32(argv[++i], &bootstrap_request.create_request.seed);
        } else if (strcmp(argv[i], "--render-distance") == 0 && i + 1 < argc) {
            debugger_play_parse_int(argv[++i], &bootstrap_request.create_request.render_distance_chunks);
        } else if (strcmp(argv[i], "--spawn-mode") == 0 && i + 1 < argc) {
            debugger_play_parse_spawn_mode(argv[++i], &bootstrap_request.create_request.spawn_mode);
        } else if (strcmp(argv[i], "--settlements") == 0 && i + 1 < argc) {
            debugger_play_parse_bool(argv[++i], &bootstrap_request.create_request.settlements_enabled);
        } else if (strcmp(argv[i], "--construction-cells") == 0 && i + 1 < argc) {
            debugger_play_parse_bool(argv[++i], &bootstrap_request.create_request.construction_cells_enabled);
        } else if (strcmp(argv[i], "--coordinate-system") == 0 && i + 1 < argc) {
            uint8_t coordinate_system = bootstrap_request.create_request.coordinate_system;
            debugger_play_parse_coordinate_system(argv[++i], &coordinate_system);
            debugger_play_set_coordinate_system(&bootstrap_request.create_request, coordinate_system);
        } else if (strcmp(argv[i], "--superchunks") == 0 && i + 1 < argc) {
            debugger_play_parse_bool(argv[++i], &bootstrap_request.create_request.superchunks_enabled);
            if (!bootstrap_request.create_request.superchunks_enabled) {
                debugger_play_set_coordinate_system(
                    &bootstrap_request.create_request,
                    (uint8_t)SDK_WORLD_COORDSYS_CHUNK_SYSTEM);
            }
        } else if (strcmp(argv[i], "--chunk-span") == 0 && i + 1 < argc) {
            debugger_play_parse_int(argv[++i], &bootstrap_request.create_request.superchunk_chunk_span);
        } else if (strcmp(argv[i], "--walls") == 0 && i + 1 < argc) {
            debugger_play_parse_bool(argv[++i], &bootstrap_request.create_request.walls_enabled);
        } else if (strcmp(argv[i], "--detached-walls") == 0 && i + 1 < argc) {
            bool detached = false;
            debugger_play_parse_bool(argv[++i], &detached);
            debugger_play_apply_legacy_walls_detached(&bootstrap_request.create_request, detached ? 1 : 0);
        } else if (strcmp(argv[i], "--wall-grid-size") == 0 && i + 1 < argc) {
            debugger_play_parse_int(argv[++i], &bootstrap_request.create_request.wall_grid_size);
        } else if (strcmp(argv[i], "--wall-grid-offset-x") == 0 && i + 1 < argc) {
            debugger_play_parse_int(argv[++i], &bootstrap_request.create_request.wall_grid_offset_x);
        } else if (strcmp(argv[i], "--wall-grid-offset-z") == 0 && i + 1 < argc) {
            debugger_play_parse_int(argv[++i], &bootstrap_request.create_request.wall_grid_offset_z);
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            strncpy_s(script_path, sizeof(script_path), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            strncpy_s(scenario, sizeof(scenario), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else {
            fprintf(stderr, "Unknown session play option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!scenario[0] && !script_path[0]) {
        strncpy_s(scenario, sizeof(scenario), "wall-smoke", _TRUNCATE);
    }
    if (_stricmp(scenario, "wall-smoke") == 0) {
        debugger_play_apply_wall_smoke_defaults(&bootstrap_request.create_request);
        create_if_missing = 1;
    }
    if (world_id[0]) {
        strncpy_s(bootstrap_request.world_id, sizeof(bootstrap_request.world_id), world_id, _TRUNCATE);
    }
    if (world_dir[0]) {
        strncpy_s(bootstrap_request.world_dir, sizeof(bootstrap_request.world_dir), world_dir, _TRUNCATE);
    }
    bootstrap_request.create_if_missing = create_if_missing;

    if (!debugger_play_bootstrap_world(&bootstrap_request, &bootstrap_result)) {
        fprintf(stderr, "session play bootstrap failed\n");
        return 1;
    }

    runner.target = bootstrap_result.target;
    runner.meta = bootstrap_result.meta;
    strncpy_s(runner.result.world_id, sizeof(runner.result.world_id), runner.target.folder_id, _TRUNCATE);
    strncpy_s(runner.result.prompt, sizeof(runner.result.prompt), "What does the screenshot show?", _TRUNCATE);
    if (scenario[0]) strncpy_s(runner.result.scenario, sizeof(runner.result.scenario), scenario, _TRUNCATE);

    debugger_play_make_run_paths(runner.target.folder_id,
                                 runner.result.run_dir, sizeof(runner.result.run_dir),
                                 runner.result.screenshot.path, sizeof(runner.result.screenshot.path),
                                 runner.result.progress_path, sizeof(runner.result.progress_path),
                                 runner.result.timing_path, sizeof(runner.result.timing_path),
                                 runner.manifest_path, sizeof(runner.manifest_path));
    if (!debugger_play_ensure_dir_chain(runner.result.run_dir)) {
        fprintf(stderr, "failed to create automation run directory\n");
        return 1;
    }
    runner.result.world_create_ms = bootstrap_result.world_create_ms;
    runner.result.bootstrap_resident_ms = bootstrap_result.resident_ready_ms;
    runner.result.bootstrap_gpu_ms = bootstrap_result.gpu_ready_ms;
    runner.result.persistence_save_ms = bootstrap_result.save_write_ms;
    debugger_play_set_stage(&runner, "headless_bootstrap_complete");

    wall_analysis_started_ms = debugger_play_now_ms();
    if (ca_analyze_wall_summary(runner.target.save_path, 0, 0,
                                DEBUGGER_DEFAULT_WALL_THRESHOLD,
                                &wall_summary) != 0) {
        runner.result.wall_analysis_ms = debugger_play_now_ms() - wall_analysis_started_ms;
        fprintf(stderr, "failed to analyze wall layout\n");
        debugger_play_store_failure_stage(&runner, "wall_analysis", "failed to analyze wall layout");
        debugger_play_write_manifest(&runner);
        return 1;
    }
    runner.result.wall_analysis_ms = debugger_play_now_ms() - wall_analysis_started_ms;
    debugger_play_apply_wall_summary(&runner.result, &wall_summary);
    debugger_play_set_stage(&runner, "wall_analysis_complete");

    if (_stricmp(scenario, "wall-smoke") == 0) {
        float cam_pos[3];
        float look_target[3];

        runner.require_screenshot = 1;
        if (!wall_summary.pass) {
            debugger_play_store_failure_stage(&runner, "wall_analysis", "wall smoke proof failed");
            debugger_play_write_manifest(&runner);
            if (json) debugger_play_print_result_json(&runner);
            return 1;
        }
        sdk_automation_script_init(&runner.script, "wall-smoke");
        strncpy_s(runner.result.script_name, sizeof(runner.result.script_name), "wall-smoke", _TRUNCATE);
        if (!debugger_play_compute_wall_viewpoint(&runner.meta, cam_pos, look_target) ||
            !debugger_play_append_wall_smoke_script(&runner.script,
                                                    runner.result.screenshot.path,
                                                    cam_pos,
                                                    look_target)) {
            sdk_automation_script_free(&runner.script);
            fprintf(stderr, "failed to build wall smoke automation script\n");
            debugger_play_store_failure_stage(&runner, "script_build", "failed to build wall smoke automation script");
            debugger_play_write_manifest(&runner);
            return 1;
        }
    } else {
        if (!sdk_automation_script_load_file(script_path, &runner.script)) {
            fprintf(stderr, "failed to load automation script: %s\n", script_path);
            return 1;
        }
        strncpy_s(runner.result.script_name, sizeof(runner.result.script_name), runner.script.name, _TRUNCATE);
    }

    host_desc.title = "WorldgenDebugger - session play";
    host_desc.width = 1280;
    host_desc.height = 720;
    host_desc.resizable = true;
    host_desc.enable_debug = true;
    host_desc.load_graphics_settings = true;
    host_desc.use_saved_vsync = true;
    host_desc.clear_color.r = 0.1f;
    host_desc.clear_color.g = 0.1f;
    host_desc.clear_color.b = 0.12f;
    host_desc.clear_color.a = 1.0f;
    host_desc.pre_frame = debugger_play_pre_frame;
    host_desc.post_frame = debugger_play_post_frame;
    host_desc.user_data = &runner;

    debugger_play_set_stage(&runner, "live_runtime_init");
    runner.live_runtime_started_ms = debugger_play_now_ms();
    sdk_runtime_host_run(&host_desc);
    host_returned_ms = debugger_play_now_ms();
    if (runner.shutdown_started_ms > 0u) {
        runner.result.shutdown_ms = host_returned_ms - runner.shutdown_started_ms;
    }

    if (!runner.failed) {
        if (!runner.completed) {
            debugger_play_store_failure_stage(&runner,
                                              "live_runtime_closed",
                                              "live runtime closed before automation completed");
        } else if (runner.require_screenshot &&
                   (!runner.result.screenshot.completed || !runner.result.screenshot.success)) {
            debugger_play_store_failure_stage(&runner,
                                              "capture_screenshot",
                                              "scenario ended without a successful screenshot");
        } else {
            runner.result.success = 1;
            debugger_play_set_stage(&runner, "completed");
        }
    }

    runner.result.total_elapsed_ms = debugger_play_now_ms() - runner.run_started_ms;

    debugger_play_write_progress(&runner);
    debugger_play_write_timings(&runner);
    debugger_play_write_manifest(&runner);
    if (json) {
        debugger_play_print_result_json(&runner);
    } else {
        printf("World ID: %s\n", runner.result.world_id);
        printf("Run Dir: %s\n", runner.result.run_dir);
        printf("Screenshot: %s\n", runner.result.screenshot.path);
        printf("Prompt: %s\n", runner.result.prompt);
        if (runner.result.failure_reason[0]) {
            printf("Failure: %s\n", runner.result.failure_reason);
        }
    }

    sdk_automation_script_free(&runner.script);
    return runner.result.success ? 0 : 1;
}
