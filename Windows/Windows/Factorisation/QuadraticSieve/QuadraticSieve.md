# Quadratic Sieve

This folder contains the APP quadratic sieve implementation.

## APP Command Surface

```text
> factor-qs <n> [--fb N] [--sieve N] [--polys N] [--rels N] [--lp-mult N] [--sp-warmup-polys N] [--siqs-only]
```

`factor-qs` does not require a DB.

## Source File

- `qs.c`: multi-polynomial QS and SIQS support for `uint64`, with related U128/BigNum bridging in the wider factorisation layer

## Default Configuration

Defaults are chosen by input bit length through `qs_config_default_for_bits()`.

That function sets heuristics for:

- factor-base size
- sieve interval size
- polynomial budget
- relation budget behavior
- large-prime multiplier
- warmup before SIQS mode

## Current Implementation Notes

The implementation includes:

- small-input shifted-center multi-polynomial QS
- larger-input SIQS-style polynomial generation
- single-large-prime variation support
- GF(2) matrix elimination for dependency finding

## When to Use QS

QS is useful when:

- you want a self-contained sieve-based explicit method
- the input is still within the practical scope of the current QS code
- rho / Fermat / smoothness methods are not a good fit

## Related Documentation

- [Documentation Index](../../Documentation.md)

- APP overview: [../../APP/APP_FactorisationGuide.md](../../APP/APP_FactorisationGuide.md)
- SQUFOF: [../SQUFOF/SQUFOF.md](../SQUFOF/SQUFOF.md)
- GNFS: [../GNFS/GNFS.md](../GNFS/GNFS.md)
