/**
 * nql_vector.c - NQL Vector type and functions.
 * Part of the CPSS Viewer amalgamation.
 *
 * Dedicated vector type with dimension tracking, stored as NQL_VAL_VECTOR.
 */

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * NqlVector STRUCT AND CORE OPERATIONS
 * ====================================================================== */

#define NQL_VECTOR_MAX_DIM 1024

struct NqlVector_s {
    double*  data;
    uint32_t dim;
};

static NqlVector* nql_vector_alloc(uint32_t dim) {
    if (dim == 0 || dim > NQL_VECTOR_MAX_DIM) return NULL;
    NqlVector* v = (NqlVector*)malloc(sizeof(NqlVector));
    if (!v) return NULL;
    v->data = (double*)calloc(dim, sizeof(double));
    if (!v->data) { free(v); return NULL; }
    v->dim = dim;
    return v;
}

static NqlVector* nql_vector_copy(const NqlVector* src) {
    if (!src) return NULL;
    NqlVector* v = nql_vector_alloc(src->dim);
    if (!v) return NULL;
    memcpy(v->data, src->data, src->dim * sizeof(double));
    return v;
}

static void nql_vector_free(NqlVector* v) {
    if (!v) return;
    free(v->data);
    free(v);
}

static NqlValue nql_val_vector(NqlVector* v) {
    NqlValue val;
    memset(&val, 0, sizeof(val));
    val.type = NQL_VAL_VECTOR;
    val.v.vector = v;
    return val;
}

/** Extract vector from NqlValue (promotes arrays). */
static NqlVector* nql_val_to_vector(const NqlValue* v) {
    if (v->type == NQL_VAL_VECTOR && v->v.vector) return v->v.vector;
    if (v->type == NQL_VAL_ARRAY && v->v.array) {
        NqlArray* arr = v->v.array;
        NqlVector* vec = nql_vector_alloc(arr->length);
        if (!vec) return NULL;
        for (uint32_t i = 0; i < arr->length; i++)
            vec->data[i] = nql_val_as_float(&arr->items[i]);
        return vec;
    }
    return NULL;
}

/* ======================================================================
 * VECTOR NQL FUNCTIONS
 * ====================================================================== */

/** VECTOR(x, y, ...) -- create vector from components (up to 8 args) */
static NqlValue nql_fn_vector(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc == 0 || argc > 8) return nql_val_null();
    NQL_NULL_GUARD_N(args, argc);
    NqlVector* v = nql_vector_alloc(argc);
    if (!v) return nql_val_null();
    for (uint32_t i = 0; i < argc; i++)
        v->data[i] = nql_val_as_float(&args[i]);
    return nql_val_vector(v);
}

/** VECTOR_DIM(v) */
static NqlValue nql_fn_vector_dim(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* v = nql_val_to_vector(&args[0]);
    if (!v) return nql_val_null();
    int64_t dim = (int64_t)v->dim;
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v);
    return nql_val_int(dim);
}

/** VECTOR_GET(v, i) -- 0-indexed */
static NqlValue nql_fn_vector_get(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* v = nql_val_to_vector(&args[0]);
    if (!v) return nql_val_null();
    int64_t idx = nql_val_as_int(&args[1]);
    double val = (idx >= 0 && (uint32_t)idx < v->dim) ? v->data[idx] : 0.0;
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v);
    return nql_val_float(val);
}

/** VECTOR_ADD(a, b) -- component-wise addition */
static NqlValue nql_fn_vector_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* a = nql_val_to_vector(&args[0]);
    NqlVector* b = nql_val_to_vector(&args[1]);
    if (!a || !b || a->dim != b->dim) {
        if (a && args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
        if (b && args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
        return nql_val_null();
    }
    NqlVector* r = nql_vector_alloc(a->dim);
    if (!r) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a); if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b); return nql_val_null(); }
    for (uint32_t i = 0; i < a->dim; i++) r->data[i] = a->data[i] + b->data[i];
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
    if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
    return nql_val_vector(r);
}

/** VECTOR_SUB(a, b) */
static NqlValue nql_fn_vector_sub(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* a = nql_val_to_vector(&args[0]);
    NqlVector* b = nql_val_to_vector(&args[1]);
    if (!a || !b || a->dim != b->dim) {
        if (a && args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
        if (b && args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
        return nql_val_null();
    }
    NqlVector* r = nql_vector_alloc(a->dim);
    if (!r) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a); if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b); return nql_val_null(); }
    for (uint32_t i = 0; i < a->dim; i++) r->data[i] = a->data[i] - b->data[i];
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
    if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
    return nql_val_vector(r);
}

/** VECTOR_SCALE(v, scalar) */
static NqlValue nql_fn_vector_scale(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* v = nql_val_to_vector(&args[0]);
    if (!v) return nql_val_null();
    double s = nql_val_as_float(&args[1]);
    NqlVector* r = nql_vector_alloc(v->dim);
    if (!r) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v); return nql_val_null(); }
    for (uint32_t i = 0; i < v->dim; i++) r->data[i] = v->data[i] * s;
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v);
    return nql_val_vector(r);
}

/** VECTOR_NEGATE(v) */
static NqlValue nql_fn_vector_negate(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* v = nql_val_to_vector(&args[0]);
    if (!v) return nql_val_null();
    NqlVector* r = nql_vector_alloc(v->dim);
    if (!r) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v); return nql_val_null(); }
    for (uint32_t i = 0; i < v->dim; i++) r->data[i] = -v->data[i];
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v);
    return nql_val_vector(r);
}

/** VECTOR_DOT(a, b) -- dot product */
static NqlValue nql_fn_vector_dot(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* a = nql_val_to_vector(&args[0]);
    NqlVector* b = nql_val_to_vector(&args[1]);
    if (!a || !b || a->dim != b->dim) {
        if (a && args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
        if (b && args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
        return nql_val_null();
    }
    double dot = 0.0;
    for (uint32_t i = 0; i < a->dim; i++) dot += a->data[i] * b->data[i];
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
    if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
    return nql_val_float(dot);
}

/** VECTOR_CROSS(a, b) -- 3D cross product */
static NqlValue nql_fn_vector_cross(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* a = nql_val_to_vector(&args[0]);
    NqlVector* b = nql_val_to_vector(&args[1]);
    if (!a || !b || a->dim != 3 || b->dim != 3) {
        if (a && args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
        if (b && args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
        return nql_val_null();
    }
    NqlVector* r = nql_vector_alloc(3);
    if (!r) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a); if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b); return nql_val_null(); }
    r->data[0] = a->data[1] * b->data[2] - a->data[2] * b->data[1];
    r->data[1] = a->data[2] * b->data[0] - a->data[0] * b->data[2];
    r->data[2] = a->data[0] * b->data[1] - a->data[1] * b->data[0];
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
    if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
    return nql_val_vector(r);
}

/** VECTOR_NORM(v) -- Euclidean L2 norm */
static NqlValue nql_fn_vector_norm(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* v = nql_val_to_vector(&args[0]);
    if (!v) return nql_val_null();
    double sum = 0.0;
    for (uint32_t i = 0; i < v->dim; i++) sum += v->data[i] * v->data[i];
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v);
    return nql_val_float(sqrt(sum));
}

/** VECTOR_NORMALIZE(v) -- unit vector */
static NqlValue nql_fn_vector_normalize(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* v = nql_val_to_vector(&args[0]);
    if (!v) return nql_val_null();
    double sum = 0.0;
    for (uint32_t i = 0; i < v->dim; i++) sum += v->data[i] * v->data[i];
    double mag = sqrt(sum);
    if (mag < 1e-15) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v); return nql_val_null(); }
    NqlVector* r = nql_vector_alloc(v->dim);
    if (!r) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v); return nql_val_null(); }
    for (uint32_t i = 0; i < v->dim; i++) r->data[i] = v->data[i] / mag;
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v);
    return nql_val_vector(r);
}

/** VECTOR_ANGLE(a, b) -- angle in radians between two vectors */
static NqlValue nql_fn_vector_angle(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* a = nql_val_to_vector(&args[0]);
    NqlVector* b = nql_val_to_vector(&args[1]);
    if (!a || !b || a->dim != b->dim) {
        if (a && args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
        if (b && args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
        return nql_val_null();
    }
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (uint32_t i = 0; i < a->dim; i++) {
        dot += a->data[i] * b->data[i];
        na += a->data[i] * a->data[i];
        nb += b->data[i] * b->data[i];
    }
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
    if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
    double denom = sqrt(na) * sqrt(nb);
    if (denom < 1e-15) return nql_val_null();
    double cosine = dot / denom;
    if (cosine > 1.0) cosine = 1.0;
    if (cosine < -1.0) cosine = -1.0;
    return nql_val_float(acos(cosine));
}

/** VECTOR_PROJECT(a, onto_b) -- projection of a onto b */
static NqlValue nql_fn_vector_project(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* a = nql_val_to_vector(&args[0]);
    NqlVector* b = nql_val_to_vector(&args[1]);
    if (!a || !b || a->dim != b->dim) {
        if (a && args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
        if (b && args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
        return nql_val_null();
    }
    double dot_ab = 0.0, dot_bb = 0.0;
    for (uint32_t i = 0; i < a->dim; i++) {
        dot_ab += a->data[i] * b->data[i];
        dot_bb += b->data[i] * b->data[i];
    }
    if (dot_bb < 1e-15) {
        if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
        if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
        return nql_val_null();
    }
    double scalar = dot_ab / dot_bb;
    NqlVector* r = nql_vector_alloc(b->dim);
    if (!r) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a); if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b); return nql_val_null(); }
    for (uint32_t i = 0; i < b->dim; i++) r->data[i] = b->data[i] * scalar;
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
    if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
    return nql_val_vector(r);
}

/** VECTOR_FROM_ARRAY(arr) -- convert array to vector */
static NqlValue nql_fn_vector_from_array(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    if (arr->length == 0 || arr->length > NQL_VECTOR_MAX_DIM) return nql_val_null();
    NqlVector* v = nql_vector_alloc(arr->length);
    if (!v) return nql_val_null();
    for (uint32_t i = 0; i < arr->length; i++)
        v->data[i] = nql_val_as_float(&arr->items[i]);
    return nql_val_vector(v);
}

/** VECTOR_TO_ARRAY(v) -- convert vector to array */
static NqlValue nql_fn_vector_to_array(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* v = nql_val_to_vector(&args[0]);
    if (!v) return nql_val_null();
    NqlArray* arr = nql_array_alloc(v->dim);
    if (!arr) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v); return nql_val_null(); }
    for (uint32_t i = 0; i < v->dim; i++)
        nql_array_push(arr, nql_val_float(v->data[i]));
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(v);
    return nql_val_array(arr);
}

/** VECTOR_DISTANCE(a, b) -- Euclidean distance */
static NqlValue nql_fn_vector_distance(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* a = nql_val_to_vector(&args[0]);
    NqlVector* b = nql_val_to_vector(&args[1]);
    if (!a || !b || a->dim != b->dim) {
        if (a && args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
        if (b && args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
        return nql_val_null();
    }
    double sum = 0.0;
    for (uint32_t i = 0; i < a->dim; i++) {
        double d = a->data[i] - b->data[i];
        sum += d * d;
    }
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
    if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
    return nql_val_float(sqrt(sum));
}

/** VECTOR_LERP(a, b, t) -- linear interpolation */
static NqlValue nql_fn_vector_lerp(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlVector* a = nql_val_to_vector(&args[0]);
    NqlVector* b = nql_val_to_vector(&args[1]);
    double t = nql_val_as_float(&args[2]);
    if (!a || !b || a->dim != b->dim) {
        if (a && args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
        if (b && args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
        return nql_val_null();
    }
    NqlVector* r = nql_vector_alloc(a->dim);
    if (!r) { if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a); if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b); return nql_val_null(); }
    for (uint32_t i = 0; i < a->dim; i++)
        r->data[i] = a->data[i] * (1.0 - t) + b->data[i] * t;
    if (args[0].type != NQL_VAL_VECTOR) nql_vector_free(a);
    if (args[1].type != NQL_VAL_VECTOR) nql_vector_free(b);
    return nql_val_vector(r);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_vector_register_functions(void) {
    nql_register_func("VECTOR",           nql_fn_vector,           1, 8, "Create vector from components");
    nql_register_func("VECTOR_DIM",       nql_fn_vector_dim,       1, 1, "Dimension of vector");
    nql_register_func("VECTOR_GET",       nql_fn_vector_get,       2, 2, "Get component at index (0-indexed)");
    nql_register_func("VECTOR_ADD",       nql_fn_vector_add,       2, 2, "Component-wise addition");
    nql_register_func("VECTOR_SUB",       nql_fn_vector_sub,       2, 2, "Component-wise subtraction");
    nql_register_func("VECTOR_SCALE",     nql_fn_vector_scale,     2, 2, "Scalar multiplication");
    nql_register_func("VECTOR_NEGATE",    nql_fn_vector_negate,    1, 1, "Negate all components");
    nql_register_func("VECTOR_DOT",       nql_fn_vector_dot,       2, 2, "Dot product");
    nql_register_func("VECTOR_CROSS",     nql_fn_vector_cross,     2, 2, "3D cross product");
    nql_register_func("VECTOR_NORM",      nql_fn_vector_norm,      1, 1, "Euclidean L2 norm");
    nql_register_func("VECTOR_NORMALIZE", nql_fn_vector_normalize, 1, 1, "Unit vector");
    nql_register_func("VECTOR_ANGLE",     nql_fn_vector_angle,     2, 2, "Angle between vectors (radians)");
    nql_register_func("VECTOR_PROJECT",   nql_fn_vector_project,   2, 2, "Projection of a onto b");
    nql_register_func("VECTOR_FROM_ARRAY",nql_fn_vector_from_array,1, 1, "Convert array to vector");
    nql_register_func("VECTOR_TO_ARRAY",  nql_fn_vector_to_array,  1, 1, "Convert vector to array");
    nql_register_func("VECTOR_DISTANCE",  nql_fn_vector_distance,  2, 2, "Euclidean distance between vectors");
    nql_register_func("VECTOR_LERP",      nql_fn_vector_lerp,      3, 3, "Linear interpolation between vectors");
}
