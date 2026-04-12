<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Core Subsystems

---

# SDK Core Overview

`SDK/Core/` is the runtime heart of the project. Public entry points are small, but the implementation under them owns frontend flow, session startup, chunk streaming, persistence, simulation, and most gameplay/runtime state.

## Subsystem Map

- `API/`
  Public API headers plus internal session glue and thin runtime entry points.
- `Frontend/`, `Settings/`, `Server/`
  Menu flow, saved-world/server UI state, settings, and local-host workflow.
- `World/`
  Chunks, worldgen, settlements, simulation, persistence, superchunks, construction cells.
- `Input/`, `Camera/`, `Math/`, `Map/`, `MeshBuilder/`
  Runtime support systems used by both frontend and world play.
- `Runtime/`, `Profiler/`, `Benchmark/`, `Automation/`
  Diagnostics, instrumentation, and developer-oriented execution helpers.
- `Scene/`
  Legacy triangle/sample scene helper; not a general game-scene abstraction.

## Read By Task

### Public API or app entry changes

- [API/SDK_APIReference.md](API/SDK_APIReference.md)
- [API/Internal/SDK_APIInternal.md](API/Internal/SDK_APIInternal.md)
- [../App/SDK_AppOverview.md](../App/SDK_AppOverview.md)

### Frontend, menu, and session work

- [Frontend/SDK_Frontend.md](Frontend/SDK_Frontend.md)
- [API/Frontend/SDK_APIFrontend.md](API/Frontend/SDK_APIFrontend.md)
- [API/Session/SDK_RuntimeSessionAndFrontend.md](API/Session/SDK_RuntimeSessionAndFrontend.md)
- [API/Session/Bootstrap/SDK_SessionBootstrap.md](API/Session/Bootstrap/SDK_SessionBootstrap.md)
- [API/Session/GameModes/SDK_SessionGameModes.md](API/Session/GameModes/SDK_SessionGameModes.md)

### World runtime changes

- [World/SDK_WorldOverview.md](World/SDK_WorldOverview.md)
- [World/Chunks/SDK_ChunkResidencyAndStreaming.md](World/Chunks/SDK_ChunkResidencyAndStreaming.md)
- [World/ConstructionCells/SDK_ConstructionSystem.md](World/ConstructionCells/SDK_ConstructionSystem.md)
- [World/Settlements/SDK_SettlementSystem.md](World/Settlements/SDK_SettlementSystem.md)
- [World/Superchunks/SDK_SuperChunks.md](World/Superchunks/SDK_SuperChunks.md)
- [World/Persistence/SDK_PersistenceAndStorage.md](World/Persistence/SDK_PersistenceAndStorage.md)

### Diagnostics, profiling, and tooling

- [Runtime/SDK_RuntimeDiagnostics.md](Runtime/SDK_RuntimeDiagnostics.md)
- [Profiler/SDK_Profiler.md](Profiler/SDK_Profiler.md)
- [Automation/SDK_Automation.md](Automation/SDK_Automation.md)
- [Benchmark/SDK_Benchmarking.md](Benchmark/SDK_Benchmarking.md)

## Core Reality

At a high level:

```text
public API
  -> frontend/session layer
      -> world bootstrap and shutdown
      -> chunk manager + streamer
      -> persistence + construction registry
      -> simulation + settlement runtime
      -> renderer-facing upload state
```

The folder structure is cleaner than the underlying ownership graph. A lot of runtime logic still crosses module boundaries through shared globals, shared chunk state, and direct helper calls.

## Documentation Strategy

- Start with the closest overview page in the subtree you intend to edit.
- Then read the leaf page for the specific folder or feature slice you will touch.
- Use the guides under [../Guides/SDK_GuidesOverview.md](../Guides/SDK_GuidesOverview.md) when the task is workflow-oriented rather than subsystem-oriented.

Recent documentation coverage additions include:

- missing Session subfolder pages
- missing World subfolder pages for settlements, topology, superchunk config/geometry, and construction-cell edge config resolution
- empty API slice folders that previously had no maintainer docs at all

## Known Structural Problems

- Global-state coupling remains high.
- `SdkChunk` still mixes world data, construction state, simulation state, and render/upload bookkeeping.
- Some supporting systems under `Core/` are still legacy or test-oriented but compiled into the main runtime.
- A few older deep-dive docs remain much larger than they need to be and are not the best first context load for an AI model.

---

## Related Documentation

### Up to Root
- [SDK Overview](../SDK_Overview.md)
- [../Guides/SDK_GuidesOverview.md](../Guides/SDK_GuidesOverview.md)

### Entry Points
- [API/SDK_APIReference.md](API/SDK_APIReference.md)
- [API/Session/SDK_RuntimeSessionAndFrontend.md](API/Session/SDK_RuntimeSessionAndFrontend.md)
- [Settings/SDK_SettingsAndControls.md](Settings/SDK_SettingsAndControls.md)

### World And Runtime
- [World/SDK_WorldOverview.md](World/SDK_WorldOverview.md)
- [Server/SDK_OnlineAndServerRuntime.md](Server/SDK_OnlineAndServerRuntime.md)
- [Runtime/SDK_RuntimeDiagnostics.md](Runtime/SDK_RuntimeDiagnostics.md)

### Support Systems
- [Input/SDK_Input.md](Input/SDK_Input.md)
- [Camera/SDK_Camera.md](Camera/SDK_Camera.md)
- [Math/SDK_Math.md](Math/SDK_Math.md)
- [MeshBuilder/SDK_MeshBuilder.md](MeshBuilder/SDK_MeshBuilder.md)
- [Map/SDK_MapSchedulerAndTileCache.md](Map/SDK_MapSchedulerAndTileCache.md)

---
*Documentation for `SDK/Core/`*
