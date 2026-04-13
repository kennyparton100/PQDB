/**
 * sdk_far_mesh_gen_scheduler.h -- Offline LOD mesh generation scheduler.
 *
 * Iterates every persisted chunk in a world save, calls
 * sdk_mesh_build_chunk_far_proxy on each one, encodes the resulting vertex
 * buffer, and stores it back via sdk_persistence_store_far_mesh.
 *
 * Mirrors sdk_worldgen_scheduler in structure.  Jobs are dispatched in
 * spiral superchunk order.
 */
#ifndef NQLSDK_FAR_MESH_GEN_SCHEDULER_H
#define NQLSDK_FAR_MESH_GEN_SCHEDULER_H

#include "../Persistence/sdk_persistence.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char world_save_id[64];
    int  worker_count;         /* 0 = auto (cpu count) */
} SdkFarMeshGenSchedulerConfig;

typedef struct {
    int worker_count;
    int queued_jobs;
    int active_workers;
    int current_ring;
    int superchunks_done;
    int chunks_done;
    float chunks_per_sec;
} SdkFarMeshGenSchedulerStats;

/**
 * Initialise the scheduler.  Opens the world persistence at the save path
 * derived from world_save_id, then starts worker threads.
 *
 * @param config    Scheduler configuration (save id, worker count).
 * @param persistence Pointer to an already-initialised SdkPersistence.
 * @return 1 on success, 0 on failure.
 */
int  init_far_mesh_gen_scheduler(const SdkFarMeshGenSchedulerConfig* config,
                                  SdkPersistence* persistence);

/** Request a graceful shutdown (sets running=0, wakes workers). */
void request_shutdown_far_mesh_gen_scheduler(void);

/**
 * Poll whether all workers have exited.
 * @return 1 when fully shut down, 0 if still running.
 */
int  poll_shutdown_far_mesh_gen_scheduler(void);

/** Forcibly join all workers (blocks). */
void shutdown_far_mesh_gen_scheduler(void);

/** Copy current stats into *out_stats. */
void get_far_mesh_gen_scheduler_stats(SdkFarMeshGenSchedulerStats* out_stats);

/**
 * Pump up to max_jobs new superchunk jobs into the queue from the spiral
 * cursor.  Call this every frame from the frontend update function.
 */
void pump_far_mesh_gen_scheduler_bulk(int max_jobs);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_FAR_MESH_GEN_SCHEDULER_H */
