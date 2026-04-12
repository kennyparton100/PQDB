# Williams p+1

This folder contains the DB-backed Williams p+1 implementation used by APP.

## APP Command Surface

```text
> factor-pplus1 <n> [B1]
```

`factor-pplus1` requires a loaded DB because it streams primes from CPSS.

## Source File

- `pplus1.c`: streamed, shard-aware, `U128`, and BigNum p+1 implementations

## Default Bounds

When `B1` is omitted:

- APP defaults to `1000000`
- the effective bound is clipped to DB coverage
- Stage 2 derives `B2 = min(B1 * 100, 10000000, db_hi)`

## Best Fit

p+1 is a complementary smoothness method for factors `p` where `p + 1` is smooth enough.

Typical use cases:

- cases where p-1 is a poor fit but a Lucas-sequence approach may work
- DB-backed method comparisons
- follow-up experiments after routing analysis

## Current Implementation Notes

The implementation:

- uses Lucas chains rather than ordinary modular powering
- tries several starting values in the `uint64` and `U128` paths
- clips work to current DB coverage
- labels Stage 1 and Stage 2 hits as `pplus1(stage1)` and `pplus1(stage2)`

## Limitations

- no DB means no p+1 run
- misses are normal when `p + 1` is not smooth enough relative to `B1`
- DB coverage limits the usable bound

## Related Documentation

- [Documentation Index](../../Documentation.md)

- APP overview: [../../APP/APP_FactorisationGuide.md](../../APP/APP_FactorisationGuide.md)
- Pollard p-1: [../PMinus1/PMinus1.md](../PMinus1/PMinus1.md)
- Trial division: [../TrialDivision/TrialDivision.md](../TrialDivision/TrialDivision.md)
