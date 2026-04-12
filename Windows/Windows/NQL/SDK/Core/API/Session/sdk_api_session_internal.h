#ifndef SDK_API_SESSION_INTERNAL_H
#define SDK_API_SESSION_INTERNAL_H

/* Include base SDK headers */
#include "../Internal/sdk_api_internal.h"
#include "../../World/Worldgen/Column/sdk_worldgen_column_internal.h"
#include "../../World/Superchunks/Config/sdk_superchunk_config.h"

/* ============================================================================
 * Global State - Async World Release
 * ============================================================================ */
extern int g_async_world_release_slot_cursor;
extern int g_async_world_release_total;
extern int g_async_world_release_freed;
extern int g_async_world_release_force_abandon;

/* ============================================================================
 * Global State - Editor Input
 * ============================================================================ */
extern bool g_editor_play_was_down;
extern bool g_editor_prev_was_down;
extern bool g_editor_next_was_down;

/* ============================================================================
 * Constants
 * ============================================================================ */
#define SDK_EDITOR_PREVIEW_TICK_FRAMES 20

/* ============================================================================
 * Forward Declarations - Core Session Functions
 * ============================================================================ */
void gen_stage_begin(const char* name);
void gen_stage_end(int chunks_processed, int data_size_kb);
void build_and_upload_chunk_sync(SdkChunk* chunk);
int wall_corner_chunks_need_mesh_count(void);
int build_wall_corner_meshes_sync(int max_chunks);
void process_streamed_chunk_results_with_budget(int max_results, float budget_ms);

/* Editor state */
bool editor_session_active(void);
bool editor_block_in_bounds(int x, int y, int z);

/* ============================================================================
 * Forward Declarations - Map Functions
 * ============================================================================ */
int map_queue_superchunk_map_tile_if_needed(int origin_x,
                                            int origin_z,
                                            bool allow_generation,
                                            bool load_ready_from_disk,
                                            SuperchunkMapCacheEntry** out_ready);
int build_superchunk_map_tile_pixels(SdkMapWorker* worker,
                                       int origin_x,
                                       int origin_z,
                                       uint32_t* out_pixels,
                                       int* out_chunk_count);
SuperchunkMapCacheEntry* request_superchunk_map_cache_entry_async(int origin_x, int origin_z, bool allow_generation);
void pump_superchunk_map_scheduler_offline_bulk(int max_jobs);

/* ============================================================================
 * Forward Declarations - Map Render Functions
 * ============================================================================ */
uint32_t map_shade_color(uint32_t color, float shade);
uint32_t map_opaque_color(uint32_t color);
uint32_t map_blend_color(uint32_t base_color, uint32_t overlay_color, float overlay_mix);
const char* map_build_kind_name(uint8_t build_kind);
BlockType map_ground_block_for_profile(const SdkTerrainColumnProfile* profile);
uint32_t map_color_for_surface_state(BlockType land_block,
                                     int center_height,
                                     int west_height,
                                     int east_height,
                                     int north_height,
                                     int south_height,
                                     int sea_level,
                                     int water_depth,
                                     int dry_count,
                                     int water_count,
                                     int submerged_count,
                                     int seasonal_ice_count,
                                     int perennial_ice_count,
                                     int alpine_bonus);

/* ============================================================================
 * Forward Declarations - Debug Compare Functions
 * ============================================================================ */
void map_debug_compare_shutdown(void);
int map_debug_compare_tile(int origin_x, int origin_z, uint16_t* out_diff_pixels);

/* ============================================================================
 * Struct Definitions - Map Tile Data
 * ============================================================================ */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t build_kind;
    uint32_t seed;
    int32_t origin_x;
    int32_t origin_z;
    uint32_t dim;
} SdkPersistedMapTileHeader;

typedef enum {
    SDK_MAP_EXACT_COLUMN_VOID = 0,
    SDK_MAP_EXACT_COLUMN_DRY,
    SDK_MAP_EXACT_COLUMN_OPEN_WATER,
    SDK_MAP_EXACT_COLUMN_SEASONAL_ICE,
    SDK_MAP_EXACT_COLUMN_PERENNIAL_ICE
} SdkMapExactColumnKind;

typedef struct {
    SdkMapExactColumnKind kind;
    BlockType ground_block;
    uint16_t land_height;
    uint16_t water_height;
    uint16_t water_depth;
} SdkMapExactColumnSample;

/* ============================================================================
 * Struct Definitions - Debug Compare State
 * ============================================================================ */
typedef struct {
    bool initialized;
    bool valid;
    uint32_t world_seed;
    char world_save_id[64];
    SdkWorldDesc world_desc;
    int origin_x;
    int origin_z;
    uint16_t diff_pixels;
    SdkMapWorker worker;
    uint32_t exact_pixels[HUD_MAP_PIXEL_COUNT];
    uint32_t fallback_pixels[HUD_MAP_PIXEL_COUNT];
} SdkMapDebugCompareState;

/* ============================================================================
 * Global State - Map Debug Compare
 * ============================================================================ */
extern SdkMapDebugCompareState g_map_debug_compare;

/* ============================================================================
 * Map Cache Identity
 * ============================================================================ */
void map_cache_identity(const char** out_world_save_id, uint32_t* out_world_seed);

/* ============================================================================
 * Map Tile Path Building
 * ============================================================================ */
int build_superchunk_map_tile_cache_path_for_identity(const char* world_save_id,
                                                       uint32_t world_seed,
                                                       int origin_x,
                                                       int origin_z,
                                                       char* out_path,
                                                       size_t out_path_len);
int superchunk_map_tile_header_valid(const SdkPersistedMapTileHeader* header,
                                     uint32_t expected_build_kind,
                                     uint32_t expected_seed,
                                     int origin_x,
                                     int origin_z);
int build_superchunk_map_tile_cache_path(int origin_x, int origin_z, char* out_path, size_t out_path_len);

/* ============================================================================
 * Map Tile Disk I/O
 * ============================================================================ */
int load_superchunk_map_tile_from_disk_for_identity(const char* world_save_id,
                                                    uint32_t world_seed,
                                                    int origin_x,
                                                    int origin_z,
                                                    uint32_t* out_pixels);
int load_superchunk_map_tile_from_disk(int origin_x, int origin_z, uint32_t* out_pixels);
int superchunk_map_tile_exists_on_disk_for_identity(const char* world_save_id,
                                                       uint32_t world_seed,
                                                       int origin_x,
                                                       int origin_z);
void save_superchunk_map_tile_to_disk_for_identity(const char* world_save_id,
                                                    uint32_t world_seed,
                                                    int origin_x,
                                                    int origin_z,
                                                    uint32_t build_kind,
                                                    const uint32_t* pixels);
void save_superchunk_map_tile_to_disk(int origin_x, int origin_z, const uint32_t* pixels);

/* ============================================================================
 * Map Scheduler Functions
 * ============================================================================ */
int map_queue_index(int head, int count, int capacity);
int choose_map_worker_count(void);
int map_scheduler_building_jobs_locked(const SdkMapSchedulerInternal* sched);
int map_floor_div_local_coord(int value, int denom);
int map_macro_tile_coord_from_origin(int origin_coord);
int map_entry_matches_macro_tile(const SuperchunkMapCacheEntry* entry, int macro_tile_x, int macro_tile_z);
int map_macro_tile_claimed_by_other_worker_locked(const SdkMapSchedulerInternal* sched,
                                                   const SdkMapWorker* worker,
                                                   int macro_tile_x,
                                                   int macro_tile_z);
int map_job_distance_from_worker(const SdkMapWorker* worker,
                                  const SuperchunkMapCacheEntry* entry);
int map_take_next_job_locked(SdkMapSchedulerInternal* sched, SdkMapWorker* worker);
int map_drop_queued_page_tiles_locked(SdkMapSchedulerInternal* sched);
int map_make_jobs_for_macro_tile_locked(SdkMapSchedulerInternal* sched,
                                        int macro_tile_x,
                                        int macro_tile_z,
                                        int* out_page_jobs_added);
int map_make_jobs_for_current_camera_locked(SdkMapSchedulerInternal* sched, int page_cursor);
int map_make_jobs_for_ring_locked(SdkMapSchedulerInternal* sched, int ring);
void map_make_jobs_for_bulk_ring(SdkMapSchedulerInternal* sched, int ring);
void map_reset_bulk_state_locked(SdkMapSchedulerInternal* sched);

/* ============================================================================
 * Map Scheduler Lifecycle
 * ============================================================================ */
void request_shutdown_superchunk_map_scheduler(void);
int poll_shutdown_superchunk_map_scheduler(void);
void shutdown_superchunk_map_scheduler(void);
void get_superchunk_map_scheduler_stats(SdkMapSchedulerStats* out_stats);
void push_superchunk_map_ui(float world_x, float world_z);

/* ============================================================================
 * Map Tile Building Functions
 * ============================================================================ */
static void map_exact_reset_worker_scratch(SdkMapWorker* worker);
static BlockType map_exact_dominant_block(const uint16_t* histogram, int cell_index);
static int map_exact_effective_height(const SdkMapExactCellAccumulator* cell);
static int map_exact_check_scheduler_running(const SdkMapSchedulerInternal* sched);
static int map_exact_classify_surface_column(const SdkWorldGenSurfaceColumn* column,
                                            const SdkTerrainColumnProfile* profile,
                                            SdkMapExactColumnSample* out_sample);
static void map_exact_accumulate_column(SdkMapWorker* worker,
                                        int cell_x,
                                        int cell_z,
                                        const SdkMapExactColumnSample* sample);
static uint32_t map_color_for_exact_cells(const SdkMapExactCellAccumulator* center,
                                          const SdkMapExactCellAccumulator* west,
                                          const SdkMapExactCellAccumulator* east,
                                          const SdkMapExactCellAccumulator* north,
                                          const SdkMapExactCellAccumulator* south,
                                          const uint16_t* dry_hist,
                                          const uint16_t* submerged_hist,
                                          int cell_index,
                                          int sea_level);


/* ============================================================================
 * Debug Compare Internal
 * ============================================================================ */
int map_debug_compare_ensure_worker(void);

/* ============================================================================
 * Core Session - Generation Summary
 * ============================================================================ */
typedef struct WorldGenStageStats {
    const char* name;
    ULONGLONG start_ms;
    ULONGLONG end_ms;
    int chunks_processed;
    int data_size_kb;
} WorldGenStageStats;

#define MAX_GEN_STAGES 16

int world_generation_summary_active(void);
void dismiss_world_generation_summary(void);
void present_world_generation_summary(void);
void world_generation_session_step(float progress, const char* description, int force);

/* ============================================================================
 * Core Session - World Generation Helpers
 * ============================================================================ */
int current_new_world_spawn_mode(const SdkWorldSaveMeta* selected_world);
int current_world_render_distance_chunks(const SdkWorldSaveMeta* selected_world);
static int spawn_top_from_profile(const SdkTerrainColumnProfile* profile);
static int world_generation_session_overlay_active(void);

/* ============================================================================
 * Core Session - Async Return & State
 * ============================================================================ */
static void present_async_return_status(float progress, const char* status);
void reset_async_world_release_state(void);
int release_world_geometry_step(int max_chunks_per_frame, float* out_progress);

/* ============================================================================
 * Core Session - Persisted State
 * ============================================================================ */
void capture_persisted_state(SdkPersistedState* out_state);
void apply_persisted_state(const SdkPersistedState* state);

/* ============================================================================
 * Core Session - Graphics & Atmosphere
 * ============================================================================ */
void apply_graphics_atmosphere(float ambient, float sky_r, float sky_g, float sky_b);

/* ============================================================================
 * Core Session - Window & Settings
 * ============================================================================ */
void update_window_title_for_test_flight(void);
void rebuild_chunk_grid_for_current_camera(int new_grid_size);
void save_graphics_settings_now(void);
void tick_startup_safe_mode(void);
bool startup_safe_mode_active(void);

/* ============================================================================
 * Core Session - Editor Helpers
 * ============================================================================ */
bool editor_voxel_buffer_has_blocks(const uint8_t* voxels, size_t voxel_count);
int find_character_asset_index_by_id_local(const char* asset_id);
int find_animation_asset_index_by_id_local(const char* asset_id);
int find_prop_asset_index_by_id_local(const char* asset_id);
int find_block_asset_index_by_id_local(const char* asset_id);
int find_item_asset_index_by_id_local(const char* asset_id);
int find_particle_effect_asset_index_by_id_local(const char* asset_id);
static SdkChunk* create_editor_chunk(int cx, int cz);
static void fill_editor_floor(SdkChunk* chunk, int floor_y);
void reset_editor_chunk_contents(SdkChunk* chunk);
int count_tracked_dirty_chunks_complete(const int* tracked_cx, const int* tracked_cz, int tracked_count);

/* ============================================================================
 * Core Session - Editor
 * ============================================================================ */
void start_editor_session(SdkSessionKind kind, const char* character_id, const char* animation_id);
void exit_editor_to_start_menu(void);
void tick_editor_session(void);
void save_editor_session_assets(void);
void build_editor_ui(SdkEditorUI* out_ui);
bool editor_block_in_bounds(int wx, int wy, int wz);
int editor_preview_chunk_cx(void);
int editor_frame_chunk_cx(int frame);
void sync_animation_preview_chunk(bool advance_frame);
void editor_prefill_hotbar(void);
void capture_editor_chunk_voxels(const SdkChunk* chunk, uint8_t* out_voxels);
void reset_editor_chunk_contents(SdkChunk* chunk);
void stamp_editor_voxels_into_chunk(SdkChunk* chunk, const uint8_t* voxels);
void finish_editor_return_to_frontend(SdkSessionKind previous_kind, const char* character_id, const char* animation_id);

/* ============================================================================
 * Core Session - Editor Start Functions
 * ============================================================================ */
int start_character_editor_session(const SdkCharacterAssetMeta* character);
int start_animation_editor_session(const SdkCharacterAssetMeta* character,
                                   const SdkAnimationAssetMeta* animation);
int start_prop_editor_session(const SdkPropAssetMeta* prop_asset);
int start_block_editor_session(const SdkBlockAssetMeta* block_asset);
int start_item_editor_session(const SdkItemAssetMeta* item_asset);
int start_particle_effect_editor_session(const SdkParticleEffectAssetMeta* particle_effect_asset);

/* ============================================================================
 * Core Session - Gameplay Character Mesh
 * ============================================================================ */
void rebuild_selected_gameplay_character_mesh(void);
void select_gameplay_character_index(int character_index, bool persist_now);

/* ============================================================================
 * Core Session - Character
 * ============================================================================ */
void refresh_animation_assets_for_selected_character(void);

/* ============================================================================
 * Core Session - Stream Budget
 * ============================================================================ */
float stream_adopt_budget_ms(void);
int stream_upload_limit_per_frame(void);
float stream_gpu_upload_budget_ms(void);
int dirty_remesh_jobs_per_frame(void);
int initial_sync_bootstrap_distance(void);

/* ============================================================================
 * Bootstrap Wall Support Types
 * ============================================================================ */
typedef struct {
    int total;
    int loaded;
    int ready;
} SdkBootstrapWallSideHealth;

/* ============================================================================
 * Core Session - Spawn
 * ============================================================================ */
void choose_random_spawn_fast(SdkWorldGen* wg);
void choose_center_spawn(SdkWorldGen* wg);
void choose_safe_spawn(SdkWorldGen* wg);
int spawn_floor_div_i(int value, int denom);
int spawn_floor_mod_i(int value, int denom);

/* ============================================================================
 * Bootstrap Functions
 * ============================================================================ */
void mark_all_loaded_chunks_dirty(void);
int bootstrap_target_is_sync(const SdkChunkResidencyTarget* target);
void bootstrap_nearby_visible_chunks_sync(void);
void evict_undesired_loaded_chunks(void);
int adopt_streamed_chunk_result(const SdkChunkBuildResult* result);
int adopt_streamed_remesh_result(const SdkChunkBuildResult* result);
void process_streamed_chunk_results(int max_results);
void process_pending_chunk_gpu_uploads(int max_chunks, float budget_ms);
void process_dirty_chunk_mesh_uploads(int max_chunks);
void rebuild_loaded_dirty_chunks_sync(int max_chunks);
void persist_loaded_chunks(void);
int bootstrap_visible_chunks_sync(void);

/* Wall support functions */
int target_matches_active_wall_stage(const SdkChunkResidencyTarget* target, SdkActiveWallStage stage);
int target_is_active_superchunk_wall_support(const SdkChunkResidencyTarget* target);
int count_loaded_active_superchunk_wall_support_targets(void);
void collect_active_superchunk_wall_support_health(SdkBootstrapWallSideHealth* west,
                                                    SdkBootstrapWallSideHealth* north,
                                                    SdkBootstrapWallSideHealth* east,
                                                    SdkBootstrapWallSideHealth* south);
int active_wall_stage_ready_count(const SdkActiveWallStage stage);
int active_wall_stage_total(const SdkActiveWallStage stage);
void log_wall_bootstrap_no_progress(int loaded, int total, int pending_jobs, int pending_results);
void log_wall_bootstrap_sync_takeover(int loaded, int total);

/* Finalization functions */
int finalize_active_wall_chunk_sync(SdkChunk* chunk);
int finalize_active_wall_stage_sync(SdkActiveWallStage stage, int desired_chunks);
void process_active_wall_finalization_sync(int max_chunks);
int collect_loaded_dirty_chunks(int* out_cx, int* out_cz, int max_tracked, int* out_far_only, int* out_invalid_dirty);
int load_missing_active_superchunk_wall_support_sync(int max_chunks);
int load_missing_active_wall_stage_sync(SdkActiveWallStage stage, int max_chunks);
size_t stream_upload_byte_budget_per_frame(void);

/* ============================================================================
 * Map Functions
 * ============================================================================ */
int map_queue_index(int head, int count, int capacity);
int choose_map_worker_count(void);
int map_take_next_job_locked(SdkMapSchedulerInternal* sched, SdkMapWorker* worker);
int map_take_next_page_job_locked(SdkMapSchedulerInternal* sched, SdkMapOfflinePageJob* out_job);
int map_drop_queued_page_tiles_locked(SdkMapSchedulerInternal* sched);
void reset_map_bulk_cursor_locked(SdkMapSchedulerInternal* sched);
int build_superchunk_map_tile_pixels_fallback(SdkMapWorker* worker,
                                               int origin_x,
                                               int origin_z,
                                               uint32_t* out_pixels);
int build_superchunk_map_tile_pixels_exact(SdkMapWorker* worker,
                                            int origin_x,
                                            int origin_z,
                                            uint32_t* out_pixels,
                                            int* out_chunk_count);
int build_superchunk_map_tile_pixels(SdkMapWorker* worker,
                                     int origin_x,
                                     int origin_z,
                                     uint32_t* out_pixels,
                                     int* out_chunk_count);
DWORD WINAPI superchunk_map_worker_proc(LPVOID param);
int init_superchunk_map_scheduler(const SdkMapSchedulerConfig* config);
void request_shutdown_superchunk_map_scheduler(void);
int poll_shutdown_superchunk_map_scheduler(void);
void shutdown_superchunk_map_scheduler(void);

/* ============================================================================
 * Map Color Functions
 * ============================================================================ */
uint8_t map_clamp_u8(int value);
uint32_t map_shade_color(uint32_t color, float shade);
static inline int session_map_superchunk_tile_blocks(void)
{
    return SDK_SUPERCHUNK_WALL_PERIOD * CHUNK_WIDTH;
}
int superchunk_origin_for_world(int world_coord);
int map_is_gate_run_coord(int local_coord);
int map_is_wall_band_local(int local_x, int local_z);
uint32_t map_wall_color_for_local(int local_x, int local_z);
BlockType map_bedrock_block_for_profile(const SdkTerrainColumnProfile* profile);
const char* map_build_kind_name(uint8_t build_kind);
uint32_t sdk_map_color_for_profiles(const SdkTerrainColumnProfile* center,
                                    const SdkTerrainColumnProfile* west,
                                    const SdkTerrainColumnProfile* east,
                                    const SdkTerrainColumnProfile* north,
                                    const SdkTerrainColumnProfile* south,
                                    int sea_level);

/* ============================================================================
 * Core Session - State
 * ============================================================================ */
void capture_persisted_state(SdkPersistedState* out_state);
void apply_persisted_state(const SdkPersistedState* state);

/* ============================================================================
 * Core Session - Misc
 * ============================================================================ */
void rebuild_chunk_grid_for_current_camera(int new_grid_size);
void save_graphics_settings_now(void);
void tick_startup_safe_mode(void);
void reset_session_runtime_state(void);
void async_world_release_tick(void);

/* ============================================================================
 * Core Session - Lifecycle
 * ============================================================================ */
void shutdown_world_session(bool save_state);
void begin_async_return_to_start(void);
void update_async_return_to_start(void);
int start_world_session(const SdkWorldSaveMeta* selected_world);
int world_generation_summary_active(void);
void dismiss_world_generation_summary(void);

#endif /* SDK_API_SESSION_INTERNAL_H */
