/**
 * CoverageSpace.c - Coverage space functions.
 * Part of the CPSS Overlay Spaces module.
 *
 * "What have we already tried, and what still looks worth trying?"
 * Axes: x = cost to test, y = remaining plausibility.
 *
 * After each failed method, regions move downward in plausibility.
 * This stops the engine from re-doing low-value work.
 *
 * Depends on: cpss_types.c (CPSSQueryStatus)
 */

/** Known factorisation methods. */
typedef enum {
    COV_METHOD_WHEEL,      /* wheel-prime division */
    COV_METHOD_TRIAL,      /* CPSS-accelerated trial division */
    COV_METHOD_FERMAT,     /* Fermat factorisation */
    COV_METHOD_RHO,        /* Pollard rho */
    COV_METHOD_PMINUS1,    /* Pollard p-1 */
    COV_METHOD_PPLUS1,     /* Williams p+1 */
    COV_METHOD_ECM,        /* elliptic curve method */
    COV_METHOD_COUNT       /* sentinel: number of methods */
} CoverageMethod;

/** Per-method tracking state. */
typedef struct {
    bool tried;              /* has this method been attempted? */
    double cost_spent;       /* cumulative cost (seconds or work units) */
    double plausibility;     /* remaining plausibility [0.0, 1.0] */
    bool found_factor;       /* did this method find a factor? */
    uint64_t work_units;     /* method-specific work counter (steps, iterations, etc.) */
} CoverageMethodState;

/** Full coverage state for one input N. */
typedef struct {
    uint64_t N;
    CoverageMethodState methods[COV_METHOD_COUNT];
    double total_cost;       /* total cost across all methods */
    bool factor_found;       /* any method found a factor? */
} CoverageState;

/** Approximate relative cost per method (normalised, lower = cheaper). */
static const double COV_DEFAULT_COST[COV_METHOD_COUNT] = {
    0.01,  /* WHEEL:   nearly free */
    0.5,   /* TRIAL:   moderate, depends on range */
    0.1,   /* FERMAT:  cheap per step */
    0.2,   /* RHO:     moderate */
    0.15,  /* PMINUS1: moderate */
    0.15,  /* PPLUS1:  moderate */
    0.8    /* ECM:     expensive */
};

/**
 * Whether a method is definitive: failure means the entire region
 * it covers is ruled out (plausibility should go to 0, not just decay).
 * Wheel division is definitive: if N has no wheel-prime factor, retrying
 * wheel division is pointless.
 */
static bool coverage_method_is_definitive(CoverageMethod method) {
    return method == COV_METHOD_WHEEL;
}

/**
 * Feature vector extracted from N for predictor calibration.
 * All signals are cheap to compute (no DB needed) and inspectable.
 */
typedef struct {
    bool is_even;
    bool is_perf_sq;
    bool has_wheel_div;
    double ns_score;          /* near-square score [0,1] */
    bool is_small;            /* N < 100000 */
    bool is_medium;           /* N < 1000000 */
    /* Shape cues derived from N alone (pre-factorisation) */
    uint8_t n_mod4;           /* N mod 4: 1 or 3 for odd N (affects Fermat parity) */
    double log2_N;            /* approximate bit size */
    bool balanced_hint;       /* heuristic: near-square score suggests balanced factors */
    bool skewed_hint;         /* heuristic: low ns_score + large N suggests skewed */
} CoverageFeatures;

/** Extract cheap features from N for predictor calibration. */
static CoverageFeatures coverage_extract_features(uint64_t N) {
    CoverageFeatures f;
    memset(&f, 0, sizeof(f));
    f.is_even = (N > 2u && (N & 1u) == 0u);
    f.is_perf_sq = is_perfect_square_u64(N);
    f.ns_score = observable_near_square_score(N);
    f.is_small = (N < 100000u);
    f.is_medium = (N < 1000000u);
    f.n_mod4 = (uint8_t)(N & 3u);
    f.log2_N = (N > 1u) ? log2((double)N) : 0.0;
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (N > WHEEL_PRIMES[i] && N % WHEEL_PRIMES[i] == 0u) {
            f.has_wheel_div = true;
            break;
        }
    }
    /* balanced_hint: ns_score > 0.5 suggests factors are not wildly apart */
    f.balanced_hint = (f.ns_score > 0.5);
    /* skewed_hint: low ns_score + medium/large N suggests one factor much smaller */
    f.skewed_hint = (f.ns_score < 0.2 && f.log2_N > 20.0);
    return f;
}

/**
 * Initialise coverage state for input N.
 * Computes N-dependent priors using cheap observable signals:
 *   - Even N: wheel plausibility high, Fermat low (even => factor 2)
 *   - Perfect square: Fermat plausibility high
 *   - Near-square score: Fermat boosted proportionally
 *   - No wheel factor: pminus1/pplus1 boosted (DB-backed methods favoured)
 *   - Balanced hint: fermat + pminus1 boosted
 *   - Skewed hint: rho + ecm boosted
 *   - Small N: trial + pminus1 boosted
 */
static void coverage_init(CoverageState* state, uint64_t N) {
    memset(state, 0, sizeof(*state));
    state->N = N;
    CoverageFeatures f = coverage_extract_features(N);

    /* Base priors */
    double p_wheel   = 0.15;
    double p_trial   = 0.5;
    double p_fermat  = 0.2;
    double p_rho     = 0.5;
    double p_pminus1 = 0.25;
    double p_pplus1  = 0.15;
    double p_ecm     = 0.4;

    /* ── Wheel / even adjustments ── */
    if (f.has_wheel_div) {
        p_wheel = 0.95;
        if (f.is_even) {
            p_fermat = 0.05; /* even => factor 2, very skewed, Fermat bad */
        }
    }
    else if (f.is_even) {
        p_wheel = 0.95;
        p_fermat = 0.05;
    }
    else {
        p_wheel = 0.0;    /* odd and not wheel-divisible => wheel useless */
    }

    /* ── Near-square / Fermat adjustments ── */
    if (f.is_perf_sq) {
        p_fermat = 0.9;   /* perfect square => 0 Fermat steps */
    }
    else if (f.ns_score > 0.8) {
        p_fermat = 0.7;
    }
    else if (f.ns_score > 0.5) {
        p_fermat = 0.35;
    }
    else if (f.ns_score < 0.15) {
        p_fermat = 0.08;  /* very low near-square => Fermat unlikely to help */
    }

    /* ── Trial adjustment for small N ── */
    if (f.is_medium) {
        p_trial = 0.85;
    }

    /* ── Calibrated p-1/p+1 boost for no-wheel-factor composites ──
     * Phase 8 finding: pminus1 is beating predictions for DB-loaded
     * no-wheel-factor small balanced semiprimes. This calibration reflects
     * that the auto-router tries p-1 before rho, and p-1 frequently wins
     * when (a) no tiny factor exists and (b) factors are not wildly skewed. */
    if (!f.has_wheel_div && !f.is_even) {
        p_pminus1 = 0.55;
        p_pplus1 = 0.35;

        /* Small N: p-1 especially effective — rho demoted */
        if (f.is_small) {
            p_pminus1 = 0.70;
            p_pplus1 = 0.40;
            p_rho = 0.30;
        }
        else if (f.is_medium) {
            p_pminus1 = 0.60;
            p_rho = 0.40;
        }

        /* Balanced hint: factors similar size favours p-1 and fermat */
        if (f.balanced_hint && !f.is_perf_sq) {
            p_pminus1 += 0.08;
            if (p_fermat < 0.3) p_fermat = 0.30; /* moderate fermat chance */
        }

        /* Skewed hint: one factor much smaller — rho/ecm more likely to find it */
        if (f.skewed_hint) {
            p_rho += 0.10;
            p_ecm += 0.10;
            p_pminus1 -= 0.10;
            p_fermat -= 0.05;
        }
    }

    /* Clamp all priors to [0, 0.95] */
    if (p_wheel   > 0.95) p_wheel   = 0.95;
    if (p_trial   > 0.95) p_trial   = 0.95;
    if (p_fermat  > 0.95) p_fermat  = 0.95;
    if (p_rho     > 0.95) p_rho     = 0.95;
    if (p_pminus1 > 0.95) p_pminus1 = 0.95;
    if (p_pplus1  > 0.95) p_pplus1  = 0.95;
    if (p_ecm     > 0.95) p_ecm     = 0.95;
    if (p_fermat  < 0.0)  p_fermat  = 0.0;
    if (p_pminus1 < 0.0)  p_pminus1 = 0.0;
    if (p_rho     < 0.0)  p_rho     = 0.0;

    state->methods[COV_METHOD_WHEEL].plausibility   = p_wheel;
    state->methods[COV_METHOD_TRIAL].plausibility   = p_trial;
    state->methods[COV_METHOD_FERMAT].plausibility  = p_fermat;
    state->methods[COV_METHOD_RHO].plausibility     = p_rho;
    state->methods[COV_METHOD_PMINUS1].plausibility = p_pminus1;
    state->methods[COV_METHOD_PPLUS1].plausibility  = p_pplus1;
    state->methods[COV_METHOD_ECM].plausibility     = p_ecm;
}

/**
 * Record that a method was tried.
 * cost: time or work spent. found_factor: did it succeed?
 *
 * On success: plausibility → 0 (no need to retry).
 * On failure:
 *   - Definitive methods (wheel): plausibility → 0 (region fully ruled out).
 *   - Non-definitive methods: plausibility decays by 0.25x per attempt
 *     (aggressive enough that retrying quickly becomes unattractive).
 */
static void coverage_mark_tried(CoverageState* state, CoverageMethod method,
                                  double cost, bool found_factor, uint64_t work_units) {
    if (method >= COV_METHOD_COUNT) return;
    CoverageMethodState* ms = &state->methods[method];
    ms->tried = true;
    ms->cost_spent += cost;
    ms->work_units += work_units;
    state->total_cost += cost;

    if (found_factor) {
        ms->found_factor = true;
        ms->plausibility = 0.0;
        state->factor_found = true;
    }
    else if (coverage_method_is_definitive(method)) {
        /* Definitive failure: this method's region is fully ruled out */
        ms->plausibility = 0.0;
    }
    else {
        /* Non-definitive: aggressive decay (0.25x per attempt) */
        ms->plausibility *= 0.25;
    }
}

/**
 * Get the remaining plausibility for a specific method.
 */
static double coverage_plausibility(const CoverageState* state, CoverageMethod method) {
    if (method >= COV_METHOD_COUNT) return 0.0;
    return state->methods[method].plausibility;
}

/**
 * Suggest the best next method to try.
 * Picks the method with the highest plausibility-to-cost ratio that
 * hasn't already found a factor.
 * Returns the method enum, or COV_METHOD_COUNT if nothing is worth trying.
 */
static CoverageMethod coverage_best_next(const CoverageState* state) {
    double best_ratio = 0.0;
    CoverageMethod best = COV_METHOD_COUNT;

    for (int i = 0; i < COV_METHOD_COUNT; ++i) {
        const CoverageMethodState* ms = &state->methods[i];
        if (ms->found_factor) continue;
        if (ms->plausibility <= 0.001) continue; /* effectively eliminated */

        double expected_cost = COV_DEFAULT_COST[i];
        /* Already-tried methods cost more on retry (diminishing returns) */
        if (ms->tried) expected_cost *= 2.0;

        double ratio = ms->plausibility / expected_cost;
        if (ratio > best_ratio) {
            best_ratio = ratio;
            best = (CoverageMethod)i;
        }
    }
    return best;
}
