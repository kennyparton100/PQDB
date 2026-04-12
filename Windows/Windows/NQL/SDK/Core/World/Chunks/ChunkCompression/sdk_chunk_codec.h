#ifndef NQLSDK_CHUNK_CODEC_H
#define NQLSDK_CHUNK_CODEC_H

#include "../sdk_chunk.h"
#include "chunk_compress.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_CHUNK_CODEC_PAYLOAD_VERSION 1
#define SDK_CHUNK_CODEC_METHOD_CELL_RLE 100

typedef struct {
    int         method_id;
    const char* codec_name;
    int         payload_version;
    int         top_y;
    uint8_t*    payload;
    size_t      payload_size;
} SdkChunkCodecBuffer;

const char* sdk_chunk_codec_method_name(int method_id);
int sdk_chunk_codec_method_from_name(const char* codec_name);
int sdk_chunk_codec_method_is_runtime_enabled(int method_id);

void sdk_chunk_codec_buffer_reset(SdkChunkCodecBuffer* buffer);

int sdk_chunk_codec_encode_auto(const SdkChunk* chunk,
                                char** out_codec_name,
                                char** out_payload_b64,
                                int* out_payload_version,
                                int* out_top_y);

int sdk_chunk_codec_encode_with_method(const SdkChunk* chunk,
                                       int method_id,
                                       char** out_payload_b64,
                                       int* out_payload_version,
                                       int* out_top_y);

int sdk_chunk_codec_encode_binary_auto(const SdkChunk* chunk,
                                       SdkChunkCodecBuffer* out_buffer);

int sdk_chunk_codec_encode_binary_with_method(const SdkChunk* chunk,
                                              int method_id,
                                              SdkChunkCodecBuffer* out_buffer);

int sdk_chunk_codec_decode(const char* codec_name,
                           int payload_version,
                           const char* payload_b64,
                           int top_y,
                           SdkChunk* out_chunk);

int sdk_chunk_codec_decode_binary(const char* codec_name,
                                  int payload_version,
                                  const uint8_t* payload,
                                  size_t payload_size,
                                  int top_y,
                                  SdkChunk* out_chunk);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_CHUNK_CODEC_H */
