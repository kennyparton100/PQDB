/**
 * sdk_chunk_streamer.c -- Asynchronous chunk generation and meshing.
 */
#include "sdk_chunk_streamer.h"
#include "../sdk_chunk.h"
#include "../../CoordinateSpaces/sdk_coordinate_space_runtime.h"
#include "../../ConstructionCells/sdk_construction_cells.h"
#include "../../Worldgen/sdk_worldgen.h"
#include "../../Worldgen/Internal/sdk_worldgen_internal.h"
#include "../../../MeshBuilder/sdk_mesh_builder.h"
#include "../../Simulation/sdk_simulation.h"
#include "../../Settlements/sdk_settlement.h"
#include "../../../API/Internal/sdk_load_trace.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SDK_CHUNK_STREAMER_MAX_WORKERS 16
#define SDK_CHUNK_STREAMER_JOB_CAPACITY SDK_CHUNK_MANAGER_MAX_DESIRED
#define SDK_CHUNK_STREAMER_RESULT_CAPACITY SDK_CHUNK_MANAGER_MAX_DESIRED
#define SDK_VERBOSE_WALL_DEBUG_LOGS 0

typedef enum {
    SDK_STREAM_TRACK_IDLE = 0,
    SDK_STREAM_TRACK_QUEUED,
    SDK_STREAM_TRACK_BUILDING
} SdkStreamTrackState;

typedef enum {
    SDK_CHUNK_JOB_GENERATE = 0,
    SDK_CHUNK_JOB_REMESH
} SdkChunkBuildJobType;

typedef enum {
    SDK_STREAM_WORKER_STAGE_IDLE = 0,
    SDK_STREAM_WORKER_STAGE_LOADING,
    SDK_STREAM_WORKER_STAGE_WORLDGEN,
    SDK_STREAM_WORKER_STAGE_MESHING,
    SDK_STREAM_WORKER_STAGE_QUEUING_RESULT
} SdkChunkStreamWorkerStage;

typedef enum {
    SDK_CHUNK_NEIGHBOR_WEST = 0,
    SDK_CHUNK_NEIGHBOR_EAST,
    SDK_CHUNK_NEIGHBOR_NORTH,
    SDK_CHUNK_NEIGHBOR_SOUTH,
    SDK_CHUNK_NEIGHBOR_NORTH_WEST,
    SDK_CHUNK_NEIGHBOR_NORTH_EAST,
    SDK_CHUNK_NEIGHBOR_SOUTH_WEST,
    SDK_CHUNK_NEIGHBOR_SOUTH_EAST,
    SDK_CHUNK_NEIGHBOR_COUNT
} SdkChunkNeighborIndex;

typedef struct {
    int          cx;
    int          cz;
    uint8_t      space_type;
    uint8_t      role;
    uint8_t      state;
    uint16_t     reserved;
    uint32_t     generation;
} SdkChunkStreamTrack;

typedef struct {
    int          cx;
    int          cz;
    uint32_t     generation;
} SdkDroppedRemeshRepair;

typedef struct {
    int          type;
    int          gx;
    int          gz;
    int          cx;
    int          cz;
    uint8_t      space_type;
    uint8_t      role;
    uint16_t     reserved1;
    uint32_t     generation;
    uint32_t     dirty_mask;
    SdkChunk*    snapshot_chunk;
    SdkChunk*    neighbor_chunks[SDK_CHUNK_NEIGHBOR_COUNT];
} SdkChunkBuildJob;

struct SdkChunkStreamerInternal;

typedef struct {
    HANDLE                          thread;
    SdkWorldGen                     worldgen;
    SdkMeshBuffer                   mesh_buf;
    SdkChunkManager                 temp_cm;
    struct SdkChunkStreamerInternal* owner;
    int                             debug_active;
    int                             debug_job_type;
    int                             debug_stage;
    int                             debug_cx;
    int                             debug_cz;
    ULONGLONG                       debug_stage_started_at;
} SdkChunkStreamWorker;

typedef struct SdkChunkStreamerInternal {
    CRITICAL_SECTION        lock;
    CONDITION_VARIABLE      jobs_cv;
    bool                    running;
    ULONGLONG               shutdown_started_at;
    int                     worker_count;
    SdkChunkStreamWorker    workers[SDK_CHUNK_STREAMER_MAX_WORKERS];
    SdkWorldDesc            world_desc;
    SdkPersistence*         persistence;
    SdkConstructionArchetypeRegistry* construction_registry;

    SdkChunkBuildJob        jobs[SDK_CHUNK_STREAMER_JOB_CAPACITY];
    int                     job_head;
    int                     job_count;

    SdkChunkBuildResult     results[SDK_CHUNK_STREAMER_RESULT_CAPACITY];
    int                     result_head;
    int                     result_count;

    SdkDroppedRemeshRepair  dropped_remeshes[SDK_CHUNK_STREAMER_RESULT_CAPACITY];
    int                     dropped_remesh_head;
    int                     dropped_remesh_count;

    SdkChunkStreamTrack     tracks[SDK_CHUNK_MANAGER_MAX_DESIRED];
} SdkChunkStreamerInternal;

static int ring_index(int head, int count, int capacity)
{
    /* Calculates the index into a circular ring buffer.
     * head: starting position in the buffer
     * count: offset from head (number of items to skip)
     * capacity: total size of the ring buffer
     * Returns the absolute index (head + count) modulo capacity.
     * Used for job queue, result queue, and dropped remesh queue indexing. */
    return (head + count) % capacity;
}

static int abs_i(int v)
{
    /* Returns the absolute value of an integer.
     * Simple helper to avoid depending on stdlib abs() macro.
     * Handles negative values by returning the negation, positive values unchanged. */
    return v < 0 ? -v : v;
}

static void queue_dropped_remesh_repair(SdkChunkStreamerInternal* impl, int cx, int cz, uint32_t generation)
{
    /* Queues a remesh job that was dropped due to full results queue for later retry.
     * When worker threads produce results but the results queue is full, the result
     * is dropped to prevent worker blocking. This function records the dropped chunk
     * coordinates so the main thread can retry scheduling the remesh later.
     *
     * Uses a circular ring buffer (dropped_remeshes) with FIFO eviction when full.
     * The oldest dropped remesh is evicted to make room for the new one if needed. */
    int out_index;

    if (!impl) return;
    if (impl->dropped_remesh_count >= SDK_CHUNK_STREAMER_RESULT_CAPACITY) {
        impl->dropped_remesh_head = ring_index(impl->dropped_remesh_head, 1, SDK_CHUNK_STREAMER_RESULT_CAPACITY);
        impl->dropped_remesh_count--;
    }

    out_index = ring_index(impl->dropped_remesh_head,
                           impl->dropped_remesh_count,
                           SDK_CHUNK_STREAMER_RESULT_CAPACITY);
    impl->dropped_remeshes[out_index].cx = cx;
    impl->dropped_remeshes[out_index].cz = cz;
    impl->dropped_remeshes[out_index].generation = generation;
    impl->dropped_remesh_count++;
}

static int target_supports_active_superchunk_wall(const SdkChunkManager* cm,
                                                  const SdkChunkResidencyTarget* target)
{
    /* Determines if a chunk target is needed for active superchunk wall support.
     * Wall support chunks are loaded in adjacent superchunks to provide neighbor
     * data for proper wall mesh generation at superchunk boundaries.
     *
     * For small render distances (< SDK_SUPERCHUNK_CHUNK_SPAN chunks), only explicit WALL_SUPPORT role counts.
     * For larger render distances, checks if chunk is in the SUPPORT stage of active wall.
     * Returns 1 (true) if the chunk should be loaded as wall support, 0 otherwise. */
    if (!cm || !target) return 0;
    if (sdk_chunk_manager_radius_from_grid_size(cm->grid_size) < SDK_SUPERCHUNK_CHUNK_SPAN) {
        return (SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_WALL_SUPPORT;
    }
    return sdk_superchunk_active_wall_stage_for_chunk(cm->primary_scx,
                                                      cm->primary_scz,
                                                      target->cx,
                                                      target->cz) == SDK_ACTIVE_WALL_STAGE_SUPPORT;
}

static int choose_worker_count(void)
{
    /* Determines the number of worker threads to create for chunk streaming.
     * Uses the number of logical CPU processors as the base count.
     * Constrains the count to valid range: minimum 1, maximum SDK_CHUNK_STREAMER_MAX_WORKERS (16).
     * Returns the calculated worker thread count for parallel chunk generation/meshing. */
    SYSTEM_INFO si;
    int count;

    GetSystemInfo(&si);
    count = (int)si.dwNumberOfProcessors;
    if (count < 1) count = 1;
    if (count > SDK_CHUNK_STREAMER_MAX_WORKERS) count = SDK_CHUNK_STREAMER_MAX_WORKERS;
    return count;
}

static void set_worker_debug_state(SdkChunkStreamWorker* worker,
                                   int active,
                                   int cx,
                                   int cz,
                                   int job_type,
                                   int stage)
{
    /* Updates debug tracking information for a worker thread.
     * Records the current job being processed (cx, cz coordinates),
     * job type (GENERATE or REMESH), and processing stage.
     * Also records the timestamp when the stage started for timing analysis.
     *
     * Thread-safe: acquires the streamer's critical section before modifying state.
     * Used by the debug UI to display what each worker is currently doing.
     *
     * active: 1 if worker is processing a job, 0 if idle
     * stage: one of SDK_STREAM_WORKER_STAGE_* values (LOADING, WORLDGEN, MESHING, etc.) */
    SdkChunkStreamerInternal* impl;

    if (!worker || !(impl = worker->owner)) return;
    EnterCriticalSection(&impl->lock);
    worker->debug_active = active;
    worker->debug_cx = cx;
    worker->debug_cz = cz;
    worker->debug_job_type = job_type;
    worker->debug_stage = stage;
    worker->debug_stage_started_at = active ? GetTickCount64() : 0u;
    LeaveCriticalSection(&impl->lock);
}

static const char* worker_stage_label(int stage)
{
    /* Returns a short human-readable label for a worker processing stage.
     * Used in debug output to indicate what phase of chunk processing
     * a worker thread is currently in.
     *
     * Labels: "load" (loading from persistence), "world" (world generation),
     *         "mesh" (mesh generation), "queue" (queuing result), "idle".
     * Returns "idle" for unknown/invalid stage values. */
    switch ((SdkChunkStreamWorkerStage)stage) {
        case SDK_STREAM_WORKER_STAGE_LOADING:       return "load";
        case SDK_STREAM_WORKER_STAGE_WORLDGEN:      return "world";
        case SDK_STREAM_WORKER_STAGE_MESHING:       return "mesh";
        case SDK_STREAM_WORKER_STAGE_QUEUING_RESULT:return "queue";
        case SDK_STREAM_WORKER_STAGE_IDLE:
        default:                                    return "idle";
    }
}

static const char* worker_job_type_label(int job_type)
{
    /* Returns a short human-readable label for a job type.
     * Used in debug output to indicate whether a worker is doing
     * initial chunk generation or remeshing an existing chunk.
     *
     * Labels: "remesh" (SDK_CHUNK_JOB_REMESH), "gen" (SDK_CHUNK_JOB_GENERATE).
     * Returns "gen" for unknown/invalid job types. */
    switch ((SdkChunkBuildJobType)job_type) {
        case SDK_CHUNK_JOB_REMESH:   return "remesh";
        case SDK_CHUNK_JOB_GENERATE:
        default:                     return "gen";
    }
}

static void release_built_chunk(SdkChunk* chunk);
static void clear_generate_track(SdkChunkStreamerInternal* impl, int cx, int cz, uint32_t generation);

static int count_active_workers_locked(const SdkChunkStreamerInternal* impl)
{
    int count = 0;
    int i;

    if (!impl) return 0;
    for (i = 0; i < impl->worker_count; ++i) {
        if (impl->workers[i].debug_active) {
            count++;
        }
    }
    return count;
}

static int discard_one_result_for_shutdown(SdkChunkStreamerInternal* impl)
{
    SdkChunkBuildResult result;

    if (!impl) return 0;
    memset(&result, 0, sizeof(result));

    EnterCriticalSection(&impl->lock);
    if (impl->result_count <= 0) {
        LeaveCriticalSection(&impl->lock);
        return 0;
    }

    result = impl->results[impl->result_head];
    impl->result_head = (impl->result_head + 1) % SDK_CHUNK_STREAMER_RESULT_CAPACITY;
    impl->result_count--;
    if (result.type == SDK_CHUNK_STREAM_RESULT_GENERATED) {
        clear_generate_track(impl, result.cx, result.cz, result.generation);
    }
    LeaveCriticalSection(&impl->lock);

    release_built_chunk(result.built_chunk);
    return 1;
}

static void clear_chunk_mesh_cpu_only(SdkChunk* chunk)
{
    /* Clears all CPU-side mesh data from a chunk without freeing the chunk itself.
     * Frees vertex buffers for all subchunks (regular and water), far meshes,
     * experimental far meshes, and exact overlay meshes.
     * Resets all mesh-related state flags and counters to initial values.
     *
     * Used when a chunk needs to be remeshed - clears old mesh data before
     * generating new mesh. Preserves the chunk's block data and metadata.
     * Does NOT touch GPU-side resources (those are managed separately). */
    int i;
    if (!chunk) return;
    for (i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        SdkChunkSubmesh* sub = &chunk->subchunks[i];
        SdkChunkSubmesh* water = &chunk->water_subchunks[i];
        if (sub->cpu_vertices) {
            free(sub->cpu_vertices);
            sub->cpu_vertices = NULL;
        }
        sub->vertex_count = 0;
        sub->vertex_capacity = 0;
        sub->dirty = false;
        sub->upload_dirty = false;
        sub->empty = true;
        memset(sub->bounds_min, 0, sizeof(sub->bounds_min));
        memset(sub->bounds_max, 0, sizeof(sub->bounds_max));
        sub->vertex_buffer = NULL;
        sub->vb_gpu = NULL;
        if (water->cpu_vertices) {
            free(water->cpu_vertices);
            water->cpu_vertices = NULL;
        }
        water->vertex_count = 0;
        water->vertex_capacity = 0;
        water->dirty = false;
        water->upload_dirty = false;
        water->empty = true;
        memset(water->bounds_min, 0, sizeof(water->bounds_min));
        memset(water->bounds_max, 0, sizeof(water->bounds_max));
        water->vertex_buffer = NULL;
        water->vb_gpu = NULL;
    }
    if (chunk->far_mesh.cpu_vertices) {
        free(chunk->far_mesh.cpu_vertices);
        chunk->far_mesh.cpu_vertices = NULL;
    }
    chunk->far_mesh.vertex_count = 0;
    chunk->far_mesh.vertex_capacity = 0;
    chunk->far_mesh.dirty = false;
    chunk->far_mesh.upload_dirty = false;
    chunk->far_mesh.empty = true;
    memset(chunk->far_mesh.bounds_min, 0, sizeof(chunk->far_mesh.bounds_min));
    memset(chunk->far_mesh.bounds_max, 0, sizeof(chunk->far_mesh.bounds_max));
    chunk->far_mesh.vertex_buffer = NULL;
    chunk->far_mesh.vb_gpu = NULL;
    if (chunk->experimental_far_mesh.cpu_vertices) {
        free(chunk->experimental_far_mesh.cpu_vertices);
        chunk->experimental_far_mesh.cpu_vertices = NULL;
    }
    chunk->experimental_far_mesh.vertex_count = 0;
    chunk->experimental_far_mesh.vertex_capacity = 0;
    chunk->experimental_far_mesh.dirty = false;
    chunk->experimental_far_mesh.upload_dirty = false;
    chunk->experimental_far_mesh.empty = true;
    memset(chunk->experimental_far_mesh.bounds_min, 0, sizeof(chunk->experimental_far_mesh.bounds_min));
    memset(chunk->experimental_far_mesh.bounds_max, 0, sizeof(chunk->experimental_far_mesh.bounds_max));
    chunk->experimental_far_mesh.vertex_buffer = NULL;
    chunk->experimental_far_mesh.vb_gpu = NULL;
    if (chunk->far_exact_overlay_mesh.cpu_vertices) {
        free(chunk->far_exact_overlay_mesh.cpu_vertices);
        chunk->far_exact_overlay_mesh.cpu_vertices = NULL;
    }
    chunk->far_exact_overlay_mesh.vertex_count = 0;
    chunk->far_exact_overlay_mesh.vertex_capacity = 0;
    chunk->far_exact_overlay_mesh.dirty = false;
    chunk->far_exact_overlay_mesh.upload_dirty = false;
    chunk->far_exact_overlay_mesh.empty = true;
    memset(chunk->far_exact_overlay_mesh.bounds_min, 0, sizeof(chunk->far_exact_overlay_mesh.bounds_min));
    memset(chunk->far_exact_overlay_mesh.bounds_max, 0, sizeof(chunk->far_exact_overlay_mesh.bounds_max));
    chunk->far_exact_overlay_mesh.vertex_buffer = NULL;
    chunk->far_exact_overlay_mesh.vb_gpu = NULL;
    chunk->dirty = false;
    chunk->geometry_dirty = 0u;
    chunk->far_mesh_dirty = 0u;
    chunk->upload_pending = 0u;
    chunk->gpu_upload_mode = SDK_CHUNK_GPU_UPLOAD_NONE;
    chunk->cpu_mesh_generation = 0u;
    chunk->gpu_mesh_generation = 0u;
    chunk->dirty_frame_age = 0u;
    chunk->empty = true;
    chunk->dirty_subchunks_mask = 0u;
    chunk->upload_subchunks_mask = 0u;
    chunk->active_subchunks_mask = 0u;
    chunk->vertex_count = 0u;
    memset(chunk->bounds_min, 0, sizeof(chunk->bounds_min));
    memset(chunk->bounds_max, 0, sizeof(chunk->bounds_max));
}

static void set_snapshot_dirty_mask(SdkChunk* chunk,
                                    uint32_t dirty_mask,
                                    int far_dirty,
                                    int experimental_far_dirty,
                                    int exact_overlay_dirty)
{
    int i;
    if (!chunk) return;
    clear_chunk_mesh_cpu_only(chunk);
    chunk->dirty_subchunks_mask = 0u;
    chunk->dirty = false;
    chunk->geometry_dirty = 0u;
    chunk->far_mesh_dirty = 0u;
    chunk->upload_pending = 0u;
    chunk->gpu_upload_mode = SDK_CHUNK_GPU_UPLOAD_NONE;
    for (i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        const uint32_t bit = (1u << i);
        chunk->subchunks[i].dirty = (dirty_mask & bit) != 0u;
        chunk->water_subchunks[i].dirty = (dirty_mask & bit) != 0u;
        if (chunk->subchunks[i].dirty) {
            chunk->dirty_subchunks_mask |= bit;
            chunk->dirty = true;
            chunk->geometry_dirty = 1u;
        }
    }
    chunk->far_mesh.dirty = far_dirty != 0;
    chunk->experimental_far_mesh.dirty = experimental_far_dirty != 0;
    chunk->far_exact_overlay_mesh.dirty = exact_overlay_dirty != 0;
    if (chunk->far_mesh.dirty || chunk->experimental_far_mesh.dirty || chunk->far_exact_overlay_mesh.dirty) {
        chunk->dirty = true;
        chunk->far_mesh_dirty = 1u;
    }
}

static SdkChunk* clone_chunk_snapshot(const SdkChunk* src, uint32_t dirty_mask)
{
    SdkChunk* dst;

    if (!src || !src->blocks) return NULL;
    dst = (SdkChunk*)calloc(1, sizeof(SdkChunk));
    if (!dst) return NULL;
    dst->cx = src->cx;
    dst->cz = src->cz;
    dst->space_type = src->space_type;
    dst->empty = src->empty;
    dst->far_mesh_excluded_block_count = src->far_mesh_excluded_block_count;
    dst->mesh_job_generation = src->mesh_job_generation;
    dst->target_mesh_generation = src->target_mesh_generation;
    dst->inflight_mesh_generation = src->inflight_mesh_generation;
    dst->cpu_mesh_generation = 0u;
    dst->gpu_mesh_generation = 0u;
    dst->dirty_frame_age = src->dirty_frame_age;
    dst->blocks = (SdkWorldCellCode*)malloc(CHUNK_TOTAL_BLOCKS * sizeof(SdkWorldCellCode));
    if (!dst->blocks) {
        release_built_chunk(dst);
        return NULL;
    }

    memcpy(dst->blocks, src->blocks, CHUNK_TOTAL_BLOCKS * sizeof(SdkWorldCellCode));
    if (src->sim_state && src->sim_state->fluid_count > 0u) {
        dst->sim_state = sdk_simulation_clone_chunk_state_for_snapshot(src->sim_state);
        if (!dst->sim_state) {
            release_built_chunk(dst);
            return NULL;
        }
    }
    if (src->construction_cells) {
        dst->construction_cells = sdk_construction_store_clone(src->construction_cells);
        if (!dst->construction_cells) {
            release_built_chunk(dst);
            return NULL;
        }
    }
    set_snapshot_dirty_mask(dst,
                            dirty_mask,
                            src->far_mesh.dirty,
                            src->experimental_far_mesh.dirty,
                            src->far_exact_overlay_mesh.dirty);
    return dst;
}

static void free_job_snapshot(SdkChunkBuildJob* job)
{
    int i;
    if (!job) return;
    release_built_chunk(job->snapshot_chunk);
    job->snapshot_chunk = NULL;
    for (i = 0; i < SDK_CHUNK_NEIGHBOR_COUNT; ++i) {
        release_built_chunk(job->neighbor_chunks[i]);
        job->neighbor_chunks[i] = NULL;
    }
}

static void move_snapshot_into_slot(SdkChunkResidentSlot* slot, SdkChunk** src_ptr)
{
    SdkChunk* src;
    if (!slot || !src_ptr || !*src_ptr) return;
    src = *src_ptr;
    slot->chunk = *src;
    slot->occupied = 1u;
    free(src);
    *src_ptr = NULL;
}

static int remesh_insert_snapshot(SdkChunkManager* temp_cm, SdkChunk** src_ptr, SdkChunkResidencyRole role)
{
    SdkChunkResidentSlot* slot;
    if (!temp_cm || !src_ptr || !*src_ptr) return 0;
    slot = sdk_chunk_manager_reserve_slot(temp_cm, (*src_ptr)->cx, (*src_ptr)->cz, role);
    if (!slot) return 0;
    move_snapshot_into_slot(slot, src_ptr);
    return 1;
}

static SdkChunk* mesh_snapshot_job(SdkChunkBuildJob* job, SdkChunk** center_snapshot_ptr, SdkMeshBuffer* mesh_buf, SdkChunkManager* temp_cm)
{
    SdkChunk* built = NULL;
    SdkChunk* center;
    int i;

    if (!job || !center_snapshot_ptr || !*center_snapshot_ptr || !mesh_buf || !temp_cm) return NULL;

    sdk_chunk_manager_shutdown(temp_cm);
    sdk_chunk_manager_init(temp_cm);

    remesh_insert_snapshot(temp_cm, center_snapshot_ptr, SDK_CHUNK_ROLE_PRIMARY);
    for (i = 0; i < SDK_CHUNK_NEIGHBOR_COUNT; ++i) {
        if (!job->neighbor_chunks[i]) continue;
        remesh_insert_snapshot(temp_cm, &job->neighbor_chunks[i], SDK_CHUNK_ROLE_FRONTIER);
    }
    sdk_chunk_manager_rebuild_lookup(temp_cm);

    center = sdk_chunk_manager_get_chunk(temp_cm, job->cx, job->cz);
    if (center) {
        sdk_mesh_set_thread_worldgen_debug_enabled(0);
        sdk_mesh_build_chunk(center, temp_cm, mesh_buf);
        sdk_mesh_set_thread_worldgen_debug_enabled(1);

        built = (SdkChunk*)malloc(sizeof(SdkChunk));
        if (built) {
            *built = *center;
            memset(center, 0, sizeof(SdkChunk));
        }
    }

    return built;
}

static SdkChunk* mesh_remesh_snapshot_job(SdkChunkBuildJob* job, SdkMeshBuffer* mesh_buf, SdkChunkManager* temp_cm)
{
    if (!job) return NULL;
    return mesh_snapshot_job(job, &job->snapshot_chunk, mesh_buf, temp_cm);
}

static void release_built_chunk(SdkChunk* chunk)
{
    if (!chunk) return;
    sdk_chunk_free(chunk);
    free(chunk);
}

static int find_track_index(const SdkChunkStreamerInternal* impl, int cx, int cz)
{
    int i;
    if (!impl) return -1;
    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_DESIRED; ++i) {
        const SdkChunkStreamTrack* track = &impl->tracks[i];
        if (track->state == SDK_STREAM_TRACK_IDLE && track->generation == 0u) continue;
        if (track->cx == cx && track->cz == cz) return i;
    }
    return -1;
}

static SdkChunkStreamTrack* find_or_alloc_track(SdkChunkStreamerInternal* impl, int cx, int cz)
{
    int i;
    int index;

    if (!impl) return NULL;
    index = find_track_index(impl, cx, cz);
    if (index >= 0) return &impl->tracks[index];

    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_DESIRED; ++i) {
        SdkChunkStreamTrack* track = &impl->tracks[i];
        if (track->generation != 0u) continue;
        memset(track, 0, sizeof(*track));
        track->cx = cx;
        track->cz = cz;
        return track;
    }
    return NULL;
}

static void clear_track(SdkChunkStreamTrack* track)
{
    if (!track) return;
    memset(track, 0, sizeof(*track));
}

typedef struct {
    uint32_t stone_bricks;
    uint32_t cobblestone;
    uint32_t crushed_stone;
    uint32_t compacted_fill;
} WallMaterialDebugCounts;

static WallMaterialDebugCounts count_wall_materials_in_chunk(const SdkChunk* chunk)
{
    WallMaterialDebugCounts counts;
    uint32_t i;

    memset(&counts, 0, sizeof(counts));
    if (!chunk || !chunk->blocks) return counts;

    for (i = 0; i < CHUNK_TOTAL_BLOCKS; ++i) {
        SdkWorldCellCode code = chunk->blocks[i];
        BlockType block = sdk_world_cell_decode_full_block(code);
        switch (block) {
            case BLOCK_STONE_BRICKS:
                counts.stone_bricks++;
                break;
            case BLOCK_COBBLESTONE:
                counts.cobblestone++;
                break;
            case BLOCK_CRUSHED_STONE:
                counts.crushed_stone++;
                break;
            case BLOCK_COMPACTED_FILL:
                counts.compacted_fill++;
                break;
            default:
                break;
        }
    }

    return counts;
}

/* Helper: Log expected wall chunk block counts for debugging */
static void log_expected_wall_chunk_debug(const SdkChunk* chunk, int loaded_from_persistence)
{
    char dbg[320];
    WallMaterialDebugCounts wall_counts;
    SdkDerivedChunkSpaceInfo space_info;

    if (!chunk) return;
    if (!SDK_VERBOSE_WALL_DEBUG_LOGS) return;

    sdk_coordinate_space_describe_chunk(chunk->cx, chunk->cz, &space_info);
    if (space_info.is_wall_position) {
        wall_counts = count_wall_materials_in_chunk(chunk);
        sprintf_s(dbg, sizeof(dbg),
            "[WALL_CHUNK_DEBUG] Chunk (%d,%d) %s: stone_bricks=%u cobblestone=%u crushed_stone=%u compacted_fill=%u\n",
            chunk->cx, chunk->cz,
            loaded_from_persistence ? "LOADED" : "GENERATED",
            wall_counts.stone_bricks,
            wall_counts.cobblestone,
            wall_counts.crushed_stone,
            wall_counts.compacted_fill);
        sdk_debug_log_output(dbg);
    }
}

static void prune_idle_tracks(SdkChunkStreamerInternal* impl, const SdkChunkManager* cm)
{
    int i;
    if (!impl || !cm) return;
    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_DESIRED; ++i) {
        SdkChunkStreamTrack* track = &impl->tracks[i];
        if (track->generation == 0u) continue;
        if (track->state != SDK_STREAM_TRACK_IDLE) continue;
        if (!sdk_chunk_manager_is_desired(cm, track->cx, track->cz, NULL, NULL)) {
            clear_track(track);
        }
    }
}

static void clear_generate_track(SdkChunkStreamerInternal* impl, int cx, int cz, uint32_t generation)
{
    int index;
    if (!impl) return;
    index = find_track_index(impl, cx, cz);
    if (index < 0) return;
    if (impl->tracks[index].generation == generation) {
        clear_track(&impl->tracks[index]);
    }
}

static int generate_job_matches_desired_target(const SdkChunkManager* cm, const SdkChunkBuildJob* job)
{
    SdkChunkResidencyRole desired_role;
    uint32_t desired_generation = 0u;

    if (!cm || !job || job->type != SDK_CHUNK_JOB_GENERATE) return 0;
    if (!sdk_chunk_manager_is_desired(cm, job->cx, job->cz, &desired_role, &desired_generation)) {
        return 0;
    }
    return desired_generation == job->generation &&
           desired_role == (SdkChunkResidencyRole)job->role;
}

static void clear_queued_track_if_matching(SdkChunkStreamerInternal* impl, const SdkChunkBuildJob* job)
{
    int track_index;

    if (!impl || !job || job->type != SDK_CHUNK_JOB_GENERATE) return;
    track_index = find_track_index(impl, job->cx, job->cz);
    if (track_index < 0) return;
    if (impl->tracks[track_index].generation == job->generation &&
        impl->tracks[track_index].state == SDK_STREAM_TRACK_QUEUED) {
        clear_track(&impl->tracks[track_index]);
    }
}

static void prune_queued_generate_jobs(SdkChunkStreamerInternal* impl, const SdkChunkManager* cm)
{
    SdkChunkBuildJob* kept_jobs = NULL;
    int kept_count = 0;
    int old_head;
    int old_count;
    int i;

    if (!impl || !cm || impl->job_count <= 0) return;

    old_head = impl->job_head;
    old_count = impl->job_count;
    kept_jobs = (SdkChunkBuildJob*)calloc((size_t)old_count, sizeof(*kept_jobs));
    if (!kept_jobs) {
        return;
    }

    for (i = 0; i < old_count; ++i) {
        int index = ring_index(old_head, i, SDK_CHUNK_STREAMER_JOB_CAPACITY);
        SdkChunkBuildJob job = impl->jobs[index];
        int keep = 1;

        if (job.type == SDK_CHUNK_JOB_GENERATE &&
            !generate_job_matches_desired_target(cm, &job)) {
            clear_queued_track_if_matching(impl, &job);
            free_job_snapshot(&job);
            keep = 0;
        }

        memset(&impl->jobs[index], 0, sizeof(impl->jobs[index]));
        if (keep) {
            kept_jobs[kept_count++] = job;
        }
    }

    impl->job_head = 0;
    impl->job_count = kept_count;
    for (i = 0; i < kept_count; ++i) {
        impl->jobs[i] = kept_jobs[i];
    }
    free(kept_jobs);
}

static DWORD WINAPI chunk_stream_worker_proc(LPVOID param)
{
    SdkChunkStreamWorker* worker = (SdkChunkStreamWorker*)param;
    SdkChunkStreamerInternal* impl;

    if (!worker) return 0;
    impl = worker->owner;
    if (!impl) return 0;

    for (;;) {
        SdkChunkBuildJob job;
        bool have_job = false;
        bool discard_generate_job = false;

        EnterCriticalSection(&impl->lock);
        while (impl->running && impl->job_count == 0) {
            SleepConditionVariableCS(&impl->jobs_cv, &impl->lock, INFINITE);
        }

        if (!impl->running && impl->job_count == 0) {
            LeaveCriticalSection(&impl->lock);
            break;
        }

        if (impl->job_count > 0) {
            int index = impl->job_head;
            job = impl->jobs[index];
            impl->job_head = (impl->job_head + 1) % SDK_CHUNK_STREAMER_JOB_CAPACITY;
            impl->job_count--;
            if (job.type == SDK_CHUNK_JOB_GENERATE) {
                int track_index = find_track_index(impl, job.cx, job.cz);
                if (track_index >= 0 &&
                    impl->tracks[track_index].generation == job.generation &&
                    impl->tracks[track_index].state == SDK_STREAM_TRACK_QUEUED) {
                    impl->tracks[track_index].state = SDK_STREAM_TRACK_BUILDING;
                } else {
                    discard_generate_job = true;
                }
            }
            have_job = true;
        }
        LeaveCriticalSection(&impl->lock);

        if (!have_job) continue;
        if (discard_generate_job) {
            char dbg[128];
            sprintf_s(dbg, sizeof(dbg), "[WORKER] Discarding job cx=%d cz=%d gen=%d\n", job.cx, job.cz, job.generation);
            sdk_debug_log_output(dbg);
            free_job_snapshot(&job);
            continue;
        }

        {
            char dbg[192];
            sprintf_s(dbg, sizeof(dbg),
                      "[WORKER] Processing job cx=%d cz=%d gen=%d space=%s type=%s\n",
                      job.cx,
                      job.cz,
                      job.generation,
                      sdk_coordinate_space_type_name((SdkCoordinateSpaceType)job.space_type),
                      job.type == SDK_CHUNK_JOB_REMESH ? "REMESH" : "GENERATE");
            sdk_debug_log_output(dbg);
        }

        set_worker_debug_state(worker, 1, job.cx, job.cz, job.type, SDK_STREAM_WORKER_STAGE_LOADING);

        {
            SdkChunk* built = (SdkChunk*)malloc(sizeof(SdkChunk));
            SdkChunkBuildResult result;

            memset(&result, 0, sizeof(result));
            result.type = (job.type == SDK_CHUNK_JOB_REMESH)
                ? SDK_CHUNK_STREAM_RESULT_REMESHED
                : SDK_CHUNK_STREAM_RESULT_GENERATED;
            result.gx = -1;
            result.gz = -1;
            result.cx = job.cx;
            result.cz = job.cz;
            result.space_type = job.space_type;
            result.generation = job.generation;
            result.dirty_mask = job.dirty_mask;
            result.loaded_from_persistence = 0u;
            result.role = job.role;

            if (job.type == SDK_CHUNK_JOB_REMESH) {
                {
                    char dbg[128];
                    sprintf_s(dbg, sizeof(dbg), "[WORKER] Remeshing cx=%d cz=%d\n", job.cx, job.cz);
                    sdk_debug_log_output(dbg);
                }
                set_worker_debug_state(worker, 1, job.cx, job.cz, job.type, SDK_STREAM_WORKER_STAGE_MESHING);
                built = mesh_remesh_snapshot_job(&job, &worker->mesh_buf, &worker->temp_cm);
                result.built_chunk = built;
                {
                    char dbg[128];
                    sprintf_s(dbg, sizeof(dbg), "[WORKER] Remesh complete cx=%d cz=%d built=%p\n", job.cx, job.cz, (void*)result.built_chunk);
                    sdk_debug_log_output(dbg);
                }
                free_job_snapshot(&job);
            } else if (built) {
                memset(built, 0, sizeof(*built));
                sdk_chunk_init_with_space(built,
                                          job.cx,
                                          job.cz,
                                          (SdkCoordinateSpaceType)job.space_type,
                                          impl->construction_registry);
                if (built->blocks) {
                    if (!impl->persistence ||
                        !sdk_persistence_load_chunk(impl->persistence, job.cx, job.cz, built)) {
                        set_worker_debug_state(worker, 1, job.cx, job.cz, job.type, SDK_STREAM_WORKER_STAGE_WORLDGEN);
                        sdk_worldgen_generate_chunk_ctx(&worker->worldgen, built);
                        result.loaded_from_persistence = 0u;
                        log_expected_wall_chunk_debug(built, 0); /* Log after generation */
                    } else {
                        result.loaded_from_persistence = 1u;
                        log_expected_wall_chunk_debug(built, 1); /* Log after load */
                    }
                    {
                        char dbg[128];
                        sprintf_s(dbg, sizeof(dbg), "[WORKER] Meshing cx=%d cz=%d (persisted=%d)\n", job.cx, job.cz, result.loaded_from_persistence);
                        sdk_debug_log_output(dbg);
                    }
                    set_worker_debug_state(worker, 1, job.cx, job.cz, job.type, SDK_STREAM_WORKER_STAGE_MESHING);
                    sdk_mesh_set_thread_worldgen_debug_enabled(0);
                    result.built_chunk = mesh_snapshot_job(&job, &built, &worker->mesh_buf, &worker->temp_cm);
                    sdk_mesh_set_thread_worldgen_debug_enabled(1);
                    {
                        char dbg[128];
                        sprintf_s(dbg, sizeof(dbg), "[WORKER] Mesh complete cx=%d cz=%d built=%p\n", job.cx, job.cz, (void*)result.built_chunk);
                        sdk_debug_log_output(dbg);
                    }
                    if (!result.built_chunk) {
                        release_built_chunk(built);
                    }
                    free_job_snapshot(&job);
                } else {
                    release_built_chunk(built);
                    free_job_snapshot(&job);
                }
            }

            {
                char dbg[192];
                sprintf_s(dbg, sizeof(dbg),
                          "[WORKER] Queuing result cx=%d cz=%d gen=%d space=%s built=%p results=%d/%d\n",
                          job.cx,
                          job.cz,
                          job.generation,
                          sdk_coordinate_space_type_name((SdkCoordinateSpaceType)result.space_type),
                          (void*)result.built_chunk,
                          impl->result_count,
                          SDK_CHUNK_STREAMER_RESULT_CAPACITY);
                sdk_debug_log_output(dbg);
            }
            set_worker_debug_state(worker, 1, job.cx, job.cz, job.type, SDK_STREAM_WORKER_STAGE_QUEUING_RESULT);
            EnterCriticalSection(&impl->lock);
            if (impl->result_count < SDK_CHUNK_STREAMER_RESULT_CAPACITY) {
                int out_index = ring_index(impl->result_head, impl->result_count, SDK_CHUNK_STREAMER_RESULT_CAPACITY);
                impl->results[out_index] = result;
                impl->result_count++;
            } else {
                if (job.type == SDK_CHUNK_JOB_GENERATE) {
                    clear_generate_track(impl, job.cx, job.cz, job.generation);
                } else {
                    queue_dropped_remesh_repair(impl, job.cx, job.cz, job.generation);
                }
                release_built_chunk(result.built_chunk);
            }
            LeaveCriticalSection(&impl->lock);
            set_worker_debug_state(worker, 0, 0, 0, SDK_CHUNK_JOB_GENERATE, SDK_STREAM_WORKER_STAGE_IDLE);
        }
    }

    return 0;
}

void sdk_chunk_streamer_init(SdkChunkStreamer* streamer, const SdkWorldDesc* world_desc, SdkPersistence* persistence)
{
    SdkChunkStreamerInternal* impl;
    int worker_count;
    int i;

    if (!streamer) return;
    if (streamer->impl) {
        sdk_chunk_streamer_shutdown(streamer);
    }

    impl = (SdkChunkStreamerInternal*)calloc(1, sizeof(SdkChunkStreamerInternal));
    if (!impl) return;

    if (world_desc) impl->world_desc = *world_desc;
    impl->persistence = persistence;
    InitializeCriticalSection(&impl->lock);
    InitializeConditionVariable(&impl->jobs_cv);
    impl->running = true;

    worker_count = choose_worker_count();
    impl->worker_count = worker_count;

    /* Debug: log worker count */
    char buf[256];
    sprintf_s(buf, sizeof(buf), "[STREAMER] Initializing with %d worker threads", worker_count);
    sdk_debug_log_output(buf);
    sdk_debug_log_output("\n");

    for (i = 0; i < worker_count; ++i) {
        SdkChunkStreamWorker* worker = &impl->workers[i];
        worker->owner = impl;
        sdk_worldgen_init_ex(&worker->worldgen, &impl->world_desc, SDK_WORLDGEN_CACHE_DISK);
        if (impl->persistence) {
            const char* save_path = sdk_persistence_get_save_path(impl->persistence);
            if (save_path && save_path[0] != '\0') {
                sdk_settlement_set_world_path(&worker->worldgen, save_path);
            }
        }
        sdk_mesh_buffer_init(&worker->mesh_buf, 65536);
        sdk_chunk_manager_init(&worker->temp_cm);
        worker->thread = CreateThread(NULL, 0, chunk_stream_worker_proc, worker, 0, NULL);
    }

    streamer->impl = impl;
}

void sdk_chunk_streamer_begin_shutdown(SdkChunkStreamer* streamer)
{
    SdkChunkStreamerInternal* impl;
    SdkChunkBuildJob pending_job;

    if (!streamer || !streamer->impl) return;
    impl = (SdkChunkStreamerInternal*)streamer->impl;

    EnterCriticalSection(&impl->lock);
    if (!impl->running) {
        LeaveCriticalSection(&impl->lock);
        return;
    }
    impl->running = false;
    impl->shutdown_started_at = GetTickCount64();
    while (impl->job_count > 0) {
        pending_job = impl->jobs[impl->job_head];
        memset(&impl->jobs[impl->job_head], 0, sizeof(impl->jobs[impl->job_head]));
        impl->job_head = (impl->job_head + 1) % SDK_CHUNK_STREAMER_JOB_CAPACITY;
        impl->job_count--;
        if (pending_job.type == SDK_CHUNK_JOB_GENERATE) {
            clear_generate_track(impl, pending_job.cx, pending_job.cz, pending_job.generation);
        }
        free_job_snapshot(&pending_job);
    }
    WakeAllConditionVariable(&impl->jobs_cv);
    LeaveCriticalSection(&impl->lock);
}

int sdk_chunk_streamer_poll_shutdown(SdkChunkStreamer* streamer)
{
    SdkChunkStreamerInternal* impl;
    ULONGLONG shutdown_elapsed = 0u;
    int i;

    if (!streamer || !streamer->impl) return 1;
    impl = (SdkChunkStreamerInternal*)streamer->impl;
    if (impl->shutdown_started_at != 0u) {
        shutdown_elapsed = GetTickCount64() - impl->shutdown_started_at;
    }

    for (i = 0; i < impl->worker_count; ++i) {
        SdkChunkStreamWorker* worker = &impl->workers[i];
        if (worker->thread) {
            DWORD wait_result = WaitForSingleObject(worker->thread, 0u);
            if (wait_result == WAIT_TIMEOUT) {
                while (discard_one_result_for_shutdown(impl)) {
                }
                if (shutdown_elapsed >= 2000u) {
                    static ULONGLONG s_last_shutdown_log_ms = 0u;
                    ULONGLONG now = GetTickCount64();
                    if (now - s_last_shutdown_log_ms >= 1000u) {
                        char dbg[160];
                        int active_workers;
                        EnterCriticalSection(&impl->lock);
                        active_workers = count_active_workers_locked(impl);
                        LeaveCriticalSection(&impl->lock);
                        s_last_shutdown_log_ms = now;
                        sprintf_s(dbg, sizeof(dbg),
                                  "[STREAM] waiting for worker shutdown elapsed=%llums active_workers=%d pending_jobs=%d pending_results=%d\n",
                                  shutdown_elapsed, active_workers, impl->job_count, impl->result_count);
                        sdk_debug_log_output(dbg);
                    }
                }
                return 0;
            }
            CloseHandle(worker->thread);
            worker->thread = NULL;
        }
        if (worker->owner) {
            sdk_worldgen_shutdown(&worker->worldgen);
            sdk_mesh_buffer_free(&worker->mesh_buf);
            sdk_chunk_manager_shutdown(&worker->temp_cm);
            worker->owner = NULL;
        }
    }

    while (discard_one_result_for_shutdown(impl)) {
    }

    DeleteCriticalSection(&impl->lock);
    free(impl);
    streamer->impl = NULL;
    return 1;
}

void sdk_chunk_streamer_shutdown(SdkChunkStreamer* streamer)
{
    sdk_chunk_streamer_begin_shutdown(streamer);
    while (!sdk_chunk_streamer_poll_shutdown(streamer)) {
        Sleep(0);
    }
}

typedef struct {
    int cx;
    int cz;
    int distance_sq;
    int target_index;
} DistanceSortedTarget;

static int compare_targets_by_distance(const void* a, const void* b)
{
    const DistanceSortedTarget* ta = (const DistanceSortedTarget*)a;
    const DistanceSortedTarget* tb = (const DistanceSortedTarget*)b;
    return ta->distance_sq - tb->distance_sq;
}

static int startup_target_tier(const SdkChunkResidencyTarget* target,
                               int cam_cx,
                               int cam_cz,
                               int safety_radius)
{
    int dx;
    int dz;
    int chebyshev;

    if (!target) return 0;

    dx = target->cx - cam_cx;
    dz = target->cz - cam_cz;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    chebyshev = (dx > dz) ? dx : dz;

    if ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_PRIMARY) {
        if (chebyshev <= safety_radius) return 1;
        if (chebyshev == safety_radius + 1) return 2;
        return 0;
    }

    if ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_WALL_SUPPORT &&
        chebyshev <= safety_radius + 1) {
        return 2;
    }

    return 0;
}

static void sdk_chunk_streamer_schedule_visible_internal(SdkChunkStreamer* streamer,
                                                         const SdkChunkManager* cm,
                                                         int include_wall_support,
                                                         int include_background_targets,
                                                         int max_pending_jobs)
{
    SdkChunkStreamerInternal* impl;
    static const int neighbor_dx[SDK_CHUNK_NEIGHBOR_COUNT] = { -1, 1, 0, 0, -1, 1, -1, 1 };
    static const int neighbor_dz[SDK_CHUNK_NEIGHBOR_COUNT] = { 0, 0, -1, 1, -1, -1, 1, 1 };
    static ULONGLONG s_last_schedule_log_ms = 0u;
    int pass;
    int i;
    DistanceSortedTarget sorted_targets[SDK_CHUNK_MANAGER_MAX_DESIRED];
    int sorted_count;
    int anchor_cx;
    int anchor_cz;

    if (!streamer || !streamer->impl || !cm) return;
    impl = (SdkChunkStreamerInternal*)streamer->impl;
    impl->construction_registry = cm->construction_registry;
    if (max_pending_jobs <= 0) {
        max_pending_jobs = SDK_CHUNK_STREAMER_JOB_CAPACITY;
    }

    anchor_cx = cm->primary_anchor_cx;
    anchor_cz = cm->primary_anchor_cz;

    EnterCriticalSection(&impl->lock);
    prune_idle_tracks(impl, cm);
    prune_queued_generate_jobs(impl, cm);

    for (pass = 0; pass < 3; ++pass) {
        sorted_count = 0;
        for (i = 0; i < sdk_chunk_manager_desired_count(cm); ++i) {
            const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(cm, i);
            int is_wall_support;
            int dx;
            int dz;

            if (!target) continue;
            is_wall_support = target_supports_active_superchunk_wall(cm, target);
            if (pass == 0 &&
                (SdkChunkResidencyRole)target->role != SDK_CHUNK_ROLE_PRIMARY) {
                continue;
            }
            if (pass == 1 && (!include_wall_support || !is_wall_support)) continue;
            if (pass == 2 && !include_background_targets) continue;
            if (pass == 2 &&
                ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_PRIMARY || is_wall_support)) {
                continue;
            }

            dx = target->cx - anchor_cx;
            dz = target->cz - anchor_cz;
            sorted_targets[sorted_count].cx = target->cx;
            sorted_targets[sorted_count].cz = target->cz;
            sorted_targets[sorted_count].distance_sq = dx * dx + dz * dz;
            sorted_targets[sorted_count].target_index = i;
            sorted_count++;
        }

        qsort(sorted_targets, (size_t)sorted_count, sizeof(DistanceSortedTarget), compare_targets_by_distance);

        for (i = 0; i < sorted_count; ++i) {
            const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(cm, sorted_targets[i].target_index);
            const SdkChunk* resident;
            SdkChunkStreamTrack* track;
            int is_wall_support;

            if (!target) continue;
            is_wall_support = target_supports_active_superchunk_wall(cm, target);
            if (pass == 0 &&
                (SdkChunkResidencyRole)target->role != SDK_CHUNK_ROLE_PRIMARY) {
                continue;
            }
            if (pass == 1 && (!include_wall_support || !is_wall_support)) continue;
            if (pass == 2 && !include_background_targets) continue;
            if (pass == 2 &&
                ((SdkChunkResidencyRole)target->role == SDK_CHUNK_ROLE_PRIMARY || is_wall_support)) {
                continue;
            }

            resident = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm, target->cx, target->cz);
            if (resident && resident->blocks) continue;

            track = find_or_alloc_track(impl, target->cx, target->cz);
            if (!track) {
                ULONGLONG now = GetTickCount64();
                if (now - s_last_schedule_log_ms >= 1000u) {
                    char dbg[256];
                    s_last_schedule_log_ms = now;
                    sprintf_s(dbg, sizeof(dbg),
                              "[STREAM] no track for (%d,%d) desired=%d jobs=%d results=%d\n",
                              target->cx, target->cz,
                              sdk_chunk_manager_desired_count(cm), impl->job_count, impl->result_count);
                    sdk_debug_log_output(dbg);
                }
                break;
            }
            if (track->generation == target->generation &&
                track->state != SDK_STREAM_TRACK_IDLE) {
                continue;
            }

            if (impl->job_count >= SDK_CHUNK_STREAMER_JOB_CAPACITY ||
                impl->job_count >= max_pending_jobs) {
                ULONGLONG now = GetTickCount64();
                if (now - s_last_schedule_log_ms >= 1000u) {
                    char dbg[256];
                    s_last_schedule_log_ms = now;
                    sprintf_s(dbg, sizeof(dbg),
                              "[STREAM] job queue capped at (%d,%d) desired=%d jobs=%d results=%d max=%d\n",
                              target->cx, target->cz,
                              sdk_chunk_manager_desired_count(cm), impl->job_count, impl->result_count,
                              max_pending_jobs);
                    sdk_debug_log_output(dbg);
                }
                LeaveCriticalSection(&impl->lock);
                return;
            }

            {
                int out_index = ring_index(impl->job_head, impl->job_count, SDK_CHUNK_STREAMER_JOB_CAPACITY);
                SdkChunkBuildJob* out_job = &impl->jobs[out_index];
                memset(out_job, 0, sizeof(*out_job));
                out_job->type = SDK_CHUNK_JOB_GENERATE;
                out_job->gx = -1;
                out_job->gz = -1;
                out_job->cx = target->cx;
                out_job->cz = target->cz;
                out_job->space_type = target->space_type;
                out_job->role = target->role;
                out_job->generation = target->generation;
                for (int n = 0; n < SDK_CHUNK_NEIGHBOR_COUNT; ++n) {
                    const SdkChunk* neighbor = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm,
                        target->cx + neighbor_dx[n], target->cz + neighbor_dz[n]);
                    if (!neighbor || !neighbor->blocks) continue;
                    out_job->neighbor_chunks[n] = clone_chunk_snapshot(neighbor, 0u);
                }
                impl->job_count++;

                track->cx = target->cx;
                track->cz = target->cz;
                track->space_type = target->space_type;
                track->role = target->role;
                track->generation = target->generation;
                track->state = SDK_STREAM_TRACK_QUEUED;
                WakeConditionVariable(&impl->jobs_cv);
            }
        }
    }

    LeaveCriticalSection(&impl->lock);
}

void sdk_chunk_streamer_schedule_visible(SdkChunkStreamer* streamer, const SdkChunkManager* cm)
{
    sdk_chunk_streamer_schedule_visible_internal(streamer, cm, 1, 1, 0);
}

void sdk_chunk_streamer_schedule_visible_no_wall_support(SdkChunkStreamer* streamer, const SdkChunkManager* cm)
{
    sdk_chunk_streamer_schedule_visible_internal(streamer, cm, 0, 1, 0);
}

void sdk_chunk_streamer_schedule_phase(SdkChunkStreamer* streamer,
                                       const SdkChunkManager* cm,
                                       SdkChunkStreamSchedulePhase phase,
                                       int safety_radius,
                                       int max_pending_jobs)
{
    switch (phase) {
        case SDK_CHUNK_STREAM_SCHEDULE_BOOTSTRAP_SYNC:
            sdk_chunk_streamer_schedule_startup_priority(streamer, cm, safety_radius, max_pending_jobs);
            return;
        case SDK_CHUNK_STREAM_SCHEDULE_VISIBLE_ONLY:
            sdk_chunk_streamer_schedule_visible_internal(streamer, cm, 1, 0, max_pending_jobs);
            return;
        case SDK_CHUNK_STREAM_SCHEDULE_FULL_RUNTIME:
        default:
            sdk_chunk_streamer_schedule_visible_internal(streamer, cm, 1, 1, max_pending_jobs);
            return;
    }
}

void sdk_chunk_streamer_schedule_startup_priority(SdkChunkStreamer* streamer,
                                                  const SdkChunkManager* cm,
                                                  int safety_radius,
                                                  int max_pending_jobs)
{
    SdkChunkStreamerInternal* impl;
    static const int neighbor_dx[SDK_CHUNK_NEIGHBOR_COUNT] = { -1, 1, 0, 0, -1, 1, -1, 1 };
    static const int neighbor_dz[SDK_CHUNK_NEIGHBOR_COUNT] = { 0, 0, -1, 1, -1, -1, 1, 1 };
    DistanceSortedTarget sorted_targets[SDK_CHUNK_MANAGER_MAX_DESIRED];
    int tier;
    int sorted_count;
    int i;
    int jobs_scheduled = 0;

    if (!streamer || !streamer->impl || !cm) return;
    impl = (SdkChunkStreamerInternal*)streamer->impl;
    impl->construction_registry = cm->construction_registry;
    if (safety_radius < 1) safety_radius = 1;
    if (max_pending_jobs <= 0) {
        max_pending_jobs = impl->worker_count * 2;
        if (max_pending_jobs < 4) max_pending_jobs = 4;
    }

    EnterCriticalSection(&impl->lock);
    prune_idle_tracks(impl, cm);
    prune_queued_generate_jobs(impl, cm);

    for (tier = 1; tier <= 2; ++tier) {
        if (impl->job_count >= max_pending_jobs) {
            break;
        }
        sorted_count = 0;
        for (i = 0; i < sdk_chunk_manager_desired_count(cm); ++i) {
            const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(cm, i);
            int dx;
            int dz;

            if (!target) continue;
            if (startup_target_tier(target, cm->cam_cx, cm->cam_cz, safety_radius) != tier) continue;

            dx = target->cx - cm->cam_cx;
            dz = target->cz - cm->cam_cz;
            sorted_targets[sorted_count].cx = target->cx;
            sorted_targets[sorted_count].cz = target->cz;
            sorted_targets[sorted_count].distance_sq = dx * dx + dz * dz;
            sorted_targets[sorted_count].target_index = i;
            sorted_count++;
        }

        qsort(sorted_targets, (size_t)sorted_count, sizeof(DistanceSortedTarget), compare_targets_by_distance);

        for (i = 0; i < sorted_count; ++i) {
            const SdkChunkResidencyTarget* target =
                sdk_chunk_manager_desired_at(cm, sorted_targets[i].target_index);
            const SdkChunk* resident;
            SdkChunkStreamTrack* track;

            if (!target) continue;
            if (startup_target_tier(target, cm->cam_cx, cm->cam_cz, safety_radius) != tier) continue;

            resident = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm, target->cx, target->cz);
            if (resident && resident->blocks) continue;

            track = find_or_alloc_track(impl, target->cx, target->cz);
            if (!track) break;
            if (track->generation == target->generation &&
                track->state != SDK_STREAM_TRACK_IDLE) {
                continue;
            }
            if (impl->job_count >= SDK_CHUNK_STREAMER_JOB_CAPACITY ||
                impl->job_count >= max_pending_jobs) {
                LeaveCriticalSection(&impl->lock);
                return;
            }

            {
                int out_index = ring_index(impl->job_head, impl->job_count, SDK_CHUNK_STREAMER_JOB_CAPACITY);
                SdkChunkBuildJob* out_job = &impl->jobs[out_index];
                memset(out_job, 0, sizeof(*out_job));
                out_job->type = SDK_CHUNK_JOB_GENERATE;
                out_job->gx = -1;
                out_job->gz = -1;
                out_job->cx = target->cx;
                out_job->cz = target->cz;
                out_job->space_type = target->space_type;
                out_job->role = target->role;
                out_job->generation = target->generation;
                for (int n = 0; n < SDK_CHUNK_NEIGHBOR_COUNT; ++n) {
                    const SdkChunk* neighbor = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm,
                        target->cx + neighbor_dx[n], target->cz + neighbor_dz[n]);
                    if (!neighbor || !neighbor->blocks) continue;
                    out_job->neighbor_chunks[n] = clone_chunk_snapshot(neighbor, 0u);
                }
                impl->job_count++;

                track->cx = target->cx;
                track->cz = target->cz;
                track->space_type = target->space_type;
                track->role = target->role;
                track->generation = target->generation;
                track->state = SDK_STREAM_TRACK_QUEUED;
                WakeConditionVariable(&impl->jobs_cv);
            }
        }
    }

    LeaveCriticalSection(&impl->lock);
}

void sdk_chunk_streamer_schedule_wall_support(SdkChunkStreamer* streamer,
                                              const SdkChunkManager* cm,
                                              int max_pending_jobs)
{
    SdkChunkStreamerInternal* impl;
    static const int neighbor_dx[SDK_CHUNK_NEIGHBOR_COUNT] = { -1, 1, 0, 0, -1, 1, -1, 1 };
    static const int neighbor_dz[SDK_CHUNK_NEIGHBOR_COUNT] = { 0, 0, -1, 1, -1, -1, 1, 1 };
    int i;

    if (!streamer || !streamer->impl || !cm) return;
    impl = (SdkChunkStreamerInternal*)streamer->impl;
    impl->construction_registry = cm->construction_registry;
    if (max_pending_jobs <= 0) {
        max_pending_jobs = impl->worker_count * 2;
        if (max_pending_jobs < 4) max_pending_jobs = 4;
    }

    EnterCriticalSection(&impl->lock);
    prune_idle_tracks(impl, cm);
    prune_queued_generate_jobs(impl, cm);

    for (i = 0; i < sdk_chunk_manager_desired_count(cm); ++i) {
        const SdkChunkResidencyTarget* target = sdk_chunk_manager_desired_at(cm, i);
        const SdkChunk* resident;
        SdkChunkStreamTrack* track;

        if (!target) continue;
        if (!target_supports_active_superchunk_wall(cm, target)) continue;
        if (impl->job_count >= max_pending_jobs) break;

        resident = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm, target->cx, target->cz);
        if (resident && resident->blocks) continue;

        track = find_or_alloc_track(impl, target->cx, target->cz);
        if (!track) break;
        if (track->generation == target->generation &&
            track->state != SDK_STREAM_TRACK_IDLE) {
            continue;
        }

        {
            int out_index = ring_index(impl->job_head, impl->job_count, SDK_CHUNK_STREAMER_JOB_CAPACITY);
            SdkChunkBuildJob* out_job = &impl->jobs[out_index];
            memset(out_job, 0, sizeof(*out_job));
            out_job->type = SDK_CHUNK_JOB_GENERATE;
            out_job->gx = -1;
            out_job->gz = -1;
            out_job->cx = target->cx;
            out_job->cz = target->cz;
            out_job->space_type = target->space_type;
            out_job->role = target->role;
            out_job->generation = target->generation;
            for (int n = 0; n < SDK_CHUNK_NEIGHBOR_COUNT; ++n) {
                const SdkChunk* neighbor = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm,
                    target->cx + neighbor_dx[n], target->cz + neighbor_dz[n]);
                if (!neighbor || !neighbor->blocks) continue;
                out_job->neighbor_chunks[n] = clone_chunk_snapshot(neighbor, 0u);
            }
            impl->job_count++;

            track->cx = target->cx;
            track->cz = target->cz;
            track->space_type = target->space_type;
            track->role = target->role;
            track->generation = target->generation;
            track->state = SDK_STREAM_TRACK_QUEUED;
            WakeConditionVariable(&impl->jobs_cv);
        }
    }

    LeaveCriticalSection(&impl->lock);
}

int sdk_chunk_streamer_schedule_dirty(SdkChunkStreamer* streamer, const SdkChunkManager* cm, int max_jobs)
{
    static const int neighbor_dx[SDK_CHUNK_NEIGHBOR_COUNT] = { -1, 1, 0, 0, -1, 1, -1, 1 };
    static const int neighbor_dz[SDK_CHUNK_NEIGHBOR_COUNT] = { 0, 0, -1, 1, -1, -1, 1, 1 };
    SdkChunkStreamerInternal* impl;
    int scheduled = 0;
    int i;

    if (!streamer || !streamer->impl || !cm || max_jobs <= 0) return 0;
    impl = (SdkChunkStreamerInternal*)streamer->impl;
    impl->construction_registry = cm->construction_registry;

    EnterCriticalSection(&impl->lock);

    for (i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT && scheduled < max_jobs; ++i) {
        SdkChunkBuildJob* out_job;
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at((SdkChunkManager*)cm, i);
        SdkChunk* chunk;
        int n;

        if (!slot || !slot->occupied) continue;
        chunk = &slot->chunk;
        if (!chunk->blocks || !sdk_chunk_needs_remesh(chunk) || chunk->remesh_queued) continue;
        if (impl->job_count >= SDK_CHUNK_STREAMER_JOB_CAPACITY) {
            LeaveCriticalSection(&impl->lock);
            return scheduled;
        }

        out_job = &impl->jobs[ring_index(impl->job_head, impl->job_count, SDK_CHUNK_STREAMER_JOB_CAPACITY)];
        memset(out_job, 0, sizeof(*out_job));
        out_job->type = SDK_CHUNK_JOB_REMESH;
        out_job->gx = -1;
        out_job->gz = -1;
        out_job->cx = chunk->cx;
        out_job->cz = chunk->cz;
        out_job->space_type = chunk->space_type;
        out_job->role = slot->role;
        out_job->generation = chunk->mesh_job_generation;
        out_job->dirty_mask = chunk->dirty_subchunks_mask;
        out_job->snapshot_chunk = clone_chunk_snapshot(chunk, out_job->dirty_mask);
        if (!out_job->snapshot_chunk) {
            continue;
        }

        for (n = 0; n < SDK_CHUNK_NEIGHBOR_COUNT; ++n) {
            const SdkChunk* neighbor = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm,
                chunk->cx + neighbor_dx[n], chunk->cz + neighbor_dz[n]);
            if (!neighbor || !neighbor->blocks) continue;
            out_job->neighbor_chunks[n] = clone_chunk_snapshot(neighbor, 0u);
        }

        impl->job_count++;
        chunk->remesh_queued = true;
        chunk->inflight_mesh_generation = out_job->generation;
        scheduled++;
        WakeConditionVariable(&impl->jobs_cv);
    }

    LeaveCriticalSection(&impl->lock);
    return scheduled;
}

int sdk_chunk_streamer_pop_result(SdkChunkStreamer* streamer, SdkChunkBuildResult* out_result)
{
    SdkChunkStreamerInternal* impl;

    if (!streamer || !streamer->impl || !out_result) return 0;
    impl = (SdkChunkStreamerInternal*)streamer->impl;

    EnterCriticalSection(&impl->lock);
    if (impl->result_count <= 0) {
        LeaveCriticalSection(&impl->lock);
        return 0;
    }

    *out_result = impl->results[impl->result_head];
    impl->result_head = (impl->result_head + 1) % SDK_CHUNK_STREAMER_RESULT_CAPACITY;
    impl->result_count--;

    if (out_result->type == SDK_CHUNK_STREAM_RESULT_GENERATED) {
        clear_generate_track(impl, out_result->cx, out_result->cz, out_result->generation);
    }

    LeaveCriticalSection(&impl->lock);
    return 1;
}

int sdk_chunk_streamer_pop_dropped_remesh(SdkChunkStreamer* streamer, int* out_cx, int* out_cz, uint32_t* out_generation)
{
    SdkChunkStreamerInternal* impl;
    SdkDroppedRemeshRepair repair;

    if (!streamer || !streamer->impl || !out_cx || !out_cz || !out_generation) return 0;
    impl = (SdkChunkStreamerInternal*)streamer->impl;

    EnterCriticalSection(&impl->lock);
    if (impl->dropped_remesh_count <= 0) {
        LeaveCriticalSection(&impl->lock);
        return 0;
    }

    repair = impl->dropped_remeshes[impl->dropped_remesh_head];
    impl->dropped_remesh_head =
        (impl->dropped_remesh_head + 1) % SDK_CHUNK_STREAMER_RESULT_CAPACITY;
    impl->dropped_remesh_count--;
    LeaveCriticalSection(&impl->lock);

    *out_cx = repair.cx;
    *out_cz = repair.cz;
    *out_generation = repair.generation;
    return 1;
}

void sdk_chunk_streamer_release_result(SdkChunkBuildResult* result)
{
    if (!result) return;
    release_built_chunk(result->built_chunk);
    memset(result, 0, sizeof(*result));
}

int sdk_chunk_streamer_pending_jobs(const SdkChunkStreamer* streamer)
{
    SdkChunkStreamerInternal* impl;
    int count = 0;

    if (!streamer || !streamer->impl) return 0;
    impl = (SdkChunkStreamerInternal*)streamer->impl;
    EnterCriticalSection(&impl->lock);
    count = impl->job_count;
    LeaveCriticalSection(&impl->lock);
    return count;
}

int sdk_chunk_streamer_pending_results(const SdkChunkStreamer* streamer)
{
    SdkChunkStreamerInternal* impl;
    int count = 0;

    if (!streamer || !streamer->impl) return 0;
    impl = (SdkChunkStreamerInternal*)streamer->impl;
    EnterCriticalSection(&impl->lock);
    count = impl->result_count;
    LeaveCriticalSection(&impl->lock);
    return count;
}

int sdk_chunk_streamer_active_workers(const SdkChunkStreamer* streamer)
{
    SdkChunkStreamerInternal* impl;
    int count = 0;

    if (!streamer || !streamer->impl) return 0;
    impl = (SdkChunkStreamerInternal*)streamer->impl;
    EnterCriticalSection(&impl->lock);
    count = count_active_workers_locked(impl);
    LeaveCriticalSection(&impl->lock);
    return count;
}

void sdk_chunk_streamer_debug_inflight_summary(const SdkChunkStreamer* streamer, char* out_text, size_t out_text_size)
{
    SdkChunkStreamerInternal* impl;
    size_t used = 0;
    int i;

    if (!out_text || out_text_size == 0) return;
    out_text[0] = '\0';
    if (!streamer || !streamer->impl) return;
    impl = (SdkChunkStreamerInternal*)streamer->impl;

    EnterCriticalSection(&impl->lock);
    for (i = 0; i < impl->worker_count; ++i) {
        const SdkChunkStreamWorker* worker = &impl->workers[i];
        int written;
        ULONGLONG elapsed = 0u;

        if (!worker->debug_active) continue;
        if (worker->debug_stage_started_at != 0u) {
            elapsed = GetTickCount64() - worker->debug_stage_started_at;
        }

        written = snprintf(out_text + used,
                           out_text_size - used,
                           "%s#%d %s (%d,%d) %s %llums",
                           used > 0 ? " | " : "",
                           i,
                           worker_job_type_label(worker->debug_job_type),
                           worker->debug_cx,
                           worker->debug_cz,
                           worker_stage_label(worker->debug_stage),
                           elapsed);
        if (written <= 0) break;
        if ((size_t)written >= (out_text_size - used)) {
            used = out_text_size - 1;
            break;
        }
        used += (size_t)written;
    }
    LeaveCriticalSection(&impl->lock);
}
