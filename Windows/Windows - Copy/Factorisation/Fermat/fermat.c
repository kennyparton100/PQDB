/**
 * Fermat difference-of-squares for uint64 inputs.
 * Best on odd composites whose prime factors are close together.
 */
static FactorResult cpss_factor_fermat(uint64_t n, uint64_t max_steps) {
    FactorResult fr;
    factor_result_init(&fr, n, "fermat");
    double t0 = now_sec();
    max_steps = fermat_default_steps_small(max_steps);

    if (n <= 1u) {
        fr.cofactor = n;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    while ((fr.cofactor & 1u) == 0u) {
        factor_result_add(&fr, 2u);
        fr.cofactor /= 2u;
    }
    if (fr.cofactor <= 1u) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }
    if (miller_rabin_u64(fr.cofactor)) {
        factor_result_add(&fr, fr.cofactor);
        fr.cofactor = 1u;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    uint64_t target = fr.cofactor;
    uint64_t a = (uint64_t)isqrt_u64(target);
    if (a * a < target) ++a;

    U128 b2 = u128_sub(u128_mul64(a, a), u128_from_u64(target));
    uint64_t b = u128_isqrt_floor(b2);
    U128 b_sq = u128_mul64(b, b);
    U128 next_sq = b_sq;
    {
        U128 delta = u128_from_u64(b);
        delta = u128_shl(delta, 1u);
        delta = u128_add(delta, u128_from_u64(1u));
        next_sq = u128_add(next_sq, delta);
    }

    for (uint64_t step = 0u;; ++step) {
        while (u128_le(next_sq, b2)) {
            ++b;
            b_sq = next_sq;
            U128 delta = u128_from_u64(b);
            delta = u128_shl(delta, 1u);
            delta = u128_add(delta, u128_from_u64(1u));
            next_sq = u128_add(b_sq, delta);
        }

        if (u128_eq(b_sq, b2)) {
            uint64_t left = a - b;
            uint64_t right = target / left;
            factor_result_set_split(&fr, left, right);
            fr.time_seconds = now_sec() - t0;
            return fr;
        }

        if (step >= max_steps) break;

        U128 delta = u128_from_u64(a);
        delta = u128_shl(delta, 1u);
        delta = u128_add(delta, u128_from_u64(1u));
        b2 = u128_add(b2, delta);
        ++a;
    }

    fr.cofactor = target;
    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Fermat difference-of-squares for native U128 inputs.
 * Uses the same square-difference walk as the uint64 path while keeping the
 * intermediate residuals in U128 arithmetic.
 */
static FactorResultU128 cpss_factor_fermat_u128(U128 n, uint64_t max_steps) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "fermat");
    double t0 = now_sec();
    max_steps = fermat_default_steps_small(max_steps);

    if (u128_le(n, u128_from_u64(1u))) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    while (u128_mod_u64(fr.cofactor, 2u) == 0u) {
        factor_result_u128_add(&fr, 2u);
        fr.cofactor = u128_div(fr.cofactor, u128_from_u64(2u));
    }
    if (u128_le(fr.cofactor, u128_from_u64(1u))) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }
    if (u128_is_prime(fr.cofactor)) {
        if (u128_fits_u64(fr.cofactor)) {
            factor_result_u128_add(&fr, fr.cofactor.lo);
            fr.cofactor = u128_from_u64(1u);
        }
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    U128 target = fr.cofactor;
    uint64_t a = u128_isqrt_ceil(target);
    U128 b2 = u128_sub(u128_mul64(a, a), target);
    uint64_t b = u128_isqrt_floor(b2);
    U128 b_sq = u128_mul64(b, b);
    U128 next_sq = b_sq;
    {
        U128 delta = u128_from_u64(b);
        delta = u128_shl(delta, 1u);
        delta = u128_add(delta, u128_from_u64(1u));
        next_sq = u128_add(next_sq, delta);
    }

    for (uint64_t step = 0u;; ++step) {
        while (u128_le(next_sq, b2)) {
            ++b;
            b_sq = next_sq;
            U128 delta = u128_from_u64(b);
            delta = u128_shl(delta, 1u);
            delta = u128_add(delta, u128_from_u64(1u));
            next_sq = u128_add(b_sq, delta);
        }

        if (u128_eq(b_sq, b2)) {
            uint64_t left = a - b;
            U128 left_u128 = u128_from_u64(left);
            U128 right = u128_div(target, left_u128);
            factor_result_u128_set_split(&fr, left_u128, right);
            fr.time_seconds = now_sec() - t0;
            return fr;
        }

        if (step >= max_steps) break;

        U128 delta = u128_from_u64(a);
        delta = u128_shl(delta, 1u);
        delta = u128_add(delta, u128_from_u64(1u));
        b2 = u128_add(b2, delta);
        ++a;
    }

    fr.cofactor = target;
    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Fermat difference-of-squares for BigNum inputs.
 * This keeps the same incremental square tracking strategy as the narrower
 * variants but trades native arithmetic for BigNum square-root and delta ops.
 */
static FactorResultBigNum cpss_factor_fermat_bn(const BigNum* n, uint64_t max_steps) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "fermat";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();
    max_steps = fermat_default_steps_bn(max_steps);

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_is_even(n)) {
        bn_from_u64(&fr.factor, 2u);
        bn_copy(&fr.cofactor, n);
        (void)bn_div_u64(&fr.cofactor, 2u);
        fr.factor_found = true;
        fr.factor_is_prime = true;
        fr.cofactor_is_prime = bn_is_one(&fr.cofactor) ? true : bn_factor_is_prime(&fr.cofactor);
        fr.fully_factored = fr.cofactor_is_prime;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (bn_factor_is_prime(n)) {
        fr.cofactor_is_prime = true;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    BigNum a;
    bn_isqrt_ceil(&a, n);

    BigNum a_sq;
    bn_mul(&a_sq, &a, &a);
    BigNum b2;
    bn_copy(&b2, &a_sq);
    bn_sub(&b2, n);

    BigNum b;
    bn_isqrt_floor(&b, &b2);
    BigNum b_sq;
    bn_mul(&b_sq, &b, &b);
    BigNum next_sq;
    bn_copy(&next_sq, &b_sq);
    {
        BigNum delta;
        bn_copy(&delta, &b);
        bn_shl1(&delta);
        bn_add_u64(&delta, 1u);
        bn_add(&next_sq, &delta);
    }

    for (uint64_t step = 0u;; ++step) {
        while (bn_cmp(&next_sq, &b2) <= 0) {
            bn_add_u64(&b, 1u);
            bn_copy(&b_sq, &next_sq);
            BigNum delta;
            bn_copy(&delta, &b);
            bn_shl1(&delta);
            bn_add_u64(&delta, 1u);
            bn_copy(&next_sq, &b_sq);
            bn_add(&next_sq, &delta);
        }

        if (bn_cmp(&b_sq, &b2) == 0) {
            BigNum left, right;
            bn_copy(&left, &a);
            bn_sub(&left, &b);
            bn_copy(&right, &a);
            bn_add(&right, &b);
            factor_result_bn_set_split(&fr, &left, &right);
            fr.time_seconds = now_sec() - t0;
            return fr;
        }

        if (step >= max_steps) break;

        BigNum delta;
        bn_copy(&delta, &a);
        bn_shl1(&delta);
        bn_add_u64(&delta, 1u);
        bn_add(&b2, &delta);
        bn_add_u64(&a, 1u);
    }

    fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}
