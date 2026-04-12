<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [World](../SDK_WorldOverview.md) > [Chunks](SDK_Chunks.md) > Residency & Streaming

---

# SDK Chunk Residency And Streaming

This document describes how chunks become desired, loaded, meshed, uploaded, and eventually evicted.

## Core Model

The runtime separates three concerns:

- `sdk_chunk_manager.*` decides which chunk coordinates are desired and what role each desired chunk has.
- `Core/API/Session/Core/sdk_api_session_core.c` and `Core/API/Session/Bootstrap/sdk_api_session_bootstrap.c` handle synchronous bootstrap and main-thread adoption of streamer results. `Core/API/Session/sdk_api_session.c` remains the translation-unit aggregator.
- `sdk_chunk_streamer.*` runs worker threads that generate or remesh chunk data asynchronously.

The chunk manager is authoritative for the desired set. The streamer never decides visibility on its own.

Chunks now carry more than terrain blocks. In the current runtime a loaded chunk can also carry:

- primary `SdkWorldCellCode` storage
- sparse chunk-local construction overflow refs
- a pointer to the active world/prop session construction archetype registry

## Residency Roles

`SdkChunkResidencyRole` currently has these meanings:

| Role | Meaning |
|---|---|
| `SDK_CHUNK_ROLE_PRIMARY` | Core chunk set for the active camera window or active superchunk |
| `SDK_CHUNK_ROLE_WALL_SUPPORT` | Chunks required to build or preserve superchunk wall geometry and nearby wall seams |
| `SDK_CHUNK_ROLE_FRONTIER` | Support chunks around the active set, including perimeter support and gate-frontier chunks |
| `SDK_CHUNK_ROLE_TRANSITION_PRELOAD` | Reserved for transition preload logic, currently not used in the active desired-set shape |
| `SDK_CHUNK_ROLE_EVICT_PENDING` | Resident chunk that is no longer desired and can be evicted |

Important terms:

- `desired`: a coordinate is part of the current desired set
- `resident`: a slot exists in the chunk manager for that coordinate
- `generation`: topology generation stamp used to drop stale async results
- `frontier`: non-primary support chunk, usually from the outer ring or gate extension
- `transition`: topology transition state stored on the manager; not currently materialized as desired preload chunks

## Two Residency Modes

### Window Mode

Window mode is active when the normalized render-distance radius is below the superchunk span.

Behavior:

- Desired set is a camera-centered square window.
- Recenter happens when the camera moves more than 2 chunks from the current primary anchor.
- Normal chunk load and unload happens as the anchor recenters.
- No full-superchunk residency is kept.

The primary set is emitted by `emit_camera_window(...)`.

### Superchunk Mode

Superchunk mode is active when the normalized render-distance radius is at least the superchunk span of `16` chunks.

Behavior:

- The active superchunk becomes the primary residency target.
- Desired set is rebuilt immediately when the camera crosses into a different superchunk.
- While inside the active superchunk, the desired set is fixed to the full active superchunk plus its ring/frontier support.

Authoritative desired-set layout in this mode:

- `256` primary chunks for the active `16 x 16` superchunk
- `68` outer-ring frontier chunks around that superchunk
- `68` wall-support chunks around the active perimeter and wall bands
- `4` transition-preload corner chunks

Expected steady-state desired count:

```text
400 total = 256 primary + 68 frontier + 68 wall support + 4 transition preload
```

This layout is built by:

- `emit_full_superchunk(...)`
- `emit_superchunk_outer_ring(...)`
- `emit_superchunk_extra_gate_frontier(...)`

The current manager does not build a second adjacent superchunk preload set in steady-state.

## Superchunk Walls And Ownership

Shared geometry lives in `Core/World/Chunks/ChunkManager/sdk_superchunk_geometry.h`:

- `SDK_SUPERCHUNK_CHUNK_SPAN = 16`
- `SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS = 64`
- `SDK_SUPERCHUNK_WALL_THICKNESS_CHUNKS = 1`
- gate width is `64` blocks and centered on local chunk range `6..9`

Wall ownership rule:

- West and north wall geometry lives in the current superchunk's own wall-band chunks.
- East and south visible perimeter comes from adjacent outer-ring chunks.

That is why a correct superchunk perimeter depends on both the active `16 x 16` set and the adjacent ring being resident and renderable.

## Startup Bootstrap Versus Runtime Bootstrap

There are two bootstrap paths, but only the nearby safety set remains synchronously required at startup.

### Startup Session Bootstrap

`bootstrap_visible_chunks_sync()` runs during initial world-session startup.

Current design:

- the startup path waits for the nearby sync-safe set only
- after that safety set is resident, startup now force-builds/uploads any still-not-ready primary safety chunks before handing off to normal runtime budgets
- wall support, wall-edge/corner finalize work, and dirty remesh can continue as background backlog
- the status text should describe that backlog honestly instead of pretending the runtime is still "finishing terrain chunks"
- startup readiness is based on real chunk state, not on `g_sdk.world_session_active`
- if residency or GPU readiness stops advancing, bootstrap fails with a bounded timeout instead of hanging forever

### Runtime Topology Bootstrap

`bootstrap_nearby_visible_chunks_sync()` runs when topology changes during an active session, for example:

- render distance changes
- primary superchunk changes
- the chunk manager rebuilds the desired set after camera movement in the relevant mode

Current behavior:

- sync-load a minimal safety set near the camera when required
- promote already-resident frontier/transition chunks in place
- hand remaining desired work to the async streamer

## Async Streamer Contract

The streamer owns:

- worker count selection
- generate job queue
- remesh job queue
- result queue
- per-coordinate generate tracks

Important implementation details:

- job capacity and result capacity both match `SDK_CHUNK_MANAGER_MAX_DESIRED`
- worker count is `max(1, cpu_count - 1)` clamped to `16`
- generate jobs are pruned if they no longer match the current desired coordinate, role, and topology generation
- stale async results are dropped during adoption if their generation no longer matches the desired target

Generate jobs:

- only scheduled for desired coordinates that do not already have loaded chunk blocks
- clone neighboring loaded chunks as snapshots when available

Remesh jobs:

- are scheduled from loaded dirty chunks
- reuse a snapshot copy of the chunk plus neighboring chunks

Async adoption stays on the main thread in the session core/bootstrap modules.

## Construction Data In The Async Flow

Construction state is part of chunk state, not an afterthought side buffer.

Current ownership rules:

- the active chunk manager owns the shared construction archetype registry for the session
- chunks point at that shared registry
- worker jobs build and clone chunk-local construction refs against the same registry
- worker jobs do not create independent archetype pools
- main-thread adoption replaces full chunk state, including construction refs

Bug-prone boundary:

- chunk replacement must acquire and release construction archetype refs exactly once
- if clone, clear, adopt, or persistence reload code changes, verify construction refcounts immediately

## Current Runtime Flow

### Initial World Session

```text
chunk manager update
  -> desired set built
  -> sync startup bootstrap
  -> mesh upload pass
  -> initial async scheduling
```

### Steady-State Frame

```text
camera update
  -> chunk manager topology update
  -> if topology changed: evict undesired + runtime sync bootstrap
  -> schedule visible generate jobs
  -> adopt async results within frame budget
  -> schedule dirty remesh jobs
```

## Diagnostics To Use

Primary logs:

- `[RESIDENCY] ...` for desired/resident counts, roles, generations, pending jobs, and primary superchunk
- `[STREAM] ...` for queue saturation, dropped stale results, and missing tracks
- `[WALL] ...` for side-specific desired/resident/GPU counts
- `[UPLOAD] ...` for mesh upload failures

When max mode is healthy, the topology log should settle around:

```text
desired=400(P256/F68/W68/T4)
```

If it does not, start with `SDK_RuntimeDiagnostics.md`.

---

## Related Documentation

### Up to Parent
- [SDK World Overview](../SDK_WorldOverview.md) - World systems hub
- [SDK Chunks](SDK_Chunks.md) - Chunk data structures

### Siblings - Chunk System
- [ChunkManager/SDK_ChunkManager.md](ChunkManager/SDK_ChunkManager.md) - Residency management
- [ChunkStreamer/SDK_ChunkStreamer.md](ChunkStreamer/SDK_ChunkStreamer.md) - Async streaming worker
- [ChunkCompression/SDK_ChunkCompression.md](ChunkCompression/SDK_ChunkCompression.md) - Serialization
- [ChunkAnalysis/SDK_ChunkAnalysis.md](ChunkAnalysis/SDK_ChunkAnalysis.md) - Analysis tools

### Related World Systems
- [../ConstructionCells/SDK_ConstructionSystem.md](../ConstructionCells/SDK_ConstructionSystem.md) - Construction system
- [../Persistence/SDK_PersistenceAndStorage.md](../Persistence/SDK_PersistenceAndStorage.md) - Save/load
- [../Simulation/SDK_Simulation.md](../Simulation/SDK_Simulation.md) - Fluid simulation
- [../../MeshBuilder/SDK_MeshBuilder.md](../../MeshBuilder/SDK_MeshBuilder.md) - Mesh generation

### Core Integration
- [../../API/SDK_APIReference.md](../../API/SDK_APIReference.md) - Public API
- [../../Runtime/SDK_RuntimeDiagnostics.md](../../Runtime/SDK_RuntimeDiagnostics.md) - Diagnostics (referenced above)

---
*Documentation for `SDK/Core/World/Chunks/`*
