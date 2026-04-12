/**
 * CFracSpace.c - Continued Fraction Space for factor-pair analysis.
 * Part of the CPSS Prime-Pair Spaces module.
 *
 * Analyses the continued fraction expansion of the ratio p/q (or q/p,
 * with p <= q) for a semiprime N = p * q. The CF expansion encodes the
 * "algebraic difficulty" of the factor ratio: ratios with short CF periods
 * or small partial quotients are structurally simpler.
 *
 * Axes:
 *   cf_length    - number of partial quotients in the CF expansion of q/p
 *   max_partial  - largest partial quotient encountered
 *   mean_partial - arithmetic mean of all partial quotients
 *   convergent_quality - how quickly convergents approximate q/p
 *
 * Depends on: <math.h> (fabs, log)
 */

#define CFRAC_MAX_QUOTIENTS 128

/** Result of a continued fraction expansion of the rational q/p. */
typedef struct {
    uint64_t quotients[CFRAC_MAX_QUOTIENTS]; /* partial quotients a_0, a_1, ... */
    uint32_t length;                          /* number of partial quotients */
    uint64_t max_partial;                     /* max(a_i) */
    double   mean_partial;                    /* mean(a_i) for i >= 1 */
    double   convergent_quality;              /* log2(q/p) / length — bits per step */
} CFracExpansion;

/**
 * Compute the continued fraction expansion of q/p where p <= q.
 * For a factor pair (p, q) with p <= q, this gives the CF of the ratio.
 * Returns the expansion result.
 */
static CFracExpansion cfrac_expand(uint64_t p, uint64_t q) {
    CFracExpansion cf;
    memset(&cf, 0, sizeof(cf));

    if (p == 0u || q == 0u) return cf;

    /* Ensure p <= q for canonical form */
    if (p > q) { uint64_t t = p; p = q; q = t; }

    uint64_t a = q, b = p;
    while (b > 0u && cf.length < CFRAC_MAX_QUOTIENTS) {
        uint64_t qi = a / b;
        cf.quotients[cf.length] = qi;
        if (qi > cf.max_partial) cf.max_partial = qi;
        ++cf.length;
        uint64_t r = a % b;
        a = b;
        b = r;
    }

    /* Compute mean of partial quotients (excluding a_0 if length > 1) */
    if (cf.length > 1u) {
        double sum = 0.0;
        for (uint32_t i = 1u; i < cf.length; ++i)
            sum += (double)cf.quotients[i];
        cf.mean_partial = sum / (double)(cf.length - 1u);
    } else if (cf.length == 1u) {
        cf.mean_partial = (double)cf.quotients[0];
    }

    /* Convergent quality: how many bits of the ratio each CF step resolves */
    if (cf.length > 0u && p > 0u) {
        double ratio = (double)q / (double)p;
        double bits = log2(ratio > 1.0 ? ratio : 1.0 / ratio);
        cf.convergent_quality = (cf.length > 1u) ? bits / (double)(cf.length - 1u) : bits;
    }

    return cf;
}

/**
 * Compute the best rational approximation (convergent) at step k.
 * Returns the k-th convergent numerator and denominator.
 * k is 0-indexed: k=0 gives a_0/1, k=1 gives (a_1*a_0+1)/a_1, etc.
 */
static void cfrac_convergent(const CFracExpansion* cf, uint32_t k,
                             uint64_t* num, uint64_t* den) {
    if (k >= cf->length || cf->length == 0u) {
        *num = 0u; *den = 1u; return;
    }

    uint64_t h_prev = 1u, h_curr = cf->quotients[0];
    uint64_t k_prev = 0u, k_curr = 1u;

    for (uint32_t i = 1u; i <= k && i < cf->length; ++i) {
        uint64_t a = cf->quotients[i];
        uint64_t h_next = a * h_curr + h_prev;
        uint64_t k_next = a * k_curr + k_prev;
        h_prev = h_curr; h_curr = h_next;
        k_prev = k_curr; k_curr = k_next;
    }

    *num = h_curr;
    *den = k_curr;
}

/**
 * CF-space "difficulty" score for a factor pair.
 * Lower score = easier to find via CF-based methods (e.g., CFRAC/SQUFOF).
 * Score = length * log2(max_partial + 1).
 */
static double cfrac_difficulty(uint64_t p, uint64_t q) {
    CFracExpansion cf = cfrac_expand(p, q);
    if (cf.length == 0u) return 0.0;
    return (double)cf.length * log2((double)cf.max_partial + 1.0);
}

/**
 * Is the factor pair "CF-easy"? True if CF expansion is short with small quotients.
 * Threshold: length <= 4 and max_partial <= 10.
 */
static bool cfrac_is_easy(uint64_t p, uint64_t q) {
    CFracExpansion cf = cfrac_expand(p, q);
    return cf.length <= 4u && cf.max_partial <= 10u;
}
