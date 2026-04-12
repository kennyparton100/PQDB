/**
 * nql_series.c - NQL Formal Power Series type and functions.
 * Part of the CPSS Viewer amalgamation.
 *
 * Truncated formal power series up to a given order.
 * Stored as NQL_VAL_SERIES via v.series.
 */

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * NqlSeries STRUCT AND CORE OPERATIONS
 * ====================================================================== */

#define NQL_SERIES_MAX_ORDER 256

struct NqlSeries_s {
    double*  coeffs;   /* coeffs[k] = coefficient of x^k */
    uint32_t order;    /* truncation order (number of terms - 1) */
};

static NqlSeries* nql_series_alloc(uint32_t order) {
    if (order > NQL_SERIES_MAX_ORDER) order = NQL_SERIES_MAX_ORDER;
    NqlSeries* s = (NqlSeries*)malloc(sizeof(NqlSeries));
    if (!s) return NULL;
    s->coeffs = (double*)calloc(order + 1, sizeof(double));
    if (!s->coeffs) { free(s); return NULL; }
    s->order = order;
    return s;
}

static void nql_series_free(NqlSeries* s) {
    if (!s) return;
    free(s->coeffs);
    free(s);
}

static NqlSeries* nql_series_copy(const NqlSeries* src) {
    if (!src) return NULL;
    NqlSeries* s = nql_series_alloc(src->order);
    if (!s) return NULL;
    memcpy(s->coeffs, src->coeffs, (src->order + 1) * sizeof(double));
    return s;
}

static NqlValue nql_val_series(NqlSeries* s) {
    NqlValue v;
    memset(&v, 0, sizeof(v));
    v.type = NQL_VAL_SERIES;
    v.v.series = s;
    return v;
}

static void nql_series_to_str(const NqlSeries* s, char* buf, size_t bufsz) {
    if (!s) { strncpy(buf, "NULL", bufsz); return; }
    size_t off = 0;
    bool first = true;
    for (uint32_t i = 0; i <= s->order && off < bufsz - 30; i++) {
        if (s->coeffs[i] == 0.0 && !first) continue;
        if (!first && s->coeffs[i] >= 0.0) off += snprintf(buf + off, bufsz - off, "+");
        if (i == 0) off += snprintf(buf + off, bufsz - off, "%.4g", s->coeffs[i]);
        else if (i == 1) off += snprintf(buf + off, bufsz - off, "%.4gx", s->coeffs[i]);
        else off += snprintf(buf + off, bufsz - off, "%.4gx^%u", s->coeffs[i], i);
        first = false;
    }
    off += snprintf(buf + off, bufsz - off, "+O(x^%u)", s->order + 1);
    buf[bufsz - 1] = '\0';
}

/* ======================================================================
 * SERIES NQL FUNCTIONS
 * ====================================================================== */

/** SERIES(coeffs_array, order) -- create from array of coefficients */
static NqlValue nql_fn_series(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    uint32_t order = (argc >= 2) ? (uint32_t)nql_val_as_int(&args[1]) : (arr->length > 0 ? arr->length - 1 : 0);
    if (order > NQL_SERIES_MAX_ORDER) order = NQL_SERIES_MAX_ORDER;
    NqlSeries* s = nql_series_alloc(order);
    if (!s) return nql_val_null();
    for (uint32_t i = 0; i <= order && i < arr->length; i++)
        s->coeffs[i] = nql_val_as_float(&arr->items[i]);
    return nql_val_series(s);
}

/** SERIES_COEFF(s, k) -- coefficient of x^k */
static NqlValue nql_fn_series_coeff(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    int64_t k = nql_val_as_int(&args[1]);
    NqlSeries* s = args[0].v.series;
    if (k < 0 || (uint32_t)k > s->order) return nql_val_float(0.0);
    return nql_val_float(s->coeffs[k]);
}

/** SERIES_ORDER(s) */
static NqlValue nql_fn_series_order(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    return nql_val_int((int64_t)args[0].v.series->order);
}

/** SERIES_ADD(a, b) */
static NqlValue nql_fn_series_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    if (args[1].type != NQL_VAL_SERIES || !args[1].v.series) return nql_val_null();
    NqlSeries* a = args[0].v.series;
    NqlSeries* b = args[1].v.series;
    uint32_t order = a->order < b->order ? a->order : b->order;
    NqlSeries* r = nql_series_alloc(order);
    if (!r) return nql_val_null();
    for (uint32_t i = 0; i <= order; i++) {
        r->coeffs[i] = (i <= a->order ? a->coeffs[i] : 0.0) + (i <= b->order ? b->coeffs[i] : 0.0);
    }
    return nql_val_series(r);
}

/** SERIES_SUB(a, b) */
static NqlValue nql_fn_series_sub(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    if (args[1].type != NQL_VAL_SERIES || !args[1].v.series) return nql_val_null();
    NqlSeries* a = args[0].v.series;
    NqlSeries* b = args[1].v.series;
    uint32_t order = a->order < b->order ? a->order : b->order;
    NqlSeries* r = nql_series_alloc(order);
    if (!r) return nql_val_null();
    for (uint32_t i = 0; i <= order; i++) {
        r->coeffs[i] = (i <= a->order ? a->coeffs[i] : 0.0) - (i <= b->order ? b->coeffs[i] : 0.0);
    }
    return nql_val_series(r);
}

/** SERIES_MUL(a, b) -- Cauchy product */
static NqlValue nql_fn_series_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    if (args[1].type != NQL_VAL_SERIES || !args[1].v.series) return nql_val_null();
    NqlSeries* a = args[0].v.series;
    NqlSeries* b = args[1].v.series;
    uint32_t order = a->order < b->order ? a->order : b->order;
    NqlSeries* r = nql_series_alloc(order);
    if (!r) return nql_val_null();
    for (uint32_t n = 0; n <= order; n++) {
        double sum = 0.0;
        for (uint32_t k = 0; k <= n; k++) {
            double ak = (k <= a->order) ? a->coeffs[k] : 0.0;
            double bk = ((n - k) <= b->order) ? b->coeffs[n - k] : 0.0;
            sum += ak * bk;
        }
        r->coeffs[n] = sum;
    }
    return nql_val_series(r);
}

/** SERIES_INV(s) -- multiplicative inverse: 1/s, requires s[0] != 0 */
static NqlValue nql_fn_series_inv(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    NqlSeries* s = args[0].v.series;
    if (s->coeffs[0] == 0.0) return nql_val_null();
    NqlSeries* r = nql_series_alloc(s->order);
    if (!r) return nql_val_null();
    r->coeffs[0] = 1.0 / s->coeffs[0];
    for (uint32_t n = 1; n <= s->order; n++) {
        double sum = 0.0;
        for (uint32_t k = 1; k <= n; k++) {
            double sk = (k <= s->order) ? s->coeffs[k] : 0.0;
            sum += sk * r->coeffs[n - k];
        }
        r->coeffs[n] = -sum / s->coeffs[0];
    }
    return nql_val_series(r);
}

/** SERIES_DERIVATIVE(s) */
static NqlValue nql_fn_series_derivative(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    NqlSeries* s = args[0].v.series;
    if (s->order == 0) { NqlSeries* r = nql_series_alloc(0); return r ? nql_val_series(r) : nql_val_null(); }
    NqlSeries* r = nql_series_alloc(s->order - 1);
    if (!r) return nql_val_null();
    for (uint32_t i = 0; i < s->order; i++)
        r->coeffs[i] = s->coeffs[i + 1] * (double)(i + 1);
    return nql_val_series(r);
}

/** SERIES_INTEGRAL(s) -- indefinite integral with constant term 0 */
static NqlValue nql_fn_series_integral(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    NqlSeries* s = args[0].v.series;
    NqlSeries* r = nql_series_alloc(s->order + 1 <= NQL_SERIES_MAX_ORDER ? s->order + 1 : NQL_SERIES_MAX_ORDER);
    if (!r) return nql_val_null();
    r->coeffs[0] = 0.0;
    for (uint32_t i = 0; i <= s->order && i + 1 <= r->order; i++)
        r->coeffs[i + 1] = s->coeffs[i] / (double)(i + 1);
    return nql_val_series(r);
}

/** SERIES_EXP(order) -- e^x = 1 + x + x^2/2! + ... */
static NqlValue nql_fn_series_exp(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    uint32_t order = (uint32_t)nql_val_as_int(&args[0]);
    if (order > NQL_SERIES_MAX_ORDER) order = NQL_SERIES_MAX_ORDER;
    NqlSeries* r = nql_series_alloc(order);
    if (!r) return nql_val_null();
    r->coeffs[0] = 1.0;
    double fact = 1.0;
    for (uint32_t i = 1; i <= order; i++) {
        fact *= (double)i;
        r->coeffs[i] = 1.0 / fact;
    }
    return nql_val_series(r);
}

/** SERIES_LOG(order) -- log(1+x) = x - x^2/2 + x^3/3 - ... */
static NqlValue nql_fn_series_log(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    uint32_t order = (uint32_t)nql_val_as_int(&args[0]);
    if (order > NQL_SERIES_MAX_ORDER) order = NQL_SERIES_MAX_ORDER;
    NqlSeries* r = nql_series_alloc(order);
    if (!r) return nql_val_null();
    r->coeffs[0] = 0.0;
    for (uint32_t i = 1; i <= order; i++)
        r->coeffs[i] = ((i % 2 == 1) ? 1.0 : -1.0) / (double)i;
    return nql_val_series(r);
}

/** SERIES_SIN(order) -- sin(x) = x - x^3/3! + x5/5! - ... */
static NqlValue nql_fn_series_sin(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    uint32_t order = (uint32_t)nql_val_as_int(&args[0]);
    if (order > NQL_SERIES_MAX_ORDER) order = NQL_SERIES_MAX_ORDER;
    NqlSeries* r = nql_series_alloc(order);
    if (!r) return nql_val_null();
    double fact = 1.0;
    for (uint32_t i = 1; i <= order; i++) {
        fact *= (double)i;
        if (i % 2 == 1) r->coeffs[i] = (((i / 2) % 2 == 0) ? 1.0 : -1.0) / fact;
    }
    return nql_val_series(r);
}

/** SERIES_COS(order) -- cos(x) = 1 - x^2/2! + x4/4! - ... */
static NqlValue nql_fn_series_cos(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    uint32_t order = (uint32_t)nql_val_as_int(&args[0]);
    if (order > NQL_SERIES_MAX_ORDER) order = NQL_SERIES_MAX_ORDER;
    NqlSeries* r = nql_series_alloc(order);
    if (!r) return nql_val_null();
    double fact = 1.0;
    r->coeffs[0] = 1.0;
    for (uint32_t i = 1; i <= order; i++) {
        fact *= (double)i;
        if (i % 2 == 0) r->coeffs[i] = (((i / 2) % 2 == 0) ? 1.0 : -1.0) / fact;
    }
    return nql_val_series(r);
}

/** SERIES_EVAL(s, x) -- evaluate truncated series at point x */
static NqlValue nql_fn_series_eval(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    NqlSeries* s = args[0].v.series;
    double x = nql_val_as_float(&args[1]);
    double result = 0.0, xn = 1.0;
    for (uint32_t i = 0; i <= s->order; i++) {
        result += s->coeffs[i] * xn;
        xn *= x;
    }
    return nql_val_float(result);
}

/** SERIES_TO_ARRAY(s) -- export coefficients as array */
static NqlValue nql_fn_series_to_array(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    NqlSeries* s = args[0].v.series;
    NqlArray* arr = nql_array_alloc(s->order + 1);
    if (!arr) return nql_val_null();
    for (uint32_t i = 0; i <= s->order; i++)
        nql_array_push(arr, nql_val_float(s->coeffs[i]));
    return nql_val_array(arr);
}

/** SERIES_COMPOSE(f, g) -- f(g(x)), requires g[0] = 0 */
static NqlValue nql_fn_series_compose(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SERIES || !args[0].v.series) return nql_val_null();
    if (args[1].type != NQL_VAL_SERIES || !args[1].v.series) return nql_val_null();
    NqlSeries* f = args[0].v.series;
    NqlSeries* g = args[1].v.series;
    if (g->coeffs[0] != 0.0) return nql_val_null(); /* g(0) must be 0 for convergence */
    uint32_t order = f->order < g->order ? f->order : g->order;
    NqlSeries* r = nql_series_alloc(order);
    if (!r) return nql_val_null();
    /* Horner-like: r = f[n], then r = r*g + f[n-1], ... */
    r->coeffs[0] = f->coeffs[order];
    for (int32_t k = (int32_t)order - 1; k >= 0; k--) {
        /* r = r * g + f[k] */
        NqlSeries* tmp = nql_series_alloc(order);
        if (!tmp) { nql_series_free(r); return nql_val_null(); }
        for (uint32_t n = 0; n <= order; n++) {
            double sum = 0.0;
            for (uint32_t j = 0; j <= n; j++) {
                sum += r->coeffs[j] * g->coeffs[n - j];
            }
            tmp->coeffs[n] = sum;
        }
        tmp->coeffs[0] += f->coeffs[k];
        nql_series_free(r);
        r = tmp;
    }
    return nql_val_series(r);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_series_register_functions(void) {
    nql_register_func("SERIES",            nql_fn_series,            1, 2, "Create power series from coefficient array");
    nql_register_func("SERIES_COEFF",      nql_fn_series_coeff,      2, 2, "Coefficient of x^k");
    nql_register_func("SERIES_ORDER",      nql_fn_series_order,      1, 1, "Truncation order");
    nql_register_func("SERIES_ADD",        nql_fn_series_add,        2, 2, "Add two series");
    nql_register_func("SERIES_SUB",        nql_fn_series_sub,        2, 2, "Subtract two series");
    nql_register_func("SERIES_MUL",        nql_fn_series_mul,        2, 2, "Multiply two series (Cauchy product)");
    nql_register_func("SERIES_INV",        nql_fn_series_inv,        1, 1, "Multiplicative inverse (constant term != 0)");
    nql_register_func("SERIES_DERIVATIVE", nql_fn_series_derivative, 1, 1, "Formal derivative");
    nql_register_func("SERIES_INTEGRAL",   nql_fn_series_integral,   1, 1, "Formal integral (constant = 0)");
    nql_register_func("SERIES_EXP",        nql_fn_series_exp,        1, 1, "e^x to given order");
    nql_register_func("SERIES_LOG",        nql_fn_series_log,        1, 1, "log(1+x) to given order");
    nql_register_func("SERIES_SIN",        nql_fn_series_sin,        1, 1, "sin(x) to given order");
    nql_register_func("SERIES_COS",        nql_fn_series_cos,        1, 1, "cos(x) to given order");
    nql_register_func("SERIES_EVAL",       nql_fn_series_eval,       2, 2, "Evaluate series at point x");
    nql_register_func("SERIES_TO_ARRAY",   nql_fn_series_to_array,   1, 1, "Export coefficients as array");
    nql_register_func("SERIES_COMPOSE",    nql_fn_series_compose,    2, 2, "Composition f(g(x)), requires g(0)=0");
}
