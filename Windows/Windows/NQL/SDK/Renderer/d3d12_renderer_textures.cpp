/**
 * d3d12_renderer_textures.cpp -- Texture asset and font atlas loading.
 */
#include "d3d12_renderer_internal.h"

static bool file_exists_w(const wchar_t* path)
{
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool resolve_texture_asset_path(const char* asset, wchar_t* out, size_t out_len)
{
    wchar_t exe_path[MAX_PATH];
    wchar_t asset_w[128];
    wchar_t candidate[MAX_PATH];
    wchar_t cwd[MAX_PATH];
    wchar_t* last_sep;

    if (!asset || !out || out_len == 0) return false;
    out[0] = L'\0';

    if (MultiByteToWideChar(CP_UTF8, 0, asset, -1, asset_w, (int)_countof(asset_w)) <= 0) {
        return false;
    }

    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH) > 0) {
        last_sep = wcsrchr(exe_path, L'\\');
        if (last_sep) {
            *(last_sep + 1) = L'\0';
            _snwprintf_s(candidate, _countof(candidate), _TRUNCATE,
                L"%sTexturePacks\\Default\\blocks\\%s.png", exe_path, asset_w);
            if (file_exists_w(candidate)) {
                wcsncpy_s(out, out_len, candidate, _TRUNCATE);
                return true;
            }
        }
    }

    if (GetCurrentDirectoryW(MAX_PATH, cwd) > 0) {
        _snwprintf_s(candidate, _countof(candidate), _TRUNCATE,
            L"%s\\TexturePacks\\Default\\blocks\\%s.png", cwd, asset_w);
        if (file_exists_w(candidate)) {
            wcsncpy_s(out, out_len, candidate, _TRUNCATE);
            return true;
        }

        _snwprintf_s(candidate, _countof(candidate), _TRUNCATE,
            L"%s\\Windows\\NQL\\SDK\\TexturePacks\\Default\\blocks\\%s.png", cwd, asset_w);
        if (file_exists_w(candidate)) {
            wcsncpy_s(out, out_len, candidate, _TRUNCATE);
            return true;
        }
    }

    return false;
}

static bool load_texture_rgba32(const wchar_t* path, UINT tile_size, std::vector<uint8_t>& rgba)
{
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    IWICBitmapSource* source = nullptr;
    UINT src_w = 0;
    UINT src_h = 0;

    if (!path || !file_exists_w(path)) return false;

    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))) {
        return false;
    }

    if (FAILED(factory->CreateDecoderFromFilename(
            path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) {
        return false;
    }

    if (FAILED(decoder->GetFrame(0, &frame))) {
        return false;
    }

    if (FAILED(frame->GetSize(&src_w, &src_h))) {
        return false;
    }

    source = frame.Get();
    if (src_w != tile_size || src_h != tile_size) {
        if (FAILED(factory->CreateBitmapScaler(&scaler))) {
            return false;
        }
        if (FAILED(scaler->Initialize(source, tile_size, tile_size, WICBitmapInterpolationModeNearestNeighbor))) {
            return false;
        }
        source = scaler.Get();
    }

    if (FAILED(factory->CreateFormatConverter(&converter))) {
        return false;
    }

    if (FAILED(converter->Initialize(
            source,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom))) {
        return false;
    }

    rgba.resize((size_t)tile_size * (size_t)tile_size * 4u);
    if (FAILED(converter->CopyPixels(
            nullptr,
            tile_size * 4u,
            (UINT)rgba.size(),
            rgba.data()))) {
        rgba.clear();
        return false;
    }

    return true;
}

static void fix_transparent_texel_rgb(std::vector<uint8_t>& rgba, UINT tile_size)
{
    std::vector<uint8_t> scratch;
    int pass;

    if (tile_size == 0) return;
    if (rgba.size() != (size_t)tile_size * (size_t)tile_size * 4u) return;

    scratch.resize(rgba.size());
    for (pass = 0; pass < 4; ++pass) {
        bool changed = false;
        UINT y;

        memcpy(scratch.data(), rgba.data(), rgba.size());
        for (y = 0; y < tile_size; ++y) {
            UINT x;
            for (x = 0; x < tile_size; ++x) {
                size_t index = ((size_t)y * (size_t)tile_size + (size_t)x) * 4u;
                uint32_t sum_r = 0;
                uint32_t sum_g = 0;
                uint32_t sum_b = 0;
                uint32_t count = 0;
                int dy;

                if (rgba[index + 3] > 16u) continue;

                for (dy = -1; dy <= 1; ++dy) {
                    int ny = (int)y + dy;
                    int dx;
                    if (ny < 0 || ny >= (int)tile_size) continue;
                    for (dx = -1; dx <= 1; ++dx) {
                        int nx = (int)x + dx;
                        size_t nindex;
                        if ((dx == 0 && dy == 0) || nx < 0 || nx >= (int)tile_size) continue;
                        nindex = ((size_t)ny * (size_t)tile_size + (size_t)nx) * 4u;
                        if (rgba[nindex + 3] <= 16u) continue;
                        sum_r += rgba[nindex + 0];
                        sum_g += rgba[nindex + 1];
                        sum_b += rgba[nindex + 2];
                        count++;
                    }
                }

                if (count > 0u) {
                    scratch[index + 0] = (uint8_t)(sum_r / count);
                    scratch[index + 1] = (uint8_t)(sum_g / count);
                    scratch[index + 2] = (uint8_t)(sum_b / count);
                    changed = true;
                }
            }
        }

        rgba.swap(scratch);
        if (!changed) {
            break;
        }
    }
}

static void normalize_cutout_alpha(std::vector<uint8_t>& rgba, UINT tile_size)
{
    size_t pixel_count;

    if (tile_size == 0) return;
    if (rgba.size() != (size_t)tile_size * (size_t)tile_size * 4u) return;

    pixel_count = (size_t)tile_size * (size_t)tile_size;
    for (size_t i = 0; i < pixel_count; ++i) {
        size_t index = i * 4u;
        rgba[index + 3] = (rgba[index + 3] >= 96u) ? 255u : 0u;
    }
}

static bool build_font_glyph_rgba(wchar_t ch, UINT tile_size, std::vector<uint8_t>& rgba)
{
    BITMAPINFO bmi;
    HDC dc = NULL;
    HBITMAP bmp = NULL;
    HGDIOBJ old_bmp = NULL;
    HFONT font = NULL;
    HGDIOBJ old_font = NULL;
    RECT rc;
    void* bits = NULL;
    bool ok = false;

    rgba.clear();
    rgba.resize((size_t)tile_size * (size_t)tile_size * 4u, 0u);
    if (ch < (wchar_t)HUD_FONT_FIRST_CHAR || ch > (wchar_t)HUD_FONT_LAST_CHAR || ch == L' ') {
        return true;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)tile_size;
    bmi.bmiHeader.biHeight = -(LONG)tile_size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dc = CreateCompatibleDC(NULL);
    if (!dc) goto cleanup;

    bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0u);
    if (!bmp || !bits) goto cleanup;
    old_bmp = SelectObject(dc, bmp);

    font = CreateFontW(
        -(LONG)(tile_size - 1u), 0, 0, 0,
        FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    if (!font) goto cleanup;
    old_font = SelectObject(dc, font);

    SetBkColor(dc, RGB(0, 0, 0));
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, OPAQUE);
    rc.left = 0;
    rc.top = 0;
    rc.right = (LONG)tile_size;
    rc.bottom = (LONG)tile_size;
    FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    DrawTextW(dc, &ch, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    {
        const uint8_t* src = (const uint8_t*)bits;
        for (UINT y = 0; y < tile_size; ++y) {
            for (UINT x = 0; x < tile_size; ++x) {
                size_t src_index = ((size_t)y * (size_t)tile_size + (size_t)x) * 4u;
                /* Extract alpha from rendered glyph (white on black) */
                uint8_t r = src[src_index + 0];
                uint8_t g = src[src_index + 1];
                uint8_t b = src[src_index + 2];
                uint8_t a = (r + g + b) / 3;  /* Convert to grayscale alpha */
                rgba[src_index + 0] = 255u;   /* R - white */
                rgba[src_index + 1] = 255u;   /* G - white */
                rgba[src_index + 2] = 255u;   /* B - white */
                rgba[src_index + 3] = a;      /* A - from glyph */
            }
        }
    }

    ok = true;

cleanup:
    if (old_font) SelectObject(dc, old_font);
    if (font) DeleteObject(font);
    if (old_bmp) SelectObject(dc, old_bmp);
    if (bmp) DeleteObject(bmp);
    if (dc) DeleteDC(dc);
    if (!ok) rgba.clear();
    return ok;
}

static void fill_fallback_texture_rgba(BlockType type, int face, std::vector<uint8_t>& rgba)
{
    uint32_t col = sdk_block_get_face_color(type, face);
    uint8_t r = (uint8_t)(col & 0xFFu);
    uint8_t g = (uint8_t)((col >> 8) & 0xFFu);
    uint8_t b = (uint8_t)((col >> 16) & 0xFFu);
    uint8_t a = (uint8_t)((col >> 24) & 0xFFu);
    size_t i;

    rgba.resize((size_t)BLOCK_TEXTURE_TILE_SIZE * (size_t)BLOCK_TEXTURE_TILE_SIZE * 4u);
    for (i = 0; i < rgba.size(); i += 4u) {
        rgba[i + 0] = r;
        rgba[i + 1] = g;
        rgba[i + 2] = b;
        rgba[i + 3] = a;
    }
}

SdkResult create_block_texture_array(void)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    D3D12_HEAP_PROPERTIES texture_hp = {};
    D3D12_HEAP_PROPERTIES upload_hp = {};
    D3D12_RESOURCE_DESC texture_desc = {};
    D3D12_RESOURCE_DESC upload_desc = {};
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    D3D12_RESOURCE_BARRIER barrier = {};
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
    std::vector<UINT> row_counts;
    std::vector<UINT64> row_sizes;
    std::vector<uint8_t> tile_rgba;
    ComPtr<ID3D12Resource> upload_buffer;
    UINT64 upload_size = 0;
    void* mapped = nullptr;
    D3D12_RANGE read_range = {0, 0};
    UINT base_layer_count = (UINT)(BLOCK_COUNT * BLOCK_TEXTURE_FACE_COUNT);
    UINT layer_count = base_layer_count + HUD_FONT_GLYPH_COUNT;
    UINT layer;

    if (!g_rs.device || !g_rs.cmd_list || !g_rs.cmd_alloc[0] || !g_rs.cmd_queue) {
        return SDK_ERR_NOT_INIT;
    }

    g_rs.block_texture_layers = layer_count;
    g_rs.font_texture_layer_base = base_layer_count;

    texture_hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Alignment = 0;
    texture_desc.Width = BLOCK_TEXTURE_TILE_SIZE;
    texture_desc.Height = BLOCK_TEXTURE_TILE_SIZE;
    texture_desc.DepthOrArraySize = (UINT16)layer_count;
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HR_CHECK(g_rs.device->CreateCommittedResource(
        &texture_hp, D3D12_HEAP_FLAG_NONE,
        &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&g_rs.block_texture_array)));

    layouts.resize(layer_count);
    row_counts.resize(layer_count);
    row_sizes.resize(layer_count);
    g_rs.device->GetCopyableFootprints(
        &texture_desc, 0, layer_count, 0,
        layouts.data(), row_counts.data(), row_sizes.data(), &upload_size);

    upload_hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_desc.Width = upload_size;
    upload_desc.Height = 1;
    upload_desc.DepthOrArraySize = 1;
    upload_desc.MipLevels = 1;
    upload_desc.SampleDesc.Count = 1;
    upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HR_CHECK(g_rs.device->CreateCommittedResource(
        &upload_hp, D3D12_HEAP_FLAG_NONE,
        &upload_desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&upload_buffer)));

    HR_CHECK(upload_buffer->Map(0, &read_range, &mapped));
    for (layer = 0; layer < layer_count; ++layer) {
        wchar_t texture_path[MAX_PATH];
        BYTE* dst = (BYTE*)mapped + layouts[layer].Offset;
        UINT row;
        if (layer < base_layer_count) {
            BlockType block_type = (BlockType)(layer / BLOCK_TEXTURE_FACE_COUNT);
            int face = (int)(layer % BLOCK_TEXTURE_FACE_COUNT);
            const char* asset = sdk_block_get_texture_asset(block_type, face);

            if (!resolve_texture_asset_path(asset, texture_path, _countof(texture_path)) ||
                !load_texture_rgba32(texture_path, BLOCK_TEXTURE_TILE_SIZE, tile_rgba)) {
                fill_fallback_texture_rgba(block_type, face, tile_rgba);
            } else {
                if (!sdk_block_is_opaque(block_type) &&
                    (sdk_block_get_behavior_flags(block_type) & SDK_BLOCK_BEHAVIOR_FLUID) == 0u) {
                    normalize_cutout_alpha(tile_rgba, BLOCK_TEXTURE_TILE_SIZE);
                }
                fix_transparent_texel_rgb(tile_rgba, BLOCK_TEXTURE_TILE_SIZE);
            }
        } else {
            wchar_t glyph = (wchar_t)(HUD_FONT_FIRST_CHAR + (layer - base_layer_count));
            if (!build_font_glyph_rgba(glyph, BLOCK_TEXTURE_TILE_SIZE, tile_rgba)) {
                tile_rgba.assign((size_t)BLOCK_TEXTURE_TILE_SIZE * (size_t)BLOCK_TEXTURE_TILE_SIZE * 4u, 0u);
            }
        }

        for (row = 0; row < BLOCK_TEXTURE_TILE_SIZE; ++row) {
            memcpy(
                dst + row * layouts[layer].Footprint.RowPitch,
                tile_rgba.data() + (size_t)row * BLOCK_TEXTURE_TILE_SIZE * 4u,
                (size_t)BLOCK_TEXTURE_TILE_SIZE * 4u);
        }
    }
    upload_buffer->Unmap(0, nullptr);

    heap_desc.NumDescriptors = 1;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HR_CHECK(g_rs.device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&g_rs.texture_srv_heap)));

    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2DArray.MostDetailedMip = 0;
    srv_desc.Texture2DArray.MipLevels = 1;
    srv_desc.Texture2DArray.FirstArraySlice = 0;
    srv_desc.Texture2DArray.ArraySize = layer_count;
    srv_desc.Texture2DArray.PlaneSlice = 0;
    srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    g_rs.device->CreateShaderResourceView(
        g_rs.block_texture_array.Get(),
        &srv_desc,
        g_rs.texture_srv_heap->GetCPUDescriptorHandleForHeapStart());

    HR_CHECK(g_rs.cmd_alloc[0]->Reset());
    HR_CHECK(g_rs.cmd_list->Reset(g_rs.cmd_alloc[0].Get(), nullptr));

    for (layer = 0; layer < layer_count; ++layer) {
        D3D12_TEXTURE_COPY_LOCATION dst_loc = {};
        D3D12_TEXTURE_COPY_LOCATION src_loc = {};

        dst_loc.pResource = g_rs.block_texture_array.Get();
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.SubresourceIndex = layer;

        src_loc.pResource = upload_buffer.Get();
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src_loc.PlacedFootprint = layouts[layer];

        g_rs.cmd_list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
    }

    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = g_rs.block_texture_array.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    g_rs.cmd_list->ResourceBarrier(1, &barrier);

    HR_CHECK(g_rs.cmd_list->Close());
    {
        ID3D12CommandList* lists[] = { g_rs.cmd_list.Get() };
        g_rs.cmd_queue->ExecuteCommandLists(1, lists);
    }
    wait_for_gpu();

    return SDK_OK;
}

