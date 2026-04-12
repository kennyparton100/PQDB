/**
 * sdk_terrain.h -- Compatibility facade over geology-first worldgen.
 */
#ifndef NQLSDK_TERRAIN_H
#define NQLSDK_TERRAIN_H

#include "../Chunks/sdk_chunk.h"
#include "../Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../Worldgen/sdk_worldgen.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void sdk_terrain_generate_chunk(SdkChunk* chunk, uint32_t seed);
void sdk_terrain_generate_all(SdkChunkManager* cm, uint32_t seed);
float sdk_terrain_noise(float x, float z);
int sdk_terrain_get_height(int wx, int wz, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_TERRAIN_H */
