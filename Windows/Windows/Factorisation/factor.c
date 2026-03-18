/**
 * cpss_factor.c - CPSS-accelerated factorisation engines.
 * Part of the CPSS Viewer amalgamation.
 *
 * Trial division uses PrimeIterator to stream primes from the DB.
 * Pollard p-1 uses PrimeIterator for stage-1 prime powers.
 * Auto-router classifies input and picks the best method.
 *
 * Most methods here benefit from the precomputed prime DB:
 *   - Trial division: streams primes from DB instead of sieving
 *   - Pollard p-1: uses DB primes for stage 1 smoothness bound
 *
 * Fermat, Pollard rho, and ECM are the explicit exceptions: they are kept as
 * opt-in methods even though they do not depend on CPSS prime enumeration.
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

static uint64_t factor_xorshift64(uint64_t* state) {
    uint64_t x = *state;
    x ^= x << 13u;
    x ^= x >> 7u;
    x ^= x << 17u;
    *state = x;
    return x;
}

static bool bn_factor_is_prime(const BigNum* n) {
    if (bn_is_zero(n) || bn_is_one(n)) return false;
    if (bn_fits_u64(n)) return miller_rabin_u64(bn_to_u64(n));
    if (bn_fits_u128(n)) return u128_is_prime(bn_to_u128(n));
    return bn_is_probable_prime(n, 12u);
}

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

static void factor_result_bn_set_from_factor(FactorResultBigNum* fr, const BigNum* n, const BigNum* factor) {
    BigNum cofactor;
    bn_divexact(&cofactor, n, factor);
    factor_result_bn_set_split(fr, factor, &cofactor);
}

static uint64_t rho_step_u64(uint64_t x, uint64_t c, uint64_t n) {
    uint64_t sq = mulmod_u64(x, x, n);
    c %= n;
    return (sq >= n - c) ? (sq - (n - c)) : (sq + c);
}

static uint64_t rho_find_factor_u64(uint64_t n, uint64_t max_iterations, uint64_t* rng_state) {
    if ((n & 1u) == 0u) return 2u;
    if (n % 3u == 0u) return 3u;

    const uint64_t attempts = 8u;
    uint64_t per_attempt = max_iterations / attempts;
    if (per_attempt == 0u) per_attempt = 1u;

    for (uint64_t attempt = 0u; attempt < attempts; ++attempt) {
        uint64_t c = (factor_xorshift64(rng_state) % (n - 1u)) + 1u;
        uint64_t x = (factor_xorshift64(rng_state) % (n - 2u)) + 2u;
        uint64_t y = x;

        for (uint64_t iter = 0u; iter < per_attempt; ++iter) {
            x = rho_step_u64(x, c, n);
            y = rho_step_u64(y, c, n);
            y = rho_step_u64(y, c, n);

            uint64_t diff = x > y ? x - y : y - x;
            uint64_t g = gcd_u64(diff, n);
            if (g == 1u) continue;
            if (g < n) return g;
            break;
        }
    }

    return 0u;
}

static bool cpss_factor_rho_recurse(FactorResult* fr, uint64_t n, uint64_t max_iterations, uint64_t* rng_state) {
    if (n <= 1u) return true;

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        while (n % p == 0u) {
            factor_result_add(fr, p);
            fr->cofactor /= p;
            n /= p;
        }
    }

    if (n <= 1u) return true;
    if (miller_rabin_u64(n)) {
        factor_result_add(fr, n);
        fr->cofactor /= n;
        return true;
    }

    uint64_t divisor = rho_find_factor_u64(n, max_iterations, rng_state);
    if (divisor <= 1u || divisor >= n) {
        fr->status = (int)CPSS_INCOMPLETE;
        return false;
    }

    bool left_ok = cpss_factor_rho_recurse(fr, divisor, max_iterations, rng_state);
    bool right_ok = cpss_factor_rho_recurse(fr, n / divisor, max_iterations, rng_state);
    return left_ok && right_ok;
}

static FactorResult cpss_factor_rho(uint64_t n, uint64_t max_iterations, uint64_t seed) {
    FactorResult fr;
    factor_result_init(&fr, n, "rho");
    double t0 = now_sec();
    max_iterations = rho_default_iterations(max_iterations);
    seed = rho_default_seed(seed);

    if (n <= 1u) {
        fr.cofactor = n;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    (void)cpss_factor_rho_recurse(&fr, n, max_iterations, &seed);
    if (fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
        factor_result_add(&fr, fr.cofactor);
        fr.cofactor = 1u;
    }

    fr.fully_factored = (fr.cofactor <= 1u);
    if (!fr.fully_factored && fr.status == (int)CPSS_OK) {
        fr.status = (int)CPSS_INCOMPLETE;
    }
    fr.time_seconds = now_sec() - t0;
    return fr;
}

static void rho_step_bn(BigNum* out, const BigNum* x, const BigNum* c, const BigNum* n) {
    BigNum sq;
    bn_mod_square(&sq, x, n);
    bn_mod_add(out, &sq, c, n);
}

static FactorResultBigNum cpss_factor_rho_bn(const BigNum* n, uint64_t max_iterations, uint64_t seed) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "rho";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();
    max_iterations = rho_default_iterations(max_iterations);
    seed = rho_default_seed(seed);

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_is_even(n)) {
        BigNum two;
        bn_from_u64(&two, 2u);
        factor_result_bn_set_from_factor(&fr, n, &two);
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_factor_is_prime(n)) {
        fr.cofactor_is_prime = true;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Try to find an initial split */
    BigNum split_factor;
    bn_zero(&split_factor);
    bool found_split = false;

    const uint64_t attempts = 8u;
    uint64_t per_attempt = max_iterations / attempts;
    if (per_attempt == 0u) per_attempt = 1u;

    for (uint64_t attempt = 0u; attempt < attempts && !found_split; ++attempt) {
        BigNum c, x, y;
        bn_random_below(&c, n, &seed);
        bn_random_below(&x, n, &seed);
        bn_copy(&y, &x);

        for (uint64_t iter = 0u; iter < per_attempt; ++iter) {
            BigNum diff, g;

            rho_step_bn(&x, &x, &c, n);
            rho_step_bn(&y, &y, &c, n);
            rho_step_bn(&y, &y, &c, n);

            if (bn_cmp(&x, &y) >= 0) {
                bn_copy(&diff, &x);
                bn_sub(&diff, &y);
            }
            else {
                bn_copy(&diff, &y);
                bn_sub(&diff, &x);
            }

            bn_gcd(&g, &diff, n);
            if (bn_is_one(&g)) continue;
            if (bn_cmp(&g, n) < 0) {
                bn_copy(&split_factor, &g);
                found_split = true;
                break;
            }
            break;
        }
    }

    if (!found_split) {
        fr.status = (int)CPSS_INCOMPLETE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Initial split found — set up factor/cofactor */
    factor_result_bn_set_from_factor(&fr, n, &split_factor);

    /* Recursive cofactor chase (up to 4 levels) */
    if (!fr.fully_factored && fr.factor_found && !fr.cofactor_is_prime) {
        BigNum cof;
        bn_copy(&cof, &fr.cofactor);

        for (unsigned depth = 0u; depth < 4u; ++depth) {
            if (bn_is_one(&cof) || bn_factor_is_prime(&cof)) {
                fr.fully_factored = true;
                break;
            }
            /* Down-tier to u64 if possible */
            if (bn_fits_u64(&cof)) {
                uint64_t c64 = bn_to_u64(&cof);
                FactorResult cr = cpss_factor_rho(c64, 0u, 0u);
                fr.fully_factored = cr.fully_factored;
                break;
            }
            /* Retry BN rho on cofactor */
            FactorResultBigNum cr = cpss_factor_rho_bn(&cof, max_iterations, seed + depth);
            if (!cr.factor_found) break;
            if (cr.fully_factored) { fr.fully_factored = true; break; }
            bn_copy(&cof, &cr.cofactor);
        }
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/* ══════════════════════════════════════════════════════════════════════
 * NATIVE U128 POLLARD RHO
 * ══════════════════════════════════════════════════════════════════════ */

static U128 rho_step_u128(U128 x, U128 c, U128 n) {
    U128 sq = u128_mulmod(x, x, n);
    return u128_mod(u128_add(sq, c), n);
}

static U128 rho_find_factor_u128(U128 n, uint64_t max_iterations, uint64_t* rng_state) {
    if (u128_mod_u64(n, 2u) == 0u) return u128_from_u64(2u);
    if (u128_mod_u64(n, 3u) == 0u) return u128_from_u64(3u);

    const uint64_t attempts = 8u;
    uint64_t per_attempt = max_iterations / attempts;
    if (per_attempt == 0u) per_attempt = 1u;

    for (uint64_t attempt = 0u; attempt < attempts; ++attempt) {
        uint64_t c_lo = factor_xorshift64(rng_state);
        uint64_t x_lo = factor_xorshift64(rng_state);
        U128 c = u128_mod(u128_from_u64(c_lo), n);
        if (u128_is_zero(c)) c = u128_from_u64(1u);
        U128 x = u128_mod(u128_from_u64(x_lo), n);
        if (u128_is_zero(x)) x = u128_from_u64(2u);
        U128 y = x;

        for (uint64_t iter = 0u; iter < per_attempt; ++iter) {
            x = rho_step_u128(x, c, n);
            y = rho_step_u128(y, c, n);
            y = rho_step_u128(y, c, n);

            U128 diff = u128_gt(x, y) ? u128_sub(x, y) : u128_sub(y, x);
            U128 g = u128_gcd(diff, n);
            if (u128_eq(g, u128_from_u64(1u))) continue;
            if (u128_lt(g, n)) return g;
            break;
        }
    }

    return u128_from_u64(0u);
}

/** Recursive U128 rho: fully factors n by splitting and recursing on both halves. */
static bool cpss_factor_rho_u128_recurse(FactorResultU128* fr, U128 n,
                                           uint64_t max_iterations, uint64_t* rng_state) {
    if (u128_le(n, u128_from_u64(1u))) return true;

    /* Strip wheel primes */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        while (u128_mod_u64(n, p) == 0u) {
            factor_result_u128_add(fr, p);
            n = u128_div(n, u128_from_u64(p));
            fr->cofactor = n;
        }
    }

    if (u128_le(n, u128_from_u64(1u))) return true;

    /* If n now fits u64, use the faster u64 recursive rho */
    if (u128_fits_u64(n)) {
        FactorResult sub;
        factor_result_init(&sub, n.lo, "rho");
        (void)cpss_factor_rho_recurse(&sub, n.lo, max_iterations, rng_state);
        for (size_t i = 0u; i < sub.factor_count; ++i) {
            for (uint64_t e = 0u; e < sub.exponents[i]; ++e)
                factor_result_u128_add(fr, sub.factors[i]);
        }
        fr->cofactor = u128_from_u64(sub.cofactor);
        return sub.fully_factored;
    }

    /* Primality check */
    if (u128_is_prime(n)) {
        /* Can't store >64-bit factor in the u64 factors array.
         * Leave it in cofactor and mark as fully factored. */
        fr->cofactor = n;
        return true;  /* it's prime, so factoring is complete */
    }

    U128 divisor = rho_find_factor_u128(n, max_iterations, rng_state);
    if (u128_is_zero(divisor) || u128_eq(divisor, n)) {
        fr->status = (int)CPSS_INCOMPLETE;
        fr->cofactor = n;
        return false;
    }

    U128 other = u128_div(n, divisor);
    bool left_ok = cpss_factor_rho_u128_recurse(fr, divisor, max_iterations, rng_state);
    bool right_ok = cpss_factor_rho_u128_recurse(fr, other, max_iterations, rng_state);
    return left_ok && right_ok;
}

static FactorResultU128 cpss_factor_rho_u128(U128 n, uint64_t max_iterations, uint64_t seed) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "rho");
    double t0 = now_sec();
    max_iterations = rho_default_iterations(max_iterations);
    seed = rho_default_seed(seed);

    if (u128_le(n, u128_from_u64(1u))) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    (void)cpss_factor_rho_u128_recurse(&fr, n, max_iterations, &seed);

    /* Check if cofactor is prime */
    if (!u128_le(fr.cofactor, u128_from_u64(1u))) {
        if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
            factor_result_u128_add(&fr, fr.cofactor.lo);
            fr.cofactor = u128_from_u64(1u);
        } else if (u128_is_prime(fr.cofactor)) {
            /* prime cofactor that doesn't fit u64 — mark fully factored */
            fr.fully_factored = true;
        }
    }

    fr.fully_factored = u128_le(fr.cofactor, u128_from_u64(1u)) || fr.fully_factored;
    if (!fr.fully_factored && fr.status == (int)CPSS_OK) {
        fr.status = (int)CPSS_INCOMPLETE;
    }
    fr.time_seconds = now_sec() - t0;
    return fr;
}

typedef struct {
    BigNum x;
    BigNum y;
    bool infinity;
} ECMPoint;

static int ecm_point_double(ECMPoint* out, const ECMPoint* p, const BigNum* a,
    const BigNum* mod, BigNum* factor) {
    ECMPoint next;
    memset(&next, 0, sizeof(next));

    if (p->infinity || bn_is_zero(&p->y)) {
        next.infinity = true;
        *out = next;
        return 1;
    }

    BigNum denom, inv;
    bn_copy(&denom, &p->y);
    bn_shl1(&denom);
    if (!bn_mod_inverse_checked(&inv, &denom, mod, factor)) {
        return (factor && bn_factor_candidate_valid(factor, mod)) ? -1 : 0;
    }

    BigNum x_sq, numer, lambda, lambda_sq, two_x, tmp;
    bn_mod_square(&x_sq, &p->x, mod);
    bn_copy(&numer, &x_sq);
    bn_mod_add(&numer, &numer, &x_sq, mod);
    bn_mod_add(&numer, &numer, &x_sq, mod);
    bn_mod_add(&numer, &numer, a, mod);
    bn_mod_mul(&lambda, &numer, &inv, mod);

    bn_mod_square(&lambda_sq, &lambda, mod);
    bn_mod_add(&two_x, &p->x, &p->x, mod);
    bn_mod_sub(&next.x, &lambda_sq, &two_x, mod);

    bn_mod_sub(&tmp, &p->x, &next.x, mod);
    bn_mod_mul(&tmp, &tmp, &lambda, mod);
    bn_mod_sub(&next.y, &tmp, &p->y, mod);
    next.infinity = false;
    *out = next;
    return 1;
}

static int ecm_point_add(ECMPoint* out, const ECMPoint* p, const ECMPoint* q, const BigNum* a,
    const BigNum* mod, BigNum* factor) {
    ECMPoint next;
    memset(&next, 0, sizeof(next));

    if (p->infinity) {
        *out = *q;
        return 1;
    }
    if (q->infinity) {
        *out = *p;
        return 1;
    }

    if (bn_cmp(&p->x, &q->x) == 0) {
        BigNum y_sum;
        bn_mod_add(&y_sum, &p->y, &q->y, mod);
        if (bn_is_zero(&y_sum)) {
            next.infinity = true;
            *out = next;
            return 1;
        }
        return ecm_point_double(out, p, a, mod, factor);
    }

    BigNum numer, denom, inv, lambda, lambda_sq, tmp;
    bn_mod_sub(&numer, &q->y, &p->y, mod);
    bn_mod_sub(&denom, &q->x, &p->x, mod);
    if (!bn_mod_inverse_checked(&inv, &denom, mod, factor)) {
        return (factor && bn_factor_candidate_valid(factor, mod)) ? -1 : 0;
    }

    bn_mod_mul(&lambda, &numer, &inv, mod);
    bn_mod_square(&lambda_sq, &lambda, mod);
    bn_mod_sub(&tmp, &lambda_sq, &p->x, mod);
    bn_mod_sub(&next.x, &tmp, &q->x, mod);

    bn_mod_sub(&tmp, &p->x, &next.x, mod);
    bn_mod_mul(&tmp, &tmp, &lambda, mod);
    bn_mod_sub(&next.y, &tmp, &p->y, mod);
    next.infinity = false;
    *out = next;
    return 1;
}

static int ecm_point_mul_u64(ECMPoint* point, uint64_t k, const BigNum* a, const BigNum* mod,
    BigNum* factor) {
    ECMPoint result;
    memset(&result, 0, sizeof(result));
    result.infinity = true;

    ECMPoint addend = *point;
    while (k > 0u) {
        if ((k & 1u) != 0u) {
            int rc = ecm_point_add(&result, &result, &addend, a, mod, factor);
            if (rc <= 0) return rc;
        }
        k >>= 1u;
        if (k == 0u) break;
        {
            int rc = ecm_point_double(&addend, &addend, a, mod, factor);
            if (rc <= 0) return rc;
        }
    }

    *point = result;
    return 1;
}

static FactorResultBigNum cpss_factor_ecm_bn(const BigNum* n, uint64_t B1, uint64_t curves, uint64_t seed) {
    return cpss_factor_ecm_bn_ex(n, B1, 0u, curves, seed);
}

/**
 * ECM with Stage 1 + Stage 2.
 * Stage 1: multiply point by p^k for primes p <= B1.
 * Stage 2: baby-step/giant-step over primes q in (B1, B2].
 *   Precomputes D/2 baby-step x-coordinates, iterates giant steps of size D,
 *   accumulates products of (Sx - Bx) mod N, periodic GCD checks.
 *   O(√(B2-B1)) point ops + O(π(B2)-π(B1)) modular multiplies.
 * Still uses affine Weierstrass coordinates (not Montgomery/projective).
 * Missing vs industrial ECM: no Montgomery curve form, no extended-GCD-free
 * x-only ladder, no FFT-based polynomial evaluation for Stage 2.
 */
static FactorResultBigNum cpss_factor_ecm_bn_ex(const BigNum* n, uint64_t B1, uint64_t B2,
                                                   uint64_t curves, uint64_t seed) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "ecm";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();
    B1 = ecm_default_B1(B1);
    B2 = ecm_default_B2(B2, B1);
    curves = ecm_default_curves(curves);
    seed = ecm_default_seed(seed);

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_is_even(n)) {
        BigNum two;
        bn_from_u64(&two, 2u);
        factor_result_bn_set_from_factor(&fr, n, &two);
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_factor_is_prime(n)) {
        fr.cofactor_is_prime = true;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    for (uint64_t curve = 0u; curve < curves; ++curve) {
        BigNum a, b, x, y, factor;
        bn_random_below(&x, n, &seed);
        bn_random_below(&y, n, &seed);
        bn_random_below(&a, n, &seed);
        bn_zero(&factor);

        {
            BigNum y2, x2, x3, ax, rhs;
            bn_mod_square(&y2, &y, n);
            bn_mod_square(&x2, &x, n);
            bn_mod_mul(&x3, &x2, &x, n);
            bn_mod_mul(&ax, &a, &x, n);
            bn_mod_add(&rhs, &x3, &ax, n);
            bn_mod_sub(&b, &y2, &rhs, n);
        }

        {
            BigNum a2, a3, b2, term1, term2, delta, g, four, twenty_seven;
            bn_mod_mul(&a2, &a, &a, n);
            bn_mod_mul(&a3, &a2, &a, n);
            bn_mod_mul(&b2, &b, &b, n);
            bn_from_u64(&four, 4u);
            bn_from_u64(&twenty_seven, 27u);
            bn_mod_mul(&term1, &a3, &four, n);
            bn_mod_mul(&term2, &b2, &twenty_seven, n);
            bn_mod_add(&delta, &term1, &term2, n);
            bn_gcd(&g, &delta, n);
            if (bn_factor_candidate_valid(&g, n)) {
                factor_result_bn_set_from_factor(&fr, n, &g);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (!bn_is_one(&g)) continue;
        }

        ECMPoint point;
        bn_copy(&point.x, &x);
        bn_copy(&point.y, &y);
        point.infinity = false;
        bool stage1_broke = false;

        /* Stage 1: primes p <= B1, multiply by p^k */
        for (uint64_t p = 2u; p <= B1; ++p) {
            if (!miller_rabin_u64(p)) continue;

            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            bn_zero(&factor);
            {
                int rc = ecm_point_mul_u64(&point, pk, &a, n, &factor);
                if (rc < 0 && bn_factor_candidate_valid(&factor, n)) {
                    fr.method = "ecm(stage1)";
                    factor_result_bn_set_from_factor(&fr, n, &factor);
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (rc <= 0 || point.infinity) { stage1_broke = true; break; }
            }
        }

        /* ── Stage 2: baby-step/giant-step ──
         * Instead of computing q·P from scratch for each prime q in (B1,B2],
         * we precompute a table of small multiples of P (baby steps), then
         * iterate large multiples (giant steps). At each giant step G=k·D·P,
         * we check gcd(product of (Gx - Bx) for each baby step, N).
         * This replaces O(π(B2)·log(B2)) point ops with O(√(B2)) point ops
         * + O(π(B2)) cheap modular multiplies.
         */
        if (!stage1_broke && !point.infinity && B2 > B1) {
            /* Choose step size D: approximately 2·√(B2-B1), rounded up to even */
            uint64_t range = B2 - B1;
            uint64_t D = 2u * (uint64_t)isqrt_u64(range);
            if (D < 4u) D = 4u;
            if (D & 1u) ++D; /* D must be even so D·P and odd baby steps cover all odd values */
            uint64_t baby_count = D / 2u; /* number of odd baby steps: 1, 3, 5, ..., D-1 */

            /* Baby steps: precompute j·P for j = 1, 2 (used as building blocks) */
            ECMPoint P1 = point;                    /* 1·P = Stage 1 endpoint */
            ECMPoint P2;
            bn_zero(&factor);
            int rc2 = ecm_point_double(&P2, &P1, &a, n, &factor);
            if (rc2 < 0 && bn_factor_candidate_valid(&factor, n)) {
                fr.method = "ecm(stage2)";
                factor_result_bn_set_from_factor(&fr, n, &factor);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (rc2 <= 0 || P2.infinity) goto ecm_next_curve;

            /* Allocate baby-step x-coordinate table */
            /* baby_x[i] = x-coord of (2i+1)·P for i = 0, 1, ..., baby_count-1 */
            BigNum* baby_x = (BigNum*)xmalloc(baby_count * sizeof(BigNum));
            bn_copy(&baby_x[0], &P1.x); /* 1·P */

            ECMPoint cur = P1; /* tracks (2i+1)·P */
            bool baby_ok = true;
            for (uint64_t i = 1u; i < baby_count; ++i) {
                /* (2(i-1)+1)·P + 2·P = (2i+1)·P */
                bn_zero(&factor);
                int rc = ecm_point_add(&cur, &cur, &P2, &a, n, &factor);
                if (rc < 0 && bn_factor_candidate_valid(&factor, n)) {
                    fr.method = "ecm(stage2)";
                    factor_result_bn_set_from_factor(&fr, n, &factor);
                    fr.time_seconds = now_sec() - t0;
                    free(baby_x);
                    return fr;
                }
                if (rc <= 0 || cur.infinity) { baby_ok = false; break; }
                bn_copy(&baby_x[i], &cur.x);
            }

            if (baby_ok) {
                /* Giant step: compute D·P */
                ECMPoint Gstep = point; /* will become D·P */
                bn_zero(&factor);
                int rcG = ecm_point_mul_u64(&Gstep, D, &a, n, &factor);
                if (rcG < 0 && bn_factor_candidate_valid(&factor, n)) {
                    fr.method = "ecm(stage2)";
                    factor_result_bn_set_from_factor(&fr, n, &factor);
                    fr.time_seconds = now_sec() - t0;
                    free(baby_x);
                    return fr;
                }

                if (rcG > 0 && !Gstep.infinity) {
                    /* Giant iteration: S starts at ceil((B1+1)/D)*D, step by D */
                    uint64_t k_start = (B1 + D) / D; /* first k such that k*D > B1 */
                    uint64_t k_end = B2 / D + 1u;    /* last k such that k*D - (D-1) <= B2 */

                    /* S = k_start * D * P */
                    ECMPoint S = point;
                    bn_zero(&factor);
                    int rcS = ecm_point_mul_u64(&S, k_start * D, &a, n, &factor);
                    if (rcS < 0 && bn_factor_candidate_valid(&factor, n)) {
                        fr.method = "ecm(stage2)";
                        factor_result_bn_set_from_factor(&fr, n, &factor);
                        fr.time_seconds = now_sec() - t0;
                        free(baby_x);
                        return fr;
                    }

                    if (rcS > 0 && !S.infinity) {
                        /* Accumulate product of (Sx - baby_x[j]) for matching primes */
                        BigNum accum;
                        bn_from_u64(&accum, 1u);
                        uint64_t gcd_interval = 0u;

                        for (uint64_t k = k_start; k <= k_end; ++k) {
                            /* For this giant step at center = k*D, check q = k*D ± (2j+1) */
                            for (uint64_t j = 0u; j < baby_count; ++j) {
                                uint64_t q_plus = k * D + (2u * j + 1u);
                                uint64_t q_minus = (k * D > 2u * j + 1u) ? k * D - (2u * j + 1u) : 0u;

                                bool want_plus = (q_plus > B1 && q_plus <= B2 && miller_rabin_u64(q_plus));
                                bool want_minus = (q_minus > B1 && q_minus <= B2 && miller_rabin_u64(q_minus));

                                if (want_plus || want_minus) {
                                    /* Accumulate (Sx - baby_x[j]) mod N */
                                    BigNum diff;
                                    bn_mod_sub(&diff, &S.x, &baby_x[j], n);
                                    if (!bn_is_zero(&diff)) {
                                        bn_mod_mul(&accum, &accum, &diff, n);
                                    }
                                }
                            }

                            /* Periodic GCD check every ~128 giant steps */
                            ++gcd_interval;
                            if (gcd_interval >= 128u || k == k_end) {
                                if (!bn_is_zero(&accum) && !bn_is_one(&accum)) {
                                    BigNum g;
                                    bn_gcd(&g, &accum, n);
                                    if (bn_factor_candidate_valid(&g, n)) {
                                        fr.method = "ecm(stage2)";
                                        factor_result_bn_set_from_factor(&fr, n, &g);
                                        fr.time_seconds = now_sec() - t0;
                                        free(baby_x);
                                        return fr;
                                    }
                                }
                                bn_from_u64(&accum, 1u);
                                gcd_interval = 0u;
                            }

                            /* Advance giant step: S += D·P */
                            if (k < k_end) {
                                bn_zero(&factor);
                                int rcA = ecm_point_add(&S, &S, &Gstep, &a, n, &factor);
                                if (rcA < 0 && bn_factor_candidate_valid(&factor, n)) {
                                    fr.method = "ecm(stage2)";
                                    factor_result_bn_set_from_factor(&fr, n, &factor);
                                    fr.time_seconds = now_sec() - t0;
                                    free(baby_x);
                                    return fr;
                                }
                                if (rcA <= 0 || S.infinity) break;
                            }
                        }
                    }
                }
            }
            free(baby_x);
        }
ecm_next_curve: (void)0;
    }

    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/* ══════════════════════════════════════════════════════════════════════
 * MONTGOMERY ECM (Pass A: Stage 1 only)
 *
 * Montgomery curve By² = x³ + Ax² + x with projective (X:Z) coordinates.
 * Suyama parametrization guarantees group order divisible by 12.
 * X-only differential ladder: zero modular inverses during scalar multiply.
 * All arithmetic via bn_mont_mul (Montgomery multiplication).
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    BigNum X;
    BigNum Z;
} ECMMontPoint;

/** xDBL: double P on Montgomery curve with parameter a24 = (A+2)/4.
 *  All values in Montgomery form. No modular inverse needed.
 *  Cost: 4 bn_mont_mul + 2 add/sub. */
static void ecm_mont_xdbl(ECMMontPoint* out, const ECMMontPoint* P,
                            const BigNum* a24_mont, const MontCtx* ctx) {
    BigNum t1, t2, t3, t4;
    /* t1 = X + Z, t2 = X - Z */
    bn_copy(&t1, &P->X); bn_add(&t1, &P->Z);
    if (bn_cmp(&t1, &ctx->n) >= 0) bn_sub(&t1, &ctx->n);
    bn_copy(&t2, &P->X);
    if (bn_cmp(&t2, &P->Z) >= 0) bn_sub(&t2, &P->Z);
    else { bn_add(&t2, &ctx->n); bn_sub(&t2, &P->Z); }
    /* t1 = t1², t2 = t2² */
    bn_mont_mul(&t3, &t1, &t1, ctx); /* t3 = (X+Z)² */
    bn_mont_mul(&t4, &t2, &t2, ctx); /* t4 = (X-Z)² */
    /* X' = t3 * t4 = (X+Z)² * (X-Z)² */
    bn_mont_mul(&out->X, &t3, &t4, ctx);
    /* t1 = t3 - t4 = (X+Z)² - (X-Z)² = 4XZ */
    bn_copy(&t1, &t3);
    if (bn_cmp(&t1, &t4) >= 0) bn_sub(&t1, &t4);
    else { bn_add(&t1, &ctx->n); bn_sub(&t1, &t4); }
    /* t2 = t4 + a24 * t1 = (X-Z)² + a24 * 4XZ */
    bn_mont_mul(&t2, a24_mont, &t1, ctx);
    bn_add(&t2, &t4);
    if (bn_cmp(&t2, &ctx->n) >= 0) bn_sub(&t2, &ctx->n);
    /* Z' = t1 * t2 = 4XZ * ((X-Z)² + a24*4XZ) */
    bn_mont_mul(&out->Z, &t1, &t2, ctx);
}

/** xADD: differential addition P+Q given P, Q, and P-Q.
 *  All values in Montgomery form. No modular inverse needed.
 *  Cost: 4 bn_mont_mul + 2 add/sub. */
static void ecm_mont_xadd(ECMMontPoint* out, const ECMMontPoint* P,
                            const ECMMontPoint* Q, const ECMMontPoint* diff,
                            const MontCtx* ctx) {
    BigNum u, v, t1, t2;
    /* u = (X_P - Z_P)(X_Q + Z_Q) */
    bn_copy(&t1, &P->X);
    if (bn_cmp(&t1, &P->Z) >= 0) bn_sub(&t1, &P->Z);
    else { bn_add(&t1, &ctx->n); bn_sub(&t1, &P->Z); }
    bn_copy(&t2, &Q->X); bn_add(&t2, &Q->Z);
    if (bn_cmp(&t2, &ctx->n) >= 0) bn_sub(&t2, &ctx->n);
    bn_mont_mul(&u, &t1, &t2, ctx);
    /* v = (X_P + Z_P)(X_Q - Z_Q) */
    bn_copy(&t1, &P->X); bn_add(&t1, &P->Z);
    if (bn_cmp(&t1, &ctx->n) >= 0) bn_sub(&t1, &ctx->n);
    bn_copy(&t2, &Q->X);
    if (bn_cmp(&t2, &Q->Z) >= 0) bn_sub(&t2, &Q->Z);
    else { bn_add(&t2, &ctx->n); bn_sub(&t2, &Q->Z); }
    bn_mont_mul(&v, &t1, &t2, ctx);
    /* X' = Z_{P-Q} * (u + v)² */
    bn_copy(&t1, &u); bn_add(&t1, &v);
    if (bn_cmp(&t1, &ctx->n) >= 0) bn_sub(&t1, &ctx->n);
    bn_mont_mul(&t2, &t1, &t1, ctx); /* (u+v)² */
    bn_mont_mul(&out->X, &diff->Z, &t2, ctx);
    /* Z' = X_{P-Q} * (u - v)² */
    bn_copy(&t1, &u);
    if (bn_cmp(&t1, &v) >= 0) bn_sub(&t1, &v);
    else { bn_add(&t1, &ctx->n); bn_sub(&t1, &v); }
    bn_mont_mul(&t2, &t1, &t1, ctx); /* (u-v)² */
    bn_mont_mul(&out->Z, &diff->X, &t2, ctx);
}

/** Montgomery ladder: compute [k]P using the x-only differential chain.
 *  Cost: exactly log2(k) xDBL + log2(k) xADD = 8M per bit. */
static void ecm_mont_ladder(ECMMontPoint* result, const ECMMontPoint* P,
                              uint64_t k, const BigNum* a24_mont, const MontCtx* ctx) {
    if (k == 0u) { bn_from_u64(&result->X, 0u); bn_from_u64(&result->Z, 0u); return; }
    if (k == 1u) { *result = *P; return; }

    ECMMontPoint R0 = *P;
    ECMMontPoint R1;
    ecm_mont_xdbl(&R1, P, a24_mont, ctx);

    uint64_t mask = (uint64_t)1u << 62u;
    while (mask > 0u && !(k & mask)) mask >>= 1u;
    mask >>= 1u; /* skip leading 1 bit */

    while (mask > 0u) {
        if (k & mask) {
            ECMMontPoint tmp;
            ecm_mont_xadd(&tmp, &R0, &R1, P, ctx);
            R0 = tmp;
            ecm_mont_xdbl(&tmp, &R1, a24_mont, ctx);
            R1 = tmp;
        } else {
            ECMMontPoint tmp;
            ecm_mont_xadd(&tmp, &R0, &R1, P, ctx);
            R1 = tmp;
            ecm_mont_xdbl(&tmp, &R0, a24_mont, ctx);
            R0 = tmp;
        }
        mask >>= 1u;
    }

    *result = R0;
}

/** Wrapper preserving old call signature (Stage 1 only, default B2). */
static FactorResultBigNum cpss_factor_ecm_mont_stage1(const BigNum* n, uint64_t B1,
                                                        uint64_t curves, uint64_t seed) {
    return cpss_factor_ecm_mont(n, B1, 0u, curves, seed);
}

/** ECM for uint64 — promotes to BigNum, runs Montgomery ECM, converts result back. */
static FactorResult cpss_factor_ecm_u64(uint64_t n, uint64_t B1, uint64_t curves, uint64_t seed) {
    FactorResult fr;
    factor_result_init(&fr, n, "ecm-mont");
    BigNum bn; bn_from_u64(&bn, n);
    FactorResultBigNum frbn = cpss_factor_ecm_mont(&bn, B1, 0u, curves, seed);
    fr.method = frbn.method;
    fr.status = frbn.status;
    fr.time_seconds = frbn.time_seconds;
    fr.fully_factored = frbn.fully_factored;
    if (frbn.factor_found && bn_fits_u64(&frbn.factor)) {
        uint64_t f = bn_to_u64(&frbn.factor);
        if (f > 1u && f < n) {
            factor_result_set_split(&fr, f, n / f);
        }
    }
    return fr;
}

/** ECM for U128 — promotes to BigNum, runs Montgomery ECM, converts result back. */
static FactorResultU128 cpss_factor_ecm_u128(U128 n, uint64_t B1, uint64_t curves, uint64_t seed) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "ecm-mont");
    /* Build BigNum from U128 */
    BigNum bn;
    bn_from_u64(&bn, n.lo);
    if (n.hi != 0u) {
        BigNum hi; bn_from_u64(&hi, n.hi);
        for (int sh = 0; sh < 64; ++sh) bn_shl1(&hi);
        bn_add(&bn, &hi);
    }
    FactorResultBigNum frbn = cpss_factor_ecm_mont(&bn, B1, 0u, curves, seed);
    fr.method = frbn.method;
    fr.status = frbn.status;
    fr.time_seconds = frbn.time_seconds;
    fr.fully_factored = frbn.fully_factored;
    if (frbn.factor_found && bn_fits_u64(&frbn.factor)) {
        uint64_t f = bn_to_u64(&frbn.factor);
        if (f > 1u) {
            factor_result_u128_add(&fr, f);
            fr.cofactor = u128_div(n, u128_from_u64(f));
            if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                factor_result_u128_add(&fr, fr.cofactor.lo);
                fr.cofactor = u128_from_u64(1u);
                fr.fully_factored = true;
            }
        }
    }
    return fr;
}

/**
 * Montgomery ECM with Stage 1 + Stage 2 BSGS.
 * Suyama parametrization + x-only Montgomery ladder.
 * Stage 1: prime powers up to B1 via Montgomery ladder.
 * Stage 2: baby-step/giant-step over primes in (B1, B2].
 * Factor detected via Z-coordinate accumulation + periodic GCD.
 */
static FactorResultBigNum cpss_factor_ecm_mont(const BigNum* n, uint64_t B1, uint64_t B2,
                                                 uint64_t curves, uint64_t seed) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "ecm-mont";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();
    B1 = ecm_default_B1(B1);
    B2 = ecm_default_B2(B2, B1);
    curves = ecm_default_curves(curves);
    seed = ecm_default_seed(seed);

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr;
    }
    if (bn_is_even(n)) {
        BigNum two; bn_from_u64(&two, 2u);
        factor_result_bn_set_from_factor(&fr, n, &two);
        fr.time_seconds = now_sec() - t0; return fr;
    }
    if (bn_factor_is_prime(n)) {
        fr.cofactor_is_prime = true; fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0; return fr;
    }

    /* Montgomery setup (n must be odd — guaranteed by even check above) */
    MontCtx ctx;
    if (!bn_montgomery_setup(&ctx, n)) {
        fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr;
    }

    for (uint64_t curve = 0u; curve < curves; ++curve) {
        /* ── Suyama parametrization ──
         * σ ∈ [6, N-1], u = σ²-5, v = 4σ
         * Q₀ = (u³ : v³)  in projective coords (no inverse needed for point)
         * a24 = (v-u)³·(3u+v) / (16·u³·v)  [needs one inverse → GCD check] */
        BigNum sigma;
        bn_random_below(&sigma, n, &seed);
        /* Ensure σ >= 6 */
        if (bn_fits_u64(&sigma) && bn_to_u64(&sigma) < 6u) bn_from_u64(&sigma, 6u + (seed & 0xFFu));

        BigNum u, v;
        bn_mod_mul(&u, &sigma, &sigma, n);             /* u = σ² mod N */
        BigNum five; bn_from_u64(&five, 5u);
        if (bn_cmp(&u, &five) >= 0) bn_sub(&u, &five); /* u = σ²-5 mod N */
        else { bn_add(&u, n); bn_sub(&u, &five); }

        bn_copy(&v, &sigma); bn_shl1(&v); bn_shl1(&v); /* v = 4σ */
        if (bn_cmp(&v, n) >= 0) { BigNum tmp; bn_copy(&tmp, &v); bn_sub(&tmp, n); bn_copy(&v, &tmp); }

        /* u3 = u³, v3 = v³ */
        BigNum u2, u3, v3;
        bn_mod_mul(&u2, &u, &u, n);
        bn_mod_mul(&u3, &u2, &u, n);
        BigNum v2; bn_mod_mul(&v2, &v, &v, n);
        bn_mod_mul(&v3, &v2, &v, n);

        /* Q₀ = (u³ : v³) in standard coords; convert to Montgomery form */
        ECMMontPoint Q0;
        bn_to_mont(&Q0.X, &u3, &ctx);
        bn_to_mont(&Q0.Z, &v3, &ctx);

        /* a24 = (v-u)³·(3u+v) / (16·u³·v) mod N
         * Denominator = 16·u³·v. Compute inverse, check GCD. */
        BigNum vu_diff;
        if (bn_cmp(&v, &u) >= 0) { bn_copy(&vu_diff, &v); bn_sub(&vu_diff, &u); }
        else { bn_copy(&vu_diff, &v); bn_add(&vu_diff, n); bn_sub(&vu_diff, &u); }
        BigNum vu3; /* (v-u)³ */
        bn_mod_mul(&u2, &vu_diff, &vu_diff, n); /* reuse u2 as temp */
        bn_mod_mul(&vu3, &u2, &vu_diff, n);
        BigNum three_u; bn_copy(&three_u, &u); bn_add(&three_u, &u);
        if (bn_cmp(&three_u, n) >= 0) bn_sub(&three_u, n);
        bn_add(&three_u, &u);
        if (bn_cmp(&three_u, n) >= 0) bn_sub(&three_u, n);
        BigNum three_u_plus_v; bn_copy(&three_u_plus_v, &three_u); bn_add(&three_u_plus_v, &v);
        if (bn_cmp(&three_u_plus_v, n) >= 0) bn_sub(&three_u_plus_v, n);
        BigNum numer;
        bn_mod_mul(&numer, &vu3, &three_u_plus_v, n); /* (v-u)³·(3u+v) */

        BigNum sixteen; bn_from_u64(&sixteen, 16u);
        BigNum denom;
        bn_mod_mul(&denom, &u3, &v, n);     /* u³·v */
        bn_mod_mul(&denom, &denom, &sixteen, n); /* 16·u³·v */

        BigNum factor_found;
        bn_zero(&factor_found);
        BigNum denom_inv;
        if (!bn_mod_inverse_checked(&denom_inv, &denom, n, &factor_found)) {
            if (bn_factor_candidate_valid(&factor_found, n)) {
                factor_result_bn_set_from_factor(&fr, n, &factor_found);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            continue; /* degenerate curve, try next */
        }

        BigNum a24;
        bn_mod_mul(&a24, &numer, &denom_inv, n);

        /* Convert a24 to Montgomery form */
        BigNum a24_mont;
        bn_to_mont(&a24_mont, &a24, &ctx);

        /* ── Stage 1: multiply Q by all prime powers p^k ≤ B1 ──
         * Accumulate Z products in Montgomery form (cheap bn_mont_mul).
         * Only convert to standard form for periodic GCD checks. */
        BigNum z_accum_mont;
        { BigNum one; bn_from_u64(&one, 1u); bn_to_mont(&z_accum_mont, &one, &ctx); }
        uint64_t gcd_counter = 0u;
        bool curve_failed = false;

        for (uint64_t p = 2u; p <= B1; ++p) {
            if (!miller_rabin_u64(p)) continue;
            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            ecm_mont_ladder(&Q0, &Q0, pk, &a24_mont, &ctx);

            /* Accumulate Z in Montgomery form (no conversion per prime) */
            bn_mont_mul(&z_accum_mont, &z_accum_mont, &Q0.Z, &ctx);

            /* Periodic GCD every 64 primes (convert once, check once) */
            ++gcd_counter;
            if (gcd_counter >= 64u) {
                gcd_counter = 0u;
                BigNum z_std;
                bn_from_mont(&z_std, &z_accum_mont, &ctx);
                BigNum g;
                bn_gcd(&g, &z_std, n);
                if (bn_factor_candidate_valid(&g, n)) {
                    fr.method = "ecm-mont(stage1)";
                    factor_result_bn_set_from_factor(&fr, n, &g);
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (!bn_is_one(&g)) { curve_failed = true; break; }
                { BigNum one; bn_from_u64(&one, 1u); bn_to_mont(&z_accum_mont, &one, &ctx); }
            }
        }

        /* Final Stage 1 GCD check for this curve */
        if (!curve_failed) {
            BigNum z_std;
            bn_from_mont(&z_std, &z_accum_mont, &ctx);
            if (!bn_is_one(&z_std)) {
                BigNum g;
                bn_gcd(&g, &z_std, n);
                if (bn_factor_candidate_valid(&g, n)) {
                    fr.method = "ecm-mont(stage1)";
                    factor_result_bn_set_from_factor(&fr, n, &g);
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
            }
        }

        /* ── Stage 2: baby-step/giant-step using Montgomery points ──
         * Baby steps: precompute (2i+1)·Q for i=0..D/2-1 using xADD with step 2·Q.
         * Giant steps: S = k·D·Q, iterate k, accumulate (Sx·Bz - Bx·Sz) mod N.
         * All arithmetic in Montgomery form. Periodic GCD checks. */
        if (!curve_failed && B2 > B1) {
            uint64_t range = B2 - B1;
            uint64_t D = 2u * (uint64_t)isqrt_u64(range);
            if (D < 4u) D = 4u;
            if (D & 1u) ++D;
            uint64_t baby_count = D / 2u;

            /* 2·Q for building baby steps */
            ECMMontPoint Q2;
            ecm_mont_xdbl(&Q2, &Q0, &a24_mont, &ctx);

            /* Baby-step table: store full (X,Z) in Montgomery form */
            ECMMontPoint* baby = (ECMMontPoint*)xmalloc(baby_count * sizeof(ECMMontPoint));
            baby[0] = Q0; /* 1·Q */
            bool baby_ok = true;

            if (baby_count > 1u) {
                /* 3·Q = Q + 2·Q (diff = Q) */
                ecm_mont_xadd(&baby[1], &Q0, &Q2, &Q0, &ctx);
                for (uint64_t i = 2u; i < baby_count; ++i) {
                    /* (2i+1)·Q = (2(i-1)+1)·Q + 2·Q, diff = (2(i-2)+1)·Q */
                    ecm_mont_xadd(&baby[i], &baby[i - 1u], &Q2, &baby[i - 2u], &ctx);
                    /* Check for degenerate zero Z */
                    if (bn_is_zero(&baby[i].Z)) { baby_ok = false; break; }
                }
            }

            if (baby_ok) {
                /* Giant step: D·Q */
                ECMMontPoint GD;
                ecm_mont_ladder(&GD, &Q0, D, &a24_mont, &ctx);

                if (!bn_is_zero(&GD.Z)) {
                    /* Starting giant step: S = k_start·D·Q */
                    uint64_t k_start = (B1 + D) / D;
                    uint64_t k_end = B2 / D + 1u;
                    ECMMontPoint S;
                    ecm_mont_ladder(&S, &Q0, k_start * D, &a24_mont, &ctx);

                    if (!bn_is_zero(&S.Z)) {
                        /* Accumulate products in Montgomery form */
                        BigNum accum_mont;
                        { BigNum one; bn_from_u64(&one, 1u); bn_to_mont(&accum_mont, &one, &ctx); }
                        uint64_t gcd_iv = 0u;

                        /* We need the previous giant step for xADD differential */
                        ECMMontPoint S_prev;
                        ecm_mont_ladder(&S_prev, &Q0, (k_start - 1u) * D, &a24_mont, &ctx);

                        for (uint64_t k = k_start; k <= k_end; ++k) {
                            /* Accumulate (Sx·Bz - Bx·Sz) for ALL baby steps.
                             * No per-pair primality test — non-prime q values add
                             * harmless extra factors to the product. The GCD still
                             * correctly detects factors. This is ~100x faster than
                             * testing each candidate for primality. */
                            for (uint64_t j = 0u; j < baby_count; ++j) {
                                BigNum t1, t2, diff;
                                bn_mont_mul(&t1, &S.X, &baby[j].Z, &ctx);
                                bn_mont_mul(&t2, &baby[j].X, &S.Z, &ctx);
                                if (bn_cmp(&t1, &t2) >= 0) {
                                    bn_copy(&diff, &t1); bn_sub(&diff, &t2);
                                } else {
                                    bn_copy(&diff, &t1); bn_add(&diff, &ctx.n); bn_sub(&diff, &t2);
                                }
                                if (!bn_is_zero(&diff)) {
                                    bn_mont_mul(&accum_mont, &accum_mont, &diff, &ctx);
                                }
                            }

                            ++gcd_iv;
                            if (gcd_iv >= 128u || k == k_end) {
                                BigNum acc_std;
                                bn_from_mont(&acc_std, &accum_mont, &ctx);
                                if (!bn_is_zero(&acc_std) && !bn_is_one(&acc_std)) {
                                    BigNum g;
                                    bn_gcd(&g, &acc_std, n);
                                    if (bn_factor_candidate_valid(&g, n)) {
                                        fr.method = "ecm-mont(stage2)";
                                        factor_result_bn_set_from_factor(&fr, n, &g);
                                        fr.time_seconds = now_sec() - t0;
                                        free(baby);
                                        return fr;
                                    }
                                }
                                { BigNum one; bn_from_u64(&one, 1u); bn_to_mont(&accum_mont, &one, &ctx); }
                                gcd_iv = 0u;
                            }

                            /* Advance giant step: S_new = S + GD (diff = S_prev) */
                            if (k < k_end) {
                                ECMMontPoint S_next;
                                ecm_mont_xadd(&S_next, &S, &GD, &S_prev, &ctx);
                                S_prev = S;
                                S = S_next;
                                if (bn_is_zero(&S.Z)) break;
                            }
                        }
                    }
                }
            }
            free(baby);
        }
    }

    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

static FactorResult cpss_factor_fermat(uint64_t n, uint64_t max_steps) {
    FactorResult fr;
    factor_result_init(&fr, n, "fermat");
    double t0 = now_sec();
    max_steps = fermat_default_steps_small(max_steps);

    if (n <= 1u) {
        fr.cofactor = n;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    while ((fr.cofactor & 1u) == 0u) {
        factor_result_add(&fr, 2u);
        fr.cofactor /= 2u;
    }
    if (fr.cofactor <= 1u) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }
    if (miller_rabin_u64(fr.cofactor)) {
        factor_result_add(&fr, fr.cofactor);
        fr.cofactor = 1u;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    uint64_t target = fr.cofactor;
    uint64_t a = (uint64_t)isqrt_u64(target);
    if (a * a < target) ++a;

    U128 b2 = u128_sub(u128_mul64(a, a), u128_from_u64(target));
    uint64_t b = u128_isqrt_floor(b2);
    U128 b_sq = u128_mul64(b, b);
    U128 next_sq = b_sq;
    {
        U128 delta = u128_from_u64(b);
        delta = u128_shl(delta, 1u);
        delta = u128_add(delta, u128_from_u64(1u));
        next_sq = u128_add(next_sq, delta);
    }

    for (uint64_t step = 0u;; ++step) {
        while (u128_le(next_sq, b2)) {
            ++b;
            b_sq = next_sq;
            U128 delta = u128_from_u64(b);
            delta = u128_shl(delta, 1u);
            delta = u128_add(delta, u128_from_u64(1u));
            next_sq = u128_add(b_sq, delta);
        }

        if (u128_eq(b_sq, b2)) {
            uint64_t left = a - b;
            uint64_t right = target / left;
            factor_result_set_split(&fr, left, right);
            fr.time_seconds = now_sec() - t0;
            return fr;
        }

        if (step >= max_steps) break;

        U128 delta = u128_from_u64(a);
        delta = u128_shl(delta, 1u);
        delta = u128_add(delta, u128_from_u64(1u));
        b2 = u128_add(b2, delta);
        ++a;
    }

    fr.cofactor = target;
    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

static FactorResultU128 cpss_factor_fermat_u128(U128 n, uint64_t max_steps) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "fermat");
    double t0 = now_sec();
    max_steps = fermat_default_steps_small(max_steps);

    if (u128_le(n, u128_from_u64(1u))) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    while (u128_mod_u64(fr.cofactor, 2u) == 0u) {
        factor_result_u128_add(&fr, 2u);
        fr.cofactor = u128_div(fr.cofactor, u128_from_u64(2u));
    }
    if (u128_le(fr.cofactor, u128_from_u64(1u))) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }
    if (u128_is_prime(fr.cofactor)) {
        if (u128_fits_u64(fr.cofactor)) {
            factor_result_u128_add(&fr, fr.cofactor.lo);
            fr.cofactor = u128_from_u64(1u);
        }
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    U128 target = fr.cofactor;
    uint64_t a = u128_isqrt_ceil(target);
    U128 b2 = u128_sub(u128_mul64(a, a), target);
    uint64_t b = u128_isqrt_floor(b2);
    U128 b_sq = u128_mul64(b, b);
    U128 next_sq = b_sq;
    {
        U128 delta = u128_from_u64(b);
        delta = u128_shl(delta, 1u);
        delta = u128_add(delta, u128_from_u64(1u));
        next_sq = u128_add(next_sq, delta);
    }

    for (uint64_t step = 0u;; ++step) {
        while (u128_le(next_sq, b2)) {
            ++b;
            b_sq = next_sq;
            U128 delta = u128_from_u64(b);
            delta = u128_shl(delta, 1u);
            delta = u128_add(delta, u128_from_u64(1u));
            next_sq = u128_add(b_sq, delta);
        }

        if (u128_eq(b_sq, b2)) {
            uint64_t left = a - b;
            U128 left_u128 = u128_from_u64(left);
            U128 right = u128_div(target, left_u128);
            factor_result_u128_set_split(&fr, left_u128, right);
            fr.time_seconds = now_sec() - t0;
            return fr;
        }

        if (step >= max_steps) break;

        U128 delta = u128_from_u64(a);
        delta = u128_shl(delta, 1u);
        delta = u128_add(delta, u128_from_u64(1u));
        b2 = u128_add(b2, delta);
        ++a;
    }

    fr.cofactor = target;
    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

static FactorResultBigNum cpss_factor_fermat_bn(const BigNum* n, uint64_t max_steps) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "fermat";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();
    max_steps = fermat_default_steps_bn(max_steps);

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_is_even(n)) {
        bn_from_u64(&fr.factor, 2u);
        bn_copy(&fr.cofactor, n);
        (void)bn_div_u64(&fr.cofactor, 2u);
        fr.factor_found = true;
        fr.factor_is_prime = true;
        fr.cofactor_is_prime = bn_is_one(&fr.cofactor) ? true : bn_factor_is_prime(&fr.cofactor);
        fr.fully_factored = fr.cofactor_is_prime;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_factor_is_prime(n)) {
        fr.cofactor_is_prime = true;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    BigNum a;
    bn_isqrt_ceil(&a, n);

    BigNum a_sq;
    bn_mul(&a_sq, &a, &a);
    BigNum b2;
    bn_copy(&b2, &a_sq);
    bn_sub(&b2, n);

    BigNum b;
    bn_isqrt_floor(&b, &b2);
    BigNum b_sq;
    bn_mul(&b_sq, &b, &b);
    BigNum next_sq;
    bn_copy(&next_sq, &b_sq);
    {
        BigNum delta;
        bn_copy(&delta, &b);
        bn_shl1(&delta);
        bn_add_u64(&delta, 1u);
        bn_add(&next_sq, &delta);
    }

    for (uint64_t step = 0u;; ++step) {
        while (bn_cmp(&next_sq, &b2) <= 0) {
            bn_add_u64(&b, 1u);
            bn_copy(&b_sq, &next_sq);
            BigNum delta;
            bn_copy(&delta, &b);
            bn_shl1(&delta);
            bn_add_u64(&delta, 1u);
            bn_copy(&next_sq, &b_sq);
            bn_add(&next_sq, &delta);
        }

        if (bn_cmp(&b_sq, &b2) == 0) {
            BigNum left, right;
            bn_copy(&left, &a);
            bn_sub(&left, &b);
            bn_copy(&right, &a);
            bn_add(&right, &b);
            factor_result_bn_set_split(&fr, &left, &right);
            fr.time_seconds = now_sec() - t0;
            return fr;
        }

        if (step >= max_steps) break;

        BigNum delta;
        bn_copy(&delta, &a);
        bn_shl1(&delta);
        bn_add_u64(&delta, 1u);
        bn_add(&b2, &delta);
        bn_add_u64(&a, 1u);
    }

    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/* ══════════════════════════════════════════════════════════════════════
 * TRIAL DIVISION ENGINE (CPSS-accelerated)
 *
 * Divides n by wheel primes, then streams primes from the DB via
 * PrimeIterator up to min(limit, sqrt(n), db_hi).
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Factor n by trial division using CPSS-streamed primes.
 * limit: max prime to try (0 = sqrt(n)).
 * Returns FactorResult with found factors, cofactor, and status.
 */
static FactorResult cpss_factor_trial(CPSSStream* s, uint64_t n, uint64_t limit) {
    FactorResult fr;
    factor_result_init(&fr, n, "trial");
    double t0 = now_sec();

    if (n <= 1u) {
        fr.cofactor = n;
        fr.fully_factored = (n <= 1u);
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Divide by wheel primes first */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        while (fr.cofactor % p == 0u) {
            factor_result_add(&fr, p);
            fr.cofactor /= p;
        }
    }

    if (fr.cofactor <= 1u) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Determine trial limit */
    uint64_t sqrt_cof = (uint64_t)isqrt_u64(fr.cofactor);
    if (sqrt_cof * sqrt_cof < fr.cofactor) ++sqrt_cof;
    uint64_t trial_limit = (limit > 0u && limit < sqrt_cof) ? limit : sqrt_cof;

    /* Check DB coverage */
    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) {
        db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    }

    /* Effective limit is min(trial_limit, db_hi) */
    uint64_t effective_limit = trial_limit;
    if (db_hi > 0u && db_hi < effective_limit) {
        effective_limit = db_hi;
        if (effective_limit < sqrt_cof) {
            fr.status = (int)CPSS_INCOMPLETE;
        }
    }
    else if (db_hi == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Stream primes from DB via iterator */
    PrimeIterator iter;
    cpss_iter_init(&iter, s, WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u);

    while (iter.valid && iter.current <= effective_limit) {
        uint64_t p = iter.current;

        /* Early stop: if p*p > cofactor, cofactor is prime */
        if (p > 0u && p <= UINT32_MAX && (uint64_t)p * p > fr.cofactor) {
            break;
        }

        while (fr.cofactor % p == 0u) {
            factor_result_add(&fr, p);
            fr.cofactor /= p;
        }

        if (fr.cofactor <= 1u) break;
        cpss_iter_next(&iter);
    }

    /* Check if fully factored */
    if (fr.cofactor <= 1u) {
        fr.fully_factored = true;
    }
    else if (fr.cofactor > 1u) {
        /* If cofactor's sqrt is within our trial range, cofactor must be prime */
        uint64_t sqrt_remaining = (uint64_t)isqrt_u64(fr.cofactor);
        if (sqrt_remaining <= effective_limit) {
            /* cofactor is prime */
            factor_result_add(&fr, fr.cofactor);
            fr.cofactor = 1u;
            fr.fully_factored = true;
        }
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/* ══════════════════════════════════════════════════════════════════════
 * POLLARD p-1 ENGINE (CPSS-accelerated stage 1)
 *
 * Computes M = lcm(1, 2, ..., B1) by iterating prime powers p^k <= B1,
 * then checks gcd(a^M - 1, n). Primes streamed from DB via iterator.
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Factor n using Pollard's p-1 method with smoothness bound B1.
 * Uses CPSS to stream primes for stage 1.
 * B1 = 0 defaults to min(1000000, db_hi).
 */
static FactorResult cpss_factor_pminus1(CPSSStream* s, uint64_t n, uint64_t B1) {
    FactorResult fr;
    factor_result_init(&fr, n, "pminus1");
    double t0 = now_sec();

    if (n <= 3u || (n & 1u) == 0u) {
        /* Handle trivially */
        if (n <= 1u) { fr.fully_factored = true; fr.cofactor = n; }
        else if ((n & 1u) == 0u) {
            while (fr.cofactor % 2u == 0u) {
                factor_result_add(&fr, 2u);
                fr.cofactor /= 2u;
            }
            if (fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
            }
            fr.fully_factored = (fr.cofactor <= 1u);
        }
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Default B1 */
    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) {
        db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    }
    if (B1 == 0u) {
        B1 = 1000000u;
        if (db_hi > 0u && db_hi < B1) B1 = db_hi;
    }
    if (db_hi > 0u && B1 > db_hi) {
        B1 = db_hi;
        fr.status = (int)CPSS_INCOMPLETE;
    }
    if (db_hi == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Stage 1: compute a^M mod n where M = product of prime powers <= B1 */
    uint64_t base = 2u; /* starting base */
    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);

    while (iter.valid && iter.current <= B1) {
        uint64_t p = iter.current;

        /* Compute p^k where p^k <= B1 */
        uint64_t pk = p;
        while (pk <= B1 / p) pk *= p; /* largest power of p <= B1 */

        base = powmod_u64(base, pk, n);

        /* Periodic GCD check */
        uint64_t g = gcd_u64(base > 0u ? base - 1u : n - 1u, n);
        if (g > 1u && g < n) {
            fr.method = "pminus1(stage1)";
            factor_result_add(&fr, g);
            fr.cofactor = n / g;
            if (miller_rabin_u64(g) && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            else if (miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            fr.time_seconds = now_sec() - t0;
            return fr;
        }
        if (g == n) {
            break;
        }

        cpss_iter_next(&iter);
    }

    /* Final Stage 1 GCD check */
    if (base > 1u) {
        uint64_t g = gcd_u64(base - 1u, n);
        if (g > 1u && g < n) {
            fr.method = "pminus1(stage1)";
            factor_result_add(&fr, g);
            fr.cofactor = n / g;
            if (miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            fr.time_seconds = now_sec() - t0;
            return fr;
        }
    }

    /* Stage 2: primes q in (B1, B2], compute base^q mod n, check GCD.
     * Catches factors where p-1 has one large prime factor in (B1, B2]. */
    if (!fr.fully_factored && base > 1u) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                uint64_t bq = powmod_u64(base, q, n);
                uint64_t g = gcd_u64(bq > 0u ? bq - 1u : n - 1u, n);
                if (g > 1u && g < n) {
                    fr.method = "pminus1(stage2)";
                    factor_result_add(&fr, g);
                    fr.cofactor = n / g;
                    if (miller_rabin_u64(fr.cofactor)) {
                        factor_result_add(&fr, fr.cofactor);
                        fr.cofactor = 1u;
                        fr.fully_factored = true;
                    }
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (g == n) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) {
        fr.status = (int)CPSS_INCOMPLETE;
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/* ══════════════════════════════════════════════════════════════════════
 * WILLIAMS p+1 FACTORISATION (CPSS-accelerated)
 *
 * Complementary to p-1: finds factor p when p+1 is B-smooth.
 * Uses Lucas sequences V_k(a) mod n.
 * Core recurrence: V_{2k} = V_k^2 - 2,  V_{k+l} = V_k*V_l - V_{k-l}
 * For prime power exponent: V_{pk}(a) = V_k(V_p(a)) by composition.
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Compute V_e(a) mod n using the Lucas chain (uint64 version).
 * Uses the standard binary ladder: maintains (V_k, V_{k+1}) pair.
 */
static uint64_t lucas_chain_u64(uint64_t a, uint64_t e, uint64_t n) {
    if (e == 0u) return 2u % n;
    if (e == 1u) return a % n;

    uint64_t vk = a % n;           /* V_1 */
    uint64_t vk1 = mulmod_u64(a, a, n);
    if (vk1 >= 2u) vk1 -= 2u; else vk1 = vk1 + n - 2u; /* V_2 = a^2 - 2 */

    /* Scan bits of e from second-highest down */
    uint64_t mask = (uint64_t)1u << 62u;
    while (mask > 0u && !(e & mask)) mask >>= 1u;
    mask >>= 1u; /* skip the leading 1 bit (already handled by V_1) */

    while (mask > 0u) {
        if (e & mask) {
            /* (V_k, V_{k+1}) → (V_{2k+1}, V_{2k+2}) */
            uint64_t t = mulmod_u64(vk, vk1, n);
            t = (t >= a % n) ? t - a % n : t + n - a % n;
            vk = t;
            vk1 = mulmod_u64(vk1, vk1, n);
            vk1 = (vk1 >= 2u) ? vk1 - 2u : vk1 + n - 2u;
        }
        else {
            /* (V_k, V_{k+1}) → (V_{2k}, V_{2k+1}) */
            uint64_t t = mulmod_u64(vk, vk1, n);
            t = (t >= a % n) ? t - a % n : t + n - a % n;
            vk1 = t;
            vk = mulmod_u64(vk, vk, n);
            vk = (vk >= 2u) ? vk - 2u : vk + n - 2u;
        }
        mask >>= 1u;
    }
    return vk;
}

/** Williams p+1 factorisation (uint64 version). */
static FactorResult cpss_factor_pplus1(CPSSStream* s, uint64_t n, uint64_t B1) {
    FactorResult fr;
    factor_result_init(&fr, n, "pplus1");
    double t0 = now_sec();

    if (n <= 3u || (n & 1u) == 0u) {
        if (n <= 1u) { fr.fully_factored = true; fr.cofactor = n; }
        else if ((n & 1u) == 0u) {
            while (fr.cofactor % 2u == 0u) { factor_result_add(&fr, 2u); fr.cofactor /= 2u; }
            if (fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u;
            }
            fr.fully_factored = (fr.cofactor <= 1u);
        }
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (B1 == 0u) { B1 = 1000000u; if (db_hi > 0u && db_hi < B1) B1 = db_hi; }
    if (db_hi > 0u && B1 > db_hi) { B1 = db_hi; fr.status = (int)CPSS_INCOMPLETE; }
    if (db_hi == 0u) { fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr; }

    /* Try a few starting values of a (the Lucas parameter) */
    static const uint64_t starts[] = { 3, 5, 7, 11, 13, 17, 19, 23 };
    uint64_t stage1_v = 0u; /* save last v for Stage 2 */
    for (size_t si = 0u; si < ARRAY_LEN(starts); ++si) {
        uint64_t v = starts[si] % n;
        PrimeIterator iter;
        cpss_iter_init(&iter, s, 2u);

        while (iter.valid && iter.current <= B1) {
            uint64_t p = iter.current;
            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            v = lucas_chain_u64(v, pk, n);

            uint64_t g = gcd_u64((v >= 2u) ? v - 2u : v + n - 2u, n);
            if (g > 1u && g < n) {
                fr.method = "pplus1(stage1)";
                factor_result_add(&fr, g);
                fr.cofactor = n / g;
                if (miller_rabin_u64(g) && miller_rabin_u64(fr.cofactor)) {
                    factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u; fr.fully_factored = true;
                } else if (miller_rabin_u64(fr.cofactor)) {
                    factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u; fr.fully_factored = true;
                }
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (g == n) break;

            cpss_iter_next(&iter);
        }
        stage1_v = v;
    }

    /* Stage 2: primes q in (B1, B2], compute V_q(v) via Lucas chain */
    if (!fr.fully_factored && stage1_v > 0u) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                uint64_t vq = lucas_chain_u64(stage1_v, q, n);
                uint64_t g = gcd_u64((vq >= 2u) ? vq - 2u : vq + n - 2u, n);
                if (g > 1u && g < n) {
                    fr.method = "pplus1(stage2)";
                    factor_result_add(&fr, g);
                    fr.cofactor = n / g;
                    if (miller_rabin_u64(g) && miller_rabin_u64(fr.cofactor)) {
                        factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u; fr.fully_factored = true;
                    } else if (miller_rabin_u64(fr.cofactor)) {
                        factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u; fr.fully_factored = true;
                    }
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (g == n) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/** Lucas chain for U128 modulus. */
static U128 lucas_chain_u128(U128 a, uint64_t e, U128 n) {
    if (e == 0u) return u128_from_u64(2u);
    if (e == 1u) return a;

    U128 two = u128_from_u64(2u);
    U128 a_mod = u128_mod(a, n);
    U128 vk = a_mod;
    U128 vk1 = u128_mulmod(a_mod, a_mod, n);
    if (u128_le(two, vk1)) vk1 = u128_sub(vk1, two);
    else vk1 = u128_sub(u128_add(vk1, n), two);

    uint64_t mask = (uint64_t)1u << 62u;
    while (mask > 0u && !(e & mask)) mask >>= 1u;
    mask >>= 1u;

    while (mask > 0u) {
        if (e & mask) {
            U128 t = u128_mulmod(vk, vk1, n);
            if (u128_le(a_mod, t)) t = u128_sub(t, a_mod);
            else t = u128_sub(u128_add(t, n), a_mod);
            vk = t;
            vk1 = u128_mulmod(vk1, vk1, n);
            if (u128_le(two, vk1)) vk1 = u128_sub(vk1, two);
            else vk1 = u128_sub(u128_add(vk1, n), two);
        }
        else {
            U128 t = u128_mulmod(vk, vk1, n);
            if (u128_le(a_mod, t)) t = u128_sub(t, a_mod);
            else t = u128_sub(u128_add(t, n), a_mod);
            vk1 = t;
            vk = u128_mulmod(vk, vk, n);
            if (u128_le(two, vk)) vk = u128_sub(vk, two);
            else vk = u128_sub(u128_add(vk, n), two);
        }
        mask >>= 1u;
    }
    return vk;
}

/** Williams p+1 for U128 n. */
static FactorResultU128 cpss_factor_pplus1_u128(CPSSStream* s, U128 n, uint64_t B1) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "pplus1");
    double t0 = now_sec();

    if (u128_le(n, u128_from_u64(3u)) || (n.lo & 1u) == 0u) {
        if (u128_le(n, u128_from_u64(1u))) { fr.fully_factored = true; fr.cofactor = n; }
        else if ((n.lo & 1u) == 0u) {
            while (u128_mod_u64(fr.cofactor, 2u) == 0u) {
                factor_result_u128_add(&fr, 2u);
                fr.cofactor = u128_div(fr.cofactor, u128_from_u64(2u));
            }
            if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                factor_result_u128_add(&fr, fr.cofactor.lo); fr.cofactor = u128_from_u64(1u);
            }
            fr.fully_factored = u128_le(fr.cofactor, u128_from_u64(1u));
        }
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (B1 == 0u) { B1 = 1000000u; if (db_hi > 0u && db_hi < B1) B1 = db_hi; }
    if (db_hi > 0u && B1 > db_hi) { B1 = db_hi; fr.status = (int)CPSS_INCOMPLETE; }
    if (db_hi == 0u) { fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr; }

    static const uint64_t starts[] = { 3, 5, 7, 11, 13, 17, 19, 23 };
    U128 stage1_v = u128_from_u64(0u);
    for (size_t si = 0u; si < ARRAY_LEN(starts); ++si) {
        U128 v = u128_from_u64(starts[si]);
        PrimeIterator iter;
        cpss_iter_init(&iter, s, 2u);

        while (iter.valid && iter.current <= B1) {
            uint64_t p = iter.current;
            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            v = lucas_chain_u128(v, pk, n);

            U128 v_minus_2 = u128_le(u128_from_u64(2u), v) ? u128_sub(v, u128_from_u64(2u))
                : u128_sub(u128_add(v, n), u128_from_u64(2u));
            U128 g = u128_gcd(v_minus_2, n);

            if (u128_gt(g, u128_from_u64(1u)) && u128_lt(g, n)) {
                fr.method = "pplus1(stage1)";
                if (u128_fits_u64(g)) {
                    factor_result_u128_add(&fr, g.lo);
                    fr.cofactor = u128_div(n, g);
                    if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                        factor_result_u128_add(&fr, fr.cofactor.lo);
                        fr.cofactor = u128_from_u64(1u); fr.fully_factored = true;
                    }
                }
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (u128_eq(g, n)) break;
            cpss_iter_next(&iter);
        }
        stage1_v = v;
    }

    /* Stage 2: primes q in (B1, B2] via Lucas chain */
    if (!fr.fully_factored && !u128_is_zero(stage1_v)) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                U128 vq = lucas_chain_u128(stage1_v, q, n);
                U128 vq_m2 = u128_le(u128_from_u64(2u), vq) ? u128_sub(vq, u128_from_u64(2u))
                    : u128_sub(u128_add(vq, n), u128_from_u64(2u));
                U128 g2 = u128_gcd(vq_m2, n);
                if (u128_gt(g2, u128_from_u64(1u)) && u128_lt(g2, n)) {
                    fr.method = "pplus1(stage2)";
                    if (u128_fits_u64(g2)) {
                        factor_result_u128_add(&fr, g2.lo);
                        fr.cofactor = u128_div(n, g2);
                        if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                            factor_result_u128_add(&fr, fr.cofactor.lo);
                            fr.cofactor = u128_from_u64(1u); fr.fully_factored = true;
                        } else if (u128_is_prime(fr.cofactor)) { fr.fully_factored = true; }
                    }
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (u128_eq(g2, n)) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/** Lucas chain for BigNum modulus using Montgomery arithmetic. */
static void lucas_chain_bn(BigNum* result, const BigNum* a, uint64_t e,
    const MontCtx* ctx) {
    if (e == 0u) { bn_from_u64(result, 2u); return; }
    if (e == 1u) { bn_copy(result, a); return; }

    /* Convert a and 2 to Montgomery form */
    BigNum a_mont, two_mont;
    bn_to_mont(&a_mont, a, ctx);
    BigNum two_plain; bn_from_u64(&two_plain, 2u);
    bn_to_mont(&two_mont, &two_plain, ctx);

    BigNum vk_mont, vk1_mont;
    bn_copy(&vk_mont, &a_mont); /* V_1 in Montgomery form */
    /* V_2 = a^2 - 2 in Montgomery form */
    bn_mont_mul(&vk1_mont, &a_mont, &a_mont, ctx);
    /* Subtract 2 in Montgomery form */
    if (bn_cmp(&vk1_mont, &two_mont) >= 0)
        { bn_copy(&vk1_mont, &vk1_mont); BigNum tmp; bn_copy(&tmp, &vk1_mont); bn_sub(&tmp, &two_mont); bn_copy(&vk1_mont, &tmp); }
    else
        { BigNum tmp; bn_copy(&tmp, &vk1_mont); bn_add(&tmp, &ctx->n); bn_sub(&tmp, &two_mont); bn_copy(&vk1_mont, &tmp); }

    uint64_t mask = (uint64_t)1u << 62u;
    while (mask > 0u && !(e & mask)) mask >>= 1u;
    mask >>= 1u;

    while (mask > 0u) {
        if (e & mask) {
            /* vk = vk*vk1 - a (all in Montgomery form) */
            BigNum t;
            bn_mont_mul(&t, &vk_mont, &vk1_mont, ctx);
            if (bn_cmp(&t, &a_mont) >= 0) bn_sub(&t, &a_mont);
            else { bn_add(&t, &ctx->n); bn_sub(&t, &a_mont); }
            bn_copy(&vk_mont, &t);
            /* vk1 = vk1^2 - 2 */
            bn_mont_mul(&vk1_mont, &vk1_mont, &vk1_mont, ctx);
            if (bn_cmp(&vk1_mont, &two_mont) >= 0) bn_sub(&vk1_mont, &two_mont);
            else { bn_add(&vk1_mont, &ctx->n); bn_sub(&vk1_mont, &two_mont); }
        }
        else {
            /* vk1 = vk*vk1 - a */
            BigNum t;
            bn_mont_mul(&t, &vk_mont, &vk1_mont, ctx);
            if (bn_cmp(&t, &a_mont) >= 0) bn_sub(&t, &a_mont);
            else { bn_add(&t, &ctx->n); bn_sub(&t, &a_mont); }
            bn_copy(&vk1_mont, &t);
            /* vk = vk^2 - 2 */
            bn_mont_mul(&vk_mont, &vk_mont, &vk_mont, ctx);
            if (bn_cmp(&vk_mont, &two_mont) >= 0) bn_sub(&vk_mont, &two_mont);
            else { bn_add(&vk_mont, &ctx->n); bn_sub(&vk_mont, &two_mont); }
        }
        mask >>= 1u;
    }

    /* Convert back from Montgomery form */
    bn_from_mont(result, &vk_mont, ctx);
}

/** Williams p+1 for BigNum n using Montgomery Lucas chains. */
static FactorResultBigNum cpss_factor_pplus1_bn(CPSSStream* s, const BigNum* n, uint64_t B1) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "pplus1";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr;
    }

    /* Strip factors of 2 */
    BigNum n_odd; bn_copy(&n_odd, n);
    while (bn_is_even(&n_odd) && !bn_is_zero(&n_odd)) bn_shr1(&n_odd);
    if (bn_is_one(&n_odd)) {
        bn_from_u64(&fr.factor, 2u); bn_from_u64(&fr.cofactor, 1u);
        fr.factor_found = true; fr.factor_is_prime = true; fr.cofactor_is_prime = true;
        fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr;
    }

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (B1 == 0u) { B1 = 1000000u; if (db_hi > 0u && db_hi < B1) B1 = db_hi; }
    if (db_hi > 0u && B1 > db_hi) { B1 = db_hi; fr.status = (int)CPSS_INCOMPLETE; }
    if (db_hi == 0u) { fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr; }

    MontCtx ctx;
    if (!bn_montgomery_setup(&ctx, &n_odd)) {
        fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr;
    }

    static const uint64_t starts[] = { 3, 5, 7, 11, 13, 17, 19, 23 };
    BigNum stage1_v_bn; bn_zero(&stage1_v_bn);
    for (size_t si = 0u; si < ARRAY_LEN(starts); ++si) {
        BigNum v; bn_from_u64(&v, starts[si]);
        PrimeIterator iter;
        cpss_iter_init(&iter, s, 2u);

        while (iter.valid && iter.current <= B1) {
            uint64_t p = iter.current;
            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            lucas_chain_bn(&v, &v, pk, &ctx);

            /* gcd(v - 2, n_odd) */
            BigNum v_minus_2; bn_copy(&v_minus_2, &v);
            BigNum two_val; bn_from_u64(&two_val, 2u);
            if (bn_cmp(&v_minus_2, &two_val) >= 0) bn_sub(&v_minus_2, &two_val);
            else { bn_add(&v_minus_2, &n_odd); bn_sub(&v_minus_2, &two_val); }

            BigNum g;
            bn_gcd(&g, &v_minus_2, &n_odd);

            if (!bn_is_one(&g) && bn_cmp(&g, &n_odd) != 0) {
                fr.method = "pplus1(stage1)";
                bn_copy(&fr.factor, &g);
                bn_divexact(&fr.cofactor, &n_odd, &g);
                fr.factor_found = true;
                if (bn_fits_u64(&fr.factor)) fr.factor_is_prime = miller_rabin_u64(bn_to_u64(&fr.factor));
                else if (bn_fits_u128(&fr.factor)) fr.factor_is_prime = u128_is_prime(bn_to_u128(&fr.factor));
                if (bn_fits_u64(&fr.cofactor)) fr.cofactor_is_prime = miller_rabin_u64(bn_to_u64(&fr.cofactor));
                else if (bn_fits_u128(&fr.cofactor)) fr.cofactor_is_prime = u128_is_prime(bn_to_u128(&fr.cofactor));
                fr.fully_factored = (fr.factor_is_prime && fr.cofactor_is_prime);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (bn_cmp(&g, &n_odd) == 0) break;
            cpss_iter_next(&iter);
        }
        /* Save last v for Stage 2 (from the last starting value that ran) */
        bn_copy(&stage1_v_bn, &v);
    }

    /* Stage 2: primes q in (B1, B2] via Lucas chain */
    if (!fr.factor_found && !bn_is_zero(&stage1_v_bn)) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                BigNum vq;
                lucas_chain_bn(&vq, &stage1_v_bn, q, &ctx);
                BigNum vq_m2; bn_copy(&vq_m2, &vq);
                BigNum two_val2; bn_from_u64(&two_val2, 2u);
                if (bn_cmp(&vq_m2, &two_val2) >= 0) bn_sub(&vq_m2, &two_val2);
                else { bn_add(&vq_m2, &n_odd); bn_sub(&vq_m2, &two_val2); }
                BigNum g2; bn_gcd(&g2, &vq_m2, &n_odd);
                if (!bn_is_one(&g2) && bn_cmp(&g2, &n_odd) != 0) {
                    fr.method = "pplus1(stage2)";
                    bn_copy(&fr.factor, &g2);
                    bn_divexact(&fr.cofactor, &n_odd, &g2);
                    fr.factor_found = true;
                    if (bn_fits_u64(&fr.factor)) fr.factor_is_prime = miller_rabin_u64(bn_to_u64(&fr.factor));
                    else if (bn_fits_u128(&fr.factor)) fr.factor_is_prime = u128_is_prime(bn_to_u128(&fr.factor));
                    if (bn_fits_u64(&fr.cofactor)) fr.cofactor_is_prime = miller_rabin_u64(bn_to_u64(&fr.cofactor));
                    else if (bn_fits_u128(&fr.cofactor)) fr.cofactor_is_prime = u128_is_prime(bn_to_u128(&fr.cofactor));
                    fr.fully_factored = (fr.factor_is_prime && fr.cofactor_is_prime);
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (bn_cmp(&g2, &n_odd) == 0) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.factor_found && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/* ══════════════════════════════════════════════════════════════════════
 * FACTOR AUTO-ROUTER
 *
 * Classifies input, checks support coverage, and picks the best method.
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Auto-factor routing strategy:
 *   1. Classify (prime? small factor? perfect power?)
 *   2. Divide out wheel primes
 *   3. Check if cofactor is prime (Miller-Rabin)
 *   4. Try p-1 with cheap B1 first (fast, O(B1) — often solves instantly)
 *   5. Try p+1 as a complementary smoothness method
 *   6. Try Pollard rho before the full trial-division fallback
 *   7. Only fall back to trial division if the faster methods miss
 */
static FactorResult cpss_db_factor_trial(CPSSDatabase* db, uint64_t n, uint64_t limit) {
    FactorResult fr;
    factor_result_init(&fr, n, "trial");
    double t0 = now_sec();

    if (n <= 1u) {
        fr.cofactor = n;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        while (fr.cofactor % p == 0u) {
            factor_result_add(&fr, p);
            fr.cofactor /= p;
        }
    }

    if (fr.cofactor <= 1u) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (!db || db->shard_count == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    {
        uint64_t sqrt_cof = (uint64_t)isqrt_u64(fr.cofactor);
        if (sqrt_cof * sqrt_cof < fr.cofactor) ++sqrt_cof;
        uint64_t trial_limit = (limit > 0u && limit < sqrt_cof) ? limit : sqrt_cof;
        uint64_t effective_limit = trial_limit;
        uint64_t db_hi = cpss_db_contiguous_hi(db, false);

        if (db_hi > 0u && db_hi < effective_limit) {
            effective_limit = db_hi;
            if (effective_limit < sqrt_cof) fr.status = (int)CPSS_INCOMPLETE;
        }

        if (effective_limit > WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u]) {
            uint64_t current = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u];
            while (current < effective_limit) {
                CPSSQueryStatus qs = CPSS_OK;
                uint64_t p = cpss_db_next_prime_s(db, current, &qs);
                if (qs != CPSS_OK) {
                    if (qs != CPSS_OUT_OF_RANGE) fr.status = (int)qs;
                    break;
                }
                if (p > effective_limit) break;
                if (p <= UINT32_MAX && (uint64_t)p * p > fr.cofactor) break;

                while (fr.cofactor % p == 0u) {
                    factor_result_add(&fr, p);
                    fr.cofactor /= p;
                }

                if (fr.cofactor <= 1u) break;
                current = p;
            }
        }

        if (fr.cofactor <= 1u) {
            fr.fully_factored = true;
        }
        else {
            uint64_t sqrt_remaining = (uint64_t)isqrt_u64(fr.cofactor);
            if (sqrt_remaining * sqrt_remaining < fr.cofactor) ++sqrt_remaining;
            if (sqrt_remaining <= effective_limit) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
        }
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}

static FactorResult cpss_db_factor_pminus1(CPSSDatabase* db, uint64_t n, uint64_t B1) {
    FactorResult fr;
    factor_result_init(&fr, n, "pminus1");
    double t0 = now_sec();

    if (n <= 3u || (n & 1u) == 0u) {
        if (n <= 1u) {
            fr.fully_factored = true;
            fr.cofactor = n;
        }
        else if ((n & 1u) == 0u) {
            while (fr.cofactor % 2u == 0u) {
                factor_result_add(&fr, 2u);
                fr.cofactor /= 2u;
            }
            if (fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
            }
            fr.fully_factored = (fr.cofactor <= 1u);
        }
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (!db || db->shard_count == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    {
        uint64_t db_hi = cpss_db_contiguous_hi(db, false);
        if (B1 == 0u) {
            B1 = 1000000u;
            if (db_hi > 0u && db_hi < B1) B1 = db_hi;
        }
        if (db_hi > 0u && B1 > db_hi) {
            B1 = db_hi;
            fr.status = (int)CPSS_INCOMPLETE;
        }

        {
            uint64_t base = 2u;
            uint64_t current = 1u;
            while (current < B1) {
                CPSSQueryStatus qs = CPSS_OK;
                uint64_t p = cpss_db_next_prime_s(db, current, &qs);
                if (qs != CPSS_OK) {
                    if (qs != CPSS_OUT_OF_RANGE) fr.status = (int)qs;
                    break;
                }
                if (p > B1) break;

                uint64_t pk = p;
                while (pk <= B1 / p) pk *= p;
                base = powmod_u64(base, pk, n);

                {
                    uint64_t g = gcd_u64(base > 0u ? base - 1u : n - 1u, n);
                    if (g > 1u && g < n) {
                        factor_result_add(&fr, g);
                        fr.cofactor = n / g;
                        if (miller_rabin_u64(g) && miller_rabin_u64(fr.cofactor)) {
                            factor_result_add(&fr, fr.cofactor);
                            fr.cofactor = 1u;
                            fr.fully_factored = true;
                        }
                        else if (miller_rabin_u64(fr.cofactor)) {
                            factor_result_add(&fr, fr.cofactor);
                            fr.cofactor = 1u;
                            fr.fully_factored = true;
                        }
                        fr.time_seconds = now_sec() - t0;
                        return fr;
                    }
                    if (g == n) break;
                }

                current = p;
            }

            if (base > 1u) {
                uint64_t g = gcd_u64(base - 1u, n);
                if (g > 1u && g < n) {
                    factor_result_add(&fr, g);
                    fr.cofactor = n / g;
                    if (miller_rabin_u64(fr.cofactor)) {
                        factor_result_add(&fr, fr.cofactor);
                        fr.cofactor = 1u;
                        fr.fully_factored = true;
                    }
                }
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

static FactorResult cpss_db_factor_pplus1(CPSSDatabase* db, uint64_t n, uint64_t B1) {
    FactorResult fr;
    factor_result_init(&fr, n, "pplus1");
    double t0 = now_sec();

    if (n <= 3u || (n & 1u) == 0u) {
        if (n <= 1u) {
            fr.fully_factored = true;
            fr.cofactor = n;
        }
        else if ((n & 1u) == 0u) {
            while (fr.cofactor % 2u == 0u) {
                factor_result_add(&fr, 2u);
                fr.cofactor /= 2u;
            }
            if (fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
            }
            fr.fully_factored = (fr.cofactor <= 1u);
        }
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (!db || db->shard_count == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    {
        uint64_t db_hi = cpss_db_contiguous_hi(db, false);
        if (B1 == 0u) {
            B1 = 1000000u;
            if (db_hi > 0u && db_hi < B1) B1 = db_hi;
        }
        if (db_hi > 0u && B1 > db_hi) {
            B1 = db_hi;
            fr.status = (int)CPSS_INCOMPLETE;
        }

        {
            static const uint64_t starts[] = { 3, 5, 7, 11, 13, 17, 19, 23 };
            for (size_t si = 0u; si < ARRAY_LEN(starts); ++si) {
                uint64_t v = starts[si] % n;
                uint64_t current = 1u;

                while (current < B1) {
                    CPSSQueryStatus qs = CPSS_OK;
                    uint64_t p = cpss_db_next_prime_s(db, current, &qs);
                    if (qs != CPSS_OK) {
                        if (qs != CPSS_OUT_OF_RANGE) fr.status = (int)qs;
                        break;
                    }
                    if (p > B1) break;

                    {
                        uint64_t pk = p;
                        while (pk <= B1 / p) pk *= p;
                        v = lucas_chain_u64(v, pk, n);
                    }

                    {
                        uint64_t g = gcd_u64((v >= 2u) ? v - 2u : v + n - 2u, n);
                        if (g > 1u && g < n) {
                            factor_result_add(&fr, g);
                            fr.cofactor = n / g;
                            if (miller_rabin_u64(g) && miller_rabin_u64(fr.cofactor)) {
                                factor_result_add(&fr, fr.cofactor);
                                fr.cofactor = 1u;
                                fr.fully_factored = true;
                            }
                            else if (miller_rabin_u64(fr.cofactor)) {
                                factor_result_add(&fr, fr.cofactor);
                                fr.cofactor = 1u;
                                fr.fully_factored = true;
                            }
                            fr.time_seconds = now_sec() - t0;
                            return fr;
                        }
                        if (g == n) break;
                    }

                    current = p;
                }
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

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

    /* Step 1: Try p-1 first — it's cheap (O(B1)) and often works.
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

    /* Step 2: Try p+1 — complements p-1, finds factors where p+1 is smooth */
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

    /* Step 4: SQUFOF — deterministic, good for hard uint64 semiprimes */
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

    /* Step 5: QS — basic quadratic sieve for remaining hard composites */
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

    /* Step 6: trial division — last resort */
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

/* ══════════════════════════════════════════════════════════════════════
 * SINGLE-PURPOSE FACTOR QUERIES
 * ══════════════════════════════════════════════════════════════════════ */

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

/* ══════════════════════════════════════════════════════════════════════
 * U128 FACTOR ENGINES
 *
 * Handle 128-bit input numbers. Primes from the DB are uint64.
 * Trial division: cofactor_128 % prime_64.
 * Pollard p-1: powmod(base, pk, n_128).
 * ══════════════════════════════════════════════════════════════════════ */

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

static void factor_result_u128_init(FactorResultU128* fr, U128 n, const char* method) {
    memset(fr, 0, sizeof(*fr));
    fr->n = n;
    fr->cofactor = n;
    fr->method = method;
    fr->status = (int)CPSS_OK;
}

/** Trial division for U128 n using CPSS-streamed uint64 primes. */
static FactorResultU128 cpss_factor_trial_u128(CPSSStream* s, U128 n, uint64_t limit) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "trial");
    double t0 = now_sec();

    if (u128_le(n, u128_from_u64(1u))) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Divide by wheel primes first */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        while (u128_mod_u64(fr.cofactor, p) == 0u) {
            factor_result_u128_add(&fr, p);
            fr.cofactor = u128_div(fr.cofactor, u128_from_u64(p));
        }
    }

    if (u128_le(fr.cofactor, u128_from_u64(1u))) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* sqrt(cofactor) — if cofactor fits in 64 bits, use isqrt_u64 */
    uint64_t sqrt_cof = u128_isqrt_ceil(fr.cofactor);

    uint64_t trial_limit = (limit > 0u && limit < sqrt_cof) ? limit : sqrt_cof;

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);

    uint64_t effective_limit = trial_limit;
    if (db_hi > 0u && db_hi < effective_limit) {
        effective_limit = db_hi;
        if (effective_limit < sqrt_cof) fr.status = (int)CPSS_INCOMPLETE;
    }
    else if (db_hi == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    PrimeIterator iter;
    cpss_iter_init(&iter, s, WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u);

    while (iter.valid && iter.current <= effective_limit) {
        uint64_t p = iter.current;
        if (u128_fits_u64(fr.cofactor) && p * p > fr.cofactor.lo) break;
        if (!u128_fits_u64(fr.cofactor) && u128_gt(u128_mul64(p, p), fr.cofactor)) break;

        while (u128_mod_u64(fr.cofactor, p) == 0u) {
            factor_result_u128_add(&fr, p);
            fr.cofactor = u128_div(fr.cofactor, u128_from_u64(p));
        }
        if (u128_le(fr.cofactor, u128_from_u64(1u))) break;
        cpss_iter_next(&iter);
    }

    if (u128_le(fr.cofactor, u128_from_u64(1u))) {
        fr.fully_factored = true;
    }
    else {
        /* If cofactor fits in uint64 and is within DB range, check if it's prime */
        if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
            factor_result_u128_add(&fr, fr.cofactor.lo);
            fr.cofactor = u128_from_u64(1u);
            fr.fully_factored = true;
        }
        else if (!u128_fits_u64(fr.cofactor) && u128_is_prime(fr.cofactor)) {
            /* Cofactor is a large prime — can't store as uint64 factor, report in cofactor */
            fr.fully_factored = true; /* it IS a prime, just too large for the factors array */
        }
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/** Pollard p-1 for U128 n using CPSS primes. */
static FactorResultU128 cpss_factor_pminus1_u128(CPSSStream* s, U128 n, uint64_t B1) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "pminus1");
    double t0 = now_sec();

    if (u128_le(n, u128_from_u64(3u)) || (n.lo & 1u) == 0u) {
        if (u128_le(n, u128_from_u64(1u))) { fr.fully_factored = true; fr.cofactor = n; }
        else if ((n.lo & 1u) == 0u) {
            while (u128_mod_u64(fr.cofactor, 2u) == 0u) {
                factor_result_u128_add(&fr, 2u);
                fr.cofactor = u128_div(fr.cofactor, u128_from_u64(2u));
            }
            if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                factor_result_u128_add(&fr, fr.cofactor.lo);
                fr.cofactor = u128_from_u64(1u);
            }
            fr.fully_factored = u128_le(fr.cofactor, u128_from_u64(1u));
        }
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (B1 == 0u) { B1 = 1000000u; if (db_hi > 0u && db_hi < B1) B1 = db_hi; }
    if (db_hi > 0u && B1 > db_hi) { B1 = db_hi; fr.status = (int)CPSS_INCOMPLETE; }
    if (db_hi == 0u) { fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr; }

    /* Stage 1: compute a^M mod n */
    U128 base = u128_from_u64(2u);
    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);

    while (iter.valid && iter.current <= B1) {
        uint64_t p = iter.current;
        uint64_t pk = p;
        while (pk <= B1 / p) pk *= p;

        base = u128_powmod(base, u128_from_u64(pk), n);

        U128 base_minus_1 = u128_sub(base, u128_from_u64(1u));
        if (u128_is_zero(base_minus_1)) base_minus_1 = u128_sub(n, u128_from_u64(1u));
        U128 g = u128_gcd(base_minus_1, n);

        if (u128_gt(g, u128_from_u64(1u)) && u128_lt(g, n)) {
            fr.method = "pminus1(stage1)";
            if (u128_fits_u64(g)) {
                factor_result_u128_add(&fr, g.lo);
                fr.cofactor = u128_div(n, g);
                if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                    factor_result_u128_add(&fr, fr.cofactor.lo);
                    fr.cofactor = u128_from_u64(1u);
                    fr.fully_factored = true;
                }
                else if (!u128_fits_u64(fr.cofactor) && u128_is_prime(fr.cofactor)) {
                    fr.fully_factored = true;
                }
            }
            fr.time_seconds = now_sec() - t0;
            return fr;
        }
        if (u128_eq(g, n)) break;
        cpss_iter_next(&iter);
    }

    /* Stage 2: primes q in (B1, B2] */
    if (!fr.fully_factored && !u128_is_zero(base)) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                U128 bq = u128_powmod(base, u128_from_u64(q), n);
                U128 bq_m1 = u128_is_zero(bq) ? u128_sub(n, u128_from_u64(1u)) : u128_sub(bq, u128_from_u64(1u));
                U128 g2 = u128_gcd(bq_m1, n);
                if (u128_gt(g2, u128_from_u64(1u)) && u128_lt(g2, n)) {
                    fr.method = "pminus1(stage2)";
                    if (u128_fits_u64(g2)) {
                        factor_result_u128_add(&fr, g2.lo);
                        fr.cofactor = u128_div(n, g2);
                        if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                            factor_result_u128_add(&fr, fr.cofactor.lo);
                            fr.cofactor = u128_from_u64(1u); fr.fully_factored = true;
                        } else if (u128_is_prime(fr.cofactor)) { fr.fully_factored = true; }
                    }
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (u128_eq(g2, n)) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK)
        fr.status = (int)CPSS_INCOMPLETE;

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Auto-factor U128 n. Strategy: small factors → primality → p-1 → trial.
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

    /* Step 1: Try p-1 first — cheap O(B1), often solves instantly */
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

    /* Step 2: Try p+1 — complements p-1, finds factors where p+1 is smooth */
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

    /* Step 3: native U128 rho — general-purpose for hard composites */
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

    /* Step 4: trial division — last resort */
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

/* ══════════════════════════════════════════════════════════════════════
 * BIGNUM (1024-bit) POLLARD p-1 ENGINE
 * ══════════════════════════════════════════════════════════════════════ */

/** Pollard p-1 for BigNum n using CPSS primes and Montgomery arithmetic. */
static FactorResultBigNum cpss_factor_pminus1_bn(CPSSStream* s, const BigNum* n, uint64_t B1) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "pminus1";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Strip factors of 2 (Montgomery needs odd modulus) */
    BigNum n_odd;
    bn_copy(&n_odd, n);
    while (bn_is_even(&n_odd) && !bn_is_zero(&n_odd)) {
        bn_shr1(&n_odd);
    }

    if (bn_is_one(&n_odd)) {
        bn_from_u64(&fr.factor, 2u);
        bn_from_u64(&fr.cofactor, 1u);
        fr.factor_found = true;
        fr.factor_is_prime = true;
        fr.cofactor_is_prime = true;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Set up DB access */
    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (B1 == 0u) { B1 = 1000000u; if (db_hi > 0u && db_hi < B1) B1 = db_hi; }
    if (db_hi > 0u && B1 > db_hi) { B1 = db_hi; fr.status = (int)CPSS_INCOMPLETE; }
    if (db_hi == 0u) { fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr; }

    /* Montgomery setup for the odd part */
    MontCtx ctx;
    if (!bn_montgomery_setup(&ctx, &n_odd)) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Stage 1: compute base^M mod n_odd where M = lcm(1..B1) */
    BigNum base;
    bn_from_u64(&base, 2u);

    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);

    while (iter.valid && iter.current <= B1) {
        uint64_t p = iter.current;
        uint64_t pk = p;
        while (pk <= B1 / p) pk *= p;

        bn_mont_powmod(&base, &base, pk, &ctx);

        /* gcd(base - 1, n_odd) */
        if (!bn_is_zero(&base) && !bn_is_one(&base)) {
            BigNum base_minus_1;
            bn_copy(&base_minus_1, &base);
            bn_sub_u64(&base_minus_1, 1u);
            BigNum g;
            bn_gcd(&g, &base_minus_1, &n_odd);

            if (!bn_is_one(&g) && bn_cmp(&g, &n_odd) != 0) {
                fr.method = "pminus1(stage1)";
                bn_copy(&fr.factor, &g);
                bn_divexact(&fr.cofactor, &n_odd, &g);
                fr.factor_found = true;
                if (bn_fits_u64(&fr.factor))
                    fr.factor_is_prime = miller_rabin_u64(bn_to_u64(&fr.factor));
                else if (bn_fits_u128(&fr.factor))
                    fr.factor_is_prime = u128_is_prime(bn_to_u128(&fr.factor));
                if (bn_fits_u64(&fr.cofactor))
                    fr.cofactor_is_prime = miller_rabin_u64(bn_to_u64(&fr.cofactor));
                else if (bn_fits_u128(&fr.cofactor))
                    fr.cofactor_is_prime = u128_is_prime(bn_to_u128(&fr.cofactor));
                fr.fully_factored = (fr.factor_is_prime && fr.cofactor_is_prime);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (bn_cmp(&g, &n_odd) == 0) break;
        }

        cpss_iter_next(&iter);
    }

    /* Stage 2: primes q in (B1, B2] */
    if (!fr.factor_found && !bn_is_zero(&base) && !bn_is_one(&base)) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                bn_mont_powmod(&base, &base, q, &ctx);
                if (!bn_is_zero(&base) && !bn_is_one(&base)) {
                    BigNum bm1; bn_copy(&bm1, &base); bn_sub_u64(&bm1, 1u);
                    BigNum g2; bn_gcd(&g2, &bm1, &n_odd);
                    if (!bn_is_one(&g2) && bn_cmp(&g2, &n_odd) != 0) {
                        fr.method = "pminus1(stage2)";
                        bn_copy(&fr.factor, &g2);
                        bn_divexact(&fr.cofactor, &n_odd, &g2);
                        fr.factor_found = true;
                        if (bn_fits_u64(&fr.factor))
                            fr.factor_is_prime = miller_rabin_u64(bn_to_u64(&fr.factor));
                        else if (bn_fits_u128(&fr.factor))
                            fr.factor_is_prime = u128_is_prime(bn_to_u128(&fr.factor));
                        if (bn_fits_u64(&fr.cofactor))
                            fr.cofactor_is_prime = miller_rabin_u64(bn_to_u64(&fr.cofactor));
                        else if (bn_fits_u128(&fr.cofactor))
                            fr.cofactor_is_prime = u128_is_prime(bn_to_u128(&fr.cofactor));
                        fr.fully_factored = (fr.factor_is_prime && fr.cofactor_is_prime);
                        fr.time_seconds = now_sec() - t0;
                        return fr;
                    }
                    if (bn_cmp(&g2, &n_odd) == 0) break;
                }
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.factor_found && fr.status == (int)CPSS_OK)
        fr.status = (int)CPSS_INCOMPLETE;

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/* factor_result_bn_print defined earlier in this file (Phase 13a) */
