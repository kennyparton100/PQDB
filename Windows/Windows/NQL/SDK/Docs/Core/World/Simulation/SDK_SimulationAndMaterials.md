# NQL SDK - Simulation And Materials

This document describes the current sparse runtime simulation layer used for fluids and loose materials in loaded chunks.

## Scope

The public surface is declared in `Core/sdk_simulation.h`. The implementation lives in `Core/sdk_simulation.c`.

This subsystem is responsible for:

- storing sparse per-chunk simulation state
- waking simulation when blocks change or chunks load
- advancing the loaded-world simulation with a bounded per-frame budget
- encoding fluid state for persistence
- exposing debug information for the fluid and performance overlays

It does not attempt to simulate the whole world offline. The system is tied to currently loaded chunk-manager state.

## Per-Chunk State

Each loaded chunk can own a `SdkChunkSimState`.

That state contains:

- sparse `fluid_cells`
- sparse `loose_cells`
- a lookup table for fluid-cell indices
- a dirty queue
- dirty mark bits used to deduplicate queued cells

The public helpers are:

- `sdk_simulation_chunk_state_create`
- `sdk_simulation_chunk_state_destroy`
- `sdk_simulation_chunk_state_clear`
- `sdk_simulation_clone_chunk_state_for_snapshot`

`SdkSimCellKey` is the packed local-cell key used throughout the simulation layer.

## Runtime Entry Points

The main runtime hooks are:

- `sdk_simulation_on_block_changed`
  - wake the affected world position after a terrain mutation
- `sdk_simulation_on_chunk_loaded`
  - seed simulation state when a chunk becomes resident
- `sdk_simulation_enqueue_local`
- `sdk_simulation_enqueue_world`
- `sdk_simulation_tick_chunk_manager`
  - process a bounded amount of work for the active chunk manager

The frame code passes a cell budget rather than allowing the simulation to run unbounded.

## Fluid Execution Modes

The water simulation currently uses two broad mechanisms:

- local wake
  - a lightweight response around changed cells
- bulk reservoir solve
  - a larger solve path for broader connected water volumes

These mechanisms are reported through `SdkFluidDebugInfo.mechanism`:

- `SDK_FLUID_DEBUG_MECH_IDLE`
- `SDK_FLUID_DEBUG_MECH_BULK_RESERVOIR`
- `SDK_FLUID_DEBUG_MECH_LOCAL_WAKE`

The implementation contains a dedicated reservoir scheduler with queued jobs, queued results, and a small active-reservoir set. This is the path used when the local wake is not sufficient.

## Debug Information

`sdk_simulation_get_debug_info` fills `SdkFluidDebugInfo`, which is what the runtime overlay and performance HUD consume.

The struct currently exposes:

- last seed coordinate that triggered the current solve
- reservoir column counts
- worker count
- processed cell counts
- dirty cell counts
- active chunk counts
- volume and target surface estimates
- solve/apply timings
- a short textual reason string

Representative log tags emitted by the simulation layer:

- `[FLUID] LOCAL WAKE ...`
- `[FLUID] BULK REQUIRED ...`
- `[FLUID] BULK DEFERRED ...`

The main runtime toggles a fluid debug overlay and feeds the renderer with the latest `SdkFluidDebugInfo`.

## Persistence Contract

The simulation layer participates in persistence through:

- `sdk_simulation_encode_chunk_fluids`
- `sdk_simulation_decode_chunk_fluids`

The encoded fluid payload is stored per chunk in `save.json` under the `chunks[].fluid` field. This lets persisted chunks restore water state without regenerating the same fluid surface from scratch.

## Shutdown Contract

The simulation layer exposes an explicit shutdown handshake:

- `sdk_simulation_begin_shutdown`
- `sdk_simulation_poll_shutdown`
- `sdk_simulation_shutdown`

That allows worker-owned reservoir work to drain cleanly during session teardown.

## Current Limits

- The system is scoped to resident chunks managed by `SdkChunkManager`.
- The simulation budget is intentionally bounded per frame.
- Debug output is strong for fluids, but the public API for loose-material behavior is much thinner than the water path.
- Persistence is chunk-local. There is no separate global simulation save file.

## Related Docs

- [SDK_ChunkResidencyAndStreaming.md](../Chunks/SDK_ChunkResidencyAndStreaming.md)
- [SDK_PersistenceAndStorage.md](../Persistence/SDK_PersistenceAndStorage.md)
- [SDK_RuntimeDiagnostics.md](../../Runtime/SDK_RuntimeDiagnostics.md)
