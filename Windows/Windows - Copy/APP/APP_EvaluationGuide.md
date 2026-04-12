# APP Evaluation Guide

This guide covers corpus generation, validation, routing evaluation, metric summaries, and method competition inside APP mode.

For the guide index, see [APP_Guide.md](APP_Guide.md).

---

## What This Area Is For

Use the evaluation tooling when you want to:

- generate semiprime datasets
- validate corpus labels against arithmetic truth
- compare predicted vs actual routing
- compare method performance across generated cases
- summarise metric behaviour by group

This is the APP area for experiments, model/routing analysis, and offline evaluation workflows.

---

## Command Surface

```text
> corpus-generate semiprime <outfile> [--cases N] [--bits B] [--seed S] [--shape ...]
> corpus-validate <input.csv> [--B1 N]
> router-eval [--cases N] [--bits B] [--seed S] [--shape ...] [--csv-out <file>]
> router-eval-file <input.csv> [--limit N] [--csv-out <file>]
> metric-eval [--cases N] [--bits B] [--seed S] [--shape ...] [--group-by ...]
> method-compete [--cases N] [--bits B] [--seed S] [--shape ...] [--B1 N] [--trial-limit N] [--method-timeout N] [--csv-out <file>]
```

---

## `corpus-generate semiprime`

Use this to generate semiprime corpora as CSV.

```text
> corpus-generate semiprime data.csv --cases 50 --bits 16 --seed 42 --shape balanced
```

Common options:

- `--cases <N>`
- `--bits <B>`
- `--seed <S>`
- `--shape <name>`

Shapes exposed by APP help include:

- `balanced`
- `skewed`
- `banded`
- `mixed`
- `rsa-like`
- `no-wheel-factor`
- `pminus1-smooth`
- `pminus1-hostile`

`no-wheel-factor`, `pminus1-smooth`, and `pminus1-hostile` are uint64-only corpus shapes in the APP help text.

When `--bits > 62`, APP switches to the BigNum corpus generator.

---

## `corpus-validate`

Use this to validate generated or existing corpus files.

```text
> corpus-validate data.csv
> corpus-validate data.csv --B1 50
```

This command checks arithmetic properties against method-bound assumptions such as `p-1`, `q-1`, `p+1`, and `q+1` smoothness relative to `B1`.

Important note: APP explicitly requires a loaded DB for `corpus-validate` because it needs prime iteration support.

---

## `router-eval`

Use this to compare routing predictions with actual winners over generated semiprimes.

```text
> router-eval --cases 20 --bits 16 --seed 42 --shape rsa-like
> router-eval --cases 100 --bits 32 --csv-out router_eval.csv
```

This reports concepts such as:

- `ideal`
- `available`
- `actual`

When `--bits > 62`, APP prints a note and uses the BigNum `method-compete` path instead of the normal predictor evaluation.

`router-eval` can still run without a loaded DB, but the current DB state changes what counts as `available`, so DB-loaded runs are the right choice when you want environment-aware validation.

---

## `router-eval-file`

Use this when you already have a corpus CSV.

```text
> router-eval-file input.csv
> router-eval-file input.csv --limit 100 --csv-out eval.csv
```

This is the easiest way to re-score a previously generated dataset without regenerating it.

---

## `metric-eval`

Use this for grouped metric summaries.

```text
> metric-eval --cases 20 --shape no-wheel-factor --group-by corpus
```

Supported grouping modes from the implementation:

- `corpus`
- `observed`
- `shape` (compat alias for `observed`)
- `winner`
- `ideal`
- `available`

Important note: APP currently warns that `metric-eval` is a `uint64`-range tool. For `--bits > 62`, it tells you to use `method-compete` instead.

---

## `method-compete`

Use this when you want direct method-vs-method competition rather than routing prediction.

```text
> method-compete --cases 20 --bits 32 --shape mixed --B1 1000000 --trial-limit 100000 --method-timeout 5
> method-compete --cases 20 --bits 128 --csv-out compete.csv
```

Useful options from the implementation:

- `--cases <N>`
- `--bits <B>`
- `--seed <S>`
- `--shape <name>`
- `--B1 <N>`
- `--trial-limit <N>`
- `--method-timeout <N>`
- `--csv-out <file>`

For `--bits > 62`, APP switches to the BigNum competition path.

Unlike `corpus-validate`, `method-compete` can run with no DB loaded, but DB-backed methods are only available when a DB is present.

---

## Recommended Evaluation Workflow

```text
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> corpus-generate semiprime data.csv --cases 50 --bits 32 --shape mixed
> corpus-validate data.csv
> router-eval --cases 50 --bits 32 --shape mixed --csv-out router.csv
> metric-eval --cases 50 --bits 32 --shape mixed --group-by winner
> method-compete --cases 50 --bits 32 --shape mixed --csv-out compete.csv
```

This gives you generation, truth checking, router scoring, metric grouping, and direct competition in one loop.

---

## Related Guides

- Semiprime analysis: [APP_SemiPrimeGuide.md](APP_SemiPrimeGuide.md)
- Factor routing and bounds: [APP_FactorisationGuide.md](APP_FactorisationGuide.md)
- Benchmarking: [APP_BenchmarkGuide.md](APP_BenchmarkGuide.md)
