<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > App

---

# SDK App Entry And Launch

## Scope

This page covers the tiny application layer under `SDK/App/`.

Current source:

- `App/main.cpp`

## How It Works

The app layer does not own gameplay logic. It is a Windows subsystem entry point that performs just enough work to bootstrap the shared cache and hand control to the public SDK API.

Current launch flow:

```text
WinMain
  -> sdk_worldgen_shared_cache_init()
  -> sdk_graphics_settings_default/load()
  -> populate SdkInitDesc
  -> nqlsdk_init(&desc)
  -> while (nqlsdk_frame() == SDK_OK) { }
  -> nqlsdk_shutdown()
  -> sdk_worldgen_shared_cache_shutdown()
```

The most important detail is that the app layer owns `sdk_worldgen_shared_cache_init()` and `sdk_worldgen_shared_cache_shutdown()` directly. That cache is therefore not fully encapsulated inside `nqlsdk_init()` / `nqlsdk_shutdown()`.

## Runtime Defaults

`main.cpp` currently hardcodes these startup defaults:

- title: `NQL SDK - D3D12`
- window size: `800x600`
- resizable window
- debug layer enabled
- clear color: dark gray
- vsync loaded from graphics settings

## What This Layer Does Not Do

- no command-line parsing
- no profile selection
- no runtime mode switching
- no special packaging/bootstrap logic

All real runtime ownership starts after `nqlsdk_init()` inside `Core/API/`.

## Known Issues And Design Flaws

- The file header still describes a colored-triangle demo, but the executable now boots the full SDK runtime. The comment is stale.
- The debug layer is enabled unconditionally in the entry point instead of being selected by configuration or build type.
- Startup cache ownership is split. `WinMain` manages the shared worldgen cache while the rest of startup is owned by `nqlsdk_init()`.
- There is no command-line or headless mode. Every launch follows the same interactive windowed path.

---

## Related Documentation

### Up to Root
- [SDK Overview](../SDK_Overview.md) - Documentation home

### Core Systems
- [../Core/API/SDK_APIReference.md](../Core/API/SDK_APIReference.md) - Public API surface
- [../Core/API/Session/SDK_RuntimeSessionAndFrontend.md](../Core/API/Session/SDK_RuntimeSessionAndFrontend.md) - Session/bootstrap flow
- [../Core/SDK_CoreOverview.md](../Core/SDK_CoreOverview.md) - Core subsystems hub

### Build & Platform
- [../Build/SDK_BuildGuide.md](../Build/SDK_BuildGuide.md) - Build and solution layout
- [../Platform/SDK_PlatformWin32.md](../Platform/SDK_PlatformWin32.md) - Win32 platform layer

### Development
- [../Debugging/SDK_Debugging.md](../Debugging/SDK_Debugging.md) - Debugging tools
- [../Tests/SDK_TestsAndBenchmarks.md](../Tests/SDK_TestsAndBenchmarks.md) - Testing

---
*Documentation for `SDK/App/`*
