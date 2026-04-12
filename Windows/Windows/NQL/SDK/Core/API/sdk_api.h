/**
 * sdk_api.h — NQL SDK public C API
 *
 * This is the primary interface for initialising, running, and shutting down
 * the NQL rendering SDK. All functions use C linkage so NQL (pure C) can
 * call them directly.
 */
#ifndef NQLSDK_API_H
#define NQLSDK_API_H

#include "../sdk_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the SDK: create window, D3D12 device, swap chain, pipeline.
 * @param desc  Initialisation parameters. NULL → all defaults.
 * @return SDK_OK on success.
 */
SdkResult nqlsdk_init(const SdkInitDesc* desc);

/**
 * Run one frame: pump window messages, render, present.
 * @return SDK_OK while running, SDK_ERR_NOT_INIT if not initialised.
 *         Returns SDK_ERR_GENERIC if the window was closed (app should exit).
 */
SdkResult nqlsdk_frame(void);

/**
 * Shut down the SDK: release D3D12 resources, destroy window.
 */
void nqlsdk_shutdown(void);

/**
 * Query whether the SDK is currently initialised and running.
 */
bool nqlsdk_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_API_H */
