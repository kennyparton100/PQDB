#ifndef SDK_API_INTERNAL_H
#define SDK_API_INTERNAL_H

#include "../sdk_api.h"
#include "../../World/Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../../World/Terrain/sdk_terrain.h"
#include "../../World/Worldgen/sdk_worldgen.h"
#include "../../MeshBuilder/sdk_mesh_builder.h"
#include "../../World/Simulation/sdk_simulation.h"
#include "../../World/Chunks/ChunkStreamer/sdk_chunk_streamer.h"
#include "../../World/Persistence/sdk_persistence.h"
#include "../../World/Persistence/sdk_world_tooling.h"
#include "./sdk_load_trace.h"
#include "../../Automation/sdk_automation.h"
#include "../../Settings/sdk_settings.h"
#include "../../Input/sdk_input.h"
#include "../../Entities/sdk_entity.h"
#include "../../Items/sdk_item.h"
#include "../../World/ConstructionCells/sdk_construction_cells.h"
#include "../../Benchmark/sdk_benchmark.h"
#include "../../Profiler/sdk_profiler.h"
#include "../../World/Buildings/sdk_building_family.h"
#include "../../RoleAssets/sdk_role_assets.h"
#include "../../World/Settlements/Runtime/sdk_settlement_runtime.h"
#include "../../../Platform/Win32/sdk_window.h"
#include "../../../Renderer/d3d12_renderer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "psapi.lib")

#ifdef __cplusplus
extern "C" {
#endif

#define HUD_MAP_CACHE_SLOTS 4096
#define HUD_MAP_PIXEL_COUNT (64 * 64)
#define SDK_MAP_MAX_WORKERS 16
#define SDK_MAP_JOB_CAPACITY 4096
#define SDK_MAP_PAGE_JOB_CAPACITY 256
#define HUD_MAP_VISIBLE_TILE_MAX 4096
#define SDK_MAP_MACRO_TILE_SUPERCHUNK_SPAN \
    ((SDK_WORLDGEN_MACRO_TILE_SIZE * SDK_WORLDGEN_MACRO_CELL_BLOCKS) / SUPERCHUNK_BLOCKS)
#define SDK_MAP_OFFLINE_BATCH_TILE_COUNT \
    (SDK_MAP_MACRO_TILE_SUPERCHUNK_SPAN * SDK_MAP_MACRO_TILE_SUPERCHUNK_SPAN)
#define SDK_MAP_EXACT_TILE_DIM (HUD_MAP_DIM + 2)
#define SDK_MAP_EXACT_TILE_CELL_COUNT (SDK_MAP_EXACT_TILE_DIM * SDK_MAP_EXACT_TILE_DIM)
#define SDK_MAP_RENDER_CACHE_VERSION 3u
#define SDK_MAP_TILE_CACHE_VERSION \
    ((((uint32_t)SDK_PERSISTENCE_WORLDGEN_REVISION) << 16) | SDK_MAP_RENDER_CACHE_VERSION)
#define SDK_OFFLINE_MAP_GENERATION_THREADS 12

#define STARTUP_SAFE_MODE_FRAMES 180

#define PLAYER_MAX_HEALTH 20
#define FALL_DAMAGE_THRESHOLD 3.0f
#define INVINCIBILITY_FRAMES 30
#define DEATH_SCREEN_FRAMES 120

#define PLAYER_MAX_HUNGER 20
#define HUNGER_TICK_INTERVAL 600
#define HUNGER_HEAL_THRESHOLD 18
#define HUNGER_SPRINT_THRESHOLD 6
#define STARVATION_THRESHOLD 0

#define SPRINT_SPEED_MULT 1.5f
#define DOUBLE_TAP_WINDOW 20

#define TEST_FLIGHT_SPEED 0.18f
#define TEST_FLIGHT_SPRINT_MULT 50.0f

#define DAY_LENGTH 24000
#define DAWN_START 0
#define NOON 6000
#define DUSK_START 12000
#define MIDNIGHT 18000
#define AMBIENT_MIN 0.15f
#define AMBIENT_MAX 1.0f

#define CAM_DEFAULT_YAW 0.0f
#define CAM_DEFAULT_PITCH -0.3f

#define PLAYER_WIDTH 0.6f
#define PLAYER_HALF_W 0.3f
#define PLAYER_HEIGHT 1.8f
#define PLAYER_EYE_H 1.62f

#define GRAVITY_ACCEL 0.04f
#define JUMP_VELOCITY 0.35f
#define TERMINAL_VEL 2.0f
#define WALK_SPEED 0.08f
#define CREATIVE_SPAWNER_STACK 32
#define VEHICLE_DISMOUNT_OFFSET 2.0f
#define SUPERCHUNK_BLOCKS 1024
#define HUD_MINIMAP_SUPERCHUNK_GRID 7
#define HUD_MAP_DIM 64
#define HUD_MAP_MAX_ZOOM_TENTHS 1000

#define SDK_CHARACTER_LIBRARY_ROOT "Build\\Characters"
#define SDK_PROP_LIBRARY_ROOT "Build\\Props"
#define SDK_BLOCK_LIBRARY_ROOT "Build\\Blocks"
#define SDK_ITEM_LIBRARY_ROOT "Build\\Items"
#define SDK_PARTICLE_EFFECT_LIBRARY_ROOT "Build\\ParticleEffects"
#define SDK_CHARACTER_META_NAME "meta.txt"
#define SDK_CHARACTER_MODEL_NAME "model.bin"
#define SDK_CHARACTER_ANIMATIONS_DIR "animations"
#define SDK_ASSET_ICON_NAME "icon.bin"
#define SDK_PARTICLE_TIMELINE_NAME "timeline.bin"
#define SDK_CHARACTER_PROFILE_ROOT "Build\\Profiles"
#define SDK_CHARACTER_PROFILE_NAME "default_character.txt"
#define SDK_MAP_TILE_CACHE_ROOT "Build\\MapCache"
#define SDK_MAP_TILE_CACHE_MAGIC 0x314D504Eu
#define HUD_MAP_CELL_BLOCKS (SUPERCHUNK_BLOCKS / HUD_MAP_DIM)
#define SPAWN_SEARCH_CANDIDATES 96
#define SPAWN_SEARCH_MIN_RADIUS 256
#define SPAWN_SEARCH_MAX_RADIUS 16384
#define SPAWN_RELIEF_SAMPLE_STEP 8
#define MAX_SIM_CELLS_PER_FRAME 2048
#define BOUNDARY_WATER_MAX_DEPTH 24
#define BOUNDARY_WATER_MAX_COLUMNS (CHUNK_WIDTH * CHUNK_DEPTH)
#define MAX_SMOKE_CLOUDS 8

#define SDK_WORLD_SAVE_MAX 32
#define SDK_SAVED_SERVER_MAX 32
#define SDK_SERVER_DEFAULT_PORT 28765
#define SDK_CHARACTER_ASSET_MAX 64
#define SDK_ANIMATION_ASSET_MAX 64
#define SDK_PROP_ASSET_MAX 256
#define SDK_BLOCK_ASSET_MAX 64
#define SDK_ITEM_ASSET_MAX 64
#define SDK_PARTICLE_EFFECT_ASSET_MAX 64
#ifndef SDK_FRONTEND_MAIN_OPTION_COUNT
#define SDK_FRONTEND_MAIN_OPTION_COUNT 11
#endif
#define SDK_CHARACTER_ACTION_OPTION_COUNT 5
#define SDK_CHARACTER_ANIMATION_MENU_OPTION_COUNT 3
#define SDK_ANIMATION_ACTION_OPTION_COUNT 4
#define SDK_GENERIC_ASSET_ACTION_OPTION_COUNT 4
#define SDK_ANISOTROPY_PRESET_COUNT 5
#define SDK_EDITOR_DEFAULT_FRAME_COUNT 8
#define SDK_CHARACTER_MODEL_WIDTH 24
#define SDK_CHARACTER_MODEL_DEPTH 16
#define SDK_CHARACTER_MODEL_HEIGHT 64
#define SDK_CHARACTER_MODEL_VOXELS (SDK_CHARACTER_MODEL_WIDTH * SDK_CHARACTER_MODEL_DEPTH * SDK_CHARACTER_MODEL_HEIGHT)
#define SDK_BLOCK_ITEM_MODEL_WIDTH 32
#define SDK_BLOCK_ITEM_MODEL_DEPTH 32
#define SDK_BLOCK_ITEM_MODEL_HEIGHT 32
#define SDK_BLOCK_ITEM_MODEL_VOXELS (SDK_BLOCK_ITEM_MODEL_WIDTH * SDK_BLOCK_ITEM_MODEL_DEPTH * SDK_BLOCK_ITEM_MODEL_HEIGHT)
#define SDK_ASSET_ICON_DIM 32
#define SDK_ASSET_ICON_VOXELS (SDK_ASSET_ICON_DIM * SDK_ASSET_ICON_DIM)
#define SDK_PARTICLE_EFFECT_SLICE_COUNT 8
#define SDK_PARTICLE_EFFECT_SLICE_WIDTH 8
#define SDK_PARTICLE_EFFECT_DEPTH 32
#define SDK_PARTICLE_EFFECT_HEIGHT 32
#define SDK_PARTICLE_EFFECT_TIMELINE_VOXELS (SDK_PARTICLE_EFFECT_SLICE_COUNT * SDK_PARTICLE_EFFECT_SLICE_WIDTH * SDK_PARTICLE_EFFECT_DEPTH * SDK_PARTICLE_EFFECT_HEIGHT)
#define SDK_EDITOR_MAX_CAPTURE_VOXELS SDK_PARTICLE_EFFECT_TIMELINE_VOXELS
#define SDK_PROP_EDITOR_CHUNK_SPAN (SUPERCHUNK_BLOCKS / CHUNK_WIDTH)

#define REACH_DISTANCE 5.0f
#define BREAK_COOLDOWN 10

#define MAX_STATION_STATES 256
#define FURNACE_SMELT_TIME 240
#define CAMPFIRE_COOK_TIME 420

#define SKILLS_MENU_TAP_FRAMES 10
#define GENERAL_SKILL_MAX_RANK 5
#define PROFESSION_SKILL_MAX_RANK 5

typedef struct SdkApiState {
    SdkWindow* window;
    SdkInitDesc desc;
    bool running;
    bool world_session_active;
    bool chunks_initialized;
    bool disable_map_generation_in_gameplay; // Skip map worker threads during fast gameplay
    SdkChunkManager chunk_mgr;
    SdkChunkStreamer chunk_streamer;
    SdkPersistence persistence;
    SdkWorldGen worldgen;
    uint32_t world_seed;
    char world_save_id[64];
    char world_save_name[64];
    SdkEntityList entities;
} SdkApiState;

typedef struct {
    bool valid;
    uint8_t state;
    uint8_t build_kind;
    uint8_t compare_valid;
    uint8_t reserved0;
    uint16_t compare_diff_pixels;
    uint32_t reserved1;
    int origin_x;
    int origin_z;
    uint64_t stamp;
    uint32_t pixels[HUD_MAP_PIXEL_COUNT];
} SuperchunkMapCacheEntry;

typedef enum {
    SDK_MAP_TILE_EMPTY = 0,
    SDK_MAP_TILE_QUEUED,
    SDK_MAP_TILE_BUILDING,
    SDK_MAP_TILE_READY
} SdkMapTileState;

typedef enum {
    SDK_MAP_SCHED_MODE_INTERACTIVE = 0,
    SDK_MAP_SCHED_MODE_OFFLINE_BULK
} SdkMapSchedulerMode;

typedef enum {
    SDK_MAP_BUILD_INTERACTIVE_FALLBACK = 0,
    SDK_MAP_BUILD_EXACT_OFFLINE = 1
} SdkMapBuildKind;

typedef struct {
    SdkWorldDesc world_desc;
    char world_save_id[64];
    uint32_t world_seed;
    int worker_count;
    int mode;
    int build_kind;
} SdkMapSchedulerConfig;

struct SdkMapSchedulerInternal;
typedef struct SdkWorldGenSurfaceColumn SdkWorldGenSurfaceColumn;

typedef struct {
    uint16_t dry_count;
    uint16_t open_water_count;
    uint16_t seasonal_ice_count;
    uint16_t perennial_ice_count;
    uint16_t submerged_count;
    uint16_t void_count;
    uint32_t land_height_sum;
    uint32_t water_height_sum;
    uint32_t water_depth_sum;
} SdkMapExactCellAccumulator;

typedef struct {
    int page_scx;
    int page_scz;
    int ring;
    int tile_count;
} SdkMapOfflinePageJob;

typedef struct {
    HANDLE thread;
    SdkWorldGen worldgen;
    SdkTerrainColumnProfile* map_profile_grid;
    SdkMapExactCellAccumulator* map_exact_cell_grid;
    uint16_t* map_exact_dry_hist;
    uint16_t* map_exact_submerged_hist;
    SdkTerrainColumnProfile* map_exact_chunk_profiles;
    SdkWorldGenSurfaceColumn* map_exact_chunk_surface;
    int worker_index;
    int last_origin_x;
    int last_origin_z;
    int claimed_macro_tile_x;
    int claimed_macro_tile_z;
    bool has_last_origin;
    bool has_macro_tile_claim;
    struct SdkMapSchedulerInternal* owner;
} SdkMapWorker;

typedef struct SdkMapSchedulerInternal {
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE jobs_cv;
    bool initialized;
    bool running;
    bool bulk_cursor_started;
    int worker_count;
    int mode;
    SdkWorldDesc world_desc;
    char world_save_id[64];
    uint32_t world_seed;
    int build_kind;
    SdkMapWorker workers[SDK_MAP_MAX_WORKERS];
    int jobs[SDK_MAP_JOB_CAPACITY];
    int job_head;
    int job_count;
    SdkMapOfflinePageJob page_jobs[SDK_MAP_PAGE_JOB_CAPACITY];
    int page_job_head;
    int page_job_count;
    int bulk_scx;
    int bulk_scz;
    int bulk_dir;
    int bulk_leg_length;
    int bulk_leg_progress;
    int bulk_legs_done;
    int bulk_ring;
    int bulk_tiles_completed;
    int bulk_tiles_inflight;
    int bulk_active_workers;
    int bulk_last_tile_chunks;
    float bulk_last_tile_build_ms;
} SdkMapSchedulerInternal;

typedef struct {
    int worker_count;
    int queued_jobs;
    int building_jobs;
    int queued_pages;
    int active_workers;
    int current_ring;
    int tiles_completed;
    int tiles_inflight;
    int last_tile_chunks;
    float last_tile_build_ms;
} SdkMapSchedulerStats;

typedef struct {
    bool valid;
    uint64_t last_wall_time_100ns;
    uint64_t last_proc_time_100ns;
    int cpu_count;
    float last_cpu_percent;
} SdkPerfTelemetryState;

typedef struct {
    int desired_primary;
    int resident_primary;
    int gpu_ready_primary;
    int pending_jobs;
    int pending_results;
    int active_workers;
    int no_cpu_mesh;
    int upload_pending;
    int gpu_mesh_generation_stale;
    int far_only_when_full_needed;
    int other_not_ready;
} SdkStartupChunkReadiness;

typedef struct {
    int desired_visible;
    int resident_visible;
    int gpu_ready_visible;
    int pending_jobs;
    int pending_results;
    int active_workers;
} SdkRuntimeChunkHealth;

typedef struct {
    bool active;
    float x;
    float y;
    float z;
    float radius;
    int timer;
} SdkSmokeCloud;

typedef struct SdkResolutionPreset {
    uint32_t width;
    uint32_t height;
} SdkResolutionPreset;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t build_kind;
    uint32_t seed;
    int32_t origin_x;
    int32_t origin_z;
    uint32_t dim;
    uint32_t pixels[HUD_MAP_PIXEL_COUNT];
} SdkPersistedMapTile;

typedef struct {
    char name[SDK_START_MENU_SERVER_NAME_MAX];
    char address[SDK_START_MENU_SERVER_ADDRESS_MAX];
} SdkSavedServerEntry;

typedef struct {
    bool active;
    char world_save_id[64];
    char world_name[SDK_START_MENU_WORLD_NAME_MAX];
    uint32_t world_seed;
    char address[SDK_START_MENU_SERVER_ADDRESS_MAX];
} SdkHostedServerEntry;

typedef enum {
    SDK_CLIENT_CONNECTION_NONE = 0,
    SDK_CLIENT_CONNECTION_LOCAL_HOST,
    SDK_CLIENT_CONNECTION_REMOTE_SERVER
} SdkClientConnectionKind;

typedef struct {
    bool connected;
    int kind;
    char server_name[SDK_START_MENU_SERVER_NAME_MAX];
    char address[SDK_START_MENU_SERVER_ADDRESS_MAX];
} SdkClientConnection;

typedef struct {
    bool active;
    bool stop_requested;
    SdkHostedServerEntry hosted;
} SdkLocalHostManager;

typedef enum {
    SDK_WORLD_LAUNCH_STANDARD = 0,
    SDK_WORLD_LAUNCH_LOCAL_HOST_JOIN,
    SDK_WORLD_LAUNCH_LOCAL_HOST_BACKGROUND
} SdkWorldLaunchMode;

typedef struct {
    char asset_id[64];
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];
    char dir_path[MAX_PATH];
    uint64_t last_write_time;
    int animation_count;
} SdkCharacterAssetMeta;

typedef struct {
    char asset_id[64];
    char character_id[64];
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];
    char dir_path[MAX_PATH];
    int frame_count;
    uint64_t last_write_time;
} SdkAnimationAssetMeta;

typedef struct {
    char asset_id[64];
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];
    char dir_path[MAX_PATH];
    uint64_t last_write_time;
} SdkBlockAssetMeta;

typedef struct {
    char asset_id[64];
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];
    char dir_path[MAX_PATH];
    uint64_t last_write_time;
} SdkItemAssetMeta;

typedef struct {
    char asset_id[64];
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];
    char dir_path[MAX_PATH];
    char family_name[32];
    int footprint_x;
    int footprint_z;
    int anchor_x;
    int anchor_y;
    int anchor_z;
    int shell_compatible;
    uint64_t last_write_time;
} SdkPropAssetMeta;

typedef struct {
    char asset_id[64];
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];
    char dir_path[MAX_PATH];
    int slice_count;
    uint64_t last_write_time;
} SdkParticleEffectAssetMeta;

typedef enum {
    SDK_SESSION_KIND_WORLD = 0,
    SDK_SESSION_KIND_CHARACTER_EDITOR,
    SDK_SESSION_KIND_ANIMATION_EDITOR,
    SDK_SESSION_KIND_PROP_EDITOR,
    SDK_SESSION_KIND_BLOCK_EDITOR,
    SDK_SESSION_KIND_ITEM_EDITOR,
    SDK_SESSION_KIND_PARTICLE_EDITOR
} SdkSessionKind;

typedef struct {
    SdkSessionKind kind;
    bool active;
    char character_id[64];
    char character_name[SDK_START_MENU_ASSET_NAME_MAX];
    char animation_id[64];
    char animation_name[SDK_START_MENU_ASSET_NAME_MAX];
    int frame_count;
    int current_frame;
    int preview_frame;
    int playback_counter;
    int preview_counter;
    bool playback_enabled;
    int base_floor_y;
    int frame_chunk_count;
    int build_chunk_min_cx;
    int build_chunk_min_cz;
    int build_chunk_span_x;
    int build_chunk_span_z;
    int build_local_x;
    int build_local_z;
    int build_min_y;
    int build_width;
    int build_depth;
    int build_height;
    int icon_chunk_cx;
    int icon_local_x;
    int icon_local_z;
    int icon_min_y;
    int icon_width;
    int icon_depth;
    int slice_count;
    int slice_width;
} SdkEditorSession;

typedef struct {
    ItemType item;
    BlockType creative_block;
    int count;
    int durability;
    uint8_t payload_kind;
    uint8_t reserved0;
    uint16_t reserved1;
    SdkConstructionItemPayload shaped;
} HotbarEntry;

typedef struct {
    bool active;
    int wx;
    int wy;
    int wz;
    BlockType block_type;
    ItemType input_item;
    int input_count;
    ItemType fuel_item;
    int fuel_count;
    ItemType output_item;
    int output_count;
    int progress;
    int burn_remaining;
    int burn_max;
} StationState;

typedef struct {
    int kind;
    int id;
} CreativeEntry;

extern SdkApiState g_sdk;
extern int g_chunk_grid_size_setting;
extern SdkGraphicsSettings g_graphics_settings;
extern SdkInputSettings g_input_settings;

extern SuperchunkMapCacheEntry g_superchunk_map_cache[HUD_MAP_CACHE_SLOTS];
extern uint64_t g_superchunk_map_stamp;
extern SdkMapSchedulerInternal g_map_scheduler;

extern float g_cam_yaw;
extern float g_cam_pitch;
extern float g_cam_look_dist;
extern bool g_cam_rotation_initialized;

extern float g_vel_y;
extern bool g_on_ground;
extern bool g_test_flight_enabled;
extern bool g_space_was_down;
extern int g_space_tap_timer;
extern bool g_worldgen_debug_was_down;
extern bool g_worldgen_chunk_report_was_down;
extern bool g_settlement_debug_was_down;
extern bool g_settlement_debug_overlay;
extern bool g_settlement_debug_cache_valid;
extern ULONGLONG g_settlement_debug_last_refresh_ms;
extern int g_settlement_debug_last_wx;
extern int g_settlement_debug_last_wz;
extern SdkSettlementDebugUI g_settlement_debug_cached_ui;
extern bool g_fluid_debug_was_down;
extern bool g_fluid_debug_overlay;
extern bool g_perf_debug_was_down;
extern bool g_perf_debug_overlay;

extern SdkPerfTelemetryState g_perf_telemetry;
extern int g_startup_safe_frames_remaining;

extern int g_player_health;
extern int g_invincible_frames;
extern float g_fall_start_y;
extern bool g_was_on_ground;
extern bool g_player_dead;
extern int g_death_timer;
extern float g_spawn_x;
extern float g_spawn_y;
extern float g_spawn_z;

extern int g_player_hunger;
extern int g_hunger_tick;

extern bool g_is_sprinting;
extern bool g_w_was_down;
extern int g_w_tap_timer;
extern bool g_w_sprint_latched;
extern int g_mounted_vehicle_index;
extern bool g_vehicle_use_was_down;
extern int g_weapon_use_cooldown;
extern int g_screen_flash_timer;
extern int g_screen_flash_duration;
extern float g_screen_flash_strength;
extern float g_player_smoke_obscurance;

extern int g_world_time;
extern SdkSmokeCloud g_smoke_clouds[MAX_SMOKE_CLOUDS];

extern SdkWorldSaveMeta g_world_saves[SDK_WORLD_SAVE_MAX];
extern int g_world_save_count;
extern int g_world_save_selected;
extern int g_world_save_scroll;
extern int g_world_action_selected;
extern int g_frontend_main_selected;
extern int g_character_menu_selected;
extern int g_character_menu_scroll;
extern int g_character_action_selected;
extern int g_character_animation_menu_selected;
extern int g_animation_menu_selected;
extern int g_animation_menu_scroll;
extern int g_animation_action_selected;
extern int g_prop_menu_selected;
extern int g_prop_menu_scroll;
extern int g_prop_action_selected;
extern int g_block_menu_selected;
extern int g_block_menu_scroll;
extern int g_block_action_selected;
extern int g_item_menu_selected;
extern int g_item_menu_scroll;
extern int g_item_action_selected;
extern int g_particle_effect_menu_selected;
extern int g_particle_effect_menu_scroll;
extern int g_particle_effect_action_selected;
extern int g_selected_character_index;
extern int g_selected_animation_index;
extern int g_selected_prop_index;
extern int g_selected_block_index;
extern int g_selected_item_index;
extern int g_selected_particle_effect_index;
extern int g_frontend_view;
extern bool g_frontend_nav_was_down[8];
extern bool g_frontend_refresh_pending;
extern SdkCharacterAssetMeta g_character_assets[SDK_CHARACTER_ASSET_MAX];
extern int g_character_asset_count;
extern SdkAnimationAssetMeta g_animation_assets[SDK_ANIMATION_ASSET_MAX];
extern int g_animation_asset_count;
extern SdkPropAssetMeta g_prop_assets[SDK_PROP_ASSET_MAX];
extern int g_prop_asset_count;
extern SdkBlockAssetMeta g_block_assets[SDK_BLOCK_ASSET_MAX];
extern int g_block_asset_count;
extern SdkItemAssetMeta g_item_assets[SDK_ITEM_ASSET_MAX];
extern int g_item_asset_count;
extern SdkParticleEffectAssetMeta g_particle_effect_assets[SDK_PARTICLE_EFFECT_ASSET_MAX];
extern int g_particle_effect_asset_count;
extern SdkSessionKind g_session_kind;
extern SdkEditorSession g_editor_session;

extern int g_world_create_selected;
extern uint32_t g_world_create_seed;
extern int g_world_create_render_distance;
extern int g_world_create_spawn_type;
extern int g_world_create_settlements_enabled;
extern int g_world_create_construction_cells_enabled;
extern int g_world_create_coordinate_system;
extern int g_world_create_superchunks_enabled;
extern int g_world_create_superchunk_chunk_span;
extern int g_world_create_walls_enabled;
extern int g_world_create_wall_grid_size;
extern int g_world_create_wall_grid_offset_x;
extern int g_world_create_wall_grid_offset_z;
extern int g_world_create_wall_thickness_blocks;
extern int g_world_create_wall_rings_shared;

extern bool g_world_generation_active;
extern bool g_world_generation_cancel_requested;
extern bool g_world_generation_is_offline;
extern float g_world_generation_progress;
extern int g_world_generation_stage;
extern char g_world_generation_status[256];
extern SdkWorldSaveMeta g_world_generation_target;
extern int g_world_generation_chunks_total;
extern int g_world_generation_chunks_done;
extern bool g_world_map_generation_active;
extern float g_world_map_generation_progress;
extern int g_world_map_generation_stage;
extern char g_world_map_generation_status[256];
extern SdkWorldSaveMeta g_world_map_generation_target;
extern int g_world_map_generation_threads;
extern int g_world_map_generation_ring;
extern int g_world_map_generation_tiles_done;

extern uint64_t g_world_generation_started_ms;
extern uint64_t g_world_generation_last_sample_ms;
extern int g_world_generation_last_sample_chunks;
extern int g_world_generation_active_workers;
extern float g_world_generation_chunks_per_sec;
extern int g_world_generation_threads;
extern int g_world_generation_superchunks_done;
extern int g_world_generation_current_ring;
extern SdkSavedServerEntry g_saved_servers[SDK_SAVED_SERVER_MAX];
extern int g_saved_server_count;
extern int g_saved_server_selected;
extern int g_saved_server_scroll;
extern int g_hosted_server_selected;
extern int g_hosted_server_scroll;
extern int g_online_section_focus;
extern int g_online_edit_selected;
extern int g_online_edit_target_index;
extern bool g_online_edit_is_new;
extern char g_online_edit_name[SDK_START_MENU_SERVER_NAME_MAX];
extern char g_online_edit_address[SDK_START_MENU_SERVER_ADDRESS_MAX];
extern char g_online_status[SDK_SERVER_STATUS_TEXT_MAX];
extern bool g_frontend_forced_open;
extern int g_pending_world_launch_mode;
extern SdkLocalHostManager g_local_host_manager;
extern SdkClientConnection g_client_connection;

#ifndef SDK_RESOLUTION_PRESET_COUNT
#define SDK_RESOLUTION_PRESET_COUNT 4
#endif
#define SDK_RENDER_DISTANCE_PRESET_COUNT 6
#define SDK_FAR_MESH_DISTANCE_PRESET_COUNT 8
extern const SdkResolutionPreset g_resolution_presets[4];
extern const int g_render_distance_presets[SDK_RENDER_DISTANCE_PRESET_COUNT];
extern const int g_far_mesh_distance_presets[SDK_FAR_MESH_DISTANCE_PRESET_COUNT];

extern HotbarEntry g_hotbar[10];
extern int g_hotbar_selected;
extern int g_break_target_bx;
extern int g_break_target_by;
extern int g_break_target_bz;
extern int g_last_hit_face;
extern float g_last_hit_world_x;
extern float g_last_hit_world_y;
extern float g_last_hit_world_z;
extern bool g_last_hit_world_valid;
extern int g_construction_place_rotation;
extern int g_break_progress;
extern bool g_break_active;
extern int g_break_cooldown;

extern bool g_craft_open;
extern bool g_craft_is_table;
extern ItemType g_craft_grid[9];
extern int g_craft_grid_count[9];
extern int g_craft_cursor;
extern int g_craft_result_idx;
extern bool g_craft_key_was_down;
extern bool g_craft_lmb_was_down;
extern bool g_craft_rmb_was_down;
extern bool g_craft_nav_was_down[6];
extern bool g_craft_result_lmb_was_down;
extern bool g_craft_result_rmb_was_down;

extern StationState g_station_states[MAX_STATION_STATES];
extern int g_station_state_count;
extern bool g_station_open;
extern SdkStationUIKind g_station_open_kind;
extern BlockType g_station_open_block_type;
extern int g_station_open_index;
extern int g_station_hovered_slot;
extern bool g_station_lmb_was_down;
extern bool g_station_rmb_was_down;

extern bool g_skills_open;
extern bool g_skills_key_was_down;
extern int g_skills_key_frames;
extern int g_skills_selected_tab;
extern int g_skills_selected_row;
extern int g_skills_selected_profession;
extern bool g_skills_nav_was_down[8];
extern bool g_pause_menu_open;
extern bool g_pause_menu_key_was_down;
extern int g_pause_menu_view;
extern int g_pause_menu_selected;
extern int g_graphics_menu_selected;
extern int g_keybind_menu_selected;
extern int g_keybind_menu_scroll;
extern bool g_keybind_capture_active;
extern bool g_pause_menu_nav_was_down[6];
extern int g_pause_character_selected;
extern int g_pause_character_scroll;
extern int g_chunk_manager_selected;
extern int g_chunk_manager_scroll;
extern int g_creative_menu_selected;
extern int g_creative_menu_scroll;
extern int g_creative_menu_filter;
extern char g_creative_menu_search[SDK_PAUSE_MENU_SEARCH_MAX];
extern int g_creative_menu_search_len;
extern int g_creative_shape_focus;
extern int g_creative_shape_row;
extern int g_creative_shape_width;
extern int g_creative_shape_height;
extern int g_creative_shape_depth;

extern bool g_command_open;
extern char g_command_text[SDK_COMMAND_LINE_TEXT_MAX];
extern int g_command_text_len;
extern bool g_command_enter_was_down;
extern bool g_command_backspace_was_down;

extern SdkProfiler g_profiler;
extern bool g_command_slash_was_down;
extern bool g_map_focus_open;
extern bool g_map_key_was_down;
extern bool g_map_zoom_left_was_down;
extern bool g_map_zoom_right_was_down;
extern int g_map_zoom_left_hold_frames;
extern int g_map_zoom_right_hold_frames;
extern float g_map_focus_world_x;
extern float g_map_focus_world_z;
extern bool g_map_focus_initialized;
extern int g_map_zoom_tenths;

extern int g_player_level;
extern int g_player_xp;
extern int g_player_xp_to_next;
extern int g_combat_skill_ranks[SDK_COMBAT_SKILL_COUNT];
extern int g_survival_skill_ranks[SDK_SURVIVAL_SKILL_COUNT];
extern int g_profession_points[SDK_PROFESSION_COUNT];
extern int g_profession_levels[SDK_PROFESSION_COUNT];
extern int g_profession_xp[SDK_PROFESSION_COUNT];
extern int g_profession_xp_to_next[SDK_PROFESSION_COUNT];
extern int g_profession_ranks[SDK_PROFESSION_COUNT][SDK_PROFESSION_NODE_COUNT];

int ensure_directory_exists_a(const char* path);
uint64_t filetime_to_u64(FILETIME ft);
float api_clampf(float v, float lo, float hi);
int api_clampi(int v, int lo, int hi);
float update_process_cpu_percent(void);
void get_monitor_work_area_size(HWND hwnd, LONG* out_width, LONG* out_height);
void clamp_window_resolution_to_work_area(HWND hwnd, uint32_t* inout_width, uint32_t* inout_height);
void get_monitor_pixel_size(HWND hwnd, LONG* out_width, LONG* out_height);
int clamp_resolution_preset_index(int index);
int resolution_preset_index_for_size(uint32_t width, uint32_t height);
int clamp_render_distance_chunks(int radius);
int render_distance_preset_index(int radius);
int clamp_far_mesh_distance_chunks(int distance);
int far_mesh_distance_preset_index(int distance);
int normalize_far_mesh_lod_distance(int render_distance_chunks, int lod_distance_chunks);
int normalize_experimental_far_mesh_distance(int render_distance_chunks,
                                             int lod_distance_chunks,
                                             int experimental_distance_chunks);
int clamp_anisotropy_level(int level);
int anisotropy_preset_index(int level);
int clamp_render_scale_percent(int value);
void apply_resolution_preset_index(int preset_index, bool save_now);
void sync_graphics_resolution_from_window(void);
void apply_window_resolution_setting(uint32_t width, uint32_t height, bool save_now);
void apply_display_mode_setting(bool save_now);
void save_input_settings_now(void);

void load_character_profile(void);
void sdk_server_runtime_reset(void);
void sdk_server_runtime_tick(void);
void sdk_server_runtime_on_world_session_stopped(void);
void sdk_saved_servers_load_all(void);
int sdk_saved_server_upsert(int index, const char* name, const char* address);
int sdk_saved_server_delete_entry(int index);
void sdk_online_clear_status(void);
void sdk_online_set_status(const char* status);
int sdk_local_host_can_start_world(const SdkWorldSaveMeta* meta);
int sdk_local_host_matches_world_id(const char* world_save_id);
void sdk_prepare_world_launch(int launch_mode);
void sdk_finalize_world_launch(const SdkWorldSaveMeta* world);
void sdk_client_join_active_local_host(void);
void sdk_client_disconnect_to_frontend(int frontend_view);
void sdk_local_host_request_stop(void);
int sdk_try_connect_saved_server(int index);
void refresh_character_assets(void);
void refresh_animation_assets_for_selected_character(void);
void refresh_prop_assets(void);
void refresh_block_assets(void);
void refresh_item_assets(void);
void refresh_particle_effect_assets(void);
int create_character_asset(SdkCharacterAssetMeta* out_meta);
int copy_character_asset(int character_index);
int delete_character_asset(int character_index);
int create_animation_asset_for_selected_character(SdkAnimationAssetMeta* out_meta);
int copy_selected_animation_asset(void);
int delete_selected_animation_asset(void);
int create_prop_asset(SdkPropAssetMeta* out_meta);
int copy_prop_asset(int prop_index);
int delete_prop_asset(int prop_index);
int create_block_asset(SdkBlockAssetMeta* out_meta);
int copy_block_asset(int block_index);
int delete_block_asset(int block_index);
int create_item_asset(SdkItemAssetMeta* out_meta);
int copy_item_asset(int item_index);
int delete_item_asset(int item_index);
int create_particle_effect_asset(SdkParticleEffectAssetMeta* out_meta);
int copy_particle_effect_asset(int particle_effect_index);
int delete_particle_effect_asset(int particle_effect_index);
int load_character_model_bytes(const char* character_id, uint8_t* out_voxels, size_t voxel_count);
int save_character_model_bytes(const char* character_id, const uint8_t* voxels, size_t voxel_count);
int load_animation_frame_bytes(const char* character_id, const char* animation_id,
                               int frame_index, uint8_t* out_voxels, size_t voxel_count);
int save_animation_frame_bytes(const char* character_id, const char* animation_id,
                               int frame_index, const uint8_t* voxels, size_t voxel_count);
int load_block_model_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count);
int save_block_model_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count);
int load_block_icon_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count);
int save_block_icon_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count);
int load_item_model_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count);
int save_item_model_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count);
int load_item_icon_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count);
int save_item_icon_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count);
int load_particle_effect_timeline_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count);
int save_particle_effect_timeline_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count);
int load_particle_effect_icon_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count);
int save_particle_effect_icon_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count);
int load_prop_chunk_bytes(const char* asset_id, int chunk_x, int chunk_z,
                          uint8_t* out_voxels, size_t voxel_count);
int save_prop_chunk_bytes(const char* asset_id, int chunk_x, int chunk_z,
                          const uint8_t* voxels, size_t voxel_count);
int load_prop_chunk_cell_codes_v2(const char* asset_id, int chunk_x, int chunk_z,
                                  SdkWorldCellCode* out_cells,
                                  int width, int depth, int height,
                                  int* out_found);
int save_prop_chunk_cell_codes_v2(const char* asset_id, int chunk_x, int chunk_z,
                                  const SdkWorldCellCode* cells,
                                  int width, int depth, int height);
int load_prop_chunk_construction_text(const char* asset_id, int chunk_x, int chunk_z,
                                      char** out_text, int* out_found);
int save_prop_chunk_construction_text(const char* asset_id, int chunk_x, int chunk_z,
                                      const char* encoded);
int load_prop_construction_registry_text(const char* asset_id, char** out_text, int* out_found);
int save_prop_construction_registry_text(const char* asset_id, const char* encoded);
void capture_persisted_state(SdkPersistedState* out_state);
void apply_persisted_state(const SdkPersistedState* state);
void rebuild_selected_gameplay_character_mesh(void);
void select_gameplay_character_index(int character_index, bool persist_now);
void frontend_open_character_menu(void);
void frontend_reset_nav_state(void);
void frontend_open_main_menu(void);
void frontend_refresh_worlds_if_needed(void);
void clear_non_frontend_ui(void);
void push_start_menu_ui(void);
void present_start_menu_frame(void);
void update_async_world_generation(void);
void set_world_generation_session_progress(float session_progress, const char* status);
void set_world_generation_session_summary(char (*lines)[128], int count);
int world_generation_summary_active(void);
void dismiss_world_generation_summary(void);
extern char g_gen_summary_lines[16][128];
extern int g_gen_summary_line_count;
void sync_active_world_meta(uint32_t world_seed);
void frontend_handle_input(void);
void begin_async_return_to_start(void);
void update_async_return_to_start(void);

int init_superchunk_map_scheduler(const SdkMapSchedulerConfig* config);
void shutdown_superchunk_map_scheduler(void);
void request_shutdown_superchunk_map_scheduler(void);
int poll_shutdown_superchunk_map_scheduler(void);
void pump_superchunk_map_scheduler_offline_bulk(int max_jobs);
void get_superchunk_map_scheduler_stats(SdkMapSchedulerStats* out_stats);
uint32_t sdk_map_color_for_profiles(const SdkTerrainColumnProfile* center,
                                    const SdkTerrainColumnProfile* west,
                                    const SdkTerrainColumnProfile* east,
                                    const SdkTerrainColumnProfile* north,
                                    const SdkTerrainColumnProfile* south,
                                    int sea_level);
bool startup_safe_mode_active(void);
void collect_startup_chunk_readiness(SdkStartupChunkReadiness* out_readiness);
const SdkStartupChunkReadiness* startup_safe_mode_readiness(void);
const SdkStartupChunkReadiness* startup_bootstrap_completion_readiness(void);
bool startup_bootstrap_completed(void);
void collect_runtime_chunk_health(SdkRuntimeChunkHealth* out_health);
int startup_safe_primary_radius(void);
int startup_safe_neighbor_radius(void);
int stream_upload_limit_per_frame(void);
float stream_gpu_upload_budget_ms(void);
int dirty_remesh_jobs_per_frame(void);
void apply_graphics_atmosphere(float ambient, float sky_r, float sky_g, float sky_b);
void mark_all_loaded_chunks_dirty(void);
void evict_undesired_loaded_chunks(void);
void bootstrap_nearby_visible_chunks_sync(void);
void process_streamed_chunk_results(int max_results);
void process_pending_chunk_gpu_uploads(int max_chunks, float budget_ms);
void process_dirty_chunk_mesh_uploads(int max_chunks);
void rebuild_loaded_dirty_chunks_sync(int max_chunks);
void persist_loaded_chunks(void);
void push_superchunk_map_ui(float world_x, float world_z);
void tick_startup_safe_mode(void);
void rebuild_chunk_grid_for_current_camera(int new_grid_size);
void save_graphics_settings_now(void);
void update_window_title_for_test_flight(void);
void reset_session_runtime_state(void);
void shutdown_world_session(bool save_state);
int start_world_session(const SdkWorldSaveMeta* selected_world);
int start_character_editor_session(const SdkCharacterAssetMeta* character);
int start_animation_editor_session(const SdkCharacterAssetMeta* character,
                                   const SdkAnimationAssetMeta* animation);
int start_prop_editor_session(const SdkPropAssetMeta* prop_asset);
int start_block_editor_session(const SdkBlockAssetMeta* block_asset);
int start_item_editor_session(const SdkItemAssetMeta* item_asset);
int start_particle_effect_editor_session(const SdkParticleEffectAssetMeta* particle_effect_asset);
bool editor_session_active(void);
bool editor_block_in_bounds(int wx, int wy, int wz);
void build_editor_ui(SdkEditorUI* out_ui);
void save_editor_session_assets(void);
void tick_editor_session(void);
int estimate_spawn_relief(SdkWorldGen* wg, int wx, int wz, int center_height);
int bootstrap_visible_chunks_sync(void);
SdkChunk* generate_or_load_chunk_sync(int cx, int cz, SdkChunkResidencyRole role);

bool is_solid_at(int wx, int wy, int wz);
bool aabb_collides(float min_x, float min_y, float min_z,
                   float max_x, float max_y, float max_z);
BlockType get_block_at(int wx, int wy, int wz);
void set_block_at(int wx, int wy, int wz, BlockType type);
void mark_chunk_stream_neighbors_dirty(int cx, int cz);
void mark_chunk_stream_adjacent_neighbors_dirty(int cx, int cz);
int sdk_actor_break_block(HotbarEntry* held, int wx, int wy, int wz, int spawn_drop);
int sdk_actor_place_block(HotbarEntry* held,
                          int wx, int wy, int wz,
                          float actor_x, float actor_feet_y, float actor_z,
                          float actor_half_w, float actor_height,
                          int enforce_actor_clearance);
bool raycast_block(float ox, float oy, float oz,
                   float dx, float dy, float dz,
                   float max_dist,
                   int* out_bx, int* out_by, int* out_bz,
                   int* out_face,
                   int* out_prev_bx, int* out_prev_by, int* out_prev_bz,
                   float* out_dist);
void resolve_loaded_chunk_boundary_water(SdkChunk* chunk);

void tick_weapon_effects(float player_x, float player_y, float player_z);
int try_use_combat_item(HotbarEntry* held,
                        float cam_x, float cam_y, float cam_z,
                        float dir_x, float dir_y, float dir_z);
void clear_hotbar_entry(HotbarEntry* entry);
void hotbar_add(ItemType type);
void hotbar_add_pickup(const SdkPickupItem* pickup);
void hotbar_set_creative_block(int slot, BlockType block);
void hotbar_set_shaped_payload(int slot, const SdkConstructionItemPayload* payload);
void hotbar_set_item(int slot, ItemType item, int count);
int hotbar_get_place_block(const HotbarEntry* entry, BlockType* out_block, int* out_consume);
int use_spawn_item_at(HotbarEntry* held, int wx, int wy, int wz);
void drop_loot_for_killed_mob(MobType killed_type, float kx, float ky, float kz);
SdkEntity* mounted_vehicle_entity(void);
void sync_camera_to_vehicle(const SdkEntity* vehicle,
                            float* cam_x, float* cam_y, float* cam_z,
                            float* look_x, float* look_y, float* look_z);
int find_mountable_vehicle(float px, float py, float pz,
                           float look_x, float look_y, float look_z);
void dismount_vehicle(float* cam_x, float* cam_y, float* cam_z,
                      float* look_x, float* look_y, float* look_z);

int is_station_block(BlockType type);
int station_state_is_meaningful(const StationState* state);
int station_find_state_public(int wx, int wy, int wz);
int station_ensure_state_public(int wx, int wy, int wz, BlockType block_type);
const StationState* station_get_state_const(int index);
int station_npc_place_item(int index, int slot, ItemType item);
int station_npc_take_output(int index, ItemType* out_item);
void station_close_ui(void);
void station_sync_to_persistence(int index);
void station_load_all_from_persistence(void);
void station_open_for_block(int wx, int wy, int wz, BlockType block_type);
int station_progress_max(BlockType block_type);
void station_tick_all(void);
int craft_grid_w(void);
int craft_grid_h(void);
void craft_update_match(void);
void craft_take_result(void);
bool craft_mouse_over_result_slot(int32_t mouse_x, int32_t mouse_y);
void craft_take_result_bulk(void);
void craft_place_from_hotbar(void);
void craft_remove_to_hotbar(void);
void craft_close(void);
int available_general_skill_points(void);
int creative_visible_entry_count(void);
CreativeEntry creative_entry_for_filtered_index(int index);
void command_close(void);
void command_open(void);
void teleport_player_to(float world_x, float world_y, float world_z,
                        float* cam_x, float* cam_y, float* cam_z,
                        float* look_x, float* look_y, float* look_z);
void command_line_handle_input(float* cam_x, float* cam_y, float* cam_z,
                               float* look_x, float* look_y, float* look_z);
void pause_menu_handle_input(void);
void station_handle_block_change(int wx, int wy, int wz, BlockType old_type, BlockType new_type);
void station_handle_ui_input(void);
void skills_clamp_selection(void);
void skills_reset_progression(void);
void skills_handle_menu_input(void);
void map_handle_input(float player_world_x, float player_world_z);

#ifdef __cplusplus
}
#endif

#endif
