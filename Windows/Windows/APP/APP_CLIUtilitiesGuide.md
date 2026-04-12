# APP CLI Utilities Guide

These commands live in the top-level `cpss_viewer` interface and are implemented in `APP/cli.c`. They are useful when you want a single action without entering interactive APP mode.

Examples below use `<stream-file>` for the input stream path. A common local single-stream example is `C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin`.

For the full APP doc map, see [APP_Guide.md](APP_Guide.md).

---

## Command Surface

```text
cpss_viewer info <file> [--deep] [--max-deep-segments N]
cpss_viewer list <file> [--limit N]
cpss_viewer export-primes <file> <lo> <hi> <out> [--limit N]
cpss_viewer clip <file> <lo> <hi> <out> [--force]
cpss_viewer split-trillion <file> <out_dir> [--trillion N] [--start-bucket N] [--end-bucket N] [--force]
cpss_viewer benchmark <file> <lo> <hi> [--limit N] [--include-write] [--mode hot|fair|auto]
cpss_viewer decompress-file <input> <output>
cpss_viewer compress-file <input> <output>
```

---

## `info`

Use `info` to print a stream summary and optional validation details.

```text
cpss_viewer info <stream-file>
cpss_viewer info <stream-file> --deep --max-deep-segments 100
```

What it reports:

- wheel and presieve configuration
- segment counts
- candidate / survivor / prime totals
- last candidate/index seen
- raw vs compressed totals
- compression ratio

---

## `list`

Use `list` to inspect segment-by-segment metadata.

```text
cpss_viewer list <stream-file>
cpss_viewer list <stream-file> --limit 50
```

This is the quickest way to see segment ranges, candidate counts, survivor counts, and on-disk sizes.

---

## `export-primes`

Use `export-primes` to decode primes from a numeric range to a text file.

```text
cpss_viewer export-primes <stream-file> 1 100000 primes.txt
cpss_viewer export-primes <stream-file> 1 100000 primes.txt --limit 1000
```

---

## `clip`

Use `clip` to create a new CPSS file restricted to `[lo, hi]`.

```text
cpss_viewer clip <stream-file> 1 100000 clipped.bin
cpss_viewer clip <stream-file> 1 100000 clipped.bin --force
```

---

## `split-trillion`

Use `split-trillion` to partition a stream into bucketed output files.

```text
cpss_viewer split-trillion <stream-file> out_dir
cpss_viewer split-trillion <stream-file> out_dir --trillion 2 --start-bucket 3 --end-bucket 7
```

Useful options:

- `--trillion <N>`
- `--start-bucket <N>`
- `--end-bucket <N>`
- `--force`

---

## `benchmark`

This is the non-APP benchmark helper for a file/range workload.

```text
cpss_viewer benchmark <stream-file> 1 1000000
cpss_viewer benchmark <stream-file> 1 1000000 --limit 5000 --mode hot
```

Modes:

- `hot`
- `fair`
- `auto`

For the larger APP benchmark suite, use [APP_BenchmarkGuide.md](APP_BenchmarkGuide.md).

---

## `decompress-file` and `compress-file`

These convert between compressed V4 CPSS files and raw CPSR files.

```text
cpss_viewer decompress-file input.cpss output.cpsr
cpss_viewer compress-file input.cpsr output.cpss
```

Use these when you need format conversion rather than query execution.

---

## When to Use CLI Instead of APP Mode

Prefer CLI utilities when:

- you only need one file operation
- you want a scriptable one-liner
- you do not need a persistent loaded DB or interactive workflow

Prefer APP mode when:

- you want repeated queries or factorisations
- you want to inspect spaces and semiprime reports interactively
- you want NQL, export pipelines, evaluation, or APP benchmarking

See [APP_GettingStarted.md](APP_GettingStarted.md) if you need the interactive shell instead.
