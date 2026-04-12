#include "debugger_cli.h"

#include "../Core/World/Persistence/sdk_persistence.h"
#include "../Core/World/Persistence/sdk_world_tooling.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int raw_loaded;
    int normalized_loaded;
    int persisted_loaded;
    int warning_count;
    SdkWorldTarget target;
    SdkWorldSaveMeta raw_meta;
    SdkWorldSaveMeta normalized_meta;
    SdkWorldDesc persisted_desc;
    SdkWorldDesc normalized_desc;
} DebuggerWorldDoctorReport;

static void debugger_print_json_string(const char* text)
{
    const char* cursor = text ? text : "";

    putchar('"');
    while (*cursor) {
        char ch = *cursor++;
        switch (ch) {
            case '\\': fputs("\\\\", stdout); break;
            case '"':  fputs("\\\"", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if ((unsigned char)ch < 0x20u) {
                    printf("\\u%04x", (unsigned char)ch);
                } else {
                    putchar(ch);
                }
                break;
        }
    }
    putchar('"');
}

static int debugger_parse_int(const char* text, int* out_value)
{
    char* end_ptr = NULL;
    long value;

    if (!text || !out_value) return 0;
    value = strtol(text, &end_ptr, 10);
    if (end_ptr == text || (end_ptr && *end_ptr != '\0')) return 0;
    *out_value = (int)value;
    return 1;
}

static int debugger_parse_u32(const char* text, uint32_t* out_value)
{
    char* end_ptr = NULL;
    unsigned long value;

    if (!text || !out_value) return 0;
    value = strtoul(text, &end_ptr, 10);
    if (end_ptr == text || (end_ptr && *end_ptr != '\0')) return 0;
    *out_value = (uint32_t)value;
    return 1;
}

static int debugger_parse_bool(const char* text, int* out_value)
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

static int debugger_parse_bool_flag(const char* text, bool* out_value)
{
    int parsed_value = 0;

    if (!out_value) return 0;
    if (!debugger_parse_bool(text, &parsed_value)) return 0;
    *out_value = parsed_value ? true : false;
    return 1;
}

static int debugger_parse_spawn_mode(const char* text, int* out_value)
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
    return debugger_parse_int(text, out_value);
}

static int debugger_parse_coordinate_system(const char* text, uint8_t* out_value)
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
    if (!debugger_parse_int(text, &parsed_value)) return 0;
    if (!sdk_world_coordinate_system_is_valid((SdkWorldCoordinateSystem)parsed_value)) return 0;
    *out_value = (uint8_t)parsed_value;
    return 1;
}

static uint8_t debugger_coordinate_system_from_legacy_flags(int superchunks_enabled,
                                                            int walls_detached)
{
    if (!superchunks_enabled) {
        return (uint8_t)SDK_WORLD_COORDSYS_CHUNK_SYSTEM;
    }
    return (uint8_t)(walls_detached
        ? SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM
        : SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM);
}

static int debugger_coordinate_system_detaches_walls(uint8_t coordinate_system)
{
    return sdk_world_coordinate_system_detaches_walls(
        (SdkWorldCoordinateSystem)coordinate_system);
}

static void debugger_world_set_coordinate_system(SdkWorldCreateRequest* request,
                                                 uint8_t coordinate_system)
{
    if (!request) return;
    request->coordinate_system = coordinate_system;
    request->superchunks_enabled =
        sdk_world_coordinate_system_uses_superchunks(
            (SdkWorldCoordinateSystem)coordinate_system) ? true : false;
}

static void debugger_world_apply_legacy_walls_detached(SdkWorldCreateRequest* request,
                                                       int walls_detached)
{
    if (!request) return;
    debugger_world_set_coordinate_system(
        request,
        debugger_coordinate_system_from_legacy_flags(
            request->superchunks_enabled ? 1 : 0,
            walls_detached));
}

static int debugger_resolve_target(const char* world_id,
                                   const char* world_dir,
                                   SdkWorldTarget* out_target)
{
    return sdk_world_target_resolve(world_id && world_id[0] ? world_id : NULL,
                                    world_dir && world_dir[0] ? world_dir : NULL,
                                    out_target);
}

static int debugger_load_persisted_world_desc(const SdkWorldTarget* target, SdkWorldDesc* out_desc)
{
    SdkPersistence persistence;
    SdkWorldDesc requested_desc;
    int loaded;

    if (!target || !out_desc || !target->save_exists) return 0;
    memset(&persistence, 0, sizeof(persistence));
    memset(&requested_desc, 0, sizeof(requested_desc));
    sdk_persistence_init(&persistence, &requested_desc, target->save_path);
    loaded = sdk_persistence_get_world_desc(&persistence, out_desc);
    sdk_persistence_shutdown(&persistence);
    return loaded;
}

void debugger_print_general_usage(const char* program_name)
{
    printf("NQL Debug CLI\n");
    printf("=============\n");
    printf("Usage:\n");
    printf("  %s world create [options]\n", program_name);
    printf("  %s world meta --world <id>|--world-dir <path> [--json]\n", program_name);
    printf("  %s world doctor --world <id>|--world-dir <path> [--json]\n", program_name);
    printf("  %s session bootstrap --world <id>|--world-dir <path> [--json]\n", program_name);
    printf("  %s session play --world <id>|--world-dir <path> [--create-if-missing] [--script <path>|--scenario wall-smoke] [--json]\n", program_name);
    printf("  %s chunks analyze --world <id>|--world-dir <path> [analysis options] [--json]\n", program_name);
    printf("  %s chunks codecs --world <id>|--world-dir <path> [--max-chunks N] [--json]\n", program_name);
    printf("  %s walls map [wall options]\n", program_name);
    printf("\nLegacy wall flags still route to `walls map`.\n");
}

void debugger_print_world_usage(const char* program_name)
{
    printf("Usage:\n");
    printf("  %s world create [options]\n", program_name);
    printf("  %s world meta --world <id>|--world-dir <path> [--json]\n", program_name);
    printf("  %s world doctor --world <id>|--world-dir <path> [--json]\n", program_name);
}

static void debugger_print_meta_text(const SdkWorldTarget* target,
                                     const SdkWorldSaveMeta* raw_meta,
                                     int raw_loaded,
                                     const SdkWorldSaveMeta* normalized_meta,
                                     int normalized_loaded,
                                     const SdkWorldDesc* persisted_desc,
                                     int persisted_loaded)
{
    printf("World ID: %s\n", target->folder_id);
    printf("World Dir: %s\n", target->world_dir);
    printf("Meta Path: %s\n", target->meta_path);
    printf("Save Path: %s\n", target->save_path);
    printf("Meta Exists: %s\n", target->meta_exists ? "yes" : "no");
    printf("Save Exists: %s\n", target->save_exists ? "yes" : "no");

    printf("\nRaw Meta\n");
    printf("--------\n");
    if (!raw_loaded) {
        printf("Not available\n");
    } else {
        printf("Display Name: %s\n", raw_meta->display_name);
        printf("Seed: %u\n", raw_meta->seed);
        printf("Render Distance: %d\n", raw_meta->render_distance_chunks);
        printf("Coordinate System: %s (%u)\n",
               sdk_world_coordinate_system_display_name(
                   (SdkWorldCoordinateSystem)raw_meta->coordinate_system),
               (unsigned)raw_meta->coordinate_system);
        printf("Settlements: %d\n", raw_meta->settlements_enabled ? 1 : 0);
        printf("Construction Cells: %d\n", raw_meta->construction_cells_enabled ? 1 : 0);
        printf("Legacy Superchunks Enabled: %d\n", raw_meta->superchunks_enabled ? 1 : 0);
        printf("Chunk Span: %d\n", raw_meta->superchunk_chunk_span);
        printf("Walls: %d\n", raw_meta->walls_enabled ? 1 : 0);
        printf("Detached Walls (Derived): %d\n",
               debugger_coordinate_system_detaches_walls(raw_meta->coordinate_system) ? 1 : 0);
        printf("Wall Grid Size: %d\n", raw_meta->wall_grid_size);
        printf("Wall Offset X/Z: %d / %d\n", raw_meta->wall_grid_offset_x, raw_meta->wall_grid_offset_z);
    }

    printf("\nNormalized Runtime Meta\n");
    printf("-----------------------\n");
    if (!normalized_loaded) {
        printf("Not available\n");
    } else {
        printf("Display Name: %s\n", normalized_meta->display_name);
        printf("Seed: %u\n", normalized_meta->seed);
        printf("Render Distance: %d\n", normalized_meta->render_distance_chunks);
        printf("Coordinate System: %s (%u)\n",
               sdk_world_coordinate_system_display_name(
                   (SdkWorldCoordinateSystem)normalized_meta->coordinate_system),
               (unsigned)normalized_meta->coordinate_system);
        printf("Settlements: %d\n", normalized_meta->settlements_enabled ? 1 : 0);
        printf("Construction Cells: %d\n", normalized_meta->construction_cells_enabled ? 1 : 0);
        printf("Legacy Superchunks Enabled: %d\n", normalized_meta->superchunks_enabled ? 1 : 0);
        printf("Chunk Span: %d\n", normalized_meta->superchunk_chunk_span);
        printf("Walls: %d\n", normalized_meta->walls_enabled ? 1 : 0);
        printf("Detached Walls (Derived): %d\n",
               debugger_coordinate_system_detaches_walls(normalized_meta->coordinate_system) ? 1 : 0);
        printf("Wall Grid Size: %d\n", normalized_meta->wall_grid_size);
        printf("Wall Offset X/Z: %d / %d\n", normalized_meta->wall_grid_offset_x, normalized_meta->wall_grid_offset_z);
    }

    printf("\nPersisted World Desc\n");
    printf("--------------------\n");
    if (!persisted_loaded) {
        printf("Not available\n");
    } else {
        printf("Seed: %u\n", persisted_desc->seed);
        printf("Settlements Enabled: %d\n", persisted_desc->settlements_enabled ? 1 : 0);
        printf("Construction Cells Enabled: %d\n", persisted_desc->construction_cells_enabled ? 1 : 0);
    }
}

static void debugger_print_meta_json(const SdkWorldTarget* target,
                                     const SdkWorldSaveMeta* raw_meta,
                                     int raw_loaded,
                                     const SdkWorldSaveMeta* normalized_meta,
                                     int normalized_loaded,
                                     const SdkWorldDesc* persisted_desc,
                                     int persisted_loaded)
{
    printf("{\"target\":{\"world_id\":");
    debugger_print_json_string(target->folder_id);
    printf(",\"world_dir\":");
    debugger_print_json_string(target->world_dir);
    printf(",\"meta_path\":");
    debugger_print_json_string(target->meta_path);
    printf(",\"save_path\":");
    debugger_print_json_string(target->save_path);
    printf(",\"meta_exists\":%s,\"save_exists\":%s}",
           target->meta_exists ? "true" : "false",
           target->save_exists ? "true" : "false");
    printf(",\"raw_loaded\":%s,\"normalized_loaded\":%s,\"persisted_loaded\":%s",
           raw_loaded ? "true" : "false",
           normalized_loaded ? "true" : "false",
           persisted_loaded ? "true" : "false");
    if (normalized_loaded) {
        printf(",\"normalized\":{\"seed\":%u,\"render_distance_chunks\":%d,\"coordinate_system\":%u,\"coordinate_system_name\":",
               normalized_meta->seed,
               normalized_meta->render_distance_chunks,
               (unsigned)normalized_meta->coordinate_system);
        debugger_print_json_string(
            sdk_world_coordinate_system_name(
                (SdkWorldCoordinateSystem)normalized_meta->coordinate_system));
        printf(",\"superchunks_enabled\":%s,\"walls_enabled\":%s,\"detached_walls\":%s,\"wall_grid_size\":%d}",
               normalized_meta->superchunks_enabled ? "true" : "false",
               normalized_meta->walls_enabled ? "true" : "false",
               debugger_coordinate_system_detaches_walls(normalized_meta->coordinate_system) ? "true" : "false",
               normalized_meta->wall_grid_size);
    }
    if (persisted_loaded) {
        printf(",\"persisted_world_desc\":{\"seed\":%u,\"settlements_enabled\":%s,\"construction_cells_enabled\":%s}",
               persisted_desc->seed,
               persisted_desc->settlements_enabled ? "true" : "false",
               persisted_desc->construction_cells_enabled ? "true" : "false");
    }
    printf("}\n");
}

static int debugger_count_doctor_warnings(DebuggerWorldDoctorReport* report)
{
    int warnings = 0;

    if (!report) return 0;
    if (!report->target.meta_exists) warnings++;
    if (!report->target.save_exists) warnings++;
    if (report->raw_loaded && report->normalized_loaded) {
        if (report->raw_meta.coordinate_system != report->normalized_meta.coordinate_system) warnings++;
        if (report->raw_meta.superchunks_enabled != report->normalized_meta.superchunks_enabled) warnings++;
        if (report->raw_meta.walls_enabled != report->normalized_meta.walls_enabled) warnings++;
        if (debugger_coordinate_system_detaches_walls(report->raw_meta.coordinate_system) !=
            debugger_coordinate_system_detaches_walls(report->normalized_meta.coordinate_system)) warnings++;
        if (report->raw_meta.wall_grid_size != report->normalized_meta.wall_grid_size) warnings++;
        if (report->raw_meta.render_distance_chunks != report->normalized_meta.render_distance_chunks) warnings++;
    }
    if (report->persisted_loaded) {
        if (report->persisted_desc.seed != report->normalized_desc.seed) warnings++;
        if (report->persisted_desc.settlements_enabled != report->normalized_desc.settlements_enabled) warnings++;
        if (report->persisted_desc.construction_cells_enabled != report->normalized_desc.construction_cells_enabled) warnings++;
    }
    report->warning_count = warnings;
    return warnings;
}

static int debugger_cmd_world_create(int argc, char** argv)
{
    SdkWorldCreateRequest request;
    SdkWorldCreateResult result;
    int json = 0;
    int i;

    memset(&request, 0, sizeof(request));
    request.render_distance_chunks = sdk_world_clamp_render_distance_chunks(8);
    request.spawn_mode = 2;
    request.settlements_enabled = true;
    request.construction_cells_enabled = false;
    request.coordinate_system = (uint8_t)SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    request.superchunks_enabled = true;
    request.superchunk_chunk_span = 16;
    request.walls_enabled = true;
    request.wall_grid_size = 18;

    for (i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if ((strcmp(argv[i], "--world") == 0 || strcmp(argv[i], "--world-id") == 0) && i + 1 < argc) {
            strncpy_s(request.folder_id, sizeof(request.folder_id), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--world-dir") == 0 && i + 1 < argc) {
            strncpy_s(request.output_dir, sizeof(request.output_dir), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            strncpy_s(request.display_name, sizeof(request.display_name), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            debugger_parse_u32(argv[++i], &request.seed);
        } else if (strcmp(argv[i], "--render-distance") == 0 && i + 1 < argc) {
            debugger_parse_int(argv[++i], &request.render_distance_chunks);
        } else if (strcmp(argv[i], "--spawn-mode") == 0 && i + 1 < argc) {
            debugger_parse_spawn_mode(argv[++i], &request.spawn_mode);
        } else if (strcmp(argv[i], "--settlements") == 0 && i + 1 < argc) {
            debugger_parse_bool_flag(argv[++i], &request.settlements_enabled);
        } else if (strcmp(argv[i], "--construction-cells") == 0 && i + 1 < argc) {
            debugger_parse_bool_flag(argv[++i], &request.construction_cells_enabled);
        } else if (strcmp(argv[i], "--coordinate-system") == 0 && i + 1 < argc) {
            uint8_t coordinate_system = request.coordinate_system;
            debugger_parse_coordinate_system(argv[++i], &coordinate_system);
            debugger_world_set_coordinate_system(&request, coordinate_system);
        } else if (strcmp(argv[i], "--superchunks") == 0 && i + 1 < argc) {
            debugger_parse_bool_flag(argv[++i], &request.superchunks_enabled);
            if (!request.superchunks_enabled) {
                debugger_world_set_coordinate_system(&request, (uint8_t)SDK_WORLD_COORDSYS_CHUNK_SYSTEM);
            }
        } else if (strcmp(argv[i], "--chunk-span") == 0 && i + 1 < argc) {
            debugger_parse_int(argv[++i], &request.superchunk_chunk_span);
        } else if (strcmp(argv[i], "--walls") == 0 && i + 1 < argc) {
            debugger_parse_bool_flag(argv[++i], &request.walls_enabled);
        } else if (strcmp(argv[i], "--walls-detached") == 0 && i + 1 < argc) {
            bool detached = false;
            debugger_parse_bool_flag(argv[++i], &detached);
            debugger_world_apply_legacy_walls_detached(&request, detached ? 1 : 0);
        } else if (strcmp(argv[i], "--wall-grid-size") == 0 && i + 1 < argc) {
            debugger_parse_int(argv[++i], &request.wall_grid_size);
        } else if (strcmp(argv[i], "--wall-offset-x") == 0 && i + 1 < argc) {
            debugger_parse_int(argv[++i], &request.wall_grid_offset_x);
        } else if (strcmp(argv[i], "--wall-offset-z") == 0 && i + 1 < argc) {
            debugger_parse_int(argv[++i], &request.wall_grid_offset_z);
        } else if (strcmp(argv[i], "--allow-existing") == 0) {
            request.allow_existing = true;
        } else {
            fprintf(stderr, "Unknown world create option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!sdk_world_create(&request, &result)) {
        fprintf(stderr, "Failed to create world target\n");
        return 1;
    }

    if (json) {
        printf("{\"created\":%s,\"world_id\":", result.existed ? "false" : "true");
        debugger_print_json_string(result.target.folder_id);
        printf(",\"world_dir\":");
        debugger_print_json_string(result.target.world_dir);
        printf(",\"meta_path\":");
        debugger_print_json_string(result.target.meta_path);
        printf(",\"save_path\":");
        debugger_print_json_string(result.target.save_path);
        printf(",\"seed\":%u,\"render_distance_chunks\":%d,\"coordinate_system\":%u,\"coordinate_system_name\":",
               result.meta.seed,
               result.meta.render_distance_chunks,
               (unsigned)result.meta.coordinate_system);
        debugger_print_json_string(
            sdk_world_coordinate_system_name(
                (SdkWorldCoordinateSystem)result.meta.coordinate_system));
        printf(",\"superchunks_enabled\":%s,\"walls_enabled\":%s,\"detached_walls\":%s,\"wall_grid_size\":%d,\"spawn_mode\":%d}\n",
               result.meta.superchunks_enabled ? "true" : "false",
               result.meta.walls_enabled ? "true" : "false",
               debugger_coordinate_system_detaches_walls(result.meta.coordinate_system) ? "true" : "false",
               result.meta.wall_grid_size,
               request.spawn_mode);
    } else {
        printf("World ID: %s\n", result.target.folder_id);
        printf("World Dir: %s\n", result.target.world_dir);
        printf("Meta Path: %s\n", result.target.meta_path);
        printf("Save Path: %s\n", result.target.save_path);
        printf("Existed: %s\n", result.existed ? "yes" : "no");
        printf("Seed: %u\n", result.meta.seed);
        printf("Render Distance: %d\n", result.meta.render_distance_chunks);
        printf("Coordinate System: %s (%u)\n",
               sdk_world_coordinate_system_display_name(
                   (SdkWorldCoordinateSystem)result.meta.coordinate_system),
               (unsigned)result.meta.coordinate_system);
        printf("Legacy Superchunks Enabled: %d\n", result.meta.superchunks_enabled ? 1 : 0);
        printf("Walls: %d\n", result.meta.walls_enabled ? 1 : 0);
        printf("Detached Walls (Derived): %d\n",
               debugger_coordinate_system_detaches_walls(result.meta.coordinate_system) ? 1 : 0);
        printf("Wall Grid Size: %d\n", result.meta.wall_grid_size);
        printf("Spawn Mode: %d\n", request.spawn_mode);
    }
    return 0;
}

static int debugger_cmd_world_meta(int argc, char** argv)
{
    char world_id[64] = {0};
    char world_dir[MAX_PATH] = {0};
    SdkWorldTarget target;
    SdkWorldSaveMeta raw_meta;
    SdkWorldSaveMeta normalized_meta;
    SdkWorldDesc persisted_desc;
    int raw_loaded;
    int normalized_loaded;
    int persisted_loaded;
    int json = 0;
    int i;

    for (i = 0; i < argc; ++i) {
        if ((strcmp(argv[i], "--world") == 0 || strcmp(argv[i], "--world-id") == 0) && i + 1 < argc) {
            strncpy_s(world_id, sizeof(world_id), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--world-dir") == 0 && i + 1 < argc) {
            strncpy_s(world_dir, sizeof(world_dir), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else {
            fprintf(stderr, "Unknown world meta option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!debugger_resolve_target(world_id, world_dir, &target)) {
        fprintf(stderr, "World target is required\n");
        return 1;
    }

    memset(&raw_meta, 0, sizeof(raw_meta));
    memset(&normalized_meta, 0, sizeof(normalized_meta));
    memset(&persisted_desc, 0, sizeof(persisted_desc));
    raw_loaded = sdk_world_target_load_meta_raw(&target, &raw_meta);
    normalized_loaded = sdk_world_target_load_meta(&target, &normalized_meta);
    persisted_loaded = debugger_load_persisted_world_desc(&target, &persisted_desc);

    if (json) {
        debugger_print_meta_json(&target,
                                 &raw_meta, raw_loaded,
                                 &normalized_meta, normalized_loaded,
                                 &persisted_desc, persisted_loaded);
    } else {
        debugger_print_meta_text(&target,
                                 &raw_meta, raw_loaded,
                                 &normalized_meta, normalized_loaded,
                                 &persisted_desc, persisted_loaded);
    }
    return 0;
}

static int debugger_cmd_world_doctor(int argc, char** argv)
{
    char world_id[64] = {0};
    char world_dir[MAX_PATH] = {0};
    DebuggerWorldDoctorReport report;
    int json = 0;
    int i;

    memset(&report, 0, sizeof(report));

    for (i = 0; i < argc; ++i) {
        if ((strcmp(argv[i], "--world") == 0 || strcmp(argv[i], "--world-id") == 0) && i + 1 < argc) {
            strncpy_s(world_id, sizeof(world_id), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--world-dir") == 0 && i + 1 < argc) {
            strncpy_s(world_dir, sizeof(world_dir), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else {
            fprintf(stderr, "Unknown world doctor option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!debugger_resolve_target(world_id, world_dir, &report.target)) {
        fprintf(stderr, "World target is required\n");
        return 1;
    }

    report.raw_loaded = sdk_world_target_load_meta_raw(&report.target, &report.raw_meta);
    report.normalized_loaded = sdk_world_target_load_meta(&report.target, &report.normalized_meta);
    if (report.normalized_loaded) {
        sdk_world_meta_to_world_desc(&report.normalized_meta, &report.normalized_desc);
    }
    report.persisted_loaded = debugger_load_persisted_world_desc(&report.target, &report.persisted_desc);
    debugger_count_doctor_warnings(&report);

    if (json) {
        printf("{\"world_id\":");
        debugger_print_json_string(report.target.folder_id);
        printf(",\"warning_count\":%d,\"meta_exists\":%s,\"save_exists\":%s,\"raw_loaded\":%s,\"normalized_loaded\":%s,\"persisted_loaded\":%s}\n",
               report.warning_count,
               report.target.meta_exists ? "true" : "false",
               report.target.save_exists ? "true" : "false",
               report.raw_loaded ? "true" : "false",
               report.normalized_loaded ? "true" : "false",
               report.persisted_loaded ? "true" : "false");
    } else {
        printf("World ID: %s\n", report.target.folder_id);
        printf("Warnings: %d\n", report.warning_count);
        printf("Meta Exists: %s\n", report.target.meta_exists ? "yes" : "no");
        printf("Save Exists: %s\n", report.target.save_exists ? "yes" : "no");
    }
    return report.warning_count > 0 ? 2 : 0;
}

int debugger_cmd_world(int argc, char** argv)
{
    if (argc < 1) {
        debugger_print_world_usage("nql_debug");
        return 1;
    }
    if (strcmp(argv[0], "create") == 0) return debugger_cmd_world_create(argc - 1, argv + 1);
    if (strcmp(argv[0], "meta") == 0) return debugger_cmd_world_meta(argc - 1, argv + 1);
    if (strcmp(argv[0], "doctor") == 0) return debugger_cmd_world_doctor(argc - 1, argv + 1);
    fprintf(stderr, "Unknown world subcommand: %s\n", argv[0]);
    return 1;
}
