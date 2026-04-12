# SDK WorldgenScheduler Documentation

Comprehensive documentation for the SDK world generation scheduler, managing async bulk world generation.

**Module:** `SDK/Core/World/Worldgen/`  
**Output:** `SDK/Docs/Core/World/Worldgen/SDK_WorldgenScheduler.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Configuration](#configuration)
- [Worker Thread Model](#worker-thread-model)
- [Statistics and Progress](#statistics-and-progress)
- [Key Functions](#key-functions)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The WorldgenScheduler manages asynchronous bulk world generation for offline/pre-generation scenarios. Unlike the ChunkStreamer which handles runtime chunk streaming, the scheduler focuses on generating large areas ahead of time with multiple worker threads.

**Key Features:**
- Multi-threaded worker pool for bulk generation
- Ring-based generation progression
- Progress statistics and monitoring
- Graceful shutdown handling

---

## Configuration

### SdkWorldGenSchedulerConfig Structure

```c
typedef struct {
    SdkWorldDesc world_desc;      // World generation parameters
    char world_save_id[64];       // Save identifier
    uint32_t world_seed;          // Generation seed
    int worker_count;             // Number of worker threads
} SdkWorldGenSchedulerConfig;
```

### Worker Count

```c
// Default: Hardware thread count
int worker_count = get_cpu_core_count();

// Can be reduced to limit CPU usage
int limited_workers = 2;  // Use only 2 cores
```

---

## Worker Thread Model

### Generation Rings

The scheduler generates chunks in concentric rings from a center point:

```
Ring 0: Center chunk only
Ring 1: 8 chunks around center
Ring 2: 16 chunks around ring 1
...
Ring N: Outer ring expansion

Each worker grabs the next unprocessed chunk in the current ring.
```

### Job Distribution

```
Coordinator Thread:
    - Manages ring progression
    - Assigns chunks to workers
    - Tracks completion

Worker Threads (N):
    - Pick next available chunk
    - Generate terrain
    - Save to persistence
    - Report completion
```

---

## Statistics and Progress

### SdkWorldGenSchedulerStats Structure

```c
typedef struct {
    int worker_count;           // Total workers
    int queued_jobs;            // Pending jobs
    int active_workers;         // Currently working
    int current_ring;           // Generation ring
    int superchunks_completed;  // Finished superchunks
    int chunks_completed;       // Total chunks done
    float chunks_per_sec;       // Throughput rate
} SdkWorldGenSchedulerStats;
```

### Progress Calculation

```c
// Get current stats
SdkWorldGenSchedulerStats stats;
get_worldgen_scheduler_stats(&stats);

// Estimate completion
int total_chunks = target_radius * target_radius * 4;  // Rough estimate
float progress = (float)stats.chunks_completed / total_chunks * 100.0f;
float eta_seconds = (total_chunks - stats.chunks_completed) / stats.chunks_per_sec;
```

---

## Key Functions

### Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `init_worldgen_scheduler` | `(config) → int` | Initialize with config |
| `request_shutdown_worldgen_scheduler` | `() → void` | Signal shutdown |
| `poll_shutdown_worldgen_scheduler` | `() → int` | Check if complete |
| `shutdown_worldgen_scheduler` | `() → void` | Cleanup |

### Progress

| Function | Signature | Description |
|----------|-----------|-------------|
| `get_worldgen_scheduler_stats` | `(out) → void` | Get current stats |
| `pump_worldgen_scheduler_offline_bulk` | `(max_jobs) → void` | Process jobs |

---

## API Surface

```c
#ifndef NQLSDK_WORLDGEN_SCHEDULER_H
#define NQLSDK_WORLDGEN_SCHEDULER_H

#include "sdk_worldgen.h"
#include "../Persistence/sdk_persistence.h"

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

#endif
```

---

## Integration Notes

### Pre-Generation Workflow

```c
void pregenerate_world_area(int center_cx, int center_cz, int radius) {
    SdkWorldGenSchedulerConfig config = {
        .world_desc = current_world_desc,
        .world_seed = current_world_desc.seed,
        .worker_count = 4  // Use 4 threads
    };
    snprintf(config.world_save_id, sizeof(config.world_save_id), 
             "%s", current_world_id);
    
    // Initialize scheduler
    if (!init_worldgen_scheduler(&config)) {
        log_error("Failed to init scheduler");
        return;
    }
    
    // Monitor progress
    while (!is_generation_complete()) {
        SdkWorldGenSchedulerStats stats;
        get_worldgen_scheduler_stats(&stats);
        
        printf("Progress: %d chunks, %.1f chunks/sec, ring %d\n",
               stats.chunks_completed, 
               stats.chunks_per_sec,
               stats.current_ring);
        
        // Pump scheduler
        pump_worldgen_scheduler_offline_bulk(10);
        
        Sleep(100);  // 100ms between updates
    }
    
    // Shutdown
    request_shutdown_worldgen_scheduler();
    while (!poll_shutdown_worldgen_scheduler()) {
        Sleep(10);
    }
    shutdown_worldgen_scheduler();
}
```

### Progress UI

```c
void draw_generation_progress_ui(void) {
    SdkWorldGenSchedulerStats stats;
    get_worldgen_scheduler_stats(&stats);
    
    // Background panel
    draw_rect(10, 10, 300, 80, COLOR_BLACK);
    
    // Title
    draw_text(20, 20, "Generating World...", COLOR_WHITE);
    
    // Progress bar
    float progress = estimate_progress(&stats);
    draw_progress_bar(20, 45, 280, 20, progress, COLOR_BLUE);
    
    // Stats
    char text[128];
    snprintf(text, sizeof(text), 
             "%d chunks @ %.1f/sec (ring %d)",
             stats.chunks_completed,
             stats.chunks_per_sec,
             stats.current_ring);
    draw_text(20, 70, text, COLOR_GRAY);
}
```

---

## AI Context Hints

### Custom Generation Area

```c
// Generate specific region (e.g., around player spawn)
void generate_spawn_area(int spawn_wx, int spawn_wz, int radius_chunks) {
    int spawn_cx = sdk_world_to_chunk_x(spawn_wx);
    int spawn_cz = sdk_world_to_chunk_z(spawn_wz);
    
    SdkWorldGenSchedulerConfig config = {
        .world_desc = current_world_desc,
        .world_seed = current_world_desc.seed,
        .worker_count = get_cpu_core_count()
    };
    
    // Center scheduler on spawn
    // (Implementation depends on scheduler internals)
    
    init_worldgen_scheduler(&config);
    
    // Generate only the specified radius
    // This would require scheduler modifications to support
    // bounded generation areas
}
```

### Priority Generation

```c
// Generate critical chunks first, then background
void priority_generation(int priority_cx, int priority_cz, 
                         int priority_radius,
                         int background_radius) {
    // Phase 1: High priority (sync, blocking)
    for (int dz = -priority_radius; dz <= priority_radius; dz++) {
        for (int dx = -priority_radius; dx <= priority_radius; dx++) {
            generate_chunk_sync(priority_cx + dx, priority_cz + dz);
        }
    }
    
    // Phase 2: Background (async via scheduler)
    SdkWorldGenSchedulerConfig config = {
        .world_desc = current_world_desc,
        .worker_count = 2  // Low priority, fewer workers
    };
    init_worldgen_scheduler(&config);
    // ... scheduler handles rest
}
```

---

## Related Documentation

- `SDK_Worldgen.md` - World generation API
- `SDK_ChunkStreamer.md` - Runtime chunk streaming
- `SDK_Persistence.md` - Saving generated chunks

---

**Source Files:**
- `SDK/Core/World/Worldgen/sdk_worldgen_scheduler.h` (959 bytes) - Public API
- `SDK/Core/World/Worldgen/sdk_worldgen_scheduler.c` (14,629 bytes) - Implementation
