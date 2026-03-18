/**
 * SemiPrimeCompetitionSpace.c - Method competition, router evaluation, metric evaluation.
 * Part of the CPSS SemiPrime Spaces module (N-centric inference).
 *
 * Head-to-head method competition (method_compete), router accuracy
 * evaluation (router_eval, router_eval_file), and per-metric analysis
 * (metric_eval) across generated or file-based corpora.
 *
 * Depends on: SemiPrimeRoutingSpace.c, SemiPrimeCorpusSpace.c, cpss_factor.c
 */

/* ── Head-to-head method competition (Phase 12b) ── */

typedef enum { MSTAT_SUCCESS, MSTAT_FAILURE, MSTAT_TIMEOUT, MSTAT_UNAVAILABLE } MethodStatus;
static const char* mstat_name(MethodStatus s) {
    switch (s) { case MSTAT_SUCCESS: return "ok"; case MSTAT_FAILURE: return "fail";
                 case MSTAT_TIMEOUT: return "timeout"; default: return "n/a"; }
}

#define COMPETE_METHODS 8
static const char* COMPETE_MNAMES[COMPETE_METHODS] = { "wheel", "fermat", "rho", "pminus1", "pplus1", "trial", "squfof", "qs" };
/* Cascade order for simulated router: wheel(0), pminus1(3), pplus1(4), rho(2), squfof(6), qs(7), trial(5), fermat(1) */
static const int COMPETE_CASCADE_ORDER[COMPETE_METHODS] = { 0, 3, 4, 2, 6, 7, 5, 1 };

typedef struct {
    MethodStatus status[COMPETE_METHODS];
    double time[COMPETE_METHODS];
} CompeteResult;

static int compete_cmp_double(const void* a, const void* b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da < db) ? -1 : (da > db) ? 1 : 0;
}

static void method_compete(CPSSStream* s, int cases, int bits, uint64_t seed,
                             const char* shape, uint64_t B1_arg, uint64_t trial_limit_arg,
                             double method_timeout, const char* csv_out) {
    RouterEnv env = router_env_from_stream(s);
    uint64_t rng = seed ? seed : 0x12345678ABCDEF01ULL;
    int half = bits / 2; if (half < 2) half = 2;

    FILE* csv_fp = NULL;
    if (csv_out && csv_out[0]) {
        csv_fp = fopen(csv_out, "w");
        if (csv_fp) fprintf(csv_fp, "N,p,q,corpus_shape,observed_shape,"
            "wheel_status,wheel_time,fermat_status,fermat_time,rho_status,rho_time,"
            "pminus1_status,pminus1_time,pplus1_status,pplus1_time,trial_status,trial_time,"
            "squfof_status,squfof_time,qs_status,qs_time,"
            "auto_router_winner,auto_router_time,sim_router_winner,fastest_method,fastest_time,"
            "all_successful,router_fastest_agree\n");
    }

    printf("method-compete: shape=%s cases=%d bits=%d seed=%"PRIu64" B1=%"PRIu64" trial_limit=%"PRIu64"\n",
        shape, cases, bits, seed, B1_arg, trial_limit_arg);
    router_env_print(&env);

    /* Accumulators */
    int fastest_counts[COMPETE_METHODS]; memset(fastest_counts, 0, sizeof(fastest_counts));
    int success_counts[COMPETE_METHODS]; memset(success_counts, 0, sizeof(success_counts));
    double all_auto_times[1024], all_fastest_times[1024]; /* up to 1024 cases */
    int total = 0, disagree = 0, sim_agree = 0;

    printf("  %-4s %-10s %-10s", "#", "N", "factors");
    for (int mi = 0; mi < COMPETE_METHODS; ++mi) printf(" %-10s", COMPETE_MNAMES[mi]);
    printf(" %-8s %-8s %-8s agree\n", "auto", "sim", "fastest");

    int generated = 0;
    for (int ci = 0; ci < cases * 10 && generated < cases; ++ci) {
        uint64_t p, q;
        if (!corpus_gen_pair(&rng, half, bits, shape, &p, &q)) continue;
        uint64_t N = u128_mul64(p, q).lo;
        if (N <= 3u || miller_rabin_u64(N)) continue;

        CompeteResult cr;
        memset(&cr, 0, sizeof(cr));

        /* Method 0: wheel */
        { double t0 = now_sec();
          bool found = false;
          for (size_t wi = 0u; wi < ARRAY_LEN(WHEEL_PRIMES); ++wi)
              if (N > WHEEL_PRIMES[wi] && N % WHEEL_PRIMES[wi] == 0u) { found = true; break; }
          cr.time[0] = now_sec() - t0;
          cr.status[0] = found ? MSTAT_SUCCESS : MSTAT_FAILURE;
        }

        /* Method 1: fermat */
        { FactorResult fr = cpss_factor_fermat(N, 0u);
          cr.time[1] = fr.time_seconds;
          cr.status[1] = fr.fully_factored ? MSTAT_SUCCESS : MSTAT_FAILURE;
        }

        /* Method 2: rho */
        { FactorResult fr = cpss_factor_rho(N, 0u, 0u);
          cr.time[2] = fr.time_seconds;
          cr.status[2] = fr.fully_factored ? MSTAT_SUCCESS : MSTAT_FAILURE;
        }

        /* Methods 3-5: DB-backed */
        if (s && s->index_len > 0u) {
            { FactorResult fr = cpss_factor_pminus1(s, N, B1_arg);
              cr.time[3] = fr.time_seconds;
              cr.status[3] = fr.fully_factored ? MSTAT_SUCCESS : MSTAT_FAILURE; }
            { FactorResult fr = cpss_factor_pplus1(s, N, B1_arg);
              cr.time[4] = fr.time_seconds;
              cr.status[4] = fr.fully_factored ? MSTAT_SUCCESS : MSTAT_FAILURE; }
            { FactorResult fr = cpss_factor_trial(s, N, trial_limit_arg);
              cr.time[5] = fr.time_seconds;
              cr.status[5] = fr.fully_factored ? MSTAT_SUCCESS : MSTAT_FAILURE; }
        } else {
            cr.status[3] = MSTAT_UNAVAILABLE;
            cr.status[4] = MSTAT_UNAVAILABLE;
            cr.status[5] = MSTAT_UNAVAILABLE;
        }

        /* Method 6: SQUFOF (no DB needed, uint64 only) */
        if ((N & 1u) && !is_perfect_square_u64(N)) {
            FactorResult fr = cpss_factor_squfof(N);
            cr.time[6] = fr.time_seconds;
            cr.status[6] = fr.fully_factored ? MSTAT_SUCCESS : MSTAT_FAILURE;
        } else {
            cr.status[6] = MSTAT_FAILURE;
        }

        /* Method 7: QS (no DB needed, uint64) */
        { FactorResult fr = cpss_factor_qs_u64(N);
          cr.time[7] = fr.time_seconds;
          cr.status[7] = fr.fully_factored ? MSTAT_SUCCESS : MSTAT_FAILURE;
        }

        /* Auto-router */
        FactorResult auto_fr;
        if (s && s->index_len > 0u) auto_fr = cpss_factor_auto(s, N);
        else { auto_fr = cpss_factor_rho(N, 0u, 0u); if (!auto_fr.fully_factored) auto_fr = cpss_factor_fermat(N, 0u); }
        const char* auto_winner = method_name_from_fr(auto_fr.method);
        double auto_time = auto_fr.time_seconds;

        /* Simulated router-order winner: first success in cascade order */
        const char* sim_winner = "none";
        for (int ci2 = 0; ci2 < COMPETE_METHODS; ++ci2) {
            int mi = COMPETE_CASCADE_ORDER[ci2];
            if (cr.status[mi] == MSTAT_SUCCESS) { sim_winner = COMPETE_MNAMES[mi]; break; }
        }

        /* Fastest successful method */
        int fastest_idx = -1;
        double fastest_time = 1e30;
        char all_ok_buf[128] = "";
        for (int mi = 0; mi < COMPETE_METHODS; ++mi) {
            if (cr.status[mi] == MSTAT_SUCCESS) {
                success_counts[mi]++;
                if (all_ok_buf[0]) strcat(all_ok_buf, ",");
                strcat(all_ok_buf, COMPETE_MNAMES[mi]);
                if (cr.time[mi] < fastest_time) { fastest_time = cr.time[mi]; fastest_idx = mi; }
            }
        }
        const char* fastest_name = fastest_idx >= 0 ? COMPETE_MNAMES[fastest_idx] : "none";
        if (fastest_idx >= 0) fastest_counts[fastest_idx]++;
        bool agree = (strcmp(auto_winner, fastest_name) == 0);
        if (!agree) ++disagree;
        if (strcmp(auto_winner, sim_winner) == 0) ++sim_agree;

        /* Store times for median computation */
        if (total < 1024) {
            all_auto_times[total] = auto_time;
            all_fastest_times[total] = fastest_idx >= 0 ? fastest_time : auto_time;
        }

        /* Per-case output */
        printf("  %-4d %-10"PRIu64" %"PRIu64"*%-6"PRIu64, generated + 1, N, p, q);
        for (int mi = 0; mi < COMPETE_METHODS; ++mi) {
            if (cr.status[mi] == MSTAT_SUCCESS) printf(" %.6f  ", cr.time[mi]);
            else printf(" %-10s", mstat_name(cr.status[mi]));
        }
        printf(" %-8s %-8s %-8s %s\n", auto_winner, sim_winner, fastest_name, agree ? "YES" : "no");

        /* CSV row */
        if (csv_fp) {
            int obs_idx = classify_shape_posthoc(p, q);
            const char* obs = (obs_idx >= 0 && obs_idx < SHAPE_CLASS_COUNT) ? SHAPE_NAMES[obs_idx] : "unknown";
            fprintf(csv_fp, "%"PRIu64",%"PRIu64",%"PRIu64",%s,%s", N, p, q, shape, obs);
            for (int mi = 0; mi < COMPETE_METHODS; ++mi)
                fprintf(csv_fp, ",%s,%.6f", mstat_name(cr.status[mi]), cr.time[mi]);
            fprintf(csv_fp, ",%s,%.6f,%s,%s,%.6f,%s,%s\n",
                auto_winner, auto_time, sim_winner, fastest_name, fastest_time,
                all_ok_buf, agree ? "yes" : "no");
        }

        ++total; ++generated;
    }

    /* Summary — timing-first */
    printf("\n  summary (corpus=%s, cases=%d, DB=%s):\n", shape, total, env.db_loaded ? "yes" : "no");

    /* Median computation */
    int n_med = total < 1024 ? total : 1024;
    if (n_med > 0) {
        qsort(all_auto_times, n_med, sizeof(double), compete_cmp_double);
        qsort(all_fastest_times, n_med, sizeof(double), compete_cmp_double);
        double med_auto = all_auto_times[n_med / 2];
        double med_fast = all_fastest_times[n_med / 2];
        printf("    median auto-router time  = %.6fs\n", med_auto);
        printf("    median fastest time      = %.6fs\n", med_fast);
        if (med_fast > 0.0)
            printf("    median auto/fastest ratio = %.2f\n", med_auto / med_fast);
    }

    printf("    fastest-method frequency:\n");
    for (int mi = 0; mi < COMPETE_METHODS; ++mi) {
        if (fastest_counts[mi] > 0)
            printf("      %-10s %d/%d (%.0f%%)\n", COMPETE_MNAMES[mi], fastest_counts[mi], total,
                total > 0 ? 100.0 * fastest_counts[mi] / total : 0.0);
    }
    printf("    success rate per method:\n");
    for (int mi = 0; mi < COMPETE_METHODS; ++mi) {
        printf("      %-10s %d/%d (%.0f%%)\n", COMPETE_MNAMES[mi], success_counts[mi], total,
            total > 0 ? 100.0 * success_counts[mi] / total : 0.0);
    }
    printf("    router-vs-fastest disagreement = %d/%d (%.0f%%)\n",
        disagree, total, total > 0 ? 100.0 * disagree / total : 0.0);
    printf("    sim-vs-actual router agreement = %d/%d (%.0f%%)\n",
        sim_agree, total, total > 0 ? 100.0 * sim_agree / total : 0.0);
    if (csv_fp) { fclose(csv_fp); printf("    CSV exported to %s\n", csv_out); }
}

/* ── Router evaluation (Phase 6/9: ideal/available/actual split) ── */

/** Shared CSV row writer for router-eval / router-eval-file results.
 *  corpus_shape = generator family (e.g. "no-wheel-factor").
 *  observed_shape = geometric classification from actual p,q. */
static void eval_csv_header(FILE* fp) {
    fprintf(fp, "N,p,q,corpus_shape,observed_shape,ideal,available,actual,ideal_match,avail_match,Delta,R,near_square_score,factor_time,db_loaded\n");
}
static void eval_csv_row(FILE* fp, uint64_t N, uint64_t p, uint64_t q,
                          const char* corpus_shape,
                          const char* ideal_name, const char* avail_name,
                          const char* actual_name, bool id_match, bool av_match,
                          double factor_time, bool db_loaded) {
    double R = (double)q / (double)p;
    double ns = observable_near_square_score(N);
    int obs_idx = classify_shape_posthoc(p, q);
    const char* obs_shape = (obs_idx >= 0 && obs_idx < SHAPE_CLASS_COUNT) ? SHAPE_NAMES[obs_idx] : "unknown";
    fprintf(fp, "%"PRIu64",%"PRIu64",%"PRIu64",%s,%s,%s,%s,%s,%s,%s,%"PRIu64",%.6f,%.4f,%.6f,%s\n",
        N, p, q, corpus_shape, obs_shape, ideal_name, avail_name, actual_name,
        id_match ? "yes" : "no", av_match ? "yes" : "no",
        q - p, R, ns, factor_time, db_loaded ? "yes" : "no");
}

static void router_eval(CPSSStream* s, int cases, int bits, uint64_t seed, const char* shape, const char* csv_out) {
    RouterEnv env = router_env_from_stream(s);
    int ideal_match = 0, avail_match = 0;
    int method_counts[COV_METHOD_COUNT]; memset(method_counts, 0, sizeof(method_counts));
    int confusion_ideal[COV_METHOD_COUNT][COV_METHOD_COUNT]; memset(confusion_ideal, 0, sizeof(confusion_ideal));
    double total_time = 0.0, sum_delta = 0.0, sum_R = 0.0;
    int total = 0, factored = 0;

    uint64_t rng = seed ? seed : 0x12345678ABCDEF01ULL;
    int half = bits / 2; if (half < 2) half = 2;

    FILE* csv_fp = NULL;
    if (csv_out && csv_out[0]) {
        csv_fp = fopen(csv_out, "w");
        if (!csv_fp) printf("WARNING: cannot open '%s' for CSV output.\n", csv_out);
        else eval_csv_header(csv_fp);
    }

    printf("router-eval: shape=%s cases=%d bits=%d seed=%"PRIu64"\n", shape, cases, bits, seed);
    router_env_print(&env);
    printf("  %-6s %-12s %-12s %-8s %-8s %-8s %-6s %-6s %-10s\n",
        "#", "N", "factors", "ideal", "avail", "actual", "id=ac", "av=ac", "time");

    int generated = 0;
    for (int ci = 0; ci < cases * 10 && generated < cases; ++ci) {
        uint64_t p, q;
        if (!corpus_gen_pair(&rng, half, bits, shape, &p, &q)) continue;
        uint64_t N = u128_mul64(p, q).lo;
        if (miller_rabin_u64(N)) continue;

        CoverageMethod ideal = predict_ideal(N);
        CoverageMethod avail = predict_available(N, &env);

        FactorResult fr;
        if (s && s->index_len > 0u) fr = cpss_factor_auto(s, N);
        else { fr = cpss_factor_rho(N, 0u, 0u); if (!fr.fully_factored) fr = cpss_factor_fermat(N, 0u); }

        const char* actual_name = method_name_from_fr(fr.method);
        int ideal_idx = (int)ideal, avail_idx = (int)avail, actual_idx = method_index(actual_name);
        bool id_match = (ideal_idx == actual_idx);
        bool av_match = (avail_idx == actual_idx);

        printf("  %-6d %-12"PRIu64" %"PRIu64"*%-8"PRIu64" %-8s %-8s %-8s %-6s %-6s %.6f\n",
            generated + 1, N, p, q,
            ideal < COV_METHOD_COUNT ? MNAMES[ideal] : "?",
            avail < COV_METHOD_COUNT ? MNAMES[avail] : "?",
            actual_name, id_match ? "YES" : "no", av_match ? "YES" : "no", fr.time_seconds);

        if (csv_fp) eval_csv_row(csv_fp, N, p, q, shape,
            ideal < COV_METHOD_COUNT ? MNAMES[ideal] : "?",
            avail < COV_METHOD_COUNT ? MNAMES[avail] : "?",
            actual_name, id_match, av_match, fr.time_seconds, env.db_loaded);

        total++; if (fr.fully_factored) factored++;
        if (id_match) ideal_match++;
        if (av_match) avail_match++;
        if (actual_idx >= 0) method_counts[actual_idx]++;
        if (ideal_idx >= 0 && ideal_idx < COV_METHOD_COUNT && actual_idx >= 0)
            confusion_ideal[ideal_idx][actual_idx]++;
        total_time += fr.time_seconds;
        sum_delta += (double)(q - p);
        sum_R += (double)q / (double)p;
        ++generated;
    }

    printf("\n  summary (corpus_shape=%s):\n", shape);
    printf("    cases          = %d (factored=%d)\n", total, factored);
    printf("    total time     = %.6fs\n", total_time);
    if (total > 0) {
        printf("    mean Delta     = %.1f\n", sum_delta / total);
        printf("    mean R         = %.4f\n", sum_R / total);
        printf("    ideal accuracy = %d/%d (%.1f%%)\n", ideal_match, total, 100.0 * ideal_match / total);
        printf("    avail accuracy = %d/%d (%.1f%%)\n", avail_match, total, 100.0 * avail_match / total);
    }
    printf("    actual method distribution:\n");
    for (int mi = 0; mi < COV_METHOD_COUNT; ++mi) {
        if (method_counts[mi] > 0) printf("      %-10s %d\n", MNAMES[mi], method_counts[mi]);
    }
    /* Confusion: ideal predicted -> actual winner */
    printf("    confusion (ideal -> actual):\n");
    printf("      %-10s", "predicted");
    for (int j = 0; j < COV_METHOD_COUNT; ++j) { if (method_counts[j] > 0) printf(" %-8s", MNAMES[j]); }
    printf("\n");
    for (int i = 0; i < COV_METHOD_COUNT; ++i) {
        bool has_row = false;
        for (int j = 0; j < COV_METHOD_COUNT; ++j) { if (confusion_ideal[i][j] > 0) has_row = true; }
        if (!has_row) continue;
        printf("      %-10s", MNAMES[i]);
        for (int j = 0; j < COV_METHOD_COUNT; ++j) {
            if (method_counts[j] > 0) printf(" %-8d", confusion_ideal[i][j]);
        }
        printf("\n");
    }
    if (!env.db_loaded && ideal_match < avail_match) {
        printf("    NOTE: ideal accuracy < available because ideal assumes DB support that is absent.\n");
    }
    /* Method-region map: compact text summary of where methods tend to win */
    if (total > 0) {
        printf("    method-region map (corpus=%s, DB=%s):\n", shape, env.db_loaded ? "yes" : "no");
        for (int mi = 0; mi < COV_METHOD_COUNT; ++mi) {
            if (method_counts[mi] == 0) continue;
            double frac = 100.0 * method_counts[mi] / total;
            printf("      %-10s wins %d/%d (%.0f%%)", MNAMES[mi], method_counts[mi], total, frac);
            if (mi == COV_METHOD_WHEEL) printf(" - small wheel-prime factor present");
            else if (mi == COV_METHOD_TRIAL) printf(" - DB trial division effective");
            else if (mi == COV_METHOD_PMINUS1) printf(" - DB p-1 iteration dominant");
            else if (mi == COV_METHOD_PPLUS1) printf(" - complementary to p-1");
            else if (mi == COV_METHOD_RHO) printf(" - general-purpose fallback");
            else if (mi == COV_METHOD_FERMAT) printf(" - near-square structure exploited");
            else if (mi == COV_METHOD_ECM) printf(" - algebraic curve method");
            printf("\n");
        }
    }
    if (csv_fp) { fclose(csv_fp); printf("    CSV exported to %s\n", csv_out); }
}

/* ── Lightweight metric evaluation ── */

/** Group-by mode for metric_eval. */
typedef enum {
    METRIC_GROUP_WINNER,    /* group by actual winning method */
    METRIC_GROUP_CORPUS,    /* group by corpus_shape (generator family) */
    METRIC_GROUP_OBSERVED,  /* group by observed geometric shape from p,q */
    METRIC_GROUP_IDEAL,     /* group by ideal predicted method */
    METRIC_GROUP_AVAILABLE  /* group by available predicted method */
} MetricGroupMode;

#define METRIC_MAX_GROUPS 16
#define METRIC_NCOLS 7

/** Accumulator for one group bucket. */
typedef struct {
    int count;
    double mins[METRIC_NCOLS], maxs[METRIC_NCOLS], sums[METRIC_NCOLS];
} MetricGroupBucket;

static void mgb_init(MetricGroupBucket* b) {
    b->count = 0;
    for (int i = 0; i < METRIC_NCOLS; ++i) { b->mins[i] = 1e30; b->maxs[i] = -1e30; b->sums[i] = 0.0; }
}
static void mgb_update(MetricGroupBucket* b, const double* vals) {
    b->count++;
    for (int i = 0; i < METRIC_NCOLS; ++i) {
        if (vals[i] < b->mins[i]) b->mins[i] = vals[i];
        if (vals[i] > b->maxs[i]) b->maxs[i] = vals[i];
        b->sums[i] += vals[i];
    }
}
static void mgb_print(const MetricGroupBucket* b, const char** headers) {
    printf("  %-16s %12s %12s %12s\n", "metric", "min", "max", "mean");
    for (int i = 0; i < METRIC_NCOLS; ++i) {
        if (b->count > 0)
            printf("  %-16s %12.4f %12.4f %12.4f\n", headers[i],
                b->mins[i], b->maxs[i], b->sums[i] / b->count);
    }
}

static void metric_eval(CPSSStream* s, int cases, int bits, uint64_t seed,
                         const char* shape, MetricGroupMode group_mode) {
    static const char* headers[] = { "balance_ratio", "log_imb", "|delta|/m", "R", "fermat_B", "ns_score", "Delta" };
    RouterEnv env = router_env_from_stream(s);
    router_env_print(&env);

    /* Overall accumulator */
    MetricGroupBucket overall; mgb_init(&overall);

    /* Group buckets: max METRIC_MAX_GROUPS */
    int num_groups = 0;
    const char* group_names[METRIC_MAX_GROUPS];
    MetricGroupBucket groups[METRIC_MAX_GROUPS];
    if (group_mode == METRIC_GROUP_WINNER || group_mode == METRIC_GROUP_IDEAL || group_mode == METRIC_GROUP_AVAILABLE) {
        num_groups = COV_METHOD_COUNT;
        for (int i = 0; i < num_groups; ++i) { group_names[i] = MNAMES[i]; mgb_init(&groups[i]); }
    } else if (group_mode == METRIC_GROUP_OBSERVED) {
        num_groups = SHAPE_CLASS_COUNT;
        for (int i = 0; i < num_groups; ++i) { group_names[i] = SHAPE_NAMES[i]; mgb_init(&groups[i]); }
    } else { /* METRIC_GROUP_CORPUS: all cases share the same corpus_shape = shape param */
        num_groups = SHAPE_CLASS_COUNT;
        for (int i = 0; i < num_groups; ++i) { group_names[i] = SHAPE_NAMES[i]; mgb_init(&groups[i]); }
    }

    uint64_t rng = seed ? seed : 0x12345678ABCDEF01ULL;
    int half = bits / 2; if (half < 2) half = 2;
    int count = 0;
    for (int ci = 0; ci < cases * 10 && count < cases; ++ci) {
        uint64_t p, q;
        if (!corpus_gen_pair(&rng, half, bits, shape, &p, &q)) continue;
        uint64_t N = u128_mul64(p, q).lo;
        if (N <= 3u || miller_rabin_u64(N)) continue;

        double vals[METRIC_NCOLS];
        vals[0] = value_space_balance_ratio(p, q);
        vals[1] = value_space_log_imbalance(p, q);
        double m, delta; mean_imbalance_from_factors(p, q, &m, &delta);
        vals[2] = m > 0 ? fabs(delta) / m : 0.0;
        vals[3] = ratio_from_factors(p, q);
        uint64_t Ae, Be; bool exact = fermat_space_from_factors_exact(p, q, &Ae, &Be);
        vals[4] = exact ? (double)Be : -1.0;
        vals[5] = observable_near_square_score(N);
        vals[6] = (double)(q - p);
        mgb_update(&overall, vals);

        /* Determine group index */
        int gidx = -1;
        if (group_mode == METRIC_GROUP_CORPUS) {
            /* Match corpus shape name (the --shape parameter) against SHAPE_NAMES */
            for (int si = 0; si < SHAPE_CLASS_COUNT; ++si) {
                if (strcmp(shape, SHAPE_NAMES[si]) == 0) { gidx = si; break; }
            }
            if (gidx < 0) gidx = SHAPE_CLASS_COUNT - 1; /* mixed fallback */
        } else if (group_mode == METRIC_GROUP_OBSERVED) {
            gidx = classify_shape_posthoc(p, q);
        } else {
            /* Need factoring for winner; need predictions for ideal/available */
            FactorResult fr;
            if (s && s->index_len > 0u) fr = cpss_factor_auto(s, N);
            else { fr = cpss_factor_rho(N, 0u, 0u); if (!fr.fully_factored) fr = cpss_factor_fermat(N, 0u); }

            if (group_mode == METRIC_GROUP_WINNER) {
                gidx = method_index(method_name_from_fr(fr.method));
            } else if (group_mode == METRIC_GROUP_IDEAL) {
                gidx = (int)predict_ideal(N);
            } else { /* METRIC_GROUP_AVAILABLE */
                gidx = (int)predict_available(N, &env);
            }
        }
        if (gidx >= 0 && gidx < num_groups) mgb_update(&groups[gidx], vals);
        ++count;
    }

    const char* gmode_name = group_mode == METRIC_GROUP_WINNER ? "winner"
        : group_mode == METRIC_GROUP_CORPUS ? "corpus"
        : group_mode == METRIC_GROUP_OBSERVED ? "observed"
        : group_mode == METRIC_GROUP_IDEAL ? "ideal" : "available";
    printf("metric-eval: shape=%s cases=%d bits=%d seed=%"PRIu64" group-by=%s\n",
        shape, cases, bits, seed, gmode_name);
    printf("\n  overall (%d cases):\n", count);
    mgb_print(&overall, headers);

    /* Grouped summaries */
    for (int gi = 0; gi < num_groups; ++gi) {
        if (groups[gi].count == 0) continue;
        printf("\n  by %s=%s (%d cases):\n", gmode_name, group_names[gi], groups[gi].count);
        mgb_print(&groups[gi], headers);
    }
}

/* ── File-based router evaluation ── */

static void router_eval_file(CPSSStream* s, const char* inpath, int limit, const char* csv_out) {
    FILE* fp = fopen(inpath, "r");
    if (!fp) { printf("Cannot open '%s'.\n", inpath); return; }

    RouterEnv env = router_env_from_stream(s);
    int ideal_match = 0, avail_match = 0;
    int method_counts[COV_METHOD_COUNT]; memset(method_counts, 0, sizeof(method_counts));
    int confusion_ideal[COV_METHOD_COUNT][COV_METHOD_COUNT]; memset(confusion_ideal, 0, sizeof(confusion_ideal));
    double total_time = 0.0, sum_delta = 0.0, sum_R = 0.0;
    int total = 0, factored = 0;

    FILE* csv_fp = NULL;
    if (csv_out && csv_out[0]) {
        csv_fp = fopen(csv_out, "w");
        if (!csv_fp) printf("WARNING: cannot open '%s' for CSV output.\n", csv_out);
        else eval_csv_header(csv_fp);
    }

    printf("router-eval-file: %s\n", inpath);
    router_env_print(&env);
    printf("  %-6s %-12s %-12s %-8s %-8s %-8s %-6s %-6s %-10s\n",
        "#", "N", "factors", "ideal", "avail", "actual", "id=ac", "av=ac", "time");

    char line_buf[512];
    /* Skip header */
    if (!fgets(line_buf, sizeof(line_buf), fp)) { fclose(fp); return; }

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        if (limit > 0 && total >= limit) break;
        /* Parse: p,q,N,bits_p,bits_q,Delta,R,log_imbalance,shape */
        uint64_t p = 0u, q = 0u, N = 0u;
        if (sscanf(line_buf, "%"PRIu64",%"PRIu64",%"PRIu64, &p, &q, &N) < 3) continue;
        if (N <= 3u || p == 0u || q == 0u) continue;
        if (p > q) { uint64_t t = p; p = q; q = t; }
        /* Extract corpus_shape from last CSV column */
        char c_shape[64] = "unknown";
        { const char* last_comma = strrchr(line_buf, ',');
          if (last_comma) { strncpy(c_shape, last_comma + 1, sizeof(c_shape) - 1);
            c_shape[sizeof(c_shape) - 1] = '\0';
            /* Strip trailing whitespace/newline */
            size_t sl = strlen(c_shape);
            while (sl > 0 && (c_shape[sl-1] == '\n' || c_shape[sl-1] == '\r' || c_shape[sl-1] == ' ')) c_shape[--sl] = '\0';
          }
        }

        CoverageMethod ideal = predict_ideal(N);
        CoverageMethod avail = predict_available(N, &env);

        FactorResult fr;
        if (s && s->index_len > 0u) fr = cpss_factor_auto(s, N);
        else { fr = cpss_factor_rho(N, 0u, 0u); if (!fr.fully_factored) fr = cpss_factor_fermat(N, 0u); }

        const char* actual_name = method_name_from_fr(fr.method);
        int ideal_idx = (int)ideal, avail_idx = (int)avail, actual_idx = method_index(actual_name);
        bool id_match = (ideal_idx == actual_idx);
        bool av_match = (avail_idx == actual_idx);

        printf("  %-6d %-12"PRIu64" %"PRIu64"*%-8"PRIu64" %-8s %-8s %-8s %-6s %-6s %.6f\n",
            total + 1, N, p, q,
            ideal < COV_METHOD_COUNT ? MNAMES[ideal] : "?",
            avail < COV_METHOD_COUNT ? MNAMES[avail] : "?",
            actual_name, id_match ? "YES" : "no", av_match ? "YES" : "no", fr.time_seconds);

        if (csv_fp) eval_csv_row(csv_fp, N, p, q, c_shape,
            ideal < COV_METHOD_COUNT ? MNAMES[ideal] : "?",
            avail < COV_METHOD_COUNT ? MNAMES[avail] : "?",
            actual_name, id_match, av_match, fr.time_seconds, env.db_loaded);

        total++; if (fr.fully_factored) factored++;
        if (id_match) ideal_match++;
        if (av_match) avail_match++;
        if (actual_idx >= 0) method_counts[actual_idx]++;
        if (ideal_idx >= 0 && ideal_idx < COV_METHOD_COUNT && actual_idx >= 0)
            confusion_ideal[ideal_idx][actual_idx]++;
        total_time += fr.time_seconds;
        sum_delta += (double)(q - p);
        sum_R += (double)q / (double)p;
    }
    fclose(fp);

    printf("\n  summary:\n");
    printf("    cases          = %d (factored=%d)\n", total, factored);
    printf("    total time     = %.6fs\n", total_time);
    if (total > 0) {
        printf("    mean Delta     = %.1f\n", sum_delta / total);
        printf("    mean R         = %.4f\n", sum_R / total);
        printf("    ideal accuracy = %d/%d (%.1f%%)\n", ideal_match, total, 100.0 * ideal_match / total);
        printf("    avail accuracy = %d/%d (%.1f%%)\n", avail_match, total, 100.0 * avail_match / total);
    }
    printf("    actual method distribution:\n");
    for (int mi = 0; mi < COV_METHOD_COUNT; ++mi) {
        if (method_counts[mi] > 0) printf("      %-10s %d\n", MNAMES[mi], method_counts[mi]);
    }
    printf("    confusion (ideal -> actual):\n");
    printf("      %-10s", "predicted");
    for (int j = 0; j < COV_METHOD_COUNT; ++j) { if (method_counts[j] > 0) printf(" %-8s", MNAMES[j]); }
    printf("\n");
    for (int i = 0; i < COV_METHOD_COUNT; ++i) {
        bool has_row = false;
        for (int j = 0; j < COV_METHOD_COUNT; ++j) { if (confusion_ideal[i][j] > 0) has_row = true; }
        if (!has_row) continue;
        printf("      %-10s", MNAMES[i]);
        for (int j = 0; j < COV_METHOD_COUNT; ++j) {
            if (method_counts[j] > 0) printf(" %-8d", confusion_ideal[i][j]);
        }
        printf("\n");
    }
    if (csv_fp) { fclose(csv_fp); printf("    CSV exported to %s\n", csv_out); }
}

/* ── BigNum method-compete (Phase 15) ── */

#define BN_COMPETE_METHODS 4
static const char* BN_COMPETE_MNAMES[BN_COMPETE_METHODS] = { "fermat", "rho", "ecm", "auto" };

static void method_compete_bn(CPSSStream* s, int cases, int bits, uint64_t seed,
                                const char* shape, const char* csv_out) {
    uint64_t rng = seed ? seed : 0x12345678ABCDEF01ULL;
    int half = bits / 2; if (half < 2) half = 2;
    bool db_loaded = (s && s->index_len > 0u);

    FILE* csv_fp = NULL;
    if (csv_out && csv_out[0]) {
        csv_fp = fopen(csv_out, "w");
        if (csv_fp) fprintf(csv_fp, "N,p,q,shape,fermat_found,fermat_time,rho_found,rho_time,ecm_found,ecm_time,auto_found,auto_time,fastest\n");
    }

    printf("method-compete-bn: shape=%s cases=%d bits=%d seed=%"PRIu64" DB=%s\n",
        shape, cases, bits, seed, db_loaded ? "yes" : "no");
    printf("  NOTE: DB-backed methods (trial, p-1, p+1) unavailable for %d-bit corpus.\n", bits);
    printf("  Methods: fermat, rho, ecm, auto\n\n");

    int fastest_counts[BN_COMPETE_METHODS]; memset(fastest_counts, 0, sizeof(fastest_counts));
    int success_counts[BN_COMPETE_METHODS]; memset(success_counts, 0, sizeof(success_counts));
    int total = 0;

    printf("  %-4s %-8s %-8s %-8s %-8s %-8s\n", "#", "fermat", "rho", "ecm", "auto", "fastest");

    int generated = 0;
    for (int ci = 0; ci < cases * 10 && generated < cases; ++ci) {
        BigNum p, q;
        if (!corpus_gen_pair_bn(&rng, half, bits, shape, &p, &q)) continue;
        BigNum N; bn_mul(&N, &p, &q);
        if (bn_is_zero(&N) || bn_factor_is_prime(&N)) continue;

        double times[BN_COMPETE_METHODS];
        bool found[BN_COMPETE_METHODS];

        { FactorResultBigNum fr = cpss_factor_fermat_bn(&N, 0u);
          times[0] = fr.time_seconds; found[0] = fr.factor_found; }
        { FactorResultBigNum fr = cpss_factor_rho_bn(&N, 0u, 0u);
          times[1] = fr.time_seconds; found[1] = fr.factor_found; }
        { FactorResultBigNum fr = cpss_factor_ecm_bn(&N, 0u, 0u, 0u);
          times[2] = fr.time_seconds; found[2] = fr.factor_found; }
        { FactorResultBigNum fr;
          if (s) fr = cpss_factor_auto_bn(s, &N);
          else { fr = cpss_factor_rho_bn(&N, 0u, 0u);
                 if (!fr.factor_found) fr = cpss_factor_fermat_bn(&N, 0u);
                 if (!fr.factor_found) fr = cpss_factor_ecm_bn(&N, 0u, 0u, 0u); }
          times[3] = fr.time_seconds; found[3] = fr.factor_found; }

        int fastest_idx = -1;
        double fastest_time = 1e30;
        for (int mi = 0; mi < BN_COMPETE_METHODS; ++mi) {
            if (found[mi]) {
                success_counts[mi]++;
                if (times[mi] < fastest_time) { fastest_time = times[mi]; fastest_idx = mi; }
            }
        }
        if (fastest_idx >= 0) fastest_counts[fastest_idx]++;

        printf("  %-4d", generated + 1);
        for (int mi = 0; mi < BN_COMPETE_METHODS; ++mi) {
            if (found[mi]) printf(" %.4f  ", times[mi]);
            else printf(" fail    ");
        }
        printf(" %s\n", fastest_idx >= 0 ? BN_COMPETE_MNAMES[fastest_idx] : "none");

        if (csv_fp) {
            char pbuf[512], qbuf[512], nbuf[512];
            bn_to_str(&p, pbuf, sizeof(pbuf));
            bn_to_str(&q, qbuf, sizeof(qbuf));
            bn_to_str(&N, nbuf, sizeof(nbuf));
            fprintf(csv_fp, "%s,%s,%s,%s", nbuf, pbuf, qbuf, shape);
            for (int mi = 0; mi < BN_COMPETE_METHODS; ++mi)
                fprintf(csv_fp, ",%s,%.6f", found[mi] ? "ok" : "fail", times[mi]);
            fprintf(csv_fp, ",%s\n", fastest_idx >= 0 ? BN_COMPETE_MNAMES[fastest_idx] : "none");
        }

        ++total; ++generated;
    }

    printf("\n  summary (corpus=%s, cases=%d, bits=%d):\n", shape, total, bits);
    printf("    fastest-method frequency:\n");
    for (int mi = 0; mi < BN_COMPETE_METHODS; ++mi) {
        if (fastest_counts[mi] > 0)
            printf("      %-10s %d/%d (%.0f%%)\n", BN_COMPETE_MNAMES[mi], fastest_counts[mi], total,
                total > 0 ? 100.0 * fastest_counts[mi] / total : 0.0);
    }
    printf("    success rate per method:\n");
    for (int mi = 0; mi < BN_COMPETE_METHODS; ++mi) {
        printf("      %-10s %d/%d (%.0f%%)\n", BN_COMPETE_MNAMES[mi], success_counts[mi], total,
            total > 0 ? 100.0 * success_counts[mi] / total : 0.0);
    }
    if (csv_fp) { fclose(csv_fp); printf("    CSV: %s\n", csv_out); }
}
