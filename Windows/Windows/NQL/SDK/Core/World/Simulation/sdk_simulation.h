/**
 * sdk_simulation.h -- Sparse runtime material simulation for loaded chunks.
 */
#ifndef NQLSDK_SIMULATION_H
#define NQLSDK_SIMULATION_H

#include "../Chunks/sdk_chunk.h"
#include "../Chunks/ChunkManager/sdk_chunk_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t SdkSimCellKey;

typedef struct {
    SdkSimCellKey key;
    uint8_t fill;
    uint8_t material_kind;
    uint8_t flags;
    uint8_t settled_ticks;
} SdkFluidCellState;

typedef struct {
    SdkSimCellKey key;
    uint8_t flags;
    uint8_t repose_bias;
} SdkLooseCellState;

#define SDK_FLUID_DEBUG_REASON_MAX 48

typedef enum {
    SDK_FLUID_DEBUG_MECH_IDLE = 0,
    SDK_FLUID_DEBUG_MECH_BULK_RESERVOIR,
    SDK_FLUID_DEBUG_MECH_LOCAL_WAKE
} SdkFluidDebugMechanism;

typedef struct {
    int      mechanism;
    int      last_seed_wx;
    int      last_seed_wy;
    int      last_seed_wz;
    uint32_t reservoir_columns;
    uint32_t total_columns;
    int      worker_count;
    uint32_t tick_processed;
    uint32_t dirty_cells;
    uint32_t active_chunks;
    uint32_t visited_columns;
    uint32_t spill_columns;
    uint32_t stage_index;
    uint32_t stage_count;
    uint32_t truncated_flags;
    uint32_t dedupe_hits;
    float    total_volume;
    float    target_surface_e;
    float    solve_ms;
    float    apply_ms;
    char     reason[SDK_FLUID_DEBUG_REASON_MAX];
} SdkFluidDebugInfo;

struct SdkChunkSimState {
    SdkFluidCellState* fluid_cells;
    uint32_t fluid_capacity;
    uint32_t fluid_count;
    SdkSimCellKey* fluid_lookup_keys;
    uint32_t* fluid_lookup_indices;
    uint32_t fluid_lookup_capacity;

    SdkLooseCellState* loose_cells;
    uint32_t loose_capacity;
    uint32_t loose_count;

    SdkSimCellKey* dirty_queue;
    uint32_t dirty_head;
    uint32_t dirty_count;
    uint32_t dirty_capacity;
    uint32_t* dirty_marks;
    uint32_t dirty_mark_words;
};

SdkChunkSimState* sdk_simulation_chunk_state_create(void);
void sdk_simulation_chunk_state_destroy(SdkChunkSimState* state);
void sdk_simulation_chunk_state_clear(SdkChunkSimState* state);
SdkChunkSimState* sdk_simulation_clone_chunk_state_for_snapshot(const SdkChunkSimState* src);

SdkSimCellKey sdk_simulation_pack_local_key(int lx, int ly, int lz);
void sdk_simulation_unpack_local_key(SdkSimCellKey key, int* out_lx, int* out_ly, int* out_lz);

uint8_t sdk_simulation_get_fluid_fill(const SdkChunk* chunk, int lx, int ly, int lz);
void sdk_simulation_enqueue_local(SdkChunk* chunk, int lx, int ly, int lz);
void sdk_simulation_enqueue_world(SdkChunkManager* cm, int wx, int wy, int wz);
void sdk_simulation_on_block_changed(SdkChunkManager* cm, int wx, int wy, int wz, BlockType old_type, BlockType new_type);
void sdk_simulation_on_chunk_loaded(SdkChunkManager* cm, SdkChunk* chunk);
void sdk_simulation_tick_chunk_manager(SdkChunkManager* cm, int max_cells);
void sdk_simulation_get_debug_info(const SdkChunkManager* cm, SdkFluidDebugInfo* out_info);
void sdk_simulation_invalidate_reservoirs(void);
void sdk_simulation_begin_shutdown(void);
int  sdk_simulation_poll_shutdown(void);
void sdk_simulation_shutdown(void);

char* sdk_simulation_encode_chunk_fluids(const SdkChunk* chunk);
int sdk_simulation_decode_chunk_fluids(SdkChunk* chunk, const char* encoded);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SIMULATION_H */
