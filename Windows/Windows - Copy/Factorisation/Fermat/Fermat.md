# Fermat Factorisation

This folder contains the extracted Fermat difference-of-squares implementation restored from `Factorisation/factor.c`.

## APP Command Surface

```text
> factor-fermat <n> [--max-steps N]
```

`factor-fermat` does not require a DB.

## Source File

- `fermat.c`: `uint64`, `U128`, and BigNum Fermat implementations

## Best Fit

Fermat is strongest when the factors are close together:

- odd semiprimes near a square
- cases where `p` and `q` are not badly imbalanced
- fallback work after rho misses but the number still looks near-square

## Default Limits

Defaults are centralized in `factor.c`:

- `uint64`: `1048576` steps when `--max-steps` is omitted
- `U128`: `1048576` steps when `--max-steps` is omitted
- BigNum: `65536` steps when `--max-steps` is omitted

## Current Implementation Notes

The implementation:

- strips factors of `2` first
- exits early on prime cofactors
- walks `a^2 - n` upward from `ceil(sqrt(n))`
- checks whether the residual becomes a perfect square
- reports `INCOMPLETE` when the step budget is exhausted

## When Not to Use Fermat

Fermat is a poor first choice when:

- the factors are highly unbalanced
- you want DB-backed smoothness methods
- you expect a tiny hidden factor where ECM or rho is more natural

## Related Documentation

- [Documentation Index](../../Documentation.md)

- APP overview: [../../APP/APP_FactorisationGuide.md](../../APP/APP_FactorisationGuide.md)
- ECM: [../ECM/ECM.md](../ECM/ECM.md)
- Pollard rho: [../Rho/Rho.md](../Rho/Rho.md)
