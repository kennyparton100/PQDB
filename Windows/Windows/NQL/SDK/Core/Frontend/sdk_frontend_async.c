#include "sdk_frontend_internal.h"
#include "../World/Superchunks/Config/sdk_superchunk_config.h"

const float k_world_generation_prep_progress_max = 0.55f;
uint64_t g_world_map_generation_started_ms = 0u;
uint64_t g_world_map_generation_last_sample_ms = 0u;
uint64_t g_world_map_generation_last_completion_ms = 0u;
int g_world_map_generation_last_sample_tiles = 0;
int g_world_map_generation_inflight = 0;
int g_world_map_generation_queued_pages = 0;
int g_world_map_generation_active_workers = 0;
int g_world_map_generation_last_tile_chunks = 0;
float g_world_map_generation_tiles_per_sec = 0.0f;
float g_world_map_generation_last_save_age_seconds = 0.0f;
float g_world_map_generation_last_tile_ms = 0.0f;

/**
 * Opens world map generation progress screen, initializes tracking variables.
 */
static void frontend_open_world_map_generating(void)
{
    g_frontend_view = SDK_START_MENU_VIEW_WORLD_MAP_GENERATING;
    g_world_map_generation_progress = 0.0f;
    g_world_map_generation_stage = 0;
    g_world_map_generation_ring = 0;
    g_world_map_generation_tiles_done = 0;
    g_world_map_generation_threads = SDK_OFFLINE_MAP_GENERATION_THREADS;
    g_world_map_generation_started_ms = GetTickCount64();
    g_world_map_generation_last_sample_ms = 0u;
    g_world_map_generation_last_completion_ms = 0u;
    g_world_map_generation_last_sample_tiles = 0;
    g_world_map_generation_inflight = 0;
    g_world_map_generation_queued_pages = 0;
    g_world_map_generation_active_workers = 0;
    g_world_map_generation_last_tile_chunks = 0;
    g_world_map_generation_tiles_per_sec = 0.0f;
    g_world_map_generation_last_save_age_seconds = 0.0f;
    g_world_map_generation_last_tile_ms = 0.0f;
    strcpy_s(g_world_map_generation_status, sizeof(g_world_map_generation_status), "Initializing exact map baker...");
    frontend_reset_nav_state();
}

/**
 * Loads world descriptor from save meta for map generation task.
 * 
 * @param meta World save meta.
 * @param out_world_desc Output world descriptor.
 * @return 1 on success, 0 on failure.
 */
static int load_world_desc_for_frontend_task(const SdkWorldSaveMeta* meta, SdkWorldDesc* out_world_desc)
{
    /* Loads world descriptor from save meta for map generation task */
    SdkPersistence persistence;
    SdkWorldDesc world_desc;

    if (!meta || !out_world_desc) return 0;
    memset(out_world_desc, 0, sizeof(*out_world_desc));
    memset(&persistence, 0, sizeof(persistence));
    world_desc.seed = meta->seed;
    sdk_persistence_init(&persistence, &world_desc, meta->save_path);
    sdk_persistence_get_world_desc(&persistence, &world_desc);
    sdk_persistence_shutdown(&persistence);
    world_desc.seed = meta->seed;
    world_desc.settlements_enabled = meta->settlements_enabled;
    world_desc.construction_cells_enabled = meta->construction_cells_enabled;
    *out_world_desc = world_desc;
    return 1;
}

/**
 * Applies superchunk configuration from save meta to current config.
 * 
 * @param meta World save meta.
 */
static void apply_superchunk_config_for_frontend_task(const SdkWorldSaveMeta* meta)
{
    /* Applies superchunk configuration from save meta to current config */
    SdkSuperchunkConfig config = {0};

    if (!meta) return;

    sdk_world_meta_to_superchunk_config(meta, &config);
    sdk_superchunk_set_config(&config);
}

/**
 * Begins async offline map tile generation for world save.
 * 
 * @param meta World save meta.
 */
void begin_async_world_map_generation(const SdkWorldSaveMeta* meta)
{
    if (!meta) return;

    frontend_open_world_map_generating();
    g_world_map_generation_target = *meta;
    g_world_map_generation_active = true;
    g_world_map_generation_progress = 0.0f;
    g_world_map_generation_stage = 0;
    g_world_map_generation_ring = 0;
    g_world_map_generation_tiles_done = 0;
    g_world_map_generation_threads = SDK_OFFLINE_MAP_GENERATION_THREADS;
    g_world_map_generation_started_ms = GetTickCount64();
    g_world_map_generation_last_sample_ms = 0u;
    g_world_map_generation_last_completion_ms = 0u;
    g_world_map_generation_last_sample_tiles = 0;
    g_world_map_generation_inflight = 0;
    g_world_map_generation_queued_pages = 0;
    g_world_map_generation_active_workers = 0;
    g_world_map_generation_last_tile_chunks = 0;
    g_world_map_generation_tiles_per_sec = 0.0f;
    g_world_map_generation_last_save_age_seconds = 0.0f;
    g_world_map_generation_last_tile_ms = 0.0f;
    strcpy_s(g_world_map_generation_status, sizeof(g_world_map_generation_status),
             "Initializing exact map baker...");
}

/**
 * Begins async world session loading from save meta.
 * 
 * @param meta World save meta.
 */
void begin_async_world_session_load(const SdkWorldSaveMeta* meta)
{
    if (!meta) return;

    g_world_generation_cancel_requested = false;
    sdk_load_trace_bind_meta(meta);
    sdk_load_trace_note("begin_async_world_session_load", meta->folder_id);
    sdk_prepare_world_launch(SDK_WORLD_LAUNCH_STANDARD);
    frontend_open_world_generating();
    g_world_generation_target = *meta;
    g_world_generation_target.render_distance_chunks =
        clamp_render_distance_chunks(g_world_generation_target.render_distance_chunks);
    g_world_generation_active = true;
    g_world_generation_is_offline = false;
    g_world_generation_progress = k_world_generation_prep_progress_max;
    g_world_generation_stage = 2;
    g_world_generation_chunks_total = 0;
    g_world_generation_chunks_done = 0;
    strcpy_s(g_world_generation_status, sizeof(g_world_generation_status), "Starting world session...");
    sdk_load_trace_note_state("async_world_session_load_ready",
                              g_frontend_view,
                              g_sdk.world_session_active ? 1 : 0,
                              1,
                              g_world_generation_stage,
                              g_world_generation_status);
}

/**
 * Begins async local host start for world (optionally joining after).
 * 
 * @param meta World save meta.
 * @param join_after_start Join after start flag.
 */
void begin_async_local_host_start(const SdkWorldSaveMeta* meta, int join_after_start)
{
    if (!meta) return;

    sdk_prepare_world_launch(join_after_start
                                 ? SDK_WORLD_LAUNCH_LOCAL_HOST_JOIN
                                 : SDK_WORLD_LAUNCH_LOCAL_HOST_BACKGROUND);
    frontend_open_world_generating();
    g_world_generation_target = *meta;
    g_world_generation_target.render_distance_chunks =
        clamp_render_distance_chunks(g_world_generation_target.render_distance_chunks);
    g_world_generation_active = true;
    g_world_generation_is_offline = false;
    g_world_generation_progress = k_world_generation_prep_progress_max;
    g_world_generation_stage = 2;
    g_world_generation_chunks_total = 0;
    g_world_generation_chunks_done = 0;
    strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
             join_after_start ? "Starting local host..." : "Starting local host in background...");
}

/**
 * Cancels active world generation and resets state.
 */
void cancel_async_world_generation(void)
{
    g_world_generation_active = false;
    g_world_generation_is_offline = false;
    g_world_generation_progress = 0.0f;
    g_world_generation_stage = 0;
    strcpy_s(g_world_generation_status, sizeof(g_world_generation_status), "");
}

/**
 * Requests graceful stop of world map generation.
 */
void request_async_world_map_generation_stop(void)
{
    if (!g_world_map_generation_active) return;
    if (g_world_map_generation_stage < 2) {
        g_world_map_generation_stage = 2;
        g_world_map_generation_progress = 0.0f;
        strcpy_s(g_world_map_generation_status, sizeof(g_world_map_generation_status),
                 "Stopping exact map workers...");
    }
}

/**
 * Updates world map generation progress, pumps scheduler each frame.
 */
void update_async_world_map_generation(void)
{
    SdkMapSchedulerStats stats;

    if (!g_world_map_generation_active) return;

    if (g_world_map_generation_stage == 0) {
        SdkWorldDesc world_desc;
        SdkMapSchedulerConfig config;

        if (g_map_scheduler.initialized) {
            shutdown_superchunk_map_scheduler();
        }
        if (!load_world_desc_for_frontend_task(&g_world_map_generation_target, &world_desc)) {
            g_world_map_generation_active = false;
            g_world_map_generation_progress = 0.0f;
            g_world_map_generation_stage = 0;
            strcpy_s(g_world_map_generation_status, sizeof(g_world_map_generation_status),
                     "Map generator init failed");
            frontend_open_world_actions();
            return;
        }

        memset(&config, 0, sizeof(config));
        config.world_desc = world_desc;
        config.world_seed = world_desc.seed;
        config.worker_count = g_world_map_generation_threads;
        config.mode = SDK_MAP_SCHED_MODE_OFFLINE_BULK;
        config.build_kind = SDK_MAP_BUILD_EXACT_OFFLINE;
        strcpy_s(config.world_save_id, sizeof(config.world_save_id), g_world_map_generation_target.folder_id);
        apply_superchunk_config_for_frontend_task(&g_world_map_generation_target);

        if (!init_superchunk_map_scheduler(&config)) {
            g_world_map_generation_active = false;
            g_world_map_generation_progress = 0.0f;
            g_world_map_generation_stage = 0;
            strcpy_s(g_world_map_generation_status, sizeof(g_world_map_generation_status),
                     "Map scheduler start failed");
            frontend_open_world_actions();
            return;
        }

        g_world_map_generation_stage = 1;
        g_world_map_generation_progress = 0.0f;
        strcpy_s(g_world_map_generation_status, sizeof(g_world_map_generation_status),
                 "Generating exact seed-terrain tiles...");
        return;
    }

    if (g_world_map_generation_stage == 1) {
        uint64_t now_ms;

        pump_superchunk_map_scheduler_offline_bulk(g_world_map_generation_threads * 8);
        memset(&stats, 0, sizeof(stats));
        get_superchunk_map_scheduler_stats(&stats);
        now_ms = GetTickCount64();
        if (g_world_map_generation_started_ms == 0u) {
            g_world_map_generation_started_ms = now_ms;
        }

        g_world_map_generation_ring = stats.current_ring;
        g_world_map_generation_inflight = stats.tiles_inflight;
        g_world_map_generation_queued_pages = stats.queued_pages;
        g_world_map_generation_active_workers = stats.active_workers;
        g_world_map_generation_last_tile_chunks = stats.last_tile_chunks;
        g_world_map_generation_last_tile_ms = stats.last_tile_build_ms;

        if (stats.tiles_completed > g_world_map_generation_tiles_done) {
            g_world_map_generation_last_completion_ms = now_ms;
        }
        g_world_map_generation_tiles_done = stats.tiles_completed;

        if (g_world_map_generation_last_sample_ms == 0u) {
            g_world_map_generation_last_sample_ms = now_ms;
            g_world_map_generation_last_sample_tiles = stats.tiles_completed;
        } else if (now_ms > g_world_map_generation_last_sample_ms + 250u) {
            uint64_t delta_ms = now_ms - g_world_map_generation_last_sample_ms;
            int delta_tiles = stats.tiles_completed - g_world_map_generation_last_sample_tiles;
            if (delta_ms > 0u) {
                g_world_map_generation_tiles_per_sec =
                    (float)delta_tiles * 1000.0f / (float)delta_ms;
            }
            g_world_map_generation_last_sample_ms = now_ms;
            g_world_map_generation_last_sample_tiles = stats.tiles_completed;
        }

        if (g_world_map_generation_last_completion_ms != 0u) {
            g_world_map_generation_last_save_age_seconds =
                (float)(now_ms - g_world_map_generation_last_completion_ms) / 1000.0f;
        } else {
            g_world_map_generation_last_save_age_seconds =
                (float)(now_ms - g_world_map_generation_started_ms) / 1000.0f;
        }

        if (stats.worker_count > 0) {
            g_world_map_generation_progress =
                api_clampf((float)(stats.queued_pages + stats.active_workers) /
                               (float)(stats.worker_count * 2),
                          0.0f, 1.0f);
        } else {
            g_world_map_generation_progress = 0.0f;
        }
        snprintf(g_world_map_generation_status, sizeof(g_world_map_generation_status),
                 "%d queued pages   %d active workers   last %.0f ms / %d chunks",
                 stats.queued_pages,
                 stats.active_workers,
                 stats.last_tile_build_ms,
                 stats.last_tile_chunks);
        return;
    }

    if (g_world_map_generation_stage == 2) {
        request_shutdown_superchunk_map_scheduler();
        if (!poll_shutdown_superchunk_map_scheduler()) {
            strcpy_s(g_world_map_generation_status, sizeof(g_world_map_generation_status),
                     "Stopping map tile workers...");
            return;
        }

        g_world_map_generation_active = false;
        g_world_map_generation_progress = 0.0f;
        g_world_map_generation_stage = 0;
        g_world_map_generation_inflight = 0;
        g_world_map_generation_queued_pages = 0;
        g_world_map_generation_active_workers = 0;
        g_world_map_generation_last_tile_chunks = 0;
        g_world_map_generation_tiles_per_sec = 0.0f;
        g_world_map_generation_last_save_age_seconds = 0.0f;
        g_world_map_generation_last_tile_ms = 0.0f;
        strcpy_s(g_world_map_generation_status, sizeof(g_world_map_generation_status), "");
        frontend_open_world_actions();
    }
}

