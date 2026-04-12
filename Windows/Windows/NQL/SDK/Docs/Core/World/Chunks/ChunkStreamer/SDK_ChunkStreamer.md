<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [World](../../SDK_WorldOverview.md) > [Chunks](../SDK_Chunks.md) > Streamer

---

# SDK Chunk Streamer

This page documents the current chunk streamer at a high level.

It intentionally avoids stale copied code and broken diagrams. For current startup/runtime context, read this together with:

- [SDK_ChunkResidencyAndStreaming.md](../SDK_ChunkResidencyAndStreaming.md)
- [../../../API/Session/SDK_RuntimeSessionAndFrontend.md](../../../API/Session/SDK_RuntimeSessionAndFrontend.md)

## Purpose

The chunk streamer is the async worker system for chunk generation and remeshing.

It is responsible for:

- worker threads
- queued chunk build jobs
- queued build results
- per-coordinate generate tracks
- worker-side mesh building for fresh chunks and remesh work

It is not responsible for deciding which chunks are desired. That remains the chunk manager's job.

## Current File Ownership

Primary file:

- `Core/World/Chunks/ChunkStreamer/sdk_chunk_streamer.c`

Closely related files:

- `Core/World/Chunks/ChunkManager/sdk_chunk_manager.c`
- `Core/API/Session/Core/sdk_api_session_core.c`
- `Core/API/Session/Bootstrap/sdk_api_session_bootstrap.c`
- `Core/MeshBuilder/sdk_mesh_builder.c`
- `Core/World/Persistence/sdk_persistence.c`

## Current Internal Model

The streamer currently owns:

- a worker pool
- a job ring buffer
- a result ring buffer
- dropped-remesh repair tracking
- per-coordinate stream tracks
- a pointer to the active construction registry

Current fixed limits in code:

- `SDK_CHUNK_STREAMER_MAX_WORKERS = 16`
- job capacity matches `SDK_CHUNK_MANAGER_MAX_DESIRED`
- result capacity matches `SDK_CHUNK_MANAGER_MAX_DESIRED`

Do not rely on older docs that still say the worker count is clamped to `12`.

## Job Types

The current streamer supports two build-job types:

- generate
  - create or load a chunk, run worldgen if needed, build mesh state, queue a built chunk result
- remesh
  - rebuild mesh state for an existing dirty chunk snapshot

The worker stage tracking also distinguishes:

- idle
- loading
- worldgen
- meshing
- queuing result

## Current Scheduling Model

Visible scheduling is distance-sorted and pass-based.

The streamer currently schedules in multiple passes so that:

- primary chunks are preferred first
- wall-support chunks can be scheduled in a separate pass
- remaining non-primary work is deferred to later passes

Startup uses a more constrained path than steady-state scheduling:

- `sdk_chunk_streamer_schedule_startup_priority(...)`

Steady-state visible scheduling uses:

- `sdk_chunk_streamer_schedule_visible(...)`

## Ownership Boundary

The chunk manager decides:

- which coordinates are desired
- what residency role each desired chunk has
- the current topology generation

The streamer decides:

- which eligible desired chunks to queue now
- which workers process which jobs
- when completed results become available to the main thread

The session/bootstrap layer decides:

- how aggressively to consume results
- when startup is complete
- when queued work should be treated as backlog instead of blocking work

## Main-Thread Adoption Contract

Workers never directly mutate the live chunk manager.

They produce `SdkChunkBuildResult` entries, and the main thread later:

- pops results
- checks generation/desired validity
- adopts or discards them
- handles upload/remesh follow-up

This split is important because many startup and residency bugs come from confusing worker completion with main-thread adoption.

## Construction Data In The Streamer

Construction state is part of the chunk payload.

That means streamer work can involve:

- chunk-local overflow refs
- the session construction registry
- construction-aware mesh building

This is a sensitive boundary. If construction registry ownership or refcounting is wrong, streamer work can expose it quickly.

## Shutdown Behavior

Shutdown is not just "stop the threads".

A correct shutdown path must account for:

- active workers
- queued jobs
- queued results
- in-flight results produced after cancellation begins

If you change streamer shutdown or session cleanup, verify result draining as well as worker termination.

## Diagnostics To Trust

Useful logs:

- `[STREAM]`
- `[READINESS]`
- `[BOOTSTRAP]`
- `[RESIDENCY]`

What they usually mean:

- many jobs, no results:
  - workers are busy or blocked before result publication
- results appear but residency does not move:
  - main-thread adoption or counting is wrong
- readiness reaches target, then degrades later:
  - post-startup residency or upload state is unstable, not initial queueing alone

## Known Current Risks

- startup and background expansion are still easy to over-couple
- wall-support scheduling remains a fragile area
- construction-registry ownership remains a crash-sensitive dependency

## Related Documentation

- [SDK_ChunkResidencyAndStreaming.md](../SDK_ChunkResidencyAndStreaming.md)
- [ChunkManager/SDK_ChunkManager.md](../ChunkManager/SDK_ChunkManager.md)
- [ChunkCompression/SDK_ChunkCompression.md](../ChunkCompression/SDK_ChunkCompression.md)
- [ChunkAnalysis/SDK_ChunkAnalysis.md](../ChunkAnalysis/SDK_ChunkAnalysis.md)
- [../../../API/Session/SDK_RuntimeSessionAndFrontend.md](../../../API/Session/SDK_RuntimeSessionAndFrontend.md)

---
*Documentation for `SDK/Core/World/Chunks/ChunkStreamer/`*
