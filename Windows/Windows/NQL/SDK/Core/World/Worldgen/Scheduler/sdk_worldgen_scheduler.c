#include "sdk_worldgen_scheduler.h"
#include "../Internal/sdk_worldgen_internal.h"
#include "../../Chunks/sdk_chunk.h"
#include "../../../API/Internal/sdk_api_internal.h"
#include <windows.h>
#include <string.h>

#define SDK_WORLDGEN_JOB_CAPACITY 256
#define SDK_WORLDGEN_MAX_WORKERS 16

typedef struct {
    int scx;
    int scz;
} SdkWorldGenJob;

typedef struct {
    void* sched;
    SdkWorldGen* worldgen;
    int worker_index;
} SdkWorldGenWorkerParam;

typedef struct {
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE jobs_cv;
    int running;
    int initialized;
    
    SdkWorldDesc world_desc;
    char world_save_id[64];
    uint32_t world_seed;
    
    int worker_count;
    SdkWorldGen* workers_worldgen[SDK_WORLDGEN_MAX_WORKERS];
    HANDLE workers_thread[SDK_WORLDGEN_MAX_WORKERS];
    SdkWorldGenWorkerParam* workers_param[SDK_WORLDGEN_MAX_WORKERS];
    
    SdkWorldGenJob jobs[SDK_WORLDGEN_JOB_CAPACITY];
    int job_head;
    int job_count;
    
    int bulk_scx;
    int bulk_scz;
    int bulk_dir;
    int bulk_leg_length;
    int bulk_leg_progress;
    int bulk_legs_done;
    int bulk_ring;
    int bulk_cursor_started;
    
    int superchunks_completed;
    int chunks_completed;
    int active_workers;
    
    ULONGLONG last_sample_ms;
    int last_sample_chunks;
    float chunks_per_sec;
} SdkWorldGenSchedulerInternal;

static SdkWorldGenSchedulerInternal g_worldgen_scheduler = {0};

static int choose_worldgen_worker_count(void)
{
    SYSTEM_INFO si;
    int count;
    
    GetSystemInfo(&si);
    count = (int)si.dwNumberOfProcessors;
    if (count < 1) count = 1;
    if (count > SDK_WORLDGEN_MAX_WORKERS) count = SDK_WORLDGEN_MAX_WORKERS;
    return count;
}

static int worldgen_job_queue_index(int head, int count)
{
    return (head + count) % SDK_WORLDGEN_JOB_CAPACITY;
}

static void reset_worldgen_cursor_locked(SdkWorldGenSchedulerInternal* sched)
{
    if (!sched) return;
    sched->bulk_cursor_started = 1;
    sched->bulk_scx = 0;
    sched->bulk_scz = 0;
    sched->bulk_dir = 0;
    sched->bulk_leg_length = 1;
    sched->bulk_leg_progress = 0;
    sched->bulk_legs_done = 0;
    sched->bulk_ring = 0;
    sched->superchunks_completed = 0;
    sched->chunks_completed = 0;
}

static void worldgen_cursor_pop_locked(SdkWorldGenSchedulerInternal* sched, int* out_scx, int* out_scz)
{
    int scx;
    int scz;
    int abs_scx;
    int abs_scz;
    
    if (!sched || !sched->bulk_cursor_started) return;
    
    scx = sched->bulk_scx;
    scz = sched->bulk_scz;
    abs_scx = (scx < 0) ? -scx : scx;
    abs_scz = (scz < 0) ? -scz : scz;
    sched->bulk_ring = (abs_scx > abs_scz) ? abs_scx : abs_scz;
    if (out_scx) *out_scx = scx;
    if (out_scz) *out_scz = scz;
    
    switch (sched->bulk_dir) {
        case 0: sched->bulk_scx++; break;
        case 1: sched->bulk_scz++; break;
        case 2: sched->bulk_scx--; break;
        default: sched->bulk_scz--; break;
    }
    
    sched->bulk_leg_progress++;
    if (sched->bulk_leg_progress >= sched->bulk_leg_length) {
        sched->bulk_leg_progress = 0;
        sched->bulk_dir = (sched->bulk_dir + 1) & 3;
        sched->bulk_legs_done++;
        if (sched->bulk_legs_done >= 2) {
            sched->bulk_legs_done = 0;
            sched->bulk_leg_length++;
        }
    }
}

static int worldgen_take_next_job_locked(SdkWorldGenSchedulerInternal* sched, SdkWorldGenJob* out_job)
{
    if (!sched || !out_job || sched->job_count <= 0) return 0;
    *out_job = sched->jobs[sched->job_head];
    sched->job_head = worldgen_job_queue_index(sched->job_head, 1);
    sched->job_count--;
    return 1;
}

DWORD WINAPI worldgen_worker_proc(LPVOID param)
{
    SdkWorldGenWorkerParam* wp = (SdkWorldGenWorkerParam*)param;
    SdkWorldGenSchedulerInternal* sched;
    SdkWorldGen* worldgen;
    
    if (!wp) return 0;
    sched = (SdkWorldGenSchedulerInternal*)wp->sched;
    worldgen = wp->worldgen;
    if (!sched || !worldgen) return 0;
    
    for (;;) {
        SdkWorldGenJob job;
        int have_job = 0;
        
        memset(&job, 0, sizeof(job));
        EnterCriticalSection(&sched->lock);
        while (sched->running && sched->job_count == 0) {
            SleepConditionVariableCS(&sched->jobs_cv, &sched->lock, INFINITE);
        }
        if (!sched->running && sched->job_count == 0) {
            LeaveCriticalSection(&sched->lock);
            break;
        }
        have_job = worldgen_take_next_job_locked(sched, &job);
        if (have_job) {
            sched->active_workers++;
        }
        LeaveCriticalSection(&sched->lock);
        
        if (!have_job) {
            continue;
        }
        
        int scx = job.scx;
        int scz = job.scz;
        int chunks_generated = 0;
        ULONGLONG start_ms = GetTickCount64();
        SdkSuperchunkCell cell;

        sdk_superchunk_cell_from_index(scx, scz, &cell);
        
        for (int local_cz = 0; local_cz < SDK_SUPERCHUNK_CHUNK_SPAN; ++local_cz) {
            for (int local_cx = 0; local_cx < SDK_SUPERCHUNK_CHUNK_SPAN; ++local_cx) {
                int cx = cell.interior_min_cx + local_cx;
                int cz = cell.interior_min_cz + local_cz;
                SdkChunk chunk;
                
                memset(&chunk, 0, sizeof(chunk));
                chunk.cx = cx;
                chunk.cz = cz;
                
                if (sdk_persistence_load_chunk(&g_sdk.persistence, cx, cz, &chunk)) {
                    /* Debug: log when chunk is loaded from persistence */
                    char buf[256];
                    sprintf_s(buf, sizeof(buf), "CHUNK_LOADED: chunk=(%d,%d) from persistence, skipping generation", cx, cz);
                    sdk_worldgen_debug_capture_note_custom(buf);
                    continue;
                }
                
                /* Debug: log when chunk will be generated fresh */
                {
                    char buf[256];
                    sprintf_s(buf, sizeof(buf), "CHUNK_GENERATE: chunk=(%d,%d) generating fresh", cx, cz);
                    sdk_worldgen_debug_capture_note_custom(buf);
                }
                
                chunk.blocks = (SdkWorldCellCode*)calloc(CHUNK_TOTAL_BLOCKS, sizeof(SdkWorldCellCode));
                if (!chunk.blocks) continue;
                
                sdk_construction_chunk_set_registry(&chunk, g_sdk.chunk_mgr.construction_registry);
                
                sdk_worldgen_generate_chunk_ctx(worldgen, &chunk);
                
                sdk_persistence_store_chunk(&g_sdk.persistence, &chunk);
                
                free(chunk.blocks);
                chunks_generated++;
            }
        }
        
        ULONGLONG elapsed_ms = GetTickCount64() - start_ms;
        
        if (chunks_generated > 0) {
            sdk_persistence_save(&g_sdk.persistence);
        }
        
        EnterCriticalSection(&sched->lock);
        sched->active_workers--;
        sched->superchunks_completed++;
        sched->chunks_completed += chunks_generated;
        
        ULONGLONG now_ms = GetTickCount64();
        if (sched->last_sample_ms == 0) {
            sched->last_sample_ms = now_ms;
            sched->last_sample_chunks = sched->chunks_completed;
        } else if (now_ms - sched->last_sample_ms >= 1000) {
            uint64_t delta_ms = now_ms - sched->last_sample_ms;
            int delta_chunks = sched->chunks_completed - sched->last_sample_chunks;
            if (delta_ms > 0) {
                sched->chunks_per_sec = (float)delta_chunks * 1000.0f / (float)delta_ms;
            }
            sched->last_sample_ms = now_ms;
            sched->last_sample_chunks = sched->chunks_completed;
        }
        
        LeaveCriticalSection(&sched->lock);
    }
    
    return 0;
}

int init_worldgen_scheduler(const SdkWorldGenSchedulerConfig* config)
{
    SdkWorldGenSchedulerInternal* impl;
    int worker_count;
    int i;
    
    if (!config) return 0;
    
    if (g_worldgen_scheduler.initialized) {
        shutdown_worldgen_scheduler();
    }
    
    impl = &g_worldgen_scheduler;
    memset(impl, 0, sizeof(*impl));
    
    impl->world_desc = config->world_desc;
    impl->world_seed = config->world_seed ? config->world_seed : config->world_desc.seed;
    strcpy_s(impl->world_save_id, sizeof(impl->world_save_id), config->world_save_id);
    
    InitializeCriticalSection(&impl->lock);
    InitializeConditionVariable(&impl->jobs_cv);
    impl->running = 1;
    
    worker_count = config->worker_count > 0 ? config->worker_count : choose_worldgen_worker_count();
    if (worker_count < 1) worker_count = 1;
    if (worker_count > SDK_WORLDGEN_MAX_WORKERS) worker_count = SDK_WORLDGEN_MAX_WORKERS;
    impl->worker_count = worker_count;
    
    reset_worldgen_cursor_locked(impl);
    impl->initialized = 1;
    
    for (i = 0; i < worker_count; ++i) {
        SdkWorldGenWorkerParam* wp = (SdkWorldGenWorkerParam*)malloc(sizeof(SdkWorldGenWorkerParam));
        if (!wp) {
            request_shutdown_worldgen_scheduler();
            while (!poll_shutdown_worldgen_scheduler()) {
                Sleep(0);
            }
            return 0;
        }
        
        wp->sched = impl;
        wp->worker_index = i;
        
        impl->workers_worldgen[i] = (SdkWorldGen*)calloc(1, sizeof(SdkWorldGen));
        if (!impl->workers_worldgen[i]) {
            free(wp);
            request_shutdown_worldgen_scheduler();
            while (!poll_shutdown_worldgen_scheduler()) {
                Sleep(0);
            }
            return 0;
        }
        
        sdk_worldgen_init_ex(impl->workers_worldgen[i], &impl->world_desc, SDK_WORLDGEN_CACHE_DISK);
        if (!impl->workers_worldgen[i]->impl) {
            free(wp);
            request_shutdown_worldgen_scheduler();
            while (!poll_shutdown_worldgen_scheduler()) {
                Sleep(0);
            }
            return 0;
        }
        wp->worldgen = impl->workers_worldgen[i];
        
        impl->workers_param[i] = wp;
        impl->workers_thread[i] = CreateThread(NULL, 0, worldgen_worker_proc, wp, 0, NULL);
        if (!impl->workers_thread[i]) {
            free(wp);
            request_shutdown_worldgen_scheduler();
            while (!poll_shutdown_worldgen_scheduler()) {
                Sleep(0);
            }
            return 0;
        }
    }
    
    return 1;
}

void request_shutdown_worldgen_scheduler(void)
{
    if (!g_worldgen_scheduler.initialized) return;
    
    EnterCriticalSection(&g_worldgen_scheduler.lock);
    if (!g_worldgen_scheduler.running) {
        LeaveCriticalSection(&g_worldgen_scheduler.lock);
        return;
    }
    g_worldgen_scheduler.running = 0;
    g_worldgen_scheduler.job_head = 0;
    g_worldgen_scheduler.job_count = 0;
    WakeAllConditionVariable(&g_worldgen_scheduler.jobs_cv);
    LeaveCriticalSection(&g_worldgen_scheduler.lock);
}

int poll_shutdown_worldgen_scheduler(void)
{
    if (!g_worldgen_scheduler.initialized) return 1;
    
    for (int i = 0; i < g_worldgen_scheduler.worker_count; ++i) {
        if (g_worldgen_scheduler.workers_thread[i]) {
            DWORD wait_result = WaitForSingleObject(g_worldgen_scheduler.workers_thread[i], 0);
            if (wait_result == WAIT_TIMEOUT) {
                return 0;
            }
            CloseHandle(g_worldgen_scheduler.workers_thread[i]);
            g_worldgen_scheduler.workers_thread[i] = NULL;
        }
        sdk_worldgen_shutdown(g_worldgen_scheduler.workers_worldgen[i]);
        free(g_worldgen_scheduler.workers_worldgen[i]);
        g_worldgen_scheduler.workers_worldgen[i] = NULL;
        if (g_worldgen_scheduler.workers_param[i]) {
            free(g_worldgen_scheduler.workers_param[i]);
            g_worldgen_scheduler.workers_param[i] = NULL;
        }
    }
    
    DeleteCriticalSection(&g_worldgen_scheduler.lock);
    memset(&g_worldgen_scheduler, 0, sizeof(g_worldgen_scheduler));
    return 1;
}

void shutdown_worldgen_scheduler(void)
{
    request_shutdown_worldgen_scheduler();
    while (!poll_shutdown_worldgen_scheduler()) {
        Sleep(0);
    }
}

void get_worldgen_scheduler_stats(SdkWorldGenSchedulerStats* out_stats)
{
    if (!out_stats) return;
    memset(out_stats, 0, sizeof(*out_stats));
    
    if (!g_worldgen_scheduler.initialized) return;
    
    EnterCriticalSection(&g_worldgen_scheduler.lock);
    out_stats->worker_count = g_worldgen_scheduler.worker_count;
    out_stats->queued_jobs = g_worldgen_scheduler.job_count;
    out_stats->active_workers = g_worldgen_scheduler.active_workers;
    out_stats->current_ring = g_worldgen_scheduler.bulk_ring;
    out_stats->superchunks_completed = g_worldgen_scheduler.superchunks_completed;
    out_stats->chunks_completed = g_worldgen_scheduler.chunks_completed;
    out_stats->chunks_per_sec = g_worldgen_scheduler.chunks_per_sec;
    LeaveCriticalSection(&g_worldgen_scheduler.lock);
}

void pump_worldgen_scheduler_offline_bulk(int max_jobs)
{
    SdkWorldGenSchedulerInternal* sched = &g_worldgen_scheduler;
    int max_outstanding;
    
    if (max_jobs < 1) max_jobs = 1;
    max_outstanding = (sched->worker_count * 4) > 0 ? (sched->worker_count * 4) : 1;
    
    for (int i = 0; i < max_jobs; ++i) {
        EnterCriticalSection(&sched->lock);
        if (!sched->initialized || !sched->running) {
            LeaveCriticalSection(&sched->lock);
            return;
        }
        
        if (sched->job_count >= max_outstanding) {
            LeaveCriticalSection(&sched->lock);
            return;
        }
        
        if (sched->job_count < SDK_WORLDGEN_JOB_CAPACITY) {
            int scx, scz;
            worldgen_cursor_pop_locked(sched, &scx, &scz);
            
            int out_index = worldgen_job_queue_index(sched->job_head, sched->job_count);
            SdkWorldGenJob* job = &sched->jobs[out_index];
            job->scx = scx;
            job->scz = scz;
            sched->job_count++;
            WakeConditionVariable(&sched->jobs_cv);
        }
        
        LeaveCriticalSection(&sched->lock);
    }
}
