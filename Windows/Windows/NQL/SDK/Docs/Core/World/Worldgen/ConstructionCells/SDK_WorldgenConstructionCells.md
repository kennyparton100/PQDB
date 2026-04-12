# NQL SDK Worldgen Construction Cells

This page describes the current worldgen-time construction-cell path.

## Current Behavior

`construction_cells_enabled` is now a real create-world feature.

When enabled:

- the frontend writes the flag to `meta.txt`
- world/session startup propagates it into `SdkWorldDesc`
- `sdk_worldgen_init` preserves the caller-provided value
- `sdk_worldgen_fill_chunk` is the single owner of construction-cell generation for fresh chunks
- chunk generation calls `generate_world_cells`
- generated construction cells use the normal chunk construction registry and persistence path
- chunk topology and surface readers treat construction cells as real top-layer occupants through construction display-material semantics

The implementation lives in:

- `Core/World/Worldgen/ConstructionCells/sdk_worldgen_construction_cells.c`
- `Core/World/ConstructionCells/`
- settlement/layout support under `Core/World/Settlements/`

## Generation Path

Current flow:

1. chunk terrain is generated
2. `generate_world_cells` runs once for the fresh chunk
3. terrain-edge topology is analyzed and surface edge cells may be replaced with construction cells at `surface_y`
4. settlement data is resolved for the chunk
5. settlement layout is generated
6. overlapping building placements are selected
7. runtime building markers are converted into construction-cell payloads
8. marker payloads are written through the normal chunk construction registry only when the target cell is otherwise empty

Loaded chunks do not re-run this pass; persisted construction content is reused as saved.

## What Gets Placed

The current pass generates two construction-cell categories:

- terrain-edge surface cells driven by chunk topology
- deterministic settlement marker cells such as:

  - entrances
  - storage markers
  - patrol markers
  - sleep/work/station markers

Terrain-edge cells are intentionally destructive: they may replace the existing surface terrain block at the selected surface position.

Settlement marker cells are non-destructive: placement only occurs when the target chunk cell is empty and the construction registry accepts the occupancy payload.

## Persistence

Worldgen construction cells survive normal save/load because they use the same chunk-level construction persistence as player/runtime construction.

That means enabled worlds now support:

- worldgen construction placement
- terrain-edge surface replacement
- chunk remesh/render
- topology/surface readback that sees construction display materials
- save/load round-trip

## Known Limits

- Terrain-edge cells are a broad surface pass, not a fully authored settlement facade system.
- Settlement/building markers remain deterministic and limited in breadth.
- Existing worlds created before the flag was wired through runtime worldgen will not retroactively gain this content.

## Related Docs

- [SDK_ConstructionSystem.md](../../ConstructionCells/SDK_ConstructionSystem.md)
- [SDK_RuntimeSessionAndFrontend.md](../../../API/Session/SDK_RuntimeSessionAndFrontend.md)
- [SDK_WorldSystemsGapAudit.md](../../../../SDK_WorldSystemsGapAudit.md)
