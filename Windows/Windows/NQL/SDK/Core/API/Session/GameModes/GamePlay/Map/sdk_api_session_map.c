#include "sdk_api_session_internal.h"

/* ============================================================================
 * Map Cache Identity
 * ============================================================================ */

void map_cache_identity(const char** out_world_save_id, uint32_t* out_world_seed)
{
    if (out_world_save_id) {
        *out_world_save_id = g_map_scheduler.world_save_id;
        if (!g_map_scheduler.initialized || !g_map_scheduler.world_save_id[0]) {
            *out_world_save_id = g_sdk.world_save_id;
        }
    }
    if (out_world_seed) {
        *out_world_seed = g_map_scheduler.initialized ? g_map_scheduler.world_seed : g_sdk.world_seed;
    }
}

/* ============================================================================
 * Map Tile Path Building
 * ============================================================================ */

int build_superchunk_map_tile_cache_path_for_identity(
    const char* world_save_id,
    uint32_t world_seed,
    int origin_x,
    int origin_z,
    char* out_path,
    size_t out_path_len)
{
    char revision_dir[MAX_PATH];
    char world_dir[MAX_PATH];
    int scx;
    int scz;
    int written;

    if (!out_path || out_path_len == 0) return 0;
    out_path[0] = '\0';
    if (!ensure_directory_exists_a(SDK_MAP_TILE_CACHE_ROOT)) return 0;

    written = snprintf(revision_dir, sizeof(revision_dir), "%s\\rev_%u", SDK_MAP_TILE_CACHE_ROOT,
                       (unsigned)SDK_MAP_TILE_CACHE_VERSION);
    if (written <= 0 || (size_t)written >= sizeof(revision_dir)) return 0;
    if (!ensure_directory_exists_a(revision_dir)) return 0;

    if (world_save_id && world_save_id[0]) {
        written = snprintf(world_dir, sizeof(world_dir), "%s\\%s", revision_dir, world_save_id);
    } else {
        written = snprintf(world_dir, sizeof(world_dir), "%s\\seed_%u", revision_dir, world_seed);
    }
    if (written <= 0 || (size_t)written >= sizeof(world_dir)) return 0;
    if (!ensure_directory_exists_a(world_dir)) return 0;

    {
        const int tile_blocks = session_map_superchunk_tile_blocks();
        scx = (origin_x >= 0) ? (origin_x / tile_blocks)
                              : -(((-origin_x) + tile_blocks - 1) / tile_blocks);
        scz = (origin_z >= 0) ? (origin_z / tile_blocks)
                              : -(((-origin_z) + tile_blocks - 1) / tile_blocks);
    }
    written = snprintf(out_path, out_path_len, "%s\\sc_%d_%d.tile", world_dir, scx, scz);
    return written > 0 && (size_t)written < out_path_len;
}

int superchunk_map_tile_header_valid(const SdkPersistedMapTileHeader* header,
                                     uint32_t expected_build_kind,
                                     uint32_t expected_seed,
                                     int origin_x,
                                     int origin_z)
{
    if (!header) return 0;
    return header->magic == SDK_MAP_TILE_CACHE_MAGIC &&
           header->version == SDK_MAP_TILE_CACHE_VERSION &&
           header->build_kind == expected_build_kind &&
           header->seed == expected_seed &&
           header->origin_x == origin_x &&
           header->origin_z == origin_z &&
           header->dim == HUD_MAP_DIM;
}

int build_superchunk_map_tile_cache_path(int origin_x, int origin_z, char* out_path, size_t out_path_len)
{
    const char* world_save_id = NULL;
    uint32_t world_seed = 0u;

    map_cache_identity(&world_save_id, &world_seed);
    return build_superchunk_map_tile_cache_path_for_identity(
        world_save_id, world_seed, origin_x, origin_z, out_path, out_path_len);
}

/* ============================================================================
 * Map Tile Disk I/O
 * ============================================================================ */

int load_superchunk_map_tile_from_disk_for_identity(const char* world_save_id,
                                                    uint32_t world_seed,
                                                    int origin_x,
                                                    int origin_z,
                                                    uint32_t* out_pixels)
{
    char path[MAX_PATH];
    FILE* file;
    SdkPersistedMapTile tile;

    if (!out_pixels) return 0;
    if (!build_superchunk_map_tile_cache_path_for_identity(
            world_save_id, world_seed, origin_x, origin_z, path, sizeof(path))) {
        return 0;
    }

    file = fopen(path, "rb");
    if (!file) return 0;
    if (fread(&tile, 1u, sizeof(tile), file) != sizeof(tile)) {
        fclose(file);
        return 0;
    }
    fclose(file);

    if (!superchunk_map_tile_header_valid((const SdkPersistedMapTileHeader*)&tile,
                                          (uint32_t)SDK_MAP_BUILD_EXACT_OFFLINE,
                                          world_seed,
                                          origin_x, origin_z)) {
        return 0;
    }

    memcpy(out_pixels, tile.pixels, sizeof(tile.pixels));
    return 1;
}

int load_superchunk_map_tile_from_disk(int origin_x, int origin_z, uint32_t* out_pixels)
{
    const char* world_save_id = NULL;
    uint32_t world_seed = 0u;

    map_cache_identity(&world_save_id, &world_seed);
    return load_superchunk_map_tile_from_disk_for_identity(
        world_save_id, world_seed, origin_x, origin_z, out_pixels);
}

int superchunk_map_tile_exists_on_disk_for_identity(const char* world_save_id,
                                                     uint32_t world_seed,
                                                     int origin_x,
                                                     int origin_z)
{
    char path[MAX_PATH];
    FILE* file;
    SdkPersistedMapTileHeader header;

    if (!build_superchunk_map_tile_cache_path_for_identity(
            world_save_id, world_seed, origin_x, origin_z, path, sizeof(path))) {
        return 0;
    }

    file = fopen(path, "rb");
    if (!file) return 0;
    if (fread(&header, 1u, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return superchunk_map_tile_header_valid(&header,
                                            (uint32_t)SDK_MAP_BUILD_EXACT_OFFLINE,
                                            world_seed,
                                            origin_x, origin_z);
}

void save_superchunk_map_tile_to_disk_for_identity(const char* world_save_id,
                                                    uint32_t world_seed,
                                                    int origin_x,
                                                    int origin_z,
                                                    uint32_t build_kind,
                                                    const uint32_t* pixels)
{
    char path[MAX_PATH];
    FILE* file;
    SdkPersistedMapTile tile;

    if (!pixels) return;
    if (!build_superchunk_map_tile_cache_path_for_identity(
            world_save_id, world_seed, origin_x, origin_z, path, sizeof(path))) {
        return;
    }

    memset(&tile, 0, sizeof(tile));
    tile.magic = SDK_MAP_TILE_CACHE_MAGIC;
    tile.version = SDK_MAP_TILE_CACHE_VERSION;
    tile.build_kind = build_kind;
    tile.seed = world_seed;
    tile.origin_x = origin_x;
    tile.origin_z = origin_z;
    tile.dim = HUD_MAP_DIM;
    memcpy(tile.pixels, pixels, sizeof(tile.pixels));

    file = fopen(path, "wb");
    if (!file) return;
    fwrite(&tile, 1u, sizeof(tile), file);
    fclose(file);
}

void save_superchunk_map_tile_to_disk(int origin_x, int origin_z, const uint32_t* pixels)
{
    const char* world_save_id = NULL;
    uint32_t world_seed = 0u;

    map_cache_identity(&world_save_id, &world_seed);
    save_superchunk_map_tile_to_disk_for_identity(world_save_id,
                                                  world_seed,
                                                  origin_x,
                                                  origin_z,
                                                  (uint32_t)SDK_MAP_BUILD_EXACT_OFFLINE,
                                                  pixels);
}

/* ============================================================================
 * Map Scheduler - Queue Management
 * ============================================================================ */

int map_queue_index(int head, int count, int capacity)
{
    return (head + count) % capacity;
}

int choose_map_worker_count(void)
{
    SYSTEM_INFO si;
    int count;

    GetSystemInfo(&si);
    count = (int)si.dwNumberOfProcessors;
    if (count < 1) count = 1;
    if (count > SDK_MAP_MAX_WORKERS) count = SDK_MAP_MAX_WORKERS;
    return count;
}

int map_scheduler_building_jobs_locked(const SdkMapSchedulerInternal* sched)
{
    int building_jobs = 0;

    if (!sched) return 0;
    for (int i = 0; i < HUD_MAP_CACHE_SLOTS; ++i) {
        if (g_superchunk_map_cache[i].valid &&
            g_superchunk_map_cache[i].state == SDK_MAP_TILE_BUILDING) {
            building_jobs++;
        }
    }
    return building_jobs;
}

int map_floor_div_local_coord(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

static int map_cell_blocks(void)
{
    int cell_blocks = session_map_superchunk_tile_blocks() / HUD_MAP_DIM;
    return (cell_blocks > 0) ? cell_blocks : 1;
}

static int map_macro_tile_superchunk_span(void)
{
    int tile_blocks = session_map_superchunk_tile_blocks();
    int macro_blocks = SDK_WORLDGEN_MACRO_TILE_SIZE * SDK_WORLDGEN_MACRO_CELL_BLOCKS;
    int span = (tile_blocks > 0) ? (macro_blocks / tile_blocks) : 0;
    return (span > 0) ? span : 1;
}

int map_macro_tile_coord_from_origin(int origin_coord)
{
    return map_floor_div_local_coord(
        origin_coord,
        session_map_superchunk_tile_blocks() * map_macro_tile_superchunk_span());
}

int map_entry_matches_macro_tile(const SuperchunkMapCacheEntry* entry, int macro_tile_x, int macro_tile_z)
{
    if (!entry) return 0;
    return map_macro_tile_coord_from_origin(entry->origin_x) == macro_tile_x &&
           map_macro_tile_coord_from_origin(entry->origin_z) == macro_tile_z;
}

int map_macro_tile_claimed_by_other_worker_locked(const SdkMapSchedulerInternal* sched,
                                                   const SdkMapWorker* worker,
                                                   int macro_tile_x,
                                                   int macro_tile_z)
{
    if (!sched) return 0;

    for (int i = 0; i < sched->worker_count; ++i) {
        const SdkMapWorker* other = &sched->workers[i];
        if (other == worker) continue;
        if (!other->has_macro_tile_claim) continue;
        if (other->claimed_macro_tile_x == macro_tile_x &&
            other->claimed_macro_tile_z == macro_tile_z) {
            return 1;
        }
    }

    return 0;
}

int map_job_distance_from_worker(const SdkMapWorker* worker,
                                 const SuperchunkMapCacheEntry* entry)
{
    int dx;
    int dz;

    if (!worker || !worker->has_last_origin || !entry) return INT_MAX;

    {
        const int tile_blocks = session_map_superchunk_tile_blocks();
        dx = abs((entry->origin_x - worker->last_origin_x) / tile_blocks);
        dz = abs((entry->origin_z - worker->last_origin_z) / tile_blocks);
    }
    return dx + dz;
}

int map_take_next_job_locked(SdkMapSchedulerInternal* sched, SdkMapWorker* worker)
{
    int best_offset = -1;
    int slot_index;

    if (!sched || sched->job_count <= 0) return -1;

    if (sched->mode == SDK_MAP_SCHED_MODE_OFFLINE_BULK && worker) {
        int best_distance = INT_MAX;

        if (worker->has_macro_tile_claim) {
            for (int i = 0; i < sched->job_count; ++i) {
                int qidx = map_queue_index(sched->job_head, i, SDK_MAP_JOB_CAPACITY);
                int candidate_slot = sched->jobs[qidx];

                if (candidate_slot >= 0 && candidate_slot < HUD_MAP_CACHE_SLOTS) {
                    SuperchunkMapCacheEntry* entry = &g_superchunk_map_cache[candidate_slot];
                    int distance;

                    if (!entry->valid || entry->state != SDK_MAP_TILE_QUEUED) continue;
                    if (!map_entry_matches_macro_tile(entry,
                                                      worker->claimed_macro_tile_x,
                                                      worker->claimed_macro_tile_z)) {
                        continue;
                    }
                    distance = map_job_distance_from_worker(worker, entry);
                    if (distance < best_distance) {
                        best_distance = distance;
                        best_offset = i;
                        if (distance <= 1) break;
                    }
                }
            }

            if (best_offset < 0) {
                worker->has_macro_tile_claim = false;
            }
        }

        if (best_offset < 0 && worker->has_last_origin) {
            best_distance = INT_MAX;
            for (int i = 0; i < sched->job_count; ++i) {
                int qidx = map_queue_index(sched->job_head, i, SDK_MAP_JOB_CAPACITY);
                int candidate_slot = sched->jobs[qidx];

                if (candidate_slot >= 0 && candidate_slot < HUD_MAP_CACHE_SLOTS) {
                    SuperchunkMapCacheEntry* entry = &g_superchunk_map_cache[candidate_slot];
                    int macro_tile_x;
                    int macro_tile_z;
                    int distance;

                    if (!entry->valid || entry->state != SDK_MAP_TILE_QUEUED) continue;
                    macro_tile_x = map_macro_tile_coord_from_origin(entry->origin_x);
                    macro_tile_z = map_macro_tile_coord_from_origin(entry->origin_z);
                    if (map_macro_tile_claimed_by_other_worker_locked(
                            sched, worker, macro_tile_x, macro_tile_z)) {
                        continue;
                    }
                    distance = map_job_distance_from_worker(worker, entry);
                    if (distance < best_distance) {
                        best_distance = distance;
                        best_offset = i;
                        if (distance <= 1) break;
                    }
                }
            }
        }

        if (best_offset < 0 && worker->has_last_origin) {
            best_distance = INT_MAX;
            for (int i = 0; i < sched->job_count; ++i) {
                int qidx = map_queue_index(sched->job_head, i, SDK_MAP_JOB_CAPACITY);
                int candidate_slot = sched->jobs[qidx];

                if (candidate_slot >= 0 && candidate_slot < HUD_MAP_CACHE_SLOTS) {
                    SuperchunkMapCacheEntry* entry = &g_superchunk_map_cache[candidate_slot];
                    int distance;

                    if (!entry->valid || entry->state != SDK_MAP_TILE_QUEUED) continue;
                    distance = map_job_distance_from_worker(worker, entry);
                    if (distance < best_distance) {
                        best_distance = distance;
                        best_offset = i;
                        if (distance <= 1) break;
                    }
                }
            }
        }
    }

    if (best_offset < 0) {
        best_offset = 0;
    }

    slot_index = sched->jobs[map_queue_index(sched->job_head, best_offset, SDK_MAP_JOB_CAPACITY)];
    for (int i = best_offset; i > 0; --i) {
        int dst = map_queue_index(sched->job_head, i, SDK_MAP_JOB_CAPACITY);
        int src = map_queue_index(sched->job_head, i - 1, SDK_MAP_JOB_CAPACITY);
        sched->jobs[dst] = sched->jobs[src];
    }
    sched->job_head = (sched->job_head + 1) % SDK_MAP_JOB_CAPACITY;
    sched->job_count--;

    if (sched->mode == SDK_MAP_SCHED_MODE_OFFLINE_BULK &&
        worker &&
        slot_index >= 0 &&
        slot_index < HUD_MAP_CACHE_SLOTS &&
        g_superchunk_map_cache[slot_index].valid) {
        worker->claimed_macro_tile_x =
            map_macro_tile_coord_from_origin(g_superchunk_map_cache[slot_index].origin_x);
        worker->claimed_macro_tile_z =
            map_macro_tile_coord_from_origin(g_superchunk_map_cache[slot_index].origin_z);
        worker->has_macro_tile_claim = true;
    }

    return slot_index;
}

static void reset_map_bulk_cursor_locked(SdkMapSchedulerInternal* sched)
{
    if (!sched) return;
    sched->bulk_cursor_started = true;
    sched->bulk_scx = 0;
    sched->bulk_scz = 0;
    sched->bulk_dir = 0;
    sched->bulk_leg_length = 1;
    sched->bulk_leg_progress = 0;
    sched->bulk_legs_done = 0;
    sched->bulk_ring = 0;
    sched->bulk_tiles_completed = 0;
}

void map_reset_bulk_state_locked(SdkMapSchedulerInternal* sched)
{
    reset_map_bulk_cursor_locked(sched);
}

static void map_bulk_cursor_pop_locked(SdkMapSchedulerInternal* sched, int* out_scx, int* out_scz)
{
    int scx;
    int scz;
    int abs_scx;
    int abs_scz;

    if (!sched || !sched->bulk_cursor_started) return;

    scx = sched->bulk_scx;
    scz = sched->bulk_scz;
    abs_scx = (scx < 0) ? -scx : scx;
    abs_scz = (scz < 0) ? -scz : scz;
    sched->bulk_ring = max(abs_scx, abs_scz) * map_macro_tile_superchunk_span();
    if (out_scx) *out_scx = scx;
    if (out_scz) *out_scz = scz;

    switch (sched->bulk_dir) {
        case 0: sched->bulk_scx++; break;
        case 1: sched->bulk_scz++; break;
        case 2: sched->bulk_scx--; break;
        default: sched->bulk_scz--; break;
    }

    sched->bulk_leg_progress++;
    if (sched->bulk_leg_progress >= sched->bulk_leg_length) {
        sched->bulk_leg_progress = 0;
        sched->bulk_dir = (sched->bulk_dir + 1) & 3;
        sched->bulk_legs_done++;
        if (sched->bulk_legs_done >= 2) {
            sched->bulk_legs_done = 0;
            sched->bulk_leg_length++;
        }
    }
}

static int map_page_queue_index(int head, int count)
{
    return (head + count) % SDK_MAP_PAGE_JOB_CAPACITY;
}

static int map_take_next_page_job_locked(SdkMapSchedulerInternal* sched, SdkMapOfflinePageJob* out_job)
{
    if (!sched || !out_job || sched->page_job_count <= 0) return 0;
    *out_job = sched->page_jobs[sched->page_job_head];
    sched->page_job_head = (sched->page_job_head + 1) % SDK_MAP_PAGE_JOB_CAPACITY;
    sched->page_job_count--;
    return 1;
}

int map_drop_queued_page_tiles_locked(SdkMapSchedulerInternal* sched)
{
    int dropped_tiles = 0;

    if (!sched) return 0;
    for (int i = 0; i < sched->page_job_count; ++i) {
        const SdkMapOfflinePageJob* job =
            &sched->page_jobs[map_page_queue_index(sched->page_job_head, i)];
        dropped_tiles += job->tile_count;
    }
    sched->page_job_head = 0;
    sched->page_job_count = 0;
    return dropped_tiles;
}

static const SdkWorldGenMacroCell* map_macro_cell_at(const SdkWorldGenMacroTile* tile, int x, int z)
{
    if (!tile) return NULL;
    x = sdk_worldgen_clampi(x, 0, SDK_WORLDGEN_TILE_STRIDE - 1);
    z = sdk_worldgen_clampi(z, 0, SDK_WORLDGEN_TILE_STRIDE - 1);
    return &tile->cells[z * SDK_WORLDGEN_TILE_STRIDE + x];
}

static float map_bilerp4f(float a, float b, float c, float d, float tx, float tz)
{
    float ab = a * (1.0f - tx) + b * tx;
    float cd = c * (1.0f - tx) + d * tx;
    return ab * (1.0f - tz) + cd * tz;
}

static int map_floor_div_i(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

static uint8_t map_weighted_vote4_u8(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                                     float w00, float w10, float w01, float w11)
{
    float best_weight = -1.0f;
    uint8_t best_value = a;
    uint8_t candidates[4] = { a, b, c, d };

    for (int i = 0; i < 4; ++i) {
        uint8_t value = candidates[i];
        float total = 0.0f;

        if (a == value) total += w00;
        if (b == value) total += w10;
        if (c == value) total += w01;
        if (d == value) total += w11;
        if (total > best_weight) {
            best_weight = total;
            best_value = value;
        }
    }

    return best_value;
}

static int sample_superchunk_map_profile_from_macro_tile(const SdkWorldGenMacroTile* tile,
                                                         SdkWorldGen* wg,
                                                         int wx,
                                                         int wz,
                                                         SdkTerrainColumnProfile* out_profile)
{
    const SdkWorldGenImpl* impl = wg ? (const SdkWorldGenImpl*)wg->impl : NULL;
    int macro_x;
    int macro_z;
    int origin_macro_x;
    int origin_macro_z;
    int local_macro_x;
    int local_macro_z;
    int block_x_in_macro;
    int block_z_in_macro;
    int ix;
    int iz;
    float tx;
    float tz;
    float w00;
    float w10;
    float w01;
    float w11;
    const SdkWorldGenMacroCell* c00;
    const SdkWorldGenMacroCell* c10;
    const SdkWorldGenMacroCell* c01;
    const SdkWorldGenMacroCell* c11;
    int water_height;
    uint8_t temperature_band;

    if (!impl || !tile || !out_profile) return 0;
    memset(out_profile, 0, sizeof(*out_profile));

    macro_x = map_floor_div_i(wx, (int)impl->macro_cell_size);
    macro_z = map_floor_div_i(wz, (int)impl->macro_cell_size);
    block_x_in_macro = wx - macro_x * (int)impl->macro_cell_size;
    block_z_in_macro = wz - macro_z * (int)impl->macro_cell_size;
    if (block_x_in_macro < 0) block_x_in_macro += impl->macro_cell_size;
    if (block_z_in_macro < 0) block_z_in_macro += impl->macro_cell_size;

    origin_macro_x = tile->tile_x * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;
    origin_macro_z = tile->tile_z * SDK_WORLDGEN_MACRO_TILE_SIZE - SDK_WORLDGEN_MACRO_TILE_HALO;
    local_macro_x = macro_x - origin_macro_x;
    local_macro_z = macro_z - origin_macro_z;
    ix = sdk_worldgen_clampi(local_macro_x, 0, SDK_WORLDGEN_TILE_STRIDE - 2);
    iz = sdk_worldgen_clampi(local_macro_z, 0, SDK_WORLDGEN_TILE_STRIDE - 2);

    tx = (float)block_x_in_macro / (float)impl->macro_cell_size;
    tz = (float)block_z_in_macro / (float)impl->macro_cell_size;
    w00 = (1.0f - tx) * (1.0f - tz);
    w10 = tx * (1.0f - tz);
    w01 = (1.0f - tx) * tz;
    w11 = tx * tz;

    c00 = map_macro_cell_at(tile, ix, iz);
    c10 = map_macro_cell_at(tile, ix + 1, iz);
    c01 = map_macro_cell_at(tile, ix, iz + 1);
    c11 = map_macro_cell_at(tile, ix + 1, iz + 1);
    if (!c00 || !c10 || !c01 || !c11) return 0;

    out_profile->base_height = (int16_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->base_height, (float)c10->base_height,
                                 (float)c01->base_height, (float)c11->base_height, tx, tz)),
        0, CHUNK_HEIGHT - 1);
    out_profile->surface_height = (int16_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->surface_height, (float)c10->surface_height,
                                 (float)c01->surface_height, (float)c11->surface_height, tx, tz)),
        0, CHUNK_HEIGHT - 1);
    water_height = sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->water_height, (float)c10->water_height,
                                 (float)c01->water_height, (float)c11->water_height, tx, tz)),
        0, CHUNK_HEIGHT - 1);
    out_profile->river_bed_height = (int16_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->river_bed_height, (float)c10->river_bed_height,
                                 (float)c01->river_bed_height, (float)c11->river_bed_height, tx, tz)),
        0, CHUNK_HEIGHT - 1);

    out_profile->soil_depth = (uint8_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->soil_depth, (float)c10->soil_depth,
                                 (float)c01->soil_depth, (float)c11->soil_depth, tx, tz)),
        0, 15);
    out_profile->sediment_depth = (uint8_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->sediment_depth, (float)c10->sediment_depth,
                                 (float)c01->sediment_depth, (float)c11->sediment_depth, tx, tz)),
        0, 15);
    out_profile->sediment_thickness = out_profile->sediment_depth;
    out_profile->regolith_thickness = (uint8_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->sediment_depth, (float)c10->sediment_depth,
                                 (float)c01->sediment_depth, (float)c11->sediment_depth, tx, tz) + 2.0f),
        0, 15);
    out_profile->river_order = (uint8_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->river_order, (float)c10->river_order,
                                 (float)c01->river_order, (float)c11->river_order, tx, tz)),
        0, 8);
    out_profile->floodplain_width = (uint8_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->floodplain_width, (float)c10->floodplain_width,
                                 (float)c01->floodplain_width, (float)c11->floodplain_width, tx, tz)),
        0, 15);
    out_profile->water_table_depth = (uint8_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->water_table_depth, (float)c10->water_table_depth,
                                 (float)c01->water_table_depth, (float)c11->water_table_depth, tx, tz)),
        0, 15);

    out_profile->terrain_province = (SdkTerrainProvince)map_weighted_vote4_u8(
        c00->terrain_province, c10->terrain_province, c01->terrain_province, c11->terrain_province,
        w00, w10, w01, w11);
    out_profile->bedrock_province = (SdkBedrockProvince)map_weighted_vote4_u8(
        c00->bedrock_province, c10->bedrock_province, c01->bedrock_province, c11->bedrock_province,
        w00, w10, w01, w11);
    temperature_band = map_weighted_vote4_u8(
        c00->temperature_band, c10->temperature_band, c01->temperature_band, c11->temperature_band,
        w00, w10, w01, w11);
    out_profile->temperature_band = (SdkTemperatureBand)temperature_band;
    out_profile->moisture_band = (SdkMoistureBand)map_weighted_vote4_u8(
        c00->moisture_band, c10->moisture_band, c01->moisture_band, c11->moisture_band,
        w00, w10, w01, w11);
    out_profile->surface_sediment = (SdkSurfaceSediment)map_weighted_vote4_u8(
        c00->surface_sediment, c10->surface_sediment, c01->surface_sediment, c11->surface_sediment,
        w00, w10, w01, w11);
    out_profile->parent_material = (SdkParentMaterialClass)map_weighted_vote4_u8(
        c00->parent_material, c10->parent_material, c01->parent_material, c11->parent_material,
        w00, w10, w01, w11);
    out_profile->drainage_class = (SdkDrainageClass)map_weighted_vote4_u8(
        c00->drainage_class, c10->drainage_class, c01->drainage_class, c11->drainage_class,
        w00, w10, w01, w11);
    out_profile->soil_reaction = (SdkSoilReactionClass)map_weighted_vote4_u8(
        c00->soil_reaction, c10->soil_reaction, c01->soil_reaction, c11->soil_reaction,
        w00, w10, w01, w11);
    out_profile->soil_fertility = (SdkSoilFertilityClass)map_weighted_vote4_u8(
        c00->soil_fertility, c10->soil_fertility, c01->soil_fertility, c11->soil_fertility,
        w00, w10, w01, w11);
    out_profile->soil_salinity = (SdkSoilSalinityClass)map_weighted_vote4_u8(
        c00->soil_salinity, c10->soil_salinity, c01->soil_salinity, c11->soil_salinity,
        w00, w10, w01, w11);
    out_profile->water_surface_class = (SdkSurfaceWaterClass)map_weighted_vote4_u8(
        c00->water_surface_class, c10->water_surface_class, c01->water_surface_class, c11->water_surface_class,
        w00, w10, w01, w11);
    out_profile->ecology = (SdkBiomeEcology)map_weighted_vote4_u8(
        c00->ecology, c10->ecology, c01->ecology, c11->ecology,
        w00, w10, w01, w11);
    out_profile->resource_province = (SdkResourceProvince)map_weighted_vote4_u8(
        c00->resource_province, c10->resource_province, c01->resource_province, c11->resource_province,
        w00, w10, w01, w11);
    out_profile->hydrocarbon_class = (SdkHydrocarbonClass)map_weighted_vote4_u8(
        c00->hydrocarbon_class, c10->hydrocarbon_class, c01->hydrocarbon_class, c11->hydrocarbon_class,
        w00, w10, w01, w11);
    out_profile->resource_grade = (uint8_t)sdk_worldgen_clampi(
        (int)lrintf(map_bilerp4f((float)c00->resource_grade, (float)c10->resource_grade,
                                 (float)c01->resource_grade, (float)c11->resource_grade, tx, tz)),
        0, 255);
    if (out_profile->resource_province != RESOURCE_PROVINCE_OIL_FIELD) {
        out_profile->hydrocarbon_class = SDK_HYDROCARBON_NONE;
    }

    if (out_profile->terrain_province == TERRAIN_PROVINCE_OPEN_OCEAN ||
        out_profile->terrain_province == TERRAIN_PROVINCE_CONTINENTAL_SHELF) {
        if (water_height < impl->sea_level) water_height = impl->sea_level;
    }
    out_profile->water_height = (int16_t)water_height;

    if (out_profile->water_height > out_profile->surface_height) {
        if (out_profile->water_surface_class == SURFACE_WATER_NONE) {
            if (temperature_band == TEMP_POLAR) {
                out_profile->water_surface_class = SURFACE_WATER_PERENNIAL_ICE;
            } else if (temperature_band == TEMP_SUBPOLAR) {
                out_profile->water_surface_class = SURFACE_WATER_SEASONAL_ICE;
            } else {
                out_profile->water_surface_class = SURFACE_WATER_OPEN;
            }
        }
        if (out_profile->river_order > 0) {
            out_profile->landform_flags |= SDK_LANDFORM_RIVER_CHANNEL;
        } else {
            out_profile->landform_flags |= SDK_LANDFORM_LAKE_BASIN;
        }
    } else {
        out_profile->water_surface_class = SURFACE_WATER_NONE;
        out_profile->water_height = out_profile->surface_height;
    }
    if (out_profile->floodplain_width > 0) {
        out_profile->landform_flags |= SDK_LANDFORM_FLOODPLAIN;
    }

    return 1;
}

/* ============================================================================
 * Map Scheduler - Lifecycle
 * ============================================================================ */

void request_shutdown_superchunk_map_scheduler(void)
{
    if (!g_map_scheduler.initialized) return;

    EnterCriticalSection(&g_map_scheduler.lock);
    if (!g_map_scheduler.running) {
        LeaveCriticalSection(&g_map_scheduler.lock);
        return;
    }
    g_map_scheduler.running = false;
    g_map_scheduler.job_head = 0;
    g_map_scheduler.job_count = 0;
    if (g_map_scheduler.page_job_count > 0) {
        int dropped_tiles = map_drop_queued_page_tiles_locked(&g_map_scheduler);
        g_map_scheduler.bulk_tiles_inflight =
            max(0, g_map_scheduler.bulk_tiles_inflight - dropped_tiles);
    }
    for (int i = 0; i < HUD_MAP_CACHE_SLOTS; ++i) {
        g_superchunk_map_cache[i].valid = false;
        g_superchunk_map_cache[i].state = SDK_MAP_TILE_EMPTY;
        g_superchunk_map_cache[i].build_kind = (uint8_t)SDK_MAP_BUILD_INTERACTIVE_FALLBACK;
    }
    WakeAllConditionVariable(&g_map_scheduler.jobs_cv);
    LeaveCriticalSection(&g_map_scheduler.lock);
}

int poll_shutdown_superchunk_map_scheduler(void)
{
    if (!g_map_scheduler.initialized) return 1;

    for (int i = 0; i < g_map_scheduler.worker_count; ++i) {
        SdkMapWorker* worker = &g_map_scheduler.workers[i];
        if (worker->thread) {
            DWORD wait_result = WaitForSingleObject(worker->thread, 0u);
            if (wait_result == WAIT_TIMEOUT) {
                return 0;
            }
            CloseHandle(worker->thread);
            worker->thread = NULL;
        }
        sdk_worldgen_shutdown(&worker->worldgen);
        worker->owner = NULL;
        free(worker->map_profile_grid);
        worker->map_profile_grid = NULL;
        free(worker->map_exact_cell_grid);
        worker->map_exact_cell_grid = NULL;
        free(worker->map_exact_dry_hist);
        worker->map_exact_dry_hist = NULL;
        free(worker->map_exact_submerged_hist);
        worker->map_exact_submerged_hist = NULL;
        free(worker->map_exact_chunk_profiles);
        worker->map_exact_chunk_profiles = NULL;
        free(worker->map_exact_chunk_surface);
        worker->map_exact_chunk_surface = NULL;
    }

    map_debug_compare_shutdown();
    DeleteCriticalSection(&g_map_scheduler.lock);
    memset(&g_map_scheduler, 0, sizeof(g_map_scheduler));
    memset(g_superchunk_map_cache, 0, sizeof(g_superchunk_map_cache));
    g_superchunk_map_stamp = 1u;
    return 1;
}

void shutdown_superchunk_map_scheduler(void)
{
    request_shutdown_superchunk_map_scheduler();
    while (!poll_shutdown_superchunk_map_scheduler()) {
        Sleep(0);
    }
}

/* ============================================================================
 * Map Tile Pixel Building Functions
 * ============================================================================ */

static void map_exact_reset_worker_scratch(SdkMapWorker* worker)
{
    if (!worker) return;
    if (worker->map_exact_cell_grid) {
        memset(worker->map_exact_cell_grid, 0,
               sizeof(SdkMapExactCellAccumulator) * SDK_MAP_EXACT_TILE_CELL_COUNT);
    }
    if (worker->map_exact_dry_hist) {
        memset(worker->map_exact_dry_hist, 0,
               sizeof(uint16_t) * SDK_MAP_EXACT_TILE_CELL_COUNT * BLOCK_COUNT);
    }
    if (worker->map_exact_submerged_hist) {
        memset(worker->map_exact_submerged_hist, 0,
               sizeof(uint16_t) * SDK_MAP_EXACT_TILE_CELL_COUNT * BLOCK_COUNT);
    }
}

static BlockType map_exact_dominant_block(const uint16_t* histogram, int cell_index)
{
    BlockType best_block = BLOCK_AIR;
    uint16_t best_count = 0u;
    int base_index;

    if (!histogram || cell_index < 0) return BLOCK_AIR;
    base_index = cell_index * BLOCK_COUNT;
    for (int block_index = 1; block_index < BLOCK_COUNT; ++block_index) {
        uint16_t count = histogram[base_index + block_index];
        if (count > best_count) {
            best_count = count;
            best_block = (BlockType)block_index;
        }
    }
    return best_block;
}

static int map_exact_effective_height(const SdkMapExactCellAccumulator* cell)
{
    uint32_t surface_count;
    uint32_t water_count;

    if (!cell) return 0;
    surface_count = (uint32_t)cell->dry_count + (uint32_t)cell->submerged_count;
    if (surface_count > 0u) {
        return (int)(cell->land_height_sum / surface_count);
    }
    water_count = (uint32_t)cell->open_water_count +
                  (uint32_t)cell->seasonal_ice_count +
                  (uint32_t)cell->perennial_ice_count;
    if (water_count > 0u) {
        return (int)(cell->water_height_sum / water_count);
    }
    return 0;
}

static int map_exact_check_scheduler_running(const SdkMapSchedulerInternal* sched)
{
    int running = 1;

    if (!sched) return 0;
    EnterCriticalSection((CRITICAL_SECTION*)&sched->lock);
    running = sched->running ? 1 : 0;
    LeaveCriticalSection((CRITICAL_SECTION*)&sched->lock);
    return running;
}

static int map_exact_classify_surface_column(const SdkWorldGenSurfaceColumn* column,
                                             const SdkTerrainColumnProfile* profile,
                                             SdkMapExactColumnSample* out_sample)
{
    BlockType ground_block;

    if (!column || !out_sample) return 0;
    memset(out_sample, 0, sizeof(*out_sample));
    ground_block = map_ground_block_for_profile(profile);
    if (ground_block == BLOCK_AIR) {
        ground_block = (column->land_block > BLOCK_AIR) ? column->land_block : BLOCK_STONE;
    }

    switch ((SdkWorldGenSurfaceColumnKind)column->kind) {
        case SDK_WORLDGEN_SURFACE_COLUMN_OPEN_WATER:
            out_sample->kind = SDK_MAP_EXACT_COLUMN_OPEN_WATER;
            out_sample->ground_block = ground_block;
            out_sample->land_height = column->land_height;
            out_sample->water_height = column->top_height;
            out_sample->water_depth = column->water_depth ? column->water_depth : 1u;
            return 1;
        case SDK_WORLDGEN_SURFACE_COLUMN_SEASONAL_ICE:
            out_sample->kind = SDK_MAP_EXACT_COLUMN_SEASONAL_ICE;
            out_sample->ground_block = ground_block;
            out_sample->land_height = column->land_height;
            out_sample->water_height = column->top_height;
            out_sample->water_depth = column->water_depth ? column->water_depth : 1u;
            return 1;
        case SDK_WORLDGEN_SURFACE_COLUMN_PERENNIAL_ICE:
            out_sample->kind = SDK_MAP_EXACT_COLUMN_PERENNIAL_ICE;
            out_sample->ground_block = ground_block;
            out_sample->land_height = column->land_height;
            out_sample->water_height = column->top_height;
            out_sample->water_depth = column->water_depth ? column->water_depth : 1u;
            return 1;
        case SDK_WORLDGEN_SURFACE_COLUMN_DRY:
        case SDK_WORLDGEN_SURFACE_COLUMN_LAVA:
            out_sample->kind = SDK_MAP_EXACT_COLUMN_DRY;
            out_sample->ground_block = ground_block;
            out_sample->land_height = column->top_height;
            return 1;
        case SDK_WORLDGEN_SURFACE_COLUMN_VOID:
        default:
            return 0;
    }
}

static void map_exact_accumulate_column(SdkMapWorker* worker,
                                        int cell_x,
                                        int cell_z,
                                        const SdkMapExactColumnSample* sample)
{
    int cell_index;
    SdkMapExactCellAccumulator* cell;

    if (!worker || !worker->map_exact_cell_grid || !sample) return;
    if (cell_x < 0 || cell_x >= SDK_MAP_EXACT_TILE_DIM ||
        cell_z < 0 || cell_z >= SDK_MAP_EXACT_TILE_DIM) {
        return;
    }

    cell_index = cell_z * SDK_MAP_EXACT_TILE_DIM + cell_x;
    cell = &worker->map_exact_cell_grid[cell_index];
    switch (sample->kind) {
        case SDK_MAP_EXACT_COLUMN_DRY:
            cell->dry_count++;
            cell->land_height_sum += sample->land_height;
            if (worker->map_exact_dry_hist &&
                sample->ground_block > BLOCK_AIR &&
                sample->ground_block < BLOCK_COUNT) {
                worker->map_exact_dry_hist[cell_index * BLOCK_COUNT + (int)sample->ground_block]++;
            }
            break;
        case SDK_MAP_EXACT_COLUMN_OPEN_WATER:
            cell->open_water_count++;
            cell->water_height_sum += sample->water_height;
            cell->water_depth_sum += sample->water_depth;
            cell->land_height_sum += sample->land_height;
            if (sample->ground_block > BLOCK_AIR && sample->ground_block < BLOCK_COUNT) {
                cell->submerged_count++;
                if (worker->map_exact_submerged_hist) {
                    worker->map_exact_submerged_hist[cell_index * BLOCK_COUNT + (int)sample->ground_block]++;
                }
            }
            break;
        case SDK_MAP_EXACT_COLUMN_SEASONAL_ICE:
            cell->seasonal_ice_count++;
            cell->water_height_sum += sample->water_height;
            cell->water_depth_sum += sample->water_depth;
            cell->land_height_sum += sample->land_height;
            if (sample->ground_block > BLOCK_AIR && sample->ground_block < BLOCK_COUNT) {
                cell->submerged_count++;
                if (worker->map_exact_submerged_hist) {
                    worker->map_exact_submerged_hist[cell_index * BLOCK_COUNT + (int)sample->ground_block]++;
                }
            }
            break;
        case SDK_MAP_EXACT_COLUMN_PERENNIAL_ICE:
            cell->perennial_ice_count++;
            cell->water_height_sum += sample->water_height;
            cell->water_depth_sum += sample->water_depth;
            cell->land_height_sum += sample->land_height;
            if (sample->ground_block > BLOCK_AIR && sample->ground_block < BLOCK_COUNT) {
                cell->submerged_count++;
                if (worker->map_exact_submerged_hist) {
                    worker->map_exact_submerged_hist[cell_index * BLOCK_COUNT + (int)sample->ground_block]++;
                }
            }
            break;
        case SDK_MAP_EXACT_COLUMN_VOID:
        default:
            cell->void_count++;
            break;
    }
}

static uint32_t map_color_for_exact_cells(const SdkMapExactCellAccumulator* center,
                                          const SdkMapExactCellAccumulator* west,
                                          const SdkMapExactCellAccumulator* east,
                                          const SdkMapExactCellAccumulator* north,
                                          const SdkMapExactCellAccumulator* south,
                                          const uint16_t* dry_hist,
                                          const uint16_t* submerged_hist,
                                          int cell_index,
                                          int sea_level)
{
    int water_count;
    int total_count;
    int west_h;
    int east_h;
    int north_h;
    int south_h;
    int center_height;
    BlockType dry_block;
    BlockType submerged_block;
    BlockType land_block;

    if (!center) return 0xFF202020u;
    water_count = (int)center->open_water_count +
                  (int)center->seasonal_ice_count +
                  (int)center->perennial_ice_count;
    total_count = (int)center->dry_count + water_count;
    if (total_count <= 0) return 0xFF202020u;

    dry_block = map_exact_dominant_block(dry_hist, cell_index);
    submerged_block = map_exact_dominant_block(submerged_hist, cell_index);
    land_block = (dry_block != BLOCK_AIR) ? dry_block : submerged_block;
    if (land_block == BLOCK_AIR) land_block = BLOCK_STONE;

    west_h = map_exact_effective_height(west ? west : center);
    east_h = map_exact_effective_height(east ? east : center);
    north_h = map_exact_effective_height(north ? north : center);
    south_h = map_exact_effective_height(south ? south : center);
    center_height = map_exact_effective_height(center);
    return map_color_for_surface_state(land_block,
                                       center_height,
                                       west_h,
                                       east_h,
                                       north_h,
                                       south_h,
                                       sea_level,
                                       (water_count > 0) ? (int)(center->water_depth_sum / (uint32_t)water_count) : 0,
                                       (int)center->dry_count,
                                       water_count,
                                       (int)center->submerged_count,
                                       (int)center->seasonal_ice_count,
                                       (int)center->perennial_ice_count,
                                       0);
}

static int build_superchunk_map_tile_pixels_fallback(SdkMapWorker* worker,
                                                     int origin_x,
                                                     int origin_z,
                                                     uint32_t* out_pixels)
{
    const SdkWorldGenMacroTile* macro_tile;
    const int sample_step = map_cell_blocks();
    const int sample_offset = sample_step / 2;
    SdkTerrainColumnProfile* profiles;
    SdkMapSchedulerInternal* sched;

    if (!worker || !out_pixels) return 0;
    profiles = worker->map_profile_grid;
    sched = worker->owner;
    if (!profiles || !sched) return 0;
    macro_tile = sdk_worldgen_require_macro_tile(&worker->worldgen, origin_x, origin_z);
    if (!macro_tile) return 0;

    for (int mz = 0; mz < HUD_MAP_DIM; ++mz) {
        if (!map_exact_check_scheduler_running(sched)) {
            return 0;
        }

        for (int mx = 0; mx < HUD_MAP_DIM; ++mx) {
            SdkTerrainColumnProfile* center = &profiles[mz * HUD_MAP_DIM + mx];
            int wx = origin_x + mx * sample_step + sample_offset;
            int wz = origin_z + mz * sample_step + sample_offset;

            if (!sample_superchunk_map_profile_from_macro_tile(
                    macro_tile, &worker->worldgen, wx, wz, center)) {
                memset(center, 0, sizeof(*center));
            }
        }
    }

    for (int mz = 0; mz < HUD_MAP_DIM; ++mz) {
        for (int mx = 0; mx < HUD_MAP_DIM; ++mx) {
            const SdkTerrainColumnProfile* center = &profiles[mz * HUD_MAP_DIM + mx];
            const SdkTerrainColumnProfile* west = (mx > 0) ? &profiles[mz * HUD_MAP_DIM + (mx - 1)] : center;
            const SdkTerrainColumnProfile* east =
                (mx + 1 < HUD_MAP_DIM) ? &profiles[mz * HUD_MAP_DIM + (mx + 1)] : center;
            const SdkTerrainColumnProfile* north = (mz > 0) ? &profiles[(mz - 1) * HUD_MAP_DIM + mx] : center;
            const SdkTerrainColumnProfile* south =
                (mz + 1 < HUD_MAP_DIM) ? &profiles[(mz + 1) * HUD_MAP_DIM + mx] : center;

            out_pixels[mz * HUD_MAP_DIM + mx] =
                sdk_map_color_for_profiles(center, west, east, north, south, worker->worldgen.desc.sea_level);
        }
    }

    return 1;
}

static int build_superchunk_map_tile_pixels_exact(SdkMapWorker* worker,
                                                  int origin_x,
                                                  int origin_z,
                                                  uint32_t* out_pixels,
                                                  int* out_chunk_count)
{
    SdkMapSchedulerInternal* sched;
    SdkTerrainColumnProfile* scratch_profiles;
    SdkWorldGenSurfaceColumn* scratch_columns;
    const int cell_blocks = map_cell_blocks();
    const int tile_blocks = session_map_superchunk_tile_blocks();
    const int halo_min_x = origin_x - cell_blocks;
    const int halo_min_z = origin_z - cell_blocks;
    const int halo_max_x = origin_x + tile_blocks + cell_blocks - 1;
    const int halo_max_z = origin_z + tile_blocks + cell_blocks - 1;
    const int origin_cx = map_floor_div_local_coord(origin_x, CHUNK_WIDTH);
    const int origin_cz = map_floor_div_local_coord(origin_z, CHUNK_DEPTH);
    int chunk_count = 0;

    if (!worker || !out_pixels || !worker->map_exact_cell_grid ||
        !worker->map_exact_dry_hist || !worker->map_exact_submerged_hist ||
        !worker->map_exact_chunk_profiles || !worker->map_exact_chunk_surface) {
        return 0;
    }
    sched = worker->owner;
    scratch_profiles = worker->map_exact_chunk_profiles;
    scratch_columns = worker->map_exact_chunk_surface;
    if (!sched) return 0;

    map_exact_reset_worker_scratch(worker);

    for (int cz = origin_cz - 1; cz <= origin_cz + SDK_SUPERCHUNK_WALL_PERIOD; ++cz) {
        if (!map_exact_check_scheduler_running(sched)) {
            return 0;
        }
        for (int cx = origin_cx - 1; cx <= origin_cx + SDK_SUPERCHUNK_WALL_PERIOD; ++cx) {
            int wx0 = cx * CHUNK_WIDTH;
            int wz0 = cz * CHUNK_DEPTH;
            int lx_start;
            int lx_end;
            int lz_start;
            int lz_end;

            if (!sdk_worldgen_generate_chunk_surface_ctx(&worker->worldgen,
                                                         cx,
                                                         cz,
                                                         scratch_profiles,
                                                         scratch_columns)) {
                return 0;
            }
            chunk_count++;

            lx_start = max(0, halo_min_x - wx0);
            lx_end = min(CHUNK_WIDTH - 1, halo_max_x - wx0);
            lz_start = max(0, halo_min_z - wz0);
            lz_end = min(CHUNK_DEPTH - 1, halo_max_z - wz0);
            if (lx_start > lx_end || lz_start > lz_end) {
                continue;
            }

            for (int lz = lz_start; lz <= lz_end; ++lz) {
                int wz = wz0 + lz;
                int cell_z = map_floor_div_local_coord(wz - origin_z, cell_blocks) + 1;
                if (cell_z < 0 || cell_z >= SDK_MAP_EXACT_TILE_DIM) continue;
                for (int lx = lx_start; lx <= lx_end; ++lx) {
                    int wx = wx0 + lx;
                    int cell_x = map_floor_div_local_coord(wx - origin_x, cell_blocks) + 1;
                    const SdkWorldGenSurfaceColumn* column;
                    SdkMapExactColumnSample sample;

                    if (cell_x < 0 || cell_x >= SDK_MAP_EXACT_TILE_DIM) continue;
                    column = &scratch_columns[lz * CHUNK_WIDTH + lx];
                    if (!map_exact_classify_surface_column(column,
                                                           &scratch_profiles[lz * CHUNK_WIDTH + lx],
                                                           &sample)) {
                        int cell_index = cell_z * SDK_MAP_EXACT_TILE_DIM + cell_x;
                        worker->map_exact_cell_grid[cell_index].void_count++;
                        continue;
                    }
                    map_exact_accumulate_column(worker, cell_x, cell_z, &sample);
                }
            }
        }
    }

    for (int mz = 0; mz < HUD_MAP_DIM; ++mz) {
        for (int mx = 0; mx < HUD_MAP_DIM; ++mx) {
            int cell_x = mx + 1;
            int cell_z = mz + 1;
            int cell_index = cell_z * SDK_MAP_EXACT_TILE_DIM + cell_x;
            const SdkMapExactCellAccumulator* center = &worker->map_exact_cell_grid[cell_index];
            const SdkMapExactCellAccumulator* west = &worker->map_exact_cell_grid[cell_index - 1];
            const SdkMapExactCellAccumulator* east = &worker->map_exact_cell_grid[cell_index + 1];
            const SdkMapExactCellAccumulator* north = &worker->map_exact_cell_grid[cell_index - SDK_MAP_EXACT_TILE_DIM];
            const SdkMapExactCellAccumulator* south = &worker->map_exact_cell_grid[cell_index + SDK_MAP_EXACT_TILE_DIM];

            out_pixels[mz * HUD_MAP_DIM + mx] =
                map_color_for_exact_cells(center,
                                          west,
                                          east,
                                          north,
                                          south,
                                          worker->map_exact_dry_hist,
                                          worker->map_exact_submerged_hist,
                                          cell_index,
                                          worker->worldgen.desc.sea_level);
        }
    }

    if (out_chunk_count) *out_chunk_count = chunk_count;
    return 1;
}

int build_superchunk_map_tile_pixels(SdkMapWorker* worker,
                                     int origin_x,
                                     int origin_z,
                                     uint32_t* out_pixels,
                                     int* out_chunk_count)
{
    SdkMapSchedulerInternal* sched;

    if (out_chunk_count) *out_chunk_count = 0;
    if (!worker || !out_pixels) return 0;
    sched = worker->owner;
    if (!sched) return 0;
    if (sched->build_kind == SDK_MAP_BUILD_EXACT_OFFLINE) {
        return build_superchunk_map_tile_pixels_exact(worker, origin_x, origin_z, out_pixels, out_chunk_count);
    }
    return build_superchunk_map_tile_pixels_fallback(worker, origin_x, origin_z, out_pixels);
}

DWORD WINAPI superchunk_map_worker_proc(LPVOID param)
{
    SdkMapWorker* worker = (SdkMapWorker*)param;
    SdkMapSchedulerInternal* sched;

    if (!worker) return 0;
    sched = worker->owner;
    if (!sched) return 0;

    if (sched->build_kind == SDK_MAP_BUILD_EXACT_OFFLINE &&
        sched->mode == SDK_MAP_SCHED_MODE_OFFLINE_BULK) {
        for (;;) {
            SdkMapOfflinePageJob page_job;
            int have_page_job = 0;
            int remaining_tiles = 0;

            memset(&page_job, 0, sizeof(page_job));
            EnterCriticalSection(&sched->lock);
            while (sched->running && sched->page_job_count == 0) {
                SleepConditionVariableCS(&sched->jobs_cv, &sched->lock, INFINITE);
            }
            if (!sched->running && sched->page_job_count == 0) {
                LeaveCriticalSection(&sched->lock);
                break;
            }
            have_page_job = map_take_next_page_job_locked(sched, &page_job);
            if (have_page_job) {
                sched->bulk_active_workers++;
            }
            LeaveCriticalSection(&sched->lock);

            if (!have_page_job) {
                continue;
            }

            remaining_tiles = page_job.tile_count;
            {
                const int batch_span = map_macro_tile_superchunk_span();
                const int tile_blocks = session_map_superchunk_tile_blocks();
                for (int local_z = 0; local_z < batch_span; ++local_z) {
                    for (int local_x = 0; local_x < batch_span; ++local_x) {
                        int scx = page_job.page_scx * batch_span + local_x;
                        int scz = page_job.page_scz * batch_span + local_z;
                        int origin_x = scx * tile_blocks;
                        int origin_z = scz * tile_blocks;
                        uint32_t local_pixels[HUD_MAP_PIXEL_COUNT];
                        int chunk_count = 0;
                        uint64_t start_ms;
                        uint64_t elapsed_ms;
                        int still_running;

                        if (superchunk_map_tile_exists_on_disk_for_identity(
                                sched->world_save_id, sched->world_seed, origin_x, origin_z)) {
                            EnterCriticalSection(&sched->lock);
                            if (sched->bulk_tiles_inflight > 0) {
                                sched->bulk_tiles_inflight--;
                            }
                            LeaveCriticalSection(&sched->lock);
                            remaining_tiles--;
                            continue;
                        }

                        start_ms = GetTickCount64();
                        if (!build_superchunk_map_tile_pixels(worker,
                                                              origin_x,
                                                              origin_z,
                                                              local_pixels,
                                                              &chunk_count)) {
                            goto offline_page_complete;
                        }
                        save_superchunk_map_tile_to_disk_for_identity(sched->world_save_id,
                                                                      sched->world_seed,
                                                                      origin_x,
                                                                      origin_z,
                                                                      (uint32_t)SDK_MAP_BUILD_EXACT_OFFLINE,
                                                                      local_pixels);
                        elapsed_ms = GetTickCount64() - start_ms;

                        EnterCriticalSection(&sched->lock);
                        if (sched->bulk_tiles_inflight > 0) {
                            sched->bulk_tiles_inflight--;
                        }
                        sched->bulk_tiles_completed++;
                        sched->bulk_last_tile_chunks = chunk_count;
                        sched->bulk_last_tile_build_ms = (float)elapsed_ms;
                        still_running = sched->running ? 1 : 0;
                        LeaveCriticalSection(&sched->lock);

                        worker->last_origin_x = origin_x;
                        worker->last_origin_z = origin_z;
                        worker->has_last_origin = true;
                        remaining_tiles--;
                        if (!still_running) {
                            goto offline_page_complete;
                        }
                    }
                }
            }

offline_page_complete:
            EnterCriticalSection(&sched->lock);
            sched->bulk_active_workers = max(0, sched->bulk_active_workers - 1);
            if (remaining_tiles > 0) {
                sched->bulk_tiles_inflight = max(0, sched->bulk_tiles_inflight - remaining_tiles);
            }
            LeaveCriticalSection(&sched->lock);
        }
        return 0;
    }

    for (;;) {
        int slot_index = -1;
        int origin_x = 0;
        int origin_z = 0;
        uint32_t local_pixels[64 * 64];

        EnterCriticalSection(&sched->lock);
        while (sched->running && sched->job_count == 0) {
            SleepConditionVariableCS(&sched->jobs_cv, &sched->lock, INFINITE);
        }

        if (!sched->running && sched->job_count == 0) {
            LeaveCriticalSection(&sched->lock);
            break;
        }

        if (sched->job_count > 0) {
            slot_index = map_take_next_job_locked(sched, worker);
            if (slot_index >= 0 && slot_index < HUD_MAP_CACHE_SLOTS &&
                g_superchunk_map_cache[slot_index].valid &&
                g_superchunk_map_cache[slot_index].state == SDK_MAP_TILE_QUEUED) {
                g_superchunk_map_cache[slot_index].state = SDK_MAP_TILE_BUILDING;
                origin_x = g_superchunk_map_cache[slot_index].origin_x;
                origin_z = g_superchunk_map_cache[slot_index].origin_z;
                worker->last_origin_x = origin_x;
                worker->last_origin_z = origin_z;
                worker->has_last_origin = true;
            } else {
                slot_index = -1;
            }
        }
        LeaveCriticalSection(&sched->lock);

        if (slot_index < 0) {
            continue;
        }

        if (!build_superchunk_map_tile_pixels(worker, origin_x, origin_z, local_pixels, NULL)) {
            continue;
        }

        EnterCriticalSection(&sched->lock);
        if (slot_index >= 0 && slot_index < HUD_MAP_CACHE_SLOTS &&
            g_superchunk_map_cache[slot_index].valid &&
            g_superchunk_map_cache[slot_index].origin_x == origin_x &&
            g_superchunk_map_cache[slot_index].origin_z == origin_z &&
            g_superchunk_map_cache[slot_index].state == SDK_MAP_TILE_BUILDING) {
            memcpy(g_superchunk_map_cache[slot_index].pixels, local_pixels, sizeof(local_pixels));
            g_superchunk_map_cache[slot_index].state = SDK_MAP_TILE_READY;
            g_superchunk_map_cache[slot_index].build_kind = (uint8_t)SDK_MAP_BUILD_INTERACTIVE_FALLBACK;
            g_superchunk_map_cache[slot_index].stamp = g_superchunk_map_stamp++;
        }
        LeaveCriticalSection(&sched->lock);
    }

    return 0;
}

int init_superchunk_map_scheduler(const SdkMapSchedulerConfig* config)
{
    SdkMapSchedulerConfig resolved;
    int i;

    if (!config) return 0;
    if (g_map_scheduler.initialized) return 1;
    map_debug_compare_shutdown();

    memset(&resolved, 0, sizeof(resolved));
    resolved = *config;
    if (resolved.worker_count <= 0) {
        resolved.worker_count = choose_map_worker_count();
    }
    if (resolved.worker_count < 1) resolved.worker_count = 1;
    if (resolved.worker_count > SDK_MAP_MAX_WORKERS) {
        resolved.worker_count = SDK_MAP_MAX_WORKERS;
    }

    memset(&g_map_scheduler, 0, sizeof(g_map_scheduler));
    memset(g_superchunk_map_cache, 0, sizeof(g_superchunk_map_cache));
    g_superchunk_map_stamp = 1u;

    InitializeCriticalSection(&g_map_scheduler.lock);
    InitializeConditionVariable(&g_map_scheduler.jobs_cv);
    g_map_scheduler.running = true;
    g_map_scheduler.worker_count = resolved.worker_count;
    g_map_scheduler.mode = resolved.mode;
    g_map_scheduler.build_kind = resolved.build_kind;
    g_map_scheduler.world_desc = resolved.world_desc;
    g_map_scheduler.world_seed = resolved.world_seed ? resolved.world_seed : resolved.world_desc.seed;
    strcpy_s(g_map_scheduler.world_save_id, sizeof(g_map_scheduler.world_save_id), resolved.world_save_id);
    reset_map_bulk_cursor_locked(&g_map_scheduler);
    g_map_scheduler.initialized = true;

    for (i = 0; i < g_map_scheduler.worker_count; ++i) {
        SdkMapWorker* worker = &g_map_scheduler.workers[i];
        worker->worker_index = i;
        worker->owner = &g_map_scheduler;
        if (resolved.build_kind == SDK_MAP_BUILD_INTERACTIVE_FALLBACK) {
            worker->map_profile_grid =
                (SdkTerrainColumnProfile*)malloc(sizeof(SdkTerrainColumnProfile) * HUD_MAP_PIXEL_COUNT);
            if (!worker->map_profile_grid) {
                request_shutdown_superchunk_map_scheduler();
                while (!poll_shutdown_superchunk_map_scheduler()) {
                    Sleep(0);
                }
                return 0;
            }
        } else {
            worker->map_exact_cell_grid =
                (SdkMapExactCellAccumulator*)malloc(sizeof(SdkMapExactCellAccumulator) *
                                                    SDK_MAP_EXACT_TILE_CELL_COUNT);
            worker->map_exact_dry_hist =
                (uint16_t*)malloc(sizeof(uint16_t) * SDK_MAP_EXACT_TILE_CELL_COUNT * BLOCK_COUNT);
            worker->map_exact_submerged_hist =
                (uint16_t*)malloc(sizeof(uint16_t) * SDK_MAP_EXACT_TILE_CELL_COUNT * BLOCK_COUNT);
            worker->map_exact_chunk_profiles =
                (SdkTerrainColumnProfile*)malloc(sizeof(SdkTerrainColumnProfile) * CHUNK_BLOCKS_PER_LAYER);
            worker->map_exact_chunk_surface =
                (SdkWorldGenSurfaceColumn*)malloc(sizeof(SdkWorldGenSurfaceColumn) * CHUNK_BLOCKS_PER_LAYER);
            if (!worker->map_exact_cell_grid ||
                !worker->map_exact_dry_hist ||
                !worker->map_exact_submerged_hist ||
                !worker->map_exact_chunk_profiles ||
                !worker->map_exact_chunk_surface) {
                request_shutdown_superchunk_map_scheduler();
                while (!poll_shutdown_superchunk_map_scheduler()) {
                    Sleep(0);
                }
                return 0;
            }
        }
        sdk_worldgen_init_ex(&worker->worldgen, &resolved.world_desc, SDK_WORLDGEN_CACHE_DISK);
        if (!worker->worldgen.impl) {
            request_shutdown_superchunk_map_scheduler();
            while (!poll_shutdown_superchunk_map_scheduler()) {
                Sleep(0);
            }
            return 0;
        }
        worker->thread = CreateThread(NULL, 0u, superchunk_map_worker_proc, worker, 0u, NULL);
        if (!worker->thread) {
            request_shutdown_superchunk_map_scheduler();
            while (!poll_shutdown_superchunk_map_scheduler()) {
                Sleep(0);
            }
            return 0;
        }
    }
    return 1;
}

SuperchunkMapCacheEntry* request_superchunk_map_cache_entry_async(int origin_x, int origin_z, bool allow_generation)
{
    SuperchunkMapCacheEntry* result = NULL;

    if (!g_map_scheduler.initialized) {
        return NULL;
    }
    if (!map_queue_superchunk_map_tile_if_needed(origin_x, origin_z, allow_generation, true, &result)) {
        return NULL;
    }
    return result;
}

int map_queue_superchunk_map_tile_if_needed(int origin_x,
                                            int origin_z,
                                            bool allow_generation,
                                            bool load_ready_from_disk,
                                            SuperchunkMapCacheEntry** out_ready)
{
    SuperchunkMapCacheEntry* best = NULL;
    uint32_t disk_pixels[HUD_MAP_PIXEL_COUNT];
    const char* world_save_id = NULL;
    uint32_t world_seed = 0u;
    uint64_t oldest_stamp = UINT64_MAX;
    int best_index = -1;
    int slot;

    if (out_ready) *out_ready = NULL;
    map_cache_identity(&world_save_id, &world_seed);

    if (g_sdk.disable_map_generation_in_gameplay && allow_generation) {
        allow_generation = false;
    }

    if (!load_ready_from_disk &&
        superchunk_map_tile_exists_on_disk_for_identity(world_save_id, world_seed, origin_x, origin_z)) {
        return 1;
    }

    EnterCriticalSection(&g_map_scheduler.lock);

    for (slot = 0; slot < HUD_MAP_CACHE_SLOTS; ++slot) {
        SuperchunkMapCacheEntry* entry = &g_superchunk_map_cache[slot];
        if (entry->valid && entry->origin_x == origin_x && entry->origin_z == origin_z) {
            entry->stamp = g_superchunk_map_stamp++;
            if (entry->state == SDK_MAP_TILE_READY) {
                if (out_ready) *out_ready = entry;
            }
            LeaveCriticalSection(&g_map_scheduler.lock);
            return 1;
        }
        if (!entry->valid) {
            best = entry;
            best_index = slot;
            break;
        }
        if (entry->state == SDK_MAP_TILE_READY && entry->stamp < oldest_stamp) {
            oldest_stamp = entry->stamp;
            best = entry;
            best_index = slot;
        }
    }

    if (!best || best_index < 0) {
        LeaveCriticalSection(&g_map_scheduler.lock);
        return 0;
    }

    if (load_ready_from_disk) {
        if (load_superchunk_map_tile_from_disk_for_identity(world_save_id, world_seed, origin_x, origin_z, disk_pixels)) {
            memset(best, 0, sizeof(*best));
            best->valid = true;
            best->state = SDK_MAP_TILE_READY;
            best->build_kind = (uint8_t)SDK_MAP_BUILD_EXACT_OFFLINE;
            best->origin_x = origin_x;
            best->origin_z = origin_z;
            best->stamp = g_superchunk_map_stamp++;
            memcpy(best->pixels, disk_pixels, sizeof(best->pixels));
            if (out_ready) *out_ready = best;
            LeaveCriticalSection(&g_map_scheduler.lock);
            return 1;
        }
    }

    if (!allow_generation) {
        LeaveCriticalSection(&g_map_scheduler.lock);
        return 0;
    }

    memset(best, 0, sizeof(*best));
    best->valid = true;
    best->state = SDK_MAP_TILE_QUEUED;
    best->build_kind = (uint8_t)SDK_MAP_BUILD_INTERACTIVE_FALLBACK;
    best->origin_x = origin_x;
    best->origin_z = origin_z;
    best->stamp = g_superchunk_map_stamp++;

    for (slot = 0; slot < HUD_MAP_DIM * HUD_MAP_DIM; ++slot) {
        best->pixels[slot] = 0xFF202020u;
    }

    if (g_map_scheduler.job_count < SDK_MAP_JOB_CAPACITY) {
        int out_index = map_queue_index(g_map_scheduler.job_head, g_map_scheduler.job_count, SDK_MAP_JOB_CAPACITY);
        g_map_scheduler.jobs[out_index] = best_index;
        g_map_scheduler.job_count++;
        WakeConditionVariable(&g_map_scheduler.jobs_cv);
    } else {
        best->valid = false;
        best->state = SDK_MAP_TILE_EMPTY;
        LeaveCriticalSection(&g_map_scheduler.lock);
        return 0;
    }

    LeaveCriticalSection(&g_map_scheduler.lock);
    return 1;
}

void pump_superchunk_map_scheduler_offline_bulk(int max_jobs)
{
    const int batch_span = map_macro_tile_superchunk_span();
    const int tile_blocks = session_map_superchunk_tile_blocks();
    int max_outstanding_pages;

    if (max_jobs < 1) max_jobs = 1;
    max_outstanding_pages = max(1, g_map_scheduler.worker_count * 2);

    for (int i = 0; i < max_jobs; ++i) {
        int batch_x = 0;
        int batch_z = 0;
        int page_ring = 0;
        int missing_tile_count = 0;

        EnterCriticalSection(&g_map_scheduler.lock);
        if (!g_map_scheduler.initialized ||
            !g_map_scheduler.running ||
            g_map_scheduler.mode != SDK_MAP_SCHED_MODE_OFFLINE_BULK ||
            g_map_scheduler.build_kind != SDK_MAP_BUILD_EXACT_OFFLINE) {
            LeaveCriticalSection(&g_map_scheduler.lock);
            return;
        }

        if (g_map_scheduler.page_job_count + g_map_scheduler.bulk_active_workers >= max_outstanding_pages ||
            g_map_scheduler.page_job_count >= SDK_MAP_PAGE_JOB_CAPACITY) {
            LeaveCriticalSection(&g_map_scheduler.lock);
            return;
        }

        map_bulk_cursor_pop_locked(&g_map_scheduler, &batch_x, &batch_z);
        page_ring = g_map_scheduler.bulk_ring;
        LeaveCriticalSection(&g_map_scheduler.lock);

        for (int local_z = 0; local_z < batch_span; ++local_z) {
            for (int local_x = 0; local_x < batch_span; ++local_x) {
                int origin_x =
                    (batch_x * batch_span + local_x) * tile_blocks;
                int origin_z =
                    (batch_z * batch_span + local_z) * tile_blocks;
                if (!superchunk_map_tile_exists_on_disk_for_identity(
                        g_map_scheduler.world_save_id,
                        g_map_scheduler.world_seed,
                        origin_x,
                        origin_z)) {
                    missing_tile_count++;
                }
            }
        }

        if (missing_tile_count <= 0) {
            continue;
        }

        EnterCriticalSection(&g_map_scheduler.lock);
        if (!g_map_scheduler.initialized ||
            !g_map_scheduler.running ||
            g_map_scheduler.mode != SDK_MAP_SCHED_MODE_OFFLINE_BULK ||
            g_map_scheduler.build_kind != SDK_MAP_BUILD_EXACT_OFFLINE ||
            g_map_scheduler.page_job_count >= SDK_MAP_PAGE_JOB_CAPACITY) {
            LeaveCriticalSection(&g_map_scheduler.lock);
            return;
        }
        {
            int out_index = map_page_queue_index(g_map_scheduler.page_job_head, g_map_scheduler.page_job_count);
            SdkMapOfflinePageJob* job = &g_map_scheduler.page_jobs[out_index];
            job->page_scx = batch_x;
            job->page_scz = batch_z;
            job->ring = page_ring;
            job->tile_count = missing_tile_count;
            g_map_scheduler.page_job_count++;
            g_map_scheduler.bulk_tiles_inflight += missing_tile_count;
        }
        WakeConditionVariable(&g_map_scheduler.jobs_cv);
        LeaveCriticalSection(&g_map_scheduler.lock);
    }
}

void get_superchunk_map_scheduler_stats(SdkMapSchedulerStats* out_stats)
{
    if (!out_stats) return;
    memset(out_stats, 0, sizeof(*out_stats));

    if (!g_map_scheduler.initialized) return;

    EnterCriticalSection(&g_map_scheduler.lock);
    out_stats->worker_count = g_map_scheduler.worker_count;
    out_stats->queued_jobs = g_map_scheduler.job_count;
    out_stats->building_jobs = map_scheduler_building_jobs_locked(&g_map_scheduler);
    out_stats->queued_pages = g_map_scheduler.page_job_count;
    out_stats->active_workers = g_map_scheduler.bulk_active_workers;
    out_stats->current_ring = g_map_scheduler.bulk_ring;
    out_stats->tiles_completed = g_map_scheduler.bulk_tiles_completed;
    out_stats->tiles_inflight = g_map_scheduler.bulk_tiles_inflight;
    out_stats->last_tile_chunks = g_map_scheduler.bulk_last_tile_chunks;
    out_stats->last_tile_build_ms = g_map_scheduler.bulk_last_tile_build_ms;
    LeaveCriticalSection(&g_map_scheduler.lock);
}

void push_superchunk_map_ui(float world_x, float world_z)
{
    SdkMapUI mui;
    typedef struct {
        int origin_x;
        int origin_z;
        SuperchunkMapCacheEntry* entry;
    } VisibleMapTile;
    VisibleMapTile visible_tiles[HUD_MAP_VISIBLE_TILE_MAX];
    int visible_tile_count = 0;
    int visible_exact_tiles = 0;
    int visible_fallback_tiles = 0;
    int player_wx;
    int player_wz;
    int focus_origin_x;
    int focus_origin_z;
    float center_x;
    float center_z;
    float visible_span_blocks;
    float min_x;
    float min_z;
    const bool allow_tile_generation = !startup_safe_mode_active() || g_map_focus_open;

    memset(&mui, 0, sizeof(mui));
    memset(visible_tiles, 0, sizeof(visible_tiles));

    if (g_craft_open || g_station_open || g_skills_open ||
        g_pause_menu_open || g_command_open) {
        sdk_renderer_set_map(&mui);
        return;
    }

    player_wx = (int)floorf(world_x);
    player_wz = (int)floorf(world_z);

    if (!g_map_focus_initialized) {
        g_map_focus_world_x = world_x;
        g_map_focus_world_z = world_z;
        g_map_focus_initialized = true;
    }

    mui.open = true;
    mui.focused = g_map_focus_open;
    mui.width = HUD_MAP_DIM;
    mui.height = HUD_MAP_DIM;
    if (g_map_zoom_tenths < 1) g_map_zoom_tenths = 1;
    if (g_map_zoom_tenths > HUD_MAP_MAX_ZOOM_TENTHS) g_map_zoom_tenths = HUD_MAP_MAX_ZOOM_TENTHS;
    if (g_map_zoom_tenths > 500) {
        g_map_zoom_tenths = 500;
    }
    mui.zoom_tenths = g_map_zoom_tenths;

    center_x = g_map_focus_open ? g_map_focus_world_x : world_x;
    center_z = g_map_focus_open ? g_map_focus_world_z : world_z;
    if (g_map_focus_open) {
        visible_span_blocks = (float)session_map_superchunk_tile_blocks() * ((float)g_map_zoom_tenths / 10.0f);
    } else {
        visible_span_blocks = (float)(HUD_MINIMAP_SUPERCHUNK_GRID * session_map_superchunk_tile_blocks());
    }
    if (visible_span_blocks < (float)map_cell_blocks()) visible_span_blocks = (float)map_cell_blocks();
    min_x = center_x - visible_span_blocks * 0.5f;
    min_z = center_z - visible_span_blocks * 0.5f;

    mui.player_cell_x = (int)floorf(((world_x - min_x) / visible_span_blocks) * (float)HUD_MAP_DIM);
    mui.player_cell_z = (int)floorf(((world_z - min_z) / visible_span_blocks) * (float)HUD_MAP_DIM);
    mui.focus_cell_x = HUD_MAP_DIM / 2;
    mui.focus_cell_z = HUD_MAP_DIM / 2;
    focus_origin_x = superchunk_origin_for_world((int)floorf(center_x));
    focus_origin_z = superchunk_origin_for_world((int)floorf(center_z));

    for (int mz = 0; mz < HUD_MAP_DIM; ++mz) {
        for (int mx = 0; mx < HUD_MAP_DIM; ++mx) {
            float sx = min_x + (((float)mx + 0.5f) / (float)HUD_MAP_DIM) * visible_span_blocks;
            float sz = min_z + (((float)mz + 0.5f) / (float)HUD_MAP_DIM) * visible_span_blocks;
            int wx = (int)floorf(sx);
            int wz = (int)floorf(sz);
            int origin_x;
            int origin_z;
            int local_x;
            int local_z;
            int tile_x;
            int tile_z;
            SuperchunkMapCacheEntry* entry;
            uint32_t color = 0xFF202028u;

            origin_x = superchunk_origin_for_world(wx);
            origin_z = superchunk_origin_for_world(wz);
            local_x = spawn_floor_mod_i(wx, session_map_superchunk_tile_blocks());
            local_z = spawn_floor_mod_i(wz, session_map_superchunk_tile_blocks());

            if (map_is_wall_band_local(local_x, local_z)) {
                color = map_wall_color_for_local(local_x, local_z);
            } else {
                int tile_slot = -1;
                for (int i = 0; i < visible_tile_count; ++i) {
                    if (visible_tiles[i].origin_x == origin_x && visible_tiles[i].origin_z == origin_z) {
                        tile_slot = i;
                        break;
                    }
                }
                if (tile_slot < 0 && visible_tile_count < HUD_MAP_VISIBLE_TILE_MAX) {
                    visible_tiles[visible_tile_count].origin_x = origin_x;
                    visible_tiles[visible_tile_count].origin_z = origin_z;
                    visible_tiles[visible_tile_count].entry =
                        request_superchunk_map_cache_entry_async(origin_x, origin_z, allow_tile_generation);
                    tile_slot = visible_tile_count;
                    visible_tile_count++;
                }
                entry = (tile_slot >= 0) ? visible_tiles[tile_slot].entry : NULL;
                if (entry) {
                    tile_x = local_x / map_cell_blocks();
                    tile_z = local_z / map_cell_blocks();
                    if (tile_x < 0) tile_x = 0;
                    if (tile_x >= HUD_MAP_DIM) tile_x = HUD_MAP_DIM - 1;
                    if (tile_z < 0) tile_z = 0;
                    if (tile_z >= HUD_MAP_DIM) tile_z = HUD_MAP_DIM - 1;
                    color = entry->pixels[tile_z * HUD_MAP_DIM + tile_x];
                }
            }
            mui.pixels[mz * HUD_MAP_DIM + mx] = color;
        }
    }

    for (int i = 0; i < visible_tile_count; ++i) {
        SuperchunkMapCacheEntry* entry = visible_tiles[i].entry;
        if (!entry || entry->state != SDK_MAP_TILE_READY) continue;
        if (entry->build_kind == (uint8_t)SDK_MAP_BUILD_EXACT_OFFLINE) {
            visible_exact_tiles++;
        } else {
            visible_fallback_tiles++;
        }
        if (visible_tiles[i].origin_x == focus_origin_x &&
            visible_tiles[i].origin_z == focus_origin_z) {
            mui.focused_tile_ready = true;
            mui.focused_tile_build_kind = entry->build_kind;
            if (map_debug_compare_tile(focus_origin_x, focus_origin_z, &entry->compare_diff_pixels)) {
                entry->compare_valid = 1u;
            }
            if (entry->compare_valid) {
                mui.focused_tile_compare_valid = true;
                mui.focused_tile_compare_diff_pixels = entry->compare_diff_pixels;
            }
        }
    }
    mui.visible_exact_tiles = visible_exact_tiles;
    mui.visible_fallback_tiles = visible_fallback_tiles;

    sdk_renderer_set_map(&mui);
}
