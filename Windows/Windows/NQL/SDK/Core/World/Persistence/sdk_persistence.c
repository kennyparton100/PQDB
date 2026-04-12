/**
 * sdk_persistence.c -- Minimal local world/player persistence.
 *
 * Stores a bounded chunk cache and player/world state in a per-world JSON save.
 * The chunk cache now uses codec-tagged base64 payloads so cached chunks can
 * bypass procedural generation on the next launch without hard-wiring one
 * storage format.
 */
#include "sdk_persistence.h"
#include "sdk_chunk_save_json.h"
#include "../Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../Chunks/ChunkCompression/sdk_chunk_codec.h"
#include "../CoordinateSpaces/sdk_coordinate_space_runtime.h"
#include "../ConstructionCells/sdk_construction_cells.h"
#include "../Simulation/sdk_simulation.h"
#include "../../Settings/sdk_settings.h"
#include "../Superchunks/Config/sdk_superchunk_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SDK_PERSISTENCE_VERSION 8
#define SDK_PERSISTENCE_DEFAULT_PATH "save.json"
#define SDK_PERSISTENCE_MAX_CHUNKS SDK_CHUNK_MANAGER_MAX_RESIDENT
#define SDK_PERSISTENCE_MAX_STATIONS 512

typedef struct {
    int      cx;
    int      cz;
    int      top_y;
    int      space_type;
    uint32_t stamp;
    char*    cells_codec;
    char*    cells_payload_b64;
    int      cells_payload_version;
    char*    encoded_fluids;
    char*    encoded_construction;
} SdkPersistedChunkEntry;

typedef struct {
    int      origin_cx;
    int      origin_cz;
    int      chunk_span;
    SdkPersistedChunkEntry* chunks;
    int      chunk_count;
    int      chunk_capacity;
} SdkPersistedSuperchunkEntry;

typedef struct {
    SdkPersistedStationState state;
} SdkPersistedStationEntry;

typedef struct {
    CRITICAL_SECTION       lock;
    SdkWorldDesc           world_desc;
    SdkPersistedState      state;
    int                    has_state;
    uint32_t               next_stamp;
    SdkPersistedChunkEntry* chunks;
    int                    chunk_count;
    int                    chunk_capacity;
    SdkPersistedSuperchunkEntry* superchunks;
    int                    superchunk_count;
    int                    superchunk_capacity;
    SdkPersistedChunkEntry* wall_chunks;
    int                    wall_chunk_count;
    int                    wall_chunk_capacity;
    SdkPersistedStationEntry* stations;
    int                    station_count;
    int                    station_capacity;
    char*                  encoded_construction_registry;
    SdkConstructionArchetypeRegistry* construction_registry;
    char                   path[MAX_PATH];
} SdkPersistenceInternal;

typedef struct {
    uint8_t* data;
    size_t   len;
    size_t   cap;
} SdkByteBuffer;

static char* persistence_dup_string(const char* s)
{
    size_t len;
    char* copy;

    if (!s) return NULL;
    len = strlen(s);
    copy = (char*)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

const char* sdk_persistence_get_save_path(const SdkPersistence* persistence)
{
    const SdkPersistenceInternal* impl;

    if (!persistence || !persistence->impl) return NULL;
    impl = (const SdkPersistenceInternal*)persistence->impl;
    return impl->path;
}

/* Helper: Calculate terrain superchunk origin from chunk coordinates. */
static int calculate_superchunk_origin(int cx, int cz, int chunk_span, int* out_origin_cx, int* out_origin_cz)
{
    int period = chunk_span + 1;
    if (chunk_span <= 0) period = SDK_SUPERCHUNK_CHUNK_SPAN_LEGACY + 1;
    if (out_origin_cx) *out_origin_cx = sdk_superchunk_floor_div_i(cx, period) * period;
    if (out_origin_cz) *out_origin_cz = sdk_superchunk_floor_div_i(cz, period) * period;
    return 1;
}

/* Helper: Check if chunk belongs to a specific superchunk */
static int chunk_belongs_to_superchunk(int cx, int cz, int origin_cx, int origin_cz, int chunk_span)
{
    return (cx > origin_cx && cx <= origin_cx + chunk_span &&
            cz > origin_cz && cz <= origin_cz + chunk_span);
}

/* Helper: Find or create superchunk entry */
static SdkPersistedSuperchunkEntry* find_or_create_superchunk_entry(
    SdkPersistedSuperchunkEntry** entries, int* count, int* capacity,
    int origin_cx, int origin_cz, int chunk_span)
{
    int i;
    SdkPersistedSuperchunkEntry* entry = NULL;

    /* Search for existing entry */
    for (i = 0; i < *count; ++i) {
        if ((*entries)[i].origin_cx == origin_cx && (*entries)[i].origin_cz == origin_cz) {
            return &(*entries)[i];
        }
    }

    /* Create new entry */
    if (*count >= *capacity) {
        int new_capacity = (*capacity == 0) ? 32 : (*capacity * 2);
        int old_capacity = *capacity;
        SdkPersistedSuperchunkEntry* new_entries = (SdkPersistedSuperchunkEntry*)realloc(
            *entries, new_capacity * sizeof(SdkPersistedSuperchunkEntry));
        if (!new_entries) return NULL;
        if (new_capacity > old_capacity) {
            memset(new_entries + old_capacity, 0,
                   (size_t)(new_capacity - old_capacity) * sizeof(SdkPersistedSuperchunkEntry));
        }
        *entries = new_entries;
        *capacity = new_capacity;
    }

    entry = &(*entries)[*count];
    memset(entry, 0, sizeof(SdkPersistedSuperchunkEntry));
    entry->origin_cx = origin_cx;
    entry->origin_cz = origin_cz;
    entry->chunk_span = chunk_span;
    (*count)++;
    return entry;
}

int sdk_persistence_bind_construction_registry(SdkPersistence* persistence,
                                               SdkConstructionArchetypeRegistry* registry)
{
    SdkPersistenceInternal* impl;
    char* encoded_copy = NULL;
    int ok = 1;

    if (!persistence || !persistence->impl) return 0;
    impl = (SdkPersistenceInternal*)persistence->impl;

    EnterCriticalSection(&impl->lock);
    impl->construction_registry = registry;
    if (impl->encoded_construction_registry) {
        encoded_copy = persistence_dup_string(impl->encoded_construction_registry);
    }
    LeaveCriticalSection(&impl->lock);

    if (registry) {
        sdk_construction_registry_clear(registry);
        if (encoded_copy) {
            ok = sdk_construction_decode_registry(registry, encoded_copy);
        }
    }
    free(encoded_copy);
    return ok;
}

static const char* skip_ws_range(const char* p, const char* end)
{
    while (p && p < end && isspace((unsigned char)*p)) p++;
    return p;
}

static const char* find_in_range(const char* start, const char* end, const char* needle)
{
    size_t needle_len;
    const char* p;

    if (!start || !end || !needle) return NULL;
    needle_len = strlen(needle);
    if (needle_len == 0) return start;
    if ((size_t)(end - start) < needle_len) return NULL;

    for (p = start; p + needle_len <= end; ++p) {
        if (memcmp(p, needle, needle_len) == 0) return p;
    }
    return NULL;
}

static const char* find_matching_delim(const char* start, const char* end, char open_ch, char close_ch)
{
    int depth = 0;
    int in_string = 0;
    const char* p;

    if (!start || !end || start >= end || *start != open_ch) return NULL;

    for (p = start; p < end; ++p) {
        char ch = *p;
        if (ch == '"' && (p == start || p[-1] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == open_ch) depth++;
        else if (ch == close_ch) {
            depth--;
            if (depth == 0) return p;
        }
    }
    return NULL;
}

static int find_value_after_key(const char* start, const char* end, const char* key, const char** out_value)
{
    char pattern[128];
    const char* key_pos;
    const char* p;

    if (!start || !end || !key || !out_value) return 0;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    key_pos = find_in_range(start, end, pattern);
    if (!key_pos) return 0;

    p = key_pos + strlen(pattern);
    p = skip_ws_range(p, end);
    if (!p || p >= end || *p != ':') return 0;
    p++;
    p = skip_ws_range(p, end);
    if (!p || p >= end) return 0;
    *out_value = p;
    return 1;
}

static int parse_int_value(const char* start, const char* end, const char* key, int* out_value)
{
    const char* value;
    char* parse_end = NULL;
    long v;

    if (!find_value_after_key(start, end, key, &value)) return 0;
    v = strtol(value, &parse_end, 10);
    if (parse_end == value) return 0;
    if (out_value) *out_value = (int)v;
    return 1;
}

static int parse_float_value(const char* start, const char* end, const char* key, float* out_value)
{
    const char* value;
    char* parse_end = NULL;
    float v;

    if (!find_value_after_key(start, end, key, &value)) return 0;
    v = strtof(value, &parse_end);
    if (parse_end == value) return 0;
    if (out_value) *out_value = v;
    return 1;
}

static int parse_float_triplet_value(const char* start, const char* end, const char* key, float out_triplet[3])
{
    const char* value;
    char* parse_end = NULL;
    int i;

    if (!find_value_after_key(start, end, key, &value)) return 0;
    value = skip_ws_range(value, end);
    if (!value || value >= end || *value != '[') return 0;
    value++;

    for (i = 0; i < 3; ++i) {
        value = skip_ws_range(value, end);
        if (!value || value >= end) return 0;
        out_triplet[i] = strtof(value, &parse_end);
        if (parse_end == value) return 0;
        value = parse_end;
        value = skip_ws_range(value, end);
        if (i < 2) {
            if (!value || value >= end || *value != ',') return 0;
            value++;
        }
    }

    value = skip_ws_range(value, end);
    return (value && value < end && *value == ']');
}

static int parse_string_value(const char* start, const char* end, const char* key, char** out_string)
{
    const char* value;
    const char* str_end;
    size_t len;
    char* out;

    if (!out_string) return 0;
    *out_string = NULL;
    if (!find_value_after_key(start, end, key, &value)) return 0;
    if (!value || value >= end || *value != '"') return 0;
    value++;
    str_end = value;
    while (str_end < end) {
        if (*str_end == '"' && str_end[-1] != '\\') break;
        str_end++;
    }
    if (str_end >= end) return 0;
    len = (size_t)(str_end - value);
    out = (char*)malloc(len + 1);
    if (!out) return 0;
    memcpy(out, value, len);
    out[len] = '\0';
    *out_string = out;
    return 1;
}

static int find_object_after_key(const char* text, const char* key, const char** out_start, const char** out_end)
{
    const char* value;
    const char* end;
    size_t text_len;

    if (!text || !key || !out_start || !out_end) return 0;
    text_len = strlen(text);
    if (!find_value_after_key(text, text + text_len, key, &value)) return 0;
    if (*value != '{') return 0;
    end = find_matching_delim(value, text + text_len, '{', '}');
    if (!end) return 0;
    *out_start = value;
    *out_end = end + 1;
    return 1;
}

static int find_array_after_key(const char* text, const char* key, const char** out_start, const char** out_end)
{
    const char* value;
    const char* end;
    size_t text_len;

    if (!text || !key || !out_start || !out_end) return 0;
    text_len = strlen(text);
    if (!find_value_after_key(text, text + text_len, key, &value)) return 0;
    if (*value != '[') return 0;
    end = find_matching_delim(value, text + text_len, '[', ']');
    if (!end) return 0;
    *out_start = value;
    *out_end = end + 1;
    return 1;
}

static int parse_chunk_cells_object(const char* start,
                                    const char* end,
                                    char** out_codec,
                                    char** out_payload_b64,
                                    int* out_payload_version)
{
    const char* cells_start = NULL;
    const char* cells_end = NULL;

    if (out_codec) *out_codec = NULL;
    if (out_payload_b64) *out_payload_b64 = NULL;
    if (out_payload_version) *out_payload_version = 0;
    if (!start || !end) return 0;

    if (!find_value_after_key(start, end, "cells", &cells_start)) return 0;
    if (*cells_start != '{') return 0;
    cells_end = find_matching_delim(cells_start, end, '{', '}');
    if (!cells_end) return 0;
    cells_end++;
    if (!parse_string_value(cells_start, cells_end, "codec", out_codec) ||
        !parse_string_value(cells_start, cells_end, "payload_b64", out_payload_b64)) {
        if (out_codec) free(*out_codec);
        if (out_payload_b64) free(*out_payload_b64);
        if (out_codec) *out_codec = NULL;
        if (out_payload_b64) *out_payload_b64 = NULL;
        return 0;
    }
    if (!parse_int_value(cells_start, cells_end, "payload_version", out_payload_version)) {
        if (out_payload_version) *out_payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    }
    return 1;
}

static char* read_text_file(const char* path, size_t* out_size)
{
    FILE* file;
    long file_size;
    char* text;
    size_t read_count;

    if (out_size) *out_size = 0;
    file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    text = (char*)malloc((size_t)file_size + 1);
    if (!text) {
        fclose(file);
        return NULL;
    }
    read_count = fread(text, 1, (size_t)file_size, file);
    fclose(file);
    text[read_count] = '\0';
    if (out_size) *out_size = read_count;
    return text;
}

static int byte_buffer_reserve(SdkByteBuffer* buffer, size_t additional)
{
    size_t required;
    size_t new_cap;
    uint8_t* new_data;

    if (!buffer) return 0;
    required = buffer->len + additional;
    if (required <= buffer->cap) return 1;

    new_cap = buffer->cap ? buffer->cap : 256;
    while (new_cap < required) new_cap *= 2;

    new_data = (uint8_t*)realloc(buffer->data, new_cap);
    if (!new_data) return 0;
    buffer->data = new_data;
    buffer->cap = new_cap;
    return 1;
}

static int byte_buffer_append_byte(SdkByteBuffer* buffer, uint8_t value)
{
    if (!byte_buffer_reserve(buffer, 1)) return 0;
    buffer->data[buffer->len++] = value;
    return 1;
}

static int byte_buffer_append_u16(SdkByteBuffer* buffer, uint16_t value)
{
    if (!byte_buffer_reserve(buffer, 2)) return 0;
    buffer->data[buffer->len++] = (uint8_t)(value & 0xFFu);
    buffer->data[buffer->len++] = (uint8_t)((value >> 8u) & 0xFFu);
    return 1;
}

static int byte_buffer_append_varuint(SdkByteBuffer* buffer, uint32_t value)
{
    do {
        uint8_t byte = (uint8_t)(value & 0x7Fu);
        value >>= 7;
        if (value) byte |= 0x80u;
        if (!byte_buffer_append_byte(buffer, byte)) return 0;
    } while (value);
    return 1;
}

static int byte_buffer_read_varuint(const uint8_t* data, size_t len, size_t* io_offset, uint32_t* out_value)
{
    uint32_t value = 0;
    uint32_t shift = 0;
    size_t offset;

    if (!data || !io_offset || !out_value) return 0;
    offset = *io_offset;
    while (offset < len && shift < 35u) {
        uint8_t byte = data[offset++];
        value |= (uint32_t)(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            *io_offset = offset;
            *out_value = value;
            return 1;
        }
        shift += 7u;
    }
    return 0;
}

static int byte_buffer_read_u16(const uint8_t* data, size_t len, size_t* io_offset, uint16_t* out_value)
{
    size_t offset;

    if (!data || !io_offset || !out_value) return 0;
    offset = *io_offset;
    if (offset + 2u > len) return 0;
    *out_value = (uint16_t)data[offset] | (uint16_t)((uint16_t)data[offset + 1u] << 8u);
    *io_offset = offset + 2u;
    return 1;
}

static char* base64_encode_alloc(const uint8_t* data, size_t len)
{
    static const char k_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len;
    size_t out_cap;
    size_t i;
    size_t o = 0;
    char* out;

    if (!data || len == 0) return persistence_dup_string("");

    out_len = ((len + 2u) / 3u) * 4u;
    out_cap = out_len + 1u;
    out = (char*)malloc(out_cap);
    if (!out) return NULL;

    for (i = 0; i < len; i += 3u) {
        uint32_t octet_a = data[i];
        uint32_t octet_b = (i + 1u < len) ? data[i + 1u] : 0u;
        uint32_t octet_c = (i + 2u < len) ? data[i + 2u] : 0u;
        uint32_t triple = (octet_a << 16u) | (octet_b << 8u) | octet_c;

        if (o + 4u >= out_cap) {
            free(out);
            return NULL;
        }
        out[o++] = k_table[(triple >> 18u) & 0x3Fu];
        out[o++] = k_table[(triple >> 12u) & 0x3Fu];
        out[o++] = (i + 1u < len) ? k_table[(triple >> 6u) & 0x3Fu] : '=';
        out[o++] = (i + 2u < len) ? k_table[triple & 0x3Fu] : '=';
    }

    out[o] = '\0';
    return out;
}

static int base64_decode_char(int ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static uint8_t* base64_decode_alloc(const char* text, size_t* out_len)
{
    size_t text_len;
    size_t out_cap;
    size_t i;
    size_t o = 0;
    uint8_t* out;

    if (out_len) *out_len = 0;
    if (!text) return NULL;
    text_len = strlen(text);
    if (text_len == 0) {
        out = (uint8_t*)malloc(1);
        if (out_len) *out_len = 0;
        return out;
    }
    if ((text_len % 4u) != 0u) return NULL;

    out_cap = (text_len / 4u) * 3u;
    out = (uint8_t*)malloc(out_cap);
    if (!out) return NULL;

    for (i = 0; i < text_len; i += 4u) {
        int a = base64_decode_char((unsigned char)text[i]);
        int b = base64_decode_char((unsigned char)text[i + 1u]);
        int c = (text[i + 2u] == '=') ? -2 : base64_decode_char((unsigned char)text[i + 2u]);
        int d = (text[i + 3u] == '=') ? -2 : base64_decode_char((unsigned char)text[i + 3u]);
        uint32_t triple;

        if (a < 0 || b < 0 || c == -1 || d == -1) {
            free(out);
            return NULL;
        }

        triple = ((uint32_t)a << 18u) | ((uint32_t)b << 12u) |
                 ((uint32_t)(c < 0 ? 0 : c) << 6u) |
                 (uint32_t)(d < 0 ? 0 : d);

        if (o >= out_cap) {
            free(out);
            return NULL;
        }
        out[o++] = (uint8_t)((triple >> 16u) & 0xFFu);
        if (c != -2) {
            if (o >= out_cap) {
                free(out);
                return NULL;
            }
            out[o++] = (uint8_t)((triple >> 8u) & 0xFFu);
        }
        if (d != -2) {
            if (o >= out_cap) {
                free(out);
                return NULL;
            }
            out[o++] = (uint8_t)(triple & 0xFFu);
        }
    }

    if (out_len) *out_len = o;
    return out;
}

static int find_chunk_entry_index(const SdkPersistenceInternal* impl, int cx, int cz)
{
    int i;
    if (!impl) return -1;
    for (i = 0; i < impl->chunk_count; ++i) {
        if (impl->chunks[i].cx == cx && impl->chunks[i].cz == cz) return i;
    }
    return -1;
}

static int ensure_chunk_capacity(SdkPersistenceInternal* impl, int needed)
{
    int new_capacity;
    SdkPersistedChunkEntry* resized;

    if (!impl) return 0;
    if (needed <= impl->chunk_capacity) return 1;

    new_capacity = impl->chunk_capacity ? impl->chunk_capacity * 2 : 32;
    while (new_capacity < needed) new_capacity *= 2;

    resized = (SdkPersistedChunkEntry*)realloc(impl->chunks, (size_t)new_capacity * sizeof(SdkPersistedChunkEntry));
    if (!resized) return 0;
    memset(resized + impl->chunk_capacity, 0, (size_t)(new_capacity - impl->chunk_capacity) * sizeof(SdkPersistedChunkEntry));
    impl->chunks = resized;
    impl->chunk_capacity = new_capacity;
    return 1;
}

static int find_station_entry_index(const SdkPersistenceInternal* impl, int wx, int wy, int wz)
{
    int i;
    if (!impl) return -1;
    for (i = 0; i < impl->station_count; ++i) {
        const SdkPersistedStationState* state = &impl->stations[i].state;
        if (state->wx == wx && state->wy == wy && state->wz == wz) return i;
    }
    return -1;
}

static int ensure_station_capacity(SdkPersistenceInternal* impl, int needed)
{
    int new_capacity;
    SdkPersistedStationEntry* resized;

    if (!impl) return 0;
    if (needed <= impl->station_capacity) return 1;

    new_capacity = impl->station_capacity ? impl->station_capacity * 2 : 32;
    while (new_capacity < needed) new_capacity *= 2;

    resized = (SdkPersistedStationEntry*)realloc(impl->stations, (size_t)new_capacity * sizeof(SdkPersistedStationEntry));
    if (!resized) return 0;
    memset(resized + impl->station_capacity, 0, (size_t)(new_capacity - impl->station_capacity) * sizeof(SdkPersistedStationEntry));
    impl->stations = resized;
    impl->station_capacity = new_capacity;
    return 1;
}

static void remove_station_entry_locked(SdkPersistenceInternal* impl, int index)
{
    if (!impl || index < 0 || index >= impl->station_count) return;
    if (index != impl->station_count - 1) {
        impl->stations[index] = impl->stations[impl->station_count - 1];
    }
    memset(&impl->stations[impl->station_count - 1], 0, sizeof(impl->stations[impl->station_count - 1]));
    impl->station_count--;
}

static void upsert_station_entry_locked(SdkPersistenceInternal* impl, const SdkPersistedStationState* state)
{
    int index;

    if (!impl || !state) return;

    index = find_station_entry_index(impl, state->wx, state->wy, state->wz);
    if (index < 0) {
        if (impl->station_count >= SDK_PERSISTENCE_MAX_STATIONS) return;
        if (!ensure_station_capacity(impl, impl->station_count + 1)) return;
        index = impl->station_count++;
    }

    impl->stations[index].state = *state;
}

static void free_chunk_entry(SdkPersistedChunkEntry* entry)
{
    if (!entry) return;
    free(entry->cells_codec);
    free(entry->cells_payload_b64);
    free(entry->encoded_fluids);
    free(entry->encoded_construction);
    memset(entry, 0, sizeof(*entry));
}

static void clear_chunk_entries_locked(SdkPersistenceInternal* impl)
{
    int i;

    if (!impl) return;
    for (i = 0; i < impl->chunk_count; ++i) {
        free_chunk_entry(&impl->chunks[i]);
    }
    impl->chunk_count = 0;
    impl->next_stamp = 0;
}

static int choose_eviction_index(const SdkPersistenceInternal* impl)
{
    int i;
    int best = 0;
    uint32_t best_stamp;

    if (!impl || impl->chunk_count <= 0) return -1;
    best_stamp = impl->chunks[0].stamp;
    for (i = 1; i < impl->chunk_count; ++i) {
        if (impl->chunks[i].stamp < best_stamp) {
            best_stamp = impl->chunks[i].stamp;
            best = i;
        }
    }
    return best;
}

static int encode_chunk_cells(const SdkChunk* chunk,
                              char** out_codec,
                              char** out_payload_b64,
                              int* out_payload_version,
                              int* out_top_y)
{
    const char* fallback_codec_name;

    if (out_codec) *out_codec = NULL;
    if (out_payload_b64) *out_payload_b64 = NULL;
    if (out_payload_version) *out_payload_version = 0;
    if (out_top_y) *out_top_y = 0;
    if (!chunk || !chunk->blocks) return 0;

    if (sdk_chunk_codec_encode_auto(chunk,
                                    out_codec,
                                    out_payload_b64,
                                    out_payload_version,
                                    out_top_y)) {
        return 1;
    }

    if (!out_codec || !out_payload_b64 || !out_payload_version || !out_top_y) {
        return 0;
    }

    fallback_codec_name = sdk_chunk_codec_method_name(SDK_CHUNK_CODEC_METHOD_CELL_RLE);
    if (!fallback_codec_name) return 0;
    if (!sdk_chunk_codec_encode_with_method(chunk,
                                            SDK_CHUNK_CODEC_METHOD_CELL_RLE,
                                            out_payload_b64,
                                            out_payload_version,
                                            out_top_y)) {
        return 0;
    }
    *out_codec = persistence_dup_string(fallback_codec_name);
    if (!*out_codec) {
        free(*out_payload_b64);
        *out_payload_b64 = NULL;
        *out_payload_version = 0;
        *out_top_y = 0;
        return 0;
    }
    return 1;
}

static int decode_chunk_blocks_legacy_bytes(const char* encoded, int top_y, SdkChunk* out_chunk)
{
    uint8_t* decoded;
    size_t decoded_len = 0;
    size_t offset = 0;
    uint32_t write_index = 0;
    uint32_t total_blocks;

    if (!out_chunk || !out_chunk->blocks) return 0;
    if (top_y < 0 || top_y > CHUNK_HEIGHT) return 0;

    memset(out_chunk->blocks, 0, CHUNK_TOTAL_BLOCKS * sizeof(SdkWorldCellCode));
    if (top_y == 0) return 1;

    total_blocks = (uint32_t)top_y * (uint32_t)CHUNK_BLOCKS_PER_LAYER;
    decoded = base64_decode_alloc(encoded, &decoded_len);
    if (!decoded && total_blocks > 0) return 0;

    while (offset < decoded_len && write_index < total_blocks) {
        uint8_t type;
        uint32_t run_len;
        SdkWorldCellCode code;

        type = decoded[offset++];
        if (!byte_buffer_read_varuint(decoded, decoded_len, &offset, &run_len)) {
            free(decoded);
            return 0;
        }
        if (run_len == 0u || write_index + run_len > total_blocks) {
            free(decoded);
            return 0;
        }
        code = sdk_world_cell_encode_full_block((BlockType)type);
        while (run_len > 0u) {
            out_chunk->blocks[write_index++] = code;
            run_len--;
        }
    }

    free(decoded);
    return write_index == total_blocks;
}

static int decode_chunk_cells_legacy(const char* encoded, int top_y, int encoded_as_cell_codes, SdkChunk* out_chunk)
{
    if (!encoded_as_cell_codes) {
        return decode_chunk_blocks_legacy_bytes(encoded, top_y, out_chunk);
    }
    return sdk_chunk_codec_decode("cell_rle",
                                  SDK_CHUNK_CODEC_PAYLOAD_VERSION,
                                  encoded,
                                  top_y,
                                  out_chunk);
}

static void append_chunk_entry_locked(SdkPersistenceInternal* impl,
                                      int cx,
                                      int cz,
                                      int space_type,
                                      int top_y,
                                      char* cells_codec,
                                      char* cells_payload_b64,
                                      int cells_payload_version)
{
    int index;

    if (!impl || !cells_codec || !cells_payload_b64) {
        free(cells_codec);
        free(cells_payload_b64);
        return;
    }

    index = find_chunk_entry_index(impl, cx, cz);
    if (index < 0) {
        if (impl->chunk_count >= SDK_PERSISTENCE_MAX_CHUNKS) {
            index = choose_eviction_index(impl);
            if (index >= 0) free_chunk_entry(&impl->chunks[index]);
        } else {
            if (!ensure_chunk_capacity(impl, impl->chunk_count + 1)) {
                free(cells_codec);
                free(cells_payload_b64);
                return;
            }
            index = impl->chunk_count++;
        }
    }

    impl->chunks[index].cx = cx;
    impl->chunks[index].cz = cz;
    impl->chunks[index].space_type = space_type;
    impl->chunks[index].top_y = top_y;
    impl->chunks[index].stamp = ++impl->next_stamp;
    free(impl->chunks[index].cells_codec);
    free(impl->chunks[index].cells_payload_b64);
    impl->chunks[index].cells_codec = cells_codec;
    impl->chunks[index].cells_payload_b64 = cells_payload_b64;
    impl->chunks[index].cells_payload_version = cells_payload_version;
}

static void set_chunk_entry_fluids_locked(SdkPersistenceInternal* impl, int cx, int cz, char* encoded_fluids)
{
    int index;

    if (!impl) {
        free(encoded_fluids);
        return;
    }

    index = find_chunk_entry_index(impl, cx, cz);
    if (index < 0) {
        free(encoded_fluids);
        return;
    }

    free(impl->chunks[index].encoded_fluids);
    impl->chunks[index].encoded_fluids = encoded_fluids;
}

static void set_chunk_entry_construction_locked(SdkPersistenceInternal* impl, int cx, int cz, char* encoded_construction)
{
    int index;

    if (!impl) {
        free(encoded_construction);
        return;
    }

    index = find_chunk_entry_index(impl, cx, cz);
    if (index < 0) {
        free(encoded_construction);
        return;
    }

    free(impl->chunks[index].encoded_construction);
    impl->chunks[index].encoded_construction = encoded_construction;
}

static void parse_hotbar_array(SdkPersistenceInternal* impl, const char* array_start, const char* array_end)
{
    const char* p;
    int slot = 0;

    if (!impl || !array_start || !array_end || array_start >= array_end) return;
    p = array_start + 1;

    while (p < array_end && slot < SDK_PERSISTENCE_HOTBAR_SLOTS) {
        char* parse_end = NULL;
        long item;
        long count;
        long durability;
        long block_type = 0;
        long payload_kind = 0;
        long shaped_material = 0;
        long shaped_profile = 0;
        char occupancy_text[SDK_PERSISTENCE_SHAPED_ITEM_B64_MAX] = "";
        size_t occupancy_len = 0u;

        p = skip_ws_range(p, array_end);
        if (!p || p >= array_end || *p == ']') break;
        if (*p != '[') break;
        p++;

        item = strtol(p, &parse_end, 10);
        if (parse_end == p) break;
        p = skip_ws_range(parse_end, array_end);
        if (!p || p >= array_end || *p != ',') break;
        p++;

        count = strtol(p, &parse_end, 10);
        if (parse_end == p) break;
        p = skip_ws_range(parse_end, array_end);
        if (!p || p >= array_end || *p != ',') break;
        p++;

        durability = strtol(p, &parse_end, 10);
        if (parse_end == p) break;
        p = skip_ws_range(parse_end, array_end);
        occupancy_text[0] = '\0';

        if (p && p < array_end && *p == ',') {
            p++;
            block_type = strtol(p, &parse_end, 10);
            if (parse_end == p) break;
            p = skip_ws_range(parse_end, array_end);
        }
        if (p && p < array_end && *p == ',') {
            p++;
            payload_kind = strtol(p, &parse_end, 10);
            if (parse_end == p) break;
            p = skip_ws_range(parse_end, array_end);
        }
        if (p && p < array_end && *p == ',') {
            p++;
            shaped_material = strtol(p, &parse_end, 10);
            if (parse_end == p) break;
            p = skip_ws_range(parse_end, array_end);
        }
        if (p && p < array_end && *p == ',') {
            p++;
            shaped_profile = strtol(p, &parse_end, 10);
            if (parse_end == p) break;
            p = skip_ws_range(parse_end, array_end);
        }
        if (p && p < array_end && *p == ',') {
            const char* str_end;
            p++;
            p = skip_ws_range(p, array_end);
            if (!p || p >= array_end || *p != '"') break;
            p++;
            str_end = p;
            while (str_end < array_end) {
                if (*str_end == '"' && (str_end == p || str_end[-1] != '\\')) break;
                str_end++;
            }
            if (str_end >= array_end) break;
            occupancy_len = (size_t)(str_end - p);
            if (occupancy_len >= sizeof(occupancy_text)) occupancy_len = sizeof(occupancy_text) - 1u;
            memcpy(occupancy_text, p, occupancy_len);
            occupancy_text[occupancy_len] = '\0';
            p = skip_ws_range(str_end + 1, array_end);
        }

        if (!p || p >= array_end || *p != ']') {
            break;
        }
        p++;

        impl->state.hotbar_item[slot] = (ItemType)item;
        impl->state.hotbar_count[slot] = (int)count;
        impl->state.hotbar_durability[slot] = (int)durability;
        impl->state.hotbar_block[slot] = (BlockType)block_type;
        impl->state.hotbar_payload_kind[slot] = (uint8_t)payload_kind;
        memset(&impl->state.hotbar_shaped[slot], 0, sizeof(impl->state.hotbar_shaped[slot]));
        impl->state.hotbar_shaped[slot].material = (uint16_t)shaped_material;
        impl->state.hotbar_shaped[slot].inline_profile_hint = (uint8_t)shaped_profile;
        impl->state.hotbar_shaped_material[slot] = (uint16_t)shaped_material;
        impl->state.hotbar_shaped_profile_hint[slot] = (uint8_t)shaped_profile;
        if (occupancy_text[0] != '\0') {
            size_t decoded_len = 0u;
            uint8_t* decoded = base64_decode_alloc(occupancy_text, &decoded_len);
            if (decoded && decoded_len == sizeof(impl->state.hotbar_shaped[slot].occupancy)) {
                memcpy(impl->state.hotbar_shaped[slot].occupancy, decoded, decoded_len);
                sdk_construction_payload_refresh_metadata(&impl->state.hotbar_shaped[slot]);
            }
            free(decoded);
            strcpy_s(impl->state.hotbar_shaped_occupancy[slot],
                     sizeof(impl->state.hotbar_shaped_occupancy[slot]),
                     occupancy_text);
        }
        slot++;

        p = skip_ws_range(p, array_end);
        if (p < array_end && *p == ',') p++;
    }
}

static void parse_chunk_object_locked(SdkPersistenceInternal* impl, const char* obj_start, const char* obj_end)
{
    SdkChunkSaveJsonEntry entry;

    if (!impl || !obj_start || !obj_end) return;
    sdk_chunk_save_json_entry_init(&entry);

    if (!sdk_chunk_save_json_parse_entry(obj_start, obj_end + 1, &entry)) {
        return;
    }

    if (entry.codec && entry.payload_b64) {
        append_chunk_entry_locked(impl,
                                  entry.cx,
                                  entry.cz,
                                  entry.space_type,
                                  entry.top_y,
                                  entry.codec,
                                  entry.payload_b64,
                                  entry.payload_version);
        entry.codec = NULL;
        entry.payload_b64 = NULL;

        if (entry.fluid) {
            set_chunk_entry_fluids_locked(impl, entry.cx, entry.cz, entry.fluid);
            entry.fluid = NULL;
        }
        if (entry.construction) {
            set_chunk_entry_construction_locked(impl, entry.cx, entry.cz, entry.construction);
            entry.construction = NULL;
        }
    }
    sdk_chunk_save_json_entry_free(&entry);
}

static void parse_chunks_array(SdkPersistenceInternal* impl, const char* array_start, const char* array_end)
{
    const char* p;

    if (!impl || !array_start || !array_end || array_start >= array_end) return;
    p = array_start + 1;

    while (p < array_end) {
        const char* obj_end;

        p = skip_ws_range(p, array_end);
        if (!p || p >= array_end || *p == ']') break;
        if (*p != '{') {
            p++;
            continue;
        }

        obj_end = find_matching_delim(p, array_end, '{', '}');
        if (!obj_end) break;

        parse_chunk_object_locked(impl, p, obj_end);
        p = obj_end + 1;
    }
}

static void parse_superchunks_array(SdkPersistenceInternal* impl, const char* array_start, const char* array_end)
{
    const char* p;

    if (!impl || !array_start || !array_end || array_start >= array_end) return;
    p = array_start + 1;

    while (p < array_end) {
        const char* obj_end;
        const char* chunks_start = NULL;
        const char* chunks_end = NULL;

        p = skip_ws_range(p, array_end);
        if (!p || p >= array_end || *p == ']') break;
        if (*p != '{') {
            p++;
            continue;
        }

        obj_end = find_matching_delim(p, array_end, '{', '}');
        if (!obj_end) break;

        /* Find nested chunks array */
        if (find_array_after_key(p, "chunks", &chunks_start, &chunks_end)) {
            /* Parse nested chunks array */
            const char* chunk_p = chunks_start + 1;
            while (chunk_p < chunks_end) {
                const char* chunk_obj_end;

                chunk_p = skip_ws_range(chunk_p, chunks_end);
                if (!chunk_p || chunk_p >= chunks_end || *chunk_p == ']') break;
                if (*chunk_p != '{') {
                    chunk_p++;
                    continue;
                }

                chunk_obj_end = find_matching_delim(chunk_p, chunks_end, '{', '}');
                if (!chunk_obj_end) break;

                parse_chunk_object_locked(impl, chunk_p, chunk_obj_end);

                chunk_p = chunk_obj_end + 1;
                chunk_p = skip_ws_range(chunk_p, chunks_end);
                if (chunk_p < chunks_end && *chunk_p == ',') chunk_p++;
            }
        }

        p = obj_end + 1;
        p = skip_ws_range(p, array_end);
        if (p < array_end && *p == ',') p++;
    }
}

static void parse_wall_chunks_array(SdkPersistenceInternal* impl, const char* array_start, const char* array_end)
{
    const char* p;

    if (!impl || !array_start || !array_end || array_start >= array_end) return;
    p = array_start + 1;

    while (p < array_end) {
        const char* obj_end;

        p = skip_ws_range(p, array_end);
        if (!p || p >= array_end || *p == ']') break;
        if (*p != '{') {
            p++;
            continue;
        }

        obj_end = find_matching_delim(p, array_end, '{', '}');
        if (!obj_end) break;

        parse_chunk_object_locked(impl, p, obj_end);

        p = obj_end + 1;
        p = skip_ws_range(p, array_end);
        if (p < array_end && *p == ',') p++;
    }
}

static void parse_station_states_array(SdkPersistenceInternal* impl, const char* array_start, const char* array_end)
{
    const char* p;

    if (!impl || !array_start || !array_end || array_start >= array_end) return;
    p = array_start + 1;

    while (p < array_end) {
        const char* obj_end;
        SdkPersistedStationState state;
        int parsed_block_type = 0;
        int parsed_input_item = 0;
        int parsed_fuel_item = 0;
        int parsed_output_item = 0;

        p = skip_ws_range(p, array_end);
        if (!p || p >= array_end || *p == ']') break;
        if (*p != '{') {
            p++;
            continue;
        }

        obj_end = find_matching_delim(p, array_end, '{', '}');
        if (!obj_end) break;

        memset(&state, 0, sizeof(state));
        if (parse_int_value(p, obj_end + 1, "x", &state.wx) &&
            parse_int_value(p, obj_end + 1, "y", &state.wy) &&
            parse_int_value(p, obj_end + 1, "z", &state.wz) &&
            parse_int_value(p, obj_end + 1, "block_type", &parsed_block_type)) {
            parse_int_value(p, obj_end + 1, "input_item", &parsed_input_item);
            parse_int_value(p, obj_end + 1, "input_count", &state.input_count);
            parse_int_value(p, obj_end + 1, "fuel_item", &parsed_fuel_item);
            parse_int_value(p, obj_end + 1, "fuel_count", &state.fuel_count);
            parse_int_value(p, obj_end + 1, "output_item", &parsed_output_item);
            parse_int_value(p, obj_end + 1, "output_count", &state.output_count);
            parse_int_value(p, obj_end + 1, "progress", &state.progress);
            parse_int_value(p, obj_end + 1, "burn_remaining", &state.burn_remaining);

            state.block_type = (BlockType)parsed_block_type;
            state.input_item = (ItemType)parsed_input_item;
            state.fuel_item = (ItemType)parsed_fuel_item;
            state.output_item = (ItemType)parsed_output_item;
            upsert_station_entry_locked(impl, &state);
        }

        p = obj_end + 1;
    }
}

static void load_world_json_locked(SdkPersistenceInternal* impl)
{
    char* text;
    size_t text_size = 0;
    int version = 0;
    int worldgen_revision = 0;
    int world_seed;
    int world_sea_level;
    int world_macro_cell_size;
    int world_coordinate_system;
    int world_walls_enabled;
    int world_time = 0;
    const char* world_start = NULL;
    const char* world_end = NULL;
    const char* player_start = NULL;
    const char* player_end = NULL;
    const char* hotbar_start = NULL;
    const char* hotbar_end = NULL;
    const char* station_states_start = NULL;
    const char* station_states_end = NULL;
    const char* chunks_start = NULL;
    const char* chunks_end = NULL;
    char* encoded_construction_registry = NULL;

    if (!impl) return;
    text = read_text_file(impl->path, &text_size);
    if (!text || text_size == 0) {
        free(text);
        return;
    }

    if (!parse_int_value(text, text + text_size, "version", &version) ||
        (version != 2 && version != 4 && version != 5 && version != SDK_PERSISTENCE_VERSION)) {
        free(text);
        return;
    }

    parse_int_value(text, text + text_size, "worldgen_revision", &worldgen_revision);

    memset(&impl->state, 0, sizeof(impl->state));
    free(impl->encoded_construction_registry);
    impl->encoded_construction_registry = NULL;

    parse_string_value(text, text + text_size, "construction_archetypes", &encoded_construction_registry);
    impl->encoded_construction_registry = encoded_construction_registry;
    encoded_construction_registry = NULL;

    if (find_object_after_key(text, "world", &world_start, &world_end)) {
        if (parse_int_value(world_start, world_end, "seed", &world_seed)) {
            impl->world_desc.seed = (uint32_t)world_seed;
        }
        if (parse_int_value(world_start, world_end, "sea_level", &world_sea_level)) {
            impl->world_desc.sea_level = (int16_t)world_sea_level;
        }
        if (parse_int_value(world_start, world_end, "macro_cell_size", &world_macro_cell_size)) {
            impl->world_desc.macro_cell_size = (uint16_t)world_macro_cell_size;
        }
        if (parse_int_value(world_start, world_end, "coordinate_system", &world_coordinate_system)) {
            impl->world_desc.coordinate_system = (uint8_t)world_coordinate_system;
        }
        if (parse_int_value(world_start, world_end, "walls_enabled", &world_walls_enabled)) {
            impl->world_desc.walls_enabled = (world_walls_enabled != 0);
        }
        if (parse_int_value(world_start, world_end, "world_time", &world_time)) {
            impl->state.world_time = world_time;
        }
    }

    if (find_object_after_key(text, "player", &player_start, &player_end)) {
        char* selected_character_id = NULL;
        parse_float_triplet_value(player_start, player_end, "pos", impl->state.position);
        parse_float_triplet_value(player_start, player_end, "spawn", impl->state.spawn);
        parse_float_value(player_start, player_end, "yaw", &impl->state.cam_yaw);
        parse_float_value(player_start, player_end, "pitch", &impl->state.cam_pitch);
        parse_int_value(player_start, player_end, "health", &impl->state.health);
        parse_int_value(player_start, player_end, "hunger", &impl->state.hunger);
        parse_int_value(player_start, player_end, "selected_hotbar", &impl->state.hotbar_selected);
        parse_int_value(player_start, player_end, "chunk_grid_size", &impl->state.chunk_grid_size);
        parse_int_value(player_start, player_end, "level", &impl->state.level);
        parse_int_value(player_start, player_end, "xp", &impl->state.xp);
        parse_int_value(player_start, player_end, "xp_to_next", &impl->state.xp_to_next);
        if (parse_string_value(player_start, player_end, "selected_character_id", &selected_character_id) &&
            selected_character_id) {
            strcpy_s(impl->state.selected_character_id, sizeof(impl->state.selected_character_id),
                     selected_character_id);
        }
        free(selected_character_id);
        impl->has_state = 1;
    }

    if (find_array_after_key(text, "hotbar", &hotbar_start, &hotbar_end)) {
        parse_hotbar_array(impl, hotbar_start, hotbar_end);
    }

    if (find_array_after_key(text, "station_states", &station_states_start, &station_states_end)) {
        parse_station_states_array(impl, station_states_start, station_states_end);
    }

    if (version == SDK_PERSISTENCE_VERSION) {
        /* Version 6: Check for new formats */
        const char* superchunks_start = NULL;
        const char* superchunks_end = NULL;
        const char* terrain_superchunks_start = NULL;
        const char* terrain_superchunks_end = NULL;
        const char* wall_chunks_start = NULL;
        const char* wall_chunks_end = NULL;

        if (find_array_after_key(text, "superchunks", &superchunks_start, &superchunks_end)) {
            /* Parse superchunks array (detached mode disabled) */
            parse_superchunks_array(impl, superchunks_start, superchunks_end);
        } else if (find_array_after_key(text, "terrain_superchunks", &terrain_superchunks_start, &terrain_superchunks_end)) {
            /* Parse terrain_superchunks and wall_chunks (detached mode enabled) */
            parse_superchunks_array(impl, terrain_superchunks_start, terrain_superchunks_end);
            if (find_array_after_key(text, "wall_chunks", &wall_chunks_start, &wall_chunks_end)) {
                parse_wall_chunks_array(impl, wall_chunks_start, wall_chunks_end);
            }
        } else if (find_array_after_key(text, "chunks", &chunks_start, &chunks_end)) {
            /* Fallback to old format for compatibility */
            parse_chunks_array(impl, chunks_start, chunks_end);
        }
    } else {
        /* Old version: parse chunks array */
        if (find_array_after_key(text, "chunks", &chunks_start, &chunks_end)) {
            parse_chunks_array(impl, chunks_start, chunks_end);
        }
    }

    if (worldgen_revision != SDK_PERSISTENCE_WORLDGEN_REVISION) {
        clear_chunk_entries_locked(impl);
        OutputDebugStringA("[PERSIST] Chunk cache invalidated due to worldgen revision change\n");
    }

    free(text);
}

void sdk_persistence_init(SdkPersistence* persistence, const SdkWorldDesc* requested_world, const char* save_path)
{
    SdkPersistenceInternal* impl;
    SdkGraphicsSettings graphics;

    if (!persistence) return;
    if (persistence->impl) sdk_persistence_shutdown(persistence);

    impl = (SdkPersistenceInternal*)calloc(1, sizeof(SdkPersistenceInternal));
    if (!impl) return;

    InitializeCriticalSection(&impl->lock);
    if (requested_world) impl->world_desc = *requested_world;
    strcpy_s(impl->path, sizeof(impl->path),
             (save_path && save_path[0]) ? save_path : SDK_PERSISTENCE_DEFAULT_PATH);

    EnterCriticalSection(&impl->lock);
    load_world_json_locked(impl);
    LeaveCriticalSection(&impl->lock);

    sdk_graphics_settings_default(&graphics);
    if (sdk_graphics_settings_load(&graphics)) {
        impl->state.chunk_grid_size = graphics.chunk_grid_size;
    } else if (impl->state.chunk_grid_size < CHUNK_GRID_MIN_SIZE ||
               impl->state.chunk_grid_size > CHUNK_GRID_MAX_SIZE) {
        impl->state.chunk_grid_size = CHUNK_GRID_DEFAULT_SIZE;
    }

    persistence->impl = impl;
}

void sdk_persistence_shutdown(SdkPersistence* persistence)
{
    SdkPersistenceInternal* impl;
    SdkPersistedSuperchunkEntry* superchunks;
    int i;

    if (!persistence || !persistence->impl) return;
    impl = (SdkPersistenceInternal*)persistence->impl;
    superchunks = impl->superchunks;

    for (i = 0; i < impl->chunk_count; ++i) {
        free_chunk_entry(&impl->chunks[i]);
    }
    free(impl->chunks);

    for (i = 0; i < impl->superchunk_count && superchunks; ++i) {
        SdkPersistedSuperchunkEntry* superchunk = &superchunks[i];
        int j;

        if (superchunk->chunks) {
            for (j = 0; j < superchunk->chunk_count; ++j) {
                free_chunk_entry(&superchunk->chunks[j]);
            }
            free(superchunk->chunks);
            superchunk->chunks = NULL;
        }
    }
    free(superchunks);

    for (i = 0; i < impl->wall_chunk_count; ++i) {
        free_chunk_entry(&impl->wall_chunks[i]);
    }
    free(impl->wall_chunks);

    free(impl->stations);
    free(impl->encoded_construction_registry);
    DeleteCriticalSection(&impl->lock);
    free(impl);
    persistence->impl = NULL;
}

int sdk_persistence_get_world_desc(const SdkPersistence* persistence, SdkWorldDesc* out_desc)
{
    SdkPersistenceInternal* impl;
    if (!persistence || !persistence->impl || !out_desc) return 0;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    *out_desc = impl->world_desc;
    LeaveCriticalSection(&impl->lock);
    return 1;
}

void sdk_persistence_set_world_desc(SdkPersistence* persistence, const SdkWorldDesc* world_desc)
{
    SdkPersistenceInternal* impl;
    if (!persistence || !persistence->impl || !world_desc) return;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    impl->world_desc = *world_desc;
    LeaveCriticalSection(&impl->lock);
}

int sdk_persistence_get_state(const SdkPersistence* persistence, SdkPersistedState* out_state)
{
    SdkPersistenceInternal* impl;
    int has_state;

    if (!persistence || !persistence->impl || !out_state) return 0;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    has_state = impl->has_state;
    if (has_state) *out_state = impl->state;
    LeaveCriticalSection(&impl->lock);
    return has_state;
}

void sdk_persistence_set_state(SdkPersistence* persistence, const SdkPersistedState* state)
{
    SdkPersistenceInternal* impl;
    if (!persistence || !persistence->impl || !state) return;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    impl->state = *state;
    impl->has_state = 1;
    LeaveCriticalSection(&impl->lock);
}

int sdk_persistence_get_station_count(const SdkPersistence* persistence)
{
    SdkPersistenceInternal* impl;
    int count;

    if (!persistence || !persistence->impl) return 0;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    count = impl->station_count;
    LeaveCriticalSection(&impl->lock);
    return count;
}

int sdk_persistence_get_station_state(const SdkPersistence* persistence, int index, SdkPersistedStationState* out_state)
{
    SdkPersistenceInternal* impl;
    if (!persistence || !persistence->impl || !out_state) return 0;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    if (index < 0 || index >= impl->station_count) {
        LeaveCriticalSection(&impl->lock);
        return 0;
    }
    *out_state = impl->stations[index].state;
    LeaveCriticalSection(&impl->lock);
    return 1;
}

int sdk_persistence_find_station_state(const SdkPersistence* persistence, int wx, int wy, int wz, SdkPersistedStationState* out_state)
{
    SdkPersistenceInternal* impl;
    int index;

    if (!persistence || !persistence->impl || !out_state) return 0;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    index = find_station_entry_index(impl, wx, wy, wz);
    if (index >= 0) {
        *out_state = impl->stations[index].state;
    }
    LeaveCriticalSection(&impl->lock);
    return index >= 0;
}

void sdk_persistence_upsert_station_state(SdkPersistence* persistence, const SdkPersistedStationState* state)
{
    SdkPersistenceInternal* impl;
    if (!persistence || !persistence->impl || !state) return;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    upsert_station_entry_locked(impl, state);
    LeaveCriticalSection(&impl->lock);
}

void sdk_persistence_remove_station_state(SdkPersistence* persistence, int wx, int wy, int wz)
{
    SdkPersistenceInternal* impl;
    int index;
    if (!persistence || !persistence->impl) return;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    index = find_station_entry_index(impl, wx, wy, wz);
    if (index >= 0) remove_station_entry_locked(impl, index);
    LeaveCriticalSection(&impl->lock);
}

void sdk_persistence_clear_station_states(SdkPersistence* persistence)
{
    SdkPersistenceInternal* impl;
    if (!persistence || !persistence->impl) return;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    memset(impl->stations, 0, (size_t)impl->station_capacity * sizeof(*impl->stations));
    impl->station_count = 0;
    LeaveCriticalSection(&impl->lock);
}

int sdk_persistence_load_chunk(SdkPersistence* persistence, int cx, int cz, SdkChunk* out_chunk)
{
    SdkPersistenceInternal* impl;
    int stored_space_type = 0;
    SdkCoordinateSpaceType expected_space_type;
    int top_y = 0;
    int payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    char* codec_copy = NULL;
    char* payload_copy = NULL;
    char* fluid_copy = NULL;
    char* construction_copy = NULL;
    int index;
    int ok = 0;

    if (!persistence || !persistence->impl || !out_chunk || !out_chunk->blocks) return 0;
    impl = (SdkPersistenceInternal*)persistence->impl;

    EnterCriticalSection(&impl->lock);
    index = find_chunk_entry_index(impl, cx, cz);
    if (index >= 0 && impl->chunks[index].cells_codec && impl->chunks[index].cells_payload_b64) {
        stored_space_type = impl->chunks[index].space_type;
        top_y = impl->chunks[index].top_y;
        payload_version = impl->chunks[index].cells_payload_version;
        codec_copy = persistence_dup_string(impl->chunks[index].cells_codec);
        payload_copy = persistence_dup_string(impl->chunks[index].cells_payload_b64);
        fluid_copy = persistence_dup_string(impl->chunks[index].encoded_fluids ? impl->chunks[index].encoded_fluids : "");
        construction_copy = persistence_dup_string(impl->chunks[index].encoded_construction ? impl->chunks[index].encoded_construction : "");
        impl->chunks[index].stamp = ++impl->next_stamp;
    }
    LeaveCriticalSection(&impl->lock);

    if (!codec_copy || !payload_copy) {
        free(codec_copy);
        free(payload_copy);
        free(fluid_copy);
        free(construction_copy);
        return 0;
    }
    expected_space_type = sdk_coordinate_space_resolve_chunk_type(cx, cz);
    if (out_chunk->space_type != 0u &&
        (SdkCoordinateSpaceType)out_chunk->space_type != expected_space_type) {
        free(codec_copy);
        free(payload_copy);
        free(fluid_copy);
        free(construction_copy);
        return 0;
    }
    if (stored_space_type > 0 && stored_space_type != (int)expected_space_type) {
        free(codec_copy);
        free(payload_copy);
        free(fluid_copy);
        free(construction_copy);
        return 0;
    }
    out_chunk->space_type = (uint8_t)expected_space_type;
    if (strcmp(codec_copy, "legacy_rle") == 0) {
        ok = decode_chunk_cells_legacy(payload_copy, top_y, 0, out_chunk);
    } else {
        ok = sdk_chunk_codec_decode(codec_copy, payload_version, payload_copy, top_y, out_chunk);
    }
    free(codec_copy);
    free(payload_copy);
    if (ok && fluid_copy) {
        ok = sdk_simulation_decode_chunk_fluids(out_chunk, fluid_copy);
    }
    if (ok && construction_copy) {
        ok = sdk_construction_decode_store(out_chunk, construction_copy);
    }
    free(fluid_copy);
    free(construction_copy);

    return ok;
}

int sdk_persistence_store_chunk(SdkPersistence* persistence, const SdkChunk* chunk)
{
    SdkPersistenceInternal* impl;
    char* cells_codec;
    char* cells_payload_b64;
    char* encoded_fluids;
    char* encoded_construction;
    int payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
    int top_y = 0;

    if (!persistence || !persistence->impl || !chunk || !chunk->blocks) return 0;
    impl = (SdkPersistenceInternal*)persistence->impl;

    cells_codec = NULL;
    cells_payload_b64 = NULL;
    if (!encode_chunk_cells(chunk, &cells_codec, &cells_payload_b64, &payload_version, &top_y)) return 0;
    encoded_fluids = sdk_simulation_encode_chunk_fluids(chunk);
    if (!encoded_fluids) {
        free(cells_codec);
        free(cells_payload_b64);
        return 0;
    }
    encoded_construction = sdk_construction_encode_store(chunk);
    if (!encoded_construction) {
        free(cells_codec);
        free(cells_payload_b64);
        free(encoded_fluids);
        return 0;
    }

    EnterCriticalSection(&impl->lock);
    append_chunk_entry_locked(impl,
                              chunk->cx,
                              chunk->cz,
                              chunk->space_type,
                              top_y,
                              cells_codec,
                              cells_payload_b64,
                              payload_version);
    set_chunk_entry_fluids_locked(impl, chunk->cx, chunk->cz, encoded_fluids);
    set_chunk_entry_construction_locked(impl, chunk->cx, chunk->cz, encoded_construction);
    LeaveCriticalSection(&impl->lock);
    return 1;
}

int sdk_persistence_get_chunk_count(const SdkPersistence* persistence)
{
    SdkPersistenceInternal* impl;
    int count;

    if (!persistence || !persistence->impl) return 0;
    impl = (SdkPersistenceInternal*)persistence->impl;
    EnterCriticalSection(&impl->lock);
    count = impl->chunk_count;
    LeaveCriticalSection(&impl->lock);
    return count;
}

static void write_chunk_entry_json(FILE* file, const char* prefix, const SdkPersistedChunkEntry* entry)
{
    SdkChunkSaveJsonEntry json_entry;

    sdk_chunk_save_json_entry_init(&json_entry);
    if (entry) {
        json_entry.cx = entry->cx;
        json_entry.cz = entry->cz;
        json_entry.top_y = entry->top_y;
        json_entry.space_type = entry->space_type;
        json_entry.payload_version = entry->cells_payload_version;
        json_entry.codec = entry->cells_codec;
        json_entry.payload_b64 = entry->cells_payload_b64;
        json_entry.fluid = entry->encoded_fluids;
        json_entry.construction = entry->encoded_construction;
    }
    sdk_chunk_save_json_write_entry(file, prefix, &json_entry, SDK_CHUNK_CODEC_PAYLOAD_VERSION);
}

void sdk_persistence_save(SdkPersistence* persistence)
{
    SdkPersistenceInternal* impl;
    FILE* file;
    int i;
    int chunk_grid_size = CHUNK_GRID_DEFAULT_SIZE;
    char* encoded_construction_registry = NULL;

    if (!persistence || !persistence->impl) return;
    impl = (SdkPersistenceInternal*)persistence->impl;

    EnterCriticalSection(&impl->lock);
    chunk_grid_size = impl->state.chunk_grid_size;
    if (impl->construction_registry) {
        encoded_construction_registry = sdk_construction_encode_registry(impl->construction_registry);
        if (!encoded_construction_registry) {
            LeaveCriticalSection(&impl->lock);
            return;
        }
    }
    file = fopen(impl->path, "wb");
    if (!file) {
        free(encoded_construction_registry);
        LeaveCriticalSection(&impl->lock);
        return;
    }

    fprintf(file, "{\n");
    fprintf(file, "  \"version\": %d,\n", SDK_PERSISTENCE_VERSION);
    fprintf(file, "  \"worldgen_revision\": %d,\n", SDK_PERSISTENCE_WORLDGEN_REVISION);
    fprintf(file, "  \"world\": {\"seed\": %u, \"sea_level\": %d, \"macro_cell_size\": %u, \"coordinate_system\": %u, \"walls_enabled\": %d, \"world_time\": %d},\n",
        impl->world_desc.seed,
        (int)impl->world_desc.sea_level,
        (unsigned)impl->world_desc.macro_cell_size,
        (unsigned)impl->world_desc.coordinate_system,
        impl->world_desc.walls_enabled ? 1 : 0,
        impl->state.world_time);
    fprintf(file, "  \"construction_archetypes\": \"%s\",\n",
            encoded_construction_registry ? encoded_construction_registry : "");

    fprintf(file,
        "  \"player\": {\"pos\": [%.3f, %.3f, %.3f], \"spawn\": [%.3f, %.3f, %.3f], "
        "\"yaw\": %.6f, \"pitch\": %.6f, \"health\": %d, \"hunger\": %d, "
        "\"selected_hotbar\": %d, \"chunk_grid_size\": %d, \"level\": %d, \"xp\": %d, "
        "\"xp_to_next\": %d, \"selected_character_id\": \"%s\"},\n",
        impl->state.position[0], impl->state.position[1], impl->state.position[2],
        impl->state.spawn[0], impl->state.spawn[1], impl->state.spawn[2],
        impl->state.cam_yaw, impl->state.cam_pitch,
        impl->state.health, impl->state.hunger,
        impl->state.hotbar_selected, impl->state.chunk_grid_size, impl->state.level,
        impl->state.xp, impl->state.xp_to_next, impl->state.selected_character_id);

    fprintf(file, "  \"hotbar\": [");
    for (i = 0; i < SDK_PERSISTENCE_HOTBAR_SLOTS; ++i) {
        if (impl->state.hotbar_payload_kind[i] == SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION &&
            impl->state.hotbar_shaped[i].occupied_count > 0u) {
            char* occupancy_encoded = base64_encode_alloc(
                (const uint8_t*)impl->state.hotbar_shaped[i].occupancy,
                sizeof(impl->state.hotbar_shaped[i].occupancy));
            fprintf(file, "%s[%d,%d,%d,%d,%u,%u,%u,\"%s\"]",
                (i == 0) ? "" : ",",
                (int)impl->state.hotbar_item[i],
                impl->state.hotbar_count[i],
                impl->state.hotbar_durability[i],
                (int)impl->state.hotbar_block[i],
                (unsigned)impl->state.hotbar_payload_kind[i],
                (unsigned)impl->state.hotbar_shaped[i].material,
                (unsigned)impl->state.hotbar_shaped[i].inline_profile_hint,
                occupancy_encoded ? occupancy_encoded : "");
            free(occupancy_encoded);
        } else {
            fprintf(file, "%s[%d,%d,%d,%d]",
                (i == 0) ? "" : ",",
                (int)impl->state.hotbar_item[i],
                impl->state.hotbar_count[i],
                impl->state.hotbar_durability[i],
                (int)impl->state.hotbar_block[i]);
        }
    }
    fprintf(file, "],\n");

    fprintf(file, "  \"station_states\": [\n");
    for (i = 0; i < impl->station_count; ++i) {
        const SdkPersistedStationState* station = &impl->stations[i].state;
        fprintf(file,
            "    %s{\"x\": %d, \"y\": %d, \"z\": %d, \"block_type\": %d, "
            "\"input_item\": %d, \"input_count\": %d, \"fuel_item\": %d, \"fuel_count\": %d, "
            "\"output_item\": %d, \"output_count\": %d, \"progress\": %d, \"burn_remaining\": %d}\n",
            (i == 0) ? "" : ",",
            station->wx, station->wy, station->wz, (int)station->block_type,
            (int)station->input_item, station->input_count,
            (int)station->fuel_item, station->fuel_count,
            (int)station->output_item, station->output_count,
            station->progress, station->burn_remaining);
    }
    fprintf(file, "  ],\n");

    /* Write chunks in coordinate-system-specific format. */
    {
        SdkWorldCoordinateSystem coordinate_system =
            (SdkWorldCoordinateSystem)impl->world_desc.coordinate_system;
        int chunk_span = sdk_superchunk_get_chunk_span();

        if (coordinate_system == SDK_WORLD_COORDSYS_CHUNK_SYSTEM) {
            fprintf(file, "  \"chunks\": [\n");
            for (i = 0; i < impl->chunk_count; ++i) {
                const SdkPersistedChunkEntry* entry = &impl->chunks[i];
                write_chunk_entry_json(file, (i == 0) ? "    " : "    ,", entry);
            }
            fprintf(file, "  ]\n");
        } else if (coordinate_system == SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM) {
            fprintf(file, "  \"superchunks\": [\n");
            for (i = 0; i < impl->chunk_count; ++i) {
                const SdkPersistedChunkEntry* entry = &impl->chunks[i];
                int origin_cx, origin_cz;
                SdkPersistedSuperchunkEntry* superchunk;

                calculate_superchunk_origin(entry->cx, entry->cz, chunk_span, &origin_cx, &origin_cz);

                superchunk = find_or_create_superchunk_entry(&impl->superchunks, &impl->superchunk_count,
                                                              &impl->superchunk_capacity, origin_cx, origin_cz,
                                                              chunk_span);
                if (superchunk) {
                    /* Add chunk to superchunk */
                    if (superchunk->chunk_count >= superchunk->chunk_capacity) {
                        int new_cap = (superchunk->chunk_capacity == 0) ? 32 : (superchunk->chunk_capacity * 2);
                        int old_cap = superchunk->chunk_capacity;
                        SdkPersistedChunkEntry* new_chunks = (SdkPersistedChunkEntry*)realloc(
                            superchunk->chunks, new_cap * sizeof(SdkPersistedChunkEntry));
                        if (new_chunks) {
                            if (new_cap > old_cap) {
                                memset(new_chunks + old_cap, 0,
                                       (size_t)(new_cap - old_cap) * sizeof(SdkPersistedChunkEntry));
                            }
                            superchunk->chunks = new_chunks;
                            superchunk->chunk_capacity = new_cap;
                        }
                    }
                    if (superchunk->chunk_count < superchunk->chunk_capacity) {
                        superchunk->chunks[superchunk->chunk_count] = *entry;
                        superchunk->chunk_count++;
                    }
                }
            }

            /* Write superchunks */
            for (i = 0; i < impl->superchunk_count; ++i) {
                const SdkPersistedSuperchunkEntry* superchunk = &impl->superchunks[i];
                int j;
                fprintf(file, "    %s{\"origin_cx\": %d, \"origin_cz\": %d, \"chunk_span\": %d, \"chunks\": [\n",
                    (i == 0) ? "" : ",",
                    superchunk->origin_cx, superchunk->origin_cz, superchunk->chunk_span);
                for (j = 0; j < superchunk->chunk_count; ++j) {
                    const SdkPersistedChunkEntry* chunk_entry = &superchunk->chunks[j];
                    write_chunk_entry_json(file, (j == 0) ? "      " : "      ,", chunk_entry);
                }
                fprintf(file, "    ]}\n");
            }
            fprintf(file, "  ]\n");

            /* Clean up temporary superchunk data */
            for (i = 0; i < impl->superchunk_count; ++i) {
                free(impl->superchunks[i].chunks);
                impl->superchunks[i].chunks = NULL;
                impl->superchunks[i].chunk_count = 0;
                impl->superchunks[i].chunk_capacity = 0;
            }
        } else {
            fprintf(file, "  \"terrain_superchunks\": [\n");
            for (i = 0; i < impl->chunk_count; ++i) {
                const SdkPersistedChunkEntry* entry = &impl->chunks[i];
                int is_wall = entry->space_type == (int)SDK_SPACE_WALL_GRID;
                int origin_cx, origin_cz;
                SdkPersistedSuperchunkEntry* superchunk;

                if (is_wall) {
                    /* Add to wall_chunks */
                    if (impl->wall_chunk_count >= impl->wall_chunk_capacity) {
                        int new_cap = (impl->wall_chunk_capacity == 0) ? 32 : (impl->wall_chunk_capacity * 2);
                        SdkPersistedChunkEntry* new_chunks = (SdkPersistedChunkEntry*)realloc(
                            impl->wall_chunks, new_cap * sizeof(SdkPersistedChunkEntry));
                        if (new_chunks) {
                            impl->wall_chunks = new_chunks;
                            impl->wall_chunk_capacity = new_cap;
                        }
                    }
                    if (impl->wall_chunk_count < impl->wall_chunk_capacity) {
                        impl->wall_chunks[impl->wall_chunk_count] = *entry;
                        impl->wall_chunk_count++;
                    }
                } else {
                    /* Add to terrain_superchunks */
                    calculate_superchunk_origin(entry->cx, entry->cz, chunk_span, &origin_cx, &origin_cz);
                    superchunk = find_or_create_superchunk_entry(&impl->superchunks, &impl->superchunk_count,
                                                                  &impl->superchunk_capacity, origin_cx, origin_cz, chunk_span);
                    if (superchunk) {
                        if (superchunk->chunk_count >= superchunk->chunk_capacity) {
                            int new_cap = (superchunk->chunk_capacity == 0) ? 32 : (superchunk->chunk_capacity * 2);
                            int old_cap = superchunk->chunk_capacity;
                            SdkPersistedChunkEntry* new_chunks = (SdkPersistedChunkEntry*)realloc(
                                superchunk->chunks, new_cap * sizeof(SdkPersistedChunkEntry));
                            if (new_chunks) {
                                if (new_cap > old_cap) {
                                    memset(new_chunks + old_cap, 0,
                                           (size_t)(new_cap - old_cap) * sizeof(SdkPersistedChunkEntry));
                                }
                                superchunk->chunks = new_chunks;
                                superchunk->chunk_capacity = new_cap;
                            }
                        }
                        if (superchunk->chunk_count < superchunk->chunk_capacity) {
                            superchunk->chunks[superchunk->chunk_count] = *entry;
                            superchunk->chunk_count++;
                        }
                    }
                }
            }

            /* Write terrain_superchunks */
            for (i = 0; i < impl->superchunk_count; ++i) {
                const SdkPersistedSuperchunkEntry* superchunk = &impl->superchunks[i];
                int j;
                fprintf(file, "    %s{\"origin_cx\": %d, \"origin_cz\": %d, \"chunk_span\": %d, \"chunks\": [\n",
                    (i == 0) ? "" : ",",
                    superchunk->origin_cx, superchunk->origin_cz, superchunk->chunk_span);
                for (j = 0; j < superchunk->chunk_count; ++j) {
                    const SdkPersistedChunkEntry* chunk_entry = &superchunk->chunks[j];
                    write_chunk_entry_json(file, (j == 0) ? "      " : "      ,", chunk_entry);
                }
                fprintf(file, "    ]}\n");
            }
            fprintf(file, "  ],\n");

            /* Clean up temporary superchunk data */
            for (i = 0; i < impl->superchunk_count; ++i) {
                free(impl->superchunks[i].chunks);
                impl->superchunks[i].chunks = NULL;
                impl->superchunks[i].chunk_count = 0;
                impl->superchunks[i].chunk_capacity = 0;
            }

            /* Write wall_chunks */
            fprintf(file, "  \"wall_chunks\": [\n");
            for (i = 0; i < impl->wall_chunk_count; ++i) {
                const SdkPersistedChunkEntry* entry = &impl->wall_chunks[i];
                write_chunk_entry_json(file, (i == 0) ? "    " : "    ,", entry);
            }
            fprintf(file, "  ]\n");

            /* Clean up temporary wall_chunks */
            free(impl->wall_chunks);
            impl->wall_chunks = NULL;
            impl->wall_chunk_count = 0;
            impl->wall_chunk_capacity = 0;
        }

        /* Clean up temporary superchunks array */
        free(impl->superchunks);
        impl->superchunks = NULL;
        impl->superchunk_count = 0;
        impl->superchunk_capacity = 0;
    }

    fprintf(file, "}\n");
    fclose(file);
    LeaveCriticalSection(&impl->lock);
    free(encoded_construction_registry);

    {
        SdkGraphicsSettings graphics;
        sdk_graphics_settings_default(&graphics);
        sdk_graphics_settings_load(&graphics);
        graphics.chunk_grid_size = chunk_grid_size;
        sdk_graphics_settings_save(&graphics);
    }

    OutputDebugStringA("[PERSIST] save snapshot written\n");
}
