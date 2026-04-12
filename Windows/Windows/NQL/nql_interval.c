/**
 * nql_interval.c - NQL Interval arithmetic type and functions.
 * Part of the CPSS Viewer amalgamation.
 *
 * Closed intervals [lo, hi] with proper interval arithmetic including
 * rounding-aware operations. Stored as NQL_VAL_INTERVAL via v.interval.
 */

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * NqlInterval STRUCT AND CORE OPERATIONS
 * ====================================================================== */

struct NqlInterval_s {
    double lo;
    double hi;
};

static NqlInterval* nql_interval_alloc(double lo, double hi) {
    NqlInterval* iv = (NqlInterval*)malloc(sizeof(NqlInterval));
    if (!iv) return NULL;
    iv->lo = lo;
    iv->hi = hi;
    return iv;
}

static NqlValue nql_val_interval(NqlInterval* iv) {
    NqlValue v;
    memset(&v, 0, sizeof(v));
    v.type = NQL_VAL_INTERVAL;
    v.v.interval = iv;
    return v;
}

static NqlInterval* nql_val_to_interval(const NqlValue* v) {
    if (v->type == NQL_VAL_INTERVAL && v->v.interval) return v->v.interval;
    if (v->type == NQL_VAL_INT || v->type == NQL_VAL_FLOAT) {
        double x = nql_val_as_float(v);
        return nql_interval_alloc(x, x);
    }
    return NULL;
}

static void nql_interval_to_str(const NqlInterval* iv, char* buf, size_t bufsz) {
    if (!iv) { strncpy(buf, "NULL", bufsz); return; }
    snprintf(buf, bufsz, "[%.6g, %.6g]", iv->lo, iv->hi);
}

/* ======================================================================
 * INTERVAL NQL FUNCTIONS
 * ====================================================================== */

/** INTERVAL(lo, hi) */
static NqlValue nql_fn_interval(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double lo = nql_val_as_float(&args[0]);
    double hi = nql_val_as_float(&args[1]);
    if (lo > hi) { double t = lo; lo = hi; hi = t; }
    NqlInterval* iv = nql_interval_alloc(lo, hi);
    return iv ? nql_val_interval(iv) : nql_val_null();
}

/** INTERVAL_LO(iv) */
static NqlValue nql_fn_interval_lo(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* iv = nql_val_to_interval(&args[0]);
    if (!iv) return nql_val_null();
    double lo = iv->lo;
    if (args[0].type != NQL_VAL_INTERVAL) free(iv);
    return nql_val_float(lo);
}

/** INTERVAL_HI(iv) */
static NqlValue nql_fn_interval_hi(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* iv = nql_val_to_interval(&args[0]);
    if (!iv) return nql_val_null();
    double hi = iv->hi;
    if (args[0].type != NQL_VAL_INTERVAL) free(iv);
    return nql_val_float(hi);
}

/** INTERVAL_WIDTH(iv) */
static NqlValue nql_fn_interval_width(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* iv = nql_val_to_interval(&args[0]);
    if (!iv) return nql_val_null();
    double w = iv->hi - iv->lo;
    if (args[0].type != NQL_VAL_INTERVAL) free(iv);
    return nql_val_float(w);
}

/** INTERVAL_MIDPOINT(iv) */
static NqlValue nql_fn_interval_midpoint(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* iv = nql_val_to_interval(&args[0]);
    if (!iv) return nql_val_null();
    double m = (iv->lo + iv->hi) * 0.5;
    if (args[0].type != NQL_VAL_INTERVAL) free(iv);
    return nql_val_float(m);
}

/** INTERVAL_ADD(a, b) -- [a.lo+b.lo, a.hi+b.hi] */
static NqlValue nql_fn_interval_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* a = nql_val_to_interval(&args[0]);
    NqlInterval* b = nql_val_to_interval(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_INTERVAL) free(a); if (b && args[1].type != NQL_VAL_INTERVAL) free(b); return nql_val_null(); }
    NqlInterval* r = nql_interval_alloc(a->lo + b->lo, a->hi + b->hi);
    if (args[0].type != NQL_VAL_INTERVAL) free(a);
    if (args[1].type != NQL_VAL_INTERVAL) free(b);
    return r ? nql_val_interval(r) : nql_val_null();
}

/** INTERVAL_SUB(a, b) -- [a.lo-b.hi, a.hi-b.lo] */
static NqlValue nql_fn_interval_sub(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* a = nql_val_to_interval(&args[0]);
    NqlInterval* b = nql_val_to_interval(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_INTERVAL) free(a); if (b && args[1].type != NQL_VAL_INTERVAL) free(b); return nql_val_null(); }
    NqlInterval* r = nql_interval_alloc(a->lo - b->hi, a->hi - b->lo);
    if (args[0].type != NQL_VAL_INTERVAL) free(a);
    if (args[1].type != NQL_VAL_INTERVAL) free(b);
    return r ? nql_val_interval(r) : nql_val_null();
}

/** INTERVAL_MUL(a, b) -- min/max of all 4 products */
static NqlValue nql_fn_interval_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* a = nql_val_to_interval(&args[0]);
    NqlInterval* b = nql_val_to_interval(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_INTERVAL) free(a); if (b && args[1].type != NQL_VAL_INTERVAL) free(b); return nql_val_null(); }
    double p1 = a->lo * b->lo, p2 = a->lo * b->hi, p3 = a->hi * b->lo, p4 = a->hi * b->hi;
    double lo = p1, hi = p1;
    if (p2 < lo) lo = p2; if (p2 > hi) hi = p2;
    if (p3 < lo) lo = p3; if (p3 > hi) hi = p3;
    if (p4 < lo) lo = p4; if (p4 > hi) hi = p4;
    NqlInterval* r = nql_interval_alloc(lo, hi);
    if (args[0].type != NQL_VAL_INTERVAL) free(a);
    if (args[1].type != NQL_VAL_INTERVAL) free(b);
    return r ? nql_val_interval(r) : nql_val_null();
}

/** INTERVAL_DIV(a, b) -- a * [1/b.hi, 1/b.lo] (b must not contain 0) */
static NqlValue nql_fn_interval_div(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* a = nql_val_to_interval(&args[0]);
    NqlInterval* b = nql_val_to_interval(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_INTERVAL) free(a); if (b && args[1].type != NQL_VAL_INTERVAL) free(b); return nql_val_null(); }
    if (b->lo <= 0.0 && b->hi >= 0.0) {
        if (args[0].type != NQL_VAL_INTERVAL) free(a);
        if (args[1].type != NQL_VAL_INTERVAL) free(b);
        return nql_val_null(); /* division by interval containing zero */
    }
    double inv_lo = 1.0 / b->hi, inv_hi = 1.0 / b->lo;
    double p1 = a->lo * inv_lo, p2 = a->lo * inv_hi, p3 = a->hi * inv_lo, p4 = a->hi * inv_hi;
    double lo = p1, hi = p1;
    if (p2 < lo) lo = p2; if (p2 > hi) hi = p2;
    if (p3 < lo) lo = p3; if (p3 > hi) hi = p3;
    if (p4 < lo) lo = p4; if (p4 > hi) hi = p4;
    NqlInterval* r = nql_interval_alloc(lo, hi);
    if (args[0].type != NQL_VAL_INTERVAL) free(a);
    if (args[1].type != NQL_VAL_INTERVAL) free(b);
    return r ? nql_val_interval(r) : nql_val_null();
}

/** INTERVAL_CONTAINS(iv, x) */
static NqlValue nql_fn_interval_contains(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* iv = nql_val_to_interval(&args[0]);
    if (!iv) return nql_val_bool(false);
    double x = nql_val_as_float(&args[1]);
    bool result = (x >= iv->lo && x <= iv->hi);
    if (args[0].type != NQL_VAL_INTERVAL) free(iv);
    return nql_val_bool(result);
}

/** INTERVAL_OVERLAPS(a, b) */
static NqlValue nql_fn_interval_overlaps(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* a = nql_val_to_interval(&args[0]);
    NqlInterval* b = nql_val_to_interval(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_INTERVAL) free(a); if (b && args[1].type != NQL_VAL_INTERVAL) free(b); return nql_val_bool(false); }
    bool result = (a->lo <= b->hi && b->lo <= a->hi);
    if (args[0].type != NQL_VAL_INTERVAL) free(a);
    if (args[1].type != NQL_VAL_INTERVAL) free(b);
    return nql_val_bool(result);
}

/** INTERVAL_INTERSECT(a, b) */
static NqlValue nql_fn_interval_intersect(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* a = nql_val_to_interval(&args[0]);
    NqlInterval* b = nql_val_to_interval(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_INTERVAL) free(a); if (b && args[1].type != NQL_VAL_INTERVAL) free(b); return nql_val_null(); }
    double lo = a->lo > b->lo ? a->lo : b->lo;
    double hi = a->hi < b->hi ? a->hi : b->hi;
    if (args[0].type != NQL_VAL_INTERVAL) free(a);
    if (args[1].type != NQL_VAL_INTERVAL) free(b);
    if (lo > hi) return nql_val_null(); /* empty intersection */
    NqlInterval* r = nql_interval_alloc(lo, hi);
    return r ? nql_val_interval(r) : nql_val_null();
}

/** INTERVAL_HULL(a, b) -- smallest interval containing both */
static NqlValue nql_fn_interval_hull(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* a = nql_val_to_interval(&args[0]);
    NqlInterval* b = nql_val_to_interval(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_INTERVAL) free(a); if (b && args[1].type != NQL_VAL_INTERVAL) free(b); return nql_val_null(); }
    double lo = a->lo < b->lo ? a->lo : b->lo;
    double hi = a->hi > b->hi ? a->hi : b->hi;
    if (args[0].type != NQL_VAL_INTERVAL) free(a);
    if (args[1].type != NQL_VAL_INTERVAL) free(b);
    NqlInterval* r = nql_interval_alloc(lo, hi);
    return r ? nql_val_interval(r) : nql_val_null();
}

/** INTERVAL_POW(iv, n) -- integer power via repeated multiplication */
static NqlValue nql_fn_interval_pow(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* iv = nql_val_to_interval(&args[0]);
    if (!iv) return nql_val_null();
    int64_t n = nql_val_as_int(&args[1]);
    double lo = iv->lo, hi = iv->hi;
    if (args[0].type != NQL_VAL_INTERVAL) free(iv);
    if (n == 0) { NqlInterval* r = nql_interval_alloc(1.0, 1.0); return r ? nql_val_interval(r) : nql_val_null(); }
    if (n == 1) { NqlInterval* r = nql_interval_alloc(lo, hi); return r ? nql_val_interval(r) : nql_val_null(); }
    if (n < 0) return nql_val_null(); /* negative powers not supported for intervals */
    /* Even powers: [0, max(lo^n, hi^n)] if interval spans 0, else min/max of endpoints */
    if (n % 2 == 0 && lo < 0.0 && hi > 0.0) {
        double a = pow(-lo, (double)n), b = pow(hi, (double)n);
        NqlInterval* r = nql_interval_alloc(0.0, a > b ? a : b);
        return r ? nql_val_interval(r) : nql_val_null();
    }
    double a = pow(lo, (double)n), b = pow(hi, (double)n);
    double rlo = a < b ? a : b, rhi = a > b ? a : b;
    NqlInterval* r = nql_interval_alloc(rlo, rhi);
    return r ? nql_val_interval(r) : nql_val_null();
}

/** INTERVAL_SQRT(iv) -- [sqrt(lo), sqrt(hi)] (lo must be >= 0) */
static NqlValue nql_fn_interval_sqrt(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* iv = nql_val_to_interval(&args[0]);
    if (!iv) return nql_val_null();
    double lo = iv->lo, hi = iv->hi;
    if (args[0].type != NQL_VAL_INTERVAL) free(iv);
    if (lo < 0.0) return nql_val_null();
    NqlInterval* r = nql_interval_alloc(sqrt(lo), sqrt(hi));
    return r ? nql_val_interval(r) : nql_val_null();
}

/** INTERVAL_ABS(iv) */
static NqlValue nql_fn_interval_abs(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlInterval* iv = nql_val_to_interval(&args[0]);
    if (!iv) return nql_val_null();
    double lo = iv->lo, hi = iv->hi;
    if (args[0].type != NQL_VAL_INTERVAL) free(iv);
    if (lo >= 0.0) { NqlInterval* r = nql_interval_alloc(lo, hi); return r ? nql_val_interval(r) : nql_val_null(); }
    if (hi <= 0.0) { NqlInterval* r = nql_interval_alloc(-hi, -lo); return r ? nql_val_interval(r) : nql_val_null(); }
    double m = -lo > hi ? -lo : hi;
    NqlInterval* r = nql_interval_alloc(0.0, m);
    return r ? nql_val_interval(r) : nql_val_null();
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_interval_register_functions(void) {
    nql_register_func("INTERVAL",           nql_fn_interval,           2, 2, "Create interval [lo, hi]");
    nql_register_func("INTERVAL_LO",        nql_fn_interval_lo,        1, 1, "Lower bound of interval");
    nql_register_func("INTERVAL_HI",        nql_fn_interval_hi,        1, 1, "Upper bound of interval");
    nql_register_func("INTERVAL_WIDTH",     nql_fn_interval_width,     1, 1, "Width (hi - lo)");
    nql_register_func("INTERVAL_MIDPOINT",  nql_fn_interval_midpoint,  1, 1, "Midpoint of interval");
    nql_register_func("INTERVAL_ADD",       nql_fn_interval_add,       2, 2, "Interval addition");
    nql_register_func("INTERVAL_SUB",       nql_fn_interval_sub,       2, 2, "Interval subtraction");
    nql_register_func("INTERVAL_MUL",       nql_fn_interval_mul,       2, 2, "Interval multiplication");
    nql_register_func("INTERVAL_DIV",       nql_fn_interval_div,       2, 2, "Interval division (divisor must not contain 0)");
    nql_register_func("INTERVAL_CONTAINS",  nql_fn_interval_contains,  2, 2, "Test if interval contains point");
    nql_register_func("INTERVAL_OVERLAPS",  nql_fn_interval_overlaps,  2, 2, "Test if two intervals overlap");
    nql_register_func("INTERVAL_INTERSECT", nql_fn_interval_intersect, 2, 2, "Intersection of two intervals");
    nql_register_func("INTERVAL_HULL",      nql_fn_interval_hull,      2, 2, "Smallest interval containing both");
    nql_register_func("INTERVAL_POW",       nql_fn_interval_pow,       2, 2, "Integer power of interval");
    nql_register_func("INTERVAL_SQRT",      nql_fn_interval_sqrt,      1, 1, "Square root (lo must be >= 0)");
    nql_register_func("INTERVAL_ABS",       nql_fn_interval_abs,       1, 1, "Absolute value of interval");
}
