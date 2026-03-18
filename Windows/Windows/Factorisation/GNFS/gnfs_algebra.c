/* ── Degree-3 algebraic element: c0 + c1*α + c2*α² in Z[α]/(f(α)) mod N ── */
typedef struct { BigNum c[3]; } AlgElem;

/** Multiply two algebraic elements mod (f(α), N). Degree-3 only.
 *  f(α) = f3·α³ + f2·α² + f1·α + f0 = 0  →  α³ = -(f2·α² + f1·α + f0) / f3 mod N
 *  We need f3_inv = f3^(-1) mod N. Passed as parameter for reuse. */
static void alg_mul(AlgElem* out, const AlgElem* a, const AlgElem* b,
                     const GNFSPoly* poly, const BigNum* f3_inv, const BigNum* n) {
    /* Product of (a0+a1α+a2α²)(b0+b1α+b2α²) = degree-4 polynomial */
    BigNum p[5]; /* p[0]..p[4] = coefficients of product */
    for (int i = 0; i < 5; ++i) bn_zero(&p[i]);

    /* Schoolbook multiply */
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            BigNum t; bn_mod_mul(&t, &a->c[i], &b->c[j], n);
            bn_add(&p[i + j], &t);
            if (bn_cmp(&p[i + j], n) >= 0) bn_sub(&p[i + j], n);
        }
    }

    /* Reduce: α³ = -f3_inv * (f2·α² + f1·α + f0) mod N
     * Process p[4] first (α⁴ = α·α³), then p[3] (α³). */
    /* Reduce p[4]: α⁴ = α·(-f3_inv*(f2α²+f1α+f0)) = -f3_inv*(f2α³+f1α²+f0α)
     * But α³ needs further reduction... For simplicity, reduce p[4] into p[0..3],
     * then reduce p[3] into p[0..2]. */

    /* p[4]*α⁴: substitute α³ = -f3_inv*(f2α²+f1α+f0), so
     * α⁴ = α·α³ = -f3_inv*(f2α³+f1α²+f0α)
     * But this still has α³. Let's do it iteratively: reduce highest degree first. */

    /* Reduce p[4] (coefficient of α⁴):
     * α⁴ = α·α³ = α·(-f3_inv·(f2α²+f1α+f0))
     *     = -f3_inv·(f2α³ + f1α² + f0α)
     * This introduces α³ again. Substitute again:
     * α³ = -f3_inv·(f2α²+f1α+f0)
     * So: α⁴ = -f3_inv·(-f3_inv·(f2α²+f1α+f0)·f2 + f1α² + f0α)
     * This gets messy. Cleaner: reduce p[4] into p[3], then reduce p[3]. */

    if (!bn_is_zero(&p[4])) {
        /* α⁴ = α·α³. Shift p[4] down: p[4]*α⁴ adds p[4]*(-f3_inv*fi) to lower degrees via α³ */
        /* Actually: p[4]*α⁴ means we have p[4] at position 4.
         * α⁴ = α·α³ = α·(-f3_inv·(f2α²+f1α+f0))
         * = -f3_inv·(f2·α³ + f1·α² + f0·α)
         * Substitute α³ again for the f2·α³ term:
         * = -f3_inv·(f2·(-f3_inv·(f2α²+f1α+f0)) + f1α² + f0α)
         * = -f3_inv·(-f3_inv·f2·(f2α²+f1α+f0) + f1α² + f0α)
         *
         * This is getting complex. Simpler: do two reductions.
         * First pass: reduce p[4] into p[0..3] by treating it as extra α³·α.
         * p[3] += p[4] contribution... Actually let's just reduce iteratively. */

        /* Reduce α⁴ → α¹·α³ = α·(-f3_inv*(f2α²+f1α+f0)):
         * Adds to: p[3] += p[4]*(-f3_inv*f2), p[2] += p[4]*(-f3_inv*f1), p[1] += p[4]*(-f3_inv*f0) */
        BigNum neg_f3_inv; bn_copy(&neg_f3_inv, n); bn_sub(&neg_f3_inv, f3_inv);
        for (int k = 0; k < 3; ++k) {
            BigNum coeff; bn_mod_mul(&coeff, &neg_f3_inv, &poly->coeffs[k], n);
            BigNum contrib; bn_mod_mul(&contrib, &p[4], &coeff, n);
            bn_add(&p[k + 1], &contrib);
            if (bn_cmp(&p[k + 1], n) >= 0) bn_sub(&p[k + 1], n);
        }
        bn_zero(&p[4]);
    }

    /* Reduce p[3] (coefficient of α³):
     * α³ = -f3_inv*(f2α²+f1α+f0)
     * p[k] += p[3]*(-f3_inv*coeffs[k]) for k=0,1,2 */
    if (!bn_is_zero(&p[3])) {
        BigNum neg_f3_inv; bn_copy(&neg_f3_inv, n); bn_sub(&neg_f3_inv, f3_inv);
        for (int k = 0; k < 3; ++k) {
            BigNum coeff; bn_mod_mul(&coeff, &neg_f3_inv, &poly->coeffs[k], n);
            BigNum contrib; bn_mod_mul(&contrib, &p[3], &coeff, n);
            bn_add(&p[k], &contrib);
            if (bn_cmp(&p[k], n) >= 0) bn_sub(&p[k], n);
        }
    }

    for (int i = 0; i < 3; ++i) bn_copy(&out->c[i], &p[i]);
}

/** Evaluate algebraic element at α = m: result = c0 + c1*m + c2*m² mod N. */
static void alg_eval_at_m(BigNum* result, const AlgElem* a, const BigNum* m, const BigNum* n) {
    BigNum r; bn_copy(&r, &a->c[0]);
    BigNum mpow; bn_copy(&mpow, m);
    for (int i = 1; i < 3; ++i) {
        BigNum term; bn_mod_mul(&term, &a->c[i], &mpow, n);
        bn_add(&r, &term);
        if (bn_cmp(&r, n) >= 0) bn_sub(&r, n);
        if (i < 2) bn_mod_mul(&mpow, &mpow, m, n);
    }
    bn_copy(result, &r);
}

/** Set algebraic element to (a - b*α) mod N. */
static void alg_from_ab(AlgElem* out, int64_t a, uint64_t b, const BigNum* n) {
    /* c0 = a mod N, c1 = -b mod N, c2 = 0 */
    if (a >= 0) { bn_from_u64(&out->c[0], (uint64_t)a); }
    else { bn_copy(&out->c[0], n); BigNum t; bn_from_u64(&t, (uint64_t)(-a)); bn_sub(&out->c[0], &t); }
    /* c1 = N - b (i.e., -b mod N) */
    bn_copy(&out->c[1], n);
    { BigNum t; bn_from_u64(&t, b); bn_sub(&out->c[1], &t); }
    bn_zero(&out->c[2]);
}

/** Set algebraic element to 1. */
static void alg_one(AlgElem* out) {
    bn_from_u64(&out->c[0], 1u);
    bn_zero(&out->c[1]);
    bn_zero(&out->c[2]);
}

/* ── Milestone 2b-size: Exact integer algebraic elements in Z[α]/(f) ──
 * Coefficients are true BigNum integers (not reduced mod N or p).
 * For non-monic f: each α³ reduction multiplies lower coeffs by f3 to stay
 * in integers. denom tracks the accumulated f3^k denominator.
 * True algebraic element = (c0 + c1·α + c2·α²) / denom in Q(α). */
typedef struct {
    BigNum c[3];   /* integer numerator coefficients */
    BigNum denom;  /* denominator (f3^reductions for non-monic; 1 for monic) */
    int reductions; /* number of f3 reductions applied */
} GNFSAlgExact;

static void gnfs_alg_exact_one(GNFSAlgExact* out) {
    bn_from_u64(&out->c[0], 1u); bn_zero(&out->c[1]); bn_zero(&out->c[2]);
    bn_from_u64(&out->denom, 1u); out->reductions = 0;
}

static void gnfs_alg_exact_from_ab(GNFSAlgExact* out, int64_t a, uint64_t b) {
    /* (a - b·α) = a + (-b)·α + 0·α² with denom=1.
     * We store |coefficients| and track signs via a sign array.
     * For exact integer replay, we avoid signed BigNum by working with
     * the product mod a large prime instead. See gnfs_alg_exact_mul_modp. */
    bn_from_u64(&out->c[0], (a >= 0) ? (uint64_t)a : (uint64_t)(-a));
    bn_from_u64(&out->c[1], b);
    bn_zero(&out->c[2]);
    bn_from_u64(&out->denom, 1u); out->reductions = 0;
}

/** Exact integer multiply in Z[α]/(f) mod a large prime p.
 *  This gives the true algebraic product reduced mod p, with exact
 *  denominator tracking for non-monic polynomials.
 *  For monic f (f3=1): no denominator growth, pure modular arithmetic.
 *  For non-monic f: each degree reduction multiplies all lower coeffs by f3
 *  and adds f3 to the denominator product. */
static void gnfs_alg_exact_mul_modp(GNFSAlgExact* out,
    const GNFSAlgExact* a, const GNFSAlgExact* b,
    const GNFSPoly* poly, const BigNum* f3_inv, const BigNum* p) {
    /* Schoolbook multiply numerators mod p */
    BigNum pr[5]; for (int i=0;i<5;++i) bn_zero(&pr[i]);
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        BigNum t; bn_mod_mul(&t, &a->c[i], &b->c[j], p);
        bn_add(&pr[i+j], &t);
        if (bn_cmp(&pr[i+j], p) >= 0) bn_sub(&pr[i+j], p);
    }
    /* New denom = a.denom * b.denom mod p */
    BigNum nd; bn_mod_mul(&nd, &a->denom, &b->denom, p);
    int nred = a->reductions + b->reductions;
    /* Reduce using α³ = -(f2α²+f1α+f0)/f3.
     * For monic (f3=1, f3_inv=1): just substitute.
     * For non-monic: multiply lower coeffs by f3 before substitution,
     * or equivalently use f3_inv mod p (valid since p is prime). */
    BigNum f3_val; bn_copy(&f3_val, &poly->coeffs[3]);
    bool is_monic = bn_is_one(&f3_val);
    /* Reduce p[4] */
    if (!bn_is_zero(&pr[4])) {
        if (!is_monic) {
            for (int k=0;k<=3;++k) bn_mod_mul(&pr[k],&pr[k],&f3_val,p);
            bn_mod_mul(&nd,&nd,&f3_val,p); ++nred;
        }
        BigNum neg_p4; bn_copy(&neg_p4,p); bn_sub(&neg_p4,&pr[4]);
        for (int k=0;k<3;++k) {
            BigNum coeff; bn_mod_mul(&coeff,&neg_p4,&poly->coeffs[k],p);
            bn_add(&pr[k+1],&coeff);
            if(bn_cmp(&pr[k+1],p)>=0) bn_sub(&pr[k+1],p);
        }
        bn_zero(&pr[4]);
    }
    /* Reduce p[3] */
    if (!bn_is_zero(&pr[3])) {
        if (!is_monic) {
            for (int k=0;k<=2;++k) bn_mod_mul(&pr[k],&pr[k],&f3_val,p);
            bn_mod_mul(&nd,&nd,&f3_val,p); ++nred;
        }
        BigNum neg_p3; bn_copy(&neg_p3,p); bn_sub(&neg_p3,&pr[3]);
        for (int k=0;k<3;++k) {
            BigNum coeff; bn_mod_mul(&coeff,&neg_p3,&poly->coeffs[k],p);
            bn_add(&pr[k],&coeff);
            if(bn_cmp(&pr[k],p)>=0) bn_sub(&pr[k],p);
        }
    }
    for (int i=0;i<3;++i) bn_copy(&out->c[i],&pr[i]);
    bn_copy(&out->denom, &nd);
    out->reductions = nred;
}
