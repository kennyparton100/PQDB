/**
 * cpss_primepair_analysis.c - Prime-Pair region statistics, CSV export.
 * Part of the CPSS Viewer amalgamation (Prime-Pair analysis layer).
 *
 * Per-space stats structs, accumulation, stats registry, and CSV export
 * for the 7 Prime-Pair spaces. All logic operates on known (p, q) pairs.
 *
 * Depends on: PrimePairSpaces/*.c, cpss_factor.c, cpss_query.c
 */

/* ══════════════════════════════════════════════════════════════════════
 * A) PER-SPACE STATS STRUCTS + ACCUMULATION
 * ══════════════════════════════════════════════════════════════════════ */

/** Common stats computed for any region regardless of space. */
typedef struct {
    size_t cell_count;
    uint64_t min_N_u64, max_N_u64;  /* only valid if product fits u64 */
    double sum_N;                    /* double accumulator for mean */
    uint64_t first_i, first_j, first_p, first_q;
    uint64_t last_i, last_j, last_p, last_q;
} CommonRegionStats;

static void common_stats_init(CommonRegionStats* s) { memset(s, 0, sizeof(*s)); s->min_N_u64 = UINT64_MAX; }
static void common_stats_update(CommonRegionStats* s, const IndexSpaceCell* c) {
    uint64_t n64 = u128_fits_u64(c->product) ? c->product.lo : UINT64_MAX;
    if (s->cell_count == 0u) {
        s->first_i = c->i; s->first_j = c->j; s->first_p = c->p; s->first_q = c->q;
    }
    s->last_i = c->i; s->last_j = c->j; s->last_p = c->p; s->last_q = c->q;
    if (n64 < s->min_N_u64) s->min_N_u64 = n64;
    if (n64 > s->max_N_u64) s->max_N_u64 = n64;
    s->sum_N += (double)n64;
    ++s->cell_count;
}
static void common_stats_print(const CommonRegionStats* s) {
    if (s->cell_count == 0u) { printf("  (no cells)\n"); return; }
    printf("  cells       = %zu\n", s->cell_count);
    printf("  N range     = %"PRIu64" .. %"PRIu64"\n", s->min_N_u64, s->max_N_u64);
    printf("  mean N      = %.1f\n", s->sum_N / (double)s->cell_count);
    printf("  first       = (%"PRIu64",%"PRIu64") %"PRIu64"*%"PRIu64"\n", s->first_i, s->first_j, s->first_p, s->first_q);
    printf("  last        = (%"PRIu64",%"PRIu64") %"PRIu64"*%"PRIu64"\n", s->last_i, s->last_j, s->last_p, s->last_q);
}

/** Index-space specific stats. */
typedef struct {
    uint64_t min_i, max_i, min_j, max_j;
    size_t diag_count, offdiag_count;
} IndexStats;
static void index_stats_init(IndexStats* s) { memset(s, 0, sizeof(*s)); s->min_i = UINT64_MAX; s->min_j = UINT64_MAX; }
static void index_stats_update(IndexStats* s, const IndexSpaceCell* c) {
    if (c->i < s->min_i) s->min_i = c->i; if (c->i > s->max_i) s->max_i = c->i;
    if (c->j < s->min_j) s->min_j = c->j; if (c->j > s->max_j) s->max_j = c->j;
    if (c->i == c->j) ++s->diag_count; else ++s->offdiag_count;
}
static void index_stats_print(const IndexStats* s) {
    printf("  i range     = %"PRIu64" .. %"PRIu64"\n", s->min_i, s->max_i);
    printf("  j range     = %"PRIu64" .. %"PRIu64"\n", s->min_j, s->max_j);
    printf("  diagonal    = %zu  off-diagonal = %zu\n", s->diag_count, s->offdiag_count);
}

/** Value-space stats. */
typedef struct {
    double min_dist, max_dist, sum_dist;
    double min_ratio, max_ratio, sum_ratio;
    size_t near_sq, moderate, skewed;
} ValueStats;
static void value_stats_init(ValueStats* s) { memset(s, 0, sizeof(*s)); s->min_dist = 1e30; s->min_ratio = 1e30; }
static void value_stats_update(ValueStats* s, const IndexSpaceCell* c) {
    double d = (double)value_space_additive_distance(c->p, c->q);
    double r = value_space_balance_ratio(c->p, c->q);
    if (d < s->min_dist) s->min_dist = d; if (d > s->max_dist) s->max_dist = d; s->sum_dist += d;
    if (r < s->min_ratio) s->min_ratio = r; if (r > s->max_ratio) s->max_ratio = r; s->sum_ratio += r;
    ValueSpaceRegion reg = value_space_region(c->p, c->q);
    if (reg == VS_NEAR_SQUARE) ++s->near_sq; else if (reg == VS_MODERATE) ++s->moderate; else ++s->skewed;
}
static void value_stats_print(const ValueStats* s, size_t n) {
    if (n == 0u) return;
    printf("  additive dist  min=%.0f  max=%.0f  mean=%.1f\n", s->min_dist, s->max_dist, s->sum_dist / n);
    printf("  balance ratio  min=%.4f  max=%.4f  mean=%.4f\n", s->min_ratio, s->max_ratio, s->sum_ratio / n);
    printf("  regions        near-sq=%zu  moderate=%zu  skewed=%zu\n", s->near_sq, s->moderate, s->skewed);
}

/** Log-space stats. */
typedef struct {
    double min_uv, max_uv, sum_uv;   /* |u-v| */
    double min_imb, max_imb, sum_imb; /* normalised */
} LogStats;
static void log_stats_init(LogStats* s) { memset(s, 0, sizeof(*s)); s->min_uv = 1e30; s->min_imb = 1e30; }
static void log_stats_update(LogStats* s, const IndexSpaceCell* c) {
    double uv = log_space_diagonal_distance(c->p, c->q);
    double imb = log_space_imbalance(c->p, c->q);
    if (uv < s->min_uv) s->min_uv = uv; if (uv > s->max_uv) s->max_uv = uv; s->sum_uv += uv;
    if (imb < s->min_imb) s->min_imb = imb; if (imb > s->max_imb) s->max_imb = imb; s->sum_imb += imb;
}
static void log_stats_print(const LogStats* s, size_t n) {
    if (n == 0u) return;
    printf("  |u-v|          min=%.4f  max=%.4f  mean=%.4f\n", s->min_uv, s->max_uv, s->sum_uv / n);
    printf("  imbalance      min=%.4f  max=%.4f  mean=%.4f\n", s->min_imb, s->max_imb, s->sum_imb / n);
}

/** Mean-imbalance stats. */
typedef struct {
    double min_delta, max_delta, sum_delta;
    double min_dm, max_dm, sum_dm; /* |delta|/m */
    size_t near_sq, moderate, unbal, skewed;
} MeanImbStats;
static void mi_stats_init(MeanImbStats* s) { memset(s, 0, sizeof(*s)); s->min_delta = 1e30; s->min_dm = 1e30; }
static void mi_stats_update(MeanImbStats* s, const IndexSpaceCell* c) {
    double m, delta; mean_imbalance_from_factors(c->p, c->q, &m, &delta);
    double ad = fabs(delta);
    double dm = m > 0.0 ? ad / m : 0.0;
    if (ad < s->min_delta) s->min_delta = ad; if (ad > s->max_delta) s->max_delta = ad; s->sum_delta += ad;
    if (dm < s->min_dm) s->min_dm = dm; if (dm > s->max_dm) s->max_dm = dm; s->sum_dm += dm;
    MeanImbalanceBias bias = mean_imbalance_bias(m, delta);
    if (bias == MI_BIAS_NEAR_SQUARE) ++s->near_sq; else if (bias == MI_BIAS_MODERATE) ++s->moderate;
    else if (bias == MI_BIAS_UNBALANCED) ++s->unbal; else ++s->skewed;
}
static void mi_stats_print(const MeanImbStats* s, size_t n) {
    if (n == 0u) return;
    printf("  |delta|        min=%.4f  max=%.4f  mean=%.4f\n", s->min_delta, s->max_delta, s->sum_delta / n);
    printf("  |delta|/m      min=%.4f  max=%.4f  mean=%.4f\n", s->min_dm, s->max_dm, s->sum_dm / n);
    printf("  bias           near-sq=%zu  moderate=%zu  unbal=%zu  skewed=%zu\n", s->near_sq, s->moderate, s->unbal, s->skewed);
}

/** Product-ratio stats. */
typedef struct {
    double min_R, max_R, sum_R;
    size_t balanced, moderate, skewed;
} RatioStats;
static void ratio_stats_init(RatioStats* s) { memset(s, 0, sizeof(*s)); s->min_R = 1e30; }
static void ratio_stats_update(RatioStats* s, const IndexSpaceCell* c) {
    double R = ratio_from_factors(c->p, c->q);
    if (R < s->min_R) s->min_R = R; if (R > s->max_R) s->max_R = R; s->sum_R += R;
    RatioClass rc = ratio_classify(R);
    if (rc == RC_BALANCED) ++s->balanced; else if (rc == RC_MODERATE) ++s->moderate; else ++s->skewed;
}
static void ratio_stats_print(const RatioStats* s, size_t n) {
    if (n == 0u) return;
    printf("  R              min=%.4f  max=%.4f  mean=%.4f\n", s->min_R, s->max_R, s->sum_R / n);
    printf("  classes        balanced=%zu  moderate=%zu  skewed=%zu\n", s->balanced, s->moderate, s->skewed);
}

/** Fermat-space stats. */
typedef struct {
    double min_B, max_B, sum_B;
    double min_steps, max_steps, sum_steps;
    size_t exact_count, half_int_count;
} FermatStats;
static void fermat_stats_init(FermatStats* s) { memset(s, 0, sizeof(*s)); s->min_B = 1e30; s->min_steps = 1e30; }
static void fermat_stats_update(FermatStats* s, const IndexSpaceCell* c) {
    uint64_t Ae, Be;
    bool exact = fermat_space_from_factors_exact(c->p, c->q, &Ae, &Be);
    if (exact) {
        ++s->exact_count;
        double b = (double)Be;
        if (b < s->min_B) s->min_B = b; if (b > s->max_B) s->max_B = b; s->sum_B += b;
        uint64_t N64 = u128_fits_u64(c->product) ? c->product.lo : 0u;
        if (N64 > 0u) {
            double st = (double)fermat_space_estimated_steps(N64, c->p, c->q);
            if (st < s->min_steps) s->min_steps = st; if (st > s->max_steps) s->max_steps = st; s->sum_steps += st;
        }
    } else { ++s->half_int_count; }
}
static void fermat_stats_print(const FermatStats* s, size_t n) {
    if (n == 0u) return;
    printf("  exact coords   = %zu  half-integer = %zu\n", s->exact_count, s->half_int_count);
    if (s->exact_count > 0u) {
        printf("  B              min=%.0f  max=%.0f  mean=%.1f\n", s->min_B, s->max_B, s->sum_B / s->exact_count);
        printf("  est. steps     min=%.0f  max=%.0f  mean=%.1f\n", s->min_steps, s->max_steps, s->sum_steps / s->exact_count);
    }
}

/** MinMax-space stats. */
typedef struct {
    double min_s, max_s, sum_s;
    double min_l, max_l, sum_l;
    size_t cov13, cov100;
} MinMaxStats;
static void mm_stats_init(MinMaxStats* s) { memset(s, 0, sizeof(*s)); s->min_s = 1e30; s->min_l = 1e30; }
static void mm_stats_update(MinMaxStats* s, const IndexSpaceCell* c) {
    uint64_t sv, lv; minmax_canonicalize(c->p, c->q, &sv, &lv);
    double sd = (double)sv, ld = (double)lv;
    if (sd < s->min_s) s->min_s = sd; if (sd > s->max_s) s->max_s = sd; s->sum_s += sd;
    if (ld < s->min_l) s->min_l = ld; if (ld > s->max_l) s->max_l = ld; s->sum_l += ld;
    if (minmax_is_covered(sv, 13u)) ++s->cov13;
    if (minmax_is_covered(sv, 100u)) ++s->cov100;
}
static void mm_stats_print(const MinMaxStats* s, size_t n) {
    if (n == 0u) return;
    printf("  s (min fac)    min=%.0f  max=%.0f  mean=%.1f\n", s->min_s, s->max_s, s->sum_s / n);
    printf("  l (max fac)    min=%.0f  max=%.0f  mean=%.1f\n", s->min_l, s->max_l, s->sum_l / n);
    printf("  covered(<=13)  = %zu/%zu  covered(<=100) = %zu/%zu\n", s->cov13, n, s->cov100, n);
}

/** Per-space stats printer callback type. */
typedef void (*SpaceStatsPrinter)(const IndexSpaceCell* cells, size_t n);

static void stats_print_index(const IndexSpaceCell* cells, size_t n) {
    CommonRegionStats cs; common_stats_init(&cs); IndexStats is; index_stats_init(&is);
    for (size_t k = 0u; k < n; ++k) { common_stats_update(&cs, &cells[k]); index_stats_update(&is, &cells[k]); }
    common_stats_print(&cs); index_stats_print(&is);
}
static void stats_print_value(const IndexSpaceCell* cells, size_t n) {
    CommonRegionStats cs; common_stats_init(&cs); ValueStats vs; value_stats_init(&vs);
    for (size_t k = 0u; k < n; ++k) { common_stats_update(&cs, &cells[k]); value_stats_update(&vs, &cells[k]); }
    common_stats_print(&cs); value_stats_print(&vs, n);
}
static void stats_print_log(const IndexSpaceCell* cells, size_t n) {
    CommonRegionStats cs; common_stats_init(&cs); LogStats ls; log_stats_init(&ls);
    for (size_t k = 0u; k < n; ++k) { common_stats_update(&cs, &cells[k]); log_stats_update(&ls, &cells[k]); }
    common_stats_print(&cs); log_stats_print(&ls, n);
}
static void stats_print_mi(const IndexSpaceCell* cells, size_t n) {
    CommonRegionStats cs; common_stats_init(&cs); MeanImbStats ms; mi_stats_init(&ms);
    for (size_t k = 0u; k < n; ++k) { common_stats_update(&cs, &cells[k]); mi_stats_update(&ms, &cells[k]); }
    common_stats_print(&cs); mi_stats_print(&ms, n);
}
static void stats_print_ratio(const IndexSpaceCell* cells, size_t n) {
    CommonRegionStats cs; common_stats_init(&cs); RatioStats rs; ratio_stats_init(&rs);
    for (size_t k = 0u; k < n; ++k) { common_stats_update(&cs, &cells[k]); ratio_stats_update(&rs, &cells[k]); }
    common_stats_print(&cs); ratio_stats_print(&rs, n);
}
static void stats_print_fermat(const IndexSpaceCell* cells, size_t n) {
    CommonRegionStats cs; common_stats_init(&cs); FermatStats fs; fermat_stats_init(&fs);
    for (size_t k = 0u; k < n; ++k) { common_stats_update(&cs, &cells[k]); fermat_stats_update(&fs, &cells[k]); }
    common_stats_print(&cs); fermat_stats_print(&fs, n);
}
static void stats_print_minmax(const IndexSpaceCell* cells, size_t n) {
    CommonRegionStats cs; common_stats_init(&cs); MinMaxStats ms; mm_stats_init(&ms);
    for (size_t k = 0u; k < n; ++k) { common_stats_update(&cs, &cells[k]); mm_stats_update(&ms, &cells[k]); }
    common_stats_print(&cs); mm_stats_print(&ms, n);
}

/** Stats registry parallel to SPACE_REGISTRY. */
static const SpaceStatsPrinter STATS_REGISTRY[] = {
    stats_print_index,
    stats_print_value,
    stats_print_log,
    stats_print_mi,
    stats_print_ratio,
    stats_print_fermat,
    stats_print_minmax,
};

static SpaceStatsPrinter stats_lookup(const char* name) {
    for (size_t i = 0u; i < SPACE_REGISTRY_COUNT; ++i) {
        if (strcmp(SPACE_REGISTRY[i].name, name) == 0) return STATS_REGISTRY[i];
    }
    return NULL;
}

/** Family-aware stats lookup: returns stats printer for a resolved SpaceEntry in prime-pair family.
 *  Returns NULL if the space is not in the prime-pair family (no stats for semiprime yet). */
static SpaceStatsPrinter family_stats_lookup(SpaceFamily fam, const SpaceEntry* sp) {
    if (fam != FAMILY_PRIMEPAIR || !sp) return NULL;
    for (size_t i = 0u; i < PRIMEPAIR_SPACE_REGISTRY_COUNT; ++i) {
        if (&PRIMEPAIR_SPACE_REGISTRY[i] == sp) return STATS_REGISTRY[i];
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * PER-N EVIDENCE CACHE: accumulates actual performed work across commands.
 * Fixed-size LRU cache keyed by N. Evidence for the same N accumulates;
 * different N values coexist up to cache capacity.
 * ══════════════════════════════════════════════════════════════════════ */

#define EVIDENCE_CACHE_SIZE 64

typedef struct {
    bool valid;
    uint64_t N;
    uint64_t last_access;         /* monotonic counter for LRU eviction */
    bool wheel_checked;
    bool wheel_found_factor;
    bool has_trial_bound;
    uint64_t trial_bound;         /* all primes <= this were actually checked and excluded */
    bool has_fermat_steps;
    uint64_t fermat_steps;        /* actual Fermat steps attempted */
    bool fermat_succeeded;
    bool found_factor;
    uint64_t found_factor_value;
    const char* last_method;
} EvidenceEntry;

static EvidenceEntry g_evidence_cache[EVIDENCE_CACHE_SIZE];
static uint64_t g_evidence_clock = 0u;

/** Find or create an evidence entry for N. LRU eviction if full. */
static EvidenceEntry* evidence_get(uint64_t N) {
    ++g_evidence_clock;
    /* Look for existing entry */
    for (int i = 0; i < EVIDENCE_CACHE_SIZE; ++i) {
        if (g_evidence_cache[i].valid && g_evidence_cache[i].N == N) {
            g_evidence_cache[i].last_access = g_evidence_clock;
            return &g_evidence_cache[i];
        }
    }
    /* Find empty slot or evict LRU */
    int best = 0;
    uint64_t oldest = UINT64_MAX;
    for (int i = 0; i < EVIDENCE_CACHE_SIZE; ++i) {
        if (!g_evidence_cache[i].valid) { best = i; break; }
        if (g_evidence_cache[i].last_access < oldest) { oldest = g_evidence_cache[i].last_access; best = i; }
    }
    memset(&g_evidence_cache[best], 0, sizeof(EvidenceEntry));
    g_evidence_cache[best].valid = true;
    g_evidence_cache[best].N = N;
    g_evidence_cache[best].last_access = g_evidence_clock;
    return &g_evidence_cache[best];
}

/** Look up evidence for N without creating. Returns NULL if not cached. */
static const EvidenceEntry* evidence_lookup(uint64_t N) {
    for (int i = 0; i < EVIDENCE_CACHE_SIZE; ++i) {
        if (g_evidence_cache[i].valid && g_evidence_cache[i].N == N) return &g_evidence_cache[i];
    }
    return NULL;
}

/** Record trial exclusion evidence. Accumulates with existing evidence for same N. */
static void work_history_record_trial(uint64_t N, uint64_t trial_limit, const FactorResult* fr) {
    EvidenceEntry* e = evidence_get(N);
    e->wheel_checked = true;
    if (!fr->fully_factored || fr->cofactor > 1u) {
        /* Trial checked up to trial_limit and didn't fully factor => exclusion */
        if (!e->has_trial_bound || trial_limit > e->trial_bound) {
            e->has_trial_bound = true;
            e->trial_bound = trial_limit;
        }
    } else if (fr->factor_count > 0u) {
        e->found_factor = true;
        e->found_factor_value = fr->factors[0];
    }
    e->last_method = "trial";
}

/** Record Fermat evidence. Accumulates with existing evidence for same N. */
static void work_history_record_fermat(uint64_t N, uint64_t max_steps, const FactorResult* fr) {
    EvidenceEntry* e = evidence_get(N);
    e->wheel_checked = true;
    if (fr->fully_factored) {
        e->fermat_succeeded = true;
        e->found_factor = true;
        if (fr->factor_count > 0u) e->found_factor_value = fr->factors[0];
    } else {
        /* Fermat tried max_steps and failed => exclusion evidence */
        if (!e->has_fermat_steps || max_steps > e->fermat_steps) {
            e->has_fermat_steps = true;
            e->fermat_steps = max_steps;
        }
        e->fermat_succeeded = false;
    }
    e->last_method = "fermat";
}

/** Record auto-router evidence from factor(auto) result. */
static void work_history_record_auto(uint64_t N, const FactorResult* fr) {
    EvidenceEntry* e = evidence_get(N);
    e->wheel_checked = true;
    /* The auto-router always checks wheel primes first */
    if (fr->fully_factored && fr->factor_count > 0u) {
        e->found_factor = true;
        e->found_factor_value = fr->factors[0];
        /* If method contains "wheel", wheel found a factor */
        if (fr->method && strstr(fr->method, "wheel")) e->wheel_found_factor = true;
    }
    e->last_method = fr->method ? fr->method : "auto";
}

/* ══════════════════════════════════════════════════════════════════════
 * D) CSV EXPORT
 * ══════════════════════════════════════════════════════════════════════ */

/** Per-space CSV header + row writer callback. */
typedef void (*SpaceCsvHeader)(FILE* fp);
typedef void (*SpaceCsvRow)(FILE* fp, const IndexSpaceCell* c);

static void csv_header_common(FILE* fp) { fprintf(fp, "i,j,p,q,N"); }
static void csv_row_common(FILE* fp, const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    fprintf(fp, "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%s", c->i, c->j, c->p, c->q, nbuf);
}

static void csv_header_index(FILE* fp) { csv_header_common(fp); fprintf(fp, "\n"); }
static void csv_row_index(FILE* fp, const IndexSpaceCell* c) { csv_row_common(fp, c); fprintf(fp, "\n"); }

static void csv_header_value(FILE* fp) { csv_header_common(fp); fprintf(fp, ",additive_dist,log_imbalance,balance_ratio,region\n"); }
static void csv_row_value(FILE* fp, const IndexSpaceCell* c) {
    csv_row_common(fp, c);
    ValueSpaceRegion reg = value_space_region(c->p, c->q);
    fprintf(fp, ",%"PRIu64",%.6f,%.6f,%s\n",
        value_space_additive_distance(c->p, c->q), value_space_log_imbalance(c->p, c->q),
        value_space_balance_ratio(c->p, c->q),
        reg == VS_NEAR_SQUARE ? "near-square" : reg == VS_MODERATE ? "moderate" : "skewed");
}

static void csv_header_log(FILE* fp) { csv_header_common(fp); fprintf(fp, ",u,v,u_plus_v,abs_u_minus_v,imbalance\n"); }
static void csv_row_log(FILE* fp, const IndexSpaceCell* c) {
    csv_row_common(fp, c); double u, v; log_space_coords(c->p, c->q, &u, &v);
    fprintf(fp, ",%.6f,%.6f,%.6f,%.6f,%.6f\n", u, v, u + v, log_space_diagonal_distance(c->p, c->q), log_space_imbalance(c->p, c->q));
}

static void csv_header_mi(FILE* fp) { csv_header_common(fp); fprintf(fp, ",m,delta,abs_delta_over_m,bias\n"); }
static void csv_row_mi(FILE* fp, const IndexSpaceCell* c) {
    csv_row_common(fp, c); double m, delta; mean_imbalance_from_factors(c->p, c->q, &m, &delta);
    MeanImbalanceBias bias = mean_imbalance_bias(m, delta);
    const char* bn = bias == MI_BIAS_NEAR_SQUARE ? "near-square" : bias == MI_BIAS_MODERATE ? "moderate"
                   : bias == MI_BIAS_UNBALANCED ? "unbalanced" : "highly-skewed";
    fprintf(fp, ",%.6f,%.6f,%.6f,%s\n", m, delta, m > 0.0 ? fabs(delta) / m : 0.0, bn);
}

static void csv_header_ratio(FILE* fp) { csv_header_common(fp); fprintf(fp, ",R,class\n"); }
static void csv_row_ratio(FILE* fp, const IndexSpaceCell* c) {
    csv_row_common(fp, c); double R = ratio_from_factors(c->p, c->q);
    RatioClass rc = ratio_classify(R);
    fprintf(fp, ",%.6f,%s\n", R, rc == RC_BALANCED ? "balanced" : rc == RC_MODERATE ? "moderate" : "skewed");
}

static void csv_header_fermat(FILE* fp) { csv_header_common(fp); fprintf(fp, ",A,B,search_start,est_steps,exact\n"); }
static void csv_row_fermat(FILE* fp, const IndexSpaceCell* c) {
    csv_row_common(fp, c);
    uint64_t Ae, Be; bool exact = fermat_space_from_factors_exact(c->p, c->q, &Ae, &Be);
    uint64_t N64 = u128_fits_u64(c->product) ? c->product.lo : 0u;
    if (exact && N64 > 0u) {
        fprintf(fp, ",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",yes\n", Ae, Be,
            fermat_space_search_start(N64), fermat_space_estimated_steps(N64, c->p, c->q));
    } else {
        double Af, Bf; fermat_space_from_factors(c->p, c->q, &Af, &Bf);
        fprintf(fp, ",%.1f,%.1f,,,no\n", Af, Bf);
    }
}

static void csv_header_minmax(FILE* fp) { csv_header_common(fp); fprintf(fp, ",s,l,cov13,cov100\n"); }
static void csv_row_minmax(FILE* fp, const IndexSpaceCell* c) {
    csv_row_common(fp, c); uint64_t sv, lv; minmax_canonicalize(c->p, c->q, &sv, &lv);
    fprintf(fp, ",%"PRIu64",%"PRIu64",%s,%s\n", sv, lv,
        minmax_is_covered(sv, 13u) ? "y" : "n", minmax_is_covered(sv, 100u) ? "y" : "n");
}

static const SpaceCsvHeader CSV_HEADERS[] = { csv_header_index, csv_header_value, csv_header_log, csv_header_mi, csv_header_ratio, csv_header_fermat, csv_header_minmax };
static const SpaceCsvRow CSV_ROWS[] = { csv_row_index, csv_row_value, csv_row_log, csv_row_mi, csv_row_ratio, csv_row_fermat, csv_row_minmax };

static int csv_lookup_idx(const char* name) {
    for (size_t i = 0u; i < SPACE_REGISTRY_COUNT; ++i) {
        if (strcmp(SPACE_REGISTRY[i].name, name) == 0) return (int)i;
    }
    return -1;
}

/** Family-aware CSV index: returns index into CSV_HEADERS/CSV_ROWS for a resolved SpaceEntry. */
static int family_csv_lookup_idx(SpaceFamily fam, const SpaceEntry* sp) {
    if (fam != FAMILY_PRIMEPAIR || !sp) return -1;
    for (size_t i = 0u; i < PRIMEPAIR_SPACE_REGISTRY_COUNT; ++i) {
        if (&PRIMEPAIR_SPACE_REGISTRY[i] == sp) return (int)i;
    }
    return -1;
}
