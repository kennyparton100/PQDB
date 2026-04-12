#ifndef SDK_SESSION_HEADLESS_H
#define SDK_SESSION_HEADLESS_H

#include "../../../World/Persistence/sdk_world_tooling.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDK_SESSION_STOP_AT_RESIDENT = 0,
    SDK_SESSION_STOP_AT_GPU_READY = 1
} SdkSessionStartStopAt;

typedef struct {
    int desired_primary;
    int resident_primary;
    int gpu_ready_primary;
    int pending_jobs;
    int pending_results;
    int pending_uploads;
    int no_cpu_mesh;
    int upload_pending;
    int gpu_mesh_generation_stale;
    int far_only_when_full_needed;
    int other_not_ready;
} SdkStartupReadinessSnapshot;

typedef struct {
    char world_id[64];
    char world_dir[MAX_PATH];
    int create_if_missing;
    SdkWorldCreateRequest create_request;
    int spawn_mode;
    int safety_radius;
    int stop_at;
    int max_iterations;
    int save_on_success;
} SdkSessionStartRequest;

typedef struct {
    int success;
    int created_world;
    int stop_at;
    int iterations;
    SdkWorldTarget target;
    SdkWorldSaveMeta meta;
    SdkWorldDesc world_desc;
    SdkSuperchunkConfig superchunk_config;
    float spawn[3];
    int spawn_cx;
    int spawn_cz;
    SdkStartupReadinessSnapshot readiness;
    int persist_store_attempts;
    int persist_store_successes;
    int persisted_chunk_count;
    int persist_encode_auto_failures;
    int persist_cell_rle_failures;
    int persist_fluids_failures;
    int persist_construction_failures;
    uint64_t total_elapsed_ms;
    uint64_t world_create_ms;
    uint64_t meta_load_ms;
    uint64_t persistence_init_ms;
    uint64_t worldgen_init_ms;
    uint64_t spawn_resolve_ms;
    uint64_t desired_primary_ms;
    uint64_t resident_ready_ms;
    uint64_t gpu_ready_ms;
    uint64_t save_write_ms;
    char failure_reason[128];
} SdkSessionStartResult;

int sdk_session_start_headless(const SdkSessionStartRequest* request,
                               SdkSessionStartResult* out_result);

#ifdef __cplusplus
}
#endif

#endif
