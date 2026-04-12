#include "../API/Internal/sdk_api_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDK_SAVED_SERVERS_PATH "servers.json"

static const char* server_skip_ws(const char* p, const char* end)
{
    /* Skips whitespace characters in JSON text */
    while (p && p < end && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static char* server_read_text_file(const char* path, size_t* out_size)
{
    /* Reads entire text file into allocated buffer, returns NULL on error */
    FILE* file;
    long length;
    size_t read_count;
    char* text;

    if (out_size) *out_size = 0u;
    file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    text = (char*)malloc((size_t)length + 1u);
    if (!text) {
        fclose(file);
        return NULL;
    }
    read_count = fread(text, 1u, (size_t)length, file);
    fclose(file);
    if (read_count != (size_t)length) {
        free(text);
        return NULL;
    }
    text[length] = '\0';
    if (out_size) *out_size = (size_t)length;
    return text;
}

static void server_trim_ascii(char* text)
{
    /* Trims leading and trailing whitespace from ASCII string in place */
    size_t length;
    size_t start;

    if (!text) return;
    length = strlen(text);
    start = 0u;
    while (text[start] != '\0' && isspace((unsigned char)text[start])) {
        ++start;
    }
    if (start > 0u) {
        memmove(text, text + start, length - start + 1u);
        length = strlen(text);
    }
    while (length > 0u && isspace((unsigned char)text[length - 1u])) {
        text[length - 1u] = '\0';
        --length;
    }
}

static void server_copy_string(char* dst, size_t dst_len, const char* src)
{
    /* Copies src to dst with length limit, then trims whitespace */
    size_t di = 0u;

    if (!dst || dst_len == 0u) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[di] != '\0' && di + 1u < dst_len) {
        dst[di] = src[di];
        ++di;
    }
    dst[di] = '\0';
    server_trim_ascii(dst);
}

static int server_normalize_address(const char* raw_address, char* out_address, size_t out_len)
{
    /* Normalizes server address, adds default port if missing, returns 1 on success */
    char temp[SDK_START_MENU_SERVER_ADDRESS_MAX];

    if (!raw_address || !out_address || out_len == 0u) return 0;
    server_copy_string(temp, sizeof(temp), raw_address);
    if (temp[0] == '\0') return 0;

    if (!strchr(temp, ':')) {
        if (snprintf(out_address, out_len, "%s:%d", temp, SDK_SERVER_DEFAULT_PORT) <= 0) {
            return 0;
        }
    } else {
        server_copy_string(out_address, out_len, temp);
    }
    return out_address[0] != '\0';
}

static const char* server_find_key_value(const char* start, const char* end, const char* key)
{
    /* Locates JSON key and returns pointer to its value */
    char pattern[64];
    const char* pos;
    size_t key_len;

    if (!start || !end || !key) return NULL;
    key_len = strlen(key);
    if (key_len + 3u >= sizeof(pattern)) return NULL;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    pos = strstr(start, pattern);
    if (!pos || pos >= end) return NULL;
    pos += strlen(pattern);
    pos = server_skip_ws(pos, end);
    if (!pos || pos >= end || *pos != ':') return NULL;
    pos++;
    return server_skip_ws(pos, end);
}

static int server_parse_json_string(const char* start, const char* end, char* out_text, size_t out_len)
{
    /* Parses JSON string value, handles escapes, returns 1 on success */
    size_t out_index = 0u;
    const char* p = start;

    if (!out_text || out_len == 0u) return 0;
    out_text[0] = '\0';
    if (!p || p >= end || *p != '"') return 0;
    ++p;
    while (p < end && *p != '"') {
        char ch = *p++;
        if (ch == '\\' && p < end) {
            ch = *p++;
        }
        if (out_index + 1u < out_len) {
            out_text[out_index++] = ch;
        }
    }
    if (p >= end || *p != '"') return 0;
    out_text[out_index] = '\0';
    return 1;
}

static void server_write_escaped_json_string(FILE* file, const char* text)
{
    /* Writes JSON string with escape sequences to file */
    size_t i;

    fputc('"', file);
    if (!text) {
        fputc('"', file);
        return;
    }
    for (i = 0u; text[i] != '\0'; ++i) {
        char ch = text[i];
        if (ch == '\\' || ch == '"') {
            fputc('\\', file);
        }
        fputc(ch, file);
    }
    fputc('"', file);
}

static void server_clear_runtime_lists(void)
{
    /* Clears saved server and hosted server lists */
    g_saved_server_count = 0;
    g_saved_server_selected = 0;
    g_saved_server_scroll = 0;
    g_hosted_server_selected = 0;
    g_hosted_server_scroll = 0;
}

static void server_clamp_saved_selection(void)
{
    /* Clamps saved server selection and scroll to valid range */
    int total_rows = g_saved_server_count + 1;

    if (total_rows <= 0) total_rows = 1;
    g_saved_server_selected = api_clampi(g_saved_server_selected, 0, total_rows - 1);
    g_saved_server_scroll = api_clampi(g_saved_server_scroll, 0,
                                       api_clampi(total_rows - SDK_START_MENU_SERVER_VISIBLE_MAX,
                                                  0, total_rows));
}

static void server_populate_local_host_entry(const SdkWorldSaveMeta* world)
{
    /* Populates local host manager entry from world save meta */
    if (!world) return;
    memset(&g_local_host_manager, 0, sizeof(g_local_host_manager));
    g_local_host_manager.active = true;
    g_local_host_manager.hosted.active = true;
    strcpy_s(g_local_host_manager.hosted.world_save_id,
             sizeof(g_local_host_manager.hosted.world_save_id),
             world->folder_id);
    strcpy_s(g_local_host_manager.hosted.world_name,
             sizeof(g_local_host_manager.hosted.world_name),
             world->display_name);
    g_local_host_manager.hosted.world_seed = world->seed;
    snprintf(g_local_host_manager.hosted.address,
             sizeof(g_local_host_manager.hosted.address),
             "127.0.0.1:%d", SDK_SERVER_DEFAULT_PORT);
}

void sdk_server_runtime_reset(void)
{
    /* Resets server runtime state, clears lists and connection info */
    server_clear_runtime_lists();
    g_online_section_focus = 0;
    g_online_edit_selected = 0;
    g_online_edit_target_index = -1;
    g_online_edit_is_new = true;
    g_online_edit_name[0] = '\0';
    g_online_edit_address[0] = '\0';
    g_online_status[0] = '\0';
    g_frontend_forced_open = false;
    g_pending_world_launch_mode = SDK_WORLD_LAUNCH_STANDARD;
    memset(&g_local_host_manager, 0, sizeof(g_local_host_manager));
    memset(&g_client_connection, 0, sizeof(g_client_connection));
}

void sdk_server_runtime_tick(void)
{
    /* Ticks server runtime, handles local host stop requests */
    if (g_local_host_manager.stop_requested && !g_sdk.world_session_active) {
        memset(&g_local_host_manager, 0, sizeof(g_local_host_manager));
        memset(&g_client_connection, 0, sizeof(g_client_connection));
        g_frontend_forced_open = false;
    }
    if (!g_local_host_manager.active && !g_sdk.world_session_active) {
        g_frontend_forced_open = false;
    }
}

void sdk_server_runtime_on_world_session_stopped(void)
{
    /* Handles cleanup when world session stops */
    int stopped_local_host = g_local_host_manager.active || g_local_host_manager.stop_requested;

    if (g_local_host_manager.active || g_local_host_manager.stop_requested) {
        memset(&g_local_host_manager, 0, sizeof(g_local_host_manager));
    }
    memset(&g_client_connection, 0, sizeof(g_client_connection));
    g_frontend_forced_open = false;
    g_pending_world_launch_mode = SDK_WORLD_LAUNCH_STANDARD;
    if (stopped_local_host) {
        sdk_online_set_status("Local host stopped.");
    } else {
        sdk_online_clear_status();
    }
}

void sdk_saved_servers_load_all(void)
{
    /* Loads all saved servers from servers.json */
    char* text;
    size_t text_size = 0u;
    const char* p;
    const char* end;

    server_clear_runtime_lists();

    text = server_read_text_file(SDK_SAVED_SERVERS_PATH, &text_size);
    if (!text) {
        return;
    }

    p = strchr(text, '[');
    end = text + text_size;
    if (!p) {
        free(text);
        return;
    }
    ++p;

    while (p < end && g_saved_server_count < SDK_SAVED_SERVER_MAX) {
        const char* object_start;
        const char* object_end;
        const char* name_value;
        const char* address_value;
        SdkSavedServerEntry entry;
        char normalized_address[SDK_START_MENU_SERVER_ADDRESS_MAX];

        p = server_skip_ws(p, end);
        if (!p || p >= end || *p == ']') break;
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p != '{') break;
        object_start = p;
        object_end = strchr(object_start, '}');
        if (!object_end || object_end > end) break;

        memset(&entry, 0, sizeof(entry));
        name_value = server_find_key_value(object_start, object_end, "name");
        address_value = server_find_key_value(object_start, object_end, "address");
        if (name_value && address_value &&
            server_parse_json_string(name_value, object_end, entry.name, sizeof(entry.name)) &&
            server_parse_json_string(address_value, object_end, entry.address, sizeof(entry.address)) &&
            server_normalize_address(entry.address, normalized_address, sizeof(normalized_address))) {
            strcpy_s(entry.address, sizeof(entry.address), normalized_address);
            if (entry.name[0] == '\0') {
                strcpy_s(entry.name, sizeof(entry.name), entry.address);
            }
            g_saved_servers[g_saved_server_count++] = entry;
        }

        p = object_end + 1;
    }

    free(text);
    server_clamp_saved_selection();
}

int sdk_saved_server_upsert(int index, const char* name, const char* address)
{
    /* Creates or updates saved server entry, rewrites servers.json */
    FILE* file;
    SdkSavedServerEntry entry;
    char normalized_address[SDK_START_MENU_SERVER_ADDRESS_MAX];
    int i;

    memset(&entry, 0, sizeof(entry));
    server_copy_string(entry.name, sizeof(entry.name), name);
    if (!server_normalize_address(address, normalized_address, sizeof(normalized_address))) {
        return 0;
    }
    strcpy_s(entry.address, sizeof(entry.address), normalized_address);
    if (entry.name[0] == '\0') {
        strcpy_s(entry.name, sizeof(entry.name), entry.address);
    }

    if (index >= 0 && index < g_saved_server_count) {
        g_saved_servers[index] = entry;
    } else {
        if (g_saved_server_count >= SDK_SAVED_SERVER_MAX) return 0;
        g_saved_servers[g_saved_server_count++] = entry;
        index = g_saved_server_count - 1;
    }

    file = fopen(SDK_SAVED_SERVERS_PATH, "wb");
    if (!file) return 0;

    fprintf(file, "[\n");
    for (i = 0; i < g_saved_server_count; ++i) {
        fprintf(file, "  {\n");
        fprintf(file, "    \"name\": ");
        server_write_escaped_json_string(file, g_saved_servers[i].name);
        fprintf(file, ",\n    \"address\": ");
        server_write_escaped_json_string(file, g_saved_servers[i].address);
        fprintf(file, "\n  }%s\n", (i + 1 < g_saved_server_count) ? "," : "");
    }
    fprintf(file, "]\n");
    fclose(file);

    g_saved_server_selected = api_clampi(index + 1, 0, g_saved_server_count);
    server_clamp_saved_selection();
    return 1;
}

int sdk_saved_server_delete_entry(int index)
{
    /* Deletes saved server entry at index, rewrites servers.json */
    FILE* file;
    int i;

    if (index < 0 || index >= g_saved_server_count) return 0;

    for (i = index; i + 1 < g_saved_server_count; ++i) {
        g_saved_servers[i] = g_saved_servers[i + 1];
    }
    g_saved_server_count--;

    file = fopen(SDK_SAVED_SERVERS_PATH, "wb");
    if (!file) return 0;
    fprintf(file, "[\n");
    for (i = 0; i < g_saved_server_count; ++i) {
        fprintf(file, "  {\n");
        fprintf(file, "    \"name\": ");
        server_write_escaped_json_string(file, g_saved_servers[i].name);
        fprintf(file, ",\n    \"address\": ");
        server_write_escaped_json_string(file, g_saved_servers[i].address);
        fprintf(file, "\n  }%s\n", (i + 1 < g_saved_server_count) ? "," : "");
    }
    fprintf(file, "]\n");
    fclose(file);

    server_clamp_saved_selection();
    return 1;
}

void sdk_online_clear_status(void)
{
    /* Clears online status message */
    g_online_status[0] = '\0';
}

void sdk_online_set_status(const char* status)
{
    /* Sets online status message */
    if (!status) {
        g_online_status[0] = '\0';
        return;
    }
    strcpy_s(g_online_status, sizeof(g_online_status), status);
}

int sdk_local_host_can_start_world(const SdkWorldSaveMeta* meta)
{
    /* Returns true if world can be started as local host */
    if (!meta) return 0;
    if (!g_local_host_manager.active) return 1;
    return strcmp(g_local_host_manager.hosted.world_save_id, meta->folder_id) == 0;
}

int sdk_local_host_matches_world_id(const char* world_save_id)
{
    /* Returns true if local host is running given world save ID */
    if (!g_local_host_manager.active || !world_save_id || !world_save_id[0]) return 0;
    return strcmp(g_local_host_manager.hosted.world_save_id, world_save_id) == 0;
}

void sdk_prepare_world_launch(int launch_mode)
{
    /* Sets pending world launch mode for next session start */
    g_pending_world_launch_mode = launch_mode;
}

void sdk_finalize_world_launch(const SdkWorldSaveMeta* world)
{
    /* Finalizes world launch, sets up local host or connection as needed */
    int launch_mode = g_pending_world_launch_mode;

    g_pending_world_launch_mode = SDK_WORLD_LAUNCH_STANDARD;
    if (!world) {
        return;
    }

    if (launch_mode == SDK_WORLD_LAUNCH_LOCAL_HOST_JOIN ||
        launch_mode == SDK_WORLD_LAUNCH_LOCAL_HOST_BACKGROUND) {
        server_populate_local_host_entry(world);
        if (launch_mode == SDK_WORLD_LAUNCH_LOCAL_HOST_JOIN) {
            g_client_connection.connected = true;
            g_client_connection.kind = SDK_CLIENT_CONNECTION_LOCAL_HOST;
            strcpy_s(g_client_connection.server_name, sizeof(g_client_connection.server_name),
                     world->display_name);
            strcpy_s(g_client_connection.address, sizeof(g_client_connection.address),
                     g_local_host_manager.hosted.address);
            g_frontend_forced_open = false;
        } else {
            memset(&g_client_connection, 0, sizeof(g_client_connection));
            g_frontend_forced_open = true;
            g_frontend_view = SDK_START_MENU_VIEW_ONLINE;
            g_online_section_focus = 1;
            frontend_reset_nav_state();
            sdk_window_clear_char_queue(g_sdk.window);
            clear_non_frontend_ui();
            sdk_online_set_status("Local host started.");
        }
    } else {
        g_frontend_forced_open = false;
        memset(&g_client_connection, 0, sizeof(g_client_connection));
    }
}

void sdk_client_join_active_local_host(void)
{
    if (!g_local_host_manager.active || !g_sdk.world_session_active) {
        sdk_online_set_status("No local host is running.");
        return;
    }

    g_client_connection.connected = true;
    g_client_connection.kind = SDK_CLIENT_CONNECTION_LOCAL_HOST;
    strcpy_s(g_client_connection.server_name, sizeof(g_client_connection.server_name),
             g_local_host_manager.hosted.world_name);
    strcpy_s(g_client_connection.address, sizeof(g_client_connection.address),
             g_local_host_manager.hosted.address);
    g_frontend_forced_open = false;
    g_pause_menu_open = false;
    g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
    g_pause_menu_selected = 0;
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
    sdk_renderer_set_chunk_manager(&g_sdk.chunk_mgr);
    sdk_online_clear_status();
}

void sdk_client_disconnect_to_frontend(int frontend_view)
{
    if (!g_sdk.world_session_active) return;

    memset(&g_client_connection, 0, sizeof(g_client_connection));
    g_frontend_forced_open = true;
    g_pause_menu_open = false;
    g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
    g_pause_menu_selected = 0;
    memset(g_pause_menu_nav_was_down, 0, sizeof(g_pause_menu_nav_was_down));
    g_craft_open = false;
    g_skills_open = false;
    g_map_focus_open = false;
    station_close_ui();
    command_close();
    g_frontend_view = frontend_view;
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
    clear_non_frontend_ui();
    sdk_online_set_status("Local host still running.");
}

void sdk_local_host_request_stop(void)
{
    if (!g_local_host_manager.active || !g_sdk.world_session_active) return;

    g_local_host_manager.stop_requested = true;
    memset(&g_client_connection, 0, sizeof(g_client_connection));
    g_frontend_forced_open = false;
    begin_async_return_to_start();
}

int sdk_try_connect_saved_server(int index)
{
    if (index < 0 || index >= g_saved_server_count) return 0;
    sdk_online_set_status("Remote server networking is not implemented yet.");
    return 0;
}
