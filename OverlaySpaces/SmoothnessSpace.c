/**
 * SmoothnessSpace.c - Smoothness-property space functions.
 * Part of the CPSS Overlay Spaces module.
 *
 * Not geometric in the usual size sense, but critical for p-1, p+1,
 * ECM routing. Two semiprimes can sit in similar size regions but
 * behave totally differently under p-1 methods.
 *
 * Axes: x = factor size, y = smoothness-friendliness of p-1 (or p+1).
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
