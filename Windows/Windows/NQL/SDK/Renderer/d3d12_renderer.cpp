/**
 * d3d12_renderer.cpp -- D3D12 renderer implementation
 *
 * Creates device, swap chain, command infrastructure, vertex buffer,
 * and drives the per-frame render loop for the triangle demo.
 */
#include "d3d12_renderer_internal.h"

RendererState g_rs;

static void log_display_mode_hresult(const char* step, HRESULT hr)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "[NQL SDK] Display mode step failed: %s (HRESULT 0x%08lX)\n",
             step ? step : "unknown", (unsigned long)hr);
    OutputDebugStringA(buf);
}

extern "C" SdkResult sdk_renderer_init(
    HWND hwnd, uint32_t width, uint32_t height,
    SdkColor4 clear_color, bool enable_debug, bool vsync)
{
    if (g_rs.initialized) return SDK_ERR_ALREADY_INIT;
    memset(&g_rs, 0, sizeof(g_rs));

    g_rs.hwnd   = hwnd;
    g_rs.width  = width;
    g_rs.height = height;
    g_rs.vsync  = vsync;
    g_rs.clear_color[0] = clear_color.r;
    g_rs.clear_color[1] = clear_color.g;
    g_rs.clear_color[2] = clear_color.b;
    g_rs.clear_color[3] = clear_color.a;

    {
        HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(com_hr)) {
            g_rs.com_initialized = true;
        } else if (com_hr != RPC_E_CHANGED_MODE) {
            return SDK_ERR_GENERIC;
        }
    }

    /* --- Debug layer --- */
    if (enable_debug) {
        ComPtr<ID3D12Debug> debug_ctrl;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_ctrl)))) {
            debug_ctrl->EnableDebugLayer();
        }
    }

    /* --- DXGI Factory --- */
    UINT factory_flags = enable_debug ? DXGI_CREATE_FACTORY_DEBUG : 0;
    HR_CHECK(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&g_rs.factory)));

    /* --- Adapter + Device --- */
    ComPtr<IDXGIAdapter1> adapter;
    SdkGetHardwareAdapter(g_rs.factory.Get(), &adapter);
    if (!adapter) return SDK_ERR_DEVICE_FAILED;

    HR_CHECK(D3D12CreateDevice(
        adapter.Get(), D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&g_rs.device)));

    /* --- Command queue --- */
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    HR_CHECK(g_rs.device->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_rs.cmd_queue)));

    /* --- Swap chain --- */
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount  = FRAME_COUNT;
    scd.Width        = width;
    scd.Height       = height;
    scd.Format       = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> sc1;
    HR_CHECK(g_rs.factory->CreateSwapChainForHwnd(
        g_rs.cmd_queue.Get(), hwnd, &scd, nullptr, nullptr, &sc1));
    HR_CHECK(sc1.As(&g_rs.swap_chain));

    /* Disable ALT+ENTER fullscreen toggle */
    g_rs.factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    g_rs.frame_index = g_rs.swap_chain->GetCurrentBackBufferIndex();

    /* --- RTV descriptor heap --- */
    D3D12_DESCRIPTOR_HEAP_DESC rtvhd = {};
    rtvhd.NumDescriptors = FRAME_COUNT;
    rtvhd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HR_CHECK(g_rs.device->CreateDescriptorHeap(&rtvhd, IID_PPV_ARGS(&g_rs.rtv_heap)));
    g_rs.rtv_descriptor_size =
        g_rs.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    /* --- Create render target views --- */
    SdkResult r = create_render_targets();
    if (r != SDK_OK) return r;

    /* --- DSV descriptor heap --- */
    D3D12_DESCRIPTOR_HEAP_DESC dsvhd = {};
    dsvhd.NumDescriptors = 1;
    dsvhd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HR_CHECK(g_rs.device->CreateDescriptorHeap(&dsvhd, IID_PPV_ARGS(&g_rs.dsv_heap)));

    /* --- Create depth buffer --- */
    r = create_depth_buffer(width, height);
    if (r != SDK_OK) return r;

    /* --- Command allocators + command list --- */
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        HR_CHECK(g_rs.device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&g_rs.cmd_alloc[i])));
    }
    HR_CHECK(g_rs.device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        g_rs.cmd_alloc[0].Get(), nullptr,
        IID_PPV_ARGS(&g_rs.cmd_list)));
    HR_CHECK(g_rs.cmd_list->Close());

    /* --- Fence --- */
    HR_CHECK(g_rs.device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&g_rs.fence)));
    g_rs.fence_value = 1;  /* First submission will signal 1 */
    g_rs.fence_values[0] = g_rs.fence_values[1] = 0; /* Nothing submitted yet */
    g_rs.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_rs.fence_event) return SDK_ERR_GENERIC;

    /* --- Pipeline (root sig + PSO) --- */
    r = sdk_pipeline_create(g_rs.device.Get(), &g_rs.root_sig, &g_rs.pso);
    if (r != SDK_OK) return r;
    r = sdk_pipeline_create_water(g_rs.device.Get(), g_rs.root_sig.Get(), &g_rs.water_pso);
    if (r != SDK_OK) return r;

    /* --- HUD pipeline (no depth) --- */
    r = sdk_pipeline_create_hud(g_rs.device.Get(), g_rs.root_sig.Get(), &g_rs.hud_pso);
    if (r != SDK_OK) return r;

    /* --- Block texture array (Minecraft-style texture pack layers) --- */
    r = create_block_texture_array();
    if (r != SDK_OK) return r;

    /* --- HUD constant buffer (ortho MVP, 256-byte aligned, per-frame) --- */
    {
        D3D12_HEAP_PROPERTIES hh = {};
        hh.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC hd = {};
        hd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        hd.Width = 256;
        hd.Height = 1; hd.DepthOrArraySize = 1; hd.MipLevels = 1;
        hd.SampleDesc.Count = 1;
        hd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        for (UINT frame = 0; frame < FRAME_COUNT; ++frame) {
            D3D12_RANGE rr = {0, 0};
            HR_CHECK(g_rs.device->CreateCommittedResource(
                &hh, D3D12_HEAP_FLAG_NONE,
                &hd, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&g_rs.hud_cb[frame])));
            HR_CHECK(g_rs.hud_cb[frame]->Map(0, &rr, (void**)&g_rs.hud_cb_mapped[frame]));
        }
    }
    g_rs.hotbar_dirty = true;

    /* --- Lighting constant buffer (ambient level for day/night, 256-byte aligned, per-frame) --- */
    {
        D3D12_HEAP_PROPERTIES lhp = {};
        lhp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC lrd = {};
        lrd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        lrd.Width = 512;
        lrd.Height = 1; lrd.DepthOrArraySize = 1; lrd.MipLevels = 1;
        lrd.SampleDesc.Count = 1;
        lrd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        for (UINT frame = 0; frame < FRAME_COUNT; ++frame) {
            D3D12_RANGE lrr = {0, 0};
            HR_CHECK(g_rs.device->CreateCommittedResource(
                &lhp, D3D12_HEAP_FLAG_NONE,
                &lrd, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&g_rs.lighting_cb[frame])));
            HR_CHECK(g_rs.lighting_cb[frame]->Map(0, &lrr, (void**)&g_rs.lighting_cb_mapped[frame]));
            memset(g_rs.lighting_cb_mapped[frame], 0, 512u);
            {
                float ambient_init[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
                memcpy(g_rs.lighting_cb_mapped[frame], ambient_init, sizeof(ambient_init));
                memcpy(g_rs.lighting_cb_mapped[frame] + 256, ambient_init, sizeof(ambient_init));
            }
        }
        g_rs.ambient_level = 1.0f;
        g_rs.atmosphere_override = false;
        g_rs.atmosphere_ambient = 1.0f;
        g_rs.atmosphere_sky_r = 0.53f;
        g_rs.atmosphere_sky_g = 0.81f;
        g_rs.atmosphere_sky_b = 0.92f;
        g_rs.atmosphere_sun_x = 0.32f;
        g_rs.atmosphere_sun_y = 0.78f;
        g_rs.atmosphere_sun_z = 0.54f;
        g_rs.atmosphere_fog_r = 0.63f;
        g_rs.atmosphere_fog_g = 0.74f;
        g_rs.atmosphere_fog_b = 0.84f;
        g_rs.atmosphere_fog_density = 0.0018f;
        g_rs.atmosphere_water_r = 0.20f;
        g_rs.atmosphere_water_g = 0.48f;
        g_rs.atmosphere_water_b = 0.72f;
        g_rs.atmosphere_water_alpha = 0.82f;
        g_rs.atmosphere_camera_submerged = false;
        g_rs.atmosphere_waterline_y = 0.0f;
        g_rs.atmosphere_water_view_depth = 0.0f;
    }

    /* --- Triangle vertex buffer --- */
    SdkVertex tri_verts[3] = {
        {{ 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},  /* top    — red   */
        {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},  /* right  — green */
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}   /* left   — blue  */
    };
    const UINT vb_size = sizeof(tri_verts);

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD; /* upload heap is fine for M1 */

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width              = vb_size;
    rd.Height             = 1;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HR_CHECK(g_rs.device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE,
        &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&g_rs.vertex_buffer)));

    /* Map and copy vertex data */
    void* mapped = nullptr;
    D3D12_RANGE read_range = {0, 0};
    HR_CHECK(g_rs.vertex_buffer->Map(0, &read_range, &mapped));
    memcpy(mapped, tri_verts, vb_size);
    g_rs.vertex_buffer->Unmap(0, nullptr);

    /* --- MVP constant buffer (256-byte aligned, per-frame) --- */
    D3D12_HEAP_PROPERTIES cb_heap = {};
    cb_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    
    D3D12_RESOURCE_DESC cb_desc = {};
    cb_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cb_desc.Width = 256;  /* Must be 256-byte aligned */
    cb_desc.Height = 1;
    cb_desc.DepthOrArraySize = 1;
    cb_desc.MipLevels = 1;
    cb_desc.SampleDesc.Count = 1;
    cb_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    
    for (UINT frame = 0; frame < FRAME_COUNT; ++frame) {
        HR_CHECK(g_rs.device->CreateCommittedResource(
            &cb_heap, D3D12_HEAP_FLAG_NONE,
            &cb_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&g_rs.constant_buffer[frame])));
        /* Map once and keep mapped (upload heap) - reuse read_range from above */
        HR_CHECK(g_rs.constant_buffer[frame]->Map(0, &read_range, (void**)&g_rs.cb_mapped[frame]));
    }

    /* Setup vertex buffer view */
    g_rs.vb_view.BufferLocation = g_rs.vertex_buffer->GetGPUVirtualAddress();
    g_rs.vb_view.StrideInBytes  = sizeof(SdkVertex);
    g_rs.vb_view.SizeInBytes    = vb_size;

    /* --- Camera and scene --- */
    float aspect = (float)width / (float)height;
    sdk_camera_init(&g_rs.camera, aspect);
    sdk_scene_init(&g_rs.scene, &g_rs.camera);
    
    /* Position camera high above terrain to drop down with gravity */
    g_rs.camera.position = sdk_vec3(0.0f, 200.0f, 0.0f);   /* High above chunk grid */
    g_rs.camera.target = sdk_vec3(0.0f, 180.0f, 64.0f);    /* Look forward along +Z */
    g_rs.camera.far_plane = 1000.0f;
    
    /* Ensure camera target is never identical to position */
    if (g_rs.camera.target.x == g_rs.camera.position.x &&
        g_rs.camera.target.y == g_rs.camera.position.y &&
        g_rs.camera.target.z == g_rs.camera.position.z) {
        g_rs.camera.target.z += 1.0f; /* Force non-zero look direction */
    }
    
    /* Debug: verify camera setup */
    char dbg[256];
    sprintf(dbg, "[NQL SDK] Camera init: pos(%.2f,%.2f,%.2f) tgt(%.2f,%.2f,%.2f)\n",
        g_rs.camera.position.x, g_rs.camera.position.y, g_rs.camera.position.z,
        g_rs.camera.target.x, g_rs.camera.target.y, g_rs.camera.target.z);
    OutputDebugStringA(dbg);
    
    /* Init animation timer (disabled for now) */
    QueryPerformanceFrequency(&g_rs.perf_freq);
    QueryPerformanceCounter(&g_rs.last_time);
    g_rs.anim_started = false;
    
    g_rs.initialized = true;
    return SDK_OK;
}


extern "C" SdkResult sdk_renderer_resize(uint32_t width, uint32_t height)
{
    if (!g_rs.initialized) return SDK_ERR_NOT_INIT;
    if (width == 0 || height == 0) return SDK_OK; /* minimised */

    wait_for_gpu();

    /* Release existing render target references */
    for (UINT i = 0; i < FRAME_COUNT; ++i) {
        g_rs.render_targets[i].Reset();
    }

    HR_CHECK(g_rs.swap_chain->ResizeBuffers(
        FRAME_COUNT, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM, 0));

    g_rs.width  = width;
    g_rs.height = height;
    g_rs.frame_index = g_rs.swap_chain->GetCurrentBackBufferIndex();

    /* Update camera aspect ratio */
    sdk_camera_set_projection(&g_rs.camera, 60.0f, (float)width / (float)height, 0.1f, 1000.0f);

    SdkResult r = create_render_targets();
    if (r != SDK_OK) return r;
    return create_depth_buffer(width, height);
}

extern "C" bool sdk_renderer_is_fullscreen(void)
{
    BOOL fullscreen = FALSE;

    if (!g_rs.initialized || !g_rs.swap_chain) return false;
    if (FAILED(g_rs.swap_chain->GetFullscreenState(&fullscreen, nullptr))) {
        return false;
    }
    return fullscreen == TRUE;
}

extern "C" SdkResult sdk_renderer_set_display_mode(int display_mode,
                                                    uint32_t requested_width,
                                                    uint32_t requested_height,
                                                    uint32_t* out_width,
                                                    uint32_t* out_height)
{
    BOOL fullscreen = FALSE;
    HRESULT hr = S_OK;

    if (out_width) *out_width = g_rs.width;
    if (out_height) *out_height = g_rs.height;
    if (!g_rs.initialized || !g_rs.swap_chain) return SDK_ERR_NOT_INIT;

    g_rs.swap_chain->GetFullscreenState(&fullscreen, nullptr);
    if (display_mode != 2 && fullscreen) {
        wait_for_gpu();
        hr = g_rs.swap_chain->SetFullscreenState(FALSE, nullptr);
        if (FAILED(hr)) {
            log_display_mode_hresult("SetFullscreenState(FALSE)", hr);
            return SDK_ERR_GENERIC;
        }
        return SDK_OK;
    }

    if (display_mode == 2) {
        ComPtr<IDXGIOutput> output;
        DXGI_MODE_DESC request_desc = {};
        DXGI_MODE_DESC matched_desc = {};
        DXGI_OUTPUT_DESC output_desc = {};

        request_desc.Width = requested_width ? requested_width : g_rs.width;
        request_desc.Height = requested_height ? requested_height : g_rs.height;
        request_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        request_desc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        request_desc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

        hr = g_rs.swap_chain->GetContainingOutput(&output);
        if (FAILED(hr) || !output) {
            log_display_mode_hresult("GetContainingOutput", hr);
            return SDK_ERR_GENERIC;
        }

        hr = output->FindClosestMatchingMode(&request_desc, &matched_desc, g_rs.device.Get());
        if (FAILED(hr)) {
            log_display_mode_hresult("FindClosestMatchingMode", hr);
            matched_desc = request_desc;
            matched_desc.RefreshRate.Numerator = 0;
            matched_desc.RefreshRate.Denominator = 0;
            if (SUCCEEDED(output->GetDesc(&output_desc))) {
                matched_desc.Width = (UINT)(output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left);
                matched_desc.Height = (UINT)(output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top);
            }
        }

        wait_for_gpu();
        hr = g_rs.swap_chain->ResizeTarget(&matched_desc);
        if (FAILED(hr)) {
            log_display_mode_hresult("ResizeTarget", hr);
            return SDK_ERR_GENERIC;
        }
        if (!fullscreen) {
            hr = g_rs.swap_chain->SetFullscreenState(TRUE, output.Get());
            if (FAILED(hr)) {
                log_display_mode_hresult("SetFullscreenState(TRUE)", hr);
                return SDK_ERR_GENERIC;
            }
        }
        if (out_width) *out_width = matched_desc.Width;
        if (out_height) *out_height = matched_desc.Height;
        return sdk_renderer_resize(matched_desc.Width, matched_desc.Height);
    }

    return SDK_OK;
}

/* ======================================================================
 * SHUTDOWN
 * ====================================================================== */

extern "C" void sdk_renderer_shutdown(void)
{
    if (!g_rs.initialized) return;

    wait_for_gpu();
    if (sdk_renderer_is_fullscreen()) {
        g_rs.swap_chain->SetFullscreenState(FALSE, nullptr);
    }
    reclaim_retired_resources();
    release_wall_proxy_cache();

    if (g_rs.fence_event) {
        CloseHandle(g_rs.fence_event);
        g_rs.fence_event = nullptr;
    }

    for (UINT i = 0; i < g_rs.retired_resource_count; ++i) {
        if (g_rs.retired_resources[i].resource) {
            g_rs.retired_resources[i].resource->Release();
        }
    }
    g_rs.retired_resource_count = 0;

    if (g_rs.com_initialized) {
        CoUninitialize();
        g_rs.com_initialized = false;
    }

    if (g_rs.player_character_cpu_vertices) {
        free(g_rs.player_character_cpu_vertices);
        g_rs.player_character_cpu_vertices = nullptr;
        g_rs.player_character_cpu_count = 0;
    }

    /* ComPtr destructors handle Release() */
    g_rs = RendererState{};
}

