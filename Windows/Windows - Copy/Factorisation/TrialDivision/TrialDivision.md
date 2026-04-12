# Trial Division

This folder contains the DB-backed streamed trial-division implementation used by APP.

## APP Command Surface

```text
> factor-trial <n> [limit]
```

`factor-trial` requires a loaded DB.

## Source File

- `trial_division.c`: streamed, shard-aware, and `U128` trial-division variants

## Current Behavior

The implementation:

- strips wheel primes first
- streams remaining primes from CPSS through `PrimeIterator` or shard-aware DB access
- stops at `min(limit, sqrt(n), db_hi)`
- reports `INCOMPLETE` when DB coverage ends before the natural limit

## Best Fit

Use explicit trial division when you want:

- deterministic DB-backed coverage up to a chosen bound
- a baseline method for comparisons
- a direct check for small and medium factors with visible DB coverage semantics

## Limits

- no DB means no trial run
- the requested limit is clipped to available DB coverage
- very large limits are still fundamentally trial division

## Related Documentation

- [Documentation Index](../../Documentation.md)

- APP overview: [../../APP/APP_FactorisationGuide.md](../../APP/APP_FactorisationGuide.md)
- Pollard p-1: [../PMinus1/PMinus1.md](../PMinus1/PMinus1.md)
- Williams p+1: [../PPlus1/PPlus1.md](../PPlus1/PPlus1.md)
