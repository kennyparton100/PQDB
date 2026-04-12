# NQL SDK - Construction System

This document describes the construction runtime as it exists today. It covers world-cell encoding, inline versus overflow construction, shared overflow archetypes, authoring controls, placement preview, persistence, and the chunk/runtime ownership boundaries that matter when changing this system.

## Scope And File Ownership

The construction system spans several layers:

- `Core/API/sdk_types.h`
  - shared world-cell, inline profile, shaped-item, and archetype id types
- `Core/World/Chunks/sdk_chunk.h`
  - `SdkWorldCellCode` helpers and chunk storage accessors
- `Core/World/ConstructionCells/sdk_construction_cells.h` / `Core/World/ConstructionCells/sdk_construction_cells.c`
  - construction authority: canonicalization, overflow storage, registry ownership helpers, placement, cutting, collision, and persistence encode/decode
- `Core/API/Interactions/sdk_api_interaction.c`
  - gameplay placement, break/use dispatch, and raycast routing
- `Core/API/Player/sdk_api_player.c`
  - hotbar payload handling and pickup insertion
- `Core/World/Persistence/sdk_persistence.c`
  - world save/load of archetype registries, chunk construction refs, and shaped hotbar payloads
- `Core/Frontend/sdk_frontend_assets.c`
  - prop-asset construction files
- `Core/API/Session/Core/sdk_api_session_core.c`
  - world-session registry binding and prop-editor load/save wiring
- `Core/MeshBuilder/sdk_mesh_builder.c`
  - full, inline, and overflow construction meshing
- `Renderer/d3d12_renderer_*.cpp`
  - placement preview rendering

If you change world-cell semantics, chunk persistence, or mixed-material overflow behavior, you almost always need to touch more than one of the files above.

## Primary World-Cell Model

Every chunk cell stores one `SdkWorldCellCode` (`uint16_t`).

There are exactly three storage categories:

- `SDK_WORLD_CELL_KIND_FULL_BLOCK`
  - direct full-block `BlockType`
- `SDK_WORLD_CELL_KIND_INLINE_CONSTRUCTION`
  - direct encoded `BlockType + SdkInlineConstructionProfile`
- `SDK_WORLD_CELL_KIND_OVERFLOW_CONSTRUCTION`
  - reserved overflow sentinel; real contents live in the chunk-local construction store and shared archetype registry

Important constants from `sdk_types.h`:

- `SDK_WORLD_CELL_INLINE_BASE = 0x8000`
- `SDK_WORLD_CELL_OVERFLOW_CODE = 0xFFFF`
- `SDK_CONSTRUCTION_CELL_RESOLUTION = 16`

The primary cell code never stores a dynamic shape id. Full blocks and inline construction are encoded directly. Overflow uses one sentinel code and resolves through chunk-local state.

## Inline Construction

Inline construction is global, compact, and single-material only.

The current inline profile table is fixed and shared across every world and prop session:

- half cuts on each axis/sign
- quarter face cuts on each axis/sign
- centered beams on X/Y/Z
- centered strips on X/Y/Z

Inline data is:

- material in the low 8 bits
- profile in bits `8..14`
- inline marker at `0x8000`

Inline cells do not have sidecar data. Size, orientation, and in-cell placement are implied by the chosen global profile.

## Overflow Construction

Overflow is used when the final local `16x16x16` cell contents cannot be represented as:

- air,
- a full single-material block,
- or an exact single-material inline profile.

Overflow is split into two layers:

### Chunk-Local Overflow Instance Store

Each chunk has a sparse construction store keyed by local cell coordinate.

Each overflow instance stores only:

- `local_index`
- `archetype_id`

This means the same detailed cell can appear many times without copying its full voxel payload into every chunk entry.

### Shared Archetype Registry

The actual detailed `16x16x16` cell contents live in a shared archetype registry.

Each archetype stores:

- `archetype_id`
- refcount
- occupied voxel count
- cached bounds
- cached face masks
- display material
- material palette
- per-voxel palette indices

The registry is shared at session scope:

- one registry for a live world session
- one registry for a prop-editor asset/session

There is no installation-wide or global cross-save registry.

## Mixed-Material Representation

Mixed materials are supported only in overflow cells.

Overflow archetypes do not store a full `BlockType` per voxel. Instead they use:

- a small per-archetype material palette
- one palette index per voxel
- palette index `0` for empty

That keeps overflow cheaper than a raw `4096 * BlockType` layout while still allowing cells such as:

- lower half stone, upper half wood
- timber posts embedded into masonry
- mixed trim/detail work inside a single `1x1x1` world cell

Inline remains single-material only by design.

## Canonicalization Rules

Every construction edit resolves to a transient `16x16x16` workspace and is then canonicalized in this order:

1. empty -> `BLOCK_AIR` full block
2. single-material full occupancy -> full block
3. single-material exact inline profile -> inline construction
4. everything else -> overflow archetype

That means:

- mixed-material cells always end up in overflow
- repeated identical overflow cells reuse one archetype
- edits that reduce a cell back to a simple single-material form collapse back to full or inline storage automatically

## Deduplication Rules

Overflow deduplication is exact-content deduplication.

Two overflow cells share one archetype only when their final local cell contents match exactly:

- same voxel occupancy
- same material palette values
- same palette assignment per occupied voxel

This is intentionally not transform-invariant. Different rotations or different in-cell offsets produce different archetypes unless the final local voxel-material data is identical.

The hash table is only a lookup accelerator. Identity is confirmed by exact content comparison, not by hash alone.

## Placement, Preview, And Editing

### Shaped Item Payloads

Inventory and hotbar shaped items are still single-material payloads. The current exact payload item path does not preserve mixed-material world cells as exact inventory items.

Current shaped-item payload fields include:

- source material
- inline profile hint
- occupancy bitset
- derived item identity metadata

For box payloads, item identity ignores axis order. `2x4x8`, `4x2x8`, and `8x4x2` are treated as the same item identity if they are the same material and solid rectangular boxes.

### Placement Preview

When holding a placeable block or shaped construction payload, the runtime now drives a renderer-facing placement preview that includes:

- outer white target cube
- `16x16` grid on the targeted face
- semi-transparent ghost of the actual placed geometry

The preview is used in both gameplay and prop-editor sessions.

### Ray-Snapped In-Cube Placement

Construction placement is face-local and ray-snapped:

- the target face becomes the placement surface
- the hit point is converted to `u/v` coordinates in the face grid
- the shaped payload is rotated for the hit face and current manual rotate state
- the payload is shifted inside the target `16x16x16` cell and clamped to fit

If the transformed payload cannot fit, preview is marked invalid and placement is blocked.

### Manual Rotate

`SDK_INPUT_ACTION_CONSTRUCTION_ROTATE` is the dedicated rotate action.

Current default binding:

- `R`

Rotation is placement-state only. It is not stored in the shaped item payload. The runtime applies rotation when resolving the final local voxel contents for preview and placement.

### Creative Construction Authoring

The creative inventory now exposes a construction shape subpanel for blocks:

- `WIDTH [-] value [+]`
- `HEIGHT [-] value [+]`
- `DEPTH [-] value [+]`

Valid values are `1..16`. `16/16/16` grants a normal full block. Anything smaller grants a shaped single-material construction payload.

The transient UI state lives in `sdk_api.c` and pause-menu code. It is not persisted to `settings.json` or `controls.cfg`.

## Placement And Merge Rules

Placement into a cell behaves like this:

- placing into a full non-air block is rejected
- placing into empty/incomplete construction loads the current cell into a transient workspace
- the transformed payload is stamped into that workspace
- overlap on already occupied voxels is allowed only when the incoming voxel material matches the existing voxel material
- different-material overwrite on an occupied voxel is rejected

Because the workspace is material-aware, multiple materials can coexist in one cell as long as they occupy different voxels.

## Cutting, Removal, And Drops

Cutting and removal operate on the same transient workspace model.

The world result is exact:

- the edited world cell keeps its precise remaining voxel/material state
- canonicalization then decides full, inline, or overflow

The item result is intentionally simpler:

- single-material fragments can still become shaped items
- mixed-material fragments do not become exact mixed-material items yet
- mixed-material edits fall back to simplified salvage behavior

This is a deliberate current limitation. World cells can be mixed-material. Inventory payloads are still exact only for single-material construction items.

## Collision, Raycast, And Display Material

Full and inline cells use their direct representations.

Overflow uses archetype data for:

- broad-phase bounds
- exact occupied-voxel collision
- exact occupied-voxel raycast

The runtime also exposes a construction display material for systems that need one representative block:

- near meshing and interaction can use exact per-voxel materials
- far/proxy paths and some UI helpers use display material as a fallback

Current display-material selection:

1. dominant exposed top-face material
2. otherwise dominant exposed material overall
3. otherwise first palette material

This is a rendering and UI simplification, not a replacement for the exact archetype contents.

## Meshing

There are now three effective mesh paths:

- full blocks
- inline construction
- overflow construction

Overflow meshing emits faces from the archetype voxel-material data, not from a single material plus occupancy bitset.

Near-mesh correctness is the primary target. Far/exact overlay and proxy paths sample construction through display-material-aware helpers rather than fully reproducing mixed interiors.

## Persistence

### World Save

`save.json` now stores:

- primary `SdkWorldCellCode` chunk data through the nested `cells` object
  - `cells.codec`
  - `cells.payload_b64`
  - `cells.payload_version`
- a top-level `construction_archetypes` payload written once per save
- per-chunk `construction` payloads that store overflow instance refs

Current save version:

- `version = 7`
- `worldgen_revision = 12`

Legacy saves still load:

- version `2`
- version `4`
- version `5`

Legacy per-instance overflow payloads are loaded and converted through the current canonicalization path.

### Prop Assets

Prop assets use the same high-level representation:

- per-chunk primary cell data in `.cells.bin`
- per-chunk overflow refs in `.construction.txt`
- one prop-local `construction_archetypes.txt`

Legacy raw voxel prop chunks (`chunk_##_##.bin`) still load as fallback and are widened into full-block cell codes on import.

## Streaming And Ownership

Chunk streaming uses the shared construction registry owned by the active chunk manager/session.

Important rules:

- worker jobs build chunks that point at the same session registry
- chunk snapshots clone chunk-local overflow instance refs
- worker jobs do not create detached private archetype pools
- main-thread adoption must keep archetype refcounts correct when chunk construction state is replaced

This is one of the most bug-prone areas in the system. If you change chunk clone/adopt/clear behavior, verify archetype acquire/release accounting immediately.

## Current Limitations

These are real current limits, not future plans:

- mixed-material exact inventory items are not implemented
- inline construction remains single-material only
- dedup is exact-content only, not rotation-invariant
- far rendering still relies on display-material simplification for overflow construction
- the construction system is stable enough to author props, but it still deserves careful save/load and streamer-lifecycle testing under heavy editing

## Maintenance Checklist

If you change any of the following, update this doc and the linked persistence/content docs in the same patch:

- `SdkWorldCellCode` bit layout
- inline profile table
- overflow archetype layout
- prop construction file names
- `save.json` construction keys
- placement-preview semantics
- construction rotate binding

Related docs:

- [SDK_Overview.md](../../../SDK_Overview.md)
- [SDK_PersistenceAndStorage.md](../Persistence/SDK_PersistenceAndStorage.md)
- [SDK_ContentToolsAndAssetLibraries.md](../../../Build/SDK_ContentToolsAndAssetLibraries.md)
- [SDK_RuntimeSessionAndFrontend.md](../../API/Session/SDK_RuntimeSessionAndFrontend.md)
- [SDK_ChunkResidencyAndStreaming.md](../Chunks/SDK_ChunkResidencyAndStreaming.md)
- [SDK_Chunks.md](../Chunks/SDK_Chunks.md) - Chunk cell code system
- [SDK_Blocks.md](../Blocks/SDK_Blocks.md) - Block type definitions
