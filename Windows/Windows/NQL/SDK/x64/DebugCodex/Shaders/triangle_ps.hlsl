/**
 * triangle_ps.hlsl - Pixel shader with directional sun, hemispheric sky, water shading, and fog
 */

cbuffer Lighting : register(b1) {
    float4 lighting0;   /* ambient, fog_density, water_alpha, pad */
    float4 sky_color;
    float4 sun_dir;
    float4 fog_color;
    float4 water_color;
    float4 camera_pos;
    float4 material_ids;
};

Texture2DArray blockTextures : register(t0);
SamplerState blockSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    uint   normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    uint   texIndex : TEXINDEX;
};

float3 decode_face_normal(uint face)
{
    if (face == 0u) return float3(-1.0f, 0.0f, 0.0f);
    if (face == 1u) return float3( 1.0f, 0.0f, 0.0f);
    if (face == 2u) return float3( 0.0f,-1.0f, 0.0f);
    if (face == 3u) return float3( 0.0f, 1.0f, 0.0f);
    if (face == 4u) return float3( 0.0f, 0.0f,-1.0f);
    return float3(0.0f, 0.0f, 1.0f);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 base = input.color;
    if (input.texIndex == 0xFFFFFFFFu) {
        return base;
    }

    base = blockTextures.Sample(blockSampler, float3(input.uv, (float)input.texIndex));
    if (base.a <= 0.01f) discard;
    base *= input.color;

    float3 n = normalize(decode_face_normal(input.normal));
    float3 sun = normalize(sun_dir.xyz);
    float sun_term = saturate(dot(n, sun));
    float hemi_term = saturate(n.y * 0.5f + 0.5f);
    float brightness = lighting0.x * lerp(0.55f, 1.00f, hemi_term) + sun_term * 0.55f;

    float3 rgb = base.rgb * brightness;
    rgb = lerp(fog_color.rgb * 0.30f + rgb * 0.70f, sky_color.rgb, 0.12f * hemi_term);

    uint block_index = input.texIndex / 6u;
    if ((float)block_index == material_ids.x) {
        float top_lit = saturate(dot(n, sun));
        float water_read = 0.25f + 0.75f * hemi_term;
        rgb = lerp(rgb, water_color.rgb, 0.72f);
        rgb += top_lit * 0.08f;
        rgb *= water_read;
        base.a *= lighting0.z;
    }

    float dist = distance(input.worldPos, camera_pos.xyz);
    float fog_factor = saturate(1.0f - exp(-lighting0.y * dist));
    if ((float)block_index == material_ids.x) {
        fog_factor = max(fog_factor, saturate((dist - 4.0f) * 0.06f));
    }

    rgb = lerp(rgb, fog_color.rgb, fog_factor);
    return float4(rgb, base.a);
}
