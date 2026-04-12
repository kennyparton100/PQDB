@echo off
REM Quick test script for WorldgenDebugger
REM Run after building with build.bat

echo Running default analysis (16x16 superchunk, detached mode)...
WorldgenDebugger.exe --visualize --range 17

echo.
echo ========================================
echo Testing attached mode...
WorldgenDebugger.exe --walls-detached 0 --range 17

echo.
echo ========================================
echo Testing different chunk span (8x8)...
WorldgenDebugger.exe --chunk-span 8 --visualize --range 9

echo.
echo ========================================
echo Testing different chunk span (32x32)...
WorldgenDebugger.exe --chunk-span 32 --visualize --range 33

pause
