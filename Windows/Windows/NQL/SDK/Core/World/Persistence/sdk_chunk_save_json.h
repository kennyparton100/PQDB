#ifndef NQLSDK_CHUNK_SAVE_JSON_H
#define NQLSDK_CHUNK_SAVE_JSON_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   cx;
    int   cz;
    int   top_y;
    int   space_type;
    int   payload_version;
    char* codec;
    char* payload_b64;
    char* fluid;
    char* construction;
} SdkChunkSaveJsonEntry;

void sdk_chunk_save_json_entry_init(SdkChunkSaveJsonEntry* entry);
void sdk_chunk_save_json_entry_free(SdkChunkSaveJsonEntry* entry);

int sdk_chunk_save_json_find_array(const char* text,
                                   const char* key,
                                   const char** out_start,
                                   const char** out_end);

int sdk_chunk_save_json_next_object(const char** io_cursor,
                                    const char* end,
                                    const char** out_obj_start,
                                    const char** out_obj_end);

int sdk_chunk_save_json_parse_entry(const char* obj_start,
                                    const char* obj_end,
                                    SdkChunkSaveJsonEntry* out_entry);

void sdk_chunk_save_json_write_entry(FILE* file,
                                     const char* prefix,
                                     const SdkChunkSaveJsonEntry* entry,
                                     int default_payload_version);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_CHUNK_SAVE_JSON_H */
