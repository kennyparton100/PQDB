/**
 * d3d12_renderer.h — D3D12 renderer interface
 *
 * C-linkage API so the pure-C sdk_api.c can call into the C++ renderer.
 */
#ifndef NQLSDK_D3D12_RENDERER_H
#define NQLSDK_D3D12_RENDERER_H

#include "../Core/sdk_types.h"
#include "../Core/Automation/sdk_automation.h"
#include "../Core/World/Chunks/sdk_chunk.h"
#include "../Core/World/Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../Core/World/Terrain/sdk_terrain.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the D3D12 renderer.
 * @param hwnd         Target window handle.
 * @param width        Initial back-buffer width.
 * @param height       Initial back-buffer height.
 * @param clear_color  Background clear colour.
 * @param enable_debug Enable D3D12 debug layer.
 * @param vsync        Present with vsync.
 * @return SDK_OK on success.
 */
SdkResult sdk_renderer_init(
    HWND        hwnd,
    uint32_t    width,
    uint32_t    height,
    SdkColor4   clear_color,
    bool        enable_debug,
    bool        vsync);

/** Render one frame (clear + draw triangle + present). */
SdkResult sdk_renderer_frame(void);

/** Handle window resize — recreate swap chain buffers. */
SdkResult sdk_renderer_resize(uint32_t width, uint32_t height);
SdkResult sdk_renderer_set_display_mode(int display_mode, uint32_t requested_width,
                                        uint32_t requested_height,
                                        uint32_t* out_width, uint32_t* out_height);
bool sdk_renderer_is_fullscreen(void);

/** Shut down renderer, release all D3D12 resources. */
void sdk_renderer_shutdown(void);

/* =================================================================
 * VOXEL WORLD RENDERING
 * ================================================================= */

/** Set the chunk manager for rendering. Must be called before chunks are drawn. */
void sdk_renderer_set_chunk_manager(SdkChunkManager* cm);

/** Upload a chunk's subchunk meshes to GPU. Creates/updates per-subchunk vertex buffers.
 * @param chunk The chunk with CPU mesh slices ready for upload
 * @return SDK_OK on success.
 */
SdkResult sdk_renderer_upload_chunk_mesh(SdkChunk* chunk);
SdkResult sdk_renderer_upload_chunk_mesh_unified(SdkChunk* chunk);
SdkResult sdk_renderer_upload_chunk_mesh_unified_mode(SdkChunk* chunk, SdkChunkGpuUploadMode mode);
void sdk_renderer_free_chunk_unified_buffer(SdkChunk* chunk);

/** Draw all chunks in the chunk manager. Call after terrain/mesh generation. */
SdkResult sdk_renderer_draw_chunks(void);

/** Free GPU resources for a specific chunk. */
void sdk_renderer_free_chunk_mesh(SdkChunk* chunk);

/** Get camera position in world coordinates for chunk culling.
 * @param out_x World X position (block coords)
 * @param out_y World Y position (block coords)
 * @param out_z World Z position (block coords)
 */
void sdk_renderer_get_camera_pos(float* out_x, float* out_y, float* out_z);

/** Set camera position in world coordinates.
 * @param x World X position
 * @param y World Y position
 * @param z World Z position
 */
void sdk_renderer_set_camera_pos(float x, float y, float z);

/** Get camera look target.
 * @param out_x Target X position
 * @param out_y Target Y position
 * @param out_z Target Z position
 */
void sdk_renderer_get_camera_target(float* out_x, float* out_y, float* out_z);

/** Set camera look target.
 * @param x Target X position
 * @param y Target Y position
 * @param z Target Z position
 */
void sdk_renderer_set_camera_target(float x, float y, float z);

/** Set the block outline highlight position. Pass visible=false to hide. */
void sdk_renderer_set_outline(int bx, int by, int bz, bool visible);

typedef enum {
    SDK_PLACEMENT_PREVIEW_NONE = 0,
    SDK_PLACEMENT_PREVIEW_FULL_BLOCK,
    SDK_PLACEMENT_PREVIEW_CONSTRUCTION
} SdkPlacementPreviewMode;

typedef struct {
    bool visible;
    bool valid;
    int  wx;
    int  wy;
    int  wz;
    int  face;
    int  face_u;
    int  face_v;
    int  mode;
    int  material;
    SdkConstructionItemPayload payload;
} SdkPlacementPreview;

/** Set the placement preview state. Pass null or visible=false to hide. */
void sdk_renderer_set_placement_preview(const SdkPlacementPreview* preview);

/** Hotbar slot for HUD display. */
typedef struct {
    int item_type;          /* ItemType enum value, 0 = empty */
    int direct_block_type;  /* BlockType for creative-selected infinite placement */
    int count;
} SdkHotbarSlot;

/** Update hotbar HUD data. Called each frame from game logic. */
void sdk_renderer_set_hotbar(const SdkHotbarSlot* slots, int num_slots, int selected);

/** Update player health and hunger state for HUD rendering. */
void sdk_renderer_set_health(int health, int max_health, bool dead, bool invincible,
                              int hunger, int max_hunger);

/** Set day/night lighting: ambient level (0.15-1.0) and sky color. */
void sdk_renderer_set_lighting(float ambient, float sky_r, float sky_g, float sky_b);
void sdk_renderer_set_vsync(bool vsync);

/** Optional atmospheric override for renderer-side lighting/fog tuning. */
typedef struct {
    bool  enabled;
    float ambient;
    float sky_r, sky_g, sky_b;
    float sun_dir_x, sun_dir_y, sun_dir_z;
    float fog_r, fog_g, fog_b;
    float fog_density;
    float water_r, water_g, water_b;
    float water_alpha;
    bool  camera_submerged;
    float waterline_y;
    float water_view_depth;
} SdkAtmosphereUI;

/** Override synthesized atmosphere parameters. Pass null to disable. */
void sdk_renderer_set_atmosphere(const SdkAtmosphereUI* ui);

/** Crafting UI data for HUD rendering. */
typedef struct {
    int  grid_item[9];     /* ItemType per slot (row-major) */
    int  grid_count[9];    /* Count per slot */
    int  grid_w, grid_h;   /* 2x2 or 3x3 */
    int  cursor;           /* Highlighted slot index */
    int  result_item;      /* ItemType of matched result, or 0 */
    int  result_count;     /* Count of result */
    bool open;             /* Is crafting UI visible? */
} SdkCraftingUI;

/** Update crafting UI state for HUD rendering. */
void sdk_renderer_set_crafting(const SdkCraftingUI* ui);

typedef enum {
    SDK_STATION_UI_NONE = 0,
    SDK_STATION_UI_FURNACE,
    SDK_STATION_UI_CAMPFIRE,
    SDK_STATION_UI_PLACEHOLDER
} SdkStationUIKind;

typedef struct {
    bool open;
    int  kind;
    int  block_type;
    int  hovered_slot;
    int  input_item;
    int  input_count;
    int  fuel_item;
    int  fuel_count;
    int  output_item;
    int  output_count;
    int  progress;
    int  progress_max;
    int  burn_remaining;
    int  burn_max;
} SdkStationUI;

/** Update station UI state for HUD rendering. */
void sdk_renderer_set_station(const SdkStationUI* ui);

#define SDK_SKILL_TAB_COUNT 3
#define SDK_PROFESSION_COUNT 12
#define SDK_COMBAT_SKILL_COUNT 4
#define SDK_SURVIVAL_SKILL_COUNT 4
#define SDK_PROFESSION_NODE_COUNT 3

typedef enum {
    SDK_SKILL_TAB_COMBAT = 0,
    SDK_SKILL_TAB_SURVIVAL,
    SDK_SKILL_TAB_PROFESSIONS
} SdkSkillTab;

typedef enum {
    SDK_PROFESSION_SKINNING = 0,
    SDK_PROFESSION_LEATHERWORKING,
    SDK_PROFESSION_MINING,
    SDK_PROFESSION_SMELTING,
    SDK_PROFESSION_ENGINEERING,
    SDK_PROFESSION_BLACKSMITHING,
    SDK_PROFESSION_HERBALISM,
    SDK_PROFESSION_MEDICINE,
    SDK_PROFESSION_COOKING,
    SDK_PROFESSION_FISHING,
    SDK_PROFESSION_BUILDING,
    SDK_PROFESSION_HUNTING
} SdkProfessionId;

typedef struct {
    bool open;
    int  level;
    int  xp_current;
    int  xp_to_next;
    int  unspent_skill_points;
    int  selected_tab;
    int  selected_row;
    int  selected_profession;
    int  combat_ranks[SDK_COMBAT_SKILL_COUNT];
    int  survival_ranks[SDK_SURVIVAL_SKILL_COUNT];
    int  profession_points[SDK_PROFESSION_COUNT];
    int  profession_levels[SDK_PROFESSION_COUNT];
    int  profession_xp[SDK_PROFESSION_COUNT];
    int  profession_xp_to_next[SDK_PROFESSION_COUNT];
    int  profession_ranks[SDK_PROFESSION_COUNT][SDK_PROFESSION_NODE_COUNT];
} SdkSkillsUI;

/** Update skills/progression HUD and menu state. */
void sdk_renderer_set_skills(const SdkSkillsUI* ui);

#define SDK_PAUSE_MENU_OPTION_COUNT 8
#define SDK_PAUSE_MENU_SEARCH_MAX 64
#define SDK_CREATIVE_MENU_VISIBLE_ROWS 11
#define SDK_RESOLUTION_PRESET_COUNT 4
#define SDK_START_MENU_ASSET_VISIBLE_MAX 12
#define SDK_START_MENU_ASSET_NAME_MAX 32
#define SDK_KEYBIND_MENU_VISIBLE_ROWS 12
#define SDK_KEYBIND_LABEL_MAX 48
#define SDK_KEYBIND_VALUE_MAX 24

typedef enum {
    SDK_PAUSE_MENU_VIEW_MAIN = 0,
    SDK_PAUSE_MENU_VIEW_GRAPHICS,
    SDK_PAUSE_MENU_VIEW_KEY_BINDINGS,
    SDK_PAUSE_MENU_VIEW_CREATIVE,
    SDK_PAUSE_MENU_VIEW_SELECT_CHARACTER,
    SDK_PAUSE_MENU_VIEW_DEBUG_PROFILER,
    SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER
} SdkPauseMenuView;

typedef enum {
    SDK_GRAPHICS_MENU_ROW_QUALITY_PRESET = 0,
    SDK_GRAPHICS_MENU_ROW_DISPLAY_MODE,
    SDK_GRAPHICS_MENU_ROW_RESOLUTION,
    SDK_GRAPHICS_MENU_ROW_RENDER_SCALE,
    SDK_GRAPHICS_MENU_ROW_ANTI_ALIASING,
    SDK_GRAPHICS_MENU_ROW_SMOOTH_LIGHTING,
    SDK_GRAPHICS_MENU_ROW_SHADOW_QUALITY,
    SDK_GRAPHICS_MENU_ROW_WATER_QUALITY,
    SDK_GRAPHICS_MENU_ROW_RENDER_DISTANCE,
    SDK_GRAPHICS_MENU_ROW_ANISOTROPIC_SAMPLING,
    SDK_GRAPHICS_MENU_ROW_FAR_TERRAIN_LOD,
    SDK_GRAPHICS_MENU_ROW_EXPERIMENTAL_FAR_MESHES,
    SDK_GRAPHICS_MENU_ROW_BLACK_SUPERCHUNK_WALLS,
    SDK_GRAPHICS_MENU_ROW_SUPERCHUNK_LOAD_MODE,
    SDK_GRAPHICS_MENU_ROW_VSYNC,
    SDK_GRAPHICS_MENU_ROW_FOG,
    SDK_GRAPHICS_MENU_ROW_COUNT
} SdkGraphicsMenuRow;

typedef enum {
    SDK_CREATIVE_ENTRY_BLOCK = 0,
    SDK_CREATIVE_ENTRY_ITEM
} SdkCreativeEntryKind;

typedef enum {
    SDK_CREATIVE_FILTER_ALL = 0,
    SDK_CREATIVE_FILTER_BUILDING_BLOCKS,
    SDK_CREATIVE_FILTER_COLORS,
    SDK_CREATIVE_FILTER_ITEMS
} SdkCreativeMenuFilter;

typedef struct {
    bool open;
    int  view;
    int  selected;
    int  graphics_selected;
    int  graphics_preset;
    int  graphics_display_mode;
    int  graphics_resolution_preset;
    int  graphics_render_scale_percent;
    int  graphics_anti_aliasing_mode;
    bool graphics_smooth_lighting;
    int  graphics_shadow_quality;
    int  graphics_water_quality;
    int  graphics_render_distance_chunks;
    int  graphics_anisotropy_level;
    int  graphics_far_terrain_lod_distance_chunks;
    int  graphics_experimental_far_mesh_distance_chunks;
    bool graphics_black_superchunk_walls;
    bool graphics_vsync;
    bool graphics_fog_enabled;
    int  resolution_width;
    int  resolution_height;
    int  keybind_selected;
    int  keybind_scroll;
    int  keybind_total;
    int  keybind_visible_count;
    bool keybind_capture_active;
    int  look_sensitivity_percent;
    bool invert_y;
    char keybind_label[SDK_KEYBIND_MENU_VISIBLE_ROWS][SDK_KEYBIND_LABEL_MAX];
    char keybind_value[SDK_KEYBIND_MENU_VISIBLE_ROWS][SDK_KEYBIND_VALUE_MAX];
    int  creative_selected;
    int  creative_scroll;
    int  creative_total;
    int  creative_visible_count;
    int  creative_filter;
    int  creative_visible_entry_kind[SDK_CREATIVE_MENU_VISIBLE_ROWS];
    int  creative_visible_entry_id[SDK_CREATIVE_MENU_VISIBLE_ROWS];
    char creative_search[SDK_PAUSE_MENU_SEARCH_MAX];
    int  creative_shape_focus;
    int  creative_shape_row;
    int  creative_shape_width;
    int  creative_shape_height;
    int  creative_shape_depth;
    int  character_selected;
    int  character_scroll;
    int  character_count;
    int  character_current;
    char character_name[SDK_START_MENU_ASSET_VISIBLE_MAX][SDK_START_MENU_ASSET_NAME_MAX];
    int  chunk_manager_selected;
    int  chunk_manager_scroll;
    bool local_hosted_session;
    bool remote_session;
} SdkPauseMenuUI;

/** Update pause menu HUD state. */
void sdk_renderer_set_pause_menu(const SdkPauseMenuUI* ui);

#define SDK_START_MENU_WORLD_VISIBLE_MAX 10
#define SDK_START_MENU_WORLD_NAME_MAX 40
#define SDK_START_MENU_SERVER_VISIBLE_MAX 8
#define SDK_START_MENU_SERVER_NAME_MAX 40
#define SDK_START_MENU_SERVER_ADDRESS_MAX 64
#define SDK_SERVER_STATUS_TEXT_MAX 128
#define SDK_START_MENU_CHARACTER_NAME_MAX 24
#define SDK_FRONTEND_MAIN_OPTION_COUNT 11
#define SDK_CHARACTER_ACTION_OPTION_COUNT 5
#define SDK_CHARACTER_ANIMATION_MENU_OPTION_COUNT 3
#define SDK_ANIMATION_ACTION_OPTION_COUNT 4
#define SDK_GENERIC_ASSET_ACTION_OPTION_COUNT 4

typedef enum {
    SDK_START_MENU_VIEW_MAIN = 0,
    SDK_START_MENU_VIEW_WORLD_SELECT,
    SDK_START_MENU_VIEW_WORLD_ACTIONS,
    SDK_START_MENU_VIEW_CHARACTERS,
    SDK_START_MENU_VIEW_CHARACTER_ACTIONS,
    SDK_START_MENU_VIEW_CHARACTER_ANIMATIONS,
    SDK_START_MENU_VIEW_ANIMATION_LIST,
    SDK_START_MENU_VIEW_ANIMATION_ACTIONS,
    SDK_START_MENU_VIEW_PROPS,
    SDK_START_MENU_VIEW_PROP_ACTIONS,
    SDK_START_MENU_VIEW_BLOCKS,
    SDK_START_MENU_VIEW_BLOCK_ACTIONS,
    SDK_START_MENU_VIEW_ITEMS,
    SDK_START_MENU_VIEW_ITEM_ACTIONS,
    SDK_START_MENU_VIEW_PARTICLE_EFFECTS,
    SDK_START_MENU_VIEW_PARTICLE_EFFECT_ACTIONS,
    SDK_START_MENU_VIEW_ONLINE,
    SDK_START_MENU_VIEW_ONLINE_EDIT_SERVER,
    SDK_START_MENU_VIEW_WORLD_CREATE,
    SDK_START_MENU_VIEW_WORLD_MAP_GENERATING,
    SDK_START_MENU_VIEW_WORLD_GENERATING,
    SDK_START_MENU_VIEW_RETURNING_TO_START
} SdkStartMenuView;

typedef struct {
    bool open;
    int  view;
    int  main_selected;
    int  world_selected;
    int  world_scroll;
    int  world_total_count;
    int  world_count;
    uint32_t world_seed[SDK_START_MENU_WORLD_VISIBLE_MAX];
    bool world_has_save[SDK_START_MENU_WORLD_VISIBLE_MAX];
    char world_name[SDK_START_MENU_WORLD_VISIBLE_MAX][SDK_START_MENU_WORLD_NAME_MAX];
    int  world_action_selected;
    bool world_action_can_play;
    bool world_action_can_toggle_host;
    bool world_action_can_generate_map;
    bool world_action_hosted_selected_world;
    uint32_t selected_world_seed;
    char selected_world_name[SDK_START_MENU_WORLD_NAME_MAX];
    int  online_section_focus;
    int  saved_server_selected;
    int  saved_server_scroll;
    int  saved_server_count;
    int  saved_server_visible_count;
    char saved_server_name[SDK_START_MENU_SERVER_VISIBLE_MAX][SDK_START_MENU_SERVER_NAME_MAX];
    char saved_server_address[SDK_START_MENU_SERVER_VISIBLE_MAX][SDK_START_MENU_SERVER_ADDRESS_MAX];
    int  hosted_server_selected;
    int  hosted_server_scroll;
    int  hosted_server_count;
    int  hosted_server_visible_count;
    uint32_t hosted_server_seed[SDK_START_MENU_SERVER_VISIBLE_MAX];
    char hosted_server_name[SDK_START_MENU_SERVER_VISIBLE_MAX][SDK_START_MENU_SERVER_NAME_MAX];
    char hosted_server_address[SDK_START_MENU_SERVER_VISIBLE_MAX][SDK_START_MENU_SERVER_ADDRESS_MAX];
    int  online_edit_selected;
    bool online_edit_is_new;
    char online_edit_name[SDK_START_MENU_SERVER_NAME_MAX];
    char online_edit_address[SDK_START_MENU_SERVER_ADDRESS_MAX];
    char status_text[SDK_SERVER_STATUS_TEXT_MAX];
    int  character_selected;
    int  character_scroll;
    int  character_count;
    char character_name[SDK_START_MENU_ASSET_VISIBLE_MAX][SDK_START_MENU_ASSET_NAME_MAX];
    int  character_action_selected;
    char selected_character_name[SDK_START_MENU_ASSET_NAME_MAX];
    int  animation_hub_selected;
    int  animation_selected;
    int  animation_scroll;
    int  animation_count;
    char animation_name[SDK_START_MENU_ASSET_VISIBLE_MAX][SDK_START_MENU_ASSET_NAME_MAX];
    int  animation_action_selected;
    char selected_animation_name[SDK_START_MENU_ASSET_NAME_MAX];
    int  prop_selected;
    int  prop_scroll;
    int  prop_count;
    char prop_name[SDK_START_MENU_ASSET_VISIBLE_MAX][SDK_START_MENU_ASSET_NAME_MAX];
    int  prop_action_selected;
    char selected_prop_name[SDK_START_MENU_ASSET_NAME_MAX];
    int  block_selected;
    int  block_scroll;
    int  block_count;
    char block_name[SDK_START_MENU_ASSET_VISIBLE_MAX][SDK_START_MENU_ASSET_NAME_MAX];
    int  block_action_selected;
    char selected_block_name[SDK_START_MENU_ASSET_NAME_MAX];
    int  item_selected;
    int  item_scroll;
    int  item_count;
    char item_name[SDK_START_MENU_ASSET_VISIBLE_MAX][SDK_START_MENU_ASSET_NAME_MAX];
    int  item_action_selected;
    char selected_item_name[SDK_START_MENU_ASSET_NAME_MAX];
    int  particle_effect_selected;
    int  particle_effect_scroll;
    int  particle_effect_count;
    char particle_effect_name[SDK_START_MENU_ASSET_VISIBLE_MAX][SDK_START_MENU_ASSET_NAME_MAX];
    int  particle_effect_action_selected;
    char selected_particle_effect_name[SDK_START_MENU_ASSET_NAME_MAX];
    /* World creation settings */
    uint32_t world_create_seed;
    int  world_create_render_distance;
    int  world_create_spawn_type;
    int  world_create_selected;
    bool world_create_settlements_enabled;
    bool world_create_construction_cells_enabled;
    uint8_t world_create_coordinate_system;
    bool world_create_superchunks_enabled;
    int  world_create_superchunk_chunk_span;
    bool world_create_walls_enabled;
    int  world_create_wall_grid_size;
    int  world_create_wall_grid_offset_x;
    int  world_create_wall_grid_offset_z;
    int  world_create_wall_thickness_blocks;
    bool world_create_wall_rings_shared;
    /* World generation progress */
    bool world_generating;
    float world_generation_progress;
    int   world_generation_stage; /* 0=init, 1=chunks, 2=entities, 3=complete */
    char  world_generation_status[256];
    bool world_map_generating;
    float world_map_generation_progress;
    int   world_map_generation_stage;
    int   world_map_generation_threads;
    int   world_map_generation_ring;
    int   world_map_generation_tiles_done;
    int   world_map_generation_inflight;
    int   world_map_generation_queued_pages;
    int   world_map_generation_active_workers;
    int   world_map_generation_last_tile_chunks;
    float world_map_generation_tiles_per_sec;
    float world_map_generation_last_save_age_seconds;
    float world_map_generation_last_tile_ms;
    char  world_map_generation_status[256];
    bool  world_gen_offline_generating;
    float world_gen_offline_progress;
    int   world_gen_offline_stage;
    int   world_gen_offline_threads;
    int   world_gen_offline_superchunks_done;
    int   world_gen_offline_current_ring;
    float world_gen_offline_chunks_per_sec;
    int   world_gen_offline_active_workers;
    char  world_gen_offline_status[128];
    /* World generation summary */
    bool  world_gen_summary_active;
    char  world_gen_summary_lines[16][128];
    int   world_gen_summary_line_count;
} SdkStartMenuUI;

/** Update startup/front-end menu state. */
void sdk_renderer_set_start_menu(const SdkStartMenuUI* ui);

typedef enum {
    SDK_EDITOR_UI_NONE = 0,
    SDK_EDITOR_UI_CHARACTER,
    SDK_EDITOR_UI_ANIMATION,
    SDK_EDITOR_UI_PROP,
    SDK_EDITOR_UI_BLOCK,
    SDK_EDITOR_UI_ITEM,
    SDK_EDITOR_UI_PARTICLE
} SdkEditorUIKind;

typedef struct {
    bool open;
    int  kind;
    char character_name[SDK_START_MENU_ASSET_NAME_MAX];
    char animation_name[SDK_START_MENU_ASSET_NAME_MAX];
    int  current_frame;
    int  frame_count;
    bool playback_enabled;
    char status[64];
} SdkEditorUI;

void sdk_renderer_set_editor(const SdkEditorUI* ui);

#define SDK_COMMAND_LINE_TEXT_MAX 128

typedef struct {
    bool open;
    char text[SDK_COMMAND_LINE_TEXT_MAX];
} SdkCommandLineUI;

/** Update command-line HUD state. */
void sdk_renderer_set_command_line(const SdkCommandLineUI* ui);

#define SDK_MAP_UI_MAX_DIM 64

typedef struct {
    bool     open;
    bool     focused;
    bool     focused_tile_ready;
    uint8_t  focused_tile_build_kind;
    uint8_t  focused_tile_compare_valid;
    uint16_t focused_tile_compare_diff_pixels;
    int      width;
    int      height;
    int      player_cell_x;
    int      player_cell_z;
    int      focus_cell_x;
    int      focus_cell_z;
    int      zoom_tenths;
    int      visible_exact_tiles;
    int      visible_fallback_tiles;
    uint32_t pixels[SDK_MAP_UI_MAX_DIM * SDK_MAP_UI_MAX_DIM];
} SdkMapUI;

/** Update top-down superchunk map HUD state. */
void sdk_renderer_set_map(const SdkMapUI* ui);

#define SDK_FLUID_DEBUG_TEXT_MAX 48

typedef enum {
    SDK_RENDERER_FLUID_DEBUG_IDLE = 0,
    SDK_RENDERER_FLUID_DEBUG_BULK_RESERVOIR,
    SDK_RENDERER_FLUID_DEBUG_LOCAL_WAKE
} SdkRendererFluidDebugMechanism;

typedef struct {
    bool open;
    int  mechanism;
    int  last_seed_wx;
    int  last_seed_wy;
    int  last_seed_wz;
    int  reservoir_columns;
    int  total_columns;
    int  worker_count;
    int  tick_processed;
    int  dirty_cells;
    int  active_chunks;
    int  stream_jobs;
    int  stream_results;
    int  total_volume;
    int  target_surface_e;
    int  solve_ms;
    int  primary_scx;
    int  primary_scz;
    int  desired_scx;
    int  desired_scz;
    int  transition_active;
    int  resident_primary;
    int  resident_frontier;
    int  resident_transition;
    int  resident_evict;
    char reason[SDK_FLUID_DEBUG_TEXT_MAX];
} SdkFluidDebugUI;

/** Update fluid-debug HUD state. */
void sdk_renderer_set_fluid_debug(const SdkFluidDebugUI* ui);

typedef struct {
    bool  open;
    int   working_set_mb;
    int   private_mb;
    int   peak_working_set_mb;
    int   system_memory_load_pct;
    float cpu_percent;
    int   stream_jobs;
    int   stream_results;
    int   sim_step_cells;
    int   sim_dirty_cells;
    int   sim_active_chunks;
    int   resident_chunks;
    int   desired_chunks;
    int   dirty_chunks;
    int   far_dirty_chunks;
    int   stalled_dirty_chunks;
    int   active_settlements;
    int   active_residents;
    int   settlement_scanned_chunks;
    int   settlement_initialized;
    int   settlement_block_mutations;
    int   primary_scx;
    int   primary_scz;
    int   desired_scx;
    int   desired_scz;
    int   transition_active;
    int   startup_safe_active;
    int   startup_primary_desired;
    int   startup_primary_resident;
    int   startup_primary_ready;
    int   startup_no_cpu_mesh;
    int   startup_upload_pending;
    int   startup_gpu_stale;
    int   startup_far_only;
} SdkPerfDebugUI;

/** Update performance/debug HUD state. */
void sdk_renderer_set_perf_debug(const SdkPerfDebugUI* ui);

#define SDK_SETTLEMENT_DEBUG_TEXT_MAX 128

typedef struct {
    bool open;
    char settlement[SDK_SETTLEMENT_DEBUG_TEXT_MAX];
    char location[SDK_SETTLEMENT_DEBUG_TEXT_MAX];
    char zone[SDK_SETTLEMENT_DEBUG_TEXT_MAX];
    char building[SDK_SETTLEMENT_DEBUG_TEXT_MAX];
    char metrics[SDK_SETTLEMENT_DEBUG_TEXT_MAX];
    char runtime[SDK_SETTLEMENT_DEBUG_TEXT_MAX];
    char resident[SDK_SETTLEMENT_DEBUG_TEXT_MAX];
    char assets[SDK_SETTLEMENT_DEBUG_TEXT_MAX];
    char note[SDK_SETTLEMENT_DEBUG_TEXT_MAX];
} SdkSettlementDebugUI;

void sdk_renderer_set_settlement_debug(const SdkSettlementDebugUI* ui);

/** Pass entity list to renderer for drawing item drops. */
#include "../Core/Entities/sdk_entity.h"
void sdk_renderer_set_entities(const SdkEntityList* entities);
void sdk_renderer_set_player_character_mesh(const BlockVertex* vertices, uint32_t vertex_count);
SdkResult sdk_renderer_request_screenshot(const char* output_path);
int sdk_renderer_poll_screenshot_result(SdkScreenshotResult* out_result);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_D3D12_RENDERER_H */
