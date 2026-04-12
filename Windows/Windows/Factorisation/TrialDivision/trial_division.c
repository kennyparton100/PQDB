/* ══════════════════════════════════════════════════════════════════════
 * TRIAL DIVISION ENGINE (CPSS-accelerated)
 *
 * Divides n by wheel primes, then streams primes from the DB via
 * PrimeIterator up to min(limit, sqrt(n), db_hi).
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Factor n by trial division using CPSS-streamed primes.
 * limit: max prime to try (0 = sqrt(n)).
 * Returns FactorResult with found factors, cofactor, and status.
 */
static FactorResult cpss_factor_trial(CPSSStream* s, uint64_t n, uint64_t limit) {
    FactorResult fr;
    factor_result_init(&fr, n, "trial");
    double t0 = now_sec();

    if (n <= 1u) {
        fr.cofactor = n;
        fr.fully_factored = (n <= 1u);
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        while (fr.cofactor % p == 0u) {
            factor_result_add(&fr, p);
            fr.cofactor /= p;
        }
    }

    if (fr.cofactor <= 1u) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    uint64_t sqrt_cof = (uint64_t)isqrt_u64(fr.cofactor);
    if (sqrt_cof * sqrt_cof < fr.cofactor) ++sqrt_cof;
    uint64_t trial_limit = (limit > 0u && limit < sqrt_cof) ? limit : sqrt_cof;

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) {
        db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    }

    uint64_t effective_limit = trial_limit;
    if (db_hi > 0u && db_hi < effective_limit) {
        effective_limit = db_hi;
        if (effective_limit < sqrt_cof) {
            fr.status = (int)CPSS_INCOMPLETE;
        }
    }
    else if (db_hi == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    PrimeIterator iter;
    cpss_iter_init(&iter, s, WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u);

    while (iter.valid && iter.current <= effective_limit) {
        uint64_t p = iter.current;

        if (p > 0u && p <= UINT32_MAX && (uint64_t)p * p > fr.cofactor) {
            break;
        }

        while (fr.cofactor % p == 0u) {
            factor_result_add(&fr, p);
            fr.cofactor /= p;
        }

        if (fr.cofactor <= 1u) break;
        cpss_iter_next(&iter);
    }

    if (fr.cofactor <= 1u) {
        fr.fully_factored = true;
    }
    else if (fr.cofactor > 1u) {
        uint64_t sqrt_remaining = (uint64_t)isqrt_u64(fr.cofactor);
        if (sqrt_remaining <= effective_limit) {
            factor_result_add(&fr, fr.cofactor);
            fr.cofactor = 1u;
            fr.fully_factored = true;
        }
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Database-backed trial division variant.
 * Uses shard-aware DB iteration instead of a stream-local PrimeIterator, but
 * otherwise mirrors the streamed engine's limit and completion semantics.
 */
static FactorResult cpss_db_factor_trial(CPSSDatabase* db, uint64_t n, uint64_t limit) {
    FactorResult fr;
    factor_result_init(&fr, n, "trial");
    double t0 = now_sec();

    if (n <= 1u) {
        fr.cofactor = n;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        while (fr.cofactor % p == 0u) {
            factor_result_add(&fr, p);
            fr.cofactor /= p;
        }
    }

    if (fr.cofactor <= 1u) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (!db || db->shard_count == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    {
        uint64_t sqrt_cof = (uint64_t)isqrt_u64(fr.cofactor);
        if (sqrt_cof * sqrt_cof < fr.cofactor) ++sqrt_cof;
        uint64_t trial_limit = (limit > 0u && limit < sqrt_cof) ? limit : sqrt_cof;
        uint64_t effective_limit = trial_limit;
        uint64_t db_hi = cpss_db_contiguous_hi(db, false);

        if (db_hi > 0u && db_hi < effective_limit) {
            effective_limit = db_hi;
            if (effective_limit < sqrt_cof) fr.status = (int)CPSS_INCOMPLETE;
        }

        if (effective_limit > WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u]) {
            uint64_t current = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u];
            while (current < effective_limit) {
                CPSSQueryStatus qs = CPSS_OK;
                uint64_t p = cpss_db_next_prime_s(db, current, &qs);
                if (qs != CPSS_OK) {
                    if (qs != CPSS_OUT_OF_RANGE) fr.status = (int)qs;
                    break;
                }
                if (p > effective_limit) break;
                if (p <= UINT32_MAX && (uint64_t)p * p > fr.cofactor) break;

                while (fr.cofactor % p == 0u) {
                    factor_result_add(&fr, p);
                    fr.cofactor /= p;
                }

                if (fr.cofactor <= 1u) break;
                current = p;
            }
        }

        if (fr.cofactor <= 1u) {
            fr.fully_factored = true;
        }
        else {
            uint64_t sqrt_remaining = (uint64_t)isqrt_u64(fr.cofactor);
            if (sqrt_remaining * sqrt_remaining < fr.cofactor) ++sqrt_remaining;
            if (sqrt_remaining <= effective_limit) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
        }
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Trial division for U128 inputs using streamed uint64 primes.
 * The result structure can only store uint64 prime factors explicitly, so a
 * larger prime remainder may stay in `cofactor` while still counting as done.
 */
static FactorResultU128 cpss_factor_trial_u128(CPSSStream* s, U128 n, uint64_t limit) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "trial");
    double t0 = now_sec();

    if (u128_le(n, u128_from_u64(1u))) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        while (u128_mod_u64(fr.cofactor, p) == 0u) {
            factor_result_u128_add(&fr, p);
            fr.cofactor = u128_div(fr.cofactor, u128_from_u64(p));
        }
    }

    if (u128_le(fr.cofactor, u128_from_u64(1u))) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    uint64_t sqrt_cof = u128_isqrt_ceil(fr.cofactor);
    uint64_t trial_limit = (limit > 0u && limit < sqrt_cof) ? limit : sqrt_cof;

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);

    uint64_t effective_limit = trial_limit;
    if (db_hi > 0u && db_hi < effective_limit) {
        effective_limit = db_hi;
        if (effective_limit < sqrt_cof) fr.status = (int)CPSS_INCOMPLETE;
    }
    else if (db_hi == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    PrimeIterator iter;
    cpss_iter_init(&iter, s, WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u);

    while (iter.valid && iter.current <= effective_limit) {
        uint64_t p = iter.current;
        if (u128_fits_u64(fr.cofactor) && p * p > fr.cofactor.lo) break;
        if (!u128_fits_u64(fr.cofactor) && u128_gt(u128_mul64(p, p), fr.cofactor)) break;

        while (u128_mod_u64(fr.cofactor, p) == 0u) {
            factor_result_u128_add(&fr, p);
            fr.cofactor = u128_div(fr.cofactor, u128_from_u64(p));
        }
        if (u128_le(fr.cofactor, u128_from_u64(1u))) break;
        cpss_iter_next(&iter);
    }

    if (u128_le(fr.cofactor, u128_from_u64(1u))) {
        fr.fully_factored = true;
    }
    else {
        if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
            factor_result_u128_add(&fr, fr.cofactor.lo);
            fr.cofactor = u128_from_u64(1u);
            fr.fully_factored = true;
        }
        else if (!u128_fits_u64(fr.cofactor) && u128_is_prime(fr.cofactor)) {
            fr.fully_factored = true;
        }
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}
