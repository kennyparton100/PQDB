/**
 * cpss_nql_funcs.c - NQL built-in function and number-type registry.
 *
 * Bridges NQL queries to the existing CPSS codebase: primality testing,
 * factorisation, prime enumeration, and number-theoretic functions.
 * Also registers all FROM-clause number types (PRIME, COMPOSITE, etc.).
 */

/* ══════════════════════════════════════════════════════════════════════
 * HELPER: DB CONTEXT
 *
 * The db_ctx pointer passed to type test/next and builtin functions
 * is a CPSSStream* (or NULL if no DB is loaded).
 * ══════════════════════════════════════════════════════════════════════ */

/** Simple primality test: uses DB if available, otherwise Miller-Rabin. */
static bool nql_is_prime_u64(uint64_t n, void* db_ctx) {
    if (n < 2u) return false;
    if (db_ctx) {
        CPSSStream* s = (CPSSStream*)db_ctx;
        return cpss_is_prime(s, n) == 1;
    }
    return miller_rabin_u64(n);
}

/** Factor n into a FactorResult. Uses auto-router if DB, else rho+fermat. */
static FactorResult nql_factor_u64(uint64_t n, void* db_ctx) {
    if (db_ctx) {
        return cpss_factor_auto((CPSSStream*)db_ctx, n);
    }
    FactorResult fr = cpss_factor_rho(n, 0u, 0u);
    if (!fr.fully_factored) fr = cpss_factor_fermat(n, 0u);
    return fr;
}

/** Integer square root (64-bit). */
static uint64_t nql_isqrt(uint64_t x) {
    return (uint64_t)isqrt_u64(x);
}

/** Integer cube root. */
static uint64_t nql_icbrt(uint64_t x) {
    if (x == 0u) return 0u;
    uint64_t r = (uint64_t)cbrt((double)x);
    while (r * r * r > x) --r;
    while ((r + 1u) * (r + 1u) * (r + 1u) <= x) ++r;
    return r;
}

/** Check if n is a perfect square. */
static bool nql_is_square(uint64_t n) {
    uint64_t r = nql_isqrt(n);
    return r * r == n;
}

/** Check if n is a perfect cube. */
static bool nql_is_cube(uint64_t n) {
    uint64_t r = nql_icbrt(n);
    return r * r * r == n;
}

/** Check if n is a perfect power (a^k for k >= 2). */
static bool nql_is_perfect_power(uint64_t n) {
    if (n <= 1u) return false;
    if (nql_is_square(n)) return true;
    if (nql_is_cube(n)) return true;
    /* Check higher powers up to log2(n) */
    for (uint32_t k = 5u; k <= 63u; k += 2u) {
        double r = pow((double)n, 1.0 / (double)k);
        uint64_t ri = (uint64_t)r;
        if (ri < 2u) break;
        /* Check ri and ri+1 */
        uint64_t pw = 1u;
        bool overflow = false;
        for (uint32_t j = 0u; j < k && !overflow; ++j) {
            if (pw > UINT64_MAX / ri) { overflow = true; break; }
            pw *= ri;
        }
        if (!overflow && pw == n) return true;
        pw = 1u; overflow = false;
        uint64_t ri1 = ri + 1u;
        for (uint32_t j = 0u; j < k && !overflow; ++j) {
            if (pw > UINT64_MAX / ri1) { overflow = true; break; }
            pw *= ri1;
        }
        if (!overflow && pw == n) return true;
    }
    return false;
}

/** Count decimal digits. */
static uint64_t nql_digit_count(uint64_t n) {
    if (n == 0u) return 1u;
    uint64_t c = 0u;
    while (n > 0u) { ++c; n /= 10u; }
    return c;
}

/** Sum of decimal digits. */
static uint64_t nql_digit_sum(uint64_t n) {
    uint64_t s = 0u;
    while (n > 0u) { s += n % 10u; n /= 10u; }
    return s;
}

/** Reverse decimal digits. */
static uint64_t nql_digit_reverse(uint64_t n) {
    uint64_t r = 0u;
    while (n > 0u) { r = r * 10u + (n % 10u); n /= 10u; }
    return r;
}

/** Is n a decimal palindrome? */
static bool nql_is_palindrome(uint64_t n) {
    return n == nql_digit_reverse(n);
}

/** Is n a Fibonacci number? (iff 5n²+4 or 5n²-4 is a perfect square) */
static bool nql_is_fibonacci(uint64_t n) {
    if (n == 0u || n == 1u) return true;
    if (n > 1000000000000000ull) return false; /* avoid overflow in 5n² */
    uint64_t n2 = n * n;
    uint64_t a = 5u * n2 + 4u;
    uint64_t b = 5u * n2 - 4u;
    return nql_is_square(a) || nql_is_square(b);
}

/** Is n a Mersenne number (2^k - 1)? */
static bool nql_is_mersenne_form(uint64_t n) {
    return n >= 1u && (n & (n + 1u)) == 0u;
}

/** Is n a Mersenne prime? */
static bool nql_is_mersenne_prime(uint64_t n, void* db_ctx) {
    return nql_is_mersenne_form(n) && nql_is_prime_u64(n, db_ctx);
}

/** Euler's totient function. */
static uint64_t nql_totient(uint64_t n, void* db_ctx) {
    if (n <= 1u) return n;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    uint64_t result = n;
    for (size_t i = 0u; i < fr.factor_count; ++i) {
        result = result / fr.factors[i] * (fr.factors[i] - 1u);
    }
    return result;
}

/** Sum of divisors (sigma function). */
static uint64_t nql_sigma(uint64_t n, void* db_ctx) {
    if (n <= 1u) return n;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    uint64_t result = 1u;
    for (size_t i = 0u; i < fr.factor_count; ++i) {
        uint64_t p = fr.factors[i];
        uint64_t e = fr.exponents[i];
        uint64_t pk = 1u;
        uint64_t s = 0u;
        for (uint64_t j = 0u; j <= e; ++j) {
            s += pk;
            pk *= p;
        }
        result *= s;
    }
    return result;
}

/** Number of divisors. */
static uint64_t nql_num_divisors(uint64_t n, void* db_ctx) {
    if (n <= 1u) return n;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    uint64_t result = 1u;
    for (size_t i = 0u; i < fr.factor_count; ++i) {
        result *= (fr.exponents[i] + 1u);
    }
    return result;
}

/** Mobius function. Returns 0, 1, or -1 (as int64). */
static int64_t nql_mobius(uint64_t n, void* db_ctx) {
    if (n <= 1u) return 1;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    for (size_t i = 0u; i < fr.factor_count; ++i) {
        if (fr.exponents[i] > 1u) return 0; /* not squarefree */
    }
    return (fr.factor_count % 2u == 0u) ? 1 : -1;
}

/** Count distinct prime factors (omega). */
static uint64_t nql_omega(uint64_t n, void* db_ctx) {
    if (n <= 1u) return 0u;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    return fr.factor_count;
}

/** Count prime factors with multiplicity (bigomega). */
static uint64_t nql_bigomega(uint64_t n, void* db_ctx) {
    if (n <= 1u) return 0u;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    uint64_t s = 0u;
    for (size_t i = 0u; i < fr.factor_count; ++i) s += fr.exponents[i];
    return s;
}

/** Smallest prime factor. */
static uint64_t nql_smallest_factor(uint64_t n, void* db_ctx) {
    if (n <= 1u) return n;
    if (n % 2u == 0u) return 2u;
    if (n % 3u == 0u) return 3u;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    if (fr.factor_count == 0u) return n;
    uint64_t m = fr.factors[0];
    for (size_t i = 1u; i < fr.factor_count; ++i) {
        if (fr.factors[i] < m) m = fr.factors[i];
    }
    return m;
}

/** Largest prime factor. */
static uint64_t nql_largest_factor(uint64_t n, void* db_ctx) {
    if (n <= 1u) return n;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    if (fr.factor_count == 0u) return n;
    uint64_t m = fr.factors[0];
    for (size_t i = 1u; i < fr.factor_count; ++i) {
        if (fr.factors[i] > m) m = fr.factors[i];
    }
    return m;
}

/** Is n B-smooth? (all prime factors <= B) */
static bool nql_is_smooth(uint64_t n, uint64_t B, void* db_ctx) {
    if (n <= 1u) return true;
    return nql_largest_factor(n, db_ctx) <= B;
}

/** Is n squarefree? (no prime factor appears more than once) */
static bool nql_is_squarefree(uint64_t n, void* db_ctx) {
    if (n <= 1u) return (n == 1u);
    FactorResult fr = nql_factor_u64(n, db_ctx);
    for (size_t i = 0u; i < fr.factor_count; ++i) {
        if (fr.exponents[i] > 1u) return false;
    }
    return true;
}

/** Is n powerful? (all prime factors p satisfy p²|n) */
static bool nql_is_powerful(uint64_t n, void* db_ctx) {
    if (n <= 1u) return (n == 1u);
    FactorResult fr = nql_factor_u64(n, db_ctx);
    for (size_t i = 0u; i < fr.factor_count; ++i) {
        if (fr.exponents[i] < 2u) return false;
    }
    return true;
}

/** Is n a semiprime? (exactly 2 prime factors counting multiplicity) */
static bool nql_is_semiprime(uint64_t n, void* db_ctx) {
    if (n < 4u) return false;
    return nql_bigomega(n, db_ctx) == 2u;
}

/** Is n composite? */
static bool nql_is_composite(uint64_t n, void* db_ctx) {
    return n > 1u && !nql_is_prime_u64(n, db_ctx);
}

/** Format factorisation as string "p1^e1 * p2^e2 ...". */
static NqlValue nql_factor_string(uint64_t n, void* db_ctx) {
    if (n <= 1u) return nql_val_str(n == 1u ? "1" : "0");
    FactorResult fr = nql_factor_u64(n, db_ctx);
    char buf[256];
    size_t off = 0u;
    for (size_t i = 0u; i < fr.factor_count && off < sizeof(buf) - 32u; ++i) {
        if (i > 0u) off += snprintf(buf + off, sizeof(buf) - off, " * ");
        if (fr.exponents[i] > 1u)
            off += snprintf(buf + off, sizeof(buf) - off, "%" PRIu64 "^%" PRIu64, fr.factors[i], fr.exponents[i]);
        else
            off += snprintf(buf + off, sizeof(buf) - off, "%" PRIu64, fr.factors[i]);
    }
    if (fr.cofactor > 1u)
        snprintf(buf + off, sizeof(buf) - off, " * %" PRIu64, fr.cofactor);
    return nql_val_str(buf);
}

/** Format divisors as comma-separated string. */
static NqlValue nql_divisors_string(uint64_t n, void* db_ctx) {
    if (n == 0u) return nql_val_str("(none)");
    if (n == 1u) return nql_val_str("1");
    /* Collect divisors by trial division */
    uint64_t divs[256];
    uint32_t count = 0u;
    for (uint64_t d = 1u; d * d <= n && count < 255u; ++d) {
        if (n % d == 0u) {
            divs[count++] = d;
            if (d != n / d && count < 255u) divs[count++] = n / d;
        }
    }
    /* Sort */
    for (uint32_t i = 0u; i < count; ++i) {
        for (uint32_t j = i + 1u; j < count; ++j) {
            if (divs[j] < divs[i]) { uint64_t tmp = divs[i]; divs[i] = divs[j]; divs[j] = tmp; }
        }
    }
    char buf[256];
    size_t off = 0u;
    for (uint32_t i = 0u; i < count && off < sizeof(buf) - 24u; ++i) {
        if (i > 0u) off += snprintf(buf + off, sizeof(buf) - off, ", ");
        off += snprintf(buf + off, sizeof(buf) - off, "%" PRIu64, divs[i]);
    }
    (void)db_ctx;
    return nql_val_str(buf);
}

/* ══════════════════════════════════════════════════════════════════════
 * BUILTIN FUNCTION WRAPPERS (NqlBuiltinFn signature)
 * ══════════════════════════════════════════════════════════════════════ */

static NqlValue nql_fn_is_prime(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_bool(nql_is_prime_u64((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_next_prime(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac;
    uint64_t n = (uint64_t)nql_val_as_int(&a[0]);
    if (ctx) return nql_val_int((int64_t)cpss_next_prime((CPSSStream*)ctx, n));
    /* Fallback: scan with Miller-Rabin */
    if (n < 2u) return nql_val_int(2);
    n = (n % 2u == 0u) ? n + 1u : n + 2u;
    while (!miller_rabin_u64(n)) n += 2u;
    return nql_val_int((int64_t)n);
}
static NqlValue nql_fn_prev_prime(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac;
    uint64_t n = (uint64_t)nql_val_as_int(&a[0]);
    if (ctx) return nql_val_int((int64_t)cpss_prev_prime((CPSSStream*)ctx, n));
    if (n <= 2u) return nql_val_int(0);
    n = (n % 2u == 0u) ? n - 1u : n - 2u;
    while (n > 1u && !miller_rabin_u64(n)) n -= 2u;
    return nql_val_int((int64_t)n);
}
static NqlValue nql_fn_pi(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac;
    if (!ctx) return nql_val_str("(requires DB)");
    return nql_val_int((int64_t)cpss_pi((CPSSStream*)ctx, (uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_nth_prime(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac;
    if (!ctx) return nql_val_str("(requires DB)");
    return nql_val_int((int64_t)cpss_nth_prime((CPSSStream*)ctx, (uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_factor(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_factor_string((uint64_t)nql_val_as_int(&a[0]), ctx);
}
static NqlValue nql_fn_smallest_factor(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_smallest_factor((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_largest_factor(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_largest_factor((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_omega(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_omega((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_bigomega(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_bigomega((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_divisors(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_divisors_string((uint64_t)nql_val_as_int(&a[0]), ctx);
}
static NqlValue nql_fn_num_divisors(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_num_divisors((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_sigma(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_sigma((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_totient(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_totient((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_mobius(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int(nql_mobius((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_gcd(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t x = (uint64_t)nql_val_as_int(&a[0]);
    uint64_t y = (uint64_t)nql_val_as_int(&a[1]);
    while (y) { uint64_t t = y; y = x % y; x = t; }
    return nql_val_int((int64_t)x);
}
static NqlValue nql_fn_lcm(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t x = (uint64_t)nql_val_as_int(&a[0]);
    uint64_t y = (uint64_t)nql_val_as_int(&a[1]);
    uint64_t g = x; uint64_t t = y;
    while (t) { uint64_t tmp = t; t = g % t; g = tmp; }
    return nql_val_int((int64_t)(x / g * y));
}
static NqlValue nql_fn_isqrt(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int((int64_t)nql_isqrt((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_pow(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t base = nql_val_as_int(&a[0]);
    int64_t exp = nql_val_as_int(&a[1]);
    if (exp < 0) return nql_val_int(0);
    int64_t r = 1;
    for (int64_t i = 0; i < exp && i < 63; ++i) r *= base;
    return nql_val_int(r);
}
static NqlValue nql_fn_mod(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t x = nql_val_as_int(&a[0]);
    int64_t y = nql_val_as_int(&a[1]);
    if (y == 0) return nql_val_null();
    return nql_val_int(x % y);
}
static NqlValue nql_fn_abs(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t x = nql_val_as_int(&a[0]);
    return nql_val_int(x < 0 ? -x : x);
}
static NqlValue nql_fn_log(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_float(log(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_digits(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int((int64_t)nql_digit_count((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_digit_sum(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int((int64_t)nql_digit_sum((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_reverse(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int((int64_t)nql_digit_reverse((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_is_palindrome(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_bool(nql_is_palindrome((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_gap(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac;
    uint64_t p = (uint64_t)nql_val_as_int(&a[0]);
    NqlValue np = nql_fn_next_prime(a, 1u, ctx);
    return nql_val_int(nql_val_as_int(&np) - (int64_t)p);
}

/* ══════════════════════════════════════════════════════════════════════
 * PHASE 1b: NEW MATH / BITWISE / NT FUNCTION WRAPPERS
 * ══════════════════════════════════════════════════════════════════════ */

static NqlValue nql_fn_floor(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_int((int64_t)floor(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_ceil(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_int((int64_t)ceil(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_round(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_int((int64_t)(nql_val_as_float(&a[0]) + 0.5));
}
static NqlValue nql_fn_sign(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t x = nql_val_as_int(&a[0]);
    return nql_val_int(x > 0 ? 1 : (x < 0 ? -1 : 0));
}
static NqlValue nql_fn_sqrt(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(sqrt(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_log2(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(log2(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_log10(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(log10(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_factorial(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t n = nql_val_as_int(&a[0]);
    if (n < 0 || n > 20) return nql_val_null(); /* overflow guard */
    int64_t r = 1;
    for (int64_t i = 2; i <= n; ++i) r *= i;
    return nql_val_int(r);
}
static NqlValue nql_fn_binomial(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t n = nql_val_as_int(&a[0]);
    int64_t k = nql_val_as_int(&a[1]);
    if (k < 0 || k > n || n > 62) return nql_val_int(0);
    if (k > n - k) k = n - k;
    int64_t r = 1;
    for (int64_t i = 0; i < k; ++i) { r = r * (n - i) / (i + 1); }
    return nql_val_int(r);
}
static NqlValue nql_fn_fibonacci_n(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t n = nql_val_as_int(&a[0]);
    if (n <= 0) return nql_val_int(0);
    if (n <= 2) return nql_val_int(1);
    int64_t a0 = 0, b0 = 1;
    for (int64_t i = 2; i <= n && i < 93; ++i) { int64_t t = a0 + b0; a0 = b0; b0 = t; }
    return nql_val_int(b0);
}
static NqlValue nql_fn_powmod(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t base = (uint64_t)nql_val_as_int(&a[0]);
    uint64_t exp  = (uint64_t)nql_val_as_int(&a[1]);
    uint64_t mod  = (uint64_t)nql_val_as_int(&a[2]);
    if (mod == 0u) return nql_val_null();
    uint64_t result = 1u;
    base %= mod;
    while (exp > 0u) {
        if (exp & 1u) result = mulmod_u64(result, base, mod);
        exp >>= 1u;
        base = mulmod_u64(base, base, mod);
    }
    return nql_val_int((int64_t)result);
}
static NqlValue nql_fn_modinv(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t a0 = nql_val_as_int(&a[0]);
    int64_t m = nql_val_as_int(&a[1]);
    if (m <= 0) return nql_val_null();
    int64_t m0 = m, x0 = 0, x1 = 1;
    if (m == 1) return nql_val_int(0);
    while (a0 > 1) {
        if (m == 0) return nql_val_null(); /* not invertible */
        int64_t q = a0 / m;
        int64_t t = m; m = a0 % m; a0 = t;
        t = x0; x0 = x1 - q * x0; x1 = t;
    }
    if (x1 < 0) x1 += m0;
    return nql_val_int(x1);
}
static NqlValue nql_fn_jacobi(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t a0 = nql_val_as_int(&a[0]);
    int64_t n = nql_val_as_int(&a[1]);
    if (n <= 0 || n % 2 == 0) return nql_val_null();
    a0 = a0 % n; if (a0 < 0) a0 += n;
    int result = 1;
    while (a0 != 0) {
        while (a0 % 2 == 0) {
            a0 /= 2;
            if (n % 8 == 3 || n % 8 == 5) result = -result;
        }
        { int64_t t = a0; a0 = n; n = t; }
        if (a0 % 4 == 3 && n % 4 == 3) result = -result;
        a0 = a0 % n;
    }
    return (n == 1) ? nql_val_int(result) : nql_val_int(0);
}
static NqlValue nql_fn_carmichael(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac;
    uint64_t n = (uint64_t)nql_val_as_int(&a[0]);
    if (n <= 1u) return nql_val_int(1);
    FactorResult fr = nql_factor_u64(n, ctx);
    uint64_t lambda = 1u;
    for (size_t i = 0u; i < fr.factor_count; ++i) {
        uint64_t p = fr.factors[i], e = fr.exponents[i];
        uint64_t pk = 1u;
        for (uint64_t j = 0u; j < e; ++j) pk *= p;
        uint64_t t = (pk / p) * (p - 1u); /* pk/p * (p-1) = p^(e-1)*(p-1) */
        if (p == 2u && e >= 3u) t /= 2u;
        /* lcm(lambda, t) */
        uint64_t g = lambda, b = t;
        while (b) { uint64_t tmp = b; b = g % b; g = tmp; }
        lambda = lambda / g * t;
    }
    return nql_val_int((int64_t)lambda);
}
static NqlValue nql_fn_bit_and(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_int(nql_val_as_int(&a[0]) & nql_val_as_int(&a[1]));
}
static NqlValue nql_fn_bit_or(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_int(nql_val_as_int(&a[0]) | nql_val_as_int(&a[1]));
}
static NqlValue nql_fn_bit_xor(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_int(nql_val_as_int(&a[0]) ^ nql_val_as_int(&a[1]));
}
static NqlValue nql_fn_bit_not(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_int(~nql_val_as_int(&a[0]));
}
static NqlValue nql_fn_bit_shl(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t k = nql_val_as_int(&a[1]);
    return (k >= 0 && k < 64) ? nql_val_int(nql_val_as_int(&a[0]) << k) : nql_val_int(0);
}
static NqlValue nql_fn_bit_shr(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t k = nql_val_as_int(&a[1]);
    return (k >= 0 && k < 64) ? nql_val_int((int64_t)((uint64_t)nql_val_as_int(&a[0]) >> k)) : nql_val_int(0);
}
static NqlValue nql_fn_bit_length(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t n = (uint64_t)nql_val_as_int(&a[0]);
    if (n == 0u) return nql_val_int(0);
    int bits = 0; while (n > 0u) { ++bits; n >>= 1u; }
    return nql_val_int(bits);
}
static NqlValue nql_fn_scalar_min(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t x = nql_val_as_int(&a[0]), y = nql_val_as_int(&a[1]);
    return nql_val_int(x < y ? x : y);
}
static NqlValue nql_fn_scalar_max(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t x = nql_val_as_int(&a[0]), y = nql_val_as_int(&a[1]);
    return nql_val_int(x > y ? x : y);
}

/* ── Phase 1c: IS_* predicate wrappers ── */

static NqlValue nql_fn_is_composite(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_bool(nql_is_composite((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_is_semiprime(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_bool(nql_is_semiprime((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_is_even(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_bool(nql_val_as_int(&a[0]) % 2 == 0);
}
static NqlValue nql_fn_is_odd(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_bool(nql_val_as_int(&a[0]) % 2 != 0);
}
static NqlValue nql_fn_is_perfect_power(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_bool(nql_is_perfect_power((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_is_twin_prime(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac;
    uint64_t n = (uint64_t)nql_val_as_int(&a[0]);
    return nql_val_bool(nql_is_prime_u64(n, ctx) && nql_is_prime_u64(n + 2u, ctx));
}
static NqlValue nql_fn_is_mersenne(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_bool(nql_is_mersenne_prime((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_is_fibonacci(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_bool(nql_is_fibonacci((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_is_square(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_bool(nql_is_square((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_is_cube(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_bool(nql_is_cube((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_is_powerful(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_bool(nql_is_powerful((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_is_squarefree(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_bool(nql_is_squarefree((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_is_smooth(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac;
    uint64_t n = (uint64_t)nql_val_as_int(&a[0]);
    uint64_t B = (uint64_t)nql_val_as_int(&a[1]);
    return nql_val_bool(nql_is_smooth(n, B, ctx));
}

/* ══════════════════════════════════════════════════════════════════════
 * NUMBER TYPE TEST/NEXT FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════ */

/* N (all natural numbers >= 1) */
static bool nql_type_N_test(uint64_t n, void* ctx) { (void)ctx; return n >= 1u; }
static uint64_t nql_type_N_next(uint64_t n, void* ctx) { (void)ctx; return n < 1u ? 1u : n; }

/* PRIME */
static bool nql_type_prime_test(uint64_t n, void* ctx) { return nql_is_prime_u64(n, ctx); }
static uint64_t nql_type_prime_next(uint64_t n, void* ctx) {
    if (n <= 2u) return 2u;
    if (n % 2u == 0u) ++n;
    while (!nql_is_prime_u64(n, ctx)) n += 2u;
    return n;
}

/* COMPOSITE */
static bool nql_type_composite_test(uint64_t n, void* ctx) { return nql_is_composite(n, ctx); }
static uint64_t nql_type_composite_next(uint64_t n, void* ctx) {
    if (n < 4u) n = 4u;
    while (!nql_is_composite(n, ctx)) ++n;
    return n;
}

/* SEMIPRIME */
static bool nql_type_semiprime_test(uint64_t n, void* ctx) { return nql_is_semiprime(n, ctx); }
static uint64_t nql_type_semiprime_next(uint64_t n, void* ctx) {
    if (n < 4u) n = 4u;
    while (!nql_is_semiprime(n, ctx)) ++n;
    return n;
}

/* EVEN */
static bool nql_type_even_test(uint64_t n, void* ctx) { (void)ctx; return n % 2u == 0u; }
static uint64_t nql_type_even_next(uint64_t n, void* ctx) {
    (void)ctx; return (n < 2u) ? 2u : (n % 2u == 0u ? n : n + 1u);
}

/* ODD */
static bool nql_type_odd_test(uint64_t n, void* ctx) { (void)ctx; return n >= 1u && n % 2u == 1u; }
static uint64_t nql_type_odd_next(uint64_t n, void* ctx) {
    (void)ctx; return (n < 1u) ? 1u : (n % 2u == 1u ? n : n + 1u);
}

/* PERFECT_POWER */
static bool nql_type_pp_test(uint64_t n, void* ctx) { (void)ctx; return nql_is_perfect_power(n); }
static uint64_t nql_type_pp_next(uint64_t n, void* ctx) {
    (void)ctx; if (n < 4u) n = 4u;
    while (!nql_is_perfect_power(n)) ++n;
    return n;
}

/* TWIN_PRIME */
static bool nql_type_twin_test(uint64_t n, void* ctx) {
    return nql_is_prime_u64(n, ctx) && nql_is_prime_u64(n + 2u, ctx);
}
static uint64_t nql_type_twin_next(uint64_t n, void* ctx) {
    if (n < 3u) n = 3u;
    while (!nql_type_twin_test(n, ctx)) ++n;
    return n;
}

/* MERSENNE */
static bool nql_type_mersenne_test(uint64_t n, void* ctx) { return nql_is_mersenne_prime(n, ctx); }
static uint64_t nql_type_mersenne_next(uint64_t n, void* ctx) {
    /* Known Mersenne primes that fit in uint64 */
    static const uint64_t mp[] = {
        3ull, 7ull, 31ull, 127ull, 8191ull, 131071ull, 524287ull,
        2147483647ull, 2305843009213693951ull, 0ull
    };
    for (size_t i = 0u; mp[i] != 0u; ++i) {
        if (mp[i] >= n) return mp[i];
    }
    (void)ctx;
    return 0u; /* none found */
}

/* FIBONACCI */
static bool nql_type_fib_test(uint64_t n, void* ctx) { (void)ctx; return nql_is_fibonacci(n); }
static uint64_t nql_type_fib_next(uint64_t n, void* ctx) {
    (void)ctx;
    if (n <= 1u) return n < 1u ? 0u : 1u;
    uint64_t a = 0u, b = 1u;
    while (b < n) { uint64_t t = a + b; a = b; b = t; }
    return b;
}

/* PALINDROME */
static bool nql_type_palin_test(uint64_t n, void* ctx) { (void)ctx; return nql_is_palindrome(n); }
static uint64_t nql_type_palin_next(uint64_t n, void* ctx) {
    (void)ctx; while (!nql_is_palindrome(n)) ++n; return n;
}

/* SQUARE */
static bool nql_type_square_test(uint64_t n, void* ctx) { (void)ctx; return nql_is_square(n); }
static uint64_t nql_type_square_next(uint64_t n, void* ctx) {
    (void)ctx; uint64_t r = nql_isqrt(n); if (r * r < n) ++r; return r * r;
}

/* CUBE */
static bool nql_type_cube_test(uint64_t n, void* ctx) { (void)ctx; return nql_is_cube(n); }
static uint64_t nql_type_cube_next(uint64_t n, void* ctx) {
    (void)ctx; uint64_t r = nql_icbrt(n); if (r * r * r < n) ++r; return r * r * r;
}

/* POWERFUL */
static bool nql_type_powerful_test(uint64_t n, void* ctx) { return nql_is_powerful(n, ctx); }
static uint64_t nql_type_powerful_next(uint64_t n, void* ctx) {
    if (n < 1u) n = 1u;
    while (!nql_is_powerful(n, ctx)) ++n;
    return n;
}

/* SQUAREFREE */
static bool nql_type_sqfree_test(uint64_t n, void* ctx) { return nql_is_squarefree(n, ctx); }
static uint64_t nql_type_sqfree_next(uint64_t n, void* ctx) {
    if (n < 1u) n = 1u;
    while (!nql_is_squarefree(n, ctx)) ++n;
    return n;
}

/* SMOOTH(B) — parameterised; test/next use a global B set by the query executor */
static uint64_t g_nql_smooth_B = 7u;
static bool nql_type_smooth_test(uint64_t n, void* ctx) { return nql_is_smooth(n, g_nql_smooth_B, ctx); }
static uint64_t nql_type_smooth_next(uint64_t n, void* ctx) {
    if (n < 1u) n = 1u;
    while (!nql_is_smooth(n, g_nql_smooth_B, ctx)) ++n;
    return n;
}

/* ══════════════════════════════════════════════════════════════════════
 * REGISTRY INITIALISATION
 * ══════════════════════════════════════════════════════════════════════ */

static void nql_register_type(const char* name, const char* col, bool (*test)(uint64_t, void*),
                               uint64_t (*next)(uint64_t, void*), bool needs_db, bool has_param) {
    if (g_nql_type_count >= NQL_MAX_NUMBER_TYPES) return;
    NqlNumberType* t = &g_nql_types[g_nql_type_count++];
    t->name = name; t->column_name = col;
    t->test = test; t->next = next;
    t->needs_db = needs_db; t->has_param = has_param;
}

static void nql_register_func(const char* name, NqlBuiltinFn fn,
                                uint32_t min_args, uint32_t max_args, const char* desc) {
    if (g_nql_func_count >= NQL_MAX_FUNCTIONS) return;
    NqlFuncEntry* e = &g_nql_funcs[g_nql_func_count++];
    e->name = name; e->fn = fn;
    e->min_args = min_args; e->max_args = max_args;
    e->description = desc;
}

static bool g_nql_registry_init = false;

static void nql_init_registry(void) {
    if (g_nql_registry_init) return;
    g_nql_registry_init = true;

    /* Number types */
    nql_register_type("N",             "n", nql_type_N_test,         nql_type_N_next,         false, false);
    nql_register_type("PRIME",         "p", nql_type_prime_test,     nql_type_prime_next,     false, false);
    nql_register_type("COMPOSITE",     "n", nql_type_composite_test, nql_type_composite_next, false, false);
    nql_register_type("SEMIPRIME",     "N", nql_type_semiprime_test, nql_type_semiprime_next, false, false);
    nql_register_type("EVEN",          "n", nql_type_even_test,      nql_type_even_next,      false, false);
    nql_register_type("ODD",           "n", nql_type_odd_test,       nql_type_odd_next,       false, false);
    nql_register_type("PERFECT_POWER", "n", nql_type_pp_test,        nql_type_pp_next,        false, false);
    nql_register_type("TWIN_PRIME",    "p", nql_type_twin_test,      nql_type_twin_next,      false, false);
    nql_register_type("MERSENNE",      "p", nql_type_mersenne_test,  nql_type_mersenne_next,  false, false);
    nql_register_type("FIBONACCI",     "n", nql_type_fib_test,       nql_type_fib_next,       false, false);
    nql_register_type("PALINDROME",    "n", nql_type_palin_test,     nql_type_palin_next,     false, false);
    nql_register_type("SQUARE",        "n", nql_type_square_test,    nql_type_square_next,    false, false);
    nql_register_type("CUBE",          "n", nql_type_cube_test,      nql_type_cube_next,      false, false);
    nql_register_type("POWERFUL",      "n", nql_type_powerful_test,  nql_type_powerful_next,  false, false);
    nql_register_type("SQUAREFREE",    "n", nql_type_sqfree_test,    nql_type_sqfree_next,    false, false);
    nql_register_type("SMOOTH",        "n", nql_type_smooth_test,    nql_type_smooth_next,    false, true);
    nql_register_type("RANGE",         "n", nql_type_N_test,        nql_type_N_next,         false, true);

    /* ── Original functions ── */
    nql_register_func("IS_PRIME",        nql_fn_is_prime,        1, 1, "Test if n is prime");
    nql_register_func("NEXT_PRIME",      nql_fn_next_prime,      1, 1, "Smallest prime > n");
    nql_register_func("PREV_PRIME",      nql_fn_prev_prime,      1, 1, "Largest prime < n");
    nql_register_func("PRIME_COUNT",     nql_fn_pi,              1, 1, "Count of primes <= n (requires DB)");
    nql_register_func("NTH_PRIME",       nql_fn_nth_prime,       1, 1, "The k-th prime (requires DB)");
    nql_register_func("FACTORISE",       nql_fn_factor,          1, 1, "Prime factorisation as string");
    nql_register_func("SMALLEST_FACTOR", nql_fn_smallest_factor, 1, 1, "Smallest prime factor");
    nql_register_func("LARGEST_FACTOR",  nql_fn_largest_factor,  1, 1, "Largest prime factor");
    nql_register_func("DISTINCT_PRIME_FACTORS", nql_fn_omega,    1, 1, "Distinct prime factor count");
    nql_register_func("PRIME_FACTOR_COUNT", nql_fn_bigomega,    1, 1, "Prime factor count with multiplicity");
    nql_register_func("DIVISORS",        nql_fn_divisors,        1, 1, "List of divisors as string");
    nql_register_func("NUM_DIVISORS",    nql_fn_num_divisors,    1, 1, "Number of divisors");
    nql_register_func("DIVISOR_SUM",     nql_fn_sigma,           1, 1, "Sum of divisors");
    nql_register_func("EULER_TOTIENT",   nql_fn_totient,         1, 1, "Euler's totient");
    nql_register_func("MOBIUS",          nql_fn_mobius,          1, 1, "Mobius function (-1, 0, 1)");
    nql_register_func("GCD",             nql_fn_gcd,             2, 2, "Greatest common divisor");
    nql_register_func("LCM",             nql_fn_lcm,             2, 2, "Least common multiple");
    nql_register_func("INT_SQRT",        nql_fn_isqrt,           1, 1, "Integer square root");
    nql_register_func("POW",             nql_fn_pow,             2, 2, "Exponentiation");
    nql_register_func("MOD",             nql_fn_mod,             2, 2, "Modular reduction");
    nql_register_func("ABS",             nql_fn_abs,             1, 1, "Absolute value");
    nql_register_func("LN",              nql_fn_log,             1, 1, "Natural logarithm");
    nql_register_func("DIGIT_COUNT",     nql_fn_digits,          1, 1, "Count of decimal digits");
    nql_register_func("DIGIT_SUM",       nql_fn_digit_sum,       1, 1, "Sum of decimal digits");
    nql_register_func("REVERSE_DIGITS",  nql_fn_reverse,         1, 1, "Reverse decimal digits");
    nql_register_func("IS_PALINDROME",   nql_fn_is_palindrome,   1, 1, "Decimal palindrome test");
    nql_register_func("PRIME_GAP",       nql_fn_gap,             1, 1, "Gap to next prime");

    /* ── Phase 1b: Math / bitwise / number-theory ── */
    nql_register_func("FLOOR",           nql_fn_floor,           1, 1, "Floor of float");
    nql_register_func("CEIL",            nql_fn_ceil,            1, 1, "Ceiling of float");
    nql_register_func("ROUND",           nql_fn_round,           1, 1, "Round to nearest integer");
    nql_register_func("SIGN",            nql_fn_sign,            1, 1, "Sign: -1, 0, or 1");
    nql_register_func("SQRT",            nql_fn_sqrt,            1, 1, "Floating-point square root");
    nql_register_func("LOG2",            nql_fn_log2,            1, 1, "Base-2 logarithm");
    nql_register_func("LOG10",           nql_fn_log10,           1, 1, "Base-10 logarithm");
    nql_register_func("FACTORIAL",       nql_fn_factorial,       1, 1, "n! (n <= 20)");
    nql_register_func("BINOMIAL",        nql_fn_binomial,        2, 2, "Binomial coefficient C(n,k)");
    nql_register_func("FIBONACCI",       nql_fn_fibonacci_n,     1, 1, "n-th Fibonacci number");
    nql_register_func("POWMOD",          nql_fn_powmod,          3, 3, "Modular exponentiation base^exp mod m");
    nql_register_func("MODINV",          nql_fn_modinv,          2, 2, "Modular inverse of a mod m");
    nql_register_func("JACOBI",          nql_fn_jacobi,          2, 2, "Jacobi symbol (a/n)");
    nql_register_func("CARMICHAEL_LAMBDA", nql_fn_carmichael,    1, 1, "Carmichael lambda function");
    nql_register_func("BIT_AND",         nql_fn_bit_and,         2, 2, "Bitwise AND");
    nql_register_func("BIT_OR",          nql_fn_bit_or,          2, 2, "Bitwise OR");
    nql_register_func("BIT_XOR",         nql_fn_bit_xor,        2, 2, "Bitwise XOR");
    nql_register_func("BIT_NOT",         nql_fn_bit_not,         1, 1, "Bitwise NOT");
    nql_register_func("BIT_SHIFT_LEFT",  nql_fn_bit_shl,        2, 2, "Left shift");
    nql_register_func("BIT_SHIFT_RIGHT", nql_fn_bit_shr,        2, 2, "Right shift");
    nql_register_func("BIT_LENGTH",      nql_fn_bit_length,      1, 1, "Number of bits");
    nql_register_func("LEAST",           nql_fn_scalar_min,      2, 2, "Smaller of two values");
    nql_register_func("GREATEST",        nql_fn_scalar_max,      2, 2, "Larger of two values");

    /* ── Phase 1c: IS_* type predicates ── */
    nql_register_func("IS_COMPOSITE",    nql_fn_is_composite,    1, 1, "Test if n is composite");
    nql_register_func("IS_SEMIPRIME",    nql_fn_is_semiprime,    1, 1, "Test if n is semiprime");
    nql_register_func("IS_EVEN",         nql_fn_is_even,         1, 1, "Test if n is even");
    nql_register_func("IS_ODD",          nql_fn_is_odd,          1, 1, "Test if n is odd");
    nql_register_func("IS_PERFECT_POWER",nql_fn_is_perfect_power,1, 1, "Test if n = a^k, k >= 2");
    nql_register_func("IS_TWIN_PRIME",   nql_fn_is_twin_prime,   1, 1, "Test if p and p+2 are both prime");
    nql_register_func("IS_MERSENNE",     nql_fn_is_mersenne,     1, 1, "Test if n is a Mersenne prime");
    nql_register_func("IS_FIBONACCI",    nql_fn_is_fibonacci,    1, 1, "Test if n is a Fibonacci number");
    nql_register_func("IS_SQUARE",       nql_fn_is_square,       1, 1, "Test if n is a perfect square");
    nql_register_func("IS_CUBE",         nql_fn_is_cube,         1, 1, "Test if n is a perfect cube");
    nql_register_func("IS_POWERFUL",     nql_fn_is_powerful,     1, 1, "Test if all prime factors p satisfy p^2|n");
    nql_register_func("IS_SQUAREFREE",   nql_fn_is_squarefree,   1, 1, "Test if no prime factor appears twice");
    nql_register_func("IS_SMOOTH",       nql_fn_is_smooth,       2, 2, "Test if n is B-smooth");

    /* ── Aliases for discoverability ── */
    nql_register_func("FACTORS_OF",      nql_fn_factor,          1, 1, "Alias for FACTORISE(n)");
    nql_register_func("DIVISORS_OF",     nql_fn_divisors,        1, 1, "Alias for DIVISORS(n)");
}
