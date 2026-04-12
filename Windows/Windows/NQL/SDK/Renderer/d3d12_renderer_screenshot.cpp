#include "d3d12_renderer_internal.h"

static void renderer_store_screenshot_result(int success,
                                             const char* path,
                                             int width,
                                             int height,
                                             const char* failure_reason)
{
    memset(&g_rs.screenshot_result, 0, sizeof(g_rs.screenshot_result));
    g_rs.screenshot_result.completed = 1;
    g_rs.screenshot_result.success = success ? 1 : 0;
    g_rs.screenshot_result.width = width;
    g_rs.screenshot_result.height = height;
    if (path && path[0]) {
        strncpy_s(g_rs.screenshot_result.path,
                  sizeof(g_rs.screenshot_result.path),
                  path,
                  _TRUNCATE);
    }
    if (failure_reason && failure_reason[0]) {
        strncpy_s(g_rs.screenshot_result.failure_reason,
                  sizeof(g_rs.screenshot_result.failure_reason),
                  failure_reason,
                  _TRUNCATE);
    }
    g_rs.screenshot_result_pending = true;
}

static int renderer_write_png_rgba(const char* output_path,
                                   const uint8_t* pixels,
                                   UINT width,
                                   UINT height,
                                   UINT stride)
{
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;

    if (!output_path || !output_path[0] || !pixels || width == 0u || height == 0u || stride == 0u) {
        return 0;
    }

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory,
                                nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return 0;
    }
    if (FAILED(factory->CreateStream(&stream))) return 0;
    if (FAILED(stream->InitializeFromFilename(std::wstring(output_path, output_path + strlen(output_path)).c_str(),
                                              GENERIC_WRITE))) {
        return 0;
    }
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) return 0;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return 0;
    if (FAILED(encoder->CreateNewFrame(&frame, &props))) return 0;
    if (FAILED(frame->Initialize(props.Get()))) return 0;
    if (FAILED(frame->SetSize(width, height))) return 0;
    {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
        if (FAILED(frame->SetPixelFormat(&format))) return 0;
    }
    if (FAILED(frame->WritePixels(height, stride, stride * height, const_cast<BYTE*>(pixels)))) return 0;
    if (FAILED(frame->Commit())) return 0;
    if (FAILED(encoder->Commit())) return 0;
    return 1;
}

SdkResult sdk_renderer_request_screenshot(const char* output_path)
{
    if (!g_rs.initialized || !output_path || !output_path[0]) {
        return SDK_ERR_INVALID_ARG;
    }

    g_rs.screenshot_request_pending = true;
    g_rs.screenshot_capture_pending = false;
    g_rs.screenshot_result_pending = false;
    memset(&g_rs.screenshot_result, 0, sizeof(g_rs.screenshot_result));
    strncpy_s(g_rs.screenshot_request_path,
              sizeof(g_rs.screenshot_request_path),
              output_path,
              _TRUNCATE);
    return SDK_OK;
}

int sdk_renderer_poll_screenshot_result(SdkScreenshotResult* out_result)
{
    if (!g_rs.screenshot_result_pending) return 0;
    if (out_result) {
        *out_result = g_rs.screenshot_result;
    }
    g_rs.screenshot_result_pending = false;
    return 1;
}

bool renderer_record_screenshot_copy(UINT frame_index)
{
    D3D12_RESOURCE_DESC desc;
    D3D12_HEAP_PROPERTIES heap_props = {};
    D3D12_RESOURCE_DESC buffer_desc = {};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT row_count = 0u;
    UINT64 row_bytes = 0u;
    UINT64 total_bytes = 0u;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    D3D12_RESOURCE_BARRIER barrier = {};
    HRESULT hr;

    if (!g_rs.screenshot_request_pending) return false;
    if (!g_rs.render_targets[frame_index]) {
        renderer_store_screenshot_result(0, g_rs.screenshot_request_path, 0, 0,
                                         "render target unavailable");
        g_rs.screenshot_request_pending = false;
        return false;
    }

    desc = g_rs.render_targets[frame_index]->GetDesc();
    g_rs.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &row_count, &row_bytes, &total_bytes);

    heap_props.Type = D3D12_HEAP_TYPE_READBACK;
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total_bytes;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = g_rs.device->CreateCommittedResource(&heap_props,
                                              D3D12_HEAP_FLAG_NONE,
                                              &buffer_desc,
                                              D3D12_RESOURCE_STATE_COPY_DEST,
                                              nullptr,
                                              IID_PPV_ARGS(&g_rs.screenshot_readback));
    if (FAILED(hr)) {
        renderer_store_screenshot_result(0, g_rs.screenshot_request_path,
                                         (int)desc.Width, (int)desc.Height,
                                         "failed to create readback resource");
        g_rs.screenshot_request_pending = false;
        return false;
    }

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = g_rs.render_targets[frame_index].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_rs.cmd_list->ResourceBarrier(1, &barrier);

    src.pResource = g_rs.render_targets[frame_index].Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    dst.pResource = g_rs.screenshot_readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;

    g_rs.cmd_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_rs.cmd_list->ResourceBarrier(1, &barrier);

    g_rs.screenshot_request_pending = false;
    g_rs.screenshot_capture_pending = true;
    g_rs.screenshot_readback_size = total_bytes;
    g_rs.screenshot_row_pitch = footprint.Footprint.RowPitch;
    g_rs.screenshot_width = (UINT)desc.Width;
    g_rs.screenshot_height = desc.Height;
    g_rs.screenshot_format = desc.Format;
    return true;
}

void renderer_finalize_screenshot(UINT64 fence_value)
{
    D3D12_RANGE read_range = { 0, 0 };
    void* mapped = nullptr;
    std::vector<uint8_t> pixels;
    uint8_t* src;
    UINT y;

    if (!g_rs.screenshot_capture_pending || !g_rs.screenshot_readback) return;

    SdkWaitForFence(g_rs.fence.Get(), fence_value, g_rs.fence_event);

    if (FAILED(g_rs.screenshot_readback->Map(0, &read_range, &mapped)) || !mapped) {
        renderer_store_screenshot_result(0, g_rs.screenshot_request_path,
                                         (int)g_rs.screenshot_width,
                                         (int)g_rs.screenshot_height,
                                         "failed to map screenshot readback");
        g_rs.screenshot_readback.Reset();
        g_rs.screenshot_capture_pending = false;
        return;
    }

    if (g_rs.screenshot_format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        g_rs.screenshot_format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        g_rs.screenshot_readback->Unmap(0, nullptr);
        g_rs.screenshot_readback.Reset();
        g_rs.screenshot_capture_pending = false;
        renderer_store_screenshot_result(0, g_rs.screenshot_request_path,
                                         (int)g_rs.screenshot_width,
                                         (int)g_rs.screenshot_height,
                                         "unsupported screenshot surface format");
        return;
    }

    pixels.resize((size_t)g_rs.screenshot_width * (size_t)g_rs.screenshot_height * 4u);
    src = (uint8_t*)mapped;
    for (y = 0; y < g_rs.screenshot_height; ++y) {
        memcpy(pixels.data() + (size_t)y * (size_t)g_rs.screenshot_width * 4u,
               src + (size_t)y * (size_t)g_rs.screenshot_row_pitch,
               (size_t)g_rs.screenshot_width * 4u);
    }

    g_rs.screenshot_readback->Unmap(0, nullptr);
    g_rs.screenshot_readback.Reset();
    g_rs.screenshot_capture_pending = false;

    if (!renderer_write_png_rgba(g_rs.screenshot_request_path,
                                 pixels.data(),
                                 g_rs.screenshot_width,
                                 g_rs.screenshot_height,
                                 g_rs.screenshot_width * 4u)) {
        renderer_store_screenshot_result(0, g_rs.screenshot_request_path,
                                         (int)g_rs.screenshot_width,
                                         (int)g_rs.screenshot_height,
                                         "failed to encode screenshot PNG");
        return;
    }

    renderer_store_screenshot_result(1, g_rs.screenshot_request_path,
                                     (int)g_rs.screenshot_width,
                                     (int)g_rs.screenshot_height,
                                     "");
}
