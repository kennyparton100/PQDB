#ifndef NQLSDK_WORLDGEN_TERRAIN_EDGE_CELLS_H
#define NQLSDK_WORLDGEN_TERRAIN_EDGE_CELLS_H

#include "../../Chunks/sdk_chunk.h"
#include "../../Worldgen/sdk_worldgen.h"

#ifdef __cplusplus
extern "C" {
#endif

void generate_terrain_edge_cells(SdkWorldGen* wg, SdkChunk* chunk);

#ifdef __cplusplus
}
#endif

#endif
