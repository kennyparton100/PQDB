#include "sdk_frontend_internal.h"

static void apply_current_world_create_settings(SdkWorldCreateRequest* request,
                                                int use_current_seed,
                                                int use_current_render_distance)
{
    /* Applies current UI settings to world create request */
    if (!request) return;

    request->settlements_enabled = g_world_create_settlements_enabled ? true : false;
    request->construction_cells_enabled = g_world_create_construction_cells_enabled ? true : false;
    request->coordinate_system = (uint8_t)g_world_create_coordinate_system;
    request->superchunks_enabled = g_world_create_superchunks_enabled ? true : false;
    request->superchunk_chunk_span =
        (g_world_create_superchunk_chunk_span > 0) ? g_world_create_superchunk_chunk_span : 16;
    request->walls_enabled = g_world_create_walls_enabled ? true : false;
    request->wall_grid_size = g_world_create_wall_grid_size;
    request->wall_grid_offset_x = g_world_create_wall_grid_offset_x;
    request->wall_grid_offset_z = g_world_create_wall_grid_offset_z;
    request->wall_thickness_blocks = g_world_create_wall_thickness_blocks;
    request->wall_rings_shared = g_world_create_wall_rings_shared ? true : false;
    request->spawn_mode = g_world_create_spawn_type;

    if (use_current_seed) {
        request->seed = g_world_create_seed;
    }

    if (use_current_render_distance) {
        request->render_distance_chunks =
            sdk_world_clamp_render_distance_chunks(g_world_create_render_distance);
    }
}

int build_world_save_dir_path(const char* folder_id, char* out_path, size_t out_path_len)
{
    /* Builds directory path for world save folder */
    return sdk_world_build_save_dir_path(folder_id, out_path, out_path_len);
}

int build_world_save_file_path(const char* folder_id, char* out_path, size_t out_path_len)
{
    /* Builds file path for world save JSON */
    return sdk_world_build_save_file_path(folder_id, out_path, out_path_len);
}

int build_world_meta_file_path(const char* folder_id, char* out_path, size_t out_path_len)
{
    /* Builds file path for world meta JSON */
    return sdk_world_build_meta_file_path(folder_id, out_path, out_path_len);
}

void save_world_meta_file(const SdkWorldSaveMeta* meta)
{
    /* Saves world metadata to file */
    sdk_world_save_meta_file(meta);
}

void load_world_meta_file(SdkWorldSaveMeta* meta)
{
    /* Loads world metadata from file */
    sdk_world_load_meta_file(meta);
}

int compare_world_save_meta_desc(const void* a, const void* b)
{
    /* Compares world saves by last write time descending, then folder ID */
    const SdkWorldSaveMeta* lhs = (const SdkWorldSaveMeta*)a;
    const SdkWorldSaveMeta* rhs = (const SdkWorldSaveMeta*)b;
    if (lhs->last_write_time < rhs->last_write_time) return 1;
    if (lhs->last_write_time > rhs->last_write_time) return -1;
    return strcmp(lhs->folder_id, rhs->folder_id);
}

void refresh_world_save_list(void)
{
    /* Scans world save directory and populates save list, sorted by time */
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char pattern[MAX_PATH];

    g_world_save_count = 0;
    g_frontend_refresh_pending = false;
    if (!sdk_world_ensure_directory_exists(SDK_WORLD_SAVE_ROOT)) return;

    if (snprintf(pattern, sizeof(pattern), "%s\\*", SDK_WORLD_SAVE_ROOT) <= 0) return;
    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) return;

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        if (g_world_save_count >= SDK_WORLD_SAVE_MAX) break;

        {
            SdkWorldSaveMeta* meta = &g_world_saves[g_world_save_count];
            DWORD save_attrs;
            memset(meta, 0, sizeof(*meta));
            strcpy_s(meta->folder_id, sizeof(meta->folder_id), find_data.cFileName);
            strcpy_s(meta->display_name, sizeof(meta->display_name), find_data.cFileName);
            build_world_save_file_path(meta->folder_id, meta->save_path, sizeof(meta->save_path));
            meta->last_write_time = filetime_to_u64(find_data.ftLastWriteTime);
            load_world_meta_file(meta);
            save_attrs = GetFileAttributesA(meta->save_path);
            meta->has_save_data = (save_attrs != INVALID_FILE_ATTRIBUTES &&
                                   (save_attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
            if (meta->display_name[0] == '\0') {
                strcpy_s(meta->display_name, sizeof(meta->display_name), meta->folder_id);
            }
            g_world_save_count++;
        }
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);

    if (g_world_save_count > 1) {
        qsort(g_world_saves, (size_t)g_world_save_count, sizeof(g_world_saves[0]), compare_world_save_meta_desc);
    }

    if (g_world_save_selected < 0) g_world_save_selected = 0;
    if (g_world_save_count <= 0) {
        g_world_save_selected = 0;
    } else if (g_world_save_selected >= g_world_save_count) {
        g_world_save_selected = g_world_save_count - 1;
    }
}

void migrate_legacy_world_save_if_needed(void)
{
    /* Migrates legacy world.json to new folder structure if found */
    DWORD legacy_attrs;
    char legacy_path[MAX_PATH];
    char dir_path[MAX_PATH];
    SdkWorldSaveMeta meta;

    legacy_path[0] = '\0';
    strcpy_s(legacy_path, sizeof(legacy_path), "world.json");
    legacy_attrs = GetFileAttributesA(legacy_path);
    if (legacy_attrs == INVALID_FILE_ATTRIBUTES || (legacy_attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) return;
    if (g_world_save_count > 0) return;

    memset(&meta, 0, sizeof(meta));
    strcpy_s(meta.folder_id, sizeof(meta.folder_id), "world_001");
    strcpy_s(meta.display_name, sizeof(meta.display_name), "MIGRATED WORLD");
    meta.seed = 0u;
    if (!build_world_save_dir_path(meta.folder_id, dir_path, sizeof(dir_path))) return;
    if (!sdk_world_ensure_directory_exists(dir_path)) return;
    if (!build_world_save_file_path(meta.folder_id, meta.save_path, sizeof(meta.save_path))) return;
    if (!CopyFileA(legacy_path, meta.save_path, TRUE)) return;
    meta.has_save_data = true;
    save_world_meta_file(&meta);
    g_frontend_refresh_pending = true;
}

uint32_t generate_new_world_seed(void)
{
    /* Generates random seed for new world */
    return sdk_world_generate_new_seed();
}

int create_new_world_save(SdkWorldSaveMeta* out_meta)
{
    /* Creates new world with random seed, fills out_meta on success */
    SdkWorldCreateRequest request;
    SdkWorldCreateResult result;

    memset(&request, 0, sizeof(request));
    request.seed = sdk_world_generate_new_seed();
    request.render_distance_chunks =
        sdk_chunk_manager_radius_from_grid_size(g_graphics_settings.chunk_grid_size);
    apply_current_world_create_settings(&request, 0, 0);
    if (!sdk_world_create(&request, &result)) return 0;
    if (out_meta) *out_meta = result.meta;
    g_frontend_refresh_pending = true;
    return 1;
}

int create_new_world_save_with_settings(SdkWorldSaveMeta* out_meta)
{
    /* Creates new world with current UI settings, fills out_meta on success */
    SdkWorldCreateRequest request;
    SdkWorldCreateResult result;

    memset(&request, 0, sizeof(request));
    request.render_distance_chunks =
        sdk_world_clamp_render_distance_chunks(g_world_create_render_distance);
    apply_current_world_create_settings(&request, 1, 1);
    if (!sdk_world_create(&request, &result)) {
        sdk_load_trace_note("world_create_failed", "sdk_world_create returned false");
        return 0;
    }
    if (out_meta) *out_meta = result.meta;
    sdk_load_trace_bind_world(result.target.folder_id, result.target.world_dir);
    sdk_load_trace_note("world_create_meta_written", result.target.meta_path);
    g_frontend_refresh_pending = true;
    return 1;
}

void sync_active_world_meta(uint32_t world_seed)
{
    /* Updates meta file for active world with current seed and name */
    SdkWorldSaveMeta meta;

    if (!g_sdk.world_save_id[0]) return;
    memset(&meta, 0, sizeof(meta));
    strcpy_s(meta.folder_id, sizeof(meta.folder_id), g_sdk.world_save_id);
    load_world_meta_file(&meta);
    strcpy_s(meta.display_name, sizeof(meta.display_name),
             g_sdk.world_save_name[0] ? g_sdk.world_save_name : g_sdk.world_save_id);
    meta.seed = world_seed;
    if (meta.render_distance_chunks <= 0) {
        meta.render_distance_chunks = sdk_chunk_manager_radius_from_grid_size(g_graphics_settings.chunk_grid_size);
    }
    build_world_save_file_path(meta.folder_id, meta.save_path, sizeof(meta.save_path));
    meta.has_save_data = true;
    save_world_meta_file(&meta);
}
