/* ── Synthetic-square CRT pipeline diagnostic ──
 * Computes S = U² for known U, runs aux-prime sqrt + polynomial CRT,
 * verifies whether it recovers ±U coefficient-wise. */
static void gnfs_synth_square_test(const GNFSPoly* poly) {
    printf("  [SYNTH] === Synthetic-Square CRT Pipeline Test ===\n");
    /* U = 3 + 5α + 7α² (small known element) */
    int64_t u[3] = {3, 5, 7};
    printf("  [SYNTH] U = [%"PRId64", %"PRId64", %"PRId64"]\n", u[0], u[1], u[2]);

    /* Compute S = U² in Z[α]/(f) with exact integer arithmetic.
     * f(x) = x³ + f2x² + f1x + f0 (monic degree 3).
     * Raw product coefficients p[0..4], then reduce p[4] and p[3]. */
    int64_t f0 = (int64_t)bn_to_u64(&poly->coeffs[0]);
    int64_t f1 = (int64_t)bn_to_u64(&poly->coeffs[1]);
    int64_t f2 = (int64_t)bn_to_u64(&poly->coeffs[2]);
    /* f3 = 1 (monic) */
    int64_t p[5] = {0,0,0,0,0};
    for (int i=0;i<3;++i) for(int j=0;j<3;++j) p[i+j] += u[i]*u[j];
    /* Reduce p[4]: α⁴ = α·(-f2α²-f1α-f0) → p[3]-=p[4]*f2, p[2]-=p[4]*f1, p[1]-=p[4]*f0 */
    p[3] -= p[4]*f2; p[2] -= p[4]*f1; p[1] -= p[4]*f0; p[4]=0;
    /* Reduce p[3]: α³ = -f2α²-f1α-f0 */
    p[2] -= p[3]*f2; p[1] -= p[3]*f1; p[0] -= p[3]*f0; p[3]=0;
    printf("  [SYNTH] S = U² = [%"PRId64", %"PRId64", %"PRId64"]\n", p[0], p[1], p[2]);

    /* Run aux-prime pipeline */
    int primes_found = 0, primes_tried = 0;
    int sign_correct = 0, sign_wrong = 0, sign_neither = 0;
    #define SYNTH_MAX_PRIMES 10
    uint64_t sp_primes[SYNTH_MAX_PRIMES];
    uint64_t sp_T[SYNTH_MAX_PRIMES][3]; /* stored per-prime T (after sign choice) */
    BigNum crt_c[3], crt_m; bn_zero(&crt_m);
    for(int c=0;c<3;++c) bn_zero(&crt_c[c]);

    uint64_t rng = 0xCAFEBABE42ULL;
    for (int tries = 0; tries < 20000 && primes_found < SYNTH_MAX_PRIMES; ++tries) {
        rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17;
        uint64_t cand = (rng | ((uint64_t)1<<59)) | 3u;
        cand = cand & ~1ull; cand |= 3u;
        if ((cand&3u)!=3u || !miller_rabin_u64(cand)) continue;
        /* Check f irreducible mod cand via Frobenius: α^p³ = α? */
        uint64_t fv[3]; for(int k=0;k<3;++k) fv[k]=bn_mod_u64(&poly->coeffs[k],cand);
        uint64_t res[3]={1,0,0}, base[3]={0,1,0};
        #define P3S(o,a2,b2,pv) do{uint64_t _t[5]={0,0,0,0,0};\
            for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+mulmod_u64((a2)[_i],(b2)[_j],pv))%pv;\
            if(_t[4]){_t[3]=(_t[3]+mulmod_u64(_t[4],pv-fv[2],pv))%pv;_t[2]=(_t[2]+mulmod_u64(_t[4],pv-fv[1],pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[4],pv-fv[0],pv))%pv;}\
            if(_t[3]){_t[2]=(_t[2]+mulmod_u64(_t[3],pv-fv[2],pv))%pv;_t[1]=(_t[1]+mulmod_u64(_t[3],pv-fv[1],pv))%pv;_t[0]=(_t[0]+mulmod_u64(_t[3],pv-fv[0],pv))%pv;}\
            (o)[0]=_t[0];(o)[1]=_t[1];(o)[2]=_t[2];}while(0)
        uint64_t ev=cand;
        while(ev>0u){if(ev&1u){uint64_t t[3];P3S(t,res,base,cand);res[0]=t[0];res[1]=t[1];res[2]=t[2];}
            uint64_t t2[3];P3S(t2,base,base,cand);base[0]=t2[0];base[1]=t2[1];base[2]=t2[2];ev>>=1u;}
        if(res[0]==0u&&res[1]==1u&&res[2]==0u) continue; /* α^p=α → linear factor */
        /* Also check α^(p²) ≠ α. If α^(p²)=α, f has a quadratic factor → reducible. */
        { uint64_t r2[3]={1,0,0}, b2[3]={res[0],res[1],res[2]};
          uint64_t e2=cand;
          while(e2>0u){if(e2&1u){uint64_t t[3];P3S(t,r2,b2,cand);r2[0]=t[0];r2[1]=t[1];r2[2]=t[2];}
              uint64_t t2[3];P3S(t2,b2,b2,cand);b2[0]=t2[0];b2[1]=t2[1];b2[2]=t2[2];e2>>=1u;}
          if(r2[0]==0u&&r2[1]==1u&&r2[2]==0u) continue; /* α^(p²)=α → quadratic factor */
        }
        ++primes_tried;

        /* Reduce S mod p */
        uint64_t sp[3];
        for(int k=0;k<3;++k) sp[k] = ((p[k]%(int64_t)cand)+(int64_t)cand)%(int64_t)cand;

        /* QR check: S^((p³-1)/2) = 1? */
        BigNum p_bn; bn_from_u64(&p_bn, cand);
        BigNum f3i; bn_from_u64(&f3i, 1u);
        AlgElem S_ae; for(int k=0;k<3;++k) bn_from_u64(&S_ae.c[k], sp[k]);
        BigNum qr_exp; gnfs_qr_exp_from_prime(&qr_exp, cand);
        unsigned qr_bits = bn_bitlen(&qr_exp);
        AlgElem qc; alg_one(&qc);
        AlgElem qb; for(int c=0;c<3;++c)bn_copy(&qb.c[c],&S_ae.c[c]);
        for(unsigned bt=0;bt<qr_bits;++bt){
            if(qr_exp.limbs[bt/64]&(1ull<<(bt%64))){AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&qc.c[c]);alg_mul(&qc,&t,&qb,poly,&f3i,&p_bn);}
            AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&qb.c[c]);alg_mul(&qb,&t2,&t2,poly,&f3i,&p_bn);}
        bool is_qr = bn_is_one(&qc.c[0])&&bn_is_zero(&qc.c[1])&&bn_is_zero(&qc.c[2]);
        if (!is_qr) {
            printf("  [SYNTH] FAIL-QR: known square NOT QR at p=%"PRIu64" (prime #%d)\n", cand, primes_tried);
            #undef P3S
            return;
        }

        /* Compute T = S^((p³+1)/4) */
        BigNum sq_exp; gnfs_sqrt_exp_from_prime(&sq_exp, cand);
        unsigned sq_bits = bn_bitlen(&sq_exp);
        AlgElem T_ae; alg_one(&T_ae);
        AlgElem sb; for(int c=0;c<3;++c)bn_copy(&sb.c[c],&S_ae.c[c]);
        for(unsigned bt=0;bt<sq_bits;++bt){
            if(sq_exp.limbs[bt/64]&(1ull<<(bt%64))){AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&T_ae.c[c]);alg_mul(&T_ae,&t,&sb,poly,&f3i,&p_bn);}
            AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&sb.c[c]);alg_mul(&sb,&t2,&t2,poly,&f3i,&p_bn);}
        /* Verify T² = S */
        AlgElem Tsq;{AlgElem t1;for(int c=0;c<3;++c)bn_copy(&t1.c[c],&T_ae.c[c]);alg_mul(&Tsq,&t1,&t1,poly,&f3i,&p_bn);}
        bool t2ok=true; for(int c=0;c<3;++c) if(bn_cmp(&Tsq.c[c],&S_ae.c[c])!=0){t2ok=false;break;}
        if(!t2ok){
            printf("  [SYNTH] FAIL-SQRT: T²≠S at p=%"PRIu64"\n", cand);
            #undef P3S
            return;
        }

        /* Check T ≡ ±U mod p */
        uint64_t tc[3]; for(int k=0;k<3;++k) tc[k]=bn_to_u64(&T_ae.c[k]);
        uint64_t up[3]; for(int k=0;k<3;++k) up[k]=((u[k]%(int64_t)cand)+(int64_t)cand)%(int64_t)cand;
        uint64_t neg_up[3]; for(int k=0;k<3;++k) neg_up[k]=(cand-up[k])%cand;
        bool is_plus = (tc[0]==up[0]&&tc[1]==up[1]&&tc[2]==up[2]);
        bool is_minus = (tc[0]==neg_up[0]&&tc[1]==neg_up[1]&&tc[2]==neg_up[2]);
        if (is_plus) ++sign_correct;
        else if (is_minus) { ++sign_correct; /* negate T to match +U for CRT consistency */
            for(int k=0;k<3;++k) tc[k]=neg_up[k]; /* use -T = U mod p */
            for(int k=0;k<3;++k){if(bn_is_zero(&T_ae.c[k]))continue;BigNum ng;bn_copy(&ng,&p_bn);bn_sub(&ng,&T_ae.c[k]);bn_copy(&T_ae.c[k],&ng);}
        } else { ++sign_neither;
            printf("  [SYNTH] FAIL-SQRT: T ≠ ±U mod p=%"PRIu64"  T=[%"PRIu64",%"PRIu64",%"PRIu64"] U=[%"PRIu64",%"PRIu64",%"PRIu64"]\n",
                cand, tc[0],tc[1],tc[2], up[0],up[1],up[2]);
            #undef P3S
            return;
        }
        printf("  [SYNTH] p=%"PRIu64": QR=yes T²=S=yes T=%sU mod p\n", cand, is_plus?"+":"-");

        /* Polynomial CRT accumulation (always using +U sign) */
        sp_primes[primes_found] = cand;
        for(int k=0;k<3;++k) sp_T[primes_found][k] = tc[k];
        if (primes_found == 0) {
            bn_from_u64(&crt_m, cand);
            for(int c=0;c<3;++c) bn_from_u64(&crt_c[c], tc[c]);
        } else {
            for(int c=0;c<3;++c) {
                uint64_t target = tc[c];
                uint64_t cur = bn_mod_u64(&crt_c[c], cand);
                uint64_t diff = (target + cand - cur) % cand;
                uint64_t mi = powmod_u64(bn_mod_u64(&crt_m, cand), cand-2, cand);
                uint64_t corr = mulmod_u64(diff, mi, cand);
                if(corr){BigNum s;bn_from_u64(&s,corr);BigNum st;bn_mul(&st,&crt_m,&s);bn_add(&crt_c[c],&st);}
            }
            BigNum nm;bn_from_u64(&nm,cand);BigNum pm;bn_mul(&pm,&crt_m,&nm);bn_copy(&crt_m,&pm);
        }
        ++primes_found;

        /* After enough primes, check centered lift */
        if (primes_found >= 2) {
            BigNum half; bn_copy(&half, &crt_m);
            for(uint32_t li=0;li<half.used;++li){half.limbs[li]>>=1;if(li+1<half.used&&(half.limbs[li+1]&1))half.limbs[li]|=(1ull<<63);}
            bn_normalize(&half);
            int64_t recovered[3];
            for (int c=0;c<3;++c) {
                bool neg = (bn_cmp(&crt_c[c], &half) > 0);
                BigNum abs_v; if(neg){bn_copy(&abs_v,&crt_m);bn_sub(&abs_v,&crt_c[c]);}else bn_copy(&abs_v,&crt_c[c]);
                recovered[c] = (int64_t)bn_to_u64(&abs_v);
                if(neg) recovered[c] = -recovered[c];
            }
            bool match_plus = (recovered[0]==u[0]&&recovered[1]==u[1]&&recovered[2]==u[2]);
            bool match_minus = (recovered[0]==-u[0]&&recovered[1]==-u[1]&&recovered[2]==-u[2]);
            printf("  [SYNTH] CRT[%d primes, %u bits]: recovered=[%"PRId64",%"PRId64",%"PRId64"] %s\n",
                primes_found, bn_bitlen(&crt_m), recovered[0], recovered[1], recovered[2],
                match_plus?"= +U  PASS!":(match_minus?"= -U  PASS!":"MISMATCH"));
            if (match_plus || match_minus) {
                printf("  [SYNTH] === SYNTH-PASS: CRT recovered %sU exactly after %d primes ===\n",
                    match_plus?"+":"-", primes_found);
                #undef P3S
                return;
            }
        }
    }
    #undef P3S
    if (primes_found < 2)
        printf("  [SYNTH] FAIL: only found %d primes (need >=2)\n", primes_found);
    else
        printf("  [SYNTH] FAIL-CRT: %d primes accumulated but coefficients didn't converge\n", primes_found);
}

/* ── Synthetic Hensel lift test ──
 * Constructs known T_true with ~100-bit coefficients, computes S = T_true² in Z[α]/(f),
 * runs Hensel lift from a small prime, verifies recovery of ±T_true.
 * Per-step assertions: T_k²≡S and (2T_k)·I_k≡1 mod p^(2^k). */
static bool gnfs_synth_hensel_test(const GNFSPoly* poly) {
    printf("  [SYNTH-HENSEL] === Synthetic Hensel Lift Test ===\n");

    /* Step 1: Construct T_true with ~100-bit coefficients.
     * Use deterministic values: T_true = [2^90 + 37, 2^95 + 71, 2^88 + 13] */
    BigNum T_true[3];
    bn_from_u64(&T_true[0], 1u); for(int i=0;i<90;++i) bn_shl1(&T_true[0]); bn_add_u64(&T_true[0], 37);
    bn_from_u64(&T_true[1], 1u); for(int i=0;i<95;++i) bn_shl1(&T_true[1]); bn_add_u64(&T_true[1], 71);
    bn_from_u64(&T_true[2], 1u); for(int i=0;i<88;++i) bn_shl1(&T_true[2]); bn_add_u64(&T_true[2], 13);
    printf("  [SYNTH-HENSEL] T_true bits: [%u, %u, %u]\n",
        bn_bitlen(&T_true[0]), bn_bitlen(&T_true[1]), bn_bitlen(&T_true[2]));

    /* Step 2: We will compute S = T_true² mod each lift modulus on the fly
     * using alg_mul (avoids needing BigNum mod BigNum reduction).
     * For the initial printout, compute S mod a large modulus once. */
    BigNum f3i; bn_from_u64(&f3i, 1u); /* monic */
    { BigNum big_mod; bn_from_u64(&big_mod, 1u);
      for (int i = 0; i < 250; ++i) bn_shl1(&big_mod);
      bn_add_u64(&big_mod, 861u);
      AlgElem T_ae; for(int c=0;c<3;++c) bn_copy(&T_ae.c[c], &T_true[c]);
      AlgElem S_info; alg_mul(&S_info, &T_ae, &T_ae, poly, &f3i, &big_mod);
      printf("  [SYNTH-HENSEL] S = T² approx coeff bits: [%u, %u, %u]\n",
          bn_bitlen(&S_info.c[0]), bn_bitlen(&S_info.c[1]), bn_bitlen(&S_info.c[2]));
    }

    /* Step 3: Find a small prime p where f is irreducible */
    uint64_t p = 0;
    for (uint64_t cand = 3; cand < 200; cand += 2) {
        if (!miller_rabin_u64(cand)) continue;
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
        if (cand % 4 == 3) { p = cand; break; }
        if (p == 0) p = cand;
    }
    if (p == 0) { printf("  [SYNTH-HENSEL] FAIL: no small irreducible prime\n"); return false; }
    printf("  [SYNTH-HENSEL] p=%"PRIu64"\n", p);

    /* Step 4: Compute S mod p = T_true² mod (f, p), then brute-force sqrt */
    BigNum p_bn; bn_from_u64(&p_bn, p);
    AlgElem T_mod_p; for(int c=0;c<3;++c) { uint64_t v = bn_mod_u64(&T_true[c], p); bn_from_u64(&T_mod_p.c[c], v); }
    AlgElem S_mod_p; alg_mul(&S_mod_p, &T_mod_p, &T_mod_p, poly, &f3i, &p_bn);

    AlgElem T0;
    bool found = false;
    for (uint64_t c0=0; c0<p && !found; ++c0)
    for (uint64_t c1=0; c1<p && !found; ++c1)
    for (uint64_t c2=0; c2<p && !found; ++c2) {
        AlgElem trial; bn_from_u64(&trial.c[0],c0); bn_from_u64(&trial.c[1],c1); bn_from_u64(&trial.c[2],c2);
        AlgElem sq; alg_mul(&sq, &trial, &trial, poly, &f3i, &p_bn);
        if (bn_cmp(&sq.c[0],&S_mod_p.c[0])==0 && bn_cmp(&sq.c[1],&S_mod_p.c[1])==0 && bn_cmp(&sq.c[2],&S_mod_p.c[2])==0) {
            for(int c=0;c<3;++c) bn_copy(&T0.c[c], &trial.c[c]);
            found = true;
        }
    }
    if (!found) { printf("  [SYNTH-HENSEL] FAIL: S not a square mod p\n"); return false; }
    printf("  [SYNTH-HENSEL] T0=[%"PRIu64",%"PRIu64",%"PRIu64"] (sqrt mod p)\n",
        bn_to_u64(&T0.c[0]), bn_to_u64(&T0.c[1]), bn_to_u64(&T0.c[2]));

    /* Step 5: Initial inverse: (2·T0)⁻¹ mod (f, p) */
    AlgElem two_T0;
    for (int c=0;c<3;++c) {
        bn_copy(&two_T0.c[c], &T0.c[c]); bn_add(&two_T0.c[c], &T0.c[c]);
        if (bn_cmp(&two_T0.c[c], &p_bn) >= 0) bn_sub(&two_T0.c[c], &p_bn);
    }
    AlgElem inv_cur;
    if (!alg_inv(&inv_cur, &two_T0, poly, &f3i, &p_bn)) {
        printf("  [SYNTH-HENSEL] FAIL: 2·T0 not invertible\n"); return false;
    }

    /* Step 6: Newton/Hensel lift */
    unsigned max_t_bits = bn_bitlen(&T_true[0]);
    for (int c=1;c<3;++c) { unsigned b=bn_bitlen(&T_true[c]); if(b>max_t_bits) max_t_bits=b; }
    unsigned target_bits = max_t_bits + 20; /* need modulus > max|T_true coefficient| */
    unsigned cur_bits = 0; { uint64_t t=p; while(t){++cur_bits;t>>=1;} }
    int lifts_needed = 0;
    { unsigned b=cur_bits; while(b<target_bits){b*=2;++lifts_needed;} }
    printf("  [SYNTH-HENSEL] target_bits=%u lifts_needed=%d\n", target_bits, lifts_needed);

    BigNum cur_mod; bn_from_u64(&cur_mod, p);
    AlgElem T_cur; for(int c=0;c<3;++c) bn_copy(&T_cur.c[c], &T0.c[c]);
    bool all_asserts_pass = true;

    for (int lift = 0; lift < lifts_needed; ++lift) {
        BigNum new_mod; bn_mul(&new_mod, &cur_mod, &cur_mod);

        /* Compute S = T_true² mod (f, new_mod) directly.
         * Reduce T_true coefficients mod new_mod first, then square. */
        AlgElem S_new;
        { AlgElem T_red;
          for(int c=0;c<3;++c) {
              if (bn_cmp(&T_true[c], &new_mod) < 0) {
                  bn_copy(&T_red.c[c], &T_true[c]); /* already < new_mod */
              } else {
                  /* BigNum mod via shift-and-subtract (binary long division).
                   * Compute rem = T_true[c] % new_mod. */
                  BigNum rem; bn_copy(&rem, &T_true[c]);
                  unsigned rem_bits = bn_bitlen(&rem);
                  unsigned mod_bits = bn_bitlen(&new_mod);
                  if (rem_bits >= mod_bits) {
                      unsigned shift = rem_bits - mod_bits;
                      BigNum shifted; bn_copy(&shifted, &new_mod);
                      bn_shl(&shifted, shift); /* shifted = new_mod << shift */
                      for (unsigned s = 0; s <= shift; ++s) {
                          if (bn_cmp(&rem, &shifted) >= 0) bn_sub(&rem, &shifted);
                          /* Right-shift by 1 */
                          for(uint32_t li=0;li<shifted.used;++li){
                              shifted.limbs[li]>>=1;
                              if(li+1<shifted.used&&(shifted.limbs[li+1]&1)) shifted.limbs[li]|=(1ull<<63);
                          }
                          bn_normalize(&shifted);
                      }
                  }
                  bn_copy(&T_red.c[c], &rem);
              }
          }
          alg_mul(&S_new, &T_red, &T_red, poly, &f3i, &new_mod);
        }

        /* ASSERTION 1: T²≡S mod cur_mod — compute both directly mod cur_mod */
        { AlgElem Tsq_cm; alg_mul(&Tsq_cm, &T_cur, &T_cur, poly, &f3i, &cur_mod);
          /* S mod cur_mod: reduce T_true mod cur_mod, then square */
          AlgElem T_red_cm;
          for(int c=0;c<3;++c) {
              if (bn_cmp(&T_true[c], &cur_mod) < 0) {
                  bn_copy(&T_red_cm.c[c], &T_true[c]);
              } else if (cur_mod.used == 1) {
                  uint64_t v = bn_mod_u64(&T_true[c], cur_mod.limbs[0]);
                  bn_from_u64(&T_red_cm.c[c], v);
              } else {
                  /* cur_mod is multi-limb but T_true >= cur_mod: use shift-and-subtract */
                  BigNum rem; bn_copy(&rem, &T_true[c]);
                  unsigned rb = bn_bitlen(&rem), mb = bn_bitlen(&cur_mod);
                  if (rb >= mb) {
                      BigNum sh; bn_copy(&sh, &cur_mod); bn_shl(&sh, rb - mb);
                      for (unsigned s = 0; s <= rb - mb; ++s) {
                          if (bn_cmp(&rem, &sh) >= 0) bn_sub(&rem, &sh);
                          for(uint32_t li=0;li<sh.used;++li){sh.limbs[li]>>=1;if(li+1<sh.used&&(sh.limbs[li+1]&1))sh.limbs[li]|=(1ull<<63);}
                          bn_normalize(&sh);
                      }
                  }
                  bn_copy(&T_red_cm.c[c], &rem);
              }
          }
          AlgElem S_cm; alg_mul(&S_cm, &T_red_cm, &T_red_cm, poly, &f3i, &cur_mod);
          bool t2_ok = true;
          for (int c=0;c<3;++c) if (bn_cmp(&Tsq_cm.c[c], &S_cm.c[c]) != 0) { t2_ok = false; break; }
          if (!t2_ok) { printf("  [SYNTH-HENSEL] FAIL: T²≢S mod p^(2^%d) at lift %d\n", lift, lift); all_asserts_pass=false; return false; }
        }

        /* ASSERTION 2: (2T)·I≡1 mod cur_mod — T_cur and inv_cur are already < cur_mod */
        { AlgElem two_T;
          for(int c=0;c<3;++c){bn_copy(&two_T.c[c],&T_cur.c[c]);bn_add(&two_T.c[c],&T_cur.c[c]);
              if(bn_cmp(&two_T.c[c],&cur_mod)>=0) bn_sub(&two_T.c[c],&cur_mod);}
          AlgElem prod; alg_mul(&prod, &two_T, &inv_cur, poly, &f3i, &cur_mod);
          bool inv_ok = bn_is_one(&prod.c[0]) && bn_is_zero(&prod.c[1]) && bn_is_zero(&prod.c[2]);
          if (!inv_ok) { printf("  [SYNTH-HENSEL] FAIL: (2T)·I≢1 mod p^(2^%d) at lift %d\n", lift, lift); all_asserts_pass=false; return false; }
        }

        /* T² mod new_mod (needed for the Newton correction below) */
        AlgElem Tsq; alg_mul(&Tsq, &T_cur, &T_cur, poly, &f3i, &new_mod);

        /* Residual = S - T² mod new_mod */
        AlgElem residual;
        for (int c=0;c<3;++c) {
            if (bn_cmp(&S_new.c[c], &Tsq.c[c]) >= 0) {
                bn_copy(&residual.c[c], &S_new.c[c]); bn_sub(&residual.c[c], &Tsq.c[c]);
            } else {
                bn_copy(&residual.c[c], &new_mod); bn_add(&residual.c[c], &S_new.c[c]); bn_sub(&residual.c[c], &Tsq.c[c]);
            }
        }

        /* Correction = residual · inv_cur mod new_mod */
        AlgElem correction; alg_mul(&correction, &residual, &inv_cur, poly, &f3i, &new_mod);

        /* T_{k+1} = T_k + correction */
        for (int c=0;c<3;++c) {
            bn_add(&T_cur.c[c], &correction.c[c]);
            if (bn_cmp(&T_cur.c[c], &new_mod) >= 0) bn_sub(&T_cur.c[c], &new_mod);
        }

        /* Lift inverse: inv_{k+1} = inv_k · (2 - 2·T_{k+1}·inv_k) */
        AlgElem two_T_new;
        for (int c=0;c<3;++c) {
            bn_copy(&two_T_new.c[c], &T_cur.c[c]); bn_add(&two_T_new.c[c], &T_cur.c[c]);
            if (bn_cmp(&two_T_new.c[c], &new_mod) >= 0) bn_sub(&two_T_new.c[c], &new_mod);
        }
        AlgElem ti_prod; alg_mul(&ti_prod, &two_T_new, &inv_cur, poly, &f3i, &new_mod);
        AlgElem two_minus;
        { BigNum two; bn_from_u64(&two, 2u);
          if (bn_cmp(&two, &ti_prod.c[0]) >= 0) { bn_copy(&two_minus.c[0], &two); bn_sub(&two_minus.c[0], &ti_prod.c[0]); }
          else { bn_copy(&two_minus.c[0], &new_mod); bn_add(&two_minus.c[0], &two); bn_sub(&two_minus.c[0], &ti_prod.c[0]); }
          for (int c=1;c<3;++c) {
              if (bn_is_zero(&ti_prod.c[c])) bn_zero(&two_minus.c[c]);
              else { bn_copy(&two_minus.c[c], &new_mod); bn_sub(&two_minus.c[c], &ti_prod.c[c]); }
          }
        }
        AlgElem new_inv; alg_mul(&new_inv, &inv_cur, &two_minus, poly, &f3i, &new_mod);
        for(int c=0;c<3;++c) bn_copy(&inv_cur.c[c], &new_inv.c[c]);

        printf("  [SYNTH-HENSEL] lift %d: %u→%u bits, T²≡S=OK, (2T)I≡1=OK\n",
            lift, bn_bitlen(&cur_mod), bn_bitlen(&new_mod));
        bn_copy(&cur_mod, &new_mod);
    }

    /* Step 7: Centered lift — recover exact integer coefficients */
    BigNum half; bn_copy(&half, &cur_mod);
    for(uint32_t li=0;li<half.used;++li){half.limbs[li]>>=1;if(li+1<half.used&&(half.limbs[li+1]&1))half.limbs[li]|=(1ull<<63);}
    bn_normalize(&half);

    BigNum recovered[3];
    bool neg[3];
    for (int c=0;c<3;++c) {
        neg[c] = (bn_cmp(&T_cur.c[c], &half) > 0);
        if (neg[c]) { bn_copy(&recovered[c], &cur_mod); bn_sub(&recovered[c], &T_cur.c[c]); }
        else bn_copy(&recovered[c], &T_cur.c[c]);
    }
    printf("  [SYNTH-HENSEL] recovered coeff bits: [%s%u, %s%u, %s%u]\n",
        neg[0]?"-":"", bn_bitlen(&recovered[0]),
        neg[1]?"-":"", bn_bitlen(&recovered[1]),
        neg[2]?"-":"", bn_bitlen(&recovered[2]));

    /* Check: recovered = ±T_true coefficient-wise */
    bool match_plus = true, match_minus = true;
    for (int c=0;c<3;++c) {
        if (neg[c] || bn_cmp(&recovered[c], &T_true[c]) != 0) match_plus = false;
        if (!neg[c] || bn_cmp(&recovered[c], &T_true[c]) != 0) match_minus = false;
    }
    if (match_plus) printf("  [SYNTH-HENSEL] === PASS: recovered +T_true exactly ===\n");
    else if (match_minus) printf("  [SYNTH-HENSEL] === PASS: recovered -T_true exactly ===\n");
    else {
        printf("  [SYNTH-HENSEL] === FAIL: recovered ≠ ±T_true ===\n");
        for (int c=0;c<3;++c) {
            char rb[128], tb[128];
            bn_to_str(&recovered[c], rb, sizeof(rb));
            bn_to_str(&T_true[c], tb, sizeof(tb));
            printf("    c[%d]: recovered=%s%s  T_true=%s\n", c, neg[c]?"-":"", rb, tb);
        }
        return false;
    }
    return true;
}

/* ── Known-square column sanity test ──
 * Tests character columns and Schirokauer map formation on synthetic elements.
 * Sub-tests:
 *   (a) Character: Legendre((a-b*r)^2, q) must be +1 (QR) for all character primes
 *   (b) Schirokauer additivity: schk(beta^2) = 2*schk(beta) mod ell
 *   (c) GF(2) parity: for ell=2, schk(beta^2) mod 2 = 0 */
static bool gnfs_selftest_columns(const GNFSPoly* poly) {
    printf("  [COLTEST] === Known-Square Column Sanity Test ===\n");
    bool all_pass = true;

    /* Pick test (a,b) pairs: small coprime values */
    int64_t test_a[] = {3, -7, 11, -23, 41};
    uint64_t test_b[] = {1, 2, 1, 3, 1};
    int n_tests = 5;

    /* Find character primes (same logic as pipeline, just smaller set) */
    uint64_t cq[16]; uint64_t cr[16]; int cn = 0;
    for (uint64_t q = 2003; cn < 8 && q < 3000; q += 2) {
        if (!miller_rabin_u64(q)) continue;
        for (uint64_t r = 0; r < q && r < 10000; ++r) {
            if (gnfs_poly_eval_mod_p(poly, r, q) == 0) {
                /* Check simple root: f'(r) != 0 mod q */
                uint64_t f3q=bn_mod_u64(&poly->coeffs[3],q), f2q=bn_mod_u64(&poly->coeffs[2],q), f1q=bn_mod_u64(&poly->coeffs[1],q);
                uint64_t fp = (mulmod_u64(3,mulmod_u64(f3q,mulmod_u64(r,r,q),q),q)
                             + mulmod_u64(2,mulmod_u64(f2q,r,q),q) + f1q) % q;
                if (fp != 0) { cq[cn]=q; cr[cn]=r; ++cn; break; }
            }
        }
    }
    printf("  [COLTEST] using %d character primes\n", cn);

    /* (a) Character on known squares: Legendre((a-b*r)^2, q) must be +1 */
    int char_fail = 0;
    for (int ti = 0; ti < n_tests; ++ti) {
        int64_t a = test_a[ti]; uint64_t b = test_b[ti];
        for (int ci = 0; ci < cn; ++ci) {
            uint64_t q = cq[ci], r = cr[ci];
            /* v = (a - b*r) mod q */
            uint64_t br = mulmod_u64(b % q, r % q, q);
            uint64_t a_mod = ((a % (int64_t)q) + (int64_t)q) % q;
            uint64_t v = (a_mod + q - br) % q;
            if (v == 0) continue; /* skip degenerate */
            /* v² mod q */
            uint64_t v2 = mulmod_u64(v, v, q);
            /* Legendre(v², q) must be +1 */
            uint64_t leg = powmod_u64(v2, (q-1)/2, q);
            if (leg != 1) {
                printf("  [COLTEST] FAIL(a): Legendre((%"PRId64"-%"PRIu64"*%"PRIu64")^2, %"PRIu64") = %"PRIu64" (expected 1)\n",
                    a, b, r, q, leg);
                ++char_fail; all_pass = false;
            }
        }
    }
    printf("  [COLTEST] (a) character on squares: %s (%d failures)\n", char_fail?"FAIL":"PASS", char_fail);

    /* (b) Schirokauer additivity: schk(beta^2) = 2*schk(beta) mod ell
     * Test with ell=2 and ell=3 if possible. */
    uint64_t test_ells[] = {2, 3};
    int n_ells = 2;
    for (int ei = 0; ei < n_ells; ++ei) {
        uint64_t ell = test_ells[ei];
        uint64_t ell2 = ell * ell;
        /* Check ell validity: f must not have double root mod ell */
        bool ell_ok = true;
        for (uint64_t r = 0; r < ell; ++r) {
            if (gnfs_poly_eval_mod_p(poly, r, ell) == 0) {
                uint64_t f3c=bn_mod_u64(&poly->coeffs[3],ell), f2c=bn_mod_u64(&poly->coeffs[2],ell), f1c=bn_mod_u64(&poly->coeffs[1],ell);
                uint64_t fp = (3*f3c*r*r + 2*f2c*r + f1c) % ell;
                if (fp == 0) { ell_ok = false; break; }
            }
        }
        if (!ell_ok) { printf("  [COLTEST] (b) ell=%"PRIu64": skipped (double root)\n", ell); continue; }

        uint64_t fm[3]; for(int k=0;k<3;++k) fm[k]=bn_mod_u64(&poly->coeffs[k],ell2);
        #define CT_MUL(out3,p3,q3) do{uint64_t _t[5]={0,0,0,0,0};\
            for(int _i=0;_i<3;++_i)for(int _j=0;_j<3;++_j)_t[_i+_j]=(_t[_i+_j]+((p3)[_i]*(q3)[_j])%ell2)%ell2;\
            if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*fm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*fm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*fm[0])%ell2)%ell2;}\
            if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*fm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*fm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*fm[0])%ell2)%ell2;}\
            (out3)[0]=_t[0];(out3)[1]=_t[1];(out3)[2]=_t[2];}while(0)

        uint64_t schk_exp = (ell*ell - 1) * (ell*ell + ell + 1);
        int add_fail = 0;
        for (int ti = 0; ti < n_tests; ++ti) {
            int64_t a = test_a[ti]; uint64_t b = test_b[ti];
            /* Check norm not divisible by ell */
            uint64_t bell=b%ell, aell=((a%(int64_t)ell)+(int64_t)ell)%(int64_t)ell;
            uint64_t f0e=bn_mod_u64(&poly->coeffs[0],ell),f1e=bn_mod_u64(&poly->coeffs[1],ell);
            uint64_t f2e=bn_mod_u64(&poly->coeffs[2],ell),f3e=bn_mod_u64(&poly->coeffs[3],ell);
            uint64_t a2e=aell*aell%ell, b2e=bell*bell%ell;
            uint64_t nm=(f3e*a2e%ell*aell%ell + f2e*a2e%ell*bell%ell + f1e*aell%ell*b2e%ell + f0e*b2e%ell*bell%ell)%ell;
            if (nm == 0) continue;

            /* Compute schk(beta) where beta = (a - b*alpha) */
            uint64_t th[3];
            th[0]=(uint64_t)(((a%(int64_t)ell2)+(int64_t)ell2)%(int64_t)ell2);
            th[1]=(ell2-(b%ell2))%ell2; th[2]=0;
            uint64_t sr1[3]={1,0,0}, sb1[3]={th[0],th[1],th[2]};
            uint64_t se=schk_exp;
            while(se>0){if(se&1){uint64_t t[3];CT_MUL(t,sr1,sb1);sr1[0]=t[0];sr1[1]=t[1];sr1[2]=t[2];}
                uint64_t t2[3];CT_MUL(t2,sb1,sb1);sb1[0]=t2[0];sb1[1]=t2[1];sb1[2]=t2[2];se>>=1;}
            uint64_t w1[3];
            w1[0]=((sr1[0]+ell2-1)%ell2)/ell%ell; w1[1]=(sr1[1]/ell)%ell; w1[2]=(sr1[2]/ell)%ell;

            /* Compute schk(beta^2) by squaring beta first, then applying map */
            uint64_t th2[3]; CT_MUL(th2, th, th); /* beta^2 mod (f, ell^2) */
            uint64_t sr2[3]={1,0,0}, sb2[3]={th2[0],th2[1],th2[2]};
            se=schk_exp;
            while(se>0){if(se&1){uint64_t t[3];CT_MUL(t,sr2,sb2);sr2[0]=t[0];sr2[1]=t[1];sr2[2]=t[2];}
                uint64_t t2[3];CT_MUL(t2,sb2,sb2);sb2[0]=t2[0];sb2[1]=t2[1];sb2[2]=t2[2];se>>=1;}
            uint64_t w2[3];
            w2[0]=((sr2[0]+ell2-1)%ell2)/ell%ell; w2[1]=(sr2[1]/ell)%ell; w2[2]=(sr2[2]/ell)%ell;

            /* Check: w2[k] = 2*w1[k] mod ell for all k */
            bool ok = true;
            for (int k=0;k<3;++k) if (w2[k] != (2*w1[k])%ell) ok = false;
            if (!ok) {
                printf("  [COLTEST] FAIL(b): ell=%"PRIu64" a=%"PRId64" b=%"PRIu64": schk(β²)=[%"PRIu64",%"PRIu64",%"PRIu64"] vs 2*schk(β)=[%"PRIu64",%"PRIu64",%"PRIu64"]\n",
                    ell, a, b, w2[0],w2[1],w2[2], (2*w1[0])%ell,(2*w1[1])%ell,(2*w1[2])%ell);
                ++add_fail; all_pass = false;
            }
        }
        #undef CT_MUL
        printf("  [COLTEST] (b) Schirokauer additivity ell=%"PRIu64": %s (%d failures)\n", ell, add_fail?"FAIL":"PASS", add_fail);

        /* (c) GF(2) parity: for ell=2, schk(beta^2) mod 2 must be 0 for all coords */
        if (ell == 2) {
            /* Already tested via additivity: 2*w1[k] mod 2 = 0. So (c) is implied by (b) for ell=2. */
            printf("  [COLTEST] (c) GF(2) parity for ell=2: implied by (b) — if additivity passes, parity is correct\n");
        }
    }

    printf("  [COLTEST] === %s ===\n", all_pass?"ALL PASS":"FAILURES DETECTED");
    return all_pass;
}

/* Forward declaration */
static void alg_mul_ref(AlgElem* out, const AlgElem* a, const AlgElem* b,
                          const GNFSPoly* poly, const BigNum* n);

/* ── Minimal Fermat failure repro at specific primes ──
 * Tests: alg_mul fuzz at failing prime, Fermat, known-square QR.
 * Uses alg_mul_ref as independent reference. */
static void gnfs_selftest_field_repro(const GNFSPoly* poly) {
    printf("  [FIELD-REPRO] === Minimal Fermat Failure Repro ===\n");
    BigNum f3i; bn_from_u64(&f3i, 1u); /* monic */
    uint64_t test_primes[] = {7, 1000000087ULL, 6803284163429478607ULL};
    int ntp = 3;
    uint64_t rng = 0xDEAD1234BEEF5678ULL;

    for (int tpi = 0; tpi < ntp; ++tpi) {
        uint64_t p = test_primes[tpi];
        BigNum p_bn; bn_from_u64(&p_bn, p);
        printf("  [FIELD-REPRO] --- p=%"PRIu64" (%u bits) ---\n", p, (unsigned)(p==0?0:64-__builtin_clzll(p)));

        /* A. Fuzz alg_mul vs alg_mul_ref at this exact prime: 500 random pairs */
        int fuzz_fail = 0;
        for (int fi = 0; fi < 500; ++fi) {
            AlgElem a, b, prod_p, prod_r;
            for (int c = 0; c < 3; ++c) {
                rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
                bn_from_u64(&a.c[c], rng % p);
                rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
                bn_from_u64(&b.c[c], rng % p);
            }
            alg_mul(&prod_p, &a, &b, poly, &f3i, &p_bn);
            alg_mul_ref(&prod_r, &a, &b, poly, &p_bn);
            for (int c = 0; c < 3; ++c) {
                if (bn_cmp(&prod_p.c[c], &prod_r.c[c]) != 0) {
                    if (fuzz_fail < 3) {
                        printf("  [FIELD-REPRO] FUZZ MISMATCH #%d at case %d coeff %d:\n", fuzz_fail, fi, c);
                        printf("    a=[%"PRIu64",%"PRIu64",%"PRIu64"] b=[%"PRIu64",%"PRIu64",%"PRIu64"]\n",
                            bn_to_u64(&a.c[0]),bn_to_u64(&a.c[1]),bn_to_u64(&a.c[2]),
                            bn_to_u64(&b.c[0]),bn_to_u64(&b.c[1]),bn_to_u64(&b.c[2]));
                        printf("    prod=[%"PRIu64",%"PRIu64",%"PRIu64"] ref=[%"PRIu64",%"PRIu64",%"PRIu64"]\n",
                            bn_to_u64(&prod_p.c[0]),bn_to_u64(&prod_p.c[1]),bn_to_u64(&prod_p.c[2]),
                            bn_to_u64(&prod_r.c[0]),bn_to_u64(&prod_r.c[1]),bn_to_u64(&prod_r.c[2]));
                    }
                    ++fuzz_fail; break;
                }
            }
        }
        printf("  [FIELD-REPRO] alg_mul fuzz: %s (%d/500 failures)\n", fuzz_fail?"FAIL":"PASS", fuzz_fail);

        /* Check irreducibility: α^p mod (f,p) ≠ α */
        AlgElem alpha_e; bn_zero(&alpha_e.c[0]); bn_from_u64(&alpha_e.c[1], 1u); bn_zero(&alpha_e.c[2]);
        AlgElem irr_res; alg_one(&irr_res);
        AlgElem irr_base; for(int c=0;c<3;++c) bn_copy(&irr_base.c[c], &alpha_e.c[c]);
        uint64_t irr_exp = p;
        while(irr_exp > 0u) {
            if(irr_exp & 1u) { AlgElem t; for(int c=0;c<3;++c) bn_copy(&t.c[c], &irr_res.c[c]);
                alg_mul(&irr_res, &t, &irr_base, poly, &f3i, &p_bn); }
            AlgElem t2; for(int c=0;c<3;++c) bn_copy(&t2.c[c], &irr_base.c[c]);
            alg_mul(&irr_base, &t2, &t2, poly, &f3i, &p_bn);
            irr_exp >>= 1u;
        }
        bool no_linear = !(bn_is_zero(&irr_res.c[0]) && bn_is_one(&irr_res.c[1]) && bn_is_zero(&irr_res.c[2]));
        /* CRITICAL: for degree 3, must also check α^(p²) ≠ α.
         * If α^p ≠ α but α^(p²) = α, then f = (linear)(irreducible quadratic) mod p.
         * In that case F_p[x]/(f) ≅ F_p × F_{p²} — NOT a field. Fermat does not apply. */
        AlgElem irr_res2; alg_one(&irr_res2);
        AlgElem irr_base2; for(int c=0;c<3;++c) bn_copy(&irr_base2.c[c], &irr_res.c[c]); /* α^p */
        uint64_t irr_exp2 = p;
        while(irr_exp2 > 0u) {
            if(irr_exp2 & 1u) { AlgElem t; for(int c=0;c<3;++c) bn_copy(&t.c[c], &irr_res2.c[c]);
                alg_mul(&irr_res2, &t, &irr_base2, poly, &f3i, &p_bn); }
            AlgElem t2; for(int c=0;c<3;++c) bn_copy(&t2.c[c], &irr_base2.c[c]);
            alg_mul(&irr_base2, &t2, &t2, poly, &f3i, &p_bn);
            irr_exp2 >>= 1u;
        }
        /* irr_res2 = (α^p)^p = α^(p²) */
        bool no_quadratic = !(bn_is_zero(&irr_res2.c[0]) && bn_is_one(&irr_res2.c[1]) && bn_is_zero(&irr_res2.c[2]));
        bool is_irr = no_linear && no_quadratic;
        /* Also check no root by brute force for small p */
        bool has_root = false;
        if (p < 100000) {
            for (uint64_t r = 0; r < p; ++r) {
                if (gnfs_poly_eval_mod_p(poly, r, p) == 0) { has_root = true; break; }
            }
        }
        printf("  [FIELD-REPRO] α^p≠α: %s  α^(p²)≠α: %s  irreducible: %s",
            no_linear?"yes":"NO", no_quadratic?"yes":"NO(has quadratic factor!)", is_irr?"YES":"NO");
        if (p < 100000) printf(" brute=%s", has_root?"root_found":"no_root");
        printf("\n");
        if (!is_irr) {
            if (no_linear && !no_quadratic)
                printf("  [FIELD-REPRO] **ROOT CAUSE**: f = (linear)(irred. quadratic) mod p. F_p[x]/(f) is NOT a field!\n");
            printf("  [FIELD-REPRO] SKIP: f reducible mod p, Fermat not applicable\n");
            continue;
        }

        /* B. Fermat: g^(p³-1) = 1. Use g = (3 - 2α) */
        AlgElem g; bn_from_u64(&g.c[0], 3u); bn_from_u64(&g.c[1], p - 2u); bn_zero(&g.c[2]);
        BigNum fermat_exp; gnfs_qr_exp_from_prime(&fermat_exp, p); bn_shl1(&fermat_exp);
        /* Verify exponent = p³-1 independently */
        { BigNum vp; bn_from_u64(&vp, p); BigNum vp2; bn_mul(&vp2, &vp, &vp);
          BigNum vp3; bn_mul(&vp3, &vp2, &vp); BigNum one; bn_from_u64(&one, 1u); bn_sub(&vp3, &one);
          bool exp_ok = (bn_cmp(&fermat_exp, &vp3) == 0);
          printf("  [FIELD-REPRO] exponent p³-1: %u bits, matches independent computation: %s\n",
              bn_bitlen(&fermat_exp), exp_ok?"YES":"**NO**");
          if (!exp_ok) {
              char a_str[256], b_str[256];
              bn_to_str(&fermat_exp, a_str, sizeof(a_str)); bn_to_str(&vp3, b_str, sizeof(b_str));
              printf("    computed=%s\n    expected=%s\n", a_str, b_str);
          }
        }
        unsigned fbits = bn_bitlen(&fermat_exp);
        /* Production exponentiation */
        AlgElem fres; alg_one(&fres);
        AlgElem fbase; for(int c=0;c<3;++c) bn_copy(&fbase.c[c], &g.c[c]);
        for(unsigned bt=0;bt<fbits;++bt){
            if(fermat_exp.limbs[bt/64]&(1ull<<(bt%64))){
                AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&fres.c[c]);
                alg_mul(&fres,&t,&fbase,poly,&f3i,&p_bn);}
            AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&fbase.c[c]);
            alg_mul(&fbase,&t2,&t2,poly,&f3i,&p_bn);}
        bool fermat_prod = bn_is_one(&fres.c[0])&&bn_is_zero(&fres.c[1])&&bn_is_zero(&fres.c[2]);
        printf("  [FIELD-REPRO] Fermat(production): g^(p³-1)=[%"PRIu64",%"PRIu64",%"PRIu64"] %s\n",
            bn_to_u64(&fres.c[0]),bn_to_u64(&fres.c[1]),bn_to_u64(&fres.c[2]),
            fermat_prod?"PASS":"**FAIL**");

        /* Reference exponentiation (using alg_mul_ref) */
        AlgElem rres; alg_one(&rres);
        AlgElem rbase; for(int c=0;c<3;++c) bn_copy(&rbase.c[c], &g.c[c]);
        for(unsigned bt=0;bt<fbits;++bt){
            if(fermat_exp.limbs[bt/64]&(1ull<<(bt%64))){
                AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&rres.c[c]);
                alg_mul_ref(&rres,&t,&rbase,poly,&p_bn);}
            AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&rbase.c[c]);
            alg_mul_ref(&rbase,&t2,&t2,poly,&p_bn);}
        bool fermat_ref = bn_is_one(&rres.c[0])&&bn_is_zero(&rres.c[1])&&bn_is_zero(&rres.c[2]);
        printf("  [FIELD-REPRO] Fermat(reference):  g^(p³-1)=[%"PRIu64",%"PRIu64",%"PRIu64"] %s\n",
            bn_to_u64(&rres.c[0]),bn_to_u64(&rres.c[1]),bn_to_u64(&rres.c[2]),
            fermat_ref?"PASS":"**FAIL**");

        /* Compare production vs reference */
        bool agree = true;
        for(int c=0;c<3;++c) if(bn_cmp(&fres.c[c],&rres.c[c])!=0) agree=false;
        printf("  [FIELD-REPRO] prod==ref: %s\n", agree?"YES":"**NO — production/reference diverge**");

        /* D. Known-square QR: s=g², QR(s) must be 1 */
        AlgElem s; { AlgElem t; for(int c=0;c<3;++c)bn_copy(&t.c[c],&g.c[c]);
            alg_mul(&s, &t, &g, poly, &f3i, &p_bn); }
        BigNum qr_exp; gnfs_qr_exp_from_prime(&qr_exp, p);
        unsigned qbits = bn_bitlen(&qr_exp);
        AlgElem qres; alg_one(&qres);
        AlgElem qbase; for(int c=0;c<3;++c) bn_copy(&qbase.c[c], &s.c[c]);
        for(unsigned bt=0;bt<qbits;++bt){
            if(qr_exp.limbs[bt/64]&(1ull<<(bt%64))){
                AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&qres.c[c]);
                alg_mul(&qres,&t,&qbase,poly,&f3i,&p_bn);}
            AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&qbase.c[c]);
            alg_mul(&qbase,&t2,&t2,poly,&f3i,&p_bn);}
        bool sq_qr = bn_is_one(&qres.c[0])&&bn_is_zero(&qres.c[1])&&bn_is_zero(&qres.c[2]);
        printf("  [FIELD-REPRO] QR(g²): [%"PRIu64",%"PRIu64",%"PRIu64"] %s\n",
            bn_to_u64(&qres.c[0]),bn_to_u64(&qres.c[1]),bn_to_u64(&qres.c[2]),
            sq_qr?"PASS (is QR)":"**FAIL (known square not QR)**");
        /* Same with reference multiply */
        AlgElem s_ref; { AlgElem t; for(int c=0;c<3;++c)bn_copy(&t.c[c],&g.c[c]);
            alg_mul_ref(&s_ref, &t, &g, poly, &p_bn); }
        AlgElem qres_ref; alg_one(&qres_ref);
        AlgElem qbase_ref; for(int c=0;c<3;++c) bn_copy(&qbase_ref.c[c], &s_ref.c[c]);
        for(unsigned bt=0;bt<qbits;++bt){
            if(qr_exp.limbs[bt/64]&(1ull<<(bt%64))){
                AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&qres_ref.c[c]);
                alg_mul_ref(&qres_ref,&t,&qbase_ref,poly,&p_bn);}
            AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&qbase_ref.c[c]);
            alg_mul_ref(&qbase_ref,&t2,&t2,poly,&p_bn);}
        bool sq_qr_ref = bn_is_one(&qres_ref.c[0])&&bn_is_zero(&qres_ref.c[1])&&bn_is_zero(&qres_ref.c[2]);
        printf("  [FIELD-REPRO] QR(g²) ref: [%"PRIu64",%"PRIu64",%"PRIu64"] %s\n",
            bn_to_u64(&qres_ref.c[0]),bn_to_u64(&qres_ref.c[1]),bn_to_u64(&qres_ref.c[2]),
            sq_qr_ref?"PASS":"**FAIL**");
    }
    printf("  [FIELD-REPRO] === done ===\n");
}

/* ── Schirokauer degeneracy test ──
 * Tests whether the ℓ=2 Schirokauer map is a valid square-class detector.
 * Uses actual integer relations (a, b), NOT F_p residues.
 * For each test relation: compute λ₂(a-bα), and check QR status at irreducible primes. */
static void gnfs_selftest_schk_degeneracy(const GNFSPoly* poly) {
    printf("  [SCHK-DEGEN] === Schirokauer ell=2 Degeneracy Test ===\n");

    uint64_t ell = 2, ell2 = 4;
    uint64_t fm[3];
    for (int k = 0; k < 3; ++k) fm[k] = bn_mod_u64(&poly->coeffs[k], ell2);
    uint64_t schk_exp = 21;

    #define SCHK_LAMBDA(a_val, b_val, out_w) do { \
        uint64_t _th[3]; \
        _th[0] = (uint64_t)((((a_val) % (int64_t)ell2) + (int64_t)ell2) % (int64_t)ell2); \
        _th[1] = (ell2 - ((b_val) % ell2)) % ell2; _th[2] = 0; \
        uint64_t _sr[3]={1,0,0}, _sb[3]={_th[0],_th[1],_th[2]}; \
        uint64_t _se = schk_exp; \
        while (_se > 0u) { \
            if (_se & 1u) { uint64_t _t[5]={0,0,0,0,0}; \
                for(int _i=0;_i<3;++_i) for(int _j=0;_j<3;++_j) \
                    _t[_i+_j] = (_t[_i+_j] + ((_sr)[_i]*(_sb)[_j]) % ell2) % ell2; \
                if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*fm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*fm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*fm[0])%ell2)%ell2;} \
                if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*fm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*fm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*fm[0])%ell2)%ell2;} \
                _sr[0]=_t[0]; _sr[1]=_t[1]; _sr[2]=_t[2]; } \
            { uint64_t _t[5]={0,0,0,0,0}; \
              for(int _i=0;_i<3;++_i) for(int _j=0;_j<3;++_j) \
                  _t[_i+_j] = (_t[_i+_j] + ((_sb)[_i]*(_sb)[_j]) % ell2) % ell2; \
              if(_t[4]){_t[3]=(_t[3]+ell2-(_t[4]*fm[2])%ell2)%ell2;_t[2]=(_t[2]+ell2-(_t[4]*fm[1])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[4]*fm[0])%ell2)%ell2;} \
              if(_t[3]){_t[2]=(_t[2]+ell2-(_t[3]*fm[2])%ell2)%ell2;_t[1]=(_t[1]+ell2-(_t[3]*fm[1])%ell2)%ell2;_t[0]=(_t[0]+ell2-(_t[3]*fm[0])%ell2)%ell2;} \
              _sb[0]=_t[0]; _sb[1]=_t[1]; _sb[2]=_t[2]; } \
            _se >>= 1u; } \
        (out_w)[0] = ((_sr[0] + ell2 - 1u) % ell2) / ell % ell; \
        (out_w)[1] = (_sr[1] / ell) % ell; \
        (out_w)[2] = (_sr[2] / ell) % ell; \
    } while(0)

    uint64_t test_p[20]; int np = 0;
    for (uint64_t c = 3; c < 200 && np < 10; c += 2) {
        if (!miller_rabin_u64(c)) continue;
        bool has_root = false;
        for (uint64_t r = 0; r < c; ++r)
            if (gnfs_poly_eval_mod_p(poly, r, c) == 0) { has_root = true; break; }
        if (has_root) continue;
        test_p[np++] = c;
    }

    BigNum f3i; bn_from_u64(&f3i, 1u);
    int64_t test_a[] = {-3, -7, -13, -99, 5, 17, -1000, -9999, 42, -55555};
    uint64_t test_b[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    int n_tests = 10;

    printf("  [SCHK-DEGEN] f mod 4: [%"PRIu64",%"PRIu64",%"PRIu64"]\n", fm[0], fm[1], fm[2]);
    printf("  [SCHK-DEGEN] %-8s %-8s %-12s %-6s %-6s\n", "a", "b", "lambda", "add?", "QR@p5");

    int nqr_with_lambda0 = 0, nqr_total = 0;

    for (int ti = 0; ti < n_tests; ++ti) {
        int64_t a = test_a[ti]; uint64_t b = test_b[ti];
        uint64_t w1[3]; SCHK_LAMBDA(a, b, w1);

        int qr_count = 0;
        for (int pi = 0; pi < (np < 3 ? np : 3); ++pi) {
            uint64_t p = test_p[pi];
            BigNum p_bn; bn_from_u64(&p_bn, p);
            AlgElem g; alg_from_ab(&g, a, b, &p_bn);
            BigNum qr_exp; gnfs_qr_exp_from_prime(&qr_exp, p);
            unsigned qr_bits = bn_bitlen(&qr_exp);
            AlgElem qc; alg_one(&qc);
            AlgElem qb; for(int c=0;c<3;++c) bn_copy(&qb.c[c], &g.c[c]);
            for(unsigned bt=0;bt<qr_bits;++bt){
                if(qr_exp.limbs[bt/64]&(1ull<<(bt%64))){AlgElem t;for(int c=0;c<3;++c)bn_copy(&t.c[c],&qc.c[c]);alg_mul(&qc,&t,&qb,poly,&f3i,&p_bn);}
                AlgElem t2;for(int c=0;c<3;++c)bn_copy(&t2.c[c],&qb.c[c]);alg_mul(&qb,&t2,&t2,poly,&f3i,&p_bn);}
            if(bn_is_one(&qc.c[0])&&bn_is_zero(&qc.c[1])&&bn_is_zero(&qc.c[2])) ++qr_count;
        }
        int qr_tested = (np < 3 ? np : 3);
        bool is_nqr = (qr_count < qr_tested);
        bool lambda_zero = (w1[0]==0 && w1[1]==0 && w1[2]==0);

        if (is_nqr) { ++nqr_total; if (lambda_zero) ++nqr_with_lambda0; }

        printf("  [SCHK-DEGEN] a=%-6"PRId64" b=%-2"PRIu64" λ=(%"PRIu64",%"PRIu64",%"PRIu64") QR=%d/%d %s%s\n",
            a, b, w1[0], w1[1], w1[2], qr_count, qr_tested,
            is_nqr ? "NQR" : "QR",
            (is_nqr && lambda_zero) ? " **NQR but λ=0**" : "");
    }

    #undef SCHK_LAMBDA

    printf("  [SCHK-DEGEN] === Summary ===\n");
    printf("  [SCHK-DEGEN] Non-QR elements with λ=0: %d/%d\n", nqr_with_lambda0, nqr_total);
    if (nqr_with_lambda0 == 0 && nqr_total > 0)
        printf("  [SCHK-DEGEN] VERDICT: ell=2 Schirokauer detects ALL tested non-squares. Map is functional.\n");
    else if (nqr_with_lambda0 > 0)
        printf("  [SCHK-DEGEN] VERDICT: ell=2 Schirokauer MISSES %d/%d non-squares. Possible degeneracy.\n",
            nqr_with_lambda0, nqr_total);
    else
        printf("  [SCHK-DEGEN] VERDICT: no non-QR elements found in test set.\n");
}

/* ── Reference oracle for alg_mul ──
 * Obviously-correct schoolbook multiply on 5 BigNum coefficients + explicit
 * α³ and α⁴ reduction. For monic degree-3 f only (f3=1).
 * This does NOT share any code path with alg_mul. */
static void alg_mul_ref(AlgElem* out, const AlgElem* a, const AlgElem* b,
                          const GNFSPoly* poly, const BigNum* n) {
    /* 5-coeff product: p[i+j] += a[i]*b[j] mod n */
    BigNum p[5];
    for (int i = 0; i < 5; ++i) bn_zero(&p[i]);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            BigNum t; bn_mod_mul(&t, &a->c[i], &b->c[j], n);
            bn_add(&p[i+j], &t);
            if (bn_cmp(&p[i+j], n) >= 0) bn_sub(&p[i+j], n);
        }
    }
    /* Reduce α⁴: α⁴ = α·α³ = α·(-c2α²-c1α-c0)
     * So p[4]*α⁴ contributes: p[3] += -p[4]*c2, p[2] += -p[4]*c1, p[1] += -p[4]*c0  (all mod n) */
    if (!bn_is_zero(&p[4])) {
        BigNum neg_p4; bn_copy(&neg_p4, n); bn_sub(&neg_p4, &p[4]); /* neg_p4 = -p[4] mod n */
        for (int k = 0; k < 3; ++k) {
            BigNum contrib; bn_mod_mul(&contrib, &neg_p4, &poly->coeffs[k], n);
            bn_add(&p[k+1], &contrib);
            if (bn_cmp(&p[k+1], n) >= 0) bn_sub(&p[k+1], n);
        }
        bn_zero(&p[4]);
    }
    /* Reduce α³: α³ = -c2α²-c1α-c0
     * So p[3]*α³ contributes: p[k] += -p[3]*c_k for k=0,1,2 */
    if (!bn_is_zero(&p[3])) {
        BigNum neg_p3; bn_copy(&neg_p3, n); bn_sub(&neg_p3, &p[3]);
        for (int k = 0; k < 3; ++k) {
            BigNum contrib; bn_mod_mul(&contrib, &neg_p3, &poly->coeffs[k], n);
            bn_add(&p[k], &contrib);
            if (bn_cmp(&p[k], n) >= 0) bn_sub(&p[k], n);
        }
    }
    for (int i = 0; i < 3; ++i) bn_copy(&out->c[i], &p[i]);
}

/* ── Fuzz test: alg_mul vs alg_mul_ref ──
 * Runs 1000+ random cases per modulus size (1, 2, 4, 8 limbs).
 * Returns true if all cases match. */
static bool gnfs_selftest_alg_mul(const GNFSPoly* poly) {
    printf("  [SELFTEST] === alg_mul reference oracle fuzz test ===\n");
    BigNum f3i; bn_from_u64(&f3i, 1u); /* monic */
    int total = 0, fail = 0;
    uint64_t rng = 0xABCD1234DEAD5678ULL;

    int limb_sizes[] = {1, 2, 4, 8};
    int num_sizes = 4;
    int cases_per_size = 1000;

    for (int si = 0; si < num_sizes; ++si) {
        int nlimbs = limb_sizes[si];
        /* Build a modulus of the target size */
        BigNum mod; bn_zero(&mod);
        mod.used = (uint32_t)nlimbs;
        for (int li = 0; li < nlimbs; ++li) {
            rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17;
            mod.limbs[li] = rng | 1u; /* ensure odd for some safety */
        }
        mod.limbs[nlimbs-1] |= ((uint64_t)1 << 63); /* ensure top limb is large */
        bn_normalize(&mod);

        int size_fail = 0;
        for (int ci = 0; ci < cases_per_size; ++ci) {
            /* Random A, B with coefficients < mod */
            AlgElem A, B;
            for (int c = 0; c < 3; ++c) {
                rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17;
                if (nlimbs == 1) {
                    bn_from_u64(&A.c[c], rng % mod.limbs[0]);
                    rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17;
                    bn_from_u64(&B.c[c], rng % mod.limbs[0]);
                } else {
                    /* Multi-limb: fill with random, then reduce mod */
                    bn_zero(&A.c[c]); bn_zero(&B.c[c]);
                    A.c[c].used = (uint32_t)nlimbs; B.c[c].used = (uint32_t)nlimbs;
                    for (int li = 0; li < nlimbs; ++li) {
                        rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17;
                        A.c[c].limbs[li] = rng;
                        rng ^= rng<<13; rng ^= rng>>7; rng ^= rng<<17;
                        B.c[c].limbs[li] = rng;
                    }
                    bn_normalize(&A.c[c]); bn_normalize(&B.c[c]);
                    /* Reduce: A.c[c] = A.c[c] mod mod via bn_mod_u64 won't work for multi-limb.
                     * Use: while (A >= mod) A -= mod. Slow but correct for test. */
                    while (bn_cmp(&A.c[c], &mod) >= 0) bn_sub(&A.c[c], &mod);
                    while (bn_cmp(&B.c[c], &mod) >= 0) bn_sub(&B.c[c], &mod);
                }
            }

            /* Compute with both implementations */
            AlgElem R1, R2;
            alg_mul(&R1, &A, &B, poly, &f3i, &mod);
            alg_mul_ref(&R2, &A, &B, poly, &mod);

            /* Compare */
            bool match = true;
            for (int c = 0; c < 3; ++c) {
                if (bn_cmp(&R1.c[c], &R2.c[c]) != 0) { match = false; break; }
            }
            if (!match) {
                ++size_fail;
                if (size_fail <= 3) {
                    printf("  [SELFTEST] MISMATCH at %d limbs, case %d:\n", nlimbs, ci);
                    for (int c = 0; c < 3; ++c) {
                        printf("    c[%d]: alg_mul=%"PRIu64" ref=%"PRIu64"\n", c,
                            bn_to_u64(&R1.c[c]), bn_to_u64(&R2.c[c]));
                    }
                }
            }
            ++total;
        }
        printf("  [SELFTEST] %d limbs: %d/%d passed%s\n", nlimbs,
            cases_per_size - size_fail, cases_per_size,
            size_fail ? " **FAILURES**" : "");
        fail += size_fail;
    }
    printf("  [SELFTEST] === total: %d/%d passed %s ===\n", total - fail, total,
        fail ? "**FAIL**" : "PASS");
    return (fail == 0);
}
