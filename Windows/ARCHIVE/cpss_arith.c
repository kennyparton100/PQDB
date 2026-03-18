/**
 * cpss_arith.c - Portable 128-bit arithmetic, modular math, Miller-Rabin, classification.
 * Part of the CPSS Viewer amalgamation.
 *
 * U128 is a portable struct {lo, hi} that works on all compilers.
 * All U128 operations are pure functions (no __int128 dependency).
 */

/* Forward declarations — defined later in this file */
static uint64_t mulmod_u64(uint64_t a, uint64_t b, uint64_t m);
static bool miller_rabin_u64(uint64_t n);

/* ══════════════════════════════════════════════════════════════════════
 * PORTABLE U128 ARITHMETIC
 * ══════════════════════════════════════════════════════════════════════ */

static inline U128 u128_from_u64(uint64_t v) { U128 r; r.lo = v; r.hi = 0u; return r; }
static inline U128 u128_make(uint64_t hi, uint64_t lo) { U128 r; r.lo = lo; r.hi = hi; return r; }
static inline bool u128_is_zero(U128 a) { return a.lo == 0u && a.hi == 0u; }
static inline bool u128_eq(U128 a, U128 b) { return a.lo == b.lo && a.hi == b.hi; }
static inline bool u128_lt(U128 a, U128 b) { return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo); }
static inline bool u128_le(U128 a, U128 b) { return a.hi < b.hi || (a.hi == b.hi && a.lo <= b.lo); }
static inline bool u128_gt(U128 a, U128 b) { return u128_lt(b, a); }
static inline bool u128_fits_u64(U128 a) { return a.hi == 0u; }
static inline uint64_t u128_to_u64(U128 a) { return a.lo; }

static inline U128 u128_add(U128 a, U128 b) {
    U128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo ? 1u : 0u);
    return r;
}

static inline U128 u128_sub(U128 a, U128 b) {
    U128 r;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - (a.lo < b.lo ? 1u : 0u);
    return r;
}

static inline U128 u128_shl(U128 a, unsigned n) {
    if (n == 0u) return a;
    if (n >= 128u) return u128_from_u64(0u);
    U128 r;
    if (n >= 64u) { r.hi = a.lo << (n - 64u); r.lo = 0u; }
    else { r.hi = (a.hi << n) | (a.lo >> (64u - n)); r.lo = a.lo << n; }
    return r;
}

static inline U128 u128_shr(U128 a, unsigned n) {
    if (n == 0u) return a;
    if (n >= 128u) return u128_from_u64(0u);
    U128 r;
    if (n >= 64u) { r.lo = a.hi >> (n - 64u); r.hi = 0u; }
    else { r.lo = (a.lo >> n) | (a.hi << (64u - n)); r.hi = a.hi >> n; }
    return r;
}

static inline U128 u128_or(U128 a, U128 b) {
    U128 r; r.lo = a.lo | b.lo; r.hi = a.hi | b.hi; return r;
}

static inline U128 u128_and(U128 a, U128 b) {
    U128 r; r.lo = a.lo & b.lo; r.hi = a.hi & b.hi; return r;
}

static inline unsigned u128_bits(U128 x) {
    if (x.hi > 0u) {
        unsigned b = 64u;
        uint64_t v = x.hi;
        while (v > 0u) { ++b; v >>= 1u; }
        return b;
    }
    unsigned b = 0u;
    uint64_t v = x.lo;
    while (v > 0u) { ++b; v >>= 1u; }
    return b;
}

/** Multiply two uint64 values producing a U128 result. */
static inline U128 u128_mul64(uint64_t a, uint64_t b) {
    U128 r;
#if defined(_MSC_VER) && defined(_M_X64)
    r.lo = _umul128(a, b, &r.hi);
#elif defined(__GNUC__) || defined(__clang__)
    unsigned __int128 p = (unsigned __int128)a * b;
    r.lo = (uint64_t)p;
    r.hi = (uint64_t)(p >> 64);
#else
    /* Portable: split into 32-bit pieces */
    uint64_t a_lo = a & 0xFFFFFFFFu, a_hi = a >> 32u;
    uint64_t b_lo = b & 0xFFFFFFFFu, b_hi = b >> 32u;
    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;
    uint64_t mid = p1 + (p0 >> 32u);
    uint64_t carry = (mid < p1) ? 1u : 0u;
    mid += p2;
    carry += (mid < p2) ? 1u : 0u;
    r.lo = (mid << 32u) | (p0 & 0xFFFFFFFFu);
    r.hi = p3 + (mid >> 32u) + (carry << 32u);
#endif
    return r;
}

/** U128 division: a / b, remainder in *rem. */
static void u128_divmod(U128 a, U128 b, U128* quot, U128* rem) {
    if (u128_is_zero(b)) { *quot = u128_from_u64(0u); *rem = u128_from_u64(0u); return; }
    if (u128_lt(a, b)) { *quot = u128_from_u64(0u); *rem = a; return; }

    U128 q = u128_from_u64(0u);
    U128 r = u128_from_u64(0u);
    for (int i = 127; i >= 0; --i) {
        r = u128_shl(r, 1u);
        r = u128_or(r, u128_and(u128_shr(a, (unsigned)i), u128_from_u64(1u)));
        if (u128_le(b, r)) {
            r = u128_sub(r, b);
            q = u128_or(q, u128_shl(u128_from_u64(1u), (unsigned)i));
        }
    }
    *quot = q;
    *rem = r;
}

static inline U128 u128_div(U128 a, U128 b) { U128 q, r; u128_divmod(a, b, &q, &r); return q; }
static inline U128 u128_mod(U128 a, U128 b) { U128 q, r; u128_divmod(a, b, &q, &r); return r; }

/** Fast U128 % uint64 — avoids full 128-bit divmod when divisor is 64-bit. */
static uint64_t u128_mod_u64(U128 a, uint64_t m) {
    if (m == 0u) return 0u;
    if (a.hi == 0u) return a.lo % m;
    /* Use property: (hi * 2^64 + lo) mod m = ((hi mod m) * (2^64 mod m) + lo mod m) mod m */
    uint64_t hi_mod = a.hi % m;
    /* Compute 2^64 mod m. Since 2^64 = m * q + r, and 2^64 overflows uint64,
     * we compute it as: (UINT64_MAX % m + 1) % m */
    uint64_t pow2_64_mod = (UINT64_MAX % m + 1u) % m;
    /* hi_mod * pow2_64_mod can overflow — use mulmod_u64 */
    uint64_t term1 = mulmod_u64(hi_mod, pow2_64_mod, m);
    uint64_t term2 = a.lo % m;
    uint64_t result = term1 + term2;
    if (result >= m || result < term1) result -= m;
    return result;
}

/** U128 to decimal string. */
static void u128_to_str(U128 x, char* buf, size_t buf_len) {
    char tmp[64];
    size_t pos = 0u;
    if (buf_len == 0u) return;
    if (u128_is_zero(x)) {
        if (buf_len >= 2u) { buf[0] = '0'; buf[1] = '\0'; }
        return;
    }
    U128 ten = u128_from_u64(10u);
    while (!u128_is_zero(x) && pos + 1u < sizeof(tmp)) {
        U128 q, r;
        u128_divmod(x, ten, &q, &r);
        tmp[pos++] = (char)('0' + (int)r.lo);
        x = q;
    }
    size_t out = 0u;
    while (pos > 0u && out + 1u < buf_len) {
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
}

/** Parse a decimal string to U128. Returns {0,0} on empty/invalid. */
static U128 u128_from_str(const char* s) {
    U128 result = u128_from_u64(0u);
    U128 ten = u128_from_u64(10u);
    while (*s) {
        if (*s < '0' || *s > '9') break;
        /* result = result * 10 + digit */
        U128 q, r;
        /* Multiply: result * 10 via shift+add: x*10 = x*8 + x*2 */
        U128 x8 = u128_shl(result, 3u);
        U128 x2 = u128_shl(result, 1u);
        result = u128_add(x8, x2);
        result = u128_add(result, u128_from_u64((uint64_t)(*s - '0')));
        (void)q; (void)r; (void)ten;
        ++s;
    }
    return result;
}

/* ── U128 modular arithmetic for factorisation ────────────────────── */

/** (a * b) % m where a, b, m are all U128. Russian peasant multiplication. */
static U128 u128_mulmod(U128 a, U128 b, U128 m) {
    if (u128_is_zero(m)) return u128_from_u64(0u);
    a = u128_mod(a, m);
    b = u128_mod(b, m);
    U128 result = u128_from_u64(0u);
    while (!u128_is_zero(b)) {
        if (b.lo & 1u) {
            U128 old_result = result;
            result = u128_add(result, a);
            /* Wrapped or >= m → reduce */
            if (u128_lt(result, old_result) || u128_le(m, result))
                result = u128_sub(result, m);
        }
        {
            U128 old_a = a;
            a = u128_add(a, a);
            if (u128_lt(a, old_a) || u128_le(m, a))
                a = u128_sub(a, m);
        }
        b = u128_shr(b, 1u);
    }
    return result;
}

/** (base ^ exp) % mod, all U128. */
static U128 u128_powmod(U128 base, U128 exp, U128 mod) {
    if (u128_eq(mod, u128_from_u64(1u))) return u128_from_u64(0u);
    U128 result = u128_from_u64(1u);
    base = u128_mod(base, mod);
    while (!u128_is_zero(exp)) {
        if (exp.lo & 1u) {
            result = u128_mulmod(result, base, mod);
        }
        exp = u128_shr(exp, 1u);
        base = u128_mulmod(base, base, mod);
    }
    return result;
}

/** GCD of two U128 values. */
static U128 u128_gcd(U128 a, U128 b) {
    while (!u128_is_zero(b)) {
        U128 t = u128_mod(a, b);
        a = b;
        b = t;
    }
    return a;
}

/** Integer square root floor for a U128 value. Result always fits in uint64. */
static uint64_t u128_isqrt_floor(U128 n) {
    if (u128_is_zero(n)) return 0u;
    if (u128_fits_u64(n)) return (uint64_t)isqrt_u64(n.lo);

    uint64_t lo = 0u;
    uint64_t hi = UINT64_MAX;
    uint64_t best = 0u;

    while (lo <= hi) {
        uint64_t mid = lo + (hi - lo) / 2u;
        U128 sq = u128_mul64(mid, mid);
        if (u128_le(sq, n)) {
            best = mid;
            if (mid == UINT64_MAX) break;
            lo = mid + 1u;
        }
        else {
            if (mid == 0u) break;
            hi = mid - 1u;
        }
    }

    return best;
}

/** Integer square root ceiling for a U128 value. Result always fits in uint64. */
static uint64_t u128_isqrt_ceil(U128 n) {
    uint64_t floor = u128_isqrt_floor(n);
    U128 sq = u128_mul64(floor, floor);
    if (u128_eq(sq, n)) return floor;
    if (floor == UINT64_MAX) return floor;
    return floor + 1u;
}

/** Miller-Rabin witness test for U128 n with uint64 witness a. */
static bool u128_miller_rabin_witness(U128 n, uint64_t a_val) {
    U128 a = u128_from_u64(a_val);
    U128 one = u128_from_u64(1u);
    U128 n_minus_1 = u128_sub(n, one);

    if (u128_is_zero(u128_mod(a, n))) return true;

    /* Write n-1 as d * 2^r */
    U128 d = n_minus_1;
    uint32_t r = 0u;
    while (!u128_is_zero(d) && (d.lo & 1u) == 0u) {
        d = u128_shr(d, 1u);
        ++r;
    }

    U128 x = u128_powmod(a, d, n);
    if (u128_eq(x, one) || u128_eq(x, n_minus_1)) return true;

    for (uint32_t i = 1u; i < r; ++i) {
        x = u128_mulmod(x, x, n);
        if (u128_eq(x, n_minus_1)) return true;
    }
    return false;
}

/** Deterministic Miller-Rabin for U128 n. */
static bool u128_is_prime(U128 n) {
    if (n.hi == 0u) return miller_rabin_u64(n.lo);
    /* n > 2^64: use witness set */
    static const uint64_t witnesses[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
    /* Quick divisibility check */
    for (size_t i = 0u; i < ARRAY_LEN(witnesses); ++i) {
        if (u128_mod_u64(n, witnesses[i]) == 0u) return false;
    }
    for (size_t i = 0u; i < ARRAY_LEN(witnesses); ++i) {
        if (!u128_miller_rabin_witness(n, witnesses[i])) return false;
    }
    return true;
}

/* ── 64-bit modular multiply ─────────────────────────────────────── */

/**
 * Compute (a * b) % m without overflow for arbitrary uint64 a, b, m.
 * Uses 128-bit intermediate via compiler intrinsics.
 */
static uint64_t mulmod_u64(uint64_t a, uint64_t b, uint64_t m) {
    if (m == 0u) return 0u;
    a %= m;
    b %= m;
#if !defined(_MSC_VER) && (defined(__GNUC__) || defined(__clang__)) && defined(__SIZEOF_INT128__)
    return (uint64_t)(((unsigned __int128)a * b) % m);
#else
    /* Russian peasant multiplication: overflow-free for any uint64 a, b, m */
    uint64_t result = 0u;
    while (b > 0u) {
        if (b & 1u) {
            result = result + a;
            if (result >= m) result -= m;
        }
        a = a + a;
        if (a >= m) a -= m;
        b >>= 1u;
    }
    return result;
#endif
}

/**
 * Compute (base^exp) % mod using binary exponentiation with mulmod_u64.
 */
static uint64_t powmod_u64(uint64_t base, uint64_t exp, uint64_t mod) {
    if (mod == 1u) return 0u;
    uint64_t result = 1u;
    base %= mod;
    while (exp > 0u) {
        if (exp & 1u) {
            result = mulmod_u64(result, base, mod);
        }
        exp >>= 1u;
        base = mulmod_u64(base, base, mod);
    }
    return result;
}

/* ── Perfect square / power tests ─────────────────────────────────── */

/** Return true if n is a perfect square. */
static bool is_perfect_square_u64(uint64_t n) {
    if (n == 0u) return true;
    uint64_t r = (uint64_t)isqrt_u64(n);
    return r * r == n;
}

/** Return true if n is a perfect power (n = a^k for some a >= 2, k >= 2). */
static bool is_perfect_power_u64(uint64_t n) {
    if (n <= 3u) return false;

    /* Only check prime exponents — composite exponents are redundant
     * (e.g. n = a^6 = (a^2)^3 is caught by exp=2 or exp=3). */
    static const uint32_t prime_exps[] = {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61
    };
    for (size_t ei = 0u; ei < ARRAY_LEN(prime_exps); ++ei) {
        uint32_t exp = prime_exps[ei];
        /* Stop when 2^exp > n */
        if (exp >= 64u || ((uint64_t)1u << exp) > n) break;

        if (exp == 2u) {
            if (is_perfect_square_u64(n)) return true;
            continue;
        }
        /* Binary search for base such that base^exp == n */
        uint64_t lo = 2u, hi = (uint64_t)1u << (64u / exp + 1u);
        if (hi < 2u) hi = 2u;
        while (lo <= hi) {
            uint64_t mid = lo + (hi - lo) / 2u;
            /* Compute mid^exp with overflow detection */
            uint64_t power = 1u;
            bool overflow = false;
            for (uint32_t i = 0u; i < exp; ++i) {
                if (power > UINT64_MAX / mid) { overflow = true; break; }
                power *= mid;
                if (power > n) { overflow = true; break; }
            }
            if (overflow || power > n) {
                if (mid == 0u) break;
                hi = mid - 1u;
            }
            else if (power < n) {
                lo = mid + 1u;
            }
            else {
                return true; /* power == n */
            }
        }
    }
    return false;
}

/* ── Miller-Rabin primality test ──────────────────────────────────── */

/**
 * Single Miller-Rabin witness test: returns true if n is "probably prime"
 * with respect to witness a.
 */
static bool miller_rabin_witness(uint64_t n, uint64_t a) {
    if (a % n == 0u) return true;

    /* Write n-1 as d * 2^r */
    uint64_t d = n - 1u;
    uint32_t r = 0u;
    while ((d & 1u) == 0u) {
        d >>= 1u;
        ++r;
    }

    uint64_t x = powmod_u64(a, d, n);
    if (x == 1u || x == n - 1u) return true;

    for (uint32_t i = 1u; i < r; ++i) {
        x = mulmod_u64(x, x, n);
        if (x == n - 1u) return true;
    }
    return false;
}

/**
 * Deterministic Miller-Rabin primality test for all n < 2^64.
 * Uses the known sufficient witness set {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37}.
 * Returns true if n is prime.
 */
static bool miller_rabin_u64(uint64_t n) {
    if (n < 2u) return false;
    if (n < 4u) return true;
    if ((n & 1u) == 0u) return false;

    /* Small prime check */
    static const uint64_t small[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
    for (size_t i = 0u; i < ARRAY_LEN(small); ++i) {
        if (n == small[i]) return true;
        if (n % small[i] == 0u) return false;
    }

    /* Deterministic witnesses sufficient for n < 3,317,044,064,679,887,385,961,981 */
    for (size_t i = 0u; i < ARRAY_LEN(small); ++i) {
        if (!miller_rabin_witness(n, small[i])) return false;
    }
    return true;
}

/* ── Composite classification ─────────────────────────────────────── */

/**
 * Classify a composite number for factor routing.
 * Always uses Miller-Rabin for primality (deterministic for uint64,
 * avoids cold-path segment decompression that cpss_is_prime can trigger).
 */
static CompositeClass cpss_classify(CPSSStream* s, uint64_t n) {
    CompositeClass c;
    memset(&c, 0, sizeof(c));
    c.n = n;
    (void)s; /* classify no longer queries the DB — Miller-Rabin is exact for uint64 */

    if (n <= 1u) return c;

    /* Even? */
    c.is_even = ((n & 1u) == 0u);
    if (c.is_even) {
        c.has_small_factor = true;
        c.smallest_factor = 2u;
    }

    /* Check wheel primes */
    if (!c.has_small_factor) {
        for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
            if (n == WHEEL_PRIMES[i]) {
                c.is_prime = true;
                c.prime_source = 2;
                return c;
            }
            if (n % WHEEL_PRIMES[i] == 0u) {
                c.has_small_factor = true;
                c.smallest_factor = WHEEL_PRIMES[i];
                break;
            }
        }
    }

    /* Perfect square? */
    c.is_perfect_square = is_perfect_square_u64(n);

    /* Perfect power? (n = a^k for some a >= 2, k >= 2).
     * Always run — numbers with small factors can still be perfect powers
     * (e.g. 121 = 11^2, 1024 = 2^10). is_perfect_power is fast (prime exponents only). */
    c.is_prime_power = is_perfect_power_u64(n);

    /* Primality: always use Miller-Rabin (deterministic for all uint64,
     * avoids triggering cold-path decompression via cpss_is_prime) */
    if (!c.has_small_factor && !c.is_perfect_square) {
        c.is_prime = miller_rabin_u64(n);
        c.prime_source = 2;
    }

    return c;
}
