/**
 * nql_fft.c - NQL FFT / NTT functions.
 * Part of the CPSS Viewer amalgamation.
 *
 * Complex FFT (radix-2 Cooley-Tukey), inverse FFT, number-theoretic
 * transform, convolution, and power spectrum.
 */

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);
static uint64_t mulmod_u64(uint64_t a, uint64_t b, uint64_t m);
static uint64_t powmod_u64(uint64_t base, uint64_t exp, uint64_t mod);

/* ======================================================================
 * FFT INTERNALS
 * ====================================================================== */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** In-place iterative radix-2 Cooley-Tukey FFT.
 *  re/im arrays of length n (must be power of 2).
 *  inverse=true for IFFT (does NOT divide by n). */
static void nql_fft_core(double* re, double* im, uint32_t n, bool inverse) {
    /* Bit-reversal permutation */
    uint32_t log2n = 0;
    for (uint32_t t = n; t > 1; t >>= 1) log2n++;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = 0;
        for (uint32_t b = 0; b < log2n; b++)
            if (i & (1u << b)) j |= (1u << (log2n - 1 - b));
        if (j > i) {
            double tr = re[i]; re[i] = re[j]; re[j] = tr;
            double ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
    /* Butterfly stages */
    for (uint32_t s = 1; s <= log2n; s++) {
        uint32_t m = 1u << s;
        double angle = (inverse ? 2.0 : -2.0) * M_PI / (double)m;
        double wm_re = cos(angle), wm_im = sin(angle);
        for (uint32_t k = 0; k < n; k += m) {
            double w_re = 1.0, w_im = 0.0;
            for (uint32_t j = 0; j < m / 2; j++) {
                double t_re = w_re * re[k + j + m/2] - w_im * im[k + j + m/2];
                double t_im = w_re * im[k + j + m/2] + w_im * re[k + j + m/2];
                double u_re = re[k + j], u_im = im[k + j];
                re[k + j] = u_re + t_re;
                im[k + j] = u_im + t_im;
                re[k + j + m/2] = u_re - t_re;
                im[k + j + m/2] = u_im - t_im;
                double new_w_re = w_re * wm_re - w_im * wm_im;
                double new_w_im = w_re * wm_im + w_im * wm_re;
                w_re = new_w_re; w_im = new_w_im;
            }
        }
    }
}

/** Round up to next power of 2. */
static uint32_t nql_next_pow2(uint32_t n) {
    uint32_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

/* ======================================================================
 * FFT NQL FUNCTIONS
 * ====================================================================== */

/** FFT(array) -- complex FFT, returns array of [re, im] pairs */
static NqlValue nql_fn_fft(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    uint32_t n = nql_next_pow2(arr->length);
    if (n > 65536) return nql_val_null();
    double* re = (double*)calloc(n, sizeof(double));
    double* im = (double*)calloc(n, sizeof(double));
    if (!re || !im) { free(re); free(im); return nql_val_null(); }
    for (uint32_t i = 0; i < arr->length; i++)
        re[i] = nql_val_as_float(&arr->items[i]);
    nql_fft_core(re, im, n, false);
    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(re); free(im); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) {
        NqlArray* pair = nql_array_alloc(2);
        if (pair) {
            nql_array_push(pair, nql_val_float(re[i]));
            nql_array_push(pair, nql_val_float(im[i]));
            nql_array_push(result, nql_val_array(pair));
        }
    }
    free(re); free(im);
    return nql_val_array(result);
}

/** IFFT(array_of_pairs) -- inverse FFT, returns array of real values */
static NqlValue nql_fn_ifft(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    uint32_t n = nql_next_pow2(arr->length);
    if (n > 65536) return nql_val_null();
    double* re = (double*)calloc(n, sizeof(double));
    double* im = (double*)calloc(n, sizeof(double));
    if (!re || !im) { free(re); free(im); return nql_val_null(); }
    for (uint32_t i = 0; i < arr->length; i++) {
        if (arr->items[i].type == NQL_VAL_ARRAY && arr->items[i].v.array && arr->items[i].v.array->length >= 2) {
            re[i] = nql_val_as_float(&arr->items[i].v.array->items[0]);
            im[i] = nql_val_as_float(&arr->items[i].v.array->items[1]);
        } else {
            re[i] = nql_val_as_float(&arr->items[i]);
        }
    }
    nql_fft_core(re, im, n, true);
    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(re); free(im); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++)
        nql_array_push(result, nql_val_float(re[i] / (double)n));
    free(re); free(im);
    return nql_val_array(result);
}

/** POWER_SPECTRUM(array) -- |FFT(x)|^2 for each frequency bin */
static NqlValue nql_fn_power_spectrum(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    uint32_t n = nql_next_pow2(arr->length);
    if (n > 65536) return nql_val_null();
    double* re = (double*)calloc(n, sizeof(double));
    double* im = (double*)calloc(n, sizeof(double));
    if (!re || !im) { free(re); free(im); return nql_val_null(); }
    for (uint32_t i = 0; i < arr->length; i++)
        re[i] = nql_val_as_float(&arr->items[i]);
    nql_fft_core(re, im, n, false);
    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(re); free(im); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++)
        nql_array_push(result, nql_val_float(re[i] * re[i] + im[i] * im[i]));
    free(re); free(im);
    return nql_val_array(result);
}

/** CONVOLUTION(a, b) -- polynomial multiplication via FFT */
static NqlValue nql_fn_convolution(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    uint32_t result_len = a->length + b->length - 1;
    uint32_t n = nql_next_pow2(result_len);
    if (n > 65536) return nql_val_null();
    double* ar = (double*)calloc(n, sizeof(double));
    double* ai = (double*)calloc(n, sizeof(double));
    double* br = (double*)calloc(n, sizeof(double));
    double* bi = (double*)calloc(n, sizeof(double));
    if (!ar || !ai || !br || !bi) { free(ar); free(ai); free(br); free(bi); return nql_val_null(); }
    for (uint32_t i = 0; i < a->length; i++) ar[i] = nql_val_as_float(&a->items[i]);
    for (uint32_t i = 0; i < b->length; i++) br[i] = nql_val_as_float(&b->items[i]);
    nql_fft_core(ar, ai, n, false);
    nql_fft_core(br, bi, n, false);
    /* Pointwise multiply */
    for (uint32_t i = 0; i < n; i++) {
        double tr = ar[i] * br[i] - ai[i] * bi[i];
        double ti = ar[i] * bi[i] + ai[i] * br[i];
        ar[i] = tr; ai[i] = ti;
    }
    nql_fft_core(ar, ai, n, true);
    NqlArray* result = nql_array_alloc(result_len);
    if (!result) { free(ar); free(ai); free(br); free(bi); return nql_val_null(); }
    for (uint32_t i = 0; i < result_len; i++)
        nql_array_push(result, nql_val_float(ar[i] / (double)n));
    free(ar); free(ai); free(br); free(bi);
    return nql_val_array(result);
}

/** NTT(array, prime) -- number-theoretic transform mod prime */
static NqlValue nql_fn_ntt(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    int64_t p = nql_val_as_int(&args[1]);
    if (p <= 1) return nql_val_null();
    uint32_t n = nql_next_pow2(arr->length);
    if (n > 65536) return nql_val_null();
    /* Check that p-1 is divisible by n */
    if ((p - 1) % n != 0) return nql_val_null();
    /* Find primitive root of p */
    uint64_t g = 0;
    { uint64_t pm1 = (uint64_t)(p - 1), tmp = pm1;
      uint64_t factors[64]; int nf = 0;
      for (uint64_t d = 2; d * d <= tmp && nf < 64; d++) {
          if (tmp % d == 0) { factors[nf++] = d; while (tmp % d == 0) tmp /= d; }
      }
      if (tmp > 1 && nf < 64) factors[nf++] = tmp;
      for (uint64_t cand = 2; cand < (uint64_t)p; cand++) {
          bool ok = true;
          for (int i = 0; i < nf; i++)
              if (powmod_u64(cand, pm1 / factors[i], (uint64_t)p) == 1) { ok = false; break; }
          if (ok) { g = cand; break; }
      }
    }
    if (g == 0) return nql_val_null();
    uint64_t w = powmod_u64(g, (uint64_t)(p - 1) / n, (uint64_t)p);
    int64_t* data = (int64_t*)calloc(n, sizeof(int64_t));
    if (!data) return nql_val_null();
    for (uint32_t i = 0; i < arr->length; i++)
        data[i] = ((nql_val_as_int(&arr->items[i]) % p) + p) % p;
    /* Bit-reversal */
    uint32_t log2n = 0;
    for (uint32_t t = n; t > 1; t >>= 1) log2n++;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = 0;
        for (uint32_t b = 0; b < log2n; b++)
            if (i & (1u << b)) j |= (1u << (log2n - 1 - b));
        if (j > i) { int64_t t = data[i]; data[i] = data[j]; data[j] = t; }
    }
    /* Butterfly */
    for (uint32_t s = 1; s <= log2n; s++) {
        uint32_t m = 1u << s;
        uint64_t wm = powmod_u64(g, (uint64_t)(p - 1) / m, (uint64_t)p);
        for (uint32_t k = 0; k < n; k += m) {
            uint64_t wj = 1;
            for (uint32_t j = 0; j < m / 2; j++) {
                int64_t t = (int64_t)mulmod_u64(wj, (uint64_t)data[k + j + m/2], (uint64_t)p);
                int64_t u = data[k + j];
                data[k + j] = (u + t) % p;
                data[k + j + m/2] = ((u - t) % p + p) % p;
                wj = mulmod_u64(wj, wm, (uint64_t)p);
            }
        }
    }
    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(data); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++)
        nql_array_push(result, nql_val_int(data[i]));
    free(data);
    return nql_val_array(result);
}

/** INTT(array, prime) -- inverse NTT */
static NqlValue nql_fn_intt(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    int64_t p = nql_val_as_int(&args[1]);
    if (p <= 1) return nql_val_null();
    uint32_t n = nql_next_pow2(arr->length);
    if (n > 65536 || (p - 1) % n != 0) return nql_val_null();
    /* Find primitive root */
    uint64_t g = 0;
    { uint64_t pm1 = (uint64_t)(p - 1), tmp = pm1;
      uint64_t factors[64]; int nf = 0;
      for (uint64_t d = 2; d * d <= tmp && nf < 64; d++) {
          if (tmp % d == 0) { factors[nf++] = d; while (tmp % d == 0) tmp /= d; }
      }
      if (tmp > 1 && nf < 64) factors[nf++] = tmp;
      for (uint64_t cand = 2; cand < (uint64_t)p; cand++) {
          bool ok = true;
          for (int i = 0; i < nf; i++)
              if (powmod_u64(cand, pm1 / factors[i], (uint64_t)p) == 1) { ok = false; break; }
          if (ok) { g = cand; break; }
      }
    }
    if (g == 0) return nql_val_null();
    /* Use inverse root: g^((p-1) - (p-1)/n) = g^(-(p-1)/n) mod p */
    uint64_t w_inv = powmod_u64(g, (uint64_t)(p - 1) - (uint64_t)(p - 1) / n, (uint64_t)p);
    int64_t* data = (int64_t*)calloc(n, sizeof(int64_t));
    if (!data) return nql_val_null();
    for (uint32_t i = 0; i < arr->length; i++)
        data[i] = ((nql_val_as_int(&arr->items[i]) % p) + p) % p;
    /* Bit-reversal */
    uint32_t log2n = 0;
    for (uint32_t t = n; t > 1; t >>= 1) log2n++;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = 0;
        for (uint32_t b = 0; b < log2n; b++)
            if (i & (1u << b)) j |= (1u << (log2n - 1 - b));
        if (j > i) { int64_t t = data[i]; data[i] = data[j]; data[j] = t; }
    }
    /* Butterfly with inverse root */
    for (uint32_t s = 1; s <= log2n; s++) {
        uint32_t m = 1u << s;
        uint64_t wm = powmod_u64(g, (uint64_t)(p - 1) - (uint64_t)(p - 1) / m, (uint64_t)p);
        for (uint32_t k = 0; k < n; k += m) {
            uint64_t wj = 1;
            for (uint32_t j = 0; j < m / 2; j++) {
                int64_t t = (int64_t)mulmod_u64(wj, (uint64_t)data[k + j + m/2], (uint64_t)p);
                int64_t u = data[k + j];
                data[k + j] = (u + t) % p;
                data[k + j + m/2] = ((u - t) % p + p) % p;
                wj = mulmod_u64(wj, wm, (uint64_t)p);
            }
        }
    }
    /* Divide by n mod p */
    uint64_t n_inv = powmod_u64((uint64_t)n, (uint64_t)(p - 2), (uint64_t)p);
    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(data); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++)
        nql_array_push(result, nql_val_int((int64_t)mulmod_u64((uint64_t)data[i], n_inv, (uint64_t)p)));
    free(data);
    return nql_val_array(result);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_fft_register_functions(void) {
    nql_register_func("FFT",              nql_fn_fft,              1, 1, "Complex FFT (returns [[re,im],...])");
    nql_register_func("IFFT",             nql_fn_ifft,             1, 1, "Inverse FFT (returns real array)");
    nql_register_func("POWER_SPECTRUM",   nql_fn_power_spectrum,   1, 1, "|FFT(x)|^2 for each bin");
    nql_register_func("CONVOLUTION",      nql_fn_convolution,      2, 2, "Polynomial multiplication via FFT");
    nql_register_func("NTT",              nql_fn_ntt,              2, 2, "Number-theoretic transform mod prime");
    nql_register_func("INTT",             nql_fn_intt,             2, 2, "Inverse NTT mod prime");
}
