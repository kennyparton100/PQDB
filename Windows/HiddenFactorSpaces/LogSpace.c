/**
 * LogSpace.c - Log-factor space (u, v) functions.
 * Part of the CPSS Hidden Factor Spaces module.
 *
 * u = log(p), v = log(q). Straightens multiplication into addition.
 * Diagonal u = v => balanced; line u + v = log(N) => constant product.
 * Distance from diagonal |u - v| is multiplicative imbalance in disguise.
 *
 * This is the geometric base layer for the hidden factor spaces.
 * MeanImbalanceSpace.c is a rotated coordinate system on top of this:
 *   m     = (u + v) / 2   (along the constant-product line)
 *   delta = (v - u) / 2   (perpendicular: imbalance axis)
 *
 * Depends on: <math.h> (log, fabs)
 */

/**
 * Compute log-space coordinates for a factor pair.
 * Sets *u = log(p), *v = log(q) using natural logarithm.
 * Returns false if either factor is 0.
 */
static bool log_space_coords(uint64_t p, uint64_t q, double* u, double* v) {
    if (p == 0u || q == 0u) return false;
    *u = log((double)p);
    *v = log((double)q);
    return true;
}

/**
 * Distance from the diagonal in log space: |log(p) - log(q)| = |log(p/q)|.
 * This is the multiplicative imbalance measure. 0.0 = balanced.
 */
static double log_space_diagonal_distance(uint64_t p, uint64_t q) {
    if (p == 0u || q == 0u) return 0.0;
    return fabs(log((double)p) - log((double)q));
}

/**
 * Product constant in log space: log(N) = u + v.
 * For a fixed N, all factor pairs lie on the line u + v = log(N).
 */
static double log_space_product_constant(uint64_t N) {
    if (N == 0u) return 0.0;
    return log((double)N);
}

/**
 * Normalised imbalance in log space: |u - v| / (u + v).
 * Returns a value in [0.0, 1.0). 0.0 = balanced, approaching 1.0 = very skewed.
 * This is better than raw index distance because it accounts for prime gaps.
 */
static double log_space_imbalance(uint64_t p, uint64_t q) {
    if (p == 0u || q == 0u) return 0.0;
    double u = log((double)p);
    double v = log((double)q);
    double sum = u + v;
    if (sum <= 0.0) return 0.0;
    return fabs(u - v) / sum;
}
