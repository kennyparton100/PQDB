/**
 * sdk_far_mesh_gen_scheduler.c -- Offline LOD mesh generation scheduler.
 *
 * Iterates every persisted chunk in spiral superchunk order, loads block
 * data from persistence, calls sdk_mesh_build_chunk_far_proxy, then stores
 * the resulting BlockVertex array back into persistence via
 * sdk_persistence_store_far_mesh.
 *
 * Architecture mirrors sdk_worldgen_scheduler: a fixed-size job ring
 * buffer populated by pump_far_mesh_gen_scheduler_bulk (called each frame
 * from the frontend) and drained by a pool of worker threads.
 */
#include "sdk_far_mesh_gen_scheduler.h"
#include "../Superchunks/Geometry/sdk_superchunk_geometry.h"
#include "../Chunks/sdk_chunk.h"
#include "../../MeshBuilder/sdk_mesh_builder.h"
#include "../../API/Internal/sdk_api_internal.h"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SDK_FAR_MESH_GEN_JOB_CAPACITY 256
#define SDK_FAR_MESH_GEN_MAX_WORKERS  16

typedef struct {
    int scx;
    int scz;
} SdkFarMeshGenJob;

typedef struct {
    void* sched;
    int   worker_index;
} SdkFarMeshGenWorkerParam;

typedef struct {
    CRITICAL_SECTION  lock;
    CONDITION_VARIABLE jobs_cv;
    int running;
    int initialized;

    SdkPersistence* persistence; /* Borrowed pointer; owned by caller */

    int worker_count;
    HANDLE workers_thread[SDK_FAR_MESH_GEN_MAX_WORKERS];
    SdkFarMeshGenWorkerParam* workers_param[SDK_FAR_MESH_GEN_MAX_WORKERS];

    SdkFarMeshGenJob jobs[SDK_FAR_MESH_GEN_JOB_CAPACITY];
    int job_head;
    int job_count;

    /* Spiral cursor state */
    int bulk_scx;
    int bulk_scz;
    int bulk_dir;
    int bulk_leg_length;
    int bulk_leg_progress;
    int bulk_legs_done;
    int bulk_ring;
    int bulk_cursor_started;

    /* Stats */
    int superchunks_done;
    int chunks_done;
    int active_workers;

    ULONGLONG last_sample_ms;
    int last_sample_chunks;
    float chunks_per_sec;
} SdkFarMeshGenSchedulerInternal;

static SdkFarMeshGenSchedulerInternal g_far_mesh_gen_scheduler = {0};

/* ========================================================================= */
/* Internal helpers                                                           */
/* ========================================================================= */

static int choose_worker_count(void)
{
    SYSTEM_INFO si;
    int count;

    GetSystemInfo(&si);
    count = (int)si.dwNumberOfProcessors;
    if (count < 1) count = 1;
    if (count > SDK_FAR_MESH_GEN_MAX_WORKERS) count = SDK_FAR_MESH_GEN_MAX_WORKERS;
    return count;
}

static int job_queue_index(int head, int offset)
{
    return (head + offset) % SDK_FAR_MESH_GEN_JOB_CAPACITY;
}

static void reset_cursor_locked(SdkFarMeshGenSchedulerInternal* s)
{
    if (!s) return;
    s->bulk_cursor_started = 1;
    s->bulk_scx           = 0;
    s->bulk_scz           = 0;
    s->bulk_dir           = 0;
    s->bulk_leg_length    = 1;
    s->bulk_leg_progress  = 0;
    s->bulk_legs_done     = 0;
    s->bulk_ring          = 0;
    s->superchunks_done   = 0;
    s->chunks_done        = 0;
}

static void cursor_pop_locked(SdkFarMeshGenSchedulerInternal* s, int* out_scx, int* out_scz)
{
    int scx, scz, abs_scx, abs_scz;

    if (!s || !s->bulk_cursor_started) return;

    scx = s->bulk_scx;
    scz = s->bulk_scz;
    abs_scx = (scx < 0) ? -scx : scx;
    abs_scz = (scz < 0) ? -scz : scz;
    s->bulk_ring = (abs_scx > abs_scz) ? abs_scx : abs_scz;
    if (out_scx) *out_scx = scx;
    if (out_scz) *out_scz = scz;

    switch (s->bulk_dir) {
        case 0: s->bulk_scx++; break;
        case 1: s->bulk_scz++; break;
        case 2: s->bulk_scx--; break;
        default: s->bulk_scz--; break;
    }

    s->bulk_leg_progress++;
    if (s->bulk_leg_progress >= s->bulk_leg_length) {
        s->bulk_leg_progress = 0;
        s->bulk_dir = (s->bulk_dir + 1) & 3;
        s->bulk_legs_done++;
        if (s->bulk_legs_done >= 2) {
            s->bulk_legs_done = 0;
            s->bulk_leg_length++;
        }
    }
}

static int take_next_job_locked(SdkFarMeshGenSchedulerInternal* s, SdkFarMeshGenJob* out_job)
{
    if (!s || !out_job || s->job_count <= 0) return 0;
    *out_job  = s->jobs[s->job_head];
    s->job_head = job_queue_index(s->job_head, 1);
    s->job_count--;
    return 1;
}

/* ========================================================================= */
/* Worker thread                                                               */
/* ========================================================================= */

DWORD WINAPI far_mesh_gen_worker_proc(LPVOID param)
{
    SdkFarMeshGenWorkerParam*       wp;
    SdkFarMeshGenSchedulerInternal* sched;
    SdkMeshBuffer                   mesh_buf;

    wp = (SdkFarMeshGenWorkerParam*)param;
    if (!wp) return 0;
    sched = (SdkFarMeshGenSchedulerInternal*)wp->sched;
    if (!sched) return 0;

    sdk_mesh_buffer_init(&mesh_buf, 4096);

    for (;;) {
        SdkFarMeshGenJob job;
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
        have_job = take_next_job_locked(sched, &job);
        if (have_job) {
            sched->active_workers++;
        }
        LeaveCriticalSection(&sched->lock);

        if (!have_job) continue;

        {
            int               scx            = job.scx;
            int               scz            = job.scz;
            int               chunks_built   = 0;
            SdkSuperchunkCell cell;

            sdk_superchunk_cell_from_index(scx, scz, &cell);

            for (int local_cz = 0; local_cz < cell.chunk_span; ++local_cz) {
                for (int local_cx = 0; local_cx < cell.chunk_span; ++local_cx) {
                    int      cx = cell.interior_min_cx + local_cx;
                    int      cz = cell.interior_min_cz + local_cz;
                    SdkChunk chunk;

                    memset(&chunk, 0, sizeof(chunk));
                    chunk.cx = cx;
                    chunk.cz = cz;

                    chunk.blocks = (SdkWorldCellCode*)calloc(CHUNK_TOTAL_BLOCKS,
                                                             sizeof(SdkWorldCellCode));
                    if (!chunk.blocks) continue;

                    if (!sdk_persistence_load_chunk(sched->persistence, cx, cz, &chunk)) {
                        free(chunk.blocks);
                        continue;
                    }

                    /* Build far proxy mesh — NULL chunk manager is valid */
                    sdk_mesh_buffer_clear(&mesh_buf);
                    sdk_mesh_build_chunk_far_proxy(&chunk, NULL, &mesh_buf);

                    free(chunk.blocks);
                    chunk.blocks = NULL;

                    /* Persist the far mesh (may be 0 verts for empty chunks) */
                    sdk_persistence_store_far_mesh(sched->persistence,
                                                   cx, cz,
                                                   mesh_buf.vertices,
                                                   mesh_buf.count);
                    chunks_built++;
                }
            }

            if (chunks_built > 0) {
                sdk_persistence_save(sched->persistence);
            }

            EnterCriticalSection(&sched->lock);
            sched->active_workers--;
            sched->superchunks_done++;
            sched->chunks_done += chunks_built;

            {
                ULONGLONG now_ms = GetTickCount64();
                if (sched->last_sample_ms == 0) {
                    sched->last_sample_ms     = now_ms;
                    sched->last_sample_chunks = sched->chunks_done;
                } else if (now_ms - sched->last_sample_ms >= 1000u) {
                    ULONGLONG delta_ms    = now_ms - sched->last_sample_ms;
                    int       delta_chunks = sched->chunks_done - sched->last_sample_chunks;
                    if (delta_ms > 0u) {
                        sched->chunks_per_sec =
                            (float)delta_chunks * 1000.0f / (float)delta_ms;
                    }
                    sched->last_sample_ms     = now_ms;
                    sched->last_sample_chunks = sched->chunks_done;
                }
            }
            LeaveCriticalSection(&sched->lock);
        }
    }

    sdk_mesh_buffer_free(&mesh_buf);
    return 0;
}

/* ========================================================================= */
/* Public API                                                                  */
/* ========================================================================= */

int init_far_mesh_gen_scheduler(const SdkFarMeshGenSchedulerConfig* config,
                                 SdkPersistence* persistence)
{
    SdkFarMeshGenSchedulerInternal* impl;
    int worker_count;
    int i;

    if (!config || !persistence) return 0;

    if (g_far_mesh_gen_scheduler.initialized) {
        shutdown_far_mesh_gen_scheduler();
    }

    impl = &g_far_mesh_gen_scheduler;
    memset(impl, 0, sizeof(*impl));

    impl->persistence = persistence;

    InitializeCriticalSection(&impl->lock);
    InitializeConditionVariable(&impl->jobs_cv);
    impl->running = 1;

    worker_count = config->worker_count > 0 ? config->worker_count : choose_worker_count();
    if (worker_count < 1) worker_count = 1;
    if (worker_count > SDK_FAR_MESH_GEN_MAX_WORKERS) worker_count = SDK_FAR_MESH_GEN_MAX_WORKERS;
    impl->worker_count = worker_count;

    reset_cursor_locked(impl);
    impl->initialized = 1;

    for (i = 0; i < worker_count; ++i) {
        SdkFarMeshGenWorkerParam* wp =
            (SdkFarMeshGenWorkerParam*)malloc(sizeof(SdkFarMeshGenWorkerParam));
        if (!wp) {
            request_shutdown_far_mesh_gen_scheduler();
            while (!poll_shutdown_far_mesh_gen_scheduler()) Sleep(0);
            return 0;
        }
        wp->sched        = impl;
        wp->worker_index = i;
        impl->workers_param[i]  = wp;
        impl->workers_thread[i] = CreateThread(NULL, 0, far_mesh_gen_worker_proc, wp, 0, NULL);
        if (!impl->workers_thread[i]) {
            free(wp);
            impl->workers_param[i] = NULL;
            request_shutdown_far_mesh_gen_scheduler();
            while (!poll_shutdown_far_mesh_gen_scheduler()) Sleep(0);
            return 0;
        }
    }

    return 1;
}

void request_shutdown_far_mesh_gen_scheduler(void)
{
    SdkFarMeshGenSchedulerInternal* impl = &g_far_mesh_gen_scheduler;
    if (!impl->initialized) return;

    EnterCriticalSection(&impl->lock);
    impl->running = 0;
    WakeAllConditionVariable(&impl->jobs_cv);
    LeaveCriticalSection(&impl->lock);
}

int poll_shutdown_far_mesh_gen_scheduler(void)
{
    SdkFarMeshGenSchedulerInternal* impl = &g_far_mesh_gen_scheduler;
    int i;
    int all_done = 1;

    if (!impl->initialized) return 1;

    for (i = 0; i < impl->worker_count; ++i) {
        if (impl->workers_thread[i]) {
            DWORD result = WaitForSingleObject(impl->workers_thread[i], 0);
            if (result != WAIT_OBJECT_0) {
                all_done = 0;
            }
        }
    }
    return all_done;
}

void shutdown_far_mesh_gen_scheduler(void)
{
    SdkFarMeshGenSchedulerInternal* impl = &g_far_mesh_gen_scheduler;
    int i;

    if (!impl->initialized) return;
    request_shutdown_far_mesh_gen_scheduler();

    for (i = 0; i < impl->worker_count; ++i) {
        if (impl->workers_thread[i]) {
            WaitForSingleObject(impl->workers_thread[i], INFINITE);
            CloseHandle(impl->workers_thread[i]);
            impl->workers_thread[i] = NULL;
        }
        if (impl->workers_param[i]) {
            free(impl->workers_param[i]);
            impl->workers_param[i] = NULL;
        }
    }

    DeleteCriticalSection(&impl->lock);
    impl->initialized = 0;
}

void get_far_mesh_gen_scheduler_stats(SdkFarMeshGenSchedulerStats* out_stats)
{
    SdkFarMeshGenSchedulerInternal* impl = &g_far_mesh_gen_scheduler;

    if (!out_stats) return;
    memset(out_stats, 0, sizeof(*out_stats));
    if (!impl->initialized) return;

    EnterCriticalSection(&impl->lock);
    out_stats->worker_count     = impl->worker_count;
    out_stats->queued_jobs      = impl->job_count;
    out_stats->active_workers   = impl->active_workers;
    out_stats->current_ring     = impl->bulk_ring;
    out_stats->superchunks_done = impl->superchunks_done;
    out_stats->chunks_done      = impl->chunks_done;
    out_stats->chunks_per_sec   = impl->chunks_per_sec;
    LeaveCriticalSection(&impl->lock);
}

void pump_far_mesh_gen_scheduler_bulk(int max_jobs)
{
    SdkFarMeshGenSchedulerInternal* impl = &g_far_mesh_gen_scheduler;
    int pumped = 0;

    if (!impl->initialized || max_jobs <= 0) return;

    EnterCriticalSection(&impl->lock);
    while (pumped < max_jobs &&
           impl->job_count < SDK_FAR_MESH_GEN_JOB_CAPACITY &&
           impl->bulk_cursor_started) {
        int write_index;
        int scx = 0, scz = 0;

        cursor_pop_locked(impl, &scx, &scz);

        write_index = job_queue_index(impl->job_head, impl->job_count);
        impl->jobs[write_index].scx = scx;
        impl->jobs[write_index].scz = scz;
        impl->job_count++;
        pumped++;
    }
    if (pumped > 0) {
        WakeAllConditionVariable(&impl->jobs_cv);
    }
    LeaveCriticalSection(&impl->lock);
}
