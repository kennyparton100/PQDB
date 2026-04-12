@echo off
REM Simple build script for benchmarks - compiles minimal test versions

set SDK_CORE=..\Core
set INCLUDES=/I"%SDK_CORE%" /I"."
set LIBS=psapi.lib

echo Building bench_common...
cl /nologo /c /O2 /W3 %INCLUDES% bench_common.c

echo Building bench_worldgen_continental...
cl /nologo /O2 /W3 %INCLUDES% bench_common.obj bench_worldgen_continental.c /Fe:bench_worldgen_continental.exe /link %LIBS%

echo Building bench_simulation...
cl /nologo /O2 /W3 %INCLUDES% bench_common.obj bench_simulation.c /Fe:bench_simulation.exe /link %LIBS%

echo Building bench_chunk_streaming...
cl /nologo /O2 /W3 %INCLUDES% bench_common.obj bench_chunk_streaming.c /Fe:bench_chunk_streaming.exe /link %LIBS%

echo Build complete!
