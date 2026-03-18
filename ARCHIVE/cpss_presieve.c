/**
 * cpss_presieve.c - Presieve bitmap construction, compiled as a separate TU.
 *
 * The inner sieve loop uses incremental residue/block tracking instead of
 * division/modulo per iteration. The wheel modulus and residue count are
 * compile-time constants so the compiler can emit multiply-shift sequences
 * for any remaining arithmetic involving these values.
 */
#include "cpss_presieve.h"
#include <stdlib.h>
#include <string.h>

/* Compile-time wheel constants.
 * PRESIEVE_WMOD = product of wheel primes {2,3,5,7,11,13} = 30030
 * PRESIEVE_RCNT = euler_phi(30030) = 5760
 * A runtime check in the amalgamation's init_wheel() validates these match. */
#define PRESIEVE_WMOD 30030u
#define PRESIEVE_RCNT 5760u

/* ── Force-inlined bit helpers ── */

#if defined(__clang__) || defined(__GNUC__)
static __attribute__((always_inline)) inline void presieve_clear_bit(uint8_t* data, uint64_t bit_index) {
#elif defined(_MSC_VER)
static __forceinline void presieve_clear_bit(uint8_t* data, uint64_t bit_index) {
#else
static inline void presieve_clear_bit(uint8_t* data, uint64_t bit_index) {
#endif
    data[bit_index >> 3u] &= (uint8_t)~(1u << (bit_index & 7u));
}

static PresieveByteBuf presieve_bitset_all_ones(uint64_t bit_count) {
    PresieveByteBuf buf;
    size_t byte_count = (size_t)((bit_count + 7u) / 8u);
    buf.data = (uint8_t*)malloc(byte_count ? byte_count : 1u);
    buf.len = byte_count;
    if (!buf.data) { buf.len = 0; return buf; }
    memset(buf.data, 0xFF, byte_count);
    if (byte_count > 0u) {
        uint32_t extra_bits = (uint32_t)(byte_count * 8u - bit_count);
        if (extra_bits) {
            uint32_t used = 8u - extra_bits;
            buf.data[byte_count - 1u] = (uint8_t)((1u << used) - 1u);
        }
    }
    return buf;
}

/* ── The hot presieve function ── */

PresieveByteBuf build_presieve_state_for_index_range(uint64_t start_index, uint32_t total_candidates) {
    const int32_t*  r2pos     = g_residue_to_pos;
    const uint32_t* ps_primes = g_presieve_primes;
    const size_t    ps_count  = g_presieve_prime_count;

    uint64_t start_block = start_index / PRESIEVE_RCNT;
    uint64_t end_index = start_index + (uint64_t)total_candidates - 1u;
    uint64_t end_block = end_index / PRESIEVE_RCNT;
    uint64_t low = start_block * (uint64_t)PRESIEVE_WMOD;
    uint64_t high = (end_block + 1u) * (uint64_t)PRESIEVE_WMOD - 1u;

    PresieveByteBuf state = presieve_bitset_all_ones(total_candidates);
    if (start_index == 0u && total_candidates > 0u && g_wheel_residues[0] == 1u) {
        presieve_clear_bit(state.data, 0u);
    }

    uint8_t* const bits = state.data;

    for (size_t i = 0u; i < ps_count; ++i) {
        uint32_t p = ps_primes[i];
        uint64_t p2 = (uint64_t)p * (uint64_t)p;
        uint64_t first = p2;
        if (first < low) {
            first = ((low + p - 1u) / p) * (uint64_t)p;
        }
        if (first > high) continue;

        /* Per-prime setup: compute initial block and residue via one division.
         * Then update incrementally: residue += p, carry into block. */
        uint64_t block   = first / PRESIEVE_WMOD;
        uint32_t residue = (uint32_t)(first - block * PRESIEVE_WMOD);

        for (uint64_t n = first; n <= high; n += p) {
            int32_t pos = r2pos[residue];
            if (pos >= 0) {
                uint64_t idx = (block - start_block) * (uint64_t)PRESIEVE_RCNT + (uint64_t)pos;
                if (idx < total_candidates) {
                    presieve_clear_bit(bits, idx);
                }
            }

            /* Incremental update: advance residue by p, carry into block */
            residue += p;
            if (residue >= PRESIEVE_WMOD) {
                residue -= PRESIEVE_WMOD;
                ++block;
                /* For p < PRESIEVE_WMOD (always true: max presieve prime is 997 < 30030),
                 * one subtraction is sufficient — no loop needed. */
            }

            if (UINT64_MAX - n < p) break;
        }
    }

    return state;
}
