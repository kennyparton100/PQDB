/**
 * sdk_window.c — Win32 window implementation
 *
 * Creates a standard overlapped window with WS_OVERLAPPEDWINDOW style,
 * handles WM_SIZE for resize tracking, and provides a message pump.
 */
#include "sdk_window.h"
#include "../../Core/API/Internal/sdk_load_trace.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

/* ======================================================================
 * INTERNAL STATE
 * ====================================================================== */

struct SdkWindow {
    HWND                hwnd;
    uint32_t            width;
    uint32_t            height;
    DWORD               windowed_style;
    DWORD               windowed_ex_style;
    RECT                windowed_rect;
    bool                borderless;
    bool                should_close;
    bool                was_resized;
    SdkResizeCallback   resize_cb;
    void*               resize_user_data;
    /* Keyboard state for WASD and arrow keys */
    bool                keys[256];  /* VK codes 0-255 */
    /* Mouse button state: [0]=left, [1]=right, [2]=middle */
    bool                mouse_down[3];
    bool                mouse_pressed[3]; /* "just pressed" flags, cleared on read */
    int32_t             mouse_x;            /* Current mouse X position */
    int32_t             mouse_y;            /* Current mouse Y position */
    uint16_t            char_queue[128];
    int                 char_head;
    int                 char_count;
};

/** Global pointer so WndProc can reach the SdkWindow (single-window SDK). */
static SdkWindow* g_sdk_window = NULL;

static const wchar_t SDK_WNDCLASS_NAME[] = L"NqlSdkWindowClass";

static void sdk_enable_dpi_awareness_once(void)
{
    static bool attempted = false;
    HMODULE user32_module;

    if (attempted) return;
    attempted = true;

    user32_module = GetModuleHandleW(L"user32.dll");
    if (user32_module) {
        typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(HANDLE);
        SetProcessDpiAwarenessContextFn set_process_dpi_awareness_context =
            (SetProcessDpiAwarenessContextFn)GetProcAddress(user32_module, "SetProcessDpiAwarenessContext");
        if (set_process_dpi_awareness_context) {
            if (set_process_dpi_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                return;
            }
        }
    }

    if (user32_module) {
        typedef BOOL (WINAPI *SetProcessDPIAwareFn)(void);
        SetProcessDPIAwareFn set_process_dpi_aware =
            (SetProcessDPIAwareFn)GetProcAddress(user32_module, "SetProcessDPIAware");
        if (set_process_dpi_aware) {
            set_process_dpi_aware();
        }
    }
}

static void sdk_monitor_rect_for_window(HWND hwnd, RECT* out_rect)
{
    HMONITOR monitor;
    MONITORINFO monitor_info;

    if (!out_rect) return;
    memset(out_rect, 0, sizeof(*out_rect));
    monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    memset(&monitor_info, 0, sizeof(monitor_info));
    monitor_info.cbSize = sizeof(monitor_info);
    if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
        *out_rect = monitor_info.rcMonitor;
        return;
    }
    out_rect->left = 0;
    out_rect->top = 0;
    out_rect->right = GetSystemMetrics(SM_CXSCREEN);
    out_rect->bottom = GetSystemMetrics(SM_CYSCREEN);
}

static void sdk_set_modifier_state(SdkWindow* win, WPARAM wp, bool down)
{
    if (!win) return;
    if (wp < 256) {
        win->keys[wp] = down;
    }
    if (wp == VK_SHIFT || wp == VK_LSHIFT || wp == VK_RSHIFT) {
        win->keys[VK_SHIFT] = down;
        win->keys[VK_LSHIFT] = down;
        win->keys[VK_RSHIFT] = down;
    } else if (wp == VK_CONTROL || wp == VK_LCONTROL || wp == VK_RCONTROL) {
        win->keys[VK_CONTROL] = down;
        win->keys[VK_LCONTROL] = down;
        win->keys[VK_RCONTROL] = down;
    } else if (wp == VK_MENU || wp == VK_LMENU || wp == VK_RMENU) {
        win->keys[VK_MENU] = down;
        win->keys[VK_LMENU] = down;
        win->keys[VK_RMENU] = down;
    }
}

/* ======================================================================
 * WNDPROC
 * ====================================================================== */

static LRESULT CALLBACK sdk_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SdkWindow* win = g_sdk_window;

    switch (msg) {
    case WM_SIZE:
        if (win) {
            if (wp == SIZE_MINIMIZED) {
                /* Window minimized - skip resize handling */
            } else if (wp == SIZE_MAXIMIZED || wp == SIZE_RESTORED) {
                win->width  = (uint32_t)LOWORD(lp);
                win->height = (uint32_t)HIWORD(lp);
                win->was_resized = true;
                if (win->resize_cb) {
                    win->resize_cb(win->width, win->height, win->resize_user_data);
                }
            }
        }
        return 0;

    case WM_CLOSE:
        sdk_load_trace_note_state("wm_close",
                                  sdk_load_trace_frontend_view(),
                                  sdk_load_trace_world_session_active(),
                                  sdk_load_trace_world_generation_active(),
                                  sdk_load_trace_world_generation_stage(),
                                  "WM_CLOSE");
        if (win) win->should_close = true;
        DestroyWindow(hwnd);  /* Actually destroy the window */
        return 0;

    case WM_DESTROY:
        sdk_load_trace_note_state("wm_destroy",
                                  sdk_load_trace_frontend_view(),
                                  sdk_load_trace_world_session_active(),
                                  sdk_load_trace_world_generation_active(),
                                  sdk_load_trace_world_generation_stage(),
                                  "WM_DESTROY");
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        sdk_set_modifier_state(win, wp, true);
        /* ESC to close */
        if (wp == VK_ESCAPE) {
            sdk_load_trace_note_state("escape_close_request",
                                      sdk_load_trace_frontend_view(),
                                      sdk_load_trace_world_session_active(),
                                      sdk_load_trace_world_generation_active(),
                                      sdk_load_trace_world_generation_stage(),
                                      "VK_ESCAPE");
            if (win) win->should_close = true;
            PostQuitMessage(0);
        }
        return 0;
        
    case WM_KEYUP:
        sdk_set_modifier_state(win, wp, false);
        return 0;

    case WM_KILLFOCUS:
        if (win) {
            memset(win->keys, 0, sizeof(win->keys));
            memset(win->mouse_down, 0, sizeof(win->mouse_down));
            memset(win->mouse_pressed, 0, sizeof(win->mouse_pressed));
        }
        return 0;

    case WM_CHAR:
        if (win && win->char_count < (int)(sizeof(win->char_queue) / sizeof(win->char_queue[0]))) {
            int tail = (win->char_head + win->char_count) % (int)(sizeof(win->char_queue) / sizeof(win->char_queue[0]));
            win->char_queue[tail] = (uint16_t)wp;
            win->char_count++;
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (win) { win->mouse_down[0] = true; win->mouse_pressed[0] = true; }
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        if (win) win->mouse_down[0] = false;
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        if (win) { win->mouse_down[1] = true; win->mouse_pressed[1] = true; }
        return 0;
    case WM_RBUTTONUP:
        if (win) win->mouse_down[1] = false;
        return 0;
    case WM_MBUTTONDOWN:
        if (win) { win->mouse_down[2] = true; win->mouse_pressed[2] = true; }
        return 0;
    case WM_MOUSEMOVE:
        if (win) {
            win->mouse_x = (int32_t)LOWORD(lp);
            win->mouse_y = (int32_t)HIWORD(lp);
        }
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ======================================================================
 * PUBLIC API
 * ====================================================================== */

SdkWindow* sdk_window_create(const SdkWindowDesc* desc)
{
    if (!desc) return NULL;

    sdk_enable_dpi_awareness_once();

    /* Defaults */
    uint32_t w = desc->width  ? desc->width  : 800;
    uint32_t h = desc->height ? desc->height : 600;
    const char* title = desc->title ? desc->title : "NQL SDK";

    /* Register window class (idempotent — returns 0 if already registered) */
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = sdk_wndproc;
    wc.hInstance      = GetModuleHandleW(NULL);
    wc.hCursor        = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName  = SDK_WNDCLASS_NAME;
    RegisterClassExW(&wc);

    /* Compute window rect for desired client size */
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!desc->resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    RECT rc = { 0, 0, (LONG)w, (LONG)h };
    AdjustWindowRect(&rc, style, FALSE);

    /* Convert title to wide string */
    wchar_t wtitle[256];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256);

    HWND hwnd = CreateWindowExW(
        0, SDK_WNDCLASS_NAME, wtitle, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, GetModuleHandleW(NULL), NULL);

    if (!hwnd) return NULL;

    SdkWindow* win = (SdkWindow*)calloc(1, sizeof(SdkWindow));
    if (!win) { DestroyWindow(hwnd); return NULL; }

    win->hwnd   = hwnd;
    win->width  = w;
    win->height = h;
    win->windowed_style = style;
    win->windowed_ex_style = 0;
    GetWindowRect(hwnd, &win->windowed_rect);
    win->borderless = false;
    g_sdk_window = win;

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    
    /* Ensure window can receive focus and clicks */
    SetActiveWindow(hwnd);
    SetForegroundWindow(hwnd);
    
    return win;
}

void sdk_window_destroy(SdkWindow* win)
{
    if (!win) return;
    if (win->hwnd) DestroyWindow(win->hwnd);
    if (g_sdk_window == win) g_sdk_window = NULL;
    free(win);
}

bool sdk_window_pump(SdkWindow* win)
{
    if (!win) return false;
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            win->should_close = true;
            sdk_load_trace_note_state("wm_quit",
                                      sdk_load_trace_frontend_view(),
                                      sdk_load_trace_world_session_active(),
                                      sdk_load_trace_world_generation_active(),
                                      sdk_load_trace_world_generation_stage(),
                                      "WM_QUIT");
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (win->should_close) {
        sdk_load_trace_note_state("window_should_close",
                                  sdk_load_trace_frontend_view(),
                                  sdk_load_trace_world_session_active(),
                                  sdk_load_trace_world_generation_active(),
                                  sdk_load_trace_world_generation_stage(),
                                  "SdkWindow.should_close");
    }
    return !win->should_close;
}

HWND sdk_window_hwnd(const SdkWindow* win)
{
    return win ? win->hwnd : NULL;
}

void sdk_window_size(const SdkWindow* win, uint32_t* out_w, uint32_t* out_h)
{
    if (!win) return;
    if (out_w) *out_w = win->width;
    if (out_h) *out_h = win->height;
}

void sdk_window_set_resize_callback(SdkWindow* win, SdkResizeCallback cb, void* user_data)
{
    if (!win) return;
    win->resize_cb = cb;
    win->resize_user_data = user_data;
}

void sdk_window_set_client_size(SdkWindow* win, uint32_t width, uint32_t height)
{
    RECT rc;
    DWORD style;
    DWORD ex_style;

    if (!win || !win->hwnd || width == 0 || height == 0) return;

    style = (DWORD)GetWindowLongPtrW(win->hwnd, GWL_STYLE);
    ex_style = (DWORD)GetWindowLongPtrW(win->hwnd, GWL_EXSTYLE);
    rc.left = 0;
    rc.top = 0;
    rc.right = (LONG)width;
    rc.bottom = (LONG)height;
    AdjustWindowRectEx(&rc, style, FALSE, ex_style);

    SetWindowPos(
        win->hwnd,
        NULL,
        0,
        0,
        rc.right - rc.left,
        rc.bottom - rc.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);

    ShowWindow(win->hwnd, SW_SHOW);
    SetActiveWindow(win->hwnd);
    SetForegroundWindow(win->hwnd);
    SetFocus(win->hwnd);
}

void sdk_window_enter_windowed(SdkWindow* win, uint32_t width, uint32_t height)
{
    RECT rc;
    int window_x;
    int window_y;

    if (!win || !win->hwnd || width == 0 || height == 0) return;

    if (!win->borderless) {
        GetWindowRect(win->hwnd, &win->windowed_rect);
    }

    SetWindowLongPtrW(win->hwnd, GWL_STYLE, (LONG_PTR)win->windowed_style);
    SetWindowLongPtrW(win->hwnd, GWL_EXSTYLE, (LONG_PTR)win->windowed_ex_style);

    rc.left = 0;
    rc.top = 0;
    rc.right = (LONG)width;
    rc.bottom = (LONG)height;
    AdjustWindowRectEx(&rc, win->windowed_style, FALSE, win->windowed_ex_style);

    window_x = win->windowed_rect.left;
    window_y = win->windowed_rect.top;
    if (window_x < -32000 || window_y < -32000) {
        window_x = CW_USEDEFAULT;
        window_y = CW_USEDEFAULT;
    }

    SetWindowPos(
        win->hwnd,
        NULL,
        window_x,
        window_y,
        rc.right - rc.left,
        rc.bottom - rc.top,
        SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

    ShowWindow(win->hwnd, SW_SHOWNORMAL);
    BringWindowToTop(win->hwnd);
    SetActiveWindow(win->hwnd);
    SetForegroundWindow(win->hwnd);
    SetFocus(win->hwnd);

    win->borderless = false;
}

void sdk_window_enter_borderless(SdkWindow* win)
{
    RECT monitor_rect;
    DWORD borderless_style;
    DWORD borderless_ex_style;

    if (!win || !win->hwnd) return;

    if (!win->borderless) {
        GetWindowRect(win->hwnd, &win->windowed_rect);
        win->windowed_style = (DWORD)GetWindowLongPtrW(win->hwnd, GWL_STYLE);
        win->windowed_ex_style = (DWORD)GetWindowLongPtrW(win->hwnd, GWL_EXSTYLE);
    }

    sdk_monitor_rect_for_window(win->hwnd, &monitor_rect);
    borderless_style = (win->windowed_style & ~(WS_OVERLAPPEDWINDOW)) | WS_POPUP | WS_VISIBLE;
    borderless_ex_style = (win->windowed_ex_style & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW;
    SetWindowLongPtrW(win->hwnd, GWL_STYLE, (LONG_PTR)borderless_style);
    SetWindowLongPtrW(win->hwnd, GWL_EXSTYLE, (LONG_PTR)borderless_ex_style);
    SetWindowPos(
        win->hwnd,
        HWND_TOP,
        monitor_rect.left,
        monitor_rect.top,
        monitor_rect.right - monitor_rect.left,
        monitor_rect.bottom - monitor_rect.top,
        SWP_SHOWWINDOW | SWP_FRAMECHANGED);

    ShowWindow(win->hwnd, SW_SHOW);
    BringWindowToTop(win->hwnd);
    SetActiveWindow(win->hwnd);
    SetForegroundWindow(win->hwnd);
    SetFocus(win->hwnd);

    win->borderless = true;
}

void sdk_window_get_monitor_rect(const SdkWindow* win, RECT* out_rect)
{
    if (!win || !win->hwnd) {
        if (out_rect) {
            out_rect->left = 0;
            out_rect->top = 0;
            out_rect->right = GetSystemMetrics(SM_CXSCREEN);
            out_rect->bottom = GetSystemMetrics(SM_CYSCREEN);
        }
        return;
    }
    sdk_monitor_rect_for_window(win->hwnd, out_rect);
}

void sdk_window_request_close(SdkWindow* win)
{
    if (!win || !win->hwnd) return;
    PostMessageW(win->hwnd, WM_CLOSE, 0u, 0u);
}

bool sdk_window_was_resized(SdkWindow* win)
{
    if (!win) return false;
    bool r = win->was_resized;
    win->was_resized = false;
    return r;
}

bool sdk_window_is_key_down(SdkWindow* win, uint8_t vk_code)
{
    if (!win || vk_code >= 256) return false;
    return win->keys[vk_code];
}

bool sdk_window_is_mouse_down(SdkWindow* win, int button)
{
    if (!win || button < 0 || button > 2) return false;
    return win->mouse_down[button];
}

bool sdk_window_was_mouse_pressed(SdkWindow* win, int button)
{
    if (!win || button < 0 || button > 2) return false;
    bool r = win->mouse_pressed[button];
    win->mouse_pressed[button] = false;
    return r;
}

void sdk_window_get_mouse_pos(SdkWindow* win, int32_t* out_x, int32_t* out_y)
{
    if (!win) return;
    if (out_x) *out_x = win->mouse_x;
    if (out_y) *out_y = win->mouse_y;
}

bool sdk_window_pop_char(SdkWindow* win, uint32_t* out_ch)
{
    if (!win || win->char_count <= 0) return false;
    if (out_ch) *out_ch = (uint32_t)win->char_queue[win->char_head];
    win->char_head = (win->char_head + 1) % (int)(sizeof(win->char_queue) / sizeof(win->char_queue[0]));
    win->char_count--;
    return true;
}

void sdk_window_clear_char_queue(SdkWindow* win)
{
    if (!win) return;
    win->char_head = 0;
    win->char_count = 0;
}
