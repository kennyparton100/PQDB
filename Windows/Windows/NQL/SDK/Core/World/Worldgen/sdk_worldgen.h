/**
 * sdk_worldgen.h -- Geology-first terrain generation entry points.
 */
#ifndef NQLSDK_WORLDGEN_H
#define NQLSDK_WORLDGEN_H

#include "../Chunks/sdk_chunk.h"
#include "../../sdk_types.h"
#include "Types/sdk_worldgen_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_WORLDGEN_MACRO_CELL_BLOCKS 32
#define SDK_WORLDGEN_MACRO_TILE_SIZE   128
#define SDK_WORLDGEN_MACRO_TILE_HALO   8
#define SDK_WORLDGEN_TILE_CACHE_SLOTS  8

typedef struct {
    SdkWorldDesc desc;
    void*        impl;
} SdkWorldGen;

typedef enum {
    SDK_WORLDGEN_CACHE_NONE = 0,
    SDK_WORLDGEN_CACHE_DISK = 1
} SdkWorldGenCacheMode;

typedef enum {
    SDK_WORLDGEN_DEBUG_OFF = 0,
    SDK_WORLDGEN_DEBUG_FORMATIONS,
    SDK_WORLDGEN_DEBUG_STRUCTURES,
    SDK_WORLDGEN_DEBUG_BODIES
} SdkWorldGenDebugMode;

void sdk_worldgen_init(SdkWorldGen* wg, const SdkWorldDesc* desc);
void sdk_worldgen_init_ex(SdkWorldGen* wg, const SdkWorldDesc* desc, SdkWorldGenCacheMode cache_mode);
void sdk_worldgen_shutdown(SdkWorldGen* wg);

int sdk_worldgen_sample_column_ctx(SdkWorldGen* wg, int wx, int wz, SdkTerrainColumnProfile* out_profile);
int sdk_worldgen_get_surface_y_ctx(SdkWorldGen* wg, int wx, int wz);
void sdk_worldgen_generate_chunk_ctx(SdkWorldGen* wg, SdkChunk* chunk);
void sdk_worldgen_set_debug_mode_ctx(SdkWorldGen* wg, SdkWorldGenDebugMode mode);
SdkWorldGenDebugMode sdk_worldgen_get_debug_mode_ctx(const SdkWorldGen* wg);
void sdk_worldgen_scan_resource_signature(SdkWorldGen* wg, int center_wx, int center_wz, int radius, SdkResourceSignature* out_signature);

const SdkTerrainColumnProfile* sdk_worldgen_sample_column(int wx, int wz);
int sdk_worldgen_get_surface_y(int wx, int wz);
void sdk_worldgen_generate_chunk(SdkChunk* chunk, uint32_t seed);
void sdk_worldgen_set_debug_mode(SdkWorldGenDebugMode mode);
SdkWorldGenDebugMode sdk_worldgen_get_debug_mode(void);
uint32_t sdk_worldgen_get_debug_color(int wx, int wy, int wz, BlockType actual_block);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_WORLDGEN_H */
