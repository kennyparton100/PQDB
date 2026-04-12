<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [World](../SDK_WorldOverview.md) > Coordinate Systems

---

# NQL SDK World Coordinate Systems

This page documents the coordinate spaces used by the world runtime.

## Core Spaces

- world block coordinates: absolute integer block positions
- chunk coordinates: `(cx, cz)` at `64 x 64` block granularity
- chunk-local coordinates: `(lx, ly, lz)` inside one chunk
- superchunk cell coordinates: terrain ownership cells derived from runtime config
- wall-grid coordinates: detached/attached wall classification cells

## Chunk Conversion Helpers

Primary owners:

- `Core/World/Chunks/sdk_chunk.h`

Key helpers:

- `sdk_world_to_chunk_x`
- `sdk_world_to_chunk_z`
- `sdk_world_to_local_x`
- `sdk_world_to_local_z`
- `sdk_chunk_to_world_x`
- `sdk_chunk_to_world_z`

## Superchunk And Wall Coordinates

Primary owners:

- `Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h`
- `Core/World/Superchunks/Config/sdk_superchunk_config.h`

Key helpers:

- `sdk_superchunk_cell_from_chunk`
- `sdk_superchunk_wall_cell_from_chunk`
- `sdk_superchunk_chunk_local_interior_coords`
- `sdk_superchunk_cell_origin_blocks`

## Why This Matters

Many engine bugs are really coordinate-space bugs. Common failure modes:

- using world coordinates where chunk-local coordinates are required
- mixing terrain superchunk ownership with detached wall-grid ownership
- assuming attached-wall formulas when detached walls are enabled
- forgetting that startup, map, wall, and settlement systems all consume these spaces differently

## Related Docs

- [../Chunks/SDK_Chunks.md](../Chunks/SDK_Chunks.md)
- [../Superchunks/SDK_SuperChunks.md](../Superchunks/SDK_SuperChunks.md)
- [../Superchunks/Geometry/SDK_SuperchunkGeometry.md](../Superchunks/Geometry/SDK_SuperchunkGeometry.md)
- [../GridWalls/SDK_GridWalls.md](../GridWalls/SDK_GridWalls.md)

