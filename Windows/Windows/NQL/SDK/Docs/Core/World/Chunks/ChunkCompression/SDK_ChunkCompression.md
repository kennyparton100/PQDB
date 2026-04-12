<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [World](../../SDK_WorldOverview.md) > [Chunks](../SDK_Chunks.md) > Compression

---

# SDK Chunk Compression

This page documents the shared chunk codec layer under `Core/World/Chunks/ChunkCompression/` and the offline `chunk_compress` CLI that sits on top of it.

## Scope

The chunk compression area now has two roles:

- shared runtime codec library
  - used by `sdk_persistence.c`, debugger tooling, and offline utilities
- offline analysis CLI
  - `chunk_compress.exe`, which inspects persisted chunk payloads and compares codecs

The important boundary is that runtime save integration is chunk-local only in this pass. Superchunk/group codecs remain tooling-only.

## Shared Runtime API

The shared API lives in `sdk_chunk_codec.h`:

- `sdk_chunk_codec_encode_auto`
- `sdk_chunk_codec_encode_with_method`
- `sdk_chunk_codec_decode`
- `sdk_chunk_codec_method_name`
- `sdk_chunk_codec_method_from_name`

`sdk_chunk_save_json.*` is the companion layer that reads and writes the nested `cells` object used by `save.json`.

## Runtime-Enabled Codecs

Current runtime codec names:

- `cell_rle`
- `volume_layer`
- `template`
- `octree`
- `bitpack`
- `sparse_column`
- `rle_bitmask`
- `hierarchical_rle_v2`

All of these now round-trip full `SdkWorldCellCode` values instead of truncating to 8-bit block ids.

Not part of runtime save auto-routing:

- `inter_chunk_delta`
  - implemented, but not selected by the current runtime writer
- `delta_template`
  - placeholder constant, not productionized
- `superchunk_hier`
  - tooling-only, not stored in live world saves

## Save Integration

Runtime chunk saves now use:

```json
"cells": {
  "codec": "hierarchical_rle_v2",
  "payload_b64": "...",
  "payload_version": 1
}
```

`sdk_persistence_store_chunk` calls `sdk_chunk_codec_encode_auto`, and `sdk_persistence_load_chunk` decodes through the same shared dispatch path.

That means runtime, debugger, and offline tools now agree on:

- codec naming
- payload versioning
- base64 handling
- chunk-entry JSON shape

## Auto-Selection Policy

The runtime auto-router is intentionally balanced rather than purely size-driven:

1. evaluate every runtime-enabled chunk-local codec
2. find the smallest successful payload
3. keep codecs within 5% of that best size
4. choose the lowest decode-cost candidate from that subset

This favors smaller save files while protecting chunk load time.

## Offline Tooling

`chunk_compress.exe` is now a thin CLI over the shared codec layer. It can:

- read current `save.json` chunk entries
- decode mixed-codec saves
- re-run compression methods against decoded chunks
- emit comparison stats

It is no longer hard-wired to `cell_rle` only.

## Design Flaws And Current Limits

- The shared codec layer is now reusable, but the offline CLI still reads the whole JSON file into memory. That is acceptable for analysis, not ideal for giant saves.
- Runtime compression is on-disk only in this pass. Loaded gameplay chunks remain expanded in memory.
- `template` and `octree` are supported for correctness and experimentation, but they are not generally the best size/speed tradeoff for terrain-heavy saves.
- Delta and superchunk/group codecs need a separate save-schema pass before they should be used as first-class runtime persistence formats.
- The legacy `rle` reader still exists for compatibility, which keeps the codebase noisier than a clean version-7-only implementation.

## Source Hygiene Notes

This directory used to mix source with binaries, objects, giant example saves, and generated CSV output. The source tree should contain code, headers, build scripts, and small curated inputs only. Build outputs belong under normal build directories, not next to the sources.

---

## Related Documentation

### Up to Parent
- [SDK World Overview](../../SDK_WorldOverview.md) - World systems hub
- [SDK Chunks](../SDK_Chunks.md) - Chunk data structures

### Siblings - Chunk System
- [ChunkAnalysis/SDK_ChunkAnalysis.md](../ChunkAnalysis/SDK_ChunkAnalysis.md) - Analysis tools
- [ChunkManager/SDK_ChunkManager.md](../ChunkManager/SDK_ChunkManager.md) - Residency management
- [ChunkStreamer/SDK_ChunkStreamer.md](../ChunkStreamer/SDK_ChunkStreamer.md) - Async streaming
- [SDK_ChunkResidencyAndStreaming.md](../SDK_ChunkResidencyAndStreaming.md) - Streaming coordination

### Related World Systems
- [../../Persistence/SDK_PersistenceAndStorage.md](../../Persistence/SDK_PersistenceAndStorage.md) - Save/load system
- [../../ConstructionCells/SDK_ConstructionSystem.md](../../ConstructionCells/SDK_ConstructionSystem.md) - Construction

### Development & Debugging
- [../../../../Debugging/SDK_Debugging.md](../../../../Debugging/SDK_Debugging.md) - Debugging tools
- [../../../../Debugging/SDK_HeadlessDebugCLI.md](../../../../Debugging/SDK_HeadlessDebugCLI.md) - Headless CLI

---
*Documentation for `SDK/Core/World/Chunks/ChunkCompression/`*
