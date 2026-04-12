/**
 * sdk_api.c - NQL SDK public C API implementation
 */
#include "./Internal/sdk_api_internal.h"
#include "./Internal/sdk_load_trace.h"
#include "../World/Chunks/ChunkStreamer/sdk_chunk_streamer.h"
#include "../World/Worldgen/sdk_worldgen.h"
#include "../World/Worldgen/Internal/sdk_worldgen_internal.h"
#include "../World/Worldgen/SharedCache/sdk_worldgen_shared_cache.h"
#include "../Benchmark/sdk_benchmark.h"
#include "../World/Persistence/sdk_persistence.h"
#include "../World/Settlements/sdk_settlement.h"
#include "../Crafting/sdk_crafting.h"

#include <stdarg.h>

/* ======================================================================
 * SHARED STATE
 * ====================================================================== */

SdkApiState g_sdk;
int g_chunk_grid_size_setting = CHUNK_GRID_DEFAULT_SIZE;
SdkGraphicsSettings g_graphics_settings;
SdkInputSettings g_input_settings;

SuperchunkMapCacheEntry g_superchunk_map_cache[HUD_MAP_CACHE_SLOTS];
uint64_t g_superchunk_map_stamp = 1;
SdkMapSchedulerInternal g_map_scheduler;

float g_cam_yaw = 0.0f;
float g_cam_pitch = 0.0f;
float g_cam_look_dist = 1.0f;
bool g_cam_rotation_initialized = false;

float g_vel_y = 0.0f;
bool g_on_ground = false;
bool g_test_flight_enabled = false;
bool g_space_was_down = false;
int g_space_tap_timer = 0;
bool g_worldgen_debug_was_down = false;
bool g_worldgen_chunk_report_was_down = false;
bool g_settlement_debug_was_down = false;
bool g_settlement_debug_overlay = false;
bool g_settlement_debug_cache_valid = false;
ULONGLONG g_settlement_debug_last_refresh_ms = 0u;
int g_settlement_debug_last_wx = 0;
int g_settlement_debug_last_wz = 0;
SdkSettlementDebugUI g_settlement_debug_cached_ui;
bool g_fluid_debug_was_down = false;
bool g_fluid_debug_overlay = false;
bool g_perf_debug_was_down = false;
bool g_perf_debug_overlay = false;

SdkPerfTelemetryState g_perf_telemetry = { false, 0u, 0u, 1, 0.0f };
int g_startup_safe_frames_remaining = STARTUP_SAFE_MODE_FRAMES;

int g_player_health = PLAYER_MAX_HEALTH;
int g_invincible_frames = 0;
float g_fall_start_y = 0.0f;
bool g_was_on_ground = true;
bool g_player_dead = false;
int g_death_timer = 0;
float g_spawn_x = 0.0f;
float g_spawn_y = 200.0f;
float g_spawn_z = 0.0f;

int g_player_hunger = PLAYER_MAX_HUNGER;
int g_hunger_tick = 0;

bool g_is_sprinting = false;
bool g_w_was_down = false;
int g_w_tap_timer = 0;
bool g_w_sprint_latched = false;
int g_mounted_vehicle_index = -1;
bool g_vehicle_use_was_down = false;
int g_weapon_use_cooldown = 0;
int g_screen_flash_timer = 0;
int g_screen_flash_duration = 0;
float g_screen_flash_strength = 0.0f;
float g_player_smoke_obscurance = 0.0f;

int g_world_time = 2000;
SdkSmokeCloud g_smoke_clouds[MAX_SMOKE_CLOUDS];

SdkWorldSaveMeta g_world_saves[SDK_WORLD_SAVE_MAX];
int g_world_save_count = 0;
int g_world_save_selected = 0;
int g_world_save_scroll = 0;
int g_world_action_selected = 0;
int g_frontend_main_selected = 0;
int g_character_menu_selected = 0;
int g_character_menu_scroll = 0;
int g_character_action_selected = 0;
int g_character_animation_menu_selected = 0;
int g_animation_menu_selected = 0;
int g_animation_menu_scroll = 0;
int g_animation_action_selected = 0;
int g_prop_menu_selected = 0;
int g_prop_menu_scroll = 0;
int g_prop_action_selected = 0;
int g_block_menu_selected = 0;
int g_block_menu_scroll = 0;
int g_block_action_selected = 0;
int g_item_menu_selected = 0;
int g_item_menu_scroll = 0;
int g_item_action_selected = 0;

/**
 * Returns human-readable string for settlement type enumeration value.
 *
 * @param type Settlement type enumeration value.
 * @return Human-readable string for the settlement type.
 */
static const char* settlement_type_name(SettlementType type)
{
    switch (type) {
        case SETTLEMENT_TYPE_VILLAGE: return "VILLAGE";
        case SETTLEMENT_TYPE_TOWN:    return "TOWN";
        case SETTLEMENT_TYPE_CITY:    return "CITY";
        default:                      return "NONE";
    }
}

/**
 * Returns human-readable string for settlement purpose enumeration value.
 *
 * @param purpose Settlement purpose enumeration value.
 * @return Human-readable string for the settlement purpose.
 */
static const char* settlement_purpose_name(SettlementPurpose purpose)
{
    switch (purpose) {
        case SETTLEMENT_PURPOSE_FARMING:    return "FARMING";
        case SETTLEMENT_PURPOSE_FISHING:    return "FISHING";
        case SETTLEMENT_PURPOSE_MINING:     return "MINING";
        case SETTLEMENT_PURPOSE_LOGISTICS:  return "LOGISTICS";
        case SETTLEMENT_PURPOSE_PROCESSING: return "PROCESSING";
        case SETTLEMENT_PURPOSE_PORT:       return "PORT";
        case SETTLEMENT_PURPOSE_CAPITAL:    return "CAPITAL";
        case SETTLEMENT_PURPOSE_FORTRESS:   return "FORTRESS";
        case SETTLEMENT_PURPOSE_HYDROCARBON:return "HYDROCARBON";
        case SETTLEMENT_PURPOSE_CEMENT:     return "CEMENT";
        case SETTLEMENT_PURPOSE_TIMBER:     return "TIMBER";
        default:                            return "UNKNOWN";
    }
}

/**
 * Returns human-readable string for settlement state enumeration value.
 *
 * @param state Settlement state enumeration value.
 * @return Human-readable string for the settlement state.
 */
static const char* settlement_state_name(SettlementState state)
{
    switch (state) {
        case SETTLEMENT_STATE_PRISTINE:   return "PRISTINE";
        case SETTLEMENT_STATE_DAMAGED:    return "DAMAGED";
        case SETTLEMENT_STATE_RUINS:      return "RUINS";
        case SETTLEMENT_STATE_ABANDONED:  return "ABANDONED";
        case SETTLEMENT_STATE_REBUILDING: return "REBUILDING";
        default:                          return "UNKNOWN";
    }
}

/**
 * Returns human-readable string for geographic variant enumeration value.
 *
 * @param variant Geographic variant enumeration value.
 * @return Human-readable string for the geographic variant.
 */
static const char* settlement_variant_name(GeographicVariant variant)
{
    switch (variant) {
        case GEOGRAPHIC_VARIANT_PLAINS:    return "PLAINS";
        case GEOGRAPHIC_VARIANT_COASTAL:   return "COASTAL";
        case GEOGRAPHIC_VARIANT_RIVERSIDE: return "RIVERSIDE";
        case GEOGRAPHIC_VARIANT_MOUNTAIN:  return "MOUNTAIN";
        case GEOGRAPHIC_VARIANT_DESERT:    return "DESERT";
        case GEOGRAPHIC_VARIANT_FOREST:    return "FOREST";
        case GEOGRAPHIC_VARIANT_JUNCTION:  return "JUNCTION";
        default:                           return "UNKNOWN";
    }
}

/**
 * Returns human-readable string for building zone type enumeration value.
 *
 * @param type Building zone type enumeration value.
 * @return Human-readable string for the building zone type.
 */
static const char* zone_type_name(BuildingZoneType type)
{
    switch (type) {
        case ZONE_TYPE_RESIDENTIAL:  return "RESIDENTIAL";
        case ZONE_TYPE_COMMERCIAL:   return "COMMERCIAL";
        case ZONE_TYPE_INDUSTRIAL:   return "INDUSTRIAL";
        case ZONE_TYPE_AGRICULTURAL: return "AGRICULTURAL";
        case ZONE_TYPE_DEFENSIVE:    return "DEFENSIVE";
        case ZONE_TYPE_CIVIC:        return "CIVIC";
        case ZONE_TYPE_HARBOR:       return "HARBOR";
        default:                     return "NONE";
    }
}

/**
 * Returns human-readable string for building type enumeration value.
 *
 * @param type Building type enumeration value.
 * @return Human-readable string for the building type.
 */
static const char* building_type_name(BuildingType type)
{
    switch (type) {
        case BUILDING_TYPE_HUT:         return "HUT";
        case BUILDING_TYPE_HOUSE:       return "HOUSE";
        case BUILDING_TYPE_MANOR:       return "MANOR";
        case BUILDING_TYPE_FARM:        return "FARM";
        case BUILDING_TYPE_BARN:        return "BARN";
        case BUILDING_TYPE_WORKSHOP:    return "WORKSHOP";
        case BUILDING_TYPE_FORGE:       return "FORGE";
        case BUILDING_TYPE_MILL:        return "MILL";
        case BUILDING_TYPE_STOREHOUSE:  return "STOREHOUSE";
        case BUILDING_TYPE_WAREHOUSE:   return "WAREHOUSE";
        case BUILDING_TYPE_SILO:        return "SILO";
        case BUILDING_TYPE_WATCHTOWER:  return "WATCHTOWER";
        case BUILDING_TYPE_BARRACKS:    return "BARRACKS";
        case BUILDING_TYPE_WALL_SECTION:return "WALL";
        case BUILDING_TYPE_WELL:        return "WELL";
        case BUILDING_TYPE_MARKET:      return "MARKET";
        case BUILDING_TYPE_DOCK:        return "DOCK";
        default:                        return "NONE";
    }
}

#define SETTLEMENT_DEBUG_REFRESH_MS 750u

static void build_settlement_debug_ui_snapshot(int query_wx, int query_wz, SdkSettlementDebugUI* out_ui)
{
    SettlementDebugInfo info;
    SdkSettlementRuntimeDebugInfo runtime_info;

    if (!out_ui) return;

    memset(out_ui, 0, sizeof(*out_ui));
    out_ui->open = true;

    memset(&info, 0, sizeof(info));
    memset(&runtime_info, 0, sizeof(runtime_info));
    sdk_settlement_query_debug_at(&g_sdk.worldgen, query_wx, query_wz, &info);
    sdk_settlement_runtime_query_debug(query_wx, query_wz, &runtime_info);
    if (info.found) {
        sprintf_s(out_ui->settlement, sizeof(out_ui->settlement), "#%u %s %s %s",
                  info.settlement_id,
                  settlement_type_name(info.type),
                  settlement_purpose_name(info.purpose),
                  settlement_variant_name(info.geographic_variant));
        sprintf_s(out_ui->location, sizeof(out_ui->location), "AT %d,%d  CTR %d,%d  RAD %u  %s",
                  query_wx, query_wz,
                  info.center_wx, info.center_wz,
                  (unsigned int)info.radius,
                  settlement_state_name(info.state));
        if (info.in_zone) {
            sprintf_s(out_ui->zone, sizeof(out_ui->zone), "ZONE %d  %s  BASE Y %d",
                      info.zone_index,
                      zone_type_name(info.zone_type),
                      (int)info.zone_base_elevation);
        } else {
            sprintf_s(out_ui->zone, sizeof(out_ui->zone), "NO ACTIVE ZONE AT %d,%d",
                      query_wx, query_wz);
        }
        if (info.in_building) {
            sprintf_s(out_ui->building, sizeof(out_ui->building), "BUILDING %d  %s  ORG %d,%d  %ux%ux%u",
                      info.building_index,
                      building_type_name(info.building_type),
                      info.building_wx, info.building_wz,
                      (unsigned int)info.building_footprint_x,
                      (unsigned int)info.building_footprint_z,
                      (unsigned int)info.building_height);
        } else {
            sprintf_s(out_ui->building, sizeof(out_ui->building), "NO BUILDING FOOTPRINT AT %d,%d",
                      query_wx, query_wz);
        }
        sprintf_s(out_ui->metrics, sizeof(out_ui->metrics), "INT %.2f  POP %u/%u  FOOD %.0f  OUT %.0f",
                  info.integrity,
                  (unsigned int)info.population,
                  (unsigned int)info.max_population,
                  info.food_production,
                  info.resource_output);
        if (runtime_info.found) {
            strcpy_s(out_ui->runtime, sizeof(out_ui->runtime), runtime_info.summary);
            strcpy_s(out_ui->resident, sizeof(out_ui->resident), runtime_info.resident);
            strcpy_s(out_ui->assets, sizeof(out_ui->assets), runtime_info.assets);
            sprintf_s(out_ui->note, sizeof(out_ui->note), "WORLD WATER %.2f  FERT %.2f  FLAT %.2f  DEF %.2f",
                      info.water_access,
                      info.fertility,
                      info.flatness,
                      info.defensibility);
        } else {
            sprintf_s(out_ui->runtime, sizeof(out_ui->runtime), "RUNTIME INACTIVE  VISIT LOADED CHUNKS TO SPAWN RESIDENTS");
            sprintf_s(out_ui->resident, sizeof(out_ui->resident), "NO LOADED RESIDENTS AT %d,%d", query_wx, query_wz);
            sprintf_s(out_ui->assets, sizeof(out_ui->assets), "PROP FALLBACKS ONLY UNTIL RUNTIME LOADS THIS SETTLEMENT");
            sprintf_s(out_ui->note, sizeof(out_ui->note), "WORLD WATER %.2f  FERT %.2f  FLAT %.2f  DEF %.2f",
                      info.water_access,
                      info.fertility,
                      info.flatness,
                      info.defensibility);
        }
    } else {
        sprintf_s(out_ui->settlement, sizeof(out_ui->settlement), "NO SETTLEMENT");
        sprintf_s(out_ui->location, sizeof(out_ui->location), "AT %d,%d", query_wx, query_wz);
        sprintf_s(out_ui->zone, sizeof(out_ui->zone), "NO ACTIVE SETTLEMENT UNDER CAMERA");
        sprintf_s(out_ui->building, sizeof(out_ui->building), "NO BUILDING FOOTPRINT");
        sprintf_s(out_ui->metrics, sizeof(out_ui->metrics), "MOVE INTO A GENERATED SETTLEMENT");
        sprintf_s(out_ui->runtime, sizeof(out_ui->runtime), "LOADED SETTLEMENT RUNTIME ONLY ACTIVATES INSIDE GENERATED SITES");
        sprintf_s(out_ui->resident, sizeof(out_ui->resident), "NO LOADED RESIDENT");
        sprintf_s(out_ui->assets, sizeof(out_ui->assets), "CHARACTER/PROP WARNINGS APPEAR HERE WHEN A LIVE SETTLEMENT IS ACTIVE");
        sprintf_s(out_ui->note, sizeof(out_ui->note), "QUERY RATE LIMITED TO 0.75S");
    }
}

static void refresh_settlement_debug_snapshot(int query_wx, int query_wz)
{
    build_settlement_debug_ui_snapshot(query_wx, query_wz, &g_settlement_debug_cached_ui);
    g_settlement_debug_cache_valid = true;
    g_settlement_debug_last_refresh_ms = GetTickCount64();
    g_settlement_debug_last_wx = query_wx;
    g_settlement_debug_last_wz = query_wz;
}
int g_particle_effect_menu_selected = 0;
int g_particle_effect_menu_scroll = 0;
int g_particle_effect_action_selected = 0;
int g_selected_character_index = -1;
int g_selected_animation_index = -1;
int g_selected_prop_index = -1;
int g_selected_block_index = -1;
int g_selected_item_index = -1;
int g_selected_particle_effect_index = -1;
int g_frontend_view = SDK_START_MENU_VIEW_MAIN;
bool g_frontend_nav_was_down[8];
bool g_frontend_refresh_pending = true;
SdkCharacterAssetMeta g_character_assets[SDK_CHARACTER_ASSET_MAX];
int g_character_asset_count = 0;
SdkAnimationAssetMeta g_animation_assets[SDK_ANIMATION_ASSET_MAX];
int g_animation_asset_count = 0;
SdkPropAssetMeta g_prop_assets[SDK_PROP_ASSET_MAX];
int g_prop_asset_count = 0;
SdkBlockAssetMeta g_block_assets[SDK_BLOCK_ASSET_MAX];
int g_block_asset_count = 0;
SdkItemAssetMeta g_item_assets[SDK_ITEM_ASSET_MAX];
int g_item_asset_count = 0;
SdkParticleEffectAssetMeta g_particle_effect_assets[SDK_PARTICLE_EFFECT_ASSET_MAX];
int g_particle_effect_asset_count = 0;
SdkSessionKind g_session_kind = SDK_SESSION_KIND_WORLD;
SdkEditorSession g_editor_session;

int g_world_create_selected = 0;
uint32_t g_world_create_seed = 0;
int g_world_create_render_distance = 8;
int g_world_create_spawn_type = 0;
int g_world_create_settlements_enabled = 1;
int g_world_create_construction_cells_enabled = 0;
int g_world_create_coordinate_system = SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
int g_world_create_superchunks_enabled = 1;
int g_world_create_superchunk_chunk_span = 16;
int g_world_create_walls_enabled = 1;
int g_world_create_wall_grid_size = 18;
int g_world_create_wall_grid_offset_x = 0;
int g_world_create_wall_grid_offset_z = 0;
int g_world_create_wall_thickness_blocks = CHUNK_WIDTH;
int g_world_create_wall_rings_shared = 1;

bool g_world_generation_active = false;
bool g_world_generation_cancel_requested = false;
float g_world_generation_progress = 0.0f;
int g_world_generation_stage = 0;
char g_world_generation_status[256] = "";
SdkWorldSaveMeta g_world_generation_target;
int g_world_generation_chunks_total = 0;
int g_world_generation_chunks_done = 0;
bool g_world_map_generation_active = false;
float g_world_map_generation_progress = 0.0f;
int g_world_map_generation_stage = 0;
char g_world_map_generation_status[256] = "";
SdkWorldSaveMeta g_world_map_generation_target;
int g_world_map_generation_threads = SDK_OFFLINE_MAP_GENERATION_THREADS;
int g_world_map_generation_ring = 0;
int g_world_map_generation_tiles_done = 0;

uint64_t g_world_generation_started_ms = 0u;
uint64_t g_world_generation_last_sample_ms = 0u;
int g_world_generation_last_sample_chunks = 0;
int g_world_generation_active_workers = 0;
float g_world_generation_chunks_per_sec = 0.0f;
int g_world_generation_threads = SDK_OFFLINE_MAP_GENERATION_THREADS;
int g_world_generation_superchunks_done = 0;
int g_world_generation_current_ring = 0;
SdkSavedServerEntry g_saved_servers[SDK_SAVED_SERVER_MAX];
int g_saved_server_count = 0;
int g_saved_server_selected = 0;
int g_saved_server_scroll = 0;
int g_hosted_server_selected = 0;
int g_hosted_server_scroll = 0;
int g_online_section_focus = 0;
int g_online_edit_selected = 0;
int g_online_edit_target_index = -1;
bool g_online_edit_is_new = true;
char g_online_edit_name[SDK_START_MENU_SERVER_NAME_MAX] = "";
char g_online_edit_address[SDK_START_MENU_SERVER_ADDRESS_MAX] = "";
char g_online_status[SDK_SERVER_STATUS_TEXT_MAX] = "";
bool g_frontend_forced_open = false;
int g_pending_world_launch_mode = SDK_WORLD_LAUNCH_STANDARD;
SdkLocalHostManager g_local_host_manager;
SdkClientConnection g_client_connection;

static char g_load_trace_path[MAX_PATH] = "";
static char g_load_trace_world_id[64] = "";
static char g_load_trace_world_dir[MAX_PATH] = "";
static uint64_t g_load_trace_started_ms = 0u;
static char g_debug_log_path[MAX_PATH] = "";
static int g_debug_log_initialized = 0;

#define SDK_PENDING_DEBUG_LOG_CAPACITY (256u * 1024u)
static char g_pending_debug_log[SDK_PENDING_DEBUG_LOG_CAPACITY];
static size_t g_pending_debug_log_len = 0u;

static void sdk_debug_log_buffer_text(const char* text)
{
    size_t text_len;
    size_t space_remaining;

    if (!text || !text[0]) return;
    text_len = strlen(text);
    if (text_len == 0u) return;
    if (g_pending_debug_log_len >= SDK_PENDING_DEBUG_LOG_CAPACITY) return;

    space_remaining = SDK_PENDING_DEBUG_LOG_CAPACITY - g_pending_debug_log_len;
    if (space_remaining <= 1u) return;
    if (text_len >= space_remaining) {
        text_len = space_remaining - 1u;
    }

    memcpy(g_pending_debug_log + g_pending_debug_log_len, text, text_len);
    g_pending_debug_log_len += text_len;
    g_pending_debug_log[g_pending_debug_log_len] = '\0';
}

static void sdk_debug_log_append_raw(const char* text)
{
    FILE* file;

    if (!text || !text[0]) return;
    if (!g_debug_log_initialized || !g_debug_log_path[0]) {
        sdk_debug_log_buffer_text(text);
        return;
    }

    file = fopen(g_debug_log_path, "ab");
    if (!file) {
        sdk_debug_log_buffer_text(text);
        return;
    }

    fwrite(text, 1u, strlen(text), file);
    fflush(file);
    fclose(file);
}

static int sdk_debug_log_resolve_world_path(const char* world_id,
                                            const char* world_dir,
                                            char* out_path,
                                            size_t out_path_len)
{
    SdkWorldTarget target;

    if (!out_path || out_path_len == 0u) return 0;
    out_path[0] = '\0';

    if (!sdk_world_target_resolve(world_id, world_dir, &target)) return 0;
    if (!target.world_dir[0]) return 0;
    if (!sdk_world_ensure_directory_exists(target.world_dir)) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", target.world_dir, SDK_WORLD_DEBUG_LOG_NAME) > 0;
}

static void sdk_debug_log_write_session_header(const char* world_id,
                                               const char* world_dir)
{
    SYSTEMTIME st;
    char header[768];
    int written;
    const char* resolved_world_id = world_id && world_id[0] ? world_id : "";
    const char* resolved_world_dir = world_dir && world_dir[0] ? world_dir : "";

    GetLocalTime(&st);
    written = snprintf(header, sizeof(header),
                       "=== NQL SDK Session Debug Log ===\n"
                       "started_local=%04u-%02u-%02u %02u:%02u:%02u.%03u\n"
                       "world_id=%s\n"
                       "world_dir=%s\n"
                       "trace_jsonl=%s\n"
                       "---\n",
                       (unsigned int)st.wYear,
                       (unsigned int)st.wMonth,
                       (unsigned int)st.wDay,
                       (unsigned int)st.wHour,
                       (unsigned int)st.wMinute,
                       (unsigned int)st.wSecond,
                       (unsigned int)st.wMilliseconds,
                       resolved_world_id,
                       resolved_world_dir,
                       g_load_trace_path);
    if (written > 0) {
        sdk_debug_log_append_raw(header);
    }
}

static void sdk_debug_log_activate_world_file(const char* world_id,
                                              const char* world_dir)
{
    char resolved_path[MAX_PATH];
    SdkWorldTarget target;
    FILE* file;

    if (!sdk_debug_log_resolve_world_path(world_id, world_dir, resolved_path, sizeof(resolved_path))) {
        return;
    }

    if (g_debug_log_initialized &&
        _stricmp(g_debug_log_path, resolved_path) == 0) {
        return;
    }

    strncpy_s(g_debug_log_path, sizeof(g_debug_log_path), resolved_path, _TRUNCATE);
    g_debug_log_initialized = 1;

    file = fopen(g_debug_log_path, "wb");
    if (file) {
        fclose(file);
    }

    if (sdk_world_target_resolve(world_id, world_dir, &target) && target.world_dir[0]) {
        strncpy_s(g_load_trace_world_dir, sizeof(g_load_trace_world_dir), target.world_dir, _TRUNCATE);
    }

    sdk_debug_log_write_session_header(world_id, g_load_trace_world_dir);
    if (g_pending_debug_log_len > 0u) {
        sdk_debug_log_append_raw(g_pending_debug_log);
        g_pending_debug_log_len = 0u;
        g_pending_debug_log[0] = '\0';
    }
}

static void sdk_debug_log_write_trace_line(const char* event_name,
                                           const char* detail,
                                           int include_state,
                                           int frontend_view,
                                           int world_session_active,
                                           int world_generation_active,
                                           int world_generation_stage,
                                           int include_readiness,
                                           int desired_primary,
                                           int resident_primary,
                                           int gpu_ready_primary,
                                           int pending_jobs,
                                           int pending_results,
                                           int pending_uploads)
{
    char line[1024];
    uint64_t now_ms = GetTickCount64();
    int written;

    written = snprintf(line, sizeof(line),
                       "[TRACE] ms=%llu event=%s",
                       (unsigned long long)(g_load_trace_started_ms > 0u
                                                ? (now_ms - g_load_trace_started_ms)
                                                : 0u),
                       event_name ? event_name : "");
    if (written < 0) return;

    if (detail && detail[0] && (size_t)written < sizeof(line)) {
        written += snprintf(line + written, sizeof(line) - (size_t)written,
                            " detail=%s", detail);
    }
    if (include_state && (size_t)written < sizeof(line)) {
        written += snprintf(line + written, sizeof(line) - (size_t)written,
                            " frontend=%d session=%d worldgen_active=%d stage=%d",
                            frontend_view,
                            world_session_active,
                            world_generation_active,
                            world_generation_stage);
    }
    if (include_readiness && (size_t)written < sizeof(line)) {
        written += snprintf(line + written, sizeof(line) - (size_t)written,
                            " desired=%d resident=%d gpu_ready=%d jobs=%d results=%d uploads=%d",
                            desired_primary,
                            resident_primary,
                            gpu_ready_primary,
                            pending_jobs,
                            pending_results,
                            pending_uploads);
    }
    if (written < 0) return;
    if ((size_t)written >= sizeof(line) - 1u) {
        line[sizeof(line) - 2u] = '\n';
        line[sizeof(line) - 1u] = '\0';
    } else {
        line[written++] = '\n';
        line[written] = '\0';
    }
    sdk_debug_log_append_raw(line);
}

static void sdk_load_trace_fprint_json_string(FILE* file, const char* text)
{
    const char* cursor = text ? text : "";

    if (!file) return;
    fputc('"', file);
    while (*cursor) {
        char ch = *cursor++;
        switch (ch) {
            case '\\': fputs("\\\\", file); break;
            case '"':  fputs("\\\"", file); break;
            case '\n': fputs("\\n", file); break;
            case '\r': fputs("\\r", file); break;
            case '\t': fputs("\\t", file); break;
            default:
                if ((unsigned char)ch < 0x20u) {
                    fprintf(file, "\\u%04x", (unsigned char)ch);
                } else {
                    fputc(ch, file);
                }
                break;
        }
    }
    fputc('"', file);
}

static void sdk_load_trace_ensure_default_path(void)
{
    SYSTEMTIME st;

    if (g_load_trace_path[0]) return;
    sdk_world_ensure_directory_exists("DebugRuns");
    GetLocalTime(&st);
    snprintf(g_load_trace_path, sizeof(g_load_trace_path),
             "DebugRuns\\live_world_creation_%04u%02u%02u_%02u%02u%02u.jsonl",
             (unsigned int)st.wYear,
             (unsigned int)st.wMonth,
             (unsigned int)st.wDay,
             (unsigned int)st.wHour,
             (unsigned int)st.wMinute,
             (unsigned int)st.wSecond);
    g_load_trace_started_ms = GetTickCount64();
}

static void sdk_load_trace_write_event(const char* event_name,
                                       const char* detail,
                                       int include_state,
                                       int frontend_view,
                                       int world_session_active,
                                       int world_generation_active,
                                       int world_generation_stage,
                                       int include_readiness,
                                       int desired_primary,
                                       int resident_primary,
                                       int gpu_ready_primary,
                                       int pending_jobs,
                                       int pending_results,
                                       int pending_uploads)
{
    FILE* file;
    uint64_t now_ms;

    sdk_load_trace_ensure_default_path();
    if (!g_load_trace_path[0]) return;

    file = fopen(g_load_trace_path, "ab");
    if (!file) return;

    now_ms = GetTickCount64();
    fprintf(file, "{\"ms\":%llu,\"event\":",
            (unsigned long long)(g_load_trace_started_ms > 0u
                                     ? (now_ms - g_load_trace_started_ms)
                                     : 0u));
    sdk_load_trace_fprint_json_string(file, event_name ? event_name : "");
    fprintf(file, ",\"world_id\":");
    sdk_load_trace_fprint_json_string(file, g_load_trace_world_id);
    fprintf(file, ",\"world_dir\":");
    sdk_load_trace_fprint_json_string(file, g_load_trace_world_dir);
    if (detail && detail[0]) {
        fprintf(file, ",\"detail\":");
        sdk_load_trace_fprint_json_string(file, detail);
    }
    if (include_state) {
        fprintf(file,
                ",\"frontend_view\":%d,\"world_session_active\":%s,"
                "\"world_generation_active\":%s,\"world_generation_stage\":%d",
                frontend_view,
                world_session_active ? "true" : "false",
                world_generation_active ? "true" : "false",
                world_generation_stage);
    }
    if (include_readiness) {
        fprintf(file,
                ",\"readiness\":{\"desired_primary\":%d,\"resident_primary\":%d,"
                "\"gpu_ready_primary\":%d,\"pending_jobs\":%d,\"pending_results\":%d,"
                "\"pending_uploads\":%d}",
                desired_primary,
                resident_primary,
                gpu_ready_primary,
                pending_jobs,
                pending_results,
                pending_uploads);
    }
    fprintf(file, "}\n");
    fclose(file);

    sdk_debug_log_write_trace_line(event_name,
                                   detail,
                                   include_state,
                                   frontend_view,
                                   world_session_active,
                                   world_generation_active,
                                   world_generation_stage,
                                   include_readiness,
                                   desired_primary,
                                   resident_primary,
                                   gpu_ready_primary,
                                   pending_jobs,
                                   pending_results,
                                   pending_uploads);
}

void sdk_load_trace_reset(const char* reason)
{
    g_load_trace_path[0] = '\0';
    g_load_trace_world_id[0] = '\0';
    g_load_trace_world_dir[0] = '\0';
    g_debug_log_path[0] = '\0';
    g_debug_log_initialized = 0;
    g_pending_debug_log_len = 0u;
    g_pending_debug_log[0] = '\0';
    sdk_load_trace_ensure_default_path();
    if (!g_load_trace_path[0]) return;
    {
        FILE* file = fopen(g_load_trace_path, "wb");
        if (file) fclose(file);
    }
    sdk_load_trace_write_event("trace_reset",
                               reason && reason[0] ? reason : "reset",
                               1,
                               g_frontend_view,
                               g_sdk.world_session_active ? 1 : 0,
                               g_world_generation_active ? 1 : 0,
                               g_world_generation_stage,
                               0, 0, 0, 0, 0, 0, 0);
}

void sdk_load_trace_bind_world(const char* world_id, const char* world_dir)
{
    sdk_load_trace_ensure_default_path();
    if (world_id && world_id[0]) {
        strncpy_s(g_load_trace_world_id, sizeof(g_load_trace_world_id), world_id, _TRUNCATE);
    }
    if (world_dir && world_dir[0]) {
        strncpy_s(g_load_trace_world_dir, sizeof(g_load_trace_world_dir), world_dir, _TRUNCATE);
    }
    sdk_debug_log_activate_world_file(g_load_trace_world_id, g_load_trace_world_dir);
    sdk_load_trace_write_event("bind_world",
                               NULL,
                               1,
                               g_frontend_view,
                               g_sdk.world_session_active ? 1 : 0,
                               g_world_generation_active ? 1 : 0,
                               g_world_generation_stage,
                               0, 0, 0, 0, 0, 0, 0);
}

void sdk_load_trace_bind_meta(const SdkWorldSaveMeta* meta)
{
    if (!meta) return;
    if (meta->folder_id[0]) {
        sdk_load_trace_bind_world(meta->folder_id, NULL);
    } else {
        sdk_load_trace_bind_world(NULL, meta->save_path);
    }
}

void sdk_load_trace_note(const char* event_name, const char* detail)
{
    sdk_load_trace_write_event(event_name,
                               detail,
                               1,
                               g_frontend_view,
                               g_sdk.world_session_active ? 1 : 0,
                               g_world_generation_active ? 1 : 0,
                               g_world_generation_stage,
                               0, 0, 0, 0, 0, 0, 0);
}

void sdk_load_trace_note_state(const char* event_name,
                               int frontend_view,
                               int world_session_active,
                               int world_generation_active,
                               int world_generation_stage,
                               const char* detail)
{
    sdk_load_trace_write_event(event_name,
                               detail,
                               1,
                               frontend_view,
                               world_session_active,
                               world_generation_active,
                               world_generation_stage,
                               0, 0, 0, 0, 0, 0, 0);
}

void sdk_load_trace_note_readiness(const char* event_name,
                                   int desired_primary,
                                   int resident_primary,
                                   int gpu_ready_primary,
                                   int pending_jobs,
                                   int pending_results,
                                   int pending_uploads,
                                   const char* detail)
{
    sdk_load_trace_write_event(event_name,
                               detail,
                               1,
                               g_frontend_view,
                               g_sdk.world_session_active ? 1 : 0,
                               g_world_generation_active ? 1 : 0,
                               g_world_generation_stage,
                               1,
                               desired_primary,
                               resident_primary,
                               gpu_ready_primary,
                               pending_jobs,
                               pending_results,
                               pending_uploads);
}

void sdk_debug_log_append(const char* text)
{
    sdk_debug_log_append_raw(text);
}

void sdk_debug_log_output(const char* text)
{
    if (!text || !text[0]) return;
    OutputDebugStringA(text);
    sdk_debug_log_append_raw(text);
}

void sdk_debug_log_printf(const char* fmt, ...)
{
    char buffer[1024];
    va_list args;
    int written;

    if (!fmt || !fmt[0]) return;
    va_start(args, fmt);
    written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written <= 0) return;
    buffer[sizeof(buffer) - 1u] = '\0';
    sdk_debug_log_output(buffer);
}

const char* sdk_debug_log_path(void)
{
    return g_debug_log_initialized ? g_debug_log_path : "";
}

const char* sdk_load_trace_path(void)
{
    sdk_load_trace_ensure_default_path();
    return g_load_trace_path;
}

int sdk_load_trace_frontend_view(void)
{
    return g_frontend_view;
}

int sdk_load_trace_world_session_active(void)
{
    return g_sdk.world_session_active ? 1 : 0;
}

int sdk_load_trace_world_generation_active(void)
{
    return g_world_generation_active ? 1 : 0;
}

int sdk_load_trace_world_generation_stage(void)
{
    return g_world_generation_stage;
}

static void collect_chunk_role_counts(const SdkChunkManager* cm,
                                      int* out_desired_primary,
                                      int* out_desired_frontier,
                                      int* out_desired_transition,
                                      int* out_resident_primary,
                                      int* out_resident_frontier,
                                      int* out_resident_transition,
                                      int* out_resident_evict)
{
    /* Counts chunks by their residency role (primary, frontier, transition, evict) */
    int i;

    if (out_desired_primary) *out_desired_primary = 0;
    if (out_desired_frontier) *out_desired_frontier = 0;
    if (out_desired_transition) *out_desired_transition = 0;
    if (out_resident_primary) *out_resident_primary = 0;
    if (out_resident_frontier) *out_resident_frontier = 0;
    if (out_resident_transition) *out_resident_transition = 0;
    if (out_resident_evict) *out_resident_evict = 0;
    if (!cm) return;

    for (i = 0; i < sdk_chunk_manager_desired_count(cm); ++i) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(cm, i);
        if (!target) continue;
        switch ((SdkChunkResidencyRole)target->role) {
            case SDK_CHUNK_ROLE_PRIMARY:
                if (out_desired_primary) (*out_desired_primary)++;
                break;
            case SDK_CHUNK_ROLE_WALL_SUPPORT:
            case SDK_CHUNK_ROLE_FRONTIER:
                if (out_desired_frontier) (*out_desired_frontier)++;
                break;
            case SDK_CHUNK_ROLE_TRANSITION_PRELOAD:
                if (out_desired_transition) (*out_desired_transition)++;
                break;
            default:
                break;
        }
    }

    for (i = 0; i < sdk_chunk_manager_slot_capacity(); ++i) {
        const SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at_const(cm, i);
        if (!slot || !slot->occupied) continue;
        switch ((SdkChunkResidencyRole)slot->role) {
            case SDK_CHUNK_ROLE_PRIMARY:
                if (out_resident_primary) (*out_resident_primary)++;
                break;
            case SDK_CHUNK_ROLE_WALL_SUPPORT:
            case SDK_CHUNK_ROLE_FRONTIER:
                if (out_resident_frontier) (*out_resident_frontier)++;
                break;
            case SDK_CHUNK_ROLE_TRANSITION_PRELOAD:
                if (out_resident_transition) (*out_resident_transition)++;
                break;
            case SDK_CHUNK_ROLE_EVICT_PENDING:
                if (out_resident_evict) (*out_resident_evict)++;
                break;
            default:
                break;
        }
    }
}

static void debug_log_chunk_residency_state(const char* reason)
{
    /* Logs current chunk residency state for debugging chunk loading issues */
    char dbg[512];
    int desired_primary;
    int desired_frontier;
    int desired_transition;
    int resident_primary;
    int resident_frontier;
    int resident_transition;
    int resident_evict;
    int pending_jobs;
    int pending_results;
    int superchunk_mode;

    if (!g_sdk.world_session_active && !g_sdk.chunks_initialized) return;

    collect_chunk_role_counts(&g_sdk.chunk_mgr,
                              &desired_primary, &desired_frontier, &desired_transition,
                              &resident_primary, &resident_frontier, &resident_transition, &resident_evict);
    pending_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
    pending_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
    superchunk_mode =
        sdk_chunk_manager_radius_from_grid_size(sdk_chunk_manager_grid_size(&g_sdk.chunk_mgr)) >=
        SDK_SUPERCHUNK_CHUNK_SPAN;

    sprintf_s(dbg, sizeof(dbg),
              "[RESIDENCY] %s mode=%s desired=%d(P%d/F%d/T%d) resident=%d(P%d/F%d/T%d/E%d) "
              "jobs=%d results=%d primary=(%d,%d) desired_sc=(%d,%d) transition=%d\n",
              reason ? reason : "STATE",
              superchunk_mode ? "superchunk" : "window",
              sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr),
              desired_primary, desired_frontier, desired_transition,
              sdk_chunk_manager_active_count(&g_sdk.chunk_mgr),
              resident_primary, resident_frontier, resident_transition, resident_evict,
              pending_jobs, pending_results,
              g_sdk.chunk_mgr.primary_scx, g_sdk.chunk_mgr.primary_scz,
              g_sdk.chunk_mgr.desired_scx, g_sdk.chunk_mgr.desired_scz,
              g_sdk.chunk_mgr.transition_active ? 1 : 0);
    sdk_debug_log_output(dbg);
}

typedef struct {
    int desired;
    int resident;
    int gpu_ready;
} WallSideHealth;

#define SDK_WALL_HEALTH_FACE_WEST  (1u << 0)
#define SDK_WALL_HEALTH_FACE_NORTH (1u << 1)
#define SDK_WALL_HEALTH_FACE_EAST  (1u << 2)
#define SDK_WALL_HEALTH_FACE_SOUTH (1u << 3)

static int chunk_has_gpu_mesh_buffers(const SdkChunk* chunk)
{
    /* Returns 1 if chunk has any GPU mesh buffers ready for rendering */
    int sub_index;

    if (!chunk) return 0;
    if (sdk_chunk_has_current_unified_gpu_mesh(chunk) && chunk->unified_total_vertices > 0) {
        return 1;
    }
    for (sub_index = 0; sub_index < CHUNK_SUBCHUNK_COUNT; ++sub_index) {
        if (chunk->subchunks[sub_index].vertex_buffer &&
            chunk->subchunks[sub_index].vertex_count > 0) {
            return 1;
        }
        if (chunk->water_subchunks[sub_index].vertex_buffer &&
            chunk->water_subchunks[sub_index].vertex_count > 0) {
            return 1;
        }
    }
    if (chunk->far_mesh.vertex_buffer && chunk->far_mesh.vertex_count > 0) return 1;
    if (chunk->experimental_far_mesh.vertex_buffer && chunk->experimental_far_mesh.vertex_count > 0) return 1;
    if (chunk->far_exact_overlay_mesh.vertex_buffer && chunk->far_exact_overlay_mesh.vertex_count > 0) return 1;
    if (chunk->empty && chunk->blocks && !chunk->upload_pending && !sdk_chunk_needs_remesh(chunk)) {
        return 1;
    }
    return 0;
}

static void wall_health_add_chunk(WallSideHealth* side, int cx, int cz)
{
    /* Adds chunk to wall health tracking, counting desired/resident/GPU-ready states */
    SdkChunk* chunk;

    if (!side) return;
    side->desired++;
    chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (!chunk) return;
    side->resident++;
    if (sdk_superchunk_active_wall_chunk_contains_chunk(g_sdk.chunk_mgr.primary_scx,
                                                        g_sdk.chunk_mgr.primary_scz,
                                                        cx,
                                                        cz)) {
        if (sdk_chunk_is_active_wall_chunk_fully_ready(&g_sdk.chunk_mgr, chunk)) {
            side->gpu_ready++;
        }
    } else if (chunk_has_gpu_mesh_buffers(chunk)) {
        side->gpu_ready++;
    }
}

static void wall_health_add_state(WallSideHealth* side,
                                  int cx,
                                  int cz,
                                  int ready)
{
    SdkChunk* chunk;

    if (!side) return;
    side->desired++;
    chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (!chunk) return;
    side->resident++;
    if (ready) {
        side->gpu_ready++;
    }
}

static int collect_grid_wall_health(WallSideHealth* west,
                                    WallSideHealth* north,
                                    WallSideHealth* east,
                                    WallSideHealth* south)
{
    int box_origin_x;
    int box_origin_z;
    int period;

    if (west) memset(west, 0, sizeof(*west));
    if (north) memset(north, 0, sizeof(*north));
    if (east) memset(east, 0, sizeof(*east));
    if (south) memset(south, 0, sizeof(*south));

    if (!g_sdk.chunks_initialized) return 0;
    if (!sdk_world_walls_enabled()) return 0;
    if (!sdk_world_walls_uses_grid_space()) return 0;

    box_origin_x = sdk_world_walls_get_box_origin(g_sdk.chunk_mgr.cam_cx);
    box_origin_z = sdk_world_walls_get_box_origin(g_sdk.chunk_mgr.cam_cz);
    period = sdk_world_walls_get_period();

    for (int i = 0; i < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++i) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, i);
        int owner_origin_x = 0;
        int owner_origin_z = 0;
        uint8_t wall_mask = 0u;
        SdkChunk* chunk;
        int ready;

        if (!target) continue;
        if ((SdkCoordinateSpaceType)target->space_type != SDK_SPACE_WALL_GRID) continue;
        if (!sdk_world_walls_get_canonical_wall_chunk_owner(target->cx,
                                                            target->cz,
                                                            &wall_mask,
                                                            &owner_origin_x,
                                                            &owner_origin_z,
                                                            NULL,
                                                            NULL)) {
            continue;
        }

        chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz);
        ready = (chunk && sdk_chunk_has_full_upload_ready_mesh(chunk)) ? 1 : 0;

        if ((wall_mask & SDK_WALL_HEALTH_FACE_WEST) &&
            owner_origin_x == box_origin_x &&
            owner_origin_z == box_origin_z) {
            wall_health_add_state(west, target->cx, target->cz, ready);
        }
        if ((wall_mask & SDK_WALL_HEALTH_FACE_NORTH) &&
            owner_origin_x == box_origin_x &&
            owner_origin_z == box_origin_z) {
            wall_health_add_state(north, target->cx, target->cz, ready);
        }
        if (((wall_mask & SDK_WALL_HEALTH_FACE_WEST) &&
             owner_origin_x == box_origin_x + period &&
             owner_origin_z == box_origin_z) ||
            ((wall_mask & SDK_WALL_HEALTH_FACE_EAST) &&
             owner_origin_x == box_origin_x &&
             owner_origin_z == box_origin_z)) {
            wall_health_add_state(east, target->cx, target->cz, ready);
        }
        if (((wall_mask & SDK_WALL_HEALTH_FACE_NORTH) &&
             owner_origin_x == box_origin_x &&
             owner_origin_z == box_origin_z + period) ||
            ((wall_mask & SDK_WALL_HEALTH_FACE_SOUTH) &&
             owner_origin_x == box_origin_x &&
             owner_origin_z == box_origin_z)) {
            wall_health_add_state(south, target->cx, target->cz, ready);
        }
    }

    return 1;
}

static int collect_active_wall_health(WallSideHealth* west,
                                      WallSideHealth* north,
                                      WallSideHealth* east,
                                      WallSideHealth* south)
{
    /* Collects health status of active wall chunks for all four cardinal directions */
    int run;
    int cx;
    int cz;

    if (west) memset(west, 0, sizeof(*west));
    if (north) memset(north, 0, sizeof(*north));
    if (east) memset(east, 0, sizeof(*east));
    if (south) memset(south, 0, sizeof(*south));

    if (!g_sdk.chunks_initialized) return 0;
    if (collect_grid_wall_health(west, north, east, south)) {
        return 1;
    }
    if (sdk_chunk_manager_radius_from_grid_size(sdk_chunk_manager_grid_size(&g_sdk.chunk_mgr)) <
        SDK_SUPERCHUNK_CHUNK_SPAN) {
        return 0;
    }

    for (run = 0; run < SDK_SUPERCHUNK_CHUNK_SPAN; ++run) {
        sdk_superchunk_wall_edge_chunk_for_run(g_sdk.chunk_mgr.primary_scx,
                                               g_sdk.chunk_mgr.primary_scz,
                                               SDK_SUPERCHUNK_WALL_FACE_WEST,
                                               run,
                                               &cx,
                                               &cz);
        wall_health_add_chunk(west, cx, cz);
        sdk_superchunk_wall_edge_chunk_for_run(g_sdk.chunk_mgr.primary_scx,
                                               g_sdk.chunk_mgr.primary_scz,
                                               SDK_SUPERCHUNK_WALL_FACE_NORTH,
                                               run,
                                               &cx,
                                               &cz);
        wall_health_add_chunk(north, cx, cz);
        sdk_superchunk_wall_edge_chunk_for_run(g_sdk.chunk_mgr.primary_scx,
                                               g_sdk.chunk_mgr.primary_scz,
                                               SDK_SUPERCHUNK_WALL_FACE_EAST,
                                               run,
                                               &cx,
                                               &cz);
        wall_health_add_chunk(east, cx, cz);
        sdk_superchunk_wall_edge_chunk_for_run(g_sdk.chunk_mgr.primary_scx,
                                               g_sdk.chunk_mgr.primary_scz,
                                               SDK_SUPERCHUNK_WALL_FACE_SOUTH,
                                               run,
                                               &cx,
                                               &cz);
        wall_health_add_chunk(south, cx, cz);
    }
    return 1;
}

static void maybe_log_active_wall_health(const char* reason)
{
    /* Logs wall chunk health if any wall is incomplete, rate-limited to 5 seconds */
    static ULONGLONG s_last_wall_log_ms = 0u;
    ULONGLONG now;
    WallSideHealth west;
    WallSideHealth north;
    WallSideHealth east;
    WallSideHealth south;
    char dbg[256];

    if (!collect_active_wall_health(&west, &north, &east, &south)) return;
    if (west.resident >= west.desired && west.gpu_ready >= west.resident &&
        north.resident >= north.desired && north.gpu_ready >= north.resident &&
        east.resident >= east.desired && east.gpu_ready >= east.resident &&
        south.resident >= south.desired && south.gpu_ready >= south.resident) {
        return;
    }
    if (startup_safe_mode_active()) return;

    now = GetTickCount64();
    if (now - s_last_wall_log_ms < 5000u) return;
    s_last_wall_log_ms = now;

    sprintf_s(dbg, sizeof(dbg),
              "[WALL] %s W=%d/%d/%d N=%d/%d/%d E=%d/%d/%d S=%d/%d/%d (desired/resident/ready)\n",
              reason ? reason : "STATE",
              west.desired, west.resident, west.gpu_ready,
              north.desired, north.resident, north.gpu_ready,
              east.desired, east.resident, east.gpu_ready,
              south.desired, south.resident, south.gpu_ready);
    sdk_debug_log_output(dbg);
}

static void maybe_log_startup_readiness(void)
{
    /* Logs live runtime streaming health during startup grace mode, rate-limited */
    static ULONGLONG s_last_runtime_log_ms = 0u;
    SdkRuntimeChunkHealth runtime_health;
    const SdkStartupChunkReadiness* startup_latched;
    ULONGLONG now;
    char runtime_dbg[256];
    char backlog_dbg[256];

    if (!startup_safe_mode_active()) return;
    if (!g_sdk.world_session_active || !g_sdk.chunks_initialized) return;
    if (!startup_bootstrap_completed()) return;

    collect_runtime_chunk_health(&runtime_health);
    startup_latched = startup_bootstrap_completion_readiness();
    now = GetTickCount64();
    if (now - s_last_runtime_log_ms < 1000u) return;
    s_last_runtime_log_ms = now;

    sprintf_s(runtime_dbg, sizeof(runtime_dbg),
              "[RUNTIME] visible desired=%d resident=%d ready=%d startup=%d/%d/%d\n",
              runtime_health.desired_visible,
              runtime_health.resident_visible,
              runtime_health.gpu_ready_visible,
              startup_latched ? startup_latched->desired_primary : 0,
              startup_latched ? startup_latched->resident_primary : 0,
              startup_latched ? startup_latched->gpu_ready_primary : 0);
    sdk_debug_log_output(runtime_dbg);

    sprintf_s(backlog_dbg, sizeof(backlog_dbg),
              "[BACKLOG] workers=%d jobs=%d results=%d\n",
              runtime_health.active_workers,
              runtime_health.pending_jobs,
              runtime_health.pending_results);
    sdk_debug_log_output(backlog_dbg);
}

static void collect_missing_desired_chunk_counts(int* out_missing_primary,
                                                 int* out_missing_ring,
                                                 int* out_missing_gate)
{
    /* Counts desired chunks that are not yet resident (primary, ring, transition) */
    int missing_primary = 0;
    int missing_ring = 0;
    int missing_transition = 0;

    if (out_missing_primary) *out_missing_primary = 0;
    if (out_missing_ring) *out_missing_ring = 0;
    if (out_missing_gate) *out_missing_gate = 0;
    if (!g_sdk.chunks_initialized) return;

    for (int i = 0; i < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++i) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, i);
        if (!target) continue;
        if (sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz)) continue;

        if ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_PRIMARY) {
            missing_primary++;
            continue;
        }

        if ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_WALL_SUPPORT ||
            sdk_superchunk_full_neighborhood_ring_contains_chunk(g_sdk.chunk_mgr.primary_scx,
                                                                 g_sdk.chunk_mgr.primary_scz,
                                                                 target->cx,
                                                                 target->cz)) {
            missing_ring++;
        } else if ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_TRANSITION_PRELOAD) {
            missing_transition++;
        } else {
            missing_ring++;
        }
    }

    if (out_missing_primary) *out_missing_primary = missing_primary;
    if (out_missing_ring) *out_missing_ring = missing_ring;
    if (out_missing_gate) *out_missing_gate = missing_transition;
}

static void maybe_log_chunk_residency_stall(void)
{
    /* Logs chunk loading stall when desired chunks exceed active with no pending work */
    static ULONGLONG s_last_stall_log_ms = 0u;
    ULONGLONG now;
    int desired_chunks;
    int active_chunks;
    int pending_jobs;
    int pending_results;
    int superchunk_mode;

    if (!g_sdk.world_session_active || !g_sdk.chunks_initialized) return;

    desired_chunks = sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr);
    active_chunks = sdk_chunk_manager_active_count(&g_sdk.chunk_mgr);
    pending_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
    pending_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
    {
        const SdkSuperchunkConfig* config = sdk_superchunk_get_config();
        superchunk_mode = config && config->enabled &&
                          sdk_chunk_manager_radius_from_grid_size(
                              sdk_chunk_manager_grid_size(&g_sdk.chunk_mgr)) >=
                              sdk_superchunk_get_chunk_span();
    }

    maybe_log_active_wall_health("HEALTH");

    if (desired_chunks <= active_chunks) return;

    now = GetTickCount64();
    if (pending_jobs == 0 && pending_results == 0) {
        if (now - s_last_stall_log_ms >= 1000u) {
            s_last_stall_log_ms = now;
            debug_log_chunk_residency_state("STALL");
            if (superchunk_mode) {
                char dbg[192];
                int missing_primary;
                int missing_ring;
                int missing_transition;
                collect_missing_desired_chunk_counts(&missing_primary, &missing_ring, &missing_transition);
                sprintf_s(dbg, sizeof(dbg),
                          "[RESIDENCY] missing desired chunks primary=%d ring=%d transition=%d desired=%d active=%d\n",
                          missing_primary, missing_ring, missing_transition, desired_chunks, active_chunks);
                sdk_debug_log_output(dbg);
            }
        }
    }
}

const SdkResolutionPreset g_resolution_presets[] = {
    { 1280u, 720u },
    { 1920u, 1080u },
    { 2560u, 1440u },
    { 3840u, 2400u }
};
const int g_anisotropy_presets[SDK_ANISOTROPY_PRESET_COUNT] = { 0, 2, 4, 8, 16 };
const int g_render_distance_presets[SDK_RENDER_DISTANCE_PRESET_COUNT] = { 4, 6, 8, 10, 12, 16 };
const int g_far_mesh_distance_presets[SDK_FAR_MESH_DISTANCE_PRESET_COUNT] = { 0, 2, 4, 6, 8, 10, 12, 16 };

HotbarEntry g_hotbar[10];
int g_hotbar_selected = 0;
int g_break_target_bx;
int g_break_target_by;
int g_break_target_bz;
int g_last_hit_face = FACE_POS_Y;
float g_last_hit_world_x = 0.0f;
float g_last_hit_world_y = 0.0f;
float g_last_hit_world_z = 0.0f;
bool g_last_hit_world_valid = false;
int g_construction_place_rotation = 0;
int g_break_progress;
bool g_break_active;
int g_break_cooldown;

bool g_craft_open = false;
bool g_craft_is_table = false;
ItemType g_craft_grid[9];
int g_craft_grid_count[9];
int g_craft_cursor = 0;
int g_craft_result_idx = -1;
bool g_craft_key_was_down = false;
bool g_craft_lmb_was_down = false;
bool g_craft_rmb_was_down = false;
bool g_craft_nav_was_down[6];
bool g_craft_result_lmb_was_down = false;
bool g_craft_result_rmb_was_down = false;

StationState g_station_states[MAX_STATION_STATES];
int g_station_state_count = 0;
bool g_station_open = false;
SdkStationUIKind g_station_open_kind = SDK_STATION_UI_NONE;
BlockType g_station_open_block_type = BLOCK_AIR;
int g_station_open_index = -1;
int g_station_hovered_slot = -1;
bool g_station_lmb_was_down = false;
bool g_station_rmb_was_down = false;

bool g_skills_open = false;
bool g_skills_key_was_down = false;
int g_skills_key_frames = 0;
int g_skills_selected_tab = SDK_SKILL_TAB_COMBAT;
int g_skills_selected_row = 0;
int g_skills_selected_profession = SDK_PROFESSION_MINING;
bool g_skills_nav_was_down[8];
bool g_pause_menu_open = false;
bool g_pause_menu_key_was_down = false;
int g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
int g_pause_menu_selected = 0;
int g_graphics_menu_selected = 0;
int g_keybind_menu_selected = 0;
int g_keybind_menu_scroll = 0;
bool g_keybind_capture_active = false;
bool g_pause_menu_nav_was_down[6];
int g_pause_character_selected = 0;
int g_pause_character_scroll = 0;
int g_chunk_manager_selected = 0;
int g_chunk_manager_scroll = 0;
int g_creative_menu_selected = 0;
int g_creative_menu_scroll = 0;
int g_creative_menu_filter = SDK_CREATIVE_FILTER_ALL;
char g_creative_menu_search[SDK_PAUSE_MENU_SEARCH_MAX];
int g_creative_menu_search_len = 0;
int g_creative_shape_focus = 0;
int g_creative_shape_row = 0;
int g_creative_shape_width = 16;
int g_creative_shape_height = 16;
int g_creative_shape_depth = 16;

bool g_command_open = false;
char g_command_text[SDK_COMMAND_LINE_TEXT_MAX];
int g_command_text_len = 0;
bool g_command_enter_was_down = false;
bool g_command_backspace_was_down = false;

SdkProfiler g_profiler;
bool g_command_slash_was_down = false;
bool g_map_focus_open = false;
bool g_map_key_was_down = false;
bool g_map_zoom_left_was_down = false;
bool g_map_zoom_right_was_down = false;
int g_map_zoom_left_hold_frames = 0;
int g_map_zoom_right_hold_frames = 0;
float g_map_focus_world_x = 0.0f;
float g_map_focus_world_z = 0.0f;
bool g_map_focus_initialized = false;
int g_map_zoom_tenths = 10;

int g_player_level = 1;
int g_player_xp = 0;
int g_player_xp_to_next = 100;
int g_combat_skill_ranks[SDK_COMBAT_SKILL_COUNT];
int g_survival_skill_ranks[SDK_SURVIVAL_SKILL_COUNT];
int g_profession_points[SDK_PROFESSION_COUNT];
int g_profession_levels[SDK_PROFESSION_COUNT];
int g_profession_xp[SDK_PROFESSION_COUNT];
int g_profession_xp_to_next[SDK_PROFESSION_COUNT];
int g_profession_ranks[SDK_PROFESSION_COUNT][SDK_PROFESSION_NODE_COUNT];

static void clear_world_target_preview(void)
{
    /* Clears the block placement preview outline and placement preview */
    g_last_hit_world_valid = false;
    sdk_renderer_set_outline(0, 0, 0, false);
    sdk_renderer_set_placement_preview(NULL);
}

static void preview_face_local_hit_uv(int wx, int wy, int wz,
                                      int face,
                                      float hit_x, float hit_y, float hit_z,
                                      int* out_u, int* out_v)
{
    /* Calculates UV coordinates for a block face hit point for placement preview */
    float local_x = hit_x - (float)wx;
    float local_y = hit_y - (float)wy;
    float local_z = hit_z - (float)wz;
    float coord_u = 0.0f;
    float coord_v = 0.0f;
    int u;
    int v;

    if (local_x < 0.0f) local_x = 0.0f;
    if (local_x > 0.9999f) local_x = 0.9999f;
    if (local_y < 0.0f) local_y = 0.0f;
    if (local_y > 0.9999f) local_y = 0.9999f;
    if (local_z < 0.0f) local_z = 0.0f;
    if (local_z > 0.9999f) local_z = 0.9999f;

    switch (face) {
        case FACE_NEG_X:
        case FACE_POS_X:
            coord_u = local_z;
            coord_v = local_y;
            break;
        case FACE_NEG_Y:
        case FACE_POS_Y:
            coord_u = local_x;
            coord_v = local_z;
            break;
        case FACE_NEG_Z:
        case FACE_POS_Z:
        default:
            coord_u = local_x;
            coord_v = local_y;
            break;
    }

    u = (int)floorf(coord_u * 16.0f);
    v = (int)floorf(coord_v * 16.0f);
    if (u < 0) u = 0;
    if (u > 15) u = 15;
    if (v < 0) v = 0;
    if (v > 15) v = 15;

    if (out_u) *out_u = u;
    if (out_v) *out_v = v;
}

uint64_t filetime_to_u64(FILETIME ft)
{
    /* Converts Windows FILETIME to uint64_t for time calculations */
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

float api_clampf(float v, float lo, float hi)
{
    /* Clamps float value to specified range [lo, hi] */
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int api_clampi(int v, int lo, int hi)
{
    /* Clamps integer value to specified range [lo, hi] */
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float update_process_cpu_percent(void)
{
    /* Updates and returns current process CPU utilization percentage */
    FILETIME now_ft;
    FILETIME create_ft;
    FILETIME exit_ft;
    FILETIME kernel_ft;
    FILETIME user_ft;
    uint64_t wall_now;
    uint64_t proc_now;

    GetSystemTimeAsFileTime(&now_ft);
    if (!GetProcessTimes(GetCurrentProcess(), &create_ft, &exit_ft, &kernel_ft, &user_ft)) {
        return g_perf_telemetry.last_cpu_percent;
    }

    wall_now = filetime_to_u64(now_ft);
    proc_now = filetime_to_u64(kernel_ft) + filetime_to_u64(user_ft);

    if (!g_perf_telemetry.valid) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_perf_telemetry.cpu_count = (int)si.dwNumberOfProcessors;
        if (g_perf_telemetry.cpu_count < 1) g_perf_telemetry.cpu_count = 1;
        g_perf_telemetry.valid = true;
        g_perf_telemetry.last_wall_time_100ns = wall_now;
        g_perf_telemetry.last_proc_time_100ns = proc_now;
        g_perf_telemetry.last_cpu_percent = 0.0f;
        return 0.0f;
    }

    if (wall_now > g_perf_telemetry.last_wall_time_100ns) {
        uint64_t wall_delta = wall_now - g_perf_telemetry.last_wall_time_100ns;
        uint64_t proc_delta = 0u;
        float cpu_percent = g_perf_telemetry.last_cpu_percent;
        if (proc_now >= g_perf_telemetry.last_proc_time_100ns) {
            proc_delta = proc_now - g_perf_telemetry.last_proc_time_100ns;
        }
        if (wall_delta > 0u && g_perf_telemetry.cpu_count > 0) {
            cpu_percent = (float)(((double)proc_delta * 100.0) /
                                  ((double)wall_delta * (double)g_perf_telemetry.cpu_count));
            if (cpu_percent < 0.0f) cpu_percent = 0.0f;
        }
        g_perf_telemetry.last_cpu_percent = cpu_percent;
    }

    g_perf_telemetry.last_wall_time_100ns = wall_now;
    g_perf_telemetry.last_proc_time_100ns = proc_now;
    return g_perf_telemetry.last_cpu_percent;
}

void get_monitor_work_area_size(HWND hwnd, LONG* out_width, LONG* out_height)
{
    /* Gets monitor work area size in pixels (excludes taskbar) */
    LONG width = 1920;
    LONG height = 1080;
    RECT work_area;
    HMONITOR monitor;
    MONITORINFO monitor_info;

    monitor = hwnd
        ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

    memset(&monitor_info, 0, sizeof(monitor_info));
    monitor_info.cbSize = sizeof(monitor_info);
    if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
        width = monitor_info.rcWork.right - monitor_info.rcWork.left;
        height = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
    } else if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        width = work_area.right - work_area.left;
        height = work_area.bottom - work_area.top;
    } else {
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }

    if (width < 320) width = 320;
    if (height < 240) height = 240;
    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
}

void get_monitor_pixel_size(HWND hwnd, LONG* out_width, LONG* out_height)
{
    /* Gets total monitor pixel dimensions including all monitors */
    RECT monitor_rect;

    if (hwnd) {
        sdk_window_get_monitor_rect(g_sdk.window, &monitor_rect);
    } else {
        monitor_rect.left = 0;
        monitor_rect.top = 0;
        monitor_rect.right = GetSystemMetrics(SM_CXSCREEN);
        monitor_rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    if (out_width) *out_width = monitor_rect.right - monitor_rect.left;
    if (out_height) *out_height = monitor_rect.bottom - monitor_rect.top;
}

void clamp_window_resolution_to_work_area(HWND hwnd, uint32_t* inout_width, uint32_t* inout_height)
{
    /* Adjusts window resolution to fit within monitor work area */
    LONG work_width = 1920;
    LONG work_height = 1080;
    uint32_t width;
    uint32_t height;

    if (!inout_width || !inout_height) return;

    width = *inout_width;
    height = *inout_height;
    if (width < 320u) width = 320u;
    if (height < 240u) height = 240u;

    get_monitor_work_area_size(hwnd, &work_width, &work_height);
    if ((LONG)width > work_width || (LONG)height > work_height) {
        double scale_x = (double)work_width / (double)width;
        double scale_y = (double)work_height / (double)height;
        double scale = (scale_x < scale_y) ? scale_x : scale_y;
        if (scale < 1.0) {
            width = (uint32_t)floor((double)width * scale);
            height = (uint32_t)floor((double)height * scale);
        }
    }

    if ((LONG)width > work_width) width = (uint32_t)work_width;
    if ((LONG)height > work_height) height = (uint32_t)work_height;
    if (width < 320u) width = (uint32_t)work_width;
    if (height < 240u) height = (uint32_t)work_height;
    if (width < 1u) width = 1u;
    if (height < 1u) height = 1u;

    *inout_width = width;
    *inout_height = height;
}

int resolution_preset_index_for_size(uint32_t width, uint32_t height)
{
    /* Finds closest resolution preset index for given dimensions */
    int best_index = 0;
    uint64_t best_distance = UINT64_MAX;
    int i;

    for (i = 0; i < SDK_RESOLUTION_PRESET_COUNT; ++i) {
        int64_t dx = (int64_t)width - (int64_t)g_resolution_presets[i].width;
        int64_t dy = (int64_t)height - (int64_t)g_resolution_presets[i].height;
        uint64_t distance = (uint64_t)(dx * dx + dy * dy);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return best_index;
}

int clamp_resolution_preset_index(int index)
{
    /* Clamps resolution preset index to valid range */
    if (index < 0) return 0;
    if (index >= SDK_RESOLUTION_PRESET_COUNT) return SDK_RESOLUTION_PRESET_COUNT - 1;
    return index;
}

int clamp_render_distance_chunks(int radius)
{
    /* Clamps render distance to nearest preset value */
    int best_index = 0;
    int best_distance = 0x7fffffff;
    int i;

    for (i = 0; i < SDK_RENDER_DISTANCE_PRESET_COUNT; ++i) {
        int distance = api_clampi(radius, -4096, 4096) - g_render_distance_presets[i];
        if (distance < 0) distance = -distance;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return g_render_distance_presets[best_index];
}

int render_distance_preset_index(int radius)
{
    /* Finds preset index for given render distance */
    int clamped = clamp_render_distance_chunks(radius);
    int i;

    for (i = 0; i < SDK_RENDER_DISTANCE_PRESET_COUNT; ++i) {
        if (g_render_distance_presets[i] == clamped) {
            return i;
        }
    }

    return 0;
}

int clamp_far_mesh_distance_chunks(int distance)
{
    /* Clamps far mesh distance to nearest preset value */
    int best_index = 0;
    int best_distance = INT_MAX;
    int i;

    for (i = 0; i < SDK_FAR_MESH_DISTANCE_PRESET_COUNT; ++i) {
        int current_distance = api_clampi(distance, -4096, 4096) - g_far_mesh_distance_presets[i];
        if (current_distance < 0) current_distance = -current_distance;
        if (current_distance < best_distance) {
            best_distance = current_distance;
            best_index = i;
        }
    }

    return g_far_mesh_distance_presets[best_index];
}

int far_mesh_distance_preset_index(int distance)
{
    /* Finds preset index for given far mesh distance */
    int clamped = clamp_far_mesh_distance_chunks(distance);
    int i;

    for (i = 0; i < SDK_FAR_MESH_DISTANCE_PRESET_COUNT; ++i) {
        if (g_far_mesh_distance_presets[i] == clamped) {
            return i;
        }
    }

    return 0;
}

int normalize_far_mesh_lod_distance(int render_distance_chunks, int lod_distance_chunks)
{
    /* Normalizes far mesh LOD distance to not exceed render distance */
    int render_distance = clamp_render_distance_chunks(render_distance_chunks);
    int lod_distance = clamp_far_mesh_distance_chunks(lod_distance_chunks);
    int best = 0;
    int i;

    if (lod_distance <= 0) return 0;
    for (i = 0; i < SDK_FAR_MESH_DISTANCE_PRESET_COUNT; ++i) {
        int candidate = g_far_mesh_distance_presets[i];
        if (candidate <= render_distance) {
            best = candidate;
        }
    }
    if (best <= 0) return 0;
    if (lod_distance > best) return best;
    return lod_distance;
}

int normalize_experimental_far_mesh_distance(int render_distance_chunks,
                                             int lod_distance_chunks,
                                             int experimental_distance_chunks)
{
    /* Normalizes experimental far mesh distance within valid bounds */
    int render_distance = clamp_render_distance_chunks(render_distance_chunks);
    int lod_distance = normalize_far_mesh_lod_distance(render_distance, lod_distance_chunks);
    int experimental_distance = clamp_far_mesh_distance_chunks(experimental_distance_chunks);

    if (experimental_distance <= 0) return 0;
    if (experimental_distance > render_distance) {
        experimental_distance = normalize_far_mesh_lod_distance(render_distance, experimental_distance);
    }
    if (lod_distance > 0 && experimental_distance > lod_distance) {
        experimental_distance = lod_distance;
    }
    return experimental_distance;
}

int clamp_anisotropy_level(int level)
{
    /* Clamps anisotropic filtering level to nearest preset */
    int best_index = 0;
    int best_distance = INT_MAX;
    int i;

    for (i = 0; i < SDK_ANISOTROPY_PRESET_COUNT; ++i) {
        int distance = api_clampi(level, -4096, 4096) - g_anisotropy_presets[i];
        if (distance < 0) distance = -distance;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return g_anisotropy_presets[best_index];
}

int anisotropy_preset_index(int level)
{
    /* Finds preset index for given anisotropy level */
    int clamped = clamp_anisotropy_level(level);
    int i;

    for (i = 0; i < SDK_ANISOTROPY_PRESET_COUNT; ++i) {
        if (g_anisotropy_presets[i] == clamped) {
            return i;
        }
    }

    return 0;
}

int clamp_render_scale_percent(int value)
{
    /* Clamps render scale to valid range [50, 100] percent */
    if (value < 50) return 50;
    if (value > 100) return 100;
    return value;
}

void sync_graphics_resolution_from_window(void)
{
    /* Synchronizes graphics settings from actual window dimensions */
    uint32_t width = 0;
    uint32_t height = 0;

    if (!g_sdk.window) return;
    sdk_window_size(g_sdk.window, &width, &height);
    if (width == 0 || height == 0) return;

    g_graphics_settings.window_width = (int)width;
    g_graphics_settings.window_height = (int)height;
    g_sdk.desc.window.width = width;
    g_sdk.desc.window.height = height;
}

void apply_window_resolution_setting(uint32_t width, uint32_t height, bool save_now)
{
    HWND hwnd = NULL;

    if (g_sdk.window) {
        hwnd = sdk_window_hwnd(g_sdk.window);
    }
    clamp_window_resolution_to_work_area(hwnd, &width, &height);

    g_graphics_settings.window_width = (int)width;
    g_graphics_settings.window_height = (int)height;
    g_sdk.desc.window.width = width;
    g_sdk.desc.window.height = height;

    if (g_sdk.window) {
        sdk_window_set_client_size(g_sdk.window, width, height);
    }
    if (save_now) {
        save_graphics_settings_now();
    }
}

void apply_display_mode_setting(bool save_now)
{
    uint32_t applied_width = (uint32_t)g_graphics_settings.window_width;
    uint32_t applied_height = (uint32_t)g_graphics_settings.window_height;
    SdkResult display_result = SDK_OK;

    if (!g_sdk.window) {
        if (save_now) save_graphics_settings_now();
        return;
    }

    if (g_graphics_settings.display_mode == SDK_DISPLAY_MODE_WINDOWED) {
        display_result = sdk_renderer_set_display_mode((int)SDK_DISPLAY_MODE_WINDOWED, 0u, 0u, NULL, NULL);
        sdk_window_enter_windowed(g_sdk.window,
                                  (uint32_t)g_graphics_settings.window_width,
                                  (uint32_t)g_graphics_settings.window_height);
        sync_graphics_resolution_from_window();
    } else if (g_graphics_settings.display_mode == SDK_DISPLAY_MODE_BORDERLESS) {
        LONG monitor_width = 1920;
        LONG monitor_height = 1080;
        display_result = sdk_renderer_set_display_mode((int)SDK_DISPLAY_MODE_BORDERLESS, 0u, 0u, NULL, NULL);
        sdk_window_enter_borderless(g_sdk.window);
        get_monitor_pixel_size(sdk_window_hwnd(g_sdk.window), &monitor_width, &monitor_height);
        applied_width = (uint32_t)monitor_width;
        applied_height = (uint32_t)monitor_height;
        g_graphics_settings.window_width = (int)applied_width;
        g_graphics_settings.window_height = (int)applied_height;
        g_sdk.desc.window.width = applied_width;
        g_sdk.desc.window.height = applied_height;
        sdk_renderer_resize(applied_width, applied_height);
    } else {
        LONG monitor_width = 1920;
        LONG monitor_height = 1080;
        applied_width = (uint32_t)g_graphics_settings.fullscreen_width;
        applied_height = (uint32_t)g_graphics_settings.fullscreen_height;
        display_result = sdk_renderer_set_display_mode((int)SDK_DISPLAY_MODE_FULLSCREEN,
                                                       applied_width, applied_height,
                                                       &applied_width, &applied_height);
        if (display_result == SDK_OK) {
            g_graphics_settings.fullscreen_width = (int)applied_width;
            g_graphics_settings.fullscreen_height = (int)applied_height;
            g_sdk.desc.window.width = applied_width;
            g_sdk.desc.window.height = applied_height;
        } else {
            g_graphics_settings.display_mode = SDK_DISPLAY_MODE_BORDERLESS;
            sdk_renderer_set_display_mode((int)SDK_DISPLAY_MODE_BORDERLESS, 0u, 0u, NULL, NULL);
            sdk_window_enter_borderless(g_sdk.window);
            get_monitor_pixel_size(sdk_window_hwnd(g_sdk.window), &monitor_width, &monitor_height);
            applied_width = (uint32_t)monitor_width;
            applied_height = (uint32_t)monitor_height;
            g_graphics_settings.window_width = (int)applied_width;
            g_graphics_settings.window_height = (int)applied_height;
            g_sdk.desc.window.width = applied_width;
            g_sdk.desc.window.height = applied_height;
            sdk_renderer_resize(applied_width, applied_height);
        }
    }

    if (save_now) {
        save_graphics_settings_now();
    }
}

void apply_resolution_preset_index(int preset_index, bool save_now)
{
    preset_index = clamp_resolution_preset_index(preset_index);
    g_graphics_settings.resolution_preset_index = preset_index;
    if (g_graphics_settings.display_mode == SDK_DISPLAY_MODE_FULLSCREEN) {
        g_graphics_settings.fullscreen_width = (int)g_resolution_presets[preset_index].width;
        g_graphics_settings.fullscreen_height = (int)g_resolution_presets[preset_index].height;
        apply_display_mode_setting(save_now);
    } else if (g_graphics_settings.display_mode == SDK_DISPLAY_MODE_WINDOWED) {
        apply_window_resolution_setting(
            g_resolution_presets[preset_index].width,
            g_resolution_presets[preset_index].height,
            save_now);
    } else {
        if (save_now) {
            save_graphics_settings_now();
        }
    }
}

void save_input_settings_now(void)
{
    sdk_input_settings_save(&g_input_settings);
}

/* ======================================================================
 * INIT
 * ====================================================================== */

SdkResult nqlsdk_init(const SdkInitDesc* desc)
{
    /* Initializes the NQL SDK with the provided configuration */
    if (g_sdk.running) return SDK_ERR_ALREADY_INIT;
    
    sdk_load_trace_reset("nqlsdk_init");
    sdk_load_trace_note("runtime_init_start", desc && desc->window.title ? desc->window.title : "NQL SDK");

    sdk_profiler_init(&g_profiler);

    /* Apply defaults */
    SdkInitDesc d;
    sdk_graphics_settings_default(&g_graphics_settings);
    sdk_graphics_settings_load(&g_graphics_settings);
    sdk_input_settings_default(&g_input_settings);
    sdk_input_settings_load(&g_input_settings);
    g_graphics_settings.resolution_preset_index =
        clamp_resolution_preset_index(g_graphics_settings.resolution_preset_index);
    g_graphics_settings.chunk_grid_size =
        sdk_chunk_manager_normalize_grid_size(g_graphics_settings.chunk_grid_size);
    g_graphics_settings.render_scale_percent = clamp_render_scale_percent(g_graphics_settings.render_scale_percent);
    g_graphics_settings.anisotropy_level = clamp_anisotropy_level(g_graphics_settings.anisotropy_level);
    g_graphics_settings.far_terrain_lod_distance_chunks =
        normalize_far_mesh_lod_distance(
            sdk_chunk_manager_radius_from_grid_size(g_graphics_settings.chunk_grid_size),
            g_graphics_settings.far_terrain_lod_distance_chunks);
    g_graphics_settings.experimental_far_mesh_distance_chunks =
        normalize_experimental_far_mesh_distance(
            sdk_chunk_manager_radius_from_grid_size(g_graphics_settings.chunk_grid_size),
            g_graphics_settings.far_terrain_lod_distance_chunks,
            g_graphics_settings.experimental_far_mesh_distance_chunks);
    sdk_mesh_set_smooth_lighting_enabled(g_graphics_settings.smooth_lighting ? 1 : 0);
    g_chunk_grid_size_setting = g_graphics_settings.chunk_grid_size;
    if (desc) {
        d = *desc;
    } else {
        memset(&d, 0, sizeof(d));
    }
    if (!d.window.title)  d.window.title  = "NQL SDK \xe2\x80\x94 D3D12";
    if (d.window.width == 0)  d.window.width  = (uint32_t)g_graphics_settings.window_width;
    if (d.window.height == 0) d.window.height = (uint32_t)g_graphics_settings.window_height;
    clamp_window_resolution_to_work_area(NULL, &d.window.width, &d.window.height);
    g_graphics_settings.window_width = (int)d.window.width;
    g_graphics_settings.window_height = (int)d.window.height;

    /* Default clear colour: dark grey */
    if (d.clear_color.r == 0.0f && d.clear_color.g == 0.0f &&
        d.clear_color.b == 0.0f && d.clear_color.a == 0.0f) {
        d.clear_color.r = 0.1f;
        d.clear_color.g = 0.1f;
        d.clear_color.b = 0.12f;
        d.clear_color.a = 1.0f;
    }
    d.vsync = g_graphics_settings.vsync;

    g_sdk.desc = d;

    /* Create window */
    g_sdk.window = sdk_window_create(&d.window);
    if (!g_sdk.window) return SDK_ERR_WINDOW_FAILED;
    sdk_load_trace_note("window_created", d.window.title);
    sync_graphics_resolution_from_window();

    /* Init renderer */
    HWND hwnd = sdk_window_hwnd(g_sdk.window);
    SdkResult r = sdk_renderer_init(
        hwnd, (uint32_t)g_graphics_settings.window_width, (uint32_t)g_graphics_settings.window_height,
        d.clear_color, d.enable_debug, d.vsync);

    if (r != SDK_OK) {
        sdk_load_trace_note("renderer_init_failed", "sdk_renderer_init failed");
        sdk_window_destroy(g_sdk.window);
        g_sdk.window = NULL;
        return r;
    }

    sdk_load_trace_note("runtime_init_complete", "renderer initialized");

    g_sdk.running = true;
    apply_display_mode_setting(false);
    g_sdk.worldgen.impl = NULL;
    g_sdk.world_seed = 0u;
    g_sdk.world_session_active = false;
    g_sdk.world_save_id[0] = '\0';
    g_sdk.world_save_name[0] = '\0';
    g_sdk.chunks_initialized = false;
    skills_reset_progression();
    station_close_ui();
    sdk_entity_init(&g_sdk.entities);
    reset_session_runtime_state();
    update_window_title_for_test_flight();
    load_character_profile();
    frontend_reset_nav_state();
    g_frontend_main_selected = 0;
    g_world_save_selected = 0;
    g_character_menu_selected = 0;
    g_character_action_selected = 0;
    g_character_animation_menu_selected = 0;
    g_animation_menu_selected = 0;
    g_animation_action_selected = 0;
    g_selected_character_index = -1;
    g_selected_animation_index = -1;
    memset(&g_editor_session, 0, sizeof(g_editor_session));
    g_session_kind = SDK_SESSION_KIND_WORLD;
    sdk_server_runtime_reset();
    g_frontend_view = SDK_START_MENU_VIEW_MAIN;
    g_frontend_refresh_pending = true;
    frontend_refresh_worlds_if_needed();
    clear_non_frontend_ui();
    sdk_renderer_set_camera_pos(0.0f, 64.0f, -24.0f);
    sdk_renderer_set_camera_target(0.0f, 32.0f, 0.0f);
    push_start_menu_ui();
    
    return SDK_OK;
}

/* ======================================================================
 * FRAME
 * ====================================================================== */

SdkResult nqlsdk_frame(void)
{
    /* Processes a single frame of the SDK main loop */
    if (!g_sdk.running) {
        sdk_debug_log_output("[NQL SDK] nqlsdk_frame: not running\n");
        return SDK_ERR_NOT_INIT;
    }

    /* Pump messages ??? returns false on WM_QUIT */
    if (!sdk_window_pump(g_sdk.window)) {
        sdk_load_trace_note_state("window_pump_returned_false",
                                  g_frontend_view,
                                  g_sdk.world_session_active ? 1 : 0,
                                  g_world_generation_active ? 1 : 0,
                                  g_world_generation_stage,
                                  "sdk_window_pump returned false");
        sdk_debug_log_output("[NQL SDK] nqlsdk_frame: window_pump returned false\n");
        return SDK_ERR_GENERIC; /* signal caller to exit */
    }

    PROF_FRAME_BEGIN();
    PROF_ZONE_BEGIN(PROF_ZONE_INPUT);
    sdk_input_frame_begin(g_sdk.window);

    /* Handle resize */
    if (sdk_window_was_resized(g_sdk.window)) {
        uint32_t w, h;
        sdk_window_size(g_sdk.window, &w, &h);
        if (w > 0 && h > 0) {
            if (g_graphics_settings.display_mode == SDK_DISPLAY_MODE_FULLSCREEN) {
                g_graphics_settings.fullscreen_width = (int)w;
                g_graphics_settings.fullscreen_height = (int)h;
            } else {
                g_graphics_settings.window_width = (int)w;
                g_graphics_settings.window_height = (int)h;
            }
            sdk_renderer_resize(w, h);
        }
    }
    PROF_ZONE_END(PROF_ZONE_INPUT);

    sdk_server_runtime_tick();

    if (g_frontend_view == SDK_START_MENU_VIEW_RETURNING_TO_START) {
        update_async_return_to_start();
        if (!g_sdk.world_session_active) {
            clear_non_frontend_ui();
            push_start_menu_ui();
            sdk_renderer_set_lighting(0.35f, 0.08f, 0.10f, 0.16f);
        } else {
            clear_world_target_preview();
            push_start_menu_ui();
        }
        {
            PROF_ZONE_BEGIN(PROF_ZONE_RENDERING);
            SdkResult return_result = sdk_renderer_frame();
            PROF_ZONE_END(PROF_ZONE_RENDERING);
            PROF_FRAME_END();
            if (return_result != SDK_OK) {
                sdk_debug_log_output("[NQL SDK] nqlsdk_frame: return-to-start renderer_frame failed\n");
            }
            return return_result;
        }
    }

    if (!g_sdk.world_session_active || g_frontend_forced_open) {
        PROF_ZONE_BEGIN(PROF_ZONE_INPUT);
        frontend_handle_input();
        PROF_ZONE_END(PROF_ZONE_INPUT);
        if (!g_sdk.world_session_active) {
            clear_non_frontend_ui();
            push_start_menu_ui();
            sdk_renderer_set_lighting(0.35f, 0.08f, 0.10f, 0.16f);
            {
                PROF_ZONE_BEGIN(PROF_ZONE_RENDERING);
                SdkResult menu_result = sdk_renderer_frame();
                PROF_ZONE_END(PROF_ZONE_RENDERING);
                PROF_FRAME_END();
                if (menu_result != SDK_OK) {
                    sdk_debug_log_output("[NQL SDK] nqlsdk_frame: renderer_frame failed\n");
                }
                return menu_result;
            }
        }
        if (g_frontend_forced_open) {
            clear_non_frontend_ui();
            push_start_menu_ui();
            sdk_renderer_set_lighting(0.35f, 0.08f, 0.10f, 0.16f);
        }
    }

    /* Update chunk system based on camera position */
    if (g_sdk.chunks_initialized) {
        float cam_x, cam_y, cam_z;
        float look_x, look_y, look_z;
        SdkAutomationFrameOverride automation_override;
        bool editor_mode = editor_session_active();
        sdk_renderer_get_camera_pos(&cam_x, &cam_y, &cam_z);
        sdk_renderer_get_camera_target(&look_x, &look_y, &look_z);
        automation_override = *sdk_automation_frame_override();
        sdk_automation_clear_frame_override();
        
        /* Initialize yaw/pitch on first frame */
        if (!g_cam_rotation_initialized) {
            g_cam_yaw = CAM_DEFAULT_YAW;
            g_cam_pitch = CAM_DEFAULT_PITCH;
            g_cam_rotation_initialized = true;
            /* Debug output removed */
        }
        
        /* --- Player feet position (cam is at eye height) --- */
        float feet_y = cam_y - PLAYER_EYE_H;
        bool pause_menu_key_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_PAUSE_MENU);
        bool skills_key_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_OPEN_SKILLS);
        bool map_key_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_OPEN_MAP);
        bool slash_key_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_OPEN_COMMAND);

        if (g_frontend_forced_open) {
            g_pause_menu_open = false;
            g_skills_open = false;
            g_station_open = false;
            g_craft_open = false;
            g_command_open = false;
            g_map_focus_open = false;
            clear_world_target_preview();
            goto skip_interaction;
        }

        if (automation_override.active && automation_override.teleport_pending) {
            cam_x = automation_override.teleport_x;
            cam_y = automation_override.teleport_y;
            cam_z = automation_override.teleport_z;
            feet_y = cam_y - PLAYER_EYE_H;
            g_vel_y = 0.0f;
            g_on_ground = false;
            g_was_on_ground = false;
            g_mounted_vehicle_index = -1;
            clear_world_target_preview();
        }

        if (pause_menu_key_down && !g_pause_menu_key_was_down) {
            if (g_pause_menu_open) {
                g_pause_menu_open = false;
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
            } else if (!g_craft_open && !g_skills_open && !g_station_open && !g_player_dead) {
                g_pause_menu_open = true;
                g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
                g_pause_character_selected = (g_selected_character_index >= 0) ? g_selected_character_index : 0;
                g_pause_character_scroll = 0;
                g_chunk_manager_selected = 0;
                g_chunk_manager_scroll = 0;
                memset(g_pause_menu_nav_was_down, 0, sizeof(g_pause_menu_nav_was_down));
            }
        }
        g_pause_menu_key_was_down = pause_menu_key_down;

        if (skills_key_down && !g_skills_key_was_down) {
            g_skills_key_frames = 0;
        }
        if (skills_key_down) {
            g_skills_key_frames++;
        } else if (g_skills_key_was_down) {
            bool quick_tap = (g_skills_key_frames > 0 && g_skills_key_frames <= SKILLS_MENU_TAP_FRAMES);
            bool other_move_keys = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MOVE_FORWARD) ||
                                   sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MOVE_LEFT) ||
                                   sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MOVE_RIGHT);
            if (quick_tap && !g_pause_menu_open && !g_map_focus_open &&
                (g_skills_open || (!g_craft_open && !g_station_open && !other_move_keys))) {
                g_skills_open = !g_skills_open;
                skills_clamp_selection();
                memset(g_skills_nav_was_down, 0, sizeof(g_skills_nav_was_down));
            }
            g_skills_key_frames = 0;
        }
        g_skills_key_was_down = skills_key_down;

        if (slash_key_down && !g_command_slash_was_down) {
            if (!g_command_open && !g_pause_menu_open && !g_craft_open &&
                !g_skills_open && !g_station_open && !g_player_dead) {
                command_open();
            }
        }
        g_command_slash_was_down = slash_key_down;

        if (map_key_down && !g_map_key_was_down) {
            if (g_map_focus_open) {
                g_map_focus_open = false;
            } else if (!g_command_open && !g_pause_menu_open && !g_craft_open &&
                       !g_skills_open && !g_station_open && !g_player_dead) {
                g_map_focus_open = true;
                g_map_focus_world_x = cam_x;
                g_map_focus_world_z = cam_z;
                g_map_focus_initialized = true;
            }
        }
        g_map_key_was_down = map_key_down;

        if (editor_mode) {
            g_map_focus_open = false;
        }

        if (g_command_open || g_pause_menu_open || g_skills_open || g_station_open || g_player_dead) {
            g_map_focus_open = false;
        }

        if (g_command_open && !g_player_dead) {
            command_line_handle_input(&cam_x, &cam_y, &cam_z, &look_x, &look_y, &look_z);
            clear_world_target_preview();
            sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
            sdk_renderer_set_camera_target(look_x, look_y, look_z);
            goto skip_interaction;
        }

        if (g_pause_menu_open && !g_player_dead) {
            pause_menu_handle_input();
            if (!g_sdk.world_session_active) {
                clear_non_frontend_ui();
                push_start_menu_ui();
                sdk_renderer_set_lighting(0.35f, 0.08f, 0.10f, 0.16f);
                return sdk_renderer_frame();
            }
            clear_world_target_preview();
            sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
            sdk_renderer_set_camera_target(look_x, look_y, look_z);
            goto skip_interaction;
        }

        if (g_skills_open && !g_player_dead) {
            skills_handle_menu_input();
            clear_world_target_preview();
            sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
            sdk_renderer_set_camera_target(look_x, look_y, look_z);
            goto skip_interaction;
        }

        if (g_station_open && !g_player_dead) {
            station_handle_ui_input();
            clear_world_target_preview();
            sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
            sdk_renderer_set_camera_target(look_x, look_y, look_z);
            goto skip_interaction;
        }

        if (g_map_focus_open && !g_player_dead && !editor_mode) {
            map_handle_input(cam_x, cam_z);
            clear_world_target_preview();
            sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
            sdk_renderer_set_camera_target(look_x, look_y, look_z);
            goto skip_interaction;
        }
        
        /* --- Arrow keys: rotate POV --- */
        float yaw = g_cam_yaw;
        float pitch = g_cam_pitch;
        float rot_speed = 0.03f * ((float)g_input_settings.look_sensitivity_percent / 100.0f);
        float pitch_dir = g_input_settings.invert_y ? -1.0f : 1.0f;
        bool camera_updated = false;
        if (automation_override.active) {
            yaw += automation_override.yaw_delta;
            pitch += automation_override.pitch_delta;
            if (fabsf(automation_override.yaw_delta) > 0.0001f ||
                fabsf(automation_override.pitch_delta) > 0.0001f) {
                camera_updated = true;
            }
        }
        if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_LOOK_LEFT))  { yaw -= rot_speed; camera_updated = true; }
        if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_LOOK_RIGHT)) { yaw += rot_speed; camera_updated = true; }
        if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_LOOK_UP))    { pitch += rot_speed * pitch_dir; camera_updated = true; }
        if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_LOOK_DOWN))  { pitch -= rot_speed * pitch_dir; camera_updated = true; }
        if (pitch >  1.5f) pitch =  1.5f;
        if (pitch < -1.5f) pitch = -1.5f;
        g_cam_yaw = yaw;
        g_cam_pitch = pitch;
        
        /* --- Forward/right vectors in XZ plane --- */
        float forward_x = sinf(yaw);
        float forward_z = cosf(yaw);
        float right_x   = cosf(yaw);
        float right_z   = -sinf(yaw);
        float look_cos_pitch = cosf(pitch);
        float look_dir_x = look_cos_pitch * sinf(yaw);
        float look_dir_y = sinf(pitch);
        float look_dir_z = look_cos_pitch * cosf(yaw);
        bool space_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_JUMP_TOGGLE_FLIGHT);
        bool geology_debug_down = sdk_window_is_key_down(g_sdk.window, VK_F5);
        bool chunk_report_down = sdk_window_is_key_down(g_sdk.window, VK_F9);
        bool settlement_debug_down = sdk_window_is_key_down(g_sdk.window, VK_F6);
        bool fluid_debug_down = sdk_window_is_key_down(g_sdk.window, VK_F7);
        bool perf_debug_down = sdk_window_is_key_down(g_sdk.window, VK_F8);

        if (automation_override.active && automation_override.toggle_flight) {
            g_test_flight_enabled = !g_test_flight_enabled;
            g_space_tap_timer = 0;
            g_vel_y = 0.0f;
            g_on_ground = false;
            g_was_on_ground = false;
            g_fall_start_y = feet_y;
            update_window_title_for_test_flight();
        }

        if (!editor_mode && geology_debug_down && !g_worldgen_debug_was_down) {
            SdkWorldGenDebugMode next_mode = sdk_worldgen_get_debug_mode_ctx(&g_sdk.worldgen);
            switch (next_mode) {
                case SDK_WORLDGEN_DEBUG_OFF:
                    next_mode = SDK_WORLDGEN_DEBUG_FORMATIONS;
                    break;
                case SDK_WORLDGEN_DEBUG_FORMATIONS:
                    next_mode = SDK_WORLDGEN_DEBUG_STRUCTURES;
                    break;
                case SDK_WORLDGEN_DEBUG_STRUCTURES:
                    next_mode = SDK_WORLDGEN_DEBUG_BODIES;
                    break;
                case SDK_WORLDGEN_DEBUG_BODIES:
                default:
                    next_mode = SDK_WORLDGEN_DEBUG_OFF;
                    break;
            }
            sdk_worldgen_set_debug_mode_ctx(&g_sdk.worldgen, next_mode);
            mark_all_loaded_chunks_dirty();
            update_window_title_for_test_flight();
            switch (next_mode) {
                case SDK_WORLDGEN_DEBUG_FORMATIONS:
                    sdk_debug_log_output("[WORLDGEN] Debug mode: formations\n");
                    break;
                case SDK_WORLDGEN_DEBUG_STRUCTURES:
                    sdk_debug_log_output("[WORLDGEN] Debug mode: structures\n");
                    break;
                case SDK_WORLDGEN_DEBUG_BODIES:
                    sdk_debug_log_output("[WORLDGEN] Debug mode: resource bodies\n");
                    break;
                case SDK_WORLDGEN_DEBUG_OFF:
                default:
                    sdk_debug_log_output("[WORLDGEN] Debug mode: off\n");
                    break;
            }
        }
        g_worldgen_debug_was_down = geology_debug_down;

        if (!editor_mode && chunk_report_down && !g_worldgen_chunk_report_was_down) {
            int report_cx = sdk_world_to_chunk_x((int)floorf(cam_x));
            int report_cz = sdk_world_to_chunk_z((int)floorf(cam_z));
            sdk_worldgen_emit_chunk_debug_report(&g_sdk.worldgen, &g_sdk.chunk_mgr, report_cx, report_cz);
        }
        g_worldgen_chunk_report_was_down = chunk_report_down;

        if (settlement_debug_down && !g_settlement_debug_was_down) {
            g_settlement_debug_overlay = !g_settlement_debug_overlay;
            g_settlement_debug_cache_valid = false;
            g_settlement_debug_last_refresh_ms = 0u;
            sdk_debug_log_output(g_settlement_debug_overlay
                ? "[SETTLEMENT] Debug overlay: on\n"
                : "[SETTLEMENT] Debug overlay: off\n");
        }
        g_settlement_debug_was_down = settlement_debug_down;

        if (fluid_debug_down && !g_fluid_debug_was_down) {
            g_fluid_debug_overlay = !g_fluid_debug_overlay;
            sdk_debug_log_output(g_fluid_debug_overlay
                ? "[FLUID] Debug overlay: on\n"
                : "[FLUID] Debug overlay: off\n");
        }
        g_fluid_debug_was_down = fluid_debug_down;

        if (perf_debug_down && !g_perf_debug_was_down) {
            g_perf_debug_overlay = !g_perf_debug_overlay;
            sdk_debug_log_output(g_perf_debug_overlay
                ? "[PERF] Debug overlay: on\n"
                : "[PERF] Debug overlay: off\n");
        }
        g_perf_debug_was_down = perf_debug_down;

        /* Double-tap SPACE toggles test flight. */
        if (space_down && !g_space_was_down) {
            if (g_space_tap_timer > 0) {
                g_test_flight_enabled = !g_test_flight_enabled;
                g_space_tap_timer = 0;
                g_vel_y = 0.0f;
                g_on_ground = false;
                g_was_on_ground = false;
                g_fall_start_y = feet_y;
                update_window_title_for_test_flight();
                sdk_debug_log_output(g_test_flight_enabled
                    ? "[PLAYER] Test flight enabled (double SPACE, testing only)\n"
                    : "[PLAYER] Test flight disabled\n");
            } else {
                g_space_tap_timer = DOUBLE_TAP_WINDOW;
            }
        }
        g_space_was_down = space_down;
        if (g_space_tap_timer > 0) g_space_tap_timer--;

        {
            bool vehicle_use_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_VEHICLE_USE);
            if (vehicle_use_down && !g_vehicle_use_was_down) {
                if (mounted_vehicle_entity()) {
                    dismount_vehicle(&cam_x, &cam_y, &cam_z, &look_x, &look_y, &look_z);
                } else {
                    int vehicle_index = find_mountable_vehicle(cam_x, cam_y, cam_z, look_x, look_y, look_z);
                    if (vehicle_index >= 0) {
                        g_mounted_vehicle_index = vehicle_index;
                        g_test_flight_enabled = false;
                        g_vel_y = 0.0f;
                        g_on_ground = false;
                        g_was_on_ground = false;
                        g_w_sprint_latched = false;
                        g_is_sprinting = false;
                        sync_camera_to_vehicle(&g_sdk.entities.entities[vehicle_index],
                                               &cam_x, &cam_y, &cam_z,
                                               &look_x, &look_y, &look_z);
                    }
                }
            }
            g_vehicle_use_was_down = vehicle_use_down;
        }

        SdkEntity* mounted_vehicle = mounted_vehicle_entity();
        
        /* --- WASD horizontal movement with collision --- */
        
        /* Sprint detection: double-tap W */
        float auto_forward = automation_override.active ? automation_override.move_forward : 0.0f;
        bool auto_w_down = auto_forward > 0.001f;
        bool w_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MOVE_FORWARD) || auto_w_down;
        bool sprint_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_SPRINT);
        if (w_down && !g_w_was_down) {
            if (g_w_tap_timer > 0) {
                g_w_sprint_latched = true;
                g_w_tap_timer = 0;
            } else {
                g_w_tap_timer = DOUBLE_TAP_WINDOW;
            }
        }
        g_w_was_down = w_down;
        if (g_w_tap_timer > 0) {
            g_w_tap_timer--;
        }
        if (!w_down || mounted_vehicle) {
            g_w_sprint_latched = false;
        }
        g_is_sprinting = (!mounted_vehicle &&
                          (sprint_down || g_w_sprint_latched) &&
                          w_down &&
                          (g_test_flight_enabled || g_player_hunger >= HUNGER_SPRINT_THRESHOLD));
        
        float mx = 0.0f, mz = 0.0f, fly_move_y = 0.0f;
        if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MOVE_FORWARD)) { mx += forward_x; mz += forward_z; }
        if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MOVE_BACKWARD)) { mx -= forward_x; mz -= forward_z; g_is_sprinting = false; g_w_sprint_latched = false; }
        if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MOVE_LEFT)) { mx -= right_x;   mz -= right_z;   g_is_sprinting = false; g_w_sprint_latched = false; }
        if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MOVE_RIGHT)) { mx += right_x;   mz += right_z;   g_is_sprinting = false; g_w_sprint_latched = false; }
        if (automation_override.active) {
            mx += forward_x * automation_override.move_forward;
            mz += forward_z * automation_override.move_forward;
            mx += right_x * automation_override.move_right;
            mz += right_z * automation_override.move_right;
        }
        
        if (g_test_flight_enabled && g_is_sprinting && w_down) {
            float sprint_fly_speed = WALK_SPEED * SPRINT_SPEED_MULT * TEST_FLIGHT_SPRINT_MULT;
            mx = look_dir_x * sprint_fly_speed;
            mz = look_dir_z * sprint_fly_speed;
            fly_move_y = look_dir_y * sprint_fly_speed;
            camera_updated = true;
        } else {
            /* Normalise diagonal movement so you don't go faster diagonally */
            float move_len = sqrtf(mx * mx + mz * mz);
            if (move_len > 0.001f) {
                float current_speed = g_is_sprinting ? (WALK_SPEED * SPRINT_SPEED_MULT) : WALK_SPEED;
                mx = (mx / move_len) * current_speed;
                mz = (mz / move_len) * current_speed;
                camera_updated = true;
            }
        }

        if (mounted_vehicle) {
            float drive_len = sqrtf(mx * mx + mz * mz);
            float drive_x = 0.0f;
            float drive_z = 0.0f;
            if (drive_len > 0.001f) {
                float drive_speed = sdk_entity_vehicle_speed(mounted_vehicle->mob_type);
                drive_x = (mx / drive_len) * drive_speed;
                drive_z = (mz / drive_len) * drive_speed;
                mounted_vehicle->mob_dir_x = drive_x / drive_speed;
                mounted_vehicle->mob_dir_z = drive_z / drive_speed;
                camera_updated = true;
            }
            mounted_vehicle->vx = drive_x;
            mounted_vehicle->vz = drive_z;
            mx = 0.0f;
            mz = 0.0f;
            fly_move_y = 0.0f;
        }
        
        if (g_test_flight_enabled) {
            cam_x += mx;
            cam_z += mz;
        } else {
            /* Move X axis with collision */
            {
                float new_x = cam_x + mx;
                float min_x = new_x - PLAYER_HALF_W;
                float max_x = new_x + PLAYER_HALF_W;
                float min_z = cam_z - PLAYER_HALF_W;
                float max_z = cam_z + PLAYER_HALF_W;
                if (!aabb_collides(min_x, feet_y, min_z, max_x, feet_y + PLAYER_HEIGHT - 0.01f, max_z)) {
                    cam_x = new_x;
                }
            }
            
            /* Move Z axis with collision */
            {
                float new_z = cam_z + mz;
                float min_x = cam_x - PLAYER_HALF_W;
                float max_x = cam_x + PLAYER_HALF_W;
                float min_z = new_z - PLAYER_HALF_W;
                float max_z = cam_z + PLAYER_HALF_W;
                if (!aabb_collides(min_x, feet_y, min_z, max_x, feet_y + PLAYER_HEIGHT - 0.01f, max_z)) {
                    cam_z = new_z;
                }
            }
        }
        
        /* --- Invincibility tick --- */
        if (g_invincible_frames > 0) g_invincible_frames--;

        /* --- Death screen: freeze player, then respawn --- */
        if (g_player_dead) {
            g_skills_open = false;
            g_pause_menu_open = false;
            g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
            command_close();
            station_close_ui();
            g_mounted_vehicle_index = -1;
            g_death_timer++;
            if (g_death_timer >= DEATH_SCREEN_FRAMES) {
                /* Respawn */
                g_player_dead = false;
                g_death_timer = 0;
                g_player_health = PLAYER_MAX_HEALTH;
                g_player_hunger = PLAYER_MAX_HUNGER;
                g_hunger_tick = 0;
                cam_x = g_spawn_x; cam_y = g_spawn_y; cam_z = g_spawn_z;
                feet_y = cam_y - PLAYER_EYE_H;
                g_vel_y = 0.0f;
                g_on_ground = false;
                g_was_on_ground = true;
                /* Clear hotbar on death */
                for (int i = 0; i < 10; i++) {
                    clear_hotbar_entry(&g_hotbar[i]);
                }
                sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
            }
            /* Skip all input while dead */
            goto skip_interaction;
        }

        if (mounted_vehicle) {
            g_vel_y = 0.0f;
            g_on_ground = false;
            g_was_on_ground = false;
            sync_camera_to_vehicle(mounted_vehicle, &cam_x, &cam_y, &cam_z, &look_x, &look_y, &look_z);
            feet_y = cam_y - PLAYER_EYE_H;
            camera_updated = true;
        } else if (g_test_flight_enabled) {
            if (space_down) fly_move_y += TEST_FLIGHT_SPEED;
            if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_DESCEND)) {
                fly_move_y -= TEST_FLIGHT_SPEED;
            }
            if (automation_override.active && fabsf(automation_override.move_up) > 0.0001f) {
                fly_move_y += automation_override.move_up * TEST_FLIGHT_SPEED;
            }
            feet_y += fly_move_y;
            g_vel_y = 0.0f;
            g_on_ground = false;
            g_was_on_ground = false;
            if (fabsf(fly_move_y) > 0.0001f) camera_updated = true;
        } else {
            /* --- Jump (spacebar) --- */
            if (space_down && g_on_ground) {
                g_vel_y = JUMP_VELOCITY;
                g_on_ground = false;
                camera_updated = true;
            }
            
            /* --- Gravity: apply velocity then resolve vertical collision --- */
            g_vel_y -= GRAVITY_ACCEL;
            if (g_vel_y < -TERMINAL_VEL) g_vel_y = -TERMINAL_VEL;
            
            feet_y += g_vel_y;
            
            /* Resolve downward collision (landing) */
            if (g_vel_y <= 0.0f) {
                float min_x = cam_x - PLAYER_HALF_W;
                float max_x = cam_x + PLAYER_HALF_W;
                float min_z = cam_z - PLAYER_HALF_W;
                float max_z = cam_z + PLAYER_HALF_W;
                if (aabb_collides(min_x, feet_y, min_z, max_x, feet_y + 0.01f, max_z)) {
                    /* Snap feet to top of the block we collided with */
                    int block_y = (int)floorf(feet_y);
                    feet_y = (float)(block_y + 1);

                    /* Fall damage: compute distance fallen */
                    if (!g_was_on_ground) {
                        float fall_dist = g_fall_start_y - feet_y;
                        if (fall_dist > FALL_DAMAGE_THRESHOLD && g_invincible_frames <= 0) {
                            int damage = (int)((fall_dist - FALL_DAMAGE_THRESHOLD) * 2.0f);
                            if (damage > 0) {
                                g_player_health -= damage;
                                g_invincible_frames = INVINCIBILITY_FRAMES;
                                if (g_player_health <= 0) {
                                    g_player_health = 0;
                                    g_player_dead = true;
                                    g_death_timer = 0;
                                }
                            }
                        }
                    }

                    g_vel_y = 0.0f;
                    g_on_ground = true;
                } else {
                    g_on_ground = false;
                }

                /* Track when player leaves ground for fall distance */
                if (g_was_on_ground && !g_on_ground) {
                    g_fall_start_y = feet_y;
                }
                g_was_on_ground = g_on_ground;
            }
            
            /* Resolve upward collision (head bump) */
            if (g_vel_y > 0.0f) {
                float head_y = feet_y + PLAYER_HEIGHT;
                float min_x = cam_x - PLAYER_HALF_W;
                float max_x = cam_x + PLAYER_HALF_W;
                float min_z = cam_z - PLAYER_HALF_W;
                float max_z = cam_z + PLAYER_HALF_W;
                if (aabb_collides(min_x, head_y - 0.01f, min_z, max_x, head_y, max_z)) {
                    int block_y = (int)floorf(head_y);
                    feet_y = (float)block_y - PLAYER_HEIGHT;
                    g_vel_y = 0.0f;
                }
            }
        }
        
        /* Update eye position from feet */
        cam_y = feet_y + PLAYER_EYE_H;
        camera_updated = true;
        
        /* --- Reconstruct look target from yaw/pitch --- */
        {
            float cos_p = cosf(g_cam_pitch);
            float sin_p = sinf(g_cam_pitch);
            look_x = cam_x + g_cam_look_dist * cos_p * sinf(g_cam_yaw);
            look_y = cam_y + g_cam_look_dist * sin_p;
            look_z = cam_z + g_cam_look_dist * cos_p * cosf(g_cam_yaw);
        }
        
        /* Update camera position and target */
        sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
        sdk_renderer_set_camera_target(look_x, look_y, look_z);

        /* --- Number keys 1-9, 0 select hotbar slot --- */
        for (int k = 0; k < 10; k++) {
            SdkInputAction hotbar_action = (SdkInputAction)(SDK_INPUT_ACTION_HOTBAR_1 + k);
            if (sdk_input_action_pressed(&g_input_settings, hotbar_action)) {
                g_hotbar_selected = k;
            }
        }

        /* --- Crafting UI toggle (C key = 2x2 hand-craft) --- */
        {
            bool c_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_OPEN_CRAFTING);
            if (c_down && !g_craft_key_was_down) {
                if (g_craft_open) {
                    craft_close();
                } else if (!g_station_open) {
                    g_craft_open = true;
                    g_craft_is_table = false;
                    g_craft_cursor = 0;
                    /* Initialize grid to known good values */
                    for (int i = 0; i < 9; i++) {
                        g_craft_grid[i] = ITEM_NONE;
                        g_craft_grid_count[i] = 0;
                    }
                    g_craft_result_idx = -1;
                    craft_update_match();
                }
            }
            g_craft_key_was_down = c_down;
        }

        /* --- Crafting UI input (when open, intercepts all interaction) --- */
        if (g_craft_open) {
            int w = craft_grid_w(), h = craft_grid_h();
            int total = w * h;
            int cx = g_craft_cursor % w, cy = g_craft_cursor / w;

            /* Arrow / WASD navigation (edge-triggered) */
            bool nav_keys[5] = {
                sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_UP) ||
                    sdk_input_raw_key_down((uint8_t)'W') || sdk_input_raw_key_down(VK_UP),
                sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_DOWN) ||
                    sdk_input_raw_key_down((uint8_t)'S') || sdk_input_raw_key_down(VK_DOWN),
                sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_LEFT) ||
                    sdk_input_raw_key_down((uint8_t)'A') || sdk_input_raw_key_down(VK_LEFT),
                sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_RIGHT) ||
                    sdk_input_raw_key_down((uint8_t)'D') || sdk_input_raw_key_down(VK_RIGHT),
                sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_BACK) ||
                    sdk_input_raw_key_down(VK_ESCAPE)
            };
            if (nav_keys[0] && !g_craft_nav_was_down[0] && cy > 0) cy--;
            if (nav_keys[1] && !g_craft_nav_was_down[1] && cy < h - 1) cy++;
            if (nav_keys[2] && !g_craft_nav_was_down[2] && cx > 0) cx--;
            if (nav_keys[3] && !g_craft_nav_was_down[3] && cx < w - 1) cx++;
            g_craft_cursor = cy * w + cx;
            for (int n = 0; n < 5; n++) g_craft_nav_was_down[n] = nav_keys[n];

            /* Get mouse position for result slot detection */
            int32_t mouse_x, mouse_y;
            sdk_window_get_mouse_pos(g_sdk.window, &mouse_x, &mouse_y);
            bool over_result = craft_mouse_over_result_slot(mouse_x, mouse_y);

            /* LMB on result slot = craft 1, else place item in grid */
            bool lmb = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_BREAK_BLOCK) ||
                       sdk_input_raw_mouse_down(0);
            if (lmb && !g_craft_lmb_was_down) {
                if (over_result && g_craft_result_idx >= 0) {
                    craft_take_result();
                } else if (!over_result) {
                    craft_place_from_hotbar();
                }
            }
            g_craft_lmb_was_down = lmb;

            /* RMB on result slot = bulk craft, else remove item from grid */
            bool rmb = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_PLACE_USE) ||
                       sdk_input_raw_mouse_down(1);
            if (rmb && !g_craft_rmb_was_down) {
                if (over_result && g_craft_result_idx >= 0) {
                    craft_take_result_bulk();
                } else if (!over_result) {
                    craft_remove_to_hotbar();
                }
            }
            g_craft_rmb_was_down = rmb;

            /* Escape = close */
            if (nav_keys[4] && !g_craft_nav_was_down[4]) craft_close();

            /* Skip block interaction while crafting */
            clear_world_target_preview();
            goto skip_interaction;
        }

        if (sdk_input_action_pressed(&g_input_settings, SDK_INPUT_ACTION_CONSTRUCTION_ROTATE)) {
            HotbarEntry* held = &g_hotbar[g_hotbar_selected];
            if (held->payload_kind == SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION &&
                held->shaped.occupied_count > 0u) {
                g_construction_place_rotation = (g_construction_place_rotation + 1) & 3;
            }
        }

        /* --- Break cooldown tick --- */
        if (g_break_cooldown > 0) g_break_cooldown--;

        /* --- Block targeting via raycast --- */
        {
            float dir_x = look_x - cam_x;
            float dir_y = look_y - cam_y;
            float dir_z = look_z - cam_z;
            float dir_len = sqrtf(dir_x*dir_x + dir_y*dir_y + dir_z*dir_z);
            if (dir_len > 0.0001f) { dir_x /= dir_len; dir_y /= dir_len; dir_z /= dir_len; }

            int hit_bx, hit_by, hit_bz, hit_face;
            int prev_bx, prev_by, prev_bz;
            float hit_dist = 0.0f;
            bool hit = raycast_block(cam_x, cam_y, cam_z, dir_x, dir_y, dir_z,
                                     REACH_DISTANCE,
                                     &hit_bx, &hit_by, &hit_bz, &hit_face,
                                     &prev_bx, &prev_by, &prev_bz,
                                     &hit_dist);

            if (hit) {
                HotbarEntry* held = &g_hotbar[g_hotbar_selected];
                int held_is_shaped = (held->payload_kind == SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION &&
                                      held->shaped.occupied_count > 0u);
                BlockType held_place_block = BLOCK_AIR;
                int held_consume_item = 0;
                int held_can_place_full_block = hotbar_get_place_block(held, &held_place_block, &held_consume_item);
                int held_placeable = held_is_shaped || held_can_place_full_block;
                SdkPlacementPreview preview;
                memset(&preview, 0, sizeof(preview));

                g_last_hit_face = hit_face;
                g_last_hit_world_x = cam_x + dir_x * hit_dist;
                g_last_hit_world_y = cam_y + dir_y * hit_dist;
                g_last_hit_world_z = cam_z + dir_z * hit_dist;
                g_last_hit_world_valid = true;

                if (held_placeable) {
                    sdk_renderer_set_outline(prev_bx, prev_by, prev_bz, true);
                    preview.visible = true;
                    preview.valid = true;
                    preview.wx = prev_bx;
                    preview.wy = prev_by;
                    preview.wz = prev_bz;
                    preview.face = hit_face;

                    if (held_is_shaped) {
                        SdkConstructionPlacementResolution resolution;
                        memset(&resolution, 0, sizeof(resolution));
                        sdk_construction_resolve_face_placement(&g_sdk.chunk_mgr,
                                                                prev_bx, prev_by, prev_bz,
                                                                &held->shaped,
                                                                hit_face,
                                                                g_construction_place_rotation,
                                                                g_last_hit_world_x,
                                                                g_last_hit_world_y,
                                                                g_last_hit_world_z,
                                                                &resolution);
                        preview.mode = SDK_PLACEMENT_PREVIEW_CONSTRUCTION;
                        preview.valid = resolution.valid ? true : false;
                        preview.face_u = resolution.face_u;
                        preview.face_v = resolution.face_v;
                        preview.material = (int)resolution.preview_payload.material;
                        preview.payload = resolution.preview_payload;
                    } else if (held_can_place_full_block) {
                        preview.mode = SDK_PLACEMENT_PREVIEW_FULL_BLOCK;
                        preview.material = (int)held_place_block;
                        preview_face_local_hit_uv(prev_bx, prev_by, prev_bz,
                                                  hit_face,
                                                  g_last_hit_world_x,
                                                  g_last_hit_world_y,
                                                  g_last_hit_world_z,
                                                  &preview.face_u,
                                                  &preview.face_v);
                        sdk_construction_payload_from_full_block(held_place_block, &preview.payload);
                        if (sdk_construction_world_cell_has_occupancy(&g_sdk.chunk_mgr, prev_bx, prev_by, prev_bz)) {
                            preview.valid = false;
                        }
                    }

                    if (editor_mode && !editor_block_in_bounds(prev_bx, prev_by, prev_bz)) {
                        preview.valid = false;
                    }
                    sdk_renderer_set_placement_preview(&preview);
                } else {
                    sdk_renderer_set_outline(hit_bx, hit_by, hit_bz, true);
                    sdk_renderer_set_placement_preview(NULL);
                }

                if (try_use_combat_item(held, cam_x, cam_y, cam_z, dir_x, dir_y, dir_z)) {
                    goto skip_interaction;
                }

                /* --- Left mouse: break block --- */
                if (sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_BREAK_BLOCK) && g_break_cooldown <= 0) {
                    int shaping_tool = (!editor_mode &&
                                        held->count > 0 &&
                                        sdk_item_is_tool(held->item) &&
                                        (sdk_item_get_tool_class(held->item) == TOOL_SAW ||
                                         sdk_item_get_tool_class(held->item) == TOOL_CHISEL));
                    if (editor_mode && !editor_block_in_bounds(hit_bx, hit_by, hit_bz)) {
                        g_break_active = false;
                        g_break_progress = 0;
                        goto editor_break_done;
                    }
                    if (shaping_tool) {
                        if (sdk_input_action_pressed(&g_input_settings, SDK_INPUT_ACTION_BREAK_BLOCK) &&
                            sdk_actor_break_block(held, hit_bx, hit_by, hit_bz, 1)) {
                            g_break_active = false;
                            g_break_progress = 0;
                            g_break_cooldown = BREAK_COOLDOWN;
                        }
                        goto editor_break_done;
                    }
                    if (g_break_active &&
                        hit_bx == g_break_target_bx &&
                        hit_by == g_break_target_by &&
                        hit_bz == g_break_target_bz) {
                        g_break_progress++;
                    } else {
                        g_break_target_bx = hit_bx;
                        g_break_target_by = hit_by;
                        g_break_target_bz = hit_bz;
                        g_break_progress = 1;
                        g_break_active = true;
                    }

                    BlockType bt = get_block_at(hit_bx, hit_by, hit_bz);
                    int hardness = sdk_block_get_hardness(bt);

                    /* Tool speed: if held item is matching tool, break faster */
                    HotbarEntry* held = &g_hotbar[g_hotbar_selected];
                    float speed_mult = 1.0f;
                    if (held->count > 0 && sdk_item_is_tool(held->item)) {
                        ToolClass tc = sdk_item_get_tool_class(held->item);
                        BlockToolPref pref = sdk_block_get_tool_pref(bt);
                        if (sdk_tool_matches_block(tc, pref)) {
                            speed_mult = sdk_item_get_speed(held->item);
                        }
                    }
                    int effective_hardness = (int)(hardness / speed_mult);
                    if (effective_hardness < 1) effective_hardness = 1;

                    if (hardness > 0 && g_break_progress >= effective_hardness) {
                        sdk_actor_break_block(held, hit_bx, hit_by, hit_bz, editor_mode ? 0 : 1);
                        g_break_active = false;
                        g_break_progress = 0;
                        g_break_cooldown = BREAK_COOLDOWN;
                    }
                editor_break_done:
                    ;
                } else if (!sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_BREAK_BLOCK)) {
                    if (g_break_active) {
                        g_break_active = false;
                        g_break_progress = 0;
                    }
                }

                /* --- Right mouse: open crafting table or place block --- */
                if (sdk_input_action_pressed(&g_input_settings, SDK_INPUT_ACTION_PLACE_USE)) {
                    /* Right-click on crafting table opens 3x3 crafting */
                    BlockType target_bt = get_block_at(hit_bx, hit_by, hit_bz);
                    if (!editor_mode && target_bt == BLOCK_CRAFTING_TABLE && !g_craft_open) {
                        g_craft_open = true;
                        g_craft_is_table = true;
                        g_craft_cursor = 0;
                        memset(g_craft_grid, 0, sizeof(g_craft_grid));
                        memset(g_craft_grid_count, 0, sizeof(g_craft_grid_count));
                        g_craft_result_idx = -1;
                        goto skip_interaction;
                    }
                    if (!editor_mode && is_station_block(target_bt) && !g_craft_open && !g_pause_menu_open && !g_skills_open) {
                        station_open_for_block(hit_bx, hit_by, hit_bz, target_bt);
                        goto skip_interaction;
                    }
                    HotbarEntry* sel = &g_hotbar[g_hotbar_selected];
                    if (!editor_mode && use_spawn_item_at(sel, prev_bx, prev_by, prev_bz)) {
                        goto skip_interaction;
                    }
                    if ((!editor_mode || editor_block_in_bounds(prev_bx, prev_by, prev_bz)) &&
                        sdk_actor_place_block(sel, prev_bx, prev_by, prev_bz,
                                              cam_x, cam_y - PLAYER_EYE_H, cam_z,
                                              PLAYER_HALF_W, PLAYER_HEIGHT, 1)) {
                        goto skip_interaction;
                    }
                }
            } else {
                HotbarEntry* held = &g_hotbar[g_hotbar_selected];
                clear_world_target_preview();
                if (g_break_active) {
                    g_break_active = false;
                    g_break_progress = 0;
                }

                if (!editor_mode && try_use_combat_item(held, cam_x, cam_y, cam_z, dir_x, dir_y, dir_z)) {
                    goto skip_interaction;
                }

                /* LMB with no block targeted: try to attack a mob */
                if (!editor_mode && sdk_input_action_pressed(&g_input_settings, SDK_INPUT_ACTION_BREAK_BLOCK)) {
                    int atk_dmg = 1; /* Fist damage */
                    if (held->count > 0) atk_dmg = sdk_item_get_attack(held->item);
                    bool mob_killed = false;
                    MobType killed_type = MOB_ZOMBIE;
                    float kx, ky, kz;
                    sdk_entity_player_attack(&g_sdk.entities,
                                             cam_x, cam_y, cam_z, look_x, look_y, look_z, atk_dmg,
                                             &mob_killed, &kx, &ky, &kz, &killed_type);
                    if (mob_killed) {
                        drop_loot_for_killed_mob(killed_type, kx, ky, kz);
                    }
                }

                /* RMB with no block targeted: eat food if holding food */
                if (!editor_mode && sdk_input_action_pressed(&g_input_settings, SDK_INPUT_ACTION_PLACE_USE)) {
                    int spawn_wx = (int)floorf(cam_x + dir_x * 2.5f);
                    int spawn_wy = (int)floorf(cam_y - PLAYER_EYE_H);
                    int spawn_wz = (int)floorf(cam_z + dir_z * 2.5f);
                    if (use_spawn_item_at(held, spawn_wx, spawn_wy, spawn_wz)) {
                        /* Consumed by spawner use. */
                    } else if (held->count > 0 && sdk_item_is_food(held->item) &&
                        g_player_hunger < PLAYER_MAX_HUNGER) {
                        int nutrition = sdk_item_get_nutrition(held->item);
                        g_player_hunger += nutrition;
                        if (g_player_hunger > PLAYER_MAX_HUNGER) g_player_hunger = PLAYER_MAX_HUNGER;
                        held->count--;
                        if (held->count <= 0) {
                            clear_hotbar_entry(held);
                        }
                    }
                }
            }
        }

    skip_interaction:

        if (!editor_mode) {
            PROF_ZONE_BEGIN(PROF_ZONE_PHYSICS);
            station_tick_all();
            sdk_simulation_tick_chunk_manager(&g_sdk.chunk_mgr, MAX_SIM_CELLS_PER_FRAME);
            PROF_ZONE_END(PROF_ZONE_PHYSICS);

            sdk_settlement_runtime_tick_loaded(&g_sdk.worldgen, &g_sdk.chunk_mgr, &g_sdk.entities);

            /* --- Tick entities (item drops, mobs) --- */
            {
                SdkPickupItem pickups[32];
                int pickup_count = 0;
                int mob_damage = 0;
                PROF_ZONE_BEGIN(PROF_ZONE_ENTITY_UPDATE);
                sdk_entity_tick_all(&g_sdk.entities,
                                    cam_x, cam_y, cam_z,
                                    pickups, &pickup_count, 32,
                                    is_solid_at, &mob_damage);
                PROF_ZONE_END(PROF_ZONE_ENTITY_UPDATE);
                for (int p = 0; p < pickup_count; p++) {
                    if (pickups[p].item != ITEM_NONE || pickups[p].payload_kind != SDK_ITEM_PAYLOAD_NONE) {
                        hotbar_add_pickup(&pickups[p]);
                    }
                }
                if (mounted_vehicle_entity()) {
                    sync_camera_to_vehicle(mounted_vehicle_entity(), &cam_x, &cam_y, &cam_z,
                                           &look_x, &look_y, &look_z);
                    sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
                    sdk_renderer_set_camera_target(look_x, look_y, look_z);
                }
                /* Apply mob contact damage to player */
                if (mob_damage > 0 && g_invincible_frames <= 0 && !g_player_dead) {
                    g_player_health -= mob_damage;
                    g_invincible_frames = INVINCIBILITY_FRAMES;
                    if (g_player_health <= 0) {
                        g_player_health = 0;
                        g_player_dead = true;
                        g_death_timer = 0;
                    }
                }
            }

            tick_weapon_effects(cam_x, cam_y, cam_z);

            /* --- Zombie spawning at night --- */
            {
                static int spawn_cooldown = 0;
                static int passive_spawn_cooldown = 0;
                if (spawn_cooldown > 0) spawn_cooldown--;
                if (passive_spawn_cooldown > 0) passive_spawn_cooldown--;
                bool is_night = (g_world_time >= DUSK_START + 2000 || g_world_time < DAWN_START + 1000);
                if (is_night && spawn_cooldown <= 0 && g_sdk.entities.count < 20) {
                    float angle = sinf((float)g_world_time * 0.1f) * 3.14159f * 2.0f;
                    float dist = 12.0f + sinf((float)g_world_time * 0.37f) * 4.0f;
                    float sx = cam_x + cosf(angle) * dist;
                    float sz = cam_z + sinf(angle) * dist;
                    int sy = sdk_worldgen_get_surface_y_ctx(&g_sdk.worldgen, (int)sx, (int)sz) + 1;
                    if (sy > 5 && sy < 200) {
                        sdk_entity_spawn_mob(&g_sdk.entities, sx, (float)sy, sz, MOB_ZOMBIE);
                    }
                    spawn_cooldown = 120;
                }

                if (!is_night && passive_spawn_cooldown <= 0 && g_sdk.entities.count < 28) {
                    float angle = cosf((float)g_world_time * 0.051f + cam_x * 0.003f) * 3.14159f * 2.0f;
                    float dist = 10.0f + fabsf(sinf((float)g_world_time * 0.021f + cam_z * 0.002f)) * 10.0f;
                    float sx = cam_x + cosf(angle) * dist;
                    float sz = cam_z + sinf(angle) * dist;
                    SdkTerrainColumnProfile profile;
                    if (sdk_worldgen_sample_column_ctx(&g_sdk.worldgen, (int)sx, (int)sz, &profile)) {
                        int relief = estimate_spawn_relief(&g_sdk.worldgen, (int)sx, (int)sz, (int)profile.surface_height);
                        if (sdk_worldgen_profile_is_passive_spawn_habitat(&profile, relief)) {
                            MobType passive_type = (((int)floorf(sx) ^ (int)floorf(sz)) & 1) ? MOB_DEER : MOB_BOAR;
                            sdk_entity_spawn_mob(&g_sdk.entities, sx, (float)profile.surface_height + 1.0f, sz, passive_type);
                        }
                    }
                    passive_spawn_cooldown = 180;
                }
            }
        }

        /* --- Send hotbar state to renderer for HUD --- */
        {
            SdkHotbarSlot slots[10];
            for (int i = 0; i < 10; i++) {
                slots[i].item_type = (int)g_hotbar[i].item;
                slots[i].direct_block_type = (int)g_hotbar[i].creative_block;
                if (g_hotbar[i].payload_kind == SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION &&
                    g_hotbar[i].shaped.occupied_count > 0u) {
                    slots[i].direct_block_type = (int)g_hotbar[i].shaped.material;
                    slots[i].count = (g_hotbar[i].count > 0) ? g_hotbar[i].count : 1;
                } else {
                    slots[i].count = g_hotbar[i].count;
                }
            }
            sdk_renderer_set_hotbar(slots, 10, g_hotbar_selected);
        }

        if (editor_mode) {
            SdkMapUI mui;
            memset(&mui, 0, sizeof(mui));
            sdk_renderer_set_map(&mui);
        } else {
            push_superchunk_map_ui(cam_x, cam_z);
        }

        /* --- Send crafting UI state to renderer --- */
        {
            SdkCraftingUI cui = {0};
            cui.open = g_craft_open;
            cui.grid_w = craft_grid_w();
            cui.grid_h = craft_grid_h();
            cui.cursor = g_craft_cursor;
            for (int i = 0; i < cui.grid_w * cui.grid_h; i++) {
                cui.grid_item[i] = (int)g_craft_grid[i];
                cui.grid_count[i] = g_craft_grid_count[i];
            }
            if (g_craft_result_idx >= 0) {
                cui.result_item = (int)g_recipes[g_craft_result_idx].result;
                cui.result_count = g_recipes[g_craft_result_idx].result_count;
            }
            sdk_renderer_set_crafting(&cui);
        }

        /* --- Send station UI state to renderer --- */
        {
            SdkStationUI stui;
            memset(&stui, 0, sizeof(stui));
            stui.open = g_station_open;
            stui.kind = (int)g_station_open_kind;
            stui.block_type = (int)g_station_open_block_type;
            stui.hovered_slot = g_station_hovered_slot;
            if (g_station_open_index >= 0 && g_station_open_index < g_station_state_count) {
                StationState* state = &g_station_states[g_station_open_index];
                stui.input_item = (int)state->input_item;
                stui.input_count = state->input_count;
                stui.fuel_item = (int)state->fuel_item;
                stui.fuel_count = state->fuel_count;
                stui.output_item = (int)state->output_item;
                stui.output_count = state->output_count;
                stui.progress = state->progress;
                stui.progress_max = station_progress_max(state->block_type);
                stui.burn_remaining = state->burn_remaining;
                stui.burn_max = state->burn_max;
            }
            sdk_renderer_set_station(&stui);
        }

        /* --- Send skills/progression UI state to renderer --- */
        {
            SdkSkillsUI sui;
            memset(&sui, 0, sizeof(sui));
            sui.open = g_skills_open;
            sui.level = g_player_level;
            sui.xp_current = g_player_xp;
            sui.xp_to_next = g_player_xp_to_next;
            sui.unspent_skill_points = available_general_skill_points();
            sui.selected_tab = g_skills_selected_tab;
            sui.selected_row = g_skills_selected_row;
            sui.selected_profession = g_skills_selected_profession;
            memcpy(sui.combat_ranks, g_combat_skill_ranks, sizeof(sui.combat_ranks));
            memcpy(sui.survival_ranks, g_survival_skill_ranks, sizeof(sui.survival_ranks));
            memcpy(sui.profession_points, g_profession_points, sizeof(sui.profession_points));
            memcpy(sui.profession_levels, g_profession_levels, sizeof(sui.profession_levels));
            memcpy(sui.profession_xp, g_profession_xp, sizeof(sui.profession_xp));
            memcpy(sui.profession_xp_to_next, g_profession_xp_to_next, sizeof(sui.profession_xp_to_next));
            memcpy(sui.profession_ranks, g_profession_ranks, sizeof(sui.profession_ranks));
            sdk_renderer_set_skills(&sui);
        }

        /* --- Send pause menu state to renderer --- */
        {
            SdkPauseMenuUI pui;
            int visible_character_count = 0;
            int actual_width = g_graphics_settings.window_width;
            int actual_height = g_graphics_settings.window_height;
            memset(&pui, 0, sizeof(pui));
            pui.open = g_pause_menu_open;
            pui.view = api_clampi(g_pause_menu_view, SDK_PAUSE_MENU_VIEW_MAIN, SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER);
            pui.selected = api_clampi(g_pause_menu_selected, 0, SDK_PAUSE_MENU_OPTION_COUNT - 1);
            pui.graphics_selected = api_clampi(g_graphics_menu_selected, 0, SDK_GRAPHICS_MENU_ROW_COUNT - 1);
            pui.graphics_preset = api_clampi((int)g_graphics_settings.preset,
                                             SDK_GRAPHICS_PRESET_PERFORMANCE,
                                             SDK_GRAPHICS_PRESET_HIGH);
            pui.graphics_display_mode = api_clampi((int)g_graphics_settings.display_mode,
                                                   SDK_DISPLAY_MODE_WINDOWED,
                                                   SDK_DISPLAY_MODE_FULLSCREEN);
            pui.graphics_resolution_preset =
                clamp_resolution_preset_index(g_graphics_settings.resolution_preset_index);
            pui.graphics_render_scale_percent = clamp_render_scale_percent(g_graphics_settings.render_scale_percent);
            pui.graphics_anti_aliasing_mode = api_clampi((int)g_graphics_settings.anti_aliasing_mode,
                                                         SDK_ANTI_ALIASING_OFF,
                                                         SDK_ANTI_ALIASING_FXAA);
            pui.graphics_smooth_lighting = g_graphics_settings.smooth_lighting;
            pui.graphics_shadow_quality = api_clampi((int)g_graphics_settings.shadow_quality,
                                                     SDK_SHADOW_QUALITY_OFF,
                                                     SDK_SHADOW_QUALITY_HIGH);
            pui.graphics_water_quality = api_clampi((int)g_graphics_settings.water_quality,
                                                    SDK_WATER_QUALITY_LOW,
                                                    SDK_WATER_QUALITY_HIGH);
            pui.graphics_render_distance_chunks =
                sdk_chunk_manager_radius_from_grid_size(g_chunk_grid_size_setting);
            pui.graphics_anisotropy_level = clamp_anisotropy_level(g_graphics_settings.anisotropy_level);
            pui.graphics_far_terrain_lod_distance_chunks =
                normalize_far_mesh_lod_distance(pui.graphics_render_distance_chunks,
                                                g_graphics_settings.far_terrain_lod_distance_chunks);
            pui.graphics_experimental_far_mesh_distance_chunks =
                normalize_experimental_far_mesh_distance(
                    pui.graphics_render_distance_chunks,
                    pui.graphics_far_terrain_lod_distance_chunks,
                    g_graphics_settings.experimental_far_mesh_distance_chunks);
            pui.graphics_black_superchunk_walls = g_graphics_settings.black_superchunk_walls;
            pui.graphics_vsync = g_graphics_settings.vsync;
            pui.graphics_fog_enabled = g_graphics_settings.fog_enabled;
            if (g_graphics_settings.display_mode == SDK_DISPLAY_MODE_FULLSCREEN) {
                actual_width = g_graphics_settings.fullscreen_width;
                actual_height = g_graphics_settings.fullscreen_height;
            }
            pui.resolution_width = actual_width;
            pui.resolution_height = actual_height;
            pui.keybind_selected = api_clampi(g_keybind_menu_selected, 0, SDK_INPUT_ACTION_COUNT + 2);
            pui.keybind_scroll = api_clampi(g_keybind_menu_scroll, 0, SDK_INPUT_ACTION_COUNT + 2);
            pui.keybind_total = SDK_INPUT_ACTION_COUNT + 3;
            pui.keybind_visible_count = 0;
            pui.keybind_capture_active = g_keybind_capture_active;
            pui.look_sensitivity_percent = api_clampi(g_input_settings.look_sensitivity_percent, 25, 300);
            pui.invert_y = g_input_settings.invert_y;
            for (int row = 0; row < SDK_KEYBIND_MENU_VISIBLE_ROWS; ++row) {
                int absolute_row = pui.keybind_scroll + row;
                if (absolute_row >= pui.keybind_total) {
                    break;
                }
                if (absolute_row < SDK_INPUT_ACTION_COUNT) {
                    sdk_input_binding_name(g_input_settings.binding_code[absolute_row],
                                           pui.keybind_value[pui.keybind_visible_count],
                                           sizeof(pui.keybind_value[pui.keybind_visible_count]));
                    strcpy_s(pui.keybind_label[pui.keybind_visible_count],
                             sizeof(pui.keybind_label[pui.keybind_visible_count]),
                             sdk_input_action_label((SdkInputAction)absolute_row));
                    if (g_keybind_capture_active && absolute_row == pui.keybind_selected) {
                        strcpy_s(pui.keybind_value[pui.keybind_visible_count],
                                 sizeof(pui.keybind_value[pui.keybind_visible_count]),
                                 "PRESS KEY...");
                    }
                } else if (absolute_row == SDK_INPUT_ACTION_COUNT) {
                    strcpy_s(pui.keybind_label[pui.keybind_visible_count],
                             sizeof(pui.keybind_label[pui.keybind_visible_count]),
                             "LOOK SENSITIVITY");
                    sprintf_s(pui.keybind_value[pui.keybind_visible_count],
                              sizeof(pui.keybind_value[pui.keybind_visible_count]),
                              "%d%%", pui.look_sensitivity_percent);
                } else if (absolute_row == SDK_INPUT_ACTION_COUNT + 1) {
                    strcpy_s(pui.keybind_label[pui.keybind_visible_count],
                             sizeof(pui.keybind_label[pui.keybind_visible_count]),
                             "INVERT Y");
                    strcpy_s(pui.keybind_value[pui.keybind_visible_count],
                             sizeof(pui.keybind_value[pui.keybind_visible_count]),
                             g_input_settings.invert_y ? "ON" : "OFF");
                } else {
                    strcpy_s(pui.keybind_label[pui.keybind_visible_count],
                             sizeof(pui.keybind_label[pui.keybind_visible_count]),
                             "RESTORE ALL DEFAULTS");
                    strcpy_s(pui.keybind_value[pui.keybind_visible_count],
                             sizeof(pui.keybind_value[pui.keybind_visible_count]),
                             "ENTER");
                }
                pui.keybind_visible_count++;
            }
            pui.creative_selected = g_creative_menu_selected;
            pui.creative_scroll = g_creative_menu_scroll;
            pui.creative_total = creative_visible_entry_count();
            pui.creative_visible_count = 0;
            pui.creative_filter = g_creative_menu_filter;
            memcpy(pui.creative_search, g_creative_menu_search, sizeof(pui.creative_search));
            pui.creative_shape_focus = g_creative_shape_focus;
            pui.creative_shape_row = g_creative_shape_row;
            pui.creative_shape_width = g_creative_shape_width;
            pui.creative_shape_height = g_creative_shape_height;
            pui.creative_shape_depth = g_creative_shape_depth;
            pui.character_current = g_selected_character_index;
            pui.chunk_manager_selected = g_chunk_manager_selected;
            pui.chunk_manager_scroll = g_chunk_manager_scroll;
            pui.local_hosted_session =
                (g_client_connection.connected &&
                 g_client_connection.kind == SDK_CLIENT_CONNECTION_LOCAL_HOST &&
                 g_local_host_manager.active);
            pui.remote_session =
                (g_client_connection.connected &&
                 g_client_connection.kind == SDK_CLIENT_CONNECTION_REMOTE_SERVER);
            if (g_character_asset_count > 0) {
                int max_scroll = api_clampi(g_character_asset_count - SDK_START_MENU_ASSET_VISIBLE_MAX,
                                            0, g_character_asset_count);
                int selected_index = api_clampi(g_pause_character_selected, 0, g_character_asset_count - 1);
                int scroll = api_clampi(g_pause_character_scroll, 0, max_scroll);
                if (selected_index < scroll) scroll = selected_index;
                if (selected_index >= scroll + SDK_START_MENU_ASSET_VISIBLE_MAX) {
                    scroll = selected_index - SDK_START_MENU_ASSET_VISIBLE_MAX + 1;
                }
                visible_character_count = g_character_asset_count - scroll;
                if (visible_character_count > SDK_START_MENU_ASSET_VISIBLE_MAX) {
                    visible_character_count = SDK_START_MENU_ASSET_VISIBLE_MAX;
                }
                pui.character_selected = selected_index;
                pui.character_scroll = scroll;
                pui.character_count = visible_character_count;
                for (int row = 0; row < visible_character_count; ++row) {
                    strcpy_s(pui.character_name[row], sizeof(pui.character_name[row]),
                             g_character_assets[scroll + row].display_name);
                }
            }
            for (int row = 0; row < SDK_CREATIVE_MENU_VISIBLE_ROWS; ++row) {
                CreativeEntry entry;
                int index = g_creative_menu_scroll + row;
                entry = creative_entry_for_filtered_index(index);
                if (entry.kind == SDK_CREATIVE_ENTRY_BLOCK && entry.id == BLOCK_AIR) break;
                if (entry.kind == SDK_CREATIVE_ENTRY_ITEM && entry.id == ITEM_NONE) break;
                pui.creative_visible_entry_kind[pui.creative_visible_count] = entry.kind;
                pui.creative_visible_entry_id[pui.creative_visible_count] = entry.id;
                pui.creative_visible_count++;
            }
            sdk_renderer_set_pause_menu(&pui);
        }

        {
            SdkCommandLineUI cui;
            memset(&cui, 0, sizeof(cui));
            cui.open = g_command_open;
            memcpy(cui.text, g_command_text, sizeof(cui.text));
            sdk_renderer_set_command_line(&cui);
        }

        PROF_ZONE_BEGIN(PROF_ZONE_DEBUG_UI);
        {
            SdkFluidDebugInfo dbg;
            SdkFluidDebugUI fui;
            int resident_primary = 0;
            int resident_frontier = 0;
            int resident_transition = 0;
            int resident_evict = 0;
            int slot_index;
            memset(&dbg, 0, sizeof(dbg));
            memset(&fui, 0, sizeof(fui));
            sdk_simulation_get_debug_info(&g_sdk.chunk_mgr, &dbg);
            for (slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
                const SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at_const(&g_sdk.chunk_mgr, slot_index);
                if (!slot || !slot->occupied) continue;
                switch ((SdkChunkResidencyRole)slot->role) {
                    case SDK_CHUNK_ROLE_PRIMARY:            resident_primary++; break;
                    case SDK_CHUNK_ROLE_WALL_SUPPORT:
                    case SDK_CHUNK_ROLE_FRONTIER:           resident_frontier++; break;
                    case SDK_CHUNK_ROLE_TRANSITION_PRELOAD: resident_transition++; break;
                    case SDK_CHUNK_ROLE_EVICT_PENDING:      resident_evict++; break;
                    case SDK_CHUNK_ROLE_NONE:
                    default:                                break;
                }
            }
            fui.open = g_fluid_debug_overlay;
            fui.mechanism = dbg.mechanism;
            fui.last_seed_wx = dbg.last_seed_wx;
            fui.last_seed_wy = dbg.last_seed_wy;
            fui.last_seed_wz = dbg.last_seed_wz;
            fui.reservoir_columns = (int)dbg.reservoir_columns;
            fui.total_columns = (int)dbg.total_columns;
            fui.worker_count = dbg.worker_count;
            fui.tick_processed = (int)dbg.tick_processed;
            fui.dirty_cells = (int)dbg.dirty_cells;
            fui.active_chunks = (int)dbg.active_chunks;
            fui.stream_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
            fui.stream_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
            fui.total_volume = (int)lrintf(dbg.total_volume);
            fui.target_surface_e = (int)lrintf(dbg.target_surface_e);
            fui.solve_ms = (int)lrintf(dbg.solve_ms);
            fui.primary_scx = g_sdk.chunk_mgr.primary_scx;
            fui.primary_scz = g_sdk.chunk_mgr.primary_scz;
            fui.desired_scx = g_sdk.chunk_mgr.desired_scx;
            fui.desired_scz = g_sdk.chunk_mgr.desired_scz;
            fui.transition_active = g_sdk.chunk_mgr.transition_active ? 1 : 0;
            fui.resident_primary = resident_primary;
            fui.resident_frontier = resident_frontier;
            fui.resident_transition = resident_transition;
            fui.resident_evict = resident_evict;
            strncpy_s(fui.reason, sizeof(fui.reason), dbg.reason, _TRUNCATE);
            sdk_renderer_set_fluid_debug(&fui);
        }

        {
            SdkFluidDebugInfo dbg;
            SdkPerfDebugUI pdu;
            SdkSettlementRuntimePerfCounters settlement_perf;
            PROCESS_MEMORY_COUNTERS_EX pmc;
            MEMORYSTATUSEX mem_status;
            int resident_chunks = 0;
            int slot_index;
            int dirty_chunks = 0;
            int far_dirty_chunks = 0;
            int stalled_dirty_chunks = 0;

            memset(&dbg, 0, sizeof(dbg));
            memset(&pdu, 0, sizeof(pdu));
            memset(&settlement_perf, 0, sizeof(settlement_perf));
            memset(&pmc, 0, sizeof(pmc));
            memset(&mem_status, 0, sizeof(mem_status));
            mem_status.dwLength = sizeof(mem_status);

            sdk_simulation_get_debug_info(&g_sdk.chunk_mgr, &dbg);
            sdk_settlement_runtime_get_perf_counters(&settlement_perf);
            for (slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
                const SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at_const(&g_sdk.chunk_mgr, slot_index);
                const SdkChunk* chunk;
                if (!slot || !slot->occupied) continue;
                resident_chunks++;
                chunk = &slot->chunk;
                if (!chunk->blocks) continue;
                if (sdk_chunk_needs_remesh(chunk)) {
                    dirty_chunks++;
                    if (chunk->far_mesh_dirty && chunk->dirty_subchunks_mask == 0u) {
                        far_dirty_chunks++;
                    }
                    if (chunk->dirty_frame_age >= 120u) {
                        stalled_dirty_chunks++;
                    }
                }
            }

            pdu.open = g_perf_debug_overlay;
            pdu.cpu_percent = update_process_cpu_percent();
            pdu.stream_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
            pdu.stream_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
            pdu.sim_step_cells = (int)dbg.tick_processed;
            pdu.sim_dirty_cells = (int)dbg.dirty_cells;
            pdu.sim_active_chunks = (int)dbg.active_chunks;
            pdu.resident_chunks = resident_chunks;
            pdu.desired_chunks = sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr);
            pdu.dirty_chunks = dirty_chunks;
            pdu.far_dirty_chunks = far_dirty_chunks;
            pdu.stalled_dirty_chunks = stalled_dirty_chunks;
            pdu.active_settlements = settlement_perf.active_settlements;
            pdu.active_residents = settlement_perf.active_residents;
            pdu.settlement_scanned_chunks = settlement_perf.scanned_loaded_chunks;
            pdu.settlement_initialized = settlement_perf.last_scan_initialized;
            pdu.settlement_block_mutations = settlement_perf.block_mutations_last_tick;
            pdu.primary_scx = g_sdk.chunk_mgr.primary_scx;
            pdu.primary_scz = g_sdk.chunk_mgr.primary_scz;
            pdu.desired_scx = g_sdk.chunk_mgr.desired_scx;
            pdu.desired_scz = g_sdk.chunk_mgr.desired_scz;
            pdu.transition_active = g_sdk.chunk_mgr.transition_active ? 1 : 0;
            {
                SdkStartupChunkReadiness readiness;
                collect_startup_chunk_readiness(&readiness);
                pdu.startup_safe_active = startup_safe_mode_active() ? 1 : 0;
                pdu.startup_primary_desired = readiness.desired_primary;
                pdu.startup_primary_resident = readiness.resident_primary;
                pdu.startup_primary_ready = readiness.gpu_ready_primary;
                pdu.startup_no_cpu_mesh = readiness.no_cpu_mesh;
                pdu.startup_upload_pending = readiness.upload_pending;
                pdu.startup_gpu_stale = readiness.gpu_mesh_generation_stale;
                pdu.startup_far_only = readiness.far_only_when_full_needed;
            }

            if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                pdu.working_set_mb = (int)(pmc.WorkingSetSize / (1024u * 1024u));
                pdu.private_mb = (int)(pmc.PrivateUsage / (1024u * 1024u));
                pdu.peak_working_set_mb = (int)(pmc.PeakWorkingSetSize / (1024u * 1024u));
            }
            if (GlobalMemoryStatusEx(&mem_status)) {
                pdu.system_memory_load_pct = (int)mem_status.dwMemoryLoad;
            }

            sdk_renderer_set_perf_debug(&pdu);
        }

        {
            SdkSettlementDebugUI sdu;
            int query_wx = (int)floorf(cam_x);
            int query_wz = (int)floorf(cam_z);

            memset(&sdu, 0, sizeof(sdu));
            sdu.open = g_settlement_debug_overlay;

            if (g_settlement_debug_overlay) {
                ULONGLONG now = GetTickCount64();
                bool moved = (!g_settlement_debug_cache_valid ||
                              query_wx != g_settlement_debug_last_wx ||
                              query_wz != g_settlement_debug_last_wz);
                bool refresh_due = (!g_settlement_debug_cache_valid ||
                                    (moved &&
                                     now - g_settlement_debug_last_refresh_ms >= SETTLEMENT_DEBUG_REFRESH_MS));

                if (refresh_due) {
                    refresh_settlement_debug_snapshot(query_wx, query_wz);
                }

                if (g_settlement_debug_cache_valid) {
                    sdu = g_settlement_debug_cached_ui;
                    sdu.open = true;
                }
            }

            sdk_renderer_set_settlement_debug(&sdu);
        }
        PROF_ZONE_END(PROF_ZONE_DEBUG_UI);

        if (!editor_mode) {
            /* --- Day/night cycle tick --- */
            {
                g_world_time = (g_world_time + 1) % DAY_LENGTH;

                /* Compute ambient level based on time of day */
                float ambient;
                if (g_world_time < NOON) {
                    ambient = AMBIENT_MIN + (AMBIENT_MAX - AMBIENT_MIN) * ((float)g_world_time / (float)NOON);
                } else if (g_world_time < DUSK_START) {
                    ambient = AMBIENT_MAX;
                } else if (g_world_time < MIDNIGHT) {
                    float t = (float)(g_world_time - DUSK_START) / (float)(MIDNIGHT - DUSK_START);
                    ambient = AMBIENT_MAX - (AMBIENT_MAX - AMBIENT_MIN) * t;
                } else {
                    float t = (float)(g_world_time - MIDNIGHT) / (float)(DAY_LENGTH - MIDNIGHT);
                    ambient = AMBIENT_MIN + (AMBIENT_MAX - AMBIENT_MIN) * 0.1f * t;
                }

                {
                    float day_r = 0.53f, day_g = 0.81f, day_b = 0.92f;
                    float night_r = 0.02f, night_g = 0.02f, night_b = 0.08f;
                    float sky_t = (ambient - AMBIENT_MIN) / (AMBIENT_MAX - AMBIENT_MIN);
                    float sky_r = night_r + (day_r - night_r) * sky_t;
                    float sky_g = night_g + (day_g - night_g) * sky_t;
                    float sky_b = night_b + (day_b - night_b) * sky_t;

                    apply_graphics_atmosphere(ambient, sky_r, sky_g, sky_b);
                }
            }

            /* --- Hunger tick: depletion, starvation, natural healing --- */
            if (!g_player_dead) {
                g_hunger_tick++;
                {
                    int effective_interval = (g_is_sprinting && !g_test_flight_enabled)
                        ? (HUNGER_TICK_INTERVAL / 4)
                        : HUNGER_TICK_INTERVAL;
                    if (effective_interval < 1) effective_interval = 1;
                    if (g_hunger_tick >= effective_interval) {
                        g_hunger_tick = 0;
                        if (g_player_hunger > 0) g_player_hunger--;
                        if (g_player_hunger >= HUNGER_HEAL_THRESHOLD &&
                            g_player_health < PLAYER_MAX_HEALTH) {
                            g_player_health++;
                        }
                        if (g_player_hunger <= STARVATION_THRESHOLD && g_player_health > 1) {
                            g_player_health--;
                            if (g_player_health <= 0) {
                                g_player_health = 0;
                                g_player_dead = true;
                                g_death_timer = 0;
                            }
                        }
                    }
                }
            }
        } else {
            g_player_health = PLAYER_MAX_HEALTH;
            g_player_hunger = PLAYER_MAX_HUNGER;
            g_invincible_frames = 0;
            g_player_dead = false;
            apply_graphics_atmosphere(0.85f, 0.55f, 0.72f, 0.90f);
            tick_editor_session();
        }

        /* --- Send health/death state to renderer --- */
        sdk_renderer_set_health(g_player_health, PLAYER_MAX_HEALTH,
                                g_player_dead, g_invincible_frames > 0,
                                g_player_hunger, PLAYER_MAX_HUNGER);

        /* --- Send entity data to renderer --- */
            sdk_renderer_set_entities(&g_sdk.entities);
        {
            SdkEditorUI editor_ui;
            build_editor_ui(&editor_ui);
            sdk_renderer_set_editor(&editor_ui);
        }
        push_start_menu_ui();
        
        int cam_cx = sdk_world_to_chunk_x((int)cam_x);
        int cam_cz = sdk_world_to_chunk_z((int)cam_z);
        
        if (!editor_mode) {
            sdk_chunk_manager_set_background_expansion(&g_sdk.chunk_mgr, !startup_safe_mode_active());

            /* Chunk residency now tracks the active render-distance window as well as superchunk transitions. */
            PROF_ZONE_BEGIN(PROF_ZONE_CHUNK_UPDATE);
            {
                bool topology_changed = sdk_chunk_manager_update(&g_sdk.chunk_mgr, cam_cx, cam_cz);
                if (topology_changed) {
                    sdk_simulation_invalidate_reservoirs();
                    evict_undesired_loaded_chunks();
                    bootstrap_nearby_visible_chunks_sync();
                    debug_log_chunk_residency_state("TOPOLOGY");
                }
            }
            PROF_ZONE_END(PROF_ZONE_CHUNK_UPDATE);

            PROF_ZONE_BEGIN(PROF_ZONE_CHUNK_STREAMING);
            if (startup_safe_mode_active()) {
                sdk_chunk_streamer_schedule_phase(&g_sdk.chunk_streamer,
                                                 &g_sdk.chunk_mgr,
                                                 SDK_CHUNK_STREAM_SCHEDULE_VISIBLE_ONLY,
                                                 startup_safe_primary_radius(),
                                                 max(8, sdk_chunk_streamer_active_workers(&g_sdk.chunk_streamer) * 2));
            } else {
                sdk_chunk_streamer_schedule_phase(&g_sdk.chunk_streamer,
                                                 &g_sdk.chunk_mgr,
                                                 SDK_CHUNK_STREAM_SCHEDULE_FULL_RUNTIME,
                                                 0,
                                                 0);
            }
            PROF_ZONE_END(PROF_ZONE_CHUNK_STREAMING);
            
            PROF_ZONE_BEGIN(PROF_ZONE_CHUNK_MESHING);
            process_dirty_chunk_mesh_uploads(dirty_remesh_jobs_per_frame());
            PROF_ZONE_END(PROF_ZONE_CHUNK_MESHING);
            
            PROF_ZONE_BEGIN(PROF_ZONE_CHUNK_ADOPTION);
            process_streamed_chunk_results(stream_upload_limit_per_frame());
            process_pending_chunk_gpu_uploads(max(1, stream_upload_limit_per_frame() / 2),
                                              stream_gpu_upload_budget_ms());
            PROF_ZONE_END(PROF_ZONE_CHUNK_ADOPTION);

            maybe_log_startup_readiness();
            maybe_log_chunk_residency_stall();
            tick_startup_safe_mode();
        } else {
            g_sdk.chunk_mgr.cam_cx = cam_cx;
            g_sdk.chunk_mgr.cam_cz = cam_cz;
            sdk_chunk_manager_rebuild_lookup(&g_sdk.chunk_mgr);
            rebuild_loaded_dirty_chunks_sync(8);
        }
    }

    PROF_ZONE_BEGIN(PROF_ZONE_RENDERING);
    SdkResult r = sdk_renderer_frame();
    PROF_ZONE_END(PROF_ZONE_RENDERING);
    
    PROF_FRAME_END();
    
    if (r != SDK_OK) {
        sdk_debug_log_output("[NQL SDK] nqlsdk_frame: renderer_frame failed\n");
    }
    return r;
}

/* ======================================================================
 * SHUTDOWN
 * ====================================================================== */

void nqlsdk_shutdown(void)
{
    /* Shuts down the SDK and releases all resources */
    if (!g_sdk.running) return;

    sdk_load_trace_note("runtime_shutdown_begin", "nqlsdk_shutdown");
    shutdown_world_session(true);
    save_graphics_settings_now();
    save_input_settings_now();

    sdk_renderer_shutdown();
    sdk_window_destroy(g_sdk.window);

    g_sdk.window  = NULL;
    g_sdk.running = false;
    sdk_load_trace_note("runtime_shutdown_complete", "nqlsdk_shutdown");
}

/* ======================================================================
 * QUERY
 * ====================================================================== */

bool nqlsdk_is_running(void)
{
    /* Returns true if the SDK is currently initialized and running */
    return g_sdk.running;
}
