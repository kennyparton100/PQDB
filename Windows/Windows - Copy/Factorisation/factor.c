/**
 * cpss_factor.c - Shared factorisation infrastructure and auto-routers.
 * Part of the CPSS Viewer amalgamation.
 *
 * Structure:
 *   1. Shared helpers: factor_result_*, tuning defaults, xorshift, split helpers,
 *      BigNum modular arithmetic helpers (bn_mod_*, bn_mod_inverse_checked, etc.)
 *   2. #include of per-method files: Rho/rho.c, ECM/ecm.c, Fermat/fermat.c,
 *      TrialDivision/trial_division.c, PMinus1/pminus1.c, PPlus1/pplus1.c
 *   3. Auto-routers: cpss_db_factor_auto, cpss_factor_auto,
 *      cpss_factor_auto_u128, cpss_factor_auto_bn
 *   4. Single-purpose queries: cpss_smallest_factor, cpss_is_smooth
 *   5. Print helpers: factor_result_print, factor_result_u128_print,
 *      factor_result_bn_print, factor_result_bn_print_recursive
 *   6. U128 factor result helpers: factor_result_u128_add, factor_result_u128_init
 *
 * Individual factorisation methods live in their own files under Factorisation/.
 * See factor copy.c for the pre-refactor monolithic version.
 */

/* Forward declarations for U128 factor result helpers (defined later in this file) */
static void factor_result_u128_add(FactorResultU128* fr, uint64_t p);
static void factor_result_u128_init(FactorResultU128* fr, U128 n, const char* method);
static FactorResultBigNum cpss_factor_pminus1_bn(CPSSStream* s, const BigNum* n, uint64_t B1);
static FactorResultBigNum cpss_factor_auto_bn(CPSSStream* s, const BigNum* n);
static FactorResultBigNum cpss_factor_ecm_bn_ex(const BigNum* n, uint64_t B1, uint64_t B2,
                                                   uint64_t curves, uint64_t seed);
static FactorResultBigNum cpss_factor_ecm_mont(const BigNum* n, uint64_t B1, uint64_t B2,
                                                 uint64_t curves, uint64_t seed);
/* Forward declarations for methods in separate files (included after cpss_factor.c) */
static FactorResult cpss_factor_squfof(uint64_t n);
static FactorResult cpss_factor_qs_u64(uint64_t N);

/** Helper: add a factor to a FactorResult, merging if already present. */
static void factor_result_add(FactorResult* fr, uint64_t p) {
    for (size_t i = 0u; i < fr->factor_count; ++i) {
        if (fr->factors[i] == p) {
            ++fr->exponents[i];
            return;
        }
    }
    if (fr->factor_count < 64u) {
        fr->factors[fr->factor_count] = p;
        fr->exponents[fr->factor_count] = 1u;
        ++fr->factor_count;
    }
}

/** Helper: initialise a FactorResult for input n. */
static void factor_result_init(FactorResult* fr, uint64_t n, const char* method) {
    memset(fr, 0, sizeof(*fr));
    fr->n = n;
    fr->cofactor = n;
    fr->method = method;
    fr->status = (int)CPSS_OK;
}

/* Default tuning helpers for the standalone engines.
 * These centralize the fallback budgets used when callers pass zero. */
static uint64_t fermat_default_steps_small(uint64_t max_steps) {
    return max_steps > 0u ? max_steps : 1048576u;
}

static uint64_t fermat_default_steps_bn(uint64_t max_steps) {
    return max_steps > 0u ? max_steps : 65536u;
}

static uint64_t rho_default_iterations(uint64_t max_iterations) {
    return max_iterations > 0u ? max_iterations : 131072u;
}

static uint64_t rho_default_seed(uint64_t seed) {
    return seed != 0u ? seed : 0x9E3779B97F4A7C15ull;
}

static uint64_t ecm_default_B1(uint64_t B1) {
    return B1 > 0u ? B1 : 5000u;
}

static uint64_t ecm_default_curves(uint64_t curves) {
    return curves > 0u ? curves : 8u;
}

static uint64_t ecm_default_seed(uint64_t seed) {
    return seed != 0u ? seed : 0xD1B54A32D192ED03ull;
}

static uint64_t ecm_default_B2(uint64_t B2, uint64_t B1) {
    if (B2 > 0u) return B2;
    uint64_t auto_B2 = B1 * 100u;
    return (auto_B2 > 500000u) ? 500000u : auto_B2;
}

/** Small local xorshift generator used to seed randomized factor engines. */
static uint64_t factor_xorshift64(uint64_t* state) {
    uint64_t x = *state;
    x ^= x << 13u;
    x ^= x >> 7u;
    x ^= x << 17u;
    *state = x;
    return x;
}

/**
 * Tiered primality probe for BigNum values.
 * Uses the cheapest narrower-width test available before falling back to the
 * generic probable-prime routine.
 */
static bool bn_factor_is_prime(const BigNum* n) {
    if (bn_is_zero(n) || bn_is_one(n)) return false;
    if (bn_fits_u64(n)) return miller_rabin_u64(bn_to_u64(n));
    if (bn_fits_u128(n)) return u128_is_prime(bn_to_u128(n));
    return bn_is_probable_prime(n, 12u);
}

/**
 * Normalize a found uint64 split into the `FactorResult` representation.
 * Prime leaves are absorbed into the factor list and the composite remainder,
 * if any, is left in `cofactor` for higher-level callers.
 */
static void factor_result_set_split(FactorResult* fr, uint64_t left, uint64_t right) {
    if (left > right) {
        uint64_t t = left;
        left = right;
        right = t;
    }
    if (left <= 1u || right <= 1u) {
        fr->status = (int)CPSS_INCOMPLETE;
        return;
    }

    bool left_prime = miller_rabin_u64(left);
    bool right_prime = miller_rabin_u64(right);
    if (left_prime && right_prime) {
        factor_result_add(fr, left);
        factor_result_add(fr, right);
        fr->cofactor = 1u;
        fr->fully_factored = true;
        return;
    }

    if (left_prime || !right_prime) {
        factor_result_add(fr, left);
        fr->cofactor = right;
    }
    else {
        factor_result_add(fr, right);
        fr->cofactor = left;
    }
}

/**
 * Normalize a U128 split while preserving any large remainder in `cofactor`.
 * Only prime factors that fit the compact uint64 factor array are materialized
 * into the factor list.
 */
static void factor_result_u128_set_split(FactorResultU128* fr, U128 left, U128 right) {
    if (u128_gt(left, right)) {
        U128 t = left;
        left = right;
        right = t;
    }
    if (u128_le(left, u128_from_u64(1u)) || u128_le(right, u128_from_u64(1u))) {
        fr->status = (int)CPSS_INCOMPLETE;
        return;
    }

    bool left_prime = u128_fits_u64(left) ? miller_rabin_u64(left.lo) : u128_is_prime(left);
    bool right_prime = u128_fits_u64(right) ? miller_rabin_u64(right.lo) : u128_is_prime(right);

    if (u128_fits_u64(left)) {
        factor_result_u128_add(fr, left.lo);
    }
    fr->cofactor = right;

    if (left_prime && right_prime) {
        fr->fully_factored = true;
        if (u128_fits_u64(right)) {
            factor_result_u128_add(fr, right.lo);
            fr->cofactor = u128_from_u64(1u);
        }
    }
}

/**
 * Record a single BigNum split and annotate whether each side is probably
 * prime so recursive printers and routers can decide the next step.
 */
static void factor_result_bn_set_split(FactorResultBigNum* fr, const BigNum* left_in, const BigNum* right_in) {
    const BigNum* left = left_in;
    const BigNum* right = right_in;
    if (bn_cmp(left_in, right_in) > 0) {
        left = right_in;
        right = left_in;
    }

    bn_copy(&fr->factor, left);
    bn_copy(&fr->cofactor, right);
    fr->factor_found = true;
    fr->factor_is_prime = bn_factor_is_prime(left);
    fr->cofactor_is_prime = bn_factor_is_prime(right);
    fr->fully_factored = (fr->factor_is_prime && fr->cofactor_is_prime);
}

/* Fixed-width BigNum helper routines used by the affine ECM and related
 * arithmetic utilities below. These stay local to factor.c because they are
 * shared infrastructure rather than standalone factorisation engines. */
static void bn_ext_add(const BigNum* a, const BigNum* b, uint64_t out[BN_MAX_LIMBS + 1u]) {
    uint64_t carry = 0u;
    for (uint32_t i = 0u; i < BN_MAX_LIMBS; ++i) {
        uint64_t av = i < a->used ? a->limbs[i] : 0u;
        uint64_t bv = i < b->used ? b->limbs[i] : 0u;
        uint64_t sum = av + bv;
        uint64_t carry1 = sum < av ? 1u : 0u;
        uint64_t sum2 = sum + carry;
        uint64_t carry2 = sum2 < sum ? 1u : 0u;
        out[i] = sum2;
        carry = (carry1 || carry2) ? 1u : 0u;
    }
    out[BN_MAX_LIMBS] = carry;
}

static void bn_ext_sub_bn(uint64_t acc[BN_MAX_LIMBS + 1u], const BigNum* b) {
    uint64_t borrow = 0u;
    for (uint32_t i = 0u; i < BN_MAX_LIMBS; ++i) {
        uint64_t bv = i < b->used ? b->limbs[i] : 0u;
        uint64_t need = bv + borrow;
        uint64_t new_borrow = (need < bv || acc[i] < need) ? 1u : 0u;
        acc[i] = acc[i] - need;
        borrow = new_borrow;
    }
    acc[BN_MAX_LIMBS] -= borrow;
}

static void bn_ext_to_bn(BigNum* out, const uint64_t acc[BN_MAX_LIMBS + 1u]) {
    bn_zero(out);
    out->used = BN_MAX_LIMBS;
    for (uint32_t i = 0u; i < BN_MAX_LIMBS; ++i) {
        out->limbs[i] = acc[i];
    }
    bn_normalize(out);
}

static void bn_mod_add(BigNum* out, const BigNum* a, const BigNum* b, const BigNum* mod) {
    uint64_t acc[BN_MAX_LIMBS + 1u];
    bn_ext_add(a, b, acc);
    if (acc[BN_MAX_LIMBS] != 0u) {
        bn_ext_sub_bn(acc, mod);
    }
    else {
        BigNum sum;
        bn_ext_to_bn(&sum, acc);
        if (bn_cmp(&sum, mod) >= 0) {
            bn_ext_sub_bn(acc, mod);
        }
    }
    bn_ext_to_bn(out, acc);
}

static void bn_mod_sub(BigNum* out, const BigNum* a, const BigNum* b, const BigNum* mod) {
    if (bn_cmp(a, b) >= 0) {
        bn_copy(out, a);
        bn_sub(out, b);
        return;
    }

    BigNum delta;
    bn_copy(&delta, mod);
    bn_sub(&delta, b);
    bn_mod_add(out, &delta, a, mod);
}

static void bn_mod_mul(BigNum* out, const BigNum* a, const BigNum* b, const BigNum* mod) {
    bn_mulmod_peasant(out, a, b, mod);
}

static void bn_mod_square(BigNum* out, const BigNum* a, const BigNum* mod) {
    bn_mulmod_peasant(out, a, a, mod);
}

static void bn_half_mod_odd(BigNum* a, const BigNum* mod) {
    if (bn_is_even(a)) {
        bn_shr1(a);
        return;
    }

    uint64_t acc[BN_MAX_LIMBS + 1u];
    bn_ext_add(a, mod, acc);
    for (int i = BN_MAX_LIMBS; i >= 0; --i) {
        uint64_t carry = (i > 0) ? (acc[i] & 1u ? (1ull << 63u) : 0u) : 0u;
        acc[i] >>= 1u;
        if (i > 0) acc[i - 1] |= carry;
    }
    bn_ext_to_bn(a, acc);
}

static bool bn_factor_candidate_valid(const BigNum* g, const BigNum* n) {
    return !bn_is_zero(g) && !bn_is_one(g) && bn_cmp(g, n) < 0;
}

/**
 * Compute an inverse modulo `mod`, reporting any discovered gcd in `factor`.
 * This lets ECM-style code treat failed inversions as factor discoveries
 * instead of generic arithmetic errors.
 */
static bool bn_mod_inverse_checked(BigNum* inv, const BigNum* a, const BigNum* mod, BigNum* factor) {
    BigNum g;
    bn_gcd(&g, a, mod);
    if (!bn_is_one(&g)) {
        if (factor && bn_factor_candidate_valid(&g, mod)) {
            bn_copy(factor, &g);
        }
        return false;
    }

    BigNum u, v, x1, x2;
    bn_copy(&u, a);
    while (bn_cmp(&u, mod) >= 0) {
        bn_sub(&u, mod);
    }
    bn_copy(&v, mod);
    bn_from_u64(&x1, 1u);
    bn_zero(&x2);

    while (!bn_is_one(&u) && !bn_is_one(&v)) {
        while (bn_is_even(&u)) {
            bn_shr1(&u);
            bn_half_mod_odd(&x1, mod);
        }
        while (bn_is_even(&v)) {
            bn_shr1(&v);
            bn_half_mod_odd(&x2, mod);
        }

        if (bn_cmp(&u, &v) >= 0) {
            bn_sub(&u, &v);
            bn_mod_sub(&x1, &x1, &x2, mod);
        }
        else {
            bn_sub(&v, &u);
            bn_mod_sub(&x2, &x2, &x1, mod);
        }
    }

    if (bn_is_one(&u)) bn_copy(inv, &x1);
    else bn_copy(inv, &x2);
    return true;
}

/**
 * Sample a non-zero BigNum uniformly enough for local randomized search.
 * The value is constrained to the interval `(0, limit)`.
 */
static void bn_random_below(BigNum* out, const BigNum* limit, uint64_t* state) {
    unsigned bits = bn_bitlen(limit);
    if (bits == 0u) {
        bn_zero(out);
        return;
    }

    do {
        bn_random_bits(out, bits, state);
        if (bn_cmp(out, limit) >= 0) {
            bn_sub(out, limit);
        }
    } while (bn_is_zero(out));
}

/** Convert a discovered BigNum factor into the canonical split result form. */
static void factor_result_bn_set_from_factor(FactorResultBigNum* fr, const BigNum* n, const BigNum* factor) {
    BigNum cofactor;
    bn_divexact(&cofactor, n, factor);
    factor_result_bn_set_split(fr, factor, &cofactor);
}

#include "Rho/rho.c"
#include "ECM/ecm.c"
#include "Fermat/fermat.c"

#include "TrialDivision/trial_division.c"
#include "PMinus1/pminus1.c"
#include "PPlus1/pplus1.c"

/* Legacy stream-local and DB-routed trial/pminus1/pplus1/lucas-chain code removed.
 * See factor copy.c for the originals.
 * Live versions: TrialDivision/trial_division.c, PMinus1/pminus1.c, PPlus1/pplus1.c */

/* ══════════════════════════════════════════════════════════════════════
 * FACTOR AUTO-ROUTERS
 *
 * Classify input, check support coverage, and pick the best method.
 * cpss_db_factor_auto  — multi-shard database path (uint64)
 * cpss_factor_auto     — single-stream path (uint64)
 * cpss_factor_auto_u128 — U128 path
 * cpss_factor_auto_bn  — BigNum path
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Database-backed auto-router for uint64 inputs.
 * Applies the same general method ordering as the stream-local router while
 * sourcing all prime walks through shard-aware DB helpers.
 */
static FactorResult cpss_db_factor_auto(CPSSDatabase* db, uint64_t n) {
    FactorResult fr;
    factor_result_init(&fr, n, "auto");
    double t0 = now_sec();
    bool used_wheel = false;

    if (n <= 1u) {
        fr.method = "auto(trivial)";
        fr.cofactor = n;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    {
        CompositeClass cls = cpss_db_classify(db, n);
        if (cls.is_prime) {
            fr.method = "auto(prime)";
            factor_result_add(&fr, n);
            fr.cofactor = 1u;
            fr.fully_factored = true;
            fr.time_seconds = now_sec() - t0;
            return fr;
        }
    }

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        while (fr.cofactor % WHEEL_PRIMES[i] == 0u) {
            used_wheel = true;
            factor_result_add(&fr, WHEEL_PRIMES[i]);
            fr.cofactor /= WHEEL_PRIMES[i];
        }
    }
    if (fr.cofactor <= 1u) {
        fr.method = "auto(wheel)";
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (miller_rabin_u64(fr.cofactor)) {
        fr.method = used_wheel ? "auto(wheel)" : "auto(prime)";
        factor_result_add(&fr, fr.cofactor);
        fr.cofactor = 1u;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    {
        FactorResult pm1 = cpss_db_factor_pminus1(db, fr.cofactor, 0u);
        if (pm1.fully_factored || pm1.factor_count > 0u) {
            fr.method = "auto(pminus1)";
            for (size_t i = 0u; i < pm1.factor_count; ++i) {
                for (uint64_t e = 0u; e < pm1.exponents[i]; ++e)
                    factor_result_add(&fr, pm1.factors[i]);
            }
            fr.cofactor = pm1.cofactor;
            fr.fully_factored = pm1.fully_factored;
            if (!fr.fully_factored && fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    {
        FactorResult pp1 = cpss_db_factor_pplus1(db, fr.cofactor, 0u);
        if (pp1.fully_factored || pp1.factor_count > 0u) {
            fr.method = "auto(pplus1)";
            for (size_t i = 0u; i < pp1.factor_count; ++i) {
                for (uint64_t e = 0u; e < pp1.exponents[i]; ++e)
                    factor_result_add(&fr, pp1.factors[i]);
            }
            fr.cofactor = pp1.cofactor;
            fr.fully_factored = pp1.fully_factored;
            if (!fr.fully_factored && fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    {
        FactorResult rho = cpss_factor_rho(fr.cofactor, 0u, 0u);
        if (rho.fully_factored || rho.factor_count > 0u) {
            fr.method = "auto(rho)";
            for (size_t i = 0u; i < rho.factor_count; ++i) {
                for (uint64_t e = 0u; e < rho.exponents[i]; ++e)
                    factor_result_add(&fr, rho.factors[i]);
            }
            fr.cofactor = rho.cofactor;
            fr.fully_factored = rho.fully_factored;
            if (!fr.fully_factored && fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    {
        FactorResult trial = cpss_db_factor_trial(db, fr.cofactor, 0u);
        fr.method = "auto(trial)";
        for (size_t i = 0u; i < trial.factor_count; ++i) {
            for (uint64_t e = 0u; e < trial.exponents[i]; ++e)
                factor_result_add(&fr, trial.factors[i]);
        }
        fr.cofactor = trial.cofactor;
        fr.fully_factored = trial.fully_factored;
        fr.status = trial.status;
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Stream-backed auto-router for uint64 inputs.
 * Prefers cheap smoothness and general-purpose split methods before falling
 * back to deterministic semiprime-focused engines and finally trial division.
 */
static FactorResult cpss_factor_auto(CPSSStream* s, uint64_t n) {
    FactorResult fr;
    factor_result_init(&fr, n, "auto");
    double t0 = now_sec();
    bool used_wheel = false;

    if (n <= 1u) {
        fr.method = "auto(trivial)";
        fr.cofactor = n;
        fr.fully_factored = (n <= 1u);
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Quick classification */
    CompositeClass cls = cpss_classify(s, n);

    if (cls.is_prime) {
        fr.method = "auto(prime)";
        factor_result_add(&fr, n);
        fr.cofactor = 1u;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Divide out small factors */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        while (fr.cofactor % WHEEL_PRIMES[i] == 0u) {
            used_wheel = true;
            factor_result_add(&fr, WHEEL_PRIMES[i]);
            fr.cofactor /= WHEEL_PRIMES[i];
        }
    }
    if (fr.cofactor <= 1u) {
        fr.method = "auto(wheel)";
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Check if cofactor is prime */
    if (miller_rabin_u64(fr.cofactor)) {
        fr.method = used_wheel ? "auto(wheel)" : "auto(prime)";
        factor_result_add(&fr, fr.cofactor);
        fr.cofactor = 1u;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Step 1: Try p-1 first â€” it's cheap (O(B1)) and often works.
     * Use default B1 (min(1M, db_hi)). This costs ~0.05-0.1s. */
    {
        FactorResult pm1 = cpss_factor_pminus1(s, fr.cofactor, 0u);
        if (pm1.fully_factored || pm1.factor_count > 0u) {
            fr.method = "auto(pminus1)";
            for (size_t i = 0u; i < pm1.factor_count; ++i) {
                for (uint64_t e = 0u; e < pm1.exponents[i]; ++e)
                    factor_result_add(&fr, pm1.factors[i]);
            }
            fr.cofactor = pm1.cofactor;
            fr.fully_factored = pm1.fully_factored;
            if (!fr.fully_factored && fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    /* Step 2: Try p+1 â€” complements p-1, finds factors where p+1 is smooth */
    {
        FactorResult pp1 = cpss_factor_pplus1(s, fr.cofactor, 0u);
        if (pp1.fully_factored || pp1.factor_count > 0u) {
            fr.method = "auto(pplus1)";
            for (size_t i = 0u; i < pp1.factor_count; ++i) {
                for (uint64_t e = 0u; e < pp1.exponents[i]; ++e)
                    factor_result_add(&fr, pp1.factors[i]);
            }
            fr.cofactor = pp1.cofactor;
            fr.fully_factored = pp1.fully_factored;
            if (!fr.fully_factored && fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    /* Step 3: rho helps on general semiprimes before trial division */
    {
        FactorResult rho = cpss_factor_rho(fr.cofactor, 0u, 0u);
        if (rho.fully_factored || rho.factor_count > 0u) {
            fr.method = "auto(rho)";
            for (size_t i = 0u; i < rho.factor_count; ++i) {
                for (uint64_t e = 0u; e < rho.exponents[i]; ++e)
                    factor_result_add(&fr, rho.factors[i]);
            }
            fr.cofactor = rho.cofactor;
            fr.fully_factored = rho.fully_factored;
            if (!fr.fully_factored && fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    /* Step 4: SQUFOF â€” deterministic, good for hard uint64 semiprimes */
    if (fr.cofactor > 1u && !miller_rabin_u64(fr.cofactor) && (fr.cofactor & 1u)) {
        FactorResult sq = cpss_factor_squfof(fr.cofactor);
        if (sq.fully_factored || sq.factor_count > 0u) {
            fr.method = "auto(squfof)";
            for (size_t i = 0u; i < sq.factor_count; ++i) {
                for (uint64_t e = 0u; e < sq.exponents[i]; ++e)
                    factor_result_add(&fr, sq.factors[i]);
            }
            fr.cofactor = sq.cofactor;
            fr.fully_factored = sq.fully_factored;
            if (!fr.fully_factored && fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    /* Step 5: QS â€” basic quadratic sieve for remaining hard composites */
    if (fr.cofactor > 1u && !miller_rabin_u64(fr.cofactor)) {
        FactorResult qs = cpss_factor_qs_u64(fr.cofactor);
        if (qs.fully_factored || qs.factor_count > 0u) {
            fr.method = "auto(qs)";
            for (size_t i = 0u; i < qs.factor_count; ++i) {
                for (uint64_t e = 0u; e < qs.exponents[i]; ++e)
                    factor_result_add(&fr, qs.factors[i]);
            }
            fr.cofactor = qs.cofactor;
            fr.fully_factored = qs.fully_factored;
            if (!fr.fully_factored && fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    /* Step 6: trial division â€” last resort */
    {
        fr.method = "auto(trial)";
        FactorResult trial = cpss_factor_trial(s, fr.cofactor, 0u);
        for (size_t i = 0u; i < trial.factor_count; ++i) {
            for (uint64_t e = 0u; e < trial.exponents[i]; ++e)
                factor_result_add(&fr, trial.factors[i]);
        }
        fr.cofactor = trial.cofactor;
        fr.fully_factored = trial.fully_factored;
        fr.status = trial.status;
    }

    if (!fr.fully_factored) {
        fr.status = (int)CPSS_INCOMPLETE;
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * SINGLE-PURPOSE FACTOR QUERIES
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/** Find the smallest prime factor of n using CPSS. */
static uint64_t cpss_smallest_factor(CPSSStream* s, uint64_t n) {
    if (n <= 1u) return 0u;
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (n % WHEEL_PRIMES[i] == 0u) return WHEEL_PRIMES[i];
    }

    PrimeIterator iter;
    cpss_iter_init(&iter, s, WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u);
    while (iter.valid) {
        uint64_t p = iter.current;
        if (p > 0u && p <= UINT32_MAX && (uint64_t)p * p > n) break;
        if (n % p == 0u) return p;
        cpss_iter_next(&iter);
    }
    /* n itself is prime (within our search range) */
    return n;
}

/**
 * Check if n is B-smooth (all prime factors <= bound).
 * Returns true if fully factored and all factors <= bound.
 */
static bool cpss_is_smooth(CPSSStream* s, uint64_t n, uint64_t bound) {
    if (n <= 1u) return true;
    FactorResult fr = cpss_factor_trial(s, n, bound);
    return fr.fully_factored;
}

/** Print a FactorResult to stdout. */
static void factor_result_print(const FactorResult* fr) {
    printf("factor(%" PRIu64 "):\n", fr->n);
    printf("  method           = %s\n", fr->method);
    printf("  status           = %s\n", cpss_status_name((CPSSQueryStatus)fr->status));
    if (fr->factor_count > 0u) {
        printf("  factorisation    = ");
        for (size_t i = 0u; i < fr->factor_count; ++i) {
            if (i > 0u) printf(" * ");
            if (fr->exponents[i] > 1u)
                printf("%" PRIu64 "^%" PRIu64, fr->factors[i], fr->exponents[i]);
            else
                printf("%" PRIu64, fr->factors[i]);
        }
        printf("\n");
    }
    if (fr->cofactor > 1u) {
        printf("  cofactor         = %" PRIu64 "\n", fr->cofactor);
    }
    printf("  fully factored   = %s\n", fr->fully_factored ? "yes" : "no");
    printf("  time             = %.6fs\n", fr->time_seconds);
}

/** Print a FactorResultU128 to stdout. */
static void factor_result_u128_print(const FactorResultU128* fr) {
    char nbuf[64]; u128_to_str(fr->n, nbuf, sizeof(nbuf));
    printf("factor(%s): [Tier B: U128]\n", nbuf);
    printf("  method           = %s\n", fr->method);
    printf("  status           = %s\n", cpss_status_name((CPSSQueryStatus)fr->status));
    if (fr->factor_count > 0u) {
        printf("  factorisation    = ");
        for (size_t i = 0u; i < fr->factor_count; ++i) {
            if (i > 0u) printf(" * ");
            if (fr->exponents[i] > 1u) printf("%"PRIu64"^%"PRIu64, fr->factors[i], fr->exponents[i]);
            else printf("%"PRIu64, fr->factors[i]);
        }
        printf("\n");
    }
    char cbuf[64]; u128_to_str(fr->cofactor, cbuf, sizeof(cbuf));
    if (!u128_eq(fr->cofactor, u128_from_u64(1u)))
        printf("  cofactor         = %s\n", cbuf);
    printf("  fully factored   = %s\n", fr->fully_factored ? "yes" : "no");
    printf("  time             = %.6fs\n", fr->time_seconds);
}

/** Print a FactorResultBigNum to stdout (single-split, no recursion). */
static void factor_result_bn_print(const FactorResultBigNum* fr) {
    char nbuf[512]; bn_to_str(&fr->n, nbuf, sizeof(nbuf));
    printf("factor(%s): [Tier C: BigNum, %u bits]\n", nbuf, bn_bitlen(&fr->n));
    printf("  method           = %s\n", fr->method ? fr->method : "none");
    printf("  status           = %s\n", cpss_status_name((CPSSQueryStatus)fr->status));
    if (fr->factor_found) {
        char fbuf[512], cbuf[512];
        bn_to_str(&fr->factor, fbuf, sizeof(fbuf));
        bn_to_str(&fr->cofactor, cbuf, sizeof(cbuf));
        printf("  factor           = %s%s\n", fbuf, fr->factor_is_prime ? " (prime)" : "");
        printf("  cofactor         = %s%s\n", cbuf, fr->cofactor_is_prime ? " (prime)" : "");
    }
    printf("  fully factored   = %s\n", fr->fully_factored ? "yes" : "no");
    printf("  time             = %.6fs\n", fr->time_seconds);
}

/**
 * Print a FactorResultBigNum with recursive cofactor factoring.
 * After printing the initial split, if the cofactor is composite,
 * attempts to factor it by down-tiering (u64 if fits) or retrying BN methods.
 * Used by the `factor` command dispatch; individual method commands use factor_result_bn_print.
 */
static void factor_result_bn_print_recursive_ex(FactorResultBigNum* fr, CPSSStream* qs, const char* tier_label) {
    char nbuf[512]; bn_to_str(&fr->n, nbuf, sizeof(nbuf));
    unsigned nbits = bn_bitlen(&fr->n);
    if (tier_label)
        printf("factor(%s): [%s, %u bits]\n", nbuf, tier_label, nbits);
    else
        printf("factor(%s): [Tier C: BigNum, %u bits]\n", nbuf, nbits);
    printf("  method           = %s\n", fr->method ? fr->method : "none");
    printf("  status           = %s\n", cpss_status_name((CPSSQueryStatus)fr->status));

    if (!fr->factor_found) {
        printf("  factoring status = no factor found\n");
        printf("  fully factored   = no\n");
        printf("  time             = %.6fs\n", fr->time_seconds);
        return;
    }

    /* Verify factor * cofactor == n */
    {
        BigNum product;
        bn_mul(&product, &fr->factor, &fr->cofactor);
        if (bn_cmp(&product, &fr->n) != 0) {
            printf("  WARNING: factor * cofactor != n (internal error)\n");
        }
    }

    char fbuf[512], cbuf[512];
    bn_to_str(&fr->factor, fbuf, sizeof(fbuf));
    bn_to_str(&fr->cofactor, cbuf, sizeof(cbuf));
    printf("  factor           = %s%s\n", fbuf, fr->factor_is_prime ? " (prime)" : "");
    printf("  cofactor         = %s%s\n", cbuf, fr->cofactor_is_prime ? " (prime)" : "");

    /* Recursive cofactor factoring (up to 4 levels deep) */
    if (!fr->fully_factored && fr->factor_found && !fr->cofactor_is_prime) {
        BigNum cof;
        bn_copy(&cof, &fr->cofactor);

        for (unsigned depth = 0u; depth < 4u; ++depth) {
            if (bn_is_one(&cof) || bn_factor_is_prime(&cof)) {
                fr->fully_factored = true;
                break;
            }

            /* Down-tier: if cofactor fits u64, use fast u64 methods */
            if (bn_fits_u64(&cof)) {
                uint64_t c64 = bn_to_u64(&cof);
                FactorResult cr;
                if (qs) {
                    cr = cpss_factor_auto(qs, c64);
                } else {
                    cr = cpss_factor_rho(c64, 0u, 0u);
                    if (!cr.fully_factored) cr = cpss_factor_fermat(c64, 0u);
                }
                if (cr.factor_count > 0u) {
                    printf("  cofactor[%u]      = ", depth + 1u);
                    for (size_t i = 0u; i < cr.factor_count; ++i) {
                        if (i > 0u) printf(" * ");
                        if (cr.exponents[i] > 1u) printf("%" PRIu64 "^%" PRIu64, cr.factors[i], cr.exponents[i]);
                        else printf("%" PRIu64, cr.factors[i]);
                    }
                    if (cr.cofactor > 1u) printf(" * %" PRIu64, cr.cofactor);
                    printf(" (u64 downtier)\n");
                    fr->fully_factored = cr.fully_factored;
                }
                break;
            }

            /* Try BigNum methods on the remaining cofactor */
            FactorResultBigNum cr;
            if (qs) {
                cr = cpss_factor_auto_bn(qs, &cof);
            } else {
                cr = cpss_factor_rho_bn(&cof, 0u, 0u);
                if (!cr.factor_found) cr = cpss_factor_fermat_bn(&cof, 0u);
                if (!cr.factor_found) cr = cpss_factor_ecm_bn(&cof, 0u, 0u, 0u);
            }

            if (!cr.factor_found) break;

            char fb2[512], cb2[512];
            bn_to_str(&cr.factor, fb2, sizeof(fb2));
            bn_to_str(&cr.cofactor, cb2, sizeof(cb2));
            printf("  cofactor[%u]      = %s%s * %s%s\n", depth + 1u,
                fb2, cr.factor_is_prime ? " (prime)" : "",
                cb2, cr.cofactor_is_prime ? " (prime)" : "");

            if (cr.fully_factored) {
                fr->fully_factored = true;
                break;
            }
            bn_copy(&cof, &cr.cofactor);
        }
    }

    if (fr->fully_factored) {
        printf("  factoring status = fully factored\n");
    } else if (fr->factor_found && !fr->cofactor_is_prime) {
        printf("  factoring status = partial (cofactor is composite)\n");
    } else {
        printf("  factoring status = one split found\n");
    }
    printf("  fully factored   = %s\n", fr->fully_factored ? "yes" : "no");
    printf("  time             = %.6fs\n", fr->time_seconds);
}

static void factor_result_bn_print_recursive(FactorResultBigNum* fr, CPSSStream* qs) {
    factor_result_bn_print_recursive_ex(fr, qs, NULL);
}

/* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
 * U128 FACTOR ENGINES
 *
 * Handle 128-bit input numbers. Primes from the DB are uint64.
 * Trial division: cofactor_128 % prime_64.
 * Pollard p-1: powmod(base, pk, n_128).
 * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

/**
 * Add one uint64 prime factor to a U128 factor result, merging exponents.
 * This compact representation deliberately omits prime factors wider than
 * uint64, which remain represented through the `cofactor` field instead.
 */
static void factor_result_u128_add(FactorResultU128* fr, uint64_t p) {
    for (size_t i = 0u; i < fr->factor_count; ++i) {
        if (fr->factors[i] == p) { ++fr->exponents[i]; return; }
    }
    if (fr->factor_count < 64u) {
        fr->factors[fr->factor_count] = p;
        fr->exponents[fr->factor_count] = 1u;
        ++fr->factor_count;
    }
}

/**
 * Initialize a U128 factor result with the original input as the cofactor.
 * Callers progressively move narrow prime factors out of `cofactor` as they
 * discover them.
 */
static void factor_result_u128_init(FactorResultU128* fr, U128 n, const char* method) {
    memset(fr, 0, sizeof(*fr));
    fr->n = n;
    fr->cofactor = n;
    fr->method = method;
    fr->status = (int)CPSS_OK;
}

/* Legacy cpss_factor_trial_u128 + cpss_factor_pminus1_u128 removed â€”
 * now in TrialDivision/trial_division.c and PMinus1/pminus1.c */

/**
 * Auto-factor U128 n. Strategy: small factors â†’ primality â†’ p-1 â†’ trial.
 * p-1 is tried first because it's O(B1) while trial is O(sqrt(n)).
 */
static FactorResultU128 cpss_factor_auto_u128(CPSSStream* s, U128 n) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "auto");
    double t0 = now_sec();
    bool used_wheel = false;

    /* If fits in uint64, delegate to the faster uint64 engine */
    if (u128_fits_u64(n)) {
        FactorResult fr64 = cpss_factor_auto(s, n.lo);
        fr.n = n;
        for (size_t i = 0u; i < fr64.factor_count; ++i) {
            fr.factors[i] = fr64.factors[i];
            fr.exponents[i] = fr64.exponents[i];
        }
        fr.factor_count = fr64.factor_count;
        fr.cofactor = u128_from_u64(fr64.cofactor);
        fr.fully_factored = fr64.fully_factored;
        fr.status = fr64.status;
        fr.method = fr64.method;
        fr.time_seconds = fr64.time_seconds;
        return fr;
    }

    /* Divide out wheel primes */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        while (u128_mod_u64(fr.cofactor, WHEEL_PRIMES[i]) == 0u) {
            used_wheel = true;
            factor_result_u128_add(&fr, WHEEL_PRIMES[i]);
            fr.cofactor = u128_div(fr.cofactor, u128_from_u64(WHEEL_PRIMES[i]));
        }
    }
    if (u128_le(fr.cofactor, u128_from_u64(1u))) {
        fr.method = "auto(wheel)";
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* If cofactor now fits in uint64, delegate */
    if (u128_fits_u64(fr.cofactor)) {
        FactorResult sub = cpss_factor_auto(s, fr.cofactor.lo);
        for (size_t i = 0u; i < sub.factor_count; ++i) {
            for (uint64_t e = 0u; e < sub.exponents[i]; ++e)
                factor_result_u128_add(&fr, sub.factors[i]);
        }
        fr.cofactor = u128_from_u64(sub.cofactor);
        fr.fully_factored = (sub.cofactor <= 1u);
        fr.method = used_wheel ? "auto(wheel)" : sub.method;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Check if cofactor is prime */
    if (u128_is_prime(fr.cofactor)) {
        /* Can't store as uint64 factor, but it IS fully factored */
        fr.method = used_wheel ? "auto(wheel)" : "auto(prime)";
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Step 1: Try p-1 first â€” cheap O(B1), often solves instantly */
    {
        FactorResultU128 pm1 = cpss_factor_pminus1_u128(s, fr.cofactor, 0u);
        if (pm1.fully_factored || pm1.factor_count > 0u) {
            fr.method = "auto(pminus1)";
            for (size_t i = 0u; i < pm1.factor_count; ++i) {
                for (uint64_t e = 0u; e < pm1.exponents[i]; ++e)
                    factor_result_u128_add(&fr, pm1.factors[i]);
            }
            fr.cofactor = pm1.cofactor;
            fr.fully_factored = pm1.fully_factored;
            if (!fr.fully_factored && u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                factor_result_u128_add(&fr, fr.cofactor.lo);
                fr.cofactor = u128_from_u64(1u);
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    /* Step 2: Try p+1 â€” complements p-1, finds factors where p+1 is smooth */
    {
        FactorResultU128 pp1 = cpss_factor_pplus1_u128(s, fr.cofactor, 0u);
        if (pp1.fully_factored || pp1.factor_count > 0u) {
            fr.method = "auto(pplus1)";
            for (size_t i = 0u; i < pp1.factor_count; ++i) {
                for (uint64_t e = 0u; e < pp1.exponents[i]; ++e)
                    factor_result_u128_add(&fr, pp1.factors[i]);
            }
            fr.cofactor = pp1.cofactor;
            fr.fully_factored = pp1.fully_factored;
            if (!fr.fully_factored && u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                factor_result_u128_add(&fr, fr.cofactor.lo);
                fr.cofactor = u128_from_u64(1u);
                fr.fully_factored = true;
            }
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    /* Step 3: native U128 rho â€” general-purpose for hard composites */
    if (!fr.fully_factored && !u128_le(fr.cofactor, u128_from_u64(1u))
        && !u128_is_prime(fr.cofactor)) {
        FactorResultU128 rho = cpss_factor_rho_u128(fr.cofactor, 0u, 0u);
        if (rho.fully_factored || rho.factor_count > 0u) {
            fr.method = "auto(rho)";
            for (size_t i = 0u; i < rho.factor_count; ++i) {
                for (uint64_t e = 0u; e < rho.exponents[i]; ++e)
                    factor_result_u128_add(&fr, rho.factors[i]);
            }
            fr.cofactor = rho.cofactor;
            fr.fully_factored = rho.fully_factored;
            if (fr.fully_factored) {
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    /* Step 4: trial division â€” last resort */
    {
        fr.method = "auto(trial)";
        FactorResultU128 trial = cpss_factor_trial_u128(s, fr.cofactor, 0u);
        for (size_t i = 0u; i < trial.factor_count; ++i) {
            for (uint64_t e = 0u; e < trial.exponents[i]; ++e)
                factor_result_u128_add(&fr, trial.factors[i]);
        }
        fr.cofactor = trial.cofactor;
        fr.fully_factored = trial.fully_factored;
        fr.status = trial.status;
    }

    if (!fr.fully_factored) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * BigNum auto-router.
 * Returns the first useful split from the cheap smoothness and rho engines,
 * then falls back to Montgomery ECM when the lighter methods do not land.
 */
static FactorResultBigNum cpss_factor_auto_bn(CPSSStream* s, const BigNum* n) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "auto";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.method = "auto(trivial)";
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_factor_is_prime(n)) {
        fr.method = "auto(prime)";
        fr.cofactor_is_prime = true;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (bn_mod_u64(n, WHEEL_PRIMES[i]) == 0u) {
            BigNum small;
            fr.method = "auto(wheel)";
            bn_from_u64(&small, WHEEL_PRIMES[i]);
            factor_result_bn_set_from_factor(&fr, n, &small);
            fr.time_seconds = now_sec() - t0;
            return fr;
        }
    }

    {
        FactorResultBigNum pm1 = cpss_factor_pminus1_bn(s, n, 0u);
        if (pm1.factor_found || pm1.fully_factored) {
            pm1.method = "auto(pminus1)";
            pm1.time_seconds = now_sec() - t0;
            return pm1;
        }
    }

    {
        FactorResultBigNum pp1 = cpss_factor_pplus1_bn(s, n, 0u);
        if (pp1.factor_found || pp1.fully_factored) {
            pp1.method = "auto(pplus1)";
            pp1.time_seconds = now_sec() - t0;
            return pp1;
        }
    }

    {
        FactorResultBigNum rho = cpss_factor_rho_bn(n, 0u, 0u);
        if (rho.factor_found || rho.fully_factored) {
            rho.method = "auto(rho)";
            rho.time_seconds = now_sec() - t0;
            return rho;
        }
    }

    {
        FactorResultBigNum ecm = cpss_factor_ecm_mont_stage1(n, 0u, 0u, 0u);
        ecm.method = "auto(ecm-mont)";
        if (!ecm.factor_found && !ecm.fully_factored && ecm.status == (int)CPSS_OK) {
            ecm.status = (int)CPSS_INCOMPLETE;
        }
        ecm.time_seconds = now_sec() - t0;
        return ecm;
    }
}

/* Legacy cpss_factor_pminus1_bn removed — now in PMinus1/pminus1.c */
