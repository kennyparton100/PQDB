#include "sdk_world_tooling.h"
#include "../Chunks/sdk_chunk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int k_render_distance_presets[] = { 4, 6, 8, 10, 12, 16 };

static int sdk_world_file_exists_a(const char* path)
{
    DWORD attrs;

    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) &&
           ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static int sdk_world_dir_exists_a(const char* path)
{
    DWORD attrs;

    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) &&
           ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

static const char* sdk_world_path_basename(const char* path)
{
    const char* slash = NULL;
    const char* backslash = NULL;

    if (!path) return NULL;
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (!slash) return backslash ? backslash + 1 : path;
    if (!backslash) return slash + 1;
    return (slash > backslash) ? slash + 1 : backslash + 1;
}

static int sdk_world_path_parent_dir(const char* path, char* out_dir, size_t out_dir_len)
{
    const char* basename;
    size_t prefix_len;

    if (!path || !path[0] || !out_dir || out_dir_len == 0u) return 0;
    basename = sdk_world_path_basename(path);
    if (!basename || basename == path) return 0;
    prefix_len = (size_t)(basename - path);
    if (prefix_len == 0u || prefix_len >= out_dir_len) return 0;
    memcpy(out_dir, path, prefix_len);
    while (prefix_len > 0u &&
           (out_dir[prefix_len - 1u] == '\\' || out_dir[prefix_len - 1u] == '/')) {
        prefix_len--;
    }
    out_dir[prefix_len] = '\0';
    return prefix_len > 0u;
}

static int sdk_world_is_save_or_meta_path(const char* path)
{
    const char* base = sdk_world_path_basename(path);

    if (!base) return 0;
    return _stricmp(base, SDK_WORLD_SAVE_DATA_NAME) == 0 ||
           _stricmp(base, SDK_WORLD_SAVE_META_NAME) == 0;
}

static void sdk_world_meta_set_defaults(SdkWorldSaveMeta* meta)
{
    if (!meta) return;

    meta->spawn_mode = 2;
    meta->settlements_enabled = true;
    meta->construction_cells_enabled = false;
    meta->coordinate_system = (uint8_t)SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    meta->superchunks_enabled = true;
    meta->superchunk_chunk_span = 16;
    meta->walls_enabled = true;
    meta->wall_grid_size = 18;
    meta->wall_grid_offset_x = 0;
    meta->wall_grid_offset_z = 0;
    meta->wall_thickness_blocks = CHUNK_WIDTH;
    meta->wall_rings_shared = true;
}

static uint8_t sdk_world_resolve_request_coordinate_system(const SdkWorldCreateRequest* request)
{
    SdkWorldCoordinateSystem coordinate_system;

    if (!request) {
        return (uint8_t)SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    }

    coordinate_system = (SdkWorldCoordinateSystem)request->coordinate_system;
    if (!sdk_world_coordinate_system_is_valid(coordinate_system)) {
        fprintf(stderr, "Error: Invalid coordinate system in request\n");
        exit(1);
    }

    return (uint8_t)coordinate_system;
}

static int sdk_world_load_meta_file_internal(SdkWorldSaveMeta* meta, int normalize)
{
    char meta_path[MAX_PATH];
    FILE* file;
    char line[160];

    if (!meta || !meta->folder_id[0]) return 0;
    if (!sdk_world_build_meta_file_path(meta->folder_id, meta_path, sizeof(meta_path))) return 0;

    sdk_world_meta_set_defaults(meta);

    file = fopen(meta_path, "rb");
    if (!file) {
        if (normalize) {
            sdk_world_meta_normalize(meta);
        }
        return 0;
    }

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "name=", 5) == 0) {
            sdk_world_copy_ascii_trimmed(meta->display_name, sizeof(meta->display_name), line + 5);
        } else if (strncmp(line, "seed=", 5) == 0) {
            meta->seed = (uint32_t)strtoul(line + 5, NULL, 10);
        } else if (strncmp(line, "render_distance_chunks=", 23) == 0) {
            meta->render_distance_chunks = sdk_world_clamp_render_distance_chunks((int)strtol(line + 23, NULL, 10));
        } else if (strncmp(line, "spawn_mode=", 11) == 0) {
            meta->spawn_mode = (int)strtol(line + 11, NULL, 10);
        } else if (strncmp(line, "settlements_enabled=", 20) == 0) {
            meta->settlements_enabled = (strtol(line + 20, NULL, 10) != 0);
        } else if (strncmp(line, "construction_cells_enabled=", 27) == 0) {
            meta->construction_cells_enabled = (strtol(line + 27, NULL, 10) != 0);
        } else if (strncmp(line, "coordinate_system=", 18) == 0) {
            meta->coordinate_system = (uint8_t)strtol(line + 18, NULL, 10);
        } else if (strncmp(line, "superchunks_enabled=", 20) == 0 ||
                   strncmp(line, "superchunks_enabled=", 19) == 0) {
            const char* value = strchr(line, '=');
            meta->superchunks_enabled = value ? (strtol(value + 1, NULL, 10) != 0) : meta->superchunks_enabled;
        } else if (strncmp(line, "superchunk_chunk_span=", 22) == 0) {
            meta->superchunk_chunk_span = (int)strtol(line + 22, NULL, 10);
        } else if (strncmp(line, "walls_enabled=", 14) == 0) {
            meta->walls_enabled = (strtol(line + 14, NULL, 10) != 0);
        } else if (strncmp(line, "wall_grid_size=", 15) == 0 ||
                   strncmp(line, "wall_grid_size=", 16) == 0) {
            const char* value = strchr(line, '=');
            meta->wall_grid_size = value ? (int)strtol(value + 1, NULL, 10) : meta->wall_grid_size;
        } else if (strncmp(line, "wall_grid_offset_x=", 19) == 0 ||
                   strncmp(line, "wall_grid_offset_x=", 20) == 0) {
            const char* value = strchr(line, '=');
            meta->wall_grid_offset_x = value ? (int)strtol(value + 1, NULL, 10) : meta->wall_grid_offset_x;
        } else if (strncmp(line, "wall_grid_offset_z=", 19) == 0 ||
                   strncmp(line, "wall_grid_offset_z=", 20) == 0) {
            const char* value = strchr(line, '=');
            meta->wall_grid_offset_z = value ? (int)strtol(value + 1, NULL, 10) : meta->wall_grid_offset_z;
        } else if (strncmp(line, "wall_thickness_blocks=", 22) == 0) {
            meta->wall_thickness_blocks = (int)strtol(line + 22, NULL, 10);
        } else if (strncmp(line, "wall_rings_shared=", 18) == 0) {
            meta->wall_rings_shared = (strtol(line + 18, NULL, 10) != 0);
        }
    }

    fclose(file);

    if (normalize) {
        sdk_world_meta_normalize(meta);
    }
    return 1;
}

static void sdk_world_fill_target_state(SdkWorldTarget* target)
{
    if (!target) return;
    target->directory_exists = sdk_world_dir_exists_a(target->world_dir) ? true : false;
    target->meta_exists = sdk_world_file_exists_a(target->meta_path) ? true : false;
    target->save_exists = sdk_world_file_exists_a(target->save_path) ? true : false;
}

static int sdk_world_next_available_folder_id(char* out_folder_id, size_t out_folder_id_len)
{
    int index;

    if (!out_folder_id || out_folder_id_len == 0u) return 0;
    if (!sdk_world_ensure_directory_exists(SDK_WORLD_SAVE_ROOT)) return 0;

    for (index = 1; index < 10000; ++index) {
        char folder_id[64];
        char dir_path[MAX_PATH];

        snprintf(folder_id, sizeof(folder_id), "world_%03d", index);
        if (!sdk_world_build_save_dir_path(folder_id, dir_path, sizeof(dir_path))) return 0;
        if (!sdk_world_dir_exists_a(dir_path)) {
            strncpy_s(out_folder_id, out_folder_id_len, folder_id, _TRUNCATE);
            return 1;
        }
    }

    return 0;
}

static void sdk_world_default_display_name(const char* folder_id,
                                           char* out_display_name,
                                           size_t out_display_name_len)
{
    int numeric_suffix = 0;

    if (!out_display_name || out_display_name_len == 0u) return;
    out_display_name[0] = '\0';
    if (!folder_id || !folder_id[0]) return;

    if (sscanf(folder_id, "world_%d", &numeric_suffix) == 1) {
        snprintf(out_display_name, out_display_name_len, "WORLD %03d", numeric_suffix);
    } else {
        strncpy_s(out_display_name, out_display_name_len, folder_id, _TRUNCATE);
    }
}

int sdk_world_clamp_render_distance_chunks(int radius)
{
    int best_index = 0;
    int best_distance = 0x7fffffff;
    int i;
    int clamped_radius = radius;

    if (clamped_radius < -4096) clamped_radius = -4096;
    if (clamped_radius > 4096) clamped_radius = 4096;

    for (i = 0; i < (int)(sizeof(k_render_distance_presets) / sizeof(k_render_distance_presets[0])); ++i) {
        int distance = clamped_radius - k_render_distance_presets[i];
        if (distance < 0) distance = -distance;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return k_render_distance_presets[best_index];
}

uint32_t sdk_world_generate_new_seed(void)
{
    LARGE_INTEGER qpc;
    uint64_t mixed;

    QueryPerformanceCounter(&qpc);
    mixed = (uint64_t)qpc.QuadPart ^
            (uint64_t)GetTickCount64() ^
            ((uint64_t)GetCurrentProcessId() << 32);
    mixed ^= mixed >> 33;
    mixed *= 0xff51afd7ed558ccdULL;
    mixed ^= mixed >> 33;
    if ((uint32_t)mixed == 0u) {
        mixed ^= 0xA341316Cu;
    }
    return (uint32_t)mixed;
}

int sdk_world_ensure_directory_exists(const char* path)
{
    DWORD attrs;

    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    if (CreateDirectoryA(path, NULL)) {
        return 1;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

void sdk_world_copy_ascii_trimmed(char* dst, size_t dst_len, const char* src)
{
    size_t len = 0u;

    if (!dst || dst_len == 0u) return;
    dst[0] = '\0';
    if (!src) return;

    while (*src == ' ' || *src == '\t') src++;
    while (src[len] != '\0' && src[len] != '\r' && src[len] != '\n' &&
           len + 1u < dst_len) {
        char ch = src[len];
        if (ch == '=') ch = '-';
        dst[len] = ch;
        len++;
    }
    while (len > 0u && (dst[len - 1u] == ' ' || dst[len - 1u] == '\t')) {
        len--;
    }
    dst[len] = '\0';
}

void sdk_world_meta_normalize(SdkWorldSaveMeta* meta)
{
    SdkWorldCoordinateSystem coordinate_system;
    int default_wall_grid_size;
    int wall_thickness_chunks;

    if (!meta) return;

    if (meta->superchunk_chunk_span <= 0) {
        meta->superchunk_chunk_span = 16;
    }

    coordinate_system = (SdkWorldCoordinateSystem)meta->coordinate_system;
    if (!sdk_world_coordinate_system_is_valid(coordinate_system)) {
        fprintf(stderr, "Error: Invalid coordinate system %d in meta\n", coordinate_system);
        exit(1);
    }

    meta->coordinate_system = (uint8_t)coordinate_system;
    meta->superchunks_enabled =
        sdk_world_coordinate_system_uses_superchunks(coordinate_system) ? true : false;
    meta->walls_enabled = meta->walls_enabled ? true : false;
    meta->wall_rings_shared = meta->wall_rings_shared ? true : false;
    if (meta->wall_thickness_blocks <= 0) {
        meta->wall_thickness_blocks = CHUNK_WIDTH;
    }
    if (meta->wall_thickness_blocks > CHUNK_WIDTH) {
        meta->wall_thickness_blocks = CHUNK_WIDTH;
    }
    wall_thickness_chunks = (meta->wall_thickness_blocks + CHUNK_WIDTH - 1) / CHUNK_WIDTH;
    if (wall_thickness_chunks <= 0) wall_thickness_chunks = 1;
    default_wall_grid_size = meta->superchunk_chunk_span + wall_thickness_chunks + wall_thickness_chunks;

    if (meta->walls_enabled && sdk_world_coordinate_system_detaches_walls(coordinate_system)) {
        if (meta->wall_grid_size <= 2 || meta->wall_grid_size < default_wall_grid_size) {
            meta->wall_grid_size = default_wall_grid_size;
        }
    } else {
        meta->wall_grid_size = default_wall_grid_size;
        meta->wall_grid_offset_x = 0;
        meta->wall_grid_offset_z = 0;
        meta->wall_rings_shared = true;
    }

    if (meta->spawn_mode < 0 || meta->spawn_mode > 2) {
        meta->spawn_mode = 2;
    }
    meta->render_distance_chunks = sdk_world_clamp_render_distance_chunks(meta->render_distance_chunks);
}

void sdk_world_apply_create_request_to_meta(const SdkWorldCreateRequest* request,
                                            SdkWorldSaveMeta* meta)
{
    if (!request || !meta) return;

    memset(meta, 0, sizeof(*meta));
    if (request->folder_id[0]) {
        strncpy_s(meta->folder_id, sizeof(meta->folder_id), request->folder_id, _TRUNCATE);
    }
    if (request->display_name[0]) {
        strncpy_s(meta->display_name, sizeof(meta->display_name), request->display_name, _TRUNCATE);
    }

    meta->seed = request->seed;
    meta->render_distance_chunks = sdk_world_clamp_render_distance_chunks(request->render_distance_chunks);
    meta->spawn_mode = request->spawn_mode;
    meta->settlements_enabled = request->settlements_enabled ? true : false;
    meta->construction_cells_enabled = request->construction_cells_enabled ? true : false;
    meta->coordinate_system = sdk_world_resolve_request_coordinate_system(request);
    meta->superchunks_enabled = request->superchunks_enabled ? true : false;
    meta->superchunk_chunk_span = request->superchunk_chunk_span > 0 ? request->superchunk_chunk_span : 16;
    meta->walls_enabled = request->walls_enabled ? true : false;
    meta->wall_grid_size = request->wall_grid_size;
    meta->wall_grid_offset_x = request->wall_grid_offset_x;
    meta->wall_grid_offset_z = request->wall_grid_offset_z;
    meta->wall_thickness_blocks = request->wall_thickness_blocks;
    meta->wall_rings_shared =
        request->wall_rings_shared ||
        (request->wall_grid_size <= 0 && request->wall_thickness_blocks <= 0);
    sdk_world_meta_normalize(meta);
}

void sdk_world_meta_to_world_desc(const SdkWorldSaveMeta* meta, SdkWorldDesc* out_world_desc)
{
    if (!meta || !out_world_desc) return;
    memset(out_world_desc, 0, sizeof(*out_world_desc));
    out_world_desc->seed = meta->seed;
    out_world_desc->coordinate_system = meta->coordinate_system;
    out_world_desc->settlements_enabled = meta->settlements_enabled;
    out_world_desc->walls_enabled = meta->walls_enabled;
    out_world_desc->construction_cells_enabled = meta->construction_cells_enabled;
}

void sdk_world_meta_to_superchunk_config(const SdkWorldSaveMeta* meta,
                                         SdkSuperchunkConfig* out_config)
{
    if (!meta || !out_config) return;
    memset(out_config, 0, sizeof(*out_config));
    out_config->chunk_span = meta->superchunk_chunk_span > 0 ? meta->superchunk_chunk_span : 16;
    out_config->coordinate_system = meta->coordinate_system;
    out_config->enabled =
        sdk_world_coordinate_system_uses_superchunks((SdkWorldCoordinateSystem)meta->coordinate_system) ? true : false;
    out_config->walls_enabled = meta->walls_enabled ? true : false;
    out_config->wall_grid_size = meta->wall_grid_size > 1 ? meta->wall_grid_size : 18;
    out_config->wall_grid_offset_x = meta->wall_grid_offset_x;
    out_config->wall_grid_offset_z = meta->wall_grid_offset_z;
    out_config->wall_thickness_blocks =
        meta->wall_thickness_blocks > 0 ? meta->wall_thickness_blocks : CHUNK_WIDTH;
    out_config->wall_rings_shared = meta->wall_rings_shared ? true : false;
    sdk_superchunk_normalize_config(out_config);
}

int sdk_world_build_save_dir_path(const char* folder_id, char* out_path, size_t out_path_len)
{
    if (!out_path || out_path_len == 0u || !folder_id || !folder_id[0]) return 0;
    out_path[0] = '\0';
    if (!sdk_world_ensure_directory_exists(SDK_WORLD_SAVE_ROOT)) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", SDK_WORLD_SAVE_ROOT, folder_id) > 0;
}

int sdk_world_build_save_file_path(const char* folder_id, char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!sdk_world_build_save_dir_path(folder_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", dir_path, SDK_WORLD_SAVE_DATA_NAME) > 0;
}

int sdk_world_build_meta_file_path(const char* folder_id, char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!sdk_world_build_save_dir_path(folder_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", dir_path, SDK_WORLD_SAVE_META_NAME) > 0;
}

int sdk_world_build_debug_log_file_path(const char* folder_id, char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!sdk_world_build_save_dir_path(folder_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", dir_path, SDK_WORLD_DEBUG_LOG_NAME) > 0;
}

void sdk_world_save_meta_file(const SdkWorldSaveMeta* meta)
{
    char dir_path[MAX_PATH];
    char meta_path[MAX_PATH];
    FILE* file;
    SdkWorldSaveMeta normalized_meta;

    if (!meta || !meta->folder_id[0]) return;
    normalized_meta = *meta;
    sdk_world_meta_normalize(&normalized_meta);
    if (!sdk_world_build_save_dir_path(meta->folder_id, dir_path, sizeof(dir_path))) return;
    if (!sdk_world_ensure_directory_exists(dir_path)) return;
    if (!sdk_world_build_meta_file_path(meta->folder_id, meta_path, sizeof(meta_path))) return;

    file = fopen(meta_path, "wb");
    if (!file) return;
    fprintf(file, "name=%s\n", normalized_meta.display_name);
    fprintf(file, "seed=%u\n", normalized_meta.seed);
    fprintf(file, "render_distance_chunks=%d\n", normalized_meta.render_distance_chunks);
    fprintf(file, "spawn_mode=%d\n", normalized_meta.spawn_mode);
    fprintf(file, "settlements_enabled=%d\n", normalized_meta.settlements_enabled ? 1 : 0);
    fprintf(file, "construction_cells_enabled=%d\n", normalized_meta.construction_cells_enabled ? 1 : 0);
    fprintf(file, "coordinate_system=%u\n", (unsigned)normalized_meta.coordinate_system);
    fprintf(file, "superchunks_enabled=%d\n", normalized_meta.superchunks_enabled ? 1 : 0);
    fprintf(file, "superchunk_chunk_span=%d\n", normalized_meta.superchunk_chunk_span);
    fprintf(file, "walls_enabled=%d\n", normalized_meta.walls_enabled ? 1 : 0);
    fprintf(file, "wall_grid_size=%d\n", normalized_meta.wall_grid_size);
    fprintf(file, "wall_grid_offset_x=%d\n", normalized_meta.wall_grid_offset_x);
    fprintf(file, "wall_grid_offset_z=%d\n", normalized_meta.wall_grid_offset_z);
    fprintf(file, "wall_thickness_blocks=%d\n", normalized_meta.wall_thickness_blocks);
    fprintf(file, "wall_rings_shared=%d\n", normalized_meta.wall_rings_shared ? 1 : 0);
    fclose(file);
}

void sdk_world_load_meta_file(SdkWorldSaveMeta* meta)
{
    (void)sdk_world_load_meta_file_internal(meta, 1);
}

void sdk_world_load_meta_file_raw(SdkWorldSaveMeta* meta)
{
    (void)sdk_world_load_meta_file_internal(meta, 0);
}

int sdk_world_target_resolve(const char* world_id,
                             const char* world_dir,
                             SdkWorldTarget* out_target)
{
    char resolved_dir[MAX_PATH];
    const char* resolved_folder_id;

    if (!out_target) return 0;
    memset(out_target, 0, sizeof(*out_target));

    if (world_id && world_id[0]) {
        strncpy_s(out_target->folder_id, sizeof(out_target->folder_id), world_id, _TRUNCATE);
        if (!sdk_world_build_save_dir_path(out_target->folder_id,
                                           out_target->world_dir,
                                           sizeof(out_target->world_dir))) {
            return 0;
        }
        if (!sdk_world_build_meta_file_path(out_target->folder_id,
                                            out_target->meta_path,
                                            sizeof(out_target->meta_path))) {
            return 0;
        }
        if (!sdk_world_build_save_file_path(out_target->folder_id,
                                            out_target->save_path,
                                            sizeof(out_target->save_path))) {
            return 0;
        }
        sdk_world_fill_target_state(out_target);
        return 1;
    }

    if (!world_dir || !world_dir[0]) return 0;

    if (sdk_world_is_save_or_meta_path(world_dir)) {
        if (!sdk_world_path_parent_dir(world_dir, resolved_dir, sizeof(resolved_dir))) {
            return 0;
        }
    } else {
        strncpy_s(resolved_dir, sizeof(resolved_dir), world_dir, _TRUNCATE);
    }

    resolved_folder_id = sdk_world_path_basename(resolved_dir);
    if (!resolved_folder_id || !resolved_folder_id[0]) return 0;

    strncpy_s(out_target->folder_id, sizeof(out_target->folder_id), resolved_folder_id, _TRUNCATE);
    strncpy_s(out_target->world_dir, sizeof(out_target->world_dir), resolved_dir, _TRUNCATE);
    snprintf(out_target->meta_path, sizeof(out_target->meta_path), "%s\\%s",
             out_target->world_dir, SDK_WORLD_SAVE_META_NAME);
    snprintf(out_target->save_path, sizeof(out_target->save_path), "%s\\%s",
             out_target->world_dir, SDK_WORLD_SAVE_DATA_NAME);
    sdk_world_fill_target_state(out_target);
    return 1;
}

int sdk_world_target_load_meta(const SdkWorldTarget* target, SdkWorldSaveMeta* out_meta)
{
    if (!target || !out_meta || !target->folder_id[0]) return 0;
    memset(out_meta, 0, sizeof(*out_meta));
    strncpy_s(out_meta->folder_id, sizeof(out_meta->folder_id), target->folder_id, _TRUNCATE);
    if (target->save_path[0]) {
        strncpy_s(out_meta->save_path, sizeof(out_meta->save_path), target->save_path, _TRUNCATE);
    }
    return sdk_world_load_meta_file_internal(out_meta, 1);
}

int sdk_world_target_load_meta_raw(const SdkWorldTarget* target, SdkWorldSaveMeta* out_meta)
{
    if (!target || !out_meta || !target->folder_id[0]) return 0;
    memset(out_meta, 0, sizeof(*out_meta));
    strncpy_s(out_meta->folder_id, sizeof(out_meta->folder_id), target->folder_id, _TRUNCATE);
    if (target->save_path[0]) {
        strncpy_s(out_meta->save_path, sizeof(out_meta->save_path), target->save_path, _TRUNCATE);
    }
    return sdk_world_load_meta_file_internal(out_meta, 0);
}

int sdk_world_create(const SdkWorldCreateRequest* request, SdkWorldCreateResult* out_result)
{
    SdkWorldCreateResult local_result;
    SdkWorldCreateRequest local_request;
    char folder_id[64];
    char debug_log_path[MAX_PATH];
    SdkWorldSaveMeta existing_meta;

    if (!request) return 0;
    memset(&local_result, 0, sizeof(local_result));
    memset(&local_request, 0, sizeof(local_request));
    local_request = *request;

    if (local_request.seed == 0u) {
        local_request.seed = sdk_world_generate_new_seed();
    }
    if (local_request.render_distance_chunks <= 0) {
        local_request.render_distance_chunks = k_render_distance_presets[2];
    }
    local_request.coordinate_system = sdk_world_resolve_request_coordinate_system(&local_request);
    local_request.superchunks_enabled =
        sdk_world_coordinate_system_uses_superchunks(
            (SdkWorldCoordinateSystem)local_request.coordinate_system) ? true : false;

    if (local_request.output_dir[0]) {
        if (!sdk_world_target_resolve(NULL, local_request.output_dir, &local_result.target)) {
            return 0;
        }
        strncpy_s(folder_id, sizeof(folder_id), local_result.target.folder_id, _TRUNCATE);
    } else if (local_request.folder_id[0]) {
        if (!sdk_world_target_resolve(local_request.folder_id, NULL, &local_result.target)) {
            return 0;
        }
        strncpy_s(folder_id, sizeof(folder_id), local_request.folder_id, _TRUNCATE);
    } else {
        if (!sdk_world_next_available_folder_id(folder_id, sizeof(folder_id))) {
            return 0;
        }
        if (!sdk_world_target_resolve(folder_id, NULL, &local_result.target)) {
            return 0;
        }
    }

    if (local_result.target.directory_exists && local_request.allow_existing) {
        local_result.existed = true;
        if (sdk_world_target_load_meta(&local_result.target, &existing_meta)) {
            local_result.meta = existing_meta;
        } else {
            memset(&local_result.meta, 0, sizeof(local_result.meta));
            strncpy_s(local_result.meta.folder_id, sizeof(local_result.meta.folder_id), folder_id, _TRUNCATE);
            sdk_world_default_display_name(folder_id,
                                           local_result.meta.display_name,
                                           sizeof(local_result.meta.display_name));
            sdk_world_meta_set_defaults(&local_result.meta);
            local_result.meta.seed = local_request.seed;
            local_result.meta.render_distance_chunks =
                sdk_world_clamp_render_distance_chunks(local_request.render_distance_chunks);
            local_result.meta.spawn_mode = local_request.spawn_mode;
            strncpy_s(local_result.meta.save_path,
                      sizeof(local_result.meta.save_path),
                      local_result.target.save_path,
                      _TRUNCATE);
        }
        if (out_result) *out_result = local_result;
        return 1;
    }

    if (local_result.target.directory_exists && !local_request.allow_existing) {
        return 0;
    }

    if (!sdk_world_ensure_directory_exists(local_result.target.world_dir)) {
        return 0;
    }

    strncpy_s(local_request.folder_id, sizeof(local_request.folder_id), folder_id, _TRUNCATE);
    sdk_world_apply_create_request_to_meta(&local_request, &local_result.meta);
    if (!local_result.meta.display_name[0]) {
        sdk_world_default_display_name(folder_id,
                                       local_result.meta.display_name,
                                       sizeof(local_result.meta.display_name));
    }
    strncpy_s(local_result.meta.folder_id, sizeof(local_result.meta.folder_id), folder_id, _TRUNCATE);
    strncpy_s(local_result.meta.save_path,
              sizeof(local_result.meta.save_path),
              local_result.target.save_path,
              _TRUNCATE);
    local_result.meta.has_save_data = sdk_world_file_exists_a(local_result.target.save_path) ? true : false;
    local_result.meta.last_write_time = GetTickCount64();
    sdk_world_save_meta_file(&local_result.meta);
    if (sdk_world_build_debug_log_file_path(folder_id, debug_log_path, sizeof(debug_log_path))) {
        FILE* debug_file = fopen(debug_log_path, "ab");
        if (debug_file) {
            fclose(debug_file);
        }
    }
    sdk_world_fill_target_state(&local_result.target);

    if (out_result) *out_result = local_result;
    return 1;
}
