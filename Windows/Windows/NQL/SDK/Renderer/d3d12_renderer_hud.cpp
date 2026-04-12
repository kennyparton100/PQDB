/**
 * d3d12_renderer_hud.cpp -- HUD generation, atmosphere state, and renderer UI setters.
 */
#include "d3d12_renderer_internal.h"
#include "../Core/Settings/sdk_settings.h"

static const uint32_t DIGIT_FONT[10] = {
    0x69996, /* 0: 0110 1001 1001 1001 0110 */
    0x26227, /* 1: 0010 0110 0010 0010 0111 */
    0x69127, /* 2: 0110 1001 0001 0010 0111 ... simplified */
    0x69169, /* 3 */
    0x99711, /* 4 */
    0xF8E1E, /* 5 */
    0x68E96, /* 6 */
    0xF1244, /* 7 */
    0x69696, /* 8 */
    0x69716, /* 9 */
};

static const uint32_t LETTER_FONT[26] = {
    0x69F99, /* A */
    0xE9E9E, /* B */
    0x69896, /* C */
    0xE999E, /* D */
    0xF8E8F, /* E */
    0xF8E88, /* F */
    0x698B6, /* G */
    0x99F99, /* H */
    0xF222F, /* I */
    0x11196, /* J */
    0x9ACA9, /* K */
    0x8888F, /* L */
    0x9FF99, /* M */
    0x9DB99, /* N */
    0x69996, /* O */
    0xE99E8, /* P */
    0x699B7, /* Q */
    0xE9EA9, /* R */
    0x7861E, /* S */
    0xF2222, /* T */
    0x99996, /* U */
    0x99966, /* V */
    0x99FF9, /* W */
    0x99699, /* X */
    0x99622, /* Y */
    0xF124F, /* Z */
};

static const uint32_t GLYPH_DASH  = 0x000F0u;
static const uint32_t GLYPH_DOT   = 0x00006u;
static const uint32_t GLYPH_SLASH = 0x12480u;

static uint32_t hud_glyph_bits(char ch)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= '0' && ch <= '9') return DIGIT_FONT[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return LETTER_FONT[ch - 'A'];
    if (ch == '-') return GLYPH_DASH;
    if (ch == '.') return GLYPH_DOT;
    if (ch == '/') return GLYPH_SLASH;
    return 0u;
}

static inline void set_textured_vertex(
    BlockVertex* v,
    float x, float y, float z,
    uint32_t col,
    uint32_t normal,
    float u, float vcoord,
    uint32_t tex_index)
{
    v->position[0] = x;
    v->position[1] = y;
    v->position[2] = z;
    v->color = col;
    v->normal = normal;
    v->uv[0] = u;
    v->uv[1] = vcoord;
    v->tex_index = tex_index;
}

/* Helper: emit a colored quad (2 tris) into a BlockVertex array. Returns verts added (6). */
static int hud_quad(BlockVertex* v, float x0, float y0, float x1, float y1, uint32_t col)
{
    float z = 0.0f;
    /* Tri 0: TL, TR, BL */
    set_untextured_vertex(&v[0], x0, y0, z, col, HUD_TEXT_NORMAL_SENTINEL);
    set_untextured_vertex(&v[1], x1, y0, z, col, HUD_TEXT_NORMAL_SENTINEL);
    set_untextured_vertex(&v[2], x0, y1, z, col, HUD_TEXT_NORMAL_SENTINEL);
    /* Tri 1: BL, TR, BR */
    set_untextured_vertex(&v[3], x0, y1, z, col, HUD_TEXT_NORMAL_SENTINEL);
    set_untextured_vertex(&v[4], x1, y0, z, col, HUD_TEXT_NORMAL_SENTINEL);
    set_untextured_vertex(&v[5], x1, y1, z, col, HUD_TEXT_NORMAL_SENTINEL);
    return 6;
}

static int hud_textured_quad(BlockVertex* v,
                             float x0, float y0, float x1, float y1,
                             float u0, float v0, float u1, float v1,
                             uint32_t col, uint32_t tex_index)
{
    float z = 0.0f;
    set_textured_vertex(&v[0], x0, y0, z, col, HUD_TEXT_NORMAL_SENTINEL, u0, v0, tex_index);
    set_textured_vertex(&v[1], x1, y0, z, col, HUD_TEXT_NORMAL_SENTINEL, u1, v0, tex_index);
    set_textured_vertex(&v[2], x0, y1, z, col, HUD_TEXT_NORMAL_SENTINEL, u0, v1, tex_index);
    set_textured_vertex(&v[3], x0, y1, z, col, HUD_TEXT_NORMAL_SENTINEL, u0, v1, tex_index);
    set_textured_vertex(&v[4], x1, y0, z, col, HUD_TEXT_NORMAL_SENTINEL, u1, v0, tex_index);
    set_textured_vertex(&v[5], x1, y1, z, col, HUD_TEXT_NORMAL_SENTINEL, u1, v1, tex_index);
    return 6;
}

static int hud_text(BlockVertex* v, const char* text, float px, float py, float scale, uint32_t col);

/* Render a 1-3 digit number at pixel position. Returns verts used. */
static int hud_number(BlockVertex* v, int number, float px, float py, float scale, uint32_t col)
{
    if (number < 0) return 0;
    char buf[8];
    sprintf(buf, "%d", number);
    return hud_text(v, buf, px, py, scale, col);
}

static int hud_text(BlockVertex* v, const char* text, float px, float py, float scale, uint32_t col)
{
    int vi = 0;
    float cursor_x = px;
    /* Reasonable glyph sizes for readable text */
    float glyph_w = 14.0f * scale;   /* 14px base width */
    float glyph_h = 16.0f * scale;   /* 16px base height */
    float glyph_advance = 15.0f * scale;  /* Slightly wider for spacing */

    if (!text) return 0;

    for (int i = 0; text[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)text[i];

        if (ch == ' ') {
            cursor_x += glyph_advance * 1.5f;
            continue;
        }

        if (ch < HUD_FONT_FIRST_CHAR || ch > HUD_FONT_LAST_CHAR) {
            cursor_x += glyph_advance;
            continue;
        }

        vi += hud_textured_quad(&v[vi],
                                cursor_x, py,
                                cursor_x + glyph_w, py + glyph_h,
                                0.0f, 0.0f, 1.0f, 1.0f,
                                col,
                                g_rs.font_texture_layer_base + (uint32_t)(ch - HUD_FONT_FIRST_CHAR));
        cursor_x += glyph_advance;
    }
    return vi;
}

struct AtmosphereConstantBuffer {
    float lighting0[4];   /* ambient, fog_density, water_alpha, anisotropy_level */
    float sky_rgba[4];
    float sun_dir[4];
    float fog_rgba[4];
    float water_rgba[4];
    float camera_pos[4];
    float material_ids[4]; /* water block index, lava block index, black-wall-pass flag, reserved */
    float water_params[4]; /* camera_submerged, waterline_y, water_view_depth, reserved */
};

static inline float clamp01(float x)
{
    return (x < 0.0f) ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static inline float lerp_float(float a, float b, float t)
{
    return a + (b - a) * t;
}

float qpc_ms(const LARGE_INTEGER& start, const LARGE_INTEGER& end)
{
    return (float)((double)(end.QuadPart - start.QuadPart) * 1000.0 / (double)g_rs.perf_freq.QuadPart);
}

void reset_perf_stats(void)
{
    g_rs.frame_ms = 0.0f;
    g_rs.render_ms = 0.0f;
    g_rs.perf_visible_chunks = 0;
    g_rs.perf_renderable_chunks = 0;
    g_rs.perf_drawn_chunks = 0;
    g_rs.perf_drawn_subchunks = 0;
    g_rs.perf_drawn_verts = 0;
    g_rs.perf_visible_full_chunks = 0;
    g_rs.perf_visible_far_chunks = 0;
    g_rs.perf_visible_proxy_chunks = 0;
    g_rs.perf_representation_transitions = 0;
    g_rs.perf_missing_mesh_chunks = 0;
    g_rs.perf_retired_resource_backlog = 0;
    g_rs.perf_upload_bytes = 0u;
}

static int hud_clampi(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int hud_clamp_render_distance_chunks(int value)
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

static int hud_clamp_far_mesh_distance_chunks(int value)
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

static const char* graphics_preset_name(int preset_index)
{
    static const char* names[3] = { "PERFORMANCE", "BALANCED", "HIGH" };
    return names[hud_clampi(preset_index, 0, 2)];
}

static const char* graphics_display_mode_name(int display_mode)
{
    static const char* names[3] = { "WINDOWED", "BORDERLESS", "FULLSCREEN" };
    return names[hud_clampi(display_mode, 0, 2)];
}

static const char* graphics_anti_aliasing_name(int aa_mode)
{
    return (aa_mode == 1) ? "FXAA" : "OFF";
}

static const char* graphics_shadow_quality_name(int quality)
{
    static const char* names[4] = { "OFF", "LOW", "MEDIUM", "HIGH" };
    return names[hud_clampi(quality, 0, 3)];
}

static const char* graphics_water_quality_name(int quality)
{
    return (quality == 0) ? "LOW" : "HIGH";
}

static const char* graphics_resolution_preset_name(int preset_index)
{
    static const char* names[SDK_RESOLUTION_PRESET_COUNT] = {
        "720P", "1080P", "1440P", "3840X2400"
    };
    return names[hud_clampi(preset_index, 0, SDK_RESOLUTION_PRESET_COUNT - 1)];
}

static void build_graphics_resolution_text(char* out_text, size_t out_text_size)
{
    int preset_index = hud_clampi(g_rs.pause_menu.graphics_resolution_preset, 0, SDK_RESOLUTION_PRESET_COUNT - 1);
    int actual_width = g_rs.pause_menu.resolution_width;
    int actual_height = g_rs.pause_menu.resolution_height;

    if (!out_text || out_text_size == 0) return;

    if (g_rs.pause_menu.graphics_display_mode == 1) {
        sprintf_s(out_text, out_text_size, "DESKTOP %dX%d", actual_width, actual_height);
    } else if (actual_width > 0 && actual_height > 0) {
        sprintf_s(out_text, out_text_size, "%dX%d", actual_width, actual_height);
    } else {
        sprintf_s(out_text, out_text_size, "%s", graphics_resolution_preset_name(preset_index));
    }
}

static void build_render_distance_text(int render_distance_chunks, char* out_text, size_t out_text_size)
{
    int clamped = hud_clamp_render_distance_chunks(render_distance_chunks);

    if (!out_text || out_text_size == 0) return;
    if (clamped == 16) {
        sprintf_s(out_text, out_text_size, "1 SUPERCHUNK");
    } else {
        sprintf_s(out_text, out_text_size, "%d CHUNKS", clamped);
    }
}

static void build_far_mesh_distance_text(int distance_chunks, char* out_text, size_t out_text_size)
{
    int clamped = hud_clamp_far_mesh_distance_chunks(distance_chunks);

    if (!out_text || out_text_size == 0) return;
    if (clamped <= 0) {
        sprintf_s(out_text, out_text_size, "OFF");
    } else if (clamped == 16) {
        sprintf_s(out_text, out_text_size, "1 SUPERCHUNK");
    } else {
        sprintf_s(out_text, out_text_size, "%d CHUNKS", clamped);
    }
}

static void build_anisotropy_text(int anisotropy_level, char* out_text, size_t out_text_size)
{
    int clamped = anisotropy_level;

    if (!out_text || out_text_size == 0) return;
    if (clamped < 0) clamped = 0;
    if (clamped <= 0) {
        sprintf_s(out_text, out_text_size, "OFF");
    } else {
        sprintf_s(out_text, out_text_size, "%dX", clamped);
    }
}

static const char* on_off_text(bool enabled)
{
    return enabled ? "ON" : "OFF";
}

static const char* creative_filter_name(int filter_value)
{
    switch (filter_value) {
        case SDK_CREATIVE_FILTER_BUILDING_BLOCKS: return "BUILD";
        case SDK_CREATIVE_FILTER_COLORS: return "COLORS";
        case SDK_CREATIVE_FILTER_ITEMS:  return "ITEMS";
        case SDK_CREATIVE_FILTER_ALL:
        default:                        return "ALL";
    }
}

static float hud_text_width_estimate(const char* text, float scale)
{
    float glyph_advance = 15.0f * scale;
    float width = 0.0f;

    if (!text) return 0.0f;

    for (int i = 0; text[i] != '\0'; ++i) {
        width += (text[i] == ' ') ? (glyph_advance * 1.5f) : glyph_advance;
    }

    return width;
}

static int hud_panel_frame(BlockVertex* v,
                           float x0, float y0, float x1, float y1,
                           uint32_t fill, uint32_t accent, uint32_t border)
{
    int vi = 0;

    vi += hud_quad(&v[vi], x0, y0, x1, y1, fill);
    vi += hud_quad(&v[vi], x0, y0, x1, y0 + 3.0f, accent);
    vi += hud_quad(&v[vi], x0, y0, x0 + 3.0f, y1, accent);
    vi += hud_quad(&v[vi], x1 - 3.0f, y0, x1, y1, border);
    vi += hud_quad(&v[vi], x0, y1 - 3.0f, x1, y1, border);
    return vi;
}

static int hud_stat_row(BlockVertex* v,
                        float x0, float y0, float x1, float y1,
                        const char* label, const char* value,
                        uint32_t fill, uint32_t label_col, uint32_t value_col,
                        float scale)
{
    int vi = 0;
    float pad_x = 12.0f;
    float text_y = y0 + ((y1 - y0) - (16.0f * scale)) * 0.5f;

    vi += hud_quad(&v[vi], x0, y0, x1, y1, fill);
    if (label && label[0]) {
        vi += hud_text(&v[vi], label, x0 + pad_x, text_y, scale, label_col);
    }
    if (value && value[0]) {
        float value_x = x1 - pad_x - hud_text_width_estimate(value, scale);
        vi += hud_text(&v[vi], value, value_x, text_y, scale, value_col);
    }
    return vi;
}

static int hud_detail_block(BlockVertex* v,
                            float x0, float y0, float x1, float y1,
                            const char* label, const char* value,
                            uint32_t fill, uint32_t label_col, uint32_t value_col,
                            float label_scale, float value_scale)
{
    int vi = 0;
    float pad_x = 12.0f;
    float label_y = y0 + 5.0f;
    float value_y = y0 + 22.0f;

    vi += hud_quad(&v[vi], x0, y0, x1, y1, fill);
    if (label && label[0]) {
        vi += hud_text(&v[vi], label, x0 + pad_x, label_y, label_scale, label_col);
    }
    if (value && value[0]) {
        vi += hud_text(&v[vi], value, x0 + pad_x, value_y, value_scale, value_col);
    }
    return vi;
}

void synthesize_atmosphere_from_lighting(void)
{
    float neutral_sky;
    if (g_rs.atmosphere_override) {
        return;
    }

    float day = clamp01((g_rs.ambient_level - 0.15f) / 0.85f);
    neutral_sky = (g_rs.sky_r + g_rs.sky_g + g_rs.sky_b) / 3.0f;
    g_rs.atmosphere_ambient = clamp01(g_rs.ambient_level);
    g_rs.atmosphere_sky_r = g_rs.sky_r;
    g_rs.atmosphere_sky_g = g_rs.sky_g;
    g_rs.atmosphere_sky_b = g_rs.sky_b;
    g_rs.atmosphere_sun_x = 0.34f;
    g_rs.atmosphere_sun_y = 0.78f;
    g_rs.atmosphere_sun_z = 0.52f;
    g_rs.atmosphere_fog_density = lerp_float(0.0058f, 0.0015f, day);
    g_rs.atmosphere_fog_r = lerp_float(0.38f, neutral_sky * 0.58f + g_rs.sky_r * 0.28f + 0.10f, day);
    g_rs.atmosphere_fog_g = lerp_float(0.42f, neutral_sky * 0.60f + g_rs.sky_g * 0.24f + 0.10f, day);
    g_rs.atmosphere_fog_b = lerp_float(0.46f, neutral_sky * 0.62f + g_rs.sky_b * 0.12f + 0.08f, day);
    g_rs.atmosphere_water_r = 0.16f;
    g_rs.atmosphere_water_g = 0.46f;
    g_rs.atmosphere_water_b = 0.66f;
    g_rs.atmosphere_water_alpha = lerp_float(0.72f, 0.88f, day);
    g_rs.atmosphere_camera_submerged = false;
    g_rs.atmosphere_waterline_y = 0.0f;
    g_rs.atmosphere_water_view_depth = 0.0f;
}

void write_atmosphere_constant_buffer(void)
{
    AtmosphereConstantBuffer cb = {};
    AtmosphereConstantBuffer cb_wall = {};
    UINT8* lighting_cb_mapped = renderer_current_lighting_cb_mapped();

    if (!lighting_cb_mapped) {
        return;
    }

    cb.lighting0[0] = g_rs.atmosphere_ambient;
    cb.lighting0[1] = g_rs.atmosphere_fog_density;
    cb.lighting0[2] = g_rs.atmosphere_water_alpha;
    cb.lighting0[3] = (float)g_rs.pause_menu.graphics_anisotropy_level;
    cb.sky_rgba[0] = g_rs.atmosphere_sky_r;
    cb.sky_rgba[1] = g_rs.atmosphere_sky_g;
    cb.sky_rgba[2] = g_rs.atmosphere_sky_b;
    cb.sky_rgba[3] = 1.0f;
    cb.sun_dir[0] = g_rs.atmosphere_sun_x;
    cb.sun_dir[1] = g_rs.atmosphere_sun_y;
    cb.sun_dir[2] = g_rs.atmosphere_sun_z;
    cb.sun_dir[3] = 0.0f;
    cb.fog_rgba[0] = g_rs.atmosphere_fog_r;
    cb.fog_rgba[1] = g_rs.atmosphere_fog_g;
    cb.fog_rgba[2] = g_rs.atmosphere_fog_b;
    cb.fog_rgba[3] = 1.0f;
    cb.water_rgba[0] = g_rs.atmosphere_water_r;
    cb.water_rgba[1] = g_rs.atmosphere_water_g;
    cb.water_rgba[2] = g_rs.atmosphere_water_b;
    cb.water_rgba[3] = 1.0f;
    cb.camera_pos[0] = g_rs.camera.position.x;
    cb.camera_pos[1] = g_rs.camera.position.y;
    cb.camera_pos[2] = g_rs.camera.position.z;
    cb.camera_pos[3] = 1.0f;
    cb.material_ids[0] = (float)BLOCK_WATER;
    cb.material_ids[1] = (float)BLOCK_LAVA;
    cb.material_ids[2] = 0.0f;
    cb.material_ids[3] = 0.0f;
    cb.water_params[0] = g_rs.atmosphere_camera_submerged ? 1.0f : 0.0f;
    cb.water_params[1] = g_rs.atmosphere_waterline_y;
    cb.water_params[2] = g_rs.atmosphere_water_view_depth;
    cb.water_params[3] = 0.0f;
    memcpy(lighting_cb_mapped, &cb, sizeof(cb));

    cb_wall = cb;
    cb_wall.material_ids[2] = 1.0f;
    memcpy(lighting_cb_mapped + 256, &cb_wall, sizeof(cb_wall));
}

/* Build HUD vertex buffer for hotbar, hearts, death screen */
#define HUD_MAX_VERTS 65536

void build_hud_verts(void)
{
    float W = (float)g_rs.width;
    float H = (float)g_rs.height;

    /* Hotbar layout */
    float slot_size   = 84.0f;
    float slot_gap    = 6.0f;
    float border      = 3.0f;
    int num_slots     = 10;
    float total_w     = num_slots * slot_size + (num_slots - 1) * slot_gap;
    float bar_x       = (W - total_w) * 0.5f;
    float bar_y       = H - slot_size - 28.0f;

    /* Large persistent scratch buffer; stack allocation overflows the default Win32 stack in Debug. */
    static BlockVertex verts[HUD_MAX_VERTS];
    int vi = 0;

    /* --- Player coordinate display (always visible) --- */
    if (vi + 200 < HUD_MAX_VERTS) {
        float cam_x, cam_y, cam_z;
        char coord_text[128];
        int cx, cz, scx, scz, local_x, local_z;
        SdkSuperchunkCell cell;
        
        sdk_renderer_get_camera_pos(&cam_x, &cam_y, &cam_z);
        cx = sdk_world_to_chunk_x((int)floorf(cam_x));
        cz = sdk_world_to_chunk_z((int)floorf(cam_z));
        sdk_superchunk_cell_from_chunk(cx, cz, &cell);
        scx = cell.scx;
        scz = cell.scz;
        sdk_superchunk_chunk_local_interior_coords(cx, cz, &local_x, &local_z);
        
        sprintf_s(coord_text, sizeof(coord_text), "CHUNK: G(%d,%d) S(%d,%d) L(%d,%d)",
                 cx, cz, scx, scz, local_x, local_z);
        
        float text_x = (W - hud_text_width_estimate(coord_text, 1.6f)) * 0.5f;
        vi += hud_text(&verts[vi], coord_text, text_x, 16.0f, 1.6f, 0xFFBEE0FF);
    }

    if (g_rs.start_menu.open && vi + 18000 < HUD_MAX_VERTS) {
        static const char* main_options[SDK_FRONTEND_MAIN_OPTION_COUNT] = {
            "CONTINUE",
            "WORLD SAVES",
            "ONLINE",
            "CREATE WORLD",
            "CHARACTERS",
            "PROPS",
            "BLOCKS",
            "ITEMS",
            "PARTICLE EFFECTS",
            "RUN BENCHMARKS",
            "EXIT GAME"
        };
        float panel_w = (g_rs.start_menu.view == SDK_START_MENU_VIEW_MAIN) ? (W * 0.42f)
                      : (g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_SELECT) ? (W * 0.72f)
                      : (g_rs.start_menu.view == SDK_START_MENU_VIEW_ONLINE) ? (W * 0.78f)
                      : (g_rs.start_menu.view == SDK_START_MENU_VIEW_ONLINE_EDIT_SERVER) ? (W * 0.60f)
                      : (g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_MAP_GENERATING ||
                         g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_GENERATING ||
                         g_rs.start_menu.view == SDK_START_MENU_VIEW_RETURNING_TO_START) ? (W * 0.56f)
                      : (W * 0.62f);
        float panel_h = (g_rs.start_menu.view == SDK_START_MENU_VIEW_MAIN) ? (H * 0.78f)
                      : (g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_SELECT) ? (H * 0.80f)
                      : (g_rs.start_menu.view == SDK_START_MENU_VIEW_ONLINE) ? (H * 0.82f)
                      : (g_rs.start_menu.view == SDK_START_MENU_VIEW_ONLINE_EDIT_SERVER) ? (H * 0.48f)
                      : (g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_MAP_GENERATING ||
                         g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_GENERATING ||
                         g_rs.start_menu.view == SDK_START_MENU_VIEW_RETURNING_TO_START) ? (H * 0.42f)
                      : (H * 0.76f);
        float px = (W - panel_w) * 0.5f;
        float py = (H - panel_h) * 0.5f;
        vi += hud_quad(&verts[vi], 0.0f, 0.0f, W, H, 0xD0080C14u);
        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + panel_h, 0xE0141A24u);
        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + 4.0f, 0xFF7EA6D8u);
        vi += hud_quad(&verts[vi], px, py, px + 4.0f, py + panel_h, 0xFF7EA6D8u);
        vi += hud_quad(&verts[vi], px + panel_w - 4.0f, py, px + panel_w, py + panel_h, 0xFF7EA6D8u);
        vi += hud_quad(&verts[vi], px, py + panel_h - 4.0f, px + panel_w, py + panel_h, 0xFF7EA6D8u);
        if (g_rs.start_menu.view == SDK_START_MENU_VIEW_MAIN) {
            float list_x = px + 28.0f;
            float list_y = py + 152.0f;
            float row_h = 62.0f;
            float list_w = panel_w - 56.0f;

            vi += hud_text(&verts[vi], "START MENU", px + 28.0f, py + 70.0f, 4.15f, 0xFFFFFFFFu);

            for (int i = 0; i < SDK_FRONTEND_MAIN_OPTION_COUNT; ++i) {
                uint32_t row_col = (i == g_rs.start_menu.main_selected) ? 0xFF80522Du : 0xD0222630u;
                if (i == 0 && g_rs.start_menu.world_total_count <= 0) {
                    row_col = 0xA0181C24u;
                }
                vi += hud_quad(&verts[vi], list_x, list_y + i * row_h,
                               list_x + list_w, list_y + i * row_h + 46.0f, row_col);
                vi += hud_text(&verts[vi], main_options[i], list_x + 18.0f, list_y + i * row_h + 11.0f, 2.72f, 0xFFFFFFFFu);
            }

            if (g_rs.start_menu.status_text[0]) {
                vi += hud_text(&verts[vi], g_rs.start_menu.status_text,
                               px + 28.0f, py + panel_h - 56.0f, 1.48f, 0xFFBEE0FFu);
            }
            vi += hud_text(&verts[vi], "UP/DOWN SELECT   ENTER CONFIRMS", px + 28.0f, py + panel_h - 32.0f, 1.58f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_SELECT) {
            float list_x = px + 24.0f;
            float list_y = py + 162.0f;
            float list_w = panel_w - 48.0f;
            float row_h = 48.0f;

            vi += hud_text(&verts[vi], "WORLD SAVES", px + 24.0f, py + 82.0f, 4.05f, 0xFFFFFFFFu);
            {
                char total_text[32];
                sprintf_s(total_text, sizeof(total_text), "%d TOTAL", g_rs.start_menu.world_total_count);
                vi += hud_text(&verts[vi], total_text,
                               px + panel_w - 24.0f - hud_text_width_estimate(total_text, 1.42f),
                               py + 92.0f, 1.42f, 0xFFB8C0D0u);
            }
            if (g_rs.start_menu.world_count <= 0) {
                vi += hud_text(&verts[vi], "NO SAVES AVAILABLE", list_x, list_y + 16.0f, 2.2f, 0xFFD0D8E0u);
                vi += hud_text(&verts[vi], "CREATE ONE FROM THE MAIN MENU", list_x, list_y + 48.0f, 1.75f, 0xFFB8C0D0u);
            } else {
                for (int i = 0; i < g_rs.start_menu.world_count; ++i) {
                    char seed_text[48];
                    int absolute_index = g_rs.start_menu.world_scroll + i;
                    uint32_t row_col = (absolute_index == g_rs.start_menu.world_selected) ? 0xFF80522Du : 0xD0222630u;
                    float row_y = list_y + i * row_h;
                    sprintf_s(seed_text, sizeof(seed_text), "SEED %u", g_rs.start_menu.world_seed[i]);
                    vi += hud_quad(&verts[vi], list_x, row_y, list_x + list_w, row_y + 38.0f, row_col);
                    vi += hud_text(&verts[vi], g_rs.start_menu.world_name[i], list_x + 14.0f, row_y + 8.0f, 2.28f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], seed_text,
                                   list_x + list_w - 14.0f - hud_text_width_estimate(seed_text, 1.44f),
                                   row_y + 10.0f, 1.44f, 0xFFCFD7E0u);
                }
            }
            if (g_rs.start_menu.status_text[0]) {
                vi += hud_text(&verts[vi], g_rs.start_menu.status_text,
                               px + 24.0f, py + panel_h - 56.0f, 1.44f, 0xFFBEE0FFu);
            }
            vi += hud_text(&verts[vi], "ENTER OPENS ACTIONS   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 32.0f, 1.58f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_ACTIONS) {
            float row_x = px + 24.0f;
            float row_y = py + 136.0f;
            float row_h = 58.0f;
            char seed_text[48];
            const char* world_action_names[5];
            bool world_action_enabled[5];

            sprintf_s(seed_text, sizeof(seed_text), "SEED %u", g_rs.start_menu.selected_world_seed);
            world_action_names[0] = "PLAY LOCAL";
            world_action_names[1] = g_rs.start_menu.world_action_hosted_selected_world
                ? "STOP HOSTING"
                : "HOST LOCALLY";
            world_action_names[2] = "GENERATE MAP";
            world_action_names[3] = "GENERATE WORLD";
            world_action_names[4] = "BACK";
            world_action_enabled[0] = g_rs.start_menu.world_action_can_play;
            world_action_enabled[1] = g_rs.start_menu.world_action_can_toggle_host;
            world_action_enabled[2] = g_rs.start_menu.world_action_can_generate_map;
            world_action_enabled[3] = g_rs.start_menu.world_action_can_generate_map;
            world_action_enabled[4] = true;

            vi += hud_text(&verts[vi], "WORLD ACTIONS", px + 24.0f, py + 56.0f, 3.1f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], g_rs.start_menu.selected_world_name, px + 24.0f, py + 94.0f, 2.2f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], seed_text, px + 24.0f, py + 126.0f, 1.62f, 0xFFB8C0D0u);

            for (int row = 0; row < 5; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.world_action_selected) ? 0xFF2C4F78u : 0xD0222630u;
                if (!world_action_enabled[row]) {
                    row_col = 0xA0181C24u;
                }
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 40.0f, row_col);
                vi += hud_text(&verts[vi], world_action_names[row], row_x + 14.0f,
                               row_y + row * row_h + 10.0f, 2.05f, 0xFFFFFFFFu);
            }
            if (g_rs.start_menu.status_text[0]) {
                vi += hud_text(&verts[vi], g_rs.start_menu.status_text,
                               px + 24.0f, py + panel_h - 54.0f, 1.42f, 0xFFBEE0FFu);
            }
            vi += hud_text(&verts[vi], "ENTER CONFIRMS   BACKSPACE RETURNS",
                           px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_ONLINE) {
            float saved_x = px + 24.0f;
            float hosted_x = px + panel_w * 0.53f;
            float section_y = py + 112.0f;
            float section_w = panel_w * 0.41f;
            float row_h = 54.0f;
            int saved_create_visible = (g_rs.start_menu.saved_server_scroll == 0) ? 1 : 0;
            int saved_visible_rows = g_rs.start_menu.saved_server_visible_count + saved_create_visible;

            vi += hud_text(&verts[vi], "ONLINE", px + 24.0f, py + 56.0f, 3.2f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], "SAVED SERVERS", saved_x, py + 92.0f, 2.1f,
                           (g_rs.start_menu.online_section_focus == 0) ? 0xFFBEE0FFu : 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], "HOSTED SERVERS", hosted_x, py + 92.0f, 2.1f,
                           (g_rs.start_menu.online_section_focus == 1) ? 0xFFBEE0FFu : 0xFFD0D8E0u);

            for (int row = 0; row < saved_visible_rows && row < SDK_START_MENU_SERVER_VISIBLE_MAX; ++row) {
                int absolute_row = g_rs.start_menu.saved_server_scroll + row;
                int server_row = row - saved_create_visible;
                uint32_t row_col = (g_rs.start_menu.online_section_focus == 0 &&
                                    absolute_row == g_rs.start_menu.saved_server_selected)
                    ? 0xFF80522Du : 0xD0222630u;
                const char* label = (absolute_row == 0)
                    ? "ADD SAVED SERVER"
                    : g_rs.start_menu.saved_server_name[server_row];

                vi += hud_quad(&verts[vi], saved_x, section_y + row * row_h,
                               saved_x + section_w, section_y + row * row_h + 42.0f, row_col);
                vi += hud_text(&verts[vi], label, saved_x + 14.0f,
                               section_y + row * row_h + 8.0f, 1.88f, 0xFFFFFFFFu);
                if (absolute_row > 0) {
                    vi += hud_text(&verts[vi], g_rs.start_menu.saved_server_address[server_row],
                                   saved_x + 14.0f, section_y + row * row_h + 24.0f,
                                   1.20f, 0xFFB8C0D0u);
                }
            }
            if (saved_visible_rows <= 0) {
                vi += hud_text(&verts[vi], "ADD SAVED SERVER", saved_x, section_y + 10.0f, 1.9f, 0xFFFFFFFFu);
            }

            if (g_rs.start_menu.hosted_server_count <= 0) {
                vi += hud_text(&verts[vi], "NO LOCAL HOSTS", hosted_x, section_y + 16.0f, 2.0f, 0xFFD0D8E0u);
                vi += hud_text(&verts[vi], "HOST A WORLD FROM WORLD SAVES", hosted_x, section_y + 48.0f, 1.45f, 0xFFB8C0D0u);
            } else {
                for (int row = 0; row < g_rs.start_menu.hosted_server_visible_count; ++row) {
                    char seed_text[48];
                    uint32_t row_col = (g_rs.start_menu.online_section_focus == 1 &&
                                        row == g_rs.start_menu.hosted_server_selected)
                        ? 0xFF2C4F78u : 0xD0222630u;
                    sprintf_s(seed_text, sizeof(seed_text), "SEED %u", g_rs.start_menu.hosted_server_seed[row]);
                    vi += hud_quad(&verts[vi], hosted_x, section_y + row * row_h,
                                   hosted_x + section_w, section_y + row * row_h + 46.0f, row_col);
                    vi += hud_text(&verts[vi], g_rs.start_menu.hosted_server_name[row], hosted_x + 14.0f,
                                   section_y + row * row_h + 8.0f, 1.88f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], g_rs.start_menu.hosted_server_address[row], hosted_x + 14.0f,
                                   section_y + row * row_h + 24.0f, 1.20f, 0xFFB8C0D0u);
                    vi += hud_text(&verts[vi], seed_text,
                                   hosted_x + section_w - 14.0f - hud_text_width_estimate(seed_text, 1.10f),
                                   section_y + row * row_h + 24.0f, 1.10f, 0xFFB8C0D0u);
                }
            }

            if (g_rs.start_menu.status_text[0]) {
                vi += hud_text(&verts[vi], g_rs.start_menu.status_text,
                               px + 24.0f, py + panel_h - 58.0f, 1.46f, 0xFFBEE0FFu);
            }
            vi += hud_text(&verts[vi], "LEFT/RIGHT SWITCH SECTION   ENTER JOIN/ADD", px + 24.0f, py + panel_h - 34.0f, 1.46f, 0xFFB8C0D0u);
            vi += hud_text(&verts[vi], "E EDIT   DEL REMOVE/STOP HOST   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 14.0f, 1.34f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_ONLINE_EDIT_SERVER) {
            static const char* row_labels[4] = {
                "DISPLAY NAME",
                "ADDRESS",
                "SAVE",
                "CANCEL"
            };
            float row_x = px + 24.0f;
            float row_y = py + 112.0f;
            float row_h = 56.0f;

            vi += hud_text(&verts[vi], g_rs.start_menu.online_edit_is_new ? "ADD SAVED SERVER" : "EDIT SAVED SERVER",
                           px + 24.0f, py + 54.0f, 2.8f, 0xFFFFFFFFu);

            for (int row = 0; row < 4; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.online_edit_selected) ? 0xFF2C4F78u : 0xD0222630u;
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 24.0f, row_y + row * row_h + 42.0f, row_col);
                vi += hud_text(&verts[vi], row_labels[row], row_x + 14.0f,
                               row_y + row * row_h + 10.0f, 1.92f, 0xFFFFFFFFu);
                if (row == 0) {
                    vi += hud_text(&verts[vi], g_rs.start_menu.online_edit_name[0] ? g_rs.start_menu.online_edit_name : "TYPE A NAME",
                                   px + panel_w - 220.0f, row_y + row * row_h + 10.0f, 1.72f, 0xFFDCE8F4u);
                } else if (row == 1) {
                    vi += hud_text(&verts[vi], g_rs.start_menu.online_edit_address[0] ? g_rs.start_menu.online_edit_address : "host or host:port",
                                   px + panel_w - 260.0f, row_y + row * row_h + 10.0f, 1.56f, 0xFFDCE8F4u);
                }
            }
            if (g_rs.start_menu.status_text[0]) {
                vi += hud_text(&verts[vi], g_rs.start_menu.status_text,
                               px + 24.0f, py + panel_h - 56.0f, 1.38f, 0xFFBEE0FFu);
            }
            vi += hud_text(&verts[vi], "TYPE INTO THE SELECTED FIELD   ENTER SAVES", px + 24.0f, py + panel_h - 32.0f, 1.36f, 0xFFB8C0D0u);
            vi += hud_text(&verts[vi], "BACKSPACE DELETES OR RETURNS", px + 24.0f, py + panel_h - 14.0f, 1.36f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_CHARACTERS) {
            float row_x = px + 24.0f;
            float row_y = py + 212.0f;
            float row_h = 56.0f;
            int create_visible = (g_rs.start_menu.character_scroll == 0) ? 1 : 0;
            int visible_rows = g_rs.start_menu.character_count + create_visible;

            vi += hud_text(&verts[vi], "CHARACTERS", px + 24.0f, py + 144.0f, 4.0f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], "CREATE NEW CHARACTER FIRST, THEN EDIT ANIMATIONS", px + 24.0f, py + 194.0f, 1.82f, 0xFFB8C0D0u);

            for (int row = 0; row < visible_rows && row < SDK_START_MENU_ASSET_VISIBLE_MAX; ++row) {
                int absolute_row = g_rs.start_menu.character_scroll + row;
                int asset_row = row - create_visible;
                uint32_t row_col = (absolute_row == g_rs.start_menu.character_selected) ? 0xFF80522Du : 0xD0222630u;
                const char* label = (absolute_row == 0) ? "CREATE NEW CHARACTER" : g_rs.start_menu.character_name[asset_row];
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 42.0f, row_col);
                vi += hud_text(&verts[vi], label, row_x + 14.0f, row_y + row * row_h + 9.0f, 2.24f, 0xFFFFFFFFu);
            }

            vi += hud_text(&verts[vi], "ENTER OPENS OR CREATES   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 32.0f, 1.56f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_CHARACTER_ACTIONS) {
            static const char* action_names[SDK_CHARACTER_ACTION_OPTION_COUNT] = {
                "EDIT", "COPY", "DELETE", "ANIMATIONS", "BACK"
            };
            float row_x = px + 24.0f;
            float row_y = py + 130.0f;
            float row_h = 58.0f;

            vi += hud_text(&verts[vi], "CHARACTER", px + 24.0f, py + 56.0f, 2.5f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], g_rs.start_menu.selected_character_name, px + 24.0f, py + 90.0f, 3.0f, 0xFFFFFFFFu);
            for (int row = 0; row < SDK_CHARACTER_ACTION_OPTION_COUNT; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.character_action_selected) ? 0xFF2C4F78u : 0xD0222630u;
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 40.0f, row_col);
                vi += hud_text(&verts[vi], action_names[row], row_x + 14.0f, row_y + row * row_h + 10.0f, 2.05f, 0xFFFFFFFFu);
            }
            vi += hud_text(&verts[vi], "ENTER CONFIRMS   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_CHARACTER_ANIMATIONS) {
            static const char* animation_hub_options[SDK_CHARACTER_ANIMATION_MENU_OPTION_COUNT] = {
                "CREATE ANIMATION", "SAVED ANIMATIONS", "BACK"
            };
            float row_x = px + 24.0f;
            float row_y = py + 136.0f;
            float row_h = 58.0f;

            vi += hud_text(&verts[vi], "ANIMATIONS", px + 24.0f, py + 56.0f, 3.1f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], g_rs.start_menu.selected_character_name, px + 24.0f, py + 96.0f, 2.0f, 0xFFBEE0FFu);
            for (int row = 0; row < SDK_CHARACTER_ANIMATION_MENU_OPTION_COUNT; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.animation_hub_selected) ? 0xFF2C4F78u : 0xD0222630u;
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 40.0f, row_col);
                vi += hud_text(&verts[vi], animation_hub_options[row], row_x + 14.0f, row_y + row * row_h + 10.0f, 2.0f, 0xFFFFFFFFu);
            }
            vi += hud_text(&verts[vi], "ANIMATIONS USE ONE CHUNK PER FRAME", px + 24.0f, py + panel_h - 52.0f, 1.45f, 0xFFB8C0D0u);
            vi += hud_text(&verts[vi], "ENTER CONFIRMS   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_ANIMATION_LIST) {
            float row_x = px + 24.0f;
            float row_y = py + 114.0f;
            float row_h = 40.0f;

            vi += hud_text(&verts[vi], "SAVED ANIMATIONS", px + 24.0f, py + 60.0f, 3.05f, 0xFFFFFFFFu);
            if (g_rs.start_menu.animation_count <= 0) {
                vi += hud_text(&verts[vi], "NO ANIMATIONS YET", row_x, row_y + 12.0f, 2.2f, 0xFFD0D8E0u);
                vi += hud_text(&verts[vi], "CREATE ONE FROM THE PREVIOUS MENU", row_x, row_y + 44.0f, 1.7f, 0xFFB8C0D0u);
            } else {
                for (int row = 0; row < g_rs.start_menu.animation_count; ++row) {
                    int absolute_index = g_rs.start_menu.animation_scroll + row;
                    uint32_t row_col = (absolute_index == g_rs.start_menu.animation_selected) ? 0xFF80522Du : 0xD0222630u;
                    vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                                   px + panel_w - 48.0f, row_y + row * row_h + 34.0f, row_col);
                    vi += hud_text(&verts[vi], g_rs.start_menu.animation_name[row],
                                   row_x + 14.0f, row_y + row * row_h + 8.0f, 1.72f, 0xFFFFFFFFu);
                }
            }
            vi += hud_text(&verts[vi], "ENTER OPENS ACTIONS   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 30.0f, 1.35f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_ANIMATION_ACTIONS) {
            static const char* animation_action_names[SDK_ANIMATION_ACTION_OPTION_COUNT] = {
                "EDIT", "COPY", "DELETE", "BACK"
            };
            float row_x = px + 24.0f;
            float row_y = py + 130.0f;
            float row_h = 58.0f;

            vi += hud_text(&verts[vi], "ANIMATION", px + 24.0f, py + 56.0f, 2.5f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], g_rs.start_menu.selected_animation_name, px + 24.0f, py + 90.0f, 3.0f, 0xFFFFFFFFu);
            for (int row = 0; row < SDK_ANIMATION_ACTION_OPTION_COUNT; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.animation_action_selected) ? 0xFF2C4F78u : 0xD0222630u;
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 40.0f, row_col);
                vi += hud_text(&verts[vi], animation_action_names[row], row_x + 14.0f, row_y + row * row_h + 10.0f, 2.05f, 0xFFFFFFFFu);
            }
            vi += hud_text(&verts[vi], "ENTER CONFIRMS   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_PROPS) {
            float row_x = px + 24.0f;
            float row_y = py + 212.0f;
            float row_h = 56.0f;
            int create_visible = (g_rs.start_menu.prop_scroll == 0) ? 1 : 0;
            int visible_rows = g_rs.start_menu.prop_count + create_visible;

            vi += hud_text(&verts[vi], "PROPS", px + 24.0f, py + 144.0f, 4.0f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], "1:1 SCALE  16X16 CHUNKS  1024X1024 STONE FLOOR", px + 24.0f, py + 194.0f, 1.82f, 0xFFB8C0D0u);

            for (int row = 0; row < visible_rows && row < SDK_START_MENU_ASSET_VISIBLE_MAX; ++row) {
                int absolute_row = g_rs.start_menu.prop_scroll + row;
                int asset_row = row - create_visible;
                uint32_t row_col = (absolute_row == g_rs.start_menu.prop_selected) ? 0xFF80522Du : 0xD0222630u;
                const char* label = (absolute_row == 0) ? "CREATE NEW PROP" : g_rs.start_menu.prop_name[asset_row];
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 42.0f, row_col);
                vi += hud_text(&verts[vi], label, row_x + 14.0f, row_y + row * row_h + 9.0f, 2.24f, 0xFFFFFFFFu);
            }

            vi += hud_text(&verts[vi], "ENTER OPENS OR CREATES   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 32.0f, 1.56f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_PROP_ACTIONS) {
            static const char* action_names[SDK_GENERIC_ASSET_ACTION_OPTION_COUNT] = {
                "EDIT", "COPY", "DELETE", "BACK"
            };
            float row_x = px + 24.0f;
            float row_y = py + 130.0f;
            float row_h = 58.0f;

            vi += hud_text(&verts[vi], "PROP", px + 24.0f, py + 56.0f, 2.5f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], g_rs.start_menu.selected_prop_name, px + 24.0f, py + 90.0f, 3.0f, 0xFFFFFFFFu);
            for (int row = 0; row < SDK_GENERIC_ASSET_ACTION_OPTION_COUNT; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.prop_action_selected) ? 0xFF2C4F78u : 0xD0222630u;
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 40.0f, row_col);
                vi += hud_text(&verts[vi], action_names[row], row_x + 14.0f, row_y + row * row_h + 10.0f, 2.05f, 0xFFFFFFFFu);
            }
            vi += hud_text(&verts[vi], "ENTER CONFIRMS   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_BLOCKS) {
            float row_x = px + 24.0f;
            float row_y = py + 212.0f;
            float row_h = 56.0f;
            int create_visible = (g_rs.start_menu.block_scroll == 0) ? 1 : 0;
            int visible_rows = g_rs.start_menu.block_count + create_visible;

            vi += hud_text(&verts[vi], "BLOCKS", px + 24.0f, py + 144.0f, 4.0f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], "LEFT CHUNK MODEL  RIGHT CHUNK 32X32 ICON", px + 24.0f, py + 194.0f, 1.82f, 0xFFB8C0D0u);

            for (int row = 0; row < visible_rows && row < SDK_START_MENU_ASSET_VISIBLE_MAX; ++row) {
                int absolute_row = g_rs.start_menu.block_scroll + row;
                int asset_row = row - create_visible;
                uint32_t row_col = (absolute_row == g_rs.start_menu.block_selected) ? 0xFF80522Du : 0xD0222630u;
                const char* label = (absolute_row == 0) ? "CREATE NEW BLOCK" : g_rs.start_menu.block_name[asset_row];
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 42.0f, row_col);
                vi += hud_text(&verts[vi], label, row_x + 14.0f, row_y + row * row_h + 9.0f, 2.24f, 0xFFFFFFFFu);
            }

            vi += hud_text(&verts[vi], "ENTER OPENS OR CREATES   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 32.0f, 1.56f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_BLOCK_ACTIONS) {
            static const char* action_names[SDK_GENERIC_ASSET_ACTION_OPTION_COUNT] = {
                "EDIT", "COPY", "DELETE", "BACK"
            };
            float row_x = px + 24.0f;
            float row_y = py + 130.0f;
            float row_h = 58.0f;

            vi += hud_text(&verts[vi], "BLOCK", px + 24.0f, py + 56.0f, 2.5f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], g_rs.start_menu.selected_block_name, px + 24.0f, py + 90.0f, 3.0f, 0xFFFFFFFFu);
            for (int row = 0; row < SDK_GENERIC_ASSET_ACTION_OPTION_COUNT; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.block_action_selected) ? 0xFF2C4F78u : 0xD0222630u;
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 40.0f, row_col);
                vi += hud_text(&verts[vi], action_names[row], row_x + 14.0f, row_y + row * row_h + 10.0f, 2.05f, 0xFFFFFFFFu);
            }
            vi += hud_text(&verts[vi], "ENTER CONFIRMS   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_ITEMS) {
            float row_x = px + 24.0f;
            float row_y = py + 212.0f;
            float row_h = 56.0f;
            int create_visible = (g_rs.start_menu.item_scroll == 0) ? 1 : 0;
            int visible_rows = g_rs.start_menu.item_count + create_visible;

            vi += hud_text(&verts[vi], "ITEMS", px + 24.0f, py + 144.0f, 4.0f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], "LEFT CHUNK ITEM MODEL  RIGHT CHUNK 32X32 ICON", px + 24.0f, py + 194.0f, 1.82f, 0xFFB8C0D0u);

            for (int row = 0; row < visible_rows && row < SDK_START_MENU_ASSET_VISIBLE_MAX; ++row) {
                int absolute_row = g_rs.start_menu.item_scroll + row;
                int asset_row = row - create_visible;
                uint32_t row_col = (absolute_row == g_rs.start_menu.item_selected) ? 0xFF80522Du : 0xD0222630u;
                const char* label = (absolute_row == 0) ? "CREATE NEW ITEM" : g_rs.start_menu.item_name[asset_row];
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 42.0f, row_col);
                vi += hud_text(&verts[vi], label, row_x + 14.0f, row_y + row * row_h + 9.0f, 2.24f, 0xFFFFFFFFu);
            }

            vi += hud_text(&verts[vi], "ENTER OPENS OR CREATES   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 32.0f, 1.56f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_ITEM_ACTIONS) {
            static const char* action_names[SDK_GENERIC_ASSET_ACTION_OPTION_COUNT] = {
                "EDIT", "COPY", "DELETE", "BACK"
            };
            float row_x = px + 24.0f;
            float row_y = py + 130.0f;
            float row_h = 58.0f;

            vi += hud_text(&verts[vi], "ITEM", px + 24.0f, py + 56.0f, 2.5f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], g_rs.start_menu.selected_item_name, px + 24.0f, py + 90.0f, 3.0f, 0xFFFFFFFFu);
            for (int row = 0; row < SDK_GENERIC_ASSET_ACTION_OPTION_COUNT; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.item_action_selected) ? 0xFF2C4F78u : 0xD0222630u;
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 40.0f, row_col);
                vi += hud_text(&verts[vi], action_names[row], row_x + 14.0f, row_y + row * row_h + 10.0f, 2.05f, 0xFFFFFFFFu);
            }
            vi += hud_text(&verts[vi], "ENTER CONFIRMS   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_PARTICLE_EFFECTS) {
            float row_x = px + 24.0f;
            float row_y = py + 212.0f;
            float row_h = 56.0f;
            int create_visible = (g_rs.start_menu.particle_effect_scroll == 0) ? 1 : 0;
            int visible_rows = g_rs.start_menu.particle_effect_count + create_visible;

            vi += hud_text(&verts[vi], "PARTICLE EFFECTS", px + 24.0f, py + 144.0f, 4.0f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], "LEFT CHUNK 8 EFFECT SLICES  RIGHT CHUNK 32X32 ICON", px + 24.0f, py + 194.0f, 1.82f, 0xFFB8C0D0u);

            for (int row = 0; row < visible_rows && row < SDK_START_MENU_ASSET_VISIBLE_MAX; ++row) {
                int absolute_row = g_rs.start_menu.particle_effect_scroll + row;
                int asset_row = row - create_visible;
                uint32_t row_col = (absolute_row == g_rs.start_menu.particle_effect_selected) ? 0xFF80522Du : 0xD0222630u;
                const char* label = (absolute_row == 0) ? "CREATE NEW PARTICLE EFFECT" : g_rs.start_menu.particle_effect_name[asset_row];
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 42.0f, row_col);
                vi += hud_text(&verts[vi], label, row_x + 14.0f, row_y + row * row_h + 9.0f, 2.24f, 0xFFFFFFFFu);
            }

            vi += hud_text(&verts[vi], "ENTER OPENS OR CREATES   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 32.0f, 1.56f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_PARTICLE_EFFECT_ACTIONS) {
            static const char* action_names[SDK_GENERIC_ASSET_ACTION_OPTION_COUNT] = {
                "EDIT", "COPY", "DELETE", "BACK"
            };
            float row_x = px + 24.0f;
            float row_y = py + 130.0f;
            float row_h = 58.0f;

            vi += hud_text(&verts[vi], "PARTICLE EFFECT", px + 24.0f, py + 56.0f, 2.5f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], g_rs.start_menu.selected_particle_effect_name,
                           px + 24.0f, py + 90.0f, 3.0f, 0xFFFFFFFFu);
            for (int row = 0; row < SDK_GENERIC_ASSET_ACTION_OPTION_COUNT; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.particle_effect_action_selected) ? 0xFF2C4F78u : 0xD0222630u;
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h,
                               px + panel_w - 48.0f, row_y + row * row_h + 40.0f, row_col);
                vi += hud_text(&verts[vi], action_names[row], row_x + 14.0f, row_y + row * row_h + 10.0f, 2.05f, 0xFFFFFFFFu);
            }
            vi += hud_text(&verts[vi], "ENTER CONFIRMS   BACKSPACE RETURNS", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_CREATE) {
            static const char* spawn_names[] = { "RANDOM", "CENTER", "SAFE SPAWN" };
            float row_x = px + 24.0f;
            float row_y = py + 118.0f;
            float row_h = 52.0f;
            char seed_text[32];
            char render_text[32];
            const char* coordinate_system_text = "Chunk System";
            char superchunk_text[32];
            char grid_size_text[32];
            char grid_offset_x_text[32];
            char grid_offset_z_text[32];
            char wall_thickness_text[32];
            const char* shared_walls_text = g_rs.start_menu.world_create_wall_rings_shared ? "SHARED" : "DOUBLE";

            sprintf_s(seed_text, sizeof(seed_text), "%u", g_rs.start_menu.world_create_seed);
            build_render_distance_text(g_rs.start_menu.world_create_render_distance,
                                       render_text, sizeof(render_text));
            switch ((SdkWorldCoordinateSystem)g_rs.start_menu.world_create_coordinate_system) {
                case SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM:
                    coordinate_system_text = "Superchunk System";
                    break;
                case SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM:
                    coordinate_system_text = "Grid & Terrain Superchunk System";
                    break;
                case SDK_WORLD_COORDSYS_CHUNK_SYSTEM:
                default:
                    coordinate_system_text = "Chunk System";
                    break;
            }
            sprintf_s(superchunk_text, sizeof(superchunk_text), "%dx%d", 
                     g_rs.start_menu.world_create_superchunk_chunk_span,
                     g_rs.start_menu.world_create_superchunk_chunk_span);
            sprintf_s(grid_size_text, sizeof(grid_size_text), "%d", g_rs.start_menu.world_create_wall_grid_size);
            sprintf_s(grid_offset_x_text, sizeof(grid_offset_x_text), "%d", g_rs.start_menu.world_create_wall_grid_offset_x);
            sprintf_s(grid_offset_z_text, sizeof(grid_offset_z_text), "%d", g_rs.start_menu.world_create_wall_grid_offset_z);
            sprintf_s(wall_thickness_text, sizeof(wall_thickness_text), "%d blocks",
                      g_rs.start_menu.world_create_wall_thickness_blocks);

            vi += hud_text(&verts[vi], "CREATE WORLD", px + 24.0f, py + 60.0f, 3.15f, 0xFFFFFFFFu);

            for (int row = 0; row < 13; ++row) {
                uint32_t row_col = (row == g_rs.start_menu.world_create_selected) ? 0xFF2C4F78u : 0xD0222630u;
                vi += hud_quad(&verts[vi], row_x, row_y + row * row_h, px + panel_w - 48.0f, row_y + row * row_h + 44.0f, row_col);

                if (row == 0) {
                    vi += hud_text(&verts[vi], "SEED", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], seed_text, px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFDCE8F4u);
                } else if (row == 1) {
                    vi += hud_text(&verts[vi], "SPAWN LOCATION", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], spawn_names[g_rs.start_menu.world_create_spawn_type], px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFDCE8F4u);
                } else if (row == 2) {
                    vi += hud_text(&verts[vi], "RENDER DISTANCE", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], render_text, px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFDCE8F4u);
                } else if (row == 3) {
                    vi += hud_text(&verts[vi], "SETTLEMENTS", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], g_rs.start_menu.world_create_settlements_enabled ? "ENABLED" : "DISABLED", px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, g_rs.start_menu.world_create_settlements_enabled ? 0xFF7CD992u : 0xFFE08C8Cu);
                } else if (row == 4) {
                    vi += hud_text(&verts[vi], "CONSTRUCTION CELLS", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], g_rs.start_menu.world_create_construction_cells_enabled ? "ENABLED" : "DISABLED", px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, g_rs.start_menu.world_create_construction_cells_enabled ? 0xFF7CD992u : 0xFFE08C8Cu);
                } else if (row == 5) {
                    vi += hud_text(&verts[vi], "COORDINATE SYSTEM", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], coordinate_system_text, px + panel_w - 420.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFDCE8F4u);
                } else if (row == 6) {
                    vi += hud_text(&verts[vi], "SUPERCHUNK SIZE", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], superchunk_text, px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFDCE8F4u);
                } else if (row == 7) {
                    vi += hud_text(&verts[vi], "WALLS", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], g_rs.start_menu.world_create_walls_enabled ? "ENABLED" : "DISABLED", px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, g_rs.start_menu.world_create_walls_enabled ? 0xFF7CD992u : 0xFFE08C8Cu);
                } else if (row == 8) {
                    vi += hud_text(&verts[vi], "WALL RING SIZE", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], grid_size_text, px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFDCE8F4u);
                } else if (row == 9) {
                    vi += hud_text(&verts[vi], "GRID OFFSET X", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], grid_offset_x_text, px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFDCE8F4u);
                } else if (row == 10) {
                    vi += hud_text(&verts[vi], "GRID OFFSET Z", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], grid_offset_z_text, px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFDCE8F4u);
                } else if (row == 11) {
                    vi += hud_text(&verts[vi], "WALL THICKNESS", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], wall_thickness_text, px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFDCE8F4u);
                } else if (row == 12) {
                    vi += hud_text(&verts[vi], "WALL SHARING", row_x + 14.0f, row_y + row * row_h + 12.0f, 2.05f, 0xFFFFFFFFu);
                    vi += hud_text(&verts[vi], shared_walls_text, px + panel_w - 300.0f, row_y + row * row_h + 12.0f, 2.05f,
                                   g_rs.start_menu.world_create_wall_rings_shared ? 0xFF7CD992u : 0xFFDCE8F4u);
                }
            }

            vi += hud_text(&verts[vi], "ENTER TO CREATE WORLD", px + 24.0f, py + panel_h - 52.0f, 1.68f, 0xFFB8C0D0u);
            vi += hud_text(&verts[vi], "0-9 TYPE SEED   LEFT/RIGHT CHANGE VALUE   BACKSPACE RETURN", px + 24.0f, py + panel_h - 30.0f, 1.68f, 0xFFB8C0D0u);
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_MAP_GENERATING) {
            float progress = g_rs.start_menu.world_map_generation_progress;
            float bar_x = px + 48.0f;
            float bar_y = py + panel_h * 0.54f;
            float bar_w = panel_w - 96.0f;
            float bar_h = 22.0f;
            char load_text[48];
            char frontier_text[64];
            char tiles_text[64];
            char inflight_text[64];
            char pages_text[64];
            char workers_text[64];
            char rate_text[96];
            char last_tile_text[96];
            char threads_text[32];

            strcpy_s(load_text, sizeof(load_text), "PIPELINE LOAD");
            snprintf(frontier_text, sizeof(frontier_text), "FRONTIER RING %d", g_rs.start_menu.world_map_generation_ring);
            snprintf(tiles_text, sizeof(tiles_text), "TILES SAVED %d", g_rs.start_menu.world_map_generation_tiles_done);
            snprintf(inflight_text, sizeof(inflight_text), "IN FLIGHT %d", g_rs.start_menu.world_map_generation_inflight);
            snprintf(pages_text, sizeof(pages_text), "QUEUED PAGES %d", g_rs.start_menu.world_map_generation_queued_pages);
            snprintf(workers_text, sizeof(workers_text), "ACTIVE WORKERS %d", g_rs.start_menu.world_map_generation_active_workers);
            snprintf(rate_text, sizeof(rate_text), "SAVE RATE %.2f/s   LAST SAVE %.1fs AGO",
                     g_rs.start_menu.world_map_generation_tiles_per_sec,
                     g_rs.start_menu.world_map_generation_last_save_age_seconds);
            snprintf(last_tile_text, sizeof(last_tile_text), "LAST TILE %.0f ms   %d chunks",
                     g_rs.start_menu.world_map_generation_last_tile_ms,
                     g_rs.start_menu.world_map_generation_last_tile_chunks);
            snprintf(threads_text, sizeof(threads_text), "%d THREADS", g_rs.start_menu.world_map_generation_threads);

            vi += hud_text(&verts[vi], "GENERATING MAP", px + 24.0f, py + 50.0f, 2.8f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], g_rs.start_menu.selected_world_name, px + 24.0f, py + 90.0f, 1.92f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], threads_text, px + panel_w - 24.0f - hud_text_width_estimate(threads_text, 1.5f),
                           py + 90.0f, 1.5f, 0xFFBEE0FFu);
            vi += hud_text(&verts[vi], load_text, bar_x, bar_y - 24.0f, 1.38f, 0xFFB8C0D0u);

            vi += hud_quad(&verts[vi], bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, 0xD0222630u);
            if (progress > 0.0f) {
                vi += hud_quad(&verts[vi], bar_x + 2.0f, bar_y + 2.0f,
                               bar_x + 2.0f + (bar_w - 4.0f) * progress, bar_y + bar_h - 2.0f,
                               0xFF4A90D8u);
            }

            vi += hud_text(&verts[vi], frontier_text, px + 24.0f, bar_y + 38.0f, 1.64f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], tiles_text, px + 24.0f, bar_y + 68.0f, 1.64f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], inflight_text, px + 24.0f, bar_y + 98.0f, 1.5f, 0xFFBEE0FFu);
            vi += hud_text(&verts[vi], pages_text, px + 24.0f, bar_y + 124.0f, 1.42f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], workers_text, px + 24.0f, bar_y + 150.0f, 1.42f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi], rate_text, px + 24.0f, py + panel_h - 106.0f, 1.36f, 0xFFB8C0D0u);
            vi += hud_text(&verts[vi], last_tile_text, px + 24.0f, py + panel_h - 82.0f, 1.36f, 0xFFB8C0D0u);
            vi += hud_text(&verts[vi], g_rs.start_menu.world_map_generation_status,
                           px + 24.0f, py + panel_h - 56.0f, 1.45f, 0xFFB8C0D0u);

            if (g_rs.start_menu.world_map_generation_stage < 2) {
                vi += hud_text(&verts[vi], "BACKSPACE TO STOP", px + 24.0f, py + panel_h - 30.0f,
                               1.6f, 0xFFB8C0D0u);
            } else {
                vi += hud_text(&verts[vi], "STOPPING MAP TILE WORKERS...", px + 24.0f, py + panel_h - 30.0f,
                               1.6f, 0xFFB8C0D0u);
            }
        } else if (g_rs.start_menu.view == SDK_START_MENU_VIEW_WORLD_GENERATING) {
            if (g_rs.start_menu.world_gen_offline_generating) {
                float progress = g_rs.start_menu.world_gen_offline_progress;
                float bar_x = px + 48.0f;
                float bar_y = py + panel_h * 0.54f;
                float bar_w = panel_w - 96.0f;
                float bar_h = 22.0f;
                char superchunks_text[64];
                char ring_text[64];
                char workers_text[64];
                char rate_text[96];
                char threads_text[32];

                snprintf(superchunks_text, sizeof(superchunks_text), "SUPERCHUNKS %d", g_rs.start_menu.world_gen_offline_superchunks_done);
                snprintf(ring_text, sizeof(ring_text), "RING %d", g_rs.start_menu.world_gen_offline_current_ring);
                snprintf(workers_text, sizeof(workers_text), "ACTIVE WORKERS %d", g_rs.start_menu.world_gen_offline_active_workers);
                snprintf(rate_text, sizeof(rate_text), "%.1f CHUNKS/SEC", g_rs.start_menu.world_gen_offline_chunks_per_sec);
                snprintf(threads_text, sizeof(threads_text), "%d THREADS", g_rs.start_menu.world_gen_offline_threads);

                vi += hud_text(&verts[vi], "GENERATING WORLD", px + 24.0f, py + 50.0f, 2.8f, 0xFFFFFFFFu);
                vi += hud_text(&verts[vi], g_rs.start_menu.selected_world_name, px + 24.0f, py + 90.0f, 1.92f, 0xFFD0D8E0u);
                vi += hud_text(&verts[vi], threads_text, px + panel_w - 24.0f - hud_text_width_estimate(threads_text, 1.5f),
                               py + 90.0f, 1.5f, 0xFFBEE0FFu);

                vi += hud_quad(&verts[vi], bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, 0xD0222630u);
                if (progress > 0.0f) {
                    vi += hud_quad(&verts[vi], bar_x + 2.0f, bar_y + 2.0f,
                                   bar_x + 2.0f + (bar_w - 4.0f) * progress, bar_y + bar_h - 2.0f,
                                   0xFF4A90D8u);
                }

                vi += hud_text(&verts[vi], ring_text, px + 24.0f, bar_y + 38.0f, 1.64f, 0xFFFFFFFFu);
                vi += hud_text(&verts[vi], superchunks_text, px + 24.0f, bar_y + 68.0f, 1.64f, 0xFFD0D8E0u);
                vi += hud_text(&verts[vi], workers_text, px + 24.0f, bar_y + 98.0f, 1.5f, 0xFFBEE0FFu);
                vi += hud_text(&verts[vi], rate_text, px + 24.0f, py + panel_h - 106.0f, 1.36f, 0xFFB8C0D0u);
                vi += hud_text(&verts[vi], g_rs.start_menu.world_gen_offline_status,
                               px + 24.0f, py + panel_h - 56.0f, 1.45f, 0xFFB8C0D0u);

                if (g_rs.start_menu.world_gen_offline_stage < 2) {
                    vi += hud_text(&verts[vi], "BACKSPACE TO STOP", px + 24.0f, py + panel_h - 30.0f,
                                   1.6f, 0xFFB8C0D0u);
                } else {
                    vi += hud_text(&verts[vi], "STOPPING WORLD GENERATION...", px + 24.0f, py + panel_h - 30.0f,
                                   1.6f, 0xFFB8C0D0u);
                }
            } else {
                bool returning_to_start = g_rs.start_menu.view == SDK_START_MENU_VIEW_RETURNING_TO_START;
                float progress = g_rs.start_menu.world_generation_progress;
                float bar_x = px + 48.0f;
                float bar_y = py + panel_h * 0.5f;
                float bar_w = panel_w - 96.0f;
                float bar_h = 24.0f;
                char progress_text[32];
                const char* title_text = returning_to_start
                    ? "RETURNING TO START MENU"
                    : (g_rs.start_menu.world_generation_stage >= 2
                        ? "STARTING WORLD SESSION"
                        : "GENERATING WORLD");
                const char* detail_line_1 = "";
                const char* detail_line_2 = "";

                sprintf_s(progress_text, sizeof(progress_text), "%.0f%%", progress * 100.0f);

                if (!returning_to_start) {
                    if (g_rs.start_menu.world_generation_stage < 2) {
                        detail_line_1 = "Progress tracks completed prep stages, not a time estimate.";
                        detail_line_2 = "Cancellation ends once session bootstrap begins.";
                    } else {
                        detail_line_1 = "Bootstrap counts nearby sync-safe loads first, then background wall/remesh work.";
                        detail_line_2 = "The world can become playable before every backlog job finishes.";
                    }
                }

                vi += hud_text(&verts[vi], title_text, px + 24.0f, py + 50.0f, 2.8f, 0xFFFFFFFFu);

                vi += hud_quad(&verts[vi], bar_x, bar_y, bar_x + bar_w, bar_y + bar_h, 0xD0222630u);
                if (progress > 0.0f) {
                    vi += hud_quad(&verts[vi], bar_x + 2.0f, bar_y + 2.0f,
                                   bar_x + 2.0f + (bar_w - 4.0f) * progress, bar_y + bar_h - 2.0f,
                                   0xFF4AB060u);
                }

                vi += hud_text(&verts[vi], progress_text, bar_x + bar_w * 0.5f - 20.0f, bar_y + 4.0f, 1.8f, 0xFFFFFFFFu);

                vi += hud_text(&verts[vi], g_rs.start_menu.world_generation_status, px + 24.0f, bar_y + 40.0f, 1.6f, 0xFFD0D8E0u);
                if (detail_line_1[0]) {
                    vi += hud_text(&verts[vi], detail_line_1, px + 24.0f, bar_y + 70.0f, 1.22f, 0xFFB8C0D0u);
                }
                if (detail_line_2[0]) {
                    vi += hud_text(&verts[vi], detail_line_2, px + 24.0f, bar_y + 92.0f, 1.22f, 0xFFB8C0D0u);
                }

                if (returning_to_start) {
                    vi += hud_text(&verts[vi], "PLEASE WAIT WHILE THE WORLD IS SAVED", px + 24.0f, py + panel_h - 52.0f, 1.6f, 0xFFB8C0D0u);
                    vi += hud_text(&verts[vi], "DO NOT CLOSE THE GAME DURING THIS STEP", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
                } else if (g_rs.start_menu.world_generation_stage < 2) {
                    vi += hud_text(&verts[vi], "BACKSPACE TO CANCEL", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
                } else {
                    vi += hud_text(&verts[vi], "BOOTSTRAPPING WORLD SESSION PLEASE WAIT", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFFB8C0D0u);
                }
            }

            if (g_rs.start_menu.world_gen_summary_active) {
                /* Render summary screen */
                vi += hud_quad(&verts[vi], px, py, px + panel_w, py + panel_h, 0xD01A1E28u);

                float summary_y = py + 40.0f;
                vi += hud_text(&verts[vi], "WORLD GENERATION SUMMARY", px + 24.0f, summary_y, 2.0f, 0xFFFFFFFFu);
                summary_y += 40.0f;

                for (int i = 0; i < g_rs.start_menu.world_gen_summary_line_count && i < 16; ++i) {
                    vi += hud_text(&verts[vi], g_rs.start_menu.world_gen_summary_lines[i],
                                 px + 24.0f, summary_y, 1.3f, 0xFFD0D8E0u);
                    summary_y += 22.0f;
                }

                vi += hud_text(&verts[vi], "PRESS ENTER TO START", px + 24.0f, py + panel_h - 30.0f, 1.6f, 0xFF4AB060u);
            }
        }

        goto hud_finalize;
    }

    if (g_rs.editor_ui.open && vi + 1200 < HUD_MAX_VERTS) {
        float px = 26.0f;
        float py = 24.0f;
        float pw = (g_rs.editor_ui.kind == SDK_EDITOR_UI_ANIMATION || g_rs.editor_ui.kind == SDK_EDITOR_UI_PARTICLE)
            ? 440.0f
            : (g_rs.editor_ui.kind == SDK_EDITOR_UI_PROP)
                ? 540.0f
            : 380.0f;
        float ph = 88.0f;
        char title_text[96];

        vi += hud_panel_frame(&verts[vi], px, py, px + pw, py + ph, 0xD0181B22u, 0xFFCFB08Eu, 0xFF4A5666u);
        if (g_rs.editor_ui.kind == SDK_EDITOR_UI_ANIMATION) {
            sprintf_s(title_text, sizeof(title_text), "ANIMATION BUILDER  %s",
                      g_rs.editor_ui.animation_name[0] ? g_rs.editor_ui.animation_name : "UNNAMED");
        } else if (g_rs.editor_ui.kind == SDK_EDITOR_UI_PROP) {
            sprintf_s(title_text, sizeof(title_text), "PROP BUILDER  %s",
                      g_rs.editor_ui.character_name[0] ? g_rs.editor_ui.character_name : "UNNAMED");
        } else if (g_rs.editor_ui.kind == SDK_EDITOR_UI_BLOCK) {
            sprintf_s(title_text, sizeof(title_text), "BLOCK BUILDER  %s",
                      g_rs.editor_ui.character_name[0] ? g_rs.editor_ui.character_name : "UNNAMED");
        } else if (g_rs.editor_ui.kind == SDK_EDITOR_UI_ITEM) {
            sprintf_s(title_text, sizeof(title_text), "ITEM BUILDER  %s",
                      g_rs.editor_ui.character_name[0] ? g_rs.editor_ui.character_name : "UNNAMED");
        } else if (g_rs.editor_ui.kind == SDK_EDITOR_UI_PARTICLE) {
            sprintf_s(title_text, sizeof(title_text), "PARTICLE EFFECT BUILDER  %s",
                      g_rs.editor_ui.character_name[0] ? g_rs.editor_ui.character_name : "UNNAMED");
        } else {
            sprintf_s(title_text, sizeof(title_text), "CHARACTER BUILDER  %s",
                      g_rs.editor_ui.character_name[0] ? g_rs.editor_ui.character_name : "UNNAMED");
        }
        vi += hud_text(&verts[vi], title_text, px + 14.0f, py + 12.0f, 1.5f, 0xFFFFFFFFu);
        if (g_rs.editor_ui.kind == SDK_EDITOR_UI_ANIMATION ||
            g_rs.editor_ui.kind == SDK_EDITOR_UI_PARTICLE) {
            char frame_text[48];
            sprintf_s(frame_text, sizeof(frame_text), "%s %d / %d   %s",
                      (g_rs.editor_ui.kind == SDK_EDITOR_UI_PARTICLE) ? "SLICE" : "FRAME",
                      g_rs.editor_ui.current_frame + 1,
                      (g_rs.editor_ui.frame_count > 0) ? g_rs.editor_ui.frame_count : 1,
                      g_rs.editor_ui.playback_enabled ? "PLAYING" : "STATIC");
            vi += hud_text(&verts[vi], frame_text, px + 14.0f, py + 38.0f, 1.4f, 0xFFBEE0FFu);
        } else if (g_rs.editor_ui.kind == SDK_EDITOR_UI_PROP) {
            vi += hud_text(&verts[vi], "1024W x 1024D  16x16 CHUNKS  STONE FLOOR  1:1 SCALE",
                           px + 14.0f, py + 38.0f, 1.4f, 0xFFBEE0FFu);
        } else if (g_rs.editor_ui.kind == SDK_EDITOR_UI_BLOCK ||
                   g_rs.editor_ui.kind == SDK_EDITOR_UI_ITEM) {
            vi += hud_text(&verts[vi], "32W x 32D x 32H  TWO CHUNKS  CREATIVE COLORS",
                           px + 14.0f, py + 38.0f, 1.4f, 0xFFBEE0FFu);
        } else {
            vi += hud_text(&verts[vi], "24W x 16D x 64H  STONE FLOOR  CREATIVE BLOCKS",
                           px + 14.0f, py + 38.0f, 1.4f, 0xFFBEE0FFu);
        }
        vi += hud_text(&verts[vi], g_rs.editor_ui.status, px + 14.0f, py + 60.0f, 1.2f, 0xFFB8C0D0u);
    }

    if (vi + 18 < HUD_MAX_VERTS) {
        float hud_plate_x0 = bar_x - 34.0f;
        float hud_plate_y0 = bar_y - 78.0f;
        float hud_plate_x1 = bar_x + total_w + 34.0f;
        float hud_plate_y1 = bar_y + slot_size + 22.0f;
        vi += hud_quad(&verts[vi], hud_plate_x0, hud_plate_y0, hud_plate_x1, hud_plate_y1, 0x9010141Cu);
        vi += hud_quad(&verts[vi], hud_plate_x0, hud_plate_y0, hud_plate_x1, hud_plate_y0 + 3.0f, 0xB05C6A7Cu);
        vi += hud_quad(&verts[vi], hud_plate_x0, hud_plate_y1 - 3.0f, hud_plate_x1, hud_plate_y1, 0xB0181E28u);
    }

    /* --- Hearts row above hotbar --- */
    {
        int max_hearts = g_rs.player_max_health / 2; /* 10 hearts */
        float heart_size = 28.0f;
        float heart_gap  = 4.0f;
        float hearts_x = bar_x;
        float hearts_y = bar_y - heart_size - 10.0f;

        for (int h = 0; h < max_hearts && vi + 12 < HUD_MAX_VERTS; h++) {
            float hx = hearts_x + h * (heart_size + heart_gap);
            int hp_for_heart = (h + 1) * 2; /* HP needed for full heart */

            uint32_t col;
            if (g_rs.player_health >= hp_for_heart) {
                col = 0xFF0000CC; /* Full heart — red (ABGR) */
            } else if (g_rs.player_health >= hp_for_heart - 1) {
                col = 0xFF0044AA; /* Half heart — darker red */
            } else {
                col = 0xFF333333; /* Empty heart — dark gray */
            }

            /* Flash hearts when invincible */
            if (g_rs.player_invincible && g_rs.player_health >= hp_for_heart - 1) {
                col = 0xFFFFFFFF; /* White flash */
            }

            /* Heart shape: simple square + smaller square on top-left/right */
            vi += hud_quad(&verts[vi], hx + 1.0f, hearts_y + 3.0f,
                           hx + heart_size - 1.0f, hearts_y + heart_size, col);
            vi += hud_quad(&verts[vi], hx, hearts_y,
                           hx + heart_size * 0.45f, hearts_y + 6.0f, col);
            vi += hud_quad(&verts[vi], hx + heart_size * 0.55f, hearts_y,
                           hx + heart_size, hearts_y + 6.0f, col);
        }
    }

    /* --- Hunger drumsticks row above hotbar (right side) --- */
    {
        int max_drums = g_rs.player_max_hunger / 2; /* 10 drumsticks */
        float drum_size = 28.0f;
        float drum_gap  = 4.0f;
        float drums_x = bar_x + total_w - max_drums * (drum_size + drum_gap) + drum_gap;
        float drums_y = bar_y - drum_size - 10.0f;

        for (int d = 0; d < max_drums && vi + 6 < HUD_MAX_VERTS; d++) {
            float dx = drums_x + d * (drum_size + drum_gap);
            int hunger_for_drum = (d + 1) * 2;

            uint32_t col;
            if (g_rs.player_hunger >= hunger_for_drum) {
                col = 0xFF0088CC; /* Full — orange-brown (ABGR) */
            } else if (g_rs.player_hunger >= hunger_for_drum - 1) {
                col = 0xFF005588; /* Half — darker */
            } else {
                col = 0xFF333333; /* Empty — dark gray */
            }

            /* Drumstick shape: rectangle body + small circle top */
            vi += hud_quad(&verts[vi], dx + 2.0f, drums_y + 3.0f,
                           dx + drum_size - 2.0f, drums_y + drum_size, col);
            vi += hud_quad(&verts[vi], dx + 1.0f, drums_y,
                           dx + drum_size - 1.0f, drums_y + 5.0f, col);
        }
    }

    /* --- XP / level bar above the hotbar --- */
    if (vi + 300 < HUD_MAX_VERTS) {
        float xp_bar_w = total_w * 0.80f;
        float xp_bar_h = 18.0f;
        float xp_x = (W - xp_bar_w) * 0.5f;
        float xp_y = bar_y - 40.0f;
        float xp_fill = 0.0f;
        int xp_current = g_rs.skills.xp_current;
        int xp_to_next = g_rs.skills.xp_to_next;

        if (xp_to_next > 0) {
            xp_fill = (float)xp_current / (float)xp_to_next;
            if (xp_fill < 0.0f) xp_fill = 0.0f;
            if (xp_fill > 1.0f) xp_fill = 1.0f;
        }

        vi += hud_quad(&verts[vi], xp_x, xp_y, xp_x + xp_bar_w, xp_y + xp_bar_h, 0xC0102810);
        vi += hud_quad(&verts[vi], xp_x + 1.0f, xp_y + 1.0f,
                       xp_x + xp_bar_w - 1.0f, xp_y + xp_bar_h - 1.0f, 0xD0204020);
        if (xp_fill > 0.001f) {
            vi += hud_quad(&verts[vi], xp_x + 1.0f, xp_y + 1.0f,
                           xp_x + 1.0f + (xp_bar_w - 2.0f) * xp_fill,
                           xp_y + xp_bar_h - 1.0f, 0xFF20D040);
        }

        vi += hud_text(&verts[vi], "LVL", xp_x + xp_bar_w * 0.5f - 44.0f, xp_y - 26.0f, 3.8f, 0xFFB8FF78);
        vi += hud_number(&verts[vi], g_rs.skills.level, xp_x + xp_bar_w * 0.5f + 22.0f, xp_y - 26.0f, 3.8f, 0xFFB8FF78);
        vi += hud_text(&verts[vi], "SP", xp_x - 54.0f, xp_y - 26.0f, 3.8f, 0xFFFFFFFF);
        vi += hud_number(&verts[vi], g_rs.skills.unspent_skill_points, xp_x - 6.0f, xp_y - 26.0f, 3.8f, 0xFFFFFFFF);
    }

    for (int i = 0; i < num_slots && vi + 200 < HUD_MAX_VERTS; i++) {
        float sx = bar_x + i * (slot_size + slot_gap);
        float sy = bar_y;

        /* Slot border (selected = bright white, else dark gray) */
        uint32_t border_col = (i == g_rs.hotbar_selected) ? 0xFFFFFFFF : 0xFF606060;
        vi += hud_quad(&verts[vi], sx, sy, sx + slot_size, sy + slot_size, border_col);

        /* Slot inner background (dark) */
        float in_x0 = sx + border, in_y0 = sy + border;
        float in_x1 = sx + slot_size - border, in_y1 = sy + slot_size - border;
        vi += hud_quad(&verts[vi], in_x0, in_y0, in_x1, in_y1, 0xC0202020);

        /* Item color swatch if slot has items */
        if ((g_rs.hotbar[i].item_type > 0 || g_rs.hotbar[i].direct_block_type > 0) &&
            g_rs.hotbar[i].count > 0) {
            int item_id = g_rs.hotbar[i].item_type;
            int direct_block = g_rs.hotbar[i].direct_block_type;
            uint32_t item_col;
            if (direct_block > 0) {
                item_col = sdk_block_get_face_color((BlockType)direct_block, 3);
            } else if (sdk_item_is_block((ItemType)item_id)) {
                BlockType bt = sdk_item_to_block((ItemType)item_id);
                item_col = sdk_block_get_face_color(bt, 3);
            } else {
                item_col = sdk_item_get_color((ItemType)item_id);
            }
            float pad = 8.0f;
            vi += hud_quad(&verts[vi], sx + pad, sy + pad,
                           sx + slot_size - pad, sy + slot_size - pad - 12.0f, item_col);

            /* Item count number (skip for tools since count is always 1) */
            if (direct_block > 0 || !sdk_item_is_tool((ItemType)item_id) || g_rs.hotbar[i].count > 1)
                vi += hud_number(&verts[vi], g_rs.hotbar[i].count,
                                 sx + 6.0f, sy + slot_size - 18.0f, 3.2f, 0xFFFFFFFF);
        }
    }

    /* --- Superchunk map HUD --- */
    if (g_rs.map_ui.open &&
        g_rs.map_ui.width > 0 && g_rs.map_ui.width <= SDK_MAP_UI_MAX_DIM &&
        g_rs.map_ui.height > 0 && g_rs.map_ui.height <= SDK_MAP_UI_MAX_DIM &&
        vi + g_rs.map_ui.width * g_rs.map_ui.height * 6 + 1024 < HUD_MAX_VERTS) {
        bool focused = g_rs.map_ui.focused;
        float map_px = focused ? ((W * 0.56f < H * 0.62f) ? W * 0.56f : H * 0.62f) : 156.0f;
        float cell_w;
        float cell_h;
        float pad = focused ? 16.0f : 10.0f;
        float title_h = focused ? 30.0f : 22.0f;
        float footer_h = focused ? 62.0f : 40.0f;
        float panel_w;
        float panel_h;
        float px;
        float py;
        float mx0 = 0.0f;
        float my0;
        float player_px0;
        float player_py0;

        if (g_rs.map_ui.zoom_tenths < 1) g_rs.map_ui.zoom_tenths = 1;
        if (g_rs.map_ui.zoom_tenths > 1000) g_rs.map_ui.zoom_tenths = 1000;

        if (map_px < 132.0f) map_px = 132.0f;
        if (!focused && map_px > 172.0f) map_px = 172.0f;

        panel_w = map_px + pad * 2.0f;
        panel_h = map_px + pad * 2.0f + title_h + footer_h;
        px = focused ? (W - panel_w) * 0.5f : (W - panel_w - 18.0f);
        py = focused ? 34.0f : 18.0f;
        mx0 = px + pad;
        my0 = py + pad + title_h;
        cell_w = map_px / (float)g_rs.map_ui.width;
        cell_h = map_px / (float)g_rs.map_ui.height;

        vi += hud_panel_frame(&verts[vi], px, py, px + panel_w, py + panel_h,
                              focused ? 0xEA11161Du : 0xD812171Fu,
                              0xFFD7B38Au, 0xFF7A664Eu);
        vi += hud_quad(&verts[vi], mx0 - 2.0f, my0 - 2.0f, mx0 + map_px + 2.0f, my0 + map_px + 2.0f, 0xA01A2028u);
        vi += hud_text(&verts[vi], focused ? "MAP FOCUS" : "MAP", px + pad, py + 8.0f, focused ? 2.1f : 1.8f, 0xFFFFFFFF);
        if (focused && vi + 96 < HUD_MAX_VERTS) {
            const char* close_text = "M CLOSE";
            vi += hud_text(&verts[vi], close_text,
                           px + panel_w - pad - hud_text_width_estimate(close_text, 1.2f),
                           py + 12.0f, 1.2f, 0xFFE3D6C6u);
        } else if (vi + 48 < HUD_MAX_VERTS) {
            const char* expand_text = "M EXPAND";
            vi += hud_text(&verts[vi], expand_text,
                           px + panel_w - pad - hud_text_width_estimate(expand_text, 1.1f),
                           py + 12.0f, 1.1f, 0xFFE3D6C6u);
        }

        for (int mz = 0; mz < g_rs.map_ui.height; ++mz) {
            for (int mx = 0; mx < g_rs.map_ui.width; ++mx) {
                uint32_t col = g_rs.map_ui.pixels[mz * g_rs.map_ui.width + mx];
                float x0 = mx0 + (float)mx * cell_w;
                float y0 = my0 + (float)mz * cell_h;
                vi += hud_quad(&verts[vi], x0, y0, x0 + cell_w, y0 + cell_h, col);
            }
        }

        for (int grid = 0; grid <= 8 && vi + 24 < HUD_MAX_VERTS; ++grid) {
            float t = (float)grid / 8.0f;
            float gx = mx0 + map_px * t;
            float gy = my0 + map_px * t;
            uint32_t grid_col = focused ? 0x907E8A96u : 0x70717A84u;
            vi += hud_quad(&verts[vi], gx - 1.0f, my0, gx + 1.0f, my0 + map_px, grid_col);
            vi += hud_quad(&verts[vi], mx0, gy - 1.0f, mx0 + map_px, gy + 1.0f, grid_col);
        }

        if (g_rs.map_ui.player_cell_x >= 0 && g_rs.map_ui.player_cell_x < g_rs.map_ui.width &&
            g_rs.map_ui.player_cell_z >= 0 && g_rs.map_ui.player_cell_z < g_rs.map_ui.height &&
            vi + 24 < HUD_MAX_VERTS) {
            player_px0 = mx0 + (float)g_rs.map_ui.player_cell_x * cell_w;
            player_py0 = my0 + (float)g_rs.map_ui.player_cell_z * cell_h;
            vi += hud_quad(&verts[vi], player_px0 - 2.0f, player_py0 - 2.0f,
                           player_px0 + cell_w + 2.0f, player_py0 + cell_h + 2.0f, 0xFFFFFFFF);
            vi += hud_quad(&verts[vi], player_px0, player_py0,
                           player_px0 + cell_w, player_py0 + cell_h, 0xFF2040FF);
            vi += hud_quad(&verts[vi], player_px0 + cell_w * 0.25f, player_py0 + cell_h * 0.25f,
                           player_px0 + cell_w * 0.75f, player_py0 + cell_h * 0.75f, 0xFFFF5040);
        }

        if (focused && vi + 24 < HUD_MAX_VERTS) {
            float cx0 = mx0 + map_px * 0.5f - 5.0f;
            float cy0 = my0 + map_px * 0.5f - 1.0f;
            vi += hud_quad(&verts[vi], cx0, cy0, cx0 + 10.0f, cy0 + 2.0f, 0xFFE8E19Au);
            vi += hud_quad(&verts[vi], cx0 + 4.0f, cy0 - 4.0f, cx0 + 6.0f, cy0 + 6.0f, 0xFFE8E19Au);
        }

        if (vi + 48 < HUD_MAX_VERTS) {
            float bar_x0 = mx0;
            float bar_y0 = my0 + map_px + 8.0f;
            float bar_x1 = mx0 + map_px;
            float t = ((float)g_rs.map_ui.zoom_tenths - 1.0f) / 999.0f;
            float handle_x = bar_x0 + (bar_x1 - bar_x0) * t;
            char map_info[96];
            char compare_info[96];
            const char* build_kind =
                g_rs.map_ui.focused_tile_build_kind == 1 ? "EXACT" : "FALLBACK";

            vi += hud_quad(&verts[vi], bar_x0, bar_y0 + 7.0f, bar_x1, bar_y0 + 11.0f, 0xA0283038u);
            vi += hud_quad(&verts[vi], handle_x - 5.0f, bar_y0 + 2.0f, handle_x + 5.0f, bar_y0 + 16.0f, 0xFFE8E0D4u);
            vi += hud_text(&verts[vi], "0.1", bar_x0, bar_y0 - 2.0f, 1.1f, 0xFFD8D0C8u);
            vi += hud_text(&verts[vi], "100.0",
                           bar_x1 - hud_text_width_estimate("100.0", 1.1f),
                           bar_y0 - 2.0f, 1.1f, 0xFFD8D0C8u);
            if (g_rs.map_ui.focused_tile_ready) {
                sprintf_s(map_info,
                          sizeof(map_info),
                          "EX %d  FB %d  FOCUS %s",
                          g_rs.map_ui.visible_exact_tiles,
                          g_rs.map_ui.visible_fallback_tiles,
                          build_kind);
            } else {
                sprintf_s(map_info,
                          sizeof(map_info),
                          "EX %d  FB %d  FOCUS PENDING",
                          g_rs.map_ui.visible_exact_tiles,
                          g_rs.map_ui.visible_fallback_tiles);
            }
            vi += hud_text(&verts[vi], map_info, px + pad, bar_y0 + 18.0f, focused ? 1.15f : 1.0f, 0xFFE3D6C6u);
            if (focused && g_rs.map_ui.focused_tile_compare_valid) {
                sprintf_s(compare_info,
                          sizeof(compare_info),
                          "EXACT/FALLBACK DIFF %u PX",
                          (unsigned)g_rs.map_ui.focused_tile_compare_diff_pixels);
                vi += hud_text(&verts[vi], compare_info,
                               px + pad,
                               bar_y0 + 34.0f, 1.15f, 0xFFE3D6C6u);
            }
            if (focused) {
                vi += hud_text(&verts[vi], "WASD PAN",
                               px + pad,
                               py + panel_h - 24.0f, 1.2f, 0xFFE3D6C6u);
                vi += hud_text(&verts[vi], "HOLD LEFT/RIGHT TO ZOOM",
                               px + panel_w - pad - hud_text_width_estimate("HOLD LEFT/RIGHT TO ZOOM", 1.2f),
                               py + panel_h - 24.0f, 1.2f, 0xFFE3D6C6u);
            }
        }
    }

    /* --- Crafting grid overlay --- */
    if (g_rs.crafting.open && vi + 600 < HUD_MAX_VERTS) {
        int cw = g_rs.crafting.grid_w, ch = g_rs.crafting.grid_h;
        float cell = 44.0f, gap = 3.0f, pad = 8.0f;
        float grid_total_w = cw * cell + (cw - 1) * gap;
        float grid_total_h = ch * cell + (ch - 1) * gap;
        float panel_w = grid_total_w + cell + gap * 2 + cell + pad * 2; /* grid + arrow + result */
        float panel_h = grid_total_h + pad * 2 + 20.0f; /* +20 for title */
        float px = (W - panel_w) * 0.5f;
        float py = (H - panel_h) * 0.5f - 40.0f;

        /* Panel background */
        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + panel_h, 0xD0181818);
        /* Panel border */
        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + 2.0f, 0xFF808080);
        vi += hud_quad(&verts[vi], px, py + panel_h - 2.0f, px + panel_w, py + panel_h, 0xFF808080);
        vi += hud_quad(&verts[vi], px, py, px + 2.0f, py + panel_h, 0xFF808080);
        vi += hud_quad(&verts[vi], px + panel_w - 2.0f, py, px + panel_w, py + panel_h, 0xFF808080);

        /* Title: "CRAFT" */
        float title_x = px + pad;
        float title_y = py + 4.0f;
        /* C */ vi += hud_quad(&verts[vi], title_x, title_y, title_x+6, title_y+10, 0xFFFFFFFF);
        title_x += 8;
        /* R */ vi += hud_quad(&verts[vi], title_x, title_y, title_x+6, title_y+10, 0xFFFFFFFF);
        title_x += 8;
        /* A */ vi += hud_quad(&verts[vi], title_x, title_y, title_x+6, title_y+10, 0xFFFFFFFF);
        title_x += 8;
        /* F */ vi += hud_quad(&verts[vi], title_x, title_y, title_x+6, title_y+10, 0xFFFFFFFF);
        title_x += 8;
        /* T */ vi += hud_quad(&verts[vi], title_x, title_y, title_x+6, title_y+10, 0xFFFFFFFF);

        float gx0 = px + pad;
        float gy0 = py + pad + 16.0f;

        /* Grid cells */
        for (int gy = 0; gy < ch; gy++) {
            for (int gx = 0; gx < cw; gx++) {
                int idx = gy * cw + gx;
                float cx0 = gx0 + gx * (cell + gap);
                float cy0 = gy0 + gy * (cell + gap);

                /* Cell border (highlighted if cursor) */
                uint32_t bcol = (idx == g_rs.crafting.cursor) ? 0xFFFFFF00 : 0xFF606060;
                vi += hud_quad(&verts[vi], cx0, cy0, cx0 + cell, cy0 + cell, bcol);
                /* Cell inner */
                vi += hud_quad(&verts[vi], cx0 + 2, cy0 + 2, cx0 + cell - 2, cy0 + cell - 2, 0xC0202020);

                /* Item swatch */
                if (g_rs.crafting.grid_count[idx] > 0 && g_rs.crafting.grid_item[idx] > 0) {
                    int item_id = g_rs.crafting.grid_item[idx];
                    uint32_t ic;
                    if (sdk_item_is_block((ItemType)item_id)) {
                        BlockType bt = sdk_item_to_block((ItemType)item_id);
                        ic = sdk_block_get_face_color(bt, 3);
                    } else {
                        ic = sdk_item_get_color((ItemType)item_id);
                    }
                    vi += hud_quad(&verts[vi], cx0 + 6, cy0 + 4, cx0 + cell - 6, cy0 + cell - 14, ic);
                    vi += hud_number(&verts[vi], g_rs.crafting.grid_count[idx],
                                     cx0 + 4, cy0 + cell - 12, 1.5f, 0xFFFFFFFF);
                }
            }
        }

        /* Arrow (→) between grid and result */
        float arrow_x = gx0 + grid_total_w + gap * 2;
        float arrow_y = gy0 + grid_total_h * 0.5f - 4.0f;
        vi += hud_quad(&verts[vi], arrow_x, arrow_y, arrow_x + cell * 0.6f, arrow_y + 8.0f, 0xFFAAAAAA);

        /* Result slot */
        float rx = arrow_x + cell * 0.6f + gap * 2;
        float ry = gy0 + grid_total_h * 0.5f - cell * 0.5f;
        uint32_t result_border = (g_rs.crafting.result_item > 0) ? 0xFF00FF00 : 0xFF404040;
        vi += hud_quad(&verts[vi], rx, ry, rx + cell, ry + cell, result_border);
        vi += hud_quad(&verts[vi], rx + 2, ry + 2, rx + cell - 2, ry + cell - 2, 0xC0202020);

        if (g_rs.crafting.result_item > 0) {
            int rid = g_rs.crafting.result_item;
            uint32_t rc;
            if (sdk_item_is_block((ItemType)rid)) {
                BlockType bt = sdk_item_to_block((ItemType)rid);
                rc = sdk_block_get_face_color(bt, 3);
            } else {
                rc = sdk_item_get_color((ItemType)rid);
            }
            vi += hud_quad(&verts[vi], rx + 6, ry + 4, rx + cell - 6, ry + cell - 14, rc);
            if (g_rs.crafting.result_count > 1)
                vi += hud_number(&verts[vi], g_rs.crafting.result_count,
                                 rx + 4, ry + cell - 12, 1.5f, 0xFFFFFFFF);
        }
    }

    /* --- Station overlay --- */
    if (g_rs.station.open && vi + 6000 < HUD_MAX_VERTS) {
        const char* title = "STATION";
        float panel_w = 360.0f;
        float panel_h = 190.0f;
        float px = (W - panel_w) * 0.5f;
        float py = (H - panel_h) * 0.5f - 30.0f;
        float cell = 40.0f;
        float input_x = px + 30.0f;
        float input_y = py + 82.0f;
        float fuel_x = px + 30.0f;
        float fuel_y = py + 132.0f;
        float output_x = px + panel_w - 70.0f;
        float output_y = py + 107.0f;

        switch ((BlockType)g_rs.station.block_type) {
            case BLOCK_FURNACE: title = "FURNACE"; break;
            case BLOCK_CAMPFIRE: title = "CAMPFIRE"; break;
            case BLOCK_ANVIL: title = "ANVIL"; break;
            case BLOCK_BLACKSMITHING_TABLE: title = "BLACKSMITHING"; break;
            case BLOCK_LEATHERWORKING_TABLE: title = "LEATHERWORKING"; break;
            default: break;
        }

        auto draw_station_slot = [&](float sx, float sy, int item_id, int count, bool hovered) {
            uint32_t border = hovered ? 0xFFFFFF00u : 0xFF606060u;
            vi += hud_quad(&verts[vi], sx, sy, sx + cell, sy + cell, border);
            vi += hud_quad(&verts[vi], sx + 2.0f, sy + 2.0f, sx + cell - 2.0f, sy + cell - 2.0f, 0xC0202020);
            if (count > 0 && item_id > 0) {
                uint32_t ic;
                if (sdk_item_is_block((ItemType)item_id)) {
                    BlockType bt = sdk_item_to_block((ItemType)item_id);
                    ic = sdk_block_get_face_color(bt, 3);
                } else {
                    ic = sdk_item_get_color((ItemType)item_id);
                }
                vi += hud_quad(&verts[vi], sx + 6.0f, sy + 4.0f, sx + cell - 6.0f, sy + cell - 14.0f, ic);
                vi += hud_number(&verts[vi], count, sx + 4.0f, sy + cell - 12.0f, 1.5f, 0xFFFFFFFF);
            }
        };

        vi += hud_quad(&verts[vi], 0.0f, 0.0f, W, H, 0x50000000);
        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + panel_h, 0xE0161820);
        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + 3.0f, 0xFF9098A8);
        vi += hud_quad(&verts[vi], px, py + panel_h - 3.0f, px + panel_w, py + panel_h, 0xFF9098A8);
        vi += hud_quad(&verts[vi], px, py, px + 3.0f, py + panel_h, 0xFF9098A8);
        vi += hud_quad(&verts[vi], px + panel_w - 3.0f, py, px + panel_w, py + panel_h, 0xFF9098A8);
        vi += hud_text(&verts[vi], title, px + 18.0f, py + 16.0f, 3.0f, 0xFFFFFFFF);

        if (g_rs.station.kind == SDK_STATION_UI_PLACEHOLDER) {
            vi += hud_text(&verts[vi], "COMING SOON", px + 70.0f, py + 90.0f, 3.0f, 0xFFD8E0EC);
            vi += hud_text(&verts[vi], "RIGHT CLICK OUTSIDE TO CLOSE", px + 32.0f, py + 142.0f, 1.4f, 0xFFB8C0D0);
        } else {
            vi += hud_text(&verts[vi], "INPUT", input_x - 2.0f, py + 60.0f, 1.6f, 0xFFD0D8E0);
            draw_station_slot(input_x, input_y, g_rs.station.input_item, g_rs.station.input_count, g_rs.station.hovered_slot == 0);

            if (g_rs.station.kind == SDK_STATION_UI_FURNACE) {
                vi += hud_text(&verts[vi], "FUEL", fuel_x - 2.0f, py + 110.0f, 1.6f, 0xFFD0D8E0);
                draw_station_slot(fuel_x, fuel_y, g_rs.station.fuel_item, g_rs.station.fuel_count, g_rs.station.hovered_slot == 1);
                vi += hud_quad(&verts[vi], px + 104.0f, py + 118.0f, px + 120.0f, py + 162.0f, 0xD020242C);
                if (g_rs.station.burn_remaining > 0 && g_rs.station.burn_max > 0) {
                    float burn_fill = (float)g_rs.station.burn_remaining / (float)g_rs.station.burn_max;
                    if (burn_fill < 0.0f) burn_fill = 0.0f;
                    if (burn_fill > 1.0f) burn_fill = 1.0f;
                    vi += hud_quad(&verts[vi], px + 106.0f, py + 160.0f - 40.0f * burn_fill,
                                   px + 118.0f, py + 160.0f, 0xFFFFA040);
                }
            }

            vi += hud_quad(&verts[vi], px + 122.0f, py + 114.0f, px + 254.0f, py + 126.0f, 0xD020242C);
            if (g_rs.station.progress > 0 && g_rs.station.progress_max > 0) {
                float prog_fill = (float)g_rs.station.progress / (float)g_rs.station.progress_max;
                if (prog_fill < 0.0f) prog_fill = 0.0f;
                if (prog_fill > 1.0f) prog_fill = 1.0f;
                vi += hud_quad(&verts[vi], px + 124.0f, py + 116.0f,
                               px + 124.0f + 128.0f * prog_fill, py + 124.0f, 0xFF4AB060);
            }

            vi += hud_text(&verts[vi], "OUTPUT", output_x - 4.0f, py + 85.0f, 1.6f, 0xFFD0D8E0);
            draw_station_slot(output_x, output_y, g_rs.station.output_item, g_rs.station.output_count, g_rs.station.hovered_slot == 2);
            vi += hud_text(&verts[vi], "RIGHT CLICK OUTSIDE TO CLOSE", px + 42.0f, py + 162.0f, 1.4f, 0xFFB8C0D0);
        }
    }

    /* --- Pause menu overlay --- */
    if (g_rs.pause_menu.open && vi + 12000 < HUD_MAX_VERTS) {
        const char* leave_world_label = g_rs.pause_menu.local_hosted_session
            ? "LEAVE HOSTED GAME"
            : (g_rs.pause_menu.remote_session ? "DISCONNECT" : "EXIT WORLD");
        const char* option_labels[8] = {
            "GRAPHICS SETTINGS",
            "KEY BINDINGS",
            "CREATIVE MODE",
            "SELECT CHARACTER",
            "CHUNK MANAGER",
            "DEBUG PROFILER",
            leave_world_label,
            "EXIT GAME"
        };
        float panel_w = (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_GRAPHICS) ? (W * 0.72f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_KEY_BINDINGS) ? (W * 0.68f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_DEBUG_PROFILER) ? (W * 0.60f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER) ? (W * 0.70f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_CREATIVE) ? (W * 0.48f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_SELECT_CHARACTER) ? (W * 0.52f)
                      : (W * 0.42f);
        float panel_h = (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_GRAPHICS) ? (H * 0.82f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_KEY_BINDINGS) ? (H * 0.82f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_DEBUG_PROFILER) ? (H * 0.88f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER) ? (H * 0.85f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_CREATIVE) ? (H * 0.86f)
                      : (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_SELECT_CHARACTER) ? (H * 0.74f)
                      : (H * 0.72f);
        float px = (W - panel_w) * 0.5f;
        float py = (H - panel_h) * 0.5f;

        vi += hud_quad(&verts[vi], 0.0f, 0.0f, W, H, 0x50000000);
        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + panel_h, 0xE0161820);
        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + 3.0f, 0xFF9098A8);
        vi += hud_quad(&verts[vi], px, py + panel_h - 3.0f, px + panel_w, py + panel_h, 0xFF9098A8);
        vi += hud_quad(&verts[vi], px, py, px + 3.0f, py + panel_h, 0xFF9098A8);
        vi += hud_quad(&verts[vi], px + panel_w - 3.0f, py, px + panel_w, py + panel_h, 0xFF9098A8);

        if (g_rs.editor_ui.open) {
            option_labels[3] = (g_rs.editor_ui.kind == SDK_EDITOR_UI_ANIMATION)
                ? "SAVE AND RETURN TO ANIMATIONS"
                : (g_rs.editor_ui.kind == SDK_EDITOR_UI_PROP)
                    ? "SAVE AND RETURN TO PROPS"
                : (g_rs.editor_ui.kind == SDK_EDITOR_UI_BLOCK)
                    ? "SAVE AND RETURN TO BLOCKS"
                    : (g_rs.editor_ui.kind == SDK_EDITOR_UI_ITEM)
                        ? "SAVE AND RETURN TO ITEMS"
                        : (g_rs.editor_ui.kind == SDK_EDITOR_UI_PARTICLE)
                            ? "SAVE AND RETURN TO PARTICLE FX"
                            : "SAVE AND RETURN TO CHARACTERS";
        }

        if (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_MAIN) {
            vi += hud_text(&verts[vi], "PAUSE MENU", px + 22.0f, py + 30.0f, 3.35f, 0xFFFFFFFF);
            vi += hud_text(&verts[vi], "BACKSPACE CLOSES", px + panel_w - 220.0f, py + 36.0f, 1.48f, 0xFFB8C0D0);

            for (int i = 0; i < SDK_PAUSE_MENU_OPTION_COUNT; ++i) {
                float row_y = py + 108.0f + i * 62.0f;
                uint32_t row_col = (i == g_rs.pause_menu.selected) ? 0xFF80522D : 0xD0222630;
                vi += hud_quad(&verts[vi], px + 22.0f, row_y, px + panel_w - 22.0f, row_y + 50.0f, row_col);
                vi += hud_text(&verts[vi], option_labels[i], px + 36.0f, row_y + 13.0f, 1.98f, 0xFFFFFFFF);
            }
        } else if (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_GRAPHICS) {
            float row_y = py + 108.0f;
            float row_h = 48.0f;
            float slider_x0 = px + 28.0f;
            float slider_x1 = px + panel_w - 28.0f;
            char resolution_text[64];
            char render_scale_text[16];
            char render_distance_text[32];
            char anisotropy_text[16];
            char far_lod_text[32];
            char experimental_far_text[32];
            char wall_color_text[16];
            char superchunk_load_text[16];
            char vsync_text[16];
            char fog_text[16];
            char aa_text[16];
            char shadow_text[16];
            char water_text[16];
            const char* row_labels[SDK_GRAPHICS_MENU_ROW_COUNT] = {
                "QUALITY PRESET", "DISPLAY MODE", "RESOLUTION", "RENDER SCALE",
                "ANTI-ALIASING", "SMOOTH LIGHTING", "SHADOW QUALITY", "WATER QUALITY",
                "RENDER DISTANCE", "ANISOTROPIC SAMPLING", "FAR TERRAIN LOD DISTANCE",
                "EXPERIMENTAL FAR MESHES",
                "BOUNDARY WALLS", "SUPERCHUNK LOAD", "VSYNC", "FOG"
            };
            const char* row_values[SDK_GRAPHICS_MENU_ROW_COUNT];
            int graphics_row = hud_clampi(g_rs.pause_menu.graphics_selected, 0, SDK_GRAPHICS_MENU_ROW_COUNT - 1);

            build_graphics_resolution_text(resolution_text, sizeof(resolution_text));
            sprintf_s(render_scale_text, sizeof(render_scale_text), "%d%%", g_rs.pause_menu.graphics_render_scale_percent);
            build_render_distance_text(g_rs.pause_menu.graphics_render_distance_chunks,
                                       render_distance_text, sizeof(render_distance_text));
            build_anisotropy_text(g_rs.pause_menu.graphics_anisotropy_level,
                                  anisotropy_text, sizeof(anisotropy_text));
            build_far_mesh_distance_text(g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks,
                                         far_lod_text, sizeof(far_lod_text));
            build_far_mesh_distance_text(g_rs.pause_menu.graphics_experimental_far_mesh_distance_chunks,
                                         experimental_far_text, sizeof(experimental_far_text));
            sprintf_s(wall_color_text, sizeof(wall_color_text), "%s",
                      g_rs.pause_menu.graphics_black_superchunk_walls ? "BLACK" : "TEXTURED");
            sprintf_s(superchunk_load_text, sizeof(superchunk_load_text), "%s",
                      (g_graphics_settings.superchunk_load_mode == SDK_SUPERCHUNK_LOAD_ASYNC) ? "ASYNC" : "SYNC");
            sprintf_s(vsync_text, sizeof(vsync_text), "%s", on_off_text(g_rs.pause_menu.graphics_vsync));
            sprintf_s(fog_text, sizeof(fog_text), "%s", on_off_text(g_rs.pause_menu.graphics_fog_enabled));
            sprintf_s(aa_text, sizeof(aa_text), "%s",
                      graphics_anti_aliasing_name(g_rs.pause_menu.graphics_anti_aliasing_mode));
            sprintf_s(shadow_text, sizeof(shadow_text), "%s",
                      graphics_shadow_quality_name(g_rs.pause_menu.graphics_shadow_quality));
            sprintf_s(water_text, sizeof(water_text), "%s",
                      graphics_water_quality_name(g_rs.pause_menu.graphics_water_quality));

            row_values[SDK_GRAPHICS_MENU_ROW_QUALITY_PRESET] = graphics_preset_name(g_rs.pause_menu.graphics_preset);
            row_values[SDK_GRAPHICS_MENU_ROW_DISPLAY_MODE] = graphics_display_mode_name(g_rs.pause_menu.graphics_display_mode);
            row_values[SDK_GRAPHICS_MENU_ROW_RESOLUTION] = resolution_text;
            row_values[SDK_GRAPHICS_MENU_ROW_RENDER_SCALE] = render_scale_text;
            row_values[SDK_GRAPHICS_MENU_ROW_ANTI_ALIASING] = aa_text;
            row_values[SDK_GRAPHICS_MENU_ROW_SMOOTH_LIGHTING] = on_off_text(g_rs.pause_menu.graphics_smooth_lighting);
            row_values[SDK_GRAPHICS_MENU_ROW_SHADOW_QUALITY] = shadow_text;
            row_values[SDK_GRAPHICS_MENU_ROW_WATER_QUALITY] = water_text;
            row_values[SDK_GRAPHICS_MENU_ROW_RENDER_DISTANCE] = render_distance_text;
            row_values[SDK_GRAPHICS_MENU_ROW_ANISOTROPIC_SAMPLING] = anisotropy_text;
            row_values[SDK_GRAPHICS_MENU_ROW_FAR_TERRAIN_LOD] = far_lod_text;
            row_values[SDK_GRAPHICS_MENU_ROW_EXPERIMENTAL_FAR_MESHES] = experimental_far_text;
            row_values[SDK_GRAPHICS_MENU_ROW_BLACK_SUPERCHUNK_WALLS] = wall_color_text;
            row_values[SDK_GRAPHICS_MENU_ROW_SUPERCHUNK_LOAD_MODE] = superchunk_load_text;
            row_values[SDK_GRAPHICS_MENU_ROW_VSYNC] = vsync_text;
            row_values[SDK_GRAPHICS_MENU_ROW_FOG] = fog_text;

            vi += hud_text(&verts[vi], "GRAPHICS", px + 22.0f, py + 30.0f, 3.15f, 0xFFFFFFFF);
            vi += hud_text(&verts[vi], "BACKSPACE RETURNS", px + panel_w - 220.0f, py + 36.0f, 1.46f, 0xFFB8C0D0);

            for (int row = 0; row < SDK_GRAPHICS_MENU_ROW_COUNT; ++row) {
                uint32_t row_col = (graphics_row == row) ? 0xFF80522Du : 0xD0222630u;
                vi += hud_quad(&verts[vi], slider_x0, row_y + row * row_h,
                               slider_x1, row_y + row * row_h + 36.0f, row_col);
                vi += hud_text(&verts[vi], row_labels[row], slider_x0 + 10.0f, row_y + row * row_h + 8.0f, 1.56f, 0xFFFFFFFFu);
                vi += hud_text(&verts[vi], row_values[row],
                               slider_x1 - 10.0f - hud_text_width_estimate(row_values[row], 1.40f),
                               row_y + row * row_h + 9.0f, 1.40f, 0xFFFFFFFFu);
            }

            vi += hud_text(&verts[vi], "UP/DOWN SELECT   LEFT/RIGHT CHANGE", slider_x0, py + panel_h - 48.0f, 1.34f, 0xFFD0D8E0u);
            vi += hud_text(&verts[vi],
                           (graphics_row == SDK_GRAPHICS_MENU_ROW_RESOLUTION &&
                            g_rs.pause_menu.graphics_display_mode == 1)
                               ? "BORDERLESS USES THE DESKTOP RESOLUTION"
                               : (graphics_row == SDK_GRAPHICS_MENU_ROW_RESOLUTION &&
                                  g_rs.pause_menu.graphics_display_mode == 0)
                                      ? "WINDOWED SIZE MAY CLAMP TO FIT THE DESKTOP"
                                      : (graphics_row == SDK_GRAPHICS_MENU_ROW_RESOLUTION &&
                                         g_rs.pause_menu.graphics_display_mode == 2)
                                            ? "FULLSCREEN USES THE CLOSEST SUPPORTED OUTPUT MODE"
                               : "QUALITY PRESET IS A STARTING POINT, NOT A LOCK",
                           slider_x0, py + panel_h - 24.0f, 1.16f, 0xFFB8C0D0u);
        } else if (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_KEY_BINDINGS) {
            float list_x = px + 26.0f;
            float list_y = py + 104.0f;
            float list_w = panel_w - 52.0f;
            float row_h = 42.0f;

            vi += hud_text(&verts[vi], "KEY BINDINGS", px + 22.0f, py + 30.0f, 3.15f, 0xFFFFFFFFu);
            vi += hud_text(&verts[vi], "BACKSPACE RETURNS", px + panel_w - 220.0f, py + 36.0f, 1.46f, 0xFFB8C0D0u);

            for (int row = 0; row < g_rs.pause_menu.keybind_visible_count; ++row) {
                int absolute_row = g_rs.pause_menu.keybind_scroll + row;
                uint32_t row_col = (absolute_row == g_rs.pause_menu.keybind_selected) ? 0xFF80522Du : 0xD0222630u;
                vi += hud_quad(&verts[vi], list_x, list_y + row * row_h,
                               list_x + list_w, list_y + row * row_h + 32.0f, row_col);
                vi += hud_text(&verts[vi], g_rs.pause_menu.keybind_label[row],
                               list_x + 10.0f, list_y + row * row_h + 7.0f, 1.42f, 0xFFFFFFFFu);
                vi += hud_text(&verts[vi], g_rs.pause_menu.keybind_value[row],
                               list_x + list_w - 10.0f - hud_text_width_estimate(g_rs.pause_menu.keybind_value[row], 1.30f),
                               list_y + row * row_h + 8.0f, 1.30f, 0xFFFFFFFFu);
            }

            if (g_rs.pause_menu.keybind_capture_active) {
                vi += hud_text(&verts[vi], "PRESS A KEY OR MOUSE BUTTON", list_x, py + panel_h - 48.0f, 1.28f, 0xFFD0D8E0u);
                vi += hud_text(&verts[vi], "BACKSPACE CLEARS   R RESTORES DEFAULT   ESC CANCELS", list_x, py + panel_h - 28.0f, 1.12f, 0xFFB8C0D0u);
            } else {
                vi += hud_text(&verts[vi], "ENTER REBINDS   LEFT/RIGHT ADJUSTS VALUES", list_x, py + panel_h - 48.0f, 1.28f, 0xFFD0D8E0u);
                vi += hud_text(&verts[vi], "R RESTORES THE SELECTED BINDING", list_x, py + panel_h - 28.0f, 1.12f, 0xFFB8C0D0u);
            }
        } else if (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_CREATIVE) {
            float search_x = px + 24.0f;
            float search_y = py + 62.0f;
            float search_w = panel_w - 48.0f;
            float list_x = px + 24.0f;
            float list_y = py + 112.0f;
            float list_w = panel_w - 48.0f;
            float row_h = 34.0f;
            float shape_x = list_x;
            float shape_y = list_y + SDK_CREATIVE_MENU_VISIBLE_ROWS * row_h + 14.0f;
            float shape_w = list_w;
            float shape_h = 152.0f;
            int selected_visible_row = g_rs.pause_menu.creative_selected - g_rs.pause_menu.creative_scroll;
            int selected_entry_kind = SDK_CREATIVE_ENTRY_ITEM;
            int selected_entry_id = ITEM_NONE;
            bool selected_is_block = false;
            char total_text[32];
            char filter_text[32];
            char grant_text[32];

            sprintf_s(total_text, sizeof(total_text), "%d RESULTS", g_rs.pause_menu.creative_total);
            sprintf_s(filter_text, sizeof(filter_text), "FILTER %s",
                      creative_filter_name(g_rs.pause_menu.creative_filter));
            sprintf_s(grant_text, sizeof(grant_text), "%s",
                      (g_rs.pause_menu.creative_shape_width == 16 &&
                       g_rs.pause_menu.creative_shape_height == 16 &&
                       g_rs.pause_menu.creative_shape_depth == 16)
                          ? "FULL BLOCK"
                          : "SHAPED PAYLOAD");

            if (selected_visible_row >= 0 && selected_visible_row < g_rs.pause_menu.creative_visible_count) {
                selected_entry_kind = g_rs.pause_menu.creative_visible_entry_kind[selected_visible_row];
                selected_entry_id = g_rs.pause_menu.creative_visible_entry_id[selected_visible_row];
                selected_is_block = (selected_entry_kind == SDK_CREATIVE_ENTRY_BLOCK && selected_entry_id != BLOCK_AIR);
            }

            vi += hud_text(&verts[vi], "CREATIVE INVENTORY", px + 22.0f, py + 18.0f, 3.1f, 0xFFFFFFFF);
            vi += hud_text(&verts[vi], "ENTER OPENS SHAPE / GRANTS ITEM", px + panel_w - 324.0f, py + 24.0f, 1.46f, 0xFFB8C0D0);
            vi += hud_text(&verts[vi], "SEARCH", search_x, search_y - 18.0f, 1.9f, 0xFFD0D8E0);
            vi += hud_quad(&verts[vi], search_x, search_y, search_x + search_w, search_y + 28.0f, 0xD0222630);
            vi += hud_text(&verts[vi], g_rs.pause_menu.creative_search, search_x + 8.0f, search_y + 8.0f, 1.9f, 0xFFFFFFFF);
            vi += hud_text(&verts[vi], total_text, search_x, search_y + 38.0f, 1.68f, 0xFFB8C0D0);
            vi += hud_text(&verts[vi], filter_text,
                           search_x + search_w - hud_text_width_estimate(filter_text, 1.68f),
                           search_y + 38.0f, 1.68f, 0xFFB8C0D0);

            for (int i = 0; i < g_rs.pause_menu.creative_visible_count; ++i) {
                int entry_kind = g_rs.pause_menu.creative_visible_entry_kind[i];
                int entry_id = g_rs.pause_menu.creative_visible_entry_id[i];
                const char* raw_name = (entry_kind == SDK_CREATIVE_ENTRY_ITEM)
                    ? sdk_item_get_name((ItemType)entry_id)
                    : sdk_block_get_name((BlockType)entry_id);
                uint32_t swatch = (entry_kind == SDK_CREATIVE_ENTRY_ITEM)
                    ? sdk_item_get_color((ItemType)entry_id)
                    : sdk_block_get_face_color((BlockType)entry_id, 3);
                char label[64];
                float row_y = list_y + i * row_h;
                bool row_selected = ((g_rs.pause_menu.creative_scroll + i) == g_rs.pause_menu.creative_selected);
                bool row_focused = row_selected && g_rs.pause_menu.creative_shape_focus == 0;
                uint32_t row_col = row_focused ? 0xFF2C4F78
                                  : row_selected ? 0xE0323C52
                                  : 0xD0222630;
                size_t len = 0;

                memset(label, 0, sizeof(label));
                if (raw_name) {
                    for (; raw_name[len] != '\0' && len + 1 < sizeof(label); ++len) {
                        char ch = raw_name[len];
                        if (ch == '_') ch = ' ';
                        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
                        label[len] = ch;
                    }
                }

                vi += hud_quad(&verts[vi], list_x, row_y, list_x + list_w, row_y + row_h - 4.0f, row_col);
                vi += hud_quad(&verts[vi], list_x + 6.0f, row_y + 5.0f, list_x + 28.0f, row_y + 23.0f, swatch);
                vi += hud_text(&verts[vi], label, list_x + 38.0f, row_y + 8.0f, 1.72f, 0xFFFFFFFF);
            }
            if (selected_is_block) {
                char material_label[64];
                char row_text[64];
                static const char* shape_row_names[3] = { "WIDTH", "HEIGHT", "DEPTH" };
                int shape_values[3] = {
                    g_rs.pause_menu.creative_shape_width,
                    g_rs.pause_menu.creative_shape_height,
                    g_rs.pause_menu.creative_shape_depth
                };

                memset(material_label, 0, sizeof(material_label));
                {
                    const char* raw_name = sdk_block_get_name((BlockType)selected_entry_id);
                    size_t len = 0;
                    if (raw_name) {
                        for (; raw_name[len] != '\0' && len + 1 < sizeof(material_label); ++len) {
                            char ch = raw_name[len];
                            if (ch == '_') ch = ' ';
                            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
                            material_label[len] = ch;
                        }
                    }
                }

                vi += hud_panel_frame(&verts[vi], shape_x, shape_y, shape_x + shape_w, shape_y + shape_h,
                                      0xC018202Au, 0xFF6E8CB2u, 0xFF38465Cu);
                vi += hud_text(&verts[vi], "CONSTRUCTION SHAPE", shape_x + 12.0f, shape_y + 10.0f, 1.72f, 0xFFFFFFFFu);
                vi += hud_text(&verts[vi], material_label, shape_x + shape_w - 12.0f - hud_text_width_estimate(material_label, 1.30f),
                               shape_y + 14.0f, 1.30f, 0xFFD0D8E0u);

                for (int row = 0; row < 3; ++row) {
                    float row_y = shape_y + 38.0f + row * 34.0f;
                    bool row_selected = (row == g_rs.pause_menu.creative_shape_row);
                    bool row_focused = row_selected && g_rs.pause_menu.creative_shape_focus == 1;
                    uint32_t fill = row_focused ? 0xFF375E8Eu
                                   : row_selected ? 0xD02E394Au
                                   : 0xA0222630u;

                    sprintf_s(row_text, sizeof(row_text), "%s   [-] %2d [+]",
                              shape_row_names[row], shape_values[row]);
                    vi += hud_quad(&verts[vi], shape_x + 10.0f, row_y, shape_x + shape_w - 10.0f, row_y + 28.0f, fill);
                    vi += hud_text(&verts[vi], row_text, shape_x + 18.0f, row_y + 6.0f, 1.44f, 0xFFFFFFFFu);
                }

                vi += hud_text(&verts[vi], grant_text, shape_x + 14.0f, shape_y + 142.0f, 1.20f, 0xFFD8E8FFu);
                vi += hud_text(&verts[vi], "R ROTATES SHAPED PLACEMENT", shape_x + shape_w - 18.0f -
                               hud_text_width_estimate("R ROTATES SHAPED PLACEMENT", 1.10f),
                               shape_y + 142.0f, 1.10f, 0xFFB8C0D0u);

                if (g_rs.pause_menu.creative_shape_focus == 1) {
                    vi += hud_text(&verts[vi], "UP/DOWN SELECTS ROW   LEFT/RIGHT CHANGES SIZE", px + 24.0f, py + panel_h - 48.0f, 1.30f, 0xFFD0D8E0u);
                    vi += hud_text(&verts[vi], "ENTER GRANTS   BACKSPACE RETURNS TO RESULTS", px + 24.0f, py + panel_h - 28.0f, 1.12f, 0xFFB8C0D0u);
                } else {
                    vi += hud_text(&verts[vi], "LEFT/RIGHT FILTER   ENTER OPENS SHAPE PANEL", px + 24.0f, py + panel_h - 48.0f, 1.30f, 0xFFD0D8E0u);
                    vi += hud_text(&verts[vi], "BACKSPACE SEARCH / RETURN   R ROTATES AFTER PLACEMENT", px + 24.0f, py + panel_h - 28.0f, 1.12f, 0xFFB8C0D0u);
                }
            } else {
                vi += hud_text(&verts[vi], "LEFT/RIGHT FILTER   ENTER GRANTS ITEM", px + 24.0f, py + panel_h - 48.0f, 1.30f, 0xFFD0D8E0u);
                vi += hud_text(&verts[vi], "BACKSPACE SEARCH / RETURN", px + 24.0f, py + panel_h - 28.0f, 1.12f, 0xFFB8C0D0u);
            }
        } else if (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_DEBUG_PROFILER) {
            ProfileFrameData last_frame;
            char line[256];
            char gfx_line[256];
            double live_frame_ms = 0.0;
            
            memset(&last_frame, 0, sizeof(last_frame));
            
            vi += hud_text(&verts[vi], "DEBUG PROFILER", px + 22.0f, py + 30.0f, 3.15f, 0xFFFFFFFF);
            
            if (g_profiler.enabled) {
                live_frame_ms = sdk_profiler_get_current_frame_age_ms(&g_profiler);
                vi += hud_text(&verts[vi], "STATUS: ENABLED", px + 24.0f, py + 100.0f, 2.0f, 0xFF50FF50);
                
                sprintf_s(line, sizeof(line), "Frames: %d", g_profiler.frame_number);
                vi += hud_text(&verts[vi], line, px + 24.0f, py + 140.0f, 1.5f, 0xFFE0E0E0);
                
                sdk_profiler_get_last_frame(&g_profiler, &last_frame);
                
                sprintf_s(line, sizeof(line), "Current Frame Age: %.2f ms", live_frame_ms);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 174.0f, 1.45f, 0xFFFFC870);

                sprintf_s(line, sizeof(line), "Last Total: %.2f ms", last_frame.frame_time_ms);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 202.0f, 1.45f, 0xFFFFFF50);

                sprintf_s(line, sizeof(line), "Last Accounted: %.2f ms", last_frame.accounted_time_ms);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 230.0f, 1.30f, 0xFFE0E0E0);

                sprintf_s(line, sizeof(line), "Last Unaccounted: %.2f ms", last_frame.unaccounted_time_ms);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 258.0f, 1.30f, 0xFFFFA0A0);
                
                sprintf_s(line, sizeof(line), "Render: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_RENDERING]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 280.0f, 1.22f, 0xFFE0E0E0);
                
                sprintf_s(line, sizeof(line), "Chunk Update: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_CHUNK_UPDATE]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 304.0f, 1.22f, 0xFFE0E0E0);
                
                sprintf_s(line, sizeof(line), "Chunk Stream: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_CHUNK_STREAMING]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 328.0f, 1.22f, 0xFFE0E0E0);
                
                sprintf_s(line, sizeof(line), "Chunk Adopt: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_CHUNK_ADOPTION]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 352.0f, 1.22f, 0xFFE0E0E0);
                
                sprintf_s(line, sizeof(line), "Chunk Mesh: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_CHUNK_MESHING]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 376.0f, 1.22f, 0xFFE0E0E0);

                sprintf_s(line, sizeof(line), "Physics: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_PHYSICS]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 400.0f, 1.22f, 0xFFE0E0E0);

                sprintf_s(line, sizeof(line), "Settlement Scan: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_SETTLEMENT_SCAN]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 424.0f, 1.22f, 0xFFE0E0E0);

                sprintf_s(line, sizeof(line), "Settlement Runtime: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_SETTLEMENT_RUNTIME]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 448.0f, 1.22f, 0xFFE0E0E0);

                sprintf_s(line, sizeof(line), "Entity Tick: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_ENTITY_UPDATE]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 472.0f, 1.22f, 0xFFE0E0E0);

                sprintf_s(line, sizeof(line), "Input: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_INPUT]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 496.0f, 1.22f, 0xFFE0E0E0);

                sprintf_s(line, sizeof(line), "Debug UI: %.2f ms", last_frame.zone_times_ms[PROF_ZONE_DEBUG_UI]);
                vi += hud_text(&verts[vi], line, px + 40.0f, py + 520.0f, 1.22f, 0xFFE0E0E0);

                sprintf_s(gfx_line, sizeof(gfx_line),
                          "GFX %d  DIST %d  LOD %d  XLOD %d  RS %d  VSYNC %d",
                          (int)g_rs.pause_menu.graphics_preset,
                          g_rs.pause_menu.graphics_render_distance_chunks,
                          g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks,
                          g_rs.pause_menu.graphics_experimental_far_mesh_distance_chunks,
                          g_rs.pause_menu.graphics_render_scale_percent,
                          g_rs.pause_menu.graphics_vsync ? 1 : 0);
                vi += hud_text(&verts[vi], gfx_line, px + 24.0f, py + panel_h - 58.0f, 1.18f, 0xFFB8C0D0);
                
                vi += hud_text(&verts[vi], "SELECT: DISABLE   BACKSPACE: RETURN", px + 24.0f, py + panel_h - 30.0f, 1.3f, 0xFFB8C0D0);
            } else {
                vi += hud_text(&verts[vi], "STATUS: DISABLED", px + 24.0f, py + 100.0f, 2.0f, 0xFFFF5050);
                vi += hud_text(&verts[vi], "Press SELECT to start profiling", px + 24.0f, py + 160.0f, 1.5f, 0xFFE0E0E0);
                vi += hud_text(&verts[vi], "SELECT: ENABLE   BACKSPACE: RETURN", px + 24.0f, py + panel_h - 30.0f, 1.3f, 0xFFB8C0D0);
            }
        } else if (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_SELECT_CHARACTER) {
            float list_x = px + 24.0f;
            float list_y = py + 168.0f;
            float list_w = panel_w - 48.0f;
            float row_h = 44.0f;
            char current_text[64];

            if (g_rs.pause_menu.character_current >= 0 &&
                g_rs.pause_menu.character_current >= g_rs.pause_menu.character_scroll &&
                g_rs.pause_menu.character_current <
                    g_rs.pause_menu.character_scroll + g_rs.pause_menu.character_count) {
                const char* current_name =
                    g_rs.pause_menu.character_name[g_rs.pause_menu.character_current - g_rs.pause_menu.character_scroll];
                sprintf_s(current_text, sizeof(current_text), "CURRENT %s", current_name);
            } else if (g_rs.pause_menu.character_current >= 0) {
                sprintf_s(current_text, sizeof(current_text), "CURRENT CHARACTER SELECTED");
            } else {
                sprintf_s(current_text, sizeof(current_text), "CURRENT NONE");
            }

            vi += hud_text(&verts[vi], "SELECT CHARACTER", px + 22.0f, py + 32.0f, 3.28f, 0xFFFFFFFF);
            vi += hud_text(&verts[vi], "ENTER APPLIES   BACKSPACE RETURNS", px + 24.0f, py + 74.0f, 1.55f, 0xFFB8C0D0);
            vi += hud_text(&verts[vi], current_text, list_x, py + 106.0f, 2.04f, 0xFFBEE0FFu);
            vi += hud_text(&verts[vi], "FRONT 11 DEPTH LAYERS USED FOR FIRST PERSON BODY", list_x, py + 138.0f, 1.58f, 0xFFB8C0D0);

            if (g_rs.pause_menu.character_count <= 0) {
                vi += hud_quad(&verts[vi], list_x, list_y, list_x + list_w, list_y + 44.0f, 0xD0222630);
                vi += hud_text(&verts[vi], "NO SAVED CHARACTERS", list_x + 16.0f, list_y + 13.0f, 2.0f, 0xFFFFFFFF);
            } else {
                for (int i = 0; i < g_rs.pause_menu.character_count; ++i) {
                    int absolute_index = g_rs.pause_menu.character_scroll + i;
                    float row_y = list_y + i * row_h;
                    uint32_t row_col = (absolute_index == g_rs.pause_menu.character_selected)
                        ? 0xFF2C4F78 : 0xD0222630;
                    uint32_t text_col = (absolute_index == g_rs.pause_menu.character_current)
                        ? 0xFFBEE0FFu : 0xFFFFFFFFu;
                    vi += hud_quad(&verts[vi], list_x, row_y, list_x + list_w, row_y + 34.0f, row_col);
                    vi += hud_text(&verts[vi], g_rs.pause_menu.character_name[i],
                                   list_x + 14.0f, row_y + 8.0f, 2.05f, text_col);
                    if (absolute_index == g_rs.pause_menu.character_current) {
                        vi += hud_text(&verts[vi], "ACTIVE",
                                       list_x + list_w - hud_text_width_estimate("ACTIVE", 1.72f) - 12.0f,
                                       row_y + 9.0f, 1.72f, 0xFFBEE0FFu);
                    }
                }
            }
        } else if (g_rs.pause_menu.view == SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER) {
            float list_x = px + 24.0f;
            float list_y = py + 108.0f;
            float list_w = panel_w - 48.0f;
            float row_h = 32.0f;
            int visible_rows = (int)((panel_h - 140.0f) / row_h);
            int chunk_count = 0;
            
            vi += hud_text(&verts[vi], "CHUNK MANAGER", px + 22.0f, py + 32.0f, 3.28f, 0xFFFFFFFF);
            vi += hud_text(&verts[vi], "BACKSPACE RETURNS", px + panel_w - 220.0f, py + 36.0f, 1.48f, 0xFFB8C0D0);
            
            /* Count resident chunks */
            if (g_chunk_mgr) {
                chunk_count = (int)g_chunk_mgr->resident_count;
            }
            
            char count_text[64];
            sprintf_s(count_text, sizeof(count_text), "LOADED CHUNKS: %d / %d", chunk_count, SDK_CHUNK_MANAGER_MAX_RESIDENT);
            vi += hud_text(&verts[vi], count_text, list_x, py + 74.0f, 1.55f, 0xFFBEE0FFu);
            
            if (!g_chunk_mgr || chunk_count == 0) {
                vi += hud_quad(&verts[vi], list_x, list_y, list_x + list_w, list_y + 44.0f, 0xD0222630);
                vi += hud_text(&verts[vi], "NO CHUNKS LOADED", list_x + 16.0f, list_y + 13.0f, 2.0f, 0xFFFFFFFF);
            } else {
                int display_count = chunk_count - g_rs.pause_menu.chunk_manager_scroll;
                if (display_count > visible_rows) display_count = visible_rows;
                
                for (int i = 0; i < display_count; ++i) {
                    int slot_index = g_rs.pause_menu.chunk_manager_scroll + i;
                    float row_y = list_y + i * row_h;
                    uint32_t row_col = (slot_index == g_rs.pause_menu.chunk_manager_selected)
                        ? 0xFF2C4F78 : 0xD0222630;
                    
                    if (slot_index < SDK_CHUNK_MANAGER_MAX_RESIDENT && g_chunk_mgr->slots[slot_index].occupied) {
                        const SdkChunk* chunk = &g_chunk_mgr->slots[slot_index].chunk;
                        char chunk_text[128];
                        SdkSuperchunkCell chunk_cell;
                        int scx;
                        int scz;
                        int local_x;
                        int local_z;
                        SdkChunkResidencyRole role = sdk_chunk_manager_get_chunk_role(g_chunk_mgr, chunk->cx, chunk->cz);
                        const char* role_name = (role == SDK_CHUNK_ROLE_PRIMARY) ? "PRI" :
                                               (role == SDK_CHUNK_ROLE_WALL_SUPPORT) ? "WALL" :
                                               (role == SDK_CHUNK_ROLE_FRONTIER) ? "FRN" : "OTH";

                        sdk_superchunk_cell_from_chunk(chunk->cx, chunk->cz, &chunk_cell);
                        scx = chunk_cell.scx;
                        scz = chunk_cell.scz;
                        sdk_superchunk_chunk_local_interior_coords(chunk->cx, chunk->cz, &local_x, &local_z);
                        
                        sprintf_s(chunk_text, sizeof(chunk_text), "G(%d,%d) S(%d,%d) L(%d,%d) %s",
                                 chunk->cx, chunk->cz, scx, scz, local_x, local_z, role_name);
                        
                        vi += hud_quad(&verts[vi], list_x, row_y, list_x + list_w, row_y + 28.0f, row_col);
                        vi += hud_text(&verts[vi], chunk_text, list_x + 10.0f, row_y + 5.0f, 1.42f, 0xFFFFFFFF);
                    }
                }
            }
            
            vi += hud_text(&verts[vi], "UP/DOWN SCROLL   BACKSPACE RETURN", list_x, py + panel_h - 30.0f, 1.3f, 0xFFB8C0D0);
        }
    }

    /* --- Skills menu overlay --- */
    if (g_rs.skills.open && vi + 12000 < HUD_MAX_VERTS) {
        static const char* tab_names[SDK_SKILL_TAB_COUNT] = {
            "COMBAT", "SURVIVAL", "PROFESSIONS"
        };
        static const char* combat_names[SDK_COMBAT_SKILL_COUNT] = {
            "MARKSMANSHIP", "MELEE", "EXPLOSIVES", "FIELDCRAFT"
        };
        static const char* survival_names[SDK_SURVIVAL_SKILL_COUNT] = {
            "ENDURANCE", "FORAGING", "WEATHERCRAFT", "NAVIGATION"
        };
        static const char* profession_names[SDK_PROFESSION_COUNT] = {
            "SKINNING", "LEATHERWORKING", "MINING", "SMELTING",
            "ENGINEERING", "BLACKSMITHING", "HERBALISM", "MEDICINE",
            "COOKING", "FISHING", "BUILDING", "HUNTING"
        };
        static const char* profession_node_names[SDK_PROFESSION_COUNT][SDK_PROFESSION_NODE_COUNT] = {
            { "CLEAN CUTS", "HIDE CARE", "TROPHIES" },
            { "TANNING", "PATTERNING", "REINFORCEMENT" },
            { "PROSPECTING", "EXTRACTION", "DEEP VEINS" },
            { "FURNACE", "FLUX", "ALLOYING" },
            { "MECHANISMS", "REPAIRS", "DEMOLITIONS" },
            { "FORGING", "TEMPERING", "TOOLSMITHING" },
            { "IDENTIFY", "CULTIVATE", "DISTILLING" },
            { "TRIAGE", "APOTHECARY", "SURGERY" },
            { "BUTCHERY", "PRESERVE", "NUTRITION" },
            { "CASTING", "NETTING", "RIVER READ" },
            { "EARTHWORKS", "CARPENTRY", "MASONRY" },
            { "TRACKING", "ARCHERY", "DRESSING" }
        };
        float panel_w = W * 0.78f;
        float panel_h = H * 0.74f;
        float px = (W - panel_w) * 0.5f;
        float py = (H - panel_h) * 0.5f;
        float tab_y = py + 18.0f;
        float content_x = px + 18.0f;
        float content_y = py + 68.0f;
        float content_w = panel_w - 36.0f;
        float content_h = panel_h - 86.0f;
        float tab_w = (panel_w - 36.0f) / 3.0f;

        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + panel_h, 0xE0121620);
        vi += hud_quad(&verts[vi], px, py, px + panel_w, py + 3.0f, 0xFF8090A0);
        vi += hud_quad(&verts[vi], px, py + panel_h - 3.0f, px + panel_w, py + panel_h, 0xFF8090A0);
        vi += hud_quad(&verts[vi], px, py, px + 3.0f, py + panel_h, 0xFF8090A0);
        vi += hud_quad(&verts[vi], px + panel_w - 3.0f, py, px + panel_w, py + panel_h, 0xFF8090A0);
        vi += hud_text(&verts[vi], "SKILLS", px + 18.0f, py + 12.0f, 3.0f, 0xFFFFFFFF);

        vi += hud_text(&verts[vi], "POINTS", px + panel_w - 150.0f, py + 14.0f, 2.0f, 0xFFD0D8E0);
        vi += hud_number(&verts[vi], g_rs.skills.unspent_skill_points, px + panel_w - 54.0f, py + 14.0f, 2.0f, 0xFFFFFFFF);

        for (int tab = 0; tab < SDK_SKILL_TAB_COUNT; ++tab) {
            float tx0 = px + 12.0f + tab_w * (float)tab;
            uint32_t tab_col = (tab == g_rs.skills.selected_tab) ? 0xFF3850A8 : 0xFF30363E;
            vi += hud_quad(&verts[vi], tx0, tab_y, tx0 + tab_w - 6.0f, tab_y + 28.0f, tab_col);
            vi += hud_text(&verts[vi], tab_names[tab], tx0 + 10.0f, tab_y + 8.0f, 2.0f, 0xFFFFFFFF);
        }

        vi += hud_quad(&verts[vi], content_x, content_y, content_x + content_w, content_y + content_h, 0xB0101218);

        if (g_rs.skills.selected_tab == SDK_SKILL_TAB_COMBAT || g_rs.skills.selected_tab == SDK_SKILL_TAB_SURVIVAL) {
            const char** names = (g_rs.skills.selected_tab == SDK_SKILL_TAB_COMBAT) ? combat_names : survival_names;
            const int* ranks = (g_rs.skills.selected_tab == SDK_SKILL_TAB_COMBAT) ? g_rs.skills.combat_ranks : g_rs.skills.survival_ranks;
            int count = (g_rs.skills.selected_tab == SDK_SKILL_TAB_COMBAT) ? SDK_COMBAT_SKILL_COUNT : SDK_SURVIVAL_SKILL_COUNT;

            for (int row = 0; row < count; ++row) {
                float row_y = content_y + 18.0f + row * 56.0f;
                uint32_t row_col = (row == g_rs.skills.selected_row) ? 0xFF244060 : 0xD0202630;
                vi += hud_quad(&verts[vi], content_x + 12.0f, row_y, content_x + content_w - 12.0f, row_y + 42.0f, row_col);
                vi += hud_text(&verts[vi], names[row], content_x + 22.0f, row_y + 10.0f, 2.0f, 0xFFFFFFFF);

                for (int rank = 0; rank < 5; ++rank) {
                    float rx0 = content_x + content_w - 150.0f + rank * 24.0f;
                    uint32_t rank_col = (rank < ranks[row]) ? 0xFF20C060 : 0xFF404850;
                    vi += hud_quad(&verts[vi], rx0, row_y + 11.0f, rx0 + 18.0f, row_y + 29.0f, rank_col);
                }
            }
        } else {
            int prof = g_rs.skills.selected_profession;
            float sidebar_w = content_w * 0.38f;
            float detail_x = content_x + sidebar_w + 16.0f;
            float detail_w = content_w - sidebar_w - 28.0f;

            for (int p = 0; p < SDK_PROFESSION_COUNT; ++p) {
                int col = p / 6;
                int row = p % 6;
                float item_x = content_x + 12.0f + col * (sidebar_w * 0.5f + 8.0f);
                float item_w = sidebar_w * 0.5f - 12.0f;
                float item_y = content_y + 12.0f + row * 42.0f;
                uint32_t item_col = (p == prof) ? 0xFF245E58 : 0xD0202630;
                vi += hud_quad(&verts[vi], item_x, item_y, item_x + item_w, item_y + 30.0f, item_col);
                vi += hud_text(&verts[vi], profession_names[p], item_x + 6.0f, item_y + 9.0f, 1.6f, 0xFFFFFFFF);
            }

            vi += hud_text(&verts[vi], profession_names[prof], detail_x, content_y + 12.0f, 2.6f, 0xFFFFFFFF);
            vi += hud_text(&verts[vi], "LEVEL", detail_x, content_y + 44.0f, 1.8f, 0xFFD0D8E0);
            vi += hud_number(&verts[vi], g_rs.skills.profession_levels[prof], detail_x + 48.0f, content_y + 44.0f, 1.8f, 0xFFFFFFFF);
            vi += hud_text(&verts[vi], "POINTS", detail_x + 96.0f, content_y + 44.0f, 1.8f, 0xFFD0D8E0);
            vi += hud_number(&verts[vi], g_rs.skills.profession_points[prof], detail_x + 152.0f, content_y + 44.0f, 1.8f, 0xFFFFFFFF);

            {
                float prof_bar_x = detail_x;
                float prof_bar_y = content_y + 68.0f;
                float prof_bar_w = detail_w - 12.0f;
                float prof_fill = 0.0f;
                if (g_rs.skills.profession_xp_to_next[prof] > 0) {
                    prof_fill = (float)g_rs.skills.profession_xp[prof] / (float)g_rs.skills.profession_xp_to_next[prof];
                    if (prof_fill < 0.0f) prof_fill = 0.0f;
                    if (prof_fill > 1.0f) prof_fill = 1.0f;
                }
                vi += hud_quad(&verts[vi], prof_bar_x, prof_bar_y, prof_bar_x + prof_bar_w, prof_bar_y + 8.0f, 0xD020242C);
                if (prof_fill > 0.001f) {
                    vi += hud_quad(&verts[vi], prof_bar_x + 1.0f, prof_bar_y + 1.0f,
                                   prof_bar_x + 1.0f + (prof_bar_w - 2.0f) * prof_fill, prof_bar_y + 7.0f, 0xFF40A0FF);
                }
                vi += hud_text(&verts[vi], "PROF XP", prof_bar_x, prof_bar_y - 12.0f, 1.5f, 0xFFD0D8E0);
            }

            for (int node = 0; node < SDK_PROFESSION_NODE_COUNT; ++node) {
                float node_y = content_y + 100.0f + node * 62.0f;
                uint32_t node_col = (node == g_rs.skills.selected_row) ? 0xFF244060 : 0xD0202630;
                vi += hud_quad(&verts[vi], detail_x, node_y, detail_x + detail_w - 12.0f, node_y + 46.0f, node_col);
                vi += hud_text(&verts[vi], profession_node_names[prof][node], detail_x + 10.0f, node_y + 12.0f, 1.8f, 0xFFFFFFFF);
                for (int rank = 0; rank < 5; ++rank) {
                    float rx0 = detail_x + detail_w - 136.0f + rank * 22.0f;
                    uint32_t rank_col = (rank < g_rs.skills.profession_ranks[prof][node]) ? 0xFF20C060 : 0xFF404850;
                    vi += hud_quad(&verts[vi], rx0, node_y + 13.0f, rx0 + 16.0f, node_y + 29.0f, rank_col);
                }
            }
        }
    }

    /* Crosshair (small cross in center of screen) */
    if (!g_rs.player_dead && !g_rs.skills.open && !g_rs.crafting.open &&
        !g_rs.station.open && !g_rs.pause_menu.open && !g_rs.command_line.open &&
        !g_rs.map_ui.focused) {
        float cx = W * 0.5f, cy = H * 0.5f;
        vi += hud_quad(&verts[vi], cx - 8.0f, cy - 1.0f, cx + 8.0f, cy + 1.0f, 0xA0FFFFFF);
        vi += hud_quad(&verts[vi], cx - 1.0f, cy - 8.0f, cx + 1.0f, cy + 8.0f, 0xA0FFFFFF);
    }

    /* Command line overlay */
    if (g_rs.command_line.open && vi + 1200 < HUD_MAX_VERTS) {
        float box_w = W * 0.72f;
        float box_h = 34.0f;
        float box_x = (W - box_w) * 0.5f;
        float box_y = H - 92.0f;
        vi += hud_quad(&verts[vi], box_x, box_y, box_x + box_w, box_y + box_h, 0xD0101218);
        vi += hud_quad(&verts[vi], box_x, box_y, box_x + box_w, box_y + 2.0f, 0xFF9098A8);
        vi += hud_quad(&verts[vi], box_x, box_y + box_h - 2.0f, box_x + box_w, box_y + box_h, 0xFF9098A8);
        vi += hud_quad(&verts[vi], box_x, box_y, box_x + 2.0f, box_y + box_h, 0xFF9098A8);
        vi += hud_quad(&verts[vi], box_x + box_w - 2.0f, box_y, box_x + box_w, box_y + box_h, 0xFF9098A8);
        vi += hud_text(&verts[vi], g_rs.command_line.text, box_x + 10.0f, box_y + 10.0f, 1.8f, 0xFFFFFFFF);
    }

    if (g_rs.fluid_debug.open && vi + 3600 < HUD_MAX_VERTS) {
        float px = 18.0f;
        float py = 18.0f;
        float pw = W * 0.34f;
        float ph = 292.0f;
        const char* mode = "IDLE";
        char cols_text[48];
        char perf_text[48];
        char queue_text[48];
        char frame_text[48];
        char draw_text[48];
        char stream_text[40];
        char residency_text[48];
        char superchunk_text[48];

        switch (g_rs.fluid_debug.mechanism) {
            case SDK_RENDERER_FLUID_DEBUG_BULK_RESERVOIR: mode = "BULK"; break;
            case SDK_RENDERER_FLUID_DEBUG_LOCAL_WAKE:     mode = "LOCAL"; break;
            case SDK_RENDERER_FLUID_DEBUG_IDLE:
            default:                                  mode = "IDLE"; break;
        }

        sprintf_s(cols_text, sizeof(cols_text), "RES %d TOT %d", g_rs.fluid_debug.reservoir_columns, g_rs.fluid_debug.total_columns);
        sprintf_s(perf_text, sizeof(perf_text), "THR %d MS %d VOL %d", g_rs.fluid_debug.worker_count, g_rs.fluid_debug.solve_ms, g_rs.fluid_debug.total_volume);
        sprintf_s(queue_text, sizeof(queue_text), "STEP %d DIRTY %d ACT %d", g_rs.fluid_debug.tick_processed, g_rs.fluid_debug.dirty_cells, g_rs.fluid_debug.active_chunks);
        sprintf_s(frame_text, sizeof(frame_text), "FRAME %.1fMS  REND %.1fMS", g_rs.frame_ms, g_rs.render_ms);
        sprintf_s(draw_text, sizeof(draw_text), "VIS %u  DRW %u  VTX %u",
            g_rs.perf_visible_chunks,
            g_rs.perf_drawn_chunks,
            g_rs.perf_drawn_verts);
        sprintf_s(stream_text, sizeof(stream_text), "JOBS %d  RESULTS %d",
            g_rs.fluid_debug.stream_jobs,
            g_rs.fluid_debug.stream_results);
        sprintf_s(superchunk_text, sizeof(superchunk_text), "P %d,%d  D %d,%d  %s",
            g_rs.fluid_debug.primary_scx,
            g_rs.fluid_debug.primary_scz,
            g_rs.fluid_debug.desired_scx,
            g_rs.fluid_debug.desired_scz,
            g_rs.fluid_debug.transition_active ? "LOAD" : "STABLE");
        sprintf_s(residency_text, sizeof(residency_text), "P %d  F %d  T %d  E %d",
            g_rs.fluid_debug.resident_primary,
            g_rs.fluid_debug.resident_frontier,
            g_rs.fluid_debug.resident_transition,
            g_rs.fluid_debug.resident_evict);

        if (pw < 460.0f) pw = 460.0f;
        if (pw > 560.0f) pw = 560.0f;

        vi += hud_panel_frame(&verts[vi], px, py, px + pw, py + ph, 0xD0121720u, 0xFF4DA3FFu, 0xFF31485Eu);
        vi += hud_text(&verts[vi], "FLUID DEBUG", px + 16.0f, py + 12.0f, 2.0f, 0xFFFFFFFFu);
        vi += hud_text(&verts[vi], "F7", px + pw - 36.0f, py + 14.0f, 1.3f, 0xFFD9E8F8u);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 46.0f, px + pw - 14.0f, py + 72.0f,
                           "MODE", mode, 0xA0182230u, 0xFFD0D8E0u, 0xFFFFFFFFu, 1.34f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 78.0f, px + pw - 14.0f, py + 104.0f,
                           "REASON", g_rs.fluid_debug.reason, 0x80161E2Au, 0xFFD0D8E0u, 0xFFFFFFFFu, 1.20f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 110.0f, px + pw - 14.0f, py + 136.0f,
                           "RESERVOIRS", cols_text, 0x80161E2Au, 0xFFD0D8E0u, 0xFFFFFFFFu, 1.22f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 142.0f, px + pw - 14.0f, py + 168.0f,
                           "SOLVER", perf_text, 0x80161E2Au, 0xFFD0D8E0u, 0xFFFFFFFFu, 1.22f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 174.0f, px + pw - 14.0f, py + 200.0f,
                           "ACTIVITY", queue_text, 0x80161E2Au, 0xFFD0D8E0u, 0xFFFFFFFFu, 1.22f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 206.0f, px + pw - 14.0f, py + 232.0f,
                           "FRAME", frame_text, 0x80161E2Au, 0xFFD0D8E0u, 0xFFFFFFFFu, 1.22f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 238.0f, px + pw - 14.0f, py + 264.0f,
                           "DRAW", draw_text, 0x80161E2Au, 0xFFD0D8E0u, 0xFFFFFFFFu, 1.15f);
        vi += hud_text(&verts[vi], stream_text, px + 22.0f, py + 270.0f, 1.14f, 0xFFD0D8E0u);
        vi += hud_text(&verts[vi], superchunk_text, px + 176.0f, py + 270.0f, 1.14f, 0xFFD0D8E0u);
        vi += hud_text(&verts[vi], residency_text, px + pw - 16.0f - hud_text_width_estimate(residency_text, 1.14f),
                       py + 270.0f, 1.14f, 0xFFD0D8E0u);
    }

    if (g_rs.perf_debug.open && vi + 3600 < HUD_MAX_VERTS) {
        float px;
        float py = 18.0f;
        float pw = W * 0.34f;
        float ph = 348.0f;
        float right_inset = (g_rs.map_ui.open && !g_rs.map_ui.focused) ? 212.0f : 18.0f;
        char mem_text[64];
        char cpu_text[48];
        char frame_text[48];
        char draw_text[48];
        char sim_text[48];
        char stream_text[40];
        char mesh_text[48];
        char settlement_text[64];
        char startup_text[96];
        char scan_text[48];
        char residency_text[40];
        char superchunk_text[48];

        sprintf_s(mem_text, sizeof(mem_text), "WS %dMB  PRIV %dMB  PEAK %dMB",
            g_rs.perf_debug.working_set_mb,
            g_rs.perf_debug.private_mb,
            g_rs.perf_debug.peak_working_set_mb);
        sprintf_s(cpu_text, sizeof(cpu_text), "CPU %.1f%%  SYS RAM %d%%",
            g_rs.perf_debug.cpu_percent,
            g_rs.perf_debug.system_memory_load_pct);
        sprintf_s(frame_text, sizeof(frame_text), "FRAME %.1fMS  REND %.1fMS",
            g_rs.frame_ms,
            g_rs.render_ms);
        sprintf_s(draw_text, sizeof(draw_text), "VIS %u  REN %u  VTX %u",
            g_rs.perf_visible_chunks,
            g_rs.perf_renderable_chunks,
            g_rs.perf_drawn_verts);
        sprintf_s(sim_text, sizeof(sim_text), "STEP %d  DIRTY %d  ACTIVE %d",
            g_rs.perf_debug.sim_step_cells,
            g_rs.perf_debug.sim_dirty_cells,
            g_rs.perf_debug.sim_active_chunks);
        sprintf_s(stream_text, sizeof(stream_text), "JOBS %d  RESULTS %d",
            g_rs.perf_debug.stream_jobs,
            g_rs.perf_debug.stream_results);
        sprintf_s(mesh_text, sizeof(mesh_text), "DIRTY %d  FAR %d  STALL %d",
            g_rs.perf_debug.dirty_chunks,
            g_rs.perf_debug.far_dirty_chunks,
            g_rs.perf_debug.stalled_dirty_chunks);
        sprintf_s(settlement_text, sizeof(settlement_text), "LIVE %d  RES %d  INIT %d  MUT %d",
            g_rs.perf_debug.active_settlements,
            g_rs.perf_debug.active_residents,
            g_rs.perf_debug.settlement_initialized,
            g_rs.perf_debug.settlement_block_mutations);
        sprintf_s(scan_text, sizeof(scan_text), "SCAN %d  %dMS/%dMS",
            g_rs.perf_debug.settlement_scanned_chunks,
            750,
            100);
        sprintf_s(residency_text, sizeof(residency_text), "LOAD %d  DES %d",
            g_rs.perf_debug.resident_chunks,
            g_rs.perf_debug.desired_chunks);
        sprintf_s(superchunk_text, sizeof(superchunk_text), "P %d,%d  D %d,%d  %s",
            g_rs.perf_debug.primary_scx,
            g_rs.perf_debug.primary_scz,
            g_rs.perf_debug.desired_scx,
            g_rs.perf_debug.desired_scz,
            g_rs.perf_debug.transition_active ? "LOAD" : "STABLE");
        if (g_rs.perf_debug.startup_safe_active) {
            sprintf_s(startup_text, sizeof(startup_text),
                "PRI %d/%d/%d  CPU %d  UP %d  ST %d  FAR %d",
                g_rs.perf_debug.startup_primary_desired,
                g_rs.perf_debug.startup_primary_resident,
                g_rs.perf_debug.startup_primary_ready,
                g_rs.perf_debug.startup_no_cpu_mesh,
                g_rs.perf_debug.startup_upload_pending,
                g_rs.perf_debug.startup_gpu_stale,
                g_rs.perf_debug.startup_far_only);
        } else {
            strcpy_s(startup_text, sizeof(startup_text), settlement_text);
        }

        if (pw < 460.0f) pw = 460.0f;
        if (pw > 560.0f) pw = 560.0f;
        px = W - pw - right_inset;
        if (px < 18.0f) px = 18.0f;

        vi += hud_panel_frame(&verts[vi], px, py, px + pw, py + ph, 0xD0191411u, 0xFFFFA040u, 0xFF6D5338u);
        vi += hud_text(&verts[vi], "PERF DEBUG", px + 16.0f, py + 12.0f, 2.0f, 0xFFFFFFFFu);
        vi += hud_text(&verts[vi], "F8", px + pw - 36.0f, py + 14.0f, 1.3f, 0xFFFFDEBAu);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 46.0f, px + pw - 14.0f, py + 72.0f,
                           "MEMORY", mem_text, 0xA0241B18u, 0xFFE6D8CCu, 0xFFFFFFFFu, 1.20f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 78.0f, px + pw - 14.0f, py + 104.0f,
                           "CPU", cpu_text, 0x80211A18u, 0xFFE6D8CCu, 0xFFFFFFFFu, 1.20f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 110.0f, px + pw - 14.0f, py + 136.0f,
                           "FRAME", frame_text, 0x80211A18u, 0xFFE6D8CCu, 0xFFFFFFFFu, 1.20f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 142.0f, px + pw - 14.0f, py + 168.0f,
                           "DRAW", draw_text, 0x80211A18u, 0xFFE6D8CCu, 0xFFFFFFFFu, 1.20f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 174.0f, px + pw - 14.0f, py + 200.0f,
                           "SIM", sim_text, 0x80211A18u, 0xFFE6D8CCu, 0xFFFFFFFFu, 1.20f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 206.0f, px + pw - 14.0f, py + 232.0f,
                           "STREAM", stream_text, 0x80211A18u, 0xFFE6D8CCu, 0xFFFFFFFFu, 1.18f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 238.0f, px + pw - 14.0f, py + 264.0f,
                           "MESH", mesh_text, 0x80211A18u, 0xFFE6D8CCu, 0xFFFFFFFFu, 1.18f);
        vi += hud_stat_row(&verts[vi], px + 14.0f, py + 270.0f, px + pw - 14.0f, py + 296.0f,
                           g_rs.perf_debug.startup_safe_active ? "STARTUP" : "SETTLE",
                           startup_text, 0x80211A18u, 0xFFE6D8CCu, 0xFFFFFFFFu, 1.14f);
        vi += hud_text(&verts[vi], scan_text, px + 22.0f, py + 302.0f, 1.12f, 0xFFE6D8CCu);
        vi += hud_text(&verts[vi], residency_text, px + 22.0f, py + 324.0f, 1.14f, 0xFFE6D8CCu);
        vi += hud_text(&verts[vi], superchunk_text,
                       px + pw - 16.0f - hud_text_width_estimate(superchunk_text, 1.14f),
                       py + 324.0f, 1.14f, 0xFFE6D8CCu);
    }

    if (g_rs.settlement_debug.open && vi + 5200 < HUD_MAX_VERTS) {
        float px = 18.0f;
        float py = (g_rs.fluid_debug.open ? 328.0f : 18.0f);
        float pw = W - 36.0f;
        float ph = 420.0f;

        if (pw > 760.0f) pw = 760.0f;
        if (pw < 520.0f) pw = 520.0f;

        vi += hud_panel_frame(&verts[vi], px, py, px + pw, py + ph, 0xD0111512u, 0xFF6ED0A8u, 0xFF355948u);
        vi += hud_text(&verts[vi], "SETTLEMENT DEBUG", px + 16.0f, py + 12.0f, 2.0f, 0xFFFFFFFFu);
        vi += hud_text(&verts[vi], "F6", px + pw - 36.0f, py + 14.0f, 1.3f, 0xFFD8FFF2u);
        vi += hud_detail_block(&verts[vi], px + 14.0f, py + 48.0f, px + pw - 14.0f, py + 84.0f,
                               "SETTLEMENT", g_rs.settlement_debug.settlement,
                               0xA014211Cu, 0xFFD4E6DEu, 0xFFFFFFFFu, 1.02f, 1.16f);
        vi += hud_detail_block(&verts[vi], px + 14.0f, py + 90.0f, px + pw - 14.0f, py + 126.0f,
                               "LOCATION", g_rs.settlement_debug.location,
                               0x80131D18u, 0xFFD4E6DEu, 0xFFFFFFFFu, 1.02f, 1.16f);
        vi += hud_detail_block(&verts[vi], px + 14.0f, py + 132.0f, px + pw - 14.0f, py + 168.0f,
                               "ZONE", g_rs.settlement_debug.zone,
                               0x80131D18u, 0xFFD4E6DEu, 0xFFFFFFFFu, 1.02f, 1.14f);
        vi += hud_detail_block(&verts[vi], px + 14.0f, py + 174.0f, px + pw - 14.0f, py + 210.0f,
                               "BUILDING", g_rs.settlement_debug.building,
                               0x80131D18u, 0xFFD4E6DEu, 0xFFFFFFFFu, 1.02f, 1.12f);
        vi += hud_detail_block(&verts[vi], px + 14.0f, py + 216.0f, px + pw - 14.0f, py + 252.0f,
                               "METRICS", g_rs.settlement_debug.metrics,
                               0x80131D18u, 0xFFD4E6DEu, 0xFFFFFFFFu, 1.02f, 1.12f);
        vi += hud_detail_block(&verts[vi], px + 14.0f, py + 258.0f, px + pw - 14.0f, py + 294.0f,
                               "RUNTIME", g_rs.settlement_debug.runtime,
                               0x80131D18u, 0xFFD4E6DEu, 0xFFFFFFFFu, 1.02f, 1.08f);
        vi += hud_detail_block(&verts[vi], px + 14.0f, py + 300.0f, px + pw - 14.0f, py + 336.0f,
                               "RESIDENT", g_rs.settlement_debug.resident,
                               0x80131D18u, 0xFFD4E6DEu, 0xFFFFFFFFu, 1.02f, 1.08f);
        vi += hud_detail_block(&verts[vi], px + 14.0f, py + 342.0f, px + pw - 14.0f, py + 378.0f,
                               "ASSETS", g_rs.settlement_debug.assets,
                               0x80131D18u, 0xFFD4E6DEu, 0xFFFFFFFFu, 1.02f, 1.08f);
        vi += hud_text(&verts[vi], g_rs.settlement_debug.note, px + 18.0f, py + 388.0f, 1.10f, 0xFFD4E6DEu);
    }

    /* Death screen overlay */
    if (g_rs.player_dead && vi + 500 < HUD_MAX_VERTS) {
        /* Semi-transparent red overlay */
        vi += hud_quad(&verts[vi], 0.0f, 0.0f, W, H, 0x80000060);
        /* "YOU DIED" text — large pixel font, centered */
        float txt_scale = 5.0f;
        float txt_w = 8 * 5 * txt_scale; /* 8 chars × 5 cols × scale */
        float txt_x = (W - txt_w) * 0.5f;
        float txt_y = H * 0.35f;
        /* Spell out digits that look like letters using the pixel font:
           We'll just draw "DEAD" as rectangles for simplicity */
        uint32_t tc = 0xFFFFFFFF;
        /* D */
        float lx = txt_x;
        vi += hud_quad(&verts[vi], lx, txt_y, lx + txt_scale, txt_y + 25*txt_scale/5, tc);
        vi += hud_quad(&verts[vi], lx, txt_y, lx + 3*txt_scale, txt_y + txt_scale, tc);
        vi += hud_quad(&verts[vi], lx, txt_y + 4*txt_scale, lx + 3*txt_scale, txt_y + 5*txt_scale, tc);
        vi += hud_quad(&verts[vi], lx + 3*txt_scale, txt_y + txt_scale, lx + 4*txt_scale, txt_y + 4*txt_scale, tc);
        /* E */
        lx += 6 * txt_scale;
        vi += hud_quad(&verts[vi], lx, txt_y, lx + txt_scale, txt_y + 5*txt_scale, tc);
        vi += hud_quad(&verts[vi], lx, txt_y, lx + 4*txt_scale, txt_y + txt_scale, tc);
        vi += hud_quad(&verts[vi], lx, txt_y + 2*txt_scale, lx + 3*txt_scale, txt_y + 3*txt_scale, tc);
        vi += hud_quad(&verts[vi], lx, txt_y + 4*txt_scale, lx + 4*txt_scale, txt_y + 5*txt_scale, tc);
        /* A */
        lx += 6 * txt_scale;
        vi += hud_quad(&verts[vi], lx, txt_y + txt_scale, lx + txt_scale, txt_y + 5*txt_scale, tc);
        vi += hud_quad(&verts[vi], lx + 3*txt_scale, txt_y + txt_scale, lx + 4*txt_scale, txt_y + 5*txt_scale, tc);
        vi += hud_quad(&verts[vi], lx + txt_scale, txt_y, lx + 3*txt_scale, txt_y + txt_scale, tc);
        vi += hud_quad(&verts[vi], lx, txt_y + 2*txt_scale, lx + 4*txt_scale, txt_y + 3*txt_scale, tc);
        /* D */
        lx += 6 * txt_scale;
        vi += hud_quad(&verts[vi], lx, txt_y, lx + txt_scale, txt_y + 5*txt_scale, tc);
        vi += hud_quad(&verts[vi], lx, txt_y, lx + 3*txt_scale, txt_y + txt_scale, tc);
        vi += hud_quad(&verts[vi], lx, txt_y + 4*txt_scale, lx + 3*txt_scale, txt_y + 5*txt_scale, tc);
        vi += hud_quad(&verts[vi], lx + 3*txt_scale, txt_y + txt_scale, lx + 4*txt_scale, txt_y + 4*txt_scale, tc);
    }

hud_finalize:
    /* Upload ortho MVP */
    SdkMat4 ortho;
    memset(&ortho, 0, sizeof(ortho));
    /* Guard against division by zero if width/height not initialized */
    if (W < 1.0f) W = 1.0f;
    if (H < 1.0f) H = 1.0f;
    ortho.m[0][0] =  2.0f / W;
    ortho.m[1][1] = -2.0f / H;   /* Y-down */
    ortho.m[2][2] =  1.0f;
    ortho.m[3][3] =  1.0f;
    ortho.m[3][0] = -1.0f;
    ortho.m[3][1] =  1.0f;
    memcpy(renderer_current_hud_cb_mapped(), &ortho, sizeof(SdkMat4));

    if (vi == 0) { g_rs.hud_vert_count = 0; return; }

    size_t vb_size = vi * sizeof(BlockVertex);
    size_t needed_capacity = (size_t)HUD_MAX_VERTS * sizeof(BlockVertex);
    {
        UINT fi = renderer_frame_slot();
        ID3D12Resource* hud_vb = renderer_current_hud_buffer();
        UINT8* hud_vb_mapped = renderer_current_hud_buffer_mapped();
        size_t* hud_vb_capacity = renderer_current_hud_buffer_capacity_ptr();
        if (!hud_vb || !hud_vb_mapped || *hud_vb_capacity < needed_capacity) {
            D3D12_HEAP_PROPERTIES hp2 = {};
            D3D12_RESOURCE_DESC rd2 = {};
            HRESULT hr2;
            D3D12_RANGE rr = {0, 0};

            hp2.Type = D3D12_HEAP_TYPE_UPLOAD;
            rd2.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd2.Width = needed_capacity;
            rd2.Height = 1; rd2.DepthOrArraySize = 1; rd2.MipLevels = 1;
            rd2.SampleDesc.Count = 1;
            rd2.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            g_rs.hud_vb_mapped[fi] = nullptr;
            g_rs.hud_vb_capacity[fi] = 0u;
            g_rs.hud_vb[fi].Reset();

            hr2 = g_rs.device->CreateCommittedResource(
                &hp2, D3D12_HEAP_FLAG_NONE,
                &rd2, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&g_rs.hud_vb[fi]));
            if (FAILED(hr2)) {
                g_rs.hud_vert_count = 0;
                return;
            }

            HR_CHECK(g_rs.hud_vb[fi]->Map(0, &rr, (void**)&g_rs.hud_vb_mapped[fi]));
            g_rs.hud_vb_capacity[fi] = needed_capacity;
        }
    }

    memcpy(renderer_current_hud_buffer_mapped(), verts, vb_size);
    g_rs.hud_vert_count = (uint32_t)vi;
}

extern "C" void sdk_renderer_set_hotbar(const SdkHotbarSlot* slots, int num_slots, int selected)
{
    if (!slots || num_slots <= 0) return;
    int n = num_slots > 10 ? 10 : num_slots;
    bool changed = (selected != g_rs.hotbar_selected);
    for (int i = 0; i < n; i++) {
        if (g_rs.hotbar[i].item_type != slots[i].item_type ||
            g_rs.hotbar[i].direct_block_type != slots[i].direct_block_type ||
            g_rs.hotbar[i].count != slots[i].count) {
            changed = true;
        }
        g_rs.hotbar[i] = slots[i];
    }
    g_rs.hotbar_selected = selected;
    if (changed) g_rs.hotbar_dirty = true;
}

extern "C" void sdk_renderer_set_health(int health, int max_health, bool dead, bool invincible,
                                         int hunger, int max_hunger)
{
    bool changed = (health != g_rs.player_health || dead != g_rs.player_dead ||
                    invincible != g_rs.player_invincible || hunger != g_rs.player_hunger);
    g_rs.player_health = health;
    g_rs.player_max_health = max_health;
    g_rs.player_dead = dead;
    g_rs.player_invincible = invincible;
    g_rs.player_hunger = hunger;
    g_rs.player_max_hunger = max_hunger;
    if (changed) g_rs.hotbar_dirty = true;
}

extern "C" void sdk_renderer_set_lighting(float ambient, float sky_r, float sky_g, float sky_b)
{
    g_rs.ambient_level = ambient;
    g_rs.sky_r = sky_r;
    g_rs.sky_g = sky_g;
    g_rs.sky_b = sky_b;
    if (!g_rs.atmosphere_override) {
        synthesize_atmosphere_from_lighting();
    }
}

extern "C" void sdk_renderer_set_vsync(bool vsync)
{
    g_rs.vsync = vsync;
}

extern "C" void sdk_renderer_set_atmosphere(const SdkAtmosphereUI* ui)
{
    if (!ui) {
        g_rs.atmosphere_override = false;
        synthesize_atmosphere_from_lighting();
        return;
    }

    g_rs.atmosphere_override = ui->enabled;
    if (!ui->enabled) {
        synthesize_atmosphere_from_lighting();
        return;
    }

    g_rs.atmosphere_ambient = clamp01(ui->ambient);
    g_rs.atmosphere_sky_r = ui->sky_r;
    g_rs.atmosphere_sky_g = ui->sky_g;
    g_rs.atmosphere_sky_b = ui->sky_b;
    g_rs.atmosphere_sun_x = ui->sun_dir_x;
    g_rs.atmosphere_sun_y = ui->sun_dir_y;
    g_rs.atmosphere_sun_z = ui->sun_dir_z;
    g_rs.atmosphere_fog_r = ui->fog_r;
    g_rs.atmosphere_fog_g = ui->fog_g;
    g_rs.atmosphere_fog_b = ui->fog_b;
    g_rs.atmosphere_fog_density = ui->fog_density;
    g_rs.atmosphere_water_r = ui->water_r;
    g_rs.atmosphere_water_g = ui->water_g;
    g_rs.atmosphere_water_b = ui->water_b;
    g_rs.atmosphere_water_alpha = ui->water_alpha;
    g_rs.atmosphere_camera_submerged = ui->camera_submerged;
    g_rs.atmosphere_waterline_y = ui->waterline_y;
    g_rs.atmosphere_water_view_depth = ui->water_view_depth;
}

extern "C" void sdk_renderer_set_crafting(const SdkCraftingUI* ui)
{
    if (ui) {
        g_rs.crafting = *ui;
        g_rs.hotbar_dirty = true;
    }
}

extern "C" void sdk_renderer_set_station(const SdkStationUI* ui)
{
    if (ui) {
        g_rs.station = *ui;
        g_rs.hotbar_dirty = true;
    }
}

extern "C" void sdk_renderer_set_skills(const SdkSkillsUI* ui)
{
    if (ui) {
        g_rs.skills = *ui;
        g_rs.hotbar_dirty = true;
    }
}

extern "C" void sdk_renderer_set_pause_menu(const SdkPauseMenuUI* ui)
{
    if (ui) {
        g_rs.pause_menu = *ui;
        g_rs.pause_menu.view = hud_clampi(g_rs.pause_menu.view, SDK_PAUSE_MENU_VIEW_MAIN, SDK_PAUSE_MENU_VIEW_CHUNK_MANAGER);
        g_rs.pause_menu.selected = hud_clampi(g_rs.pause_menu.selected, 0, SDK_PAUSE_MENU_OPTION_COUNT - 1);
        g_rs.pause_menu.graphics_selected =
            hud_clampi(g_rs.pause_menu.graphics_selected, 0, SDK_GRAPHICS_MENU_ROW_COUNT - 1);
        g_rs.pause_menu.graphics_preset = hud_clampi(g_rs.pause_menu.graphics_preset, 0, 2);
        g_rs.pause_menu.graphics_resolution_preset =
            hud_clampi(g_rs.pause_menu.graphics_resolution_preset, 0, SDK_RESOLUTION_PRESET_COUNT - 1);
        g_rs.pause_menu.graphics_render_distance_chunks =
            hud_clamp_render_distance_chunks(g_rs.pause_menu.graphics_render_distance_chunks);
        g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks =
            hud_clamp_far_mesh_distance_chunks(g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks);
        if (g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks >
            g_rs.pause_menu.graphics_render_distance_chunks) {
            g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks =
                g_rs.pause_menu.graphics_render_distance_chunks;
        }
        g_rs.pause_menu.graphics_experimental_far_mesh_distance_chunks =
            hud_clamp_far_mesh_distance_chunks(g_rs.pause_menu.graphics_experimental_far_mesh_distance_chunks);
        if (g_rs.pause_menu.graphics_experimental_far_mesh_distance_chunks >
            g_rs.pause_menu.graphics_render_distance_chunks) {
            g_rs.pause_menu.graphics_experimental_far_mesh_distance_chunks =
                g_rs.pause_menu.graphics_render_distance_chunks;
        }
        if (g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks > 0 &&
            g_rs.pause_menu.graphics_experimental_far_mesh_distance_chunks >
                g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks) {
            g_rs.pause_menu.graphics_experimental_far_mesh_distance_chunks =
                g_rs.pause_menu.graphics_far_terrain_lod_distance_chunks;
        }
        g_rs.pause_menu.graphics_anisotropy_level =
            hud_clampi(g_rs.pause_menu.graphics_anisotropy_level, 0, 16);
        g_rs.pause_menu.graphics_display_mode =
            hud_clampi(g_rs.pause_menu.graphics_display_mode, 0, 2);
        g_rs.pause_menu.graphics_render_scale_percent =
            hud_clampi(g_rs.pause_menu.graphics_render_scale_percent, 50, 100);
        g_rs.pause_menu.graphics_anti_aliasing_mode =
            hud_clampi(g_rs.pause_menu.graphics_anti_aliasing_mode, 0, 1);
        g_rs.pause_menu.graphics_shadow_quality =
            hud_clampi(g_rs.pause_menu.graphics_shadow_quality, 0, 3);
        g_rs.pause_menu.graphics_water_quality =
            hud_clampi(g_rs.pause_menu.graphics_water_quality, 0, 1);
        g_rs.pause_menu.graphics_black_superchunk_walls =
            g_rs.pause_menu.graphics_black_superchunk_walls ? true : false;
        g_rs.pause_menu.graphics_smooth_lighting =
            g_rs.pause_menu.graphics_smooth_lighting ? true : false;
        g_rs.pause_menu.creative_filter =
            hud_clampi(g_rs.pause_menu.creative_filter, SDK_CREATIVE_FILTER_ALL, SDK_CREATIVE_FILTER_ITEMS);
        g_rs.pause_menu.creative_shape_focus =
            hud_clampi(g_rs.pause_menu.creative_shape_focus, 0, 1);
        g_rs.pause_menu.creative_shape_row =
            hud_clampi(g_rs.pause_menu.creative_shape_row, 0, 2);
        g_rs.pause_menu.creative_shape_width =
            hud_clampi(g_rs.pause_menu.creative_shape_width, 1, 16);
        g_rs.pause_menu.creative_shape_height =
            hud_clampi(g_rs.pause_menu.creative_shape_height, 1, 16);
        g_rs.pause_menu.creative_shape_depth =
            hud_clampi(g_rs.pause_menu.creative_shape_depth, 1, 16);
        g_rs.pause_menu.keybind_selected =
            hud_clampi(g_rs.pause_menu.keybind_selected, 0, 0x7FFF);
        g_rs.pause_menu.keybind_scroll =
            hud_clampi(g_rs.pause_menu.keybind_scroll, 0, 0x7FFF);
        g_rs.pause_menu.keybind_total =
            hud_clampi(g_rs.pause_menu.keybind_total, 0, 0x7FFF);
        g_rs.pause_menu.keybind_visible_count =
            hud_clampi(g_rs.pause_menu.keybind_visible_count, 0, SDK_KEYBIND_MENU_VISIBLE_ROWS);
        g_rs.pause_menu.keybind_capture_active =
            g_rs.pause_menu.keybind_capture_active ? true : false;
        g_rs.pause_menu.invert_y =
            g_rs.pause_menu.invert_y ? true : false;
        g_rs.pause_menu.character_count =
            hud_clampi(g_rs.pause_menu.character_count, 0, SDK_START_MENU_ASSET_VISIBLE_MAX);
        g_rs.pause_menu.character_selected =
            hud_clampi(g_rs.pause_menu.character_selected, 0, 0x7FFF);
        g_rs.pause_menu.character_scroll =
            hud_clampi(g_rs.pause_menu.character_scroll, 0, 0x7FFF);
        g_rs.pause_menu.character_current =
            hud_clampi(g_rs.pause_menu.character_current, -1, 0x7FFF);
        g_rs.hotbar_dirty = true;
    }
}

extern "C" void sdk_renderer_set_start_menu(const SdkStartMenuUI* ui)
{
    if (ui) {
        g_rs.start_menu = *ui;
        g_rs.hotbar_dirty = true;
    }
}

extern "C" void sdk_renderer_set_editor(const SdkEditorUI* ui)
{
    if (ui) {
        g_rs.editor_ui = *ui;
        g_rs.hotbar_dirty = true;
    }
}

extern "C" void sdk_renderer_set_command_line(const SdkCommandLineUI* ui)
{
    if (ui) {
        g_rs.command_line = *ui;
        g_rs.hotbar_dirty = true;
    }
}

extern "C" void sdk_renderer_set_map(const SdkMapUI* ui)
{
    if (ui) {
        if (memcmp(&g_rs.map_ui, ui, sizeof(*ui)) != 0) {
            g_rs.map_ui = *ui;
            g_rs.hotbar_dirty = true;
        }
    }
}

extern "C" void sdk_renderer_set_fluid_debug(const SdkFluidDebugUI* ui)
{
    if (ui) {
        g_rs.fluid_debug = *ui;
        g_rs.hotbar_dirty = true;
    }
}

extern "C" void sdk_renderer_set_perf_debug(const SdkPerfDebugUI* ui)
{
    if (ui) {
        g_rs.perf_debug = *ui;
        g_rs.hotbar_dirty = true;
    }
}

extern "C" void sdk_renderer_set_settlement_debug(const SdkSettlementDebugUI* ui)
{
    if (ui) {
        g_rs.settlement_debug = *ui;
        g_rs.hotbar_dirty = true;
    }
}
