/**
 * SemiPrimeCorpusSpace.c - Corpus generation and validation.
 * Part of the CPSS SemiPrime Spaces module (N-centric inference).
 *
 * Semiprime corpus generation (balanced, skewed, rsa-like, etc.),
 * corpus validation with regime degeneracy and smoothness checks.
 *
 * Depends on: SemiPrimeRoutingSpace.c (MNAMES, classify_shape_posthoc)
 */

/* ── Corpus helpers ── */

static uint64_t corpus_xorshift(uint64_t* state) {
    uint64_t x = *state; x ^= x << 13u; x ^= x >> 7u; x ^= x << 17u; *state = x; return x;
}
static uint64_t corpus_rand_bits(uint64_t* rng, int bits) {
    if (bits <= 1) return 2u; if (bits > 62) bits = 62;
    uint64_t lo = (uint64_t)1u << (bits - 1);
    return (lo + (corpus_xorshift(rng) % lo)) | 1u;
}
static uint64_t corpus_next_prime(uint64_t n) {
    if (n <= 2u) return 2u; if ((n & 1u) == 0u) ++n;
    while (!miller_rabin_u64(n)) n += 2u; return n;
}

/** Generate a semiprime pair for the given shape. Returns false on failure. */
static bool corpus_gen_pair(uint64_t* rng, int half, int bits, const char* shape,
                             uint64_t* out_p, uint64_t* out_q) {
    uint64_t p = 0u, q = 0u;
    if (strcmp(shape, "balanced") == 0) {
        p = corpus_next_prime(corpus_rand_bits(rng, half));
        q = corpus_next_prime(corpus_rand_bits(rng, half));
    } else if (strcmp(shape, "skewed") == 0) {
        int sb = half / 2; if (sb < 2) sb = 2;
        p = corpus_next_prime(corpus_rand_bits(rng, sb));
        q = corpus_next_prime(corpus_rand_bits(rng, bits - sb));
    } else if (strcmp(shape, "banded") == 0) {
        p = corpus_next_prime(corpus_rand_bits(rng, half));
        q = corpus_next_prime(p + 2u + (corpus_xorshift(rng) % 20u));
    } else if (strcmp(shape, "rsa-like") == 0) {
        p = corpus_next_prime(corpus_rand_bits(rng, half));
        q = corpus_next_prime(corpus_rand_bits(rng, half));
        while (q == p) q = corpus_next_prime(q + 2u);
    } else if (strcmp(shape, "no-wheel-factor") == 0) {
        int min_bits = half; if (min_bits < 5) min_bits = 5;
        p = corpus_next_prime(corpus_rand_bits(rng, min_bits));
        while (p <= WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u]) p = corpus_next_prime(p + 2u);
        q = corpus_next_prime(corpus_rand_bits(rng, min_bits));
        while (q <= WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] || q == p) q = corpus_next_prime(q + 2u);
    } else if (strcmp(shape, "pminus1-smooth") == 0) {
        /* Construct BOTH p and q with smooth p-1/q-1.
           Strategy: build n-1 from small prime powers, then check if n-1+1 is prime. */
        uint64_t* targets[2] = { &p, &q };
        for (int ti = 0; ti < 2; ++ti) {
            *targets[ti] = 0u;
            for (int attempt = 0; attempt < 200; ++attempt) {
                uint64_t nm1 = 2u;
                while (nm1 < ((uint64_t)1u << (half - 2))) {
                    uint64_t small_p = 2u + (corpus_xorshift(rng) % 12u);
                    if (!miller_rabin_u64(small_p)) continue;
                    nm1 *= small_p;
                    if (nm1 > ((uint64_t)1u << half)) break;
                }
                uint64_t cand = nm1 + 1u;
                if (miller_rabin_u64(cand) && cand > 13u) { *targets[ti] = cand; break; }
            }
            if (*targets[ti] == 0u) *targets[ti] = corpus_next_prime(corpus_rand_bits(rng, half));
        }
        while (q == p) q = corpus_next_prime(q + 2u);
    } else if (strcmp(shape, "pminus1-hostile") == 0) {
        /* Construct BOTH p and q as safe-prime-like: n = 2*large_prime + 1.
           This makes n-1 = 2*large_prime, so largest factor of n-1 is large_prime > B1
           for sufficiently large bit sizes. More attempts to reduce fallback rate. */
        uint64_t* targets[2] = { &p, &q };
        for (int ti = 0; ti < 2; ++ti) {
            *targets[ti] = 0u;
            for (int attempt = 0; attempt < 200; ++attempt) {
                uint64_t inner = corpus_next_prime(corpus_rand_bits(rng, half - 1));
                uint64_t cand = 2u * inner + 1u;
                if (miller_rabin_u64(cand) && cand > 13u) { *targets[ti] = cand; break; }
            }
            if (*targets[ti] == 0u) *targets[ti] = corpus_next_prime(corpus_rand_bits(rng, half));
        }
        while (q == p) q = corpus_next_prime(q + 2u);
    } else { /* mixed */
        int r = (int)(corpus_xorshift(rng) % 4u);
        if (r == 0) return corpus_gen_pair(rng, half, bits, "balanced", out_p, out_q);
        if (r == 1) return corpus_gen_pair(rng, half, bits, "skewed", out_p, out_q);
        if (r == 2) return corpus_gen_pair(rng, half, bits, "banded", out_p, out_q);
        return corpus_gen_pair(rng, half, bits, "rsa-like", out_p, out_q);
    }
    if (p > q) { uint64_t t = p; p = q; q = t; }
    U128 prod = u128_mul64(p, q);
    if (!u128_fits_u64(prod)) return false;
    if (prod.lo <= 3u) return false;
    *out_p = p; *out_q = q;
    return true;
}

static void corpus_generate_semiprime(const char* outpath, int cases, int bits,
                                       uint64_t seed, const char* shape) {
    FILE* fp = fopen(outpath, "w");
    if (!fp) { printf("Cannot open '%s' for writing.\n", outpath); return; }
    fprintf(fp, "p,q,N,bits_p,bits_q,Delta,R,log_imbalance,shape\n");
    uint64_t rng = seed ? seed : 0x12345678ABCDEF01ULL;
    int half = bits / 2; if (half < 2) half = 2;
    int written = 0;
    for (int ci = 0; ci < cases * 10 && written < cases; ++ci) {
        uint64_t p, q;
        if (!corpus_gen_pair(&rng, half, bits, shape, &p, &q)) continue;
        uint64_t N = u128_mul64(p, q).lo;
        unsigned bp = 0, bq = 0;
        { uint64_t v = p; while (v) { ++bp; v >>= 1u; } }
        { uint64_t v = q; while (v) { ++bq; v >>= 1u; } }
        fprintf(fp, "%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%"PRIu64",%.6f,%.6f,%s\n",
            p, q, N, bp, bq, q - p, (double)q / (double)p, 0.5 * log((double)q / (double)p), shape);
        ++written;
    }
    fclose(fp);
    printf("generated %d semiprimes to %s (shape=%s, bits=%d, seed=%"PRIu64")\n", written, outpath, shape, bits, seed);
}

/* ── BigNum corpus generation (Phase 15) ── */

/** Find next probable prime >= n (BigNum). */
static void corpus_next_prime_bn(BigNum* n) {
    if (bn_is_even(n)) bn_add_u64(n, 1u);
    while (!bn_is_probable_prime(n, 12u)) bn_add_u64(n, 2u);
}

/** Generate a random BigNum with exactly `bits` bits, odd. */
static void corpus_rand_bits_bn(BigNum* out, int bits, uint64_t* rng) {
    if (bits <= 1) { bn_from_u64(out, 3u); return; }
    bn_random_bits(out, (unsigned)bits, rng);
    /* Ensure high bit is set */
    if ((int)bn_bitlen(out) < bits) {
        BigNum hi; bn_from_u64(&hi, 1u);
        for (int i = 0; i < bits - 1; ++i) bn_shl1(&hi);
        bn_add(out, &hi);
    }
    /* Ensure odd */
    out->limbs[0] |= 1u;
}

/** Generate a BigNum semiprime pair for the given shape. */
static bool corpus_gen_pair_bn(uint64_t* rng, int half, int bits, const char* shape,
                                BigNum* out_p, BigNum* out_q) {
    BigNum p, q;
    bn_zero(&p); bn_zero(&q);

    if (strcmp(shape, "balanced") == 0 || strcmp(shape, "rsa-like") == 0) {
        corpus_rand_bits_bn(&p, half, rng);
        corpus_next_prime_bn(&p);
        corpus_rand_bits_bn(&q, half, rng);
        corpus_next_prime_bn(&q);
        /* rsa-like: ensure p != q */
        while (bn_cmp(&p, &q) == 0) bn_add_u64(&q, 2u);
        if (strcmp(shape, "rsa-like") == 0) corpus_next_prime_bn(&q);
    } else if (strcmp(shape, "skewed") == 0) {
        int sb = half / 2; if (sb < 2) sb = 2;
        corpus_rand_bits_bn(&p, sb, rng);
        corpus_next_prime_bn(&p);
        corpus_rand_bits_bn(&q, bits - sb, rng);
        corpus_next_prime_bn(&q);
    } else if (strcmp(shape, "banded") == 0) {
        corpus_rand_bits_bn(&p, half, rng);
        corpus_next_prime_bn(&p);
        bn_copy(&q, &p);
        bn_add_u64(&q, 2u + (corpus_xorshift(rng) % 20u));
        corpus_next_prime_bn(&q);
    } else { /* mixed */
        int r = (int)(corpus_xorshift(rng) % 4u);
        const char* sub = r == 0 ? "balanced" : r == 1 ? "skewed" : r == 2 ? "banded" : "rsa-like";
        return corpus_gen_pair_bn(rng, half, bits, sub, out_p, out_q);
    }

    /* Ensure p <= q */
    if (bn_cmp(&p, &q) > 0) { BigNum t; bn_copy(&t, &p); bn_copy(&p, &q); bn_copy(&q, &t); }

    /* Verify product fits in BigNum */
    BigNum prod; bn_mul(&prod, &p, &q);
    if (bn_is_zero(&prod)) return false;

    bn_copy(out_p, &p);
    bn_copy(out_q, &q);
    return true;
}

/** BigNum corpus generation: writes decimal p,q,N to CSV. */
static void corpus_generate_semiprime_bn(const char* outpath, int cases, int bits,
                                          uint64_t seed, const char* shape) {
    FILE* fp = fopen(outpath, "w");
    if (!fp) { printf("Cannot open '%s' for writing.\n", outpath); return; }
    fprintf(fp, "p,q,N,bits_p,bits_q,shape\n");
    uint64_t rng = seed ? seed : 0x12345678ABCDEF01ULL;
    int half = bits / 2; if (half < 2) half = 2;
    int written = 0;
    for (int ci = 0; ci < cases * 10 && written < cases; ++ci) {
        BigNum p, q;
        if (!corpus_gen_pair_bn(&rng, half, bits, shape, &p, &q)) continue;
        BigNum N; bn_mul(&N, &p, &q);
        char pbuf[512], qbuf[512], nbuf[512];
        bn_to_str(&p, pbuf, sizeof(pbuf));
        bn_to_str(&q, qbuf, sizeof(qbuf));
        bn_to_str(&N, nbuf, sizeof(nbuf));
        fprintf(fp, "%s,%s,%s,%u,%u,%s\n", pbuf, qbuf, nbuf, bn_bitlen(&p), bn_bitlen(&q), shape);
        ++written;
    }
    fclose(fp);
    printf("generated %d BigNum semiprimes to %s (shape=%s, bits=%d, seed=%"PRIu64")\n", written, outpath, shape, bits, seed);
}

/* ── Corpus validation (Phase 12a) ── */

/** Validate a corpus CSV: regime degeneracy, per-row smoothness, label truth. */
static void corpus_validate(CPSSStream* s, const char* inpath, uint64_t B1) {
    FILE* fp = fopen(inpath, "r");
    if (!fp) { printf("Cannot open '%s'.\n", inpath); return; }
    if (B1 == 0u) B1 = 1000000u;

    /* Read header */
    char line_buf[512];
    if (!fgets(line_buf, sizeof(line_buf), fp)) { fclose(fp); return; }

    /* Detect bit range from first data line for regime analysis */
    long first_data_pos = ftell(fp);
    uint64_t max_factor_seen = 0u;
    int row_count = 0;
    while (fgets(line_buf, sizeof(line_buf), fp)) {
        uint64_t p = 0u, q = 0u;
        if (sscanf(line_buf, "%"PRIu64",%"PRIu64, &p, &q) < 2) continue;
        if (p > max_factor_seen) max_factor_seen = p;
        if (q > max_factor_seen) max_factor_seen = q;
        ++row_count;
    }
    unsigned max_bits = 0;
    { uint64_t v = max_factor_seen; while (v) { ++max_bits; v >>= 1u; } }
    bool degenerate = (max_factor_seen > 0u && max_factor_seen < B1);

    printf("corpus-validate: %s (%d rows)\n", inpath, row_count);
    printf("  B1               = %"PRIu64"\n", B1);
    printf("  max factor seen  = %"PRIu64" (~%u bits)\n", max_factor_seen, max_bits);
    if (degenerate) {
        printf("  WARNING: all factors < B1. hostile/smooth distinction is DEGENERATE.\n");
        printf("  Recommendation: use larger --bits or smaller --B1 (e.g. --B1 %u).\n",
            max_bits > 0 ? (1u << (max_bits > 1 ? max_bits - 1 : 0)) : 50u);
    } else {
        printf("  regime           = non-degenerate (factors can exceed B1)\n");
    }

    /* Second pass: per-row analysis */
    fseek(fp, first_data_pos, SEEK_SET);
    int valid = 0, invalid = 0;
    int pm1_p_smooth = 0, pm1_q_smooth = 0, pm1_both_smooth = 0, pm1_neither = 0;
    double sum_pm1_p_ratio = 0.0, sum_pm1_q_ratio = 0.0;
    double min_pm1_p_ratio = 1e30, max_pm1_p_ratio = 0.0;
    double min_pm1_q_ratio = 1e30, max_pm1_q_ratio = 0.0;
    int ridx = 0;

    printf("\n  %-4s %-8s %-8s %-12s %-6s %-12s %-6s %-12s %-6s %-12s %-6s %-5s %-5s %-6s %-8s\n",
        "#", "p", "q", "pm1_lpf(p)", "B1sm", "pm1_lpf(q)", "B1sm",
        "pp1_lpf(p)", "B1sm", "pp1_lpf(q)", "B1sm", "pm1?", "pp1?", "valid", "shape");

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        uint64_t p = 0u, q = 0u, N = 0u;
        if (sscanf(line_buf, "%"PRIu64",%"PRIu64",%"PRIu64, &p, &q, &N) < 3) continue;
        if (p == 0u || q == 0u) continue;
        if (p > q) { uint64_t t = p; p = q; q = t; }
        ++ridx;

        /* Extract corpus_shape from last CSV column */
        char c_shape[64] = "unknown";
        { const char* lc = strrchr(line_buf, ',');
          if (lc) { strncpy(c_shape, lc + 1, sizeof(c_shape) - 1);
            c_shape[sizeof(c_shape) - 1] = '\0';
            size_t sl = strlen(c_shape);
            while (sl > 0 && (c_shape[sl-1] == '\n' || c_shape[sl-1] == '\r' || c_shape[sl-1] == ' ')) c_shape[--sl] = '\0';
          }
        }

        /* Compute largest prime factor and B1-smoothness for p-1, q-1, p+1, q+1 */
        LargestFactorResult pm1_p = largest_prime_factor_of(s, p > 1u ? p - 1u : 1u, B1);
        LargestFactorResult pm1_q = largest_prime_factor_of(s, q > 1u ? q - 1u : 1u, B1);
        LargestFactorResult pp1_p = largest_prime_factor_of(s, p + 1u, B1);
        LargestFactorResult pp1_q = largest_prime_factor_of(s, q + 1u, B1);

        bool pm1_susceptible = pm1_p.all_below_B1 || pm1_q.all_below_B1;
        bool pp1_susceptible = pp1_p.all_below_B1 || pp1_q.all_below_B1;

        /* Label truth check */
        bool label_ok = true;
        double R = (double)q / (double)p;
        if (strcmp(c_shape, "balanced") == 0)         label_ok = (R < 2.0);
        else if (strcmp(c_shape, "skewed") == 0)      label_ok = (R >= 4.0);
        else if (strcmp(c_shape, "banded") == 0)      label_ok = ((q - p) <= 40u);
        else if (strcmp(c_shape, "rsa-like") == 0)    label_ok = (p != q && R < 4.0);
        else if (strcmp(c_shape, "no-wheel-factor") == 0) {
            uint64_t sp = p < q ? p : q;
            label_ok = (sp > WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u]);
        }
        else if (strcmp(c_shape, "pminus1-smooth") == 0) label_ok = pm1_susceptible;
        else if (strcmp(c_shape, "pminus1-hostile") == 0) label_ok = (!pm1_susceptible && !pp1_susceptible);
        /* mixed always valid */

        if (label_ok) ++valid; else ++invalid;

        /* Accumulate p-1 smoothness stats */
        double pm1_p_ratio = (p > 2u) ? (double)pm1_p.largest / (double)(p - 1u) : 0.0;
        double pm1_q_ratio = (q > 2u) ? (double)pm1_q.largest / (double)(q - 1u) : 0.0;
        sum_pm1_p_ratio += pm1_p_ratio; sum_pm1_q_ratio += pm1_q_ratio;
        if (pm1_p_ratio < min_pm1_p_ratio) min_pm1_p_ratio = pm1_p_ratio;
        if (pm1_p_ratio > max_pm1_p_ratio) max_pm1_p_ratio = pm1_p_ratio;
        if (pm1_q_ratio < min_pm1_q_ratio) min_pm1_q_ratio = pm1_q_ratio;
        if (pm1_q_ratio > max_pm1_q_ratio) max_pm1_q_ratio = pm1_q_ratio;
        if (pm1_p.all_below_B1 && pm1_q.all_below_B1) ++pm1_both_smooth;
        else if (pm1_p.all_below_B1) ++pm1_p_smooth;
        else if (pm1_q.all_below_B1) ++pm1_q_smooth;
        else ++pm1_neither;

        printf("  %-4d %-8"PRIu64" %-8"PRIu64" %-12"PRIu64" %-6s %-12"PRIu64" %-6s %-12"PRIu64" %-6s %-12"PRIu64" %-6s %-5s %-5s %-6s %s\n",
            ridx, p, q,
            pm1_p.largest, pm1_p.all_below_B1 ? "yes" : "NO",
            pm1_q.largest, pm1_q.all_below_B1 ? "yes" : "NO",
            pp1_p.largest, pp1_p.all_below_B1 ? "yes" : "NO",
            pp1_q.largest, pp1_q.all_below_B1 ? "yes" : "NO",
            pm1_susceptible ? "yes" : "NO",
            pp1_susceptible ? "yes" : "NO",
            label_ok ? "PASS" : "FAIL", c_shape);
    }
    fclose(fp);

    /* Summary */
    printf("\n  summary:\n");
    if (degenerate) printf("    WARNING: degenerate regime (all factors < B1=%"PRIu64")\n", B1);
    printf("    total rows     = %d\n", ridx);
    printf("    label valid    = %d  invalid = %d  (%.0f%% valid)\n",
        valid, invalid, ridx > 0 ? 100.0 * valid / ridx : 0.0);
    printf("    p-1 smoothness breakdown (B1=%"PRIu64"):\n", B1);
    printf("      p-1 smooth only  = %d\n", pm1_p_smooth);
    printf("      q-1 smooth only  = %d\n", pm1_q_smooth);
    printf("      both smooth      = %d\n", pm1_both_smooth);
    printf("      neither smooth   = %d\n", pm1_neither);
    if (ridx > 0) {
        printf("    p-1 largest-factor ratio (p-1):\n");
        printf("      min=%.6f  max=%.6f  mean=%.6f\n", min_pm1_p_ratio, max_pm1_p_ratio, sum_pm1_p_ratio / ridx);
        printf("    p-1 largest-factor ratio (q-1):\n");
        printf("      min=%.6f  max=%.6f  mean=%.6f\n", min_pm1_q_ratio, max_pm1_q_ratio, sum_pm1_q_ratio / ridx);
    }
    if (strcmp("pminus1-hostile", "") != 0) { /* always print this note for clarity */
        printf("    NOTE: 'pminus1-hostile' label requires BOTH pm1 and pp1 to be non-susceptible\n");
        printf("          (neither p-1/q-1 nor p+1/q+1 is B1-smooth).\n");
    }
}
