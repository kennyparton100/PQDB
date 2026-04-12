#include "sdk_station_internal.h"
#include "sdk_crafting.h"

int craft_grid_w(void) { /* Returns crafting grid width (2 for inventory, 3 for table) */ return g_craft_is_table ? 3 : 2; }
int craft_grid_h(void) { /* Returns crafting grid height (2 for inventory, 3 for table) */ return g_craft_is_table ? 3 : 2; }

/** Check recipe match and update result index. */
/* Updates craft result by matching current grid against recipes */
void craft_update_match(void)
{
    int w = craft_grid_w(), h = craft_grid_h();
    
    /* For 2x2 grids, match against 2x2 sub-pattern in 3x3 with appropriate offset */
    if (!g_craft_is_table) {
        ItemType grid2[4];
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++) {
                int idx = y * 2 + x;
                ItemType item = (g_craft_grid_count[idx] > 0) ? g_craft_grid[idx] : ITEM_NONE;
                /* Bounds check to prevent corruption */
                if (item < 0 || item > 200) {
                    char dbg[256];
                    sprintf(dbg, "[CRAFT] CORRUPTION: grid[%d][%d] = %d (count=%d)\n", y, x, item, g_craft_grid_count[idx]);
                    OutputDebugStringA(dbg);
                    item = ITEM_NONE;
                    g_craft_grid[idx] = ITEM_NONE;
                    g_craft_grid_count[idx] = 0;
                }
                grid2[idx] = item;
            }
        int old_result = g_craft_result_idx;
        g_craft_result_idx = sdk_crafting_match(grid2, 2, 2);
        
        /* Debug output disabled to prevent corruption spam */
    } else {
        ItemType grid3[9];
        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 3; x++)
                grid3[y * 3 + x] = (g_craft_grid_count[y * 3 + x] > 0) ? g_craft_grid[y * 3 + x] : ITEM_NONE;
        int old_result = g_craft_result_idx;
        g_craft_result_idx = sdk_crafting_match(grid3, 3, 3);
        
        /* Debug output disabled to prevent corruption spam */
    }
}

/** Take the craft result: consume grid items, add result to hotbar. */
/* Takes crafting result, consumes ingredients, adds to hotbar */
void craft_take_result(void)
{
    if (g_craft_result_idx < 0) return;
    const CraftRecipe* rec = &g_recipes[g_craft_result_idx];

    /* Consume one of each pattern slot */
    int w = craft_grid_w(), h = craft_grid_h();

    /* Find the offset where the pattern matched */
    /* Re-run match logic to find offset */
    int found_ox = -1, found_oy = -1;
    for (int oy = 0; oy <= h - rec->height && found_ox < 0; oy++) {
        for (int ox = 0; ox <= w - rec->width && found_ox < 0; ox++) {
            int match = 1;
            for (int gy = 0; gy < h && match; gy++) {
                for (int gx = 0; gx < w && match; gx++) {
                    int px = gx - ox, py = gy - oy;
                    ItemType expected = ITEM_NONE;
                    if (px >= 0 && px < rec->width && py >= 0 && py < rec->height)
                        expected = rec->pattern[py * rec->width + px];
                    ItemType actual = (g_craft_grid_count[gy * w + gx] > 0) ? g_craft_grid[gy * w + gx] : ITEM_NONE;
                    if (actual != expected) match = 0;
                }
            }
            if (match) { found_ox = ox; found_oy = oy; }
        }
    }
    if (found_ox < 0) return;

    /* Consume items from grid */
    for (int py = 0; py < rec->height; py++) {
        for (int px = 0; px < rec->width; px++) {
            if (rec->pattern[py * rec->width + px] != ITEM_NONE) {
                int gx = found_ox + px, gy = found_oy + py;
                int idx = gy * w + gx;
                g_craft_grid_count[idx]--;
                if (g_craft_grid_count[idx] <= 0) {
                    g_craft_grid[idx] = ITEM_NONE;
                    g_craft_grid_count[idx] = 0;
                }
            }
        }
    }

    /* Add result to hotbar */
    for (int i = 0; i < rec->result_count; i++)
        hotbar_add(rec->result);

    /* Re-check for another match */
    craft_update_match();
}

/** Check if mouse is over the crafting result slot. */
/* Returns true if mouse coordinates are over crafting result slot */
bool craft_mouse_over_result_slot(int32_t mouse_x, int32_t mouse_y)
{
    int w = craft_grid_w(), h = craft_grid_h();
    float cell = 44.0f, gap = 3.0f, pad = 8.0f;
    float grid_total_w = w * cell + (w - 1) * gap;
    float grid_total_h = h * cell + (h - 1) * gap;
    float panel_w = grid_total_w + cell + gap * 2 + cell + pad * 2;
    float panel_h = grid_total_h + pad * 2 + 20.0f;
    
    /* Get window size for centering */
    uint32_t win_w, win_h;
    sdk_window_size(g_sdk.window, &win_w, &win_h);
    
    float px = ((float)win_w - panel_w) * 0.5f;
    float py = ((float)win_h - panel_h) * 0.5f - 40.0f;
    float gx0 = px + pad;
    float gy0 = py + pad + 16.0f;
    float arrow_x = gx0 + grid_total_w + gap * 2;
    float rx = arrow_x + cell * 0.6f + gap * 2;
    float ry = gy0 + grid_total_h * 0.5f - cell * 0.5f;
    
    return (mouse_x >= rx && mouse_x < rx + cell &&
            mouse_y >= ry && mouse_y < ry + cell);
}

int station_ui_slot_at(int32_t mouse_x, int32_t mouse_y, int* out_inside_panel)
{
    /* Returns slot index (0=input, 1=fuel, 2=output) at mouse position, or -1 */
    /* Determines station UI slot under mouse coordinates */
    uint32_t win_w, win_h;
    float panel_w = 360.0f;
    float panel_h = 190.0f;
    float cell = 40.0f;
    float px;
    float py;
    float input_x;
    float input_y;
    float fuel_x;
    float fuel_y;
    float output_x;
    float output_y;
    int inside = 0;

    sdk_window_size(g_sdk.window, &win_w, &win_h);
    px = ((float)win_w - panel_w) * 0.5f;
    py = ((float)win_h - panel_h) * 0.5f - 30.0f;

    inside = (mouse_x >= px && mouse_x < px + panel_w &&
              mouse_y >= py && mouse_y < py + panel_h);
    if (out_inside_panel) *out_inside_panel = inside;

    input_x = px + 30.0f;
    input_y = py + 82.0f;
    fuel_x = px + 30.0f;
    fuel_y = py + 132.0f;
    output_x = px + panel_w - 70.0f;
    output_y = py + 107.0f;

    if (mouse_x >= input_x && mouse_x < input_x + cell &&
        mouse_y >= input_y && mouse_y < input_y + cell) {
        return 0;
    }
    if (g_station_open_kind == SDK_STATION_UI_FURNACE &&
        mouse_x >= fuel_x && mouse_x < fuel_x + cell &&
        mouse_y >= fuel_y && mouse_y < fuel_y + cell) {
        return 1;
    }
    if (mouse_x >= output_x && mouse_x < output_x + cell &&
        mouse_y >= output_y && mouse_y < output_y + cell) {
        return 2;
    }

    return -1;
}

void station_handle_ui_input(void)
{
    /* Handles mouse input for station UI (slot clicks, panel close) */
    int32_t mouse_x, mouse_y;
    int inside_panel = 0;
    int slot;
    bool lmb;
    bool rmb;

    if (!g_station_open) return;

    if ((g_station_open_kind == SDK_STATION_UI_FURNACE || g_station_open_kind == SDK_STATION_UI_CAMPFIRE) &&
        (g_station_open_index < 0 || g_station_open_index >= g_station_state_count)) {
        station_close_ui();
        return;
    }

    sdk_window_get_mouse_pos(g_sdk.window, &mouse_x, &mouse_y);
    slot = station_ui_slot_at(mouse_x, mouse_y, &inside_panel);
    g_station_hovered_slot = slot;

    lmb = sdk_window_is_mouse_down(g_sdk.window, 0);
    if (lmb && !g_station_lmb_was_down) {
        if (!inside_panel) {
            station_close_ui();
        } else if (slot == 2 && g_station_open_index >= 0) {
            station_take_output(g_station_open_index, 0);
        } else if (slot >= 0 && g_station_open_index >= 0) {
            station_place_from_hotbar(g_station_open_index, slot);
        }
    }
    g_station_lmb_was_down = lmb;

    rmb = sdk_window_is_mouse_down(g_sdk.window, 1);
    if (rmb && !g_station_rmb_was_down) {
        if (!inside_panel) {
            station_close_ui();
        } else if (slot == 2 && g_station_open_index >= 0) {
            station_take_output(g_station_open_index, 1);
        } else if (slot >= 0 && g_station_open_index >= 0) {
            station_remove_to_hotbar(g_station_open_index, slot, 1);
        }
    }
    g_station_rmb_was_down = rmb;
}

/** Craft as many items as possible (bulk craft). */
void craft_take_result_bulk(void)
{
    int crafted = 0;
    while (g_craft_result_idx >= 0 && crafted < 64) { /* Limit to prevent infinite loops */
        craft_take_result();
        crafted++;
    }
}

/** Move an item from hotbar selected slot into the craft grid cursor slot. */
void craft_place_from_hotbar(void)
{
    HotbarEntry* sel = &g_hotbar[g_hotbar_selected];
    if (sel->count <= 0 || sel->item == ITEM_NONE) return;
    int idx = g_craft_cursor;
    ItemType item = sel->item;
    /* Debug output removed */

    if (g_craft_grid_count[idx] > 0 && g_craft_grid[idx] != sel->item) return; /* Different item */

    /* Move one item into grid */
    g_craft_grid[idx] = sel->item;
    g_craft_grid_count[idx]++;
    sel->count--;
    if (sel->count <= 0) {
        sel->item = ITEM_NONE;
        sel->creative_block = BLOCK_AIR;
        sel->durability = 0;
    }
    craft_update_match();
}

/** Move an item from grid cursor slot back to hotbar. */
void craft_remove_to_hotbar(void)
{
    int idx = g_craft_cursor;
    if (g_craft_grid_count[idx] <= 0) return;

    hotbar_add(g_craft_grid[idx]);
    g_craft_grid_count[idx]--;
    if (g_craft_grid_count[idx] <= 0) {
        g_craft_grid[idx] = ITEM_NONE;
        g_craft_grid_count[idx] = 0;
    }
    craft_update_match();
}

/** Close crafting UI, return all grid items to hotbar. */
void craft_close(void)
{
    int w = craft_grid_w(), h = craft_grid_h();
    for (int i = 0; i < w * h; i++) {
        while (g_craft_grid_count[i] > 0) {
            hotbar_add(g_craft_grid[i]);
            g_craft_grid_count[i]--;
        }
        g_craft_grid[i] = ITEM_NONE;
    }
    g_craft_open = false;
    g_craft_result_idx = -1;
    g_craft_cursor = 0;
}
