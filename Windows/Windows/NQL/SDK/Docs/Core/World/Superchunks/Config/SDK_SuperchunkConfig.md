# NQL SDK Superchunk Configuration

Owned files:

- `Core/World/Superchunks/Config/sdk_superchunk_config.h`
- `Core/World/Superchunks/Config/sdk_superchunk_config.c`

This slice owns runtime normalization and accessors for superchunk and wall-grid config.

Key entry points:

- `sdk_superchunk_validate_chunk_span`
- `sdk_superchunk_normalize_config`
- `sdk_superchunk_get_config`
- `sdk_superchunk_set_config`

It also owns the normalized accessors for chunk span, block span, wall period, wall-grid size/offset, gate width, and wall enable/detached flags.

Related docs:

- [../SDK_SuperChunks.md](../SDK_SuperChunks.md)
- [../Geometry/SDK_SuperchunkGeometry.md](../Geometry/SDK_SuperchunkGeometry.md)
- [../../GridWalls/SDK_GridWalls.md](../../GridWalls/SDK_GridWalls.md)

