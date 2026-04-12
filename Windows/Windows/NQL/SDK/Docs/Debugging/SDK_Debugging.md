<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Debugging

---

# SDK Debugging And Analysis

This page documents the maintained debugging surface under `SDK/Debugging/`.

## Scope

The main debugger target is the separate `WorldgenDebugger` project. It now acts as the maintained headless debug CLI for world creation, metadata inspection, headless startup, chunk analysis, codec inspection, and wall mapping.

Important source files:

- `debugger_main.c`
- `debugger_cli_world.c`
- `debugger_cli_runtime.c`
- `debugger_cli_walls.c`
- `debugger_mapping.c`
- `debugger_stubs.c`

Important dependency boundaries:

- shared wall geometry from `sdk_superchunk_geometry.h`
- shared chunk save JSON helpers from `sdk_chunk_save_json.*`
- shared runtime chunk codecs from `sdk_chunk_codec.*`

## What The Debugger Is Good For

- creating worlds through the shared metadata path
- resolving worlds by world id or save directory
- validating `meta.txt` and `save.json` coherence
- running headless startup/bootstrap diagnostics
- generating or exporting world-layout reports
- checking wall placement against the shared geometry helpers
- inspecting worldgen output without the renderer or live session shell

## What It Is Not Good For

- renderer-only bugs
- input, UI, or windowing bugs
- chunk residency / streamer timing bugs
- GPU upload behavior
- full runtime performance validation

Those paths are stubbed or bypassed.

## Current Architecture

`debugger_stubs.c` provides no-op implementations for renderer/frustum glue that headless tools do not need.

The maintained user-facing command surface is documented in:

- [SDK_HeadlessDebugCLI.md](./SDK_HeadlessDebugCLI.md)

`debugger_mapping.c` no longer owns a private chunk-save writer. It now writes chunk entries through the same shared helpers as the runtime:

- `sdk_chunk_codec_encode_auto`
- `sdk_chunk_save_json_write_entry`

That matters because older debugger output had drifted away from current save-file and wall-layout rules.

## Design Flaws And Risks

- The debugger is still a partial environment. A “correct” debugger result does not prove gameplay, streaming, or rendering behavior is correct.
- Historical notes such as `DETACHED_WALLS_ANALYSIS.md` describe older wall-model assumptions. Current gameplay wall placement is the shared-boundary model, and detached-wall fields are persisted metadata rather than a second live coordinate system.
- The source tree previously mixed `.obj`, `.exe`, CSV exports, logs, and giant sample saves into the same folder as source. That makes review and code search harder and should stay cleaned up.

## Hygiene Rule

Treat `SDK/Debugging/` as source plus small reference inputs only.

Do not keep these in the source folder:

- `WorldgenDebugger.exe`
- `.obj` files
- generated CSV / JSON reports
- large sample `save.json` snapshots
- transient debug logs

Use normal build output folders or dedicated external test-data locations instead.

---

## Related Documentation

### Up to Root
- [SDK Overview](../SDK_Overview.md) - Documentation home

### Debugging Tools
- [SDK_HeadlessDebugCLI.md](./SDK_HeadlessDebugCLI.md) - Headless CLI reference

### Build & Analysis
- [../Build/SDK_BuildGuide.md](../Build/SDK_BuildGuide.md) - Build guide
- [../Core/World/Chunks/ChunkCompression/SDK_ChunkCompression.md](../Core/World/Chunks/ChunkCompression/SDK_ChunkCompression.md) - Compression
- [../Core/World/Chunks/ChunkAnalysis/SDK_ChunkAnalysis.md](../Core/World/Chunks/ChunkAnalysis/SDK_ChunkAnalysis.md) - Chunk analysis
- [../Core/World/Persistence/SDK_PersistenceAndStorage.md](../Core/World/Persistence/SDK_PersistenceAndStorage.md) - Persistence

### Testing
- [../Tests/SDK_TestsAndBenchmarks.md](../Tests/SDK_TestsAndBenchmarks.md) - Testing framework
- [../Core/Benchmark/SDK_Benchmarking.md](../Core/Benchmark/SDK_Benchmarking.md) - In-app benchmarks

---
*Documentation for `SDK/Debugging/`*
