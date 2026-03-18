/**
 * SemiPrimeRoutingSpace.c - Environment-aware routing, factor-explain, evidence.
 * Part of the CPSS SemiPrime Spaces module (N-centric inference).
 *
 * RouterEnv, predict_ideal, predict_available, factor_explain_v2,
 * evidence gathering, shared method-name helpers (MNAMES, classify_shape_posthoc).
 *
 * Depends on: SemiPrimeCoverageSpace.c, SemiPrimeObservationSpace.c, cpss_factor.c
 */

/* ══════════════════════════════════════════════════════════════════════
 * PHASE 6: ENVIRONMENT-AWARE ROUTING + EVIDENCE + CORPUS + EVAL
 * ══════════════════════════════════════════════════════════════════════ */

/** Runtime environment model — what methods are actually executable now. */
typedef struct {
    bool db_loaded;
    bool db_support_complete;     /* DB covers up to sqrt(N) for relevant N */
    uint64_t trial_bound;        /* highest prime available for trial div (0=none) */
    bool wheel_available;        /* always true: wheel division is cheap */
    bool fermat_available;       /* always true: no DB needed */
    bool rho_available;          /* always true: no DB needed */
    bool trial_db_available;     /* DB-backed trial division available? */
    bool pminus1_available;      /* needs DB for prime iteration */
    bool pplus1_available;       /* needs DB for prime iteration */
    bool ecm_available;          /* always true: no DB needed */
} RouterEnv;

static RouterEnv router_env_from_stream(CPSSStream* s) {
    RouterEnv env;
    memset(&env, 0, sizeof(env));
    env.wheel_available = true;
    env.fermat_available = true;
    env.rho_available = true;
    env.ecm_available = true;
    if (s && s->index_len > 0u) {
        env.db_loaded = true;
        env.trial_db_available = true;
        env.pminus1_available = true;
        env.pplus1_available = true;
        env.trial_bound = segment_end_n(&s->index_entries[s->index_len - 1u]);
    }
    return env;
}

static void router_env_print(const RouterEnv* env) {
    printf("  environment:\n");
    printf("    DB loaded      = %s\n", env->db_loaded ? "yes" : "no");
    if (env->db_loaded) {
        printf("    trial bound    = %"PRIu64"\n", env->trial_bound);
        printf("    trial/p-1/p+1  = available\n");
    } else {
        printf("    trial/p-1/p+1  = unavailable (no DB)\n");
    }
    printf("    wheel/fermat/rho/ecm = always available\n");
}

/** Predict best method under ideal (full DB) conditions. */
static CoverageMethod predict_ideal(uint64_t N) {
    CoverageState cov; coverage_init(&cov, N);
    return coverage_best_next(&cov);
}

/** Predict best method under actual available environment.
 *  1) Zeros methods that require DB when DB is absent.
 *  2) When DB absent + no wheel factor: boosts rho (it becomes the real
 *     workhorse) and tightens Fermat so it only wins with strong signals.
 *  3) When DB IS loaded: small positive boost for DB-backed methods. */
static CoverageMethod predict_available(uint64_t N, const RouterEnv* env) {
    CoverageState cov; coverage_init(&cov, N);
    /* Zero out methods that aren't actually available */
    if (!env->trial_db_available) cov.methods[COV_METHOD_TRIAL].plausibility = 0.0;
    if (!env->pminus1_available)  cov.methods[COV_METHOD_PMINUS1].plausibility = 0.0;
    if (!env->pplus1_available)   cov.methods[COV_METHOD_PPLUS1].plausibility = 0.0;

    CoverageFeatures f = coverage_extract_features(N);
    if (!env->db_loaded) {
        /* No DB: trial/p-1/p+1 are gone. Rho is the real general-purpose
         * method in this regime. Fermat should only win when ns_score is
         * genuinely compelling (>0.7), not just moderate. */
        if (!f.has_wheel_div && !f.is_even) {
            cov.methods[COV_METHOD_RHO].plausibility += 0.20; /* rho becomes primary */
            /* Tighten Fermat: only keep high prior if signal is strong */
            if (f.ns_score < 0.7) {
                cov.methods[COV_METHOD_FERMAT].plausibility *= 0.5;
            }
        }
    } else {
        /* DB loaded: boost DB-backed methods.
         * Phase 11 sweep: pminus1 dominates, pplus1 wins ~10% of remaining.
         * Fermat overpredicts when DB is loaded because auto-router tries
         * pminus1/trial first regardless of near-square signal. */
        if (!f.has_wheel_div && !f.is_even) {
            cov.methods[COV_METHOD_PMINUS1].plausibility *= 1.15;
            cov.methods[COV_METHOD_PPLUS1].plausibility *= 1.15;
            if (f.is_medium) {
                cov.methods[COV_METHOD_TRIAL].plausibility *= 1.10;
            }
            /* Fermat rarely wins when DB-backed methods run first */
            if (f.ns_score < 0.8) {
                cov.methods[COV_METHOD_FERMAT].plausibility *= 0.7;
            }
        }
    }
    return coverage_best_next(&cov);
}

/** Evidence about factor shape gathered from real or manual sources. */
typedef struct {
    bool has_min_factor_lb;
    uint64_t min_factor_lb;
    bool min_factor_lb_actual;    /* true = from real work, false = manual/hypothetical */
    const char* min_factor_lb_source;

    bool has_fermat_failed_steps;
    uint64_t fermat_failed_steps;
    bool fermat_failed_steps_actual;
    const char* fermat_failed_steps_source;
} FactorShapeEvidence;

/** Gather conservative evidence from cheap checks + work-history. */
static FactorShapeEvidence gather_auto_evidence(uint64_t N) {
    FactorShapeEvidence ev;
    memset(&ev, 0, sizeof(ev));

    /* 1. Wheel exclusion (always performed — it's just modular arithmetic) */
    bool found_wheel = false;
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (N > WHEEL_PRIMES[i] && N % WHEEL_PRIMES[i] == 0u) { found_wheel = true; break; }
    }
    if (!found_wheel) {
        ev.has_min_factor_lb = true;
        ev.min_factor_lb = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u;
        ev.min_factor_lb_actual = true;
        ev.min_factor_lb_source = "wheel-exclusion(p>13)";
    }

    /* 2. Per-N evidence cache: actual trial exclusion bound */
    const EvidenceEntry* cached = evidence_lookup(N);
    if (cached) {
        if (cached->has_trial_bound && cached->trial_bound + 1u > ev.min_factor_lb) {
            ev.has_min_factor_lb = true;
            ev.min_factor_lb = cached->trial_bound + 1u;
            ev.min_factor_lb_actual = true;
            ev.min_factor_lb_source = "actual-trial-exclusion";
        }
        /* 3. Per-N evidence cache: actual Fermat failed steps */
        if (cached->has_fermat_steps && !cached->fermat_succeeded) {
            ev.has_fermat_failed_steps = true;
            ev.fermat_failed_steps = cached->fermat_steps;
            ev.fermat_failed_steps_actual = true;
            ev.fermat_failed_steps_source = "actual-fermat-failure";
        }
    }
    return ev;
}

/* ── Shared helpers used by factor-explain, router-eval, metric-eval, CSV ── */

static const char* MNAMES[] = { "wheel", "trial", "fermat", "rho", "pminus1", "pplus1", "ecm" };

static const char* method_name_from_fr(const char* fr_method) {
    if (!fr_method) return "unknown";
    if (strstr(fr_method, "wheel")) return "wheel"; if (strstr(fr_method, "trial")) return "trial";
    if (strstr(fr_method, "fermat")) return "fermat"; if (strstr(fr_method, "rho")) return "rho";
    if (strstr(fr_method, "pminus1")) return "pminus1"; if (strstr(fr_method, "pplus1")) return "pplus1";
    if (strstr(fr_method, "ecm")) return "ecm"; if (strstr(fr_method, "prime")) return "prime";
    return fr_method;
}
static int method_index(const char* name) {
    for (int i = 0; i < COV_METHOD_COUNT; ++i) { if (strcmp(MNAMES[i], name) == 0) return i; }
    return -1;
}

/** Post-hoc shape classification from known factors p, q. */
#define SHAPE_CLASS_COUNT 7
static const char* SHAPE_NAMES[SHAPE_CLASS_COUNT] = {
    "balanced", "skewed", "rsa-like", "no-wheel-factor", "pminus1-smooth", "pminus1-hostile", "mixed"
};
static int classify_shape_posthoc(uint64_t p, uint64_t q) {
    double R = (double)q / (double)p;
    bool has_wheel = false;
    uint64_t sp = p < q ? p : q;
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (sp == WHEEL_PRIMES[i]) { has_wheel = true; break; }
    }
    if (has_wheel) return 6; /* mixed: has a tiny factor */
    if (R < 2.0) return 0;  /* balanced */
    if (R > 100.0) return 1; /* skewed */
    if (sp > WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u]) return 3; /* no-wheel-factor */
    return 2; /* rsa-like */
}

/** Enhanced factor-explain v2 with environment + ideal/available split. */
static void factor_explain_v2(CPSSStream* s, uint64_t N) {
    printf("factor-explain(%"PRIu64"):\n", N);
    bool is_pr = miller_rabin_u64(N);
    bool is_sq = is_perfect_square_u64(N);
    bool is_even = (N > 2u && (N & 1u) == 0u);

    /* 1. Environment */
    RouterEnv env = router_env_from_stream(s);
    router_env_print(&env);

    /* 2. Classification */
    printf("  classification:\n");
    printf("    prime          = %s\n", is_pr ? "yes" : "no");
    printf("    perfect square = %s\n", is_sq ? "yes" : "no");
    printf("    even           = %s\n", is_even ? "yes" : "no");
    if (is_pr) { printf("  conclusion: %"PRIu64" is prime.\n", N); return; }
    if (N <= 3u) { printf("  N too small for analysis.\n"); return; }

    uint64_t wheel_div = 0u;
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (N > WHEEL_PRIMES[i] && N % WHEEL_PRIMES[i] == 0u) { wheel_div = WHEEL_PRIMES[i]; break; }
    }
    printf("    wheel divides  = %s", wheel_div ? "yes" : "no");
    if (wheel_div) printf(" (factor %"PRIu64")", wheel_div);
    printf("\n");

    /* 3. Observables */
    double ns_score = observable_near_square_score(N);
    printf("  observables:\n");
    printf("    near-square    = %.4f\n", ns_score);
    if (env.db_loaded) {
        SupportCoverage cov = cpss_support_coverage(s, N);
        printf("    DB coverage    = %.1f%%  (%s)\n", cov.db_coverage * 100.0,
            cov.db_complete ? "complete" : "partial");
    }

    /* 4. Work-history (per-N evidence cache) */
    {
        const EvidenceEntry* cached = evidence_lookup(N);
        if (cached) {
            printf("  evidence cache (N=%"PRIu64"):\n", N);
            if (cached->has_trial_bound)
                printf("    trial excluded  <= %"PRIu64" (actual)\n", cached->trial_bound);
            if (cached->has_fermat_steps)
                printf("    fermat steps    = %"PRIu64" (%s)\n", cached->fermat_steps,
                    cached->fermat_succeeded ? "succeeded" : "failed");
            if (cached->found_factor)
                printf("    found factor    = %"PRIu64"\n", cached->found_factor_value);
            if (cached->last_method)
                printf("    last method     = %s\n", cached->last_method);
        }
    }

    /* 5. Factor-shape bounds */
    FactorShapeEvidence ev = gather_auto_evidence(N);
    printf("  factor-shape bounds:\n");
    if (!is_sq) {
        uint64_t a0 = (uint64_t)isqrt_u64(N); if (a0 * a0 < N) ++a0;
        U128 r0_u128 = u128_sub(u128_mul64(a0, a0), u128_from_u64(N));
        uint64_t r0 = u128_fits_u64(r0_u128) ? r0_u128.lo : 0u;
        double delta_lower = 2.0 * sqrt((double)r0);
        printf("    Delta >= %.4f  (baseline square-gap, r0=%"PRIu64")\n", delta_lower, r0);
        /* Stronger lower bound from actual Fermat failure */
        if (ev.has_fermat_failed_steps && ev.fermat_failed_steps > 0u) {
            uint64_t aK = a0 + ev.fermat_failed_steps;
            U128 aK_sq = u128_mul64(aK, aK);
            if (!u128_lt(aK_sq, u128_from_u64(N))) {
                U128 rK = u128_sub(aK_sq, u128_from_u64(N));
                uint64_t rK64 = u128_fits_u64(rK) ? rK.lo : 0u;
                double fermat_lower = 2.0 * sqrt((double)rK64);
                if (fermat_lower > delta_lower) delta_lower = fermat_lower;
                printf("    Delta > %.4f  (%"PRIu64" Fermat steps failed, %s)\n",
                    fermat_lower, ev.fermat_failed_steps,
                    ev.fermat_failed_steps_source ? ev.fermat_failed_steps_source : "manual");
            }
        }
        if (ev.has_min_factor_lb && ev.min_factor_lb >= 2u && ev.min_factor_lb * ev.min_factor_lb <= N) {
            uint64_t du = N / ev.min_factor_lb - ev.min_factor_lb;
            printf("    Delta <= %"PRIu64"  (p>=%"PRIu64", %s%s)\n", du, ev.min_factor_lb,
                ev.min_factor_lb_source ? ev.min_factor_lb_source : "manual",
                ev.min_factor_lb_actual ? ", actual" : ", hypothetical");
        } else {
            printf("    (no upper bound on Delta)\n");
        }
    } else {
        printf("    perfect square => Delta=0, R=1\n");
    }

    /* 5. Routing: ideal vs available with per-method priors */
    CoverageState cov_ideal; coverage_init(&cov_ideal, N);
    CoverageMethod ideal = coverage_best_next(&cov_ideal);
    CoverageMethod avail = predict_available(N, &env);
    CoverageFeatures feat = coverage_extract_features(N);
    printf("  routing:\n");
    printf("    ideal best     = %s  (assumes full DB support)\n", ideal < COV_METHOD_COUNT ? MNAMES[ideal] : "none");
    printf("    available best = %s  (current environment)\n", avail < COV_METHOD_COUNT ? MNAMES[avail] : "none");
    printf("    calibration signals:\n");
    printf("      near_square_score = %.4f  balanced_hint = %s  skewed_hint = %s\n",
        feat.ns_score, feat.balanced_hint ? "yes" : "no", feat.skewed_hint ? "yes" : "no");
    printf("      log2(N) = %.1f  N_mod4 = %u  is_small = %s\n",
        feat.log2_N, feat.n_mod4, feat.is_small ? "yes" : "no");
    if (ideal != avail) {
        printf("    ideal != available:\n");
        if (!env.db_loaded) {
            printf("      no DB loaded => trial/pminus1/pplus1 unavailable\n");
            /* Show which methods were zeroed */
            if (cov_ideal.methods[COV_METHOD_PMINUS1].plausibility > 0.01)
                printf("      ideal pminus1 prior=%.2f => zeroed (no DB)\n", cov_ideal.methods[COV_METHOD_PMINUS1].plausibility);
            if (cov_ideal.methods[COV_METHOD_TRIAL].plausibility > 0.01)
                printf("      ideal trial prior=%.2f => zeroed (no DB)\n", cov_ideal.methods[COV_METHOD_TRIAL].plausibility);
        } else {
            printf("      DB loaded but coverage may be partial\n");
        }
    }

    /* 6. Method-specific reasoning with signal citations */
    printf("  reasoning:\n");
    if (wheel_div) {
        printf("    wheel: N divisible by %"PRIu64" => prior=0.95, near-certain success.\n", wheel_div);
        printf("    other methods: unnecessary if wheel succeeds.\n");
    } else if (is_sq) {
        printf("    fermat: perfect square => 0 steps, instant (prior=0.90).\n");
    } else {
        printf("    wheel: no wheel-prime factor => prior=0.00, eliminated.\n");
        /* Fermat reasoning */
        if (feat.ns_score > 0.8)
            printf("    fermat: ns_score=%.3f (>0.8) => prior=0.70, strongly favoured.\n", feat.ns_score);
        else if (feat.ns_score > 0.5)
            printf("    fermat: ns_score=%.3f (>0.5) => prior=0.35, moderate chance.\n", feat.ns_score);
        else if (feat.ns_score < 0.15)
            printf("    fermat: ns_score=%.3f (<0.15) => prior=0.08, unlikely to help.\n", feat.ns_score);
        else
            printf("    fermat: ns_score=%.3f => prior=0.20, base level.\n", feat.ns_score);
        /* p-1 reasoning */
        if (env.db_loaded) {
            double pm1_prior = cov_ideal.methods[COV_METHOD_PMINUS1].plausibility;
            printf("    pminus1: DB loaded, prior=%.2f", pm1_prior);
            if (!wheel_div && !is_even) {
                printf(" (boosted: no tiny factor");
                if (feat.is_small) printf(", small N => strong boost");
                else if (feat.is_medium) printf(", medium N => moderate boost");
                if (feat.balanced_hint) printf(", balanced hint");
                printf(")");
            }
            printf(".\n");
            /* trial */
            printf("    trial: DB loaded, bound=%"PRIu64", prior=%.2f.\n",
                env.trial_bound, cov_ideal.methods[COV_METHOD_TRIAL].plausibility);
            /* p+1 */
            printf("    pplus1: DB loaded, prior=%.2f (complementary to p-1).\n",
                cov_ideal.methods[COV_METHOD_PPLUS1].plausibility);
        } else {
            printf("    pminus1: DB not loaded => unavailable (would have prior=%.2f).\n",
                cov_ideal.methods[COV_METHOD_PMINUS1].plausibility);
            printf("    trial: DB not loaded => unavailable.\n");
            printf("    pplus1: DB not loaded => unavailable.\n");
        }
        /* Rho reasoning */
        printf("    rho: always available, prior=%.2f", cov_ideal.methods[COV_METHOD_RHO].plausibility);
        if (feat.skewed_hint) printf(" (boosted: skewed hint)");
        printf(", general-purpose fallback.\n");
        /* ECM */
        printf("    ecm: always available, prior=%.2f", cov_ideal.methods[COV_METHOD_ECM].plausibility);
        if (feat.skewed_hint) printf(" (boosted: skewed hint)");
        printf(", expensive but powerful.\n");
        /* Evidence */
        if (ev.has_min_factor_lb)
            printf("    evidence: wheel primes excluded, p >= %"PRIu64".\n", ev.min_factor_lb);
    }

    /* 7. Post-hoc: factor, compare prediction vs truth */
    if (N > 3u && !is_pr && N < 1000000000000ULL) {
        FactorResult fr;
        if (s && s->index_len > 0u) fr = cpss_factor_auto(s, N);
        else { fr = cpss_factor_rho(N, 0u, 0u); if (!fr.fully_factored) fr = cpss_factor_fermat(N, 0u); }
        if (fr.fully_factored && fr.factor_count >= 1u) {
            uint64_t p = fr.factors[0], q_val = N / p;
            if (p > q_val) { uint64_t t = p; p = q_val; q_val = t; }
            printf("  post-hoc (factors: %"PRIu64" * %"PRIu64"):\n", p, q_val);
            printf("    Delta          = %"PRIu64"\n", q_val - p);
            printf("    R              = %.4f\n", ratio_from_factors(p, q_val));
            double fA, fB; fermat_space_from_factors(p, q_val, &fA, &fB);
            printf("    Fermat A=%.1f B=%.1f\n", fA, fB);
            printf("    actual method  = %s (%.6fs)\n", fr.method, fr.time_seconds);
            /* Compare prediction vs truth — separate ideal and available analysis */
            const char* actual_name = method_name_from_fr(fr.method);
            int actual_idx = method_index(actual_name);
            bool ideal_correct = ((int)ideal == actual_idx);
            bool avail_correct = ((int)avail == actual_idx);
            printf("  prediction vs truth:\n");
            printf("    actual winner    = %s\n", actual_name);
            printf("    ideal predicted  = %s  %s\n",
                ideal < COV_METHOD_COUNT ? MNAMES[ideal] : "?",
                ideal_correct ? "CORRECT" : "WRONG");
            printf("    avail predicted  = %s  %s\n",
                avail < COV_METHOD_COUNT ? MNAMES[avail] : "?",
                avail_correct ? "CORRECT" : "WRONG");
            /* Explain ideal miss */
            if (!ideal_correct && ideal < COV_METHOD_COUNT) {
                printf("  ideal miss explanation:\n");
                printf("    predicted %s (prior=%.2f, ratio=%.1f)\n",
                    MNAMES[ideal], cov_ideal.methods[ideal].plausibility,
                    cov_ideal.methods[ideal].plausibility / COV_DEFAULT_COST[ideal]);
                if (ideal == COV_METHOD_PMINUS1 || ideal == COV_METHOD_TRIAL || ideal == COV_METHOD_PPLUS1) {
                    printf("    reason: DB-backed %s would be available in full-support environment\n", MNAMES[ideal]);
                    if (!env.db_loaded)
                        printf("    but no DB was loaded, so this method was never executed.\n");
                }
                if (actual_idx >= 0 && actual_idx < COV_METHOD_COUNT)
                    printf("    winner %s had ideal prior=%.2f, ratio=%.1f\n",
                        actual_name, cov_ideal.methods[actual_idx].plausibility,
                        cov_ideal.methods[actual_idx].plausibility / COV_DEFAULT_COST[actual_idx]);
            }
            /* Explain available miss */
            if (!avail_correct && avail < COV_METHOD_COUNT) {
                printf("  available miss explanation:\n");
                if (avail == COV_METHOD_FERMAT) {
                    printf("    predicted fermat because ns_score=%.3f", feat.ns_score);
                    if (feat.ns_score > 0.5) printf(" (moderate)");
                    else printf(" (weak)");
                    printf(", but rho won in practice.\n");
                    if (!env.db_loaded)
                        printf("    in no-DB mode, rho is the primary general-purpose method.\n");
                } else if (avail == COV_METHOD_RHO) {
                    printf("    predicted rho but %s won; rho is a reasonable fallback.\n", actual_name);
                } else {
                    printf("    predicted %s but %s won.\n", MNAMES[avail], actual_name);
                }
            }
        } else if (fr.fully_factored && fr.factor_count == 1u) {
            printf("  post-hoc: %"PRIu64"^%"PRIu64" (%s, %.6fs)\n",
                fr.factors[0], fr.exponents[0], fr.method, fr.time_seconds);
        }
    }
}
