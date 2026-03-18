/**
 * cpss_bignum.c - Fixed-max-width BigNum arithmetic for 1024-bit support.
 * Part of the CPSS Viewer amalgamation.
 *
 * BigNum is a stack-allocated array of uint64 limbs (little-endian).
 * BN_MAX_LIMBS (64) gives 4096 bits. All ops respect the 'used' field
 * to avoid touching unused limbs.
 *
 * Provides: core ops, schoolbook multiply, Montgomery reduction,
 * binary GCD (Stein's), exact division, and string I/O.
 */

/* ══════════════════════════════════════════════════════════════════════
 * CORE OPERATIONS
 * ══════════════════════════════════════════════════════════════════════ */

static inline void bn_zero(BigNum* a) {
    memset(a, 0, sizeof(*a));
    a->used = 1u;
}

static inline void bn_from_u64(BigNum* a, uint64_t v) {
    memset(a, 0, sizeof(*a));
    a->limbs[0] = v;
    a->used = 1u;
}

static inline void bn_copy(BigNum* dst, const BigNum* src) {
    *dst = *src;
}

static inline void bn_swap(BigNum* a, BigNum* b) {
    BigNum t = *a; *a = *b; *b = t;
}

static void bn_normalize(BigNum* a) {
    while (a->used > 1u && a->limbs[a->used - 1u] == 0u) --a->used;
}

static inline bool bn_is_zero(const BigNum* a) {
    return a->used == 1u && a->limbs[0] == 0u;
}

static inline bool bn_is_one(const BigNum* a) {
    return a->used == 1u && a->limbs[0] == 1u;
}

static inline bool bn_is_odd(const BigNum* a) {
    return (a->limbs[0] & 1u) != 0u;
}

static inline bool bn_is_even(const BigNum* a) {
    return (a->limbs[0] & 1u) == 0u;
}

static inline bool bn_fits_u64(const BigNum* a) {
    return a->used <= 1u;
}

static inline bool bn_fits_u128(const BigNum* a) {
    return a->used <= 2u;
}

static inline uint64_t bn_to_u64(const BigNum* a) {
    return a->limbs[0];
}

static U128 bn_to_u128(const BigNum* a) {
    U128 r;
    r.lo = a->limbs[0];
    r.hi = (a->used >= 2u) ? a->limbs[1] : 0u;
    return r;
}

static void bn_from_u128(BigNum* a, U128 v) {
    memset(a, 0, sizeof(*a));
    a->limbs[0] = v.lo;
    a->limbs[1] = v.hi;
    a->used = (v.hi > 0u) ? 2u : 1u;
}

/** Return total bit length. */
static unsigned bn_bitlen(const BigNum* a) {
    if (bn_is_zero(a)) return 0u;
    uint64_t top = a->limbs[a->used - 1u];
    unsigned bits = (unsigned)(a->used - 1u) * 64u;
    while (top > 0u) { ++bits; top >>= 1u; }
    return bits;
}

/** Count trailing zero bits. */
static unsigned bn_ctz(const BigNum* a) {
    if (bn_is_zero(a)) return 0u;
    unsigned count = 0u;
    for (uint32_t i = 0u; i < a->used; ++i) {
        if (a->limbs[i] != 0u) {
            uint64_t v = a->limbs[i];
            while ((v & 1u) == 0u) { ++count; v >>= 1u; }
            return count;
        }
        count += 64u;
    }
    return count;
}

/** Three-way comparison: -1 if a < b, 0 if equal, +1 if a > b. */
static int bn_cmp(const BigNum* a, const BigNum* b) {
    if (a->used != b->used) return (a->used > b->used) ? 1 : -1;
    for (int i = (int)a->used - 1; i >= 0; --i) {
        if (a->limbs[i] != b->limbs[i])
            return (a->limbs[i] > b->limbs[i]) ? 1 : -1;
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * ARITHMETIC
 * ══════════════════════════════════════════════════════════════════════ */

/** a = a + b. May grow a->used by 1. */
static void bn_add(BigNum* a, const BigNum* b) {
    uint64_t carry = 0u;
    uint32_t max_used = a->used > b->used ? a->used : b->used;
    for (uint32_t i = 0u; i < max_used || carry; ++i) {
        if (i >= BN_MAX_LIMBS) {
            if (carry) { fprintf(stderr, "BN OVERFLOW: bn_add carry lost at %u limbs (%u bits)\n", BN_MAX_LIMBS, BN_MAX_LIMBS*64); fflush(stderr); }
            break;
        }
        uint64_t av = (i < a->used) ? a->limbs[i] : 0u;
        uint64_t bv = (i < b->used) ? b->limbs[i] : 0u;
        uint64_t sum = av + bv + carry;
        carry = (sum < av || (carry && sum == av)) ? 1u : 0u;
        a->limbs[i] = sum;
        if (i >= a->used) a->used = i + 1u;
    }
    if (a->used < max_used) a->used = max_used;
    bn_normalize(a);
}

/** a = a - b (a >= b assumed). */
static void bn_sub(BigNum* a, const BigNum* b) {
    uint64_t borrow = 0u;
    for (uint32_t i = 0u; i < a->used; ++i) {
        uint64_t bv = (i < b->used) ? b->limbs[i] : 0u;
        uint64_t diff = a->limbs[i] - bv - borrow;
        borrow = (a->limbs[i] < bv + borrow || (borrow && bv == UINT64_MAX)) ? 1u : 0u;
        a->limbs[i] = diff;
    }
    bn_normalize(a);
}

static void bn_add_u64(BigNum* a, uint64_t v) {
    BigNum t; bn_from_u64(&t, v);
    bn_add(a, &t);
}

static void bn_sub_u64(BigNum* a, uint64_t v) {
    BigNum t; bn_from_u64(&t, v);
    bn_sub(a, &t);
}

/** Left-shift by 1 bit. */
static void bn_shl1(BigNum* a) {
    uint64_t carry = 0u;
    for (uint32_t i = 0u; i < a->used; ++i) {
        uint64_t new_carry = a->limbs[i] >> 63u;
        a->limbs[i] = (a->limbs[i] << 1u) | carry;
        carry = new_carry;
    }
    if (carry && a->used < BN_MAX_LIMBS) {
        a->limbs[a->used] = carry;
        ++a->used;
    }
}

/** Right-shift by 1 bit. */
static void bn_shr1(BigNum* a) {
    for (uint32_t i = 0u; i < a->used; ++i) {
        a->limbs[i] >>= 1u;
        if (i + 1u < a->used) a->limbs[i] |= (a->limbs[i + 1u] & 1u) << 63u;
    }
    bn_normalize(a);
}

/** Right-shift by n bits. */
static void bn_shr(BigNum* a, unsigned n) {
    if (n == 0u) return;
    unsigned limb_shift = n / 64u;
    unsigned bit_shift = n % 64u;
    if (limb_shift >= a->used) { bn_from_u64(a, 0u); return; }
    if (limb_shift > 0u) {
        for (uint32_t i = 0u; i + limb_shift < a->used; ++i)
            a->limbs[i] = a->limbs[i + limb_shift];
        for (uint32_t i = a->used - limb_shift; i < a->used; ++i)
            a->limbs[i] = 0u;
        a->used -= limb_shift;
    }
    if (bit_shift > 0u) {
        for (uint32_t i = 0u; i < a->used; ++i) {
            a->limbs[i] >>= bit_shift;
            if (i + 1u < a->used)
                a->limbs[i] |= a->limbs[i + 1u] << (64u - bit_shift);
        }
    }
    bn_normalize(a);
}

/** Left-shift by n bits. */
static void bn_shl(BigNum* a, unsigned n) {
    if (n == 0u) return;
    unsigned limb_shift = n / 64u;
    unsigned bit_shift = n % 64u;
    if (limb_shift > 0u) {
        for (int i = (int)a->used - 1 + (int)limb_shift; i >= (int)limb_shift; --i) {
            if ((uint32_t)i < BN_MAX_LIMBS) a->limbs[i] = a->limbs[i - limb_shift];
        }
        for (uint32_t i = 0u; i < limb_shift && i < BN_MAX_LIMBS; ++i)
            a->limbs[i] = 0u;
        a->used += limb_shift;
        if (a->used > BN_MAX_LIMBS) a->used = BN_MAX_LIMBS;
    }
    if (bit_shift > 0u) {
        uint64_t carry = 0u;
        for (uint32_t i = 0u; i < a->used; ++i) {
            uint64_t new_carry = a->limbs[i] >> (64u - bit_shift);
            a->limbs[i] = (a->limbs[i] << bit_shift) | carry;
            carry = new_carry;
        }
        if (carry && a->used < BN_MAX_LIMBS) {
            a->limbs[a->used] = carry;
            ++a->used;
        }
    }
    bn_normalize(a);
}

/* ══════════════════════════════════════════════════════════════════════
 * SMALL-DIVISOR OPERATIONS
 * ══════════════════════════════════════════════════════════════════════ */

/** Return a % m where m is uint64. */
static uint64_t bn_mod_u64(const BigNum* a, uint64_t m) {
    if (m == 0u) return 0u;
    uint64_t rem = 0u;
    /* (rem * 2^64 + limb) mod m, scanning from most-significant limb down */
    uint64_t pow2_64_mod = (UINT64_MAX % m + 1u) % m;
    for (int i = (int)a->used - 1; i >= 0; --i) {
        uint64_t hi_mod = mulmod_u64(rem, pow2_64_mod, m);
        rem = (hi_mod + a->limbs[i] % m) % m;
    }
    return rem;
}

/** a = a / m (in-place), returns remainder. m is uint64. */
static uint64_t bn_div_u64(BigNum* a, uint64_t m) {
    if (m == 0u) return 0u;
    uint64_t rem = 0u;
    for (int i = (int)a->used - 1; i >= 0; --i) {
        /* Compute (rem * 2^64 + a->limbs[i]) / m */
        U128 dividend = u128_make(rem, a->limbs[i]);
        /* Use u128_div for the quotient */
        U128 divisor = u128_from_u64(m);
        U128 q = u128_div(dividend, divisor);
        U128 r = u128_mod(dividend, divisor);
        a->limbs[i] = q.lo;
        rem = r.lo;
    }
    bn_normalize(a);
    return rem;
}

/* ══════════════════════════════════════════════════════════════════════
 * EXACT DIVISION
 * ══════════════════════════════════════════════════════════════════════ */

/** q = a / b where b divides a exactly. Uses schoolbook long division. */
static void bn_divexact(BigNum* q, const BigNum* a, const BigNum* b) {
    /* Simple approach: use repeated subtraction with shifting.
     * For exact division we can also use Newton's method or
     * schoolbook — here we use schoolbook for correctness. */
    BigNum rem; bn_copy(&rem, a);
    bn_zero(q);

    if (bn_is_zero(b)) return;
    if (bn_cmp(a, b) < 0) return; /* a < b → quotient 0 */

    unsigned a_bits = bn_bitlen(a);
    unsigned b_bits = bn_bitlen(b);
    if (b_bits > a_bits) return;

    int shift = (int)a_bits - (int)b_bits;
    BigNum divisor; bn_copy(&divisor, b);
    bn_shl(&divisor, (unsigned)shift);

    for (int i = shift; i >= 0; --i) {
        if (bn_cmp(&rem, &divisor) >= 0) {
            bn_sub(&rem, &divisor);
            /* Set bit i in quotient */
            q->limbs[i / 64] |= (uint64_t)1u << (i % 64);
            if ((uint32_t)(i / 64 + 1) > q->used) q->used = (uint32_t)(i / 64 + 1);
        }
        bn_shr1(&divisor);
    }
    bn_normalize(q);
}

/* ══════════════════════════════════════════════════════════════════════
 * STRING I/O
 * ══════════════════════════════════════════════════════════════════════ */

/** Parse decimal string to BigNum. Returns false on overflow. */
static bool bn_from_str(BigNum* a, const char* s) {
    bn_zero(a);
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '\0') return false;
    while (*s >= '0' && *s <= '9') {
        /* a = a * 10 + digit */
        /* Multiply by 10: a*8 + a*2 via shifts */
        BigNum a8; bn_copy(&a8, a); bn_shl(&a8, 3u);
        BigNum a2; bn_copy(&a2, a); bn_shl(&a2, 1u);
        bn_copy(a, &a8);
        bn_add(a, &a2);
        bn_add_u64(a, (uint64_t)(*s - '0'));
        /* Overflow check */
        if (a->used > BN_MAX_LIMBS) {
            bn_zero(a);
            return false;
        }
        ++s;
    }
    bn_normalize(a);
    return true;
}

/** BigNum to decimal string. buf must be large enough (~310 chars for 1024 bits). */
static void bn_to_str(const BigNum* a, char* buf, size_t buf_len) {
    if (buf_len == 0u) return;
    if (bn_is_zero(a)) {
        if (buf_len >= 2u) { buf[0] = '0'; buf[1] = '\0'; }
        return;
    }
    char tmp[512];
    size_t pos = 0u;
    BigNum work; bn_copy(&work, a);
    while (!bn_is_zero(&work) && pos + 1u < sizeof(tmp)) {
        uint64_t digit = bn_div_u64(&work, 10u);
        tmp[pos++] = (char)('0' + digit);
    }
    size_t out = 0u;
    while (pos > 0u && out + 1u < buf_len) {
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
}

/* ══════════════════════════════════════════════════════════════════════
 * MULTIPLICATION (Schoolbook + Karatsuba)
 * ══════════════════════════════════════════════════════════════════════ */

#define KARATSUBA_THRESHOLD 4  /* use Karatsuba when both operands have ≥4 limbs */

/** Schoolbook multiply on raw limb arrays: out[0..an+bn-1] = a[0..an-1] × b[0..bn-1].
 *  Caller must ensure out is zeroed and has room for an+bn limbs. */
static void limb_mul_schoolbook(uint64_t* out, const uint64_t* a, uint32_t an,
                                  const uint64_t* b, uint32_t bn) {
    for (uint32_t i = 0u; i < an; ++i) {
        uint64_t carry = 0u;
        for (uint32_t j = 0u; j < bn; ++j) {
            uint32_t k = i + j;
            U128 prod = u128_mul64(a[i], b[j]);
            uint64_t lo = out[k] + prod.lo;
            uint64_t c1 = (lo < out[k]) ? 1u : 0u;
            lo += carry;
            c1 += (lo < carry) ? 1u : 0u;
            out[k] = lo;
            carry = prod.hi + c1;
        }
        for (uint32_t k = i + bn; carry; ++k) {
            uint64_t sum = out[k] + carry;
            carry = (sum < out[k]) ? 1u : 0u;
            out[k] = sum;
        }
    }
}

/** Add src[0..n-1] to dst[0..], propagating carry. */
static void limb_add(uint64_t* dst, const uint64_t* src, uint32_t n) {
    uint64_t carry = 0u;
    for (uint32_t i = 0u; i < n; ++i) {
        uint64_t s = dst[i] + src[i] + carry;
        carry = (dst[i] > s || (carry && src[i] == UINT64_MAX)) ? 1u : 0u;
        /* More reliable carry detection */
        uint64_t t1 = dst[i] + src[i];
        uint64_t c1 = (t1 < dst[i]) ? 1u : 0u;
        uint64_t t2 = t1 + carry;
        uint64_t c2 = (t2 < t1) ? 1u : 0u;
        dst[i] = t2;
        carry = c1 + c2;
    }
    for (uint32_t i = n; carry; ++i) {
        uint64_t s = dst[i] + carry;
        carry = (s < dst[i]) ? 1u : 0u;
        dst[i] = s;
    }
}

/** Subtract src[0..n-1] from dst[0..], propagating borrow. dst must be >= src. */
static void limb_sub(uint64_t* dst, const uint64_t* src, uint32_t n) {
    uint64_t borrow = 0u;
    for (uint32_t i = 0u; i < n; ++i) {
        uint64_t s = dst[i] - src[i] - borrow;
        borrow = (dst[i] < src[i] + borrow || (borrow && src[i] == UINT64_MAX)) ? 1u : 0u;
        /* More reliable borrow detection */
        uint64_t t1 = dst[i] - src[i];
        uint64_t b1 = (dst[i] < src[i]) ? 1u : 0u;
        uint64_t t2 = t1 - borrow;
        uint64_t b2 = (t1 < borrow) ? 1u : 0u;
        dst[i] = t2;
        borrow = b1 + b2;
    }
    for (uint32_t i = n; borrow; ++i) {
        uint64_t s = dst[i] - borrow;
        borrow = (dst[i] < borrow) ? 1u : 0u;
        dst[i] = s;
    }
}

/** Karatsuba multiply on raw limb arrays.
 *  out must have room for 2*n limbs and be zeroed.
 *  tmp must have room for 6*n limbs (scratch space).
 *  a and b are both padded/truncated to n limbs. */
static void limb_mul_karatsuba(uint64_t* out, const uint64_t* a, const uint64_t* b,
                                 uint32_t n, uint64_t* tmp) {
    if (n < KARATSUBA_THRESHOLD) {
        limb_mul_schoolbook(out, a, n, b, n);
        return;
    }

    uint32_t half = n / 2u;
    uint32_t upper = n - half;

    /* a = a_lo + a_hi * B^half,  b = b_lo + b_hi * B^half */
    const uint64_t* a_lo = a;
    const uint64_t* a_hi = a + half;
    const uint64_t* b_lo = b;
    const uint64_t* b_hi = b + half;

    /* z0 = a_lo * b_lo */
    uint64_t* z0 = tmp;                    /* 2*half limbs */
    memset(z0, 0, 2u * half * sizeof(uint64_t));
    /* z2 = a_hi * b_hi */
    uint64_t* z2 = tmp + 2u * half;        /* 2*upper limbs */
    memset(z2, 0, 2u * upper * sizeof(uint64_t));
    /* scratch for (a_lo+a_hi) and (b_lo+b_hi) and their product */
    uint64_t* sa = tmp + 2u * half + 2u * upper;  /* upper+1 limbs */
    uint64_t* sb = sa + upper + 1u;                /* upper+1 limbs */
    uint64_t* z1 = sb + upper + 1u;                /* 2*(upper+1) limbs */

    /* Recursive z0 = a_lo * b_lo (schoolbook for small) */
    if (half < KARATSUBA_THRESHOLD) {
        limb_mul_schoolbook(z0, a_lo, half, b_lo, half);
    } else {
        uint64_t* sub_tmp = z1 + 2u * (upper + 1u); /* need more scratch */
        limb_mul_karatsuba(z0, a_lo, b_lo, half, sub_tmp);
    }

    /* Recursive z2 = a_hi * b_hi */
    if (upper < KARATSUBA_THRESHOLD) {
        limb_mul_schoolbook(z2, a_hi, upper, b_hi, upper);
    } else {
        uint64_t* sub_tmp = z1 + 2u * (upper + 1u);
        limb_mul_karatsuba(z2, a_hi, b_hi, upper, sub_tmp);
    }

    /* sa = a_lo + a_hi, sb = b_lo + b_hi */
    memset(sa, 0, (upper + 1u) * sizeof(uint64_t));
    memset(sb, 0, (upper + 1u) * sizeof(uint64_t));
    memcpy(sa, a_lo, half * sizeof(uint64_t));
    limb_add(sa, a_hi, upper);
    memcpy(sb, b_lo, half * sizeof(uint64_t));
    limb_add(sb, b_hi, upper);

    /* z1 = (a_lo + a_hi) * (b_lo + b_hi) */
    uint32_t s_len = upper + 1u;
    memset(z1, 0, 2u * s_len * sizeof(uint64_t));
    if (s_len < KARATSUBA_THRESHOLD) {
        limb_mul_schoolbook(z1, sa, s_len, sb, s_len);
    } else {
        uint64_t* sub_tmp = z1 + 2u * s_len;
        limb_mul_karatsuba(z1, sa, sb, s_len, sub_tmp);
    }

    /* z1 = z1 - z0 - z2 */
    limb_sub(z1, z0, 2u * half);
    limb_sub(z1, z2, 2u * upper);

    /* out = z0 + z1 * B^half + z2 * B^(2*half) */
    memcpy(out, z0, 2u * half * sizeof(uint64_t));
    limb_add(out + half, z1, 2u * s_len);
    limb_add(out + 2u * half, z2, 2u * upper);
}

/** Multiply a × b → result. Uses Karatsuba for ≥4 limbs, schoolbook otherwise. */
static void bn_mul(BigNum* result, const BigNum* a, const BigNum* b) {
    uint32_t result_used = a->used + b->used;
    if (result_used > BN_MAX_LIMBS * 2) result_used = BN_MAX_LIMBS * 2;

    uint64_t tmp[BN_MAX_LIMBS * 2];
    memset(tmp, 0, result_used * sizeof(uint64_t));

    if (a->used >= KARATSUBA_THRESHOLD && b->used >= KARATSUBA_THRESHOLD) {
        /* Pad to same size for Karatsuba */
        uint32_t n = a->used > b->used ? a->used : b->used;
        uint64_t a_pad[BN_MAX_LIMBS], b_pad[BN_MAX_LIMBS];
        memset(a_pad, 0, n * sizeof(uint64_t));
        memset(b_pad, 0, n * sizeof(uint64_t));
        memcpy(a_pad, a->limbs, a->used * sizeof(uint64_t));
        memcpy(b_pad, b->limbs, b->used * sizeof(uint64_t));

        uint64_t scratch[BN_MAX_LIMBS * 6];
        memset(scratch, 0, sizeof(scratch));
        limb_mul_karatsuba(tmp, a_pad, b_pad, n, scratch);
    } else {
        limb_mul_schoolbook(tmp, a->limbs, a->used, b->limbs, b->used);
    }

    /* Overflow detection: check if significant limbs were lost */
    if (result_used > BN_MAX_LIMBS) {
        bool lost = false;
        for (uint32_t i = BN_MAX_LIMBS; i < result_used; ++i) if (tmp[i]) { lost = true; break; }
        if (lost) fprintf(stderr, "BN OVERFLOW: bn_mul product %u limbs truncated to %u (%u bits lost)\n",
            result_used, (uint32_t)BN_MAX_LIMBS, (result_used - BN_MAX_LIMBS)*64);
    }
    memset(result, 0, sizeof(*result));
    uint32_t copy_limbs = result_used < BN_MAX_LIMBS ? result_used : BN_MAX_LIMBS;
    memcpy(result->limbs, tmp, copy_limbs * sizeof(uint64_t));
    result->used = copy_limbs;
    bn_normalize(result);
}

/** Convert BigNum to double (approximate, for log-sieve initialization). */
static double bn_to_double(const BigNum* a) {
    if (bn_is_zero(a)) return 0.0;
    double result = 0.0;
    double base = 1.0;
    for (uint32_t i = 0u; i < a->used; ++i) {
        result += (double)a->limbs[i] * base;
        base *= 18446744073709551616.0; /* 2^64 */
    }
    return result;
}

/** Integer square root floor for a BigNum. */
static void bn_isqrt_floor(BigNum* result, const BigNum* a) {
    if (bn_is_zero(a) || bn_is_one(a)) {
        bn_copy(result, a);
        return;
    }
    if (bn_fits_u64(a)) {
        bn_from_u64(result, (uint64_t)isqrt_u64(bn_to_u64(a)));
        return;
    }
    if (bn_fits_u128(a)) {
        bn_from_u64(result, u128_isqrt_floor(bn_to_u128(a)));
        return;
    }

    unsigned hi_shift = (bn_bitlen(a) + 1u) / 2u;
    BigNum lo, hi, best;
    bn_zero(&lo);
    bn_from_u64(&hi, 1u);
    bn_shl(&hi, hi_shift);
    bn_sub_u64(&hi, 1u);
    bn_zero(&best);

    while (bn_cmp(&lo, &hi) <= 0) {
        BigNum mid;
        bn_copy(&mid, &lo);
        bn_add(&mid, &hi);
        bn_shr1(&mid);

        BigNum sq;
        bn_mul(&sq, &mid, &mid);
        if (bn_cmp(&sq, a) <= 0) {
            bn_copy(&best, &mid);
            bn_copy(&lo, &mid);
            bn_add_u64(&lo, 1u);
        }
        else {
            if (bn_is_zero(&mid)) break;
            bn_copy(&hi, &mid);
            bn_sub_u64(&hi, 1u);
        }
    }

    bn_copy(result, &best);
}

/** Integer square root ceiling for a BigNum. */
static void bn_isqrt_ceil(BigNum* result, const BigNum* a) {
    bn_isqrt_floor(result, a);
    BigNum sq;
    bn_mul(&sq, result, result);
    if (bn_cmp(&sq, a) < 0) {
        bn_add_u64(result, 1u);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * MONTGOMERY REDUCTION
 * ══════════════════════════════════════════════════════════════════════ */

/** Compute -n^(-1) mod 2^64 using Newton's method. n must be odd. */
static uint64_t bn_mont_inv(uint64_t n0) {
    /* Newton's method: x = x * (2 - n * x) mod 2^64
     * Start with x = 1 (correct mod 2^1 for odd n) */
    uint64_t x = 1u;
    for (int i = 0; i < 6; ++i) { /* 6 iterations → 64-bit precision */
        x = x * (2u - n0 * x);
    }
    return (uint64_t)(-(int64_t)x); /* negate to get -n^(-1) */
}

/** Set up Montgomery context for modulus n. n must be odd. */
static bool bn_montgomery_setup(MontCtx* ctx, const BigNum* n) {
    if (bn_is_even(n)) return false;
    bn_copy(&ctx->n, n);
    ctx->r_limbs = n->used;
    ctx->n_inv = bn_mont_inv(n->limbs[0]);

    /* Compute R^2 mod n where R = 2^(64 * r_limbs).
     * Start with R mod n (= R - n * floor(R/n)), then square it mod n.
     * Simple approach: start with 1, left-shift by 1 bit, reduce mod n, repeat 2*64*r_limbs times. */
    bn_from_u64(&ctx->r2, 1u);
    unsigned total_bits = 2u * 64u * ctx->r_limbs;
    for (unsigned i = 0u; i < total_bits; ++i) {
        bn_shl1(&ctx->r2);
        if (bn_cmp(&ctx->r2, n) >= 0)
            bn_sub(&ctx->r2, n);
    }
    return true;
}

/**
 * Montgomery reduction (REDC).
 * Input: T (product, up to 2*r_limbs limbs stored in t_limbs[])
 * Output: T * R^(-1) mod n, stored in result.
 */
static void bn_montgomery_reduce(BigNum* result, const uint64_t* t_limbs, uint32_t t_used,
    const MontCtx* ctx) {
    /* Work with a mutable copy of T, extended to 2*r_limbs+1 */
    uint32_t n_limbs = ctx->r_limbs;
    uint32_t work_size = 2u * n_limbs + 1u;
    uint64_t work[BN_MAX_LIMBS * 2 + 1];
    memset(work, 0, work_size * sizeof(uint64_t));
    for (uint32_t i = 0u; i < t_used && i < work_size; ++i)
        work[i] = t_limbs[i];

    /* REDC loop: for each limb i of T */
    for (uint32_t i = 0u; i < n_limbs; ++i) {
        uint64_t m = work[i] * ctx->n_inv;
        /* work += m * n * 2^(64*i) */
        uint64_t carry = 0u;
        for (uint32_t j = 0u; j < n_limbs; ++j) {
            uint32_t k = i + j;
            if (k >= work_size) break;
            U128 prod = u128_mul64(m, ctx->n.limbs[j]);
            /* Three-term addition: work[k] + prod.lo + carry → (new work[k], new carry) */
            uint64_t s1 = work[k] + prod.lo;
            uint64_t c1 = (s1 < work[k]) ? 1u : 0u;
            uint64_t s2 = s1 + carry;
            uint64_t c2 = (s2 < s1) ? 1u : 0u;
            work[k] = s2;
            carry = prod.hi + c1 + c2;
        }
        /* Propagate carry */
        for (uint32_t k = i + n_limbs; carry && k < work_size; ++k) {
            uint64_t s = work[k] + carry;
            carry = (s < work[k]) ? 1u : 0u;
            work[k] = s;
        }
    }

    /* Result = work >> (64 * n_limbs) = work[n_limbs .. 2*n_limbs] */
    memset(result, 0, sizeof(*result));
    for (uint32_t i = 0u; i < n_limbs + 1u && i < BN_MAX_LIMBS; ++i) {
        result->limbs[i] = (n_limbs + i < work_size) ? work[n_limbs + i] : 0u;
    }
    result->used = n_limbs + 1u;
    if (result->used > BN_MAX_LIMBS) result->used = BN_MAX_LIMBS;
    bn_normalize(result);

    /* Final subtraction if result >= n */
    if (bn_cmp(result, &ctx->n) >= 0)
        bn_sub(result, &ctx->n);
}

/** Convert a to Montgomery form: aR mod n. */
static void bn_to_mont(BigNum* result, const BigNum* a, const MontCtx* ctx) {
    /* aR mod n = REDC(a * R^2) */
    uint64_t prod[BN_MAX_LIMBS * 2];
    uint32_t to_mont_size = a->used + ctx->r2.used + 1u;
    if (to_mont_size > BN_MAX_LIMBS * 2) to_mont_size = BN_MAX_LIMBS * 2;
    memset(prod, 0, to_mont_size * sizeof(uint64_t));
    /* Schoolbook multiply a × R^2 into prod */
    for (uint32_t i = 0u; i < a->used; ++i) {
        uint64_t carry = 0u;
        for (uint32_t j = 0u; j < ctx->r2.used; ++j) {
            uint32_t k = i + j;
            if (k >= BN_MAX_LIMBS * 2) break;
            U128 p = u128_mul64(a->limbs[i], ctx->r2.limbs[j]);
            uint64_t s1 = prod[k] + p.lo;
            uint64_t c1 = (s1 < prod[k]) ? 1u : 0u;
            uint64_t s2 = s1 + carry;
            uint64_t c2 = (s2 < s1) ? 1u : 0u;
            prod[k] = s2;
            carry = p.hi + c1 + c2;
        }
        for (uint32_t k = i + ctx->r2.used; carry && k < BN_MAX_LIMBS * 2; ++k) {
            uint64_t s = prod[k] + carry;
            carry = (s < prod[k]) ? 1u : 0u;
            prod[k] = s;
        }
    }
    uint32_t prod_used = a->used + ctx->r2.used;
    if (prod_used > BN_MAX_LIMBS * 2) prod_used = BN_MAX_LIMBS * 2;
    bn_montgomery_reduce(result, prod, prod_used, ctx);
}

/** Convert from Montgomery form: REDC(aR) = a. */
static void bn_from_mont(BigNum* result, const BigNum* a_mont, const MontCtx* ctx) {
    /* REDC(aR) with the input padded to 2*r_limbs */
    uint64_t padded[BN_MAX_LIMBS * 2];
    uint32_t pad_size = 2u * ctx->r_limbs + 1u;
    if (pad_size > BN_MAX_LIMBS * 2) pad_size = BN_MAX_LIMBS * 2;
    memset(padded, 0, pad_size * sizeof(uint64_t));
    for (uint32_t i = 0u; i < a_mont->used; ++i)
        padded[i] = a_mont->limbs[i];
    bn_montgomery_reduce(result, padded, a_mont->used, ctx);
}

/** Montgomery multiplication: result = REDC(a_mont * b_mont). */
static void bn_mont_mul(BigNum* result, const BigNum* a, const BigNum* b, const MontCtx* ctx) {
    uint64_t prod[BN_MAX_LIMBS * 2];
    uint32_t prod_size = a->used + b->used + 1u;
    if (prod_size > BN_MAX_LIMBS * 2) prod_size = BN_MAX_LIMBS * 2;
    memset(prod, 0, prod_size * sizeof(uint64_t));

    if (a->used >= KARATSUBA_THRESHOLD && b->used >= KARATSUBA_THRESHOLD) {
        uint32_t n = a->used > b->used ? a->used : b->used;
        uint64_t a_pad[BN_MAX_LIMBS], b_pad[BN_MAX_LIMBS];
        memset(a_pad, 0, n * sizeof(uint64_t));
        memset(b_pad, 0, n * sizeof(uint64_t));
        memcpy(a_pad, a->limbs, a->used * sizeof(uint64_t));
        memcpy(b_pad, b->limbs, b->used * sizeof(uint64_t));
        uint64_t scratch[BN_MAX_LIMBS * 6];
        memset(scratch, 0, sizeof(scratch));
        limb_mul_karatsuba(prod, a_pad, b_pad, n, scratch);
    } else {
        limb_mul_schoolbook(prod, a->limbs, a->used, b->limbs, b->used);
    }

    uint32_t prod_used = a->used + b->used;
    if (prod_used > BN_MAX_LIMBS * 2) prod_used = BN_MAX_LIMBS * 2;
    bn_montgomery_reduce(result, prod, prod_used, ctx);
}

/**
 * Montgomery modular exponentiation: result = base^exp mod n.
 * exp is uint64 (prime powers from DB are always uint64).
 * n must be odd; caller must set up MontCtx.
 */
static void bn_mont_powmod(BigNum* result, const BigNum* base, uint64_t exp,
    const MontCtx* ctx) {
    /* Convert base to Montgomery form */
    BigNum base_mont;
    bn_to_mont(&base_mont, base, ctx);

    /* result = R mod n (Montgomery form of 1) */
    BigNum one; bn_from_u64(&one, 1u);
    BigNum result_mont;
    bn_to_mont(&result_mont, &one, ctx);

    /* Binary exponentiation */
    while (exp > 0u) {
        if (exp & 1u) {
            bn_mont_mul(&result_mont, &result_mont, &base_mont, ctx);
        }
        bn_mont_mul(&base_mont, &base_mont, &base_mont, ctx);
        exp >>= 1u;
    }

    /* Convert back from Montgomery form */
    bn_from_mont(result, &result_mont, ctx);
}

/* ══════════════════════════════════════════════════════════════════════
 * BINARY GCD (Stein's Algorithm)
 * ══════════════════════════════════════════════════════════════════════ */

/** Compute gcd(a, b) using binary GCD. No division needed. */
static void bn_gcd(BigNum* result, const BigNum* a_in, const BigNum* b_in) {
    BigNum a, b;
    bn_copy(&a, a_in);
    bn_copy(&b, b_in);

    if (bn_is_zero(&a)) { bn_copy(result, &b); return; }
    if (bn_is_zero(&b)) { bn_copy(result, &a); return; }

    /* Find common factor of 2 */
    unsigned a_tz = bn_ctz(&a);
    unsigned b_tz = bn_ctz(&b);
    unsigned common_tz = (a_tz < b_tz) ? a_tz : b_tz;
    bn_shr(&a, a_tz);
    bn_shr(&b, b_tz);

    /* a is now odd */
    while (!bn_is_zero(&b)) {
        /* Remove factors of 2 from b */
        unsigned tz = bn_ctz(&b);
        if (tz > 0u) bn_shr(&b, tz);
        /* Now both a and b are odd. Subtract smaller from larger. */
        if (bn_cmp(&a, &b) > 0) bn_swap(&a, &b);
        bn_sub(&b, &a);
    }

    bn_copy(result, &a);
    bn_shl(result, common_tz);
}

/* ══════════════════════════════════════════════════════════════════════
 * BIGNUM RANDOM PRIME GENERATION
 *
 * Generates random probable primes of a given bit length using a
 * simple xorshift64 PRNG + deterministic Miller-Rabin (for ≤64-bit)
 * or probabilistic Miller-Rabin with strong witnesses (for >64-bit).
 * Does NOT require the CPSS database — pure computation.
 * ══════════════════════════════════════════════════════════════════════ */

/** Fill a BigNum with random bits from xorshift64 PRNG. */
static void bn_random_bits(BigNum* a, unsigned bits, uint64_t* rng_state) {
    bn_zero(a);
    if (bits == 0u) return;
    uint32_t limbs_needed = (bits + 63u) / 64u;
    if (limbs_needed > BN_MAX_LIMBS) limbs_needed = BN_MAX_LIMBS;
    a->used = limbs_needed;
    for (uint32_t i = 0u; i < limbs_needed; ++i) {
        /* xorshift64 */
        uint64_t x = *rng_state;
        x ^= x << 13u;
        x ^= x >> 7u;
        x ^= x << 17u;
        *rng_state = x;
        a->limbs[i] = x;
    }
    /* Mask top limb to exact bit count */
    unsigned top_bits = ((bits - 1u) % 64u) + 1u;
    if (top_bits < 64u) {
        a->limbs[limbs_needed - 1u] &= ((uint64_t)1u << top_bits) - 1u;
    }
    /* Set MSB to ensure exactly 'bits' bit length */
    unsigned msb_limb = (bits - 1u) / 64u;
    unsigned msb_bit = (bits - 1u) % 64u;
    a->limbs[msb_limb] |= (uint64_t)1u << msb_bit;
    /* Set LSB to make it odd */
    a->limbs[0] |= 1u;
    bn_normalize(a);
}

/** BigNum Miller-Rabin primality test with k witnesses. Uses Montgomery. */
static bool bn_is_probable_prime(const BigNum* n, unsigned rounds) {
    if (bn_is_zero(n) || bn_is_one(n)) return false;
    if (bn_fits_u64(n)) return miller_rabin_u64(bn_to_u64(n));
    if (bn_fits_u128(n)) return u128_is_prime(bn_to_u128(n));
    if (bn_is_even(n)) return false;

    /* Check small prime divisibility */
    static const uint64_t small_primes[] = { 3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97 };
    for (size_t i = 0u; i < sizeof(small_primes)/sizeof(small_primes[0]); ++i) {
        if (bn_mod_u64(n, small_primes[i]) == 0u) {
            /* n is divisible by this small prime — is n equal to it? */
            BigNum sp; bn_from_u64(&sp, small_primes[i]);
            return bn_cmp(n, &sp) == 0;
        }
    }

    /* Montgomery-based Miller-Rabin */
    MontCtx ctx;
    if (!bn_montgomery_setup(&ctx, n)) return false;

    BigNum n_minus_1;
    bn_copy(&n_minus_1, n);
    bn_sub_u64(&n_minus_1, 1u);

    /* Write n-1 = d * 2^r */
    BigNum d;
    bn_copy(&d, &n_minus_1);
    unsigned r = bn_ctz(&d);
    bn_shr(&d, r);

    /* We need powmod with BigNum exponent d — use peasant mulmod for this since
     * Montgomery powmod only takes uint64 exponent. Build a dedicated loop. */
    static const uint64_t witnesses[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
    unsigned num_witnesses = rounds < (unsigned)(sizeof(witnesses)/sizeof(witnesses[0]))
        ? rounds : (unsigned)(sizeof(witnesses)/sizeof(witnesses[0]));

    for (unsigned w = 0u; w < num_witnesses; ++w) {
        BigNum a; bn_from_u64(&a, witnesses[w]);

        /* Compute a^d mod n using Montgomery with BigNum exponent */
        BigNum a_mont;
        bn_to_mont(&a_mont, &a, &ctx);
        BigNum one; bn_from_u64(&one, 1u);
        BigNum result_mont;
        bn_to_mont(&result_mont, &one, &ctx);

        /* Binary exponentiation scanning d from MSB down */
        unsigned d_bits = bn_bitlen(&d);
        for (int bi = (int)d_bits - 1; bi >= 0; --bi) {
            bn_mont_mul(&result_mont, &result_mont, &result_mont, &ctx);
            uint32_t limb_idx = (uint32_t)bi / 64u;
            uint32_t bit_idx = (uint32_t)bi % 64u;
            if (limb_idx < d.used && (d.limbs[limb_idx] >> bit_idx) & 1u) {
                bn_mont_mul(&result_mont, &result_mont, &a_mont, &ctx);
            }
        }

        BigNum x;
        bn_from_mont(&x, &result_mont, &ctx);

        /* Check x == 1 or x == n-1 */
        BigNum one_plain; bn_from_u64(&one_plain, 1u);
        if (bn_cmp(&x, &one_plain) == 0 || bn_cmp(&x, &n_minus_1) == 0) continue;

        bool found = false;
        for (unsigned i = 1u; i < r; ++i) {
            /* x = x^2 mod n via Montgomery */
            bn_to_mont(&a_mont, &x, &ctx);
            bn_mont_mul(&result_mont, &a_mont, &a_mont, &ctx);
            bn_from_mont(&x, &result_mont, &ctx);
            if (bn_cmp(&x, &n_minus_1) == 0) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

/**
 * Generate a random probable prime of exactly 'bits' bit length.
 * Uses the given PRNG state. Increments by 2 until a probable prime is found.
 * Returns the prime in *out.
 */
static void bn_random_prime(BigNum* out, unsigned bits, uint64_t* rng_state) {
    bn_random_bits(out, bits, rng_state);
    /* Ensure odd */
    out->limbs[0] |= 1u;
    /* Ensure MSB set */
    unsigned msb_limb = (bits - 1u) / 64u;
    unsigned msb_bit = (bits - 1u) % 64u;
    out->limbs[msb_limb] |= (uint64_t)1u << msb_bit;
    bn_normalize(out);

    /* Increment by 2 until prime */
    while (!bn_is_probable_prime(out, 12u)) {
        bn_add_u64(out, 2u);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * PEASANT MULMOD FALLBACK (for testing correctness)
 * ══════════════════════════════════════════════════════════════════════ */

/** (a * b) % m using Russian peasant multiplication. Slow but simple. */
static void bn_mulmod_peasant(BigNum* result, const BigNum* a_in, const BigNum* b_in,
    const BigNum* m) {
    BigNum a, b;
    /* a = a_in mod m */
    bn_divexact(&a, a_in, m); /* abuse: we need mod, not divexact. Use a different approach. */
    /* Actually, for correctness fallback, just do the peasant multiply directly.
     * Assume inputs are already < m. */
    bn_copy(&a, a_in);
    bn_copy(&b, b_in);
    bn_zero(result);

    while (!bn_is_zero(&b)) {
        if (bn_is_odd(&b)) {
            bn_add(result, &a);
            if (bn_cmp(result, m) >= 0)
                bn_sub(result, m);
        }
        bn_shl1(&a);
        if (bn_cmp(&a, m) >= 0)
            bn_sub(&a, m);
        bn_shr1(&b);
    }
}
