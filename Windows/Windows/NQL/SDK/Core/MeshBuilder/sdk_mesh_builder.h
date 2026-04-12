/**
 * sdk_mesh_builder.h — Mesh generation for chunks
 *
 * Generates vertex buffers from chunk block data with face culling.
 */
#ifndef NQLSDK_MESH_BUILDER_H
#define NQLSDK_MESH_BUILDER_H

#include "../World/Chunks/sdk_chunk.h"
#include "../World/Chunks/ChunkManager/sdk_chunk_manager.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =================================================================
 * CONSTANTS
 * ================================================================= */

/* Initial scratch capacity; buffer grows on demand. */
#define MESH_BUFFER_INITIAL_VERTS (CHUNK_BLOCKS_PER_LAYER * 24)

/* Face normal indices */
#define FACE_NEG_X 0  /* Left */
#define FACE_POS_X 1  /* Right */
#define FACE_NEG_Y 2  /* Bottom */
#define FACE_POS_Y 3  /* Top */
#define FACE_NEG_Z 4  /* Back */
#define FACE_POS_Z 5  /* Front */

/* =================================================================
 * MESH BUFFER
 * ================================================================= */

typedef struct {
    BlockVertex* vertices;
    uint32_t    count;
    uint32_t    capacity;
} SdkMeshBuffer;

/* =================================================================
 * FUNCTIONS
 * ================================================================= */

/** Initialize a mesh buffer with given capacity */
void sdk_mesh_buffer_init(SdkMeshBuffer* buf, uint32_t capacity);

/** Free mesh buffer memory */
void sdk_mesh_buffer_free(SdkMeshBuffer* buf);

/** Clear mesh buffer (reset count to 0) */
void sdk_mesh_buffer_clear(SdkMeshBuffer* buf);

/** Add a quad (4 vertices) to the buffer */
void sdk_mesh_buffer_add_quad(SdkMeshBuffer* buf, 
    const BlockVertex* v0, const BlockVertex* v1, 
    const BlockVertex* v2, const BlockVertex* v3);

/** Build meshes for the dirty subchunks within a chunk using face culling.
 * Only generates faces where neighbor is AIR or outside the loaded chunk set.
 * @param chunk  The chunk whose dirty subchunks should be meshed
 * @param cm     Chunk manager for cross-chunk neighbor checks (may be NULL)
 * @param output Scratch buffer reused across subchunks
 */
void sdk_mesh_build_chunk(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output);

/** Build or rebuild the low-poly far-distance proxy mesh for a chunk. */
void sdk_mesh_build_chunk_far_proxy(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output);
void sdk_mesh_build_chunk_experimental_far_proxy(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output);

/** Build meshes for all dirty chunks in a chunk manager.
 * @param cm  The chunk manager
 */
void sdk_mesh_build_all(SdkChunkManager* cm);

/** Get the six neighbor offsets for face iteration */
void sdk_mesh_get_neighbor_offsets(int face, int* out_dx, int* out_dy, int* out_dz);

/** Enable/disable worldgen debug coloration for the current thread's mesh builds. */
void sdk_mesh_set_thread_worldgen_debug_enabled(int enabled);
void sdk_mesh_set_smooth_lighting_enabled(int enabled);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_MESH_BUILDER_H */
