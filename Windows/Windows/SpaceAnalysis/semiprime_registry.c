/**
 * cpss_semiprime_space_registry.c - SemiPrime space registry for family-aware navigation.
 * Part of the CPSS Viewer amalgamation.
 *
 * Defines the SemiPrime space registry type (per-N report model, NOT cell-grid),
 * 4 first-batch entries (observation, coverage, bounds, routing),
 * lookup/list helpers, and per-space metrics descriptions.
 *
 * Depends on: SemiPrimeObservationSpace.c, SemiPrimeCoverageSpace.c,
 *             SemiPrimeBoundsSpace.c, SemiPrimeRoutingSpace.c,
 *             cpss_cli.c (parse_numeric, ParsedNum, tier_name)
 */

/* ── SemiPrime space registry entry type ── */

/**
 * SemiPrime spaces are per-N report spaces, not (i,j) cell-grid spaces.
 * Each entry takes a ParsedNum (tier-aware) and produces a structured report.
 */
typedef void (*SemiPrimeDispatchFn)(CPSSStream* s, const ParsedNum* pn,
                                     char** extra_argv, int extra_argc);

struct SemiPrimeSpaceEntry {
    const char* name;           /* canonical CLI name */
    const char* display_name;   /* heading name */
    const char* description;    /* one-line summary */
    SemiPrimeDispatchFn dispatch;
};

/* ── Thin dispatch wrappers for the 4 first-batch spaces ── */

static void sp_dispatch_observation(CPSSStream* s, const ParsedNum* pn,
                                     char** extra_argv, int extra_argc) {
    (void)extra_argv; (void)extra_argc;
    if (pn->tier == TIER_U64) {
        if (!s || s->index_len == 0u) {
            printf("observable(%"PRIu64"): no database loaded.\n", pn->u64);
            return;
        }
        ObservableFeatureVec vec;
        observable_compute(s, pn->u64, &vec);
        const char* suggestion;
        if (vec.is_prime) { suggestion = "prime"; }
        else {
            RouterEnv obs_env = router_env_from_stream(s);
            CoverageMethod obs_best = predict_available(pn->u64, &obs_env);
            suggestion = (obs_best < COV_METHOD_COUNT) ? MNAMES[obs_best] : "auto";
        }
        printf("observable(%"PRIu64"):\n", pn->u64);
        printf("  tier               = uint64\n");
        printf("  is_prime           = %s\n", vec.is_prime ? "yes" : "no");
        printf("  is_perfect_square  = %s\n", vec.is_perfect_square ? "yes" : "no");
        printf("  wheel_prime_divides = %s\n", vec.divisible_by_wheel_prime ? "yes" : "no");
        printf("  near_square_score  = %.6f\n", vec.near_square_score);
        printf("  small_factor_ev    = %.6f\n", vec.small_factor_evidence);
        printf("  cpss_coverage      = %.4f  (%s)\n", vec.cpss_coverage_ratio,
            vec.cpss_coverage_complete ? "complete" : "partial");
        printf("  route suggestion   = %s\n", suggestion);
    } else {
        char nbuf[512]; bn_to_str(&pn->bn, nbuf, sizeof(nbuf));
        ObservableFeatureVecBN vec;
        observable_compute_bn(&pn->bn, &vec);
        const char* suggestion = observable_route_suggest_bn(&vec);
        printf("observable(%s): [%s, %u bits]\n", nbuf, tier_name(pn->tier), pn->bits);
        printf("  is_probable_prime  = %s\n", vec.is_probable_prime ? "yes" : "no");
        printf("  is_perfect_square  = %s\n", vec.is_perfect_square ? "yes" : "no");
        printf("  wheel_prime_divides = %s\n", vec.divisible_by_wheel_prime ? "yes" : "no");
        printf("  near_square_score  = %.6f\n", vec.near_square_score);
        printf("  small_factor_ev    = %.6f\n", vec.small_factor_evidence);
        printf("  cpss_coverage      = n/a  (DB cannot cover sqrt of %u-bit input)\n", pn->bits);
        printf("  route suggestion   = %s\n", suggestion);
    }
}

static void sp_dispatch_coverage(CPSSStream* s, const ParsedNum* pn,
                                  char** extra_argv, int extra_argc) {
    (void)extra_argv; (void)extra_argc;
    RouterEnv env = router_env_from_stream(s);

    if (pn->tier == TIER_U64) {
        uint64_t N = pn->u64;
        CoverageState ideal_st; coverage_init(&ideal_st, N);
        CoverageMethod ideal_best = coverage_best_next(&ideal_st);
        CoverageState avail_st; coverage_init(&avail_st, N);
        if (!env.trial_db_available) avail_st.methods[COV_METHOD_TRIAL].plausibility = 0.0;
        if (!env.pminus1_available)  avail_st.methods[COV_METHOD_PMINUS1].plausibility = 0.0;
        if (!env.pplus1_available)   avail_st.methods[COV_METHOD_PPLUS1].plausibility = 0.0;
        if (!env.db_loaded) {
            CoverageFeatures f = coverage_extract_features(N);
            if (!f.has_wheel_div && !f.is_even) {
                avail_st.methods[COV_METHOD_RHO].plausibility += 0.20;
                if (f.ns_score < 0.7) avail_st.methods[COV_METHOD_FERMAT].plausibility *= 0.5;
            }
        }
        CoverageMethod avail_best = coverage_best_next(&avail_st);

        printf("coverage-sim(%"PRIu64"):\n", N);
        router_env_print(&env);
        printf("  ideal priors (assumes full DB):\n");
        for (int mi = 0; mi < COV_METHOD_COUNT; ++mi)
            printf("    %-10s plausibility=%.2f  cost=%.2f  ratio=%.1f\n",
                MNAMES[mi], ideal_st.methods[mi].plausibility, COV_DEFAULT_COST[mi],
                COV_DEFAULT_COST[mi] > 0.0 ? ideal_st.methods[mi].plausibility / COV_DEFAULT_COST[mi] : 0.0);
        printf("  ideal best         = %s\n", ideal_best < COV_METHOD_COUNT ? MNAMES[ideal_best] : "none");
        printf("  available priors (current environment):\n");
        for (int mi = 0; mi < COV_METHOD_COUNT; ++mi) {
            const char* tag = "";
            if (avail_st.methods[mi].plausibility <= 0.001 && ideal_st.methods[mi].plausibility > 0.01)
                tag = "  [unavailable]";
            printf("    %-10s plausibility=%.2f  cost=%.2f  ratio=%.1f%s\n",
                MNAMES[mi], avail_st.methods[mi].plausibility, COV_DEFAULT_COST[mi],
                COV_DEFAULT_COST[mi] > 0.0 ? avail_st.methods[mi].plausibility / COV_DEFAULT_COST[mi] : 0.0, tag);
        }
        printf("  available best     = %s\n", avail_best < COV_METHOD_COUNT ? MNAMES[avail_best] : "none");
        coverage_mark_tried(&avail_st, COV_METHOD_WHEEL, 0.001, false, 1u);
        CoverageMethod after_wheel = coverage_best_next(&avail_st);
        printf("  after wheel fail   = %s\n", after_wheel < COV_METHOD_COUNT ? MNAMES[after_wheel] : "none");
    } else {
        char nbuf[512]; bn_to_str(&pn->bn, nbuf, sizeof(nbuf));
        printf("coverage-sim(%s): [%s, %u bits]\n", nbuf, tier_name(pn->tier), pn->bits);
        router_env_print(&env);
        ObservableFeatureVecBN obs; observable_compute_bn(&pn->bn, &obs);
        double p_wheel = obs.divisible_by_wheel_prime ? 0.95 : 0.0;
        double p_fermat = obs.is_perfect_square ? 0.90 : obs.near_square_score > 0.8 ? 0.70
                        : obs.near_square_score > 0.5 ? 0.35 : 0.10;
        double p_rho = 0.50; double p_ecm = (pn->bits > 128u) ? 0.60 : 0.40;
        if (pn->bits > 256u) { p_ecm = 0.70; p_rho = 0.40; p_fermat *= 0.5; }
        printf("  NOTE: DB-backed methods unavailable for %u-bit input.\n", pn->bits);
        printf("  available priors:\n");
        printf("    %-10s plausibility=%.2f  %s\n", "wheel", p_wheel, p_wheel > 0.01 ? "" : "[no wheel factor]");
        printf("    %-10s plausibility=%.2f  [unavailable]\n", "trial", 0.0);
        printf("    %-10s plausibility=%.2f  ns=%.4f\n", "fermat", p_fermat, obs.near_square_score);
        printf("    %-10s plausibility=%.2f\n", "rho", p_rho);
        printf("    %-10s plausibility=%.2f  [unavailable]\n", "pminus1", 0.0);
        printf("    %-10s plausibility=%.2f  [unavailable]\n", "pplus1", 0.0);
        printf("    %-10s plausibility=%.2f%s\n", "ecm", p_ecm, pn->bits > 128u ? "  [boosted]" : "");
        const char* best = "rho"; double br = p_rho / 0.2;
        if (p_wheel / 0.01 > br && p_wheel > 0.01) { best = "wheel"; br = p_wheel / 0.01; }
        if (p_fermat / 0.1 > br) { best = "fermat"; br = p_fermat / 0.1; }
        if (p_ecm / 0.8 > br) { best = "ecm"; br = p_ecm / 0.8; }
        printf("  available best     = %s\n", best);
    }
}

static void sp_dispatch_bounds(CPSSStream* s, const ParsedNum* pn,
                                char** extra_argv, int extra_argc) {
    (void)s;
    int64_t fermat_K = 0; uint64_t min_L = 0u;
    bool have_K = false, have_L = false, use_auto = false;
    for (int ai = 0; ai < extra_argc; ++ai) {
        if (strcmp(extra_argv[ai], "--fermat-failed-steps") == 0 && ai + 1 < extra_argc) {
            fermat_K = (int64_t)_strtoi64(extra_argv[++ai], NULL, 10); have_K = true;
        } else if (strcmp(extra_argv[ai], "--min-factor-lower-bound") == 0 && ai + 1 < extra_argc) {
            min_L = parse_u64(extra_argv[++ai], "L"); have_L = true;
        } else if (strcmp(extra_argv[ai], "--auto") == 0) {
            use_auto = true;
        }
    }
    if (pn->tier == TIER_U64) {
        uint64_t N = pn->u64;
        if (use_auto && !have_L) {
            FactorShapeEvidence ev = gather_auto_evidence(N);
            if (ev.has_min_factor_lb) { min_L = ev.min_factor_lb; have_L = true; }
        }
        difference_bounds(N, fermat_K, min_L, have_K, have_L);
    } else {
        if (use_auto && !have_L) {
            bool found_wheel = false;
            for (size_t wi = 0u; wi < ARRAY_LEN(WHEEL_PRIMES); ++wi)
                if (bn_mod_u64(&pn->bn, WHEEL_PRIMES[wi]) == 0u) { found_wheel = true; break; }
            if (!found_wheel) { min_L = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u; have_L = true; }
        }
        difference_bounds_bn(&pn->bn, fermat_K, min_L, have_K, have_L);
    }
}

static void sp_dispatch_routing(CPSSStream* s, const ParsedNum* pn,
                                 char** extra_argv, int extra_argc) {
    (void)extra_argv; (void)extra_argc;
    if (pn->tier == TIER_U64) {
        factor_explain_v2(s, pn->u64);
    } else {
        /* BigNum factor-explain — mirrors the inline block in cpss_app.c */
        char nbuf[512]; bn_to_str(&pn->bn, nbuf, sizeof(nbuf));
        printf("factor-explain(%s): [%s, %u bits]\n", nbuf, tier_name(pn->tier), pn->bits);
        printf("  environment:\n");
        printf("    tier           = %s (%u bits)\n", tier_name(pn->tier), pn->bits);
        printf("    DB loaded      = %s\n", s ? "yes" : "no");
        printf("    trial/p-1/p+1  = unavailable (DB cannot cover sqrt of %u-bit N)\n", pn->bits);
        printf("    wheel/fermat/rho/ecm = always available\n");
        ObservableFeatureVecBN obs; observable_compute_bn(&pn->bn, &obs);
        printf("  observables:\n");
        printf("    near_square    = %.4f\n", obs.near_square_score);
        printf("    perfect_square = %s\n", obs.is_perfect_square ? "yes" : "no");
        printf("    wheel_divides  = %s\n", obs.divisible_by_wheel_prime ? "yes" : "no");
        printf("  routing:\n");
        const char* bn_route = observable_route_suggest_bn(&obs);
        printf("    suggested      = %s\n", bn_route);
        printf("  factorisation:\n");
        FactorResultBigNum fr;
        if (s) fr = cpss_factor_auto_bn(s, &pn->bn);
        else {
            fr = cpss_factor_rho_bn(&pn->bn, 0u, 0u);
            if (!fr.factor_found) fr = cpss_factor_fermat_bn(&pn->bn, 0u);
            if (!fr.factor_found) fr = cpss_factor_ecm_bn(&pn->bn, 0u, 0u, 0u);
        }
        printf("    method         = %s\n", fr.method ? fr.method : "none");
        if (fr.factor_found) {
            char fbuf[512], cbuf[512];
            bn_to_str(&fr.factor, fbuf, sizeof(fbuf));
            bn_to_str(&fr.cofactor, cbuf, sizeof(cbuf));
            printf("    factor         = %s%s\n", fbuf, fr.factor_is_prime ? " (prime)" : "");
            printf("    cofactor       = %s%s\n", cbuf, fr.cofactor_is_prime ? " (prime)" : "");
        }
        printf("    fully factored = %s\n", fr.fully_factored ? "yes" : "no");
        printf("  classification:\n");
        if (fr.factor_found) printf("    composite (factored)\n");
        else {
            bool prp = bn_is_probable_prime(&pn->bn, 20);
            printf("    %s\n", prp ? "probable prime (Miller-Rabin, 20 witnesses)" : "composite (unfactored)");
        }
        printf("  time             = %.6fs\n", fr.time_seconds);
    }
}

/* ── Registry array ── */

static const SemiPrimeSpaceEntry SEMIPRIME_NAMED_REGISTRY[] = {
    { "observation", "Observation", "Observable feature vector (ns_score, wheel, coverage)",
      sp_dispatch_observation },
    { "coverage",    "Coverage",    "Method plausibility priors and cost model",
      sp_dispatch_coverage },
    { "bounds",      "Bounds",      "Factor-difference bounds from Fermat geometry",
      sp_dispatch_bounds },
    { "routing",     "Routing",     "Environment-aware method routing analysis",
      sp_dispatch_routing },
};
#define SEMIPRIME_NAMED_COUNT (sizeof(SEMIPRIME_NAMED_REGISTRY) / sizeof(SEMIPRIME_NAMED_REGISTRY[0]))

/* ── Lookup + list helpers ── */

static const SemiPrimeSpaceEntry* semiprime_space_lookup(const char* name) {
    for (size_t i = 0u; i < SEMIPRIME_NAMED_COUNT; ++i) {
        if (strcmp(SEMIPRIME_NAMED_REGISTRY[i].name, name) == 0)
            return &SEMIPRIME_NAMED_REGISTRY[i];
    }
    return NULL;
}

static void semiprime_list_spaces(void) {
    for (size_t i = 0u; i < SEMIPRIME_NAMED_COUNT; ++i) {
        printf("  %-14s %s\n", SEMIPRIME_NAMED_REGISTRY[i].name,
            SEMIPRIME_NAMED_REGISTRY[i].description);
    }
}

/* ── Per-space metrics descriptions ── */

static void semiprime_print_metrics(const SemiPrimeSpaceEntry* sp) {
    printf("SemiPrime %s space metrics:\n", sp->display_name);
    if (strcmp(sp->name, "observation") == 0) {
        printf("  near_square_score   proximity to perfect square [0..1]\n"
               "  small_factor_ev     evidence of wheel-prime factor [0/1]\n"
               "  is_perfect_square   exact square test\n"
               "  is_prime            Miller-Rabin primality\n"
               "  cpss_coverage       fraction of [2,sqrt(N)] covered by DB\n"
               "  route_suggestion    coverage-model best available method\n"
               "  Navigation: per-N report (not region-based)\n");
    } else if (strcmp(sp->name, "coverage") == 0) {
        printf("  per-method plausibility   prior probability of success [0..1]\n"
               "  per-method cost           relative execution cost\n"
               "  plausibility/cost ratio   expected value ranking\n"
               "  ideal_best                best method assuming full DB\n"
               "  available_best            best method given current environment\n"
               "  after_wheel_fail          next-best after wheel elimination\n"
               "  Navigation: per-N report (not region-based)\n");
    } else if (strcmp(sp->name, "bounds") == 0) {
        printf("  Delta lower bound   2*sqrt(ceil(sqrt(N))^2 - N)\n"
               "  Delta upper bound   from min-factor exclusion (if available)\n"
               "  R bounds            implied ratio q/p\n"
               "  delta bounds        log-imbalance (1/2)*log(q/p)\n"
               "  Fermat baseline     a0, r0 from ceil(sqrt(N))\n"
               "  Flags: --auto, --fermat-failed-steps K, --min-factor-lower-bound L\n"
               "  Navigation: per-N report (not region-based)\n");
    } else if (strcmp(sp->name, "routing") == 0) {
        printf("  environment         DB state, method availability\n"
               "  observables         ns_score, wheel, coverage signals\n"
               "  factor-shape bounds inline Delta/R bounds\n"
               "  routing prediction  ideal vs available method selection\n"
               "  method reasoning    per-method signal-based explanation\n"
               "  factorisation       actual factor attempt + result\n"
               "  post-hoc           prediction vs truth comparison\n"
               "  Navigation: per-N report (not region-based)\n");
    } else {
        printf("  (no detailed metrics available for this space yet)\n");
    }
}
