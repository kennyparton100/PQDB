<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [World](../../SDK_WorldOverview.md) > [Chunks](../SDK_Chunks.md) > Analysis

---

# SDK Chunk Analysis

This page documents the offline chunk-analysis tool under `Core/World/Chunks/ChunkAnalysis/`.

## Current Scope

`ChunkAnalysis` is an offline inspection tool. It is not part of the gameplay runtime.

Current responsibilities:

- load `save.json`
- decode persisted chunk payloads through the shared runtime codec layer
- analyze block distribution, palette/entropy patterns, and wall placement
- export CSV/JSON summaries

## Shared Runtime Dependencies

The main cleanup in this area was removing private copies of runtime parsing logic.

The tool now reuses:

- `sdk_chunk_save_json.*`
- `sdk_chunk_codec.*`
- `sdk_superchunk_geometry.h`

That keeps the analysis path aligned with the current save contract and wall-layout helpers.

## Detached Walls

Detached settings are now treated as live geometry inputs:

- wall-grid size
- wall-grid offset x/z
- detached-vs-attached normalization

The tool no longer documents detached walls as metadata-only behavior.

## Current Limits

- `ChunkAnalysis` still reads the full `save.json` into memory before analysis.
- Some heavier report modes are intentionally offline-only.
- A few analysis helpers are still heuristic or placeholder quality rather than authoritative simulation.

Examples:

- simple clustering/export helpers in `chunk_analysis.c`
- placeholder `top_y` handling in some synthesized analysis paths

Those are audit items, not blockers for using the tool as a save-format and wall-layout verifier.

## Related Docs

- [ChunkCompression/SDK_ChunkCompression.md](../ChunkCompression/SDK_ChunkCompression.md)
- [ChunkManager/SDK_ChunkManager.md](../ChunkManager/SDK_ChunkManager.md)
- [ChunkStreamer/SDK_ChunkStreamer.md](../ChunkStreamer/SDK_ChunkStreamer.md)
- [SDK_ChunkResidencyAndStreaming.md](../SDK_ChunkResidencyAndStreaming.md)
- [SDK_PersistenceAndStorage.md](../../Persistence/SDK_PersistenceAndStorage.md)
- [SDK_WorldSystemsGapAudit.md](../../../../SDK_WorldSystemsGapAudit.md)
