/**
 * sdk_chunk.h — Chunk data structures
 *
 * A chunk is a 64×64×1024 block region of the world.
 */
#ifndef NQLSDK_CHUNK_H
#define NQLSDK_CHUNK_H

#include "../../sdk_types.h"
#include "../Blocks/sdk_block.h"
#include "../CoordinateSpaces/sdk_coordinate_space.h"
#include "../../Camera/sdk_camera.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SdkChunkSimState SdkChunkSimState;
typedef struct SdkConstructionCellStore SdkConstructionCellStore;
typedef struct SdkConstructionArchetypeRegistry SdkConstructionArchetypeRegistry;

/* =================================================================
 * CONSTANTS
 * ================================================================= */

#define CHUNK_WIDTH  64
#define CHUNK_DEPTH  64
#define CHUNK_HEIGHT 1024
#define CHUNK_SUBCHUNK_HEIGHT 64
#define CHUNK_SUBCHUNK_COUNT (CHUNK_HEIGHT / CHUNK_SUBCHUNK_HEIGHT)
#define CHUNK_BLOCKS_PER_LAYER (CHUNK_WIDTH * CHUNK_DEPTH)
#define CHUNK_TOTAL_BLOCKS (CHUNK_WIDTH * CHUNK_DEPTH * CHUNK_HEIGHT)

/* =================================================================
 * VERTEX FORMAT FOR BLOCKS
 * ================================================================= */

typedef struct {
    float    position[3];  /* World position (x, y, z) */
    uint32_t color;        /* Packed RGBA */
    uint32_t normal;       /* Packed normal index (0-5: -X, +X, -Y, +Y, -Z, +Z) */
    float    uv[2];        /* Texture coordinates within a tile */
    uint32_t tex_index;    /* Texture array layer, UINT32_MAX = untextured */
} BlockVertex;

/* =================================================================
 * CHUNK STRUCT
 * ================================================================= */

typedef struct {
    bool    dirty;         /* Needs mesh rebuild/upload */
    bool    upload_dirty;  /* CPU mesh changed; GPU upload/update required */
    bool    empty;         /* Contains no visible geometry */

    /* World-space bounds of the generated mesh slice */
    float   bounds_min[3];
    float   bounds_max[3];

    /* CPU mesh data - persists for GPU upload */
    BlockVertex* cpu_vertices;
    uint32_t vertex_count;
    uint32_t vertex_capacity;

    /* GPU resources - managed by renderer */
    void*   vb_gpu;        /* D3D12_GPU_VIRTUAL_ADDRESS (uint64_t) */
    void*   vertex_buffer; /* ID3D12Resource* */
} SdkChunkSubmesh;

typedef enum {
    SDK_CHUNK_GPU_UPLOAD_NONE = 0,
    SDK_CHUNK_GPU_UPLOAD_FAR_ONLY = 1,
    SDK_CHUNK_GPU_UPLOAD_FULL = 2
} SdkChunkGpuUploadMode;

typedef struct {
    int16_t cx, cz;        /* Chunk world position (chunk coordinates) */
    uint8_t space_type;    /* SdkCoordinateSpaceType */
    uint8_t reserved0;
    SdkWorldCellCode* blocks; /* Flat array: blocks[CHUNK_TOTAL_BLOCKS], y-major */
    SdkConstructionCellStore* construction_cells; /* Sparse overflow instance refs. */
    SdkConstructionArchetypeRegistry* construction_registry; /* Shared world/prop-local archetype table. */
    SdkChunkSimState* sim_state; /* Sparse runtime simulation state for active cells */
    uint32_t far_mesh_excluded_block_count;
    bool    dirty;         /* Any subchunk needs mesh rebuild */
    bool    empty;         /* Contains no visible geometry */
    bool    remesh_queued; /* Async remesh job is queued/in-flight */
    uint8_t geometry_dirty; /* One or more subchunk meshes need rebuild */
    uint8_t far_mesh_dirty; /* One or more far mesh representations need rebuild */
    uint8_t upload_pending; /* CPU mesh changed and still needs GPU upload */
    uint8_t gpu_upload_mode; /* SdkChunkGpuUploadMode */
    uint32_t dirty_subchunks_mask;
    uint32_t upload_subchunks_mask;
    uint32_t active_subchunks_mask;
    uint32_t sim_dirty_mask;
    uint32_t mesh_job_generation;
    uint32_t target_mesh_generation; /* Desired mesh generation (incremented when marked dirty) */
    uint32_t inflight_mesh_generation;
    uint32_t cpu_mesh_generation;
    uint32_t gpu_mesh_generation;
    uint32_t dirty_frame_age;
    uint32_t wall_finalized_generation;

    /* World-space bounds of the generated mesh */
    float   bounds_min[3];
    float   bounds_max[3];
    uint32_t vertex_count; /* Aggregate vertex count across subchunks */

    /* Unified vertex buffer (all submeshes in one GPU buffer) */
    void*    unified_vertex_buffer; /* ID3D12Resource* */
    void*    unified_vb_gpu;        /* D3D12_GPU_VIRTUAL_ADDRESS */
    BlockVertex* unified_staging;   /* CPU staging buffer */
    uint32_t unified_staging_capacity;
    uint32_t unified_total_vertices;
    uint32_t unified_vertex_capacity;
    
    /* Offsets and counts for each submesh within unified buffer */
    uint32_t subchunk_offsets[CHUNK_SUBCHUNK_COUNT];
    uint32_t subchunk_vertex_counts[CHUNK_SUBCHUNK_COUNT];
    uint32_t water_offsets[CHUNK_SUBCHUNK_COUNT];
    uint32_t water_vertex_counts[CHUNK_SUBCHUNK_COUNT];
    uint32_t far_mesh_offset;
    uint32_t far_mesh_vertex_count;
    uint32_t experimental_far_offset;
    uint32_t experimental_far_vertex_count;
    uint32_t exact_overlay_offset;
    uint32_t exact_overlay_vertex_count;

    SdkChunkSubmesh subchunks[CHUNK_SUBCHUNK_COUNT];
    SdkChunkSubmesh water_subchunks[CHUNK_SUBCHUNK_COUNT];
    SdkChunkSubmesh far_mesh; /* Low-poly far-distance proxy mesh */
    SdkChunkSubmesh experimental_far_mesh; /* Experimental far-distance proxy mesh */
    SdkChunkSubmesh far_exact_overlay_mesh; /* Exact mesh for trees and perimeter strips over far proxies */
} SdkChunk;

typedef struct {
    uint32_t full_block_count;
    uint32_t inline_construction_count;
    uint32_t overflow_construction_count;
} SdkChunkCellKindCounts;

/* =================================================================
 * FUNCTIONS
 * ================================================================= */

/** Initialize a chunk at the given chunk coordinates */
void sdk_chunk_init(SdkChunk* chunk, int cx, int cz, SdkConstructionArchetypeRegistry* construction_registry);
void sdk_chunk_init_with_space(SdkChunk* chunk,
                               int cx,
                               int cz,
                               SdkCoordinateSpaceType space_type,
                               SdkConstructionArchetypeRegistry* construction_registry);

/** Free chunk resources (CPU and GPU) */
void sdk_chunk_free(SdkChunk* chunk);

/** Mark a specific subchunk dirty. */
void sdk_chunk_mark_subchunk_dirty(SdkChunk* chunk, int subchunk_index);
void sdk_chunk_mark_subchunk_fill_dirty(SdkChunk* chunk, int subchunk_index);

/** Mark every subchunk dirty. */
void sdk_chunk_mark_all_dirty(SdkChunk* chunk);

/** Mark the subchunk touched by a block edit dirty, plus vertical neighbors at boundaries. */
void sdk_chunk_mark_block_dirty(SdkChunk* chunk, int lx, int ly, int lz);
void sdk_chunk_mark_block_fill_dirty(SdkChunk* chunk, int lx, int ly, int lz);

/** Recompute aggregate bounds, dirty flags, and total vertex count from subchunks. */
void sdk_chunk_refresh_mesh_state(SdkChunk* chunk);
void sdk_chunk_count_cell_kinds(const SdkChunk* chunk, SdkChunkCellKindCounts* out_counts);

/** Transfer CPU mesh ownership from src to dst without touching dst block data. */
void sdk_chunk_take_mesh_state(SdkChunk* dst, SdkChunk* src);
void sdk_chunk_apply_mesh_state(SdkChunk* dst, SdkChunk* src, uint32_t dirty_mask);
void sdk_chunk_recount_far_mesh_excluded_blocks(SdkChunk* chunk);

static inline SdkWorldCellKind sdk_world_cell_kind(SdkWorldCellCode code) {
    if (code == (SdkWorldCellCode)SDK_WORLD_CELL_OVERFLOW_CODE) {
        return SDK_WORLD_CELL_KIND_OVERFLOW_CONSTRUCTION;
    }
    if (code >= (SdkWorldCellCode)SDK_WORLD_CELL_INLINE_BASE) {
        return SDK_WORLD_CELL_KIND_INLINE_CONSTRUCTION;
    }
    return SDK_WORLD_CELL_KIND_FULL_BLOCK;
}

static inline int sdk_world_cell_is_full_block(SdkWorldCellCode code) {
    return sdk_world_cell_kind(code) == SDK_WORLD_CELL_KIND_FULL_BLOCK;
}

static inline int sdk_world_cell_is_inline_construction(SdkWorldCellCode code) {
    return sdk_world_cell_kind(code) == SDK_WORLD_CELL_KIND_INLINE_CONSTRUCTION;
}

static inline int sdk_world_cell_is_overflow_construction(SdkWorldCellCode code) {
    return sdk_world_cell_kind(code) == SDK_WORLD_CELL_KIND_OVERFLOW_CONSTRUCTION;
}

static inline SdkWorldCellCode sdk_world_cell_encode_full_block(BlockType type) {
    return (SdkWorldCellCode)(uint16_t)type;
}

static inline int sdk_world_cell_can_inline_block(BlockType type) {
    return (unsigned)type <= (unsigned)SDK_WORLD_CELL_INLINE_MATERIAL_MASK;
}

static inline SdkWorldCellCode sdk_world_cell_encode_inline_construction(BlockType material,
                                                                         SdkInlineConstructionProfile profile) {
    if (!sdk_world_cell_can_inline_block(material)) {
        return (SdkWorldCellCode)SDK_WORLD_CELL_OVERFLOW_CODE;
    }
    if (profile <= SDK_INLINE_PROFILE_NONE || profile >= SDK_INLINE_PROFILE_COUNT) {
        return sdk_world_cell_encode_full_block(material);
    }
    return (SdkWorldCellCode)(SDK_WORLD_CELL_INLINE_BASE |
                              (((SdkWorldCellCode)profile << SDK_WORLD_CELL_INLINE_PROFILE_SHIFT) &
                               SDK_WORLD_CELL_INLINE_PROFILE_MASK) |
                              ((SdkWorldCellCode)material & SDK_WORLD_CELL_INLINE_MATERIAL_MASK));
}

static inline BlockType sdk_world_cell_decode_full_block(SdkWorldCellCode code) {
    if (!sdk_world_cell_is_full_block(code) || code >= (SdkWorldCellCode)BLOCK_COUNT) {
        return BLOCK_AIR;
    }
    return (BlockType)code;
}

static inline BlockType sdk_world_cell_inline_material(SdkWorldCellCode code) {
    if (!sdk_world_cell_is_inline_construction(code)) return BLOCK_AIR;
    return (BlockType)(code & SDK_WORLD_CELL_INLINE_MATERIAL_MASK);
}

static inline SdkInlineConstructionProfile sdk_world_cell_inline_profile(SdkWorldCellCode code) {
    SdkInlineConstructionProfile profile;

    if (!sdk_world_cell_is_inline_construction(code)) {
        return SDK_INLINE_PROFILE_NONE;
    }
    profile = (SdkInlineConstructionProfile)((code & SDK_WORLD_CELL_INLINE_PROFILE_MASK) >>
                                             SDK_WORLD_CELL_INLINE_PROFILE_SHIFT);
    if (profile <= SDK_INLINE_PROFILE_NONE || profile >= SDK_INLINE_PROFILE_COUNT) {
        return SDK_INLINE_PROFILE_NONE;
    }
    return profile;
}

/** Get stored world-cell code at local chunk coordinates. */
static inline SdkWorldCellCode sdk_chunk_get_cell_code(const SdkChunk* chunk, int lx, int ly, int lz) {
    uint32_t idx;
    if (!chunk || !chunk->blocks) return sdk_world_cell_encode_full_block(BLOCK_AIR);
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)ly >= CHUNK_HEIGHT || (unsigned)lz >= CHUNK_DEPTH) {
        return sdk_world_cell_encode_full_block(BLOCK_AIR);
    }
    idx = (uint32_t)ly * CHUNK_BLOCKS_PER_LAYER + (uint32_t)lz * CHUNK_WIDTH + (uint32_t)lx;
    return chunk->blocks[idx];
}

static inline void sdk_chunk_set_cell_code_raw(SdkChunk* chunk, int lx, int ly, int lz, SdkWorldCellCode code) {
    uint32_t idx;
    if (!chunk || !chunk->blocks) return;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)ly >= CHUNK_HEIGHT || (unsigned)lz >= CHUNK_DEPTH) {
        return;
    }
    idx = (uint32_t)ly * CHUNK_BLOCKS_PER_LAYER + (uint32_t)lz * CHUNK_WIDTH + (uint32_t)lx;
    chunk->blocks[idx] = code;
}

/** Get block type at local chunk coordinates */
static inline BlockType sdk_chunk_get_block(const SdkChunk* chunk, int lx, int ly, int lz) {
    return sdk_world_cell_decode_full_block(sdk_chunk_get_cell_code(chunk, lx, ly, lz));
}

static inline int sdk_chunk_subchunk_index_from_y(int ly) {
    return ly / CHUNK_SUBCHUNK_HEIGHT;
}

static inline int sdk_chunk_subchunk_min_y(int subchunk_index) {
    return subchunk_index * CHUNK_SUBCHUNK_HEIGHT;
}

static inline int sdk_chunk_subchunk_max_y(int subchunk_index) {
    return sdk_chunk_subchunk_min_y(subchunk_index) + CHUNK_SUBCHUNK_HEIGHT - 1;
}

static inline int sdk_chunk_block_excluded_from_far_mesh(BlockType type) {
    return type == BLOCK_LOG || type == BLOCK_LEAVES;
}

static inline int sdk_chunk_needs_remesh(const SdkChunk* chunk) {
    return chunk && chunk->blocks && chunk->dirty &&
           (chunk->geometry_dirty != 0u || chunk->far_mesh_dirty != 0u);
}

static inline int sdk_chunk_has_current_unified_gpu_mesh(const SdkChunk* chunk) {
    return chunk &&
           chunk->unified_vertex_buffer != NULL &&
           chunk->gpu_mesh_generation != 0u &&
           chunk->gpu_mesh_generation == chunk->cpu_mesh_generation;
}

/** Set block type at local chunk coordinates */
static inline void sdk_chunk_set_block(SdkChunk* chunk, int lx, int ly, int lz, BlockType type) {
    uint32_t idx;
    BlockType old_type;
    if (!chunk || !chunk->blocks) return;
    if ((unsigned)lx >= CHUNK_WIDTH || (unsigned)ly >= CHUNK_HEIGHT || (unsigned)lz >= CHUNK_DEPTH) {
        return;
    }
    idx = (uint32_t)ly * CHUNK_BLOCKS_PER_LAYER + (uint32_t)lz * CHUNK_WIDTH + (uint32_t)lx;
    old_type = sdk_world_cell_decode_full_block(chunk->blocks[idx]);
    if (old_type == type) return;
    if (sdk_chunk_block_excluded_from_far_mesh(old_type) &&
        chunk->far_mesh_excluded_block_count > 0u) {
        chunk->far_mesh_excluded_block_count--;
    }
    chunk->blocks[idx] = sdk_world_cell_encode_full_block(type);
    if (sdk_chunk_block_excluded_from_far_mesh(type)) {
        chunk->far_mesh_excluded_block_count++;
    }
    sdk_chunk_mark_block_dirty(chunk, lx, ly, lz);
    if (type != BLOCK_AIR) chunk->empty = false;
}

/** Clear all blocks to AIR */
void sdk_chunk_clear(SdkChunk* chunk);

/** Check if chunk is visible in camera frustum */
bool sdk_chunk_is_visible(const SdkChunk* chunk, const SdkCamera* camera);

/** Check if a subchunk is visible in camera frustum. */
bool sdk_chunk_subchunk_is_visible(const SdkChunk* chunk, int subchunk_index, const SdkCamera* camera);

/** Check if a block is at the edge of the chunk (needs neighbor check) */
static inline int sdk_chunk_is_edge(int lx, int ly, int lz) {
    return lx == 0 || lx == CHUNK_WIDTH - 1 ||
           lz == 0 || lz == CHUNK_DEPTH - 1 ||
           ly == 0 || ly == CHUNK_HEIGHT - 1;
}

/** Convert world block coordinates to chunk coordinates */
static inline int sdk_world_to_chunk_x(int wx) {
    return (wx < 0) ? ((wx + 1) / CHUNK_WIDTH) - 1 : wx / CHUNK_WIDTH;
}

static inline int sdk_world_to_chunk_z(int wz) {
    return (wz < 0) ? ((wz + 1) / CHUNK_DEPTH) - 1 : wz / CHUNK_DEPTH;
}

/** Convert world block coordinates to local chunk coordinates */
static inline int sdk_world_to_local_x(int wx, int cx) {
    int lx = wx - cx * CHUNK_WIDTH;
    return lx;
}

static inline int sdk_world_to_local_z(int wz, int cz) {
    int lz = wz - cz * CHUNK_DEPTH;
    return lz;
}

/** Convert chunk + local to world coordinates */
static inline int sdk_chunk_to_world_x(int lx, int cx) {
    return cx * CHUNK_WIDTH + lx;
}

static inline int sdk_chunk_to_world_z(int lz, int cz) {
    return cz * CHUNK_DEPTH + lz;
}

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_CHUNK_H */
