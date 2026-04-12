# SDK Headless Debug CLI

This page documents the unified headless debugging CLI implemented in `SDK/Debugging/`.

The current binary name remains `WorldgenDebugger.exe`, but the command model is now the supported `nql_debug` style subcommand surface:

```text
WorldgenDebugger.exe world create ...
WorldgenDebugger.exe world meta ...
WorldgenDebugger.exe world doctor ...
WorldgenDebugger.exe session bootstrap ...
WorldgenDebugger.exe chunks analyze ...
WorldgenDebugger.exe chunks codecs ...
WorldgenDebugger.exe walls map ...
```

## Goals

- create worlds through the same normalized metadata path as the game
- target worlds by world id or save directory instead of raw `save.json` paths
- run a real headless startup/bootstrap path
- expose structured `--json` output for AI and automation
- reuse shared runtime helpers instead of debugger-local writers and parsers

## Primary Source Files

- `SDK/Debugging/debugger_main.c`
- `SDK/Debugging/debugger_cli_world.c`
- `SDK/Debugging/debugger_cli_runtime.c`
- `SDK/Debugging/debugger_cli_walls.c`
- `SDK/Core/World/Persistence/sdk_world_tooling.h`
- `SDK/Core/World/Persistence/sdk_world_tooling.c`
- `SDK/Core/API/Session/sdk_session_headless.h`
- `SDK/Core/API/Session/sdk_session_headless.c`

## Shared Internal Types

### World tooling

- `SdkWorldCreateRequest`
- `SdkWorldCreateResult`
- `SdkWorldTarget`

These types are defined in `sdk_world_tooling.h` and are used by both the frontend and the CLI.

### Headless startup

- `SdkSessionStartRequest`
- `SdkSessionStartResult`
- `SdkStartupReadinessSnapshot`

These types are defined in `sdk_session_headless.h` and expose the headless bootstrap contract.

## World Target Resolution

All world-oriented commands resolve a target world through shared helpers instead of reading raw files directly from the command line.

Supported forms:

- `--world world_029`
- `--world-dir Build\WorldSaves\world_029`
- `--world-dir C:\full\path\to\world_029`

The resolved target exposes:

- world id / folder id
- world directory
- `meta.txt` path
- `save.json` path
- whether `meta.txt` exists
- whether `save.json` exists

Default root for CLI-created worlds:

```text
SDK/Build/WorldSaves
```

## Commands

### `world create`

Creates a world using the shared normalized metadata contract.

Example:

```text
WorldgenDebugger.exe world create --world world_cli_test --seed 123 --render-distance 16 --superchunks 1 --walls 1 --walls-detached 1 --chunk-span 16 --json
```

Important options:

- `--world <id>`
- `--world-dir <path>`
- `--seed <u32>`
- `--render-distance <int>`
- `--spawn-mode random|center|safe`
- `--settlements 0|1`
- `--construction-cells 0|1`
- `--superchunks 0|1`
- `--chunk-span <int>`
- `--walls 0|1`
- `--walls-detached 0|1`
- `--wall-grid-size <int>`
- `--wall-offset-x <int>`
- `--wall-offset-z <int>`
- `--json`

Behavior:

- creates the world directory if missing
- normalizes the requested settings
- writes `meta.txt` through `sdk_world_meta_save`
- does not hand-write metadata with debugger-only code

### `world meta`

Shows resolved target information plus raw / normalized metadata.

Example:

```text
WorldgenDebugger.exe world meta --world world_cli_test --json
```

Intended use:

- verify what was requested versus what was normalized
- confirm where the CLI thinks the world actually lives
- inspect `meta.txt` without manually opening it

### `world doctor`

Validates a world folder and reports mismatches or incomplete state.

Example:

```text
WorldgenDebugger.exe world doctor --world world_029 --json
```

Typical checks:

- `meta.txt` exists
- `save.json` exists
- metadata loads
- normalized config is coherent
- persisted save loads when present

This command is the intended fast check for cases like:

- `walls_enabled=1` with `superchunks_enabled=0`
- `meta.txt` exists but `save.json` does not
- a world folder resolves to the wrong root

### `session bootstrap`

Runs the real headless startup/bootstrap path without the windowed game shell.

Example:

```text
WorldgenDebugger.exe session bootstrap --world world_cli_test --stop-at resident --json
```

Important options:

- `--world <id>`
- `--world-dir <path>`
- `--create-if-missing`
- `--stop-at resident|gpu`
- `--safety-radius <int>`
- `--max-iterations <int>`
- `--save-on-success 0|1`
- world-create options for create-if-missing mode
- `--json`

Returned readiness data includes:

- desired primary chunk count
- resident primary chunk count
- GPU-ready chunk count
- pending jobs
- pending results

Use this command when:

- startup says `loading nearby terrain X/Y/Z`
- a created world looks wrong before opening the full game
- a metadata change needs a fast bootstrap validation loop

### `chunks analyze`

Routes the legacy chunk analysis engine through world target resolution.

Example:

```text
WorldgenDebugger.exe chunks analyze --world world_016 --max-chunks 16
WorldgenDebugger.exe chunks analyze --world world_016 --max-chunks 16 --json
```

Behavior:

- resolves the world first
- forwards the resolved `save.json` to the chunk analysis engine
- preserves legacy analysis options
- supports CLI-level `--json`

`--json` mode:

- runs the legacy analyzer quietly
- writes its normal JSON report
- returns a stable JSON envelope containing:
  - `world_id`
  - `save_path`
  - `report_path`
  - embedded `report`

This keeps the analysis path scriptable without requiring the operator to pass `save.json` directly.

### `chunks codecs`

Summarizes chunk codec usage for a resolved world save.

Example:

```text
WorldgenDebugger.exe chunks codecs --world world_016 --max-chunks 8 --json
```

Current output includes:

- total chunk entries seen
- sampled decode count
- decode success / failure totals
- per-codec chunk counts
- per-codec payload size totals

The command scans:

- nested `terrain_superchunks[*].chunks`
- top-level `wall_chunks`
- other chunk-array layouts that use the same chunk-entry contract

### `walls map`

Keeps the existing wall/superchunk mapping analysis as part of the unified CLI.

Example:

```text
WorldgenDebugger.exe walls map --chunk-span 16 --walls-detached 1 --wall-grid-size 18 --offset-x 0 --offset-z 0 --json
```

Use this to:

- inspect current shared wall layout behavior
- verify detached-wall grid inputs
- compare counts of wall, corner, and interior chunks

## JSON Output Convention

The maintained commands support readable text by default and structured output via `--json`.

Currently supported:

- `world create`
- `world meta`
- `world doctor`
- `session bootstrap`
- `chunks analyze`
- `chunks codecs`
- `walls map`

Machine-readable output is intended for:

- AI-driven debugging loops
- automated smoke tests
- regression snapshots

## Frontend And CLI Fidelity

World creation is no longer supposed to drift between frontend and debugger paths.

The maintained contract is:

1. build a `SdkWorldCreateRequest`
2. normalize through shared world tooling
3. write `meta.txt` with shared helpers
4. resolve `SdkWorldDesc` and `SdkSuperchunkConfig` from the same metadata

That means the CLI is now the correct place to validate whether a new-world metadata bug is in:

- frontend input capture
- request construction
- metadata normalization
- metadata persistence
- headless startup

instead of guessing from `meta.txt` alone.

## Known Limits

- The binary name is still `WorldgenDebugger.exe`. The command surface is unified, but the rename to `nql_debug` is deferred.
- `chunks analyze` still uses the legacy analysis engine internally. The CLI wrapper resolves world targets and adds structured output, but the analysis internals are not yet a fresh rewrite.
- Headless bootstrap is a real startup path, but it still uses headless-safe approximations for renderer-dependent mesh readiness.
- The CLI is the preferred debugging surface for world creation and startup, but it is not a replacement for full in-game validation of rendering and performance bugs.

## Suggested Workflow

For a suspected new-world creation bug:

1. `world create --json`
2. `world meta --json`
3. `world doctor --json`
4. `session bootstrap --json`

For a save / chunk-format investigation:

1. `world meta --json`
2. `chunks codecs --json`
3. `chunks analyze --json`
4. `walls map --json` when wall layout is relevant

## Related Docs

- [SDK_Debugging.md](./SDK_Debugging.md)
- [SDK_ChunkAnalysis.md](../Core/World/Chunks/ChunkAnalysis/SDK_ChunkAnalysis.md)
- [SDK_ChunkCompression.md](../Core/World/Chunks/ChunkCompression/SDK_ChunkCompression.md)
- [SDK_PersistenceAndStorage.md](../Core/World/Persistence/SDK_PersistenceAndStorage.md)
