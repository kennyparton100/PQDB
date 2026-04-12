/* ══════════════════════════════════════════════════════════════════════
 * POLLARD p-1 ENGINE (CPSS-accelerated stage 1)
 *
 * Computes M = lcm(1, 2, ..., B1) by iterating prime powers p^k <= B1,
 * then checks gcd(a^M - 1, n). Primes streamed from DB via iterator.
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Factor n using Pollard's p-1 method with smoothness bound B1.
 * Uses CPSS to stream primes for stage 1.
 * B1 = 0 defaults to min(1000000, db_hi).
 */
static FactorResult cpss_factor_pminus1(CPSSStream* s, uint64_t n, uint64_t B1) {
    FactorResult fr;
    factor_result_init(&fr, n, "pminus1");
    double t0 = now_sec();

    if (n <= 3u || (n & 1u) == 0u) {
        if (n <= 1u) { fr.fully_factored = true; fr.cofactor = n; }
        else if ((n & 1u) == 0u) {
            while (fr.cofactor % 2u == 0u) {
                factor_result_add(&fr, 2u);
                fr.cofactor /= 2u;
            }
            if (fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
            }
            fr.fully_factored = (fr.cofactor <= 1u);
        }
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) {
        db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    }
    if (B1 == 0u) {
        B1 = 1000000u;
        if (db_hi > 0u && db_hi < B1) B1 = db_hi;
    }
    if (db_hi > 0u && B1 > db_hi) {
        B1 = db_hi;
        fr.status = (int)CPSS_INCOMPLETE;
    }
    if (db_hi == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    uint64_t base = 2u;
    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);

    while (iter.valid && iter.current <= B1) {
        uint64_t p = iter.current;
        uint64_t pk = p;
        while (pk <= B1 / p) pk *= p;

        base = powmod_u64(base, pk, n);

        uint64_t g = gcd_u64(base > 0u ? base - 1u : n - 1u, n);
        if (g > 1u && g < n) {
            fr.method = "pminus1(stage1)";
            factor_result_add(&fr, g);
            fr.cofactor = n / g;
            if (miller_rabin_u64(g) && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            else if (miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            fr.time_seconds = now_sec() - t0;
            return fr;
        }
        if (g == n) {
            break;
        }

        cpss_iter_next(&iter);
    }

    if (base > 1u) {
        uint64_t g = gcd_u64(base - 1u, n);
        if (g > 1u && g < n) {
            fr.method = "pminus1(stage1)";
            factor_result_add(&fr, g);
            fr.cofactor = n / g;
            if (miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
                fr.fully_factored = true;
            }
            fr.time_seconds = now_sec() - t0;
            return fr;
        }
    }

    if (!fr.fully_factored && base > 1u) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                uint64_t bq = powmod_u64(base, q, n);
                uint64_t g = gcd_u64(bq > 0u ? bq - 1u : n - 1u, n);
                if (g > 1u && g < n) {
                    fr.method = "pminus1(stage2)";
                    factor_result_add(&fr, g);
                    fr.cofactor = n / g;
                    if (miller_rabin_u64(fr.cofactor)) {
                        factor_result_add(&fr, fr.cofactor);
                        fr.cofactor = 1u;
                        fr.fully_factored = true;
                    }
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (g == n) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) {
        fr.status = (int)CPSS_INCOMPLETE;
    }

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Database-backed Pollard p-1 variant.
 * Mirrors the streamed engine but advances across database shards using
 * `cpss_db_next_prime_s` instead of a `PrimeIterator`.
 */
static FactorResult cpss_db_factor_pminus1(CPSSDatabase* db, uint64_t n, uint64_t B1) {
    FactorResult fr;
    factor_result_init(&fr, n, "pminus1");
    double t0 = now_sec();

    if (n <= 3u || (n & 1u) == 0u) {
        if (n <= 1u) {
            fr.fully_factored = true;
            fr.cofactor = n;
        }
        else if ((n & 1u) == 0u) {
            while (fr.cofactor % 2u == 0u) {
                factor_result_add(&fr, 2u);
                fr.cofactor /= 2u;
            }
            if (fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor);
                fr.cofactor = 1u;
            }
            fr.fully_factored = (fr.cofactor <= 1u);
        }
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    if (!db || db->shard_count == 0u) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    {
        uint64_t db_hi = cpss_db_contiguous_hi(db, false);
        if (B1 == 0u) {
            B1 = 1000000u;
            if (db_hi > 0u && db_hi < B1) B1 = db_hi;
        }
        if (db_hi > 0u && B1 > db_hi) {
            B1 = db_hi;
            fr.status = (int)CPSS_INCOMPLETE;
        }

        {
            uint64_t base = 2u;
            uint64_t current = 1u;
            while (current < B1) {
                CPSSQueryStatus qs = CPSS_OK;
                uint64_t p = cpss_db_next_prime_s(db, current, &qs);
                if (qs != CPSS_OK) {
                    if (qs != CPSS_OUT_OF_RANGE) fr.status = (int)qs;
                    break;
                }
                if (p > B1) break;

                uint64_t pk = p;
                while (pk <= B1 / p) pk *= p;
                base = powmod_u64(base, pk, n);

                {
                    uint64_t g = gcd_u64(base > 0u ? base - 1u : n - 1u, n);
                    if (g > 1u && g < n) {
                        factor_result_add(&fr, g);
                        fr.cofactor = n / g;
                        if (miller_rabin_u64(g) && miller_rabin_u64(fr.cofactor)) {
                            factor_result_add(&fr, fr.cofactor);
                            fr.cofactor = 1u;
                            fr.fully_factored = true;
                        }
                        else if (miller_rabin_u64(fr.cofactor)) {
                            factor_result_add(&fr, fr.cofactor);
                            fr.cofactor = 1u;
                            fr.fully_factored = true;
                        }
                        fr.time_seconds = now_sec() - t0;
                        return fr;
                    }
                    if (g == n) break;
                }

                current = p;
            }

            if (base > 1u) {
                uint64_t g = gcd_u64(base - 1u, n);
                if (g > 1u && g < n) {
                    factor_result_add(&fr, g);
                    fr.cofactor = n / g;
                    if (miller_rabin_u64(fr.cofactor)) {
                        factor_result_add(&fr, fr.cofactor);
                        fr.cofactor = 1u;
                        fr.fully_factored = true;
                    }
                }
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Pollard p-1 for U128 inputs using CPSS-streamed primes.
 * Keeps the same stage-1 / light stage-2 strategy as the uint64 engine while
 * leaving oversized prime cofactors in `cofactor` when they do not fit the
 * compact uint64 factor array.
 */
static FactorResultU128 cpss_factor_pminus1_u128(CPSSStream* s, U128 n, uint64_t B1) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "pminus1");
    double t0 = now_sec();

    if (u128_le(n, u128_from_u64(3u)) || (n.lo & 1u) == 0u) {
        if (u128_le(n, u128_from_u64(1u))) { fr.fully_factored = true; fr.cofactor = n; }
        else if ((n.lo & 1u) == 0u) {
            while (u128_mod_u64(fr.cofactor, 2u) == 0u) {
                factor_result_u128_add(&fr, 2u);
                fr.cofactor = u128_div(fr.cofactor, u128_from_u64(2u));
            }
            if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                factor_result_u128_add(&fr, fr.cofactor.lo);
                fr.cofactor = u128_from_u64(1u);
            }
            fr.fully_factored = u128_le(fr.cofactor, u128_from_u64(1u));
        }
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (B1 == 0u) { B1 = 1000000u; if (db_hi > 0u && db_hi < B1) B1 = db_hi; }
    if (db_hi > 0u && B1 > db_hi) { B1 = db_hi; fr.status = (int)CPSS_INCOMPLETE; }
    if (db_hi == 0u) { fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr; }

    U128 base = u128_from_u64(2u);
    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);

    while (iter.valid && iter.current <= B1) {
        uint64_t p = iter.current;
        uint64_t pk = p;
        while (pk <= B1 / p) pk *= p;

        base = u128_powmod(base, u128_from_u64(pk), n);

        U128 base_minus_1 = u128_sub(base, u128_from_u64(1u));
        if (u128_is_zero(base_minus_1)) base_minus_1 = u128_sub(n, u128_from_u64(1u));
        U128 g = u128_gcd(base_minus_1, n);

        if (u128_gt(g, u128_from_u64(1u)) && u128_lt(g, n)) {
            fr.method = "pminus1(stage1)";
            factor_result_u128_set_split(&fr, g, u128_div(n, g));
            fr.time_seconds = now_sec() - t0;
            return fr;
        }
        if (u128_eq(g, n)) break;
        cpss_iter_next(&iter);
    }

    if (!fr.fully_factored && !u128_is_zero(base)) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                U128 bq = u128_powmod(base, u128_from_u64(q), n);
                U128 bq_m1 = u128_is_zero(bq) ? u128_sub(n, u128_from_u64(1u)) : u128_sub(bq, u128_from_u64(1u));
                U128 g2 = u128_gcd(bq_m1, n);
                if (u128_gt(g2, u128_from_u64(1u)) && u128_lt(g2, n)) {
                    fr.method = "pminus1(stage2)";
                    factor_result_u128_set_split(&fr, g2, u128_div(n, g2));
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (u128_eq(g2, n)) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK)
        fr.status = (int)CPSS_INCOMPLETE;

    fr.time_seconds = now_sec() - t0;
    return fr;
}

/* ══════════════════════════════════════════════════════════════════════
 * BIGNUM (1024-bit) POLLARD p-1 ENGINE
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Pollard p-1 for BigNum inputs using CPSS primes and Montgomery arithmetic.
 * Strips powers of two so the Montgomery modulus is odd, then reuses the same
 * staged smoothness search as the narrower implementations.
 */
static FactorResultBigNum cpss_factor_pminus1_bn(CPSSStream* s, const BigNum* n, uint64_t B1) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "pminus1";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    BigNum n_odd;
    bn_copy(&n_odd, n);
    while (bn_is_even(&n_odd) && !bn_is_zero(&n_odd)) {
        bn_shr1(&n_odd);
    }

    if (bn_is_one(&n_odd)) {
        bn_from_u64(&fr.factor, 2u);
        bn_from_u64(&fr.cofactor, 1u);
        fr.factor_found = true;
        fr.factor_is_prime = true;
        fr.cofactor_is_prime = true;
        fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (B1 == 0u) { B1 = 1000000u; if (db_hi > 0u && db_hi < B1) B1 = db_hi; }
    if (db_hi > 0u && B1 > db_hi) { B1 = db_hi; fr.status = (int)CPSS_INCOMPLETE; }
    if (db_hi == 0u) { fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr; }

    MontCtx ctx;
    if (!bn_montgomery_setup(&ctx, &n_odd)) {
        fr.status = (int)CPSS_UNAVAILABLE;
        fr.time_seconds = now_sec() - t0;
        return fr;
    }

    BigNum base;
    bn_from_u64(&base, 2u);

    PrimeIterator iter;
    cpss_iter_init(&iter, s, 2u);

    while (iter.valid && iter.current <= B1) {
        uint64_t p = iter.current;
        uint64_t pk = p;
        while (pk <= B1 / p) pk *= p;

        bn_mont_powmod(&base, &base, pk, &ctx);

        if (!bn_is_zero(&base) && !bn_is_one(&base)) {
            BigNum base_minus_1;
            bn_copy(&base_minus_1, &base);
            bn_sub_u64(&base_minus_1, 1u);
            BigNum g;
            bn_gcd(&g, &base_minus_1, &n_odd);

            if (!bn_is_one(&g) && bn_cmp(&g, &n_odd) != 0) {
                fr.method = "pminus1(stage1)";
                bn_copy(&fr.factor, &g);
                bn_divexact(&fr.cofactor, &n_odd, &g);
                fr.factor_found = true;
                if (bn_fits_u64(&fr.factor))
                    fr.factor_is_prime = miller_rabin_u64(bn_to_u64(&fr.factor));
                else if (bn_fits_u128(&fr.factor))
                    fr.factor_is_prime = u128_is_prime(bn_to_u128(&fr.factor));
                if (bn_fits_u64(&fr.cofactor))
                    fr.cofactor_is_prime = miller_rabin_u64(bn_to_u64(&fr.cofactor));
                else if (bn_fits_u128(&fr.cofactor))
                    fr.cofactor_is_prime = u128_is_prime(bn_to_u128(&fr.cofactor));
                fr.fully_factored = (fr.factor_is_prime && fr.cofactor_is_prime);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (bn_cmp(&g, &n_odd) == 0) break;
        }

        cpss_iter_next(&iter);
    }

    if (!fr.factor_found && !bn_is_zero(&base) && !bn_is_one(&base)) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                bn_mont_powmod(&base, &base, q, &ctx);
                if (!bn_is_zero(&base) && !bn_is_one(&base)) {
                    BigNum bm1; bn_copy(&bm1, &base); bn_sub_u64(&bm1, 1u);
                    BigNum g2; bn_gcd(&g2, &bm1, &n_odd);
                    if (!bn_is_one(&g2) && bn_cmp(&g2, &n_odd) != 0) {
                        fr.method = "pminus1(stage2)";
                        bn_copy(&fr.factor, &g2);
                        bn_divexact(&fr.cofactor, &n_odd, &g2);
                        fr.factor_found = true;
                        if (bn_fits_u64(&fr.factor))
                            fr.factor_is_prime = miller_rabin_u64(bn_to_u64(&fr.factor));
                        else if (bn_fits_u128(&fr.factor))
                            fr.factor_is_prime = u128_is_prime(bn_to_u128(&fr.factor));
                        if (bn_fits_u64(&fr.cofactor))
                            fr.cofactor_is_prime = miller_rabin_u64(bn_to_u64(&fr.cofactor));
                        else if (bn_fits_u128(&fr.cofactor))
                            fr.cofactor_is_prime = u128_is_prime(bn_to_u128(&fr.cofactor));
                        fr.fully_factored = (fr.factor_is_prime && fr.cofactor_is_prime);
                        fr.time_seconds = now_sec() - t0;
                        return fr;
                    }
                    if (bn_cmp(&g2, &n_odd) == 0) break;
                }
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.factor_found && fr.status == (int)CPSS_OK)
        fr.status = (int)CPSS_INCOMPLETE;

    fr.time_seconds = now_sec() - t0;
    return fr;
}
