# APP Database Guide

This guide covers database loading, cache management, and file infrastructure inside APP mode.

For the top-level index, see [APP_Guide.md](APP_Guide.md).

---

## Why the DB Matters

The APP shell can run some arithmetic methods without a DB, but most of its high-value workflows depend on loaded CPSS data:

- prime queries
- prime iteration
- DB-backed factor methods
- prime-pair region fetches
- export pipelines
- DB-aware evaluation and many benchmarks

If you see `No database loaded.`, start here.

---

## Single Stream vs Sharded DB

### Single stream

```text
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --seg-start 0 --seg-end 100 --mode performance
```

Use this when you are working from one stream file rather than a shard directory.
For normal development work, start with `--mode benchmark`.
Keep the segmented `performance` example for machines where you intentionally want the higher-RAM hot-path tradeoff described below.

### Sharded DB

```text
> load-db shards/
> load-db shards/ --seg-start 10 --seg-end 50 --mode benchmark
```

Use this when your data is spread across shard files managed as one database.

---

## Load Modes

Both `load` and `load-db` accept:

- `--mode benchmark`
- `--mode performance`

`benchmark` is the recommended mode for normal development work. `performance` is the code default if `--mode` is omitted.

Actual behavior in `app.c`:

- `benchmark`: keeps the cold-loaded mapped segments available after the hot working set is prepared
- `performance`: drops the cold-loaded mapped view after preload to reduce mapped-file footprint
- for `load-db`, `performance` mode leaves only the hot-cached segment window directly queryable after unmapping

Use `benchmark` for normal development work.
Use `performance` only as a deliberate high-RAM mode when the machine can carry the hotter working set on RAM alone; in practice that means treating it as the specialist option, not the everyday one.

---

## Inspecting Current State

```text
> db-status
> cache-status
```

`db-status` is the first command to run after loading because it tells you:

- whether a DB is open
- how many segments are visible
- the numeric range covered
- cache-related status information

`cache-status` shows wheel-cache and hot-cache budgets, usage, and entry counts.

---

## Preloading and Warming

```text
> preload
> preload 0 100
> preload hot 0 100
> preload index
```

Use preloading when you want to pay setup cost once before a larger run.

Actual meanings:

- `preload [start] [end]`: warm the wheel cache
- `preload hot [start] [end]`: warm the hot segment cache
- `preload index`: build the per-segment prime count index

Budget requirements:

- `preload` needs a nonzero `--wheel-cache` budget
- `preload hot` needs a nonzero `--hot-cache` budget

Common cases:

- warm wheel cache before many small-factor queries
- warm hot segment cache before repeated query/factor workloads
- build prime count index before heavy rank/count work

---

## Resetting Caches

```text
> cache-reset
```

This clears both:

- wheel cache
- hot segment cache

Use it before controlled experiments when you want a cold-ish state.

---

## Startup Options That Affect DB Work

APP startup accepts:

```text
cpss_viewer APP --wheel-cache 256MB --hot-cache 1GB --db-ram 8GB --threads 8
```

Meanings:

- `--wheel-cache`: wheel cache budget
- `--hot-cache`: decompressed hot segment cache budget
- `--db-ram`: RAM ceiling for DB-backed loading
- `--threads`: thread count used by parallel workloads

If you plan to do repeated DB queries or evaluation runs, set these before entering the shell.

---

## Sidecar Index Commands

These commands manage `.cpssi` sidecar indexes.

```text
> build-index <file>
> rebuild-index <file>
> verify-index <file>
> build-index-db <dir>
```

Use them when you need to create or validate auxiliary index structures for a file or shard directory.

---

## Manifest Commands

These commands manage the database manifest.

```text
> build-manifest <dir>
> verify-manifest <dir>
> db-coverage
> db-gaps
> db-overlaps
```

Use them to audit a shard set for continuity, overlap, and declared coverage.

---

## Suggested DB Session

```text
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> db-status
> cache-status
> preload hot 0 50
> preload index
> is-prime 1000003
> factor 143
```

This is a good starting pattern for normal development work with a single stream file.

---

## Related Guides

- Queries: [APP_QueryGuide.md](APP_QueryGuide.md)
- Factorisation: [APP_FactorisationGuide.md](APP_FactorisationGuide.md)
- Export pipelines: [APP_ExportGuide.md](APP_ExportGuide.md)
- Benchmarking: [APP_BenchmarkGuide.md](APP_BenchmarkGuide.md)
