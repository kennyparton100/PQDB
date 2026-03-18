/**
 * SemiPrimeBoundsSpace.c - Difference/imbalance bounds from Fermat geometry.
 * Part of the CPSS SemiPrime Spaces module (N-centric inference).
 *
 * Given only N (factors unknown), derives honest bounds on Delta, R, delta
 * from square gaps, failed Fermat steps, and factor exclusion evidence.
 *
 * Depends on: cpss_arith.c, cpss_factor.c
 */

/* ══════════════════════════════════════════════════════════════════════
 * PHASE 4: DIFFERENCE / IMBALANCE BOUNDS
 *
 * For semiprime N=p*q with p<=q, derive honest bounds on:
 *   Delta = q - p = 2B
 *   R     = q/p
 *   delta = (1/2)*log(q/p)
 * from Fermat geometry, failed Fermat steps, and factor exclusion.
 * ══════════════════════════════════════════════════════════════════════ */

static void difference_bounds(uint64_t N, int64_t fermat_K, uint64_t min_factor_L,
                               bool have_K, bool have_L) {
    printf("difference-bounds(%"PRIu64"):\n", N);

    /* Classification */
    bool is_even = (N > 2u && (N & 1u) == 0u);
    bool is_sq = is_perfect_square_u64(N);
    bool is_pr = miller_rabin_u64(N);
    printf("  N              = %"PRIu64"\n", N);
    printf("  even           = %s\n", is_even ? "yes" : "no");
    printf("  perfect square = %s\n", is_sq ? "yes" : "no");
    if (is_pr) {
        printf("  N is prime. Not a semiprime.\n");
        return;
    }
    if (N <= 3u) {
        printf("  N too small for semiprime analysis.\n");
        return;
    }

    /* Fermat baseline: a0 = ceil(sqrt(N)), r0 = a0^2 - N */
    uint64_t a0 = (uint64_t)isqrt_u64(N);
    if (a0 * a0 < N) ++a0; /* ceil */
    U128 a0_sq = u128_mul64(a0, a0);
    U128 N_u128 = u128_from_u64(N);
    U128 r0_u128 = u128_sub(a0_sq, N_u128);
    uint64_t r0 = u128_fits_u64(r0_u128) ? r0_u128.lo : UINT64_MAX;

    printf("\n  Fermat baseline:\n");
    printf("    a0 = ceil(sqrt(N)) = %"PRIu64"\n", a0);
    printf("    r0 = a0^2 - N      = %"PRIu64"\n", r0);

    if (is_sq) {
        printf("    N is a perfect square => if N = p^2, then Delta = 0 exactly.\n");
        printf("\n  Bounds (assuming N = p*q semiprime with p=q):\n");
        printf("    Delta = 0\n");
        printf("    R     = 1.0000\n");
        printf("    delta = 0.0000\n");
        return;
    }

    /* ── Lower bound on Delta from square gap ── */
    double sqrt_r0 = sqrt((double)r0);
    double delta_lower_sq = 2.0 * sqrt_r0;
    printf("\n  Lower bound (from nearest square):\n");
    printf("    B^2 >= r0 = %"PRIu64"\n", r0);
    printf("    B   >= sqrt(r0) = %.4f\n", sqrt_r0);
    printf("    Delta >= 2*sqrt(r0) = %.4f\n", delta_lower_sq);

    /* ── Stronger lower bound from failed Fermat steps ── */
    double delta_lower_fermat = delta_lower_sq; /* start with baseline */
    if (have_K) {
        printf("\n  Lower bound (from %"PRId64" failed Fermat steps):\n", (long long)fermat_K);
        if (fermat_K < 0) {
            printf("    (invalid: K must be >= 0)\n");
        } else {
            uint64_t aK = a0 + (uint64_t)fermat_K;
            U128 aK_sq = u128_mul64(aK, aK);
            if (u128_lt(aK_sq, N_u128)) {
                printf("    (a0+K)^2 < N: no useful bound from K=%"PRId64"\n", (long long)fermat_K);
            } else {
                U128 rK = u128_sub(aK_sq, N_u128);
                uint64_t rK64 = u128_fits_u64(rK) ? rK.lo : UINT64_MAX;
                double sqrt_rK = sqrt((double)rK64);
                delta_lower_fermat = 2.0 * sqrt_rK;
                if (fermat_K == 0) {
                    printf("    K=0: A >= a0, so B^2 >= r0 = %"PRIu64"\n", r0);
                    printf("    Delta >= 2*sqrt(r0) = %.4f  (same as baseline)\n", delta_lower_fermat);
                } else {
                    printf("    A > a0 + %"PRId64" = %"PRIu64"\n", (long long)fermat_K, aK);
                    printf("    B^2 > (a0+K)^2 - N = %"PRIu64"\n", rK64);
                    printf("    Delta > 2*sqrt(%"PRIu64") = %.4f\n", rK64, delta_lower_fermat);
                }
            }
        }
    }

    /* Best lower bound on Delta */
    double best_delta_lower = delta_lower_fermat;

    /* ── Upper bound from ruling out small factors ── */
    double delta_upper = -1.0; /* negative means no upper bound */
    if (have_L && min_factor_L >= 2u) {
        printf("\n  Upper bound (from p >= %"PRIu64"):\n", min_factor_L);
        if (min_factor_L * min_factor_L > N) {
            printf("    L^2 > N: L=%"PRIu64" is too large (no factor p >= L possible)\n", min_factor_L);
        } else {
            /* Delta <= N/L - L, using U128 for safety */
            uint64_t q_max = N / min_factor_L;
            uint64_t delta_upper_u64 = q_max - min_factor_L;
            delta_upper = (double)delta_upper_u64;
            printf("    q <= N/L = %"PRIu64"/%"PRIu64" = %"PRIu64"\n", N, min_factor_L, q_max);
            printf("    Delta <= q - p <= %"PRIu64" - %"PRIu64" = %"PRIu64"\n", q_max, min_factor_L, delta_upper_u64);
            printf("    Delta <= %"PRIu64"\n", delta_upper_u64);
        }
    }

    /* ── Summary ── */
    printf("\n  Summary:\n");

    /* Delta bounds */
    if (delta_upper >= 0.0 && best_delta_lower > 0.0) {
        printf("    Delta in [%.4f, %.4f]\n", best_delta_lower, delta_upper);
    } else if (best_delta_lower > 0.0) {
        printf("    Delta >= %.4f  (no upper bound available)\n", best_delta_lower);
    } else if (delta_upper >= 0.0) {
        printf("    Delta <= %.4f  (no lower bound beyond 0)\n", delta_upper);
    } else {
        printf("    Delta >= 0  (no useful bounds available)\n");
    }

    /* Implied ratio bounds: R = q/p */
    /* From Delta bounds and N=pq:
       p = (sqrt(4N + Delta^2) - Delta) / 2
       q = (sqrt(4N + Delta^2) + Delta) / 2
       R = q/p
       For lower bound on R: use lower bound on Delta
       For upper bound on R: use upper bound on Delta */
    printf("\n  Implied ratio bounds (R = q/p):\n");
    if (best_delta_lower > 0.0) {
        double D = best_delta_lower;
        double disc = sqrt(4.0 * (double)N + D * D);
        double p_est = (disc - D) / 2.0;
        double q_est = (disc + D) / 2.0;
        double R_lower = (p_est > 0.0) ? q_est / p_est : 1.0;
        printf("    R >= %.6f  (from Delta >= %.4f)\n", R_lower, D);
    } else {
        printf("    R >= 1.0000  (trivial)\n");
    }
    if (delta_upper >= 0.0) {
        double D = delta_upper;
        double disc = sqrt(4.0 * (double)N + D * D);
        double p_est = (disc - D) / 2.0;
        double q_est = (disc + D) / 2.0;
        double R_upper = (p_est > 0.0) ? q_est / p_est : 1.0;
        printf("    R <= %.6f  (from Delta <= %.4f)\n", R_upper, D);
    } else {
        printf("    R <= N/4 = %.1f  (trivial, no upper constraint)\n", (double)N / 4.0);
    }

    /* Implied log-imbalance bounds: delta_log = (1/2)*log(R) */
    printf("\n  Implied log-imbalance bounds (delta = (1/2)*log(q/p)):\n");
    if (best_delta_lower > 0.0) {
        double D = best_delta_lower;
        double disc = sqrt(4.0 * (double)N + D * D);
        double p_est = (disc - D) / 2.0;
        double q_est = (disc + D) / 2.0;
        double R_lower = (p_est > 0.0) ? q_est / p_est : 1.0;
        printf("    delta >= %.6f\n", 0.5 * log(R_lower));
    } else {
        printf("    delta >= 0.0000  (trivial)\n");
    }
    if (delta_upper >= 0.0) {
        double D = delta_upper;
        double disc = sqrt(4.0 * (double)N + D * D);
        double p_est = (disc - D) / 2.0;
        double q_est = (disc + D) / 2.0;
        double R_upper = (p_est > 0.0) ? q_est / p_est : 1.0;
        printf("    delta <= %.6f\n", 0.5 * log(R_upper));
    } else {
        printf("    (no upper bound on delta)\n");
    }
}

/** BigNum difference-bounds: Fermat geometry bounds for arbitrarily large N. */
static void difference_bounds_bn(const BigNum* N, int64_t fermat_K, uint64_t min_factor_L,
                                  bool have_K, bool have_L) {
    char nbuf[512]; bn_to_str(N, nbuf, sizeof(nbuf));
    unsigned nbits = bn_bitlen(N);
    printf("difference-bounds(%s): [%u bits]\n", nbuf, nbits);

    /* Classification */
    BigNum two_bn; bn_from_u64(&two_bn, 2u);
    bool is_even = bn_is_even(N) && bn_cmp(N, &two_bn) > 0;
    bool is_pr = bn_factor_is_prime(N);
    printf("  N              = %s\n", nbuf);
    printf("  bits           = %u\n", nbits);
    printf("  even           = %s\n", is_even ? "yes" : "no");
    if (is_pr) {
        printf("  N is prime (probable). Not a semiprime.\n");
        return;
    }
    if (bn_is_zero(N) || bn_is_one(N)) {
        printf("  N too small for semiprime analysis.\n");
        return;
    }

    /* Perfect square check */
    BigNum s;
    bn_isqrt_ceil(&s, N);
    BigNum s_floor;
    bn_copy(&s_floor, &s);
    if (bn_cmp(&s, N) > 0 || bn_is_zero(&s)) {
        /* s is ceil, floor = s-1 if s^2 > N */
        BigNum s2; bn_mul(&s2, &s, &s);
        if (bn_cmp(&s2, N) > 0 && !bn_is_zero(&s)) {
            bn_copy(&s_floor, &s);
            bn_sub_u64(&s_floor, 1u);
        }
    }
    BigNum s_floor_sq; bn_mul(&s_floor_sq, &s_floor, &s_floor);
    bool is_sq = (bn_cmp(&s_floor_sq, N) == 0);

    printf("  perfect square = %s\n", is_sq ? "yes" : "no");
    if (is_sq) {
        printf("    N is a perfect square => if N = p^2, then Delta = 0 exactly.\n");
        printf("\n  Bounds (assuming N = p*q semiprime with p=q):\n");
        printf("    Delta = 0\n    R     = 1.0000\n    delta = 0.0000\n");
        return;
    }

    /* Fermat baseline: a0 = ceil(sqrt(N)), r0 = a0^2 - N */
    BigNum a0;
    bn_isqrt_ceil(&a0, N);
    BigNum a0_sq; bn_mul(&a0_sq, &a0, &a0);
    BigNum r0; bn_copy(&r0, &a0_sq); bn_sub(&r0, N);

    char a0buf[512]; bn_to_str(&a0, a0buf, sizeof(a0buf));
    char r0buf[512]; bn_to_str(&r0, r0buf, sizeof(r0buf));
    printf("\n  Fermat baseline:\n");
    printf("    a0 = ceil(sqrt(N)) = %s\n", a0buf);
    printf("    r0 = a0^2 - N      = %s\n", r0buf);

    /* Lower bound: Delta >= 2*sqrt(r0) */
    double r0_d = bn_fits_u64(&r0) ? (double)bn_to_u64(&r0) : pow(2.0, (double)bn_bitlen(&r0));
    double delta_lower = 2.0 * sqrt(r0_d);
    printf("\n  Lower bound (from nearest square):\n");
    printf("    B^2 >= r0 = %s\n", r0buf);
    printf("    Delta >= 2*sqrt(r0) ~ %.4f\n", delta_lower);

    /* Stronger lower bound from failed Fermat steps */
    double delta_lower_fermat = delta_lower;
    if (have_K && fermat_K > 0) {
        printf("\n  Lower bound (from %"PRId64" failed Fermat steps):\n", (long long)fermat_K);
        BigNum aK; bn_copy(&aK, &a0); bn_add_u64(&aK, (uint64_t)fermat_K);
        BigNum aK_sq; bn_mul(&aK_sq, &aK, &aK);
        if (bn_cmp(&aK_sq, N) >= 0) {
            BigNum rK; bn_copy(&rK, &aK_sq); bn_sub(&rK, N);
            double rK_d = bn_fits_u64(&rK) ? (double)bn_to_u64(&rK) : pow(2.0, (double)bn_bitlen(&rK));
            delta_lower_fermat = 2.0 * sqrt(rK_d);
            char rKbuf[512]; bn_to_str(&rK, rKbuf, sizeof(rKbuf));
            printf("    B^2 > (a0+K)^2 - N = %s\n", rKbuf);
            printf("    Delta > 2*sqrt(rK) ~ %.4f\n", delta_lower_fermat);
        }
    }
    double best_delta_lower = delta_lower_fermat;

    /* Upper bound from ruling out small factors */
    double delta_upper = -1.0;
    if (have_L && min_factor_L >= 2u) {
        printf("\n  Upper bound (from p >= %"PRIu64"):\n", min_factor_L);
        BigNum L_bn; bn_from_u64(&L_bn, min_factor_L);
        BigNum L_sq; bn_mul(&L_sq, &L_bn, &L_bn);
        if (bn_cmp(&L_sq, N) > 0) {
            printf("    L^2 > N: L=%"PRIu64" is too large\n", min_factor_L);
        } else {
            /* q_max = N / L, Delta <= q_max - L */
            BigNum q_max, rem;
            bn_copy(&q_max, N);
            /* Integer division N/L */
            if (min_factor_L > 0u) {
                (void)bn_div_u64(&q_max, min_factor_L);
            }
            BigNum delta_bn; bn_copy(&delta_bn, &q_max); bn_sub(&delta_bn, &L_bn);
            char dubuf[512]; bn_to_str(&delta_bn, dubuf, sizeof(dubuf));
            delta_upper = bn_fits_u64(&delta_bn) ? (double)bn_to_u64(&delta_bn) : pow(2.0, (double)bn_bitlen(&delta_bn));
            printf("    q <= N/L, Delta <= %s\n", dubuf);
        }
    }

    /* Summary */
    printf("\n  Summary:\n");
    if (delta_upper >= 0.0 && best_delta_lower > 0.0)
        printf("    Delta in [~%.4f, ~%.4f]\n", best_delta_lower, delta_upper);
    else if (best_delta_lower > 0.0)
        printf("    Delta >= ~%.4f  (no upper bound available)\n", best_delta_lower);
    else
        printf("    Delta >= 0  (no useful bounds available)\n");

    /* Implied ratio bounds */
    printf("\n  Implied ratio bounds (R = q/p, approximate):\n");
    if (best_delta_lower > 0.0) {
        double N_d = pow(2.0, (double)nbits - 1.0); /* rough estimate */
        double disc = sqrt(4.0 * N_d + best_delta_lower * best_delta_lower);
        double p_est = (disc - best_delta_lower) / 2.0;
        double R_lower = (p_est > 0.0) ? (disc + best_delta_lower) / (2.0 * p_est) : 1.0;
        printf("    R >= ~%.6f  (from Delta >= ~%.4f)\n", R_lower, best_delta_lower);
    } else {
        printf("    R >= 1.0000  (trivial)\n");
    }
    printf("  NOTE: ratio bounds are approximate for %u-bit N (double precision loss).\n", nbits);
}
