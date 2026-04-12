/**
 * sdk_superchunk_geometry.h -- Shared superchunk geometry constants.
 *
 * This header is the single source of truth for superchunk span, wall
 * thickness, and gate geometry.
 *
 * Runtime ownership rule:
 * - west and north wall geometry lives inside the superchunk that owns it
 * - east and south visible perimeter support comes from the adjacent outer ring
 *
 * NOTE: For runtime-configurable superchunk sizes, use the accessor functions
 * in sdk_superchunk_config.h. The macros below are kept for backward compatibility
 * and will be deprecated once all code is migrated to runtime accessors.
 */
#ifndef NQLSDK_SUPERCHUNK_GEOMETRY_H
#define NQLSDK_SUPERCHUNK_GEOMETRY_H

#include <stdint.h>
#include "../../Chunks/sdk_chunk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Legacy hardcoded constants - use sdk_superchunk_config.h for runtime values */
#define SDK_SUPERCHUNK_CHUNK_SPAN_LEGACY 16
#define SDK_SUPERCHUNK_BLOCK_SPAN_LEGACY (SDK_SUPERCHUNK_CHUNK_SPAN_LEGACY * CHUNK_WIDTH)
#define SDK_SUPERCHUNK_WALL_PERIOD_LEGACY (SDK_SUPERCHUNK_CHUNK_SPAN_LEGACY + 1)

/* Enable runtime configuration */
#define SUPERCHUNK_USE_RUNTIME_CONFIG 1

/* For backward compatibility, redirect to runtime accessors when config is available */
#ifdef SUPERCHUNK_USE_RUNTIME_CONFIG
#include "../Config/sdk_superchunk_config.h"
#include "../../Walls/Config/sdk_world_walls_config.h"
#define SDK_SUPERCHUNK_CHUNK_SPAN sdk_superchunk_get_chunk_span()
#define SDK_SUPERCHUNK_BLOCK_SPAN sdk_superchunk_get_block_span()
#define SDK_SUPERCHUNK_WALL_PERIOD sdk_superchunk_get_wall_period()
#else
/* Default to legacy values for now */
#define SDK_SUPERCHUNK_CHUNK_SPAN SDK_SUPERCHUNK_CHUNK_SPAN_LEGACY
#define SDK_SUPERCHUNK_BLOCK_SPAN SDK_SUPERCHUNK_BLOCK_SPAN_LEGACY
#define SDK_SUPERCHUNK_WALL_PERIOD SDK_SUPERCHUNK_WALL_PERIOD_LEGACY
#endif

/* Wall thickness - can be made configurable later */
#ifdef SUPERCHUNK_USE_RUNTIME_CONFIG
#define SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS sdk_superchunk_get_wall_thickness_blocks()
#define SDK_SUPERCHUNK_WALL_THICKNESS_CHUNKS sdk_superchunk_get_wall_thickness_chunks()
#else
#define SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS 64
#if (SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS % CHUNK_WIDTH) != 0
#error SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS must be divisible by CHUNK_WIDTH
#endif
#define SDK_SUPERCHUNK_WALL_THICKNESS_CHUNKS (SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS / CHUNK_WIDTH)
#endif

/* Gate geometry - use runtime accessors when available */
#ifdef SUPERCHUNK_USE_RUNTIME_CONFIG
#define SDK_SUPERCHUNK_GATE_WIDTH_BLOCKS sdk_superchunk_get_gate_width_blocks()
#define SDK_SUPERCHUNK_GATE_START_BLOCK sdk_superchunk_get_gate_start_block()
#define SDK_SUPERCHUNK_GATE_END_BLOCK sdk_superchunk_get_gate_end_block()
#define SDK_SUPERCHUNK_GATE_SUPPORT_WIDTH_CHUNKS sdk_superchunk_get_gate_support_width_chunks()
#define SDK_SUPERCHUNK_GATE_SUPPORT_START_CHUNK sdk_superchunk_get_gate_support_start_chunk()
#define SDK_SUPERCHUNK_GATE_SUPPORT_END_CHUNK sdk_superchunk_get_gate_support_end_chunk()
#else
#define SDK_SUPERCHUNK_GATE_WIDTH_BLOCKS 64
#define SDK_SUPERCHUNK_GATE_START_BLOCK 480
#define SDK_SUPERCHUNK_GATE_END_BLOCK (SDK_SUPERCHUNK_GATE_START_BLOCK + SDK_SUPERCHUNK_GATE_WIDTH_BLOCKS - 1)
#define SDK_SUPERCHUNK_GATE_SUPPORT_WIDTH_CHUNKS 4
#define SDK_SUPERCHUNK_GATE_SUPPORT_START_CHUNK \
    ((SDK_SUPERCHUNK_CHUNK_SPAN - SDK_SUPERCHUNK_GATE_SUPPORT_WIDTH_CHUNKS) / 2)
#define SDK_SUPERCHUNK_GATE_SUPPORT_END_CHUNK \
    (SDK_SUPERCHUNK_GATE_SUPPORT_START_CHUNK + SDK_SUPERCHUNK_GATE_SUPPORT_WIDTH_CHUNKS - 1)
#endif

/* Legacy aliases kept so worldgen modules keep a single geometry source of truth. */
#define SDK_SUPERCHUNK_BLOCKS SDK_SUPERCHUNK_BLOCK_SPAN
#define SDK_SUPERCHUNK_WALL_THICKNESS SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS
#define SDK_SUPERCHUNK_GATE_WIDTH SDK_SUPERCHUNK_GATE_WIDTH_BLOCKS
#define SDK_SUPERCHUNK_GATE_START SDK_SUPERCHUNK_GATE_START_BLOCK
#define SDK_SUPERCHUNK_GATE_END SDK_SUPERCHUNK_GATE_END_BLOCK

typedef enum SdkSuperchunkWallFaceMask {
    SDK_SUPERCHUNK_WALL_FACE_NONE  = 0,
    SDK_SUPERCHUNK_WALL_FACE_WEST  = 1u << 0,
    SDK_SUPERCHUNK_WALL_FACE_NORTH = 1u << 1,
    SDK_SUPERCHUNK_WALL_FACE_EAST  = 1u << 2,
    SDK_SUPERCHUNK_WALL_FACE_SOUTH = 1u << 3
} SdkSuperchunkWallFaceMask;

typedef struct SdkSuperchunkCell {
    int scx;
    int scz;
    int chunk_span;
    int origin_cx;
    int origin_cz;
    int east_cx;
    int south_cz;
    int interior_min_cx;
    int interior_max_cx;
    int interior_min_cz;
    int interior_max_cz;
} SdkSuperchunkCell;

typedef struct SdkSuperchunkWallGridCell {
    int grid_x;
    int grid_z;
    int period;
    int origin_cx;
    int origin_cz;
    int east_cx;
    int south_cz;
    int offset_x;
    int offset_z;
} SdkSuperchunkWallGridCell;

static inline int sdk_superchunk_wall_period_blocks(void)
{
    return SDK_SUPERCHUNK_WALL_PERIOD * CHUNK_WIDTH;
}

static inline int sdk_superchunk_floor_div_i(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return (value - denom + 1) / denom;
}

static inline int sdk_superchunk_floor_mod_i(int value, int denom)
{
    int mod = value % denom;
    if (mod < 0) mod += denom;
    return mod;
}

static inline int sdk_superchunk_get_superchunk_from_global(int value)
{
    return sdk_superchunk_floor_div_i(value, SDK_SUPERCHUNK_WALL_PERIOD);
}

static inline void sdk_superchunk_cell_from_index(int scx, int scz, SdkSuperchunkCell* out_cell)
{
    const int period = SDK_SUPERCHUNK_WALL_PERIOD;

    if (!out_cell) return;

    out_cell->scx = scx;
    out_cell->scz = scz;
    out_cell->chunk_span = SDK_SUPERCHUNK_CHUNK_SPAN;
    out_cell->origin_cx = scx * period;
    out_cell->origin_cz = scz * period;
    out_cell->east_cx = out_cell->origin_cx + period;
    out_cell->south_cz = out_cell->origin_cz + period;
    out_cell->interior_min_cx = out_cell->origin_cx + 1;
    out_cell->interior_max_cx = out_cell->origin_cx + out_cell->chunk_span;
    out_cell->interior_min_cz = out_cell->origin_cz + 1;
    out_cell->interior_max_cz = out_cell->origin_cz + out_cell->chunk_span;
}

static inline void sdk_superchunk_cell_from_chunk(int cx, int cz, SdkSuperchunkCell* out_cell)
{
    const int scx = sdk_superchunk_floor_div_i(cx, SDK_SUPERCHUNK_WALL_PERIOD);
    const int scz = sdk_superchunk_floor_div_i(cz, SDK_SUPERCHUNK_WALL_PERIOD);
    sdk_superchunk_cell_from_index(scx, scz, out_cell);
}

static inline int sdk_superchunk_chunk_local_period_x(int cx)
{
    return sdk_superchunk_floor_mod_i(cx, SDK_SUPERCHUNK_WALL_PERIOD);
}

static inline int sdk_superchunk_chunk_local_period_z(int cz)
{
    return sdk_superchunk_floor_mod_i(cz, SDK_SUPERCHUNK_WALL_PERIOD);
}

static inline int sdk_superchunk_wall_grid_effective_offset_x(void)
{
    int offset_x = 0;
    sdk_world_walls_get_effective_offset(&offset_x, NULL);
    return offset_x;
}

static inline int sdk_superchunk_wall_grid_effective_offset_z(void)
{
    int offset_z = 0;
    sdk_world_walls_get_effective_offset(NULL, &offset_z);
    return offset_z;
}

static inline int sdk_superchunk_wall_grid_chunk_local_period_x(int cx)
{
    return sdk_world_walls_chunk_local_x(cx);
}

static inline int sdk_superchunk_wall_grid_chunk_local_period_z(int cz)
{
    return sdk_world_walls_chunk_local_z(cz);
}

static inline void sdk_superchunk_wall_cell_from_index(int grid_x,
                                                       int grid_z,
                                                       SdkSuperchunkWallGridCell* out_cell)
{
    int period;
    int offset_x;
    int offset_z;

    if (!out_cell) return;
    period = sdk_world_walls_get_period();
    sdk_world_walls_get_effective_offset(&offset_x, &offset_z);

    out_cell->grid_x = grid_x;
    out_cell->grid_z = grid_z;
    out_cell->period = period;
    out_cell->offset_x = offset_x;
    out_cell->offset_z = offset_z;
    out_cell->origin_cx = grid_x * period + offset_x;
    out_cell->origin_cz = grid_z * period + offset_z;
    out_cell->east_cx = out_cell->origin_cx + period;
    out_cell->south_cz = out_cell->origin_cz + period;
}

static inline void sdk_superchunk_wall_cell_from_chunk(int cx,
                                                       int cz,
                                                       SdkSuperchunkWallGridCell* out_cell)
{
    const int period = sdk_world_walls_get_period();
    const int offset_x = sdk_superchunk_wall_grid_effective_offset_x();
    const int offset_z = sdk_superchunk_wall_grid_effective_offset_z();
    const int grid_x = sdk_superchunk_floor_div_i(cx - offset_x, period);
    const int grid_z = sdk_superchunk_floor_div_i(cz - offset_z, period);

    sdk_superchunk_wall_cell_from_index(grid_x, grid_z, out_cell);
}

static inline int sdk_superchunk_chunk_local_interior_x(int cx)
{
    SdkSuperchunkCell cell;
    sdk_superchunk_cell_from_chunk(cx, 0, &cell);
    return cx - cell.interior_min_cx;
}

static inline int sdk_superchunk_chunk_local_interior_z(int cz)
{
    SdkSuperchunkCell cell;
    sdk_superchunk_cell_from_chunk(0, cz, &cell);
    return cz - cell.interior_min_cz;
}

static inline void sdk_superchunk_chunk_local_interior_coords(int cx,
                                                              int cz,
                                                              int* out_local_x,
                                                              int* out_local_z)
{
    SdkSuperchunkCell cell;

    sdk_superchunk_cell_from_chunk(cx, cz, &cell);
    if (out_local_x) *out_local_x = cx - cell.interior_min_cx;
    if (out_local_z) *out_local_z = cz - cell.interior_min_cz;
}

static inline int sdk_superchunk_gate_support_contains_chunk_run(int run_chunk_local)
{
    return run_chunk_local >= SDK_SUPERCHUNK_GATE_SUPPORT_START_CHUNK &&
           run_chunk_local <= SDK_SUPERCHUNK_GATE_SUPPORT_END_CHUNK;
}

static inline int sdk_superchunk_gate_contains_block_run(int run_block_local)
{
    return run_block_local >= SDK_SUPERCHUNK_GATE_START_BLOCK &&
           run_block_local <= SDK_SUPERCHUNK_GATE_END_BLOCK;
}

static inline int sdk_superchunk_gate_intersects_chunk_run(int run_chunk_local)
{
    const int chunk_block_min = run_chunk_local * CHUNK_WIDTH;
    const int chunk_block_max = chunk_block_min + CHUNK_WIDTH - 1;

    return chunk_block_max >= SDK_SUPERCHUNK_GATE_START_BLOCK &&
           chunk_block_min <= SDK_SUPERCHUNK_GATE_END_BLOCK;
}

static inline void sdk_superchunk_cell_origin_blocks(const SdkSuperchunkCell* cell,
                                                     int* out_origin_x,
                                                     int* out_origin_z)
{
    if (!cell) return;
    if (out_origin_x) *out_origin_x = cell->origin_cx * CHUNK_WIDTH;
    if (out_origin_z) *out_origin_z = cell->origin_cz * CHUNK_DEPTH;
}

static inline void sdk_superchunk_cell_edge_blocks(const SdkSuperchunkCell* cell,
                                                   int* out_east_x,
                                                   int* out_south_z)
{
    if (!cell) return;
    if (out_east_x) *out_east_x = cell->east_cx * CHUNK_WIDTH;
    if (out_south_z) *out_south_z = cell->south_cz * CHUNK_DEPTH;
}

static inline void sdk_superchunk_world_block_local_to_cell(int scx,
                                                            int scz,
                                                            int wx,
                                                            int wz,
                                                            int* out_local_x,
                                                            int* out_local_z)
{
    SdkSuperchunkCell cell;
    int origin_x = 0;
    int origin_z = 0;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    sdk_superchunk_cell_origin_blocks(&cell, &origin_x, &origin_z);
    if (out_local_x) *out_local_x = wx - origin_x;
    if (out_local_z) *out_local_z = wz - origin_z;
}

static inline int sdk_superchunk_wall_face_run_local(SdkSuperchunkWallFaceMask face,
                                                     int local_super_x,
                                                     int local_super_z)
{
    switch (face) {
        case SDK_SUPERCHUNK_WALL_FACE_WEST:
        case SDK_SUPERCHUNK_WALL_FACE_EAST:
            return local_super_z;
        case SDK_SUPERCHUNK_WALL_FACE_NORTH:
        case SDK_SUPERCHUNK_WALL_FACE_SOUTH:
            return local_super_x;
        case SDK_SUPERCHUNK_WALL_FACE_NONE:
        default:
            return local_super_x;
    }
}

static inline int sdk_superchunk_chunk_is_wall_anywhere(int cx, int cz)
{
    return sdk_world_walls_get_canonical_wall_chunk_owner(cx, cz, NULL, NULL, NULL, NULL, NULL) != 0;
}

static inline int sdk_superchunk_get_canonical_wall_chunk_owner(int cx,
                                                                int cz,
                                                                uint8_t* out_wall_mask,
                                                                int* out_origin_cx,
                                                                int* out_origin_cz,
                                                                int* out_period_local_x,
                                                                int* out_period_local_z)
{
    return sdk_world_walls_get_canonical_wall_chunk_owner(cx,
                                                          cz,
                                                          out_wall_mask,
                                                          out_origin_cx,
                                                          out_origin_cz,
                                                          out_period_local_x,
                                                          out_period_local_z);
}

static inline int sdk_superchunk_chunk_in_interior(int scx, int scz, int cx, int cz)
{
    SdkSuperchunkCell cell;
    sdk_superchunk_cell_from_index(scx, scz, &cell);
    return cx >= cell.interior_min_cx &&
           cx <= cell.interior_max_cx &&
           cz >= cell.interior_min_cz &&
           cz <= cell.interior_max_cz;
}

static inline int sdk_superchunk_chunk_in_full_neighborhood(int scx, int scz, int cx, int cz)
{
    SdkSuperchunkCell cell;
    sdk_superchunk_cell_from_index(scx, scz, &cell);
    return cx >= cell.origin_cx &&
           cx <= cell.east_cx &&
           cz >= cell.origin_cz &&
           cz <= cell.south_cz;
}

static inline int sdk_superchunk_chunk_is_active_wall(int scx, int scz, int cx, int cz)
{
    SdkSuperchunkCell cell;
    int on_vertical;
    int on_horizontal;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    on_vertical = (cx == cell.origin_cx || cx == cell.east_cx) &&
                  cz >= cell.origin_cz && cz <= cell.south_cz;
    on_horizontal = (cz == cell.origin_cz || cz == cell.south_cz) &&
                    cx >= cell.origin_cx && cx <= cell.east_cx;
    return on_vertical || on_horizontal;
}

static inline int sdk_superchunk_chunk_is_active_wall_corner(int scx, int scz, int cx, int cz)
{
    SdkSuperchunkCell cell;
    sdk_superchunk_cell_from_index(scx, scz, &cell);
    return (cx == cell.origin_cx || cx == cell.east_cx) &&
           (cz == cell.origin_cz || cz == cell.south_cz);
}

static inline int sdk_superchunk_chunk_is_active_wall_edge(int scx, int scz, int cx, int cz)
{
    return sdk_superchunk_chunk_is_active_wall(scx, scz, cx, cz) &&
           !sdk_superchunk_chunk_is_active_wall_corner(scx, scz, cx, cz);
}

static inline int sdk_superchunk_chunk_is_active_wall_support(int scx, int scz, int cx, int cz)
{
    SdkSuperchunkCell cell;
    int adjacent_vertical;
    int adjacent_horizontal;

    if (sdk_superchunk_chunk_is_active_wall(scx, scz, cx, cz)) return 0;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    adjacent_vertical = (cx == cell.origin_cx - 1 || cx == cell.origin_cx + 1 ||
                         cx == cell.east_cx - 1 || cx == cell.east_cx + 1) &&
                        cz >= cell.origin_cz && cz <= cell.south_cz;
    adjacent_horizontal = (cz == cell.origin_cz - 1 || cz == cell.origin_cz + 1 ||
                           cz == cell.south_cz - 1 || cz == cell.south_cz + 1) &&
                          cx >= cell.origin_cx && cx <= cell.east_cx;
    return adjacent_vertical || adjacent_horizontal;
}

static inline void sdk_superchunk_wall_edge_chunk_for_run(int scx,
                                                          int scz,
                                                          SdkSuperchunkWallFaceMask face,
                                                          int run_local,
                                                          int* out_cx,
                                                          int* out_cz)
{
    SdkSuperchunkCell cell;
    int clamped_run = run_local;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    if (clamped_run < 0) clamped_run = 0;
    if (clamped_run >= cell.chunk_span) clamped_run = cell.chunk_span - 1;

    switch (face) {
        case SDK_SUPERCHUNK_WALL_FACE_WEST:
            if (out_cx) *out_cx = cell.origin_cx;
            if (out_cz) *out_cz = cell.interior_min_cz + clamped_run;
            break;
        case SDK_SUPERCHUNK_WALL_FACE_NORTH:
            if (out_cx) *out_cx = cell.interior_min_cx + clamped_run;
            if (out_cz) *out_cz = cell.origin_cz;
            break;
        case SDK_SUPERCHUNK_WALL_FACE_EAST:
            if (out_cx) *out_cx = cell.east_cx;
            if (out_cz) *out_cz = cell.interior_min_cz + clamped_run;
            break;
        case SDK_SUPERCHUNK_WALL_FACE_SOUTH:
            if (out_cx) *out_cx = cell.interior_min_cx + clamped_run;
            if (out_cz) *out_cz = cell.south_cz;
            break;
        case SDK_SUPERCHUNK_WALL_FACE_NONE:
        default:
            if (out_cx) *out_cx = cell.origin_cx;
            if (out_cz) *out_cz = cell.origin_cz;
            break;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SUPERCHUNK_GEOMETRY_H */
