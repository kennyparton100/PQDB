#ifndef SDK_PROFILER_H
#define SDK_PROFILER_H

#include <windows.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROF_ZONE_FRAME_TOTAL = 0,
    PROF_ZONE_RENDERING,
    PROF_ZONE_CHUNK_UPDATE,
    PROF_ZONE_CHUNK_STREAMING,
    PROF_ZONE_CHUNK_ADOPTION,
    PROF_ZONE_CHUNK_MESHING,
    PROF_ZONE_PHYSICS,
    PROF_ZONE_SETTLEMENT_SCAN,
    PROF_ZONE_SETTLEMENT_RUNTIME,
    PROF_ZONE_ENTITY_UPDATE,
    PROF_ZONE_INPUT,
    PROF_ZONE_DEBUG_UI,
    PROF_ZONE_COUNT
} ProfileZone;

#define PROF_HISTORY_SIZE 600

typedef struct {
    double zone_times_ms[PROF_ZONE_COUNT];
    double frame_time_ms;
    double accounted_time_ms;
    double unaccounted_time_ms;
} ProfileFrameData;

typedef struct {
    bool enabled;
    LARGE_INTEGER freq;
    FILE* log_file;
    char log_path[512];
    int frame_number;
    
    LARGE_INTEGER frame_start;
    LARGE_INTEGER zone_starts[PROF_ZONE_COUNT];
    double zone_times_ms[PROF_ZONE_COUNT];
    bool frame_in_progress;
    
    ProfileFrameData history[PROF_HISTORY_SIZE];
    int history_index;
    int history_count;
} SdkProfiler;

void sdk_profiler_init(SdkProfiler* prof);
int sdk_profiler_enable(SdkProfiler* prof, const char* log_path);
void sdk_profiler_disable(SdkProfiler* prof);
void sdk_profiler_begin_frame(SdkProfiler* prof);
void sdk_profiler_end_frame(SdkProfiler* prof);
void sdk_profiler_zone_begin(SdkProfiler* prof, ProfileZone zone);
void sdk_profiler_zone_end(SdkProfiler* prof, ProfileZone zone);
void sdk_profiler_get_last_frame(SdkProfiler* prof, ProfileFrameData* out_data);
double sdk_profiler_get_current_frame_age_ms(SdkProfiler* prof);
void sdk_profiler_log_note(SdkProfiler* prof, const char* tag, const char* note);
const char* sdk_profiler_zone_name(ProfileZone zone);

#define PROF_FRAME_BEGIN() if(g_profiler.enabled) sdk_profiler_begin_frame(&g_profiler)
#define PROF_FRAME_END() if(g_profiler.enabled) sdk_profiler_end_frame(&g_profiler)
#define PROF_ZONE_BEGIN(zone) if(g_profiler.enabled) sdk_profiler_zone_begin(&g_profiler, zone)
#define PROF_ZONE_END(zone) if(g_profiler.enabled) sdk_profiler_zone_end(&g_profiler, zone)

#ifdef __cplusplus
}
#endif

#endif
