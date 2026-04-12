# APP Guide

The `APP` surface is the interactive control layer for the CPSS Viewer. It combines database loading, prime queries, factorisation, space exploration, semiprime analysis, export pipelines, evaluation tooling, benchmarking, and NQL access in one shell.

---

## What This Guide Set Covers

There are two user-facing ways to work with the APP layer:

- `cpss_viewer APP` for the interactive shell
- `cpss_viewer <command>` for one-shot utility commands from `cli.c`

Use this file as the entry point, then jump to the focused guide for the area you are working in.

## Starting APP Mode

```text
cpss_viewer APP
cpss_viewer APP --wheel-cache 256MB --hot-cache 1GB --db-ram 8GB --threads 8
```

For single-stream work, a common local load target is:

`C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin`

Inside APP mode:

```text
> help
> help factor
> help space
> exit
```

---

## Guide Map

| Area | Use this when you want to... | Guide |
|---|---|---|
| Getting started | Launch APP mode, understand the shell, and run a first session | [APP_GettingStarted.md](APP_GettingStarted.md) |
| One-shot utilities | Inspect files, export primes, clip files, split files, or convert formats without entering APP mode | [APP_CLIUtilitiesGuide.md](APP_CLIUtilitiesGuide.md) |
| Database and cache management | Load streams, load shard directories, inspect DB state, and manage caches | [APP_DatabaseGuide.md](APP_DatabaseGuide.md) |
| Prime queries and NQL | Run prime lookups, range queries, classification, and raw `query` statements | [APP_QueryGuide.md](APP_QueryGuide.md) |
| Factorisation | Use auto-factor, method-specific factorisers, and routing/bounds tools | [APP_FactorisationGuide.md](APP_FactorisationGuide.md) |
| Prime-pair spaces | Navigate `space`, compare regions, inspect metrics, and use space-oriented analysis | [APP_SpaceGuide.md](APP_SpaceGuide.md) |
| Semiprime analysis | Work with semiprime observation/coverage/bounds/routing views and overlay-style commands | [APP_SemiPrimeGuide.md](APP_SemiPrimeGuide.md) |
| Export and batch pipelines | Export space data and run `query-file` / `factor-file` pipelines | [APP_ExportGuide.md](APP_ExportGuide.md) |
| Evaluation tooling | Generate corpora, validate labels, and evaluate router/method behaviour | [APP_EvaluationGuide.md](APP_EvaluationGuide.md) |
| Benchmarking | Run APP benchmarks for queries, factoring, and sector behaviour | [APP_BenchmarkGuide.md](APP_BenchmarkGuide.md) |
| Legacy compatibility | Translate older per-space commands into the newer unified APP surface | [APP_LegacyGuide.md](APP_LegacyGuide.md) |
| Workflow recipes | Follow the recommended APP exploration order from load to export | [APP_WorkflowGuide.md](APP_WorkflowGuide.md) |

---

## Existing Detailed Docs Outside `APP`

These guides already exist and are worth keeping open alongside the APP docs:

- [NQL User Guide](../NQL/NQL_UserGuide.md)
- [NQL Documentation](../NQL/NQL_Documentation.md)
- [Index Space](../PrimePairSpaces/IndexSpace.md)
- [Value Space](../PrimePairSpaces/ValueSpace.md)
- [Log Space](../PrimePairSpaces/LogSpace.md)
- [Mean-Imbalance Space](../PrimePairSpaces/MeanImbalanceSpace.md)
- [Product-Ratio Space](../PrimePairSpaces/ProductRatioSpace.md)
- [Fermat Space](../PrimePairSpaces/FermatSpace.md)
- [MinMax Space](../PrimePairSpaces/MinMaxSpace.md)
- [SemiPrime Observation Space](../SemiPrimeSpaces/SemiPrimeObservationSpace.md)
- [SemiPrime Coverage Space](../SemiPrimeSpaces/SemiPrimeCoverageSpace.md)
- [CFrac Space](../PrimePairSpaces/CFracSpace.md)
- [Lattice Space](../PrimePairSpaces/LatticeSpace.md)

---

## Recommended First Session

```text
cpss_viewer APP --hot-cache 512MB
```

```text
> help
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> db-status
> is-prime 1000003
> factor 143
> space-list
> space value diagonal 4 8
> query SELECT p FROM PRIME WHERE p BETWEEN 1 AND 100
```

This path gives you one example from each major APP area:

- shell discovery
- database loading
- query execution
- factorisation
- space exploration
- NQL usage

---

## High-Level Command Map

If you only need a mental model of the APP shell, use these buckets:

- **Database**: `load`, `load-db`, `db-status`, `preload`, cache commands, sidecar/manifest tools
- **Queries**: `is-prime`, `next-prime`, `pi`, `range-stats`, `classify`, `prime-iter`, `query`
- **Factorisation**: `factor`, `factor-rho`, `factor-ecm`, `factor-pminus1`, `factor-pplus1`, `difference-bounds`
- **Prime-pair spaces**: `space`, `space-compare`, `space-stats`, `space-export`, `space-metrics`
- **Semiprime analysis**: `space semiprime ...`, `observable`, `coverage-sim`, `factor-explain`
- **Evaluation**: `corpus-generate`, `corpus-validate`, `router-eval`, `method-compete`, `metric-eval`
- **Benchmarking**: `benchmark-*`
- **Batch pipelines**: `query-file`, `factor-file`

---

## Suggested Reading Order

1. [APP_GettingStarted.md](APP_GettingStarted.md)
2. [APP_DatabaseGuide.md](APP_DatabaseGuide.md)
3. [APP_QueryGuide.md](APP_QueryGuide.md)
4. [APP_FactorisationGuide.md](APP_FactorisationGuide.md)
5. [APP_SpaceGuide.md](APP_SpaceGuide.md)
6. [APP_SemiPrimeGuide.md](APP_SemiPrimeGuide.md)
7. [APP_ExportGuide.md](APP_ExportGuide.md)
8. [APP_EvaluationGuide.md](APP_EvaluationGuide.md)
9. [APP_BenchmarkGuide.md](APP_BenchmarkGuide.md)
10. [APP_WorkflowGuide.md](APP_WorkflowGuide.md)

Use [APP_CLIUtilitiesGuide.md](APP_CLIUtilitiesGuide.md) whenever you need the non-interactive helpers from `cli.c`.
Use [APP_LegacyGuide.md](APP_LegacyGuide.md) when translating older command habits into the new unified interface.

---

Back to [Documentation Index](../Documentation.md)
