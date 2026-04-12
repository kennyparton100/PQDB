/**
 * Returns spawn mode for new world (0=random, 1=center/classic, 2=safe)
 */
int current_new_world_spawn_mode(const SdkWorldSaveMeta* selected_world)
{
    if (selected_world && selected_world->spawn_mode >= 0 && selected_world->spawn_mode <= 2) {
        return selected_world->spawn_mode;
    }
    if (selected_world &&
        g_frontend_view == SDK_START_MENU_VIEW_WORLD_GENERATING &&
        g_world_generation_stage == 2 &&
        strcmp(selected_world->folder_id, g_world_generation_target.folder_id) == 0) {
        return api_clampi(g_world_create_spawn_type, 0, 2);
    }
    return 2;
}

/**
 * Returns render distance in chunks for selected world
 */
int current_world_render_distance_chunks(const SdkWorldSaveMeta* selected_world)
{
    if (selected_world && selected_world->render_distance_chunks > 0) {
        return clamp_render_distance_chunks(selected_world->render_distance_chunks);
    }
    return sdk_chunk_manager_radius_from_grid_size(g_graphics_settings.chunk_grid_size);
}

/* ============================================================================
 * Spawn Helper Functions
 * ============================================================================ */

uint32_t spawn_rng_next(uint32_t* state)
{
    uint32_t x = *state;
    if (x == 0u) x = 0xA341316Cu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

int spawn_rng_range(uint32_t* state, int min_value, int max_value)
{
    uint32_t span;
    if (max_value <= min_value) return min_value;
    span = (uint32_t)(max_value - min_value + 1);
    return min_value + (int)(spawn_rng_next(state) % span);
}

int spawn_abs_i(int value)
{
    return value < 0 ? -value : value;
}

int spawn_floor_div_i(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

int spawn_floor_mod_i(int value, int denom)
{
    int div = spawn_floor_div_i(value, denom);
    return value - div * denom;
}

int spawn_is_in_superchunk_wall_band(int wx, int wz)
{
    const int superchunk_blocks = 1024;
    const int avoid_margin = 96;
    int local_x = spawn_floor_mod_i(wx, superchunk_blocks);
    int local_z = spawn_floor_mod_i(wz, superchunk_blocks);
    return local_x < avoid_margin ||
           local_x >= superchunk_blocks - avoid_margin ||
           local_z < avoid_margin ||
           local_z >= superchunk_blocks - avoid_margin;
}

int estimate_spawn_relief(SdkWorldGen* wg, int wx, int wz, int center_height)
{
    static const int offsets[4][2] = {
        { SPAWN_RELIEF_SAMPLE_STEP, 0 },
        { -SPAWN_RELIEF_SAMPLE_STEP, 0 },
        { 0, SPAWN_RELIEF_SAMPLE_STEP },
        { 0, -SPAWN_RELIEF_SAMPLE_STEP }
    };
    int max_relief = 0;

    for (int i = 0; i < 4; ++i) {
        SdkTerrainColumnProfile neighbor;
        int diff;
        if (!sdk_worldgen_sample_column_ctx(wg, wx + offsets[i][0], wz + offsets[i][1], &neighbor)) {
            continue;
        }
        diff = spawn_abs_i((int)neighbor.surface_height - center_height);
        if (diff > max_relief) max_relief = diff;
    }

    return max_relief;
}

int score_spawn_candidate(SdkWorldGen* wg, int wx, int wz,
                          SdkTerrainColumnProfile* out_profile, int* out_relief)
{
    SdkTerrainColumnProfile profile;
    int relief;
    int score;

    if (!sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) {
        return INT_MIN;
    }

    if (spawn_is_in_superchunk_wall_band(wx, wz)) {
        return INT_MIN;
    }

    relief = estimate_spawn_relief(wg, wx, wz, (int)profile.surface_height);
    score = sdk_worldgen_score_spawn_candidate_profile(wg->desc.sea_level, &profile, relief);

    if (out_profile) *out_profile = profile;
    if (out_relief) *out_relief = relief;
    return score;
}


// ============================================================================
// SPAWN Functions 
// ============================================================================

static void commit_spawn_choice(const char* label,
                                int best_x,
                                int best_z,
                                const SdkTerrainColumnProfile* best_profile,
                                int best_relief,
                                int best_score)
{
    int top_y = (int)best_profile->surface_height + 2;
    g_spawn_x = (float)best_x + 0.5f;
    g_spawn_z = (float)best_z + 0.5f;
    g_spawn_y = (float)top_y + 3.0f;

    {
        char dbg[256];
        sprintf(dbg,
                "[SPAWN] %s spawn at (%d,%d) top=%d surface=%d water=%d province=%d moisture=%d relief=%d score=%d\n",
                label ? label : "Selected",
                best_x, best_z,
                top_y,
                (int)best_profile->surface_height, (int)best_profile->water_height,
                (int)best_profile->terrain_province, (int)best_profile->moisture_band,
                best_relief, best_score);
        OutputDebugStringA(dbg);
    }
}

static uint32_t init_spawn_rng(const SdkWorldGen* wg)
{
    uint64_t tick = GetTickCount64();
    return (uint32_t)(tick ^ (tick >> 32) ^ (uint64_t)wg->desc.seed ^ (uint64_t)GetCurrentProcessId());
}

static int sample_spawn_column_if_safe(SdkWorldGen* wg, int wx, int wz, SdkTerrainColumnProfile* out_profile)
{
    if (!wg || !out_profile) return 0;
    if (spawn_is_in_superchunk_wall_band(wx, wz)) return 0;
    return sdk_worldgen_sample_column_ctx(wg, wx, wz, out_profile);
}

static void choose_center_spawn(SdkWorldGen* wg)
{
    static const int offsets[][2] = {
        { 160, 0 }, { -160, 0 }, { 0, 160 }, { 0, -160 },
        { 224, 0 }, { -224, 0 }, { 0, 224 }, { 0, -224 },
        { 160, 160 }, { -160, 160 }, { 160, -160 }, { -160, -160 },
        { 224, 160 }, { -224, 160 }, { 224, -160 }, { -224, -160 }
    };
    int best_score = INT_MIN;
    int best_relief = 0;
    int best_x = 0;
    int best_z = 0;
    SdkTerrainColumnProfile best_profile;
    bool found = false;

    memset(&best_profile, 0, sizeof(best_profile));

    for (int i = 0; i < (int)(sizeof(offsets) / sizeof(offsets[0])); ++i) {
        int relief = 0;
        SdkTerrainColumnProfile profile;
        int score = score_spawn_candidate(wg, offsets[i][0], offsets[i][1], &profile, &relief);

        if (score > best_score) {
            best_score = score;
            best_relief = relief;
            best_profile = profile;
            best_x = offsets[i][0];
            best_z = offsets[i][1];
            found = true;
        }
    }

    if (!found) {
        if (!sample_spawn_column_if_safe(wg, -160, -160, &best_profile)) {
            memset(&best_profile, 0, sizeof(best_profile));
            best_profile.surface_height = (int16_t)api_clampi(wg->desc.sea_level, 0, CHUNK_HEIGHT - 4);
            best_profile.water_height = best_profile.surface_height;
        }
        best_x = -160;
        best_z = -160;
    }

    commit_spawn_choice("Center", best_x, best_z, &best_profile, best_relief, best_score);
}

static void choose_random_spawn_fast(SdkWorldGen* wg)
{
    const int random_spawn_radius = 320;
    const int search_candidates = 16;
    uint32_t rng_state = init_spawn_rng(wg);
    SdkTerrainColumnProfile profile;

    memset(&profile, 0, sizeof(profile));

    for (int i = 0; i < search_candidates; ++i) {
        int wx = spawn_rng_range(&rng_state, -random_spawn_radius, random_spawn_radius);
        int wz = spawn_rng_range(&rng_state, -random_spawn_radius, random_spawn_radius);
        if (sample_spawn_column_if_safe(wg, wx, wz, &profile)) {
            commit_spawn_choice("Random", wx, wz, &profile, 0, 0);
            return;
        }
    }

    if (sample_spawn_column_if_safe(wg, -160, -160, &profile)) {
        commit_spawn_choice("Random", -160, -160, &profile, 0, 0);
        return;
    }

    memset(&profile, 0, sizeof(profile));
    profile.surface_height = (int16_t)api_clampi(wg->desc.sea_level, 0, CHUNK_HEIGHT - 4);
    profile.water_height = profile.surface_height;
    commit_spawn_choice("Random", -160, -160, &profile, 0, 0);
}

static void choose_safe_spawn(SdkWorldGen* wg)
{
    uint32_t rng_state;
    int best_score = INT_MIN;
    int best_relief = 0;
    int best_x = 0;
    int best_z = 0;
    SdkTerrainColumnProfile best_profile;
    bool found = false;

    memset(&best_profile, 0, sizeof(best_profile));

    rng_state = init_spawn_rng(wg);

    for (int i = 0; i < SPAWN_SEARCH_CANDIDATES; ++i) {
        int wx = spawn_rng_range(&rng_state, -SPAWN_SEARCH_MAX_RADIUS, SPAWN_SEARCH_MAX_RADIUS);
        int wz = spawn_rng_range(&rng_state, -SPAWN_SEARCH_MAX_RADIUS, SPAWN_SEARCH_MAX_RADIUS);
        SdkTerrainColumnProfile profile;
        int relief = 0;
        int score = score_spawn_candidate(wg, wx, wz, &profile, &relief);

        if (score > best_score) {
            best_score = score;
            best_relief = relief;
            best_profile = profile;
            best_x = wx;
            best_z = wz;
            found = true;
        }

        if (((i + 1) % 8) == 0 || (i + 1) == SPAWN_SEARCH_CANDIDATES) {
            char status[64];
            float t = (float)(i + 1) / (float)SPAWN_SEARCH_CANDIDATES;
            sprintf_s(status, sizeof(status), "Choosing safe spawn %d/%d", i + 1, SPAWN_SEARCH_CANDIDATES);
            world_generation_session_step(0.54f + 0.08f * t, status, 1);
        }
    }

    if (!found) {
        choose_random_spawn_fast(wg);
        return;
    }

    commit_spawn_choice("Safe", best_x, best_z, &best_profile, best_relief, best_score);
}
