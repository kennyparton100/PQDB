/**
 * FermatSpace.c - Fermat space (A, B) functions.
 * Part of the CPSS Hidden Factor Spaces module.
 *
 * A = (p + q) / 2,  B = (q - p) / 2.  (q >= p convention)
 * Identity: N = A^2 - B^2 = (A-B)(A+B) = p*q.
 *
 * B = 0 => perfect square. B small => Fermat finds it fast.
 * B large => Fermat gets cooked.
 *
 * Inputs are assumed to be valid prime factors (p, q >= 2).
 * All arithmetic uses overflow-safe paths (U128 where needed).
 *
 * Depends on: cpss_arith.c (isqrt_u64, U128, u128_mul64, u128_sub)
 */

/**
 * Compute Fermat space coordinates from known factors p, q.
 * Uses doubles because A and B may be half-integers when p+q is odd.
 * Sets *A_out = (p+q)/2.0, *B_out = |q-p|/2.0.
 */
static void fermat_space_from_factors(uint64_t p, uint64_t q,
                                       double* A_out, double* B_out) {
    uint64_t lo = (p < q) ? p : q;
    uint64_t hi = (p < q) ? q : p;
    *A_out = ((double)lo + (double)hi) / 2.0;
    *B_out = ((double)hi - (double)lo) / 2.0;
}

/**
 * Compute Fermat coordinates as integers (only valid when p, q same parity).
 * For odd semiprimes (both factors odd), A and B are exact integers.
 * Returns false if p + q is odd (A, B would be half-integers).
 * Returns false if A overflows uint64 (both factors near UINT64_MAX).
 *
 * Parity check uses XOR to avoid overflow: (lo ^ hi) & 1 detects
 * whether lo + hi is odd without computing the sum.
 */
static bool fermat_space_from_factors_exact(uint64_t p, uint64_t q,
                                             uint64_t* A_out, uint64_t* B_out) {
    uint64_t lo = (p < q) ? p : q;
    uint64_t hi = (p < q) ? q : p;
    /* XOR low bits: if different parity, sum is odd => half-integer */
    if ((lo ^ hi) & 1u) return false;
    /* Overflow-safe average: lo + (hi - lo) / 2 never overflows */
    *A_out = lo + (hi - lo) / 2u;
    *B_out = (hi - lo) / 2u;
    return true;
}

/**
 * Verify the Fermat identity: N = A^2 - B^2.
 * Uses U128 to avoid overflow. Returns true if identity holds.
 */
static bool fermat_space_verify(uint64_t N, uint64_t A, uint64_t B) {
    if (A < B) return false;
    U128 a_sq = u128_mul64(A, A);
    U128 b_sq = u128_mul64(B, B);
    if (u128_lt(a_sq, b_sq)) return false;
    U128 diff = u128_sub(a_sq, b_sq);
    return u128_fits_u64(diff) && diff.lo == N;
}

/**
 * Compute the Fermat search starting point: A_start = ceil(sqrt(N)).
 * This is where Fermat's method begins scanning.
 */
static uint64_t fermat_space_search_start(uint64_t N) {
    if (N <= 1u) return N;
    uint64_t root = (uint64_t)isqrt_u64(N);
    if (root * root < N) ++root;
    return root;
}

/**
 * Estimate the number of Fermat steps needed to find factors p, q.
 * steps = A - ceil(sqrt(N)) where A = (p + q) / 2.
 * Returns 0 if N is a perfect square, UINT64_MAX if factors are
 * incompatible with Fermat (different parity or A overflows).
 */
static uint64_t fermat_space_estimated_steps(uint64_t N, uint64_t p, uint64_t q) {
    uint64_t A, B;
    if (!fermat_space_from_factors_exact(p, q, &A, &B)) return UINT64_MAX;
    uint64_t start = fermat_space_search_start(N);
    return (A >= start) ? (A - start) : 0u;
}
