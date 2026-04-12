/**
 * sdk_simulation.c -- Sparse runtime material simulation.
 */
#include "sdk_simulation.h"
#include "../Blocks/sdk_block.h"

#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SDK_SIM_INITIAL_CAPACITY 64u
#define SDK_SIM_INITIAL_QUEUE_CAPACITY 128u
#define SDK_SIM_MAX_FLUID_TRANSFER 160u
#define SDK_SIM_WATER_WAKE_SURFACE_TOLERANCE 2
#define SDK_SIM_WATER_WAKE_MIN_DEPTH_BELOW_BREACH 4
#define SDK_SIM_WATER_WAKE_MAX_COLUMNS 8192u
#define SDK_SIM_BULK_WATER_MIN_COLUMNS 96u
#define SDK_SIM_BULK_WATER_MAX_COLUMNS 32768u
#define SDK_SIM_BULK_WATER_MAX_WORKERS 16
#define SDK_SIM_BULK_WATER_PARALLEL_MIN_COLUMNS 4096u
#define SDK_SIM_FORCE_BULK_MIN_COLUMNS 256u
#define SDK_SIM_DIRTY_MARK_WORDS (CHUNK_TOTAL_BLOCKS / 32u)
#define SDK_SIM_EMPTY_LOOKUP_KEY UINT32_MAX
#define SDK_SIM_FLUID_LOOKUP_MIN_CAPACITY 128u
#define SDK_SIM_ADAPTIVE_BUDGET_MAX_MULTIPLIER 8
#define SDK_SIM_PROACTIVE_BULK_SCAN_CHUNKS 24
#define SDK_SIM_PROACTIVE_BULK_SCAN_CELLS 24

typedef struct {
    int   wx;
    int   wz;
    int   barrier_y;
    float current_surface_e;
    float initial_surface_e;
    uint8_t cap_block;
    uint8_t flags;
} SdkBulkWaterColumn;

typedef struct {
    const SdkBulkWaterColumn* cols;
    uint32_t begin_index;
    uint32_t end_index;
    float probe_surface_e;
    float volume_sum;
    float min_base_e;
} SdkBulkWaterSumTask;

#define SDK_SIM_RESERVOIR_JOB_CAPACITY 8
#define SDK_SIM_RESERVOIR_RESULT_CAPACITY 8
#define SDK_SIM_MAX_ACTIVE_RESERVOIRS 4
#define SDK_SIM_MAX_STAGE_COUNT 8u
#define SDK_SIM_RESERVOIR_STATUS_COMPLETE                 0u
#define SDK_SIM_RESERVOIR_STATUS_TRUNCATED_LOADED_BOUNDS  (1u << 0)
#define SDK_SIM_RESERVOIR_STATUS_TRUNCATED_MAX_COLUMNS    (1u << 1)
#define SDK_SIM_RESERVOIR_STATUS_DEFERRED                 (1u << 2)
#define SDK_SIM_RESERVOIR_STATUS_STALE_RESULT             (1u << 3)

typedef struct {
    uint32_t     generation;
    int          seed_wx;
    int          seed_wy;
    int          seed_wz;
    uint32_t     reservoir_count;
    uint32_t     total_count;
    uint32_t     status_flags;
    SdkBulkWaterColumn* cols;
} SdkReservoirJob;

typedef struct {
    uint32_t     generation;
    int          seed_wx;
    int          seed_wy;
    int          seed_wz;
    uint32_t     reservoir_count;
    uint32_t     total_count;
    uint32_t     status_flags;
    int          worker_count;
    float        total_volume;
    float        target_surface_e;
    float        solve_ms;
    uint32_t     stage_count;
    SdkBulkWaterColumn* cols;
} SdkReservoirResult;

typedef struct {
    int          active;
    uint32_t     generation;
    int          seed_wx;
    int          seed_wy;
    int          seed_wz;
    uint32_t     reservoir_count;
    uint32_t     total_count;
    uint32_t     status_flags;
    int          worker_count;
    float        total_volume;
    float        target_surface_e;
    float        solve_ms;
    uint32_t     stage_count;
    uint32_t     stage_index;
    float        last_stage_surface_e;
    SdkBulkWaterColumn* cols;
} SdkActiveReservoir;

typedef struct {
    CRITICAL_SECTION    lock;
    CONDITION_VARIABLE  jobs_cv;
    HANDLE              thread;
    int                 running;
    int                 started;
    uint32_t            topo_generation;
    SdkReservoirJob     jobs[SDK_SIM_RESERVOIR_JOB_CAPACITY];
    int                 job_head;
    int                 job_count;
    SdkReservoirResult  results[SDK_SIM_RESERVOIR_RESULT_CAPACITY];
    int                 result_head;
    int                 result_count;
    SdkActiveReservoir  active[SDK_SIM_MAX_ACTIVE_RESERVOIRS];
} SdkReservoirScheduler;

static uint32_t g_sim_round_robin_cursor = 0u;
static SdkFluidDebugInfo g_fluid_debug_info = { 0 };
static LARGE_INTEGER g_fluid_debug_qpc_freq = { 0 };
static SdkReservoirScheduler g_reservoir_scheduler = { 0 };

static int fluid_lookup_reserve(SdkChunkSimState* state, uint32_t required_cells);
static void fluid_lookup_clear(SdkChunkSimState* state);
static int fluid_lookup_get_index(const SdkChunkSimState* state, SdkSimCellKey key);
static int fluid_lookup_set_index(SdkChunkSimState* state, SdkSimCellKey key, uint32_t index);
static void fluid_lookup_remove_key(SdkChunkSimState* state, SdkSimCellKey key);

static double fluid_debug_now_ms(void)
{
    LARGE_INTEGER now;
    if (g_fluid_debug_qpc_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&g_fluid_debug_qpc_freq);
        if (g_fluid_debug_qpc_freq.QuadPart == 0) return (double)GetTickCount64();
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)g_fluid_debug_qpc_freq.QuadPart;
}

static void fluid_debug_set_reason(const char* reason)
{
    if (!reason) reason = "NONE";
    strncpy_s(g_fluid_debug_info.reason, sizeof(g_fluid_debug_info.reason), reason, _TRUNCATE);
}

static int dirty_mark_is_set(const SdkChunkSimState* state, SdkSimCellKey key)
{
    uint32_t word_index;
    uint32_t bit;

    if (!state || !state->dirty_marks) return 0;
    word_index = ((uint32_t)key) >> 5u;
    if (word_index >= state->dirty_mark_words) return 0;
    bit = 1u << (((uint32_t)key) & 31u);
    return (state->dirty_marks[word_index] & bit) != 0u;
}

static void dirty_mark_set(SdkChunkSimState* state, SdkSimCellKey key)
{
    uint32_t word_index;
    uint32_t bit;

    if (!state || !state->dirty_marks) return;
    word_index = ((uint32_t)key) >> 5u;
    if (word_index >= state->dirty_mark_words) return;
    bit = 1u << (((uint32_t)key) & 31u);
    state->dirty_marks[word_index] |= bit;
}

static void dirty_mark_clear(SdkChunkSimState* state, SdkSimCellKey key)
{
    uint32_t word_index;
    uint32_t bit;

    if (!state || !state->dirty_marks) return;
    word_index = ((uint32_t)key) >> 5u;
    if (word_index >= state->dirty_mark_words) return;
    bit = 1u << (((uint32_t)key) & 31u);
    state->dirty_marks[word_index] &= ~bit;
}

static int sim_ring_index(int head, int count, int capacity)
{
    return (head + count) % capacity;
}

SdkSimCellKey sdk_simulation_pack_local_key(int lx, int ly, int lz)
{
    return (SdkSimCellKey)(((uint32_t)lx & 0x3Fu) |
                           (((uint32_t)lz & 0x3Fu) << 6u) |
                           (((uint32_t)ly & 0x3FFu) << 12u));
}

void sdk_simulation_unpack_local_key(SdkSimCellKey key, int* out_lx, int* out_ly, int* out_lz)
{
    if (out_lx) *out_lx = (int)(key & 0x3Fu);
    if (out_lz) *out_lz = (int)((key >> 6u) & 0x3Fu);
    if (out_ly) *out_ly = (int)((key >> 12u) & 0x3FFu);
}

SdkChunkSimState* sdk_simulation_chunk_state_create(void)
{
    SdkChunkSimState* state = (SdkChunkSimState*)calloc(1, sizeof(SdkChunkSimState));
    if (!state) return NULL;
    state->dirty_mark_words = SDK_SIM_DIRTY_MARK_WORDS;
    state->dirty_marks = (uint32_t*)calloc((size_t)state->dirty_mark_words, sizeof(uint32_t));
    if (!state->dirty_marks) {
        free(state);
        return NULL;
    }
    return state;
}

void sdk_simulation_chunk_state_clear(SdkChunkSimState* state)
{
    if (!state) return;
    state->fluid_count = 0u;
    state->loose_count = 0u;
    state->dirty_head = 0u;
    state->dirty_count = 0u;
    fluid_lookup_clear(state);
    if (state->dirty_marks && state->dirty_mark_words > 0u) {
        memset(state->dirty_marks, 0, (size_t)state->dirty_mark_words * sizeof(uint32_t));
    }
}

void sdk_simulation_chunk_state_destroy(SdkChunkSimState* state)
{
    if (!state) return;
    free(state->fluid_cells);
    free(state->fluid_lookup_keys);
    free(state->fluid_lookup_indices);
    free(state->loose_cells);
    free(state->dirty_queue);
    free(state->dirty_marks);
    free(state);
}

SdkChunkSimState* sdk_simulation_clone_chunk_state_for_snapshot(const SdkChunkSimState* src)
{
    SdkChunkSimState* dst;

    if (!src) return NULL;

    dst = sdk_simulation_chunk_state_create();
    if (!dst) return NULL;

    if (src->fluid_count > 0u && src->fluid_cells) {
        dst->fluid_cells = (SdkFluidCellState*)malloc((size_t)src->fluid_count * sizeof(SdkFluidCellState));
        if (!dst->fluid_cells) goto fail;
        memcpy(dst->fluid_cells, src->fluid_cells, (size_t)src->fluid_count * sizeof(SdkFluidCellState));
        dst->fluid_count = src->fluid_count;
        dst->fluid_capacity = src->fluid_count;
    }

    if (src->fluid_lookup_capacity > 0u && src->fluid_lookup_keys && src->fluid_lookup_indices) {
        dst->fluid_lookup_keys = (SdkSimCellKey*)malloc((size_t)src->fluid_lookup_capacity * sizeof(SdkSimCellKey));
        dst->fluid_lookup_indices = (uint32_t*)malloc((size_t)src->fluid_lookup_capacity * sizeof(uint32_t));
        if (!dst->fluid_lookup_keys || !dst->fluid_lookup_indices) goto fail;
        memcpy(dst->fluid_lookup_keys,
               src->fluid_lookup_keys,
               (size_t)src->fluid_lookup_capacity * sizeof(SdkSimCellKey));
        memcpy(dst->fluid_lookup_indices,
               src->fluid_lookup_indices,
               (size_t)src->fluid_lookup_capacity * sizeof(uint32_t));
        dst->fluid_lookup_capacity = src->fluid_lookup_capacity;
    }

    if (src->loose_count > 0u && src->loose_cells) {
        dst->loose_cells = (SdkLooseCellState*)malloc((size_t)src->loose_count * sizeof(SdkLooseCellState));
        if (!dst->loose_cells) goto fail;
        memcpy(dst->loose_cells, src->loose_cells, (size_t)src->loose_count * sizeof(SdkLooseCellState));
        dst->loose_count = src->loose_count;
        dst->loose_capacity = src->loose_count;
    }

    return dst;

fail:
    sdk_simulation_chunk_state_destroy(dst);
    return NULL;
}

static int reserve_bytes(void** io_ptr, uint32_t elem_size, uint32_t* io_cap, uint32_t required)
{
    void* resized;
    uint32_t new_cap;

    if (!io_ptr || !io_cap) return 0;
    if (required <= *io_cap) return 1;

    new_cap = (*io_cap > 0u) ? *io_cap : SDK_SIM_INITIAL_CAPACITY;
    while (new_cap < required) new_cap *= 2u;

    resized = realloc(*io_ptr, (size_t)new_cap * (size_t)elem_size);
    if (!resized) return 0;
    *io_ptr = resized;
    *io_cap = new_cap;
    return 1;
}

static int queue_reserve(SdkChunkSimState* state, uint32_t required)
{
    SdkSimCellKey* new_queue;
    uint32_t i;
    uint32_t new_cap;

    if (!state) return 0;
    if (required <= state->dirty_capacity) return 1;

    new_cap = state->dirty_capacity ? state->dirty_capacity : SDK_SIM_INITIAL_QUEUE_CAPACITY;
    while (new_cap < required) new_cap *= 2u;

    new_queue = (SdkSimCellKey*)malloc((size_t)new_cap * sizeof(SdkSimCellKey));
    if (!new_queue) return 0;

    for (i = 0u; i < state->dirty_count; ++i) {
        new_queue[i] = state->dirty_queue[(state->dirty_head + i) % state->dirty_capacity];
    }

    free(state->dirty_queue);
    state->dirty_queue = new_queue;
    state->dirty_capacity = new_cap;
    state->dirty_head = 0u;
    return 1;
}

static uint32_t fluid_lookup_hash(SdkSimCellKey key)
{
    uint32_t h = (uint32_t)key * 0x9e3779b9u;
    h ^= h >> 16u;
    h *= 0x85ebca6bu;
    h ^= h >> 13u;
    return h;
}

static uint32_t fluid_lookup_find_slot_raw(const SdkSimCellKey* keys,
                                           uint32_t capacity,
                                           SdkSimCellKey key,
                                           int* out_found)
{
    uint32_t mask;
    uint32_t slot;

    if (out_found) *out_found = 0;
    if (!keys || capacity == 0u) return 0u;
    mask = capacity - 1u;
    slot = fluid_lookup_hash(key) & mask;
    for (;;) {
        if (keys[slot] == SDK_SIM_EMPTY_LOOKUP_KEY) {
            return slot;
        }
        if (keys[slot] == key) {
            if (out_found) *out_found = 1;
            return slot;
        }
        slot = (slot + 1u) & mask;
    }
}

static void fluid_lookup_insert_raw(SdkSimCellKey* keys,
                                    uint32_t* indices,
                                    uint32_t capacity,
                                    SdkSimCellKey key,
                                    uint32_t index)
{
    int found = 0;
    uint32_t slot = fluid_lookup_find_slot_raw(keys, capacity, key, &found);
    if (!keys || !indices || capacity == 0u) return;
    keys[slot] = key;
    indices[slot] = index;
}

static void fluid_lookup_clear(SdkChunkSimState* state)
{
    if (!state || !state->fluid_lookup_keys || !state->fluid_lookup_indices || state->fluid_lookup_capacity == 0u) {
        return;
    }
    for (uint32_t i = 0u; i < state->fluid_lookup_capacity; ++i) {
        state->fluid_lookup_keys[i] = SDK_SIM_EMPTY_LOOKUP_KEY;
    }
    memset(state->fluid_lookup_indices, 0xFF, (size_t)state->fluid_lookup_capacity * sizeof(uint32_t));
}

static int fluid_lookup_rebuild(SdkChunkSimState* state, uint32_t new_capacity)
{
    SdkSimCellKey* new_keys;
    uint32_t* new_indices;
    uint32_t i;

    if (!state) return 0;
    if (new_capacity < SDK_SIM_FLUID_LOOKUP_MIN_CAPACITY) {
        new_capacity = SDK_SIM_FLUID_LOOKUP_MIN_CAPACITY;
    }
    if ((new_capacity & (new_capacity - 1u)) != 0u) {
        uint32_t pow2 = SDK_SIM_FLUID_LOOKUP_MIN_CAPACITY;
        while (pow2 < new_capacity) pow2 <<= 1u;
        new_capacity = pow2;
    }

    new_keys = (SdkSimCellKey*)malloc((size_t)new_capacity * sizeof(SdkSimCellKey));
    new_indices = (uint32_t*)malloc((size_t)new_capacity * sizeof(uint32_t));
    if (!new_keys || !new_indices) {
        free(new_keys);
        free(new_indices);
        return 0;
    }

    for (i = 0u; i < new_capacity; ++i) {
        new_keys[i] = SDK_SIM_EMPTY_LOOKUP_KEY;
        new_indices[i] = UINT32_MAX;
    }
    for (i = 0u; i < state->fluid_count; ++i) {
        fluid_lookup_insert_raw(new_keys, new_indices, new_capacity, state->fluid_cells[i].key, i);
    }

    free(state->fluid_lookup_keys);
    free(state->fluid_lookup_indices);
    state->fluid_lookup_keys = new_keys;
    state->fluid_lookup_indices = new_indices;
    state->fluid_lookup_capacity = new_capacity;
    return 1;
}

static int fluid_lookup_reserve(SdkChunkSimState* state, uint32_t required_cells)
{
    uint32_t desired_capacity;

    if (!state) return 0;
    if (required_cells == 0u) {
        fluid_lookup_clear(state);
        return 1;
    }

    desired_capacity = SDK_SIM_FLUID_LOOKUP_MIN_CAPACITY;
    while (desired_capacity < required_cells * 2u) {
        desired_capacity <<= 1u;
    }
    if (desired_capacity <= state->fluid_lookup_capacity) {
        return 1;
    }
    return fluid_lookup_rebuild(state, desired_capacity);
}

static int fluid_lookup_get_index(const SdkChunkSimState* state, SdkSimCellKey key)
{
    int found = 0;
    uint32_t slot;

    if (!state || !state->fluid_lookup_keys || !state->fluid_lookup_indices || state->fluid_lookup_capacity == 0u) {
        return -1;
    }
    slot = fluid_lookup_find_slot_raw(state->fluid_lookup_keys, state->fluid_lookup_capacity, key, &found);
    if (!found) return -1;
    return (int)state->fluid_lookup_indices[slot];
}

static int fluid_lookup_set_index(SdkChunkSimState* state, SdkSimCellKey key, uint32_t index)
{
    int found = 0;
    uint32_t slot;

    if (!state) return 0;
    if (!fluid_lookup_reserve(state, state->fluid_count > 0u ? state->fluid_count : 1u)) return 0;
    slot = fluid_lookup_find_slot_raw(state->fluid_lookup_keys, state->fluid_lookup_capacity, key, &found);
    state->fluid_lookup_keys[slot] = key;
    state->fluid_lookup_indices[slot] = index;
    return 1;
}

static void fluid_lookup_remove_key(SdkChunkSimState* state, SdkSimCellKey key)
{
    int found = 0;
    uint32_t slot;
    uint32_t next;
    uint32_t mask;

    if (!state || !state->fluid_lookup_keys || !state->fluid_lookup_indices || state->fluid_lookup_capacity == 0u) {
        return;
    }

    slot = fluid_lookup_find_slot_raw(state->fluid_lookup_keys, state->fluid_lookup_capacity, key, &found);
    if (!found) return;

    mask = state->fluid_lookup_capacity - 1u;
    state->fluid_lookup_keys[slot] = SDK_SIM_EMPTY_LOOKUP_KEY;
    state->fluid_lookup_indices[slot] = UINT32_MAX;

    next = (slot + 1u) & mask;
    while (state->fluid_lookup_keys[next] != SDK_SIM_EMPTY_LOOKUP_KEY) {
        SdkSimCellKey rehash_key = state->fluid_lookup_keys[next];
        uint32_t rehash_index = state->fluid_lookup_indices[next];
        state->fluid_lookup_keys[next] = SDK_SIM_EMPTY_LOOKUP_KEY;
        state->fluid_lookup_indices[next] = UINT32_MAX;
        fluid_lookup_insert_raw(state->fluid_lookup_keys,
                                state->fluid_lookup_indices,
                                state->fluid_lookup_capacity,
                                rehash_key,
                                rehash_index);
        next = (next + 1u) & mask;
    }
}

static void enqueue_key(SdkChunkSimState* state, SdkSimCellKey key)
{
    uint32_t tail;
    if (!state) return;
    if (dirty_mark_is_set(state, key)) {
        g_fluid_debug_info.dedupe_hits++;
        return;
    }
    if (!queue_reserve(state, state->dirty_count + 1u)) return;
    tail = (state->dirty_head + state->dirty_count) % state->dirty_capacity;
    state->dirty_queue[tail] = key;
    state->dirty_count++;
    dirty_mark_set(state, key);
}

static int dequeue_key(SdkChunkSimState* state, SdkSimCellKey* out_key)
{
    if (!state || !out_key || state->dirty_count == 0u) return 0;
    *out_key = state->dirty_queue[state->dirty_head];
    state->dirty_head = (state->dirty_head + 1u) % state->dirty_capacity;
    state->dirty_count--;
    dirty_mark_clear(state, *out_key);
    return 1;
}

static int find_fluid_index(const SdkChunkSimState* state, SdkSimCellKey key)
{
    return fluid_lookup_get_index(state, key);
}

static int find_loose_index(const SdkChunkSimState* state, SdkSimCellKey key)
{
    uint32_t i;
    if (!state) return -1;
    for (i = 0u; i < state->loose_count; ++i) {
        if (state->loose_cells[i].key == key) return (int)i;
    }
    return -1;
}

static void remove_fluid_local(SdkChunk* chunk, int lx, int ly, int lz)
{
    SdkChunkSimState* state;
    int index;
    uint32_t last_index;
    SdkSimCellKey key;
    if (!chunk || !chunk->sim_state) return;
    state = chunk->sim_state;
    key = sdk_simulation_pack_local_key(lx, ly, lz);
    index = find_fluid_index(state, key);
    if (index < 0) return;
    fluid_lookup_remove_key(state, key);
    last_index = state->fluid_count - 1u;
    if ((uint32_t)index != last_index) {
        state->fluid_cells[index] = state->fluid_cells[last_index];
        fluid_lookup_set_index(state, state->fluid_cells[index].key, (uint32_t)index);
    }
    state->fluid_count = last_index;
}

static void set_partial_fluid_local(SdkChunk* chunk, int lx, int ly, int lz, uint8_t fill, uint8_t material_kind)
{
    SdkChunkSimState* state;
    SdkSimCellKey key;
    int index;

    if (!chunk || !chunk->sim_state || fill == 0u || fill >= 255u) return;
    state = chunk->sim_state;
    key = sdk_simulation_pack_local_key(lx, ly, lz);
    index = find_fluid_index(state, key);
    if (index < 0) {
        if (!reserve_bytes((void**)&state->fluid_cells, sizeof(SdkFluidCellState), &state->fluid_capacity, state->fluid_count + 1u) ||
            !fluid_lookup_reserve(state, state->fluid_count + 1u)) {
            return;
        }
        index = (int)state->fluid_count++;
    }
    state->fluid_cells[index].key = key;
    state->fluid_cells[index].fill = fill;
    state->fluid_cells[index].material_kind = material_kind;
    state->fluid_cells[index].flags = 0u;
    fluid_lookup_set_index(state, key, (uint32_t)index);
}

static void remove_loose_local(SdkChunk* chunk, int lx, int ly, int lz)
{
    SdkChunkSimState* state;
    int index;
    if (!chunk || !chunk->sim_state) return;
    state = chunk->sim_state;
    index = find_loose_index(state, sdk_simulation_pack_local_key(lx, ly, lz));
    if (index < 0) return;
    state->loose_cells[index] = state->loose_cells[state->loose_count - 1u];
    state->loose_count--;
}

static void add_loose_local(SdkChunk* chunk, int lx, int ly, int lz)
{
    SdkChunkSimState* state;
    SdkSimCellKey key;
    if (!chunk || !chunk->sim_state) return;
    state = chunk->sim_state;
    key = sdk_simulation_pack_local_key(lx, ly, lz);
    if (find_loose_index(state, key) >= 0) return;
    if (!reserve_bytes((void**)&state->loose_cells, sizeof(SdkLooseCellState), &state->loose_capacity, state->loose_count + 1u)) {
        return;
    }
    state->loose_cells[state->loose_count].key = key;
    state->loose_cells[state->loose_count].flags = 0u;
    state->loose_cells[state->loose_count].repose_bias = sdk_block_get_angle_of_repose(sdk_chunk_get_block(chunk, lx, ly, lz));
    state->loose_count++;
}

static void clear_runtime_local(SdkChunk* chunk, int lx, int ly, int lz)
{
    remove_fluid_local(chunk, lx, ly, lz);
    remove_loose_local(chunk, lx, ly, lz);
}

static SdkChunk* chunk_from_world(SdkChunkManager* cm, int wx, int wz, int* out_lx, int* out_lz)
{
    int cx;
    int cz;
    SdkChunk* chunk;

    if (!cm) return NULL;
    cx = sdk_world_to_chunk_x(wx);
    cz = sdk_world_to_chunk_z(wz);
    chunk = sdk_chunk_manager_get_chunk(cm, cx, cz);
    if (!chunk) return NULL;
    if (out_lx) *out_lx = sdk_world_to_local_x(wx, cx);
    if (out_lz) *out_lz = sdk_world_to_local_z(wz, cz);
    return chunk;
}

static void mark_cross_chunk_neighbors_dirty_internal(SdkChunkManager* cm, int wx, int wy, int wz, int fill_only)
{
    int cx;
    int cz;
    int lx;
    int lz;
    int subchunk_index;
    SdkChunk* neighbor;

    if (!cm || wy < 0 || wy >= CHUNK_HEIGHT) return;
    cx = sdk_world_to_chunk_x(wx);
    cz = sdk_world_to_chunk_z(wz);
    lx = sdk_world_to_local_x(wx, cx);
    lz = sdk_world_to_local_z(wz, cz);
    subchunk_index = sdk_chunk_subchunk_index_from_y(wy);

    if (lx == 0) {
        neighbor = sdk_chunk_manager_get_chunk(cm, cx - 1, cz);
        if (neighbor) {
            if (fill_only) sdk_chunk_mark_block_fill_dirty(neighbor, 0, wy, 0);
            else sdk_chunk_mark_block_dirty(neighbor, 0, wy, 0);
        }
    }
    if (lx == CHUNK_WIDTH - 1) {
        neighbor = sdk_chunk_manager_get_chunk(cm, cx + 1, cz);
        if (neighbor) {
            if (fill_only) sdk_chunk_mark_block_fill_dirty(neighbor, 0, wy, 0);
            else sdk_chunk_mark_block_dirty(neighbor, 0, wy, 0);
        }
    }
    if (lz == 0) {
        neighbor = sdk_chunk_manager_get_chunk(cm, cx, cz - 1);
        if (neighbor) {
            if (fill_only) sdk_chunk_mark_block_fill_dirty(neighbor, 0, wy, 0);
            else sdk_chunk_mark_block_dirty(neighbor, 0, wy, 0);
        }
    }
    if (lz == CHUNK_DEPTH - 1) {
        neighbor = sdk_chunk_manager_get_chunk(cm, cx, cz + 1);
        if (neighbor) {
            if (fill_only) sdk_chunk_mark_block_fill_dirty(neighbor, 0, wy, 0);
            else sdk_chunk_mark_block_dirty(neighbor, 0, wy, 0);
        }
    }
    (void)subchunk_index;
}

static void mark_cross_chunk_neighbors_dirty(SdkChunkManager* cm, int wx, int wy, int wz)
{
    mark_cross_chunk_neighbors_dirty_internal(cm, wx, wy, wz, 0);
}

static void mark_cross_chunk_neighbors_fill_dirty(SdkChunkManager* cm, int wx, int wy, int wz)
{
    mark_cross_chunk_neighbors_dirty_internal(cm, wx, wy, wz, 1);
}

static void set_block_world(SdkChunkManager* cm, int wx, int wy, int wz, BlockType type)
{
    int lx;
    int lz;
    SdkChunk* chunk;
    BlockType old_type;

    if (!cm || wy < 0 || wy >= CHUNK_HEIGHT) return;
    chunk = chunk_from_world(cm, wx, wz, &lx, &lz);
    if (!chunk) return;
    old_type = sdk_chunk_get_block(chunk, lx, wy, lz);
    if (old_type == type) return;
    sdk_chunk_set_block(chunk, lx, wy, lz, type);
    mark_cross_chunk_neighbors_dirty(cm, wx, wy, wz);
}

uint8_t sdk_simulation_get_fluid_fill(const SdkChunk* chunk, int lx, int ly, int lz)
{
    int index;
    BlockType block;

    if (!chunk || !chunk->blocks) return 0u;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)ly >= CHUNK_HEIGHT || (unsigned)lz >= CHUNK_DEPTH) return 0u;
    block = sdk_chunk_get_block(chunk, lx, ly, lz);
    if ((sdk_block_get_behavior_flags(block) & SDK_BLOCK_BEHAVIOR_FLUID) == 0u) return 0u;
    if (!chunk->sim_state) return 255u;
    index = find_fluid_index(chunk->sim_state, sdk_simulation_pack_local_key(lx, ly, lz));
    if (index < 0) return 255u;
    return chunk->sim_state->fluid_cells[index].fill;
}

static uint8_t fluid_fill_at_world(SdkChunkManager* cm, int wx, int wy, int wz, BlockType* out_block)
{
    int lx;
    int lz;
    SdkChunk* chunk;
    if (out_block) *out_block = BLOCK_AIR;
    if (!cm || wy < 0 || wy >= CHUNK_HEIGHT) return 0u;
    chunk = chunk_from_world(cm, wx, wz, &lx, &lz);
    if (!chunk) return 0u;
    if (out_block) *out_block = sdk_chunk_get_block(chunk, lx, wy, lz);
    return sdk_simulation_get_fluid_fill(chunk, lx, wy, lz);
}

static void set_fluid_fill_world(SdkChunkManager* cm, int wx, int wy, int wz, BlockType fluid_block, uint8_t fill)
{
    int lx;
    int lz;
    SdkChunk* chunk;
    BlockType current;
    uint8_t previous_fill;
    int block_changed = 0;

    if (!cm || wy < 0 || wy >= CHUNK_HEIGHT) return;
    chunk = chunk_from_world(cm, wx, wz, &lx, &lz);
    if (!chunk) return;
    current = sdk_chunk_get_block(chunk, lx, wy, lz);
    previous_fill = sdk_simulation_get_fluid_fill(chunk, lx, wy, lz);

    if (fill == 0u) {
        remove_fluid_local(chunk, lx, wy, lz);
        if (current == fluid_block) {
            sdk_chunk_set_block(chunk, lx, wy, lz, BLOCK_AIR);
            mark_cross_chunk_neighbors_dirty(cm, wx, wy, wz);
            block_changed = 1;
        }
        if (previous_fill != 0u && !block_changed) {
            sdk_chunk_mark_block_fill_dirty(chunk, lx, wy, lz);
            mark_cross_chunk_neighbors_fill_dirty(cm, wx, wy, wz);
        }
        return;
    }

    if (current != fluid_block) {
        sdk_chunk_set_block(chunk, lx, wy, lz, fluid_block);
        mark_cross_chunk_neighbors_dirty(cm, wx, wy, wz);
        block_changed = 1;
    }
    if (fill >= 255u) remove_fluid_local(chunk, lx, wy, lz);
    else set_partial_fluid_local(chunk, lx, wy, lz, fill, (uint8_t)sdk_block_get_runtime_material(fluid_block));
    if (previous_fill != fill && !block_changed) {
        sdk_chunk_mark_block_fill_dirty(chunk, lx, wy, lz);
        mark_cross_chunk_neighbors_fill_dirty(cm, wx, wy, wz);
    }
}

void sdk_simulation_enqueue_local(SdkChunk* chunk, int lx, int ly, int lz)
{
    if (!chunk || !chunk->sim_state) return;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)ly >= CHUNK_HEIGHT || (unsigned)lz >= CHUNK_DEPTH) return;
    enqueue_key(chunk->sim_state, sdk_simulation_pack_local_key(lx, ly, lz));
}

void sdk_simulation_enqueue_world(SdkChunkManager* cm, int wx, int wy, int wz)
{
    int lx;
    int lz;
    SdkChunk* chunk;
    if (!cm || wy < 0 || wy >= CHUNK_HEIGHT) return;
    chunk = chunk_from_world(cm, wx, wz, &lx, &lz);
    if (!chunk) return;
    sdk_simulation_enqueue_local(chunk, lx, wy, lz);
}

static void enqueue_world_neighborhood(SdkChunkManager* cm, int wx, int wy, int wz)
{
    static const int offsets[15][3] = {
        { 0, 0, 0 },
        { -1, 0, 0 }, { 1, 0, 0 },
        { 0, -1, 0 }, { 0, 1, 0 },
        { 0, 0, -1 }, { 0, 0, 1 },
        { -1, -1, 0 }, { 1, -1, 0 },
        { 0, -1, -1 }, { 0, -1, 1 },
        { -1, 0, -1 }, { -1, 0, 1 },
        { 1, 0, -1 }, { 1, 0, 1 }
    };
    int i;
    for (i = 0; i < 15; ++i) {
        sdk_simulation_enqueue_world(cm,
                                     wx + offsets[i][0],
                                     wy + offsets[i][1],
                                     wz + offsets[i][2]);
    }
}

static int find_loaded_chunk_bounds(SdkChunkManager* cm, int* out_min_cx, int* out_max_cx, int* out_min_cz, int* out_max_cz)
{
    int slot_index;
    int found = 0;
    int min_cx = 0;
    int max_cx = 0;
    int min_cz = 0;
    int max_cz = 0;

    if (!cm) return 0;
    for (slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(cm, slot_index);
        SdkChunk* chunk;
        if (!slot || !slot->occupied) continue;
        chunk = &slot->chunk;
        if (!chunk->blocks) continue;
        if (!found) {
            min_cx = max_cx = chunk->cx;
            min_cz = max_cz = chunk->cz;
            found = 1;
        } else {
            if (chunk->cx < min_cx) min_cx = chunk->cx;
            if (chunk->cx > max_cx) max_cx = chunk->cx;
            if (chunk->cz < min_cz) min_cz = chunk->cz;
            if (chunk->cz > max_cz) max_cz = chunk->cz;
        }
    }

    if (!found) return 0;
    if (out_min_cx) *out_min_cx = min_cx;
    if (out_max_cx) *out_max_cx = max_cx;
    if (out_min_cz) *out_min_cz = min_cz;
    if (out_max_cz) *out_max_cz = max_cz;
    return 1;
}

static int column_surface_water_y(SdkChunkManager* cm, int wx, int wz, int reference_y)
{
    int min_y;
    int max_y;
    int y;

    if (!cm) return -1;
    if (reference_y < 0) reference_y = 0;
    if (reference_y >= CHUNK_HEIGHT) reference_y = CHUNK_HEIGHT - 1;

    min_y = reference_y - 12;
    max_y = reference_y + 4;
    if (min_y < 0) min_y = 0;
    if (max_y >= CHUNK_HEIGHT) max_y = CHUNK_HEIGHT - 1;

    for (y = max_y; y >= min_y; --y) {
        BlockType block;
        uint8_t fill = fluid_fill_at_world(cm, wx, y, wz, &block);
        if (block == BLOCK_WATER && fill > 0u) {
            while (y + 1 < CHUNK_HEIGHT) {
                uint8_t above_fill = fluid_fill_at_world(cm, wx, y + 1, wz, &block);
                if (block != BLOCK_WATER || above_fill == 0u) break;
                ++y;
            }
            return y;
        }
    }

    return -1;
}

static void enqueue_water_column_slice(SdkChunkManager* cm, int wx, int wz, int top_y, int min_y)
{
    int y;

    if (!cm) return;
    if (top_y < 0) return;
    if (min_y < 0) min_y = 0;
    if (min_y > top_y) min_y = top_y;

    for (y = top_y; y >= min_y; --y) {
        BlockType block;
        uint8_t fill = fluid_fill_at_world(cm, wx, y, wz, &block);
        if (block != BLOCK_WATER || fill == 0u) break;
        sdk_simulation_enqueue_world(cm, wx, y, wz);
    }
}

static int find_adjacent_water_seed(SdkChunkManager* cm, int wx, int wy, int wz,
                                    int* out_wx, int* out_wy, int* out_wz)
{
    static const int offsets[7][3] = {
        { 0, 0, 0 },
        { -1, 0, 0 }, { 1, 0, 0 },
        { 0, -1, 0 }, { 0, 1, 0 },
        { 0, 0, -1 }, { 0, 0, 1 }
    };
    int i;

    if (!cm) return 0;
    for (i = 0; i < 7; ++i) {
        int sx = wx + offsets[i][0];
        int sy = wy + offsets[i][1];
        int sz = wz + offsets[i][2];
        BlockType block;
        uint8_t fill;

        if (sy < 0 || sy >= CHUNK_HEIGHT) continue;
        fill = fluid_fill_at_world(cm, sx, sy, sz, &block);
        if (block == BLOCK_WATER && fill > 0u) {
            if (out_wx) *out_wx = sx;
            if (out_wy) *out_wy = sy;
            if (out_wz) *out_wz = sz;
            return 1;
        }
    }
    return 0;
}

static int is_bulk_water_lid(BlockType block)
{
    return block == BLOCK_AIR ||
           block == BLOCK_SNOW ||
           block == BLOCK_ICE ||
           block == BLOCK_SEA_ICE;
}

static void activate_loaded_water_surface_band(SdkChunkManager* cm,
                                               int seed_wx, int seed_wz,
                                               int seed_top_y, int breach_y)
{
    int min_cx;
    int max_cx;
    int min_cz;
    int max_cz;
    int min_wx;
    int max_wx;
    int min_wz;
    int max_wz;
    int width;
    int depth;
    int min_y;
    uint8_t* visited = NULL;
    int* queue_wx = NULL;
    int* queue_wz = NULL;
    uint32_t head = 0u;
    uint32_t tail = 0u;

    if (!cm) return;
    if (!find_loaded_chunk_bounds(cm, &min_cx, &max_cx, &min_cz, &max_cz)) return;

    min_wx = min_cx * CHUNK_WIDTH;
    max_wx = (max_cx + 1) * CHUNK_WIDTH - 1;
    min_wz = min_cz * CHUNK_DEPTH;
    max_wz = (max_cz + 1) * CHUNK_DEPTH - 1;
    if (seed_wx < min_wx || seed_wx > max_wx || seed_wz < min_wz || seed_wz > max_wz) return;
    width = max_wx - min_wx + 1;
    depth = max_wz - min_wz + 1;
    min_y = breach_y - SDK_SIM_WATER_WAKE_MIN_DEPTH_BELOW_BREACH;
    if (min_y < 0) min_y = 0;

    visited = (uint8_t*)calloc((size_t)width * (size_t)depth, sizeof(uint8_t));
    queue_wx = (int*)malloc(sizeof(int) * SDK_SIM_WATER_WAKE_MAX_COLUMNS);
    queue_wz = (int*)malloc(sizeof(int) * SDK_SIM_WATER_WAKE_MAX_COLUMNS);
    if (!visited || !queue_wx || !queue_wz) {
        free(visited);
        free(queue_wx);
        free(queue_wz);
        return;
    }

    queue_wx[tail] = seed_wx;
    queue_wz[tail] = seed_wz;
    tail++;

    while (head < tail) {
        static const int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        int wx = queue_wx[head];
        int wz = queue_wz[head];
        int local_x = wx - min_wx;
        int local_z = wz - min_wz;
        uint32_t visit_index;
        int top_y;
        int i;

        head++;
        if (local_x < 0 || local_x >= width || local_z < 0 || local_z >= depth) continue;
        visit_index = (uint32_t)local_z * (uint32_t)width + (uint32_t)local_x;
        if (visited[visit_index]) continue;
        visited[visit_index] = 1u;

        top_y = column_surface_water_y(cm, wx, wz, seed_top_y);
        if (top_y < 0) continue;
        if (abs(top_y - seed_top_y) > SDK_SIM_WATER_WAKE_SURFACE_TOLERANCE) continue;

        enqueue_water_column_slice(cm, wx, wz, top_y, min_y);

        for (i = 0; i < 4; ++i) {
            int nx = wx + dirs[i][0];
            int nz = wz + dirs[i][1];
            if (nx < min_wx || nx > max_wx || nz < min_wz || nz > max_wz) continue;
            if (tail >= SDK_SIM_WATER_WAKE_MAX_COLUMNS) break;
            queue_wx[tail] = nx;
            queue_wz[tail] = nz;
            tail++;
        }
    }

    free(visited);
    free(queue_wx);
    free(queue_wz);
}

static uint32_t estimate_connected_surface_water_columns(SdkChunkManager* cm,
                                                         int seed_wx, int seed_wy, int seed_wz,
                                                         uint32_t limit)
{
    int min_cx;
    int max_cx;
    int min_cz;
    int max_cz;
    int min_wx;
    int max_wx;
    int min_wz;
    int max_wz;
    int width;
    int depth;
    int seed_top_y;
    uint8_t* visited = NULL;
    int* queue_wx = NULL;
    int* queue_wz = NULL;
    uint32_t head = 0u;
    uint32_t tail = 0u;
    uint32_t count = 0u;

    if (!cm || limit == 0u) return 0u;
    if (!find_loaded_chunk_bounds(cm, &min_cx, &max_cx, &min_cz, &max_cz)) return 0u;

    seed_top_y = column_surface_water_y(cm, seed_wx, seed_wz, seed_wy);
    if (seed_top_y < 0) return 0u;

    min_wx = min_cx * CHUNK_WIDTH;
    max_wx = (max_cx + 1) * CHUNK_WIDTH - 1;
    min_wz = min_cz * CHUNK_DEPTH;
    max_wz = (max_cz + 1) * CHUNK_DEPTH - 1;
    width = max_wx - min_wx + 1;
    depth = max_wz - min_wz + 1;
    if (seed_wx < min_wx || seed_wx > max_wx || seed_wz < min_wz || seed_wz > max_wz) return 0u;

    visited = (uint8_t*)calloc((size_t)width * (size_t)depth, sizeof(uint8_t));
    queue_wx = (int*)malloc(sizeof(int) * (size_t)(limit * 4u + 16u));
    queue_wz = (int*)malloc(sizeof(int) * (size_t)(limit * 4u + 16u));
    if (!visited || !queue_wx || !queue_wz) {
        free(visited);
        free(queue_wx);
        free(queue_wz);
        return 0u;
    }

    queue_wx[tail] = seed_wx;
    queue_wz[tail] = seed_wz;
    tail++;

    while (head < tail && count < limit) {
        static const int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        int wx = queue_wx[head];
        int wz = queue_wz[head];
        int local_x = wx - min_wx;
        int local_z = wz - min_wz;
        uint32_t visit_index;
        int top_y;
        int i;

        head++;
        if (local_x < 0 || local_x >= width || local_z < 0 || local_z >= depth) continue;
        visit_index = (uint32_t)local_z * (uint32_t)width + (uint32_t)local_x;
        if (visited[visit_index]) continue;
        visited[visit_index] = 1u;

        top_y = column_surface_water_y(cm, wx, wz, seed_top_y);
        if (top_y < 0) continue;
        if (abs(top_y - seed_top_y) > (SDK_SIM_WATER_WAKE_SURFACE_TOLERANCE + 2)) continue;

        count++;
        if (count >= limit) break;

        for (i = 0; i < 4; ++i) {
            int nx = wx + dirs[i][0];
            int nz = wz + dirs[i][1];
            if (nx < min_wx || nx > max_wx || nz < min_wz || nz > max_wz) continue;
            if (tail >= limit * 4u + 16u) break;
            queue_wx[tail] = nx;
            queue_wz[tail] = nz;
            tail++;
        }
    }

    free(visited);
    free(queue_wx);
    free(queue_wz);
    return count;
}

static void measure_open_top_column_state(SdkChunkManager* cm, int wx, int wz, int search_top_y,
                                          int* out_barrier_y, float* out_surface_e)
{
    int y;
    int top_water_y = -1;
    uint8_t top_fill = 0u;
    int barrier_y = -1;

    if (!cm) {
        if (out_barrier_y) *out_barrier_y = -1;
        if (out_surface_e) *out_surface_e = 0.0f;
        return;
    }

    if (search_top_y < 0) search_top_y = 0;
    if (search_top_y >= CHUNK_HEIGHT) search_top_y = CHUNK_HEIGHT - 1;

    for (y = search_top_y; y >= 0; --y) {
        BlockType block;
        uint8_t fill = fluid_fill_at_world(cm, wx, y, wz, &block);

        if (top_water_y < 0) {
            if (block == BLOCK_WATER && fill > 0u) {
                top_water_y = y;
                top_fill = fill;
                continue;
            }
            if (!is_bulk_water_lid(block)) {
                barrier_y = y;
                break;
            }
        } else {
            if (block != BLOCK_WATER || fill == 0u) {
                barrier_y = y;
                break;
            }
        }
    }

    if (top_water_y >= 0) {
        if (out_barrier_y) *out_barrier_y = barrier_y;
        if (out_surface_e) *out_surface_e = (float)top_water_y + (float)top_fill / 255.0f;
    } else {
        if (out_barrier_y) *out_barrier_y = barrier_y;
        if (out_surface_e) *out_surface_e = (float)(barrier_y + 1);
    }
}

static BlockType surface_cap_block_at(SdkChunkManager* cm, int wx, int wz, float surface_e)
{
    int lx;
    int lz;
    SdkChunk* chunk;
    int cap_y;
    BlockType block;

    if (!cm) return BLOCK_AIR;
    cap_y = (int)ceilf(surface_e);
    if (cap_y < 0 || cap_y >= CHUNK_HEIGHT) return BLOCK_AIR;
    chunk = chunk_from_world(cm, wx, wz, &lx, &lz);
    if (!chunk) return BLOCK_AIR;
    block = sdk_chunk_get_block(chunk, lx, cap_y, lz);
    if (block == BLOCK_ICE || block == BLOCK_SEA_ICE || block == BLOCK_SNOW) {
        return block;
    }
    return BLOCK_AIR;
}

static void clear_bulk_water_lid(SdkChunkManager* cm, int wx, int wz, int from_y, int to_y)
{
    int y;

    if (!cm) return;
    if (from_y > to_y) return;
    if (from_y < 0) from_y = 0;
    if (to_y >= CHUNK_HEIGHT) to_y = CHUNK_HEIGHT - 1;

    for (y = from_y; y <= to_y; ++y) {
        int lx;
        int lz;
        SdkChunk* chunk;
        BlockType block;

        chunk = chunk_from_world(cm, wx, wz, &lx, &lz);
        if (!chunk) continue;
        block = sdk_chunk_get_block(chunk, lx, y, lz);
        if (!is_bulk_water_lid(block) || block == BLOCK_AIR) continue;
        clear_runtime_local(chunk, lx, y, lz);
        set_block_world(cm, wx, y, wz, BLOCK_AIR);
    }
}

static DWORD WINAPI bulk_water_sum_worker_proc(LPVOID param)
{
    SdkBulkWaterSumTask* task = (SdkBulkWaterSumTask*)param;
    uint32_t i;
    float volume_sum = 0.0f;
    float min_base_e = FLT_MAX;

    if (!task) return 0;

    for (i = task->begin_index; i < task->end_index; ++i) {
        float base_e = (float)task->cols[i].barrier_y + 1.0f;
        if (base_e < min_base_e) min_base_e = base_e;
        if (task->probe_surface_e < 0.0f) {
            float current_volume = task->cols[i].current_surface_e - base_e;
            if (current_volume > 0.0f) volume_sum += current_volume;
        } else {
            float depth_e = task->probe_surface_e - base_e;
            if (depth_e > 0.0f) volume_sum += depth_e;
        }
    }

    task->volume_sum = volume_sum;
    task->min_base_e = (min_base_e == FLT_MAX) ? 0.0f : min_base_e;
    return 0;
}

static int choose_bulk_water_worker_count(uint32_t column_count)
{
    SYSTEM_INFO si;
    int worker_count;

    if (column_count < SDK_SIM_BULK_WATER_PARALLEL_MIN_COLUMNS) return 1;

    GetSystemInfo(&si);
    worker_count = (int)si.dwNumberOfProcessors;
    if (worker_count < 1) worker_count = 1;
    if (worker_count > SDK_SIM_BULK_WATER_MAX_WORKERS) worker_count = SDK_SIM_BULK_WATER_MAX_WORKERS;
    if ((uint32_t)worker_count > column_count / 512u) {
        worker_count = (int)(column_count / 512u);
        if (worker_count < 1) worker_count = 1;
    }
    return worker_count;
}

static void sum_bulk_water_columns(const SdkBulkWaterColumn* cols, uint32_t total_count, float probe_surface_e,
                                   float* out_volume_sum, float* out_min_base_e, int* out_worker_count)
{
    HANDLE threads[SDK_SIM_BULK_WATER_MAX_WORKERS];
    SdkBulkWaterSumTask tasks[SDK_SIM_BULK_WATER_MAX_WORKERS];
    int worker_count;
    uint32_t chunk_size;
    uint32_t begin = 0u;
    int i;
    float volume_sum = 0.0f;
    float min_base_e = FLT_MAX;

    if (out_volume_sum) *out_volume_sum = 0.0f;
    if (out_min_base_e) *out_min_base_e = 0.0f;
    if (out_worker_count) *out_worker_count = 0;
    if (!cols || total_count == 0u) return;

    worker_count = choose_bulk_water_worker_count(total_count);
    if (worker_count <= 1) {
        SdkBulkWaterSumTask task;
        memset(&task, 0, sizeof(task));
        task.cols = cols;
        task.begin_index = 0u;
        task.end_index = total_count;
        task.probe_surface_e = probe_surface_e;
        bulk_water_sum_worker_proc(&task);
        if (out_volume_sum) *out_volume_sum = task.volume_sum;
        if (out_min_base_e) *out_min_base_e = task.min_base_e;
        if (out_worker_count) *out_worker_count = 1;
        return;
    }

    chunk_size = (total_count + (uint32_t)worker_count - 1u) / (uint32_t)worker_count;
    for (i = 0; i < worker_count; ++i) {
        uint32_t end = begin + chunk_size;
        if (end > total_count) end = total_count;
        memset(&tasks[i], 0, sizeof(tasks[i]));
        tasks[i].cols = cols;
        tasks[i].begin_index = begin;
        tasks[i].end_index = end;
        tasks[i].probe_surface_e = probe_surface_e;
        threads[i] = CreateThread(NULL, 0u, bulk_water_sum_worker_proc, &tasks[i], 0u, NULL);
        if (!threads[i]) {
            bulk_water_sum_worker_proc(&tasks[i]);
        }
        begin = end;
    }

    for (i = 0; i < worker_count; ++i) {
        if (threads[i]) {
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
        }
        volume_sum += tasks[i].volume_sum;
        if (tasks[i].min_base_e < min_base_e) min_base_e = tasks[i].min_base_e;
    }

    if (out_volume_sum) *out_volume_sum = volume_sum;
    if (out_min_base_e) *out_min_base_e = (min_base_e == FLT_MAX) ? 0.0f : min_base_e;
    if (out_worker_count) *out_worker_count = worker_count;
}

static uint32_t compute_reservoir_stage_count(const SdkBulkWaterColumn* cols, uint32_t total_count, float target_surface_e)
{
    float max_delta = 0.0f;
    uint32_t i;
    uint32_t stage_count;

    if (!cols || total_count == 0u) return 1u;
    for (i = 0u; i < total_count; ++i) {
        float delta = fabsf(cols[i].current_surface_e - target_surface_e);
        if (delta > max_delta) max_delta = delta;
    }

    stage_count = (uint32_t)ceilf(max_delta * 0.75f);
    if (stage_count < 1u) stage_count = 1u;
    if (stage_count > SDK_SIM_MAX_STAGE_COUNT) stage_count = SDK_SIM_MAX_STAGE_COUNT;
    return stage_count;
}

static float solve_bulk_target_surface_histogram(const SdkBulkWaterColumn* cols,
                                                 uint32_t total_count,
                                                 float total_volume,
                                                 float min_base_e)
{
    uint32_t base_counts[CHUNK_HEIGHT + 1];
    uint32_t i;
    uint32_t active_columns = 0u;
    int y;
    int min_y;
    float remaining = total_volume;

    memset(base_counts, 0, sizeof(base_counts));
    min_y = (int)floorf(min_base_e);
    if (min_y < 0) min_y = 0;
    if (min_y >= CHUNK_HEIGHT) min_y = CHUNK_HEIGHT - 1;

    for (i = 0u; i < total_count; ++i) {
        int base_y = cols[i].barrier_y + 1;
        if (base_y < 0) base_y = 0;
        if (base_y > CHUNK_HEIGHT) base_y = CHUNK_HEIGHT;
        base_counts[base_y]++;
    }

    for (y = min_y; y < CHUNK_HEIGHT; ++y) {
        active_columns += base_counts[y];
        if (active_columns == 0u) continue;
        if (remaining <= (float)active_columns) {
            return (float)y + remaining / (float)active_columns;
        }
        remaining -= (float)active_columns;
    }

    return (float)(CHUNK_HEIGHT - 1);
}

static void free_reservoir_job(SdkReservoirJob* job)
{
    if (!job) return;
    free(job->cols);
    memset(job, 0, sizeof(*job));
}

static void free_reservoir_result(SdkReservoirResult* result)
{
    if (!result) return;
    free(result->cols);
    memset(result, 0, sizeof(*result));
}

static void free_active_reservoir(SdkActiveReservoir* active)
{
    if (!active) return;
    free(active->cols);
    memset(active, 0, sizeof(*active));
}

static void ensure_reservoir_scheduler_started(void)
{
    SdkReservoirScheduler* sched = &g_reservoir_scheduler;
    if (sched->started) return;
    InitializeCriticalSection(&sched->lock);
    InitializeConditionVariable(&sched->jobs_cv);
    sched->running = 1;
    sched->started = 1;
}

static void solve_reservoir_job(const SdkReservoirJob* job, SdkReservoirResult* out_result)
{
    double start_ms = fluid_debug_now_ms();
    float total_volume = 0.0f;
    float min_base_e = 0.0f;
    float target_surface_e = 0.0f;
    int worker_count = 1;

    if (!job || !out_result) return;
    memset(out_result, 0, sizeof(*out_result));
    out_result->generation = job->generation;
    out_result->seed_wx = job->seed_wx;
    out_result->seed_wy = job->seed_wy;
    out_result->seed_wz = job->seed_wz;
    out_result->reservoir_count = job->reservoir_count;
    out_result->total_count = job->total_count;
    out_result->status_flags = job->status_flags;
    out_result->cols = job->cols;
    if (!out_result->cols || out_result->total_count == 0u) return;

    sum_bulk_water_columns(out_result->cols, out_result->total_count, -1.0f,
                           &total_volume, &min_base_e, &worker_count);
    target_surface_e = solve_bulk_target_surface_histogram(out_result->cols,
                                                           out_result->total_count,
                                                           total_volume,
                                                           min_base_e);
    out_result->worker_count = worker_count;
    out_result->total_volume = total_volume;
    out_result->target_surface_e = target_surface_e;
    out_result->stage_count = compute_reservoir_stage_count(out_result->cols,
                                                            out_result->total_count,
                                                            target_surface_e);
    out_result->solve_ms = (float)(fluid_debug_now_ms() - start_ms);
}

static DWORD WINAPI reservoir_worker_proc(LPVOID param)
{
    SdkReservoirScheduler* sched = (SdkReservoirScheduler*)param;

    for (;;) {
        SdkReservoirJob job;
        SdkReservoirResult result;
        int have_job = 0;

        memset(&job, 0, sizeof(job));
        memset(&result, 0, sizeof(result));

        EnterCriticalSection(&sched->lock);
        while (sched->running && sched->job_count == 0) {
            SleepConditionVariableCS(&sched->jobs_cv, &sched->lock, INFINITE);
        }
        if (!sched->running && sched->job_count == 0) {
            LeaveCriticalSection(&sched->lock);
            break;
        }
        if (sched->job_count > 0) {
            job = sched->jobs[sched->job_head];
            memset(&sched->jobs[sched->job_head], 0, sizeof(sched->jobs[sched->job_head]));
            sched->job_head = (sched->job_head + 1) % SDK_SIM_RESERVOIR_JOB_CAPACITY;
            sched->job_count--;
            have_job = 1;
        }
        LeaveCriticalSection(&sched->lock);

        if (!have_job) continue;

        solve_reservoir_job(&job, &result);
        job.cols = NULL;

        EnterCriticalSection(&sched->lock);
        if (sched->result_count < SDK_SIM_RESERVOIR_RESULT_CAPACITY) {
            int out_index = sim_ring_index(sched->result_head, sched->result_count, SDK_SIM_RESERVOIR_RESULT_CAPACITY);
            sched->results[out_index] = result;
            sched->result_count++;
            memset(&result, 0, sizeof(result));
        }
        LeaveCriticalSection(&sched->lock);

        free_reservoir_job(&job);
        free_reservoir_result(&result);
    }

    return 0;
}

static int enqueue_reservoir_job(const SdkReservoirJob* job)
{
    SdkReservoirScheduler* sched = &g_reservoir_scheduler;
    int out_index;

    if (!job || !job->cols || job->total_count == 0u) return 0;
    ensure_reservoir_scheduler_started();

    EnterCriticalSection(&sched->lock);
    if (!sched->thread) {
        sched->thread = CreateThread(NULL, 0u, reservoir_worker_proc, sched, 0u, NULL);
        if (!sched->thread) {
            LeaveCriticalSection(&sched->lock);
            return 0;
        }
    }
    if (sched->job_count >= SDK_SIM_RESERVOIR_JOB_CAPACITY) {
        LeaveCriticalSection(&sched->lock);
        return 0;
    }
    out_index = sim_ring_index(sched->job_head, sched->job_count, SDK_SIM_RESERVOIR_JOB_CAPACITY);
    sched->jobs[out_index] = *job;
    sched->job_count++;
    WakeConditionVariable(&sched->jobs_cv);
    LeaveCriticalSection(&sched->lock);
    return 1;
}

static int pop_reservoir_result(SdkReservoirResult* out_result)
{
    SdkReservoirScheduler* sched = &g_reservoir_scheduler;
    if (!out_result || !sched->started) return 0;

    EnterCriticalSection(&sched->lock);
    if (sched->result_count <= 0) {
        LeaveCriticalSection(&sched->lock);
        return 0;
    }
    *out_result = sched->results[sched->result_head];
    memset(&sched->results[sched->result_head], 0, sizeof(sched->results[sched->result_head]));
    sched->result_head = (sched->result_head + 1) % SDK_SIM_RESERVOIR_RESULT_CAPACITY;
    sched->result_count--;
    LeaveCriticalSection(&sched->lock);
    return 1;
}

static uint32_t current_reservoir_generation(void)
{
    return g_reservoir_scheduler.topo_generation;
}

static void bump_reservoir_generation(void)
{
    g_reservoir_scheduler.topo_generation++;
    if (g_reservoir_scheduler.topo_generation == 0u) {
        g_reservoir_scheduler.topo_generation = 1u;
    }
}

void sdk_simulation_shutdown(void)
{
    sdk_simulation_begin_shutdown();
    while (!sdk_simulation_poll_shutdown()) {
        Sleep(0);
    }
}

void sdk_simulation_begin_shutdown(void)
{
    SdkReservoirScheduler* sched = &g_reservoir_scheduler;
    SdkReservoirJob pending_job;

    if (!sched->started) return;

    EnterCriticalSection(&sched->lock);
    if (!sched->running) {
        LeaveCriticalSection(&sched->lock);
        return;
    }
    sched->running = 0;
    while (sched->job_count > 0) {
        pending_job = sched->jobs[sched->job_head];
        memset(&sched->jobs[sched->job_head], 0, sizeof(sched->jobs[sched->job_head]));
        sched->job_head = (sched->job_head + 1) % SDK_SIM_RESERVOIR_JOB_CAPACITY;
        sched->job_count--;
        free_reservoir_job(&pending_job);
    }
    WakeAllConditionVariable(&sched->jobs_cv);
    LeaveCriticalSection(&sched->lock);
}

int sdk_simulation_poll_shutdown(void)
{
    int i;
    SdkReservoirScheduler* sched = &g_reservoir_scheduler;

    if (!sched->started) return 1;

    if (sched->thread) {
        DWORD wait_result = WaitForSingleObject(sched->thread, 0u);
        if (wait_result == WAIT_TIMEOUT) {
            return 0;
        }
        CloseHandle(sched->thread);
        sched->thread = NULL;
    }

    for (i = 0; i < SDK_SIM_RESERVOIR_JOB_CAPACITY; ++i) {
        free_reservoir_job(&sched->jobs[i]);
    }
    for (i = 0; i < SDK_SIM_RESERVOIR_RESULT_CAPACITY; ++i) {
        free_reservoir_result(&sched->results[i]);
    }
    for (i = 0; i < SDK_SIM_MAX_ACTIVE_RESERVOIRS; ++i) {
        free_active_reservoir(&sched->active[i]);
    }

    DeleteCriticalSection(&sched->lock);
    memset(sched, 0, sizeof(*sched));
    return 1;
}

void sdk_simulation_invalidate_reservoirs(void)
{
    int i;
    bump_reservoir_generation();
    if (!g_reservoir_scheduler.started) return;
    for (i = 0; i < SDK_SIM_MAX_ACTIVE_RESERVOIRS; ++i) {
        free_active_reservoir(&g_reservoir_scheduler.active[i]);
    }
}

static void apply_open_top_column_target(SdkChunkManager* cm, int wx, int wz, int barrier_y,
                                         float current_surface_e, float target_surface_e,
                                         BlockType cap_block)
{
    int y;
    int top_y;
    int target_top_y;
    int min_surface_cell;
    int old_cap_y;

    if (!cm) return;
    if (barrier_y >= CHUNK_HEIGHT - 1) return;
    if (target_surface_e < (float)(barrier_y + 1)) target_surface_e = (float)(barrier_y + 1);

    top_y = (int)ceilf(fmaxf(current_surface_e, target_surface_e)) + 1;
    if (top_y >= CHUNK_HEIGHT) top_y = CHUNK_HEIGHT - 1;
    target_top_y = (int)ceilf(target_surface_e);
    old_cap_y = (int)ceilf(current_surface_e);
    min_surface_cell = (int)floorf(fminf(current_surface_e, target_surface_e)) - 1;
    if (min_surface_cell < barrier_y + 1) min_surface_cell = barrier_y + 1;
    clear_bulk_water_lid(cm, wx, wz,
                         (old_cap_y < target_top_y ? old_cap_y : target_top_y),
                         top_y);

    for (y = min_surface_cell; y <= top_y; ++y) {
        float depth = target_surface_e - (float)y;
        int desired_fill;

        if (depth <= 0.0f) desired_fill = 0;
        else if (depth >= 1.0f) desired_fill = 255;
        else desired_fill = (int)lrintf(depth * 255.0f);

        if (desired_fill <= 0) {
            set_fluid_fill_world(cm, wx, y, wz, BLOCK_WATER, 0u);
        } else {
            if (desired_fill > 255) desired_fill = 255;
            set_fluid_fill_world(cm, wx, y, wz, BLOCK_WATER, (uint8_t)desired_fill);
        }
    }

    if (cap_block == BLOCK_ICE || cap_block == BLOCK_SEA_ICE || cap_block == BLOCK_SNOW) {
        int lx;
        int lz;
        SdkChunk* chunk = chunk_from_world(cm, wx, wz, &lx, &lz);
        if (chunk && target_top_y >= 0 && target_top_y < CHUNK_HEIGHT) {
            sdk_chunk_set_block(chunk, lx, target_top_y, lz, cap_block);
            mark_cross_chunk_neighbors_dirty(cm, wx, target_top_y, wz);
        }
    }
}

static int build_reservoir_snapshot(SdkChunkManager* cm,
                                    int seed_wx, int seed_wy, int seed_wz,
                                    SdkReservoirJob* out_job)
{
    int min_cx;
    int max_cx;
    int min_cz;
    int max_cz;
    int min_wx;
    int max_wx;
    int min_wz;
    int max_wz;
    int width;
    int depth;
    int seed_top_y;
    float seed_surface_e;
    uint8_t* tags = NULL;
    int* queue_wx = NULL;
    int* queue_wz = NULL;
    SdkBulkWaterColumn* cols = NULL;
    uint32_t head = 0u;
    uint32_t tail = 0u;
    uint32_t reservoir_count = 0u;
    uint32_t total_count = 0u;
    uint32_t status_flags = SDK_SIM_RESERVOIR_STATUS_COMPLETE;

    if (!cm || !out_job) return 0;
    memset(out_job, 0, sizeof(*out_job));
    if (!find_loaded_chunk_bounds(cm, &min_cx, &max_cx, &min_cz, &max_cz)) return 0;

    seed_top_y = column_surface_water_y(cm, seed_wx, seed_wz, seed_wy);
    if (seed_top_y < 0) return 0;
    {
        BlockType block;
        uint8_t fill = fluid_fill_at_world(cm, seed_wx, seed_top_y, seed_wz, &block);
        if (block != BLOCK_WATER || fill == 0u) return 0;
        seed_surface_e = (float)seed_top_y + (float)fill / 255.0f;
    }

    min_wx = min_cx * CHUNK_WIDTH;
    max_wx = (max_cx + 1) * CHUNK_WIDTH - 1;
    min_wz = min_cz * CHUNK_DEPTH;
    max_wz = (max_cz + 1) * CHUNK_DEPTH - 1;
    width = max_wx - min_wx + 1;
    depth = max_wz - min_wz + 1;
    if (seed_wx < min_wx || seed_wx > max_wx || seed_wz < min_wz || seed_wz > max_wz) return 0;

    tags = (uint8_t*)calloc((size_t)width * (size_t)depth, sizeof(uint8_t));
    queue_wx = (int*)malloc(sizeof(int) * SDK_SIM_BULK_WATER_MAX_COLUMNS);
    queue_wz = (int*)malloc(sizeof(int) * SDK_SIM_BULK_WATER_MAX_COLUMNS);
    cols = (SdkBulkWaterColumn*)malloc(sizeof(SdkBulkWaterColumn) * SDK_SIM_BULK_WATER_MAX_COLUMNS);
    if (!tags || !queue_wx || !queue_wz || !cols) {
        free(tags);
        free(queue_wx);
        free(queue_wz);
        free(cols);
        return 0;
    }

    queue_wx[tail] = seed_wx;
    queue_wz[tail] = seed_wz;
    tail++;

    while (head < tail && total_count < SDK_SIM_BULK_WATER_MAX_COLUMNS) {
        static const int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        int wx = queue_wx[head];
        int wz = queue_wz[head];
        int local_x = wx - min_wx;
        int local_z = wz - min_wz;
        uint32_t idx;
        int barrier_y;
        float surface_e;
        int surface_y;
        int d;

        head++;
        if (local_x < 0 || local_x >= width || local_z < 0 || local_z >= depth) continue;
        idx = (uint32_t)local_z * (uint32_t)width + (uint32_t)local_x;
        if (tags[idx] != 0u) continue;

        surface_y = column_surface_water_y(cm, wx, wz, seed_top_y);
        if (surface_y >= 0 && abs(surface_y - seed_top_y) <= SDK_SIM_WATER_WAKE_SURFACE_TOLERANCE) {
            measure_open_top_column_state(cm, wx, wz, seed_top_y + 2, &barrier_y, &surface_e);
            tags[idx] = 1u;
            cols[total_count].wx = wx;
            cols[total_count].wz = wz;
            cols[total_count].barrier_y = barrier_y;
            cols[total_count].current_surface_e = surface_e;
            cols[total_count].initial_surface_e = surface_e;
            cols[total_count].cap_block = (uint8_t)surface_cap_block_at(cm, wx, wz, surface_e);
            cols[total_count].flags = 1u;
            total_count++;
            reservoir_count++;

            for (d = 0; d < 4; ++d) {
                int nx = wx + dirs[d][0];
                int nz = wz + dirs[d][1];
                if (nx < min_wx || nx > max_wx || nz < min_wz || nz > max_wz) continue;
                if (tail >= SDK_SIM_BULK_WATER_MAX_COLUMNS) {
                    status_flags |= SDK_SIM_RESERVOIR_STATUS_TRUNCATED_MAX_COLUMNS;
                    break;
                }
                queue_wx[tail] = nx;
                queue_wz[tail] = nz;
                tail++;
            }
        }
    }

    if (reservoir_count < SDK_SIM_BULK_WATER_MIN_COLUMNS) {
        free(tags);
        free(queue_wx);
        free(queue_wz);
        free(cols);
        return 0;
    }

    for (uint32_t i = 0u; i < reservoir_count && tail < SDK_SIM_BULK_WATER_MAX_COLUMNS; ++i) {
        static const int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        int d;
        for (d = 0; d < 4; ++d) {
            int nx = cols[i].wx + dirs[d][0];
            int nz = cols[i].wz + dirs[d][1];
            if (nx < min_wx || nx > max_wx || nz < min_wz || nz > max_wz) continue;
            if (tail >= SDK_SIM_BULK_WATER_MAX_COLUMNS) {
                status_flags |= SDK_SIM_RESERVOIR_STATUS_TRUNCATED_MAX_COLUMNS;
                break;
            }
            queue_wx[tail] = nx;
            queue_wz[tail] = nz;
            tail++;
        }
    }

    while (head < tail && total_count < SDK_SIM_BULK_WATER_MAX_COLUMNS) {
        static const int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        int wx = queue_wx[head];
        int wz = queue_wz[head];
        int local_x = wx - min_wx;
        int local_z = wz - min_wz;
        uint32_t idx;
        int barrier_y;
        float surface_e;
        int d;

        head++;
        if (local_x < 0 || local_x >= width || local_z < 0 || local_z >= depth) continue;
        idx = (uint32_t)local_z * (uint32_t)width + (uint32_t)local_x;
        if (tags[idx] != 0u) continue;

        measure_open_top_column_state(cm, wx, wz, seed_top_y + 2, &barrier_y, &surface_e);
        if ((float)(barrier_y + 1) >= seed_surface_e - 0.01f) continue;

        tags[idx] = 2u;
        cols[total_count].wx = wx;
        cols[total_count].wz = wz;
        cols[total_count].barrier_y = barrier_y;
        cols[total_count].current_surface_e = surface_e;
        cols[total_count].initial_surface_e = surface_e;
        cols[total_count].cap_block = (uint8_t)surface_cap_block_at(cm, wx, wz, surface_e);
        cols[total_count].flags = 0u;
        total_count++;

        for (d = 0; d < 4; ++d) {
            int nx = wx + dirs[d][0];
            int nz = wz + dirs[d][1];
            if (nx < min_wx || nx > max_wx || nz < min_wz || nz > max_wz) continue;
            if (tail >= SDK_SIM_BULK_WATER_MAX_COLUMNS) {
                status_flags |= SDK_SIM_RESERVOIR_STATUS_TRUNCATED_MAX_COLUMNS;
                break;
            }
            queue_wx[tail] = nx;
            queue_wz[tail] = nz;
            tail++;
        }
    }

    free(tags);
    free(queue_wx);
    free(queue_wz);

    out_job->generation = current_reservoir_generation();
    out_job->seed_wx = seed_wx;
    out_job->seed_wy = seed_wy;
    out_job->seed_wz = seed_wz;
    out_job->reservoir_count = reservoir_count;
    out_job->total_count = total_count;
    out_job->status_flags = status_flags;
    out_job->cols = cols;
    return 1;
}

static void process_async_reservoir_results(void)
{
    SdkReservoirResult result;

    while (pop_reservoir_result(&result)) {
        int slot_index = -1;
        int i;
        if (result.generation != current_reservoir_generation()) {
            free_reservoir_result(&result);
            g_fluid_debug_info.truncated_flags = SDK_SIM_RESERVOIR_STATUS_STALE_RESULT;
            fluid_debug_set_reason("RESERVOIR STALE");
            continue;
        }

        for (i = 0; i < SDK_SIM_MAX_ACTIVE_RESERVOIRS; ++i) {
            if (!g_reservoir_scheduler.active[i].active) {
                slot_index = i;
                break;
            }
        }
        if (slot_index < 0) slot_index = 0;
        free_active_reservoir(&g_reservoir_scheduler.active[slot_index]);
        g_reservoir_scheduler.active[slot_index].active = 1;
        g_reservoir_scheduler.active[slot_index].generation = result.generation;
        g_reservoir_scheduler.active[slot_index].seed_wx = result.seed_wx;
        g_reservoir_scheduler.active[slot_index].seed_wy = result.seed_wy;
        g_reservoir_scheduler.active[slot_index].seed_wz = result.seed_wz;
        g_reservoir_scheduler.active[slot_index].reservoir_count = result.reservoir_count;
        g_reservoir_scheduler.active[slot_index].total_count = result.total_count;
        g_reservoir_scheduler.active[slot_index].status_flags = result.status_flags;
        g_reservoir_scheduler.active[slot_index].worker_count = result.worker_count;
        g_reservoir_scheduler.active[slot_index].total_volume = result.total_volume;
        g_reservoir_scheduler.active[slot_index].target_surface_e = result.target_surface_e;
        g_reservoir_scheduler.active[slot_index].solve_ms = result.solve_ms;
        g_reservoir_scheduler.active[slot_index].stage_count = result.stage_count;
        g_reservoir_scheduler.active[slot_index].stage_index = 0u;
        g_reservoir_scheduler.active[slot_index].last_stage_surface_e = result.target_surface_e;
        g_reservoir_scheduler.active[slot_index].cols = result.cols;
        result.cols = NULL;

        g_fluid_debug_info.mechanism = SDK_FLUID_DEBUG_MECH_BULK_RESERVOIR;
        g_fluid_debug_info.last_seed_wx = result.seed_wx;
        g_fluid_debug_info.last_seed_wy = result.seed_wy;
        g_fluid_debug_info.last_seed_wz = result.seed_wz;
        g_fluid_debug_info.reservoir_columns = result.reservoir_count;
        g_fluid_debug_info.total_columns = result.total_count;
        g_fluid_debug_info.visited_columns = result.total_count;
        g_fluid_debug_info.spill_columns = result.total_count - result.reservoir_count;
        g_fluid_debug_info.worker_count = result.worker_count;
        g_fluid_debug_info.total_volume = result.total_volume;
        g_fluid_debug_info.target_surface_e = result.target_surface_e;
        g_fluid_debug_info.solve_ms = result.solve_ms;
        g_fluid_debug_info.stage_index = 0u;
        g_fluid_debug_info.stage_count = result.stage_count;
        g_fluid_debug_info.truncated_flags = result.status_flags;
        fluid_debug_set_reason("BULK QUEUED");

        free_reservoir_result(&result);
    }
}

static void advance_active_reservoirs(SdkChunkManager* cm)
{
    int i;

    for (i = 0; i < SDK_SIM_MAX_ACTIVE_RESERVOIRS; ++i) {
        SdkActiveReservoir* active = &g_reservoir_scheduler.active[i];
        uint32_t col_index;
        double start_ms;
        double end_ms;

        if (!active->active || !active->cols || active->total_count == 0u) continue;
        if (active->generation != current_reservoir_generation()) {
            free_active_reservoir(active);
            continue;
        }
        if (active->stage_index >= active->stage_count) {
            free_active_reservoir(active);
            continue;
        }

        start_ms = fluid_debug_now_ms();
        active->stage_index++;
        for (col_index = 0u; col_index < active->total_count; ++col_index) {
            SdkBulkWaterColumn* col = &active->cols[col_index];
            float fraction = (float)active->stage_index / (float)active->stage_count;
            float stage_surface_e = (active->stage_index >= active->stage_count)
                ? active->target_surface_e
                : (col->initial_surface_e + (active->target_surface_e - col->initial_surface_e) * fraction);
            apply_open_top_column_target(cm,
                                         col->wx, col->wz,
                                         col->barrier_y,
                                         col->current_surface_e,
                                         stage_surface_e,
                                         (BlockType)col->cap_block);
            col->current_surface_e = stage_surface_e;
        }
        end_ms = fluid_debug_now_ms();

        g_fluid_debug_info.mechanism = SDK_FLUID_DEBUG_MECH_BULK_RESERVOIR;
        g_fluid_debug_info.last_seed_wx = active->seed_wx;
        g_fluid_debug_info.last_seed_wy = active->seed_wy;
        g_fluid_debug_info.last_seed_wz = active->seed_wz;
        g_fluid_debug_info.reservoir_columns = active->reservoir_count;
        g_fluid_debug_info.total_columns = active->total_count;
        g_fluid_debug_info.visited_columns = active->total_count;
        g_fluid_debug_info.spill_columns = active->total_count - active->reservoir_count;
        g_fluid_debug_info.worker_count = active->worker_count;
        g_fluid_debug_info.total_volume = active->total_volume;
        g_fluid_debug_info.target_surface_e = active->target_surface_e;
        g_fluid_debug_info.solve_ms = active->solve_ms;
        g_fluid_debug_info.stage_index = active->stage_index;
        g_fluid_debug_info.stage_count = active->stage_count;
        g_fluid_debug_info.truncated_flags = active->status_flags;
        g_fluid_debug_info.apply_ms = (float)(end_ms - start_ms);
        fluid_debug_set_reason("BULK STAGE");

        if (active->stage_index >= active->stage_count) {
            free_active_reservoir(active);
        }
    }
}

static int try_bulk_equalize_open_water(SdkChunkManager* cm, int seed_wx, int seed_wy, int seed_wz)
{
    SdkReservoirJob job;
    if (!build_reservoir_snapshot(cm, seed_wx, seed_wy, seed_wz, &job)) return 0;
    if (!enqueue_reservoir_job(&job)) {
        free_reservoir_job(&job);
        return 0;
    }

    g_fluid_debug_info.mechanism = SDK_FLUID_DEBUG_MECH_BULK_RESERVOIR;
    g_fluid_debug_info.last_seed_wx = seed_wx;
    g_fluid_debug_info.last_seed_wy = seed_wy;
    g_fluid_debug_info.last_seed_wz = seed_wz;
    g_fluid_debug_info.reservoir_columns = job.reservoir_count;
    g_fluid_debug_info.total_columns = job.total_count;
    g_fluid_debug_info.visited_columns = job.total_count;
    g_fluid_debug_info.spill_columns = job.total_count - job.reservoir_count;
    g_fluid_debug_info.stage_index = 0u;
    g_fluid_debug_info.stage_count = 0u;
    g_fluid_debug_info.truncated_flags = job.status_flags;
    g_fluid_debug_info.solve_ms = 0.0f;
    g_fluid_debug_info.apply_ms = 0.0f;
    fluid_debug_set_reason("BULK DEFERRED");
    {
        char dbg[256];
        sprintf_s(dbg, sizeof(dbg),
                  "[FLUID] BULK DEFERRED seed(%d,%d,%d) res=%u total=%u flags=0x%X\n",
                  seed_wx, seed_wy, seed_wz,
                  job.reservoir_count, job.total_count, job.status_flags);
        OutputDebugStringA(dbg);
    }
    return 1;
}

void sdk_simulation_on_block_changed(SdkChunkManager* cm, int wx, int wy, int wz, BlockType old_type, BlockType new_type)
{
    int lx;
    int lz;
    SdkChunk* chunk;
    int seed_wx;
    int seed_wy;
    int seed_wz;

    if (!cm || wy < 0 || wy >= CHUNK_HEIGHT) return;
    chunk = chunk_from_world(cm, wx, wz, &lx, &lz);
    if (!chunk) return;

    if ((sdk_block_get_behavior_flags(old_type) & SDK_BLOCK_BEHAVIOR_FLUID) != 0u &&
        old_type != new_type) {
        remove_fluid_local(chunk, lx, wy, lz);
    }
    if ((sdk_block_get_behavior_flags(old_type) & SDK_BLOCK_BEHAVIOR_GRANULAR) != 0u &&
        (sdk_block_get_behavior_flags(new_type) & SDK_BLOCK_BEHAVIOR_GRANULAR) == 0u) {
        remove_loose_local(chunk, lx, wy, lz);
    }
    if ((sdk_block_get_behavior_flags(new_type) & SDK_BLOCK_BEHAVIOR_GRANULAR) != 0u) {
        add_loose_local(chunk, lx, wy, lz);
    }

    enqueue_world_neighborhood(cm, wx, wy, wz);
    bump_reservoir_generation();

    if (find_adjacent_water_seed(cm, wx, wy, wz, &seed_wx, &seed_wy, &seed_wz)) {
        uint32_t surface_count = estimate_connected_surface_water_columns(
            cm, seed_wx, seed_wy, seed_wz, SDK_SIM_FORCE_BULK_MIN_COLUMNS);
        int require_bulk = (surface_count >= SDK_SIM_FORCE_BULK_MIN_COLUMNS);

        if (!try_bulk_equalize_open_water(cm, seed_wx, seed_wy, seed_wz)) {
            if (require_bulk) {
                g_fluid_debug_info.mechanism = SDK_FLUID_DEBUG_MECH_BULK_RESERVOIR;
                g_fluid_debug_info.last_seed_wx = seed_wx;
                g_fluid_debug_info.last_seed_wy = seed_wy;
                g_fluid_debug_info.last_seed_wz = seed_wz;
                g_fluid_debug_info.reservoir_columns = surface_count;
                g_fluid_debug_info.total_columns = surface_count;
                g_fluid_debug_info.worker_count = 0;
                g_fluid_debug_info.total_volume = 0.0f;
                g_fluid_debug_info.target_surface_e = 0.0f;
                g_fluid_debug_info.solve_ms = 0.0f;
                fluid_debug_set_reason("BULK REQUIRED");
                {
                    char dbg[224];
                    sprintf_s(dbg, sizeof(dbg),
                              "[FLUID] BULK REQUIRED seed(%d,%d,%d) surface=%u; local wake skipped\n",
                              seed_wx, seed_wy, seed_wz, surface_count);
                    OutputDebugStringA(dbg);
                }
                return;
            }
            int seed_top_y = column_surface_water_y(cm, seed_wx, seed_wz, seed_wy);
            if (seed_top_y >= 0) {
                g_fluid_debug_info.mechanism = SDK_FLUID_DEBUG_MECH_LOCAL_WAKE;
                g_fluid_debug_info.last_seed_wx = seed_wx;
                g_fluid_debug_info.last_seed_wy = seed_wy;
                g_fluid_debug_info.last_seed_wz = seed_wz;
                g_fluid_debug_info.reservoir_columns = 0u;
                g_fluid_debug_info.total_columns = 0u;
                g_fluid_debug_info.worker_count = 1;
                g_fluid_debug_info.total_volume = 0.0f;
                g_fluid_debug_info.target_surface_e = (float)seed_top_y;
                g_fluid_debug_info.solve_ms = 0.0f;
                fluid_debug_set_reason("LOCAL WAKE");
                {
                    char dbg[192];
                    sprintf_s(dbg, sizeof(dbg),
                              "[FLUID] LOCAL WAKE seed(%d,%d,%d) top=%d\n",
                              seed_wx, seed_wy, seed_wz, seed_top_y);
                    OutputDebugStringA(dbg);
                }
                activate_loaded_water_surface_band(cm, seed_wx, seed_wz, seed_top_y, wy);
            }
        }
    }
}

void sdk_simulation_on_chunk_loaded(SdkChunkManager* cm, SdkChunk* chunk)
{
    uint32_t i;
    int lx;
    int ly;
    int lz;

    (void)cm;
    if (!chunk || !chunk->sim_state) return;
    bump_reservoir_generation();

    for (i = 0u; i < chunk->sim_state->fluid_count; ++i) {
        sdk_simulation_unpack_local_key(chunk->sim_state->fluid_cells[i].key, &lx, &ly, &lz);
        sdk_simulation_enqueue_local(chunk, lx, ly, lz);
    }
}

static int is_granular_block(BlockType block)
{
    return (sdk_block_get_behavior_flags(block) & SDK_BLOCK_BEHAVIOR_GRANULAR) != 0u;
}

static int can_accept_water(BlockType block)
{
    return block == BLOCK_AIR || block == BLOCK_WATER;
}

static int is_open_cell(SdkChunkManager* cm, int wx, int wy, int wz)
{
    BlockType block;
    fluid_fill_at_world(cm, wx, wy, wz, &block);
    return block == BLOCK_AIR || block == BLOCK_WATER;
}

static int transfer_water(SdkChunkManager* cm,
                          int src_wx, int src_wy, int src_wz,
                          int dst_wx, int dst_wy, int dst_wz,
                          uint8_t limit)
{
    BlockType src_block;
    BlockType dst_block;
    uint8_t src_fill;
    uint8_t dst_fill;
    uint8_t capacity;
    uint8_t transfer;
    int dy;

    src_fill = fluid_fill_at_world(cm, src_wx, src_wy, src_wz, &src_block);
    dst_fill = fluid_fill_at_world(cm, dst_wx, dst_wy, dst_wz, &dst_block);

    if (src_block != BLOCK_WATER || src_fill == 0u || !can_accept_water(dst_block)) return 0;
    capacity = (dst_fill >= 255u) ? 0u : (uint8_t)(255u - dst_fill);
    if (capacity == 0u) return 0;

    transfer = src_fill;
    if (transfer > capacity) transfer = capacity;
    if (transfer > limit) transfer = limit;
    if (transfer == 0u) return 0;

    set_fluid_fill_world(cm, dst_wx, dst_wy, dst_wz, BLOCK_WATER, (uint8_t)(dst_fill + transfer));
    set_fluid_fill_world(cm, src_wx, src_wy, src_wz, BLOCK_WATER, (uint8_t)(src_fill - transfer));
    
    dy = dst_wy - src_wy;
    if (dy < 0) {
        sdk_simulation_enqueue_world(cm, dst_wx, dst_wy, dst_wz);
        if (src_fill > transfer) {
            sdk_simulation_enqueue_world(cm, src_wx, src_wy, src_wz);
        }
    } else {
        sdk_simulation_enqueue_world(cm, src_wx, src_wy, src_wz);
        sdk_simulation_enqueue_world(cm, dst_wx, dst_wy, dst_wz);
        if (src_wx != dst_wx || src_wz != dst_wz) {
            sdk_simulation_enqueue_world(cm, dst_wx, dst_wy - 1, dst_wz);
        }
    }
    return 1;
}

static int move_loose_block(SdkChunkManager* cm,
                            int src_wx, int src_wy, int src_wz,
                            int dst_wx, int dst_wy, int dst_wz)
{
    int src_lx;
    int src_lz;
    int dst_lx;
    int dst_lz;
    SdkChunk* src_chunk;
    SdkChunk* dst_chunk;
    BlockType src_block;
    BlockType dst_block;
    uint8_t dst_fill;

    src_chunk = chunk_from_world(cm, src_wx, src_wz, &src_lx, &src_lz);
    dst_chunk = chunk_from_world(cm, dst_wx, dst_wz, &dst_lx, &dst_lz);
    if (!src_chunk || !dst_chunk || src_wy < 0 || src_wy >= CHUNK_HEIGHT || dst_wy < 0 || dst_wy >= CHUNK_HEIGHT) {
        return 0;
    }

    src_block = sdk_chunk_get_block(src_chunk, src_lx, src_wy, src_lz);
    dst_block = sdk_chunk_get_block(dst_chunk, dst_lx, dst_wy, dst_lz);
    if (!is_granular_block(src_block)) return 0;
    if (!(dst_block == BLOCK_AIR || dst_block == BLOCK_WATER)) return 0;

    dst_fill = sdk_simulation_get_fluid_fill(dst_chunk, dst_lx, dst_wy, dst_lz);
    clear_runtime_local(dst_chunk, dst_lx, dst_wy, dst_lz);
    set_block_world(cm, dst_wx, dst_wy, dst_wz, src_block);

    if (dst_block == BLOCK_WATER && dst_fill > 0u) {
        set_fluid_fill_world(cm, src_wx, src_wy, src_wz, BLOCK_WATER, dst_fill);
    } else {
        clear_runtime_local(src_chunk, src_lx, src_wy, src_lz);
        set_block_world(cm, src_wx, src_wy, src_wz, BLOCK_AIR);
    }

    remove_loose_local(src_chunk, src_lx, src_wy, src_lz);
    add_loose_local(dst_chunk, dst_lx, dst_wy, dst_lz);
    sdk_simulation_enqueue_world(cm, dst_wx, dst_wy, dst_wz);
    if (dst_wy > 0) {
        sdk_simulation_enqueue_world(cm, dst_wx, dst_wy - 1, dst_wz);
    }
    return 1;
}

static int water_head_count(SdkChunkManager* cm, int wx, int wy, int wz, int max_cells)
{
    int count = 0;
    int y;

    if (!cm || max_cells <= 0 || wy < 0 || wy >= CHUNK_HEIGHT) return 0;
    for (y = wy; y < CHUNK_HEIGHT && count < max_cells; ++y) {
        BlockType block;
        uint8_t fill = fluid_fill_at_world(cm, wx, y, wz, &block);
        if (block != BLOCK_WATER || fill == 0u) break;
        count++;
        if (fill < 255u) break;
    }
    return count;
}

static int pull_water_down_from_above(SdkChunkManager* cm, int wx, int wy, int wz)
{
    BlockType dst_block;
    BlockType src_block;
    uint8_t dst_fill;
    uint8_t src_fill;
    uint8_t transfer;

    if (!cm || wy < 0 || wy >= CHUNK_HEIGHT - 1) return 0;

    dst_fill = fluid_fill_at_world(cm, wx, wy, wz, &dst_block);
    if (dst_block != BLOCK_WATER || dst_fill >= 255u) return 0;

    src_fill = fluid_fill_at_world(cm, wx, wy + 1, wz, &src_block);
    if (src_block != BLOCK_WATER || src_fill == 0u) return 0;

    transfer = (uint8_t)(255u - dst_fill);
    if (transfer > src_fill) transfer = src_fill;
    if (transfer == 0u) return 0;

    set_fluid_fill_world(cm, wx, wy, wz, BLOCK_WATER, (uint8_t)(dst_fill + transfer));
    set_fluid_fill_world(cm, wx, wy + 1, wz, BLOCK_WATER, (uint8_t)(src_fill - transfer));
    sdk_simulation_enqueue_world(cm, wx, wy, wz);
    if (src_fill > transfer) {
        sdk_simulation_enqueue_world(cm, wx, wy + 1, wz);
    }
    return 1;
}

static int process_fluid_cell(SdkChunkManager* cm, int wx, int wy, int wz)
{
    static const int lateral_dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    BlockType current_block;
    uint8_t current_fill;
    int moved = 0;
    int i;

    current_fill = fluid_fill_at_world(cm, wx, wy, wz, &current_block);
    if (current_block != BLOCK_WATER || current_fill == 0u) return 0;

    if (wy > 0 && transfer_water(cm, wx, wy, wz, wx, wy - 1, wz, 255u)) {
        moved = 1;
    }

    if (!moved && wy > 0) {
        for (i = 0; i < 4; ++i) {
            int dx = lateral_dirs[i][0];
            int dz = lateral_dirs[i][1];
            if (is_open_cell(cm, wx + dx, wy, wz + dz) &&
                is_open_cell(cm, wx + dx, wy - 1, wz + dz) &&
                transfer_water(cm, wx, wy, wz, wx + dx, wy - 1, wz + dz, 255u)) {
                moved = 1;
                break;
            }
        }
    }

    if (moved) return 1;

    for (i = 0; i < 4; ++i) {
        int best_dir = -1;
        uint8_t best_fill = 255u;
        uint8_t limit;
        int dir;

        current_fill = fluid_fill_at_world(cm, wx, wy, wz, &current_block);
        if (current_block != BLOCK_WATER || current_fill == 0u) break;

        for (dir = 0; dir < 4; ++dir) {
            int dx = lateral_dirs[dir][0];
            int dz = lateral_dirs[dir][1];
            BlockType neighbor_block;
            uint8_t neighbor_fill = fluid_fill_at_world(cm, wx + dx, wy, wz + dz, &neighbor_block);
            if (!can_accept_water(neighbor_block)) continue;
            if (neighbor_fill < best_fill) {
                best_fill = neighbor_fill;
                best_dir = dir;
            }
        }

        if (best_dir < 0 || current_fill <= best_fill + 1u) break;

        {
            uint16_t delta = (uint16_t)(current_fill - best_fill);
            limit = (uint8_t)((delta * 3u) / 4u);
            if (delta >= 48u && limit < 24u) limit = 24u;
            if (current_fill >= 255u) {
                int pressure_limit = 192;
                if (pressure_limit > limit) limit = (uint8_t)pressure_limit;
            } else if (current_fill >= 224u && limit < 96u) {
                limit = 96u;
            }
        }
        if (current_fill >= 255u) {
            if (limit < 160u) limit = 160u;
        }
        if (limit == 0u) limit = 1u;
        if (limit > 255u - best_fill) limit = (uint8_t)(255u - best_fill);
        if (limit > 255u) limit = 255u;
        if (transfer_water(cm, wx, wy, wz,
                           wx + lateral_dirs[best_dir][0], wy, wz + lateral_dirs[best_dir][1],
                           limit)) {
            moved = 1;
            if (!pull_water_down_from_above(cm, wx, wy, wz)) {
                break;
            }
            continue;
        }
        break;
    }

    return moved;
}

static int process_loose_cell(SdkChunkManager* cm, int wx, int wy, int wz)
{
    static const int slide_dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    int lx;
    int lz;
    SdkChunk* chunk;
    BlockType block;
    BlockType below_block;
    int i;

    chunk = chunk_from_world(cm, wx, wz, &lx, &lz);
    if (!chunk || wy < 0 || wy >= CHUNK_HEIGHT) return 0;
    block = sdk_chunk_get_block(chunk, lx, wy, lz);
    if (!is_granular_block(block)) {
        remove_loose_local(chunk, lx, wy, lz);
        return 0;
    }

    if (wy > 0) {
        fluid_fill_at_world(cm, wx, wy - 1, wz, &below_block);
        if (below_block == BLOCK_AIR || below_block == BLOCK_WATER) {
            return move_loose_block(cm, wx, wy, wz, wx, wy - 1, wz);
        }
    }

    if (wy > 0) {
        for (i = 0; i < 4; ++i) {
            int dx = slide_dirs[i][0];
            int dz = slide_dirs[i][1];
            if (is_open_cell(cm, wx + dx, wy, wz + dz) &&
                is_open_cell(cm, wx + dx, wy - 1, wz + dz)) {
                return move_loose_block(cm, wx, wy, wz, wx + dx, wy - 1, wz + dz);
            }
        }
    }

    remove_loose_local(chunk, lx, wy, lz);
    return 0;
}

static int chunk_camera_distance(const SdkChunkManager* cm, const SdkChunk* chunk)
{
    int dx;
    int dz;

    if (!cm || !chunk) return 0;
    dx = chunk->cx - cm->cam_cx;
    dz = chunk->cz - cm->cam_cz;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return (dx > dz) ? dx : dz;
}

static int compute_adaptive_sim_budget(const SdkChunkManager* cm, int base_cells, uint32_t* out_dirty_cells)
{
    uint32_t dirty_cells = 0u;
    uint32_t near_dirty_cells = 0u;
    uint32_t active_chunks = 0u;
    int budget = base_cells;
    int slot_index;
    int max_budget;

    if (out_dirty_cells) *out_dirty_cells = 0u;
    if (!cm || base_cells <= 0) return base_cells;

    for (slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        const SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at_const(cm, slot_index);
        const SdkChunk* chunk;
        uint32_t dirty_count;
        int distance;

        if (!slot || !slot->occupied) continue;
        if (slot->role != SDK_CHUNK_ROLE_PRIMARY &&
            slot->role != SDK_CHUNK_ROLE_WALL_SUPPORT &&
            slot->role != SDK_CHUNK_ROLE_FRONTIER) {
            continue;
        }
        chunk = &slot->chunk;
        if (!chunk->blocks || !chunk->sim_state || chunk->sim_state->dirty_count == 0u) continue;

        dirty_count = chunk->sim_state->dirty_count;
        dirty_cells += dirty_count;
        active_chunks++;
        distance = chunk_camera_distance(cm, chunk);
        if (distance <= 1) {
            near_dirty_cells += dirty_count;
        } else if (distance == 2) {
            near_dirty_cells += dirty_count / 2u;
        }
    }

    if (out_dirty_cells) *out_dirty_cells = dirty_cells;
    if (dirty_cells <= (uint32_t)base_cells) return base_cells;

    budget += (int)((dirty_cells - (uint32_t)base_cells) / 2u);
    budget += (int)(near_dirty_cells / 2u);
    if (active_chunks > 4u) {
        int bonus = base_cells / 8;
        if (bonus < 1) bonus = 1;
        budget += (int)(active_chunks - 4u) * bonus;
    }

    max_budget = base_cells * SDK_SIM_ADAPTIVE_BUDGET_MAX_MULTIPLIER;
    if (budget > max_budget) budget = max_budget;
    if (budget < base_cells) budget = base_cells;
    return budget;
}

static int reservoir_scheduler_busy(void)
{
    uint32_t i;
    if (g_reservoir_scheduler.job_count > 0 || g_reservoir_scheduler.result_count > 0) return 1;
    for (i = 0u; i < SDK_SIM_MAX_ACTIVE_RESERVOIRS; ++i) {
        if (g_reservoir_scheduler.active[i].active) return 1;
    }
    return 0;
}

static int maybe_trigger_proactive_bulk_equalization(SdkChunkManager* cm, uint32_t dirty_cells, int base_cells)
{
    int slot_index;
    int scanned_chunks = 0;

    if (!cm || dirty_cells < (uint32_t)(base_cells * 2)) return 0;
    if (reservoir_scheduler_busy()) return 0;

    for (slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(cm, slot_index);
        SdkChunk* chunk;
        uint32_t cell_index;
        int distance;

        if (!slot || !slot->occupied) continue;
        if (slot->role != SDK_CHUNK_ROLE_PRIMARY &&
            slot->role != SDK_CHUNK_ROLE_WALL_SUPPORT &&
            slot->role != SDK_CHUNK_ROLE_FRONTIER) {
            continue;
        }
        chunk = &slot->chunk;
        if (!chunk->blocks || !chunk->sim_state || chunk->sim_state->dirty_count == 0u || chunk->sim_state->fluid_count == 0u) {
            continue;
        }

        distance = chunk_camera_distance(cm, chunk);
        if (distance > 2) continue;
        if (chunk->sim_state->dirty_count < (uint32_t)(base_cells / 8) && distance > 1) continue;

        for (cell_index = 0u;
             cell_index < chunk->sim_state->fluid_count && cell_index < SDK_SIM_PROACTIVE_BULK_SCAN_CELLS;
             ++cell_index) {
            int lx;
            int ly;
            int lz;
            int wx;
            int wz;
            BlockType block;
            uint8_t fill;
            uint32_t surface_count;

            sdk_simulation_unpack_local_key(chunk->sim_state->fluid_cells[cell_index].key, &lx, &ly, &lz);
            wx = sdk_chunk_to_world_x(lx, chunk->cx);
            wz = sdk_chunk_to_world_z(lz, chunk->cz);
            block = sdk_chunk_get_block(chunk, lx, ly, lz);
            fill = sdk_simulation_get_fluid_fill(chunk, lx, ly, lz);
            if (block != BLOCK_WATER || fill == 0u) continue;

            surface_count = estimate_connected_surface_water_columns(
                cm, wx, ly, wz, SDK_SIM_BULK_WATER_MIN_COLUMNS);
            if (surface_count >= SDK_SIM_BULK_WATER_MIN_COLUMNS &&
                try_bulk_equalize_open_water(cm, wx, ly, wz)) {
                fluid_debug_set_reason("BULK PRESSURE");
                return 1;
            }
        }

        scanned_chunks++;
        if (scanned_chunks >= SDK_SIM_PROACTIVE_BULK_SCAN_CHUNKS) break;
    }

    return 0;
}

void sdk_simulation_tick_chunk_manager(SdkChunkManager* cm, int max_cells)
{
    uint32_t total_slots;
    uint32_t cursor;
    uint32_t attempts = 0u;
    uint32_t dirty_cells = 0u;
    int budget;
    int processed = 0;

    if (!cm || max_cells <= 0) return;
    process_async_reservoir_results();
    advance_active_reservoirs(cm);
    budget = compute_adaptive_sim_budget(cm, max_cells, &dirty_cells);
    maybe_trigger_proactive_bulk_equalization(cm, dirty_cells, max_cells);
    total_slots = (uint32_t)sdk_chunk_manager_slot_capacity();
    if (total_slots == 0u) return;

    cursor = g_sim_round_robin_cursor % total_slots;
    while (processed < budget && attempts < total_slots * 2u) {
        uint32_t slot_index = cursor % total_slots;
        SdkChunkResidentSlot* resident = sdk_chunk_manager_get_slot_at(cm, (int)slot_index);
        SdkChunk* chunk;
        int chunk_budget;
        int distance;

        cursor++;
        attempts++;

        if (!resident || !resident->occupied) continue;
        if (resident->role != SDK_CHUNK_ROLE_PRIMARY &&
            resident->role != SDK_CHUNK_ROLE_WALL_SUPPORT &&
            resident->role != SDK_CHUNK_ROLE_FRONTIER) {
            continue;
        }
        chunk = &resident->chunk;
        if (!chunk->blocks || !chunk->sim_state || chunk->sim_state->dirty_count == 0u) continue;

        distance = chunk_camera_distance(cm, chunk);
        if (distance <= 1) chunk_budget = 8;
        else if (distance == 2) chunk_budget = 4;
        else chunk_budget = 2;
        if (chunk->sim_state->dirty_count > (uint32_t)budget / 2u) {
            chunk_budget += 2;
        }

        while (processed < budget && chunk_budget-- > 0) {
            SdkSimCellKey key;
            int lx;
            int ly;
            int lz;
            int wx;
            int wz;
            BlockType block;

            if (!dequeue_key(chunk->sim_state, &key)) break;
            sdk_simulation_unpack_local_key(key, &lx, &ly, &lz);
            if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)ly >= CHUNK_HEIGHT || (unsigned)lz >= CHUNK_DEPTH) {
                continue;
            }

            wx = sdk_chunk_to_world_x(lx, chunk->cx);
            wz = sdk_chunk_to_world_z(lz, chunk->cz);
            block = sdk_chunk_get_block(chunk, lx, ly, lz);

            if ((sdk_block_get_behavior_flags(block) & SDK_BLOCK_BEHAVIOR_FLUID) != 0u) {
                process_fluid_cell(cm, wx, ly, wz);
            } else if ((sdk_block_get_behavior_flags(block) & SDK_BLOCK_BEHAVIOR_GRANULAR) != 0u) {
                process_loose_cell(cm, wx, ly, wz);
            } else {
                clear_runtime_local(chunk, lx, ly, lz);
            }
            processed++;
        }
    }

    g_sim_round_robin_cursor = cursor % total_slots;
    g_fluid_debug_info.tick_processed = (uint32_t)processed;
}

void sdk_simulation_get_debug_info(const SdkChunkManager* cm, SdkFluidDebugInfo* out_info)
{
    uint32_t dirty_cells = 0u;
    uint32_t active_chunks = 0u;
    int slot_index;

    if (!out_info) return;
    *out_info = g_fluid_debug_info;

    if (!cm) return;
    for (slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        const SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at_const(cm, slot_index);
        const SdkChunk* chunk;
        if (!slot || !slot->occupied) continue;
        if (slot->role != SDK_CHUNK_ROLE_PRIMARY &&
            slot->role != SDK_CHUNK_ROLE_WALL_SUPPORT &&
            slot->role != SDK_CHUNK_ROLE_FRONTIER) {
            continue;
        }
        chunk = &slot->chunk;
        if (!chunk->blocks || !chunk->sim_state) continue;
        if (chunk->sim_state->dirty_count > 0u) active_chunks++;
        dirty_cells += chunk->sim_state->dirty_count;
    }

    out_info->dirty_cells = dirty_cells;
    out_info->active_chunks = active_chunks;
}

char* sdk_simulation_encode_chunk_fluids(const SdkChunk* chunk)
{
    SdkChunkSimState* state;
    uint32_t i;
    size_t cap = 32u;
    size_t len = 0u;
    char* text;
    int written;

    if (!chunk || !chunk->sim_state) {
        text = (char*)malloc(1u);
        if (text) text[0] = '\0';
        return text;
    }

    state = chunk->sim_state;
    text = (char*)malloc(cap);
    if (!text) return NULL;
    text[0] = '\0';

    for (i = 0u; i < state->fluid_count; ++i) {
        const SdkFluidCellState* cell = &state->fluid_cells[i];
        if (cell->fill == 0u || cell->fill >= 255u) continue;
        for (;;) {
            written = snprintf(text + len, cap - len, "%u:%u:%u:%u;", cell->key, cell->fill, cell->material_kind, cell->flags);
            if (written < 0) {
                free(text);
                return NULL;
            }
            if ((size_t)written < cap - len) {
                len += (size_t)written;
                break;
            }
            cap *= 2u;
            text = (char*)realloc(text, cap);
            if (!text) return NULL;
        }
    }

    return text;
}

int sdk_simulation_decode_chunk_fluids(SdkChunk* chunk, const char* encoded)
{
    char* copy;
    char* ctx = NULL;
    char* token;

    if (!chunk || !chunk->sim_state || !encoded) return 0;
    sdk_simulation_chunk_state_clear(chunk->sim_state);
    if (encoded[0] == '\0') return 1;

    copy = _strdup(encoded);
    if (!copy) return 0;

    token = strtok_s(copy, ";", &ctx);
    while (token) {
        unsigned long key = 0ul;
        unsigned long fill = 0ul;
        unsigned long material = 0ul;
        unsigned long flags = 0ul;
        int lx;
        int ly;
        int lz;
        if (sscanf_s(token, "%lu:%lu:%lu:%lu", &key, &fill, &material, &flags) == 4) {
            sdk_simulation_unpack_local_key((SdkSimCellKey)key, &lx, &ly, &lz);
            if ((unsigned)lx < CHUNK_WIDTH && (unsigned)ly < CHUNK_HEIGHT && (unsigned)lz < CHUNK_DEPTH &&
                fill > 0ul && fill < 255ul) {
                set_partial_fluid_local(chunk, lx, ly, lz, (uint8_t)fill, (uint8_t)material);
                sdk_simulation_enqueue_local(chunk, lx, ly, lz);
                {
                    int index = find_fluid_index(chunk->sim_state, (SdkSimCellKey)key);
                    if (index >= 0) chunk->sim_state->fluid_cells[index].flags = (uint8_t)flags;
                }
            }
        }
        token = strtok_s(NULL, ";", &ctx);
    }

    free(copy);
    return 1;
}
