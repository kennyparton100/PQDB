/**
 * nql_cfrac.c - NQL Continued Fractions.
 * Part of the CPSS Viewer amalgamation.
 *
 * Exact CF from rationals, approximate CF from floats, periodic CF for sqrt(n),
 * convergents, best rational approximations.
 */

#include <math.h>
#include <stdlib.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * CONTINUED FRACTION FUNCTIONS
 * ====================================================================== */

/** CF_FROM_RATIONAL(p, q) -- exact CF expansion of p/q */
static NqlValue nql_fn_cf_from_rational(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    int64_t p = nql_val_as_int(&args[0]);
    int64_t q = nql_val_as_int(&args[1]);
    if (q == 0) return nql_val_null();
    if (q < 0) { p = -p; q = -q; }
    NqlArray* r = nql_array_alloc(32);
    if (!r) return nql_val_null();
    while (q != 0) {
        int64_t a = p / q;
        if (p < 0 && p % q != 0) a--; /* floor division */
        nql_array_push(r, nql_val_int(a));
        int64_t tmp = p - a * q;
        p = q; q = tmp;
    }
    return nql_val_array(r);
}

/** CF_FROM_FLOAT(x, max_terms) -- approximate CF from float */
static NqlValue nql_fn_cf_from_float(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double x = nql_val_as_float(&args[0]);
    int max_terms = (int)nql_val_as_int(&args[1]);
    if (max_terms < 1) max_terms = 1;
    if (max_terms > 100) max_terms = 100;
    NqlArray* r = nql_array_alloc((uint32_t)max_terms);
    if (!r) return nql_val_null();
    for (int i = 0; i < max_terms; i++) {
        int64_t a = (int64_t)floor(x);
        nql_array_push(r, nql_val_int(a));
        double frac = x - (double)a;
        if (fabs(frac) < 1e-12) break;
        x = 1.0 / frac;
        if (fabs(x) > 1e15) break;
    }
    return nql_val_array(r);
}

/** CF_FROM_SQRT(n [, max_terms]) -- periodic CF for sqrt(n) */
static NqlValue nql_fn_cf_from_sqrt(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    int64_t n = nql_val_as_int(&args[0]);
    int max_terms = (argc >= 2) ? (int)nql_val_as_int(&args[1]) : 50;
    if (n < 0) return nql_val_null();
    int64_t a0 = (int64_t)floor(sqrt((double)n));
    if (a0 * a0 == n) {
        /* Perfect square */
        NqlArray* r = nql_array_alloc(1);
        if (!r) return nql_val_null();
        nql_array_push(r, nql_val_int(a0));
        return nql_val_array(r);
    }
    NqlArray* r = nql_array_alloc((uint32_t)max_terms);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_int(a0));
    int64_t m = 0, d = 1, a = a0;
    for (int i = 1; i < max_terms; i++) {
        m = d * a - m;
        d = (n - m * m) / d;
        if (d == 0) break;
        a = (a0 + m) / d;
        nql_array_push(r, nql_val_int(a));
        if (a == 2 * a0) break; /* period complete */
    }
    return nql_val_array(r);
}

/** CF_CONVERGENT(cf_array, k) -- k-th convergent as [p, q] */
static NqlValue nql_fn_cf_convergent(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* cf = args[0].v.array;
    int k = (int)nql_val_as_int(&args[1]);
    if (k < 0 || (uint32_t)k >= cf->length) return nql_val_null();
    int64_t p_prev = 1, p_curr = nql_val_as_int(&cf->items[0]);
    int64_t q_prev = 0, q_curr = 1;
    for (int i = 1; i <= k; i++) {
        int64_t a = nql_val_as_int(&cf->items[i]);
        int64_t p_new = a * p_curr + p_prev;
        int64_t q_new = a * q_curr + q_prev;
        p_prev = p_curr; p_curr = p_new;
        q_prev = q_curr; q_curr = q_new;
    }
    NqlArray* r = nql_array_alloc(2);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_int(p_curr));
    nql_array_push(r, nql_val_int(q_curr));
    return nql_val_array(r);
}

/** CF_CONVERGENTS(cf_array) -- all convergents as [[p0,q0], [p1,q1], ...] */
static NqlValue nql_fn_cf_convergents(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* cf = args[0].v.array;
    NqlArray* result = nql_array_alloc(cf->length);
    if (!result) return nql_val_null();
    int64_t p_prev = 1, p_curr = nql_val_as_int(&cf->items[0]);
    int64_t q_prev = 0, q_curr = 1;
    NqlArray* pair = nql_array_alloc(2);
    if (pair) { nql_array_push(pair, nql_val_int(p_curr)); nql_array_push(pair, nql_val_int(q_curr)); nql_array_push(result, nql_val_array(pair)); }
    for (uint32_t i = 1; i < cf->length; i++) {
        int64_t a = nql_val_as_int(&cf->items[i]);
        int64_t p_new = a * p_curr + p_prev;
        int64_t q_new = a * q_curr + q_prev;
        p_prev = p_curr; p_curr = p_new;
        q_prev = q_curr; q_curr = q_new;
        pair = nql_array_alloc(2);
        if (pair) { nql_array_push(pair, nql_val_int(p_curr)); nql_array_push(pair, nql_val_int(q_curr)); nql_array_push(result, nql_val_array(pair)); }
    }
    return nql_val_array(result);
}

/** CF_TO_RATIONAL(cf_array) -- evaluate CF to [p, q] */
static NqlValue nql_fn_cf_to_rational(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* cf = args[0].v.array;
    if (cf->length == 0) return nql_val_null();
    /* Evaluate from the tail */
    int64_t p = nql_val_as_int(&cf->items[cf->length - 1]), q = 1;
    for (int i = (int)cf->length - 2; i >= 0; i--) {
        /* invert: p/q -> q/p, then add a_i */
        int64_t tmp = p; p = q; q = tmp;
        int64_t a = nql_val_as_int(&cf->items[i]);
        p = a * q + p;
    }
    NqlArray* r = nql_array_alloc(2);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_int(p));
    nql_array_push(r, nql_val_int(q));
    return nql_val_array(r);
}

/** CF_PERIOD(cf_array_from_sqrt) -- period length (0 if perfect square) */
static NqlValue nql_fn_cf_period(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* cf = args[0].v.array;
    if (cf->length <= 1) return nql_val_int(0);
    return nql_val_int((int64_t)(cf->length - 1));
}

/** CF_BEST_APPROXIMATIONS(x, max_den) -- best rational approximations with den <= max_den */
static NqlValue nql_fn_cf_best_approx(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    double x = nql_val_as_float(&args[0]);
    int64_t max_den = nql_val_as_int(&args[1]);
    if (max_den < 1) return nql_val_null();
    NqlArray* result = nql_array_alloc(32);
    if (!result) return nql_val_null();
    int64_t p_prev = 1, p_curr, q_prev = 0, q_curr = 1;
    double rem = x;
    for (int iter = 0; iter < 100; iter++) {
        int64_t a = (int64_t)floor(rem);
        if (iter == 0) p_curr = a;
        else {
            int64_t pn = a * p_curr + p_prev;
            int64_t qn = a * q_curr + q_prev;
            if (qn > max_den) break;
            p_prev = p_curr; p_curr = pn;
            q_prev = q_curr; q_curr = qn;
        }
        NqlArray* pair = nql_array_alloc(2);
        if (pair) { nql_array_push(pair, nql_val_int(p_curr)); nql_array_push(pair, nql_val_int(q_curr)); nql_array_push(result, nql_val_array(pair)); }
        double frac = rem - (double)a;
        if (fabs(frac) < 1e-12) break;
        rem = 1.0 / frac;
        if (fabs(rem) > 1e15) break;
    }
    return nql_val_array(result);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_cfrac_register_functions(void) {
    nql_register_func("CF_FROM_RATIONAL",       nql_fn_cf_from_rational, 2, 2, "Exact CF of p/q");
    nql_register_func("CF_FROM_FLOAT",          nql_fn_cf_from_float,    2, 2, "Approximate CF from float");
    nql_register_func("CF_FROM_SQRT",           nql_fn_cf_from_sqrt,     1, 2, "Periodic CF for sqrt(n)");
    nql_register_func("CF_CONVERGENT",          nql_fn_cf_convergent,    2, 2, "k-th convergent [p, q]");
    nql_register_func("CF_CONVERGENTS",         nql_fn_cf_convergents,   1, 1, "All convergents [[p,q],...]");
    nql_register_func("CF_TO_RATIONAL",         nql_fn_cf_to_rational,   1, 1, "Evaluate CF to [p, q]");
    nql_register_func("CF_PERIOD",              nql_fn_cf_period,        1, 1, "Period length of sqrt CF");
    nql_register_func("CF_BEST_APPROXIMATIONS", nql_fn_cf_best_approx,  2, 2, "Best rational approx with den <= max_den");
}
