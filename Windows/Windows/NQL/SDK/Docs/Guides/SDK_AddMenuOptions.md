<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > [Guides](SDK_GuidesOverview.md) > Add Menu Options

---

# Add Menu Options

This guide covers how to add or change frontend menu options in the current engine.

It is written for the real codebase, where menu behavior is split across:

- frontend input handling
- frontend state globals
- UI snapshot population
- world launch helpers

## First Principle

Do not treat the menu as a single file.

A correct menu change usually touches at least two layers:

1. state and input
2. the action or data handoff behind the menu item

If you only change the visible UI text, the feature is not real.

## Current Frontend Ownership

Primary files:

- `Core/Frontend/sdk_frontend_menu.c`
  - input handling, view transitions, UI snapshot population, world-create row behavior
- `Core/Frontend/sdk_frontend_internal.h`
  - frontend entry points
- `Core/API/Internal/sdk_api_internal.h`
  - shared frontend/runtime declarations and globals
- `Core/API/sdk_api.c`
  - storage for many frontend globals

Common follow-on files:

- `Core/Frontend/sdk_frontend_worlds.c`
  - world save creation and metadata flow
- `Core/Frontend/sdk_frontend_async.c`
  - start world session / local host / exact map launch handoff
- `Core/Frontend/sdk_frontend_worldgen.c`
  - async world generation and session-start progress loop

## How The Frontend Actually Works

Simplified runtime loop:

```text
nqlsdk_frame()
  -> frontend_handle_input()
  -> push_start_menu_ui()
  -> present_start_menu_frame()
```

Meaning:

- `frontend_handle_input()` mutates state and launches actions
- `push_start_menu_ui()` packages the current state into the UI payload used by the renderer
- the renderer displays what the frontend already decided

If a menu option is interactive, the behavior lives in the input/state path first, not in rendering.

## Safe Menu-Edit Workflow

## 1. Decide what kind of option you are adding

There are three common cases:

- display-only option
  - changes text or presentation only
- stateful menu option
  - changes frontend state and survives while the view remains active
- launch/runtime option
  - must eventually affect world creation, world loading, or another subsystem

Most mistakes happen when the last case is implemented like the middle case.

## 2. Find the view that owns the option

The active screen is selected by `g_frontend_view` in `sdk_frontend_menu.c`.

Typical places:

- main menu
- world select
- world actions
- world create
- online/server screens

Keep the option inside the correct view branch in `frontend_handle_input()`.

## 3. Add or reuse backing state

For menu state that must persist across frames, back it with real state.

Examples already present:

- `g_world_create_seed`
- `g_world_create_render_distance`
- `g_world_create_spawn_type`
- `g_world_create_settlements_enabled`
- `g_world_create_construction_cells_enabled`

Where these live:

- declarations in `Core/API/Internal/sdk_api_internal.h`
- storage in `Core/API/sdk_api.c`
- mutation in `Core/Frontend/sdk_frontend_menu.c`

If your option needs persistent frontend state, follow that pattern.

## 4. Populate the UI snapshot

After changing the state model, make sure `push_start_menu_ui()` exports the values the renderer expects.

If the renderer needs a field that does not already exist in the start-menu UI payload, extend the shared UI data shape before trying to draw it.

## 5. Wire the action path

If activating the option should do real work, route it to the owning helper instead of embedding everything in the menu file.

Examples:

- world save creation -> `sdk_frontend_worlds.c`
- session launch -> `sdk_frontend_async.c`
- async generation loop -> `sdk_frontend_worldgen.c`

The menu should choose and launch. It should not become the real implementation.

## Current World-Create Row Map

As of the current code in `sdk_frontend_menu.c`, `g_world_create_selected` means:

| Row | Meaning |
|---|---|
| `0` | seed |
| `1` | spawn mode |
| `2` | render distance |
| `3` | settlements enabled |
| `4` | construction cells enabled |
| `5` | superchunks enabled |
| `6` | superchunk chunk span |
| `7` | walls enabled |
| `8` | walls detached |
| `9` | detached wall grid size |
| `10` | detached wall grid offset x |
| `11` | detached wall grid offset z |

If you insert a new world-create option, you must review:

- up/down selection bounds
- left/right behavior
- text-input behavior for numeric rows
- activate behavior
- the UI snapshot

## Common Menu Mistakes

### Adding visible text without backing state

This creates fake UI. It looks implemented and does nothing.

### Changing frontend globals but not the launch handoff

This is common for world-create options. The menu changes, but runtime behavior never sees it.

### Putting too much logic in `frontend_handle_input()`

That function already has a lot of branching. Launch helpers and state translation belong in the owning frontend/world/session files.

### Forgetting normalization rules

Current world-create logic normalizes combinations such as:

- walls require superchunks
- disabled walls disable detached mode
- attached walls force `wall_grid_size = chunk_span`
- detached walls enforce a minimum grid size

If you add a dependent option, add its normalization rules explicitly.

## Verification Checklist

For a menu-only change:

- the option appears in the right view
- selection/navigation reaches it correctly
- left/right or activate modifies the expected state
- backing state survives across frames

For a menu-to-runtime change:

- the option value survives activation
- the owning launch helper receives the value
- the downstream subsystem sees the value
- reloading the relevant screen still shows the correct state

For a world-create option:

- create the world
- inspect the written `meta.txt`
- reopen the world-select list
- start the world and confirm the runtime behavior matches the option

## Minimal File Set For This Task

Load these first:

- `Core/Frontend/sdk_frontend_menu.c`
- `Core/Frontend/sdk_frontend_internal.h`
- `Core/API/Internal/sdk_api_internal.h`
- `Core/API/sdk_api.c`

Then load the owner of the launched action, if any.

---

## Related Guides

- [SDK_AIChangeContext.md](SDK_AIChangeContext.md)
- [SDK_AddWorldCreateAndWorldgenOptions.md](SDK_AddWorldCreateAndWorldgenOptions.md)
- [NewWorldCreation/SDK_NewWorldCreationCallStack.md](NewWorldCreation/SDK_NewWorldCreationCallStack.md)

---
*Practical guide for frontend/menu changes*
