# APP Factorisation Guide

This guide covers APP-mode factorisation commands, routing tools, and method-specific entry points.

For the full doc map, see [APP_Guide.md](APP_Guide.md).

---

## Main Entry Points

### Tier-aware auto factorisation

```text
> factor <n>
> factor-bigint <n>
```

`factor` is the normal entry point. It routes by numeric tier:

- `uint64`
- `U128`
- `BigNum`

It also changes behaviour based on whether a DB is loaded.

`factor-bigint` forces the BigNum path.

### Method-specific factorisation

```text
> factor-fermat <n> [--max-steps N]
> factor-rho <n> [--max-iterations N] [--seed N]
> factor-ecm <n> [--B1 N] [--B2 N] [--curves N] [--seed N] [--affine]
> factor-squfof <n>
> factor-qs <n> [--fb N] [--sieve N] [--polys N] [--rels N] [--lp-mult N] [--sp-warmup-polys N] [--siqs-only]
> factor-trial <n> [limit]
> factor-pminus1 <n> [B1]
> factor-pplus1 <n> [B1]
```

ECM note:

- without `--affine`, APP uses the Montgomery/Suyama path
- `--affine` switches to the legacy affine BigNum path with Stage 2 support
- explicit `--B2` tuning is only forwarded to the affine BigNum path; the default Montgomery path derives its own `B2` from `B1`

### Routing and explanation tools

```text
> factor-explain <N>
> difference-bounds <N> [--auto] [--fermat-failed-steps K] [--min-factor-lower-bound L]
```

### Utility factor commands

```text
> smallest-factor <n>
> smoothness-check <n> [bound]
> factor-batch <n1> <n2> ...
```

---

## How `factor` Behaves

### `uint64` inputs

With a DB loaded, APP uses the fast uint64 auto-router. Without a DB, it falls back to methods such as rho and Fermat.

### `U128` inputs

With a DB loaded, APP uses the U128 auto-router. Without a DB, APP prints that it is falling back to BigNum methods.

### BigNum inputs

APP uses the BigNum router and recursive BigNum printing path.

This is why `factor` is usually the right first command: it picks a tier and environment-aware route for you.

---

## When to Use Explicit Methods

Use explicit methods when you want to test an idea rather than let the router choose.

- `factor-fermat`: near-square candidates
- `factor-rho`: general-purpose fallback / randomized splitting
- `factor-ecm`: larger inputs or small hidden factors
- `factor-squfof`: hard `uint64` semiprimes
- `factor-qs`: self-contained sieve-based factoring for `uint64`
- `factor-trial`: explicit DB-backed trial division
- `factor-pminus1`: smoothness-driven DB-backed factoring
- `factor-pplus1`: complementary smoothness-driven DB-backed factoring

---

## DB-Backed vs No-DB Methods

### Usually DB-backed

- `factor-trial`
- `factor-pminus1`
- `factor-pplus1`
- DB-assisted auto-routing

### Usually works without DB

- `factor-fermat`
- `factor-rho`
- `factor-squfof`
- `factor-qs`
- ECM and BigNum fallback paths

If you want the widest method surface, load a DB first.

---

## Routing and Evidence Tools

### `factor-explain`

Use this when you want APP to explain why a method is favoured.

```text
> factor-explain 143
> factor-explain 1000003
```

The output combines:

- cheap observables
- factor-shape intuition
- routing suggestions
- an actual factorisation attempt
- post-hoc geometric interpretation when factors are found

### `difference-bounds`

Use this when you want bounds on factor separation or shape.

```text
> difference-bounds 77 --auto
> difference-bounds 143 --min-factor-lower-bound 11
> difference-bounds 1000003 --fermat-failed-steps 1000
```

This is especially useful when reasoning about near-square structure or ruling out small factors.

---

## Example Sessions

### Basic auto-factoring

```text
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> factor 143
> factor 600851475143
```

### Test a specific method

```text
> factor-rho 8051
> factor-fermat 5959 --max-steps 100000
> factor-pminus1 10403 1000000
```

### Understand routing instead of just the answer

```text
> factor-explain 323
> difference-bounds 323 --auto
```

---

## GNFS Status

```text
> factor-gnfs <n>
```

This command is present for GNFS pipeline validation, but it is not the normal usable APP factoring path and does not provide ordinary factor extraction yet.

---

## Method-Specific Implementation Docs

- ECM: [../Factorisation/ECM/ECM.md](../Factorisation/ECM/ECM.md)
- Fermat: [../Factorisation/Fermat/Fermat.md](../Factorisation/Fermat/Fermat.md)
- GNFS: [../Factorisation/GNFS/GNFS.md](../Factorisation/GNFS/GNFS.md)
- Pollard p-1: [../Factorisation/PMinus1/PMinus1.md](../Factorisation/PMinus1/PMinus1.md)
- Williams p+1: [../Factorisation/PPlus1/PPlus1.md](../Factorisation/PPlus1/PPlus1.md)
- Quadratic Sieve: [../Factorisation/QuadraticSieve/QuadraticSieve.md](../Factorisation/QuadraticSieve/QuadraticSieve.md)
- Pollard rho: [../Factorisation/Rho/Rho.md](../Factorisation/Rho/Rho.md)
- SQUFOF: [../Factorisation/SQUFOF/SQUFOF.md](../Factorisation/SQUFOF/SQUFOF.md)
- Trial division: [../Factorisation/TrialDivision/TrialDivision.md](../Factorisation/TrialDivision/TrialDivision.md)

---

## Related Guides

- Queries: [APP_QueryGuide.md](APP_QueryGuide.md)
- Prime-pair spaces: [APP_SpaceGuide.md](APP_SpaceGuide.md)
- Semiprime analysis: [APP_SemiPrimeGuide.md](APP_SemiPrimeGuide.md)
- Evaluation: [APP_EvaluationGuide.md](APP_EvaluationGuide.md)
- Benchmarking: [APP_BenchmarkGuide.md](APP_BenchmarkGuide.md)
