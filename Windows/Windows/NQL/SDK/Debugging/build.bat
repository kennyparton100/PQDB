@echo off
setlocal
pushd "%~dp0"

echo Building WorldgenDebugger...

set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat

if not exist "%VCVARS%" (
    echo Error: Could not find vcvars64.bat
    popd
    exit /b 1
)

call "%VCVARS%"

MSBuild ..\Build\WorldgenDebugger.vcxproj /m /p:Configuration=Debug /p:Platform=x64

if %ERRORLEVEL% neq 0 (
    echo Build failed!
    popd
    exit /b 1
)

echo.
echo Build successful!
echo Output: ..\x64\Debug\WorldgenDebugger.exe

popd
endlocal
