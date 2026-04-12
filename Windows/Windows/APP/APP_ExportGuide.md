# APP Export Guide

This guide covers CSV export and file-based batch pipelines inside APP mode.

For the full doc map, see [APP_Guide.md](APP_Guide.md).

---

## Export Workflows in APP

APP has two main export styles:

- **region export** from prime-pair spaces
- **batch pipelines** from input files

Use region export when you want structured space data. Use batch pipelines when you want to run the same query or factorisation over many inputs.

---

## `space-export`

Use `space-export` to write a prime-pair region to CSV.

```text
> space-export <space> <region> <args...> <outfile>
```

Examples:

```text
> space-export value triangle 4 20 values.csv
> space-export fermat band 1 4 30 fermat.csv
> space-export minmax rect-unique 4 20 4 20 coverage.csv
```

### What it exports

The APP help describes this as:

- one row per cell
- standard columns like `i`, `j`, `p`, `q`, `N`
- then space-specific derived columns

### Important limitation

`space-export` is for **prime-pair** spaces.

Semiprime spaces are per-`N` reports, so APP explicitly rejects using `space-export` with the semiprime family.

Use this instead for semiprime work:

```text
> space semiprime <name> <N> [flags]
```

---

## Batch Query Pipelines

Use `query-file` when you have many inputs and want APP to write batch query results.

```text
> query-file <command> <input> <output> [--input-format auto|txt|csv] [--output-format csv|jsonl]
```

`query-file` requires a loaded DB in APP mode.

Supported subcommands:

- `is-prime`
- `next-prime`
- `prev-prime`
- `classify`
- `count-range`

Examples:

```text
> query-file is-prime inputs.txt primality.csv
> query-file classify numbers.csv classes.jsonl --input-format csv --output-format jsonl
```

Use this when you already have a file of numbers or ranges and want APP to produce a machine-readable result set.

---

## Batch Factor Pipelines

Use `factor-file` when you want batch factorisation output.

```text
> factor-file <input> <output> [--input-format auto|txt|csv] [--output-format csv|jsonl]
```

Example:

```text
> factor-file inputs.txt factors.csv
> factor-file semiprimes.csv factors.jsonl --input-format csv --output-format jsonl
```

`factor-file` requires a loaded DB in APP mode.

---

## Choosing Output Formats

The implemented batch options are:

- input: `auto`, `txt`, `csv`
- output: `csv`, `jsonl`

General guidance:

- use `csv` when you want spreadsheet-friendly output
- use `jsonl` when you want one structured result object per line for downstream tooling

---

## Typical Export Session

```text
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> space-export value triangle 4 20 values.csv
> query-file is-prime inputs.txt primality.csv
> factor-file semiprimes.txt factors.jsonl --output-format jsonl
```

This sequence covers:

- structural region export
- batch querying
- batch factorisation

---

## Related Guides

- Space navigation: [APP_SpaceGuide.md](APP_SpaceGuide.md)
- Queries: [APP_QueryGuide.md](APP_QueryGuide.md)
- Factorisation: [APP_FactorisationGuide.md](APP_FactorisationGuide.md)
- Evaluation tooling: [APP_EvaluationGuide.md](APP_EvaluationGuide.md)
