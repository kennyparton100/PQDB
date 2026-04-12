<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > [Guides](SDK_GuidesOverview.md) > Session Start Flow

---

# Session Start Flow

This guide explains how a world session starts in the current engine.

It is aimed at change work, not just understanding. The focus is:

- what calls what
- when settings become runtime state
- when the world becomes playable
- which files are safe to edit for which type of startup change

## High-Level Flow

Current local start path:

```text
sdk_frontend_menu.c
  -> begin_async_world_session_load(...)

sdk_frontend_async.c
  -> frontend_open_world_generating()
  -> g_world_generation_stage = 2

sdk_frontend_worldgen.c
  -> update_async_world_generation()
  -> start_world_session(...)

sdk_api_session_core.c
  -> persistence init
  -> worldgen init
  -> map scheduler init
  -> chunk manager init
  -> construction registry bind
  -> chunk streamer init
  -> restore state or choose spawn
  -> build desired chunks
  -> bootstrap_visible_chunks_sync()
  -> mark world session active
```

## Primary Files

Load these first for startup work:

- `Core/Frontend/sdk_frontend_async.c`
- `Core/Frontend/sdk_frontend_worldgen.c`
- `Core/API/Session/Core/sdk_api_session_core.c`
- `Core/API/Session/Bootstrap/sdk_api_session_bootstrap.c`
- `Core/API/Session/Bootstrap/sdk_api_session_bootstrap_policy.h`

Load these next if the change touches residency/streaming:

- `Core/World/Chunks/ChunkManager/sdk_chunk_manager.h`
- `Core/World/Chunks/ChunkManager/sdk_chunk_manager.c`
- `Core/World/Chunks/ChunkStreamer/sdk_chunk_streamer.c`

Load these next if the change touches save/load or world desc:

- `Core/World/Persistence/sdk_persistence.c`
- `Core/World/Persistence/sdk_world_tooling.c`

## Detailed Runtime Bring-Up

`start_world_session(...)` in `sdk_api_session_core.c` currently does this:

1. reset prior session if one is active
2. initialize persistence for the selected save
3. apply `SdkWorldSaveMeta` settings to runtime config
4. initialize worldgen
5. initialize map scheduler
6. reset runtime/session state
7. load station state
8. initialize chunk manager
9. bind the persistence-backed construction registry
10. initialize chunk streamer
11. initialize entities
12. restore persisted state or choose a new spawn
13. build desired chunk topology
14. synchronously bootstrap nearby chunks
15. set `g_sdk.world_session_active = true`

## Where Settings Become Runtime State

The critical handoff happens near the top of `start_world_session(...)`.

Current split:

- `SdkWorldDesc`
  - `seed`
  - `settlements_enabled`
  - `construction_cells_enabled`
- `SdkSuperchunkConfig`
  - `enabled`
  - `chunk_span`
  - `walls_enabled`
  - `walls_detached`
  - `wall_grid_size`
  - `wall_grid_offset_x`
  - `wall_grid_offset_z`

If a new startup-affecting option is missing here, the runtime will silently use defaults.

## When The World Becomes Playable

The current engine does not wait for the full world backlog before entering gameplay.

Playability now happens after `bootstrap_visible_chunks_sync()` succeeds.

That bootstrap currently does this:

1. count the sync-safe target set
2. schedule startup-priority chunk work
3. wait until nearby sync-safe chunks are resident
4. wait until that same nearby set is GPU-ready
5. persist loaded chunks
6. hand the remaining backlog to background streaming

Then startup finishes and gameplay begins.

This is a major behavior change from older "wait for everything" assumptions.

## Bootstrap Contracts

`bootstrap_visible_chunks_sync()` is the startup gate.

Important current rules:

- it only waits for the nearby safety set, not all desired chunks
- it tracks:
  - desired
  - resident
  - gpu-ready
  - active workers
  - pending jobs
  - pending results
- it has bounded stall timeouts
- after success, background streaming continues outside the startup gate

## Current Startup Status Text Model

The status text is no longer meant to imply that every terrain/wall task is finished before play begins.

The maintained startup model is:

- load enough nearby terrain safely
- enter the world
- keep broader backlog in the background

If you change startup behavior, update the status text honestly.

## Safe Edit Zones

### Safe startup-flow edits

Usually safe if done carefully:

- changing progress/status text
- changing which metadata fields are applied to `SdkWorldDesc`
- changing which metadata fields are applied to `SdkSuperchunkConfig`
- adjusting frontend handoff for world-session load

### High-risk startup edits

Require more context before touching:

- construction registry binding
- sync-safe target classification
- streamer startup priority scheduling
- chunk adoption / result draining
- wall-support startup behavior

These are the areas most likely to create hangs, false readiness, or memory faults.

## Logs To Use

For startup debugging, the most useful current logs are:

- `[GEN_STAGE]`
- `[BOOTSTRAP]`
- `[READINESS]`
- `[STARTUP]`
- `[RESIDENCY]`
- `[WALL]`

Useful interpretation:

- if `resident` never moves, the sync-safe set is not being loaded or counted correctly
- if `resident` reaches target but `gpu_ready` stalls, upload/full-mesh readiness is the blocker
- if startup succeeds but the world later destabilizes, the issue is likely post-bootstrap residency or background streaming, not session start itself

## Current Known Hot Spots

These are important when editing startup-related code:

- construction registry lifetime is still a crash-sensitive area
- background expansion after startup is still more aggressive than ideal
- wall-support backlog is still a fragile part of the residency model

Do not assume "startup succeeded once" means the startup architecture is already stable.

## Verification Checklist

For any session-start change:

1. Start a fresh world.
2. Start an existing world with persisted state.
3. Confirm the correct spawn path runs.
4. Confirm nearby chunk bootstrap completes.
5. Confirm the world reaches `g_sdk.world_session_active = true`.
6. Confirm no startup-stage regression in `[READINESS]` and `[BOOTSTRAP]` logs.
7. Confirm background streaming still continues after entering the world.

If the change touches metadata:

8. Verify the selected save's `meta.txt`.
9. Verify runtime uses the same values after reload.

## Minimal File Set For This Task

Load these first:

- `Core/Frontend/sdk_frontend_async.c`
- `Core/Frontend/sdk_frontend_worldgen.c`
- `Core/API/Session/Core/sdk_api_session_core.c`
- `Core/API/Session/Bootstrap/sdk_api_session_bootstrap.c`

Then load the owning subsystem for the specific startup change.

---

## Related Guides

- [SDK_AIChangeContext.md](SDK_AIChangeContext.md)
- [SDK_AddWorldCreateAndWorldgenOptions.md](SDK_AddWorldCreateAndWorldgenOptions.md)
- [NewWorldCreation/SDK_NewWorldCreationCallStack.md](NewWorldCreation/SDK_NewWorldCreationCallStack.md)
- [../Core/API/Session/SDK_RuntimeSessionAndFrontend.md](../Core/API/Session/SDK_RuntimeSessionAndFrontend.md)

---
*Practical guide for world-session startup and bootstrap changes*
