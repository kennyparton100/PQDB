# Engine Audit P0 Fixes Plan

Address all critical (P0) issues from the engine audit: construction registry race condition, startup streaming instability, wall system contract failures, and readiness accounting confusion.

## Issue 1: Construction Registry Crash (Highest Severity)

**Problem**: `construction_registry_rebuild_hash()` frees and rebuilds `hash_table` while worker threads read it via `find_match()`, causing use-after-free (observed pointer `0x1110111011139E6`).

**Root Cause**: No synchronization between hash table readers and writers.

**Files**: `sdk_construction_cells.h`, `sdk_construction_cells.c`

**Changes**:
1. Add `SRWLOCK lock;` to `SdkConstructionArchetypeRegistry` struct
2. Initialize lock in `sdk_construction_registry_create()` with `InitializeSRWLock()`
3. Wrap `find_match()` with `AcquireSRWLockShared()` / `ReleaseSRWLockShared()`
4. Wrap `rebuild_hash()` with `AcquireSRWLockExclusive()` / `ReleaseSRWLockExclusive()`
5. Add revision check after unlock to detect stale reads

## Issue 2: Startup/Post-Startup Streaming Instability

**Problem**: 116-second startup latency, immediate background expansion to 373+ jobs, readiness regresses after startup completes.

**Root Cause**: Startup bootstrap and background residency expansion are entangled with no clear boundary.

**Files**: `sdk_api_session_bootstrap.c`, `sdk_chunk_streamer.c`, `sdk_chunk_manager.c`

**Changes**:
1. Add explicit `startup_complete` flag to chunk manager
2. Split scheduling: `schedule_startup_priority()` vs `schedule_background_expansion()`
3. Add staged expansion with job budget limits (max 16 background jobs initially)
4. Freeze readiness accounting once startup succeeds
5. Add 2-second startup budget target with per-phase instrumentation

## Issue 3: Wall System Contract Failure

**Problem**: Wall health stuck at `W=16/0/0 N=16/0/0 E=16/0/0 S=16/0/0` - walls desired but never ready.

**Root Cause**: No clear wall completion model - wall-support loading, health reporting, and frontier streaming mixed without state machine.

**Files**: `sdk_superchunk_geometry.h`, `sdk_chunk_manager.c`, `sdk_chunk_streamer.c`

**Changes**:
1. Define `SdkWallState` enum: `PENDING`, `LOADING`, `GENERATED`, `MESHED`, `UPLOADED`, `READY`
2. Add wall progress tracking struct with per-state counters
3. Add wall-specific scheduling pass in streamer (before general frontier)
4. Make wall health reporting track state transitions, not just counts
5. Add wall completion gate before declaring superchunk ready

## Issue 4: Readiness Accounting Confusion

**Problem**: Readiness metrics (`resident=9 gpu_ready=9`) regress to lower values while player is in world.

**Root Cause**: Bootstrap readiness and runtime residency health use same counters.

**Files**: `sdk_api_session_bootstrap.c`, `sdk_chunk_manager.c`, `sdk_api_session_core.c`

**Changes**:
1. Add separate accounting structs:
   - `SdkStartupReadiness` (frozen after startup)
   - `SdkRuntimeHealth` (live metrics)
   - `SdkBackgroundBacklog` (pending work)
2. Log with distinct prefixes: `[STARTUP]`, `[RUNTIME]`, `[BACKLOG]`, `[WALL]`
3. Add monotonic invariant: startup success never "undone"
4. Update status text to show all four metrics

## Implementation Order

1. **Registry fix first** - addresses the crash surface
2. **Readiness split** - makes debugging other issues possible
3. **Startup/background separation** - prevents queue flooding
4. **Wall state machine** - fixes wall contract

## Verification Plan

- Registry: Stress test with 10+ world loads, verify no `0x111...` pointers
- Startup: Measure end-to-end time, verify < 2s target
- Readiness: Verify metrics never regress after startup completes
- Walls: Verify `W=16/16/16` before declaring world ready

## Risk Mitigation

- All changes behind `#ifdef SDK_ENGINE_AUDIT_FIXES` initially
- Add extensive logging for new code paths
- Keep old paths as fallback for first release
- Test with both low and high render distances
