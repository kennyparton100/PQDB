# WorldgenDebugger

Headless debugging tool for worldgen and wall-layout inspection.

## Current Reality

The debugger is useful for offline world-layout inspection, but it is not the gameplay runtime. It reuses shared helpers and stubs out renderer-facing code.

Shared dependencies now used here:

- `sdk_superchunk_geometry.h`
- `sdk_chunk_codec.*`
- `sdk_chunk_save_json.*`

That keeps debugger output aligned with current save files and wall geometry.

## Hygiene Rule

Do not keep generated files in this directory:

- `WorldgenDebugger.exe`
- `.obj`
- generated CSV / JSON reports
- transient logs
- large sample world saves

Use normal build output folders and external test data instead.

See [SDK_Debugging.md](../Docs/Debugging/SDK_Debugging.md) for the maintained documentation.
