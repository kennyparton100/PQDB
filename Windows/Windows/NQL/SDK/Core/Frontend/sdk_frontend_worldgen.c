#include "sdk_frontend_internal.h"
#include "../World/Worldgen/Scheduler/sdk_worldgen_scheduler.h"
#include "../API/Internal/sdk_api_internal.h"
#include "../World/Superchunks/Config/sdk_superchunk_config.h"
#include <windows.h>
#include <string.h>

static int load_world_desc_for_frontend_task(const SdkWorldSaveMeta* meta, SdkWorldDesc* out_world_desc)
{
    /* Loads world descriptor from save meta for world generation */
    char world_path[MAX_PATH];
    
    if (!meta || !out_world_desc) return 0;
    
    if (!build_world_save_dir_path(meta->folder_id, world_path, sizeof(world_path))) {
        return 0;
    }
    
    sdk_world_meta_to_world_desc(meta, out_world_desc);
    return 1;
}

static void apply_superchunk_config_for_frontend_task(const SdkWorldSaveMeta* meta)
{
    /* Applies superchunk configuration from save meta for world generation */
    SdkSuperchunkConfig config = {0};

    if (!meta) return;

    sdk_world_meta_to_superchunk_config(meta, &config);
    sdk_superchunk_set_config(&config);
}

void begin_async_world_generation(const SdkWorldSaveMeta* meta)
{
    /* Begins async offline world chunk generation for save */
    SdkWorldDesc world_desc;
    
    if (!meta) return;
    
    if (g_world_generation_active) return;
    
    if (!load_world_desc_for_frontend_task(meta, &world_desc)) {
        return;
    }
    
    g_world_generation_target = *meta;
    g_world_generation_active = true;
    g_world_generation_is_offline = true;
    g_world_generation_progress = 0.0f;
    g_world_generation_stage = 0;
    g_world_generation_superchunks_done = 0;
    g_world_generation_current_ring = 0;
    g_world_generation_threads = SDK_OFFLINE_MAP_GENERATION_THREADS;
    g_world_generation_started_ms = GetTickCount64();
    g_world_generation_last_sample_ms = 0u;
    g_world_generation_last_sample_chunks = 0;
    g_world_generation_active_workers = 0;
    g_world_generation_chunks_per_sec = 0.0f;
    strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
             "Initializing world generator...");
    
    frontend_open_world_generating();
}

void request_async_world_generation_stop(void)
{
    /* Requests graceful stop of world generation */
    if (!g_world_generation_active) return;
    if (g_world_generation_stage < 2) {
        g_world_generation_stage = 2;
        g_world_generation_progress = 0.0f;
        strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
                 "Stopping world generator...");
    }
}

void update_async_world_generation(void)
{
    /* Updates world generation progress and manages scheduler stages */
    SdkWorldGenSchedulerStats stats;
    static int s_last_traced_stage = -1;
    
    if (!g_world_generation_active) return;
    if (s_last_traced_stage != g_world_generation_stage) {
        s_last_traced_stage = g_world_generation_stage;
        sdk_load_trace_note_state("update_async_world_generation_stage",
                                  g_frontend_view,
                                  g_sdk.world_session_active ? 1 : 0,
                                  g_world_generation_active ? 1 : 0,
                                  g_world_generation_stage,
                                  g_world_generation_status);
    }
    
    if (g_world_generation_stage == 0) {
        SdkWorldDesc world_desc;
        SdkWorldGenSchedulerConfig config;
        char save_path[MAX_PATH];
        
        if (!load_world_desc_for_frontend_task(&g_world_generation_target, &world_desc)) {
            sdk_load_trace_note("async_world_generation_load_desc_failed", "load_world_desc_for_frontend_task failed");
            g_world_generation_active = false;
            g_world_generation_progress = 0.0f;
            g_world_generation_stage = 0;
            strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
                     "World generator init failed");
            frontend_open_world_actions();
            return;
        }
        
        if (!build_world_save_file_path(g_world_generation_target.folder_id, save_path, sizeof(save_path))) {
            sdk_load_trace_note("async_world_generation_save_path_failed", "build_world_save_file_path failed");
            g_world_generation_active = false;
            g_world_generation_progress = 0.0f;
            g_world_generation_stage = 0;
            strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
                     "World save path failed");
            frontend_open_world_actions();
            return;
        }
        
        if (g_sdk.persistence.impl) {
            sdk_persistence_shutdown(&g_sdk.persistence);
        }
        sdk_persistence_init(&g_sdk.persistence, &world_desc, save_path);
        
        memset(&config, 0, sizeof(config));
        config.world_desc = world_desc;
        config.world_seed = world_desc.seed;
        config.worker_count = g_world_generation_threads;
        strcpy_s(config.world_save_id, sizeof(config.world_save_id), g_world_generation_target.folder_id);
        apply_superchunk_config_for_frontend_task(&g_world_generation_target);
        
        if (!init_worldgen_scheduler(&config)) {
            sdk_load_trace_note("async_world_generation_scheduler_init_failed", "init_worldgen_scheduler failed");
            g_world_generation_active = false;
            g_world_generation_progress = 0.0f;
            g_world_generation_stage = 0;
            strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
                     "World generator init failed");
            frontend_open_world_actions();
            return;
        }
        
        g_world_generation_stage = 1;
        g_world_generation_progress = 0.0f;
        strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
                 "Generating world chunks...");
        return;
    }
    
    if (g_world_generation_stage == 1) {
        uint64_t now_ms;
        int known_superchunks;
        
        pump_worldgen_scheduler_offline_bulk(g_world_generation_threads * 8);
        memset(&stats, 0, sizeof(stats));
        get_worldgen_scheduler_stats(&stats);
        now_ms = GetTickCount64();
        
        g_world_generation_current_ring = stats.current_ring;
        g_world_generation_active_workers = stats.active_workers;
        g_world_generation_superchunks_done = stats.superchunks_completed;
        
        if (g_world_generation_last_sample_ms == 0u) {
            g_world_generation_last_sample_ms = now_ms;
            g_world_generation_last_sample_chunks = stats.chunks_completed;
        } else if (now_ms > g_world_generation_last_sample_ms + 250u) {
            uint64_t delta_ms = now_ms - g_world_generation_last_sample_ms;
            int delta_chunks = stats.chunks_completed - g_world_generation_last_sample_chunks;
            if (delta_ms > 0u) {
                g_world_generation_chunks_per_sec =
                    (float)delta_chunks * 1000.0f / (float)delta_ms;
            }
            g_world_generation_last_sample_ms = now_ms;
            g_world_generation_last_sample_chunks = stats.chunks_completed;
        }
        
        known_superchunks = stats.superchunks_completed + stats.queued_jobs + stats.active_workers;
        if (known_superchunks > 0) {
            g_world_generation_progress =
                api_clampf((float)stats.superchunks_completed / (float)known_superchunks,
                           0.0f, 1.0f);
        } else {
            g_world_generation_progress = (stats.chunks_completed > 0) ? 1.0f : 0.0f;
        }
        
        snprintf(g_world_generation_status, sizeof(g_world_generation_status),
                 "%d/%d superchunks   ring %d   %.1f chunks/sec",
                 stats.superchunks_completed,
                 known_superchunks > 0 ? known_superchunks : stats.superchunks_completed,
                 stats.current_ring,
                 g_world_generation_chunks_per_sec);
        
        /* Check if generation is complete */
        if (stats.queued_jobs == 0 && stats.active_workers == 0) {
            g_world_generation_stage = 2;
        }
    }
    
    if (g_world_generation_stage == 2) {
        if (g_world_generation_is_offline) {
            request_shutdown_worldgen_scheduler();
            if (!poll_shutdown_worldgen_scheduler()) {
                strcpy_s(g_world_generation_status, sizeof(g_world_generation_status),
                         "Stopping world generator workers...");
                return;
            }
            
            sdk_persistence_save(&g_sdk.persistence);
            sdk_persistence_shutdown(&g_sdk.persistence);
        } else {
            sdk_load_trace_note("start_world_session_begin", g_world_generation_target.folder_id);
            if (start_world_session(&g_world_generation_target)) {
                sdk_load_trace_note("start_world_session_success", g_world_generation_target.folder_id);
                sdk_finalize_world_launch(&g_world_generation_target);
            } else {
                sdk_load_trace_note("start_world_session_failure", g_world_generation_target.folder_id);
                g_frontend_refresh_pending = true;
                frontend_open_main_menu();
            }
        }
        
        g_world_generation_active = false;
        g_world_generation_progress = 0.0f;
        g_world_generation_stage = 0;
        g_world_generation_active_workers = 0;
        g_world_generation_chunks_per_sec = 0.0f;
        strcpy_s(g_world_generation_status, sizeof(g_world_generation_status), "");
        if (g_world_generation_is_offline) {
            frontend_open_world_actions();
        }
        s_last_traced_stage = -1;
    }
}
