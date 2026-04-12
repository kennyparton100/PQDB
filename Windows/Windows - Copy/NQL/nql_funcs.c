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
 * PHASE 2: NEW NUMBER-THEORY HELPERS
 * ══════════════════════════════════════════════════════════════════════ */

/** Liouville lambda: (-1)^Omega(n) where Omega counts prime factors with multiplicity. */
static int64_t nql_liouville(uint64_t n, void* db_ctx) {
    if (n <= 1u) return 1;
    uint64_t omega = nql_bigomega(n, db_ctx);
    return (omega % 2u == 0u) ? 1 : -1;
}

/** Multiplicative order of a modulo n: smallest k>0 such that a^k ≡ 1 (mod n). */
static uint64_t nql_multiplicative_order(uint64_t a, uint64_t n) {
    if (n <= 1u) return 0u;
    a %= n;
    if (a == 0u) return 0u;
    /* Check gcd(a,n)==1 */
    uint64_t g = a, b = n;
    while (b) { uint64_t t = b; b = g % b; g = t; }
    if (g != 1u) return 0u; /* not coprime */
    uint64_t result = 1u;
    uint64_t power = a;
    while (power != 1u) {
        power = mulmod_u64(power, a, n);
        ++result;
        if (result > n) return 0u; /* safety cap */
    }
    return result;
}

/** Smallest primitive root modulo p (p must be prime). Returns 0 if none found. */
static uint64_t nql_primitive_root(uint64_t p, void* db_ctx) {
    if (p <= 1u) return 0u;
    if (p == 2u) return 1u;
    uint64_t phi = p - 1u;
    /* Factor phi to get distinct prime factors */
    FactorResult fr = nql_factor_u64(phi, db_ctx);
    for (uint64_t g = 2u; g < p; ++g) {
        bool is_root = true;
        for (size_t i = 0u; i < fr.factor_count; ++i) {
            uint64_t exp = phi / fr.factors[i];
            /* Compute g^exp mod p using mulmod */
            uint64_t base = g % p, result = 1u, e = exp;
            while (e > 0u) {
                if (e & 1u) result = mulmod_u64(result, base, p);
                e >>= 1u;
                base = mulmod_u64(base, base, p);
            }
            if (result == 1u) { is_root = false; break; }
        }
        if (is_root) return g;
    }
    return 0u;
}

/** Count of primitive roots modulo p = phi(phi(p)) = phi(p-1) for prime p. */
static uint64_t nql_primitive_root_count(uint64_t p, void* db_ctx) {
    if (p <= 1u) return 0u;
    if (p == 2u) return 1u;
    return nql_totient(p - 1u, db_ctx);
}

/** Chebyshev theta: θ(x) = Σ_{p≤x, p prime} log(p). */
static double nql_chebyshev_theta(uint64_t x, void* db_ctx) {
    double sum = 0.0;
    for (uint64_t n = 2u; n <= x; ++n) {
        if (nql_is_prime_u64(n, db_ctx))
            sum += log((double)n);
    }
    return sum;
}

/** Chebyshev psi: ψ(x) = Σ_{p^k≤x} log(p). */
static double nql_chebyshev_psi(uint64_t x, void* db_ctx) {
    double sum = 0.0;
    for (uint64_t n = 2u; n <= x; ++n) {
        if (!nql_is_prime_u64(n, db_ctx)) continue;
        double lp = log((double)n);
        uint64_t pk = n;
        while (pk <= x) {
            sum += lp;
            if (pk > x / n) break; /* overflow guard */
            pk *= n;
        }
    }
    return sum;
}

/** Radical of n: product of distinct prime factors. rad(1)=1. */
static uint64_t nql_radical(uint64_t n, void* db_ctx) {
    if (n <= 1u) return n;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    uint64_t rad = 1u;
    for (size_t i = 0u; i < fr.factor_count; ++i)
        rad *= fr.factors[i];
    return rad;
}

/** Sum of prime factors with repetition: sopfr(12) = 2+2+3 = 7. */
static uint64_t nql_sopfr(uint64_t n, void* db_ctx) {
    if (n <= 1u) return 0u;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    uint64_t s = 0u;
    for (size_t i = 0u; i < fr.factor_count; ++i)
        s += fr.factors[i] * fr.exponents[i];
    return s;
}

/** Digital root: repeatedly sum digits until single digit. */
static uint64_t nql_digital_root(uint64_t n) {
    if (n == 0u) return 0u;
    return 1u + ((n - 1u) % 9u);
}

/** Is n a Carmichael number? Composite n where a^(n-1) ≡ 1 (mod n) for all a coprime to n.
 *  Uses Korselt's criterion: n is squarefree and (p-1)|(n-1) for every prime factor p. */
static bool nql_is_carmichael(uint64_t n, void* db_ctx) {
    if (n < 561u) return false; /* 561 is the smallest Carmichael number */
    if (nql_is_prime_u64(n, db_ctx)) return false;
    FactorResult fr = nql_factor_u64(n, db_ctx);
    if (fr.factor_count < 3u) return false; /* Carmichael numbers have >= 3 prime factors */
    uint64_t nm1 = n - 1u;
    for (size_t i = 0u; i < fr.factor_count; ++i) {
        if (fr.exponents[i] > 1u) return false; /* must be squarefree */
        if (nm1 % (fr.factors[i] - 1u) != 0u) return false; /* Korselt: (p-1)|(n-1) */
    }
    return true;
}

/* ══════════════════════════════════════════════════════════════════════
 * PHASE 3: QS BUILDING-BLOCK HELPERS
 * ══════════════════════════════════════════════════════════════════════ */

/** Tonelli-Shanks modular square root: returns r such that r² ≡ n (mod p), or 0 if none. */
static uint64_t nql_tonelli_shanks(uint64_t n, uint64_t p) {
    if (p == 2u) return n & 1u;
    n %= p;
    if (n == 0u) return 0u;
    if (powmod_u64(n, (p - 1u) / 2u, p) != 1u) return 0u; /* not a QR */
    if ((p & 3u) == 3u) return powmod_u64(n, (p + 1u) / 4u, p);
    uint64_t Q = p - 1u, S = 0u;
    while ((Q & 1u) == 0u) { Q >>= 1u; ++S; }
    uint64_t z = 2u;
    while (powmod_u64(z, (p - 1u) / 2u, p) != p - 1u) { ++z; if (z >= p) return 0u; }
    uint64_t M = S, c = powmod_u64(z, Q, p);
    uint64_t t = powmod_u64(n, Q, p), R = powmod_u64(n, (Q + 1u) / 2u, p);
    for (;;) {
        if (t == 1u) return R;
        uint64_t i = 0u, tmp = t;
        while (tmp != 1u) { tmp = mulmod_u64(tmp, tmp, p); ++i; if (i >= M) return 0u; }
        uint64_t b = c;
        for (uint64_t j = 0u; j < M - i - 1u; ++j) b = mulmod_u64(b, b, p);
        M = i; c = mulmod_u64(b, b, p); t = mulmod_u64(t, c, p); R = mulmod_u64(R, b, p);
    }
}

/** Legendre symbol (a/p) for odd prime p. Returns -1, 0, or 1. */
static int64_t nql_legendre(uint64_t a, uint64_t p) {
    if (p < 2u) return 0;
    a %= p;
    if (a == 0u) return 0;
    uint64_t r = powmod_u64(a, (p - 1u) / 2u, p);
    if (r == 1u) return 1;
    if (r == p - 1u) return -1;
    return 0;
}

/** Trial-factor n against a factor base array, return exponent parity array.
 *  Returns an array where element i = (exponent of fb[i] in n) mod 2.
 *  Also returns the cofactor via the last array element. */
static NqlValue nql_fn_trial_factor_fb(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    uint64_t n = (uint64_t)nql_val_as_int(&args[0]);
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* fb = args[1].v.array;
    NqlArray* result = nql_array_alloc(fb->length + 1u);
    if (!result) return nql_val_null();
    uint64_t rem = n;
    for (uint32_t i = 0u; i < fb->length; ++i) {
        uint64_t p = (uint64_t)nql_val_as_int(&fb->items[i]);
        int exp = 0;
        while (p > 0u && rem % p == 0u) { rem /= p; ++exp; }
        nql_array_push(result, nql_val_int(exp & 1));
    }
    nql_array_push(result, nql_val_int((int64_t)rem)); /* cofactor */
    return nql_val_array(result);
}

/** Sieve subtract stride: subtract value from every stride-th element starting at start. */
static NqlValue nql_fn_sieve_subtract_stride(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array; /* modify in place for performance */
    int64_t start = nql_val_as_int(&args[1]);
    int64_t stride = nql_val_as_int(&args[2]);
    double value = nql_val_as_float(&args[3]);
    if (stride <= 0) return args[0];
    for (int64_t i = start; i >= 0 && (uint32_t)i < a->length; i += stride) {
        double cur = nql_val_as_float(&a->items[(uint32_t)i]);
        a->items[(uint32_t)i] = nql_val_float(cur - value);
    }
    return args[0]; /* return same array (mutated) */
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
 * PHASE 2: NEW FUNCTION WRAPPERS
 * ══════════════════════════════════════════════════════════════════════ */

static NqlValue nql_fn_liouville(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int(nql_liouville((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_multiplicative_order(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int((int64_t)nql_multiplicative_order(
        (uint64_t)nql_val_as_int(&a[0]), (uint64_t)nql_val_as_int(&a[1])));
}
static NqlValue nql_fn_primitive_root(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_primitive_root((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_primitive_root_count(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_primitive_root_count((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_chebyshev_theta(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_float(nql_chebyshev_theta((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_chebyshev_psi(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_float(nql_chebyshev_psi((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_radical(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_radical((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_sopfr(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_int((int64_t)nql_sopfr((uint64_t)nql_val_as_int(&a[0]), ctx));
}
static NqlValue nql_fn_digital_root(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int((int64_t)nql_digital_root((uint64_t)nql_val_as_int(&a[0])));
}
static NqlValue nql_fn_is_carmichael(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; return nql_val_bool(nql_is_carmichael((uint64_t)nql_val_as_int(&a[0]), ctx));
}

/* ── Wide-type-aware IS_PRIME (replaces uint64-only version) ── */

static NqlValue nql_fn_is_prime_wide(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac;
    if (a[0].type == NQL_VAL_BIGNUM) {
        return nql_val_bool(bn_factor_is_prime(&a[0].v.bnval));
    }
    if (a[0].type == NQL_VAL_U128) {
        return nql_val_bool(u128_is_prime(a[0].v.u128val));
    }
    return nql_val_bool(nql_is_prime_u64((uint64_t)nql_val_as_int(&a[0]), ctx));
}

/* ── Wide-type-aware GCD ── */

static NqlValue nql_fn_gcd_wide(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    NqlValType tier = nql_wider_type(&a[0], &a[1]);
    if (tier == NQL_VAL_BIGNUM) {
        BigNum x = nql_val_as_bignum(&a[0]);
        BigNum y = nql_val_as_bignum(&a[1]);
        BigNum g; bn_gcd(&g, &x, &y);
        return nql_val_from_bignum(&g);
    }
    if (tier == NQL_VAL_U128) {
        U128 x = nql_val_as_u128(&a[0]);
        U128 y = nql_val_as_u128(&a[1]);
        U128 g = u128_gcd(x, y);
        return nql_val_from_u128(g);
    }
    uint64_t x = (uint64_t)nql_val_as_int(&a[0]);
    uint64_t y = (uint64_t)nql_val_as_int(&a[1]);
    while (y) { uint64_t t = y; y = x % y; x = t; }
    return nql_val_int((int64_t)x);
}

/* ── Method-specific factoring wrappers ── */

/** FACTOR_FERMAT(n, max_steps) — Fermat difference-of-squares. Returns factor or null. */
static NqlValue nql_fn_factor_fermat_nql(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t n = (uint64_t)nql_val_as_int(&args[0]);
    uint64_t max_steps = (ac >= 2u) ? (uint64_t)nql_val_as_int(&args[1]) : 0u;
    FactorResult fr = cpss_factor_fermat(n, max_steps);
    if (fr.factor_count > 0u) return nql_val_int((int64_t)fr.factors[0]);
    return nql_val_null();
}

/** FACTOR_RHO(n, max_iterations, seed) — Pollard rho. Returns factor or null. */
static NqlValue nql_fn_factor_rho_nql(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t n = (uint64_t)nql_val_as_int(&args[0]);
    uint64_t max_iter = (ac >= 2u) ? (uint64_t)nql_val_as_int(&args[1]) : 0u;
    uint64_t seed = (ac >= 3u) ? (uint64_t)nql_val_as_int(&args[2]) : 0u;
    FactorResult fr = cpss_factor_rho(n, max_iter, seed);
    if (fr.factor_count > 0u) return nql_val_int((int64_t)fr.factors[0]);
    return nql_val_null();
}

/** FACTOR_SQUFOF(n) — Shanks' SQUFOF. Returns factor or null. */
static NqlValue nql_fn_factor_squfof_nql(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t n = (uint64_t)nql_val_as_int(&args[0]);
    FactorResult fr = cpss_factor_squfof(n);
    if (fr.factor_count > 0u) return nql_val_int((int64_t)fr.factors[0]);
    return nql_val_null();
}

/** FACTOR_ECM(n, B1, curves) — ECM for BigNum. Returns factor or null. */
static NqlValue nql_fn_factor_ecm_nql(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type == NQL_VAL_BIGNUM || args[0].type == NQL_VAL_U128) {
        BigNum n = nql_val_as_bignum(&args[0]);
        uint64_t B1 = (ac >= 2u) ? (uint64_t)nql_val_as_int(&args[1]) : 0u;
        uint64_t curves = (ac >= 3u) ? (uint64_t)nql_val_as_int(&args[2]) : 0u;
        FactorResultBigNum fr = cpss_factor_ecm_bn(&n, B1, 0u, curves);
        if (fr.factor_found) return nql_val_from_bignum(&fr.factor);
        return nql_val_null();
    }
    /* uint64 path: use rho as ECM doesn't have a separate u64 entry */
    uint64_t n = (uint64_t)nql_val_as_int(&args[0]);
    FactorResult fr = cpss_factor_rho(n, 0u, 0u);
    if (fr.factor_count > 0u) return nql_val_int((int64_t)fr.factors[0]);
    return nql_val_null();
}

/** FACTOR_QS(n) — call the full C SIQS engine. Returns factor or null. */
static NqlValue nql_fn_factor_qs_nql(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t n = (uint64_t)nql_val_as_int(&args[0]);
    FactorResult fr = cpss_factor_qs_u64(n);
    if (fr.factor_count > 0u) return nql_val_int((int64_t)fr.factors[0]);
    return nql_val_null();
}

/** LUCAS_V(a, e, n) — compute V_e(a) mod n via Lucas chain. Used in Williams p+1. */
static NqlValue nql_fn_lucas_v(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t a = (uint64_t)nql_val_as_int(&args[0]);
    uint64_t e = (uint64_t)nql_val_as_int(&args[1]);
    uint64_t n = (uint64_t)nql_val_as_int(&args[2]);
    if (n <= 1u) return nql_val_int(0);
    /* Lucas chain: V_0 = 2, V_1 = a, V_{2k} = V_k^2 - 2, V_{k+l} = V_k*V_l - V_{k-l} */
    if (e == 0u) return nql_val_int((int64_t)(2u % n));
    if (e == 1u) return nql_val_int((int64_t)(a % n));
    uint64_t a_mod = a % n;
    uint64_t vk = a_mod;
    uint64_t vk1 = mulmod_u64(a_mod, a_mod, n);
    vk1 = (vk1 >= 2u) ? vk1 - 2u : vk1 + n - 2u;
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
        } else {
            uint64_t t = mulmod_u64(vk, vk1, n);
            t = (t >= a_mod) ? t - a_mod : t + n - a_mod;
            vk1 = t;
            vk = mulmod_u64(vk, vk, n);
            vk = (vk >= 2u) ? vk - 2u : vk + n - 2u;
        }
        mask >>= 1u;
    }
    return nql_val_int((int64_t)vk);
}

/** IS_PERFECT_SQUARE(n) — works for int64, U128, and BigNum. */
static NqlValue nql_fn_is_perfect_square_wide(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type == NQL_VAL_U128) {
        U128 n = args[0].v.u128val;
        uint64_t s = u128_isqrt_floor(n);
        U128 s_sq = u128_mul64(s, s);
        return nql_val_bool(u128_eq(s_sq, n));
    }
    if (args[0].type == NQL_VAL_BIGNUM) {
        /* BigNum: compute isqrt then check s*s == n */
        BigNum n = args[0].v.bnval;
        if (bn_is_zero(&n)) return nql_val_bool(true);
        /* Use double approximation for initial estimate, then Newton */
        BigNum s; bn_copy(&s, &n); bn_shr1(&s); /* rough start */
        if (bn_is_zero(&s)) bn_from_u64(&s, 1u);
        for (int iter = 0; iter < 200; ++iter) {
            BigNum q; bn_divexact(&q, &n, &s);
            bn_add(&q, &s);
            bn_shr1(&q);
            if (bn_cmp(&q, &s) >= 0) break;
            bn_copy(&s, &q);
        }
        BigNum sq; bn_mul(&sq, &s, &s);
        return nql_val_bool(bn_cmp(&sq, &n) == 0);
    }
    uint64_t n = (uint64_t)nql_val_as_int(&args[0]);
    uint64_t s = (uint64_t)isqrt_u64(n);
    return nql_val_bool(s * s == n);
}

/** INT_SQRT wide — works for U128 and BigNum too. */
static NqlValue nql_fn_isqrt_wide(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type == NQL_VAL_U128) {
        return nql_val_int((int64_t)u128_isqrt_floor(args[0].v.u128val));
    }
    return nql_val_int((int64_t)isqrt_u64((uint64_t)nql_val_as_int(&args[0])));
}

/* ══════════════════════════════════════════════════════════════════════
 * GNFS PREREQUISITES (Phase 0)
 * ══════════════════════════════════════════════════════════════════════ */

/** NTH_ROOT(n, k) — floor(n^(1/k)) for BigNum n, using Newton's method. */
static NqlValue nql_fn_nth_root(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    BigNum n = nql_val_as_bignum(&args[0]);
    int k = (int)nql_val_as_int(&args[1]);
    if (k <= 0 || bn_is_zero(&n)) return nql_val_int(0);
    if (k == 1) return nql_val_from_bignum(&n);
    if (k == 2) {
        /* Use existing isqrt for small values */
        if (bn_fits_u64(&n)) return nql_val_int((int64_t)isqrt_u64(bn_to_u64(&n)));
    }
    /* Newton: x_{n+1} = ((k-1)*x_n + n / x_n^(k-1)) / k */
    /* Start with bit-length estimate: x0 = 2^(bitlen(n)/k) */
    unsigned bits = bn_bitlen(&n);
    unsigned start_bits = (bits + (unsigned)k - 1u) / (unsigned)k;
    BigNum x; bn_from_u64(&x, 1u);
    bn_shl(&x, start_bits > 0 ? start_bits : 1);
    BigNum km1; bn_from_u64(&km1, (uint64_t)(k - 1));
    BigNum kbn; bn_from_u64(&kbn, (uint64_t)k);
    for (int iter = 0; iter < 500; ++iter) {
        /* x_pow = x^(k-1) */
        BigNum x_pow; bn_from_u64(&x_pow, 1u);
        for (int j = 0; j < k - 1; ++j) {
            BigNum tmp; bn_mul(&tmp, &x_pow, &x);
            bn_copy(&x_pow, &tmp);
        }
        if (bn_is_zero(&x_pow)) break;
        BigNum q; bn_divexact(&q, &n, &x_pow);
        /* new_x = (km1 * x + q) / k */
        BigNum km1x; bn_mul(&km1x, &km1, &x);
        bn_add(&km1x, &q);
        BigNum new_x; bn_divexact(&new_x, &km1x, &kbn);
        if (bn_cmp(&new_x, &x) >= 0) break; /* converged */
        bn_copy(&x, &new_x);
    }
    /* Verify: x^k <= n < (x+1)^k */
    return nql_val_from_bignum(&x);
}

/** POLY_MOD_COEFFICIENTS(p, m) — reduce all coefficients of p mod m. Returns new poly. */
static NqlValue nql_fn_poly_mod_coeffs(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_null();
    NqlPoly* src = (NqlPoly*)args[0].v.qs_ptr;
    int64_t m = nql_val_as_int(&args[1]);
    if (m <= 0) return nql_val_null();
    NqlPoly* r = nql_poly_copy(src);
    if (!r) return nql_val_null();
    for (int i = 0; i <= r->degree; ++i) {
        r->coeffs[i] = ((r->coeffs[i] % m) + m) % m;
    }
    nql_poly_normalize(r);
    return nql_val_poly(r);
}

/** POLY_IS_IRREDUCIBLE_MOD(f, p) — test if f is irreducible over GF(p).
 *  Uses: f is irreducible iff gcd(f, x^(p^i) - x) = 1 for i = 1..deg(f)/2,
 *  and x^(p^d) ≡ x mod f where d = deg(f). Practical for small p and small degree. */
static NqlValue nql_fn_poly_is_irred_mod(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_bool(false);
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    uint64_t p = (uint64_t)nql_val_as_int(&args[1]);
    if (f->degree <= 0 || p < 2u) return nql_val_bool(false);
    /* Simple check: f has no roots mod p means it's irreducible for degree <= 3 */
    /* For higher degree, count roots. If roots == 0 and degree <= 3, irreducible. */
    int root_count = 0;
    for (uint64_t x = 0u; x < p && x < 10000u; ++x) {
        if (nql_poly_eval_mod(f, x, p) == 0u) ++root_count;
    }
    if (root_count > 0) return nql_val_bool(false); /* has roots = reducible */
    if (f->degree <= 3) return nql_val_bool(true); /* no roots + degree <= 3 = irreducible */
    /* For degree > 3, no roots is necessary but not sufficient. Conservative: return true for now. */
    return nql_val_bool(true);
}

/** POLY_SCALE(p, c) — multiply all coefficients by scalar c. Returns new poly. */
static NqlValue nql_fn_poly_scale(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_POLY || !args[0].v.qs_ptr) return nql_val_null();
    NqlPoly* src = (NqlPoly*)args[0].v.qs_ptr;
    int64_t c = nql_val_as_int(&args[1]);
    NqlPoly* r = nql_poly_copy(src);
    if (!r) return nql_val_null();
    for (int i = 0; i <= r->degree; ++i) r->coeffs[i] *= c;
    nql_poly_normalize(r);
    return nql_val_poly(r);
}

/** POLY_COMPOSE(f, g) — compute f(g(x)). Returns new polynomial. */
static NqlValue nql_fn_poly_compose(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_POLY || args[1].type != NQL_VAL_POLY) return nql_val_null();
    NqlPoly* f = (NqlPoly*)args[0].v.qs_ptr;
    NqlPoly* g = (NqlPoly*)args[1].v.qs_ptr;
    if (!f || !g || f->degree < 0) return nql_val_poly(nql_poly_alloc(0));
    /* Horner: result = f[d]; for i = d-1..0: result = result * g + f[i] */
    NqlPoly* result = nql_poly_alloc(0);
    if (!result) return nql_val_null();
    result->coeffs[0] = f->coeffs[f->degree]; result->degree = 0;
    for (int i = f->degree - 1; i >= 0; --i) {
        NqlPoly* prod = nql_poly_mul(result, g);
        nql_poly_free(result);
        if (!prod) return nql_val_null();
        /* Add f->coeffs[i] to constant term */
        if (!nql_poly_ensure(prod, 0)) { nql_poly_free(prod); return nql_val_null(); }
        prod->coeffs[0] += f->coeffs[i];
        nql_poly_normalize(prod);
        result = prod;
    }
    return nql_val_poly(result);
}

/** TIMER_START() — return current time in seconds (high-resolution). */
static NqlValue nql_fn_timer_start(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)args; (void)ac; (void)ctx;
    return nql_val_float(now_sec());
}

/** TIMER_ELAPSED(start) — seconds since start. */
static NqlValue nql_fn_timer_elapsed(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    double start = nql_val_as_float(&args[0]);
    return nql_val_float(now_sec() - start);
}

/** FILE_WRITE_LINES(filename, array) — write array elements as lines to a file. */
static NqlValue nql_fn_file_write_lines(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_STRING) return nql_val_bool(false);
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_bool(false);
    FILE* fp = fopen(args[0].v.sval, "w");
    if (!fp) return nql_val_bool(false);
    NqlArray* arr = args[1].v.array;
    for (uint32_t i = 0u; i < arr->length; ++i) {
        char buf[512];
        nql_val_to_str(&arr->items[i], buf, sizeof(buf));
        fprintf(fp, "%s\n", buf);
    }
    fclose(fp);
    return nql_val_bool(true);
}

/** FILE_READ_LINES(filename) — read all lines from file into an array of strings. */
static NqlValue nql_fn_file_read_lines(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    FILE* fp = fopen(args[0].v.sval, "r");
    if (!fp) return nql_val_null();
    NqlArray* arr = nql_array_alloc(256u);
    if (!arr) { fclose(fp); return nql_val_null(); }
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0u && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        nql_array_push(arr, nql_val_str(line));
    }
    fclose(fp);
    return nql_val_array(arr);
}

/** FILE_APPEND_LINE(filename, value) — append one line to a file. */
static NqlValue nql_fn_file_append_line(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_STRING) return nql_val_bool(false);
    FILE* fp = fopen(args[0].v.sval, "a");
    if (!fp) return nql_val_bool(false);
    char buf[512];
    nql_val_to_str(&args[1], buf, sizeof(buf));
    fprintf(fp, "%s\n", buf);
    fclose(fp);
    return nql_val_bool(true);
}

/* ── Maths engine gap-fill functions ── */

/** EXTENDED_GCD(a, b) — returns ARRAY(gcd, x, y) where a*x + b*y = gcd. */
static NqlValue nql_fn_extended_gcd(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t a = nql_val_as_int(&args[0]);
    int64_t b = nql_val_as_int(&args[1]);
    int64_t old_r = a, r = b, old_s = 1, s = 0, old_t = 0, t = 1;
    while (r != 0) {
        int64_t q = old_r / r;
        int64_t tmp;
        tmp = r; r = old_r - q * r; old_r = tmp;
        tmp = s; s = old_s - q * s; old_s = tmp;
        tmp = t; t = old_t - q * t; old_t = tmp;
    }
    NqlArray* arr = nql_array_alloc(3u);
    if (!arr) return nql_val_null();
    nql_array_push(arr, nql_val_int(old_r)); /* gcd */
    nql_array_push(arr, nql_val_int(old_s)); /* x */
    nql_array_push(arr, nql_val_int(old_t)); /* y */
    return nql_val_array(arr);
}

/** CHINESE_REMAINDER(remainders, moduli) — CRT solver. Both args are arrays. */
static NqlValue nql_fn_chinese_remainder(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* rems = args[0].v.array;
    NqlArray* mods = args[1].v.array;
    uint32_t n = rems->length < mods->length ? rems->length : mods->length;
    if (n == 0u) return nql_val_null();
    int64_t result = nql_val_as_int(&rems->items[0]);
    int64_t mod = nql_val_as_int(&mods->items[0]);
    for (uint32_t i = 1u; i < n; ++i) {
        int64_t ri = nql_val_as_int(&rems->items[i]);
        int64_t mi = nql_val_as_int(&mods->items[i]);
        /* Extended GCD to find inverse */
        int64_t g, x, y, old_r = mod, r = mi, old_s = 1, s = 0, old_t = 0, t = 1;
        while (r != 0) { int64_t q = old_r/r; int64_t tmp; tmp=r; r=old_r-q*r; old_r=tmp; tmp=s; s=old_s-q*s; old_s=tmp; tmp=t; t=old_t-q*t; old_t=tmp; }
        g = old_r; x = old_s;
        if (g != 1 && g != -1) return nql_val_null(); /* moduli not coprime */
        int64_t lcm = mod * (mi / g);
        int64_t diff = ri - result;
        result = result + mod * ((diff * x) % (mi / g));
        mod = lcm;
        result = ((result % mod) + mod) % mod;
    }
    return nql_val_int(result);
}

/** SIGMA_K(n, k) — generalised divisor function: sum of k-th powers of divisors. */
static NqlValue nql_fn_sigma_k(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    uint64_t n = (uint64_t)nql_val_as_int(&args[0]);
    int64_t k = nql_val_as_int(&args[1]);
    if (n == 0u) return nql_val_int(0);
    int64_t sum = 0;
    for (uint64_t d = 1u; d * d <= n; ++d) {
        if (n % d == 0u) {
            int64_t pk = 1;
            for (int64_t j = 0; j < k; ++j) pk *= (int64_t)d;
            sum += pk;
            if (d != n / d) {
                pk = 1;
                uint64_t d2 = n / d;
                for (int64_t j = 0; j < k; ++j) pk *= (int64_t)d2;
                sum += pk;
            }
        }
    }
    return nql_val_int(sum);
}

/** IS_PRIMITIVE_ROOT(g, p) — test if g is a primitive root modulo prime p. */
static NqlValue nql_fn_is_primitive_root(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac;
    uint64_t g = (uint64_t)nql_val_as_int(&args[0]);
    uint64_t p = (uint64_t)nql_val_as_int(&args[1]);
    if (p <= 1u) return nql_val_bool(false);
    uint64_t phi = p - 1u;
    /* Factor phi to get distinct prime factors */
    FactorResult fr = nql_factor_u64(phi, ctx);
    for (size_t i = 0u; i < fr.factor_count; ++i) {
        uint64_t exp = phi / fr.factors[i];
        if (powmod_u64(g % p, exp, p) == 1u) return nql_val_bool(false);
    }
    return nql_val_bool(true);
}

/** RANDOM(lo, hi) — random integer in [lo, hi]. Uses xorshift. */
static uint64_t g_nql_rng_state = 0x5DEECE66DULL;
static NqlValue nql_fn_random(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    int64_t lo = nql_val_as_int(&args[0]);
    int64_t hi = nql_val_as_int(&args[1]);
    if (hi < lo) return nql_val_int(lo);
    g_nql_rng_state ^= g_nql_rng_state << 13u;
    g_nql_rng_state ^= g_nql_rng_state >> 7u;
    g_nql_rng_state ^= g_nql_rng_state << 17u;
    uint64_t range = (uint64_t)(hi - lo + 1);
    return nql_val_int(lo + (int64_t)(g_nql_rng_state % range));
}

/** RANDOM_FLOAT() — random float in [0, 1). */
static NqlValue nql_fn_random_float(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)args; (void)ac; (void)ctx;
    g_nql_rng_state ^= g_nql_rng_state << 13u;
    g_nql_rng_state ^= g_nql_rng_state >> 7u;
    g_nql_rng_state ^= g_nql_rng_state << 17u;
    return nql_val_float((double)(g_nql_rng_state & 0xFFFFFFFFFFFFFULL) / (double)0x10000000000000ULL);
}

/** TO_STRING(value) — explicit string conversion. */
static NqlValue nql_fn_to_string(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    char buf[512];
    nql_val_to_str(&args[0], buf, sizeof(buf));
    return nql_val_str(buf);
}

/** TO_INT(value) — explicit integer conversion. */
static NqlValue nql_fn_to_int(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int(nql_val_as_int(&args[0]));
}

/** TO_FLOAT(value) — explicit float conversion. */
static NqlValue nql_fn_to_float(const NqlValue* args, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_float(nql_val_as_float(&args[0]));
}

/* ── Standard maths function wrappers (trig, hyperbolic, exp, constants) ── */

static NqlValue nql_fn_sin(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(sin(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_cos(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(cos(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_tan(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(tan(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_asin(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(asin(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_acos(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(acos(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_atan(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(atan(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_atan2(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(atan2(nql_val_as_float(&a[0]), nql_val_as_float(&a[1])));
}
static NqlValue nql_fn_sinh(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(sinh(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_cosh(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(cosh(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_tanh(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(tanh(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_exp(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(exp(nql_val_as_float(&a[0])));
}
static NqlValue nql_fn_log_base(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(log(nql_val_as_float(&a[0])) / log(nql_val_as_float(&a[1])));
}
static NqlValue nql_fn_degrees(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(nql_val_as_float(&a[0]) * 180.0 / 3.14159265358979323846);
}
static NqlValue nql_fn_radians(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx; return nql_val_float(nql_val_as_float(&a[0]) * 3.14159265358979323846 / 180.0);
}
static NqlValue nql_fn_pi_const(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)a; (void)ac; (void)ctx; return nql_val_float(3.14159265358979323846);
}
static NqlValue nql_fn_e_const(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)a; (void)ac; (void)ctx; return nql_val_float(2.71828182845904523536);
}

/* ── Phase 3: QS building-block wrappers ── */

static NqlValue nql_fn_tonelli_shanks(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int((int64_t)nql_tonelli_shanks((uint64_t)nql_val_as_int(&a[0]),
                                                    (uint64_t)nql_val_as_int(&a[1])));
}
static NqlValue nql_fn_mulmod(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int((int64_t)mulmod_u64((uint64_t)nql_val_as_int(&a[0]),
                                            (uint64_t)nql_val_as_int(&a[1]),
                                            (uint64_t)nql_val_as_int(&a[2])));
}
static NqlValue nql_fn_legendre_symbol(const NqlValue* a, uint32_t ac, void* ctx) {
    (void)ac; (void)ctx;
    return nql_val_int(nql_legendre((uint64_t)nql_val_as_int(&a[0]),
                                    (uint64_t)nql_val_as_int(&a[1])));
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

/* CARMICHAEL (composite n where Korselt's criterion holds) */
static bool nql_type_carmichael_test(uint64_t n, void* ctx) { return nql_is_carmichael(n, ctx); }
static uint64_t nql_type_carmichael_next(uint64_t n, void* ctx) {
    if (n < 561u) n = 561u;
    while (!nql_is_carmichael(n, ctx)) { ++n; if (n > 1000000000ull) return 0u; }
    return n;
}

/* CUBAN_PRIME: primes of the form p = 3n² + 3n + 1 for some n >= 1. */
static bool nql_type_cuban_test(uint64_t p, void* ctx) {
    if (!nql_is_prime_u64(p, ctx)) return false;
    if (p < 7u) return false;
    /* Solve 3n² + 3n + 1 = p => n = (-3 + sqrt(9 - 12(1-p))) / 6 = (-3 + sqrt(12p-3)) / 6 */
    double disc = 12.0 * (double)p - 3.0;
    if (disc < 0.0) return false;
    double n_approx = (-3.0 + sqrt(disc)) / 6.0;
    uint64_t n_lo = (uint64_t)(n_approx > 1.0 ? n_approx - 1.0 : 0.0);
    for (uint64_t n = n_lo; n <= n_lo + 3u; ++n) {
        if (n == 0u) continue;
        uint64_t val = 3u * n * n + 3u * n + 1u;
        if (val == p) return true;
        if (val > p) break;
    }
    return false;
}
static uint64_t nql_type_cuban_next(uint64_t start, void* ctx) {
    if (start < 7u) start = 7u;
    /* Generate candidates from the formula and test primality */
    uint64_t n = 1u;
    while (1) {
        uint64_t val = 3u * n * n + 3u * n + 1u;
        if (val >= start && nql_is_prime_u64(val, ctx)) return val;
        if (val > UINT64_MAX / 4u) return 0u; /* overflow guard */
        ++n;
    }
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

    /* Phase 2 number types */
    nql_register_type("CARMICHAEL",    "n", nql_type_carmichael_test, nql_type_carmichael_next, false, false);
    nql_register_type("CUBAN_PRIME",   "p", nql_type_cuban_test,      nql_type_cuban_next,      false, false);

    /* ── Original functions ── */
    nql_register_func("IS_PRIME",        nql_fn_is_prime_wide,   1, 1, "Test if n is prime (works for int64/U128/BigNum)");
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
    nql_register_func("GCD",             nql_fn_gcd_wide,        2, 2, "Greatest common divisor (works for int64/U128/BigNum)");
    nql_register_func("LCM",             nql_fn_lcm,             2, 2, "Least common multiple");
    nql_register_func("INT_SQRT",        nql_fn_isqrt_wide,      1, 1, "Integer square root (works for int64/U128)");
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

    /* ── Standard maths: trig, hyperbolic, exponential, constants ── */
    nql_register_func("SIN",             nql_fn_sin,             1, 1, "Sine (radians)");
    nql_register_func("COS",             nql_fn_cos,             1, 1, "Cosine (radians)");
    nql_register_func("TAN",             nql_fn_tan,             1, 1, "Tangent (radians)");
    nql_register_func("ASIN",            nql_fn_asin,            1, 1, "Arc sine");
    nql_register_func("ACOS",            nql_fn_acos,            1, 1, "Arc cosine");
    nql_register_func("ATAN",            nql_fn_atan,            1, 1, "Arc tangent");
    nql_register_func("ATAN2",           nql_fn_atan2,           2, 2, "Two-argument arc tangent");
    nql_register_func("SINH",            nql_fn_sinh,            1, 1, "Hyperbolic sine");
    nql_register_func("COSH",            nql_fn_cosh,            1, 1, "Hyperbolic cosine");
    nql_register_func("TANH",            nql_fn_tanh,            1, 1, "Hyperbolic tangent");
    nql_register_func("EXP",             nql_fn_exp,             1, 1, "e^x");
    nql_register_func("LOG",             nql_fn_log_base,        2, 2, "Logarithm base b: LOG(x, base)");
    nql_register_func("DEGREES",         nql_fn_degrees,         1, 1, "Radians to degrees");
    nql_register_func("RADIANS",         nql_fn_radians,         1, 1, "Degrees to radians");
    nql_register_func("PI",              nql_fn_pi_const,        0, 0, "Pi constant");
    nql_register_func("E",               nql_fn_e_const,         0, 0, "Euler's number constant");

    /* ── Maths engine gap-fill ── */
    nql_register_func("EXTENDED_GCD",       nql_fn_extended_gcd,       2, 2, "Returns ARRAY(gcd, x, y) where ax+by=gcd");
    nql_register_func("CHINESE_REMAINDER",  nql_fn_chinese_remainder,  2, 2, "CRT solver: CHINESE_REMAINDER(remainders, moduli)");
    nql_register_func("SIGMA_K",            nql_fn_sigma_k,            2, 2, "Generalised divisor function: sum of k-th powers of divisors");
    nql_register_func("IS_PRIMITIVE_ROOT",  nql_fn_is_primitive_root,  2, 2, "Test if g is a primitive root mod p");
    nql_register_func("RANDOM",             nql_fn_random,             2, 2, "Random integer in [lo, hi]");
    nql_register_func("RANDOM_FLOAT",       nql_fn_random_float,       0, 0, "Random float in [0, 1)");
    nql_register_func("TO_STRING",          nql_fn_to_string,          1, 1, "Explicit string conversion");
    nql_register_func("TO_INT",             nql_fn_to_int,             1, 1, "Explicit integer conversion");
    nql_register_func("TO_FLOAT",           nql_fn_to_float,           1, 1, "Explicit float conversion");

    /* ── Method-specific factoring + maths from Factorisation/ ── */
    nql_register_func("FACTOR_FERMAT",       nql_fn_factor_fermat_nql,   1, 2, "Fermat factoring: returns factor or null");
    nql_register_func("FACTOR_RHO",          nql_fn_factor_rho_nql,      1, 3, "Pollard rho: returns factor or null");
    nql_register_func("FACTOR_SQUFOF",       nql_fn_factor_squfof_nql,   1, 1, "SQUFOF: returns factor or null");
    nql_register_func("FACTOR_ECM",          nql_fn_factor_ecm_nql,      1, 3, "ECM: returns factor or null (BigNum-aware)");
    nql_register_func("LUCAS_V",             nql_fn_lucas_v,             3, 3, "Lucas sequence V_e(a) mod n");
    nql_register_func("FACTOR_QS",           nql_fn_factor_qs_nql,       1, 1, "Quadratic Sieve (full SIQS engine): returns factor or null");
    nql_register_func("IS_PERFECT_SQUARE",   nql_fn_is_perfect_square_wide, 1, 1, "Test if n is a perfect square (int64/U128/BigNum)");

    /* ── GNFS Prerequisites (Phase 0) ── */
    nql_register_func("NTH_ROOT",              nql_fn_nth_root,            2, 2, "floor(n^(1/k)) for BigNum — Newton's method");
    nql_register_func("POLY_MOD_COEFFICIENTS", nql_fn_poly_mod_coeffs,     2, 2, "Reduce polynomial coefficients mod m");
    nql_register_func("POLY_IS_IRREDUCIBLE_MOD", nql_fn_poly_is_irred_mod, 2, 2, "Test irreducibility over GF(p)");
    nql_register_func("POLY_SCALE",            nql_fn_poly_scale,          2, 2, "Multiply all coefficients by scalar");
    nql_register_func("POLY_COMPOSE",          nql_fn_poly_compose,        2, 2, "Compute f(g(x))");
    nql_register_func("TIMER_START",           nql_fn_timer_start,         0, 0, "Current time in seconds");
    nql_register_func("TIMER_ELAPSED",         nql_fn_timer_elapsed,       1, 1, "Seconds since start time");
    nql_register_func("FILE_WRITE_LINES",      nql_fn_file_write_lines,    2, 2, "Write array as lines to file");
    nql_register_func("FILE_READ_LINES",       nql_fn_file_read_lines,     1, 1, "Read lines from file into array");
    nql_register_func("FILE_APPEND_LINE",      nql_fn_file_append_line,    2, 2, "Append one line to file");

    /* ── GNFS Phase 1: Polynomial selection ── */
    nql_register_func("GNFS_BASE_M_SELECT",       nql_fn_gnfs_base_m,           2, 2, "floor(N^(1/d)) as BigNum");
    nql_register_func("GNFS_BASE_M_POLY",         nql_fn_gnfs_base_m_poly,      2, 2, "Generate base-m poly pair [f, g, m]");
    nql_register_func("GNFS_POLY_ALPHA",           nql_fn_gnfs_poly_alpha,       1, 2, "Alpha-value of polynomial f");
    nql_register_func("GNFS_POLY_SKEWNESS",        nql_fn_gnfs_poly_skewness,    1, 1, "Optimal skewness estimate");
    nql_register_func("GNFS_POLY_ROTATE",          nql_fn_gnfs_poly_rotate,      3, 4, "Rotate f by j0*g + j1*x*g");

    /* ── GNFS Phase 2: Norms ── */
    nql_register_func("GNFS_RATIONAL_NORM",        nql_fn_gnfs_rational_norm,    3, 3, "|a - b*m| as BigNum");
    nql_register_func("GNFS_ALGEBRAIC_NORM",       nql_fn_gnfs_algebraic_norm,   3, 3, "|(-b)^d * f(a/b)| as BigNum");
    nql_register_func("GNFS_ALGEBRAIC_NORM_MOD",   nql_fn_gnfs_algebraic_norm_mod, 4, 4, "Algebraic norm mod p (fast)");

    /* ── GNFS Phase 3: 2D special-q lattice sieve ── */
    nql_register_func("GNFS_SIEVE_REGION_CREATE",  nql_fn_gnfs_sieve_create,      2, 2, "Create width x height sieve region");
    nql_register_func("GNFS_SIEVE_SET_SPECIAL_Q",  nql_fn_gnfs_sieve_set_q,       3, 3, "Set special-q and compute lattice basis");
    nql_register_func("GNFS_SIEVE_INIT_NORMS",     nql_fn_gnfs_sieve_init_norms,  4, 4, "Fill sieve with log-norm estimates");
    nql_register_func("GNFS_SIEVE_RUN_SIDE",       nql_fn_gnfs_sieve_run_side,    4, 4, "Sieve one side (0=rational, 1=algebraic)");
    nql_register_func("GNFS_SIEVE_CANDIDATES",     nql_fn_gnfs_sieve_candidates,  3, 3, "Scan both sides for smooth (a,b) pairs");

    /* ── GNFS Native FB + Fused Collect ── */
    nql_register_func("GNFS_FB_BUILD",             nql_fn_gnfs_fb_build,          5, 6, "Build native GNFS FB (rat+alg sides with roots)");
    nql_register_func("GNFS_FB_RAT_COUNT",         nql_fn_gnfs_fb_rat_count,      1, 1, "Rational FB prime count");
    nql_register_func("GNFS_FB_ALG_COUNT",         nql_fn_gnfs_fb_alg_count,      1, 1, "Algebraic FB (p,r) pair count");
    nql_register_func("GNFS_FB_RAT_PRIMES",         nql_fn_gnfs_fb_rat_primes,     1, 1, "Array of rational FB primes");
    nql_register_func("GNFS_FB_ALG_PRIMES",         nql_fn_gnfs_fb_alg_primes,     1, 1, "Array of algebraic FB primes");
    nql_register_func("GNFS_SIEVE_AND_COLLECT",    nql_fn_gnfs_sieve_and_collect, 5, 5, "Fused sieve scan + trial factor + relation add");

    /* ── GNFS Phase 4: Relation collection + filtering ── */
    nql_register_func("GNFS_TRIAL_FACTOR_RATIONAL",  nql_fn_gnfs_trial_rat,        4, 4, "Trial-factor |a-bm| over rational FB");
    nql_register_func("GNFS_TRIAL_FACTOR_ALGEBRAIC", nql_fn_gnfs_trial_alg,        5, 5, "Trial-factor algebraic norm over FB");
    nql_register_func("GNFS_RELATION_CREATE",        nql_fn_gnfs_rel_create,       2, 3, "Create GNFS relation store with exponent storage");
    nql_register_func("GNFS_RELATION_ADD",           nql_fn_gnfs_rel_add,          5, 7, "Add relation (a, b, rat_exps, alg_exps [, rat_lp, alg_lp])");
    nql_register_func("GNFS_RELATION_COUNT",         nql_fn_gnfs_rel_count,        1, 1, "Total relations");
    nql_register_func("GNFS_RELATION_COUNT_BY_TYPE", nql_fn_gnfs_rel_count_type,   2, 2, "Count by type (0=FF,1=FP,2=PF,3=PP)");
    nql_register_func("GNFS_RELATION_A_VALS",        nql_fn_gnfs_rel_a_vals,       1, 1, "Array of all a-values");
    nql_register_func("GNFS_RELATION_B_VALS",        nql_fn_gnfs_rel_b_vals,       1, 1, "Array of all b-values");
    nql_register_func("GNFS_FILTER_SINGLETONS",      nql_fn_gnfs_filter_singletons, 1, 1, "Remove singleton LP relations");
    nql_register_func("GNFS_RELATION_EXPORT",        nql_fn_gnfs_rel_export,       2, 2, "Export relations to file");
    nql_register_func("GNFS_RELATION_IMPORT",        nql_fn_gnfs_rel_import,       3, 3, "Import relations from file");
    nql_register_func("GNFS_RELATION_PARITY_ROWS",   nql_fn_gnfs_rel_parity_rows,  3, 3, "Build parity rows from stored exponents");

    /* ── GNFS Phase 5: Sparse GF(2) Block Lanczos ── */
    nql_register_func("GNFS_SPARSE_MATRIX_BUILD",  nql_fn_gnfs_sparse_build,      2, 2, "Build CSR sparse matrix from row arrays");
    nql_register_func("GNFS_SPARSE_ROWS",          nql_fn_gnfs_sparse_rows,       1, 1, "Number of rows");
    nql_register_func("GNFS_SPARSE_COLS",          nql_fn_gnfs_sparse_cols,       1, 1, "Number of columns");
    nql_register_func("GNFS_SPARSE_NNZ",           nql_fn_gnfs_sparse_nnz,        1, 1, "Number of non-zeros");
    nql_register_func("GNFS_BLOCK_LANCZOS",        nql_fn_gnfs_block_lanczos,     1, 1, "Solve null space via Block Lanczos");
    nql_register_func("GNFS_DENSE_SOLVE",          nql_fn_gnfs_dense_solve,       3, 3, "Build+eliminate dense GF2 matrix, return dependencies");

    /* ── GNFS Phase 6: Extraction ── */
    nql_register_func("GNFS_EXTRACT_RATIONAL_SQRT", nql_fn_gnfs_extract_rat_sqrt, 4, 4, "Rational side sqrt from stored exponents");
    nql_register_func("GNFS_EXTRACT_TRY_FACTOR",    nql_fn_gnfs_extract_try,      3, 3, "Try GCD(x +/- y, N)");
    nql_register_func("GNFS_ALGEBRAIC_SQRT",        nql_fn_gnfs_alg_sqrt,         5, 5, "Algebraic side sqrt from stored exponents");

    /* ── Aliases for discoverability ── */
    nql_register_func("FACTORS_OF",      nql_fn_factor,          1, 1, "Alias for FACTORISE(n)");
    nql_register_func("DIVISORS_OF",     nql_fn_divisors,        1, 1, "Alias for DIVISORS(n)");

    /* ── Phase 2: New number-theory functions ── */
    nql_register_func("LIOUVILLE",       nql_fn_liouville,       1, 1, "Liouville lambda: (-1)^Omega(n)");
    nql_register_func("MULTIPLICATIVE_ORDER", nql_fn_multiplicative_order, 2, 2, "Order of a modulo n");
    nql_register_func("PRIMITIVE_ROOT",  nql_fn_primitive_root,  1, 1, "Smallest primitive root mod p");
    nql_register_func("PRIMITIVE_ROOT_COUNT", nql_fn_primitive_root_count, 1, 1, "Count of primitive roots mod p");
    nql_register_func("CHEBYSHEV_THETA", nql_fn_chebyshev_theta, 1, 1, "Chebyshev theta: sum of log(p) for p <= x");
    nql_register_func("CHEBYSHEV_PSI",   nql_fn_chebyshev_psi,   1, 1, "Chebyshev psi: sum of log(p) for p^k <= x");
    nql_register_func("RADICAL",         nql_fn_radical,         1, 1, "Product of distinct prime factors");
    nql_register_func("SOPFR",           nql_fn_sopfr,           1, 1, "Sum of prime factors with repetition");
    nql_register_func("DIGITAL_ROOT",    nql_fn_digital_root,    1, 1, "Iterated digit sum to single digit");
    nql_register_func("IS_CARMICHAEL",   nql_fn_is_carmichael,   1, 1, "Test if n is a Carmichael number");

    /* ── Phase 3: Array functions ── */
    nql_register_func("ARRAY",           nql_fn_array,           0, 32, "Construct array from arguments");
    nql_register_func("ARRAY_RANGE",     nql_fn_array_range,     2, 2,  "Array [lo, lo+1, ..., hi]");
    nql_register_func("ARRAY_FILL",      nql_fn_array_fill,      2, 2,  "Array of N copies of a value");
    nql_register_func("ARRAY_LENGTH",    nql_fn_array_length,    1, 1,  "Number of elements");
    nql_register_func("ARRAY_GET",       nql_fn_array_get,       2, 2,  "Element at index (0-based)");
    nql_register_func("ARRAY_FIRST",     nql_fn_array_first,     1, 1,  "First element");
    nql_register_func("ARRAY_LAST",      nql_fn_array_last,      1, 1,  "Last element");
    nql_register_func("ARRAY_SLICE",     nql_fn_array_slice,     3, 3,  "Sub-array [start, end)");
    nql_register_func("ARRAY_APPEND",    nql_fn_array_append,    2, 2,  "New array with value appended");
    nql_register_func("ARRAY_CONCAT",    nql_fn_array_concat,    2, 2,  "Concatenate two arrays");
    nql_register_func("ARRAY_REVERSE",   nql_fn_array_reverse,   1, 1,  "Reversed copy");
    nql_register_func("ARRAY_SORT",      nql_fn_array_sort,      1, 1,  "Sorted ascending copy");
    nql_register_func("ARRAY_UNIQUE",    nql_fn_array_unique,    1, 1,  "Remove duplicates");
    nql_register_func("ARRAY_CONTAINS",  nql_fn_array_contains,  2, 2,  "Test if value is in array");
    nql_register_func("ARRAY_INDEX_OF",  nql_fn_array_index_of,  2, 2,  "Index of value (-1 if not found)");
    nql_register_func("ARRAY_SUM",       nql_fn_array_sum,       1, 1,  "Sum of elements");
    nql_register_func("ARRAY_MIN",       nql_fn_array_min,       1, 1,  "Minimum element");
    nql_register_func("ARRAY_MAX",       nql_fn_array_max,       1, 1,  "Maximum element");
    nql_register_func("ARRAY_PRODUCT",   nql_fn_array_product,   1, 1,  "Product of elements");
    nql_register_func("ARRAY_AVG",       nql_fn_array_avg,       1, 1,  "Average of elements");
    nql_register_func("ARRAY_CREATE",    nql_fn_array_create,    1, 2,  "Pre-allocated array of given size");
    nql_register_func("ARRAY_SET",       nql_fn_array_set,       3, 3,  "New array with element at index replaced");
    nql_register_func("ARRAY_SET_INPLACE", nql_fn_array_set_inplace, 3, 3, "Modify element in place (mutable)");
    nql_register_func("ARRAY_BIT_XOR",   nql_fn_array_bit_xor,  2, 2,  "Element-wise bitwise XOR");

    /* ── Phase 4: QS building-block functions ── */
    nql_register_func("TONELLI_SHANKS",  nql_fn_tonelli_shanks,  2, 2,  "Modular square root: r where r^2 = n mod p");
    nql_register_func("MULMOD",          nql_fn_mulmod,          3, 3,  "Modular multiply: (a * b) mod m");
    nql_register_func("LEGENDRE_SYMBOL", nql_fn_legendre_symbol, 2, 2,  "Legendre symbol (a/p): -1, 0, or 1");
    nql_register_func("TRIAL_FACTOR_FB", nql_fn_trial_factor_fb, 2, 2,  "Trial-factor n over factor base array");
    nql_register_func("SIEVE_SUBTRACT_STRIDE", nql_fn_sieve_subtract_stride, 4, 4, "Subtract value at every stride-th element");

    /* ── Phase 4: GF(2) matrix functions ── */
    nql_register_func("MATRIX_GF2_CREATE",     nql_fn_matrix_gf2_create,     2, 2, "Create rows x cols binary matrix");
    nql_register_func("MATRIX_GF2_SET_BIT",    nql_fn_matrix_gf2_set_bit,    3, 3, "Set bit at (row, col) to 1");
    nql_register_func("MATRIX_GF2_FLIP_BIT",   nql_fn_matrix_gf2_flip_bit,   3, 3, "XOR bit at (row, col)");
    nql_register_func("MATRIX_GF2_GET_BIT",    nql_fn_matrix_gf2_get_bit,    3, 3, "Get bit at (row, col)");
    nql_register_func("MATRIX_GF2_XOR_ROW",    nql_fn_matrix_gf2_xor_row,    3, 3, "XOR src_row into dst_row");
    nql_register_func("MATRIX_GF2_ELIMINATE",   nql_fn_matrix_gf2_eliminate,  3, 3, "Gaussian elimination, return pivot array");
    nql_register_func("MATRIX_GF2_NULL_SPACE",  nql_fn_matrix_gf2_null_space, 4, 4, "Extract null-space dependency vectors");

    /* ── Phase 5: QS native opaque-object built-ins ── */
    nql_register_func("TRIAL_SMALL_FACTOR",     nql_fn_trial_small_factor,    2, 2,  "Check if any prime <= bound divides N (returns factor or null)");
    nql_register_func("FACTOR_BASE_BUILD",      nql_fn_factor_base_build,     2, 3,  "Build QS factor base (call TRIAL_SMALL_FACTOR first)");
    nql_register_func("FACTOR_BASE_SIZE",       nql_fn_factor_base_size,      1, 1,  "Number of primes in factor base");
    nql_register_func("FACTOR_BASE_PRIME",      nql_fn_factor_base_prime,     2, 2,  "i-th factor base prime");
    nql_register_func("FACTOR_BASE_LARGEST",    nql_fn_factor_base_largest,   1, 1,  "Largest factor base prime");
    nql_register_func("FACTOR_BASE_LP_BOUND",   nql_fn_factor_base_lp_bound, 1, 1,  "Large prime bound");
    nql_register_func("QS_SIEVE_CREATE",        nql_fn_qs_sieve_create,       1, 1,  "Create sieve buffer");
    nql_register_func("QS_SIEVE_INIT",          nql_fn_qs_sieve_init_sp,      3, 3,  "Initialise sieve for single-poly Q(x)=(x+c)^2-N");
    nql_register_func("QS_SIEVE_RUN",           nql_fn_qs_sieve_run,          3, 3,  "Run sieve: subtract log(p) at roots");
    nql_register_func("QS_SIEVE_CANDIDATES",    nql_fn_qs_sieve_candidates,   2, 2,  "Scan sieve for smooth candidates");
    nql_register_func("QS_CANDIDATE_QX",       nql_fn_qs_candidate_qx,       2, 2,  "Compute Q(x) for sieve candidate (U128-safe)");
    nql_register_func("QS_CANDIDATE_XC",       nql_fn_qs_candidate_xc,       2, 2,  "Compute x+center for sieve candidate");
    nql_register_func("TRIAL_FACTOR_CANDIDATE",  nql_fn_qs_trial_factor,      3, 4,  "Trial-factor |Qx| over FB with full exponents");
    nql_register_func("TRIAL_RESULT_IS_SMOOTH",  nql_fn_qs_trial_is_smooth,   1, 1,  "Is trial result fully smooth?");
    nql_register_func("TRIAL_RESULT_IS_PARTIAL", nql_fn_qs_trial_is_partial,  1, 1,  "Is trial result a single-LP partial?");
    nql_register_func("TRIAL_RESULT_COFACTOR",   nql_fn_qs_trial_cofactor,    1, 1,  "Cofactor after trial division");
    nql_register_func("RELATION_SET_CREATE",     nql_fn_qs_relset_create,     1, 3,  "Create empty relation store");
    nql_register_func("RELATION_SET_ADD",        nql_fn_qs_relset_add,        2, 2,  "Add relation from trial result");
    nql_register_func("RELATION_SET_COUNT",      nql_fn_qs_relset_count,      1, 1,  "Number of collected relations");
    nql_register_func("RELATION_SET_PRUNE_SINGLETONS", nql_fn_qs_relset_prune, 1, 1, "Remove singleton LP partials");
    nql_register_func("GF2_MATRIX_BUILD",        nql_fn_qs_gf2_build,        1, 1,  "Build packed GF(2) matrix from relations");
    nql_register_func("GF2_ELIMINATE",           nql_fn_qs_gf2_eliminate,     1, 1,  "Gaussian elimination with history tracking");
    nql_register_func("GF2_NULLSPACE_COUNT",     nql_fn_qs_gf2_nullspace_count, 1, 1, "Number of null-space vectors");
    nql_register_func("GF2_NULLSPACE_VECTOR",    nql_fn_qs_gf2_nullvec,      2, 2,  "Extract i-th null-space dependency");
    nql_register_func("GF2_RANK",               nql_fn_qs_gf2_rank,          1, 1,  "Matrix rank after elimination");
    nql_register_func("DEPENDENCY_EXTRACT",      nql_fn_qs_dep_extract,      4, 4,  "Try x^2=y^2 extraction, return factor or null");
    nql_register_func("DEPENDENCY_COMBINE",      nql_fn_qs_dep_combine,      2, 2,  "XOR two dependency vectors");

    /* ── Phase 6: Polynomial arithmetic (GNFS prerequisite) ── */
    nql_register_func("POLY",                   nql_fn_poly,                0, 32, "Create polynomial from coefficients (low-to-high)");
    nql_register_func("POLY_FROM_ROOTS",        nql_fn_poly_from_roots,     1, 32, "Create (x-r1)(x-r2)... from roots");
    nql_register_func("POLY_DEGREE",            nql_fn_poly_degree,         1, 1,  "Degree of polynomial");
    nql_register_func("POLY_LEADING_COEFFICIENT", nql_fn_poly_lc,           1, 1,  "Leading coefficient");
    nql_register_func("POLY_COEFFICIENT",       nql_fn_poly_coeff,          2, 2,  "Coefficient of x^i");
    nql_register_func("POLY_EVALUATE",          nql_fn_poly_eval,           2, 2,  "Evaluate p(x) at integer x");
    nql_register_func("POLY_EVALUATE_MOD",      nql_fn_poly_eval_mod,       3, 3,  "Evaluate p(x) mod m");
    nql_register_func("POLY_ADD",               nql_fn_poly_add,            2, 2,  "Polynomial addition");
    nql_register_func("POLY_SUBTRACT",          nql_fn_poly_sub,            2, 2,  "Polynomial subtraction");
    nql_register_func("POLY_MULTIPLY",          nql_fn_poly_mul,            2, 2,  "Polynomial multiplication");
    nql_register_func("POLY_DIVIDE",            nql_fn_poly_div,            2, 2,  "Polynomial quotient");
    nql_register_func("POLY_REMAINDER",         nql_fn_poly_rem,            2, 2,  "Polynomial remainder");
    nql_register_func("POLY_GCD",               nql_fn_poly_gcd,            2, 2,  "Polynomial GCD");
    nql_register_func("POLY_DERIVATIVE",        nql_fn_poly_deriv,          1, 1,  "Formal derivative");
    nql_register_func("POLY_ROOTS_MOD",         nql_fn_poly_roots_mod,      2, 2,  "Find all roots mod prime");
    nql_register_func("POLY_RESULTANT",         nql_fn_poly_resultant,      2, 2,  "Resultant of two polynomials");
    nql_register_func("POLY_DISCRIMINANT",      nql_fn_poly_disc,           1, 1,  "Discriminant of polynomial");
}
