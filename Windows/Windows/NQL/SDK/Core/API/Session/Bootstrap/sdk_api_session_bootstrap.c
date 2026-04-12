#include "sdk_api_session_internal.h"
#include "sdk_api_session_bootstrap_policy.h"

#define SDK_BOOTSTRAP_STALL_TIMEOUT_MS 15000ULL

static int bootstrap_readiness_changed(const SdkStartupChunkReadiness* lhs,
                                       const SdkStartupChunkReadiness* rhs)
{
    if (!lhs || !rhs) return 1;
    return lhs->desired_primary != rhs->desired_primary ||
           lhs->resident_primary != rhs->resident_primary ||
           lhs->gpu_ready_primary != rhs->gpu_ready_primary ||
           lhs->active_workers != rhs->active_workers ||
           lhs->pending_jobs != rhs->pending_jobs ||
           lhs->pending_results != rhs->pending_results ||
           lhs->no_cpu_mesh != rhs->no_cpu_mesh ||
           lhs->upload_pending != rhs->upload_pending ||
           lhs->far_only_when_full_needed != rhs->far_only_when_full_needed ||
           lhs->gpu_mesh_generation_stale != rhs->gpu_mesh_generation_stale ||
           lhs->other_not_ready != rhs->other_not_ready;
}

static int bootstrap_fail_stalled(const char* trace_key,
                                  const char* phase_label,
                                  const SdkStartupChunkReadiness* readiness)
{
    char trace_note[256];
    char status[160];

    sprintf_s(trace_note, sizeof(trace_note),
              "%s desired=%d resident=%d gpu_ready=%d active_workers=%d pending_jobs=%d pending_results=%d no_cpu=%d upload_pending=%d far_only=%d gpu_stale=%d other=%d",
              phase_label ? phase_label : "bootstrap",
              readiness ? readiness->desired_primary : 0,
              readiness ? readiness->resident_primary : 0,
              readiness ? readiness->gpu_ready_primary : 0,
              readiness ? readiness->active_workers : 0,
              readiness ? readiness->pending_jobs : 0,
              readiness ? readiness->pending_results : 0,
              readiness ? readiness->no_cpu_mesh : 0,
              readiness ? readiness->upload_pending : 0,
              readiness ? readiness->far_only_when_full_needed : 0,
              readiness ? readiness->gpu_mesh_generation_stale : 0,
              readiness ? readiness->other_not_ready : 0);
    sdk_load_trace_note(trace_key ? trace_key : "bootstrap_stalled", trace_note);

    sprintf_s(status, sizeof(status),
              "World startup stalled during %s.",
              phase_label ? phase_label : "chunk bootstrap");
    world_generation_session_step(0.66f, status, 1);
    return 0;
}

static int bootstrap_chunk_has_gpu_ready_buffers(const SdkChunk* chunk)
{
    int sub_index;

    if (!chunk || !chunk->blocks) return 0;
    if (sdk_chunk_has_current_unified_gpu_mesh(chunk) && chunk->unified_total_vertices > 0) {
        return 1;
    }
    for (sub_index = 0; sub_index < CHUNK_SUBCHUNK_COUNT; ++sub_index) {
        if (chunk->subchunks[sub_index].vertex_buffer &&
            chunk->subchunks[sub_index].vertex_count > 0) {
            return 1;
        }
        if (chunk->water_subchunks[sub_index].vertex_buffer &&
            chunk->water_subchunks[sub_index].vertex_count > 0) {
            return 1;
        }
    }
    if (chunk->far_mesh.vertex_buffer && chunk->far_mesh.vertex_count > 0) return 1;
    if (chunk->experimental_far_mesh.vertex_buffer && chunk->experimental_far_mesh.vertex_count > 0) return 1;
    if (chunk->far_exact_overlay_mesh.vertex_buffer && chunk->far_exact_overlay_mesh.vertex_count > 0) return 1;
    return chunk->empty && !chunk->upload_pending && !sdk_chunk_needs_remesh(chunk);
}

/* ============================================================================
 * Bootstrap - Active Wall Stage Loading
 * ============================================================================ */

/**
 * Synchronously loads missing chunks for active wall stage, returns count loaded
 */
int load_missing_active_wall_stage_sync(SdkActiveWallStage stage, int max_chunks)
{
    int loaded = 0;

    if (max_chunks <= 0) return 0;

    for (int target_index = 0;
         target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr) && loaded < max_chunks;
         ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);

        if (!target_matches_active_wall_stage(target, stage)) continue;
        if (sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz)) continue;
        if (!generate_or_load_chunk_sync(target->cx, target->cz, (SdkChunkResidencyRole)target->role)) {
            break;
        }
        mark_chunk_stream_adjacent_neighbors_dirty(target->cx, target->cz);
        loaded++;
    }

    return loaded;
}

/* ============================================================================
 * Bootstrap - Visible Chunks Sync (Main World Bootstrap)
 * ============================================================================ */

int bootstrap_visible_chunks_sync(void)
{
    /* Bootstrap: synchronously loads visible chunks for world startup */
    int sync_target_total = 0;
    SdkStartupChunkReadiness readiness;
    SdkStartupChunkReadiness last_progress_readiness;
    ULONGLONG last_progress_ms = GetTickCount64();

    memset(&last_progress_readiness, 0xff, sizeof(last_progress_readiness));

    for (int target_index = 0; target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);
        if (bootstrap_target_is_sync(target)) {
            sync_target_total++;
        }
    }
    {
        char dbg[128];
        sprintf_s(dbg, sizeof(dbg), "[BOOTSTRAP] sync_target_total=%d safety_radius=%d\n",
            sync_target_total, startup_safe_primary_radius());
        sdk_debug_log_output(dbg);
    }
    if (sync_target_total > 0) {
        char status[96];
        sprintf_s(status, sizeof(status), "Loading nearby terrain %d/0/0", sync_target_total);
        world_generation_session_step(0.66f, status, 1);
    }

    sdk_chunk_streamer_schedule_phase(&g_sdk.chunk_streamer,
                                      &g_sdk.chunk_mgr,
                                      SDK_CHUNK_STREAM_SCHEDULE_BOOTSTRAP_SYNC,
                                      startup_safe_primary_radius(),
                                      max(4, sync_target_total + 2));

    collect_startup_chunk_readiness(&readiness);
    last_progress_readiness = readiness;
    while (!g_world_generation_cancel_requested &&
           readiness.resident_primary < sync_target_total) {
        process_streamed_chunk_results_with_budget(512, 0.0f);
        collect_startup_chunk_readiness(&readiness);

        if (bootstrap_readiness_changed(&last_progress_readiness, &readiness)) {
            last_progress_readiness = readiness;
            last_progress_ms = GetTickCount64();
        } else if (readiness.active_workers > 0) {
            last_progress_ms = GetTickCount64();
        } else if ((GetTickCount64() - last_progress_ms) >= SDK_BOOTSTRAP_STALL_TIMEOUT_MS) {
            return bootstrap_fail_stalled("bootstrap_startup_resident_stalled",
                                          "nearby terrain residency",
                                          &readiness);
        }

        if (sync_target_total > 0) {
            char status[96];
            float progress = 0.66f + 0.16f *
                ((float)readiness.resident_primary / (float)sync_target_total);
            sprintf_s(status, sizeof(status), "Loading nearby terrain %d/%d/%d W%d",
                      sync_target_total,
                      readiness.resident_primary,
                      readiness.gpu_ready_primary,
                      readiness.active_workers);
            world_generation_session_step(progress, status, 1);
        }

        if (readiness.resident_primary < sync_target_total) {
            Sleep(1);
        }
    }

    if (g_world_generation_cancel_requested) {
        return 0;
    }

    collect_startup_chunk_readiness(&readiness);
    last_progress_readiness = readiness;
    last_progress_ms = GetTickCount64();
    while (!g_world_generation_cancel_requested &&
           readiness.desired_primary > 0 &&
           readiness.gpu_ready_primary < readiness.desired_primary) {
        process_streamed_chunk_results_with_budget(16, 0.75f);
        sync_upload_startup_primary_chunks(max(4, readiness.desired_primary));
        process_pending_chunk_gpu_uploads(max(4, readiness.desired_primary), 4.0f);
        collect_startup_chunk_readiness(&readiness);

        if (bootstrap_readiness_changed(&last_progress_readiness, &readiness)) {
            last_progress_readiness = readiness;
            last_progress_ms = GetTickCount64();
        } else if (readiness.active_workers > 0) {
            last_progress_ms = GetTickCount64();
        } else if ((GetTickCount64() - last_progress_ms) >= SDK_BOOTSTRAP_STALL_TIMEOUT_MS) {
            return bootstrap_fail_stalled("bootstrap_startup_gpu_stalled",
                                          "nearby terrain GPU upload",
                                          &readiness);
        }

        {
            char status[96];
            float progress = 0.82f + 0.14f *
                ((float)readiness.gpu_ready_primary / (float)readiness.desired_primary);
            sprintf_s(status, sizeof(status), "Loading nearby terrain %d/%d/%d W%d",
                      readiness.desired_primary,
                      readiness.resident_primary,
                      readiness.gpu_ready_primary,
                      readiness.active_workers);
            world_generation_session_step(progress, status, 1);
        }

        if (readiness.gpu_ready_primary < readiness.desired_primary) {
            Sleep(1);
        }
    }

    if (g_world_generation_cancel_requested) {
        return 0;
    }

    persist_loaded_chunks();
    world_generation_session_step(0.97f,
                                  "Nearby terrain ready. Background streaming continues...",
                                  1);
    world_generation_session_step(0.98f, "Finalizing world session...", 1);
    process_streamed_chunk_results_with_budget(64, 0.0f);
    process_pending_chunk_gpu_uploads(max(4, sync_target_total), 2.0f);
    evict_undesired_loaded_chunks();
    return 1;
}

/* ============================================================================
 * Bootstrap - Nearby Visible Chunks Sync (Simplified for editor sessions)
 * ============================================================================ */

void bootstrap_nearby_visible_chunks_sync(void)
{
    /* Bootstrap: synchronously loads nearby visible chunks (editor sessions) */
    int synced = 0;

    if (!g_sdk.chunks_initialized) return;

    for (int target_index = 0; target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);
        SdkChunk* chunk;

        if (!sdk_bootstrap_target_is_sync_safety(target,
                                                 g_sdk.chunk_mgr.cam_cx,
                                                 g_sdk.chunk_mgr.cam_cz,
                                                 initial_sync_bootstrap_distance())) {
            continue;
        }
        chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz);
        if (chunk && chunk->blocks) continue;

        chunk = generate_or_load_chunk_sync(target->cx, target->cz, (SdkChunkResidencyRole)target->role);
        if (!chunk) continue;

        if ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_PRIMARY ||
            (SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_WALL_SUPPORT) {
            build_and_upload_chunk_sync(chunk);
        }

        mark_chunk_stream_adjacent_neighbors_dirty(target->cx, target->cz);
        synced++;
    }
}

/* ============================================================================
 * Bootstrap - Chunk Eviction
 * ============================================================================ */

/**
 * Evicts loaded chunks that are no longer in desired set
 */
void evict_undesired_loaded_chunks(void)
{
    int slot_index;

    if (!g_sdk.chunks_initialized) return;

    for (slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(&g_sdk.chunk_mgr, slot_index);
        if (!slot || !slot->occupied || slot->desired) continue;
        if (slot->chunk.blocks) {
            sdk_renderer_free_chunk_mesh(&slot->chunk);
        }
        sdk_chunk_manager_release_slot(&g_sdk.chunk_mgr, slot);
    }
}

/* ============================================================================
 * Bootstrap Static Helper Functions (from original sdk_api_session.c)
 * ============================================================================ */

/**
 * Returns true if chunk target should be loaded synchronously
 */
static int bootstrap_target_is_sync(const SdkChunkResidencyTarget* target)
{
    return sdk_bootstrap_target_is_sync_safety(target,
                                               g_sdk.chunk_mgr.cam_cx,
                                               g_sdk.chunk_mgr.cam_cz,
                                               initial_sync_bootstrap_distance());
}

/**
 * Returns true if wall bootstrap mode is active (large render distance)
 */
static int wall_bootstrap_mode_active(void)
{
    return sdk_chunk_manager_radius_from_grid_size(sdk_chunk_manager_grid_size(&g_sdk.chunk_mgr)) >=
           SDK_SUPERCHUNK_CHUNK_SPAN;
}

int target_matches_active_wall_stage(const SdkChunkResidencyTarget* target, SdkActiveWallStage stage)
{
    /* Returns true if chunk target matches given active wall stage */
    if (!target) return 0;
    return sdk_superchunk_active_wall_stage_for_chunk(g_sdk.chunk_mgr.primary_scx,
                                                      g_sdk.chunk_mgr.primary_scz,
                                                      target->cx,
                                                      target->cz) == stage;
}

int active_wall_stage_total(SdkActiveWallStage stage)
{
    /* Counts total chunks in given active wall stage */
    int total = 0;

    for (int target_index = 0; target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);
        if (target_matches_active_wall_stage(target, stage)) {
            total++;
        }
    }

    return total;
}

int active_wall_stage_ready_count(SdkActiveWallStage stage)
{
    /* Counts ready chunks in given active wall stage */
    int ready = 0;

    for (int target_index = 0; target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);
        SdkChunk* chunk;

        if (!target_matches_active_wall_stage(target, stage)) continue;
        chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz);
        if (!chunk) continue;

        if (stage == SDK_ACTIVE_WALL_STAGE_EDGE || stage == SDK_ACTIVE_WALL_STAGE_CORNER) {
            if (sdk_chunk_is_active_wall_chunk_fully_ready(&g_sdk.chunk_mgr, chunk)) {
                ready++;
            }
        } else if (bootstrap_chunk_has_gpu_ready_buffers(chunk)) {
            ready++;
        }
    }

    return ready;
}

int target_is_active_superchunk_wall_support(const SdkChunkResidencyTarget* target)
{
    /* Returns true if target is active superchunk wall support chunk */
    return target_matches_active_wall_stage(target, SDK_ACTIVE_WALL_STAGE_SUPPORT);
}

/* ============================================================================
 * Chunk Result Processing Functions
 * ============================================================================ */

int adopt_streamed_chunk_result(const SdkChunkBuildResult* result)
{
    /* Adopts streamed chunk build result into chunk manager */
    static ULONGLONG s_last_stream_drop_log_ms = 0u;
    SdkChunk* slot;
    SdkChunkResidencyRole role;
    uint32_t expected_generation = 0u;
    SdkChunkResidentSlot* existing;

    if (!result || !result->built_chunk) return 0;
    if (!sdk_chunk_manager_is_desired(&g_sdk.chunk_mgr, result->cx, result->cz, &role, &expected_generation)) {
        ULONGLONG now = GetTickCount64();
        if (now - s_last_stream_drop_log_ms >= 500u) {
            char dbg[256];
            s_last_stream_drop_log_ms = now;
            sprintf_s(dbg, sizeof(dbg),
                      "[STREAM] drop undesired chunk (%d,%d) gen=%u jobs=%d results=%d desired=%d active=%d\n",
                      result->cx, result->cz, result->generation,
                      sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer),
                      sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer),
                      sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr),
                      sdk_chunk_manager_active_count(&g_sdk.chunk_mgr));
            sdk_debug_log_output(dbg);
        }
        return 0;
    }
    if (expected_generation != result->generation) {
        ULONGLONG now = GetTickCount64();
        if (now - s_last_stream_drop_log_ms >= 500u) {
            char dbg[256];
            s_last_stream_drop_log_ms = now;
            sprintf_s(dbg, sizeof(dbg),
                      "[STREAM] drop stale chunk (%d,%d) got_gen=%u want_gen=%u role=%d\n",
                      result->cx, result->cz, result->generation, expected_generation, (int)role);
            sdk_debug_log_output(dbg);
        }
        return 0;
    }

    existing = sdk_chunk_manager_find_slot(&g_sdk.chunk_mgr, result->cx, result->cz);
    if (existing && existing->chunk.blocks) {
        existing->role = (uint8_t)role;
        existing->desired = 1u;
        existing->desired_role = (uint8_t)role;
        refresh_chunk_wall_finalization_state(&existing->chunk);
        return 0;
    }

    {
        char dbg[128];
        sprintf_s(dbg, sizeof(dbg), "[ADOPT] Adopting chunk (%d,%d) gen=%u role=%d\n",
            result->cx, result->cz, result->generation, (int)role);
        sdk_debug_log_output(dbg);
    }

    slot = sdk_chunk_manager_adopt_built_chunk(&g_sdk.chunk_mgr, result->built_chunk, role);
    if (!slot) {
        char dbg[128];
        sprintf_s(dbg, sizeof(dbg), "[ADOPT] FAILED to adopt chunk (%d,%d) - no slot available\n",
            result->cx, result->cz);
        sdk_debug_log_output(dbg);
        return 0;
    }

    if (!result->loaded_from_persistence) {
        resolve_loaded_chunk_boundary_water(slot);
    }
    refresh_chunk_wall_finalization_state(slot);
    sdk_simulation_on_chunk_loaded(&g_sdk.chunk_mgr, slot);
    mark_chunk_stream_adjacent_neighbors_dirty(result->cx, result->cz);
    {
        char dbg[128];
        sprintf_s(dbg, sizeof(dbg), "[ADOPT] SUCCESS chunk (%d,%d) now resident. loaded_from_persist=%d\n",
            result->cx, result->cz, result->loaded_from_persistence);
        sdk_debug_log_output(dbg);
    }
    return 1;
}

int adopt_streamed_remesh_result(const SdkChunkBuildResult* result)
{
    /* Applies streamed remesh result to existing chunk */
    SdkChunk* slot;

    if (!result || !result->built_chunk) return 0;
    if (result->type != SDK_CHUNK_STREAM_RESULT_REMESHED) return 0;
    slot = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, result->cx, result->cz);
    if (!slot) {
        return 0;
    }
    if (!slot->blocks || slot->cx != result->cx || slot->cz != result->cz) {
        if (slot->blocks) {
            slot->remesh_queued = false;
            slot->inflight_mesh_generation = 0u;
        }
        return 0;
    }

    if (slot->inflight_mesh_generation != result->generation ||
        slot->mesh_job_generation != result->generation) {
        slot->remesh_queued = false;
        slot->inflight_mesh_generation = 0u;
        return 0;
    }

    sdk_chunk_apply_mesh_state(slot, result->built_chunk, result->dirty_mask);
    slot->remesh_queued = false;
    slot->inflight_mesh_generation = 0u;
    return 1;
}

void process_streamed_chunk_results(int max_results)
{
    /* Processes pending streamed chunk results with budget */
    process_streamed_chunk_results_with_budget(max_results, stream_adopt_budget_ms());
}

/* ============================================================================
 * Chunk Dirty/Rebuild Functions
 * ============================================================================ */

void mark_all_loaded_chunks_dirty(void)
{
    /* Marks all loaded chunks dirty for rebuild */
    if (!g_sdk.chunks_initialized) return;
    for (int slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(&g_sdk.chunk_mgr, slot_index);
        if (!slot || !slot->occupied || !slot->chunk.blocks) continue;
        sdk_chunk_mark_all_dirty(&slot->chunk);
    }
}

void log_wall_bootstrap_no_progress(int loaded, int total, int pending_jobs, int pending_results)
{
    /* Logs debug info when wall bootstrap makes no progress */
    char dbg[640];
    char inflight[320];
    SdkBootstrapWallSideHealth west;
    SdkBootstrapWallSideHealth north;
    SdkBootstrapWallSideHealth east;
    SdkBootstrapWallSideHealth south;

    collect_active_superchunk_wall_support_health(&west, &north, &east, &south);
    sdk_chunk_streamer_debug_inflight_summary(&g_sdk.chunk_streamer, inflight, sizeof(inflight));

    sprintf_s(dbg, sizeof(dbg),
              "[STREAM] perimeter wall bootstrap no progress loaded=%d total=%d jobs=%d results=%d desired=%d active=%d "
              "support W=%d/%d/%d N=%d/%d/%d E=%d/%d/%d S=%d/%d/%d inflight=%s\n",
              loaded, total, pending_jobs, pending_results,
              sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr),
              sdk_chunk_manager_active_count(&g_sdk.chunk_mgr),
              west.total, west.loaded, west.ready,
              north.total, north.loaded, north.ready,
              east.total, east.loaded, east.ready,
              south.total, south.loaded, south.ready,
              inflight[0] ? inflight : "none");
    sdk_debug_log_output(dbg);
}


/* ============================================================================
 * Phase 3: Stream Budget & GPU Upload Functions
 * ============================================================================ */

float stream_adopt_budget_ms(void)
{
    /* Returns time budget for stream result adoption based on settings */
    if (startup_safe_mode_active()) {
        return 2.0f;
    }

    const int desired_chunks = sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr);
    const int active_chunks = sdk_chunk_manager_active_count(&g_sdk.chunk_mgr);
    const int pending_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
    const int pending_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
    const int startup_backlog = (active_chunks + pending_results + 8) < desired_chunks ||
                                pending_jobs > 32 ||
                                pending_results > 16;

    if (startup_backlog) {
        switch (g_graphics_settings.preset) {
            case SDK_GRAPHICS_PRESET_PERFORMANCE: return 10.0f; // Increased from 1.5ms for vehicle speed
            case SDK_GRAPHICS_PRESET_HIGH:        return 8.0f;
            case SDK_GRAPHICS_PRESET_BALANCED:
            default:                             return 4.0f;
        }
    }

    switch (g_graphics_settings.preset) {
        case SDK_GRAPHICS_PRESET_PERFORMANCE: return 1.5f;
        case SDK_GRAPHICS_PRESET_HIGH:        return 4.0f;
        case SDK_GRAPHICS_PRESET_BALANCED:
        default:                             return 2.5f;
    }
}

static int logical_parallelism_budget(void)
{
    /* Returns number of logical processors (clamped 1-16) */
    SYSTEM_INFO si;
    int count;

    GetSystemInfo(&si);
    count = (int)si.dwNumberOfProcessors;
    if (count < 1) count = 1;
    if (count > 16) count = 16;
    return count;
}

int stream_upload_limit_per_frame(void)
{
    /* Returns max chunk uploads per frame based on settings */
    int parallelism = logical_parallelism_budget();

    if (startup_safe_mode_active()) {
        return max(4, parallelism / 2);
    }

    const int desired_chunks = sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr);
    const int active_chunks = sdk_chunk_manager_active_count(&g_sdk.chunk_mgr);
    const int pending_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
    const int pending_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
    const int startup_backlog = (active_chunks + pending_results + 8) < desired_chunks ||
                                pending_jobs > 32 ||
                                pending_results > 16;

    if (startup_backlog) {
        switch (g_graphics_settings.preset) {
            case SDK_GRAPHICS_PRESET_PERFORMANCE: return max(4, parallelism / 2);
            case SDK_GRAPHICS_PRESET_HIGH:        return parallelism;
            case SDK_GRAPHICS_PRESET_BALANCED:
            default:                             return max(6, (parallelism * 3) / 4);
        }
    }

    switch (g_graphics_settings.preset) {
        case SDK_GRAPHICS_PRESET_PERFORMANCE: return max(2, parallelism / 3);
        case SDK_GRAPHICS_PRESET_HIGH:        return max(6, (parallelism * 3) / 4);
        case SDK_GRAPHICS_PRESET_BALANCED:
        default:                             return max(4, parallelism / 2);
    }
}


size_t stream_upload_byte_budget_per_frame(void)
{
    /* Returns GPU upload byte budget per frame based on settings */
    const int desired_chunks = sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr);
    const int active_chunks = sdk_chunk_manager_active_count(&g_sdk.chunk_mgr);
    const int pending_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
    const int pending_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
    const int startup_backlog = (active_chunks + pending_results + 8) < desired_chunks ||
                                pending_jobs > 32 ||
                                pending_results > 16;

    if (startup_safe_mode_active()) {
        return 16u * 1024u * 1024u;
    }

    if (startup_backlog) {
        switch (g_graphics_settings.preset) {
            case SDK_GRAPHICS_PRESET_PERFORMANCE: return 12u * 1024u * 1024u;
            case SDK_GRAPHICS_PRESET_HIGH:        return 32u * 1024u * 1024u;
            case SDK_GRAPHICS_PRESET_BALANCED:
            default:                             return 20u * 1024u * 1024u;
        }
    }

    switch (g_graphics_settings.preset) {
        case SDK_GRAPHICS_PRESET_PERFORMANCE: return 6u * 1024u * 1024u;
        case SDK_GRAPHICS_PRESET_HIGH:        return 16u * 1024u * 1024u;
        case SDK_GRAPHICS_PRESET_BALANCED:
        default:                             return 10u * 1024u * 1024u;
    }
}

float stream_gpu_upload_budget_ms(void)
{
    /* Returns GPU upload time budget in milliseconds */
    if (startup_safe_mode_active()) {
        return 3.0f;
    }

    switch (g_graphics_settings.preset) {
        case SDK_GRAPHICS_PRESET_PERFORMANCE: return 0.75f;
        case SDK_GRAPHICS_PRESET_HIGH:        return 2.0f;
        case SDK_GRAPHICS_PRESET_BALANCED:
        default:                             return 1.25f;
    }
}

int dirty_remesh_jobs_per_frame(void)
{
    /* Returns number of dirty remesh jobs to schedule per frame */
    int parallelism = logical_parallelism_budget();
    int pending_jobs = sdk_chunk_streamer_pending_jobs(&g_sdk.chunk_streamer);
    int pending_results = sdk_chunk_streamer_pending_results(&g_sdk.chunk_streamer);
    int target_queue_depth;
    int base_jobs;

    if (startup_safe_mode_active()) {
        base_jobs = 0;
    } else {
        switch (g_graphics_settings.preset) {
            case SDK_GRAPHICS_PRESET_PERFORMANCE: base_jobs = 1; break;
            case SDK_GRAPHICS_PRESET_HIGH:        base_jobs = 3; break;
            case SDK_GRAPHICS_PRESET_BALANCED:
            default:                             base_jobs = 2; break;
        }
    }

    target_queue_depth = max(base_jobs, min(4, max(1, parallelism / 4)));

    if (pending_results >= target_queue_depth) {
        return 0;
    }
    if (pending_jobs >= target_queue_depth) {
        return 0;
    }

    return min(base_jobs, target_queue_depth - pending_jobs);
}

int initial_sync_bootstrap_distance(void)
{
    /* Returns initial sync bootstrap distance (1 chunk) */
    return 1;
}


/* ============================================================================

 * Wall Support Counting Functions
 * ============================================================================ */

int count_loaded_active_superchunk_wall_support_targets(void)
{
    /* Counts loaded active superchunk wall support chunks */
    int loaded = 0;

    for (int target_index = 0; target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);
        if (bootstrap_target_is_sync(target)) continue;
        if (!target_is_active_superchunk_wall_support(target)) continue;
        if (sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz)) {
            loaded++;
        }
    }

    return loaded;
}

void collect_active_superchunk_wall_support_health(SdkBootstrapWallSideHealth* west,
                                                    SdkBootstrapWallSideHealth* north,
                                                    SdkBootstrapWallSideHealth* east,
                                                    SdkBootstrapWallSideHealth* south)
{
    /* Collects health stats for each wall side during bootstrap */
    int origin_cx;
    int origin_cz;

    if (west) memset(west, 0, sizeof(*west));
    if (north) memset(north, 0, sizeof(*north));
    if (east) memset(east, 0, sizeof(*east));
    if (south) memset(south, 0, sizeof(*south));

    if (sdk_chunk_manager_radius_from_grid_size(sdk_chunk_manager_grid_size(&g_sdk.chunk_mgr)) <
        SDK_SUPERCHUNK_CHUNK_SPAN) {
        return;
    }

    {
        SdkSuperchunkCell cell;
        sdk_superchunk_cell_from_index(g_sdk.chunk_mgr.primary_scx, g_sdk.chunk_mgr.primary_scz, &cell);
        origin_cx = cell.origin_cx;
        origin_cz = cell.origin_cz;
    }

    for (int target_index = 0; target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr); ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);
        int loaded;
        int ready = 0;
        int adjacent_west;
        int adjacent_north;
        int adjacent_east;
        int adjacent_south;
        SdkChunk* chunk;

        if (bootstrap_target_is_sync(target)) continue;
        if (!target_is_active_superchunk_wall_support(target)) continue;

        chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz);
        loaded = chunk ? 1 : 0;
        if (chunk && bootstrap_chunk_has_gpu_ready_buffers(chunk)) {
            ready = 1;
        }
        adjacent_west = (target->cx == origin_cx - 1 || target->cx == origin_cx + 1);
        adjacent_north = (target->cz == origin_cz - 1 || target->cz == origin_cz + 1);
        adjacent_east = (target->cx == origin_cx + SDK_SUPERCHUNK_WALL_PERIOD - 1 ||
                         target->cx == origin_cx + SDK_SUPERCHUNK_WALL_PERIOD + 1);
        adjacent_south = (target->cz == origin_cz + SDK_SUPERCHUNK_WALL_PERIOD - 1 ||
                          target->cz == origin_cz + SDK_SUPERCHUNK_WALL_PERIOD + 1);

        if (adjacent_west && west) {
            west->total++;
            west->loaded += loaded;
            west->ready += ready;
        }
        if (adjacent_north && north) {
            north->total++;
            north->loaded += loaded;
            north->ready += ready;
        }
        if (adjacent_east && east) {
            east->total++;
            east->loaded += loaded;
            east->ready += ready;
        }
        if (adjacent_south && south) {
            south->total++;
            south->loaded += loaded;
            south->ready += ready;
        }
    }
}

/* ============================================================================
 * Wall Finalization
 * ============================================================================ */

int finalize_active_wall_stage_sync(SdkActiveWallStage stage, int desired_chunks)
{
    /* Finalizes active wall chunks for given stage synchronously */
    int finalized = 0;

    if (desired_chunks <= 0) return 0;

    for (int target_index = 0;
         target_index < sdk_chunk_manager_desired_count(&g_sdk.chunk_mgr) && finalized < desired_chunks;
         ++target_index) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(&g_sdk.chunk_mgr, target_index);
        SdkChunk* chunk;

        if (!target_matches_active_wall_stage(target, stage)) continue;
        chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz);
        if (!chunk || !chunk->blocks) continue;
        if (sdk_chunk_is_active_wall_chunk_fully_ready(&g_sdk.chunk_mgr, chunk)) continue;
        if (finalize_active_wall_chunk_sync(chunk)) {
            finalized++;
        }
    }

    return finalized;
}

int finalize_active_wall_chunk_sync(SdkChunk* chunk)
{
    /* Finalizes single active wall chunk, marks dirty if needed */
    if (!chunk || !chunk->blocks) return 0;
    if (!sdk_superchunk_active_wall_chunk_contains_chunk(g_sdk.chunk_mgr.primary_scx,
                                                         g_sdk.chunk_mgr.primary_scz,
                                                         chunk->cx,
                                                         chunk->cz)) {
        refresh_chunk_wall_finalization_state(chunk);
        return 0;
    }
    if (chunk->wall_finalized_generation == g_sdk.chunk_mgr.topology_generation) {
        return 0;
    }

    sdk_worldgen_finalize_chunk_walls_ctx(&g_sdk.worldgen, chunk);
    sdk_chunk_mark_all_dirty(chunk);
    chunk->wall_finalized_generation = g_sdk.chunk_mgr.topology_generation;
    return 1;
}
