/**
 * triangle_ps.hlsl - Pixel shader with directional sun, hemispheric sky, water shading, and fog
 */

cbuffer Lighting : register(b1) {
    float4 lighting0;   /* ambient, fog_density, water_alpha, anisotropy_level */
    float4 sky_color;
    float4 sun_dir;
    float4 fog_color;
    float4 water_color;
    float4 camera_pos;
    float4 material_ids;
    float4 water_params; /* camera_submerged, waterline_y, water_view_depth, reserved */
};

Texture2DArray blockTextures : register(t0);
SamplerState pointSampler : register(s0);
SamplerState fontSampler : register(s1);
SamplerState linearSampler : register(s2);
SamplerState aniso2Sampler : register(s3);
SamplerState aniso4Sampler : register(s4);
SamplerState aniso8Sampler : register(s5);
SamplerState aniso16Sampler : register(s6);

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

float4 sample_block_texture_filtered(uint texIndex, float2 uv, float anisotropyLevel)
{
    if (anisotropyLevel >= 16.0f) {
        return blockTextures.Sample(aniso16Sampler, float3(uv, (float)texIndex));
    }
    if (anisotropyLevel >= 8.0f) {
        return blockTextures.Sample(aniso8Sampler, float3(uv, (float)texIndex));
    }
    if (anisotropyLevel >= 4.0f) {
        return blockTextures.Sample(aniso4Sampler, float3(uv, (float)texIndex));
    }
    if (anisotropyLevel >= 2.0f) {
        return blockTextures.Sample(aniso2Sampler, float3(uv, (float)texIndex));
    }
    if (anisotropyLevel >= 1.0f) {
        return blockTextures.Sample(linearSampler, float3(uv, (float)texIndex));
    }
    return blockTextures.Sample(pointSampler, float3(uv, (float)texIndex));
}

float fog_dither(float3 worldPos)
{
    float2 p = worldPos.xz * 4.0f;
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f) - 0.5f;
}

float water_segment_fraction(float waterlineY, float cameraY, float targetY)
{
    float a = cameraY - waterlineY;
    float b = targetY - waterlineY;

    if (a <= 0.0f && b <= 0.0f) return 1.0f;
    if (a > 0.0f && b > 0.0f) return 0.0f;
    if (abs(a - b) < 0.0001f) return (a <= 0.0f) ? 1.0f : 0.0f;

    {
        float t = saturate(a / (a - b));
        return (a <= 0.0f) ? t : (1.0f - t);
    }
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 base = input.color;
    float anisotropyLevel = max(lighting0.w, 0.0f);
    uint block_index = (input.texIndex == 0xFFFFFFFFu) ? 0xFFFFFFFFu : (input.texIndex / 6u);
    bool is_water_material = (input.texIndex != 0xFFFFFFFFu && (float)block_index == material_ids.x);
    bool camera_submerged = water_params.x > 0.5f;
    bool water_volume_active = camera_submerged || water_params.z > 0.01f;
    if (input.normal == 0xFFFFFFFFu) {
        if (input.texIndex == 0xFFFFFFFFu) {
            return base;
        }
        base = blockTextures.Sample(fontSampler, float3(input.uv, (float)input.texIndex));
        /* Show texture multiplied by color (white font texture should show as white) */
        return base * input.color;
    }

    if (input.texIndex != 0xFFFFFFFFu) {
        base = sample_block_texture_filtered(input.texIndex, input.uv, anisotropyLevel);
        if (base.a <= (is_water_material ? 0.01f : 0.50f)) discard;
        base *= input.color;
        if (!is_water_material) {
            base.a = 1.0f;
        }
    }

    if (material_ids.z > 0.5f) {
        base.rgb = float3(0.0f, 0.0f, 0.0f);
        if (base.a < 1.0f) {
            base.a = max(base.a, 0.92f);
        }
    }

    float3 n = normalize(decode_face_normal(input.normal));
    float3 sun = normalize(sun_dir.xyz);
    float sun_term = saturate(dot(n, sun));
    float hemi_term = saturate(n.y * 0.5f + 0.5f);
    float brightness = lighting0.x * lerp(0.55f, 1.00f, hemi_term) + sun_term * 0.55f;

    float3 rgb = base.rgb * brightness;
    rgb = lerp(fog_color.rgb * 0.30f + rgb * 0.70f, sky_color.rgb, 0.12f * hemi_term);

    if (is_water_material) {
        float top_lit = saturate(dot(n, sun));
        rgb += top_lit * 0.08f;
        base.a *= lighting0.z;
    }

    float fog_factor = 0.0f;
    {
        float dist = distance(input.worldPos, camera_pos.xyz);
        fog_factor = saturate(1.0f - exp(-lighting0.y * dist));
        float water_fraction = water_volume_active
            ? water_segment_fraction(water_params.y, camera_pos.y, input.worldPos.y)
            : 0.0f;
        float water_path = dist * water_fraction;
        float water_density = lighting0.y * (camera_submerged ? 8.5f : 5.0f);
        float underwater_fog = saturate(1.0f - exp(-water_density * water_path));
        float underwater_depth = saturate(water_params.z / 12.0f);

        if (water_path > 0.001f) {
            float3 underwater_tint = lerp(water_color.rgb, fog_color.rgb, 0.35f);
            rgb = lerp(rgb, underwater_tint, underwater_fog * (0.70f + underwater_depth * 0.20f));
            if (!is_water_material) {
                rgb *= 1.0f - underwater_fog * (camera_submerged ? 0.22f : 0.10f);
            }
        }

        if (camera_submerged && !is_water_material) {
            float camera_depth = max(0.0f, water_params.y - camera_pos.y);
            float submerge_factor = saturate(camera_depth / 2.5f);
            rgb = lerp(rgb, water_color.rgb * 0.62f + fog_color.rgb * 0.18f, 0.16f + submerge_factor * 0.20f);
            fog_factor = saturate(max(fog_factor, underwater_fog));
        }

        rgb = lerp(rgb, fog_color.rgb, fog_factor);
    }

    rgb = saturate(rgb + fog_dither(input.worldPos) / 255.0f * (0.35f + fog_factor * 0.65f));
    return float4(rgb, base.a);
}
