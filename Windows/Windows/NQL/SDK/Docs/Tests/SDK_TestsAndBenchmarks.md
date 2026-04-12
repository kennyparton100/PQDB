<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Tests

---

# SDK Tests And Standalone Benchmarks

## Scope

This page documents the standalone material under `SDK/Tests/`.

Key files:

- `master_benchmark.c`
- `bench_*.c`
- `bench_common.c`
- `run_all_benchmarks.bat`
- `README_BENCHMARKS.md`
- `BUILD_INTEGRATION.md`

## What This Directory Is

`SDK/Tests/` is a separate developer toolbox of console-oriented benchmark sources and notes.

It is not the same thing as:

- the in-app benchmark harness under `Core/Benchmark/`
- the `WorldgenDebugger` tool under `SDK/Debugging/`

## How It Works

The checked-in solution does not build these tests directly.

Instead, the directory contains:

- benchmark source files for individual subsystems
- a master runner
- batch/build notes describing how to wire them into a project or external build

The intended workflow is:

```text
compile one or more bench_*.c programs
  -> run them from a benchmark output directory
  -> collect JSON / CSV artifacts
```

## Known Issues And Design Flaws

- The checked-in Visual Studio solution does not include these tests as first-class projects, so the docs here are partly procedural guidance rather than an executable contract.
- `README_BENCHMARKS.md` and `BUILD_INTEGRATION.md` are useful, but they describe several possible build approaches instead of one canonical maintained path.
- Output handling is manual and current-directory based.
- There is overlap with `Core/Benchmark/`, which means the SDK currently has two separate benchmarking stories instead of one coherent test strategy.

## Recommended Use

- Use `SDK/Tests/` when you want standalone console benchmarks or when you need to wire custom perf runners into another build system.
- Use `Core/Benchmark/` only when you explicitly want the in-app benchmark path exposed through the frontend runtime.

---

## Related Documentation

### Up to Root
- [SDK Overview](../SDK_Overview.md) - Documentation home

### Benchmarking
- [../Core/Benchmark/SDK_Benchmarking.md](../Core/Benchmark/SDK_Benchmarking.md) - In-app benchmark harness

### Build & Debug
- [../Build/SDK_BuildGuide.md](../Build/SDK_BuildGuide.md) - Build guide
- [../Debugging/SDK_Debugging.md](../Debugging/SDK_Debugging.md) - Debugging tools
- [../Debugging/SDK_HeadlessDebugCLI.md](../Debugging/SDK_HeadlessDebugCLI.md) - Headless CLI

### Core Systems
- [../Core/Runtime/SDK_RuntimeDiagnostics.md](../Core/Runtime/SDK_RuntimeDiagnostics.md) - Runtime diagnostics
- [../Core/Profiler/SDK_Profiler.md](../Core/Profiler/SDK_Profiler.md) - Profiling

---
*Documentation for `SDK/Tests/`*
