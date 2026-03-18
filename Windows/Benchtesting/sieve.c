/**
 * cpss_sieve.c - Segmented sieve baseline for benchmarks
 * Part of the CPSS Viewer amalgamation.
 */
/** Sieve primes in [a,b] from scratch, counting and optionally writing them. */
static void segmented_sieve_count_write(uint64_t a, uint64_t b, uint64_t limit, FILE* out_fp, uint64_t* count_out) {
    *count_out = 0u;
    if (b < 2u || b < a) {
        return;
    }
    if (a < 2u) {
        a = 2u;
    }

    uint64_t size64 = b - a + 1u;
    if (size64 > SIZE_MAX) {
        die("segmented_sieve_range too large for this build");
    }
    size_t size = (size_t)size64;

    uint32_t root = isqrt_u64(b);
    U32Vec base = simple_base_primes(root);
    uint8_t* sieve = (uint8_t*)xmalloc(size);
    memset(sieve, 1, size);

    for (size_t i = 0u; i < base.len; ++i) {
        uint32_t p = base.data[i];
        uint64_t p2 = (uint64_t)p * (uint64_t)p;
        uint64_t start = p2;
        if (start < a) {
            start = ((a + p - 1u) / p) * (uint64_t)p;
        }
        for (uint64_t n = start; n <= b; n += p) {
            sieve[n - a] = 0u;
            if (UINT64_MAX - n < p) {
                break;
            }
        }
    }

    for (size_t i = 0u; i < size; ++i) {
        if (sieve[i]) {
            ++(*count_out);
            maybe_write_prime(out_fp, a + i);
            if (limit && *count_out >= limit) {
                break;
            }
        }
    }

    free(base.data);
    free(sieve);
}

/**
 * Odd-only segmented sieve for benchmark baseline.
 * Uses the global cached base primes (must be pre-warmed via ensure_base_primes_cached).
 * Caller provides a reusable sieve buffer to avoid allocator churn across repeats.
 * sieve_buf/sieve_buf_cap are in/out: the buffer is grown if needed.
 * Returns prime count in [a, b].  Optionally writes primes to out_fp.
 */
static uint64_t segmented_sieve_odd(
    uint64_t a, uint64_t b, uint64_t limit, FILE* out_fp,
    uint8_t** sieve_buf, size_t* sieve_buf_cap
) {
    uint64_t count = 0u;
    if (b < 2u || b < a) {
        return 0u;
    }

    /* Handle 2 separately */
    if (a <= 2u && 2u <= b) {
        ++count;
        maybe_write_prime(out_fp, 2u);
        if (limit && count >= limit) return count;
    }

    /* Odd-only range: map odd numbers in [odd_lo, odd_hi] */
    uint64_t odd_lo = (a <= 3u) ? 3u : (a | 1u);  /* first odd >= max(a,3) */
    uint64_t odd_hi = b | 0u;                       /* b itself, we'll check parity */
    if (!(odd_hi & 1u)) {
        if (odd_hi == 0u) return count;
        odd_hi -= 1u;
    }
    if (odd_lo > odd_hi) return count;

    /* sieve[i] represents the odd number odd_lo + 2*i */
    uint64_t range = (odd_hi - odd_lo) / 2u + 1u;
    if (range > SIZE_MAX) {
        die("segmented_sieve_odd: range too large for this build");
    }
    size_t size = (size_t)range;

    /* Grow caller's reusable buffer if needed */
    if (size > *sieve_buf_cap) {
        free(*sieve_buf);
        *sieve_buf = (uint8_t*)xmalloc(size);
        *sieve_buf_cap = size;
    }
    uint8_t* sieve = *sieve_buf;
    memset(sieve, 1, size);

    /* Mark composites using globally cached base primes (skip p=2) */
    for (size_t pi = 1u; pi < g_cached_base_primes.len; ++pi) {
        uint32_t p = g_cached_base_primes.data[pi];
        uint64_t p64 = (uint64_t)p;
        if (p64 * p64 > odd_hi) break;

        /* Find first odd multiple of p >= max(p*p, odd_lo) */
        uint64_t start = p64 * p64;
        if (start < odd_lo) {
            uint64_t k = (odd_lo - start + 2u * p64 - 1u) / (2u * p64);
            start += k * 2u * p64;
        }
        if (!(start & 1u)) {
            start += p64;
        }

        /* Mark odd multiples of p */
        if (start >= odd_lo) {
            uint64_t idx = (start - odd_lo) / 2u;
            for (size_t j = (size_t)idx; j < size; j += p) {
                sieve[j] = 0u;
            }
        }
    }

    /* Count / write surviving primes */
    for (size_t i = 0u; i < size; ++i) {
        if (sieve[i]) {
            ++count;
            maybe_write_prime(out_fp, odd_lo + 2u * i);
            if (limit && count >= limit) break;
        }
    }

    return count;
}

/** Create and open a temporary file for benchmark write tests (Win32). */
static FILE* open_temp_output(char* path_buf, size_t path_buf_len) {
    char temp_dir[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);
    if (!n || n >= sizeof(temp_dir)) die_win32("GetTempPathA failed");
    if (path_buf_len < MAX_PATH) die("temp path buffer too small");
    if (GetTempFileNameA(temp_dir, "cps", 0, path_buf) == 0) die_win32("GetTempFileNameA failed");

    FILE* fp = fopen(path_buf, "w");
    if (!fp) {
        remove(path_buf);
        die_errno("fopen temp file failed");
    }
    return fp;
}

/**
 * Total-time-only range benchmark (non-exact-segment path).
 * Clearly labeled: no component split, uses strengthened odd-only sieve.
 * Used when the benchmarked range does not match exactly one segment.
 */
static BenchmarkRow benchmark_range_once(CPSSStream* s, uint64_t lo, uint64_t hi, uint64_t limit, bool include_write) {
    BenchmarkRow row;
    memset(&row, 0, sizeof(row));
    row.decomp_included = true;
    row.prep_included = true;

    uint32_t root = isqrt_u64(hi);
    ensure_base_primes_cached(root);

    uint8_t* sieve_buf = NULL;
    size_t sieve_buf_cap = 0u;

    double t0 = now_sec();
    cpss_scan_range(s, lo, hi, limit, NULL, &row.cpss_count);
    double t1 = now_sec();
    row.cpss_total_time = t1 - t0;
    row.cpss_decompress_time = row.cpss_total_time;
    row.cpss_prep_time = 0.0;
    row.cpss_query_time = 0.0;

    if (include_write) {
        char path[TEMP_PATH_CAP];
        FILE* fp = open_temp_output(path, sizeof(path));
        double tw0 = now_sec();
        uint64_t written = 0u;
        cpss_scan_range(s, lo, hi, limit, fp, &written);
        double tw1 = now_sec();
        fclose(fp);
        remove(path);
        row.cpss_write_time = tw1 - tw0;
        row.cpss_write_time_valid = true;
    }

    double t2 = now_sec();
    row.sieve_count = segmented_sieve_odd(lo, hi, limit, NULL, &sieve_buf, &sieve_buf_cap);
    double t3 = now_sec();
    row.sieve_total_time = t3 - t2;

    if (include_write) {
        char path[TEMP_PATH_CAP];
        FILE* fp = open_temp_output(path, sizeof(path));
        double tw0 = now_sec();
        uint64_t written = 0u;
        segmented_sieve_count_write(lo, hi, limit, fp, &written);
        double tw1 = now_sec();
        fclose(fp);
        remove(path);
        row.sieve_write_time = tw1 - tw0;
        row.sieve_write_time_valid = true;
    }

    free(sieve_buf);
    return row;
}
