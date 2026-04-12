<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > [Guides](SDK_GuidesOverview.md) > Add World-Create And Worldgen Options

---

# Add World-Create And Worldgen Options

This guide explains how to add a world-create option that must become real engine behavior.

Examples:

- new create-world toggles
- new spawn-mode-related options
- new worldgen feature flags
- new wall/superchunk configuration fields

## The Important Boundary

In the current engine, the create-world UI is not the source of truth.

The option becomes real only after it passes through this chain:

```text
frontend globals
  -> SdkWorldCreateRequest
  -> sdk_world_create(...)
  -> SdkWorldSaveMeta
  -> meta.txt
  -> SdkWorldSaveMeta reload
  -> SdkWorldDesc and/or SdkSuperchunkConfig
  -> runtime session / offline tasks / worldgen consumers
```

If you stop anywhere before the end of that chain, the feature is only partially implemented.

## Current Ownership

Primary files:

- `Core/Frontend/sdk_frontend_menu.c`
  - editable UI state and input
- `Core/Frontend/sdk_frontend_worlds.c`
  - converts frontend state into `SdkWorldCreateRequest`
- `Core/World/Persistence/sdk_world_tooling.h`
  - `SdkWorldSaveMeta`, `SdkWorldCreateRequest`, `SdkWorldCreateResult`
- `Core/World/Persistence/sdk_world_tooling.c`
  - metadata normalization, `meta.txt` serialization, `sdk_world_create(...)`
- `Core/API/Session/Core/sdk_api_session_core.c`
  - consumes `SdkWorldSaveMeta` for runtime session start

Secondary files:

- `Core/Frontend/sdk_frontend_async.c`
  - exact-map and world-session startup handoff
- `Core/Frontend/sdk_frontend_worldgen.c`
  - offline generation handoff
- `Core/sdk_types.h`
  - if the option belongs in `SdkWorldDesc`
- `Core/World/Superchunks/Config/sdk_superchunk_config.*`
  - if the option belongs in superchunk/wall config
- relevant `Core/World/Worldgen/...` modules
  - if the option changes generated output

## Decide Which Data Model Owns The New Option

Before editing, classify the option.

### Case 1: Pure save metadata

Use this only if the option changes selection or UX but does not alter world runtime behavior.

Owner:

- `SdkWorldSaveMeta`

### Case 2: Worldgen/runtime world descriptor

Use this if the option affects generated terrain/content behavior directly.

Owner:

- `SdkWorldDesc`

Current examples:

- `seed`
- `settlements_enabled`
- `construction_cells_enabled`

### Case 3: Superchunk or wall topology config

Use this if the option changes residency/topology/wall layout rather than per-column generation.

Owner:

- `SdkSuperchunkConfig`

Current examples:

- `superchunks_enabled`
- `superchunk_chunk_span`
- `walls_enabled`
- `walls_detached`
- `wall_grid_size`
- `wall_grid_offset_x`
- `wall_grid_offset_z`

## Current Create-World Flow

The practical flow today is:

```text
sdk_frontend_menu.c
  -> g_world_create_* globals
  -> create_new_world_save_with_settings(...)

sdk_frontend_worlds.c
  -> apply_current_world_create_settings(...)
  -> SdkWorldCreateRequest
  -> sdk_world_create(...)

sdk_world_tooling.c
  -> sdk_world_apply_create_request_to_meta(...)
  -> sdk_world_meta_normalize(...)
  -> sdk_world_save_meta_file(...)

later:
  -> meta reload into SdkWorldSaveMeta
  -> runtime and offline tasks consume that meta
```

## Implementation Recipe

## 1. Add the editable frontend state

Add the UI-facing state in the frontend layer if it needs to be selectable in the create-world menu.

Typical places:

- declaration: `Core/API/Internal/sdk_api_internal.h`
- storage: `Core/API/sdk_api.c`
- defaults and input handling: `Core/Frontend/sdk_frontend_menu.c`

## 2. Put the field on the correct create/save structure

Usually this means:

- `SdkWorldCreateRequest`
- `SdkWorldSaveMeta`

Both live in `sdk_world_tooling.h`.

Do not skip the request type. The request is the handoff from UI state to durable world creation.

## 3. Copy the field into the request in `sdk_frontend_worlds.c`

The frontend currently does this through:

- `apply_current_world_create_settings(...)`

If the new option is not copied there, it will never leave the menu.

## 4. Normalize and persist it in `sdk_world_tooling.c`

Review and update:

- `sdk_world_apply_create_request_to_meta(...)`
- `sdk_world_meta_normalize(...)`
- `sdk_world_save_meta_file(...)`
- `sdk_world_load_meta_file_internal(...)`
- default values in `sdk_world_meta_set_defaults(...)`

This is where many half-implemented options fail.

## 5. Route it into runtime consumers

Choose the correct downstream path:

- if it belongs in `SdkWorldDesc`:
  - update `sdk_world_meta_to_world_desc(...)`
  - update runtime/offline callers that construct or patch `SdkWorldDesc`
- if it belongs in `SdkSuperchunkConfig`:
  - update `sdk_world_meta_to_superchunk_config(...)`
  - update runtime/offline callers that build config from save meta

## 6. Use it in the actual consumer

Examples:

- session startup in `sdk_api_session_core.c`
- offline generation in `sdk_frontend_worldgen.c`
- exact-map or frontend task setup in `sdk_frontend_async.c`
- the worldgen implementation itself in `Core/World/Worldgen/...`

The option is not real until the consumer reads it and changes behavior.

## Current Runtime Consumption Example

At session start, `start_world_session(...)` currently does two important conversions:

- `SdkWorldSaveMeta -> SdkWorldDesc`
  - seed
  - settlements enabled
  - construction cells enabled
- `SdkWorldSaveMeta -> SdkSuperchunkConfig`
  - superchunks
  - chunk span
  - walls
  - detached walls
  - grid size and offsets

That is the model to follow.

## Common Mistakes

### Writing only the UI

This changes the menu and nothing else.

### Writing `meta.txt` but not loading it back

This creates a fake persistence path. The file looks right, but the runtime still uses defaults.

### Putting the option on the wrong structure

Do not put topology options in `SdkWorldDesc`.
Do not put terrain/content generation flags only in `SdkSuperchunkConfig`.

### Forgetting offline tasks

If the option affects exact-map generation or offline world generation, update:

- `sdk_frontend_async.c`
- `sdk_frontend_worldgen.c`

not just runtime session start.

## Verification Checklist

For every new option, verify all of these:

1. The menu can edit it.
2. `create_new_world_save_with_settings(...)` sends it into `SdkWorldCreateRequest`.
3. `sdk_world_create(...)` persists it into `meta.txt`.
4. Reloading the save list preserves the value.
5. Session startup sees the correct value.
6. Offline tasks see the correct value if they should.
7. The real consumer changes behavior.

If the option affects world generation:

8. Create a fresh world with the option on.
9. Create a fresh world with the option off.
10. Confirm generated output diverges in the expected way.

## Minimal File Set For This Task

Load these first:

- `Core/Frontend/sdk_frontend_menu.c`
- `Core/Frontend/sdk_frontend_worlds.c`
- `Core/World/Persistence/sdk_world_tooling.h`
- `Core/World/Persistence/sdk_world_tooling.c`
- `Core/API/Session/Core/sdk_api_session_core.c`

Then load the actual consumer.

---

## Related Guides

- [SDK_AIChangeContext.md](SDK_AIChangeContext.md)
- [SDK_AddMenuOptions.md](SDK_AddMenuOptions.md)
- [SDK_SessionStartFlow.md](SDK_SessionStartFlow.md)
- [NewWorldCreation/SDK_NewWorldCreationCallStack.md](NewWorldCreation/SDK_NewWorldCreationCallStack.md)

---
*Practical guide for end-to-end world option changes*
