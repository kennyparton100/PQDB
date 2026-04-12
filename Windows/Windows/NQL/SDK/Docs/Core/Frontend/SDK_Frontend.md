<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../SDK_Overview.md) > [Core](../SDK_CoreOverview.md) > Frontend

---

# SDK Frontend

This page describes the current frontend/start-menu layer as it actually exists now.

Use this page for current ownership and flow. For task-oriented edit instructions, start with:

- [../../Guides/SDK_AddMenuOptions.md](../../Guides/SDK_AddMenuOptions.md)
- [../../Guides/SDK_AddWorldCreateAndWorldgenOptions.md](../../Guides/SDK_AddWorldCreateAndWorldgenOptions.md)

## Scope

The frontend is the UI/runtime handoff layer for:

- main menu navigation
- world list and world actions
- create-world settings
- async world-session launch
- async exact-map generation
- character/prop/block/item/animation browsers
- local-host/server menu state

## Current File Ownership

Primary files:

- `Core/Frontend/sdk_frontend_menu.c`
  - menu state transitions, input handling, UI snapshot population
- `Core/Frontend/sdk_frontend_worlds.c`
  - world list refresh, world creation requests, meta path helpers
- `Core/Frontend/sdk_frontend_async.c`
  - async world-session load and exact-map generation entry points
- `Core/Frontend/sdk_frontend_worldgen.c`
  - async generation/session-start progress loop
- `Core/Server/sdk_server_runtime.c`
  - saved server and local-host UI/runtime state

Shared declarations and global storage:

- `Core/Frontend/sdk_frontend_internal.h`
- `Core/API/Internal/sdk_api_internal.h`
- `Core/API/sdk_api.c`

## Runtime Shape

The important split is:

- `frontend_handle_input()`
  - mutates frontend state and launches actions
- `push_start_menu_ui()`
  - copies current frontend state into the renderer-facing UI payload
- `present_start_menu_frame()`
  - renders the current menu frame

The renderer does not own frontend logic. It consumes the UI snapshot the frontend already built.

## Main Views

The current frontend is driven by `g_frontend_view`.

Important views:

- main menu
- world select
- world create
- world actions
- world generating
- world map generating
- characters
- props
- blocks
- items
- animation list
- online
- online edit server

## Current World-Create Flow

The current create-world path is:

```text
frontend_open_world_create_menu()
  -> initialize g_world_create_* state

frontend_handle_input()
  -> edit g_world_create_* values
  -> create_new_world_save_with_settings(...)
  -> refresh_world_save_list()
  -> begin_async_world_session_load(...)
```

This is important:

- create-world now goes through `SdkWorldCreateRequest` and `sdk_world_create(...)`
- it is not just hand-written `meta.txt` assembly in the menu

## Current World-Create State

The maintained create-world state currently includes:

- seed
- spawn mode
- render distance
- settlements enabled
- construction cells enabled
- superchunks enabled
- superchunk chunk span
- walls enabled
- walls detached
- wall grid size
- wall grid offset x
- wall grid offset z

These values are:

1. edited in `sdk_frontend_menu.c`
2. copied into `SdkWorldCreateRequest` in `sdk_frontend_worlds.c`
3. normalized and persisted through `sdk_world_create(...)` in `sdk_world_tooling.c`

## World Metadata

The current world metadata file is:

- `meta.txt`

The current save file is:

- `save.json`

Do not rely on older docs that still mention `world_meta.txt`.

## Async Session Load

Starting a world from the frontend now uses:

```text
begin_async_world_session_load(...)
  -> frontend_open_world_generating()
  -> mark generation UI active
  -> set stage to session start

update_async_world_generation()
  -> start_world_session(...)
```

That means:

- the frontend owns the progress UI
- the session layer owns the actual world bring-up

## Exact Map Generation

Exact map generation is a separate frontend task from world-session startup.

Current path:

```text
begin_async_world_map_generation(...)
  -> frontend_open_world_map_generating()
  -> init map scheduler in offline mode
  -> pump map scheduler each frame
  -> request/poll shutdown
```

This is not the same as create-world startup.

## Practical Editing Rules

When changing frontend behavior:

- edit menu state in `sdk_frontend_menu.c`
- edit world creation request/handoff in `sdk_frontend_worlds.c`
- edit async launch behavior in `sdk_frontend_async.c` or `sdk_frontend_worldgen.c`
- edit saved-server/local-host behavior in `Core/Server/sdk_server_runtime.c`

Do not put real subsystem logic into the menu file if another module already owns it.

## Known Weak Spots

- frontend state still relies heavily on globals
- `sdk_frontend_menu.c` remains a large, branching file
- older docs and comments still occasionally refer to the pre-refactor flat source layout

## Related Documentation

- [../../Guides/SDK_GuidesOverview.md](../../Guides/SDK_GuidesOverview.md)
- [../../Guides/SDK_AddMenuOptions.md](../../Guides/SDK_AddMenuOptions.md)
- [../../Guides/SDK_AddWorldCreateAndWorldgenOptions.md](../../Guides/SDK_AddWorldCreateAndWorldgenOptions.md)
- [../API/Session/SDK_RuntimeSessionAndFrontend.md](../API/Session/SDK_RuntimeSessionAndFrontend.md)
- [../Server/SDK_OnlineAndServerRuntime.md](../Server/SDK_OnlineAndServerRuntime.md)

---
*Documentation for `SDK/Core/Frontend/`*
