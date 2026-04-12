/* ============================================================================
 * Editor Session Functions
 * ============================================================================ */

static int start_editor_session_common(SdkSessionKind kind,
                                       const SdkCharacterAssetMeta* character,
                                       const SdkAnimationAssetMeta* animation)
{
    int frame;
    int total_editor_chunks = 0;
    int frame_count = (kind == SDK_SESSION_KIND_ANIMATION_EDITOR && animation)
        ? api_clampi(animation->frame_count, 1, SDK_EDITOR_DEFAULT_FRAME_COUNT)
        : 1;
    uint8_t voxels[SDK_CHARACTER_MODEL_VOXELS];
    float center_x;
    float center_z;
    float cam_x;
    float cam_y;
    float cam_z;

    if (!character) return 0;
    if (g_sdk.world_session_active) {
        shutdown_world_session(true);
    }

    memset(&g_editor_session, 0, sizeof(g_editor_session));
    g_session_kind = kind;
    g_editor_session.active = true;
    g_editor_session.kind = kind;
    strcpy_s(g_editor_session.character_id, sizeof(g_editor_session.character_id), character->asset_id);
    strcpy_s(g_editor_session.character_name, sizeof(g_editor_session.character_name), character->display_name);
    if (animation) {
        strcpy_s(g_editor_session.animation_id, sizeof(g_editor_session.animation_id), animation->asset_id);
        strcpy_s(g_editor_session.animation_name, sizeof(g_editor_session.animation_name), animation->display_name);
    }
    g_editor_session.frame_count = frame_count;
    g_editor_session.current_frame = 0;
    g_editor_session.preview_frame = 0;
    g_editor_session.frame_chunk_count = frame_count;
    g_editor_session.preview_counter = 0;
    g_editor_session.playback_enabled = false;
    g_editor_session.base_floor_y = 128;
    g_editor_session.build_chunk_min_cx = 0;
    g_editor_session.build_chunk_min_cz = 0;
    g_editor_session.build_chunk_span_x = 1;
    g_editor_session.build_chunk_span_z = 1;
    g_editor_session.build_local_x = (CHUNK_WIDTH - SDK_CHARACTER_MODEL_WIDTH) / 2;
    g_editor_session.build_local_z = (CHUNK_DEPTH - SDK_CHARACTER_MODEL_DEPTH) / 2;
    g_editor_session.build_min_y = g_editor_session.base_floor_y + 1;
    g_editor_session.build_width = SDK_CHARACTER_MODEL_WIDTH;
    g_editor_session.build_depth = SDK_CHARACTER_MODEL_DEPTH;
    g_editor_session.build_height = SDK_CHARACTER_MODEL_HEIGHT;
    g_editor_session.icon_chunk_cx = 0;
    g_editor_session.icon_local_x = 0;
    g_editor_session.icon_local_z = 0;
    g_editor_session.icon_min_y = 0;
    g_editor_session.icon_width = 0;
    g_editor_session.icon_depth = 0;
    g_editor_session.slice_count = 0;
    g_editor_session.slice_width = 0;
    g_editor_play_was_down = false;
    g_editor_prev_was_down = false;
    g_editor_next_was_down = false;

    reset_session_runtime_state();
    skills_reset_progression();
    sdk_entity_init(&g_sdk.entities);
    station_close_ui();
    command_close();
    g_player_dead = false;
    g_player_health = PLAYER_MAX_HEALTH;
    g_player_hunger = PLAYER_MAX_HUNGER;
    g_test_flight_enabled = true;
    editor_prefill_hotbar();

    sdk_chunk_manager_init(&g_sdk.chunk_mgr);
    sdk_chunk_manager_set_background_expansion(&g_sdk.chunk_mgr, false);
    sdk_chunk_manager_set_grid_size(&g_sdk.chunk_mgr, 17);
    g_sdk.chunks_initialized = true;
    sdk_renderer_set_chunk_manager(&g_sdk.chunk_mgr);

    if (kind == SDK_SESSION_KIND_ANIMATION_EDITOR) {
        SdkChunk* preview_chunk = create_editor_chunk(editor_preview_chunk_cx(), 0);
        if (!preview_chunk) return 0;
        fill_editor_floor(preview_chunk, g_editor_session.base_floor_y);
        load_animation_frame_bytes(character->asset_id, animation->asset_id, 0, voxels, sizeof(voxels));
        stamp_editor_voxels_into_chunk(preview_chunk, voxels);
        sdk_chunk_mark_all_dirty(preview_chunk);
        total_editor_chunks++;
    }

    for (frame = 0; frame < frame_count; ++frame) {
        SdkChunk* chunk = create_editor_chunk(editor_frame_chunk_cx(frame), 0);
        if (!chunk) return 0;
        fill_editor_floor(chunk, g_editor_session.base_floor_y);
        if (kind == SDK_SESSION_KIND_CHARACTER_EDITOR) {
            load_character_model_bytes(character->asset_id, voxels, sizeof(voxels));
        } else {
            load_animation_frame_bytes(character->asset_id, animation->asset_id, frame, voxels, sizeof(voxels));
        }
        stamp_editor_voxels_into_chunk(chunk, voxels);
        sdk_chunk_mark_all_dirty(chunk);
        total_editor_chunks++;
    }

    rebuild_loaded_dirty_chunks_sync(total_editor_chunks);

    center_x = (float)(g_editor_session.build_local_x + (SDK_CHARACTER_MODEL_WIDTH / 2)) + 0.5f;
    center_z = (float)(g_editor_session.build_local_z + (SDK_CHARACTER_MODEL_DEPTH / 2)) + 0.5f;
    g_cam_rotation_initialized = true;
    g_cam_yaw = 0.0f;
    g_cam_pitch = -0.28f;
    teleport_player_to(center_x, (float)g_editor_session.base_floor_y + 10.0f, center_z - 20.0f,
                       &cam_x, &cam_y, &cam_z, NULL, NULL, NULL);
    sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
    sdk_renderer_set_camera_target(center_x, (float)g_editor_session.base_floor_y + 8.0f, center_z);

    g_sdk.world_seed = 0u;
    g_sdk.world_save_id[0] = '\0';
    g_sdk.world_save_name[0] = '\0';
    g_sdk.world_session_active = true;
    g_frontend_view = SDK_START_MENU_VIEW_MAIN;
    g_frontend_refresh_pending = true;
    frontend_reset_nav_state();
    return 1;
}

int start_character_editor_session(const SdkCharacterAssetMeta* character)
{
    return start_editor_session_common(SDK_SESSION_KIND_CHARACTER_EDITOR, character, NULL);
}

int start_animation_editor_session(const SdkCharacterAssetMeta* character,
                                   const SdkAnimationAssetMeta* animation)
{
    return start_editor_session_common(SDK_SESSION_KIND_ANIMATION_EDITOR, character, animation);
}

void tick_editor_session(void)
{
    bool play_down;
    bool prev_down;
    bool next_down;
    float cam_x;
    float cam_y;
    float cam_z;

    if (!editor_session_active()) return;
    if (g_session_kind != SDK_SESSION_KIND_ANIMATION_EDITOR &&
        g_session_kind != SDK_SESSION_KIND_PARTICLE_EDITOR) {
        return;
    }

    play_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_EDITOR_TOGGLE_PLAYBACK) ||
                sdk_input_raw_key_down(VK_F9);
    prev_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_EDITOR_PREV_FRAME) ||
                sdk_input_raw_key_down(VK_PRIOR);
    next_down = sdk_input_action_down(&g_input_settings, SDK_INPUT_ACTION_EDITOR_NEXT_FRAME) ||
                sdk_input_raw_key_down(VK_NEXT);

    if (play_down && !g_editor_play_was_down) {
        g_editor_session.playback_enabled = !g_editor_session.playback_enabled;
        g_editor_session.playback_counter = 0;
    }
    if (!g_editor_session.playback_enabled) {
        if (prev_down && !g_editor_prev_was_down && g_editor_session.current_frame > 0) {
            g_editor_session.current_frame--;
        }
        if (next_down && !g_editor_next_was_down &&
            g_editor_session.current_frame + 1 < g_editor_session.frame_count) {
            g_editor_session.current_frame++;
        }
        if (g_session_kind == SDK_SESSION_KIND_ANIMATION_EDITOR) {
            sdk_renderer_get_camera_pos(&cam_x, &cam_y, &cam_z);
            (void)cam_y;
            (void)cam_z;
            {
                int cam_chunk = sdk_world_to_chunk_x((int)floorf(cam_x));
                if (cam_chunk >= 0 && cam_chunk < g_editor_session.frame_count) {
                    g_editor_session.current_frame = cam_chunk;
                }
            }
        } else {
            sdk_renderer_get_camera_pos(&cam_x, &cam_y, &cam_z);
            {
                int local_x = (int)floorf(cam_x);
                (void)cam_y;
                (void)cam_z;
                if (sdk_world_to_chunk_x(local_x) == 0) {
                    int slice = api_clampi(sdk_world_to_local_x(local_x, 0) / g_editor_session.slice_width,
                                           0, g_editor_session.frame_count - 1);
                    g_editor_session.current_frame = slice;
                }
            }
        }
    } else {
        g_editor_session.playback_counter++;
        if (g_editor_session.playback_counter >= 10) {
            g_editor_session.playback_counter = 0;
            g_editor_session.current_frame =
                (g_editor_session.current_frame + 1) % g_editor_session.frame_count;
        }
    }

    if (g_session_kind == SDK_SESSION_KIND_ANIMATION_EDITOR) {
        g_editor_session.preview_counter++;
        if (g_editor_session.preview_counter >= SDK_EDITOR_PREVIEW_TICK_FRAMES) {
            sync_animation_preview_chunk(true);
        } else {
            sync_animation_preview_chunk(false);
        }
    }

    g_editor_play_was_down = play_down;
    g_editor_prev_was_down = prev_down;
    g_editor_next_was_down = next_down;
}

void save_editor_session_assets(void)
{
    uint8_t voxels[SDK_EDITOR_MAX_CAPTURE_VOXELS];

    if (!editor_session_active()) return;
    if (g_session_kind == SDK_SESSION_KIND_CHARACTER_EDITOR) {
        SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, 0, 0);
        int character_index;
        size_t voxel_count = (size_t)g_editor_session.build_width *
                             (size_t)g_editor_session.build_depth *
                             (size_t)g_editor_session.build_height;

        capture_editor_chunk_voxels(chunk, voxels);
        if (!editor_voxel_buffer_has_blocks(voxels, voxel_count)) {
            character_index = find_character_asset_index_by_id_local(g_editor_session.character_id);
            if (character_index >= 0 && g_character_assets[character_index].animation_count <= 0) {
                delete_character_asset(character_index);
            }
            return;
        }
        save_character_model_bytes(g_editor_session.character_id, voxels, voxel_count);
    } else if (g_session_kind == SDK_SESSION_KIND_ANIMATION_EDITOR) {
        int frame;
        int any_blocks = 0;
        size_t voxel_count = (size_t)g_editor_session.build_width *
                             (size_t)g_editor_session.build_depth *
                             (size_t)g_editor_session.build_height;

        for (frame = 0; frame < g_editor_session.frame_count; ++frame) {
            SdkChunk* chunk = sdk_chunk_manager_get_chunk(&g_sdk.chunk_mgr, editor_frame_chunk_cx(frame), 0);
            capture_editor_chunk_voxels(chunk, voxels);
            if (editor_voxel_buffer_has_blocks(voxels, voxel_count)) {
                any_blocks = 1;
            }
            save_animation_frame_bytes(g_editor_session.character_id, g_editor_session.animation_id,
                                       frame, voxels, voxel_count);
        }

        if (!any_blocks) {
            /* TODO: delete_animation_asset not implemented */
            /*
            int character_index = find_character_asset_index_by_id_local(g_editor_session.character_id);
            int animation_index = find_animation_asset_index_by_id_local(g_editor_session.animation_id);
            if (character_index >= 0 && animation_index >= 0) {
                delete_animation_asset(character_index, animation_index);
            }
            */
        }
    }
}

/* ============================================================================
 * Dual-Chunk Asset Editor Common (Block, Item, Particle Effect)
 * ============================================================================ */

static int start_dual_chunk_asset_editor_common(SdkSessionKind kind,
                                                const char* asset_id,
                                                const char* display_name)
{
    uint8_t model_voxels[SDK_PARTICLE_EFFECT_TIMELINE_VOXELS];
    uint8_t icon_voxels[SDK_ASSET_ICON_VOXELS];
    SdkChunk* model_chunk;
    SdkChunk* icon_chunk;
    int total_editor_chunks = 0;
    float model_center_x;
    float icon_center_x;
    float center_x;
    float center_z;
    float cam_x;
    float cam_y;
    float cam_z;

    if (!asset_id || !asset_id[0]) return 0;
    if (g_sdk.world_session_active) {
        shutdown_world_session(true);
    }

    memset(&g_editor_session, 0, sizeof(g_editor_session));
    g_session_kind = kind;
    g_editor_session.active = true;
    g_editor_session.kind = kind;
    strcpy_s(g_editor_session.character_id, sizeof(g_editor_session.character_id), asset_id);
    strcpy_s(g_editor_session.character_name, sizeof(g_editor_session.character_name),
             display_name ? display_name : asset_id);
    g_editor_session.frame_count = (kind == SDK_SESSION_KIND_PARTICLE_EDITOR)
        ? SDK_PARTICLE_EFFECT_SLICE_COUNT
        : 1;
    g_editor_session.current_frame = 0;
    g_editor_session.preview_frame = 0;
    g_editor_session.frame_chunk_count = 1;
    g_editor_session.preview_counter = 0;
    g_editor_session.playback_enabled = false;
    g_editor_session.base_floor_y = 128;
    g_editor_session.build_chunk_min_cx = 0;
    g_editor_session.build_chunk_min_cz = 0;
    g_editor_session.build_chunk_span_x = 1;
    g_editor_session.build_chunk_span_z = 1;
    g_editor_session.icon_chunk_cx = 1;
    g_editor_session.icon_local_x = (CHUNK_WIDTH - SDK_ASSET_ICON_DIM) / 2;
    g_editor_session.icon_local_z = (CHUNK_DEPTH - SDK_ASSET_ICON_DIM) / 2;
    g_editor_session.icon_min_y = g_editor_session.base_floor_y + 1;
    g_editor_session.icon_width = SDK_ASSET_ICON_DIM;
    g_editor_session.icon_depth = SDK_ASSET_ICON_DIM;
    if (kind == SDK_SESSION_KIND_PARTICLE_EDITOR) {
        g_editor_session.build_local_x = 0;
        g_editor_session.build_local_z = (CHUNK_DEPTH - SDK_PARTICLE_EFFECT_DEPTH) / 2;
        g_editor_session.build_min_y = g_editor_session.base_floor_y + 1;
        g_editor_session.build_width = SDK_PARTICLE_EFFECT_SLICE_COUNT * SDK_PARTICLE_EFFECT_SLICE_WIDTH;
        g_editor_session.build_depth = SDK_PARTICLE_EFFECT_DEPTH;
        g_editor_session.build_height = SDK_PARTICLE_EFFECT_HEIGHT;
        g_editor_session.slice_count = SDK_PARTICLE_EFFECT_SLICE_COUNT;
        g_editor_session.slice_width = SDK_PARTICLE_EFFECT_SLICE_WIDTH;
        load_particle_effect_timeline_bytes(asset_id, model_voxels, SDK_PARTICLE_EFFECT_TIMELINE_VOXELS);
        load_particle_effect_icon_bytes(asset_id, icon_voxels, SDK_ASSET_ICON_VOXELS);
    } else {
        g_editor_session.build_local_x = (CHUNK_WIDTH - SDK_BLOCK_ITEM_MODEL_WIDTH) / 2;
        g_editor_session.build_local_z = (CHUNK_DEPTH - SDK_BLOCK_ITEM_MODEL_DEPTH) / 2;
        g_editor_session.build_min_y = g_editor_session.base_floor_y + 1;
        g_editor_session.build_width = SDK_BLOCK_ITEM_MODEL_WIDTH;
        g_editor_session.build_depth = SDK_BLOCK_ITEM_MODEL_DEPTH;
        g_editor_session.build_height = SDK_BLOCK_ITEM_MODEL_HEIGHT;
        if (kind == SDK_SESSION_KIND_BLOCK_EDITOR) {
            load_block_model_bytes(asset_id, model_voxels, SDK_BLOCK_ITEM_MODEL_VOXELS);
            load_block_icon_bytes(asset_id, icon_voxels, SDK_ASSET_ICON_VOXELS);
        } else {
            load_item_model_bytes(asset_id, model_voxels, SDK_BLOCK_ITEM_MODEL_VOXELS);
            load_item_icon_bytes(asset_id, icon_voxels, SDK_ASSET_ICON_VOXELS);
        }
    }
    g_editor_play_was_down = false;
    g_editor_prev_was_down = false;
    g_editor_next_was_down = false;

    reset_session_runtime_state();
    skills_reset_progression();
    sdk_entity_init(&g_sdk.entities);
    station_close_ui();
    command_close();
    g_player_dead = false;
    g_player_health = PLAYER_MAX_HEALTH;
    g_player_hunger = PLAYER_MAX_HUNGER;
    g_test_flight_enabled = true;
    editor_prefill_hotbar();

    sdk_chunk_manager_init(&g_sdk.chunk_mgr);
    sdk_chunk_manager_set_background_expansion(&g_sdk.chunk_mgr, false);
    sdk_chunk_manager_set_grid_size(&g_sdk.chunk_mgr, 17);
    g_sdk.chunks_initialized = true;
    sdk_renderer_set_chunk_manager(&g_sdk.chunk_mgr);

    model_chunk = create_editor_chunk(0, 0);
    if (!model_chunk) return 0;
    fill_editor_floor(model_chunk, g_editor_session.base_floor_y);
    stamp_editor_voxels_into_chunk(model_chunk, model_voxels);
    sdk_chunk_mark_all_dirty(model_chunk);
    total_editor_chunks++;

    icon_chunk = create_editor_chunk(g_editor_session.icon_chunk_cx, 0);
    if (!icon_chunk) return 0;
    fill_editor_floor(icon_chunk, g_editor_session.base_floor_y);
    stamp_editor_icon_plane(icon_chunk, icon_voxels);
    sdk_chunk_mark_all_dirty(icon_chunk);
    total_editor_chunks++;

    rebuild_loaded_dirty_chunks_sync(total_editor_chunks);

    model_center_x = (float)(g_editor_session.build_local_x + (g_editor_session.build_width / 2)) + 0.5f;
    icon_center_x = (float)(g_editor_session.icon_chunk_cx * CHUNK_WIDTH +
                            g_editor_session.icon_local_x + (g_editor_session.icon_width / 2)) + 0.5f;
    center_x = (model_center_x + icon_center_x) * 0.5f;
    center_z = (float)(g_editor_session.build_local_z + (g_editor_session.build_depth / 2)) + 0.5f;
    g_cam_rotation_initialized = true;
    g_cam_yaw = 0.0f;
    g_cam_pitch = -0.28f;
    teleport_player_to(center_x, (float)g_editor_session.base_floor_y + 12.0f, center_z - 24.0f,
                       &cam_x, &cam_y, &cam_z, NULL, NULL, NULL);
    sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
    sdk_renderer_set_camera_target(center_x, (float)g_editor_session.base_floor_y + 8.0f, center_z);

    g_sdk.world_seed = 0u;
    g_sdk.world_save_id[0] = '\0';
    g_sdk.world_save_name[0] = '\0';
    g_sdk.world_session_active = true;
    g_frontend_view = SDK_START_MENU_VIEW_MAIN;
    g_frontend_refresh_pending = true;
    frontend_reset_nav_state();
    return 1;
}

int start_block_editor_session(const SdkBlockAssetMeta* block_asset)
{
    if (!block_asset) return 0;
    return start_dual_chunk_asset_editor_common(SDK_SESSION_KIND_BLOCK_EDITOR,
                                                block_asset->asset_id,
                                                block_asset->display_name);
}

int start_item_editor_session(const SdkItemAssetMeta* item_asset)
{
    if (!item_asset) return 0;
    return start_dual_chunk_asset_editor_common(SDK_SESSION_KIND_ITEM_EDITOR,
                                                item_asset->asset_id,
                                                item_asset->display_name);
}

int start_particle_effect_editor_session(const SdkParticleEffectAssetMeta* particle_effect_asset)
{
    if (!particle_effect_asset) return 0;
    return start_dual_chunk_asset_editor_common(SDK_SESSION_KIND_PARTICLE_EDITOR,
                                                particle_effect_asset->asset_id,
                                                particle_effect_asset->display_name);
}

