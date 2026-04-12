# SDK Benchmarking

## Scope

This page documents the benchmark harness compiled under `Core/Benchmark/`.

Current source:

- `Core/Benchmark/sdk_benchmark.h`
- `Core/Benchmark/sdk_benchmark.c`

This is separate from the standalone sources under `SDK/Tests/`.

## How It Works

The benchmark harness is linked into the main `NqlSDK` application. It is triggered from the frontend menu rather than from a dedicated console tool.

Current flow:

```text
frontend menu action
  -> run_performance_benchmarks()
      -> create temporary worldgen instance
      -> generate hardcoded sample chunks
      -> time worldgen and mesh build steps
      -> write JSON + CSV files to working directory
      -> show summary in MessageBoxA
```

Outputs:

- `sdk_benchmark_results.json`
- `sdk_benchmark_results.csv`
- `sdk_benchmark_details.csv`

## What It Measures

- worldgen timings across hardcoded coordinate patterns
- mesh-build timings for a subset of those generated chunks
- rough process working-set usage

It is primarily a developer profiling aid, not a reproducible test harness.

## Known Issues And Design Flaws

- The benchmark harness is compiled into the main application instead of living behind a dedicated test/tool target.
- It is Windows/UI bound. Results are presented through `MessageBoxA`, which makes the flow awkward for CI or batch automation.
- It writes output files to the current working directory instead of a dedicated benchmark artifact root.
- The workload is hardcoded. There is no CLI, seed control from the user, or parameterized sampling plan.
- `sdk_benchmark.c` mutates `worldgen_times[]` while writing the detailed CSV, then counts outliers afterward. That means the final outlier counts shown in the message box are not trustworthy.
- The harness initializes its own temporary worldgen directly, so it is closer to a subsystem probe than to an end-to-end gameplay benchmark.

## Related Docs

- Standalone tests and benchmark sources: [../../Tests/SDK_TestsAndBenchmarks.md](../../Tests/SDK_TestsAndBenchmarks.md)
- Build guide: [../../Build/SDK_BuildGuide.md](../../Build/SDK_BuildGuide.md)
- Worldgen overview: [../World/SDK_WorldOverview.md](../World/SDK_WorldOverview.md)
