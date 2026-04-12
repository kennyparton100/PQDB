# ChunkCompression

Shared chunk codec code plus the `chunk_compress` offline CLI.

## What Lives Here

- `sdk_chunk_codec.*`
  - shared runtime codec dispatch used by persistence, debugger tooling, and offline utilities
- `sdk_chunk_save_json.*` in `../../Persistence/`
  - companion JSON helper for the nested `cells` save format
- `main.c`
  - offline CLI for inspecting and comparing chunk payloads

## Runtime Save Contract

Runtime chunk saves now store cell data as:

```json
"cells": {
  "codec": "hierarchical_rle_v2",
  "payload_b64": "...",
  "payload_version": 1
}
```

The runtime no longer hard-wires chunk persistence to `cell_rle`.

## Runtime-Enabled Codecs

- `cell_rle`
- `volume_layer`
- `template`
- `octree`
- `bitpack`
- `sparse_column`
- `rle_bitmask`
- `hierarchical_rle_v2`

Not selected by runtime auto-routing in this pass:

- `inter_chunk_delta`
- `delta_template`
- `superchunk_hier`

## Known Limits

- runtime compression is on-disk only; gameplay chunks stay decompressed in memory
- the CLI still reads whole `save.json` files into memory
- large generated saves and build artifacts do not belong in this source folder

See [SDK_ChunkCompression.md](../../../../Docs/Core/World/Chunks/SDK_ChunkCompression.md) for the maintained documentation.
