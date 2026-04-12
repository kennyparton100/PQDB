@echo off
REM run_all_benchmarks.bat - Execute all SDK benchmarks

echo ========================================
echo SDK Benchmark Suite Runner
echo ========================================
echo.

REM Check if master benchmark exists
if not exist "master_benchmark.exe" (
    echo ERROR: master_benchmark.exe not found
    echo Please build the benchmark suite first
    pause
    exit /b 1
)

REM Run master benchmark
master_benchmark.exe

REM Check exit code
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo WARNING: Some benchmarks failed or regressed
    pause
    exit /b 1
)

echo.
echo All benchmarks completed successfully
pause
exit /b 0
