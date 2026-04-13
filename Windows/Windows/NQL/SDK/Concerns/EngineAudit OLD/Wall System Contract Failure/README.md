# Wall System Contract Failure (P0)

**Severity:** P0 (Critical - Stuck/Stalled)
**Status:** Research Complete, Pending Implementation
**Assigned:** Engine Team  
**Date Identified:** 2026-04-10

---

## Executive Summary

The superchunk wall system has an **implicit contract that is not upheld**: wall chunks are expected to load during startup and provide collision/visibility boundaries, but there's no explicit state machine tracking their completion. Wall chunks get stuck in intermediate states (generated but not meshed, meshed but not uploaded, etc.) with no recovery mechanism. The health logging (W=%d/%d N=%d/%d E=%d/%d S=%d/%d) only tracks loaded/total, not completion states.

---

## Problem Statement

### Symptom: Wall Chunks Stuck at Partial Completion
- Wall chunks are generated (blocks exist in memory)
- Wall chunks are NOT meshed (no CPU mesh built)
- Wall chunks are NOT uploaded (no GPU mesh)
- Wall health reports show `W=64/64` (all loaded) but walls are invisible/non-functional

### Symptom: Wall Bootstrap No Progress
- Log shows `[STREAM] perimeter wall bootstrap no progress loaded=0 total=4`
- Wall chunks are in the "desired" set but never reach "fully ready"
- No visibility into which state wall chunks are stuck in

---

## Root Cause Analysis

### Root Cause 1: No Wall State Machine

**Current Code:** `sdk_api_session_bootstrap.c:713-775`

```c
void collect_active_superchunk_wall_support_health(SdkBootstrapWallSideHealth* west,
                                                   SdkBootstrapWallSideHealth* north,
                                                   SdkBootstrapWallSideHealth* east,
                                                   SdkBootstrapWallSideHealth* south)
{
    // ...
    loaded = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, target->cx, target->cz) ? 1 : 0;
    
    // Only tracks: loaded (1/0) vs total
    // Does NOT track: GENERATED → MESHED → UPLOADED → READY
    
    if (adjacent_west && west) {
        west->total++;
        west->loaded += loaded;  // <-- Just 1 or 0!
    }
    // ...
}
```

**The Problem:**
- `loaded` is binary (chunk pointer exists or not)
- A chunk with `blocks` allocated but no mesh counts as `loaded=1`
- No tracking of per-chunk completion state

### Root Cause 2: Confused "Fully Ready" Check

**Location:** `sdk_api_session_bootstrap.c:339-346` and `sdk_api_session_bootstrap.c:797`

```c
int active_wall_stage_ready_count(SdkActiveWallStage stage)
{
    // ...
    if (stage == SDK_ACTIVE_WALL_STAGE_EDGE || stage == SDK_ACTIVE_WALL_STAGE_CORNER) {
        if (sdk_chunk_is_active_wall_chunk_fully_ready(&g_sdk.chunk_mgr, chunk)) {
            ready++;
        }
    } else {
        ready++;  // <-- SUPPORT stage counts as ready just by existing!
    }
}
```

```c
int finalize_active_wall_stage_sync(SdkActiveWallStage stage, int desired_chunks)
{
    // ...
    if (sdk_chunk_is_active_wall_chunk_fully_ready(&g_sdk.chunk_mgr, chunk)) continue;
    if (finalize_active_wall_chunk_sync(chunk)) {  // <-- What does this do?
        finalized++;
    }
}
```

**The Problem:**
- Different logic for SUPPORT vs EDGE/CORNER stages
- `finalize_active_wall_chunk_sync()` is unclear about what "finalization" means
- No explicit state transitions

### Root Cause 3: Missing State Transition Logic

**Desired State Machine:**
```
PENDING → GENERATED → CPU_MESHED → GPU_UPLOADED → READY
   ↑________↓_________↓_____________↓_______________↓
   (Any state can transition to PENDING on chunk eviction)
```

**Current Reality:**
```
loaded? → stuck forever in random intermediate state
```

### Root Cause 4: Wall Support vs Wall Chunk Confusion

**Location:** `sdk_api_session_internal.h:366-369`

```c
typedef struct {
    int total;
    int loaded;
} SdkBootstrapWallSideHealth;
```

**The Problem:**
- Tracks "wall support chunks" (support role)
- Wall support chunks are neighbors of actual wall chunks
- Actual wall chunks (with block 114, stone bricks) not tracked separately
- `log_wall_bootstrap_no_progress()` shows W=%d/%d but counts support chunks, not wall chunks

---

## Affected Code Paths

### 1. Wall Health Collection
- **File:** `sdk_api_session_bootstrap.c:713-775`
- **Function:** `collect_active_superchunk_wall_support_health()`
- **Problem:** Binary loaded/total, no completion states

### 2. Wall Ready Count
- **File:** `sdk_api_session_bootstrap.c:326-349`
- **Function:** `active_wall_stage_ready_count()`
- **Problem:** Different logic per stage, no explicit state checks

### 3. Wall Finalization
- **File:** `sdk_api_session_bootstrap.c:781-820`
- **Functions:** `finalize_active_wall_stage_sync()`, `finalize_active_wall_chunk_sync()`
- **Problem:** Unclear what "finalization" means

### 4. Debug Logging
- **File:** `sdk_api_session_bootstrap.c:493-518`
- **Function:** `log_wall_bootstrap_no_progress()`
- **Problem:** W=%d/%d format misleading - shows support chunks as "walls"

---

## Data Structures

### Current Health Tracking (Insufficient)
```c
// sdk_api_session_internal.h:366-369
typedef struct {
    int total;
    int loaded;
} SdkBootstrapWallSideHealth;
```

### Proposed Wall Chunk State
```c
typedef enum SdkWallChunkState {
    SDK_WALL_STATE_PENDING = 0,        // Desired but not allocated
    SDK_WALL_STATE_GENERATED,          // Has blocks array
    SDK_WALL_STATE_CPU_MESHED,         // CPU mesh built
    SDK_WALL_STATE_GPU_UPLOADED,       // GPU mesh uploaded
    SDK_WALL_STATE_READY,              // Fully ready for rendering/collision
    SDK_WALL_STATE_COUNT
} SdkWallChunkState;

typedef struct SdkWallChunkStatus {
    int cx, cz;                        // Chunk coordinates
    SdkWallChunkState state;           // Current state
    SdkActiveWallStage stage;          // support/edge/corner
    ULONGLONG state_entered_ms;        // When entered current state
    int blocked_by_cx, blocked_by_cz;  // If waiting on neighbor, which one?
} SdkWallChunkStatus;

typedef struct SdkWallSideHealth {
    int total;                         // Total wall chunks on this side
    int pending;                       // Not yet generated
    int generated;                     // Has blocks
    int cpu_meshed;                    // CPU mesh ready
    int gpu_uploaded;                  // GPU mesh ready
    int ready;                         // Fully ready
    int stalled_ms;                    // Time in current non-ready state
} SdkWallSideHealth;
```

---

## Proposed Fix

### Phase 1: Add Per-Chunk Wall State

#### 1.1 Add State to Chunk Structure
```c
// In SdkChunk (sdk_chunk.h)
typedef struct SdkChunk {
    // ... existing fields ...
    
    // Wall-specific state tracking
    SdkWallChunkState wall_state;           // Current wall state
    SdkActiveWallStage wall_stage;        // support/edge/corner
    uint32_t wall_flags;                  // SDK_WALL_FLAG_DIRTY, etc.
} SdkChunk;
```

#### 1.2 State Transition Functions
```c
void sdk_chunk_set_wall_state(SdkChunk* chunk, SdkWallChunkState new_state)
{
    if (!chunk || chunk->wall_state == new_state) return;
    
    SdkWallChunkState old_state = chunk->wall_state;
    chunk->wall_state = new_state;
    chunk->wall_state_entered_ms = GetTickCount64();
    
    // Log state transitions for debugging
    OutputDebugStringA("[WALL] chunk (%d,%d): %s → %s\n", 
                       chunk->cx, chunk->cz,
                       wall_state_name(old_state),
                       wall_state_name(new_state));
}
```

### Phase 2: Implement State Machine

#### 2.1 Define Valid Transitions
```c
static const int k_valid_wall_transitions[SDK_WALL_STATE_COUNT][SDK_WALL_STATE_COUNT] = {
    // FROM → TO:  PENDING  GENERATED  CPU_MESHED  GPU_UPLOADED  READY
    /* PENDING */     { 0,       1,         0,          0,           0 },
    /* GENERATED */   { 1,       0,         1,          0,           0 },
    /* CPU_MESHED */  { 1,       1,         0,          1,           0 },
    /* GPU_UPLOADED */{ 1,       1,         1,          0,           1 },
    /* READY */       { 1,       0,         0,          0,           0 }
    // PENDING can be reached from any state via eviction
};

int sdk_wall_state_can_transition(SdkWallChunkState from, SdkWallChunkState to)
{
    return k_valid_wall_transitions[from][to];
}
```

#### 2.2 Update State on Chunk Events
```c
// When chunk is allocated/generated
void on_chunk_generated(SdkChunk* chunk) {
    if (sdk_chunk_is_wall_chunk(chunk)) {
        sdk_chunk_set_wall_state(chunk, SDK_WALL_STATE_GENERATED);
    }
}

// When CPU mesh is built
void on_chunk_cpu_mesh_complete(SdkChunk* chunk) {
    if (chunk->wall_state == SDK_WALL_STATE_GENERATED) {
        sdk_chunk_set_wall_state(chunk, SDK_WALL_STATE_CPU_MESHED);
    }
}

// When GPU upload completes
void on_chunk_gpu_upload_complete(SdkChunk* chunk) {
    if (chunk->wall_state == SDK_WALL_STATE_CPU_MESHED) {
        sdk_chunk_set_wall_state(chunk, SDK_WALL_STATE_GPU_UPLOADED);
    }
    if (chunk->wall_state == SDK_WALL_STATE_GPU_UPLOADED) {
        // Check if all dependencies ready
        if (sdk_chunk_wall_dependencies_ready(chunk)) {
            sdk_chunk_set_wall_state(chunk, SDK_WALL_STATE_READY);
        }
    }
}

// When chunk is evicted
void on_chunk_evicted(SdkChunk* chunk) {
    sdk_chunk_set_wall_state(chunk, SDK_WALL_STATE_PENDING);
}
```

### Phase 3: Enhanced Health Tracking

#### 3.1 Detailed Health Collection
```c
void collect_wall_side_health_detailed(SdkWallSideHealth* health, 
                                        SdkSuperchunkWallSide side)
{
    memset(health, 0, sizeof(*health));
    
    for (each wall chunk on this side) {
        SdkChunk* chunk = ...;
        health->total++;
        
        switch (chunk->wall_state) {
            case SDK_WALL_STATE_PENDING:       health->pending++; break;
            case SDK_WALL_STATE_GENERATED:   health->generated++; break;
            case SDK_WALL_STATE_CPU_MESHED:  health->cpu_meshed++; break;
            case SDK_WALL_STATE_GPU_UPLOADED:health->gpu_uploaded++; break;
            case SDK_WALL_STATE_READY:       health->ready++; break;
        }
        
        if (chunk->wall_state != SDK_WALL_STATE_READY) {
            ULONGLONG stalled = GetTickCount64() - chunk->wall_state_entered_ms;
            health->stalled_ms = max(health->stalled_ms, (int)stalled);
        }
    }
}
```

#### 3.2 Enhanced Debug Logging
```c
void log_wall_health_detailed(void)
{
    SdkWallSideHealth west, north, east, south;
    collect_wall_side_health_detailed(&west, SDK_WALL_SIDE_WEST);
    collect_wall_side_health_detailed(&north, SDK_WALL_SIDE_NORTH);
    // ...
    
    sprintf_s(dbg, sizeof(dbg),
        "[WALL] West:  %d total | %d ready | %d pending | %d generated | %d meshed | %d uploaded | stalled=%dms\n"
        "[WALL] North: %d total | %d ready | %d pending | %d generated | %d meshed | %d uploaded | stalled=%dms\n"
        // ...
        , west.total, west.ready, west.pending, west.generated, 
          west.cpu_meshed, west.gpu_uploaded, west.stalled_ms
        , north.total, north.ready, north.pending, north.generated,
          north.cpu_meshed, north.gpu_uploaded, north.stalled_ms);
}
```

### Phase 4: Recovery Mechanism

#### 4.1 Stalled Chunk Detection
```c
void check_for_stalled_wall_chunks(void)
{
    for (each wall chunk) {
        if (chunk->wall_state == SDK_WALL_STATE_READY) continue;
        
        ULONGLONG stalled_ms = GetTickCount64() - chunk->wall_state_entered_ms;
        if (stalled_ms > 5000) {  // 5 seconds in same non-ready state
            // Attempt recovery
            sdk_wall_chunk_attempt_recovery(chunk);
        }
    }
}
```

#### 4.2 Recovery Actions
```c
void sdk_wall_chunk_attempt_recovery(SdkChunk* chunk)
{
    switch (chunk->wall_state) {
        case SDK_WALL_STATE_GENERATED:
            // Trigger mesh rebuild
            sdk_chunk_mark_mesh_dirty(chunk);
            break;
            
        case SDK_WALL_STATE_CPU_MESHED:
            // Trigger GPU upload
            sdk_chunk_schedule_gpu_upload(chunk);
            break;
            
        case SDK_WALL_STATE_GPU_UPLOADED:
            // Check dependencies, mark ready if deps satisfied
            if (sdk_chunk_wall_dependencies_ready(chunk)) {
                sdk_chunk_set_wall_state(chunk, SDK_WALL_STATE_READY);
            }
            break;
            
        default:
            break;
    }
}
```

---

## Testing Strategy

### 1. State Machine Validation
```c
// Verify all valid transitions work
// Verify no invalid transitions possible
// Test eviction from each state
```

### 2. Stalled Chunk Detection
```c
// Artificially delay mesh generation
// Verify detection after timeout
// Verify recovery action taken
```

### 3. Wall Completion Test
```c
// Start session with large render distance
// Monitor wall health until all sides show ready=total
// Verify walls are actually visible/collidable
```

### 4. Regression Test
```c
// Move player far from origin (trigger wall eviction)
// Return to origin
// Verify walls regenerate correctly
```

---

## Files to Modify

| File | Lines | Change |
|------|-------|--------|
| `sdk_chunk.h` | SdkChunk struct | Add `wall_state`, `wall_stage` fields |
| `sdk_api_session_internal.h:366-369` | WallSideHealth struct | Expand to detailed breakdown |
| `sdk_api_session_bootstrap.c:713-775` | `collect_active_superchunk_wall_support_health()` | Use new detailed struct |
| `sdk_api_session_bootstrap.c:326-349` | `active_wall_stage_ready_count()` | Check `wall_state == READY` |
| `sdk_api_session_bootstrap.c:781-820` | Wall finalization | State-aware recovery |
| `sdk_api_session_bootstrap.c:493-518` | `log_wall_bootstrap_no_progress()` | Detailed state output |
| `sdk_mesh_builder.c` | Mesh completion | Call `sdk_chunk_set_wall_state(CPU_MESHED)` |
| `sdk_renderer.c` | Upload completion | Call `sdk_chunk_set_wall_state(GPU_UPLOADED)` |

---

## Related Issues

- **Startup Streaming Instability:** Wall chunks affected by queue flooding
- **Readiness Accounting Confusion:** Wall chunk state conflated with general readiness
- **Construction Registry Crash:** Wall chunk generation uses construction registry

---

## References

- Superchunk Wall Documentation: `SDK_SuperchunkWalls.md`
- Original Audit Report: `../engine_audit_2026-04-10.md`
