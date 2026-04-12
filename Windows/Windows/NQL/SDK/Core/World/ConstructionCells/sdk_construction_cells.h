#ifndef NQLSDK_CONSTRUCTION_CELLS_H
#define NQLSDK_CONSTRUCTION_CELLS_H

#include "../Chunks/sdk_chunk.h"
#include "../Chunks/ChunkManager/sdk_chunk_manager.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_CONSTRUCTION_FACE_MASK_WORDS 4
#define SDK_CONSTRUCTION_EDIT_FRAGMENT_MAX 8

typedef struct SdkConstructionOverflowInstance {
    uint32_t local_index;
    SdkConstructionArchetypeId archetype_id;
} SdkConstructionOverflowInstance;

typedef struct SdkConstructionCellStore {
    SdkConstructionOverflowInstance* entries;
    SdkConstructionArchetypeRegistry* registry;
    uint32_t count;
    uint32_t capacity;
} SdkConstructionCellStore;

typedef struct SdkConstructionArchetype {
    SdkConstructionArchetypeId id;
    uint32_t hash;
    uint32_t refcount;
    uint16_t occupied_count;
    uint8_t palette_count;
    uint8_t display_material; /* BlockType */
    uint8_t bounds_min[3];
    uint8_t bounds_max[3]; /* Exclusive */
    uint64_t face_masks[6][SDK_CONSTRUCTION_FACE_MASK_WORDS];
    uint16_t palette[SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE];
    uint8_t voxel_palette[SDK_CONSTRUCTION_CELL_VOXELS];
} SdkConstructionArchetype;

typedef struct SdkConstructionArchetypeRegistry {
    SdkConstructionArchetype** slots;
    uint32_t slot_count;
    uint32_t slot_capacity;
    uint32_t active_count;
    SdkConstructionArchetypeId* hash_table;
    uint32_t hash_capacity;
    uint32_t revision;
    SRWLOCK lock;
} SdkConstructionArchetypeRegistry;

typedef struct SdkConstructionWorkspace {
    uint16_t voxels[SDK_CONSTRUCTION_CELL_VOXELS]; /* BlockType per voxel, 0 = BLOCK_AIR */
} SdkConstructionWorkspace;

typedef struct SdkConstructionFragment {
    int wx;
    int wy;
    int wz;
    SdkConstructionItemPayload payload;
} SdkConstructionFragment;

typedef struct SdkConstructionEditOutcome {
    int changed;
    int fragment_count;
    SdkConstructionFragment fragments[SDK_CONSTRUCTION_EDIT_FRAGMENT_MAX];
} SdkConstructionEditOutcome;

typedef struct SdkConstructionPlacementResolution {
    int valid;
    int face_u;
    int face_v;
    SdkConstructionItemPayload resolved_payload;
    SdkConstructionItemPayload preview_payload;
} SdkConstructionPlacementResolution;

void sdk_construction_clear_occupancy(uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT]);
void sdk_construction_fill_full_occupancy(uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT]);
void sdk_construction_fill_profile_occupancy(SdkInlineConstructionProfile profile,
                                             uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT]);
int sdk_construction_try_match_inline_profile(const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                              SdkInlineConstructionProfile* out_profile);
void sdk_construction_payload_from_full_block(BlockType material, SdkConstructionItemPayload* out_payload);
void sdk_construction_payload_make_box(BlockType material, int width, int height, int depth,
                                       SdkConstructionItemPayload* out_payload);
void sdk_construction_payload_transform_for_face(const SdkConstructionItemPayload* src,
                                                 int face, int rotation,
                                                 SdkConstructionItemPayload* out_payload);
void sdk_construction_payload_copy(SdkConstructionItemPayload* dst, const SdkConstructionItemPayload* src);
void sdk_construction_payload_refresh_metadata(SdkConstructionItemPayload* payload);
int sdk_construction_payload_empty(const SdkConstructionItemPayload* payload);
BlockType sdk_construction_payload_material(const SdkConstructionItemPayload* payload);
int sdk_construction_payload_matches_inline(const SdkConstructionItemPayload* payload,
                                            SdkInlineConstructionProfile* out_profile);
int sdk_construction_payload_same_item_identity(const SdkConstructionItemPayload* a,
                                                const SdkConstructionItemPayload* b);

int sdk_construction_occupancy_get(const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                   int x, int y, int z);
void sdk_construction_occupancy_set(uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                    int x, int y, int z, int occupied);
void sdk_construction_occupancy_bounds(const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                       uint16_t* out_count,
                                       uint8_t out_min[3],
                                       uint8_t out_max[3]);

SdkConstructionArchetypeRegistry* sdk_construction_registry_create(void);
void sdk_construction_registry_free(SdkConstructionArchetypeRegistry* registry);
void sdk_construction_registry_clear(SdkConstructionArchetypeRegistry* registry);
char* sdk_construction_encode_registry(const SdkConstructionArchetypeRegistry* registry);
int sdk_construction_decode_registry(SdkConstructionArchetypeRegistry* registry, const char* encoded);

void sdk_construction_store_free(SdkConstructionCellStore* store);
SdkConstructionCellStore* sdk_construction_store_clone(const SdkConstructionCellStore* src);
void sdk_construction_chunk_set_registry(SdkChunk* chunk, SdkConstructionArchetypeRegistry* registry);

int sdk_construction_chunk_get_cell_payload(const SdkChunk* chunk,
                                            int lx, int ly, int lz,
                                            BlockType* out_material,
                                            uint64_t out_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                            SdkInlineConstructionProfile* out_profile);
int sdk_construction_chunk_get_workspace(const SdkChunk* chunk,
                                         int lx, int ly, int lz,
                                         SdkConstructionWorkspace* out_workspace);
BlockType sdk_construction_chunk_get_display_material(const SdkChunk* chunk, int lx, int ly, int lz);
int sdk_construction_chunk_cell_has_occupancy(const SdkChunk* chunk, int lx, int ly, int lz);
int sdk_construction_chunk_set_cell_payload(SdkChunk* chunk,
                                            int lx, int ly, int lz,
                                            BlockType material,
                                            const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT]);
int sdk_construction_chunk_set_workspace(SdkChunk* chunk,
                                         int lx, int ly, int lz,
                                         const SdkConstructionWorkspace* workspace);
void sdk_construction_chunk_clear_cell(SdkChunk* chunk, int lx, int ly, int lz);

int sdk_construction_world_cell_has_occupancy(const SdkChunkManager* cm, int wx, int wy, int wz);
BlockType sdk_construction_world_cell_display_material(const SdkChunkManager* cm, int wx, int wy, int wz);
int sdk_construction_world_cell_payload(const SdkChunkManager* cm,
                                        int wx, int wy, int wz,
                                        BlockType* out_material,
                                        uint64_t out_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT]);

int sdk_construction_world_cell_intersects_aabb(const SdkChunkManager* cm,
                                                int wx, int wy, int wz,
                                                float min_x, float min_y, float min_z,
                                                float max_x, float max_y, float max_z);
int sdk_construction_world_cell_raycast(const SdkChunkManager* cm,
                                        int wx, int wy, int wz,
                                        float ox, float oy, float oz,
                                        float dx, float dy, float dz,
                                        float max_dist,
                                        float* out_dist,
                                        int* out_face);

int sdk_construction_resolve_face_placement(const SdkChunkManager* cm,
                                            int wx, int wy, int wz,
                                            const SdkConstructionItemPayload* payload,
                                            int face,
                                            int rotation,
                                            float hit_x, float hit_y, float hit_z,
                                            SdkConstructionPlacementResolution* out_resolution);

int sdk_construction_place_payload(SdkChunkManager* cm,
                                   int wx, int wy, int wz,
                                   const SdkConstructionItemPayload* payload,
                                   SdkConstructionEditOutcome* out_outcome);
int sdk_construction_cut_world_cell(SdkChunkManager* cm,
                                    int wx, int wy, int wz,
                                    int face,
                                    ToolClass tool,
                                    SdkConstructionEditOutcome* out_outcome);
int sdk_construction_remove_world_cell(SdkChunkManager* cm,
                                       int wx, int wy, int wz,
                                       SdkConstructionEditOutcome* out_outcome);

char* sdk_construction_encode_store(const SdkChunk* chunk);
int sdk_construction_decode_store(SdkChunk* chunk, const char* encoded);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_CONSTRUCTION_CELLS_H */
