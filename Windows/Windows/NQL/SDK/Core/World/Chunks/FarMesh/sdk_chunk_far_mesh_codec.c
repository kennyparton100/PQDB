/**
 * sdk_chunk_far_mesh_codec.c -- Encode/decode a BlockVertex array to/from base64.
 *
 * Raw binary layout: uint32_t vertex_count followed by count * sizeof(BlockVertex) bytes.
 * The whole blob is then base64-encoded with the existing chunk_compress base64 API.
 */
#include "sdk_chunk_far_mesh_codec.h"
#include "../ChunkCompression/chunk_compress.h"

#include <stdlib.h>
#include <string.h>

#define SDK_FAR_MESH_MAGIC 0x464D4553u /* 'FMES' */

int sdk_chunk_far_mesh_encode(const BlockVertex* verts, uint32_t count, char** out_b64)
{
    size_t   vertex_bytes;
    size_t   payload_size;
    uint8_t* payload;
    char*    b64;
    size_t   b64_len;
    uint32_t magic;
    int      rc;

    if (!out_b64) return 0;
    *out_b64 = NULL;

    if (count == 0) {
        /* Empty mesh: encode a zero-count header only. */
        magic        = SDK_FAR_MESH_MAGIC;
        payload_size = sizeof(uint32_t) + sizeof(uint32_t); /* magic + count */
        payload      = (uint8_t*)malloc(payload_size);
        if (!payload) return 0;
        memcpy(payload,                     &magic, sizeof(uint32_t));
        memcpy(payload + sizeof(uint32_t),  &count, sizeof(uint32_t));
    } else {
        vertex_bytes = (size_t)count * sizeof(BlockVertex);
        payload_size = sizeof(uint32_t) + sizeof(uint32_t) + vertex_bytes; /* magic + count + data */
        payload      = (uint8_t*)malloc(payload_size);
        if (!payload) return 0;
        magic = SDK_FAR_MESH_MAGIC;
        memcpy(payload,                     &magic,  sizeof(uint32_t));
        memcpy(payload + sizeof(uint32_t),  &count,  sizeof(uint32_t));
        memcpy(payload + 2u * sizeof(uint32_t), verts, vertex_bytes);
    }

    rc = base64_encode(payload, payload_size, &b64, &b64_len);
    free(payload);
    if (rc != 0) return 0;

    *out_b64 = b64;
    return 1;
}

int sdk_chunk_far_mesh_decode(const char* b64, BlockVertex** out_verts, uint32_t* out_count)
{
    uint8_t*    payload      = NULL;
    size_t      payload_size = 0;
    uint32_t    magic;
    uint32_t    count;
    size_t      vertex_bytes;
    BlockVertex* verts;
    int         rc;

    if (!b64 || !out_verts || !out_count) return 0;
    *out_verts = NULL;
    *out_count = 0;

    rc = base64_decode(b64, strlen(b64), &payload, &payload_size);
    if (rc != 0 || !payload) return 0;

    if (payload_size < 2u * sizeof(uint32_t)) {
        free(payload);
        return 0;
    }

    memcpy(&magic, payload,                    sizeof(uint32_t));
    memcpy(&count, payload + sizeof(uint32_t), sizeof(uint32_t));

    if (magic != SDK_FAR_MESH_MAGIC) {
        free(payload);
        return 0;
    }

    if (count == 0) {
        free(payload);
        *out_verts = NULL;
        *out_count = 0;
        return 1;
    }

    vertex_bytes = (size_t)count * sizeof(BlockVertex);
    if (payload_size < 2u * sizeof(uint32_t) + vertex_bytes) {
        free(payload);
        return 0;
    }

    verts = (BlockVertex*)malloc(vertex_bytes);
    if (!verts) {
        free(payload);
        return 0;
    }
    memcpy(verts, payload + 2u * sizeof(uint32_t), vertex_bytes);
    free(payload);

    *out_verts = verts;
    *out_count = count;
    return 1;
}
