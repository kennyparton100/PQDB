/**
 * IndexSpace.c - Index space (i, j) functions.
 * Part of the CPSS Hidden Factor Spaces module.
 *
 * Axes = 1-based prime indices, not prime values.
 * product(i, j) = prime(i) * prime(j)
 *
 * Good for: storage ranges, CPSS coverage ranges, index-bounded sweeps,
 * bookkeeping. Note: uniform index steps map to non-uniform prime gaps.
 *
 * All index lookups are DB-bounded: results are only valid for indices
 * within the loaded CPSS stream's coverage. Out-of-range indices produce
 * explicit failure via status codes or false returns.
 *
 * Depends on: cpss_query.c (cpss_nth_prime, cpss_pi),
 *             cpss_arith.c (U128, miller_rabin_u64),
 *             cpss_iter.c  (PrimeIterator)
 */

/** Result of an index-space product lookup. */
typedef struct {
    uint64_t p;         /* prime(i), 0 if out of range */
    uint64_t q;         /* prime(j), 0 if out of range */
    U128 product;       /* p * q as U128 (never overflows) */
    bool valid;         /* true if both indices resolved */
} IndexSpaceProduct;

/**
 * Compute N = prime(i) * prime(j) using the CPSS stream.
 * i and j are 1-based indices (1 -> 2, 2 -> 3, 3 -> 5, ...).
 * Returns a full result struct; check .valid before using .product.
 */
static IndexSpaceProduct index_space_product(CPSSStream* s, uint64_t i, uint64_t j) {
    IndexSpaceProduct r;
    memset(&r, 0, sizeof(r));
    if (i == 0u || j == 0u) return r;
    r.p = cpss_nth_prime(s, i);
    r.q = cpss_nth_prime(s, j);
    if (r.p == 0u || r.q == 0u) return r;
    r.product = u128_mul64(r.p, r.q);
    r.valid = true;
    return r;
}

/**
 * Reverse lookup: given primes p and q, find their 1-based indices.
 * Returns true only if:
 *   1. Both p and q are prime (Miller-Rabin verified).
 *   2. Both fall within the DB's coverage range.
 *   3. The round-trip nth_prime(pi(p)) == p confirms DB completeness.
 *
 * Without the round-trip check, pi(p) could be wrong if the DB has
 * gaps or p is beyond DB coverage.
 */
static bool index_space_from_primes(CPSSStream* s, uint64_t p, uint64_t q,
                                     uint64_t* i_out, uint64_t* j_out) {
    if (!miller_rabin_u64(p) || !miller_rabin_u64(q)) return false;

    uint64_t pi_p = cpss_pi(s, p);
    uint64_t pi_q = cpss_pi(s, q);
    if (pi_p == 0u || pi_q == 0u) return false;

    /* Round-trip verification: confirm the DB actually covers these primes */
    if (cpss_nth_prime(s, pi_p) != p) return false;
    if (cpss_nth_prime(s, pi_q) != q) return false;

    *i_out = pi_p;
    *j_out = pi_q;
    return true;
}

/**
 * Enumerate products for row i using the prime iterator.
 * N = prime(i) * prime(j) for j = 1..max_j.
 * Writes products as U128 into out[]. Returns actual count written.
 * out must have space for at least max_j elements.
 *
 * Also writes the column primes into primes_out[] if non-NULL
 * (must also have space for max_j elements).
 */
static size_t index_space_row(CPSSStream* s, uint64_t i, uint64_t max_j,
                               U128* out, uint64_t* primes_out) {
    if (i == 0u || max_j == 0u) return 0u;
    uint64_t p = cpss_nth_prime(s, i);
    if (p == 0u) return 0u;

    size_t count = 0u;
    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);

    uint64_t j = 0u;
    while (iter.valid && j < max_j) {
        uint64_t q = iter.current;
        out[count] = u128_mul64(p, q);
        if (primes_out) primes_out[count] = q;
        ++count;
        ++j;
        cpss_iter_next(&iter);
    }
    return count;
}

/** Single cell in index space with full labels. */
typedef struct {
    uint64_t i;         /* row index (1-based) */
    uint64_t j;         /* column index (1-based) */
    uint64_t p;         /* prime(i) */
    uint64_t q;         /* prime(j) */
    U128 product;       /* p * q */
    bool valid;
} IndexSpaceCell;

/** Compute a single labelled cell at (i, j). */
static IndexSpaceCell index_space_cell(CPSSStream* s, uint64_t i, uint64_t j) {
    IndexSpaceCell c;
    memset(&c, 0, sizeof(c));
    c.i = i; c.j = j;
    if (i == 0u || j == 0u) return c;
    c.p = cpss_nth_prime(s, i);
    c.q = cpss_nth_prime(s, j);
    if (c.p == 0u || c.q == 0u) return c;
    c.product = u128_mul64(c.p, c.q);
    c.valid = true;
    return c;
}

/**
 * Enumerate cells for row i, columns j_lo..j_hi.
 * Uses nth_prime for i (once), then PrimeIterator to skip to j_lo
 * and iterate through j_hi without repeated nth_prime calls.
 *
 * out must have space for at least (j_hi - j_lo + 1) elements.
 * Returns actual count written (may be less if DB range is exceeded).
 */
static size_t index_space_row_range(CPSSStream* s, uint64_t i,
                                     uint64_t j_lo, uint64_t j_hi,
                                     IndexSpaceCell* out) {
    if (i == 0u || j_lo == 0u || j_hi < j_lo) return 0u;
    uint64_t p = cpss_nth_prime(s, i);
    if (p == 0u) return 0u;

    /* Get prime(j_lo) to seed the iterator at the right starting point */
    uint64_t q_start = cpss_nth_prime(s, j_lo);
    if (q_start == 0u) return 0u;

    PrimeIterator iter;
    cpss_iter_init(&iter, s, q_start);

    size_t count = 0u;
    for (uint64_t j = j_lo; j <= j_hi && iter.valid; ++j) {
        IndexSpaceCell* c = &out[count];
        c->i = i;
        c->j = j;
        c->p = p;
        c->q = iter.current;
        c->product = u128_mul64(p, iter.current);
        c->valid = true;
        ++count;
        if (j < j_hi) cpss_iter_next(&iter);
    }
    return count;
}

/**
 * Enumerate cells for column j, rows i_lo..i_hi.
 * Uses nth_prime for j (once), then PrimeIterator for row primes.
 *
 * out must have space for at least (i_hi - i_lo + 1) elements.
 * Returns actual count written.
 */
static size_t index_space_col_range(CPSSStream* s, uint64_t j,
                                     uint64_t i_lo, uint64_t i_hi,
                                     IndexSpaceCell* out) {
    if (j == 0u || i_lo == 0u || i_hi < i_lo) return 0u;
    uint64_t q = cpss_nth_prime(s, j);
    if (q == 0u) return 0u;

    uint64_t p_start = cpss_nth_prime(s, i_lo);
    if (p_start == 0u) return 0u;

    PrimeIterator iter;
    cpss_iter_init(&iter, s, p_start);

    size_t count = 0u;
    for (uint64_t i = i_lo; i <= i_hi && iter.valid; ++i) {
        IndexSpaceCell* c = &out[count];
        c->i = i;
        c->j = j;
        c->p = iter.current;
        c->q = q;
        c->product = u128_mul64(iter.current, q);
        c->valid = true;
        ++count;
        if (i < i_hi) cpss_iter_next(&iter);
    }
    return count;
}

/**
 * Enumerate all cells in rectangle [i_lo..i_hi] x [j_lo..j_hi].
 * If unique_only is true, only cells with i <= j are included
 * (upper triangle — each semiprime appears once).
 *
 * Iterates row-by-row using index_space_row_range internally.
 * out must have space for (i_hi-i_lo+1)*(j_hi-j_lo+1) elements
 * (or half that if unique_only).
 * Returns actual count written.
 */
static size_t index_space_rect(CPSSStream* s,
                                uint64_t i_lo, uint64_t i_hi,
                                uint64_t j_lo, uint64_t j_hi,
                                bool unique_only,
                                IndexSpaceCell* out, size_t out_cap) {
    if (i_lo == 0u || j_lo == 0u || i_hi < i_lo || j_hi < j_lo) return 0u;

    size_t total = 0u;
    uint64_t cols = j_hi - j_lo + 1u;
    /* Temp buffer for one row */
    IndexSpaceCell* row_buf = (IndexSpaceCell*)xmalloc((size_t)cols * sizeof(IndexSpaceCell));

    for (uint64_t i = i_lo; i <= i_hi; ++i) {
        uint64_t eff_j_lo = unique_only && i > j_lo ? i : j_lo;
        if (eff_j_lo > j_hi) continue;

        size_t row_count = index_space_row_range(s, i, eff_j_lo, j_hi, row_buf);
        for (size_t k = 0u; k < row_count && total < out_cap; ++k) {
            out[total++] = row_buf[k];
        }
    }

    free(row_buf);
    return total;
}

/**
 * Diagonal region: all (i, i) for i in [i_lo..i_hi].
 * These are the perfect-square semiprimes: N = prime(i)^2.
 * out must have space for (i_hi - i_lo + 1) elements.
 */
static size_t index_space_diagonal(CPSSStream* s, uint64_t i_lo, uint64_t i_hi,
                                    IndexSpaceCell* out) {
    if (i_lo == 0u || i_hi < i_lo) return 0u;
    uint64_t p_start = cpss_nth_prime(s, i_lo);
    if (p_start == 0u) return 0u;

    PrimeIterator iter;
    cpss_iter_init(&iter, s, p_start);
    size_t count = 0u;
    for (uint64_t i = i_lo; i <= i_hi && iter.valid; ++i) {
        IndexSpaceCell* c = &out[count];
        c->i = i; c->j = i;
        c->p = iter.current; c->q = iter.current;
        c->product = u128_mul64(iter.current, iter.current);
        c->valid = true;
        ++count;
        if (i < i_hi) cpss_iter_next(&iter);
    }
    return count;
}

/**
 * Band region: all (i, i+offset) for i in [i_lo..i_hi] where i+offset >= 1.
 * offset=0 is the diagonal. offset=1 is the first super-diagonal, etc.
 * out must have space for (i_hi - i_lo + 1) elements.
 */
static size_t index_space_band(CPSSStream* s, int64_t offset,
                                uint64_t i_lo, uint64_t i_hi,
                                IndexSpaceCell* out) {
    if (i_lo == 0u || i_hi < i_lo) return 0u;
    size_t count = 0u;
    for (uint64_t i = i_lo; i <= i_hi; ++i) {
        int64_t j_signed = (int64_t)i + offset;
        if (j_signed < 1) continue;
        uint64_t j = (uint64_t)j_signed;
        IndexSpaceCell c = index_space_cell(s, i, j);
        if (!c.valid) continue;
        out[count++] = c;
    }
    return count;
}

/**
 * Triangle region: all (i, j) with i_lo <= i <= j <= i_hi.
 * This is the upper triangle of the square [i_lo..i_hi]^2.
 * Equivalent to index_space_rect with unique_only=true and j range = i range.
 * out must have space for (i_hi-i_lo+1)*(i_hi-i_lo+2)/2 elements.
 */
static size_t index_space_triangle(CPSSStream* s, uint64_t i_lo, uint64_t i_hi,
                                    IndexSpaceCell* out, size_t out_cap) {
    return index_space_rect(s, i_lo, i_hi, i_lo, i_hi, true, out, out_cap);
}

/**
 * Window region: all cells in [i-r..i+r] x [j-r..j+r], clipped to >= 1.
 * out must have space for (2*r+1)^2 elements.
 */
static size_t index_space_window(CPSSStream* s, uint64_t ci, uint64_t cj,
                                  uint64_t radius, IndexSpaceCell* out, size_t out_cap) {
    uint64_t i_lo = (ci > radius) ? ci - radius : 1u;
    uint64_t i_hi = ci + radius;
    uint64_t j_lo = (cj > radius) ? cj - radius : 1u;
    uint64_t j_hi = cj + radius;
    return index_space_rect(s, i_lo, i_hi, j_lo, j_hi, false, out, out_cap);
}

/**
 * Compute the min and max products for an index rectangle [i_lo..i_hi] x [j_lo..j_hi].
 * Returns true if bounds were computed successfully.
 * Products are returned as U128 to avoid truncation.
 *
 * N_min = prime(i_lo) * prime(j_lo)  (smallest pair in rectangle)
 * N_max = prime(i_hi) * prime(j_hi)  (largest pair in rectangle)
 */
static bool index_space_bounds(CPSSStream* s,
                                uint64_t i_lo, uint64_t i_hi,
                                uint64_t j_lo, uint64_t j_hi,
                                U128* N_min, U128* N_max) {
    if (i_lo == 0u || j_lo == 0u || i_hi < i_lo || j_hi < j_lo) return false;

    uint64_t p_lo = cpss_nth_prime(s, i_lo);
    uint64_t q_lo = cpss_nth_prime(s, j_lo);
    uint64_t p_hi = cpss_nth_prime(s, i_hi);
    uint64_t q_hi = cpss_nth_prime(s, j_hi);
    if (p_lo == 0u || q_lo == 0u || p_hi == 0u || q_hi == 0u) return false;

    *N_min = u128_mul64(p_lo, q_lo);
    *N_max = u128_mul64(p_hi, q_hi);
    return true;
}
