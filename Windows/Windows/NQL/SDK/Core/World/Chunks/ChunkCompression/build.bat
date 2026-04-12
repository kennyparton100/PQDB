@echo off
setlocal
pushd "%~dp0"

echo Building chunk_compress...

set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat

if not exist "%VCVARS%" (
    echo Error: Could not find vcvars64.bat
    popd
    exit /b 1
)

call "%VCVARS%"

set OUTDIR=..\..\..\..\Build\Tooling\ChunkCompression
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

cl /O2 /W3 /nologo /MD ^
  /I"." ^
  /I".." ^
  /Fo"%OUTDIR%\\" ^
  /Fe"%OUTDIR%\\chunk_compress.exe" ^
  main.c ^
  rle_decoder.c ^
  chunk_compress.c ^
  chunk_compress_impl.c ^
  sdk_chunk_codec.c ^
  sdk_chunk_tool_stubs.c ^
  ..\sdk_chunk.c ^
  ..\..\Blocks\sdk_block.c ^
  ..\..\ConstructionCells\sdk_construction_cells.c ^
  ..\..\Simulation\sdk_simulation.c ^
  ..\..\Persistence\sdk_chunk_save_json.c ^
  ..\..\Superchunks\Config\sdk_superchunk_config.c

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    popd
    exit /b 1
)

echo.
echo Build successful!
echo Output: %OUTDIR%\chunk_compress.exe
echo Usage: chunk_compress.exe save.json [max_chunks]

popd
endlocal
