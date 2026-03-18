/**
 * cpss_query.c - Extended prime queries: next/prev prime, pi, nth_prime, prime_gap, range_stats,
 *                support_coverage, segment_info, status-aware wrappers.
 * Part of the CPSS Viewer amalgamation.
 */

/* Forward declarations for functions defined after this section */
static uint64_t cpss_count_range_hot(CPSSStream* s, uint64_t lo, uint64_t hi);
static size_t cpss_list_range_hot(CPSSStream* s, uint64_t lo, uint64_t hi,
    uint64_t* out_buf, size_t buf_cap);

/**
 * Reverse-map a result_bits position back to a candidate number.
 * Given the rb_pos (index into result_bits), find the candidate index
 * that maps to survivor_pos == rb_pos, then convert to candidate number.
 * Uses rank_prefix for O(RANK_BLOCK_SIZE/8) reverse lookup.
 * (Body placed here because get_bit/index_to_candidate are defined in cpss_stream.c.)
 */
static uint64_t rbits_pos_to_candidate(const HotSegmentEntry* hot, uint64_t start_index, uint64_t rb_pos) {
    /* Binary search rank_prefix to find which presieve block contains survivor_pos == rb_pos */
    size_t ps_block = 0u;
    {
        size_t lo = 0u, hi = hot->rank_blocks;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2u;
            if (hot->rank_prefix[mid + 1u] <= (uint32_t)rb_pos) lo = mid + 1u;
            else hi = mid;
        }
        ps_block = lo;
    }

    uint64_t ps_start = (uint64_t)ps_block * RANK_BLOCK_SIZE;
    uint64_t spos = hot->rank_prefix[ps_block];
    for (uint64_t idx = ps_start; idx < hot->total_candidates; ++idx) {
        if (!get_bit(hot->presieve, idx)) continue;
        if (spos == rb_pos) {
            return index_to_candidate(start_index + idx);
        }
        ++spos;
    }
    return 0u; /* shouldn't reach */
}

/* ══════════════════════════════════════════════════════════════════════
 * EXTENDED PRIME QUERY FUNCTIONS
 *
 * next_prime, prev_prime, pi, nth_prime, k_nearest_primes,
 * range_stats, factor_support.
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Find the smallest prime > n within the stream.
 * Sets *out_status if non-NULL.
 * Uses wheel-index stepping (no per-candidate modulo).
 */
static uint64_t cpss_next_prime_s(CPSSStream* s, uint64_t n, CPSSQueryStatus* out_status) {
    cpss_build_segment_index(s);
    if (s->index_len == 0u) {
        if (out_status) *out_status = CPSS_UNAVAILABLE;
        return 0u;
    }
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);

    /* Check wheel primes first */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (WHEEL_PRIMES[i] > n) {
            if (out_status) *out_status = CPSS_OK;
            return WHEEL_PRIMES[i];
        }
    }

    /* Find the first wheel index > n, then step through wheel indices.
     * This avoids the expensive per-candidate modulo in the inner loop. */
    uint64_t start = n + 1u;
    if (start < 17u) start = 17u;
    uint64_t widx;
    if (!next_candidate_index_ge(start, &widx)) {
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return 0u;
    }

    /* Step through wheel indices — each index_to_candidate is just a
     * multiply + table lookup, no modulo division. */
    while (1) {
        uint64_t candidate = index_to_candidate(widx);
        if (candidate > db_hi) break;
        int r = cpss_is_prime(s, candidate);
        if (r == 1) {
            if (out_status) *out_status = CPSS_OK;
            return candidate;
        }
        if (r < 0) {
            if (out_status) *out_status = (r == -2) ? CPSS_UNAVAILABLE : CPSS_OUT_OF_RANGE;
            return 0u;
        }
        ++widx;
    }
    if (out_status) *out_status = CPSS_OUT_OF_RANGE;
    return 0u;
}

/** Backward-compatible wrapper without status. */
static uint64_t cpss_next_prime(CPSSStream* s, uint64_t n) {
    return cpss_next_prime_s(s, n, NULL);
}

/**
 * Find the largest prime < n within the stream.
 * Sets *out_status if non-NULL.
 * Uses wheel-index stepping (no per-candidate modulo).
 */
static uint64_t cpss_prev_prime_s(CPSSStream* s, uint64_t n, CPSSQueryStatus* out_status) {
    cpss_build_segment_index(s);
    if (s->index_len == 0u) {
        if (out_status) *out_status = CPSS_UNAVAILABLE;
        return 0u;
    }

    if (n <= 2u) {
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return 0u;
    }

    /* Check wheel primes directly for small n */
    if (n <= WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u) {
        for (int i = (int)ARRAY_LEN(WHEEL_PRIMES) - 1; i >= 0; --i) {
            if (WHEEL_PRIMES[i] < n) {
                if (out_status) *out_status = CPSS_OK;
                return WHEEL_PRIMES[i];
            }
        }
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return 0u;
    }

    uint64_t db_lo = segment_start_n(&s->index_entries[0]);

    /* Find the last wheel index < n, then step backward through wheel indices. */
    uint64_t widx;
    if (!prev_candidate_index_le(n - 1u, &widx)) {
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return 0u;
    }

    while (1) {
        uint64_t candidate = index_to_candidate(widx);
        if (candidate < db_lo) break;
        int r = cpss_is_prime(s, candidate);
        if (r == 1) {
            if (out_status) *out_status = CPSS_OK;
            return candidate;
        }
        if (r < 0) {
            if (out_status) *out_status = (r == -2) ? CPSS_UNAVAILABLE : CPSS_OUT_OF_RANGE;
            return 0u;
        }
        if (widx == 0u) break;
        --widx;
    }

    /* Check wheel primes as fallback */
    for (int i = (int)ARRAY_LEN(WHEEL_PRIMES) - 1; i >= 0; --i) {
        if (WHEEL_PRIMES[i] < n) {
            if (out_status) *out_status = CPSS_OK;
            return WHEEL_PRIMES[i];
        }
    }
    if (out_status) *out_status = CPSS_OUT_OF_RANGE;
    return 0u;
}

/** Backward-compatible wrapper without status. */
static uint64_t cpss_prev_prime(CPSSStream* s, uint64_t n) {
    return cpss_prev_prime_s(s, n, NULL);
}

/**
 * pi(n): count of primes <= n.
 * Delegates to cpss_count_range_hot(s, 1, n) which uses prefix sums.
 */
static uint64_t cpss_pi(CPSSStream* s, uint64_t n) {
    if (n < 2u) return 0u;
    return cpss_count_range_hot(s, 1u, n);
}

/**
 * nth_prime(k): return the k-th prime (1-indexed: nth_prime(1)=2).
 * Uses segment prefix sums for O(log S) segment lookup.
 * Hot path uses rbits_select + rbits_pos_to_candidate for fast intra-segment lookup.
 * Returns the prime, or 0 if k exceeds the stream's coverage.
 */
static uint64_t cpss_nth_prime(CPSSStream* s, uint64_t k) {
    if (k == 0u) return 0u;

    /* Handle wheel primes */
    if (k <= ARRAY_LEN(WHEEL_PRIMES)) return WHEEL_PRIMES[k - 1u];
    uint64_t remaining = k - ARRAY_LEN(WHEEL_PRIMES);

    cpss_build_segment_index(s);
    cpss_build_prime_index(s);
    if (s->index_len == 0u) return 0u;

    /* Binary search on seg_prime_prefix to find which segment contains the target */
    size_t seg = 0u;
    {
        size_t lo = 0u, hi = s->index_len;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2u;
            if (s->seg_prime_prefix[mid + 1u] < remaining) lo = mid + 1u;
            else hi = mid;
        }
        seg = lo;
    }
    if (seg >= s->index_len) return 0u;

    uint64_t skip = remaining - s->seg_prime_prefix[seg];
    if (skip == 0u) skip = 1u;

    const SegmentIndexEntry* entry = &s->index_entries[seg];

    /* === Hot path: rbits_select-accelerated === */
    HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
    if (hot && hot->rbits_rank) {
        uint64_t rb_pos = rbits_select(hot, skip);
        if (rb_pos != UINT64_MAX) {
            return rbits_pos_to_candidate(hot, entry->start_index, rb_pos);
        }
        return 0u;
    }

    /* === Cold path: decompress segment and walk bits === */
    SegmentRecord rec;
    if (!cpss_load_segment(s, seg, &rec)) return 0u;

    ByteBuf presieve = build_presieve_state_for_index_range(entry->start_index, rec.total_candidates);
    bool presieve_borrowed = false;

    uint64_t prime_idx = 0u;
    uint64_t spos = 0u;
    uint64_t result = 0u;
    for (uint64_t idx = 0u; idx < rec.total_candidates; ++idx) {
        if (!get_bit(presieve.data, idx)) continue;
        if (get_bit(rec.result_bits, spos)) {
            ++prime_idx;
            if (prime_idx == skip) {
                result = index_to_candidate(rec.start_index + idx);
                break;
            }
        }
        ++spos;
    }

    if (!presieve_borrowed) free(presieve.data);
    segment_record_free(&rec);
    return result;
}

/** Prime gap info returned by cpss_prime_gap_at(). */
typedef struct {
    uint64_t n;
    bool n_is_prime;
    uint64_t prev;          /* largest prime < n, or 0 */
    uint64_t next;          /* smallest prime > n, or 0 */
    uint64_t gap_below;     /* n - prev, or 0 */
    uint64_t gap_above;     /* next - n, or 0 */
    uint64_t gap_around;    /* next - prev (full gap), or 0 */
} PrimeGapInfo;

/** Compute prime gap information at n. */
static PrimeGapInfo cpss_prime_gap_at(CPSSStream* s, uint64_t n) {
    PrimeGapInfo g;
    memset(&g, 0, sizeof(g));
    g.n = n;
    g.n_is_prime = (cpss_is_prime(s, n) == 1);
    g.prev = cpss_prev_prime(s, n);
    g.next = cpss_next_prime(s, n);
    if (g.prev > 0u) g.gap_below = n - g.prev;
    if (g.next > 0u) g.gap_above = g.next - n;
    if (g.prev > 0u && g.next > 0u) g.gap_around = g.next - g.prev;
    return g;
}

/** Range statistics returned by cpss_range_stats(). */
typedef struct {
    uint64_t lo, hi;
    uint64_t prime_count;
    uint64_t range_width;
    double density;           /* prime_count / range_width */
    double expected_density;  /* 1 / ln(midpoint) */
    uint64_t first_prime;
    uint64_t last_prime;
    uint64_t max_gap;
    uint64_t min_gap;
    double avg_gap;
} RangeStats;

/** Compute summary statistics for primes in [lo, hi]. */
static RangeStats cpss_range_stats(CPSSStream* s, uint64_t lo, uint64_t hi) {
    RangeStats st;
    memset(&st, 0, sizeof(st));
    st.lo = lo;
    st.hi = hi;
    st.range_width = (hi >= lo) ? hi - lo + 1u : 0u;
    if (st.range_width == 0u) return st;

    st.prime_count = cpss_count_range_hot(s, lo, hi);

    /* Get first prime: use next_prime from lo-1 (or is_prime at lo) */
    st.first_prime = 0u;
    if (st.prime_count > 0u) {
        if (cpss_is_prime(s, lo) == 1) st.first_prime = lo;
        else {
            uint64_t fp = cpss_next_prime(s, lo);
            if (fp > 0u && fp <= hi) st.first_prime = fp;
        }
    }

    /* Get last prime via prev_prime from hi+1 (or is_prime at hi) */
    st.last_prime = 0u;
    if (st.prime_count > 0u) {
        if (cpss_is_prime(s, hi) == 1) st.last_prime = hi;
        else {
            uint64_t lp = cpss_prev_prime(s, hi);
            if (lp > 0u && lp >= lo) st.last_prime = lp;
        }
    }

    /* Compute gap stats if we have enough primes */
    st.density = st.range_width > 0u ? (double)st.prime_count / (double)st.range_width : 0.0;
    double mid = (double)lo / 2.0 + (double)hi / 2.0;
    st.expected_density = mid > 2.0 ? 1.0 / log(mid) : 1.0;

    /* Compute gap stats via list_range_hot (sequential presieve/result_bits scan,
     * much faster than per-prime next_prime calls which redo segment lookup each time).
     * Cap at 10K primes (~80KB) to bound allocation. */
    if (st.prime_count >= 2u) {
        size_t gap_cap = (size_t)st.prime_count;
        if (gap_cap > 10000u) gap_cap = 10000u;
        uint64_t* primes = (uint64_t*)xmalloc(gap_cap * sizeof(uint64_t));
        size_t listed = cpss_list_range_hot(s, lo, hi, primes, gap_cap);
        if (listed >= 2u) {
            st.min_gap = UINT64_MAX;
            st.max_gap = 0u;
            double gap_sum = 0.0;
            for (size_t i = 1u; i < listed; ++i) {
                uint64_t gap = primes[i] - primes[i - 1u];
                if (gap < st.min_gap) st.min_gap = gap;
                if (gap > st.max_gap) st.max_gap = gap;
                gap_sum += (double)gap;
            }
            st.avg_gap = gap_sum / (double)(listed - 1u);
        }
        free(primes);
    }

    return st;
}

/* ══════════════════════════════════════════════════════════════════════
 * SUPPORT COVERAGE & SEGMENT INFO QUERIES
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Check whether the loaded DB has sufficient prime support for
 * complete trial division of n (i.e., all primes up to sqrt(n)).
 * Reports both DB-level coverage (segment index metadata) and
 * runtime availability (file mapped, hot-cached segments).
 */
static SupportCoverage cpss_support_coverage(CPSSStream* s, uint64_t n) {
    SupportCoverage cov;
    memset(&cov, 0, sizeof(cov));
    cov.n = n;
    cov.sqrt_n = (uint64_t)isqrt_u64(n);
    if (cov.sqrt_n * cov.sqrt_n < n) ++cov.sqrt_n; /* ceiling */

    cpss_build_segment_index(s);
    if (s->index_len == 0u) {
        return cov;
    }

    /* DB-level coverage: based on segment index metadata (always available) */
    cov.db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    cov.db_complete = (cov.db_hi >= cov.sqrt_n);
    uint64_t db_eff = cov.db_hi < cov.sqrt_n ? cov.db_hi : cov.sqrt_n;
    cov.db_coverage = cov.sqrt_n > 0u ? (double)db_eff / (double)cov.sqrt_n : 1.0;

    /* Runtime: is the file still memory-mapped? */
    cov.rt_file_mapped = (s->map != NULL);

    /* Runtime: find highest hot-cached segment */
    cov.rt_hot_hi = 0u;
    for (size_t i = s->index_len; i > 0u; --i) {
        const SegmentIndexEntry* entry = &s->index_entries[i - 1u];
        if (hot_cache_lookup(s->stream_id, entry->segment_no)) {
            cov.rt_hot_hi = entry->end_n;
            break;
        }
    }
    cov.rt_hot_complete = (cov.rt_hot_hi >= cov.sqrt_n);
    uint64_t hot_eff = cov.rt_hot_hi < cov.sqrt_n ? cov.rt_hot_hi : cov.sqrt_n;
    cov.rt_hot_coverage = cov.sqrt_n > 0u ? (double)hot_eff / (double)cov.sqrt_n : 0.0;

    return cov;
}

/**
 * Get detailed segment info for the segment containing candidate n.
 */
static SegmentInfo cpss_segment_info(CPSSStream* s, uint64_t n) {
    SegmentInfo info;
    memset(&info, 0, sizeof(info));
    info.n = n;

    cpss_build_segment_index(s);
    size_t seg_idx = cpss_find_segment_for_n(s, n);
    if (seg_idx == SIZE_MAX) {
        info.found = false;
        return info;
    }

    const SegmentIndexEntry* entry = &s->index_entries[seg_idx];
    info.found = true;
    info.segment_no = entry->segment_no;
    info.seg_start_n = entry->start_n;
    info.seg_end_n = entry->end_n;
    info.total_candidates = entry->total_candidates;
    info.survivor_count = entry->survivor_count;

    /* Check hot cache */
    HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
    info.is_hot = (hot != NULL);
    info.is_cold = (s->map != NULL);

    /* Get prime count from index if available */
    cpss_build_prime_index(s);
    if (s->seg_prime_counts && seg_idx < s->index_len &&
        s->seg_prime_counts[seg_idx] != UINT64_MAX) {
        info.prime_count = s->seg_prime_counts[seg_idx];
    }

    return info;
}

