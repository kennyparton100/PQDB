/** Compute (p^3 - 1) / 2 as BigNum. QR test exponent in F_{p^3}. */
static void gnfs_qr_exp_from_prime(BigNum* out, uint64_t p) {
    BigNum pb; bn_from_u64(&pb, p);
    BigNum p2; bn_mul(&p2, &pb, &pb);
    BigNum p3; bn_mul(&p3, &p2, &pb);
    BigNum one; bn_from_u64(&one, 1u);
    bn_sub(&p3, &one);
    for (uint32_t li = 0; li < p3.used; ++li) {
        p3.limbs[li] >>= 1;
        if (li + 1 < p3.used && (p3.limbs[li + 1] & 1))
            p3.limbs[li] |= ((uint64_t)1 << 63);
    }
    bn_normalize(&p3);
    bn_copy(out, &p3);
}

/** Compute (p^3 + 1) / 4 as BigNum. Sqrt exponent in F_{p^3}, p ≡ 3 (mod 4). */
static void gnfs_sqrt_exp_from_prime(BigNum* out, uint64_t p) {
    BigNum pb; bn_from_u64(&pb, p);
    BigNum p2; bn_mul(&p2, &pb, &pb);
    BigNum p3; bn_mul(&p3, &p2, &pb);
    BigNum one; bn_from_u64(&one, 1u);
    bn_add(&p3, &one);
    for (int shift = 0; shift < 2; ++shift) {
        for (uint32_t li = 0; li < p3.used; ++li) {
            p3.limbs[li] >>= 1;
            if (li + 1 < p3.used && (p3.limbs[li + 1] & 1))
                p3.limbs[li] |= ((uint64_t)1 << 63);
        }
        bn_normalize(&p3);
    }
    bn_copy(out, &p3);
}

/* ── Polynomial inversion mod (f, M): find B s.t. A*B ≡ 1 mod (f, M) ──
 * Uses the 3×3 matrix representation of multiplication by A and Gaussian elimination.
 * Returns false if A is not invertible mod (f, M).
 * HARD RULE: mod MUST fit in u64 (i.e., mod->used == 1). This function is only
 * valid at the base-prime modulus. Any call at larger moduli is a bug — use
 * Newton inverse lifting instead (see gnfs_hensel.c). */
static bool alg_inv(AlgElem* out, const AlgElem* a, const GNFSPoly* poly, const BigNum* f3_inv, const BigNum* mod) {
    if (mod->used > 1) {
        fprintf(stderr, "BUG: alg_inv called with %u-limb modulus (%u bits). Only valid for base-prime (1 limb).\n",
            mod->used, bn_bitlen(mod));
        return false;
    }
    /* Build 3×3 matrix [A | I] where A is the multiplication-by-a matrix.
     * Column j of A = coefficients of a * α^j mod (f, mod). */
    BigNum mat[3][6]; /* augmented [A | I] */
    for (int i=0;i<3;++i) for(int j=0;j<6;++j) bn_zero(&mat[i][j]);
    /* Column 0: a * 1 = a */
    for (int i=0;i<3;++i) bn_copy(&mat[i][0], &a->c[i]);
    /* Column 1: a * α */
    { AlgElem alpha; bn_zero(&alpha.c[0]); bn_from_u64(&alpha.c[1],1); bn_zero(&alpha.c[2]);
      AlgElem prod; alg_mul(&prod, a, &alpha, poly, f3_inv, mod);
      for (int i=0;i<3;++i) bn_copy(&mat[i][1], &prod.c[i]); }
    /* Column 2: a * α² */
    { AlgElem alpha2; bn_zero(&alpha2.c[0]); bn_zero(&alpha2.c[1]); bn_from_u64(&alpha2.c[2],1);
      AlgElem prod; alg_mul(&prod, a, &alpha2, poly, f3_inv, mod);
      for (int i=0;i<3;++i) bn_copy(&mat[i][2], &prod.c[i]); }
    /* Identity on right */
    for (int i=0;i<3;++i) bn_from_u64(&mat[i][3+i], 1);
    /* Gaussian elimination mod `mod` */
    for (int col=0;col<3;++col) {
        /* Find pivot */
        int pivot = -1;
        for (int row=col;row<3;++row) if (!bn_is_zero(&mat[row][col])) { pivot=row; break; }
        if (pivot < 0) return false; /* not invertible */
        if (pivot != col) for(int j=0;j<6;++j){BigNum t;bn_copy(&t,&mat[col][j]);bn_copy(&mat[col][j],&mat[pivot][j]);bn_copy(&mat[pivot][j],&t);}
        /* Compute pivot inverse mod `mod` (using powmod for prime-power moduli) */
        uint64_t piv_u = bn_mod_u64(&mat[col][col], bn_to_u64(mod));
        uint64_t mod_u = bn_to_u64(mod);
        /* For prime-power mod, use extended GCD for inverse */
        /* Simplified: if mod fits u64, use powmod with Euler totient... 
         * For small primes p, mod=p^e, totient=p^(e-1)*(p-1). But we don't know p here.
         * Use extended GCD instead. */
        int64_t g, x_eg, y_eg;
        { int64_t a_eg=(int64_t)piv_u, b_eg=(int64_t)mod_u, x0=1, x1=0, y0=0, y1=1;
          while(b_eg){int64_t q=a_eg/b_eg,t=b_eg;b_eg=a_eg-q*b_eg;a_eg=t;t=x1;x1=x0-q*x1;x0=t;t=y1;y1=y0-q*y1;y0=t;}
          g=a_eg; x_eg=x0; y_eg=y0; }
        if (g != 1) return false; /* not invertible */
        uint64_t piv_inv = ((x_eg % (int64_t)mod_u) + (int64_t)mod_u) % (int64_t)mod_u;
        /* Scale pivot row */
        for (int j=0;j<6;++j) {
            uint64_t v = bn_mod_u64(&mat[col][j], mod_u);
            bn_from_u64(&mat[col][j], mulmod_u64(v, piv_inv, mod_u));
        }
        /* Eliminate other rows */
        for (int row=0;row<3;++row) {
            if (row==col) continue;
            uint64_t fac = bn_mod_u64(&mat[row][col], mod_u);
            if (fac==0) continue;
            for (int j=0;j<6;++j) {
                uint64_t rj = bn_mod_u64(&mat[row][j], mod_u);
                uint64_t cj = bn_mod_u64(&mat[col][j], mod_u);
                uint64_t sub = mulmod_u64(fac, cj, mod_u);
                bn_from_u64(&mat[row][j], (rj + mod_u - sub) % mod_u);
            }
        }
    }
    /* Result is in columns 3-5 */
    for (int i=0;i<3;++i) bn_copy(&out->c[i], &mat[i][3]);
    return true;
}
