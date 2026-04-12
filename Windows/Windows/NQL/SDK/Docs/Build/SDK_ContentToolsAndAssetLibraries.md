<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Build > Content Tools

---

# NQL SDK - Content Tools And Asset Libraries

This document describes the local asset-library layout and the frontend/editor code that manages authored content for characters, props, blocks, items, and particle effects.

## Scope

Most file and menu logic lives in `Core/sdk_api_frontend.c`.

Runtime binding helpers live in:

- `Core/sdk_role_assets.c`
- `Core/sdk_building_family.c`

Shared metadata structs for the frontend asset lists live in `Core/sdk_api_internal.h`.

## Content Roots

The current asset roots are:

- `Characters/`
- `Props/`
- `Blocks/`
- `Items/`
- `ParticleEffects/`
- `Profiles/`

These roots are local workspace content libraries. The frontend creates directories on demand and scans them to populate menu lists.

## Character Assets

Character root layout:

```text
Characters/
  <character_id>/
    meta.txt
    model.bin
    animations/
      <animation_id>/
        meta.txt
        frame_000.bin
        frame_001.bin
        ...
```

Current metadata keys:

- character `meta.txt`
  - `name`
- animation `meta.txt`
  - `name`
  - `frame_count`

Binary payloads:

- `model.bin`
  - raw voxel model buffer for the character
- `frame_###.bin`
  - raw voxel frame payload for an animation frame

New assets are auto-generated with ids such as `character_001` and `animation_001`.

## Prop Assets

Prop root layout:

```text
Props/
  <prop_id>/
    meta.txt
    construction_archetypes.txt
    chunks/
      chunk_00_00.bin
      chunk_00_00.cells.bin
      chunk_00_00.construction.txt
      ...
```

Current prop metadata keys:

- `name`
- `family`
- `footprint_x`
- `footprint_z`
- `anchor_x`
- `anchor_y`
- `anchor_z`
- `shell_compatible`

Current prop geometry storage is layered:

- `chunks/chunk_##_##.bin`
  - legacy raw voxel payload, still loadable as fallback
- `chunks/chunk_##_##.cells.bin`
  - current primary chunk storage using `SdkWorldCellCode`
- `chunks/chunk_##_##.construction.txt`
  - chunk-local overflow instance refs for construction cells
- `construction_archetypes.txt`
  - prop-local shared overflow-archetype registry

The prop editor now saves and loads the same high-level construction representation used by live world chunks:

- primary cell storage
- shared archetypes written once per prop
- sparse per-chunk overflow refs

Legacy byte-only prop chunks still load and are widened into full-block cell codes on import.

## Block And Item Assets

Block and item roots are parallel:

```text
Blocks/<asset_id>/
  meta.txt
  model.bin
  icon.bin

Items/<asset_id>/
  meta.txt
  model.bin
  icon.bin
```

Current metadata keys:

- `name`

`model.bin` stores the voxel model. `icon.bin` stores the icon payload used by the editor/runtime UI.

## Particle Effect Assets

Particle effect root layout:

```text
ParticleEffects/
  <asset_id>/
    meta.txt
    timeline.bin
    icon.bin
```

Current metadata keys:

- `name`
- `slice_count`

`timeline.bin` stores the voxel timeline payload used by the particle editor. `icon.bin` stores the corresponding icon.

## Legacy Profile Migration

The frontend still understands a legacy default-character profile under `Profiles/default_character.txt`.

On load it can migrate that legacy selection into the modern `Characters/` library by creating a new character asset and renaming it with the legacy display name.

## Frontend Asset Refresh Model

Each asset class has a refresh pass that:

- enumerates directories beneath the root
- reads metadata files if they exist
- falls back to asset id when the display name is missing
- records `last_write_time`
- sorts the visible menu list

Creation, copy, delete, and editor-launch actions are all handled from `sdk_api_frontend.c`.

## Runtime Bindings

Two small runtime binding files connect authored content to gameplay/runtime systems.

### `sdk_role_assets.c`

Maps NPC roles to desired character asset ids, for example:

- commoner -> `settler_commoner`
- builder -> `builder`
- blacksmith -> `blacksmith`
- miner -> `miner`
- foreman -> `foreman`

The resolver also checks whether the authored asset actually exists and marks it as missing when it does not.

### `sdk_building_family.c`

Maps settlement building types to:

- runtime building families
- default prop ids
- runtime marker placements such as entrance, sleep, work, storage, patrol, water, and station markers

This file is the bridge between settlement-generated building shells and the prop/content system consumed by settlement runtime.

## Session Kinds

The frontend/editor uses `SdkSessionKind` to distinguish:

- normal world session
- character editor
- animation editor
- prop editor
- block editor
- item editor
- particle editor

Those sessions share renderer/runtime infrastructure but differ in file I/O, UI, and payload shapes.

## Construction-Aware Prop Editing

The prop editor uses the runtime chunk/construction stack rather than a separate authored-voxel implementation.

That means prop sessions now share the same major construction concepts as world sessions:

- `SdkWorldCellCode` primary cell storage
- inline construction profiles
- overflow construction refs per chunk
- a prop-local shared construction archetype registry
- placement preview and shaped construction placement

Important practical consequence:

- if you change construction save/load rules, prop-editor compatibility is affected even if world gameplay still appears to work

---

## Related Documentation

### Up to Parent
- [SDK Build Guide](SDK_BuildGuide.md) - Build documentation
- [SDK Overview](../SDK_Overview.md) - Documentation home

### Core Systems
- [../Core/API/Session/SDK_RuntimeSessionAndFrontend.md](../Core/API/Session/SDK_RuntimeSessionAndFrontend.md) - Session/frontend
- [../Core/World/ConstructionCells/SDK_ConstructionSystem.md](../Core/World/ConstructionCells/SDK_ConstructionSystem.md) - Construction cells
- [../Core/World/Settlements/SDK_SettlementSystem.md](../Core/World/Settlements/SDK_SettlementSystem.md) - Settlement system
- [../Core/Server/SDK_OnlineAndServerRuntime.md](../Core/Server/SDK_OnlineAndServerRuntime.md) - Server runtime

### Related Assets
- [../Core/Items/SDK_Items.md](../Core/Items/SDK_Items.md) - Items system
- [../Core/World/Blocks/SDK_Blocks.md](../Core/World/Blocks/SDK_Blocks.md) - Block types
- [../Core/RoleAssets/SDK_RoleAssets.md](../Core/RoleAssets/SDK_RoleAssets.md) - NPC roles
- [../Textures/TexturePackSpec.md](../Textures/TexturePackSpec.md) - Texture packs

---
*Documentation for content tools and asset libraries*
