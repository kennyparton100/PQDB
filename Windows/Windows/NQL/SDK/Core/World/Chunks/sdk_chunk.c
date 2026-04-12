/**
 * sdk_chunk.c — Chunk implementation
 */
#include "sdk_chunk.h"
#include "../ConstructionCells/sdk_construction_cells.h"
#include "../Simulation/sdk_simulation.h"
#include "../../../Renderer/d3d12_renderer.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void chunk_zero_mesh(SdkChunkSubmesh* sub)
{
    if (!sub) return;
    if (sub->cpu_vertices) {
        free(sub->cpu_vertices);
        sub->cpu_vertices = NULL;
    }
    sub->vertex_capacity = 0;
    sub->vertex_count = 0;
    sub->vertex_buffer = NULL;
    sub->vb_gpu = NULL;
    memset(sub->bounds_min, 0, sizeof(sub->bounds_min));
    memset(sub->bounds_max, 0, sizeof(sub->bounds_max));
    sub->dirty = false;
    sub->upload_dirty = false;
    sub->empty = true;
}

static void chunk_accumulate_submesh_bounds(const SdkChunkSubmesh* sub,
                                            bool* io_any_active,
                                            float* io_min_x,
                                            float* io_min_y,
                                            float* io_min_z,
                                            float* io_max_x,
                                            float* io_max_y,
                                            float* io_max_z)
{
    if (!sub || sub->vertex_count == 0) return;
    if (!io_any_active || !io_min_x || !io_min_y || !io_min_z ||
        !io_max_x || !io_max_y || !io_max_z) {
        return;
    }

    if (!*io_any_active) {
        *io_min_x = sub->bounds_min[0];
        *io_min_y = sub->bounds_min[1];
        *io_min_z = sub->bounds_min[2];
        *io_max_x = sub->bounds_max[0];
        *io_max_y = sub->bounds_max[1];
        *io_max_z = sub->bounds_max[2];
        *io_any_active = true;
        return;
    }

    if (sub->bounds_min[0] < *io_min_x) *io_min_x = sub->bounds_min[0];
    if (sub->bounds_min[1] < *io_min_y) *io_min_y = sub->bounds_min[1];
    if (sub->bounds_min[2] < *io_min_z) *io_min_z = sub->bounds_min[2];
    if (sub->bounds_max[0] > *io_max_x) *io_max_x = sub->bounds_max[0];
    if (sub->bounds_max[1] > *io_max_y) *io_max_y = sub->bounds_max[1];
    if (sub->bounds_max[2] > *io_max_z) *io_max_z = sub->bounds_max[2];
}

static void chunk_advance_mesh_job_generation(SdkChunk* chunk)
{
    if (!chunk) return;
    chunk->mesh_job_generation++;
    if (chunk->mesh_job_generation == 0u) {
        chunk->mesh_job_generation = 1u;
    }
}

static void chunk_mark_subchunk_dirty_internal(SdkChunk* chunk, int subchunk_index, bool include_far_meshes)
{
    if (!chunk) return;
    if ((unsigned)subchunk_index >= CHUNK_SUBCHUNK_COUNT) return;

    chunk_advance_mesh_job_generation(chunk);
    chunk->dirty_frame_age = 0u;
    chunk->subchunks[subchunk_index].dirty = true;
    chunk->water_subchunks[subchunk_index].dirty = true;
    chunk->dirty_subchunks_mask |= (1u << subchunk_index);
    chunk->geometry_dirty = 1u;
    chunk->dirty = true;
    chunk->sim_dirty_mask |= (1u << subchunk_index);
    if (include_far_meshes) {
        chunk->far_mesh.dirty = true;
        chunk->experimental_far_mesh.dirty = true;
        chunk->far_exact_overlay_mesh.dirty = true;
        chunk->far_mesh_dirty = 1u;
    }
}

static void chunk_reset_transferred_mesh(SdkChunkSubmesh* sub)
{
    if (!sub) return;
    sub->cpu_vertices = NULL;
    sub->vertex_count = 0;
    sub->vertex_capacity = 0;
    sub->dirty = false;
    sub->upload_dirty = false;
    sub->empty = true;
    memset(sub->bounds_min, 0, sizeof(sub->bounds_min));
    memset(sub->bounds_max, 0, sizeof(sub->bounds_max));
}

static void chunk_take_single_mesh(SdkChunkSubmesh* dst, SdkChunkSubmesh* src)
{
    if (!dst || !src) return;

    if (dst->cpu_vertices) {
        free(dst->cpu_vertices);
        dst->cpu_vertices = NULL;
    }

    dst->cpu_vertices = src->cpu_vertices;
    dst->vertex_count = src->vertex_count;
    dst->vertex_capacity = src->vertex_capacity;
    dst->empty = src->empty;
    dst->dirty = false; /* Mesh is now up to date after taking */
    dst->upload_dirty = src->upload_dirty;
    memcpy(dst->bounds_min, src->bounds_min, sizeof(dst->bounds_min));
    memcpy(dst->bounds_max, src->bounds_max, sizeof(dst->bounds_max));

    chunk_reset_transferred_mesh(src);
}

static void chunk_zero_submeshes(SdkChunk* chunk)
{
    int i;
    if (!chunk) return;
    for (i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        chunk_zero_mesh(&chunk->subchunks[i]);
        chunk_zero_mesh(&chunk->water_subchunks[i]);
    }
    chunk_zero_mesh(&chunk->far_mesh);
    chunk_zero_mesh(&chunk->experimental_far_mesh);
    chunk_zero_mesh(&chunk->far_exact_overlay_mesh);
}

void sdk_chunk_recount_far_mesh_excluded_blocks(SdkChunk* chunk)
{
    uint32_t count = 0u;
    uint32_t idx;

    if (!chunk || !chunk->blocks) {
        if (chunk) chunk->far_mesh_excluded_block_count = 0u;
        return;
    }

    for (idx = 0; idx < CHUNK_TOTAL_BLOCKS; ++idx) {
        if (sdk_chunk_block_excluded_from_far_mesh(
                sdk_world_cell_decode_full_block(chunk->blocks[idx]))) {
            count++;
        }
    }
    chunk->far_mesh_excluded_block_count = count;
}

void sdk_chunk_count_cell_kinds(const SdkChunk* chunk, SdkChunkCellKindCounts* out_counts)
{
    uint32_t idx;

    if (!out_counts) return;
    memset(out_counts, 0, sizeof(*out_counts));
    if (!chunk || !chunk->blocks) return;

    for (idx = 0; idx < CHUNK_TOTAL_BLOCKS; ++idx) {
        switch (sdk_world_cell_kind(chunk->blocks[idx])) {
            case SDK_WORLD_CELL_KIND_FULL_BLOCK:
                out_counts->full_block_count++;
                break;
            case SDK_WORLD_CELL_KIND_INLINE_CONSTRUCTION:
                out_counts->inline_construction_count++;
                break;
            case SDK_WORLD_CELL_KIND_OVERFLOW_CONSTRUCTION:
                out_counts->overflow_construction_count++;
                break;
            default:
                break;
        }
    }
}

void sdk_chunk_mark_subchunk_dirty(SdkChunk* chunk, int subchunk_index)
{
    chunk_mark_subchunk_dirty_internal(chunk, subchunk_index, true);
}

void sdk_chunk_mark_subchunk_fill_dirty(SdkChunk* chunk, int subchunk_index)
{
    chunk_mark_subchunk_dirty_internal(chunk, subchunk_index, false);
}

void sdk_chunk_mark_all_dirty(SdkChunk* chunk)
{
    int i;
    if (!chunk) return;

    chunk->target_mesh_generation++; /* Track that we need a new mesh */
    chunk_advance_mesh_job_generation(chunk);
    chunk->dirty_subchunks_mask = 0;
    for (i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        chunk->subchunks[i].dirty = true;
        chunk->water_subchunks[i].dirty = true;
        chunk->dirty_subchunks_mask |= (1u << i);
    }
    chunk->dirty = true;
    chunk->geometry_dirty = 1u;
    chunk->far_mesh.dirty = true;
    chunk->experimental_far_mesh.dirty = true;
    chunk->far_exact_overlay_mesh.dirty = true;
    chunk->far_mesh_dirty = 1u;
    chunk->dirty_frame_age = 0u;
}

void sdk_chunk_mark_block_dirty(SdkChunk* chunk, int lx, int ly, int lz)
{
    int sy;
    (void)lx;
    (void)lz;

    if (!chunk) return;
    if ((unsigned)ly >= CHUNK_HEIGHT) return;

    sy = sdk_chunk_subchunk_index_from_y(ly);
    sdk_chunk_mark_subchunk_dirty(chunk, sy);

    if ((ly % CHUNK_SUBCHUNK_HEIGHT) == 0 && sy > 0) {
        sdk_chunk_mark_subchunk_dirty(chunk, sy - 1);
    }
    if ((ly % CHUNK_SUBCHUNK_HEIGHT) == (CHUNK_SUBCHUNK_HEIGHT - 1) &&
        sy + 1 < CHUNK_SUBCHUNK_COUNT) {
        sdk_chunk_mark_subchunk_dirty(chunk, sy + 1);
    }
}

void sdk_chunk_mark_block_fill_dirty(SdkChunk* chunk, int lx, int ly, int lz)
{
    int sy;
    (void)lx;
    (void)lz;

    if (!chunk) return;
    if ((unsigned)ly >= CHUNK_HEIGHT) return;

    sy = sdk_chunk_subchunk_index_from_y(ly);
    sdk_chunk_mark_subchunk_fill_dirty(chunk, sy);

    if ((ly % CHUNK_SUBCHUNK_HEIGHT) == 0 && sy > 0) {
        sdk_chunk_mark_subchunk_fill_dirty(chunk, sy - 1);
    }
    if ((ly % CHUNK_SUBCHUNK_HEIGHT) == (CHUNK_SUBCHUNK_HEIGHT - 1) &&
        sy + 1 < CHUNK_SUBCHUNK_COUNT) {
        sdk_chunk_mark_subchunk_fill_dirty(chunk, sy + 1);
    }
}

void sdk_chunk_refresh_mesh_state(SdkChunk* chunk)
{
    int i;
    bool any_dirty;
    bool any_geometry_dirty;
    bool any_far_dirty;
    bool any_upload_pending;
    bool any_active;
    bool far_active;
    bool experimental_far_active;
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;

    if (!chunk) return;

    chunk->dirty_subchunks_mask = 0;
    chunk->upload_subchunks_mask = 0;
    chunk->active_subchunks_mask = 0;
    chunk->vertex_count = 0;
    any_geometry_dirty = false;
    any_far_dirty = chunk->far_mesh.dirty ||
                    chunk->experimental_far_mesh.dirty ||
                    chunk->far_exact_overlay_mesh.dirty;
    any_upload_pending = chunk->far_mesh.upload_dirty ||
                         chunk->experimental_far_mesh.upload_dirty ||
                         chunk->far_exact_overlay_mesh.upload_dirty;
    any_dirty = chunk->far_mesh.dirty ||
                chunk->experimental_far_mesh.dirty ||
                chunk->far_exact_overlay_mesh.dirty;
    any_active = false;
    far_active = chunk->far_mesh.vertex_count > 0;
    experimental_far_active = chunk->experimental_far_mesh.vertex_count > 0;
    min_x = FLT_MAX;
    min_y = FLT_MAX;
    min_z = FLT_MAX;
    max_x = -FLT_MAX;
    max_y = -FLT_MAX;
    max_z = -FLT_MAX;

    for (i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        const SdkChunkSubmesh* sub = &chunk->subchunks[i];
        const SdkChunkSubmesh* water = &chunk->water_subchunks[i];
        if (sub->dirty) {
            any_dirty = true;
            any_geometry_dirty = true;
        }
        if (water->dirty) {
            any_dirty = true;
            any_geometry_dirty = true;
        }
        if (sub->dirty || water->dirty) {
            chunk->dirty_subchunks_mask |= (1u << i);
        }
        if (sub->upload_dirty || water->upload_dirty) {
            chunk->upload_subchunks_mask |= (1u << i);
            any_upload_pending = true;
        }
        if (sub->vertex_count > 0 || water->vertex_count > 0) {
            chunk->active_subchunks_mask |= (1u << i);
        }
        chunk->vertex_count += sub->vertex_count;
        chunk->vertex_count += water->vertex_count;
        chunk_accumulate_submesh_bounds(sub, &any_active,
                                        &min_x, &min_y, &min_z,
                                        &max_x, &max_y, &max_z);
        chunk_accumulate_submesh_bounds(water, &any_active,
                                        &min_x, &min_y, &min_z,
                                        &max_x, &max_y, &max_z);
    }

    chunk->dirty = any_dirty;
    chunk->geometry_dirty = any_geometry_dirty ? 1u : 0u;
    chunk->far_mesh_dirty = any_far_dirty ? 1u : 0u;
    chunk->upload_pending = any_upload_pending ? 1u : 0u;
    if (chunk->dirty) {
        chunk->dirty_frame_age++;
    } else {
        chunk->dirty_frame_age = 0u;
    }
    chunk->empty = !(any_active || far_active || experimental_far_active);
    if (!any_dirty) {
        chunk->sim_dirty_mask = 0u;
    }

    if (any_active) {
        chunk->bounds_min[0] = min_x;
        chunk->bounds_min[1] = min_y;
        chunk->bounds_min[2] = min_z;
        chunk->bounds_max[0] = max_x;
        chunk->bounds_max[1] = max_y;
        chunk->bounds_max[2] = max_z;
    } else if (far_active) {
        memcpy(chunk->bounds_min, chunk->far_mesh.bounds_min, sizeof(chunk->bounds_min));
        memcpy(chunk->bounds_max, chunk->far_mesh.bounds_max, sizeof(chunk->bounds_max));
    } else if (experimental_far_active) {
        memcpy(chunk->bounds_min, chunk->experimental_far_mesh.bounds_min, sizeof(chunk->bounds_min));
        memcpy(chunk->bounds_max, chunk->experimental_far_mesh.bounds_max, sizeof(chunk->bounds_max));
    } else {
        memset(chunk->bounds_min, 0, sizeof(chunk->bounds_min));
        memset(chunk->bounds_max, 0, sizeof(chunk->bounds_max));
    }
}

void sdk_chunk_take_mesh_state(SdkChunk* dst, SdkChunk* src)
{
    int i;

    if (!dst || !src) return;

    for (i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        chunk_take_single_mesh(&dst->subchunks[i], &src->subchunks[i]);
        chunk_take_single_mesh(&dst->water_subchunks[i], &src->water_subchunks[i]);
    }

    chunk_take_single_mesh(&dst->far_mesh, &src->far_mesh);
    chunk_take_single_mesh(&dst->experimental_far_mesh, &src->experimental_far_mesh);
    chunk_take_single_mesh(&dst->far_exact_overlay_mesh, &src->far_exact_overlay_mesh);

    dst->dirty_subchunks_mask = src->dirty_subchunks_mask;
    dst->upload_subchunks_mask = src->upload_subchunks_mask;
    dst->active_subchunks_mask = src->active_subchunks_mask;
    dst->vertex_count = src->vertex_count;
    memcpy(dst->bounds_min, src->bounds_min, sizeof(dst->bounds_min));
    memcpy(dst->bounds_max, src->bounds_max, sizeof(dst->bounds_max));
    dst->dirty = src->dirty;
    dst->geometry_dirty = src->geometry_dirty;
    dst->far_mesh_dirty = src->far_mesh_dirty;
    dst->upload_pending = src->upload_pending;
    dst->cpu_mesh_generation = src->cpu_mesh_generation ? src->cpu_mesh_generation : src->mesh_job_generation;
    dst->gpu_mesh_generation = src->gpu_mesh_generation;
    dst->dirty_frame_age = src->dirty_frame_age;
    dst->empty = src->empty;
}

void sdk_chunk_apply_mesh_state(SdkChunk* dst, SdkChunk* src, uint32_t dirty_mask)
{
    int i;

    if (!dst || !src) return;

    for (i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        const uint32_t bit = (1u << i);

        if ((dirty_mask & bit) == 0u) continue;

        chunk_take_single_mesh(&dst->subchunks[i], &src->subchunks[i]);
        chunk_take_single_mesh(&dst->water_subchunks[i], &src->water_subchunks[i]);
    }

    if (src->far_mesh.upload_dirty) {
        chunk_take_single_mesh(&dst->far_mesh, &src->far_mesh);
    }
    if (src->experimental_far_mesh.upload_dirty) {
        chunk_take_single_mesh(&dst->experimental_far_mesh, &src->experimental_far_mesh);
    }
    if (src->far_exact_overlay_mesh.upload_dirty) {
        chunk_take_single_mesh(&dst->far_exact_overlay_mesh, &src->far_exact_overlay_mesh);
    }

    dst->cpu_mesh_generation = src->cpu_mesh_generation ? src->cpu_mesh_generation : src->mesh_job_generation;
    dst->gpu_mesh_generation = 0u;
    sdk_chunk_refresh_mesh_state(dst);
}

void sdk_chunk_init_with_space(SdkChunk* chunk,
                               int cx,
                               int cz,
                               SdkCoordinateSpaceType space_type,
                               SdkConstructionArchetypeRegistry* construction_registry)
{
    if (!chunk) return;
    
    memset(chunk, 0, sizeof(SdkChunk));
    chunk->cx = (int16_t)cx;
    chunk->cz = (int16_t)cz;
    chunk->space_type = (uint8_t)space_type;
    chunk->empty = true;
    chunk->construction_registry = construction_registry;
    
    /* Allocate block storage */
    chunk->blocks = (SdkWorldCellCode*)calloc(CHUNK_TOTAL_BLOCKS, sizeof(SdkWorldCellCode));
    if (!chunk->blocks) {
        /* Allocation failed - chunk will be empty */
        chunk->empty = true;
        return;
    }

    chunk->sim_state = sdk_simulation_chunk_state_create();
    chunk->construction_cells = NULL;
    chunk->remesh_queued = false;
    chunk->sim_dirty_mask = 0u;
    chunk->mesh_job_generation = 0u;
    chunk->target_mesh_generation = 0u;
    chunk->inflight_mesh_generation = 0u;
    chunk->cpu_mesh_generation = 0u;
    chunk->gpu_mesh_generation = 0u;
    chunk->far_mesh_excluded_block_count = 0u;
    chunk->geometry_dirty = 0u;
    chunk->far_mesh_dirty = 0u;
    chunk->upload_pending = 0u;
    chunk->gpu_upload_mode = SDK_CHUNK_GPU_UPLOAD_NONE;
    chunk->dirty_frame_age = 0u;
    chunk->wall_finalized_generation = 0u;

    sdk_chunk_mark_all_dirty(chunk);
}

void sdk_chunk_init(SdkChunk* chunk, int cx, int cz, SdkConstructionArchetypeRegistry* construction_registry)
{
    sdk_chunk_init_with_space(chunk, cx, cz, SDK_SPACE_GLOBAL_CHUNK, construction_registry);
}

void sdk_chunk_free(SdkChunk* chunk)
{
    if (!chunk) return;
    
    if (chunk->blocks) {
        free(chunk->blocks);
        chunk->blocks = NULL;
    }
    if (chunk->sim_state) {
        sdk_simulation_chunk_state_destroy(chunk->sim_state);
        chunk->sim_state = NULL;
    }
    if (chunk->construction_cells) {
        sdk_construction_store_free(chunk->construction_cells);
        chunk->construction_cells = NULL;
    }
    chunk->construction_registry = NULL;

    chunk_zero_submeshes(chunk);
    sdk_renderer_free_chunk_unified_buffer(chunk);
    chunk->dirty_subchunks_mask = 0;
    chunk->upload_subchunks_mask = 0;
    chunk->active_subchunks_mask = 0;
    chunk->sim_dirty_mask = 0;
    chunk->vertex_count = 0;
    chunk->remesh_queued = false;
    chunk->mesh_job_generation = 0u;
    chunk->target_mesh_generation = 0u;
    chunk->inflight_mesh_generation = 0u;
    chunk->cpu_mesh_generation = 0u;
    chunk->gpu_mesh_generation = 0u;
    chunk->far_mesh_excluded_block_count = 0u;
    chunk->geometry_dirty = 0u;
    chunk->far_mesh_dirty = 0u;
    chunk->upload_pending = 0u;
    chunk->gpu_upload_mode = SDK_CHUNK_GPU_UPLOAD_NONE;
    chunk->dirty_frame_age = 0u;
    chunk->wall_finalized_generation = 0u;
    chunk->unified_staging = NULL;
    chunk->unified_staging_capacity = 0u;
    chunk->unified_total_vertices = 0u;
    chunk->unified_vertex_capacity = 0u;
    memset(chunk->bounds_min, 0, sizeof(chunk->bounds_min));
    memset(chunk->bounds_max, 0, sizeof(chunk->bounds_max));
    chunk->dirty = false;
    chunk->empty = true;
}

void sdk_chunk_free_mesh(SdkChunk* chunk)
{
    int i;
    if (!chunk) return;

    sdk_renderer_free_chunk_mesh(chunk);
    sdk_renderer_free_chunk_unified_buffer(chunk);
    chunk->unified_staging = NULL;
    chunk->unified_staging_capacity = 0;
    chunk->unified_total_vertices = 0;
    chunk->unified_vertex_capacity = 0;
    chunk->gpu_mesh_generation = 0u;
    chunk->cpu_mesh_generation = 0u;

    for (i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        free(chunk->subchunks[i].cpu_vertices);
        chunk->subchunks[i].cpu_vertices = NULL;
        chunk->subchunks[i].vertex_count = 0;
        chunk->subchunks[i].vertex_capacity = 0;

        free(chunk->water_subchunks[i].cpu_vertices);
        chunk->water_subchunks[i].cpu_vertices = NULL;
        chunk->water_subchunks[i].vertex_count = 0;
        chunk->water_subchunks[i].vertex_capacity = 0;
    }

    free(chunk->far_mesh.cpu_vertices);
    chunk->far_mesh.cpu_vertices = NULL;
    chunk->far_mesh.vertex_count = 0;
    chunk->far_mesh.vertex_capacity = 0;

    free(chunk->experimental_far_mesh.cpu_vertices);
    chunk->experimental_far_mesh.cpu_vertices = NULL;
    chunk->experimental_far_mesh.vertex_count = 0;
    chunk->experimental_far_mesh.vertex_capacity = 0;

    free(chunk->far_exact_overlay_mesh.cpu_vertices);
    chunk->far_exact_overlay_mesh.cpu_vertices = NULL;
    chunk->far_exact_overlay_mesh.vertex_count = 0;
    chunk->far_exact_overlay_mesh.vertex_capacity = 0;
}

void sdk_chunk_clear(SdkChunk* chunk)
{
    int i;
    if (!chunk || !chunk->blocks) return;
    
    memset(chunk->blocks, 0, CHUNK_TOTAL_BLOCKS * sizeof(SdkWorldCellCode));
    chunk->far_mesh_excluded_block_count = 0u;
    if (chunk->sim_state) {
        sdk_simulation_chunk_state_clear(chunk->sim_state);
    }
    for (i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        SdkChunkSubmesh* sub = &chunk->subchunks[i];
        SdkChunkSubmesh* water = &chunk->water_subchunks[i];
        if (sub->cpu_vertices) {
            free(sub->cpu_vertices);
            sub->cpu_vertices = NULL;
        }
        sub->vertex_capacity = 0;
        sub->vertex_count = 0;
        memset(sub->bounds_min, 0, sizeof(sub->bounds_min));
        memset(sub->bounds_max, 0, sizeof(sub->bounds_max));
        sub->empty = true;
        sub->dirty = true;
        sub->upload_dirty = false;
        if (water->cpu_vertices) {
            free(water->cpu_vertices);
            water->cpu_vertices = NULL;
        }
        water->vertex_capacity = 0;
        water->vertex_count = 0;
        memset(water->bounds_min, 0, sizeof(water->bounds_min));
        memset(water->bounds_max, 0, sizeof(water->bounds_max));
        water->empty = true;
        water->dirty = true;
        water->upload_dirty = false;
    }
    chunk_zero_mesh(&chunk->far_mesh);
    chunk->far_mesh.dirty = true;
    chunk_zero_mesh(&chunk->experimental_far_mesh);
    chunk->experimental_far_mesh.dirty = true;
    chunk_zero_mesh(&chunk->far_exact_overlay_mesh);
    chunk->far_exact_overlay_mesh.dirty = true;
    chunk->vertex_count = 0;
    chunk->active_subchunks_mask = 0;
    chunk->sim_dirty_mask = 0;
    chunk->geometry_dirty = 0u;
    chunk->far_mesh_dirty = 0u;
    chunk->upload_pending = 0u;
    chunk->gpu_upload_mode = SDK_CHUNK_GPU_UPLOAD_NONE;
    chunk->dirty_frame_age = 0u;
    chunk->wall_finalized_generation = 0u;
    memset(chunk->bounds_min, 0, sizeof(chunk->bounds_min));
    memset(chunk->bounds_max, 0, sizeof(chunk->bounds_max));
    chunk->remesh_queued = false;
    chunk->mesh_job_generation = 0u;
    chunk->target_mesh_generation = 0u;
    chunk->inflight_mesh_generation = 0u;
    chunk->cpu_mesh_generation = 0u;
    chunk->gpu_mesh_generation = 0u;
    sdk_chunk_mark_all_dirty(chunk);
    chunk->empty = true;
}

/** Check if chunk is visible in camera frustum using sphere test */
bool sdk_chunk_is_visible(const SdkChunk* chunk, const SdkCamera* camera)
{
    uint32_t visible_vertex_count = 0u;

    if (chunk) {
        visible_vertex_count = chunk->vertex_count;
        if (visible_vertex_count == 0u) {
            visible_vertex_count = chunk->far_mesh.vertex_count;
        }
        if (visible_vertex_count == 0u) {
            visible_vertex_count = chunk->experimental_far_mesh.vertex_count;
        }
    }

    if (!chunk || chunk->empty || visible_vertex_count == 0u) {
        return false;
    }

    float min_x = chunk->bounds_min[0];
    float min_y = chunk->bounds_min[1];
    float min_z = chunk->bounds_min[2];
    float max_x = chunk->bounds_max[0];
    float max_y = chunk->bounds_max[1];
    float max_z = chunk->bounds_max[2];

    if (max_x <= min_x || max_y <= min_y || max_z <= min_z) {
        min_x = (float)(chunk->cx * CHUNK_WIDTH);
        min_y = 0.0f;
        min_z = (float)(chunk->cz * CHUNK_DEPTH);
        max_x = min_x + CHUNK_WIDTH;
        max_y = (float)CHUNK_HEIGHT;
        max_z = min_z + CHUNK_DEPTH;
    }

    return sdk_frustum_contains_aabb(&camera->frustum, min_x, min_y, min_z, max_x, max_y, max_z);
}

bool sdk_chunk_subchunk_is_visible(const SdkChunk* chunk, int subchunk_index, const SdkCamera* camera)
{
    const SdkChunkSubmesh* sub;
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;

    if (!chunk || !camera) return false;
    if ((unsigned)subchunk_index >= CHUNK_SUBCHUNK_COUNT) return false;

    sub = &chunk->subchunks[subchunk_index];
    {
        const SdkChunkSubmesh* water = &chunk->water_subchunks[subchunk_index];
        int have_opaque = !sub->empty && sub->vertex_count > 0;
        int have_water = !water->empty && water->vertex_count > 0;

        if (!have_opaque && !have_water) return false;

        if (have_opaque) {
            min_x = sub->bounds_min[0];
            min_y = sub->bounds_min[1];
            min_z = sub->bounds_min[2];
            max_x = sub->bounds_max[0];
            max_y = sub->bounds_max[1];
            max_z = sub->bounds_max[2];
        } else {
            min_x = water->bounds_min[0];
            min_y = water->bounds_min[1];
            min_z = water->bounds_min[2];
            max_x = water->bounds_max[0];
            max_y = water->bounds_max[1];
            max_z = water->bounds_max[2];
        }

        if (have_opaque && have_water) {
            if (water->bounds_min[0] < min_x) min_x = water->bounds_min[0];
            if (water->bounds_min[1] < min_y) min_y = water->bounds_min[1];
            if (water->bounds_min[2] < min_z) min_z = water->bounds_min[2];
            if (water->bounds_max[0] > max_x) max_x = water->bounds_max[0];
            if (water->bounds_max[1] > max_y) max_y = water->bounds_max[1];
            if (water->bounds_max[2] > max_z) max_z = water->bounds_max[2];
        }
    }

    if (max_x <= min_x || max_y <= min_y || max_z <= min_z) {
        min_x = (float)(chunk->cx * CHUNK_WIDTH);
        min_y = (float)sdk_chunk_subchunk_min_y(subchunk_index);
        min_z = (float)(chunk->cz * CHUNK_DEPTH);
        max_x = min_x + CHUNK_WIDTH;
        max_y = min_y + CHUNK_SUBCHUNK_HEIGHT;
        max_z = min_z + CHUNK_DEPTH;
    }

    return sdk_frustum_contains_aabb(&camera->frustum, min_x, min_y, min_z, max_x, max_y, max_z);
}
