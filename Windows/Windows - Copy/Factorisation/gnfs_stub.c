/**
 * gnfs_stub.c - General Number Field Sieve (GNFS).
 *
 * Thin stub that #includes the GNFS sub-files in dependency order.
 * All GNFS code lives in the Factorisation/GNFS/ folder.
 */

#include "GNFS/gnfs_types.c"     /* constants, structs, config defaults          */
#include "GNFS/gnfs_poly.c"      /* polynomial selection, eval, homogeneous eval */
#include "GNFS/gnfs_algebra.c"   /* AlgElem ops, AlgExact, alg_mul, alg_inv     */
#include "GNFS/gnfs_field.c"     /* QR/sqrt exponents, irreducibility helpers    */
#include "GNFS/gnfs_hensel.c"    /* Hensel lifting algebraic square root         */
#include "GNFS/gnfs_synth.c"     /* synthetic-square CRT pipeline diagnostic     */
#include "GNFS/gnfs_unit.c"      /* fundamental unit computation, unit parity    */
#include "GNFS/gnfs_pipeline.c"  /* main GNFS pipeline (sieve, linalg, sqrt)     */
#include "GNFS/gnfs_entry.c"     /* entry points: u64, BigNum, print_status      */
