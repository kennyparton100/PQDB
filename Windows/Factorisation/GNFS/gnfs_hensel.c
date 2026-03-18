/* ── Hensel lifting algebraic square root ──
 * Given S in Z[α]/(f), compute T s.t. T²=S using:
 *   1. Small prime p, brute-force sqrt(S) mod (f,p)
 *   2. Newton lift: T_{k+1} = T_k + (S-T_k²)·(2T_k)⁻¹ mod (f, p^(2^(k+1)))
 * Returns true if successful, with T_true coefficients in `result`. */
static bool gnfs_hensel_sqrt(const GNFSPoly* poly, const BigNum* n,
                              const uint64_t* hd, int hist_words,
                              const int64_t* rel_a, const uint64_t* rel_b, int rel_count,
                              uint64_t* out_y_alg, unsigned est_sqrt_bits) {
    /* Step 1: Find a small prime p where f is irreducible */
    uint64_t p = 0;
    for (uint64_t cand = 3; cand < 200; cand += 2) {
        if (!miller_rabin_u64(cand)) continue;
        if (cand == 2) continue;
        /* Check irreducibility: brute-force check all roots of f mod cand */
        bool has_root = false;
        for (uint64_t r = 0; r < cand && !has_root; ++r) {
            uint64_t val = 0, rpow = 1;
            for (int d = 0; d <= poly->degree; ++d) {
                val = (val + mulmod_u64(bn_mod_u64(&poly->coeffs[d], cand), rpow, cand)) % cand;
                rpow = mulmod_u64(rpow, r, cand);
            }
            if (val == 0) has_root = true;
        }
        if (has_root) continue;
        /* For degree 3: no root means irreducible (over F_p, a cubic without roots is irreducible) */
        if (cand % 4 == 3) { p = cand; break; } /* prefer p≡3 mod 4 for easy sqrt */
        if (p == 0) p = cand; /* fallback: any irreducible prime */
    }
    if (p == 0) { printf("  [HENSEL] no small irreducible prime found\n"); return false; }

    /* Step 2: Compute S = ∏(a_i - b_i·α) mod (f, p) */
    BigNum p_bn; bn_from_u64(&p_bn, p);
    BigNum f3i; bn_from_u64(&f3i, 1u); /* monic */
    AlgElem S; alg_one(&S);
    int dep_sz = 0;
    for (int rj = 0; rj < rel_count; ++rj) {
        if (!(hd[rj/64] & (1ull<<(rj%64)))) continue;
        ++dep_sz;
        AlgElem el; alg_from_ab(&el, rel_a[rj], rel_b[rj], &p_bn);
        AlgElem prev; for(int c=0;c<3;++c) bn_copy(&prev.c[c], &S.c[c]);
        alg_mul(&S, &prev, &el, poly, &f3i, &p_bn);
    }
    printf("  [HENSEL] p=%"PRIu64" dep_sz=%d S=[%"PRIu64",%"PRIu64",%"PRIu64"]\n",
        p, dep_sz, bn_to_u64(&S.c[0]), bn_to_u64(&S.c[1]), bn_to_u64(&S.c[2]));

    /* Step 3: Brute-force sqrt(S) mod (f, p) */
    AlgElem T0;
    bool found_sqrt = false;
    for (uint64_t c0 = 0; c0 < p && !found_sqrt; ++c0)
    for (uint64_t c1 = 0; c1 < p && !found_sqrt; ++c1)
    for (uint64_t c2 = 0; c2 < p && !found_sqrt; ++c2) {
        AlgElem trial; bn_from_u64(&trial.c[0],c0); bn_from_u64(&trial.c[1],c1); bn_from_u64(&trial.c[2],c2);
        AlgElem sq; alg_mul(&sq, &trial, &trial, poly, &f3i, &p_bn);
        if (bn_cmp(&sq.c[0],&S.c[0])==0 && bn_cmp(&sq.c[1],&S.c[1])==0 && bn_cmp(&sq.c[2],&S.c[2])==0) {
            for(int c=0;c<3;++c) bn_copy(&T0.c[c], &trial.c[c]);
            found_sqrt = true;
        }
    }
    if (!found_sqrt) { printf("  [HENSEL] no sqrt found mod (f,%"PRIu64") — not a square\n", p); return false; }
    printf("  [HENSEL] T0=[%"PRIu64",%"PRIu64",%"PRIu64"]\n", bn_to_u64(&T0.c[0]), bn_to_u64(&T0.c[1]), bn_to_u64(&T0.c[2]));

    /* Step 4: Hensel lift — Newton iterations doubling precision.
     * Compute initial inverse: inv0 = (2·T0)⁻¹ mod (f, p)  [small p, u64 OK]
     * Then at each step, lift BOTH T and inv using only alg_mul:
     *   T_{k+1} = T_k + (S - T_k²) · inv_k  mod (f, new_mod)
     *   inv_{k+1} = inv_k · (2 - 2·T_{k+1} · inv_k)  mod (f, new_mod) */
    unsigned target_bits = est_sqrt_bits + 20;
    unsigned cur_bits = 0; { uint64_t t=p; while(t){++cur_bits;t>>=1;} }
    int lifts_needed = 0;
    { unsigned b = cur_bits; while (b < target_bits) { b *= 2; ++lifts_needed; } }
    printf("  [HENSEL] target_bits=%u cur_bits=%u lifts_needed=%d\n", target_bits, cur_bits, lifts_needed);
    if (lifts_needed > 15) { printf("  [HENSEL] too many lifts needed\n"); return false; }

    /* Initial inverse: inv0 = (2·T0)⁻¹ mod (f, p)  [p is small, alg_inv OK] */
    AlgElem two_T0;
    for (int c=0;c<3;++c) {
        bn_copy(&two_T0.c[c], &T0.c[c]); bn_add(&two_T0.c[c], &T0.c[c]);
        if (bn_cmp(&two_T0.c[c], &p_bn) >= 0) bn_sub(&two_T0.c[c], &p_bn);
    }
    AlgElem inv_cur;
    if (!alg_inv(&inv_cur, &two_T0, poly, &f3i, &p_bn)) {
        printf("  [HENSEL] 2·T0 not invertible mod p=%"PRIu64"\n", p);
        return false;
    }

    BigNum cur_mod; bn_from_u64(&cur_mod, p);
    AlgElem T_cur; for(int c=0;c<3;++c) bn_copy(&T_cur.c[c], &T0.c[c]);

    for (int lift = 0; lift < lifts_needed; ++lift) {
        BigNum new_mod; bn_mul(&new_mod, &cur_mod, &cur_mod);
        printf("  [HENSEL] lift %d: mod_bits %u → %u\n", lift, bn_bitlen(&cur_mod), bn_bitlen(&new_mod));

        /* Recompute S mod new_mod (accumulate product of relations) */
        AlgElem S_new; alg_one(&S_new);
        for (int rj = 0; rj < rel_count; ++rj) {
            if (!(hd[rj/64] & (1ull<<(rj%64)))) continue;
            AlgElem el; alg_from_ab(&el, rel_a[rj], rel_b[rj], &new_mod);
            AlgElem prev; for(int c=0;c<3;++c) bn_copy(&prev.c[c], &S_new.c[c]);
            alg_mul(&S_new, &prev, &el, poly, &f3i, &new_mod);
        }

        /* residual = S - T² mod new_mod */
        AlgElem Tsq; alg_mul(&Tsq, &T_cur, &T_cur, poly, &f3i, &new_mod);
        AlgElem residual;
        for (int c=0;c<3;++c) {
            if (bn_cmp(&S_new.c[c], &Tsq.c[c]) >= 0) {
                bn_copy(&residual.c[c], &S_new.c[c]); bn_sub(&residual.c[c], &Tsq.c[c]);
            } else {
                bn_copy(&residual.c[c], &new_mod); bn_add(&residual.c[c], &S_new.c[c]); bn_sub(&residual.c[c], &Tsq.c[c]);
            }
        }

        /* correction = residual · inv_cur mod (f, new_mod) */
        AlgElem correction; alg_mul(&correction, &residual, &inv_cur, poly, &f3i, &new_mod);

        /* T_{k+1} = T_k + correction */
        for (int c=0;c<3;++c) {
            bn_add(&T_cur.c[c], &correction.c[c]);
            if (bn_cmp(&T_cur.c[c], &new_mod) >= 0) bn_sub(&T_cur.c[c], &new_mod);
        }

        /* Lift inverse: inv_{k+1} = inv_k · (2 - 2·T_{k+1} · inv_k) mod new_mod */
        AlgElem two_T_new;
        for (int c=0;c<3;++c) {
            bn_copy(&two_T_new.c[c], &T_cur.c[c]); bn_add(&two_T_new.c[c], &T_cur.c[c]);
            if (bn_cmp(&two_T_new.c[c], &new_mod) >= 0) bn_sub(&two_T_new.c[c], &new_mod);
        }
        AlgElem prod; alg_mul(&prod, &two_T_new, &inv_cur, poly, &f3i, &new_mod);
        /* two_minus_prod = 2 - prod */
        AlgElem two_minus;
        { BigNum two; bn_from_u64(&two, 2u);
          if (bn_cmp(&two, &prod.c[0]) >= 0) { bn_copy(&two_minus.c[0], &two); bn_sub(&two_minus.c[0], &prod.c[0]); }
          else { bn_copy(&two_minus.c[0], &new_mod); bn_add(&two_minus.c[0], &two); bn_sub(&two_minus.c[0], &prod.c[0]); }
          for (int c=1;c<3;++c) {
              if (bn_is_zero(&prod.c[c])) bn_zero(&two_minus.c[c]);
              else { bn_copy(&two_minus.c[c], &new_mod); bn_sub(&two_minus.c[c], &prod.c[c]); }
          }
        }
        AlgElem new_inv; alg_mul(&new_inv, &inv_cur, &two_minus, poly, &f3i, &new_mod);
        for(int c=0;c<3;++c) bn_copy(&inv_cur.c[c], &new_inv.c[c]);

        bn_copy(&cur_mod, &new_mod);
    }

    /* Step 5: Evaluate T(m) mod N directly from BigNum T_cur coefficients.
     * Centered lift: if coeff > M/2, it's negative → use N - (|coeff| mod N). */
    BigNum half; bn_copy(&half, &cur_mod);
    for(uint32_t li=0;li<half.used;++li){half.limbs[li]>>=1;if(li+1<half.used&&(half.limbs[li+1]&1))half.limbs[li]|=(1ull<<63);}
    bn_normalize(&half);
    uint64_t n_u = bn_to_u64(n);
    uint64_t m_u = bn_to_u64(&poly->m);
    uint64_t y_alg = 0, mpow = 1;
    for (int c = 0; c < 3; ++c) {
        bool neg = (bn_cmp(&T_cur.c[c], &half) > 0);
        BigNum abs_v;
        if (neg) { bn_copy(&abs_v, &cur_mod); bn_sub(&abs_v, &T_cur.c[c]); }
        else bn_copy(&abs_v, &T_cur.c[c]);
        uint64_t cv = bn_mod_u64(&abs_v, n_u);
        if (neg) cv = (n_u - cv) % n_u;
        uint64_t term = mulmod_u64(cv, mpow, n_u);
        y_alg = (y_alg + term) % n_u;
        if (c < 2) mpow = mulmod_u64(mpow, m_u, n_u);
        printf("  [HENSEL] coeff[%d]: %s%u bits, mod N = %"PRIu64"\n", c, neg?"-":"", bn_bitlen(&abs_v), cv);
    }
    *out_y_alg = y_alg;
    printf("  [HENSEL] y_alg = T(m) mod N = %"PRIu64"\n", y_alg);
    return true;
}
