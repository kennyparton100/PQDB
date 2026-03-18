/**
 * ProductRatioSpace.c - Product/ratio space (N, R) functions.
 * Part of the CPSS Hidden Factor Spaces module.
 *
 * N = p*q (known), R = max(p,q) / min(p,q) >= 1.
 * For a fixed input N, the product is known so the unknown is the ratio.
 * This is one of the best routing views: "How imbalanced are the factors?"
 *
 * This file is pure ratio-space math. Routing heuristics (e.g. "is Fermat
 * plausible?") belong in ObservableSpace.c where they can use cheap signals.
 *
 * Depends on: cpss_arith.c (isqrt_u64, U128)
 */

/** Ratio classification buckets. */
typedef enum {
    RC_BALANCED,    /* R < RC_BALANCED_THRESHOLD */
    RC_MODERATE,    /* R < RC_SKEWED_THRESHOLD */
    RC_SKEWED       /* R >= RC_SKEWED_THRESHOLD */
} RatioClass;

#define RC_BALANCED_THRESHOLD  2.0    /* R < 2 => near-square region */
#define RC_SKEWED_THRESHOLD    100.0  /* R >= 100 => highly skewed */

/**
 * Compute the factor ratio R = max(p,q) / min(p,q).
 * R >= 1.0 always. Returns 0.0 if either factor is 0.
 */
static double ratio_from_factors(uint64_t p, uint64_t q) {
    if (p == 0u || q == 0u) return 0.0;
    uint64_t lo = (p < q) ? p : q;
    uint64_t hi = (p < q) ? q : p;
    return (double)hi / (double)lo;
}

/**
 * Classify the ratio into buckets.
 * BALANCED:  R < 2    (near-square)
 * MODERATE:  R < 100  (general methods territory)
 * SKEWED:    R >= 100  (small-factor territory)
 */
static RatioClass ratio_classify(double R) {
    if (R < RC_BALANCED_THRESHOLD) return RC_BALANCED;
    if (R < RC_SKEWED_THRESHOLD)   return RC_MODERATE;
    return RC_SKEWED;
}

/**
 * Compute theoretical R bounds for a semiprime N = p * q.
 *
 * R_min = 1.0 (when p == q, i.e. N is a perfect square of a prime).
 * R_max = the ratio when the smaller factor is the smallest prime
 *         that divides N. Without knowing factors, the tightest bound is:
 *   - If N is even:  R_max = (N/2) / 2 = N/4
 *   - If N is odd:   R_max = (N/3) / 3 = N/9 (smallest odd prime factor >= 3)
 *   - General:       R_max = (N/s) / s = N/s^2 for smallest prime factor s
 *
 * These are loose upper bounds. The actual R depends on the true factors.
 */
static void ratio_bounds_for_N(uint64_t N, double* R_min_out, double* R_max_out) {
    *R_min_out = 1.0;
    if (N < 4u) {
        *R_max_out = 1.0;
        return;
    }
    /* Find the smallest prime that could divide N */
    uint64_t smallest_possible = 2u;
    if ((N & 1u) != 0u) smallest_possible = 3u;
    /* R_max = (N / s) / s where s is the smallest possible factor */
    *R_max_out = (double)N / ((double)smallest_possible * (double)smallest_possible);
}

/**
 * Given N and a known smaller factor s, compute the exact ratio.
 * R = (N/s) / s. Returns 0.0 if s is 0 or doesn't divide N.
 */
static double ratio_from_N_and_small_factor(uint64_t N, uint64_t s) {
    if (s == 0u || N % s != 0u) return 0.0;
    uint64_t l = N / s;
    return (double)l / (double)s;
}
