/**
 * cpss_presieve.h - Shared declarations for presieve globals and the
 * build_presieve_state_for_index_range function.
 * Used by both the amalgamation (cpss_globals.c) and the separate
 * presieve translation unit (cpss_presieve.c).
 */
#ifndef CPSS_PRESIEVE_H
#define CPSS_PRESIEVE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct { uint8_t* data; size_t len; } PresieveByteBuf;

/* Presieve globals — defined in the amalgamation (cpss_globals.c),
 * declared extern here so cpss_presieve.c can see them. */
extern uint32_t  g_wheel_modulus;
extern uint32_t* g_wheel_residues;
extern uint32_t  g_residue_count;
extern int32_t*  g_residue_to_pos;
extern uint32_t* g_presieve_primes;
extern size_t    g_presieve_prime_count;

/* The presieve function — compiled in its own TU for optimizer isolation. */
PresieveByteBuf build_presieve_state_for_index_range(uint64_t start_index, uint32_t total_candidates);

#endif /* CPSS_PRESIEVE_H */
