/**
 * nql_string.c - NQL String operations module.
 * Part of the CPSS Viewer amalgamation.
 *
 * Provides ~15 string manipulation functions for NQL.
 */

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * STRING FUNCTIONS
 * ====================================================================== */

/** STRING_LENGTH(s) -- number of characters */
static NqlValue nql_fn_string_length(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_int(0);
    return nql_val_int((int64_t)strlen(args[0].v.sval));
}

/** SUBSTRING(s, start, len) -- 0-indexed substring */
static NqlValue nql_fn_substring(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* s = args[0].v.sval;
    int64_t slen = (int64_t)strlen(s);
    int64_t start = nql_val_as_int(&args[1]);
    int64_t len = (argc >= 3) ? nql_val_as_int(&args[2]) : (slen - start);
    if (start < 0) start = 0;
    if (start >= slen) return nql_val_str("");
    if (len < 0) len = 0;
    if (start + len > slen) len = slen - start;
    if (len > 255) len = 255;
    char buf[256];
    memcpy(buf, s + start, (size_t)len);
    buf[len] = '\0';
    return nql_val_str(buf);
}

/** STRING_SPLIT(s, delim) -- split into array of strings */
static NqlValue nql_fn_string_split(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_STRING || args[1].type != NQL_VAL_STRING) return nql_val_null();
    const char* s = args[0].v.sval;
    const char* delim = args[1].v.sval;
    size_t dlen = strlen(delim);
    if (dlen == 0) return nql_val_null();
    NqlArray* result = nql_array_alloc(16);
    if (!result) return nql_val_null();
    const char* p = s;
    while (*p) {
        const char* found = strstr(p, delim);
        if (!found) {
            nql_array_push(result, nql_val_str(p));
            break;
        }
        size_t chunk = (size_t)(found - p);
        char buf[256];
        if (chunk > 255) chunk = 255;
        memcpy(buf, p, chunk);
        buf[chunk] = '\0';
        nql_array_push(result, nql_val_str(buf));
        p = found + dlen;
        if (!*p) nql_array_push(result, nql_val_str(""));
    }
    return nql_val_array(result);
}

/** STRING_JOIN(array, delim) -- join array of strings */
static NqlValue nql_fn_string_join(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    const char* delim = args[1].v.sval;
    size_t dlen = strlen(delim);
    char buf[256];
    size_t off = 0;
    for (uint32_t i = 0; i < arr->length && off < 250; i++) {
        if (i > 0 && off + dlen < 250) { memcpy(buf + off, delim, dlen); off += dlen; }
        char tmp[256];
        nql_val_to_str(&arr->items[i], tmp, sizeof(tmp));
        size_t tlen = strlen(tmp);
        if (off + tlen > 254) tlen = 254 - off;
        memcpy(buf + off, tmp, tlen);
        off += tlen;
    }
    buf[off] = '\0';
    return nql_val_str(buf);
}

/** STRING_REPLACE(s, old, new) -- replace all occurrences */
static NqlValue nql_fn_string_replace(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING || args[1].type != NQL_VAL_STRING || args[2].type != NQL_VAL_STRING)
        return nql_val_null();
    const char* s = args[0].v.sval;
    const char* old_str = args[1].v.sval;
    const char* new_str = args[2].v.sval;
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    if (old_len == 0) return args[0];
    char buf[256];
    size_t off = 0;
    const char* p = s;
    while (*p && off < 250) {
        const char* found = strstr(p, old_str);
        if (!found) {
            size_t rem = strlen(p);
            if (rem > 254 - off) rem = 254 - off;
            memcpy(buf + off, p, rem);
            off += rem;
            break;
        }
        size_t chunk = (size_t)(found - p);
        if (chunk > 254 - off) chunk = 254 - off;
        memcpy(buf + off, p, chunk);
        off += chunk;
        if (off + new_len > 254) break;
        memcpy(buf + off, new_str, new_len);
        off += new_len;
        p = found + old_len;
    }
    buf[off] = '\0';
    return nql_val_str(buf);
}

/** STRING_UPPER(s) */
static NqlValue nql_fn_string_upper(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    char buf[256];
    const char* s = args[0].v.sval;
    size_t i;
    for (i = 0; s[i] && i < 255; i++)
        buf[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    buf[i] = '\0';
    return nql_val_str(buf);
}

/** STRING_LOWER(s) */
static NqlValue nql_fn_string_lower(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    char buf[256];
    const char* s = args[0].v.sval;
    size_t i;
    for (i = 0; s[i] && i < 255; i++)
        buf[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i];
    buf[i] = '\0';
    return nql_val_str(buf);
}

/** STRING_CONTAINS(s, sub) */
static NqlValue nql_fn_string_contains(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_STRING || args[1].type != NQL_VAL_STRING) return nql_val_bool(false);
    return nql_val_bool(strstr(args[0].v.sval, args[1].v.sval) != NULL);
}

/** STRING_STARTS_WITH(s, prefix) */
static NqlValue nql_fn_string_starts_with(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING || args[1].type != NQL_VAL_STRING) return nql_val_bool(false);
    size_t plen = strlen(args[1].v.sval);
    return nql_val_bool(strncmp(args[0].v.sval, args[1].v.sval, plen) == 0);
}

/** STRING_ENDS_WITH(s, suffix) */
static NqlValue nql_fn_string_ends_with(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING || args[1].type != NQL_VAL_STRING) return nql_val_bool(false);
    size_t slen = strlen(args[0].v.sval);
    size_t plen = strlen(args[1].v.sval);
    if (plen > slen) return nql_val_bool(false);
    return nql_val_bool(strcmp(args[0].v.sval + slen - plen, args[1].v.sval) == 0);
}

/** STRING_INDEX_OF(s, sub) -- 0-indexed, -1 if not found */
static NqlValue nql_fn_string_index_of(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING || args[1].type != NQL_VAL_STRING) return nql_val_int(-1);
    const char* found = strstr(args[0].v.sval, args[1].v.sval);
    if (!found) return nql_val_int(-1);
    return nql_val_int((int64_t)(found - args[0].v.sval));
}

/** STRING_TRIM(s) -- remove leading/trailing whitespace */
static NqlValue nql_fn_string_trim(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* s = args[0].v.sval;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) len--;
    char buf[256];
    if (len > 255) len = 255;
    memcpy(buf, s, len);
    buf[len] = '\0';
    return nql_val_str(buf);
}

/** STRING_REPEAT(s, n) */
static NqlValue nql_fn_string_repeat(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    int64_t n = nql_val_as_int(&args[1]);
    if (n <= 0) return nql_val_str("");
    const char* s = args[0].v.sval;
    size_t slen = strlen(s);
    char buf[256];
    size_t off = 0;
    for (int64_t i = 0; i < n && off + slen < 255; i++) {
        memcpy(buf + off, s, slen);
        off += slen;
    }
    buf[off] = '\0';
    return nql_val_str(buf);
}

/** STRING_REVERSE(s) */
static NqlValue nql_fn_string_reverse(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* s = args[0].v.sval;
    size_t len = strlen(s);
    char buf[256];
    if (len > 255) len = 255;
    for (size_t i = 0; i < len; i++) buf[i] = s[len - 1 - i];
    buf[len] = '\0';
    return nql_val_str(buf);
}

/** STRING_CHAR_AT(s, i) -- character at index as string */
static NqlValue nql_fn_string_char_at(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    int64_t idx = nql_val_as_int(&args[1]);
    size_t len = strlen(args[0].v.sval);
    if (idx < 0 || (size_t)idx >= len) return nql_val_null();
    char buf[2] = { args[0].v.sval[idx], '\0' };
    return nql_val_str(buf);
}

/** FORMAT(pattern, args...) -- simple %d/%f/%s substitution */
static NqlValue nql_fn_format(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc < 1 || args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* pat = args[0].v.sval;
    char buf[256];
    size_t off = 0;
    uint32_t ai = 1;
    for (const char* p = pat; *p && off < 250; p++) {
        if (*p == '%' && ai < argc) {
            char tmp[64];
            nql_val_to_str(&args[ai++], tmp, sizeof(tmp));
            size_t tlen = strlen(tmp);
            if (off + tlen > 254) tlen = 254 - off;
            memcpy(buf + off, tmp, tlen);
            off += tlen;
        } else {
            buf[off++] = *p;
        }
    }
    buf[off] = '\0';
    return nql_val_str(buf);
}

/** TYPEOF(val) -- returns string name of value type */
static NqlValue nql_fn_typeof(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    static const char* type_names[] = {
        "NULL", "INT", "FLOAT", "BOOL", "STRING", "U128", "BIGNUM", "ARRAY", "POLY",
        "QS_FACTOR_BASE", "QS_SIEVE", "QS_TRIAL_RESULT", "QS_RELATION_SET",
        "QS_GF2_MATRIX", "QS_DEPENDENCY",
        "COMPLEX", "MATRIX", "SPARSE_MATRIX", "VECTOR", "RATIONAL",
        "SYMBOLIC", "INTERVAL", "GRAPH", "GROUP", "FIELD", "DISTRIBUTION", "SERIES"
    };
    int idx = (int)args[0].type;
    if (idx >= 0 && idx < (int)(sizeof(type_names)/sizeof(type_names[0])))
        return nql_val_str(type_names[idx]);
    return nql_val_str("UNKNOWN");
}

/** NQL_FUNC_COUNT() -- returns number of registered built-in functions */
static NqlValue nql_fn_func_count(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)args; (void)argc; (void)ctx;
    return nql_val_int((int64_t)g_nql_func_count);
}

/* ======================================================================
 * HIGHER-ORDER ARRAY FUNCTIONS
 *
 * These use built-in function lookup by name. The function name is passed
 * as a string argument. Built-in functions are called directly; user-
 * defined functions are not accessible here (they need the eval context).
 * ====================================================================== */

/** Helper: invoke a built-in function by name with given args */
static NqlValue nql_call_by_name(const char* name, const NqlValue* args, uint32_t argc, void* db_ctx) {
    const NqlFuncEntry* fe = nql_func_lookup(name);
    if (!fe) return nql_val_null();
    if (argc < fe->min_args || argc > fe->max_args) return nql_val_null();
    return fe->fn(args, argc, db_ctx);
}

/** ARRAY_MAP(array, func_name) -- apply 1-arg function to each element */
static NqlValue nql_fn_array_map(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    NqlArray* src = args[0].v.array;
    const char* fname = args[1].v.sval;
    NqlArray* result = nql_array_alloc(src->length);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < src->length; i++) {
        NqlValue r = nql_call_by_name(fname, &src->items[i], 1, ctx);
        nql_array_push(result, r);
    }
    return nql_val_array(result);
}

/** ARRAY_FILTER(array, func_name) -- keep elements where func returns truthy */
static NqlValue nql_fn_array_filter(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    NqlArray* src = args[0].v.array;
    const char* fname = args[1].v.sval;
    NqlArray* result = nql_array_alloc(src->length);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < src->length; i++) {
        NqlValue test = nql_call_by_name(fname, &src->items[i], 1, ctx);
        if (nql_val_is_truthy(&test))
            nql_array_push(result, src->items[i]);
    }
    return nql_val_array(result);
}

/** ARRAY_REDUCE(array, func_name, initial) -- fold left with 2-arg function */
static NqlValue nql_fn_array_reduce(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    NqlArray* src = args[0].v.array;
    const char* fname = args[1].v.sval;
    NqlValue acc = args[2];
    for (uint32_t i = 0; i < src->length; i++) {
        NqlValue pair[2] = { acc, src->items[i] };
        acc = nql_call_by_name(fname, pair, 2, ctx);
    }
    return acc;
}

/** ARRAY_SORT_BY(array, func_name) -- sort by key function (ascending) */
static const char* g_nql_sort_fname;
static void* g_nql_sort_ctx;
static int nql_sort_by_cmp(const void* a, const void* b) {
    NqlValue ka = nql_call_by_name(g_nql_sort_fname, (const NqlValue*)a, 1, g_nql_sort_ctx);
    NqlValue kb = nql_call_by_name(g_nql_sort_fname, (const NqlValue*)b, 1, g_nql_sort_ctx);
    double da = nql_val_as_float(&ka);
    double db = nql_val_as_float(&kb);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}
static NqlValue nql_fn_array_sort_by(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    NqlArray* src = args[0].v.array;
    NqlArray* result = nql_array_copy(src);
    if (!result) return nql_val_null();
    g_nql_sort_fname = args[1].v.sval;
    g_nql_sort_ctx = ctx;
    qsort(result->items, result->length, sizeof(NqlValue), nql_sort_by_cmp);
    return nql_val_array(result);
}

/** ARRAY_ZIP(a, b) -- merge two arrays into array of [a_i, b_i] pairs */
static NqlValue nql_fn_array_zip(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc; (void)ctx;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    uint32_t len = a->length < b->length ? a->length : b->length;
    NqlArray* result = nql_array_alloc(len);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < len; i++) {
        NqlArray* pair = nql_array_alloc(2);
        if (!pair) return nql_val_array(result);
        nql_array_push(pair, a->items[i]);
        nql_array_push(pair, b->items[i]);
        nql_array_push(result, nql_val_array(pair));
    }
    return nql_val_array(result);
}

/** ARRAY_FLAT_MAP(array, func_name) -- map then flatten one level */
static NqlValue nql_fn_array_flat_map(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    NqlArray* src = args[0].v.array;
    const char* fname = args[1].v.sval;
    NqlArray* result = nql_array_alloc(src->length * 2);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < src->length; i++) {
        NqlValue r = nql_call_by_name(fname, &src->items[i], 1, ctx);
        if (r.type == NQL_VAL_ARRAY && r.v.array) {
            for (uint32_t j = 0; j < r.v.array->length; j++)
                nql_array_push(result, r.v.array->items[j]);
        } else {
            nql_array_push(result, r);
        }
    }
    return nql_val_array(result);
}

/** ARRAY_ANY(array, func_name) -- true if any element satisfies func */
static NqlValue nql_fn_array_any(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_bool(false);
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    NqlArray* src = args[0].v.array;
    const char* fname = args[1].v.sval;
    for (uint32_t i = 0; i < src->length; i++) {
        NqlValue test = nql_call_by_name(fname, &src->items[i], 1, ctx);
        if (nql_val_is_truthy(&test)) return nql_val_bool(true);
    }
    return nql_val_bool(false);
}

/** ARRAY_ALL(array, func_name) -- true if all elements satisfy func */
static NqlValue nql_fn_array_all(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_bool(true);
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    NqlArray* src = args[0].v.array;
    const char* fname = args[1].v.sval;
    for (uint32_t i = 0; i < src->length; i++) {
        NqlValue test = nql_call_by_name(fname, &src->items[i], 1, ctx);
        if (!nql_val_is_truthy(&test)) return nql_val_bool(false);
    }
    return nql_val_bool(true);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_string_register_functions(void) {
    /* String operations */
    nql_register_func("STRING_LENGTH",     nql_fn_string_length,     1, 1, "Number of characters in string");
    nql_register_func("SUBSTRING",         nql_fn_substring,         2, 3, "Substring(s, start [, len])");
    nql_register_func("STRING_SPLIT",      nql_fn_string_split,      2, 2, "Split string by delimiter into array");
    nql_register_func("STRING_JOIN",       nql_fn_string_join,       2, 2, "Join array of strings with delimiter");
    nql_register_func("STRING_REPLACE",    nql_fn_string_replace,    3, 3, "Replace all occurrences in string");
    nql_register_func("STRING_UPPER",      nql_fn_string_upper,      1, 1, "Convert to uppercase");
    nql_register_func("STRING_LOWER",      nql_fn_string_lower,      1, 1, "Convert to lowercase");
    nql_register_func("STRING_CONTAINS",   nql_fn_string_contains,   2, 2, "Test if string contains substring");
    nql_register_func("STRING_STARTS_WITH",nql_fn_string_starts_with,2, 2, "Test if string starts with prefix");
    nql_register_func("STRING_ENDS_WITH",  nql_fn_string_ends_with,  2, 2, "Test if string ends with suffix");
    nql_register_func("STRING_INDEX_OF",   nql_fn_string_index_of,   2, 2, "Find index of substring (-1 if not found)");
    nql_register_func("STRING_TRIM",       nql_fn_string_trim,       1, 1, "Remove leading/trailing whitespace");
    nql_register_func("STRING_REPEAT",     nql_fn_string_repeat,     2, 2, "Repeat string n times");
    nql_register_func("STRING_REVERSE",    nql_fn_string_reverse,    1, 1, "Reverse characters in string");
    nql_register_func("STRING_CHAR_AT",    nql_fn_string_char_at,    2, 2, "Character at index (0-indexed)");
    nql_register_func("FORMAT",            nql_fn_format,            1, 8, "Format string with % substitution");

    /* Utility */
    nql_register_func("TYPEOF",            nql_fn_typeof,            1, 1, "Type name of value as string");
    nql_register_func("NQL_FUNC_COUNT",    nql_fn_func_count,        0, 0, "Number of registered built-in functions");

    /* Higher-order array functions */
    nql_register_func("ARRAY_MAP",         nql_fn_array_map,         2, 2, "Apply function to each element");
    nql_register_func("ARRAY_FILTER",      nql_fn_array_filter,      2, 2, "Keep elements where function returns true");
    nql_register_func("ARRAY_REDUCE",      nql_fn_array_reduce,      3, 3, "Fold left with 2-arg function");
    nql_register_func("ARRAY_SORT_BY",     nql_fn_array_sort_by,     2, 2, "Sort array by key function");
    nql_register_func("ARRAY_ZIP",         nql_fn_array_zip,         2, 2, "Merge two arrays into pairs");
    nql_register_func("ARRAY_FLAT_MAP",    nql_fn_array_flat_map,    2, 2, "Map then flatten one level");
    nql_register_func("ARRAY_ANY",         nql_fn_array_any,         2, 2, "True if any element satisfies function");
    nql_register_func("ARRAY_ALL",         nql_fn_array_all,         2, 2, "True if all elements satisfy function");
}
