# SDK Renderer And Runtime Foundation Audit

Date: 2026-04-02

## Scope
- Static review of the render path, chunk streaming/remesh pipeline, and nearby runtime ownership boundaries.
- Primary files reviewed:
  - `Renderer/d3d12_renderer.cpp`
  - `Renderer/d3d12_renderer_frame.cpp`
  - `Renderer/d3d12_renderer_hud.cpp`
  - `Renderer/d3d12_renderer_chunks_unified.cpp`
  - `Renderer/d3d12_renderer_resources.cpp`
  - `Core/sdk_chunk.c`
  - `Core/sdk_chunk.h`
  - `Core/sdk_chunk_streamer.c`
  - `Core/sdk_api_session.c`
  - `Core/sdk_api.c`
  - `Core/sdk_server_runtime.c`
- This was a code audit only. I did not run a live repro capture in this pass.

## Executive Summary
- The strongest match for the reported "wrong geometry or wrong data for one frame" glitch is unsafe reuse of single mapped upload resources across multiple frames.
- The second strongest match is a chunk remesh correctness gap: CPU mesh state can advance before GPU unified-buffer state is refreshed, and the draw path still trusts the old GPU buffer.
- A deeper correctness issue exists underneath that: remesh invalidation is not monotonic, so stale remesh jobs and wall-proxy cache signatures can survive newer edits.
- Under streamer pressure, dropped remesh results can also strand a chunk in a permanently queued/inflight state.

## Findings

### 1. Critical: single mapped upload resources are reused across frames without frame-local ownership
- Evidence:
  - `Renderer/d3d12_renderer_internal.h:89-90` stores a single `constant_buffer` and `cb_mapped`.
  - `Renderer/d3d12_renderer_internal.h:122-126` stores a single `hud_vb`, `hud_vb_mapped`, `hud_cb`, and `hud_cb_mapped`.
  - `Renderer/d3d12_renderer_internal.h:141-142` stores a single `lighting_cb` and `lighting_cb_mapped`.
  - `Renderer/d3d12_renderer.cpp:150-166`, `169-190`, and `246-265` create and persistently map exactly one scene CB, one lighting CB, and one HUD CB.
  - `Renderer/d3d12_renderer_frame.cpp:1293-1297` only waits for `g_rs.fence_values[fi]`, which protects the allocator/backbuffer slot for the current frame index, not every single shared upload resource.
  - `Renderer/d3d12_renderer_frame.cpp:1323` rewrites `g_rs.cb_mapped` every frame.
  - `Renderer/d3d12_renderer_hud.cpp:487-491` rewrites `lighting_cb_mapped`.
  - `Renderer/d3d12_renderer_hud.cpp:2441` rewrites `hud_cb_mapped`.
  - `Renderer/d3d12_renderer_hud.cpp:2471` rewrites `hud_vb_mapped`.
- Why this matters:
  - These resources are global and shared across all frames, but the frame loop only waits on the fence associated with the current allocator index.
  - With `FRAME_COUNT > 1`, the GPU can still be reading the previous frame's contents while the CPU writes the next frame's constants or HUD vertices into the same upload heap memory.
  - That can produce exactly the observed symptom: one-frame camera/projection corruption, lighting mismatch, HUD corruption, or "wrong data for one frame" during heavy scene churn.
- Reproduction hypothesis:
  - Increase render distance and stream pressure, then yaw the camera rapidly so matrix and lighting updates happen every frame while the GPU is still busy on the previous one.
- Fix:
  - Make all per-frame rewritten upload resources frame-local.
  - Minimum scope:
    - scene CB: `FRAME_COUNT` copies or a ring-buffered upload allocation,
    - lighting CB: `FRAME_COUNT` copies of both the normal and wall-black slices,
    - HUD CB: `FRAME_COUNT` copies,
    - HUD VB: `FRAME_COUNT` buffers or a fenced ring allocator.
  - Bind the frame-local GPU address for the current frame index instead of the single global resource.
- Regression test:
  - Add debug instrumentation that tags each per-frame upload write with the current frame index and asserts if that memory region is being reused before its fence completes.
  - Stress test: max practical distance, rapid yaw, active chunk streaming, walls enabled, HUD visible.

### 2. Critical: remesh adoption updates CPU mesh state before GPU unified-buffer state, and the draw path still trusts the old GPU buffer
- Evidence:
  - `Core/sdk_api_session.c:2752-2780` accepts remesh results in `adopt_streamed_remesh_result`.
  - `Core/sdk_api_session.c:2777` calls `sdk_chunk_apply_mesh_state(slot, result->built_chunk, result->dirty_mask)`.
  - `Core/sdk_chunk.c:359-384` transfers only CPU mesh/submesh state and then calls `sdk_chunk_refresh_mesh_state(dst)`.
  - `Core/sdk_chunk.c:227-328` recomputes dirty and upload flags, but does not invalidate or version existing unified GPU state.
  - `Renderer/d3d12_renderer_frame.cpp:504-527` and `541-579` draw directly from `chunk->unified_vertex_buffer`, `subchunk_offsets`, `water_offsets`, and far-mesh offsets when those counts are non-zero.
  - `Renderer/d3d12_renderer_frame.cpp:319-331` treats `chunk->unified_vertex_buffer != nullptr` as "ready" for representation choice.
  - `Core/sdk_api_session.c:2857-2935` uploads chunk GPU data later, under a separate deferred upload budget.
- Why this matters:
  - After remesh adoption, the chunk has newer CPU mesh data and `upload_pending`, but it can still retain an older unified GPU buffer and older offsets/counts.
  - The draw path does not require "GPU generation == CPU mesh generation"; it only requires that a unified buffer exists.
  - Under budget pressure, a chunk can be drawn for one or more frames using stale GPU geometry after newer mesh content has already been adopted.
- Reproduction hypothesis:
  - Stream many chunks, constrain upload budget, then rotate the camera so chunks alternate between becoming visible and being remeshed while uploads lag behind adoption.
- Fix:
  - Add an explicit invariant:
    - CPU mesh content generation,
    - uploaded GPU mesh generation.
  - After remesh adoption:
    - either invalidate drawability by clearing/staling unified GPU state immediately,
    - or keep the old GPU buffer but reject it in the draw path when `gpu_mesh_generation != cpu_mesh_generation`.
  - Do not let `chunk->unified_vertex_buffer != nullptr` alone imply readiness.
- Regression test:
  - Instrument a warning if a chunk is drawn while `upload_pending` is set and the uploaded generation is older than the current CPU mesh generation.
  - Repeat edits/remeshes to the same chunk while camera motion forces it into and out of visibility.

### 3. Critical: remesh invalidation is not monotonic, so stale jobs and wall-proxy cache signatures can survive newer edits
- Evidence:
  - `Core/sdk_chunk.c:64-88` only increments `chunk->mesh_job_generation` when a subchunk transitions from not-dirty to dirty.
  - `Core/sdk_chunk.c:165-183` `sdk_chunk_mark_all_dirty` does not increment `mesh_job_generation` at all.
  - `Core/sdk_chunk_streamer.c:1066-1068` snapshots the current `mesh_job_generation` and `dirty_mask` into a remesh job.
  - `Core/sdk_api_session.c:2770-2774` accepts or rejects remesh results using only `inflight_mesh_generation` and `mesh_job_generation`.
  - `Renderer/d3d12_renderer_frame.cpp:1024-1060` uses `mesh_job_generation` and `dirty_subchunks_mask` as part of the wall-proxy cache signature.
- Why this matters:
  - If a chunk is edited again while already dirty, `mesh_job_generation` may not advance.
  - An older remesh result can then still look current and be accepted.
  - The same bug weakens wall-proxy cache invalidation, because the cache signature depends on a generation that is not guaranteed to change on every geometry-affecting edit.
- Reproduction hypothesis:
  - Repeated block edits or repeated dirty marking to the same chunk while a remesh is already queued or in flight.
  - Wall-proxy superchunks are especially sensitive because their signatures are derived from neighbor mesh generations.
- Fix:
  - Replace the current "first transition to dirty" behavior with a true monotonic mesh content generation that advances on every content-affecting invalidation.
  - `sdk_chunk_mark_all_dirty` must also advance it.
  - Result acceptance should compare against that monotonic content generation, not the current partial heuristic.
  - Wall-proxy signatures should key off the same monotonic generation.
- Regression test:
  - Add a focused test that edits the same chunk multiple times while a remesh is in flight and verifies that only the final generation can be adopted.
  - Add a wall-proxy cache test that edits boundary chunks repeatedly and verifies proxy rebuilds track every change.

### 4. High: dropped remesh results can strand chunks in `remesh_queued` / `inflight_mesh_generation`
- Evidence:
  - `Core/sdk_chunk_streamer.c:655-666` drops results when `SDK_CHUNK_STREAMER_RESULT_CAPACITY` is full.
  - For generate jobs, `clear_generate_track` is called at `Core/sdk_chunk_streamer.c:662-664`.
  - For remesh jobs, the worker only releases the built chunk at `Core/sdk_chunk_streamer.c:665`; it does not repair the live chunk's `remesh_queued` or `inflight_mesh_generation`.
  - `Core/sdk_chunk_streamer.c:1052-1082` will not schedule a remesh when `chunk->remesh_queued` is still set.
- Why this matters:
  - Under queue pressure, a dropped remesh result can leave the resident chunk permanently flagged as already queued.
  - That chunk will stop rescheduling remesh work and can remain visually stale until it is unloaded or otherwise reset.
- Reproduction hypothesis:
  - Artificially reduce result capacity or spike build throughput so workers produce remesh results faster than the main thread can adopt them.
- Fix:
  - Add an explicit repair path for dropped remesh results.
  - If the worker cannot enqueue a remesh result, the owning live chunk must become reschedulable on the main thread:
    - clear `remesh_queued`,
    - clear or repair `inflight_mesh_generation`,
    - leave the chunk dirty.
- Regression test:
  - Force result queue overflow under remesh load and verify that every affected chunk becomes eligible for rescheduling within a bounded number of frames.

### 5. Medium: wall-health diagnostics can report "GPU ready" when no GPU mesh buffer exists
- Evidence:
  - `Core/sdk_api.c:464-488` returns true from `chunk_has_gpu_mesh_buffers` not only for actual GPU buffers, but also when:
    - `chunk->blocks` exists,
    - `!chunk->upload_pending`,
    - `!sdk_chunk_needs_remesh(chunk)`.
- Why this matters:
  - This is a diagnostics bug, not a direct draw bug, but it can hide upload-state problems during renderer triage by overstating readiness.
- Fix:
  - Restrict `chunk_has_gpu_mesh_buffers` to actual GPU-backed mesh state only.
  - If a softer "logically ready" concept is needed, expose it as a separate function with a separate metric name.
- Regression test:
  - Compare wall-health counters before and after forcing GPU buffer teardown; diagnostics should drop until an actual upload completes.

### 6. Medium: remote saved-server connection is an explicit stub
- Evidence:
  - `Core/sdk_server_runtime.c:519-521` rejects saved-server connection attempts with `sdk_online_set_status("Remote server networking is not implemented yet.");`
- Why this matters:
  - This is not causing the reported renderer glitch, but it is an explicit runtime gap that should stay visible in planning.
  - It matters if future work assumes remote online flows are part of the stable foundation.
- Fix:
  - Either implement the remote networking path or hard-fence the UI/feature surface so it cannot be mistaken for a partially working runtime feature.
- Regression test:
  - Saved-server connect flow should either fully connect or remain unavailable behind a clear capability gate.

## Notes
- I did not find evidence that deferred retirement of old chunk GPU buffers is the main glitch source. `Renderer/d3d12_renderer_resources.cpp:65-104` retires those resources against a fence value instead of freeing them immediately.
- I also found several per-frame dynamic buffers that call `wait_for_gpu()` before reset and rebuild:
  - `Renderer/d3d12_renderer_frame.cpp:1506-1507`
  - `Renderer/d3d12_renderer_frame.cpp:1564-1565`
  - `Renderer/d3d12_renderer_frame.cpp:1797-1798`
- Those are correctness-safe, but they are full GPU stalls and should be revisited later as a performance issue, not as the immediate one-frame corruption root cause.

## Fix Order
1. Fix per-frame upload resource ownership in the renderer.
2. Add explicit CPU-mesh-generation vs GPU-mesh-generation tracking on `SdkChunk`.
3. Make mesh invalidation monotonic and use it for remesh result acceptance and wall-proxy cache invalidation.
4. Repair remesh result overflow bookkeeping so dropped results cannot strand chunks.
5. Clean up misleading diagnostics and keep explicit runtime stubs visible but separate from render correctness work.

## Recommended Instrumentation Before The Fix Pass
- Log or assert when a per-frame upload resource is written before its prior fence has completed.
- Log when a chunk draw uses unified GPU data older than the current CPU mesh generation.
- Log when a remesh result is rejected or accepted with a generation that no longer matches the latest content generation.
- Log when a remesh result is dropped due to result-queue overflow and include the owning chunk coordinates.
