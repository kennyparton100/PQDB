# Readiness Accounting Confusion (P0)

**Severity:** P0 (Critical - Incorrect Metrics/State Machine Confusion)
**Status:** Research Complete, Pending Implementation
**Assigned:** Engine Team  
**Date Identified:** 2026-04-10

---

## Executive Summary

The `collect_startup_chunk_readiness()` function is used for **three incompatible purposes** with conflicting expectations:
1. **Bootstrap progress tracking** - expects monotonic increase
2. **Runtime health monitoring** - expects live fluctuating metrics  
3. **Safe-mode exit criteria** - expects stable signal

This causes "progress regression" where reported `resident_primary` or `gpu_ready_primary` values decrease after being logged as "ready", confusing the state machine and making it impossible to determine when startup is truly complete.

---

## Problem Statement

### Symptom: Apparent Progress Regression
```
[READINESS] desired=49 resident=45 gpu_ready=42 | ...
[READINESS] desired=49 resident=44 gpu_ready=41 | ...  <-- WENT DOWN!
[READINESS] desired=49 resident=46 gpu_ready=43 | ...
```

### Symptom: Safe Mode Never Exits
- Safe mode tracks `gpu_ready_primary >= desired_primary` for exit
- Values fluctuate, causing exit check to fail intermittently
- Player stuck in "startup safe mode" indefinitely

### Symptom: Debug HUD Shows Inconsistent Numbers
- HUD shows different readiness values than bootstrap log
- Users report confusion about actual loading state

---

## Root Cause Analysis

### Root Cause 1: Single Function, Multiple Semantics

**Location:** `sdk_api_session_core.c:185-269`

```c
void collect_startup_chunk_readiness(SdkStartupChunkReadiness* out_readiness)
{
    // CALLED FROM:
    // 1. bootstrap_visible_chunks_sync() - wants MONOTONIC progress
    // 2. safe-mode tracking (per-frame) - wants STABLE exit criteria
    // 3. debug HUD display - wants LIVE metrics
    // 4. session headless tests - wants ACCURATE counts
    
    SdkStartupChunkReadiness readiness = {0};
    
    for (int i = 0; i < desired_count; i++) {
        const SdkChunkResidencyTarget* target = ...;
        SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, 
                                                        target->cx, target->cz);
        
        if (!chunk) {
            skip_count_no_chunk++;
            continue;  // <-- NOT COUNTED in readiness!
        }
        
        if (!chunk->blocks) {
            skip_count_no_blocks++;
            continue;  // <-- NOT COUNTED!
        }
        
        if (!target_is_primary_residency(target)) {
            skip_count_not_primary++;
            continue;  // <-- NOT COUNTED!
        }
        
        readiness.resident_primary++;
        
        if (sdk_chunk_has_full_upload_ready_mesh(chunk)) {
            readiness.gpu_ready_primary++;  // <-- Can DECREASE if mesh becomes stale!
        }
    }
    
    // ... copy to out_readiness ...
}
```

**The Problem:**
- Chunks can be **evicted** → `resident_primary` decreases
- Meshes can become **stale** → `gpu_ready_primary` decreases  
- Different callers expect different behaviors

### Root Cause 2: No Historical Tracking

**Current:** Each call recalculates from current state
```c
// Call 1: 45 chunks resident
// Call 2: 44 chunks resident (one was evicted)
// Call 3: 46 chunks resident (new one loaded)
```

**Expected for Bootstrap:** Track maximum achieved
```c
// Call 1: max_resident = 45
// Call 2: max_resident = 45 (even though current is 44)
// Call 3: max_resident = 46
```

### Root Cause 3: Mixed Counters in Single Struct

**Current Structure:** `sdk_api_session_internal.h` (lines around 340-360)

```c
typedef struct SdkStartupChunkReadiness {
    int desired_primary;        // Target count (stable during session)
    int resident_primary;       // CURRENTLY resident (fluctuates!)
    int gpu_ready_primary;      // CURRENTLY GPU ready (fluctuates!)
    int active_workers;         // CURRENT worker count
    int pending_jobs;           // CURRENT queue depth
    int pending_results;        // CURRENT results waiting
    int no_cpu_mesh;            // CURRENTLY missing CPU mesh
    int upload_pending;         // CURRENTLY pending upload
    int gpu_mesh_generation_stale;  // CURRENTLY stale
} SdkStartupChunkReadiness;
```

**The Problem:**
- `desired_primary` is stable (good for all uses)
- `resident_primary` and `gpu_ready_primary` fluctuate (bad for bootstrap tracking)
- Single struct used for incompatible purposes

---

## Affected Code Paths

### 1. Bootstrap Progress Tracking
- **File:** `sdk_api_session_bootstrap.c:120-194`
- **Function:** `bootstrap_visible_chunks_sync()`
- **Calls:** `collect_startup_chunk_readiness(&readiness)` in while loops
- **Expects:** Monotonic progress (only increases or stays same)
- **Gets:** Fluctuating values (can decrease)

### 2. Safe Mode Exit Check
- **File:** `sdk_api_session_core.c:930-951`
- **Function:** `update_startup_safe_mode_status()`
- **Calls:** `collect_startup_chunk_readiness(&readiness)` per frame
- **Expects:** Stable signal for exit criteria
- **Gets:** Fluctuating values causing intermittent exit failures

### 3. Debug HUD Display
- **File:** `sdk_api.c:3225-3235`
- **Function:** `update_per_frame_debug()`
- **Calls:** `collect_startup_chunk_readiness(&readiness)`
- **Expects:** Live accurate metrics
- **Gets:** Correct for this use case (live metrics)

### 4. Headless Session Tests
- **File:** `sdk_session_headless.c:628-642`
- **Function:** `wait_for_session_ready()`
- **Calls:** `collect_startup_chunk_readiness(&readiness)`
- **Expects:** Accurate stable signal for test completion
- **Gets:** Fluctuating values causing test flakiness

---

## Data Structures

### Current (Problematic)
```c
typedef struct SdkStartupChunkReadiness {
    int desired_primary;
    int resident_primary;       // <-- FLUCTUATES
    int gpu_ready_primary;      // <-- FLUCTUATES
    int active_workers;
    int pending_jobs;
    int pending_results;
    int no_cpu_mesh;
    int upload_pending;
    int gpu_mesh_generation_stale;
} SdkStartupChunkReadiness;
```

### Proposed: Separate Concerns

```c
// ============================================================================
// For Bootstrap - Monotonic Progress Tracking
// ============================================================================

typedef struct SdkBootstrapProgress {
    // Static target (set at bootstrap start)
    int desired_primary;
    int target_total;           // sync_target_total from bootstrap
    
    // Monotonic progress (only increases during this bootstrap)
    int max_resident_primary;   // Highest resident count achieved
    int max_gpu_ready_primary;  // Highest GPU ready count achieved
    
    // Phase tracking
    int phase;                  // 0=init, 1=resident, 2=gpu, 3=complete
    ULONGLONG phase_start_ms;   // When current phase started
    
    // Completion flags (set once, never cleared)
    int resident_phase_complete;
    int gpu_phase_complete;
    int bootstrap_complete;
} SdkBootstrapProgress;

// ============================================================================
// For Runtime - Live Health Metrics
// ============================================================================

typedef struct SdkRuntimeChunkHealth {
    // Current state (fluctuates normally)
    int desired_count;
    int resident_count;
    int gpu_ready_count;
    int pending_jobs;
    int pending_results;
    int active_workers;
    
    // Health ratios (0.0 - 1.0)
    float resident_ratio;       // resident / desired
    float gpu_ready_ratio;      // gpu_ready / desired
    float queue_pressure;       // pending_jobs / worker_count
    
    // Anomaly detection
    int regression_count;       // How many times counts decreased
    int stall_duration_ms;      // Time with no progress
} SdkRuntimeChunkHealth;

// ============================================================================
// For Safe Mode - Stable Exit Criteria  
// ============================================================================

typedef struct SdkSafeModeState {
    // Threshold (set at safe mode entry)
    int exit_threshold_resident;
    int exit_threshold_gpu;
    
    // Current readings
    int current_resident;
    int current_gpu;
    
    // Stability tracking
    int consecutive_ready_frames;   // Frames meeting criteria
    int required_ready_frames;        // e.g., 10 frames
    ULONGLONG first_ready_ms;         // When criteria first met
    
    // Exit flag (computed from stability)
    int can_exit;
} SdkSafeModeState;
```

---

## Proposed Fix

### Phase 1: Create Separate Collection Functions

#### 1.1 Bootstrap Progress Collection
```c
// File: sdk_api_session_bootstrap.c

static SdkBootstrapProgress s_bootstrap_progress = {0};

void collect_bootstrap_progress(SdkBootstrapProgress* out_progress)
{
    // Calculate current state
    int current_resident = 0;
    int current_gpu = 0;
    
    for (int i = 0; i < desired_count; i++) {
        // ... count logic ...
        current_resident++;
        if (sdk_chunk_has_full_upload_ready_mesh(chunk)) {
            current_gpu++;
        }
    }
    
    // Update monotonic maximums
    if (current_resident > s_bootstrap_progress.max_resident_primary) {
        s_bootstrap_progress.max_resident_primary = current_resident;
    }
    if (current_gpu > s_bootstrap_progress.max_gpu_ready_primary) {
        s_bootstrap_progress.max_gpu_ready_primary = current_gpu;
    }
    
    // Update completion flags
    if (!s_bootstrap_progress.resident_phase_complete) {
        if (s_bootstrap_progress.max_resident_primary >= s_bootstrap_progress.target_total) {
            s_bootstrap_progress.resident_phase_complete = 1;
        }
    }
    
    if (!s_bootstrap_progress.gpu_phase_complete) {
        if (s_bootstrap_progress.max_gpu_ready_primary >= s_bootstrap_progress.desired_primary) {
            s_bootstrap_progress.gpu_phase_complete = 1;
        }
    }
    
    *out_progress = s_bootstrap_progress;
}

void reset_bootstrap_progress(void)
{
    memset(&s_bootstrap_progress, 0, sizeof(s_bootstrap_progress));
}
```

#### 1.2 Runtime Health Collection
```c
// File: sdk_api_session_core.c (or new file)

void collect_runtime_chunk_health(SdkRuntimeChunkHealth* out_health)
{
    static int last_resident = 0;
    static int last_gpu = 0;
    
    // Calculate current state
    out_health->desired_count = ...;
    out_health->resident_count = ...;
    out_health->gpu_ready_count = ...;
    out_health->pending_jobs = ...;
    out_health->active_workers = ...;
    
    // Calculate ratios
    if (out_health->desired_count > 0) {
        out_health->resident_ratio = (float)out_health->resident_count / out_health->desired_count;
        out_health->gpu_ready_ratio = (float)out_health->gpu_ready_count / out_health->desired_count;
    }
    
    if (out_health->active_workers > 0) {
        out_health->queue_pressure = (float)out_health->pending_jobs / out_health->active_workers;
    }
    
    // Detect regression
    if (out_health->resident_count < last_resident ||
        out_health->gpu_ready_count < last_gpu) {
        out_health->regression_count++;
    }
    
    last_resident = out_health->resident_count;
    last_gpu = out_health->gpu_ready_count;
}
```

#### 1.3 Safe Mode State Tracking
```c
// File: sdk_api_session_core.c

static SdkSafeModeState s_safe_mode_state = {0};

void update_safe_mode_state(void)
{
    // Get current readings
    int current_resident = ...;  // Count currently resident
    int current_gpu = ...;       // Count currently GPU ready
    
    s_safe_mode_state.current_resident = current_resident;
    s_safe_mode_state.current_gpu = current_gpu;
    
    // Check criteria
    int criteria_met = (current_resident >= s_safe_mode_state.exit_threshold_resident &&
                        current_gpu >= s_safe_mode_state.exit_threshold_gpu);
    
    if (criteria_met) {
        if (s_safe_mode_state.consecutive_ready_frames == 0) {
            s_safe_mode_state.first_ready_ms = GetTickCount64();
        }
        s_safe_mode_state.consecutive_ready_frames++;
        
        // Require N consecutive frames before allowing exit
        if (s_safe_mode_state.consecutive_ready_frames >= s_safe_mode_state.required_ready_frames) {
            s_safe_mode_state.can_exit = 1;
        }
    } else {
        // Reset on failure - require consecutive success
        s_safe_mode_state.consecutive_ready_frames = 0;
        s_safe_mode_state.can_exit = 0;
    }
}

void init_safe_mode_state(int required_resident, int required_gpu)
{
    memset(&s_safe_mode_state, 0, sizeof(s_safe_mode_state));
    s_safe_mode_state.exit_threshold_resident = required_resident;
    s_safe_mode_state.exit_threshold_gpu = required_gpu;
    s_safe_mode_state.required_ready_frames = 10;  // Configurable
}
```

### Phase 2: Update All Call Sites

| Current Call | New Call | Context |
|--------------|----------|---------|
| `collect_startup_chunk_readiness()` in `bootstrap_visible_chunks_sync()` | `collect_bootstrap_progress()` | Bootstrap tracking |
| `collect_startup_chunk_readiness()` in `update_startup_safe_mode_status()` | `update_safe_mode_state()` + `safe_mode_can_exit()` | Exit criteria |
| `collect_startup_chunk_readiness()` in debug HUD | `collect_runtime_chunk_health()` | Live display |
| `collect_startup_chunk_readiness()` in headless tests | `collect_bootstrap_progress()` | Test completion |

### Phase 3: Deprecate Old Function

```c
// Mark as deprecated - redirect to runtime health
__attribute__((deprecated("Use collect_bootstrap_progress() or collect_runtime_chunk_health()")))
void collect_startup_chunk_readiness(SdkStartupChunkReadiness* out_readiness)
{
    // Redirect to runtime health for backward compatibility
    SdkRuntimeChunkHealth health;
    collect_runtime_chunk_health(&health);
    
    // Map fields
    out_readiness->desired_primary = health.desired_count;
    out_readiness->resident_primary = health.resident_count;
    out_readiness->gpu_ready_primary = health.gpu_ready_count;
    // ... etc
}
```

---

## Testing Strategy

### 1. Monotonic Progress Test
```c
// Simulate bootstrap with chunk eviction
// Verify max_resident_primary never decreases
// Verify completion flags set correctly
```

### 2. Safe Mode Stability Test
```c
// Simulate fluctuating readiness
// Verify safe mode requires N consecutive frames
// Verify no premature exit
```

### 3. Runtime Metrics Test
```c
// Simulate normal runtime operations
// Verify ratios calculated correctly
// Verify regression detection works
```

### 4. Integration Test
```c
// Full session start/exit cycle
// Verify bootstrap completes correctly
// Verify safe mode exits correctly
// Verify runtime metrics accurate
```

---

## Files to Modify

| File | Lines | Change |
|------|-------|--------|
| `sdk_api_session_internal.h` | ~340-360 | Add new struct definitions |
| `sdk_api_session_bootstrap.c` | New function | `collect_bootstrap_progress()` |
| `sdk_api_session_bootstrap.c:120-194` | `bootstrap_visible_chunks_sync()` | Use `collect_bootstrap_progress()` |
| `sdk_api_session_core.c` | New functions | `collect_runtime_chunk_health()`, `update_safe_mode_state()` |
| `sdk_api_session_core.c:930-951` | `update_startup_safe_mode_status()` | Use safe mode state |
| `sdk_api.c:3225-3235` | Debug HUD | Use `collect_runtime_chunk_health()` |
| `sdk_session_headless.c:628-642` | Headless tests | Use `collect_bootstrap_progress()` |

---

## Related Issues

- **Startup Streaming Instability:** Same root cause - mixed readiness tracking
- **Wall System Contract Failure:** Wall chunk readiness affected by same confusion
- **Construction Registry Crash:** Worker thread behavior affects readiness counts

---

## References

- Original Audit Report: `../engine_audit_2026-04-10.md`
