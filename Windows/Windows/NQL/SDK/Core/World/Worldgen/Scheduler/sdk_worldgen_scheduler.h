#ifndef NQLSDK_WORLDGEN_SCHEDULER_H
#define NQLSDK_WORLDGEN_SCHEDULER_H

#include "../sdk_worldgen.h"
#include "../../Persistence/sdk_persistence.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    SdkWorldDesc world_desc;
    char world_save_id[64];
    uint32_t world_seed;
    int worker_count;
} SdkWorldGenSchedulerConfig;

typedef struct {
    int worker_count;
    int queued_jobs;
    int active_workers;
    int current_ring;
    int superchunks_completed;
    int chunks_completed;
    float chunks_per_sec;
} SdkWorldGenSchedulerStats;

int init_worldgen_scheduler(const SdkWorldGenSchedulerConfig* config);
void request_shutdown_worldgen_scheduler(void);
int poll_shutdown_worldgen_scheduler(void);
void shutdown_worldgen_scheduler(void);
void get_worldgen_scheduler_stats(SdkWorldGenSchedulerStats* out_stats);
void pump_worldgen_scheduler_offline_bulk(int max_jobs);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_WORLDGEN_SCHEDULER_H */
