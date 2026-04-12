/**
 * sdk_chunk_manager.h -- Superchunk-aware resident chunk manager.
 *
 * Maintains the authoritative desired and resident chunk sets for the runtime.
 *
 * Key responsibilities:
 * - choose between moving-window mode and full-superchunk mode
 * - assign residency roles for primary and frontier chunks
 * - own the slot lookup used by bootstrap, streaming, simulation, and rendering
 *
 * Contract with surrounding systems:
 * - startup and topology bootstrap use the manager's desired set for sync loads
 * - the async streamer schedules generate jobs only for desired coordinates
 * - the renderer consumes resident slots but does not define desired residency
 *
 * `grid_size` is a legacy graphics-facing setting. The manager normalizes it to
 * preset radii and derives the actual residency mode from that normalized value.
 */
#ifndef NQLSDK_CHUNK_MANAGER_H
#define NQLSDK_CHUNK_MANAGER_H

#include "../sdk_chunk.h"
#include "../../Superchunks/Geometry/sdk_superchunk_geometry.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHUNK_GRID_MIN_SIZE 1
#define CHUNK_GRID_DEFAULT_SIZE 17
#define CHUNK_GRID_MAX_SIZE 33
#define CHUNK_GRID_MAX_COUNT (CHUNK_GRID_MAX_SIZE * CHUNK_GRID_MAX_SIZE)

#define SDK_GATE_FRONTIER_DEPTH_CHUNKS SDK_SUPERCHUNK_WALL_THICKNESS_CHUNKS
#define SDK_GATE_FRONTIER_WIDTH_CHUNKS SDK_SUPERCHUNK_GATE_SUPPORT_WIDTH_CHUNKS
#define SDK_CHUNK_MANAGER_MAX_RESIDENT 768
#define SDK_CHUNK_MANAGER_MAX_DESIRED 768
#define SDK_CHUNK_MANAGER_HASH_CAPACITY 4096

typedef enum {
    SDK_CHUNK_ROLE_NONE = 0,
    SDK_CHUNK_ROLE_PRIMARY,
    SDK_CHUNK_ROLE_WALL_SUPPORT,
    SDK_CHUNK_ROLE_FRONTIER,
    SDK_CHUNK_ROLE_TRANSITION_PRELOAD,
    SDK_CHUNK_ROLE_EVICT_PENDING
} SdkChunkResidencyRole;

typedef enum {
    SDK_CHUNK_RENDER_REPRESENTATION_FULL = 0,
    SDK_CHUNK_RENDER_REPRESENTATION_FAR,
    SDK_CHUNK_RENDER_REPRESENTATION_PROXY
} SdkChunkRenderRepresentation;

typedef enum {
    SDK_SUPERCHUNK_RING_NONE = -1,
    SDK_SUPERCHUNK_RING_WEST = 0,
    SDK_SUPERCHUNK_RING_NORTH = 1,
    SDK_SUPERCHUNK_RING_EAST = 2,
    SDK_SUPERCHUNK_RING_SOUTH = 3
} SdkSuperChunkRingSide;

typedef enum {
    SDK_ACTIVE_WALL_STAGE_NONE = 0,
    SDK_ACTIVE_WALL_STAGE_SUPPORT,
    SDK_ACTIVE_WALL_STAGE_EDGE,
    SDK_ACTIVE_WALL_STAGE_CORNER
} SdkActiveWallStage;

typedef struct {
    int scx;
    int scz;
} SdkSuperChunkCoord;

typedef struct {
    int      cx;
    int      cz;
    uint8_t  space_type;
    uint8_t  role;
    uint16_t reserved1;
    uint32_t generation;
} SdkChunkResidencyTarget;

typedef struct {
    SdkChunk chunk;
    uint8_t  occupied;
    uint8_t  desired;
    uint8_t  role;
    uint8_t  desired_role;
    uint8_t  render_representation;
    uint8_t  render_far_mesh_kind;
    uint16_t render_reserved1;
} SdkChunkResidentSlot;

typedef struct {
    SdkChunkResidentSlot   slots[SDK_CHUNK_MANAGER_MAX_RESIDENT];
    int32_t                lookup[SDK_CHUNK_MANAGER_HASH_CAPACITY];
    SdkConstructionArchetypeRegistry* construction_registry;
    int                    cam_cx;
    int                    cam_cz;
    int                    grid_size; /* Legacy graphics setting; residency is superchunk-driven. */
    uint32_t               next_slot;
    uint32_t               topology_generation;
    uint16_t               resident_count;
    uint8_t                primary_valid;
    uint8_t                transition_active;
    uint8_t                primary_expanded;
    uint8_t                background_expansion_enabled;
    uint8_t                topology_dirty;
    int                    primary_scx;
    int                    primary_scz;
    int                    prev_scx;          /* Previous superchunk for gate persistence */
    int                    prev_scz;          /* Previous superchunk for gate persistence */
    int                    primary_load_radius;
    int                    primary_anchor_cx;
    int                    primary_anchor_cz;
    int                    desired_scx;
    int                    desired_scz;
    int                    transition_entry_cx;
    int                    transition_entry_cz;
    SdkChunkResidencyTarget desired[SDK_CHUNK_MANAGER_MAX_DESIRED];
    int                    desired_count;
    
    /* Row-based async superchunk loading state */
    int                    async_load_active;           /* 0 = inactive, 1 = loading rows */
    int                    async_load_axis;             /* 0 = X axis, 1 = Z axis */
    int                    async_load_direction;      /* +1 or -1 (which way player moved) */
    int                    async_load_current_row;    /* Current row being loaded (0-15) */
    int                    async_load_total_rows;       /* Total rows to load (16 for full superchunk) */
    int                    async_prev_scx;              /* Previous superchunk X */
    int                    async_prev_scz;              /* Previous superchunk Z */
    int                    async_new_scx;               /* New superchunk X */
    int                    async_new_scz;               /* New superchunk Z */
    int                    async_budget_per_frame;      /* Rows to process per frame */
} SdkChunkManager;

void sdk_chunk_manager_init(SdkChunkManager* cm);
int sdk_chunk_manager_normalize_grid_size(int grid_size);
int sdk_chunk_manager_grid_size_from_radius(int radius);
int sdk_chunk_manager_radius_from_grid_size(int grid_size);
void sdk_chunk_manager_set_grid_size(SdkChunkManager* cm, int grid_size);
void sdk_chunk_manager_set_background_expansion(SdkChunkManager* cm, bool enabled);
void sdk_chunk_manager_shutdown(SdkChunkManager* cm);
bool sdk_chunk_manager_update(SdkChunkManager* cm, int new_cx, int new_cz);
SdkChunk* sdk_chunk_manager_get_chunk(SdkChunkManager* cm, int cx, int cz);

SdkChunkResidentSlot* sdk_chunk_manager_find_slot(SdkChunkManager* cm, int cx, int cz);
SdkChunkResidentSlot* sdk_chunk_manager_reserve_slot(SdkChunkManager* cm, int cx, int cz, SdkChunkResidencyRole role);
SdkChunk* sdk_chunk_manager_adopt_built_chunk(SdkChunkManager* cm, SdkChunk* built_chunk, SdkChunkResidencyRole role);
void sdk_chunk_manager_release_slot(SdkChunkManager* cm, SdkChunkResidentSlot* slot);
void sdk_chunk_manager_rebuild_lookup(SdkChunkManager* cm);

static inline int sdk_chunk_manager_grid_size(const SdkChunkManager* cm) {
    /* Returns the normalized grid size for the chunk manager.
     * The grid_size is normalized to ensure it falls within valid bounds
     * (CHUNK_GRID_MIN_SIZE to CHUNK_GRID_MAX_SIZE).
     * If cm is NULL, returns the default grid size (17x17). */
    return sdk_chunk_manager_normalize_grid_size(cm ? cm->grid_size : CHUNK_GRID_DEFAULT_SIZE);
}

static inline int sdk_chunk_manager_active_count(const SdkChunkManager* cm) {
    /* Returns the number of currently resident (active) chunks.
     * This counts chunks that have been loaded and have block data.
     * Returns 0 if the chunk manager is not initialized. */
    return cm ? (int)cm->resident_count : 0;
}

static inline int sdk_chunk_manager_slot_capacity(void) {
    /* Returns the maximum number of chunk slots available.
     * This is a compile-time constant (768) representing the
     * hard limit on how many chunks can be resident at once. */
    return SDK_CHUNK_MANAGER_MAX_RESIDENT;
}

static inline SdkChunkResidentSlot* sdk_chunk_manager_get_slot_at(SdkChunkManager* cm, int index) {
    /* Retrieves a pointer to the chunk slot at the given index.
     * Performs bounds checking to ensure index is valid.
     * Returns NULL if chunk manager is not initialized or index is out of bounds.
     * The returned slot may or may not be occupied (check slot->occupied). */
    if (!cm || index < 0 || index >= SDK_CHUNK_MANAGER_MAX_RESIDENT) return NULL;
    return &cm->slots[index];
}

static inline const SdkChunkResidentSlot* sdk_chunk_manager_get_slot_at_const(const SdkChunkManager* cm, int index) {
    /* Const version of sdk_chunk_manager_get_slot_at.
     * Returns a read-only pointer to the chunk slot at the given index.
     * Useful when iterating slots without modifying them. */
    if (!cm || index < 0 || index >= SDK_CHUNK_MANAGER_MAX_RESIDENT) return NULL;
    return &cm->slots[index];
}

static inline int sdk_chunk_manager_desired_count(const SdkChunkManager* cm) {
    /* Returns the number of chunks currently marked as "desired".
     * Desired chunks are those that should be resident based on camera position
     * and render distance. This is the target set, not necessarily the resident set.
     * Returns 0 if chunk manager is not initialized. */
    return cm ? cm->desired_count : 0;
}

static inline const SdkChunkResidencyTarget* sdk_chunk_manager_desired_at(const SdkChunkManager* cm, int index) {
    /* Retrieves the desired chunk target at the given index.
     * The desired array contains all chunks that should be loaded based on
     * current camera position and superchunk configuration.
     * Each target includes chunk coordinates, role (PRIMARY, WALL_SUPPORT, etc.),
     * and generation number for tracking updates.
     * Returns NULL if index is out of bounds or chunk manager is not initialized. */
    if (!cm || index < 0 || index >= cm->desired_count) return NULL;
    return &cm->desired[index];
}

static inline SdkSuperChunkRingSide sdk_superchunk_outer_ring_side_for_chunk(int scx, int scz, int cx, int cz) {
    /* Determines which side of the superchunk's outer ring contains the given chunk.
     * Superchunks are 17x17 chunks with an outer ring of wall/support chunks.
     * This function checks if cx,cz lies on the west, north, east, or south wall,
     * within the interior bounds (not at corners).
     * Returns SDK_SUPERCHUNK_RING_NONE if the chunk is not on the outer ring.
     *
     * The check excludes corners by using interior_min/max bounds. */
    SdkSuperchunkCell cell;
    sdk_superchunk_cell_from_index(scx, scz, &cell);

    if (cx == cell.origin_cx &&
        cz >= cell.interior_min_cz &&
        cz <= cell.interior_max_cz) {
        return SDK_SUPERCHUNK_RING_WEST;
    }
    if (cz == cell.origin_cz &&
        cx >= cell.interior_min_cx &&
        cx <= cell.interior_max_cx) {
        return SDK_SUPERCHUNK_RING_NORTH;
    }
    if (cx == cell.east_cx &&
        cz >= cell.interior_min_cz &&
        cz <= cell.interior_max_cz) {
        return SDK_SUPERCHUNK_RING_EAST;
    }
    if (cz == cell.south_cz &&
        cx >= cell.interior_min_cx &&
        cx <= cell.interior_max_cx) {
        return SDK_SUPERCHUNK_RING_SOUTH;
    }
    return SDK_SUPERCHUNK_RING_NONE;
}

static inline int sdk_superchunk_outer_ring_contains_chunk(int scx, int scz, int cx, int cz) {
    /* Returns true (non-zero) if the chunk lies on the superchunk's outer ring.
     * The outer ring consists of the perimeter chunks surrounding the 15x15 interior.
     * This is a wrapper that checks if sdk_superchunk_outer_ring_side_for_chunk
     * returns any valid ring side (WEST, NORTH, EAST, or SOUTH). */
    return sdk_superchunk_outer_ring_side_for_chunk(scx, scz, cx, cz) != SDK_SUPERCHUNK_RING_NONE;
}

static inline int sdk_superchunk_full_neighborhood_contains_chunk(int scx, int scz, int cx, int cz) {
    /* Returns true if the chunk is within the 3x3 superchunk neighborhood centered on scx,scz.
     * The full neighborhood includes the primary superchunk and all 8 adjacent superchunks.
     * This provides a larger working set for streaming and wall support calculations. */
    return sdk_superchunk_chunk_in_full_neighborhood(scx, scz, cx, cz);
}

static inline int sdk_superchunk_full_neighborhood_ring_contains_chunk(int scx, int scz, int cx, int cz) {
    /* Returns true if the chunk is in the full neighborhood but NOT in the interior.
     * This identifies chunks on the perimeter of the 3x3 superchunk neighborhood,
     * which are typically loaded as WALL_SUPPORT or FRONTIER role chunks.
     * These chunks are needed for proper meshing at superchunk boundaries. */
    if (!sdk_superchunk_full_neighborhood_contains_chunk(scx, scz, cx, cz)) return 0;
    return !sdk_superchunk_chunk_in_interior(scx, scz, cx, cz);
}

static inline int sdk_superchunk_active_wall_corner_contains_chunk(int scx, int scz, int cx, int cz) {
    /* Returns true if the chunk is at a corner of the active wall.
     * Active wall corners are the 4 corner chunks of the primary superchunk
     * that are currently being used for wall mesh generation.
     * These chunks require special handling for wall finalization. */
    return sdk_superchunk_chunk_is_active_wall_corner(scx, scz, cx, cz);
}

static inline int sdk_superchunk_active_wall_edge_contains_chunk(int scx, int scz, int cx, int cz) {
    /* Returns true if the chunk is on an edge (but not corner) of the active wall.
     * Active wall edges are the non-corner perimeter chunks used for wall mesh.
     * These are processed after corners during wall finalization. */
    return sdk_superchunk_chunk_is_active_wall_edge(scx, scz, cx, cz);
}

static inline int sdk_superchunk_active_wall_chunk_contains_chunk(int scx, int scz, int cx, int cz) {
    /* Returns true if the chunk is part of the active wall (any wall chunk).
     * The active wall consists of all perimeter chunks of the primary superchunk
     * that are currently being used for far-distance rendering optimization.
     * This includes both wall support chunks and the main wall chunks. */
    return sdk_superchunk_chunk_is_active_wall(scx, scz, cx, cz);
}

static inline int sdk_superchunk_active_wall_support_contains_chunk(int scx, int scz, int cx, int cz) {
    /* Returns true if the chunk is a wall support chunk for the active wall.
     * Wall support chunks are loaded in the superchunks adjacent to the primary
     * superchunk to provide neighbor data for wall mesh generation.
     * These have SDK_CHUNK_ROLE_WALL_SUPPORT and are within safety_radius+1. */
    return sdk_superchunk_chunk_is_active_wall_support(scx, scz, cx, cz);
}

static inline SdkActiveWallStage sdk_superchunk_active_wall_stage_for_chunk(int scx, int scz, int cx, int cz) {
    /* Determines the active wall processing stage for a chunk.
     * Wall chunks are processed in stages during bootstrap: corners first,
     * then edges, then support chunks. This helps prioritize which chunks
     * need to be meshed first for proper wall rendering.
     * Returns SDK_ACTIVE_WALL_STAGE_NONE if the chunk is not part of the active wall. */
    if (sdk_superchunk_active_wall_corner_contains_chunk(scx, scz, cx, cz)) {
        return SDK_ACTIVE_WALL_STAGE_CORNER;
    }
    if (sdk_superchunk_active_wall_edge_contains_chunk(scx, scz, cx, cz)) {
        return SDK_ACTIVE_WALL_STAGE_EDGE;
    }
    if (sdk_superchunk_active_wall_support_contains_chunk(scx, scz, cx, cz)) {
        return SDK_ACTIVE_WALL_STAGE_SUPPORT;
    }
    return SDK_ACTIVE_WALL_STAGE_NONE;
}

static inline int sdk_chunk_has_full_upload_ready_mesh(const SdkChunk* chunk) {
    /* Checks if a chunk has a fully prepared mesh ready for GPU upload.
     * This verifies multiple conditions:
     * - Chunk exists and has block data
     * - GPU upload mode is set to FULL (not FAR or PROXY)
     * - No upload is currently pending
     * - Chunk doesn't need remeshing (mesh is up to date)
     * - Either chunk is empty (no blocks) OR has current unified GPU mesh
     *
     * Returns true only when the chunk is fully ready for rendering. */
    return chunk &&
           chunk->blocks &&
           (SdkChunkGpuUploadMode)chunk->gpu_upload_mode == SDK_CHUNK_GPU_UPLOAD_FULL &&
           !chunk->upload_pending &&
           !sdk_chunk_needs_remesh(chunk) &&
           (chunk->empty || sdk_chunk_has_current_unified_gpu_mesh(chunk));
}

static inline int sdk_chunk_is_active_wall_chunk_fully_ready(const SdkChunkManager* cm,
                                                             const SdkChunk* chunk) {
    /* Checks if an active wall chunk has completed all wall finalization processing.
     * Wall chunks require special finalization to ensure proper meshing at superchunk
     * boundaries. The wall_finalized_generation tracks which topology generation
     * the chunk was last finalized for.
     *
     * Returns true only if:
     * - Chunk manager and chunk are valid
     * - Chunk is part of the active wall
     * - Chunk's wall_finalized_generation matches current topology_generation
     *
     * When topology changes (camera moves to new superchunk), this will return
     * false until the chunk is re-finalized for the new topology. */
    if (!cm || !chunk) return 0;
    if (!sdk_superchunk_active_wall_chunk_contains_chunk(cm->primary_scx,
                                                         cm->primary_scz,
                                                         chunk->cx,
                                                         chunk->cz)) {
        return 0;
    }
    return chunk->wall_finalized_generation == cm->topology_generation;
}

bool sdk_chunk_manager_is_desired(const SdkChunkManager* cm, int cx, int cz,
                                  SdkChunkResidencyRole* out_role,
                                  uint32_t* out_generation);
SdkChunkResidencyRole sdk_chunk_manager_get_chunk_role(const SdkChunkManager* cm, int cx, int cz);

typedef void (*SdkChunkCallback)(SdkChunk* chunk, int slot_index, int role, void* user);
void sdk_chunk_manager_foreach(SdkChunkManager* cm, SdkChunkCallback cb, void* user);
uint64_t sdk_chunk_manager_memory_usage(const SdkChunkManager* cm);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_CHUNK_MANAGER_H */
