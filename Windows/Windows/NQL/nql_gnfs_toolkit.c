/**
 * nql_gnfs_toolkit.c - GNFS mathematical building blocks for NQL
 * 
 * Provides the algebraic number theory primitives needed to construct
 * a General Number Field Sieve in compact NQL scripts:
 *
 *   1. Modular arithmetic (ADDMOD, SUBMOD, KRONECKER, ORDER_MOD, PRIMITIVE_ROOT, CRT_PAIR)
 *   2. Prime generation and smooth-number tools (PRIMES_UP_TO, SMOOTH_FACTOR, TRIAL_DIVIDE, SMOOTHNESS_BOUND)
 *   3. Factor-base construction (FACTOR_BASE_POLY)
 *   4. Polynomial helpers (POLY_EVAL_MOD, POLY_DEGREE, POLY_LEADING, POLY_COEFF, POLY_CONTENT,
 *      POLY_HENSEL_LIFT, POLY_TRANSLATE, POLY_FROM_ROOTS_MOD)
 *   5. Lattice reduction (LATTICE_REDUCE_2D)
 *   6. GNFS parameter helpers (GNFS_OPTIMAL_DEGREE, GNFS_SMOOTHNESS_BOUND)
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);
NqlValue nql_val_float(double f);
NqlValue nql_val_int(int64_t i);
NqlValue nql_val_null(void);
NqlValue nql_val_bool(bool b);
static NqlArray* nql_array_alloc(uint32_t capacity);
static bool nql_array_push(NqlArray* a, NqlValue val);
static NqlValue nql_val_array(NqlArray* a);
static int64_t nql_val_as_int(const NqlValue* v);
static uint64_t nql_mulmod64(uint64_t a, uint64_t b, uint64_t m);
static uint64_t nql_powmod64(uint64_t base, uint64_t exp, uint64_t m);

/* ================================================================
 * S1  MODULAR ARITHMETIC
 * ================================================================ */

static NqlValue nql_fn_addmod(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null(); NQL_NULL_GUARD3(args);
    uint64_t a = (uint64_t)nql_val_as_int(&args[0]);
    uint64_t b = (uint64_t)nql_val_as_int(&args[1]);
    uint64_t m = (uint64_t)nql_val_as_int(&args[2]);
    if (m == 0) return nql_val_null();
    a %= m; b %= m;
    uint64_t r = a + b;
    if (r >= m || r < a) r -= m;
    return nql_val_int((int64_t)r);
}

static NqlValue nql_fn_submod(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null(); NQL_NULL_GUARD3(args);
    uint64_t a = (uint64_t)nql_val_as_int(&args[0]);
    uint64_t b = (uint64_t)nql_val_as_int(&args[1]);
    uint64_t m = (uint64_t)nql_val_as_int(&args[2]);
    if (m == 0) return nql_val_null();
    a %= m; b %= m;
    return nql_val_int((int64_t)((a >= b) ? (a - b) : (m - b + a)));
}

static NqlValue nql_fn_kronecker(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null(); NQL_NULL_GUARD2(args);
    int64_t a = nql_val_as_int(&args[0]);
    int64_t n = nql_val_as_int(&args[1]);
    if (n == 0) return nql_val_int((a == 1 || a == -1) ? 1 : 0);
    if (n == 1) return nql_val_int(1);
    int result = 1;
    if (n < 0) { n = -n; if (a < 0) result = -result; }
    int v = 0;
    while ((n & 1) == 0) { n >>= 1; v++; }
    if (v & 1) { int64_t a8 = ((a % 8) + 8) % 8; if (a8 == 3 || a8 == 5) result = -result; }
    if (n == 1) return nql_val_int(result);
    a = ((a % n) + n) % n;
    while (a != 0) {
        while ((a & 1) == 0) {
            a >>= 1;
            int64_t n8 = n % 8;
            if (n8 == 3 || n8 == 5) result = -result;
        }
        int64_t tmp = a; a = n; n = tmp;
        if ((a % 4) == 3 && (n % 4) == 3) result = -result;
        a %= n;
    }
    return nql_val_int(n == 1 ? result : 0);
}

static NqlValue nql_fn_crt_pair(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 4) return nql_val_null(); NQL_NULL_GUARD4(args);
    int64_t r1 = nql_val_as_int(&args[0]), m1 = nql_val_as_int(&args[1]);
    int64_t r2 = nql_val_as_int(&args[2]), m2 = nql_val_as_int(&args[3]);
    if (m1 <= 0 || m2 <= 0) return nql_val_null();
    int64_t a = m1, b = m2, x0 = 1, x1 = 0;
    while (b) { int64_t q = a / b, t = b; b = a - q * b; a = t; t = x1; x1 = x0 - q * x1; x0 = t; }
    int64_t g = a;
    if ((r2 - r1) % g != 0) return nql_val_null();
    int64_t lcm = m1 / g * m2;
    int64_t m2g = m2 / g;
    int64_t diff = (r2 - r1) / g;
    int64_t k = ((diff % m2g) * (x0 % m2g) % m2g + m2g) % m2g;
    int64_t result = r1 + m1 * k;
    result = ((result % lcm) + lcm) % lcm;
    return nql_val_int(result);
}

/* ================================================================
 * S2  PRIME GENERATION & SMOOTH-NUMBER TOOLS
 * ================================================================ */

static NqlValue nql_fn_primes_up_to(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null(); NQL_NULL_GUARD1(args);
    int64_t B = nql_val_as_int(&args[0]);
    if (B < 2 || B > 10000000) return nql_val_null();
    uint32_t n = (uint32_t)B;
    uint8_t* sieve = (uint8_t*)calloc(n + 1, 1);
    if (!sieve) return nql_val_null();
    for (uint32_t i = 2; (uint64_t)i * i <= n; i++)
        if (!sieve[i])
            for (uint32_t j = i * i; j <= n; j += i) sieve[j] = 1;
    uint32_t count = 0;
    for (uint32_t i = 2; i <= n; i++) if (!sieve[i]) count++;
    NqlArray* result = nql_array_alloc(count);
    if (!result) { free(sieve); return nql_val_null(); }
    for (uint32_t i = 2; i <= n; i++)
        if (!sieve[i]) nql_array_push(result, nql_val_int((int64_t)i));
    free(sieve);
    return nql_val_array(result);
}

static NqlValue nql_fn_trial_divide(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null(); NQL_NULL_GUARD2(args);
    int64_t ns = nql_val_as_int(&args[0]);
    int64_t B = nql_val_as_int(&args[1]);
    if (B < 2) return nql_val_null();
    uint64_t n = (ns < 0) ? (uint64_t)(-ns) : (uint64_t)ns;
    if (n <= 1) return nql_val_null();
    NqlArray* factors = nql_array_alloc(32);
    if (!factors) return nql_val_null();
    for (uint64_t p = 2; p <= (uint64_t)B && p * p <= n; p++) {
        if (n % p == 0) {
            int64_t e = 0;
            while (n % p == 0) { n /= p; e++; }
            NqlArray* pair = nql_array_alloc(2);
            if (!pair) return nql_val_null();
            nql_array_push(pair, nql_val_int((int64_t)p));
            nql_array_push(pair, nql_val_int(e));
            nql_array_push(factors, nql_val_array(pair));
        }
    }
    if (n > 1 && n <= (uint64_t)B) {
        NqlArray* pair = nql_array_alloc(2);
        if (pair) { nql_array_push(pair, nql_val_int((int64_t)n)); nql_array_push(pair, nql_val_int(1)); nql_array_push(factors, nql_val_array(pair)); n = 1; }
    }
    NqlArray* result = nql_array_alloc(2);
    if (!result) return nql_val_null();
    nql_array_push(result, nql_val_array(factors));
    nql_array_push(result, nql_val_int((int64_t)n));
    return nql_val_array(result);
}

static NqlValue nql_fn_smooth_factor(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null(); NQL_NULL_GUARD2(args);
    int64_t ns = nql_val_as_int(&args[0]);
    int64_t B = nql_val_as_int(&args[1]);
    if (B < 2) return nql_val_null();
    uint64_t n = (ns < 0) ? (uint64_t)(-ns) : (uint64_t)ns;
    if (n <= 1) { NqlArray* e = nql_array_alloc(0); return e ? nql_val_array(e) : nql_val_null(); }
    NqlArray* factors = nql_array_alloc(32);
    if (!factors) return nql_val_null();
    for (uint64_t p = 2; p <= (uint64_t)B; p++) {
        if (n % p == 0) {
            int64_t e = 0;
            while (n % p == 0) { n /= p; e++; }
            NqlArray* pair = nql_array_alloc(2);
            if (!pair) return nql_val_null();
            nql_array_push(pair, nql_val_int((int64_t)p));
            nql_array_push(pair, nql_val_int(e));
            nql_array_push(factors, nql_val_array(pair));
        }
        if (n == 1) break;
    }
    if (n != 1) return nql_val_null();
    return nql_val_array(factors);
}

static NqlValue nql_fn_smoothness_bound(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc < 1) return nql_val_null();
    double N;
    if (args[0].type == NQL_VAL_INT) N = (double)args[0].v.ival;
    else if (args[0].type == NQL_VAL_FLOAT) N = args[0].v.fval;
    else return nql_val_null();
    if (N < 2.0) return nql_val_null();
    double ln_N = log(N);
    double ln_ln_N = log(ln_N);
    double c = pow(64.0 / 9.0, 1.0 / 3.0);
    double L = exp(c * pow(ln_N, 1.0 / 3.0) * pow(ln_ln_N, 2.0 / 3.0));
    return nql_val_int((int64_t)pow(L, 1.0 / sqrt(2.0)));
}

static NqlValue nql_fn_factor_base_poly(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_null();
    int64_t B = nql_val_as_int(&args[1]);
    if (B < 2 || B > 10000000) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    uint32_t n = (uint32_t)B;
    uint8_t* sieve = (uint8_t*)calloc(n + 1, 1);
    if (!sieve) return nql_val_null();
    for (uint32_t i = 2; (uint64_t)i * i <= n; i++)
        if (!sieve[i]) for (uint32_t j = i * i; j <= n; j += i) sieve[j] = 1;
    NqlArray* result = nql_array_alloc(256);
    if (!result) { free(sieve); return nql_val_null(); }
    for (uint32_t p = 2; p <= n; p++) {
        if (sieve[p]) continue;
        for (uint32_t r = 0; r < p; r++) {
            /* Horner eval mod p */
            int64_t val = 0;
            for (int32_t k = (int32_t)f->degree; k >= 0; k--)
                val = ((val * (int64_t)r) % (int64_t)p + (f->coeffs[k] % (int64_t)p + (int64_t)p)) % (int64_t)p;
            if (val == 0) {
                NqlArray* pair = nql_array_alloc(2);
                if (pair) {
                    nql_array_push(pair, nql_val_int((int64_t)p));
                    nql_array_push(pair, nql_val_int((int64_t)r));
                    nql_array_push(result, nql_val_array(pair));
                }
            }
        }
    }
    free(sieve);
    return nql_val_array(result);
}

/* ================================================================
 * S3  POLYNOMIAL HELPERS (only functions not already in nql_poly.c)
 * ================================================================ */

static NqlValue nql_fn_poly_content_tk(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1 || args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    int64_t g = 0;
    for (uint32_t i = 0; i <= f->degree; i++) {
        int64_t c = f->coeffs[i]; if (c < 0) c = -c;
        if (g == 0) g = c;
        else { while (c) { int64_t t = c; c = g % c; g = t; } }
    }
    return nql_val_int(g);
}

/** POLY_TRANSLATE(poly, c) -- compute f(x + c) via iterated Horner shift */
static NqlValue nql_fn_poly_translate_tk(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    if (argc != 2 || args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    int64_t c = nql_val_as_int(&args[1]);
    uint32_t d = f->degree;
    int64_t* co = (int64_t*)malloc((d + 1) * sizeof(int64_t));
    if (!co) return nql_val_null();
    memcpy(co, f->coeffs, (d + 1) * sizeof(int64_t));
    /* Taylor shift: d passes of synthetic substitution */
    for (uint32_t step = 0; step < d; step++)
        for (int32_t i = (int32_t)d - 1; i >= (int32_t)step; i--)
            co[i] += c * co[i + 1];
    NqlPoly* rp = nql_poly_alloc((int)d);
    if (!rp) { free(co); return nql_val_null(); }
    memcpy(rp->coeffs, co, (d + 1) * sizeof(int64_t));
    free(co);
    return nql_val_poly(rp);
}

/** POLY_HENSEL_LIFT(poly, root, p, e) -- lift root mod p to root mod p^e */
static NqlValue nql_fn_poly_hensel_lift_tk(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD4(args);
    if (argc != 4 || args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    int64_t root = nql_val_as_int(&args[1]);
    int64_t p = nql_val_as_int(&args[2]);
    int64_t e = nql_val_as_int(&args[3]);
    if (p < 2 || e < 1 || e > 30) return nql_val_null();
    int64_t mod = p;
    int64_t r = ((root % mod) + mod) % mod;
    /* Validate: root must be a root of f mod p */
    { int64_t check = 0;
      for (int32_t i = (int32_t)f->degree; i >= 0; i--)
          check = ((check * r) % p + ((f->coeffs[i] % p) + p) % p) % p;
      if (check != 0) return nql_val_null();
    }
    for (int64_t step = 1; step < e; step++) {
        int64_t next_mod = mod * p;
        /* Evaluate f(r) mod next_mod via Horner */
        int64_t fval = 0;
        for (int32_t i = (int32_t)f->degree; i >= 0; i--)
            fval = (fval * r + f->coeffs[i]) % next_mod;
        fval = ((fval % next_mod) + next_mod) % next_mod;
        /* Evaluate f'(r) mod p */
        int64_t fprime = 0;
        for (int32_t i = (int32_t)f->degree; i >= 1; i--)
            fprime = (fprime * r + f->coeffs[i] * (int64_t)i) % p;
        fprime = ((fprime % p) + p) % p;
        if (fprime == 0) return nql_val_null(); /* Singular lift */
        /* Modular inverse of f'(r) mod p */
        int64_t inv = 1;
        { int64_t aa = fprime, bb = p, x0 = 1, x1 = 0;
          while (bb) { int64_t q = aa / bb, t = bb; bb = aa - q * bb; aa = t; t = x1; x1 = x0 - q * x1; x0 = t; }
          inv = ((x0 % p) + p) % p;
        }
        /* Newton step: r = r - f(r) * inv(f'(r)) mod next_mod */
        int64_t adjust = (fval / mod % p * inv) % p;
        r = r - adjust * mod;
        r = ((r % next_mod) + next_mod) % next_mod;
        mod = next_mod;
    }
    return nql_val_int(r);
}

/** POLY_FROM_ROOTS_MOD(roots_array, p) -- (x-r1)(x-r2)...(x-rk) mod p */
static NqlValue nql_fn_poly_from_roots_mod_tk(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null(); NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* roots = args[0].v.array;
    int64_t p = nql_val_as_int(&args[1]);
    if (p < 2 || roots->length == 0 || roots->length > 100) return nql_val_null();
    uint32_t n = roots->length;
    /* Start with polynomial = [1] (constant 1, degree 0) */
    int64_t* co = (int64_t*)calloc(n + 1, sizeof(int64_t));
    if (!co) return nql_val_null();
    co[0] = 1;
    uint32_t deg = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t r = nql_val_as_int(&roots->items[i]);
        r = ((r % p) + p) % p;
        /* Multiply by (x - r): shift up and subtract */
        for (int32_t j = (int32_t)deg; j >= 0; j--) {
            co[j + 1] = (co[j + 1] + co[j]) % p;
            co[j] = ((-r * co[j]) % p + p) % p;
        }
        deg++;
    }
    NqlPoly* rp = nql_poly_alloc((int)deg);
    if (!rp) { free(co); return nql_val_null(); }
    memcpy(rp->coeffs, co, (deg + 1) * sizeof(int64_t));
    free(co);
    return nql_val_poly(rp);
}

/** POLY_TO_ARRAY(poly) -- extract coefficients as array [a0, a1, ..., ad] */
static NqlValue nql_fn_poly_to_array_tk(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1 || args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    NqlArray* result = nql_array_alloc(f->degree + 1);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i <= f->degree; i++)
        nql_array_push(result, nql_val_int(f->coeffs[i]));
    return nql_val_array(result);
}

/* ================================================================
 * S4  LATTICE REDUCTION
 * ================================================================ */

/** LATTICE_REDUCE_2D(v1x, v1y, v2x, v2y) -- Lagrange/Gauss 2D lattice reduction.
 *  Returns [u1x, u1y, u2x, u2y] with short reduced basis. */
static NqlValue nql_fn_lattice_reduce_2d(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 4) return nql_val_null(); NQL_NULL_GUARD4(args);
    double b1x = (double)nql_val_as_int(&args[0]);
    double b1y = (double)nql_val_as_int(&args[1]);
    double b2x = (double)nql_val_as_int(&args[2]);
    double b2y = (double)nql_val_as_int(&args[3]);
    /* Lagrange-Gauss reduction */
    double n1 = b1x * b1x + b1y * b1y;
    double n2 = b2x * b2x + b2y * b2y;
    if (n1 > n2) { double t; t = b1x; b1x = b2x; b2x = t; t = b1y; b1y = b2y; b2y = t; t = n1; n1 = n2; n2 = t; }
    for (int iter = 0; iter < 1000; iter++) {
        double dot = b1x * b2x + b1y * b2y;
        double mu = floor(dot / n1 + 0.5);
        if (mu == 0.0) break;
        b2x -= mu * b1x; b2y -= mu * b1y;
        n2 = b2x * b2x + b2y * b2y;
        if (n2 >= n1) break;
        double t; t = b1x; b1x = b2x; b2x = t; t = b1y; b1y = b2y; b2y = t; t = n1; n1 = n2; n2 = t;
    }
    NqlArray* result = nql_array_alloc(4);
    if (!result) return nql_val_null();
    nql_array_push(result, nql_val_int((int64_t)b1x));
    nql_array_push(result, nql_val_int((int64_t)b1y));
    nql_array_push(result, nql_val_int((int64_t)b2x));
    nql_array_push(result, nql_val_int((int64_t)b2y));
    return nql_val_array(result);
}

/** RATIONAL_RECONSTRUCT(a, m, bound) -- find x, y with x/y = a mod m, |x|,|y| <= bound.
 *  Returns [x, y] or null. Uses extended Euclidean algorithm. */
static NqlValue nql_fn_rational_reconstruct(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null(); NQL_NULL_GUARD3(args);
    int64_t a = nql_val_as_int(&args[0]);
    int64_t m = nql_val_as_int(&args[1]);
    int64_t bound = nql_val_as_int(&args[2]);
    if (m <= 0 || bound <= 0) return nql_val_null();
    a = ((a % m) + m) % m;
    int64_t r0 = m, r1 = a, s0 = 0, s1 = 1;
    while (r1 > bound) {
        int64_t q = r0 / r1;
        int64_t t;
        t = r1; r1 = r0 - q * r1; r0 = t;
        t = s1; s1 = s0 - q * s1; s0 = t;
    }
    int64_t x = r1, y = s1;
    if (y < 0) { x = -x; y = -y; }
    if (y > bound) return nql_val_null();
    NqlArray* result = nql_array_alloc(2);
    if (!result) return nql_val_null();
    nql_array_push(result, nql_val_int(x));
    nql_array_push(result, nql_val_int(y));
    return nql_val_array(result);
}

/* ================================================================
 * S5  GNFS PARAMETER HELPERS
 * ================================================================ */

/** GNFS_OPTIMAL_DEGREE(digits) -- suggested polynomial degree for N with given digit count */
static NqlValue nql_fn_gnfs_optimal_degree(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null(); NQL_NULL_GUARD1(args);
    int64_t digits = nql_val_as_int(&args[0]);
    /* Standard heuristic thresholds */
    if (digits <= 0) return nql_val_null();
    if (digits < 35) return nql_val_int(3);
    if (digits < 60) return nql_val_int(4);
    if (digits < 100) return nql_val_int(5);
    if (digits < 140) return nql_val_int(6);
    return nql_val_int(7);
}

/** GNFS_SMOOTHNESS_BOUND(N, d) -- L(N)^alpha for NFS with degree d.
 *  Optimal alpha ~ (2/(9d))^(1/3). */
static NqlValue nql_fn_gnfs_smoothness_bound(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null(); NQL_NULL_GUARD2(args);
    double N;
    if (args[0].type == NQL_VAL_INT) N = (double)args[0].v.ival;
    else if (args[0].type == NQL_VAL_FLOAT) N = args[0].v.fval;
    else return nql_val_null();
    int64_t d = nql_val_as_int(&args[1]);
    if (N < 2.0 || d < 2) return nql_val_null();
    double ln_N = log(N);
    double ln_ln_N = log(ln_N);
    double c = pow(64.0 / 9.0, 1.0 / 3.0);
    double L = exp(c * pow(ln_N, 1.0 / 3.0) * pow(ln_ln_N, 2.0 / 3.0));
    double alpha = pow(2.0 / (9.0 * (double)d), 1.0 / 3.0);
    return nql_val_int((int64_t)pow(L, alpha));
}

/** GNFS_SIEVE_RADIUS(N, d) -- suggested sieve half-width for lattice sieve */
static NqlValue nql_fn_gnfs_sieve_radius(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null(); NQL_NULL_GUARD2(args);
    double N;
    if (args[0].type == NQL_VAL_INT) N = (double)args[0].v.ival;
    else if (args[0].type == NQL_VAL_FLOAT) N = args[0].v.fval;
    else return nql_val_null();
    int64_t d = nql_val_as_int(&args[1]);
    if (N < 2.0 || d < 2) return nql_val_null();
    double digits = log10(N);
    /* Heuristic: sieve radius ~ B^(1.5) where B is smoothness bound */
    double ln_N = log(N);
    double ln_ln_N = log(ln_N);
    double c = pow(64.0 / 9.0, 1.0 / 3.0);
    double L = exp(c * pow(ln_N, 1.0 / 3.0) * pow(ln_ln_N, 2.0 / 3.0));
    double alpha = pow(2.0 / (9.0 * (double)d), 1.0 / 3.0);
    double B = pow(L, alpha);
    double radius = pow(B, 1.2);
    if (radius < 1000) radius = 1000;
    if (radius > 1e8) radius = 1e8;
    return nql_val_int((int64_t)radius);
}

/** EXPONENT_VECTOR(factor_pairs, fb_primes) -- build GF(2) exponent vector.
 *  factor_pairs: [[p1,e1],[p2,e2],...], fb_primes: [p1,p2,...,pk].
 *  Returns array of 0/1 (exponents mod 2). */
static NqlValue nql_fn_exponent_vector(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* pairs = args[0].v.array;
    NqlArray* fb = args[1].v.array;
    NqlArray* result = nql_array_alloc(fb->length);
    if (!result) return nql_val_null();
    /* Initialize to zero */
    for (uint32_t i = 0; i < fb->length; i++)
        nql_array_push(result, nql_val_int(0));
    /* Fill in exponents mod 2 */
    for (uint32_t i = 0; i < pairs->length; i++) {
        if (pairs->items[i].type != NQL_VAL_ARRAY || !pairs->items[i].v.array) continue;
        NqlArray* pair = pairs->items[i].v.array;
        if (pair->length < 2) continue;
        int64_t p = nql_val_as_int(&pair->items[0]);
        int64_t e = nql_val_as_int(&pair->items[1]);
        /* Find p in factor base */
        for (uint32_t j = 0; j < fb->length; j++) {
            if (nql_val_as_int(&fb->items[j]) == p) {
                result->items[j] = nql_val_int(e % 2);
                break;
            }
        }
    }
    return nql_val_array(result);
}

/** LOG_NORM_POLY(a, b, poly) -- log2(|f_hom(a,b)|), the logarithmic norm for sieve initialisation */
static NqlValue nql_fn_log_norm_poly(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null(); NQL_NULL_GUARD3(args);
    if (args[2].type != NQL_VAL_POLY || !args[2].v.qs_ptr) return nql_val_null();
    double a = (double)nql_val_as_int(&args[0]);
    double b = (double)nql_val_as_int(&args[1]);
    NqlPoly* f = (NqlPoly*)args[2].v.qs_ptr;
    /* Evaluate homogeneous polynomial: sum c_i * a^i * b^(d-i) */
    double val = 0.0;
    double ai = 1.0, bd = pow(b, (double)f->degree);
    double b_inv = (b != 0.0) ? (a / b) : 0.0;
    /* Use Horner in a/b then multiply by b^d for stability */
    if (fabs(b) > 0.0) {
        double ratio = a / b;
        double hval = 0.0;
        for (int32_t i = (int32_t)f->degree; i >= 0; i--)
            hval = hval * ratio + (double)f->coeffs[i];
        val = hval * bd;
    } else {
        val = (double)f->coeffs[f->degree] * pow(a, (double)f->degree);
    }
    if (val == 0.0) return nql_val_float(0.0);
    return nql_val_float(log2(fabs(val)));
}

/* ================================================================
 * REGISTRATION
 * ================================================================ */

void nql_gnfs_toolkit_register(void) {
    /* Modular arithmetic */
    nql_register_func("ADDMOD",              nql_fn_addmod,              3, 3, "(a+b) mod m, overflow-safe");
    nql_register_func("SUBMOD",              nql_fn_submod,              3, 3, "(a-b) mod m, overflow-safe");
    nql_register_func("KRONECKER",           nql_fn_kronecker,           2, 2, "Kronecker symbol (a/n)");
    nql_register_func("CRT_PAIR",            nql_fn_crt_pair,            4, 4, "CRT for two congruences");

    /* Prime generation & smooth-number tools */
    nql_register_func("PRIMES_UP_TO",        nql_fn_primes_up_to,        1, 1, "Sieve primes up to B (max 10^7)");
    nql_register_func("TRIAL_DIVIDE",        nql_fn_trial_divide,        2, 2, "Trial divide n by primes <= B -> [[p,e]..., cofactor]");
    nql_register_func("SMOOTH_FACTOR",       nql_fn_smooth_factor,       2, 2, "Full factorisation if B-smooth, else null");
    nql_register_func("SMOOTHNESS_BOUND",    nql_fn_smoothness_bound,    1, 1, "NFS L(N)^(1/sqrt(2)) optimal bound");
    nql_register_func("FACTOR_BASE_POLY",    nql_fn_factor_base_poly,    2, 2, "Primes <= B with poly roots -> [[p,root],...]");

    /* Polynomial helpers (new, not duplicating nql_poly.c) */
    nql_register_func("POLY_CONTENT",        nql_fn_poly_content_tk,        1, 1, "GCD of all coefficients");
    nql_register_func("POLY_TRANSLATE",      nql_fn_poly_translate_tk,      2, 2, "Compute f(x+c)");
    nql_register_func("POLY_HENSEL_LIFT",    nql_fn_poly_hensel_lift_tk,    4, 4, "Lift root mod p to mod p^e");
    nql_register_func("POLY_FROM_ROOTS_MOD", nql_fn_poly_from_roots_mod_tk, 2, 2, "Build poly from roots mod p");
    nql_register_func("POLY_TO_ARRAY",       nql_fn_poly_to_array_tk,       1, 1, "Extract coefficients as array");

    /* Lattice & reconstruction */
    nql_register_func("LATTICE_REDUCE_2D",   nql_fn_lattice_reduce_2d,   4, 4, "Lagrange-Gauss 2D lattice reduction");
    nql_register_func("RATIONAL_RECONSTRUCT",nql_fn_rational_reconstruct,3, 3, "Find x/y = a mod m with |x|,|y| <= bound");

    /* GNFS parameter helpers */
    nql_register_func("GNFS_OPTIMAL_DEGREE", nql_fn_gnfs_optimal_degree, 1, 1, "Suggested poly degree for digit count");
    nql_register_func("GNFS_SMOOTHNESS_BOUND_D",nql_fn_gnfs_smoothness_bound,2,2,"NFS smoothness bound for N with degree d");
    nql_register_func("GNFS_SIEVE_RADIUS",   nql_fn_gnfs_sieve_radius,   2, 2, "Suggested sieve half-width");
    nql_register_func("EXPONENT_VECTOR",     nql_fn_exponent_vector,     2, 2, "Build GF(2) exponent vector from factors + FB");
    nql_register_func("LOG_NORM_POLY",       nql_fn_log_norm_poly,       3, 3, "log2(|f_hom(a,b)|) for sieve norms");
}
