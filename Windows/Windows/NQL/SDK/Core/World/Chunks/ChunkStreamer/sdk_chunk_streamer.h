/**
 * sdk_chunk_streamer.h -- Asynchronous chunk generation and meshing.
 */
#ifndef NQLSDK_CHUNK_STREAMER_H
#define NQLSDK_CHUNK_STREAMER_H

#include "../sdk_chunk.h"
#include "../ChunkManager/sdk_chunk_manager.h"
#include "../../Persistence/sdk_persistence.h"
#include "../../../sdk_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int       type;
    int       gx;
    int       gz;
    int       cx;
    int       cz;
    uint8_t   space_type;
    uint8_t   reserved0;
    uint32_t  generation;
    uint32_t  dirty_mask;
    uint8_t   loaded_from_persistence;
    uint8_t   role;
    SdkChunk* built_chunk;
} SdkChunkBuildResult;

typedef enum {
    SDK_CHUNK_STREAM_RESULT_GENERATED = 0,
    SDK_CHUNK_STREAM_RESULT_REMESHED
} SdkChunkStreamResultType;

typedef struct {
    void* impl;
} SdkChunkStreamer;

typedef enum {
    SDK_CHUNK_STREAM_SCHEDULE_BOOTSTRAP_SYNC = 0,
    SDK_CHUNK_STREAM_SCHEDULE_VISIBLE_ONLY,
    SDK_CHUNK_STREAM_SCHEDULE_FULL_RUNTIME
} SdkChunkStreamSchedulePhase;

void sdk_chunk_streamer_init(SdkChunkStreamer* streamer, const SdkWorldDesc* world_desc, SdkPersistence* persistence);
void sdk_chunk_streamer_begin_shutdown(SdkChunkStreamer* streamer);
int  sdk_chunk_streamer_poll_shutdown(SdkChunkStreamer* streamer);
void sdk_chunk_streamer_shutdown(SdkChunkStreamer* streamer);

void sdk_chunk_streamer_schedule_phase(SdkChunkStreamer* streamer,
                                       const SdkChunkManager* cm,
                                       SdkChunkStreamSchedulePhase phase,
                                       int safety_radius,
                                       int max_pending_jobs);
void sdk_chunk_streamer_schedule_visible(SdkChunkStreamer* streamer, const SdkChunkManager* cm);
void sdk_chunk_streamer_schedule_visible_no_wall_support(SdkChunkStreamer* streamer, const SdkChunkManager* cm);
void sdk_chunk_streamer_schedule_startup_priority(SdkChunkStreamer* streamer,
                                                  const SdkChunkManager* cm,
                                                  int safety_radius,
                                                  int max_pending_jobs);
void sdk_chunk_streamer_schedule_wall_support(SdkChunkStreamer* streamer, const SdkChunkManager* cm, int max_pending_jobs);
int  sdk_chunk_streamer_schedule_dirty(SdkChunkStreamer* streamer, const SdkChunkManager* cm, int max_jobs);
int  sdk_chunk_streamer_pop_result(SdkChunkStreamer* streamer, SdkChunkBuildResult* out_result);
void sdk_chunk_streamer_release_result(SdkChunkBuildResult* result);
int  sdk_chunk_streamer_pending_jobs(const SdkChunkStreamer* streamer);
int  sdk_chunk_streamer_pending_results(const SdkChunkStreamer* streamer);
int  sdk_chunk_streamer_active_workers(const SdkChunkStreamer* streamer);
int  sdk_chunk_streamer_pop_dropped_remesh(SdkChunkStreamer* streamer, int* out_cx, int* out_cz, uint32_t* out_generation);
void sdk_chunk_streamer_debug_inflight_summary(const SdkChunkStreamer* streamer, char* out_text, size_t out_text_size);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_CHUNK_STREAMER_H */
