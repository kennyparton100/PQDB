# Wall Chunk Loading Investigation Report

**Date:** 2026-04-12  
**World:** world_046  
**Status:** IN PROGRESS

---

## Executive Summary

Wall chunks are not loading in the game despite being correctly emitted as desired targets. The profiler shows wall health as `W=17/0/0 N=17/0/0 E=17/0/0 S=17/0/0` - meaning 17 wall chunks are desired per direction but 0 are resident and 0 are ready.

Additionally, chunk adoption and meshing are causing significant frame rate lag:
- `ChunkAdopt_ms`: 100-230ms per frame
- `ChunkMesh_ms`: 60-180ms per frame

---

## Key Findings

### 1. Wall Chunk Emission is Working

The `rebuild_desired()` function in `sdk_chunk_manager.c` correctly emits wall chunks via:
- `emit_superchunk_wall_ring()` - emits wall chunks along the 4 edges
- `emit_corner_adjacent_wall_chunks()` - emits chunks adjacent to corners
- `emit_corner_wall_chunks()` - emits the 4 corner wall chunks
- `emit_diagonal_corners()` - emits diagonal corner transition chunks

**Evidence from logs:**
```
[WALL] HEALTH W=17/0/0 N=17/0/0 E=17/0/0 S=17/0/0 (desired/resident/ready)
```

This shows wall chunks ARE being desired (17 per direction), but they are NOT becoming resident.

### 2. Wall Chunk Finalization Architecture

Wall chunks have a special lifecycle:

1. **Generation**: Worker threads generate wall chunks via `SDK_CHUNK_JOB_GENERATE` jobs
2. **Adoption**: `adopt_streamed_chunk_result()` adopts the generated chunk
3. **State Refresh**: `refresh_chunk_wall_finalization_state()` is called on adoption
4. **Finalization**: `process_active_wall_finalization_sync()` finalizes EDGE/CORNER chunks

**Key field:** `chunk->wall_finalized_generation` tracks which topology generation the chunk was finalized for.

**Code locations:**
- `sdk_chunk_manager.c:120-121`: `chunk_is_wall_chunk_fully_ready_for_superchunk()` checks:
  ```c
  return chunk->wall_finalized_generation == cm->topology_generation &&
         sdk_chunk_has_full_upload_ready_mesh(chunk);
  ```

- `sdk_api_session_editor_helpers.c:182-199`: `refresh_chunk_wall_finalization_state()`:
  - For EDGE/CORNER chunks: resets `wall_finalized_generation` to 0 if it doesn't match current topology
  - For other chunks: sets it to current topology (marking them as "finalized")

### 3. The Finalization Dependency Chain

`process_active_wall_finalization_sync()` has a critical dependency:

```c
// sdk_api_session_core.c:723-740
static void process_active_wall_finalization_sync(int max_chunks)
{
    int support_total = active_wall_stage_total(SDK_ACTIVE_WALL_STAGE_SUPPORT);
    int support_ready = active_wall_stage_ready_count(SDK_ACTIVE_WALL_STAGE_SUPPORT);
    if (support_ready < support_total) return;  // EARLY EXIT if SUPPORT not ready

    // Only then finalize EDGE and CORNER
    remaining -= finalize_active_wall_stage_sync(SDK_ACTIVE_WALL_STAGE_EDGE, remaining);
    if (remaining > 0) {
        finalize_active_wall_stage_sync(SDK_ACTIVE_WALL_STAGE_CORNER, remaining);
    }
}
```

**Problem:** EDGE and CORNER wall chunks will NEVER be finalized until ALL SUPPORT chunks are ready.

### 4. Async Loading Path Issues

In `rebuild_desired()` (sdk_chunk_manager.c:852-925):

When `g_graphics_settings.superchunk_load_mode == SDK_SUPERCHUNK_LOAD_ASYNC` and a superchunk transition is active:

```c
if (cm->async_load_active) {
    process_async_rows(cm);  // Only emits PRIMARY chunks
    
    // Also emit frontier chunks and walls for new superchunk
    emit_frontier_chunks(cm, cm->async_new_scx, cm->async_new_scz);
    emit_superchunk_wall_ring(cm, cm->async_new_scx, cm->async_new_scz);
    // ... more wall emission
} else {
    goto sync_rebuild;  // Full rebuild path
}
```

**Issue:** The async path emits wall chunks as desired, but these wall chunks are NOT being generated because:
1. The streamer prioritizes PRIMARY chunks first (pass 0)
2. Wall support chunks are only processed in pass 1
3. There may be a generation mismatch causing jobs to be pruned

### 5. Potential Root Causes

**Hypothesis A: Wall Chunks Never Get Finalized**
- Wall chunks are generated and adopted
- But `wall_finalized_generation` doesn't match `topology_generation`
- `process_active_wall_finalization_sync()` requires SUPPORT chunks to be ready
- SUPPORT chunks may not be getting generated/finalized properly

**Hypothesis B: Wall Chunks Not Being Generated**
- Wall chunks are desired but no jobs are queued for them
- The streamer's `prune_queued_generate_jobs()` may be removing them
- Generation mismatch between desired and queued jobs

**Hypothesis C: Topology Generation Mismatch**
- When `topology_generation` increments, existing wall chunks need re-finalization
- But the re-finalization path has a bug where it resets to 0 but never finalizes

---

## Performance Issues

### Chunk Adoption Lag (100-230ms)

From `profiler_log_20260412_154820.csv`:
```
Frame 1: ChunkAdopt_ms=231.462
Frame 2: ChunkAdopt_ms=207.917
Frame 3: ChunkAdopt_ms=192.242
```

**Causes:**
1. `process_streamed_chunk_results_with_budget()` may be adopting too many chunks in one frame
2. `sdk_chunk_manager_rebuild_lookup()` is called after each adoption (O(n) operation)
3. Wall finalization state refresh happens during adoption

### Chunk Meshing Lag (60-180ms)

From profiler:
```
Frame 17: ChunkMesh_ms=103.200
Frame 18: ChunkMesh_ms=77.864
Frame 48: ChunkMesh_ms=315.464
```

**Causes:**
1. Wall chunks are being regenerated repeatedly due to topology changes
2. Each wall chunk requires neighbor chunk data for proper meshing
3. The mesh builder may be doing redundant work

---

## Code Analysis Details

### File: sdk_chunk_manager.c

**Key functions:**
- `chunk_is_wall_chunk_fully_ready_for_superchunk()` (line 111-122): Checks if wall chunk is ready
- `rebuild_desired()` (line 852-925): Rebuilds desired set, handles async loading
- `process_async_rows()` (line 813-850): Processes async row loading

**Async loading budget:**
```c
cm->async_budget_per_frame = 2;  // 2 rows per frame = ~8 frames for full transition
```

### File: sdk_api_session_core.c

**Key functions:**
- `process_streamed_chunk_results_with_budget()` (line 805-904): Processes chunk results
- `process_active_wall_finalization_sync()` (line 723-740): Finalizes wall chunks

**Wall finalization budget:**
```c
process_active_wall_finalization_sync(8);  // Max 8 chunks per call
```

### File: sdk_api_session_bootstrap.c

**Key functions:**
- `adopt_streamed_chunk_result()` (line 382-428): Adopts streamed chunks
- `finalize_active_wall_stage_sync()` (line 812-835): Finalizes wall chunks by stage
- `finalize_active_wall_chunk_sync()` (line 837-856): Finalizes single wall chunk

---

## Open Questions

1. **Why are SUPPORT chunks not becoming ready?** This blocks EDGE/CORNER finalization.

2. **Is the topology generation incrementing unexpectedly?** This would cause all wall chunks to need re-finalization.

3. **Are wall chunk jobs being queued correctly?** The streamer has 3 passes - PRIMARY, WALL_SUPPORT, BACKGROUND.

4. **Is there a deadlock in the wall finalization chain?**
   - EDGE/CORNER need SUPPORT to be ready
   - SUPPORT may need EDGE/CORNER for neighbor meshing

5. **Why is meshing so slow?** Are wall chunks being regenerated unnecessarily?

---

## Recommended Diagnostic Steps

1. **Add verbose wall debug logging**
   - Enable `SDK_VERBOSE_WALL_DEBUG_LOGS` in relevant files
   - Log each stage of wall chunk lifecycle

2. **Trace a single wall chunk**
   - Pick a specific wall chunk coordinate (e.g., cx=-1, cz=0)
   - Log its journey from desired → job queued → generated → adopted → finalized

3. **Check topology generation changes**
   - Log when `topology_generation` increments
   - Verify it's not incrementing on every frame

4. **Profile chunk adoption**
   - Add timing around `sdk_chunk_manager_rebuild_lookup()`
   - Check if lookup rebuild is the bottleneck

5. **Verify SUPPORT chunk readiness**
   - Log how many SUPPORT chunks are ready vs total
   - Check if SUPPORT chunks are being generated

---

## Related Files

- `sdk_chunk_manager.c` - Chunk management and desired set rebuild
- `sdk_chunk_streamer.c` - Async chunk generation/meshing
- `sdk_api_session_core.c` - Chunk result processing and wall finalization
- `sdk_api_session_bootstrap.c` - Chunk adoption and wall finalization
- `sdk_api_session_editor_helpers.c` - Wall finalization state refresh

---

## Next Investigation Steps

1. Add detailed logging to track wall chunk lifecycle
2. Verify SUPPORT chunks are being generated and becoming ready
3. Check if topology generation is stable
4. Profile the exact cause of adoption lag
5. Determine if async loading is the primary issue or if sync mode also fails

---

*Report generated by Cascade on 2026-04-12*
*Investigation ongoing - this document will be updated with new findings*
