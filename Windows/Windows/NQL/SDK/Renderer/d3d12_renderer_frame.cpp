/**
 * d3d12_renderer_frame.cpp -- Per-frame command recording and presentation.
 */
#include "d3d12_renderer_internal.h"
#include "../Core/API/Internal/sdk_api_internal.h"
#include "../Core/World/CoordinateSpaces/sdk_coordinate_space_runtime.h"
#include "../Core/World/Worldgen/Column/sdk_worldgen_column_internal.h"
#include "../Core/World/Superchunks/Config/sdk_superchunk_config.h"

#include <algorithm>

static int renderer_clampi(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int renderer_clamp_render_distance_chunks(int value)
{
    static const int presets[] = { 4, 6, 8, 10, 12, 16 };
    int best_index = 0;
    int best_distance = 0x7fffffff;

    for (int i = 0; i < (int)(sizeof(presets) / sizeof(presets[0])); ++i) {
        int distance = value - presets[i];
        if (distance < 0) distance = -distance;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return presets[best_index];
}

static int renderer_clamp_far_mesh_distance_chunks(int value)
{
    static const int presets[] = { 0, 2, 4, 6, 8, 10, 12, 16 };
    int best_index = 0;
    int best_distance = 0x7fffffff;

    for (int i = 0; i < (int)(sizeof(presets) / sizeof(presets[0])); ++i) {
        int distance = value - presets[i];
        if (distance < 0) distance = -distance;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return presets[best_index];
}

static int renderer_floor_div_i(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

static int camera_superchunk_x(void)
{
    if (!g_chunk_mgr) return 0;
    return renderer_floor_div_i(g_chunk_mgr->cam_cx, SDK_SUPERCHUNK_WALL_PERIOD);
}

static int camera_superchunk_z(void)
{
    if (!g_chunk_mgr) return 0;
    return renderer_floor_div_i(g_chunk_mgr->cam_cz, SDK_SUPERCHUNK_WALL_PERIOD);
}

static int superchunk_chebyshev_distance(int scx, int scz, int target_scx, int target_scz)
{
    int dx = scx - target_scx;
    int dz = scz - target_scz;

    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return (dx > dz) ? dx : dz;
}

static uint64_t renderer_mix_u64(uint64_t seed, uint64_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

static int chunk_chebyshev_distance(const SdkChunk* chunk)
{
    int dx;
    int dz;

    if (!chunk || !g_chunk_mgr) return 0;

    dx = chunk->cx - g_chunk_mgr->cam_cx;
    dz = chunk->cz - g_chunk_mgr->cam_cz;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return (dx > dz) ? dx : dz;
}

static int renderer_far_lod_distance_chunks(void)
{
    int render_distance = renderer_clamp_render_distance_chunks(g_rs.pause_menu.graphics_render_distance_chunks);
    int lod_distance = renderer_clamp_far_mesh_distance_chunks(g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks);
    if (lod_distance <= 0) return 0;
    if (lod_distance > render_distance) return render_distance;
    return lod_distance;
}

static int renderer_lod_exit_distance_chunks(int enter_distance)
{
    int exit_distance = enter_distance - 2;
    return (exit_distance > 0) ? exit_distance : 0;
}

static int renderer_experimental_far_mesh_distance_chunks(void)
{
    int render_distance = renderer_clamp_render_distance_chunks(g_rs.pause_menu.graphics_render_distance_chunks);
    int lod_distance = renderer_far_lod_distance_chunks();
    int experimental_distance = renderer_clamp_far_mesh_distance_chunks(
        g_rs.pause_menu.graphics_experimental_far_mesh_distance_chunks);

    if (experimental_distance <= 0) return 0;
    if (experimental_distance > render_distance) {
        experimental_distance = render_distance;
    }
    if (lod_distance > 0 && experimental_distance > lod_distance) {
        experimental_distance = lod_distance;
    }
    return experimental_distance;
}

static bool renderer_far_terrain_lod_enabled(void)
{
    return renderer_far_lod_distance_chunks() > 0;
}

static bool renderer_black_superchunk_walls_enabled(void)
{
    return g_rs.pause_menu.graphics_black_superchunk_walls;
}

static bool renderer_should_draw_player_character(void)
{
    return g_chunk_mgr &&
           !g_rs.editor_ui.open &&
           g_rs.player_character_cpu_vertices &&
           g_rs.player_character_cpu_count > 0;
}

static void transform_player_character_vertex(const BlockVertex* src, BlockVertex* dst)
{
    float cam_x = g_rs.camera.position.x;
    float cam_y = g_rs.camera.position.y;
    float cam_z = g_rs.camera.position.z;
    float dir_x = g_rs.camera.target.x - cam_x;
    float dir_z = g_rs.camera.target.z - cam_z;
    float dir_len = sqrtf(dir_x * dir_x + dir_z * dir_z);
    float fwd_x;
    float fwd_z;
    float right_x;
    float right_z;
    float origin_x;
    float origin_y;
    float origin_z;

    if (!src || !dst) return;

    if (dir_len <= 0.0001f) {
        fwd_x = 0.0f;
        fwd_z = 1.0f;
    } else {
        fwd_x = dir_x / dir_len;
        fwd_z = dir_z / dir_len;
    }
    right_x = fwd_z;
    right_z = -fwd_x;
    origin_x = cam_x;
    origin_y = cam_y - 1.62f;
    origin_z = cam_z;

    *dst = *src;
    dst->position[0] = origin_x + src->position[0] * right_x + src->position[2] * fwd_x;
    dst->position[1] = origin_y + src->position[1];
    dst->position[2] = origin_z + src->position[0] * right_z + src->position[2] * fwd_z;
}

static uint32_t preview_alpha_color(uint32_t rgb, uint8_t alpha)
{
    return (rgb & 0x00FFFFFFu) | ((uint32_t)alpha << 24);
}

static uint32_t preview_face_color(BlockType material, int face, bool valid, uint8_t alpha)
{
    if (!valid) {
        return preview_alpha_color(0x00FF5050u, alpha);
    }
    return preview_alpha_color(sdk_block_get_face_color(material, face), alpha);
}

static void preview_push_tri(std::vector<BlockVertex>& verts,
                             const BlockVertex& a,
                             const BlockVertex& b,
                             const BlockVertex& c)
{
    verts.push_back(a);
    verts.push_back(b);
    verts.push_back(c);
}

static void preview_push_quad(std::vector<BlockVertex>& verts,
                              const BlockVertex& v0,
                              const BlockVertex& v1,
                              const BlockVertex& v2,
                              const BlockVertex& v3)
{
    preview_push_tri(verts, v0, v1, v2);
    preview_push_tri(verts, v2, v1, v3);
}

static void preview_push_double_sided_quad(std::vector<BlockVertex>& verts,
                                           const BlockVertex& v0,
                                           const BlockVertex& v1,
                                           const BlockVertex& v2,
                                           const BlockVertex& v3)
{
    preview_push_quad(verts, v0, v1, v2, v3);
    preview_push_tri(verts, v2, v1, v0);
    preview_push_tri(verts, v3, v1, v2);
}

static void preview_box_face_vertices(BlockVertex out[4],
                                      int face,
                                      BlockType material,
                                      bool valid,
                                      float min_x, float min_y, float min_z,
                                      float max_x, float max_y, float max_z)
{
    uint32_t color = preview_face_color(material, face, valid, 112);

    switch (face) {
        case FACE_NEG_X:
            set_untextured_vertex(&out[0], min_x, max_y, max_z, color, FACE_NEG_X);
            set_untextured_vertex(&out[1], min_x, max_y, min_z, color, FACE_NEG_X);
            set_untextured_vertex(&out[2], min_x, min_y, max_z, color, FACE_NEG_X);
            set_untextured_vertex(&out[3], min_x, min_y, min_z, color, FACE_NEG_X);
            break;
        case FACE_POS_X:
            set_untextured_vertex(&out[0], max_x, max_y, min_z, color, FACE_POS_X);
            set_untextured_vertex(&out[1], max_x, max_y, max_z, color, FACE_POS_X);
            set_untextured_vertex(&out[2], max_x, min_y, min_z, color, FACE_POS_X);
            set_untextured_vertex(&out[3], max_x, min_y, max_z, color, FACE_POS_X);
            break;
        case FACE_NEG_Y:
            set_untextured_vertex(&out[0], min_x, min_y, min_z, color, FACE_NEG_Y);
            set_untextured_vertex(&out[1], max_x, min_y, min_z, color, FACE_NEG_Y);
            set_untextured_vertex(&out[2], min_x, min_y, max_z, color, FACE_NEG_Y);
            set_untextured_vertex(&out[3], max_x, min_y, max_z, color, FACE_NEG_Y);
            break;
        case FACE_POS_Y:
            set_untextured_vertex(&out[0], max_x, max_y, min_z, color, FACE_POS_Y);
            set_untextured_vertex(&out[1], min_x, max_y, min_z, color, FACE_POS_Y);
            set_untextured_vertex(&out[2], max_x, max_y, max_z, color, FACE_POS_Y);
            set_untextured_vertex(&out[3], min_x, max_y, max_z, color, FACE_POS_Y);
            break;
        case FACE_NEG_Z:
            set_untextured_vertex(&out[0], max_x, max_y, min_z, color, FACE_NEG_Z);
            set_untextured_vertex(&out[1], min_x, max_y, min_z, color, FACE_NEG_Z);
            set_untextured_vertex(&out[2], max_x, min_y, min_z, color, FACE_NEG_Z);
            set_untextured_vertex(&out[3], min_x, min_y, min_z, color, FACE_NEG_Z);
            break;
        case FACE_POS_Z:
        default:
            set_untextured_vertex(&out[0], min_x, max_y, max_z, color, FACE_POS_Z);
            set_untextured_vertex(&out[1], max_x, max_y, max_z, color, FACE_POS_Z);
            set_untextured_vertex(&out[2], min_x, min_y, max_z, color, FACE_POS_Z);
            set_untextured_vertex(&out[3], max_x, min_y, max_z, color, FACE_POS_Z);
            break;
    }
}

static void preview_emit_box(std::vector<BlockVertex>& verts,
                             BlockType material,
                             bool valid,
                             float min_x, float min_y, float min_z,
                             float max_x, float max_y, float max_z)
{
    for (int face = 0; face < 6; ++face) {
        BlockVertex quad[4];
        preview_box_face_vertices(quad, face, material, valid, min_x, min_y, min_z, max_x, max_y, max_z);
        preview_push_quad(verts, quad[0], quad[1], quad[2], quad[3]);
    }
}

static void preview_emit_payload(std::vector<BlockVertex>& verts,
                                 int wx, int wy, int wz,
                                 const SdkConstructionItemPayload* payload,
                                 bool valid)
{
    uint8_t bounds_min[3];
    uint8_t bounds_max[3];
    uint16_t occupied_count = 0u;
    BlockType material;

    if (!payload || payload->occupied_count == 0u) return;
    material = (BlockType)payload->material;
    if (material == BLOCK_AIR) return;

    sdk_construction_occupancy_bounds(payload->occupancy, &occupied_count, bounds_min, bounds_max);
    if (occupied_count == 0u) return;

    if (payload->item_identity_kind == SDK_CONSTRUCTION_ITEM_IDENTITY_UNORDERED_BOX) {
        preview_emit_box(verts,
                         material,
                         valid,
                         (float)wx + (float)bounds_min[0] / 16.0f,
                         (float)wy + (float)bounds_min[1] / 16.0f,
                         (float)wz + (float)bounds_min[2] / 16.0f,
                         (float)wx + (float)bounds_max[0] / 16.0f,
                         (float)wy + (float)bounds_max[1] / 16.0f,
                         (float)wz + (float)bounds_max[2] / 16.0f);
        return;
    }

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                float min_x;
                float min_y;
                float min_z;
                float max_x;
                float max_y;
                float max_z;

                if (!sdk_construction_occupancy_get(payload->occupancy, x, y, z)) continue;
                min_x = (float)wx + (float)x / 16.0f;
                min_y = (float)wy + (float)y / 16.0f;
                min_z = (float)wz + (float)z / 16.0f;
                max_x = (float)wx + (float)(x + 1) / 16.0f;
                max_y = (float)wy + (float)(y + 1) / 16.0f;
                max_z = (float)wz + (float)(z + 1) / 16.0f;

                if (x == 0 || !sdk_construction_occupancy_get(payload->occupancy, x - 1, y, z)) {
                    BlockVertex quad[4];
                    preview_box_face_vertices(quad, FACE_NEG_X, material, valid, min_x, min_y, min_z, max_x, max_y, max_z);
                    preview_push_quad(verts, quad[0], quad[1], quad[2], quad[3]);
                }
                if (x == 15 || !sdk_construction_occupancy_get(payload->occupancy, x + 1, y, z)) {
                    BlockVertex quad[4];
                    preview_box_face_vertices(quad, FACE_POS_X, material, valid, min_x, min_y, min_z, max_x, max_y, max_z);
                    preview_push_quad(verts, quad[0], quad[1], quad[2], quad[3]);
                }
                if (y == 0 || !sdk_construction_occupancy_get(payload->occupancy, x, y - 1, z)) {
                    BlockVertex quad[4];
                    preview_box_face_vertices(quad, FACE_NEG_Y, material, valid, min_x, min_y, min_z, max_x, max_y, max_z);
                    preview_push_quad(verts, quad[0], quad[1], quad[2], quad[3]);
                }
                if (y == 15 || !sdk_construction_occupancy_get(payload->occupancy, x, y + 1, z)) {
                    BlockVertex quad[4];
                    preview_box_face_vertices(quad, FACE_POS_Y, material, valid, min_x, min_y, min_z, max_x, max_y, max_z);
                    preview_push_quad(verts, quad[0], quad[1], quad[2], quad[3]);
                }
                if (z == 0 || !sdk_construction_occupancy_get(payload->occupancy, x, y, z - 1)) {
                    BlockVertex quad[4];
                    preview_box_face_vertices(quad, FACE_NEG_Z, material, valid, min_x, min_y, min_z, max_x, max_y, max_z);
                    preview_push_quad(verts, quad[0], quad[1], quad[2], quad[3]);
                }
                if (z == 15 || !sdk_construction_occupancy_get(payload->occupancy, x, y, z + 1)) {
                    BlockVertex quad[4];
                    preview_box_face_vertices(quad, FACE_POS_Z, material, valid, min_x, min_y, min_z, max_x, max_y, max_z);
                    preview_push_quad(verts, quad[0], quad[1], quad[2], quad[3]);
                }
            }
        }
    }
}

static void preview_face_rect_vertices(BlockVertex out[4],
                                       int wx, int wy, int wz,
                                       int face,
                                       float u0, float v0,
                                       float u1, float v1,
                                       float plane_epsilon,
                                       uint32_t color)
{
    float x0 = (float)wx;
    float y0 = (float)wy;
    float z0 = (float)wz;

    switch (face) {
        case FACE_POS_X:
            set_untextured_vertex(&out[0], x0 + plane_epsilon, y0 + v0, z0 + u0, color, FACE_NEG_X);
            set_untextured_vertex(&out[1], x0 + plane_epsilon, y0 + v0, z0 + u1, color, FACE_NEG_X);
            set_untextured_vertex(&out[2], x0 + plane_epsilon, y0 + v1, z0 + u0, color, FACE_NEG_X);
            set_untextured_vertex(&out[3], x0 + plane_epsilon, y0 + v1, z0 + u1, color, FACE_NEG_X);
            break;
        case FACE_NEG_X:
            set_untextured_vertex(&out[0], x0 + 1.0f - plane_epsilon, y0 + v0, z0 + u0, color, FACE_POS_X);
            set_untextured_vertex(&out[1], x0 + 1.0f - plane_epsilon, y0 + v0, z0 + u1, color, FACE_POS_X);
            set_untextured_vertex(&out[2], x0 + 1.0f - plane_epsilon, y0 + v1, z0 + u0, color, FACE_POS_X);
            set_untextured_vertex(&out[3], x0 + 1.0f - plane_epsilon, y0 + v1, z0 + u1, color, FACE_POS_X);
            break;
        case FACE_POS_Y:
            set_untextured_vertex(&out[0], x0 + u0, y0 + plane_epsilon, z0 + v0, color, FACE_NEG_Y);
            set_untextured_vertex(&out[1], x0 + u1, y0 + plane_epsilon, z0 + v0, color, FACE_NEG_Y);
            set_untextured_vertex(&out[2], x0 + u0, y0 + plane_epsilon, z0 + v1, color, FACE_NEG_Y);
            set_untextured_vertex(&out[3], x0 + u1, y0 + plane_epsilon, z0 + v1, color, FACE_NEG_Y);
            break;
        case FACE_NEG_Y:
            set_untextured_vertex(&out[0], x0 + u0, y0 + 1.0f - plane_epsilon, z0 + v0, color, FACE_POS_Y);
            set_untextured_vertex(&out[1], x0 + u1, y0 + 1.0f - plane_epsilon, z0 + v0, color, FACE_POS_Y);
            set_untextured_vertex(&out[2], x0 + u0, y0 + 1.0f - plane_epsilon, z0 + v1, color, FACE_POS_Y);
            set_untextured_vertex(&out[3], x0 + u1, y0 + 1.0f - plane_epsilon, z0 + v1, color, FACE_POS_Y);
            break;
        case FACE_POS_Z:
            set_untextured_vertex(&out[0], x0 + u0, y0 + v0, z0 + plane_epsilon, color, FACE_NEG_Z);
            set_untextured_vertex(&out[1], x0 + u1, y0 + v0, z0 + plane_epsilon, color, FACE_NEG_Z);
            set_untextured_vertex(&out[2], x0 + u0, y0 + v1, z0 + plane_epsilon, color, FACE_NEG_Z);
            set_untextured_vertex(&out[3], x0 + u1, y0 + v1, z0 + plane_epsilon, color, FACE_NEG_Z);
            break;
        case FACE_NEG_Z:
        default:
            set_untextured_vertex(&out[0], x0 + u0, y0 + v0, z0 + 1.0f - plane_epsilon, color, FACE_POS_Z);
            set_untextured_vertex(&out[1], x0 + u1, y0 + v0, z0 + 1.0f - plane_epsilon, color, FACE_POS_Z);
            set_untextured_vertex(&out[2], x0 + u0, y0 + v1, z0 + 1.0f - plane_epsilon, color, FACE_POS_Z);
            set_untextured_vertex(&out[3], x0 + u1, y0 + v1, z0 + 1.0f - plane_epsilon, color, FACE_POS_Z);
            break;
    }
}

static void preview_emit_face_grid(std::vector<BlockVertex>& verts,
                                   const SdkPlacementPreview& preview)
{
    const float line_half = 0.00125f;
    const float plane_epsilon = 0.0015f;
    const uint32_t grid_color = preview.valid ? 0x70FFFFFFu : 0x70FF8080u;
    const uint32_t highlight_color = preview.valid ? 0x38B0D8FFu : 0x48FF6060u;

    for (int i = 0; i <= 16; ++i) {
        float coord = (float)i / 16.0f;
        BlockVertex quad_u[4];
        BlockVertex quad_v[4];
        preview_face_rect_vertices(quad_u,
                                   preview.wx, preview.wy, preview.wz,
                                   preview.face,
                                   coord - line_half, 0.0f,
                                   coord + line_half, 1.0f,
                                   plane_epsilon,
                                   grid_color);
        preview_face_rect_vertices(quad_v,
                                   preview.wx, preview.wy, preview.wz,
                                   preview.face,
                                   0.0f, coord - line_half,
                                   1.0f, coord + line_half,
                                   plane_epsilon,
                                   grid_color);
        preview_push_double_sided_quad(verts, quad_u[0], quad_u[1], quad_u[2], quad_u[3]);
        preview_push_double_sided_quad(verts, quad_v[0], quad_v[1], quad_v[2], quad_v[3]);
    }

    {
        float u0 = (float)renderer_clampi(preview.face_u, 0, 15) / 16.0f;
        float v0 = (float)renderer_clampi(preview.face_v, 0, 15) / 16.0f;
        float u1 = u0 + (1.0f / 16.0f);
        float v1 = v0 + (1.0f / 16.0f);
        BlockVertex quad[4];
        preview_face_rect_vertices(quad,
                                   preview.wx, preview.wy, preview.wz,
                                   preview.face,
                                   u0, v0, u1, v1,
                                   plane_epsilon * 0.5f,
                                   highlight_color);
        preview_push_double_sided_quad(verts, quad[0], quad[1], quad[2], quad[3]);
    }
}

static void build_placement_preview_vertices(std::vector<BlockVertex>& verts)
{
    const SdkPlacementPreview& preview = g_rs.placement_preview;

    verts.clear();
    if (!preview.visible || preview.mode == SDK_PLACEMENT_PREVIEW_NONE ||
        preview.payload.occupied_count == 0u) {
        return;
    }

    preview_emit_payload(verts,
                         preview.wx, preview.wy, preview.wz,
                         &preview.payload,
                         preview.valid);
    preview_emit_face_grid(verts, preview);
}

static D3D12_GPU_VIRTUAL_ADDRESS renderer_lighting_cb_gpu_address(bool wall_black_pass)
{
    ID3D12Resource* lighting_cb = renderer_current_lighting_cb();
    D3D12_GPU_VIRTUAL_ADDRESS base;

    if (!lighting_cb) {
        return 0u;
    }
    base = lighting_cb->GetGPUVirtualAddress();
    return wall_black_pass ? (base + 256u) : base;
}

static bool chunk_intersects_wall_volume(const SdkChunk* chunk)
{
    const SdkSuperchunkConfig* config;
    SdkDerivedChunkSpaceInfo space_info;

    if (!chunk) return false;
    
    config = sdk_superchunk_get_config();
    if (!config || !config->enabled || !config->walls_enabled) return false;

    sdk_coordinate_space_describe_chunk(chunk->cx, chunk->cz, &space_info);
    return space_info.is_wall_position != 0;
}

static bool chunk_supports_active_superchunk_wall(const SdkChunk* chunk)
{
    const SdkSuperchunkConfig* config;

    if (!chunk || !g_chunk_mgr) return false;
    
    config = sdk_superchunk_get_config();
    if (!config || !config->enabled || !config->walls_enabled) return false;

    return sdk_superchunk_chunk_is_active_wall(g_chunk_mgr->primary_scx,
                                               g_chunk_mgr->primary_scz,
                                               chunk->cx,
                                               chunk->cz) != 0;
}

static bool superchunk_uses_wall_proxy(int scx, int scz)
{
    const SdkSuperchunkConfig* config;
    
    config = sdk_superchunk_get_config();
    if (!config || !config->walls_enabled) return false;
    
    if (!renderer_far_terrain_lod_enabled() || !g_chunk_mgr) return false;
    if (superchunk_chebyshev_distance(scx, scz, camera_superchunk_x(), camera_superchunk_z()) <= 1) {
        return false;
    }
    if (superchunk_chebyshev_distance(scx, scz, g_chunk_mgr->primary_scx, g_chunk_mgr->primary_scz) <= 1) {
        return false;
    }
    if (g_chunk_mgr->transition_active &&
        (superchunk_chebyshev_distance(scx, scz, g_chunk_mgr->desired_scx, g_chunk_mgr->desired_scz) <= 1 ||
         superchunk_chebyshev_distance(scx, scz, g_chunk_mgr->prev_scx, g_chunk_mgr->prev_scz) <= 1)) {
        return false;
    }
    return true;
}

static bool chunk_eligible_for_wall_proxy(const SdkChunk* chunk)
{
    int scx;
    int scz;

    if (!chunk || !chunk_intersects_wall_volume(chunk)) return false;
    if (chunk_supports_active_superchunk_wall(chunk)) return false;

    scx = renderer_floor_div_i(chunk->cx, SDK_SUPERCHUNK_WALL_PERIOD);
    scz = renderer_floor_div_i(chunk->cz, SDK_SUPERCHUNK_WALL_PERIOD);
    return superchunk_uses_wall_proxy(scx, scz);
}

static bool chunk_eligible_for_black_wall_shading(const SdkChunk* chunk)
{
    return chunk_intersects_wall_volume(chunk);
}

typedef enum {
    RENDERER_FAR_MESH_NONE = 0,
    RENDERER_FAR_MESH_EXPERIMENTAL,
    RENDERER_FAR_MESH_STABLE
} RendererFarMeshKind;

static const SdkChunkSubmesh* chunk_far_mesh_submesh(const SdkChunk* chunk, RendererFarMeshKind kind)
{
    if (!chunk) return nullptr;

    switch (kind) {
        case RENDERER_FAR_MESH_EXPERIMENTAL:
            return &chunk->experimental_far_mesh;
        case RENDERER_FAR_MESH_STABLE:
            return &chunk->far_mesh;
        case RENDERER_FAR_MESH_NONE:
        default:
            return nullptr;
    }
}

static RendererFarMeshKind chunk_far_mesh_kind(const SdkChunk* chunk)
{
    int scx;
    int scz;
    int distance;
    int stable_distance;
    int experimental_distance;
    bool unified_ready;

    if (!chunk || !g_chunk_mgr) return RENDERER_FAR_MESH_NONE;
    stable_distance = renderer_far_lod_distance_chunks();
    experimental_distance = renderer_experimental_far_mesh_distance_chunks();
    if (stable_distance <= 0 && experimental_distance <= 0) return RENDERER_FAR_MESH_NONE;
    if (chunk_intersects_wall_volume(chunk)) {
        return RENDERER_FAR_MESH_NONE;
    }
    scx = renderer_floor_div_i(chunk->cx, SDK_SUPERCHUNK_WALL_PERIOD);
    scz = renderer_floor_div_i(chunk->cz, SDK_SUPERCHUNK_WALL_PERIOD);
    if (superchunk_uses_wall_proxy(scx, scz)) {
        return RENDERER_FAR_MESH_NONE;
    }
    distance = chunk_chebyshev_distance(chunk);
    unified_ready = sdk_chunk_has_current_unified_gpu_mesh(chunk) != 0;

    if (experimental_distance > 0 &&
        distance >= experimental_distance &&
        distance < stable_distance &&
        unified_ready &&
        chunk->experimental_far_vertex_count > 0) {
        return RENDERER_FAR_MESH_EXPERIMENTAL;
    }
    if (stable_distance > 0 &&
        distance >= stable_distance &&
        unified_ready &&
        chunk->far_mesh_vertex_count > 0) {
        return RENDERER_FAR_MESH_STABLE;
    }
    if (experimental_distance > 0 &&
        distance >= experimental_distance &&
        unified_ready &&
        chunk->experimental_far_vertex_count > 0) {
        return RENDERER_FAR_MESH_EXPERIMENTAL;
    }
    return RENDERER_FAR_MESH_NONE;
}

static void chunk_default_bounds(const SdkChunk* chunk, float out_min[3], float out_max[3])
{
    float min_x;
    float min_y;
    float min_z;
    float max_x;
    float max_y;
    float max_z;

    if (!chunk || !out_min || !out_max) return;

    min_x = chunk->bounds_min[0];
    min_y = chunk->bounds_min[1];
    min_z = chunk->bounds_min[2];
    max_x = chunk->bounds_max[0];
    max_y = chunk->bounds_max[1];
    max_z = chunk->bounds_max[2];

    if (max_x <= min_x || max_y <= min_y || max_z <= min_z) {
        min_x = (float)(chunk->cx * CHUNK_WIDTH);
        min_y = 0.0f;
        min_z = (float)(chunk->cz * CHUNK_DEPTH);
        max_x = min_x + CHUNK_WIDTH;
        max_y = (float)CHUNK_HEIGHT;
        max_z = min_z + CHUNK_DEPTH;
    }

    out_min[0] = min_x;
    out_min[1] = min_y;
    out_min[2] = min_z;
    out_max[0] = max_x;
    out_max[1] = max_y;
    out_max[2] = max_z;
}

static bool submesh_bounds_valid(const SdkChunkSubmesh* sub)
{
    return sub &&
           sub->bounds_max[0] > sub->bounds_min[0] &&
           sub->bounds_max[1] > sub->bounds_min[1] &&
           sub->bounds_max[2] > sub->bounds_min[2];
}

static void chunk_representation_bounds(const SdkChunk* chunk,
                                        SdkChunkRenderRepresentation representation,
                                        float out_min[3],
                                        float out_max[3])
{
    if (!chunk || !out_min || !out_max) return;

    if (representation == SDK_CHUNK_RENDER_REPRESENTATION_FAR) {
        RendererFarMeshKind kind = chunk_far_mesh_kind(chunk);
        const SdkChunkSubmesh* far_sub = chunk_far_mesh_submesh(chunk, kind);
        if (submesh_bounds_valid(far_sub)) {
            memcpy(out_min, far_sub->bounds_min, sizeof(far_sub->bounds_min));
            memcpy(out_max, far_sub->bounds_max, sizeof(far_sub->bounds_max));
            return;
        }
    }

    chunk_default_bounds(chunk, out_min, out_max);
}

static bool chunk_representation_visible(const SdkChunk* chunk,
                                         SdkChunkRenderRepresentation representation)
{
    float bounds_min[3];
    float bounds_max[3];

    if (!chunk) return false;
    chunk_representation_bounds(chunk, representation, bounds_min, bounds_max);
    return sdk_frustum_contains_aabb(&g_rs.camera.frustum,
                                     bounds_min[0], bounds_min[1], bounds_min[2],
                                     bounds_max[0], bounds_max[1], bounds_max[2]);
}

static SdkChunkRenderRepresentation choose_chunk_render_representation(SdkChunkResidentSlot* resident)
{
    SdkChunk* chunk;
    SdkChunkRenderRepresentation previous;
    RendererFarMeshKind previous_far_kind;
    int stable_distance;
    int experimental_distance;
    int far_enter_distance;
    int exit_distance;
    int stable_exit_distance;
    int distance;
    int scx;
    int scz;
    bool wall_band;
    bool proxy_allowed;

    if (!resident || !resident->occupied) return SDK_CHUNK_RENDER_REPRESENTATION_FULL;
    chunk = &resident->chunk;
    if (!chunk->blocks) return SDK_CHUNK_RENDER_REPRESENTATION_FULL;

    previous = (SdkChunkRenderRepresentation)resident->render_representation;
    if (previous > SDK_CHUNK_RENDER_REPRESENTATION_PROXY) {
        previous = SDK_CHUNK_RENDER_REPRESENTATION_FULL;
    }
    previous_far_kind = (RendererFarMeshKind)resident->render_far_mesh_kind;
    if (previous_far_kind > RENDERER_FAR_MESH_STABLE) {
        previous_far_kind = RENDERER_FAR_MESH_NONE;
    }

    stable_distance = renderer_far_lod_distance_chunks();
    experimental_distance = renderer_experimental_far_mesh_distance_chunks();
    far_enter_distance = (experimental_distance > 0) ? experimental_distance : stable_distance;
    if (far_enter_distance <= 0 || !g_chunk_mgr) {
        return SDK_CHUNK_RENDER_REPRESENTATION_FULL;
    }

    exit_distance = renderer_lod_exit_distance_chunks(far_enter_distance);
    stable_exit_distance = renderer_lod_exit_distance_chunks(stable_distance);
    distance = chunk_chebyshev_distance(chunk);
    scx = renderer_floor_div_i(chunk->cx, SDK_SUPERCHUNK_WALL_PERIOD);
    scz = renderer_floor_div_i(chunk->cz, SDK_SUPERCHUNK_WALL_PERIOD);
    wall_band = chunk_intersects_wall_volume(chunk);
    proxy_allowed = wall_band && chunk_eligible_for_wall_proxy(chunk);

    if (proxy_allowed) {
        if (previous == SDK_CHUNK_RENDER_REPRESENTATION_PROXY) {
            if (distance >= exit_distance) return SDK_CHUNK_RENDER_REPRESENTATION_PROXY;
        } else if (distance >= stable_distance) {
            return SDK_CHUNK_RENDER_REPRESENTATION_PROXY;
        }
    }

    if (!wall_band) {
        if (previous == SDK_CHUNK_RENDER_REPRESENTATION_FAR) {
            if (previous_far_kind == RENDERER_FAR_MESH_STABLE) {
                if (distance >= stable_exit_distance) return SDK_CHUNK_RENDER_REPRESENTATION_FAR;
            } else if (distance >= exit_distance) {
                return SDK_CHUNK_RENDER_REPRESENTATION_FAR;
            }
        } else if (distance >= far_enter_distance) {
            return SDK_CHUNK_RENDER_REPRESENTATION_FAR;
        }
    }

    return SDK_CHUNK_RENDER_REPRESENTATION_FULL;
}

static void renderer_add_verts(int* io_total_verts, uint32_t vertex_count)
{
    if (!io_total_verts) return;
    *io_total_verts = (vertex_count > (uint32_t)(INT_MAX - *io_total_verts))
        ? INT_MAX
        : (*io_total_verts + (int)vertex_count);
}

static bool draw_chunk_far_mesh(const SdkChunk* chunk,
                                int* io_draw_calls,
                                int* io_total_verts)
{
    RendererFarMeshKind kind;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    ID3D12Resource* vb;
    uint32_t vertex_count;
    uint32_t offset;

    if (!sdk_chunk_has_current_unified_gpu_mesh(chunk)) return false;
    kind = chunk_far_mesh_kind(chunk);
    
    if (kind == RENDERER_FAR_MESH_STABLE) {
        vertex_count = chunk->far_mesh_vertex_count;
        offset = chunk->far_mesh_offset;
    } else {
        vertex_count = chunk->experimental_far_vertex_count;
        offset = chunk->experimental_far_offset;
    }
    
    if (vertex_count == 0) return false;

    vb = (ID3D12Resource*)chunk->unified_vertex_buffer;
    vbv.BufferLocation = vb->GetGPUVirtualAddress() + (offset * sizeof(BlockVertex));
    vbv.StrideInBytes = sizeof(BlockVertex);
    vbv.SizeInBytes = (UINT)(vertex_count * sizeof(BlockVertex));
    g_rs.cmd_list->IASetVertexBuffers(0, 1, &vbv);
    g_rs.cmd_list->DrawInstanced((UINT)vertex_count, 1, 0, 0);

    if (io_draw_calls) (*io_draw_calls)++;
    renderer_add_verts(io_total_verts, vertex_count);
    return true;
}

static bool draw_chunk_full_layer_mesh(const SdkChunk* chunk,
                                       bool water_pass,
                                       int* io_draw_calls,
                                       int* io_total_verts);

static bool draw_chunk_full_mesh(const SdkChunk* chunk,
                                 int* io_draw_calls,
                                 int* io_total_verts)
{
    return draw_chunk_full_layer_mesh(chunk, false, io_draw_calls, io_total_verts);
}

static bool draw_chunk_full_layer_mesh(const SdkChunk* chunk,
                                       bool water_pass,
                                       int* io_draw_calls,
                                       int* io_total_verts)
{
    bool drew_any = false;

    if (!sdk_chunk_has_current_unified_gpu_mesh(chunk)) return false;

    for (int sub_index = CHUNK_SUBCHUNK_COUNT - 1; sub_index >= 0; --sub_index) {
        uint32_t vertex_count = water_pass
            ? chunk->water_vertex_counts[sub_index]
            : chunk->subchunk_vertex_counts[sub_index];
        uint32_t offset = water_pass
            ? chunk->water_offsets[sub_index]
            : chunk->subchunk_offsets[sub_index];
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        ID3D12Resource* vb;

        if (vertex_count == 0) {
            continue;
        }
        if (!sdk_chunk_subchunk_is_visible(chunk, sub_index, &g_rs.camera)) {
            continue;
        }

        drew_any = true;
        vb = (ID3D12Resource*)chunk->unified_vertex_buffer;
        vbv.BufferLocation = vb->GetGPUVirtualAddress() + (offset * sizeof(BlockVertex));
        vbv.StrideInBytes = sizeof(BlockVertex);
        vbv.SizeInBytes = (UINT)(vertex_count * sizeof(BlockVertex));
        g_rs.cmd_list->IASetVertexBuffers(0, 1, &vbv);
        g_rs.cmd_list->DrawInstanced((UINT)vertex_count, 1, 0, 0);

        if (io_draw_calls) (*io_draw_calls)++;
        renderer_add_verts(io_total_verts, vertex_count);
    }

    return drew_any;
}

static bool draw_chunk_full_water_mesh(const SdkChunk* chunk,
                                       int* io_draw_calls,
                                       int* io_total_verts)
{
    return draw_chunk_full_layer_mesh(chunk, true, io_draw_calls, io_total_verts);
}

static bool chunk_has_full_water_mesh(const SdkChunk* chunk)
{
    if (!chunk) return false;
    for (int sub_index = 0; sub_index < CHUNK_SUBCHUNK_COUNT; ++sub_index) {
        if (chunk->water_vertex_counts[sub_index] > 0) {
            return true;
        }
    }
    return false;
}

struct VisibleChunkDraw {
    SdkChunkResidentSlot* resident;
    SdkChunkRenderRepresentation representation;
    bool black_walls;
    bool draw_full_water;
    float water_sort_distance_sq;
};

struct WallProxyColumnSample {
    bool     valid;
    bool     gate_open;
    float    lower_top;
    float    upper_bottom;
    float    upper_top;
    uint32_t lower_color;
    uint32_t upper_color;
};

struct WallProxyCacheEntry {
    bool            valid;
    int             scx;
    int             scz;
    uint64_t        signature;
    uint64_t        last_used;
    ID3D12Resource* vertex_buffer;
    uint32_t        vertex_count;
    float           bounds_min[3];
    float           bounds_max[3];
};

enum WallProxySide {
    WALL_PROXY_WEST = 0,
    WALL_PROXY_NORTH,
    WALL_PROXY_EAST,
    WALL_PROXY_SOUTH
};

static const int k_wall_proxy_cache_capacity = 16;
static const int k_wall_proxy_sample_step = 8;
static const float k_wall_proxy_cap_depth = 8.0f;
static WallProxyCacheEntry g_wall_proxy_cache[k_wall_proxy_cache_capacity];
static uint64_t g_wall_proxy_use_counter = 1u;

static bool renderer_full_superchunk_wall_proxy_mode(void)
{
    return renderer_clamp_render_distance_chunks(g_rs.pause_menu.graphics_render_distance_chunks) == 16;
}

static float chunk_water_sort_distance_sq(const SdkChunk* chunk)
{
    float center_x;
    float center_y;
    float center_z;
    float dx;
    float dy;
    float dz;

    if (!chunk) return 0.0f;

    if (chunk->bounds_min[0] != 0.0f || chunk->bounds_max[0] != 0.0f ||
        chunk->bounds_min[1] != 0.0f || chunk->bounds_max[1] != 0.0f ||
        chunk->bounds_min[2] != 0.0f || chunk->bounds_max[2] != 0.0f) {
        center_x = (chunk->bounds_min[0] + chunk->bounds_max[0]) * 0.5f;
        center_y = (chunk->bounds_min[1] + chunk->bounds_max[1]) * 0.5f;
        center_z = (chunk->bounds_min[2] + chunk->bounds_max[2]) * 0.5f;
    } else {
        center_x = (float)(chunk->cx * CHUNK_WIDTH) + (float)CHUNK_WIDTH * 0.5f;
        center_y = (float)CHUNK_HEIGHT * 0.5f;
        center_z = (float)(chunk->cz * CHUNK_DEPTH) + (float)CHUNK_DEPTH * 0.5f;
    }

    dx = center_x - g_rs.camera.position.x;
    dy = center_y - g_rs.camera.position.y;
    dz = center_z - g_rs.camera.position.z;
    return dx * dx + dy * dy + dz * dz;
}

static void release_wall_proxy_entry(WallProxyCacheEntry* entry, bool retire)
{
    if (!entry) return;
    if (entry->vertex_buffer) {
        if (retire) {
            retire_resource_later(entry->vertex_buffer);
        } else {
            entry->vertex_buffer->Release();
        }
        entry->vertex_buffer = nullptr;
    }
    memset(entry, 0, sizeof(*entry));
}

void release_wall_proxy_cache(void)
{
    for (int i = 0; i < k_wall_proxy_cache_capacity; ++i) {
        release_wall_proxy_entry(&g_wall_proxy_cache[i], false);
    }
    g_wall_proxy_use_counter = 1u;
}

static void push_wall_proxy_tri(std::vector<BlockVertex>& verts,
                                const BlockVertex& a,
                                const BlockVertex& b,
                                const BlockVertex& c)
{
    verts.push_back(a);
    verts.push_back(b);
    verts.push_back(c);
}

static void push_wall_proxy_quad(std::vector<BlockVertex>& verts,
                                 const BlockVertex& v0,
                                 const BlockVertex& v1,
                                 const BlockVertex& v2,
                                 const BlockVertex& v3)
{
    push_wall_proxy_tri(verts, v0, v1, v2);
    push_wall_proxy_tri(verts, v2, v1, v3);
}

static bool wall_face_block_filled(const SdkChunk* chunk, int lx, int ly, int lz, BlockType* out_block)
{
    BlockType block;

    if (out_block) *out_block = BLOCK_AIR;
    if (!chunk || !chunk->blocks || ly < 0 || ly >= CHUNK_HEIGHT) return false;
    block = sdk_chunk_get_block(chunk, lx, ly, lz);
    if (block == BLOCK_AIR) return false;
    if ((sdk_block_get_behavior_flags(block) & SDK_BLOCK_BEHAVIOR_FLUID) != 0u) {
        if (sdk_simulation_get_fluid_fill(chunk, lx, ly, lz) == 0u) return false;
    }
    if (out_block) *out_block = block;
    return true;
}

static bool wall_proxy_side_is_x_aligned(WallProxySide side)
{
    return side == WALL_PROXY_WEST || side == WALL_PROXY_EAST;
}

static uint32_t wall_proxy_side_face(WallProxySide side)
{
    switch (side) {
        case WALL_PROXY_WEST:
            return FACE_NEG_X;
        case WALL_PROXY_EAST:
            return FACE_POS_X;
        case WALL_PROXY_NORTH:
            return FACE_NEG_Z;
        case WALL_PROXY_SOUTH:
            return FACE_POS_Z;
        default:
            return FACE_NEG_Z;
    }
}

static SdkSuperchunkWallFaceMask wall_proxy_side_superchunk_face(WallProxySide side)
{
    switch (side) {
        case WALL_PROXY_WEST:  return SDK_SUPERCHUNK_WALL_FACE_WEST;
        case WALL_PROXY_EAST:  return SDK_SUPERCHUNK_WALL_FACE_EAST;
        case WALL_PROXY_NORTH: return SDK_SUPERCHUNK_WALL_FACE_NORTH;
        case WALL_PROXY_SOUTH:
        default:               return SDK_SUPERCHUNK_WALL_FACE_SOUTH;
    }
}

static int wall_proxy_side_gate_index(WallProxySide side)
{
    switch (side) {
        case WALL_PROXY_WEST:  return 0;
        case WALL_PROXY_NORTH: return 1;
        case WALL_PROXY_EAST:  return 2;
        case WALL_PROXY_SOUTH:
        default:               return 3;
    }
}

static bool wall_proxy_canonical_profile(int scx,
                                         int scz,
                                         int wx,
                                         int wz,
                                         WallProxySide side,
                                         int* out_wall_top_y,
                                         int* out_gate_floor_y,
                                         int* out_arch_top_y)
{
    SdkTerrainColumnProfile terrain_profile;
    SdkSuperChunkWallProfile wall_profile;
    SdkSuperchunkCell cell;
    const SdkSuperchunkWallFaceMask face = wall_proxy_side_superchunk_face(side);
    int local_super_x;
    int local_super_z;
    int run_local;
    int origin_world_x = 0;
    int origin_world_z = 0;
    const int wall_period_blocks = sdk_superchunk_wall_period_blocks();
    int gate_floor_y = -1;
    int arch_top_y = -1;
    bool on_face = false;

    if (out_wall_top_y) *out_wall_top_y = -1;
    if (out_gate_floor_y) *out_gate_floor_y = -1;
    if (out_arch_top_y) *out_arch_top_y = -1;
    if (!g_sdk.worldgen.impl) return false;
    if (!sdk_worldgen_sample_column_ctx(&g_sdk.worldgen, wx, wz, &terrain_profile)) return false;
    if (!compute_superchunk_wall_profile(&g_sdk.worldgen, &terrain_profile, wx, wz, &wall_profile)) return false;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    sdk_superchunk_cell_origin_blocks(&cell, &origin_world_x, &origin_world_z);
    sdk_superchunk_world_block_local_to_cell(scx, scz, wx, wz, &local_super_x, &local_super_z);
    if (out_wall_top_y) *out_wall_top_y = wall_profile.wall_top_y;

    run_local = sdk_superchunk_wall_face_run_local(face, local_super_x, local_super_z);
    switch (face) {
        case SDK_SUPERCHUNK_WALL_FACE_WEST:
            on_face = local_super_x >= 0 && local_super_x < CHUNK_WIDTH;
            break;
        case SDK_SUPERCHUNK_WALL_FACE_EAST:
            on_face = local_super_x >= wall_period_blocks &&
                      local_super_x < wall_period_blocks + CHUNK_WIDTH;
            break;
        case SDK_SUPERCHUNK_WALL_FACE_NORTH:
            on_face = local_super_z >= 0 && local_super_z < CHUNK_DEPTH;
            break;
        case SDK_SUPERCHUNK_WALL_FACE_SOUTH:
            on_face = local_super_z >= wall_period_blocks &&
                      local_super_z < wall_period_blocks + CHUNK_DEPTH;
            break;
        case SDK_SUPERCHUNK_WALL_FACE_NONE:
        default:
            break;
    }

    if (on_face && sdk_superchunk_gate_contains_block_run(run_local)) {
        gate_floor_y = sdk_superchunk_gate_floor_y_for_side_ctx(&g_sdk.worldgen,
                                                                origin_world_x,
                                                                origin_world_z,
                                                                wall_proxy_side_gate_index(side));
        if (gate_floor_y > 0) {
            arch_top_y = sdk_superchunk_gate_arch_top_y(run_local, gate_floor_y);
        }
    }

    if (out_gate_floor_y) *out_gate_floor_y = gate_floor_y;
    if (out_arch_top_y) *out_arch_top_y = arch_top_y;
    return true;
}

static void sample_wall_face_column(int scx, int scz, WallProxySide side, int run_local,
                                    WallProxyColumnSample* out_sample)
{
    int cx;
    int cz;
    int lx;
    int lz;
    int wx;
    int wz;
    int face;
    int top_y = -1;
    int wall_top_y = -1;
    int gate_floor_y = -1;
    int arch_top_y = -1;
    uint32_t upper_color = 0xFF7C7C7Cu;
    uint32_t lower_color = 0xFF7C7C7Cu;
    SdkChunk* chunk;
    int y;

    if (!out_sample) return;
    memset(out_sample, 0, sizeof(*out_sample));
    if (!g_chunk_mgr) return;

    face = (int)wall_proxy_side_face(side);
    switch (side) {
        case WALL_PROXY_WEST:
            sdk_superchunk_wall_edge_chunk_for_run(scx, scz, SDK_SUPERCHUNK_WALL_FACE_WEST,
                                                   run_local / CHUNK_DEPTH, &cx, &cz);
            lx = 0;
            lz = run_local % CHUNK_DEPTH;
            break;
        case WALL_PROXY_EAST:
            sdk_superchunk_wall_edge_chunk_for_run(scx, scz, SDK_SUPERCHUNK_WALL_FACE_EAST,
                                                   run_local / CHUNK_DEPTH, &cx, &cz);
            lx = 0;
            lz = run_local % CHUNK_DEPTH;
            break;
        case WALL_PROXY_NORTH:
            sdk_superchunk_wall_edge_chunk_for_run(scx, scz, SDK_SUPERCHUNK_WALL_FACE_NORTH,
                                                   run_local / CHUNK_WIDTH, &cx, &cz);
            lx = run_local % CHUNK_WIDTH;
            lz = 0;
            break;
        case WALL_PROXY_SOUTH:
        default:
            sdk_superchunk_wall_edge_chunk_for_run(scx, scz, SDK_SUPERCHUNK_WALL_FACE_SOUTH,
                                                   run_local / CHUNK_WIDTH, &cx, &cz);
            lx = run_local % CHUNK_WIDTH;
            lz = 0;
            break;
    }

    wx = cx * CHUNK_WIDTH + lx;
    wz = cz * CHUNK_DEPTH + lz;

    chunk = sdk_chunk_manager_get_chunk(g_chunk_mgr, cx, cz);
    if (!chunk || !chunk->blocks) return;
    wall_proxy_canonical_profile(scx, scz, wx, wz, side, &wall_top_y, &gate_floor_y, &arch_top_y);

    for (y = 0; y < CHUNK_HEIGHT; ++y) {
        BlockType block = BLOCK_AIR;
        bool filled = wall_face_block_filled(chunk, lx, y, lz, &block);

        if (filled) {
            top_y = y;
            upper_color = sdk_block_get_face_color(block, face);
        }
    }

    if (top_y < 0) return;

    out_sample->valid = true;
    out_sample->upper_top = (float)((wall_top_y >= 0) ? (wall_top_y + 1) : (top_y + 1));
    out_sample->lower_top = out_sample->upper_top;
    out_sample->upper_bottom = out_sample->upper_top;
    out_sample->lower_color = upper_color;
    out_sample->upper_color = upper_color;

    if (gate_floor_y > 0 && arch_top_y >= gate_floor_y) {
        for (y = gate_floor_y - 1; y >= 0; --y) {
            BlockType block = BLOCK_AIR;
            if (wall_face_block_filled(chunk, lx, y, lz, &block)) {
                lower_color = sdk_block_get_face_color(block, face);
                break;
            }
        }

        out_sample->gate_open = true;
        out_sample->lower_top = (float)gate_floor_y;
        out_sample->upper_bottom = (float)(arch_top_y + 1);
        out_sample->lower_color = lower_color;
    }
}

static void emit_wall_proxy_vertical_strip(std::vector<BlockVertex>& verts,
                                           bool x_aligned,
                                           uint32_t face,
                                           float fixed_coord,
                                           float run0,
                                           float run1,
                                           float bottom0,
                                           float bottom1,
                                           float top0,
                                           float top1,
                                           uint32_t color)
{
    BlockVertex v0;
    BlockVertex v1;
    BlockVertex v2;
    BlockVertex v3;

    if (top0 <= bottom0 + 0.01f && top1 <= bottom1 + 0.01f) return;

    if (x_aligned) {
        set_untextured_vertex(&v0, fixed_coord, top0, run0, color, face);
        set_untextured_vertex(&v1, fixed_coord, top1, run1, color, face);
        set_untextured_vertex(&v2, fixed_coord, bottom0, run0, color, face);
        set_untextured_vertex(&v3, fixed_coord, bottom1, run1, color, face);
    } else {
        set_untextured_vertex(&v0, run0, top0, fixed_coord, color, face);
        set_untextured_vertex(&v1, run1, top1, fixed_coord, color, face);
        set_untextured_vertex(&v2, run0, bottom0, fixed_coord, color, face);
        set_untextured_vertex(&v3, run1, bottom1, fixed_coord, color, face);
    }
    push_wall_proxy_quad(verts, v0, v1, v2, v3);
}

static void emit_wall_proxy_cap(std::vector<BlockVertex>& verts,
                                bool x_aligned,
                                float fixed_coord,
                                float run0,
                                float run1,
                                float top0,
                                float top1,
                                uint32_t color)
{
    BlockVertex v0;
    BlockVertex v1;
    BlockVertex v2;
    BlockVertex v3;

    if (top0 <= 0.01f && top1 <= 0.01f) return;

    if (x_aligned) {
        set_untextured_vertex(&v0, fixed_coord, top0, run0, color, FACE_POS_Y);
        set_untextured_vertex(&v1, fixed_coord + k_wall_proxy_cap_depth, top0, run0, color, FACE_POS_Y);
        set_untextured_vertex(&v2, fixed_coord, top1, run1, color, FACE_POS_Y);
        set_untextured_vertex(&v3, fixed_coord + k_wall_proxy_cap_depth, top1, run1, color, FACE_POS_Y);
    } else {
        set_untextured_vertex(&v0, run0, top0, fixed_coord, color, FACE_POS_Y);
        set_untextured_vertex(&v1, run0, top0, fixed_coord + k_wall_proxy_cap_depth, color, FACE_POS_Y);
        set_untextured_vertex(&v2, run1, top1, fixed_coord, color, FACE_POS_Y);
        set_untextured_vertex(&v3, run1, top1, fixed_coord + k_wall_proxy_cap_depth, color, FACE_POS_Y);
    }
    push_wall_proxy_quad(verts, v0, v1, v2, v3);
}

static void build_wall_proxy_side(std::vector<BlockVertex>& verts, int scx, int scz, WallProxySide side)
{
    const int sample_count = 1025;  /* Maximum for 128x128 superchunk with step 1 */
    WallProxyColumnSample samples[1025];
    SdkSuperchunkCell cell;
    int origin_block_x = 0;
    int origin_block_z = 0;
    int east_block_x = 0;
    int south_block_z = 0;
    float origin_world_x;
    float origin_world_z;
    bool x_aligned = wall_proxy_side_is_x_aligned(side);
    uint32_t face = wall_proxy_side_face(side);
    float fixed_coord;
    float run_origin;

    sdk_superchunk_cell_from_index(scx, scz, &cell);
    sdk_superchunk_cell_origin_blocks(&cell, &origin_block_x, &origin_block_z);
    sdk_superchunk_cell_edge_blocks(&cell, &east_block_x, &south_block_z);
    origin_world_x = (float)origin_block_x;
    origin_world_z = (float)origin_block_z;

    if (side == WALL_PROXY_WEST) {
        fixed_coord = origin_world_x;
    } else if (side == WALL_PROXY_EAST) {
        fixed_coord = (float)east_block_x;
    } else if (side == WALL_PROXY_NORTH) {
        fixed_coord = origin_world_z;
    } else {
        fixed_coord = (float)south_block_z;
    }
    run_origin = x_aligned ? origin_world_z : origin_world_x;

    for (int i = 0; i < sample_count; ++i) {
        sample_wall_face_column(scx, scz, side, i * k_wall_proxy_sample_step, &samples[i]);
    }

    for (int i = 0; i < sample_count - 1; ++i) {
        const WallProxyColumnSample& s0 = samples[i];
        const WallProxyColumnSample& s1 = samples[i + 1];
        float run0 = run_origin + (float)(i * k_wall_proxy_sample_step);
        float run1 = run_origin + (float)((i + 1) * k_wall_proxy_sample_step);

        if (!s0.valid && !s1.valid) continue;

        if (!s0.gate_open && !s1.gate_open) {
            emit_wall_proxy_vertical_strip(verts, x_aligned, face, fixed_coord, run0, run1,
                                           0.0f, 0.0f, s0.upper_top, s1.upper_top,
                                           s0.upper_color);
        } else {
            emit_wall_proxy_vertical_strip(verts, x_aligned, face, fixed_coord, run0, run1,
                                           0.0f, 0.0f, s0.lower_top, s1.lower_top,
                                           s0.lower_color);
            emit_wall_proxy_vertical_strip(verts, x_aligned, face, fixed_coord, run0, run1,
                                           s0.upper_bottom, s1.upper_bottom, s0.upper_top, s1.upper_top,
                                           s0.upper_color);
        }
        emit_wall_proxy_cap(verts, x_aligned, fixed_coord, run0, run1, s0.upper_top, s1.upper_top, s0.upper_color);
    }
}

static uint64_t wall_proxy_signature_for_superchunk(int scx, int scz, bool* out_has_any)
{
    SdkSuperchunkCell cell;
    bool has_any = false;
    uint64_t signature = 1469598103934665603ull;

    if (!g_chunk_mgr) {
        if (out_has_any) *out_has_any = false;
        return 0u;
    }
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    for (int run = 0; run < SDK_SUPERCHUNK_CHUNK_SPAN; ++run) {
        const int run_cx = cell.interior_min_cx + run;
        const int run_cz = cell.interior_min_cz + run;
        const SdkChunk* west = sdk_chunk_manager_get_chunk(g_chunk_mgr, cell.origin_cx, run_cz);
        const SdkChunk* north = sdk_chunk_manager_get_chunk(g_chunk_mgr, run_cx, cell.origin_cz);
        const SdkChunk* east = sdk_chunk_manager_get_chunk(g_chunk_mgr, cell.east_cx, run_cz);
        const SdkChunk* south = sdk_chunk_manager_get_chunk(g_chunk_mgr, run_cx, cell.south_cz);

        signature = renderer_mix_u64(signature, (uint64_t)(uint32_t)run_cz);
        signature = renderer_mix_u64(signature, west ? west->mesh_job_generation : 0u);
        signature = renderer_mix_u64(signature, west ? west->dirty_subchunks_mask : 0u);
        if (west && west->blocks) has_any = true;

        signature = renderer_mix_u64(signature, (uint64_t)(uint32_t)run_cx);
        signature = renderer_mix_u64(signature, north ? north->mesh_job_generation : 0u);
        signature = renderer_mix_u64(signature, north ? north->dirty_subchunks_mask : 0u);
        if (north && north->blocks) has_any = true;

        if (renderer_full_superchunk_wall_proxy_mode()) {
            signature = renderer_mix_u64(signature, (uint64_t)(uint32_t)run_cz);
            signature = renderer_mix_u64(signature, east ? east->mesh_job_generation : 0u);
            signature = renderer_mix_u64(signature, east ? east->dirty_subchunks_mask : 0u);
            if (east && east->blocks) has_any = true;

            signature = renderer_mix_u64(signature, (uint64_t)(uint32_t)run_cx);
            signature = renderer_mix_u64(signature, south ? south->mesh_job_generation : 0u);
            signature = renderer_mix_u64(signature, south ? south->dirty_subchunks_mask : 0u);
            if (south && south->blocks) has_any = true;
        }
    }

    if (out_has_any) *out_has_any = has_any;
    return signature;
}

static WallProxyCacheEntry* find_wall_proxy_entry(int scx, int scz)
{
    for (int i = 0; i < k_wall_proxy_cache_capacity; ++i) {
        if (g_wall_proxy_cache[i].valid &&
            g_wall_proxy_cache[i].scx == scx &&
            g_wall_proxy_cache[i].scz == scz) {
            return &g_wall_proxy_cache[i];
        }
    }
    return nullptr;
}

static WallProxyCacheEntry* alloc_wall_proxy_entry(int scx, int scz)
{
    WallProxyCacheEntry* best = nullptr;

    for (int i = 0; i < k_wall_proxy_cache_capacity; ++i) {
        if (!g_wall_proxy_cache[i].valid) {
            best = &g_wall_proxy_cache[i];
            break;
        }
        if (!best || g_wall_proxy_cache[i].last_used < best->last_used) {
            best = &g_wall_proxy_cache[i];
        }
    }

    if (!best) return nullptr;
    release_wall_proxy_entry(best, true);
    best->scx = scx;
    best->scz = scz;
    return best;
}

static bool upload_wall_proxy_vertices(WallProxyCacheEntry* entry, const std::vector<BlockVertex>& verts)
{
    D3D12_HEAP_PROPERTIES hp = {};
    D3D12_RESOURCE_DESC rd = {};
    D3D12_RANGE read_range = {0, 0};
    ID3D12Resource* vb = nullptr;
    void* mapped = nullptr;
    size_t vb_size;
    float min_x;
    float min_y;
    float min_z;
    float max_x;
    float max_y;
    float max_z;

    if (!entry) return false;
    if (entry->vertex_buffer) {
        retire_resource_later(entry->vertex_buffer);
        entry->vertex_buffer = nullptr;
    }
    entry->vertex_count = 0;
    memset(entry->bounds_min, 0, sizeof(entry->bounds_min));
    memset(entry->bounds_max, 0, sizeof(entry->bounds_max));
    if (verts.empty()) {
        entry->valid = false;
        return false;
    }

    vb_size = verts.size() * sizeof(BlockVertex);
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = vb_size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(g_rs.device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&vb)))) {
        entry->valid = false;
        return false;
    }
    if (FAILED(vb->Map(0, &read_range, &mapped))) {
        vb->Release();
        entry->valid = false;
        return false;
    }
    memcpy(mapped, verts.data(), vb_size);
    vb->Unmap(0, nullptr);

    min_x = min_y = min_z = FLT_MAX;
    max_x = max_y = max_z = -FLT_MAX;
    for (size_t i = 0; i < verts.size(); ++i) {
        const BlockVertex& v = verts[i];
        if (v.position[0] < min_x) min_x = v.position[0];
        if (v.position[1] < min_y) min_y = v.position[1];
        if (v.position[2] < min_z) min_z = v.position[2];
        if (v.position[0] > max_x) max_x = v.position[0];
        if (v.position[1] > max_y) max_y = v.position[1];
        if (v.position[2] > max_z) max_z = v.position[2];
    }

    entry->vertex_buffer = vb;
    entry->vertex_count = (uint32_t)verts.size();
    entry->bounds_min[0] = min_x;
    entry->bounds_min[1] = min_y;
    entry->bounds_min[2] = min_z;
    entry->bounds_max[0] = max_x;
    entry->bounds_max[1] = max_y;
    entry->bounds_max[2] = max_z;
    entry->valid = true;
    return true;
}

static WallProxyCacheEntry* ensure_wall_proxy(int scx, int scz)
{
    bool has_any = false;
    uint64_t signature = wall_proxy_signature_for_superchunk(scx, scz, &has_any);
    WallProxyCacheEntry* entry;
    std::vector<BlockVertex> verts;

    if (!has_any) return nullptr;

    entry = find_wall_proxy_entry(scx, scz);
    if (entry && entry->valid && entry->signature == signature && entry->vertex_buffer && entry->vertex_count > 0) {
        entry->last_used = g_wall_proxy_use_counter++;
        return entry;
    }
    if (!entry) {
        entry = alloc_wall_proxy_entry(scx, scz);
    }
    if (!entry) return nullptr;

    build_wall_proxy_side(verts, scx, scz, WALL_PROXY_WEST);
    build_wall_proxy_side(verts, scx, scz, WALL_PROXY_NORTH);
    if (renderer_full_superchunk_wall_proxy_mode()) {
        build_wall_proxy_side(verts, scx, scz, WALL_PROXY_EAST);
        build_wall_proxy_side(verts, scx, scz, WALL_PROXY_SOUTH);
    }
    if (!upload_wall_proxy_vertices(entry, verts)) {
        return nullptr;
    }

    entry->signature = signature;
    entry->last_used = g_wall_proxy_use_counter++;
    return entry;
}

static bool wall_proxy_is_visible(const WallProxyCacheEntry* entry)
{
    if (!entry || !entry->valid || !entry->vertex_buffer || entry->vertex_count == 0) return false;
    return sdk_frustum_contains_aabb(&g_rs.camera.frustum,
                                     entry->bounds_min[0], entry->bounds_min[1], entry->bounds_min[2],
                                     entry->bounds_max[0], entry->bounds_max[1], entry->bounds_max[2]);
}

static void draw_visible_wall_proxies(int* io_draw_calls, int* io_total_verts, bool black_walls)
{
    int superchunks_x[k_wall_proxy_cache_capacity];
    int superchunks_z[k_wall_proxy_cache_capacity];
    int superchunk_count = 0;

    if (!renderer_far_terrain_lod_enabled() || !g_chunk_mgr) return;

    g_rs.cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_rs.cmd_list->SetGraphicsRootConstantBufferView(1, renderer_lighting_cb_gpu_address(black_walls));

    for (int slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        SdkChunkResidentSlot* resident = sdk_chunk_manager_get_slot_at(g_chunk_mgr, slot_index);
        SdkChunk* chunk;
        bool seen = false;

        if (!resident || !resident->occupied) continue;
        chunk = &resident->chunk;
        if (!chunk->blocks) continue;
        if (!chunk_eligible_for_wall_proxy(chunk)) continue;
        if ((SdkChunkRenderRepresentation)resident->render_representation !=
            SDK_CHUNK_RENDER_REPRESENTATION_PROXY) {
            continue;
        }

        {
            int scx = renderer_floor_div_i(chunk->cx, SDK_SUPERCHUNK_WALL_PERIOD);
            int scz = renderer_floor_div_i(chunk->cz, SDK_SUPERCHUNK_WALL_PERIOD);
            for (int i = 0; i < superchunk_count; ++i) {
                if (superchunks_x[i] == scx && superchunks_z[i] == scz) {
                    seen = true;
                    break;
                }
            }
            if (!seen && superchunk_count < k_wall_proxy_cache_capacity) {
                superchunks_x[superchunk_count] = scx;
                superchunks_z[superchunk_count] = scz;
                superchunk_count++;
            }
        }
    }

    for (int i = 0; i < superchunk_count; ++i) {
        WallProxyCacheEntry* entry = ensure_wall_proxy(superchunks_x[i], superchunks_z[i]);
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        ID3D12Resource* vb;

        if (!wall_proxy_is_visible(entry)) continue;

        vb = entry->vertex_buffer;
        vbv.BufferLocation = vb->GetGPUVirtualAddress();
        vbv.StrideInBytes = sizeof(BlockVertex);
        vbv.SizeInBytes = (UINT)(entry->vertex_count * sizeof(BlockVertex));
        g_rs.cmd_list->IASetVertexBuffers(0, 1, &vbv);
        g_rs.cmd_list->DrawInstanced((UINT)entry->vertex_count, 1, 0, 0);

        if (io_draw_calls) (*io_draw_calls)++;
        if (io_total_verts) (*io_total_verts) += (int)entry->vertex_count;
    }
}

extern "C" SdkResult sdk_renderer_frame(void)
{
    if (!g_rs.initialized) return SDK_ERR_NOT_INIT;
    if (g_rs.width == 0 || g_rs.height == 0) return SDK_OK;

    UINT fi = g_rs.frame_index;
    LARGE_INTEGER frame_start;
    LARGE_INTEGER render_start;
    LARGE_INTEGER frame_end;
    LARGE_INTEGER render_end;

    /* Wait for the previous submission using this allocator to complete */
    UINT64 last_submitted = g_rs.fence_values[fi];
    if (last_submitted > 0) {
        SdkWaitForFence(g_rs.fence.Get(), last_submitted, g_rs.fence_event);
    }
    reclaim_retired_resources();
    reset_perf_stats();
    g_rs.perf_upload_bytes = g_rs.perf_upload_bytes_pending;
    g_rs.perf_upload_bytes_pending = 0u;
    QueryPerformanceCounter(&frame_start);

    /* Reset allocator + command list */
    HR_CHECK(g_rs.cmd_alloc[fi]->Reset());
    HR_CHECK(g_rs.cmd_list->Reset(g_rs.cmd_alloc[fi].Get(), g_rs.pso.Get()));
    g_rs.cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    QueryPerformanceCounter(&render_start);

    /* Animation disabled - camera stays fixed for now */
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (g_rs.anim_started) {
        g_rs.frame_ms = qpc_ms(g_rs.last_time, now);
    }
    g_rs.last_time = now;
    g_rs.anim_started = true;

    /* Update camera matrices (column-major, right-handed) */
    sdk_camera_update(&g_rs.camera);

    /* Upload view_projection as MVP to constant buffer */
    memcpy(renderer_current_constant_buffer_mapped(), &g_rs.camera.view_projection, sizeof(SdkMat4));

    synthesize_atmosphere_from_lighting();
    write_atmosphere_constant_buffer();
    g_rs.clear_color[0] = g_rs.atmosphere_sky_r;
    g_rs.clear_color[1] = g_rs.atmosphere_sky_g;
    g_rs.clear_color[2] = g_rs.atmosphere_sky_b;
    g_rs.clear_color[3] = 1.0f;

    /* Set root signature first (required before binding CBV) */
    g_rs.cmd_list->SetGraphicsRootSignature(g_rs.root_sig.Get());
    if (g_rs.texture_srv_heap) {
        ID3D12DescriptorHeap* heaps[] = { g_rs.texture_srv_heap.Get() };
        g_rs.cmd_list->SetDescriptorHeaps(1, heaps);
        g_rs.cmd_list->SetGraphicsRootDescriptorTable(
            2, g_rs.texture_srv_heap->GetGPUDescriptorHandleForHeapStart());
    }

    /* Bind constant buffers: 0=MVP, 1=Lighting */
    D3D12_GPU_VIRTUAL_ADDRESS cb_gpu = renderer_current_constant_buffer()->GetGPUVirtualAddress();
    g_rs.cmd_list->SetGraphicsRootConstantBufferView(0, cb_gpu);
    g_rs.cmd_list->SetGraphicsRootConstantBufferView(1, renderer_lighting_cb_gpu_address(false));

    D3D12_VIEWPORT vp = {};
    vp.Width    = (float)g_rs.width;
    vp.Height   = (float)g_rs.height;
    vp.MaxDepth = 1.0f;
    g_rs.cmd_list->RSSetViewports(1, &vp);

    D3D12_RECT scissor = { 0, 0, (LONG)g_rs.width, (LONG)g_rs.height };
    g_rs.cmd_list->RSSetScissorRects(1, &scissor);

    /* Transition render target: PRESENT → RENDER_TARGET */
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = g_rs.render_targets[fi].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_rs.cmd_list->ResourceBarrier(1, &barrier);

    /* Get RTV handle */
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle =
        g_rs.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv_handle.ptr += (SIZE_T)(fi * g_rs.rtv_descriptor_size);

    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle =
        g_rs.dsv_heap->GetCPUDescriptorHandleForHeapStart();
    g_rs.cmd_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &dsv_handle);

    /* Clear render target and depth buffer */
    g_rs.cmd_list->ClearRenderTargetView(rtv_handle, g_rs.clear_color, 0, nullptr);
    g_rs.cmd_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    /* Draw all chunks with valid vertex buffers */
    if (g_chunk_mgr) {
        std::vector<VisibleChunkDraw> visible_draws;
        g_rs.cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        int chunks_visible = 0;
        int chunks_renderable = 0;
        int chunks_drawn = 0;
        int subchunks_drawn = 0;
        int total_verts = 0;
        visible_draws.reserve((size_t)sdk_chunk_manager_active_count(g_chunk_mgr));

        for (int slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
            SdkChunkResidentSlot* resident = sdk_chunk_manager_get_slot_at(g_chunk_mgr, slot_index);
            SdkChunk* chunk;
            SdkChunkRenderRepresentation representation;
            SdkChunkRenderRepresentation previous;
            bool black_walls;

            if (!resident || !resident->occupied) continue;
            chunk = &resident->chunk;
            if (!chunk->blocks) continue;

            previous = (SdkChunkRenderRepresentation)resident->render_representation;
            if (previous > SDK_CHUNK_RENDER_REPRESENTATION_PROXY) {
                previous = SDK_CHUNK_RENDER_REPRESENTATION_FULL;
            }
            representation = choose_chunk_render_representation(resident);
            resident->render_representation = (uint8_t)representation;
            resident->render_far_mesh_kind = (uint8_t)chunk_far_mesh_kind(chunk);
            if (representation != previous) {
                g_rs.perf_representation_transitions++;
            }

            if (!chunk_representation_visible(chunk, representation)) {
                continue;
            }
            chunks_visible++;
            switch (representation) {
                case SDK_CHUNK_RENDER_REPRESENTATION_PROXY:
                    g_rs.perf_visible_proxy_chunks++;
                    continue;
                case SDK_CHUNK_RENDER_REPRESENTATION_FAR:
                    g_rs.perf_visible_far_chunks++;
                    break;
                case SDK_CHUNK_RENDER_REPRESENTATION_FULL:
                default:
                    g_rs.perf_visible_full_chunks++;
                    break;
            }

            black_walls = renderer_black_superchunk_walls_enabled() &&
                          chunk_eligible_for_black_wall_shading(chunk);
            visible_draws.push_back({ resident, representation, black_walls, false,
                                      chunk_water_sort_distance_sq(chunk) });
        }

        for (VisibleChunkDraw& draw : visible_draws) {
            SdkChunk* chunk = &draw.resident->chunk;
            bool chunk_had_renderable_mesh = false;
            bool full_water_available = chunk_has_full_water_mesh(chunk);

            draw.draw_full_water = false;

            g_rs.cmd_list->SetGraphicsRootConstantBufferView(1, renderer_lighting_cb_gpu_address(draw.black_walls));

            if (draw.representation == SDK_CHUNK_RENDER_REPRESENTATION_FAR) {
                chunk_had_renderable_mesh = draw_chunk_far_mesh(chunk, &subchunks_drawn, &total_verts);
                if (!chunk_had_renderable_mesh) {
                    g_rs.perf_missing_mesh_chunks++;
                    chunk_had_renderable_mesh = draw_chunk_full_mesh(chunk, &subchunks_drawn, &total_verts);
                    if (chunk_had_renderable_mesh || full_water_available) {
                        draw.draw_full_water = full_water_available;
                        chunk_had_renderable_mesh = true;
                    }
                }
            } else {
                chunk_had_renderable_mesh = draw_chunk_full_mesh(chunk, &subchunks_drawn, &total_verts);
                if (chunk_had_renderable_mesh || full_water_available) {
                    draw.draw_full_water = full_water_available;
                    chunk_had_renderable_mesh = true;
                } else {
                    g_rs.perf_missing_mesh_chunks++;
                    chunk_had_renderable_mesh = draw_chunk_far_mesh(chunk, &subchunks_drawn, &total_verts);
                }
            }

            if (!chunk_had_renderable_mesh) {
                g_rs.perf_missing_mesh_chunks++;
                continue;
            }

            chunks_renderable++;
            chunks_drawn++;
        }

        draw_visible_wall_proxies(&subchunks_drawn, &total_verts, renderer_black_superchunk_walls_enabled());
        if (g_rs.water_pso) {
            std::vector<const VisibleChunkDraw*> water_draws;
            g_rs.cmd_list->SetPipelineState(g_rs.water_pso.Get());
            g_rs.cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_rs.cmd_list->SetGraphicsRootConstantBufferView(1, renderer_lighting_cb_gpu_address(false));
            water_draws.reserve(visible_draws.size());
            for (const VisibleChunkDraw& draw : visible_draws) {
                if (!draw.draw_full_water) continue;
                water_draws.push_back(&draw);
            }
            std::sort(water_draws.begin(), water_draws.end(),
                      [](const VisibleChunkDraw* a, const VisibleChunkDraw* b) {
                          return a->water_sort_distance_sq > b->water_sort_distance_sq;
                      });
            for (const VisibleChunkDraw* draw : water_draws) {
                draw_chunk_full_water_mesh(&draw->resident->chunk, &subchunks_drawn, &total_verts);
            }
            g_rs.cmd_list->SetPipelineState(g_rs.pso.Get());
        }
        g_rs.cmd_list->SetGraphicsRootConstantBufferView(1, renderer_lighting_cb_gpu_address(false));
        g_rs.perf_retired_resource_backlog = g_rs.retired_resource_count;

        g_rs.perf_visible_chunks = (uint32_t)chunks_visible;
        g_rs.perf_renderable_chunks = (uint32_t)chunks_renderable;
        g_rs.perf_drawn_chunks = (uint32_t)chunks_drawn;
        g_rs.perf_drawn_subchunks = (uint32_t)subchunks_drawn;
        g_rs.perf_drawn_verts = (uint32_t)total_verts;
    }

    if (renderer_should_draw_player_character()) {
        const uint32_t local_count = g_rs.player_character_cpu_count;

        if (g_rs.player_character_vb) wait_for_gpu();
        g_rs.player_character_vb.Reset();
        g_rs.player_character_vert_count = 0;

        if (local_count > 0) {
            BlockVertex* transformed =
                (BlockVertex*)malloc((size_t)local_count * sizeof(BlockVertex));

            if (transformed) {
                size_t vb_size;
                D3D12_HEAP_PROPERTIES ehp = {};
                D3D12_RESOURCE_DESC erd = {};
                HRESULT ehr;

                for (uint32_t i = 0; i < local_count; ++i) {
                    transform_player_character_vertex(&g_rs.player_character_cpu_vertices[i],
                                                      &transformed[i]);
                }

                vb_size = (size_t)local_count * sizeof(BlockVertex);
                ehp.Type = D3D12_HEAP_TYPE_UPLOAD;
                erd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                erd.Width = vb_size;
                erd.Height = 1;
                erd.DepthOrArraySize = 1;
                erd.MipLevels = 1;
                erd.SampleDesc.Count = 1;
                erd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                ehr = g_rs.device->CreateCommittedResource(
                    &ehp, D3D12_HEAP_FLAG_NONE,
                    &erd, D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr, IID_PPV_ARGS(&g_rs.player_character_vb));
                if (SUCCEEDED(ehr)) {
                    void* mapped = nullptr;
                    D3D12_RANGE read_range = {0, 0};

                    if (SUCCEEDED(g_rs.player_character_vb->Map(0, &read_range, &mapped))) {
                        D3D12_VERTEX_BUFFER_VIEW pvbv = {};
                        memcpy(mapped, transformed, vb_size);
                        g_rs.player_character_vb->Unmap(0, nullptr);
                        g_rs.player_character_vert_count = local_count;
                        pvbv.BufferLocation = g_rs.player_character_vb->GetGPUVirtualAddress();
                        pvbv.StrideInBytes = sizeof(BlockVertex);
                        pvbv.SizeInBytes = (UINT)vb_size;
                        g_rs.cmd_list->SetGraphicsRootConstantBufferView(1, renderer_lighting_cb_gpu_address(false));
                        g_rs.cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        g_rs.cmd_list->IASetVertexBuffers(0, 1, &pvbv);
                        g_rs.cmd_list->DrawInstanced(local_count, 1, 0, 0);
                    }
                }
                free(transformed);
            }
        }
    }

    /* Draw item drop entities as small colored cubes */
    if (g_rs.entity_list) {
        if (g_rs.entity_vb) wait_for_gpu();
        g_rs.entity_vb.Reset();
        g_rs.entity_vert_count = 0;

        /* Count active entities (items + mobs) */
        int active = 0;
        for (int i = 0; i < ENTITY_MAX; i++)
            if (g_rs.entity_list->entities[i].active) active++;

        if (active > 0) {
            /* 36 verts per cube (6 faces × 2 tris × 3 verts) */
            int max_verts = active * 36 * 12;
            BlockVertex* ev = (BlockVertex*)malloc(max_verts * sizeof(BlockVertex));
            int vi = 0;

            auto emit_box = [&](float center_x, float center_y, float center_z,
                                float half_x, float half_y, float half_z,
                                float rot_cs, float rot_sn,
                                uint32_t box_col) {
                if (vi + 36 > max_verts) return;

                float box_corners[8][3];
                int box_ci = 0;
                for (int dy = -1; dy <= 1; dy += 2)
                    for (int dz = -1; dz <= 1; dz += 2)
                        for (int dx = -1; dx <= 1; dx += 2) {
                            float lx = dx * half_x;
                            float lz = dz * half_z;
                            box_corners[box_ci][0] = center_x + lx * rot_cs - lz * rot_sn;
                            box_corners[box_ci][1] = center_y + dy * half_y;
                            box_corners[box_ci][2] = center_z + lx * rot_sn + lz * rot_cs;
                            box_ci++;
                        }

                auto emit_face = [&](int a, int b, int c, int d, uint32_t face) {
                    set_untextured_vertex(&ev[vi++], box_corners[a][0], box_corners[a][1], box_corners[a][2], box_col, face);
                    set_untextured_vertex(&ev[vi++], box_corners[b][0], box_corners[b][1], box_corners[b][2], box_col, face);
                    set_untextured_vertex(&ev[vi++], box_corners[c][0], box_corners[c][1], box_corners[c][2], box_col, face);
                    set_untextured_vertex(&ev[vi++], box_corners[c][0], box_corners[c][1], box_corners[c][2], box_col, face);
                    set_untextured_vertex(&ev[vi++], box_corners[b][0], box_corners[b][1], box_corners[b][2], box_col, face);
                    set_untextured_vertex(&ev[vi++], box_corners[d][0], box_corners[d][1], box_corners[d][2], box_col, face);
                };

                emit_face(0, 1, 4, 5, 4);
                emit_face(3, 2, 7, 6, 5);
                emit_face(2, 0, 6, 4, 0);
                emit_face(1, 3, 5, 7, 1);
                emit_face(0, 2, 1, 3, 2);
                emit_face(4, 5, 6, 7, 3);
            };

            for (int i = 0; i < ENTITY_MAX && vi + 36 <= max_verts; i++) {
                const SdkEntity* e = &g_rs.entity_list->entities[i];
                if (!e->active) continue;

                float S, cx, cy, cz;
                uint32_t col;

                if (e->kind == ENTITY_ITEM) {
                    S = ITEM_SIZE;
                    float bob = sinf((float)e->age * ITEM_BOB_SPEED) * ITEM_BOB_AMP;
                    cx = e->px; cy = e->py + S + bob; cz = e->pz;
                    col = sdk_item_get_color(e->drop_item);
                    if (col == 0xFFFFFFFFu && e->drop_display_block != BLOCK_AIR) {
                        col = sdk_block_get_face_color((BlockType)e->drop_display_block, FACE_POS_Y);
                    }
                } else if (e->kind == ENTITY_MOB) {
                    /* Mob: placeholder box with per-type dimensions. */
                    S = sdk_entity_mob_width(e->mob_type);
                    cx = e->px; cy = e->py + sdk_entity_mob_height(e->mob_type) * 0.5f; cz = e->pz;
                    col = e->mob_color;
                    /* Flash red when hurt */
                    if (e->mob_hurt_timer > 0) col = 0xFF0000FF;
                } else continue;

                /* Half-height: items are cubes, mobs are tall boxes */
                float SY = (e->kind == ENTITY_MOB) ? sdk_entity_mob_height(e->mob_type) * 0.5f : S;

                /* Spin rotation around Y axis (items spin, mobs don't) */
                float cs = (e->kind == ENTITY_ITEM) ? cosf(e->spin) : 1.0f;
                float sn = (e->kind == ENTITY_ITEM) ? sinf(e->spin) : 0.0f;

                /* 8 corners of box, then rotate XZ by spin */
                float corners[8][3];
                int ci = 0;
                for (int dy = -1; dy <= 1; dy += 2)
                    for (int dz = -1; dz <= 1; dz += 2)
                        for (int dx = -1; dx <= 1; dx += 2) {
                            float lx = dx * S, lz = dz * S;
                            corners[ci][0] = cx + lx * cs - lz * sn;
                            corners[ci][1] = cy + dy * SY;
                            corners[ci][2] = cz + lx * sn + lz * cs;
                            ci++;
                        }

                /* Helper: emit face quad from 4 corner indices */
                #define EFACE(a,b,c,d,fc) do { \
                    uint32_t fc2 = (e->kind == ENTITY_ITEM) ? sdk_item_get_color(e->drop_item) : col; \
                    set_untextured_vertex(&ev[vi++], corners[a][0], corners[a][1], corners[a][2], fc2, fc); \
                    set_untextured_vertex(&ev[vi++], corners[b][0], corners[b][1], corners[b][2], fc2, fc); \
                    set_untextured_vertex(&ev[vi++], corners[c][0], corners[c][1], corners[c][2], fc2, fc); \
                    set_untextured_vertex(&ev[vi++], corners[c][0], corners[c][1], corners[c][2], fc2, fc); \
                    set_untextured_vertex(&ev[vi++], corners[b][0], corners[b][1], corners[b][2], fc2, fc); \
                    set_untextured_vertex(&ev[vi++], corners[d][0], corners[d][1], corners[d][2], fc2, fc); \
                } while(0)
                /* Corners: 0(-1,-1,-1) 1(1,-1,-1) 2(-1,-1,1) 3(1,-1,1) 4(-1,1,-1) 5(1,1,-1) 6(-1,1,1) 7(1,1,1) */
                EFACE(0,1,4,5, 4); /* -Z face */
                EFACE(3,2,7,6, 5); /* +Z face */
                EFACE(2,0,6,4, 0); /* -X face */
                EFACE(1,3,5,7, 1); /* +X face */
                EFACE(0,2,1,3, 2); /* -Y face */
                EFACE(4,5,6,7, 3); /* +Y face */
                #undef EFACE

                if (e->kind == ENTITY_MOB) {
                    float dir_cs = 1.0f;
                    float dir_sn = 0.0f;
                    uint32_t accent = (e->mob_hurt_timer > 0) ? 0xFF0000FFu : e->mob_color_secondary;
                    if (fabsf(e->mob_dir_x) + fabsf(e->mob_dir_z) > 0.001f) {
                        float ang = atan2f(e->mob_dir_x, e->mob_dir_z);
                        dir_cs = cosf(ang);
                        dir_sn = sinf(ang);
                    }

                    switch (e->mob_type) {
                        case MOB_COMMONER:
                        case MOB_BUILDER:
                        case MOB_BLACKSMITH:
                        case MOB_MINER:
                        case MOB_FOREMAN:
                        case MOB_SOLDIER:
                        case MOB_GENERAL:
                            emit_box(e->px, e->py + 0.40f, e->pz, 0.07f, 0.35f, 0.07f, dir_cs, dir_sn, col);
                            emit_box(e->px, e->py + 1.55f, e->pz, 0.16f, 0.16f, 0.16f, dir_cs, dir_sn, accent);
                            emit_box(e->px - 0.20f, e->py + 1.00f, e->pz, 0.05f, 0.28f, 0.05f, dir_cs, dir_sn, col);
                            emit_box(e->px + 0.20f, e->py + 1.00f, e->pz, 0.05f, 0.28f, 0.05f, dir_cs, dir_sn, col);
                            if (e->mob_type == MOB_COMMONER) {
                                emit_box(e->px, e->py + 1.68f, e->pz, 0.16f, 0.05f, 0.16f, dir_cs, dir_sn, accent);
                            } else if (e->mob_type == MOB_BUILDER) {
                                emit_box(e->px, e->py + 1.76f, e->pz, 0.20f, 0.05f, 0.18f, dir_cs, dir_sn, accent);
                            } else if (e->mob_type == MOB_BLACKSMITH) {
                                emit_box(e->px, e->py + 0.84f, e->pz + 0.10f, 0.18f, 0.25f, 0.02f, dir_cs, dir_sn, accent);
                            } else if (e->mob_type == MOB_MINER) {
                                emit_box(e->px, e->py + 1.72f, e->pz, 0.18f, 0.05f, 0.18f, dir_cs, dir_sn, accent);
                                emit_box(e->px, e->py + 1.58f, e->pz + 0.16f, 0.04f, 0.04f, 0.02f, dir_cs, dir_sn, 0xFFB0F0FFu);
                            } else if (e->mob_type == MOB_FOREMAN) {
                                emit_box(e->px, e->py + 1.72f, e->pz, 0.18f, 0.05f, 0.18f, dir_cs, dir_sn, accent);
                                emit_box(e->px, e->py + 1.08f, e->pz - 0.14f, 0.18f, 0.18f, 0.03f, dir_cs, dir_sn, accent);
                            } else if (e->mob_type == MOB_SOLDIER) {
                                emit_box(e->px, e->py + 1.72f, e->pz, 0.18f, 0.07f, 0.17f, dir_cs, dir_sn, accent);
                                emit_box(e->px, e->py + 1.02f, e->pz - 0.12f, 0.17f, 0.22f, 0.03f, dir_cs, dir_sn, accent);
                            } else if (e->mob_type == MOB_GENERAL) {
                                emit_box(e->px, e->py + 1.74f, e->pz, 0.18f, 0.05f, 0.18f, dir_cs, dir_sn, accent);
                                emit_box(e->px - 0.18f, e->py + 1.10f, e->pz, 0.03f, 0.22f, 0.18f, dir_cs, dir_sn, accent);
                                emit_box(e->px + 0.18f, e->py + 1.10f, e->pz, 0.03f, 0.22f, 0.18f, dir_cs, dir_sn, accent);
                            }
                            break;
                        case MOB_BOAR:
                            emit_box(e->px, e->py + 0.58f, e->pz + 0.25f, 0.14f, 0.14f, 0.14f, dir_cs, dir_sn, accent);
                            emit_box(e->px - 0.14f, e->py + 0.18f, e->pz - 0.16f, 0.04f, 0.18f, 0.04f, dir_cs, dir_sn, col);
                            emit_box(e->px + 0.14f, e->py + 0.18f, e->pz - 0.16f, 0.04f, 0.18f, 0.04f, dir_cs, dir_sn, col);
                            emit_box(e->px - 0.14f, e->py + 0.18f, e->pz + 0.16f, 0.04f, 0.18f, 0.04f, dir_cs, dir_sn, col);
                            emit_box(e->px + 0.14f, e->py + 0.18f, e->pz + 0.16f, 0.04f, 0.18f, 0.04f, dir_cs, dir_sn, col);
                            break;
                        case MOB_DEER:
                            emit_box(e->px, e->py + 0.92f, e->pz + 0.30f, 0.11f, 0.26f, 0.10f, dir_cs, dir_sn, accent);
                            emit_box(e->px - 0.12f, e->py + 0.26f, e->pz - 0.18f, 0.04f, 0.28f, 0.04f, dir_cs, dir_sn, col);
                            emit_box(e->px + 0.12f, e->py + 0.26f, e->pz - 0.18f, 0.04f, 0.28f, 0.04f, dir_cs, dir_sn, col);
                            emit_box(e->px - 0.12f, e->py + 0.26f, e->pz + 0.18f, 0.04f, 0.28f, 0.04f, dir_cs, dir_sn, col);
                            emit_box(e->px + 0.12f, e->py + 0.26f, e->pz + 0.18f, 0.04f, 0.28f, 0.04f, dir_cs, dir_sn, col);
                            break;
                        case MOB_CAR:
                            emit_box(e->px, e->py + 0.82f, e->pz + 0.04f, 0.30f, 0.16f, 0.34f, dir_cs, dir_sn, accent);
                            emit_box(e->px - 0.34f, e->py + 0.18f, e->pz - 0.48f, 0.10f, 0.10f, 0.16f, dir_cs, dir_sn, 0xFF202020u);
                            emit_box(e->px + 0.34f, e->py + 0.18f, e->pz - 0.48f, 0.10f, 0.10f, 0.16f, dir_cs, dir_sn, 0xFF202020u);
                            emit_box(e->px - 0.34f, e->py + 0.18f, e->pz + 0.48f, 0.10f, 0.10f, 0.16f, dir_cs, dir_sn, 0xFF202020u);
                            emit_box(e->px + 0.34f, e->py + 0.18f, e->pz + 0.48f, 0.10f, 0.10f, 0.16f, dir_cs, dir_sn, 0xFF202020u);
                            break;
                        case MOB_MOTORBIKE:
                            emit_box(e->px, e->py + 0.52f, e->pz, 0.05f, 0.23f, 0.44f, dir_cs, dir_sn, col);
                            emit_box(e->px, e->py + 0.82f, e->pz - 0.05f, 0.12f, 0.05f, 0.12f, dir_cs, dir_sn, accent);
                            emit_box(e->px, e->py + 0.92f, e->pz + 0.34f, 0.18f, 0.03f, 0.03f, dir_cs, dir_sn, accent);
                            emit_box(e->px, e->py + 0.48f, e->pz + 0.40f, 0.11f, 0.11f, 0.05f, dir_cs, dir_sn, 0xFF202020u);
                            emit_box(e->px, e->py + 0.48f, e->pz - 0.40f, 0.11f, 0.11f, 0.05f, dir_cs, dir_sn, 0xFF202020u);
                            break;
                        case MOB_TANK:
                            emit_box(e->px, e->py + 0.86f, e->pz, 0.32f, 0.15f, 0.30f, dir_cs, dir_sn, accent);
                            emit_box(e->px, e->py + 0.86f, e->pz + 0.62f, 0.08f, 0.07f, 0.44f, dir_cs, dir_sn, accent);
                            emit_box(e->px - 0.46f, e->py + 0.26f, e->pz, 0.08f, 0.12f, 0.80f, dir_cs, dir_sn, 0xFF202020u);
                            emit_box(e->px + 0.46f, e->py + 0.26f, e->pz, 0.08f, 0.12f, 0.80f, dir_cs, dir_sn, 0xFF202020u);
                            break;
                        default:
                            break;
                    }
                }
            }

            if (vi > 0) {
                size_t vb_size = vi * sizeof(BlockVertex);
                D3D12_HEAP_PROPERTIES ehp = {};
                ehp.Type = D3D12_HEAP_TYPE_UPLOAD;
                D3D12_RESOURCE_DESC erd = {};
                erd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                erd.Width = vb_size;
                erd.Height = 1; erd.DepthOrArraySize = 1; erd.MipLevels = 1;
                erd.SampleDesc.Count = 1;
                erd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                HRESULT ehr = g_rs.device->CreateCommittedResource(
                    &ehp, D3D12_HEAP_FLAG_NONE,
                    &erd, D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr, IID_PPV_ARGS(&g_rs.entity_vb));
                if (SUCCEEDED(ehr)) {
                    void* em = nullptr;
                    D3D12_RANGE rr = {0, 0};
                    g_rs.entity_vb->Map(0, &rr, &em);
                    memcpy(em, ev, vb_size);
                    g_rs.entity_vb->Unmap(0, nullptr);
                    g_rs.entity_vert_count = (uint32_t)vi;

                    D3D12_VERTEX_BUFFER_VIEW evbv = {};
                    evbv.BufferLocation = g_rs.entity_vb->GetGPUVirtualAddress();
                    evbv.StrideInBytes = sizeof(BlockVertex);
                    evbv.SizeInBytes = (UINT)vb_size;
                    g_rs.cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    g_rs.cmd_list->IASetVertexBuffers(0, 1, &evbv);
                    g_rs.cmd_list->DrawInstanced(g_rs.entity_vert_count, 1, 0, 0);
                }
            }
            free(ev);
        }
    }

    if (g_rs.placement_preview_visible) {
        if (g_rs.placement_preview_dirty || !g_rs.placement_preview_vb) {
            std::vector<BlockVertex> preview_verts;

            if (g_rs.placement_preview_vb) {
                ID3D12Resource* retired = g_rs.placement_preview_vb.Detach();
                retire_resource_later(retired);
            }
            g_rs.placement_preview_vert_count = 0u;

            build_placement_preview_vertices(preview_verts);
            if (!preview_verts.empty()) {
                size_t vb_size = preview_verts.size() * sizeof(BlockVertex);
                D3D12_HEAP_PROPERTIES hp = {};
                D3D12_RESOURCE_DESC rd = {};

                hp.Type = D3D12_HEAP_TYPE_UPLOAD;
                rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                rd.Width = vb_size;
                rd.Height = 1;
                rd.DepthOrArraySize = 1;
                rd.MipLevels = 1;
                rd.SampleDesc.Count = 1;
                rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                HRESULT hr = g_rs.device->CreateCommittedResource(
                    &hp, D3D12_HEAP_FLAG_NONE,
                    &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr, IID_PPV_ARGS(&g_rs.placement_preview_vb));
                if (SUCCEEDED(hr)) {
                    void* mapped = nullptr;
                    D3D12_RANGE range = { 0, 0 };
                    g_rs.placement_preview_vb->Map(0, &range, &mapped);
                    memcpy(mapped, preview_verts.data(), vb_size);
                    g_rs.placement_preview_vb->Unmap(0, nullptr);
                    g_rs.placement_preview_vert_count = (uint32_t)preview_verts.size();
                }
            }

            g_rs.placement_preview_dirty = false;
        }

        if (g_rs.placement_preview_vb && g_rs.placement_preview_vert_count > 0u && g_rs.water_pso) {
            D3D12_VERTEX_BUFFER_VIEW pvbv = {};
            g_rs.cmd_list->SetPipelineState(g_rs.water_pso.Get());
            pvbv.BufferLocation = g_rs.placement_preview_vb->GetGPUVirtualAddress();
            pvbv.StrideInBytes = sizeof(BlockVertex);
            pvbv.SizeInBytes = g_rs.placement_preview_vert_count * sizeof(BlockVertex);
            g_rs.cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_rs.cmd_list->IASetVertexBuffers(0, 1, &pvbv);
            g_rs.cmd_list->DrawInstanced(g_rs.placement_preview_vert_count, 1, 0, 0);
            g_rs.cmd_list->SetPipelineState(g_rs.pso.Get());
        }
    }

    /* Draw block outline highlight */
    if (g_rs.outline_visible) {
        /* Rebuild outline VB if position changed */
        if (g_rs.outline_dirty || !g_rs.outline_vb) {
            if (g_rs.outline_vb) {
                ID3D12Resource* retired = g_rs.outline_vb.Detach();
                retire_resource_later(retired);
            }

            float ox = (float)g_rs.outline_bx;
            float oy = (float)g_rs.outline_by;
            float oz = (float)g_rs.outline_bz;
            float E  = 0.005f;   /* Expand slightly to avoid z-fight */
            float T  = 0.02f;    /* Edge beam thickness */
            uint32_t col = 0x40FFFFFF; /* Faint white, semi-transparent */

            /* 12 edges of a cube, each edge = 2 tris = 6 verts */
            BlockVertex verts[72];
            int vi = 0;

            /* Helper macro: emit a thin quad (2 tris) given 4 corners */
            #define OUTLINE_QUAD(ax,ay,az, bx2,by2,bz2, cx2,cy2,cz2, dx2,dy2,dz2) do { \
                set_untextured_vertex(&verts[vi++], ax, ay, az, col, 3); \
                set_untextured_vertex(&verts[vi++], bx2, by2, bz2, col, 3); \
                set_untextured_vertex(&verts[vi++], cx2, cy2, cz2, col, 3); \
                set_untextured_vertex(&verts[vi++], cx2, cy2, cz2, col, 3); \
                set_untextured_vertex(&verts[vi++], bx2, by2, bz2, col, 3); \
                set_untextured_vertex(&verts[vi++], dx2, dy2, dz2, col, 3); \
            } while(0)

            float x0 = ox - E, x1 = ox + 1.0f + E;
            float y0 = oy - E, y1 = oy + 1.0f + E;
            float z0 = oz - E, z1 = oz + 1.0f + E;

            /* 4 edges along X axis */
            OUTLINE_QUAD(x0,y0-T,z0-T, x1,y0-T,z0-T, x0,y0+T,z0+T, x1,y0+T,z0+T);
            OUTLINE_QUAD(x0,y1-T,z0-T, x1,y1-T,z0-T, x0,y1+T,z0+T, x1,y1+T,z0+T);
            OUTLINE_QUAD(x0,y0-T,z1-T, x1,y0-T,z1-T, x0,y0+T,z1+T, x1,y0+T,z1+T);
            OUTLINE_QUAD(x0,y1-T,z1-T, x1,y1-T,z1-T, x0,y1+T,z1+T, x1,y1+T,z1+T);
            /* 4 edges along Y axis */
            OUTLINE_QUAD(x0-T,y0,z0-T, x0-T,y1,z0-T, x0+T,y0,z0+T, x0+T,y1,z0+T);
            OUTLINE_QUAD(x1-T,y0,z0-T, x1-T,y1,z0-T, x1+T,y0,z0+T, x1+T,y1,z0+T);
            OUTLINE_QUAD(x0-T,y0,z1-T, x0-T,y1,z1-T, x0+T,y0,z1+T, x0+T,y1,z1+T);
            OUTLINE_QUAD(x1-T,y0,z1-T, x1-T,y1,z1-T, x1+T,y0,z1+T, x1+T,y1,z1+T);
            /* 4 edges along Z axis */
            OUTLINE_QUAD(x0-T,y0-T,z0, x0-T,y0-T,z1, x0+T,y0+T,z0, x0+T,y0+T,z1);
            OUTLINE_QUAD(x1-T,y0-T,z0, x1-T,y0-T,z1, x1+T,y0+T,z0, x1+T,y0+T,z1);
            OUTLINE_QUAD(x0-T,y1-T,z0, x0-T,y1-T,z1, x0+T,y1+T,z0, x0+T,y1+T,z1);
            OUTLINE_QUAD(x1-T,y1-T,z0, x1-T,y1-T,z1, x1+T,y1+T,z0, x1+T,y1+T,z1);
            #undef OUTLINE_QUAD

            /* Upload to GPU */
            size_t vb_size = sizeof(verts);
            D3D12_HEAP_PROPERTIES hp2 = {};
            hp2.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC rd2 = {};
            rd2.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd2.Width = vb_size;
            rd2.Height = 1; rd2.DepthOrArraySize = 1; rd2.MipLevels = 1;
            rd2.SampleDesc.Count = 1;
            rd2.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            HRESULT hr2 = g_rs.device->CreateCommittedResource(
                &hp2, D3D12_HEAP_FLAG_NONE,
                &rd2, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&g_rs.outline_vb));
            if (SUCCEEDED(hr2)) {
                void* mapped2 = nullptr;
                D3D12_RANGE rr = {0, 0};
                g_rs.outline_vb->Map(0, &rr, &mapped2);
                memcpy(mapped2, verts, vb_size);
                g_rs.outline_vb->Unmap(0, nullptr);
            }
            g_rs.outline_dirty = false;
        }

        if (g_rs.outline_vb) {
            D3D12_VERTEX_BUFFER_VIEW ovbv = {};
            ovbv.BufferLocation = g_rs.outline_vb->GetGPUVirtualAddress();
            ovbv.StrideInBytes  = sizeof(BlockVertex);
            ovbv.SizeInBytes    = 72 * sizeof(BlockVertex);
            g_rs.cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_rs.cmd_list->IASetVertexBuffers(0, 1, &ovbv);
            g_rs.cmd_list->DrawInstanced(72, 1, 0, 0);
        }
    }

    /* --- HUD pass (no depth, ortho projection) --- */
    if (g_rs.hud_pso) {
        if (g_rs.hotbar_dirty || g_rs.fluid_debug.open) {
            build_hud_verts();
            g_rs.hotbar_dirty = false;
        }
        if (renderer_current_hud_buffer() && g_rs.hud_vert_count > 0) {
            g_rs.cmd_list->SetPipelineState(g_rs.hud_pso.Get());
            g_rs.cmd_list->SetGraphicsRootConstantBufferView(0, renderer_current_hud_cb()->GetGPUVirtualAddress());
            D3D12_VERTEX_BUFFER_VIEW hvbv = {};
            hvbv.BufferLocation = renderer_current_hud_buffer()->GetGPUVirtualAddress();
            hvbv.StrideInBytes  = sizeof(BlockVertex);
            hvbv.SizeInBytes    = g_rs.hud_vert_count * sizeof(BlockVertex);
            g_rs.cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_rs.cmd_list->IASetVertexBuffers(0, 1, &hvbv);
            g_rs.cmd_list->DrawInstanced(g_rs.hud_vert_count, 1, 0, 0);
        }
    }

    /* Transition render target: RENDER_TARGET → PRESENT */
    if (!renderer_record_screenshot_copy(fi)) {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        g_rs.cmd_list->ResourceBarrier(1, &barrier);
    }

    HR_CHECK(g_rs.cmd_list->Close());

    /* Execute */
    ID3D12CommandList* lists[] = { g_rs.cmd_list.Get() };
    g_rs.cmd_queue->ExecuteCommandLists(1, lists);

    /* Present */
    UINT sync_interval = g_rs.vsync ? 1 : 0;
    HR_CHECK(g_rs.swap_chain->Present(sync_interval, 0));
    QueryPerformanceCounter(&render_end);

    /* Signal fence and record which value this allocator is waiting for */
    UINT64 current_fence = g_rs.fence_value++;
    g_rs.cmd_queue->Signal(g_rs.fence.Get(), current_fence);
    g_rs.fence_values[fi] = current_fence;
    renderer_finalize_screenshot(current_fence);

    /* Advance frame index */
    g_rs.frame_index = g_rs.swap_chain->GetCurrentBackBufferIndex();
    QueryPerformanceCounter(&frame_end);
    g_rs.render_ms = qpc_ms(render_start, render_end);
    if (g_rs.anim_started) {
        g_rs.frame_ms = qpc_ms(frame_start, frame_end);
    }

    return SDK_OK;
}
