/**
 * d3d12_helpers.h — D3D12 utility macros and adapter selection
 *
 * C++ header. Provides HR_CHECK, GetHardwareAdapter, and fence helpers.
 */
#ifndef NQLSDK_D3D12_HELPERS_H
#define NQLSDK_D3D12_HELPERS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../Core/API/Internal/sdk_load_trace.h"

using Microsoft::WRL::ComPtr;

/* ======================================================================
 * HR_CHECK — abort on HRESULT failure (debug builds print file:line)
 * ====================================================================== */

inline void SdkHrCheck(HRESULT hr, const char* expr, const char* file, int line)
{
    if (FAILED(hr)) {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "[NQL SDK] D3D12 HRESULT 0x%08lX failed\n  %s\n  %s:%d\n",
                 (unsigned long)hr, expr, file, line);
        sdk_debug_log_output(buf);
        fprintf(stderr, "%s", buf);
        __debugbreak();
    }
}

#define HR_CHECK(expr) SdkHrCheck((expr), #expr, __FILE__, __LINE__)

/* ======================================================================
 * GetHardwareAdapter — pick the first D3D12-capable GPU
 * ====================================================================== */

inline void SdkGetHardwareAdapter(
    IDXGIFactory4* factory,
    IDXGIAdapter1** out_adapter,
    D3D_FEATURE_LEVEL min_level = D3D_FEATURE_LEVEL_11_0)
{
    *out_adapter = nullptr;
    ComPtr<IDXGIAdapter1> adapter;

    for (UINT i = 0;
         factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        /* Skip software / WARP adapters */
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter.Reset();
            continue;
        }

        /* Check if this adapter supports the minimum feature level */
        if (SUCCEEDED(D3D12CreateDevice(
                adapter.Get(), min_level,
                __uuidof(ID3D12Device), nullptr)))
        {
            *out_adapter = adapter.Detach();
            return;
        }
        adapter.Reset();
    }
}

/* ======================================================================
 * Fence wait helper
 * ====================================================================== */

inline void SdkWaitForFence(
    ID3D12Fence* fence,
    uint64_t     target_value,
    HANDLE       fence_event)
{
    UINT64 completed = fence->GetCompletedValue();
    if (completed < target_value) {
        HR_CHECK(fence->SetEventOnCompletion(target_value, fence_event));
        DWORD wait_result = WaitForSingleObject(fence_event, 5000); /* 5 second timeout */
        if (wait_result == WAIT_TIMEOUT) {
            sdk_debug_log_output("[NQL SDK] WARNING: Fence wait timeout!\n");
            /* Don't hang forever - let the caller handle it */
        }
    }
}

#endif /* NQLSDK_D3D12_HELPERS_H */
