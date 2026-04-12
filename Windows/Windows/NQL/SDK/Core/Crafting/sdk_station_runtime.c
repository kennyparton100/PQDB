#include "sdk_station_internal.h"

/**
 * Returns true if block type is a station (furnace, campfire, anvil, etc.)
 */
int is_station_block(BlockType type)
{
    return type == BLOCK_FURNACE ||
           type == BLOCK_CAMPFIRE ||
           type == BLOCK_ANVIL ||
           type == BLOCK_BLACKSMITHING_TABLE ||
           type == BLOCK_LEATHERWORKING_TABLE;
}

/**
 * Returns true if block type is a processing station (furnace or campfire)
 */
int is_processing_station_block(BlockType type)
{
    return type == BLOCK_FURNACE || type == BLOCK_CAMPFIRE;
}

/**
 * Returns burn duration in ticks for fuel item (0 if not fuel)
 */
int station_fuel_value(ItemType item)
{
    switch (item) {
        case ITEM_COAL:         return 1200;
        case ITEM_BLOCK_LOG:    return 400;
        case ITEM_BLOCK_PLANKS: return 160;
        case ITEM_BLOCK_REEDS:  return 80;
        default: return 0;
    }
}

/**
 * Returns output item type for input processed at station (NONE if invalid)
 */
ItemType station_process_result(BlockType block_type, ItemType input_item)
{
    if (block_type == BLOCK_FURNACE) {
        if (input_item == ITEM_IRONSTONE) return ITEM_IRON_INGOT;
        if (input_item == ITEM_BLOCK_IRON_ORE) return ITEM_IRON_INGOT;
        if (input_item == ITEM_RAW_MEAT) return ITEM_COOKED_MEAT;
        return ITEM_NONE;
    }
    if (block_type == BLOCK_CAMPFIRE) {
        return (input_item == ITEM_RAW_MEAT) ? ITEM_COOKED_MEAT : ITEM_NONE;
    }
    return ITEM_NONE;
}

/**
 * Returns processing time in ticks for station type
 */
int station_progress_max(BlockType block_type)
{
    return (block_type == BLOCK_CAMPFIRE) ? CAMPFIRE_COOK_TIME : FURNACE_SMELT_TIME;
}

/**
 * Returns true if station has items/progress and should be persisted
 */
int station_state_is_meaningful(const StationState* state)
{
    if (!state || !state->active) return 0;
    return state->input_count > 0 ||
           state->fuel_count > 0 ||
           state->output_count > 0 ||
           state->progress > 0 ||
           state->burn_remaining > 0;
}

/**
 * Finds station state index at world position, returns -1 if not found
 */
int station_find_index(int wx, int wy, int wz)
{
    for (int i = 0; i < g_station_state_count; ++i) {
        if (!g_station_states[i].active) continue;
        if (g_station_states[i].wx == wx &&
            g_station_states[i].wy == wy &&
            g_station_states[i].wz == wz) {
            return i;
        }
    }
    return -1;
}

int station_ensure_state(int wx, int wy, int wz, BlockType block_type);

int station_find_state_public(int wx, int wy, int wz)
{
    /* Public wrapper to find station state index at position */
    return station_find_index(wx, wy, wz);
}

int station_ensure_state_public(int wx, int wy, int wz, BlockType block_type)
{
    /* Public wrapper to get or create station state at position */
    return station_ensure_state(wx, wy, wz, block_type);
}

const StationState* station_get_state_const(int index)
{
    /* Returns const pointer to station state at index, or NULL if invalid */
    if (index < 0 || index >= g_station_state_count) return NULL;
    if (!g_station_states[index].active) return NULL;
    return &g_station_states[index];
}

void station_close_ui(void)
{
    /* Closes station UI and resets open state */
    g_station_open = false;
    g_station_open_kind = SDK_STATION_UI_NONE;
    g_station_open_block_type = BLOCK_AIR;
    g_station_open_index = -1;
    g_station_hovered_slot = -1;
}

void station_remove_index(int index)
{
    /* Removes station state at index, swapping with last element */
    if (index < 0 || index >= g_station_state_count) return;

    if (g_station_open && g_station_open_index == index) {
        station_close_ui();
    } else if (g_station_open && g_station_open_index == g_station_state_count - 1) {
        g_station_open_index = index;
    }

    if (index != g_station_state_count - 1) {
        g_station_states[index] = g_station_states[g_station_state_count - 1];
    }
    memset(&g_station_states[g_station_state_count - 1], 0, sizeof(g_station_states[g_station_state_count - 1]));
    g_station_state_count--;
}

void station_remove_at(int wx, int wy, int wz)
{
    /* Removes station at position, spawning all items as drops */
    int index = station_find_index(wx, wy, wz);
    if (index >= 0) {
        StationState* state = &g_station_states[index];
        for (int i = 0; i < state->input_count; ++i) {
            sdk_entity_spawn_item(&g_sdk.entities, (float)wx + 0.5f, (float)wy + 0.5f, (float)wz + 0.5f, state->input_item);
        }
        for (int i = 0; i < state->fuel_count; ++i) {
            sdk_entity_spawn_item(&g_sdk.entities, (float)wx + 0.5f, (float)wy + 0.5f, (float)wz + 0.5f, state->fuel_item);
        }
        for (int i = 0; i < state->output_count; ++i) {
            sdk_entity_spawn_item(&g_sdk.entities, (float)wx + 0.5f, (float)wy + 0.5f, (float)wz + 0.5f, state->output_item);
        }
        station_remove_index(index);
    }
    sdk_persistence_remove_station_state(&g_sdk.persistence, wx, wy, wz);
}

void station_sync_to_persistence(int index)
{
    /* Saves station state to persistence if meaningful, removes if empty */
    SdkPersistedStationState persisted;
    StationState* state;

    if (index < 0 || index >= g_station_state_count) return;
    state = &g_station_states[index];
    if (!state->active) return;

    if (!station_state_is_meaningful(state)) {
        sdk_persistence_remove_station_state(&g_sdk.persistence, state->wx, state->wy, state->wz);
        return;
    }

    memset(&persisted, 0, sizeof(persisted));
    persisted.wx = state->wx;
    persisted.wy = state->wy;
    persisted.wz = state->wz;
    persisted.block_type = state->block_type;
    persisted.input_item = state->input_item;
    persisted.input_count = state->input_count;
    persisted.fuel_item = state->fuel_item;
    persisted.fuel_count = state->fuel_count;
    persisted.output_item = state->output_item;
    persisted.output_count = state->output_count;
    persisted.progress = state->progress;
    persisted.burn_remaining = state->burn_remaining;
    sdk_persistence_upsert_station_state(&g_sdk.persistence, &persisted);
}

void station_load_all_from_persistence(void)
{
    /* Loads all persisted station states into runtime array */
    int count = sdk_persistence_get_station_count(&g_sdk.persistence);
    g_station_state_count = 0;
    memset(g_station_states, 0, sizeof(g_station_states));

    for (int i = 0; i < count && g_station_state_count < MAX_STATION_STATES; ++i) {
        SdkPersistedStationState persisted;
        StationState* state;
        if (!sdk_persistence_get_station_state(&g_sdk.persistence, i, &persisted)) continue;
        state = &g_station_states[g_station_state_count++];
        memset(state, 0, sizeof(*state));
        state->active = true;
        state->wx = persisted.wx;
        state->wy = persisted.wy;
        state->wz = persisted.wz;
        state->block_type = persisted.block_type;
        state->input_item = persisted.input_item;
        state->input_count = persisted.input_count;
        state->fuel_item = persisted.fuel_item;
        state->fuel_count = persisted.fuel_count;
        state->output_item = persisted.output_item;
        state->output_count = persisted.output_count;
        state->progress = persisted.progress;
        state->burn_remaining = persisted.burn_remaining;
        state->burn_max = (persisted.burn_remaining > 0) ? persisted.burn_remaining : 1;
    }
}

int station_ensure_state(int wx, int wy, int wz, BlockType block_type)
{
    /* Gets existing station state or creates new one at position */
    int index = station_find_index(wx, wy, wz);
    if (index >= 0) {
        g_station_states[index].block_type = block_type;
        return index;
    }

    if (g_station_state_count >= MAX_STATION_STATES) return -1;

    index = g_station_state_count++;
    memset(&g_station_states[index], 0, sizeof(g_station_states[index]));
    g_station_states[index].active = true;
    g_station_states[index].wx = wx;
    g_station_states[index].wy = wy;
    g_station_states[index].wz = wz;
    g_station_states[index].block_type = block_type;
    g_station_states[index].burn_max = 1;

    if (is_processing_station_block(block_type)) {
        SdkPersistedStationState persisted;
        if (sdk_persistence_find_station_state(&g_sdk.persistence, wx, wy, wz, &persisted)) {
            g_station_states[index].input_item = persisted.input_item;
            g_station_states[index].input_count = persisted.input_count;
            g_station_states[index].fuel_item = persisted.fuel_item;
            g_station_states[index].fuel_count = persisted.fuel_count;
            g_station_states[index].output_item = persisted.output_item;
            g_station_states[index].output_count = persisted.output_count;
            g_station_states[index].progress = persisted.progress;
            g_station_states[index].burn_remaining = persisted.burn_remaining;
            g_station_states[index].burn_max = (persisted.burn_remaining > 0) ? persisted.burn_remaining : 1;
        }
    }

    return index;
}

void station_open_for_block(int wx, int wy, int wz, BlockType block_type)
{
    /* Opens station UI for block at position */
    g_station_open = true;
    g_station_open_block_type = block_type;
    g_station_hovered_slot = -1;

    if (block_type == BLOCK_FURNACE) {
        g_station_open_kind = SDK_STATION_UI_FURNACE;
        g_station_open_index = station_ensure_state(wx, wy, wz, block_type);
    } else if (block_type == BLOCK_CAMPFIRE) {
        g_station_open_kind = SDK_STATION_UI_CAMPFIRE;
        g_station_open_index = station_ensure_state(wx, wy, wz, block_type);
    } else {
        g_station_open_kind = SDK_STATION_UI_PLACEHOLDER;
        g_station_open_index = -1;
    }
}

int station_output_accepts(const StationState* state, ItemType result)
{
    /* Returns true if station output slot can accept result item */
    if (!state || result == ITEM_NONE) return 0;
    return state->output_count <= 0 ||
           (state->output_item == result && state->output_count < 64);
}

int station_can_place_in_slot(BlockType block_type, int slot, ItemType item)
{
    /* Returns true if item can be placed in station slot (0=input, 1=fuel) */
    if (item == ITEM_NONE) return 0;
    if (slot == 0) return station_process_result(block_type, item) != ITEM_NONE;
    if (slot == 1 && block_type == BLOCK_FURNACE) return station_fuel_value(item) > 0;
    return 0;
}

void station_take_output(int index, int take_all)
{
    /* Takes output items from station to hotbar (all or one) */
    StationState* state;
    if (index < 0 || index >= g_station_state_count) return;
    state = &g_station_states[index];
    if (state->output_count <= 0 || state->output_item == ITEM_NONE) return;

    if (take_all) {
        while (state->output_count > 0) {
            hotbar_add(state->output_item);
            state->output_count--;
        }
    } else {
        hotbar_add(state->output_item);
        state->output_count--;
    }

    if (state->output_count <= 0) {
        state->output_item = ITEM_NONE;
        state->output_count = 0;
    }
    station_sync_to_persistence(index);
}

void station_place_from_hotbar(int index, int slot)
{
    /* Places selected hotbar item into station slot */
    StationState* state;
    HotbarEntry* sel = &g_hotbar[g_hotbar_selected];

    if (index < 0 || index >= g_station_state_count || sel->count <= 0) return;
    state = &g_station_states[index];
    if (!station_can_place_in_slot(state->block_type, slot, sel->item)) return;

    if (slot == 0) {
        if (state->input_count > 0 && state->input_item != sel->item) return;
        state->input_item = sel->item;
        state->input_count++;
    } else if (slot == 1) {
        if (state->fuel_count > 0 && state->fuel_item != sel->item) return;
        state->fuel_item = sel->item;
        state->fuel_count++;
    } else {
        return;
    }

    sel->count--;
    if (sel->count <= 0) {
        sel->item = ITEM_NONE;
        sel->creative_block = BLOCK_AIR;
        sel->durability = 0;
    }
    station_sync_to_persistence(index);
}

int station_npc_place_item(int index, int slot, ItemType item)
{
    /* Places item into station slot by NPC (not from hotbar) */
    StationState* state;

    if (index < 0 || index >= g_station_state_count) return 0;
    state = &g_station_states[index];
    if (!state->active) return 0;
    if (!station_can_place_in_slot(state->block_type, slot, item)) return 0;

    if (slot == 0) {
        if (state->input_count > 0 && state->input_item != item) return 0;
        state->input_item = item;
        state->input_count++;
    } else if (slot == 1 && state->block_type == BLOCK_FURNACE) {
        if (state->fuel_count > 0 && state->fuel_item != item) return 0;
        state->fuel_item = item;
        state->fuel_count++;
    } else {
        return 0;
    }

    station_sync_to_persistence(index);
    return 1;
}

int station_npc_take_output(int index, ItemType* out_item)
{
    /* Takes output from station for NPC, returns 1 on success, fills out_item */
    StationState* state;
    ItemType item;

    if (out_item) *out_item = ITEM_NONE;
    if (index < 0 || index >= g_station_state_count) return 0;
    state = &g_station_states[index];
    if (!state->active || state->output_count <= 0 || state->output_item == ITEM_NONE) return 0;

    item = state->output_item;
    state->output_count--;
    if (state->output_count <= 0) {
        state->output_count = 0;
        state->output_item = ITEM_NONE;
    }
    if (out_item) *out_item = item;
    station_sync_to_persistence(index);
    return 1;
}

void station_remove_to_hotbar(int index, int slot, int take_all)
{
    /* Removes item from station slot back to hotbar (all or one) */
    StationState* state;
    ItemType item = ITEM_NONE;
    int* count_ptr = NULL;

    if (index < 0 || index >= g_station_state_count) return;
    state = &g_station_states[index];

    if (slot == 0) {
        item = state->input_item;
        count_ptr = &state->input_count;
    } else if (slot == 1 && state->block_type == BLOCK_FURNACE) {
        item = state->fuel_item;
        count_ptr = &state->fuel_count;
    } else {
        return;
    }

    if (*count_ptr <= 0 || item == ITEM_NONE) return;
    if (take_all) {
        while (*count_ptr > 0) {
            hotbar_add(item);
            (*count_ptr)--;
        }
    } else {
        hotbar_add(item);
        (*count_ptr)--;
    }

    if (*count_ptr <= 0) {
        if (slot == 0) state->input_item = ITEM_NONE;
        else state->fuel_item = ITEM_NONE;
        *count_ptr = 0;
    }
    station_sync_to_persistence(index);
}

void tick_station_state(int index)
{
    /* Processes one tick of station logic (fuel consumption, smelting progress) */
    ItemType result_item;
    StationState* state;
    SdkChunk* chunk;

    if (index < 0 || index >= g_station_state_count) return;
    state = &g_station_states[index];
    if (!state->active) return;

    chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr,
                                        sdk_world_to_chunk_x(state->wx),
                                        sdk_world_to_chunk_z(state->wz));
    if (!chunk || chunk->cx != sdk_world_to_chunk_x(state->wx) || chunk->cz != sdk_world_to_chunk_z(state->wz)) {
        return;
    }

    if (get_block_at(state->wx, state->wy, state->wz) != state->block_type) {
        station_remove_at(state->wx, state->wy, state->wz);
        return;
    }

    result_item = station_process_result(state->block_type, state->input_item);

    if (state->block_type == BLOCK_FURNACE) {
        int can_process = state->input_count > 0 &&
                          result_item != ITEM_NONE &&
                          station_output_accepts(state, result_item);

        if (state->burn_remaining > 0) {
            state->burn_remaining--;
        }

        if (state->burn_remaining <= 0 && can_process &&
            state->fuel_count > 0 && station_fuel_value(state->fuel_item) > 0) {
            state->burn_max = station_fuel_value(state->fuel_item);
            state->burn_remaining = state->burn_max;
            state->fuel_count--;
            if (state->fuel_count <= 0) {
                state->fuel_item = ITEM_NONE;
                state->fuel_count = 0;
            }
        }

        if (can_process && state->burn_remaining > 0) {
            state->progress++;
            if (state->progress >= FURNACE_SMELT_TIME) {
                state->progress = 0;
                state->input_count--;
                if (state->input_count <= 0) {
                    state->input_item = ITEM_NONE;
                    state->input_count = 0;
                }
                if (state->output_count <= 0) {
                    state->output_item = result_item;
                    state->output_count = 1;
                } else {
                    state->output_count++;
                }
            }
        } else if (!can_process) {
            state->progress = 0;
        }
    } else if (state->block_type == BLOCK_CAMPFIRE) {
        int can_process = state->input_count > 0 &&
                          result_item != ITEM_NONE &&
                          station_output_accepts(state, result_item);
        if (can_process) {
            state->progress++;
            if (state->progress >= CAMPFIRE_COOK_TIME) {
                state->progress = 0;
                state->input_count--;
                if (state->input_count <= 0) {
                    state->input_item = ITEM_NONE;
                    state->input_count = 0;
                }
                if (state->output_count <= 0) {
                    state->output_item = result_item;
                    state->output_count = 1;
                } else {
                    state->output_count++;
                }
            }
        } else {
            state->progress = 0;
        }
    }

    station_sync_to_persistence(index);
}

void station_tick_all(void)
{
    /* Ticks all active station states */
    int i = 0;
    while (i < g_station_state_count) {
        int before_count = g_station_state_count;
        tick_station_state(i);
        if (g_station_state_count == before_count) {
            ++i;
        }
    }
}

void station_handle_block_change(int wx, int wy, int wz, BlockType old_type, BlockType new_type)
{
    /* Handles block change, removes station state if station block changed */
    if (is_station_block(old_type) && old_type != new_type) {
        station_remove_at(wx, wy, wz);
    }
    if (g_station_open &&
        is_station_block(g_station_open_block_type) &&
        g_station_open_index >= 0 &&
        g_station_open_index < g_station_state_count) {
        StationState* open_state = &g_station_states[g_station_open_index];
        if (open_state->wx == wx && open_state->wy == wy && open_state->wz == wz &&
            new_type != g_station_open_block_type) {
            station_close_ui();
        }
    }
}
