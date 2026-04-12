#ifndef SDK_FRONTEND_INTERNAL_H
#define SDK_FRONTEND_INTERNAL_H

#include "../API/Internal/sdk_api_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const float k_world_generation_prep_progress_max;
extern uint64_t g_world_map_generation_started_ms;
extern uint64_t g_world_map_generation_last_sample_ms;
extern uint64_t g_world_map_generation_last_completion_ms;
extern int g_world_map_generation_last_sample_tiles;
extern int g_world_map_generation_inflight;
extern int g_world_map_generation_queued_pages;
extern int g_world_map_generation_active_workers;
extern int g_world_map_generation_last_tile_chunks;
extern float g_world_map_generation_tiles_per_sec;
extern float g_world_map_generation_last_save_age_seconds;
extern float g_world_map_generation_last_tile_ms;

extern uint64_t g_world_generation_started_ms;
extern uint64_t g_world_generation_last_sample_ms;
extern int g_world_generation_last_sample_chunks;
extern int g_world_generation_active_workers;
extern float g_world_generation_chunks_per_sec;

void copy_ascii_trimmed(char* dst, size_t dst_len, const char* src);

int build_world_save_dir_path(const char* folder_id, char* out_path, size_t out_path_len);
int build_world_save_file_path(const char* folder_id, char* out_path, size_t out_path_len);
int build_world_meta_file_path(const char* folder_id, char* out_path, size_t out_path_len);
void save_world_meta_file(const SdkWorldSaveMeta* meta);
void load_world_meta_file(SdkWorldSaveMeta* meta);
void refresh_world_save_list(void);
void migrate_legacy_world_save_if_needed(void);
uint32_t generate_new_world_seed(void);
int create_new_world_save(SdkWorldSaveMeta* out_meta);
int create_new_world_save_with_settings(SdkWorldSaveMeta* out_meta);
void sync_active_world_meta(uint32_t world_seed);

void frontend_open_world_create_menu(void);
void frontend_open_world_generating(void);
void frontend_reset_nav_state(void);
void frontend_open_main_menu(void);
void frontend_open_world_select(void);
void frontend_refresh_worlds_if_needed(void);
void set_world_generation_session_progress(float session_progress, const char* status);
void push_start_menu_ui(void);
void present_start_menu_frame(void);
void clear_non_frontend_ui(void);
void frontend_open_character_menu(void);
void frontend_handle_input(void);
void frontend_open_world_actions(void);

int selected_world_is_local_host(void);
int selected_world_can_start_local_host(void);
int selected_world_can_play_local(void);
int selected_world_can_generate_map(void);
void frontend_open_online_menu(void);
void frontend_open_online_edit_server(int server_index);

void begin_async_world_generation(const SdkWorldSaveMeta* meta);
void begin_async_world_map_generation(const SdkWorldSaveMeta* meta);
void begin_async_world_session_load(const SdkWorldSaveMeta* meta);
void begin_async_local_host_start(const SdkWorldSaveMeta* meta, int join_after_start);
void cancel_async_world_generation(void);
void request_async_world_map_generation_stop(void);
void update_async_world_generation(void);
void update_async_world_map_generation(void);
void request_async_world_generation_stop(void);

#ifdef __cplusplus
}
#endif

#endif
