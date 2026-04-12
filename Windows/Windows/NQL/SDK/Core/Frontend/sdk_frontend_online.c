#include "sdk_frontend_internal.h"

int selected_world_is_local_host(void)
{
    /* Returns true if selected world is currently hosted locally */
    if (g_world_save_count <= 0) return 0;
    return sdk_local_host_matches_world_id(g_world_saves[g_world_save_selected].folder_id);
}

int selected_world_can_start_local_host(void)
{
    /* Returns true if selected world can be started as local host */
    if (g_world_save_count <= 0) return 0;
    return sdk_local_host_can_start_world(&g_world_saves[g_world_save_selected]);
}

int selected_world_can_play_local(void)
{
    /* Returns true if selected world can be played (hosted or can start host) */
    if (g_world_save_count <= 0) return 0;
    if (selected_world_is_local_host()) return 1;
    return selected_world_can_start_local_host();
}

int selected_world_can_generate_map(void)
{
    /* Returns true if map can be generated (no active local host) */
    return !g_local_host_manager.active;
}

void frontend_open_online_menu(void)
{
    /* Opens online multiplayer menu */
    frontend_refresh_worlds_if_needed();
    g_frontend_view = SDK_START_MENU_VIEW_ONLINE;
    g_online_section_focus = 0;
    g_saved_server_selected = api_clampi(g_saved_server_selected, 0, g_saved_server_count);
    g_hosted_server_selected = 0;
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

void frontend_open_online_edit_server(int server_index)
{
    /* Opens server edit dialog for given index (-1 for new server) */
    SdkSavedServerEntry* entry = NULL;

    if (server_index >= 0 && server_index < g_saved_server_count) {
        entry = &g_saved_servers[server_index];
    }
    g_online_edit_is_new = (entry == NULL);
    g_online_edit_target_index = server_index;
    g_online_edit_selected = 0;
    g_online_edit_name[0] = '\0';
    g_online_edit_address[0] = '\0';
    if (entry) {
        strcpy_s(g_online_edit_name, sizeof(g_online_edit_name), entry->name);
        strcpy_s(g_online_edit_address, sizeof(g_online_edit_address), entry->address);
    }
    g_frontend_view = SDK_START_MENU_VIEW_ONLINE_EDIT_SERVER;
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

