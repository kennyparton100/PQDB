#include "../sdk_chunk.h"
#include "../ChunkManager/sdk_chunk_manager.h"
#include "../../../../Renderer/d3d12_renderer.h"

bool sdk_frustum_contains_aabb(const SdkFrustum* frustum,
                               float min_x,
                               float min_y,
                               float min_z,
                               float max_x,
                               float max_y,
                               float max_z)
{
    (void)frustum;
    (void)min_x;
    (void)min_y;
    (void)min_z;
    (void)max_x;
    (void)max_y;
    (void)max_z;
    return true;
}

void sdk_renderer_free_chunk_unified_buffer(SdkChunk* chunk)
{
    (void)chunk;
}

void sdk_renderer_free_chunk_mesh(SdkChunk* chunk)
{
    (void)chunk;
}

SdkChunk* sdk_chunk_manager_get_chunk(SdkChunkManager* cm, int cx, int cz)
{
    (void)cm;
    (void)cx;
    (void)cz;
    return NULL;
}
