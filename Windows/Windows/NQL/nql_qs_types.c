/**
 * nql_qs_types.c - Native QS runtime objects for NQL.
 * Part of the CPSS Viewer amalgamation.
 *
 * Implements first-class opaque QS objects: QsFactorBase, QsSieveBuffer,
 * QsTrialResult, QsRelationSet, QsGf2Matrix, QsDependency.
 * All hot-path computation is native C. NQL is the orchestration layer.
 *
 * Internal arithmetic uses U128 where overflow is possible (see plan SA).
 * All stored values are uint64_t. No NqlValue boxing in hot paths.
 */

/* ======================================================================
 * QsFactorBase -- factor base for QS
 * ====================================================================== */

/** Ownership tag: prevents use-after-move for QS opaque handles.
 *  Set to QS_OWNED on creation. Set to 0 on move (variable overwrite).
 *  All built-in functions check this tag before accessing the object. */
#define QS_OWNED 0x51534F4Bu /* 'QSOK' */

typedef struct {
    uint32_t  owner_tag;      /* QS_OWNED if live, 0 if moved */
    uint64_t  N;              /* target being factored */
    uint64_t* primes;         /* heap, length = count */
    uint64_t* sqrts;          /* heap, length = count: sqrt(N) mod p */
    double*   logp;           /* heap, length = count: log2(p) */
    int       count;          /* number of FB primes */
    uint64_t  lp_bound;       /* large prime bound = largest * lp_mult */
    int       lp_mult;        /* large prime multiplier */
} QsFactorBase;

static void qs_fb_free(QsFactorBase* fb) {
    if (!fb) return;
    free(fb->primes); free(fb->sqrts); free(fb->logp);
    free(fb);
}

/** Tonelli-Shanks modular square root for FB construction. */
static uint64_t qs_fb_modsqrt(uint64_t n, uint64_t p) {
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

/** TRIAL_SMALL_FACTOR(N, bound) -- check if any prime p <= bound divides N.
 *  Returns the factor as int, or null if no small factor found.
 *  This is a separate precheck so FACTOR_BASE_BUILD never encounters p|N. */
static NqlValue nql_fn_trial_small_factor(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    uint64_t N = (uint64_t)nql_val_as_int(&args[0]);
    int bound = (int)nql_val_as_int(&args[1]);
    if (N <= 1u) return nql_val_null();
    if ((N & 1u) == 0u) return nql_val_int(2);
    for (uint64_t p = 3u; p <= (uint64_t)bound; p += 2u) {
        if (!miller_rabin_u64(p)) continue;
        if (N % p == 0u) return nql_val_int((int64_t)p);
    }
    return nql_val_null();
}

/** FACTOR_BASE_BUILD(N, bound, lp_mult).
 *  PRECONDITION: caller has already run TRIAL_SMALL_FACTOR and confirmed no small factor.
 *  Returns QsFactorBase or null on allocation failure / invalid input. */
static NqlValue nql_fn_factor_base_build(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    uint64_t N = (uint64_t)nql_val_as_int(&args[0]);
    int bound = (int)nql_val_as_int(&args[1]);
    int lp_mult = (argc >= 3u) ? (int)nql_val_as_int(&args[2]) : 64;

    if (N <= 1u || bound < 3) return nql_val_null();

    int max_fb = bound < 10000 ? bound : 10000;
    QsFactorBase* fb = (QsFactorBase*)malloc(sizeof(QsFactorBase));
    if (!fb) return nql_val_null();
    memset(fb, 0, sizeof(*fb));
    fb->owner_tag = QS_OWNED;
    fb->N = N;
    fb->primes = (uint64_t*)malloc((size_t)max_fb * sizeof(uint64_t));
    fb->sqrts  = (uint64_t*)malloc((size_t)max_fb * sizeof(uint64_t));
    fb->logp   = (double*)malloc((size_t)max_fb * sizeof(double));
    fb->count = 0;
    fb->lp_mult = lp_mult;
    if (!fb->primes || !fb->sqrts || !fb->logp) { qs_fb_free(fb); return nql_val_null(); }

    /* p=2: always included. Single hit class in sieve.
     * Sieve spec for p=2: stride=2, start offset = ((center - half) & 1).
     * Only one root class because x^2-N mod 2 has exactly one residue class. */
    fb->primes[0] = 2u;
    fb->sqrts[0] = N & 1u; /* not a real sqrt, just a parity marker */
    fb->logp[0] = 1.0;
    fb->count = 1;

    for (uint64_t p = 3u; p <= (uint64_t)bound && fb->count < max_fb; p += 2u) {
        if (!miller_rabin_u64(p)) continue;
        /* Skip p|N -- caller should have caught this via TRIAL_SMALL_FACTOR */
        if (N % p == 0u) continue;
        uint64_t r = qs_fb_modsqrt(N % p, p);
        if (r == 0u) continue;
        fb->primes[fb->count] = p;
        fb->sqrts[fb->count] = r;
        fb->logp[fb->count] = log2((double)p);
        ++fb->count;
    }

    fb->lp_bound = (fb->count > 0 && lp_mult > 0)
        ? fb->primes[fb->count - 1] * (uint64_t)lp_mult : 0u;

    NqlValue result;
    memset(&result, 0, sizeof(result));
    result.type = NQL_VAL_QS_FACTOR_BASE;
    result.v.qs_ptr = fb;
    return result;
}

static NqlValue nql_fn_factor_base_size(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_FACTOR_BASE || !args[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(((QsFactorBase*)args[0].v.qs_ptr)->count);
}

static NqlValue nql_fn_factor_base_prime(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_FACTOR_BASE || !args[0].v.qs_ptr) return nql_val_null();
    QsFactorBase* fb = (QsFactorBase*)args[0].v.qs_ptr;
    int i = (int)nql_val_as_int(&args[1]);
    if (i < 0 || i >= fb->count) return nql_val_null();
    return nql_val_int((int64_t)fb->primes[i]);
}

static NqlValue nql_fn_factor_base_largest(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_FACTOR_BASE || !args[0].v.qs_ptr) return nql_val_int(0);
    QsFactorBase* fb = (QsFactorBase*)args[0].v.qs_ptr;
    return (fb->count > 0) ? nql_val_int((int64_t)fb->primes[fb->count - 1]) : nql_val_int(0);
}

static NqlValue nql_fn_factor_base_lp_bound(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_FACTOR_BASE || !args[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int((int64_t)((QsFactorBase*)args[0].v.qs_ptr)->lp_bound);
}

/* ======================================================================
 * QsSieveBuffer -- log-approximation sieve
 * ====================================================================== */

typedef struct {
    double*  log_vals;        /* heap, length = total_size */
    int      half_width;      /* M: sieve covers [-M, M] */
    int      total_size;      /* 2*M+1 */
    uint64_t center;          /* current polynomial center */
    uint64_t N;               /* target */
} QsSieveBuffer;

static void qs_sieve_free(QsSieveBuffer* s) {
    if (!s) return;
    free(s->log_vals);
    free(s);
}

/** SIEVE_CREATE(half_width) */
static NqlValue nql_fn_qs_sieve_create(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    int half = (int)nql_val_as_int(&args[0]);
    if (half <= 0 || half > 131072) return nql_val_null();
    QsSieveBuffer* s = (QsSieveBuffer*)malloc(sizeof(QsSieveBuffer));
    if (!s) return nql_val_null();
    s->half_width = half;
    s->total_size = 2 * half + 1;
    s->log_vals = (double*)malloc((size_t)s->total_size * sizeof(double));
    if (!s->log_vals) { free(s); return nql_val_null(); }
    s->center = 0u;
    s->N = 0u;
    NqlValue r; memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_QS_SIEVE; r.v.qs_ptr = s;
    return r;
}

/** SIEVE_INIT_SINGLE_POLY(sieve, N, center) -- fill with log2(Q(x)), sign-aware.
 *  Q(x) = (x+center)^2 - N. Uses U128 for (x+center)^2. */
static NqlValue nql_fn_qs_sieve_init_sp(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_SIEVE || !args[0].v.qs_ptr) return nql_val_null();
    QsSieveBuffer* s = (QsSieveBuffer*)args[0].v.qs_ptr;
    uint64_t N = (uint64_t)nql_val_as_int(&args[1]);
    uint64_t center = (uint64_t)nql_val_as_int(&args[2]);
    s->N = N;
    s->center = center;
    int half = s->half_width;

    for (int xi = 0; xi < s->total_size; ++xi) {
        int64_t x = (int64_t)xi - half;
        int64_t xc = x + (int64_t)center;
        if (xc <= 0) { s->log_vals[xi] = 99.0; continue; }
        /* Use U128 for (xc)^2 to avoid overflow */
        U128 xc_sq = u128_mul64((uint64_t)xc, (uint64_t)xc);
        U128 N128 = u128_from_u64(N);
        if (u128_lt(xc_sq, N128)) {
            /* Q(x) is negative -- store log2(|Q(x)|) */
            U128 Qx_abs = u128_sub(N128, xc_sq);
            if (u128_is_zero(Qx_abs)) { s->log_vals[xi] = 99.0; continue; }
            s->log_vals[xi] = u128_fits_u64(Qx_abs) ? log2((double)Qx_abs.lo)
                : log2((double)Qx_abs.hi * 18446744073709551616.0 + (double)Qx_abs.lo);
        } else {
            U128 Qx = u128_sub(xc_sq, N128);
            if (u128_is_zero(Qx)) { s->log_vals[xi] = 99.0; continue; }
            s->log_vals[xi] = u128_fits_u64(Qx) ? log2((double)Qx.lo)
                : log2((double)Qx.hi * 18446744073709551616.0 + (double)Qx.lo);
        }
    }
    return args[0]; /* return same handle (mutated in place) */
}

/** SIEVE_RUN(sieve, fb, center) -- subtract log(p) at roots. The hot inner loop. */
static NqlValue nql_fn_qs_sieve_run(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_SIEVE || !args[0].v.qs_ptr) return nql_val_null();
    if (args[1].type != NQL_VAL_QS_FACTOR_BASE || !args[1].v.qs_ptr) return nql_val_null();
    QsSieveBuffer* s = (QsSieveBuffer*)args[0].v.qs_ptr;
    QsFactorBase* fb = (QsFactorBase*)args[1].v.qs_ptr;
    uint64_t center = (uint64_t)nql_val_as_int(&args[2]);
    int total = s->total_size;

    for (int fi = 0; fi < fb->count; ++fi) {
        uint64_t p = fb->primes[fi];
        double lp = fb->logp[fi];

        if (p == 2u) {
            /* p=2 exact spec: Q(x) = (xc)^2 - N where xc = xi - half + center.
             * N is odd (precondition). Q(x) is even iff xc is odd (odd^2 - odd = even).
             * xc = xi - half + center is odd when xi has parity = (half - center) & 1.
             * Single hit class, stride 2. */
            int start = (int)((uint64_t)(s->half_width) - center) & 1;
            /* Clamp start to [0, 1] */
            if (start < 0) start += 2;
            for (int xi = start; xi < total; xi += 2) s->log_vals[xi] -= lp;
            continue;
        }

        /* Two roots: center + sqrt === 0 mod p and center - sqrt === 0 mod p */
        uint64_t c_mod = center % p;
        uint64_t root1 = (fb->sqrts[fi] + p - c_mod) % p;
        uint64_t root2 = (p - fb->sqrts[fi] + p - c_mod) % p;

        for (int64_t xi = (int64_t)root1; xi < total; xi += (int64_t)p)
            if (xi >= 0) s->log_vals[xi] -= lp;
        if (root2 != root1)
            for (int64_t xi = (int64_t)root2; xi < total; xi += (int64_t)p)
                if (xi >= 0) s->log_vals[xi] -= lp;
    }
    return args[0]; /* mutated in place */
}

/** SIEVE_CANDIDATES(sieve, threshold) -- return NQL array of candidate indices. */
static NqlValue nql_fn_qs_sieve_candidates(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_SIEVE || !args[0].v.qs_ptr) return nql_val_null();
    QsSieveBuffer* s = (QsSieveBuffer*)args[0].v.qs_ptr;
    double threshold = nql_val_as_float(&args[1]);

    NqlArray* cands = nql_array_alloc(256u);
    if (!cands) return nql_val_null();
    for (int xi = 0; xi < s->total_size; ++xi) {
        if (s->log_vals[xi] <= threshold)
            nql_array_push(cands, nql_val_int((int64_t)xi));
    }
    return nql_val_array(cands);
}

/** QS_CANDIDATE_QX(sieve, xi) -- compute Q(x) = (xi - half + center)^2 - N using U128.
 *  Returns the absolute value of Q(x) as int64 (for values that fit). */
static NqlValue nql_fn_qs_candidate_qx(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_QS_SIEVE || !args[0].v.qs_ptr) return nql_val_null();
    QsSieveBuffer* s = (QsSieveBuffer*)args[0].v.qs_ptr;
    int64_t xi = nql_val_as_int(&args[1]);
    int64_t x = xi - s->half_width;
    int64_t xc = x + (int64_t)s->center;
    if (xc <= 0) return nql_val_null();
    U128 xc_sq = u128_mul64((uint64_t)xc, (uint64_t)xc);
    U128 N128 = u128_from_u64(s->N);
    if (u128_lt(xc_sq, N128)) {
        U128 Qx = u128_sub(N128, xc_sq);
        return u128_fits_u64(Qx) ? nql_val_int((int64_t)Qx.lo) : nql_val_from_u128(Qx);
    }
    U128 Qx = u128_sub(xc_sq, N128);
    if (u128_is_zero(Qx)) return nql_val_int(0);
    return u128_fits_u64(Qx) ? nql_val_int((int64_t)Qx.lo) : nql_val_from_u128(Qx);
}

/** QS_CANDIDATE_XC(sieve, xi) -- compute xc = xi - half + center. */
static NqlValue nql_fn_qs_candidate_xc(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_QS_SIEVE || !args[0].v.qs_ptr) return nql_val_null();
    QsSieveBuffer* s = (QsSieveBuffer*)args[0].v.qs_ptr;
    int64_t xi = nql_val_as_int(&args[1]);
    int64_t xc = xi - s->half_width + (int64_t)s->center;
    return nql_val_int(xc);
}

/* ======================================================================
 * QsTrialResult -- trial factoring result for a single candidate
 * ====================================================================== */

/** QsTrialResult -- trial factoring result for a single candidate.
 *  NOTE: Standalone heap allocation is the correctness/testing path.
 *  Phase 2b's SIEVE_SCAN_AND_FACTOR eliminates per-candidate allocation. */
typedef struct {
    uint64_t  x;              /* LHS value */
    U128      Qx_stored;     /* RHS value for extraction -- U128 because SIQS a*|Q(x)| can overflow u64 */
    uint64_t  cofactor;       /* remainder after FB division */
    uint32_t* exponents;      /* heap, length = fb_count */
    bool      sign_negative;  /* true if Q(x) < 0 */
    bool      is_smooth;
    bool      is_partial;
    int       fb_count;
} QsTrialResult;

static void qs_trial_free(QsTrialResult* tr) {
    if (!tr) return;
    free(tr->exponents);
    free(tr);
}

/** TRIAL_FACTOR_CANDIDATE(Qx_abs, fb, x_val, sign_negative)
 *  Trial-divide |Qx| over FB. Returns QsTrialResult with full exponents. */
static NqlValue nql_fn_qs_trial_factor(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    uint64_t Qx_abs = (uint64_t)nql_val_as_int(&args[0]);
    if (args[1].type != NQL_VAL_QS_FACTOR_BASE || !args[1].v.qs_ptr) return nql_val_null();
    QsFactorBase* fb = (QsFactorBase*)args[1].v.qs_ptr;
    uint64_t x_val = (uint64_t)nql_val_as_int(&args[2]);
    bool sign = (argc >= 4u) ? nql_val_is_truthy(&args[3]) : false;

    if (Qx_abs == 0u) return nql_val_null();

    QsTrialResult* tr = (QsTrialResult*)malloc(sizeof(QsTrialResult));
    if (!tr) return nql_val_null();
    tr->fb_count = fb->count;
    tr->exponents = (uint32_t*)calloc((size_t)fb->count, sizeof(uint32_t));
    if (!tr->exponents) { free(tr); return nql_val_null(); }
    tr->x = x_val;
    tr->sign_negative = sign;

    uint64_t rem = Qx_abs;
    for (int fi = 0; fi < fb->count; ++fi) {
        uint64_t p = fb->primes[fi];
        while (p > 0u && rem % p == 0u) {
            rem /= p;
            ++tr->exponents[fi];
        }
    }

    tr->cofactor = rem;
    tr->Qx_stored = u128_from_u64(Qx_abs); /* caller overrides for SIQS: u128_mul64(a, Qx_abs) */
    tr->is_smooth = (rem == 1u);
    tr->is_partial = (!tr->is_smooth && rem <= fb->lp_bound && rem > 1u && miller_rabin_u64(rem));

    NqlValue r; memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_QS_TRIAL_RESULT; r.v.qs_ptr = tr;
    return r;
}

static NqlValue nql_fn_qs_trial_is_smooth(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_TRIAL_RESULT || !args[0].v.qs_ptr) return nql_val_bool(false);
    return nql_val_bool(((QsTrialResult*)args[0].v.qs_ptr)->is_smooth);
}

static NqlValue nql_fn_qs_trial_is_partial(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_TRIAL_RESULT || !args[0].v.qs_ptr) return nql_val_bool(false);
    return nql_val_bool(((QsTrialResult*)args[0].v.qs_ptr)->is_partial);
}

static NqlValue nql_fn_qs_trial_cofactor(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_TRIAL_RESULT || !args[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int((int64_t)((QsTrialResult*)args[0].v.qs_ptr)->cofactor);
}

/* ======================================================================
 * QsRelationSet -- packed relation store with LP virtual columns
 * ====================================================================== */

typedef struct {
    int       fb_count;
    int       max_rels;
    int       count;

    uint64_t* x;              /* LHS x-values */
    U128*     Qx;             /* RHS stored values -- U128 for SIQS a*|Q(x)| overflow safety */
    uint64_t* lp;             /* large prime per relation (0 = smooth) */
    bool*     sign;           /* sign_negative flags */
    uint32_t* exponents;      /* flat: exponents[rel * fb_count + fi] */

    int       total_cols;     /* sign_col(1) + fb_count + lp_col_count */
    int       row_words;      /* ceil(total_cols / 64) */
    uint64_t* parity;         /* flat: parity[rel * row_words + w] */

    int       max_lp_cols;
    int       lp_col_count;
    uint64_t* lp_col_to_prime;
} QsRelationSet;

static void qs_relset_free(QsRelationSet* rs) {
    if (!rs) return;
    free(rs->x); free(rs->Qx); free(rs->lp); free(rs->sign);
    free(rs->exponents); free(rs->parity); free(rs->lp_col_to_prime);
    free(rs);
}

/** RELATION_SET_CREATE(fb_count, max_rels, max_lp_cols) */
static NqlValue nql_fn_qs_relset_create(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    int fb_count = (int)nql_val_as_int(&args[0]);
    int max_rels = (argc >= 2u) ? (int)nql_val_as_int(&args[1]) : (fb_count + 320);
    int max_lp = (argc >= 3u) ? (int)nql_val_as_int(&args[2]) : 256;
    if (fb_count <= 0 || max_rels <= 0) return nql_val_null();

    QsRelationSet* rs = (QsRelationSet*)calloc(1, sizeof(QsRelationSet));
    if (!rs) return nql_val_null();
    rs->fb_count = fb_count;
    rs->max_rels = max_rels;
    rs->max_lp_cols = max_lp;
    rs->count = 0;
    rs->lp_col_count = 0;

    /* Pre-allocate max_cols for parity with room for LP growth */
    int max_cols = 1 + fb_count + max_lp; /* sign + FB + LP */
    rs->total_cols = 1 + fb_count; /* initially just sign + FB */
    rs->row_words = (max_cols + 63) / 64;

    rs->x = (uint64_t*)calloc((size_t)max_rels, sizeof(uint64_t));
    rs->Qx = (U128*)calloc((size_t)max_rels, sizeof(U128));
    rs->lp = (uint64_t*)calloc((size_t)max_rels, sizeof(uint64_t));
    rs->sign = (bool*)calloc((size_t)max_rels, sizeof(bool));
    rs->exponents = (uint32_t*)calloc((size_t)max_rels * (size_t)fb_count, sizeof(uint32_t));
    rs->parity = (uint64_t*)calloc((size_t)max_rels * (size_t)rs->row_words, sizeof(uint64_t));
    rs->lp_col_to_prime = (uint64_t*)calloc((size_t)max_lp, sizeof(uint64_t));

    if (!rs->x || !rs->Qx || !rs->lp || !rs->sign || !rs->exponents || !rs->parity || !rs->lp_col_to_prime) {
        qs_relset_free(rs); return nql_val_null();
    }

    NqlValue r; memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_QS_RELATION_SET; r.v.qs_ptr = rs;
    return r;
}

/** RELATION_SET_ADD(rels, trial_result) -- add a relation. Mutates in place. */
static NqlValue nql_fn_qs_relset_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_RELATION_SET || !args[0].v.qs_ptr) return nql_val_null();
    if (args[1].type != NQL_VAL_QS_TRIAL_RESULT || !args[1].v.qs_ptr) return args[0];
    QsRelationSet* rs = (QsRelationSet*)args[0].v.qs_ptr;
    QsTrialResult* tr = (QsTrialResult*)args[1].v.qs_ptr;

    if (!tr->is_smooth && !tr->is_partial) return args[0]; /* discard */
    if (rs->count >= rs->max_rels) return args[0]; /* full */

    int ri = rs->count;

    /* Copy relation data */
    rs->x[ri] = tr->x;
    rs->Qx[ri] = tr->Qx_stored; /* U128 */
    rs->sign[ri] = tr->sign_negative;
    memcpy(&rs->exponents[ri * rs->fb_count], tr->exponents, (size_t)rs->fb_count * sizeof(uint32_t));

    /* Build parity row */
    uint64_t* row = &rs->parity[ri * rs->row_words];
    memset(row, 0, (size_t)rs->row_words * sizeof(uint64_t));

    /* Column 0: sign */
    if (tr->sign_negative) row[0] |= 1ull;

    /* Columns 1..fb_count: exponent parities */
    for (int fi = 0; fi < rs->fb_count; ++fi) {
        if (tr->exponents[fi] & 1u) {
            int col = 1 + fi;
            row[col / 64] |= (1ull << (col % 64));
        }
    }

    /* LP handling */
    if (tr->is_partial) {
        rs->lp[ri] = tr->cofactor;
        /* Find or assign LP column */
        int lp_col = -1;
        for (int li = 0; li < rs->lp_col_count; ++li) {
            if (rs->lp_col_to_prime[li] == tr->cofactor) { lp_col = li; break; }
        }
        if (lp_col < 0 && rs->lp_col_count < rs->max_lp_cols) {
            lp_col = rs->lp_col_count++;
            rs->lp_col_to_prime[lp_col] = tr->cofactor;
            rs->total_cols = 1 + rs->fb_count + rs->lp_col_count;
        }
        if (lp_col >= 0) {
            int col = 1 + rs->fb_count + lp_col;
            row[col / 64] |= (1ull << (col % 64));
        } else {
            return args[0]; /* LP table full, discard */
        }
    } else {
        rs->lp[ri] = 0u;
    }

    ++rs->count;
    return args[0]; /* mutated in place */
}

static NqlValue nql_fn_qs_relset_count(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_RELATION_SET || !args[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(((QsRelationSet*)args[0].v.qs_ptr)->count);
}

/** RELATION_SET_PRUNE_SINGLETONS -- remove partials whose LP appears only once. Mutates. */
static NqlValue nql_fn_qs_relset_prune(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_RELATION_SET || !args[0].v.qs_ptr) return nql_val_null();
    QsRelationSet* rs = (QsRelationSet*)args[0].v.qs_ptr;

    /* Count occurrences per LP column */
    int* occ = (int*)calloc((size_t)rs->lp_col_count, sizeof(int));
    if (!occ) return args[0];
    for (int ri = 0; ri < rs->count; ++ri) {
        if (rs->lp[ri] == 0u) continue;
        for (int li = 0; li < rs->lp_col_count; ++li) {
            if (rs->lp_col_to_prime[li] == rs->lp[ri]) { ++occ[li]; break; }
        }
    }

    /* Build new LP map: only LPs with count >= 2 */
    uint64_t* new_lp_primes = (uint64_t*)calloc((size_t)rs->max_lp_cols, sizeof(uint64_t));
    int* old_to_new = (int*)malloc((size_t)rs->lp_col_count * sizeof(int));
    int new_lp_count = 0;
    for (int li = 0; li < rs->lp_col_count; ++li) {
        if (occ[li] >= 2) {
            old_to_new[li] = new_lp_count;
            new_lp_primes[new_lp_count] = rs->lp_col_to_prime[li];
            ++new_lp_count;
        } else {
            old_to_new[li] = -1;
        }
    }

    /* Compact relations: remove singleton partials, rebuild parity rows */
    int write = 0;
    for (int ri = 0; ri < rs->count; ++ri) {
        if (rs->lp[ri] != 0u) {
            /* Find old LP column */
            int old_col = -1;
            for (int li = 0; li < rs->lp_col_count; ++li) {
                if (rs->lp_col_to_prime[li] == rs->lp[ri]) { old_col = li; break; }
            }
            if (old_col < 0 || old_to_new[old_col] < 0) continue; /* singleton -- discard */
        }

        if (write != ri) {
            rs->x[write] = rs->x[ri];
            rs->Qx[write] = rs->Qx[ri];
            rs->lp[write] = rs->lp[ri];
            rs->sign[write] = rs->sign[ri];
            memcpy(&rs->exponents[write * rs->fb_count], &rs->exponents[ri * rs->fb_count],
                   (size_t)rs->fb_count * sizeof(uint32_t));
        }

        /* Rebuild parity row for this relation */
        uint64_t* row = &rs->parity[write * rs->row_words];
        memset(row, 0, (size_t)rs->row_words * sizeof(uint64_t));
        if (rs->sign[write]) row[0] |= 1ull;
        for (int fi = 0; fi < rs->fb_count; ++fi) {
            if (rs->exponents[write * rs->fb_count + fi] & 1u) {
                int col = 1 + fi;
                row[col / 64] |= (1ull << (col % 64));
            }
        }
        if (rs->lp[write] != 0u) {
            int old_col = -1;
            for (int li = 0; li < rs->lp_col_count; ++li) {
                if (rs->lp_col_to_prime[li] == rs->lp[write]) { old_col = li; break; }
            }
            if (old_col >= 0 && old_to_new[old_col] >= 0) {
                int col = 1 + rs->fb_count + old_to_new[old_col];
                row[col / 64] |= (1ull << (col % 64));
            }
        }
        ++write;
    }
    rs->count = write;
    rs->lp_col_count = new_lp_count;
    memcpy(rs->lp_col_to_prime, new_lp_primes, (size_t)new_lp_count * sizeof(uint64_t));
    rs->total_cols = 1 + rs->fb_count + new_lp_count;

    free(occ); free(new_lp_primes); free(old_to_new);
    return args[0]; /* mutated */
}

/* ======================================================================
 * QsGf2Matrix -- packed GF(2) parity matrix with history tracking
 * ====================================================================== */

typedef struct {
    uint64_t* data;           /* row-major parity matrix (heap) */
    uint64_t* history;        /* identity -> dependency tracking (heap) */
    int       rows;
    int       cols;
    int       row_words;
    int       hist_words;
    int*      pivot_col;      /* pivot_col[r] = column for pivot row r, or -1 */
    bool      eliminated;
} QsGf2Matrix;

static void qs_gf2_free(QsGf2Matrix* m) {
    if (!m) return;
    free(m->data); free(m->history); free(m->pivot_col);
    free(m);
}

/** GF2_MATRIX_BUILD(rels) -- build packed GF(2) matrix from RelationSet. */
static NqlValue nql_fn_qs_gf2_build(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_RELATION_SET || !args[0].v.qs_ptr) return nql_val_null();
    QsRelationSet* rs = (QsRelationSet*)args[0].v.qs_ptr;
    if (rs->count == 0) return nql_val_null();

    QsGf2Matrix* m = (QsGf2Matrix*)calloc(1, sizeof(QsGf2Matrix));
    if (!m) return nql_val_null();
    m->rows = rs->count;
    m->cols = rs->total_cols;
    m->row_words = (m->cols + 63) / 64;
    m->hist_words = (m->rows + 63) / 64;
    m->eliminated = false;

    m->data = (uint64_t*)calloc((size_t)m->rows * (size_t)m->row_words, sizeof(uint64_t));
    m->history = (uint64_t*)calloc((size_t)m->rows * (size_t)m->hist_words, sizeof(uint64_t));
    m->pivot_col = (int*)malloc((size_t)m->rows * sizeof(int));
    if (!m->data || !m->history || !m->pivot_col) { qs_gf2_free(m); return nql_val_null(); }

    /* Copy parity rows from RelationSet */
    for (int ri = 0; ri < m->rows; ++ri) {
        /* Copy the parity data (may be wider than m->row_words if rs->row_words was over-allocated) */
        int copy_words = m->row_words < rs->row_words ? m->row_words : rs->row_words;
        memcpy(&m->data[ri * m->row_words], &rs->parity[ri * rs->row_words],
               (size_t)copy_words * sizeof(uint64_t));
        /* Init history as identity */
        m->history[ri * m->hist_words + ri / 64] = (1ull << (ri % 64));
        m->pivot_col[ri] = -1;
    }

    NqlValue r; memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_QS_GF2_MATRIX; r.v.qs_ptr = m;
    return r;
}

/** GF2_ELIMINATE(matrix) -- Gaussian elimination in-place with history. */
static NqlValue nql_fn_qs_gf2_eliminate(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_GF2_MATRIX || !args[0].v.qs_ptr) return nql_val_null();
    QsGf2Matrix* m = (QsGf2Matrix*)args[0].v.qs_ptr;
    if (m->eliminated) return args[0];

    bool* is_pivot = (bool*)calloc((size_t)m->rows, sizeof(bool));
    if (!is_pivot) return args[0];

    for (int col = 0; col < m->cols; ++col) {
        int piv = -1;
        int w = col / 64, b = col % 64;
        for (int ri = 0; ri < m->rows; ++ri) {
            if (is_pivot[ri]) continue;
            if (m->data[ri * m->row_words + w] & (1ull << b)) {
                piv = ri; m->pivot_col[ri] = col; is_pivot[ri] = true; break;
            }
        }
        if (piv < 0) continue;
        /* XOR pivot row into all other rows with this column set */
        for (int ri = 0; ri < m->rows; ++ri) {
            if (ri == piv) continue;
            if (!(m->data[ri * m->row_words + w] & (1ull << b))) continue;
            for (int ww = 0; ww < m->row_words; ++ww)
                m->data[ri * m->row_words + ww] ^= m->data[piv * m->row_words + ww];
            for (int ww = 0; ww < m->hist_words; ++ww)
                m->history[ri * m->hist_words + ww] ^= m->history[piv * m->hist_words + ww];
        }
    }

    free(is_pivot);
    m->eliminated = true;
    return args[0]; /* mutated */
}

static NqlValue nql_fn_qs_gf2_nullspace_count(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_GF2_MATRIX || !args[0].v.qs_ptr) return nql_val_int(0);
    QsGf2Matrix* m = (QsGf2Matrix*)args[0].v.qs_ptr;
    int count = 0;
    for (int ri = 0; ri < m->rows; ++ri) {
        bool zero = true;
        for (int w = 0; w < m->row_words; ++w) {
            if (m->data[ri * m->row_words + w]) { zero = false; break; }
        }
        if (zero) ++count;
    }
    return nql_val_int(count);
}

static NqlValue nql_fn_qs_gf2_rank(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_GF2_MATRIX || !args[0].v.qs_ptr) return nql_val_int(0);
    QsGf2Matrix* m = (QsGf2Matrix*)args[0].v.qs_ptr;
    int rank = 0;
    for (int ri = 0; ri < m->rows; ++ri) {
        if (m->pivot_col[ri] >= 0) ++rank;
    }
    return nql_val_int(rank);
}

/* ======================================================================
 * QsDependency -- null-space dependency vector
 * ====================================================================== */

typedef struct {
    uint64_t* hist_vector;    /* bitset: which relations participate */
    int       hist_words;
} QsDependency;

static void qs_dep_free(QsDependency* d) {
    if (!d) return;
    free(d->hist_vector);
    free(d);
}

/** GF2_NULLSPACE_VECTOR(matrix, i) -- extract i-th null-space dependency. */
static NqlValue nql_fn_qs_gf2_nullvec(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_GF2_MATRIX || !args[0].v.qs_ptr) return nql_val_null();
    QsGf2Matrix* m = (QsGf2Matrix*)args[0].v.qs_ptr;
    int target_idx = (int)nql_val_as_int(&args[1]);

    /* Find the target_idx-th zero row */
    int found = 0;
    for (int ri = 0; ri < m->rows; ++ri) {
        bool zero = true;
        for (int w = 0; w < m->row_words; ++w) {
            if (m->data[ri * m->row_words + w]) { zero = false; break; }
        }
        if (!zero) continue;
        if (found == target_idx) {
            QsDependency* d = (QsDependency*)malloc(sizeof(QsDependency));
            if (!d) return nql_val_null();
            d->hist_words = m->hist_words;
            d->hist_vector = (uint64_t*)malloc((size_t)m->hist_words * sizeof(uint64_t));
            if (!d->hist_vector) { free(d); return nql_val_null(); }
            memcpy(d->hist_vector, &m->history[ri * m->hist_words],
                   (size_t)m->hist_words * sizeof(uint64_t));
            NqlValue r; memset(&r, 0, sizeof(r));
            r.type = NQL_VAL_QS_DEPENDENCY; r.v.qs_ptr = d;
            return r;
        }
        ++found;
    }
    return nql_val_null();
}

/** DEPENDENCY_COMBINE(d1, d2) -- XOR two dependencies. */
static NqlValue nql_fn_qs_dep_combine(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_DEPENDENCY || !args[0].v.qs_ptr) return nql_val_null();
    if (args[1].type != NQL_VAL_QS_DEPENDENCY || !args[1].v.qs_ptr) return nql_val_null();
    QsDependency* d1 = (QsDependency*)args[0].v.qs_ptr;
    QsDependency* d2 = (QsDependency*)args[1].v.qs_ptr;
    int hw = d1->hist_words < d2->hist_words ? d1->hist_words : d2->hist_words;

    QsDependency* d = (QsDependency*)malloc(sizeof(QsDependency));
    if (!d) return nql_val_null();
    d->hist_words = hw;
    d->hist_vector = (uint64_t*)malloc((size_t)hw * sizeof(uint64_t));
    if (!d->hist_vector) { free(d); return nql_val_null(); }
    for (int w = 0; w < hw; ++w)
        d->hist_vector[w] = d1->hist_vector[w] ^ d2->hist_vector[w];

    NqlValue r; memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_QS_DEPENDENCY; r.v.qs_ptr = d;
    return r;
}

/** DEPENDENCY_EXTRACT(dep, rels, fb, N) -- compute x^2===y^2 and try GCD.
 *  Uses stored full exponents + lp_col_to_prime for LP reconstruction.
 *  Returns factor as int, or null if trivial. */
static NqlValue nql_fn_qs_dep_extract(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_QS_DEPENDENCY || !args[0].v.qs_ptr) return nql_val_null();
    if (args[1].type != NQL_VAL_QS_RELATION_SET || !args[1].v.qs_ptr) return nql_val_null();
    if (args[2].type != NQL_VAL_QS_FACTOR_BASE || !args[2].v.qs_ptr) return nql_val_null();
    QsDependency* dep = (QsDependency*)args[0].v.qs_ptr;
    QsRelationSet* rs = (QsRelationSet*)args[1].v.qs_ptr;
    QsFactorBase* fb = (QsFactorBase*)args[2].v.qs_ptr;
    uint64_t N = (uint64_t)nql_val_as_int(&args[3]);

    /* Accumulate x product and exponent sums */
    uint64_t x_acc = 1u;
    uint64_t* e_sum = (uint64_t*)calloc((size_t)rs->fb_count, sizeof(uint64_t));
    if (!e_sum) return nql_val_null();

    /* LP counting: small hash */
    uint64_t lp_vals[64]; uint32_t lp_cnts[64]; int lp_n = 0;
    memset(lp_cnts, 0, sizeof(lp_cnts));

    for (int rj = 0; rj < rs->count; ++rj) {
        if (!(dep->hist_vector[rj / 64] & (1ull << (rj % 64)))) continue;
        x_acc = mulmod_u64(x_acc, rs->x[rj], N);
        for (int fi = 0; fi < rs->fb_count; ++fi)
            e_sum[fi] += rs->exponents[rj * rs->fb_count + fi];
        if (rs->lp[rj] > 0u) {
            int found = -1;
            for (int li = 0; li < lp_n; ++li) {
                if (lp_vals[li] == rs->lp[rj]) { found = li; break; }
            }
            if (found >= 0) ++lp_cnts[found];
            else if (lp_n < 64) { lp_vals[lp_n] = rs->lp[rj]; lp_cnts[lp_n] = 1; ++lp_n; }
        }
    }

    /* Compute y = product of fb[i]^(e_sum[i]/2) mod N */
    uint64_t y_acc = 1u;
    for (int fi = 0; fi < rs->fb_count; ++fi) {
        if (e_sum[fi] == 0u) continue;
        uint64_t half = e_sum[fi] / 2u;
        if (half > 0u) y_acc = mulmod_u64(y_acc, powmod_u64(fb->primes[fi], half, N), N);
    }
    /* Include LP contributions */
    for (int li = 0; li < lp_n; ++li) {
        uint32_t half = lp_cnts[li] / 2u;
        if (half > 0u) y_acc = mulmod_u64(y_acc, powmod_u64(lp_vals[li], half, N), N);
    }

    free(e_sum);

    /* Try GCD(x +/- y, N) */
    uint64_t d1 = (x_acc >= y_acc) ? x_acc - y_acc : N - (y_acc - x_acc);
    uint64_t d2 = x_acc + y_acc; if (d2 >= N) d2 -= N;
    uint64_t g1 = gcd_u64(d1, N);
    uint64_t g2 = gcd_u64(d2, N);

    if (g1 > 1u && g1 < N) return nql_val_int((int64_t)g1);
    if (g2 > 1u && g2 < N) return nql_val_int((int64_t)g2);
    return nql_val_null(); /* trivial GCD */
}

/* ======================================================================
 * nql_val_free -- destructor hook for QS opaque objects
 * ====================================================================== */

static void nql_val_free(NqlValue* v) {
    if (!v) return;
    switch (v->type) {
        case NQL_VAL_QS_FACTOR_BASE:  qs_fb_free((QsFactorBase*)v->v.qs_ptr); break;
        case NQL_VAL_QS_SIEVE:        qs_sieve_free((QsSieveBuffer*)v->v.qs_ptr); break;
        case NQL_VAL_QS_TRIAL_RESULT: qs_trial_free((QsTrialResult*)v->v.qs_ptr); break;
        case NQL_VAL_QS_RELATION_SET: qs_relset_free((QsRelationSet*)v->v.qs_ptr); break;
        case NQL_VAL_QS_GF2_MATRIX:   qs_gf2_free((QsGf2Matrix*)v->v.qs_ptr); break;
        case NQL_VAL_QS_DEPENDENCY:    qs_dep_free((QsDependency*)v->v.qs_ptr); break;
        case NQL_VAL_ARRAY:
            if (v->v.array) nql_array_free(v->v.array);
            break;
        case NQL_VAL_POLY:
            if (v->v.qs_ptr) nql_poly_free((NqlPoly*)v->v.qs_ptr);
            break;
        default: break; /* scalar types -- nothing to free */
    }
    v->type = NQL_VAL_NULL;
    v->v.qs_ptr = NULL;
}
