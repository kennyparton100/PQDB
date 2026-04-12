#include "sdk_runtime_host.h"

#include "../API/sdk_api.h"
#include "../Settings/sdk_settings.h"
#include "../World/Worldgen/SharedCache/sdk_worldgen_shared_cache.h"

#include <string.h>

static void sdk_runtime_host_init_defaults(SdkRuntimeHostDesc* desc)
{
    /* Initializes runtime host descriptor with default values */
    if (!desc) return;
    memset(desc, 0, sizeof(*desc));
    desc->title = "NQL SDK - D3D12";
    desc->width = 800;
    desc->height = 600;
    desc->resizable = true;
    desc->enable_debug = true;
    desc->load_graphics_settings = true;
    desc->use_saved_vsync = true;
    desc->vsync = true;
    desc->clear_color.r = 0.1f;
    desc->clear_color.g = 0.1f;
    desc->clear_color.b = 0.12f;
    desc->clear_color.a = 1.0f;
}

int sdk_runtime_host_run(const SdkRuntimeHostDesc* desc)
{
    /* Runs SDK runtime host with given descriptor, manages init/shutdown lifecycle */
    SdkRuntimeHostDesc resolved;
    SdkGraphicsSettings graphics;
    SdkInitDesc init_desc;
    SdkResult init_result;

    sdk_runtime_host_init_defaults(&resolved);
    if (desc) {
        if (desc->title) resolved.title = desc->title;
        if (desc->width > 0u) resolved.width = desc->width;
        if (desc->height > 0u) resolved.height = desc->height;
        resolved.resizable = desc->resizable;
        resolved.enable_debug = desc->enable_debug;
        resolved.load_graphics_settings = desc->load_graphics_settings;
        resolved.use_saved_vsync = desc->use_saved_vsync;
        resolved.vsync = desc->vsync;
        resolved.clear_color = desc->clear_color;
        resolved.pre_frame = desc->pre_frame;
        resolved.post_frame = desc->post_frame;
        resolved.user_data = desc->user_data;
    }

    memset(&graphics, 0, sizeof(graphics));
    memset(&init_desc, 0, sizeof(init_desc));

    sdk_worldgen_shared_cache_init();

    sdk_graphics_settings_default(&graphics);
    if (resolved.load_graphics_settings) {
        sdk_graphics_settings_load(&graphics);
    }

    init_desc.window.title = resolved.title;
    init_desc.window.width = resolved.width;
    init_desc.window.height = resolved.height;
    init_desc.window.resizable = resolved.resizable;
    init_desc.enable_debug = resolved.enable_debug;
    init_desc.vsync = resolved.use_saved_vsync ? graphics.vsync : resolved.vsync;
    init_desc.clear_color = resolved.clear_color;

    init_result = nqlsdk_init(&init_desc);
    if (init_result != SDK_OK) {
        sdk_worldgen_shared_cache_shutdown();
        return 1;
    }

    while (nqlsdk_is_running()) {
        SdkResult frame_result;

        if (resolved.pre_frame && !resolved.pre_frame(resolved.user_data)) {
            break;
        }

        frame_result = nqlsdk_frame();
        if (frame_result != SDK_OK) {
            break;
        }

        if (resolved.post_frame && !resolved.post_frame(resolved.user_data)) {
            break;
        }
    }

    nqlsdk_shutdown();
    sdk_worldgen_shared_cache_shutdown();
    return 0;
}
