/**
 * ResidueGridSpace.c - Residue grid (p mod M, q mod M) pair-centric primitives.
 * Part of the CPSS Prime-Pair Spaces module (exact factor-pair geometry).
 *
 * This is an overlay on value/log space. It colours the (p, q) grid
 * with modular constraints. For M = 30, primes > 5 live only in
 * residue classes {1, 7, 11, 13, 17, 19, 23, 29} mod 30.
 *
 * Contains: residue class enumeration, pair-possibility check,
 * surviving-fraction computation.
 *
 * N-driven elimination (residue_eliminate_for_N) lives in
 * SemiPrimeSpaces/SemiPrimeResidueSpace.c.
 *
 * Depends on: cpss_util.c (gcd_u64)
 */

/**
 * Compute all residue classes coprime to M.
 * Primes > max_wheel_prime can only appear in these classes mod M.
 * Writes residues into classes[] (must have capacity >= M).
 * Sets *count to the number of valid classes (Euler's totient of M).
 */
static void residue_valid_classes(uint32_t M, uint32_t* classes, size_t* count) {
    *count = 0u;
    if (M == 0u) return;
    for (uint32_t r = 0u; r < M; ++r) {
        if (gcd_u64((uint64_t)r, (uint64_t)M) == 1u) {
            classes[*count] = r;
            ++(*count);
        }
    }
}

/**
 * Check if a (p mod M, q mod M) pair is consistent with a given N.
 * For the pair to be valid: (p_mod * q_mod) mod M == N mod M.
 * Returns true if the pair is consistent.
 */
static bool residue_pair_possible(uint64_t N, uint32_t p_mod, uint32_t q_mod,
                                   uint32_t M) {
    if (M == 0u) return false;
    uint64_t product_mod = ((uint64_t)p_mod * (uint64_t)q_mod) % M;
    return product_mod == (N % M);
}

/**
 * Compute the fraction of the full residue grid that survives.
 * = (coprime residues)^2 / M^2.
 * This tells you how much the wheel filter eliminates.
 */
static double residue_surviving_fraction(uint32_t M) {
    if (M == 0u) return 0.0;
    size_t totient = 0u;
    for (uint32_t r = 0u; r < M; ++r) {
        if (gcd_u64((uint64_t)r, (uint64_t)M) == 1u) ++totient;
    }
    double grid_fraction = (double)totient / (double)M;
    return grid_fraction * grid_fraction;
}
