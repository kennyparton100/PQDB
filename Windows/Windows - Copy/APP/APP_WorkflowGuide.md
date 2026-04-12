# APP Workflow Guide

This guide turns the APP help system’s recommended workflow into a reusable sequence you can follow during normal exploration.

For the guide index, see [APP_Guide.md](APP_Guide.md).

---

## Recommended Exploration Order

The APP help text presents a practical order:

1. load a database
2. explore a region across spaces
3. inspect aggregate statistics
4. explain factorisation routing
5. export data when needed
6. use discovery commands to branch out

This guide expands that into a working session template.

---

## Workflow 1: First Interactive Exploration Session

```text
cpss_viewer APP --hot-cache 512MB
```

```text
> help
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> db-status
> space-list
> space-regions
> space-metrics value
```

Goal:

- get the shell running
- confirm the DB is loaded
- see what spaces and region types are available

---

## Workflow 2: Explore One Region Through Multiple Views

Start with one region and render it through several spaces.

```text
> space index triangle 4 6
> space-compare triangle 4 6 --spaces value,fermat,minmax
```

Why this works well:

- `index` shows the structural cell layout
- `value` shows literal factor geometry
- `fermat` shows near-square structure
- `minmax` shows small-factor coverage perspective

If you are not sure which space to choose, begin with `space-compare`.

---

## Workflow 3: Move from Views to Statistics

Once a region looks interesting, ask for aggregates.

```text
> space-stats value triangle 4 20
> space-stats-compare diagonal 4 50 --spaces value,log,product-ratio
```

Use this phase when you want:

- counts
- min/max ranges
- mean behaviour
- comparisons across geometric interpretations

---

## Workflow 4: Understand a Specific Number

Switch from region exploration to per-`N` reasoning.

```text
> factor-explain 143
> difference-bounds 143 --auto
> space semiprime observation 143
> space semiprime routing 143
```

This is the best route when your question is:

- why did the router choose this method?
- how balanced or skewed is this semiprime?
- what evidence does the engine have before full factorisation?

---

## Workflow 5: Validate with Direct Queries or Factorisation

After a space/routing hypothesis, confirm it directly.

```text
> is-prime 1000003
> factor 143
> factor-rho 8051
> factor-pminus1 10403 1000000
```

Use this phase when you want the actual arithmetic answer after interpretive exploration.

---

## Workflow 6: Export for External Analysis

When a region or experiment becomes interesting, export it.

```text
> space-export value triangle 4 20 values.csv
> query-file classify inputs.txt classes.csv
> factor-file semiprimes.txt factors.jsonl --output-format jsonl
```

This is the right point to move into notebooks, spreadsheets, or downstream scripts.

---

## Workflow 7: Scale Up to Evaluation

When individual examples are not enough, move to corpus-driven work.

```text
> corpus-generate semiprime data.csv --cases 50 --bits 32 --shape mixed
> corpus-validate data.csv
> router-eval --cases 50 --bits 32 --shape mixed --csv-out router.csv
> method-compete --cases 50 --bits 32 --shape mixed --csv-out compete.csv
```

This shifts the question from:

- “what happened on this one `N`?”

to:

- “what happens across a shaped distribution of inputs?”

---

## Workflow 8: Benchmark the Environment

When performance matters, benchmark separately from evaluation.

```text
> benchmark-is-prime --count 10000 --mode hot
> benchmark-everything --count 50 --bits 32 --mode cold
> benchmark-all-sectors --mode auto
```

Use benchmarking when you care about throughput or cache behaviour, not routing quality.

---

## Short Workflow Recipes

### “I just loaded a DB and want to explore.”

```text
> db-status
> space-list
> space value diagonal 4 8
```

### “I want to understand one semiprime.”

```text
> factor-explain 323
> difference-bounds 323 --auto
> space semiprime routing 323
```

### “I want data I can plot elsewhere.”

```text
> space-export fermat triangle 4 20 fermat.csv
```

### “I want many cases, not one.”

```text
> corpus-generate semiprime data.csv --cases 100 --bits 32 --shape mixed
> router-eval-file data.csv
```

---

## Related Guides

- Startup: [APP_GettingStarted.md](APP_GettingStarted.md)
- Prime-pair spaces: [APP_SpaceGuide.md](APP_SpaceGuide.md)
- Semiprime analysis: [APP_SemiPrimeGuide.md](APP_SemiPrimeGuide.md)
- Export pipelines: [APP_ExportGuide.md](APP_ExportGuide.md)
- Evaluation: [APP_EvaluationGuide.md](APP_EvaluationGuide.md)
- Benchmarking: [APP_BenchmarkGuide.md](APP_BenchmarkGuide.md)
