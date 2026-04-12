#ifndef SDK_RUNTIME_HOST_H
#define SDK_RUNTIME_HOST_H

#include "../sdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*SdkRuntimeHostFrameCallback)(void* user_data);

typedef struct {
    const char* title;
    uint32_t width;
    uint32_t height;
    bool resizable;
    bool enable_debug;
    bool load_graphics_settings;
    bool use_saved_vsync;
    bool vsync;
    SdkColor4 clear_color;
    SdkRuntimeHostFrameCallback pre_frame;
    SdkRuntimeHostFrameCallback post_frame;
    void* user_data;
} SdkRuntimeHostDesc;

int sdk_runtime_host_run(const SdkRuntimeHostDesc* desc);

#ifdef __cplusplus
}
#endif

#endif
