/**
 * sdk_chunk_manager.c -- Superchunk-aware resident chunk manager.
 */
#include "sdk_chunk_manager.h"
#include "../../CoordinateSpaces/sdk_coordinate_space_runtime.h"
#include "../../ConstructionCells/sdk_construction_cells.h"
#include "../../Config/sdk_world_config.h"
#include "../../Settlements/Runtime/sdk_settlement_runtime.h"
#include "../../Superchunks/Config/sdk_superchunk_config.h"
#include "../../../Settings/sdk_settings.h"
#include "../../../API/Internal/sdk_load_trace.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SDK_VERBOSE_WALL_DEBUG_LOGS 0

static int floor_div_i(int value, int denom)
{
    return sdk_superchunk_floor_div_i(value, denom);
}

static int abs_i(int value)
{
    return value < 0 ? -value : value;
}

static int min_i(int a, int b)
{
    return a < b ? a : b;
}

static const int k_render_distance_presets[] = { 4, 6, 8, 10, 12, 16 };
static const int k_transition_bridge_depth_chunks = 4;
static const int k_transition_primary_radius_chunks = 4;
static const int k_background_expand_step_chunks = 2;

static uint32_t coord_hash(int cx, int cz)
{
    uint32_t h = (uint32_t)cx * 0x8da6b343u;
    h ^= (uint32_t)cz * 0xd8163841u;
    h ^= h >> 13u;
    h *= 0x85ebca6bu;
    h ^= h >> 16u;
    return h;
}

int sdk_chunk_manager_grid_size_from_radius(int radius)
{
    if (radius <= k_render_distance_presets[0]) {
        radius = k_render_distance_presets[0];
    } else if (radius >= k_render_distance_presets[(sizeof(k_render_distance_presets) / sizeof(k_render_distance_presets[0])) - 1]) {
        radius = k_render_distance_presets[(sizeof(k_render_distance_presets) / sizeof(k_render_distance_presets[0])) - 1];
    } else {
        int best_index = 0;
        int best_distance = INT_MAX;
        int i;
        for (i = 0; i < (int)(sizeof(k_render_distance_presets) / sizeof(k_render_distance_presets[0])); ++i) {
            int distance = abs_i(radius - k_render_distance_presets[i]);
            if (distance < best_distance) {
                best_distance = distance;
                best_index = i;
            }
        }
        radius = k_render_distance_presets[best_index];
    }
    return radius * 2 + 1;
}

int sdk_chunk_manager_radius_from_grid_size(int grid_size)
{
    int radius = (grid_size > 0) ? ((grid_size - 1) / 2) : k_render_distance_presets[2];
    int best_index = 0;
    int best_distance = INT_MAX;
    int i;

    for (i = 0; i < (int)(sizeof(k_render_distance_presets) / sizeof(k_render_distance_presets[0])); ++i) {
        int distance = abs_i(radius - k_render_distance_presets[i]);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return k_render_distance_presets[best_index];
}

int sdk_chunk_manager_normalize_grid_size(int grid_size)
{
    return sdk_chunk_manager_grid_size_from_radius(sdk_chunk_manager_radius_from_grid_size(grid_size));
}

static int role_priority(SdkChunkResidencyRole role)
{
    switch (role) {
        case SDK_CHUNK_ROLE_PRIMARY:            return 5;
        case SDK_CHUNK_ROLE_WALL_SUPPORT:       return 4;
        case SDK_CHUNK_ROLE_TRANSITION_PRELOAD: return 3;
        case SDK_CHUNK_ROLE_FRONTIER:           return 2;
        case SDK_CHUNK_ROLE_EVICT_PENDING:      return 1;
        case SDK_CHUNK_ROLE_NONE:
        default:                                return 0;
    }
}

static void desired_add(SdkChunkManager* cm, int cx, int cz, SdkChunkResidencyRole role);

static int chunk_is_wall_chunk_fully_ready_for_superchunk(const SdkChunkManager* cm,
                                                          int scx,
                                                          int scz,
                                                          const SdkChunk* chunk)
{
    if (!cm || !chunk) return 0;
    if (!sdk_superchunk_active_wall_chunk_contains_chunk(scx, scz, chunk->cx, chunk->cz)) {
        return 0;
    }
    return chunk->wall_finalized_generation == cm->topology_generation &&
           sdk_chunk_has_full_upload_ready_mesh(chunk);
}

static void emit_superchunk_full_neighborhood(SdkChunkManager* cm,
                                              int scx,
                                              int scz,
                                              SdkChunkResidencyRole role)
{
    SdkSuperchunkCell cell;
    int cx;
    int cz;

    if (!cm) return;
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    for (cz = cell.origin_cz; cz <= cell.south_cz; ++cz) {
        for (cx = cell.origin_cx; cx <= cell.east_cx; ++cx) {
            desired_add(cm, cx, cz, role);
        }
    }
}

static void emit_superchunk_full_neighborhood_ring(SdkChunkManager* cm,
                                                   int scx,
                                                   int scz,
                                                   SdkChunkResidencyRole role)
{
    SdkSuperchunkCell cell;
    int cx;
    int cz;

    if (!cm) return;
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    for (cz = cell.origin_cz; cz <= cell.south_cz; ++cz) {
        for (cx = cell.origin_cx; cx <= cell.east_cx; ++cx) {
            if (!sdk_superchunk_full_neighborhood_ring_contains_chunk(scx, scz, cx, cz)) continue;
            desired_add(cm, cx, cz, role);
        }
    }
}

static int superchunk_full_neighborhood_ready(const SdkChunkManager* cm, int scx, int scz)
{
    SdkSuperchunkCell cell;
    int cx;
    int cz;

    if (!cm) return 0;
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    for (cz = cell.origin_cz; cz <= cell.south_cz; ++cz) {
        for (cx = cell.origin_cx; cx <= cell.east_cx; ++cx) {
            SdkChunk* chunk;

            if (!sdk_superchunk_full_neighborhood_contains_chunk(scx, scz, cx, cz)) continue;
            chunk = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm, cx, cz);
            if (!chunk) return 0;

            if (sdk_superchunk_active_wall_chunk_contains_chunk(scx, scz, cx, cz)) {
                if (!chunk_is_wall_chunk_fully_ready_for_superchunk(cm, scx, scz, chunk)) {
                    return 0;
                }
                continue;
            }

            if (sdk_superchunk_full_neighborhood_ring_contains_chunk(scx, scz, cx, cz) &&
                !sdk_chunk_has_full_upload_ready_mesh(chunk)) {
                return 0;
            }
        }
    }

    return 1;
}

static int superchunk_primary_window_ready(const SdkChunkManager* cm,
                                           int scx,
                                           int scz,
                                           int focus_cx,
                                           int focus_cz,
                                           int radius)
{
    SdkSuperchunkCell cell;
    int min_cx;
    int max_cx;
    int min_cz;
    int max_cz;
    int dist;
    int cx;
    int cz;

    if (!cm) return 0;
    if (radius >= SDK_SUPERCHUNK_CHUNK_SPAN) {
        return superchunk_full_neighborhood_ready(cm, scx, scz);
    }
    if (radius < 0) radius = 0;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    min_cx = focus_cx - radius;
    max_cx = focus_cx + radius;
    min_cz = focus_cz - radius;
    max_cz = focus_cz + radius;

    if (min_cx < cell.interior_min_cx) min_cx = cell.interior_min_cx;
    if (max_cx > cell.interior_max_cx) max_cx = cell.interior_max_cx;
    if (min_cz < cell.interior_min_cz) min_cz = cell.interior_min_cz;
    if (max_cz > cell.interior_max_cz) max_cz = cell.interior_max_cz;

    for (dist = 0; dist <= radius; ++dist) {
        for (cz = min_cz; cz <= max_cz; ++cz) {
            for (cx = min_cx; cx <= max_cx; ++cx) {
                SdkChunk* chunk;

                if (abs_i(cx - focus_cx) > dist || abs_i(cz - focus_cz) > dist) continue;
                if (abs_i(cx - focus_cx) != dist && abs_i(cz - focus_cz) != dist && dist != 0) continue;

                chunk = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm, cx, cz);
                if (!chunk) return 0;
                if (!sdk_chunk_has_full_upload_ready_mesh(chunk)) return 0;
            }
        }
    }

    return 1;
}

static int chunk_in_superchunk(int cx, int cz, int scx, int scz)
{
    return sdk_superchunk_chunk_in_interior(scx, scz, cx, cz);
}

static int transition_primary_window_radius(const SdkChunkManager* cm)
{
    int chunk_span = sdk_superchunk_get_chunk_span();
    int radius = min_i(k_transition_primary_radius_chunks, chunk_span);
    if (!cm) return radius;
    if (radius < 1) radius = 1;
    return radius;
}

static int transition_bridge_depth(const SdkChunkManager* cm)
{
    int chunk_span = sdk_superchunk_get_chunk_span();
    int depth = min_i(k_transition_bridge_depth_chunks, chunk_span);
    if (!cm) return depth;
    if (depth < 1) depth = 1;
    return depth;
}

static void emit_previous_superchunk_transition_bridge(SdkChunkManager* cm)
{
    SdkSuperchunkCell prev_cell;
    int dx;
    int dz;
    int depth;
    int cx_min;
    int cx_max;
    int cz_min;
    int cz_max;
    int cx;
    int cz;

    if (!cm || !cm->transition_active) return;

    dx = cm->primary_scx - cm->prev_scx;
    dz = cm->primary_scz - cm->prev_scz;
    depth = transition_bridge_depth(cm);
    sdk_superchunk_cell_from_index(cm->prev_scx, cm->prev_scz, &prev_cell);

    cx_min = prev_cell.origin_cx;
    cx_max = prev_cell.east_cx;
    cz_min = prev_cell.origin_cz;
    cz_max = prev_cell.south_cz;

    if (dx > 0) {
        cx_min = prev_cell.interior_max_cx - depth + 1;
        if (cx_min < prev_cell.origin_cx) cx_min = prev_cell.origin_cx;
    } else if (dx < 0) {
        cx_max = prev_cell.interior_min_cx + depth - 1;
        if (cx_max > prev_cell.east_cx) cx_max = prev_cell.east_cx;
    } else if (dz > 0) {
        cz_min = prev_cell.interior_max_cz - depth + 1;
        if (cz_min < prev_cell.origin_cz) cz_min = prev_cell.origin_cz;
    } else if (dz < 0) {
        cz_max = prev_cell.interior_min_cz + depth - 1;
        if (cz_max > prev_cell.south_cz) cz_max = prev_cell.south_cz;
    }

    for (cz = cz_min; cz <= cz_max; ++cz) {
        for (cx = cx_min; cx <= cx_max; ++cx) {
            desired_add(cm, cx, cz, SDK_CHUNK_ROLE_TRANSITION_PRELOAD);
        }
    }
}

static int player_cleared_transition_bridge(const SdkChunkManager* cm, int cx, int cz)
{
    SdkSuperchunkCell cell;
    int dx;
    int dz;
    int depth;

    if (!cm || !cm->transition_active) return 0;

    sdk_superchunk_cell_from_index(cm->primary_scx, cm->primary_scz, &cell);
    dx = cm->primary_scx - cm->prev_scx;
    dz = cm->primary_scz - cm->prev_scz;
    depth = transition_bridge_depth(cm);

    if (dx > 0) return cx >= cell.interior_min_cx + depth;
    if (dx < 0) return cx <= cell.interior_max_cx - depth;
    if (dz > 0) return cz >= cell.interior_min_cz + depth;
    if (dz < 0) return cz <= cell.interior_max_cz - depth;
    return 1;
}

static int initial_primary_load_radius(const SdkChunkManager* cm)
{
    int chunk_span = sdk_superchunk_get_chunk_span();
    int radius = sdk_chunk_manager_grid_size(cm) / 2;

    if (radius > chunk_span) radius = chunk_span;
    return radius;
}

static int single_superchunk_mode_active(const SdkChunkManager* cm)
{
    const SdkSuperchunkConfig* config;
    int chunk_span;
    int render_radius;
    
    if (!cm) return 0;
    
    config = sdk_superchunk_get_config();
    if (!config || !config->enabled) return 0;
    
    chunk_span = sdk_superchunk_get_chunk_span();
    render_radius = sdk_chunk_manager_radius_from_grid_size(sdk_chunk_manager_grid_size(cm));
    
    return render_radius >= chunk_span;
}

static void reset_primary_load_window(SdkChunkManager* cm, int anchor_cx, int anchor_cz)
{
    if (!cm) return;
    cm->primary_anchor_cx = anchor_cx;
    cm->primary_anchor_cz = anchor_cz;
    cm->primary_load_radius = initial_primary_load_radius(cm);
    cm->primary_expanded = 0u;
}

static void clear_slot(SdkChunkResidentSlot* slot)
{
    int cx, cz;
    if (!slot) return;
    
    if (slot->occupied && slot->chunk.blocks) {
        cx = slot->chunk.cx;
        cz = slot->chunk.cz;
        sdk_settlement_runtime_notify_chunk_unloaded(cx, cz);
    }
    
    sdk_chunk_free(&slot->chunk);
    memset(slot, 0, sizeof(*slot));
}

static void desired_clear(SdkChunkManager* cm)
{
    if (!cm) return;
    cm->desired_count = 0;
}

static void desired_add(SdkChunkManager* cm, int cx, int cz, SdkChunkResidencyRole role)
{
    SdkCoordinateSpaceType space_type;
    int i;

    if (!cm || role == SDK_CHUNK_ROLE_NONE) return;
    space_type = sdk_coordinate_space_resolve_chunk_type(cx, cz);
    for (i = 0; i < cm->desired_count; ++i) {
        SdkChunkResidencyTarget* existing = &cm->desired[i];
        if (existing->cx == cx && existing->cz == cz) {
            if ((SdkCoordinateSpaceType)existing->space_type != space_type) {
                char dbg[256];
                sprintf_s(dbg, sizeof(dbg),
                          "[RESIDENCY] conflicting space_type for (%d,%d): existing=%s new=%s\n",
                          cx,
                          cz,
                          sdk_coordinate_space_type_name((SdkCoordinateSpaceType)existing->space_type),
                          sdk_coordinate_space_type_name(space_type));
                sdk_debug_log_output(dbg);
            }
            if (role_priority(role) > role_priority((SdkChunkResidencyRole)existing->role)) {
                existing->role = (uint8_t)role;
            }
            existing->space_type = (uint8_t)space_type;
            existing->generation = cm->topology_generation;
            return;
        }
    }
    if (cm->desired_count >= SDK_CHUNK_MANAGER_MAX_DESIRED) {
        sdk_debug_log_output("[RESIDENCY] desired set exceeded SDK_CHUNK_MANAGER_MAX_DESIRED\n");
        return;
    }
    cm->desired[cm->desired_count].cx = cx;
    cm->desired[cm->desired_count].cz = cz;
    cm->desired[cm->desired_count].space_type = (uint8_t)space_type;
    cm->desired[cm->desired_count].role = (uint8_t)role;
    cm->desired[cm->desired_count].reserved1 = 0u;
    cm->desired[cm->desired_count].generation = cm->topology_generation;
    cm->desired_count++;
}

static int chunk_within_primary_cap_margin(const SdkChunkManager* cm, int cx, int cz, int margin)
{
    if (!cm) return 0;
    if (margin < 0) margin = 0;
    return abs_i(cx - cm->primary_anchor_cx) <= cm->primary_load_radius + margin &&
           abs_i(cz - cm->primary_anchor_cz) <= cm->primary_load_radius + margin;
}

static int chunk_within_primary_cap(const SdkChunkManager* cm, int cx, int cz)
{
    return chunk_within_primary_cap_margin(cm, cx, cz, 0);
}

static void desired_add_if_primary_visible(SdkChunkManager* cm, int cx, int cz, SdkChunkResidencyRole role)
{
    int margin = 0;

    if (!cm) return;
    if (role == SDK_CHUNK_ROLE_FRONTIER || role == SDK_CHUNK_ROLE_WALL_SUPPORT) {
        margin = SDK_GATE_FRONTIER_DEPTH_CHUNKS;
    }
    if (!chunk_within_primary_cap_margin(cm, cx, cz, margin)) return;
    desired_add(cm, cx, cz, role);
}

static void emit_superchunk_window(SdkChunkManager* cm, int scx, int scz,
                                   SdkChunkResidencyRole role,
                                   int focus_cx, int focus_cz,
                                   int radius)
{
    SdkSuperchunkCell cell;
    int min_cx;
    int max_cx;
    int min_cz;
    int max_cz;
    int dist;
    int cx;
    int cz;

    if (!cm) return;
    if (radius < 0) radius = 0;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    min_cx = focus_cx - radius;
    max_cx = focus_cx + radius;
    min_cz = focus_cz - radius;
    max_cz = focus_cz + radius;

    if (min_cx < cell.interior_min_cx) min_cx = cell.interior_min_cx;
    if (max_cx > cell.interior_max_cx) max_cx = cell.interior_max_cx;
    if (min_cz < cell.interior_min_cz) min_cz = cell.interior_min_cz;
    if (max_cz > cell.interior_max_cz) max_cz = cell.interior_max_cz;

    for (dist = 0; dist <= radius; ++dist) {
        for (cz = min_cz; cz <= max_cz; ++cz) {
            for (cx = min_cx; cx <= max_cx; ++cx) {
                if (abs_i(cx - focus_cx) > dist || abs_i(cz - focus_cz) > dist) continue;
                if (abs_i(cx - focus_cx) != dist && abs_i(cz - focus_cz) != dist && dist != 0) continue;
                desired_add(cm, cx, cz, role);
            }
        }
    }
}

static void emit_camera_window(SdkChunkManager* cm, int focus_cx, int focus_cz,
                               int radius, SdkChunkResidencyRole role)
{
    int min_cx;
    int max_cx;
    int min_cz;
    int max_cz;
    int dist;
    int cx;
    int cz;

    if (!cm) return;
    if (radius < 0) radius = 0;

    min_cx = focus_cx - radius;
    max_cx = focus_cx + radius;
    min_cz = focus_cz - radius;
    max_cz = focus_cz + radius;

    for (dist = 0; dist <= radius; ++dist) {
        for (cz = min_cz; cz <= max_cz; ++cz) {
            for (cx = min_cx; cx <= max_cx; ++cx) {
                if (abs_i(cx - focus_cx) > dist || abs_i(cz - focus_cz) > dist) continue;
                if (abs_i(cx - focus_cx) != dist && abs_i(cz - focus_cz) != dist && dist != 0) continue;
                desired_add(cm, cx, cz, role);
            }
        }
    }
}

static void emit_full_superchunk(SdkChunkManager* cm, int scx, int scz, SdkChunkResidencyRole role)
{
    SdkSuperchunkCell cell;
    int local_x;
    int local_z;

    if (!cm) return;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    for (local_z = 0; local_z < SDK_SUPERCHUNK_CHUNK_SPAN; ++local_z) {
        for (local_x = 0; local_x < SDK_SUPERCHUNK_CHUNK_SPAN; ++local_x) {
            desired_add(cm, cell.interior_min_cx + local_x, cell.interior_min_cz + local_z, role);
        }
    }
}

static void warn_if_desired_set_near_capacity(const SdkChunkManager* cm)
{
    char dbg[160];

    if (!cm) return;
    if (cm->desired_count < (SDK_CHUNK_MANAGER_MAX_DESIRED * 9) / 10) return;

    sprintf_s(dbg, sizeof(dbg),
              "[RESIDENCY] desired set near capacity: desired=%d cap=%d\n",
              cm->desired_count, SDK_CHUNK_MANAGER_MAX_DESIRED);
    sdk_debug_log_output(dbg);
}

static void emit_visible_frontiers(SdkChunkManager* cm, int scx, int scz)
{
    SdkSuperchunkCell cell;
    int cx;
    int cz;

    if (!cm) return;

    sdk_superchunk_cell_from_index(scx, scz, &cell);

    for (cz = cell.origin_cz; cz <= cell.south_cz; ++cz) {
        for (cx = cell.origin_cx; cx <= cell.east_cx; ++cx) {
            if (!sdk_superchunk_full_neighborhood_ring_contains_chunk(scx, scz, cx, cz)) continue;
            desired_add_if_primary_visible(cm, cx, cz, SDK_CHUNK_ROLE_WALL_SUPPORT);
        }
    }
}

static void sync_slot_desired_roles(SdkChunkManager* cm)
{
    int i;
    int j;

    if (!cm) return;

    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; ++i) {
        SdkChunkResidentSlot* slot = &cm->slots[i];
        slot->desired = 0u;
        slot->desired_role = SDK_CHUNK_ROLE_NONE;
        if (!slot->occupied) {
            slot->role = SDK_CHUNK_ROLE_NONE;
        }
    }

    for (j = 0; j < cm->desired_count; ++j) {
        const SdkChunkResidencyTarget* target = &cm->desired[j];
        SdkChunkResidentSlot* slot = sdk_chunk_manager_find_slot(cm, target->cx, target->cz);
        if (!slot) continue;
        slot->desired = 1u;
        slot->desired_role = target->role;
        slot->role = target->role;
    }

    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; ++i) {
        SdkChunkResidentSlot* slot = &cm->slots[i];
        if (slot->occupied && !slot->desired) {
            slot->role = SDK_CHUNK_ROLE_EVICT_PENDING;
        }
    }
}

static void emit_previous_superchunk_full_neighborhood(SdkChunkManager* cm)
{
    emit_previous_superchunk_transition_bridge(cm);
}

static void emit_frontier_chunks(SdkChunkManager* cm, int scx, int scz)
{
    SdkSuperchunkCell cell;
    int i;

    if (!cm) return;
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    /* Load the outer frontier one chunk beyond the active wall boundaries. */
    for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
        desired_add(cm, cell.origin_cx - 1, cell.interior_min_cz + i, SDK_CHUNK_ROLE_FRONTIER);
    }
    for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
        desired_add(cm, cell.interior_min_cx + i, cell.origin_cz - 1, SDK_CHUNK_ROLE_FRONTIER);
    }
    for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
        desired_add(cm, cell.east_cx + 1, cell.interior_min_cz + i, SDK_CHUNK_ROLE_FRONTIER);
    }
    for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
        desired_add(cm, cell.interior_min_cx + i, cell.south_cz + 1, SDK_CHUNK_ROLE_FRONTIER);
    }
}

static void emit_diagonal_corners(SdkChunkManager* cm, int scx, int scz)
{
    SdkSuperchunkCell cell;
    int i, j;

    if (!cm) return;
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 2; ++j) {
            desired_add(cm, cell.origin_cx - 1 + i, cell.origin_cz - 1 + j, SDK_CHUNK_ROLE_TRANSITION_PRELOAD);
        }
    }
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 2; ++j) {
            desired_add(cm, cell.east_cx + i, cell.origin_cz - 1 + j, SDK_CHUNK_ROLE_TRANSITION_PRELOAD);
        }
    }
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 2; ++j) {
            desired_add(cm, cell.origin_cx - 1 + i, cell.south_cz + j, SDK_CHUNK_ROLE_TRANSITION_PRELOAD);
        }
    }
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 2; ++j) {
            desired_add(cm, cell.east_cx + i, cell.south_cz + j, SDK_CHUNK_ROLE_TRANSITION_PRELOAD);
        }
    }
}

static void emit_corner_adjacent_wall_chunks(SdkChunkManager* cm, int scx, int scz)
{
    SdkSuperchunkCell cell;
    int cx;
    int cz;

    if (!cm) return;
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    for (cz = cell.origin_cz - 1; cz <= cell.south_cz + 1; ++cz) {
        for (cx = cell.origin_cx - 1; cx <= cell.east_cx + 1; ++cx) {
            if (!sdk_superchunk_active_wall_support_contains_chunk(scx, scz, cx, cz)) continue;
            desired_add(cm, cx, cz, SDK_CHUNK_ROLE_WALL_SUPPORT);
        }
    }
}

static void emit_corner_wall_chunks(SdkChunkManager* cm, int scx, int scz)
{
    SdkSuperchunkCell cell;

    if (!cm) return;
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    if (SDK_VERBOSE_WALL_DEBUG_LOGS) {
        char buf[512];
        sprintf_s(buf, sizeof(buf), "CORNER_WALL_EMIT: sc=(%d,%d) origin=(%d,%d) NW=(%d,%d) NE=(%d,%d) SW=(%d,%d) SE=(%d,%d)",
                 scx, scz, cell.origin_cx, cell.origin_cz,
                 cell.origin_cx, cell.origin_cz,
                 cell.east_cx, cell.origin_cz,
                 cell.origin_cx, cell.south_cz,
                 cell.east_cx, cell.south_cz);
        sdk_debug_log_output(buf);
        sdk_debug_log_output("\n");
    }

    /* Load 4 corner wall chunks at the intersections of period boundaries */
    desired_add(cm, cell.origin_cx, cell.origin_cz, SDK_CHUNK_ROLE_WALL_SUPPORT);
    desired_add(cm, cell.east_cx, cell.origin_cz, SDK_CHUNK_ROLE_WALL_SUPPORT);
    desired_add(cm, cell.origin_cx, cell.south_cz, SDK_CHUNK_ROLE_WALL_SUPPORT);
    desired_add(cm, cell.east_cx, cell.south_cz, SDK_CHUNK_ROLE_WALL_SUPPORT);
}

static void emit_superchunk_wall_ring(SdkChunkManager* cm, int scx, int scz)
{
    const SdkSuperchunkConfig* config;
    SdkSuperchunkCell cell;
    int i;

    if (!cm) return;
    
    config = sdk_superchunk_get_config();
    if (!config || !config->walls_enabled) return;
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    if (SDK_VERBOSE_WALL_DEBUG_LOGS) {
        char buf[512];
        sprintf_s(buf, sizeof(buf), "WALL_RING_EMIT: sc=(%d,%d) origin=(%d,%d) period=%d W=[%d,%d] N=[%d,%d] E=[%d,%d] S=[%d,%d]",
                 scx, scz, cell.origin_cx, cell.origin_cz, SDK_SUPERCHUNK_WALL_PERIOD,
                 cell.origin_cx, cell.origin_cz,
                 cell.origin_cx, cell.origin_cz,
                 cell.east_cx, cell.origin_cz,
                 cell.origin_cx, cell.south_cz);
        sdk_debug_log_output(buf);
        sdk_debug_log_output("\n");
    }

    for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
        desired_add(cm, cell.origin_cx, cell.interior_min_cz + i, SDK_CHUNK_ROLE_WALL_SUPPORT);
    }
    for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
        desired_add(cm, cell.interior_min_cx + i, cell.origin_cz, SDK_CHUNK_ROLE_WALL_SUPPORT);
    }
    for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
        desired_add(cm, cell.east_cx, cell.interior_min_cz + i, SDK_CHUNK_ROLE_WALL_SUPPORT);
    }
    for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
        desired_add(cm, cell.interior_min_cx + i, cell.south_cz, SDK_CHUNK_ROLE_WALL_SUPPORT);
    }
}

/* ==========================================================================
 * ROW-BASED ASYNC SUPERCHUNK LOADING
 * When player crosses superchunk boundary, load/unload rows gradually
 * to maintain smooth frame rate.
 * ========================================================================== */

static void emit_superchunk_row(SdkChunkManager* cm, int scx, int scz, int axis, int row, SdkChunkResidencyRole role)
{
    SdkSuperchunkCell cell;
    int i;
    
    if (!cm) return;
    sdk_superchunk_cell_from_index(scx, scz, &cell);
    if (row < 0 || row >= SDK_SUPERCHUNK_CHUNK_SPAN) return;

    if (axis == 0) {  /* X axis rows - vary Z, iterate X */
        int cz = cell.interior_min_cz + row;
        for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
            desired_add(cm, cell.interior_min_cx + i, cz, role);
        }
    } else {  /* Z axis rows - vary X, iterate Z */
        int cx = cell.interior_min_cx + row;
        for (i = 0; i < SDK_SUPERCHUNK_CHUNK_SPAN; ++i) {
            desired_add(cm, cx, cell.interior_min_cz + i, role);
        }
    }
}

static void detect_superchunk_transition(SdkChunkManager* cm)
{
    if (!cm) return;

    /* Skip if this is the initial state (first world load) - use sync rebuild instead */
    if (cm->async_prev_scx == INT_MAX || cm->async_prev_scz == INT_MAX) {
        /* Just update prev to current without triggering async transition */
        cm->async_prev_scx = cm->primary_scx;
        cm->async_prev_scz = cm->primary_scz;
        return;
    }

    if (cm->async_load_active) {
        /* Let the active transition continue without resetting row progress. */
        if (cm->primary_scx == cm->async_new_scx && cm->primary_scz == cm->async_new_scz) {
            return;
        }

        /* Player crossed again before completion. Restart from the most recently
         * committed superchunk toward the latest target. */
        cm->async_load_active = 0;
    }
    
    if (cm->primary_scx != cm->async_prev_scx || cm->primary_scz != cm->async_prev_scz) {
        /* Superchunk changed - start async load */
        cm->async_new_scx = cm->primary_scx;
        cm->async_new_scz = cm->primary_scz;
        cm->async_load_axis = (cm->primary_scx != cm->async_prev_scx) ? 1 : 0; /* 0=Z, 1=X */
        cm->async_load_direction = (cm->async_load_axis == 1)
            ? (cm->primary_scx > cm->async_prev_scx ? 1 : -1)   /* East/West */
            : (cm->primary_scz > cm->async_prev_scz ? 1 : -1); /* North/South */
        cm->async_load_current_row = 0;
        cm->async_load_total_rows = SDK_SUPERCHUNK_CHUNK_SPAN;
        cm->async_load_active = 1;
    }
}

static void process_async_rows(SdkChunkManager* cm)
{
    int rows_this_frame = cm->async_budget_per_frame;
    int i;
    int row;
    
    /* Process this frame's batch of rows */
    for (i = 0; i < rows_this_frame && cm->async_load_current_row < cm->async_load_total_rows; i++) {
        cm->async_load_current_row++;
    }
    
    /* Emit all loaded rows from new superchunk (closest to player) */
    for (row = 0; row < cm->async_load_current_row; row++) {
        int row_to_load = (cm->async_load_direction > 0)
            ? row                           /* Forward: 0, 1, 2... */
            : (SDK_SUPERCHUNK_CHUNK_SPAN - 1 - row); /* Backward: 15, 14, 13... */
        
        emit_superchunk_row(cm, cm->async_new_scx, cm->async_new_scz,
                           cm->async_load_axis, row_to_load, SDK_CHUNK_ROLE_PRIMARY);
    }
    
    /* Emit remaining rows from old superchunk (furthest from player) */
    for (row = cm->async_load_current_row; row < SDK_SUPERCHUNK_CHUNK_SPAN; row++) {
        int row_to_keep = (cm->async_load_direction > 0)
            ? (SDK_SUPERCHUNK_CHUNK_SPAN - 1 - row) /* Forward: keep far rows first */
            : row;                                    /* Backward: keep near rows first */
        
        emit_superchunk_row(cm, cm->async_prev_scx, cm->async_prev_scz,
                           cm->async_load_axis, row_to_keep, SDK_CHUNK_ROLE_PRIMARY);
    }
    
    if (cm->async_load_current_row >= cm->async_load_total_rows) {
        /* Loading complete */
        cm->async_load_active = 0;
        cm->async_prev_scx = cm->async_new_scx;
        cm->async_prev_scz = cm->async_new_scz;
    }
}

static void rebuild_desired(SdkChunkManager* cm, int topology_changed)
{
    SdkWorldCoordinateSystem coordinate_system;
    int superchunk_mode;

    if (!cm) return;
    if (topology_changed) {
        cm->topology_generation++;
        if (cm->topology_generation == 0u) cm->topology_generation = 1u;
    }
    coordinate_system = sdk_world_get_coordinate_system();
    superchunk_mode = single_superchunk_mode_active(cm);

    desired_clear(cm);
    if (coordinate_system == SDK_WORLD_COORDSYS_CHUNK_SYSTEM) {
        emit_camera_window(cm,
                           cm->primary_anchor_cx,
                           cm->primary_anchor_cz,
                           cm->primary_load_radius,
                           SDK_CHUNK_ROLE_PRIMARY);
    } else if (superchunk_mode) {
        /* Check superchunk load mode - SYNC blocks, ASYNC will be non-blocking */
        if (g_graphics_settings.superchunk_load_mode == SDK_SUPERCHUNK_LOAD_ASYNC) {
            detect_superchunk_transition(cm);
            
            if (cm->async_load_active) {
                /* ASYNC: Load/unload rows gradually */
                process_async_rows(cm);
                
                /* Also emit frontier chunks and walls for new superchunk */
                emit_frontier_chunks(cm, cm->async_new_scx, cm->async_new_scz);
                emit_superchunk_wall_ring(cm, cm->async_new_scx, cm->async_new_scz);
                emit_diagonal_corners(cm, cm->async_new_scx, cm->async_new_scz);
                emit_corner_adjacent_wall_chunks(cm, cm->async_new_scx, cm->async_new_scz);
                emit_corner_wall_chunks(cm, cm->async_new_scx, cm->async_new_scz);
            } else {
                /* No active transition - normal sync rebuild */
                goto sync_rebuild;
            }
        } else {
            /* SYNC: Normal blocking load */
            goto sync_rebuild;
        }
    } else {
        emit_camera_window(cm, cm->primary_anchor_cx, cm->primary_anchor_cz,
                           cm->primary_load_radius, SDK_CHUNK_ROLE_PRIMARY);
        emit_visible_frontiers(cm, cm->primary_scx, cm->primary_scz);
    }
    goto sync_cleanup;

sync_rebuild:
    /* Loading order: C -> R -> X -> T -> Y -> E */
    if (cm->primary_load_radius < SDK_SUPERCHUNK_CHUNK_SPAN) {
        emit_superchunk_window(cm,
                               cm->primary_scx,
                               cm->primary_scz,
                               SDK_CHUNK_ROLE_PRIMARY,
                               cm->primary_anchor_cx,
                               cm->primary_anchor_cz,
                               cm->primary_load_radius);  /* C(window) */
    } else {
        emit_full_superchunk(cm, cm->primary_scx, cm->primary_scz, SDK_CHUNK_ROLE_PRIMARY);  /* C */
    }
    emit_frontier_chunks(cm, cm->primary_scx, cm->primary_scz);  /* R */
    emit_superchunk_wall_ring(cm, cm->primary_scx, cm->primary_scz);  /* X */
    emit_diagonal_corners(cm, cm->primary_scx, cm->primary_scz);  /* T */
    emit_corner_adjacent_wall_chunks(cm, cm->primary_scx, cm->primary_scz);  /* Y */
    emit_corner_wall_chunks(cm, cm->primary_scx, cm->primary_scz);  /* E */
    emit_previous_superchunk_full_neighborhood(cm);

sync_cleanup:
    sync_slot_desired_roles(cm);
    warn_if_desired_set_near_capacity(cm);
}

void sdk_chunk_manager_rebuild_lookup(SdkChunkManager* cm)
{
    int i;

    if (!cm) return;
    for (i = 0; i < SDK_CHUNK_MANAGER_HASH_CAPACITY; ++i) {
        cm->lookup[i] = -1;
    }

    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; ++i) {
        uint32_t start;
        int probe;
        const SdkChunkResidentSlot* slot = &cm->slots[i];

        if (!slot->occupied || !slot->chunk.blocks) continue;
        start = coord_hash(slot->chunk.cx, slot->chunk.cz) & (SDK_CHUNK_MANAGER_HASH_CAPACITY - 1u);
        for (probe = 0; probe < SDK_CHUNK_MANAGER_HASH_CAPACITY; ++probe) {
            int index = (int)((start + (uint32_t)probe) & (SDK_CHUNK_MANAGER_HASH_CAPACITY - 1u));
            if (cm->lookup[index] < 0) {
                cm->lookup[index] = i;
                break;
            }
        }
    }
}

void sdk_chunk_manager_init(SdkChunkManager* cm)
{
    int i;

    if (!cm) return;
    memset(cm, 0, sizeof(*cm));
    cm->cam_cx = 0;
    cm->cam_cz = 0;
    cm->grid_size = CHUNK_GRID_DEFAULT_SIZE;
    cm->next_slot = 0u;
    cm->topology_generation = 1u;
    cm->primary_valid = 0u;
    cm->transition_active = 0u;
    cm->background_expansion_enabled = 1u;
    cm->topology_dirty = 0u;
    cm->desired_count = 0;
    cm->construction_registry = sdk_construction_registry_create();
    for (i = 0; i < SDK_CHUNK_MANAGER_HASH_CAPACITY; ++i) {
        cm->lookup[i] = -1;
    }
    
    /* Initialize async loading state */
    cm->async_load_active = 0;
    cm->async_budget_per_frame = 2;  /* 2 rows per frame = ~8 frames for full transition */
    cm->async_prev_scx = INT_MAX;    /* Invalid initial value */
    cm->async_prev_scz = INT_MAX;    /* Invalid initial value */
}

void sdk_chunk_manager_shutdown(SdkChunkManager* cm)
{
    int i;
    if (!cm) return;
    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; ++i) {
        clear_slot(&cm->slots[i]);
    }
    sdk_construction_registry_free(cm->construction_registry);
    memset(cm, 0, sizeof(*cm));
}

void sdk_chunk_manager_set_grid_size(SdkChunkManager* cm, int grid_size)
{
    if (!cm) return;
    grid_size = sdk_chunk_manager_normalize_grid_size(grid_size);
    if (cm->grid_size == grid_size) return;
    cm->grid_size = grid_size;
    cm->topology_dirty = 1u;
}

void sdk_chunk_manager_set_background_expansion(SdkChunkManager* cm, bool enabled)
{
    if (!cm) return;
    cm->background_expansion_enabled = enabled ? 1u : 0u;
}

SdkChunkResidentSlot* sdk_chunk_manager_find_slot(SdkChunkManager* cm, int cx, int cz)
{
    uint32_t start;
    int probe;
    int i;

    if (!cm) return NULL;
    start = coord_hash(cx, cz) & (SDK_CHUNK_MANAGER_HASH_CAPACITY - 1u);
    for (probe = 0; probe < SDK_CHUNK_MANAGER_HASH_CAPACITY; ++probe) {
        int index = (int)((start + (uint32_t)probe) & (SDK_CHUNK_MANAGER_HASH_CAPACITY - 1u));
        int slot_index = cm->lookup[index];
        if (slot_index < 0) return NULL;
        if (slot_index < SDK_CHUNK_MANAGER_MAX_RESIDENT) {
            SdkChunkResidentSlot* slot = &cm->slots[slot_index];
            if (slot->occupied && slot->chunk.blocks &&
                slot->chunk.cx == cx && slot->chunk.cz == cz) {
                return slot;
            }
        }
    }

    /* Recover from stale lookup state caused by direct slot mutation paths. */
    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; ++i) {
        SdkChunkResidentSlot* slot = &cm->slots[i];
        if (!slot->occupied || !slot->chunk.blocks) continue;
        if (slot->chunk.cx == cx && slot->chunk.cz == cz) {
            sdk_chunk_manager_rebuild_lookup(cm);
            return slot;
        }
    }
    return NULL;
}

SdkChunk* sdk_chunk_manager_get_chunk(SdkChunkManager* cm, int cx, int cz)
{
    SdkChunkResidentSlot* slot = sdk_chunk_manager_find_slot(cm, cx, cz);
    if (!slot) return NULL;
    return slot->chunk.blocks ? &slot->chunk : NULL;
}

SdkChunkResidencyRole sdk_chunk_manager_get_chunk_role(const SdkChunkManager* cm, int cx, int cz)
{
    SdkChunkResidentSlot* slot = sdk_chunk_manager_find_slot((SdkChunkManager*)cm, cx, cz);
    if (!slot) return SDK_CHUNK_ROLE_NONE;
    return (SdkChunkResidencyRole)slot->role;
}

bool sdk_chunk_manager_is_desired(const SdkChunkManager* cm, int cx, int cz,
                                  SdkChunkResidencyRole* out_role,
                                  uint32_t* out_generation)
{
    int i;
    if (out_role) *out_role = SDK_CHUNK_ROLE_NONE;
    if (out_generation) *out_generation = cm ? cm->topology_generation : 0u;
    if (!cm) return false;
    for (i = 0; i < cm->desired_count; ++i) {
        const SdkChunkResidencyTarget* target = &cm->desired[i];
        if (target->cx == cx && target->cz == cz) {
            if (out_role) *out_role = (SdkChunkResidencyRole)target->role;
            if (out_generation) *out_generation = target->generation;
            return true;
        }
    }
    return false;
}

SdkChunkResidentSlot* sdk_chunk_manager_reserve_slot(SdkChunkManager* cm, int cx, int cz, SdkChunkResidencyRole role)
{
    uint32_t i;
    SdkChunkResidentSlot* existing;

    if (!cm) return NULL;
    existing = sdk_chunk_manager_find_slot(cm, cx, cz);
    if (existing) {
        existing->occupied = 1u;
        existing->desired = 1u;
        existing->desired_role = (uint8_t)role;
        existing->role = (uint8_t)role;
        return existing;
    }

    for (i = 0u; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; ++i) {
        uint32_t index = (cm->next_slot + i) % SDK_CHUNK_MANAGER_MAX_RESIDENT;
        SdkChunkResidentSlot* slot = &cm->slots[index];
        if (slot->occupied) continue;
        memset(slot, 0, sizeof(*slot));
        slot->occupied = 1u;
        slot->desired = 1u;
        slot->desired_role = (uint8_t)role;
        slot->role = (uint8_t)role;
        cm->next_slot = (index + 1u) % SDK_CHUNK_MANAGER_MAX_RESIDENT;
        cm->resident_count++;
        return slot;
    }

    return NULL;
}

SdkChunk* sdk_chunk_manager_adopt_built_chunk(SdkChunkManager* cm, SdkChunk* built_chunk, SdkChunkResidencyRole role)
{
    SdkChunkResidentSlot* slot;

    if (!cm || !built_chunk) return NULL;
    slot = sdk_chunk_manager_reserve_slot(cm, built_chunk->cx, built_chunk->cz, role);
    if (!slot) return NULL;

    if (slot->chunk.blocks) {
        clear_slot(slot);
        slot->occupied = 1u;
        slot->desired = 1u;
        slot->desired_role = (uint8_t)role;
        slot->role = (uint8_t)role;
    }

    slot->chunk = *built_chunk;
    slot->chunk.construction_registry = cm->construction_registry;
    memset(built_chunk, 0, sizeof(*built_chunk));
    sdk_chunk_manager_rebuild_lookup(cm);
    
    sdk_settlement_runtime_notify_chunk_loaded(slot->chunk.cx, slot->chunk.cz);
    
    return &slot->chunk;
}

void sdk_chunk_manager_release_slot(SdkChunkManager* cm, SdkChunkResidentSlot* slot)
{
    if (!cm || !slot || !slot->occupied) return;
    clear_slot(slot);
    if (cm->resident_count > 0) cm->resident_count--;
    sdk_chunk_manager_rebuild_lookup(cm);
}

bool sdk_chunk_manager_update(SdkChunkManager* cm, int new_cx, int new_cz)
{
    /** sdk_chunk_manager_update is the core chunk residency management function. 
    *   It determines which chunks should be loaded based on camera position.
    */  
    int new_scx;
    int new_scz;
    int superchunk_mode;
    const int recenter_threshold = 2;
    int changed = 0;
    int topology_changed = 0;

    if (!cm) return false;

    /* Set camera position in global world space chunk coordinates */
    cm->cam_cx = new_cx;
    cm->cam_cz = new_cz;    

    if (sdk_world_has_superchunks()) {

        /* Get superchunk coordinates */
        new_scx = floor_div_i(new_cx, SDK_SUPERCHUNK_WALL_PERIOD);
        new_scz = floor_div_i(new_cz, SDK_SUPERCHUNK_WALL_PERIOD);

        /* Check if single superchunk mode is active */
        superchunk_mode = single_superchunk_mode_active(cm);
    
        /* Debug: log superchunk coordinate changes */
        if (new_scx != cm->primary_scx || new_scz != cm->primary_scz) {
            char buf[256];
            sprintf_s(buf, sizeof(buf), "SUPERCHUNK_CHANGE: old=(%d,%d) new=(%d,%d) chunk=(%d,%d) period=%d",
                     cm->primary_scx, cm->primary_scz, new_scx, new_scz, new_cx, new_cz, SDK_SUPERCHUNK_WALL_PERIOD);
            /* Note: Can't use worldgen debug here, would need chunk manager-specific logging */
        }
    
        if (!cm->primary_valid) {
            cm->primary_scx = new_scx;
            cm->primary_scz = new_scz;
            cm->desired_scx = new_scx;
            cm->desired_scz = new_scz;
            cm->primary_valid = 1u;
            cm->transition_active = 0u;
            cm->topology_dirty = 0u;
            reset_primary_load_window(cm, new_cx, new_cz);
            rebuild_desired(cm, 0);
            return true;
        }
    
        if (cm->topology_dirty) {
            cm->topology_dirty = 0u;
            topology_changed = 1;
            if (!superchunk_mode || !chunk_in_superchunk(new_cx, new_cz, cm->primary_scx, cm->primary_scz)) {
                cm->primary_scx = new_scx;
                cm->primary_scz = new_scz;
            }
            cm->desired_scx = cm->primary_scx;
            cm->desired_scz = cm->primary_scz;
            cm->transition_active = 0u;
            reset_primary_load_window(cm, new_cx, new_cz);
            changed = 1;
        }
    
        if (!superchunk_mode) {
            cm->primary_scx = new_scx;
            cm->primary_scz = new_scz;
            cm->desired_scx = new_scx;
            cm->desired_scz = new_scz;
            if (cm->transition_active) {
                cm->transition_active = 0u;
                changed = 1;
            }
            if (abs_i(new_cx - cm->primary_anchor_cx) > recenter_threshold ||
                abs_i(new_cz - cm->primary_anchor_cz) > recenter_threshold) {
                reset_primary_load_window(cm, new_cx, new_cz);
                changed = 1;
            }
            if (changed) {
                rebuild_desired(cm, topology_changed);
            }
            return changed != 0;
        }
    
        if (cm->transition_active) {
            if (chunk_in_superchunk(new_cx, new_cz, cm->prev_scx, cm->prev_scz)) {
                cm->primary_scx = cm->prev_scx;
                cm->primary_scz = cm->prev_scz;
                cm->desired_scx = cm->primary_scx;
                cm->desired_scz = cm->primary_scz;
                cm->prev_scx = cm->primary_scx;
                cm->prev_scz = cm->primary_scz;
                cm->transition_active = 0u;
                cm->transition_entry_cx = new_cx;
                cm->transition_entry_cz = new_cz;
                reset_primary_load_window(cm, new_cx, new_cz);
                changed = 1;
            } else if (player_cleared_transition_bridge(cm, new_cx, new_cz) &&
                       superchunk_primary_window_ready(cm,
                                                       cm->primary_scx,
                                                       cm->primary_scz,
                                                       cm->primary_anchor_cx,
                                                       cm->primary_anchor_cz,
                                                       cm->primary_load_radius)) {
                cm->transition_active = 0u;
                cm->prev_scx = cm->primary_scx;
                cm->prev_scz = cm->primary_scz;
                changed = 1;
            }
        }
    
        if (!chunk_in_superchunk(new_cx, new_cz, cm->primary_scx, cm->primary_scz)) {
            int old_scx = cm->primary_scx;
            int old_scz = cm->primary_scz;
            int adjacent_transition = (abs_i(new_scx - old_scx) + abs_i(new_scz - old_scz) == 1);
    
            cm->prev_scx = old_scx;
            cm->prev_scz = old_scz;
            cm->primary_scx = new_scx;
            cm->primary_scz = new_scz;
            cm->desired_scx = new_scx;
            cm->desired_scz = new_scz;
            cm->transition_entry_cx = new_cx;
            cm->transition_entry_cz = new_cz;
            cm->transition_active = adjacent_transition ? 1u : 0u;
            if (!cm->transition_active) {
                cm->prev_scx = new_scx;
                cm->prev_scz = new_scz;
            }
            reset_primary_load_window(cm, new_cx, new_cz);
            if (cm->transition_active) {
                cm->primary_load_radius = transition_primary_window_radius(cm);
                cm->primary_expanded =
                    (cm->primary_load_radius >= SDK_SUPERCHUNK_CHUNK_SPAN) ? 1u : 0u;
            }
            changed = 1;
            topology_changed = 1;
        } else {
            cm->desired_scx = cm->primary_scx;
            cm->desired_scz = cm->primary_scz;
            if (cm->primary_load_radius < SDK_SUPERCHUNK_CHUNK_SPAN &&
                (abs_i(new_cx - cm->primary_anchor_cx) > recenter_threshold ||
                 abs_i(new_cz - cm->primary_anchor_cz) > recenter_threshold)) {
                cm->primary_anchor_cx = new_cx;
                cm->primary_anchor_cz = new_cz;
                changed = 1;
            }
            if (!cm->transition_active &&
                cm->background_expansion_enabled &&
                cm->primary_load_radius < SDK_SUPERCHUNK_CHUNK_SPAN &&
                superchunk_primary_window_ready(cm,
                                                cm->primary_scx,
                                                cm->primary_scz,
                                                cm->primary_anchor_cx,
                                                cm->primary_anchor_cz,
                                                cm->primary_load_radius)) {
                cm->primary_load_radius += k_background_expand_step_chunks;
                if (cm->primary_load_radius >= SDK_SUPERCHUNK_CHUNK_SPAN) {
                    cm->primary_load_radius = SDK_SUPERCHUNK_CHUNK_SPAN;
                    cm->primary_expanded = 1u;
                }
                changed = 1;
            }
        }
    
        if (changed) {
            rebuild_desired(cm, topology_changed);
        }

        return changed != 0;

    } else {
        /* Non-superchunk world: simple camera-centered window */
        if (!cm->primary_valid) {
            cm->primary_valid = 1u;
            cm->topology_dirty = 0u;
            reset_primary_load_window(cm, new_cx, new_cz);
            rebuild_desired(cm, 0);
            return true;
        }

        if (cm->topology_dirty) {
            cm->topology_dirty = 0u;
            reset_primary_load_window(cm, new_cx, new_cz);
            rebuild_desired(cm, 1);
            return true;
        }

        /* Recenter window when camera moves beyond threshold */
        if (abs_i(new_cx - cm->primary_anchor_cx) > recenter_threshold ||
            abs_i(new_cz - cm->primary_anchor_cz) > recenter_threshold) {
            reset_primary_load_window(cm, new_cx, new_cz);
            rebuild_desired(cm, 0);
            return true;
        }

        return false;
    }

}

void sdk_chunk_manager_foreach(SdkChunkManager* cm, SdkChunkCallback cb, void* user)
{
    int i;
    if (!cm || !cb) return;
    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; ++i) {
        SdkChunkResidentSlot* slot = &cm->slots[i];
        if (!slot->occupied || !slot->chunk.blocks) continue;
        cb(&slot->chunk, i, slot->role, user);
    }
}

uint64_t sdk_chunk_manager_memory_usage(const SdkChunkManager* cm)
{
    uint64_t total = 0u;
    int i;

    if (!cm) return 0u;
    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; ++i) {
        const SdkChunkResidentSlot* slot = &cm->slots[i];
        if (slot->occupied && slot->chunk.blocks) {
            total += (uint64_t)CHUNK_TOTAL_BLOCKS * sizeof(SdkWorldCellCode);
            if (slot->chunk.construction_cells) {
                total += sizeof(*slot->chunk.construction_cells);
                total += (uint64_t)slot->chunk.construction_cells->capacity *
                         sizeof(slot->chunk.construction_cells->entries[0]);
            }
        }
    }
    return total;
}
