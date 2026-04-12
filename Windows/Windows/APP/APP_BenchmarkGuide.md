# APP Benchmark Guide

This guide covers the APP benchmarking commands.

For the top-level index, see [APP_Guide.md](APP_Guide.md).

---

## Benchmark Command Groups

APP exposes two broad benchmark families:

- **method benchmarks** such as Fermat, rho, and ECM
- **DB-backed workload benchmarks** such as query, factor, iteration, and sector coverage tests

---

## Core Benchmark Commands

### Method-focused benchmarks

```text
> benchmark-fermat [--count N] [--bits B] [--seed S] [--max-steps N] [--threads N]
> benchmark-rho [--count N] [--bits B] [--seed S] [--max-iterations N] [--threads N]
> benchmark-ecm [--count N] [--bits B] [--seed S] [--B1 N] [--curves N] [--threads N]
```

`benchmark-fermat` also accepts these aliases in the APP command dispatcher:

- `benchmark-factor-fermat`
- `benchmark-fermat-factor`

These are good choices when you want to compare algorithm behaviour without centering the session on DB queries.

---

## Wide APP Benchmark Sweeps

### `benchmark-everything`

```text
> benchmark-everything [--count N] [--bits B] [--seed S] [--mode cold|hot] [--threads N]
```

Use this for a broad benchmark pass across multiple operations.

### Query and factor workload benchmarks

```text
> benchmark-is-prime
> benchmark-count-range
> benchmark-list-range
> benchmark-mixed
> benchmark-semiprime-factorisation
> benchmark-next-prime
> benchmark-prev-prime
> benchmark-pi
> benchmark-nth-prime
> benchmark-prime-gap
> benchmark-primes-near
> benchmark-range-stats
> benchmark-factor-support
> benchmark-pminus1
> benchmark-pplus1
> benchmark-iter
> benchmark-factor-trial
> benchmark-factor-auto
> benchmark-classify
> benchmark-all-sectors
```

These are the commands to use when you want to measure the APP shell’s real DB-backed workload surface.

---

## DB Requirement

In the APP command loop, most `benchmark-*` commands require a loaded DB.

If no DB is loaded, APP prints:

```text
No database loaded.
```

Practical rule:

- load a DB before query, factor, iterator, or sector benchmarks
- method-only benchmarks such as Fermat/rho/ECM are the least DB-dependent starting points

---

## Benchmark Modes

`benchmark-everything` and many DB-backed workload benchmarks support `--mode cold|hot`.

`benchmark-all-sectors` is different and uses:

- `--mode hot`
- `--mode fair`
- `--mode auto`

Inside APP mode, the benchmark dispatcher uses this to constrain the benchmark range to the shell’s known cold or hot segment ranges.

That means `--mode` is not just a label — it changes which loaded range is benchmarked.

Example:

```text
> load-db shards/
> benchmark-everything --count 100 --bits 32 --mode hot
> benchmark-is-prime --count 10000 --mode cold
```

---

## Example Sessions

### Compare pure methods

```text
> benchmark-fermat --count 100 --bits 32 --seed 42
> benchmark-rho --count 100 --bits 32 --seed 42
> benchmark-ecm --count 50 --bits 64 --B1 1000 --curves 20
```

### Benchmark live DB-backed workloads

```text
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> benchmark-is-prime --count 10000 --mode hot
> benchmark-everything --count 50 --bits 32 --mode cold
> benchmark-all-sectors --mode auto
```

---

## When to Use Benchmarking vs Evaluation

Use benchmarking when you care about:

- runtime
- throughput
- hot/cold behaviour
- cache-sensitive performance

Use evaluation when you care about:

- routing quality
- corpus labels
- method win rates
- metric summaries

For the experiment/analysis side, see [APP_EvaluationGuide.md](APP_EvaluationGuide.md).

---

## Related Guides

- Database loading and caches: [APP_DatabaseGuide.md](APP_DatabaseGuide.md)
- Evaluation tooling: [APP_EvaluationGuide.md](APP_EvaluationGuide.md)
- Factorisation commands: [APP_FactorisationGuide.md](APP_FactorisationGuide.md)
