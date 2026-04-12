/**
 * sdk_worldgen_column_internal.h -- Shared internals for split worldgen column modules.
 */
#ifndef NQLSDK_WORLDGEN_COLUMN_INTERNAL_H
#define NQLSDK_WORLDGEN_COLUMN_INTERNAL_H
#include "../Internal/sdk_worldgen_internal.h"
#include "../../Superchunks/Geometry/sdk_superchunk_geometry.h"
#include "../../Settlements/sdk_settlement.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t wall_mask;
    uint8_t gate_mask;
    int wall_top_y;
    int west_gate_floor_y;
    int north_gate_floor_y;
    int east_gate_floor_y;
    int south_gate_floor_y;
    BlockType face_block;
    BlockType core_block;
    BlockType foundation_block;
} SdkSuperChunkWallProfile;

typedef enum {
    SDK_WORLDGEN_SURFACE_COLUMN_VOID = 0,
    SDK_WORLDGEN_SURFACE_COLUMN_DRY,
    SDK_WORLDGEN_SURFACE_COLUMN_OPEN_WATER,
    SDK_WORLDGEN_SURFACE_COLUMN_SEASONAL_ICE,
    SDK_WORLDGEN_SURFACE_COLUMN_PERENNIAL_ICE,
    SDK_WORLDGEN_SURFACE_COLUMN_LAVA
} SdkWorldGenSurfaceColumnKind;

typedef struct SdkWorldGenSurfaceColumn {
    uint16_t top_height;
    uint16_t land_height;
    uint16_t water_height;
    uint16_t water_depth;
    BlockType top_block;
    BlockType land_block;
    uint8_t kind;
    uint8_t reserved0;
    uint16_t reserved1;
} SdkWorldGenSurfaceColumn;

typedef enum {
    SDK_WORLDGEN_TREE_NONE = 0,
    SDK_WORLDGEN_TREE_ROUND,
    SDK_WORLDGEN_TREE_CONIFER,
    SDK_WORLDGEN_TREE_TROPICAL,
    SDK_WORLDGEN_TREE_SHRUB
} SdkWorldGenTreeArchetype;

typedef struct {
    float    chance;
    uint8_t  archetype;
} SdkWorldGenTreeRule;

typedef struct {
    BlockType block;
    float     roll_limit;
    uint8_t   min_moisture_band;
} SdkWorldGenPlantEntry;

typedef struct {
    uint8_t              count;
    SdkWorldGenPlantEntry entries[3];
} SdkWorldGenPlantRuleSet;

#define SDK_SUPERCHUNK_WALL_DEFAULT_TOP 511
#define SDK_SUPERCHUNK_WALL_RAISE_TRIGGER 462
#define SDK_SUPERCHUNK_WALL_CLEARANCE 50
#define SDK_SUPERCHUNK_GATE_HEIGHT 30
#define SDK_SUPERCHUNK_BUTTRESS_INTERVAL 128

BlockType rock_block_for_profile(const SdkTerrainColumnProfile* profile, int wx, int wy, int wz);
BlockType soil_block_for_profile(const SdkTerrainColumnProfile* profile);
BlockType subsoil_block_for_profile(const SdkTerrainColumnProfile* profile);
BlockType sdk_worldgen_visual_surface_block_for_profile(const SdkTerrainColumnProfile* profile);
int sdk_worldgen_score_spawn_candidate_profile(int sea_level,
                                               const SdkTerrainColumnProfile* profile,
                                               int relief);
float geology_band_weight(float distance, float inner, float outer);
void build_strata_column(SdkWorldGen* wg, const SdkTerrainColumnProfile* profile,
                         const SdkRegionFieldSample* geology,
                         int wx, int wz, int surface_y, SdkStrataColumn* out_column);
BlockType stratigraphic_block_for_y(const SdkTerrainColumnProfile* profile,
                                    const SdkRegionFieldSample* geology,
                                    const SdkStrataColumn* strata,
                                    int wx, int wy, int wz);
BlockType apply_fault_zone_override(const SdkRegionFieldSample* geology,
                                    const SdkStrataColumn* strata,
                                    BlockType host_block, int wy);
SdkResourceBodyKind resource_body_kind_at(const SdkTerrainColumnProfile* profile,
                                          const SdkRegionFieldSample* geology,
                                          const SdkStrataColumn* strata,
                                          BlockType host_block,
                                          int wy, int surface_y);
BlockType maybe_resource_block(SdkWorldGen* wg, const SdkTerrainColumnProfile* profile,
                               const SdkRegionFieldSample* geology,
                               const SdkStrataColumn* strata,
                               BlockType host_block, int wx, int wy, int wz, int surface_y);
void carve_geology_caves_in_column(SdkChunk* chunk,
                                   SdkWorldGen* wg,
                                   const SdkTerrainColumnProfile* profile,
                                   const SdkRegionFieldSample* geology,
                                   int lx,
                                   int lz,
                                   int wx,
                                   int wz);
void worldgen_set_block_fast(SdkChunk* chunk, int lx, int ly, int lz, BlockType type);
void maybe_place_tree(SdkChunk* chunk, SdkWorldGen* wg, int lx, int ly, int lz,
                      const SdkTerrainColumnProfile* profile);
void maybe_place_surface_plant(SdkChunk* chunk, SdkWorldGen* wg, int lx, int ly, int lz,
                               const SdkTerrainColumnProfile* profile, BlockType top_block);
const SdkWorldGenTreeRule* sdk_worldgen_tree_rule_for_profile(const SdkTerrainColumnProfile* profile);
const SdkWorldGenPlantRuleSet* sdk_worldgen_plant_rules_for_profile(const SdkTerrainColumnProfile* profile);
int sdk_worldgen_top_block_supports_surface_flora(BlockType top_block);
int floor_mod_superchunk(int value, int denom);
int sdk_worldgen_get_canonical_wall_chunk_owner(int cx,
                                                int cz,
                                                uint8_t* out_wall_mask,
                                                int* out_origin_cx,
                                                int* out_origin_cz,
                                                int* out_period_local_x,
                                                int* out_period_local_z);
int compute_superchunk_wall_profile(SdkWorldGen* wg,
                                    const SdkTerrainColumnProfile* terrain_profile,
                                    int wx,
                                    int wz,
                                    SdkSuperChunkWallProfile* out_profile);
int sdk_superchunk_gate_floor_y_for_side_ctx(SdkWorldGen* wg, int super_origin_x, int super_origin_z, int side);
int sdk_superchunk_gate_arch_top_y(int run_local, int gate_floor_y);
int superchunk_wall_gate_open_at(const SdkSuperChunkWallProfile* wall_profile,
                                 int local_super_x,
                                 int local_super_z,
                                 int y);
void sdk_worldgen_finalize_chunk_walls_ctx(SdkWorldGen* wg, SdkChunk* chunk);
int sdk_worldgen_generate_chunk_surface_ctx(SdkWorldGen* wg,
                                            int cx,
                                            int cz,
                                            SdkTerrainColumnProfile* scratch_profiles,
                                            SdkWorldGenSurfaceColumn* out_columns);
#ifdef __cplusplus
}
#endif
#endif /* NQLSDK_WORLDGEN_COLUMN_INTERNAL_H */
