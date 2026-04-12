# NQL SDK - Worldgen Terrain Pipeline

This document describes the current terrain-generation pipeline from world descriptor to generated chunk blocks.

## Public Contract Versus Internal Layers

The stable public entry points are in `Core/World/sdk_worldgen.h`.

Important public calls:

- `sdk_worldgen_init`
- `sdk_worldgen_init_ex`
- `sdk_worldgen_shutdown`
- `sdk_worldgen_sample_column_ctx`
- `sdk_worldgen_get_surface_y_ctx`
- `sdk_worldgen_generate_chunk_ctx`
- `sdk_worldgen_scan_resource_signature`

The gameplay-facing terrain sample contract is still:

- `SdkTerrainColumnProfile`

The internal pipeline beneath that sample is layered and cached.

## Layer Order

The current terrain stack is:

```text
SdkWorldDesc
  -> continental tile analysis
  -> macro tile synthesis and hydrology
  -> region tile synthesis
  -> column profile sampling
  -> chunk block fill
  -> caves, fluids, lava, and flora
  -> settlement terrain/buildings/routes
  -> superchunk wall application and sealing
```

The key point is that terrain semantics are not produced by one monolithic noise call. The system progressively refines larger-scale data into chunk-time block placement.

## Layer Responsibilities

### Continental Layer

The continental layer owns the broadest terrain and hydrology signals, including:

- filled and raw continental height
- coast distance and ocean mask
- precipitation and runoff
- flatness
- lake and basin data
- water access, harbor score, confluence, and flood-risk signals

These values feed terrain shaping, settlement suitability, and spawn scoring.

### Macro Layer

The macro layer adds larger-scale terrain synthesis and hydrology refinement.

It is also where `sdk_worldgen_run_hydrology` is applied after macro tile generation.

### Region Layer

The region layer resolves more local geology and terrain controls, including:

- weathered/upper/lower/basement boundaries
- stratigraphy control
- terrain and bedrock provinces
- soil/drainage/fertility descriptors
- water-table depth
- volcanic and resource-related controls

### Column Sampling

Column sampling produces `SdkTerrainColumnProfile`, which is the main terrain summary consumed by:

- chunk filling
- spawn scoring
- settlement suitability
- surface-block selection
- debug and reporting helpers

### Chunk Fill

`sdk_worldgen_fill_chunk` is the main chunk materialization path.

Current order for a normal chunk:

1. clear the chunk
2. sample each column and fill bedrock, soil, sediment, and stratigraphic blocks
3. carve geology caves
4. add standing water, rivers, and lava where applicable
5. place trees and surface plants
6. fetch settlement data and apply settlement terrain, routes, and buildings
7. seal generated water
8. apply superchunk walls

For full wall chunks, the function short-circuits terrain generation, applies the wall first, and still runs settlement generation afterward.

## Cache Layers

The worldgen implementation currently uses several distinct cache concepts.

### Disk Continental Tile Cache

`sdk_worldgen_init` defaults to `SDK_WORLDGEN_CACHE_DISK`, which allocates the disk-backed tile cache from `sdk_worldgen_tile_cache.c`.

That cache:

- stores continental-analysis tiles in `WorldGenCache/`
- compresses tiles with LZ4
- keys them by world seed and tile coordinates

### Shared In-Memory Tile Caches

`sdk_worldgen_shared_cache.c` implements global shared caches for:

- continental tiles
- macro tiles
- region tiles

Current slot counts:

- `64` continental
- `32` macro
- `32` region

These caches are the shared tile path used by the higher-level worldgen accessors. Their init/shutdown APIs are explicit, so startup/shutdown code must treat cache availability as an application-level invariant.

### Settlement Metadata Cache

Settlement metadata is cached separately inside the worldgen implementation:

- `SDK_SETTLEMENT_CACHE_SLOTS = 8`

That cache is not a terrain tile cache. It stores `SuperchunkSettlementData` keyed by superchunk coordinates.

## Stable Downstream Consumers

The current terrain pipeline feeds multiple other systems:

- settlement suitability and layout in `sdk_settlement*.c`
- chunk-time block generation
- worldgen debug reports
- resource-signature scans
- map tile generation for both interactive and offline scheduler paths
- spawn scoring in `Core/API/Session/GameModes/GamePlay/Spawn/sdk_api_session_spawn.c`

When changing terrain semantics, assume these downstream consumers will also need review.

## Important Extension Boundaries

If you need to add new terrain behavior, prefer extending at one of these boundaries:

- add new large-scale analysis to continental or macro tiles
- add new geology/soil/resource controls to region sampling
- extend `SdkTerrainColumnProfile` only when the new signal needs to be consumed outside worldgen internals
- keep chunk fill order explicit rather than hiding new behavior inside unrelated block-placement helpers

## Current Maintenance Notes

- `sdk_worldgen_init_ex` is the main opt-in point for cache mode selection.
- `SdkTerrainColumnProfile` remains the compatibility boundary most gameplay/runtime systems care about.
- Map tile caching is a separate subsystem under `MapCache/`; it is documented in [SDK_MapSchedulerAndTileCache.md](../Map/SDK_MapSchedulerAndTileCache.md).
- Settlement persistence and chunk cache invalidation are documented separately in [SDK_PersistenceAndStorage.md](Persistence/SDK_PersistenceAndStorage.md).

## Related Docs

- [SDK_Worldgen.md](Worldgen/SDK_Worldgen.md) - Public API and world generation system
- [SDK_WorldgenScheduler.md](Worldgen/Scheduler/SDK_WorldgenScheduler.md) - Async bulk generation
- [SDK_WorldgenInternal.md](Worldgen/Internal/SDK_WorldgenInternal.md) - Internal tile caches and geology
- [SDK_SettlementSystem.md](Settlements/SDK_SettlementSystem.md) - Settlement integration
- [SDK_MapSchedulerAndTileCache.md](../Map/SDK_MapSchedulerAndTileCache.md) - Map tile subsystem
- [SDK_PersistenceAndStorage.md](Persistence/SDK_PersistenceAndStorage.md) - Save/load and cache invalidation
