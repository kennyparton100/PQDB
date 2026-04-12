# NQL SDK - Settlement System

This document describes the settlement system as it exists in the current codebase. It covers metadata generation, chunk integration, persistence, and the separate loaded-settlement runtime.

## Scope

The settlement subsystem spans:

- `sdk_settlement_types.h`
  - public types and metadata shapes
- `sdk_settlement.h`
  - public entry points
- `sdk_settlement.c`
  - metadata generation, caching, chunk integration, and queries
- `sdk_settlement_layout.c`
  - deterministic building layouts
- `sdk_settlement_building.c`
  - shell/block generation for buildings
- `sdk_settlement_terrain.c`
  - zone terrain preparation
- `sdk_settlement_roads.c`
  - route and path generation into chunks
- `sdk_settlement_persistence.c`
  - settlement metadata save/load
- `sdk_settlement_runtime.c`
  - loaded runtime buildings, residents, and runtime debug/perf state

## Public Data Shapes

Key public types:

- `SettlementMetadata`
  - persistent per-settlement metadata
- `SuperchunkSettlementData`
  - metadata bucket for one `16 x 16` chunk superchunk
- `SettlementLayout`
  - deterministic building placement list derived from metadata
- `SettlementSuitability`
  - scores used during siting
- `SettlementDebugInfo`
  - query result for debug overlays and reports

Important public limits:

- `SDK_MAX_SETTLEMENTS_PER_SUPERCHUNK = 32`
- `SDK_MAX_CHUNKS_PER_SETTLEMENT = 64`
- per-chunk reverse index stores up to `8` settlement references

## What Is Implemented Today

The current system does more than define types:

- generates superchunk settlement metadata on demand
- caches settlement metadata inside the worldgen implementation
- serializes metadata to versioned settlement files
- builds deterministic layouts from metadata
- modifies chunk terrain around settlement zones
- places roads/routes and building shells during chunk generation
- exposes chunk-local and point-local debug queries
- runs a separate loaded-settlement runtime that binds buildings to props, computes runtime markers, and spawns residents/entities

## What The Generator Actually Places

The type system supports villages, towns, and cities.

The current superchunk metadata generator in `sdk_settlement.c` only emits:

- villages
- towns

The generator currently does not place cities, even though the public enums and layout code support the idea of cities.

## Superchunk Metadata Generation

Settlement metadata is generated per superchunk.

Current flow inside `generate_superchunk_settlements`:

1. Divide the superchunk into `64 x 64` block candidate cells.
2. Probe four candidate points per cell.
3. Sample `SdkTerrainColumnProfile` plus approximate continental data.
4. Build best village and best town candidates per cell.
5. Keep candidates above the current score thresholds.
6. Sort by competitiveness.
7. Reject candidates that are:
   - underwater
   - too close to superchunk wall bounds
   - too close to gate areas
   - too close to already accepted settlements
8. Materialize `SettlementMetadata`, generate foundations, and compute chunk intersections.
9. Build the reverse chunk-to-settlement index.

The current siting pass logs extensive `[SETTLEMENT]` debug output when metadata is loaded or generated.

## Chunk Integration

Settlement chunk integration happens from worldgen, not from a separate post-process.

`sdk_worldgen_fill_chunk` currently:

1. generates terrain columns and water
2. generates surface flora
3. fetches `SuperchunkSettlementData`
4. calls `sdk_settlement_generate_for_chunk`
5. seals water and reapplies superchunk walls

For wall chunks the terrain path is shorter, but settlement generation still runs.

Inside `sdk_settlement_generate_for_chunk`, each settlement can:

- prepare zone terrain for intersecting chunks
- generate routes and internal paths via `sdk_settlement_generate_routes_for_chunk`
- place building shells for buildings intersecting the chunk

## Layouts, Building Families, And Runtime Markers

`sdk_settlement_generate_layout` is the deterministic bridge from metadata to per-building placement.

The loaded-settlement runtime then translates layout entries into:

- `SdkSettlementBuildingInstance`
- `SdkBuildingFamily`
- desired prop ids from `sdk_building_family.c`
- runtime markers such as entrance, sleep, work, storage, patrol, water, and station markers

`sdk_role_assets.c` resolves NPC roles and building types to authored character/prop ids and also marks missing authored assets.

This means settlement generation has two distinct outputs:

- chunk-time terrain and building shell blocks
- loaded-runtime building/resident state for active settlements

## Loaded Settlement Runtime

`sdk_settlement_runtime.c` is separate from metadata generation.

It owns:

- active runtime slots for loaded settlements
- derived building instances and runtime markers
- resident planning and runtime entity spawn/despawn
- support-block seeding for required stations/markers
- runtime debug summaries and perf counters

Important current runtime limits:

- `SDK_SETTLEMENT_RUNTIME_MAX_ACTIVE = 64`
- `SDK_SETTLEMENT_RUNTIME_MAX_BUILDINGS = 96`
- `SDK_SETTLEMENT_RUNTIME_MAX_RESIDENTS = 16`
- `SDK_SETTLEMENT_RUNTIME_MAX_MARKERS = 8`

This runtime only activates for settlements relevant to loaded chunk-manager state.

## Persistence

Settlement metadata is serialized independently from `save.json`.

Current file contract:

- filename pattern: `settlements_sc<scx>_<scz>.dat`
- magic: `SETT`
- current version: `3`
- backward readers for versions `1` and `2`

Version 3 stores:

- `SettlementMetadataV3`
- reverse chunk lookup tables (`chunk_settlement_count` and `chunk_settlement_indices`)

The worldgen implementation keeps an LRU cache of:

- `SDK_SETTLEMENT_CACHE_SLOTS = 8`

When an entry is evicted or the worldgen shuts down, the cache flush path attempts to save the metadata back out.

Important current caveat:

- the persistence helpers expect a base path string to append `settlements_sc...dat` onto
- the current session wiring passes the persistence save-path string into `sdk_settlement_set_world_path`

Treat the path contract as a maintenance-sensitive area when touching settlement save/load behavior.

## Public API Surface

High-signal public helpers:

- `sdk_settlement_generate_for_chunk`
- `sdk_settlement_get_or_create_data`
- `sdk_settlement_query_debug_at`
- `sdk_settlement_evaluate_suitability`
- `sdk_settlement_generate_layout`
- `sdk_settlement_generate_building`
- `sdk_settlement_save_superchunk`
- `sdk_settlement_load_superchunk`
- `sdk_settlement_set_world_path`
- `sdk_settlement_flush_cache`
- `sdk_settlement_get_for_chunk`

The reverse lookup helper `sdk_settlement_get_for_chunk` is what debug/reporting paths use when they need to know which settlements overlap a chunk.

## Current Limits

- The generator currently places villages and towns only.
- Chunk reverse lookup stores at most eight settlement references per chunk cell.
- Persistence format exists and is versioned, but the world-path contract is easy to misuse.
- Runtime residents/buildings are bounded by fixed-size arrays per active settlement runtime.

## Related Docs

- [SDK_WorldgenTerrainPipeline.md](../SDK_WorldgenTerrainPipeline.md)
- [SDK_Buildings.md](../Buildings/SDK_Buildings.md) - Building types and markers
- [SDK_SettlementRoads.md](Roads/SDK_SettlementRoads.md) - Route and road generation
- [SDK_ContentToolsAndAssetLibraries.md](../../../Build/SDK_ContentToolsAndAssetLibraries.md)
- [SDK_PersistenceAndStorage.md](../Persistence/SDK_PersistenceAndStorage.md)
- [SDK_RuntimeDiagnostics.md](../../Runtime/SDK_RuntimeDiagnostics.md)
