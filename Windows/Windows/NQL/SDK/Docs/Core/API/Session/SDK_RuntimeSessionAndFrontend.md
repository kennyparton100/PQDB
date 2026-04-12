<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [API](../SDK_APIReference.md) > Session

---

# NQL SDK Runtime Session And Frontend

This page describes the current world-start flow and the frontend settings that materially affect runtime worlds.

## Startup Flow

High-level session bring-up:

1. open `save.json` / `meta.txt`
2. initialize worldgen with the selected `SdkWorldDesc`
3. apply normalized superchunk and wall config
4. bind persistence-backed construction registry
5. initialize chunk manager and streamer
6. restore player/world state or choose a spawn
7. build the desired residency set
8. synchronously load the nearby safety set to residency
9. synchronously ensure that same safety set is GPU-ready
10. mark the world playable and continue wall/remesh backlog in the background

The runtime entrypoint translation unit is still `Core/API/Session/sdk_api_session.c`, but the maintained implementation is now split across feature modules such as:

- `Core/API/Session/Core/sdk_api_session_core.c`
- `Core/API/Session/Bootstrap/sdk_api_session_bootstrap.c`
- `Core/API/Session/Core/sdk_api_session_generation.c`
- `Core/API/Session/GameModes/GamePlay/Spawn/sdk_api_session_spawn.c`
- `Core/API/Session/GameModes/Editor/sdk_api_session_editor.c`
- `Core/API/Session/GameModes/Editor/sdk_api_session_editor_helpers.c`
- `Core/API/Session/GameModes/Editor/sdk_api_session_prop_editor.c`

## Frontend Settings That Now Reach Runtime

The current world-create surface is expected to be real runtime input:

- seed
- render distance
- spawn mode
- settlements enabled
- construction cells enabled
- superchunks enabled
- superchunk chunk span
- walls enabled
- walls detached
- wall grid size
- wall grid offset x/z

The offline world-generation and exact-map tasks now explicitly propagate these values instead of only guaranteeing the seed.

Spawn mode is now persisted through `meta.txt` as:

- `0 = random`
- `1 = center/classic`
- `2 = safe`

Older worlds without `spawn_mode` default to `2` (`safe`).

## Startup Status Text

Current major statuses:

- `Opening world save...`
- `Initializing world generator...`
- `Preparing world systems...`
- `Loading station state...`
- `Preparing chunk systems...`
- `Initializing entities...`
- `Restoring world state...`
- `Choosing random/center/safe spawn...`
- `Planning visible chunks...`
- `Loading nearby terrain desired/resident/gpu_ready`
- `Nearby terrain ready. Background streaming continues...`
- `Finalizing world session...`
- `World session ready`

Older staged texts such as `Preparing perimeter walls ...` and `Finishing terrain chunks ...` are no longer the maintained description of startup behavior.

Successful startup now enters gameplay immediately after bootstrap succeeds. The old post-load blocking generation summary is no longer part of the success path.

## Construction Cells

`construction_cells_enabled` now drives real worldgen behavior.

When enabled:

- the flag survives world-create save/load
- it reaches `SdkWorldDesc`
- worldgen places deterministic settlement-linked construction cells
- those cells use the normal chunk construction registry and persistence path

## Detached Walls

Detached walls are no longer metadata only.

Runtime now uses:

- terrain superchunk helpers for terrain ownership/local interior coords
- detached wall-grid helpers for wall classification/grouping when detached mode is enabled

Normalization rules:

- walls require superchunks
- detached mode defaults to `wall_grid_size = chunk_span + 2`
- attached mode normalizes `wall_grid_size = chunk_span`

## Known Limits

- Startup no longer sync-loads the full wall-support backlog, but unusual detached offsets still need wider live validation.
- Exact-map generation has its own progress model and is still less detailed than runtime bootstrap reporting.
- Bootstrap failure is now bounded rather than hanging indefinitely. If residency or GPU readiness stops advancing, startup fails through a shared cleanup path and returns to the frontend instead of spinning forever.

---

## Related Documentation

### Up to Parent
- [SDK API Reference](../SDK_APIReference.md) - Public API surface
- [SDK Core Overview](../../SDK_CoreOverview.md) - Core subsystems hub

### Frontend & UI
- [../../Frontend/SDK_Frontend.md](../../Frontend/SDK_Frontend.md) - Frontend UI flow
- [../../Settings/SDK_Settings.md](../../Settings/SDK_Settings.md) - Settings system
- [../../Settings/SDK_SettingsAndControls.md](../../Settings/SDK_SettingsAndControls.md) - Settings and controls

### World Systems
- [../../World/SDK_WorldOverview.md](../../World/SDK_WorldOverview.md) - World architecture
- [../../World/Chunks/ChunkManager/SDK_ChunkManager.md](../../World/Chunks/ChunkManager/SDK_ChunkManager.md) - Chunk residency
- [../../World/Persistence/SDK_PersistenceAndStorage.md](../../World/Persistence/SDK_PersistenceAndStorage.md) - Persistence

### Platform & Entry
- [../../../Platform/SDK_PlatformWin32.md](../../../Platform/SDK_PlatformWin32.md) - Win32 platform
- [../../../App/SDK_AppOverview.md](../../../App/SDK_AppOverview.md) - App entry point

---
*Documentation for `SDK/Core/API/Session/`*
