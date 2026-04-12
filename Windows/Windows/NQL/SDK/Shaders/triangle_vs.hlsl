/**
 * triangle_vs.hlsl — Vertex shader with MVP transform
 *
 * Transforms vertex position by MVP matrix and forwards color.
 */

cbuffer Transform : register(b0) {
    float4x4 mvp;
};

struct VSInput {
    float3 position : POSITION;
    float4 color    : COLOR;
    uint   normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    uint   texIndex : TEXINDEX;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    uint   normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    uint   texIndex : TEXINDEX;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(mvp, float4(input.position, 1.0f));
    output.color    = input.color;
    output.normal   = input.normal;
    output.uv       = input.uv;
    output.worldPos = input.position;
    output.texIndex = input.texIndex;
    return output;
}
