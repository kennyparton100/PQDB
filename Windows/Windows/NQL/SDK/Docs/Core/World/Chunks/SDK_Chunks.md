<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../SDK_Overview.md) > [Core](../../SDK_CoreOverview.md) > [World](../SDK_WorldOverview.md) > Chunks

---

# SDK Chunks

This page documents the core chunk data model used by runtime, persistence, and tooling.

## Scope

The chunk subsystem centers on `SdkChunk` in `Core/World/Chunks/sdk_chunk.*`.

A chunk owns:

- the primary `SdkWorldCellCode` array
- sparse construction overflow refs
- sparse simulation state
- dirty flags for near/far mesh rebuilds
- CPU and GPU mesh bookkeeping

That makes `SdkChunk` the main handoff object between worldgen, streaming, persistence, meshing, and the renderer.

## Dimensions

```c
#define CHUNK_WIDTH  64
#define CHUNK_DEPTH  64
#define CHUNK_HEIGHT 1024
#define CHUNK_SUBCHUNK_HEIGHT 64
#define CHUNK_SUBCHUNK_COUNT 16
#define CHUNK_BLOCKS_PER_LAYER 4096
#define CHUNK_TOTAL_BLOCKS 4194304
```

Storage is Y-major:

```c
index = ly * CHUNK_BLOCKS_PER_LAYER +
        lz * CHUNK_WIDTH +
        lx;
```

That layout is shared across runtime meshing, persistence codecs, and most offline tooling.

## Cell Code Model

Every primary chunk cell stores one `SdkWorldCellCode` (`uint16_t`).

Current kinds:

- full block
  - any code below `SDK_WORLD_CELL_INLINE_BASE`
- inline construction
  - bit `0x8000` set, with material in the low 8 bits and profile in bits `8..14`
- overflow construction
  - sentinel `0xFFFF`, resolved through the chunk-local construction store

Important constants:

- `SDK_WORLD_CELL_INLINE_BASE = 0x8000`
- `SDK_WORLD_CELL_INLINE_PROFILE_SHIFT = 8`
- `SDK_WORLD_CELL_INLINE_PROFILE_MASK = 0x7F00`
- `SDK_WORLD_CELL_INLINE_MATERIAL_MASK = 0x00FF`
- `SDK_WORLD_CELL_OVERFLOW_CODE = 0xFFFF`

Important nuance:

- full-block kind classification is based on the cell-code range
- valid block ids are still bounded by `BlockType` / `BLOCK_COUNT`

So not every code below `0x8000` is necessarily a meaningful block id.

## Mesh And Dirty Tracking

`SdkChunk` is split into 16 subchunks for rebuild and culling:

- `dirty_subchunks_mask`
- `upload_subchunks_mask`
- `active_subchunks_mask`
- `sim_dirty_mask`

Near and far mesh paths are tracked separately:

- per-subchunk near meshes
- per-subchunk water meshes
- `far_mesh`
- `experimental_far_mesh`
- `far_exact_overlay_mesh`

Chunk GPU upload mode is explicit:

- `SDK_CHUNK_GPU_UPLOAD_NONE`
- `SDK_CHUNK_GPU_UPLOAD_FAR_ONLY`
- `SDK_CHUNK_GPU_UPLOAD_FULL`

This matters for streaming because runtime can promote chunk residency before it has paid for every mesh/upload path.

## Persistence Boundary

The primary cell array is now persisted through the shared chunk codec layer rather than a hard-wired `cell_rle` field.

Relevant docs:

- [ChunkCompression/SDK_ChunkCompression.md](ChunkCompression/SDK_ChunkCompression.md)
- [SDK_PersistenceAndStorage.md](../Persistence/SDK_PersistenceAndStorage.md)

The important split is:

- `SdkWorldCellCode[]`
  - compressed through chunk codecs
- construction overflow refs
  - stored separately in per-chunk `construction`
- shared construction archetype registry
  - stored once per save

## Tooling Boundary

Standalone tools such as `ChunkCompression`, `ChunkAnalysis`, and the debugger now reuse shared chunk-save and chunk-codec helpers. They still need small render/frustum stubs to link against `sdk_chunk.c`, because the chunk implementation owns mesh/resource cleanup hooks.

That coupling is real and worth remembering when splitting chunk code further.

## Known Design Flaws

- `SdkChunk` is a large cross-subsystem struct. It mixes world data, construction state, simulation state, and render-state ownership in one object.
- Standalone tooling still depends on chunk/runtime code that was originally written for the full app, which is why CLI builds need stubbed renderer hooks.
- The chunk layer is cleanest at the cell-array and dirty-mask level. The further code gets into mesh/upload ownership, the more tightly coupled it becomes to renderer behavior.

---

## Related Documentation

### Up to Parent
- [SDK World Overview](../SDK_WorldOverview.md) - Back to World systems

### Siblings - Chunk Management
- [ChunkManager/SDK_ChunkManager.md](ChunkManager/SDK_ChunkManager.md) - Residency management
- [SDK_ChunkResidencyAndStreaming.md](SDK_ChunkResidencyAndStreaming.md) - Streaming system
- [ChunkStreamer/SDK_ChunkStreamer.md](ChunkStreamer/SDK_ChunkStreamer.md) - Async streaming

### Related - Data & Persistence
- [ChunkCompression/SDK_ChunkCompression.md](ChunkCompression/SDK_ChunkCompression.md) - Serialization format
- [ChunkAnalysis/SDK_ChunkAnalysis.md](ChunkAnalysis/SDK_ChunkAnalysis.md) - Analysis tools
- [../Persistence/SDK_PersistenceAndStorage.md](../Persistence/SDK_PersistenceAndStorage.md) - Save/load system

### Related - Rendering & World
- [../../MeshBuilder/SDK_MeshBuilder.md](../../MeshBuilder/SDK_MeshBuilder.md) - Mesh generation
- [../ConstructionCells/SDK_ConstructionSystem.md](../ConstructionCells/SDK_ConstructionSystem.md) - Construction system
- [../Simulation/SDK_Simulation.md](../Simulation/SDK_Simulation.md) - Fluid simulation

---
*Documentation for `SDK/Core/World/Chunks/`*
