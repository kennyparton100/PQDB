# NQL SDK Documentation Audit

Date: 2026-04-08

## Scope

This page tracks documentation freshness for the checked-in SDK under `Windows/NQL/SDK/`.

This pass focused on the world/runtime areas that had drifted the furthest from the code:

- superchunk and detached-wall behavior
- construction-cells worldgen
- startup/bootstrap progress reporting
- persistence/save grouping
- debugger and analysis notes that were still describing older behavior

## Current Status

The docs tree is now navigable, but not every leaf page is equally current.

Use these as the maintained starting points:

1. [SDK_Overview.md](SDK_Overview.md)
2. [Core/SDK_CoreOverview.md](Core/SDK_CoreOverview.md)
3. [SDK_WorldSystemsGapAudit.md](SDK_WorldSystemsGapAudit.md)
4. [Core/World/Superchunks/SDK_SuperChunks.md](Core/World/Superchunks/SDK_SuperChunks.md)
5. [Core/World/Persistence/SDK_PersistenceAndStorage.md](Core/World/Persistence/SDK_PersistenceAndStorage.md)
6. [Core/World/Worldgen/ConstructionCells/SDK_WorldgenConstructionCells.md](Core/World/Worldgen/ConstructionCells/SDK_WorldgenConstructionCells.md)
7. [Core/Runtime/SDK_RuntimeDiagnostics.md](Core/Runtime/SDK_RuntimeDiagnostics.md)

## Key Findings From This Pass

### 1. World-create settings were not all reaching the runtime

- Offline world generation was only guaranteed to receive the seed.
- `settlements_enabled`, `construction_cells_enabled`, and superchunk/wall config needed explicit propagation from frontend metadata into the offline generator and exact-map tasks.

### 2. Construction-cells worldgen was previously exposed before it was real

- The previous experimental path was centered on a slope/archetype experiment and could self-defeat.
- The live path now generates deterministic settlement-linked construction cells through the shared chunk construction registry.

### 3. Detached-wall notes had become contradictory

- Runtime wall classification, worldgen ownership, persistence grouping, and debugger/analysis tooling now use wall-grid helpers.
- Older notes that said detached-wall fields were ignored are now historical and should not be treated as current guidance.

### 4. Startup progress text had drifted from runtime behavior

- The old docs described a synchronous wall-support takeover path as if it were still the normal startup route.
- The current startup path makes the session playable after the sync-safe nearby set and leaves wall/remesh backlog to background work.

### 5. Source-tree noise was still affecting maintenance

- `sdk_api_session copy.c` and `sdk_api_session copy 2.c` were still present in the active tree and made search results misleading.
- Those stale copies have now been removed from the active source tree.

## Known Documentation Risk Areas

These areas still deserve skepticism until they receive a deeper rewrite:

- very large legacy leaf pages with long code snippets copied from older implementations
- pages with visible encoding damage
- debugger notes outside `Docs/` that read like design proposals instead of current behavior
- any page that still says detached-wall settings are metadata only

## Recommended Maintenance Rule

For world/runtime behavior, prefer the docs that describe:

- the current helper layer
- the actual save contract
- the current startup stage text
- known limits explicitly called out as partial

Do not rely on older illustrative formulas when they disagree with the code.
