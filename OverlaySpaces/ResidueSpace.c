/**
 * ResidueSpace.c - Residue space (p mod M, q mod M) functions.
 * Part of the CPSS Overlay Spaces module.
 *
 * This is an overlay, not a main geometry. It colours value/log space
 * with modular constraints. For M = 30, primes > 5 live only in
 * residue classes {1, 7, 11, 13, 17, 19, 23, 29} mod 30.
 *
 * Useful for: wheel filters, residue exclusions, congruence overlays,
 * infrastructure constraints.
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
 * Mark impossible cells in the residue grid for a given N and modulus M.
 * grid is a row-major array of size grid_size * grid_size where grid_size
 * is the number of valid residue classes.
 * grid[i * grid_size + j] = 1 if classes[i] * classes[j] ≡ N (mod M), else 0.
 *
 * classes[] must be pre-computed via residue_valid_classes().
 * Returns the number of surviving (possible) cells.
 */
static size_t residue_eliminate_for_N(uint64_t N, uint32_t M,
                                       const uint32_t* classes, size_t grid_size,
                                       uint8_t* grid) {
    size_t survivors = 0u;
    uint64_t N_mod = N % M;
    for (size_t i = 0u; i < grid_size; ++i) {
        for (size_t j = 0u; j < grid_size; ++j) {
            uint64_t prod = ((uint64_t)classes[i] * (uint64_t)classes[j]) % M;
            if (prod == N_mod) {
                grid[i * grid_size + j] = 1u;
                ++survivors;
            }
            else {
                grid[i * grid_size + j] = 0u;
            }
        }
    }
    return survivors;
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
