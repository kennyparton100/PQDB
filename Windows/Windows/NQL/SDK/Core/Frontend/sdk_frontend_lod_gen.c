/**
 * sdk_frontend_lod_gen.c -- Async far LOD mesh generation frontend driver.
 *
 * Mirrors sdk_frontend_worldgen.c but drives the far mesh gen scheduler
 * instead of the worldgen scheduler.  Called once per frame from
 * sdk_api.c's update function alongside update_async_world_generation.
 */
#include "sdk_frontend_internal.h"
#include "../World/FarMeshGen/sdk_far_mesh_gen_scheduler.h"
#include "../API/Internal/sdk_api_internal.h"
#include "../World/Superchunks/Config/sdk_superchunk_config.h"
#include "../World/Persistence/sdk_world_tooling.h"

#include <windows.h>
#include <string.h>

void frontend_open_far_lod_generating(void)
{
    g_frontend_view = SDK_START_MENU_VIEW_FAR_LOD_GENERATING;
}

void begin_async_far_lod_gen(const SdkWorldSaveMeta* meta)
{
    if (!meta) return;
    if (g_far_lod_gen_active) return;

    g_far_lod_gen_target           = *meta;
    g_far_lod_gen_active           = true;
    g_far_lod_gen_progress         = 0.0f;
    g_far_lod_gen_stage            = 0;
    g_far_lod_gen_superchunks_done = 0;
    g_far_lod_gen_current_ring     = 0;
    g_far_lod_gen_threads          = SDK_OFFLINE_MAP_GENERATION_THREADS;
    g_far_lod_gen_active_workers   = 0;
    g_far_lod_gen_chunks_per_sec   = 0.0f;
    strcpy_s(g_far_lod_gen_status, sizeof(g_far_lod_gen_status),
             "Initializing LOD mesh generator...");

    frontend_open_far_lod_generating();
}

void request_async_far_lod_gen_stop(void)
{
    if (!g_far_lod_gen_active) return;
    if (g_far_lod_gen_stage < 2) {
        g_far_lod_gen_stage    = 2;
        g_far_lod_gen_progress = 0.0f;
        strcpy_s(g_far_lod_gen_status, sizeof(g_far_lod_gen_status),
                 "Stopping LOD mesh generator...");
    }
}

void update_async_far_lod_gen(void)
{
    SdkFarMeshGenSchedulerStats stats;

    if (!g_far_lod_gen_active) return;

    /* ------------------------------------------------------------------ */
    /* Stage 0: initialise persistence + scheduler                         */
    /* ------------------------------------------------------------------ */
    if (g_far_lod_gen_stage == 0) {
        SdkFarMeshGenSchedulerConfig config;
        SdkWorldDesc                 world_desc;
        SdkSuperchunkConfig          sc_config;
        char save_path[MAX_PATH];

        sdk_world_meta_to_world_desc(&g_far_lod_gen_target, &world_desc);
        sdk_world_meta_to_superchunk_config(&g_far_lod_gen_target, &sc_config);
        sdk_superchunk_set_config(&sc_config);

        if (!build_world_save_file_path(g_far_lod_gen_target.folder_id,
                                        save_path, sizeof(save_path))) {
            strcpy_s(g_far_lod_gen_status, sizeof(g_far_lod_gen_status),
                     "LOD gen: failed to build save path");
            g_far_lod_gen_active = false;
            frontend_open_world_actions();
            return;
        }

        if (g_sdk.persistence.impl) {
            sdk_persistence_shutdown(&g_sdk.persistence);
        }
        sdk_persistence_init(&g_sdk.persistence, &world_desc, save_path);

        memset(&config, 0, sizeof(config));
        config.worker_count = g_far_lod_gen_threads;
        strcpy_s(config.world_save_id, sizeof(config.world_save_id),
                 g_far_lod_gen_target.folder_id);

        if (!init_far_mesh_gen_scheduler(&config, &g_sdk.persistence)) {
            strcpy_s(g_far_lod_gen_status, sizeof(g_far_lod_gen_status),
                     "LOD gen: scheduler init failed");
            g_far_lod_gen_active = false;
            frontend_open_world_actions();
            return;
        }

        g_far_lod_gen_stage    = 1;
        g_far_lod_gen_progress = 0.0f;
        strcpy_s(g_far_lod_gen_status, sizeof(g_far_lod_gen_status),
                 "Generating LOD meshes...");
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Stage 1: pump jobs + poll stats                                     */
    /* ------------------------------------------------------------------ */
    if (g_far_lod_gen_stage == 1) {
        int known;

        pump_far_mesh_gen_scheduler_bulk(g_far_lod_gen_threads * 8);
        memset(&stats, 0, sizeof(stats));
        get_far_mesh_gen_scheduler_stats(&stats);

        g_far_lod_gen_current_ring     = stats.current_ring;
        g_far_lod_gen_active_workers   = stats.active_workers;
        g_far_lod_gen_superchunks_done = stats.superchunks_done;
        g_far_lod_gen_chunks_per_sec   = stats.chunks_per_sec;

        known = stats.superchunks_done + stats.queued_jobs + stats.active_workers;
        if (known > 0) {
            g_far_lod_gen_progress =
                api_clampf((float)stats.superchunks_done / (float)known, 0.0f, 1.0f);
        } else {
            g_far_lod_gen_progress = (stats.chunks_done > 0) ? 1.0f : 0.0f;
        }

        snprintf(g_far_lod_gen_status, sizeof(g_far_lod_gen_status),
                 "%d superchunks   ring %d   %.1f chunks/sec",
                 stats.superchunks_done,
                 stats.current_ring,
                 stats.chunks_per_sec);

        if (stats.queued_jobs == 0 && stats.active_workers == 0) {
            g_far_lod_gen_stage = 2;
        }
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Stage 2: shut down scheduler + save                                 */
    /* ------------------------------------------------------------------ */
    if (g_far_lod_gen_stage == 2) {
        request_shutdown_far_mesh_gen_scheduler();
        if (!poll_shutdown_far_mesh_gen_scheduler()) {
            strcpy_s(g_far_lod_gen_status, sizeof(g_far_lod_gen_status),
                     "Stopping LOD mesh workers...");
            return;
        }

        sdk_persistence_save(&g_sdk.persistence);
        sdk_persistence_shutdown(&g_sdk.persistence);

        g_far_lod_gen_active           = false;
        g_far_lod_gen_progress         = 0.0f;
        g_far_lod_gen_stage            = 0;
        g_far_lod_gen_active_workers   = 0;
        g_far_lod_gen_chunks_per_sec   = 0.0f;
        strcpy_s(g_far_lod_gen_status, sizeof(g_far_lod_gen_status), "");

        frontend_open_world_actions();
    }
}
