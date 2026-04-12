<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Build

---

# NQL SDK Build Guide

This guide matches the checked-in Visual Studio solution and project files under `Windows/NQL/SDK/Build/`.

## Prerequisites

- Visual Studio 2022
- MSVC toolset `v143`
- Desktop development with C++
- Windows 10 SDK

No external package-manager bootstrap is required by the checked-in solution.

## Open The Solution

Open:

- `Windows/NQL/SDK/Build/NqlSDK.sln`

The solution currently contains two projects:

- `NqlSDK`
- `WorldgenDebugger`

Checked-in configurations:

- `Debug | x64`
- `Release | x64`

`SDK/Tests/` contains standalone benchmark sources and notes, but those tests are not integrated as first-class solution projects. See [../Tests/SDK_TestsAndBenchmarks.md](../Tests/SDK_TestsAndBenchmarks.md).

## Build Outputs

The checked-in `.vcxproj` files write to:

- `Windows/NQL/SDK/x64/Debug/`
- `Windows/NQL/SDK/x64/Release/`

Primary outputs:

- `Windows/NQL/SDK/x64/Debug/NqlSDK.exe`
- `Windows/NQL/SDK/x64/Release/NqlSDK.exe`
- `Windows/NQL/SDK/x64/Debug/WorldgenDebugger.exe`
- `Windows/NQL/SDK/x64/Release/WorldgenDebugger.exe`

## Post-Build Asset Copy

`NqlSDK.vcxproj` copies runtime assets beside the executable after a successful build:

- `Shaders/*.hlsl` -> `<OutDir>/Shaders/`
- `TexturePacks/` -> `<OutDir>/TexturePacks/`

That copied asset layout is part of the current runtime contract. The renderer expects to find the HLSL files at launch because shaders are compiled from disk rather than embedded.

## Build Steps

1. Open `NqlSDK.sln`.
2. Select `Debug | x64` or `Release | x64`.
3. Build the solution.
4. Launch `NqlSDK` or `WorldgenDebugger` from Visual Studio, or run the binaries directly from the matching `x64` output directory.

## Runtime Files And Working Directory

The SDK creates and updates data files relative to the current working directory, not relative to the executable:

- `settings.json`
- `controls.cfg`
- `servers.json`
- `WorldSaves/`
- `MapCache/`

That means launch location matters when you run the executable outside Visual Studio.

## Source Layout

```text
SDK/
  App/
  Build/
  Core/
  Debugging/
  Docs/
  Platform/Win32/
  Renderer/
  Shaders/
  Tests/
  TexturePacks/
```

Practical ownership:

- `App/`: entry point only
- `Core/`: API, session, frontend, world, persistence, runtime services
- `Platform/Win32/`: window and input shell
- `Renderer/`: D3D12 device, frame, HUD, chunk draw path
- `Debugging/`: standalone worldgen debugger and analysis artifacts
- `Tests/`: standalone benchmark sources and notes

## Known Build And Packaging Risks

- Shader compilation is runtime-only. Missing `Shaders/` beside `NqlSDK.exe` will break startup.
- `d3d12_pipeline.cpp` compiles shaders with debug/no-opt flags in every build configuration, so Release is still paying startup compile cost.
- The executable writes runtime data into the working directory. That is convenient for local development but fragile for packaging and automation.
- `Core/Benchmark/sdk_benchmark.c` is compiled into the main app, so benchmark-only code is not fully separated from shipping runtime code.

---

## Related Documentation

### Up to Root
- [SDK Overview](../SDK_Overview.md) - Documentation home

### App & Entry
- [../App/SDK_AppOverview.md](../App/SDK_AppOverview.md) - App entry and launch

### Core Systems
- [../Core/SDK_CoreOverview.md](../Core/SDK_CoreOverview.md) - Core runtime overview
- [../Core/Settings/SDK_SettingsAndControls.md](../Core/Settings/SDK_SettingsAndControls.md) - Settings and controls
- [../Core/Runtime/SDK_RuntimeDiagnostics.md](../Core/Runtime/SDK_RuntimeDiagnostics.md) - Runtime diagnostics
- [../Core/Benchmark/SDK_Benchmarking.md](../Core/Benchmark/SDK_Benchmarking.md) - In-app benchmarks

### Assets & Rendering
- [SDK_ContentToolsAndAssetLibraries.md](SDK_ContentToolsAndAssetLibraries.md) - Content tools
- [../Shaders/SDK_Shaders.md](../Shaders/SDK_Shaders.md) - Shader system
- [../Textures/TexturePackSpec.md](../Textures/TexturePackSpec.md) - Texture packs

### Platform & Tests
- [../Platform/SDK_PlatformWin32.md](../Platform/SDK_PlatformWin32.md) - Win32 platform
- [../Tests/SDK_TestsAndBenchmarks.md](../Tests/SDK_TestsAndBenchmarks.md) - Testing framework
- [../Debugging/SDK_Debugging.md](../Debugging/SDK_Debugging.md) - Debugging tools

---
*Documentation for `SDK/Build/`*
