/**
 * SemiPrimeObservationSpace.c - Observable-feature space functions.
 * Part of the CPSS SemiPrime Spaces module (N-centric inference).
 *
 * This is the engine's routing dashboard: what it can actually see
 * before factorisation succeeds. Not the true factor space — it's
 * the space of cheap signals available before a win.
 *
 * Axes: x = evidence of small factor, y = evidence of near-square structure.
 *
 * Depends on: cpss_arith.c (is_perfect_square_u64, isqrt_u64, miller_rabin_u64),
 *             cpss_query.c (cpss_support_coverage)
 */

/** Feature vector: cheap observable signals for an input N. */
typedef struct {
    uint64_t N;
    double near_square_score;      /* 0.0 = far from square, 1.0 = perfect square */
    double small_factor_evidence;  /* 0.0 = no evidence, 1.0 = definitely has small factor */
    bool divisible_by_wheel_prime; /* N % p == 0 for some p in {2,3,5,7,11,13}? */
    bool is_perfect_square;        /* N is a perfect square? */
    bool cpss_coverage_complete;   /* CPSS covers up to sqrt(N)? */
    double cpss_coverage_ratio;    /* fraction of [2, sqrt(N)] covered by DB */
    bool is_prime;                 /* N is prime (via Miller-Rabin)? */
} ObservableFeatureVec;

/**
 * Score how close N is to a perfect square.
 * Returns 1.0 if N is a perfect square, approaches 0.0 as N moves away.
 * score = 1.0 / (1.0 + |N - floor(sqrt(N))^2| / sqrt(N))
 */
static double observable_near_square_score(uint64_t N) {
    if (N <= 1u) return 1.0;
    uint64_t root = (uint64_t)isqrt_u64(N);
    uint64_t sq = root * root;
    uint64_t gap = (N >= sq) ? (N - sq) : (sq - N);
    double sqrt_N = sqrt((double)N);
    if (sqrt_N <= 0.0) return 0.0;
    return 1.0 / (1.0 + (double)gap / sqrt_N);
}

/**
 * Quick small-factor evidence check.
 * Returns 1.0 if N has a wheel-prime factor, 0.0 otherwise.
 * This is the cheapest possible signal (just modular arithmetic).
 */
static double observable_small_factor_evidence(uint64_t N) {
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (N % WHEEL_PRIMES[i] == 0u && N > WHEEL_PRIMES[i]) {
            return 1.0;
        }
    }
    return 0.0;
}

/**
 * Compute the full observable feature vector for N.
 * This gathers all cheap signals the engine can use for routing.
 */
static void observable_compute(CPSSStream* s, uint64_t N, ObservableFeatureVec* vec) {
    memset(vec, 0, sizeof(*vec));
    vec->N = N;

    if (N <= 1u) return;

    /* Near-square score */
    vec->near_square_score = observable_near_square_score(N);
    vec->is_perfect_square = is_perfect_square_u64(N);

    /* Small-factor evidence */
    vec->divisible_by_wheel_prime = false;
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (N % WHEEL_PRIMES[i] == 0u && N > WHEEL_PRIMES[i]) {
            vec->divisible_by_wheel_prime = true;
            break;
        }
    }
    vec->small_factor_evidence = vec->divisible_by_wheel_prime ? 1.0 : 0.0;

    /* Primality (cheap Miller-Rabin) */
    vec->is_prime = miller_rabin_u64(N);

    /* CPSS coverage */
    SupportCoverage cov = cpss_support_coverage(s, N);
    vec->cpss_coverage_complete = cov.db_complete;
    vec->cpss_coverage_ratio = cov.db_coverage;
}

/** BigNum near-square score: 1/(1 + gap/sqrt(N)) where gap = ceil(sqrt(N))^2 - N. */
static double observable_near_square_score_bn(const BigNum* N) {
    if (bn_is_zero(N) || bn_is_one(N)) return 1.0;
    BigNum s;
    bn_isqrt_ceil(&s, N);
    BigNum s2;
    bn_mul(&s2, &s, &s);
    BigNum gap;
    bn_copy(&gap, &s2);
    bn_sub(&gap, N);
    /* Approximate sqrt(N) via bit length: 2^(bitlen/2) */
    double log2_N = (double)bn_bitlen(N);
    double sqrt_N_approx = pow(2.0, log2_N / 2.0);
    if (sqrt_N_approx <= 0.0) return 0.0;
    /* gap as double — may lose precision for huge gaps, but that's fine for a score */
    double gap_d = bn_fits_u64(&gap) ? (double)bn_to_u64(&gap) : pow(2.0, (double)bn_bitlen(&gap));
    return 1.0 / (1.0 + gap_d / sqrt_N_approx);
}

/** BigNum observable feature vector. */
typedef struct {
    BigNum N;
    double near_square_score;
    double small_factor_evidence;
    bool divisible_by_wheel_prime;
    bool is_perfect_square;
    bool is_probable_prime;
    unsigned bits;
} ObservableFeatureVecBN;

/** Compute BigNum observable features (no DB needed). */
static void observable_compute_bn(const BigNum* N, ObservableFeatureVecBN* vec) {
    memset(vec, 0, sizeof(*vec));
    bn_copy(&vec->N, N);
    vec->bits = bn_bitlen(N);
    if (bn_is_zero(N) || bn_is_one(N)) return;

    vec->near_square_score = observable_near_square_score_bn(N);

    /* Perfect square check: ceil(sqrt(N))^2 == N */
    {
        BigNum s; bn_isqrt_ceil(&s, N);
        BigNum s2; bn_mul(&s2, &s, &s);
        vec->is_perfect_square = (bn_cmp(&s2, N) == 0);
    }

    /* Wheel-prime divisibility */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (bn_mod_u64(N, WHEEL_PRIMES[i]) == 0u && !bn_is_zero(N)) {
            /* Ensure N > wheel prime */
            BigNum wp; bn_from_u64(&wp, WHEEL_PRIMES[i]);
            if (bn_cmp(N, &wp) > 0) {
                vec->divisible_by_wheel_prime = true;
                break;
            }
        }
    }
    vec->small_factor_evidence = vec->divisible_by_wheel_prime ? 1.0 : 0.0;

    /* Primality */
    vec->is_probable_prime = bn_factor_is_prime(N);
}

/** Suggest routing for BigNum input. */
static const char* observable_route_suggest_bn(const ObservableFeatureVecBN* vec) {
    if (vec->is_probable_prime) return "prime";
    if (vec->divisible_by_wheel_prime) return "wheel";
    if (vec->is_perfect_square) return "fermat";
    if (vec->near_square_score > 0.9) return "fermat";
    /* For large N without DB, rho/ecm are the real workhorses */
    if (vec->bits > 128u) return "ecm";
    if (vec->near_square_score > 0.5) return "fermat";
    return "rho";
}

/**
 * Cheap heuristic route hint from observable features only.
 * This is a fallback for when the full coverage model is not yet in scope.
 * Callers with access to the coverage model should use predict_available() instead.
 */
static const char* observable_route_hint(const ObservableFeatureVec* vec) {
    if (vec->is_prime) return "prime";
    if (vec->divisible_by_wheel_prime) return "wheel";
    if (vec->is_perfect_square) return "fermat";
    if (vec->near_square_score > 0.9) return "fermat";
    if (vec->cpss_coverage_complete) return "trial";
    if (vec->near_square_score > 0.5) return "fermat";
    return "auto";
}
