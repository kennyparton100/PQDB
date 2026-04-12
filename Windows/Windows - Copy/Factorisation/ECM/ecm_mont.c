/* ══════════════════════════════════════════════════════════════════════
 * MONTGOMERY ECM (Pass A: Stage 1 only)
 *
 * Montgomery curve By² = x³ + Ax² + x with projective (X:Z) coordinates.
 * Suyama parametrization guarantees group order divisible by 12.
 * X-only differential ladder: zero modular inverses during scalar multiply.
 * All arithmetic via bn_mont_mul (Montgomery multiplication).
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    BigNum X;
    BigNum Z;
} ECMMontPoint;

/** xDBL: double P on Montgomery curve with parameter a24 = (A+2)/4.
 *  All values in Montgomery form. No modular inverse needed.
 *  Cost: 4 bn_mont_mul + 2 add/sub. */
static void ecm_mont_xdbl(ECMMontPoint* out, const ECMMontPoint* P,
                            const BigNum* a24_mont, const MontCtx* ctx) {
    BigNum t1, t2, t3, t4;
    /* t1 = X + Z, t2 = X - Z */
    bn_copy(&t1, &P->X); bn_add(&t1, &P->Z);
    if (bn_cmp(&t1, &ctx->n) >= 0) bn_sub(&t1, &ctx->n);
    bn_copy(&t2, &P->X);
    if (bn_cmp(&t2, &P->Z) >= 0) bn_sub(&t2, &P->Z);
    else { bn_add(&t2, &ctx->n); bn_sub(&t2, &P->Z); }
    /* t1 = t1², t2 = t2² */
    bn_mont_mul(&t3, &t1, &t1, ctx); /* t3 = (X+Z)² */
    bn_mont_mul(&t4, &t2, &t2, ctx); /* t4 = (X-Z)² */
    /* X' = t3 * t4 = (X+Z)² * (X-Z)² */
    bn_mont_mul(&out->X, &t3, &t4, ctx);
    /* t1 = t3 - t4 = (X+Z)² - (X-Z)² = 4XZ */
    bn_copy(&t1, &t3);
    if (bn_cmp(&t1, &t4) >= 0) bn_sub(&t1, &t4);
    else { bn_add(&t1, &ctx->n); bn_sub(&t1, &t4); }
    /* t2 = t4 + a24 * t1 = (X-Z)² + a24 * 4XZ */
    bn_mont_mul(&t2, a24_mont, &t1, ctx);
    bn_add(&t2, &t4);
    if (bn_cmp(&t2, &ctx->n) >= 0) bn_sub(&t2, &ctx->n);
    /* Z' = t1 * t2 = 4XZ * ((X-Z)² + a24*4XZ) */
    bn_mont_mul(&out->Z, &t1, &t2, ctx);
}

/** xADD: differential addition P+Q given P, Q, and P-Q.
 *  All values in Montgomery form. No modular inverse needed.
 *  Cost: 4 bn_mont_mul + 2 add/sub. */
static void ecm_mont_xadd(ECMMontPoint* out, const ECMMontPoint* P,
                            const ECMMontPoint* Q, const ECMMontPoint* diff,
                            const MontCtx* ctx) {
    BigNum u, v, t1, t2;
    /* u = (X_P - Z_P)(X_Q + Z_Q) */
    bn_copy(&t1, &P->X);
    if (bn_cmp(&t1, &P->Z) >= 0) bn_sub(&t1, &P->Z);
    else { bn_add(&t1, &ctx->n); bn_sub(&t1, &P->Z); }
    bn_copy(&t2, &Q->X); bn_add(&t2, &Q->Z);
    if (bn_cmp(&t2, &ctx->n) >= 0) bn_sub(&t2, &ctx->n);
    bn_mont_mul(&u, &t1, &t2, ctx);
    /* v = (X_P + Z_P)(X_Q - Z_Q) */
    bn_copy(&t1, &P->X); bn_add(&t1, &P->Z);
    if (bn_cmp(&t1, &ctx->n) >= 0) bn_sub(&t1, &ctx->n);
    bn_copy(&t2, &Q->X);
    if (bn_cmp(&t2, &Q->Z) >= 0) bn_sub(&t2, &Q->Z);
    else { bn_add(&t2, &ctx->n); bn_sub(&t2, &Q->Z); }
    bn_mont_mul(&v, &t1, &t2, ctx);
    /* X' = Z_{P-Q} * (u + v)² */
    bn_copy(&t1, &u); bn_add(&t1, &v);
    if (bn_cmp(&t1, &ctx->n) >= 0) bn_sub(&t1, &ctx->n);
    bn_mont_mul(&t2, &t1, &t1, ctx); /* (u+v)² */
    bn_mont_mul(&out->X, &diff->Z, &t2, ctx);
    /* Z' = X_{P-Q} * (u - v)² */
    bn_copy(&t1, &u);
    if (bn_cmp(&t1, &v) >= 0) bn_sub(&t1, &v);
    else { bn_add(&t1, &ctx->n); bn_sub(&t1, &v); }
    bn_mont_mul(&t2, &t1, &t1, ctx); /* (u-v)² */
    bn_mont_mul(&out->Z, &diff->X, &t2, ctx);
}

/** Montgomery ladder: compute [k]P using the x-only differential chain.
 *  Cost: exactly log2(k) xDBL + log2(k) xADD = 8M per bit. */
static void ecm_mont_ladder(ECMMontPoint* result, const ECMMontPoint* P,
                              uint64_t k, const BigNum* a24_mont, const MontCtx* ctx) {
    if (k == 0u) { bn_from_u64(&result->X, 0u); bn_from_u64(&result->Z, 0u); return; }
    if (k == 1u) { *result = *P; return; }

    ECMMontPoint R0 = *P;
    ECMMontPoint R1;
    ecm_mont_xdbl(&R1, P, a24_mont, ctx);

    uint64_t mask = (uint64_t)1u << 62u;
    while (mask > 0u && !(k & mask)) mask >>= 1u;
    mask >>= 1u; /* skip leading 1 bit */

    while (mask > 0u) {
        if (k & mask) {
            ECMMontPoint tmp;
            ecm_mont_xadd(&tmp, &R0, &R1, P, ctx);
            R0 = tmp;
            ecm_mont_xdbl(&tmp, &R1, a24_mont, ctx);
            R1 = tmp;
        } else {
            ECMMontPoint tmp;
            ecm_mont_xadd(&tmp, &R0, &R1, P, ctx);
            R1 = tmp;
            ecm_mont_xdbl(&tmp, &R0, a24_mont, ctx);
            R0 = tmp;
        }
        mask >>= 1u;
    }

    *result = R0;
}

/** Wrapper preserving old call signature (Stage 1 only, default B2). */
static FactorResultBigNum cpss_factor_ecm_mont_stage1(const BigNum* n, uint64_t B1,
                                                        uint64_t curves, uint64_t seed) {
    return cpss_factor_ecm_mont(n, B1, 0u, curves, seed);
}

/** ECM for uint64 — promotes to BigNum, runs Montgomery ECM, converts result back. */
static FactorResult cpss_factor_ecm_u64(uint64_t n, uint64_t B1, uint64_t curves, uint64_t seed) {
    FactorResult fr;
    factor_result_init(&fr, n, "ecm-mont");
    BigNum bn; bn_from_u64(&bn, n);
    FactorResultBigNum frbn = cpss_factor_ecm_mont(&bn, B1, 0u, curves, seed);
    fr.method = frbn.method;
    fr.status = frbn.status;
    fr.time_seconds = frbn.time_seconds;
    fr.fully_factored = frbn.fully_factored;
    if (frbn.factor_found && bn_fits_u64(&frbn.factor)) {
        uint64_t f = bn_to_u64(&frbn.factor);
        if (f > 1u && f < n) {
            factor_result_set_split(&fr, f, n / f);
        }
    }
    return fr;
}

/** ECM for U128 — promotes to BigNum, runs Montgomery ECM, converts result back. */
static FactorResultU128 cpss_factor_ecm_u128(U128 n, uint64_t B1, uint64_t curves, uint64_t seed) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "ecm-mont");
    /* Build BigNum from U128 */
    BigNum bn;
    bn_from_u64(&bn, n.lo);
    if (n.hi != 0u) {
        BigNum hi; bn_from_u64(&hi, n.hi);
        for (int sh = 0; sh < 64; ++sh) bn_shl1(&hi);
        bn_add(&bn, &hi);
    }
    FactorResultBigNum frbn = cpss_factor_ecm_mont(&bn, B1, 0u, curves, seed);
    fr.method = frbn.method;
    fr.status = frbn.status;
    fr.time_seconds = frbn.time_seconds;
    fr.fully_factored = frbn.fully_factored;
    if (frbn.factor_found && bn_fits_u64(&frbn.factor)) {
        uint64_t f = bn_to_u64(&frbn.factor);
        if (f > 1u) {
            factor_result_u128_add(&fr, f);
            fr.cofactor = u128_div(n, u128_from_u64(f));
            if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                factor_result_u128_add(&fr, fr.cofactor.lo);
                fr.cofactor = u128_from_u64(1u);
                fr.fully_factored = true;
            }
        }
    }
    return fr;
}

/**
 * Montgomery ECM with Stage 1 + Stage 2 BSGS.
 * Suyama parametrization + x-only Montgomery ladder.
 * Stage 1: prime powers up to B1 via Montgomery ladder.
 * Stage 2: baby-step/giant-step over primes in (B1, B2].
 * Factor detected via Z-coordinate accumulation + periodic GCD.
 */
static FactorResultBigNum cpss_factor_ecm_mont(const BigNum* n, uint64_t B1, uint64_t B2,
                                                 uint64_t curves, uint64_t seed) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "ecm-mont";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();
    B1 = ecm_default_B1(B1);
    B2 = ecm_default_B2(B2, B1);
    curves = ecm_default_curves(curves);
    seed = ecm_default_seed(seed);

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr;
    }
    if (bn_is_even(n)) {
        BigNum two; bn_from_u64(&two, 2u);
        factor_result_bn_set_from_factor(&fr, n, &two);
        fr.time_seconds = now_sec() - t0; return fr;
    }
    if (bn_factor_is_prime(n)) {
        fr.cofactor_is_prime = true; fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0; return fr;
    }

    /* Montgomery setup (n must be odd — guaranteed by even check above) */
    MontCtx ctx;
    if (!bn_montgomery_setup(&ctx, n)) {
        fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr;
    }

    for (uint64_t curve = 0u; curve < curves; ++curve) {
        /* ── Suyama parametrization ──
         * σ ∈ [6, N-1], u = σ²-5, v = 4σ
         * Q₀ = (u³ : v³)  in projective coords (no inverse needed for point)
         * a24 = (v-u)³·(3u+v) / (16·u³·v)  [needs one inverse → GCD check] */
        BigNum sigma;
        bn_random_below(&sigma, n, &seed);
        /* Ensure σ >= 6 */
        if (bn_fits_u64(&sigma) && bn_to_u64(&sigma) < 6u) bn_from_u64(&sigma, 6u + (seed & 0xFFu));

        BigNum u, v;
        bn_mod_mul(&u, &sigma, &sigma, n);             /* u = σ² mod N */
        BigNum five; bn_from_u64(&five, 5u);
        if (bn_cmp(&u, &five) >= 0) bn_sub(&u, &five); /* u = σ²-5 mod N */
        else { bn_add(&u, n); bn_sub(&u, &five); }

        bn_copy(&v, &sigma); bn_shl1(&v); bn_shl1(&v); /* v = 4σ */
        if (bn_cmp(&v, n) >= 0) { BigNum tmp; bn_copy(&tmp, &v); bn_sub(&tmp, n); bn_copy(&v, &tmp); }

        /* u3 = u³, v3 = v³ */
        BigNum u2, u3, v3;
        bn_mod_mul(&u2, &u, &u, n);
        bn_mod_mul(&u3, &u2, &u, n);
        BigNum v2; bn_mod_mul(&v2, &v, &v, n);
        bn_mod_mul(&v3, &v2, &v, n);

        /* Q₀ = (u³ : v³) in standard coords; convert to Montgomery form */
        ECMMontPoint Q0;
        bn_to_mont(&Q0.X, &u3, &ctx);
        bn_to_mont(&Q0.Z, &v3, &ctx);

        /* a24 = (v-u)³·(3u+v) / (16·u³·v) mod N
         * Denominator = 16·u³·v. Compute inverse, check GCD. */
        BigNum vu_diff;
        if (bn_cmp(&v, &u) >= 0) { bn_copy(&vu_diff, &v); bn_sub(&vu_diff, &u); }
        else { bn_copy(&vu_diff, &v); bn_add(&vu_diff, n); bn_sub(&vu_diff, &u); }
        BigNum vu3; /* (v-u)³ */
        bn_mod_mul(&u2, &vu_diff, &vu_diff, n); /* reuse u2 as temp */
        bn_mod_mul(&vu3, &u2, &vu_diff, n);
        BigNum three_u; bn_copy(&three_u, &u); bn_add(&three_u, &u);
        if (bn_cmp(&three_u, n) >= 0) bn_sub(&three_u, n);
        bn_add(&three_u, &u);
        if (bn_cmp(&three_u, n) >= 0) bn_sub(&three_u, n);
        BigNum three_u_plus_v; bn_copy(&three_u_plus_v, &three_u); bn_add(&three_u_plus_v, &v);
        if (bn_cmp(&three_u_plus_v, n) >= 0) bn_sub(&three_u_plus_v, n);
        BigNum numer;
        bn_mod_mul(&numer, &vu3, &three_u_plus_v, n); /* (v-u)³·(3u+v) */

        BigNum sixteen; bn_from_u64(&sixteen, 16u);
        BigNum denom;
        bn_mod_mul(&denom, &u3, &v, n);     /* u³·v */
        bn_mod_mul(&denom, &denom, &sixteen, n); /* 16·u³·v */

        BigNum factor_found;
        bn_zero(&factor_found);
        BigNum denom_inv;
        if (!bn_mod_inverse_checked(&denom_inv, &denom, n, &factor_found)) {
            if (bn_factor_candidate_valid(&factor_found, n)) {
                factor_result_bn_set_from_factor(&fr, n, &factor_found);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            continue; /* degenerate curve, try next */
        }

        BigNum a24;
        bn_mod_mul(&a24, &numer, &denom_inv, n);

        /* Convert a24 to Montgomery form */
        BigNum a24_mont;
        bn_to_mont(&a24_mont, &a24, &ctx);

        /* ── Stage 1: multiply Q by all prime powers p^k ≤ B1 ──
         * Accumulate Z products in Montgomery form (cheap bn_mont_mul).
         * Only convert to standard form for periodic GCD checks. */
        BigNum z_accum_mont;
        { BigNum one; bn_from_u64(&one, 1u); bn_to_mont(&z_accum_mont, &one, &ctx); }
        uint64_t gcd_counter = 0u;
        bool curve_failed = false;

        for (uint64_t p = 2u; p <= B1; ++p) {
            if (!miller_rabin_u64(p)) continue;
            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            ecm_mont_ladder(&Q0, &Q0, pk, &a24_mont, &ctx);

            /* Accumulate Z in Montgomery form (no conversion per prime) */
            bn_mont_mul(&z_accum_mont, &z_accum_mont, &Q0.Z, &ctx);

            /* Periodic GCD every 64 primes (convert once, check once) */
            ++gcd_counter;
            if (gcd_counter >= 64u) {
                gcd_counter = 0u;
                BigNum z_std;
                bn_from_mont(&z_std, &z_accum_mont, &ctx);
                BigNum g;
                bn_gcd(&g, &z_std, n);
                if (bn_factor_candidate_valid(&g, n)) {
                    fr.method = "ecm-mont(stage1)";
                    factor_result_bn_set_from_factor(&fr, n, &g);
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (!bn_is_one(&g)) { curve_failed = true; break; }
                { BigNum one; bn_from_u64(&one, 1u); bn_to_mont(&z_accum_mont, &one, &ctx); }
            }
        }

        /* Final Stage 1 GCD check for this curve */
        if (!curve_failed) {
            BigNum z_std;
            bn_from_mont(&z_std, &z_accum_mont, &ctx);
            if (!bn_is_one(&z_std)) {
                BigNum g;
                bn_gcd(&g, &z_std, n);
                if (bn_factor_candidate_valid(&g, n)) {
                    fr.method = "ecm-mont(stage1)";
                    factor_result_bn_set_from_factor(&fr, n, &g);
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
            }
        }

        /* ── Stage 2: baby-step/giant-step using Montgomery points ──
         * Baby steps: precompute (2i+1)·Q for i=0..D/2-1 using xADD with step 2·Q.
         * Giant steps: S = k·D·Q, iterate k, accumulate (Sx·Bz - Bx·Sz) mod N.
         * All arithmetic in Montgomery form. Periodic GCD checks. */
        if (!curve_failed && B2 > B1) {
            uint64_t range = B2 - B1;
            uint64_t D = 2u * (uint64_t)isqrt_u64(range);
            if (D < 4u) D = 4u;
            if (D & 1u) ++D;
            uint64_t baby_count = D / 2u;

            /* 2·Q for building baby steps */
            ECMMontPoint Q2;
            ecm_mont_xdbl(&Q2, &Q0, &a24_mont, &ctx);

            /* Baby-step table: store full (X,Z) in Montgomery form */
            ECMMontPoint* baby = (ECMMontPoint*)xmalloc(baby_count * sizeof(ECMMontPoint));
            baby[0] = Q0; /* 1·Q */
            bool baby_ok = true;

            if (baby_count > 1u) {
                /* 3·Q = Q + 2·Q (diff = Q) */
                ecm_mont_xadd(&baby[1], &Q0, &Q2, &Q0, &ctx);
                for (uint64_t i = 2u; i < baby_count; ++i) {
                    /* (2i+1)·Q = (2(i-1)+1)·Q + 2·Q, diff = (2(i-2)+1)·Q */
                    ecm_mont_xadd(&baby[i], &baby[i - 1u], &Q2, &baby[i - 2u], &ctx);
                    /* Check for degenerate zero Z */
                    if (bn_is_zero(&baby[i].Z)) { baby_ok = false; break; }
                }
            }

            if (baby_ok) {
                /* Giant step: D·Q */
                ECMMontPoint GD;
                ecm_mont_ladder(&GD, &Q0, D, &a24_mont, &ctx);

                if (!bn_is_zero(&GD.Z)) {
                    /* Starting giant step: S = k_start·D·Q */
                    uint64_t k_start = (B1 + D) / D;
                    uint64_t k_end = B2 / D + 1u;
                    ECMMontPoint S;
                    ecm_mont_ladder(&S, &Q0, k_start * D, &a24_mont, &ctx);

                    if (!bn_is_zero(&S.Z)) {
                        /* Accumulate products in Montgomery form */
                        BigNum accum_mont;
                        { BigNum one; bn_from_u64(&one, 1u); bn_to_mont(&accum_mont, &one, &ctx); }
                        uint64_t gcd_iv = 0u;

                        /* We need the previous giant step for xADD differential */
                        ECMMontPoint S_prev;
                        ecm_mont_ladder(&S_prev, &Q0, (k_start - 1u) * D, &a24_mont, &ctx);

                        for (uint64_t k = k_start; k <= k_end; ++k) {
                            /* Accumulate (Sx·Bz - Bx·Sz) for ALL baby steps.
                             * No per-pair primality test — non-prime q values add
                             * harmless extra factors to the product. The GCD still
                             * correctly detects factors. This is ~100x faster than
                             * testing each candidate for primality. */
                            for (uint64_t j = 0u; j < baby_count; ++j) {
                                BigNum t1, t2, diff;
                                bn_mont_mul(&t1, &S.X, &baby[j].Z, &ctx);
                                bn_mont_mul(&t2, &baby[j].X, &S.Z, &ctx);
                                if (bn_cmp(&t1, &t2) >= 0) {
                                    bn_copy(&diff, &t1); bn_sub(&diff, &t2);
                                } else {
                                    bn_copy(&diff, &t1); bn_add(&diff, &ctx.n); bn_sub(&diff, &t2);
                                }
                                if (!bn_is_zero(&diff)) {
                                    bn_mont_mul(&accum_mont, &accum_mont, &diff, &ctx);
                                }
                            }

                            ++gcd_iv;
                            if (gcd_iv >= 128u || k == k_end) {
                                BigNum acc_std;
                                bn_from_mont(&acc_std, &accum_mont, &ctx);
                                if (!bn_is_zero(&acc_std) && !bn_is_one(&acc_std)) {
                                    BigNum g;
                                    bn_gcd(&g, &acc_std, n);
                                    if (bn_factor_candidate_valid(&g, n)) {
                                        fr.method = "ecm-mont(stage2)";
                                        factor_result_bn_set_from_factor(&fr, n, &g);
                                        fr.time_seconds = now_sec() - t0;
                                        free(baby);
                                        return fr;
                                    }
                                }
                                { BigNum one; bn_from_u64(&one, 1u); bn_to_mont(&accum_mont, &one, &ctx); }
                                gcd_iv = 0u;
                            }

                            /* Advance giant step: S_new = S + GD (diff = S_prev) */
                            if (k < k_end) {
                                ECMMontPoint S_next;
                                ecm_mont_xadd(&S_next, &S, &GD, &S_prev, &ctx);
                                S_prev = S;
                                S = S_next;
                                if (bn_is_zero(&S.Z)) break;
                            }
                        }
                    }
                }
            }
            free(baby);
        }
    }

    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}
