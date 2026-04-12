# Benchmark Suite Build Integration

## Files Created

### Shared Utilities
- `bench_common.h` - Shared benchmark utilities header
- `bench_common.c` - Implementation (timing, JSON/CSV output, memory tracking)

### Individual Benchmarks
- `bench_worldgen_continental.c` - Continental-scale terrain generation
- `bench_worldgen_chunk.c` - Chunk-level terrain generation  
- `bench_mesh_builder.c` - Voxel mesh building
- `bench_chunk_streaming.c` - Chunk manager and streaming
- `bench_simulation.c` - Physics and entity simulation
- `bench_persistence.c` - Save/load I/O
- `bench_settlement.c` - Settlement generation

### Master Runner
- `master_benchmark.c` - Runs all benchmarks and generates consolidated report
- `run_all_benchmarks.bat` - Batch script to execute master benchmark

### Documentation
- `README_BENCHMARKS.md` - Usage guide and benchmark details
- `BUILD_INTEGRATION.md` - This file

## Integration Steps

### Option 1: Add to Existing Visual Studio Solution

1. **Open NqlSDK.sln** in Visual Studio 2022

2. **Create New Projects** for each benchmark:
   - Right-click Solution → Add → New Project
   - Choose "Console App (C++)" but use .c extension
   - Project name: `BenchWorldgenContinental` (repeat for each)
   - Location: `SDK/Tests/`

3. **Configure Each Project**:
   - **Configuration**: Create new "Benchmark|x64" or use "Release|x64"
   - **Output Directory**: `$(SolutionDir)x64\Benchmark\`
   - **Include Directories**: Add `$(ProjectDir)..\Core;$(ProjectDir)`
   - **Additional Dependencies**: Link against psapi.lib (for memory tracking)
   - **C/C++** → **Precompiled Headers**: Not Using Precompiled Headers

4. **Add Source Files** to each project:
   - Add `bench_common.c` and `bench_common.h` to all projects
   - Add respective `bench_*.c` file to each project

5. **Link Dependencies**:
   Each benchmark needs to link against SDK Core libraries. Add references to:
   - SDK Core static library (if available)
   - OR add SDK Core .c/.cpp files as "Add Existing Item" (compile but don't link)

### Option 2: Manual Build (Command Line)

Create a simple build script for each benchmark:

```batch
@echo off
set SDK_ROOT=..\..\Core
set INCLUDES=/I"%SDK_ROOT%" /I"."
set LIBS=psapi.lib

cl /nologo /O2 /W3 %INCLUDES% bench_common.c bench_worldgen_continental.c ^
   %SDK_ROOT%\sdk_worldgen_internal.c ^
   %SDK_ROOT%\sdk_worldgen.c ^
   /Fe:bench_worldgen_continental.exe ^
   /link %LIBS%
```

**Note**: You'll need to identify which SDK Core .c files each benchmark depends on.

### Option 3: CMake Build (Recommended for Cross-Platform)

Create `CMakeLists.txt` in Tests directory:

```cmake
cmake_minimum_required(VERSION 3.15)
project(SDK_Benchmarks)

set(CMAKE_C_STANDARD 11)

# Common library
add_library(bench_common STATIC bench_common.c bench_common.h)
target_link_libraries(bench_common psapi)

# Individual benchmarks
add_executable(bench_worldgen_continental bench_worldgen_continental.c)
target_link_libraries(bench_worldgen_continental bench_common sdk_core)

add_executable(bench_worldgen_chunk bench_worldgen_chunk.c)
target_link_libraries(bench_worldgen_chunk bench_common sdk_core)

# ... repeat for each benchmark ...

# Master benchmark
add_executable(master_benchmark master_benchmark.c)
target_link_libraries(master_benchmark bench_common)
```

## Dependency Resolution

### SDK Core Dependencies

Each benchmark includes headers from `../Core/`. You need to ensure:

1. **Headers are accessible**: Include path set correctly
2. **Functions are available**: Either:
   - Link against SDK Core static library
   - Compile SDK Core .c files alongside benchmark
   - Use DLL exports if SDK is built as DLL

### Required SDK Core Modules

| Benchmark | Core Dependencies |
|-----------|------------------|
| bench_worldgen_continental | sdk_worldgen_internal.h/c |
| bench_worldgen_chunk | sdk_worldgen.h/c, sdk_chunk.h/c |
| bench_mesh_builder | sdk_mesh_builder.h/c, sdk_chunk.h/c, sdk_block.h/c |
| bench_chunk_streaming | sdk_chunk_manager.h/c |
| bench_simulation | sdk_simulation.h/c, sdk_entity.h/c |
| bench_persistence | sdk_persistence.h/c, sdk_chunk.h/c |
| bench_settlement | sdk_settlement.h/c |

### System Libraries

All benchmarks require:
- `psapi.lib` - For memory usage tracking (GetProcessMemoryInfo)
- Standard C runtime

## Testing the Build

1. **Build all benchmarks**
2. **Navigate to output directory**: `cd x64\Benchmark`
3. **Run a single benchmark**: `bench_worldgen_chunk.exe`
4. **Verify output files created**: `bench_worldgen_chunk.json` and `.csv`
5. **Run master benchmark**: `master_benchmark.exe`
6. **Check consolidated output**: `benchmark_results_master.json` and `.csv`

## Troubleshooting Build Issues

### Linker Errors (Unresolved External Symbol)

**Problem**: Benchmark can't find SDK functions  
**Solution**: 
- Add SDK Core .c files to project
- OR link against SDK Core library
- OR verify DLL exports if using DLL build

### Include Path Errors

**Problem**: Cannot find sdk_*.h files  
**Solution**: Add `$(ProjectDir)..\Core` to Include Directories

### psapi.lib Missing

**Problem**: Cannot find GetProcessMemoryInfo  
**Solution**: Add `psapi.lib` to Additional Dependencies in Linker settings

### Runtime DLL Missing

**Problem**: Application fails to start, 0xC0000135 error  
**Solution**: 
- Build in Release mode with static runtime (/MT)
- OR ensure all DLLs are in same directory as .exe
- OR use vcpkg/dependency manager

## Next Steps After Building

1. **Run baseline benchmarks** to establish performance baseline
2. **Commit results** to version control
3. **Set up CI/CD** to run benchmarks automatically on commits
4. **Create performance dashboard** to track trends over time
5. **Add more benchmarks** as new systems are developed

## Performance Tuning

To get accurate benchmark results:

1. **Build in Release mode** with optimizations enabled
2. **Disable debug features** (assertions, logging)
3. **Run on idle system** (close other applications)
4. **Use consistent power settings** (High Performance mode)
5. **Run multiple times** and take median/average
6. **Disable antivirus** temporarily if it interferes

## Maintenance

As the SDK evolves:

- **Update benchmarks** when APIs change
- **Add new benchmarks** for new features
- **Adjust metrics** if defaults become too fast/slow
- **Archive old results** for historical comparison
- **Document significant changes** in results
