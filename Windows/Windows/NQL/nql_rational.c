/**
 * nql_rational.c - NQL Rational number type and functions.
 * Part of the CPSS Viewer amalgamation.
 *
 * Exact p/q arithmetic with automatic GCD normalization.
 * Stored as NQL_VAL_RATIONAL via heap pointer in v.rational.
 */

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * NqlRational STRUCT AND CORE OPERATIONS
 * ====================================================================== */

struct NqlRational_s {
    int64_t num;    /* numerator */
    int64_t den;    /* denominator (always > 0) */
};

static int64_t nql_rat_gcd(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { int64_t t = b; b = a % b; a = t; }
    return a;
}

static int64_t nql_rat_abs(int64_t x) { return x < 0 ? -x : x; }

/** Allocate and normalize a rational number. */
static NqlRational* nql_rational_alloc(int64_t num, int64_t den) {
    if (den == 0) return NULL;
    NqlRational* r = (NqlRational*)malloc(sizeof(NqlRational));
    if (!r) return NULL;
    /* Normalize: den > 0, reduce by GCD */
    if (den < 0) { num = -num; den = -den; }
    int64_t g = nql_rat_gcd(nql_rat_abs(num), den);
    if (g > 1) { num /= g; den /= g; }
    r->num = num;
    r->den = den;
    return r;
}

static NqlRational* nql_rational_copy(const NqlRational* src) {
    if (!src) return NULL;
    NqlRational* r = (NqlRational*)malloc(sizeof(NqlRational));
    if (!r) return NULL;
    r->num = src->num;
    r->den = src->den;
    return r;
}

static NqlValue nql_val_rational(NqlRational* r) {
    NqlValue v;
    memset(&v, 0, sizeof(v));
    v.type = NQL_VAL_RATIONAL;
    v.v.rational = r;
    return v;
}

/** Extract a rational from an NqlValue (promotes int/float). */
static NqlRational* nql_val_to_rational(const NqlValue* v) {
    if (v->type == NQL_VAL_RATIONAL && v->v.rational) return v->v.rational;
    if (v->type == NQL_VAL_INT) return nql_rational_alloc(v->v.ival, 1);
    if (v->type == NQL_VAL_FLOAT) {
        /* Approximate as p/q with limited denominator */
        double x = v->v.fval;
        int64_t sign = x < 0 ? -1 : 1;
        x = x < 0 ? -x : x;
        int64_t p0 = 0, q0 = 1, p1 = 1, q1 = 0;
        double rem = x;
        for (int i = 0; i < 30; i++) {
            int64_t a = (int64_t)rem;
            int64_t p2 = a * p1 + p0;
            int64_t q2 = a * q1 + q0;
            if (q2 > 1000000000LL) break;
            p0 = p1; q0 = q1; p1 = p2; q1 = q2;
            double frac = rem - (double)a;
            if (frac < 1e-12) break;
            rem = 1.0 / frac;
        }
        return nql_rational_alloc(sign * p1, q1);
    }
    return NULL;
}

/** Format rational as string: "3/4" or "5" */
static void nql_rational_to_str(const NqlRational* r, char* buf, size_t bufsz) {
    if (!r) { strncpy(buf, "NULL", bufsz); return; }
    if (r->den == 1)
        snprintf(buf, bufsz, "%lld", (long long)r->num);
    else
        snprintf(buf, bufsz, "%lld/%lld", (long long)r->num, (long long)r->den);
}

/* ======================================================================
 * RATIONAL NQL FUNCTIONS
 * ====================================================================== */

/** RATIONAL(p, q) -- create rational number */
static NqlValue nql_fn_rational(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    int64_t num = nql_val_as_int(&args[0]);
    int64_t den = (argc >= 2) ? nql_val_as_int(&args[1]) : 1;
    if (den == 0) return nql_val_null();
    NqlRational* r = nql_rational_alloc(num, den);
    return r ? nql_val_rational(r) : nql_val_null();
}

/** RATIONAL_NUM(r) -- numerator */
static NqlValue nql_fn_rational_num(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* r = nql_val_to_rational(&args[0]);
    if (!r) return nql_val_null();
    int64_t num = r->num;
    if (args[0].type != NQL_VAL_RATIONAL) free(r);
    return nql_val_int(num);
}

/** RATIONAL_DEN(r) -- denominator */
static NqlValue nql_fn_rational_den(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* r = nql_val_to_rational(&args[0]);
    if (!r) return nql_val_null();
    int64_t den = r->den;
    if (args[0].type != NQL_VAL_RATIONAL) free(r);
    return nql_val_int(den);
}

/** RATIONAL_ADD(a, b) */
static NqlValue nql_fn_rational_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* a = nql_val_to_rational(&args[0]);
    NqlRational* b = nql_val_to_rational(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_RATIONAL) free(a); if (b && args[1].type != NQL_VAL_RATIONAL) free(b); return nql_val_null(); }
    NqlRational* r = nql_rational_alloc(a->num * b->den + b->num * a->den, a->den * b->den);
    if (args[0].type != NQL_VAL_RATIONAL) free(a);
    if (args[1].type != NQL_VAL_RATIONAL) free(b);
    return r ? nql_val_rational(r) : nql_val_null();
}

/** RATIONAL_SUB(a, b) */
static NqlValue nql_fn_rational_sub(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* a = nql_val_to_rational(&args[0]);
    NqlRational* b = nql_val_to_rational(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_RATIONAL) free(a); if (b && args[1].type != NQL_VAL_RATIONAL) free(b); return nql_val_null(); }
    NqlRational* r = nql_rational_alloc(a->num * b->den - b->num * a->den, a->den * b->den);
    if (args[0].type != NQL_VAL_RATIONAL) free(a);
    if (args[1].type != NQL_VAL_RATIONAL) free(b);
    return r ? nql_val_rational(r) : nql_val_null();
}

/** RATIONAL_MUL(a, b) */
static NqlValue nql_fn_rational_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* a = nql_val_to_rational(&args[0]);
    NqlRational* b = nql_val_to_rational(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_RATIONAL) free(a); if (b && args[1].type != NQL_VAL_RATIONAL) free(b); return nql_val_null(); }
    NqlRational* r = nql_rational_alloc(a->num * b->num, a->den * b->den);
    if (args[0].type != NQL_VAL_RATIONAL) free(a);
    if (args[1].type != NQL_VAL_RATIONAL) free(b);
    return r ? nql_val_rational(r) : nql_val_null();
}

/** RATIONAL_DIV(a, b) */
static NqlValue nql_fn_rational_div(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* a = nql_val_to_rational(&args[0]);
    NqlRational* b = nql_val_to_rational(&args[1]);
    if (!a || !b || b->num == 0) { if (a && args[0].type != NQL_VAL_RATIONAL) free(a); if (b && args[1].type != NQL_VAL_RATIONAL) free(b); return nql_val_null(); }
    NqlRational* r = nql_rational_alloc(a->num * b->den, a->den * b->num);
    if (args[0].type != NQL_VAL_RATIONAL) free(a);
    if (args[1].type != NQL_VAL_RATIONAL) free(b);
    return r ? nql_val_rational(r) : nql_val_null();
}

/** RATIONAL_POW(r, n) -- integer exponent */
static NqlValue nql_fn_rational_pow(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    NqlRational* a = nql_val_to_rational(&args[0]);
    if (!a) return nql_val_null();
    int64_t n = nql_val_as_int(&args[1]);
    int64_t num = a->num, den = a->den;
    if (args[0].type != NQL_VAL_RATIONAL) free(a);
    if (n < 0) { int64_t t = num; num = den; den = t; n = -n; if (den < 0) { num = -num; den = -den; } }
    int64_t rn = 1, rd = 1;
    for (int64_t i = 0; i < n && i < 60; i++) { rn *= num; rd *= den; }
    NqlRational* r = nql_rational_alloc(rn, rd);
    return r ? nql_val_rational(r) : nql_val_null();
}

/** RATIONAL_ABS(r) */
static NqlValue nql_fn_rational_abs(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* a = nql_val_to_rational(&args[0]);
    if (!a) return nql_val_null();
    NqlRational* r = nql_rational_alloc(nql_rat_abs(a->num), a->den);
    if (args[0].type != NQL_VAL_RATIONAL) free(a);
    return r ? nql_val_rational(r) : nql_val_null();
}

/** RATIONAL_CMP(a, b) -- returns -1, 0, or 1 */
static NqlValue nql_fn_rational_cmp(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* a = nql_val_to_rational(&args[0]);
    NqlRational* b = nql_val_to_rational(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_RATIONAL) free(a); if (b && args[1].type != NQL_VAL_RATIONAL) free(b); return nql_val_null(); }
    int64_t diff = a->num * b->den - b->num * a->den;
    if (args[0].type != NQL_VAL_RATIONAL) free(a);
    if (args[1].type != NQL_VAL_RATIONAL) free(b);
    return nql_val_int(diff < 0 ? -1 : (diff > 0 ? 1 : 0));
}

/** RATIONAL_TO_FLOAT(r) */
static NqlValue nql_fn_rational_to_float(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* r = nql_val_to_rational(&args[0]);
    if (!r) return nql_val_null();
    double val = (double)r->num / (double)r->den;
    if (args[0].type != NQL_VAL_RATIONAL) free(r);
    return nql_val_float(val);
}

/** RATIONAL_TO_CF(r) -- continued fraction expansion as array */
static NqlValue nql_fn_rational_to_cf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* r = nql_val_to_rational(&args[0]);
    if (!r) return nql_val_null();
    int64_t num = r->num, den = r->den;
    if (args[0].type != NQL_VAL_RATIONAL) free(r);
    if (den == 0) return nql_val_null();
    NqlArray* cf = nql_array_alloc(16);
    if (!cf) return nql_val_null();
    if (num < 0) { num = -num; } /* handle sign separately */
    for (int i = 0; i < 100 && den != 0; i++) {
        int64_t q = num / den;
        nql_array_push(cf, nql_val_int(q));
        int64_t rem = num % den;
        num = den;
        den = rem;
    }
    return nql_val_array(cf);
}

/** RATIONAL_FROM_FLOAT(f, max_den) -- best rational approximation */
static NqlValue nql_fn_rational_from_float(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    double x = nql_val_as_float(&args[0]);
    int64_t max_den = nql_val_as_int(&args[1]);
    if (max_den <= 0) max_den = 1000000;
    int64_t sign = x < 0 ? -1 : 1;
    x = x < 0 ? -x : x;
    int64_t p0 = 0, q0 = 1, p1 = 1, q1 = 0;
    double rem = x;
    for (int i = 0; i < 50; i++) {
        int64_t a = (int64_t)rem;
        int64_t p2 = a * p1 + p0;
        int64_t q2 = a * q1 + q0;
        if (q2 > max_den) break;
        p0 = p1; q0 = q1; p1 = p2; q1 = q2;
        double frac = rem - (double)a;
        if (frac < 1e-14) break;
        rem = 1.0 / frac;
    }
    NqlRational* r = nql_rational_alloc(sign * p1, q1);
    return r ? nql_val_rational(r) : nql_val_null();
}

/** RATIONAL_MEDIANT(a, b) -- Stern-Brocot mediant */
static NqlValue nql_fn_rational_mediant(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    NqlRational* a = nql_val_to_rational(&args[0]);
    NqlRational* b = nql_val_to_rational(&args[1]);
    if (!a || !b) { if (a && args[0].type != NQL_VAL_RATIONAL) free(a); if (b && args[1].type != NQL_VAL_RATIONAL) free(b); return nql_val_null(); }
    NqlRational* r = nql_rational_alloc(a->num + b->num, a->den + b->den);
    if (args[0].type != NQL_VAL_RATIONAL) free(a);
    if (args[1].type != NQL_VAL_RATIONAL) free(b);
    return r ? nql_val_rational(r) : nql_val_null();
}

/** RATIONAL_TO_STRING(r) -- explicit string conversion */
static NqlValue nql_fn_rational_to_string(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_RATIONAL || !args[0].v.rational) return nql_val_null();
    char buf[64];
    nql_rational_to_str(args[0].v.rational, buf, sizeof(buf));
    return nql_val_str(buf);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_rational_register_functions(void) {
    nql_register_func("RATIONAL",           nql_fn_rational,           1, 2, "Create rational p/q (auto-reduces)");
    nql_register_func("RATIONAL_NUM",       nql_fn_rational_num,       1, 1, "Numerator of rational");
    nql_register_func("RATIONAL_DEN",       nql_fn_rational_den,       1, 1, "Denominator of rational");
    nql_register_func("RATIONAL_ADD",       nql_fn_rational_add,       2, 2, "Add two rationals");
    nql_register_func("RATIONAL_SUB",       nql_fn_rational_sub,       2, 2, "Subtract two rationals");
    nql_register_func("RATIONAL_MUL",       nql_fn_rational_mul,       2, 2, "Multiply two rationals");
    nql_register_func("RATIONAL_DIV",       nql_fn_rational_div,       2, 2, "Divide two rationals");
    nql_register_func("RATIONAL_POW",       nql_fn_rational_pow,       2, 2, "Rational to integer power");
    nql_register_func("RATIONAL_ABS",       nql_fn_rational_abs,       1, 1, "Absolute value of rational");
    nql_register_func("RATIONAL_CMP",       nql_fn_rational_cmp,       2, 2, "Compare rationals: -1, 0, 1");
    nql_register_func("RATIONAL_TO_FLOAT",  nql_fn_rational_to_float,  1, 1, "Convert rational to float");
    nql_register_func("RATIONAL_TO_CF",     nql_fn_rational_to_cf,     1, 1, "Continued fraction expansion as array");
    nql_register_func("RATIONAL_FROM_FLOAT",nql_fn_rational_from_float,2, 2, "Best rational approx of float");
    nql_register_func("RATIONAL_MEDIANT",   nql_fn_rational_mediant,   2, 2, "Stern-Brocot mediant of two rationals");
    nql_register_func("RATIONAL_TO_STRING", nql_fn_rational_to_string, 1, 1, "Convert rational to string form");
}
