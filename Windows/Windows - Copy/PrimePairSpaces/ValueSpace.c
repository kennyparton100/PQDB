/**
 * ValueSpace.c - Value space (p, q) functions.
 * Part of the CPSS Hidden Factor Spaces module.
 *
 * Axes = actual prime values. This is the honest geometry.
 * Near diagonal => balanced semiprimes; far from diagonal => skewed.
 *
 * Inputs are generic factor pairs (not necessarily prime). The caller
 * is responsible for ensuring primality if that matters for their use case.
 *
 * Depends on: cpss_arith.c (U128, u128_mul64), <math.h> (log, fabs)
 */

/** Region classification for a (p, q) factor pair. */
typedef enum {
    VS_NEAR_SQUARE,   /* factors within ~1.4x ratio */
    VS_MODERATE,      /* factors within ~10x ratio */
    VS_SKEWED         /* factors more than 10x apart */
} ValueSpaceRegion;

/**
 * Region thresholds expressed as min(p,q)/max(p,q) balance ratios.
 * NEAR_SQUARE:  ratio >= VS_THRESHOLD_NEAR_SQUARE  (~1.4x)
 * MODERATE:     ratio >= VS_THRESHOLD_MODERATE      (~10x)
 * SKEWED:       ratio <  VS_THRESHOLD_MODERATE
 */
#define VS_THRESHOLD_NEAR_SQUARE 0.7   /* min/max >= 0.7 => within ~1.4x */
#define VS_THRESHOLD_MODERATE    0.1   /* min/max >= 0.1 => within ~10x  */

/** Compute N = p * q as U128 (never overflows for uint64 inputs). */
static U128 value_space_product_u128(uint64_t p, uint64_t q) {
    return u128_mul64(p, q);
}

/**
 * Additive distance from the diagonal: |p - q|.
 * Less meaningful than log-imbalance for large factors, but cheap
 * and useful as a secondary signal.
 */
static uint64_t value_space_additive_distance(uint64_t p, uint64_t q) {
    return (p >= q) ? (p - q) : (q - p);
}

/**
 * Multiplicative imbalance: |log(p) - log(q)| = log(max/min).
 * This is the geometrically meaningful distance in value space.
 * 0.0 = balanced (p == q). Larger values = more skewed.
 * Returns 0.0 if either factor is 0.
 */
static double value_space_log_imbalance(uint64_t p, uint64_t q) {
    if (p == 0u || q == 0u) return 0.0;
    return fabs(log((double)p) - log((double)q));
}

/**
 * Balance ratio: min(p,q) / max(p,q).
 * Returns a value in (0.0, 1.0]. 1.0 = perfectly balanced (p == q).
 */
static double value_space_balance_ratio(uint64_t p, uint64_t q) {
    if (p == 0u || q == 0u) return 0.0;
    uint64_t lo = (p < q) ? p : q;
    uint64_t hi = (p < q) ? q : p;
    return (double)lo / (double)hi;
}

/**
 * Classify the factor pair into a region based on their balance ratio.
 * Uses named threshold constants defined above.
 */
static ValueSpaceRegion value_space_region(uint64_t p, uint64_t q) {
    double ratio = value_space_balance_ratio(p, q);
    if (ratio >= VS_THRESHOLD_NEAR_SQUARE) return VS_NEAR_SQUARE;
    if (ratio >= VS_THRESHOLD_MODERATE)    return VS_MODERATE;
    return VS_SKEWED;
}
