#include "sdk_profiler.h"
#include <string.h>
#include <time.h>

static double profiler_accounted_time_ms(const ProfileFrameData* frame)
{
    /* Calculates total time accounted for by all zones except frame total */
    int i;
    double total = 0.0;

    if (!frame) return 0.0;
    for (i = 0; i < PROF_ZONE_COUNT; ++i) {
        if (i == PROF_ZONE_FRAME_TOTAL) continue;
        total += frame->zone_times_ms[i];
    }
    return total;
}

void sdk_profiler_init(SdkProfiler* prof)
{
    /* Initializes profiler with zeroed state and query frequency */
    if (!prof) return;
    memset(prof, 0, sizeof(*prof));
    QueryPerformanceFrequency(&prof->freq);
}

int sdk_profiler_enable(SdkProfiler* prof, const char* log_path)
{
    /* Enables profiling and opens CSV log file with timestamp */
    time_t now;
    struct tm tm_info;
    char timestamp[64];
    
    if (!prof || prof->enabled) return 0;
    
    time(&now);
    localtime_s(&tm_info, &now);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm_info);
    
    snprintf(prof->log_path, sizeof(prof->log_path), "%s\\profiler_log_%s.csv", 
             log_path, timestamp);
    
    prof->log_file = fopen(prof->log_path, "w");
    if (!prof->log_file) {
        return 0;
    }
    
    fprintf(prof->log_file, "Performance Profiler Log\n");
    fprintf(prof->log_file, "Started: %s\n", timestamp);
    fprintf(prof->log_file, "---\n");
    fprintf(prof->log_file, "Frame,Total_ms,Accounted_ms,Unaccounted_ms,Render_ms,ChunkUpdate_ms,ChunkStream_ms,ChunkAdopt_ms,ChunkMesh_ms,Physics_ms,SettlementScan_ms,SettlementRuntime_ms,Entity_ms,Input_ms,DebugUI_ms\n");
    fflush(prof->log_file);
    
    prof->enabled = true;
    prof->frame_number = 0;
    prof->history_index = 0;
    prof->history_count = 0;
    
    return 1;
}

void sdk_profiler_disable(SdkProfiler* prof)
{
    /* Disables profiling, writes summary statistics, closes log file */
    int i;
    double avg_frame = 0.0;
    double avg_accounted = 0.0;
    double avg_unaccounted = 0.0;
    double max_frame = 0.0;
    double min_frame = 1e9;
    double zone_avgs[PROF_ZONE_COUNT] = {0};
    
    if (!prof || !prof->enabled) return;
    
    if (prof->log_file) {
        fprintf(prof->log_file, "---\n");
        fprintf(prof->log_file, "Summary (%d frames):\n", prof->history_count);
        
        for (i = 0; i < prof->history_count; i++) {
            ProfileFrameData* frame = &prof->history[i];
            avg_frame += frame->frame_time_ms;
            avg_accounted += frame->accounted_time_ms;
            avg_unaccounted += frame->unaccounted_time_ms;
            if (frame->frame_time_ms > max_frame) max_frame = frame->frame_time_ms;
            if (frame->frame_time_ms < min_frame) min_frame = frame->frame_time_ms;
            
            for (int z = 0; z < PROF_ZONE_COUNT; z++) {
                zone_avgs[z] += frame->zone_times_ms[z];
            }
        }
        
        if (prof->history_count > 0) {
            avg_frame /= prof->history_count;
            avg_accounted /= prof->history_count;
            avg_unaccounted /= prof->history_count;
            for (i = 0; i < PROF_ZONE_COUNT; i++) {
                zone_avgs[i] /= prof->history_count;
            }
        }
        
        fprintf(prof->log_file, "Average frame time: %.2fms (%.1f fps)\n", 
                avg_frame, avg_frame > 0.0 ? (1000.0 / avg_frame) : 0.0);
        fprintf(prof->log_file, "Average accounted time: %.2fms\n", avg_accounted);
        fprintf(prof->log_file, "Average unaccounted time: %.2fms\n", avg_unaccounted);
        fprintf(prof->log_file, "Max frame time: %.2fms\n", max_frame);
        fprintf(prof->log_file, "Min frame time: %.2fms\n", min_frame);
        fprintf(prof->log_file, "\nZone Averages:\n");
        
        for (i = 0; i < PROF_ZONE_COUNT; i++) {
            double pct = (avg_frame > 0.0) ? (zone_avgs[i] / avg_frame * 100.0) : 0.0;
            fprintf(prof->log_file, "  %s: %.2fms (%.1f%%)\n",
                    sdk_profiler_zone_name((ProfileZone)i), zone_avgs[i], pct);
        }
        
        fclose(prof->log_file);
        prof->log_file = NULL;
    }
    
    prof->enabled = false;
    prof->frame_in_progress = false;
}

void sdk_profiler_begin_frame(SdkProfiler* prof)
{
    /* Marks start of frame, captures timestamp and resets zone times */
    if (!prof || !prof->enabled) return;
    QueryPerformanceCounter(&prof->frame_start);
    memset(prof->zone_times_ms, 0, sizeof(prof->zone_times_ms));
    prof->frame_in_progress = true;
}

void sdk_profiler_end_frame(SdkProfiler* prof)
{
    /* Marks end of frame, calculates timing, logs to CSV, updates history */
    LARGE_INTEGER frame_end;
    double frame_time_ms;
    double accounted_time_ms;
    double unaccounted_time_ms;
    ProfileFrameData* history_entry;
    
    if (!prof || !prof->enabled) return;
    
    QueryPerformanceCounter(&frame_end);
    frame_time_ms = ((double)(frame_end.QuadPart - prof->frame_start.QuadPart) * 1000.0) / 
                    (double)prof->freq.QuadPart;
    history_entry = &prof->history[prof->history_index];
    memset(history_entry, 0, sizeof(*history_entry));
    memcpy(history_entry->zone_times_ms, prof->zone_times_ms, sizeof(prof->zone_times_ms));
    history_entry->frame_time_ms = frame_time_ms;
    accounted_time_ms = profiler_accounted_time_ms(history_entry);
    unaccounted_time_ms = frame_time_ms - accounted_time_ms;
    if (unaccounted_time_ms < 0.0) unaccounted_time_ms = 0.0;
    history_entry->accounted_time_ms = accounted_time_ms;
    history_entry->unaccounted_time_ms = unaccounted_time_ms;
    prof->frame_in_progress = false;
    
    if (prof->log_file) {
        fprintf(prof->log_file, "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
                prof->frame_number,
                frame_time_ms,
                accounted_time_ms,
                unaccounted_time_ms,
                prof->zone_times_ms[PROF_ZONE_RENDERING],
                prof->zone_times_ms[PROF_ZONE_CHUNK_UPDATE],
                prof->zone_times_ms[PROF_ZONE_CHUNK_STREAMING],
                prof->zone_times_ms[PROF_ZONE_CHUNK_ADOPTION],
                prof->zone_times_ms[PROF_ZONE_CHUNK_MESHING],
                prof->zone_times_ms[PROF_ZONE_PHYSICS],
                prof->zone_times_ms[PROF_ZONE_SETTLEMENT_SCAN],
                prof->zone_times_ms[PROF_ZONE_SETTLEMENT_RUNTIME],
                prof->zone_times_ms[PROF_ZONE_ENTITY_UPDATE],
                prof->zone_times_ms[PROF_ZONE_INPUT],
                prof->zone_times_ms[PROF_ZONE_DEBUG_UI]);
        
        if (prof->frame_number % 60 == 0) {
            fflush(prof->log_file);
        }
    }
    
    prof->history_index = (prof->history_index + 1) % PROF_HISTORY_SIZE;
    if (prof->history_count < PROF_HISTORY_SIZE) {
        prof->history_count++;
    }
    
    prof->frame_number++;
}

void sdk_profiler_zone_begin(SdkProfiler* prof, ProfileZone zone)
{
    /* Marks start of timing zone, capturing start timestamp */
    if (!prof || !prof->enabled || zone < 0 || zone >= PROF_ZONE_COUNT) return;
    QueryPerformanceCounter(&prof->zone_starts[zone]);
}

void sdk_profiler_zone_end(SdkProfiler* prof, ProfileZone zone)
{
    /* Marks end of timing zone, adds elapsed time to zone total */
    LARGE_INTEGER end;
    double elapsed_ms;
    
    if (!prof || !prof->enabled || zone < 0 || zone >= PROF_ZONE_COUNT) return;
    
    QueryPerformanceCounter(&end);
    elapsed_ms = ((double)(end.QuadPart - prof->zone_starts[zone].QuadPart) * 1000.0) / 
                 (double)prof->freq.QuadPart;
    
    prof->zone_times_ms[zone] += elapsed_ms;
}

void sdk_profiler_get_last_frame(SdkProfiler* prof, ProfileFrameData* out_data)
{
    /* Copies the most recent frame data to out_data */
    int idx;
    if (!prof || !out_data) return;
    
    memset(out_data, 0, sizeof(*out_data));
    
    if (prof->history_count == 0) {
        return;
    }
    
    idx = (prof->history_index - 1 + PROF_HISTORY_SIZE) % PROF_HISTORY_SIZE;
    *out_data = prof->history[idx];
}

double sdk_profiler_get_current_frame_age_ms(SdkProfiler* prof)
{
    /* Returns elapsed time since current frame began in milliseconds */
    LARGE_INTEGER now;

    if (!prof || !prof->enabled || !prof->frame_in_progress) return 0.0;
    QueryPerformanceCounter(&now);
    return ((double)(now.QuadPart - prof->frame_start.QuadPart) * 1000.0) /
           (double)prof->freq.QuadPart;
}

void sdk_profiler_log_note(SdkProfiler* prof, const char* tag, const char* note)
{
    /* Writes a tagged note message to the profiler log file */
    if (!prof || !prof->enabled || !prof->log_file) return;
    fprintf(prof->log_file, "%s: %s\n",
            tag ? tag : "Note",
            note ? note : "");
    fflush(prof->log_file);
}

const char* sdk_profiler_zone_name(ProfileZone zone)
{
    /* Returns human-readable name for a profile zone enum */
    switch (zone) {
        case PROF_ZONE_FRAME_TOTAL:     return "Frame Total";
        case PROF_ZONE_RENDERING:       return "Rendering";
        case PROF_ZONE_CHUNK_UPDATE:    return "Chunk Update";
        case PROF_ZONE_CHUNK_STREAMING: return "Chunk Streaming";
        case PROF_ZONE_CHUNK_ADOPTION:  return "Chunk Adoption";
        case PROF_ZONE_CHUNK_MESHING:   return "Chunk Meshing";
        case PROF_ZONE_PHYSICS:         return "Physics";
        case PROF_ZONE_SETTLEMENT_SCAN: return "Settlement Scan";
        case PROF_ZONE_SETTLEMENT_RUNTIME:return "Settlement Runtime";
        case PROF_ZONE_ENTITY_UPDATE:   return "Entity Update";
        case PROF_ZONE_INPUT:           return "Input";
        case PROF_ZONE_DEBUG_UI:        return "Debug UI";
        default:                        return "Unknown";
    }
}
