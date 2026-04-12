# Detached Walls Analysis

This file now documents the current detached-wall state used by runtime, persistence, and debugger tooling.

## Current State

Detached walls are no longer "declared but ignored".

The live helper layer now uses:

- `chunk_span` for terrain superchunk interior layout
- detached wall-grid size/offset inputs for wall classification when `walls_detached = true`
- shared geometry helpers for debugger mapping, chunk analysis, and persistence grouping

Primary files:

- `Core/World/Superchunks/Config/sdk_superchunk_config.*`
- `Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h`
- `Core/World/Worldgen/Column/sdk_worldgen_column.c`
- `Core/World/Persistence/sdk_persistence.c`
- `Debugging/debugger_mapping.c`

## Important Semantics

- Terrain superchunk layout still uses the shared-boundary model.
- Detached mode adds a live wall grid; it does not replace terrain superchunk ownership.
- Attached mode normalizes `wall_grid_size` back to `chunk_span`.
- Detached mode defaults to `wall_grid_size = chunk_span + 2`.

## What Is Still Partial

- Non-default detached offsets and unusual grid sizes need broader live runtime validation in residency staging and distant wall-proxy rendering.
- The headless debugger is still an offline analysis tool, not the gameplay runtime.

## Historical Context

Older versions of this note described detached walls as incomplete and unused. That is no longer current guidance.

Use these maintained pages first:

- `Docs/SDK_WorldSystemsGapAudit.md`
- `Docs/Core/World/Superchunks/SDK_SuperChunks.md`
- `Docs/Core/World/Persistence/SDK_PersistenceAndStorage.md`
