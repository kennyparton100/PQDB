/* ══════════════════════════════════════════════════════════════════════
 * WILLIAMS p+1 FACTORISATION (CPSS-accelerated)
 *
 * Complementary to p-1: finds factor p when p+1 is B-smooth.
 * Uses Lucas sequences V_k(a) mod n.
 * Core recurrence: V_{2k} = V_k^2 - 2,  V_{k+l} = V_k*V_l - V_{k-l}
 * For prime power exponent: V_{pk}(a) = V_k(V_p(a)) by composition.
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Compute V_e(a) mod n using the Lucas chain (uint64 version).
 * Uses the standard binary ladder: maintains (V_k, V_{k+1}) pair.
 */
static uint64_t lucas_chain_u64(uint64_t a, uint64_t e, uint64_t n) {
    if (e == 0u) return 2u % n;
    if (e == 1u) return a % n;

    uint64_t a_mod = a % n;
    uint64_t vk = a_mod;
    uint64_t vk1 = mulmod_u64(a_mod, a_mod, n);
    if (vk1 >= 2u) vk1 -= 2u; else vk1 = vk1 + n - 2u;

    uint64_t mask = (uint64_t)1u << 62u;
    while (mask > 0u && !(e & mask)) mask >>= 1u;
    mask >>= 1u;

    while (mask > 0u) {
        if (e & mask) {
            uint64_t t = mulmod_u64(vk, vk1, n);
            t = (t >= a_mod) ? t - a_mod : t + n - a_mod;
            vk = t;
            vk1 = mulmod_u64(vk1, vk1, n);
            vk1 = (vk1 >= 2u) ? vk1 - 2u : vk1 + n - 2u;
        }
        else {
            uint64_t t = mulmod_u64(vk, vk1, n);
            t = (t >= a_mod) ? t - a_mod : t + n - a_mod;
            vk1 = t;
            vk = mulmod_u64(vk, vk, n);
            vk = (vk >= 2u) ? vk - 2u : vk + n - 2u;
        }
        mask >>= 1u;
    }
    return vk;
}

/** Williams p+1 factorisation (uint64 version). */
static FactorResult cpss_factor_pplus1(CPSSStream* s, uint64_t n, uint64_t B1) {
    FactorResult fr;
    factor_result_init(&fr, n, "pplus1");
    double t0 = now_sec();

    if (n <= 3u || (n & 1u) == 0u) {
        if (n <= 1u) { fr.fully_factored = true; fr.cofactor = n; }
        else if ((n & 1u) == 0u) {
            while (fr.cofactor % 2u == 0u) { factor_result_add(&fr, 2u); fr.cofactor /= 2u; }
            if (fr.cofactor > 1u && miller_rabin_u64(fr.cofactor)) {
                factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u;
            }
            fr.fully_factored = (fr.cofactor <= 1u);
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

    static const uint64_t starts[] = { 3, 5, 7, 11, 13, 17, 19, 23 };
    uint64_t stage1_v = 0u;
    for (size_t si = 0u; si < ARRAY_LEN(starts); ++si) {
        uint64_t v = starts[si] % n;
        PrimeIterator iter;
        cpss_iter_init(&iter, s, 2u);

        while (iter.valid && iter.current <= B1) {
            uint64_t p = iter.current;
            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            v = lucas_chain_u64(v, pk, n);

            uint64_t g = gcd_u64((v >= 2u) ? v - 2u : v + n - 2u, n);
            if (g > 1u && g < n) {
                fr.method = "pplus1(stage1)";
                factor_result_add(&fr, g);
                fr.cofactor = n / g;
                if (miller_rabin_u64(g) && miller_rabin_u64(fr.cofactor)) {
                    factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u; fr.fully_factored = true;
                } else if (miller_rabin_u64(fr.cofactor)) {
                    factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u; fr.fully_factored = true;
                }
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (g == n) break;

            cpss_iter_next(&iter);
        }
        stage1_v = v;
    }

    if (!fr.fully_factored && stage1_v > 0u) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                uint64_t vq = lucas_chain_u64(stage1_v, q, n);
                uint64_t g = gcd_u64((vq >= 2u) ? vq - 2u : vq + n - 2u, n);
                if (g > 1u && g < n) {
                    fr.method = "pplus1(stage2)";
                    factor_result_add(&fr, g);
                    fr.cofactor = n / g;
                    if (miller_rabin_u64(g) && miller_rabin_u64(fr.cofactor)) {
                        factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u; fr.fully_factored = true;
                    } else if (miller_rabin_u64(fr.cofactor)) {
                        factor_result_add(&fr, fr.cofactor); fr.cofactor = 1u; fr.fully_factored = true;
                    }
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (g == n) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/** Lucas chain for U128 modulus. */
static U128 lucas_chain_u128(U128 a, uint64_t e, U128 n) {
    if (e == 0u) return u128_from_u64(2u);
    if (e == 1u) return a;

    U128 two = u128_from_u64(2u);
    U128 a_mod = u128_mod(a, n);
    U128 vk = a_mod;
    U128 vk1 = u128_mulmod(a_mod, a_mod, n);
    if (u128_le(two, vk1)) vk1 = u128_sub(vk1, two);
    else vk1 = u128_sub(u128_add(vk1, n), two);

    uint64_t mask = (uint64_t)1u << 62u;
    while (mask > 0u && !(e & mask)) mask >>= 1u;
    mask >>= 1u;

    while (mask > 0u) {
        if (e & mask) {
            U128 t = u128_mulmod(vk, vk1, n);
            if (u128_le(a_mod, t)) t = u128_sub(t, a_mod);
            else t = u128_sub(u128_add(t, n), a_mod);
            vk = t;
            vk1 = u128_mulmod(vk1, vk1, n);
            if (u128_le(two, vk1)) vk1 = u128_sub(vk1, two);
            else vk1 = u128_sub(u128_add(vk1, n), two);
        }
        else {
            U128 t = u128_mulmod(vk, vk1, n);
            if (u128_le(a_mod, t)) t = u128_sub(t, a_mod);
            else t = u128_sub(u128_add(t, n), a_mod);
            vk1 = t;
            vk = u128_mulmod(vk, vk, n);
            if (u128_le(two, vk)) vk = u128_sub(vk, two);
            else vk = u128_sub(u128_add(vk, n), two);
        }
        mask >>= 1u;
    }
    return vk;
}

/** Williams p+1 for U128 n. */
static FactorResultU128 cpss_factor_pplus1_u128(CPSSStream* s, U128 n, uint64_t B1) {
    FactorResultU128 fr;
    factor_result_u128_init(&fr, n, "pplus1");
    double t0 = now_sec();

    if (u128_le(n, u128_from_u64(3u)) || (n.lo & 1u) == 0u) {
        if (u128_le(n, u128_from_u64(1u))) { fr.fully_factored = true; fr.cofactor = n; }
        else if ((n.lo & 1u) == 0u) {
            while (u128_mod_u64(fr.cofactor, 2u) == 0u) {
                factor_result_u128_add(&fr, 2u);
                fr.cofactor = u128_div(fr.cofactor, u128_from_u64(2u));
            }
            if (u128_fits_u64(fr.cofactor) && miller_rabin_u64(fr.cofactor.lo)) {
                factor_result_u128_add(&fr, fr.cofactor.lo); fr.cofactor = u128_from_u64(1u);
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

    static const uint64_t starts[] = { 3, 5, 7, 11, 13, 17, 19, 23 };
    U128 stage1_v = u128_from_u64(0u);
    for (size_t si = 0u; si < ARRAY_LEN(starts); ++si) {
        U128 v = u128_from_u64(starts[si]);
        PrimeIterator iter;
        cpss_iter_init(&iter, s, 2u);

        while (iter.valid && iter.current <= B1) {
            uint64_t p = iter.current;
            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            v = lucas_chain_u128(v, pk, n);

            U128 v_minus_2 = u128_le(u128_from_u64(2u), v) ? u128_sub(v, u128_from_u64(2u))
                : u128_sub(u128_add(v, n), u128_from_u64(2u));
            U128 g = u128_gcd(v_minus_2, n);

            if (u128_gt(g, u128_from_u64(1u)) && u128_lt(g, n)) {
                fr.method = "pplus1(stage1)";
                factor_result_u128_set_split(&fr, g, u128_div(n, g));
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (u128_eq(g, n)) break;
            cpss_iter_next(&iter);
        }
        stage1_v = v;
    }

    if (!fr.fully_factored && !u128_is_zero(stage1_v)) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                U128 vq = lucas_chain_u128(stage1_v, q, n);
                U128 vq_m2 = u128_le(u128_from_u64(2u), vq) ? u128_sub(vq, u128_from_u64(2u))
                    : u128_sub(u128_add(vq, n), u128_from_u64(2u));
                U128 g2 = u128_gcd(vq_m2, n);
                if (u128_gt(g2, u128_from_u64(1u)) && u128_lt(g2, n)) {
                    fr.method = "pplus1(stage2)";
                    factor_result_u128_set_split(&fr, g2, u128_div(n, g2));
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (u128_eq(g2, n)) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Lucas chain for BigNum modulus using Montgomery arithmetic.
 * Keeps the ladder state in Montgomery form so repeated Lucas composition can
 * reuse the same modular multiplication path as the BigNum p-1 engine.
 */
static void lucas_chain_bn(BigNum* result, const BigNum* a, uint64_t e,
    const MontCtx* ctx) {
    if (e == 0u) { bn_from_u64(result, 2u); return; }
    if (e == 1u) { bn_copy(result, a); return; }

    BigNum a_mont, two_mont;
    bn_to_mont(&a_mont, a, ctx);
    BigNum two_plain; bn_from_u64(&two_plain, 2u);
    bn_to_mont(&two_mont, &two_plain, ctx);

    BigNum vk_mont, vk1_mont;
    bn_copy(&vk_mont, &a_mont);
    bn_mont_mul(&vk1_mont, &a_mont, &a_mont, ctx);
    if (bn_cmp(&vk1_mont, &two_mont) >= 0)
        { BigNum tmp; bn_copy(&tmp, &vk1_mont); bn_sub(&tmp, &two_mont); bn_copy(&vk1_mont, &tmp); }
    else
        { BigNum tmp; bn_copy(&tmp, &vk1_mont); bn_add(&tmp, &ctx->n); bn_sub(&tmp, &two_mont); bn_copy(&vk1_mont, &tmp); }

    uint64_t mask = (uint64_t)1u << 62u;
    while (mask > 0u && !(e & mask)) mask >>= 1u;
    mask >>= 1u;

    while (mask > 0u) {
        if (e & mask) {
            BigNum t;
            bn_mont_mul(&t, &vk_mont, &vk1_mont, ctx);
            if (bn_cmp(&t, &a_mont) >= 0) bn_sub(&t, &a_mont);
            else { bn_add(&t, &ctx->n); bn_sub(&t, &a_mont); }
            bn_copy(&vk_mont, &t);
            bn_mont_mul(&vk1_mont, &vk1_mont, &vk1_mont, ctx);
            if (bn_cmp(&vk1_mont, &two_mont) >= 0) bn_sub(&vk1_mont, &two_mont);
            else { bn_add(&vk1_mont, &ctx->n); bn_sub(&vk1_mont, &two_mont); }
        }
        else {
            BigNum t;
            bn_mont_mul(&t, &vk_mont, &vk1_mont, ctx);
            if (bn_cmp(&t, &a_mont) >= 0) bn_sub(&t, &a_mont);
            else { bn_add(&t, &ctx->n); bn_sub(&t, &a_mont); }
            bn_copy(&vk1_mont, &t);
            bn_mont_mul(&vk_mont, &vk_mont, &vk_mont, ctx);
            if (bn_cmp(&vk_mont, &two_mont) >= 0) bn_sub(&vk_mont, &two_mont);
            else { bn_add(&vk_mont, &ctx->n); bn_sub(&vk_mont, &two_mont); }
        }
        mask >>= 1u;
    }

    bn_from_mont(result, &vk_mont, ctx);
}

/**
 * Williams p+1 for BigNum inputs using Montgomery Lucas chains.
 * Strips powers of two so the modulus is valid for Montgomery setup, then runs
 * the same seeded stage-1 / light stage-2 flow as the narrower variants.
 */
static FactorResultBigNum cpss_factor_pplus1_bn(CPSSStream* s, const BigNum* n, uint64_t B1) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "pplus1";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();

    if (bn_is_zero(n) || bn_is_one(n)) {
        fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr;
    }

    BigNum n_odd; bn_copy(&n_odd, n);
    while (bn_is_even(&n_odd) && !bn_is_zero(&n_odd)) bn_shr1(&n_odd);
    if (bn_is_one(&n_odd)) {
        bn_from_u64(&fr.factor, 2u); bn_from_u64(&fr.cofactor, 1u);
        fr.factor_found = true; fr.factor_is_prime = true; fr.cofactor_is_prime = true;
        fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr;
    }

    cpss_build_segment_index(s);
    uint64_t db_hi = 0u;
    if (s->index_len > 0u) db_hi = segment_end_n(&s->index_entries[s->index_len - 1u]);
    if (B1 == 0u) { B1 = 1000000u; if (db_hi > 0u && db_hi < B1) B1 = db_hi; }
    if (db_hi > 0u && B1 > db_hi) { B1 = db_hi; fr.status = (int)CPSS_INCOMPLETE; }
    if (db_hi == 0u) { fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr; }

    MontCtx ctx;
    if (!bn_montgomery_setup(&ctx, &n_odd)) {
        fr.status = (int)CPSS_UNAVAILABLE; fr.time_seconds = now_sec() - t0; return fr;
    }

    static const uint64_t starts[] = { 3, 5, 7, 11, 13, 17, 19, 23 };
    BigNum stage1_v_bn; bn_zero(&stage1_v_bn);
    for (size_t si = 0u; si < ARRAY_LEN(starts); ++si) {
        BigNum v; bn_from_u64(&v, starts[si]);
        PrimeIterator iter;
        cpss_iter_init(&iter, s, 2u);

        while (iter.valid && iter.current <= B1) {
            uint64_t p = iter.current;
            uint64_t pk = p;
            while (pk <= B1 / p) pk *= p;

            lucas_chain_bn(&v, &v, pk, &ctx);

            BigNum v_minus_2; bn_copy(&v_minus_2, &v);
            BigNum two_val; bn_from_u64(&two_val, 2u);
            if (bn_cmp(&v_minus_2, &two_val) >= 0) bn_sub(&v_minus_2, &two_val);
            else { bn_add(&v_minus_2, &n_odd); bn_sub(&v_minus_2, &two_val); }

            BigNum g;
            bn_gcd(&g, &v_minus_2, &n_odd);

            if (!bn_is_one(&g) && bn_cmp(&g, &n_odd) != 0) {
                fr.method = "pplus1(stage1)";
                bn_copy(&fr.factor, &g);
                bn_divexact(&fr.cofactor, &n_odd, &g);
                fr.factor_found = true;
                if (bn_fits_u64(&fr.factor)) fr.factor_is_prime = miller_rabin_u64(bn_to_u64(&fr.factor));
                else if (bn_fits_u128(&fr.factor)) fr.factor_is_prime = u128_is_prime(bn_to_u128(&fr.factor));
                if (bn_fits_u64(&fr.cofactor)) fr.cofactor_is_prime = miller_rabin_u64(bn_to_u64(&fr.cofactor));
                else if (bn_fits_u128(&fr.cofactor)) fr.cofactor_is_prime = u128_is_prime(bn_to_u128(&fr.cofactor));
                fr.fully_factored = (fr.factor_is_prime && fr.cofactor_is_prime);
                fr.time_seconds = now_sec() - t0;
                return fr;
            }
            if (bn_cmp(&g, &n_odd) == 0) break;
            cpss_iter_next(&iter);
        }
        bn_copy(&stage1_v_bn, &v);
    }

    if (!fr.factor_found && !bn_is_zero(&stage1_v_bn)) {
        uint64_t B2 = B1 * 100u;
        if (B2 > 10000000u) B2 = 10000000u;
        if (B2 > db_hi) B2 = db_hi;
        if (B2 > B1) {
            PrimeIterator iter2;
            cpss_iter_init(&iter2, s, B1 + 1u);
            while (iter2.valid && iter2.current <= B2) {
                uint64_t q = iter2.current;
                BigNum vq;
                lucas_chain_bn(&vq, &stage1_v_bn, q, &ctx);
                BigNum vq_m2; bn_copy(&vq_m2, &vq);
                BigNum two_val2; bn_from_u64(&two_val2, 2u);
                if (bn_cmp(&vq_m2, &two_val2) >= 0) bn_sub(&vq_m2, &two_val2);
                else { bn_add(&vq_m2, &n_odd); bn_sub(&vq_m2, &two_val2); }
                BigNum g2; bn_gcd(&g2, &vq_m2, &n_odd);
                if (!bn_is_one(&g2) && bn_cmp(&g2, &n_odd) != 0) {
                    fr.method = "pplus1(stage2)";
                    bn_copy(&fr.factor, &g2);
                    bn_divexact(&fr.cofactor, &n_odd, &g2);
                    fr.factor_found = true;
                    if (bn_fits_u64(&fr.factor)) fr.factor_is_prime = miller_rabin_u64(bn_to_u64(&fr.factor));
                    else if (bn_fits_u128(&fr.factor)) fr.factor_is_prime = u128_is_prime(bn_to_u128(&fr.factor));
                    if (bn_fits_u64(&fr.cofactor)) fr.cofactor_is_prime = miller_rabin_u64(bn_to_u64(&fr.cofactor));
                    else if (bn_fits_u128(&fr.cofactor)) fr.cofactor_is_prime = u128_is_prime(bn_to_u128(&fr.cofactor));
                    fr.fully_factored = (fr.factor_is_prime && fr.cofactor_is_prime);
                    fr.time_seconds = now_sec() - t0;
                    return fr;
                }
                if (bn_cmp(&g2, &n_odd) == 0) break;
                cpss_iter_next(&iter2);
            }
        }
    }

    if (!fr.factor_found && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}

/**
 * Database-backed Williams p+1 variant.
 * Uses shard-aware prime enumeration but otherwise matches the streamed uint64
 * engine's seed schedule, stage-1 walk, and completion semantics.
 */
static FactorResult cpss_db_factor_pplus1(CPSSDatabase* db, uint64_t n, uint64_t B1) {
    FactorResult fr;
    factor_result_init(&fr, n, "pplus1");
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
            static const uint64_t starts[] = { 3, 5, 7, 11, 13, 17, 19, 23 };
            for (size_t si = 0u; si < ARRAY_LEN(starts); ++si) {
                uint64_t v = starts[si] % n;
                uint64_t current = 1u;

                while (current < B1) {
                    CPSSQueryStatus qs = CPSS_OK;
                    uint64_t p = cpss_db_next_prime_s(db, current, &qs);
                    if (qs != CPSS_OK) {
                        if (qs != CPSS_OUT_OF_RANGE) fr.status = (int)qs;
                        break;
                    }
                    if (p > B1) break;

                    {
                        uint64_t pk = p;
                        while (pk <= B1 / p) pk *= p;
                        v = lucas_chain_u64(v, pk, n);
                    }

                    {
                        uint64_t g = gcd_u64((v >= 2u) ? v - 2u : v + n - 2u, n);
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
            }
        }
    }

    if (!fr.fully_factored && fr.status == (int)CPSS_OK) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}
