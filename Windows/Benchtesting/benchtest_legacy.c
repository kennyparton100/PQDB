/**
 * cpss_bench_legacy.c - Legacy per-sector benchmarks: benchmark_sector_fair, cmd_benchmark, cmd_benchmark_all_sectors
 * Part of the CPSS Viewer amalgamation.
 */

/** qsort comparator for doubles. */
static int cmp_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/** Compute the median of n doubles (non-destructive - copies first). */
static double median_of_copy(const double* data, int n) {
    double* copy = (double*)xmalloc((size_t)n * sizeof(*copy));
    memcpy(copy, data, (size_t)n * sizeof(*copy));
    qsort(copy, (size_t)n, sizeof(*copy), cmp_double);
    double m;
    if ((n & 1) != 0) {
        m = copy[n / 2];
    }
    else {
        m = 0.5 * (copy[n / 2 - 1] + copy[n / 2]);
    }
    free(copy);
    return m;
}

/**
 * Fair benchmark for a single exact segment.
 *
 * Splits CPSS timing into: decompression, presieve build, query.
 * Uses the strengthened odd-only sieve baseline with globally cached base primes.
 * Runs (repeats + 1) iterations; the first is a warmup excluded from stats.
 * Reuses heap buffers across repeats for both sides.
 *
 * Mode determines which CPSS components are included in the verdict total:
 *   hot:  query + (decomp if 1.2x rule triggers).
 *         Presieve excluded ONLY if the wheel cache actually holds the helper
 *         state for this segment.  If not cached, prep counts.
 *   fair: query + (decomp if 1.2x rule triggers) + presieve.
 *   auto: same as fair; per-sector 1.2x decision is reported.
 *
 * Warmup runs are allowed to populate the wheel cache.
 * Measured runs use whatever cache state exists after warmup.
 */
static BenchmarkSectorResult benchmark_sector_fair(
    CPSSStream* s, size_t seg_no, int repeats, uint64_t limit,
    bool include_write, BenchmarkMode mode
) {
    const SegmentIndexEntry* entry = cpss_get_segment_entry(s, seg_no);
    if (!entry) die("benchmark_sector_fair: segment out of range");

    uint64_t seg_lo = segment_start_n(entry);
    uint64_t seg_hi = segment_end_n(entry);

    /* Determine 1.2x decompression rule */
    bool decomp_included;
    double expansion = (entry->compressed_size > 0u)
        ? (double)entry->raw_size / (double)entry->compressed_size
        : 1.0;
    decomp_included = (entry->raw_size >= 1.2 * (double)entry->compressed_size);

    /* Compute presieve bitmap size for this segment */
    size_t presieve_byte_count = (size_t)((entry->total_candidates + 7u) / 8u);
    bool cache_fits = (g_wheel_cache.budget > 0 && presieve_byte_count <= g_wheel_cache.budget);

    /* For fair/auto modes, prep is always included.
       For hot mode, prep_included is determined per-run based on cache hit. */
    bool base_prep_included = (mode != BENCH_HOT);

    BenchmarkSectorResult result;
    memset(&result, 0, sizeof(result));
    result.segment_no = entry->segment_no;
    result.start_n = seg_lo;
    result.end_n = seg_hi;
    result.candidates = entry->total_candidates;
    result.survivors = entry->survivor_count;
    result.compressed_size = entry->compressed_size;
    result.raw_size = entry->raw_size;
    result.expansion_ratio = expansion;
    result.mode_name = benchmark_mode_name(mode);
    result.decomp_included = decomp_included;
    result.prep_included = base_prep_included; /* may be refined after runs */
    result.repeats = repeats;
    result.limit = limit;
    result.count_match = true;
    result.cache_fits = cache_fits;
    result.presieve_bytes = presieve_byte_count;
    result.cache_hit = false; /* set to true if at least first measured run hit */

    /* Compute stream_prime_count from index entry (need to decompress once) */
    {
        SegmentRecord tmp;
        if (!cpss_load_segment(s, seg_no, &tmp)) die("segment not available (file unmapped)");
        result.stream_prime_count = bit_count_bytes(tmp.result_bits, tmp.result_bits_len);
        for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
            if (seg_lo <= WHEEL_PRIMES[i] && WHEEL_PRIMES[i] <= seg_hi) {
                result.stream_prime_count += 1u;
            }
        }
        segment_record_free(&tmp);
    }

    /* Allocate time arrays */
    result.cpss_total_times = (double*)xmalloc((size_t)repeats * sizeof(double));
    result.sieve_total_times = (double*)xmalloc((size_t)repeats * sizeof(double));
    result.cpss_decompress_times = (double*)xmalloc((size_t)repeats * sizeof(double));
    result.cpss_prep_times = (double*)xmalloc((size_t)repeats * sizeof(double));
    result.cpss_query_times = (double*)xmalloc((size_t)repeats * sizeof(double));

    /* Pre-allocate reusable buffers outside the timed section. */
    uint8_t* sieve_buf = NULL;
    size_t sieve_buf_cap = 0u;

    /* Locate segment data in mmap */
    uint64_t comp_data_offset = entry->file_offset + 4u;
    const uint8_t* comp_data = s->map + comp_data_offset;
    uint32_t comp_len = entry->compressed_size;

    uint64_t first_cpss_count = UINT64_MAX;
    uint64_t first_sieve_count = UINT64_MAX;
    bool first_measured_cache_hit = false;

    /* Run (repeats + 1) iterations; iteration 0 is warmup */
    int total_runs = repeats + 1;
    for (int run = 0; run < total_runs; ++run) {
        bool is_warmup = (run == 0);
        int stat_idx = run - 1;

        /* === CPSS path: decompress -> presieve -> query === */
        uint8_t* raw = NULL;
        size_t raw_len = 0u;
        bool raw_borrowed = false; /* true if raw points into mmap (raw files) */
        double decompress_time = 0.0;

        if (s->is_raw) {
            /* Raw file: borrow directly from mmap, zero decompress cost */
            const uint8_t* seg_data = s->map + entry->file_offset + 4u;
            raw = (uint8_t*)seg_data; /* borrowed, do NOT free */
            raw_len = entry->raw_size;
            raw_borrowed = true;
        }
        else {
            /* Compressed file: inflate and time it */
            double td0 = now_sec();
            if (!zlib_decompress_alloc(comp_data, comp_len, &raw, &raw_len)) {
                die("benchmark: zlib decompression failed");
            }
            double td1 = now_sec();
            decompress_time = td1 - td0;
        }

        /* Parse the decompressed segment header */
        if (raw_len < 12u) { if (!raw_borrowed) free(raw); die("benchmark: raw payload too short"); }
        uint32_t total_candidates = read_u32_le(raw + 0u);
        uint32_t survivor_count = read_u32_le(raw + 4u);
        uint32_t rbits_len = read_u32_le(raw + 8u);
        uint8_t* result_bits = raw + 12u;
        (void)rbits_len;

        /* Presieve: check wheel cache first (hot mode), else build */
        ByteBuf presieve = { 0 };
        double prep_time = 0.0;
        bool run_cache_hit = false;

        if (mode == BENCH_HOT && g_wheel_cache.budget > 0) {
            size_t cached_len = 0u;
            const uint8_t* cached = wheel_cache_lookup(s->stream_id, entry->segment_no, &cached_len);
            if (cached) {
                /* Cache hit - use cached presieve, prep_time = 0 */
                presieve.data = (uint8_t*)cached; /* read-only borrow */
                presieve.len = cached_len;
                run_cache_hit = true;
                prep_time = 0.0;
            }
        }

        if (!run_cache_hit) {
            /* Cache miss or non-hot mode - build presieve and time it */
            double tp0 = now_sec();
            presieve = build_presieve_state_for_index_range(entry->start_index, total_candidates);
            double tp1 = now_sec();
            prep_time = tp1 - tp0;

            /* In hot mode, try to cache the presieve for future runs */
            if (mode == BENCH_HOT && g_wheel_cache.budget > 0 && cache_fits) {
                /* Make a copy for the cache (presieve.data will be freed later or owned by cache) */
                uint8_t* cache_copy = (uint8_t*)xmalloc(presieve.len);
                memcpy(cache_copy, presieve.data, presieve.len);
                wheel_cache_insert(s->stream_id, entry->segment_no, entry->start_index,
                    total_candidates, cache_copy, presieve.len);
            }
        }

        /* Build a SegmentRecord on the stack for query */
        SegmentRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.segment_no = entry->segment_no;
        rec.start_index = entry->start_index;
        rec.next_index = entry->next_index;
        rec.total_candidates = total_candidates;
        rec.survivor_count = survivor_count;
        rec.result_bits_len = (survivor_count + 7u) / 8u;
        rec.raw_data = raw;
        rec.raw_size = raw_len;
        rec.result_bits = result_bits;
        rec.compressed_size = comp_len;
        rec.file_offset = entry->file_offset;

        /* Query: count primes from decoded CPSS data */
        double tq0 = now_sec();
        uint64_t cpss_count = 0u;
        for (size_t wi = 0u; wi < ARRAY_LEN(WHEEL_PRIMES); ++wi) {
            uint64_t p = WHEEL_PRIMES[wi];
            if (seg_lo <= p && p <= seg_hi) {
                ++cpss_count;
                if (limit && cpss_count >= limit) break;
            }
        }
        if (!limit || cpss_count < limit) {
            process_loaded_segment_range(&rec, &presieve, seg_lo, seg_hi, limit, NULL, &cpss_count);
        }
        double tq1 = now_sec();
        double query_time = tq1 - tq0;

        /* Free presieve only if we own it (not a cache borrow) */
        if (!run_cache_hit) {
            free(presieve.data);
        }
        if (!raw_borrowed) free(raw);

        /* Compute CPSS verdict total based on mode and actual cache state */
        bool this_prep_included;
        if (mode == BENCH_HOT) {
            /* Hot mode: prep excluded only if cache actually served it */
            this_prep_included = !run_cache_hit;
        }
        else {
            this_prep_included = true; /* fair/auto always include prep */
        }

        double cpss_total = query_time;
        if (decomp_included) cpss_total += decompress_time;
        if (this_prep_included) cpss_total += prep_time;

        /* === Sieve path === */
        double ts0 = now_sec();
        uint64_t sieve_count = segmented_sieve_odd(seg_lo, seg_hi, limit, NULL, &sieve_buf, &sieve_buf_cap);
        double ts1 = now_sec();
        double sieve_total = ts1 - ts0;

        /* Record results (skip warmup) */
        if (!is_warmup) {
            result.cpss_total_times[stat_idx] = cpss_total;
            result.sieve_total_times[stat_idx] = sieve_total;
            result.cpss_decompress_times[stat_idx] = decompress_time;
            result.cpss_prep_times[stat_idx] = prep_time;
            result.cpss_query_times[stat_idx] = query_time;

            /* Track whether first measured run was a cache hit */
            if (first_cpss_count == UINT64_MAX) {
                first_measured_cache_hit = run_cache_hit;
                first_cpss_count = cpss_count;
                first_sieve_count = sieve_count;
            }
            else if (cpss_count != first_cpss_count || sieve_count != first_sieve_count) {
                result.count_match = false;
            }
            if (cpss_count != sieve_count) {
                result.count_match = false;
            }
        }
    }

    free(sieve_buf);

    /* Finalize cache_hit and prep_included based on measured runs */
    result.cache_hit = first_measured_cache_hit;
    if (mode == BENCH_HOT) {
        result.prep_included = !first_measured_cache_hit;
    }

    /* Compute medians */
    result.cpss_total_median = median_of_copy(result.cpss_total_times, repeats);
    result.sieve_total_median = median_of_copy(result.sieve_total_times, repeats);
    result.cpss_decompress_median = median_of_copy(result.cpss_decompress_times, repeats);
    result.cpss_prep_median = median_of_copy(result.cpss_prep_times, repeats);
    result.cpss_query_median = median_of_copy(result.cpss_query_times, repeats);

    /* Determine winner */
    if (fabs(result.cpss_total_median - result.sieve_total_median) < 1e-12) {
        result.winner = "tie";
        result.speedup = 1.0;
    }
    else if (result.cpss_total_median < result.sieve_total_median) {
        result.winner = "cpss";
        result.speedup = result.sieve_total_median / result.cpss_total_median;
    }
    else {
        result.winner = "sieve";
        result.speedup = result.cpss_total_median / result.sieve_total_median;
    }

    (void)include_write;
    return result;
}

/** Free heap data inside a BenchmarkSectorResult. */
static void benchmark_sector_result_free(BenchmarkSectorResult* r) {
    free(r->cpss_total_times);
    free(r->sieve_total_times);
    free(r->cpss_decompress_times);
    free(r->cpss_prep_times);
    free(r->cpss_query_times);
    memset(r, 0, sizeof(*r));
}

/** Format a byte count as a human-readable string (e.g. "1.50 MB"). */
