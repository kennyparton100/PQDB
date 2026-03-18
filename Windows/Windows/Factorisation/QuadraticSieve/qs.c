/**
 * cpss_factor_qs.c - Quadratic Sieve with multi-polynomial support.
 * Part of the CPSS Viewer amalgamation.
 *
 * Small inputs (<30 bits): shifted-center multi-polynomial.
 * Larger inputs: SIQS with CRT-based polynomial generation.
 * Single-large-prime variation: LP tracked as virtual matrix column.
 * Gaussian elimination over GF(2) with nullspace combination extraction.
 *
 * Depends on: cpss_arith.c (isqrt_u64, gcd_u64, miller_rabin_u64, mulmod_u64, powmod_u64)
 */

#define QS_MAX_FB     512
#define QS_MAX_SIEVE  65536
#define QS_MAX_RELS   (QS_MAX_FB + 320)
#define QS_MAX_POLYS  512
#define QS_MAX_LP_COLS 256    /* max distinct large primes tracked as virtual columns */
#define QS_LP_MULT    64     /* large prime bound = LP_MULT * largest FB prime */
#define QS_TOTAL_COLS (QS_MAX_FB + QS_MAX_LP_COLS)  /* FB cols + LP virtual cols */

/** QS tunable configuration. All fields have defaults from qs_config_default_for_bits().
 *  CLI flags override individual fields for research/debug tuning. */
typedef struct {
    int fb_size;           /* factor base size */
    int sieve_half;        /* sieve interval half-width */
    int max_polys;         /* max polynomial count */
    int max_rels;          /* relation budget */
    int lp_mult;           /* LP bound = lp_mult * largest FB prime (0 = disable LP) */
    int sp_warmup_polys;   /* max single-poly warmup iterations before SIQS */
    int sp_warmup_rels;    /* max single-poly relations before switching to SIQS */
    bool siqs_only;        /* force SIQS-only, skip single-poly warmup entirely */
    bool diag;             /* print diagnostic / research output */
    bool diverse;          /* diversity-enforced collection: cap rows per source family */
    int family_cap;        /* max rows per source family (0 = auto: max_rels / 8) */
} QSConfig;

/** Initialize QS config with default heuristics for given bit length. */
static void qs_config_default_for_bits(unsigned bits, QSConfig* cfg) {
    cfg->fb_size = (bits < 25) ? 30 : (bits < 35) ? 60 : (bits < 45) ? 180
                 : (bits < 55) ? 250 : 400;
    if (cfg->fb_size > QS_MAX_FB) cfg->fb_size = QS_MAX_FB;

    cfg->sieve_half = (bits < 25) ? 4096 : (bits < 35) ? 8192 : (bits < 45) ? 16384
                    : (bits < 55) ? 32768 : 65536;
    if (cfg->sieve_half > QS_MAX_SIEVE) cfg->sieve_half = QS_MAX_SIEVE;

    cfg->max_polys = (bits < 30) ? 8 : (bits < 40) ? 32 : (bits < 50) ? 128 : 512;
    if (cfg->max_polys > QS_MAX_POLYS) cfg->max_polys = QS_MAX_POLYS;

    cfg->max_rels = 0; /* 0 = auto: fb_count + 256 */
    cfg->lp_mult = QS_LP_MULT;
    cfg->sp_warmup_polys = 4;
    cfg->sp_warmup_rels = 0; /* 0 = auto: fb_count / 3 */
    cfg->siqs_only = false;
    cfg->diag = false;
    cfg->diverse = false;
    cfg->family_cap = 0; /* 0 = auto */
}

static uint64_t qs_modsqrt(uint64_t n, uint64_t p) {
    if (p == 2u) return n & 1u;
    n %= p;
    if (n == 0u) return 0u;
    if (powmod_u64(n, (p - 1u) / 2u, p) != 1u) return 0u;
    if ((p & 3u) == 3u) return powmod_u64(n, (p + 1u) / 4u, p);
    uint64_t Q = p - 1u, S = 0u;
    while ((Q & 1u) == 0u) { Q >>= 1u; ++S; }
    uint64_t z = 2u;
    while (powmod_u64(z, (p - 1u) / 2u, p) != p - 1u) { ++z; if (z >= p) return 0u; }
    uint64_t M = S, c = powmod_u64(z, Q, p);
    uint64_t t = powmod_u64(n, Q, p), R = powmod_u64(n, (Q + 1u) / 2u, p);
    for (;;) {
        if (t == 1u) return R;
        uint64_t i = 0u, tmp = t;
        while (tmp != 1u) { tmp = mulmod_u64(tmp, tmp, p); ++i; if (i >= M) return 0u; }
        uint64_t b = c;
        for (uint64_t j = 0u; j < M - i - 1u; ++j) b = mulmod_u64(b, b, p);
        M = i; c = mulmod_u64(b, b, p); t = mulmod_u64(t, c, p); R = mulmod_u64(R, b, p);
    }
}

/** Sieve one polynomial Q(x) = (x+center)^2 - N over [-M, M].
 *  Collects fully-smooth and single-large-prime partial relations.
 *  For partials, the LP gets a virtual column in the parity matrix. */
static int qs_sieve_single_poly(
    uint64_t target, uint64_t center,
    const uint64_t* fb, const uint64_t* fb_sqrt, int fb_count,
    int sieve_half, double threshold,
    uint64_t* matrix, int row_words, uint64_t* rel_x, uint64_t* rel_Qx,
    uint64_t* rel_lp, uint64_t lp_bound,
    uint64_t* lp_hash_vals, int* lp_col_count,
    int rel_count, int max_rels,
    FactorResult* fr, double t0)
{
    int total_sieve = 2 * sieve_half + 1;
    double* sieve_log = (double*)xmalloc((size_t)total_sieve * sizeof(double));

    for (int xi = 0; xi < total_sieve; ++xi) {
        int64_t x = (int64_t)xi - sieve_half;
        int64_t xc = x + (int64_t)center;
        if (xc <= 0) { sieve_log[xi] = 99.0; continue; }
        uint64_t xc_u = (uint64_t)xc;
        if (xc_u * xc_u < target) { sieve_log[xi] = 99.0; continue; }
        uint64_t Qx = xc_u * xc_u - target;
        if (Qx == 0u) {
            uint64_t g = gcd_u64(xc_u, target);
            if (g > 1u && g < target) {
                factor_result_set_split(fr, g, target / g);
                free(sieve_log);
                fr->time_seconds = now_sec() - t0;
                return -1; /* signal: factor found directly */
            }
            sieve_log[xi] = 99.0; continue;
        }
        sieve_log[xi] = log2((double)Qx);
    }

    for (int fi = 0; fi < fb_count; ++fi) {
        uint64_t p = fb[fi];
        double logp = log2((double)p);
        if (p == 2u) {
            for (int xi = 0; xi < total_sieve; xi += 2) sieve_log[xi] -= logp;
            continue;
        }
        uint64_t c_mod = center % p;
        uint64_t root1 = (fb_sqrt[fi] + p - c_mod) % p;
        uint64_t root2 = (p - fb_sqrt[fi] + p - c_mod) % p;
        for (int64_t xi = (int64_t)root1; xi < total_sieve; xi += (int64_t)p)
            if (xi >= 0) sieve_log[xi] -= logp;
        if (root2 != root1)
            for (int64_t xi = (int64_t)root2; xi < total_sieve; xi += (int64_t)p)
                if (xi >= 0) sieve_log[xi] -= logp;
    }

    int added = 0;
    for (int xi = 0; xi < total_sieve && rel_count + added < max_rels; ++xi) {
        if (sieve_log[xi] > threshold) continue;
        int64_t x = (int64_t)xi - sieve_half;
        int64_t xc = x + (int64_t)center;
        if (xc <= 0) continue;
        uint64_t xc_u = (uint64_t)xc;
        if (xc_u * xc_u < target) continue;
        uint64_t Qx = xc_u * xc_u - target;
        if (Qx == 0u) continue;

        uint64_t rem = Qx;
        uint64_t* row = &matrix[(rel_count + added) * row_words];
        memset(row, 0, (size_t)row_words * sizeof(uint64_t));
        for (int fi = 0; fi < fb_count; ++fi) {
            int exp = 0;
            while (rem % fb[fi] == 0u) { rem /= fb[fi]; ++exp; }
            if (exp & 1) row[fi / 64] ^= (1ull << (fi % 64));
        }
        if (rem == 1u) {
            /* Fully smooth relation */
            rel_x[rel_count + added] = xc_u;
            rel_Qx[rel_count + added] = Qx;
            rel_lp[rel_count + added] = 0u;
            ++added;
        } else if (rem <= lp_bound && rem > 1u && miller_rabin_u64(rem)) {
            /* Partial: one large prime cofactor. Assign LP virtual column. */
            int lp_col = -1;
            for (int li = 0; li < *lp_col_count; ++li) {
                if (lp_hash_vals[li] == rem) { lp_col = li; break; }
            }
            if (lp_col < 0 && *lp_col_count < QS_MAX_LP_COLS) {
                lp_col = (*lp_col_count)++;
                lp_hash_vals[lp_col] = rem;
            }
            if (lp_col >= 0) {
                int col_idx = fb_count + lp_col;
                row[col_idx / 64] ^= (1ull << (col_idx % 64));
                rel_x[rel_count + added] = xc_u;
                rel_Qx[rel_count + added] = Qx;
                rel_lp[rel_count + added] = rem;
                ++added;
            }
        }
    }

    free(sieve_log);
    return added;
}

/** Sieve one SIQS polynomial Q(x) = a*x^2 + 2*b*x + c where a = p1*p2 from FB.
 *  b computed via CRT, c = (b^2 - N) / a (exact, via U128).
 *  Relation identity: (a*x + b)^2 = N + a*Q(x), so (a*x+b)^2 ≡ a*Q(x) (mod N).
 *  We store rel_x = (a*x+b) mod N and rel_Qx = a*|Q(x)| so that rel_x^2 ≡ rel_Qx (mod N).
 *  Returns count of relations added, or -1 if a direct factor was found. */
static int qs_sieve_siqs_poly(
    uint64_t target, uint64_t a, int64_t b_signed, int64_t c_signed,
    uint64_t p1, uint64_t p2, int ai1, int ai2,
    const uint64_t* fb, const uint64_t* fb_sqrt, int fb_count,
    int sieve_half, double threshold,
    uint64_t* matrix, int row_words, uint64_t* rel_x, uint64_t* rel_Qx,
    uint64_t* rel_lp, uint64_t lp_bound,
    uint64_t* lp_hash_vals, int* lp_col_count,
    int rel_count, int max_rels,
    FactorResult* fr, double t0)
{
    int total_sieve = 2 * sieve_half + 1;
    double* sieve_log = (double*)xmalloc((size_t)total_sieve * sizeof(double));

    /* Initialize sieve with log2(|Q(x)|) — NOT log2(a*|Q(x)|).
     * Since a is a product of FB primes, it is known-smooth by construction.
     * The sieve only needs to predict whether |Q(x)| itself is FB-smooth.
     * The a factor's exponents are injected into the parity row afterward. */
    for (int xi = 0; xi < total_sieve; ++xi) {
        int64_t x = (int64_t)xi - sieve_half;
        int64_t Qx = (int64_t)a * x * x + 2 * b_signed * x + c_signed;
        int64_t Qx_abs = (Qx >= 0) ? Qx : -Qx;
        if (Qx_abs == 0) {
            int64_t axb = (int64_t)a * x + b_signed;
            uint64_t axb_abs = (axb >= 0) ? (uint64_t)axb : (uint64_t)(-axb);
            if (axb_abs > 1u) {
                uint64_t g = gcd_u64(axb_abs % target, target);
                if (g > 1u && g < target) {
                    factor_result_set_split(fr, g, target / g);
                    free(sieve_log); fr->time_seconds = now_sec() - t0;
                    return -1;
                }
            }
            sieve_log[xi] = 99.0; continue;
        }
        sieve_log[xi] = log2((double)Qx_abs);
    }

    /* Sieve: subtract log(p) at roots of Q(x) ≡ 0 (mod p).
     * For p | a: Q(x) ≡ 2bx + c (mod p), one root.
     * Do NOT subtract log(p1) or log(p2) for the a-factor — those are
     * known-smooth and handled separately in the parity row injection. */
    for (int fi = 0; fi < fb_count; ++fi) {
        uint64_t p = fb[fi];
        double logp = log2((double)p);
        if (p == 2u) {
            for (int xi = 0; xi < total_sieve; xi += 2) sieve_log[xi] -= logp;
            continue;
        }
        if (p == p1 || p == p2) {
            /* p divides a: Q(x) ≡ 2bx + c (mod p), one root */
            uint64_t b_mod = ((b_signed % (int64_t)p) + (int64_t)p) % p;
            uint64_t two_b = (2u * b_mod) % p;
            if (two_b == 0u) continue;
            uint64_t two_b_inv = powmod_u64(two_b, p - 2u, p);
            uint64_t c_mod = ((c_signed % (int64_t)p) + (int64_t)p) % p;
            uint64_t root = mulmod_u64(p - c_mod, two_b_inv, p);
            root = (root + (uint64_t)sieve_half) % p;
            for (int64_t xi = (int64_t)root; xi < total_sieve; xi += (int64_t)p)
                if (xi >= 0) sieve_log[xi] -= logp;
            continue;
        }
        /* General: x ≡ (±√N - b) * a^{-1} (mod p) */
        uint64_t a_inv = powmod_u64(a % p, p - 2u, p);
        uint64_t b_mod = ((b_signed % (int64_t)p) + (int64_t)p) % p;
        uint64_t neg_b = (p - b_mod) % p;
        uint64_t r1 = mulmod_u64((neg_b + fb_sqrt[fi]) % p, a_inv, p);
        uint64_t r2 = mulmod_u64((neg_b + p - fb_sqrt[fi]) % p, a_inv, p);
        r1 = (r1 + (uint64_t)sieve_half) % p;
        r2 = (r2 + (uint64_t)sieve_half) % p;
        for (int64_t xi = (int64_t)r1; xi < total_sieve; xi += (int64_t)p)
            if (xi >= 0) sieve_log[xi] -= logp;
        if (r2 != r1)
            for (int64_t xi = (int64_t)r2; xi < total_sieve; xi += (int64_t)p)
                if (xi >= 0) sieve_log[xi] -= logp;
    }

    /* Collect fully-smooth relations.
     * Trial-factor |Q(x)| over FB (not a*|Q(x)|).
     * Then inject a's known FB-prime exponents into the parity row.
     * Store a*|Q(x)| as rel_Qx for the extraction identity (ax+b)² ≡ a·Q(x) (mod N). */
    int added = 0;
    for (int xi = 0; xi < total_sieve && rel_count + added < max_rels; ++xi) {
        if (sieve_log[xi] > threshold) continue;
        int64_t x = (int64_t)xi - sieve_half;
        int64_t Qx = (int64_t)a * x * x + 2 * b_signed * x + c_signed;
        uint64_t Qx_abs = (Qx >= 0) ? (uint64_t)Qx : (uint64_t)(-Qx);
        if (Qx_abs == 0u) continue;

        /* Trial-factor |Q(x)| over FB — a is NOT included here */
        uint64_t rem = Qx_abs;
        uint64_t* row = &matrix[(rel_count + added) * row_words];
        memset(row, 0, (size_t)row_words * sizeof(uint64_t));
        for (int fi = 0; fi < fb_count; ++fi) {
            int exp = 0;
            while (rem % fb[fi] == 0u) { rem /= fb[fi]; ++exp; }
            if (exp & 1) row[fi / 64] ^= (1ull << (fi % 64));
        }
        /* Determine if fully smooth or single-LP partial */
        bool is_smooth = (rem == 1u);
        bool is_partial = (!is_smooth && rem <= lp_bound && rem > 1u && miller_rabin_u64(rem));
        if (!is_smooth && !is_partial) continue;

        /* Inject a's known FB-prime exponents into the parity row.
         * a = p1 * p2, each appears once (odd exponent → toggle parity). */
        row[ai1 / 64] ^= (1ull << (ai1 % 64));
        row[ai2 / 64] ^= (1ull << (ai2 % 64));

        /* If partial, assign LP virtual column */
        if (is_partial) {
            int lp_col = -1;
            for (int li = 0; li < *lp_col_count; ++li) {
                if (lp_hash_vals[li] == rem) { lp_col = li; break; }
            }
            if (lp_col < 0 && *lp_col_count < QS_MAX_LP_COLS) {
                lp_col = (*lp_col_count)++;
                lp_hash_vals[lp_col] = rem;
            }
            if (lp_col < 0) continue; /* LP table full */
            int col_idx = fb_count + lp_col;
            row[col_idx / 64] ^= (1ull << (col_idx % 64));
        }

        /* Relation x-value: (a*x + b) mod N */
        int64_t axb = (int64_t)a * x + b_signed;
        uint64_t axb_mod;
        if (axb >= 0) axb_mod = (uint64_t)axb % target;
        else axb_mod = target - ((uint64_t)(-axb) % target);

        /* Store a*|Q(x)| as rel_Qx for extraction: (ax+b)² ≡ a·Q(x) (mod N) */
        uint64_t aQx = a * Qx_abs;
        if (Qx_abs > 0u && aQx / Qx_abs != a) continue; /* overflow guard */

        rel_x[rel_count + added] = axb_mod;
        rel_Qx[rel_count + added] = aQx;
        rel_lp[rel_count + added] = is_partial ? rem : 0u;
        ++added;
    }

    free(sieve_log);
    return added;
}

/** QS with explicit config. Called by cpss_factor_qs_u64 (default) or CLI with overrides. */
static FactorResult cpss_factor_qs_u64_ex(uint64_t N, const QSConfig* cfg) {
    FactorResult fr;
    factor_result_init(&fr, N, "qs");
    double t0 = now_sec();

    if (N <= 1u) { fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr; }
    while ((fr.cofactor & 1u) == 0u) { factor_result_add(&fr, 2u); fr.cofactor /= 2u; }
    if (fr.cofactor <= 1u) { fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr; }
    if (miller_rabin_u64(fr.cofactor)) {
        factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u;
        fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr;
    }

    uint64_t target = fr.cofactor;
    unsigned bits = 0u;
    { uint64_t tmp = target; while (tmp > 0u) { ++bits; tmp >>= 1u; } }

    int fb_size = cfg->fb_size;
    if (fb_size > QS_MAX_FB) fb_size = QS_MAX_FB;
    int sieve_half = cfg->sieve_half;
    if (sieve_half > QS_MAX_SIEVE) sieve_half = QS_MAX_SIEVE;
    int max_polys = cfg->max_polys;
    if (max_polys > QS_MAX_POLYS) max_polys = QS_MAX_POLYS;

    if (cfg->diag) {
        printf("  [QS diag] effective settings:\n");
        printf("    bits=%u  fb_size=%d  sieve_half=%d  max_polys=%d\n", bits, fb_size, sieve_half, max_polys);
        printf("    lp_mult=%d  sp_warmup_polys=%d  sp_warmup_rels=%d  siqs_only=%s\n",
            cfg->lp_mult, cfg->sp_warmup_polys, cfg->sp_warmup_rels > 0 ? cfg->sp_warmup_rels : -1,
            cfg->siqs_only ? "yes" : "no");
    }

    /* Build factor base */
    uint64_t* fb = (uint64_t*)xmalloc((size_t)fb_size * sizeof(uint64_t));
    uint64_t* fb_sqrt = (uint64_t*)xmalloc((size_t)fb_size * sizeof(uint64_t));
    int fb_count = 0;
    fb[0] = 2u; fb_sqrt[0] = target & 1u; fb_count = 1;
    for (uint64_t p = 3u; fb_count < fb_size && p < 200000u; p += 2u) {
        if (!miller_rabin_u64(p)) continue;
        uint64_t r = qs_modsqrt(target % p, p);
        if (r == 0u && (target % p) != 0u) continue;
        fb[fb_count] = p; fb_sqrt[fb_count] = r; ++fb_count;
    }
    if (fb_count < 5) {
        free(fb); free(fb_sqrt);
        fr.status = (int)CPSS_INCOMPLETE; fr.time_seconds = now_sec() - t0; return fr;
    }

    /* LP hash table: maps large prime values to virtual column indices.
     * Virtual column index for LP = fb_count + lp_slot.
     * row_words covers fb_count + lp_col_count total columns. */
    uint64_t lp_bound = (cfg->lp_mult > 0) ? fb[fb_count - 1] * (uint64_t)cfg->lp_mult : 0u;
    uint64_t lp_hash_vals[QS_MAX_LP_COLS];
    int lp_col_count = 0;
    memset(lp_hash_vals, 0, sizeof(lp_hash_vals));

    int total_cols = fb_count + QS_MAX_LP_COLS;
    int row_words = (total_cols + 63) / 64;
    int max_rels = (cfg->max_rels > 0) ? cfg->max_rels : (fb_count + 256);
    if (max_rels > QS_MAX_RELS) max_rels = QS_MAX_RELS;

    uint64_t* matrix = (uint64_t*)xmalloc((size_t)max_rels * (size_t)row_words * sizeof(uint64_t));
    memset(matrix, 0, (size_t)max_rels * (size_t)row_words * sizeof(uint64_t));
    uint64_t* rel_x = (uint64_t*)xmalloc((size_t)max_rels * sizeof(uint64_t));
    uint64_t* rel_Qx = (uint64_t*)xmalloc((size_t)max_rels * sizeof(uint64_t));
    uint64_t* rel_lp = (uint64_t*)xcalloc((size_t)max_rels, sizeof(uint64_t));
    int* rel_family = (int*)xcalloc((size_t)max_rels, sizeof(int)); /* 0=sp, 1..N=SIQS family */
    int rel_count = 0;

    /* Diversity enforcement: cap rows per source family */
    int div_cap = 0; /* 0 = no cap (default mode) */
    if (cfg->diverse) {
        div_cap = (cfg->family_cap > 0) ? cfg->family_cap : (max_rels / 8);
        if (div_cap < 4) div_cap = 4;
    }
    /* Family row counts: index 0 = sp, index 1..256 = SIQS families */
    #define QS_MAX_FAMILIES 257
    int family_rows[QS_MAX_FAMILIES];
    memset(family_rows, 0, sizeof(family_rows));
    int next_siqs_family = 1; /* next SIQS family ID to assign */

    uint64_t s = (uint64_t)isqrt_u64(target);
    if (s * s < target) ++s;

    /* Smoothness threshold: accept candidates where residual log is small.
     * For LP variation: allow residual up to log2(lp_bound) so near-smooth values
     * with one large prime cofactor are also collected. */
    double threshold = log2((double)lp_bound) + 1.0;
    if (threshold < 10.0) threshold = 10.0;

    /* Multi-polynomial sieving strategy:
     * Small inputs (< 30 bits): single-poly only.
     * Larger inputs: SIQS-first with limited single-poly warmup.
     * Cap single-poly to a few warmup iterations, then use SIQS exclusively
     * to maximize relation-source diversity across different a values. */
    bool use_siqs = cfg->siqs_only || (bits >= 30 && fb_count >= 10);
    int sp_rels = 0, siqs_rels = 0, siqs_a_count = 0;

    /* SIQS a-selection: enumerate all distinct pairs (i,j) from FB middle range */
    int a_lo = fb_count / 4;
    if (a_lo < 2) a_lo = 2;
    int a_hi = (fb_count * 3) / 4;
    if (a_hi <= a_lo) a_hi = fb_count - 1;
    if (a_hi >= fb_count) a_hi = fb_count - 1;

    #define QS_FREE_ALL() do { free(fb); free(fb_sqrt); free(matrix); free(rel_x); free(rel_Qx); free(rel_lp); free(rel_family); } while(0)

    /* Helper: stamp family ID on newly added relations and enforce diversity cap.
     * Returns the number of relations actually kept (may be less than added if capped). */
    #define QS_STAMP_FAMILY(added, fam_id) do { \
        int _kept = 0; \
        for (int _ri = 0; _ri < (added); ++_ri) { \
            int _idx = rel_count + _kept; \
            int _src = rel_count + _ri; \
            if (div_cap > 0 && (fam_id) < QS_MAX_FAMILIES && family_rows[(fam_id)] >= div_cap) { \
                continue; /* skip: this family is at cap */ \
            } \
            if (_kept != _ri) { \
                for (int _w = 0; _w < row_words; ++_w) matrix[_idx * row_words + _w] = matrix[_src * row_words + _w]; \
                rel_x[_idx] = rel_x[_src]; \
                rel_Qx[_idx] = rel_Qx[_src]; \
                rel_lp[_idx] = rel_lp[_src]; \
            } \
            rel_family[_idx] = (fam_id); \
            if ((fam_id) < QS_MAX_FAMILIES) ++family_rows[(fam_id)]; \
            ++_kept; \
        } \
        (added) = _kept; \
    } while(0)

    if (!use_siqs) {
        /* Small input: all single-poly */
        for (int poly = 0; poly < max_polys && rel_count < max_rels; ++poly) {
            uint64_t center = s + (uint64_t)poly * (uint64_t)sieve_half;
            int added = qs_sieve_single_poly(target, center, fb, fb_sqrt, fb_count,
                sieve_half, threshold, matrix, row_words, rel_x, rel_Qx,
                rel_lp, lp_bound, lp_hash_vals, &lp_col_count,
                rel_count, max_rels, &fr, t0);
            if (added < 0) { QS_FREE_ALL(); return fr; }
            QS_STAMP_FAMILY(added, 0);
            rel_count += added;
        }
    } else {
        /* Phase 1: limited single-poly warmup (skipped if siqs_only) */
        int sp_cap = (cfg->sp_warmup_rels > 0) ? cfg->sp_warmup_rels : (fb_count / 3);
        if (sp_cap < 10) sp_cap = 10;
        int sp_max_polys = cfg->siqs_only ? 0 : cfg->sp_warmup_polys;
        for (int poly = 0; poly < sp_max_polys && sp_rels < sp_cap; ++poly) {
            uint64_t center = s + (uint64_t)poly * (uint64_t)sieve_half;
            int added = qs_sieve_single_poly(target, center, fb, fb_sqrt, fb_count,
                sieve_half, threshold, matrix, row_words, rel_x, rel_Qx,
                rel_lp, lp_bound, lp_hash_vals, &lp_col_count,
                rel_count, max_rels, &fr, t0);
            if (added < 0) { QS_FREE_ALL(); return fr; }
            QS_STAMP_FAMILY(added, 0);
            sp_rels += added;
            rel_count += added;
        }

        /* Phase 2: SIQS polynomials — systematically enumerate all (i,j) pairs
         * for maximum a-value diversity. Don't stop at max_rels — keep trying
         * new a values to increase diversity even after the budget is mostly full.
         * Relations beyond max_rels are silently dropped but the diversity search
         * continues to find polynomials that produce diverse smooth relations. */
        int siqs_polys_tried = 0;
        int siqs_poly_limit = max_polys * 2; /* generous poly budget */
        for (int i = a_lo; i < a_hi && siqs_polys_tried < siqs_poly_limit; ++i) {
            for (int j = i + 1; j <= a_hi && siqs_polys_tried < siqs_poly_limit; ++j) {
                uint64_t p1 = fb[i], p2 = fb[j];
                if (p1 <= 2u || p2 <= 2u) continue;
                uint64_t a = p1 * p2;
                if (a == 0u) continue;

                /* Check gcd(a, N) */
                { uint64_t ga = gcd_u64(a, target);
                  if (ga > 1u && ga < target) {
                    factor_result_set_split(&fr, ga, target / ga);
                    free(fb); free(fb_sqrt); free(matrix); free(rel_x); free(rel_Qx);
                    fr.time_seconds = now_sec() - t0; return fr;
                }}

                /* CRT: b ≡ √N (mod p1), b ≡ √N (mod p2) */
                uint64_t r1 = fb_sqrt[i], r2 = fb_sqrt[j];
                if (r1 == 0u || r2 == 0u) continue;

                uint64_t p1_inv = powmod_u64(p1 % p2, p2 - 2u, p2);
                uint64_t dr = (r2 >= r1) ? r2 - r1 : p2 - ((r1 - r2) % p2);
                uint64_t k_val = mulmod_u64(dr % p2, p1_inv, p2);
                uint64_t b0 = r1 + p1 * k_val;

                int64_t b_signed;
                if (b0 > a / 2u) b_signed = (int64_t)b0 - (int64_t)a;
                else b_signed = (int64_t)b0;

                uint64_t b_abs = (b_signed >= 0) ? (uint64_t)b_signed : (uint64_t)(-b_signed);
                U128 b_sq = u128_mul64(b_abs, b_abs);
                if (u128_mod_u64(b_sq, a) != target % a) {
                    b_signed = -b_signed;
                    b_abs = (b_signed >= 0) ? (uint64_t)b_signed : (uint64_t)(-b_signed);
                    b_sq = u128_mul64(b_abs, b_abs);
                    if (u128_mod_u64(b_sq, a) != target % a) continue;
                }

                int64_t c_signed;
                U128 target_128 = u128_from_u64(target);
                if (u128_gt(b_sq, target_128) || u128_eq(b_sq, target_128)) {
                    U128 num = u128_sub(b_sq, target_128);
                    if (u128_mod_u64(num, a) != 0u) continue;
                    U128 c_128 = u128_div(num, u128_from_u64(a));
                    if (!u128_fits_u64(c_128)) continue;
                    c_signed = (int64_t)c_128.lo;
                } else {
                    U128 num = u128_sub(target_128, b_sq);
                    if (u128_mod_u64(num, a) != 0u) continue;
                    U128 c_128 = u128_div(num, u128_from_u64(a));
                    if (!u128_fits_u64(c_128)) continue;
                    c_signed = -(int64_t)c_128.lo;
                }

                /* Assign SIQS family ID for this a-value */
                int this_family = next_siqs_family;
                if (this_family < QS_MAX_FAMILIES) ++next_siqs_family;

                /* In diversity mode, skip this poly entirely if family already at cap */
                if (div_cap > 0 && this_family < QS_MAX_FAMILIES && family_rows[this_family] >= div_cap)
                    { ++siqs_polys_tried; continue; }

                int added = qs_sieve_siqs_poly(target, a, b_signed, c_signed,
                    p1, p2, i, j, fb, fb_sqrt, fb_count,
                    sieve_half, threshold, matrix, row_words, rel_x, rel_Qx,
                    rel_lp, lp_bound, lp_hash_vals, &lp_col_count,
                    rel_count, max_rels, &fr, t0);
                if (added < 0) { QS_FREE_ALL(); return fr; }
                QS_STAMP_FAMILY(added, this_family);
                ++siqs_polys_tried;
                if (added > 0) ++siqs_a_count;
                siqs_rels += added;
                rel_count += added;
            }
        }
    }

    /* Post-process LP columns: remove singleton LPs (appear only once)
     * because they can never contribute to a dependency.
     * Rebuild LP column assignments for LPs with count >= 2. */
    {
        /* Count occurrences of each LP column */
        int lp_occ[QS_MAX_LP_COLS];
        memset(lp_occ, 0, sizeof(lp_occ));
        for (int ri = 0; ri < rel_count; ++ri) {
            if (rel_lp[ri] == 0u) continue;
            for (int li = 0; li < lp_col_count; ++li) {
                if (lp_hash_vals[li] == rel_lp[ri]) { ++lp_occ[li]; break; }
            }
        }
        /* Build new LP column map: only LPs with count >= 2 */
        uint64_t new_lp_vals[QS_MAX_LP_COLS];
        int new_lp_map[QS_MAX_LP_COLS]; /* old index → new index, or -1 */
        int new_lp_count = 0;
        for (int li = 0; li < lp_col_count; ++li) {
            if (lp_occ[li] >= 2) {
                new_lp_map[li] = new_lp_count;
                new_lp_vals[new_lp_count] = lp_hash_vals[li];
                ++new_lp_count;
            } else {
                new_lp_map[li] = -1;
            }
        }
        /* Rebuild matrix rows: clear old LP columns, set new ones.
         * Also discard partial relations whose LP was singleton. */
        int new_row_words = (fb_count + new_lp_count + 63) / 64;
        int write = 0;
        for (int ri = 0; ri < rel_count; ++ri) {
            if (rel_lp[ri] != 0u) {
                /* Find old LP column index */
                int old_col = -1;
                for (int li = 0; li < lp_col_count; ++li) {
                    if (lp_hash_vals[li] == rel_lp[ri]) { old_col = li; break; }
                }
                if (old_col < 0 || new_lp_map[old_col] < 0) continue; /* singleton LP — discard */
                /* Clear old LP bit, set new LP bit */
                int old_idx = fb_count + old_col;
                matrix[ri * row_words + old_idx / 64] &= ~(1ull << (old_idx % 64));
                int new_idx = fb_count + new_lp_map[old_col];
                matrix[ri * row_words + new_idx / 64] |= (1ull << (new_idx % 64));
            }
            /* Compact: copy row ri to position write */
            if (write != ri) {
                for (int w = 0; w < row_words; ++w) matrix[write * row_words + w] = matrix[ri * row_words + w];
                rel_x[write] = rel_x[ri];
                rel_Qx[write] = rel_Qx[ri];
                rel_lp[write] = rel_lp[ri];
                rel_family[write] = rel_family[ri];
            }
            ++write;
        }
        rel_count = write;
        lp_col_count = new_lp_count;
        memcpy(lp_hash_vals, new_lp_vals, (size_t)new_lp_count * sizeof(uint64_t));
        row_words = new_row_words;
    }

    #undef QS_STAMP_FAMILY

    int active_cols = fb_count + lp_col_count;
    int full_rels = 0, partial_rels = 0;
    for (int ri = 0; ri < rel_count; ++ri) {
        if (rel_lp[ri] == 0u) ++full_rels; else ++partial_rels;
    }

    if (cfg->diag) {
        /* Final-matrix provenance: count rows by family in the actual kept matrix */
        int mat_sp = 0, mat_siqs = 0, mat_distinct_a = 0;
        int mat_fam_counts[QS_MAX_FAMILIES];
        memset(mat_fam_counts, 0, sizeof(mat_fam_counts));
        for (int ri = 0; ri < rel_count; ++ri) {
            int f = rel_family[ri];
            if (f == 0) ++mat_sp;
            else ++mat_siqs;
            if (f >= 0 && f < QS_MAX_FAMILIES) ++mat_fam_counts[f];
        }
        for (int f = 1; f < QS_MAX_FAMILIES; ++f) {
            if (mat_fam_counts[f] > 0) ++mat_distinct_a;
        }
        printf("  [QS diag] relation collection:\n");
        printf("    fb_count=%d (requested %d)  lp_bound=%"PRIu64"\n", fb_count, fb_size, lp_bound);
        printf("    max_rels=%d  rels_kept=%d (full=%d partial=%d)\n",
            max_rels, rel_count, full_rels, partial_rels);
        printf("    lp_cols=%d  active_cols=%d  surplus=%d\n",
            lp_col_count, active_cols, rel_count - active_cols);
        if (cfg->diverse) printf("    diversity: cap=%d\n", div_cap);
        printf("  [QS diag] final matrix provenance:\n");
        printf("    sp_rows=%d  siqs_rows=%d  distinct_a_in_matrix=%d\n",
            mat_sp, mat_siqs, mat_distinct_a);
        /* Show top families */
        int max_fam_rows = 0, max_fam_id = -1;
        for (int f = 0; f < QS_MAX_FAMILIES; ++f) {
            if (mat_fam_counts[f] > max_fam_rows) { max_fam_rows = mat_fam_counts[f]; max_fam_id = f; }
        }
        if (max_fam_id >= 0) {
            printf("    dominant_family=%s%d (%d rows, %.0f%%)\n",
                max_fam_id == 0 ? "sp" : "siqs_a", max_fam_id,
                max_fam_rows, 100.0 * max_fam_rows / (rel_count > 0 ? rel_count : 1));
        }
        /* Check for duplicate parity rows (cheap: compare consecutive sorted-ish rows) */
        int dup_count = 0;
        for (int ri = 1; ri < rel_count; ++ri) {
            bool same = true;
            for (int w = 0; w < row_words; ++w) {
                if (matrix[ri * row_words + w] != matrix[(ri - 1) * row_words + w]) { same = false; break; }
            }
            if (same) ++dup_count;
        }
        if (dup_count > 0) printf("    adjacent_duplicate_rows=%d\n", dup_count);
    }

    if (rel_count <= active_cols) {
        if (cfg->diag) printf("  [QS diag] FAIL: relation-starved (rels %d <= active_cols %d)\n", rel_count, active_cols);
        QS_FREE_ALL();
        fr.status = (int)CPSS_INCOMPLETE; fr.time_seconds = now_sec() - t0; return fr;
    }

    /* Gaussian elimination over GF(2) — covers FB columns + LP virtual columns */
    int* pivot_row = (int*)xmalloc((size_t)active_cols * sizeof(int));
    for (int i = 0; i < active_cols; ++i) pivot_row[i] = -1;
    int hist_words = (rel_count + 63) / 64;
    uint64_t* history = (uint64_t*)xmalloc((size_t)rel_count * (size_t)hist_words * sizeof(uint64_t));
    memset(history, 0, (size_t)rel_count * (size_t)hist_words * sizeof(uint64_t));
    for (int i = 0; i < rel_count; ++i)
        history[i * hist_words + i / 64] = (1ull << (i % 64));

    bool* qs_row_is_pivot = (bool*)xcalloc((size_t)rel_count, sizeof(bool));
    for (int col = 0; col < active_cols; ++col) {
        int piv = -1;
        for (int ri = 0; ri < rel_count; ++ri) {
            if (qs_row_is_pivot[ri]) continue;
            if (matrix[ri * row_words + col / 64] & (1ull << (col % 64))) {
                piv = ri; pivot_row[col] = ri; qs_row_is_pivot[ri] = true; break;
            }
        }
        if (piv < 0) continue;
        for (int ri = 0; ri < rel_count; ++ri) {
            if (ri == piv) continue;
            if (!(matrix[ri * row_words + col / 64] & (1ull << (col % 64)))) continue;
            for (int w = 0; w < row_words; ++w) matrix[ri * row_words + w] ^= matrix[piv * row_words + w];
            for (int w = 0; w < hist_words; ++w) history[ri * hist_words + w] ^= history[piv * hist_words + w];
        }
    }
    free(qs_row_is_pivot);

    /* Compute rank = number of pivot columns found */
    int matrix_rank = 0;
    for (int col = 0; col < active_cols; ++col) { if (pivot_row[col] >= 0) ++matrix_rank; }

    /* Collect nullspace basis vectors (zero rows after elimination) */
    int ns_idx[256]; /* indices of nullspace basis vectors */
    int ns_count = 0;
    for (int ri = 0; ri < rel_count && ns_count < 256; ++ri) {
        bool is_zero = true;
        for (int w = 0; w < row_words; ++w) {
            if (matrix[ri * row_words + w] != 0u) { is_zero = false; break; }
        }
        if (is_zero) ns_idx[ns_count++] = ri;
    }

    if (cfg->diag) {
        printf("  [QS diag] matrix / extraction:\n");
        printf("    rank=%d/%d  nullspace=%d\n", matrix_rank, active_cols, ns_count);
    }

    int diag_phase1 = 0, diag_phase2 = 0, diag_phase3 = 0;

    /* Helper: attempt factor extraction from a combined history vector.
     * combined_hist is the XOR of selected basis vectors' history rows. */
    bool found_factor = false;

    /* Try a dependency defined by combined_hist.
     * For y computation: trial-factor each rel_Qx over FB for exponents,
     * then count LP appearances and include lp^(count/2) in y. */
    #define QS_TRY_DEPENDENCY(combined_hist) do { \
        uint64_t x_acc = 1u, y2_e[QS_MAX_FB]; \
        memset(y2_e, 0, sizeof(uint64_t) * (size_t)fb_count); \
        /* Count LP appearances: use a small hash */ \
        uint64_t _lp_vals[64]; uint32_t _lp_cnts[64]; int _lp_n = 0; \
        memset(_lp_cnts, 0, sizeof(_lp_cnts)); \
        for (int rj = 0; rj < rel_count; ++rj) { \
            if (!((combined_hist)[rj / 64] & (1ull << (rj % 64)))) continue; \
            x_acc = mulmod_u64(x_acc, rel_x[rj], target); \
            uint64_t _rem = rel_Qx[rj]; \
            for (int fi = 0; fi < fb_count; ++fi) { \
                while (_rem % fb[fi] == 0u) { _rem /= fb[fi]; ++y2_e[fi]; } \
            } \
            if (rel_lp[rj] > 0u) { \
                int _found = -1; \
                for (int _li = 0; _li < _lp_n; ++_li) { \
                    if (_lp_vals[_li] == rel_lp[rj]) { _found = _li; break; } \
                } \
                if (_found >= 0) { ++_lp_cnts[_found]; } \
                else if (_lp_n < 64) { _lp_vals[_lp_n] = rel_lp[rj]; _lp_cnts[_lp_n] = 1; ++_lp_n; } \
            } \
        } \
        uint64_t y_acc = 1u; \
        for (int fi = 0; fi < fb_count; ++fi) { \
            if (y2_e[fi] == 0u) continue; \
            uint64_t _h = y2_e[fi] / 2u; \
            if (_h > 0u) y_acc = mulmod_u64(y_acc, powmod_u64(fb[fi], _h, target), target); \
        } \
        /* Include lp^(count/2) for each LP in the dependency */ \
        for (int _li = 0; _li < _lp_n; ++_li) { \
            uint32_t _h = _lp_cnts[_li] / 2u; \
            if (_h > 0u) y_acc = mulmod_u64(y_acc, powmod_u64(_lp_vals[_li], _h, target), target); \
        } \
        uint64_t _d = (x_acc >= y_acc) ? x_acc - y_acc : target - (y_acc - x_acc); \
        uint64_t _s = x_acc + y_acc; if (_s >= target) _s -= target; \
        uint64_t _g1 = gcd_u64(_d, target), _g2 = gcd_u64(_s, target); \
        if (_g1 > 1u && _g1 < target) { factor_result_set_split(&fr, _g1, target / _g1); found_factor = true; } \
        else if (_g2 > 1u && _g2 < target) { factor_result_set_split(&fr, _g2, target / _g2); found_factor = true; } \
    } while(0)

    /* Phase 1: try each individual basis vector */
    for (int ni = 0; ni < ns_count && !found_factor; ++ni) {
        uint64_t* h = &history[ns_idx[ni] * hist_words];
        ++diag_phase1;
        QS_TRY_DEPENDENCY(h);
    }

    /* Phase 2: try pairwise XOR combinations */
    if (!found_factor && ns_count >= 2) {
        uint64_t combined[16]; /* enough for hist_words up to 16 */
        for (int ni = 0; ni < ns_count - 1 && !found_factor; ++ni) {
            for (int nj = ni + 1; nj < ns_count && !found_factor; ++nj) {
                for (int w = 0; w < hist_words && w < 16; ++w)
                    combined[w] = history[ns_idx[ni] * hist_words + w]
                                ^ history[ns_idx[nj] * hist_words + w];
                ++diag_phase2;
                QS_TRY_DEPENDENCY(combined);
            }
        }
    }

    /* Phase 3: try random XOR combinations of 3-6 basis vectors */
    if (!found_factor && ns_count >= 3) {
        uint64_t combined[16];
        uint64_t rng = target ^ 0xDEADBEEFCAFE1234ULL;
        int max_attempts = (ns_count < 20) ? ns_count * ns_count : 400;
        for (int attempt = 0; attempt < max_attempts && !found_factor; ++attempt) {
            int combo_size = 3 + (int)(factor_xorshift64(&rng) % 4u); /* 3-6 */
            if (combo_size > ns_count) combo_size = ns_count;
            memset(combined, 0, (size_t)hist_words * sizeof(uint64_t));
            for (int ci = 0; ci < combo_size; ++ci) {
                int pick = (int)(factor_xorshift64(&rng) % (uint64_t)ns_count);
                for (int w = 0; w < hist_words && w < 16; ++w)
                    combined[w] ^= history[ns_idx[pick] * hist_words + w];
            }
            /* Check combined is not all-zero (degenerate) */
            bool nonzero = false;
            for (int w = 0; w < hist_words; ++w) { if (combined[w]) { nonzero = true; break; } }
            if (!nonzero) continue;
            ++diag_phase3;
            QS_TRY_DEPENDENCY(combined);
        }
    }

    #undef QS_TRY_DEPENDENCY

    if (cfg->diag) {
        printf("  [QS diag] extraction: phase1=%d phase2=%d phase3=%d total=%d  result=%s\n",
            diag_phase1, diag_phase2, diag_phase3, diag_phase1 + diag_phase2 + diag_phase3,
            found_factor ? "FACTOR_FOUND" : (ns_count == 0 ? "ZERO_NULLSPACE" : "ALL_TRIVIAL_GCD"));
    }

    QS_FREE_ALL();
    free(pivot_row); free(history);
    #undef QS_FREE_ALL
    if (!found_factor) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/** Default-config wrapper: reproduces current behavior for auto-router and simple CLI. */
static FactorResult cpss_factor_qs_u64(uint64_t N) {
    unsigned bits = 0u;
    { uint64_t tmp = N; while (tmp > 0u) { ++bits; tmp >>= 1u; } }
    QSConfig cfg;
    qs_config_default_for_bits(bits, &cfg);
    return cpss_factor_qs_u64_ex(N, &cfg);
}

/** BigNum QS: bounded research-attempt mode.
 *  Single-poly Q(x) = (x+s)^2 - N with LP variation.
 *  Accepts inputs up to 1024 bits. Expected to return INCOMPLETE on hard large inputs.
 *  Reuses: small-prime FB, log-sieve, parity matrix, Gaussian elimination, extraction.
 *  Uses BigNum for: N, s, x-values, Q(x) evaluation, and congruence-of-squares extraction. */
static FactorResultBigNum cpss_factor_qs_bn(const BigNum* n, const QSConfig* cfg) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "qs-bn";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();

    if (bn_is_zero(n) || bn_is_one(n)) { fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr; }
    if (bn_is_even(n)) {
        BigNum two; bn_from_u64(&two, 2u);
        factor_result_bn_set_from_factor(&fr, n, &two);
        fr.time_seconds = now_sec() - t0; return fr;
    }
    if (bn_factor_is_prime(n)) {
        fr.cofactor_is_prime = true; fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0; return fr;
    }

    unsigned bits = bn_bitlen(n);

    /* Bounded attempt limits — cap FB and sieve for safety on large inputs */
    int fb_size = cfg->fb_size;
    if (fb_size > QS_MAX_FB) fb_size = QS_MAX_FB;
    int sieve_half = cfg->sieve_half;
    if (sieve_half > QS_MAX_SIEVE) sieve_half = QS_MAX_SIEVE;
    int max_polys = cfg->max_polys;
    if (max_polys > QS_MAX_POLYS) max_polys = QS_MAX_POLYS;

    if (cfg->diag) {
        printf("  [QS-BN diag] BigNum QS attempt: bits=%u\n", bits);
        printf("    fb_size=%d  sieve_half=%d  max_polys=%d  lp_mult=%d\n",
            fb_size, sieve_half, max_polys, cfg->lp_mult);
    }

    /* Build factor base: primes where N is QR mod p */
    uint64_t* fb = (uint64_t*)xmalloc((size_t)fb_size * sizeof(uint64_t));
    uint64_t* fb_sqrt = (uint64_t*)xmalloc((size_t)fb_size * sizeof(uint64_t));
    int fb_count = 0;
    fb[0] = 2u; fb_sqrt[0] = bn_mod_u64(n, 2u); fb_count = 1;
    for (uint64_t p = 3u; fb_count < fb_size && p < 200000u; p += 2u) {
        if (!miller_rabin_u64(p)) continue;
        uint64_t n_mod_p = bn_mod_u64(n, p);
        uint64_t r = qs_modsqrt(n_mod_p, p);
        if (r == 0u && n_mod_p != 0u) continue;
        fb[fb_count] = p; fb_sqrt[fb_count] = r; ++fb_count;
    }
    if (fb_count < 5) {
        free(fb); free(fb_sqrt);
        fr.status = (int)CPSS_INCOMPLETE; fr.time_seconds = now_sec() - t0; return fr;
    }

    uint64_t lp_bound = (cfg->lp_mult > 0) ? fb[fb_count - 1] * (uint64_t)cfg->lp_mult : 0u;
    uint64_t lp_hash_vals[QS_MAX_LP_COLS];
    int lp_col_count = 0;
    memset(lp_hash_vals, 0, sizeof(lp_hash_vals));

    int total_cols = fb_count + QS_MAX_LP_COLS;
    int row_words = (total_cols + 63) / 64;
    int max_rels = (cfg->max_rels > 0) ? cfg->max_rels : (fb_count + 256);
    if (max_rels > QS_MAX_RELS) max_rels = QS_MAX_RELS;

    uint64_t* matrix = (uint64_t*)xmalloc((size_t)max_rels * (size_t)row_words * sizeof(uint64_t));
    memset(matrix, 0, (size_t)max_rels * (size_t)row_words * sizeof(uint64_t));
    BigNum* bn_rel_x = (BigNum*)xmalloc((size_t)max_rels * sizeof(BigNum));
    uint64_t* rel_lp = (uint64_t*)xcalloc((size_t)max_rels, sizeof(uint64_t));
    int rel_count = 0;

    /* s = ceil(sqrt(N)) */
    BigNum s;
    bn_isqrt_floor(&s, n);
    { BigNum s2; bn_mul(&s2, &s, &s);
      if (bn_cmp(&s2, n) < 0) { BigNum one; bn_from_u64(&one, 1u); bn_add(&s, &one); } }

    double threshold = log2((double)(lp_bound > 0u ? lp_bound : fb[fb_count - 1])) + 1.0;
    if (threshold < 10.0) threshold = 10.0;

    #define QS_BN_FREE() do { free(fb); free(fb_sqrt); free(matrix); free(bn_rel_x); free(rel_lp); } while(0)

    /* Multi-polynomial sieve: single-poly Q(x) = (x+s+k*M)^2 - N */
    int total_sieve = 2 * sieve_half + 1;
    double* sieve_log = (double*)xmalloc((size_t)total_sieve * sizeof(double));

    for (int poly = 0; poly < max_polys && rel_count < max_rels; ++poly) {
        /* Center for this polynomial: s + poly * sieve_half */
        BigNum center;
        bn_copy(&center, &s);
        { BigNum offset; bn_from_u64(&offset, (uint64_t)poly * (uint64_t)sieve_half);
          bn_add(&center, &offset); }

        /* Initialize sieve: log2(|Q(x)|) ≈ log2((center + x)^2 - N)
         * Use double approximation for sieve values */
        double center_d = bn_to_double(&center);
        double N_d = bn_to_double(n);
        for (int xi = 0; xi < total_sieve; ++xi) {
            double xd = (double)(xi - sieve_half);
            double cxd = center_d + xd;
            double Qd = cxd * cxd - N_d;
            if (Qd <= 0.0) { sieve_log[xi] = 99.0; continue; }
            sieve_log[xi] = log2(Qd);
        }

        /* Sieve: subtract log(p) at roots */
        for (int fi = 0; fi < fb_count; ++fi) {
            uint64_t p = fb[fi];
            double logp = log2((double)p);
            if (p == 2u) {
                for (int xi = 0; xi < total_sieve; xi += 2) sieve_log[xi] -= logp;
                continue;
            }
            uint64_t c_mod = bn_mod_u64(&center, p);
            uint64_t root1 = (fb_sqrt[fi] + p - c_mod) % p;
            uint64_t root2 = (p - fb_sqrt[fi] + p - c_mod) % p;
            for (int64_t xi = (int64_t)root1; xi < total_sieve; xi += (int64_t)p)
                if (xi >= 0) sieve_log[xi] -= logp;
            if (root2 != root1)
                for (int64_t xi = (int64_t)root2; xi < total_sieve; xi += (int64_t)p)
                    if (xi >= 0) sieve_log[xi] -= logp;
        }

        /* Collect relations */
        for (int xi = 0; xi < total_sieve && rel_count < max_rels; ++xi) {
            if (sieve_log[xi] > threshold) continue;

            /* Compute exact Q(x) = (center + x)^2 - N via BigNum */
            BigNum cx; bn_copy(&cx, &center);
            int64_t x = (int64_t)xi - sieve_half;
            if (x >= 0) { BigNum xbn; bn_from_u64(&xbn, (uint64_t)x); bn_add(&cx, &xbn); }
            else { BigNum xbn; bn_from_u64(&xbn, (uint64_t)(-x)); bn_sub(&cx, &xbn); }

            BigNum cx2; bn_mul(&cx2, &cx, &cx);
            if (bn_cmp(&cx2, n) <= 0) continue;
            BigNum Qx; bn_copy(&Qx, &cx2); bn_sub(&Qx, n);
            if (bn_is_zero(&Qx)) {
                /* Direct factor: cx divides N */
                BigNum g; bn_gcd(&g, &cx, n);
                if (bn_factor_candidate_valid(&g, n)) {
                    factor_result_bn_set_from_factor(&fr, n, &g);
                    free(sieve_log); QS_BN_FREE();
                    fr.time_seconds = now_sec() - t0; return fr;
                }
                continue;
            }

            /* Trial-factor Q(x) over FB — Q(x) can be large but FB primes are small */
            BigNum rem; bn_copy(&rem, &Qx);
            uint64_t* row = &matrix[rel_count * row_words];
            memset(row, 0, (size_t)row_words * sizeof(uint64_t));
            for (int fi = 0; fi < fb_count; ++fi) {
                uint64_t p = fb[fi];
                int exp = 0;
                while (bn_mod_u64(&rem, p) == 0u) {
                    BigNum div_p; bn_from_u64(&div_p, p);
                    BigNum q_out; bn_copy(&q_out, &rem);
                    /* rem = rem / p (exact) */
                    bn_divexact(&q_out, &rem, &div_p);
                    bn_copy(&rem, &q_out);
                    ++exp;
                }
                if (exp & 1) row[fi / 64] ^= (1ull << (fi % 64));
            }

            bool is_smooth = bn_is_one(&rem);
            bool is_partial = false;
            uint64_t lp_val = 0u;
            if (!is_smooth && lp_bound > 0u && bn_fits_u64(&rem)) {
                uint64_t rem64 = bn_to_u64(&rem);
                if (rem64 <= lp_bound && rem64 > 1u && miller_rabin_u64(rem64)) {
                    is_partial = true;
                    lp_val = rem64;
                }
            }
            if (!is_smooth && !is_partial) continue;

            if (is_partial) {
                int lp_col = -1;
                for (int li = 0; li < lp_col_count; ++li) {
                    if (lp_hash_vals[li] == lp_val) { lp_col = li; break; }
                }
                if (lp_col < 0 && lp_col_count < QS_MAX_LP_COLS) {
                    lp_col = lp_col_count++;
                    lp_hash_vals[lp_col] = lp_val;
                }
                if (lp_col < 0) continue;
                int col_idx = fb_count + lp_col;
                row[col_idx / 64] ^= (1ull << (col_idx % 64));
            }

            bn_copy(&bn_rel_x[rel_count], &cx);
            rel_lp[rel_count] = lp_val;
            ++rel_count;
        }
    }

    free(sieve_log);

    /* LP singleton filtering (same logic as uint64 QS) */
    {
        int lp_occ[QS_MAX_LP_COLS]; memset(lp_occ, 0, sizeof(lp_occ));
        for (int ri = 0; ri < rel_count; ++ri) {
            if (rel_lp[ri] == 0u) continue;
            for (int li = 0; li < lp_col_count; ++li)
                if (lp_hash_vals[li] == rel_lp[ri]) { ++lp_occ[li]; break; }
        }
        uint64_t new_lp_vals[QS_MAX_LP_COLS];
        int new_lp_map[QS_MAX_LP_COLS], new_lp_count = 0;
        for (int li = 0; li < lp_col_count; ++li) {
            if (lp_occ[li] >= 2) { new_lp_map[li] = new_lp_count; new_lp_vals[new_lp_count++] = lp_hash_vals[li]; }
            else new_lp_map[li] = -1;
        }
        int write = 0;
        for (int ri = 0; ri < rel_count; ++ri) {
            if (rel_lp[ri] != 0u) {
                int old_col = -1;
                for (int li = 0; li < lp_col_count; ++li)
                    if (lp_hash_vals[li] == rel_lp[ri]) { old_col = li; break; }
                if (old_col < 0 || new_lp_map[old_col] < 0) continue;
                int old_idx = fb_count + old_col;
                matrix[ri * row_words + old_idx / 64] &= ~(1ull << (old_idx % 64));
                int new_idx = fb_count + new_lp_map[old_col];
                matrix[ri * row_words + new_idx / 64] |= (1ull << (new_idx % 64));
            }
            if (write != ri) {
                for (int w = 0; w < row_words; ++w) matrix[write * row_words + w] = matrix[ri * row_words + w];
                bn_copy(&bn_rel_x[write], &bn_rel_x[ri]);
                rel_lp[write] = rel_lp[ri];
            }
            ++write;
        }
        rel_count = write;
        lp_col_count = new_lp_count;
    }

    int active_cols = fb_count + lp_col_count;
    int full_rels = 0, partial_rels = 0;
    for (int ri = 0; ri < rel_count; ++ri) { if (rel_lp[ri] == 0u) ++full_rels; else ++partial_rels; }

    if (cfg->diag) {
        printf("  [QS-BN diag] rels=%d (full=%d partial=%d) lp_cols=%d active=%d surplus=%d\n",
            rel_count, full_rels, partial_rels, lp_col_count, active_cols, rel_count - active_cols);
    }

    if (rel_count <= active_cols) {
        if (cfg->diag) printf("  [QS-BN diag] FAIL: relation-starved (%d <= %d)\n", rel_count, active_cols);
        QS_BN_FREE(); fr.status = (int)CPSS_INCOMPLETE; fr.time_seconds = now_sec() - t0; return fr;
    }

    /* Gaussian elimination — identical to uint64 QS (matrix is mod-2, no BigNum needed) */
    int* pivot_row = (int*)xmalloc((size_t)active_cols * sizeof(int));
    for (int i = 0; i < active_cols; ++i) pivot_row[i] = -1;
    int hist_words = (rel_count + 63) / 64;
    uint64_t* history = (uint64_t*)xmalloc((size_t)rel_count * (size_t)hist_words * sizeof(uint64_t));
    memset(history, 0, (size_t)rel_count * (size_t)hist_words * sizeof(uint64_t));
    for (int i = 0; i < rel_count; ++i)
        history[i * hist_words + i / 64] = (1ull << (i % 64));

    for (int col = 0; col < active_cols; ++col) {
        int piv = -1;
        for (int ri = 0; ri < rel_count; ++ri) {
            if (pivot_row[col] >= 0) break;
            if (matrix[ri * row_words + col / 64] & (1ull << (col % 64))) { piv = ri; pivot_row[col] = ri; break; }
        }
        if (piv < 0) continue;
        for (int ri = 0; ri < rel_count; ++ri) {
            if (ri == piv) continue;
            if (!(matrix[ri * row_words + col / 64] & (1ull << (col % 64)))) continue;
            for (int w = 0; w < row_words; ++w) matrix[ri * row_words + w] ^= matrix[piv * row_words + w];
            for (int w = 0; w < hist_words; ++w) history[ri * hist_words + w] ^= history[piv * hist_words + w];
        }
    }

    /* Collect nullspace basis vectors */
    int ns_idx[256]; int ns_count = 0;
    for (int ri = 0; ri < rel_count && ns_count < 256; ++ri) {
        bool is_zero = true;
        for (int w = 0; w < row_words; ++w) { if (matrix[ri * row_words + w]) { is_zero = false; break; } }
        if (is_zero) ns_idx[ns_count++] = ri;
    }

    if (cfg->diag) {
        int rank = 0; for (int c = 0; c < active_cols; ++c) if (pivot_row[c] >= 0) ++rank;
        printf("  [QS-BN diag] rank=%d/%d nullspace=%d\n", rank, active_cols, ns_count);
    }

    /* Extraction: for each nullspace vector, compute x = product of rel_x mod N,
     * y = sqrt of product of Q(x) mod N (via FB exponents + LP), check gcd. */
    bool found_factor = false;
    for (int ni = 0; ni < ns_count && !found_factor; ++ni) {
        uint64_t* h = &history[ns_idx[ni] * hist_words];
        BigNum x_acc; bn_from_u64(&x_acc, 1u);
        uint64_t y2_exp[QS_MAX_FB]; memset(y2_exp, 0, sizeof(uint64_t) * (size_t)fb_count);
        uint64_t lp_counts[64]; uint64_t lp_vals_seen[64]; int lp_n = 0;
        memset(lp_counts, 0, sizeof(lp_counts));

        for (int rj = 0; rj < rel_count; ++rj) {
            if (!(h[rj / 64] & (1ull << (rj % 64)))) continue;
            bn_mod_mul(&x_acc, &x_acc, &bn_rel_x[rj], n);
            /* Reconstruct Q(x) = rel_x^2 - N and factor over FB */
            BigNum rx2; bn_mod_mul(&rx2, &bn_rel_x[rj], &bn_rel_x[rj], n);
            /* For y exponents, we trial-factor the actual Q(x) but track via stored info.
             * Since Q(x) was already trial-factored during collection, re-derive from rel_x. */
            BigNum Qx_full; bn_mul(&Qx_full, &bn_rel_x[rj], &bn_rel_x[rj]); bn_sub(&Qx_full, n);
            BigNum rem_q; bn_copy(&rem_q, &Qx_full);
            for (int fi = 0; fi < fb_count; ++fi) {
                while (bn_mod_u64(&rem_q, fb[fi]) == 0u) {
                    BigNum dp; bn_from_u64(&dp, fb[fi]);
                    BigNum qo; bn_divexact(&qo, &rem_q, &dp);
                    bn_copy(&rem_q, &qo);
                    ++y2_exp[fi];
                }
            }
            if (rel_lp[rj] > 0u) {
                int found_lp = -1;
                for (int li = 0; li < lp_n; ++li) if (lp_vals_seen[li] == rel_lp[rj]) { found_lp = li; break; }
                if (found_lp >= 0) ++lp_counts[found_lp];
                else if (lp_n < 64) { lp_vals_seen[lp_n] = rel_lp[rj]; lp_counts[lp_n] = 1; ++lp_n; }
            }
        }

        BigNum y_acc; bn_from_u64(&y_acc, 1u);
        for (int fi = 0; fi < fb_count; ++fi) {
            uint64_t half = y2_exp[fi] / 2u;
            if (half > 0u) {
                BigNum base; bn_from_u64(&base, fb[fi]);
                BigNum pw; bn_from_u64(&pw, 1u);
                for (uint64_t e = 0; e < half; ++e) bn_mod_mul(&pw, &pw, &base, n);
                bn_mod_mul(&y_acc, &y_acc, &pw, n);
            }
        }
        for (int li = 0; li < lp_n; ++li) {
            uint64_t half = lp_counts[li] / 2u;
            if (half > 0u) {
                BigNum base; bn_from_u64(&base, lp_vals_seen[li]);
                BigNum pw; bn_from_u64(&pw, 1u);
                for (uint64_t e = 0; e < half; ++e) bn_mod_mul(&pw, &pw, &base, n);
                bn_mod_mul(&y_acc, &y_acc, &pw, n);
            }
        }

        /* gcd(x - y, N) and gcd(x + y, N) */
        BigNum diff, sum_val;
        if (bn_cmp(&x_acc, &y_acc) >= 0) { bn_copy(&diff, &x_acc); bn_sub(&diff, &y_acc); }
        else { bn_copy(&diff, &x_acc); bn_add(&diff, n); bn_sub(&diff, &y_acc); }
        bn_copy(&sum_val, &x_acc); bn_add(&sum_val, &y_acc);
        if (bn_cmp(&sum_val, n) >= 0) bn_sub(&sum_val, n);

        BigNum g1, g2;
        bn_gcd(&g1, &diff, n);
        bn_gcd(&g2, &sum_val, n);
        if (bn_factor_candidate_valid(&g1, n)) {
            fr.method = "qs-bn"; factor_result_bn_set_from_factor(&fr, n, &g1);
            found_factor = true;
        } else if (bn_factor_candidate_valid(&g2, n)) {
            fr.method = "qs-bn"; factor_result_bn_set_from_factor(&fr, n, &g2);
            found_factor = true;
        }
    }

    if (cfg->diag) {
        printf("  [QS-BN diag] extraction: tried=%d result=%s\n", ns_count,
            found_factor ? "FACTOR_FOUND" : (ns_count == 0 ? "ZERO_NULLSPACE" : "ALL_TRIVIAL_GCD"));
    }

    QS_BN_FREE(); free(pivot_row); free(history);
    #undef QS_BN_FREE
    if (!found_factor) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

static FactorResultU128 cpss_factor_qs_u128(U128 N) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, N, "qs");
    double t0 = now_sec();
    if (u128_fits_u64(N)) {
        FactorResult fr64 = cpss_factor_qs_u64(N.lo);
        fr.n = N;
        for (size_t i = 0u; i < fr64.factor_count; ++i) {
            fr.factors[i] = fr64.factors[i]; fr.exponents[i] = fr64.exponents[i];
        }
        fr.factor_count = fr64.factor_count;
        fr.cofactor = u128_from_u64(fr64.cofactor);
        fr.fully_factored = fr64.fully_factored;
        fr.status = fr64.status; fr.time_seconds = fr64.time_seconds;
        return fr;
    }
    /* True U128: promote to BigNum and use BigNum QS attempt path */
    BigNum bn;
    bn_from_u64(&bn, N.lo);
    if (N.hi != 0u) {
        BigNum hi; bn_from_u64(&hi, N.hi);
        for (int sh = 0; sh < 64; ++sh) bn_shl1(&hi);
        bn_add(&bn, &hi);
    }
    unsigned bits = bn_bitlen(&bn);
    QSConfig cfg;
    qs_config_default_for_bits(bits, &cfg);
    FactorResultBigNum frbn = cpss_factor_qs_bn(&bn, &cfg);
    fr.method = frbn.method;
    fr.status = frbn.status;
    fr.time_seconds = frbn.time_seconds;
    fr.fully_factored = frbn.fully_factored;
    if (frbn.factor_found && bn_fits_u64(&frbn.factor)) {
        uint64_t f = bn_to_u64(&frbn.factor);
        if (f > 1u) {
            factor_result_u128_add(&fr, f);
            fr.cofactor = u128_div(N, u128_from_u64(f));
            if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                factor_result_u128_add(&fr, fr.cofactor.lo);
                fr.cofactor = u128_from_u64(1u);
                fr.fully_factored = true;
            }
        }
    }
    return fr;
}
