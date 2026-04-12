# SDK Benchmark Suite

Comprehensive performance testing suite for the NQL SDK engine.

## Overview

The benchmark suite measures performance across all major engine systems:
- **World Generation**: Continental-scale and chunk-level terrain synthesis
- **Mesh Building**: Voxel geometry construction and optimization
- **Chunk Streaming**: Memory management and residency
- **Simulation**: Entity physics and updates
- **Persistence**: Save/load I/O performance
- **Settlement**: Building generation and placement

## Quick Start

### Build the Benchmarks
1. Open the SDK solution in Visual Studio 2022
2. Select **Benchmark|x64** configuration (or create it)
3. Build the solution

### Run All Benchmarks
```batch
cd x64\Benchmark
run_all_benchmarks.bat
```

Or run the master benchmark directly:
```batch
master_benchmark.exe
```

### Run Individual Benchmarks
Each benchmark can be run standalone:
```batch
bench_worldgen_continental.exe
bench_worldgen_chunk.exe
bench_mesh_builder.exe
bench_chunk_streaming.exe
bench_simulation.exe
bench_persistence.exe
bench_settlement.exe
```

## Output Files

Each benchmark generates two output files:

### JSON Format
Structured data for programmatic analysis:
```json
{
  "benchmark": "worldgen_chunk",
  "single_chunk_ms": 1.234,
  "batch_10_chunks_ms": 12.456,
  "memory_mb": 45
}
```

### CSV Format
One row per benchmark run, suitable for Excel/plotting:
```
benchmark,single_chunk_ms,batch_10_chunks_ms,memory_mb
worldgen_chunk,1.234,12.456,45
```

### Master Output
The master benchmark generates consolidated reports:
- `benchmark_results_master.json` - Full results with timestamps
- `benchmark_results_master.csv` - Time-series data for tracking

## Benchmark Details

### bench_worldgen_continental
Tests continental-scale terrain generation.

**Metrics:**
- Plate site generation (32x32, 64x64, 128x128)
- Climate field computation
- Memory usage

### bench_worldgen_chunk
Tests chunk-level terrain generation.

**Metrics:**
- Single chunk generation time
- Batch generation (10, 100 chunks)
- Column sampling performance
- Surface height lookup

### bench_mesh_builder
Tests voxel mesh generation.

**Metrics:**
- Empty chunk meshing
- Full chunk meshing (all solid)
- Complex chunk meshing (mixed terrain)
- Vertex counts

### bench_chunk_streaming
Tests chunk manager and streaming system.

**Metrics:**
- Allocation/deallocation overhead
- Hash lookup performance
- Residency calculation
- Topology rebuild

### bench_simulation
Tests physics and entity simulation.

**Metrics:**
- Entity simulation step time
- Entity list allocation

### bench_persistence
Tests save/load performance.

**Metrics:**
- Chunk serialization time
- Chunk deserialization time

### bench_settlement
Tests settlement generation.

**Metrics:**
- Settlement initialization
- Building placement

## Tracking Performance Over Time

### Version Control Approach
Commit benchmark results alongside code changes:
```bash
git add benchmark_results_master.csv
git commit -m "Baseline: Initial benchmark results"
```

### Regression Detection
Compare current results against baseline:
1. Run benchmarks before making changes
2. Save results as `baseline.csv`
3. Make code changes
4. Run benchmarks again
5. Compare new results against baseline

### Excel Analysis
Open `benchmark_results_master.csv` in Excel to:
- Create line charts showing trends
- Calculate percentage changes
- Identify performance regressions

## Extending the Suite

### Adding a New Benchmark

1. Create `bench_newsystem.c` in the Tests directory
2. Include `bench_common.h`
3. Implement benchmark functions
4. Generate JSON and CSV output
5. Add entry to `master_benchmark.c` g_benchmarks array
6. Rebuild

Example structure:
```c
#include "bench_common.h"
#include "../Core/sdk_newsystem.h"

int main(void) {
    BenchTimer timer;
    bench_timer_init(&timer);
    
    bench_timer_start(&timer);
    // ... benchmark code ...
    double elapsed_ms = bench_timer_end_ms(&timer);
    
    // Write JSON/CSV output
    return 0;
}
```

## Performance Expectations

Typical results on modern hardware (for reference):
- Single chunk generation: < 2 ms
- Mesh building (complex): < 5 ms
- Hash lookup: < 0.001 ms
- Entity step (100 entities): < 0.1 ms

**Note**: These are approximate values. Your results will vary based on hardware.

## Troubleshooting

### Benchmark Fails to Run
- Ensure all SDK DLLs are in the same directory as the .exe
- Check that the benchmark configuration built successfully
- Run from the output directory (x64/Benchmark)

### Results Seem Wrong
- Disable background processes (antivirus, updates)
- Run multiple times and compare results
- Check system resource usage (Task Manager)
- Ensure power plan is set to "High Performance"

### Missing Output Files
- Check file permissions in the output directory
- Verify benchmark completed successfully (exit code 0)
- Look for error messages in console output

## Best Practices

1. **Consistent Environment**: Run benchmarks on the same machine with consistent settings
2. **Multiple Runs**: Run 3-5 times and use median values to reduce variance
3. **Warm-up**: First run may be slower due to cold caches
4. **Idle System**: Close other applications during benchmarking
5. **Version Control**: Track results in git alongside code changes
6. **Document Changes**: Note what changed when performance shifts significantly
