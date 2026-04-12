# SQUFOF

This folder contains the APP Shanks square forms factorisation implementation.

## APP Command Surface

```text
> factor-squfof <n>
```

`factor-squfof` does not require a DB.

## Source File

- `squfof.c`: `uint64` SQUFOF with multiple multipliers and recursive split handling

## Best Fit

SQUFOF is aimed at hard `uint64` semiprimes, especially when:

- rho is not splitting quickly
- the number is not an obvious Fermat case
- you want a deterministic arithmetic-only method in the `uint64` range

## Current Implementation Notes

The implementation:

- tries a fixed multiplier set to improve success rate
- uses continued-fraction square form cycling
- strips powers of `2` first
- exits early on prime cofactors and perfect squares
- recursively re-factors non-prime splits within the same method

## Limits

- this is a `uint64` method, not the general BigNum path
- misses return `INCOMPLETE`

## Related Documentation

- [Documentation Index](../../Documentation.md)

- APP overview: [../../APP/APP_FactorisationGuide.md](../../APP/APP_FactorisationGuide.md)
- Pollard rho: [../Rho/Rho.md](../Rho/Rho.md)
- Quadratic Sieve: [../QuadraticSieve/QuadraticSieve.md](../QuadraticSieve/QuadraticSieve.md)
