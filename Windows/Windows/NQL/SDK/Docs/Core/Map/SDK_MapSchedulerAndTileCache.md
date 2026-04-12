# NQL SDK - Map Scheduler And Tile Cache

This document describes the superchunk map scheduler, the in-memory tile cache used by the HUD map, and the disk cache stored under `MapCache/`.

## Scope

Ownership is split between:

- `Core/API/Internal/sdk_api_internal.h`
  - scheduler structs, cache constants, tile header structs, and public internal helpers
- `Core/API/Session/GameModes/GamePlay/Map/sdk_api_session_map.c`
  - scheduler implementation, worker threads, cache lookup, disk I/O, and debug compare helpers
- `Core/Frontend/sdk_frontend_async.c`
  - offline map-generation UI and progress reporting

This subsystem is separate from `sdk_worldgen_tile_cache.c`. That worldgen cache stores compressed continental-analysis tiles under `WorldGenCache/`; this subsystem stores rendered superchunk map tiles under `MapCache/`.

## Two Scheduler Modes

The scheduler exposes two modes through `SdkMapSchedulerMode`:

- `SDK_MAP_SCHED_MODE_INTERACTIVE`
  - started with an active world session
  - used to populate or refresh visible HUD tiles
  - paired with `SDK_MAP_BUILD_INTERACTIVE_FALLBACK`
- `SDK_MAP_SCHED_MODE_OFFLINE_BULK`
  - started from the frontend offline generation flow
  - walks the world in pages/rings and emits exact cached tiles
  - paired with `SDK_MAP_BUILD_EXACT_OFFLINE`

The current frontend uses offline bulk generation when the user requests world map generation for a selected save. The active world session starts the scheduler in interactive fallback mode.

## Worker Ownership

`SdkMapSchedulerInternal` owns:

- scheduler lock and worker condition variable
- worker threads and per-worker worldgen contexts
- queued tile jobs
- queued page jobs for offline bulk generation
- current bulk-cursor/ring state
- world identity (`world_save_id` and `world_seed`)
- build kind and worker count

Important limits/constants:

- `SDK_MAP_MAX_WORKERS = 16`
- `SDK_OFFLINE_MAP_GENERATION_THREADS = 12`
- `HUD_MAP_CACHE_SLOTS = 4096`
- `HUD_MAP_PIXEL_COUNT = 64 * 64`

Interactive and offline workers both build map pixels from worldgen, but the exact and fallback paths are separate implementations.

## Tile Build Kinds

The scheduler currently distinguishes two output qualities:

- `SDK_MAP_BUILD_INTERACTIVE_FALLBACK`
  - lower-cost path used for interactive HUD updates
- `SDK_MAP_BUILD_EXACT_OFFLINE`
  - exact path used for offline bulk generation and disk persistence

The tile cache remembers which build kind produced a tile. Runtime code prefers exact offline tiles from disk when they exist and falls back to interactive tiles otherwise.

## Disk Cache Layout

Tile files are written under:

```text
MapCache/
  rev_<SDK_MAP_TILE_CACHE_VERSION>/
    <world_save_id>/
      sc_<scx>_<scz>.tile
```

If no world-save id is available, the fallback directory is:

```text
MapCache/
  rev_<SDK_MAP_TILE_CACHE_VERSION>/
    seed_<world_seed>/
```

The revision directory is derived from:

- `SDK_MAP_RENDER_CACHE_VERSION`
- `SDK_PERSISTENCE_WORLDGEN_REVISION`
- `SDK_MAP_TILE_CACHE_VERSION = (worldgen_revision << 16) | render_cache_version`

That ties map-tile cache invalidation to both terrain semantics and tile-rendering format changes.

## Tile File Format

Persisted tile files use `SdkPersistedMapTile`.

Header fields:

- `magic`
- `version`
- `build_kind`
- `seed`
- `origin_x`
- `origin_z`
- `dim`

Payload:

- `pixels[HUD_MAP_PIXEL_COUNT]`

The loader validates all header fields, including build kind, seed, origin, and dimension, before accepting a disk tile as valid.

## Runtime Cache Behavior

The in-memory cache is `g_superchunk_map_cache`.

Each entry stores:

- validity/state flags
- build kind
- optional debug-compare state
- tile origin
- stamp
- `64 x 64` pixels

Tile lookup prefers:

1. ready in-memory entry
2. exact offline disk tile for the active world identity
3. queued or freshly built fallback tile

This is why a session can immediately consume exact cached tiles from previous offline generation runs.

## Frontend Offline Generation

The frontend tracks offline generation progress separately from world-session startup.

Visible statuses include:

- active generation progress
- queued pages and inflight tiles
- worker count
- last tile timing/chunk counts
- shutdown phase status such as `Stopping map tile workers...`

The scheduler is explicitly shut down through a request/poll handshake before the frontend considers generation complete.

## Debug Compare Path

`Core/API/Session/GameModes/GamePlay/Map/sdk_api_session_map.c` includes a map debug-compare helper that can build both exact and fallback tiles for the same origin and record a compare note through the profiler.

This path exists to answer a very specific maintainer question:

- how different is the exact offline tile from the current interactive fallback tile?

It is diagnostics infrastructure, not part of the normal runtime cache path.

## Related Docs

- [SDK_RuntimeSessionAndFrontend.md](../API/Session/SDK_RuntimeSessionAndFrontend.md)
- [SDK_WorldgenTerrainPipeline.md](../World/SDK_WorldgenTerrainPipeline.md)
- [SDK_PersistenceAndStorage.md](../World/Persistence/SDK_PersistenceAndStorage.md)
