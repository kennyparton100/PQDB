/**
 * sdk_terrain.c -- Backwards-compatible terrain wrapper.
 */
#include "sdk_terrain.h"
#include "../Worldgen/Internal/sdk_worldgen_internal.h"

void sdk_terrain_generate_chunk(SdkChunk* chunk, uint32_t seed)
{
    sdk_worldgen_generate_chunk(chunk, seed);
}

void sdk_terrain_generate_all(SdkChunkManager* cm, uint32_t seed)
{
    int slot_index;
    if (!cm) return;
    for (slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(cm, slot_index);
        SdkChunk* chunk;
        if (!slot || !slot->occupied) continue;
        chunk = &slot->chunk;
        if (chunk->blocks && chunk->dirty) {
            sdk_worldgen_generate_chunk(chunk, seed);
        }
    }
}

float sdk_terrain_noise(float x, float z)
{
    return sdk_worldgen_fbm(x, z, 0x12345678u, 4);
}

int sdk_terrain_get_height(int wx, int wz, uint32_t seed)
{
    SdkWorldDesc desc;
    SdkWorldGen wg;
    int height = 0;
    desc.seed = seed;
    desc.sea_level = 192;
    desc.macro_cell_size = SDK_WORLDGEN_MACRO_CELL_BLOCKS;
    wg.impl = NULL;
    sdk_worldgen_init_ex(&wg, &desc, SDK_WORLDGEN_CACHE_NONE);
    if (wg.impl) {
        height = sdk_worldgen_get_surface_y_ctx(&wg, wx, wz);
    }
    sdk_worldgen_shutdown(&wg);
    return height;
}
