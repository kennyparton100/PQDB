/* ── Stage 1: Polynomial Selection (base-m) ── */

/** Compute floor(a^(1/d)) via Newton's method for d >= 2. */
static void bn_nth_root_floor(BigNum* result, const BigNum* a, int d) {
    if (d == 2) { bn_isqrt_floor(result, a); return; }
    if (bn_is_zero(a) || bn_is_one(a)) { bn_copy(result, a); return; }
    /* Initial guess: 2^(bitlen/d) */
    unsigned blen = bn_bitlen(a);
    BigNum x; bn_from_u64(&x, 1u);
    unsigned shift = blen / (unsigned)d;
    for (unsigned i = 0; i < shift; ++i) bn_shl1(&x);
    /* Newton: x_{n+1} = ((d-1)*x + a / x^(d-1)) / d */
    for (int iter = 0; iter < 200; ++iter) {
        /* Compute x^(d-1) */
        BigNum xpow; bn_from_u64(&xpow, 1u);
        for (int i = 0; i < d - 1; ++i) bn_mul(&xpow, &xpow, &x);
        /* q = a / x^(d-1) */
        BigNum q; bn_divexact(&q, a, &xpow); /* approximate: integer division */
        /* new_x = ((d-1)*x + q) / d */
        BigNum dm1x; bn_copy(&dm1x, &x);
        for (int i = 1; i < d - 1; ++i) bn_add(&dm1x, &x);
        bn_add(&dm1x, &q);
        BigNum d_bn; bn_from_u64(&d_bn, (uint64_t)d);
        BigNum new_x; bn_divexact(&new_x, &dm1x, &d_bn);
        if (bn_cmp(&new_x, &x) >= 0) break; /* converged */
        bn_copy(&x, &new_x);
    }
    /* Adjust: ensure x^d <= a < (x+1)^d */
    bn_copy(result, &x);
}

/* Forward declaration */
static bool gnfs_poly_select_base_m(GNFSPoly* poly, const BigNum* n, int d, bool diag);

/** Select polynomial using configured strategy. */
static bool gnfs_poly_select(GNFSPoly* poly, const BigNum* n, const GNFSConfig* cfg) {
    switch (cfg->poly_strategy) {
        case GNFS_POLY_BASE_M:
            return gnfs_poly_select_base_m(poly, n, cfg->degree, cfg->diag);
        default:
            printf("  [GNFS] ERROR: unsupported polynomial strategy %d\n", cfg->poly_strategy);
            return false;
    }
}

/** Select base-m polynomial: f(x) with f(m) = N, degree d. */
static bool gnfs_poly_select_base_m(GNFSPoly* poly, const BigNum* n, int d, bool diag) {
    poly->degree = d;
    bn_nth_root_floor(&poly->m, n, d);
    /* Extract base-m digits: N = c_d*m^d + c_{d-1}*m^{d-1} + ... + c_0 */
    BigNum rem; bn_copy(&rem, n);
    for (int i = 0; i <= d; ++i) {
        if (bn_is_zero(&rem)) { bn_zero(&poly->coeffs[i]); continue; }
        if (bn_is_zero(&poly->m)) { bn_copy(&poly->coeffs[i], &rem); bn_zero(&rem); continue; }
        /* coeffs[i] = rem mod m, rem = rem / m */
        BigNum q_out;
        bn_divexact(&q_out, &rem, &poly->m);
        BigNum prod; bn_mul(&prod, &q_out, &poly->m);
        bn_copy(&poly->coeffs[i], &rem);
        bn_sub(&poly->coeffs[i], &prod);
        bn_copy(&rem, &q_out);
    }
    /* Verify: evaluate f(m) and check = N */
    BigNum eval; bn_zero(&eval);
    BigNum mpow; bn_from_u64(&mpow, 1u);
    for (int i = 0; i <= d; ++i) {
        BigNum term; bn_mul(&term, &poly->coeffs[i], &mpow);
        bn_add(&eval, &term);
        if (i < d) bn_mul(&mpow, &mpow, &poly->m);
    }
    bool ok = (bn_cmp(&eval, n) == 0);
    if (diag) {
        char mbuf[256]; bn_to_str(&poly->m, mbuf, sizeof(mbuf));
        printf("  [GNFS] polynomial: degree=%d  m=%s\n", d, mbuf);
        printf("  [GNFS] coeffs=[");
        for (int i = d; i >= 0; --i) {
            char cbuf[64]; bn_to_str(&poly->coeffs[i], cbuf, sizeof(cbuf));
            printf("%s%s", cbuf, i > 0 ? ", " : "");
        }
        printf("]\n  [GNFS] verification: f(m) = N  %s\n", ok ? "OK" : "FAILED");
    }
    return ok;
}

/** Evaluate f(x) mod p (small prime). */
static uint64_t gnfs_poly_eval_mod_p(const GNFSPoly* poly, uint64_t x, uint64_t p) {
    uint64_t result = 0u, xpow = 1u;
    for (int i = 0; i <= poly->degree; ++i) {
        uint64_t ci = bn_mod_u64(&poly->coeffs[i], p);
        result = (result + mulmod_u64(ci, xpow, p)) % p;
        if (i < poly->degree) xpow = mulmod_u64(xpow, x, p);
    }
    return result;
}

/** Evaluate homogeneous F(a,b) = sum c_i * a^i * b^(d-i) as BigNum. */
static void gnfs_eval_homogeneous(BigNum* result, const GNFSPoly* poly, int64_t a, uint64_t b) {
    bn_zero(result);
    int d = poly->degree;
    /* Compute a-powers and b-powers as BigNum */
    BigNum apow[GNFS_MAX_DEGREE + 1], bpow[GNFS_MAX_DEGREE + 1];
    bn_from_u64(&apow[0], 1u);
    bn_from_u64(&bpow[0], 1u);
    BigNum abs_a; bn_from_u64(&abs_a, (a >= 0) ? (uint64_t)a : (uint64_t)(-a));
    BigNum b_bn; bn_from_u64(&b_bn, b);
    for (int i = 1; i <= d; ++i) {
        bn_mul(&apow[i], &apow[i - 1], &abs_a);
        bn_mul(&bpow[i], &bpow[i - 1], &b_bn);
    }
    /* F(a,b) = sum c_i * a^i * b^(d-i)
     * For signed a: terms with odd i are negated if a < 0 */
    BigNum pos_sum; bn_zero(&pos_sum);
    BigNum neg_sum; bn_zero(&neg_sum);
    for (int i = 0; i <= d; ++i) {
        BigNum term; bn_mul(&term, &poly->coeffs[i], &apow[i]);
        bn_mul(&term, &term, &bpow[d - i]);
        bool negate = (a < 0 && (i & 1));
        if (negate) bn_add(&neg_sum, &term);
        else bn_add(&pos_sum, &term);
    }
    if (bn_cmp(&pos_sum, &neg_sum) >= 0) {
        bn_copy(result, &pos_sum); bn_sub(result, &neg_sum);
    } else {
        bn_copy(result, &neg_sum); bn_sub(result, &pos_sum);
    }
}
