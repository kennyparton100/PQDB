# Pollard Rho

This folder contains the extracted Pollard rho implementation used by APP for explicit rho runs and no-DB fallback work.

## APP Command Surface

```text
> factor-rho <n> [--max-iterations N] [--seed N]
```

`factor-rho` does not require a DB.

## Source File

- `rho.c`: `uint64` recursive rho, BigNum rho splitting, and native `U128` rho support

## Default Parameters

Defaults are centralized in `factor.c`:

- `max_iterations`: `131072`
- `seed`: `0x9E3779B97F4A7C15`

## Best Fit

Rho is the general-purpose explicit splitter for:

- odd composites without obvious near-square structure
- no-DB fallback runs
- cases where you want a quick randomized split before heavier methods

## Current Implementation Notes

The implementation:

- strips wheel-prime factors first
- recursively splits `uint64` composites until prime leaves remain
- uses randomized `x^2 + c mod n` walks
- marks `INCOMPLETE` when the iteration budget misses
- has BigNum and native `U128` paths for larger inputs

## Practical Use

Rho is usually the first explicit no-DB method to try when:

- Fermat is a poor geometric fit
- you do not want DB-backed methods
- you want a cheap probabilistic split before ECM

## Related Documentation

- [Documentation Index](../../Documentation.md)
- APP overview: [../../APP/APP_FactorisationGuide.md](../../APP/APP_FactorisationGuide.md)
- Fermat: [../Fermat/Fermat.md](../Fermat/Fermat.md)
- ECM: [../ECM/ECM.md](../ECM/ECM.md)
