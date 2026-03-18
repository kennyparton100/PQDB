/**
 * MinMaxSpace.c - Min/max factor space (s, l) functions.
 * Part of the CPSS Hidden Factor Spaces module.
 *
 * s = min(p, q), l = max(p, q). Only the upper triangle exists.
 * Every semiprime appears exactly once. Clean for small-factor coverage
 * visualisation and region elimination.
 *
 * All counting and enumeration is DB-bounded: results are only accurate
 * within the loaded CPSS stream's coverage. pi() and next_prime() may
 * return incomplete results beyond DB range.
 *
 * Depends on: cpss_arith.c (U128, u128_mul64),
 *             cpss_query.c (cpss_pi, cpss_next_prime_s),
 *             cpss_iter.c  (PrimeIterator)
 */

/**
 * Canonicalize a factor pair: force s <= l.
 * Sets *s_out = min(p, q), *l_out = max(p, q).
 */
static void minmax_canonicalize(uint64_t p, uint64_t q,
                                 uint64_t* s_out, uint64_t* l_out) {
    if (p <= q) {
        *s_out = p;
        *l_out = q;
    }
    else {
        *s_out = q;
        *l_out = p;
    }
}

/**
 * Check if a semiprime with smaller factor s_val is covered by
 * trial division up to trial_limit.
 * Returns true if s_val <= trial_limit (the small factor would be found).
 */
static bool minmax_is_covered(uint64_t s_val, uint64_t trial_limit) {
    return s_val <= trial_limit;
}

/**
 * Count the number of semiprimes p*q <= N_max that are covered by
 * trial division up to trial_limit, using CPSS to enumerate primes.
 * Only counts canonical pairs (s <= l).
 *
 * For each prime s <= trial_limit, counts primes l in [s, N_max/s].
 * This can be expensive for large ranges; use small N_max for exploration.
 *
 * DB-bounded: if l_max exceeds the DB's coverage, the count for that
 * row of s will be an undercount. The returned total is therefore a
 * lower bound when N_max is large relative to DB coverage.
 */
static uint64_t minmax_coverage_count(CPSSStream* stream, uint64_t trial_limit,
                                       uint64_t N_max) {
    uint64_t count = 0u;
    PrimeIterator s_iter;
    cpss_iter_init(&s_iter, stream, 2u);

    while (s_iter.valid && s_iter.current <= trial_limit) {
        uint64_t s = s_iter.current;
        if (s == 0u) break;
        uint64_t l_max = N_max / s;
        if (l_max < s) { cpss_iter_next(&s_iter); continue; }

        /* Count primes in [s, l_max] via pi(l_max) - pi(s-1).
         * pi() returns DB-bounded counts; if l_max > db_hi the
         * count is capped at the DB boundary (undercount). */
        uint64_t pi_hi = cpss_pi(stream, l_max);
        uint64_t pi_lo = (s >= 2u) ? cpss_pi(stream, s - 1u) : 0u;
        count += (pi_hi >= pi_lo) ? (pi_hi - pi_lo) : 0u;

        cpss_iter_next(&s_iter);
    }
    return count;
}

/**
 * Find the boundary of the uncovered region: the smallest semiprime
 * whose smaller factor exceeds trial_limit.
 * Returns (next_prime_after_trial_limit)^2, i.e., the smallest N = p*p
 * where p is the first prime > trial_limit.
 *
 * Uses status-aware cpss_next_prime_s to detect DB range limits.
 * Sets *out_status if non-NULL: CPSS_OK on success, CPSS_OUT_OF_RANGE
 * if trial_limit is at or beyond DB coverage.
 * Returns 0 on overflow or if the next prime is out of DB range.
 */
static uint64_t minmax_first_uncovered(CPSSStream* stream, uint64_t trial_limit,
                                        CPSSQueryStatus* out_status) {
    CPSSQueryStatus qs = CPSS_OK;
    uint64_t p = cpss_next_prime_s(stream, trial_limit, &qs);
    if (out_status) *out_status = qs;
    if (p == 0u || qs != CPSS_OK) return 0u;
    U128 sq = u128_mul64(p, p);
    return u128_fits_u64(sq) ? sq.lo : 0u;
}
