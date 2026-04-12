/**
 * main.cpp — NQL SDK demo entry point
 *
 * Creates a window, initialises D3D12, and renders a coloured triangle
 * until the user closes the window.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "../Core/Runtime/sdk_runtime_host.h"

int WINAPI WinMain(
    _In_ HINSTANCE /*hInstance*/,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPSTR /*lpCmdLine*/,
    _In_ int /*nCmdShow*/)
{
    SdkRuntimeHostDesc host = {0};

    host.title = "NQL SDK - D3D12";
    host.width = 800;
    host.height = 600;
    host.resizable = true;
    host.enable_debug = true;
    host.load_graphics_settings = true;
    host.use_saved_vsync = true;
    host.clear_color.r = 0.1f;
    host.clear_color.g = 0.1f;
    host.clear_color.b = 0.12f;
    host.clear_color.a = 1.0f;

    return sdk_runtime_host_run(&host);
}
