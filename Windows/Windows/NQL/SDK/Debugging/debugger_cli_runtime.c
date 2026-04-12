#include "debugger_cli.h"

#include "../Core/API/Session/Headless/sdk_session_headless.h"
#include "../Core/World/Chunks/ChunkAnalysis/chunk_analysis.h"
#include "../Core/World/Chunks/ChunkCompression/sdk_chunk_codec.h"
#include "../Core/World/Chunks/sdk_chunk.h"
#include "../Core/World/Persistence/sdk_world_tooling.h"
#include "../Core/World/Persistence/sdk_chunk_save_json.h"
#include "../Core/World/Settlements/sdk_settlement.h"

#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUGGER_MAX_FORWARD_ARGS 256
#define DEBUGGER_CODEC_SUMMARY_CAP 32

int chunk_analysis_cli_main(int argc, char** argv);

typedef struct {
    char   codec_name[64];
    int    chunk_count;
    int    decode_success;
    int    decode_failure;
    size_t total_payload_chars;
} DebuggerCodecSummary;

static void debugger_rt_print_json_string(const char* text)
{
    const char* cursor = text ? text : "";

    putchar('"');
    while (*cursor) {
        char ch = *cursor++;
        if (ch == '\\') fputs("\\\\", stdout);
        else if (ch == '"') fputs("\\\"", stdout);
        else putchar(ch);
    }
    putchar('"');
}

static int debugger_rt_parse_int(const char* text, int* out_value)
{
    char* end_ptr = NULL;
    long value;

    if (!text || !out_value) return 0;
    value = strtol(text, &end_ptr, 10);
    if (end_ptr == text || (end_ptr && *end_ptr != '\0')) return 0;
    *out_value = (int)value;
    return 1;
}

static int debugger_rt_parse_u32(const char* text, uint32_t* out_value)
{
    char* end_ptr = NULL;
    unsigned long value;

    if (!text || !out_value) return 0;
    value = strtoul(text, &end_ptr, 10);
    if (end_ptr == text || (end_ptr && *end_ptr != '\0')) return 0;
    *out_value = (uint32_t)value;
    return 1;
}

static int debugger_rt_parse_bool(const char* text, int* out_value)
{
    if (!text || !out_value) return 0;
    if (_stricmp(text, "1") == 0 || _stricmp(text, "true") == 0 || _stricmp(text, "yes") == 0 || _stricmp(text, "on") == 0) {
        *out_value = 1;
        return 1;
    }
    if (_stricmp(text, "0") == 0 || _stricmp(text, "false") == 0 || _stricmp(text, "no") == 0 || _stricmp(text, "off") == 0) {
        *out_value = 0;
        return 1;
    }
    return 0;
}

static int debugger_rt_parse_bool_flag(const char* text, bool* out_value)
{
    int parsed_value = 0;

    if (!out_value) return 0;
    if (!debugger_rt_parse_bool(text, &parsed_value)) return 0;
    *out_value = parsed_value ? true : false;
    return 1;
}

static int debugger_rt_parse_spawn_mode(const char* text, int* out_value)
{
    if (!text || !out_value) return 0;
    if (_stricmp(text, "random") == 0) {
        *out_value = 0;
        return 1;
    }
    if (_stricmp(text, "center") == 0) {
        *out_value = 1;
        return 1;
    }
    if (_stricmp(text, "safe") == 0) {
        *out_value = 2;
        return 1;
    }
    return debugger_rt_parse_int(text, out_value);
}

static int debugger_rt_parse_coordinate_system(const char* text, uint8_t* out_value)
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
    if (!debugger_rt_parse_int(text, &parsed_value)) return 0;
    if (!sdk_world_coordinate_system_is_valid((SdkWorldCoordinateSystem)parsed_value)) return 0;
    *out_value = (uint8_t)parsed_value;
    return 1;
}

static uint8_t debugger_rt_coordinate_system_from_legacy_flags(int superchunks_enabled,
                                                               int walls_detached)
{
    if (!superchunks_enabled) {
        return (uint8_t)SDK_WORLD_COORDSYS_CHUNK_SYSTEM;
    }
    return (uint8_t)(walls_detached
        ? SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM
        : SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM);
}

static int debugger_rt_coordinate_system_detaches_walls(uint8_t coordinate_system)
{
    return sdk_world_coordinate_system_detaches_walls(
        (SdkWorldCoordinateSystem)coordinate_system);
}

static void debugger_rt_set_coordinate_system(SdkWorldCreateRequest* request,
                                              uint8_t coordinate_system)
{
    if (!request) return;
    request->coordinate_system = coordinate_system;
    request->superchunks_enabled =
        sdk_world_coordinate_system_uses_superchunks(
            (SdkWorldCoordinateSystem)coordinate_system) ? true : false;
}

static void debugger_rt_apply_legacy_walls_detached(SdkWorldCreateRequest* request,
                                                    int walls_detached)
{
    if (!request) return;
    debugger_rt_set_coordinate_system(
        request,
        debugger_rt_coordinate_system_from_legacy_flags(
            request->superchunks_enabled ? 1 : 0,
            walls_detached));
}

static int debugger_rt_resolve_target(const char* world_id,
                                      const char* world_dir,
                                      SdkWorldTarget* out_target)
{
    return sdk_world_target_resolve(world_id && world_id[0] ? world_id : NULL,
                                    world_dir && world_dir[0] ? world_dir : NULL,
                                    out_target);
}

static int debugger_rt_read_text_file(const char* path, char** out_text, size_t* out_size)
{
    FILE* file = NULL;
    long file_size = 0;
    size_t read_size = 0;
    char* text = NULL;

    if (!path || !out_text) return 0;
    *out_text = NULL;
    if (out_size) *out_size = 0u;

    file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    text = (char*)malloc((size_t)file_size + 1u);
    if (!text) {
        fclose(file);
        return 0;
    }

    read_size = fread(text, 1, (size_t)file_size, file);
    fclose(file);
    text[read_size] = '\0';
    *out_text = text;
    if (out_size) *out_size = read_size;
    return 1;
}

static int debugger_rt_run_chunk_analysis_quiet(int argc, char** argv)
{
    FILE* null_file = NULL;
    int stdout_copy = -1;
    int stderr_copy = -1;
    int result = 0;

    fflush(stdout);
    fflush(stderr);

    stdout_copy = _dup(_fileno(stdout));
    stderr_copy = _dup(_fileno(stderr));
    null_file = fopen("NUL", "w");
    if (stdout_copy < 0 || stderr_copy < 0 || !null_file) {
        if (null_file) fclose(null_file);
        if (stdout_copy >= 0) _close(stdout_copy);
        if (stderr_copy >= 0) _close(stderr_copy);
        return chunk_analysis_cli_main(argc, argv);
    }

    _dup2(_fileno(null_file), _fileno(stdout));
    _dup2(_fileno(null_file), _fileno(stderr));

    result = chunk_analysis_cli_main(argc, argv);

    fflush(stdout);
    fflush(stderr);
    _dup2(stdout_copy, _fileno(stdout));
    _dup2(stderr_copy, _fileno(stderr));

    _close(stdout_copy);
    _close(stderr_copy);
    fclose(null_file);
    return result;
}

static DebuggerCodecSummary* debugger_rt_codec_summary_get(DebuggerCodecSummary* summaries,
                                                           int* io_count,
                                                           const char* codec_name)
{
    int i;
    const char* name = codec_name && codec_name[0] ? codec_name : "cell_rle";

    if (!summaries || !io_count) return NULL;
    for (i = 0; i < *io_count; ++i) {
        if (_stricmp(summaries[i].codec_name, name) == 0) {
            return &summaries[i];
        }
    }
    if (*io_count >= DEBUGGER_CODEC_SUMMARY_CAP) return NULL;

    memset(&summaries[*io_count], 0, sizeof(summaries[*io_count]));
    strncpy_s(summaries[*io_count].codec_name, sizeof(summaries[*io_count].codec_name), name, _TRUNCATE);
    (*io_count)++;
    return &summaries[*io_count - 1];
}

static int debugger_rt_find_array_bounds_after_key(const char* search_from,
                                                   const char* end,
                                                   const char* quoted_key,
                                                   const char** out_key_pos,
                                                   const char** out_array_start,
                                                   const char** out_array_end)
{
    const char* key_pos;
    const char* cursor;
    int depth = 0;
    int in_string = 0;
    int escaped = 0;

    if (!search_from || !end || !quoted_key || !out_key_pos || !out_array_start || !out_array_end) return 0;

    key_pos = strstr(search_from, quoted_key);
    if (!key_pos || key_pos >= end) return 0;

    cursor = key_pos + strlen(quoted_key);
    while (cursor < end && *cursor != '[') {
        cursor++;
    }
    if (cursor >= end || *cursor != '[') return 0;

    *out_key_pos = key_pos;
    *out_array_start = cursor + 1;
    for (; cursor < end; ++cursor) {
        char ch = *cursor;

        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (ch == '\\') {
                escaped = 1;
            } else if (ch == '"') {
                in_string = 0;
            }
            continue;
        }

        if (ch == '"') {
            in_string = 1;
            continue;
        }
        if (ch == '[') {
            depth++;
            continue;
        }
        if (ch == ']') {
            depth--;
            if (depth == 0) {
                *out_array_end = cursor;
                return 1;
            }
        }
    }

    return 0;
}

static void debugger_rt_accumulate_codec_entry(const SdkChunkSaveJsonEntry* entry,
                                               int max_chunks,
                                               int* io_sampled_chunks,
                                               int* io_total_chunks,
                                               int* io_total_decode_success,
                                               int* io_total_decode_failure,
                                               DebuggerCodecSummary* summaries,
                                               int* io_summary_count)
{
    DebuggerCodecSummary* summary;
    size_t payload_chars;

    if (!entry || !io_total_chunks || !io_sampled_chunks ||
        !io_total_decode_success || !io_total_decode_failure ||
        !summaries || !io_summary_count) {
        return;
    }

    summary = debugger_rt_codec_summary_get(summaries, io_summary_count,
                                            entry->codec ? entry->codec : "cell_rle");
    if (!summary) return;

    payload_chars = entry->payload_b64 ? strlen(entry->payload_b64) : 0u;
    summary->chunk_count++;
    summary->total_payload_chars += payload_chars;
    (*io_total_chunks)++;

    if (max_chunks <= 0 || *io_sampled_chunks < max_chunks) {
        SdkChunk chunk;
        int decode_ok;

        sdk_chunk_init(&chunk, entry->cx, entry->cz, NULL);
        decode_ok = sdk_chunk_codec_decode(entry->codec ? entry->codec : "cell_rle",
                                           entry->payload_version,
                                           entry->payload_b64 ? entry->payload_b64 : "",
                                           entry->top_y,
                                           &chunk);
        if (decode_ok) {
            summary->decode_success++;
            (*io_total_decode_success)++;
        } else {
            summary->decode_failure++;
            (*io_total_decode_failure)++;
        }
        (*io_sampled_chunks)++;
        sdk_chunk_free(&chunk);
    }
}

static void debugger_rt_process_chunk_array(const char* array_start,
                                            const char* array_end,
                                            int max_chunks,
                                            int* io_sampled_chunks,
                                            int* io_total_chunks,
                                            int* io_total_decode_success,
                                            int* io_total_decode_failure,
                                            DebuggerCodecSummary* summaries,
                                            int* io_summary_count)
{
    const char* cursor = array_start;

    if (!array_start || !array_end || array_end < array_start) return;

    while (cursor < array_end) {
        const char* obj_start = NULL;
        const char* obj_end = NULL;
        SdkChunkSaveJsonEntry entry;

        if (!sdk_chunk_save_json_next_object(&cursor, array_end, &obj_start, &obj_end)) {
            break;
        }

        sdk_chunk_save_json_entry_init(&entry);
        if (!sdk_chunk_save_json_parse_entry(obj_start, obj_end, &entry)) {
            sdk_chunk_save_json_entry_free(&entry);
            continue;
        }

        debugger_rt_accumulate_codec_entry(&entry,
                                           max_chunks,
                                           io_sampled_chunks,
                                           io_total_chunks,
                                           io_total_decode_success,
                                           io_total_decode_failure,
                                           summaries,
                                           io_summary_count);
        sdk_chunk_save_json_entry_free(&entry);
    }
}

void debugger_print_session_usage(const char* program_name)
{
    printf("Usage:\n");
    printf("  %s session bootstrap --world <id>|--world-dir <path> [options]\n", program_name);
    printf("  %s session play --world <id>|--world-dir <path> [--create-if-missing] [--script <path>|--scenario wall-smoke] [--json]\n", program_name);
}

void debugger_print_chunks_usage(const char* program_name)
{
    printf("Usage:\n");
    printf("  %s chunks analyze --world <id>|--world-dir <path> [analysis options] [--json]\n", program_name);
    printf("  %s chunks codecs --world <id>|--world-dir <path> [--max-chunks N] [--json]\n", program_name);
}

static int debugger_cmd_session_bootstrap(int argc, char** argv)
{
    SdkSessionStartRequest request;
    SdkSessionStartResult result;
    int json = 0;
    int i;

    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    request.spawn_mode = -1;
    request.stop_at = SDK_SESSION_STOP_AT_GPU_READY;
    request.safety_radius = 2;
    request.max_iterations = 800;
    request.save_on_success = 1;
    request.create_request.render_distance_chunks = sdk_world_clamp_render_distance_chunks(8);
    request.create_request.spawn_mode = 2;
    request.create_request.settlements_enabled = true;
    request.create_request.construction_cells_enabled = false;
    request.create_request.coordinate_system =
        (uint8_t)SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    request.create_request.superchunks_enabled = true;
    request.create_request.superchunk_chunk_span = 16;
    request.create_request.walls_enabled = true;
    request.create_request.wall_grid_size = 18;

    for (i = 0; i < argc; ++i) {
        if ((strcmp(argv[i], "--world") == 0 || strcmp(argv[i], "--world-id") == 0) && i + 1 < argc) {
            strncpy_s(request.world_id, sizeof(request.world_id), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--world-dir") == 0 && i + 1 < argc) {
            strncpy_s(request.world_dir, sizeof(request.world_dir), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--create-if-missing") == 0) {
            request.create_if_missing = 1;
        } else if (strcmp(argv[i], "--stop-at") == 0 && i + 1 < argc) {
            ++i;
            request.stop_at = (_stricmp(argv[i], "resident") == 0)
                ? SDK_SESSION_STOP_AT_RESIDENT
                : SDK_SESSION_STOP_AT_GPU_READY;
        } else if (strcmp(argv[i], "--safety-radius") == 0 && i + 1 < argc) {
            debugger_rt_parse_int(argv[++i], &request.safety_radius);
        } else if (strcmp(argv[i], "--max-iterations") == 0 && i + 1 < argc) {
            debugger_rt_parse_int(argv[++i], &request.max_iterations);
        } else if (strcmp(argv[i], "--save-on-success") == 0 && i + 1 < argc) {
            debugger_rt_parse_bool(argv[++i], &request.save_on_success);
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            debugger_rt_parse_u32(argv[++i], &request.create_request.seed);
        } else if (strcmp(argv[i], "--render-distance") == 0 && i + 1 < argc) {
            debugger_rt_parse_int(argv[++i], &request.create_request.render_distance_chunks);
        } else if (strcmp(argv[i], "--spawn-mode") == 0 && i + 1 < argc) {
            debugger_rt_parse_spawn_mode(argv[++i], &request.spawn_mode);
            request.create_request.spawn_mode = request.spawn_mode;
        } else if (strcmp(argv[i], "--settlements") == 0 && i + 1 < argc) {
            debugger_rt_parse_bool_flag(argv[++i], &request.create_request.settlements_enabled);
        } else if (strcmp(argv[i], "--construction-cells") == 0 && i + 1 < argc) {
            debugger_rt_parse_bool_flag(argv[++i], &request.create_request.construction_cells_enabled);
        } else if (strcmp(argv[i], "--coordinate-system") == 0 && i + 1 < argc) {
            uint8_t coordinate_system = request.create_request.coordinate_system;
            debugger_rt_parse_coordinate_system(argv[++i], &coordinate_system);
            debugger_rt_set_coordinate_system(&request.create_request, coordinate_system);
        } else if (strcmp(argv[i], "--superchunks") == 0 && i + 1 < argc) {
            debugger_rt_parse_bool_flag(argv[++i], &request.create_request.superchunks_enabled);
            if (!request.create_request.superchunks_enabled) {
                debugger_rt_set_coordinate_system(
                    &request.create_request,
                    (uint8_t)SDK_WORLD_COORDSYS_CHUNK_SYSTEM);
            }
        } else if (strcmp(argv[i], "--chunk-span") == 0 && i + 1 < argc) {
            debugger_rt_parse_int(argv[++i], &request.create_request.superchunk_chunk_span);
        } else if (strcmp(argv[i], "--walls") == 0 && i + 1 < argc) {
            debugger_rt_parse_bool_flag(argv[++i], &request.create_request.walls_enabled);
        } else if (strcmp(argv[i], "--walls-detached") == 0 && i + 1 < argc) {
            bool detached = false;
            debugger_rt_parse_bool_flag(argv[++i], &detached);
            debugger_rt_apply_legacy_walls_detached(&request.create_request, detached ? 1 : 0);
        } else if (strcmp(argv[i], "--wall-grid-size") == 0 && i + 1 < argc) {
            debugger_rt_parse_int(argv[++i], &request.create_request.wall_grid_size);
        } else if (strcmp(argv[i], "--wall-offset-x") == 0 && i + 1 < argc) {
            debugger_rt_parse_int(argv[++i], &request.create_request.wall_grid_offset_x);
        } else if (strcmp(argv[i], "--wall-offset-z") == 0 && i + 1 < argc) {
            debugger_rt_parse_int(argv[++i], &request.create_request.wall_grid_offset_z);
        } else {
            fprintf(stderr, "Unknown session bootstrap option: %s\n", argv[i]);
            return 1;
        }
    }

    sdk_settlement_set_diagnostics_enabled(false);
    if (!sdk_session_start_headless(&request, &result)) {
        sdk_settlement_set_diagnostics_enabled(true);
        if (json) {
            printf("{\"success\":false,\"failure_reason\":");
            debugger_rt_print_json_string(result.failure_reason);
            printf("}\n");
        } else {
            fprintf(stderr, "Headless bootstrap failed: %s\n",
                    result.failure_reason[0] ? result.failure_reason : "unknown error");
        }
        return 1;
    }
    sdk_settlement_set_diagnostics_enabled(true);

    if (json) {
        printf("{\"success\":true,\"world_id\":");
        debugger_rt_print_json_string(result.target.folder_id);
        printf(",\"created_world\":%s", result.created_world ? "true" : "false");
        printf(",\"spawn\":[%.3f,%.3f,%.3f]", result.spawn[0], result.spawn[1], result.spawn[2]);
        printf(",\"spawn_chunk\":[%d,%d]", result.spawn_cx, result.spawn_cz);
        printf(",\"iterations\":%d", result.iterations);
        printf(",\"desired_primary\":%d", result.readiness.desired_primary);
        printf(",\"resident_primary\":%d", result.readiness.resident_primary);
        printf(",\"gpu_ready_primary\":%d", result.readiness.gpu_ready_primary);
        printf(",\"pending_jobs\":%d", result.readiness.pending_jobs);
        printf(",\"pending_results\":%d", result.readiness.pending_results);
        printf(",\"persist_store_attempts\":%d", result.persist_store_attempts);
        printf(",\"persist_store_successes\":%d", result.persist_store_successes);
        printf(",\"persisted_chunk_count\":%d", result.persisted_chunk_count);
        printf(",\"persist_encode_auto_failures\":%d", result.persist_encode_auto_failures);
        printf(",\"persist_cell_rle_failures\":%d", result.persist_cell_rle_failures);
        printf(",\"persist_fluids_failures\":%d", result.persist_fluids_failures);
        printf(",\"persist_construction_failures\":%d", result.persist_construction_failures);
        printf(",\"timings_ms\":{\"total\":%llu,\"world_create\":%llu,\"meta_load\":%llu,"
               "\"persistence_init\":%llu,\"worldgen_init\":%llu,\"spawn_resolve\":%llu,"
               "\"desired_primary\":%llu,\"resident_ready\":%llu,\"gpu_ready\":%llu,"
               "\"save_write\":%llu}}\n",
               (unsigned long long)result.total_elapsed_ms,
               (unsigned long long)result.world_create_ms,
               (unsigned long long)result.meta_load_ms,
               (unsigned long long)result.persistence_init_ms,
               (unsigned long long)result.worldgen_init_ms,
               (unsigned long long)result.spawn_resolve_ms,
               (unsigned long long)result.desired_primary_ms,
               (unsigned long long)result.resident_ready_ms,
               (unsigned long long)result.gpu_ready_ms,
               (unsigned long long)result.save_write_ms);
    } else {
        printf("World ID: %s\n", result.target.folder_id);
        printf("Created World: %s\n", result.created_world ? "yes" : "no");
        printf("Spawn: %.3f, %.3f, %.3f\n", result.spawn[0], result.spawn[1], result.spawn[2]);
        printf("Spawn Chunk: %d, %d\n", result.spawn_cx, result.spawn_cz);
        printf("Iterations: %d\n", result.iterations);
        printf("Desired/Resident/GPU Ready: %d / %d / %d\n",
               result.readiness.desired_primary,
               result.readiness.resident_primary,
               result.readiness.gpu_ready_primary);
        printf("Pending Jobs/Results: %d / %d\n",
               result.readiness.pending_jobs,
               result.readiness.pending_results);
        printf("Persist Store Attempts/Successes/Count: %d / %d / %d\n",
               result.persist_store_attempts,
               result.persist_store_successes,
               result.persisted_chunk_count);
        printf("Persist Failures Auto/RLE/Fluids/Construction: %d / %d / %d / %d\n",
               result.persist_encode_auto_failures,
               result.persist_cell_rle_failures,
               result.persist_fluids_failures,
               result.persist_construction_failures);
        printf("Timings (ms) Create/Meta/Persistence/Worldgen/Spawn/Desired/Resident/GPU/Save/Total: "
               "%llu / %llu / %llu / %llu / %llu / %llu / %llu / %llu / %llu / %llu\n",
               (unsigned long long)result.world_create_ms,
               (unsigned long long)result.meta_load_ms,
               (unsigned long long)result.persistence_init_ms,
               (unsigned long long)result.worldgen_init_ms,
               (unsigned long long)result.spawn_resolve_ms,
               (unsigned long long)result.desired_primary_ms,
               (unsigned long long)result.resident_ready_ms,
               (unsigned long long)result.gpu_ready_ms,
               (unsigned long long)result.save_write_ms,
               (unsigned long long)result.total_elapsed_ms);
    }
    return 0;
}

static int debugger_cmd_chunks_analyze(int argc, char** argv)
{
    char world_id[64] = {0};
    char world_dir[MAX_PATH] = {0};
    SdkWorldTarget target;
    char* forwarded[DEBUGGER_MAX_FORWARD_ARGS];
    char output_prefix[MAX_PATH] = {0};
    char analysis_dir[MAX_PATH] = {0};
    char report_path[MAX_PATH] = {0};
    char* report_json = NULL;
    size_t report_size = 0u;
    int forwarded_count = 0;
    int json = 0;
    int wall_analysis = 0;
    int saw_output = 0;
    int saw_no_csv = 0;
    int result_code;
    int i;
    uint64_t started_ms = GetTickCount64();

    for (i = 0; i < argc; ++i) {
        if ((strcmp(argv[i], "--world") == 0 || strcmp(argv[i], "--world-id") == 0) && i + 1 < argc) {
            strncpy_s(world_id, sizeof(world_id), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--world-dir") == 0 && i + 1 < argc) {
            strncpy_s(world_dir, sizeof(world_dir), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "--wall-analysis") == 0) {
            wall_analysis = 1;
            if (forwarded_count < DEBUGGER_MAX_FORWARD_ARGS - 2) {
                forwarded[forwarded_count++] = argv[i];
            }
        } else if ((strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) && i + 1 < argc) {
            saw_output = 1;
            strncpy_s(output_prefix, sizeof(output_prefix), argv[i + 1], _TRUNCATE);
            if (forwarded_count < DEBUGGER_MAX_FORWARD_ARGS - 2) {
                forwarded[forwarded_count++] = argv[i];
                forwarded[forwarded_count++] = argv[i + 1];
            }
            ++i;
        } else if (strcmp(argv[i], "--no-csv") == 0) {
            saw_no_csv = 1;
            if (forwarded_count < DEBUGGER_MAX_FORWARD_ARGS - 2) {
                forwarded[forwarded_count++] = argv[i];
            }
        } else if (forwarded_count < DEBUGGER_MAX_FORWARD_ARGS - 2) {
            forwarded[forwarded_count++] = argv[i];
        }
    }

    if (!debugger_rt_resolve_target(world_id, world_dir, &target)) {
        fprintf(stderr, "Chunk analysis requires --world or --world-dir\n");
        return 1;
    }
    if (!target.save_exists) {
        fprintf(stderr, "World save file is missing: %s\n", target.save_path);
        return 1;
    }

    if (json && !saw_output) {
        snprintf(analysis_dir, sizeof(analysis_dir), "%s\\analysis", target.world_dir);
        if (!sdk_world_ensure_directory_exists(analysis_dir)) {
            fprintf(stderr, "Failed to create analysis directory: %s\n", analysis_dir);
            return 1;
        }
        snprintf(output_prefix, sizeof(output_prefix), "%s\\chunk_analysis", analysis_dir);
        forwarded[forwarded_count++] = "--output";
        forwarded[forwarded_count++] = output_prefix;
    }
    if (json && !saw_no_csv) {
        forwarded[forwarded_count++] = "--no-csv";
    }
    forwarded[forwarded_count++] = target.save_path;
    forwarded[forwarded_count] = NULL;

    if (!json) {
        return chunk_analysis_cli_main(forwarded_count, forwarded);
    }

    result_code = debugger_rt_run_chunk_analysis_quiet(forwarded_count, forwarded);
    if (result_code != 0) {
        if (wall_analysis) {
            CA_WallAnalysisSummary wall_summary;
            memset(&wall_summary, 0, sizeof(wall_summary));
            if (ca_analyze_wall_summary(target.save_path, 0, 0, 1u, &wall_summary) == 0) {
                printf("{\"success\":true,\"world_id\":");
                debugger_rt_print_json_string(target.folder_id);
                printf(",\"save_path\":");
                debugger_rt_print_json_string(target.save_path);
                printf(",\"analysis_report_available\":false,\"analysis_exit_code\":%d", result_code);
                printf(",\"elapsed_ms\":%llu", (unsigned long long)(GetTickCount64() - started_ms));
                printf(",\"wall_analysis\":{\"pass\":%s,\"expected_wall_chunk_count\":%d,"
                       "\"correct_wall_chunk_count\":%d,\"missing_wall_chunk_count\":%d,"
                       "\"unexpected_wall_chunk_count\":%d,\"problematic_wall_chunk_count\":%d,"
                       "\"normalized_config\":{\"chunk_span\":%d,\"coordinate_system\":%u,\"detached_walls\":%s,\"wall_grid_size\":%d,"
                       "\"wall_grid_offset_x\":%d,\"wall_grid_offset_z\":%d}}",
                       wall_summary.pass ? "true" : "false",
                       wall_summary.expected_wall_chunk_count,
                       wall_summary.correct_wall_chunk_count,
                       wall_summary.missing_wall_chunk_count,
                       wall_summary.unexpected_wall_chunk_count,
                       wall_summary.problematic_wall_chunk_count,
                       wall_summary.config.chunk_span,
                       (unsigned)wall_summary.config.coordinate_system,
                       debugger_rt_coordinate_system_detaches_walls(wall_summary.config.coordinate_system) ? "true" : "false",
                       wall_summary.config.wall_grid_size,
                       wall_summary.config.wall_grid_offset_x,
                       wall_summary.config.wall_grid_offset_z);
                printf(",\"report_loaded\":false}\n");
                return 0;
            }
        }

        printf("{\"success\":false,\"world_id\":");
        debugger_rt_print_json_string(target.folder_id);
        printf(",\"save_path\":");
        debugger_rt_print_json_string(target.save_path);
        printf(",\"analysis_exit_code\":%d,\"elapsed_ms\":%llu}\n",
               result_code,
               (unsigned long long)(GetTickCount64() - started_ms));
        return result_code;
    }

    snprintf(report_path, sizeof(report_path), "%s_report.json", output_prefix);
    if (!debugger_rt_read_text_file(report_path, &report_json, &report_size)) {
        printf("{\"success\":true,\"world_id\":");
        debugger_rt_print_json_string(target.folder_id);
        printf(",\"save_path\":");
        debugger_rt_print_json_string(target.save_path);
        printf(",\"report_path\":");
        debugger_rt_print_json_string(report_path);
        printf(",\"elapsed_ms\":%llu", (unsigned long long)(GetTickCount64() - started_ms));
        if (wall_analysis) {
            CA_WallAnalysisSummary wall_summary;
            memset(&wall_summary, 0, sizeof(wall_summary));
            if (ca_analyze_wall_summary(target.save_path, 0, 0, 1u, &wall_summary) == 0) {
                printf(",\"wall_analysis\":{\"pass\":%s,\"expected_wall_chunk_count\":%d,"
                       "\"correct_wall_chunk_count\":%d,\"missing_wall_chunk_count\":%d,"
                       "\"unexpected_wall_chunk_count\":%d,\"problematic_wall_chunk_count\":%d,"
                       "\"normalized_config\":{\"chunk_span\":%d,\"coordinate_system\":%u,\"detached_walls\":%s,\"wall_grid_size\":%d,"
                       "\"wall_grid_offset_x\":%d,\"wall_grid_offset_z\":%d}}",
                       wall_summary.pass ? "true" : "false",
                       wall_summary.expected_wall_chunk_count,
                       wall_summary.correct_wall_chunk_count,
                       wall_summary.missing_wall_chunk_count,
                       wall_summary.unexpected_wall_chunk_count,
                       wall_summary.problematic_wall_chunk_count,
                       wall_summary.config.chunk_span,
                       (unsigned)wall_summary.config.coordinate_system,
                       debugger_rt_coordinate_system_detaches_walls(wall_summary.config.coordinate_system) ? "true" : "false",
                       wall_summary.config.wall_grid_size,
                       wall_summary.config.wall_grid_offset_x,
                       wall_summary.config.wall_grid_offset_z);
            }
        }
        printf(",\"report_loaded\":false}\n");
        return 0;
    }

    printf("{\"success\":true,\"world_id\":");
    debugger_rt_print_json_string(target.folder_id);
    printf(",\"save_path\":");
    debugger_rt_print_json_string(target.save_path);
    printf(",\"report_path\":");
    debugger_rt_print_json_string(report_path);
    printf(",\"elapsed_ms\":%llu", (unsigned long long)(GetTickCount64() - started_ms));
    if (wall_analysis) {
        CA_WallAnalysisSummary wall_summary;
        memset(&wall_summary, 0, sizeof(wall_summary));
        if (ca_analyze_wall_summary(target.save_path, 0, 0, 1u, &wall_summary) == 0) {
            printf(",\"wall_analysis\":{\"pass\":%s,\"expected_wall_chunk_count\":%d,"
                   "\"correct_wall_chunk_count\":%d,\"missing_wall_chunk_count\":%d,"
                   "\"unexpected_wall_chunk_count\":%d,\"problematic_wall_chunk_count\":%d,"
                   "\"normalized_config\":{\"chunk_span\":%d,\"coordinate_system\":%u,\"detached_walls\":%s,\"wall_grid_size\":%d,"
                   "\"wall_grid_offset_x\":%d,\"wall_grid_offset_z\":%d}}",
                   wall_summary.pass ? "true" : "false",
                   wall_summary.expected_wall_chunk_count,
                   wall_summary.correct_wall_chunk_count,
                   wall_summary.missing_wall_chunk_count,
                   wall_summary.unexpected_wall_chunk_count,
                   wall_summary.problematic_wall_chunk_count,
                   wall_summary.config.chunk_span,
                   (unsigned)wall_summary.config.coordinate_system,
                   debugger_rt_coordinate_system_detaches_walls(wall_summary.config.coordinate_system) ? "true" : "false",
                   wall_summary.config.wall_grid_size,
                   wall_summary.config.wall_grid_offset_x,
                   wall_summary.config.wall_grid_offset_z);
        }
    }
    printf(",\"report\":%s}\n", report_json);
    free(report_json);
    return 0;
}

static int debugger_cmd_chunks_codecs(int argc, char** argv)
{
    char world_id[64] = {0};
    char world_dir[MAX_PATH] = {0};
    SdkWorldTarget target;
    int max_chunks = 0;
    int json = 0;
    char* file_text = NULL;
    size_t file_size = 0u;
    const char* end = NULL;
    const char* search = NULL;
    DebuggerCodecSummary summaries[DEBUGGER_CODEC_SUMMARY_CAP];
    int summary_count = 0;
    int total_chunks = 0;
    int sampled_chunks = 0;
    int total_decode_success = 0;
    int total_decode_failure = 0;
    int i;

    memset(summaries, 0, sizeof(summaries));

    for (i = 0; i < argc; ++i) {
        if ((strcmp(argv[i], "--world") == 0 || strcmp(argv[i], "--world-id") == 0) && i + 1 < argc) {
            strncpy_s(world_id, sizeof(world_id), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--world-dir") == 0 && i + 1 < argc) {
            strncpy_s(world_dir, sizeof(world_dir), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--max-chunks") == 0 && i + 1 < argc) {
            debugger_rt_parse_int(argv[++i], &max_chunks);
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else {
            fprintf(stderr, "Unknown chunks codecs option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!debugger_rt_resolve_target(world_id, world_dir, &target)) {
        fprintf(stderr, "Chunk codecs requires --world or --world-dir\n");
        return 1;
    }
    if (!target.save_exists) {
        fprintf(stderr, "World save file is missing: %s\n", target.save_path);
        return 1;
    }

    if (!debugger_rt_read_text_file(target.save_path, &file_text, &file_size)) {
        fprintf(stderr, "Failed to read world save: %s\n", target.save_path);
        return 1;
    }

    end = file_text + file_size;
    search = file_text;
    while (search < end) {
        const char* key_pos = NULL;
        const char* array_start = NULL;
        const char* array_end = NULL;

        if (!debugger_rt_find_array_bounds_after_key(search, end, "\"chunks\"",
                                                     &key_pos, &array_start, &array_end)) {
            break;
        }

        debugger_rt_process_chunk_array(array_start, array_end,
                                        max_chunks,
                                        &sampled_chunks,
                                        &total_chunks,
                                        &total_decode_success,
                                        &total_decode_failure,
                                        summaries,
                                        &summary_count);
        search = array_end + 1;
    }

    search = file_text;
    while (search < end) {
        const char* key_pos = NULL;
        const char* array_start = NULL;
        const char* array_end = NULL;

        if (!debugger_rt_find_array_bounds_after_key(search, end, "\"wall_chunks\"",
                                                     &key_pos, &array_start, &array_end)) {
            break;
        }

        debugger_rt_process_chunk_array(array_start, array_end,
                                        max_chunks,
                                        &sampled_chunks,
                                        &total_chunks,
                                        &total_decode_success,
                                        &total_decode_failure,
                                        summaries,
                                        &summary_count);
        search = array_end + 1;
    }

    free(file_text);

    if (total_chunks <= 0) {
        fprintf(stderr, "No chunk entries found in save: %s\n", target.save_path);
        return 1;
    }

    if (json) {
        printf("{\"world_id\":");
        debugger_rt_print_json_string(target.folder_id);
        printf(",\"save_path\":");
        debugger_rt_print_json_string(target.save_path);
        printf(",\"total_chunks\":%d,\"sampled_chunks\":%d,\"decode_success\":%d,\"decode_failure\":%d,\"codecs\":[",
               total_chunks, sampled_chunks, total_decode_success, total_decode_failure);
        for (i = 0; i < summary_count; ++i) {
            if (i > 0) printf(",");
            printf("{\"name\":");
            debugger_rt_print_json_string(summaries[i].codec_name);
            printf(",\"chunk_count\":%d,\"payload_chars\":%zu,\"decode_success\":%d,\"decode_failure\":%d}",
                   summaries[i].chunk_count,
                   summaries[i].total_payload_chars,
                   summaries[i].decode_success,
                   summaries[i].decode_failure);
        }
        printf("]}\n");
    } else {
        printf("World ID: %s\n", target.folder_id);
        printf("Save Path: %s\n", target.save_path);
        printf("Total Chunk Entries: %d\n", total_chunks);
        printf("Sampled Decode Count: %d\n", sampled_chunks);
        printf("Decode Success/Failure: %d / %d\n", total_decode_success, total_decode_failure);
        printf("\nCodec Summary\n");
        printf("-------------\n");
        for (i = 0; i < summary_count; ++i) {
            printf("%s: chunks=%d payload_chars=%zu decode_ok=%d decode_fail=%d\n",
                   summaries[i].codec_name,
                   summaries[i].chunk_count,
                   summaries[i].total_payload_chars,
                   summaries[i].decode_success,
                   summaries[i].decode_failure);
        }
    }

    return 0;
}

int debugger_cmd_session(int argc, char** argv)
{
    if (argc < 1) {
        debugger_print_session_usage("nql_debug");
        return 1;
    }
    if (strcmp(argv[0], "bootstrap") == 0) return debugger_cmd_session_bootstrap(argc - 1, argv + 1);
    if (strcmp(argv[0], "play") == 0) return debugger_cmd_session_play(argc - 1, argv + 1);
    fprintf(stderr, "Unknown session subcommand: %s\n", argv[0]);
    return 1;
}

int debugger_cmd_chunks(int argc, char** argv)
{
    if (argc < 1) {
        debugger_print_chunks_usage("nql_debug");
        return 1;
    }
    if (strcmp(argv[0], "analyze") == 0) return debugger_cmd_chunks_analyze(argc - 1, argv + 1);
    if (strcmp(argv[0], "codecs") == 0) return debugger_cmd_chunks_codecs(argc - 1, argv + 1);
    fprintf(stderr, "Unknown chunks subcommand: %s\n", argv[0]);
    return 1;
}
