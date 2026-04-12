/**
 * d3d12_pipeline.h — D3D12 pipeline state object + root signature
 *
 * C++ header. Compiles HLSL shaders and creates the PSO for the triangle.
 */
#ifndef NQLSDK_D3D12_PIPELINE_H
#define NQLSDK_D3D12_PIPELINE_H

#include "d3d12_helpers.h"
#include "../Core/sdk_types.h"

/**
 * Create the root signature and pipeline state object.
 * Compiles vertex + pixel shaders from the Shaders/ directory.
 *
 * @param device    The D3D12 device.
 * @param out_root  Receives the root signature.
 * @param out_pso   Receives the pipeline state object.
 * @return SDK_OK on success.
 */
SdkResult sdk_pipeline_create(
    ID3D12Device*                       device,
    ComPtr<ID3D12RootSignature>*        out_root,
    ComPtr<ID3D12PipelineState>*        out_pso);

SdkResult sdk_pipeline_create_water(
    ID3D12Device*                       device,
    ID3D12RootSignature*                root_sig,
    ComPtr<ID3D12PipelineState>*        out_pso);

/** Create a HUD pipeline (same shaders, no depth test). */
SdkResult sdk_pipeline_create_hud(
    ID3D12Device*                       device,
    ID3D12RootSignature*                root_sig,
    ComPtr<ID3D12PipelineState>*        out_pso);

#endif /* NQLSDK_D3D12_PIPELINE_H */
