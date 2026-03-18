/**
 * SemiPrimeResidueSpace.c - N-driven residue elimination.
 * Part of the CPSS SemiPrime Spaces module (N-centric inference).
 *
 * Given only N and a modulus M, eliminates impossible (p mod M, q mod M)
 * cells from the residue grid. This is semiprime analysis: we know N
 * but not the factors.
 *
 * Pair-grid primitives (residue_valid_classes, residue_pair_possible,
 * residue_surviving_fraction) live in PrimePairSpaces/ResidueGridSpace.c.
 *
 * Depends on: PrimePairSpaces/ResidueGridSpace.c (residue_valid_classes)
 */

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
