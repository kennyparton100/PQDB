/**
 * nql_elliptic.c - NQL Elliptic Curve arithmetic over GF(p).
 * Part of the CPSS Viewer amalgamation.
 *
 * Weierstrass curves y^2 = x^3 + ax + b over prime fields.
 * Point addition, scalar multiplication, order computation (baby-step giant-step).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * EC point representation: array [x, y] or [0] for point at infinity
 * Curve representation: array [a, b, p]
 * ====================================================================== */

static int64_t ec_mod(int64_t a, int64_t p) { int64_t r = a % p; return r < 0 ? r + p : r; }

static int64_t ec_inv(int64_t a, int64_t p) {
    int64_t g = p, x = 0, y = 1, aa = ec_mod(a, p);
    while (aa > 0) {
        int64_t q = g / aa, tmp = g - q * aa; g = aa; aa = tmp;
        tmp = x - q * y; x = y; y = tmp;
    }
    return ec_mod(x, p);
}

static int64_t ec_mulmod(int64_t a, int64_t b, int64_t p) {
    return (int64_t)mulmod_u64((uint64_t)ec_mod(a, p), (uint64_t)ec_mod(b, p), (uint64_t)p);
}

static bool ec_is_inf(const NqlArray* pt) {
    return (!pt || pt->length == 1);
}

static NqlValue ec_make_inf(void) {
    NqlArray* r = nql_array_alloc(1);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_int(0));
    return nql_val_array(r);
}

static NqlValue ec_make_point(int64_t x, int64_t y) {
    NqlArray* r = nql_array_alloc(2);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_int(x));
    nql_array_push(r, nql_val_int(y));
    return nql_val_array(r);
}

/* Add two points on y^2 = x^3 + ax + b mod p */
static NqlValue ec_add_pts(int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t a, int64_t p) {
    int64_t lam;
    if (x1 == x2 && y1 == y2) {
        /* Point doubling */
        if (y1 == 0) return ec_make_inf();
        int64_t num = ec_mod(3 * ec_mulmod(x1, x1, p) + a, p);
        int64_t den = ec_mod(2 * y1, p);
        lam = ec_mulmod(num, ec_inv(den, p), p);
    } else {
        if (x1 == x2) return ec_make_inf(); /* P + (-P) = O */
        int64_t num = ec_mod(y2 - y1, p);
        int64_t den = ec_mod(x2 - x1, p);
        lam = ec_mulmod(num, ec_inv(den, p), p);
    }
    int64_t x3 = ec_mod(ec_mulmod(lam, lam, p) - x1 - x2, p);
    int64_t y3 = ec_mod(ec_mulmod(lam, x1 - x3, p) - y1, p);
    return ec_make_point(x3, y3);
}

/* ======================================================================
 * NQL FUNCTIONS
 * ====================================================================== */

/** EC_CURVE(a, b, p) -- create curve descriptor [a, b, p] */
static NqlValue nql_fn_ec_curve(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    int64_t a = nql_val_as_int(&args[0]);
    int64_t b = nql_val_as_int(&args[1]);
    int64_t p = nql_val_as_int(&args[2]);
    if (p < 3) return nql_val_null();
    /* Check discriminant: 4a^3 + 27b^2 != 0 mod p */
    int64_t disc = ec_mod(4 * ec_mulmod(a, ec_mulmod(a, a, p), p) + 27 * ec_mulmod(b, b, p), p);
    if (disc == 0) return nql_val_null();
    NqlArray* r = nql_array_alloc(3);
    if (!r) return nql_val_null();
    nql_array_push(r, nql_val_int(ec_mod(a, p)));
    nql_array_push(r, nql_val_int(ec_mod(b, p)));
    nql_array_push(r, nql_val_int(p));
    return nql_val_array(r);
}

/** EC_POINT(curve, x, y) -- create/validate point */
static NqlValue nql_fn_ec_point(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length < 3) return nql_val_null();
    NqlArray* c = args[0].v.array;
    int64_t a = nql_val_as_int(&c->items[0]);
    int64_t b = nql_val_as_int(&c->items[1]);
    int64_t p = nql_val_as_int(&c->items[2]);
    int64_t x = ec_mod(nql_val_as_int(&args[1]), p);
    int64_t y = ec_mod(nql_val_as_int(&args[2]), p);
    /* Verify: y^2 = x^3 + ax + b mod p */
    int64_t lhs = ec_mulmod(y, y, p);
    int64_t rhs = ec_mod(ec_mulmod(x, ec_mulmod(x, x, p), p) + ec_mulmod(a, x, p) + b, p);
    if (lhs != rhs) return nql_val_null();
    return ec_make_point(x, y);
}

/** EC_INFINITY() -- point at infinity */
static NqlValue nql_fn_ec_infinity(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)args; (void)argc; (void)ctx;
    return ec_make_inf();
}

/** EC_IS_ON_CURVE(curve, point) */
static NqlValue nql_fn_ec_is_on(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || args[1].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* c = args[0].v.array; NqlArray* pt = args[1].v.array;
    if (!c || c->length < 3) return nql_val_null();
    if (ec_is_inf(pt)) return nql_val_bool(true);
    if (pt->length < 2) return nql_val_bool(false);
    int64_t a = nql_val_as_int(&c->items[0]), b = nql_val_as_int(&c->items[1]), p = nql_val_as_int(&c->items[2]);
    int64_t x = ec_mod(nql_val_as_int(&pt->items[0]), p);
    int64_t y = ec_mod(nql_val_as_int(&pt->items[1]), p);
    int64_t lhs = ec_mulmod(y, y, p);
    int64_t rhs = ec_mod(ec_mulmod(x, ec_mulmod(x, x, p), p) + ec_mulmod(a, x, p) + b, p);
    return nql_val_bool(lhs == rhs);
}

/** EC_ADD(curve, P, Q) -- point addition */
static NqlValue nql_fn_ec_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || args[1].type != NQL_VAL_ARRAY || args[2].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* c = args[0].v.array; NqlArray* P = args[1].v.array; NqlArray* Q = args[2].v.array;
    if (!c || c->length < 3) return nql_val_null();
    int64_t a = nql_val_as_int(&c->items[0]), p = nql_val_as_int(&c->items[2]);
    if (ec_is_inf(P)) return nql_val_array(Q);
    if (ec_is_inf(Q)) return nql_val_array(P);
    if (P->length < 2 || Q->length < 2) return nql_val_null();
    int64_t x1 = ec_mod(nql_val_as_int(&P->items[0]), p), y1 = ec_mod(nql_val_as_int(&P->items[1]), p);
    int64_t x2 = ec_mod(nql_val_as_int(&Q->items[0]), p), y2 = ec_mod(nql_val_as_int(&Q->items[1]), p);
    return ec_add_pts(x1, y1, x2, y2, a, p);
}

/** EC_NEGATE(curve, P) -- point negation */
static NqlValue nql_fn_ec_negate(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || args[1].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* c = args[0].v.array; NqlArray* pt = args[1].v.array;
    if (!c || c->length < 3 || ec_is_inf(pt)) return ec_make_inf();
    if (pt->length < 2) return nql_val_null();
    int64_t p = nql_val_as_int(&c->items[2]);
    int64_t x = ec_mod(nql_val_as_int(&pt->items[0]), p);
    int64_t y = ec_mod(-nql_val_as_int(&pt->items[1]), p);
    return ec_make_point(x, y);
}

/** EC_SCALAR_MUL(curve, n, P) -- scalar multiplication nP */
static NqlValue nql_fn_ec_scalar_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_ARRAY || args[2].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* c = args[0].v.array;
    if (!c || c->length < 3) return nql_val_null();
    int64_t n = nql_val_as_int(&args[1]);
    int64_t a_coeff = nql_val_as_int(&c->items[0]), p = nql_val_as_int(&c->items[2]);
    NqlArray* P = args[2].v.array;
    if (ec_is_inf(P) || n == 0) return ec_make_inf();

    bool neg = false;
    if (n < 0) { neg = true; n = -n; }

    /* Double-and-add */
    int64_t rx = -1, ry = -1; /* result = infinity */
    bool res_inf = true;
    int64_t bx = ec_mod(nql_val_as_int(&P->items[0]), p);
    int64_t by = ec_mod(nql_val_as_int(&P->items[1]), p);
    if (neg) by = ec_mod(-by, p);

    while (n > 0) {
        if (n & 1) {
            if (res_inf) { rx = bx; ry = by; res_inf = false; }
            else {
                NqlValue sum = ec_add_pts(rx, ry, bx, by, a_coeff, p);
                if (sum.type == NQL_VAL_ARRAY && sum.v.array) {
                    if (ec_is_inf(sum.v.array)) { res_inf = true; }
                    else { rx = nql_val_as_int(&sum.v.array->items[0]); ry = nql_val_as_int(&sum.v.array->items[1]); }
                }
            }
        }
        NqlValue dbl = ec_add_pts(bx, by, bx, by, a_coeff, p);
        if (dbl.type == NQL_VAL_ARRAY && dbl.v.array && !ec_is_inf(dbl.v.array)) {
            bx = nql_val_as_int(&dbl.v.array->items[0]); by = nql_val_as_int(&dbl.v.array->items[1]);
        } else break;
        n >>= 1;
    }
    return res_inf ? ec_make_inf() : ec_make_point(rx, ry);
}

/** EC_ORDER(curve) -- group order via counting points (small primes) */
static NqlValue nql_fn_ec_order(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length < 3) return nql_val_null();
    NqlArray* c = args[0].v.array;
    int64_t a = nql_val_as_int(&c->items[0]), b = nql_val_as_int(&c->items[1]), p = nql_val_as_int(&c->items[2]);
    if (p > 100000) return nql_val_null(); /* Too large for naive count */
    int64_t count = 1; /* point at infinity */
    for (int64_t x = 0; x < p; x++) {
        int64_t rhs = ec_mod(ec_mulmod(x, ec_mulmod(x, x, p), p) + ec_mulmod(a, x, p) + b, p);
        if (rhs == 0) { count++; continue; }
        /* Euler criterion: rhs^((p-1)/2) mod p */
        int64_t exp = (p - 1) / 2, base = rhs, res = 1;
        while (exp > 0) { if (exp & 1) res = ec_mulmod(res, base, p); base = ec_mulmod(base, base, p); exp >>= 1; }
        if (res == 1) count += 2; /* two square roots */
    }
    return nql_val_int(count);
}

/** EC_RANDOM_POINT(curve) -- random point on curve */
static NqlValue nql_fn_ec_random_point(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array || args[0].v.array->length < 3) return nql_val_null();
    NqlArray* c = args[0].v.array;
    int64_t a = nql_val_as_int(&c->items[0]), b = nql_val_as_int(&c->items[1]), p = nql_val_as_int(&c->items[2]);
    for (int attempt = 0; attempt < 1000; attempt++) {
        int64_t x = rand() % p;
        int64_t rhs = ec_mod(ec_mulmod(x, ec_mulmod(x, x, p), p) + ec_mulmod(a, x, p) + b, p);
        if (rhs == 0) return ec_make_point(x, 0);
        /* Check if QR */
        int64_t exp = (p - 1) / 2, base = rhs, res = 1;
        while (exp > 0) { if (exp & 1) res = ec_mulmod(res, base, p); base = ec_mulmod(base, base, p); exp >>= 1; }
        if (res == 1) {
            /* Tonelli-Shanks simplified for p === 3 mod 4 */
            int64_t y;
            if (p % 4 == 3) {
                exp = (p + 1) / 4; base = rhs; y = 1;
                while (exp > 0) { if (exp & 1) y = ec_mulmod(y, base, p); base = ec_mulmod(base, base, p); exp >>= 1; }
            } else {
                /* Brute search for small p */
                y = -1;
                for (int64_t t = 0; t < p; t++) if (ec_mulmod(t, t, p) == rhs) { y = t; break; }
                if (y < 0) continue;
            }
            return ec_make_point(x, y);
        }
    }
    return ec_make_inf();
}

/** EC_DISCRETE_LOG(curve, P, Q, order) -- find n where Q = nP (baby-step giant-step) */
static NqlValue nql_fn_ec_dlog(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc; NQL_NULL_GUARD4(args);
    if (args[0].type != NQL_VAL_ARRAY || args[1].type != NQL_VAL_ARRAY || args[2].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* c = args[0].v.array;
    if (!c || c->length < 3) return nql_val_null();
    int64_t order = nql_val_as_int(&args[3]);
    if (order < 1 || order > 1000000) return nql_val_null();
    int64_t a_coeff = nql_val_as_int(&c->items[0]), p = nql_val_as_int(&c->items[2]);
    NqlArray* P = args[1].v.array; NqlArray* Q = args[2].v.array;
    if (ec_is_inf(P)) return nql_val_null();

    int64_t m = (int64_t)ceil(sqrt((double)order));
    /* Baby steps: store jP for j = 0..m-1 */
    int64_t* bx_arr = (int64_t*)malloc(m * sizeof(int64_t));
    int64_t* by_arr = (int64_t*)malloc(m * sizeof(int64_t));
    bool* binf = (bool*)calloc(m, sizeof(bool));
    if (!bx_arr || !by_arr || !binf) { free(bx_arr); free(by_arr); free(binf); return nql_val_null(); }
    
    binf[0] = true; /* 0*P = inf */
    int64_t cx = ec_mod(nql_val_as_int(&P->items[0]), p);
    int64_t cy = ec_mod(nql_val_as_int(&P->items[1]), p);
    bx_arr[0] = 0; by_arr[0] = 0;
    for (int64_t j = 1; j < m; j++) {
        if (j == 1) { bx_arr[j] = cx; by_arr[j] = cy; binf[j] = false; }
        else {
            if (binf[j - 1]) { bx_arr[j] = cx; by_arr[j] = cy; binf[j] = false; }
            else {
                NqlValue sum = ec_add_pts(bx_arr[j - 1], by_arr[j - 1], cx, cy, a_coeff, p);
                if (sum.type == NQL_VAL_ARRAY && sum.v.array && !ec_is_inf(sum.v.array)) {
                    bx_arr[j] = nql_val_as_int(&sum.v.array->items[0]);
                    by_arr[j] = nql_val_as_int(&sum.v.array->items[1]);
                    binf[j] = false;
                } else { binf[j] = true; }
            }
        }
    }

    /* Giant step: -mP */
    NqlValue curve_v = args[0];
    NqlValue mP_args[3] = {curve_v, nql_val_int(m), args[1]};
    NqlValue mP = nql_fn_ec_scalar_mul(mP_args, 3, ctx);
    /* Negate mP */
    NqlValue neg_mP_args[2] = {curve_v, mP};
    NqlValue neg_mP = nql_fn_ec_negate(neg_mP_args, 2, ctx);

    /* gamma = Q */
    int64_t gx, gy; bool ginf;
    if (ec_is_inf(Q)) { ginf = true; gx = gy = 0; }
    else { ginf = false; gx = ec_mod(nql_val_as_int(&Q->items[0]), p); gy = ec_mod(nql_val_as_int(&Q->items[1]), p); }

    int64_t result = -1;
    for (int64_t i = 0; i <= m; i++) {
        /* Check if gamma matches any baby step */
        for (int64_t j = 0; j < m; j++) {
            if (ginf && binf[j]) { result = i * m + j; break; }
            if (!ginf && !binf[j] && gx == bx_arr[j] && gy == by_arr[j]) { result = i * m + j; break; }
        }
        if (result >= 0) break;
        /* gamma += neg_mP */
        if (neg_mP.type == NQL_VAL_ARRAY && neg_mP.v.array && !ec_is_inf(neg_mP.v.array)) {
            int64_t nx = nql_val_as_int(&neg_mP.v.array->items[0]);
            int64_t ny = nql_val_as_int(&neg_mP.v.array->items[1]);
            if (ginf) { gx = nx; gy = ny; ginf = false; }
            else {
                NqlValue sum = ec_add_pts(gx, gy, nx, ny, a_coeff, p);
                if (sum.type == NQL_VAL_ARRAY && sum.v.array && !ec_is_inf(sum.v.array)) {
                    gx = nql_val_as_int(&sum.v.array->items[0]); gy = nql_val_as_int(&sum.v.array->items[1]);
                } else { ginf = true; }
            }
        }
    }

    free(bx_arr); free(by_arr); free(binf);
    return (result >= 0) ? nql_val_int(result) : nql_val_null();
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_elliptic_register_functions(void) {
    nql_register_func("EC_CURVE",        nql_fn_ec_curve,        3, 3, "Create curve y^2=x^3+ax+b mod p: [a,b,p]");
    nql_register_func("EC_POINT",        nql_fn_ec_point,        3, 3, "Create/validate point on curve");
    nql_register_func("EC_INFINITY",     nql_fn_ec_infinity,     0, 0, "Point at infinity [0]");
    nql_register_func("EC_IS_ON_CURVE",  nql_fn_ec_is_on,       2, 2, "Test if point lies on curve");
    nql_register_func("EC_ADD",          nql_fn_ec_add,          3, 3, "Point addition P+Q");
    nql_register_func("EC_NEGATE",       nql_fn_ec_negate,       2, 2, "Point negation -P");
    nql_register_func("EC_SCALAR_MUL",   nql_fn_ec_scalar_mul,   3, 3, "Scalar multiplication nP");
    nql_register_func("EC_ORDER",        nql_fn_ec_order,        1, 1, "Group order (small primes only)");
    nql_register_func("EC_RANDOM_POINT", nql_fn_ec_random_point, 1, 1, "Random point on curve");
    nql_register_func("EC_DISCRETE_LOG", nql_fn_ec_dlog,         4, 4, "Baby-step giant-step ECDLP");
}
