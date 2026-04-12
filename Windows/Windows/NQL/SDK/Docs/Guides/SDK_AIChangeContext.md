<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > [Guides](SDK_GuidesOverview.md) > AI Change Context

---

# AI Change Context

This page is for loading an AI model with the right context for one change, instead of dumping half the engine into the prompt.

The rule is simple:

- load the smallest current file set that owns the behavior
- understand the handoff boundaries
- avoid older docs unless you need broader background

## General Rules

### Load code before large docs

For active runtime work, prefer:

1. the relevant guide in this folder
2. the current source files
3. the smaller refreshed overview docs
4. older large subsystem pages only if still needed

### Assume old flat paths may be stale

Many older notes still mention files like:

- `Core/sdk_api_session.c`
- `Core/sdk_server_runtime.c`
- `Core/sdk_construction_cells.c`

The maintained implementation is now more split than that. Always confirm the live file path before editing.

### Treat these subsystems as high-risk

If the change touches any of these, load more context before editing:

- construction registry ownership
- chunk bootstrap / streamer scheduling
- wall-support residency
- persistence load/save contracts
- D3D12 upload / frame synchronization

## Minimal Context Packs

## 1. Add Or Change A Frontend Menu Option

Load first:

- `Docs/Guides/SDK_AddMenuOptions.md`
- `Core/Frontend/sdk_frontend_menu.c`
- `Core/Frontend/sdk_frontend_internal.h`
- `Core/API/Internal/sdk_api_internal.h`
- `Core/API/sdk_api.c`

Load next only if needed:

- `Core/Frontend/sdk_frontend_worlds.c`
- `Core/Frontend/sdk_frontend_async.c`
- `Core/Frontend/sdk_frontend_worldgen.c`

Questions to answer before editing:

- is this option only UI state, or does it launch runtime work?
- is the option per-view temporary state, or does it need persistent globals?
- does the renderer need a new datum, or can the existing UI snapshot carry it?

## 2. Add A World-Create Option That Must Reach Runtime

Load first:

- `Docs/Guides/SDK_AddWorldCreateAndWorldgenOptions.md`
- `Core/Frontend/sdk_frontend_menu.c`
- `Core/Frontend/sdk_frontend_worlds.c`
- `Core/World/Persistence/sdk_world_tooling.h`
- `Core/World/Persistence/sdk_world_tooling.c`
- `Core/API/Session/Core/sdk_api_session_core.c`

Load next if the option affects offline generation or map tasks:

- `Core/Frontend/sdk_frontend_async.c`
- `Core/Frontend/sdk_frontend_worldgen.c`

Load next if it affects worldgen behavior:

- the relevant `Core/World/Worldgen/...` module
- `Core/sdk_types.h` if the option belongs in `SdkWorldDesc`
- `Core/World/Superchunks/Config/sdk_superchunk_config.h`
- `Core/World/Superchunks/Config/sdk_superchunk_config.c` if it belongs in `SdkSuperchunkConfig`

Critical boundary:

- UI state becomes durable world metadata through `SdkWorldCreateRequest` and `sdk_world_create(...)`
- runtime then consumes `SdkWorldSaveMeta`, not the UI globals

## 3. Change World Session Startup

Load first:

- `Docs/Guides/SDK_SessionStartFlow.md`
- `Core/API/Session/Core/sdk_api_session_core.c`
- `Core/API/Session/Bootstrap/sdk_api_session_bootstrap.c`
- `Core/API/Session/Bootstrap/sdk_api_session_bootstrap_policy.h`
- `Core/World/Chunks/ChunkManager/sdk_chunk_manager.h`
- `Core/World/Chunks/ChunkStreamer/sdk_chunk_streamer.c`

Load next if the task touches persistence or spawn:

- `Core/World/Persistence/sdk_persistence.c`
- `Core/API/Session/Headless/sdk_session_headless.c`

Known sensitive areas:

- construction registry binding during startup
- sync-safe chunk counting
- wall-support scheduling
- background expansion after the world becomes playable

## 4. Change Construction-Cell Worldgen Or Surface Semantics

Load first:

- `Docs/Core/World/Worldgen/ConstructionCells/SDK_WorldgenConstructionCells.md`
- `Core/World/ConstructionCells/sdk_construction_cells.c`
- `Core/World/Chunks/Topology/sdk_chunk_topology.c`
- `Core/World/Worldgen/ConstructionCells/sdk_worldgen_construction_cells.c`
- `Core/World/Worldgen/ConstructionCells/sdk_worldgen_terrain_edge_cells.c`

Load next if persistence or runtime placement is involved:

- `Core/World/Persistence/sdk_persistence.c`
- `Core/API/sdk_api_interaction.c`
- `Core/MeshBuilder/sdk_mesh_builder.c`

Current warning:

- construction-cell support is functional, but the construction registry is still a crash-sensitive subsystem

## 5. Change Save/Meta Handling

Load first:

- `Core/World/Persistence/sdk_world_tooling.h`
- `Core/World/Persistence/sdk_world_tooling.c`
- `Core/Frontend/sdk_frontend_worlds.c`

Load next if runtime consumption changes:

- `Core/API/Session/Core/sdk_api_session_core.c`
- `Core/Frontend/sdk_frontend_async.c`
- `Core/Frontend/sdk_frontend_worldgen.c`

## File Ownership Cheatsheet

For current runtime work, think in these ownership layers:

- `Core/Frontend/`
  - menu state, world selection, async launch handoff
- `Core/World/Persistence/sdk_world_tooling.*`
  - world create request, meta normalization, world target resolution
- `Core/API/Session/Core/`
  - world session bring-up and steady-state core helpers
- `Core/API/Session/Bootstrap/`
  - startup sync bootstrap and nearby-chunk bring-up policy
- `Core/World/Chunks/`
  - desired set, residency roles, streamer jobs, chunk adoption contracts
- `Core/World/Worldgen/`
  - actual generated terrain/content behavior

## AI Prompting Pattern That Works Well Here

When handing work to an AI model, include:

1. the guide name
2. the exact behavior to change
3. the specific files already loaded
4. the verification expectation

Good example:

```text
Use Docs/Guides/SDK_AddWorldCreateAndWorldgenOptions.md.
Task: add a new world-create toggle that persists in meta.txt and affects runtime worldgen.
Loaded files:
- Core/Frontend/sdk_frontend_menu.c
- Core/Frontend/sdk_frontend_worlds.c
- Core/World/Persistence/sdk_world_tooling.h
- Core/World/Persistence/sdk_world_tooling.c
- Core/API/Session/Core/sdk_api_session_core.c
Make the change end-to-end and list any additional files that must be touched.
```

That pattern works much better here than asking the model to reason from the whole repo at once.

---

## Related Guides

- [SDK_GuidesOverview.md](SDK_GuidesOverview.md)
- [SDK_AddMenuOptions.md](SDK_AddMenuOptions.md)
- [SDK_AddWorldCreateAndWorldgenOptions.md](SDK_AddWorldCreateAndWorldgenOptions.md)
- [SDK_SessionStartFlow.md](SDK_SessionStartFlow.md)

---
*Task-oriented context loading guide for AI-assisted changes*
