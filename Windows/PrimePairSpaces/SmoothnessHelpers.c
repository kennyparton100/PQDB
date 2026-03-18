/**
 * SmoothnessHelpers.c - Exact B-smoothness arithmetic for known primes.
 * Part of the CPSS Prime-Pair Spaces module (exact factor-pair support).
 *
 * Low-level smoothness computation: given a known prime p, compute
 * B-smoothness scores for p-1 or p+1. These are exact arithmetic
 * operations on known values, not inference.
 *
 * N-centric routing signals (smoothness_pminus1_friendly, classify,
 * route_hint) live in SemiPrimeSpaces/SemiPrimeSmoothnessSpace.c.
 *
 * Depends on: cpss_iter.c (PrimeIterator), cpss_arith.c (miller_rabin_u64)
 */

/** Smoothness classification. */
typedef enum {
    SMOOTH_HIGH,     /* p-1 very smooth: Pollard p-1 happy zone */
    SMOOTH_MEDIUM,   /* maybe worth trying */
    SMOOTH_LOW       /* p-1 probably waste of time */
} SmoothnessClass;

/**
 * Compute a B-smoothness score for n: the number of distinct prime
 * factors of n that are <= B, divided by the total number of distinct
 * prime factors. Returns a value in [0.0, 1.0].
 *
 * Uses CPSS iterator to stream primes for trial division.
 * Fully factors n by primes up to B, then checks remainder.
 */
static double smoothness_score_u64(CPSSStream* s, uint64_t n, uint64_t B) {
    if (n <= 1u) return 1.0;
    uint64_t remaining = n;
    uint32_t smooth_factors = 0u;
    uint32_t total_factors = 0u;

    /* Trial divide by primes up to B */
    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);
    while (iter.valid && iter.current <= B) {
        uint64_t p = iter.current;
        if (p * p > remaining) break;
        if (remaining % p == 0u) {
            ++smooth_factors;
            ++total_factors;
            while (remaining % p == 0u) remaining /= p;
        }
        cpss_iter_next(&iter);
    }

    /* Remainder > 1: it's a prime factor. Check if it's within the smooth bound. */
    if (remaining > 1u) {
        ++total_factors;
        if (remaining <= B) ++smooth_factors;
    }

    if (total_factors == 0u) return 1.0;
    return (double)smooth_factors / (double)total_factors;
}

/**
 * Find the largest prime factor of n using trial division via CPSS iterator.
 * Returns {largest_factor, is_all_below_B1} for B1-smoothness checking.
 */
typedef struct { uint64_t largest; bool all_below_B1; } LargestFactorResult;

static LargestFactorResult largest_prime_factor_of(CPSSStream* s, uint64_t n, uint64_t B1) {
    LargestFactorResult r = { 1u, true };
    if (n <= 1u) return r;
    uint64_t remaining = n;

    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);
    while (iter.valid) {
        uint64_t q = iter.current;
        if (q * q > remaining) break;
        if (remaining % q == 0u) {
            r.largest = q;
            while (remaining % q == 0u) remaining /= q;
        }
        cpss_iter_next(&iter);
    }
    if (remaining > 1u) r.largest = remaining;
    r.all_below_B1 = (r.largest <= B1);
    return r;
}
