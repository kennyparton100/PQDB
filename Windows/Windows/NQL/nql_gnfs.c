/**
 * nql_gnfs.c - GNFS subsystem native objects for NQL.
 * Part of the CPSS Viewer amalgamation.
 *
 * Implements: polynomial selection, factor base construction (rational + algebraic),
 * norm computation, and GNFS-specific utilities.
 * Later phases add: sieve, relations, sparse LA, algebraic sqrt.
 *
 * Architecture: NQL orchestration + native C hot paths (same as QS).
 */

/* ======================================================================
 * PHASE 1: POLYNOMIAL SELECTION
 * ====================================================================== */

/** GNFS_BASE_M_SELECT(N, degree) -- compute m = floor(N^(1/d)) as BigNum. */
static NqlValue nql_fn_gnfs_base_m(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    BigNum N = nql_val_as_bignum(&args[0]);
    int d = (int)nql_val_as_int(&args[1]);
    if (d <= 0 || bn_is_zero(&N)) return nql_val_null();

    /* Use NTH_ROOT logic: Newton's method for floor(N^(1/d)) */
    unsigned bits = bn_bitlen(&N);
    unsigned start_bits = (bits + (unsigned)d - 1u) / (unsigned)d;
    BigNum x; bn_from_u64(&x, 1u);
    bn_shl(&x, start_bits > 0 ? start_bits : 1);
    BigNum dm1; bn_from_u64(&dm1, (uint64_t)(d - 1));
    BigNum dbn; bn_from_u64(&dbn, (uint64_t)d);

    for (int iter = 0; iter < 500; ++iter) {
        BigNum x_pow; bn_from_u64(&x_pow, 1u);
        for (int j = 0; j < d - 1; ++j) {
            BigNum tmp; bn_mul(&tmp, &x_pow, &x);
            bn_copy(&x_pow, &tmp);
        }
        if (bn_is_zero(&x_pow)) break;
        BigNum q; bn_divexact(&q, &N, &x_pow);
        BigNum dm1x; bn_mul(&dm1x, &dm1, &x);
        bn_add(&dm1x, &q);
        BigNum new_x; bn_divexact(&new_x, &dm1x, &dbn);
        if (bn_cmp(&new_x, &x) >= 0) break;
        bn_copy(&x, &new_x);
    }
    return nql_val_from_bignum(&x);
}

/** GNFS_BASE_M_POLY(N, degree) -- generate base-m polynomial pair.
 *  Returns an array: [f_poly, g_poly, m_bignum]
 *  f(x) = c_d*x^d + c_{d-1}*x^{d-1} + ... + c_0 where N is expressed in base m
 *  g(x) = x - m (the linear rational polynomial)
 *  Property: f(m) === 0 (mod N) and g(m) = 0. */
static NqlValue nql_fn_gnfs_base_m_poly(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    BigNum N = nql_val_as_bignum(&args[0]);
    int d = (int)nql_val_as_int(&args[1]);
    if (d <= 0 || d > 10 || bn_is_zero(&N)) return nql_val_null();

    /* Compute m = floor(N^(1/d)) using uint64 fast path when possible */
    BigNum m;
    if (bn_fits_u64(&N)) {
        uint64_t n64 = bn_to_u64(&N);
        /* Direct computation via floating point + correction */
        double approx = pow((double)n64, 1.0 / (double)d);
        uint64_t m64 = (uint64_t)approx;
        /* Correct: ensure m^d <= N < (m+1)^d */
        while (1) {
            uint64_t pw = 1u;
            bool overflow = false;
            for (int j = 0; j < d; ++j) {
                if (pw > UINT64_MAX / (m64 + 1u)) { overflow = true; break; }
                pw *= (m64 + 1u);
            }
            if (!overflow && pw <= n64) { ++m64; continue; }
            break;
        }
        while (m64 > 0u) {
            uint64_t pw = 1u;
            bool overflow = false;
            for (int j = 0; j < d; ++j) {
                if (pw > UINT64_MAX / m64) { overflow = true; break; }
                pw *= m64;
            }
            if (overflow || pw > n64) { --m64; continue; }
            break;
        }
        bn_from_u64(&m, m64);
    } else {
        /* BigNum Newton: m_{i+1} = ((d-1)*m_i + N/m_i^(d-1)) / d */
        unsigned bits = bn_bitlen(&N);
        unsigned start_bits = (bits + (unsigned)d - 1u) / (unsigned)d;
        bn_from_u64(&m, 1u);
        bn_shl(&m, start_bits > 0 ? start_bits : 1);
        for (int iter = 0; iter < 500; ++iter) {
            BigNum x_pow; bn_from_u64(&x_pow, 1u);
            for (int j = 0; j < d - 1; ++j) {
                BigNum tmp; bn_mul(&tmp, &x_pow, &m);
                bn_copy(&x_pow, &tmp);
            }
            if (bn_is_zero(&x_pow)) break;
            /* q = floor(N / m^(d-1)) -- use divexact as approximation, then adjust */
            BigNum q; bn_divexact(&q, &N, &x_pow);
            BigNum dm1x; bn_from_u64(&dm1x, (uint64_t)(d - 1));
            BigNum dm1m; bn_mul(&dm1m, &dm1x, &m);
            bn_add(&dm1m, &q);
            BigNum dbn; bn_from_u64(&dbn, (uint64_t)d);
            BigNum new_m; bn_divexact(&new_m, &dm1m, &dbn);
            if (bn_cmp(&new_m, &m) >= 0) break;
            bn_copy(&m, &new_m);
        }
    }

    /* Express N in base m: N = c_d * m^d + c_{d-1} * m^{d-1} + ... + c_0 */
    NqlPoly* f = nql_poly_alloc(d);
    if (!f) return nql_val_null();
    BigNum rem; bn_copy(&rem, &N);
    for (int i = 0; i <= d; ++i) {
        if (bn_is_zero(&rem)) { f->coeffs[i] = 0; continue; }
        if (bn_is_zero(&m)) { /* m=0 edge case */
            if (bn_fits_u64(&rem)) f->coeffs[i] = (int64_t)bn_to_u64(&rem);
            break;
        }
        /* rem = q * m + r; coeffs[i] = r */
        uint64_t r_u64 = bn_mod_u64(&rem, bn_fits_u64(&m) ? bn_to_u64(&m) : UINT64_MAX);
        f->coeffs[i] = (int64_t)r_u64;
        /* rem = (rem - r) / m */
        BigNum r_bn; bn_from_u64(&r_bn, r_u64);
        bn_sub(&rem, &r_bn);
        if (!bn_is_zero(&rem) && !bn_is_zero(&m)) {
            BigNum q; bn_divexact(&q, &rem, &m);
            bn_copy(&rem, &q);
        }
    }
    f->degree = d;
    nql_poly_normalize(f);

    /* g(x) = x - m (linear rational polynomial) */
    NqlPoly* g = nql_poly_alloc(1);
    if (!g) { nql_poly_free(f); return nql_val_null(); }
    if (bn_fits_u64(&m)) {
        g->coeffs[0] = -(int64_t)bn_to_u64(&m);
    } else {
        g->coeffs[0] = 0; /* m too large for int64 coeff -- store as 0, use m separately */
    }
    g->coeffs[1] = 1;
    g->degree = 1;

    /* Return [f, g, m] as array */
    NqlArray* result = nql_array_alloc(3u);
    if (!result) { nql_poly_free(f); nql_poly_free(g); return nql_val_null(); }
    nql_array_push(result, nql_val_poly(f));
    nql_array_push(result, nql_val_poly(g));
    nql_array_push(result, nql_val_from_bignum(&m));
    return nql_val_array(result);
}

/** GNFS_POLY_ALPHA(f, bound) -- compute alpha-value of polynomial f.
 *  alpha(f) ~= -sigma_{p prime <= bound} (log(p) * (number of roots of f mod p)) / (p - 1)
 *  Lower alpha = better polynomial. Typical bound: 2000. */
static NqlValue nql_fn_gnfs_poly_alpha(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ctx;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_float(0.0);
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    int bound = (ac >= 2u) ? (int)nql_val_as_int(&args[1]) : 2000;

    double alpha = 0.0;
    for (int p = 2; p <= bound; ++p) {
        if (!miller_rabin_u64((uint64_t)p)) continue;
        /* Count roots of f mod p */
        int root_count = 0;
        for (int x = 0; x < p; ++x) {
            if (nql_poly_eval_mod(f, (uint64_t)x, (uint64_t)p) == 0u) ++root_count;
        }
        /* Contribution: -log(p) * root_count / (p - 1) */
        alpha -= log((double)p) * (double)root_count / (double)(p - 1);
    }
    return nql_val_float(alpha);
}

/** GNFS_POLY_SKEWNESS(f) -- estimate optimal skewness s for polynomial f.
 *  Skewness balances the contribution of high and low degree terms.
 *  Approximate: s ~= |c_0 / c_d|^(1/d) where c_0 = constant, c_d = leading coeff. */
static NqlValue nql_fn_gnfs_poly_skewness(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_float(1.0);
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    if (f->degree <= 0) return nql_val_float(1.0);
    double c0 = fabs((double)f->coeffs[0]);
    double cd = fabs((double)f->coeffs[f->degree]);
    if (cd < 1e-15 || c0 < 1e-15) return nql_val_float(1.0);
    return nql_val_float(pow(c0 / cd, 1.0 / (double)f->degree));
}

/** GNFS_POLY_ROTATE(f, g, j0, j1) -- polynomial rotation: f' = f + j0*g + j1*x*g.
 *  Preserves the property f(m) === 0 mod N since g(m) = 0.
 *  Returns new f polynomial. */
static NqlValue nql_fn_gnfs_poly_rotate(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_POLY || args[1].type != NQL_VAL_POLY) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    NqlPoly* g = (NqlPoly*)args[1].v.qs_ptr;
    int64_t j0 = nql_val_as_int(&args[2]);
    int64_t j1 = (ac >= 4u) ? nql_val_as_int(&args[3]) : 0;

    NqlPoly* result = nql_poly_copy(f);
    if (!result) return nql_val_null();

    /* f' = f + j0 * g */
    if (j0 != 0) {
        for (int i = 0; i <= g->degree && i <= result->degree; ++i)
            result->coeffs[i] += j0 * g->coeffs[i];
        if (g->degree > result->degree) {
            nql_poly_ensure(result, g->degree);
            for (int i = result->degree + 1; i <= g->degree; ++i)
                result->coeffs[i] = j0 * g->coeffs[i];
            result->degree = g->degree;
        }
    }

    /* f' = f' + j1 * x * g (shift g by one degree then add) */
    if (j1 != 0) {
        int shifted_deg = g->degree + 1;
        nql_poly_ensure(result, shifted_deg);
        if (shifted_deg > result->degree) result->degree = shifted_deg;
        for (int i = 0; i <= g->degree; ++i)
            result->coeffs[i + 1] += j1 * g->coeffs[i];
    }

    nql_poly_normalize(result);
    return nql_val_poly(result);
}

/* ======================================================================
 * PHASE 2: ALGEBRAIC FACTOR BASE + NORMS
 * ====================================================================== */

/** GNFS_RATIONAL_NORM(a, b, m) -- compute |a - b*m| as BigNum.
 *  This is the rational side norm for GNFS relation (a, b). */
static NqlValue nql_fn_gnfs_rational_norm(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    BigNum a_bn = nql_val_as_bignum(&args[0]);
    BigNum b_bn = nql_val_as_bignum(&args[1]);
    BigNum m_bn = nql_val_as_bignum(&args[2]);
    BigNum bm; bn_mul(&bm, &b_bn, &m_bn);
    BigNum result;
    if (bn_cmp(&a_bn, &bm) >= 0) {
        bn_copy(&result, &a_bn);
        bn_sub(&result, &bm);
    } else {
        bn_copy(&result, &bm);
        bn_sub(&result, &a_bn);
    }
    return nql_val_from_bignum(&result);
}

/** GNFS_ALGEBRAIC_NORM(a, b, f) -- compute |(-b)^d * f(a/b)| as BigNum.
 *  = |sigma_{i=0}^{d} c_i * a^i * (-b)^{d-i}| where f(x) = sigma c_i x^i.
 *  This is the algebraic side norm for GNFS. Uses BigNum throughout. */
static NqlValue nql_fn_gnfs_algebraic_norm(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t a = nql_val_as_int(&args[0]);
    int64_t b = nql_val_as_int(&args[1]);
    if (args[2].type != NQL_VAL_POLY || !args[2].v.qs_ptr) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[2].v.qs_ptr;
    if (f->degree < 0) return nql_val_int(0);

    int d = f->degree;
    /* Compute sigma c_i * a^i * (-b)^{d-i} using BigNum */
    BigNum sum; bn_zero(&sum);
    BigNum a_pow; bn_from_u64(&a_pow, 1u);

    /* Precompute powers of a and (-b) */
    BigNum a_powers[11]; /* max degree 10 */
    BigNum nb_powers[11];
    bn_from_u64(&a_powers[0], 1u);
    int64_t neg_b = -b;
    if (neg_b >= 0) bn_from_u64(&nb_powers[0], 1u);
    else { bn_from_u64(&nb_powers[0], 1u); }

    BigNum a_bn; bn_from_u64(&a_bn, (uint64_t)(a >= 0 ? a : -a));
    BigNum nb_bn; bn_from_u64(&nb_bn, (uint64_t)(neg_b >= 0 ? neg_b : -neg_b));
    bool a_neg = (a < 0);
    bool nb_neg = (neg_b < 0);

    for (int i = 1; i <= d; ++i) {
        bn_mul(&a_powers[i], &a_powers[i-1], &a_bn);
        bn_mul(&nb_powers[i], &nb_powers[i-1], &nb_bn);
    }

    /* Sum terms: c_i * a^i * (-b)^(d-i) */
    /* Track sign separately */
    BigNum pos_sum; bn_zero(&pos_sum);
    BigNum neg_sum; bn_zero(&neg_sum);

    for (int i = 0; i <= d; ++i) {
        if (f->coeffs[i] == 0) continue;
        BigNum term; bn_mul(&term, &a_powers[i], &nb_powers[d - i]);
        /* Sign: coeff_sign * a_sign^i * nb_sign^(d-i) */
        bool term_neg = (f->coeffs[i] < 0);
        if (a_neg && (i & 1)) term_neg = !term_neg;
        if (nb_neg && ((d - i) & 1)) term_neg = !term_neg;
        /* Scale by |c_i| */
        uint64_t abs_ci = (uint64_t)(f->coeffs[i] >= 0 ? f->coeffs[i] : -f->coeffs[i]);
        if (abs_ci != 1u) {
            BigNum ci_bn; bn_from_u64(&ci_bn, abs_ci);
            BigNum scaled; bn_mul(&scaled, &term, &ci_bn);
            bn_copy(&term, &scaled);
        }
        if (term_neg) bn_add(&neg_sum, &term);
        else bn_add(&pos_sum, &term);
    }

    /* result = |pos_sum - neg_sum| */
    BigNum result;
    if (bn_cmp(&pos_sum, &neg_sum) >= 0) {
        bn_copy(&result, &pos_sum);
        bn_sub(&result, &neg_sum);
    } else {
        bn_copy(&result, &neg_sum);
        bn_sub(&result, &pos_sum);
    }
    return nql_val_from_bignum(&result);
}

/** GNFS_ALGEBRAIC_NORM_MOD(a, b, f, p) -- compute algebraic norm mod p.
 *  Much faster than full BigNum norm when we only need smoothness over small primes.
 *  Uses uint64 modular arithmetic. */
static NqlValue nql_fn_gnfs_algebraic_norm_mod(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t a = nql_val_as_int(&args[0]);
    int64_t b = nql_val_as_int(&args[1]);
    if (args[2].type != NQL_VAL_POLY || !args[2].v.qs_ptr) return nql_val_int(0);
    NqlPoly* f = (NqlPoly*)args[2].v.qs_ptr;
    uint64_t p = (uint64_t)nql_val_as_int(&args[3]);
    if (p == 0u || f->degree < 0) return nql_val_int(0);

    int d = f->degree;
    uint64_t a_mod = ((a % (int64_t)p) + (int64_t)p) % (int64_t)p;
    uint64_t neg_b = ((((-b) % (int64_t)p) + (int64_t)p) % (int64_t)p);
    uint64_t result = 0u;
    uint64_t a_pow = 1u;
    uint64_t nb_pow = 1u;

    /* Precompute (-b)^d mod p, then divide down */
    for (int i = 0; i < d; ++i) nb_pow = mulmod_u64(nb_pow, neg_b, p);
    /* nb_pow = (-b)^d */

    uint64_t nb_inv = (neg_b > 0u) ? powmod_u64(neg_b, p - 2u, p) : 0u;

    for (int i = 0; i <= d; ++i) {
        uint64_t ci = ((f->coeffs[i] % (int64_t)p) + (int64_t)p) % (int64_t)p;
        uint64_t term = mulmod_u64(ci, mulmod_u64(a_pow, nb_pow, p), p);
        result = (result + term) % p;
        a_pow = mulmod_u64(a_pow, a_mod, p);
        if (nb_inv > 0u) nb_pow = mulmod_u64(nb_pow, nb_inv, p);
    }
    return nql_val_int((int64_t)result);
}

/* ======================================================================
 * PHASE 3: 2D SPECIAL-Q LATTICE SIEVE
 * ====================================================================== */

typedef struct {
    int       width, height;
    float*    rat_sieve;       /* heap, width x height */
    float*    alg_sieve;       /* heap, width x height */
    uint64_t  special_q;
    uint64_t  special_r;
    int64_t   e0x, e0y, e1x, e1y; /* lattice basis */
} GnfsSieveRegion;

static void gnfs_sieve_free(GnfsSieveRegion* s) {
    if (!s) return;
    free(s->rat_sieve); free(s->alg_sieve); free(s);
}

/** GNFS_SIEVE_REGION_CREATE(width, height) */
static NqlValue nql_fn_gnfs_sieve_create(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int w = (int)nql_val_as_int(&args[0]);
    int h = (int)nql_val_as_int(&args[1]);
    if (w <= 0 || h <= 0 || (int64_t)w * h > 100000000LL) return nql_val_null();
    GnfsSieveRegion* s = (GnfsSieveRegion*)calloc(1, sizeof(GnfsSieveRegion));
    if (!s) return nql_val_null();
    s->width = w; s->height = h;
    s->rat_sieve = (float*)calloc((size_t)w * (size_t)h, sizeof(float));
    s->alg_sieve = (float*)calloc((size_t)w * (size_t)h, sizeof(float));
    if (!s->rat_sieve || !s->alg_sieve) { gnfs_sieve_free(s); return nql_val_null(); }
    NqlValue r; memset(&r, 0, sizeof(r)); r.type = NQL_VAL_QS_SIEVE; r.v.qs_ptr = s;
    return r;
}

/** GNFS_SIEVE_SET_SPECIAL_Q(region, q, r) -- set special-q and compute lattice basis.
 *  Lattice basis for the sublattice { (a,b) : a === b*r (mod q) }. */
static NqlValue nql_fn_gnfs_sieve_set_q(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsSieveRegion* s = (GnfsSieveRegion*)args[0].v.qs_ptr;
    s->special_q = (uint64_t)nql_val_as_int(&args[1]);
    s->special_r = (uint64_t)nql_val_as_int(&args[2]);
    /* Lattice basis: e0 = (q, 0), e1 = (r, 1). Reduce via Gauss. */
    int64_t a1 = (int64_t)s->special_q, a2 = 0;
    int64_t b1 = (int64_t)s->special_r, b2 = 1;
    /* 2D Gauss reduction */
    for (int iter = 0; iter < 200; ++iter) {
        int64_t na = a1*a1 + a2*a2, nb = b1*b1 + b2*b2;
        if (na > nb) { int64_t t; t=a1;a1=b1;b1=t; t=a2;a2=b2;b2=t; int64_t tn=na;na=nb;nb=tn; }
        int64_t dot = a1*b1 + a2*b2;
        int64_t mu = (na > 0) ? (dot + (dot > 0 ? na/2 : -na/2)) / na : 0;
        b1 -= mu * a1; b2 -= mu * a2;
        int64_t new_nb = b1*b1 + b2*b2;
        if (new_nb >= nb) break;
    }
    s->e0x = a1; s->e0y = a2; s->e1x = b1; s->e1y = b2;
    return args[0];
}

/** GNFS_SIEVE_INIT_NORMS(region, f, g, m) -- fill sieve with log-norm estimates. */
static NqlValue nql_fn_gnfs_sieve_init_norms(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsSieveRegion* s = (GnfsSieveRegion*)args[0].v.qs_ptr;
    if (args[1].type != NQL_VAL_POLY || args[2].type != NQL_VAL_POLY) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[1].v.qs_ptr;
    /* m for rational norm */
    int64_t m_val = nql_val_as_int(&args[3]);
    int half_w = s->width / 2, half_h = s->height / 2;

    for (int bj = 0; bj < s->height; ++bj) {
        for (int ai = 0; ai < s->width; ++ai) {
            int u = ai - half_w, v = bj - half_h;
            int64_t a = (int64_t)s->e0x * u + (int64_t)s->e1x * v;
            int64_t b = (int64_t)s->e0y * u + (int64_t)s->e1y * v;
            if (b == 0 && a == 0) { s->rat_sieve[bj*s->width+ai] = 99.f; s->alg_sieve[bj*s->width+ai] = 99.f; continue; }
            /* Rational: |a - b*m| */
            double rat_norm = fabs((double)a - (double)b * (double)m_val);
            s->rat_sieve[bj * s->width + ai] = (rat_norm > 1.0) ? (float)log2(rat_norm) : 0.f;
            /* Algebraic: |(-b)^d * f(a/b)| = |sigma c_i * a^i * (-b)^(d-i)| */
            {
                double alg_est = 0.0;
                double a_pow = 1.0;
                double neg_b = -(double)b;
                double nb_pow = 1.0;
                for (int k = 0; k < f->degree; ++k) nb_pow *= neg_b; /* nb_pow = (-b)^d */
                for (int k = 0; k <= f->degree; ++k) {
                    alg_est += (double)f->coeffs[k] * a_pow * nb_pow;
                    a_pow *= (double)a;
                    if (neg_b != 0.0) nb_pow /= neg_b; /* (-b)^(d-k-1) */
                }
                alg_est = fabs(alg_est);
                s->alg_sieve[bj * s->width + ai] = (alg_est > 1.0) ? (float)log2(alg_est) : 0.f;
            }
        }
    }
    return args[0];
}

/** GNFS_SIEVE_RUN_SIDE(region, primes_array, roots_array, side) -- sieve one side.
 *  side=0: rational (root = m mod p), side=1: algebraic (root = r where f(r)===0 mod p).
 *  For each prime p with root r: sieve cells where a === b*r (mod p).
 *  In lattice coords: (e0x*u + e1x*v) === (e0y*u + e1y*v)*r (mod p).
 *  Rearranging: u*(e0x - e0y*r) === -v*(e1x - e1y*r) (mod p).
 *  For each row v, compute the starting u and stride within that row. */
static NqlValue nql_fn_gnfs_sieve_run_side(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsSieveRegion* s = (GnfsSieveRegion*)args[0].v.qs_ptr;
    if (args[1].type != NQL_VAL_ARRAY || args[2].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* primes = args[1].v.array;
    NqlArray* roots = args[2].v.array;
    int side = (int)nql_val_as_int(&args[3]);
    float* sieve = (side == 0) ? s->rat_sieve : s->alg_sieve;
    int half_w = s->width / 2, half_h = s->height / 2;

    uint32_t count = primes->length < roots->length ? primes->length : roots->length;
    for (uint32_t fi = 0; fi < count; ++fi) {
        uint64_t p = (uint64_t)nql_val_as_int(&primes->items[fi]);
        float lp = (float)log2((double)p);
        if (p <= 1u) continue;
        uint64_t r = (uint64_t)nql_val_as_int(&roots->items[fi]);

        /* Compute coefficients: a === b*r (mod p) in lattice coords.
         * a = e0x*u + e1x*v, b = e0y*u + e1y*v
         * => u*(e0x - e0y*r) + v*(e1x - e1y*r) === 0 (mod p)
         * Let A = (e0x - e0y*r) mod p, B = (e1x - e1y*r) mod p */
        int64_t A = ((s->e0x - (int64_t)(s->e0y * (int64_t)r)) % (int64_t)p + (int64_t)p) % (int64_t)p;
        int64_t B = ((s->e1x - (int64_t)(s->e1y * (int64_t)r)) % (int64_t)p + (int64_t)p) % (int64_t)p;

        if (A == 0 && B == 0) {
            /* p divides all norms in this lattice -- subtract from everything */
            for (int idx = 0; idx < s->width * s->height; ++idx) sieve[idx] -= lp;
            continue;
        }

        if (A == 0) {
            /* u is free, v must be === 0 mod p. Sieve entire rows where v === 0. */
            for (int vv = -(half_h / (int)p) * (int)p; vv < s->height - half_h; vv += (int)p) {
                int bj = vv + half_h;
                if (bj >= 0 && bj < s->height)
                    for (int ai = 0; ai < s->width; ++ai)
                        sieve[bj * s->width + ai] -= lp;
            }
            continue;
        }

        /* General case: for each row v, solve u === -v*B*A^{-1} (mod p) */
        uint64_t A_inv = powmod_u64((uint64_t)A, p - 2u, p);
        for (int bj = 0; bj < s->height; ++bj) {
            int v = bj - half_h;
            /* u_start === (-v * B * A_inv) mod p */
            int64_t vB = ((int64_t)(-v) * B) % (int64_t)p;
            if (vB < 0) vB += (int64_t)p;
            int64_t u_start_mod = (vB * (int64_t)A_inv) % (int64_t)p;
            /* Map to sieve index: ai = u + half_w, with u starting at u_start_mod - half_w adjusted */
            int64_t first_u = u_start_mod - ((int64_t)half_w / (int64_t)p) * (int64_t)p - (int64_t)half_w;
            while (first_u < -half_w) first_u += (int64_t)p;
            while (first_u >= -half_w + (int64_t)p) first_u -= (int64_t)p;
            for (int64_t u = first_u; u < s->width - half_w; u += (int64_t)p) {
                int ai = (int)(u + half_w);
                if (ai >= 0 && ai < s->width)
                    sieve[bj * s->width + ai] -= lp;
            }
        }
    }
    return args[0];
}

/** GNFS_SIEVE_CANDIDATES(region, rat_thresh, alg_thresh) -- scan both sides. */
static NqlValue nql_fn_gnfs_sieve_candidates(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsSieveRegion* s = (GnfsSieveRegion*)args[0].v.qs_ptr;
    float rt = (float)nql_val_as_float(&args[1]);
    float at = (float)nql_val_as_float(&args[2]);
    int half_w = s->width / 2, half_h = s->height / 2;

    NqlArray* cands = nql_array_alloc(256u);
    if (!cands) return nql_val_null();
    for (int bj = 0; bj < s->height; ++bj) {
        for (int ai = 0; ai < s->width; ++ai) {
            int idx = bj * s->width + ai;
            if (s->rat_sieve[idx] <= rt && s->alg_sieve[idx] <= at) {
                int u = ai - half_w, v = bj - half_h;
                int64_t a = (int64_t)s->e0x * u + (int64_t)s->e1x * v;
                int64_t b = (int64_t)s->e0y * u + (int64_t)s->e1y * v;
                /* Store as ARRAY(a, b) */
                NqlArray* pair = nql_array_alloc(2u);
                if (pair) {
                    nql_array_push(pair, nql_val_int(a));
                    nql_array_push(pair, nql_val_int(b));
                    nql_array_push(cands, nql_val_array(pair));
                }
            }
        }
    }
    return nql_val_array(cands);
}

/* ======================================================================
 * FIX 2: NATIVE GNFS FACTOR BASE (both sides + roots)
 * ====================================================================== */

typedef struct {
    uint64_t  N;
    int64_t   m;              /* common root: f(m) === 0 mod N */
    NqlPoly*  f;              /* algebraic polynomial (borrowed, not owned) */
    /* Rational side */
    uint64_t* rat_primes;
    uint64_t* rat_roots;      /* rat_roots[i] = m mod rat_primes[i] */
    double*   rat_logp;
    int       rat_count;
    uint64_t  rat_bound;
    /* Algebraic side */
    uint64_t* alg_primes;
    uint64_t* alg_roots;      /* alg_roots[i] = root r where f(r)===0 mod alg_primes[i] */
    double*   alg_logp;
    int       alg_count;
    uint64_t  alg_bound;
    /* LP bounds */
    uint64_t  rat_lp_bound;
    uint64_t  alg_lp_bound;
} GnfsFactorBaseNative;

static void gnfs_fb_native_free(GnfsFactorBaseNative* fb) {
    if (!fb) return;
    free(fb->rat_primes); free(fb->rat_roots); free(fb->rat_logp);
    free(fb->alg_primes); free(fb->alg_roots); free(fb->alg_logp);
    free(fb);
}

/** GNFS_FB_BUILD(N, f, m, rat_bound, alg_bound, lp_mult) -- build both FBs natively. */
static NqlValue nql_fn_gnfs_fb_build(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t N = (uint64_t)nql_val_as_int(&args[0]);
    if (args[1].type != NQL_VAL_POLY || !args[1].v.qs_ptr) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[1].v.qs_ptr;
    int64_t m = nql_val_as_int(&args[2]);
    int rat_bound = (int)nql_val_as_int(&args[3]);
    int alg_bound = (int)nql_val_as_int(&args[4]);
    int lp_mult = (ac >= 6u) ? (int)nql_val_as_int(&args[5]) : 64;

    GnfsFactorBaseNative* fb = (GnfsFactorBaseNative*)calloc(1, sizeof(GnfsFactorBaseNative));
    if (!fb) return nql_val_null();
    fb->N = N; fb->m = m; fb->f = f;
    fb->rat_bound = (uint64_t)rat_bound;
    fb->alg_bound = (uint64_t)alg_bound;

    /* Allocate max-sized arrays */
    int max_rat = rat_bound < 100000 ? rat_bound : 100000;
    int max_alg = alg_bound < 100000 ? alg_bound : 100000;
    fb->rat_primes = (uint64_t*)malloc((size_t)max_rat * sizeof(uint64_t));
    fb->rat_roots  = (uint64_t*)malloc((size_t)max_rat * sizeof(uint64_t));
    fb->rat_logp   = (double*)malloc((size_t)max_rat * sizeof(double));
    fb->alg_primes = (uint64_t*)malloc((size_t)max_alg * sizeof(uint64_t));
    fb->alg_roots  = (uint64_t*)malloc((size_t)max_alg * sizeof(uint64_t));
    fb->alg_logp   = (double*)malloc((size_t)max_alg * sizeof(double));
    if (!fb->rat_primes || !fb->alg_primes) { gnfs_fb_native_free(fb); return nql_val_null(); }

    /* Build rational FB: all primes p <= rat_bound, root = m mod p */
    uint64_t um = (uint64_t)(m >= 0 ? m : -m);
    for (uint64_t p = 2u; p <= (uint64_t)rat_bound && fb->rat_count < max_rat; ++p) {
        if (!miller_rabin_u64(p)) continue;
        if (N % p == 0u) continue; /* skip factors of N */
        fb->rat_primes[fb->rat_count] = p;
        fb->rat_roots[fb->rat_count] = um % p;
        fb->rat_logp[fb->rat_count] = log2((double)p);
        ++fb->rat_count;
    }

    /* Build algebraic FB: (p, r) pairs where f(r) === 0 mod p */
    for (uint64_t p = 2u; p <= (uint64_t)alg_bound && fb->alg_count < max_alg; ++p) {
        if (!miller_rabin_u64(p)) continue;
        if (N % p == 0u) continue;
        /* Find all roots of f mod p */
        for (uint64_t r = 0u; r < p; ++r) {
            if (nql_poly_eval_mod(f, r, p) == 0u) {
                if (fb->alg_count < max_alg) {
                    fb->alg_primes[fb->alg_count] = p;
                    fb->alg_roots[fb->alg_count] = r;
                    fb->alg_logp[fb->alg_count] = log2((double)p);
                    ++fb->alg_count;
                }
            }
        }
    }

    fb->rat_lp_bound = (fb->rat_count > 0) ? fb->rat_primes[fb->rat_count - 1] * (uint64_t)lp_mult : 0u;
    fb->alg_lp_bound = (fb->alg_count > 0) ? fb->alg_primes[fb->alg_count - 1] * (uint64_t)lp_mult : 0u;

    NqlValue rv; memset(&rv, 0, sizeof(rv));
    rv.type = NQL_VAL_QS_FACTOR_BASE; rv.v.qs_ptr = fb;
    return rv;
}

static NqlValue nql_fn_gnfs_fb_rat_count(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!a[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(((GnfsFactorBaseNative*)a[0].v.qs_ptr)->rat_count);
}
static NqlValue nql_fn_gnfs_fb_alg_count(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!a[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(((GnfsFactorBaseNative*)a[0].v.qs_ptr)->alg_count);
}
static NqlValue nql_fn_gnfs_fb_rat_primes(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!a[0].v.qs_ptr) return nql_val_null();
    GnfsFactorBaseNative* fb = (GnfsFactorBaseNative*)a[0].v.qs_ptr;
    NqlArray* arr = nql_array_alloc((uint32_t)fb->rat_count);
    if (!arr) return nql_val_null();
    for (int i = 0; i < fb->rat_count; ++i) nql_array_push(arr, nql_val_int((int64_t)fb->rat_primes[i]));
    return nql_val_array(arr);
}
static NqlValue nql_fn_gnfs_fb_alg_primes(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!a[0].v.qs_ptr) return nql_val_null();
    GnfsFactorBaseNative* fb = (GnfsFactorBaseNative*)a[0].v.qs_ptr;
    NqlArray* arr = nql_array_alloc((uint32_t)fb->alg_count);
    if (!arr) return nql_val_null();
    for (int i = 0; i < fb->alg_count; ++i) nql_array_push(arr, nql_val_int((int64_t)fb->alg_primes[i]));
    return nql_val_array(arr);
}

/* ======================================================================
 * PHASE 4: GNFS RELATION COLLECTION + FILTERING (corrected data model)
 * ====================================================================== */

typedef struct {
    int       rat_fb_count;
    int       alg_fb_count;
    int       max_rels;
    int       count;
    int64_t*  a_vals;          /* heap, length max_rels */
    int64_t*  b_vals;          /* heap, length max_rels */
    uint64_t* rat_lp;          /* large prime on rational side (0=smooth) */
    uint64_t* alg_lp;          /* large prime on algebraic side (0=smooth) */
    uint8_t*  rel_type;        /* 0=FF, 1=FP, 2=PF, 3=PP */
    uint32_t* rat_exponents;   /* flat: [rel * rat_fb_count + fi], full exponents */
    uint32_t* alg_exponents;   /* flat: [rel * alg_fb_count + fi], full exponents */
} GnfsRelationSet;

static void gnfs_relset_free(GnfsRelationSet* rs) {
    if (!rs) return;
    free(rs->a_vals); free(rs->b_vals);
    free(rs->rat_lp); free(rs->alg_lp); free(rs->rel_type);
    free(rs->rat_exponents); free(rs->alg_exponents);
    free(rs);
}

/** GNFS_SIEVE_AND_COLLECT(region, gnfs_fb, rels, rat_thresh, alg_thresh)
 *  Fused: scan candidates -> trial-factor both sides -> add relations.
 *  All in native C -- no NQL loop overhead or int64 overflow issues. */
static NqlValue nql_fn_gnfs_sieve_and_collect(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr || !args[1].v.qs_ptr || !args[2].v.qs_ptr) return nql_val_null();
    GnfsSieveRegion* s = (GnfsSieveRegion*)args[0].v.qs_ptr;
    GnfsFactorBaseNative* fb = (GnfsFactorBaseNative*)args[1].v.qs_ptr;
    GnfsRelationSet* rs = (GnfsRelationSet*)args[2].v.qs_ptr;
    float rat_thresh = (float)nql_val_as_float(&args[3]);
    float alg_thresh = (float)nql_val_as_float(&args[4]);
    int half_w = s->width / 2, half_h = s->height / 2;
    int added = 0;

    /* Heap-allocate exponent buffers to avoid stack overflow for large FBs */
    uint32_t* rat_exp_buf = (uint32_t*)calloc((size_t)fb->rat_count + 1u, sizeof(uint32_t));
    uint32_t* alg_exp_buf = (uint32_t*)calloc((size_t)fb->alg_count + 1u, sizeof(uint32_t));
    if (!rat_exp_buf || !alg_exp_buf) { free(rat_exp_buf); free(alg_exp_buf); return nql_val_int(0); }

    for (int bj = 0; bj < s->height; ++bj) {
        for (int ai = 0; ai < s->width; ++ai) {
            int idx = bj * s->width + ai;
            if (s->rat_sieve[idx] > rat_thresh || s->alg_sieve[idx] > alg_thresh) continue;

            int u = ai - half_w, v = bj - half_h;
            int64_t a = (int64_t)s->e0x * u + (int64_t)s->e1x * v;
            int64_t b = (int64_t)s->e0y * u + (int64_t)s->e1y * v;
            if (b <= 0 || a == 0) continue;
            if (gcd_u64((uint64_t)(a >= 0 ? a : -a), (uint64_t)b) != 1u) continue;

            /* Rational norm |a - b*m| */
            int64_t rn_s = a - b * fb->m;
            uint64_t rat_norm = (uint64_t)(rn_s >= 0 ? rn_s : -rn_s);
            if (rat_norm == 0u) continue;

            /* Trial-factor rational norm */
            uint64_t rat_rem = rat_norm;
            memset(rat_exp_buf, 0, (size_t)fb->rat_count * sizeof(uint32_t));
            for (int fi = 0; fi < fb->rat_count; ++fi) {
                uint64_t p = fb->rat_primes[fi];
                while (p > 0u && rat_rem > 0u && rat_rem % p == 0u) { rat_rem /= p; ++rat_exp_buf[fi]; }
            }
            bool rat_smooth = (rat_rem == 1u);
            bool rat_partial = (!rat_smooth && rat_rem <= fb->rat_lp_bound && rat_rem > 1u && miller_rabin_u64(rat_rem));
            if (!rat_smooth && !rat_partial) continue;

            /* Algebraic norm |(-b)^d * f(a/b)| = |sigma c_i * a^i * (-b)^(d-i)| exact int64 */
            int alg_d = fb->f->degree;
            int64_t alg_val = 0;
            int64_t alg_a_pow = 1;
            int64_t alg_nb_pow = 1;
            for (int k = 0; k < alg_d; ++k) alg_nb_pow *= -b;
            int64_t alg_nb_cur = alg_nb_pow;
            for (int k = 0; k <= alg_d; ++k) {
                alg_val += fb->f->coeffs[k] * alg_a_pow * alg_nb_cur;
                alg_a_pow *= a;
                if (b != 0 && k < alg_d) alg_nb_cur /= -b;
            }
            uint64_t alg_norm = (uint64_t)(alg_val >= 0 ? alg_val : -alg_val);
            if (alg_norm == 0u) continue;

            /* Trial-factor algebraic norm */
            uint64_t alg_rem = alg_norm;
            memset(alg_exp_buf, 0, (size_t)fb->alg_count * sizeof(uint32_t));
            for (int fi = 0; fi < fb->alg_count; ++fi) {
                uint64_t p = fb->alg_primes[fi];
                while (p > 0u && alg_rem > 0u && alg_rem % p == 0u) { alg_rem /= p; ++alg_exp_buf[fi]; }
            }
            bool alg_smooth = (alg_rem == 1u);
            bool alg_partial = (!alg_smooth && alg_rem <= fb->alg_lp_bound && alg_rem > 1u && miller_rabin_u64(alg_rem));
            if (!alg_smooth && !alg_partial) continue;

            /* Add relation */
            if (rs->count < rs->max_rels) {
                int ri = rs->count;
                rs->a_vals[ri] = a;
                rs->b_vals[ri] = b;
                rs->rat_lp[ri] = rat_smooth ? 1u : rat_rem;
                rs->alg_lp[ri] = alg_smooth ? 1u : alg_rem;
                uint8_t t = 0;
                if (!rat_smooth) t |= 1u;
                if (!alg_smooth) t |= 2u;
                rs->rel_type[ri] = t;
                int rc = fb->rat_count < rs->rat_fb_count ? fb->rat_count : rs->rat_fb_count;
                int ac2 = fb->alg_count < rs->alg_fb_count ? fb->alg_count : rs->alg_fb_count;
                memcpy(&rs->rat_exponents[ri * rs->rat_fb_count], rat_exp_buf, (size_t)rc * sizeof(uint32_t));
                memcpy(&rs->alg_exponents[ri * rs->alg_fb_count], alg_exp_buf, (size_t)ac2 * sizeof(uint32_t));
                ++rs->count;
                ++added;
            }
        }
    }
    free(rat_exp_buf); free(alg_exp_buf);
    return nql_val_int(added);
}

/** GNFS_RELATION_CREATE(rat_fb_count, alg_fb_count, max_rels) */
static NqlValue nql_fn_gnfs_rel_create(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int rfc = (int)nql_val_as_int(&args[0]);
    int afc = (int)nql_val_as_int(&args[1]);
    int max_r = (ac >= 3u) ? (int)nql_val_as_int(&args[2]) : 100000;
    if (max_r > 10000000) max_r = 10000000;
    GnfsRelationSet* rs = (GnfsRelationSet*)calloc(1, sizeof(GnfsRelationSet));
    if (!rs) return nql_val_null();
    rs->rat_fb_count = rfc; rs->alg_fb_count = afc; rs->max_rels = max_r;
    rs->a_vals = (int64_t*)calloc((size_t)max_r, sizeof(int64_t));
    rs->b_vals = (int64_t*)calloc((size_t)max_r, sizeof(int64_t));
    rs->rat_lp = (uint64_t*)calloc((size_t)max_r, sizeof(uint64_t));
    rs->alg_lp = (uint64_t*)calloc((size_t)max_r, sizeof(uint64_t));
    rs->rel_type = (uint8_t*)calloc((size_t)max_r, sizeof(uint8_t));
    rs->rat_exponents = (uint32_t*)calloc((size_t)max_r * (size_t)rfc, sizeof(uint32_t));
    rs->alg_exponents = (uint32_t*)calloc((size_t)max_r * (size_t)afc, sizeof(uint32_t));
    if (!rs->a_vals || !rs->b_vals || !rs->rat_lp || !rs->alg_lp || !rs->rel_type
        || !rs->rat_exponents || !rs->alg_exponents) {
        gnfs_relset_free(rs); return nql_val_null();
    }
    NqlValue r; memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_QS_RELATION_SET; r.v.qs_ptr = rs;
    return r;
}

/** GNFS_TRIAL_FACTOR_RATIONAL(a, b, m, rat_primes_array) -- trial-divide |a - b*m| over rational FB.
 *  Returns ARRAY of exponents + cofactor as last element. Uses BigNum for a-b*m. */
static NqlValue nql_fn_gnfs_trial_rat(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t a = nql_val_as_int(&args[0]);
    int64_t b = nql_val_as_int(&args[1]);
    BigNum m_bn = nql_val_as_bignum(&args[2]);
    if (args[3].type != NQL_VAL_ARRAY || !args[3].v.array) return nql_val_null();
    NqlArray* primes = args[3].v.array;
    /* Compute |a - b*m| as uint64 if possible */
    BigNum a_bn; bn_from_u64(&a_bn, (uint64_t)(a >= 0 ? a : -a));
    BigNum b_bn; bn_from_u64(&b_bn, (uint64_t)(b >= 0 ? b : -b));
    BigNum bm; bn_mul(&bm, &b_bn, &m_bn);
    BigNum norm;
    if (bn_cmp(&a_bn, &bm) >= 0) { bn_copy(&norm, &a_bn); bn_sub(&norm, &bm); }
    else { bn_copy(&norm, &bm); bn_sub(&norm, &a_bn); }
    /* Trial divide over FB */
    uint64_t rem = bn_fits_u64(&norm) ? bn_to_u64(&norm) : UINT64_MAX;
    NqlArray* result = nql_array_alloc(primes->length + 1u);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < primes->length; ++i) {
        uint64_t p = (uint64_t)nql_val_as_int(&primes->items[i]);
        uint32_t exp = 0u;
        while (p > 0u && rem > 0u && rem % p == 0u) { rem /= p; ++exp; }
        nql_array_push(result, nql_val_int((int64_t)exp));
    }
    nql_array_push(result, nql_val_int((int64_t)rem)); /* cofactor */
    return nql_val_array(result);
}

/** GNFS_TRIAL_FACTOR_ALGEBRAIC(a, b, f, alg_primes, alg_roots) -- trial-divide algebraic norm.
 *  First checks divisibility via f(a/b) mod p = 0, i.e., sigma c_i * a^i * (-b)^(d-i) === 0 mod p.
 *  Then counts the exponent by repeated division. Returns ARRAY of exponents + cofactor. */
static NqlValue nql_fn_gnfs_trial_alg(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t a = nql_val_as_int(&args[0]);
    int64_t b = nql_val_as_int(&args[1]);
    if (args[2].type != NQL_VAL_POLY || !args[2].v.qs_ptr) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[2].v.qs_ptr;
    if (args[3].type != NQL_VAL_ARRAY || !args[3].v.array) return nql_val_null();
    if (args[4].type != NQL_VAL_ARRAY || !args[4].v.array) return nql_val_null();
    NqlArray* primes = args[3].v.array;
    NqlArray* roots = args[4].v.array;
    /* Compute algebraic norm as uint64 (for small inputs) */
    /* |(-b)^d * f(a/b)| = |sigma c_i * a^i * (-b)^(d-i)| */
    int d = f->degree;
    int64_t norm_val = 0;
    int64_t a_pow = 1, neg_b_pow = 1;
    for (int i = 0; i < d; ++i) neg_b_pow *= -b;
    /* neg_b_pow = (-b)^d */
    int64_t nb_inv = (b != 0) ? 1 : 0; /* we'll divide by (-b) each step */
    int64_t cur_nb = neg_b_pow;
    for (int i = 0; i <= d; ++i) {
        norm_val += f->coeffs[i] * a_pow * cur_nb;
        a_pow *= a;
        if (b != 0 && i < d) cur_nb /= (-b);
    }
    uint64_t rem = (uint64_t)(norm_val >= 0 ? norm_val : -norm_val);

    uint32_t count = primes->length < roots->length ? (uint32_t)primes->length : (uint32_t)roots->length;
    NqlArray* result = nql_array_alloc(count + 1u);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t p = (uint64_t)nql_val_as_int(&primes->items[i]);
        uint32_t exp = 0u;
        while (p > 0u && rem > 0u && rem % p == 0u) { rem /= p; ++exp; }
        nql_array_push(result, nql_val_int((int64_t)exp));
    }
    nql_array_push(result, nql_val_int((int64_t)rem));
    return nql_val_array(result);
}

/** GNFS_RELATION_ADD(rels, a, b, rat_exp_array, alg_exp_array, rat_cofactor, alg_cofactor)
 *  Add a relation with full exponent data from trial factoring. */
static NqlValue nql_fn_gnfs_rel_add(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsRelationSet* rs = (GnfsRelationSet*)args[0].v.qs_ptr;
    if (rs->count >= rs->max_rels) return args[0];

    int ri = rs->count;
    rs->a_vals[ri] = nql_val_as_int(&args[1]);
    rs->b_vals[ri] = nql_val_as_int(&args[2]);

    /* Copy rational exponents from array arg */
    if (args[3].type == NQL_VAL_ARRAY && args[3].v.array) {
        NqlArray* re = args[3].v.array;
        for (uint32_t i = 0; i < re->length && (int)i < rs->rat_fb_count; ++i)
            rs->rat_exponents[ri * rs->rat_fb_count + i] = (uint32_t)nql_val_as_int(&re->items[i]);
    }
    /* Copy algebraic exponents from array arg */
    if (args[4].type == NQL_VAL_ARRAY && args[4].v.array) {
        NqlArray* ae = args[4].v.array;
        for (uint32_t i = 0; i < ae->length && (int)i < rs->alg_fb_count; ++i)
            rs->alg_exponents[ri * rs->alg_fb_count + i] = (uint32_t)nql_val_as_int(&ae->items[i]);
    }

    rs->rat_lp[ri] = (ac >= 6u) ? (uint64_t)nql_val_as_int(&args[5]) : 1u;
    rs->alg_lp[ri] = (ac >= 7u) ? (uint64_t)nql_val_as_int(&args[6]) : 1u;
    uint8_t t = 0;
    if (rs->rat_lp[ri] > 1u) t |= 1u;
    if (rs->alg_lp[ri] > 1u) t |= 2u;
    rs->rel_type[ri] = t;
    ++rs->count;
    return args[0];
}

static NqlValue nql_fn_gnfs_rel_count(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(((GnfsRelationSet*)args[0].v.qs_ptr)->count);
}

/** GNFS_RELATION_COUNT_BY_TYPE(rels, type) -- 0=FF, 1=FP, 2=PF, 3=PP */
static NqlValue nql_fn_gnfs_rel_count_type(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_int(0);
    GnfsRelationSet* rs = (GnfsRelationSet*)args[0].v.qs_ptr;
    int t = (int)nql_val_as_int(&args[1]);
    int c = 0;
    for (int i = 0; i < rs->count; ++i) if (rs->rel_type[i] == (uint8_t)t) ++c;
    return nql_val_int(c);
}

/** GNFS_FILTER_SINGLETONS(rels) -- remove relations with LP appearing only once on either side. */
static NqlValue nql_fn_gnfs_filter_singletons(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsRelationSet* rs = (GnfsRelationSet*)args[0].v.qs_ptr;

    bool changed = true;
    while (changed) {
        changed = false;
        /* Count LP occurrences -- simple hash via modulo */
        #define GNFS_LP_HASH_SIZE 65537
        int* rat_occ = (int*)calloc(GNFS_LP_HASH_SIZE, sizeof(int));
        int* alg_occ = (int*)calloc(GNFS_LP_HASH_SIZE, sizeof(int));
        if (!rat_occ || !alg_occ) { free(rat_occ); free(alg_occ); return args[0]; }

        for (int i = 0; i < rs->count; ++i) {
            if (rs->rat_lp[i] > 1u) rat_occ[rs->rat_lp[i] % GNFS_LP_HASH_SIZE]++;
            if (rs->alg_lp[i] > 1u) alg_occ[rs->alg_lp[i] % GNFS_LP_HASH_SIZE]++;
        }

        int write = 0;
        for (int i = 0; i < rs->count; ++i) {
            bool discard = false;
            if (rs->rat_lp[i] > 1u && rat_occ[rs->rat_lp[i] % GNFS_LP_HASH_SIZE] < 2) discard = true;
            if (rs->alg_lp[i] > 1u && alg_occ[rs->alg_lp[i] % GNFS_LP_HASH_SIZE] < 2) discard = true;
            if (discard) { changed = true; continue; }
            if (write != i) {
                rs->a_vals[write] = rs->a_vals[i];
                rs->b_vals[write] = rs->b_vals[i];
                rs->rat_lp[write] = rs->rat_lp[i];
                rs->alg_lp[write] = rs->alg_lp[i];
                rs->rel_type[write] = rs->rel_type[i];
                memcpy(&rs->rat_exponents[write * rs->rat_fb_count],
                       &rs->rat_exponents[i * rs->rat_fb_count],
                       (size_t)rs->rat_fb_count * sizeof(uint32_t));
                memcpy(&rs->alg_exponents[write * rs->alg_fb_count],
                       &rs->alg_exponents[i * rs->alg_fb_count],
                       (size_t)rs->alg_fb_count * sizeof(uint32_t));
            }
            ++write;
        }
        rs->count = write;
        free(rat_occ); free(alg_occ);
        #undef GNFS_LP_HASH_SIZE
    }
    return args[0];
}

/** GNFS_RELATION_EXPORT(rels, filename) -- write relations to text file for checkpointing. */
static NqlValue nql_fn_gnfs_rel_export(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr || args[1].type != NQL_VAL_STRING) return nql_val_bool(false);
    GnfsRelationSet* rs = (GnfsRelationSet*)args[0].v.qs_ptr;
    FILE* fp = fopen(args[1].v.sval, "w");
    if (!fp) return nql_val_bool(false);
    fprintf(fp, "# GNFS relations: %d\n", rs->count);
    for (int i = 0; i < rs->count; ++i) {
        fprintf(fp, "%" PRId64 ",%" PRId64 ",%" PRIu64 ",%" PRIu64 ",%d\n",
            rs->a_vals[i], rs->b_vals[i], rs->rat_lp[i], rs->alg_lp[i], (int)rs->rel_type[i]);
    }
    fclose(fp);
    return nql_val_bool(true);
}

/** GNFS_RELATION_IMPORT(filename, rat_fb_count, alg_fb_count) -- read relations from file. */
static NqlValue nql_fn_gnfs_rel_import(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    int rfc = (int)nql_val_as_int(&args[1]);
    int afc = (int)nql_val_as_int(&args[2]);
    FILE* fp = fopen(args[0].v.sval, "r");
    if (!fp) return nql_val_null();
    /* Count lines first */
    int line_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) { if (line[0] != '#') ++line_count; }
    rewind(fp);

    GnfsRelationSet* rs = (GnfsRelationSet*)calloc(1, sizeof(GnfsRelationSet));
    if (!rs) { fclose(fp); return nql_val_null(); }
    rs->rat_fb_count = rfc; rs->alg_fb_count = afc;
    rs->max_rels = line_count + 1000;
    rs->a_vals = (int64_t*)calloc((size_t)rs->max_rels, sizeof(int64_t));
    rs->b_vals = (int64_t*)calloc((size_t)rs->max_rels, sizeof(int64_t));
    rs->rat_lp = (uint64_t*)calloc((size_t)rs->max_rels, sizeof(uint64_t));
    rs->alg_lp = (uint64_t*)calloc((size_t)rs->max_rels, sizeof(uint64_t));
    rs->rel_type = (uint8_t*)calloc((size_t)rs->max_rels, sizeof(uint8_t));
    if (!rs->a_vals || !rs->b_vals) { gnfs_relset_free(rs); fclose(fp); return nql_val_null(); }

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#') continue;
        int64_t a, b; uint64_t rl, al; int t;
        if (sscanf(line, "%" SCNd64 ",%" SCNd64 ",%" SCNu64 ",%" SCNu64 ",%d", &a, &b, &rl, &al, &t) == 5) {
            int ri = rs->count;
            if (ri >= rs->max_rels) break;
            rs->a_vals[ri] = a; rs->b_vals[ri] = b;
            rs->rat_lp[ri] = rl; rs->alg_lp[ri] = al;
            rs->rel_type[ri] = (uint8_t)t;
            ++rs->count;
        }
    }
    fclose(fp);
    NqlValue r; memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_QS_RELATION_SET; r.v.qs_ptr = rs;
    return r;
}

/** GNFS_RELATION_A_VALS(rels) -- return array of all a-values. */
static NqlValue nql_fn_gnfs_rel_a_vals(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsRelationSet* rs = (GnfsRelationSet*)args[0].v.qs_ptr;
    NqlArray* arr = nql_array_alloc((uint32_t)rs->count);
    if (!arr) return nql_val_null();
    for (int i = 0; i < rs->count; ++i)
        nql_array_push(arr, nql_val_int(rs->a_vals[i]));
    return nql_val_array(arr);
}

/** GNFS_RELATION_B_VALS(rels) -- return array of all b-values. */
static NqlValue nql_fn_gnfs_rel_b_vals(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsRelationSet* rs = (GnfsRelationSet*)args[0].v.qs_ptr;
    NqlArray* arr = nql_array_alloc((uint32_t)rs->count);
    if (!arr) return nql_val_null();
    for (int i = 0; i < rs->count; ++i)
        nql_array_push(arr, nql_val_int(rs->b_vals[i]));
    return nql_val_array(arr);
}

/** GNFS_RELATION_PARITY_ROWS(rels, fb_rat_count, fb_alg_count) -- build parity row arrays from stored exponents.
 *  Returns array of arrays. Each inner array = list of column indices where exponent is odd.
 *  Column layout: [0..rfc-1] = rational FB, [rfc..rfc+afc-1] = algebraic FB. */
static NqlValue nql_fn_gnfs_rel_parity_rows(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsRelationSet* rs = (GnfsRelationSet*)args[0].v.qs_ptr;
    int rfc = (int)nql_val_as_int(&args[1]);
    int afc = (int)nql_val_as_int(&args[2]);
    if (rfc > rs->rat_fb_count) rfc = rs->rat_fb_count;
    if (afc > rs->alg_fb_count) afc = rs->alg_fb_count;

    NqlArray* rows = nql_array_alloc((uint32_t)rs->count);
    if (!rows) return nql_val_null();

    for (int i = 0; i < rs->count; ++i) {
        NqlArray* row = nql_array_alloc(32u);
        if (!row) { nql_array_push(rows, nql_val_array(nql_array_alloc(0u))); continue; }

        /* Rational exponent parities -> columns 0..rfc-1 */
        for (int fi = 0; fi < rfc; ++fi) {
            if (rs->rat_exponents[i * rs->rat_fb_count + fi] & 1u)
                nql_array_push(row, nql_val_int(fi));
        }
        /* Algebraic exponent parities -> columns rfc..rfc+afc-1 */
        for (int fi = 0; fi < afc; ++fi) {
            if (rs->alg_exponents[i * rs->alg_fb_count + fi] & 1u)
                nql_array_push(row, nql_val_int(rfc + fi));
        }
        nql_array_push(rows, nql_val_array(row));
    }
    return nql_val_array(rows);
}

/** GNFS_DENSE_SOLVE(parity_rows, nrels, ncols) -- build dense GF2 matrix, eliminate, return null-space.
 *  All-in-one native function that replaces the broken NQL matrix loop.
 *  Returns array of dependency vectors (each = array of participating row indices). */
static NqlValue nql_fn_gnfs_dense_solve(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* parity = args[0].v.array;
    int nrels = (int)nql_val_as_int(&args[1]);
    int ncols = (int)nql_val_as_int(&args[2]);
    if (nrels <= 0 || ncols <= 0) return nql_val_null();
    if (nrels > (int)parity->length) nrels = (int)parity->length;

    int row_words = (ncols + 63) / 64;
    int hist_words = (nrels + 63) / 64;

    /* Allocate packed matrix + history */
    uint64_t* mat = (uint64_t*)calloc((size_t)nrels * (size_t)row_words, sizeof(uint64_t));
    uint64_t* hist = (uint64_t*)calloc((size_t)nrels * (size_t)hist_words, sizeof(uint64_t));
    if (!mat || !hist) { free(mat); free(hist); return nql_val_null(); }

    /* Fill matrix from parity rows */
    for (int ri = 0; ri < nrels; ++ri) {
        if (parity->items[ri].type != NQL_VAL_ARRAY || !parity->items[ri].v.array) continue;
        NqlArray* row = parity->items[ri].v.array;
        for (uint32_t j = 0; j < row->length; ++j) {
            int col = (int)nql_val_as_int(&row->items[j]);
            if (col >= 0 && col < ncols)
                mat[ri * row_words + col / 64] |= (1ull << (col % 64));
        }
        /* Init history as identity */
        hist[ri * hist_words + ri / 64] = (1ull << (ri % 64));
    }

    /* Gaussian elimination */
    bool* is_pivot = (bool*)calloc((size_t)nrels, sizeof(bool));
    if (!is_pivot) { free(mat); free(hist); return nql_val_null(); }

    for (int col = 0; col < ncols; ++col) {
        int w = col / 64, b = col % 64;
        int piv = -1;
        for (int ri = 0; ri < nrels; ++ri) {
            if (is_pivot[ri]) continue;
            if (mat[ri * row_words + w] & (1ull << b)) { piv = ri; is_pivot[ri] = true; break; }
        }
        if (piv < 0) continue;
        for (int ri = 0; ri < nrels; ++ri) {
            if (ri == piv) continue;
            if (!(mat[ri * row_words + w] & (1ull << b))) continue;
            for (int ww = 0; ww < row_words; ++ww) mat[ri * row_words + ww] ^= mat[piv * row_words + ww];
            for (int ww = 0; ww < hist_words; ++ww) hist[ri * hist_words + ww] ^= hist[piv * hist_words + ww];
        }
    }

    /* Collect null-space vectors (zero rows after elimination) */
    NqlArray* vectors = nql_array_alloc(256u);
    if (!vectors) { free(mat); free(hist); free(is_pivot); return nql_val_null(); }
    for (int ri = 0; ri < nrels; ++ri) {
        bool zero = true;
        for (int w = 0; w < row_words; ++w) { if (mat[ri * row_words + w]) { zero = false; break; } }
        if (!zero) continue;
        /* Extract history vector as array of relation indices */
        NqlArray* dep = nql_array_alloc(64u);
        if (!dep) continue;
        for (int rj = 0; rj < nrels; ++rj) {
            if (hist[ri * hist_words + rj / 64] & (1ull << (rj % 64)))
                nql_array_push(dep, nql_val_int(rj));
        }
        if (dep->length > 0u) nql_array_push(vectors, nql_val_array(dep));
        else nql_array_free(dep);
    }

    free(mat); free(hist); free(is_pivot);
    return nql_val_array(vectors);
}

/* ======================================================================
 * PHASE 5: SPARSE GF(2) BLOCK LANCZOS
 * ====================================================================== */

typedef struct {
    int   rows, cols, nnz;
    int*  row_ptr;   /* CSR row pointers, length rows+1 */
    int*  col_idx;   /* CSR column indices, length nnz */
} GnfsSparseMatrix;

static void gnfs_sparse_free(GnfsSparseMatrix* m) {
    if (!m) return; free(m->row_ptr); free(m->col_idx); free(m);
}

/** GNFS_SPARSE_MATRIX_BUILD(parity_rows_array, num_cols) -- build CSR from array of arrays.
 *  Each inner array is a list of set column indices for that row. */
static NqlValue nql_fn_gnfs_sparse_build(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* rows = args[0].v.array;
    int ncols = (int)nql_val_as_int(&args[1]);
    int nrows = (int)rows->length;

    /* First pass: count non-zeros */
    int nnz = 0;
    for (int r = 0; r < nrows; ++r) {
        if (rows->items[r].type == NQL_VAL_ARRAY && rows->items[r].v.array)
            nnz += (int)rows->items[r].v.array->length;
    }

    GnfsSparseMatrix* m = (GnfsSparseMatrix*)calloc(1, sizeof(GnfsSparseMatrix));
    if (!m) return nql_val_null();
    m->rows = nrows; m->cols = ncols; m->nnz = nnz;
    m->row_ptr = (int*)malloc(((size_t)nrows + 1) * sizeof(int));
    m->col_idx = (int*)malloc((size_t)nnz * sizeof(int));
    if (!m->row_ptr || !m->col_idx) { gnfs_sparse_free(m); return nql_val_null(); }

    int pos = 0;
    for (int r = 0; r < nrows; ++r) {
        m->row_ptr[r] = pos;
        if (rows->items[r].type == NQL_VAL_ARRAY && rows->items[r].v.array) {
            NqlArray* row = rows->items[r].v.array;
            for (uint32_t j = 0; j < row->length; ++j)
                m->col_idx[pos++] = (int)nql_val_as_int(&row->items[j]);
        }
    }
    m->row_ptr[nrows] = pos;

    NqlValue rv; memset(&rv, 0, sizeof(rv));
    rv.type = NQL_VAL_QS_GF2_MATRIX; rv.v.qs_ptr = m;
    return rv;
}

/** GNFS_SPARSE_ROWS/COLS/NNZ accessors */
static NqlValue nql_fn_gnfs_sparse_rows(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!a[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(((GnfsSparseMatrix*)a[0].v.qs_ptr)->rows);
}
static NqlValue nql_fn_gnfs_sparse_cols(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!a[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(((GnfsSparseMatrix*)a[0].v.qs_ptr)->cols);
}
static NqlValue nql_fn_gnfs_sparse_nnz(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!a[0].v.qs_ptr) return nql_val_int(0);
    return nql_val_int(((GnfsSparseMatrix*)a[0].v.qs_ptr)->nnz);
}

/** Sparse matrix-vector multiply over GF(2): out[row] = XOR of v[col] for each nonzero in row. */
static void gnfs_sparse_mul(const GnfsSparseMatrix* m, const uint64_t* v, uint64_t* out, int n) {
    memset(out, 0, (size_t)n * sizeof(uint64_t));
    for (int r = 0; r < m->rows && r < n; ++r) {
        uint64_t acc = 0u;
        for (int j = m->row_ptr[r]; j < m->row_ptr[r + 1]; ++j) {
            int c = m->col_idx[j];
            if (c >= 0 && c < n) acc ^= v[c];
        }
        out[r] = acc;
    }
}

/** Sparse transpose-multiply: out[col] = XOR of v[row] for each nonzero (row,col). */
static void gnfs_sparse_mul_transpose(const GnfsSparseMatrix* m, const uint64_t* v, uint64_t* out, int n) {
    memset(out, 0, (size_t)n * sizeof(uint64_t));
    for (int r = 0; r < m->rows && r < n; ++r) {
        for (int j = m->row_ptr[r]; j < m->row_ptr[r + 1]; ++j) {
            int c = m->col_idx[j];
            if (c >= 0 && c < n) out[c] ^= v[r];
        }
    }
}

/** GNFS_BLOCK_LANCZOS(sparse_matrix) -- solve null space of M^T*M over GF(2).
 *  Uses the symmetric product B = M^T * M so Lanczos applies.
 *  Block size = 64 (one machine word). Returns array of null-space vectors.
 *  Each vector is an array of row indices that XOR to zero in the original parity matrix. */
static NqlValue nql_fn_gnfs_block_lanczos(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (!args[0].v.qs_ptr) return nql_val_null();
    GnfsSparseMatrix* mat = (GnfsSparseMatrix*)args[0].v.qs_ptr;
    int n = mat->cols > mat->rows ? mat->cols : mat->rows;
    if (n == 0) return nql_val_null();

    /* Working vectors -- each is n x 64 bits (one uint64 per row, 64 parallel vectors) */
    uint64_t* x    = (uint64_t*)calloc((size_t)n, sizeof(uint64_t)); /* accumulated solution */
    uint64_t* v    = (uint64_t*)calloc((size_t)n, sizeof(uint64_t)); /* current Lanczos vector */
    uint64_t* v_prev = (uint64_t*)calloc((size_t)n, sizeof(uint64_t));
    uint64_t* Bv   = (uint64_t*)calloc((size_t)n, sizeof(uint64_t)); /* B*v = M^T*M*v */
    uint64_t* tmp  = (uint64_t*)calloc((size_t)n, sizeof(uint64_t)); /* scratch */
    if (!x || !v || !v_prev || !Bv || !tmp) {
        free(x); free(v); free(v_prev); free(Bv); free(tmp);
        return nql_val_null();
    }

    /* Initialize v with random vector */
    uint64_t rng = 0xDEADBEEFCAFE1234ULL ^ (uint64_t)n;
    for (int i = 0; i < n; ++i) {
        rng ^= rng << 13u; rng ^= rng >> 7u; rng ^= rng << 17u;
        v[i] = rng;
    }

    /* Lanczos iteration on B = M^T * M:
     * v_{i+1} = B * v_i XOR v_{i-1}  (three-term recurrence over GF(2))
     * x accumulates: x ^= v_i at each step
     * Converged when B*v_i = 0 (v is in null space of B, hence M*v is in null space of M^T) */
    int max_iter = (n / 64) + 200;
    if (max_iter > 50000) max_iter = 50000;

    for (int iter = 0; iter < max_iter; ++iter) {
        /* Bv = M^T * M * v */
        gnfs_sparse_mul(mat, v, tmp, n);       /* tmp = M * v */
        gnfs_sparse_mul_transpose(mat, tmp, Bv, n); /* Bv = M^T * tmp */

        /* Accumulate solution */
        for (int i = 0; i < n; ++i) x[i] ^= v[i];

        /* Check convergence */
        bool converged = true;
        for (int i = 0; i < n; ++i) { if (Bv[i]) { converged = false; break; } }
        if (converged) break;

        /* Three-term recurrence: v_next = Bv XOR v_prev */
        for (int i = 0; i < n; ++i) {
            uint64_t v_next = Bv[i] ^ v_prev[i];
            v_prev[i] = v[i];
            v[i] = v_next;
        }
    }

    /* Extract null-space vectors: each of the 64 bit-columns of x is a candidate.
     * Verify each by checking M * candidate == 0. */
    NqlArray* vectors = nql_array_alloc(64u);
    if (!vectors) { free(x); free(v); free(v_prev); free(Bv); free(tmp); return nql_val_null(); }

    for (int bit = 0; bit < 64; ++bit) {
        /* Extract bit-column as a dense vector, then verify */
        memset(tmp, 0, (size_t)n * sizeof(uint64_t));
        bool has_bits = false;
        for (int i = 0; i < n; ++i) {
            if (x[i] & (1ull << bit)) { tmp[i] = 1u; has_bits = true; }
        }
        if (!has_bits) continue;

        /* Verify: check that M * candidate == 0 (mod 2) */
        bool valid = true;
        for (int r = 0; r < mat->rows; ++r) {
            uint64_t dot = 0u;
            for (int j = mat->row_ptr[r]; j < mat->row_ptr[r + 1]; ++j) {
                int c = mat->col_idx[j];
                if (c >= 0 && c < n) dot ^= tmp[c];
            }
            if (dot & 1u) { valid = false; break; }
        }
        if (!valid) continue;

        /* Collect row indices */
        NqlArray* vec = nql_array_alloc(128u);
        if (!vec) continue;
        for (int i = 0; i < n; ++i) {
            if (x[i] & (1ull << bit))
                nql_array_push(vec, nql_val_int(i));
        }
        if (vec->length > 0u) nql_array_push(vectors, nql_val_array(vec));
        else nql_array_free(vec);
    }

    free(x); free(v); free(v_prev); free(Bv); free(tmp);
    return nql_val_array(vectors);
}

/* ======================================================================
 * PHASE 6: ALGEBRAIC SQUARE ROOT + FINAL EXTRACTION
 * ====================================================================== */

/** GNFS_EXTRACT_RATIONAL_PRODUCT(dep_indices, rels, m, N) -- rational side raw product.
 *  Computes x = prod (a_i - b_i * m) mod N -- the raw product, NOT the sqrt. */
static NqlValue nql_fn_gnfs_extract_rat_sqrt(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (!args[1].v.qs_ptr) return nql_val_null();
    NqlArray* deps = args[0].v.array;
    GnfsRelationSet* rs = (GnfsRelationSet*)args[1].v.qs_ptr;
    int64_t m_val = nql_val_as_int(&args[2]);
    uint64_t N64 = (uint64_t)nql_val_as_int(&args[3]);

    /* x = prod (a_i - b_i * m) mod N */
    uint64_t x = 1u;
    for (uint32_t k = 0; k < deps->length; ++k) {
        int idx = (int)nql_val_as_int(&deps->items[k]);
        if (idx < 0 || idx >= rs->count) continue;
        int64_t a = rs->a_vals[idx];
        int64_t b = rs->b_vals[idx];
        int64_t norm_s = a - b * m_val;
        uint64_t norm_u = (uint64_t)(norm_s >= 0 ? norm_s : -norm_s);
        if (norm_u == 0u) continue;
        x = mulmod_u64(x, norm_u % N64, N64);
    }
    return nql_val_int((int64_t)x);
}

/** GNFS_EXTRACT_TRY_FACTOR(x_rat, y_alg, N) -- try GCD(x+/-y, N). */
static NqlValue nql_fn_gnfs_extract_try(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    BigNum x = nql_val_as_bignum(&args[0]);
    BigNum y = nql_val_as_bignum(&args[1]);
    BigNum N = nql_val_as_bignum(&args[2]);

    /* g1 = GCD(x - y, N) */
    BigNum diff;
    if (bn_cmp(&x, &y) >= 0) { bn_copy(&diff, &x); bn_sub(&diff, &y); }
    else { bn_copy(&diff, &y); bn_sub(&diff, &x); }
    BigNum g1; bn_gcd(&g1, &diff, &N);
    if (!bn_is_one(&g1) && bn_cmp(&g1, &N) != 0) return nql_val_from_bignum(&g1);

    /* g2 = GCD(x + y, N) */
    BigNum sum; bn_copy(&sum, &x); bn_add(&sum, &y);
    /* Reduce mod N if needed */
    if (bn_cmp(&sum, &N) >= 0) bn_sub(&sum, &N);
    BigNum g2; bn_gcd(&g2, &sum, &N);
    if (!bn_is_one(&g2) && bn_cmp(&g2, &N) != 0) return nql_val_from_bignum(&g2);

    return nql_val_null();
}

/** GNFS_ALGEBRAIC_SQRT(dep_indices, rels, rat_primes, alg_primes, N) -- algebraic side square root.
 *  Computes y from stored exponents: for each FB prime p, sum exponents across selected relations,
 *  halve (guaranteed even by GF(2) dependency), compute y = prod p^(sum/2) mod N.
 *  This works because the dependency ensures all exponent sums are even. */
static NqlValue nql_fn_gnfs_alg_sqrt(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (!args[1].v.qs_ptr) return nql_val_null();
    if (args[2].type != NQL_VAL_ARRAY || !args[2].v.array) return nql_val_null();
    if (args[3].type != NQL_VAL_ARRAY || !args[3].v.array) return nql_val_null();

    NqlArray* deps = args[0].v.array;
    GnfsRelationSet* rs = (GnfsRelationSet*)args[1].v.qs_ptr;
    NqlArray* rat_primes = args[2].v.array;
    NqlArray* alg_primes = args[3].v.array;
    BigNum N = nql_val_as_bignum(&args[4]);

    /* Sum rational exponents across dependency */
    uint64_t* rat_sum = (uint64_t*)calloc((size_t)rs->rat_fb_count, sizeof(uint64_t));
    uint64_t* alg_sum = (uint64_t*)calloc((size_t)rs->alg_fb_count, sizeof(uint64_t));
    if (!rat_sum || !alg_sum) { free(rat_sum); free(alg_sum); return nql_val_null(); }

    for (uint32_t k = 0; k < deps->length; ++k) {
        int idx = (int)nql_val_as_int(&deps->items[k]);
        if (idx < 0 || idx >= rs->count) continue;
        for (int fi = 0; fi < rs->rat_fb_count; ++fi)
            rat_sum[fi] += rs->rat_exponents[idx * rs->rat_fb_count + fi];
        for (int fi = 0; fi < rs->alg_fb_count; ++fi)
            alg_sum[fi] += rs->alg_exponents[idx * rs->alg_fb_count + fi];
    }

    /* Compute y = prod rat_prime[i]^(rat_sum[i]/2) * prod alg_prime[i]^(alg_sum[i]/2) mod N */
    BigNum y; bn_from_u64(&y, 1u);

    for (int fi = 0; fi < rs->rat_fb_count && (uint32_t)fi < rat_primes->length; ++fi) {
        uint64_t half = rat_sum[fi] / 2u;
        if (half == 0u) continue;
        uint64_t p = (uint64_t)nql_val_as_int(&rat_primes->items[fi]);
        if (p <= 1u) continue;
        /* y = y * p^half mod N */
        if (bn_fits_u64(&N)) {
            uint64_t n64 = bn_to_u64(&N);
            uint64_t pw = powmod_u64(p, half, n64);
            uint64_t y64 = bn_fits_u64(&y) ? bn_to_u64(&y) : (uint64_t)bn_mod_u64(&y, n64);
            y64 = mulmod_u64(y64, pw, n64);
            bn_from_u64(&y, y64);
        } else {
            BigNum p_bn; bn_from_u64(&p_bn, p);
            BigNum pw; bn_from_u64(&pw, 1u);
            for (uint64_t j = 0; j < half; ++j) bn_mulmod_peasant(&pw, &pw, &p_bn, &N);
            bn_mulmod_peasant(&y, &y, &pw, &N);
        }
    }

    for (int fi = 0; fi < rs->alg_fb_count && (uint32_t)fi < alg_primes->length; ++fi) {
        uint64_t half = alg_sum[fi] / 2u;
        if (half == 0u) continue;
        uint64_t p = (uint64_t)nql_val_as_int(&alg_primes->items[fi]);
        if (p <= 1u) continue;
        if (bn_fits_u64(&N)) {
            uint64_t n64 = bn_to_u64(&N);
            uint64_t pw = powmod_u64(p, half, n64);
            uint64_t y64 = bn_fits_u64(&y) ? bn_to_u64(&y) : (uint64_t)bn_mod_u64(&y, n64);
            y64 = mulmod_u64(y64, pw, n64);
            bn_from_u64(&y, y64);
        } else {
            BigNum p_bn; bn_from_u64(&p_bn, p);
            BigNum pw; bn_from_u64(&pw, 1u);
            for (uint64_t j = 0; j < half; ++j) bn_mulmod_peasant(&pw, &pw, &p_bn, &N);
            bn_mulmod_peasant(&y, &y, &pw, &N);
        }
    }

    free(rat_sum); free(alg_sum);
    return nql_val_from_bignum(&y);
}
