#include "sdk_frontend_internal.h"

bool g_world_generation_is_offline = false;
static bool g_world_create_seed_typing = false;
static int g_world_create_seed_digits = 0;

/**
 * Stops seed typing mode, resetting digit counter.
 */
static void world_create_seed_stop_typing(void)
{
    g_world_create_seed_typing = false;
    g_world_create_seed_digits = 0;
}

/**
 * Appends digit to world seed during typing mode.
 * 
 * @param digit The digit to append (0-9).
 */
static void world_create_seed_append_digit(uint32_t digit)
{
    uint64_t next;

    if (digit > 9u) return;
    if (!g_world_create_seed_typing) {
        g_world_create_seed = 0u;
        g_world_create_seed_typing = true;
        g_world_create_seed_digits = 0;
    }

    next = (uint64_t)g_world_create_seed * 10u + (uint64_t)digit;
    if (next > UINT32_MAX) {
        g_world_create_seed = UINT32_MAX;
    } else {
        g_world_create_seed = (uint32_t)next;
    }
    if (g_world_create_seed_digits < 10) {
        g_world_create_seed_digits++;
    }
}

/**
 * Removes last digit from world seed, returns 1 if changed.
 * 
 * @return 1 if the seed was changed, 0 otherwise.
 */
static int world_create_seed_backspace(void)
{
    if (!g_world_create_seed_typing) return 0;

    if (g_world_create_seed_digits <= 1) {
        g_world_create_seed = 0u;
        g_world_create_seed_digits = 0;
        return 1;
    }

    g_world_create_seed /= 10u;
    g_world_create_seed_digits--;
    return 1;
}

/**
 * Normalizes world creation settings based on superchunk/wall options.
 */
static void world_create_normalize_settings(void)
{
    int default_wall_grid_size;
    int wall_thickness_chunks;
    SdkWorldCoordinateSystem coordinate_system;

    if (g_world_create_superchunk_chunk_span <= 0) {
        g_world_create_superchunk_chunk_span = 16;
    }
    if (g_world_create_wall_thickness_blocks <= 0) {
        g_world_create_wall_thickness_blocks = CHUNK_WIDTH;
    }
    if (g_world_create_wall_thickness_blocks > CHUNK_WIDTH) {
        g_world_create_wall_thickness_blocks = CHUNK_WIDTH;
    }
    coordinate_system = (SdkWorldCoordinateSystem)g_world_create_coordinate_system;
    if (!sdk_world_coordinate_system_is_valid(coordinate_system)) {
        coordinate_system = SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    }
    g_world_create_coordinate_system = coordinate_system;
    g_world_create_superchunks_enabled =
        sdk_world_coordinate_system_uses_superchunks(coordinate_system) ? 1 : 0;
    g_world_create_wall_rings_shared = g_world_create_wall_rings_shared ? 1 : 0;
    wall_thickness_chunks = (g_world_create_wall_thickness_blocks + CHUNK_WIDTH - 1) / CHUNK_WIDTH;
    if (wall_thickness_chunks <= 0) wall_thickness_chunks = 1;
    default_wall_grid_size = g_world_create_superchunk_chunk_span + wall_thickness_chunks + wall_thickness_chunks;

    if (!g_world_create_walls_enabled) {
        g_world_create_wall_grid_size = default_wall_grid_size;
        g_world_create_wall_grid_offset_x = 0;
        g_world_create_wall_grid_offset_z = 0;
        g_world_create_wall_rings_shared = 1;
        return;
    }

    if (sdk_world_coordinate_system_detaches_walls(coordinate_system)) {
        if (g_world_create_wall_grid_size <= 2 ||
            g_world_create_wall_grid_size < default_wall_grid_size) {
            g_world_create_wall_grid_size = default_wall_grid_size;
        }
    } else {
        g_world_create_wall_grid_size = default_wall_grid_size;
        g_world_create_wall_grid_offset_x = 0;
        g_world_create_wall_grid_offset_z = 0;
        g_world_create_wall_rings_shared = 1;
    }
}

/**
 * Appends printable ASCII character to buffer if space available.
 * 
 * @param buffer The buffer to append to.
 * @param buffer_len The length of the buffer.
 * @param ch The character to append.
 */
static void online_text_append(char* buffer, size_t buffer_len, uint32_t ch)
{
    size_t len;

    if (!buffer || buffer_len == 0u) return;
    if (ch < 32u || ch >= 127u) return;
    len = strlen(buffer);
    if (len + 1u >= buffer_len) return;
    buffer[len] = (char)ch;
    buffer[len + 1u] = '\0';
}

/**
 * Removes last character from buffer, returns 1 if changed.
 * 
 * @param buffer The buffer to modify.
 * @return 1 if the buffer was changed, 0 otherwise.
 */
static int online_text_backspace(char* buffer)
{
    size_t len;

    if (!buffer) return 0;
    len = strlen(buffer);
    if (len == 0u) return 0;
    buffer[len - 1u] = '\0';
    return 1;
}

static int world_create_bool_from_delta(int current_value, int delta)
{
    /* Converts delta to boolean (positive=1, negative=0, zero=current) */
    if (delta > 0) return 1;
    if (delta < 0) return 0;
    return current_value ? 1 : 0;
}

static int world_create_coordinate_system_from_delta(int current_value, int delta)
{
    int next = current_value + delta;

    if (next < SDK_WORLD_COORDSYS_CHUNK_SYSTEM) {
        next = SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    }
    if (next > SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM) {
        next = SDK_WORLD_COORDSYS_CHUNK_SYSTEM;
    }
    return next;
}

void frontend_open_world_create_menu(void)
{
    /* Opens world creation menu with default settings */
    g_frontend_view = SDK_START_MENU_VIEW_WORLD_CREATE;
    g_world_create_selected = 0;
    g_world_create_seed = (uint32_t)GetTickCount64();
    g_world_create_render_distance =
        sdk_chunk_manager_radius_from_grid_size(g_graphics_settings.chunk_grid_size);
    g_world_create_spawn_type = 0;
    g_world_create_settlements_enabled = 1;
    g_world_create_construction_cells_enabled = 0;
    g_world_create_coordinate_system = SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM;
    g_world_create_superchunks_enabled = 1;
    g_world_create_superchunk_chunk_span = 16;
    g_world_create_walls_enabled = 1;
    g_world_create_wall_grid_size = 18;
    g_world_create_wall_grid_offset_x = 0;
    g_world_create_wall_grid_offset_z = 0;
    g_world_create_wall_thickness_blocks = CHUNK_WIDTH;
    g_world_create_wall_rings_shared = 1;
    world_create_normalize_settings();
    world_create_seed_stop_typing();
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

void frontend_open_world_generating(void)
{
    /* Opens world generation progress screen */
    g_frontend_view = SDK_START_MENU_VIEW_WORLD_GENERATING;
    g_world_generation_progress = 0.0f;
    g_world_generation_stage = 0;
    strcpy_s(g_world_generation_status, sizeof(g_world_generation_status), "Initializing...");
    frontend_reset_nav_state();
}

void frontend_open_world_actions(void)
{
    /* Opens world actions menu for selected world */
    frontend_refresh_worlds_if_needed();
    if (g_world_save_count <= 0) {
        frontend_open_world_select();
        return;
    }
    g_world_save_selected = api_clampi(g_world_save_selected, 0, g_world_save_count - 1);
    g_world_action_selected = 0;
    g_frontend_view = SDK_START_MENU_VIEW_WORLD_ACTIONS;
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

void frontend_reset_nav_state(void)
{
    /* Resets frontend navigation input state */
    memset(g_frontend_nav_was_down, 0, sizeof(g_frontend_nav_was_down));
}

void frontend_refresh_worlds_if_needed(void)
{
    /* Refreshes world/asset lists if refresh is pending */
    if (g_frontend_refresh_pending) {
        refresh_world_save_list();
        sdk_saved_servers_load_all();
        refresh_character_assets();
        refresh_animation_assets_for_selected_character();
        refresh_prop_assets();
        refresh_block_assets();
        refresh_item_assets();
        refresh_particle_effect_assets();
        g_frontend_refresh_pending = false;
    }
    migrate_legacy_world_save_if_needed();
    if (g_frontend_refresh_pending) {
        refresh_world_save_list();
        sdk_saved_servers_load_all();
        refresh_character_assets();
        refresh_animation_assets_for_selected_character();
        refresh_prop_assets();
        refresh_block_assets();
        refresh_item_assets();
        refresh_particle_effect_assets();
        g_frontend_refresh_pending = false;
    }
}

static void build_start_menu_ui(SdkStartMenuUI* ui)
{
    /* Builds UI state structure for start menu from global state */
    int i;
    int start_index;

    if (!ui) return;

    memset(ui, 0, sizeof(*ui));
    ui->open = !g_sdk.world_session_active ||
               g_frontend_forced_open ||
               g_frontend_view == SDK_START_MENU_VIEW_RETURNING_TO_START;
    ui->view = g_frontend_view;
    ui->main_selected = g_frontend_main_selected;
    ui->world_selected = g_world_save_selected;
    ui->world_scroll = g_world_save_scroll;
    ui->world_total_count = g_world_save_count;
    ui->world_count = g_world_save_count;
    ui->world_action_selected = g_world_action_selected;
    ui->world_action_can_play = selected_world_can_play_local();
    ui->world_action_can_toggle_host = selected_world_can_start_local_host() || selected_world_is_local_host();
    ui->world_action_can_generate_map = selected_world_can_generate_map();
    ui->world_action_hosted_selected_world = selected_world_is_local_host();
    ui->online_section_focus = g_online_section_focus;
    ui->saved_server_selected = g_saved_server_selected;
    ui->saved_server_scroll = g_saved_server_scroll;
    ui->saved_server_count = g_saved_server_count;
    ui->hosted_server_selected = g_hosted_server_selected;
    ui->hosted_server_scroll = g_hosted_server_scroll;
    ui->hosted_server_count = g_local_host_manager.active ? 1 : 0;
    ui->online_edit_selected = g_online_edit_selected;
    ui->online_edit_is_new = g_online_edit_is_new;
    strcpy_s(ui->online_edit_name, sizeof(ui->online_edit_name), g_online_edit_name);
    strcpy_s(ui->online_edit_address, sizeof(ui->online_edit_address), g_online_edit_address);
    strcpy_s(ui->status_text, sizeof(ui->status_text), g_online_status);
    ui->character_selected = g_character_menu_selected;
    ui->character_scroll = g_character_menu_scroll;
    ui->character_count = g_character_asset_count;
    ui->character_action_selected = g_character_action_selected;
    ui->animation_hub_selected = g_character_animation_menu_selected;
    ui->animation_selected = g_animation_menu_selected;
    ui->animation_scroll = g_animation_menu_scroll;
    ui->animation_count = g_animation_asset_count;
    ui->animation_action_selected = g_animation_action_selected;
    ui->prop_selected = g_prop_menu_selected;
    ui->prop_scroll = g_prop_menu_scroll;
    ui->prop_count = g_prop_asset_count;
    ui->prop_action_selected = g_prop_action_selected;
    ui->block_selected = g_block_menu_selected;
    ui->block_scroll = g_block_menu_scroll;
    ui->block_count = g_block_asset_count;
    ui->block_action_selected = g_block_action_selected;
    ui->item_selected = g_item_menu_selected;
    ui->item_scroll = g_item_menu_scroll;
    ui->item_count = g_item_asset_count;
    ui->item_action_selected = g_item_action_selected;
    ui->particle_effect_selected = g_particle_effect_menu_selected;
    ui->particle_effect_scroll = g_particle_effect_menu_scroll;
    ui->particle_effect_count = g_particle_effect_asset_count;
    ui->particle_effect_action_selected = g_particle_effect_action_selected;
    ui->world_create_seed = g_world_create_seed;
    ui->world_create_render_distance = g_world_create_render_distance;
    ui->world_create_spawn_type = g_world_create_spawn_type;
    ui->world_create_selected = g_world_create_selected;
    ui->world_create_settlements_enabled = g_world_create_settlements_enabled;
    ui->world_create_construction_cells_enabled = g_world_create_construction_cells_enabled;
    ui->world_create_coordinate_system = (uint8_t)g_world_create_coordinate_system;
    ui->world_create_superchunks_enabled = g_world_create_superchunks_enabled;
    ui->world_create_superchunk_chunk_span = g_world_create_superchunk_chunk_span;
    ui->world_create_walls_enabled = g_world_create_walls_enabled;
    ui->world_create_wall_grid_size = g_world_create_wall_grid_size;
    ui->world_create_wall_grid_offset_x = g_world_create_wall_grid_offset_x;
    ui->world_create_wall_grid_offset_z = g_world_create_wall_grid_offset_z;
    ui->world_create_wall_thickness_blocks = g_world_create_wall_thickness_blocks;
    ui->world_create_wall_rings_shared = g_world_create_wall_rings_shared ? true : false;
    ui->world_generating = g_world_generation_active;
    ui->world_generation_progress = g_world_generation_progress;
    ui->world_generation_stage = g_world_generation_stage;
    strncpy_s(ui->world_generation_status, sizeof(ui->world_generation_status),
              g_world_generation_status, _TRUNCATE);
    ui->world_map_generating = g_world_map_generation_active;
    ui->world_map_generation_progress = g_world_map_generation_progress;
    ui->world_map_generation_stage = g_world_map_generation_stage;
    ui->world_map_generation_threads = g_world_map_generation_threads;
    ui->world_map_generation_ring = g_world_map_generation_ring;
    ui->world_map_generation_tiles_done = g_world_map_generation_tiles_done;
    ui->world_map_generation_inflight = g_world_map_generation_inflight;
    ui->world_map_generation_queued_pages = g_world_map_generation_queued_pages;
    ui->world_map_generation_active_workers = g_world_map_generation_active_workers;
    ui->world_map_generation_last_tile_chunks = g_world_map_generation_last_tile_chunks;
    ui->world_map_generation_tiles_per_sec = g_world_map_generation_tiles_per_sec;
    ui->world_map_generation_last_save_age_seconds = g_world_map_generation_last_save_age_seconds;
    ui->world_map_generation_last_tile_ms = g_world_map_generation_last_tile_ms;
    strcpy_s(ui->world_map_generation_status, sizeof(ui->world_map_generation_status),
             g_world_map_generation_status);
    
    ui->world_gen_offline_generating = g_world_generation_is_offline;
    ui->world_gen_offline_progress = g_world_generation_progress;
    ui->world_gen_offline_stage = g_world_generation_stage;
    ui->world_gen_offline_threads = g_world_generation_threads;
    ui->world_gen_offline_superchunks_done = g_world_generation_superchunks_done;
    ui->world_gen_offline_current_ring = g_world_generation_current_ring;
    ui->world_gen_offline_chunks_per_sec = g_world_generation_chunks_per_sec;
    ui->world_gen_offline_active_workers = g_world_generation_active_workers;
    strcpy_s(ui->world_gen_offline_status, sizeof(ui->world_gen_offline_status),
             g_world_generation_status);

    ui->world_gen_summary_active = world_generation_summary_active();
    for (int i = 0; i < 16; ++i) {
        strcpy_s(ui->world_gen_summary_lines[i], sizeof(ui->world_gen_summary_lines[0]),
                 g_gen_summary_lines[i]);
    }
    ui->world_gen_summary_line_count = g_gen_summary_line_count;

    start_index = api_clampi(g_world_save_scroll, 0,
                             api_clampi(g_world_save_count - SDK_START_MENU_WORLD_VISIBLE_MAX, 0, g_world_save_count));
    ui->world_scroll = start_index;
    if (ui->world_count > SDK_START_MENU_WORLD_VISIBLE_MAX) {
        ui->world_count = SDK_START_MENU_WORLD_VISIBLE_MAX;
    }
    if (g_world_save_count - start_index < ui->world_count) {
        ui->world_count = g_world_save_count - start_index;
    }
    for (i = 0; i < ui->world_count; ++i) {
        ui->world_seed[i] = g_world_saves[start_index + i].seed;
        ui->world_has_save[i] = g_world_saves[start_index + i].has_save_data;
        strcpy_s(ui->world_name[i], sizeof(ui->world_name[i]), g_world_saves[start_index + i].display_name);
    }
    if (g_world_map_generation_active) {
        ui->selected_world_seed = g_world_map_generation_target.seed;
        strcpy_s(ui->selected_world_name, sizeof(ui->selected_world_name),
                 g_world_map_generation_target.display_name);
    } else if (g_world_save_count > 0) {
        int selected_index = api_clampi(g_world_save_selected, 0, g_world_save_count - 1);
        ui->selected_world_seed = g_world_saves[selected_index].seed;
        strcpy_s(ui->selected_world_name, sizeof(ui->selected_world_name),
                 g_world_saves[selected_index].display_name);
    }
    start_index = api_clampi(g_saved_server_scroll, 0,
                             api_clampi(g_saved_server_count + 1 - SDK_START_MENU_SERVER_VISIBLE_MAX,
                                        0, g_saved_server_count + 1));
    ui->saved_server_scroll = start_index;
    {
        int asset_start_index = (start_index > 0) ? (start_index - 1) : 0;
        int visible_asset_limit = SDK_START_MENU_SERVER_VISIBLE_MAX - ((start_index == 0) ? 1 : 0);
        if (visible_asset_limit < 0) visible_asset_limit = 0;
        ui->saved_server_visible_count = g_saved_server_count - asset_start_index;
        if (ui->saved_server_visible_count > visible_asset_limit) {
            ui->saved_server_visible_count = visible_asset_limit;
        }
        for (i = 0; i < ui->saved_server_visible_count; ++i) {
            strcpy_s(ui->saved_server_name[i], sizeof(ui->saved_server_name[i]),
                     g_saved_servers[asset_start_index + i].name);
            strcpy_s(ui->saved_server_address[i], sizeof(ui->saved_server_address[i]),
                     g_saved_servers[asset_start_index + i].address);
        }
    }
    ui->hosted_server_visible_count = ui->hosted_server_count;
    if (g_local_host_manager.active) {
        ui->hosted_server_seed[0] = g_local_host_manager.hosted.world_seed;
        strcpy_s(ui->hosted_server_name[0], sizeof(ui->hosted_server_name[0]),
                 g_local_host_manager.hosted.world_name);
        strcpy_s(ui->hosted_server_address[0], sizeof(ui->hosted_server_address[0]),
                 g_local_host_manager.hosted.address);
    }
    start_index = api_clampi(g_character_menu_scroll, 0,
                             api_clampi(g_character_asset_count + 1 - SDK_START_MENU_ASSET_VISIBLE_MAX, 0, g_character_asset_count + 1));
    ui->character_scroll = start_index;
    {
        int asset_start_index = (start_index > 0) ? (start_index - 1) : 0;
        int visible_asset_limit = SDK_START_MENU_ASSET_VISIBLE_MAX - ((start_index == 0) ? 1 : 0);
        if (visible_asset_limit < 0) visible_asset_limit = 0;
        ui->character_count = g_character_asset_count - asset_start_index;
        if (ui->character_count > visible_asset_limit) {
            ui->character_count = visible_asset_limit;
        }
        for (i = 0; i < ui->character_count; ++i) {
            strcpy_s(ui->character_name[i], sizeof(ui->character_name[i]),
                     g_character_assets[asset_start_index + i].display_name);
        }
    }
    if (g_selected_character_index >= 0 && g_selected_character_index < g_character_asset_count) {
        strcpy_s(ui->selected_character_name, sizeof(ui->selected_character_name),
                 g_character_assets[g_selected_character_index].display_name);
    }
    start_index = api_clampi(g_animation_menu_scroll, 0,
                             api_clampi(g_animation_asset_count - SDK_START_MENU_ASSET_VISIBLE_MAX, 0, g_animation_asset_count));
    ui->animation_scroll = start_index;
    if (ui->animation_count > SDK_START_MENU_ASSET_VISIBLE_MAX) {
        ui->animation_count = SDK_START_MENU_ASSET_VISIBLE_MAX;
    }
    if (g_animation_asset_count - start_index < ui->animation_count) {
        ui->animation_count = g_animation_asset_count - start_index;
    }
    for (i = 0; i < ui->animation_count; ++i) {
        strcpy_s(ui->animation_name[i], sizeof(ui->animation_name[i]),
                 g_animation_assets[start_index + i].display_name);
    }
    if (g_selected_animation_index >= 0 && g_selected_animation_index < g_animation_asset_count) {
        strcpy_s(ui->selected_animation_name, sizeof(ui->selected_animation_name),
                 g_animation_assets[g_selected_animation_index].display_name);
    }
    start_index = api_clampi(g_prop_menu_scroll, 0,
                             api_clampi(g_prop_asset_count + 1 - SDK_START_MENU_ASSET_VISIBLE_MAX, 0, g_prop_asset_count + 1));
    ui->prop_scroll = start_index;
    {
        int asset_start_index = (start_index > 0) ? (start_index - 1) : 0;
        int visible_asset_limit = SDK_START_MENU_ASSET_VISIBLE_MAX - ((start_index == 0) ? 1 : 0);
        if (visible_asset_limit < 0) visible_asset_limit = 0;
        ui->prop_count = g_prop_asset_count - asset_start_index;
        if (ui->prop_count > visible_asset_limit) {
            ui->prop_count = visible_asset_limit;
        }
        for (i = 0; i < ui->prop_count; ++i) {
            strcpy_s(ui->prop_name[i], sizeof(ui->prop_name[i]),
                     g_prop_assets[asset_start_index + i].display_name);
        }
    }
    if (g_selected_prop_index >= 0 && g_selected_prop_index < g_prop_asset_count) {
        strcpy_s(ui->selected_prop_name, sizeof(ui->selected_prop_name),
                 g_prop_assets[g_selected_prop_index].display_name);
    }
    start_index = api_clampi(g_block_menu_scroll, 0,
                             api_clampi(g_block_asset_count + 1 - SDK_START_MENU_ASSET_VISIBLE_MAX, 0, g_block_asset_count + 1));
    ui->block_scroll = start_index;
    {
        int asset_start_index = (start_index > 0) ? (start_index - 1) : 0;
        int visible_asset_limit = SDK_START_MENU_ASSET_VISIBLE_MAX - ((start_index == 0) ? 1 : 0);
        if (visible_asset_limit < 0) visible_asset_limit = 0;
        ui->block_count = g_block_asset_count - asset_start_index;
        if (ui->block_count > visible_asset_limit) {
            ui->block_count = visible_asset_limit;
        }
        for (i = 0; i < ui->block_count; ++i) {
            strcpy_s(ui->block_name[i], sizeof(ui->block_name[i]),
                     g_block_assets[asset_start_index + i].display_name);
        }
    }
    if (g_selected_block_index >= 0 && g_selected_block_index < g_block_asset_count) {
        strcpy_s(ui->selected_block_name, sizeof(ui->selected_block_name),
                 g_block_assets[g_selected_block_index].display_name);
    }
    start_index = api_clampi(g_item_menu_scroll, 0,
                             api_clampi(g_item_asset_count + 1 - SDK_START_MENU_ASSET_VISIBLE_MAX, 0, g_item_asset_count + 1));
    ui->item_scroll = start_index;
    {
        int asset_start_index = (start_index > 0) ? (start_index - 1) : 0;
        int visible_asset_limit = SDK_START_MENU_ASSET_VISIBLE_MAX - ((start_index == 0) ? 1 : 0);
        if (visible_asset_limit < 0) visible_asset_limit = 0;
        ui->item_count = g_item_asset_count - asset_start_index;
        if (ui->item_count > visible_asset_limit) {
            ui->item_count = visible_asset_limit;
        }
        for (i = 0; i < ui->item_count; ++i) {
            strcpy_s(ui->item_name[i], sizeof(ui->item_name[i]),
                     g_item_assets[asset_start_index + i].display_name);
        }
    }
    if (g_selected_item_index >= 0 && g_selected_item_index < g_item_asset_count) {
        strcpy_s(ui->selected_item_name, sizeof(ui->selected_item_name),
                 g_item_assets[g_selected_item_index].display_name);
    }
    start_index = api_clampi(g_particle_effect_menu_scroll, 0,
                             api_clampi(g_particle_effect_asset_count + 1 - SDK_START_MENU_ASSET_VISIBLE_MAX, 0, g_particle_effect_asset_count + 1));
    ui->particle_effect_scroll = start_index;
    {
        int asset_start_index = (start_index > 0) ? (start_index - 1) : 0;
        int visible_asset_limit = SDK_START_MENU_ASSET_VISIBLE_MAX - ((start_index == 0) ? 1 : 0);
        if (visible_asset_limit < 0) visible_asset_limit = 0;
        ui->particle_effect_count = g_particle_effect_asset_count - asset_start_index;
        if (ui->particle_effect_count > visible_asset_limit) {
            ui->particle_effect_count = visible_asset_limit;
        }
        for (i = 0; i < ui->particle_effect_count; ++i) {
            strcpy_s(ui->particle_effect_name[i], sizeof(ui->particle_effect_name[i]),
                     g_particle_effect_assets[asset_start_index + i].display_name);
        }
    }
    if (g_selected_particle_effect_index >= 0 &&
        g_selected_particle_effect_index < g_particle_effect_asset_count) {
        strcpy_s(ui->selected_particle_effect_name, sizeof(ui->selected_particle_effect_name),
                 g_particle_effect_assets[g_selected_particle_effect_index].display_name);
    }
}

static void clamp_scroll_selection(int total_rows, int visible_rows, int* io_selected, int* io_scroll)
{
    int max_scroll;

    if (!io_selected || !io_scroll) return;
    if (total_rows <= 0) {
        *io_selected = 0;
        *io_scroll = 0;
        return;
    }

    *io_selected = api_clampi(*io_selected, 0, total_rows - 1);
    max_scroll = api_clampi(total_rows - visible_rows, 0, total_rows);
    *io_scroll = api_clampi(*io_scroll, 0, max_scroll);
    if (*io_selected < *io_scroll) {
        *io_scroll = *io_selected;
    }
    if (*io_selected >= *io_scroll + visible_rows) {
        *io_scroll = *io_selected - visible_rows + 1;
    }
}

void set_world_generation_session_progress(float session_progress, const char* status)
{
    float clamped_progress = api_clampf(session_progress, 0.0f, 1.0f);

    g_world_generation_progress = k_world_generation_prep_progress_max +
                                  (1.0f - k_world_generation_prep_progress_max) * clamped_progress;
    if (status && status[0] != '\0') {
        strncpy_s(g_world_generation_status, sizeof(g_world_generation_status), status, _TRUNCATE);
    }
}

void push_start_menu_ui(void)
{
    SdkStartMenuUI ui;

    update_async_world_generation();
    update_async_world_map_generation();
    build_start_menu_ui(&ui);
    sdk_renderer_set_start_menu(&ui);
}

void present_start_menu_frame(void)
{
    SdkStartMenuUI ui;
    SdkResult menu_result;

    build_start_menu_ui(&ui);
    sdk_renderer_set_start_menu(&ui);
    sdk_renderer_set_lighting(0.35f, 0.08f, 0.10f, 0.16f);
    menu_result = sdk_renderer_frame();
    if (menu_result != SDK_OK) {
        OutputDebugStringA("[NQL SDK] present_start_menu_frame: renderer_frame failed\n");
    }
}

void clear_non_frontend_ui(void)
{
    SdkHotbarSlot hotbar[10];
    SdkCraftingUI crafting_ui;
    SdkStationUI station_ui;
    SdkSkillsUI skills_ui;
    SdkPauseMenuUI pause_ui;
    SdkCommandLineUI command_ui;
    SdkMapUI map_ui;
    SdkFluidDebugUI fluid_ui;
    SdkPerfDebugUI perf_ui;
    SdkEditorUI editor_ui;

    memset(hotbar, 0, sizeof(hotbar));
    memset(&crafting_ui, 0, sizeof(crafting_ui));
    memset(&station_ui, 0, sizeof(station_ui));
    memset(&skills_ui, 0, sizeof(skills_ui));
    memset(&pause_ui, 0, sizeof(pause_ui));
    memset(&command_ui, 0, sizeof(command_ui));
    memset(&map_ui, 0, sizeof(map_ui));
    memset(&fluid_ui, 0, sizeof(fluid_ui));
    memset(&perf_ui, 0, sizeof(perf_ui));
    memset(&editor_ui, 0, sizeof(editor_ui));

    sdk_renderer_set_chunk_manager(NULL);
    sdk_renderer_set_outline(0, 0, 0, false);
    sdk_renderer_set_placement_preview(NULL);
    sdk_renderer_set_hotbar(hotbar, 10, 0);
    sdk_renderer_set_health(0, 0, false, false, 0, 0);
    sdk_renderer_set_entities(NULL);
    sdk_renderer_set_player_character_mesh(NULL, 0);
    sdk_renderer_set_crafting(&crafting_ui);
    sdk_renderer_set_station(&station_ui);
    sdk_renderer_set_skills(&skills_ui);
    sdk_renderer_set_pause_menu(&pause_ui);
    sdk_renderer_set_editor(&editor_ui);
    sdk_renderer_set_command_line(&command_ui);
    sdk_renderer_set_map(&map_ui);
    sdk_renderer_set_fluid_debug(&fluid_ui);
    sdk_renderer_set_perf_debug(&perf_ui);
}

void frontend_open_main_menu(void)
{
    g_frontend_view = SDK_START_MENU_VIEW_MAIN;
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

void frontend_open_world_select(void)
{
    g_frontend_view = SDK_START_MENU_VIEW_WORLD_SELECT;
    g_world_action_selected = 0;
    frontend_refresh_worlds_if_needed();
    clamp_scroll_selection(g_world_save_count, SDK_START_MENU_WORLD_VISIBLE_MAX,
                           &g_world_save_selected, &g_world_save_scroll);
    frontend_reset_nav_state();
}

static int find_character_asset_index_by_id(const char* asset_id)
{
    int i;

    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_character_asset_count; ++i) {
        if (strcmp(g_character_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_animation_asset_index_by_id(const char* asset_id)
{
    int i;

    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_animation_asset_count; ++i) {
        if (strcmp(g_animation_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_prop_asset_index_by_id(const char* asset_id)
{
    int i;

    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_prop_asset_count; ++i) {
        if (strcmp(g_prop_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_block_asset_index_by_id(const char* asset_id)
{
    int i;

    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_block_asset_count; ++i) {
        if (strcmp(g_block_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_item_asset_index_by_id(const char* asset_id)
{
    int i;

    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_item_asset_count; ++i) {
        if (strcmp(g_item_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_particle_effect_asset_index_by_id(const char* asset_id)
{
    int i;

    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_particle_effect_asset_count; ++i) {
        if (strcmp(g_particle_effect_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

void frontend_open_character_menu(void)
{
    frontend_refresh_worlds_if_needed();
    g_frontend_view = SDK_START_MENU_VIEW_CHARACTERS;
    if (g_character_menu_selected < 0) g_character_menu_selected = 0;
    if (g_character_menu_selected > g_character_asset_count) {
        g_character_menu_selected = g_character_asset_count;
    }
    clamp_scroll_selection(g_character_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                           &g_character_menu_selected, &g_character_menu_scroll);
    if (g_selected_character_index >= g_character_asset_count) {
        g_selected_character_index = g_character_asset_count - 1;
    }
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

static void frontend_open_prop_menu(void)
{
    frontend_refresh_worlds_if_needed();
    g_frontend_view = SDK_START_MENU_VIEW_PROPS;
    if (g_prop_menu_selected < 0) g_prop_menu_selected = 0;
    if (g_prop_menu_selected > g_prop_asset_count) g_prop_menu_selected = g_prop_asset_count;
    clamp_scroll_selection(g_prop_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                           &g_prop_menu_selected, &g_prop_menu_scroll);
    if (g_selected_prop_index >= g_prop_asset_count) g_selected_prop_index = g_prop_asset_count - 1;
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

static void frontend_open_block_menu(void)
{
    frontend_refresh_worlds_if_needed();
    g_frontend_view = SDK_START_MENU_VIEW_BLOCKS;
    if (g_block_menu_selected < 0) g_block_menu_selected = 0;
    if (g_block_menu_selected > g_block_asset_count) g_block_menu_selected = g_block_asset_count;
    clamp_scroll_selection(g_block_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                           &g_block_menu_selected, &g_block_menu_scroll);
    if (g_selected_block_index >= g_block_asset_count) g_selected_block_index = g_block_asset_count - 1;
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

static void frontend_open_item_menu(void)
{
    frontend_refresh_worlds_if_needed();
    g_frontend_view = SDK_START_MENU_VIEW_ITEMS;
    if (g_item_menu_selected < 0) g_item_menu_selected = 0;
    if (g_item_menu_selected > g_item_asset_count) g_item_menu_selected = g_item_asset_count;
    clamp_scroll_selection(g_item_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                           &g_item_menu_selected, &g_item_menu_scroll);
    if (g_selected_item_index >= g_item_asset_count) g_selected_item_index = g_item_asset_count - 1;
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

static void frontend_open_particle_effect_menu(void)
{
    frontend_refresh_worlds_if_needed();
    g_frontend_view = SDK_START_MENU_VIEW_PARTICLE_EFFECTS;
    if (g_particle_effect_menu_selected < 0) g_particle_effect_menu_selected = 0;
    if (g_particle_effect_menu_selected > g_particle_effect_asset_count) {
        g_particle_effect_menu_selected = g_particle_effect_asset_count;
    }
    clamp_scroll_selection(g_particle_effect_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                           &g_particle_effect_menu_selected, &g_particle_effect_menu_scroll);
    if (g_selected_particle_effect_index >= g_particle_effect_asset_count) {
        g_selected_particle_effect_index = g_particle_effect_asset_count - 1;
    }
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

static void frontend_open_character_actions_for_index(int character_index)
{
    if (character_index < 0 || character_index >= g_character_asset_count) return;
    g_selected_character_index = character_index;
    g_character_action_selected = 0;
    refresh_animation_assets_for_selected_character();
    g_frontend_view = SDK_START_MENU_VIEW_CHARACTER_ACTIONS;
    frontend_reset_nav_state();
}

static void frontend_open_character_animations_menu(void)
{
    if (g_selected_character_index < 0 || g_selected_character_index >= g_character_asset_count) return;
    g_character_animation_menu_selected = 0;
    refresh_animation_assets_for_selected_character();
    g_frontend_view = SDK_START_MENU_VIEW_CHARACTER_ANIMATIONS;
    frontend_reset_nav_state();
}

static void frontend_open_animation_list_menu(void)
{
    refresh_animation_assets_for_selected_character();
    g_animation_menu_selected = api_clampi(g_animation_menu_selected, 0,
                                           (g_animation_asset_count > 0) ? (g_animation_asset_count - 1) : 0);
    clamp_scroll_selection(g_animation_asset_count, SDK_START_MENU_ASSET_VISIBLE_MAX,
                           &g_animation_menu_selected, &g_animation_menu_scroll);
    g_frontend_view = SDK_START_MENU_VIEW_ANIMATION_LIST;
    frontend_reset_nav_state();
}

static void frontend_open_animation_actions_for_index(int animation_index)
{
    if (animation_index < 0 || animation_index >= g_animation_asset_count) return;
    g_selected_animation_index = animation_index;
    g_animation_action_selected = 0;
    g_frontend_view = SDK_START_MENU_VIEW_ANIMATION_ACTIONS;
    frontend_reset_nav_state();
}

static void frontend_open_prop_actions_for_index(int prop_index)
{
    if (prop_index < 0 || prop_index >= g_prop_asset_count) return;
    g_selected_prop_index = prop_index;
    g_prop_action_selected = 0;
    g_frontend_view = SDK_START_MENU_VIEW_PROP_ACTIONS;
    frontend_reset_nav_state();
}

static void frontend_open_block_actions_for_index(int block_index)
{
    if (block_index < 0 || block_index >= g_block_asset_count) return;
    g_selected_block_index = block_index;
    g_block_action_selected = 0;
    g_frontend_view = SDK_START_MENU_VIEW_BLOCK_ACTIONS;
    frontend_reset_nav_state();
}

static void frontend_open_item_actions_for_index(int item_index)
{
    if (item_index < 0 || item_index >= g_item_asset_count) return;
    g_selected_item_index = item_index;
    g_item_action_selected = 0;
    g_frontend_view = SDK_START_MENU_VIEW_ITEM_ACTIONS;
    frontend_reset_nav_state();
}

static void frontend_open_particle_effect_actions_for_index(int particle_effect_index)
{
    if (particle_effect_index < 0 || particle_effect_index >= g_particle_effect_asset_count) return;
    g_selected_particle_effect_index = particle_effect_index;
    g_particle_effect_action_selected = 0;
    g_frontend_view = SDK_START_MENU_VIEW_PARTICLE_EFFECT_ACTIONS;
    frontend_reset_nav_state();
}


void frontend_handle_input(void)
{
    bool nav_up = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_UP) ||
                  sdk_input_raw_key_down(VK_UP) || sdk_input_raw_key_down((uint8_t)'W');
    bool nav_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_DOWN) ||
                    sdk_input_raw_key_down(VK_DOWN) || sdk_input_raw_key_down((uint8_t)'S');
    bool nav_left = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_LEFT) ||
                    sdk_input_raw_key_down(VK_LEFT) || sdk_input_raw_key_down((uint8_t)'A');
    bool nav_right = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_RIGHT) ||
                     sdk_input_raw_key_down(VK_RIGHT) || sdk_input_raw_key_down((uint8_t)'D');
    bool activate = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_CONFIRM) ||
                    sdk_input_raw_key_down(VK_RETURN);
    bool backspace_key = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_BACK) ||
                         sdk_input_raw_key_down(VK_BACK);
    bool escape_key = sdk_input_raw_key_down(VK_ESCAPE);
    bool delete_key = sdk_input_raw_key_down(VK_DELETE);
    bool edit_key = sdk_input_raw_key_down((uint8_t)'E');
    bool backspace = backspace_key || escape_key;

    frontend_refresh_worlds_if_needed();

    if (g_frontend_view == SDK_START_MENU_VIEW_MAIN) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_frontend_main_selected > 0) g_frontend_main_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] && g_frontend_main_selected < SDK_FRONTEND_MAIN_OPTION_COUNT - 1) g_frontend_main_selected++;
        if (activate && !g_frontend_nav_was_down[4]) {
            if (g_local_host_manager.active &&
                g_frontend_main_selected >= 3 &&
                g_frontend_main_selected <= 9) {
                sdk_online_set_status("Stop the local host before opening other tools.");
            } else
            if (g_frontend_main_selected == 0) {
                if (g_world_save_count > 0) {
                    const SdkWorldSaveMeta* selected_world =
                        &g_world_saves[api_clampi(g_world_save_selected, 0, g_world_save_count - 1)];
                    if (selected_world_is_local_host() && g_frontend_forced_open) {
                        sdk_client_join_active_local_host();
                    } else if (selected_world_can_play_local()) {
                        begin_async_world_session_load(selected_world);
                    } else {
                        sdk_online_set_status("Stop the current local host first.");
                    }
                }
            } else if (g_frontend_main_selected == 1) {
                frontend_open_world_select();
            } else if (g_frontend_main_selected == 2) {
                frontend_open_online_menu();
            } else if (g_frontend_main_selected == 3) {
                frontend_open_world_create_menu();
            } else if (g_frontend_main_selected == 4) {
                frontend_open_character_menu();
            } else if (g_frontend_main_selected == 5) {
                frontend_open_prop_menu();
            } else if (g_frontend_main_selected == 6) {
                frontend_open_block_menu();
            } else if (g_frontend_main_selected == 7) {
                frontend_open_item_menu();
            } else if (g_frontend_main_selected == 8) {
                frontend_open_particle_effect_menu();
            } else if (g_frontend_main_selected == 9) {
                run_performance_benchmarks();
            } else if (g_frontend_main_selected == 10) {
                sdk_window_request_close(g_sdk.window);
            }
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_WORLD_SELECT) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_world_save_selected > 0) g_world_save_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] && g_world_save_selected < g_world_save_count - 1) g_world_save_selected++;
        clamp_scroll_selection(g_world_save_count, SDK_START_MENU_WORLD_VISIBLE_MAX,
                               &g_world_save_selected, &g_world_save_scroll);
        if (activate && !g_frontend_nav_was_down[4] && g_world_save_count > 0) {
            frontend_open_world_actions();
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_main_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_WORLD_ACTIONS) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_world_action_selected > 0) g_world_action_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] && g_world_action_selected < 4) g_world_action_selected++;
        if (activate && !g_frontend_nav_was_down[4] && g_world_save_count > 0) {
            switch (g_world_action_selected) {
                case 0:
                    if (selected_world_is_local_host()) {
                        sdk_client_join_active_local_host();
                    } else if (selected_world_can_play_local()) {
                        begin_async_local_host_start(&g_world_saves[g_world_save_selected], 1);
                    } else {
                        sdk_online_set_status("Stop the current local host first.");
                    }
                    break;
                case 1:
                    if (selected_world_is_local_host()) {
                        sdk_local_host_request_stop();
                    } else if (selected_world_can_start_local_host()) {
                        begin_async_local_host_start(&g_world_saves[g_world_save_selected], 0);
                    } else {
                        sdk_online_set_status("Stop the current local host first.");
                    }
                    break;
                case 2:
                    if (selected_world_can_generate_map()) {
                        begin_async_world_map_generation(&g_world_saves[g_world_save_selected]);
                    } else {
                        sdk_online_set_status("Stop local hosting before generating maps.");
                    }
                    break;
                case 3:
                    if (selected_world_can_generate_map()) {
                        begin_async_world_generation(&g_world_saves[g_world_save_selected]);
                    } else {
                        sdk_online_set_status("Stop local hosting before generating world.");
                    }
                    break;
                case 4:
                default:
                    frontend_open_world_select();
                    break;
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_world_select();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_ONLINE) {
        int saved_total_rows = g_saved_server_count + 1;
        int hosted_total_rows = g_local_host_manager.active ? 1 : 0;

        if ((nav_left && !g_frontend_nav_was_down[2]) ||
            (nav_right && !g_frontend_nav_was_down[3])) {
            g_online_section_focus = (g_online_section_focus == 0) ? 1 : 0;
        }

        if (g_online_section_focus == 0) {
            if (nav_up && !g_frontend_nav_was_down[0] && g_saved_server_selected > 0) {
                g_saved_server_selected--;
            }
            if (nav_down && !g_frontend_nav_was_down[1] &&
                g_saved_server_selected < saved_total_rows - 1) {
                g_saved_server_selected++;
            }
            clamp_scroll_selection(saved_total_rows, SDK_START_MENU_SERVER_VISIBLE_MAX,
                                   &g_saved_server_selected, &g_saved_server_scroll);
            if (activate && !g_frontend_nav_was_down[4]) {
                if (g_saved_server_selected == 0) {
                    frontend_open_online_edit_server(-1);
                } else {
                    sdk_try_connect_saved_server(g_saved_server_selected - 1);
                }
            }
            if (edit_key && !g_frontend_nav_was_down[6] && g_saved_server_selected > 0) {
                frontend_open_online_edit_server(g_saved_server_selected - 1);
            }
            if (delete_key && !g_frontend_nav_was_down[7] && g_saved_server_selected > 0) {
                if (sdk_saved_server_delete_entry(g_saved_server_selected - 1)) {
                    sdk_online_set_status("Saved server removed.");
                }
            }
        } else {
            if (hosted_total_rows <= 0) {
                g_hosted_server_selected = 0;
            } else {
                g_hosted_server_selected = api_clampi(g_hosted_server_selected, 0, hosted_total_rows - 1);
            }
            if (activate && !g_frontend_nav_was_down[4] && g_local_host_manager.active) {
                sdk_client_join_active_local_host();
            }
            if (delete_key && !g_frontend_nav_was_down[7] && g_local_host_manager.active) {
                sdk_local_host_request_stop();
            }
        }

        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_main_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_ONLINE_EDIT_SERVER) {
        char* active_field;
        size_t active_field_len;
        int consumed_backspace = 0;

        if (nav_up && !g_frontend_nav_was_down[0] && g_online_edit_selected > 0) {
            g_online_edit_selected--;
        }
        if (nav_down && !g_frontend_nav_was_down[1] && g_online_edit_selected < 3) {
            g_online_edit_selected++;
        }

        active_field = (g_online_edit_selected == 0)
            ? g_online_edit_name
            : g_online_edit_address;
        active_field_len = (g_online_edit_selected == 0)
            ? sizeof(g_online_edit_name)
            : sizeof(g_online_edit_address);

        if (g_online_edit_selected <= 1) {
            uint32_t ch;
            while (sdk_window_pop_char(g_sdk.window, &ch)) {
                online_text_append(active_field, active_field_len, ch);
            }
            if (backspace_key && !g_frontend_nav_was_down[5] && online_text_backspace(active_field)) {
                consumed_backspace = 1;
            }
        }

        if (activate && !g_frontend_nav_was_down[4]) {
            if (g_online_edit_selected == 2) {
                if (sdk_saved_server_upsert(g_online_edit_is_new ? -1 : g_online_edit_target_index,
                                            g_online_edit_name, g_online_edit_address)) {
                    sdk_online_set_status(g_online_edit_is_new
                                              ? "Saved server added."
                                              : "Saved server updated.");
                    frontend_open_online_menu();
                } else {
                    sdk_online_set_status("Enter a valid server address.");
                }
            } else if (g_online_edit_selected == 3) {
                frontend_open_online_menu();
            }
        }

        if (!consumed_backspace && backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_online_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_CHARACTERS) {
        int max_selected = g_character_asset_count;
        if (nav_up && !g_frontend_nav_was_down[0] && g_character_menu_selected > 0) g_character_menu_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] && g_character_menu_selected < max_selected) g_character_menu_selected++;
        clamp_scroll_selection(g_character_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                               &g_character_menu_selected, &g_character_menu_scroll);
        if (activate && !g_frontend_nav_was_down[4]) {
            if (g_character_menu_selected == 0) {
                SdkCharacterAssetMeta created;
                if (create_character_asset(&created)) {
                    refresh_character_assets();
                    g_selected_character_index = find_character_asset_index_by_id(created.asset_id);
                    if (g_selected_character_index < 0) g_selected_character_index = 0;
                    if (g_selected_character_index >= 0 &&
                        g_selected_character_index < g_character_asset_count) {
                        start_character_editor_session(&g_character_assets[g_selected_character_index]);
                    }
                }
            } else if (g_character_menu_selected - 1 < g_character_asset_count) {
                frontend_open_character_actions_for_index(g_character_menu_selected - 1);
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_main_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_CHARACTER_ACTIONS) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_character_action_selected > 0) g_character_action_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] &&
            g_character_action_selected < SDK_CHARACTER_ACTION_OPTION_COUNT - 1) {
            g_character_action_selected++;
        }
        if (activate && !g_frontend_nav_was_down[4] &&
            g_selected_character_index >= 0 && g_selected_character_index < g_character_asset_count) {
            switch (g_character_action_selected) {
                case 0:
                    start_character_editor_session(&g_character_assets[g_selected_character_index]);
                    break;
                case 1:
                    if (copy_character_asset(g_selected_character_index)) {
                        refresh_character_assets();
                        frontend_open_character_menu();
                    }
                    break;
                case 2:
                    if (delete_character_asset(g_selected_character_index)) {
                        refresh_character_assets();
                        frontend_open_character_menu();
                    }
                    break;
                case 3:
                    frontend_open_character_animations_menu();
                    break;
                case 4:
                default:
                    frontend_open_character_menu();
                    break;
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_character_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_CHARACTER_ANIMATIONS) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_character_animation_menu_selected > 0) {
            g_character_animation_menu_selected--;
        }
        if (nav_down && !g_frontend_nav_was_down[1] &&
            g_character_animation_menu_selected < SDK_CHARACTER_ANIMATION_MENU_OPTION_COUNT - 1) {
            g_character_animation_menu_selected++;
        }
        if (activate && !g_frontend_nav_was_down[4]) {
            if (g_character_animation_menu_selected == 0) {
                SdkAnimationAssetMeta created;
                if (create_animation_asset_for_selected_character(&created)) {
                    refresh_animation_assets_for_selected_character();
                    g_selected_animation_index = find_animation_asset_index_by_id(created.asset_id);
                    if (g_selected_animation_index >= 0 &&
                        g_selected_animation_index < g_animation_asset_count &&
                        g_selected_character_index >= 0 &&
                        g_selected_character_index < g_character_asset_count) {
                        start_animation_editor_session(
                            &g_character_assets[g_selected_character_index],
                            &g_animation_assets[g_selected_animation_index]);
                    }
                }
            } else if (g_character_animation_menu_selected == 1) {
                frontend_open_animation_list_menu();
            } else {
                frontend_open_character_actions_for_index(g_selected_character_index);
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_character_actions_for_index(g_selected_character_index);
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_ANIMATION_LIST) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_animation_menu_selected > 0) g_animation_menu_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] &&
            g_animation_menu_selected < g_animation_asset_count - 1) {
            g_animation_menu_selected++;
        }
        clamp_scroll_selection(g_animation_asset_count, SDK_START_MENU_ASSET_VISIBLE_MAX,
                               &g_animation_menu_selected, &g_animation_menu_scroll);
        if (activate && !g_frontend_nav_was_down[4] && g_animation_asset_count > 0) {
            frontend_open_animation_actions_for_index(g_animation_menu_selected);
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_character_animations_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_ANIMATION_ACTIONS) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_animation_action_selected > 0) g_animation_action_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] &&
            g_animation_action_selected < SDK_ANIMATION_ACTION_OPTION_COUNT - 1) {
            g_animation_action_selected++;
        }
        if (activate && !g_frontend_nav_was_down[4] &&
            g_selected_character_index >= 0 && g_selected_character_index < g_character_asset_count &&
            g_selected_animation_index >= 0 && g_selected_animation_index < g_animation_asset_count) {
            switch (g_animation_action_selected) {
                case 0:
                    start_animation_editor_session(
                        &g_character_assets[g_selected_character_index],
                        &g_animation_assets[g_selected_animation_index]);
                    break;
                case 1:
                    if (copy_selected_animation_asset()) {
                        refresh_animation_assets_for_selected_character();
                        frontend_open_animation_list_menu();
                    }
                    break;
                case 2:
                    if (delete_selected_animation_asset()) {
                        refresh_animation_assets_for_selected_character();
                        frontend_open_animation_list_menu();
                    }
                    break;
                case 3:
                default:
                    frontend_open_animation_list_menu();
                    break;
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_animation_list_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_PROPS) {
        int max_selected = g_prop_asset_count;
        if (nav_up && !g_frontend_nav_was_down[0] && g_prop_menu_selected > 0) g_prop_menu_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] && g_prop_menu_selected < max_selected) g_prop_menu_selected++;
        clamp_scroll_selection(g_prop_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                               &g_prop_menu_selected, &g_prop_menu_scroll);
        if (activate && !g_frontend_nav_was_down[4]) {
            if (g_prop_menu_selected == 0) {
                SdkPropAssetMeta created;
                if (create_prop_asset(&created)) {
                    refresh_prop_assets();
                    g_selected_prop_index = find_prop_asset_index_by_id(created.asset_id);
                    if (g_selected_prop_index < 0) g_selected_prop_index = 0;
                    if (g_selected_prop_index >= 0 && g_selected_prop_index < g_prop_asset_count) {
                        start_prop_editor_session(&g_prop_assets[g_selected_prop_index]);
                    }
                }
            } else if (g_prop_menu_selected - 1 < g_prop_asset_count) {
                frontend_open_prop_actions_for_index(g_prop_menu_selected - 1);
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_main_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_PROP_ACTIONS) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_prop_action_selected > 0) g_prop_action_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] &&
            g_prop_action_selected < SDK_GENERIC_ASSET_ACTION_OPTION_COUNT - 1) {
            g_prop_action_selected++;
        }
        if (activate && !g_frontend_nav_was_down[4] &&
            g_selected_prop_index >= 0 && g_selected_prop_index < g_prop_asset_count) {
            switch (g_prop_action_selected) {
                case 0:
                    start_prop_editor_session(&g_prop_assets[g_selected_prop_index]);
                    break;
                case 1:
                    if (copy_prop_asset(g_selected_prop_index)) {
                        refresh_prop_assets();
                        frontend_open_prop_menu();
                    }
                    break;
                case 2:
                    if (delete_prop_asset(g_selected_prop_index)) {
                        refresh_prop_assets();
                        frontend_open_prop_menu();
                    }
                    break;
                case 3:
                default:
                    frontend_open_prop_menu();
                    break;
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_prop_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_BLOCKS) {
        int max_selected = g_block_asset_count;
        if (nav_up && !g_frontend_nav_was_down[0] && g_block_menu_selected > 0) g_block_menu_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] && g_block_menu_selected < max_selected) g_block_menu_selected++;
        clamp_scroll_selection(g_block_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                               &g_block_menu_selected, &g_block_menu_scroll);
        if (activate && !g_frontend_nav_was_down[4]) {
            if (g_block_menu_selected == 0) {
                SdkBlockAssetMeta created;
                if (create_block_asset(&created)) {
                    refresh_block_assets();
                    g_selected_block_index = find_block_asset_index_by_id(created.asset_id);
                    if (g_selected_block_index < 0) g_selected_block_index = 0;
                    if (g_selected_block_index >= 0 && g_selected_block_index < g_block_asset_count) {
                        start_block_editor_session(&g_block_assets[g_selected_block_index]);
                    }
                }
            } else if (g_block_menu_selected - 1 < g_block_asset_count) {
                frontend_open_block_actions_for_index(g_block_menu_selected - 1);
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_main_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_BLOCK_ACTIONS) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_block_action_selected > 0) g_block_action_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] &&
            g_block_action_selected < SDK_GENERIC_ASSET_ACTION_OPTION_COUNT - 1) {
            g_block_action_selected++;
        }
        if (activate && !g_frontend_nav_was_down[4] &&
            g_selected_block_index >= 0 && g_selected_block_index < g_block_asset_count) {
            switch (g_block_action_selected) {
                case 0:
                    start_block_editor_session(&g_block_assets[g_selected_block_index]);
                    break;
                case 1:
                    if (copy_block_asset(g_selected_block_index)) {
                        refresh_block_assets();
                        frontend_open_block_menu();
                    }
                    break;
                case 2:
                    if (delete_block_asset(g_selected_block_index)) {
                        refresh_block_assets();
                        frontend_open_block_menu();
                    }
                    break;
                case 3:
                default:
                    frontend_open_block_menu();
                    break;
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_block_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_ITEMS) {
        int max_selected = g_item_asset_count;
        if (nav_up && !g_frontend_nav_was_down[0] && g_item_menu_selected > 0) g_item_menu_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] && g_item_menu_selected < max_selected) g_item_menu_selected++;
        clamp_scroll_selection(g_item_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                               &g_item_menu_selected, &g_item_menu_scroll);
        if (activate && !g_frontend_nav_was_down[4]) {
            if (g_item_menu_selected == 0) {
                SdkItemAssetMeta created;
                if (create_item_asset(&created)) {
                    refresh_item_assets();
                    g_selected_item_index = find_item_asset_index_by_id(created.asset_id);
                    if (g_selected_item_index < 0) g_selected_item_index = 0;
                    if (g_selected_item_index >= 0 && g_selected_item_index < g_item_asset_count) {
                        start_item_editor_session(&g_item_assets[g_selected_item_index]);
                    }
                }
            } else if (g_item_menu_selected - 1 < g_item_asset_count) {
                frontend_open_item_actions_for_index(g_item_menu_selected - 1);
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_main_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_ITEM_ACTIONS) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_item_action_selected > 0) g_item_action_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] &&
            g_item_action_selected < SDK_GENERIC_ASSET_ACTION_OPTION_COUNT - 1) {
            g_item_action_selected++;
        }
        if (activate && !g_frontend_nav_was_down[4] &&
            g_selected_item_index >= 0 && g_selected_item_index < g_item_asset_count) {
            switch (g_item_action_selected) {
                case 0:
                    start_item_editor_session(&g_item_assets[g_selected_item_index]);
                    break;
                case 1:
                    if (copy_item_asset(g_selected_item_index)) {
                        refresh_item_assets();
                        frontend_open_item_menu();
                    }
                    break;
                case 2:
                    if (delete_item_asset(g_selected_item_index)) {
                        refresh_item_assets();
                        frontend_open_item_menu();
                    }
                    break;
                case 3:
                default:
                    frontend_open_item_menu();
                    break;
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_item_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_PARTICLE_EFFECTS) {
        int max_selected = g_particle_effect_asset_count;
        if (nav_up && !g_frontend_nav_was_down[0] && g_particle_effect_menu_selected > 0) {
            g_particle_effect_menu_selected--;
        }
        if (nav_down && !g_frontend_nav_was_down[1] && g_particle_effect_menu_selected < max_selected) {
            g_particle_effect_menu_selected++;
        }
        clamp_scroll_selection(g_particle_effect_asset_count + 1, SDK_START_MENU_ASSET_VISIBLE_MAX,
                               &g_particle_effect_menu_selected, &g_particle_effect_menu_scroll);
        if (activate && !g_frontend_nav_was_down[4]) {
            if (g_particle_effect_menu_selected == 0) {
                SdkParticleEffectAssetMeta created;
                if (create_particle_effect_asset(&created)) {
                    refresh_particle_effect_assets();
                    g_selected_particle_effect_index = find_particle_effect_asset_index_by_id(created.asset_id);
                    if (g_selected_particle_effect_index < 0) g_selected_particle_effect_index = 0;
                    if (g_selected_particle_effect_index >= 0 &&
                        g_selected_particle_effect_index < g_particle_effect_asset_count) {
                        start_particle_effect_editor_session(
                            &g_particle_effect_assets[g_selected_particle_effect_index]);
                    }
                }
            } else if (g_particle_effect_menu_selected - 1 < g_particle_effect_asset_count) {
                frontend_open_particle_effect_actions_for_index(g_particle_effect_menu_selected - 1);
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_main_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_PARTICLE_EFFECT_ACTIONS) {
        if (nav_up && !g_frontend_nav_was_down[0] && g_particle_effect_action_selected > 0) {
            g_particle_effect_action_selected--;
        }
        if (nav_down && !g_frontend_nav_was_down[1] &&
            g_particle_effect_action_selected < SDK_GENERIC_ASSET_ACTION_OPTION_COUNT - 1) {
            g_particle_effect_action_selected++;
        }
        if (activate && !g_frontend_nav_was_down[4] &&
            g_selected_particle_effect_index >= 0 &&
            g_selected_particle_effect_index < g_particle_effect_asset_count) {
            switch (g_particle_effect_action_selected) {
                case 0:
                    start_particle_effect_editor_session(
                        &g_particle_effect_assets[g_selected_particle_effect_index]);
                    break;
                case 1:
                    if (copy_particle_effect_asset(g_selected_particle_effect_index)) {
                        refresh_particle_effect_assets();
                        frontend_open_particle_effect_menu();
                    }
                    break;
                case 2:
                    if (delete_particle_effect_asset(g_selected_particle_effect_index)) {
                        refresh_particle_effect_assets();
                        frontend_open_particle_effect_menu();
                    }
                    break;
                case 3:
                default:
                    frontend_open_particle_effect_menu();
                    break;
            }
        }
        if (backspace && !g_frontend_nav_was_down[5]) {
            frontend_open_particle_effect_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_WORLD_CREATE) {
        int previous_selected = g_world_create_selected;
        int consumed_backspace = 0;

        if (nav_up && !g_frontend_nav_was_down[0] && g_world_create_selected > 0) g_world_create_selected--;
        if (nav_down && !g_frontend_nav_was_down[1] && g_world_create_selected < 12) g_world_create_selected++;

        if (g_world_create_selected != previous_selected && g_world_create_selected != 0) {
            world_create_seed_stop_typing();
        }

        if (g_world_create_selected == 0) {
            uint32_t ch;

            while (sdk_window_pop_char(g_sdk.window, &ch)) {
                if (ch >= '0' && ch <= '9') {
                    world_create_seed_append_digit((uint32_t)(ch - '0'));
                }
            }

            if (backspace_key && !g_frontend_nav_was_down[5] && world_create_seed_backspace()) {
                consumed_backspace = 1;
            }
        }

        if ((nav_left && !g_frontend_nav_was_down[2]) || (nav_right && !g_frontend_nav_was_down[3])) {
            int delta = (nav_right && !g_frontend_nav_was_down[3]) ? 1 : -1;
            if (g_world_create_selected == 0) {
                g_world_create_seed = (uint32_t)(GetTickCount64() + rand());
                world_create_seed_stop_typing();
            } else if (g_world_create_selected == 1) {
                g_world_create_spawn_type = (g_world_create_spawn_type + delta + 3) % 3;
            } else if (g_world_create_selected == 2) {
                int current_idx = render_distance_preset_index(g_world_create_render_distance);
                current_idx = (current_idx + delta + SDK_RENDER_DISTANCE_PRESET_COUNT) %
                              SDK_RENDER_DISTANCE_PRESET_COUNT;
                g_world_create_render_distance = g_render_distance_presets[current_idx];
            } else if (g_world_create_selected == 3) {
                g_world_create_settlements_enabled =
                    world_create_bool_from_delta(g_world_create_settlements_enabled, delta);
            } else if (g_world_create_selected == 4) {
                g_world_create_construction_cells_enabled =
                    world_create_bool_from_delta(g_world_create_construction_cells_enabled, delta);
            } else if (g_world_create_selected == 5) {
                g_world_create_coordinate_system =
                    world_create_coordinate_system_from_delta(g_world_create_coordinate_system, delta);
                world_create_normalize_settings();
            } else if (g_world_create_selected == 6) {
                /* Cycle through chunk span values: 2, 4, 8, 16, 32, 64, 128 */
                const int spans[] = {2, 4, 8, 16, 32, 64, 128};
                const int span_count = sizeof(spans) / sizeof(spans[0]);
                int current_idx = 0;
                for (int i = 0; i < span_count; ++i) {
                    if (spans[i] == g_world_create_superchunk_chunk_span) {
                        current_idx = i;
                        break;
                    }
                }
                current_idx = (current_idx + delta + span_count) % span_count;
                g_world_create_superchunk_chunk_span = spans[current_idx];
                world_create_normalize_settings();
            } else if (g_world_create_selected == 7) {
                g_world_create_walls_enabled =
                    world_create_bool_from_delta(g_world_create_walls_enabled, delta);
                world_create_normalize_settings();
            } else if (g_world_create_selected == 8) {
                /* Grid size - editable only when detached walls are active */
                /* Integer input like seed - handled separately below */
            } else if (g_world_create_selected == 9) {
                /* Grid offset X - editable only when detached walls are active */
                /* Integer input like seed - handled separately below */
            } else if (g_world_create_selected == 10) {
                /* Grid offset Z - editable only when detached walls are active */
                /* Integer input like seed - handled separately below */
            } else if (g_world_create_selected == 11) {
                /* Wall thickness - editable as integer blocks below */
            } else if (g_world_create_selected == 12) {
                g_world_create_wall_rings_shared =
                    world_create_bool_from_delta(g_world_create_wall_rings_shared, delta);
                world_create_normalize_settings();
            }
        }

        if (g_world_create_selected == 0) {
            uint32_t ch;
            while (sdk_window_pop_char(g_sdk.window, &ch)) {
                if (ch >= '0' && ch <= '9') {
                    world_create_seed_append_digit((uint32_t)(ch - '0'));
                }
            }
            if (backspace_key && !g_frontend_nav_was_down[5] && world_create_seed_backspace()) {
                consumed_backspace = 1;
            }
        } else if (g_world_create_selected == 8) {
            uint32_t ch;
            if (g_world_create_walls_enabled &&
                g_world_create_coordinate_system == SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM) {
                while (sdk_window_pop_char(g_sdk.window, &ch)) {
                    if (ch >= '0' && ch <= '9') {
                        g_world_create_wall_grid_size = g_world_create_wall_grid_size * 10 + (ch - '0');
                        if (g_world_create_wall_grid_size > 100) g_world_create_wall_grid_size = 100;
                    }
                }
                if (backspace_key && !g_frontend_nav_was_down[5]) {
                    g_world_create_wall_grid_size = g_world_create_wall_grid_size / 10;
                    consumed_backspace = 1;
                }
            }
        } else if (g_world_create_selected == 9) {
            uint32_t ch;
            if (g_world_create_walls_enabled &&
                g_world_create_coordinate_system == SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM) {
                while (sdk_window_pop_char(g_sdk.window, &ch)) {
                    if (ch >= '0' && ch <= '9') {
                        g_world_create_wall_grid_offset_x = g_world_create_wall_grid_offset_x * 10 + (ch - '0');
                        if (g_world_create_wall_grid_offset_x > 1000) g_world_create_wall_grid_offset_x = 1000;
                    }
                }
                if (backspace_key && !g_frontend_nav_was_down[5]) {
                    g_world_create_wall_grid_offset_x = g_world_create_wall_grid_offset_x / 10;
                    consumed_backspace = 1;
                }
            }
        } else if (g_world_create_selected == 10) {
            uint32_t ch;
            if (g_world_create_walls_enabled &&
                g_world_create_coordinate_system == SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM) {
                while (sdk_window_pop_char(g_sdk.window, &ch)) {
                    if (ch >= '0' && ch <= '9') {
                        g_world_create_wall_grid_offset_z = g_world_create_wall_grid_offset_z * 10 + (ch - '0');
                        if (g_world_create_wall_grid_offset_z > 1000) g_world_create_wall_grid_offset_z = 1000;
                    }
                }
                if (backspace_key && !g_frontend_nav_was_down[5]) {
                    g_world_create_wall_grid_offset_z = g_world_create_wall_grid_offset_z / 10;
                    consumed_backspace = 1;
                }
            }
        } else if (g_world_create_selected == 11) {
            uint32_t ch;
            if (g_world_create_walls_enabled) {
                while (sdk_window_pop_char(g_sdk.window, &ch)) {
                    if (ch >= '0' && ch <= '9') {
                        g_world_create_wall_thickness_blocks =
                            g_world_create_wall_thickness_blocks * 10 + (ch - '0');
                        if (g_world_create_wall_thickness_blocks > CHUNK_WIDTH) {
                            g_world_create_wall_thickness_blocks = CHUNK_WIDTH;
                        }
                    }
                }
                if (backspace_key && !g_frontend_nav_was_down[5]) {
                    g_world_create_wall_thickness_blocks = g_world_create_wall_thickness_blocks / 10;
                    consumed_backspace = 1;
                }
                world_create_normalize_settings();
            }
        }

        if (activate && !g_frontend_nav_was_down[4]) {
            SdkWorldSaveMeta created_world;
            world_create_normalize_settings();
            sdk_load_trace_reset("world_create_menu_activate");
            sdk_load_trace_note("world_create_enter_pressed", "world create menu activate");
            if (create_new_world_save_with_settings(&created_world)) {
                sdk_load_trace_bind_meta(&created_world);
                sdk_load_trace_note("world_create_menu_success", created_world.folder_id);
                refresh_world_save_list();
                begin_async_world_session_load(&created_world);
            } else {
                sdk_load_trace_note("world_create_menu_failure", "create_new_world_save_with_settings failed");
            }
        }

        if (!consumed_backspace && backspace && !g_frontend_nav_was_down[5]) {
            world_create_seed_stop_typing();
            frontend_open_main_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_WORLD_MAP_GENERATING) {
        if (backspace && !g_frontend_nav_was_down[5] && g_world_map_generation_stage < 2) {
            request_async_world_map_generation_stop();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_WORLD_GENERATING) {
        if (world_generation_summary_active()) {
            if (activate && !g_frontend_nav_was_down[4]) {
                dismiss_world_generation_summary();
            }
        } else if (g_world_generation_stage < 2 && backspace && !g_frontend_nav_was_down[5]) {
            cancel_async_world_generation();
            frontend_open_main_menu();
        }
    } else if (g_frontend_view == SDK_START_MENU_VIEW_RETURNING_TO_START) {
        /* Input disabled while the world is saving and shutting down. */
    }

    g_frontend_nav_was_down[0] = nav_up;
    g_frontend_nav_was_down[1] = nav_down;
    g_frontend_nav_was_down[2] = nav_left;
    g_frontend_nav_was_down[3] = nav_right;
    g_frontend_nav_was_down[4] = activate;
    g_frontend_nav_was_down[5] = backspace;
    g_frontend_nav_was_down[6] = edit_key;
    g_frontend_nav_was_down[7] = delete_key;
}
