# APP Query Guide

This guide covers the prime-query side of APP mode, plus the bridge into NQL.

For the guide index, see [APP_Guide.md](APP_Guide.md).

---

## Query Modes

APP gives you two ways to ask questions about numbers:

- direct shell commands such as `is-prime`, `pi`, and `prime-iter`
- raw NQL statements via `query ...`

Use direct commands when you want a focused built-in operation. Use NQL when you want richer selection, filtering, expressions, or mixed output.

---

## Commands in This Area

### Direct query commands

```text
is-prime <n>
is-prime-bigint <n> [--rounds N]
next-prime <n>
prev-prime <n>
pi <n>
nth-prime <k>
prime-gap <n>
count-range <lo> <hi>
list-range <lo> <hi> [limit]
primes-near <n> [radius]
nearest <n> <k>
range-stats <lo> <hi>
prime-iter <start> [count]
prime-iter-prev <start> [count]
classify <n>
support-coverage <n>
segment-info <n>
is-prime-batch <n1> <n2> ...
```

### NQL entry point

```text
query <NQL statement>
```

---

## Typical Query Session

```text
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> is-prime 1000003
> next-prime 1000000
> pi 1000000
> range-stats 1 100000
> prime-iter 1000 20
```

Most direct query commands require a loaded DB.

The main exception is:

```text
> is-prime-bigint 340282366920938463463374607431768211507 --rounds 20
```

That command is a pure probable-prime test and does not depend on the DB.

---

## Prime Lookup Commands

Use these when you need local primality or neighborhood information.

```text
> is-prime 97
> next-prime 100
> prev-prime 100
> prime-gap 1009
> primes-near 1000 5
> nearest 1000 10
```

Best use cases:

- validate a candidate
- find nearby primes
- inspect local prime spacing

---

## Counting and Range Commands

Use these for aggregate or slice-style work.

```text
> pi 1000000
> count-range 1 1000000
> list-range 1000 1100
> range-stats 1 1000000
```

Use `list-range` when you need the actual primes. Use `count-range` or `pi` when you only need totals.

---

## Iteration Commands

Use these when you want streaming-style traversal.

```text
> prime-iter 1000 25
> prime-iter-prev 1000 25
```

These are useful for quick manual scans and debugging exact prime neighborhoods.

---

## Classification and Coverage Commands

```text
> classify 143
> support-coverage 999983
> segment-info 1000003
```

What they are for:

- `classify`: composite classification / structural summary
- `support-coverage`: whether the DB can support the requested work cleanly
- `segment-info`: where a value lives inside the loaded segment layout

---

## Batch Querying

For file-based pipelines, use the export/batch guide:

- [APP_ExportGuide.md](APP_ExportGuide.md)

That guide covers:

- `query-file`
- input/output formats
- batch result generation

---

## NQL from APP Mode

Run NQL by prefixing the statement with `query`.

```text
> query SELECT p FROM PRIME WHERE p BETWEEN 1 AND 50
> query SELECT COUNT(*) FROM PRIME WHERE p <= 1000000
> query SELECT n, FACTORISE(n) FROM N WHERE n BETWEEN 1 AND 20
```

Inside APP mode, `query` is handled before normal tokenization so the statement is passed through as raw NQL source.

Use NQL when you need:

- computed columns
- filtering and aggregation
- number-type iteration
- expression-heavy output

Detailed NQL docs already exist here:

- [../NQL/NQL_UserGuide.md](../NQL/NQL_UserGuide.md)
- [../NQL/NQL_Documentation.md](../NQL/NQL_Documentation.md)

---

## Choosing Between Direct Commands and NQL

Use direct commands when:

- you want one built-in answer fast
- the command already exists exactly as needed
- you are exploring from the shell interactively

Use NQL when:

- you want multiple columns or computed output
- you need filtering across a generated number set
- you want a reusable query workflow

---

## Related Guides

- Startup: [APP_GettingStarted.md](APP_GettingStarted.md)
- Database loading: [APP_DatabaseGuide.md](APP_DatabaseGuide.md)
- Factorisation: [APP_FactorisationGuide.md](APP_FactorisationGuide.md)
- Export pipelines: [APP_ExportGuide.md](APP_ExportGuide.md)
