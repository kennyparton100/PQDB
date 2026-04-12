/* ============================================================================
 * Prop Editor Session
 * ============================================================================ */

int start_prop_editor_session(const SdkPropAssetMeta* prop_asset)
{
    uint8_t* chunk_voxels;
    SdkWorldCellCode* chunk_cells;
    size_t voxel_count;
    size_t cell_count;
    int chunk_x;
    int chunk_z;
    int total_editor_chunks = 0;
    float center_x;
    float center_z;
    float cam_x;
    float cam_y;
    float cam_z;
    float camera_distance;

    if (!prop_asset) return 0;
    if (g_sdk.world_session_active) {
        shutdown_world_session(true);
    }

    memset(&g_editor_session, 0, sizeof(g_editor_session));
    g_session_kind = SDK_SESSION_KIND_PROP_EDITOR;
    g_editor_session.active = true;
    g_editor_session.kind = SDK_SESSION_KIND_PROP_EDITOR;
    strcpy_s(g_editor_session.character_id, sizeof(g_editor_session.character_id), prop_asset->asset_id);
    strcpy_s(g_editor_session.character_name, sizeof(g_editor_session.character_name), prop_asset->display_name);
    g_editor_session.frame_count = 1;
    g_editor_session.current_frame = 0;
    g_editor_session.preview_frame = 0;
    g_editor_session.frame_chunk_count = 1;
    g_editor_session.preview_counter = 0;
    g_editor_session.playback_enabled = false;
    g_editor_session.base_floor_y = 128;
    g_editor_session.build_chunk_min_cx = -(SDK_PROP_EDITOR_CHUNK_SPAN / 2);
    g_editor_session.build_chunk_min_cz = -(SDK_PROP_EDITOR_CHUNK_SPAN / 2);
    g_editor_session.build_chunk_span_x = SDK_PROP_EDITOR_CHUNK_SPAN;
    g_editor_session.build_chunk_span_z = SDK_PROP_EDITOR_CHUNK_SPAN;
    g_editor_session.build_local_x = 0;
    g_editor_session.build_local_z = 0;
    g_editor_session.build_min_y = g_editor_session.base_floor_y + 1;
    g_editor_session.build_width = CHUNK_WIDTH;
    g_editor_session.build_depth = CHUNK_DEPTH;
    g_editor_session.build_height = CHUNK_HEIGHT - g_editor_session.build_min_y;
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

    voxel_count = (size_t)CHUNK_WIDTH *
                  (size_t)CHUNK_DEPTH *
                  (size_t)g_editor_session.build_height;
    cell_count = voxel_count;
    chunk_voxels = (uint8_t*)malloc(voxel_count);
    chunk_cells = (SdkWorldCellCode*)malloc(cell_count * sizeof(SdkWorldCellCode));
    if (!chunk_voxels || !chunk_cells) {
        free(chunk_voxels);
        free(chunk_cells);
        return 0;
    }

    for (chunk_z = 0; chunk_z < g_editor_session.build_chunk_span_z; ++chunk_z) {
        for (chunk_x = 0; chunk_x < g_editor_session.build_chunk_span_x; ++chunk_x) {
            int world_cx = g_editor_session.build_chunk_min_cx + chunk_x;
            int world_cz = g_editor_session.build_chunk_min_cz + chunk_z;
            SdkChunk* chunk = create_editor_chunk(world_cx, world_cz);

            if (!chunk) {
                free(chunk_voxels);
                free(chunk_cells);
                return 0;
            }

            fill_editor_floor(chunk, g_editor_session.base_floor_y);
            load_prop_chunk_bytes(prop_asset->asset_id, chunk_x, chunk_z, chunk_voxels, voxel_count);
            stamp_editor_voxels_into_chunk_volume(chunk, chunk_voxels,
                                                  0, 0,
                                                  g_editor_session.build_min_y,
                                                  CHUNK_WIDTH, CHUNK_DEPTH,
                                                  g_editor_session.build_height);
            sdk_chunk_mark_all_dirty(chunk);
            total_editor_chunks++;
        }
    }

    free(chunk_voxels);
    free(chunk_cells);
    rebuild_loaded_dirty_chunks_sync(total_editor_chunks);

    center_x = 0.5f;
    center_z = 0.5f;
    camera_distance = (float)(SUPERCHUNK_BLOCKS / 3);
    g_cam_rotation_initialized = true;
    g_cam_yaw = 0.0f;
    g_cam_pitch = -0.44f;
    teleport_player_to(center_x, (float)g_editor_session.base_floor_y + 176.0f, center_z - camera_distance,
                       &cam_x, &cam_y, &cam_z, NULL, NULL, NULL);
    sdk_renderer_set_camera_pos(cam_x, cam_y, cam_z);
    sdk_renderer_set_camera_target(center_x, (float)g_editor_session.base_floor_y + 32.0f, center_z);

    g_sdk.world_seed = 0u;
    g_sdk.world_save_id[0] = '\0';
    g_sdk.world_save_name[0] = '\0';
    g_sdk.world_session_active = true;
    g_frontend_view = SDK_START_MENU_VIEW_MAIN;
    g_frontend_refresh_pending = true;
    frontend_reset_nav_state();
    return 1;
}
