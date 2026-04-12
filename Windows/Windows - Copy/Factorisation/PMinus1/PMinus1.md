# Pollard p-1

This folder contains the DB-backed Pollard p-1 implementation used by APP.

## APP Command Surface

```text
> factor-pminus1 <n> [B1]
```

`factor-pminus1` requires a loaded DB because it streams primes from CPSS.

## Source File

- `pminus1.c`: streamed, shard-aware, `U128`, and BigNum p-1 implementations

## Default Bounds

When `B1` is omitted:

- APP defaults to `1000000`
- the effective bound is clipped to DB coverage
- Stage 2 derives `B2 = min(B1 * 100, 10000000, db_hi)`

## Best Fit

p-1 is strongest when a factor `p` has a smooth `p - 1` structure.

Typical use cases:

- semiprimes with unusually smooth predecessor structure
- DB-backed experiments where you want a targeted smoothness method
- auto-router follow-up analysis

## Current Implementation Notes

The implementation:

- uses CPSS prime iteration for Stage 1 prime powers
- clips work to current DB coverage
- reports `INCOMPLETE` when the DB cannot support the full requested smoothness bound
- exposes Stage 1 and Stage 2 success via method labels such as `pminus1(stage1)`

## Limitations

- no DB means no p-1 run
- misses are normal when `p - 1` is not smooth enough relative to `B1`
- DB coverage limits the usable bound

## Related Documentation

- [Documentation Index](../../Documentation.md)

- APP overview: [../../APP/APP_FactorisationGuide.md](../../APP/APP_FactorisationGuide.md)
- Williams p+1: [../PPlus1/PPlus1.md](../PPlus1/PPlus1.md)
- Trial division: [../TrialDivision/TrialDivision.md](../TrialDivision/TrialDivision.md)
