<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../SDK_Overview.md) > [Core](../SDK_CoreOverview.md) > API

---

# NQL SDK - API Reference

This document covers the public C API defined in `Core/API/sdk_api.h` and `Core/sdk_types.h`.

It does not attempt to document the internal runtime architecture. For internal session, residency, renderer, worldgen, persistence, and diagnostics behavior, use the maintainer docs linked from [SDK_Overview.md](../../SDK_Overview.md).

## Lifecycle Functions

### `nqlsdk_init`

```c
SdkResult nqlsdk_init(const SdkInitDesc* desc);
```

Initializes the SDK runtime.

- Creates the window.
- Initializes the renderer.
- Loads graphics and input settings.
- Resets frontend/runtime state.

`desc` may be `NULL`, in which case the SDK applies defaults and uses persisted graphics settings where appropriate.

### `nqlsdk_frame`

```c
SdkResult nqlsdk_frame(void);
```

Runs one frame of the SDK.

Returns:

- `SDK_OK` while the runtime is still running
- `SDK_ERR_GENERIC` if the window is closed and the caller should exit
- `SDK_ERR_NOT_INIT` if called before successful initialization

### `nqlsdk_shutdown`

```c
void nqlsdk_shutdown(void);
```

Shuts down the current world session if one is active, saves settings, releases renderer resources, and destroys the window.

### `nqlsdk_is_running`

```c
bool nqlsdk_is_running(void);
```

Returns `true` while the SDK has been initialized and not yet shut down.

## Result Codes

`SdkResult` is the shared public return-code enum.

| Value | Code | Meaning |
|---|---:|---|
| `SDK_OK` | 0 | Success |
| `SDK_ERR_GENERIC` | -1 | General failure or closed runtime |
| `SDK_ERR_INVALID_ARG` | -2 | Invalid parameter |
| `SDK_ERR_OUT_OF_MEMORY` | -3 | Allocation failure |
| `SDK_ERR_WINDOW_FAILED` | -4 | Win32 window creation failed |
| `SDK_ERR_DEVICE_FAILED` | -5 | D3D12 device creation failed |
| `SDK_ERR_SWAPCHAIN_FAILED` | -6 | Swap-chain creation failed |
| `SDK_ERR_PIPELINE_FAILED` | -7 | Renderer pipeline initialization failed |
| `SDK_ERR_SHADER_FAILED` | -8 | Shader compilation failed |
| `SDK_ERR_ALREADY_INIT` | -9 | `nqlsdk_init` called twice |
| `SDK_ERR_NOT_INIT` | -10 | Runtime function called before init |

## Public Descriptors

### `SdkWindowDesc`

```c
typedef struct SdkWindowDesc {
    const char* title;
    uint32_t    width;
    uint32_t    height;
    bool        resizable;
} SdkWindowDesc;
```

Public meaning:

- `title`
  - UTF-8 window title; `NULL` means use the SDK default title
- `width`, `height`
  - requested client-area size; `0` means use defaults
- `resizable`
  - whether the user can resize the window

### `SdkWorldDesc`

```c
typedef struct SdkWorldDesc {
    uint32_t seed;
    int16_t  sea_level;
    uint16_t macro_cell_size;
} SdkWorldDesc;
```

Public meaning:

- `seed`
  - `0` means use the SDK default deterministic seed
- `sea_level`
  - `0` means worldgen chooses the default sea level
- `macro_cell_size`
  - `0` means use the default macro-cell size

### `SdkInitDesc`

```c
typedef struct SdkInitDesc {
    SdkWindowDesc window;
    SdkColor4     clear_color;
    bool          enable_debug;
    bool          vsync;
    SdkWorldDesc  world;
} SdkInitDesc;
```

Public meaning:

- `window`
  - top-level window settings
- `clear_color`
  - default background clear color; all-zero means the runtime substitutes its default dark clear color
- `enable_debug`
  - enables the D3D12 debug layer when available
- `vsync`
  - present with vsync; the runtime may also seed this from persisted graphics settings
- `world`
  - initial world descriptor used when a world session is created

## Shared Math Types

The public shared types are declared in `sdk_types.h`.

| Type | Fields |
|---|---|
| `SdkVec2` | `x, y` |
| `SdkVec3` | `x, y, z` |
| `SdkVec4` | `x, y, z, w` |
| `SdkMat4` | `m[4][4]` |
| `SdkColor4` | `r, g, b, a` |
| `SdkVertex` | `position`, `color` |

## Minimal Example

```c
#include "Core/sdk_api.h"

int main(void) {
    SdkInitDesc desc = {0};
    desc.window.width = 1280;
    desc.window.height = 720;
    desc.enable_debug = true;
    desc.vsync = true;
    desc.world.seed = 0x12345678u;

    if (nqlsdk_init(&desc) != SDK_OK) {
        return 1;
    }

    while (nqlsdk_frame() == SDK_OK) {
    }

    nqlsdk_shutdown();
    return 0;
}
```

---

## Related Documentation

### Up to Parent
- [SDK Core Overview](../SDK_CoreOverview.md) - Core subsystems hub
- [SDK Overview](../../SDK_Overview.md) - Documentation root

### Session & Frontend
- [Session/SDK_RuntimeSessionAndFrontend.md](Session/SDK_RuntimeSessionAndFrontend.md) - Session bootstrap flow
- [../Frontend/SDK_Frontend.md](../Frontend/SDK_Frontend.md) - Frontend UI flow

### World Systems
- [../World/SDK_WorldOverview.md](../World/SDK_WorldOverview.md) - World architecture
- [../World/Chunks/SDK_Chunks.md](../World/Chunks/SDK_Chunks.md) - Chunk data
- [../World/Chunks/ChunkManager/SDK_ChunkManager.md](../World/Chunks/ChunkManager/SDK_ChunkManager.md) - Residency

### Settings & Platform
- [../Settings/SDK_SettingsAndControls.md](../Settings/SDK_SettingsAndControls.md) - Settings system
- [../../Platform/SDK_PlatformWin32.md](../../Platform/SDK_PlatformWin32.md) - Win32 platform

### Entry & Build
- [../../App/SDK_AppOverview.md](../../App/SDK_AppOverview.md) - App entry point
- [../../Build/SDK_BuildGuide.md](../../Build/SDK_BuildGuide.md) - Build guide

---

## Notes

- The public API surface is intentionally small.
- World sessions, chunk streaming, renderer behavior, settlement runtime, simulation, and persistence are all internal subsystems behind this API.
- If a future API addition is needed, prefer documenting it here and the owning runtime subsystem in the maintainer docs rather than expanding this file into internal implementation detail.

---
*Documentation for `SDK/Core/API/`*
