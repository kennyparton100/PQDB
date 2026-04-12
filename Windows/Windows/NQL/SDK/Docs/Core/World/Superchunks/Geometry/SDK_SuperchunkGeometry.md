# NQL SDK Superchunk Geometry

Owned file:

- `Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h`

This header is the canonical formula layer for superchunk cells, wall cells, interior coordinates, wall ownership, gate-support runs, and active wall stage classification.

High-signal helpers:

- `sdk_superchunk_cell_from_chunk`
- `sdk_superchunk_wall_cell_from_chunk`
- `sdk_superchunk_chunk_local_interior_coords`
- `sdk_superchunk_chunk_is_active_wall`
- `sdk_superchunk_chunk_is_active_wall_support`
- `sdk_superchunk_get_canonical_wall_chunk_owner`

Use this file when changing coordinate math or wall ownership formulas. Do not re-derive those formulas in leaf systems.

Related docs:

- [../SDK_SuperChunks.md](../SDK_SuperChunks.md)
- [../Config/SDK_SuperchunkConfig.md](../Config/SDK_SuperchunkConfig.md)
- [../../CoordinateSystems/SDK_WorldCoordinateSystems.md](../../CoordinateSystems/SDK_WorldCoordinateSystems.md)

