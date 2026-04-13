/**
 * sdk_chunk_far_mesh_codec.h -- Encode/decode a BlockVertex array to/from base64.
 *
 * Used to persist pre-baked far LOD proxy meshes in the world save file so
 * chunks beyond render distance can be displayed without block data in RAM.
 */
#ifndef NQLSDK_CHUNK_FAR_MESH_CODEC_H
#define NQLSDK_CHUNK_FAR_MESH_CODEC_H

#include "../sdk_chunk.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encode a BlockVertex array to a NUL-terminated base64 string.
 *
 * @param verts     Pointer to vertex array (may be NULL if count==0).
 * @param count     Number of vertices.
 * @param out_b64   On success, receives a malloc'd NUL-terminated base64 string.
 *                  Caller must free() it.
 * @return 1 on success, 0 on failure.
 */
int sdk_chunk_far_mesh_encode(const BlockVertex* verts, uint32_t count, char** out_b64);

/**
 * Decode a base64 string back into a malloc'd BlockVertex array.
 *
 * @param b64        NUL-terminated base64 string.
 * @param out_verts  On success, receives a malloc'd BlockVertex array.
 *                   Caller must free() it.
 * @param out_count  On success, receives the number of vertices.
 * @return 1 on success, 0 on failure.
 */
int sdk_chunk_far_mesh_decode(const char* b64, BlockVertex** out_verts, uint32_t* out_count);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_CHUNK_FAR_MESH_CODEC_H */
