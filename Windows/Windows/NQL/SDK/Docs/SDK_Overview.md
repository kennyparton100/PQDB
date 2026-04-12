<!-- Navigation: SDK Documentation Root -->
**Navigation:** NQL SDK Docs (Root)

---

# NQL SDK Documentation Overview

Maintainer-facing documentation for `Windows/NQL/SDK/`.

This tree is organized for change work. Start from the owning overview page, then open the nearest leaf page for the files you plan to edit. Do not treat this docs tree as a historical narrative or design manifesto; it is a working map of the checked-in engine.

## Best Entry Points

- [Core/SDK_CoreOverview.md](Core/SDK_CoreOverview.md)
- [Guides/SDK_GuidesOverview.md](Guides/SDK_GuidesOverview.md)
- [Build/SDK_BuildGuide.md](Build/SDK_BuildGuide.md)
- [App/SDK_AppOverview.md](App/SDK_AppOverview.md)
- [Renderer/SDK_RendererRuntime.md](Renderer/SDK_RendererRuntime.md)
- [Debugging/SDK_Debugging.md](Debugging/SDK_Debugging.md)
- [Tests/SDK_TestsAndBenchmarks.md](Tests/SDK_TestsAndBenchmarks.md)
- [SDK_DocumentationAudit.md](SDK_DocumentationAudit.md)

## Change-Oriented Reading Paths

### Menu, frontend, or world-create UI changes

- [Guides/SDK_AddMenuOptions.md](Guides/SDK_AddMenuOptions.md)
- [Guides/SDK_AddWorldCreateAndWorldgenOptions.md](Guides/SDK_AddWorldCreateAndWorldgenOptions.md)
- [Core/Frontend/SDK_Frontend.md](Core/Frontend/SDK_Frontend.md)
- [Core/API/Frontend/SDK_APIFrontend.md](Core/API/Frontend/SDK_APIFrontend.md)
- [Core/API/Session/SDK_RuntimeSessionAndFrontend.md](Core/API/Session/SDK_RuntimeSessionAndFrontend.md)

### Session start, spawn choice, or world load changes

- [Guides/SDK_SessionStartFlow.md](Guides/SDK_SessionStartFlow.md)
- [Core/API/Session/SDK_RuntimeSessionAndFrontend.md](Core/API/Session/SDK_RuntimeSessionAndFrontend.md)
- [Core/API/Session/Bootstrap/SDK_SessionBootstrap.md](Core/API/Session/Bootstrap/SDK_SessionBootstrap.md)
- [Core/API/Session/GameModes/GamePlay/Spawn/SDK_SessionGameplaySpawn.md](Core/API/Session/GameModes/GamePlay/Spawn/SDK_SessionGameplaySpawn.md)
- [Core/World/Chunks/SDK_ChunkResidencyAndStreaming.md](Core/World/Chunks/SDK_ChunkResidencyAndStreaming.md)

### Chunk streaming, walls, residency, or superchunk work

- [Core/World/SDK_WorldOverview.md](Core/World/SDK_WorldOverview.md)
- [Core/World/Chunks/SDK_ChunkResidencyAndStreaming.md](Core/World/Chunks/SDK_ChunkResidencyAndStreaming.md)
- [Core/World/Chunks/ChunkManager/SDK_ChunkManager.md](Core/World/Chunks/ChunkManager/SDK_ChunkManager.md)
- [Core/World/Chunks/ChunkStreamer/SDK_ChunkStreamer.md](Core/World/Chunks/ChunkStreamer/SDK_ChunkStreamer.md)
- [Core/World/Superchunks/SDK_SuperChunks.md](Core/World/Superchunks/SDK_SuperChunks.md)

### Worldgen, terrain, construction cells, or save/load changes

- [Core/World/SDK_WorldgenTerrainPipeline.md](Core/World/SDK_WorldgenTerrainPipeline.md)
- [Core/World/Worldgen/SDK_Worldgen.md](Core/World/Worldgen/SDK_Worldgen.md)
- [Core/World/ConstructionCells/SDK_ConstructionSystem.md](Core/World/ConstructionCells/SDK_ConstructionSystem.md)
- [Core/World/Persistence/SDK_PersistenceAndStorage.md](Core/World/Persistence/SDK_PersistenceAndStorage.md)
- [Core/World/Chunks/ChunkCompression/SDK_ChunkCompression.md](Core/World/Chunks/ChunkCompression/SDK_ChunkCompression.md)

### Settlements, buildings, or NPC-world integration changes

- [Core/World/Settlements/SDK_SettlementSystem.md](Core/World/Settlements/SDK_SettlementSystem.md)
- [Core/World/Settlements/Layout/SDK_SettlementLayout.md](Core/World/Settlements/Layout/SDK_SettlementLayout.md)
- [Core/World/Settlements/Building/SDK_SettlementBuilding.md](Core/World/Settlements/Building/SDK_SettlementBuilding.md)
- [Core/World/Settlements/Runtime/SDK_SettlementRuntime.md](Core/World/Settlements/Runtime/SDK_SettlementRuntime.md)
- [Core/World/Buildings/SDK_Buildings.md](Core/World/Buildings/SDK_Buildings.md)

## Runtime Structure

Startup:

```text
WinMain
  -> shared cache init
  -> graphics/settings load
  -> nqlsdk_init
      -> renderer + window
      -> frontend/session reset
      -> frontend menu or session bootstrap
```

Frame loop:

```text
nqlsdk_frame
  -> Win32 message pump
  -> input + frontend/runtime UI
  -> session update
  -> chunk adoption / streaming follow-up
  -> simulation + settlement runtime
  -> renderer frame
```

Shutdown:

```text
nqlsdk_shutdown
  -> active session teardown
  -> settings save
  -> renderer shutdown
  -> window destroy
  -> shared cache shutdown
```

## Coverage Notes

- Core Session and Core World now have leaf docs for previously undocumented subfolders, including bootstrap, headless, gameplay spawn/map slices, settlement subareas, chunk topology, superchunk config/geometry, and construction-cell edge config resolution.
- Older deep-dive pages still exist. Some are useful, some are historical, and some are too detailed for first-pass loading. Prefer the refreshed overview pages first.
- If docs and code disagree, prefer source ownership over prose and then update the docs rather than preserving stale descriptions.

## Known Structural Risks

- The runtime is still global-state heavy. Many subsystems communicate through `g_sdk`, shared registries, or session globals rather than isolated service boundaries.
- Streaming, worldgen, settlement runtime, and renderer upload state still overlap in `SdkChunk`, which makes “single owner” reasoning harder than the folder layout suggests.
- Some older docs remain overlong and mix current behavior with historical explanation. This pass fixes coverage and navigation, not every legacy page’s style.

---

## Related Documentation

### Project Status
- [SDK_DocumentationAudit.md](SDK_DocumentationAudit.md)
- [SDK_Milestones.md](SDK_Milestones.md)
- [SDK_WorldSystemsGapAudit.md](SDK_WorldSystemsGapAudit.md)

### Core Runtime
- [Core/SDK_CoreOverview.md](Core/SDK_CoreOverview.md)
- [Core/API/SDK_APIReference.md](Core/API/SDK_APIReference.md)
- [Core/Runtime/SDK_RuntimeDiagnostics.md](Core/Runtime/SDK_RuntimeDiagnostics.md)

### Tools And Workflow
- [Guides/SDK_GuidesOverview.md](Guides/SDK_GuidesOverview.md)
- [Build/SDK_BuildGuide.md](Build/SDK_BuildGuide.md)
- [Debugging/SDK_Debugging.md](Debugging/SDK_Debugging.md)
- [Tests/SDK_TestsAndBenchmarks.md](Tests/SDK_TestsAndBenchmarks.md)

---
*Root documentation for NQL SDK*
