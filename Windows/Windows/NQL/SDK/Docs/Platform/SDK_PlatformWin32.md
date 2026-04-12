<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../SDK_Overview.md) > Platform

---

# SDK Platform Layer: Win32

## Scope

This page documents the current platform shell under `SDK/Platform/Win32/`.

Source:

- `Platform/Win32/sdk_window.h`
- `Platform/Win32/sdk_window.c`

## Responsibilities

The Win32 platform layer owns:

- window creation and destruction
- the message pump
- resize notifications
- borderless/windowed mode switching
- keyboard state
- mouse button state
- simple mouse position tracking
- a small `WM_CHAR` queue

It does not own rendering, gameplay input mapping, or higher-level UI state. It only exposes raw-ish platform state for the rest of the SDK.

## How It Works

The platform layer wraps one `SdkWindow` object around:

- `HWND`
- cached width and height
- borderless/windowed restore state
- key and mouse arrays
- a fixed-size text-input queue

Current flow:

```text
sdk_window_create
  -> enable DPI awareness once
  -> register window class
  -> create Win32 window
  -> allocate SdkWindow
  -> show/focus window

sdk_window_pump
  -> PeekMessage loop
  -> TranslateMessage / DispatchMessage
  -> stop on WM_QUIT
```

`sdk_wndproc()` updates the cached state for:

- `WM_SIZE`
- `WM_CLOSE`
- `WM_DESTROY`
- `WM_KEYDOWN` / `WM_KEYUP`
- `WM_CHAR`
- mouse button and mouse move events

## Known Issues And Design Flaws

- The implementation is effectively single-window only. `sdk_window.c` uses one global `g_sdk_window`, so `WndProc` instance routing is not generic.
- The layer is Win32-only. There is no platform abstraction boundary beyond the C wrapper itself.
- Input handling is intentionally simple: no raw input, no high-resolution mouse deltas, no IME-aware text model, and only a small fixed-size char queue.
- The `WM_CHAR` queue silently stops accepting input once it is full.
- Focus management is aggressive. The code repeatedly calls `SetActiveWindow`, `SetForegroundWindow`, and `SetFocus`, which is practical for a local tool but not ideal as a reusable platform layer.

---

## Related Documentation

### Up to Root
- [SDK Overview](../SDK_Overview.md) - Documentation home

### Core Systems
- [../Core/Frontend/SDK_Frontend.md](../Core/Frontend/SDK_Frontend.md) - Frontend UI
- [../Core/Input/SDK_Input.md](../Core/Input/SDK_Input.md) - Input system
- [../Core/API/SDK_APIReference.md](../Core/API/SDK_APIReference.md) - Public API

### App & Build
- [../App/SDK_AppOverview.md](../App/SDK_AppOverview.md) - Entry point
- [../Build/SDK_BuildGuide.md](../Build/SDK_BuildGuide.md) - Build guide

---
*Documentation for `SDK/Platform/Win32/`*
