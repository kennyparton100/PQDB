/**
 * d3d12_renderer_resources.cpp -- Swap chain targets and resource lifetime helpers.
 */
#include "d3d12_renderer_internal.h"

SdkResult create_render_targets(void)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
        g_rs.rtv_heap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        HR_CHECK(g_rs.swap_chain->GetBuffer(i, IID_PPV_ARGS(&g_rs.render_targets[i])));
        g_rs.device->CreateRenderTargetView(g_rs.render_targets[i].Get(), nullptr, rtv_handle);
        rtv_handle.ptr += g_rs.rtv_descriptor_size;
    }
    return SDK_OK;
}

SdkResult create_depth_buffer(uint32_t width, uint32_t height)
{
    g_rs.depth_buffer.Reset();

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC dd = {};
    dd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    dd.Width              = width;
    dd.Height             = height;
    dd.DepthOrArraySize   = 1;
    dd.MipLevels          = 1;
    dd.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count   = 1;
    dd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear_val = {};
    clear_val.Format               = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clear_val.DepthStencil.Depth   = 1.0f;
    clear_val.DepthStencil.Stencil = 0;

    HR_CHECK(g_rs.device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE,
        &dd, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear_val, IID_PPV_ARGS(&g_rs.depth_buffer)));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
    dsv_desc.Format        = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    g_rs.device->CreateDepthStencilView(
        g_rs.depth_buffer.Get(), &dsv_desc,
        g_rs.dsv_heap->GetCPUDescriptorHandleForHeapStart());

    return SDK_OK;
}

void wait_for_gpu(void)
{
    /* Signal a new fence value and wait for it */
    UINT64 fv = g_rs.fence_value++;
    HR_CHECK(g_rs.cmd_queue->Signal(g_rs.fence.Get(), fv));
    SdkWaitForFence(g_rs.fence.Get(), fv, g_rs.fence_event);
}

void retire_resource_later(ID3D12Resource* resource)
{
    RendererState::RetiredResource entry = {};
    if (!resource) return;
    if (g_rs.retired_resource_count >= RETIRED_RESOURCE_CAPACITY) {
        wait_for_gpu();
        reclaim_retired_resources();
        if (g_rs.retired_resource_count >= RETIRED_RESOURCE_CAPACITY) {
            resource->Release();
            return;
        }
    }
    entry.resource = resource;
    entry.retire_fence = g_rs.fence_value;
    g_rs.retired_resources[g_rs.retired_resource_count++] = entry;
}

void reclaim_retired_resources(void)
{
    UINT64 completed;
    UINT write_index = 0;
    UINT i;

    if (!g_rs.fence) return;
    if (g_rs.retired_resource_count == 0) return;

    completed = g_rs.fence->GetCompletedValue();
    for (i = 0; i < g_rs.retired_resource_count; ++i) {
        RendererState::RetiredResource entry = g_rs.retired_resources[i];
        if (completed >= entry.retire_fence) {
            if (entry.resource) entry.resource->Release();
        } else {
            if (write_index != i) {
                g_rs.retired_resources[write_index] = entry;
            }
            ++write_index;
        }
    }
    g_rs.retired_resource_count = write_index;
}

