<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [World](../SDK_WorldOverview.md) > Grid Walls

---

# NQL SDK Grid Walls

This page documents the wall-grid concept used by superchunks and detached walls.

## Scope

Primary owners:

- `Core/World/Superchunks/Config/sdk_superchunk_config.*`
- `Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h`
- `Core/World/Worldgen/Column/sdk_worldgen_column.c`

## Attached vs Detached

Attached mode:

- wall-grid size normalizes to `chunk_span`
- wall classification follows terrain superchunk ownership directly

Detached mode:

- wall-grid size defaults to `chunk_span + 2`
- `wall_grid_offset_x` / `wall_grid_offset_z` become live runtime inputs
- terrain ownership and wall classification are no longer the same space

## Runtime Uses

- classifying wall chunks
- finding wall-cell ownership
- deciding gate-support runs
- aligning runtime wall health and debug reporting
- map rendering and wall-band spawn avoidance

## Change Guidance

- If a change affects where walls appear, verify attached and detached behavior separately.
- Do not re-derive wall-grid formulas in leaf systems. Use the superchunk helper layer.

## Related Docs

- [../Superchunks/SDK_SuperChunks.md](../Superchunks/SDK_SuperChunks.md)
- [../Superchunks/Config/SDK_SuperchunkConfig.md](../Superchunks/Config/SDK_SuperchunkConfig.md)
- [../Superchunks/Geometry/SDK_SuperchunkGeometry.md](../Superchunks/Geometry/SDK_SuperchunkGeometry.md)
- [../CoordinateSystems/SDK_WorldCoordinateSystems.md](../CoordinateSystems/SDK_WorldCoordinateSystems.md)

