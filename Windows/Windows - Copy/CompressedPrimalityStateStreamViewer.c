/*
 * CompressedPrimalityStateStreamViewer.c - Amalgamation shell.
 *
 * Windows-native C port of the CPSS viewer / validator / exporter /
 * clipper / splitter / benchmarker / NQL maths engine.
 *
 * Uses Win32 API for file mapping, directory creation, temporary
 * files, and high-resolution timing.
 *
 * This file #includes the logical source files in dependency order.
 * The vcxproj compiles only this file - all code lives in the included .c files.
 *
 * Build (MSVC):
 *   cl /O2 /W4 CompressedPrimalityStateStreamViewer.c zlib.lib /Fe:cpss_viewer.exe
 */

/* ── Platform & core data types ── */
#include "platform.h"                          /* includes, defines, Win32 pragmas              */
#include "presieve.h"                          /* presieve types + extern decls (separate TU)   */
#include "DataTypes/types.c"                   /* all struct/enum/typedef definitions            */
#include "DataTypes/status.c"                  /* query status enum + helpers                   */
#include "DataTypes/globals.c"                 /* global state, wheel tables, cache instances    */
#include "DataTypes/util.c"                    /* memory, die, bitops, wheel init, presieve, zlib*/

/* ── CPSS — Compressed Primality State Streams ── */
#include "CPSS/cpss_cache.c"                   /* hash map, wheel cache, hot cache, rank/select */
#include "CPSS/cpss_sidecar.c"                 /* .cpssi sidecar index read/write/verify        */
#include "CPSS/cpss_stream.c"                  /* CPSSStream: open, close, index, is_prime, etc  */
#include "CPSS/cpss_query.c"                   /* extended queries: next/prev, pi, nth, gap     */
#include "CPSS/cpss_database.c"                /* CPSSDatabase: multi-shard routing + queries   */
#include "CPSS/cpss_manifest.c"                /* .cpss-manifest.json read/write/verify         */
#include "CPSS/cpss_iter.c"                    /* PrimeIterator: cursor API for streaming primes*/

/* ── Numeric arithmetic (not CPSS-specific) ── */
#include "DataTypes/arith.c"                   /* 128-bit modular arithmetic, Miller-Rabin      */
#include "DataTypes/bignum.c"                  /* BigNum (1024-bit) arithmetic, Montgomery, GCD */

/* ── Factorisation ── */
#include "Factorisation/factor.c"              /* trial division, p-1, p+1, rho, ECM, auto-router */
#include "Factorisation/SQUFOF/squfof.c"       /* Shanks' SQUFOF (uint64)                       */
#include "Factorisation/QuadraticSieve/qs.c"   /* SIQS Quadratic Sieve (uint64 + U128)          */
#include "Factorisation/gnfs_stub.c"           /* GNFS sub-file includes                        */

/* ── Prime-Pair Spaces — exact (p,q) factor-pair geometry ── */
#include "PrimePairSpaces/ValueSpace.c"        /* (p,q) value geometry   */
#include "PrimePairSpaces/LogSpace.c"          /* (u,v) log geometry     */
#include "PrimePairSpaces/MeanImbalanceSpace.c"/* (m,δ) on top of Log   */
#include "PrimePairSpaces/ProductRatioSpace.c" /* (N,R) ratio buckets    */
#include "PrimePairSpaces/FermatSpace.c"       /* (A,B) Fermat coords    */
#include "PrimePairSpaces/IndexSpace.c"        /* (i,j) index lookups    */
#include "PrimePairSpaces/MinMaxSpace.c"       /* (s,l) coverage logic   */
#include "PrimePairSpaces/ResidueGridSpace.c"  /* residue classes         */
#include "PrimePairSpaces/SmoothnessHelpers.c" /* B-smooth helpers        */
#include "PrimePairSpaces/CFracSpace.c"        /* CF expansion analysis   */
#include "PrimePairSpaces/LatticeSpace.c"      /* lattice-reduction view  */

/* ── SemiPrime Spaces — N-centric inference ── */
#include "SemiPrimeSpaces/SemiPrimeResidueSpace.c"     /* N-driven elimination   */
#include "SemiPrimeSpaces/SemiPrimeSmoothnessSpace.c"   /* routing signals        */
#include "SemiPrimeSpaces/SemiPrimeObservationSpace.c"  /* observable signals     */
#include "SemiPrimeSpaces/SemiPrimeCoverageSpace.c"     /* cost/plausibility      */

/* ── NQL — Number Query Language (SQL for numbers) ── */
#include "NQL/nql_types.c"                     /* tokens, AST nodes, value types, registries    */
#include "NQL/nql_array.c"                     /* NqlArray type + ARRAY_* functions             */
#include "NQL/nql_matrix_gf2.c"                /* GF(2) binary matrix for QS linear algebra    */
#include "NQL/nql_poly.c"                      /* Polynomial type + arithmetic + GNFS primitives */
#include "NQL/nql_qs_types.c"                  /* QS native opaque types + all QS built-ins     */
#include "NQL/nql_gnfs.c"                      /* GNFS polynomial selection + factor base + norms */
#include "NQL/nql_lexer.c"                     /* hand-written tokeniser                        */
#include "NQL/nql_parser.c"                    /* recursive-descent parser → AST                */
#include "NQL/nql_funcs.c"                     /* built-in functions + number-type registry      */
#include "NQL/nql_eval.c"                      /* query executor, aggregates, formatting         */
#include "NQL/nql_entry.c"                     /* entry point: nql_execute(), help               */

/* ── CPSS batch & mutation ── */
#include "CPSS/cpss_batch.c"                   /* query-file / factor-file batch pipelines      */
#include "CPSS/cpss_mutate.c"                  /* clip, split-trillion, encode_result_bits      */

/* ── Benchtesting ── */
#include "Benchtesting/sieve.c"                /* segmented sieve baseline for benchmarks       */
#include "Benchtesting/benchtest_legacy.c"     /* per-sector benchmarks: benchmark, bench-all   */

/* ── APP — CLI & interactive REPL ── */
#include "APP/cli.c"                           /* parse helpers, print_usage, CLI commands      */
#include "Benchtesting/benchtest_query.c"      /* query benchmark suite: 5 benchmark commands   */

/* ── Main entry point (app.c internally includes SpaceAnalysis + SemiPrime analysis chain) ── */
#include "APP/app.c"                           /* main(), APP mode loop, command dispatch       */

/* DO NOT add code below this line. All source lives in the included files. */