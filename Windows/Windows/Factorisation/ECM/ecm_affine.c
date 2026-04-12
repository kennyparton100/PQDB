typedef struct {
    BigNum x;
    BigNum y;
    bool infinity;
} ECMPoint;

static int ecm_point_double(ECMPoint* out, const ECMPoint* p, const BigNum* a,
    const BigNum* mod, BigNum* factor) {
    ECMPoint next;
    memset(&next, 0, sizeof(next));

    if (p->infinity || bn_is_zero(&p->y)) {
        next.infinity = true;
        *out = next;
        return 1;
    }

    BigNum denom, inv;
    bn_copy(&denom, &p->y);
    bn_shl1(&denom);
    if (!bn_mod_inverse_checked(&inv, &denom, mod, factor)) {
        return (factor && bn_factor_candidate_valid(factor, mod)) ? -1 : 0;
    }

    BigNum x_sq, numer, lambda, lambda_sq, two_x, tmp;
    bn_mod_square(&x_sq, &p->x, mod);
    bn_copy(&numer, &x_sq);
    bn_mod_add(&numer, &numer, &x_sq, mod);
    bn_mod_add(&numer, &numer, &x_sq, mod);
    bn_mod_add(&numer, &numer, a, mod);
    bn_mod_mul(&lambda, &numer, &inv, mod);

    bn_mod_square(&lambda_sq, &lambda, mod);
    bn_mod_add(&two_x, &p->x, &p->x, mod);
    bn_mod_sub(&next.x, &lambda_sq, &two_x, mod);

    bn_mod_sub(&tmp, &p->x, &next.x, mod);
    bn_mod_mul(&tmp, &tmp, &lambda, mod);
    bn_mod_sub(&next.y, &tmp, &p->y, mod);
    next.infinity = false;
    *out = next;
    return 1;
}

static int ecm_point_add(ECMPoint* out, const ECMPoint* p, const ECMPoint* q, const BigNum* a,
    const BigNum* mod, BigNum* factor) {
    ECMPoint next;
    memset(&next, 0, sizeof(next));

    if (p->infinity) {
        *out = *q;
        return 1;
    }
    if (q->infinity) {
        *out = *p;
        return 1;
    }

    if (bn_cmp(&p->x, &q->x) == 0) {
        BigNum y_sum;
        bn_mod_add(&y_sum, &p->y, &q->y, mod);
        if (bn_is_zero(&y_sum)) {
            next.infinity = true;
            *out = next;
            return 1;
        }
        return ecm_point_double(out, p, a, mod, factor);
    }

    BigNum numer, denom, inv, lambda, lambda_sq, tmp;
    bn_mod_sub(&numer, &q->y, &p->y, mod);
    bn_mod_sub(&denom, &q->x, &p->x, mod);
    if (!bn_mod_inverse_checked(&inv, &denom, mod, factor)) {
        return (factor && bn_factor_candidate_valid(factor, mod)) ? -1 : 0;
    }

    bn_mod_mul(&lambda, &numer, &inv, mod);
    bn_mod_square(&lambda_sq, &lambda, mod);
    bn_mod_sub(&tmp, &lambda_sq, &p->x, mod);
    bn_mod_sub(&next.x, &tmp, &q->x, mod);

    bn_mod_sub(&tmp, &p->x, &next.x, mod);
    bn_mod_mul(&tmp, &tmp, &lambda, mod);
    bn_mod_sub(&next.y, &tmp, &p->y, mod);
    next.infinity = false;
    *out = next;
    return 1;
}

static int ecm_point_mul_u64(ECMPoint* point, uint64_t k, const BigNum* a, const BigNum* mod,
    BigNum* factor) {
    ECMPoint result;
    memset(&result, 0, sizeof(result));
    result.infinity = true;

    ECMPoint addend = *point;
    while (k > 0u) {
        if ((k & 1u) != 0u) {
            int rc = ecm_point_add(&result, &result, &addend, a, mod, factor);
            if (rc <= 0) return rc;
        }
        k >>= 1u;
        if (k == 0u) break;
        {
            int rc = ecm_point_double(&addend, &addend, a, mod, factor);
            if (rc <= 0) return rc;
        }
    }

    *point = result;
    return 1;
}

static FactorResultBigNum cpss_factor_ecm_bn(const BigNum* n, uint64_t B1, uint64_t curves, uint64_t seed) {
    return cpss_factor_ecm_bn_ex(n, B1, 0u, curves, seed);
}

/**
 * ECM with Stage 1 + Stage 2.
 * Stage 1: multiply point by p^k for primes p <= B1.
 * Stage 2: baby-step/giant-step over primes q in (B1, B2].
 *   Precomputes D/2 baby-step x-coordinates, iterates giant steps of size D,
 *   accumulates products of (Sx - Bx) mod N, periodic GCD checks.
 *   O(√(B2-B1)) point ops + O(π(B2)-π(B1)) modular multiplies.
 * Still uses affine Weierstrass coordinates (not Montgomery/projective).
 * Missing vs industrial ECM: no Montgomery curve form, no extended-GCD-free
 * x-only ladder, no FFT-based polynomial evaluation for Stage 2.
 */
static FactorResultBigNum cpss_factor_ecm_bn_ex(const BigNum* n, uint64_t B1, uint64_t B2,
                                                   uint64_t curves, uint64_t seed) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "ecm";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();
    B1 = ecm_default_B1(B1);
    B2 = ecm_default_B2(B2, B1);
    curves = ecm_default_curves(curves);
    seed = ecm_default_seed(seed);

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_is_even(n)) {
        BigNum two;
        bn_from_u64(&two, 2u);
        factor_result_bn_set_from_factor(&fr, n, &two);
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_factor_is_prime(n)) {
        fr.cofactor_is_prime = true;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    for (uint64_t curve = 0u; curve < curves; ++curve) {
        BigNum a, b, x, y, factor;
        bn_random_below(&x, n, &seed);
        bn_random_below(&y, n, &seed);
        bn_random_below(&a, n, &seed);
        bn_zero(&factor);

        {
            BigNum y2, x2, x3, ax, rhs;
            bn_mod_square(&y2, &y, n);
            bn_mod_square(&x2, &x, n);
            bn_mod_mul(&x3, &x2, &x, n);
            bn_mod_mul(&ax, &a, &x, n);
            bn_mod_add(&rhs, &x3, &ax, n);
            bn_mod_sub(&b, &y2, &rhs, n);
        }

        {
            BigNum a2, a3, b2, term1, term2, delta, g, four, twenty_seven;
            bn_mod_mul(&a2, &a, &a, n);
            bn_mod_mul(&a3, &a2, &a, n);
            bn_mod_mul(&b2, &b, &b, n);
            bn_from_u64(&four, 4u);
            bn_from_u64(&twenty_seven, 27u);
            bn_mod_mul(&term1, &a3, &four, n);
            bn_mod_mul(&term2, &b2, &twenty_seven, n);
            bn_mod_add(&delta, &term1, &term2, n);
            bn_gcd(&g, &delta, n);
            if (bn_factor_candidate_valid(&g, n)) {
                factor_result_bn_set_from_factor(&fr, n, &g);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (!bn_is_one(&g)) continue;
        }

        ECMPoint point;
        bn_copy(&point.x, &x);
        bn_copy(&point.y, &y);
        point.infinity = false;
        bool stage1_broke = false;

        /* Stage 1: primes p <= B1, multiply by p^k */
        for (uint64_t p = 2u; p <= B1; ++p) {
            if (!miller_rabin_u64(p)) continue;

            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            bn_zero(&factor);
            {
                int rc = ecm_point_mul_u64(&point, pk, &a, n, &factor);
                if (rc < 0 && bn_factor_candidate_valid(&factor, n)) {
                    fr.method = "ecm(stage1)";
                    factor_result_bn_set_from_factor(&fr, n, &factor);
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (rc <= 0 || point.infinity) { stage1_broke = true; break; }
            }
        }

        /* ── Stage 2: baby-step/giant-step ──
         * Instead of computing q·P from scratch for each prime q in (B1,B2],
         * we precompute a table of small multiples of P (baby steps), then
         * iterate large multiples (giant steps). At each giant step G=k·D·P,
         * we check gcd(product of (Gx - Bx) for each baby step, N).
         * This replaces O(π(B2)·log(B2)) point ops with O(√(B2)) point ops
         * + O(π(B2)) cheap modular multiplies.
         */
        if (!stage1_broke && !point.infinity && B2 > B1) {
            /* Choose step size D: approximately 2·√(B2-B1), rounded up to even */
            uint64_t range = B2 - B1;
            uint64_t D = 2u * (uint64_t)isqrt_u64(range);
            if (D < 4u) D = 4u;
            if (D & 1u) ++D; /* D must be even so D·P and odd baby steps cover all odd values */
            uint64_t baby_count = D / 2u; /* number of odd baby steps: 1, 3, 5, ..., D-1 */

            /* Baby steps: precompute j·P for j = 1, 2 (used as building blocks) */
            ECMPoint P1 = point;                    /* 1·P = Stage 1 endpoint */
            ECMPoint P2;
            bn_zero(&factor);
            int rc2 = ecm_point_double(&P2, &P1, &a, n, &factor);
            if (rc2 < 0 && bn_factor_candidate_valid(&factor, n)) {
                fr.method = "ecm(stage2)";
                factor_result_bn_set_from_factor(&fr, n, &factor);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (rc2 <= 0 || P2.infinity) goto ecm_next_curve;

            /* Allocate baby-step x-coordinate table */
            /* baby_x[i] = x-coord of (2i+1)·P for i = 0, 1, ..., baby_count-1 */
            BigNum* baby_x = (BigNum*)xmalloc(baby_count * sizeof(BigNum));
            bn_copy(&baby_x[0], &P1.x); /* 1·P */

            ECMPoint cur = P1; /* tracks (2i+1)·P */
            bool baby_ok = true;
            for (uint64_t i = 1u; i < baby_count; ++i) {
                /* (2(i-1)+1)·P + 2·P = (2i+1)·P */
                bn_zero(&factor);
                int rc = ecm_point_add(&cur, &cur, &P2, &a, n, &factor);
                if (rc < 0 && bn_factor_candidate_valid(&factor, n)) {
                    fr.method = "ecm(stage2)";
                    factor_result_bn_set_from_factor(&fr, n, &factor);
                    fr.time_seconds = now_sec() - t0;
                    free(baby_x);
                    return fr;
                }
                if (rc <= 0 || cur.infinity) { baby_ok = false; break; }
                bn_copy(&baby_x[i], &cur.x);
            }

            if (baby_ok) {
                /* Giant step: compute D·P */
                ECMPoint Gstep = point; /* will become D·P */
                bn_zero(&factor);
                int rcG = ecm_point_mul_u64(&Gstep, D, &a, n, &factor);
                if (rcG < 0 && bn_factor_candidate_valid(&factor, n)) {
                    fr.method = "ecm(stage2)";
                    factor_result_bn_set_from_factor(&fr, n, &factor);
                    fr.time_seconds = now_sec() - t0;
                    free(baby_x);
                    return fr;
                }

                if (rcG > 0 && !Gstep.infinity) {
                    /* Giant iteration: S starts at ceil((B1+1)/D)*D, step by D */
                    uint64_t k_start = (B1 + D) / D; /* first k such that k*D > B1 */
                    uint64_t k_end = B2 / D + 1u;    /* last k such that k*D - (D-1) <= B2 */

                    /* S = k_start * D * P */
                    ECMPoint S = point;
                    bn_zero(&factor);
                    int rcS = ecm_point_mul_u64(&S, k_start * D, &a, n, &factor);
                    if (rcS < 0 && bn_factor_candidate_valid(&factor, n)) {
                        fr.method = "ecm(stage2)";
                        factor_result_bn_set_from_factor(&fr, n, &factor);
                        fr.time_seconds = now_sec() - t0;
                        free(baby_x);
                        return fr;
                    }

                    if (rcS > 0 && !S.infinity) {
                        /* Accumulate product of (Sx - baby_x[j]) for matching primes */
                        BigNum accum;
                        bn_from_u64(&accum, 1u);
                        uint64_t gcd_interval = 0u;

                        for (uint64_t k = k_start; k <= k_end; ++k) {
                            /* For this giant step at center = k*D, check q = k*D ± (2j+1) */
                            for (uint64_t j = 0u; j < baby_count; ++j) {
                                uint64_t q_plus = k * D + (2u * j + 1u);
                                uint64_t q_minus = (k * D > 2u * j + 1u) ? k * D - (2u * j + 1u) : 0u;

                                bool want_plus = (q_plus > B1 && q_plus <= B2 && miller_rabin_u64(q_plus));
                                bool want_minus = (q_minus > B1 && q_minus <= B2 && miller_rabin_u64(q_minus));

                                if (want_plus || want_minus) {
                                    /* Accumulate (Sx - baby_x[j]) mod N */
                                    BigNum diff;
                                    bn_mod_sub(&diff, &S.x, &baby_x[j], n);
                                    if (!bn_is_zero(&diff)) {
                                        bn_mod_mul(&accum, &accum, &diff, n);
                                    }
                                }
                            }

                            /* Periodic GCD check every ~128 giant steps */
                            ++gcd_interval;
                            if (gcd_interval >= 128u || k == k_end) {
                                if (!bn_is_zero(&accum) && !bn_is_one(&accum)) {
                                    BigNum g;
                                    bn_gcd(&g, &accum, n);
                                    if (bn_factor_candidate_valid(&g, n)) {
                                        fr.method = "ecm(stage2)";
                                        factor_result_bn_set_from_factor(&fr, n, &g);
                                        fr.time_seconds = now_sec() - t0;
                                        free(baby_x);
                                        return fr;
                                    }
                                }
                                bn_from_u64(&accum, 1u);
                                gcd_interval = 0u;
                            }

                            /* Advance giant step: S += D·P */
                            if (k < k_end) {
                                bn_zero(&factor);
                                int rcA = ecm_point_add(&S, &S, &Gstep, &a, n, &factor);
                                if (rcA < 0 && bn_factor_candidate_valid(&factor, n)) {
                                    fr.method = "ecm(stage2)";
                                    factor_result_bn_set_from_factor(&fr, n, &factor);
                                    fr.time_seconds = now_sec() - t0;
                                    free(baby_x);
                                    return fr;
                                }
                                if (rcA <= 0 || S.infinity) break;
                            }
                        }
                    }
                }
            }
            free(baby_x);
        }
ecm_next_curve: (void)0;
    }

    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}
