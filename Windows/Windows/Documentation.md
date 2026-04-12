# CPSS Viewer Documentation Index

Root navigation for all documentation in the CPSS Viewer codebase.

---

## APP — Interactive Shell & CLI

| Guide | Description |
|-------|-------------|
| [APP Guide](APP/APP_Guide.md) | Top-level entry point for all APP documentation |
| [Getting Started](APP/APP_GettingStarted.md) | First session walkthrough |
| [CLI Utilities](APP/APP_CLIUtilitiesGuide.md) | One-shot file commands (info, list, clip, export) |
| [Database](APP/APP_DatabaseGuide.md) | Loading, caching, sidecar/manifest tools |
| [Queries](APP/APP_QueryGuide.md) | Prime lookups, iteration, classification |
| [Factorisation](APP/APP_FactorisationGuide.md) | Auto-factor, method-specific commands, routing |
| [Spaces](APP/APP_SpaceGuide.md) | Prime-pair space navigation and comparison |
| [SemiPrime](APP/APP_SemiPrimeGuide.md) | Per-N semiprime observation/coverage/routing |
| [Export](APP/APP_ExportGuide.md) | Batch pipelines and CSV/JSONL export |
| [Evaluation](APP/APP_EvaluationGuide.md) | Corpus generation, router-eval, method-compete |
| [Benchmarking](APP/APP_BenchmarkGuide.md) | APP benchmark suite |

---

## NQL — Number Query Language (223 built-in functions)

| Guide | Description |
|-------|-------------|
| [NQL User Guide](NQL/NQL_UserGuide.md) | Complete usage reference: types, functions, syntax |
| [NQL Technical Docs](NQL/NQL_Documentation.md) | Architecture, internals, extension guide |

### NQL Scripts

| Script | Description |
|--------|-------------|
| [quadratic_sieve.nql](NQL/Scripts/quadratic_sieve.nql) | QS factoring via native opaque objects |
| [gnfs.nql](NQL/Scripts/gnfs.nql) | GNFS factoring: polynomial selection → sieve → LA → extraction |

---

## Factorisation Methods

| Method | Guide |
|--------|-------|
| Trial Division | [TrialDivision.md](Factorisation/TrialDivision/TrialDivision.md) |
| Pollard Rho | [Rho.md](Factorisation/Rho/Rho.md) |
| Pollard p-1 | [PMinus1.md](Factorisation/PMinus1/PMinus1.md) |
| Williams p+1 | [PPlus1.md](Factorisation/PPlus1/PPlus1.md) |
| Fermat | [Fermat.md](Factorisation/Fermat/Fermat.md) |
| ECM | [ECM.md](Factorisation/ECM/ECM.md) |
| SQUFOF | [SQUFOF.md](Factorisation/SQUFOF/SQUFOF.md) |
| Quadratic Sieve | [QuadraticSieve.md](Factorisation/QuadraticSieve/QuadraticSieve.md) |
| GNFS (WIP) | [GNFS.md](Factorisation/GNFS/GNFS.md) |

---

## Prime-Pair Spaces

| Space | Guide |
|-------|-------|
| Index | [IndexSpace.md](PrimePairSpaces/IndexSpace.md) |
| Value | [ValueSpace.md](PrimePairSpaces/ValueSpace.md) |
| Log | [LogSpace.md](PrimePairSpaces/LogSpace.md) |
| Mean-Imbalance | [MeanImbalanceSpace.md](PrimePairSpaces/MeanImbalanceSpace.md) |
| Product-Ratio | [ProductRatioSpace.md](PrimePairSpaces/ProductRatioSpace.md) |
| Fermat | [FermatSpace.md](PrimePairSpaces/FermatSpace.md) |
| MinMax | [MinMaxSpace.md](PrimePairSpaces/MinMaxSpace.md) |
| CFrac | [CFracSpace.md](PrimePairSpaces/CFracSpace.md) |
| Lattice | [LatticeSpace.md](PrimePairSpaces/LatticeSpace.md) |

---

## SemiPrime Spaces

| Space | Guide |
|-------|-------|
| Observation | [SemiPrimeObservationSpace.md](SemiPrimeSpaces/SemiPrimeObservationSpace.md) |
| Coverage | [SemiPrimeCoverageSpace.md](SemiPrimeSpaces/SemiPrimeCoverageSpace.md) |

---

## Codebase Structure

```
Windows/
├── DataTypes/          types.c, arith.c (U128), bignum.c (BigNum), globals.c, util.c
├── CPSS/               cpss_stream.c, cpss_query.c, cpss_database.c, cpss_cache.c, ...
├── Factorisation/      factor.c (auto-routers + shared helpers)
│   ├── Rho/            rho.c — Pollard rho (uint64/U128/BigNum)
│   ├── ECM/            ecm.c — Elliptic Curve Method
│   ├── Fermat/         fermat.c — Fermat factorisation
│   ├── TrialDivision/  trial_division.c — DB-backed trial division
│   ├── PMinus1/        pminus1.c — Pollard p-1
│   ├── PPlus1/         pplus1.c — Williams p+1
│   ├── SQUFOF/         squfof.c — Shanks' SQUFOF
│   ├── QuadraticSieve/ qs.c — Self-Initialising QS
│   └── GNFS/           (work in progress)
├── PrimePairSpaces/    9 space implementations + documentation
├── SemiPrimeSpaces/    4 per-N analysis spaces + documentation
├── NQL/                nql_types.c, nql_lexer.c, nql_parser.c, nql_funcs.c, nql_eval.c, nql_entry.c
├── APP/                app.c (main + command dispatch), cli.c (CLI helpers)
├── Benchtesting/       benchtest_legacy.c, benchtest_query.c, sieve.c
├── Tests/              test_harness.h, test_integration.c
└── CompressedPrimalityStateStreamViewer.c  (amalgamation shell)
```

The project compiles as a single translation unit via the amalgamation file.
All `.c` files under subdirectories are `#include`d in dependency order.
