/**
 * sdk_window.h — Win32 window creation and message pump
 *
 * Pure C interface. Implementation in sdk_window.c.
 */
#ifndef NQLSDK_WINDOW_H
#define NQLSDK_WINDOW_H

#include "../../Core/sdk_types.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque window handle. */
typedef struct SdkWindow SdkWindow;

/** Callback invoked when the window is resized. */
typedef void (*SdkResizeCallback)(uint32_t width, uint32_t height, void* user_data);

/** Create and show a Win32 window. Returns NULL on failure. */
SdkWindow* sdk_window_create(const SdkWindowDesc* desc);

/** Destroy the window and free resources. */
void sdk_window_destroy(SdkWindow* win);

/** Pump Win32 messages. Returns false when WM_QUIT is received. */
bool sdk_window_pump(SdkWindow* win);

/** Get the native HWND. */
HWND sdk_window_hwnd(const SdkWindow* win);

/** Get current client area dimensions. */
void sdk_window_size(const SdkWindow* win, uint32_t* out_w, uint32_t* out_h);

/** Register a resize callback. */
void sdk_window_set_resize_callback(SdkWindow* win, SdkResizeCallback cb, void* user_data);

/** Resize the window client area immediately. */
void sdk_window_set_client_size(SdkWindow* win, uint32_t width, uint32_t height);
void sdk_window_enter_windowed(SdkWindow* win, uint32_t width, uint32_t height);
void sdk_window_enter_borderless(SdkWindow* win);
void sdk_window_get_monitor_rect(const SdkWindow* win, RECT* out_rect);

/** Request an orderly close of the window via WM_CLOSE. */
void sdk_window_request_close(SdkWindow* win);

/** Check if a resize event occurred since last call (and clear the flag). */
bool sdk_window_was_resized(SdkWindow* win);

/** Check if a key is currently pressed (VK codes: 'W'=0x57, 'A'=0x41, etc.) */
bool sdk_window_is_key_down(SdkWindow* win, uint8_t vk_code);

/** Check if a mouse button is currently held (0=left, 1=right, 2=middle). */
bool sdk_window_is_mouse_down(SdkWindow* win, int button);

/** Check if a mouse button was just pressed this frame (auto-clears). */
bool sdk_window_was_mouse_pressed(SdkWindow* win, int button);

/** Get current mouse position in client coordinates. */
void sdk_window_get_mouse_pos(SdkWindow* win, int32_t* out_x, int32_t* out_y);

/** Pop the next queued WM_CHAR character. Returns false if no characters are queued. */
bool sdk_window_pop_char(SdkWindow* win, uint32_t* out_ch);

/** Clear any queued WM_CHAR text input. */
void sdk_window_clear_char_queue(SdkWindow* win);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_WINDOW_H */
