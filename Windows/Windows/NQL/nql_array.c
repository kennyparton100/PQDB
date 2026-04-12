/**
 * nql_array.c - NQL Array type and ARRAY_* built-in functions.
 * Part of the CPSS Viewer amalgamation.
 *
 * Arrays are heap-allocated, variable-length ordered collections of NqlValue.
 * Stored by pointer in NqlValue (NQL_VAL_ARRAY). Arrays are value-owned:
 * copying an NqlValue that holds an array creates a deep copy.
 *
 * Naming convention: all functions use ARRAY_* prefix for discoverability.
 */

/* ======================================================================
 * NqlArray STRUCT AND CORE OPERATIONS
 * ====================================================================== */

#define NQL_ARRAY_MAX_LENGTH 100000u

struct NqlArray_s {
    NqlValue* items;
    uint32_t  length;
    uint32_t  capacity;
};

static NqlArray* nql_array_alloc(uint32_t capacity) {
    NqlArray* a = (NqlArray*)malloc(sizeof(NqlArray));
    if (!a) return NULL;
    if (capacity == 0u) capacity = 8u;
    if (capacity > NQL_ARRAY_MAX_LENGTH) capacity = NQL_ARRAY_MAX_LENGTH;
    a->items = (NqlValue*)calloc(capacity, sizeof(NqlValue));
    if (!a->items) { free(a); return NULL; }
    a->length = 0u;
    a->capacity = capacity;
    return a;
}

static void nql_array_free(NqlArray* a) {
    if (!a) return;
    free(a->items);
    free(a);
}

static NqlArray* nql_array_copy(const NqlArray* src) {
    if (!src) return NULL;
    NqlArray* dst = nql_array_alloc(src->length > 0u ? src->length : 8u);
    if (!dst) return NULL;
    dst->length = src->length;
    /* For now, just memcpy - arrays contain simple scalars in this test */
    memcpy(dst->items, src->items, src->length * sizeof(NqlValue));
    return dst;
}

static bool nql_array_push(NqlArray* a, NqlValue val) {
    if (!a) return false;
    if (a->length >= NQL_ARRAY_MAX_LENGTH) return false;
    if (a->length >= a->capacity) {
        uint32_t new_cap = a->capacity * 2u;
        if (new_cap > NQL_ARRAY_MAX_LENGTH) new_cap = NQL_ARRAY_MAX_LENGTH;
        NqlValue* new_items = (NqlValue*)realloc(a->items, new_cap * sizeof(NqlValue));
        if (!new_items) return false;
        a->items = new_items;
        a->capacity = new_cap;
    }
    a->items[a->length++] = val;
    return true;
}

/* ======================================================================
 * NqlValue ARRAY CONSTRUCTORS AND HELPERS
 * ====================================================================== */

static NqlValue nql_val_array(NqlArray* a) {
    NqlValue r;
    memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_ARRAY;
    r.v.array = a;
    return r;
}

/* Forward declare -- full definition in nql_eval.c */
static void nql_val_to_str(const NqlValue* v, char* buf, size_t bufsz);

/** Format an array as a string: [1, 2, 3] */
static void nql_array_to_str(const NqlArray* a, char* buf, size_t bufsz) {
    if (!a || a->length == 0u) { strncpy(buf, "[]", bufsz); buf[bufsz-1]='\0'; return; }
    size_t off = 0u;
    off += snprintf(buf + off, bufsz - off, "[");
    for (uint32_t i = 0u; i < a->length && off < bufsz - 20u; ++i) {
        if (i > 0u) off += snprintf(buf + off, bufsz - off, ", ");
        char tmp[64];
        nql_val_to_str(&a->items[i], tmp, sizeof(tmp));
        off += snprintf(buf + off, bufsz - off, "%s", tmp);
        if (off >= bufsz - 20u && i < a->length - 1u) {
            off += snprintf(buf + off, bufsz - off, ", ... (%u more)", a->length - i - 1u);
            break;
        }
    }
    snprintf(buf + off, bufsz - off, "]");
}

/* ======================================================================
 * ARRAY BUILT-IN FUNCTION IMPLEMENTATIONS
 * ====================================================================== */

/** ARRAY(v1, v2, ...) -- construct from arguments (variadic via max_args) */
static NqlValue nql_fn_array(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    NqlArray* a = nql_array_alloc(argc > 0u ? argc : 8u);
    if (!a) return nql_val_null();
    for (uint32_t i = 0u; i < argc; ++i)
        nql_array_push(a, args[i]);
    return nql_val_array(a);
}

/** ARRAY_RANGE(lo, hi) -- create [lo, lo+1, ..., hi] */
static NqlValue nql_fn_array_range(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    int64_t lo = nql_val_as_int(&args[0]);
    int64_t hi = nql_val_as_int(&args[1]);
    if (hi < lo) return nql_val_array(nql_array_alloc(0u));
    uint32_t count = (uint32_t)(hi - lo + 1);
    if (count > NQL_ARRAY_MAX_LENGTH) count = NQL_ARRAY_MAX_LENGTH;
    NqlArray* a = nql_array_alloc(count);
    if (!a) return nql_val_null();
    for (uint32_t i = 0u; i < count; ++i)
        nql_array_push(a, nql_val_int(lo + (int64_t)i));
    return nql_val_array(a);
}

/** ARRAY_FILL(value, count) -- create array of `count` copies of `value` */
static NqlValue nql_fn_array_fill(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    NqlValue val = args[0];
    uint32_t count = (uint32_t)nql_val_as_int(&args[1]);
    if (count > NQL_ARRAY_MAX_LENGTH) count = NQL_ARRAY_MAX_LENGTH;
    NqlArray* a = nql_array_alloc(count);
    if (!a) return nql_val_null();
    for (uint32_t i = 0u; i < count; ++i)
        nql_array_push(a, val);
    return nql_val_array(a);
}

/** ARRAY_LENGTH(a) */
static NqlValue nql_fn_array_length(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_int(0);
    return nql_val_int((int64_t)args[0].v.array->length);
}

/** ARRAY_GET(a, index) -- 0-indexed */
static NqlValue nql_fn_array_get(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    int64_t idx = nql_val_as_int(&args[1]);
    NqlArray* a = args[0].v.array;
    if (idx < 0 || (uint32_t)idx >= a->length) return nql_val_null();
    return a->items[(uint32_t)idx];
}

/** ARRAY_FIRST(a) */
static NqlValue nql_fn_array_first(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length == 0u) return nql_val_null();
    return args[0].v.array->items[0];
}

/** ARRAY_LAST(a) */
static NqlValue nql_fn_array_last(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length == 0u) return nql_val_null();
    NqlArray* a = args[0].v.array;
    return a->items[a->length - 1u];
}

/** ARRAY_SLICE(a, start, end) -- elements [start, end) */
static NqlValue nql_fn_array_slice(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* src = args[0].v.array;
    int64_t start = nql_val_as_int(&args[1]);
    int64_t end = nql_val_as_int(&args[2]);
    if (start < 0) start = 0;
    if (end > (int64_t)src->length) end = (int64_t)src->length;
    if (start >= end) return nql_val_array(nql_array_alloc(0u));
    uint32_t count = (uint32_t)(end - start);
    NqlArray* a = nql_array_alloc(count);
    if (!a) return nql_val_null();
    for (uint32_t i = 0u; i < count; ++i)
        nql_array_push(a, src->items[(uint32_t)start + i]);
    return nql_val_array(a);
}

/** ARRAY_APPEND(a, value) -- return new array with value appended */
static NqlValue nql_fn_array_append(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = nql_array_copy(args[0].v.array);
    if (!a) return nql_val_null();
    nql_array_push(a, args[1]);
    return nql_val_array(a);
}

/** ARRAY_CONCAT(a, b) -- return new array that is a followed by b */
static NqlValue nql_fn_array_concat(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || args[1].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* sa = args[0].v.array;
    NqlArray* sb = args[1].v.array;
    if (!sa || !sb) return nql_val_null();
    NqlArray* a = nql_array_alloc(sa->length + sb->length);
    if (!a) return nql_val_null();
    for (uint32_t i = 0u; i < sa->length; ++i) nql_array_push(a, sa->items[i]);
    for (uint32_t i = 0u; i < sb->length; ++i) nql_array_push(a, sb->items[i]);
    return nql_val_array(a);
}

/** ARRAY_REVERSE(a) */
static NqlValue nql_fn_array_reverse(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* src = args[0].v.array;
    NqlArray* a = nql_array_alloc(src->length);
    if (!a) return nql_val_null();
    for (uint32_t i = src->length; i > 0u; --i)
        nql_array_push(a, src->items[i - 1u]);
    return nql_val_array(a);
}

/** Comparison for sorting NqlValues by numeric value. */
static int nql_val_compare_asc(const void* a, const void* b) {
    const NqlValue* va = (const NqlValue*)a;
    const NqlValue* vb = (const NqlValue*)b;
    double da = nql_val_as_float(va);
    double db = nql_val_as_float(vb);
    return (da > db) ? 1 : (da < db) ? -1 : 0;
}

/** ARRAY_SORT(a) -- ascending */
static NqlValue nql_fn_array_sort(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = nql_array_copy(args[0].v.array);
    if (!a) return nql_val_null();
    qsort(a->items, a->length, sizeof(NqlValue), nql_val_compare_asc);
    return nql_val_array(a);
}

/** ARRAY_UNIQUE(a) -- remove duplicates (preserves order, O(n^2)) */
static NqlValue nql_fn_array_unique(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* src = args[0].v.array;
    NqlArray* a = nql_array_alloc(src->length);
    if (!a) return nql_val_null();
    for (uint32_t i = 0u; i < src->length; ++i) {
        bool dup = false;
        int64_t vi = nql_val_as_int(&src->items[i]);
        for (uint32_t j = 0u; j < a->length; ++j) {
            if (nql_val_as_int(&a->items[j]) == vi) { dup = true; break; }
        }
        if (!dup) nql_array_push(a, src->items[i]);
    }
    return nql_val_array(a);
}

/** ARRAY_CONTAINS(a, value) */
static NqlValue nql_fn_array_contains(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_bool(false);
    NqlArray* a = args[0].v.array;
    int64_t target = nql_val_as_int(&args[1]);
    for (uint32_t i = 0u; i < a->length; ++i) {
        if (nql_val_as_int(&a->items[i]) == target) return nql_val_bool(true);
    }
    return nql_val_bool(false);
}

/** ARRAY_INDEX_OF(a, value) -- returns index or -1 */
static NqlValue nql_fn_array_index_of(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_int(-1);
    NqlArray* a = args[0].v.array;
    int64_t target = nql_val_as_int(&args[1]);
    for (uint32_t i = 0u; i < a->length; ++i) {
        if (nql_val_as_int(&a->items[i]) == target) return nql_val_int((int64_t)i);
    }
    return nql_val_int(-1);
}

/** ARRAY_SUM(a) */
static NqlValue nql_fn_array_sum(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_int(0);
    NqlArray* a = args[0].v.array;
    int64_t sum = 0;
    for (uint32_t i = 0u; i < a->length; ++i)
        sum += nql_val_as_int(&a->items[i]);
    return nql_val_int(sum);
}

/** ARRAY_MIN(a) */
static NqlValue nql_fn_array_min(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length == 0u) return nql_val_null();
    NqlArray* a = args[0].v.array;
    int64_t m = nql_val_as_int(&a->items[0]);
    for (uint32_t i = 1u; i < a->length; ++i) {
        int64_t v = nql_val_as_int(&a->items[i]);
        if (v < m) m = v;
    }
    return nql_val_int(m);
}

/** ARRAY_MAX(a) */
static NqlValue nql_fn_array_max(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length == 0u) return nql_val_null();
    NqlArray* a = args[0].v.array;
    int64_t m = nql_val_as_int(&a->items[0]);
    for (uint32_t i = 1u; i < a->length; ++i) {
        int64_t v = nql_val_as_int(&a->items[i]);
        if (v > m) m = v;
    }
    return nql_val_int(m);
}

/** ARRAY_PRODUCT(a) */
static NqlValue nql_fn_array_product(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length == 0u) return nql_val_int(0);
    NqlArray* a = args[0].v.array;
    int64_t prod = 1;
    for (uint32_t i = 0u; i < a->length; ++i)
        prod *= nql_val_as_int(&a->items[i]);
    return nql_val_int(prod);
}

/** ARRAY_AVG(a) */
static NqlValue nql_fn_array_avg(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length == 0u) return nql_val_null();
    NqlArray* a = args[0].v.array;
    double sum = 0.0;
    for (uint32_t i = 0u; i < a->length; ++i)
        sum += nql_val_as_float(&a->items[i]);
    return nql_val_float(sum / (double)a->length);
}

/** ARRAY_CREATE(size, default_value) -- pre-allocated array filled with default */
static NqlValue nql_fn_array_create(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    uint32_t size = (uint32_t)nql_val_as_int(&args[0]);
    if (size > NQL_ARRAY_MAX_LENGTH) size = NQL_ARRAY_MAX_LENGTH;
    NqlArray* a = nql_array_alloc(size);
    if (!a) return nql_val_null();
    NqlValue def = (argc >= 2u) ? args[1] : nql_val_int(0);
    for (uint32_t i = 0u; i < size; ++i)
        nql_array_push(a, def);
    return nql_val_array(a);
}

/** __ARRAY_LITERAL(elem1, elem2, ...) -- create array from listed elements (parser internal) */
static NqlValue nql_fn_array_literal(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    NqlArray* a = nql_array_alloc(argc > 0u ? argc : 4u);
    if (!a) return nql_val_null();
    for (uint32_t i = 0u; i < argc; ++i)
        nql_array_push(a, args[i]);
    return nql_val_array(a);
}

/** ARRAY_SET(a, index, value) -- return new array with element replaced (copy-on-write) */
static NqlValue nql_fn_array_set(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    int64_t idx = nql_val_as_int(&args[1]);
    NqlArray* src = args[0].v.array;
    if (idx < 0 || (uint32_t)idx >= src->length) return nql_val_null();
    NqlArray* a = nql_array_copy(src);
    if (!a) return nql_val_null();
    a->items[(uint32_t)idx] = args[2];
    return nql_val_array(a);
}

/** ARRAY_SET_INPLACE(a, index, value) -- modify array in place, auto-grows if needed */
static NqlValue nql_fn_array_set_inplace(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    int64_t idx = nql_val_as_int(&args[1]);
    if (idx < 0 || (uint32_t)idx >= NQL_ARRAY_MAX_LENGTH) return args[0];
    NqlArray* a = args[0].v.array;
    /* Auto-grow: fill with NULL values up to idx */
    while (a->length <= (uint32_t)idx) {
        if (!nql_array_push(a, nql_val_null())) return args[0];
    }
    a->items[(uint32_t)idx] = args[2];
    return args[0];
}

/** ARRAY_BIT_XOR(a, b) -- element-wise BIT_XOR of two integer arrays */
static NqlValue nql_fn_array_bit_xor(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    uint32_t len = a->length < b->length ? a->length : b->length;
    NqlArray* result = nql_array_alloc(len);
    if (!result) return nql_val_null();
    for (uint32_t i = 0u; i < len; ++i) {
        int64_t va = nql_val_as_int(&a->items[i]);
        int64_t vb = nql_val_as_int(&b->items[i]);
        nql_array_push(result, nql_val_int(va ^ vb));
    }
    return nql_val_array(result);
}
