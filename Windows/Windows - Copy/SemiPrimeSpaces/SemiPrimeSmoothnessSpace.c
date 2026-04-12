/**
 * SemiPrimeSmoothnessSpace.c - N-centric smoothness routing signals.
 * Part of the CPSS SemiPrime Spaces module (N-centric inference).
 *
 * Uses exact smoothness helpers (SmoothnessHelpers.c) to produce
 * routing signals for semiprime analysis. These functions assess
 * how friendly a known prime factor's p-1 or p+1 is for specific
 * factorisation methods, and suggest routing decisions.
 *
 * Exact B-smoothness arithmetic lives in
 * PrimePairSpaces/SmoothnessHelpers.c.
 *
 * Depends on: PrimePairSpaces/SmoothnessHelpers.c (SmoothnessClass,
 *             smoothness_score_u64, LargestFactorResult, largest_prime_factor_of)
 */

/**
 * Assess p-1 friendliness for a known prime factor p.
 * Returns the ratio of the largest prime factor of (p-1) to p.
 * Low ratio => p-1 is smooth => Pollard p-1 will find p easily.
 * High ratio => p-1 has a large prime factor => p-1 method struggles.
 *
 * Uses trial division to find the largest prime factor of (p-1).
 */
static double smoothness_pminus1_friendly(CPSSStream* s, uint64_t p) {
    if (p <= 2u) return 1.0;
    uint64_t pm1 = p - 1u;
    uint64_t remaining = pm1;
    uint64_t largest_factor = 1u;

    /* Divide out small primes using iterator */
    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);
    while (iter.valid) {
        uint64_t q = iter.current;
        if (q * q > remaining) break;
        if (remaining % q == 0u) {
            largest_factor = q;
            while (remaining % q == 0u) remaining /= q;
        }
        cpss_iter_next(&iter);
    }
    if (remaining > 1u) largest_factor = remaining;

    /* Return ratio: larger = worse for p-1 */
    return (double)largest_factor / (double)p;
}

/**
 * Classify smoothness level.
 * score < 0.01 => HIGH  (largest factor of p-1 is tiny relative to p)
 * score < 0.1  => MEDIUM
 * score >= 0.1 => LOW
 */
static SmoothnessClass smoothness_classify(double score) {
    if (score < 0.01) return SMOOTH_HIGH;
    if (score < 0.1)  return SMOOTH_MEDIUM;
    return SMOOTH_LOW;
}

/**
 * Assess p+1 friendliness for a known prime factor p.
 * Same logic as smoothness_pminus1_friendly but for (p+1).
 * Returns ratio of largest prime factor of (p+1) to p.
 */
static double smoothness_pplus1_friendly(CPSSStream* s, uint64_t p) {
    if (p <= 1u) return 1.0;
    uint64_t pp1 = p + 1u;
    LargestFactorResult r = largest_prime_factor_of(s, pp1, UINT64_MAX);
    return (double)r.largest / (double)p;
}

/**
 * Suggest a smoothness-based method.
 * Returns: 1 = try p-1,  2 = try p+1,  3 = try both,  0 = skip.
 *
 * pm1_score and pp1_score are from smoothness_pminus1_friendly (or
 * analogous p+1 computation). Lower scores are better.
 */
static int smoothness_route_hint(double pm1_score, double pp1_score) {
    bool pm1_good = (pm1_score < 0.05);
    bool pp1_good = (pp1_score < 0.05);
    if (pm1_good && pp1_good) return 3;
    if (pm1_good)             return 1;
    if (pp1_good)             return 2;
    return 0;
}
