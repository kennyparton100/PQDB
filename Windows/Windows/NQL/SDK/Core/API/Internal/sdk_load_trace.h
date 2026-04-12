#ifndef SDK_LOAD_TRACE_H
#define SDK_LOAD_TRACE_H

#include "../../World/Persistence/sdk_world_tooling.h"

#ifdef __cplusplus
extern "C" {
#endif

void sdk_load_trace_reset(const char* reason);
void sdk_load_trace_bind_world(const char* world_id, const char* world_dir);
void sdk_load_trace_bind_meta(const SdkWorldSaveMeta* meta);
void sdk_load_trace_note(const char* event_name, const char* detail);
void sdk_load_trace_note_state(const char* event_name,
                               int frontend_view,
                               int world_session_active,
                               int world_generation_active,
                               int world_generation_stage,
                               const char* detail);
void sdk_load_trace_note_readiness(const char* event_name,
                                   int desired_primary,
                                   int resident_primary,
                                   int gpu_ready_primary,
                                   int pending_jobs,
                                   int pending_results,
                                   int pending_uploads,
                                   const char* detail);
void sdk_debug_log_append(const char* text);
void sdk_debug_log_output(const char* text);
void sdk_debug_log_printf(const char* fmt, ...);
const char* sdk_debug_log_path(void);
const char* sdk_load_trace_path(void);
int sdk_load_trace_frontend_view(void);
int sdk_load_trace_world_session_active(void);
int sdk_load_trace_world_generation_active(void);
int sdk_load_trace_world_generation_stage(void);

#ifdef __cplusplus
}
#endif

#endif
