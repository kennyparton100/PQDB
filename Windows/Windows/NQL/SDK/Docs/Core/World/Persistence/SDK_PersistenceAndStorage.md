# NQL SDK Persistence And Storage

This page documents the current world-save contract.

## Current Versions

- `save.json version = 7`
- `worldgen_revision = 13`

Revision `13` invalidates older cached wall data from the hybrid-layout period.

## World Save Layout

Each world uses:

```text
Build/WorldSaves/<world_id>/
  meta.txt
  save.json
```

`meta.txt` stores the lightweight frontend settings. `save.json` stores runtime state plus the chunk cache.

Current `meta.txt` fields include:

- display name / folder identity
- seed
- render distance
- `spawn_mode`
- settlements / construction cells toggles
- superchunk and wall configuration

`spawn_mode` is optional for backward compatibility and is interpreted as:

- `0 = random`
- `1 = center/classic`
- `2 = safe`

If `spawn_mode` is absent, runtime defaults to `2` (`safe`).

## Chunk Storage

Persisted chunks now use codec-tagged cell payloads:

```json
{
  "cx": 1,
  "cz": 1,
  "top_y": 512,
  "cells": {
    "codec": "hierarchical_rle_v2",
    "payload_b64": "...",
    "payload_version": 1
  },
  "fluid": "...",
  "construction": "..."
}
```

Supported runtime save codecs include:

- `cell_rle`
- `volume_layer`
- `template`
- `octree`
- `bitpack`
- `sparse_column`
- `rle_bitmask`
- `hierarchical_rle_v2`

## Save Grouping

Chunk grouping depends on superchunk and wall settings:

- when detached mode is off, chunks are written under `superchunks`
- when detached mode is on, non-wall chunks are written under `terrain_superchunks`
- detached wall chunks are written under `wall_chunks`

Important correction:

- `walls_detached`, `wall_grid_size`, and wall-grid offsets now affect live wall classification and save/debug grouping
- they are not just round-tripped metadata anymore

Terrain-superchunk grouping uses the terrain superchunk period derived from `chunk_span`.
Wall-chunk classification uses the shared wall-grid helpers from `sdk_superchunk_geometry.h`.

## Construction Persistence

Worldgen and runtime construction both use the normal chunk construction path:

- per-chunk `construction` payloads
- shared overflow registry stored under `construction_archetypes`

That means worldgen construction cells created in enabled worlds survive save/load without a separate special-case format.

## Known Limits

- `save.json` is still one large JSON container, so very large worlds still pay the structural cost of a monolithic file.
- In-memory live chunk compression is deferred.
- Mixed old saves can still load through compatibility paths, but current tooling assumes active work is on the version-7 contract.
- `meta.txt` remains a lightweight settings file. Runtime state such as player position, chunk cache, and simulation state still belongs in `save.json`.

## Related Docs

- [SDK_ChunkCompression.md](../Chunks/ChunkCompression/SDK_ChunkCompression.md)
- [SDK_ChunkAnalysis.md](../Chunks/ChunkAnalysis/SDK_ChunkAnalysis.md)
- [SDK_SuperChunks.md](../Superchunks/SDK_SuperChunks.md)
