#include "sdk_api_session_internal.h"
#include "../../../World/Config/sdk_world_config.h"
#include "../../../World/CoordinateSpaces/sdk_coordinate_space_runtime.h"

/* ============================================================================
 * Forward Declarations for Static Helper Functions
 * ============================================================================ */
static void stamp_editor_voxels_into_chunk_volume(SdkChunk* chunk, const uint8_t* voxels,
                                                  int local_x, int local_z, int min_y,
                                                  int width, int depth, int height);
static void stamp_editor_icon_plane(SdkChunk* chunk, const uint8_t* voxels);
static int world_generation_session_present_frame(void);

/**
 * Resets async world geometry release state
 */
void reset_async_world_release_state(void)
{
    g_async_world_release_slot_cursor = 0;
    g_async_world_release_total = -1;
    g_async_world_release_freed = 0;
    g_async_world_release_force_abandon = 0;
}

/**
 * Releases world geometry progressively, returns 1 when complete
 */
int release_world_geometry_step(int max_chunks_per_frame, float* out_progress)
{
    int released_this_frame = 0;
    int slot_capacity;

    if (out_progress) *out_progress = 1.0f;
    if (!g_sdk.chunks_initialized) {
        reset_async_world_release_state();
        return 1;
    }

    if (g_async_world_release_total < 0) {
        g_async_world_release_total = (int)g_sdk.chunk_mgr.resident_count;
        g_async_world_release_freed = 0;
        g_async_world_release_slot_cursor = 0;
        g_async_world_release_force_abandon = 0;
        sdk_renderer_set_chunk_manager(NULL);
    }

    slot_capacity = sdk_chunk_manager_slot_capacity();
    while (g_async_world_release_slot_cursor < slot_capacity &&
           released_this_frame < max_chunks_per_frame) {
        int slot_index = g_async_world_release_slot_cursor++;
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(&g_sdk.chunk_mgr, slot_index);
        if (!slot || !slot->occupied) continue;
        sdk_renderer_free_chunk_mesh(&slot->chunk);
        sdk_chunk_manager_release_slot(&g_sdk.chunk_mgr, slot);
        g_async_world_release_freed++;
        released_this_frame++;
    }

    if (out_progress) {
        if (g_async_world_release_total > 0) {
            *out_progress = api_clampf((float)g_async_world_release_freed /
                                       (float)g_async_world_release_total,
                                       0.0f, 1.0f);
        } else {
            *out_progress = 1.0f;
        }
    }

    if (g_sdk.chunk_mgr.resident_count > 0 &&
        g_async_world_release_slot_cursor >= slot_capacity) {
        if (g_async_world_release_force_abandon) {
            memset(&g_sdk.chunk_mgr, 0, sizeof(g_sdk.chunk_mgr));
            g_sdk.chunks_initialized = false;
            reset_async_world_release_state();
            if (out_progress) *out_progress = 1.0f;
            return 1;
        }
        g_async_world_release_force_abandon = 1;
        g_async_world_release_slot_cursor = 0;
        return 0;
    }

    if (g_sdk.chunk_mgr.resident_count > 0) {
        return 0;
    }

    memset(&g_sdk.chunk_mgr, 0, sizeof(g_sdk.chunk_mgr));
    g_sdk.chunks_initialized = false;
    reset_async_world_release_state();
    if (out_progress) *out_progress = 1.0f;
    return 1;
}

int count_tracked_dirty_chunks_complete(const int* tracked_cx,
                                       const int* tracked_cz,
                                       int tracked_count)
{
    int done = 0;
    if (!tracked_cx || !tracked_cz || tracked_count <= 0) return 0;
    for (int index = 0; index < tracked_count; ++index) {
        const SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr,
                                                            tracked_cx[index],
                                                            tracked_cz[index]);
        if (!chunk || !chunk->blocks || !sdk_chunk_needs_remesh(chunk)) {
            done++;
        }
    }
    return done;
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */
static uint8_t session_fluid_fill_at_world(SdkChunkManager* cm, int wx, int wy, int wz, BlockType* out_block)
{
    int cx;
    int cz;
    int lx;
    int lz;
    SdkChunk* chunk;

    if (out_block) *out_block = BLOCK_AIR;
    if (!cm || wy < 0 || wy >= CHUNK_HEIGHT) return 0u;

    cx = sdk_world_to_chunk_x(wx);
    cz = sdk_world_to_chunk_z(wz);
    chunk = sdk_chunk_manager_get_chunk(cm, cx, cz);
    if (!chunk) return 0u;

    lx = sdk_world_to_local_x(wx, cx);
    lz = sdk_world_to_local_z(wz, cz);
    if (out_block) *out_block = sdk_chunk_get_block(chunk, lx, wy, lz);
    return sdk_simulation_get_fluid_fill(chunk, lx, wy, lz);
}

static SdkStartupChunkReadiness g_startup_chunk_readiness;
static SdkStartupChunkReadiness g_startup_bootstrap_completion_readiness;
static SdkRuntimeChunkHealth g_runtime_chunk_health;
static bool g_startup_safe_mode_enabled = true;
static bool g_startup_bootstrap_complete = false;

bool startup_safe_mode_active(void)
{
    return g_startup_safe_mode_enabled;
}

bool startup_bootstrap_completed(void)
{
    return g_startup_bootstrap_complete;
}

const SdkStartupChunkReadiness* startup_bootstrap_completion_readiness(void)
{
    return &g_startup_bootstrap_completion_readiness;
}

static int startup_chunk_chebyshev_distance(int cx, int cz)
{
    int dx = cx - g_sdk.chunk_mgr.cam_cx;
    int dz = cz - g_sdk.chunk_mgr.cam_cz;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return (dx > dz) ? dx : dz;
}

int startup_safe_primary_radius(void)
{
    int radius = initial_sync_bootstrap_distance();
    if (radius < 1) radius = 1;
    return radius;
}

int startup_safe_neighbor_radius(void)
{
    return startup_safe_primary_radius() + 1;
}

static int startup_chunk_requires_full_upload(SdkChunkResidencyRole role, int cx, int cz)
{
    if (role == SDK_CHUNK_ROLE_PRIMARY) {
        return startup_chunk_chebyshev_distance(cx, cz) <= startup_safe_neighbor_radius();
    }
    if (role == SDK_CHUNK_ROLE_WALL_SUPPORT) {
        return startup_chunk_chebyshev_distance(cx, cz) <= startup_safe_primary_radius();
    }
    return 0;
}

static int startup_chunk_in_sync_safety_set(SdkChunkResidencyRole role, int cx, int cz)
{
    if (role == SDK_CHUNK_ROLE_PRIMARY) {
        return startup_chunk_chebyshev_distance(cx, cz) <= startup_safe_primary_radius();
    }
    if (role == SDK_CHUNK_ROLE_WALL_SUPPORT) {
        return startup_chunk_chebyshev_distance(cx, cz) <= startup_safe_primary_radius();
    }
    return 0;
}

void collect_startup_chunk_readiness(SdkStartupChunkReadiness* out_readiness)
{
    SdkStartupChunkReadiness readiness;
    static ULONGLONG s_last_debug_ms = 0u;
    int skip_count_not_primary = 0;
    int skip_count_outside_radius = 0;
    int skip_count_no_chunk = 0;
    int skip_count_no_blocks = 0;

    memset(&readiness, 0, sizeof(readiness));
    readiness.pending_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
    readiness.pending_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
    readiness.active_workers = sdk_chunk_streamer_active_workers(&g_sdk.chunk_streamer);

    if (g_sdk.chunks_initialized) {
        const int safety_radius = startup_safe_primary_radius();

        for (int i = 0; i < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++i) {
            const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, i);
            SdkChunkResidentSlot* slot;
            SdkChunk* chunk;
            int distance;

            if (!target) continue;

            distance = startup_chunk_chebyshev_distance(target->cx, target->cz);

            if (!startup_chunk_in_sync_safety_set((SdkChunkResidencyRole)target->role,
                                                  target->cx,
                                                  target->cz)) {
                skip_count_not_primary++;
                continue;
            }
            if (distance > safety_radius &&
                (SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_PRIMARY) {
                skip_count_outside_radius++;
                continue;
            }

            readiness.desired_primary++;
            slot = sdk_chunk_manager_find_slot(&g_sdk.chunk_mgr, target->cx, target->cz);
            if (!slot || !slot->occupied) {
                skip_count_no_chunk++;
                continue;
            }
            chunk = &slot->chunk;
            if (!chunk->blocks) {
                skip_count_no_blocks++;
                continue;
            }

            readiness.resident_primary++;
            if (sdk_chunk_has_full_upload_ready_mesh(chunk)) {
                readiness.gpu_ready_primary++;
                continue;
            }

            if (sdk_chunk_needs_remesh(chunk)) {
                readiness.no_cpu_mesh++;
            } else if (chunk->upload_pending) {
                readiness.upload_pending++;
            } else if ((SdkChunkGpuUploadMode)chunk->gpu_upload_mode != SDK_CHUNK_GPU_UPLOAD_FULL) {
                readiness.far_only_when_full_needed++;
            } else if (!chunk->empty &&
                       (!chunk->unified_vertex_buffer ||
                        chunk->gpu_mesh_generation == 0u ||
                        chunk->gpu_mesh_generation != chunk->cpu_mesh_generation)) {
                readiness.gpu_mesh_generation_stale++;
            } else {
                readiness.other_not_ready++;
            }
        }
    }

    if ((GetTickCount64() - s_last_debug_ms) >= 1000u) {
        char dbg[512];
        s_last_debug_ms = GetTickCount64();
        sprintf_s(dbg, sizeof(dbg),
            "[READINESS] desired=%d resident=%d gpu_ready=%d | "
            "skips: not_primary=%d outside_radius=%d no_chunk=%d no_blocks=%d | "
            "pending: workers=%d jobs=%d results=%d\n",
            readiness.desired_primary, readiness.resident_primary, readiness.gpu_ready_primary,
            skip_count_not_primary, skip_count_outside_radius, skip_count_no_chunk, skip_count_no_blocks,
            readiness.active_workers, readiness.pending_jobs, readiness.pending_results);
        OutputDebugStringA(dbg);
    }

    g_startup_chunk_readiness = readiness;
    if (out_readiness) {
        *out_readiness = readiness;
    }
}

const SdkStartupChunkReadiness* startup_safe_mode_readiness(void)
{
    return &g_startup_chunk_readiness;
}

static int runtime_chunk_is_gpu_ready(const SdkChunk* chunk)
{
    if (!chunk || !chunk->blocks) return 0;
    if (chunk->empty && !chunk->upload_pending && !sdk_chunk_needs_remesh(chunk)) {
        return 1;
    }
    return sdk_chunk_has_current_unified_gpu_mesh(chunk) &&
           !chunk->upload_pending &&
           !sdk_chunk_needs_remesh(chunk);
}

void collect_runtime_chunk_health(SdkRuntimeChunkHealth* out_health)
{
    SdkRuntimeChunkHealth health;

    memset(&health, 0, sizeof(health));
    health.pending_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
    health.pending_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
    health.active_workers = sdk_chunk_streamer_active_workers(&g_sdk.chunk_streamer);

    if (g_sdk.chunks_initialized) {
        for (int i = 0; i < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++i) {
            const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, i);
            SdkChunkResidentSlot* slot;
            SdkChunk* chunk;

            if (!target) continue;
            health.desired_visible++;
            slot = sdk_chunk_manager_find_slot(&g_sdk.chunk_mgr, target->cx, target->cz);
            if (!slot || !slot->occupied) continue;
            chunk = &slot->chunk;
            if (!chunk->blocks) continue;
            health.resident_visible++;
            if (runtime_chunk_is_gpu_ready(chunk)) {
                health.gpu_ready_visible++;
            }
        }
    }

    g_runtime_chunk_health = health;
    if (out_health) {
        *out_health = health;
    }
}

static void latch_startup_bootstrap_completion(const SdkStartupChunkReadiness* readiness)
{
    if (g_startup_bootstrap_complete) return;
    if (readiness) {
        g_startup_bootstrap_completion_readiness = *readiness;
    } else {
        collect_startup_chunk_readiness(&g_startup_bootstrap_completion_readiness);
    }
    g_startup_bootstrap_complete = true;

    {
        char dbg[256];
        const SdkStartupChunkReadiness* latched = &g_startup_bootstrap_completion_readiness;
        sprintf_s(dbg, sizeof(dbg),
                  "[STARTUP] safety-set latched desired=%d resident=%d ready=%d jobs=%d results=%d "
                  "no_cpu=%d upload=%d stale=%d far_only=%d other=%d\n",
                  latched->desired_primary,
                  latched->resident_primary,
                  latched->gpu_ready_primary,
                  latched->pending_jobs,
                  latched->pending_results,
                  latched->no_cpu_mesh,
                  latched->upload_pending,
                  latched->gpu_mesh_generation_stale,
                  latched->far_only_when_full_needed,
                  latched->other_not_ready);
        OutputDebugStringA(dbg);
    }
}

static void refresh_startup_safe_mode_status_text(const SdkStartupChunkReadiness* readiness)
{
    if (!readiness) return;
    if (!g_startup_safe_mode_enabled) return;

    if (g_startup_bootstrap_complete) {
        SdkRuntimeChunkHealth runtime_health;

        collect_runtime_chunk_health(&runtime_health);
        snprintf(g_world_generation_status,
                 sizeof(g_world_generation_status),
                 "Background streaming %d/%d/%d  W%d J%d R%d",
                 runtime_health.desired_visible,
                 runtime_health.resident_visible,
                 runtime_health.gpu_ready_visible,
                 runtime_health.active_workers,
                 runtime_health.pending_jobs,
                 runtime_health.pending_results);
        return;
    }

    snprintf(g_world_generation_status,
             sizeof(g_world_generation_status),
             "Loading nearby terrain %d/%d/%d  W%d J%d R%d",
             readiness->desired_primary,
             readiness->resident_primary,
             readiness->gpu_ready_primary,
             readiness->active_workers,
             readiness->pending_jobs,
             readiness->pending_results);
}

static SdkChunkGpuUploadMode target_upload_mode_for_chunk(SdkChunkResidencyRole role,
                                                          int cx,
                                                          int cz)
{
    switch (role) {
        case SDK_CHUNK_ROLE_WALL_SUPPORT:
            if (startup_safe_mode_active() && !startup_chunk_requires_full_upload(role, cx, cz)) {
                return SDK_CHUNK_GPU_UPLOAD_FAR_ONLY;
            }
            return SDK_CHUNK_GPU_UPLOAD_FULL;
        case SDK_CHUNK_ROLE_FRONTIER:
        case SDK_CHUNK_ROLE_TRANSITION_PRELOAD:
        case SDK_CHUNK_ROLE_EVICT_PENDING:
            return SDK_CHUNK_GPU_UPLOAD_FAR_ONLY;
        case SDK_CHUNK_ROLE_PRIMARY:
        case SDK_CHUNK_ROLE_NONE:
        default:
            return SDK_CHUNK_GPU_UPLOAD_FULL;
    }
}

static size_t estimate_chunk_upload_bytes_for_mode(const SdkChunk* chunk, SdkChunkGpuUploadMode mode)
{
    uint64_t total_vertices = 0u;

    if (!chunk) return 0u;

    if (mode == SDK_CHUNK_GPU_UPLOAD_FULL) {
        for (int i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
            total_vertices += chunk->subchunks[i].vertex_count;
            total_vertices += chunk->water_subchunks[i].vertex_count;
        }
    }

    total_vertices += chunk->far_mesh.vertex_count;
    total_vertices += chunk->experimental_far_mesh.vertex_count;
    total_vertices += chunk->far_exact_overlay_mesh.vertex_count;
    return (size_t)(total_vertices * sizeof(BlockVertex));
}

static int sample_camera_water_state(float cam_x,
                                     float cam_y,
                                     float cam_z,
                                     int* out_camera_submerged,
                                     float* out_waterline_y,
                                     float* out_water_view_depth)
{
    int wx;
    int wz;
    int ref_y;
    int min_y;
    int max_y;
    int y;

    if (out_camera_submerged) *out_camera_submerged = 0;
    if (out_waterline_y) *out_waterline_y = 0.0f;
    if (out_water_view_depth) *out_water_view_depth = 0.0f;

    if (!g_sdk.world_session_active || !g_sdk.chunks_initialized) return 0;

    wx = (int)floorf(cam_x);
    wz = (int)floorf(cam_z);
    ref_y = (int)floorf(cam_y);
    if (ref_y < 0) ref_y = 0;
    if (ref_y >= CHUNK_HEIGHT) ref_y = CHUNK_HEIGHT - 1;

    min_y = ref_y - 16;
    max_y = ref_y + 12;
    if (min_y < 0) min_y = 0;
    if (max_y >= CHUNK_HEIGHT) max_y = CHUNK_HEIGHT - 1;

    for (y = max_y; y >= min_y; --y) {
        BlockType block;
        uint8_t fill = session_fluid_fill_at_world(&g_sdk.chunk_mgr, wx, y, wz, &block);
        int top_y;
        int bottom_y;
        uint8_t top_fill;
        float waterline_y;

        if (block != BLOCK_WATER || fill == 0u) continue;

        top_y = y;
        top_fill = fill;
        while (top_y + 1 < CHUNK_HEIGHT) {
            BlockType above_block;
            uint8_t above_fill = session_fluid_fill_at_world(&g_sdk.chunk_mgr, wx, top_y + 1, wz, &above_block);
            if (above_block != BLOCK_WATER || above_fill == 0u) break;
            top_y++;
            top_fill = above_fill;
        }

        bottom_y = top_y;
        while (bottom_y > 0) {
            BlockType below_block;
            uint8_t below_fill = session_fluid_fill_at_world(&g_sdk.chunk_mgr, wx, bottom_y - 1, wz, &below_block);
            if (below_block != BLOCK_WATER || below_fill == 0u) break;
            bottom_y--;
        }

        waterline_y = (float)top_y + ((float)top_fill / 255.0f);
        if (out_waterline_y) *out_waterline_y = waterline_y;
        if (out_water_view_depth) *out_water_view_depth = waterline_y - (float)bottom_y;

        if (out_camera_submerged) {
            BlockType here_block;
            uint8_t here_fill = session_fluid_fill_at_world(&g_sdk.chunk_mgr, wx, ref_y, wz, &here_block);
            if (here_block == BLOCK_WATER && here_fill > 0u) {
                float cell_surface = (float)ref_y + ((float)here_fill / 255.0f);
                *out_camera_submerged = cam_y < cell_surface;
            }
        }
        return 1;
    }

    return 0;
}

static void repair_dropped_remesh_results(void)
{
    /* Repairs chunks with dropped remesh results, clears inflight flags */
    int cx;
    int cz;
    uint32_t generation;

    while (sdk_chunk_streamer_pop_dropped_remesh(&g_sdk.chunk_streamer, &cx, &cz, &generation)) {
        SdkChunk* slot = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
        if (!slot) {
            continue;
        }
        if (!slot->blocks) {
            continue;
        }
        if (slot->inflight_mesh_generation != generation) {
            continue;
        }
        slot->remesh_queued = false;
        slot->inflight_mesh_generation = 0u;
    }
}

static int wall_corner_chunks_need_mesh_count(void)
{
    /* Counts primary superchunk corner chunks that need mesh */
    int need_mesh = 0;
    SdkSuperchunkCell cell;

    /* Only check the 4 corner chunks for the primary superchunk */
    sdk_superchunk_cell_from_index(g_sdk.chunk_mgr.primary_scx, g_sdk.chunk_mgr.primary_scz, &cell);
    
    /* NW corner */
    int cx = cell.origin_cx;
    int cz = cell.origin_cz;
    SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (chunk && chunk->blocks && !sdk_chunk_has_full_upload_ready_mesh(chunk)) {
        need_mesh++;
    }
    
    /* NE corner */
    cx = cell.east_cx;
    cz = cell.origin_cz;
    chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (chunk && chunk->blocks && !sdk_chunk_has_full_upload_ready_mesh(chunk)) {
        need_mesh++;
    }
    
    /* SW corner */
    cx = cell.origin_cx;
    cz = cell.south_cz;
    chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (chunk && chunk->blocks && !sdk_chunk_has_full_upload_ready_mesh(chunk)) {
        need_mesh++;
    }
    
    /* SE corner */
    cx = cell.east_cx;
    cz = cell.south_cz;
    chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
    if (chunk && chunk->blocks && !sdk_chunk_has_full_upload_ready_mesh(chunk)) {
        need_mesh++;
    }

    return need_mesh;
}

static int build_wall_corner_meshes_sync(int max_chunks)
{
    /* Builds and uploads meshes for primary superchunk corners */
    int built = 0;
    SdkSuperchunkCell cell;

    if (max_chunks <= 0) return 0;

    /* Only check the 4 corner chunks for the primary superchunk */
    sdk_superchunk_cell_from_index(g_sdk.chunk_mgr.primary_scx, g_sdk.chunk_mgr.primary_scz, &cell);
    
    /* NW corner */
    if (built < max_chunks) {
        int cx = cell.origin_cx;
        int cz = cell.origin_cz;
        SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
        if (chunk && chunk->blocks && !sdk_chunk_has_full_upload_ready_mesh(chunk)) {
            build_and_upload_chunk_sync(chunk);
            built++;
        }
    }
    
    /* NE corner */
    if (built < max_chunks) {
        int cx = cell.east_cx;
        int cz = cell.origin_cz;
        SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
        if (chunk && chunk->blocks && !sdk_chunk_has_full_upload_ready_mesh(chunk)) {
            build_and_upload_chunk_sync(chunk);
            built++;
        }
    }
    
    /* SW corner */
    if (built < max_chunks) {
        int cx = cell.origin_cx;
        int cz = cell.south_cz;
        SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
        if (chunk && chunk->blocks && !sdk_chunk_has_full_upload_ready_mesh(chunk)) {
            build_and_upload_chunk_sync(chunk);
            built++;
        }
    }
    
    /* SE corner */
    if (built < max_chunks) {
        int cx = cell.east_cx;
        int cz = cell.south_cz;
        SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);
        if (chunk && chunk->blocks && !sdk_chunk_has_full_upload_ready_mesh(chunk)) {
            build_and_upload_chunk_sync(chunk);
            built++;
        }
    }

    return built;
}

static int sync_upload_startup_primary_chunks(int max_chunks)
{
    int uploaded = 0;
    const int safety_radius = startup_safe_primary_radius();

    if (max_chunks <= 0 || !g_sdk.chunks_initialized) return 0;

    for (int target_index = 0;
         target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr) && uploaded < max_chunks;
         ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);
        SdkChunkResidentSlot* slot;
        SdkChunk* chunk;

        if (!target) continue;
        if (!startup_chunk_in_sync_safety_set((SdkChunkResidencyRole)target->role,
                                              target->cx,
                                              target->cz)) {
            continue;
        }

        slot = sdk_chunk_manager_find_slot(&g_sdk.chunk_mgr, target->cx, target->cz);
        if (!slot || !slot->occupied) continue;

        chunk = &slot->chunk;
        if (!chunk->blocks) continue;
        if (sdk_chunk_has_full_upload_ready_mesh(chunk)) continue;

        build_and_upload_chunk_sync(chunk);
        uploaded++;
    }

    return uploaded;
}

static int collect_loaded_dirty_chunks(int* out_cx,
                                       int* out_cz,
                                       int max_tracked,
                                       int* out_far_only,
                                       int* out_invalid_dirty)
{
    int dirty_count = 0;

    if (out_far_only) *out_far_only = 0;
    if (out_invalid_dirty) *out_invalid_dirty = 0;

    for (int slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(&g_sdk.chunk_mgr, slot_index);
        SdkChunk* chunk;

        if (!slot || !slot->occupied) continue;
        chunk = &slot->chunk;
        if (!chunk->blocks) continue;

        if (chunk->dirty && chunk->dirty_subchunks_mask == 0u && chunk->far_mesh_dirty) {
            if (out_far_only) (*out_far_only)++;
        }
        if (chunk->dirty &&
            chunk->dirty_subchunks_mask == 0u &&
            !chunk->far_mesh.dirty &&
            !chunk->experimental_far_mesh.dirty &&
            !chunk->far_exact_overlay_mesh.dirty) {
            if (out_invalid_dirty) (*out_invalid_dirty)++;
        }

        if (!sdk_chunk_needs_remesh(chunk)) continue;
        if (dirty_count < max_tracked && out_cx && out_cz) {
            out_cx[dirty_count] = chunk->cx;
            out_cz[dirty_count] = chunk->cz;
        }
        dirty_count++;
    }

    return dirty_count;
}

static void process_active_wall_finalization_sync(int max_chunks)
{
    int support_total;
    int support_ready;
    int remaining;

    if (max_chunks <= 0) return;

    support_total = active_wall_stage_total(SDK_ACTIVE_WALL_STAGE_SUPPORT);
    support_ready = active_wall_stage_ready_count(SDK_ACTIVE_WALL_STAGE_SUPPORT);
    if (support_ready < support_total) return;

    remaining = max_chunks;
    remaining -= finalize_active_wall_stage_sync(SDK_ACTIVE_WALL_STAGE_EDGE, remaining);
    if (remaining > 0) {
        finalize_active_wall_stage_sync(SDK_ACTIVE_WALL_STAGE_CORNER, remaining);
    }
}

static int load_missing_active_superchunk_wall_support_sync(int max_chunks)
{
    int loaded = 0;

    if (max_chunks <= 0) return 0;

    for (int target_index = 0; target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);
        if (bootstrap_target_is_sync(target)) continue;
        if (!target_is_active_superchunk_wall_support(target)) continue;
        if (sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz)) continue;
        if (generate_or_load_chunk_sync(target->cx, target->cz, (SdkChunkResidencyRole)target->role)) {
            mark_chunk_stream_adjacent_neighbors_dirty(target->cx, target->cz);
            loaded++;
            if (loaded >= max_chunks) {
                return loaded;
            }
            continue;
        }
        break;
    }

    return loaded;
}

/* ============================================================================
 * Chunk Building and Uploading
 * ============================================================================ */

void build_and_upload_chunk_sync(SdkChunk* chunk)
{
    static SdkMeshBuffer mesh_buf;
    static bool init_mesh_buf = false;
    SdkResult upload_result;

    if (!chunk || !chunk->blocks) return;
    if (!init_mesh_buf) {
        sdk_mesh_buffer_init(&mesh_buf, MESH_BUFFER_INITIAL_VERTS);
        init_mesh_buf = true;
    }

    sdk_mesh_build_chunk(chunk, &g_sdk.chunk_mgr, &mesh_buf);
    
    /* Empty chunks have no geometry to upload - mark as fully uploaded */
    if (chunk->empty) {
        chunk->gpu_upload_mode = (uint8_t)SDK_CHUNK_GPU_UPLOAD_FULL;
        return;
    }
    
    upload_result = sdk_renderer_upload_chunk_mesh_unified(chunk);
    if (upload_result != SDK_OK) {
        char dbg[160];
        sprintf_s(dbg, sizeof(dbg),
                  "[UPLOAD] sync upload failed for chunk (%d,%d) result=%d\n",
                  chunk->cx, chunk->cz, (int)upload_result);
        OutputDebugStringA(dbg);
    }
}

/* ============================================================================
 * Streamed Chunk Results Processing
 * ============================================================================ */

void process_streamed_chunk_results_with_budget(int max_results, float budget_ms)
{
    LARGE_INTEGER start;
    LARGE_INTEGER now;
    LARGE_INTEGER freq;
    int enforce_budget = 1;
    int processed = 0;
    SdkChunkBuildResult result;
    size_t upload_budget_bytes = stream_upload_byte_budget_per_frame();
    size_t uploaded_bytes = 0u;

    if (budget_ms <= 0.0f) {
        enforce_budget = 0;
        memset(&freq, 0, sizeof(freq));
        memset(&start, 0, sizeof(start));
    } else {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }

    while (processed < max_results) {
        int pending_before = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
        size_t estimated_upload_bytes = 0u;

        if (processed > 0 && uploaded_bytes >= upload_budget_bytes) {
            break;
        }
        if (!sdk_chunk_streamer_pop_result(&g_sdk.chunk_streamer, &result)) {
            if (processed == 0 && pending_before > 0) {
                char dbg[128];
                sprintf_s(dbg, sizeof(dbg), "[PROCESS] No results popped despite %d pending\n", pending_before);
                OutputDebugStringA(dbg);
            }
            break;
        }
        {
            char dbg[192];
            sprintf_s(dbg, sizeof(dbg),
                      "[PROCESS] Popped result cx=%d cz=%d gen=%u space=%s type=%s built=%p\n",
                      result.cx,
                      result.cz,
                      result.generation,
                      sdk_coordinate_space_type_name((SdkCoordinateSpaceType)result.space_type),
                      result.type == SDK_CHUNK_STREAM_RESULT_REMESHED ? "REMESH" : "GENERATE",
                      (void*)result.built_chunk);
            OutputDebugStringA(dbg);
        }
        if (result.built_chunk) {
            const SdkChunk* built = result.built_chunk;
            uint64_t total_vertices = 0u;
            for (int i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
                total_vertices += built->subchunks[i].vertex_count;
                total_vertices += built->water_subchunks[i].vertex_count;
            }
            total_vertices += built->far_mesh.vertex_count;
            total_vertices += built->experimental_far_mesh.vertex_count;
            total_vertices += built->far_exact_overlay_mesh.vertex_count;
            estimated_upload_bytes = (size_t)(total_vertices * sizeof(BlockVertex));
        }

        if (result.built_chunk) {
            int adopted = 0;
            if (result.type == SDK_CHUNK_STREAM_RESULT_REMESHED) {
                adopted = adopt_streamed_remesh_result(&result);
            } else {
                adopted = adopt_streamed_chunk_result(&result);
            }
            {
                char dbg[192];
                sprintf_s(dbg, sizeof(dbg),
                          "[PROCESS] Adoption result: adopted=%d for cx=%d cz=%d space=%s\n",
                          adopted,
                          result.cx,
                          result.cz,
                          sdk_coordinate_space_type_name((SdkCoordinateSpaceType)result.space_type));
                OutputDebugStringA(dbg);
            }
            if (adopted) {
                result.built_chunk = NULL;
            }
        } else {
            char dbg[128];
            sprintf_s(dbg, sizeof(dbg), "[PROCESS] No built_chunk in result cx=%d cz=%d\n", result.cx, result.cz);
            OutputDebugStringA(dbg);
        }
        sdk_chunk_streamer_release_result(&result);
        processed++;
        uploaded_bytes += estimated_upload_bytes;

        if (enforce_budget) {
            QueryPerformanceCounter(&now);
            double elapsed_ms = ((double)(now.QuadPart - start.QuadPart) * 1000.0) / (double)freq.QuadPart;
            if (elapsed_ms >= (double)budget_ms) {
                break;
            }
        }
    }

    process_active_wall_finalization_sync(8);
}

/* ============================================================================
 * Dirty Chunk Management
 * ============================================================================ */

void rebuild_loaded_dirty_chunks_sync(int max_chunks)
{
    int rebuilt = 0;

    if (max_chunks <= 0 || !g_sdk.chunks_initialized) return;

    for (int slot_index = 0;
         slot_index < sdk_chunk_manager_slot_capacity() && rebuilt < max_chunks;
         ++slot_index) {
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(&g_sdk.chunk_mgr, slot_index);
        SdkChunk* chunk;

        if (!slot || !slot->occupied) continue;
        chunk = &slot->chunk;
        if (!sdk_chunk_needs_remesh(chunk)) continue;

        build_and_upload_chunk_sync(chunk);
        rebuilt++;
    }
}

void persist_loaded_chunks(void)
{
    if (!g_sdk.chunks_initialized) return;
    for (int slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(&g_sdk.chunk_mgr, slot_index);
        if (!slot || !slot->occupied || !slot->chunk.blocks) continue;
        if (sdk_superchunk_active_wall_chunk_contains_chunk(g_sdk.chunk_mgr.primary_scx,
                                                            g_sdk.chunk_mgr.primary_scz,
                                                            slot->chunk.cx,
                                                            slot->chunk.cz) &&
            slot->chunk.wall_finalized_generation != g_sdk.chunk_mgr.topology_generation) {
            continue;
        }
        sdk_persistence_store_chunk(&g_sdk.persistence, &slot->chunk);
    }
}

/* ============================================================================
 * Session Runtime State Reset
 * ============================================================================ */

void reset_session_runtime_state(void)
{
    sdk_settlement_runtime_init();
    g_startup_safe_frames_remaining = STARTUP_SAFE_MODE_FRAMES;
    g_startup_safe_mode_enabled = true;
    g_startup_bootstrap_complete = false;
    memset(&g_startup_chunk_readiness, 0, sizeof(g_startup_chunk_readiness));
    memset(&g_startup_bootstrap_completion_readiness, 0, sizeof(g_startup_bootstrap_completion_readiness));
    memset(&g_runtime_chunk_health, 0, sizeof(g_runtime_chunk_health));
    g_test_flight_enabled = false;
    g_space_was_down = false;
    g_space_tap_timer = 0;
    g_cam_rotation_initialized = false;
    g_cam_yaw = CAM_DEFAULT_YAW;
    g_cam_pitch = CAM_DEFAULT_PITCH;
    g_cam_look_dist = 1.0f;
    g_vel_y = 0.0f;
    g_on_ground = false;
    g_worldgen_debug_was_down = false;
    g_settlement_debug_was_down = false;
    g_settlement_debug_overlay = false;
    g_settlement_debug_cache_valid = false;
    g_settlement_debug_last_refresh_ms = 0u;
    g_settlement_debug_last_wx = 0;
    g_settlement_debug_last_wz = 0;
    memset(&g_settlement_debug_cached_ui, 0, sizeof(g_settlement_debug_cached_ui));
    g_fluid_debug_was_down = false;
    g_fluid_debug_overlay = false;
    g_perf_debug_was_down = false;
    g_perf_debug_overlay = false;
    g_w_was_down = false;
    g_w_tap_timer = 0;
    g_w_sprint_latched = false;
    g_is_sprinting = false;
    g_mounted_vehicle_index = -1;
    g_vehicle_use_was_down = false;
    g_weapon_use_cooldown = 0;
    g_screen_flash_timer = 0;
    g_screen_flash_duration = 0;
    g_screen_flash_strength = 0.0f;
    g_player_smoke_obscurance = 0.0f;
    memset(g_smoke_clouds, 0, sizeof(g_smoke_clouds));
    g_construction_place_rotation = 0;
    g_creative_shape_focus = 0;
    g_creative_shape_row = 0;
    g_creative_shape_width = 16;
    g_creative_shape_height = 16;
    g_creative_shape_depth = 16;
}

/* ============================================================================
 * Graphics Settings
 * ============================================================================ */

void rebuild_chunk_grid_for_current_camera(int new_grid_size)
{
    float cam_x, cam_y, cam_z;
    int cam_cx;
    int cam_cz;

    new_grid_size = sdk_chunk_manager_normalize_grid_size(new_grid_size);
    if (new_grid_size == g_chunk_grid_size_setting) {
        return;
    }

    g_chunk_grid_size_setting = new_grid_size;
    g_graphics_settings.chunk_grid_size = new_grid_size;

    if (g_sdk.world_session_active && g_sdk.chunks_initialized) {
        sdk_renderer_get_camera_pos(&cam_x, &cam_y, &cam_z);
        cam_cx = sdk_world_to_chunk_x((int)floorf(cam_x));
        cam_cz = sdk_world_to_chunk_z((int)floorf(cam_z));
        sdk_chunk_manager_set_grid_size(&g_sdk.chunk_mgr, new_grid_size);
        if (sdk_chunk_manager_update(&g_sdk.chunk_mgr, cam_cx, cam_cz)) {
            sdk_simulation_invalidate_reservoirs();
            evict_undesired_loaded_chunks();
            bootstrap_nearby_visible_chunks_sync();
            sdk_chunk_streamer_schedule_visible(&g_sdk.chunk_streamer, &g_sdk.chunk_mgr);
        }
    }

    save_graphics_settings_now();
}

void save_graphics_settings_now(void)
{
    sync_graphics_resolution_from_window();
    g_graphics_settings.chunk_grid_size = g_chunk_grid_size_setting;
    sdk_graphics_settings_save(&g_graphics_settings);
}

void tick_startup_safe_mode(void)
{
    SdkStartupChunkReadiness readiness;
    SdkRuntimeChunkHealth runtime_health;
    static uint64_t s_last_startup_trace_ms = 0u;
    int backlog_jobs_threshold;
    int backlog_results_threshold;

    if (!g_startup_safe_mode_enabled) return;

    collect_startup_chunk_readiness(&readiness);
    refresh_startup_safe_mode_status_text(&readiness);
    collect_runtime_chunk_health(&runtime_health);
    if (GetTickCount64() - s_last_startup_trace_ms >= 500u) {
        s_last_startup_trace_ms = GetTickCount64();
        if (g_startup_bootstrap_complete) {
            sdk_load_trace_note_readiness("runtime_grace_mode",
                                          runtime_health.desired_visible,
                                          runtime_health.resident_visible,
                                          runtime_health.gpu_ready_visible,
                                          runtime_health.pending_jobs,
                                          runtime_health.pending_results,
                                          0,
                                          g_world_generation_status);
        } else {
            sdk_load_trace_note_readiness("startup_safe_mode",
                                          readiness.desired_primary,
                                          readiness.resident_primary,
                                          readiness.gpu_ready_primary,
                                          readiness.pending_jobs,
                                          readiness.pending_results,
                                          readiness.upload_pending,
                                          g_world_generation_status);
        }
    }

    if (!g_startup_bootstrap_complete &&
        readiness.desired_primary > 0 &&
        readiness.gpu_ready_primary >= readiness.desired_primary) {
        latch_startup_bootstrap_completion(&readiness);
    }

    if (g_pause_menu_open || g_map_focus_open || g_command_open ||
        g_craft_open || g_station_open || g_skills_open) {
        return;
    }

    if (!g_startup_bootstrap_complete) {
        return;
    }

    backlog_jobs_threshold = max(8, runtime_health.active_workers * 2);
    backlog_results_threshold = max(4, runtime_health.active_workers);

    if (runtime_health.pending_jobs <= backlog_jobs_threshold &&
        runtime_health.pending_results <= backlog_results_threshold) {
        if (g_startup_safe_frames_remaining > 0) {
            g_startup_safe_frames_remaining--;
        }
    }
    if (g_startup_safe_frames_remaining <= 0) {
        g_startup_safe_mode_enabled = false;
        g_world_generation_status[0] = '\0';
    }
}

/* ============================================================================
 * World Session Lifecycle
 * ============================================================================ */

static void cleanup_failed_world_session_start(void)
{
    sdk_chunk_streamer_shutdown(&g_sdk.chunk_streamer);
    sdk_settlement_runtime_shutdown(&g_sdk.entities);
    sdk_simulation_shutdown();
    shutdown_superchunk_map_scheduler();

    if (g_sdk.chunks_initialized) {
        for (int slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
            SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(&g_sdk.chunk_mgr, slot_index);
            if (!slot || !slot->occupied) continue;
            sdk_renderer_free_chunk_mesh(&slot->chunk);
        }
        sdk_chunk_manager_shutdown(&g_sdk.chunk_mgr);
        g_sdk.chunks_initialized = false;
    }

    sdk_renderer_set_chunk_manager(NULL);
    sdk_worldgen_shutdown(&g_sdk.worldgen);
    sdk_persistence_shutdown(&g_sdk.persistence);
    sdk_world_config_shutdown();
    memset(&g_startup_chunk_readiness, 0, sizeof(g_startup_chunk_readiness));
    memset(&g_startup_bootstrap_completion_readiness, 0, sizeof(g_startup_bootstrap_completion_readiness));
    memset(&g_runtime_chunk_health, 0, sizeof(g_runtime_chunk_health));
    g_startup_bootstrap_complete = false;
    g_startup_safe_mode_enabled = false;
    g_startup_safe_frames_remaining = 0;
    g_sdk.disable_map_generation_in_gameplay = false;
    g_sdk.world_seed = 0u;
    g_sdk.world_session_active = false;
    g_sdk.world_save_id[0] = '\0';
    g_sdk.world_save_name[0] = '\0';
    sdk_server_runtime_on_world_session_stopped();
}

void shutdown_world_session(bool save_state)
{
    SdkPersistedState persisted_state;

    if (!g_sdk.world_session_active) return;

    if (editor_session_active()) {
        sdk_settlement_runtime_shutdown(&g_sdk.entities);
        if (save_state) {
            save_editor_session_assets();
        }
        sdk_chunk_streamer_shutdown(&g_sdk.chunk_streamer);
        shutdown_superchunk_map_scheduler();
        if (g_sdk.chunks_initialized) {
            sdk_renderer_set_chunk_manager(NULL);
            sdk_chunk_manager_shutdown(&g_sdk.chunk_mgr);
            g_sdk.chunks_initialized = false;
        }
        sdk_renderer_set_chunk_manager(NULL);
        memset(&g_editor_session, 0, sizeof(g_editor_session));
        g_session_kind = SDK_SESSION_KIND_WORLD;
        g_sdk.world_seed = 0u;
        g_sdk.world_session_active = false;
        g_sdk.world_save_id[0] = '\0';
        g_sdk.world_save_name[0] = '\0';
        sdk_server_runtime_on_world_session_stopped();
        return;
    }

    sdk_chunk_streamer_shutdown(&g_sdk.chunk_streamer);
    sdk_settlement_runtime_shutdown(&g_sdk.entities);
    sdk_simulation_shutdown();
    shutdown_superchunk_map_scheduler();

    if (g_sdk.chunks_initialized) {
        persist_loaded_chunks();
    }
    if (save_state) {
        capture_persisted_state(&persisted_state);
        sdk_persistence_set_state(&g_sdk.persistence, &persisted_state);
        sdk_persistence_clear_station_states(&g_sdk.persistence);
        for (int i = 0; i < g_station_state_count; ++i) {
            if (station_state_is_meaningful(&g_station_states[i])) {
                station_sync_to_persistence(i);
            }
        }
        sdk_persistence_save(&g_sdk.persistence);
    }

    if (g_sdk.chunks_initialized) {
        for (int slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
            SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(&g_sdk.chunk_mgr, slot_index);
            if (!slot || !slot->occupied) continue;
            sdk_renderer_free_chunk_mesh(&slot->chunk);
        }
        sdk_chunk_manager_shutdown(&g_sdk.chunk_mgr);
        g_sdk.chunks_initialized = false;
    }

    sdk_worldgen_shutdown(&g_sdk.worldgen);
    sdk_persistence_shutdown(&g_sdk.persistence);
    sdk_world_config_shutdown();
    sdk_renderer_set_chunk_manager(NULL);
    g_sdk.disable_map_generation_in_gameplay = false;
    g_sdk.world_seed = 0u;
    g_sdk.world_session_active = false;
    g_sdk.world_save_id[0] = '\0';
    g_sdk.world_save_name[0] = '\0';
    sdk_server_runtime_on_world_session_stopped();
}

void begin_async_return_to_start(void)
{
    if (!g_sdk.world_session_active) {
        frontend_open_main_menu();
        return;
    }

    g_pause_menu_open = false;
    g_pause_menu_view = SDK_PAUSE_MENU_VIEW_MAIN;
    g_pause_menu_selected = 0;
    memset(g_pause_menu_nav_was_down, 0, sizeof(g_pause_menu_nav_was_down));
    g_craft_open = false;
    g_skills_open = false;
    g_map_focus_open = false;
    station_close_ui();
    command_close();

    g_frontend_view = SDK_START_MENU_VIEW_RETURNING_TO_START;
    g_world_generation_active = false;
    g_world_generation_progress = 0.04f;
    g_world_generation_stage = 0;
    reset_async_world_release_state();
    if (editor_session_active()) {
        strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
                 "Preparing editor save...");
    } else {
        strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
                 "Preparing return to start menu...");
    }
    frontend_reset_nav_state();
    sdk_window_clear_char_queue(g_sdk.window);
}

static void present_async_return_status(float progress, const char* status)
{
    g_world_generation_progress = progress;
    if (status) {
        strncpy_s(g_world_generation_status, sizeof(g_world_generation_status), status, _TRUNCATE);
    } else {
        g_world_generation_status[0] = '\0';
    }
}


/* ============================================================================
 * Async Return to Start - State Machine Update
 * ============================================================================ */

void update_async_return_to_start(void)
{
    SdkPersistedState persisted_state;

    if (g_frontend_view != SDK_START_MENU_VIEW_RETURNING_TO_START) return;

    if (!g_sdk.world_session_active) {
        g_world_generation_progress = 0.0f;
        g_world_generation_stage = 0;
        g_world_generation_status[0] = '\0';
        g_frontend_refresh_pending = true;
        frontend_open_main_menu();
        return;
    }

    if (editor_session_active()) {
        SdkSessionKind previous_kind = g_session_kind;
        char character_id[64];
        char animation_id[64];

        strcpy_s(character_id, sizeof(character_id), g_editor_session.character_id);
        strcpy_s(animation_id, sizeof(animation_id), g_editor_session.animation_id);
        switch (g_world_generation_stage) {
            case 0:
                present_async_return_status(
                    0.34f,
                    (g_session_kind == SDK_SESSION_KIND_ANIMATION_EDITOR)
                        ? "Saving animation frames..."
                        : (g_session_kind == SDK_SESSION_KIND_PROP_EDITOR)
                            ? "Saving prop chunks..."
                        : (g_session_kind == SDK_SESSION_KIND_BLOCK_EDITOR)
                            ? "Saving block asset..."
                            : (g_session_kind == SDK_SESSION_KIND_ITEM_EDITOR)
                                ? "Saving item asset..."
                                : (g_session_kind == SDK_SESSION_KIND_PARTICLE_EDITOR)
                                    ? "Saving particle effect..."
                                    : "Saving character model...");
                save_editor_session_assets();
                g_world_generation_stage = 1;
                break;

            case 1:
            {
                float release_progress = 1.0f;
                int geometry_done = release_world_geometry_step(4, &release_progress);
                float progress = 0.72f + (0.20f * release_progress);
                present_async_return_status(progress, "Releasing editor geometry...");
                if (!geometry_done) {
                    break;
                }
            }
                sdk_renderer_set_chunk_manager(NULL);
                clear_non_frontend_ui();
                g_world_generation_stage = 2;
                break;

            case 2:
            default:
                present_async_return_status(
                    0.96f,
                    (previous_kind == SDK_SESSION_KIND_ANIMATION_EDITOR)
                        ? "Returning to saved animations..."
                        : (previous_kind == SDK_SESSION_KIND_PROP_EDITOR)
                            ? "Returning to saved props..."
                        : (previous_kind == SDK_SESSION_KIND_BLOCK_EDITOR)
                            ? "Returning to saved blocks..."
                            : (previous_kind == SDK_SESSION_KIND_ITEM_EDITOR)
                                ? "Returning to saved items..."
                                : (previous_kind == SDK_SESSION_KIND_PARTICLE_EDITOR)
                                    ? "Returning to saved particle effects..."
                                    : "Returning to saved characters...");
                memset(&g_editor_session, 0, sizeof(g_editor_session));
                g_session_kind = SDK_SESSION_KIND_WORLD;
                g_sdk.world_seed = 0u;
                g_sdk.world_session_active = false;
                g_sdk.world_save_id[0] = '\0';
                g_sdk.world_save_name[0] = '\0';
                sdk_server_runtime_on_world_session_stopped();
                g_world_generation_stage = 0;
                g_world_generation_status[0] = '\0';
                g_frontend_refresh_pending = true;
                finish_editor_return_to_frontend(previous_kind, character_id, animation_id);
                break;
        }
        return;
    }

    switch (g_world_generation_stage) {
        case 0:
            present_async_return_status(0.10f, "Stopping background systems...");
            sdk_chunk_streamer_begin_shutdown(&g_sdk.chunk_streamer);
            sdk_simulation_begin_shutdown();
            request_shutdown_superchunk_map_scheduler();
            g_world_generation_stage = 1;
            break;

        case 1:
        {
            int streamer_done = sdk_chunk_streamer_poll_shutdown(&g_sdk.chunk_streamer);
            int simulation_done = sdk_simulation_poll_shutdown();
            int map_done = poll_shutdown_superchunk_map_scheduler();
            int completed = (streamer_done ? 1 : 0) + (simulation_done ? 1 : 0) + (map_done ? 1 : 0);

            float progress = 0.10f + (0.20f * ((float)completed / 3.0f));

            if (!streamer_done) {
                present_async_return_status(progress, "Stopping chunk streamer...");
                break;
            }
            if (!simulation_done) {
                present_async_return_status(progress, "Stopping fluid simulation...");
                break;
            }
            if (!map_done) {
                present_async_return_status(progress, "Stopping map tile workers...");
                break;
            }

            present_async_return_status(0.30f, "Background systems stopped");
            g_world_generation_stage = 2;
            break;
        }

        case 2:
            present_async_return_status(0.36f, "Saving loaded chunks...");
            if (g_sdk.chunks_initialized) {
                persist_loaded_chunks();
            }
            g_world_generation_stage = 3;
            break;

        case 3:
            present_async_return_status(0.62f, "Saving player and station state...");
            capture_persisted_state(&persisted_state);
            sdk_persistence_set_state(&g_sdk.persistence, &persisted_state);
            sdk_persistence_clear_station_states(&g_sdk.persistence);
            for (int i = 0; i < g_station_state_count; ++i) {
                if (station_state_is_meaningful(&g_station_states[i])) {
                    station_sync_to_persistence(i);
                }
            }
            sdk_persistence_save(&g_sdk.persistence);
            g_world_generation_stage = 4;
            break;

        case 4:
        {
            float release_progress = 1.0f;
            int geometry_done = release_world_geometry_step(12, &release_progress);
            float progress = 0.84f + (0.10f * release_progress);
            present_async_return_status(progress, "Releasing world geometry...");
            if (!geometry_done) {
                break;
            }
        }
            sdk_renderer_set_chunk_manager(NULL);
            clear_non_frontend_ui();
            g_world_generation_stage = 5;
            break;

        case 5:
        default:
            present_async_return_status(0.96f, "Returning to start menu...");
            shutdown_world_session(false);
            g_world_generation_stage = 0;
            g_world_generation_status[0] = '\0';
            g_frontend_refresh_pending = true;
            frontend_open_main_menu();
            break;
    }
}

/* ============================================================================
 * Start World Session
 * ============================================================================ */

int start_world_session(const SdkWorldSaveMeta* selected_world)
{
    SdkWorldDesc world_desc;
    SdkPersistedState persisted_state;
    int have_persisted_state;

    if (!selected_world) return 0;
    g_world_generation_cancel_requested = false;
    sdk_load_trace_bind_meta(selected_world);
    sdk_load_trace_note("start_world_session_enter", selected_world->folder_id);
    if (g_sdk.world_session_active) {
        shutdown_world_session(true);
    }

    memset(&world_desc, 0, sizeof(world_desc));
    memset(&g_editor_session, 0, sizeof(g_editor_session));
    g_session_kind = SDK_SESSION_KIND_WORLD;
    sdk_world_meta_to_world_desc(selected_world, &world_desc);

    gen_stage_begin("Opening world save");
    world_generation_session_step(0.06f, "Opening world save...", 1);
    sdk_persistence_init(&g_sdk.persistence, &world_desc, selected_world->save_path);
    sdk_persistence_get_world_desc(&g_sdk.persistence, &world_desc);

    /* Apply world metadata before worldgen init so new sessions do not inherit stale defaults. */
    {
        /* Initialize world config - this now handles superchunk and wall config */
        sdk_world_config_init(selected_world);
    }

    g_sdk.worldgen.impl = NULL;
    gen_stage_end(0, 0);

    gen_stage_begin("Initializing world generator");
    world_generation_session_step(0.14f, "Initializing world generator...", 1);
    sdk_worldgen_init(&g_sdk.worldgen, &world_desc);

    if (!g_sdk.worldgen.impl) {
        sdk_load_trace_note("start_world_session_worldgen_init_failed", selected_world->folder_id);
        world_generation_session_step(0.14f, "World generator init failed", 1);
        gen_stage_end(0, 0);
        cleanup_failed_world_session_start();
        return 0;
    }
    sdk_settlement_set_world_path(&g_sdk.worldgen, sdk_persistence_get_save_path(&g_sdk.persistence));
    gen_stage_end(0, 0);

    gen_stage_begin("Preparing world systems");
    world_generation_session_step(0.22f, "Preparing world systems...", 1);
    g_sdk.desc.world = g_sdk.worldgen.desc;
    sdk_persistence_set_world_desc(&g_sdk.persistence, &g_sdk.worldgen.desc);
    g_sdk.world_seed = g_sdk.worldgen.desc.seed;
    strcpy_s(g_sdk.world_save_id, sizeof(g_sdk.world_save_id), selected_world->folder_id);
    strcpy_s(g_sdk.world_save_name, sizeof(g_sdk.world_save_name), selected_world->display_name);
    sync_active_world_meta(g_sdk.world_seed);

    if (g_map_scheduler.initialized) {
        shutdown_superchunk_map_scheduler();
    }
    {
        SdkMapSchedulerConfig map_config;
        memset(&map_config, 0, sizeof(map_config));
        map_config.world_desc = g_sdk.worldgen.desc;
        map_config.world_seed = g_sdk.worldgen.desc.seed;
        map_config.worker_count = choose_map_worker_count();
        map_config.mode = SDK_MAP_SCHED_MODE_INTERACTIVE;
        map_config.build_kind = SDK_MAP_BUILD_INTERACTIVE_FALLBACK;
        strcpy_s(map_config.world_save_id, sizeof(map_config.world_save_id), selected_world->folder_id);
        if (!init_superchunk_map_scheduler(&map_config)) {
            sdk_load_trace_note("start_world_session_map_scheduler_failed", selected_world->folder_id);
            world_generation_session_step(0.22f, "Map scheduler init failed", 1);
            gen_stage_end(0, 0);
            cleanup_failed_world_session_start();
            return 0;
        }
    }
    reset_session_runtime_state();
    skills_reset_progression();
    station_close_ui();
    gen_stage_end(0, 0);

    gen_stage_begin("Loading station state");
    world_generation_session_step(0.30f, "Loading station state...", 1);
    station_load_all_from_persistence();
    gen_stage_end(0, 0);

    gen_stage_begin("Preparing chunk systems");
    world_generation_session_step(0.38f, "Preparing chunk systems...", 1);
    sdk_chunk_manager_init(&g_sdk.chunk_mgr);
    g_sdk.chunks_initialized = true;
    sdk_chunk_manager_set_background_expansion(&g_sdk.chunk_mgr, false);
    if (!sdk_persistence_bind_construction_registry(&g_sdk.persistence, g_sdk.chunk_mgr.construction_registry)) {
        sdk_load_trace_note("start_world_session_construction_registry_failed", selected_world->folder_id);
        world_generation_session_step(0.38f, "Construction registry load failed", 1);
        cleanup_failed_world_session_start();
        return 0;
    }
    g_sdk.disable_map_generation_in_gameplay = true;
    sdk_chunk_streamer_init(&g_sdk.chunk_streamer, &g_sdk.worldgen.desc, &g_sdk.persistence);
    sdk_worldgen_set_debug_mode_ctx(&g_sdk.worldgen, sdk_worldgen_get_debug_mode_ctx(&g_sdk.worldgen));
    sdk_renderer_set_chunk_manager(&g_sdk.chunk_mgr);
    gen_stage_end(0, 0);

    gen_stage_begin("Initializing entities");
    world_generation_session_step(0.46f, "Initializing entities...", 1);
    sdk_entity_init(&g_sdk.entities);
    update_window_title_for_test_flight();
    gen_stage_end(0, 0);

    gen_stage_begin("Restoring world state");
    world_generation_session_step(0.54f, "Restoring world state...", 1);
    have_persisted_state = sdk_persistence_get_state(&g_sdk.persistence, &persisted_state);
    if (have_persisted_state) {
        apply_persisted_state(&persisted_state);
        g_sdk.chunk_mgr.cam_cx = sdk_world_to_chunk_x((int)floorf(persisted_state.position[0]));
        g_sdk.chunk_mgr.cam_cz = sdk_world_to_chunk_z((int)floorf(persisted_state.position[2]));
    } else {
        g_chunk_grid_size_setting = sdk_chunk_manager_grid_size_from_radius(
            current_world_render_distance_chunks(selected_world));
        switch (current_new_world_spawn_mode(selected_world)) {
            case 0:
                world_generation_session_step(0.54f, "Choosing random spawn...", 1);
                choose_random_spawn_fast(&g_sdk.worldgen);
                break;
            case 1:
                world_generation_session_step(0.54f, "Choosing center spawn...", 1);
                choose_center_spawn(&g_sdk.worldgen);
                break;
            case 2:
            default:
                world_generation_session_step(0.54f, "Choosing safe spawn...", 1);
                choose_safe_spawn(&g_sdk.worldgen);
                break;
        }
        g_sdk.chunk_mgr.cam_cx = sdk_world_to_chunk_x((int)floorf(g_spawn_x));
        g_sdk.chunk_mgr.cam_cz = sdk_world_to_chunk_z((int)floorf(g_spawn_z));
    }
    rebuild_selected_gameplay_character_mesh();
    g_pause_character_selected = (g_selected_character_index >= 0) ? g_selected_character_index : 0;
    g_pause_character_scroll = 0;
    gen_stage_end(0, 0);

    gen_stage_begin("Planning visible chunks");
    world_generation_session_step(0.64f, "Planning visible chunks...", 1);
    sdk_chunk_manager_set_grid_size(&g_sdk.chunk_mgr, g_chunk_grid_size_setting);
    sdk_chunk_manager_update(&g_sdk.chunk_mgr, g_sdk.chunk_mgr.cam_cx, g_sdk.chunk_mgr.cam_cz);

    if (!have_persisted_state) {
        float look_target_y;
        sdk_renderer_set_camera_pos(g_spawn_x, g_spawn_y, g_spawn_z);
        look_target_y = g_spawn_y + 50.0f;
        sdk_renderer_set_camera_target(g_spawn_x, look_target_y, g_spawn_z + 10.0f);
    }
    gen_stage_end(0, 0);

    gen_stage_begin("Loading nearby chunks");
    world_generation_session_step(0.66f, "Loading nearby chunks...", 1);
    if (!bootstrap_visible_chunks_sync()) {
        if (g_world_generation_cancel_requested) {
            sdk_load_trace_note("start_world_session_cancelled", selected_world->folder_id);
        } else {
            sdk_load_trace_note("start_world_session_bootstrap_failed", selected_world->folder_id);
        }
        cleanup_failed_world_session_start();
        return 0;
    }
    if (g_world_generation_cancel_requested) {
        sdk_load_trace_note("start_world_session_cancelled", selected_world->folder_id);
        cleanup_failed_world_session_start();
        return 0;
    }
    {
        SdkStartupChunkReadiness readiness;
        collect_startup_chunk_readiness(&readiness);
        latch_startup_bootstrap_completion(&readiness);
    }
    {
        int chunks_loaded = sdk_chunk_manager_active_count(&g_sdk.chunk_mgr);
        gen_stage_end(chunks_loaded, 0);
    }
    world_generation_session_step(1.0f, "World session ready", 1);
    dismiss_world_generation_summary();
    g_sdk.world_session_active = true;
    sdk_load_trace_note("start_world_session_ready", selected_world->folder_id);
    return 1;
}

