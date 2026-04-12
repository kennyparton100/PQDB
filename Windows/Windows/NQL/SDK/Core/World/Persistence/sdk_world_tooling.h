#ifndef SDK_WORLD_TOOLING_H
#define SDK_WORLD_TOOLING_H

#include "../../sdk_types.h"
#include "../Superchunks/Config/sdk_superchunk_config.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SDK_START_MENU_WORLD_NAME_MAX
#define SDK_START_MENU_WORLD_NAME_MAX 40
#endif

#ifndef SDK_WORLD_SAVE_ROOT
#define SDK_WORLD_SAVE_ROOT "WorldSaves"
#endif

#ifndef SDK_WORLD_SAVE_META_NAME
#define SDK_WORLD_SAVE_META_NAME "meta.txt"
#endif

#ifndef SDK_WORLD_SAVE_DATA_NAME
#define SDK_WORLD_SAVE_DATA_NAME "save.json"
#endif

#ifndef SDK_WORLD_DEBUG_LOG_NAME
#define SDK_WORLD_DEBUG_LOG_NAME "debug.log"
#endif

typedef struct {
    char folder_id[64];
    char display_name[SDK_START_MENU_WORLD_NAME_MAX];
    char save_path[MAX_PATH];
    uint32_t seed;
    int render_distance_chunks;
    int spawn_mode;
    uint64_t last_write_time;
    bool has_save_data;
    bool settlements_enabled;
    bool construction_cells_enabled;
    uint8_t coordinate_system; /* SdkWorldCoordinateSystem */
    bool superchunks_enabled;
    int superchunk_chunk_span;
    bool walls_enabled;
    int wall_grid_size; /* Wall ring size for detached/grid wall systems */
    int wall_grid_offset_x;
    int wall_grid_offset_z;
    int wall_thickness_blocks;
    bool wall_rings_shared;
} SdkWorldSaveMeta;

typedef struct {
    char folder_id[64];
    char display_name[SDK_START_MENU_WORLD_NAME_MAX];
    char output_dir[MAX_PATH];
    uint32_t seed;
    int render_distance_chunks;
    int spawn_mode;
    bool settlements_enabled;
    bool construction_cells_enabled;
    uint8_t coordinate_system; /* SdkWorldCoordinateSystem */
    bool superchunks_enabled;
    int superchunk_chunk_span;
    bool walls_enabled;
    int wall_grid_size; /* Wall ring size for detached/grid wall systems */
    int wall_grid_offset_x;
    int wall_grid_offset_z;
    int wall_thickness_blocks;
    bool wall_rings_shared;
    bool allow_existing;
} SdkWorldCreateRequest;

typedef struct {
    char folder_id[64];
    char world_dir[MAX_PATH];
    char meta_path[MAX_PATH];
    char save_path[MAX_PATH];
    bool directory_exists;
    bool meta_exists;
    bool save_exists;
} SdkWorldTarget;

typedef struct {
    SdkWorldTarget target;
    SdkWorldSaveMeta meta;
    bool existed;
} SdkWorldCreateResult;

int sdk_world_clamp_render_distance_chunks(int radius);
uint32_t sdk_world_generate_new_seed(void);
int sdk_world_ensure_directory_exists(const char* path);
void sdk_world_copy_ascii_trimmed(char* dst, size_t dst_len, const char* src);

void sdk_world_meta_normalize(SdkWorldSaveMeta* meta);
void sdk_world_apply_create_request_to_meta(const SdkWorldCreateRequest* request,
                                            SdkWorldSaveMeta* meta);
void sdk_world_meta_to_world_desc(const SdkWorldSaveMeta* meta, SdkWorldDesc* out_world_desc);
void sdk_world_meta_to_superchunk_config(const SdkWorldSaveMeta* meta,
                                         SdkSuperchunkConfig* out_config);

int sdk_world_build_save_dir_path(const char* folder_id, char* out_path, size_t out_path_len);
int sdk_world_build_save_file_path(const char* folder_id, char* out_path, size_t out_path_len);
int sdk_world_build_meta_file_path(const char* folder_id, char* out_path, size_t out_path_len);
int sdk_world_build_debug_log_file_path(const char* folder_id, char* out_path, size_t out_path_len);

void sdk_world_save_meta_file(const SdkWorldSaveMeta* meta);
void sdk_world_load_meta_file(SdkWorldSaveMeta* meta);
void sdk_world_load_meta_file_raw(SdkWorldSaveMeta* meta);

int sdk_world_target_resolve(const char* world_id,
                             const char* world_dir,
                             SdkWorldTarget* out_target);
int sdk_world_target_load_meta(const SdkWorldTarget* target, SdkWorldSaveMeta* out_meta);
int sdk_world_target_load_meta_raw(const SdkWorldTarget* target, SdkWorldSaveMeta* out_meta);

int sdk_world_create(const SdkWorldCreateRequest* request, SdkWorldCreateResult* out_result);

#ifdef __cplusplus
}
#endif

#endif
