# ChunkAnalysis

Offline chunk inspection and reporting tool.

## Current Role

`ChunkAnalysis` decodes saved chunk payloads through the shared runtime helpers and then runs statistical or wall-geometry analysis over the decoded chunks.

It now depends on:

- `sdk_chunk_save_json.*`
  - shared parsing of chunk entries
- `sdk_chunk_codec.*`
  - shared payload decode dispatch
- `sdk_superchunk_geometry.h`
  - shared wall expectation logic

## Important Limits

- the CLI reads the entire `save.json` into memory
- it is meant for offline analysis, not gameplay-time diagnostics
- current analysis supports the version-7 `cells.codec` save path and shared-parser `cell_rle` compatibility
- old `legacy_rle` payloads are not a first-class analysis target in this pass

See [SDK_ChunkAnalysis.md](../../../../Docs/Core/World/Chunks/SDK_ChunkAnalysis.md) for the maintained documentation.
