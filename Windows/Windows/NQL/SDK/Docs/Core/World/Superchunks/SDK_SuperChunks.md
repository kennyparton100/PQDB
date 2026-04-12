# NQL SDK Superchunks

This page documents the current superchunk and wall-layout model used by runtime code, persistence, debugger tooling, and chunk analysis.

## Current Model

- Terrain superchunks are keyed by `chunk_span`.
- Terrain ownership still uses the shared-boundary layout: `period = chunk_span + 1`.
- Interior chunks live at local coordinates `0 .. chunk_span - 1`.
- Wall chunks occupy the shared boundary lines around that interior.

With the default configuration:

- `chunk_span = 16`
- terrain/wall period = `17`
- the first terrain interior chunk is `G(1,1) -> L(0,0)`
- `G(0,0)` is the northwest corner wall chunk

## Detached Walls

Detached mode is now live runtime behavior, not metadata only.

When `walls_detached = true`:

- terrain superchunk geometry still uses `chunk_span`
- wall classification also uses a detached wall grid
- wall-grid period comes from normalized `wall_grid_size`
- default normalization is `wall_grid_size = chunk_span + 2`
- wall-grid offsets come from `wall_grid_offset_x` / `wall_grid_offset_z`

The current helper layer is split across:

- `Core/World/Superchunks/Config/sdk_superchunk_config.*`
- `Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h`

Important helpers:

- `sdk_superchunk_normalize_config`
- `sdk_superchunk_get_chunk_span`
- `sdk_superchunk_get_wall_period`
- `sdk_superchunk_get_wall_grid_period`
- `sdk_superchunk_wall_cell_from_chunk`
- `sdk_superchunk_chunk_is_wall_anywhere`
- `sdk_superchunk_chunk_local_interior_coords`

## Runtime Ownership Rules

- Canonical wall ownership is still west/north based so worldgen does not double-author wall chunks.
- East/south active faces reuse the same physical chunks through adjacent-cell ownership.
- Wall profile, gate checks, persistence grouping, debugger mapping, and chunk analysis must all consume the same helper layer instead of re-deriving their own formulas.

## Settings Semantics

`SdkSuperchunkConfig` now has explicit meanings:

- `enabled`
  - enables superchunk-mode runtime behavior
- `chunk_span`
  - terrain interior span
- `walls_enabled`
  - walls require superchunks
- `walls_detached`
  - enables the detached wall grid
- `wall_grid_size`
  - detached wall-grid size input, normalized by config rules
- `wall_grid_offset_x`, `wall_grid_offset_z`
  - detached wall-grid offsets

Normalization rules:

- invalid `chunk_span` falls back to `16`
- `walls_enabled` implies `enabled`
- detached mode defaults to `wall_grid_size = chunk_span + 2`
- attached mode normalizes `wall_grid_size = chunk_span`

## Known Limits

- Non-default detached offsets now affect runtime classification and save/debug grouping, but they still need broader live-play validation in residency and distant wall-proxy behavior.
- Some legacy leaf docs still show older illustrative formulas. Prefer this page plus [SDK_WorldSystemsGapAudit.md](../../../SDK_WorldSystemsGapAudit.md) when they disagree.

## Related Docs

- [SDK_WorldOverview.md](../SDK_WorldOverview.md)
- [SDK_PersistenceAndStorage.md](../Persistence/SDK_PersistenceAndStorage.md)
- [SDK_RuntimeDiagnostics.md](../../Runtime/SDK_RuntimeDiagnostics.md)
- [SDK_WorldgenConstructionCells.md](../Worldgen/ConstructionCells/SDK_WorldgenConstructionCells.md)
