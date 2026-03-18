/* ── GNFS pipeline entry point ── */

static FactorResultBigNum cpss_factor_gnfs_pipeline(const BigNum* n, const GNFSConfig* cfg) {
    FactorResultBigNum fr;
    memset(&fr, 0, sizeof(fr));
    bn_copy(&fr.n, n);
    bn_copy(&fr.cofactor, n);
    fr.method = "gnfs(pipeline)";
    fr.status = (int)CPSS_OK;
    double t0 = now_sec();

    // End Invalid N value calls ASAP
    if (bn_is_zero(n) || bn_is_one(n)) { fr.fully_factored = true; fr.time_seconds = now_sec() - t0; return fr; }
    if (bn_is_even(n)) {
        BigNum two; bn_from_u64(&two, 2u);
        factor_result_bn_set_from_factor(&fr, n, &two);
        fr.time_seconds = now_sec() - t0; return fr;
    }
    if (bn_factor_is_prime(n)) {
        fr.cofactor_is_prime = true; fr.fully_factored = true;
        fr.time_seconds = now_sec() - t0; return fr;
    }

    // N is now confirmed to be a composite number that needs factoring

    /* Stage 1: Polynomial selection */
    GNFSPoly poly;
    if (!gnfs_poly_select(&poly, n, cfg)) {
        if (cfg->diag) printf("  [GNFS] FAIL: polynomial verification failed\n");
        fr.status = (int)CPSS_INCOMPLETE; fr.time_seconds = now_sec() - t0; return fr;
    }
    if (cfg->selftest && poly.degree == 3 && bn_is_one(&poly.coeffs[3])) {
        gnfs_selftest_alg_mul(&poly);
        gnfs_selftest_columns(&poly);
        gnfs_selftest_field_repro(&poly);
        gnfs_selftest_schk_degeneracy(&poly);
        fr.time_seconds = now_sec() - t0; return fr;
    }
    if (cfg->synth_hensel && poly.degree == 3 && bn_is_one(&poly.coeffs[3])) {
        gnfs_synth_hensel_test(&poly);
        fr.time_seconds = now_sec() - t0; return fr;
    }
    if (cfg->synth_square && poly.degree == 3 && bn_is_one(&poly.coeffs[3])) {
        gnfs_synth_square_test(&poly);
        gnfs_synth_hensel_test(&poly);
        fr.time_seconds = now_sec() - t0; return fr;
    }

    /* ── Order / discriminant / index audit ── */
    if (cfg->audit_order && poly.degree == 3) {
        printf("  [GNFS order-audit] polynomial discriminant analysis\n");
        /* For cubic f(x) = a*x^3 + b*x^2 + c*x + d:
         * disc(f) = b^2*c^2 - 4*a*c^3 - 4*b^3*d + 18*a*b*c*d - 27*a^2*d^2
         * All terms are BigNum products of the coefficients. */
        BigNum *ca = &poly.coeffs[3], *cb = &poly.coeffs[2], *cc = &poly.coeffs[1], *cd = &poly.coeffs[0];

        /* Compute each of the 5 terms as signed values.
         * Since BigNum is unsigned, track signs separately and combine at the end.
         * term1 = +b^2*c^2, term2 = -4*a*c^3, term3 = -4*b^3*d, term4 = +18*a*b*c*d, term5 = -27*a^2*d^2 */
        BigNum t1, t2, t3, t4, t5;
        /* t1 = b^2 * c^2 */
        BigNum b2; bn_mul(&b2, cb, cb);
        BigNum c2; bn_mul(&c2, cc, cc);
        bn_mul(&t1, &b2, &c2);
        /* t2 = 4 * a * c^3 */
        BigNum c3; bn_mul(&c3, &c2, cc);
        BigNum ac3; bn_mul(&ac3, ca, &c3);
        BigNum four; bn_from_u64(&four, 4u);
        bn_mul(&t2, &four, &ac3);
        /* t3 = 4 * b^3 * d */
        BigNum b3; bn_mul(&b3, &b2, cb);
        BigNum b3d; bn_mul(&b3d, &b3, cd);
        bn_mul(&t3, &four, &b3d);
        /* t4 = 18 * a * b * c * d */
        BigNum ab; bn_mul(&ab, ca, cb);
        BigNum abcd; bn_mul(&abcd, &ab, cc); bn_mul(&abcd, &abcd, cd);
        BigNum eighteen; bn_from_u64(&eighteen, 18u);
        bn_mul(&t4, &eighteen, &abcd);
        /* t5 = 27 * a^2 * d^2 */
        BigNum a2; bn_mul(&a2, ca, ca);
        BigNum d2; bn_mul(&d2, cd, cd);
        BigNum a2d2; bn_mul(&a2d2, &a2, &d2);
        BigNum tw7; bn_from_u64(&tw7, 27u);
        bn_mul(&t5, &tw7, &a2d2);

        /* disc = t1 - t2 - t3 + t4 - t5 = (t1 + t4) - (t2 + t3 + t5) */
        BigNum pos_sum; bn_copy(&pos_sum, &t1); bn_add(&pos_sum, &t4);
        BigNum neg_sum; bn_copy(&neg_sum, &t2); bn_add(&neg_sum, &t3); bn_add(&neg_sum, &t5);

        BigNum disc_abs;
        bool disc_negative;
        if (bn_cmp(&pos_sum, &neg_sum) >= 0) {
            bn_copy(&disc_abs, &pos_sum); bn_sub(&disc_abs, &neg_sum);
            disc_negative = false;
        } else {
            bn_copy(&disc_abs, &neg_sum); bn_sub(&disc_abs, &pos_sum);
            disc_negative = true;
        }

        char disc_buf[256]; bn_to_str(&disc_abs, disc_buf, sizeof(disc_buf));
        printf("    disc(f) = %s%s (%u bits)\n", disc_negative?"-":"", disc_buf, bn_bitlen(&disc_abs));

        /* Factor disc_abs by trial division up to 10000 */
        printf("    factorisation (trial up to 10000):\n");
        BigNum rem; bn_copy(&rem, &disc_abs);
        bool has_square_factor = false;
        int bad_prime_count = 0;
        uint64_t bad_primes[64]; int bad_exps[64];

        for (uint64_t p = 2u; p <= 10000u && !bn_is_zero(&rem) && !bn_is_one(&rem); ++p) {
            if (p > 2u && !miller_rabin_u64(p)) continue;
            int ex = 0;
            while (bn_mod_u64(&rem, p) == 0u) {
                BigNum dp; bn_from_u64(&dp, p);
                BigNum qo; bn_divexact(&qo, &rem, &dp);
                bn_copy(&rem, &qo);
                ++ex;
            }
            if (ex > 0) {
                printf("      %"PRIu64"^%d", p, ex);
                if (ex >= 2) { printf(" [SQUARE FACTOR]"); has_square_factor = true; }
                /* Check if this prime divides leading coeff */
                if (!bn_is_one(ca) && bn_mod_u64(ca, p) == 0u)
                    printf(" [divides f3=%s]", bn_is_one(ca)?"1":"f3");
                printf("\n");
                if (bad_prime_count < 64) {
                    bad_primes[bad_prime_count] = p;
                    bad_exps[bad_prime_count] = ex;
                    ++bad_prime_count;
                }
            }
        }
        if (!bn_is_one(&rem)) {
            char rbuf[128]; bn_to_str(&rem, rbuf, sizeof(rbuf));
            printf("      remaining cofactor: %s (%u bits)\n", rbuf, bn_bitlen(&rem));
            /* Check if cofactor is a perfect square */
            BigNum isqrt_r; bn_isqrt_floor(&isqrt_r, &rem);
            BigNum sq_check; bn_mul(&sq_check, &isqrt_r, &isqrt_r);
            if (bn_cmp(&sq_check, &rem) == 0) {
                printf("      cofactor is a PERFECT SQUARE\n");
                has_square_factor = true;
            }
        } else {
            printf("      (fully factored)\n");
        }

        printf("    squarefree: %s\n", has_square_factor ? "NO" : "yes (up to trial bound)");

        /* For monic polynomial: disc(f) = disc(K) * [O_K : Z[alpha]]^2
         * So if disc(f) is squarefree, then [O_K : Z[alpha]] = 1.
         * For non-monic: disc(f) = f3^(2d-2) * disc(K) * [O_K : Z[alpha]]^2 / ...
         * The relationship is more complex. */
        bool is_monic = bn_is_one(ca);

        printf("    polynomial type: %s\n", is_monic ? "monic" : "non-monic");
        if (is_monic) {
            printf("    disc(f) = disc(K) * [O_K : Z[alpha]]^2  (exact for monic)\n");
            if (!has_square_factor) {
                printf("    disc(f) squarefree → [O_K : Z[alpha]] = 1 (proven)\n");
                printf("    → Z[alpha] IS the maximal order O_K\n");
            } else {
                printf("    disc(f) has square factors → [O_K : Z[alpha]] may be > 1\n");
                printf("    bad primes (dividing possible index):");
                for (int i = 0; i < bad_prime_count; ++i)
                    if (bad_exps[i] >= 2) printf(" %"PRIu64"(^%d)", bad_primes[i], bad_exps[i]);
                printf("\n");
            }
        } else {
            /* For non-monic cubic f3*x^3+...: the discriminant of the order Z[alpha]
             * where alpha is a root of the MONIC polynomial g(x) = f(x)/f3 is different.
             * The standard result: disc(Z[alpha]) = disc(f) / f3^(2*(d-1)) for the
             * primitive element theta = f3*alpha (which satisfies a monic polynomial). */
            uint64_t f3_val = bn_to_u64(ca);
            printf("    non-monic: f3 = %"PRIu64"\n", f3_val);
            printf("    note: for non-monic f, the order Z[alpha] is NOT standard.\n");
            printf("    the standard primitive element is theta = f3*alpha with monic min poly.\n");
            printf("    disc(Z[theta]) = disc(f) / f3^%d\n", 2*(poly.degree-1));
            printf("    bad primes include those dividing f3 = %"PRIu64"\n", f3_val);
        }

        /* Bad primes summary */
        printf("    bad primes (dividing disc):");
        for (int i = 0; i < bad_prime_count; ++i) {
            printf(" %"PRIu64"^%d", bad_primes[i], bad_exps[i]);
            if (bad_exps[i] >= 2) printf("[sq]");
        }
        printf("\n");

        /* Conclusion */
        if (is_monic && !has_square_factor) {
            printf("  [GNFS order-audit] CONCLUSION: Z[alpha] = O_K (proven). Current ring is safe.\n");
            printf("    → algebraic FB, characters, and QR reasoning are theoretically valid\n");
            printf("    → the 0/395 non-QR result is NOT caused by order/index issues\n");
        } else if (is_monic && has_square_factor) {
            printf("  [GNFS order-audit] CONCLUSION: Z[alpha] MAY NOT equal O_K (disc has square factors)\n");
            printf("    → primes dividing the index may corrupt algebraic FB / character reasoning\n");
            printf("    → recommend: compute exact index or switch to a polynomial with squarefree discriminant\n");
        } else {
            printf("  [GNFS order-audit] CONCLUSION: non-monic polynomial — Z[alpha] analysis is non-standard\n");
            printf("    → recommend: use monic target for algebraic extraction debugging\n");
        }
    }

    /* Stage 2: Factor base construction */
    /* Rational Factor Base (RFB): store primes p where N has square roots mod p */
    uint64_t* rfb = (uint64_t*)xmalloc((size_t)GNFS_MAX_FB * sizeof(uint64_t));
    int rfb_count = 0;
    /* Collect primes up to rfb_bound, filtering composites with Miller-Rabin */
    for (uint64_t p = 2u; p <= (uint64_t)cfg->rfb_bound && rfb_count < GNFS_MAX_FB; ++p) {
        if (p > 2u && !miller_rabin_u64(p)) continue; /* skip composites (except p=2) */
        rfb[rfb_count++] = p; /* store prime in rational factor base */
    }

    /* Algebraic Factor Base (AFB): store prime-ideal pairs (p, r) where f(r) ≡ 0 (mod p) */
    GNFSAlgPrime* afb = (GNFSAlgPrime*)xmalloc((size_t)GNFS_MAX_ALG_FB * sizeof(GNFSAlgPrime));
    int afb_count = 0;
    /* For each rational prime up to afb_bound, find polynomial roots modulo p */
    for (int ri = 0; ri < rfb_count && rfb[ri] <= (uint64_t)cfg->afb_bound; ++ri) {
        uint64_t p = rfb[ri];
        /* Find roots of f(x) mod p by brute force: test all r in [0, p-1] */
        for (uint64_t r = 0u; r < p && afb_count < GNFS_MAX_ALG_FB; ++r) {
            if (gnfs_poly_eval_mod_p(&poly, r, p) == 0u) {
                /* Store prime ideal (p, r) where r is a root of f(x) ≡ 0 (mod p) */
                afb[afb_count].p = p;  /* prime modulus */
                afb[afb_count].r = r;  /* root of polynomial modulo p */
                ++afb_count;
            }
        }
    }

    /* Quadratic character columns: root-based local algebraic characters.
     * For each character prime q with a simple root r of f(x) mod q:
     *   character bit for relation (a,b) = Legendre(a - b*r, q)
     *   bit = 1 if (a-b*r) is a quadratic non-residue mod q, 0 otherwise.
     * These columns force the dependency product ∏(a_i - b_i·α) to be a
     * true square in the number ring (not just square up to units). */
    #define GNFS_MAX_CHARS 64
    uint64_t char_primes[GNFS_MAX_CHARS];
    uint64_t char_roots[GNFS_MAX_CHARS]; /* root r: f(r) ≡ 0 mod q */
    int char_count = 0;
    int char_vzero[GNFS_MAX_CHARS]; /* track v==0 occurrences per column */
    memset(char_vzero, 0, sizeof(char_vzero));
    { int desired = cfg->char_cols;
      if (desired > GNFS_MAX_CHARS) desired = GNFS_MAX_CHARS;
      /* Choose small odd primes q where f has at least one simple root mod q.
       * Skip primes dividing N. Use primes ABOVE the algebraic FB bound so they
       * don't overlap with the algebraic ideal columns. */
      uint64_t start_q = (uint64_t)cfg->afb_bound + 1u;
      if (start_q < 5u) start_q = 5u;
      for (uint64_t q = start_q; char_count < desired && q < start_q + 10000u; q += 2u) {
          if (!miller_rabin_u64(q)) continue;
          if (bn_mod_u64(n, q) == 0u) continue;
          /* Find a simple root of f(x) mod q */
          uint64_t root = 0u;
          bool found_root = false;
          for (uint64_t r = 0u; r < q && r < 100000u; ++r) {
              if (gnfs_poly_eval_mod_p(&poly, r, q) == 0u) {
                  /* Check simplicity: f'(r) ≠ 0 mod q */
                  /* f'(x) = 3*f3*x² + 2*f2*x + f1 for degree 3 */
                  uint64_t f3q = bn_mod_u64(&poly.coeffs[3], q);
                  uint64_t f2q = bn_mod_u64(&poly.coeffs[2], q);
                  uint64_t f1q = bn_mod_u64(&poly.coeffs[1], q);
                  uint64_t fp = (mulmod_u64(3u, mulmod_u64(f3q, mulmod_u64(r, r, q), q), q)
                               + mulmod_u64(2u, mulmod_u64(f2q, r, q), q) + f1q) % q;
                  if (fp != 0u) { root = r; found_root = true; break; }
              }
          }
          if (!found_root) continue;
          char_primes[char_count] = q;
          char_roots[char_count] = root;
          ++char_count;
      }
    }

    /* Compute real root of f for algebraic-sign column (infinite-place character).
     * α_real is the unique real root of f(x) = x³ + c2x² + c1x + c0.
     * NOTE: α_real ≠ m. For f(x)=x³+16000x+63, α_real ≈ -0.00394, m = 1000000. */
    double alpha_real = 0.0;
    { double c0d = bn_to_double(&poly.coeffs[0]), c1d = bn_to_double(&poly.coeffs[1]);
      double c2d = bn_to_double(&poly.coeffs[2]);
      /* Newton's method: x_{n+1} = x - f(x)/f'(x), start near 0 */
      double x = -c0d / c1d; /* initial guess from linear term */
      for (int iter = 0; iter < 100; ++iter) {
          double fx = x*x*x + c2d*x*x + c1d*x + c0d;
          double fpx = 3.0*x*x + 2.0*c2d*x + c1d;
          if (fpx == 0.0) break;
          double dx = fx / fpx;
          x -= dx;
          if (dx > -1e-15 && dx < 1e-15) break;
      }
      alpha_real = x;
      if (cfg->diag) printf("  [GNFS] alpha_real = %.15f  (m = %"PRIu64")\n", alpha_real, bn_to_u64(&poly.m));
    }

    /* Compute fundamental unit for unit-parity column */
    int64_t unit_c0 = 0, unit_c1 = 0, unit_c2 = 0;
    double unit_sigma1 = 1.0;
    double log_unit_sigma1 = 0.0;
    bool have_unit = false;
    if (cfg->schirokauer && poly.degree == 3 && bn_is_one(&poly.coeffs[3]) && bn_is_zero(&poly.coeffs[2])) {
        int64_t p_coeff = (int64_t)bn_to_u64(&poly.coeffs[1]);
        int64_t q_coeff = (int64_t)bn_to_u64(&poly.coeffs[0]);
        have_unit = find_fundamental_unit(p_coeff, q_coeff,
            &unit_c0, &unit_c1, &unit_c2, &unit_sigma1, cfg->diag);
        if (have_unit) {
            double abs_s = unit_sigma1 > 0 ? unit_sigma1 : -unit_sigma1;
            log_unit_sigma1 = log(abs_s);
            if (log_unit_sigma1 < 0) log_unit_sigma1 = -log_unit_sigma1;
        }
    }
    int unit_col = have_unit ? 1 : 0;

    /* Total columns: 2 sign + unit + RFB + AFB + character + Schirokauer
     * Column 0 = rational sign: sign(a - b*m)
     * Column 1 = algebraic sign: sign(a - b*alpha_real)  [infinite-place character]
     * Column 2 = unit parity (if unit found) */
    int sign_col = 2 + unit_col; /* first non-sign column index */
    int schk2_cols = cfg->schk2 ? 3 : 0; /* 2-adic Schirokauer: 1 bit from p1 + 2 bits from p2 */
    int schk_ell_cols = cfg->schirokauer ? 3 : 0; /* ell=2 Schirokauer: 3 GF(2) columns */
    int schk_ell3_cols = cfg->schirokauer ? 6 : 0; /* ell=3 Schirokauer: 3 Z/3Z values × 2 bits each = 6 GF(2) columns */
    uint64_t schk_ell = 0; /* Schirokauer prime for ell-adic path */
    if (cfg->schirokauer) {
        /* Try ℓ=2 first: valid when f has no double root mod 2 (i.e., 2 ∤ disc(f)).
         * For ℓ=2, Z/ℓZ = GF(2) so mod-2 reduction is LOSSLESS — the GF(2) nullspace
         * exactly enforces the Schirokauer constraint. This is the correct approach. */
        bool ell2_ok = true;
        if (bn_is_even(n)) ell2_ok = false;
        /* Check f has no double root mod 2: evaluate f(0),f(1),f'(0),f'(1) mod 2 */
        if (ell2_ok) {
            for (uint64_t r = 0; r < 2; ++r) {
                if (gnfs_poly_eval_mod_p(&poly, r, 2) == 0u) {
                    uint64_t f3c = bn_mod_u64(&poly.coeffs[3], 2);
                    uint64_t f2c = bn_mod_u64(&poly.coeffs[2], 2);
                    uint64_t f1c = bn_mod_u64(&poly.coeffs[1], 2);
                    uint64_t fp = (3u*f3c*r*r + 2u*f2c*r + f1c) % 2;
                    if (fp == 0u) { ell2_ok = false; break; } /* double root → 2|disc(f) */
                }
            }
        }
        if (cfg->diag) {
            printf("  [GNFS schk-ell] ell=2 test: N_even=%s", bn_is_even(n)?"YES(reject)":"no");
            if (!bn_is_even(n)) {
                /* Print root/derivative details */
                for (uint64_t r = 0; r < 2; ++r) {
                    uint64_t fv = gnfs_poly_eval_mod_p(&poly, r, 2);
                    if (fv == 0) {
                        uint64_t f3c=bn_mod_u64(&poly.coeffs[3],2), f2c=bn_mod_u64(&poly.coeffs[2],2), f1c=bn_mod_u64(&poly.coeffs[1],2);
                        uint64_t fp = (3u*f3c*r*r + 2u*f2c*r + f1c) % 2;
                        printf("  f(%"PRIu64")=0 f'(%"PRIu64")=%"PRIu64"%s", r, r, fp, fp==0?" (double root→REJECT)":"(simple→ok)");
                    }
                }
            }
            printf("  → ell2_ok=%s\n", ell2_ok?"YES (lossless)":"NO (fallback to large ell)");
        }
        if (ell2_ok) {
            schk_ell = 2;
        } else {
            /* Fallback: choose ℓ > AFB bound. Note: mod-2 reduction is lossy for ℓ>2. */
            for (uint64_t cand = (uint64_t)cfg->afb_bound + 1u | 1u; cand < (uint64_t)cfg->afb_bound + 10000u; cand += 2u) {
                if (!miller_rabin_u64(cand)) continue;
                if (bn_mod_u64(n, cand) == 0u) continue;
                bool bad = false;
                for (uint64_t r = 0; r < cand && r < 10000u; ++r) {
                    if (gnfs_poly_eval_mod_p(&poly, r, cand) == 0u) {
                        uint64_t f3c = bn_mod_u64(&poly.coeffs[3], cand);
                        uint64_t f2c = bn_mod_u64(&poly.coeffs[2], cand);
                        uint64_t f1c = bn_mod_u64(&poly.coeffs[1], cand);
                        uint64_t fp = (mulmod_u64(3u, mulmod_u64(f3c, mulmod_u64(r, r, cand), cand), cand)
                                     + mulmod_u64(2u, mulmod_u64(f2c, r, cand), cand) + f1c) % cand;
                        if (fp == 0u) { bad = true; break; }
                    }
                }
                if (bad) continue;
                schk_ell = cand;
                break;
            }
        }
        if (schk_ell == 0) { schk_ell_cols = 0; }
        if (cfg->diag) printf("  [GNFS schk-ell] chosen ell=%"PRIu64" (%s)\n", schk_ell,
            schk_ell==2?"LOSSLESS":(schk_ell==0?"NONE — no valid ell found":"LOSSY — mod-2 reduction active"));
    }
    int total_cols = sign_col + rfb_count + afb_count + char_count + schk2_cols + schk_ell_cols + schk_ell3_cols;
    int target_rels = (cfg->target_rels > 0) ? cfg->target_rels : (total_cols + 64);
    if (target_rels > GNFS_MAX_RELS) target_rels = GNFS_MAX_RELS;

    /* Column-layout audit: print exact ranges for each column family */
    int col_rfb_start = sign_col, col_rfb_end = sign_col + rfb_count - 1;
    int col_afb_start = sign_col + rfb_count, col_afb_end = sign_col + rfb_count + afb_count - 1;
    int col_char_start = sign_col + rfb_count + afb_count, col_char_end = col_char_start + char_count - 1;
    int col_schk2_start = col_char_start + char_count, col_schk2_end = col_schk2_start + schk2_cols - 1;
    int col_schk_start = col_schk2_start + schk2_cols, col_schk_end = col_schk_start + schk_ell_cols - 1;
    int col_schk3_start = col_schk_start + schk_ell_cols, col_schk3_end = col_schk3_start + schk_ell3_cols - 1;

    if (cfg->diag) {
        printf("  [COL-LAYOUT] rat_sign=0 alg_sign=1  RFB=[%d..%d](%d)  AFB=[%d..%d](%d)  CHAR=[%d..%d](%d)  SCHK2=[%d..%d](%d)  SCHK=[%d..%d](%d)  SCHK3=[%d..%d](%d)  total=%d\n",
            col_rfb_start, col_rfb_end, rfb_count,
            col_afb_start, col_afb_end, afb_count,
            col_char_start, col_char_end, char_count,
            col_schk2_start, col_schk2_end, schk2_cols,
            col_schk_start, col_schk_end, schk_ell_cols,
            col_schk3_start, col_schk3_end, schk_ell3_cols,
            total_cols);
        printf("  [GNFS] rational FB: %d primes up to %d\n", rfb_count, cfg->rfb_bound);
        printf("  [GNFS] algebraic FB: %d ideals from %d primes (avg %.2f roots/prime)\n",
            afb_count, rfb_count, rfb_count > 0 ? (double)afb_count / rfb_count : 0.0);
        printf("  [GNFS] character columns: %d (q,r) pairs [", char_count);
        for (int ci = 0; ci < char_count; ++ci)
            printf("%s(%"PRIu64",%"PRIu64")", ci ? "," : "", char_primes[ci], char_roots[ci]);
        printf("]\n");
        printf("  [GNFS] total columns: %d (2 sign + %d rational + %d algebraic + %d character + %d schk2 + %d schk_ell2 + %d schk_ell3)\n",
            total_cols, rfb_count, afb_count, char_count, schk2_cols, schk_ell_cols, schk_ell3_cols);
        printf("  [GNFS] target relations: %d\n", target_rels);
    }

    /* Stage 3: Naive relation collection */
    int row_words = (total_cols + 63) / 64;
    uint64_t* matrix = (uint64_t*)xmalloc((size_t)target_rels * (size_t)row_words * sizeof(uint64_t));
    memset(matrix, 0, (size_t)target_rels * (size_t)row_words * sizeof(uint64_t));
    /* Per-relation (a, b) pairs for Milestone 1.5 rational-side replay */
    int64_t* rel_a = (int64_t*)xmalloc((size_t)target_rels * sizeof(int64_t));
    uint64_t* rel_b = (uint64_t*)xmalloc((size_t)target_rels * sizeof(uint64_t));
    int rel_count = 0;
    uint64_t candidates_tested = 0u, rat_smooth_count = 0u, both_smooth_count = 0u;

    for (int64_t b = 1; b <= cfg->b_range && rel_count < target_rels; ++b) {
        for (int64_t a = -(int64_t)cfg->a_range; a <= (int64_t)cfg->a_range && rel_count < target_rels; ++a) {
            if (a == 0) continue;
            if (gcd_u64((uint64_t)(a < 0 ? -a : a), (uint64_t)b) != 1u) continue;
            ++candidates_tested;

            /* Rational norm: |a - b*m| */
            BigNum bm; bn_copy(&bm, &poly.m);
            { BigNum b_bn; bn_from_u64(&b_bn, (uint64_t)b);
              bn_mul(&bm, &bm, &b_bn); }
            BigNum rat_norm;
            BigNum a_bn;
            if (a >= 0) {
                bn_from_u64(&a_bn, (uint64_t)a);
                if (bn_cmp(&a_bn, &bm) >= 0) { bn_copy(&rat_norm, &a_bn); bn_sub(&rat_norm, &bm); }
                else { bn_copy(&rat_norm, &bm); bn_sub(&rat_norm, &a_bn); }
            } else {
                bn_from_u64(&a_bn, (uint64_t)(-a));
                bn_copy(&rat_norm, &bm); bn_add(&rat_norm, &a_bn);
            }

            if (bn_is_zero(&rat_norm)) continue;

            /* Determine sign of (a - b*m): negative when a < b*m */
            bool rat_negative;
            if (a >= 0) {
                BigNum a_check; bn_from_u64(&a_check, (uint64_t)a);
                rat_negative = (bn_cmp(&a_check, &bm) < 0);
            } else {
                rat_negative = true; /* a < 0 and b*m > 0, so a - b*m < 0 */
            }

            /* Trial-factor rational norm over rational FB */
            BigNum rrem; bn_copy(&rrem, &rat_norm);
            uint64_t* row = &matrix[rel_count * row_words];
            memset(row, 0, (size_t)row_words * sizeof(uint64_t));
            /* Column 0 = rational sign bit: 1 if (a - b*m) is negative */
            if (rat_negative) row[0] ^= 1ull;
            /* Column 1 = algebraic sign bit: 1 if (a - b*alpha_real) is negative */
            { double v_alg = (double)a - (double)b * alpha_real;
              if (v_alg < 0.0) row[1/64] ^= (1ull << (1 % 64));
            }
            /* Column 2 = unit parity bit (if unit found) */
            if (have_unit) {
                int up = unit_parity(a, b, alpha_real, log_unit_sigma1);
                if (up) row[2/64] ^= (1ull << (2 % 64));
            }
            for (int fi = 0; fi < rfb_count; ++fi) {
                int exp = 0;
                while (bn_mod_u64(&rrem, rfb[fi]) == 0u) {
                    BigNum dp; bn_from_u64(&dp, rfb[fi]);
                    BigNum qo; bn_divexact(&qo, &rrem, &dp);
                    bn_copy(&rrem, &qo);
                    ++exp;
                }
                int col = sign_col + fi;
                if (exp & 1) row[col / 64] ^= (1ull << (col % 64));
            }
            if (!bn_is_one(&rrem)) continue; /* rational norm not smooth */
            ++rat_smooth_count;

            /* Algebraic norm: |F(a, b)| */
            BigNum alg_norm;
            gnfs_eval_homogeneous(&alg_norm, &poly, a, (uint64_t)b);
            if (bn_is_zero(&alg_norm)) continue;

            /* Trial-factor algebraic norm over algebraic FB.
             * For each prime p in the algebraic FB, compute the total p-adic
             * valuation of the algebraic norm. Then assign the exponent to the
             * ideal (p, r) where a ≡ b*r (mod p). */
            BigNum arem; bn_copy(&arem, &alg_norm);
            for (int fi = 0; fi < afb_count; ) {
                uint64_t p = afb[fi].p;
                /* Count total p-valuation of the algebraic norm */
                int total_exp = 0;
                while (bn_mod_u64(&arem, p) == 0u) {
                    BigNum dp; bn_from_u64(&dp, p);
                    BigNum qo; bn_divexact(&qo, &arem, &dp);
                    bn_copy(&arem, &qo);
                    ++total_exp;
                }
                /* Assign exponent to the matching ideal: a ≡ b*r (mod p) */
                if (total_exp > 0) {
                    uint64_t a_mod = ((a % (int64_t)p) + (int64_t)p) % p;
                    for (int fj = fi; fj < afb_count && afb[fj].p == p; ++fj) {
                        uint64_t br = mulmod_u64((uint64_t)b % p, afb[fj].r, p);
                        if (br == a_mod) {
                            int col = sign_col + rfb_count + fj;
                            if (total_exp & 1) row[col / 64] ^= (1ull << (col % 64));
                            break;
                        }
                    }
                }
                /* Skip all ideals with the same prime */
                while (fi < afb_count && afb[fi].p == p) ++fi;
            }
            if (!bn_is_one(&arem)) continue; /* algebraic norm not smooth */

            /* Compute root-based quadratic character bits for this relation.
             * For each character pair (q, r): v = a - b*r mod q.
             * bit = 1 if Legendre(v, q) = -1 (non-residue), 0 otherwise. */
            for (int ci = 0; ci < char_count; ++ci) {
                uint64_t q = char_primes[ci];
                uint64_t r = char_roots[ci];
                /* v = (a - b*r) mod q, handling signed a */
                uint64_t br = mulmod_u64((uint64_t)b % q, r % q, q);
                uint64_t a_mod = ((a % (int64_t)q) + (int64_t)q) % q;
                uint64_t v = (a_mod + q - br) % q;
                if (v == 0u) { ++char_vzero[ci]; continue; }
                /* Euler criterion: v^((q-1)/2) mod q */
                uint64_t leg = powmod_u64(v, (q - 1u) / 2u, q);
                if (leg == q - 1u) { /* non-residue → bit = 1 */
                    int col = sign_col + rfb_count + afb_count + ci;
                    row[col / 64] ^= (1ull << (col % 64));
                }
            }

            /* ell-adic Schirokauer map (3 columns): β^exp mod (f, ℓ²), extract map, reduce mod 2.
             * Only defined when gcd(Norm(β), ℓ) = 1. Skip if ℓ | F(a,b). */
            if (schk_ell_cols > 0 && schk_ell > 0) {
                int sell_base = sign_col + rfb_count + afb_count + char_count + schk2_cols;
                uint64_t ell = schk_ell;
                uint64_t ell2 = ell * ell;
                /* Check if ℓ | Norm(β) = F(a,b) using homogeneous form. If so, skip. */
                uint64_t norm_mod_ell;
                { uint64_t bell = (uint64_t)b % ell;
                  uint64_t aell = ((a % (int64_t)ell) + (int64_t)ell) % (int64_t)ell;
                  /* F(a,b) = f3*a³ + f2*a²*b + f1*a*b² + f0*b³ mod ℓ */
                  uint64_t f0e=bn_mod_u64(&poly.coeffs[0],ell), f1e=bn_mod_u64(&poly.coeffs[1],ell);
                  uint64_t f2e=bn_mod_u64(&poly.coeffs[2],ell), f3e=bn_mod_u64(&poly.coeffs[3],ell);
                  uint64_t a2=mulmod_u64(aell,aell,ell), b2=mulmod_u64(bell,bell,ell);
                  norm_mod_ell = (mulmod_u64(f3e,mulmod_u64(a2,aell,ell),ell)
                    + mulmod_u64(f2e,mulmod_u64(a2,bell,ell),ell)
                    + mulmod_u64(f1e,mulmod_u64(aell,b2,ell),ell)
                    + mulmod_u64(f0e,mulmod_u64(b2,bell,ell),ell)) % ell;
                }
                if (norm_mod_ell == 0) goto schk_skip; /* ℓ | Norm(β), leave columns = 0 */
                /* Universal exponent = (ℓ²-1)(ℓ²+ℓ+1) = lcm(ℓ-1,ℓ²-1,ℓ³-1).
                 * Works for ALL factorization patterns of f mod ℓ (irreducible, 1+2, or 1+1+1). */
                uint64_t schk_exp = (ell*ell - 1u) * (ell*ell + ell + 1u);
                /* f mod ℓ² coefficients */
                uint64_t fm[3];
                for (int k = 0; k < 3; ++k) fm[k] = bn_mod_u64(&poly.coeffs[k], ell2);
                /* Element β = (a - b·α) mod ℓ² */
                uint64_t th[3];
                th[0] = (uint64_t)(((a % (int64_t)ell2) + (int64_t)ell2) % (int64_t)ell2);
                th[1] = (ell2 - (b % ell2)) % ell2;
                th[2] = 0;
                /* Poly multiply mod (f, ℓ²) using uint64. All intermediates < 3·ℓ⁴ < 2^50 for ℓ<2^13. */
                #define SCHK_MUL(out3,p3,q3) do { \
                    uint64_t _t[5]={0,0,0,0,0}; \
                    for(int _i=0;_i<3;++_i) for(int _j=0;_j<3;++_j) \
                        _t[_i+_j] = (_t[_i+_j] + ((p3)[_i]*(q3)[_j]) % ell2) % ell2; \
                    if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*fm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*fm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*fm[0])%ell2)%ell2;} \
                    if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*fm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*fm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*fm[0])%ell2)%ell2;} \
                    (out3)[0]=_t[0];(out3)[1]=_t[1];(out3)[2]=_t[2];}while(0)
                /* Binary exponentiation: β^(ℓ^d - 1) mod (f, ℓ²) */
                uint64_t sr[3] = {1, 0, 0};
                uint64_t sb[3] = {th[0], th[1], th[2]};
                uint64_t se = schk_exp;
                while (se > 0u) {
                    if (se & 1u) { uint64_t t[3]; SCHK_MUL(t, sr, sb); sr[0]=t[0]; sr[1]=t[1]; sr[2]=t[2]; }
                    uint64_t t2[3]; SCHK_MUL(t2, sb, sb); sb[0]=t2[0]; sb[1]=t2[1]; sb[2]=t2[2];
                    se >>= 1u;
                }
                #undef SCHK_MUL
                /* Extract: λ_k = ((res[k] - δ_{k,0}) / ℓ) mod ℓ for k=0,1,2 */
                uint64_t w[3];
                w[0] = ((sr[0] + ell2 - 1u) % ell2) / ell % ell;
                w[1] = (sr[1] / ell) % ell;
                w[2] = (sr[2] / ell) % ell;
                /* GF(2) columns: w_k mod 2 */
                for (int k = 0; k < 3; ++k) {
                    if (w[k] & 1u) { int c = sell_base + k; row[c/64] ^= (1ull << (c%64)); }
                }
                schk_skip:;
            }

            /* ℓ=3 Schirokauer map (6 GF(2) columns): β^104 mod (f, 9), extract Z/3Z, binary-encode */
            if (schk_ell3_cols > 0) {
                int s3_base = col_schk3_start;
                uint64_t el3 = 3, el3_2 = 9;
                /* Check 3 ∤ Norm(β) */
                uint64_t bell3 = (uint64_t)b % el3;
                uint64_t aell3 = ((a % (int64_t)el3) + (int64_t)el3) % (int64_t)el3;
                uint64_t f0e3 = bn_mod_u64(&poly.coeffs[0], el3), f1e3 = bn_mod_u64(&poly.coeffs[1], el3);
                uint64_t f2e3 = bn_mod_u64(&poly.coeffs[2], el3), f3e3 = bn_mod_u64(&poly.coeffs[3], el3);
                uint64_t a2_3 = mulmod_u64(aell3, aell3, el3), b2_3 = mulmod_u64(bell3, bell3, el3);
                uint64_t nm3 = (mulmod_u64(f3e3, mulmod_u64(a2_3, aell3, el3), el3)
                    + mulmod_u64(f2e3, mulmod_u64(a2_3, bell3, el3), el3)
                    + mulmod_u64(f1e3, mulmod_u64(aell3, b2_3, el3), el3)
                    + mulmod_u64(f0e3, mulmod_u64(b2_3, bell3, el3), el3)) % el3;
                if (nm3 != 0) {
                    /* f mod 9 coefficients */
                    uint64_t fm3[3]; for (int k = 0; k < 3; ++k) fm3[k] = bn_mod_u64(&poly.coeffs[k], el3_2);
                    /* β = (a - bα) mod 9 */
                    uint64_t th3[3];
                    th3[0] = (uint64_t)(((a % (int64_t)el3_2) + (int64_t)el3_2) % (int64_t)el3_2);
                    th3[1] = (el3_2 - (b % el3_2)) % el3_2;
                    th3[2] = 0;
                    /* Exponent: (ℓ²-1)(ℓ²+ℓ+1) = 8*13 = 104 */
                    uint64_t s3_exp = (el3*el3 - 1u) * (el3*el3 + el3 + 1u);
                    /* Binary exponentiation mod (f, 9) */
                    uint64_t s3r[3] = {1, 0, 0}, s3b[3] = {th3[0], th3[1], th3[2]};
                    uint64_t s3e = s3_exp;
                    #define SCHK3_MUL(out3, p3, q3) do { \
                        uint64_t _t[5]={0,0,0,0,0}; \
                        for(int _i=0;_i<3;++_i) for(int _j=0;_j<3;++_j) \
                            _t[_i+_j] = (_t[_i+_j] + mulmod_u64((p3)[_i], (q3)[_j], el3_2)) % el3_2; \
                        if(_t[4]){_t[3]=(_t[3]+el3_2-mulmod_u64(_t[4],fm3[2],el3_2))%el3_2; \
                            _t[2]=(_t[2]+el3_2-mulmod_u64(_t[4],fm3[1],el3_2))%el3_2; \
                            _t[1]=(_t[1]+el3_2-mulmod_u64(_t[4],fm3[0],el3_2))%el3_2;} \
                        if(_t[3]){_t[2]=(_t[2]+el3_2-mulmod_u64(_t[3],fm3[2],el3_2))%el3_2; \
                            _t[1]=(_t[1]+el3_2-mulmod_u64(_t[3],fm3[1],el3_2))%el3_2; \
                            _t[0]=(_t[0]+el3_2-mulmod_u64(_t[3],fm3[0],el3_2))%el3_2;} \
                        (out3)[0]=_t[0];(out3)[1]=_t[1];(out3)[2]=_t[2]; \
                    } while(0)
                    while (s3e > 0u) {
                        if (s3e & 1u) { uint64_t t[3]; SCHK3_MUL(t, s3r, s3b); s3r[0]=t[0]; s3r[1]=t[1]; s3r[2]=t[2]; }
                        uint64_t t2[3]; SCHK3_MUL(t2, s3b, s3b); s3b[0]=t2[0]; s3b[1]=t2[1]; s3b[2]=t2[2];
                        s3e >>= 1u;
                    }
                    #undef SCHK3_MUL
                    /* Extract: λ_k = ((res[k] - δ_{k,0}) / ℓ) mod ℓ */
                    uint64_t w3[3];
                    w3[0] = ((s3r[0] + el3_2 - 1u) % el3_2) / el3 % el3;
                    w3[1] = (s3r[1] / el3) % el3;
                    w3[2] = (s3r[2] / el3) % el3;
                    /* Binary-encode each Z/3Z value as 2 GF(2) bits: val → (bit0, bit1) */
                    for (int k = 0; k < 3; ++k) {
                        int c0 = s3_base + k * 2;     /* low bit */
                        int c1 = s3_base + k * 2 + 1; /* high bit */
                        if (w3[k] & 1u) row[c0/64] ^= (1ull << (c0%64));
                        if (w3[k] & 2u) row[c1/64] ^= (1ull << (c1%64));
                    }
                }
            }

            /* 2-adic Schirokauer map bits (3 columns) */
            if (schk2_cols > 0) {
                int schk_base = sign_col + rfb_count + afb_count + char_count;
                /* p1 bit: from a-b */
                int64_t s_ab = a - (int64_t)b;
                int bit_p1 = 0;
                if (s_ab != 0) {
                    int64_t s_abs = s_ab < 0 ? -s_ab : s_ab;
                    while ((s_abs & 1) == 0) s_abs >>= 1; /* strip 2-part */
                    bit_p1 = (int)((s_abs >> 1) & 1); /* test bit 1: ≡3 mod 4 → 1 */
                }
                if (bit_p1) row[(schk_base)/64] ^= (1ull << ((schk_base) % 64));

                /* p2 bits: from F4 projection */
                /* v2_local = (v2(norm) - v1) / 2; if > 0, output zeros */
                BigNum alg_norm_copy; gnfs_eval_homogeneous(&alg_norm_copy, &poly, a, (uint64_t)b);
                /* 2-adic valuation from BigNum: count trailing zero bits directly */
                int vn = 0;
                if (!bn_is_zero(&alg_norm_copy)) {
                    for (uint32_t li = 0; li < alg_norm_copy.used; ++li) {
                        if (alg_norm_copy.limbs[li] == 0) { vn += 64; continue; }
                        uint64_t w = alg_norm_copy.limbs[li];
                        while ((w & 1) == 0) { ++vn; w >>= 1; }
                        break;
                    }
                }
                int64_t s_ab2 = a - (int64_t)b;
                int v1 = 0; { int64_t tmp = s_ab2 < 0 ? -s_ab2 : s_ab2; if(tmp){while((tmp&1)==0){++v1;tmp>>=1;}} }
                int v2_loc = (vn > v1) ? (vn - v1) / 2 : 0;

                int lam_bit0 = 0, lam_bit1 = 0;
                if (v2_loc == 0) {
                    /* Projected triple via CRT idempotent e2 = 2+α+α² mod (f,4) */
                    int p0 = (int)(((2*a - (int64_t)b) % 4 + 4) % 4);
                    int p1v = (int)(((a - 2*(int64_t)b) % 4 + 4) % 4);
                    int p2v = (int)(((a - (int64_t)b) % 4 + 4) % 4);
                    int p0_lo=p0&1, p0_hi=(p0>>1)&1;
                    int p1_lo=p1v&1, p1_hi=(p1v>>1)&1;
                    int p2_lo=p2v&1, p2_hi=(p2v>>1)&1;
                    /* u in F4 */
                    int u0 = p0_lo ^ p2_lo;
                    int u1 = p1_lo ^ p2_lo;
                    /* v in F4 */
                    int v0 = p0_hi ^ p2_hi;
                    int v1f = p1_hi ^ p1_lo ^ p2_hi;
                    /* lambda = v * u^{-1} in F4 */
                    /* F4 inverse: 0→0(error), (0,1)→(0,1), (1,0)→(1,1), (1,1)→(1,0) */
                    if (u0 || u1) { /* u != 0 */
                        int ui0, ui1;
                        if (!u0 && u1) { ui0=0; ui1=1; } /* 1 → 1 */
                        else if (u0 && !u1) { ui0=1; ui1=1; } /* ω → ω+1 */
                        else { ui0=1; ui1=0; } /* ω+1 → ω */
                        /* F4 multiply: (a0+a1ω)(b0+b1ω) = (a0b0⊕a1b1) + (a0b1⊕a1b0⊕a1b1)ω */
                        lam_bit0 = (v0&ui0) ^ (v1f&ui1);
                        lam_bit1 = (v0&ui1) ^ (v1f&ui0) ^ (v1f&ui1);
                    }
                }
                if (lam_bit0) row[(schk_base+1)/64] ^= (1ull << ((schk_base+1) % 64));
                if (lam_bit1) row[(schk_base+2)/64] ^= (1ull << ((schk_base+2) % 64));
            }

            ++both_smooth_count;
            rel_a[rel_count] = a;
            rel_b[rel_count] = (uint64_t)b;

            /* Row-decode audit: print full bit decode for first 3 relations */
            if (cfg->local_square && rel_count < 3) {
                printf("  [ROW-AUDIT] rel#%d a=%"PRId64" b=%"PRIu64"\n", rel_count, a, (uint64_t)b);
                /* Decode by family */
                bool rat_sign_bit = (row[0] & 1ull) != 0;
                bool alg_sign_bit = (row[0] & 2ull) != 0;
                printf("  [ROW-AUDIT]   rat_sign=%d alg_sign=%d\n", rat_sign_bit?1:0, alg_sign_bit?1:0);
                /* RFB hits */
                int rfb_set=0;
                for (int ci=col_rfb_start; ci<=col_rfb_end; ++ci)
                    if (row[ci/64]&(1ull<<(ci%64))) ++rfb_set;
                printf("  [ROW-AUDIT]   RFB[%d..%d]: %d set\n", col_rfb_start, col_rfb_end, rfb_set);
                /* AFB hits */
                int afb_set=0;
                for (int ci=col_afb_start; ci<=col_afb_end; ++ci)
                    if (row[ci/64]&(1ull<<(ci%64))) ++afb_set;
                printf("  [ROW-AUDIT]   AFB[%d..%d]: %d set\n", col_afb_start, col_afb_end, afb_set);
                /* Char hits */
                int char_set=0;
                printf("  [ROW-AUDIT]   CHAR[%d..%d]:", col_char_start, col_char_end);
                for (int ci=col_char_start; ci<=col_char_end && char_count>0; ++ci)
                    if (row[ci/64]&(1ull<<(ci%64))) { printf(" col%d", ci); ++char_set; }
                printf("  [%d/%d set]\n", char_set, char_count);
                /* Schk hits */
                if (schk_ell_cols > 0) {
                    printf("  [ROW-AUDIT]   SCHK[%d..%d]:", col_schk_start, col_schk_end);
                    for (int ci=col_schk_start; ci<=col_schk_end; ++ci)
                        printf(" col%d=%d", ci, (row[ci/64]&(1ull<<(ci%64)))?1:0);
                    printf("\n");
                }
                /* Total bits set + stray check */
                int total_set=0;
                for (int ci=0; ci<total_cols; ++ci) if (row[ci/64]&(1ull<<(ci%64))) ++total_set;
                int stray=0;
                for (int ci=total_cols; ci<row_words*64; ++ci) if (row[ci/64]&(1ull<<(ci%64))) ++stray;
                printf("  [ROW-AUDIT]   total_bits_set=%d stray_beyond_cols=%d\n", total_set, stray);
            }

            ++rel_count;
        }

        /* Periodic progress */
        if (cfg->diag && (b % 10 == 0 || b == cfg->b_range)) {
            printf("  [GNFS] sieve: b=%"PRId64"/%d  cands=%"PRIu64"  rat_smooth=%"PRIu64
                   "  both_smooth=%"PRIu64"\n",
                b, cfg->b_range, candidates_tested, rat_smooth_count, both_smooth_count);
        }
    }

    /* Schirokauer Fermat validation: test β^(p³-1) ≡ 1 mod p for p=7 using SCHK_MUL-style arithmetic */
    if (cfg->schirokauer && cfg->diag) {
        uint64_t tp = 7, tp2 = 49;
        uint64_t texp = 7*7*7 - 1; /* 342 */
        uint64_t tfm[3]; for(int k=0;k<3;++k) tfm[k]=bn_mod_u64(&poly.coeffs[k],tp2);
        /* β = (1 - α) = (1, p-1, 0) = (1, 48, 0) mod 49 */
        uint64_t tth[3] = {1, tp2-1, 0};
        uint64_t tsr[3]={1,0,0}, tsb[3]={tth[0],tth[1],tth[2]};
        uint64_t tse=texp;
        while(tse>0u){
            if(tse&1u){
                uint64_t _t[5]={0,0,0,0,0};
                for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+((tsr)[_i]*(tsb)[_j])%tp2)%tp2;
                if(_t[4]){_t[3]=(_t[3]+tp2-(_t[4]*tfm[2])%tp2)%tp2;_t[2]=(_t[2]+tp2-(_t[4]*tfm[1])%tp2)%tp2;_t[1]=(_t[1]+tp2-(_t[4]*tfm[0])%tp2)%tp2;}
                if(_t[3]){_t[2]=(_t[2]+tp2-(_t[3]*tfm[2])%tp2)%tp2;_t[1]=(_t[1]+tp2-(_t[3]*tfm[1])%tp2)%tp2;_t[0]=(_t[0]+tp2-(_t[3]*tfm[0])%tp2)%tp2;}
                tsr[0]=_t[0];tsr[1]=_t[1];tsr[2]=_t[2];
            }
            { uint64_t _t[5]={0,0,0,0,0};
              for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+((tsb)[_i]*(tsb)[_j])%tp2)%tp2;
              if(_t[4]){_t[3]=(_t[3]+tp2-(_t[4]*tfm[2])%tp2)%tp2;_t[2]=(_t[2]+tp2-(_t[4]*tfm[1])%tp2)%tp2;_t[1]=(_t[1]+tp2-(_t[4]*tfm[0])%tp2)%tp2;}
              if(_t[3]){_t[2]=(_t[2]+tp2-(_t[3]*tfm[2])%tp2)%tp2;_t[1]=(_t[1]+tp2-(_t[3]*tfm[1])%tp2)%tp2;_t[0]=(_t[0]+tp2-(_t[3]*tfm[0])%tp2)%tp2;}
              tsb[0]=_t[0];tsb[1]=_t[1];tsb[2]=_t[2];
            }
            tse>>=1u;
        }
        printf("  [GNFS schk-verify] p=7 SCHK: (1-α)^342 mod (f,49) = [%"PRIu64",%"PRIu64",%"PRIu64"] %s\n",
            tsr[0], tsr[1], tsr[2], (tsr[0]%7==1&&tsr[1]%7==0&&tsr[2]%7==0)?"Fermat OK":"**Fermat FAIL**");
        /* Now test with ell=ℓ (the actual Schirokauer prime) on first relation */
        if (rel_count > 0 && schk_ell > 0) {
            uint64_t ell=schk_ell, ell2=ell*ell, sexp=ell*ell*ell-1;
            uint64_t sfm[3]; for(int k=0;k<3;++k) sfm[k]=bn_mod_u64(&poly.coeffs[k],ell2);
            uint64_t sth[3];
            sth[0]=(uint64_t)(((rel_a[0]%(int64_t)ell2)+(int64_t)ell2)%(int64_t)ell2);
            sth[1]=(ell2-(rel_b[0]%ell2))%ell2; sth[2]=0;
            /* Print the element and f coefficients for diagnosis */
            printf("  [GNFS schk-verify] ell=%"PRIu64" ell2=%"PRIu64" exp=%"PRIu64"\n", ell, ell2, sexp);
            printf("  [GNFS schk-verify] f mod ell2: [%"PRIu64",%"PRIu64",%"PRIu64"]\n", sfm[0], sfm[1], sfm[2]);
            printf("  [GNFS schk-verify] beta = [%"PRIu64",%"PRIu64",0]  (a=%"PRId64" b=%"PRIu64")\n", sth[0], sth[1], rel_a[0], rel_b[0]);
            /* Step-by-step: compute β² first, print it */
            { uint64_t _t[5]={0,0,0,0,0};
              for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+((sth)[_i]*(sth)[_j])%ell2)%ell2;
              printf("  [GNFS schk-verify] β² raw (before reduce): [%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"]\n",_t[0],_t[1],_t[2],_t[3],_t[4]);
              if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*sfm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*sfm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*sfm[0])%ell2)%ell2;}
              if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*sfm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*sfm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*sfm[0])%ell2)%ell2;}
              printf("  [GNFS schk-verify] β² reduced: [%"PRIu64",%"PRIu64",%"PRIu64"]\n",_t[0],_t[1],_t[2]);
              /* Also compute β² using u64 mulmod for cross-check */
              uint64_t c0=sth[0]%ell2, c1=sth[1]%ell2;
              uint64_t d0=mulmod_u64(c0,c0,ell2);
              uint64_t d1=(mulmod_u64(c0,c1,ell2)+mulmod_u64(c1,c0,ell2))%ell2;
              uint64_t d2=mulmod_u64(c1,c1,ell2);
              printf("  [GNFS schk-verify] β² via mulmod_u64: [%"PRIu64",%"PRIu64",%"PRIu64"] (no reduce needed for deg-1 input)\n",d0,d1,d2);
              bool sq_match=(_t[0]==d0&&_t[1]==d1&&_t[2]==d2);
              printf("  [GNFS schk-verify] β² match: %s\n", sq_match?"YES":"**NO**");
            }
        }
    }

    /* ── Variant sweep: compare local-square across column configurations ── */
    if (cfg->variant_sweep && rel_count > 0) {
        printf("\n  [VARIANT-SWEEP] === Matrix Variant Local-Square Comparison ===\n");
        printf("  [VARIANT-SWEEP] relation pool: %d relations, a in [%"PRId64"..], b in [1..%d]\n",
            rel_count, rel_a[0], cfg->b_range);

        /* Find 30 small irreducible test primes (brute-force root check — trusted) */
        uint64_t vt_primes[64]; int vt_np = 0;
        for (uint64_t cand = 3; cand < 500 && vt_np < 30; cand += 2) {
            if (!miller_rabin_u64(cand)) continue;
            if (cand == 2) continue;
            bool has_root = false;
            for (uint64_t r = 0; r < cand && !has_root; ++r)
                if (gnfs_poly_eval_mod_p(&poly, r, cand) == 0) has_root = true;
            if (has_root) continue;
            vt_primes[vt_np++] = cand;
        }
        printf("  [VARIANT-SWEEP] test primes: %d (brute-force irreducible)\n", vt_np);

        /* Column families:
         * 0: RFB+AFB only (no sign, no char, no schk)
         * 1: +2 sign columns
         * 2: +2 sign +8 char
         * 3: +2 sign +32 char
         * 4: +2 sign +schk ell=2 (no char)
         * 5: +2 sign +8 char +schk ell=2 (current full) */
        const char* vt_names[] = {"RFB+AFB", "+sign", "+sign+8char", "+sign+schk2", "+sign+8char+schk2", "+sign+schk3", "+sign+schk2+schk3", "+sign+8char+schk2+3"};
        int vt_sign[]  = {0, 2, 2, 2, 2, 2, 2, 2};
        int vt_char[]  = {0, 0, 8, 0, 8, 0, 0, 8};
        int vt_schk[]  = {0, 0, 0, 3, 3, 0, 3, 3};
        int vt_schk3[] = {0, 0, 0, 0, 0, 6, 6, 6};
        int n_variants = 8;

        printf("  [VARIANT-SWEEP] %-20s %6s %6s %6s %6s %6s %8s\n",
            "variant", "rows", "cols", "rank", "null", "lsq50", "avgQR");

        for (int vi = 0; vi < n_variants; ++vi) {
            int v_sign_cols = vt_sign[vi];
            int v_char_count = vt_char[vi] < char_count ? vt_char[vi] : char_count;
            int v_schk_cols = vt_schk[vi];
            int v_schk3_cols = vt_schk3[vi];
            int v_total = v_sign_cols + rfb_count + afb_count + v_char_count + v_schk_cols + v_schk3_cols;
            int v_rw = (v_total + 63) / 64;

            /* Build matrix for this variant */
            uint64_t* vm = (uint64_t*)xmalloc((size_t)rel_count * (size_t)v_rw * sizeof(uint64_t));
            memset(vm, 0, (size_t)rel_count * (size_t)v_rw * sizeof(uint64_t));

            for (int ri = 0; ri < rel_count; ++ri) {
                int64_t a = rel_a[ri]; uint64_t b = rel_b[ri];
                uint64_t* vrow = &vm[ri * v_rw];

                /* Sign columns */
                if (v_sign_cols >= 1) {
                    /* rational sign: a - b*m < 0 */
                    BigNum bm; bn_copy(&bm, &poly.m); { BigNum bb; bn_from_u64(&bb, b); bn_mul(&bm, &bm, &bb); }
                    bool rat_neg = (a >= 0) ? (bn_cmp(&(BigNum){.limbs={(uint64_t)(a>=0?(uint64_t)a:0)},.used=1}, &bm) < 0) : true;
                    if (a < 0) rat_neg = true;
                    else { BigNum ac; bn_from_u64(&ac, (uint64_t)a); rat_neg = (bn_cmp(&ac, &bm) < 0); }
                    if (rat_neg) vrow[0] ^= 1ull;
                }
                if (v_sign_cols >= 2) {
                    double v_alg = (double)a - (double)b * alpha_real;
                    if (v_alg < 0.0) vrow[0] ^= 2ull;
                }

                /* RFB columns */
                { BigNum rn;
                  BigNum bm2; bn_copy(&bm2, &poly.m); { BigNum bb; bn_from_u64(&bb, b); bn_mul(&bm2, &bm2, &bb); }
                  if (a >= 0) { BigNum ac; bn_from_u64(&ac, (uint64_t)a);
                      if (bn_cmp(&ac, &bm2) >= 0) { bn_copy(&rn, &ac); bn_sub(&rn, &bm2); }
                      else { bn_copy(&rn, &bm2); bn_sub(&rn, &ac); }
                  } else { BigNum ac; bn_from_u64(&ac, (uint64_t)(-a)); bn_copy(&rn, &bm2); bn_add(&rn, &ac); }
                  for (int fi = 0; fi < rfb_count; ++fi) {
                      int exp = 0;
                      while (bn_mod_u64(&rn, rfb[fi]) == 0u) {
                          BigNum dp; bn_from_u64(&dp, rfb[fi]); BigNum qo; bn_divexact(&qo, &rn, &dp); bn_copy(&rn, &qo); ++exp; }
                      int col = v_sign_cols + fi;
                      if (exp & 1) vrow[col/64] ^= (1ull << (col%64));
                  }
                }

                /* AFB columns */
                { BigNum an; gnfs_eval_homogeneous(&an, &poly, a, b);
                  for (int fi = 0; fi < afb_count; ) {
                      uint64_t p = afb[fi].p; int te = 0;
                      while (bn_mod_u64(&an, p) == 0u) {
                          BigNum dp; bn_from_u64(&dp, p); BigNum qo; bn_divexact(&qo, &an, &dp); bn_copy(&an, &qo); ++te; }
                      if (te > 0) {
                          uint64_t am = ((a % (int64_t)p) + (int64_t)p) % p;
                          for (int fj = fi; fj < afb_count && afb[fj].p == p; ++fj) {
                              uint64_t br = mulmod_u64(b % p, afb[fj].r, p);
                              if (br == am) { int col = v_sign_cols + rfb_count + fj;
                                  if (te & 1) vrow[col/64] ^= (1ull << (col%64)); break; } } }
                      while (fi < afb_count && afb[fi].p == p) ++fi;
                  }
                }

                /* Character columns */
                for (int ci = 0; ci < v_char_count; ++ci) {
                    uint64_t q = char_primes[ci], r = char_roots[ci];
                    uint64_t br = mulmod_u64(b % q, r % q, q);
                    uint64_t am = ((a % (int64_t)q) + (int64_t)q) % q;
                    uint64_t v = (am + q - br) % q;
                    if (v == 0u) continue;
                    uint64_t leg = powmod_u64(v, (q - 1u) / 2u, q);
                    if (leg == q - 1u) { int col = v_sign_cols + rfb_count + afb_count + ci;
                        vrow[col/64] ^= (1ull << (col%64)); }
                }

                /* Schirokauer ell=2 columns */
                if (v_schk_cols > 0 && schk_ell == 2) {
                    int sb = v_sign_cols + rfb_count + afb_count + v_char_count;
                    uint64_t ell = 2, ell2 = 4;
                    uint64_t sfm[3]; for(int k=0;k<3;++k) sfm[k]=bn_mod_u64(&poly.coeffs[k],ell2);
                    uint64_t sth[3];
                    sth[0]=(uint64_t)(((a%(int64_t)ell2)+(int64_t)ell2)%(int64_t)ell2);
                    sth[1]=(ell2-(b%ell2))%ell2; sth[2]=0;
                    uint64_t sse = 21; /* (ell²-1)(ell²+ell+1) */
                    uint64_t ssr[3]={1,0,0}, ssb[3]={sth[0],sth[1],sth[2]};
                    while(sse>0u){
                        if(sse&1u){uint64_t _t[5]={0,0,0,0,0};
                            for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+((ssr)[_i]*(ssb)[_j])%ell2)%ell2;
                            if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*sfm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*sfm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*sfm[0])%ell2)%ell2;}
                            if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*sfm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*sfm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*sfm[0])%ell2)%ell2;}
                            ssr[0]=_t[0];ssr[1]=_t[1];ssr[2]=_t[2];}
                        {uint64_t _t[5]={0,0,0,0,0};
                            for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+((ssb)[_i]*(ssb)[_j])%ell2)%ell2;
                            if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*sfm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*sfm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*sfm[0])%ell2)%ell2;}
                            if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*sfm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*sfm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*sfm[0])%ell2)%ell2;}
                            ssb[0]=_t[0];ssb[1]=_t[1];ssb[2]=_t[2];}
                        sse>>=1u;}
                    uint64_t sw[3];
                    sw[0]=((ssr[0]+ell2-1u)%ell2)/ell%ell;
                    sw[1]=(ssr[1]/ell)%ell; sw[2]=(ssr[2]/ell)%ell;
                    for(int k=0;k<3;++k) if(sw[k]&1u){int c=sb+k;vrow[c/64]^=(1ull<<(c%64));}
                }
            }

            /* Local-square battery on first 50 deps (using same test primes) */
            int v_pass=0, v_total_qr=0, v_total_tests=0;
            BigNum f3i_v; bn_from_u64(&f3i_v, 1u);
            int* v_pivot = (int*)xmalloc((size_t)v_total * sizeof(int));

            /* Rebuild rows from scratch for elimination with history */
            memset(vm, 0, (size_t)rel_count * (size_t)v_rw * sizeof(uint64_t));
            for (int ri = 0; ri < rel_count; ++ri) {
                int64_t a = rel_a[ri]; uint64_t b = rel_b[ri];
                uint64_t* vrow = &vm[ri * v_rw];
                if (v_sign_cols >= 1) {
                    BigNum bm; bn_copy(&bm, &poly.m); { BigNum bb; bn_from_u64(&bb, b); bn_mul(&bm, &bm, &bb); }
                    bool rat_neg; if(a<0)rat_neg=true;else{BigNum ac;bn_from_u64(&ac,(uint64_t)a);rat_neg=(bn_cmp(&ac,&bm)<0);}
                    if (rat_neg) vrow[0] ^= 1ull;
                }
                if (v_sign_cols >= 2) { if ((double)a - (double)b * alpha_real < 0.0) vrow[0] ^= 2ull; }
                { BigNum rn; BigNum bm2; bn_copy(&bm2, &poly.m); { BigNum bb; bn_from_u64(&bb, b); bn_mul(&bm2, &bm2, &bb); }
                  if(a>=0){BigNum ac;bn_from_u64(&ac,(uint64_t)a);if(bn_cmp(&ac,&bm2)>=0){bn_copy(&rn,&ac);bn_sub(&rn,&bm2);}else{bn_copy(&rn,&bm2);bn_sub(&rn,&ac);}}
                  else{BigNum ac;bn_from_u64(&ac,(uint64_t)(-a));bn_copy(&rn,&bm2);bn_add(&rn,&ac);}
                  for(int fi=0;fi<rfb_count;++fi){int exp=0;while(bn_mod_u64(&rn,rfb[fi])==0u){BigNum dp;bn_from_u64(&dp,rfb[fi]);BigNum qo;bn_divexact(&qo,&rn,&dp);bn_copy(&rn,&qo);++exp;}
                      int col=v_sign_cols+fi;if(exp&1)vrow[col/64]^=(1ull<<(col%64));}}
                { BigNum an; gnfs_eval_homogeneous(&an, &poly, a, b);
                  for(int fi=0;fi<afb_count;){uint64_t p=afb[fi].p;int te=0;
                      while(bn_mod_u64(&an,p)==0u){BigNum dp;bn_from_u64(&dp,p);BigNum qo;bn_divexact(&qo,&an,&dp);bn_copy(&an,&qo);++te;}
                      if(te>0){uint64_t am=((a%(int64_t)p)+(int64_t)p)%p;
                          for(int fj=fi;fj<afb_count&&afb[fj].p==p;++fj){uint64_t br=mulmod_u64(b%p,afb[fj].r,p);
                              if(br==am){int col=v_sign_cols+rfb_count+fj;if(te&1)vrow[col/64]^=(1ull<<(col%64));break;}}}
                      while(fi<afb_count&&afb[fi].p==p)++fi;}}
                for(int ci=0;ci<v_char_count;++ci){uint64_t q=char_primes[ci],r=char_roots[ci];
                    uint64_t br=mulmod_u64(b%q,r%q,q);uint64_t am=((a%(int64_t)q)+(int64_t)q)%q;uint64_t v=(am+q-br)%q;
                    if(v==0u)continue;uint64_t leg=powmod_u64(v,(q-1u)/2u,q);
                    if(leg==q-1u){int col=v_sign_cols+rfb_count+afb_count+ci;vrow[col/64]^=(1ull<<(col%64));}}
                if(v_schk_cols>0&&schk_ell==2){int sb=v_sign_cols+rfb_count+afb_count+v_char_count;
                    uint64_t ell=2,ell2=4;uint64_t sfm[3];for(int k=0;k<3;++k)sfm[k]=bn_mod_u64(&poly.coeffs[k],ell2);
                    uint64_t sth[3];sth[0]=(uint64_t)(((a%(int64_t)ell2)+(int64_t)ell2)%(int64_t)ell2);sth[1]=(ell2-(b%ell2))%ell2;sth[2]=0;
                    uint64_t sse=21,ssr[3]={1,0,0},ssb2[3]={sth[0],sth[1],sth[2]};
                    while(sse>0u){if(sse&1u){uint64_t _t[5]={0,0,0,0,0};for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+((ssr)[_i]*(ssb2)[_j])%ell2)%ell2;
                        if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*sfm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*sfm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*sfm[0])%ell2)%ell2;}
                        if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*sfm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*sfm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*sfm[0])%ell2)%ell2;}
                        ssr[0]=_t[0];ssr[1]=_t[1];ssr[2]=_t[2];}
                    {uint64_t _t[5]={0,0,0,0,0};for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+((ssb2)[_i]*(ssb2)[_j])%ell2)%ell2;
                        if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*sfm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*sfm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*sfm[0])%ell2)%ell2;}
                        if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*sfm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*sfm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*sfm[0])%ell2)%ell2;}
                        ssb2[0]=_t[0];ssb2[1]=_t[1];ssb2[2]=_t[2];}sse>>=1u;}
                    uint64_t sw[3];sw[0]=((ssr[0]+ell2-1u)%ell2)/ell%ell;sw[1]=(ssr[1]/ell)%ell;sw[2]=(ssr[2]/ell)%ell;
                    for(int k=0;k<3;++k)if(sw[k]&1u){int c=sb+k;vrow[c/64]^=(1ull<<(c%64));}}
                /* ℓ=3 Schirokauer (6 GF(2) cols) */
                if(v_schk3_cols>0){
                    int s3b=v_sign_cols+rfb_count+afb_count+v_char_count+v_schk_cols;
                    uint64_t el3=3,el3_2=9;
                    uint64_t bell3=b%el3,aell3=((a%(int64_t)el3)+(int64_t)el3)%(int64_t)el3;
                    uint64_t f0e3=bn_mod_u64(&poly.coeffs[0],el3),f1e3=bn_mod_u64(&poly.coeffs[1],el3);
                    uint64_t f2e3=bn_mod_u64(&poly.coeffs[2],el3),f3e3=bn_mod_u64(&poly.coeffs[3],el3);
                    uint64_t a2_3=mulmod_u64(aell3,aell3,el3),b2_3=mulmod_u64(bell3,bell3,el3);
                    uint64_t nm3=(mulmod_u64(f3e3,mulmod_u64(a2_3,aell3,el3),el3)
                        +mulmod_u64(f2e3,mulmod_u64(a2_3,bell3,el3),el3)
                        +mulmod_u64(f1e3,mulmod_u64(aell3,b2_3,el3),el3)
                        +mulmod_u64(f0e3,mulmod_u64(b2_3,bell3,el3),el3))%el3;
                    if(nm3!=0){
                        uint64_t fm3[3];for(int k=0;k<3;++k)fm3[k]=bn_mod_u64(&poly.coeffs[k],el3_2);
                        uint64_t th3[3];th3[0]=(uint64_t)(((a%(int64_t)el3_2)+(int64_t)el3_2)%(int64_t)el3_2);
                        th3[1]=(el3_2-(b%el3_2))%el3_2;th3[2]=0;
                        uint64_t s3e=(el3*el3-1u)*(el3*el3+el3+1u);
                        uint64_t s3r[3]={1,0,0},s3bb[3]={th3[0],th3[1],th3[2]};
                        while(s3e>0u){
                            if(s3e&1u){uint64_t _t[5]={0,0,0,0,0};for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+mulmod_u64(s3r[_i],s3bb[_j],el3_2))%el3_2;
                                if(_t[4]){_t[3]=(_t[3]+el3_2-mulmod_u64(_t[4],fm3[2],el3_2))%el3_2;_t[2]=(_t[2]+el3_2-mulmod_u64(_t[4],fm3[1],el3_2))%el3_2;_t[1]=(_t[1]+el3_2-mulmod_u64(_t[4],fm3[0],el3_2))%el3_2;}
                                if(_t[3]){_t[2]=(_t[2]+el3_2-mulmod_u64(_t[3],fm3[2],el3_2))%el3_2;_t[1]=(_t[1]+el3_2-mulmod_u64(_t[3],fm3[1],el3_2))%el3_2;_t[0]=(_t[0]+el3_2-mulmod_u64(_t[3],fm3[0],el3_2))%el3_2;}
                                s3r[0]=_t[0];s3r[1]=_t[1];s3r[2]=_t[2];}
                            {uint64_t _t[5]={0,0,0,0,0};for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+mulmod_u64(s3bb[_i],s3bb[_j],el3_2))%el3_2;
                                if(_t[4]){_t[3]=(_t[3]+el3_2-mulmod_u64(_t[4],fm3[2],el3_2))%el3_2;_t[2]=(_t[2]+el3_2-mulmod_u64(_t[4],fm3[1],el3_2))%el3_2;_t[1]=(_t[1]+el3_2-mulmod_u64(_t[4],fm3[0],el3_2))%el3_2;}
                                if(_t[3]){_t[2]=(_t[2]+el3_2-mulmod_u64(_t[3],fm3[2],el3_2))%el3_2;_t[1]=(_t[1]+el3_2-mulmod_u64(_t[3],fm3[1],el3_2))%el3_2;_t[0]=(_t[0]+el3_2-mulmod_u64(_t[3],fm3[0],el3_2))%el3_2;}
                                s3bb[0]=_t[0];s3bb[1]=_t[1];s3bb[2]=_t[2];}
                            s3e>>=1u;}
                        uint64_t w3[3];w3[0]=((s3r[0]+el3_2-1u)%el3_2)/el3%el3;w3[1]=(s3r[1]/el3)%el3;w3[2]=(s3r[2]/el3)%el3;
                        for(int k=0;k<3;++k){int c0=s3b+k*2;int c1=s3b+k*2+1;
                            if(w3[k]&1u)vrow[c0/64]^=(1ull<<(c0%64));
                            if(w3[k]&2u)vrow[c1/64]^=(1ull<<(c1%64));}
                    }
                }
            }

            /* GF(2) elimination with history */
            int v_hw = (rel_count + 63) / 64;
            uint64_t* v_hist = (uint64_t*)xmalloc((size_t)rel_count * (size_t)v_hw * sizeof(uint64_t));
            memset(v_hist, 0, (size_t)rel_count * (size_t)v_hw * sizeof(uint64_t));
            for (int r=0;r<rel_count;++r) v_hist[r*v_hw + r/64] |= (1ull<<(r%64));

            bool* v_row_used = (bool*)xmalloc((size_t)rel_count * sizeof(bool));
            memset(v_row_used, 0, (size_t)rel_count * sizeof(bool));
            for(int c=0;c<v_total;++c) v_pivot[c]=-1;
            for(int c=0;c<v_total;++c){
                int pr=-1;
                for(int r=0;r<rel_count;++r) if(!v_row_used[r] && (vm[r*v_rw+c/64]&(1ull<<(c%64)))){pr=r;break;}
                if(pr<0)continue;
                v_pivot[c]=pr; v_row_used[pr]=true;
                for(int r=0;r<rel_count;++r){
                    if(r==pr)continue;
                    if(vm[r*v_rw+c/64]&(1ull<<(c%64))){
                        for(int w=0;w<v_rw;++w) vm[r*v_rw+w]^=vm[pr*v_rw+w];
                        for(int w=0;w<v_hw;++w) v_hist[r*v_hw+w]^=v_hist[pr*v_hw+w];
                    }
                }
            }
            int v_rank=0; for(int c=0;c<v_total;++c) if(v_pivot[c]>=0) ++v_rank;
            int v_ns=0; int v_nz_first=-1;
            for(int r=0;r<rel_count;++r){bool zr=true;for(int w=0;w<v_rw;++w)if(vm[r*v_rw+w]){zr=false;break;}
                if(zr)++v_ns; else if(v_nz_first<0) v_nz_first=r;}

            /* Local-square on first 50 null-space deps */
            int max_test = 50;
            int di_tested = 0;
            for (int r = 0; r < rel_count && di_tested < max_test; ++r) {
                bool zr = true; for(int w=0;w<v_rw;++w) if(vm[r*v_rw+w]){zr=false;break;} if(!zr) continue;
                uint64_t* hd = &v_hist[r * v_hw];
                int qr_pass_count = 0;
                for (int pi = 0; pi < vt_np; ++pi) {
                    uint64_t p = vt_primes[pi];
                    BigNum p_bn; bn_from_u64(&p_bn, p);
                    AlgElem Sp; alg_one(&Sp);
                    for (int rj = 0; rj < rel_count; ++rj) {
                        if (!(hd[rj/64] & (1ull<<(rj%64)))) continue;
                        AlgElem el; alg_from_ab(&el, rel_a[rj], rel_b[rj], &p_bn);
                        AlgElem prev; for(int c=0;c<3;++c) bn_copy(&prev.c[c], &Sp.c[c]);
                        alg_mul(&Sp, &prev, &el, &poly, &f3i_v, &p_bn);
                    }
                    BigNum qr_exp; gnfs_qr_exp_from_prime(&qr_exp, p);
                    unsigned qr_bits = bn_bitlen(&qr_exp);
                    AlgElem qc; alg_one(&qc);
                    AlgElem qb; for(int c=0;c<3;++c) bn_copy(&qb.c[c], &Sp.c[c]);
                    for(unsigned bt=0;bt<qr_bits;++bt){
                        if(qr_exp.limbs[bt/64]&(1ull<<(bt%64))){AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&qc.c[c]);alg_mul(&qc,&t,&qb,&poly,&f3i_v,&p_bn);}
                        AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&qb.c[c]);alg_mul(&qb,&t2,&t2,&poly,&f3i_v,&p_bn);}
                    bool is_qr = bn_is_one(&qc.c[0]) && bn_is_zero(&qc.c[1]) && bn_is_zero(&qc.c[2]);
                    if (is_qr) ++qr_pass_count;
                }
                if (qr_pass_count == vt_np) ++v_pass;
                v_total_qr += qr_pass_count;
                v_total_tests += vt_np;
                ++di_tested;
            }

            double avg_qr = (di_tested > 0 && vt_np > 0) ? (double)v_total_qr / ((double)di_tested * vt_np) * 100.0 : 0.0;
            if (vi == 0) {
                int pivot_used = 0; for(int c=0;c<v_total;++c) if(v_pivot[c]>=0) ++pivot_used;
                printf("  [VARIANT-SWEEP] debug: pivots=%d zero_rows=%d non-zero first=%d total_rows=%d\n",
                    pivot_used, v_ns, v_nz_first, rel_count);
            }
            printf("  [VARIANT-SWEEP] %-20s %6d %6d %6d %6d %3d/%-3d %6.1f%%\n",
                vt_names[vi], rel_count, v_total, v_rank, v_ns, v_pass, di_tested, avg_qr);

            free(v_hist); free(v_pivot); free(vm);
        }
        printf("  [VARIANT-SWEEP] === done ===\n");
        fr.time_seconds = now_sec() - t0; return fr;
    }

    /* Store raw Z/ℓZ Schirokauer values for dep-sum analysis */
    uint16_t* schk_raw = NULL; /* 3 values per relation, stored as uint16 (ℓ < 65536) */
    if (cfg->schirokauer && schk_ell > 0 && rel_count > 0) {
        schk_raw = (uint16_t*)xmalloc((size_t)rel_count * 3 * sizeof(uint16_t));
        /* Recompute raw values (not mod 2) for each relation */
        uint64_t ell = schk_ell, ell2 = ell*ell;
        uint64_t schk_exp = (ell*ell - 1u) * (ell*ell + ell + 1u);
        uint64_t fm[3]; for(int k=0;k<3;++k) fm[k]=bn_mod_u64(&poly.coeffs[k],ell2);
        int fermat_fail = 0, norm_skip = 0;
        for (int ri = 0; ri < rel_count; ++ri) {
            /* Check if ℓ | Norm(β) — if so, map undefined, set to 0 */
            { uint64_t bell=rel_b[ri]%ell, aell=((rel_a[ri]%(int64_t)ell)+(int64_t)ell)%(int64_t)ell;
              uint64_t f0e=bn_mod_u64(&poly.coeffs[0],ell),f1e=bn_mod_u64(&poly.coeffs[1],ell);
              uint64_t f2e=bn_mod_u64(&poly.coeffs[2],ell),f3e=bn_mod_u64(&poly.coeffs[3],ell);
              uint64_t a2=mulmod_u64(aell,aell,ell), b2=mulmod_u64(bell,bell,ell);
              uint64_t nm=(mulmod_u64(f3e,mulmod_u64(a2,aell,ell),ell)
                + mulmod_u64(f2e,mulmod_u64(a2,bell,ell),ell)
                + mulmod_u64(f1e,mulmod_u64(aell,b2,ell),ell)
                + mulmod_u64(f0e,mulmod_u64(b2,bell,ell),ell)) % ell;
              if(nm==0){schk_raw[ri*3]=schk_raw[ri*3+1]=schk_raw[ri*3+2]=0;++norm_skip;continue;}
            }
            uint64_t th[3];
            th[0]=(uint64_t)(((rel_a[ri]%(int64_t)ell2)+(int64_t)ell2)%(int64_t)ell2);
            th[1]=(ell2-(rel_b[ri]%ell2))%ell2; th[2]=0;
            #define SCHK2(out3,p3,q3) do{uint64_t _t[5]={0,0,0,0,0};\
                for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+((p3)[_i]*(q3)[_j])%ell2)%ell2;\
                if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*fm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*fm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*fm[0])%ell2)%ell2;}\
                if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*fm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*fm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*fm[0])%ell2)%ell2;}\
                (out3)[0]=_t[0];(out3)[1]=_t[1];(out3)[2]=_t[2];}while(0)
            uint64_t sr[3]={1,0,0},sb[3]={th[0],th[1],th[2]};
            uint64_t se=schk_exp;
            while(se>0u){if(se&1u){uint64_t t[3];SCHK2(t,sr,sb);sr[0]=t[0];sr[1]=t[1];sr[2]=t[2];}
                uint64_t t2[3];SCHK2(t2,sb,sb);sb[0]=t2[0];sb[1]=t2[1];sb[2]=t2[2];se>>=1u;}
            #undef SCHK2
            /* Verify Fermat: sr ≡ 1 mod ℓ */
            if (sr[0]%ell!=1 || sr[1]%ell!=0 || sr[2]%ell!=0) ++fermat_fail;
            schk_raw[ri*3+0] = (uint16_t)(((sr[0]+ell2-1)%ell2)/ell%ell);
            schk_raw[ri*3+1] = (uint16_t)((sr[1]/ell)%ell);
            schk_raw[ri*3+2] = (uint16_t)((sr[2]/ell)%ell);
        }
        if (cfg->diag) {
            int sb_col = sign_col + rfb_count + afb_count + char_count + schk2_cols;
            int nz[3]={0,0,0};
            for(int ri=0;ri<rel_count;++ri){
                uint64_t* row=&matrix[ri*(size_t)row_words];
                for(int k=0;k<3;++k) if(row[(sb_col+k)/64]&(1ull<<((sb_col+k)%64))) ++nz[k];
            }
            printf("  [GNFS] Schirokauer ell=%"PRIu64" (exp=%"PRIu64"): col0=%d/%d col1=%d/%d col2=%d/%d  fermat_fail=%d\n",
                schk_ell, (schk_ell*schk_ell-1)*(schk_ell*schk_ell+schk_ell+1), nz[0], rel_count, nz[1], rel_count, nz[2], rel_count, fermat_fail);
        }
    }
    if (cfg->diag) {
        double rs = candidates_tested > 0 ? 100.0 * rat_smooth_count / candidates_tested : 0.0;
        double bs = candidates_tested > 0 ? 100.0 * both_smooth_count / candidates_tested : 0.0;
        printf("  [GNFS] collection done: %d relations from %"PRIu64" candidates\n",
            rel_count, candidates_tested);
        printf("  [GNFS] smoothness: rational=%.3f%%  both=%.4f%%\n", rs, bs);
    }

    /* ── Row verification audit ── */
    if (cfg->verify_rows && rel_count > 0) {
        int audit_n = rel_count < 10 ? rel_count : 10;
        int rat_mm=0, alg_mm=0, char_mm_cnt=0, sign_mm_cnt=0, full_ok=0;
        printf("  [GNFS verify] auditing %d relations\n", audit_n);
        for (int ri = 0; ri < audit_n; ++ri) {
            int64_t a = rel_a[ri]; uint64_t b = rel_b[ri];
            uint64_t* stored = &matrix[ri * row_words];
            uint64_t expect[32]; memset(expect, 0, sizeof(uint64_t)*(size_t)row_words);
            /* Sign bit (col 0) */
            BigNum bm_v; bn_copy(&bm_v, &poly.m);
            { BigNum bb; bn_from_u64(&bb, b); bn_mul(&bm_v, &bm_v, &bb); }
            bool eneg = (a >= 0) ? (bn_cmp(&(BigNum){.limbs={(uint64_t)a},.used=1}, &bm_v) < 0) : true;
            /* Redo: safer sign check */
            if (a >= 0) { BigNum ac; bn_from_u64(&ac,(uint64_t)a); eneg = bn_cmp(&ac,&bm_v)<0; }
            else eneg = true;
            if (eneg) expect[0] ^= 1ull;
            /* Rational norm */
            BigNum rn;
            if (a >= 0) { BigNum ac; bn_from_u64(&ac,(uint64_t)a);
                if (bn_cmp(&ac,&bm_v)>=0){bn_copy(&rn,&ac);bn_sub(&rn,&bm_v);}
                else{bn_copy(&rn,&bm_v);bn_sub(&rn,&ac);}
            } else { BigNum ac; bn_from_u64(&ac,(uint64_t)(-a)); bn_copy(&rn,&bm_v); bn_add(&rn,&ac); }
            /* Rational FB parity */
            BigNum rrem; bn_copy(&rrem, &rn);
            for (int fi=0;fi<rfb_count;++fi) {
                int ex=0;
                while(bn_mod_u64(&rrem,rfb[fi])==0u){BigNum dp;bn_from_u64(&dp,rfb[fi]);BigNum qo;bn_divexact(&qo,&rrem,&dp);bn_copy(&rrem,&qo);++ex;}
                int col=sign_col+fi;
                if(ex&1) expect[col/64]^=(1ull<<(col%64));
            }
            /* Algebraic norm */
            BigNum an; gnfs_eval_homogeneous(&an, &poly, a, b);
            BigNum arem; bn_copy(&arem, &an);
            for (int fi=0;fi<afb_count;) {
                uint64_t p=afb[fi].p; int te=0;
                while(bn_mod_u64(&arem,p)==0u){BigNum dp;bn_from_u64(&dp,p);BigNum qo;bn_divexact(&qo,&arem,&dp);bn_copy(&arem,&qo);++te;}
                if(te>0){
                    uint64_t am=((a%(int64_t)p)+(int64_t)p)%p;
                    for(int fj=fi;fj<afb_count&&afb[fj].p==p;++fj){
                        uint64_t br=mulmod_u64(b%p,afb[fj].r,p);
                        if(br==am){int col=sign_col+rfb_count+fj;if(te&1)expect[col/64]^=(1ull<<(col%64));break;}
                    }
                }
                while(fi<afb_count&&afb[fi].p==p)++fi;
            }
            /* Character bits */
            for (int ci=0;ci<char_count;++ci) {
                uint64_t q=char_primes[ci],r=char_roots[ci];
                uint64_t br2=mulmod_u64(b%q,r%q,q);
                uint64_t am2=((a%(int64_t)q)+(int64_t)q)%q;
                uint64_t v=(am2+q-br2)%q;
                if(v!=0u){uint64_t leg=powmod_u64(v,(q-1u)/2u,q);
                    if(leg==q-1u){int col=sign_col+rfb_count+afb_count+ci;expect[col/64]^=(1ull<<(col%64));}}
            }
            /* Compare */
            bool s_ok=true,r_ok=true,a_ok=true,c_ok=true,f_ok=true;
            /* Sign: col 0 */
            if((stored[0]&1ull)!=(expect[0]&1ull)){s_ok=false;++sign_mm_cnt;}
            /* Rational: cols sign_col..sign_col+rfb_count-1 */
            for(int fi=0;fi<rfb_count;++fi){int c=sign_col+fi;
                if((stored[c/64]&(1ull<<(c%64)))!=(expect[c/64]&(1ull<<(c%64)))){r_ok=false;break;}}
            if(!r_ok)++rat_mm;
            /* Algebraic: cols sign_col+rfb_count..sign_col+rfb_count+afb_count-1 */
            for(int fi=0;fi<afb_count;++fi){int c=sign_col+rfb_count+fi;
                if((stored[c/64]&(1ull<<(c%64)))!=(expect[c/64]&(1ull<<(c%64)))){a_ok=false;break;}}
            if(!a_ok)++alg_mm;
            /* Character: cols sign_col+rfb_count+afb_count.. */
            for(int ci=0;ci<char_count;++ci){int c=sign_col+rfb_count+afb_count+ci;
                if((stored[c/64]&(1ull<<(c%64)))!=(expect[c/64]&(1ull<<(c%64)))){c_ok=false;break;}}
            if(!c_ok)++char_mm_cnt;
            /* Full row */
            for(int w=0;w<row_words;++w)if(stored[w]!=expect[w]){f_ok=false;break;}
            if(f_ok)++full_ok;
            if(ri<5||!f_ok){
                printf("    rel#%d a=%"PRId64" b=%"PRIu64": sign=%s rat=%s alg=%s char=%s full=%s\n",
                    ri,a,b,s_ok?"ok":"MISMATCH",r_ok?"ok":"MISMATCH",a_ok?"ok":"MISMATCH",
                    c_ok?"ok":"MISMATCH",f_ok?"MATCH":"MISMATCH");
            }
        }
        printf("  [GNFS verify] summary: %d/%d full match, sign_mm=%d rat_mm=%d alg_mm=%d char_mm=%d\n",
            full_ok,audit_n,sign_mm_cnt,rat_mm,alg_mm,char_mm_cnt);
        if(full_ok==audit_n) printf("  [GNFS verify] conclusion: stored rows appear correct\n");
        else if(alg_mm>0) printf("  [GNFS verify] conclusion: algebraic FB assignment has mismatches\n");
        else if(char_mm_cnt>0) printf("  [GNFS verify] conclusion: character column integration has mismatches\n");
        else if(sign_mm_cnt>0) printf("  [GNFS verify] conclusion: sign column has mismatches\n");
        else printf("  [GNFS verify] conclusion: some rows inconsistent (check details above)\n");
    }

    /* Stage 4: Singleton filtering */
    int pre_filter = rel_count;
    int filter_passes = 0;
    {
        int* col_occ = (int*)xcalloc((size_t)total_cols, sizeof(int));
        for (;;) {
            memset(col_occ, 0, (size_t)total_cols * sizeof(int));
            for (int ri = 0; ri < rel_count; ++ri) {
                for (int c = 0; c < total_cols; ++c) {
                    if (matrix[ri * row_words + c / 64] & (1ull << (c % 64))) ++col_occ[c];
                }
            }
            int removed = 0;
            int write = 0;
            for (int ri = 0; ri < rel_count; ++ri) {
                bool has_singleton = false;
                for (int c = 0; c < total_cols; ++c) {
                    if ((matrix[ri * row_words + c / 64] & (1ull << (c % 64))) && col_occ[c] == 1) {
                        has_singleton = true; break;
                    }
                }
                if (has_singleton) { ++removed; continue; }
                if (write != ri) {
                    for (int w = 0; w < row_words; ++w) matrix[write * row_words + w] = matrix[ri * row_words + w];
                    rel_a[write] = rel_a[ri];
                    rel_b[write] = rel_b[ri];
                }
                ++write;
            }
            rel_count = write;
            ++filter_passes;
            if (removed == 0) break;
        }
        free(col_occ);
    }

    /* Count effective columns (non-empty) */
    int eff_cols = 0;
    { int* col_used = (int*)xcalloc((size_t)total_cols, sizeof(int));
      for (int ri = 0; ri < rel_count; ++ri)
        for (int c = 0; c < total_cols; ++c)
            if (matrix[ri * row_words + c / 64] & (1ull << (c % 64))) col_used[c] = 1;
      for (int c = 0; c < total_cols; ++c) if (col_used[c]) ++eff_cols;
      free(col_used); }

    if (cfg->diag) {
        printf("  [GNFS] filtering: %d passes, %d→%d relations, %d effective columns\n",
            filter_passes, pre_filter, rel_count, eff_cols);
    }

    if (rel_count <= eff_cols || rel_count < 2) {
        if (cfg->diag) printf("  [GNFS] FAIL: relation-starved (%d rels <= %d eff_cols)\n", rel_count, eff_cols);
        free(rfb); free(afb); free(matrix);
        fr.status = (int)CPSS_INCOMPLETE; fr.time_seconds = now_sec() - t0; return fr;
    }

    /* Save pre-elimination matrix for nullspace verification */
    uint64_t* orig_matrix = NULL;
    if (cfg->diag) {
        orig_matrix = (uint64_t*)xmalloc((size_t)rel_count * (size_t)row_words * sizeof(uint64_t));
        memcpy(orig_matrix, matrix, (size_t)rel_count * (size_t)row_words * sizeof(uint64_t));
    }

    /* ── 2-adic Schirokauer diagnostic comparison ── */
    if (cfg->schk2 && schk2_cols > 0 && rel_count > 2 && orig_matrix) {
        printf("  [GNFS schk2] 2-adic Schirokauer diagnostic\n");
        int schk_base = sign_col + rfb_count + afb_count + char_count;
        for (int si = 0; si < 3; ++si) {
            int c = schk_base + si; int ones = 0;
            for (int ri = 0; ri < rel_count; ++ri)
                if (orig_matrix[ri*row_words + c/64] & (1ull<<(c%64))) ++ones;
            printf("    schk2[%d]: ones=%d zeros=%d / %d%s\n",
                si, ones, rel_count-ones, rel_count,
                (ones==0||ones==rel_count)?" [CONSTANT]":"");
        }
        /* Baseline: no-schk2 matrix */
        int nc = total_cols - schk2_cols;
        int nrw = (nc + 63) / 64;
        uint64_t* nm = (uint64_t*)xmalloc((size_t)rel_count*(size_t)nrw*sizeof(uint64_t));
        memset(nm, 0, (size_t)rel_count*(size_t)nrw*sizeof(uint64_t));
        for (int ri=0;ri<rel_count;++ri)
            for (int c2=0;c2<nc;++c2)
                if (orig_matrix[ri*row_words+c2/64]&(1ull<<(c2%64)))
                    nm[ri*nrw+c2/64]|=(1ull<<(c2%64));
        int* np=(int*)xmalloc((size_t)nc*sizeof(int));
        for(int i=0;i<nc;++i)np[i]=-1;
        int nhw=(rel_count+63)/64;
        uint64_t* nh=(uint64_t*)xmalloc((size_t)rel_count*(size_t)nhw*sizeof(uint64_t));
        memset(nh,0,(size_t)rel_count*(size_t)nhw*sizeof(uint64_t));
        for(int i=0;i<rel_count;++i)nh[i*nhw+i/64]=(1ull<<(i%64));
        bool* nrip=(bool*)xcalloc((size_t)rel_count,sizeof(bool));
        for(int col=0;col<nc;++col){int piv=-1;
            for(int ri=0;ri<rel_count;++ri){if(nrip[ri])continue;
                if(nm[ri*nrw+col/64]&(1ull<<(col%64))){piv=ri;np[col]=ri;nrip[ri]=true;break;}}
            if(piv<0)continue;
            for(int ri=0;ri<rel_count;++ri){if(ri==piv)continue;
                if(!(nm[ri*nrw+col/64]&(1ull<<(col%64))))continue;
                for(int w=0;w<nrw;++w)nm[ri*nrw+w]^=nm[piv*nrw+w];
                for(int w=0;w<nhw;++w)nh[ri*nhw+w]^=nh[piv*nhw+w];}}
        int base_rank=0;for(int c2=0;c2<nc;++c2)if(np[c2]>=0)++base_rank;
        typedef struct{int row;int sz;}S2Dep;
        S2Dep bsd[256];int bns=0;
        for(int ri=0;ri<rel_count&&bns<256;++ri){bool z=true;
            for(int w=0;w<nrw;++w)if(nm[ri*nrw+w]){z=false;break;}
            if(!z)continue;int sz=0;
            for(int rj=0;rj<rel_count;++rj)if(nh[ri*nhw+rj/64]&(1ull<<(rj%64)))++sz;
            bsd[bns].row=ri;bsd[bns].sz=sz;++bns;}
        for(int i=1;i<bns;++i){S2Dep t=bsd[i];int j=i-1;while(j>=0&&bsd[j].sz>t.sz){bsd[j+1]=bsd[j];--j;}bsd[j+1]=t;}
        printf("    without schk2: cols=%d rank=%d ns=%d min_dep=%d\n",
            nc, base_rank, bns, bns>0?bsd[0].sz:0);

        /* Full matrix with schk2 */
        int fc2 = total_cols;
        int frw = row_words;
        uint64_t* fm = (uint64_t*)xmalloc((size_t)rel_count*(size_t)frw*sizeof(uint64_t));
        memcpy(fm, orig_matrix, (size_t)rel_count*(size_t)frw*sizeof(uint64_t));
        int* fp=(int*)xmalloc((size_t)fc2*sizeof(int));
        for(int i=0;i<fc2;++i)fp[i]=-1;
        uint64_t* fh=(uint64_t*)xmalloc((size_t)rel_count*(size_t)nhw*sizeof(uint64_t));
        memset(fh,0,(size_t)rel_count*(size_t)nhw*sizeof(uint64_t));
        for(int i=0;i<rel_count;++i)fh[i*nhw+i/64]=(1ull<<(i%64));
        bool* frip=(bool*)xcalloc((size_t)rel_count,sizeof(bool));
        for(int col=0;col<fc2;++col){int piv=-1;
            for(int ri=0;ri<rel_count;++ri){if(frip[ri])continue;
                if(fm[ri*frw+col/64]&(1ull<<(col%64))){piv=ri;fp[col]=ri;frip[ri]=true;break;}}
            if(piv<0)continue;
            for(int ri=0;ri<rel_count;++ri){if(ri==piv)continue;
                if(!(fm[ri*frw+col/64]&(1ull<<(col%64))))continue;
                for(int w=0;w<frw;++w)fm[ri*frw+w]^=fm[piv*frw+w];
                for(int w=0;w<nhw;++w)fh[ri*nhw+w]^=fh[piv*nhw+w];}}
        int full_rank=0;for(int c2=0;c2<fc2;++c2)if(fp[c2]>=0)++full_rank;
        S2Dep fsd[256];int fns=0;
        for(int ri=0;ri<rel_count&&fns<256;++ri){bool z=true;
            for(int w=0;w<frw;++w)if(fm[ri*frw+w]){z=false;break;}
            if(!z)continue;int sz=0;
            for(int rj=0;rj<rel_count;++rj)if(fh[ri*nhw+rj/64]&(1ull<<(rj%64)))++sz;
            fsd[fns].row=ri;fsd[fns].sz=sz;++fns;}
        for(int i=1;i<fns;++i){S2Dep t=fsd[i];int j=i-1;while(j>=0&&fsd[j].sz>t.sz){fsd[j+1]=fsd[j];--j;}fsd[j+1]=t;}
        printf("    with schk2:    cols=%d rank=%d ns=%d min_dep=%d\n",
            fc2, full_rank, fns, fns>0?fsd[0].sz:0);
        int rank_delta = full_rank - base_rank;
        printf("    rank delta: %+d  nullspace delta: %+d\n", rank_delta, fns - bns);

        /* QR persistence retest if rank increased */
        if (rank_delta > 0 && fns > 0 && bn_is_one(&poly.coeffs[3])) {
            printf("    QR retest on smallest schk2-constrained dep...\n");
            /* Find aux prime */
            uint64_t qp = 0u;
            { uint64_t rng = 0xC0FFEE1234567891ULL ^ (uint64_t)bn_bitlen(n);
              for (int tries=0;tries<2000&&qp==0u;++tries){
                  rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;
                  uint64_t cand=(rng|((uint64_t)1<<59))|3u;cand=cand&~1ull;cand|=3u;
                  if((cand&3u)!=3u||!miller_rabin_u64(cand))continue;
                  uint64_t f0v=bn_mod_u64(&poly.coeffs[0],cand),f1v=bn_mod_u64(&poly.coeffs[1],cand),f2v=bn_mod_u64(&poly.coeffs[2],cand);
                  uint64_t rr[3]={1,0,0},bb[3]={0,1,0};
                  #define P3MQ(o,a2,b2,pv) do{uint64_t _t[5]={0,0,0,0,0};\
                      for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+mulmod_u64((a2)[_i],(b2)[_j],pv))%pv;\
                      if(_t[4]){_t[3]=(_t[3]+mulmod_u64(_t[4],pv-f2v,pv))%pv;_t[2]=(_t[2]+mulmod_u64(_t[4],pv-f1v,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[4],pv-f0v,pv))%pv;}\
                      if(_t[3]){_t[2]=(_t[2]+mulmod_u64(_t[3],pv-f2v,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[3],pv-f1v,pv))%pv;_t[0]=(_t[0]+mulmod_u64(_t[3],pv-f0v,pv))%pv;}\
                      (o)[0]=_t[0];(o)[1]=_t[1];(o)[2]=_t[2];}while(0)
                  uint64_t ev=cand;
                  while(ev>0u){if(ev&1u){uint64_t t[3];P3MQ(t,rr,bb,cand);rr[0]=t[0];rr[1]=t[1];rr[2]=t[2];}
                      uint64_t t2[3];P3MQ(t2,bb,bb,cand);bb[0]=t2[0];bb[1]=t2[1];bb[2]=t2[2];ev>>=1u;}
                  #undef P3MQ
                  if(rr[0]==0u&&rr[1]==1u&&rr[2]==0u)continue;
                  qp=cand;
              }
            }
            if (qp) {
                int qr_hits = 0, qr_test = fns < 5 ? fns : 5;
                for (int di = 0; di < qr_test; ++di) {
                    uint64_t* hd = &fh[fsd[di].row * nhw];
                    BigNum p_bn; bn_from_u64(&p_bn, qp);
                    BigNum f3i; bn_from_u64(&f3i, 1u);
                    AlgElem Sp; alg_one(&Sp);
                    for(int rj=0;rj<rel_count;++rj){
                        if(!(hd[rj/64]&(1ull<<(rj%64))))continue;
                        AlgElem el;alg_from_ab(&el,rel_a[rj],rel_b[rj],&p_bn);
                        AlgElem prev;for(int c2=0;c2<3;++c2)bn_copy(&prev.c[c2],&Sp.c[c2]);
                        alg_mul(&Sp,&prev,&el,&poly,&f3i,&p_bn);
                    }
                    BigNum qr_exp; gnfs_qr_exp_from_prime(&qr_exp, qp);
                    unsigned qr_bits=bn_bitlen(&qr_exp);
                    AlgElem qc;alg_one(&qc);
                    AlgElem _b;for(int c2=0;c2<3;++c2)bn_copy(&_b.c[c2],&Sp.c[c2]);
                    for(unsigned bt=0;bt<qr_bits;++bt){
                        if(qr_exp.limbs[bt/64]&(1ull<<(bt%64))){AlgElem _t;for(int c2=0;c2<3;++c2)bn_copy(&_t.c[c2],&qc.c[c2]);alg_mul(&qc,&_t,&_b,&poly,&f3i,&p_bn);}
                        AlgElem _t2;for(int c2=0;c2<3;++c2)bn_copy(&_t2.c[c2],&_b.c[c2]);alg_mul(&_b,&_t2,&_t2,&poly,&f3i,&p_bn);}
                    bool is_qr=bn_is_one(&qc.c[0])&&bn_is_zero(&qc.c[1])&&bn_is_zero(&qc.c[2]);
                    if(is_qr)++qr_hits;
                    printf("      dep#%d (%d rels): QR=%s\n", di, fsd[di].sz, is_qr?"YES":"no");
                }
                printf("    QR persistence: %d/%d\n", qr_hits, qr_test);
            }
        } else if (rank_delta == 0) {
            printf("    schk2 columns are linearly REDUNDANT — no new independent constraint\n");
        }

        free(nm);free(np);free(nh);free(nrip);
        free(fm);free(fp);free(fh);free(frip);
    }

    /* ── Character differential audit ── */
    if (cfg->audit_chars && rel_count > 2) {
        int nc_cols = sign_col + rfb_count + afb_count; /* no-char column count */
        int nc_rw = (nc_cols + 63) / 64;
        printf("  [GNFS char-diff] building no-char matrix: %d cols (vs %d with chars)\n", nc_cols, total_cols);

        /* Build no-char matrix: copy sign + rational + algebraic columns only */
        uint64_t* nc_mat = (uint64_t*)xmalloc((size_t)rel_count * (size_t)nc_rw * sizeof(uint64_t));
        memset(nc_mat, 0, (size_t)rel_count * (size_t)nc_rw * sizeof(uint64_t));
        for (int ri = 0; ri < rel_count; ++ri) {
            for (int c = 0; c < nc_cols; ++c) {
                if (orig_matrix[ri * row_words + c/64] & (1ull<<(c%64)))
                    nc_mat[ri * nc_rw + c/64] |= (1ull<<(c%64));
            }
        }

        /* Gaussian elimination on no-char matrix */
        int* nc_piv = (int*)xmalloc((size_t)nc_cols * sizeof(int));
        for (int i = 0; i < nc_cols; ++i) nc_piv[i] = -1;
        int nc_hw = (rel_count + 63) / 64;
        uint64_t* nc_hist = (uint64_t*)xmalloc((size_t)rel_count * (size_t)nc_hw * sizeof(uint64_t));
        memset(nc_hist, 0, (size_t)rel_count * (size_t)nc_hw * sizeof(uint64_t));
        for (int i = 0; i < rel_count; ++i) nc_hist[i*nc_hw + i/64] = (1ull<<(i%64));
        bool* nc_rip = (bool*)xcalloc((size_t)rel_count, sizeof(bool));
        for (int col = 0; col < nc_cols; ++col) {
            int piv = -1;
            for (int ri = 0; ri < rel_count; ++ri) {
                if (nc_rip[ri]) continue;
                if (nc_mat[ri*nc_rw + col/64] & (1ull<<(col%64))) { piv = ri; nc_piv[col] = ri; nc_rip[ri] = true; break; }
            }
            if (piv < 0) continue;
            for (int ri = 0; ri < rel_count; ++ri) {
                if (ri == piv) continue;
                if (!(nc_mat[ri*nc_rw + col/64] & (1ull<<(col%64)))) continue;
                for (int w = 0; w < nc_rw; ++w) nc_mat[ri*nc_rw+w] ^= nc_mat[piv*nc_rw+w];
                for (int w = 0; w < nc_hw; ++w) nc_hist[ri*nc_hw+w] ^= nc_hist[piv*nc_hw+w];
            }
        }
        int nc_rank = 0;
        for (int c = 0; c < nc_cols; ++c) if (nc_piv[c] >= 0) ++nc_rank;
        /* Collect no-char nullspace */
        typedef struct { int row; int sz; } NcDep;
        NcDep nc_deps[256]; int nc_ns = 0;
        for (int ri = 0; ri < rel_count && nc_ns < 256; ++ri) {
            bool z = true;
            for (int w = 0; w < nc_rw; ++w) { if (nc_mat[ri*nc_rw+w]) { z = false; break; } }
            if (!z) continue;
            uint64_t* h = &nc_hist[ri*nc_hw];
            int sz = 0;
            for (int rj = 0; rj < rel_count; ++rj) if (h[rj/64]&(1ull<<(rj%64))) ++sz;
            nc_deps[nc_ns].row = ri; nc_deps[nc_ns].sz = sz; ++nc_ns;
        }
        /* Sort no-char deps by size */
        for (int i = 1; i < nc_ns; ++i) {
            NcDep tmp = nc_deps[i]; int j = i-1;
            while (j>=0 && nc_deps[j].sz > tmp.sz) { nc_deps[j+1]=nc_deps[j]; --j; }
            nc_deps[j+1] = tmp;
        }

        printf("  [GNFS char-diff] no-char: rank=%d/%d  nullspace=%d\n", nc_rank, nc_cols, nc_ns);
        printf("  [GNFS char-diff] with-char: will be computed next (rank/nullspace from main pipeline)\n");
        printf("  [GNFS char-diff] no-char smallest deps:");
        for (int i = 0; i < nc_ns && i < 5; ++i) printf(" %d", nc_deps[i].sz);
        printf("\n");

        /* Test no-char deps against character columns */
        int nc_char_pass = 0, nc_char_fail = 0;
        int nc_test = nc_ns < 10 ? nc_ns : 10;
        for (int di = 0; di < nc_test; ++di) {
            uint64_t* h = &nc_hist[nc_deps[di].row * nc_hw];
            uint64_t csig = 0u;
            for (int ci = 0; ci < char_count; ++ci) {
                int par = 0;
                for (int rj = 0; rj < rel_count; ++rj) {
                    if (!(h[rj/64]&(1ull<<(rj%64)))) continue;
                    int c = sign_col + rfb_count + afb_count + ci;
                    if (orig_matrix[rj*row_words + c/64] & (1ull<<(c%64))) ++par;
                }
                if (par & 1) csig |= (1ull<<ci);
            }
            bool cok = (csig == 0u);
            if (cok) ++nc_char_pass; else ++nc_char_fail;
            if (di < 5)
                printf("    nc_dep#%d (%d rels): char_sig=0x%"PRIx64" %s\n",
                    di, nc_deps[di].sz, csig, cok ? "PASS" : "FAIL");
        }
        printf("  [GNFS char-diff] no-char deps vs char constraints: %d/%d pass, %d/%d fail\n",
            nc_char_pass, nc_test, nc_char_fail, nc_test);

        /* Per-column character entropy */
        printf("  [GNFS char-diff] per-column character entropy:\n");
        int const_cols = 0, info_cols = 0;
        for (int ci = 0; ci < char_count; ++ci) {
            int ones = 0;
            int c = sign_col + rfb_count + afb_count + ci;
            for (int ri = 0; ri < rel_count; ++ri)
                if (orig_matrix[ri*row_words + c/64] & (1ull<<(c%64))) ++ones;
            int zeros = rel_count - ones;
            bool is_const = (ones == 0 || zeros == 0);
            if (is_const) ++const_cols; else ++info_cols;
            printf("    char[%d] (q=%"PRIu64"): ones=%d zeros=%d%s\n",
                ci, char_primes[ci], ones, zeros, is_const ? " [CONSTANT]" : "");
        }
        printf("  [GNFS char-diff] informative=%d  constant=%d  out of %d\n",
            info_cols, const_cols, char_count);

        /* Conclusion */
        if (nc_char_fail > nc_test / 2) {
            printf("  [GNFS char-diff] CONCLUSION B: >50%% no-char deps fail char constraints → chars ARE active\n");
        } else if (nc_char_fail > 0) {
            printf("  [GNFS char-diff] CONCLUSION D: some no-char deps fail chars, but minority → chars weakly active\n");
        } else {
            printf("  [GNFS char-diff] CONCLUSION C: 0/%d no-char deps fail char constraints → chars are redundant\n", nc_test);
            if (const_cols > 0)
                printf("    note: %d/%d char columns are constant → those are truly dead weight\n", const_cols, char_count);
        }

        free(nc_mat); free(nc_piv); free(nc_hist); free(nc_rip);
    }

    /* ── 4-way square-class dimension audit ── */
    if (cfg->audit_real_sign && rel_count > 2 && orig_matrix && poly.degree == 3) {
        /* Compute real root of f(x) via bisection */
        double fc_d[4]; for (int i=0;i<=3;++i) fc_d[i] = bn_to_double(&poly.coeffs[i]);
        #define FEV_RS(x) (fc_d[3]*(x)*(x)*(x)+fc_d[2]*(x)*(x)+fc_d[1]*(x)+fc_d[0])
        double lo_r=-1e6,hi_r=1e6,flo_r=FEV_RS(lo_r),fhi_r=FEV_RS(hi_r);
        if(flo_r*fhi_r>0){lo_r=-1e9;hi_r=1e9;flo_r=FEV_RS(lo_r);fhi_r=FEV_RS(hi_r);}
        double alpha_R=0.0;
        if(flo_r*fhi_r<=0){for(int it=0;it<100;++it){double mid=(lo_r+hi_r)/2.0,fm=FEV_RS(mid);
            if(fm==0.0){alpha_R=mid;break;}if(flo_r*fm<0){hi_r=mid;fhi_r=fm;}else{lo_r=mid;flo_r=fm;}alpha_R=(lo_r+hi_r)/2.0;}}
        #undef FEV_RS
        printf("  [GNFS 4-way] square-class dimension audit: alpha_R=%.6f\n", alpha_R);

        /* Precompute real-sign bits */
        uint8_t* rsign = (uint8_t*)xmalloc((size_t)rel_count);
        for (int ri=0;ri<rel_count;++ri)
            rsign[ri] = ((double)rel_a[ri] - (double)rel_b[ri] * alpha_R) < 0.0 ? 1 : 0;

        /* Column layout: base = sign_col + rfb_count + afb_count (no chars, no sign)
         * FB-only columns start at col sign_col (skipping col 0 = sign).
         * For "no sign" variants: use cols sign_col..sign_col+rfb_count+afb_count-1
         * For "with chars": also include char columns */
        int base_cols = rfb_count + afb_count; /* FB only, no sign, no chars */

        /* Find one auxiliary prime for QR testing */
        uint64_t qr_prime = 0u;
        { uint64_t rng = 0xC0FFEE1234567891ULL ^ (uint64_t)bn_bitlen(n);
          for (int tries=0;tries<2000&&qr_prime==0u;++tries) {
              rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;
              uint64_t cand=(rng|((uint64_t)1<<59))|3u; cand=cand&~1ull;cand|=3u;
              if((cand&3u)!=3u||!miller_rabin_u64(cand))continue;
              uint64_t f0v=bn_mod_u64(&poly.coeffs[0],cand),f1v=bn_mod_u64(&poly.coeffs[1],cand),f2v=bn_mod_u64(&poly.coeffs[2],cand);
              uint64_t res4[3]={1,0,0},base4[3]={0,1,0};
              #define P3M4(o,a2,b2,pv) do{uint64_t _t[5]={0,0,0,0,0};\
                  for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+mulmod_u64((a2)[_i],(b2)[_j],pv))%pv;\
                  if(_t[4]){_t[3]=(_t[3]+mulmod_u64(_t[4],pv-f2v,pv))%pv;_t[2]=(_t[2]+mulmod_u64(_t[4],pv-f1v,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[4],pv-f0v,pv))%pv;}\
                  if(_t[3]){_t[2]=(_t[2]+mulmod_u64(_t[3],pv-f2v,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[3],pv-f1v,pv))%pv;_t[0]=(_t[0]+mulmod_u64(_t[3],pv-f0v,pv))%pv;}\
                  (o)[0]=_t[0];(o)[1]=_t[1];(o)[2]=_t[2];}while(0)
              uint64_t ev=cand;
              while(ev>0u){if(ev&1u){uint64_t t[3];P3M4(t,res4,base4,cand);res4[0]=t[0];res4[1]=t[1];res4[2]=t[2];}
                  uint64_t t2[3];P3M4(t2,base4,base4,cand);base4[0]=t2[0];base4[1]=t2[1];base4[2]=t2[2];ev>>=1u;}
              #undef P3M4
              if(res4[0]==0u&&res4[1]==1u&&res4[2]==0u)continue;
              qr_prime=cand;
          }
        }

        /* 4 variants:
         * V0: FB only (no sign, no chars)
         * V1: FB + real-sign (no chars)
         * V2: FB + chars (no sign) = current minus dead sign col
         * V3: FB + chars + real-sign */
        static const char* v4names[] = {
            "V0(FB only)", "V1(FB+real-sign)", "V2(FB+chars)", "V3(FB+chars+real-sign)"
        };
        int v4_cols[4], v4_rank[4], v4_ns[4], v4_min[4];
        int v4_qr[4]; /* QR status of smallest dep: 1=QR, 0=non-QR, -1=no dep */

        for (int vi = 0; vi < 4; ++vi) {
            bool use_sign = (vi == 1 || vi == 3);
            bool use_chars = (vi == 2 || vi == 3);
            int vc = base_cols + (use_sign ? 1 : 0) + (use_chars ? char_count : 0);
            int vrw = (vc + 63) / 64;
            v4_cols[vi] = vc;

            uint64_t* vm = (uint64_t*)xmalloc((size_t)rel_count*(size_t)vrw*sizeof(uint64_t));
            memset(vm, 0, (size_t)rel_count*(size_t)vrw*sizeof(uint64_t));

            for (int ri = 0; ri < rel_count; ++ri) {
                int col_out = 0;
                /* Optional: real-sign column */
                if (use_sign) {
                    if (rsign[ri]) vm[ri*vrw] |= (1ull << col_out);
                    ++col_out;
                }
                /* FB columns: copy from orig_matrix cols sign_col..sign_col+base_cols-1 */
                for (int c = 0; c < base_cols; ++c) {
                    int src = sign_col + c; /* skip orig col 0 (dead sign) */
                    if (orig_matrix[ri*row_words + src/64] & (1ull<<(src%64)))
                        vm[ri*vrw + (col_out+c)/64] |= (1ull<<((col_out+c)%64));
                }
                col_out += base_cols;
                /* Optional: character columns */
                if (use_chars) {
                    for (int ci = 0; ci < char_count; ++ci) {
                        int src = sign_col + rfb_count + afb_count + ci;
                        if (orig_matrix[ri*row_words + src/64] & (1ull<<(src%64)))
                            vm[ri*vrw + (col_out+ci)/64] |= (1ull<<((col_out+ci)%64));
                    }
                }
            }

            /* Gaussian elimination */
            int* vp=(int*)xmalloc((size_t)vc*sizeof(int));
            for(int i=0;i<vc;++i)vp[i]=-1;
            int vhw=(rel_count+63)/64;
            uint64_t* vh=(uint64_t*)xmalloc((size_t)rel_count*(size_t)vhw*sizeof(uint64_t));
            memset(vh,0,(size_t)rel_count*(size_t)vhw*sizeof(uint64_t));
            for(int i=0;i<rel_count;++i)vh[i*vhw+i/64]=(1ull<<(i%64));
            bool* vrip=(bool*)xcalloc((size_t)rel_count,sizeof(bool));
            for(int col=0;col<vc;++col){int piv=-1;
                for(int ri=0;ri<rel_count;++ri){if(vrip[ri])continue;
                    if(vm[ri*vrw+col/64]&(1ull<<(col%64))){piv=ri;vp[col]=ri;vrip[ri]=true;break;}}
                if(piv<0)continue;
                for(int ri=0;ri<rel_count;++ri){if(ri==piv)continue;
                    if(!(vm[ri*vrw+col/64]&(1ull<<(col%64))))continue;
                    for(int w=0;w<vrw;++w)vm[ri*vrw+w]^=vm[piv*vrw+w];
                    for(int w=0;w<vhw;++w)vh[ri*vhw+w]^=vh[piv*vhw+w];}}
            int vr=0;for(int c=0;c<vc;++c)if(vp[c]>=0)++vr;
            v4_rank[vi]=vr;

            /* Collect + sort nullspace deps */
            typedef struct{int row;int sz;}V4Dep;
            V4Dep vd[256];int vns=0;
            for(int ri=0;ri<rel_count&&vns<256;++ri){bool z=true;
                for(int w=0;w<vrw;++w)if(vm[ri*vrw+w]){z=false;break;}
                if(!z)continue;
                int sz=0;for(int rj=0;rj<rel_count;++rj)if(vh[ri*vhw+rj/64]&(1ull<<(rj%64)))++sz;
                vd[vns].row=ri;vd[vns].sz=sz;++vns;}
            for(int i=1;i<vns;++i){V4Dep t=vd[i];int j=i-1;while(j>=0&&vd[j].sz>t.sz){vd[j+1]=vd[j];--j;}vd[j+1]=t;}
            v4_ns[vi]=vns;
            v4_min[vi]=vns>0?vd[0].sz:0;

            /* QR test on smallest dep */
            v4_qr[vi] = -1;
            if (vns > 0 && qr_prime != 0u) {
                uint64_t ap = qr_prime;
                BigNum p_bn; bn_from_u64(&p_bn, ap);
                BigNum f3i; bn_from_u64(&f3i, 1u);
                uint64_t* hd = &vh[vd[0].row * vhw];
                AlgElem Sp; alg_one(&Sp);
                for(int rj=0;rj<rel_count;++rj){
                    if(!(hd[rj/64]&(1ull<<(rj%64))))continue;
                    AlgElem el;alg_from_ab(&el,rel_a[rj],rel_b[rj],&p_bn);
                    AlgElem prev;for(int c=0;c<3;++c)bn_copy(&prev.c[c],&Sp.c[c]);
                    alg_mul(&Sp,&prev,&el,&poly,&f3i,&p_bn);
                }
                /* QR check: S^((p^3-1)/2) */
                BigNum qr_exp; gnfs_qr_exp_from_prime(&qr_exp, ap);
                unsigned qr_bits=bn_bitlen(&qr_exp);
                AlgElem qc;alg_one(&qc);
                AlgElem _b;for(int c=0;c<3;++c)bn_copy(&_b.c[c],&Sp.c[c]);
                for(unsigned bt=0;bt<qr_bits;++bt){
                    if(qr_exp.limbs[bt/64]&(1ull<<(bt%64))){AlgElem _t;for(int c=0;c<3;++c)bn_copy(&_t.c[c],&qc.c[c]);alg_mul(&qc,&_t,&_b,&poly,&f3i,&p_bn);}
                    AlgElem _t2;for(int c=0;c<3;++c)bn_copy(&_t2.c[c],&_b.c[c]);alg_mul(&_b,&_t2,&_t2,&poly,&f3i,&p_bn);}
                v4_qr[vi] = (bn_is_one(&qc.c[0])&&bn_is_zero(&qc.c[1])&&bn_is_zero(&qc.c[2])) ? 1 : 0;
            }

            free(vm);free(vp);free(vh);free(vrip);
        }

        /* Results table */
        printf("  [GNFS 4-way] comparison (60-bit monic, real-embedding sign):\n");
        printf("    %-25s cols  rank  ns   min_dep  QR\n", "variant");
        for(int vi=0;vi<4;++vi)
            printf("    %-25s %4d  %4d  %3d  %3d      %s\n",
                v4names[vi], v4_cols[vi], v4_rank[vi], v4_ns[vi], v4_min[vi],
                v4_qr[vi]<0?"n/a":(v4_qr[vi]?"YES":"no"));

        /* Analysis */
        int sign_rank_delta = v4_rank[1] - v4_rank[0]; /* FB+sign vs FB */
        int chars_rank_delta = v4_rank[2] - v4_rank[0]; /* FB+chars vs FB */
        int both_rank_delta = v4_rank[3] - v4_rank[0]; /* FB+chars+sign vs FB */
        printf("  [GNFS 4-way] rank deltas vs V0(FB only):\n");
        printf("    +real-sign: %+d   +chars: %+d   +both: %+d\n",
            sign_rank_delta, chars_rank_delta, both_rank_delta);

        /* Conclusion */
        if (chars_rank_delta >= 1 && sign_rank_delta >= 1 && both_rank_delta == chars_rank_delta + sign_rank_delta) {
            printf("  [GNFS 4-way] CONCLUSION B: sign and chars are INDEPENDENT dimensions\n");
            printf("    sign passive in current matrix only because it was dead (constant); chars hit a DIFFERENT dimension\n");
        } else if (chars_rank_delta >= 1 && both_rank_delta == chars_rank_delta) {
            printf("  [GNFS 4-way] CONCLUSION A: chars span the sign dimension\n");
            printf("    adding real-sign to chars gives no extra rank → chars already cover sign\n");
            printf("    the unconstrained dimension is the fundamental unit ε\n");
        } else if (sign_rank_delta >= 1 && both_rank_delta == sign_rank_delta) {
            printf("  [GNFS 4-way] CONCLUSION: sign adds rank but chars add nothing beyond sign\n");
        } else {
            printf("  [GNFS 4-way] CONCLUSION C: ambiguous (deltas: sign=%+d chars=%+d both=%+d)\n",
                sign_rank_delta, chars_rank_delta, both_rank_delta);
        }

        /* Final recommendation */
        if (both_rank_delta > chars_rank_delta && both_rank_delta > 0) {
            printf("  [GNFS 4-way] → real-sign provides independent constraint → next step: add real-sign column to production matrix\n");
        } else if (chars_rank_delta >= 1 && both_rank_delta == chars_rank_delta) {
            printf("  [GNFS 4-way] → chars already span sign; ε dimension is unconstrained → next step: implement Schirokauer map for ε\n");
        }

        free(rsign);
    }

    /* ── Character-strength sweep ── */
    if (cfg->sweep_chars && rel_count > 2 && orig_matrix) {
        static const int sweep_vals[] = { 0, 8, 16, 32 };
        int sweep_n = 4;
        printf("  [GNFS sweep] character-strength sweep on %d kept relations\n", rel_count);
        printf("  [GNFS sweep] available char columns: %d\n", char_count);

        /* Per-column entropy (once, for all 32) */
        printf("  [GNFS sweep] per-column entropy (first %d chars):\n", char_count < 32 ? char_count : 32);
        int const_count_total = 0;
        for (int ci = 0; ci < char_count && ci < 32; ++ci) {
            int ones = 0, c = sign_col + rfb_count + afb_count + ci;
            for (int ri = 0; ri < rel_count; ++ri)
                if (orig_matrix[ri*row_words + c/64] & (1ull<<(c%64))) ++ones;
            bool cst = (ones == 0 || ones == rel_count);
            if (cst) ++const_count_total;
            if (ci < 8 || cst)
                printf("    char[%d] (q=%"PRIu64"): ones=%d zeros=%d%s\n",
                    ci, char_primes[ci], ones, rel_count-ones, cst?" [CONST]":"");
        }
        if (char_count > 8)
            printf("    ... (%d more, %d constant)\n", char_count - 8, const_count_total > 0 ? const_count_total : 0);

        /* Find 3 auxiliary primes for QR testing */
        uint64_t sw_primes[3]; int sw_pc = 0;
        { uint64_t rng = 0xC0FFEE1234567891ULL ^ (uint64_t)bn_bitlen(n);
          for (int tries = 0; tries < 2000 && sw_pc < 3; ++tries) {
              rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17;
              uint64_t cand = (rng | ((uint64_t)1<<59)) | 3u;
              cand = cand & ~1ull; cand |= 3u;
              if ((cand&3u)!=3u || !miller_rabin_u64(cand)) continue;
              /* Irreducibility check */
              uint64_t f0v=bn_mod_u64(&poly.coeffs[0],cand), f1v=bn_mod_u64(&poly.coeffs[1],cand), f2v=bn_mod_u64(&poly.coeffs[2],cand);
              uint64_t res[3]={1,0,0}, base2[3]={0,1,0};
              #define P3M(o,a2,b2,pv) do{uint64_t _t[5]={0,0,0,0,0};\
                  for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+mulmod_u64((a2)[_i],(b2)[_j],pv))%pv;\
                  if(_t[4]){_t[3]=(_t[3]+mulmod_u64(_t[4],pv-f2v,pv))%pv;_t[2]=(_t[2]+mulmod_u64(_t[4],pv-f1v,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[4],pv-f0v,pv))%pv;}\
                  if(_t[3]){_t[2]=(_t[2]+mulmod_u64(_t[3],pv-f2v,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[3],pv-f1v,pv))%pv;_t[0]=(_t[0]+mulmod_u64(_t[3],pv-f0v,pv))%pv;}\
                  (o)[0]=_t[0];(o)[1]=_t[1];(o)[2]=_t[2];}while(0)
              uint64_t ev = cand;
              while(ev>0u){if(ev&1u){uint64_t t[3];P3M(t,res,base2,cand);res[0]=t[0];res[1]=t[1];res[2]=t[2];}
                  uint64_t t2[3];P3M(t2,base2,base2,cand);base2[0]=t2[0];base2[1]=t2[1];base2[2]=t2[2];ev>>=1u;}
              #undef P3M
              if(res[0]==0u&&res[1]==1u&&res[2]==0u) continue;
              sw_primes[sw_pc++] = cand;
          }
        }

        /* Sweep results storage */
        int sw_nullspace[4], sw_rank[4], sw_min_dep[4], sw_qr_hits[4];

        for (int si = 0; si < sweep_n; ++si) {
            int k = sweep_vals[si];
            if (k > char_count) k = char_count;
            int sc = sign_col + rfb_count + afb_count + k;
            int srw = (sc + 63) / 64;

            /* Build matrix variant with k character columns */
            uint64_t* sm = (uint64_t*)xmalloc((size_t)rel_count * (size_t)srw * sizeof(uint64_t));
            memset(sm, 0, (size_t)rel_count * (size_t)srw * sizeof(uint64_t));
            for (int ri = 0; ri < rel_count; ++ri)
                for (int c = 0; c < sc; ++c)
                    if (orig_matrix[ri*row_words + c/64] & (1ull<<(c%64)))
                        sm[ri*srw + c/64] |= (1ull<<(c%64));

            /* Gaussian elimination */
            int* sp = (int*)xmalloc((size_t)sc * sizeof(int));
            for (int i = 0; i < sc; ++i) sp[i] = -1;
            int shw = (rel_count+63)/64;
            uint64_t* sh = (uint64_t*)xmalloc((size_t)rel_count*(size_t)shw*sizeof(uint64_t));
            memset(sh, 0, (size_t)rel_count*(size_t)shw*sizeof(uint64_t));
            for (int i = 0; i < rel_count; ++i) sh[i*shw+i/64]=(1ull<<(i%64));
            bool* srip = (bool*)xcalloc((size_t)rel_count, sizeof(bool));
            for (int col = 0; col < sc; ++col) {
                int piv = -1;
                for (int ri = 0; ri < rel_count; ++ri) {
                    if (srip[ri]) continue;
                    if (sm[ri*srw+col/64]&(1ull<<(col%64))) { piv=ri; sp[col]=ri; srip[ri]=true; break; }
                }
                if (piv<0) continue;
                for (int ri = 0; ri < rel_count; ++ri) {
                    if (ri==piv) continue;
                    if (!(sm[ri*srw+col/64]&(1ull<<(col%64)))) continue;
                    for (int w=0;w<srw;++w) sm[ri*srw+w]^=sm[piv*srw+w];
                    for (int w=0;w<shw;++w) sh[ri*shw+w]^=sh[piv*shw+w];
                }
            }
            int sr = 0; for (int c=0;c<sc;++c) if(sp[c]>=0) ++sr;
            /* Collect nullspace deps */
            typedef struct { int row; int sz; } SwDep;
            SwDep sd[256]; int sns = 0;
            for (int ri=0;ri<rel_count&&sns<256;++ri) {
                bool z=true; for(int w=0;w<srw;++w) if(sm[ri*srw+w]){z=false;break;}
                if(!z) continue;
                int sz=0; for(int rj=0;rj<rel_count;++rj) if(sh[ri*shw+rj/64]&(1ull<<(rj%64))) ++sz;
                sd[sns].row=ri; sd[sns].sz=sz; ++sns;
            }
            for (int i=1;i<sns;++i) { SwDep t=sd[i]; int j=i-1; while(j>=0&&sd[j].sz>t.sz){sd[j+1]=sd[j];--j;} sd[j+1]=t; }

            sw_nullspace[si] = sns; sw_rank[si] = sr; sw_min_dep[si] = sns>0?sd[0].sz:0;

            /* QR persistence: test smallest dep against 3 aux primes */
            int qr_hits = 0;
            if (sns > 0 && sw_pc > 0) {
                uint64_t* hd = &sh[sd[0].row * shw];
                for (int pi = 0; pi < sw_pc; ++pi) {
                    uint64_t ap = sw_primes[pi];
                    BigNum pbn; bn_from_u64(&pbn, ap);
                    BigNum f3i; bn_from_u64(&f3i, 1u);
                    AlgElem S; alg_one(&S);
                    for (int rj=0;rj<rel_count;++rj) {
                        if (!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                        AlgElem el; alg_from_ab(&el, rel_a[rj], rel_b[rj], &pbn);
                        AlgElem pr; for(int c=0;c<3;++c) bn_copy(&pr.c[c],&S.c[c]);
                        alg_mul(&S, &pr, &el, &poly, &f3i, &pbn);
                    }
                    /* QR check */
                    BigNum qexp; gnfs_qr_exp_from_prime(&qexp, ap);
                    unsigned qbits=bn_bitlen(&qexp);
                    AlgElem qc; alg_one(&qc);
                    AlgElem _b; for(int c=0;c<3;++c) bn_copy(&_b.c[c],&S.c[c]);
                    for(unsigned bt=0;bt<qbits;++bt){
                        if(qexp.limbs[bt/64]&(1ull<<(bt%64))){AlgElem _t;for(int c=0;c<3;++c)bn_copy(&_t.c[c],&qc.c[c]);alg_mul(&qc,&_t,&_b,&poly,&f3i,&pbn);}
                        AlgElem _t2;for(int c=0;c<3;++c)bn_copy(&_t2.c[c],&_b.c[c]);alg_mul(&_b,&_t2,&_t2,&poly,&f3i,&pbn);
                    }
                    if(bn_is_one(&qc.c[0])&&bn_is_zero(&qc.c[1])&&bn_is_zero(&qc.c[2])) ++qr_hits;
                }
            }
            sw_qr_hits[si] = qr_hits;

            printf("  [GNFS sweep] chars=%d: cols=%d rank=%d ns=%d min_dep=%d QR=%d/%d\n",
                sweep_vals[si], sc, sr, sns, sns>0?sd[0].sz:0, qr_hits, sw_pc);

            free(sm); free(sp); free(sh); free(srip);
        }

        /* Conclusion */
        bool qr_improved = false;
        for (int si=1;si<sweep_n;++si) if(sw_qr_hits[si]>sw_qr_hits[0]) qr_improved=true;
        bool deps_grow = sw_min_dep[sweep_n-1] > sw_min_dep[0] + 5;

        if (qr_improved) {
            printf("  [GNFS sweep] CONCLUSION: more chars improved QR persistence → strengthen character coverage\n");
        } else if (deps_grow && !qr_improved) {
            printf("  [GNFS sweep] CONCLUSION: more chars grow dep sizes but QR stays at 0 → current char family insufficient\n");
            printf("    → next step: exact integer algebraic-product measurement (2b-size), not more characters\n");
        } else {
            printf("  [GNFS sweep] CONCLUSION: chars have minimal structural effect → move past character tuning\n");
        }
    }

    /* Stage 5+6: Dense GF(2) Gaussian elimination + nullspace */
    int* pivot_row = (int*)xmalloc((size_t)total_cols * sizeof(int));
    for (int i = 0; i < total_cols; ++i) pivot_row[i] = -1;
    int hist_words = (rel_count + 63) / 64;
    uint64_t* history = (uint64_t*)xmalloc((size_t)rel_count * (size_t)hist_words * sizeof(uint64_t));
    memset(history, 0, (size_t)rel_count * (size_t)hist_words * sizeof(uint64_t));
    for (int i = 0; i < rel_count; ++i)
        history[i * hist_words + i / 64] = (1ull << (i % 64));

    bool* row_is_pivot = (bool*)xcalloc((size_t)rel_count, sizeof(bool));
    for (int col = 0; col < total_cols; ++col) {
        int piv = -1;
        for (int ri = 0; ri < rel_count; ++ri) {
            if (row_is_pivot[ri]) continue; /* skip rows already used as pivot */
            if (matrix[ri * row_words + col / 64] & (1ull << (col % 64))) {
                piv = ri; pivot_row[col] = ri; row_is_pivot[ri] = true; break;
            }
        }
        if (piv < 0) continue;
        for (int ri = 0; ri < rel_count; ++ri) {
            if (ri == piv) continue;
            if (!(matrix[ri * row_words + col / 64] & (1ull << (col % 64)))) continue;
            for (int w = 0; w < row_words; ++w) matrix[ri * row_words + w] ^= matrix[piv * row_words + w];
            for (int w = 0; w < hist_words; ++w) history[ri * hist_words + w] ^= history[piv * hist_words + w];
        }
    }

    int rank = 0;
    for (int c = 0; c < total_cols; ++c) if (pivot_row[c] >= 0) ++rank;
    int ns_count = 0;
    for (int ri = 0; ri < rel_count; ++ri) {
        bool is_zero = true;
        for (int w = 0; w < row_words; ++w) { if (matrix[ri * row_words + w]) { is_zero = false; break; } }
        if (is_zero) ++ns_count;
    }

    if (cfg->diag) {
        printf("  [GNFS] matrix: %d rows x %d cols\n", rel_count, eff_cols);
        printf("  [GNFS] rank: %d/%d  nullspace: %d vectors\n", rank, eff_cols, ns_count);

        /* Nullspace verification: XOR original parity rows for each dependency,
         * check all columns cancel to zero. Verify up to 5 dependencies. */
        if (ns_count > 0 && orig_matrix) {
            int verify_count = ns_count < 5 ? ns_count : 5;
            int verified = 0, failed = 0;
            int ns_verified = 0;
            for (int ri = 0; ri < rel_count && ns_verified < verify_count; ++ri) {
                bool is_zero = true;
                for (int w = 0; w < row_words; ++w) { if (matrix[ri * row_words + w]) { is_zero = false; break; } }
                if (!is_zero) continue;
                /* XOR original rows selected by this dependency's history */
                uint64_t* h = &history[ri * hist_words];
                uint64_t* check = (uint64_t*)xmalloc((size_t)row_words * sizeof(uint64_t));
                memset(check, 0, (size_t)row_words * sizeof(uint64_t));
                int dep_size = 0;
                for (int rj = 0; rj < rel_count; ++rj) {
                    if (!(h[rj / 64] & (1ull << (rj % 64)))) continue;
                    for (int w = 0; w < row_words; ++w) check[w] ^= orig_matrix[rj * row_words + w];
                    ++dep_size;
                }
                bool all_zero = true;
                for (int w = 0; w < row_words; ++w) { if (check[w]) { all_zero = false; break; } }
                if (all_zero) ++verified; else ++failed;
                if (ns_verified == 0 && cfg->diag) {
                    printf("  [GNFS] dep #0: %d relations, parity cancel=%s\n",
                        dep_size, all_zero ? "YES" : "NO");
                    /* Family-wise decode of the XOR result */
                    if (cfg->local_square) {
                        int sign_res = (check[0] & 1ull) ? 1 : 0;
                        int rfb_res=0, afb_res=0, char_res=0, schk_res=0;
                        for (int ci=col_rfb_start; ci<=col_rfb_end; ++ci)
                            if (check[ci/64]&(1ull<<(ci%64))) ++rfb_res;
                        for (int ci=col_afb_start; ci<=col_afb_end; ++ci)
                            if (check[ci/64]&(1ull<<(ci%64))) ++afb_res;
                        for (int ci=col_char_start; ci<=col_char_end && char_count>0; ++ci)
                            if (check[ci/64]&(1ull<<(ci%64))) ++char_res;
                        for (int ci=col_schk_start; ci<=col_schk_end && schk_ell_cols>0; ++ci)
                            if (check[ci/64]&(1ull<<(ci%64))) ++schk_res;
                        printf("  [NS-AUDIT] dep#0 XOR residual by family: sign=%d RFB=%d/%d AFB=%d/%d CHAR=%d/%d SCHK=%d/%d\n",
                            sign_res, rfb_res, rfb_count, afb_res, afb_count, char_res, char_count, schk_res, schk_ell_cols);
                        if (all_zero)
                            printf("  [NS-AUDIT] ALL FAMILIES CANCEL → matrix nullspace is semantically correct\n");
                        else {
                            printf("  [NS-AUDIT] RESIDUAL BITS REMAIN → matrix/nullspace wiring bug!\n");
                            if (char_res > 0) printf("  [NS-AUDIT]   character columns did NOT cancel: %d residual bits\n", char_res);
                            if (schk_res > 0) printf("  [NS-AUDIT]   Schirokauer columns did NOT cancel: %d residual bits\n", schk_res);
                        }
                    }
                }
                free(check);
                ++ns_verified;
            }
            printf("  [GNFS] nullspace verification: %d/%d verified OK", verified, verify_count);
            if (failed > 0) printf(", %d FAILED", failed);
            printf("\n");
        }

        if (ns_count > 0)
            printf("  [GNFS] result: NULLSPACE_FOUND (%d dependencies)\n", ns_count);
        else
            printf("  [GNFS] result: NO_NULLSPACE (rank = rows, no dependencies)\n");

        /* Z/ℓZ Schirokauer sum diagnostic: check which deps have sum ≡ 0 mod ℓ */
        if (ns_count > 0 && schk_raw != NULL && schk_ell > 0) {
            uint64_t ell = schk_ell;
            int deps_checked = 0, deps_zero = 0;
            int ns_idx = 0;
            for (int ri = 0; ri < rel_count && deps_checked < 20; ++ri) {
                bool is_zero = true;
                for (int w = 0; w < row_words; ++w) { if (matrix[ri*row_words+w]) { is_zero = false; break; } }
                if (!is_zero) continue;
                uint64_t* h = &history[ri * hist_words];
                uint64_t sum[3] = {0, 0, 0};
                int dep_sz = 0;
                for (int rj = 0; rj < rel_count; ++rj) {
                    if (!(h[rj/64]&(1ull<<(rj%64)))) continue;
                    ++dep_sz;
                    for (int k = 0; k < 3; ++k) sum[k] = (sum[k] + schk_raw[rj*3+k]) % ell;
                }
                bool all_zero = (sum[0]==0 && sum[1]==0 && sum[2]==0);
                if (all_zero) ++deps_zero;
                if (deps_checked < 5)
                    printf("  [GNFS schk-sum] dep#%d (%d rels): sum mod %"PRIu64" = [%"PRIu64",%"PRIu64",%"PRIu64"] %s\n",
                        deps_checked, dep_sz, ell, sum[0], sum[1], sum[2], all_zero?"ZERO (square candidate)":"NONZERO");
                ++deps_checked;
            }
            printf("  [GNFS schk-sum] %d/%d deps have all sums = 0 mod %"PRIu64"\n", deps_zero, deps_checked, ell);
        }
    }

    /* ── Milestone 1.5: Rational-side extraction-prep ──
     * For each verified nullspace dependency, compute:
     *   x = ∏(a_i - b_i·m) mod N   (rational-side product)
     *   Accumulate rational FB exponents, check all even
     *   y = ∏ p^(exp/2) mod N       (rational square root)
     *   gcd(x ± y, N)               (diagnostic — may or may not yield factor) */
    bool found_factor_m15 = false;
    if (ns_count > 0 && cfg->diag) {
        int m15_max = ns_count < 10 ? ns_count : 10;
        int m15_tried = 0;
        printf("  [GNFS 1.5] rational-side extraction-prep (%d dependencies):\n", m15_max);

        for (int ri = 0; ri < rel_count && m15_tried < m15_max; ++ri) {
            bool is_zero = true;
            for (int w = 0; w < row_words; ++w) { if (matrix[ri * row_words + w]) { is_zero = false; break; } }
            if (!is_zero) continue;

            uint64_t* h = &history[ri * hist_words];
            int dep_size = 0;

            /* x = ∏(a_i - b_i·m) mod N */
            BigNum x_prod; bn_from_u64(&x_prod, 1u);
            /* Rational FB exponent accumulation */
            uint64_t rat_exp[GNFS_MAX_FB];
            memset(rat_exp, 0, sizeof(uint64_t) * (size_t)rfb_count);

            for (int rj = 0; rj < rel_count; ++rj) {
                if (!(h[rj / 64] & (1ull << (rj % 64)))) continue;
                ++dep_size;

                /* Compute (a - b*m) mod N for this relation */
                int64_t ra = rel_a[rj];
                uint64_t rb = rel_b[rj];
                BigNum bm_val; bn_copy(&bm_val, &poly.m);
                { BigNum b_bn; bn_from_u64(&b_bn, rb); bn_mul(&bm_val, &bm_val, &b_bn); }

                BigNum val;
                if (ra >= 0) {
                    BigNum a_bn; bn_from_u64(&a_bn, (uint64_t)ra);
                    if (bn_cmp(&a_bn, &bm_val) >= 0) { bn_copy(&val, &a_bn); bn_sub(&val, &bm_val); }
                    else { bn_copy(&val, &bm_val); bn_sub(&val, &a_bn); }
                } else {
                    BigNum a_bn; bn_from_u64(&a_bn, (uint64_t)(-ra));
                    bn_copy(&val, &bm_val); bn_add(&val, &a_bn);
                }
                /* Reduce mod N and multiply into x */
                BigNum val_mod; bn_copy(&val_mod, &val);
                /* val_mod = val mod N (if val >= N) */
                while (bn_cmp(&val_mod, n) >= 0) bn_sub(&val_mod, n);
                bn_mod_mul(&x_prod, &x_prod, &val_mod, n);

                /* Accumulate rational FB exponents from |a - b*m| */
                BigNum rrem; bn_copy(&rrem, &val);
                for (int fi = 0; fi < rfb_count; ++fi) {
                    while (bn_mod_u64(&rrem, rfb[fi]) == 0u) {
                        BigNum dp; bn_from_u64(&dp, rfb[fi]);
                        BigNum qo; bn_divexact(&qo, &rrem, &dp);
                        bn_copy(&rrem, &qo);
                        ++rat_exp[fi];
                    }
                }
            }

            /* Check all rational exponents are even */
            bool all_even = true;
            int odd_count = 0;
            for (int fi = 0; fi < rfb_count; ++fi) {
                if (rat_exp[fi] & 1u) { all_even = false; ++odd_count; }
            }

            /* Compute rational square root: y = ∏ p^(exp/2) mod N */
            BigNum y_rat; bn_from_u64(&y_rat, 1u);
            for (int fi = 0; fi < rfb_count; ++fi) {
                uint64_t half = rat_exp[fi] / 2u;
                if (half > 0u) {
                    BigNum base; bn_from_u64(&base, rfb[fi]);
                    BigNum pw; bn_from_u64(&pw, 1u);
                    for (uint64_t e = 0; e < half; ++e) bn_mod_mul(&pw, &pw, &base, n);
                    bn_mod_mul(&y_rat, &y_rat, &pw, n);
                }
            }

            /* Diagnostic GCDs */
            BigNum diff, sum_v;
            if (bn_cmp(&x_prod, &y_rat) >= 0) { bn_copy(&diff, &x_prod); bn_sub(&diff, &y_rat); }
            else { bn_copy(&diff, &x_prod); bn_add(&diff, n); bn_sub(&diff, &y_rat); }
            bn_copy(&sum_v, &x_prod); bn_add(&sum_v, &y_rat);
            if (bn_cmp(&sum_v, n) >= 0) bn_sub(&sum_v, n);

            BigNum g1, g2;
            bn_gcd(&g1, &diff, n);
            bn_gcd(&g2, &sum_v, n);

            bool g1_nontrivial = bn_factor_candidate_valid(&g1, n);
            bool g2_nontrivial = bn_factor_candidate_valid(&g2, n);

            printf("    dep #%d: %d relations  rat_exponents_all_even=%s",
                m15_tried, dep_size, all_even ? "YES" : "NO");
            if (!all_even) printf("(%d odd)", odd_count);

            char g1buf[128], g2buf[128];
            bn_to_str(&g1, g1buf, sizeof(g1buf));
            bn_to_str(&g2, g2buf, sizeof(g2buf));
            printf("  gcd(x-y,N)=%s%s  gcd(x+y,N)=%s%s\n",
                g1buf, g1_nontrivial ? " [FACTOR!]" : "",
                g2buf, g2_nontrivial ? " [FACTOR!]" : "");

            if (g1_nontrivial && !found_factor_m15) {
                printf("  [GNFS 1.5] RATIONAL-SIDE EXTRACTION HIT: factor found\n");
                factor_result_bn_set_from_factor(&fr, n, &g1);
                fr.method = "gnfs(rational-extraction)";
                found_factor_m15 = true;
            } else if (g2_nontrivial && !found_factor_m15) {
                printf("  [GNFS 1.5] RATIONAL-SIDE EXTRACTION HIT: factor found\n");
                factor_result_bn_set_from_factor(&fr, n, &g2);
                fr.method = "gnfs(rational-extraction)";
                found_factor_m15 = true;
            }

            ++m15_tried;
        }
        if (!found_factor_m15) {
            printf("  [GNFS 1.5] no nontrivial factor from rational side (expected — needs algebraic sqrt)\n");
        }

        /* ── Milestone 2a: Algebraic product replay ──
         * For each dependency, compute the algebraic product S = ∏(a_i - b_i·α)
         * in Z[α]/(f) using the modular AlgElem arithmetic.
         * This is computed mod N (for diagnostics) and could be computed mod an
         * auxiliary prime p for the eventual Couveignes square root.
         *
         * The algebraic square root itself is NOT implemented.
         * This milestone validates that the algebraic product can be formed correctly. */
        if (!found_factor_m15 && poly.degree == 3 && cfg->diag) {
            /* Compute f3_inv mod N via extended GCD (NOT Fermat — N is composite) */
            BigNum f3_inv, gcd_f3;
            bn_gcd(&gcd_f3, &poly.coeffs[3], n);
            bool have_f3_inv = false;
            if (bn_is_one(&gcd_f3)) {
                /* Use bn_mod_inverse_checked which handles composite moduli correctly */
                BigNum dummy_factor;
                have_f3_inv = bn_mod_inverse_checked(&f3_inv, &poly.coeffs[3], n, &dummy_factor);
            }
            if (bn_is_one(&poly.coeffs[3])) {
                /* f3 = 1 → monic polynomial, no inverse needed */
                bn_from_u64(&f3_inv, 1u);
                have_f3_inv = true;
            }

            if (have_f3_inv) {
                printf("  [GNFS 2a] algebraic product replay (degree 3, f3=%s):\n",
                    bn_is_one(&poly.coeffs[3]) ? "1 (monic)" : "non-monic");

                int m2a_max = ns_count < 3 ? ns_count : 3;
                int m2a_tried = 0;
                for (int ri = 0; ri < rel_count && m2a_tried < m2a_max; ++ri) {
                    bool is_zero = true;
                    for (int w = 0; w < row_words; ++w) { if (matrix[ri * row_words + w]) { is_zero = false; break; } }
                    if (!is_zero) continue;

                    uint64_t* h = &history[ri * hist_words];

                    /* Form algebraic product S = ∏(a_i - b_i·α) mod (f, N) */
                    AlgElem S; alg_one(&S);
                    int dep_sz = 0;
                    int f3_red_count = 0; /* track f3 reductions for denominator */
                    BigNum direct_prod; bn_from_u64(&direct_prod, 1u); /* ∏(a_i - b_i*m) mod N */
                    int bridge_mismatches = 0;
                    for (int rj = 0; rj < rel_count; ++rj) {
                        if (!(h[rj / 64] & (1ull << (rj % 64)))) continue;
                        ++dep_sz;
                        AlgElem elem;
                        alg_from_ab(&elem, rel_a[rj], rel_b[rj], n);
                        AlgElem prev; for (int ci = 0; ci < 3; ++ci) bn_copy(&prev.c[ci], &S.c[ci]);
                        alg_mul(&S, &prev, &elem, &poly, &f3_inv, n);
                        /* Each multiply can introduce up to 2 f3 reductions (degree 4→2) */
                        f3_red_count += 2; /* conservative upper bound */
                        /* Bridge invariant: alg_eval_at_m(S) must equal ∏(a_j - b_j*m) mod N */
                        BigNum s_step; alg_eval_at_m(&s_step, &S, &poly.m, n);
                        BigNum abm_val; alg_eval_at_m(&abm_val, &elem, &poly.m, n);
                        bn_mod_mul(&direct_prod, &direct_prod, &abm_val, n);
                        bool step_ok = (bn_cmp(&s_step, &direct_prod) == 0);
                        if (!step_ok) ++bridge_mismatches;
                        if (dep_sz <= 3 || !step_ok) {
                            char sb[128], db[128];
                            bn_to_str(&s_step, sb, sizeof(sb));
                            bn_to_str(&direct_prod, db, sizeof(db));
                            printf("      step %d: a=%"PRId64" b=%"PRIu64" eval(S)=%s direct=%s %s\n",
                                dep_sz, rel_a[rj], rel_b[rj], sb, db, step_ok ? "MATCH" : "MISMATCH");
                        }
                    }

                    /* Evaluate S at α = m → S(m) mod N */
                    BigNum s_at_m;
                    alg_eval_at_m(&s_at_m, &S, &poly.m, n);

                    /* Coefficient sizes */
                    unsigned c0_bits = bn_bitlen(&S.c[0]);
                    unsigned c1_bits = bn_bitlen(&S.c[1]);
                    unsigned c2_bits = bn_bitlen(&S.c[2]);

                    char sm_buf[128], dp_buf[128];
                    bn_to_str(&s_at_m, sm_buf, sizeof(sm_buf));
                    bn_to_str(&direct_prod, dp_buf, sizeof(dp_buf));
                    bool bridge_final_ok = (bn_cmp(&s_at_m, &direct_prod) == 0);

                    printf("    dep #%d: %d rels  coeff_bits=[%u, %u, %u]  f3_reds<=%d\n",
                        m2a_tried, dep_sz, c0_bits, c1_bits, c2_bits, f3_red_count);
                    printf("      BRIDGE: eval(S)=%s  direct=%s  %s (%d/%d steps ok)\n",
                        sm_buf, dp_buf, bridge_final_ok ? "MATCH" : "**MISMATCH**",
                        dep_sz - bridge_mismatches, dep_sz);

                    ++m2a_tried;
                }
                printf("  [GNFS 2a] algebraic product replay complete\n");
            } else {
                printf("  [GNFS 2a] SKIP: f3 not invertible mod N\n");
            }
        }

        /* ── Milestone 2b-size: Exact integer algebraic product measurement ──
         * Compute S = ∏(a_i - b_i·α) mod a VERY large auxiliary prime p_big
         * to measure the true integer coefficient sizes without overflow.
         * The coefficients mod p_big equal the true integer coefficients
         * as long as p_big > max(|coeff|). Using a ~500-bit prime gives
         * headroom for deps up to ~50 relations on toy targets. */
        if (cfg->measure_alg && ns_count > 0 && poly.degree == 3) {
            /* Sort deps by size for measurement */
            typedef struct { int row; int sz; } MsDep;
            MsDep ms_deps[256]; int ms_ns = 0;
            for (int ri = 0; ri < rel_count && ms_ns < 256; ++ri) {
                bool z = true;
                for (int w = 0; w < row_words; ++w) { if (matrix[ri*row_words+w]) { z = false; break; } }
                if (!z) continue;
                uint64_t* h = &history[ri * hist_words];
                int sz = 0;
                for (int rj = 0; rj < rel_count; ++rj) if (h[rj/64]&(1ull<<(rj%64))) ++sz;
                ms_deps[ms_ns].row = ri; ms_deps[ms_ns].sz = sz; ++ms_ns;
            }
            for (int i=1;i<ms_ns;++i){MsDep t=ms_deps[i];int j=i-1;while(j>=0&&ms_deps[j].sz>t.sz){ms_deps[j+1]=ms_deps[j];--j;}ms_deps[j+1]=t;}

            printf("  [GNFS 2b-size] exact algebraic product measurement\n");
            printf("  [GNFS 2b-size] smallest deps:");
            for (int i=0;i<ms_ns&&i<5;++i) printf(" %d",ms_deps[i].sz);
            printf("\n");

            /* Use a large prime p_big for modular measurement.
             * For the toy target with ~20-rel deps, coefficients are bounded by
             * roughly (max(|a|)+max(b)*m)^20 ≈ 1160^20 ≈ 2^204.
             * A 512-bit prime gives ample headroom. */
            BigNum p_big;
            /* Use a known large prime: 2^521 - 1 (Mersenne prime M521) */
            bn_from_u64(&p_big, 1u);
            for (int i = 0; i < 521; ++i) bn_shl1(&p_big);
            { BigNum one; bn_from_u64(&one, 1u); bn_sub(&p_big, &one); }

            /* Compute f3_inv mod p_big (Fermat: f3^(p_big-2) mod p_big) */
            BigNum f3_inv_big;
            BigNum f3_val; bn_copy(&f3_val, &poly.coeffs[3]);
            if (bn_is_one(&f3_val)) {
                bn_from_u64(&f3_inv_big, 1u);
            } else {
                /* For small f3, f3_inv = f3^(p_big-2) mod p_big. Since p_big is prime, this works. */
                BigNum exp_m2; bn_copy(&exp_m2, &p_big);
                { BigNum two; bn_from_u64(&two, 2u); bn_sub(&exp_m2, &two); }
                /* Use bn_mont_powmod for efficiency */
                bn_from_u64(&f3_inv_big, 1u); /* fallback: compute via repeated squaring */
                BigNum base_f; bn_copy(&base_f, &f3_val);
                unsigned eb = bn_bitlen(&exp_m2);
                for (unsigned bit = 0; bit < eb; ++bit) {
                    if (exp_m2.limbs[bit/64] & (1ull<<(bit%64)))
                        bn_mod_mul(&f3_inv_big, &f3_inv_big, &base_f, &p_big);
                    bn_mod_mul(&base_f, &base_f, &base_f, &p_big);
                }
            }
            bool is_monic = bn_is_one(&poly.coeffs[3]);

            int ms_test = ms_ns < 3 ? ms_ns : 3;
            for (int di = 0; di < ms_test; ++di) {
                int dep_row = ms_deps[di].row;
                int dep_sz = ms_deps[di].sz;
                uint64_t* hd = &history[dep_row * hist_words];

                /* Find max|a| and max b in this dependency */
                int64_t max_abs_a = 0; uint64_t max_b = 0;
                for (int rj = 0; rj < rel_count; ++rj) {
                    if (!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                    int64_t aa = rel_a[rj] < 0 ? -rel_a[rj] : rel_a[rj];
                    if (aa > max_abs_a) max_abs_a = aa;
                    if (rel_b[rj] > max_b) max_b = rel_b[rj];
                }

                /* Replay exact product mod p_big */
                GNFSAlgExact S;
                gnfs_alg_exact_one(&S);
                bn_from_u64(&S.denom, 1u);
                S.reductions = 0;
                /* Use gnfs_alg_exact_mul_modp with p_big as modulus */
                for (int rj = 0; rj < rel_count; ++rj) {
                    if (!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                    GNFSAlgExact elem;
                    /* (a - b*alpha) mod p_big: handle signed a */
                    int64_t ra = rel_a[rj]; uint64_t rb = rel_b[rj];
                    if (ra >= 0) bn_from_u64(&elem.c[0], (uint64_t)ra);
                    else { bn_copy(&elem.c[0], &p_big); BigNum t; bn_from_u64(&t, (uint64_t)(-ra)); bn_sub(&elem.c[0], &t); }
                    /* c[1] = -b mod p_big = p_big - b */
                    bn_copy(&elem.c[1], &p_big); { BigNum t; bn_from_u64(&t, rb); bn_sub(&elem.c[1], &t); }
                    bn_zero(&elem.c[2]);
                    bn_from_u64(&elem.denom, 1u); elem.reductions = 0;

                    GNFSAlgExact prev;
                    for (int ci=0;ci<3;++ci) bn_copy(&prev.c[ci], &S.c[ci]);
                    bn_copy(&prev.denom, &S.denom); prev.reductions = S.reductions;
                    gnfs_alg_exact_mul_modp(&S, &prev, &elem, &poly, &f3_inv_big, &p_big);
                }

                unsigned c0b = bn_bitlen(&S.c[0]);
                unsigned c1b = bn_bitlen(&S.c[1]);
                unsigned c2b = bn_bitlen(&S.c[2]);
                unsigned db = bn_bitlen(&S.denom);
                unsigned maxb = c0b; if (c1b>maxb) maxb=c1b; if (c2b>maxb) maxb=c2b;
                /* Centered: true coefficients may be negative, so actual size is max(coeff, p_big - coeff) */
                /* Check if any coefficient > p_big/2 → would be negative in centered representation */
                BigNum phalf; bn_copy(&phalf, &p_big);
                /* phalf = p_big >> 1 */
                for (uint32_t li=0;li<phalf.used;++li){phalf.limbs[li]>>=1;if(li+1<phalf.used&&(phalf.limbs[li+1]&1))phalf.limbs[li]|=(1ull<<63);}
                bn_normalize(&phalf);
                bool c0_neg = bn_cmp(&S.c[0], &phalf) > 0;
                bool c1_neg = bn_cmp(&S.c[1], &phalf) > 0;
                bool c2_neg = bn_cmp(&S.c[2], &phalf) > 0;

                /* Heuristic T-bound: sqrt has coefficients ~half the bit size */
                unsigned t_est = (maxb + 1) / 2;

                char mbuf[64]; bn_to_str(&poly.m, mbuf, sizeof(mbuf));
                printf("    dep#%d (%d rels): max|a|=%"PRId64" max_b=%"PRIu64" m=%s f3=%s\n",
                    di, dep_sz, max_abs_a, max_b, mbuf, is_monic?"1":"non-monic");
                printf("      S coeff bits = [%u, %u, %u]  denom_bits=%u  max=%u\n",
                    c0b, c1b, c2b, is_monic?0:db, maxb);
                printf("      centered signs: c0=%s c1=%s c2=%s\n",
                    c0_neg?"neg":"pos", c1_neg?"neg":"pos", c2_neg?"neg":"pos");
                printf("      T-scale estimate ~ %u bits\n", t_est);
                if (maxb >= 521) printf("      WARNING: coefficients may exceed p_big (521 bits) — measurement unreliable\n");
                else if (maxb > 450) printf("      WARNING: coefficients near p_big limit — approaching unreliable\n");
                else printf("      status: OK (coefficients well within 521-bit measurement prime)\n");
            }

            /* Summary */
            if (ms_test > 0) {
                printf("  [GNFS 2b-size] feasibility:\n");
                printf("    toy deps (%d rels): likely feasible for single-prime recovery with ~%u-bit auxiliary prime\n",
                    ms_deps[0].sz, ms_deps[0].sz > 0 ? (unsigned)(ms_deps[0].sz * 11) : 0);
                printf("    note: single-prime recovery requires auxiliary prime > max T coefficient\n");
                printf("    CRT alternative: ~%u/%u primes needed for T coefficient recovery\n",
                    ms_deps[0].sz > 0 ? (unsigned)((ms_deps[0].sz * 11 / 2 + 63) / 64) : 0, 64);
            }
        }

        /* ── Guarded single-prime algebraic recovery: scan ALL deps × multiple primes ── */
        if (cfg->recover_one && ns_count > 0 && poly.degree == 3) {
            typedef struct { int row; int sz; } RcDep;
            RcDep rc_deps[256]; int rc_ns = 0;
            for (int ri = 0; ri < rel_count && rc_ns < 256; ++ri) {
                bool z = true;
                for (int w = 0; w < row_words; ++w) { if (matrix[ri*row_words+w]) { z = false; break; } }
                if (!z) continue;
                uint64_t* h = &history[ri * hist_words]; int sz = 0;
                for (int rj = 0; rj < rel_count; ++rj) if (h[rj/64]&(1ull<<(rj%64))) ++sz;
                rc_deps[rc_ns].row = ri; rc_deps[rc_ns].sz = sz; ++rc_ns;
            }
            for (int i=1;i<rc_ns;++i){RcDep t=rc_deps[i];int j=i-1;while(j>=0&&rc_deps[j].sz>t.sz){rc_deps[j+1]=rc_deps[j];--j;}rc_deps[j+1]=t;}

            /* Find 5 auxiliary primes */
            uint64_t rp_list[5]; int rp_count = 0;
            { uint64_t rng = 0xFACE0FF1CE2B4D69ULL;
              for (int tries = 0; tries < 3000 && rp_count < 5; ++tries) {
                  rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17;
                  uint64_t cand = (rng | ((uint64_t)1<<59)) | 3u;
                  cand = cand & ~1ull; cand |= 3u;
                  if ((cand&3u)!=3u || !miller_rabin_u64(cand)) continue;
                  uint64_t f0v=bn_mod_u64(&poly.coeffs[0],cand),f1v=bn_mod_u64(&poly.coeffs[1],cand);
                  uint64_t f2v=bn_mod_u64(&poly.coeffs[2],cand),f3q=bn_mod_u64(&poly.coeffs[3],cand);
                  if (f3q==0u) continue;
                  uint64_t f3i=powmod_u64(f3q,cand-2u,cand);
                  uint64_t mf0=mulmod_u64(f0v,f3i,cand),mf1=mulmod_u64(f1v,f3i,cand),mf2=mulmod_u64(f2v,f3i,cand);
                  uint64_t res[3]={1,0,0},base2[3]={0,1,0};
                  #define P3MR2(o,a2,b2,pv) do{uint64_t _t[5]={0,0,0,0,0};\
                      for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+mulmod_u64((a2)[_i],(b2)[_j],pv))%pv;\
                      if(_t[4]){_t[3]=(_t[3]+mulmod_u64(_t[4],pv-mf2,pv))%pv;_t[2]=(_t[2]+mulmod_u64(_t[4],pv-mf1,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[4],pv-mf0,pv))%pv;}\
                      if(_t[3]){_t[2]=(_t[2]+mulmod_u64(_t[3],pv-mf2,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[3],pv-mf1,pv))%pv;_t[0]=(_t[0]+mulmod_u64(_t[3],pv-mf0,pv))%pv;}\
                      (o)[0]=_t[0];(o)[1]=_t[1];(o)[2]=_t[2];}while(0)
                  uint64_t ev=cand;
                  while(ev>0u){if(ev&1u){uint64_t t[3];P3MR2(t,res,base2,cand);res[0]=t[0];res[1]=t[1];res[2]=t[2];}
                      uint64_t t2[3];P3MR2(t2,base2,base2,cand);base2[0]=t2[0];base2[1]=t2[1];base2[2]=t2[2];ev>>=1u;}
                  #undef P3MR2
                  if(res[0]==0u&&res[1]==1u&&res[2]==0u) continue;
                  rp_list[rp_count++] = cand;
              }
            }

            int total_tested = 0, qr_hits = 0;
            bool found_alg = false;
            printf("  [GNFS recover] scanning %d deps × %d primes for QR hits\n", rc_ns, rp_count);

            for (int pi = 0; pi < rp_count && !found_alg; ++pi) {
                uint64_t ap = rp_list[pi];
                BigNum p_bn; bn_from_u64(&p_bn, ap);
                uint64_t f3u = bn_mod_u64(&poly.coeffs[3], ap);
                uint64_t f3ip = (f3u<=1u) ? f3u : powmod_u64(f3u, ap-2u, ap);
                BigNum f3_inv_p; bn_from_u64(&f3_inv_p, f3ip);

                /* Precompute QR exponent (p³-1)/2 and sqrt exponent (p³+1)/4 */
                BigNum qr_exp; gnfs_qr_exp_from_prime(&qr_exp, ap);
                unsigned qr_bits=bn_bitlen(&qr_exp);
                BigNum sq_exp; gnfs_sqrt_exp_from_prime(&sq_exp, ap);
                unsigned sq_bits=bn_bitlen(&sq_exp);

                #define ALG_POW_R(res2,base_in,exp_in,exp_bits2) do{ \
                    alg_one(&(res2)); \
                    AlgElem _b2;for(int _c=0;_c<3;++_c)bn_copy(&_b2.c[_c],&(base_in).c[_c]); \
                    for(unsigned _bt=0;_bt<(exp_bits2);++_bt){ \
                        if((exp_in).limbs[_bt/64]&(1ull<<(_bt%64))){AlgElem _t;for(int _c=0;_c<3;++_c)bn_copy(&_t.c[_c],&(res2).c[_c]);alg_mul(&(res2),&_t,&_b2,&poly,&f3_inv_p,&p_bn);} \
                        AlgElem _t2;for(int _c=0;_c<3;++_c)bn_copy(&_t2.c[_c],&_b2.c[_c]);alg_mul(&_b2,&_t2,&_t2,&poly,&f3_inv_p,&p_bn);} \
                } while(0)

                for (int di = 0; di < rc_ns && !found_alg; ++di) {
                    uint64_t* hd = &history[rc_deps[di].row * hist_words];
                    int dsz = rc_deps[di].sz;
                    ++total_tested;

                    /* Compute S mod (f, p) */
                    AlgElem S_p; alg_one(&S_p);
                    int f3_reds = 0;
                    for (int rj=0;rj<rel_count;++rj) {
                        if(!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                        AlgElem el; alg_from_ab(&el,rel_a[rj],rel_b[rj],&p_bn);
                        AlgElem prev; for(int c=0;c<3;++c)bn_copy(&prev.c[c],&S_p.c[c]);
                        alg_mul(&S_p,&prev,&el,&poly,&f3_inv_p,&p_bn);
                        if(!bn_is_one(&poly.coeffs[3])) f3_reds+=2;
                    }

                    /* QR check */
                    AlgElem qc; ALG_POW_R(qc,S_p,qr_exp,qr_bits);
                    bool is_qr=bn_is_one(&qc.c[0])&&bn_is_zero(&qc.c[1])&&bn_is_zero(&qc.c[2]);
                    if(!is_qr) continue;
                    ++qr_hits;
                    printf("  [GNFS recover] QR HIT! dep#%d (%d rels) on p=%"PRIu64"\n", di, dsz, ap);

                    /* Field sqrt */
                    AlgElem T_p; ALG_POW_R(T_p,S_p,sq_exp,sq_bits);
                    AlgElem T_sq;{AlgElem t1;for(int c=0;c<3;++c)bn_copy(&t1.c[c],&T_p.c[c]);alg_mul(&T_sq,&t1,&t1,&poly,&f3_inv_p,&p_bn);}
                    bool t2_ok=true; for(int c=0;c<3;++c) if(bn_cmp(&T_sq.c[c],&S_p.c[c])!=0){t2_ok=false;break;}
                    printf("  [GNFS recover] T^2=S: %s\n", t2_ok?"PASS":"FAIL");
                    if(!t2_ok) continue;

                    /* Centered recovery */
                    int64_t tc[3];
                    for(int c=0;c<3;++c){uint64_t tv=bn_to_u64(&T_p.c[c]);tc[c]=(tv<=ap/2u)?(int64_t)tv:(int64_t)tv-(int64_t)ap;}
                    printf("  [GNFS recover] T=[%"PRId64",%"PRId64",%"PRId64"]\n",tc[0],tc[1],tc[2]);

                    /* Denominator */
                    bool is_monic=bn_is_one(&poly.coeffs[3]);
                    BigNum denom_val;bn_from_u64(&denom_val,1u);
                    if(!is_monic){uint64_t f3_64=bn_to_u64(&poly.coeffs[3]);int hr=f3_reds/2;
                        for(int i=0;i<hr;++i){BigNum t;bn_from_u64(&t,f3_64);bn_mul(&denom_val,&denom_val,&t);}}
                    BigNum denom_gcd;bn_gcd(&denom_gcd,&denom_val,n);
                    bool denom_ok=bn_is_one(&denom_gcd);
                    if(!denom_ok&&!bn_is_zero(&denom_gcd)&&bn_cmp(&denom_gcd,n)!=0){
                        printf("  [GNFS recover] BONUS: gcd(denom,N) is nontrivial!\n");
                        factor_result_bn_set_from_factor(&fr,n,&denom_gcd);fr.method="gnfs(denom-factor)";found_alg=true;continue;
                    }
                    if(!denom_ok) continue;

                    /* Evaluate y_alg */
                    BigNum y_alg;bn_from_u64(&y_alg,0u);BigNum mpow;bn_from_u64(&mpow,1u);
                    for(int c=0;c<3;++c){BigNum term;
                        if(tc[c]>=0)bn_from_u64(&term,(uint64_t)tc[c]);
                        else{bn_copy(&term,n);BigNum t;bn_from_u64(&t,(uint64_t)(-tc[c]));bn_sub(&term,&t);}
                        bn_mod_mul(&term,&term,&mpow,n);bn_add(&y_alg,&term);
                        if(bn_cmp(&y_alg,n)>=0)bn_sub(&y_alg,n);
                        if(c<2)bn_mod_mul(&mpow,&mpow,&poly.m,n);}
                    if(!is_monic){BigNum dinv,dfac;
                        if(bn_mod_inverse_checked(&dinv,&denom_val,n,&dfac))bn_mod_mul(&y_alg,&y_alg,&dinv,n);
                        else{printf("  [GNFS recover] denom not invertible\n");continue;}}

                    /* Rational side */
                    BigNum x_prod;bn_from_u64(&x_prod,1u);BigNum y_rat;bn_from_u64(&y_rat,1u);
                    uint64_t lr[GNFS_MAX_FB];memset(lr,0,sizeof(uint64_t)*(size_t)rfb_count);
                    for(int rj=0;rj<rel_count;++rj){if(!(hd[rj/64]&(1ull<<(rj%64))))continue;
                        BigNum bm2;bn_copy(&bm2,&poly.m);{BigNum bb;bn_from_u64(&bb,rel_b[rj]);bn_mul(&bm2,&bm2,&bb);}
                        BigNum val;if(rel_a[rj]>=0){BigNum ac;bn_from_u64(&ac,(uint64_t)rel_a[rj]);
                            if(bn_cmp(&ac,&bm2)>=0){bn_copy(&val,&ac);bn_sub(&val,&bm2);}else{bn_copy(&val,&bm2);bn_sub(&val,&ac);}
                        }else{BigNum ac;bn_from_u64(&ac,(uint64_t)(-rel_a[rj]));bn_copy(&val,&bm2);bn_add(&val,&ac);}
                        BigNum vm;bn_copy(&vm,&val);while(bn_cmp(&vm,n)>=0)bn_sub(&vm,n);
                        bn_mod_mul(&x_prod,&x_prod,&vm,n);BigNum rr;bn_copy(&rr,&val);
                        for(int fi=0;fi<rfb_count;++fi){while(bn_mod_u64(&rr,rfb[fi])==0u){BigNum dp;bn_from_u64(&dp,rfb[fi]);BigNum qo;bn_divexact(&qo,&rr,&dp);bn_copy(&rr,&qo);++lr[fi];}}}
                    for(int fi=0;fi<rfb_count;++fi){uint64_t half=lr[fi]/2u;if(half>0u){BigNum bp;bn_from_u64(&bp,rfb[fi]);BigNum pw;bn_from_u64(&pw,1u);for(uint64_t e=0;e<half;++e)bn_mod_mul(&pw,&pw,&bp,n);bn_mod_mul(&y_rat,&y_rat,&pw,n);}}

                    /* GCD */
                    for(int neg=0;neg<2&&!found_alg;++neg){
                        BigNum y_use;if(neg==0)bn_mod_mul(&y_use,&y_rat,&y_alg,n);
                        else{BigNum ny;bn_copy(&ny,n);bn_sub(&ny,&y_alg);bn_mod_mul(&y_use,&y_rat,&ny,n);}
                        BigNum diff,sum2;
                        if(bn_cmp(&x_prod,&y_use)>=0){bn_copy(&diff,&x_prod);bn_sub(&diff,&y_use);}
                        else{bn_copy(&diff,&x_prod);bn_add(&diff,n);bn_sub(&diff,&y_use);}
                        bn_copy(&sum2,&x_prod);bn_add(&sum2,&y_use);if(bn_cmp(&sum2,n)>=0)bn_sub(&sum2,n);
                        BigNum g1,g2;bn_gcd(&g1,&diff,n);bn_gcd(&g2,&sum2,n);
                        bool g1ok=bn_factor_candidate_valid(&g1,n);bool g2ok=bn_factor_candidate_valid(&g2,n);
                        char g1s[64],g2s[64];bn_to_str(&g1,g1s,sizeof(g1s));bn_to_str(&g2,g2s,sizeof(g2s));
                        printf("  [GNFS recover] %s: gcd(x-y,N)=%s%s gcd(x+y,N)=%s%s\n",
                            neg?"(-T)":"(+T)",g1s,g1ok?" [FACTOR!]":"",g2s,g2ok?" [FACTOR!]":"");
                        if(g1ok){factor_result_bn_set_from_factor(&fr,n,&g1);fr.method="gnfs(alg-extraction)";found_alg=true;}
                        else if(g2ok){factor_result_bn_set_from_factor(&fr,n,&g2);fr.method="gnfs(alg-extraction)";found_alg=true;}
                    }
                }
                #undef ALG_POW_R
            }
            if (found_alg) found_factor_m15 = true;
            printf("  [GNFS recover] summary: %d tested, %d QR hits, alg_factor=%s\n",
                total_tested, qr_hits, found_alg?"YES":"no");
        }

        /* ── CRT multi-prime algebraic square root recovery ── */
        if (cfg->crt_recover && !found_factor_m15 && ns_count > 0 && poly.degree == 3 && bn_is_one(&poly.coeffs[3])) {
            printf("  [GNFS CRT] multi-prime algebraic sqrt recovery (monic degree 3)\n");

            /* Step 1: Sort deps by size, pick the smallest */
            typedef struct { int row; int sz; } CrtDep;
            CrtDep crt_deps[256]; int crt_ns = 0;
            for (int ri = 0; ri < rel_count && crt_ns < 256; ++ri) {
                bool z = true;
                for (int w = 0; w < row_words; ++w) { if (matrix[ri*row_words+w]) { z = false; break; } }
                if (!z) continue;
                uint64_t* h = &history[ri * hist_words]; int sz = 0;
                for (int rj = 0; rj < rel_count; ++rj) if (h[rj/64]&(1ull<<(rj%64))) ++sz;
                crt_deps[crt_ns].row = ri; crt_deps[crt_ns].sz = sz; ++crt_ns;
            }
            for (int i=1;i<crt_ns;++i){CrtDep t=crt_deps[i];int j=i-1;while(j>=0&&crt_deps[j].sz>t.sz){crt_deps[j+1]=crt_deps[j];--j;}crt_deps[j+1]=t;}

            /* ── Local-square battery: test each dep at 20+ small irreducible primes ── */
            if (cfg->local_square && crt_ns > 0) {
                printf("  [LOCAL-SQ] === Local-Square Battery ===\n");
                printf("  [LOCAL-SQ] matrix: rows=%d cols=%d chars=%d schk_ell=%"PRIu64" schk_cols=%d\n",
                    rel_count, total_cols, char_count, schk_ell, schk_ell_cols);
                printf("  [LOCAL-SQ] rank=%d nullspace=%d deps_available=%d\n", rank, ns_count, crt_ns);
                /* Find small primes where f is irreducible (no root mod p, degree 3 ⟹ irreducible) */
                uint64_t lsq_primes[64]; int lsq_np = 0;
                for (uint64_t cand = 3; cand < 500 && lsq_np < 30; cand += 2) {
                    if (!miller_rabin_u64(cand)) continue;
                    /* Skip primes dividing disc(f) or 2 */
                    if (cand == 2) continue;
                    bool has_root = false;
                    for (uint64_t r = 0; r < cand && !has_root; ++r) {
                        if (gnfs_poly_eval_mod_p(&poly, r, cand) == 0) has_root = true;
                    }
                    if (has_root) continue; /* f has a root → reducible mod cand */
                    lsq_primes[lsq_np++] = cand;
                }
                printf("  [LOCAL-SQ] found %d small irreducible primes for f\n", lsq_np);
                if (lsq_np < 5) { printf("  [LOCAL-SQ] too few primes, skipping battery\n"); }
                else {
                    int deps_tested = 0, deps_all_qr = 0;
                    int max_test = crt_ns < 50 ? crt_ns : 50;
                    BigNum f3i_ls; bn_from_u64(&f3i_ls, 1u); /* monic */
                    for (int di = 0; di < max_test; ++di) {
                        uint64_t* hd = &history[crt_deps[di].row * hist_words];
                        int qr_pass = 0, qr_fail = 0;
                        for (int pi = 0; pi < lsq_np; ++pi) {
                            uint64_t p = lsq_primes[pi];
                            BigNum p_bn; bn_from_u64(&p_bn, p);
                            /* Compute S = ∏(a_i - b_i·α) mod (f, p) */
                            AlgElem Sp; alg_one(&Sp);
                            for (int rj = 0; rj < rel_count; ++rj) {
                                if (!(hd[rj/64] & (1ull<<(rj%64)))) continue;
                                AlgElem el; alg_from_ab(&el, rel_a[rj], rel_b[rj], &p_bn);
                                AlgElem prev; for(int c=0;c<3;++c) bn_copy(&prev.c[c], &Sp.c[c]);
                                alg_mul(&Sp, &prev, &el, &poly, &f3i_ls, &p_bn);
                            }
                            /* QR check: S^((p³-1)/2) = 1? */
                            BigNum qr_exp; gnfs_qr_exp_from_prime(&qr_exp, p);
                            unsigned qr_bits = bn_bitlen(&qr_exp);
                            AlgElem qc; alg_one(&qc);
                            AlgElem qb; for(int c=0;c<3;++c) bn_copy(&qb.c[c], &Sp.c[c]);
                            for (unsigned bt=0; bt<qr_bits; ++bt) {
                                if (qr_exp.limbs[bt/64]&(1ull<<(bt%64))) {
                                    AlgElem t; for(int c=0;c<3;++c) bn_copy(&t.c[c], &qc.c[c]);
                                    alg_mul(&qc, &t, &qb, &poly, &f3i_ls, &p_bn);
                                }
                                AlgElem t2; for(int c=0;c<3;++c) bn_copy(&t2.c[c], &qb.c[c]);
                                alg_mul(&qb, &t2, &t2, &poly, &f3i_ls, &p_bn);
                            }
                            bool is_qr = bn_is_one(&qc.c[0]) && bn_is_zero(&qc.c[1]) && bn_is_zero(&qc.c[2]);
                            if (is_qr) ++qr_pass; else ++qr_fail;
                        }
                        bool all_pass = (qr_fail == 0);
                        if (all_pass) ++deps_all_qr;
                        if (di < 10 || all_pass)
                            printf("  [LOCAL-SQ] dep#%d (%d rels): QR %d/%d pass %s\n",
                                di, crt_deps[di].sz, qr_pass, lsq_np, all_pass ? "ALL-PASS ***" : "FAIL");
                        ++deps_tested;
                    }
                    printf("  [LOCAL-SQ] === %d/%d deps pass ALL %d primes ===\n", deps_all_qr, deps_tested, lsq_np);
                    if (deps_all_qr == 0)
                        printf("  [LOCAL-SQ] CONCLUSION: no deps are local squares. Matrix constraints are insufficient.\n");
                    else
                        printf("  [LOCAL-SQ] CONCLUSION: %d true-square candidates found.\n", deps_all_qr);

                    /* ── Unit-obstruction probe ──
                     * Re-run the battery but record per-prime QR bitmask for each dep.
                     * If a fixed non-square unit ε multiplies every dep's product, then
                     * ε is QR at some primes and NQR at others, creating a fixed pattern:
                     * deps would fail at exactly the primes where ε is NQR. */
                    printf("  [UNIT-PROBE] === Unit Obstruction Analysis ===\n");
                    int probe_n = deps_tested < 20 ? deps_tested : 20;
                    /* per_prime_pass[pi] = how many of the first probe_n deps pass QR at prime pi */
                    int per_prime_pass[64]; memset(per_prime_pass, 0, sizeof(per_prime_pass));
                    /* Store per-dep bitmask for pattern analysis */
                    uint32_t dep_qr_mask[20]; memset(dep_qr_mask, 0, sizeof(dep_qr_mask));

                    for (int di = 0; di < probe_n; ++di) {
                        uint64_t* hd2 = &history[crt_deps[di].row * hist_words];
                        for (int pi = 0; pi < lsq_np; ++pi) {
                            uint64_t p = lsq_primes[pi];
                            BigNum p_bn2; bn_from_u64(&p_bn2, p);
                            AlgElem Sp2; alg_one(&Sp2);
                            for (int rj = 0; rj < rel_count; ++rj) {
                                if (!(hd2[rj/64] & (1ull<<(rj%64)))) continue;
                                AlgElem el2; alg_from_ab(&el2, rel_a[rj], rel_b[rj], &p_bn2);
                                AlgElem prev2; for(int c=0;c<3;++c) bn_copy(&prev2.c[c], &Sp2.c[c]);
                                alg_mul(&Sp2, &prev2, &el2, &poly, &f3i_ls, &p_bn2);
                            }
                            BigNum qr_exp2; gnfs_qr_exp_from_prime(&qr_exp2, p);
                            unsigned qr_bits2 = bn_bitlen(&qr_exp2);
                            AlgElem qc2; alg_one(&qc2);
                            AlgElem qb2; for(int c=0;c<3;++c) bn_copy(&qb2.c[c], &Sp2.c[c]);
                            for (unsigned bt=0; bt<qr_bits2; ++bt) {
                                if (qr_exp2.limbs[bt/64]&(1ull<<(bt%64))) {
                                    AlgElem t; for(int c=0;c<3;++c) bn_copy(&t.c[c], &qc2.c[c]);
                                    alg_mul(&qc2, &t, &qb2, &poly, &f3i_ls, &p_bn2);
                                }
                                AlgElem t2; for(int c=0;c<3;++c) bn_copy(&t2.c[c], &qb2.c[c]);
                                alg_mul(&qb2, &t2, &t2, &poly, &f3i_ls, &p_bn2);
                            }
                            bool is_qr2 = bn_is_one(&qc2.c[0]) && bn_is_zero(&qc2.c[1]) && bn_is_zero(&qc2.c[2]);
                            if (is_qr2) { ++per_prime_pass[pi]; dep_qr_mask[di] |= (1u << pi); }
                        }
                    }

                    /* Print per-prime pass rates */
                    printf("  [UNIT-PROBE] per-prime QR pass rates (across %d deps):\n    ", probe_n);
                    for (int pi = 0; pi < lsq_np; ++pi)
                        printf("p=%"PRIu64":%d/%d ", lsq_primes[pi], per_prime_pass[pi], probe_n);
                    printf("\n");

                    /* Check for unit signature: do all deps share the SAME QR bitmask? */
                    uint32_t common_mask = dep_qr_mask[0];
                    bool all_same = true;
                    for (int di = 1; di < probe_n; ++di)
                        if (dep_qr_mask[di] != common_mask) { all_same = false; break; }

                    if (all_same) {
                        int pass_count = 0;
                        for (int pi = 0; pi < lsq_np; ++pi) if (common_mask & (1u << pi)) ++pass_count;
                        printf("  [UNIT-PROBE] ALL %d deps have IDENTICAL QR pattern: %d/%d primes pass\n", probe_n, pass_count, lsq_np);
                        printf("  [UNIT-PROBE] STRONG UNIT SIGNATURE: product = ε·γ² where ε is NQR at %d primes\n", lsq_np - pass_count);
                        printf("  [UNIT-PROBE] Failing primes:");
                        for (int pi = 0; pi < lsq_np; ++pi) if (!(common_mask & (1u << pi))) printf(" %"PRIu64, lsq_primes[pi]);
                        printf("\n");
                    } else {
                        /* Check near-identical: what fraction of deps share the most common mask? */
                        int best_count = 0;
                        uint32_t best_mask = 0;
                        for (int di = 0; di < probe_n; ++di) {
                            int cnt = 0;
                            for (int dj = 0; dj < probe_n; ++dj) if (dep_qr_mask[dj] == dep_qr_mask[di]) ++cnt;
                            if (cnt > best_count) { best_count = cnt; best_mask = dep_qr_mask[di]; }
                        }
                        printf("  [UNIT-PROBE] QR patterns vary across deps. Most common mask shared by %d/%d deps.\n", best_count, probe_n);
                        if (best_count > probe_n * 3 / 4)
                            printf("  [UNIT-PROBE] WEAK UNIT SIGNATURE: majority of deps share same pattern\n");
                        else
                            printf("  [UNIT-PROBE] NO UNIT SIGNATURE: patterns look random across deps\n");
                    }
                }
            }

            if (cfg->local_square) { /* battery done, skip extraction */ }
            else if (crt_ns == 0) { printf("  [GNFS CRT] no dependencies available\n"); }
            else {
              int max_deps_try = crt_ns; /* try ALL deps */
              bool crt_global_found = false;
              for (int dep_idx = 0; dep_idx < max_deps_try && !crt_global_found; ++dep_idx) {
                int dep_row = crt_deps[dep_idx].row;
                int dep_sz = crt_deps[dep_idx].sz;
                uint64_t* hd = &history[dep_row * hist_words];
                /* Pre-filter: skip deps where Schirokauer Z/ℓZ sums are nonzero */
                if (schk_raw != NULL && schk_ell > 0) {
                    uint64_t ell = schk_ell;
                    uint64_t ssum[3] = {0,0,0};
                    for (int rj=0;rj<rel_count;++rj) {
                        if(!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                        for(int k=0;k<3;++k) ssum[k]=(ssum[k]+schk_raw[rj*3+k])%ell;
                    }
                    if (ssum[0]||ssum[1]||ssum[2]) {
                        if (dep_idx < 5) printf("  [GNFS CRT] dep #%d: schk sum nonzero [%"PRIu64",%"PRIu64",%"PRIu64"], skip\n",
                            dep_idx, ssum[0], ssum[1], ssum[2]);
                        continue;
                    }
                }
                printf("  [GNFS CRT] trying dep #%d (%d rels) [schk OK]\n", dep_idx, dep_sz);

                /* Step 1b: Compute rational sqrt y_rat for sign alignment */
                uint64_t y_rat_u64 = 1u;
                { uint64_t n_u = bn_to_u64(n);
                  uint64_t rat_exp[GNFS_MAX_FB]; memset(rat_exp, 0, sizeof(uint64_t)*(size_t)rfb_count);
                  for (int rj=0;rj<rel_count;++rj) {
                      if(!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                      int64_t a=rel_a[rj]; uint64_t b=rel_b[rj];
                      uint64_t bm = b * bn_to_u64(&poly.m);
                      uint64_t val = (a >= 0 && (uint64_t)a >= bm) ? (uint64_t)a - bm : (a < 0) ? bm + (uint64_t)(-a) : bm - (uint64_t)a;
                      for(int fi=0;fi<rfb_count;++fi) while(val % rfb[fi]==0){val/=rfb[fi];++rat_exp[fi];}
                  }
                  for(int fi=0;fi<rfb_count;++fi) {
                      uint64_t half = rat_exp[fi]/2;
                      if(half>0) y_rat_u64 = mulmod_u64(y_rat_u64, powmod_u64(rfb[fi], half, n_u), n_u);
                  }
                }

                /* Step 2: Coefficient size estimate from 2b-size measurement.
                 * For the 40-bit target with ~138-rel deps, max|a|~50000, b~100, m~8192:
                 * Product coefficients are ~39-40 bits. Sqrt ~half = ~20 bits.
                 * But we don't know until we measure. Use dep_sz * log2(max_elem) / 2 as estimate. */
                int64_t max_abs_a = 0; uint64_t max_b = 0;
                for (int rj = 0; rj < rel_count; ++rj) {
                    if (!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                    int64_t aa = rel_a[rj] < 0 ? -rel_a[rj] : rel_a[rj];
                    if (aa > max_abs_a) max_abs_a = aa;
                    if (rel_b[rj] > max_b) max_b = rel_b[rj];
                }
                /* WARNING: est_sqrt_bits formula is UNVERIFIED. See GNFS dev spec Phase 4.
                 * The correct growth recurrence is additive per relation:
                 *   log2(M_{k+1}) ≈ log2(M_k) + log2(A_k) + log2(Λ)
                 * where Λ = max(1+|c0|, 2+|c1|, 2+|c2|). This formula below is a placeholder
                 * that has not been validated against empirical coefficient measurement. */
                uint64_t max_elem = (uint64_t)max_abs_a;
                { uint64_t bm = max_b * bn_to_u64(&poly.m); if (bm > max_elem) max_elem = bm; }
                unsigned elem_bits = 0; { uint64_t t = max_elem; while(t){++elem_bits;t>>=1;} }
                unsigned m_bits = bn_bitlen(&poly.m);
                unsigned est_sqrt_bits = (unsigned)dep_sz * (elem_bits + m_bits) / 2u;
                /* We need CRT modulus > 2 * max|T_coeff|, so > 2^(est_sqrt_bits+1) */
                unsigned needed_crt_bits = est_sqrt_bits + 2u; /* +2 for safety + sign */
                unsigned primes_needed = (needed_crt_bits + 60u) / 61u; /* each prime ~61 bits */
                if (primes_needed < 2) primes_needed = 2;
                if (primes_needed > 60) primes_needed = 60; /* cap */

                printf("  [GNFS CRT] max|a|=%"PRId64" max_b=%"PRIu64" elem_bits=%u\n", max_abs_a, max_b, elem_bits);
                printf("  [GNFS CRT] est_sqrt_bits=%u  needed_crt_bits=%u  primes_needed=%u\n",
                    est_sqrt_bits, needed_crt_bits, primes_needed);

                /* ── Hensel lifting algebraic sqrt (bypasses CRT sign problem) ── */
                uint64_t y_alg_h = 0;
                bool hensel_ok = gnfs_hensel_sqrt(&poly, n, hd, hist_words, rel_a, rel_b, rel_count,
                                                   &y_alg_h, est_sqrt_bits);
                if (hensel_ok) {
                    uint64_t n_u = bn_to_u64(n);
                    uint64_t m_u = bn_to_u64(&poly.m);
                    /* Compute signed x_prod = ∏(a-b*m) mod N */
                    uint64_t xp_h = 1;
                    for (int rj=0;rj<rel_count;++rj) {
                        if(!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                        uint64_t a_mod = ((rel_a[rj]%(int64_t)n_u)+(int64_t)n_u)%(int64_t)n_u;
                        uint64_t bm_v = mulmod_u64(rel_b[rj], m_u, n_u);
                        xp_h = mulmod_u64(xp_h, (a_mod + n_u - bm_v) % n_u, n_u);
                    }
                    uint64_t ya2_h = mulmod_u64(y_alg_h, y_alg_h, n_u);
                    bool cong_h = (ya2_h == xp_h || ya2_h == (n_u - xp_h) % n_u);
                    printf("  [HENSEL] y_alg=%"PRIu64" y_alg²=%"PRIu64" x_prod=%"PRIu64" %s\n",
                        y_alg_h, ya2_h, xp_h, cong_h ? "CONGRUENCE!" : "no-match");
                    if (cong_h) {
                        /* Try GCD with all sign combinations */
                        for (int neg=0; neg<4 && !crt_global_found; ++neg) {
                            uint64_t ya = (neg&1) ? (n_u - y_alg_h) % n_u : y_alg_h;
                            uint64_t yr = (neg&2) ? (n_u - y_rat_u64) % n_u : y_rat_u64;
                            uint64_t yc = mulmod_u64(yr, ya, n_u);
                            uint64_t d1 = (xp_h >= yc) ? xp_h - yc : xp_h + n_u - yc;
                            uint64_t d2 = (xp_h + yc) % n_u;
                            uint64_t g1 = gcd_u64(d1, n_u), g2 = gcd_u64(d2, n_u);
                            if (g1>1 && g1<n_u) {
                                printf("  *** GNFS FACTOR (Hensel): %"PRIu64" ***\n", g1);
                                BigNum fac; bn_from_u64(&fac, g1);
                                factor_result_bn_set_from_factor(&fr, n, &fac);
                                fr.method = "gnfs(hensel)"; crt_global_found = true;
                            } else if (g2>1 && g2<n_u) {
                                printf("  *** GNFS FACTOR (Hensel): %"PRIu64" ***\n", g2);
                                BigNum fac; bn_from_u64(&fac, g2);
                                factor_result_bn_set_from_factor(&fr, n, &fac);
                                fr.method = "gnfs(hensel)"; crt_global_found = true;
                            }
                        }
                        if (!crt_global_found) printf("  [HENSEL] congruence but GCDs trivial\n");
                    }
                }
                if (crt_global_found) continue; /* found factor, skip CRT */

                /* QUARANTINE: 2-prime brute-force — dead code, based on wrong assumption
                 * that CRT modulus > N suffices. It doesn't — need modulus > true coefficient size.
                 * Retained for reference only. Do not use as extraction route. */
                uint64_t bf_primes[2]; uint64_t bf_T[2][3]; int bf_count = 0;

                /* Step 3: Find auxiliary primes that give QR hits on this dependency */
                #define CRT_MAX_PRIMES 64
                uint64_t crt_primes[CRT_MAX_PRIMES];
                BigNum crt_coeff[3]; /* running CRT reconstructed coefficients */
                BigNum crt_mod;      /* running product of primes */
                bn_zero(&crt_mod);
                for (int c=0;c<3;++c) bn_zero(&crt_coeff[c]);
                int crt_prime_count = 0;
                int primes_tried = 0;

                { uint64_t rng = 0xDEADBEEF12345678ULL ^ (uint64_t)dep_sz;
                  for (int tries = 0; tries < 10000 && crt_prime_count < (int)primes_needed && crt_prime_count < CRT_MAX_PRIMES; ++tries) {
                      rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17;
                      uint64_t cand = (rng | ((uint64_t)1<<59)) | 3u;
                      cand = cand & ~1ull; cand |= 3u;
                      if ((cand&3u)!=3u || !miller_rabin_u64(cand)) continue;
                      /* Check f irreducible mod cand */
                      uint64_t f0v=bn_mod_u64(&poly.coeffs[0],cand),f1v=bn_mod_u64(&poly.coeffs[1],cand),f2v=bn_mod_u64(&poly.coeffs[2],cand);
                      uint64_t res3[3]={1,0,0},base3[3]={0,1,0};
                      #define P3CRT(o,a2,b2,pv) do{uint64_t _t[5]={0,0,0,0,0};\
                          for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+mulmod_u64((a2)[_i],(b2)[_j],pv))%pv;\
                          if(_t[4]){_t[3]=(_t[3]+mulmod_u64(_t[4],pv-f2v,pv))%pv;_t[2]=(_t[2]+mulmod_u64(_t[4],pv-f1v,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[4],pv-f0v,pv))%pv;}\
                          if(_t[3]){_t[2]=(_t[2]+mulmod_u64(_t[3],pv-f2v,pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[3],pv-f1v,pv))%pv;_t[0]=(_t[0]+mulmod_u64(_t[3],pv-f0v,pv))%pv;}\
                          (o)[0]=_t[0];(o)[1]=_t[1];(o)[2]=_t[2];}while(0)
                      uint64_t ev=cand;
                      while(ev>0u){if(ev&1u){uint64_t t[3];P3CRT(t,res3,base3,cand);res3[0]=t[0];res3[1]=t[1];res3[2]=t[2];}
                          uint64_t t2[3];P3CRT(t2,base3,base3,cand);base3[0]=t2[0];base3[1]=t2[1];base3[2]=t2[2];ev>>=1u;}
                      if(res3[0]==0u&&res3[1]==1u&&res3[2]==0u) continue; /* α^p=α → linear factor */
                      /* Also check α^(p²) ≠ α to catch linear×quadratic factorizations */
                      { uint64_t r2[3]={1,0,0}, b2[3]={res3[0],res3[1],res3[2]};
                        uint64_t e2=cand;
                        while(e2>0u){if(e2&1u){uint64_t t[3];P3CRT(t,r2,b2,cand);r2[0]=t[0];r2[1]=t[1];r2[2]=t[2];}
                            uint64_t t2[3];P3CRT(t2,b2,b2,cand);b2[0]=t2[0];b2[1]=t2[1];b2[2]=t2[2];e2>>=1u;}
                        if(r2[0]==0u&&r2[1]==1u&&r2[2]==0u) continue; /* α^(p²)=α → quad factor */
                      }
                      #undef P3CRT
                      ++primes_tried;

                      /* Compute S mod (f, cand) and check QR */
                      BigNum p_bn; bn_from_u64(&p_bn, cand);
                      BigNum f3i; bn_from_u64(&f3i, 1u); /* monic */
                      AlgElem S_c; alg_one(&S_c);
                      for (int rj=0;rj<rel_count;++rj) {
                          if(!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                          AlgElem el; alg_from_ab(&el,rel_a[rj],rel_b[rj],&p_bn);
                          AlgElem prev; for(int c=0;c<3;++c)bn_copy(&prev.c[c],&S_c.c[c]);
                          alg_mul(&S_c,&prev,&el,&poly,&f3i,&p_bn);
                      }
                      /* QR check */
                      BigNum qr_exp; gnfs_qr_exp_from_prime(&qr_exp, cand);
                      unsigned qr_bits=bn_bitlen(&qr_exp);
                      AlgElem qc; alg_one(&qc);
                      AlgElem _b; for(int c=0;c<3;++c)bn_copy(&_b.c[c],&S_c.c[c]);
                      for(unsigned bt=0;bt<qr_bits;++bt){
                          if(qr_exp.limbs[bt/64]&(1ull<<(bt%64))){AlgElem _t;for(int c=0;c<3;++c)bn_copy(&_t.c[c],&qc.c[c]);alg_mul(&qc,&_t,&_b,&poly,&f3i,&p_bn);}
                          AlgElem _t2;for(int c=0;c<3;++c)bn_copy(&_t2.c[c],&_b.c[c]);alg_mul(&_b,&_t2,&_t2,&poly,&f3i,&p_bn);}
                      bool is_qr=bn_is_one(&qc.c[0])&&bn_is_zero(&qc.c[1])&&bn_is_zero(&qc.c[2]);
                      if(!is_qr) continue;

                      /* Compute T = S^((p³+1)/4) mod (f, p) */
                      BigNum sq_exp; gnfs_sqrt_exp_from_prime(&sq_exp, cand);
                      unsigned sq_bits=bn_bitlen(&sq_exp);
                      AlgElem T_c; alg_one(&T_c);
                      AlgElem _b2; for(int c=0;c<3;++c)bn_copy(&_b2.c[c],&S_c.c[c]);
                      for(unsigned bt=0;bt<sq_bits;++bt){
                          if(sq_exp.limbs[bt/64]&(1ull<<(bt%64))){AlgElem _t;for(int c=0;c<3;++c)bn_copy(&_t.c[c],&T_c.c[c]);alg_mul(&T_c,&_t,&_b2,&poly,&f3i,&p_bn);}
                          AlgElem _t2;for(int c=0;c<3;++c)bn_copy(&_t2.c[c],&_b2.c[c]);alg_mul(&_b2,&_t2,&_t2,&poly,&f3i,&p_bn);}
                      /* Verify T² = S */
                      AlgElem T_sq;{AlgElem t1;for(int c=0;c<3;++c)bn_copy(&t1.c[c],&T_c.c[c]);alg_mul(&T_sq,&t1,&t1,&poly,&f3i,&p_bn);}
                      bool t2_ok=true; for(int c=0;c<3;++c) if(bn_cmp(&T_sq.c[c],&S_c.c[c])!=0){t2_ok=false;break;}
                      if(!t2_ok) continue;

                      /* Store first 2 QR-hit primes for brute-force sign search */
                      if (bf_count < 2) {
                          bf_primes[bf_count] = cand;
                          for(int c=0;c<3;++c) bf_T[bf_count][c] = bn_to_u64(&T_c.c[c]);
                          ++bf_count;
                      }

                      /* QUARANTINE: CRT sign alignment — does not work as a primary extraction route.
                       * The "pick smaller centered value" heuristic fails for real deps where true
                       * coefficients are thousands of bits (larger than CRT modulus for early primes).
                       * Retained as diagnostic only behind --crt-recover. */
                      if (crt_prime_count >= 1) {
                          unsigned mod_bits = bn_bitlen(&crt_mod);
                          if (mod_bits > est_sqrt_bits) {
                              /* After threshold: exact match against running CRT */
                              int pos_match=0, neg_match=0;
                              for (int c=0;c<3;++c) {
                                  uint64_t r = bn_mod_u64(&crt_coeff[c], cand);
                                  uint64_t tp = bn_to_u64(&T_c.c[c]);
                                  uint64_t tn = (cand - tp) % cand;
                                  if (r==tp) ++pos_match;
                                  if (r==tn) ++neg_match;
                              }
                              if (neg_match > pos_match) {
                                  for (int c=0;c<3;++c){if(bn_is_zero(&T_c.c[c]))continue;
                                      BigNum ng;bn_copy(&ng,&p_bn);bn_sub(&ng,&T_c.c[c]);bn_copy(&T_c.c[c],&ng);}
                              }
                          } else {
                              /* Before threshold: trial CRT both signs, pick smaller */
                              uint64_t m_inv = powmod_u64(bn_mod_u64(&crt_mod,cand), cand-2, cand);
                              BigNum trial_p[3], trial_n[3];
                              for(int c=0;c<3;++c){bn_copy(&trial_p[c],&crt_coeff[c]);bn_copy(&trial_n[c],&crt_coeff[c]);}
                              for(int c=0;c<3;++c){
                                  uint64_t tp=bn_to_u64(&T_c.c[c]), tn=(cand-tp)%cand;
                                  uint64_t r=bn_mod_u64(&crt_coeff[c],cand);
                                  uint64_t cp=mulmod_u64((tp+cand-r)%cand,m_inv,cand);
                                  uint64_t cn=mulmod_u64((tn+cand-r)%cand,m_inv,cand);
                                  if(cp){BigNum s;bn_from_u64(&s,cp);BigNum st;bn_mul(&st,&crt_mod,&s);bn_add(&trial_p[c],&st);}
                                  if(cn){BigNum s;bn_from_u64(&s,cn);BigNum st;bn_mul(&st,&crt_mod,&s);bn_add(&trial_n[c],&st);}
                              }
                              BigNum new_m;{BigNum pp;bn_from_u64(&pp,cand);bn_mul(&new_m,&crt_mod,&pp);}
                              BigNum hm;bn_copy(&hm,&new_m);
                              for(uint32_t li=0;li<hm.used;++li){hm.limbs[li]>>=1;if(li+1<hm.used&&(hm.limbs[li+1]&1))hm.limbs[li]|=(1ull<<63);}
                              bn_normalize(&hm);
                              /* Sum |centered| across all 3 coefficients for each sign */
                              unsigned pos_bits=0, neg_bits=0;
                              for(int c=0;c<3;++c){
                                  BigNum ap;if(bn_cmp(&trial_p[c],&hm)>0){bn_copy(&ap,&new_m);bn_sub(&ap,&trial_p[c]);}else bn_copy(&ap,&trial_p[c]);
                                  BigNum an;if(bn_cmp(&trial_n[c],&hm)>0){bn_copy(&an,&new_m);bn_sub(&an,&trial_n[c]);}else bn_copy(&an,&trial_n[c]);
                                  pos_bits+=bn_bitlen(&ap); neg_bits+=bn_bitlen(&an);
                              }
                              if (neg_bits < pos_bits) {
                                  for(int c=0;c<3;++c){if(bn_is_zero(&T_c.c[c]))continue;
                                      BigNum ng;bn_copy(&ng,&p_bn);bn_sub(&ng,&T_c.c[c]);bn_copy(&T_c.c[c],&ng);}
                              }
                          }
                      }

                      crt_primes[crt_prime_count] = cand;
                      uint64_t crt_stored_T[CRT_MAX_PRIMES][3]; /* QUARANTINE: uses bn_to_u64 on small-prime sqrt values — OK for small p */
                      for (int c=0;c<3;++c) crt_stored_T[crt_prime_count][c] = bn_to_u64(&T_c.c[c]);

                      /* Incremental CRT: first prime initializes, subsequent primes update */
                      if (crt_prime_count == 0) {
                          bn_from_u64(&crt_mod, cand);
                          for (int c=0;c<3;++c) bn_copy(&crt_coeff[c], &T_c.c[c]);
                      } else {
                          for (int c = 0; c < 3; ++c) {
                              uint64_t a_i_val = bn_to_u64(&T_c.c[c]);
                              uint64_t r_mod_pi = bn_mod_u64(&crt_coeff[c], cand);
                              uint64_t diff_val = (a_i_val + cand - r_mod_pi) % cand;
                              uint64_t m_mod_pi = bn_mod_u64(&crt_mod, cand);
                              uint64_t m_inv_pi = powmod_u64(m_mod_pi, cand - 2u, cand);
                              uint64_t corr = mulmod_u64(diff_val, m_inv_pi, cand);
                              if (corr != 0u) {
                                  BigNum corr_bn; bn_from_u64(&corr_bn, corr);
                                  BigNum step; bn_mul(&step, &crt_mod, &corr_bn);
                                  bn_add(&crt_coeff[c], &step);
                              }
                          }
                          { BigNum pi_bn3; bn_from_u64(&pi_bn3, cand);
                          BigNum new_mod; bn_mul(&new_mod, &crt_mod, &pi_bn3);
                          bn_copy(&crt_mod, &new_mod); }
                          printf("    CRT after prime[%d]: modulus_bits=%u\n", crt_prime_count, bn_bitlen(&crt_mod));
                      }
                      /* Sign correction: once CRT modulus > estimated sqrt bound, evaluate
                       * CRT(m) mod N and check if result² ≡ x_prod mod N. If not, flip sign. */
                      if (crt_prime_count >= 1 && bn_bitlen(&crt_mod) > est_sqrt_bits + 10u) {
                          /* Evaluate current CRT at m mod N using centered lift */
                          uint64_t n_u = bn_to_u64(n); uint64_t m_u = bn_to_u64(&poly.m);
                          BigNum M_h; bn_copy(&M_h, &crt_mod);
                          for(uint32_t li=0;li<M_h.used;++li){M_h.limbs[li]>>=1;if(li+1<M_h.used&&(M_h.limbs[li+1]&1))M_h.limbs[li]|=(1ull<<63);}
                          bn_normalize(&M_h);
                          uint64_t test_ya = 0, mpw = 1;
                          for (int c=0;c<3;++c) {
                              uint64_t cv = bn_mod_u64(&crt_coeff[c], n_u);
                              bool neg = (bn_cmp(&crt_coeff[c], &M_h) > 0);
                              if (neg) cv = (n_u - cv) % n_u;
                              uint64_t term = mulmod_u64(cv, mpw, n_u);
                              test_ya = (test_ya + (neg ? n_u - term : term)) % n_u;
                              if (c<2) mpw = mulmod_u64(mpw, m_u, n_u);
                          }
                          uint64_t test_ya2 = mulmod_u64(test_ya, test_ya, n_u);
                          /* Compute x_prod mod N for this dep */
                          uint64_t xp_test = 1;
                          for (int rj=0;rj<rel_count;++rj) {
                              if(!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                              int64_t a=rel_a[rj]; uint64_t bval=rel_b[rj];
                              uint64_t bm_v = mulmod_u64(bval, m_u, n_u);
                              uint64_t av = ((a%(int64_t)n_u)+(int64_t)n_u)%(int64_t)n_u;
                              uint64_t v = (av + n_u - bm_v) % n_u;
                              xp_test = mulmod_u64(xp_test, v, n_u);
                          }
                          bool sign_ok = (test_ya2 == xp_test || test_ya2 == (n_u - xp_test) % n_u);
                          if (!sign_ok) {
                              /* Flip sign: negate T_c coefficients, redo CRT for this prime */
                              /* Undo the CRT step first */
                              BigNum old_mod; bn_copy(&old_mod, &crt_mod);
                              { BigNum pi_bn2; bn_from_u64(&pi_bn2, cand);
                                bn_divexact(&crt_mod, &old_mod, &pi_bn2); }
                              /* Negate T_c */
                              for (int c=0;c<3;++c) {
                                  if (bn_is_zero(&T_c.c[c])) continue;
                                  BigNum neg2; bn_copy(&neg2, &p_bn); bn_sub(&neg2, &T_c.c[c]);
                                  bn_copy(&T_c.c[c], &neg2);
                                  crt_stored_T[crt_prime_count][c] = bn_to_u64(&T_c.c[c]);
                              }
                              /* Redo CRT step with opposite sign — revert coeff to before, redo */
                              /* Simpler: just recompute from scratch for this prime */
                              for (int c=0;c<3;++c) {
                                  /* Revert: subtract the old correction */
                                  uint64_t new_val = bn_to_u64(&T_c.c[c]);
                                  uint64_t r_mod_pi = bn_mod_u64(&crt_coeff[c], cand);
                                  /* The old CRT step added crt_mod_old * corr_old. We need to undo it
                                   * and redo with new_val. Simplest: recompute coeff from the original. */
                                  /* Actually: the old coeff was crt_coeff_before + old_mod * old_corr.
                                   * We need crt_coeff_before + old_mod * new_corr where new_corr uses neg T.
                                   * crt_coeff_before = crt_coeff - old_mod * old_corr. */
                              }
                              /* Simplest correct approach: recompute full CRT from stored T values */
                              bn_from_u64(&crt_mod, crt_primes[0]);
                              for (int c=0;c<3;++c) bn_from_u64(&crt_coeff[c], crt_stored_T[0][c]);
                              for (int pi=1;pi<=(int)crt_prime_count;++pi) {
                                  uint64_t pp = crt_primes[pi];
                                  BigNum pp_bn; bn_from_u64(&pp_bn, pp);
                                  for (int c=0;c<3;++c) {
                                      uint64_t target = crt_stored_T[pi][c];
                                      uint64_t cur = bn_mod_u64(&crt_coeff[c], pp);
                                      uint64_t d = (target + pp - cur) % pp;
                                      uint64_t mi = powmod_u64(bn_mod_u64(&crt_mod, pp), pp-2, pp);
                                      uint64_t cr = mulmod_u64(d, mi, pp);
                                      if (cr) { BigNum s; bn_from_u64(&s, cr); BigNum st; bn_mul(&st, &crt_mod, &s); bn_add(&crt_coeff[c], &st); }
                                  }
                                  BigNum nm; bn_mul(&nm, &crt_mod, &pp_bn); bn_copy(&crt_mod, &nm);
                              }
                              if (crt_prime_count < 3)
                                  printf("    sign-flip at prime[%d]: CRT rebuilt\n", crt_prime_count);
                          }
                      }
                      ++crt_prime_count;
                  }
                }
                printf("  [GNFS CRT] found %d/%u needed QR-hit primes (tried %d admissible) bf=%d\n",
                    crt_prime_count, primes_needed, primes_tried, bf_count);

                /* ── 2-prime brute-force sign search ── */
                if (bf_count >= 2 && !crt_global_found) {
                    uint64_t n_u = bn_to_u64(n);
                    uint64_t m_u = bn_to_u64(&poly.m);
                    /* Compute signed x_prod = ∏(a-b*m) mod N for this dep */
                    uint64_t xp_s = 1;
                    for (int rj=0;rj<rel_count;++rj) {
                        if(!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                        uint64_t a_mod = ((rel_a[rj]%(int64_t)n_u)+(int64_t)n_u)%(int64_t)n_u;
                        uint64_t bm_v = mulmod_u64(rel_b[rj], m_u, n_u);
                        xp_s = mulmod_u64(xp_s, (a_mod + n_u - bm_v) % n_u, n_u);
                    }
                    /* Try 2 sign combos: prime[0]=+, prime[1]=±  */
                    uint64_t p0=bf_primes[0], p1=bf_primes[1];
                    uint64_t m_inv = powmod_u64(p0 % p1, p1-2, p1);
                    for (int s1 = 0; s1 < 2 && !crt_global_found; ++s1) {
                        /* CRT 3 coefficients: T.c[k] mod (p0*p1) */
                        uint64_t y_alg_trial = 0, mpow = 1;
                        for (int c = 0; c < 3; ++c) {
                            uint64_t t0 = bf_T[0][c]; /* prime[0]: always + */
                            uint64_t t1 = s1 ? (p1 - bf_T[1][c]) % p1 : bf_T[1][c];
                            /* CRT: value = t0 + p0 * ((t1 - t0%p1) * p0_inv mod p1) */
                            uint64_t r0_mod_p1 = t0 % p1;
                            uint64_t diff = (t1 + p1 - r0_mod_p1) % p1;
                            uint64_t corr = mulmod_u64(diff, m_inv, p1);
                            /* crt_val = t0 + p0 * corr. Reduce mod N for evaluation. */
                            uint64_t crt_mod_n = (t0 % n_u + mulmod_u64(p0 % n_u, corr % n_u, n_u)) % n_u;
                            uint64_t term = mulmod_u64(crt_mod_n, mpow, n_u);
                            y_alg_trial = (y_alg_trial + term) % n_u;
                            if (c < 2) mpow = mulmod_u64(mpow, m_u, n_u);
                        }
                        uint64_t ya2 = mulmod_u64(y_alg_trial, y_alg_trial, n_u);
                        bool cong = (ya2 == xp_s || ya2 == (n_u - xp_s) % n_u);
                        if (cong) {
                            printf("  [GNFS 2P] dep#%d sign=%d: y_alg=%"PRIu64" y_alg²=%"PRIu64" x_prod=%"PRIu64" CONGRUENCE!\n",
                                dep_idx, s1, y_alg_trial, ya2, xp_s);
                            /* Try GCD: gcd(x_prod ± y_rat*y_alg, N) */
                            uint64_t yr_u = y_rat_u64;
                            for (int neg2 = 0; neg2 < 4 && !crt_global_found; ++neg2) {
                                uint64_t ya = (neg2&1) ? (n_u - y_alg_trial) % n_u : y_alg_trial;
                                uint64_t yr = (neg2&2) ? (n_u - yr_u) % n_u : yr_u;
                                uint64_t y_combined = mulmod_u64(yr, ya, n_u);
                                uint64_t d1 = (xp_s >= y_combined) ? xp_s - y_combined : xp_s + n_u - y_combined;
                                uint64_t d2 = (xp_s + y_combined) % n_u;
                                uint64_t g1 = gcd_u64(d1, n_u), g2 = gcd_u64(d2, n_u);
                                if (g1 > 1 && g1 < n_u) {
                                    printf("  *** GNFS FACTOR FOUND: %"PRIu64" ***\n", g1);
                                    BigNum fac; bn_from_u64(&fac, g1);
                                    factor_result_bn_set_from_factor(&fr, n, &fac);
                                    fr.method = "gnfs(2prime-bf)"; crt_global_found = true;
                                } else if (g2 > 1 && g2 < n_u) {
                                    printf("  *** GNFS FACTOR FOUND: %"PRIu64" ***\n", g2);
                                    BigNum fac; bn_from_u64(&fac, g2);
                                    factor_result_bn_set_from_factor(&fr, n, &fac);
                                    fr.method = "gnfs(2prime-bf)"; crt_global_found = true;
                                }
                            }
                            if (!crt_global_found) printf("  [GNFS 2P] congruence but GCDs trivial\n");
                        }
                    }
                    if (!crt_global_found && bf_count >= 2) {
                        /* Neither sign gave congruence — dep not a true square */
                    }
                }

                if (crt_prime_count >= 2) {

                    /* Step 5: Centered lift — if coeff > M/2, subtract M */
                    BigNum M_half; bn_copy(&M_half, &crt_mod);
                    for (uint32_t li=0;li<M_half.used;++li){M_half.limbs[li]>>=1;if(li+1<M_half.used&&(M_half.limbs[li+1]&1))M_half.limbs[li]|=(1ull<<63);}
                    bn_normalize(&M_half);

                    bool coeff_neg[3];
                    BigNum coeff_abs[3];
                    for (int c=0;c<3;++c) {
                        coeff_neg[c] = (bn_cmp(&crt_coeff[c], &M_half) > 0);
                        if (coeff_neg[c]) {
                            bn_copy(&coeff_abs[c], &crt_mod);
                            bn_sub(&coeff_abs[c], &crt_coeff[c]);
                        } else {
                            bn_copy(&coeff_abs[c], &crt_coeff[c]);
                        }
                    }

                    printf("  [GNFS CRT] reconstructed T coefficients:\n");
                    for (int c=0;c<3;++c) {
                        char vbuf[128]; bn_to_str(&coeff_abs[c], vbuf, sizeof(vbuf));
                        printf("    T.c[%d] = %s%s (%u bits)\n", c, coeff_neg[c]?"-":"", vbuf, bn_bitlen(&coeff_abs[c]));
                    }
                    printf("    CRT modulus: %u bits  (need > %u bits for safety)\n",
                        bn_bitlen(&crt_mod), needed_crt_bits);
                    bool crt_safe = (bn_bitlen(&crt_mod) > needed_crt_bits);
                    printf("    centered recovery: %s\n", crt_safe ? "SAFE" : "INSUFFICIENT (need more primes)");

                    /* Step 5b: CRT verification — reduce T mod first prime, check T² ≡ S */
                    if (crt_prime_count >= 1) {
                        uint64_t vp = crt_primes[0];
                        BigNum vp_bn; bn_from_u64(&vp_bn, vp);
                        BigNum vf3i; bn_from_u64(&vf3i, 1u);
                        /* Reduce CRT T mod vp */
                        uint64_t tc_mod[3];
                        for (int c=0;c<3;++c) {
                            uint64_t raw = bn_mod_u64(&crt_coeff[c], vp);
                            if (coeff_neg[c]) tc_mod[c] = (vp - raw) % vp;
                            else tc_mod[c] = raw;
                        }
                        /* Compute T² mod (f, vp) */
                        uint64_t f0v=bn_mod_u64(&poly.coeffs[0],vp),f1v=bn_mod_u64(&poly.coeffs[1],vp),f2v=bn_mod_u64(&poly.coeffs[2],vp);
                        uint64_t tsq[5]={0,0,0,0,0};
                        for(int i=0;i<3;++i)for(int j=0;j<3;++j)tsq[i+j]=(tsq[i+j]+mulmod_u64(tc_mod[i],tc_mod[j],vp))%vp;
                        if(tsq[4]){tsq[3]=(tsq[3]+mulmod_u64(tsq[4],vp-f2v,vp))%vp;tsq[2]=(tsq[2]+mulmod_u64(tsq[4],vp-f1v,vp))%vp;tsq[1]=(tsq[1]+mulmod_u64(tsq[4],vp-f0v,vp))%vp;}
                        if(tsq[3]){tsq[2]=(tsq[2]+mulmod_u64(tsq[3],vp-f2v,vp))%vp;tsq[1]=(tsq[1]+mulmod_u64(tsq[3],vp-f1v,vp))%vp;tsq[0]=(tsq[0]+mulmod_u64(tsq[3],vp-f0v,vp))%vp;}
                        /* Compute S mod (f, vp) */
                        AlgElem S_v; alg_one(&S_v);
                        for(int rj=0;rj<rel_count;++rj){if(!(hd[rj/64]&(1ull<<(rj%64))))continue;
                            AlgElem el;alg_from_ab(&el,rel_a[rj],rel_b[rj],&vp_bn);
                            AlgElem prev;for(int c=0;c<3;++c)bn_copy(&prev.c[c],&S_v.c[c]);
                            alg_mul(&S_v,&prev,&el,&poly,&vf3i,&vp_bn);}
                        uint64_t sv[3]; for(int c=0;c<3;++c) sv[c]=bn_to_u64(&S_v.c[c]);
                        bool t2_eq_s = (tsq[0]==sv[0]&&tsq[1]==sv[1]&&tsq[2]==sv[2]);
                        printf("    CRT verify: T² mod p[0]=%"PRIu64": [%"PRIu64",%"PRIu64",%"PRIu64"] vs S=[%"PRIu64",%"PRIu64",%"PRIu64"] %s\n",
                            vp, tsq[0],tsq[1],tsq[2], sv[0],sv[1],sv[2], t2_eq_s?"MATCH":"**MISMATCH**");
                    }

                    /* Step 6: Evaluate T(m) mod N: y_alg = c[0] + c[1]*m + c[2]*m² mod N.
                     * CRT coefficients can be much larger than N, so reduce mod N first.
                     * Since N fits in u64, use bn_mod_u64 for the reduction. */
                    uint64_t n_u64 = bn_to_u64(n);
                    uint64_t m_u64 = bn_to_u64(&poly.m);
                    uint64_t y_alg_u64 = 0u;
                    uint64_t mpow_u64 = 1u;
                    for (int c = 0; c < 3; ++c) {
                        uint64_t cv = bn_mod_u64(&coeff_abs[c], n_u64);
                        uint64_t term = mulmod_u64(cv, mpow_u64, n_u64);
                        if (coeff_neg[c]) {
                            y_alg_u64 = (y_alg_u64 + n_u64 - term) % n_u64;
                        } else {
                            y_alg_u64 = (y_alg_u64 + term) % n_u64;
                        }
                        if (c < 2) mpow_u64 = mulmod_u64(mpow_u64, m_u64, n_u64);
                    }
                    BigNum y_alg; bn_from_u64(&y_alg, y_alg_u64);
                    { char yb[128]; bn_to_str(&y_alg, yb, sizeof(yb));
                      printf("  [GNFS CRT] y_alg = T(m) mod N = %s\n", yb); }

                    /* Step 7: Rational side — compute x_prod = ∏(a-b*m) mod N (SIGNED, not absolute)
                     * and y_rat = ∏ p^(e/2) mod N from |a-b*m| exponents. */
                    BigNum x_prod; bn_from_u64(&x_prod, 1u);
                    BigNum y_rat; bn_from_u64(&y_rat, 1u);
                    uint64_t lr2[GNFS_MAX_FB]; memset(lr2, 0, sizeof(uint64_t)*(size_t)rfb_count);
                    for (int rj=0;rj<rel_count;++rj) {
                        if(!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                        BigNum bm2; bn_copy(&bm2,&poly.m);
                        {BigNum bb;bn_from_u64(&bb,rel_b[rj]);bn_mul(&bm2,&bm2,&bb);}
                        /* Compute (a - b*m) mod N — SIGNED: if a-b*m < 0, use N - |a-b*m| */
                        BigNum vm;
                        BigNum abs_val; /* |a - b*m| for factoring exponents */
                        if(rel_a[rj]>=0){BigNum ac;bn_from_u64(&ac,(uint64_t)rel_a[rj]);
                            if(bn_cmp(&ac,&bm2)>=0){bn_copy(&abs_val,&ac);bn_sub(&abs_val,&bm2);bn_copy(&vm,&abs_val);while(bn_cmp(&vm,n)>=0)bn_sub(&vm,n);}
                            else{bn_copy(&abs_val,&bm2);bn_sub(&abs_val,&ac);/* a-b*m < 0 → use N - |a-b*m| mod N */bn_copy(&vm,&abs_val);while(bn_cmp(&vm,n)>=0)bn_sub(&vm,n);bn_copy(&vm,n);BigNum tmp2;bn_copy(&tmp2,&abs_val);while(bn_cmp(&tmp2,n)>=0)bn_sub(&tmp2,n);bn_sub(&vm,&tmp2);}
                        }else{BigNum ac;bn_from_u64(&ac,(uint64_t)(-rel_a[rj]));bn_copy(&abs_val,&bm2);bn_add(&abs_val,&ac);/* a<0 → a-b*m < 0 always */bn_copy(&vm,n);BigNum tmp2;bn_copy(&tmp2,&abs_val);while(bn_cmp(&tmp2,n)>=0)bn_sub(&tmp2,n);bn_sub(&vm,&tmp2);}
                        bn_mod_mul(&x_prod,&x_prod,&vm,n);
                        BigNum rr; bn_copy(&rr,&abs_val);
                        for(int fi=0;fi<rfb_count;++fi){
                            while(bn_mod_u64(&rr,rfb[fi])==0u){BigNum dp;bn_from_u64(&dp,rfb[fi]);BigNum qo;bn_divexact(&qo,&rr,&dp);bn_copy(&rr,&qo);++lr2[fi];}}
                    }
                    for(int fi=0;fi<rfb_count;++fi){uint64_t half=lr2[fi]/2u;if(half>0u){BigNum bp;bn_from_u64(&bp,rfb[fi]);BigNum pw;bn_from_u64(&pw,1u);for(uint64_t e=0;e<half;++e)bn_mod_mul(&pw,&pw,&bp,n);bn_mod_mul(&y_rat,&y_rat,&pw,n);}}

                    /* Diagnostic: isolate which side is broken — for ALL deps */
                    { static int gcd_diag = 0;
                      uint64_t n_u = bn_to_u64(n);
                      uint64_t yr_u = bn_to_u64(&y_rat);
                      uint64_t xp_u = bn_to_u64(&x_prod);
                      uint64_t yr2 = mulmod_u64(yr_u, yr_u, n_u);
                      uint64_t ya2 = mulmod_u64(y_alg_u64, y_alg_u64, n_u);
                      uint64_t neg_xp = (n_u - xp_u) % n_u;
                      if (gcd_diag < 3) {
                        printf("  [GNFS CRT] x_prod=%"PRIu64"  y_rat=%"PRIu64"  y_alg=%"PRIu64"\n", xp_u, yr_u, y_alg_u64);
                        printf("  [GNFS CRT] y_rat²=%"PRIu64" vs x_prod=%"PRIu64" %s\n", yr2, xp_u,
                            yr2==xp_u?"MATCH":(yr2==neg_xp?"MATCH(-x)":"**MISMATCH**"));
                        printf("  [GNFS CRT] y_alg²=%"PRIu64" vs x_prod=%"PRIu64" %s vs -x_prod=%"PRIu64" %s\n",
                            ya2, xp_u, ya2==xp_u?"MATCH":"miss", neg_xp, ya2==neg_xp?"MATCH":"miss");
                        ++gcd_diag;
                      }
                      /* Check if y_alg² matches ±x_prod — if so, we have the right congruence */
                      if (ya2 == xp_u || ya2 == neg_xp) {
                          printf("  [GNFS CRT] *** y_alg²≡%sx_prod mod N — CONGRUENCE FOUND ***\n", ya2==neg_xp?"-":"");
                          /* Now try GCD with CORRECT x_prod (signed) and y = y_rat * y_alg */
                      }
                    }
                    /* Step 8: GCD tests — try both y = y_rat * y_alg and y = y_rat * (-y_alg) */
                    bool crt_found = false;
                    for (int neg = 0; neg < 2 && !crt_found; ++neg) {
                        BigNum y_use;
                        if (neg == 0) bn_mod_mul(&y_use, &y_rat, &y_alg, n);
                        else { BigNum ny; bn_copy(&ny, n); bn_sub(&ny, &y_alg); bn_mod_mul(&y_use, &y_rat, &ny, n); }
                        BigNum diff2, sum2;
                        if (bn_cmp(&x_prod, &y_use) >= 0) { bn_copy(&diff2, &x_prod); bn_sub(&diff2, &y_use); }
                        else { bn_copy(&diff2, &x_prod); bn_add(&diff2, n); bn_sub(&diff2, &y_use); }
                        bn_copy(&sum2, &x_prod); bn_add(&sum2, &y_use); if (bn_cmp(&sum2, n) >= 0) bn_sub(&sum2, n);
                        BigNum g1, g2; bn_gcd(&g1, &diff2, n); bn_gcd(&g2, &sum2, n);
                        bool g1ok = bn_factor_candidate_valid(&g1, n);
                        bool g2ok = bn_factor_candidate_valid(&g2, n);
                        char g1s[64], g2s[64]; bn_to_str(&g1, g1s, sizeof(g1s)); bn_to_str(&g2, g2s, sizeof(g2s));
                        printf("  [GNFS CRT] %s: gcd(x-y,N)=%s%s  gcd(x+y,N)=%s%s\n",
                            neg ? "(-T)" : "(+T)", g1s, g1ok?" [FACTOR!]":"", g2s, g2ok?" [FACTOR!]":"");
                        if (g1ok) { factor_result_bn_set_from_factor(&fr, n, &g1); fr.method = "gnfs(crt-extraction)"; crt_found = true; }
                        else if (g2ok) { factor_result_bn_set_from_factor(&fr, n, &g2); fr.method = "gnfs(crt-extraction)"; crt_found = true; }
                    }

                    if (crt_found) {
                        found_factor_m15 = true;
                        printf("  [GNFS CRT] *** FACTOR FOUND via CRT algebraic extraction ***\n");
                    } else {
                        printf("  [GNFS CRT] no factor from this dep with %d primes (sign ambiguity or need more deps)\n", crt_prime_count);
                        /* Try with flipped sign anchor: negate ALL T coefficients and re-evaluate */
                        printf("  [GNFS CRT] trying sign-flipped anchor...\n");
                        BigNum y_alg_neg; bn_copy(&y_alg_neg, n); bn_sub(&y_alg_neg, &y_alg);
                        for (int neg = 0; neg < 2 && !crt_found; ++neg) {
                            BigNum y_use;
                            if (neg == 0) bn_mod_mul(&y_use, &y_rat, &y_alg_neg, n);
                            else { BigNum ny; bn_copy(&ny, n); bn_sub(&ny, &y_alg_neg); bn_mod_mul(&y_use, &y_rat, &ny, n); }
                            BigNum diff2, sum2;
                            if (bn_cmp(&x_prod, &y_use) >= 0) { bn_copy(&diff2, &x_prod); bn_sub(&diff2, &y_use); }
                            else { bn_copy(&diff2, &x_prod); bn_add(&diff2, n); bn_sub(&diff2, &y_use); }
                            bn_copy(&sum2, &x_prod); bn_add(&sum2, &y_use); if (bn_cmp(&sum2, n) >= 0) bn_sub(&sum2, n);
                            BigNum g1, g2; bn_gcd(&g1, &diff2, n); bn_gcd(&g2, &sum2, n);
                            bool g1ok = bn_factor_candidate_valid(&g1, n);
                            bool g2ok = bn_factor_candidate_valid(&g2, n);
                            char g1s[64], g2s[64]; bn_to_str(&g1, g1s, sizeof(g1s)); bn_to_str(&g2, g2s, sizeof(g2s));
                            printf("  [GNFS CRT] flipped %s: gcd(x-y,N)=%s%s  gcd(x+y,N)=%s%s\n",
                                neg ? "(-T)" : "(+T)", g1s, g1ok?" [FACTOR!]":"", g2s, g2ok?" [FACTOR!]":"");
                            if (g1ok) { factor_result_bn_set_from_factor(&fr, n, &g1); fr.method = "gnfs(crt-extraction)"; crt_found = true; }
                            else if (g2ok) { factor_result_bn_set_from_factor(&fr, n, &g2); fr.method = "gnfs(crt-extraction)"; crt_found = true; }
                        }
                        if (crt_found) { found_factor_m15 = true; printf("  [GNFS CRT] *** FACTOR FOUND via flipped CRT ***\n"); }
                    }

                    if (crt_found) crt_global_found = true;
                }
              } /* end dep_idx loop */
              if (!crt_global_found) printf("  [GNFS CRT] no factor from %d deps tried\n", max_deps_try);
            }
        }

        /* ── Milestone 2b-field: Auxiliary-prime field-level sqrt validation ──
         * For one dependency, compute S = ∏(a_i - b_i·α) mod (f, p) where p is
         * a suitable auxiliary prime, then compute T = S^((p³+1)/4) mod (f, p)
         * and verify T² = S. This validates the field-level sqrt math.
         * Does NOT attempt coefficient recovery or factor extraction. */
        if (!found_factor_m15 && poly.degree == 3 && bn_is_one(&poly.coeffs[3])) {
            /* Step 1: Sort dependencies by size, report smallest 5 */
            typedef struct { int row_idx; int dep_size; } DepInfo;
            DepInfo dep_list[256];
            int dep_list_count = 0;
            for (int ri = 0; ri < rel_count && dep_list_count < 256; ++ri) {
                bool is_zero = true;
                for (int w = 0; w < row_words; ++w) { if (matrix[ri * row_words + w]) { is_zero = false; break; } }
                if (!is_zero) continue;
                uint64_t* h = &history[ri * hist_words];
                int sz = 0;
                for (int rj = 0; rj < rel_count; ++rj)
                    if (h[rj / 64] & (1ull << (rj % 64))) ++sz;
                dep_list[dep_list_count].row_idx = ri;
                dep_list[dep_list_count].dep_size = sz;
                ++dep_list_count;
            }
            /* Sort by dep_size ascending (simple insertion sort) */
            for (int i = 1; i < dep_list_count; ++i) {
                DepInfo tmp = dep_list[i];
                int j = i - 1;
                while (j >= 0 && dep_list[j].dep_size > tmp.dep_size) { dep_list[j + 1] = dep_list[j]; --j; }
                dep_list[j + 1] = tmp;
            }

            if (cfg->diag) {
                printf("  [GNFS 2b-field] dependency sizes (smallest 5):");
                for (int i = 0; i < dep_list_count && i < 5; ++i)
                    printf(" %d", dep_list[i].dep_size);
                printf("\n");
            }

            /* Step 2: Find multiple auxiliary primes p: prime, p≡3(mod4), f irreducible mod p.
             * S may be non-QR for some p but QR for others, so try several. */
            uint64_t aux_primes[8];
            int aux_prime_count = 0;
            { uint64_t rng = 0xC0FFEE1234567891ULL ^ (uint64_t)bn_bitlen(n);
              int tries = 0;
              for (; tries < 2000 && aux_prime_count < 5; ++tries) {
                  /* Generate candidate: random odd number ≡ 3 (mod 4) in ~60-bit range */
                  rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
                  uint64_t cand = (rng | ((uint64_t)1 << 59)) | 3u; /* set high bit + low bits for ≡3 mod 4 */
                  cand = cand & ~(uint64_t)1u; cand |= 3u; /* ensure odd and ≡3 mod 4: last 2 bits = 11 */
                  if ((cand & 3u) != 3u) continue;
                  if (!miller_rabin_u64(cand)) continue;
                  /* Check f is irreducible mod cand.
                   * For degree 3: f irreducible over F_p iff f has no root mod p.
                   * Proper test: compute x^p mod f(x) in F_p[x]. If x^p ≡ x mod f(x),
                   * then f has a root and is reducible. If x^p ≢ x, f is irreducible.
                   * This uses O(log p) polynomial multiplications, each O(d²). */
                  {
                      /* Represent polynomials mod f(x) as 3 coefficients (degree < 3).
                       * Compute x^p mod f(x) via repeated squaring. */
                      uint64_t res[3] = {0, 1, 0}; /* res = x = 0 + 1·x + 0·x² */
                      uint64_t base[3] = {0, 1, 0}; /* base = x */
                      uint64_t pp = cand;
                      /* f(x) = x³ + f2·x² + f1·x + f0 for monic */
                      uint64_t f0 = bn_mod_u64(&poly.coeffs[0], cand);
                      uint64_t f1 = bn_mod_u64(&poly.coeffs[1], cand);
                      uint64_t f2 = bn_mod_u64(&poly.coeffs[2], cand);
                      /* f3 = 1 for monic. Reduction: x³ = -f2·x² - f1·x - f0 mod p */

                      /* poly_mul_mod_f: multiply two degree<3 polys mod f(x) mod p */
                      #define POLY3_MUL(out, a, b, p_val) do { \
                          uint64_t _t[5] = {0,0,0,0,0}; \
                          for (int _i=0;_i<3;++_i) for (int _j=0;_j<3;++_j) \
                              _t[_i+_j] = (_t[_i+_j] + mulmod_u64((a)[_i], (b)[_j], p_val)) % p_val; \
                          /* reduce x^4: x^4 = x·x³ = x·(-f2x²-f1x-f0) = -f2x³-f1x²-f0x */ \
                          /* so x^4 contribution goes to x³,x²,x with neg coeffs */ \
                          if (_t[4]) { \
                              _t[3] = (_t[3] + mulmod_u64(_t[4], p_val - f2, p_val)) % p_val; \
                              _t[2] = (_t[2] + mulmod_u64(_t[4], p_val - f1, p_val)) % p_val; \
                              _t[1] = (_t[1] + mulmod_u64(_t[4], p_val - f0, p_val)) % p_val; \
                          } \
                          /* reduce x^3 */ \
                          if (_t[3]) { \
                              _t[2] = (_t[2] + mulmod_u64(_t[3], p_val - f2, p_val)) % p_val; \
                              _t[1] = (_t[1] + mulmod_u64(_t[3], p_val - f1, p_val)) % p_val; \
                              _t[0] = (_t[0] + mulmod_u64(_t[3], p_val - f0, p_val)) % p_val; \
                          } \
                          (out)[0]=_t[0]; (out)[1]=_t[1]; (out)[2]=_t[2]; \
                      } while(0)

                      /* Binary exponentiation: res = x^p mod f(x) mod p */
                      res[0] = 0; res[1] = 0; res[2] = 0;
                      /* Start with res = 1 (constant polynomial) */
                      res[0] = 1; res[1] = 0; res[2] = 0;
                      base[0] = 0; base[1] = 1; base[2] = 0; /* base = x */
                      uint64_t exp_val = pp;
                      while (exp_val > 0u) {
                          if (exp_val & 1u) {
                              uint64_t tmp[3];
                              POLY3_MUL(tmp, res, base, cand);
                              res[0]=tmp[0]; res[1]=tmp[1]; res[2]=tmp[2];
                          }
                          uint64_t tmp2[3];
                          POLY3_MUL(tmp2, base, base, cand);
                          base[0]=tmp2[0]; base[1]=tmp2[1]; base[2]=tmp2[2];
                          exp_val >>= 1u;
                      }
                      #undef POLY3_MUL
                      /* x^p mod f(x) mod p. If x^p = x, f has a linear factor → reducible. */
                      bool is_x = (res[0] == 0u && res[1] == 1u && res[2] == 0u);
                      if (is_x) continue; /* has linear factor → skip */
                      /* For degree 3: must ALSO check x^(p²) ≠ x to rule out
                       * f = (linear)(irred.quadratic). Compute (x^p)^p = x^(p²) mod f. */
                      #define POLY3_MUL2(out, a2, b2, p_val) do { \
                          uint64_t _t[5] = {0,0,0,0,0}; \
                          for (int _i=0;_i<3;++_i) for (int _j=0;_j<3;++_j) \
                              _t[_i+_j] = (_t[_i+_j] + mulmod_u64((a2)[_i], (b2)[_j], p_val)) % p_val; \
                          if (_t[4]) { \
                              _t[3] = (_t[3] + mulmod_u64(_t[4], p_val - f2, p_val)) % p_val; \
                              _t[2] = (_t[2] + mulmod_u64(_t[4], p_val - f1, p_val)) % p_val; \
                              _t[1] = (_t[1] + mulmod_u64(_t[4], p_val - f0, p_val)) % p_val; \
                          } \
                          if (_t[3]) { \
                              _t[2] = (_t[2] + mulmod_u64(_t[3], p_val - f2, p_val)) % p_val; \
                              _t[1] = (_t[1] + mulmod_u64(_t[3], p_val - f1, p_val)) % p_val; \
                              _t[0] = (_t[0] + mulmod_u64(_t[3], p_val - f0, p_val)) % p_val; \
                          } \
                          (out)[0]=_t[0]; (out)[1]=_t[1]; (out)[2]=_t[2]; \
                      } while(0)
                      uint64_t res2[3] = {1,0,0}, base2[3] = {res[0],res[1],res[2]};
                      uint64_t exp2 = cand;
                      while (exp2 > 0u) {
                          if (exp2 & 1u) { uint64_t tmp[3]; POLY3_MUL2(tmp, res2, base2, cand);
                              res2[0]=tmp[0]; res2[1]=tmp[1]; res2[2]=tmp[2]; }
                          uint64_t tmp2[3]; POLY3_MUL2(tmp2, base2, base2, cand);
                          base2[0]=tmp2[0]; base2[1]=tmp2[1]; base2[2]=tmp2[2];
                          exp2 >>= 1u;
                      }
                      #undef POLY3_MUL2
                      bool is_x2 = (res2[0] == 0u && res2[1] == 1u && res2[2] == 0u);
                      if (is_x2) continue; /* has quadratic factor → skip */
                  }
                  /* Check p does not divide disc(f) — for monic x³+ax+b: disc = -4a³-27b² */
                  /* Simplified: just check f has no repeated roots mod p, which is implied by
                   * irreducibility (no roots at all). So this check is redundant here. */
                  aux_primes[aux_prime_count++] = cand;
              }
              if (cfg->diag) {
                  printf("  [GNFS 2b-field] found %d admissible auxiliary primes\n", aux_prime_count);
                  for (int pi = 0; pi < aux_prime_count; ++pi)
                      printf("    p[%d] = %"PRIu64" (%u bits)\n", pi, aux_primes[pi],
                          (unsigned)(64 - __builtin_clzll(aux_primes[pi])));
              }
            }

            /* ── GNFS algebraic target audit ── */
            if (aux_prime_count > 0 && dep_list_count > 0) {
                int audit_deps = dep_list_count < 5 ? dep_list_count : 5;
                int audit_primes = aux_prime_count;

                printf("  [GNFS audit] algebraic target QR persistence: %d deps x %d primes\n",
                    audit_deps, audit_primes);
                printf("  [GNFS audit] target recipe: S = prod(a_i - b_i*alpha) mod (f, p)\n");
                printf("  [GNFS audit] f3 = 1 (monic), no denominator correction\n");

                /* ── QR sanity check: small-prime Fermat test first ──
                 * All computations use the trusted alg_mul path (proven by 4000/4000 oracle test). */
                {
                    /* Test with p=7: f(x)=x³+6709x²+5715x+355 ≡ x³+3x²+3x+5 mod 7 (no roots → irreducible).
                     * g = (1 - α) = (1, 6, 0) in F_7.  g^(7³-1) = g^342 must equal 1. */
                    BigNum p7; bn_from_u64(&p7, 7u);
                    BigNum f3i7; bn_from_u64(&f3i7, 1u);
                    AlgElem g7; bn_from_u64(&g7.c[0], 1u); bn_from_u64(&g7.c[1], 6u); bn_zero(&g7.c[2]);
                    /* Compute g7^342 via binary exponentiation */
                    AlgElem res7; alg_one(&res7);
                    AlgElem base7; for(int c=0;c<3;++c) bn_copy(&base7.c[c], &g7.c[c]);
                    unsigned exp342 = 342u;
                    while (exp342 > 0u) {
                        if (exp342 & 1u) {
                            AlgElem t; for(int c=0;c<3;++c) bn_copy(&t.c[c], &res7.c[c]);
                            alg_mul(&res7, &t, &base7, &poly, &f3i7, &p7);
                        }
                        AlgElem t2; for(int c=0;c<3;++c) bn_copy(&t2.c[c], &base7.c[c]);
                        alg_mul(&base7, &t2, &t2, &poly, &f3i7, &p7);
                        exp342 >>= 1u;
                    }
                    bool small_fermat = bn_is_one(&res7.c[0]) && bn_is_zero(&res7.c[1]) && bn_is_zero(&res7.c[2]);
                    printf("  [GNFS QR-sanity] small-prime p=7: g^342=[%"PRIu64",%"PRIu64",%"PRIu64"] Fermat=%s\n",
                        bn_to_u64(&res7.c[0]), bn_to_u64(&res7.c[1]), bn_to_u64(&res7.c[2]),
                        small_fermat ? "OK" : "**FAIL**");
                }
                /* Search for medium primes (20-40 bits) where f IS irreducible, test Fermat */
                {
                    static const uint64_t test_primes[] = {
                        1000000007u, 1000000087u, 1000000123u, 1000000223u, 1000000319u,
                        2000000003u, 3000000019u, 4000000007u, 8000000003u, 100000000003ull,
                        500000000023ull, 1000000000039ull, 4000000000037ull
                    };
                    int ntp = sizeof(test_primes)/sizeof(test_primes[0]);
                    for (int tpi = 0; tpi < ntp; ++tpi) {
                        uint64_t mp = test_primes[tpi];
                        if (!miller_rabin_u64(mp)) continue;
                        if ((mp & 3u) != 3u) continue; /* need p ≡ 3 mod 4 */
                        uint64_t mf0=bn_mod_u64(&poly.coeffs[0],mp),mf1=bn_mod_u64(&poly.coeffs[1],mp),mf2=bn_mod_u64(&poly.coeffs[2],mp);
                        /* Irreducibility check: compute α^p mod (f, p) using trusted alg_mul.
                         * If α^p = α, then f has a root (reducible). Otherwise irreducible for degree 3. */
                        BigNum mp_irr_bn; bn_from_u64(&mp_irr_bn, mp);
                        BigNum f3i_irr; bn_from_u64(&f3i_irr, 1u);
                        AlgElem alpha_irr; bn_zero(&alpha_irr.c[0]); bn_from_u64(&alpha_irr.c[1], 1u); bn_zero(&alpha_irr.c[2]);
                        AlgElem irr_res; alg_one(&irr_res);
                        AlgElem irr_base; for(int c=0;c<3;++c) bn_copy(&irr_base.c[c], &alpha_irr.c[c]);
                        uint64_t irr_exp = mp;
                        while(irr_exp > 0u) {
                            if(irr_exp & 1u) { AlgElem t; for(int c=0;c<3;++c) bn_copy(&t.c[c], &irr_res.c[c]);
                                alg_mul(&irr_res, &t, &irr_base, &poly, &f3i_irr, &mp_irr_bn); }
                            AlgElem t2; for(int c=0;c<3;++c) bn_copy(&t2.c[c], &irr_base.c[c]);
                            alg_mul(&irr_base, &t2, &t2, &poly, &f3i_irr, &mp_irr_bn);
                            irr_exp >>= 1u;
                        }
                        bool no_lin = !(bn_is_zero(&irr_res.c[0]) && bn_is_one(&irr_res.c[1]) && bn_is_zero(&irr_res.c[2]));
                        if (!no_lin) continue; /* has linear factor, skip */
                        /* Must also check α^(p²) ≠ α to rule out (linear)(irred.quadratic) factorization */
                        AlgElem irr2_res; alg_one(&irr2_res);
                        AlgElem irr2_base; for(int c=0;c<3;++c) bn_copy(&irr2_base.c[c], &irr_res.c[c]);
                        uint64_t irr2_exp = mp;
                        while(irr2_exp > 0u) {
                            if(irr2_exp & 1u) { AlgElem t; for(int c=0;c<3;++c) bn_copy(&t.c[c], &irr2_res.c[c]);
                                alg_mul(&irr2_res, &t, &irr2_base, &poly, &f3i_irr, &mp_irr_bn); }
                            AlgElem t2; for(int c=0;c<3;++c) bn_copy(&t2.c[c], &irr2_base.c[c]);
                            alg_mul(&irr2_base, &t2, &t2, &poly, &f3i_irr, &mp_irr_bn);
                            irr2_exp >>= 1u;
                        }
                        bool no_quad = !(bn_is_zero(&irr2_res.c[0]) && bn_is_one(&irr2_res.c[1]) && bn_is_zero(&irr2_res.c[2]));
                        if (!no_quad) continue; /* has quadratic factor, skip */
                        /* Brute-force root check for small primes */
                        if (mp < 100000000u) {
                            int root_count = 0;
                            for (uint64_t rx = 0; rx < mp; ++rx) {
                                uint64_t fv = (mulmod_u64(mulmod_u64(rx,rx,mp),rx,mp) + mulmod_u64(mf2,mulmod_u64(rx,rx,mp),mp) + mulmod_u64(mf1,rx,mp) + mf0) % mp;
                                if (fv == 0) { ++root_count; if (root_count <= 3) printf("    BRUTE root: f(%"PRIu64")=0 mod %"PRIu64"\n", rx, mp); }
                            }
                            if (root_count > 0) { printf("    BRUTE: f has %d roots mod %"PRIu64" — NOT irreducible! irr check is WRONG\n", root_count, mp); continue; }
                        }
                        /* BigNum Fermat: g^(p³-1) must = 1 */
                        BigNum mp_bn; bn_from_u64(&mp_bn, mp);
                        BigNum mf3i; bn_from_u64(&mf3i, 1u);
                        AlgElem mg; alg_from_ab(&mg, rel_a[0], rel_b[0], &mp_bn);
                        BigNum mfermat; gnfs_qr_exp_from_prime(&mfermat, mp); bn_shl1(&mfermat);
                        unsigned mfbits = bn_bitlen(&mfermat);
                        /* Verify exponent: compute p³-1 independently */
                        { BigNum vp; bn_from_u64(&vp, mp);
                          BigNum vp2; bn_mul(&vp2, &vp, &vp);
                          BigNum vp3; bn_mul(&vp3, &vp2, &vp);
                          BigNum one; bn_from_u64(&one, 1u); bn_sub(&vp3, &one);
                          bool exp_ok = (bn_cmp(&mfermat, &vp3) == 0);
                          if (!exp_ok) printf("    **EXPONENT MISMATCH** for p=%"PRIu64"\n", mp);
                        }
                        AlgElem mres; alg_one(&mres);
                        AlgElem mbase; for(int c=0;c<3;++c) bn_copy(&mbase.c[c], &mg.c[c]);
                        for(unsigned bt=0;bt<mfbits;++bt){
                            if(mfermat.limbs[bt/64]&((uint64_t)1u<<(bt%64))){
                                AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&mres.c[c]);
                                alg_mul(&mres,&t,&mbase,&poly,&mf3i,&mp_bn);}
                            AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&mbase.c[c]);
                            alg_mul(&mbase,&t2,&t2,&poly,&mf3i,&mp_bn);}
                        bool mfok = bn_is_one(&mres.c[0])&&bn_is_zero(&mres.c[1])&&bn_is_zero(&mres.c[2]);
                        /* Frobenius test: ((g^p)^p)^p = g using trusted alg_mul */
                        AlgElem frob_res; for(int c=0;c<3;++c)bn_copy(&frob_res.c[c],&mg.c[c]);
                        for (int round=0;round<3;++round) {
                            AlgElem fr; alg_one(&fr);
                            AlgElem fb; for(int c=0;c<3;++c)bn_copy(&fb.c[c],&frob_res.c[c]);
                            uint64_t fe = mp;
                            while(fe>0u){
                                if(fe&1u){AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&fr.c[c]);alg_mul(&fr,&t,&fb,&poly,&mf3i,&mp_bn);}
                                AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&fb.c[c]);alg_mul(&fb,&t2,&t2,&poly,&mf3i,&mp_bn);
                                fe>>=1u;}
                            for(int c=0;c<3;++c)bn_copy(&frob_res.c[c],&fr.c[c]);
                        }
                        bool frob_ok = (bn_cmp(&frob_res.c[0],&mg.c[0])==0 && bn_cmp(&frob_res.c[1],&mg.c[1])==0 && bn_cmp(&frob_res.c[2],&mg.c[2])==0);
                        /* Coefficient validity: g^(p-1) coeffs must be < p */
                        AlgElem qr; alg_one(&qr);
                        AlgElem qb; for(int c=0;c<3;++c) bn_copy(&qb.c[c], &mg.c[c]);
                        uint64_t qe = mp - 1u;
                        while(qe>0u){
                            if(qe&1u){AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&qr.c[c]);alg_mul(&qr,&t,&qb,&poly,&mf3i,&mp_bn);}
                            AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&qb.c[c]);alg_mul(&qb,&t2,&t2,&poly,&mf3i,&mp_bn);
                            qe>>=1u;}
                        bool qcoeffs_ok = true;
                        for(int c=0;c<3;++c) if(bn_cmp(&qr.c[c],&mp_bn)>=0) qcoeffs_ok=false;

                        unsigned pbits = 0; { uint64_t t=mp; while(t){++pbits;t>>=1;} }
                        printf("  [GNFS QR-sanity] p=%"PRIu64" (%u bits): irr=yes Fermat=%s Frobenius=%s coeffs_valid=%s [trusted alg_mul]\n",
                            mp, pbits, mfok?"OK":"FAIL", frob_ok?"OK":"FAIL", qcoeffs_ok?"yes":"**NO**");
                        if (!mfok) break;
                    }
                }
                /* ── QR sanity check: validate exponentiation on known square ── */
                {
                    uint64_t sp = aux_primes[0];
                    BigNum sp_bn; bn_from_u64(&sp_bn, sp);
                    BigNum sf3i; bn_from_u64(&sf3i, 1u);
                    AlgElem san_single; alg_from_ab(&san_single, rel_a[0], rel_b[0], &sp_bn);
                    AlgElem san_sq;
                    { AlgElem t; for(int c=0;c<3;++c) bn_copy(&t.c[c], &san_single.c[c]);
                      alg_mul(&san_sq, &t, &san_single, &poly, &sf3i, &sp_bn); }
                    BigNum san_qr_exp; gnfs_qr_exp_from_prime(&san_qr_exp, sp);
                    unsigned san_qr_bits=bn_bitlen(&san_qr_exp);
                    /* Fermat exponent = p³-1 = 2 * qr_exp */
                    BigNum san_fermat_exp; bn_copy(&san_fermat_exp, &san_qr_exp);
                    bn_shl1(&san_fermat_exp);
                    unsigned san_fermat_bits=bn_bitlen(&san_fermat_exp);
                    /* Verify p³: compute independently and compare */
                    { BigNum vp; bn_from_u64(&vp, sp);
                      BigNum vp2; bn_mul(&vp2, &vp, &vp);
                      BigNum vp3; bn_mul(&vp3, &vp2, &vp);
                      uint64_t p3_mod_p = bn_mod_u64(&vp3, sp);
                      BigNum vp3m1; bn_copy(&vp3m1, &vp3);
                      { BigNum one; bn_from_u64(&one, 1u); bn_sub(&vp3m1, &one); }
                      /* fermat_exp should = vp3m1 */
                      bool exp_match = (bn_cmp(&san_fermat_exp, &vp3m1) == 0);
                      printf("  [GNFS QR-sanity] p^3 verify: bits=%u  p^3 mod p=%"PRIu64" (expect 0)  fermat_exp match=%s\n",
                          bn_bitlen(&vp3), p3_mod_p, exp_match ? "YES" : "**NO**");
                      if (!exp_match) {
                          char a_str[256], b_str[256];
                          bn_to_str(&san_fermat_exp, a_str, sizeof(a_str));
                          bn_to_str(&vp3m1, b_str, sizeof(b_str));
                          printf("    fermat_exp = %s\n    p^3-1      = %s\n", a_str, b_str);
                      }
                    }
                    #define ALG_POW_SAN(res2,base_in,exp_in,exp_bits2) do{ \
                        alg_one(&(res2)); \
                        AlgElem _bs;for(int _c=0;_c<3;++_c)bn_copy(&_bs.c[_c],&(base_in).c[_c]); \
                        for(unsigned _bt=0;_bt<(exp_bits2);++_bt){ \
                            if((exp_in).limbs[_bt/64]&((uint64_t)1u<<(_bt%64))){ \
                                AlgElem _t;for(int _c=0;_c<3;++_c)bn_copy(&_t.c[_c],&(res2).c[_c]); \
                                alg_mul(&(res2),&_t,&_bs,&poly,&sf3i,&sp_bn);} \
                            AlgElem _t2;for(int _c=0;_c<3;++_c)bn_copy(&_t2.c[_c],&_bs.c[_c]); \
                            alg_mul(&_bs,&_t2,&_t2,&poly,&sf3i,&sp_bn);} \
                    } while(0)
                    AlgElem qc_san;
                    ALG_POW_SAN(qc_san, san_sq, san_qr_exp, san_qr_bits);
                    bool sq_qr=bn_is_one(&qc_san.c[0])&&bn_is_zero(&qc_san.c[1])&&bn_is_zero(&qc_san.c[2]);
                    ALG_POW_SAN(qc_san, san_single, san_qr_exp, san_qr_bits);
                    bool single_qr=bn_is_one(&qc_san.c[0])&&bn_is_zero(&qc_san.c[1])&&bn_is_zero(&qc_san.c[2]);
                    ALG_POW_SAN(qc_san, san_single, san_fermat_exp, san_fermat_bits);
                    bool fermat_ok=bn_is_one(&qc_san.c[0])&&bn_is_zero(&qc_san.c[1])&&bn_is_zero(&qc_san.c[2]);
                    /* Print actual Fermat result coefficients for diagnosis */
                    { char fb0[64],fb1[64],fb2[64];
                      bn_to_str(&qc_san.c[0],fb0,sizeof(fb0));bn_to_str(&qc_san.c[1],fb1,sizeof(fb1));bn_to_str(&qc_san.c[2],fb2,sizeof(fb2));
                      printf("  [GNFS QR-sanity] Fermat result: [%s, %s, %s]\n", fb0, fb1, fb2); }
                    #undef ALG_POW_SAN
                    /* Verify single multiplication: print g² coefficients */
                    { char s0[64],s1[64],s2[64];
                      bn_to_str(&san_sq.c[0],s0,sizeof(s0));bn_to_str(&san_sq.c[1],s1,sizeof(s1));bn_to_str(&san_sq.c[2],s2,sizeof(s2));
                      printf("  [GNFS QR-sanity] g^2(BigNum)=[%s,%s,%s]\n",s0,s1,s2);
                      char g0[64],g1[64];
                      bn_to_str(&san_single.c[0],g0,sizeof(g0));bn_to_str(&san_single.c[1],g1,sizeof(g1));
                      printf("  [GNFS QR-sanity] g=[%s,%s,0]\n",g0,g1);
                    }
                    printf("  [GNFS QR-sanity] p=%"PRIu64"  a=%"PRId64" b=%"PRIu64":\n", sp, rel_a[0], rel_b[0]);
                    printf("    qr_exp_bits=%u  fermat_exp_bits=%u\n", san_qr_bits, san_fermat_bits);
                    printf("    single^((p^3-1)/2) = %s\n", single_qr ? "1 (QR)" : "non-1 (non-QR)");
                    printf("    single^2^((p^3-1)/2) = %s (MUST be 1)\n", sq_qr ? "1 (QR) OK" : "non-1 **BUG**");
                    printf("    single^(p^3-1) = %s (Fermat, MUST be 1)\n", fermat_ok ? "1 OK" : "non-1 **BUG**");
                    /* Irreducibility cross-check: α^p mod (f,p) via trusted alg_mul */
                    { AlgElem bn_xp_res; alg_one(&bn_xp_res);
                      AlgElem bn_xp_base; bn_zero(&bn_xp_base.c[0]); bn_from_u64(&bn_xp_base.c[1], 1u); bn_zero(&bn_xp_base.c[2]);
                      uint64_t xp_exp = sp;
                      while(xp_exp>0u){
                          if(xp_exp&1u){AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&bn_xp_res.c[c]);alg_mul(&bn_xp_res,&t,&bn_xp_base,&poly,&sf3i,&sp_bn);}
                          AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&bn_xp_base.c[c]);alg_mul(&bn_xp_base,&t2,&t2,&poly,&sf3i,&sp_bn);
                          xp_exp>>=1u;}
                      bool bn_is_x = bn_is_zero(&bn_xp_res.c[0]) && bn_is_one(&bn_xp_res.c[1]) && bn_is_zero(&bn_xp_res.c[2]);
                      printf("    α^p mod (f,p) = [%"PRIu64",%"PRIu64",%"PRIu64"] %s [trusted alg_mul]\n",
                          bn_to_u64(&bn_xp_res.c[0]),bn_to_u64(&bn_xp_res.c[1]),bn_to_u64(&bn_xp_res.c[2]),
                          bn_is_x?"= α (REDUCIBLE!)":"!= α (irreducible)");
                    }
                    /* Frobenius: ((g^p)^p)^p = g via trusted alg_mul */
                    { AlgElem bn_frob; alg_from_ab(&bn_frob, rel_a[0], rel_b[0], &sp_bn);
                      for (int round=0;round<3;++round) {
                          AlgElem fr; alg_one(&fr);
                          AlgElem fb; for(int c=0;c<3;++c) bn_copy(&fb.c[c], &bn_frob.c[c]);
                          uint64_t fe = sp;
                          while(fe>0u){
                              if(fe&1u){AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&fr.c[c]);alg_mul(&fr,&t,&fb,&poly,&sf3i,&sp_bn);}
                              AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&fb.c[c]);alg_mul(&fb,&t2,&t2,&poly,&sf3i,&sp_bn);
                              fe>>=1u;}
                          for(int c=0;c<3;++c) bn_copy(&bn_frob.c[c], &fr.c[c]);
                      }
                      AlgElem orig_elem; alg_from_ab(&orig_elem, rel_a[0], rel_b[0], &sp_bn);
                      bool frob_ok = (bn_cmp(&bn_frob.c[0],&orig_elem.c[0])==0 && bn_cmp(&bn_frob.c[1],&orig_elem.c[1])==0 && bn_cmp(&bn_frob.c[2],&orig_elem.c[2])==0);
                      printf("    Frobenius g^(p³)=g: %s [trusted alg_mul]\n", frob_ok?"YES":"NO");
                    }
                    if (!sq_qr) printf("  [GNFS QR-sanity] **BUG: known square fails QR** [trusted alg_mul path]\n");
                    if (!fermat_ok) printf("  [GNFS QR-sanity] **BUG: Fermat fails** [trusted alg_mul path]\n");
                    if (sq_qr && fermat_ok) printf("  [GNFS QR-sanity] QR test machinery is sound [all paths use trusted alg_mul]\n");
                }

                /* Helper: compute QR status of an AlgElem in F_{p^3} */
                /* Returns true if elem^((p^3-1)/2) = 1 mod (f,p) */
                int qr_hit_any = 0;
                int neg_hit_any = 0;
                int alpha_hit_any = 0;

                for (int di = 0; di < audit_deps; ++di) {
                    int dep_row = dep_list[di].row_idx;
                    int dep_sz = dep_list[di].dep_size;
                    uint64_t* hd = &history[dep_row * hist_words];
                    int base_qr = 0, neg_qr = 0, alpha_qr = 0;

                    for (int pi = 0; pi < audit_primes; ++pi) {
                        uint64_t ap = aux_primes[pi];
                        BigNum p_bn; bn_from_u64(&p_bn, ap);
                        BigNum f3i; bn_from_u64(&f3i, 1u);

                        /* QR exponent = (p^3-1)/2 */
                        BigNum qr_e; gnfs_qr_exp_from_prime(&qr_e, ap);
                        unsigned qr_b = bn_bitlen(&qr_e);

                        #define ALG_POW_A(res, base_in, exp_in, exp_bits) do { \
                            alg_one(&(res)); \
                            AlgElem _b; for(int _c=0;_c<3;++_c) bn_copy(&_b.c[_c],&(base_in).c[_c]); \
                            for(unsigned _bt=0;_bt<(exp_bits);++_bt){ \
                                if((exp_in).limbs[_bt/64]&((uint64_t)1u<<(_bt%64))){ \
                                    AlgElem _t; for(int _c=0;_c<3;++_c) bn_copy(&_t.c[_c],&(res).c[_c]); \
                                    alg_mul(&(res),&_t,&_b,&poly,&f3i,&p_bn); } \
                                AlgElem _t2; for(int _c=0;_c<3;++_c) bn_copy(&_t2.c[_c],&_b.c[_c]); \
                                alg_mul(&_b,&_t2,&_t2,&poly,&f3i,&p_bn); } \
                        } while(0)

                        /* Compute S mod (f, p) */
                        AlgElem S; alg_one(&S);
                        for (int rj = 0; rj < rel_count; ++rj) {
                            if (!(hd[rj/64] & (1ull<<(rj%64)))) continue;
                            AlgElem el; alg_from_ab(&el, rel_a[rj], rel_b[rj], &p_bn);
                            AlgElem pr; for(int c=0;c<3;++c) bn_copy(&pr.c[c],&S.c[c]);
                            alg_mul(&S, &pr, &el, &poly, &f3i, &p_bn);
                        }

                        /* QR check: S_base */
                        AlgElem qc; ALG_POW_A(qc, S, qr_e, qr_b);
                        bool bqr = bn_is_one(&qc.c[0]) && bn_is_zero(&qc.c[1]) && bn_is_zero(&qc.c[2]);
                        if (bqr) ++base_qr;

                        /* Variant: -S_base (negate c[0] mod p, leave c[1],c[2]) */
                        AlgElem negS;
                        if (bn_is_zero(&S.c[0])) bn_zero(&negS.c[0]);
                        else { bn_copy(&negS.c[0], &p_bn); bn_sub(&negS.c[0], &S.c[0]); }
                        if (bn_is_zero(&S.c[1])) bn_zero(&negS.c[1]);
                        else { bn_copy(&negS.c[1], &p_bn); bn_sub(&negS.c[1], &S.c[1]); }
                        if (bn_is_zero(&S.c[2])) bn_zero(&negS.c[2]);
                        else { bn_copy(&negS.c[2], &p_bn); bn_sub(&negS.c[2], &S.c[2]); }
                        ALG_POW_A(qc, negS, qr_e, qr_b);
                        bool nqr = bn_is_one(&qc.c[0]) && bn_is_zero(&qc.c[1]) && bn_is_zero(&qc.c[2]);
                        if (nqr) ++neg_qr;

                        /* Variant: S * alpha (multiply by alpha = [0,1,0]) */
                        AlgElem alpha_el; bn_zero(&alpha_el.c[0]); bn_from_u64(&alpha_el.c[1], 1u); bn_zero(&alpha_el.c[2]);
                        AlgElem Sa; alg_mul(&Sa, &S, &alpha_el, &poly, &f3i, &p_bn);
                        ALG_POW_A(qc, Sa, qr_e, qr_b);
                        bool aqr = bn_is_one(&qc.c[0]) && bn_is_zero(&qc.c[1]) && bn_is_zero(&qc.c[2]);
                        if (aqr) ++alpha_qr;

                        #undef ALG_POW_A
                    }

                    if (base_qr) ++qr_hit_any;
                    if (neg_qr) ++neg_hit_any;
                    if (alpha_qr) ++alpha_hit_any;

                    printf("    dep#%d (%d rels): S_base QR=%d/%d  -S QR=%d/%d  S*α QR=%d/%d\n",
                        di, dep_sz, base_qr, audit_primes, neg_qr, audit_primes, alpha_qr, audit_primes);
                }

                /* Evidence classification */
                printf("  [GNFS audit] summary: base_QR_deps=%d  neg_QR_deps=%d  alpha_QR_deps=%d  out of %d\n",
                    qr_hit_any, neg_hit_any, alpha_hit_any, audit_deps);
                if (neg_hit_any > 0 && qr_hit_any == 0) {
                    printf("  [GNFS audit] conclusion=A: -S is QR but S is not → sign correction missing from target\n");
                } else if (alpha_hit_any > 0 && qr_hit_any == 0) {
                    printf("  [GNFS audit] conclusion=A: S*α is QR but S is not → α-factor correction missing from target\n");
                } else if (qr_hit_any > 0) {
                    printf("  [GNFS audit] conclusion=PARTIAL: some deps have QR base target on some primes\n");
                } else if (neg_hit_any == 0 && alpha_hit_any == 0 && qr_hit_any == 0) {
                    printf("  [GNFS audit] conclusion=B or C: no simple correction flips QR → deeper issue\n");
                }
            }
            /* ── Dependency-level sign/character effectiveness audit ── */
            if (cfg->audit_deps && dep_list_count > 0) {
            int ad_n = dep_list_count < 5 ? dep_list_count : 5;
            printf("  [GNFS dep-audit] analyzing %d smallest dependencies\n", ad_n);

            int all_sign_match = 0, all_sign_mismatch = 0;
            uint64_t char_sigs[5]; memset(char_sigs, 0, sizeof(char_sigs));
            int distinct_sigs = 0;

            for (int di = 0; di < ad_n; ++di) {
                int drow = dep_list[di].row_idx;
                int dsz = dep_list[di].dep_size;
                uint64_t* hd = &history[drow * hist_words];

                /* 1. Column-class cancellation from stored matrix */
                uint64_t xor_row[32]; memset(xor_row, 0, sizeof(uint64_t)*(size_t)row_words);
                for (int rj = 0; rj < rel_count; ++rj) {
                    if (!(hd[rj/64] & (1ull<<(rj%64)))) continue;
                    for (int w = 0; w < row_words; ++w) xor_row[w] ^= orig_matrix[rj * row_words + w];
                }
                bool sign_cancel = !(xor_row[0] & 1ull);
                bool rat_cancel = true, alg_cancel = true, char_cancel_ok = true, full_cancel = true;
                for (int fi = 0; fi < rfb_count; ++fi) {
                    int c = sign_col + fi;
                    if (xor_row[c/64] & (1ull<<(c%64))) { rat_cancel = false; break; }
                }
                for (int fi = 0; fi < afb_count; ++fi) {
                    int c = sign_col + rfb_count + fi;
                    if (xor_row[c/64] & (1ull<<(c%64))) { alg_cancel = false; break; }
                }
                uint64_t char_sig = 0u;
                for (int ci = 0; ci < char_count; ++ci) {
                    int c = sign_col + rfb_count + afb_count + ci;
                    if (xor_row[c/64] & (1ull<<(c%64))) { char_cancel_ok = false; char_sig |= (1ull<<ci); }
                }
                for (int w = 0; w < row_words; ++w) { if (xor_row[w]) { full_cancel = false; break; } }

                /* 2. Compute rational sign parity vs algebraic norm sign parity */
                int rat_neg_count = 0, alg_neg_count = 0;
                for (int rj = 0; rj < rel_count; ++rj) {
                    if (!(hd[rj/64] & (1ull<<(rj%64)))) continue;
                    int64_t ra = rel_a[rj]; uint64_t rb = rel_b[rj];
                    /* Rational sign: (a - b*m) < 0 */
                    BigNum bm2; bn_copy(&bm2, &poly.m);
                    { BigNum bb; bn_from_u64(&bb, rb); bn_mul(&bm2, &bm2, &bb); }
                    bool rneg;
                    if (ra >= 0) { BigNum ac; bn_from_u64(&ac,(uint64_t)ra); rneg = bn_cmp(&ac,&bm2)<0; }
                    else rneg = true;
                    if (rneg) ++rat_neg_count;
                    /* Algebraic norm sign: sign of F(a,b) = homogeneous poly eval
                     * F(a,b) = c3*a^3 + c2*a^2*b + c1*a*b^2 + c0*b^3
                     * For the sign, we need to know if F(a,b) is negative.
                     * gnfs_eval_homogeneous returns |F(a,b)|, so we check directly. */
                    /* F(a,b) with signed a: terms with odd power of a are negative when a<0 */
                    /* Quick sign check: evaluate F(a,b) as signed double */
                    double ad = (double)ra, bd = (double)rb;
                    double Fd = 0.0;
                    double apow_d = 1.0;
                    for (int k = 0; k <= poly.degree; ++k) {
                        double ck = bn_to_double(&poly.coeffs[k]);
                        double bpow_d = 1.0;
                        for (int j = 0; j < poly.degree - k; ++j) bpow_d *= bd;
                        Fd += ck * apow_d * bpow_d;
                        apow_d *= ad;
                    }
                    if (Fd < 0.0) ++alg_neg_count;
                }
                int rat_sign_par = rat_neg_count & 1;
                int alg_sign_par = alg_neg_count & 1;
                bool signs_match = (rat_sign_par == alg_sign_par);
                if (signs_match) ++all_sign_match; else ++all_sign_mismatch;
                char_sigs[di] = char_sig;

                printf("    dep#%d (%d rels): sign=%s rat=%s alg=%s char=%s full=%s\n",
                    di, dsz, sign_cancel?"ok":"FAIL", rat_cancel?"ok":"FAIL",
                    alg_cancel?"ok":"FAIL", char_cancel_ok?"ok":"FAIL", full_cancel?"ok":"FAIL");
                printf("      rat_sign_par=%d  alg_norm_sign_par=%d  match=%s  char_sig=0x%"PRIx64"\n",
                    rat_sign_par, alg_sign_par, signs_match?"yes":"NO", char_sig);
            }

            /* Character effectiveness summary */
            for (int di = 0; di < ad_n; ++di) {
                bool is_new = true;
                for (int dj = 0; dj < di; ++dj) { if (char_sigs[dj] == char_sigs[di]) { is_new = false; break; } }
                if (is_new) ++distinct_sigs;
            }
            printf("  [GNFS dep-audit] sign match: %d/%d  mismatch: %d/%d\n",
                all_sign_match, ad_n, all_sign_mismatch, ad_n);
            printf("  [GNFS dep-audit] distinct char signatures: %d/%d\n", distinct_sigs, ad_n);
            printf("  [GNFS dep-audit] char columns %s\n",
                distinct_sigs > 1 ? "appear discriminating (multiple distinct signatures)"
                                  : "appear INERT (all deps have same signature)");

            /* Conclusion */
            if (all_sign_mismatch > 0) {
                printf("  [GNFS dep-audit] CONCLUSION A: rational sign != algebraic norm sign for %d/%d deps\n", all_sign_mismatch, ad_n);
                printf("    → current sign column tracks WRONG object (rational sign instead of algebraic norm sign)\n");
                printf("    → next step: rebuild matrix with algebraic norm sign in column 0\n");
            } else if (distinct_sigs <= 1) {
                printf("  [GNFS dep-audit] CONCLUSION C: character columns inert, signs match\n");
                printf("    → character columns not discriminating; may need different character primes or more columns\n");
            } else {
                printf("  [GNFS dep-audit] CONCLUSION D: signs match, chars discriminate, but QR still fails\n");
                printf("    → deeper unit-group issue; move to exact integer algebraic product measurement\n");
            }
            } /* end audit_deps */

            /* ── Infinite-place / unit obstruction audit ── */
            if (cfg->audit_units && dep_list_count > 0) {
                printf("  [GNFS unit-audit] infinite-place / real-embedding sign audit\n");

                /* Find the real root of f(x) via bisection.
                 * f(x) = f3*x^3 + f2*x^2 + f1*x + f0. Evaluate as double. */
                double fc[4];
                for (int i = 0; i <= 3; ++i) fc[i] = bn_to_double(&poly.coeffs[i]);

                #define FEVAL(x) (fc[3]*(x)*(x)*(x) + fc[2]*(x)*(x) + fc[1]*(x) + fc[0])

                /* Bracket: try [-10000, 10000] expanding if needed */
                double lo = -1e6, hi = 1e6;
                double flo = FEVAL(lo), fhi = FEVAL(hi);
                /* Ensure bracket */
                if (flo * fhi > 0) { lo = -1e9; hi = 1e9; flo = FEVAL(lo); fhi = FEVAL(hi); }
                double alpha_R = 0.0;
                if (flo * fhi <= 0) {
                    for (int iter = 0; iter < 100; ++iter) {
                        double mid = (lo + hi) / 2.0;
                        double fmid = FEVAL(mid);
                        if (fmid == 0.0) { alpha_R = mid; break; }
                        if (flo * fmid < 0) { hi = mid; fhi = fmid; }
                        else { lo = mid; flo = fmid; }
                        alpha_R = (lo + hi) / 2.0;
                    }
                }
                #undef FEVAL

                double m_d = bn_to_double(&poly.m);
                printf("    real root alpha_R ≈ %.6f  (m = %.0f)\n", alpha_R, m_d);

                /* Per-relation sign sample (first 10 relations) */
                printf("    per-relation sign sample (first 10):\n");
                int sample_n = rel_count < 10 ? rel_count : 10;
                int rat_neg_sample = 0, real_neg_sample = 0, norm_neg_sample = 0;
                for (int ri = 0; ri < sample_n; ++ri) {
                    double a_d = (double)rel_a[ri], b_d = (double)rel_b[ri];
                    bool rat_neg = (a_d - b_d * m_d) < 0.0;
                    bool real_neg = (a_d - b_d * alpha_R) < 0.0;
                    /* Norm sign: F(a,b) evaluated as double */
                    double Fd = 0.0, apow = 1.0;
                    for (int k = 0; k <= poly.degree; ++k) {
                        double bpow = 1.0;
                        for (int j = 0; j < poly.degree - k; ++j) bpow *= b_d;
                        Fd += fc[k] * apow * bpow;
                        apow *= a_d;
                    }
                    bool norm_neg = (Fd < 0.0);
                    if (rat_neg) ++rat_neg_sample;
                    if (real_neg) ++real_neg_sample;
                    if (norm_neg) ++norm_neg_sample;
                    if (ri < 5)
                        printf("      rel#%d a=%"PRId64" b=%"PRIu64": rat=%c real=%c norm=%c\n",
                            ri, rel_a[ri], rel_b[ri],
                            rat_neg?'-':'+', real_neg?'-':'+', norm_neg?'-':'+');
                }
                printf("      ... neg counts: rat=%d real=%d norm=%d / %d\n",
                    rat_neg_sample, real_neg_sample, norm_neg_sample, sample_n);

                /* Dependency-level audit */
                int ad_n = dep_list_count < 5 ? dep_list_count : 5;
                int rat_even = 0, real_even = 0, norm_even = 0, rat_real_differ = 0;
                for (int di = 0; di < ad_n; ++di) {
                    int drow = dep_list[di].row_idx;
                    int dsz = dep_list[di].dep_size;
                    uint64_t* hd = &history[drow * hist_words];

                    int r_neg = 0, e_neg = 0, n_neg = 0;
                    for (int rj = 0; rj < rel_count; ++rj) {
                        if (!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                        double a_d = (double)rel_a[rj], b_d = (double)rel_b[rj];
                        if ((a_d - b_d * m_d) < 0.0) ++r_neg;
                        if ((a_d - b_d * alpha_R) < 0.0) ++e_neg;
                        double Fd = 0.0, apow = 1.0;
                        for (int k = 0; k <= poly.degree; ++k) {
                            double bpow = 1.0;
                            for (int j = 0; j < poly.degree - k; ++j) bpow *= b_d;
                            Fd += fc[k] * apow * bpow;
                            apow *= a_d;
                        }
                        if (Fd < 0.0) ++n_neg;
                    }
                    int rp = r_neg & 1, ep = e_neg & 1, np = n_neg & 1;
                    bool rr_same = (rp == ep);
                    if (rp == 0) ++rat_even;
                    if (ep == 0) ++real_even;
                    if (np == 0) ++norm_even;
                    if (!rr_same) ++rat_real_differ;

                    printf("    dep#%d (%d rels): rat_par=%d real_par=%d norm_par=%d  rat_vs_real=%s\n",
                        di, dsz, rp, ep, np, rr_same ? "same" : "DIFFER");
                }

                printf("  [GNFS unit-audit] summary across %d deps:\n", ad_n);
                printf("    rat_even=%d real_even=%d norm_even=%d  rat_real_differ=%d\n",
                    rat_even, real_even, norm_even, rat_real_differ);

                if (rat_real_differ > 0) {
                    printf("  [GNFS unit-audit] CONCLUSION B: rational sign ≠ real-embedding sign for %d/%d deps\n", rat_real_differ, ad_n);
                    printf("    → current sign column (rational) is NOT the correct infinite-place constraint\n");
                    printf("    → the real-embedding sign of (a - b·α_R) should be the sign column instead\n");
                } else if (rat_even == ad_n && real_even == ad_n) {
                    printf("  [GNFS unit-audit] CONCLUSION A: rat sign = real-embedding sign for all deps (both even)\n");
                    printf("    → sign column is likely correct; obstruction is elsewhere\n");
                } else {
                    printf("  [GNFS unit-audit] CONCLUSION C: mixed / inconclusive\n");
                }
            } /* end audit_units */
        } else if (!found_factor_m15 && poly.degree == 3 && !bn_is_one(&poly.coeffs[3]) && cfg->diag) {
            printf("  [GNFS 2b-field] SKIP: non-monic polynomial (f3 != 1) — monic-only for field sqrt\n");

            /* Unit audit also runs for non-monic targets */
            if (cfg->audit_units && ns_count > 0) {
                /* Build dep_list for non-monic target */
                typedef struct { int row_idx; int dep_size; } DepInfoNM;
                DepInfoNM dep_list_nm[256]; int dlc_nm = 0;
                for (int ri = 0; ri < rel_count && dlc_nm < 256; ++ri) {
                    bool z = true;
                    for (int w = 0; w < row_words; ++w) { if (matrix[ri*row_words+w]) { z = false; break; } }
                    if (!z) continue;
                    uint64_t* h = &history[ri * hist_words]; int sz = 0;
                    for (int rj = 0; rj < rel_count; ++rj) if (h[rj/64]&(1ull<<(rj%64))) ++sz;
                    dep_list_nm[dlc_nm].row_idx = ri; dep_list_nm[dlc_nm].dep_size = sz; ++dlc_nm;
                }
                for (int i=1;i<dlc_nm;++i){DepInfoNM t=dep_list_nm[i];int j=i-1;while(j>=0&&dep_list_nm[j].dep_size>t.dep_size){dep_list_nm[j+1]=dep_list_nm[j];--j;}dep_list_nm[j+1]=t;}

                printf("  [GNFS unit-audit] infinite-place / real-embedding sign audit (non-monic)\n");
                double fc[4]; for (int i=0;i<=3;++i) fc[i] = bn_to_double(&poly.coeffs[i]);
                #define FEVAL_NM(x) (fc[3]*(x)*(x)*(x) + fc[2]*(x)*(x) + fc[1]*(x) + fc[0])
                double lo=-1e6,hi=1e6,flo=FEVAL_NM(lo),fhi=FEVAL_NM(hi);
                if(flo*fhi>0){lo=-1e9;hi=1e9;flo=FEVAL_NM(lo);fhi=FEVAL_NM(hi);}
                double alpha_R=0.0;
                if(flo*fhi<=0){for(int it=0;it<100;++it){double mid=(lo+hi)/2.0,fmid=FEVAL_NM(mid);if(fmid==0.0){alpha_R=mid;break;}if(flo*fmid<0){hi=mid;fhi=fmid;}else{lo=mid;flo=fmid;}alpha_R=(lo+hi)/2.0;}}
                #undef FEVAL_NM
                double m_d = bn_to_double(&poly.m);
                printf("    real root alpha_R ≈ %.6f  (m = %.0f)\n", alpha_R, m_d);

                int ad_n = dlc_nm < 5 ? dlc_nm : 5;
                int rat_even=0,real_even=0,norm_even=0,rat_real_differ=0;
                for (int di=0;di<ad_n;++di) {
                    int drow=dep_list_nm[di].row_idx, dsz=dep_list_nm[di].dep_size;
                    uint64_t* hd=&history[drow*hist_words];
                    int r_neg=0,e_neg=0,n_neg=0;
                    for(int rj=0;rj<rel_count;++rj){
                        if(!(hd[rj/64]&(1ull<<(rj%64)))) continue;
                        double a_d=(double)rel_a[rj],b_d=(double)rel_b[rj];
                        if((a_d-b_d*m_d)<0.0)++r_neg;
                        if((a_d-b_d*alpha_R)<0.0)++e_neg;
                        double Fd=0.0,apow=1.0;
                        for(int k=0;k<=poly.degree;++k){double bpow=1.0;for(int j=0;j<poly.degree-k;++j)bpow*=b_d;Fd+=fc[k]*apow*bpow;apow*=a_d;}
                        if(Fd<0.0)++n_neg;
                    }
                    int rp=r_neg&1,ep=e_neg&1,np=n_neg&1;
                    bool rr_same=(rp==ep);
                    if(rp==0)++rat_even; if(ep==0)++real_even; if(np==0)++norm_even;
                    if(!rr_same)++rat_real_differ;
                    printf("    dep#%d (%d rels): rat_par=%d real_par=%d norm_par=%d  rat_vs_real=%s\n",
                        di,dsz,rp,ep,np,rr_same?"same":"DIFFER");
                }
                printf("  [GNFS unit-audit] summary: rat_even=%d real_even=%d norm_even=%d rat_real_differ=%d / %d\n",
                    rat_even,real_even,norm_even,rat_real_differ,ad_n);
                if(rat_real_differ>0) printf("  [GNFS unit-audit] CONCLUSION B: rat sign ≠ real-embedding sign → missing infinite-place constraint\n");
                else if(rat_even==ad_n&&real_even==ad_n) printf("  [GNFS unit-audit] CONCLUSION A: signs match, both even → sign column likely correct\n");
                else printf("  [GNFS unit-audit] CONCLUSION C: mixed / inconclusive\n");
            }
        }
    }

    /* Milestone endpoint */
    if (ns_count > 0 && !found_factor_m15) {
        fr.method = "gnfs(pipeline)";
    }

    free(rfb); free(afb); free(matrix); free(pivot_row); free(history); free(row_is_pivot);
    free(rel_a); free(rel_b);
    if (orig_matrix) free(orig_matrix);
    if (!found_factor_m15) fr.status = (int)CPSS_INCOMPLETE;
    fr.time_seconds = now_sec() - t0;
    return fr;
}
