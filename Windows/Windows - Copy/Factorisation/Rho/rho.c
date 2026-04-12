/* ══════════════════════════════════════════════════════════════════════
 * POLLARD RHO FACTORISATION
 *
 * General-purpose split finder for odd composite inputs that do not fall
 * quickly to wheel trial division or smoothness methods. The uint64 and
 * U128 paths recurse until the remaining cofactor is prime; the BigNum path
 * records a split and relies on higher-level recursive printing/routing to
 * continue if needed.
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * One Pollard rho iteration for f(x) = x^2 + c mod n.
 * Used by the tortoise-hare cycle finder in the uint64 engine.
 */
static uint64_t rho_step_u64(uint64_t x, uint64_t c, uint64_t n) {
    uint64_t sq = mulmod_u64(x, x, n);
    c %= n;
    return (sq >= n - c) ? (sq - (n - c)) : (sq + c);
}

/**
 * Try several randomized rho walks and return the first non-trivial divisor.
 * Returns 0 when all attempts exhaust the iteration budget without a split.
 */
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

/**
 * Recursively split a uint64 composite until only prime leaves remain.
 * Wheel factors are stripped before rho is invoked so cheap cases exit early.
 */
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

/**
 * Public uint64 Pollard rho entry point.
 * Normalizes defaults, dispatches recursive splitting, and finalizes status.
 */
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

/**
 * BigNum rho step for the same polynomial map f(x) = x^2 + c mod n.
 * Uses the local BigNum modular helpers retained in the root factor file.
 */
static void rho_step_bn(BigNum* out, const BigNum* x, const BigNum* c, const BigNum* n) {
    BigNum sq;
    bn_mod_square(&sq, x, n);
    bn_mod_add(out, &sq, c, n);
}

/**
 * BigNum rho splitter.
 * Finds one split and opportunistically probes the remaining cofactor to mark
 * whether the overall decomposition is likely complete.
 */
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

    factor_result_bn_set_from_factor(&fr, n, &split_factor);

    if (!fr.fully_factored && fr.factor_found && !fr.cofactor_is_prime) {
        BigNum cof;
        bn_copy(&cof, &fr.cofactor);

        for (unsigned depth = 0u; depth < 4u; ++depth) {
            if (bn_is_one(&cof) || bn_factor_is_prime(&cof)) {
                fr.fully_factored = true;
                break;
            }
            if (bn_fits_u64(&cof)) {
                uint64_t c64 = bn_to_u64(&cof);
                FactorResult cr = cpss_factor_rho(c64, 0u, 0u);
                fr.fully_factored = cr.fully_factored;
                break;
            }
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

/**
 * One rho step in the native U128 arithmetic path.
 * This mirrors the uint64 polynomial while keeping all reductions in U128.
 */
static U128 rho_step_u128(U128 x, U128 c, U128 n) {
    U128 sq = u128_mulmod(x, x, n);
    return u128_mod(u128_add(sq, c), n);
}

/**
 * Randomized divisor search for U128 inputs.
 * Returns zero when no divisor is found within the allocated work budget.
 */
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

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        while (u128_mod_u64(n, p) == 0u) {
            factor_result_u128_add(fr, p);
            n = u128_div(n, u128_from_u64(p));
            fr->cofactor = n;
        }
    }

    if (u128_le(n, u128_from_u64(1u))) return true;

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

    if (u128_is_prime(n)) {
        fr->cofactor = n;
        return true;
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

/**
 * Public U128 Pollard rho entry point.
 * Delegates to the recursive U128 splitter and promotes any final prime
 * cofactor into the compact uint64 factor list when it fits.
 */
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

    if (!u128_le(fr.cofactor, u128_from_u64(1u))) {
        if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
            factor_result_u128_add(&fr, fr.cofactor.lo);
            fr.cofactor = u128_from_u64(1u);
        } else if (u128_is_prime(fr.cofactor)) {
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
