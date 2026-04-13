# Startup and Post-Startup Streaming Instability (P0)

**Severity:** P0 (Critical - Latency/Instability)
**Status:** Research Complete, Pending Implementation
**Assigned:** Engine Team  
**Date Identified:** 2026-04-10

---

## Executive Summary

The bootstrap/background boundary is **ill-defined**, causing 116+ second startup loops with no clear completion signal. Post-startup, background expansion floods the streaming queue and stalls foreground chunks. The root cause is entangled code paths and mixed readiness accounting that prevents clean handoff between bootstrap and runtime streaming phases.

---

## Problem Statement

### Symptom 1: Extended Bootstrap Duration
- Bootstrap visible chunks loop runs for 116+ seconds
- No clear signal when "startup streaming" transitions to "background streaming"
- Session startup sometimes succeeds, sometimes loops indefinitely

### Symptom 2: Post-Startup Streaming Instability  
- Background expansion (`background_expansion=true`) floods queue immediately after startup
- Foreground chunks (near player) get stalled behind background work
- User experiences hitches and delayed chunk loading after session is "ready"

---

## Root Cause Analysis

### Root Cause 1: Bootstrap Loop with No Termination Guard

**Location:** `sdk_api_session_bootstrap.c:87-215`

```c
int bootstrap_visible_chunks_sync(void)
{
    // ...
    
    collect_startup_chunk_readiness(&readiness);
    last_progress_readiness = readiness;
    while (!g_world_generation_cancel_requested &&
           readiness.resident_primary < sync_target_total) {  // <-- PHASE 1 LOOP
        // ... process streaming ...
        collect_startup_chunk_readiness(&readiness);
    }
    // ...
    
    collect_startup_chunk_readiness(&readiness);
    last_progress_readiness = readiness;
    last_progress_ms = GetTickCount64();
    while (!g_world_generation_cancel_requested &&
           readiness.desired_primary > 0 &&
           readiness.gpu_ready_primary < readiness.desired_primary) {  // <-- PHASE 2 LOOP
        // ... process streaming ...
        collect_startup_chunk_readiness(&readiness);
    }
    // ...
    
    sdk_chunk_streamer_schedule_startup_priority(&g_sdk.chunk_streamer,  // <-- RESTARTS!
                                                 &g_sdk.chunk_mgr,
                                                 startup_safe_primary_radius(),
                                                 0);  // max_pending_jobs = 0 means unlimited!
    // ...
}
```

**The Problem:**
1. Phase 1 waits for `resident_primary >= sync_target_total` (CPU-side ready)
2. Phase 2 waits for `gpu_ready_primary >= desired_primary` (GPU-side ready)
3. Then immediately calls `schedule_startup_priority()` with `max_pending_jobs=0` (unlimited)
4. This floods the queue with background work before startup is truly "done"

### Root Cause 2: Mixed Readiness Accounting

**Location:** `sdk_api_session_core.c:185-269`

```c
void collect_startup_chunk_readiness(SdkStartupChunkReadiness* out_readiness)
{
    // This function is called:
    // - During bootstrap (expected: monotonic increase)
    // - During runtime per-frame (expected: live health metrics)
    // - During safe-mode exit check (line 933)
    
    // Result: "readiness" values can DECREASE after being reported as "ready"
    // causing apparent regressions and confusing the state machine
}
```

**The Problem:**
- Same `SdkStartupChunkReadiness` struct used for:
  - **Bootstrap progress tracking** (should only increase)
  - **Runtime health monitoring** (can fluctuate)
  - **Safe-mode exit criteria** (needs stable signal)

### Root Cause 3: Background Expansion Entanglement

**Location:** `sdk_api.c:3366` (background expansion callback)

```c
// Post-startup, background expansion is enabled
g_sdk.chunk_streamer.background_expansion = true;

// This causes the streamer to continuously add chunks to the job queue
// even when startup streaming should be winding down
```

**The Problem:**
- No clear boundary between "startup priority queue" and "background queue"
- Background expansion races with startup completion
- Worker threads process mixed-priority work without discrimination

---

## Affected Code Paths

### 1. Bootstrap Synchronous Path
- **File:** `sdk_api_session_bootstrap.c:87-215` (`bootstrap_visible_chunks_sync()`)
- **Calls:** `collect_startup_chunk_readiness()` in tight loop
- **Problem:** No clear termination, restarts streaming after "completion"

### 2. Startup Priority Scheduling
- **File:** `sdk_chunk_streamer.c:1278-1320` (`sdk_chunk_streamer_schedule_startup_priority()`)
- **Parameter:** `max_pending_jobs=0` means unlimited jobs
- **Problem:** Called during bootstrap AND post-bootstrap with different semantics

### 3. Safe Mode Tracking
- **File:** `sdk_api_session_core.c:930-951` (`update_startup_safe_mode_status()`)
- **Calls:** `collect_startup_chunk_readiness()` every frame
- **Problem:** Uses same counters as bootstrap, sees "regressions"

### 4. Background Expansion Toggle
- **File:** `sdk_api.c:3366`
- **Problem:** Enabled immediately post-startup without queue flush

---

## Data Structures

### Current Readiness Structure
```c
// sdk_api_session_internal.h (approximate location)
typedef struct SdkStartupChunkReadiness {
    int desired_primary;        // Target count (bootstrap AND runtime)
    int resident_primary;       // Currently resident (bootstrap AND runtime)
    int gpu_ready_primary;      // GPU uploaded (bootstrap AND runtime)
    int active_workers;         // Currently running worker threads
    int pending_jobs;           // Jobs in queue waiting for workers
    int pending_results;        // Completed results waiting for adoption
    int no_cpu_mesh;            // Resident but no CPU mesh yet
    int upload_pending;         // CPU mesh ready, waiting for GPU upload
    int gpu_mesh_generation_stale;  // GPU mesh needs regeneration
} SdkStartupChunkReadiness;
```

### Proposed Separation

```c
typedef struct SdkBootstrapProgress {
    // Monotonic progress tracking (only increases during bootstrap)
    int desired_primary;
    int resident_primary;
    int gpu_ready_primary;
    int target_total;
    ULONGLONG start_time_ms;
    int phase;  // 0=init, 1=resident, 2=gpu, 3=complete
} SdkBootstrapProgress;

typedef struct SdkRuntimeChunkHealth {
    // Live metrics (can fluctuate during runtime)
    int desired_count;
    int resident_count;
    int gpu_ready_count;
    int pending_jobs;
    int pending_results;
    float fill_ratio;
} SdkRuntimeChunkHealth;
```

---

## Proposed Fix

### Phase 1: Define Clear Bootstrap Phases

#### 1.1 Add Explicit Phase Enum
```c
typedef enum SdkBootstrapPhase {
    SDK_BOOTSTRAP_PHASE_INIT = 0,
    SDK_BOOTSTRAP_PHASE_RESIDENT,      // Phase 1: CPU-side generation
    SDK_BOOTSTRAP_PHASE_GPU_UPLOAD,    // Phase 2: GPU mesh upload
    SDK_BOOTSTRAP_PHASE_COMPLETE,      // Handoff to runtime
    SDK_BOOTSTRAP_PHASE_COUNT
} SdkBootstrapPhase;
```

#### 1.2 Modify Bootstrap Function
```c
int bootstrap_visible_chunks_sync(void)
{
    SdkBootstrapPhase phase = SDK_BOOTSTRAP_PHASE_RESIDENT;
    
    // PHASE 1: Resident (CPU-side)
    while (phase == SDK_BOOTSTRAP_PHASE_RESIDENT) {
        collect_bootstrap_progress(&progress);
        if (progress.resident_primary >= progress.target_total) {
            phase = SDK_BOOTSTRAP_PHASE_GPU_UPLOAD;
        }
        // ... process streaming ...
    }
    
    // PHASE 2: GPU Upload
    while (phase == SDK_BOOTSTRAP_PHASE_GPU_UPLOAD) {
        collect_bootstrap_progress(&progress);
        if (progress.gpu_ready_primary >= progress.desired_primary) {
            phase = SDK_BOOTSTRAP_PHASE_COMPLETE;
        }
        // ... process streaming ...
    }
    
    // PHASE 3: Complete - Explicit handoff
    sdk_chunk_streamer_mark_startup_complete(&g_sdk.chunk_streamer);
    // DON'T call schedule_startup_priority again here!
    
    return 1;
}
```

### Phase 2: Separate Bootstrap from Runtime Metrics

#### 2.1 Create Separate Collection Functions
```c
// For bootstrap - monotonic counters
void collect_bootstrap_progress(SdkBootstrapProgress* out_progress)
{
    static int max_resident = 0;
    static int max_gpu_ready = 0;
    
    // Calculate current state
    int current_resident = ...;
    int current_gpu = ...;
    
    // Only increase, never decrease
    max_resident = max(max_resident, current_resident);
    max_gpu_ready = max(max_gpu_ready, current_gpu);
    
    out_progress->resident_primary = max_resident;
    out_progress->gpu_ready_primary = max_gpu_ready;
}

// For runtime - live metrics
void collect_runtime_chunk_health(SdkRuntimeChunkHealth* out_health)
{
    // Current state, may fluctuate
    out_health->resident_count = ...;
    out_health->gpu_ready_count = ...;
    // ...
}
```

#### 2.2 Update Call Sites
| Original Call | New Call | Context |
|--------------|----------|---------|
| `collect_startup_chunk_readiness()` in bootstrap | `collect_bootstrap_progress()` | Monotonic tracking |
| `collect_startup_chunk_readiness()` in safe-mode | `collect_bootstrap_progress()` | Exit criteria |
| `collect_startup_chunk_readiness()` in debug HUD | `collect_runtime_chunk_health()` | Live display |

### Phase 3: Queue Isolation

#### 3.1 Add Startup Queue Separation
```c
typedef struct SdkChunkStreamer {
    // ... existing fields ...
    
    // Separate queues for startup vs background
    SdkChunkJobQueue startup_queue;      // High priority, limited
    SdkChunkJobQueue background_queue;   // Normal priority, unlimited
    
    // Startup completion flag
    int startup_complete;
    
    // Limits
    int max_startup_jobs;                // e.g., 32
    int max_background_jobs;             // e.g., 64
} SdkChunkStreamer;
```

#### 3.2 Startup Completion Handler
```c
void sdk_chunk_streamer_mark_startup_complete(SdkChunkStreamer* streamer)
{
    streamer->startup_complete = 1;
    
    // Flush or transfer remaining startup jobs
    // Don't accept new startup-priority jobs
    // Enable background expansion only after flush
    
    // Move any pending startup jobs to background queue
    sdk_chunk_job_queue_transfer_all(&streamer->startup_queue, 
                                      &streamer->background_queue);
}
```

### Phase 4: Background Expansion Timing

#### 4.1 Delayed Background Enable
```c
// In session initialization completion
void complete_session_startup(void)
{
    // 1. Mark startup complete
    sdk_chunk_streamer_mark_startup_complete(&g_sdk.chunk_streamer);
    
    // 2. Process remaining startup results
    process_streamed_chunk_results_with_budget(INT_MAX, 0.0f);
    
    // 3. NOW enable background expansion
    g_sdk.chunk_streamer.background_expansion = true;
}
```

---

## Testing Strategy

### 1. Bootstrap Duration Test
```c
// Measure time from session start to bootstrap completion
// Verify completion in < 30 seconds (not 116+)
```

### 2. Phase Transition Test
```c
// Instrument phase transitions
// Verify: RESIDENT → GPU_UPLOAD → COMPLETE (never backwards)
```

### 3. Queue Isolation Test
```c
// Fill background queue with distant chunks
// Verify foreground chunks still get processed
```

### 4. Regression Test
```c
// Cancel and restart session multiple times
// Verify no state corruption between sessions
```

---

## Files to Modify

| File | Lines | Change |
|------|-------|--------|
| `sdk_api_session_internal.h` | ~350-370 | Add `SdkBootstrapPhase` enum, separate progress/health structs |
| `sdk_api_session_bootstrap.c:87-215` | Bootstrap function | Add phase tracking, remove restart call |
| `sdk_api_session_bootstrap.c:185-269` | `collect_startup_chunk_readiness()` | Split into two functions |
| `sdk_chunk_streamer.h` | Struct definition | Add startup queue, completion flag |
| `sdk_chunk_streamer.c` | Queue management | Add startup/background separation |
| `sdk_api.c:3366` | Background expansion | Delay enable until after startup complete |
| `sdk_api_session_core.c:930-951` | Safe mode tracking | Use `collect_bootstrap_progress()` |

---

## Related Issues

- **Construction Registry Crash:** Worker threads affected by streaming instability may contribute to this crash
- **Readiness Accounting Confusion:** Same root cause - mixed use of readiness metrics
- **Wall System Contract Failure:** Wall chunk loading affected by startup streaming issues

---

## References

- Original Audit Report: `../engine_audit_2026-04-10.md`
