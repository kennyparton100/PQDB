#include "sdk_session_headless.h"

#include "../../Internal/sdk_load_trace.h"
#include "../../../World/Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../../../World/Config/sdk_world_config.h"
#include "../../../World/Chunks/ChunkStreamer/sdk_chunk_streamer.h"
#include "../../../World/Worldgen/sdk_worldgen.h"
#include "../../../World/Worldgen/Column/sdk_worldgen_column_internal.h"
#include "../../../World/Worldgen/SharedCache/sdk_worldgen_shared_cache.h"
#include "../../../World/Persistence/sdk_persistence.h"
#include "../../../World/Chunks/ChunkCompression/sdk_chunk_codec.h"
#include "../../../World/Simulation/sdk_simulation.h"
#include "../../../World/ConstructionCells/sdk_construction_cells.h"
#include "../../../World/Settlements/sdk_settlement.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int headless_abs_i(int value)
{
    /* Returns absolute value of integer */
    return value < 0 ? -value : value;
}

static uint32_t headless_rng_next(uint32_t* state)
{
    /* Xorshift RNG - advances state and returns next value */
    uint32_t x = *state;
    if (x == 0u) x = 0xA341316Cu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int headless_rng_range(uint32_t* state, int min_value, int max_value)
{
    /* Returns random int in range [min, max] using headless RNG */
    uint32_t span;

    if (max_value <= min_value) return min_value;
    span = (uint32_t)(max_value - min_value + 1);
    return min_value + (int)(headless_rng_next(state) % span);
}

static int headless_spawn_is_in_wall_band(int wx, int wz)
{
    /* Returns true if world position is in superchunk wall band */
    int superchunk_blocks;
    int avoid_margin;
    int local_x;
    int local_z;

    if (!sdk_superchunk_get_enabled() || !sdk_superchunk_get_walls_enabled()) return 0;
    superchunk_blocks = sdk_superchunk_get_block_span();
    if (superchunk_blocks <= 0) return 0;
    avoid_margin = 96;
    local_x = sdk_superchunk_floor_mod_i(wx, superchunk_blocks);
    local_z = sdk_superchunk_floor_mod_i(wz, superchunk_blocks);
    return local_x < avoid_margin ||
           local_x >= superchunk_blocks - avoid_margin ||
           local_z < avoid_margin ||
           local_z >= superchunk_blocks - avoid_margin;
}

static int headless_estimate_spawn_relief(SdkWorldGen* wg, int wx, int wz, int center_height)
{
    /* Estimates terrain relief around spawn point from 4 neighbor samples */
    static const int offsets[4][2] = {
        { 8, 0 },
        { -8, 0 },
        { 0, 8 },
        { 0, -8 }
    };
    int max_relief = 0;
    int i;

    for (i = 0; i < 4; ++i) {
        SdkTerrainColumnProfile neighbor;
        int diff;

        if (!sdk_worldgen_sample_column_ctx(wg, wx + offsets[i][0], wz + offsets[i][1], &neighbor)) {
            continue;
        }
        diff = headless_abs_i((int)neighbor.surface_height - center_height);
        if (diff > max_relief) max_relief = diff;
    }

    return max_relief;
}

static int headless_score_spawn_candidate(SdkWorldGen* wg,
                                          int wx,
                                          int wz,
                                          SdkTerrainColumnProfile* out_profile,
                                          int* out_relief)
{
    /* Scores spawn candidate location, returns INT_MIN if invalid */
    SdkTerrainColumnProfile profile;
    int relief;
    int score;

    if (!sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) {
        return INT_MIN;
    }
    if (headless_spawn_is_in_wall_band(wx, wz)) {
        return INT_MIN;
    }

    relief = headless_estimate_spawn_relief(wg, wx, wz, (int)profile.surface_height);
    score = sdk_worldgen_score_spawn_candidate_profile(wg->desc.sea_level, &profile, relief);

    if (out_profile) *out_profile = profile;
    if (out_relief) *out_relief = relief;
    return score;
}

static void headless_commit_spawn(float out_spawn[3],
                                  int best_x,
                                  int best_z,
                                  const SdkTerrainColumnProfile* best_profile)
{
    /* Commits final spawn position from best candidate */
    int top_y = (int)best_profile->surface_height + 2;

    out_spawn[0] = (float)best_x + 0.5f;
    out_spawn[1] = (float)top_y + 3.0f;
    out_spawn[2] = (float)best_z + 0.5f;
}

static void headless_choose_center_spawn(SdkWorldGen* wg, float out_spawn[3])
{
    /* Chooses spawn near center (0,0) from predefined offset candidates */
    static const int offsets[][2] = {
        { 160, 0 }, { -160, 0 }, { 0, 160 }, { 0, -160 },
        { 224, 0 }, { -224, 0 }, { 0, 224 }, { 0, -224 },
        { 160, 160 }, { -160, 160 }, { 160, -160 }, { -160, -160 }
    };
    int best_score = INT_MIN;
    int best_x = -160;
    int best_z = -160;
    SdkTerrainColumnProfile best_profile;
    int found = 0;
    int i;

    memset(&best_profile, 0, sizeof(best_profile));
    best_profile.surface_height = (int16_t)wg->desc.sea_level;
    best_profile.water_height = (int16_t)wg->desc.sea_level;

    for (i = 0; i < (int)(sizeof(offsets) / sizeof(offsets[0])); ++i) {
        SdkTerrainColumnProfile profile;
        int relief = 0;
        int score = headless_score_spawn_candidate(wg, offsets[i][0], offsets[i][1], &profile, &relief);
        if (score > best_score) {
            best_score = score;
            best_profile = profile;
            best_x = offsets[i][0];
            best_z = offsets[i][1];
            found = 1;
        }
    }

    if (!found) {
        sdk_worldgen_sample_column_ctx(wg, best_x, best_z, &best_profile);
    }
    headless_commit_spawn(out_spawn, best_x, best_z, &best_profile);
}

static void headless_choose_random_spawn(SdkWorldGen* wg, float out_spawn[3])
{
    /* Chooses random spawn within radius, falls back to center spawn */
    const int random_spawn_radius = 320;
    uint32_t rng_state;
    int i;

    rng_state = (uint32_t)(wg->desc.seed ^ (uint32_t)GetTickCount64());

    for (i = 0; i < 16; ++i) {
        int wx = headless_rng_range(&rng_state, -random_spawn_radius, random_spawn_radius);
        int wz = headless_rng_range(&rng_state, -random_spawn_radius, random_spawn_radius);
        SdkTerrainColumnProfile profile;

        if (headless_spawn_is_in_wall_band(wx, wz)) continue;
        if (sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) {
            headless_commit_spawn(out_spawn, wx, wz, &profile);
            return;
        }
    }

    headless_choose_center_spawn(wg, out_spawn);
}

static void headless_choose_safe_spawn(SdkWorldGen* wg, float out_spawn[3])
{
    /* Chooses safe spawn by scoring 96 random candidate locations */
    uint32_t rng_state;
    int best_score = INT_MIN;
    int best_x = -160;
    int best_z = -160;
    SdkTerrainColumnProfile best_profile;
    int found = 0;
    int i;

    memset(&best_profile, 0, sizeof(best_profile));
    best_profile.surface_height = (int16_t)wg->desc.sea_level;
    best_profile.water_height = (int16_t)wg->desc.sea_level;
    rng_state = (uint32_t)(wg->desc.seed ^ (uint32_t)(GetTickCount64() >> 8));

    for (i = 0; i < 96; ++i) {
        int wx = headless_rng_range(&rng_state, -16384, 16384);
        int wz = headless_rng_range(&rng_state, -16384, 16384);
        SdkTerrainColumnProfile profile;
        int relief = 0;
        int score = headless_score_spawn_candidate(wg, wx, wz, &profile, &relief);

        if (score > best_score) {
            best_score = score;
            best_profile = profile;
            best_x = wx;
            best_z = wz;
            found = 1;
        }
    }

    if (!found) {
        headless_choose_random_spawn(wg, out_spawn);
        return;
    }
    headless_commit_spawn(out_spawn, best_x, best_z, &best_profile);
}

static void headless_mark_chunk_gpu_ready(SdkChunk* chunk, int full_upload)
{
    if (!chunk) return;

    chunk->gpu_upload_mode = (uint8_t)(full_upload ? SDK_CHUNK_GPU_UPLOAD_FULL
                                                   : SDK_CHUNK_GPU_UPLOAD_FAR_ONLY);
    chunk->upload_pending = 0u;
    if (!chunk->empty && chunk->cpu_mesh_generation == 0u) {
        chunk->cpu_mesh_generation = 1u;
    }
    if (!chunk->empty && chunk->unified_vertex_buffer == NULL) {
        chunk->unified_vertex_buffer = (void*)chunk;
    }
    if (chunk->cpu_mesh_generation != 0u) {
        chunk->gpu_mesh_generation = chunk->cpu_mesh_generation;
    } else if (chunk->empty) {
        chunk->gpu_mesh_generation = 1u;
    }
}

static int headless_target_in_safety_set(const SdkChunkResidencyTarget* target,
                                         int cam_cx,
                                         int cam_cz,
                                         int safety_radius)
{
    int dx;
    int dz;

    if (!target) return 0;
    if ((SdkChunkResidencyRole)target->role != SDK_CHUNK_ROLE_PRIMARY) return 0;
    dx = headless_abs_i(target->cx - cam_cx);
    dz = headless_abs_i(target->cz - cam_cz);
    return dx <= safety_radius && dz <= safety_radius;
}

static void headless_collect_readiness(const SdkChunkManager* cm,
                                       const SdkChunkStreamer* streamer,
                                       int cam_cx,
                                       int cam_cz,
                                       int safety_radius,
                                       SdkStartupReadinessSnapshot* out_snapshot)
{
    int i;

    if (!out_snapshot) return;
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (!cm) return;

    out_snapshot->pending_jobs = streamer ? sdk_chunk_streamer_pending_jobs(streamer) : 0;
    out_snapshot->pending_results = streamer ? sdk_chunk_streamer_pending_results(streamer) : 0;

    for (i = 0; i < sdk_chunk_manager_desired_count(cm); ++i) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(cm, i);
        SdkChunkResidentSlot* slot;
        SdkChunk* chunk;

        if (!headless_target_in_safety_set(target, cam_cx, cam_cz, safety_radius)) continue;
        out_snapshot->desired_primary++;

        slot = sdk_chunk_manager_find_slot((SdkChunkManager*)cm, target->cx, target->cz);
        if (!slot || !slot->occupied || !slot->chunk.blocks) {
            out_snapshot->other_not_ready++;
            continue;
        }

        chunk = &slot->chunk;
        out_snapshot->resident_primary++;
        if (chunk->upload_pending) {
            out_snapshot->pending_uploads++;
            out_snapshot->upload_pending++;
        }
        if (sdk_chunk_has_full_upload_ready_mesh(chunk)) {
            out_snapshot->gpu_ready_primary++;
            continue;
        }
        if (!chunk->empty && chunk->cpu_mesh_generation == 0u) {
            out_snapshot->no_cpu_mesh++;
        } else if ((SdkChunkGpuUploadMode)chunk->gpu_upload_mode != SDK_CHUNK_GPU_UPLOAD_FULL) {
            out_snapshot->far_only_when_full_needed++;
        } else if (chunk->gpu_mesh_generation != chunk->cpu_mesh_generation) {
            out_snapshot->gpu_mesh_generation_stale++;
        } else if (chunk->upload_pending) {
            out_snapshot->upload_pending++;
        } else {
            out_snapshot->other_not_ready++;
        }
    }
}

static int headless_readiness_satisfied(const SdkStartupReadinessSnapshot* snapshot, int stop_at)
{
    if (!snapshot || snapshot->desired_primary <= 0) return 0;
    if (stop_at == SDK_SESSION_STOP_AT_RESIDENT) {
        return snapshot->resident_primary >= snapshot->desired_primary;
    }
    return snapshot->gpu_ready_primary >= snapshot->desired_primary;
}

static void headless_store_default_state(SdkPersistence* persistence,
                                         const float spawn[3],
                                         int chunk_grid_size)
{
    SdkPersistedState state;

    if (!persistence) return;
    memset(&state, 0, sizeof(state));
    state.position[0] = spawn[0];
    state.position[1] = spawn[1];
    state.position[2] = spawn[2];
    state.spawn[0] = spawn[0];
    state.spawn[1] = spawn[1];
    state.spawn[2] = spawn[2];
    state.cam_yaw = 0.0f;
    state.cam_pitch = -0.3f;
    state.health = 20;
    state.hunger = 20;
    state.world_time = 2000;
    state.hotbar_selected = 0;
    state.chunk_grid_size = chunk_grid_size;
    sdk_persistence_set_state(persistence, &state);
}

static void headless_probe_store_failure(const SdkChunk* chunk,
                                         SdkSessionStartResult* result)
{
    char* codec_name = NULL;
    char* payload_b64 = NULL;
    char* text = NULL;
    int payload_version = 0;
    int top_y = 0;

    if (!chunk || !result) return;

    if (!sdk_chunk_codec_encode_auto(chunk,
                                     &codec_name,
                                     &payload_b64,
                                     &payload_version,
                                     &top_y)) {
        result->persist_encode_auto_failures++;
    }
    free(codec_name);
    free(payload_b64);
    codec_name = NULL;
    payload_b64 = NULL;

    if (!sdk_chunk_codec_encode_with_method(chunk,
                                            SDK_CHUNK_CODEC_METHOD_CELL_RLE,
                                            &payload_b64,
                                            &payload_version,
                                            &top_y)) {
        result->persist_cell_rle_failures++;
    }
    free(payload_b64);

    text = sdk_simulation_encode_chunk_fluids(chunk);
    if (!text) {
        result->persist_fluids_failures++;
    }
    free(text);
    text = sdk_construction_encode_store(chunk);
    if (!text) {
        result->persist_construction_failures++;
    }
    free(text);
}

int sdk_session_start_headless(const SdkSessionStartRequest* request,
                               SdkSessionStartResult* out_result)
{
    SdkSessionStartResult local_result;
    SdkWorldCreateResult create_result;
    SdkWorldDesc requested_desc;
    SdkPersistence* persistence = NULL;
    SdkWorldGen* worldgen = NULL;
    SdkChunkManager* chunk_mgr = NULL;
    SdkChunkStreamer* streamer = NULL;
    SdkPersistedState persisted_state;
    int have_persisted_state = 0;
    int safety_radius;
    int max_iterations;
    int stop_at;
    int success = 0;
    int shutdown_streamer_started = 0;
    int shutdown_chunk_mgr_started = 0;
    int shutdown_worldgen_started = 0;
    int shutdown_persistence_started = 0;
    int shared_cache_started = 0;
    int create_if_missing;
    uint64_t started_ms = GetTickCount64();
    uint64_t stage_ms = 0u;

    if (!request) return 0;
    memset(&local_result, 0, sizeof(local_result));
    memset(&create_result, 0, sizeof(create_result));
    memset(&requested_desc, 0, sizeof(requested_desc));
    memset(&persisted_state, 0, sizeof(persisted_state));
    sdk_load_trace_reset("headless_session_start");
    sdk_load_trace_note("headless_session_enter", "sdk_session_start_headless");

    sdk_worldgen_shared_cache_init();
    shared_cache_started = 1;

    persistence = (SdkPersistence*)calloc(1, sizeof(*persistence));
    worldgen = (SdkWorldGen*)calloc(1, sizeof(*worldgen));
    chunk_mgr = (SdkChunkManager*)calloc(1, sizeof(*chunk_mgr));
    streamer = (SdkChunkStreamer*)calloc(1, sizeof(*streamer));
    if (!persistence || !worldgen || !chunk_mgr || !streamer) {
        strcpy_s(local_result.failure_reason, sizeof(local_result.failure_reason),
                 "headless session allocation failed");
        goto cleanup;
    }

    create_if_missing = request->create_if_missing ? 1 : 0;
    safety_radius = request->safety_radius > 0 ? request->safety_radius : 2;
    max_iterations = request->max_iterations > 0 ? request->max_iterations : 800;
    stop_at = (request->stop_at == SDK_SESSION_STOP_AT_RESIDENT)
        ? SDK_SESSION_STOP_AT_RESIDENT
        : SDK_SESSION_STOP_AT_GPU_READY;
    local_result.stop_at = stop_at;

    if (!sdk_world_target_resolve(request->world_id[0] ? request->world_id : NULL,
                                  request->world_dir[0] ? request->world_dir : NULL,
                                  &local_result.target)) {
        if (!create_if_missing) {
            strcpy_s(local_result.failure_reason, sizeof(local_result.failure_reason),
                     "could not resolve world target");
            goto cleanup;
        }
    }

    if ((!local_result.target.directory_exists || !local_result.target.meta_exists) && create_if_missing) {
        SdkWorldCreateRequest create_request = request->create_request;

        if (!create_request.output_dir[0] && request->world_dir[0]) {
            strncpy_s(create_request.output_dir, sizeof(create_request.output_dir),
                      request->world_dir, _TRUNCATE);
        }
        if (!create_request.folder_id[0] && request->world_id[0]) {
            strncpy_s(create_request.folder_id, sizeof(create_request.folder_id),
                      request->world_id, _TRUNCATE);
        }
        stage_ms = GetTickCount64();
        if (!sdk_world_create(&create_request, &create_result)) {
            strcpy_s(local_result.failure_reason, sizeof(local_result.failure_reason),
                     "failed to create requested world");
            goto cleanup;
        }
        local_result.world_create_ms = GetTickCount64() - stage_ms;
        local_result.created_world = 1;
        local_result.target = create_result.target;
        sdk_load_trace_bind_world(local_result.target.folder_id, local_result.target.world_dir);
        sdk_load_trace_note("headless_world_created", local_result.target.folder_id);
    }

    stage_ms = GetTickCount64();
    if (!sdk_world_target_load_meta(&local_result.target, &local_result.meta)) {
        strcpy_s(local_result.failure_reason, sizeof(local_result.failure_reason),
                 "failed to load normalized world metadata");
        goto cleanup;
    }
    local_result.meta_load_ms = GetTickCount64() - stage_ms;
    sdk_load_trace_bind_meta(&local_result.meta);
    sdk_load_trace_note("headless_meta_loaded", local_result.target.folder_id);
    sdk_debug_log_printf("[HEADLESS] begin world=%s stop_at=%s safety_radius=%d max_iterations=%d create_if_missing=%d\n",
                         local_result.target.folder_id,
                         stop_at == SDK_SESSION_STOP_AT_RESIDENT ? "resident" : "gpu_ready",
                         safety_radius,
                         max_iterations,
                         create_if_missing);
    sdk_debug_log_printf("[HEADLESS] target world_dir=%s save_path=%s\n",
                         local_result.target.world_dir,
                         local_result.target.save_path);
    sdk_debug_log_printf("[HEADLESS] meta seed=%u render_distance=%d spawn_mode=%d coordinate_system=%s(%u) settlements=%d construction_cells=%d superchunks=%d walls=%d wall_grid=%d offsets=(%d,%d)\n",
                         local_result.meta.seed,
                         local_result.meta.render_distance_chunks,
                         local_result.meta.spawn_mode,
                         sdk_world_coordinate_system_display_name(
                             (SdkWorldCoordinateSystem)local_result.meta.coordinate_system),
                         (unsigned)local_result.meta.coordinate_system,
                         local_result.meta.settlements_enabled ? 1 : 0,
                         local_result.meta.construction_cells_enabled ? 1 : 0,
                         local_result.meta.superchunks_enabled ? 1 : 0,
                         local_result.meta.walls_enabled ? 1 : 0,
                         local_result.meta.wall_grid_size,
                         local_result.meta.wall_grid_offset_x,
                         local_result.meta.wall_grid_offset_z);

    sdk_world_meta_to_world_desc(&local_result.meta, &requested_desc);
    stage_ms = GetTickCount64();
    sdk_persistence_init(persistence, &requested_desc, local_result.target.save_path);
    shutdown_persistence_started = 1;
    sdk_persistence_get_world_desc(persistence, &local_result.world_desc);
    local_result.persistence_init_ms = GetTickCount64() - stage_ms;

    /* Initialize world config - this now handles superchunk and wall config */
    sdk_world_config_init(&local_result.meta);

    stage_ms = GetTickCount64();
    sdk_worldgen_init(worldgen, &local_result.world_desc);
    shutdown_worldgen_started = 1;
    local_result.worldgen_init_ms = GetTickCount64() - stage_ms;
    if (!worldgen->impl) {
        strcpy_s(local_result.failure_reason, sizeof(local_result.failure_reason),
                 "worldgen init failed");
        goto cleanup;
    }

    sdk_settlement_set_world_path(worldgen, sdk_persistence_get_save_path(persistence));

    sdk_chunk_manager_init(chunk_mgr);
    shutdown_chunk_mgr_started = 1;
    sdk_chunk_manager_set_background_expansion(chunk_mgr, false);
    if (!sdk_persistence_bind_construction_registry(persistence, chunk_mgr->construction_registry)) {
        strcpy_s(local_result.failure_reason, sizeof(local_result.failure_reason),
                 "construction registry load failed");
        goto cleanup;
    }

    sdk_chunk_streamer_init(streamer, &worldgen->desc, persistence);
    shutdown_streamer_started = 1;

    have_persisted_state = sdk_persistence_get_state(persistence, &persisted_state);
    stage_ms = GetTickCount64();
    if (have_persisted_state) {
        local_result.spawn[0] = persisted_state.position[0];
        local_result.spawn[1] = persisted_state.position[1];
        local_result.spawn[2] = persisted_state.position[2];
        chunk_mgr->cam_cx = sdk_world_to_chunk_x((int)floorf(persisted_state.position[0]));
        chunk_mgr->cam_cz = sdk_world_to_chunk_z((int)floorf(persisted_state.position[2]));
        sdk_chunk_manager_set_grid_size(chunk_mgr,
                                        persisted_state.chunk_grid_size > 0
                                            ? persisted_state.chunk_grid_size
                                            : sdk_chunk_manager_grid_size_from_radius(
                                                  local_result.meta.render_distance_chunks));
    } else {
        int spawn_mode = request->spawn_mode >= 0 ? request->spawn_mode : request->create_request.spawn_mode;
        sdk_chunk_manager_set_grid_size(chunk_mgr,
                                        sdk_chunk_manager_grid_size_from_radius(
                                            local_result.meta.render_distance_chunks));
        switch (spawn_mode) {
            case 0:
                headless_choose_random_spawn(worldgen, local_result.spawn);
                break;
            case 1:
                headless_choose_center_spawn(worldgen, local_result.spawn);
                break;
            case 2:
            default:
                headless_choose_safe_spawn(worldgen, local_result.spawn);
                break;
        }
        chunk_mgr->cam_cx = sdk_world_to_chunk_x((int)floorf(local_result.spawn[0]));
        chunk_mgr->cam_cz = sdk_world_to_chunk_z((int)floorf(local_result.spawn[2]));
    }
    local_result.spawn_resolve_ms = GetTickCount64() - stage_ms;

    local_result.spawn_cx = chunk_mgr->cam_cx;
    local_result.spawn_cz = chunk_mgr->cam_cz;
    sdk_debug_log_printf("[HEADLESS] spawn pos=(%.3f,%.3f,%.3f) chunk=(%d,%d) persisted_state=%d\n",
                         local_result.spawn[0],
                         local_result.spawn[1],
                         local_result.spawn[2],
                         local_result.spawn_cx,
                         local_result.spawn_cz,
                         have_persisted_state ? 1 : 0);
    sdk_chunk_manager_update(chunk_mgr, chunk_mgr->cam_cx, chunk_mgr->cam_cz);

    for (local_result.iterations = 0;
         local_result.iterations < max_iterations;
         ++local_result.iterations) {
        SdkChunkBuildResult result;
        int adopted_any = 0;
        int processed = 0;

        sdk_chunk_streamer_schedule_startup_priority(streamer, chunk_mgr, safety_radius, 0);

        while (processed < 128 && sdk_chunk_streamer_pop_result(streamer, &result)) {
            SdkChunkResidencyRole role;
            uint32_t expected_generation = 0u;
            SdkChunk* slot_chunk = NULL;

            processed++;
            if (!result.built_chunk) {
                sdk_chunk_streamer_release_result(&result);
                continue;
            }
            if (!sdk_chunk_manager_is_desired(chunk_mgr,
                                             result.cx,
                                             result.cz,
                                             &role,
                                             &expected_generation) ||
                expected_generation != result.generation) {
                sdk_chunk_streamer_release_result(&result);
                continue;
            }

            if (result.type == SDK_CHUNK_STREAM_RESULT_GENERATED) {
                slot_chunk = sdk_chunk_manager_adopt_built_chunk(chunk_mgr, result.built_chunk, role);
            } else {
                slot_chunk = sdk_chunk_manager_get_chunk(chunk_mgr, result.cx, result.cz);
                if (slot_chunk) {
                    sdk_chunk_apply_mesh_state(slot_chunk, result.built_chunk, result.dirty_mask);
                }
            }

            if (slot_chunk) {
                headless_mark_chunk_gpu_ready(slot_chunk, 1);
                local_result.persist_store_attempts++;
                if (sdk_persistence_store_chunk(persistence, slot_chunk)) {
                    local_result.persist_store_successes++;
                } else {
                    headless_probe_store_failure(slot_chunk, &local_result);
                }
                adopted_any = 1;
            }
            sdk_chunk_streamer_release_result(&result);
        }

        headless_collect_readiness(chunk_mgr,
                                   streamer,
                                   chunk_mgr->cam_cx,
                                   chunk_mgr->cam_cz,
                                   safety_radius,
                                   &local_result.readiness);
        if (local_result.readiness.desired_primary > 0 && local_result.desired_primary_ms == 0u) {
            local_result.desired_primary_ms = GetTickCount64() - started_ms;
        }
        if (local_result.readiness.desired_primary > 0 &&
            local_result.readiness.resident_primary >= local_result.readiness.desired_primary &&
            local_result.resident_ready_ms == 0u) {
            local_result.resident_ready_ms = GetTickCount64() - started_ms;
        }
        if (local_result.readiness.desired_primary > 0 &&
            local_result.readiness.gpu_ready_primary >= local_result.readiness.desired_primary &&
            local_result.gpu_ready_ms == 0u) {
            local_result.gpu_ready_ms = GetTickCount64() - started_ms;
        }
        sdk_load_trace_note_readiness("headless_iteration_readiness",
                                      local_result.readiness.desired_primary,
                                      local_result.readiness.resident_primary,
                                      local_result.readiness.gpu_ready_primary,
                                      local_result.readiness.pending_jobs,
                                      local_result.readiness.pending_results,
                                      local_result.readiness.pending_uploads,
                                      "headless bootstrap iteration");
        if (headless_readiness_satisfied(&local_result.readiness, stop_at)) {
            success = 1;
            sdk_load_trace_note("headless_readiness_satisfied", local_result.target.folder_id);
            break;
        }

        if (!adopted_any && local_result.readiness.pending_jobs == 0 && local_result.readiness.pending_results == 0) {
            sdk_chunk_streamer_schedule_visible_no_wall_support(streamer, chunk_mgr);
        }

        Sleep(4);
    }

    if (!success && local_result.failure_reason[0] == '\0') {
        strcpy_s(local_result.failure_reason, sizeof(local_result.failure_reason),
                 "startup readiness not reached before iteration cap");
    }

    if (success && request->save_on_success) {
        int slot_index;
        stage_ms = GetTickCount64();

        for (slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
            const SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at_const(chunk_mgr, slot_index);
            if (!slot || !slot->occupied || !slot->chunk.blocks) continue;
            local_result.persist_store_attempts++;
            if (sdk_persistence_store_chunk(persistence, &slot->chunk)) {
                local_result.persist_store_successes++;
            } else {
                headless_probe_store_failure(&slot->chunk, &local_result);
            }
        }
        local_result.persisted_chunk_count = sdk_persistence_get_chunk_count(persistence);
        if (!have_persisted_state) {
            headless_store_default_state(persistence,
                                         local_result.spawn,
                                         sdk_chunk_manager_grid_size(chunk_mgr));
        }
        sdk_persistence_set_world_desc(persistence, &local_result.world_desc);
        sdk_persistence_save(persistence);
        local_result.save_write_ms = GetTickCount64() - stage_ms;
        local_result.persisted_chunk_count = sdk_persistence_get_chunk_count(persistence);
    }

cleanup:
    local_result.success = success ? 1 : 0;
    local_result.total_elapsed_ms = GetTickCount64() - started_ms;
    sdk_debug_log_printf("[HEADLESS] result success=%d failure=\"%s\" iterations=%d desired=%d resident=%d gpu_ready=%d jobs=%d results=%d uploads=%d elapsed_ms=%llu\n",
                         local_result.success,
                         local_result.failure_reason[0] ? local_result.failure_reason : "",
                         local_result.iterations,
                         local_result.readiness.desired_primary,
                         local_result.readiness.resident_primary,
                         local_result.readiness.gpu_ready_primary,
                         local_result.readiness.pending_jobs,
                         local_result.readiness.pending_results,
                         local_result.readiness.pending_uploads,
                         (unsigned long long)local_result.total_elapsed_ms);
    sdk_load_trace_note_readiness(local_result.success ? "headless_session_success" : "headless_session_failure",
                                  local_result.readiness.desired_primary,
                                  local_result.readiness.resident_primary,
                                  local_result.readiness.gpu_ready_primary,
                                  local_result.readiness.pending_jobs,
                                  local_result.readiness.pending_results,
                                  local_result.readiness.pending_uploads,
                                  local_result.success
                                      ? local_result.target.folder_id
                                      : (local_result.failure_reason[0]
                                             ? local_result.failure_reason
                                             : "headless session failed"));

    if (shutdown_streamer_started) {
        sdk_chunk_streamer_begin_shutdown(streamer);
        while (!sdk_chunk_streamer_poll_shutdown(streamer)) {
            Sleep(1);
        }
        sdk_chunk_streamer_shutdown(streamer);
    }
    if (shutdown_chunk_mgr_started) {
        sdk_chunk_manager_shutdown(chunk_mgr);
    }
    if (shutdown_worldgen_started) {
        sdk_worldgen_shutdown(worldgen);
    }
    if (shutdown_persistence_started) {
        sdk_persistence_shutdown(persistence);
    }
    sdk_world_config_shutdown();
    if (shared_cache_started) {
        sdk_worldgen_shared_cache_shutdown();
    }

    free(streamer);
    free(chunk_mgr);
    free(worldgen);
    free(persistence);

    if (out_result) *out_result = local_result;
    return success;
}
