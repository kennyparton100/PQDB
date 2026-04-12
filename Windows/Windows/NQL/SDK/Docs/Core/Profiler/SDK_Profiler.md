# SDK Profiler Documentation

Comprehensive documentation for the SDK Profiler module providing frame-based performance profiling with zone timing and CSV logging.

**Module:** `SDK/Core/Profiler/`  
**Output:** `SDK/Docs/Core/Profiler/SDK_Profiler.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Architecture](#architecture)
- [Profile Zones](#profile-zones)
- [History & Logging](#history--logging)
- [Key Functions](#key-functions)
- [Global State](#global-state)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Profiler provides lightweight frame-based performance profiling with support for hierarchical zones. It uses QueryPerformanceCounter for high-resolution timing and can log results to CSV for analysis.

**Key Features:**
- 12 predefined profile zones covering all major systems
- QueryPerformanceCounter-based high-resolution timing
- 600-frame rolling history buffer
- CSV logging with automatic timestamped filenames
- Accounted vs unaccounted time tracking
- Convenience macros for minimal overhead

---

## Architecture

### Frame Timing Flow

```
Frame Start:
    PROF_FRAME_BEGIN() or sdk_profiler_begin_frame()
           │
           └──► QueryPerformanceCounter(&frame_start)
           └──► Clear zone_times_ms array
           └──► frame_in_progress = true

Zone Timing:
    PROF_ZONE_BEGIN(PROF_ZONE_RENDERING)
           │
           └──► QueryPerformanceCounter(&zone_starts[zone])
           
    [Do work...]
    
    PROF_ZONE_END(PROF_ZONE_RENDERING)
           │
           └──► QueryPerformanceCounter(&end)
           └──► zone_times_ms[zone] += elapsed_ms

Frame End:
    PROF_FRAME_END() or sdk_profiler_end_frame()
           │
           ├──► Calculate total frame time
           ├──► Calculate accounted time (sum of zones)
           ├──► Calculate unaccounted time (total - accounted)
           ├──► Store in history ring buffer
           └──► Write to CSV log file
```

### Zone Nesting

Zones can be nested but times accumulate linearly:

```c
PROF_ZONE_BEGIN(PROF_ZONE_CHUNK_UPDATE);      // +0ms
    PROF_ZONE_BEGIN(PROF_ZONE_CHUNK_MESHING); // +0ms  
    // Meshing work: 2ms
    PROF_ZONE_END(PROF_ZONE_CHUNK_MESHING);     // +2ms
    
    PROF_ZONE_BEGIN(PROF_ZONE_CHUNK_STREAMING); // +0ms
    // Streaming work: 1ms
    PROF_ZONE_END(PROF_ZONE_CHUNK_STREAMING);   // +1ms
// Total: Chunk_Update = 3ms (includes nested zones)
PROF_ZONE_END(PROF_ZONE_CHUNK_UPDATE);          // +3ms
```

---

## Profile Zones

**ProfileZone enum** defines 12 zones:

| Zone | Name | Purpose |
|------|------|---------|
| `PROF_ZONE_FRAME_TOTAL` | Frame Total | Entire frame time |
| `PROF_ZONE_RENDERING` | Rendering | GPU rendering, draw calls |
| `PROF_ZONE_CHUNK_UPDATE` | Chunk Update | Chunk state updates |
| `PROF_ZONE_CHUNK_STREAMING` | Chunk Streaming | Async chunk loading |
| `PROF_ZONE_CHUNK_ADOPTION` | Chunk Adoption | New chunk integration |
| `PROF_ZONE_CHUNK_MESHING` | Chunk Meshing | Mesh generation |
| `PROF_ZONE_PHYSICS` | Physics | Physics simulation |
| `PROF_ZONE_SETTLEMENT_SCAN` | Settlement Scan | Settlement detection |
| `PROF_ZONE_SETTLEMENT_RUNTIME` | Settlement Runtime | Settlement simulation |
| `PROF_ZONE_ENTITY_UPDATE` | Entity Update | Entity processing |
| `PROF_ZONE_INPUT` | Input | Input handling |
| `PROF_ZONE_DEBUG_UI` | Debug UI | Debug overlay rendering |

---

## History & Logging

### History Buffer

```c
#define PROF_HISTORY_SIZE 600

typedef struct {
    double zone_times_ms[PROF_ZONE_COUNT];  // Per-zone times
    double frame_time_ms;                   // Total frame time
    double accounted_time_ms;               // Sum of all zones
    double unaccounted_time_ms;             // Frame - Accounted
} ProfileFrameData;

typedef struct {
    ProfileFrameData history[PROF_HISTORY_SIZE];  // Ring buffer
    int history_index;                             // Current position
    int history_count;                             // Total stored (max 600)
} SdkProfiler;
```

### CSV Log Format

**Filename:** `profiler_log_YYYYMMDD_HHMMSS.csv`

**Header:**
```csv
Performance Profiler Log
Started: 20240115_143022
---
Frame,Total_ms,Accounted_ms,Unaccounted_ms,Render_ms,ChunkUpdate_ms,ChunkStream_ms,ChunkAdopt_ms,ChunkMesh_ms,Physics_ms,SettlementScan_ms,SettlementRuntime_ms,Entity_ms,Input_ms,DebugUI_ms
```

**Sample Data:**
```csv
0,16.542,15.123,1.419,8.234,2.156,0.823,0.412,1.921,0.156,0.234,0.123,0.987,0.045,0.432
1,16.891,15.456,1.435,8.512,2.234,0.765,0.398,1.859,0.145,0.221,0.134,1.012,0.051,0.425
```

### Summary Output

On profiler disable, summary statistics are appended:

```
---
Summary (600 frames):
Average frame time: 16.72ms (59.8 fps)
Average accounted time: 15.29ms
Average unaccounted time: 1.43ms
Max frame time: 33.45ms
Min frame time: 14.12ms

Zone Averages:
  Frame Total: 16.72ms (100.0%)
  Rendering: 8.34ms (49.9%)
  Chunk Update: 2.18ms (13.0%)
  ...
```

---

## Key Functions

### Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_profiler_init` | `(SdkProfiler*) → void` | Initialize profiler state |
| `sdk_profiler_enable` | `(SdkProfiler*, const char* log_path) → int` | Enable and open log file |
| `sdk_profiler_disable` | `(SdkProfiler*) → void` | Disable and write summary |

### Frame Timing

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_profiler_begin_frame` | `(SdkProfiler*) → void` | Mark frame start |
| `sdk_profiler_end_frame` | `(SdkProfiler*) → void` | Mark frame end, update history |

### Zone Timing

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_profiler_zone_begin` | `(SdkProfiler*, ProfileZone) → void` | Start zone timing |
| `sdk_profiler_zone_end` | `(SdkProfiler*, ProfileZone) → void` | End zone timing |

### Queries

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_profiler_get_last_frame` | `(SdkProfiler*, ProfileFrameData*) → void` | Get last frame data |
| `sdk_profiler_get_current_frame_age_ms` | `(SdkProfiler*) → double` | Time since frame start |
| `sdk_profiler_zone_name` | `(ProfileZone) → const char*` | Get zone display name |
| `sdk_profiler_log_note` | `(SdkProfiler*, const char* tag, const char* note) → void` | Write annotation to log |

---

## Global State

```c
// Global profiler instance (defined elsewhere)
extern SdkProfiler g_profiler;

// Convenience macros (check enabled to minimize overhead)
#define PROF_FRAME_BEGIN() if(g_profiler.enabled) sdk_profiler_begin_frame(&g_profiler)
#define PROF_FRAME_END() if(g_profiler.enabled) sdk_profiler_end_frame(&g_profiler)
#define PROF_ZONE_BEGIN(zone) if(g_profiler.enabled) sdk_profiler_zone_begin(&g_profiler, zone)
#define PROF_ZONE_END(zone) if(g_profiler.enabled) sdk_profiler_zone_end(&g_profiler, zone)
```

---

## API Surface

### Public Header (sdk_profiler.h)

```c
/* Profile zones */
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

/* Frame data */
typedef struct {
    double zone_times_ms[PROF_ZONE_COUNT];
    double frame_time_ms;
    double accounted_time_ms;
    double unaccounted_time_ms;
} ProfileFrameData;

/* Profiler state */
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

/* Functions */
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

/* Macros */
#define PROF_FRAME_BEGIN() ...
#define PROF_FRAME_END() ...
#define PROF_ZONE_BEGIN(zone) ...
#define PROF_ZONE_END(zone) ...
```

---

## Integration Notes

### Basic Setup

```c
// Initialize
SdkProfiler g_profiler;
sdk_profiler_init(&g_profiler);

// Enable with logging
sdk_profiler_enable(&g_profiler, "logs/profiler");
// Creates: logs/profiler\profiler_log_YYYYMMDD_HHMMSS.csv

// Game loop
while (running) {
    PROF_FRAME_BEGIN();
    
    PROF_ZONE_BEGIN(PROF_ZONE_INPUT);
    process_input();
    PROF_ZONE_END(PROF_ZONE_INPUT);
    
    PROF_ZONE_BEGIN(PROF_ZONE_CHUNK_UPDATE);
    update_chunks();
    PROF_ZONE_END(PROF_ZONE_CHUNK_UPDATE);
    
    PROF_ZONE_BEGIN(PROF_ZONE_RENDERING);
    render_frame();
    PROF_ZONE_END(PROF_ZONE_RENDERING);
    
    PROF_FRAME_END();
}

// Cleanup
sdk_profiler_disable(&g_profiler);
```

### Runtime Toggle

```c
// Toggle profiler at runtime
void toggle_profiler() {
    if (g_profiler.enabled) {
        sdk_profiler_disable(&g_profiler);
    } else {
        sdk_profiler_enable(&g_profiler, "logs");
    }
}
```

### Frame Time Budget Check

```c
// Check if we're exceeding frame budget (16.67ms for 60fps)
void check_frame_budget() {
    double age = sdk_profiler_get_current_frame_age_ms(&g_profiler);
    if (age > 16.0) {
        // Running long, do less work this frame
        sdk_profiler_log_note(&g_profiler, "Budget", "Frame exceeded budget");
    }
}
```

### Display Stats

```c
void draw_profiler_overlay() {
    ProfileFrameData data;
    sdk_profiler_get_last_frame(&g_profiler, &data);
    
    draw_text("Frame: %.2fms (%.1f fps)", 
              data.frame_time_ms, 
              1000.0 / data.frame_time_ms);
    
    draw_text("Unaccounted: %.2fms", data.unaccounted_time_ms);
    
    for (int i = 1; i < PROF_ZONE_COUNT; i++) {
        draw_text("%s: %.2fms", 
                  sdk_profiler_zone_name(i),
                  data.zone_times_ms[i]);
    }
}
```

---

## AI Context Hints

### Adding New Zones

1. **Add to ProfileZone enum:**
   ```c
   typedef enum {
       // ... existing zones ...
       PROF_ZONE_MY_NEW_ZONE,
       PROF_ZONE_COUNT  // Keep last
   } ProfileZone;
   ```

2. **Add zone name:**
   ```c
   const char* sdk_profiler_zone_name(ProfileZone zone) {
       switch (zone) {
           // ... existing cases ...
           case PROF_ZONE_MY_NEW_ZONE: return "My New Zone";
       }
   }
   ```

3. **Add to CSV header:**
   ```c
   // In sdk_profiler_enable()
   fprintf(prof->log_file, "...,MyNewZone_ms\n");
   
   // In sdk_profiler_end_frame()
   fprintf(prof->log_file, "...,%f\n", prof->zone_times_ms[PROF_ZONE_MY_NEW_ZONE]);
   ```

### Custom Profiling

For profiling specific systems not covered by zones:

```c
typedef struct {
    double total_time;
    int call_count;
} CustomProfileData;

CustomProfileData my_system_profile = {0};

void profiled_function() {
    LARGE_INTEGER start, end, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    
    // Do work...
    
    QueryPerformanceCounter(&end);
    double elapsed = ((double)(end.QuadPart - start.QuadPart) * 1000.0) / freq.QuadPart;
    
    my_system_profile.total_time += elapsed;
    my_system_profile.call_count++;
}
```

### Profiling Specific Code Sections

```c
// Scoped profiling
#define SCOPED_PROFILE(zone) \
    for (struct { int i; } _prof = {0}; _prof.i == 0; _prof.i++, PROF_ZONE_END(zone)) \
        PROF_ZONE_BEGIN(zone)

// Usage:
void my_function() {
    SCOPED_PROFILE(PROF_ZONE_PHYSICS) {
        // Physics code here
        simulate_physics();
    }  // Automatic PROF_ZONE_END on scope exit
}
```

### Conditional Profiling

```c
// Only profile in debug builds
#ifdef DEBUG
    #define DEBUG_PROF_ZONE_BEGIN(zone) PROF_ZONE_BEGIN(zone)
    #define DEBUG_PROF_ZONE_END(zone) PROF_ZONE_END(zone)
#else
    #define DEBUG_PROF_ZONE_BEGIN(zone)
    #define DEBUG_PROF_ZONE_END(zone)
#endif
```

### Analyzing Profile Data

```c
void analyze_performance() {
    double total_accounted = 0;
    double max_zone_time = 0;
    int max_zone = -1;
    
    // Average last 60 frames
    for (int i = 0; i < 60 && i < g_profiler.history_count; i++) {
        int idx = (g_profiler.history_index - 1 - i + PROF_HISTORY_SIZE) % PROF_HISTORY_SIZE;
        ProfileFrameData* frame = &g_profiler.history[idx];
        
        for (int z = 0; z < PROF_ZONE_COUNT; z++) {
            total_accounted += frame->zone_times_ms[z];
            if (frame->zone_times_ms[z] > max_zone_time) {
                max_zone_time = frame->zone_times_ms[z];
                max_zone = z;
            }
        }
    }
    
    printf("Hot zone: %s (%.2fms average)\n",
           sdk_profiler_zone_name(max_zone), max_zone_time / 60.0);
}
```

---

## Related Documentation

- `SDK/Core/Runtime/` - Game loop integration
- `SDK/Core/World/Chunks/` - Chunk profiling zones
- `SDK/Core/Renderer/` - Rendering profiling
- `SDK/Docs/Core/Entities/SDK_Entities.md` - Entity update profiling

---

**Source Files:**
- `SDK/Core/Profiler/sdk_profiler.h` (2,197 bytes) - Public API
- `SDK/Core/Profiler/sdk_profiler.c` (8,964 bytes) - Implementation
