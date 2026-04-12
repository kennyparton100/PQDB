/**
 * d3d12_pipeline.cpp — Root signature + PSO creation, HLSL compilation
 *
 * Compiles shaders at runtime using D3DCompileFromFile and builds the
 * graphics pipeline state for the coloured-triangle demo.
 */
#include "d3d12_pipeline.h"

#include <string.h>
#include <stdio.h>

/* ======================================================================
 * HELPER: resolve shader path relative to the executable
 * ====================================================================== */

static bool resolve_shader_path(const wchar_t* filename, wchar_t* out, size_t out_len)
{
    /* Get directory of the running .exe */
    wchar_t exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    /* Strip filename from exe path */
    wchar_t* last_sep = wcsrchr(exe_path, L'\\');
    if (last_sep) *(last_sep + 1) = L'\0';

    /* Build path: <exe_dir>\Shaders\<filename> */
    _snwprintf(out, out_len, L"%sShaders\\%s", exe_path, filename);
    return true;
}

/* ======================================================================
 * HELPER: try multiple shader search paths
 * ====================================================================== */

static HRESULT compile_shader(
    const wchar_t* filename,
    const char* entry,
    const char* target,
    ID3DBlob** out_blob)
{
    wchar_t path[MAX_PATH];
    HRESULT hr;
    ComPtr<ID3DBlob> errors;

    /* Strategy 1: relative to exe — <exe_dir>\Shaders\filename */
    if (resolve_shader_path(filename, path, MAX_PATH)) {
        hr = D3DCompileFromFile(path, nullptr, nullptr,
                                entry, target,
                                D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
                                0, out_blob, &errors);
        if (SUCCEEDED(hr)) return hr;
    }

    /* Strategy 2: relative to CWD — Shaders\filename */
    _snwprintf(path, MAX_PATH, L"Shaders\\%s", filename);
    hr = D3DCompileFromFile(path, nullptr, nullptr,
                            entry, target,
                            D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
                            0, out_blob, &errors);
    if (SUCCEEDED(hr)) return hr;

    /* Strategy 3: CWD\SDK\Shaders\filename (running from project root) */
    _snwprintf(path, MAX_PATH, L"SDK\\Shaders\\%s", filename);
    hr = D3DCompileFromFile(path, nullptr, nullptr,
                            entry, target,
                            D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
                            0, out_blob, &errors);
    if (SUCCEEDED(hr)) return hr;

    if (errors) {
        fprintf(stderr, "[NQL SDK] Shader compile error:\n%s\n",
                (const char*)errors->GetBufferPointer());
    }
    return hr;
}

/* ======================================================================
 * PUBLIC: create root signature + PSO
 * ====================================================================== */

SdkResult sdk_pipeline_create(
    ID3D12Device*                       device,
    ComPtr<ID3D12RootSignature>*        out_root,
    ComPtr<ID3D12PipelineState>*        out_pso)
{
    if (!device || !out_root || !out_pso) return SDK_ERR_INVALID_ARG;

    /* --- Root signature with MVP (b0, vertex) + Lighting (b1, pixel) + texture array (t0, pixel) --- */
    D3D12_DESCRIPTOR_RANGE srv_range = {};
    srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_range.NumDescriptors = 1;
    srv_range.BaseShaderRegister = 0;
    srv_range.RegisterSpace = 0;
    srv_range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER root_params[3] = {};
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_params[0].Descriptor.ShaderRegister = 0;  /* b0 */
    root_params[0].Descriptor.RegisterSpace = 0;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_params[1].Descriptor.ShaderRegister = 1;  /* b1 */
    root_params[1].Descriptor.RegisterSpace = 0;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[2].DescriptorTable.NumDescriptorRanges = 1;
    root_params[2].DescriptorTable.pDescriptorRanges = &srv_range;
    root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[7] = {};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MipLODBias = 0.0f;
    samplers[0].MaxAnisotropy = 1;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplers[0].MinLOD = 0.0f;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    samplers[1] = samplers[0];
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;  /* Point filtering for crisp text */
    samplers[1].ShaderRegister = 1;

    samplers[2] = samplers[0];
    samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[2].ShaderRegister = 2;

    samplers[3] = samplers[0];
    samplers[3].Filter = D3D12_FILTER_ANISOTROPIC;
    samplers[3].MaxAnisotropy = 2;
    samplers[3].ShaderRegister = 3;

    samplers[4] = samplers[0];
    samplers[4].Filter = D3D12_FILTER_ANISOTROPIC;
    samplers[4].MaxAnisotropy = 4;
    samplers[4].ShaderRegister = 4;

    samplers[5] = samplers[0];
    samplers[5].Filter = D3D12_FILTER_ANISOTROPIC;
    samplers[5].MaxAnisotropy = 8;
    samplers[5].ShaderRegister = 5;

    samplers[6] = samplers[0];
    samplers[6].Filter = D3D12_FILTER_ANISOTROPIC;
    samplers[6].MaxAnisotropy = 16;
    samplers[6].ShaderRegister = 6;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 3;
    rsd.pParameters = root_params;
    rsd.NumStaticSamplers = _countof(samplers);
    rsd.pStaticSamplers = samplers;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig_blob, err_blob;
    HR_CHECK(D3D12SerializeRootSignature(
        &rsd, D3D_ROOT_SIGNATURE_VERSION_1,
        &sig_blob, &err_blob));

    HR_CHECK(device->CreateRootSignature(
        0,
        sig_blob->GetBufferPointer(),
        sig_blob->GetBufferSize(),
        IID_PPV_ARGS(&(*out_root))));

    /* --- Compile shaders --- */
    ComPtr<ID3DBlob> vs_blob, ps_blob;

    HRESULT hr = compile_shader(L"triangle_vs.hlsl", "VSMain", "vs_5_0", &vs_blob);
    if (FAILED(hr)) return SDK_ERR_SHADER_FAILED;

    hr = compile_shader(L"triangle_ps.hlsl", "PSMain", "ps_5_0", &ps_blob);
    if (FAILED(hr)) return SDK_ERR_SHADER_FAILED;

    /* --- Input layout matching BlockVertex --- */
    D3D12_INPUT_ELEMENT_DESC input_elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32_UINT,           0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT,           0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    /* --- Graphics PSO --- */
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
    psd.InputLayout           = { input_elems, _countof(input_elems) };
    psd.pRootSignature        = out_root->Get();
    psd.VS                    = { vs_blob->GetBufferPointer(), vs_blob->GetBufferSize() };
    psd.PS                    = { ps_blob->GetBufferPointer(), ps_blob->GetBufferSize() };

    /* Rasteriser — solid fill, no culling for a flat triangle */
    psd.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psd.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
    psd.RasterizerState.DepthClipEnable       = TRUE;
    psd.RasterizerState.FrontCounterClockwise = FALSE;

    /* Blend — alpha enabled for water / translucent atmospheric passes */
    psd.BlendState.AlphaToCoverageEnable = FALSE;
    psd.BlendState.IndependentBlendEnable = FALSE;
    psd.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psd.DepthStencilState.DepthEnable    = TRUE;
    psd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS;
    psd.DepthStencilState.StencilEnable  = FALSE;
    psd.SampleMask                      = UINT_MAX;
    psd.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psd.NumRenderTargets                = 1;
    psd.RTVFormats[0]                   = DXGI_FORMAT_R8G8B8A8_UNORM;
    psd.DSVFormat                       = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psd.SampleDesc.Count                = 1;

    HR_CHECK(device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&(*out_pso))));

    return SDK_OK;
}

SdkResult sdk_pipeline_create_water(
    ID3D12Device*                       device,
    ID3D12RootSignature*                root_sig,
    ComPtr<ID3D12PipelineState>*        out_pso)
{
    if (!device || !root_sig || !out_pso) return SDK_ERR_INVALID_ARG;

    ComPtr<ID3DBlob> vs_blob, ps_blob;
    HRESULT hr = compile_shader(L"triangle_vs.hlsl", "VSMain", "vs_5_0", &vs_blob);
    if (FAILED(hr)) return SDK_ERR_SHADER_FAILED;
    hr = compile_shader(L"triangle_ps.hlsl", "PSMain", "ps_5_0", &ps_blob);
    if (FAILED(hr)) return SDK_ERR_SHADER_FAILED;

    D3D12_INPUT_ELEMENT_DESC input_elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32_UINT,           0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT,           0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
    psd.InputLayout           = { input_elems, _countof(input_elems) };
    psd.pRootSignature        = root_sig;
    psd.VS                    = { vs_blob->GetBufferPointer(), vs_blob->GetBufferSize() };
    psd.PS                    = { ps_blob->GetBufferPointer(), ps_blob->GetBufferSize() };
    psd.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psd.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
    psd.RasterizerState.DepthClipEnable       = TRUE;
    psd.RasterizerState.FrontCounterClockwise = FALSE;
    psd.BlendState.AlphaToCoverageEnable = FALSE;
    psd.BlendState.IndependentBlendEnable = FALSE;
    psd.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psd.DepthStencilState.DepthEnable    = TRUE;
    psd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psd.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psd.DepthStencilState.StencilEnable  = FALSE;
    psd.SampleMask                      = UINT_MAX;
    psd.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psd.NumRenderTargets                = 1;
    psd.RTVFormats[0]                   = DXGI_FORMAT_R8G8B8A8_UNORM;
    psd.DSVFormat                       = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psd.SampleDesc.Count                = 1;

    HR_CHECK(device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&(*out_pso))));
    return SDK_OK;
}

SdkResult sdk_pipeline_create_hud(
    ID3D12Device*                       device,
    ID3D12RootSignature*                root_sig,
    ComPtr<ID3D12PipelineState>*        out_pso)
{
    if (!device || !root_sig || !out_pso) return SDK_ERR_INVALID_ARG;

    /* Compile shaders (same as 3D pipeline) */
    ComPtr<ID3DBlob> vs_blob, ps_blob;
    HRESULT hr = compile_shader(L"triangle_vs.hlsl", "VSMain", "vs_5_0", &vs_blob);
    if (FAILED(hr)) return SDK_ERR_SHADER_FAILED;
    hr = compile_shader(L"triangle_ps.hlsl", "PSMain", "ps_5_0", &ps_blob);
    if (FAILED(hr)) return SDK_ERR_SHADER_FAILED;

    D3D12_INPUT_ELEMENT_DESC input_elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32_UINT,           0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXINDEX", 0, DXGI_FORMAT_R32_UINT,           0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psd = {};
    psd.InputLayout           = { input_elems, _countof(input_elems) };
    psd.pRootSignature        = root_sig;
    psd.VS                    = { vs_blob->GetBufferPointer(), vs_blob->GetBufferSize() };
    psd.PS                    = { ps_blob->GetBufferPointer(), ps_blob->GetBufferSize() };
    psd.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
    psd.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
    psd.RasterizerState.DepthClipEnable       = FALSE;
    psd.RasterizerState.FrontCounterClockwise = FALSE;
    psd.BlendState.AlphaToCoverageEnable = FALSE;
    psd.BlendState.IndependentBlendEnable = FALSE;
    psd.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psd.DepthStencilState.DepthEnable    = FALSE;
    psd.DepthStencilState.StencilEnable  = FALSE;
    psd.SampleMask                      = UINT_MAX;
    psd.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psd.NumRenderTargets                = 1;
    psd.RTVFormats[0]                   = DXGI_FORMAT_R8G8B8A8_UNORM;
    psd.SampleDesc.Count                = 1;

    HR_CHECK(device->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&(*out_pso))));
    return SDK_OK;
}
