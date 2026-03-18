/**
 * cpss_bench_query.c - Query benchmark suite: PRNG, latency stats, 5 benchmark commands
 * Part of the CPSS Viewer amalgamation.
 */

typedef struct {
    size_t hot_count, hot_bytes, wheel_count, wheel_bytes;
} CacheSnapshot;

typedef struct {
    size_t wheel_budget, hot_budget;
    bool was_readonly;
} SavedCacheBudgets;

static uint64_t rand_range(uint64_t* state, uint64_t lo, uint64_t hi);
static CacheSnapshot snap_cache(void);
static void print_cache_delta(const CacheSnapshot* before, const CacheSnapshot* after);
static SavedCacheBudgets bench_disable_cache_fill(void);
static void bench_restore_cache_fill(const SavedCacheBudgets* saved);
static void bench_random_db_prime_pair(CPSSStream* s, uint64_t factor_max,
    uint64_t* state, uint64_t* p_out, uint64_t* q_out);
static void bench_worker_pminus1(void* shared_ptr, size_t index);
static void bench_worker_pplus1(void* shared_ptr, size_t index);

typedef void (*BenchParallelFn)(void* shared, size_t index);

typedef struct {
    volatile LONG next_index;
    size_t count;
    BenchParallelFn fn;
    void* shared;
} BenchParallelPool;

typedef enum {
    BENCH_FACTOR_CASE_U64 = 0,
    BENCH_FACTOR_CASE_U128,
    BENCH_FACTOR_CASE_BN
} BenchFactorCaseKind;

typedef struct {
    BenchFactorCaseKind kind;
    uint64_t n64;
    U128 n128;
    BigNum nbn;
    uint64_t seed;
} BenchFactorCase;

typedef struct {
    bool ok;
    bool partial;
    double elapsed_sec;
    const char* method;
    int status;
} BenchFactorOutcome;

typedef struct {
    const BenchFactorCase* cases;
    BenchFactorOutcome* outcomes;
    CPSSStream* stream;
    uint64_t param1;
    uint64_t param2;
} BenchFactorShared;

typedef struct {
    size_t succeeded;
    size_t failed;
    double total_success_sec;
    double total_fail_sec;
    double fastest_success;
    double slowest_success;
    double fastest_fail;
    double slowest_fail;
} FactorBenchSummary;

typedef struct {
    const char* name;
    size_t success;
    size_t partial;
    size_t failed;
    double total_success_sec;
} BenchAutoMethodStats;

typedef struct {
    uint64_t n;
} BenchU64Case;

typedef struct {
    bool fully_factored;
    double latency_us;
} BenchU64Outcome;

typedef struct {
    const BenchU64Case* cases;
    BenchU64Outcome* outcomes;
    CPSSStream* stream;
} BenchU64Shared;

static uint32_t bench_effective_threads(size_t count, uint32_t requested) {
    uint32_t effective = requested > 0u ? requested : 1u;
    if (effective > MAXIMUM_WAIT_OBJECTS) effective = MAXIMUM_WAIT_OBJECTS;
    if (count <= 1u) return 1u;
    if ((size_t)effective > count) effective = (uint32_t)count;
    return effective;
}

static DWORD WINAPI bench_parallel_worker(LPVOID param) {
    BenchParallelPool* pool = (BenchParallelPool*)param;
    for (;;) {
        LONG idx = InterlockedIncrement(&pool->next_index) - 1;
        if (idx < 0 || (size_t)idx >= pool->count) break;
        pool->fn(pool->shared, (size_t)idx);
    }
    return 0;
}

static void bench_run_parallel(size_t count, uint32_t requested_threads,
    BenchParallelFn fn, void* shared) {
    uint32_t n_threads = bench_effective_threads(count, requested_threads);
    if (n_threads <= 1u) {
        for (size_t i = 0u; i < count; ++i) fn(shared, i);
        return;
    }

    BenchParallelPool pool;
    pool.next_index = 0;
    pool.count = count;
    pool.fn = fn;
    pool.shared = shared;

    HANDLE* threads = (HANDLE*)xmalloc((size_t)n_threads * sizeof(HANDLE));
    for (uint32_t ti = 0u; ti < n_threads; ++ti) {
        threads[ti] = CreateThread(NULL, 0, bench_parallel_worker, &pool, 0, NULL);
        if (!threads[ti]) die_win32("CreateThread failed for benchmark worker");
    }
    WaitForMultipleObjects(n_threads, threads, TRUE, INFINITE);
    for (uint32_t ti = 0u; ti < n_threads; ++ti) CloseHandle(threads[ti]);
    free(threads);
}

static FactorBenchSummary summarize_factor_bench(const BenchFactorOutcome* outcomes, size_t count) {
    FactorBenchSummary summary;
    memset(&summary, 0, sizeof(summary));
    summary.fastest_success = 1e30;
    summary.fastest_fail = 1e30;

    for (size_t i = 0u; i < count; ++i) {
        double elapsed = outcomes[i].elapsed_sec;
        if (outcomes[i].ok) {
            ++summary.succeeded;
            summary.total_success_sec += elapsed;
            if (elapsed < summary.fastest_success) summary.fastest_success = elapsed;
            if (elapsed > summary.slowest_success) summary.slowest_success = elapsed;
        }
        else {
            ++summary.failed;
            summary.total_fail_sec += elapsed;
            if (elapsed < summary.fastest_fail) summary.fastest_fail = elapsed;
            if (elapsed > summary.slowest_fail) summary.slowest_fail = elapsed;
        }
    }

    return summary;
}

static void print_factor_bench_summary(size_t count, const FactorBenchSummary* summary, double total_gen_sec) {
    printf("=== results ===\n");
    printf("  cases              = %zu\n", count);
    printf("  succeeded          = %zu (%.1f%%)\n",
        summary->succeeded, count > 0u ? 100.0 * (double)summary->succeeded / (double)count : 0.0);
    printf("  failed             = %zu (%.1f%%)\n",
        summary->failed, count > 0u ? 100.0 * (double)summary->failed / (double)count : 0.0);
    if (summary->succeeded > 0u) {
        printf("  success avg time   = %.6fs\n", summary->total_success_sec / (double)summary->succeeded);
        printf("  success fastest    = %.6fs\n", summary->fastest_success);
        printf("  success slowest    = %.6fs\n", summary->slowest_success);
        printf("  success total      = %.3fs\n", summary->total_success_sec);
    }
    if (summary->failed > 0u) {
        printf("  fail avg time      = %.6fs\n", summary->total_fail_sec / (double)summary->failed);
        printf("  fail fastest       = %.6fs\n", summary->fastest_fail);
        printf("  fail slowest       = %.6fs\n", summary->slowest_fail);
        printf("  fail total         = %.3fs\n", summary->total_fail_sec);
    }
    printf("  total factor time  = %.3fs\n", summary->total_success_sec + summary->total_fail_sec);
    if (total_gen_sec > 0.0) {
        printf("  total gen time     = %.3fs (avg %.3fs/case)\n", total_gen_sec, total_gen_sec / (double)count);
    }
}

/* ── benchmark-pplus1 ─────────────────────────────────────────────── */

static void cmd_bench_pplus1(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 50u;
    unsigned bits = 40u;
    uint64_t seed = 42u;
    uint64_t B1 = 0u;
    const char* cache_mode = "normal";
    uint32_t threads = g_app_threads;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) count = (size_t)parse_u64(argv[++i], "count");
        else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) bits = (unsigned)parse_u64(argv[++i], "bits");
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = parse_u64(argv[++i], "seed");
        else if (strcmp(argv[i], "--B1") == 0 && i + 1 < argc) B1 = parse_u64(argv[++i], "B1");
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) cache_mode = argv[++i];
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) threads = (uint32_t)parse_u64(argv[++i], "threads");
    }

    if (bits < 4u) bits = 4u;
    if (bits > (unsigned)(BN_MAX_LIMBS * 64)) bits = (unsigned)(BN_MAX_LIMBS * 64);

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }

    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    (void)constrain_lo;
    unsigned half_bits = bits / 2u;
    if (half_bits < 2u) half_bits = 2u;

    uint64_t db_bits = 0u;
    { uint64_t v = db_hi; while (v > 0u) { ++db_bits; v >>= 1u; } }
    bool use_random_primes = (half_bits > db_bits);
    bool use_bignum = (bits > 128u);

    uint64_t factor_max = 0u;
    if (!use_random_primes) {
        factor_max = (half_bits >= 64u) ? db_hi : ((1ull << half_bits) - 1u);
        if (factor_max > db_hi) factor_max = db_hi;
        if (factor_max < 3u) factor_max = 3u;
    }

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);
    bool forced_no_fill = false;
    threads = bench_effective_threads(count, threads);
    if (threads > 1u && !no_fill) {
        forced_no_fill = true;
        no_fill = true;
    }

    printf("=== benchmark-pplus1 ===\n");
    printf("  count=%zu  bits=%u  half_bits=%u  seed=%" PRIu64 "\n", count, bits, half_bits, seed);
    if (use_random_primes) {
        printf("  prime source       = random (Miller-Rabin, %u-bit factors)\n", half_bits);
    }
    else {
        printf("  prime source       = DB (factor_max=%" PRIu64 ")\n", factor_max);
    }
    if (B1 > 0u) printf("  B1                 = %" PRIu64 "\n", B1);
    else printf("  B1                 = default (min(1M, db_hi))\n");
    printf("  engine             = %s\n", use_bignum ? "BigNum (Montgomery)" : "U128");
    printf("  threads            = %u\n", threads);
    printf("  cache-mode         = %s\n", no_fill ? "no-fill" : cache_mode);
    if (forced_no_fill) {
        printf("  note               = threaded runs force cache no-fill/read-only\n");
    }
    printf("\n");

    if (use_random_primes) {
        printf("  generating %zu random %u-bit semiprimes...\n", count, bits);
    }

    SavedCacheBudgets saved = { 0 };
    if (no_fill) saved = bench_disable_cache_fill();
    CacheSnapshot snap0 = snap_cache();

    uint64_t rng = seed;
    double total_gen_sec = 0.0;
    BenchFactorCase* cases = (BenchFactorCase*)xmalloc(count * sizeof(BenchFactorCase));
    BenchFactorOutcome* outcomes = (BenchFactorOutcome*)xmalloc(count * sizeof(BenchFactorOutcome));

    for (size_t ci = 0u; ci < count; ++ci) {
        if (use_random_primes) {
            BigNum bn_p, bn_q;
            double gen0 = now_sec();
            cases[ci].kind = BENCH_FACTOR_CASE_BN;
            bn_random_prime(&bn_p, half_bits, &rng);
            bn_random_prime(&bn_q, half_bits, &rng);
            bn_mul(&cases[ci].nbn, &bn_p, &bn_q);
            {
                double gen_elapsed = now_sec() - gen0;
                total_gen_sec += gen_elapsed;
                if (ci == 0u) {
                    printf("  first case: p=%u-bit q=%u-bit n=%u-bit (gen=%.3fs)\n",
                        bn_bitlen(&bn_p), bn_bitlen(&bn_q), bn_bitlen(&cases[ci].nbn), gen_elapsed);
                }
            }
        }
        else if (use_bignum) {
            uint64_t p, q;
            BigNum bn_p, bn_q;
            bench_random_db_prime_pair(s, factor_max, &rng, &p, &q);
            cases[ci].kind = BENCH_FACTOR_CASE_BN;
            bn_from_u64(&bn_p, p);
            bn_from_u64(&bn_q, q);
            bn_mul(&cases[ci].nbn, &bn_p, &bn_q);
            if (ci == 0u) {
                printf("  first case: p=%" PRIu64 " q=%" PRIu64 " n=%u-bit\n",
                    p, q, bn_bitlen(&cases[ci].nbn));
            }
        }
        else {
            uint64_t p, q;
            bench_random_db_prime_pair(s, factor_max, &rng, &p, &q);
            cases[ci].kind = BENCH_FACTOR_CASE_U128;
            cases[ci].n128 = u128_mul64(p, q);
            if (ci == 0u) {
                char nbuf[64];
                u128_to_str(cases[ci].n128, nbuf, sizeof(nbuf));
                printf("  first case: p=%" PRIu64 " q=%" PRIu64 " n=%s\n", p, q, nbuf);
            }
        }
    }

    {
        BenchFactorShared shared;
        shared.cases = cases;
        shared.outcomes = outcomes;
        shared.stream = s;
        shared.param1 = B1;
        shared.param2 = 0u;
        bench_run_parallel(count, threads, bench_worker_pplus1, &shared);
    }

    {
        FactorBenchSummary summary = summarize_factor_bench(outcomes, count);
        print_factor_bench_summary(count, &summary, total_gen_sec);
    }

    if (no_fill) bench_restore_cache_fill(&saved);
    CacheSnapshot snap1 = snap_cache();
    print_cache_delta(&snap0, &snap1);
    free(outcomes);
    free(cases);
}

/** CLI handler: benchmark every segment and produce optional CSV output. */
static int cmd_benchmark_all_sectors(int argc, char** argv) {
    if (argc < 3) {
        print_usage(stderr);
        return 1;
    }

    int repeats = 1;
    uint64_t every = 1u;
    uint64_t limit = 0u;
    uint64_t start_seg = 0u;
    uint64_t end_seg = 0u;
    bool end_seg_given = false;
    bool include_write = false;
    const char* csv_out = NULL;
    BenchmarkMode mode = BENCH_AUTO;

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--start-seg") == 0 && i + 1 < argc) {
            start_seg = parse_u64(argv[++i], "start-seg");
        }
        else if (strcmp(argv[i], "--end-seg") == 0 && i + 1 < argc) {
            end_seg = parse_u64(argv[++i], "end-seg");
            end_seg_given = true;
        }
        else if (strcmp(argv[i], "--every") == 0 && i + 1 < argc) {
            every = parse_u64(argv[++i], "every");
            if (every == 0u) die("every must be >= 1");
        }
        else if (strcmp(argv[i], "--repeats") == 0 && i + 1 < argc) {
            repeats = parse_int(argv[++i], "repeats");
            if (repeats < 1) die("repeats must be >= 1");
        }
        else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = parse_u64(argv[++i], "limit");
        }
        else if (strcmp(argv[i], "--include-write") == 0) {
            include_write = true;
        }
        else if (strcmp(argv[i], "--csv-out") == 0 && i + 1 < argc) {
            csv_out = argv[++i];
        }
        else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = parse_benchmark_mode(argv[++i]);
        }
        else {
            print_usage(stderr);
            return 1;
        }
    }

    CPSSStream cpss;
    cpss_open(&cpss, argv[2]);
    cpss_build_segment_index(&cpss);

    /* Pre-scan: count jobs and find max end_n for base prime pre-warming */
    size_t jobs = 0u;
    uint64_t max_end_n = 0u;
    uint64_t decomp_included_count = 0u;
    uint64_t decomp_excluded_count = 0u;
    uint64_t cache_hit_count = 0u;
    uint64_t cache_miss_count = 0u;
    uint64_t cache_too_large_count = 0u;

    for (size_t i = 0u; i < cpss.index_len; ++i) {
        uint64_t seg = cpss.index_entries[i].segment_no;
        if (seg < start_seg) continue;
        if (end_seg_given && seg > end_seg) break;
        if (((seg - start_seg) % every) != 0u) continue;
        ++jobs;
        uint64_t en = segment_end_n(&cpss.index_entries[i]);
        if (en > max_end_n) max_end_n = en;
    }

    if (jobs == 0u) {
        printf("No matching sectors to benchmark.\n");
        cpss_close(&cpss);
        return 0;
    }

    /* Pre-warm global base prime cache once for all sectors */
    {
        uint32_t max_root = isqrt_u64(max_end_n);
        ensure_base_primes_cached(max_root);
        printf("base primes cached   = up to %" PRIu32 " (%" PRIu64 " primes)\n",
            g_cached_base_primes_limit, (uint64_t)g_cached_base_primes.len);
    }

    printf("jobs to run          = %zu\n", jobs);
    printf("repeats              = %d (+ 1 warmup)\n", repeats);
    printf("benchmark mode       = %s\n", benchmark_mode_name(mode));
    if (g_app_wheel_cache_bytes > 0) {
        char cb[64];
        printf("wheel cache budget   = %s\n", human_size((uint64_t)g_app_wheel_cache_bytes, cb, sizeof(cb)));
    }
    else if (mode == BENCH_HOT) {
        printf("wheel cache budget   = 0 (no cache - hot mode will include prep)\n");
    }
    printf("every                = %" PRIu64 "\n", every);
    printf("start_seg            = %" PRIu64 "\n", start_seg);
    if (end_seg_given) {
        printf("end_seg              = %" PRIu64 "\n", end_seg);
    }
    else {
        printf("end_seg              = end\n");
    }
    if (limit) {
        printf("prime export limit   = %" PRIu64 "\n", limit);
    }

    FILE* csv = NULL;
    if (csv_out) {
        csv = fopen(csv_out, "w");
        if (!csv) {
            cpss_close(&cpss);
            die_errno("failed to open csv output");
        }
        fprintf(csv, "segment_no,start_n,end_n,candidates,survivors,stream_prime_count,"
            "compressed_size,raw_size,expansion_ratio,"
            "benchmark_mode,decomp_included,prep_included,"
            "cache_hit,cache_fits,presieve_bytes,"
            "repeats,limit");
        for (int i = 1; i <= repeats; ++i) {
            fprintf(csv, ",cpss_total_run_%d,sieve_total_run_%d", i, i);
        }
        fprintf(csv, ",cpss_decompress_median,cpss_prep_median,cpss_query_median,"
            "cpss_total_median,sieve_total_median,"
            "winner,speedup,count_match\n");
    }

    size_t completed = 0u;
    uint64_t cpss_wins = 0u;
    uint64_t sieve_wins = 0u;
    uint64_t ties = 0u;
    double total_cpss_median = 0.0;
    double total_sieve_median = 0.0;

    for (size_t i = 0u; i < cpss.index_len; ++i) {
        uint64_t seg = cpss.index_entries[i].segment_no;
        if (seg < start_seg) continue;
        if (end_seg_given && seg > end_seg) break;
        if (((seg - start_seg) % every) != 0u) continue;

        BenchmarkSectorResult r = benchmark_sector_fair(&cpss, i, repeats, limit, include_write, mode);
        ++completed;

        if (r.decomp_included) ++decomp_included_count;
        else ++decomp_excluded_count;

        /* Track cache stats for hot mode */
        if (mode == BENCH_HOT) {
            if (r.cache_hit) ++cache_hit_count;
            else if (!r.cache_fits) ++cache_too_large_count;
            else ++cache_miss_count;
        }

        printf(
            "[%zu/%zu] seg=%" PRIu64 " | n=%" PRIu64 "..%" PRIu64
            " | z=%" PRIu32 " raw=%zu ratio=%.2fx"
            " | mode=%s decomp=%s prep=%s\n",
            completed, jobs, r.segment_no, r.start_n, r.end_n,
            r.compressed_size, r.raw_size, r.expansion_ratio,
            r.mode_name,
            r.decomp_included ? "YES" : "NO",
            r.prep_included ? "YES" : "NO"
        );
        if (mode == BENCH_HOT) {
            printf(
                "      wheel-cache: hit=%s size=%zu bytes fits=%s\n",
                r.cache_hit ? "YES" : "NO",
                r.presieve_bytes,
                r.cache_fits ? "YES" : "NO"
            );
        }
        printf(
            "      cpss: decomp=%.6fs prep=%.6fs query=%.6fs total=%.6fs\n",
            r.cpss_decompress_median, r.cpss_prep_median, r.cpss_query_median, r.cpss_total_median
        );
        printf(
            "      sieve: total=%.6fs | winner=%s speedup=%.3fx count_match=%s\n",
            r.sieve_total_median, r.winner, r.speedup,
            r.count_match ? "true" : "false"
        );

        if (strcmp(r.winner, "cpss") == 0) ++cpss_wins;
        else if (strcmp(r.winner, "sieve") == 0) ++sieve_wins;
        else ++ties;

        total_cpss_median += r.cpss_total_median;
        total_sieve_median += r.sieve_total_median;

        if (csv) {
            fprintf(csv, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu32 ",%" PRIu32 ",%" PRIu64 ","
                "%" PRIu32 ",%zu,%.6f,"
                "%s,%s,%s,"
                "%s,%s,%zu,"
                "%d,%" PRIu64,
                r.segment_no, r.start_n, r.end_n, r.candidates, r.survivors, r.stream_prime_count,
                r.compressed_size, r.raw_size, r.expansion_ratio,
                r.mode_name,
                r.decomp_included ? "true" : "false",
                r.prep_included ? "true" : "false",
                r.cache_hit ? "true" : "false",
                r.cache_fits ? "true" : "false",
                r.presieve_bytes,
                r.repeats, r.limit);
            for (int j = 0; j < repeats; ++j) {
                fprintf(csv, ",%.9f,%.9f", r.cpss_total_times[j], r.sieve_total_times[j]);
            }
            fprintf(csv, ",%.9f,%.9f,%.9f,%.9f,%.9f,%s,%.9f,%s\n",
                r.cpss_decompress_median, r.cpss_prep_median, r.cpss_query_median,
                r.cpss_total_median, r.sieve_total_median,
                r.winner, r.speedup, r.count_match ? "true" : "false");
        }

        benchmark_sector_result_free(&r);
    }

    printf("\n");
    printf("=== SUMMARY ===\n");
    printf("segments tested      = %zu\n", completed);
    printf("benchmark mode       = %s\n", benchmark_mode_name(mode));
    printf("cpss wins            = %" PRIu64 "\n", cpss_wins);
    printf("sieve wins           = %" PRIu64 "\n", sieve_wins);
    printf("ties                 = %" PRIu64 "\n", ties);
    printf("sum median cpss      = %.4fs\n", total_cpss_median);
    printf("sum median sieve     = %.4fs\n", total_sieve_median);

    if (completed > 0u && total_cpss_median > 0.0 && total_sieve_median > 0.0) {
        if (total_cpss_median < total_sieve_median) {
            printf("overall verdict      = CPSS is %.3fx faster by summed medians\n", total_sieve_median / total_cpss_median);
        }
        else {
            printf("overall verdict      = sieve is %.3fx faster by summed medians\n", total_cpss_median / total_sieve_median);
        }
    }

    printf("\n1.2x decompression rule applied per sector:\n");
    printf("  decomp included    = %" PRIu64 " sectors (raw_size >= 1.2 * compressed_size)\n", decomp_included_count);
    printf("  decomp excluded    = %" PRIu64 " sectors (raw_size < 1.2 * compressed_size, kept decompressed)\n", decomp_excluded_count);
    if (mode == BENCH_HOT) {
        char cb[64];
        printf("\nWheel cache summary:\n");
        printf("  budget             = %s\n",
            g_app_wheel_cache_bytes > 0
            ? human_size((uint64_t)g_app_wheel_cache_bytes, cb, sizeof(cb))
            : "0 (disabled)");
        printf("  cache hits         = %" PRIu64 "\n", cache_hit_count);
        printf("  cache misses       = %" PRIu64 "\n", cache_miss_count);
        printf("  too large to cache = %" PRIu64 "\n", cache_too_large_count);
        printf("  final bytes used   = %zu\n", wheel_cache_bytes_used());
        printf("  final entry count  = %zu\n", wheel_cache_entry_count());
        printf("  NOTE: hot mode - prep excluded only for sectors whose presieve was cache-resident.\n");
    }
    else {
        printf("  NOTE: %s mode - per-segment presieve build included for all sectors.\n", benchmark_mode_name(mode));
    }

    if (csv) {
        fclose(csv);
        printf("csv written to       = %s\n", csv_out);
    }
    cpss_close(&cpss);
    (void)include_write;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * QUERY BENCHMARK SUITE
 *
 * Realistic workload benchmarks for the CPSS query engine.
 * Most benchmarks operate on an already-loaded CPSSStream via APP mode.
 * benchmark-fermat, benchmark-rho, and benchmark-ecm are the explicit
 * DB-independent exceptions.
 * ══════════════════════════════════════════════════════════════════════ */

 /* ── PRNG (xorshift64, deterministic) ──────────────────────────────── */

static uint64_t xorshift64(uint64_t* state) {
    uint64_t x = *state;
    x ^= x << 13u;
    x ^= x >> 7u;
    x ^= x << 17u;
    *state = x;
    return x;
}

static uint64_t rand_range(uint64_t* state, uint64_t lo, uint64_t hi) {
    if (lo >= hi) return lo;
    uint64_t range = hi - lo + 1u;
    return lo + xorshift64(state) % range;
}

static uint64_t rand_odd_bits_u64(uint64_t* state, unsigned bits) {
    if (bits <= 1u) return 1u;
    if (bits >= 64u) {
        return xorshift64(state) | (1ull << 63u) | 1u;
    }

    uint64_t lo = ((uint64_t)1u << (bits - 1u)) | 1u;
    uint64_t hi = ((uint64_t)1u << bits) - 1u;
    uint64_t v = rand_range(state, lo, hi);
    v |= ((uint64_t)1u << (bits - 1u));
    v |= 1u;
    return v;
}

static uint64_t rand_bits_u64(uint64_t* state, unsigned bits) {
    if (bits == 0u) return 0u;
    if (bits >= 64u) {
        return xorshift64(state) | (1ull << 63u);
    }

    {
        uint64_t mask = ((uint64_t)1u << bits) - 1u;
        uint64_t v = xorshift64(state) & mask;
        v |= (uint64_t)1u << (bits - 1u);
        return v;
    }
}

static unsigned u64_bitlen(uint64_t n) {
    unsigned bits = 0u;
    while (n > 0u) {
        ++bits;
        n >>= 1u;
    }
    return bits;
}

static uint64_t fermat_even_delta(uint64_t* state, uint64_t delta_max) {
    if (delta_max < 2u) return 2u;
    uint64_t delta = rand_range(state, 1u, delta_max);
    if (delta & 1u) {
        if (delta < delta_max) ++delta;
        else --delta;
    }
    if (delta < 2u) delta = 2u;
    return delta;
}

static uint64_t random_prime_u64_bits(uint64_t* state, unsigned bits) {
    if (bits < 2u) bits = 2u;
    for (;;) {
        uint64_t candidate = rand_odd_bits_u64(state, bits);
        if (miller_rabin_u64(candidate)) return candidate;
    }
}

static void bench_random_semiprime_bn(BigNum* p, BigNum* q, BigNum* n,
    unsigned p_bits, unsigned q_bits, uint64_t* state) {
    if (p_bits < 2u) p_bits = 2u;
    if (q_bits < 2u) q_bits = 2u;
    bn_random_prime(p, p_bits, state);
    bn_random_prime(q, q_bits, state);
    bn_mul(n, p, q);
}

static void bench_random_bits_bn_exact(BigNum* a, unsigned bits, uint64_t* state, bool odd) {
    bn_random_bits(a, bits, state);
    if (!odd && (xorshift64(state) & 1u) == 0u) {
        a->limbs[0] &= ~1ull;
        bn_normalize(a);
    }
}

static void bench_factor_case_from_bn(BenchFactorCase* out, const BigNum* n) {
    if (bn_fits_u64(n)) {
        out->kind = BENCH_FACTOR_CASE_U64;
        out->n64 = bn_to_u64(n);
    }
    else if (bn_fits_u128(n)) {
        out->kind = BENCH_FACTOR_CASE_U128;
        out->n128 = bn_to_u128(n);
    }
    else {
        out->kind = BENCH_FACTOR_CASE_BN;
        bn_copy(&out->nbn, n);
    }
}

static size_t bench_factor_auto_method_bucket(const char* method) {
    static const char* const names[] = {
        "auto(trivial)",
        "auto(prime)",
        "auto(wheel)",
        "auto(pminus1)",
        "auto(pplus1)",
        "auto(rho)",
        "auto(trial)",
        "auto(ecm)",
        "auto",
        "auto(unknown)"
    };

    if (method != NULL) {
        for (size_t i = 0u; i + 1u < ARRAY_LEN(names); ++i) {
            if (strcmp(method, names[i]) == 0) return i;
        }
    }
    return ARRAY_LEN(names) - 1u;
}

static void print_bench_factor_auto_breakdown(const BenchFactorOutcome* outcomes, size_t count) {
    static const char* const names[] = {
        "auto(trivial)",
        "auto(prime)",
        "auto(wheel)",
        "auto(pminus1)",
        "auto(pplus1)",
        "auto(rho)",
        "auto(trial)",
        "auto(ecm)",
        "auto",
        "auto(unknown)"
    };
    BenchAutoMethodStats stats[ARRAY_LEN(names)];
    memset(stats, 0, sizeof(stats));

    for (size_t i = 0u; i < ARRAY_LEN(names); ++i) {
        stats[i].name = names[i];
    }

    for (size_t i = 0u; i < count; ++i) {
        size_t bucket = bench_factor_auto_method_bucket(outcomes[i].method);
        if (outcomes[i].ok) {
            ++stats[bucket].success;
            stats[bucket].total_success_sec += outcomes[i].elapsed_sec;
        }
        else if (outcomes[i].partial) {
            ++stats[bucket].partial;
        }
        else {
            ++stats[bucket].failed;
        }
    }

    printf("  method breakdown:\n");
    for (size_t i = 0u; i < ARRAY_LEN(stats); ++i) {
        size_t total = stats[i].success + stats[i].partial + stats[i].failed;
        if (total == 0u) continue;
        printf("    %-16s success=%zu partial=%zu failed=%zu",
            stats[i].name, stats[i].success, stats[i].partial, stats[i].failed);
        if (stats[i].success > 0u) {
            printf(" avg=%.6fs", stats[i].total_success_sec / (double)stats[i].success);
        }
        printf("\n");
    }
}

static const char* bench_semiprime_shape_name(const char* shape) {
    if (strcmp(shape, "balanced") == 0) return "balanced";
    if (strcmp(shape, "skewed") == 0) return "skewed";
    die("benchmark shape must be 'balanced' or 'skewed'");
    return "balanced";
}

static unsigned bench_skewed_factor_bits(unsigned bits) {
    unsigned factor_bits = bits / 3u;
    if (factor_bits < 16u) factor_bits = 16u;
    if (factor_bits > 32u) factor_bits = 32u;
    if (factor_bits >= bits) factor_bits = bits / 2u;
    if (factor_bits < 2u) factor_bits = 2u;
    return factor_bits;
}

static void bench_semiprime_shape_bits(unsigned bits, const char* shape,
    unsigned* p_bits, unsigned* q_bits) {
    unsigned left_bits = 0u;
    unsigned right_bits = 0u;

    if (strcmp(shape, "skewed") == 0) {
        left_bits = bench_skewed_factor_bits(bits);
        right_bits = bits > left_bits ? bits - left_bits : left_bits;
    }
    else {
        left_bits = bits / 2u;
        right_bits = bits > left_bits ? bits - left_bits : left_bits;
    }

    if (left_bits < 2u) left_bits = 2u;
    if (right_bits < 2u) right_bits = 2u;
    *p_bits = left_bits;
    *q_bits = right_bits;
}

static void bench_random_db_prime_pair(CPSSStream* s, uint64_t factor_max,
    uint64_t* state, uint64_t* p_out, uint64_t* q_out) {
    uint64_t p = 0u;
    uint64_t q = 0u;

    if (factor_max < 3u) die("benchmark factor_max must be >= 3");

    for (unsigned tries = 0u; tries < 32u; ++tries) {
        p = cpss_next_prime(s, rand_range(state, 3u, factor_max));
        if (p != 0u && p <= factor_max) break;
    }
    if (p == 0u || p > factor_max) p = cpss_next_prime(s, 2u);
    if (p == 0u || p > factor_max) die("DB prime coverage is insufficient for requested benchmark bits");

    for (unsigned tries = 0u; tries < 32u; ++tries) {
        q = cpss_next_prime(s, rand_range(state, 3u, factor_max));
        if (q != 0u && q <= factor_max && q != p) break;
    }
    if (q == 0u || q > factor_max || q == p) {
        if (p < factor_max) q = cpss_next_prime(s, p + 1u);
        if (q == 0u || q > factor_max || q == p) {
            q = (p > 3u) ? cpss_prev_prime(s, p - 1u) : 0u;
        }
        if (q == 0u || q > factor_max) q = p;
    }

    *p_out = p;
    *q_out = q;
}

static uint64_t bench_random_composite_u64(unsigned bits, uint64_t* state) {
    uint64_t lo = (uint64_t)1u << (bits - 1u);
    uint64_t hi = ((uint64_t)1u << bits) - 1u;

    for (;;) {
        uint64_t n = rand_range(state, lo, hi) | 1u;
        if (!miller_rabin_u64(n)) return n;
        if (n <= hi / 3u) return n * 3u;
        if (n > lo + 2u && !miller_rabin_u64(n - 2u)) return n - 2u;
    }
}

static void bench_worker_fermat(void* shared_ptr, size_t index) {
    BenchFactorShared* shared = (BenchFactorShared*)shared_ptr;
    const BenchFactorCase* bc = &shared->cases[index];
    BenchFactorOutcome* outcome = &shared->outcomes[index];
    memset(outcome, 0, sizeof(*outcome));

    if (bc->kind == BENCH_FACTOR_CASE_U64) {
        FactorResult fr = cpss_factor_fermat(bc->n64, shared->param1);
        outcome->ok = fr.fully_factored || fr.factor_count > 0u;
        outcome->partial = (!fr.fully_factored && fr.factor_count > 0u);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
    else if (bc->kind == BENCH_FACTOR_CASE_U128) {
        FactorResultU128 fr = cpss_factor_fermat_u128(bc->n128, shared->param1);
        outcome->ok = fr.fully_factored || fr.factor_count > 0u;
        outcome->partial = (!fr.fully_factored && fr.factor_count > 0u);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
    else {
        FactorResultBigNum fr = cpss_factor_fermat_bn(&bc->nbn, shared->param1);
        outcome->ok = fr.factor_found || fr.fully_factored;
        outcome->partial = (!fr.fully_factored && fr.factor_found);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
}

static void bench_worker_rho(void* shared_ptr, size_t index) {
    BenchFactorShared* shared = (BenchFactorShared*)shared_ptr;
    const BenchFactorCase* bc = &shared->cases[index];
    BenchFactorOutcome* outcome = &shared->outcomes[index];
    memset(outcome, 0, sizeof(*outcome));

    if (bc->kind == BENCH_FACTOR_CASE_U64) {
        FactorResult fr = cpss_factor_rho(bc->n64, shared->param1, bc->seed);
        outcome->ok = fr.fully_factored || fr.factor_count > 0u;
        outcome->partial = (!fr.fully_factored && fr.factor_count > 0u);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
    else {
        FactorResultBigNum fr = cpss_factor_rho_bn(&bc->nbn, shared->param1, bc->seed);
        outcome->ok = fr.factor_found || fr.fully_factored;
        outcome->partial = (!fr.fully_factored && fr.factor_found);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
}

static void bench_worker_ecm(void* shared_ptr, size_t index) {
    BenchFactorShared* shared = (BenchFactorShared*)shared_ptr;
    const BenchFactorCase* bc = &shared->cases[index];
    BenchFactorOutcome* outcome = &shared->outcomes[index];
    memset(outcome, 0, sizeof(*outcome));
    FactorResultBigNum fr = cpss_factor_ecm_bn(&bc->nbn, shared->param1, shared->param2, bc->seed);
    outcome->ok = fr.factor_found || fr.fully_factored;
    outcome->partial = (!fr.fully_factored && fr.factor_found);
    outcome->elapsed_sec = fr.time_seconds;
    outcome->method = fr.method;
    outcome->status = fr.status;
}

static void bench_worker_pminus1(void* shared_ptr, size_t index) {
    BenchFactorShared* shared = (BenchFactorShared*)shared_ptr;
    const BenchFactorCase* bc = &shared->cases[index];
    BenchFactorOutcome* outcome = &shared->outcomes[index];
    memset(outcome, 0, sizeof(*outcome));

    if (bc->kind == BENCH_FACTOR_CASE_U128) {
        FactorResultU128 fr = cpss_factor_pminus1_u128(shared->stream, bc->n128, shared->param1);
        outcome->ok = fr.fully_factored;
        outcome->partial = (!fr.fully_factored && fr.factor_count > 0u);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
    else {
        FactorResultBigNum fr = cpss_factor_pminus1_bn(shared->stream, &bc->nbn, shared->param1);
        outcome->ok = fr.factor_found;
        outcome->partial = (!fr.fully_factored && fr.factor_found);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
}

static void bench_worker_pplus1(void* shared_ptr, size_t index) {
    BenchFactorShared* shared = (BenchFactorShared*)shared_ptr;
    const BenchFactorCase* bc = &shared->cases[index];
    BenchFactorOutcome* outcome = &shared->outcomes[index];
    memset(outcome, 0, sizeof(*outcome));

    if (bc->kind == BENCH_FACTOR_CASE_U128) {
        FactorResultU128 fr = cpss_factor_pplus1_u128(shared->stream, bc->n128, shared->param1);
        outcome->ok = fr.fully_factored;
        outcome->partial = (!fr.fully_factored && fr.factor_count > 0u);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
    else {
        FactorResultBigNum fr = cpss_factor_pplus1_bn(shared->stream, &bc->nbn, shared->param1);
        outcome->ok = fr.factor_found;
        outcome->partial = (!fr.fully_factored && fr.factor_found);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
}

static void bench_worker_factor_trial(void* shared_ptr, size_t index) {
    BenchU64Shared* shared = (BenchU64Shared*)shared_ptr;
    BenchU64Outcome* outcome = &shared->outcomes[index];
    FactorResult fr = cpss_factor_trial(shared->stream, shared->cases[index].n, 0u);
    outcome->fully_factored = fr.fully_factored;
    outcome->latency_us = fr.time_seconds * 1e6;
}

static void bench_worker_factor_auto(void* shared_ptr, size_t index) {
    BenchFactorShared* shared = (BenchFactorShared*)shared_ptr;
    const BenchFactorCase* bc = &shared->cases[index];
    BenchFactorOutcome* outcome = &shared->outcomes[index];
    memset(outcome, 0, sizeof(*outcome));

    if (bc->kind == BENCH_FACTOR_CASE_U64) {
        FactorResult fr = cpss_factor_auto(shared->stream, bc->n64);
        outcome->ok = fr.fully_factored;
        outcome->partial = (!fr.fully_factored && fr.factor_count > 0u);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
    else if (bc->kind == BENCH_FACTOR_CASE_U128) {
        FactorResultU128 fr = cpss_factor_auto_u128(shared->stream, bc->n128);
        outcome->ok = fr.fully_factored;
        outcome->partial = (!fr.fully_factored && fr.factor_count > 0u);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
    else {
        FactorResultBigNum fr = cpss_factor_auto_bn(shared->stream, &bc->nbn);
        outcome->ok = fr.fully_factored;
        outcome->partial = (!fr.fully_factored && fr.factor_found);
        outcome->elapsed_sec = fr.time_seconds;
        outcome->method = fr.method;
        outcome->status = fr.status;
    }
}

static void cmd_bench_rho(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 50u;
    unsigned bits = 64u;
    uint64_t seed = 42u;
    uint64_t max_iterations = 0u;
    const char* shape = "balanced";
    uint32_t threads = g_app_threads;

    (void)s;
    (void)constrain_lo;
    (void)constrain_hi;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) count = (size_t)parse_u64(argv[++i], "count");
        else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) bits = (unsigned)parse_u64(argv[++i], "bits");
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = parse_u64(argv[++i], "seed");
        else if (strcmp(argv[i], "--max-iterations") == 0 && i + 1 < argc) max_iterations = parse_u64(argv[++i], "max-iterations");
        else if (strcmp(argv[i], "--shape") == 0 && i + 1 < argc) shape = bench_semiprime_shape_name(argv[++i]);
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) threads = (uint32_t)parse_u64(argv[++i], "threads");
    }

    if (bits < 4u) bits = 4u;
    if (bits > (unsigned)(BN_MAX_LIMBS * 64)) bits = (unsigned)(BN_MAX_LIMBS * 64);

    unsigned p_bits = 0u, q_bits = 0u;
    bench_semiprime_shape_bits(bits, shape, &p_bits, &q_bits);
    threads = bench_effective_threads(count, threads);

    printf("=== benchmark-rho ===\n");
    printf("  count=%zu  bits=%u  seed=%" PRIu64 "\n", count, bits, seed);
    if (max_iterations > 0u) printf("  max-iterations    = %" PRIu64 "\n", max_iterations);
    else printf("  max-iterations    = default by engine\n");
    printf("  threads           = %u\n", threads);
    printf("  shape             = %s\n", shape);
    printf("  corpus            = %s semiprimes (%u-bit factor, %u-bit cofactor)\n",
        shape, p_bits, q_bits);
    printf("  engine            = %s\n\n", bits <= 64u ? "uint64 rho" : "BigNum rho");

    uint64_t rng = seed;
    double total_gen_sec = 0.0;
    BenchFactorCase* cases = (BenchFactorCase*)xmalloc(count * sizeof(BenchFactorCase));
    BenchFactorOutcome* outcomes = (BenchFactorOutcome*)xmalloc(count * sizeof(BenchFactorOutcome));

    for (size_t ci = 0u; ci < count; ++ci) {
        cases[ci].seed = rand_range(&rng, 1u, UINT64_MAX - 1u);
        if (bits <= 64u) {
            uint64_t p = random_prime_u64_bits(&rng, p_bits);
            uint64_t q = random_prime_u64_bits(&rng, q_bits);
            cases[ci].kind = BENCH_FACTOR_CASE_U64;
            cases[ci].n64 = p * q;
            if (ci == 0u) {
                printf("  first case: p=%" PRIu64 " q=%" PRIu64 " n=%" PRIu64 "\n", p, q, cases[ci].n64);
            }
        }
        else {
            BigNum p, q;
            double gen0 = now_sec();
            cases[ci].kind = BENCH_FACTOR_CASE_BN;
            bench_random_semiprime_bn(&p, &q, &cases[ci].nbn, p_bits, q_bits, &rng);
            {
                double gen_elapsed = now_sec() - gen0;
                total_gen_sec += gen_elapsed;
                if (ci == 0u) {
                    printf("  first case: p=%u-bit q=%u-bit n=%u-bit (gen=%.3fs)\n",
                        bn_bitlen(&p), bn_bitlen(&q), bn_bitlen(&cases[ci].nbn), gen_elapsed);
                }
            }
        }
    }

    {
        BenchFactorShared shared;
        shared.cases = cases;
        shared.outcomes = outcomes;
        shared.stream = NULL;
        shared.param1 = max_iterations;
        shared.param2 = 0u;
        bench_run_parallel(count, threads, bench_worker_rho, &shared);
    }

    {
        FactorBenchSummary summary = summarize_factor_bench(outcomes, count);
        print_factor_bench_summary(count, &summary, total_gen_sec);
    }

    free(outcomes);
    free(cases);
}

static void cmd_bench_ecm(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 50u;
    unsigned bits = 128u;
    uint64_t seed = 42u;
    uint64_t B1 = 0u;
    uint64_t curves = 0u;
    const char* shape = "balanced";
    uint32_t threads = g_app_threads;

    (void)s;
    (void)constrain_lo;
    (void)constrain_hi;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) count = (size_t)parse_u64(argv[++i], "count");
        else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) bits = (unsigned)parse_u64(argv[++i], "bits");
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = parse_u64(argv[++i], "seed");
        else if (strcmp(argv[i], "--B1") == 0 && i + 1 < argc) B1 = parse_u64(argv[++i], "B1");
        else if (strcmp(argv[i], "--curves") == 0 && i + 1 < argc) curves = parse_u64(argv[++i], "curves");
        else if (strcmp(argv[i], "--shape") == 0 && i + 1 < argc) shape = bench_semiprime_shape_name(argv[++i]);
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) threads = (uint32_t)parse_u64(argv[++i], "threads");
    }

    if (bits < 8u) bits = 8u;
    if (bits > (unsigned)(BN_MAX_LIMBS * 64)) bits = (unsigned)(BN_MAX_LIMBS * 64);

    unsigned factor_bits = 0u, cofactor_bits = 0u;
    bench_semiprime_shape_bits(bits, shape, &factor_bits, &cofactor_bits);
    threads = bench_effective_threads(count, threads);

    printf("=== benchmark-ecm ===\n");
    printf("  count=%zu  bits=%u  seed=%" PRIu64 "\n", count, bits, seed);
    if (B1 > 0u) printf("  B1                = %" PRIu64 "\n", B1);
    else printf("  B1                = default by engine\n");
    if (curves > 0u) printf("  curves            = %" PRIu64 "\n", curves);
    else printf("  curves            = default by engine\n");
    printf("  threads           = %u\n", threads);
    printf("  shape             = %s\n", shape);
    printf("  corpus            = %s semiprimes (%u-bit factor, %u-bit cofactor)\n",
        shape, factor_bits, cofactor_bits);
    printf("  engine            = BigNum ECM stage-1\n\n");

    uint64_t rng = seed;
    double total_gen_sec = 0.0;
    BenchFactorCase* cases = (BenchFactorCase*)xmalloc(count * sizeof(BenchFactorCase));
    BenchFactorOutcome* outcomes = (BenchFactorOutcome*)xmalloc(count * sizeof(BenchFactorOutcome));

    for (size_t ci = 0u; ci < count; ++ci) {
        BigNum p, q;
        double gen0 = now_sec();
        cases[ci].kind = BENCH_FACTOR_CASE_BN;
        cases[ci].seed = rand_range(&rng, 1u, UINT64_MAX - 1u);
        bench_random_semiprime_bn(&p, &q, &cases[ci].nbn, factor_bits, cofactor_bits, &rng);
        {
            double gen_elapsed = now_sec() - gen0;
            total_gen_sec += gen_elapsed;
            if (ci == 0u) {
                printf("  first case: p=%u-bit q=%u-bit n=%u-bit (gen=%.3fs)\n",
                    bn_bitlen(&p), bn_bitlen(&q), bn_bitlen(&cases[ci].nbn), gen_elapsed);
            }
        }
    }

    {
        BenchFactorShared shared;
        shared.cases = cases;
        shared.outcomes = outcomes;
        shared.stream = NULL;
        shared.param1 = B1;
        shared.param2 = curves;
        bench_run_parallel(count, threads, bench_worker_ecm, &shared);
    }

    {
        FactorBenchSummary summary = summarize_factor_bench(outcomes, count);
        print_factor_bench_summary(count, &summary, total_gen_sec);
    }

    free(outcomes);
    free(cases);
}

static void cmd_bench_fermat(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 50u;
    unsigned bits = 64u;
    uint64_t seed = 42u;
    uint64_t max_steps = 0u;
    uint64_t delta_max = 4096u;
    uint32_t threads = g_app_threads;

    (void)s;
    (void)constrain_lo;
    (void)constrain_hi;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) count = (size_t)parse_u64(argv[++i], "count");
        else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) bits = (unsigned)parse_u64(argv[++i], "bits");
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = parse_u64(argv[++i], "seed");
        else if (strcmp(argv[i], "--max-steps") == 0 && i + 1 < argc) max_steps = parse_u64(argv[++i], "max-steps");
        else if (strcmp(argv[i], "--delta-max") == 0 && i + 1 < argc) delta_max = parse_u64(argv[++i], "delta-max");
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) threads = (uint32_t)parse_u64(argv[++i], "threads");
    }

    if (bits < 4u) bits = 4u;
    if (bits > (unsigned)(BN_MAX_LIMBS * 64)) bits = (unsigned)(BN_MAX_LIMBS * 64);
    if (delta_max < 2u) delta_max = 2u;
    if (delta_max > (UINT64_MAX / 2u) - 1u) delta_max = (UINT64_MAX / 2u) - 1u;
    threads = bench_effective_threads(count, threads);

    printf("=== benchmark-fermat ===\n");
    printf("  count=%zu  bits=%u  seed=%" PRIu64 "  delta-max=%" PRIu64 "\n",
        count, bits, seed, delta_max);
    if (max_steps > 0u) printf("  max-steps         = %" PRIu64 "\n", max_steps);
    else printf("  max-steps         = default by engine\n");
    printf("  threads           = %u\n", threads);
    printf("  engine            = %s\n\n",
        bits <= 64u ? "uint64" : bits <= 128u ? "U128" : "BigNum");

    uint64_t rng = seed;
    BenchFactorCase* cases = (BenchFactorCase*)xmalloc(count * sizeof(BenchFactorCase));
    BenchFactorOutcome* outcomes = (BenchFactorOutcome*)xmalloc(count * sizeof(BenchFactorOutcome));

    for (size_t ci = 0u; ci < count; ++ci) {
        if (bits <= 64u) {
            unsigned factor_bits = bits / 2u;
            uint64_t delta;
            uint64_t left;
            uint64_t right;
            if (factor_bits < 2u) factor_bits = 2u;
            delta = fermat_even_delta(&rng, delta_max);
            left = rand_odd_bits_u64(&rng, factor_bits);
            if (left > UINT64_MAX - 2u * delta) left = (UINT64_MAX - 2u * delta) | 1u;
            right = left + 2u * delta;
            cases[ci].kind = BENCH_FACTOR_CASE_U64;
            cases[ci].n64 = left * right;
            if (ci == 0u) {
                printf("  first case: factors=%" PRIu64 " and %" PRIu64 " (delta=%" PRIu64 ")\n",
                    left, right, delta);
            }
        }
        else if (bits <= 128u) {
            unsigned factor_bits = bits / 2u;
            uint64_t delta;
            uint64_t left;
            uint64_t right;
            if (factor_bits < 2u) factor_bits = 2u;
            if (factor_bits > 64u) factor_bits = 64u;
            delta = fermat_even_delta(&rng, delta_max);
            left = rand_odd_bits_u64(&rng, factor_bits);
            if (left > UINT64_MAX - 2u * delta) left = (UINT64_MAX - 2u * delta) | 1u;
            right = left + 2u * delta;
            cases[ci].kind = BENCH_FACTOR_CASE_U128;
            cases[ci].n128 = u128_mul64(left, right);
            if (ci == 0u) {
                char nbuf[64];
                u128_to_str(cases[ci].n128, nbuf, sizeof(nbuf));
                printf("  first case: factors=%" PRIu64 " and %" PRIu64 " (delta=%" PRIu64 ", n=%s)\n",
                    left, right, delta, nbuf);
            }
        }
        else {
            unsigned factor_bits = bits / 2u;
            uint64_t delta;
            BigNum left, right;
            if (factor_bits < 2u) factor_bits = 2u;
            delta = fermat_even_delta(&rng, delta_max);
            bn_random_bits(&left, factor_bits, &rng);
            left.limbs[0] |= 1u;
            bn_copy(&right, &left);
            bn_add_u64(&right, 2u * delta);
            cases[ci].kind = BENCH_FACTOR_CASE_BN;
            bn_mul(&cases[ci].nbn, &left, &right);
            if (ci == 0u) {
                printf("  first case: factors=%u-bit and %u-bit (delta=%" PRIu64 ", n=%u-bit)\n",
                    bn_bitlen(&left), bn_bitlen(&right), delta, bn_bitlen(&cases[ci].nbn));
            }
        }
    }

    {
        BenchFactorShared shared;
        shared.cases = cases;
        shared.outcomes = outcomes;
        shared.stream = NULL;
        shared.param1 = max_steps;
        shared.param2 = 0u;
        bench_run_parallel(count, threads, bench_worker_fermat, &shared);
    }

    {
        FactorBenchSummary summary = summarize_factor_bench(outcomes, count);
        print_factor_bench_summary(count, &summary, 0.0);
    }

    free(outcomes);
    free(cases);
}

/* ── Latency stats ─────────────────────────────────────────────────── */

typedef struct {
    size_t count;
    double min_us, max_us, mean_us;
    double p50_us, p95_us, p99_us;
    double total_sec;
    double qps;
} LatencyReport;

static int cmp_double_bench(const void* a, const void* b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

static LatencyReport compute_latency(double* us_arr, size_t n, double wall_sec) {
    LatencyReport r;
    memset(&r, 0, sizeof(r));
    r.count = n;
    r.total_sec = wall_sec;
    if (n == 0u) return r;
    qsort(us_arr, n, sizeof(double), cmp_double_bench);
    r.min_us = us_arr[0];
    r.max_us = us_arr[n - 1u];
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) sum += us_arr[i];
    r.mean_us = sum / (double)n;
    r.p50_us = us_arr[n / 2u];
    r.p95_us = us_arr[(size_t)(n * 0.95)];
    r.p99_us = us_arr[(size_t)(n * 0.99)];
    r.qps = wall_sec > 0.0 ? (double)n / wall_sec : 0.0;
    return r;
}

static void print_latency(const LatencyReport* r, const char* label) {
    printf("  %s: %zu ops in %.3fs (%.0f ops/sec)\n", label, r->count, r->total_sec, r->qps);
    printf("    p50=%.1fus  p95=%.1fus  p99=%.1fus  min=%.1fus  max=%.1fus  mean=%.1fus\n",
        r->p50_us, r->p95_us, r->p99_us, r->min_us, r->max_us, r->mean_us);
}

/* ── Cache snapshot ────────────────────────────────────────────────── */

static CacheSnapshot snap_cache(void) {
    CacheSnapshot s;
    s.hot_count = g_hot_cache.count;
    s.hot_bytes = g_hot_cache.bytes_used;
    s.wheel_count = g_wheel_cache.count;
    s.wheel_bytes = g_wheel_cache.bytes_used;
    return s;
}

static void print_cache_delta(const CacheSnapshot* before, const CacheSnapshot* after) {
    printf("  cache delta: hot entries %zu->%zu  wheel entries %zu->%zu\n",
        before->hot_count, after->hot_count,
        before->wheel_count, after->wheel_count);
}

/* ── Shared: save/restore cache budgets for no-fill mode ───────────── */

/**
 * Enter cache no-fill mode: set budgets to 0 (blocks insert) and
 * set g_cache_readonly = true (blocks LRU counter writes in lookups).
 * Combined, this makes the entire cache layer truly read-only with
 * zero data races — safe for concurrent threads.
 */
static SavedCacheBudgets bench_disable_cache_fill(void) {
    SavedCacheBudgets saved;
    saved.wheel_budget = g_wheel_cache.budget;
    saved.hot_budget = g_hot_cache.budget;
    saved.was_readonly = g_cache_readonly;
    g_wheel_cache.budget = 0u;
    g_hot_cache.budget = 0u;
    g_cache_readonly = true;
    return saved;
}

static void bench_restore_cache_fill(const SavedCacheBudgets* saved) {
    g_wheel_cache.budget = saved->wheel_budget;
    g_hot_cache.budget = saved->hot_budget;
    g_cache_readonly = saved->was_readonly;
}

/* ── benchmark-is-prime ────────────────────────────────────────────── */

static void cmd_bench_is_prime(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 1000u;
    uint64_t seed = 42u;
    uint64_t range_lo = 1u;
    uint64_t range_hi = 0u;
    const char* pattern = "random";
    const char* csv_out = NULL;
    const char* cache_mode = "normal";
    int64_t target_seg = -1;
    uint64_t center = 0u;
    bool has_center = false;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--min") == 0 && i + 1 < argc) { range_lo = parse_u64(argv[++i], "min"); }
        else if (strcmp(argv[i], "--max") == 0 && i + 1 < argc) { range_hi = parse_u64(argv[++i], "max"); }
        else if (strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) { pattern = argv[++i]; }
        else if (strcmp(argv[i], "--csv-out") == 0 && i + 1 < argc) { csv_out = argv[++i]; }
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) { cache_mode = argv[++i]; }
        else if (strcmp(argv[i], "--target-seg") == 0 && i + 1 < argc) { target_seg = (int64_t)parse_u64(argv[++i], "target-seg"); }
        else if (strcmp(argv[i], "--center") == 0 && i + 1 < argc) { center = parse_u64(argv[++i], "center"); has_center = true; }
    }

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    /* Clamp to constrained range (cold or hot segment bounds) */
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;

    /* --target-seg overrides range (clamped to constrained bounds) */
    if (target_seg >= 0 && (size_t)target_seg < s->index_len) {
        range_lo = segment_start_n(&s->index_entries[(size_t)target_seg]);
        range_hi = segment_end_n(&s->index_entries[(size_t)target_seg]);
    }
    else if (has_center) {
        uint64_t radius = 50000u;
        range_lo = center > radius ? center - radius : db_lo;
        range_hi = center + radius < db_hi ? center + radius : db_hi;
    }
    else {
        if (range_hi == 0u) range_hi = db_hi;
    }
    if (range_lo < db_lo) range_lo = db_lo;
    if (range_hi > db_hi) range_hi = db_hi;
    if (range_lo > range_hi) { printf("Constrained range is empty.\n"); return; }

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);

    printf("=== benchmark-is-prime ===\n");
    printf("  count=%zu  pattern=%s  seed=%" PRIu64 "\n", count, pattern, seed);
    printf("  range=%" PRIu64 "..%" PRIu64 "\n", range_lo, range_hi);
    printf("  cache-mode=%s  cache-fill=%s\n", cache_mode, no_fill ? "DISABLED" : "allowed");
    if (target_seg >= 0) printf("  target-seg=%zu\n", (size_t)target_seg);
    if (has_center) printf("  center=%" PRIu64 "\n", center);

    /* Generate query numbers */
    uint64_t* queries = (uint64_t*)xmalloc(count * sizeof(uint64_t));
    uint64_t rng = seed;

    if (strcmp(pattern, "sequential") == 0) {
        uint64_t stride = (range_hi - range_lo) / (count > 1u ? count - 1u : 1u);
        if (stride < 1u) stride = 1u;
        for (size_t i = 0; i < count; ++i) queries[i] = range_lo + i * stride;
    }
    else if (strcmp(pattern, "same-segment") == 0 && target_seg < 0) {
        /* Pick a random segment within the constrained range */
        size_t seg_lo_idx = cpss_find_segment_for_n(s, range_lo);
        size_t seg_hi_idx = cpss_find_segment_for_n(s, range_hi);
        if (seg_lo_idx == SIZE_MAX) seg_lo_idx = 0u;
        if (seg_hi_idx == SIZE_MAX) seg_hi_idx = s->index_len - 1u;
        size_t seg = (size_t)rand_range(&rng, seg_lo_idx, seg_hi_idx);
        uint64_t slo = segment_start_n(&s->index_entries[seg]);
        uint64_t shi = segment_end_n(&s->index_entries[seg]);
        if (slo < db_lo) slo = db_lo;
        if (shi > db_hi) shi = db_hi;
        range_lo = slo; range_hi = shi;
        printf("  same-segment: seg=%zu (%" PRIu64 "..%" PRIu64 ")\n", seg, range_lo, range_hi);
        for (size_t i = 0; i < count; ++i) queries[i] = rand_range(&rng, range_lo, range_hi);
    }
    else if (strcmp(pattern, "clustered") == 0) {
        size_t num_clusters = count / 20u;
        if (num_clusters < 1u) num_clusters = 1u;
        uint64_t* centers = (uint64_t*)xmalloc(num_clusters * sizeof(uint64_t));
        for (size_t ci = 0; ci < num_clusters; ++ci) centers[ci] = rand_range(&rng, range_lo, range_hi);
        for (size_t i = 0; i < count; ++i) {
            uint64_t cc = centers[i % num_clusters];
            int64_t offset = (int64_t)(xorshift64(&rng) % 10000u) - 5000;
            uint64_t v = (offset < 0 && (uint64_t)(-offset) > cc) ? range_lo : cc + (uint64_t)offset;
            if (v < range_lo) v = range_lo;
            if (v > range_hi) v = range_hi;
            queries[i] = v;
        }
        free(centers);
    }
    else if (strcmp(pattern, "hotset") == 0) {
        size_t hot_size = count / 10u;
        if (hot_size < 5u) hot_size = 5u;
        uint64_t* hs = (uint64_t*)xmalloc(hot_size * sizeof(uint64_t));
        for (size_t i = 0; i < hot_size; ++i) hs[i] = rand_range(&rng, range_lo, range_hi);
        for (size_t i = 0; i < count; ++i) {
            if (xorshift64(&rng) % 5u < 4u) queries[i] = hs[xorshift64(&rng) % hot_size];
            else queries[i] = rand_range(&rng, range_lo, range_hi);
        }
        free(hs);
    }
    else {
        for (size_t i = 0; i < count; ++i) queries[i] = rand_range(&rng, range_lo, range_hi);
    }

    /* Run benchmark */
    SavedCacheBudgets saved = { 0 };
    if (no_fill) saved = bench_disable_cache_fill();

    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t prime_count = 0u, composite_count = 0u, outside_count = 0u;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        double t0 = now_sec();
        int r = cpss_is_prime(s, queries[i]);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
        if (r == 1) ++prime_count;
        else if (r == 0) ++composite_count;
        else ++outside_count;
    }

    double wall1 = now_sec();
    if (no_fill) bench_restore_cache_fill(&saved);
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);

    printf("  results: %" PRIu64 " prime, %" PRIu64 " composite, %" PRIu64 " outside\n",
        prime_count, composite_count, outside_count);
    print_latency(&lr, "is-prime");
    print_cache_delta(&snap0, &snap1);

    if (csv_out) {
        FILE* f = fopen(csv_out, "w");
        if (f) {
            fprintf(f, "query_idx,n,result,latency_us\n");
            for (size_t i = 0; i < count; ++i) {
                fprintf(f, "%zu,%" PRIu64 ",%d,%.1f\n", i, queries[i],
                    queries[i] < prime_count ? 1 : 0, latencies[i]);
            }
            fclose(f);
            printf("  csv written: %s\n", csv_out);
        }
    }

    free(queries);
    free(latencies);
}

/* ── benchmark-count-range ─────────────────────────────────────────── */

static void cmd_bench_count_range(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 100u;
    uint64_t seed = 42u;
    const char* pattern = "random";
    uint64_t min_width = 100u, max_width = 1000000u;
    const char* csv_out = NULL;
    const char* cache_mode = "normal";
    int64_t target_seg = -1;
    uint64_t center = 0u;
    bool has_center = false;
    uint64_t fixed_lo = 0u, fixed_hi = 0u;
    bool has_fixed_lo = false, has_fixed_hi = false;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) { pattern = argv[++i]; }
        else if (strcmp(argv[i], "--min-width") == 0 && i + 1 < argc) { min_width = parse_u64(argv[++i], "min-width"); }
        else if (strcmp(argv[i], "--max-width") == 0 && i + 1 < argc) { max_width = parse_u64(argv[++i], "max-width"); }
        else if (strcmp(argv[i], "--csv-out") == 0 && i + 1 < argc) { csv_out = argv[++i]; }
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) { cache_mode = argv[++i]; }
        else if (strcmp(argv[i], "--target-seg") == 0 && i + 1 < argc) { target_seg = (int64_t)parse_u64(argv[++i], "target-seg"); }
        else if (strcmp(argv[i], "--center") == 0 && i + 1 < argc) { center = parse_u64(argv[++i], "center"); has_center = true; }
        else if (strcmp(argv[i], "--fixed-lo") == 0 && i + 1 < argc) { fixed_lo = parse_u64(argv[++i], "fixed-lo"); has_fixed_lo = true; }
        else if (strcmp(argv[i], "--fixed-hi") == 0 && i + 1 < argc) { fixed_hi = parse_u64(argv[++i], "fixed-hi"); has_fixed_hi = true; }
    }

    cpss_build_segment_index(s);
    cpss_build_prime_index(s); /* pre-build so first query doesn't include setup */
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;

    /* Compute effective query range from controls */
    uint64_t qry_lo = db_lo, qry_hi = db_hi;
    if (target_seg >= 0 && (size_t)target_seg < s->index_len) {
        qry_lo = segment_start_n(&s->index_entries[(size_t)target_seg]);
        qry_hi = segment_end_n(&s->index_entries[(size_t)target_seg]);
    }
    else if (has_center) {
        uint64_t radius = max_width * 2u;
        qry_lo = center > radius ? center - radius : db_lo;
        qry_hi = center + radius < db_hi ? center + radius : db_hi;
    }
    if (qry_lo < db_lo) qry_lo = db_lo;
    if (qry_hi > db_hi) qry_hi = db_hi;
    if (qry_lo > qry_hi) { printf("Constrained range is empty.\n"); return; }

    /* Set width defaults per pattern */
    if (strcmp(pattern, "small-window") == 0) { min_width = 100u; max_width = 1000000u; }
    else if (strcmp(pattern, "medium-window") == 0) { min_width = 1000000u; max_width = 1000000000u; }
    else if (strcmp(pattern, "large-window") == 0) { min_width = 1000000000u; max_width = 100000000000ull; }
    else if (strcmp(pattern, "full-range") == 0) { min_width = db_hi / 2u; max_width = db_hi; }
    else if (strcmp(pattern, "hot-boundary") == 0) { min_width = 100u; max_width = 100000u; }

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);

    /* Warn on likely-expensive workloads */
    if (strcmp(pattern, "large-window") == 0 && count > 20u) {
        printf("  WARNING: large-window with count=%zu may be very slow. Consider --count 5-20.\n", count);
    }
    if (strcmp(pattern, "full-range") == 0 && count > 10u && !has_fixed_hi) {
        printf("  WARNING: full-range with varying hi and count=%zu may thrash caches. Consider --fixed-hi or --count 5.\n", count);
    }

    printf("=== benchmark-count-range ===\n");
    printf("  count=%zu  pattern=%s  seed=%" PRIu64 "\n", count, pattern, seed);
    printf("  width=%" PRIu64 "..%" PRIu64 "\n", min_width, max_width);
    printf("  cache-mode=%s  cache-fill=%s\n", cache_mode, no_fill ? "DISABLED" : "allowed");
    if (target_seg >= 0) printf("  target-seg=%zu  range=%" PRIu64 "..%" PRIu64 "\n", (size_t)target_seg, qry_lo, qry_hi);
    if (has_center) printf("  center=%" PRIu64 "\n", center);
    if (has_fixed_lo) printf("  fixed-lo=%" PRIu64 "\n", fixed_lo);
    if (has_fixed_hi) printf("  fixed-hi=%" PRIu64 "\n", fixed_hi);

    SavedCacheBudgets saved = { 0 };
    if (no_fill) saved = bench_disable_cache_fill();

    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t* widths = (uint64_t*)xmalloc(count * sizeof(uint64_t));
    uint64_t rng = seed;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();
    FILE* csv = csv_out ? fopen(csv_out, "w") : NULL;
    if (csv) fprintf(csv, "idx,lo,hi,width,count,latency_us\n");

    for (size_t i = 0; i < count; ++i) {
        uint64_t lo, hi;

        if (has_fixed_lo && has_fixed_hi) {
            lo = fixed_lo; hi = fixed_hi;
        }
        else if (has_fixed_hi) {
            lo = has_fixed_lo ? fixed_lo : 1u;
            hi = fixed_hi;
        }
        else if (strcmp(pattern, "full-range") == 0) {
            lo = has_fixed_lo ? fixed_lo : 1u;
            hi = rand_range(&rng, db_hi / 2u, db_hi);
        }
        else if (strcmp(pattern, "hot-boundary") == 0) {
            uint64_t w = rand_range(&rng, min_width, max_width);
            size_t seg = (target_seg >= 0) ? (size_t)target_seg : (size_t)rand_range(&rng, 0, s->index_len - 1u);
            uint64_t boundary = segment_end_n(&s->index_entries[seg]);
            lo = (boundary > w / 2u) ? boundary - w / 2u : qry_lo;
            hi = lo + w;
        }
        else {
            uint64_t w = rand_range(&rng, min_width, max_width);
            lo = rand_range(&rng, qry_lo, qry_hi > w ? qry_hi - w : qry_lo);
            hi = lo + w;
        }
        if (hi > db_hi) hi = db_hi;
        if (lo < 1u) lo = 1u;
        widths[i] = hi - lo;

        double t0 = now_sec();
        uint64_t c = cpss_count_range_hot(s, lo, hi);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
        if (csv) fprintf(csv, "%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.1f\n",
            i, lo, hi, widths[i], c, latencies[i]);
    }

    double wall1 = now_sec();
    if (no_fill) bench_restore_cache_fill(&saved);
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);

    {
        uint64_t w_min = UINT64_MAX, w_max = 0u, w_sum = 0u;
        for (size_t wi = 0; wi < count; ++wi) {
            if (widths[wi] < w_min) w_min = widths[wi];
            if (widths[wi] > w_max) w_max = widths[wi];
            w_sum += widths[wi];
        }
        printf("  width min=%" PRIu64 " max=%" PRIu64 " mean=%" PRIu64 "\n",
            count > 0 ? w_min : 0u, w_max, count > 0 ? w_sum / count : 0u);
    }
    print_latency(&lr, "count-range");
    print_cache_delta(&snap0, &snap1);

    if (csv) { fclose(csv); printf("  csv written: %s\n", csv_out); }
    free(latencies);
    free(widths);
}

/* ── benchmark-list-range ──────────────────────────────────────────── */

static void cmd_bench_list_range(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 100u;
    uint64_t seed = 42u;
    const char* pattern = "random";
    uint64_t min_width = 100u, max_width = 10000u;
    size_t limit = 1000u;
    const char* csv_out = NULL;
    const char* cache_mode = "normal";
    int64_t target_seg = -1;
    uint64_t center = 0u;
    bool has_center = false;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) { pattern = argv[++i]; }
        else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) { limit = (size_t)parse_u64(argv[++i], "limit"); }
        else if (strcmp(argv[i], "--min-width") == 0 && i + 1 < argc) { min_width = parse_u64(argv[++i], "min-width"); }
        else if (strcmp(argv[i], "--max-width") == 0 && i + 1 < argc) { max_width = parse_u64(argv[++i], "max-width"); }
        else if (strcmp(argv[i], "--csv-out") == 0 && i + 1 < argc) { csv_out = argv[++i]; }
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) { cache_mode = argv[++i]; }
        else if (strcmp(argv[i], "--target-seg") == 0 && i + 1 < argc) { target_seg = (int64_t)parse_u64(argv[++i], "target-seg"); }
        else if (strcmp(argv[i], "--center") == 0 && i + 1 < argc) { center = parse_u64(argv[++i], "center"); has_center = true; }
    }
    if (strcmp(pattern, "small-window") == 0) { min_width = 100u; max_width = 10000u; }
    else if (strcmp(pattern, "clustered") == 0) { min_width = 1000u; max_width = 100000u; }

    cpss_build_segment_index(s);
    cpss_build_prime_index(s); /* pre-build so first query doesn't include setup */
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;

    uint64_t qry_lo = db_lo, qry_hi = db_hi;
    if (target_seg >= 0 && (size_t)target_seg < s->index_len) {
        qry_lo = segment_start_n(&s->index_entries[(size_t)target_seg]);
        qry_hi = segment_end_n(&s->index_entries[(size_t)target_seg]);
    }
    else if (has_center) {
        uint64_t radius = max_width * 2u;
        qry_lo = center > radius ? center - radius : db_lo;
        qry_hi = center + radius < db_hi ? center + radius : db_hi;
    }
    if (qry_lo < db_lo) qry_lo = db_lo;
    if (qry_hi > db_hi) qry_hi = db_hi;
    if (qry_lo > qry_hi) { printf("Constrained range is empty.\n"); return; }

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);

    printf("=== benchmark-list-range ===\n");
    printf("  count=%zu  pattern=%s  seed=%" PRIu64 "  limit=%zu\n", count, pattern, seed, limit);
    printf("  cache-mode=%s  cache-fill=%s\n", cache_mode, no_fill ? "DISABLED" : "allowed");
    if (target_seg >= 0) printf("  target-seg=%zu  range=%" PRIu64 "..%" PRIu64 "\n", (size_t)target_seg, qry_lo, qry_hi);
    if (has_center) printf("  center=%" PRIu64 "\n", center);

    SavedCacheBudgets saved = { 0 };
    if (no_fill) saved = bench_disable_cache_fill();

    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t* buf = (uint64_t*)xmalloc(limit * sizeof(uint64_t));
    uint64_t rng = seed;
    uint64_t total_primes_listed = 0u;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        uint64_t w = rand_range(&rng, min_width, max_width);
        uint64_t lo = rand_range(&rng, qry_lo, qry_hi > w ? qry_hi - w : qry_lo);
        uint64_t hi = lo + w;
        if (hi > db_hi) hi = db_hi;

        double t0 = now_sec();
        size_t found = cpss_list_range_hot(s, lo, hi, buf, limit);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
        total_primes_listed += found;
    }

    double wall1 = now_sec();
    if (no_fill) bench_restore_cache_fill(&saved);
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);

    printf("  total primes listed = %" PRIu64 "\n", total_primes_listed);
    printf("  avg list length     = %.1f\n", count > 0 ? (double)total_primes_listed / (double)count : 0.0);
    print_latency(&lr, "list-range");
    print_cache_delta(&snap0, &snap1);

    free(latencies);
    free(buf);
}

/* ── benchmark-mixed ───────────────────────────────────────────────── */

static void cmd_bench_mixed(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t ops = 1000u;
    uint64_t seed = 42u;
    const char* pattern = "random";
    unsigned mix_is = 70u, mix_count = 25u, mix_list = 5u;
    const char* cache_mode = "normal";

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--ops") == 0 && i + 1 < argc) { ops = (size_t)parse_u64(argv[++i], "ops"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) { pattern = argv[++i]; }
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) { cache_mode = argv[++i]; }
        else if (strcmp(argv[i], "--mix") == 0 && i + 1 < argc) {
            ++i;
            if (sscanf(argv[i], "%u:%u:%u", &mix_is, &mix_count, &mix_list) != 3) {
                printf("Invalid mix format. Use A:B:C (e.g. 70:25:5)\n");
                return;
            }
        }
    }

    unsigned mix_total = mix_is + mix_count + mix_list;
    if (mix_total == 0u) { printf("Mix sums to zero.\n"); return; }

    cpss_build_segment_index(s);
    cpss_build_prime_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    if (db_lo > db_hi) { printf("Constrained range is empty.\n"); return; }

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);

    printf("=== benchmark-mixed ===\n");
    printf("  ops=%zu  pattern=%s  seed=%" PRIu64 "\n", ops, pattern, seed);
    printf("  mix=%u:%u:%u (is-prime:count:list)\n", mix_is, mix_count, mix_list);
    printf("  cache-mode=%s  cache-fill=%s\n", cache_mode, no_fill ? "DISABLED" : "allowed");

    size_t cap_is = ops, cap_count = ops, cap_list = ops;
    double* lat_is = (double*)xmalloc(cap_is * sizeof(double));
    double* lat_count = (double*)xmalloc(cap_count * sizeof(double));
    double* lat_list = (double*)xmalloc(cap_list * sizeof(double));
    size_t n_is = 0u, n_count = 0u, n_list = 0u;
    uint64_t list_buf[100];
    uint64_t rng = seed;

    SavedCacheBudgets saved = { 0 };
    if (no_fill) saved = bench_disable_cache_fill();

    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < ops; ++i) {
        unsigned roll = (unsigned)(xorshift64(&rng) % mix_total);
        if (roll < mix_is) {
            /* is-prime */
            uint64_t n = rand_range(&rng, db_lo, db_hi);
            double t0 = now_sec();
            (void)cpss_is_prime(s, n);
            double t1 = now_sec();
            if (n_is < cap_is) lat_is[n_is++] = (t1 - t0) * 1e6;
        }
        else if (roll < mix_is + mix_count) {
            /* count-range */
            uint64_t w = rand_range(&rng, 100u, 10000000u);
            uint64_t lo = rand_range(&rng, db_lo, db_hi > w ? db_hi - w : db_lo);
            double t0 = now_sec();
            (void)cpss_count_range_hot(s, lo, lo + w);
            double t1 = now_sec();
            if (n_count < cap_count) lat_count[n_count++] = (t1 - t0) * 1e6;
        }
        else {
            /* list-range */
            uint64_t w = rand_range(&rng, 100u, 10000u);
            uint64_t lo = rand_range(&rng, db_lo, db_hi > w ? db_hi - w : db_lo);
            double t0 = now_sec();
            (void)cpss_list_range_hot(s, lo, lo + w, list_buf, 100u);
            double t1 = now_sec();
            if (n_list < cap_list) lat_list[n_list++] = (t1 - t0) * 1e6;
        }
    }

    double wall1 = now_sec();
    if (no_fill) bench_restore_cache_fill(&saved);
    CacheSnapshot snap1 = snap_cache();

    printf("  total wall time = %.3fs  (%.0f ops/sec)\n", wall1 - wall0,
        (wall1 - wall0) > 0.0 ? (double)ops / (wall1 - wall0) : 0.0);

    if (n_is > 0u) {
        LatencyReport lr = compute_latency(lat_is, n_is, wall1 - wall0);
        print_latency(&lr, "is-prime");
    }
    if (n_count > 0u) {
        LatencyReport lr = compute_latency(lat_count, n_count, wall1 - wall0);
        print_latency(&lr, "count-range");
    }
    if (n_list > 0u) {
        LatencyReport lr = compute_latency(lat_list, n_list, wall1 - wall0);
        print_latency(&lr, "list-range");
    }
    print_cache_delta(&snap0, &snap1);

    free(lat_is);
    free(lat_count);
    free(lat_list);
}

/* ── benchmark-next-prime ──────────────────────────────────────────── */

static void cmd_bench_next_prime(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 1000u;
    uint64_t seed = 42u;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
    }

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    if (db_lo > db_hi) { printf("Constrained range is empty.\n"); return; }

    printf("=== benchmark-next-prime ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  range=%" PRIu64 "..%" PRIu64 "\n", count, seed, db_lo, db_hi);

    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t rng = seed;
    size_t found = 0u, not_found = 0u, wrong = 0u;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        uint64_t n = rand_range(&rng, db_lo, db_hi > 1000u ? db_hi - 1000u : db_lo);
        double t0 = now_sec();
        uint64_t p = cpss_next_prime(s, n);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
        if (p > 0u) { ++found; if (p <= n) ++wrong; }
        else ++not_found;
    }

    double wall1 = now_sec();
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);
    printf("  found=%zu  not_found=%zu  wrong=%zu\n", found, not_found, wrong);
    print_latency(&lr, "next-prime");
    print_cache_delta(&snap0, &snap1);
    free(latencies);
}

/* ── benchmark-prev-prime ──────────────────────────────────────────── */

static void cmd_bench_prev_prime(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 1000u;
    uint64_t seed = 42u;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
    }

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    if (db_lo > db_hi) { printf("Constrained range is empty.\n"); return; }

    printf("=== benchmark-prev-prime ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  range=%" PRIu64 "..%" PRIu64 "\n", count, seed, db_lo, db_hi);

    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t rng = seed;
    size_t found = 0u, not_found = 0u, wrong = 0u;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        uint64_t n = rand_range(&rng, db_lo + 1000u < db_hi ? db_lo + 1000u : db_lo, db_hi);
        double t0 = now_sec();
        uint64_t p = cpss_prev_prime(s, n);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
        if (p > 0u) { ++found; if (p >= n) ++wrong; }
        else ++not_found;
    }

    double wall1 = now_sec();
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);
    printf("  found=%zu  not_found=%zu  wrong=%zu\n", found, not_found, wrong);
    print_latency(&lr, "prev-prime");
    print_cache_delta(&snap0, &snap1);
    free(latencies);
}

/* ── benchmark-pi ──────────────────────────────────────────────────── */

static void cmd_bench_pi(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 100u;
    uint64_t seed = 42u;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
    }

    cpss_build_segment_index(s);
    cpss_build_prime_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    if (db_lo > db_hi) { printf("Constrained range is empty.\n"); return; }

    printf("=== benchmark-pi ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  range=%" PRIu64 "..%" PRIu64 "\n", count, seed, db_lo, db_hi);

    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t rng = seed;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        uint64_t n = rand_range(&rng, db_lo, db_hi);
        double t0 = now_sec();
        (void)cpss_pi(s, n);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
    }

    double wall1 = now_sec();
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);
    print_latency(&lr, "pi");
    print_cache_delta(&snap0, &snap1);
    free(latencies);
}

/* ── benchmark-nth-prime ───────────────────────────────────────────── */

static void cmd_bench_nth_prime(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 100u;
    uint64_t seed = 42u;
    (void)constrain_lo; (void)constrain_hi;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
    }

    cpss_build_segment_index(s);
    cpss_build_prime_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }

    /* Compute max_k from the constrained segment range only */
    uint64_t max_k = ARRAY_LEN(WHEEL_PRIMES); /* wheel primes always available */
    if (constrain_lo > 0u || constrain_hi < UINT64_MAX) {
        /* Find first/last segments within the constrained range */
        size_t first = 0u, last = s->index_len - 1u;
        for (size_t si = 0u; si < s->index_len; ++si) {
            if (segment_end_n(&s->index_entries[si]) >= constrain_lo) { first = si; break; }
        }
        for (size_t si = s->index_len; si > 0u; --si) {
            if (segment_start_n(&s->index_entries[si - 1u]) <= constrain_hi) { last = si - 1u; break; }
        }
        if (last >= first && last < s->index_len) {
            max_k += s->seg_prime_prefix[last + 1u] - s->seg_prime_prefix[first];
        }
    }
    else {
        max_k = s->seg_prime_prefix[s->index_len] + ARRAY_LEN(WHEEL_PRIMES);
    }
    if (max_k < 1u) { printf("No primes in constrained range.\n"); return; }

    printf("=== benchmark-nth-prime ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  max_k=%" PRIu64 "\n", count, seed, max_k);

    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t rng = seed;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        uint64_t k = rand_range(&rng, 1u, max_k);
        double t0 = now_sec();
        (void)cpss_nth_prime(s, k);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
    }

    double wall1 = now_sec();
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);
    print_latency(&lr, "nth-prime");
    print_cache_delta(&snap0, &snap1);
    free(latencies);
}

/* ── benchmark-prime-gap ───────────────────────────────────────────── */

static void cmd_bench_prime_gap(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 500u;
    uint64_t seed = 42u;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
    }

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    if (db_lo > db_hi) { printf("Constrained range is empty.\n"); return; }

    printf("=== benchmark-prime-gap ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  range=%" PRIu64 "..%" PRIu64 "\n", count, seed, db_lo, db_hi);

    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t rng = seed;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        uint64_t n = rand_range(&rng, db_lo, db_hi);
        double t0 = now_sec();
        (void)cpss_prime_gap_at(s, n);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
    }

    double wall1 = now_sec();
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);
    print_latency(&lr, "prime-gap");
    print_cache_delta(&snap0, &snap1);
    free(latencies);
}

/* ── benchmark-primes-near ─────────────────────────────────────────── */

static void cmd_bench_primes_near(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 500u;
    uint64_t seed = 42u;
    uint64_t radius = 1000u;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--radius") == 0 && i + 1 < argc) { radius = parse_u64(argv[++i], "radius"); }
    }

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    if (db_lo > db_hi) { printf("Constrained range is empty.\n"); return; }

    printf("=== benchmark-primes-near ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  radius=%" PRIu64 "  range=%" PRIu64 "..%" PRIu64 "\n",
        count, seed, radius, db_lo, db_hi);

    size_t limit = 200u;
    uint64_t* buf = (uint64_t*)xmalloc(limit * sizeof(uint64_t));
    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t rng = seed;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        uint64_t n = rand_range(&rng, db_lo, db_hi);
        uint64_t lo = n > radius ? n - radius : db_lo;
        uint64_t hi = n + radius < db_hi ? n + radius : db_hi;
        double t0 = now_sec();
        (void)cpss_list_range_hot(s, lo, hi, buf, limit);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
    }

    double wall1 = now_sec();
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);
    print_latency(&lr, "primes-near");
    print_cache_delta(&snap0, &snap1);
    free(latencies);
    free(buf);
}

/* ── benchmark-range-stats ─────────────────────────────────────────── */

static void cmd_bench_range_stats(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 100u;
    uint64_t seed = 42u;
    uint64_t max_width = 10000000u;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--max-width") == 0 && i + 1 < argc) { max_width = parse_u64(argv[++i], "max-width"); }
    }

    cpss_build_segment_index(s);
    cpss_build_prime_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    if (db_lo > db_hi) { printf("Constrained range is empty.\n"); return; }

    printf("=== benchmark-range-stats ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  max-width=%" PRIu64 "  range=%" PRIu64 "..%" PRIu64 "\n",
        count, seed, max_width, db_lo, db_hi);

    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t rng = seed;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        uint64_t w = rand_range(&rng, 1000u, max_width);
        uint64_t lo = rand_range(&rng, db_lo, db_hi > w ? db_hi - w : db_lo);
        uint64_t hi = lo + w;
        if (hi > db_hi) hi = db_hi;
        double t0 = now_sec();
        (void)cpss_range_stats(s, lo, hi);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
    }

    double wall1 = now_sec();
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);
    print_latency(&lr, "range-stats");
    print_cache_delta(&snap0, &snap1);
    free(latencies);
}

/* ── benchmark-factor-support ──────────────────────────────────────── */

static void cmd_bench_factor_support(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 100u;
    uint64_t seed = 42u;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
    }

    cpss_build_segment_index(s);
    cpss_build_prime_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    if (db_lo > db_hi) { printf("Constrained range is empty.\n"); return; }

    printf("=== benchmark-factor-support ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  range=%" PRIu64 "..%" PRIu64 "\n", count, seed, db_lo, db_hi);

    size_t limit = 10000u;
    uint64_t* buf = (uint64_t*)xmalloc(limit * sizeof(uint64_t));
    double* latencies = (double*)xmalloc(count * sizeof(double));
    uint64_t rng = seed;
    CacheSnapshot snap0 = snap_cache();
    double wall0 = now_sec();

    for (size_t i = 0; i < count; ++i) {
        uint64_t n = rand_range(&rng, db_lo * db_lo < db_hi ? db_lo * db_lo : db_lo, db_hi);
        uint64_t sqrt_n = isqrt_u64(n);
        if (sqrt_n > db_hi) sqrt_n = db_hi;
        double t0 = now_sec();
        (void)cpss_count_range_hot(s, 2u, sqrt_n);
        (void)cpss_list_range_hot(s, 2u, sqrt_n, buf, limit);
        double t1 = now_sec();
        latencies[i] = (t1 - t0) * 1e6;
    }

    double wall1 = now_sec();
    CacheSnapshot snap1 = snap_cache();
    LatencyReport lr = compute_latency(latencies, count, wall1 - wall0);
    print_latency(&lr, "factor-support");
    print_cache_delta(&snap0, &snap1);
    free(latencies);
    free(buf);
}

/* ── benchmark-semiprime-factorisation ─────────────────────────────── */

/** Per-method result for the semiprime factorisation benchmark. */
typedef struct {
    const char* method;
    bool succeeded;
    uint64_t found_p, found_q;
    double time_sec;
} SemiprimeMethodResult;

/** Thread argument for running a single factorisation method. */
typedef struct {
    CPSSStream* stream;
    U128 n;
    uint64_t B1;              /* for p-1 */
    SemiprimeMethodResult result;
} SemiprimeWorkerArg;

static DWORD WINAPI semiprime_worker_trial(LPVOID param) {
    SemiprimeWorkerArg* arg = (SemiprimeWorkerArg*)param;
    arg->result.method = "trial";
    double t0 = now_sec();
    FactorResultU128 fr = cpss_factor_trial_u128(arg->stream, arg->n, 0u);
    double t1 = now_sec();
    arg->result.time_sec = t1 - t0;
    arg->result.succeeded = fr.fully_factored;
    if (fr.factor_count >= 1u) arg->result.found_p = fr.factors[0];
    if (fr.factor_count >= 2u) arg->result.found_q = fr.factors[1];
    else if (fr.factor_count == 1u && u128_fits_u64(fr.cofactor) && fr.cofactor.lo > 1u)
        arg->result.found_q = fr.cofactor.lo;
    return 0;
}

static DWORD WINAPI semiprime_worker_pminus1(LPVOID param) {
    SemiprimeWorkerArg* arg = (SemiprimeWorkerArg*)param;
    arg->result.method = "pminus1";
    double t0 = now_sec();
    FactorResultU128 fr = cpss_factor_pminus1_u128(arg->stream, arg->n, arg->B1);
    double t1 = now_sec();
    arg->result.time_sec = t1 - t0;
    arg->result.succeeded = fr.fully_factored;
    if (fr.factor_count >= 1u) arg->result.found_p = fr.factors[0];
    if (fr.factor_count >= 2u) arg->result.found_q = fr.factors[1];
    else if (fr.factor_count == 1u && u128_fits_u64(fr.cofactor) && fr.cofactor.lo > 1u)
        arg->result.found_q = fr.cofactor.lo;
    return 0;
}

static DWORD WINAPI semiprime_worker_pplus1(LPVOID param) {
    SemiprimeWorkerArg* arg = (SemiprimeWorkerArg*)param;
    arg->result.method = "pplus1";
    double t0 = now_sec();
    FactorResultU128 fr = cpss_factor_pplus1_u128(arg->stream, arg->n, arg->B1);
    double t1 = now_sec();
    arg->result.time_sec = t1 - t0;
    arg->result.succeeded = fr.fully_factored;
    if (fr.factor_count >= 1u) arg->result.found_p = fr.factors[0];
    if (fr.factor_count >= 2u) arg->result.found_q = fr.factors[1];
    else if (fr.factor_count == 1u && u128_fits_u64(fr.cofactor) && fr.cofactor.lo > 1u)
        arg->result.found_q = fr.cofactor.lo;
    return 0;
}

static DWORD WINAPI semiprime_worker_auto(LPVOID param) {
    SemiprimeWorkerArg* arg = (SemiprimeWorkerArg*)param;
    arg->result.method = "auto";
    double t0 = now_sec();
    FactorResultU128 fr = cpss_factor_auto_u128(arg->stream, arg->n);
    double t1 = now_sec();
    arg->result.time_sec = t1 - t0;
    arg->result.succeeded = fr.fully_factored;
    if (fr.factor_count >= 1u) arg->result.found_p = fr.factors[0];
    if (fr.factor_count >= 2u) arg->result.found_q = fr.factors[1];
    else if (fr.factor_count == 1u && u128_fits_u64(fr.cofactor) && fr.cofactor.lo > 1u)
        arg->result.found_q = fr.cofactor.lo;
    return 0;
}

/**
 * Generate semiprimes, then race all factorisation methods against each other.
 * Each method runs in its own thread. Reports per-method success/failure/time.
 * Uses portable U128 — supports up to 128-bit semiprimes.
 */
static void cmd_bench_semiprime_factorisation(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t cases = 5u;
    unsigned bits = 40u;
    uint64_t seed = 42u;
    uint64_t B1 = 0u;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--cases") == 0 && i + 1 < argc) cases = (size_t)parse_u64(argv[++i], "cases");
        else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) bits = (unsigned)parse_u64(argv[++i], "bits");
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = parse_u64(argv[++i], "seed");
        else if (strcmp(argv[i], "--B1") == 0 && i + 1 < argc) B1 = parse_u64(argv[++i], "B1");
    }

    if (bits < 4u) bits = 4u;
    if (bits > 128u) bits = 128u;

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }

    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    (void)constrain_lo;
    unsigned half_bits = bits / 2u;
    if (half_bits < 2u) half_bits = 2u;

    uint64_t factor_max = (half_bits >= 64u) ? db_hi : ((1ull << half_bits) - 1u);
    if (factor_max > db_hi) factor_max = db_hi;
    if (factor_max < 3u) factor_max = 3u;

    U128 max_product = u128_mul64(factor_max, factor_max);

    /* Enter read-only cache mode for thread safety */
    SavedCacheBudgets saved = bench_disable_cache_fill();

    printf("=== benchmark-semiprime-factorisation ===\n");
    printf("  cases=%zu  bits=%u  half_bits=%u  seed=%" PRIu64 "\n", cases, bits, half_bits, seed);
    printf("  factor_max         = %" PRIu64 "\n", factor_max);
    {
        char mbuf[64];
        u128_to_str(max_product, mbuf, sizeof(mbuf));
        printf("  max product        = %s (%u-bit)\n", mbuf, u128_bits(max_product));
    }
    printf("  methods            = trial, pminus1, pplus1, auto (parallel per case)\n");
    if (B1 > 0u) printf("  p-1 B1             = %" PRIu64 "\n", B1);
    printf("\n");

    uint64_t rng = seed;
    size_t trial_wins = 0u, pm1_wins = 0u, pp1_wins = 0u, auto_wins = 0u;
    size_t trial_ok = 0u, pm1_ok = 0u, pp1_ok = 0u, auto_ok = 0u;

    for (size_t c = 0u; c < cases; ++c) {
        uint64_t p = cpss_next_prime(s, rand_range(&rng, 3u, factor_max));
        if (p == 0u || p > factor_max) p = cpss_next_prime(s, 2u);
        uint64_t q = cpss_next_prime(s, rand_range(&rng, 3u, factor_max));
        if (q == 0u || q > factor_max) q = cpss_next_prime(s, p);
        if (q == p) q = cpss_next_prime(s, p);
        if (q == 0u) { printf("  case %zu: could not generate distinct primes\n", c); continue; }
        if (p > q) { uint64_t t = p; p = q; q = t; }

        U128 n = u128_mul64(p, q);
        char nbuf[64];
        u128_to_str(n, nbuf, sizeof(nbuf));
        printf("  case %zu: n=%s  p=%" PRIu64 " q=%" PRIu64 "\n", c, nbuf, p, q);

        /* Set up workers — one per method */
        #define NUM_METHODS 4
        SemiprimeWorkerArg args[NUM_METHODS];
        memset(args, 0, sizeof(args));
        for (int m = 0; m < NUM_METHODS; ++m) {
            args[m].stream = s;
            args[m].n = n;
            args[m].B1 = B1;
        }

        HANDLE threads[NUM_METHODS];
        threads[0] = CreateThread(NULL, 0, semiprime_worker_trial, &args[0], 0, NULL);
        threads[1] = CreateThread(NULL, 0, semiprime_worker_pminus1, &args[1], 0, NULL);
        threads[2] = CreateThread(NULL, 0, semiprime_worker_pplus1, &args[2], 0, NULL);
        threads[3] = CreateThread(NULL, 0, semiprime_worker_auto, &args[3], 0, NULL);

        WaitForMultipleObjects(NUM_METHODS, threads, TRUE, INFINITE);
        for (int m = 0; m < NUM_METHODS; ++m) CloseHandle(threads[m]);

        /* Report results */
        double best_time = 1e30;
        const char* winner = "none";
        for (int m = 0; m < NUM_METHODS; ++m) {
            SemiprimeMethodResult* r = &args[m].result;
            bool correct = false;
            if (r->succeeded) {
                correct = ((r->found_p == p && r->found_q == q) ||
                           (r->found_p == q && r->found_q == p));
            }
            printf("    %-8s: %s  %.6fs",
                r->method,
                r->succeeded ? (correct ? "OK" : "WRONG") : "FAIL",
                r->time_sec);
            if (r->succeeded && correct) {
                printf("  %" PRIu64 " x %" PRIu64, r->found_p, r->found_q);
                if (r->time_sec < best_time) { best_time = r->time_sec; winner = r->method; }
            }
            printf("\n");

            if (strcmp(r->method, "trial") == 0 && r->succeeded) ++trial_ok;
            else if (strcmp(r->method, "pminus1") == 0 && r->succeeded) ++pm1_ok;
            else if (strcmp(r->method, "pplus1") == 0 && r->succeeded) ++pp1_ok;
            else if (strcmp(r->method, "auto") == 0 && r->succeeded) ++auto_ok;
        }
        printf("    winner: %s (%.6fs)\n\n", winner, best_time);
        if (strcmp(winner, "trial") == 0) ++trial_wins;
        else if (strcmp(winner, "pminus1") == 0) ++pm1_wins;
        else if (strcmp(winner, "pplus1") == 0) ++pp1_wins;
        else if (strcmp(winner, "auto") == 0) ++auto_wins;
        #undef NUM_METHODS
    }

    bench_restore_cache_fill(&saved);

    printf("=== summary ===\n");
    printf("  cases              = %zu\n", cases);
    printf("  trial:   %zu/%zu solved, %zu fastest\n", trial_ok, cases, trial_wins);
    printf("  pminus1: %zu/%zu solved, %zu fastest\n", pm1_ok, cases, pm1_wins);
    printf("  pplus1:  %zu/%zu solved, %zu fastest\n", pp1_ok, cases, pp1_wins);
    printf("  auto:    %zu/%zu solved, %zu fastest\n", auto_ok, cases, auto_wins);
}

/* ── benchmark-pminus1 ─────────────────────────────────────────────── */

static void cmd_bench_pminus1(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 50u;
    unsigned bits = 40u;
    uint64_t seed = 42u;
    uint64_t B1 = 0u;
    const char* cache_mode = "normal";
    uint32_t threads = g_app_threads;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) count = (size_t)parse_u64(argv[++i], "count");
        else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) bits = (unsigned)parse_u64(argv[++i], "bits");
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = parse_u64(argv[++i], "seed");
        else if (strcmp(argv[i], "--B1") == 0 && i + 1 < argc) B1 = parse_u64(argv[++i], "B1");
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) cache_mode = argv[++i];
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) threads = (uint32_t)parse_u64(argv[++i], "threads");
    }

    if (bits < 4u) bits = 4u;
    if (bits > (unsigned)(BN_MAX_LIMBS * 64)) bits = (unsigned)(BN_MAX_LIMBS * 64);

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }

    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    (void)constrain_lo;
    unsigned half_bits = bits / 2u;
    if (half_bits < 2u) half_bits = 2u;

    /* Decide whether to use DB primes or self-generated random primes.
     * Use random primes when half_bits exceeds what the DB can provide. */
    uint64_t db_bits = 0u;
    { uint64_t v = db_hi; while (v > 0u) { ++db_bits; v >>= 1u; } }
    bool use_random_primes = (half_bits > db_bits);
    bool use_bignum = (bits > 128u);

    uint64_t factor_max = 0u;
    if (!use_random_primes) {
        factor_max = (half_bits >= 64u) ? db_hi : ((1ull << half_bits) - 1u);
        if (factor_max > db_hi) factor_max = db_hi;
        if (factor_max < 3u) factor_max = 3u;
    }

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);
    bool forced_no_fill = false;
    threads = bench_effective_threads(count, threads);
    if (threads > 1u && !no_fill) {
        forced_no_fill = true;
        no_fill = true;
    }

    printf("=== benchmark-pminus1 ===\n");
    printf("  count=%zu  bits=%u  half_bits=%u  seed=%" PRIu64 "\n", count, bits, half_bits, seed);
    if (use_random_primes) {
        printf("  prime source       = random (Miller-Rabin, %u-bit factors)\n", half_bits);
    }
    else {
        printf("  prime source       = DB (factor_max=%" PRIu64 ")\n", factor_max);
    }
    if (B1 > 0u) printf("  B1                 = %" PRIu64 "\n", B1);
    else printf("  B1                 = default (min(1M, db_hi))\n");
    printf("  engine             = %s\n", use_bignum ? "BigNum (Montgomery)" : "U128");
    printf("  threads            = %u\n", threads);
    printf("  cache-mode         = %s\n", no_fill ? "no-fill" : cache_mode);
    if (forced_no_fill) {
        printf("  note               = threaded runs force cache no-fill/read-only\n");
    }
    printf("\n");

    if (use_random_primes) {
        printf("  generating %zu random %u-bit semiprimes...\n", count, bits);
    }

    SavedCacheBudgets saved = { 0 };
    if (no_fill) saved = bench_disable_cache_fill();
    CacheSnapshot snap0 = snap_cache();

    uint64_t rng = seed;
    double total_gen_sec = 0.0;
    BenchFactorCase* cases = (BenchFactorCase*)xmalloc(count * sizeof(BenchFactorCase));
    BenchFactorOutcome* outcomes = (BenchFactorOutcome*)xmalloc(count * sizeof(BenchFactorOutcome));

    for (size_t ci = 0u; ci < count; ++ci) {
        if (use_random_primes) {
            BigNum bn_p, bn_q;
            double gen0 = now_sec();
            cases[ci].kind = BENCH_FACTOR_CASE_BN;
            bn_random_prime(&bn_p, half_bits, &rng);
            bn_random_prime(&bn_q, half_bits, &rng);
            bn_mul(&cases[ci].nbn, &bn_p, &bn_q);
            {
                double gen_elapsed = now_sec() - gen0;
                total_gen_sec += gen_elapsed;
                if (ci == 0u) {
                    printf("  first case: p=%u-bit q=%u-bit n=%u-bit (gen=%.3fs)\n",
                        bn_bitlen(&bn_p), bn_bitlen(&bn_q), bn_bitlen(&cases[ci].nbn), gen_elapsed);
                }
            }
        }
        else if (use_bignum) {
            uint64_t p, q;
            BigNum bn_p, bn_q;
            bench_random_db_prime_pair(s, factor_max, &rng, &p, &q);
            cases[ci].kind = BENCH_FACTOR_CASE_BN;
            bn_from_u64(&bn_p, p);
            bn_from_u64(&bn_q, q);
            bn_mul(&cases[ci].nbn, &bn_p, &bn_q);
            if (ci == 0u) {
                printf("  first case: p=%" PRIu64 " q=%" PRIu64 " n=%u-bit\n",
                    p, q, bn_bitlen(&cases[ci].nbn));
            }
        }
        else {
            uint64_t p, q;
            bench_random_db_prime_pair(s, factor_max, &rng, &p, &q);
            cases[ci].kind = BENCH_FACTOR_CASE_U128;
            cases[ci].n128 = u128_mul64(p, q);
            if (ci == 0u) {
                char nbuf[64];
                u128_to_str(cases[ci].n128, nbuf, sizeof(nbuf));
                printf("  first case: p=%" PRIu64 " q=%" PRIu64 " n=%s\n", p, q, nbuf);
            }
        }
    }

    {
        BenchFactorShared shared;
        shared.cases = cases;
        shared.outcomes = outcomes;
        shared.stream = s;
        shared.param1 = B1;
        shared.param2 = 0u;
        bench_run_parallel(count, threads, bench_worker_pminus1, &shared);
    }

    {
        FactorBenchSummary summary = summarize_factor_bench(outcomes, count);
        print_factor_bench_summary(count, &summary, total_gen_sec);
    }

    if (no_fill) bench_restore_cache_fill(&saved);
    CacheSnapshot snap1 = snap_cache();
    print_cache_delta(&snap0, &snap1);
    free(outcomes);
    free(cases);
}

/* ── benchmark-iter ────────────────────────────────────────────────── */

static void cmd_bench_iter(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 10000u;
    uint64_t seed = 42u;
    const char* cache_mode = "normal";

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) { cache_mode = argv[++i]; }
    }

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);

    printf("=== benchmark-iter ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  cache-mode=%s\n", count, seed, cache_mode);
    printf("  range=%" PRIu64 "..%" PRIu64 "\n", db_lo, db_hi);

    SavedCacheBudgets saved_budgets = { 0 };
    if (no_fill) saved_budgets = bench_disable_cache_fill();
    CacheSnapshot snap0 = snap_cache();

    /* Pick random start points and iterate count primes from each */
    uint64_t rng = seed;
    size_t n_starts = 10u;
    if (count < 100u) n_starts = 1u;
    size_t per_start = count / n_starts;
    double* us_arr = (double*)xmalloc(n_starts * sizeof(double));
    uint64_t total_primes = 0u;

    double wall0 = now_sec();
    for (size_t si = 0u; si < n_starts; ++si) {
        uint64_t start = rand_range(&rng, db_lo, db_hi);
        PrimeIterator iter;
        double t0 = now_sec();
        cpss_iter_init(&iter, s, start);
        size_t got = 0u;
        while (iter.valid && got < per_start) {
            ++got;
            cpss_iter_next(&iter);
        }
        double t1 = now_sec();
        us_arr[si] = (t1 - t0) * 1e6;
        total_primes += got;
    }
    double wall1 = now_sec();

    printf("  total primes iterated = %" PRIu64 "\n", total_primes);
    printf("  wall time             = %.3fs\n", wall1 - wall0);
    if (wall1 - wall0 > 0.0)
        printf("  throughput            = %.0f primes/sec\n", (double)total_primes / (wall1 - wall0));

    LatencyReport lr = compute_latency(us_arr, n_starts, wall1 - wall0);
    print_latency(&lr, "iter-batch");

    if (no_fill) bench_restore_cache_fill(&saved_budgets);
    CacheSnapshot snap1 = snap_cache();
    print_cache_delta(&snap0, &snap1);
    free(us_arr);
}

/* ── benchmark-factor-trial ───────────────────────────────────────── */

static void cmd_bench_factor_trial(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 100u;
    uint64_t seed = 42u;
    unsigned bits = 40u;
    const char* corpus = "random-composite";
    const char* cache_mode = "normal";
    uint32_t threads = g_app_threads;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) { bits = (unsigned)parse_u64(argv[++i], "bits"); }
        else if (strcmp(argv[i], "--corpus") == 0 && i + 1 < argc) { corpus = argv[++i]; }
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) { cache_mode = argv[++i]; }
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) { threads = (uint32_t)parse_u64(argv[++i], "threads"); }
    }

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    (void)constrain_lo;

    if (bits > 63u) bits = 63u;
    if (bits < 4u) bits = 4u;

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);
    bool forced_no_fill = false;
    threads = bench_effective_threads(count, threads);
    if (threads > 1u && !no_fill) {
        forced_no_fill = true;
        no_fill = true;
    }

    printf("=== benchmark-factor-trial ===\n");
    printf("  count=%zu  bits=%u  corpus=%s  seed=%" PRIu64 "\n", count, bits, corpus, seed);
    printf("  db_hi=%" PRIu64 "  threads=%u  cache-mode=%s\n", db_hi, threads, no_fill ? "no-fill" : cache_mode);
    if (forced_no_fill) {
        printf("  note               = threaded runs force cache no-fill/read-only\n");
    }

    SavedCacheBudgets saved_budgets = { 0 };
    if (no_fill) saved_budgets = bench_disable_cache_fill();
    CacheSnapshot snap0 = snap_cache();

    uint64_t rng = seed;
    BenchU64Case* cases = (BenchU64Case*)xmalloc(count * sizeof(BenchU64Case));
    BenchU64Outcome* outcomes = (BenchU64Outcome*)xmalloc(count * sizeof(BenchU64Outcome));

    for (size_t ci = 0u; ci < count; ++ci) {
        if (strcmp(corpus, "semiprime") == 0) {
            unsigned half = bits / 2u;
            uint64_t p_max;
            uint64_t p, q;
            if (half < 2u) half = 2u;
            p_max = ((uint64_t)1u << half) - 1u;
            if (p_max > db_hi) p_max = db_hi;
            bench_random_db_prime_pair(s, p_max, &rng, &p, &q);
            cases[ci].n = p * q;
        }
        else {
            cases[ci].n = bench_random_composite_u64(bits, &rng);
        }
    }

    {
        BenchU64Shared shared;
        shared.cases = cases;
        shared.outcomes = outcomes;
        shared.stream = s;
        double wall0 = now_sec();
        bench_run_parallel(count, threads, bench_worker_factor_trial, &shared);
        {
            double wall1 = now_sec();
            double* us_arr = (double*)xmalloc(count * sizeof(double));
            size_t fully_factored = 0u;
            size_t incomplete = 0u;
            for (size_t ci = 0u; ci < count; ++ci) {
                us_arr[ci] = outcomes[ci].latency_us;
                if (outcomes[ci].fully_factored) ++fully_factored;
                else ++incomplete;
            }

            {
                LatencyReport lr = compute_latency(us_arr, count, wall1 - wall0);
                print_latency(&lr, "factor-trial");
            }

            printf("  fully factored        = %zu / %zu\n", fully_factored, count);
            printf("  incomplete            = %zu\n", incomplete);
            free(us_arr);
        }
    }

    if (no_fill) bench_restore_cache_fill(&saved_budgets);
    CacheSnapshot snap1 = snap_cache();
    print_cache_delta(&snap0, &snap1);
    free(outcomes);
    free(cases);
}

/* ── benchmark-classify ───────────────────────────────────────────── */

static void cmd_bench_classify(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 1000u;
    uint64_t seed = 42u;
    const char* cache_mode = "normal";

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) { cache_mode = argv[++i]; }
    }

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_lo = segment_start_n(&s->index_entries[0]);
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_lo > db_lo) db_lo = constrain_lo;
    if (constrain_hi < db_hi) db_hi = constrain_hi;

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);

    printf("=== benchmark-classify ===\n");
    printf("  count=%zu  seed=%" PRIu64 "  cache-mode=%s\n", count, seed, cache_mode);
    printf("  range=%" PRIu64 "..%" PRIu64 "\n", db_lo, db_hi);

    SavedCacheBudgets saved_budgets = { 0 };
    if (no_fill) saved_budgets = bench_disable_cache_fill();
    CacheSnapshot snap0 = snap_cache();

    uint64_t rng = seed;
    double* us_arr = (double*)xmalloc(count * sizeof(double));
    size_t n_prime = 0u, n_even = 0u, n_square = 0u, n_power = 0u, n_small_factor = 0u;

    double wall0 = now_sec();
    for (size_t ci = 0u; ci < count; ++ci) {
        uint64_t n = rand_range(&rng, 2u, db_hi);
        double t0 = now_sec();
        CompositeClass cls = cpss_classify(s, n);
        double t1 = now_sec();
        us_arr[ci] = (t1 - t0) * 1e6;

        if (cls.is_prime) ++n_prime;
        if (cls.is_even) ++n_even;
        if (cls.is_perfect_square) ++n_square;
        if (cls.is_prime_power) ++n_power;
        if (cls.has_small_factor) ++n_small_factor;
    }
    double wall1 = now_sec();

    LatencyReport lr = compute_latency(us_arr, count, wall1 - wall0);
    print_latency(&lr, "classify");

    printf("  breakdown: prime=%zu even=%zu square=%zu power=%zu small_factor=%zu\n",
        n_prime, n_even, n_square, n_power, n_small_factor);

    if (no_fill) bench_restore_cache_fill(&saved_budgets);
    CacheSnapshot snap1 = snap_cache();
    print_cache_delta(&snap0, &snap1);
    free(us_arr);
}

/* ── benchmark-factor-auto ────────────────────────────────────────── */

static void cmd_bench_factor_auto(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    size_t count = 50u;
    uint64_t seed = 42u;
    unsigned bits = 40u;
    const char* corpus = "mixed";
    const char* cache_mode = "normal";
    uint32_t threads = g_app_threads;

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) { count = (size_t)parse_u64(argv[++i], "count"); }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) { seed = parse_u64(argv[++i], "seed"); }
        else if (strcmp(argv[i], "--bits") == 0 && i + 1 < argc) { bits = (unsigned)parse_u64(argv[++i], "bits"); }
        else if (strcmp(argv[i], "--corpus") == 0 && i + 1 < argc) { corpus = argv[++i]; }
        else if (strcmp(argv[i], "--cache-mode") == 0 && i + 1 < argc) { cache_mode = argv[++i]; }
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) { threads = (uint32_t)parse_u64(argv[++i], "threads"); }
    }

    cpss_build_segment_index(s);
    if (s->index_len == 0u) { printf("No segments loaded.\n"); return; }
    uint64_t db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (constrain_hi < db_hi) db_hi = constrain_hi;
    (void)constrain_lo;
    if (strcmp(corpus, "mixed") != 0 &&
        strcmp(corpus, "prime") != 0 &&
        strcmp(corpus, "semiprime") != 0) {
        die("benchmark-factor-auto corpus must be mixed, prime, or semiprime");
    }

    if (bits < 4u) bits = 4u;
    if (bits > (unsigned)(BN_MAX_LIMBS * 64)) {
        die("benchmark-factor-auto bits exceed current BigNum limit");
    }

    uint64_t db_bits = 0u;
    for (uint64_t v = db_hi; v > 0u; v >>= 1u) ++db_bits;

    bool no_fill = (strcmp(cache_mode, "no-fill") == 0);
    bool forced_no_fill = false;
    threads = bench_effective_threads(count, threads);
    if (threads > 1u && !no_fill) {
        forced_no_fill = true;
        no_fill = true;
    }

    printf("=== benchmark-factor-auto ===\n");
    printf("  count=%zu  bits=%u  corpus=%s  seed=%" PRIu64 "\n", count, bits, corpus, seed);
    printf("  db_hi=%" PRIu64 " (~%" PRIu64 "-bit coverage)  threads=%u  cache-mode=%s\n",
        db_hi, db_bits, threads, no_fill ? "no-fill" : cache_mode);
    printf("  engine             = %s\n",
        bits <= 64u ? "uint64 auto" :
        bits <= 128u ? "U128 auto" :
        "BigNum auto (p-1, p+1, rho, ECM)");
    if (strcmp(corpus, "semiprime") == 0) {
        unsigned p_bits = bits / 2u;
        unsigned q_bits = bits > p_bits ? bits - p_bits : p_bits;
        if (p_bits < 2u) p_bits = 2u;
        if (q_bits < 2u) q_bits = 2u;
        printf("  case source        = balanced random semiprimes (%u-bit factor, %u-bit cofactor)\n",
            p_bits, q_bits);
    }
    else if (strcmp(corpus, "prime") == 0) {
        printf("  case source        = random exact-bit probable primes\n");
    }
    else {
        printf("  case source        = random exact-bit integers\n");
    }
    if (forced_no_fill) {
        printf("  note               = threaded runs force cache no-fill/read-only\n");
    }

    SavedCacheBudgets saved_budgets = { 0 };
    if (no_fill) saved_budgets = bench_disable_cache_fill();
    CacheSnapshot snap0 = snap_cache();

    uint64_t rng = seed;
    double total_gen_sec = 0.0;
    BenchFactorCase* cases = (BenchFactorCase*)xmalloc(count * sizeof(BenchFactorCase));
    BenchFactorOutcome* outcomes = (BenchFactorOutcome*)xmalloc(count * sizeof(BenchFactorOutcome));
    for (size_t ci = 0u; ci < count; ++ci) {
        double gen0 = now_sec();
        cases[ci].seed = rand_range(&rng, 1u, UINT64_MAX - 1u);
        if (strcmp(corpus, "semiprime") == 0) {
            unsigned p_bits = bits / 2u;
            unsigned q_bits = bits > p_bits ? bits - p_bits : p_bits;
            if (p_bits < 2u) p_bits = 2u;
            if (q_bits < 2u) q_bits = 2u;

            if (bits <= 64u) {
                uint64_t p = 0u;
                uint64_t q = 0u;
                uint64_t n = 0u;
                do {
                    p = random_prime_u64_bits(&rng, p_bits);
                    q = random_prime_u64_bits(&rng, q_bits);
                    n = p * q;
                } while (u64_bitlen(n) != bits);
                cases[ci].kind = BENCH_FACTOR_CASE_U64;
                cases[ci].n64 = n;
                if (ci == 0u) {
                    printf("  first case: p=%" PRIu64 " q=%" PRIu64 " n=%" PRIu64 "\n", p, q, n);
                }
            }
            else if (bits <= 128u) {
                uint64_t p = 0u;
                uint64_t q = 0u;
                U128 n = u128_from_u64(0u);
                do {
                    p = random_prime_u64_bits(&rng, p_bits);
                    q = random_prime_u64_bits(&rng, q_bits);
                    n = u128_mul64(p, q);
                } while (u128_bits(n) != bits);
                cases[ci].kind = BENCH_FACTOR_CASE_U128;
                cases[ci].n128 = n;
                if (ci == 0u) {
                    char nbuf[64];
                    u128_to_str(n, nbuf, sizeof(nbuf));
                    printf("  first case: p=%" PRIu64 " q=%" PRIu64 " n=%s\n", p, q, nbuf);
                }
            }
            else {
                BigNum p, q, n;
                do {
                    bn_random_prime(&p, p_bits, &rng);
                    bn_random_prime(&q, q_bits, &rng);
                    bn_mul(&n, &p, &q);
                } while (bn_bitlen(&n) != bits);
                bench_factor_case_from_bn(&cases[ci], &n);
                if (ci == 0u) {
                    printf("  first case: p=%u-bit q=%u-bit n=%u-bit\n",
                        bn_bitlen(&p), bn_bitlen(&q), bn_bitlen(&n));
                }
            }
        }
        else if (strcmp(corpus, "prime") == 0) {
            if (bits <= 64u) {
                cases[ci].kind = BENCH_FACTOR_CASE_U64;
                cases[ci].n64 = random_prime_u64_bits(&rng, bits);
                if (ci == 0u) {
                    printf("  first case: prime=%" PRIu64 "\n", cases[ci].n64);
                }
            }
            else {
                BigNum n;
                bn_random_prime(&n, bits, &rng);
                bench_factor_case_from_bn(&cases[ci], &n);
                if (ci == 0u) {
                    if (cases[ci].kind == BENCH_FACTOR_CASE_U128) {
                        char nbuf[64];
                        u128_to_str(cases[ci].n128, nbuf, sizeof(nbuf));
                        printf("  first case: prime=%s\n", nbuf);
                    }
                    else {
                        printf("  first case: prime=%u-bit\n", bn_bitlen(&n));
                    }
                }
            }
        }
        else {
            if (bits <= 64u) {
                cases[ci].kind = BENCH_FACTOR_CASE_U64;
                cases[ci].n64 = rand_bits_u64(&rng, bits);
                if (ci == 0u) {
                    printf("  first case: n=%" PRIu64 "\n", cases[ci].n64);
                }
            }
            else {
                BigNum n;
                bench_random_bits_bn_exact(&n, bits, &rng, false);
                bench_factor_case_from_bn(&cases[ci], &n);
                if (ci == 0u) {
                    if (cases[ci].kind == BENCH_FACTOR_CASE_U128) {
                        char nbuf[64];
                        u128_to_str(cases[ci].n128, nbuf, sizeof(nbuf));
                        printf("  first case: n=%s\n", nbuf);
                    }
                    else {
                        printf("  first case: n=%u-bit\n", bn_bitlen(&n));
                    }
                }
            }
        }
        total_gen_sec += now_sec() - gen0;
    }

    {
        BenchFactorShared shared;
        shared.cases = cases;
        shared.outcomes = outcomes;
        shared.stream = s;
        shared.param1 = 0u;
        shared.param2 = 0u;
        double wall0 = now_sec();
        bench_run_parallel(count, threads, bench_worker_factor_auto, &shared);
        {
            double wall1 = now_sec();
            double* us_arr = (double*)xmalloc(count * sizeof(double));
            size_t fully_factored = 0u;
            size_t partial = 0u;
            size_t incomplete = 0u;
            size_t unavailable = 0u;
            size_t out_of_range = 0u;
            size_t other_failed = 0u;
            for (size_t ci = 0u; ci < count; ++ci) {
                us_arr[ci] = outcomes[ci].elapsed_sec * 1e6;
                if (outcomes[ci].ok) {
                    ++fully_factored;
                }
                else if (outcomes[ci].partial) {
                    ++partial;
                }
                else if (outcomes[ci].status == (int)CPSS_INCOMPLETE) {
                    ++incomplete;
                }
                else if (outcomes[ci].status == (int)CPSS_UNAVAILABLE) {
                    ++unavailable;
                }
                else if (outcomes[ci].status == (int)CPSS_OUT_OF_RANGE) {
                    ++out_of_range;
                }
                else {
                    ++other_failed;
                }
            }

            {
                LatencyReport lr = compute_latency(us_arr, count, wall1 - wall0);
                print_latency(&lr, "factor-auto");
            }

            printf("  fully factored        = %zu / %zu\n", fully_factored, count);
            printf("  partial factor found  = %zu\n", partial);
            printf("  incomplete            = %zu\n", incomplete);
            printf("  unavailable           = %zu\n", unavailable);
            printf("  out of range          = %zu\n", out_of_range);
            if (other_failed > 0u) {
                printf("  other failed          = %zu\n", other_failed);
            }
            print_bench_factor_auto_breakdown(outcomes, count);
            free(us_arr);
        }
    }

    if (no_fill) bench_restore_cache_fill(&saved_budgets);
    if (total_gen_sec > 0.0) {
        printf("  total gen time     = %.3fs (avg %.3fs/case)\n",
            total_gen_sec, total_gen_sec / (double)count);
    }
    CacheSnapshot snap1 = snap_cache();
    print_cache_delta(&snap0, &snap1);
    free(outcomes);
    free(cases);
}

typedef void (*BenchEverythingFn)(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi);

typedef struct {
    const char* name;
    BenchEverythingFn fn;
    bool requires_db;
} BenchEverythingEntry;

static bool bench_everything_has_option(int argc, char** argv, const char* opt) {
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], opt) == 0) return true;
    }
    return false;
}

static const char* bench_everything_option_value(int argc, char** argv, const char* opt) {
    for (int i = 2; i + 1 < argc; ++i) {
        if (strcmp(argv[i], opt) == 0) return argv[i + 1];
    }
    return NULL;
}

static bool bench_everything_skip_forwarded_arg(const char* opt) {
    return strcmp(opt, "--csv-out") == 0;
}

static int bench_everything_build_argv(char** outv, size_t out_cap, const char* command,
    int argc, char** argv, const char* default_count_value,
    bool add_ops_from_count, bool add_cases_from_count) {
    size_t n = 0u;
    bool has_count = false;
    bool has_ops = false;
    bool has_cases = false;
    const char* count_value = bench_everything_option_value(argc, argv, "--count");
    const char* ops_value = bench_everything_option_value(argc, argv, "--ops");
    const char* cases_value = bench_everything_option_value(argc, argv, "--cases");

    has_count = (count_value != NULL);
    has_ops = (ops_value != NULL);
    has_cases = (cases_value != NULL);

    if (out_cap < 2u) die("benchmark-everything argv buffer is too small");
    outv[n++] = argv[0];
    outv[n++] = (char*)command;

    for (int i = 2; i < argc; ++i) {
        if (bench_everything_skip_forwarded_arg(argv[i])) {
            if (strcmp(argv[i], "--csv-out") == 0 && i + 1 < argc) ++i;
            continue;
        }
        if (n >= out_cap) die("benchmark-everything argv buffer overflow");
        outv[n++] = argv[i];
    }

    if (!has_count && default_count_value) {
        if (n + 2u > out_cap) die("benchmark-everything argv buffer overflow");
        outv[n++] = "--count";
        outv[n++] = (char*)default_count_value;
        count_value = default_count_value;
    }

    if (add_ops_from_count && !has_ops) {
        const char* value = ops_value ? ops_value : count_value;
        if (value) {
            if (n + 2u > out_cap) die("benchmark-everything argv buffer overflow");
            outv[n++] = "--ops";
            outv[n++] = (char*)value;
        }
    }

    if (add_cases_from_count && !has_cases) {
        const char* value = cases_value ? cases_value : count_value;
        if (value) {
            if (n + 2u > out_cap) die("benchmark-everything argv buffer overflow");
            outv[n++] = "--cases";
            outv[n++] = (char*)value;
        }
    }

    return (int)n;
}

static void cmd_bench_everything(CPSSStream* s, int argc, char** argv,
    uint64_t constrain_lo, uint64_t constrain_hi) {
    static const BenchEverythingEntry entries[] = {
        { "benchmark-fermat", cmd_bench_fermat, false },
        { "benchmark-rho", cmd_bench_rho, false },
        { "benchmark-ecm", cmd_bench_ecm, false },
        { "benchmark-is-prime", cmd_bench_is_prime, true },
        { "benchmark-count-range", cmd_bench_count_range, true },
        { "benchmark-list-range", cmd_bench_list_range, true },
        { "benchmark-mixed", cmd_bench_mixed, true },
        { "benchmark-semiprime-factorisation", cmd_bench_semiprime_factorisation, true },
        { "benchmark-next-prime", cmd_bench_next_prime, true },
        { "benchmark-prev-prime", cmd_bench_prev_prime, true },
        { "benchmark-pi", cmd_bench_pi, true },
        { "benchmark-nth-prime", cmd_bench_nth_prime, true },
        { "benchmark-prime-gap", cmd_bench_prime_gap, true },
        { "benchmark-primes-near", cmd_bench_primes_near, true },
        { "benchmark-range-stats", cmd_bench_range_stats, true },
        { "benchmark-factor-support", cmd_bench_factor_support, true },
        { "benchmark-pminus1", cmd_bench_pminus1, true },
        { "benchmark-pplus1", cmd_bench_pplus1, true },
        { "benchmark-iter", cmd_bench_iter, true },
        { "benchmark-factor-trial", cmd_bench_factor_trial, true },
        { "benchmark-factor-auto", cmd_bench_factor_auto, true },
        { "benchmark-classify", cmd_bench_classify, true }
    };
    enum { BENCH_EVERYTHING_ARGV_CAP = 96 };
    const char* default_count_value = "5";
    const size_t total_entries = sizeof(entries) / sizeof(entries[0]);
    size_t ran = 0u;
    size_t skipped = 0u;
    bool warned_csv = false;
    double wall0 = now_sec();

    if (bench_everything_has_option(argc, argv, "--csv-out")) {
        warned_csv = true;
    }

    printf("=== benchmark-everything ===\n");
    printf("  scope              = APP benchmark suite\n");
    printf("  database           = %s\n", s ? "loaded" : "not loaded (DB-backed benchmarks will be skipped)");
    printf("  default count      = %s (used when --count is omitted)\n", default_count_value);
    printf("  notes              = --count is also mapped to benchmark-mixed --ops and\n");
    printf("                       benchmark-semiprime-factorisation --cases\n");
    printf("  excludes           = benchmark-all-sectors (run it directly)\n");
    if (warned_csv) {
        printf("  note               = --csv-out is ignored to avoid file collisions\n");
    }
    printf("\n");

    for (size_t i = 0u; i < total_entries; ++i) {
        char* sub_argv[BENCH_EVERYTHING_ARGV_CAP];
        int sub_argc;

        if (entries[i].requires_db && !s) {
            ++skipped;
            printf("--- [%zu/%zu] %s ---\n", i + 1u, total_entries, entries[i].name);
            printf("skipped: no database loaded\n\n");
            continue;
        }

        sub_argc = bench_everything_build_argv(
            sub_argv, BENCH_EVERYTHING_ARGV_CAP, entries[i].name, argc, argv,
            default_count_value,
            strcmp(entries[i].name, "benchmark-mixed") == 0,
            strcmp(entries[i].name, "benchmark-semiprime-factorisation") == 0
        );

        ++ran;
        printf("--- [%zu/%zu] %s ---\n", i + 1u, total_entries, entries[i].name);
        {
            double t0 = now_sec();
            entries[i].fn(s, sub_argc, sub_argv, constrain_lo, constrain_hi);
            printf("completed in %.3fs\n\n", now_sec() - t0);
        }
    }

    printf("=== benchmark-everything summary ===\n");
    printf("  ran                = %zu\n", ran);
    printf("  skipped            = %zu\n", skipped);
    printf("  total wall time    = %.3fs\n", now_sec() - wall0);
}

/** Entry point: initialize globals and dispatch to the appropriate CLI subcommand. */
