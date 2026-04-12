#include "sdk_chunk_save_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* chunk_save_dup_string(const char* s)
{
    size_t len;
    char* copy;

    if (!s) return NULL;
    len = strlen(s);
    copy = (char*)malloc(len + 1u);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1u);
    return copy;
}

void sdk_chunk_save_json_entry_init(SdkChunkSaveJsonEntry* entry)
{
    if (!entry) return;
    memset(entry, 0, sizeof(*entry));
}

void sdk_chunk_save_json_entry_free(SdkChunkSaveJsonEntry* entry)
{
    if (!entry) return;
    free(entry->codec);
    free(entry->payload_b64);
    free(entry->fluid);
    free(entry->construction);
    memset(entry, 0, sizeof(*entry));
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
    if (needle_len == 0u) return start;
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

static int find_value_after_key_range(const char* start,
                                      const char* end,
                                      const char* key,
                                      const char** out_value)
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

static int parse_int_value_range(const char* start, const char* end, const char* key, int* out_value)
{
    const char* value;
    char* parse_end = NULL;
    long parsed;

    if (!find_value_after_key_range(start, end, key, &value)) return 0;
    parsed = strtol(value, &parse_end, 10);
    if (parse_end == value) return 0;
    if (out_value) *out_value = (int)parsed;
    return 1;
}

static int parse_string_value_range(const char* start, const char* end, const char* key, char** out_string)
{
    const char* value;
    const char* str_end;
    size_t len;
    char* out;

    if (!out_string) return 0;
    *out_string = NULL;
    if (!find_value_after_key_range(start, end, key, &value)) return 0;
    if (!value || value >= end || *value != '"') return 0;
    value++;
    str_end = value;
    while (str_end < end) {
        if (*str_end == '"' && str_end[-1] != '\\') break;
        str_end++;
    }
    if (str_end >= end) return 0;
    len = (size_t)(str_end - value);
    out = (char*)malloc(len + 1u);
    if (!out) return 0;
    memcpy(out, value, len);
    out[len] = '\0';
    *out_string = out;
    return 1;
}

int sdk_chunk_save_json_find_array(const char* text,
                                   const char* key,
                                   const char** out_start,
                                   const char** out_end)
{
    const char* value;
    const char* end;
    size_t text_len;

    if (!text || !key || !out_start || !out_end) return 0;
    text_len = strlen(text);
    if (!find_value_after_key_range(text, text + text_len, key, &value)) return 0;
    if (*value != '[') return 0;
    end = find_matching_delim(value, text + text_len, '[', ']');
    if (!end) return 0;
    *out_start = value;
    *out_end = end + 1;
    return 1;
}

int sdk_chunk_save_json_next_object(const char** io_cursor,
                                    const char* end,
                                    const char** out_obj_start,
                                    const char** out_obj_end)
{
    const char* p;
    const char* obj_end;

    if (!io_cursor || !*io_cursor || !end || !out_obj_start || !out_obj_end) return 0;

    p = *io_cursor;
    while (p < end) {
        p = skip_ws_range(p, end);
        if (!p || p >= end || *p == ']') return 0;
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '{') break;
        p++;
    }
    if (!p || p >= end || *p != '{') return 0;

    obj_end = find_matching_delim(p, end, '{', '}');
    if (!obj_end) return 0;

    *out_obj_start = p;
    *out_obj_end = obj_end + 1;
    *io_cursor = obj_end + 1;
    return 1;
}

static int parse_cells_object(const char* start,
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

    if (!find_value_after_key_range(start, end, "cells", &cells_start)) return 0;
    if (*cells_start != '{') return 0;
    cells_end = find_matching_delim(cells_start, end, '{', '}');
    if (!cells_end) return 0;
    cells_end++;

    if (!parse_string_value_range(cells_start, cells_end, "codec", out_codec) ||
        !parse_string_value_range(cells_start, cells_end, "payload_b64", out_payload_b64)) {
        if (out_codec) {
            free(*out_codec);
            *out_codec = NULL;
        }
        if (out_payload_b64) {
            free(*out_payload_b64);
            *out_payload_b64 = NULL;
        }
        return 0;
    }

    if (!parse_int_value_range(cells_start, cells_end, "payload_version", out_payload_version) &&
        out_payload_version) {
        *out_payload_version = 1;
    }
    return 1;
}

int sdk_chunk_save_json_parse_entry(const char* obj_start,
                                    const char* obj_end,
                                    SdkChunkSaveJsonEntry* out_entry)
{
    char* legacy_payload = NULL;
    int legacy_is_cell_codes = 0;

    if (!obj_start || !obj_end || !out_entry) return 0;
    sdk_chunk_save_json_entry_free(out_entry);
    out_entry->space_type = 0;

    if (!parse_int_value_range(obj_start, obj_end, "cx", &out_entry->cx) ||
        !parse_int_value_range(obj_start, obj_end, "cz", &out_entry->cz) ||
        !parse_int_value_range(obj_start, obj_end, "top_y", &out_entry->top_y)) {
        sdk_chunk_save_json_entry_free(out_entry);
        return 0;
    }

    if (!parse_cells_object(obj_start,
                            obj_end,
                            &out_entry->codec,
                            &out_entry->payload_b64,
                            &out_entry->payload_version)) {
        if (parse_string_value_range(obj_start, obj_end, "cell_rle", &legacy_payload)) {
            legacy_is_cell_codes = 1;
        } else if (parse_string_value_range(obj_start, obj_end, "rle", &legacy_payload)) {
            legacy_is_cell_codes = 0;
        }

        if (!legacy_payload) {
            sdk_chunk_save_json_entry_free(out_entry);
            return 0;
        }

        out_entry->codec = chunk_save_dup_string(legacy_is_cell_codes ? "cell_rle" : "legacy_rle");
        out_entry->payload_b64 = legacy_payload;
        out_entry->payload_version = 1;
        if (!out_entry->codec) {
            sdk_chunk_save_json_entry_free(out_entry);
            return 0;
        }
    }

    (void)parse_int_value_range(obj_start, obj_end, "space_type", &out_entry->space_type);
    parse_string_value_range(obj_start, obj_end, "fluid", &out_entry->fluid);
    parse_string_value_range(obj_start, obj_end, "construction", &out_entry->construction);
    return 1;
}

void sdk_chunk_save_json_write_entry(FILE* file,
                                     const char* prefix,
                                     const SdkChunkSaveJsonEntry* entry,
                                     int default_payload_version)
{
    if (!file) return;

    fprintf(file,
            "%s{\"cx\": %d, \"cz\": %d, \"top_y\": %d, \"space_type\": %d, "
            "\"cells\": {\"codec\": \"%s\", \"payload_b64\": \"%s\", \"payload_version\": %d}, "
            "\"fluid\": \"%s\", \"construction\": \"%s\"}\n",
            prefix ? prefix : "",
            entry ? entry->cx : 0,
            entry ? entry->cz : 0,
            entry ? entry->top_y : 0,
            entry ? entry->space_type : 0,
            (entry && entry->codec) ? entry->codec : "cell_rle",
            (entry && entry->payload_b64) ? entry->payload_b64 : "",
            entry ? entry->payload_version : default_payload_version,
            (entry && entry->fluid) ? entry->fluid : "",
            (entry && entry->construction) ? entry->construction : "");
}
