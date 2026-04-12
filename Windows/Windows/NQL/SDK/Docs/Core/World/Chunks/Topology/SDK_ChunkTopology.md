<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [World](../../SDK_WorldOverview.md) > [Chunks](../SDK_Chunks.md) > Topology

---

# NQL SDK Chunk Topology

This page documents the topology helper layer in `Core/World/Chunks/Topology/`.

## Scope

Owned files:

- `sdk_chunk_topology.h`
- `sdk_chunk_topology.c`

## Responsibilities

- scanning a chunk into a 2D surface/topology map
- detecting terrain-edge cells
- reporting per-column top height
- extracting unique edge configurations for terrain-edge construction-cell generation

## Key Entry Points

- `sdk_chunk_topology_analyze`
- `sdk_chunk_topology_is_edge`
- `sdk_chunk_topology_height_at`
- `sdk_chunk_topology_get_height_range`
- `sdk_chunk_topology_count_edge_blocks`
- `sdk_chunk_topology_find_unique_edges`

## Important Current Behavior

Topology is construction-aware. Surface scans must treat non-air construction cells with occupancy/display material as top-layer occupants rather than reading them back as air.

## Related Docs

- [../SDK_Chunks.md](../SDK_Chunks.md)
- [../../ConstructionCells/SDK_ConstructionSystem.md](../../ConstructionCells/SDK_ConstructionSystem.md)
- [../../Worldgen/ConstructionCells/SDK_WorldgenConstructionCells.md](../../Worldgen/ConstructionCells/SDK_WorldgenConstructionCells.md)
- [../../Worldgen/ConstructionCells/EdgeConfigResolver/SDK_WorldgenConstructionCellEdgeConfigResolver.md](../../Worldgen/ConstructionCells/EdgeConfigResolver/SDK_WorldgenConstructionCellEdgeConfigResolver.md)

