/**
 * Returns true if voxel buffer contains any non-air blocks
 */
static SdkChunkGpuUploadMode target_upload_mode_for_chunk(SdkChunkResidencyRole role,
                                                          int cx,
                                                          int cz);
static size_t estimate_chunk_upload_bytes_for_mode(const SdkChunk* chunk,
                                                   SdkChunkGpuUploadMode mode);
static int sample_camera_water_state(float cam_x,
                                     float cam_y,
                                     float cam_z,
                                     int* out_camera_submerged,
                                     float* out_waterline_y,
                                     float* out_water_view_depth);
static void repair_dropped_remesh_results(void);

bool editor_voxel_buffer_has_blocks(const uint8_t* voxels, size_t voxel_count)
{
    size_t i;
    if (!voxels) return false;
    for (i = 0; i < voxel_count; ++i) {
        if ((BlockType)voxels[i] != BLOCK_AIR) {
            return true;
        }
    }
    return false;
}

static void editor_prefill_hotbar(void)
{
    static const BlockType color_blocks[] = {
        BLOCK_COLOR_WHITE,
        BLOCK_COLOR_BLACK,
        BLOCK_COLOR_RED,
        BLOCK_COLOR_ORANGE,
        BLOCK_COLOR_YELLOW,
        BLOCK_COLOR_GREEN,
        BLOCK_COLOR_CYAN,
        BLOCK_COLOR_BLUE,
        BLOCK_COLOR_PURPLE,
        BLOCK_STONE
    };
    static const BlockType prop_blocks[] = {
        BLOCK_STONE,
        BLOCK_COBBLESTONE,
        BLOCK_LOG,
        BLOCK_PLANKS,
        BLOCK_BRICK,
        BLOCK_STONE_BRICKS,
        BLOCK_CONCRETE,
        BLOCK_REINFORCED_CONCRETE,
        BLOCK_GRAVEL,
        BLOCK_SANDBAGS
    };
    const BlockType* selected_blocks = color_blocks;
    int i;

    if (g_session_kind == SDK_SESSION_KIND_PROP_EDITOR) {
        selected_blocks = prop_blocks;
    }

    for (i = 0; i < 10; ++i) {
        clear_hotbar_entry(&g_hotbar[i]);
        hotbar_set_creative_block(i, selected_blocks[i]);
    }
    g_hotbar_selected = 0;
    g_creative_menu_filter = (g_session_kind == SDK_SESSION_KIND_PROP_EDITOR)
        ? SDK_CREATIVE_FILTER_BUILDING_BLOCKS
        : SDK_CREATIVE_FILTER_COLORS;
    g_creative_menu_selected = 0;
    g_creative_menu_scroll = 0;
    g_creative_menu_search[0] = '\0';
    g_creative_menu_search_len = 0;
}

static size_t editor_voxel_index_for_dims(int vx, int vz, int vy, int width, int depth)
{
    return ((size_t)vy * (size_t)depth + (size_t)vz) * (size_t)width + (size_t)vx;
}

static size_t editor_voxel_index(int vx, int vz, int vy)
{
    return editor_voxel_index_for_dims(vx, vz, vy,
                                       SDK_CHARACTER_MODEL_WIDTH,
                                       SDK_CHARACTER_MODEL_DEPTH);
}

static void set_gameplay_character_vertex(BlockVertex* v,
                                          float x, float y, float z,
                                          uint32_t color, uint32_t normal)
{
    if (!v) return;
    v->position[0] = x;
    v->position[1] = y;
    v->position[2] = z;
    v->color = color;
    v->normal = normal;
    v->uv[0] = 0.0f;
    v->uv[1] = 0.0f;
    v->tex_index = UINT32_MAX;
}

static void gameplay_character_emit_face(BlockVertex* verts,
                                         uint32_t* inout_count,
                                         float x0, float y0, float z0,
                                         float x1, float y1, float z1,
                                         uint32_t color,
                                         int face)
{
    uint32_t count;

    if (!verts || !inout_count) return;
    count = *inout_count;

    switch (face) {
        case FACE_NEG_X:
            set_gameplay_character_vertex(&verts[count + 0], x0, y0, z0, color, FACE_NEG_X);
            set_gameplay_character_vertex(&verts[count + 1], x0, y1, z0, color, FACE_NEG_X);
            set_gameplay_character_vertex(&verts[count + 2], x0, y0, z1, color, FACE_NEG_X);
            set_gameplay_character_vertex(&verts[count + 3], x0, y0, z1, color, FACE_NEG_X);
            set_gameplay_character_vertex(&verts[count + 4], x0, y1, z0, color, FACE_NEG_X);
            set_gameplay_character_vertex(&verts[count + 5], x0, y1, z1, color, FACE_NEG_X);
            break;
        case FACE_POS_X:
            set_gameplay_character_vertex(&verts[count + 0], x1, y0, z1, color, FACE_POS_X);
            set_gameplay_character_vertex(&verts[count + 1], x1, y1, z1, color, FACE_POS_X);
            set_gameplay_character_vertex(&verts[count + 2], x1, y0, z0, color, FACE_POS_X);
            set_gameplay_character_vertex(&verts[count + 3], x1, y0, z0, color, FACE_POS_X);
            set_gameplay_character_vertex(&verts[count + 4], x1, y1, z1, color, FACE_POS_X);
            set_gameplay_character_vertex(&verts[count + 5], x1, y1, z0, color, FACE_POS_X);
            break;
        case FACE_POS_Y:
            set_gameplay_character_vertex(&verts[count + 0], x0, y1, z1, color, FACE_POS_Y);
            set_gameplay_character_vertex(&verts[count + 1], x1, y1, z1, color, FACE_POS_Y);
            set_gameplay_character_vertex(&verts[count + 2], x0, y1, z0, color, FACE_POS_Y);
            set_gameplay_character_vertex(&verts[count + 3], x0, y1, z0, color, FACE_POS_Y);
            set_gameplay_character_vertex(&verts[count + 4], x1, y1, z1, color, FACE_POS_Y);
            set_gameplay_character_vertex(&verts[count + 5], x1, y1, z0, color, FACE_POS_Y);
            break;
        case FACE_NEG_Z:
            set_gameplay_character_vertex(&verts[count + 0], x1, y0, z0, color, FACE_NEG_Z);
            set_gameplay_character_vertex(&verts[count + 1], x1, y1, z0, color, FACE_NEG_Z);
            set_gameplay_character_vertex(&verts[count + 2], x0, y0, z0, color, FACE_NEG_Z);
            set_gameplay_character_vertex(&verts[count + 3], x0, y0, z0, color, FACE_NEG_Z);
            set_gameplay_character_vertex(&verts[count + 4], x1, y1, z0, color, FACE_NEG_Z);
            set_gameplay_character_vertex(&verts[count + 5], x1, y0, z0, color, FACE_NEG_Z);
            break;
        default:
            break;
    }

    *inout_count = count + 6u;
}

static int gameplay_character_depth_voxels(void)
{
    return 11;
}

static int gameplay_character_height_voxels(void)
{
    return 57;
}

static bool gameplay_character_face_supported(int face)
{
    return face == FACE_NEG_X || face == FACE_POS_X ||
           face == FACE_POS_Y || face == FACE_NEG_Z;
}

static BlockType gameplay_character_voxel(const uint8_t* voxels, int vx, int vz, int vy)
{
    if (!voxels) return BLOCK_AIR;
    if (vx < 0 || vx >= SDK_CHARACTER_MODEL_WIDTH ||
        vz < 0 || vz >= gameplay_character_depth_voxels() ||
        vy < 0 || vy >= gameplay_character_height_voxels()) {
        return BLOCK_AIR;
    }
    return (BlockType)voxels[editor_voxel_index(vx, vz, vy)];
}

static void refresh_chunk_wall_finalization_state(SdkChunk* chunk)
{
    SdkActiveWallStage stage;

    if (!chunk) return;

    stage = sdk_superchunk_active_wall_stage_for_chunk(g_sdk.chunk_mgr.primary_scx,
                                                       g_sdk.chunk_mgr.primary_scz,
                                                       chunk->cx,
                                                       chunk->cz);
    if (stage == SDK_ACTIVE_WALL_STAGE_EDGE || stage == SDK_ACTIVE_WALL_STAGE_CORNER) {
        if (chunk->wall_finalized_generation != g_sdk.chunk_mgr.topology_generation) {
            chunk->wall_finalized_generation = 0u;
        }
    } else {
        chunk->wall_finalized_generation = g_sdk.chunk_mgr.topology_generation;
    }
}

static int build_gameplay_character_mesh(const uint8_t* voxels,
                                         BlockVertex** out_vertices,
                                         uint32_t* out_vertex_count)
{
    const float voxel_size = PLAYER_HEIGHT / (float)SDK_CHARACTER_MODEL_HEIGHT;
    const float width = voxel_size * (float)SDK_CHARACTER_MODEL_WIDTH;
    const float x_origin = -width * 0.5f;
    const float z_origin = 0.04f;
    const uint32_t max_vertices =
        (uint32_t)(SDK_CHARACTER_MODEL_WIDTH *
                   gameplay_character_depth_voxels() *
                   gameplay_character_height_voxels() * 24);
    BlockVertex* verts;
    uint32_t count = 0;
    int vx;
    int vz;
    int vy;

    if (out_vertices) *out_vertices = NULL;
    if (out_vertex_count) *out_vertex_count = 0;
    if (!voxels || !out_vertices || !out_vertex_count) return 0;

    verts = (BlockVertex*)malloc((size_t)max_vertices * sizeof(BlockVertex));
    if (!verts) return 0;

    for (vy = 0; vy < gameplay_character_height_voxels(); ++vy) {
        for (vz = 0; vz < gameplay_character_depth_voxels(); ++vz) {
            for (vx = 0; vx < SDK_CHARACTER_MODEL_WIDTH; ++vx) {
                static const int faces[] = { FACE_NEG_X, FACE_POS_X, FACE_POS_Y, FACE_NEG_Z };
                BlockType block = gameplay_character_voxel(voxels, vx, vz, vy);
                float x0;
                float y0;
                float z0;
                float x1;
                float y1;
                float z1;
                int face_index;

                if (block == BLOCK_AIR) continue;

                x0 = x_origin + (float)vx * voxel_size;
                y0 = (float)vy * voxel_size;
                z0 = z_origin + (float)vz * voxel_size;
                x1 = x0 + voxel_size;
                y1 = y0 + voxel_size;
                z1 = z0 + voxel_size;

                for (face_index = 0; face_index < (int)(sizeof(faces) / sizeof(faces[0])); ++face_index) {
                    int face = faces[face_index];
                    int nx = vx;
                    int nz = vz;
                    int ny = vy;

                    if (!gameplay_character_face_supported(face)) continue;
                    switch (face) {
                        case FACE_NEG_X: nx = vx - 1; break;
                        case FACE_POS_X: nx = vx + 1; break;
                        case FACE_POS_Y: ny = vy + 1; break;
                        case FACE_NEG_Z: nz = vz - 1; break;
                        default: break;
                    }
                    if (gameplay_character_voxel(voxels, nx, nz, ny) != BLOCK_AIR) continue;
                    gameplay_character_emit_face(verts, &count, x0, y0, z0, x1, y1, z1,
                                                 sdk_block_get_face_color(block, face), face);
                }
            }
        }
    }

    if (count == 0u) {
        free(verts);
        return 1;
    }

    *out_vertices = verts;
    *out_vertex_count = count;
    return 1;
}

void rebuild_selected_gameplay_character_mesh(void)
{
    uint8_t voxels[SDK_CHARACTER_MODEL_VOXELS];
    BlockVertex* vertices = NULL;
    uint32_t vertex_count = 0;

    if (g_selected_character_index < 0 || g_selected_character_index >= g_character_asset_count) {
        sdk_renderer_set_player_character_mesh(NULL, 0);
        return;
    }

    if (!load_character_model_bytes(g_character_assets[g_selected_character_index].asset_id,
                                    voxels, sizeof(voxels))) {
        sdk_renderer_set_player_character_mesh(NULL, 0);
        return;
    }

    if (!build_gameplay_character_mesh(voxels, &vertices, &vertex_count)) {
        sdk_renderer_set_player_character_mesh(NULL, 0);
        return;
    }

    sdk_renderer_set_player_character_mesh(vertices, vertex_count);
    free(vertices);
}

SdkChunk* generate_or_load_chunk_sync(int cx, int cz, SdkChunkResidencyRole role)
{
    SdkChunkResidentSlot* slot;
    int loaded_from_persistence = 0;
    SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, cx, cz);

    char dbg[256];
    const char* role_name = (role == SDK_CHUNK_ROLE_PRIMARY) ? "PRIMARY" :
                           (role == SDK_CHUNK_ROLE_FRONTIER) ? "FRONTIER" :
                           (role == SDK_CHUNK_ROLE_WALL_SUPPORT) ? "WALL_SUPPORT" :
                           (role == SDK_CHUNK_ROLE_TRANSITION_PRELOAD) ? "TRANSITION_PRELOAD" :
                           (role == SDK_CHUNK_ROLE_EVICT_PENDING) ? "EVICT_PENDING" : "UNKNOWN";

    if (chunk) {
        slot = sdk_chunk_manager_find_slot(&g_sdk.chunk_mgr, cx, cz);
        if (slot) {
            slot->role = (uint8_t)role;
            slot->desired = 1u;
            slot->desired_role = (uint8_t)role;
            refresh_chunk_wall_finalization_state(&slot->chunk);
        }
        sprintf_s(dbg, sizeof(dbg), "[CHUNK] Already resident (%d,%d) role=%s\n", cx, cz, role_name);
        OutputDebugStringA(dbg);
        return chunk;
    }

    sprintf_s(dbg, sizeof(dbg), "[CHUNK] Loading (%d,%d) role=%s\n", cx, cz, role_name);
    OutputDebugStringA(dbg);

    slot = sdk_chunk_manager_reserve_slot(&g_sdk.chunk_mgr, cx, cz, role);
    if (!slot) {
        sprintf_s(dbg, sizeof(dbg), "[CHUNK] FAILED to reserve slot (%d,%d) role=%s\n", cx, cz, role_name);
        OutputDebugStringA(dbg);
        return NULL;
    }

    sdk_chunk_init(&slot->chunk, cx, cz, g_sdk.chunk_mgr.construction_registry);
    chunk = &slot->chunk;
    if (!chunk->blocks) {
        sdk_chunk_manager_release_slot(&g_sdk.chunk_mgr, slot);
        sprintf_s(dbg, sizeof(dbg), "[CHUNK] FAILED block allocation (%d,%d) role=%s\n", cx, cz, role_name);
        OutputDebugStringA(dbg);
        return NULL;
    }
    sdk_chunk_manager_rebuild_lookup(&g_sdk.chunk_mgr);

    if (!sdk_persistence_load_chunk(&g_sdk.persistence, cx, cz, chunk)) {
        sprintf_s(dbg, sizeof(dbg), "[CHUNK] Generating (%d,%d) role=%s\n", cx, cz, role_name);
        OutputDebugStringA(dbg);
        sdk_worldgen_generate_chunk_ctx(&g_sdk.worldgen, chunk);
    } else {
        loaded_from_persistence = 1;
        sprintf_s(dbg, sizeof(dbg), "[CHUNK] Loaded from persistence (%d,%d) role=%s\n", cx, cz, role_name);
        OutputDebugStringA(dbg);
    }
    if (!loaded_from_persistence) {
        resolve_loaded_chunk_boundary_water(chunk);
    }
    refresh_chunk_wall_finalization_state(chunk);
    sdk_simulation_on_chunk_loaded(&g_sdk.chunk_mgr, chunk);
    sprintf_s(dbg, sizeof(dbg), "[CHUNK] SUCCESS (%d,%d) role=%s\n", cx, cz, role_name);
    OutputDebugStringA(dbg);
    return chunk;
}

static void sync_animation_preview_chunk(bool advance_frame)
{
    uint8_t voxels[SDK_CHARACTER_MODEL_VOXELS];
    SdkChunk* preview_chunk;
    SdkChunk* source_chunk;
    int source_frame;
    bool source_dirty = false;

    if (!editor_session_active()) return;
    if (g_session_kind != SDK_SESSION_KIND_ANIMATION_EDITOR) return;
    if (g_editor_session.frame_count <= 0) return;

    preview_chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, editor_preview_chunk_cx(), 0);
    if (!preview_chunk || !preview_chunk->blocks) return;

    source_frame = api_clampi(g_editor_session.preview_frame, 0, g_editor_session.frame_count - 1);
    if (advance_frame) {
        source_frame = (source_frame + 1) % g_editor_session.frame_count;
        g_editor_session.preview_frame = source_frame;
        g_editor_session.preview_counter = 0;
    }

    source_chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, editor_frame_chunk_cx(source_frame), 0);
    if (source_chunk) {
        source_dirty = source_chunk->dirty;
    }

    if (!advance_frame && !source_dirty) {
        return;
    }

    memset(voxels, (int)BLOCK_AIR, sizeof(voxels));
    if (source_chunk && source_chunk->blocks) {
        capture_editor_chunk_voxels(source_chunk, voxels);
    }

    reset_editor_chunk_contents(preview_chunk);
    stamp_editor_voxels_into_chunk(preview_chunk, voxels);
    sdk_chunk_mark_all_dirty(preview_chunk);
}

void update_window_title_for_test_flight(void)
{
    char title[512];
    wchar_t wtitle[512];
    const char* base_title;
    const char* debug_suffix = NULL;
    HWND hwnd;

    if (!g_sdk.window) return;

    base_title = g_sdk.desc.window.title ? g_sdk.desc.window.title : "NQL SDK - D3D12";
    switch (sdk_worldgen_get_debug_mode_ctx(&g_sdk.worldgen)) {
        case SDK_WORLDGEN_DEBUG_FORMATIONS:
            debug_suffix = "GEO DEBUG: FORMATIONS";
            break;
        case SDK_WORLDGEN_DEBUG_STRUCTURES:
            debug_suffix = "GEO DEBUG: STRUCTURES";
            break;
        case SDK_WORLDGEN_DEBUG_BODIES:
            debug_suffix = "GEO DEBUG: BODIES";
            break;
        case SDK_WORLDGEN_DEBUG_OFF:
        default:
            debug_suffix = NULL;
            break;
    }

    if (g_test_flight_enabled && debug_suffix) {
        snprintf(title, sizeof(title), "%s [TEST FLIGHT ENABLED - testing only | %s]", base_title, debug_suffix);
    } else if (g_test_flight_enabled) {
        snprintf(title, sizeof(title), "%s [TEST FLIGHT ENABLED - testing only]", base_title);
    } else if (debug_suffix) {
        snprintf(title, sizeof(title), "%s [%s]", base_title, debug_suffix);
    } else {
        snprintf(title, sizeof(title), "%s", base_title);
    }

    hwnd = sdk_window_hwnd(g_sdk.window);
    if (!hwnd) return;

    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, (int)(sizeof(wtitle) / sizeof(wtitle[0])));
    SetWindowTextW(hwnd, wtitle);
}

bool editor_session_active(void)
{
    return g_editor_session.active &&
           (g_session_kind == SDK_SESSION_KIND_CHARACTER_EDITOR ||
            g_session_kind == SDK_SESSION_KIND_ANIMATION_EDITOR ||
            g_session_kind == SDK_SESSION_KIND_PROP_EDITOR ||
            g_session_kind == SDK_SESSION_KIND_BLOCK_EDITOR ||
            g_session_kind == SDK_SESSION_KIND_ITEM_EDITOR ||
            g_session_kind == SDK_SESSION_KIND_PARTICLE_EDITOR);
}

void apply_graphics_atmosphere(float ambient, float sky_r, float sky_g, float sky_b)
{
    SdkAtmosphereUI ui;
    float cam_x = 0.0f;
    float cam_y = 0.0f;
    float cam_z = 0.0f;
    float fog_density = 0.0f;
    float water_alpha = 0.72f;
    float neutral_sky = (sky_r + sky_g + sky_b) / 3.0f;
    float smoke = api_clampf(g_player_smoke_obscurance, 0.0f, 1.0f);
    float flash = 0.0f;
    float waterline_y = 0.0f;
    float water_view_depth = 0.0f;
    int camera_submerged = 0;
    int water_column_visible = 0;

    if (g_screen_flash_timer > 0 && g_screen_flash_duration > 0) {
        flash = api_clampf(((float)g_screen_flash_timer / (float)g_screen_flash_duration) *
                           g_screen_flash_strength, 0.0f, 1.0f);
    }

    memset(&ui, 0, sizeof(ui));
    ui.enabled = true;
    ui.ambient = ambient;
    ui.sky_r = sky_r;
    ui.sky_g = sky_g;
    ui.sky_b = sky_b;
    ui.sun_dir_x = 0.35f;
    ui.sun_dir_y = 0.82f;
    ui.sun_dir_z = 0.44f;
    ui.fog_r = neutral_sky * 0.54f + sky_r * 0.30f + 0.10f;
    ui.fog_g = neutral_sky * 0.58f + sky_g * 0.26f + 0.10f;
    ui.fog_b = neutral_sky * 0.60f + sky_b * 0.14f + 0.08f;
    ui.water_r = 0.18f;
    ui.water_g = 0.42f;
    ui.water_b = 0.58f;
    ui.camera_submerged = false;
    ui.waterline_y = 0.0f;
    ui.water_view_depth = 0.0f;

    sdk_renderer_get_camera_pos(&cam_x, &cam_y, &cam_z);
    water_column_visible = sample_camera_water_state(cam_x, cam_y, cam_z,
                                                     &camera_submerged,
                                                     &waterline_y,
                                                     &water_view_depth);

    if (!g_graphics_settings.fog_enabled) {
        fog_density = 0.0f;
    } else {
        switch (g_graphics_settings.preset) {
            case SDK_GRAPHICS_PRESET_PERFORMANCE: fog_density = 0.0018f; water_alpha = 0.58f; break;
            case SDK_GRAPHICS_PRESET_HIGH:        fog_density = 0.0038f; water_alpha = 0.82f; break;
            case SDK_GRAPHICS_PRESET_BALANCED:
            default:                             fog_density = 0.0028f; water_alpha = 0.72f; break;
        }
    }

    if (g_graphics_settings.water_quality == SDK_WATER_QUALITY_LOW) {
        ui.water_r = 0.20f;
        ui.water_g = 0.38f;
        ui.water_b = 0.50f;
        water_alpha *= 0.88f;
    } else {
        ui.water_r = 0.16f;
        ui.water_g = 0.46f;
        ui.water_b = 0.66f;
        water_alpha = api_clampf(water_alpha + 0.04f, 0.0f, 1.0f);
    }

    if (smoke > 0.01f) {
        ambient = ambient * (1.0f - smoke * 0.22f) + 0.52f * smoke * 0.22f;
        ui.fog_r = ui.fog_r * (1.0f - smoke * 0.55f) + 0.66f * smoke * 0.55f;
        ui.fog_g = ui.fog_g * (1.0f - smoke * 0.55f) + 0.68f * smoke * 0.55f;
        ui.fog_b = ui.fog_b * (1.0f - smoke * 0.55f) + 0.70f * smoke * 0.55f;
        fog_density += 0.020f * smoke;
    }
    if (flash > 0.01f) {
        ambient = ambient * (1.0f - flash) + flash;
        ui.sky_r = ui.sky_r * (1.0f - flash) + flash;
        ui.sky_g = ui.sky_g * (1.0f - flash) + flash;
        ui.sky_b = ui.sky_b * (1.0f - flash) + flash;
        ui.fog_r = ui.fog_r * (1.0f - flash) + flash;
        ui.fog_g = ui.fog_g * (1.0f - flash) + flash;
        ui.fog_b = ui.fog_b * (1.0f - flash) + flash;
        fog_density *= (1.0f - flash * 0.75f);
    }

    if (water_column_visible) {
        ui.waterline_y = waterline_y;
        ui.water_view_depth = water_view_depth;
    }
    if (camera_submerged) {
        float depth_factor = api_clampf((waterline_y - cam_y) / 3.0f, 0.0f, 1.0f);
        ui.camera_submerged = true;
        ambient *= 0.78f;
        ui.sky_r = ui.water_r * 0.34f + 0.02f;
        ui.sky_g = ui.water_g * 0.42f + 0.03f;
        ui.sky_b = ui.water_b * 0.55f + 0.05f;
        ui.fog_r = ui.water_r * 0.92f;
        ui.fog_g = ui.water_g * 0.98f;
        ui.fog_b = ui.water_b;
        fog_density = api_clampf(fmaxf(fog_density, 0.020f + depth_factor * 0.010f), 0.0f, 0.080f);
        water_alpha = api_clampf(water_alpha + 0.12f, 0.0f, 1.0f);
    } else if (water_column_visible) {
        float depth_factor = api_clampf(water_view_depth / 12.0f, 0.0f, 1.0f);
        water_alpha = api_clampf(water_alpha + depth_factor * 0.06f, 0.0f, 1.0f);
    }

    sdk_renderer_set_lighting(ambient, ui.sky_r, ui.sky_g, ui.sky_b);
    ui.ambient = ambient;
    ui.fog_density = fog_density;
    ui.water_alpha = water_alpha;
    sdk_renderer_set_atmosphere(&ui);
}

void apply_persisted_state(const SdkPersistedState* state)
{
    int i;
    int selected_character_index = g_selected_character_index;
    float look_target_x;
    float look_target_y;
    float look_target_z;
    float cos_p;
    float sin_p;

    if (!state) return;

    g_spawn_x = state->spawn[0];
    g_spawn_y = state->spawn[1];
    g_spawn_z = state->spawn[2];
    g_player_health = state->health > 0 ? state->health : PLAYER_MAX_HEALTH;
    g_player_hunger = state->hunger > 0 ? state->hunger : PLAYER_MAX_HUNGER;
    g_world_time = state->world_time;
    g_hotbar_selected = state->hotbar_selected;
    g_chunk_grid_size_setting = sdk_chunk_manager_normalize_grid_size(state->chunk_grid_size);
    g_graphics_settings.chunk_grid_size = g_chunk_grid_size_setting;
    if (g_hotbar_selected < 0) g_hotbar_selected = 0;
    if (g_hotbar_selected >= SDK_PERSISTENCE_HOTBAR_SLOTS) g_hotbar_selected = SDK_PERSISTENCE_HOTBAR_SLOTS - 1;
    g_player_level = (state->level > 0) ? state->level : 1;
    g_player_xp = (state->xp >= 0) ? state->xp : 0;
    g_player_xp_to_next = (state->xp_to_next > 0) ? state->xp_to_next : 100;
    g_player_dead = false;
    g_death_timer = 0;
    g_hunger_tick = 0;
    g_is_sprinting = false;
    g_test_flight_enabled = false;

    for (i = 0; i < SDK_PERSISTENCE_HOTBAR_SLOTS; ++i) {
        clear_hotbar_entry(&g_hotbar[i]);
        g_hotbar[i].item = state->hotbar_item[i];
        g_hotbar[i].creative_block = state->hotbar_block[i];
        g_hotbar[i].count = state->hotbar_count[i];
        g_hotbar[i].durability = state->hotbar_durability[i];
        g_hotbar[i].payload_kind = state->hotbar_payload_kind[i];
        g_hotbar[i].shaped = state->hotbar_shaped[i];
    }

    if (state->selected_character_id[0] != '\0') {
        selected_character_index = -1;
        refresh_character_assets();
        for (i = 0; i < g_character_asset_count; ++i) {
            if (strcmp(g_character_assets[i].asset_id, state->selected_character_id) == 0) {
                selected_character_index = i;
                break;
            }
        }
        g_selected_character_index = selected_character_index;
    }
    g_pause_character_selected = (g_selected_character_index >= 0) ? g_selected_character_index : 0;
    g_pause_character_scroll = 0;

    g_cam_yaw = state->cam_yaw;
    g_cam_pitch = state->cam_pitch;
    g_cam_rotation_initialized = true;
    g_cam_look_dist = 1.0f;

    sdk_renderer_set_camera_pos(state->position[0], state->position[1], state->position[2]);
    cos_p = cosf(g_cam_pitch);
    sin_p = sinf(g_cam_pitch);
    look_target_x = state->position[0] + g_cam_look_dist * cos_p * sinf(g_cam_yaw);
    look_target_y = state->position[1] + g_cam_look_dist * sin_p;
    look_target_z = state->position[2] + g_cam_look_dist * cos_p * cosf(g_cam_yaw);
    sdk_renderer_set_camera_target(look_target_x, look_target_y, look_target_z);
    rebuild_selected_gameplay_character_mesh();
}

void capture_persisted_state(SdkPersistedState* out_state)
{
    float cam_x, cam_y, cam_z;
    int i;

    if (!out_state) return;
    memset(out_state, 0, sizeof(*out_state));

    sdk_renderer_get_camera_pos(&cam_x, &cam_y, &cam_z);
    out_state->position[0] = cam_x;
    out_state->position[1] = cam_y;
    out_state->position[2] = cam_z;
    out_state->spawn[0] = g_spawn_x;
    out_state->spawn[1] = g_spawn_y;
    out_state->spawn[2] = g_spawn_z;
    out_state->cam_yaw = g_cam_yaw;
    out_state->cam_pitch = g_cam_pitch;
    out_state->health = g_player_health;
    out_state->hunger = g_player_hunger;
    out_state->world_time = g_world_time;
    out_state->hotbar_selected = g_hotbar_selected;
    out_state->chunk_grid_size = g_chunk_grid_size_setting;
    out_state->level = g_player_level;
    out_state->xp = g_player_xp;
    out_state->xp_to_next = g_player_xp_to_next;
    if (g_selected_character_index >= 0 && g_selected_character_index < g_character_asset_count) {
        strcpy_s(out_state->selected_character_id, sizeof(out_state->selected_character_id),
                 g_character_assets[g_selected_character_index].asset_id);
    }

    for (i = 0; i < SDK_PERSISTENCE_HOTBAR_SLOTS; ++i) {
        out_state->hotbar_item[i] = g_hotbar[i].item;
        out_state->hotbar_block[i] = g_hotbar[i].creative_block;
        out_state->hotbar_count[i] = g_hotbar[i].count;
        out_state->hotbar_durability[i] = g_hotbar[i].durability;
        out_state->hotbar_payload_kind[i] = g_hotbar[i].payload_kind;
        out_state->hotbar_shaped[i] = g_hotbar[i].shaped;
        out_state->hotbar_shaped_material[i] = g_hotbar[i].shaped.material;
        out_state->hotbar_shaped_profile_hint[i] = g_hotbar[i].shaped.inline_profile_hint;
    }
}

void build_editor_ui(SdkEditorUI* out_ui)
{
    if (!out_ui) return;
    memset(out_ui, 0, sizeof(*out_ui));
    if (!editor_session_active()) return;

    out_ui->open = true;
    if (g_session_kind == SDK_SESSION_KIND_ANIMATION_EDITOR) {
        out_ui->kind = SDK_EDITOR_UI_ANIMATION;
    } else if (g_session_kind == SDK_SESSION_KIND_PROP_EDITOR) {
        out_ui->kind = SDK_EDITOR_UI_PROP;
    } else if (g_session_kind == SDK_SESSION_KIND_BLOCK_EDITOR) {
        out_ui->kind = SDK_EDITOR_UI_BLOCK;
    } else if (g_session_kind == SDK_SESSION_KIND_ITEM_EDITOR) {
        out_ui->kind = SDK_EDITOR_UI_ITEM;
    } else if (g_session_kind == SDK_SESSION_KIND_PARTICLE_EDITOR) {
        out_ui->kind = SDK_EDITOR_UI_PARTICLE;
    } else {
        out_ui->kind = SDK_EDITOR_UI_CHARACTER;
    }
    strcpy_s(out_ui->character_name, sizeof(out_ui->character_name), g_editor_session.character_name);
    strcpy_s(out_ui->animation_name, sizeof(out_ui->animation_name), g_editor_session.animation_name);
    out_ui->current_frame = g_editor_session.current_frame;
    out_ui->frame_count = g_editor_session.frame_count;
    out_ui->playback_enabled = g_editor_session.playback_enabled;
    if (g_session_kind == SDK_SESSION_KIND_ANIMATION_EDITOR) {
        strcpy_s(out_ui->status, sizeof(out_ui->status),
                 "LEFT CHUNK SLOW PREVIEW  PGUP/PGDN FRAME  F9 PLAY");
    } else if (g_session_kind == SDK_SESSION_KIND_PROP_EDITOR) {
        strcpy_s(out_ui->status, sizeof(out_ui->status),
                 "16x16 CHUNKS  1024x1024 FLOOR  1:1 PROP BUILD SPACE");
    } else if (g_session_kind == SDK_SESSION_KIND_BLOCK_EDITOR) {
        strcpy_s(out_ui->status, sizeof(out_ui->status),
                 "LEFT CHUNK MODEL  RIGHT CHUNK 32x32 ICON  COLORS READY");
    } else if (g_session_kind == SDK_SESSION_KIND_ITEM_EDITOR) {
        strcpy_s(out_ui->status, sizeof(out_ui->status),
                 "LEFT CHUNK ITEM MODEL  RIGHT CHUNK 32x32 ICON  COLORS READY");
    } else if (g_session_kind == SDK_SESSION_KIND_PARTICLE_EDITOR) {
        strcpy_s(out_ui->status, sizeof(out_ui->status),
                 "LEFT CHUNK EFFECT SLICES  RIGHT CHUNK 32x32 ICON  F9 PLAY");
    } else {
        strcpy_s(out_ui->status, sizeof(out_ui->status),
                 "24W x 16D x 64H BUILD VOLUME  CREATIVE COLORS READY");
    }
}

bool editor_block_in_bounds(int wx, int wy, int wz)
{
    int cx;
    int cz;
    int lx;
    int lz;

    if (!editor_session_active()) return false;
    cx = sdk_world_to_chunk_x(wx);
    cz = sdk_world_to_chunk_z(wz);
    lx = sdk_world_to_local_x(wx, cx);
    lz = sdk_world_to_local_z(wz, cz);

    if (g_session_kind == SDK_SESSION_KIND_PROP_EDITOR) {
        return cx >= g_editor_session.build_chunk_min_cx &&
               cx < g_editor_session.build_chunk_min_cx + g_editor_session.build_chunk_span_x &&
               cz >= g_editor_session.build_chunk_min_cz &&
               cz < g_editor_session.build_chunk_min_cz + g_editor_session.build_chunk_span_z &&
               wy >= g_editor_session.build_min_y &&
               wy < g_editor_session.build_min_y + g_editor_session.build_height;
    }

    if (cz != 0) return false;

    if (g_session_kind == SDK_SESSION_KIND_CHARACTER_EDITOR) {
        if (cx != 0) return false;
    } else if (g_session_kind == SDK_SESSION_KIND_ANIMATION_EDITOR) {
        if (cx < 0 || cx >= g_editor_session.frame_chunk_count) return false;
    } else if (g_session_kind == SDK_SESSION_KIND_BLOCK_EDITOR ||
               g_session_kind == SDK_SESSION_KIND_ITEM_EDITOR ||
               g_session_kind == SDK_SESSION_KIND_PARTICLE_EDITOR) {
        if (cx == 0) {
            return lx >= g_editor_session.build_local_x &&
                   lx < g_editor_session.build_local_x + g_editor_session.build_width &&
                   lz >= g_editor_session.build_local_z &&
                   lz < g_editor_session.build_local_z + g_editor_session.build_depth &&
                   wy >= g_editor_session.build_min_y &&
                   wy < g_editor_session.build_min_y + g_editor_session.build_height;
        }
        if (cx == g_editor_session.icon_chunk_cx) {
            return lx >= g_editor_session.icon_local_x &&
                   lx < g_editor_session.icon_local_x + g_editor_session.icon_width &&
                   lz >= g_editor_session.icon_local_z &&
                   lz < g_editor_session.icon_local_z + g_editor_session.icon_depth &&
                   wy == g_editor_session.icon_min_y;
        }
        return false;
    } else {
        return false;
    }

    return lx >= g_editor_session.build_local_x &&
           lx < g_editor_session.build_local_x + g_editor_session.build_width &&
           lz >= g_editor_session.build_local_z &&
           lz < g_editor_session.build_local_z + g_editor_session.build_depth &&
           wy >= g_editor_session.build_min_y &&
           wy < g_editor_session.build_min_y + g_editor_session.build_height;
}

void process_dirty_chunk_mesh_uploads(int max_chunks)
{
    repair_dropped_remesh_results();
    sdk_chunk_streamer_schedule_dirty(&g_sdk.chunk_streamer, &g_sdk.chunk_mgr, max_chunks);
}

void process_pending_chunk_gpu_uploads(int max_chunks, float budget_ms)
{
    LARGE_INTEGER start;
    LARGE_INTEGER now;
    LARGE_INTEGER freq;
    int processed = 0;
    int enforce_budget = budget_ms > 0.0f;
    size_t uploaded_bytes = 0u;
    size_t upload_budget_bytes = stream_upload_byte_budget_per_frame();

    if (max_chunks <= 0 || !g_sdk.chunks_initialized) return;

    if (enforce_budget) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    } else {
        memset(&freq, 0, sizeof(freq));
        memset(&start, 0, sizeof(start));
    }

    for (int pass = 0; pass < 2 && processed < max_chunks; ++pass) {
        for (int slot_index = 0;
             slot_index < sdk_chunk_manager_slot_capacity() && processed < max_chunks;
             ++slot_index) {
            SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(&g_sdk.chunk_mgr, slot_index);
            SdkChunk* chunk;
            SdkChunkGpuUploadMode target_mode;
            SdkChunkResidencyRole role;
            size_t estimated_upload_bytes;

            if (!slot || !slot->occupied) continue;
            chunk = &slot->chunk;
            if (!chunk->blocks) continue;
            if (sdk_chunk_needs_remesh(chunk)) continue;

            role = (SdkChunkResidencyRole)slot->role;
            target_mode = target_upload_mode_for_chunk(role, chunk->cx, chunk->cz);
            if (pass == 0 && target_mode != SDK_CHUNK_GPU_UPLOAD_FULL) continue;
            if (pass == 1 && target_mode == SDK_CHUNK_GPU_UPLOAD_FULL) continue;

            if (!chunk->upload_pending &&
                (SdkChunkGpuUploadMode)chunk->gpu_upload_mode >= target_mode) {
                continue;
            }

            estimated_upload_bytes = estimate_chunk_upload_bytes_for_mode(chunk, target_mode);
            if (estimated_upload_bytes == 0u) continue;

            if (processed > 0 && uploaded_bytes + estimated_upload_bytes > upload_budget_bytes) {
                continue;
            }
            if (processed == 0 &&
                uploaded_bytes + estimated_upload_bytes > upload_budget_bytes &&
                target_mode != SDK_CHUNK_GPU_UPLOAD_FULL) {
                continue;
            }

            if (sdk_renderer_upload_chunk_mesh_unified_mode(chunk, target_mode) != SDK_OK) {
                char dbg[192];
                sprintf_s(dbg, sizeof(dbg),
                          "[UPLOAD] deferred upload failed for chunk (%d,%d) role=%d mode=%d\n",
                          chunk->cx, chunk->cz, (int)role, (int)target_mode);
                OutputDebugStringA(dbg);
                continue;
            }

            uploaded_bytes += estimated_upload_bytes;
            processed++;

            if (enforce_budget) {
                QueryPerformanceCounter(&now);
                if ((((double)(now.QuadPart - start.QuadPart) * 1000.0) / (double)freq.QuadPart) >=
                    (double)budget_ms) {
                    return;
                }
            }
        }
    }
}

void select_gameplay_character_index(int character_index, bool persist_now)
{
    refresh_character_assets();
    if (g_character_asset_count <= 0) {
        g_selected_character_index = -1;
        g_pause_character_selected = 0;
        g_pause_character_scroll = 0;
        sdk_renderer_set_player_character_mesh(NULL, 0);
        return;
    }

    g_selected_character_index = api_clampi(character_index, 0, g_character_asset_count - 1);
    g_pause_character_selected = g_selected_character_index;
    rebuild_selected_gameplay_character_mesh();

    if (persist_now && g_sdk.world_session_active && !editor_session_active()) {
        SdkPersistedState persisted_state;
        capture_persisted_state(&persisted_state);
        sdk_persistence_set_state(&g_sdk.persistence, &persisted_state);
        sdk_persistence_save(&g_sdk.persistence);
    }
}

/* ============================================================================
 * Global State - Editor Input
 * ============================================================================ */
bool g_editor_play_was_down = false;
bool g_editor_prev_was_down = false;
bool g_editor_next_was_down = false;

/* ============================================================================
 * Editor Helper Functions
 * ============================================================================ */

static int editor_frame_chunk_cx(int frame_index)
{
    return frame_index;
}

static int editor_preview_chunk_cx(void)
{
    return -1;
}

static SdkChunk* create_editor_chunk(int cx, int cz)
{
    SdkChunkResidentSlot* slot;

    slot = sdk_chunk_manager_reserve_slot(&g_sdk.chunk_mgr, cx, cz, SDK_CHUNK_ROLE_PRIMARY);
    if (!slot) return NULL;
    sdk_chunk_init(&slot->chunk, cx, cz, g_sdk.chunk_mgr.construction_registry);
    if (!slot->chunk.blocks) {
        sdk_chunk_manager_release_slot(&g_sdk.chunk_mgr, slot);
        return NULL;
    }
    sdk_chunk_clear(&slot->chunk);
    slot->desired = 1u;
    slot->desired_role = (uint8_t)SDK_CHUNK_ROLE_PRIMARY;
    slot->role = (uint8_t)SDK_CHUNK_ROLE_PRIMARY;
    sdk_chunk_manager_rebuild_lookup(&g_sdk.chunk_mgr);
    return &slot->chunk;
}

static void fill_editor_floor_with_materials(SdkChunk* chunk, int floor_y,
                                             BlockType base_block, BlockType floor_block)
{
    int lx;
    int lz;
    int base_y;

    if (!chunk || !chunk->blocks) return;
    if (floor_y < 0 || floor_y >= CHUNK_HEIGHT) return;
    base_y = floor_y - 1;

    for (lz = 0; lz < CHUNK_DEPTH; ++lz) {
        for (lx = 0; lx < CHUNK_WIDTH; ++lx) {
            if (base_y >= 0) {
                sdk_chunk_set_block(chunk, lx, base_y, lz, base_block);
            }
            sdk_chunk_set_block(chunk, lx, floor_y, lz, floor_block);
        }
    }
}

void reset_editor_chunk_contents(SdkChunk* chunk)
{
    if (!chunk || !chunk->blocks) return;
    sdk_renderer_free_chunk_mesh(chunk);
    sdk_chunk_clear(chunk);
    fill_editor_floor(chunk, g_editor_session.base_floor_y);
}

static void stamp_editor_voxels_into_chunk_volume(SdkChunk* chunk, const uint8_t* voxels,
                                                  int local_x, int local_z, int min_y,
                                                  int width, int depth, int height)
{
    int vx;
    int vz;
    int vy;

    if (!chunk || !chunk->blocks || !voxels) return;

    for (vy = 0; vy < height; ++vy) {
        int ly = min_y + vy;
        if (ly < 0 || ly >= CHUNK_HEIGHT) continue;
        for (vz = 0; vz < depth; ++vz) {
            int lz = local_z + vz;
            for (vx = 0; vx < width; ++vx) {
                int lx = local_x + vx;
                BlockType block =
                    (BlockType)voxels[editor_voxel_index_for_dims(vx, vz, vy, width, depth)];
                if (block != BLOCK_AIR) {
                    sdk_chunk_set_block(chunk, lx, ly, lz, block);
                }
            }
        }
    }
}

static void stamp_editor_voxels_into_chunk(SdkChunk* chunk, const uint8_t* voxels)
{
    stamp_editor_voxels_into_chunk_volume(chunk, voxels,
                                        g_editor_session.build_local_x,
                                        g_editor_session.build_local_z,
                                        g_editor_session.build_min_y,
                                        g_editor_session.build_width,
                                        g_editor_session.build_depth,
                                        g_editor_session.build_height);
}

static void stamp_editor_icon_plane(SdkChunk* chunk, const uint8_t* voxels)
{
    int vx;
    int vz;

    if (!chunk || !chunk->blocks || !voxels) return;

    for (vz = 0; vz < g_editor_session.icon_depth; ++vz) {
        int lz = g_editor_session.icon_local_z + vz;
        for (vx = 0; vx < g_editor_session.icon_width; ++vx) {
            int lx = g_editor_session.icon_local_x + vx;
            BlockType block = (BlockType)voxels[(size_t)vz * (size_t)g_editor_session.icon_width + (size_t)vx];
            if (block != BLOCK_AIR) {
                sdk_chunk_set_block(chunk, lx, g_editor_session.icon_min_y, lz, block);
            }
        }
    }
}

static void capture_editor_chunk_voxels_volume(const SdkChunk* chunk, uint8_t* out_voxels,
                                               int local_x, int local_z, int min_y,
                                               int width, int depth, int height)
{
    int vx;
    int vz;
    int vy;
    size_t voxel_count = (size_t)width * (size_t)depth * (size_t)height;

    if (!out_voxels) return;
    memset(out_voxels, (int)BLOCK_AIR, voxel_count);
    if (!chunk || !chunk->blocks) return;

    for (vy = 0; vy < height; ++vy) {
        int ly = min_y + vy;
        if (ly < 0 || ly >= CHUNK_HEIGHT) continue;
        for (vz = 0; vz < depth; ++vz) {
            int lz = local_z + vz;
            for (vx = 0; vx < width; ++vx) {
                int lx = local_x + vx;
                out_voxels[editor_voxel_index_for_dims(vx, vz, vy, width, depth)] =
                    (uint8_t)sdk_chunk_get_block(chunk, lx, ly, lz);
            }
        }
    }
}

static void capture_editor_chunk_voxels(const SdkChunk* chunk, uint8_t* out_voxels)
{
    capture_editor_chunk_voxels_volume(chunk, out_voxels,
                                       g_editor_session.build_local_x,
                                       g_editor_session.build_local_z,
                                       g_editor_session.build_min_y,
                                       g_editor_session.build_width,
                                       g_editor_session.build_depth,
                                       g_editor_session.build_height);
}

static void fill_editor_floor(SdkChunk* chunk, int floor_y)
{
    fill_editor_floor_with_materials(chunk, floor_y, BLOCK_REINFORCED_CONCRETE, BLOCK_CONCRETE);
}

void finish_editor_return_to_frontend(SdkSessionKind previous_kind,
                                             const char* character_id,
                                             const char* animation_id)
{
    refresh_character_assets();
    if (character_id && character_id[0]) {
        int idx = find_character_asset_index_by_id_local(character_id);
        if (idx >= 0) {
            g_selected_character_index = idx;
        } else if (g_character_asset_count <= 0) {
            g_selected_character_index = -1;
        } else {
            g_selected_character_index = api_clampi(g_selected_character_index, 0, g_character_asset_count - 1);
        }
    }

    refresh_animation_assets_for_selected_character();
    if (previous_kind == SDK_SESSION_KIND_ANIMATION_EDITOR && animation_id && animation_id[0]) {
        int idx = find_animation_asset_index_by_id_local(animation_id);
        if (idx >= 0) {
            g_selected_animation_index = idx;
        } else if (g_animation_asset_count <= 0) {
            g_selected_animation_index = -1;
        } else {
            g_selected_animation_index = api_clampi(g_selected_animation_index, 0, g_animation_asset_count - 1);
        }
    }

    if (previous_kind == SDK_SESSION_KIND_ANIMATION_EDITOR &&
        g_selected_character_index >= 0 && g_animation_asset_count > 0) {
        g_frontend_main_selected = 4;
        g_frontend_view = SDK_START_MENU_VIEW_ANIMATION_LIST;
        g_animation_menu_selected = api_clampi(g_selected_animation_index, 0, g_animation_asset_count - 1);
    } else if (previous_kind == SDK_SESSION_KIND_CHARACTER_EDITOR) {
        g_frontend_main_selected = 4;
        g_frontend_view = SDK_START_MENU_VIEW_CHARACTERS;
        if (g_character_asset_count > 0) {
            g_character_menu_selected =
                api_clampi(g_selected_character_index + 1, 0, g_character_asset_count);
        } else {
            g_character_menu_selected = 0;
        }
    } else if (previous_kind == SDK_SESSION_KIND_PROP_EDITOR) {
        refresh_prop_assets();
        if (character_id && character_id[0]) {
            g_selected_prop_index = find_prop_asset_index_by_id_local(character_id);
        }
        g_frontend_main_selected = 5;
        g_frontend_view = SDK_START_MENU_VIEW_PROPS;
        g_prop_menu_selected = (g_selected_prop_index >= 0)
            ? api_clampi(g_selected_prop_index + 1, 0, g_prop_asset_count)
            : 0;
    } else if (previous_kind == SDK_SESSION_KIND_BLOCK_EDITOR) {
        refresh_block_assets();
        if (character_id && character_id[0]) {
            g_selected_block_index = find_block_asset_index_by_id_local(character_id);
        }
        g_frontend_main_selected = 6;
        g_frontend_view = SDK_START_MENU_VIEW_BLOCKS;
        g_block_menu_selected = (g_selected_block_index >= 0)
            ? api_clampi(g_selected_block_index + 1, 0, g_block_asset_count)
            : 0;
    } else if (previous_kind == SDK_SESSION_KIND_ITEM_EDITOR) {
        refresh_item_assets();
        if (character_id && character_id[0]) {
            g_selected_item_index = find_item_asset_index_by_id_local(character_id);
        }
        g_frontend_main_selected = 7;
        g_frontend_view = SDK_START_MENU_VIEW_ITEMS;
        g_item_menu_selected = (g_selected_item_index >= 0)
            ? api_clampi(g_selected_item_index + 1, 0, g_item_asset_count)
            : 0;
    } else if (previous_kind == SDK_SESSION_KIND_PARTICLE_EDITOR) {
        refresh_particle_effect_assets();
        if (character_id && character_id[0]) {
            g_selected_particle_effect_index = find_particle_effect_asset_index_by_id_local(character_id);
        }
        g_frontend_main_selected = 8;
        g_frontend_view = SDK_START_MENU_VIEW_PARTICLE_EFFECTS;
        g_particle_effect_menu_selected = (g_selected_particle_effect_index >= 0)
            ? api_clampi(g_selected_particle_effect_index + 1, 0, g_particle_effect_asset_count)
            : 0;
    } else {
        g_frontend_main_selected = 0;
        g_frontend_view = SDK_START_MENU_VIEW_MAIN;
    }
    frontend_reset_nav_state();
}
