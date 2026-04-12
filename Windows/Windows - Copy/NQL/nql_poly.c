/**
 * nql_poly.c - Polynomial arithmetic type for NQL.
 * Part of the CPSS Viewer amalgamation.
 *
 * Polynomials are stored as coefficient arrays (low-to-high degree):
 *   coeffs[0] = constant term, coeffs[degree] = leading coefficient.
 * Coefficients are int64_t. All operations are exact integer arithmetic.
 * Modular polynomial ops use explicit mod parameter.
 *
 * GNFS prerequisites provided:
 *   - Polynomial evaluation (rational and modular)
 *   - Polynomial arithmetic (+, -, *, div, mod, GCD)
 *   - Root-finding over Z/pZ (algebraic factor base construction)
 *   - Resultant and discriminant (polynomial selection quality)
 *   - Derivative (for separability checks)
 */

/* ══════════════════════════════════════════════════════════════════════
 * NqlPoly struct — heap-allocated polynomial
 * ══════════════════════════════════════════════════════════════════════ */

#define NQL_POLY_MAX_DEGREE 256

typedef struct {
    int64_t* coeffs;    /* heap, length = degree + 1, coeffs[i] = coeff of x^i */
    int      degree;    /* degree of polynomial (-1 for zero poly) */
    int      alloc;     /* allocated capacity */
} NqlPoly;

static NqlPoly* nql_poly_alloc(int degree) {
    NqlPoly* p = (NqlPoly*)malloc(sizeof(NqlPoly));
    if (!p) return NULL;
    int alloc = degree + 1;
    if (alloc < 4) alloc = 4;
    if (alloc > NQL_POLY_MAX_DEGREE + 1) alloc = NQL_POLY_MAX_DEGREE + 1;
    p->coeffs = (int64_t*)calloc((size_t)alloc, sizeof(int64_t));
    if (!p->coeffs) { free(p); return NULL; }
    p->degree = degree;
    p->alloc = alloc;
    return p;
}

static void nql_poly_free(NqlPoly* p) {
    if (!p) return;
    free(p->coeffs);
    free(p);
}

static NqlPoly* nql_poly_copy(const NqlPoly* src) {
    if (!src) return NULL;
    NqlPoly* p = nql_poly_alloc(src->degree);
    if (!p) return NULL;
    memcpy(p->coeffs, src->coeffs, (size_t)(src->degree + 1) * sizeof(int64_t));
    p->degree = src->degree;
    return p;
}

/** Normalize: strip trailing zero coefficients. */
static void nql_poly_normalize(NqlPoly* p) {
    while (p->degree > 0 && p->coeffs[p->degree] == 0) --p->degree;
    if (p->degree == 0 && p->coeffs[0] == 0) p->degree = -1; /* zero poly */
}

/** Ensure capacity for degree d. */
static bool nql_poly_ensure(NqlPoly* p, int d) {
    if (d + 1 <= p->alloc) return true;
    int new_alloc = d + 1;
    if (new_alloc > NQL_POLY_MAX_DEGREE + 1) return false;
    int64_t* new_c = (int64_t*)realloc(p->coeffs, (size_t)new_alloc * sizeof(int64_t));
    if (!new_c) return false;
    memset(new_c + p->alloc, 0, (size_t)(new_alloc - p->alloc) * sizeof(int64_t));
    p->coeffs = new_c;
    p->alloc = new_alloc;
    return true;
}

/** Format polynomial as string: "3x^2 - 2x + 1" */
static void nql_poly_to_str(const NqlPoly* p, char* buf, size_t bufsz) {
    if (!p || p->degree < 0) { strncpy(buf, "0", bufsz); buf[bufsz-1]='\0'; return; }
    size_t off = 0;
    bool first = true;
    for (int i = p->degree; i >= 0 && off < bufsz - 20; --i) {
        int64_t c = p->coeffs[i];
        if (c == 0) continue;
        if (!first && c > 0) off += snprintf(buf + off, bufsz - off, " + ");
        else if (!first && c < 0) { off += snprintf(buf + off, bufsz - off, " - "); c = -c; }
        else if (first && c < 0) { off += snprintf(buf + off, bufsz - off, "-"); c = -c; }
        if (i == 0 || (c != 1 && c != -1))
            off += snprintf(buf + off, bufsz - off, "%" PRId64, c);
        else if (c == 1 && i > 0) { /* skip "1" before x */ }
        if (i > 1) off += snprintf(buf + off, bufsz - off, "x^%d", i);
        else if (i == 1) off += snprintf(buf + off, bufsz - off, "x");
        first = false;
    }
    if (first) strncpy(buf + off, "0", bufsz - off);
    buf[bufsz-1] = '\0';
}

/* ══════════════════════════════════════════════════════════════════════
 * NqlValue integration
 * ══════════════════════════════════════════════════════════════════════ */

static NqlValue nql_val_poly(NqlPoly* p) {
    NqlValue r; memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_POLY;
    r.v.qs_ptr = p; /* reuse qs_ptr void* slot */
    return r;
}

/* ══════════════════════════════════════════════════════════════════════
 * Polynomial arithmetic (pure integer)
 * ══════════════════════════════════════════════════════════════════════ */

static NqlPoly* nql_poly_add(const NqlPoly* a, const NqlPoly* b) {
    int deg = a->degree > b->degree ? a->degree : b->degree;
    NqlPoly* r = nql_poly_alloc(deg);
    if (!r) return NULL;
    for (int i = 0; i <= deg; ++i) {
        int64_t ca = (i <= a->degree) ? a->coeffs[i] : 0;
        int64_t cb = (i <= b->degree) ? b->coeffs[i] : 0;
        r->coeffs[i] = ca + cb;
    }
    r->degree = deg;
    nql_poly_normalize(r);
    return r;
}

static NqlPoly* nql_poly_sub(const NqlPoly* a, const NqlPoly* b) {
    int deg = a->degree > b->degree ? a->degree : b->degree;
    NqlPoly* r = nql_poly_alloc(deg);
    if (!r) return NULL;
    for (int i = 0; i <= deg; ++i) {
        int64_t ca = (i <= a->degree) ? a->coeffs[i] : 0;
        int64_t cb = (i <= b->degree) ? b->coeffs[i] : 0;
        r->coeffs[i] = ca - cb;
    }
    r->degree = deg;
    nql_poly_normalize(r);
    return r;
}

static NqlPoly* nql_poly_mul(const NqlPoly* a, const NqlPoly* b) {
    if (a->degree < 0 || b->degree < 0) return nql_poly_alloc(0);
    int deg = a->degree + b->degree;
    if (deg > NQL_POLY_MAX_DEGREE) return NULL;
    NqlPoly* r = nql_poly_alloc(deg);
    if (!r) return NULL;
    for (int i = 0; i <= a->degree; ++i)
        for (int j = 0; j <= b->degree; ++j)
            r->coeffs[i + j] += a->coeffs[i] * b->coeffs[j];
    r->degree = deg;
    nql_poly_normalize(r);
    return r;
}

/** Polynomial division: returns (quotient, remainder) via output params.
 *  q and rem must be freed by caller. */
static bool nql_poly_divmod(const NqlPoly* a, const NqlPoly* b,
                             NqlPoly** q_out, NqlPoly** r_out) {
    if (b->degree < 0 || (b->degree == 0 && b->coeffs[0] == 0)) return false;
    NqlPoly* rem = nql_poly_copy(a);
    if (!rem) return false;
    int qdeg = a->degree - b->degree;
    if (qdeg < 0) qdeg = 0;
    NqlPoly* q = nql_poly_alloc(qdeg);
    if (!q) { nql_poly_free(rem); return false; }
    int64_t lc = b->coeffs[b->degree];
    while (rem->degree >= b->degree && rem->degree >= 0) {
        int64_t rc = rem->coeffs[rem->degree];
        if (rc % lc != 0) break; /* not exact — stop */
        int64_t coeff = rc / lc;
        int shift = rem->degree - b->degree;
        if (shift >= 0 && shift < q->alloc) q->coeffs[shift] = coeff;
        for (int i = 0; i <= b->degree; ++i)
            rem->coeffs[i + shift] -= coeff * b->coeffs[i];
        nql_poly_normalize(rem);
    }
    nql_poly_normalize(q);
    *q_out = q;
    *r_out = rem;
    return true;
}

/** Polynomial GCD via Euclidean algorithm (exact integer division). */
static NqlPoly* nql_poly_gcd(const NqlPoly* a, const NqlPoly* b) {
    NqlPoly* x = nql_poly_copy(a);
    NqlPoly* y = nql_poly_copy(b);
    if (!x || !y) { nql_poly_free(x); nql_poly_free(y); return NULL; }
    while (y->degree >= 0) {
        NqlPoly* q; NqlPoly* r;
        if (!nql_poly_divmod(x, y, &q, &r)) break;
        nql_poly_free(x); nql_poly_free(q);
        x = y; y = r;
    }
    nql_poly_free(y);
    return x;
}

/** Evaluate polynomial at integer x. */
static int64_t nql_poly_eval_int(const NqlPoly* p, int64_t x) {
    if (!p || p->degree < 0) return 0;
    int64_t result = p->coeffs[p->degree];
    for (int i = p->degree - 1; i >= 0; --i)
        result = result * x + p->coeffs[i];
    return result;
}

/** Evaluate polynomial at x modulo m. Uses mulmod for overflow safety. */
static uint64_t nql_poly_eval_mod(const NqlPoly* p, uint64_t x, uint64_t m) {
    if (!p || p->degree < 0 || m == 0) return 0u;
    uint64_t result = (uint64_t)((p->coeffs[p->degree] % (int64_t)m + (int64_t)m) % (int64_t)m);
    for (int i = p->degree - 1; i >= 0; --i) {
        result = mulmod_u64(result, x, m);
        int64_t ci = p->coeffs[i] % (int64_t)m;
        if (ci < 0) ci += (int64_t)m;
        result = (result + (uint64_t)ci) % m;
    }
    return result;
}

/** Formal derivative. */
static NqlPoly* nql_poly_derivative(const NqlPoly* p) {
    if (!p || p->degree <= 0) return nql_poly_alloc(0);
    NqlPoly* d = nql_poly_alloc(p->degree - 1);
    if (!d) return NULL;
    for (int i = 1; i <= p->degree; ++i)
        d->coeffs[i - 1] = p->coeffs[i] * (int64_t)i;
    d->degree = p->degree - 1;
    nql_poly_normalize(d);
    return d;
}

/** Find all roots of p(x) mod prime by brute force (0..prime-1). Returns NqlArray of roots. */
static NqlValue nql_poly_roots_mod(const NqlPoly* p, uint64_t prime) {
    NqlArray* roots = nql_array_alloc(32u);
    if (!roots) return nql_val_null();
    for (uint64_t x = 0u; x < prime; ++x) {
        if (nql_poly_eval_mod(p, x, prime) == 0u)
            nql_array_push(roots, nql_val_int((int64_t)x));
    }
    return nql_val_array(roots);
}

/** Resultant of two polynomials via Sylvester matrix determinant (small degree only).
 *  For degree d1, d2 ≤ 10. Returns int64. */
static int64_t nql_poly_resultant(const NqlPoly* f, const NqlPoly* g) {
    if (!f || !g || f->degree < 0 || g->degree < 0) return 0;
    int n = f->degree + g->degree;
    if (n > 20 || n <= 0) return 0; /* too large for dense */
    /* Build Sylvester matrix */
    int64_t* mat = (int64_t*)calloc((size_t)n * (size_t)n, sizeof(int64_t));
    if (!mat) return 0;
    /* g->degree rows from f */
    for (int r = 0; r < g->degree; ++r)
        for (int i = 0; i <= f->degree; ++i)
            mat[r * n + (r + i)] = f->coeffs[f->degree - i];
    /* f->degree rows from g */
    for (int r = 0; r < f->degree; ++r)
        for (int i = 0; i <= g->degree; ++i)
            mat[(g->degree + r) * n + (r + i)] = g->coeffs[g->degree - i];
    /* Gaussian elimination for determinant */
    int64_t det = 1;
    for (int col = 0; col < n; ++col) {
        int pivot = -1;
        for (int r = col; r < n; ++r) {
            if (mat[r * n + col] != 0) { pivot = r; break; }
        }
        if (pivot < 0) { free(mat); return 0; }
        if (pivot != col) {
            for (int j = 0; j < n; ++j) {
                int64_t t = mat[col * n + j]; mat[col * n + j] = mat[pivot * n + j]; mat[pivot * n + j] = t;
            }
            det = -det;
        }
        det *= mat[col * n + col];
        for (int r = col + 1; r < n; ++r) {
            if (mat[r * n + col] == 0) continue;
            int64_t factor = mat[r * n + col];
            int64_t div = mat[col * n + col];
            for (int j = col; j < n; ++j)
                mat[r * n + j] = mat[r * n + j] * div - factor * mat[col * n + j];
            det /= div; /* careful: this is integer division */
        }
    }
    free(mat);
    return det;
}

/** Discriminant = (-1)^(d*(d-1)/2) * Resultant(f, f') / lc(f). */
static int64_t nql_poly_discriminant(const NqlPoly* f) {
    if (!f || f->degree <= 0) return 0;
    NqlPoly* fp = nql_poly_derivative(f);
    if (!fp) return 0;
    int64_t res = nql_poly_resultant(f, fp);
    nql_poly_free(fp);
    int64_t lc = f->coeffs[f->degree];
    if (lc == 0) return 0;
    int64_t disc = res / lc;
    int sign_exp = (f->degree * (f->degree - 1)) / 2;
    if (sign_exp & 1) disc = -disc;
    return disc;
}

/* ══════════════════════════════════════════════════════════════════════
 * NQL built-in function wrappers
 * ══════════════════════════════════════════════════════════════════════ */

/** POLY(c0, c1, ..., cn) — create polynomial from coefficients (low-to-high). */
static NqlValue nql_fn_poly(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc == 0u) return nql_val_poly(nql_poly_alloc(0));
    NqlPoly* p = nql_poly_alloc((int)argc - 1);
    if (!p) return nql_val_null();
    for (uint32_t i = 0u; i < argc; ++i)
        p->coeffs[i] = nql_val_as_int(&args[i]);
    p->degree = (int)argc - 1;
    nql_poly_normalize(p);
    return nql_val_poly(p);
}

/** POLY_DEGREE(p) */
static NqlValue nql_fn_poly_degree(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_int(-1);
    return nql_val_int(((NqlPoly*)args[0].v.qs_ptr)->degree);
}

/** POLY_LEADING_COEFFICIENT(p) */
static NqlValue nql_fn_poly_lc(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_int(0);
    NqlPoly* p = (NqlPoly*)args[0].v.qs_ptr;
    return (p->degree >= 0) ? nql_val_int(p->coeffs[p->degree]) : nql_val_int(0);
}

/** POLY_COEFFICIENT(p, i) — coefficient of x^i */
static NqlValue nql_fn_poly_coeff(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_int(0);
    NqlPoly* p = (NqlPoly*)args[0].v.qs_ptr;
    int i = (int)nql_val_as_int(&args[1]);
    if (i < 0 || i > p->degree) return nql_val_int(0);
    return nql_val_int(p->coeffs[i]);
}

/** POLY_EVALUATE(p, x) — evaluate at integer x */
static NqlValue nql_fn_poly_eval(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(nql_poly_eval_int((NqlPoly*)args[0].v.qs_ptr, nql_val_as_int(&args[1])));
}

/** POLY_EVALUATE_MOD(p, x, m) — evaluate at x mod m */
static NqlValue nql_fn_poly_eval_mod(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int((int64_t)nql_poly_eval_mod((NqlPoly*)args[0].v.qs_ptr,
        (uint64_t)nql_val_as_int(&args[1]), (uint64_t)nql_val_as_int(&args[2])));
}

/** POLY_ADD(a, b) */
static NqlValue nql_fn_poly_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || args[1].type != NQL_VAL_POLY) return nql_val_null();
    NqlPoly* r = nql_poly_add((NqlPoly*)args[0].v.qs_ptr, (NqlPoly*)args[1].v.qs_ptr);
    return r ? nql_val_poly(r) : nql_val_null();
}

/** POLY_SUBTRACT(a, b) */
static NqlValue nql_fn_poly_sub(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || args[1].type != NQL_VAL_POLY) return nql_val_null();
    NqlPoly* r = nql_poly_sub((NqlPoly*)args[0].v.qs_ptr, (NqlPoly*)args[1].v.qs_ptr);
    return r ? nql_val_poly(r) : nql_val_null();
}

/** POLY_MULTIPLY(a, b) */
static NqlValue nql_fn_poly_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || args[1].type != NQL_VAL_POLY) return nql_val_null();
    NqlPoly* r = nql_poly_mul((NqlPoly*)args[0].v.qs_ptr, (NqlPoly*)args[1].v.qs_ptr);
    return r ? nql_val_poly(r) : nql_val_null();
}

/** POLY_DIVIDE(a, b) — quotient */
static NqlValue nql_fn_poly_div(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || args[1].type != NQL_VAL_POLY) return nql_val_null();
    NqlPoly* q; NqlPoly* r;
    if (!nql_poly_divmod((NqlPoly*)args[0].v.qs_ptr, (NqlPoly*)args[1].v.qs_ptr, &q, &r))
        return nql_val_null();
    nql_poly_free(r);
    return nql_val_poly(q);
}

/** POLY_REMAINDER(a, b) — remainder */
static NqlValue nql_fn_poly_rem(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || args[1].type != NQL_VAL_POLY) return nql_val_null();
    NqlPoly* q; NqlPoly* r;
    if (!nql_poly_divmod((NqlPoly*)args[0].v.qs_ptr, (NqlPoly*)args[1].v.qs_ptr, &q, &r))
        return nql_val_null();
    nql_poly_free(q);
    return nql_val_poly(r);
}

/** POLY_GCD(a, b) */
static NqlValue nql_fn_poly_gcd(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || args[1].type != NQL_VAL_POLY) return nql_val_null();
    NqlPoly* r = nql_poly_gcd((NqlPoly*)args[0].v.qs_ptr, (NqlPoly*)args[1].v.qs_ptr);
    return r ? nql_val_poly(r) : nql_val_null();
}

/** POLY_DERIVATIVE(p) */
static NqlValue nql_fn_poly_deriv(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_null();
    NqlPoly* r = nql_poly_derivative((NqlPoly*)args[0].v.qs_ptr);
    return r ? nql_val_poly(r) : nql_val_null();
}

/** POLY_ROOTS_MOD(p, prime) — find all roots mod prime */
static NqlValue nql_fn_poly_roots_mod(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_null();
    return nql_poly_roots_mod((NqlPoly*)args[0].v.qs_ptr, (uint64_t)nql_val_as_int(&args[1]));
}

/** POLY_RESULTANT(f, g) */
static NqlValue nql_fn_poly_resultant(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || args[1].type != NQL_VAL_POLY) return nql_val_int(0);
    return nql_val_int(nql_poly_resultant((NqlPoly*)args[0].v.qs_ptr, (NqlPoly*)args[1].v.qs_ptr));
}

/** POLY_DISCRIMINANT(f) */
static NqlValue nql_fn_poly_disc(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(nql_poly_discriminant((NqlPoly*)args[0].v.qs_ptr));
}

/** POLY_FROM_ROOTS(r1, r2, ...) — construct (x-r1)(x-r2)... */
static NqlValue nql_fn_poly_from_roots(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc == 0u) return nql_val_poly(nql_poly_alloc(0));
    /* Start with (x - r1) */
    NqlPoly* result = nql_poly_alloc(1);
    if (!result) return nql_val_null();
    result->coeffs[0] = -nql_val_as_int(&args[0]);
    result->coeffs[1] = 1;
    result->degree = 1;
    for (uint32_t i = 1u; i < argc; ++i) {
        NqlPoly* factor = nql_poly_alloc(1);
        if (!factor) { nql_poly_free(result); return nql_val_null(); }
        factor->coeffs[0] = -nql_val_as_int(&args[i]);
        factor->coeffs[1] = 1;
        factor->degree = 1;
        NqlPoly* product = nql_poly_mul(result, factor);
        nql_poly_free(result);
        nql_poly_free(factor);
        if (!product) return nql_val_null();
        result = product;
    }
    return nql_val_poly(result);
}
