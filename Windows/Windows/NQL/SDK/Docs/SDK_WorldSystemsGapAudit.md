# NQL SDK World Systems Gap Audit

Date: 2026-04-08

This page tracks the create-world/runtime features that matter for actual gameplay worlds. It is intentionally narrower than a full backlog. Unrelated stubs such as remote networking stay out of the main matrix.

Baseline logs used in this pass:

- `Build/WorldSaves/world_022/debug.log`
- `Build/WorldSaves/world_023/debug.log`

Those logs were treated as historical regression samples for the old wall-bootstrap hang. Current runtime code should no longer rely on the synchronous perimeter-wall takeover path they captured.

## Create-World Settings Matrix

| Setting | Status | Notes |
|---|---|---|
| Seed | implemented | Used by runtime worldgen, offline world generation, and exact map generation. |
| Spawn Type | implemented | Runtime session startup honors random/center/safe spawn selection. |
| Render Distance | implemented | Stored in world metadata and used by session launch. |
| Settlements Enabled | implemented | Propagated into world/session startup and offline generation tasks. |
| Construction Cells Enabled | implemented | Now drives deterministic settlement-linked worldgen construction placement through the shared construction registry. |
| Superchunks Enabled | implemented | Session startup normalizes wall settings against this and applies it to runtime config. |
| Superchunk Chunk Span | implemented | Runtime config and offline generation paths now use the selected span. |
| Walls Enabled | implemented | Requires superchunks and is normalized consistently in frontend/session config. |
| Walls Detached | partial | Wall-grid sizing/offsets now affect wall classification/worldgen/persistence/debugger paths, but non-default detached layouts still need broader live validation in residency/proxy behavior. |
| Wall Grid Size | partial | Live wall-grid helpers use it; attached mode normalizes back to `chunk_span`. |
| Wall Grid Offset X/Z | partial | Live wall-grid helpers use offsets for classification/grouping, but non-default offsets still need broader runtime validation. |

## Active World-System Issues Still Worth Watching

### Detached walls

- The shared wall-grid helper layer is now the source of truth for wall classification and persistence grouping.
- Detached settings are no longer just saved metadata.
- Remaining risk: residency staging, active-wall proxying, and wide live-play validation for unusual detached grids or offsets.

### Construction-cells worldgen

- The broken slope/archetype experiment was replaced with settlement-marker driven construction placement.
- The system now uses the normal chunk construction registry and save/load path.
- Remaining risk: content breadth. The current pass places deterministic marker cells, not a full authored settlement-construction language.

### Honest progress reporting

- Offline world generation now reports real completion counts instead of queue-pressure ratios.
- World-session bootstrap now becomes playable after the sync-safe nearby set and reports remaining wall/remesh work as background backlog.
- Remaining risk: exact map generation still uses its own scheduler-facing progress model and may need a later pass if users want the same level of accounting there.

## Backlog Appendix

These are real, but not part of the current world-systems completion bar:

- remote server networking
- legacy scene module cleanup
- shader build/runtime packaging improvements
- broader binary-container save redesign beyond `save.json`

## Partial / Stub Inventory Outside The Main World Bar

These are the most visible unfinished areas discovered during this audit wave:

- `Debugging/debugger_stubs.c`
  - intentional no-op linker stubs for the headless debugger
- `Core/MeshBuilder/sdk_mesh_builder.*`
  - `experimental_far_*` proxy path is still explicitly experimental
- `Core/World/Chunks/ChunkAnalysis/chunk_analysis.c`
  - some helper/export logic is still placeholder quality rather than authoritative simulation
- `Core/API/Session/sdk_api_session_core.c`
  - contains a `delete_animation_asset not implemented` TODO

These were left as backlog because they are not create-world settings that currently misrepresent runtime functionality.
