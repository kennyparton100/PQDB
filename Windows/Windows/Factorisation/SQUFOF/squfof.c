/**
 * cpss_factor_squfof.c - Shanks' Square Forms Factorization (SQUFOF).
 * Part of the CPSS Viewer amalgamation.
 *
 * Deterministic factoring method effective for hard uint64 semiprimes
 * in the 20-62 bit range where Pollard rho may struggle.
 * No DB needed. Pure arithmetic.
 *
 * Algorithm: continued fraction expansion of sqrt(k*N) to find a
 * proper square form, then reverse to extract a factor.
 *
 * Depends on: cpss_arith.c (isqrt_u64, gcd_u64, miller_rabin_u64)
 */

/* Small multipliers to try — improves success rate on hard inputs */
static const uint64_t SQUFOF_MULTIPLIERS[] = {
    1, 3, 5, 7, 11, 3*5, 3*7, 3*11, 5*7, 5*11, 7*11,
    3*5*7, 3*5*11, 3*7*11, 5*7*11, 3*5*7*11
};
#define SQUFOF_MULT_COUNT (sizeof(SQUFOF_MULTIPLIERS) / sizeof(SQUFOF_MULTIPLIERS[0]))

/**
 * Core SQUFOF for a single multiplier k*N.
 * Returns a non-trivial factor of N, or 0 on failure.
 */
static uint64_t squfof_inner(uint64_t N, uint64_t kN, uint64_t max_iter) {
    /* sqrt(kN) */
    uint64_t s = (uint64_t)isqrt_u64(kN);
    if (s * s == kN) {
        /* kN is a perfect square — check gcd(s, N) */
        uint64_t g = gcd_u64(s, N);
        if (g > 1u && g < N) return g;
        return 0u;
    }

    /* Forward cycle: infrastructure of the continued fraction of sqrt(kN) */
    uint64_t Qprev = 1u;
    uint64_t Q = kN - s * s;
    if (Q == 0u) return 0u;
    uint64_t P = s;

    /* Iteration limit based on fourth root of kN, capped */
    uint64_t limit = max_iter;
    if (limit == 0u) {
        /* ~2 * kN^(1/4) is a reasonable bound */
        uint64_t fourth = (uint64_t)isqrt_u64((uint64_t)isqrt_u64(kN));
        limit = fourth * 4u;
        if (limit < 256u) limit = 256u;
        if (limit > 1000000u) limit = 1000000u;
    }

    /* Forward phase: look for a proper square form Q_i */
    uint64_t i;
    for (i = 1u; i <= limit; ++i) {
        uint64_t b = (s + P) / Q;
        uint64_t Pnext = b * Q - P;
        uint64_t Qnext = Qprev + b * (P - Pnext);

        /* Check if Q is a perfect square (only at even steps) */
        if ((i & 1u) == 0u) {
            uint64_t sq = (uint64_t)isqrt_u64(Q);
            if (sq * sq == Q && sq > 0u) {
                /* Found proper square form — start reverse cycle */
                uint64_t sqQ = sq;
                uint64_t b0 = (s - P) / sqQ;
                uint64_t P0 = b0 * sqQ + P;
                uint64_t Q0prev = sqQ;
                uint64_t Q0 = (kN - P0 * P0) / sqQ;
                if (Q0 == 0u) goto next_step;

                /* Reverse cycle until P stabilizes */
                for (uint64_t j = 0u; j < limit; ++j) {
                    uint64_t br = (s + P0) / Q0;
                    uint64_t P0next = br * Q0 - P0;
                    uint64_t Q0next = Q0prev + br * (P0 - P0next);

                    if (P0 == P0next) {
                        /* Extract factor */
                        uint64_t g = gcd_u64(N, P0);
                        if (g > 1u && g < N) return g;
                        g = gcd_u64(N, Q0);
                        if (g > 1u && g < N) return g;
                        break;
                    }

                    Q0prev = Q0;
                    Q0 = Q0next;
                    P0 = P0next;
                }
            }
        }

next_step:
        Qprev = Q;
        Q = Qnext;
        P = Pnext;

        if (Q == 0u) break;
    }

    return 0u;
}

/**
 * SQUFOF factorisation for uint64 N.
 * Tries multiple multipliers k to find a factor.
 * Returns FactorResult.
 */
static FactorResult cpss_factor_squfof(uint64_t n) {
    FactorResult fr;
    factor_result_init(&fr, n, "squfof");
    double t0 = now_sec();

    if (n <= 1u) {
        fr.cofactor = n;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    /* Strip factors of 2 */
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
    if (is_perfect_square_u64(fr.cofactor)) {
        uint64_t s = (uint64_t)isqrt_u64(fr.cofactor);
        factor_result_add(&fr, s);
        factor_result_add(&fr, s);
        fr.cofactor = 1u;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    uint64_t target = fr.cofactor;

    /* Try each multiplier */
    for (size_t mi = 0u; mi < SQUFOF_MULT_COUNT; ++mi) {
        uint64_t k = SQUFOF_MULTIPLIERS[mi];
        /* Check for overflow: k * target must fit in uint64 */
        if (k > 0u && target > UINT64_MAX / k) continue;
        uint64_t kN = k * target;
        if (kN <= 1u) continue;

        uint64_t divisor = squfof_inner(target, kN, 0u);
        if (divisor > 1u && divisor < target) {
            uint64_t g = gcd_u64(divisor, target);
            if (g > 1u && g < target) {
                uint64_t left = g, right = target / g;
                /* Recursively factor left half */
                if (left <= 1u) { /* nothing */ }
                else if (miller_rabin_u64(left)) { factor_result_add(&fr, left); }
                else {
                    FactorResult lr = cpss_factor_squfof(left);
                    for (size_t i = 0u; i < lr.factor_count; ++i)
                        for (uint64_t e = 0u; e < lr.exponents[i]; ++e)
                            factor_result_add(&fr, lr.factors[i]);
                }
                /* Recursively factor right half */
                if (right <= 1u) { /* nothing */ }
                else if (miller_rabin_u64(right)) { factor_result_add(&fr, right); }
                else {
                    FactorResult rr = cpss_factor_squfof(right);
                    for (size_t i = 0u; i < rr.factor_count; ++i)
                        for (uint64_t e = 0u; e < rr.exponents[i]; ++e)
                            factor_result_add(&fr, rr.factors[i]);
                }
                /* Recompute cofactor from found factors */
                uint64_t product = 1u;
                for (size_t i = 0u; i < fr.factor_count; ++i)
                    for (uint64_t e = 0u; e < fr.exponents[i]; ++e)
                        product *= fr.factors[i];
                fr.cofactor = (product > 0u) ? fr.n / product : fr.n;
                fr.fully_factored = (fr.cofactor <= 1u);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
        }
    }

    /* SQUFOF failed */
    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}
