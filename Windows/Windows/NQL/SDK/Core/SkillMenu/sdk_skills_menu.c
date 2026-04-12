#include "../Runtime/sdk_runtime_internal.h"

int total_skill_points_for_level(int level)
{
    /* Calculates total skill points for given level (triangular number) */
    if (level <= 0) return 0;
    return (level * (level + 1)) / 2;
}

int spent_general_skill_points(void)
{
    /* Calculates spent skill points across combat and survival skills */
    int spent = 0;
    int i;

    for (i = 0; i < SDK_COMBAT_SKILL_COUNT; ++i) spent += g_combat_skill_ranks[i];
    for (i = 0; i < SDK_SURVIVAL_SKILL_COUNT; ++i) spent += g_survival_skill_ranks[i];
    return spent;
}

int available_general_skill_points(void)
{
    /* Returns available unspent general skill points */
    int available = total_skill_points_for_level(g_player_level) - spent_general_skill_points();
    return available > 0 ? available : 0;
}

int skills_row_count_for_tab(int tab)
{
    /* Returns row count for given skill tab */
    switch (tab) {
        case SDK_SKILL_TAB_COMBAT: return SDK_COMBAT_SKILL_COUNT;
        case SDK_SKILL_TAB_SURVIVAL: return SDK_SURVIVAL_SKILL_COUNT;
        case SDK_SKILL_TAB_PROFESSIONS: return SDK_PROFESSION_NODE_COUNT;
        default: return 0;
    }
}

void skills_clamp_selection(void)
{
    /* Clamps skill menu selection to valid ranges */
    int row_count;

    if (g_skills_selected_tab < 0) g_skills_selected_tab = 0;
    if (g_skills_selected_tab >= SDK_SKILL_TAB_COUNT) g_skills_selected_tab = SDK_SKILL_TAB_COUNT - 1;
    if (g_skills_selected_profession < 0) g_skills_selected_profession = 0;
    if (g_skills_selected_profession >= SDK_PROFESSION_COUNT) g_skills_selected_profession = SDK_PROFESSION_COUNT - 1;

    row_count = skills_row_count_for_tab(g_skills_selected_tab);
    if (g_skills_selected_row < 0) g_skills_selected_row = 0;
    if (row_count <= 0) {
        g_skills_selected_row = 0;
    } else if (g_skills_selected_row >= row_count) {
        g_skills_selected_row = row_count - 1;
    }
}

void skills_reset_progression(void)
{
    /* Resets all skill and progression state to defaults */
    int i;

    g_skills_open = false;
    g_pause_menu_open = false;
    g_pause_menu_key_was_down = false;
    g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
    g_pause_menu_selected = 0;
    g_chunk_manager_selected = 0;
    g_chunk_manager_scroll = 0;
    g_graphics_menu_selected = 0;
    memset(g_pause_menu_nav_was_down, 0, sizeof(g_pause_menu_nav_was_down));
    g_creative_menu_selected = 0;
    g_creative_menu_scroll = 0;
    g_creative_menu_filter = (g_session_kind == SDK_SESSION_KIND_PROP_EDITOR)
        ? SDK_CREATIVE_FILTER_BUILDING_BLOCKS
        : SDK_CREATIVE_FILTER_ALL;
    g_creative_menu_search[0] = '\0';
    g_creative_menu_search_len = 0;
    g_command_open = false;
    g_command_text[0] = '\0';
    g_command_text_len = 0;
    g_command_enter_was_down = false;
    g_command_backspace_was_down = false;
    g_command_slash_was_down = false;
    g_map_focus_open = false;
    g_map_key_was_down = false;
    g_map_zoom_left_was_down = false;
    g_map_zoom_right_was_down = false;
    g_map_zoom_left_hold_frames = 0;
    g_map_zoom_right_hold_frames = 0;
    g_map_focus_world_x = 0.0f;
    g_map_focus_world_z = 0.0f;
    g_map_focus_initialized = false;
    g_map_zoom_tenths = 10;
    g_skills_key_was_down = false;
    g_skills_key_frames = 0;
    g_skills_selected_tab = SDK_SKILL_TAB_COMBAT;
    g_skills_selected_row = 0;
    g_skills_selected_profession = SDK_PROFESSION_MINING;
    memset(g_skills_nav_was_down, 0, sizeof(g_skills_nav_was_down));

    g_player_level = 1;
    g_player_xp = 0;
    g_player_xp_to_next = 100;
    memset(g_combat_skill_ranks, 0, sizeof(g_combat_skill_ranks));
    memset(g_survival_skill_ranks, 0, sizeof(g_survival_skill_ranks));
    memset(g_profession_points, 0, sizeof(g_profession_points));
    memset(g_profession_levels, 0, sizeof(g_profession_levels));
    memset(g_profession_xp, 0, sizeof(g_profession_xp));
    memset(g_profession_ranks, 0, sizeof(g_profession_ranks));
    for (i = 0; i < SDK_PROFESSION_COUNT; ++i) {
        g_profession_xp_to_next[i] = 25;
    }
}

void skills_handle_menu_input(void)
{
    /* Handles skills menu navigation input */
    bool nav_keys[8];

    nav_keys[0] = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_UP) ||
                  sdk_input_raw_key_down(VK_UP);
    nav_keys[1] = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_DOWN) ||
                  sdk_input_raw_key_down(VK_DOWN);
    nav_keys[2] = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_LEFT) ||
                  sdk_input_raw_key_down(VK_LEFT);
    nav_keys[3] = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_RIGHT) ||
                  sdk_input_raw_key_down(VK_RIGHT);
    nav_keys[4] = sdk_window_is_key_down(g_sdk.window, (uint8_t)'Q');
    nav_keys[5] = sdk_window_is_key_down(g_sdk.window, (uint8_t)'E');
    nav_keys[6] = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_CONFIRM) ||
                  sdk_input_raw_key_down(VK_RETURN);
    nav_keys[7] = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_MENU_BACK) ||
                  sdk_input_raw_key_down(VK_ESCAPE);

    if (nav_keys[2] && !g_skills_nav_was_down[2] && g_skills_selected_tab > 0) {
        g_skills_selected_tab--;
        g_skills_selected_row = 0;
    }
    if (nav_keys[3] && !g_skills_nav_was_down[3] && g_skills_selected_tab < SDK_SKILL_TAB_COUNT - 1) {
        g_skills_selected_tab++;
        g_skills_selected_row = 0;
    }
    if (nav_keys[0] && !g_skills_nav_was_down[0] && g_skills_selected_row > 0) {
        g_skills_selected_row--;
    }
    if (nav_keys[1] && !g_skills_nav_was_down[1] &&
        g_skills_selected_row < skills_row_count_for_tab(g_skills_selected_tab) - 1) {
        g_skills_selected_row++;
    }

    if (g_skills_selected_tab == SDK_SKILL_TAB_PROFESSIONS) {
        if (nav_keys[4] && !g_skills_nav_was_down[4] && g_skills_selected_profession > 0) {
            g_skills_selected_profession--;
        }
        if (nav_keys[5] && !g_skills_nav_was_down[5] &&
            g_skills_selected_profession < SDK_PROFESSION_COUNT - 1) {
            g_skills_selected_profession++;
        }
    }

    if (nav_keys[6] && !g_skills_nav_was_down[6]) {
        if (g_skills_selected_tab == SDK_SKILL_TAB_COMBAT) {
            if (available_general_skill_points() > 0 &&
                g_combat_skill_ranks[g_skills_selected_row] < GENERAL_SKILL_MAX_RANK) {
                g_combat_skill_ranks[g_skills_selected_row]++;
            }
        } else if (g_skills_selected_tab == SDK_SKILL_TAB_SURVIVAL) {
            if (available_general_skill_points() > 0 &&
                g_survival_skill_ranks[g_skills_selected_row] < GENERAL_SKILL_MAX_RANK) {
                g_survival_skill_ranks[g_skills_selected_row]++;
            }
        } else {
            int prof = g_skills_selected_profession;
            if (g_profession_points[prof] > 0 &&
                g_profession_ranks[prof][g_skills_selected_row] < PROFESSION_SKILL_MAX_RANK) {
                g_profession_ranks[prof][g_skills_selected_row]++;
                g_profession_points[prof]--;
            }
        }
    }

    if (nav_keys[7] && !g_skills_nav_was_down[7]) {
        g_skills_open = false;
    }

    skills_clamp_selection();
    memcpy(g_skills_nav_was_down, nav_keys, sizeof(g_skills_nav_was_down));
}

