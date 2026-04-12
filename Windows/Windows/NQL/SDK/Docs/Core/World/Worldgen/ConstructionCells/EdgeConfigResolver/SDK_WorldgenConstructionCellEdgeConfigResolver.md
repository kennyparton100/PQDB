# NQL SDK Worldgen Construction Cell Edge Config Resolver

Owned files:

- `Core/World/Worldgen/ConstructionCells/EdgeConfigResolver/sdk_worldgen_construction_cells_edge_config_resolver.h`
- `Core/World/Worldgen/ConstructionCells/EdgeConfigResolver/sdk_worldgen_construction_cells_edge_config_resolver.c`

This slice converts topology edge configurations into voxel-grid occupancy/material data for terrain-edge construction-cell generation.

Key entry points:

- `sdk_edge_config_resolver_init_grid`
- `sdk_edge_config_resolver_fill_from_config`
- `sdk_edge_config_resolver_fill_occupancy`
- `sdk_edge_config_resolver_dominant_material`

Use this file when changing how edge shapes turn into sub-block construction-cell geometry.

Related docs:

- [../SDK_WorldgenConstructionCells.md](../SDK_WorldgenConstructionCells.md)
- [../../../Chunks/Topology/SDK_ChunkTopology.md](../../../Chunks/Topology/SDK_ChunkTopology.md)
- [../../../ConstructionCells/SDK_ConstructionSystem.md](../../../ConstructionCells/SDK_ConstructionSystem.md)

