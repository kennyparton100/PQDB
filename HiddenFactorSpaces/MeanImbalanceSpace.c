/**
 * MeanImbalanceSpace.c - Mean/imbalance space (m, delta) functions.
 * Part of the CPSS Hidden Factor Spaces module.
 *
 * A rotated coordinate system on top of LogSpace (u, v):
 *   m     = (u + v) / 2 = log(N) / 2
 *   delta = (v - u) / 2
 * Inverse: p = e^(m - delta), q = e^(m + delta).
 *
 * For fixed N, m is fixed so all uncertainty collapses into delta.
 * This makes it the most engine-friendly hidden space: factor-shape
 * reduces to a single parameter.
 *
 * Depends on: LogSpace.c (conceptual base layer),
 *             <math.h> (log, exp, fabs)
 */

/**
 * Routing bias based on delta magnitude.
 * These are tendencies, not guarantees. A large delta does not mean
 * a small factor is practically findable — it means the factor shape
 * is skewed, which biases toward certain method families.
 */
typedef enum {
    MI_BIAS_NEAR_SQUARE,   /* delta/m very small: Fermat-like methods favoured */
    MI_BIAS_MODERATE,      /* delta/m moderate: no strong bias */
    MI_BIAS_UNBALANCED,    /* delta/m larger: rho/ECM territory */
    MI_BIAS_HIGHLY_SKEWED  /* delta/m very large: small-factor methods favoured */
} MeanImbalanceBias;

#define MI_NEAR_SQUARE_THRESHOLD  0.05  /* delta/m < 0.05 */
#define MI_MODERATE_THRESHOLD     0.3   /* delta/m < 0.3  */
#define MI_UNBALANCED_THRESHOLD   0.7   /* delta/m < 0.7  */

/**
 * Compute (m, delta) coordinates from known factors p, q.
 * Built on top of LogSpace: uses log_space_coords() for (u, v), then
 *   m     = (u + v) / 2
 *   delta = (v - u) / 2
 * Returns false if either factor is 0 (propagated from log_space_coords).
 */
static bool mean_imbalance_from_factors(uint64_t p, uint64_t q,
                                         double* m, double* delta) {
    double u, v;
    if (!log_space_coords(p, q, &u, &v)) return false;
    *m = (u + v) / 2.0;
    *delta = (v - u) / 2.0;
    return true;
}

/**
 * Compute m from N alone (m is fully determined by the product).
 * Built on top of LogSpace: m = log(N) / 2 = log_space_product_constant(N) / 2.
 */
static double mean_imbalance_m_from_N(uint64_t N) {
    return log_space_product_constant(N) / 2.0;
}

/**
 * Inverse transform: approximate factors from (m, delta).
 * p_approx = e^(m - delta), q_approx = e^(m + delta).
 * These are floating-point approximations (useful for routing, not exact).
 */
static void mean_imbalance_to_factors(double m, double delta,
                                       double* p_approx, double* q_approx) {
    *p_approx = exp(m - delta);
    *q_approx = exp(m + delta);
}

/**
 * Classify the factor-shape bias based on delta/m ratio.
 * This describes the tendency of the factor pair's shape, not a
 * guaranteed routing decision. Actual routing should combine this
 * with observable signals (ObservableSpace.c) and coverage state
 * (CoverageSpace.c).
 */
static MeanImbalanceBias mean_imbalance_bias(double m, double delta) {
    if (m <= 0.0) return MI_BIAS_MODERATE;
    double ratio = fabs(delta) / m;
    if (ratio < MI_NEAR_SQUARE_THRESHOLD) return MI_BIAS_NEAR_SQUARE;
    if (ratio < MI_MODERATE_THRESHOLD)    return MI_BIAS_MODERATE;
    if (ratio < MI_UNBALANCED_THRESHOLD)  return MI_BIAS_UNBALANCED;
    return MI_BIAS_HIGHLY_SKEWED;
}
