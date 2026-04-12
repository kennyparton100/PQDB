/* ============================================================================
 * Global State - Generation Summary
 * ============================================================================ */

static WorldGenStageStats g_gen_stage_stats[MAX_GEN_STAGES];
static int g_gen_stage_count = 0;

char g_gen_summary_lines[MAX_GEN_STAGES][128];
int g_gen_summary_line_count = 0;
static int g_gen_summary_active = 0;

/* ============================================================================
 * Generation Stage Tracking
 * ============================================================================ */

void gen_stage_begin(const char* name)
{
    const char* stage_name = (name && name[0]) ? name : "<unnamed>";

    if (g_gen_stage_count < MAX_GEN_STAGES) {
        char dbg[256];
        int idx = g_gen_stage_count;

        g_gen_stage_stats[idx].name = stage_name;
        g_gen_stage_stats[idx].start_ms = GetTickCount64();
        g_gen_stage_stats[idx].end_ms = 0;
        g_gen_stage_stats[idx].chunks_processed = 0;
        g_gen_stage_stats[idx].data_size_kb = 0;
        g_gen_stage_count++;

        sprintf_s(dbg, sizeof(dbg),
                  "[GEN_STAGE] begin idx=%d/%d name=\"%s\"\n",
                  idx, MAX_GEN_STAGES, stage_name);
        OutputDebugStringA(dbg);
        sdk_load_trace_note("gen_stage_begin", stage_name);
    } else {
        char dbg[256];

        sprintf_s(dbg, sizeof(dbg),
                  "[GEN_STAGE] overflow name=\"%s\" max=%d\n",
                  stage_name, MAX_GEN_STAGES);
        OutputDebugStringA(dbg);
        sdk_load_trace_note("gen_stage_overflow", stage_name);
    }
}

void gen_stage_end(int chunks_processed, int data_size_kb)
{
    if (g_gen_stage_count > 0) {
        char dbg[256];
        int idx = g_gen_stage_count - 1;
        ULONGLONG end_ms = GetTickCount64();
        ULONGLONG duration_ms;

        g_gen_stage_stats[idx].end_ms = end_ms;
        g_gen_stage_stats[idx].chunks_processed = chunks_processed;
        g_gen_stage_stats[idx].data_size_kb = data_size_kb;
        duration_ms = end_ms - g_gen_stage_stats[idx].start_ms;

        sprintf_s(dbg, sizeof(dbg),
                  "[GEN_STAGE] end idx=%d name=\"%s\" duration=%llums chunks=%d data_kb=%d\n",
                  idx,
                  g_gen_stage_stats[idx].name ? g_gen_stage_stats[idx].name : "<unnamed>",
                  duration_ms,
                  chunks_processed,
                  data_size_kb);
        OutputDebugStringA(dbg);
        sdk_load_trace_note("gen_stage_end",
                            g_gen_stage_stats[idx].name ? g_gen_stage_stats[idx].name : "<unnamed>");
    } else {
        char dbg[192];

        sprintf_s(dbg, sizeof(dbg),
                  "[GEN_STAGE] end called with no active stage chunks=%d data_kb=%d\n",
                  chunks_processed, data_size_kb);
        OutputDebugStringA(dbg);
        sdk_load_trace_note("gen_stage_end_without_begin", "no active stage");
    }
}

/* ============================================================================
 * World Generation Summary
 * ============================================================================ */

void set_world_generation_session_summary(char (*lines)[128], int count)
{
    for (int i = 0; i < count && i < MAX_GEN_STAGES; ++i) {
        strcpy_s(g_gen_summary_lines[i], sizeof(g_gen_summary_lines[0]), lines[i]);
    }
    g_gen_summary_line_count = count;
}

static void present_world_generation_summary(void)
{
    char summary_lines[MAX_GEN_STAGES][128];
    int line_count = 0;
    ULONGLONG total_ms = 0;

    /* Build summary lines */
    for (int i = 0; i < g_gen_stage_count; ++i) {
        const WorldGenStageStats* stage = &g_gen_stage_stats[i];
        ULONGLONG duration_ms = stage->end_ms - stage->start_ms;
        total_ms += duration_ms;

        if (stage->chunks_processed > 0) {
            sprintf_s(summary_lines[line_count], sizeof(summary_lines[0]),
                     "%-30s %5llums  %4d chunks",
                     stage->name, duration_ms, stage->chunks_processed);
        } else {
            sprintf_s(summary_lines[line_count], sizeof(summary_lines[0]),
                     "%-30s %5llums",
                     stage->name, duration_ms);
        }
        line_count++;
    }

    /* Add total line */
    sprintf_s(summary_lines[line_count], sizeof(summary_lines[0]),
             "%-30s %5llums (total)",
             "TOTAL", total_ms);
    line_count++;

    /* Display summary */
    set_world_generation_session_summary((char (*)[128])summary_lines, line_count);
    g_gen_summary_active = 0;
    frontend_reset_nav_state();
    present_start_menu_frame();
}

int world_generation_summary_active(void)
{
    return g_gen_summary_active;
}

void dismiss_world_generation_summary(void)
{
    g_gen_summary_active = 0;
    g_gen_summary_line_count = 0;
    g_gen_stage_count = 0;
}

/* ============================================================================
 * World Generation Session Overlay
 * ============================================================================ */

static int world_generation_session_overlay_active(void)
{
    return !g_sdk.world_session_active &&
           g_frontend_view == SDK_START_MENU_VIEW_WORLD_GENERATING &&
           g_world_generation_stage == 2;
}

static int world_generation_session_present_frame(void)
{
    if (g_sdk.window && !sdk_window_pump(g_sdk.window)) {
        if (!g_world_generation_cancel_requested) {
            g_world_generation_cancel_requested = true;
            sdk_load_trace_note("world_generation_session_cancelled",
                                "sdk_window_pump returned false during startup");
        }
        return 0;
    }
    present_start_menu_frame();
    return 1;
}

void world_generation_session_step(float session_progress, const char* status, int present)
{
    static float s_last_traced_progress = -1.0f;
    static char s_last_traced_status[256] = "";

    if (!world_generation_session_overlay_active()) return;
    set_world_generation_session_progress(session_progress, status);
    if (status &&
        (fabsf(session_progress - s_last_traced_progress) > 0.0001f ||
         strcmp(s_last_traced_status, status) != 0)) {
        s_last_traced_progress = session_progress;
        strncpy_s(s_last_traced_status, sizeof(s_last_traced_status), status, _TRUNCATE);
        sdk_load_trace_note_state("world_generation_session_step",
                                  g_frontend_view,
                                  g_sdk.world_session_active ? 1 : 0,
                                  g_world_generation_active ? 1 : 0,
                                  g_world_generation_stage,
                                  status);
    }
    if (present) {
        world_generation_session_present_frame();
    }
}
