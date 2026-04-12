# ECM Factorisation

This folder contains the APP elliptic-curve method implementations extracted from `Factorisation/factor.c`.

## APP Command Surface

```text
> factor-ecm <n> [--B1 N] [--B2 N] [--curves N] [--seed N] [--affine]
```

Implemented routing in `app.c`:

- default path uses the Montgomery/Suyama implementation
- `--affine` switches to the legacy affine BigNum implementation
- no DB is required

## Files in This Folder

- `ecm.c`: include aggregator for the ECM implementation files
- `ecm_affine.c`: affine Weierstrass BigNum ECM with Stage 1 and Stage 2 baby-step/giant-step work
- `ecm_mont.c`: Montgomery x-only ECM used by the default APP path for `uint64`, `U128`, and default BigNum runs

## Default Parameters

Defaults are centralized in `factor.c`:

- `B1`: `5000`
- `curves`: `8`
- `seed`: `0xD1B54A32D192ED03`
- auto `B2`: `min(B1 * 100, 500000)` when the implementation receives `B2 = 0`

## Important Current Behavior

- APP parses `--B2` for `factor-ecm`
- the default Montgomery path does **not** forward the explicit `B2` argument from `app.c`
- the affine path does receive the explicit `B2` value
- the default Montgomery path therefore derives `B2` internally from `B1`

This means `--B2` is currently most meaningful when you also pass `--affine`.

## When to Use ECM

ECM is the right explicit method when you expect:

- a relatively small hidden factor inside a larger composite
- a case that is not close enough to square for Fermat
- a case where smoothness methods are too specialized and rho is stalling
- larger `U128` or BigNum inputs where a probabilistic curve search is still practical

## Current Implementation Notes

### Affine path

The affine implementation:

- works in BigNum arithmetic
- uses affine Weierstrass coordinates
- performs modular inversions during point arithmetic
- includes an explicit Stage 2 baby-step/giant-step search

### Montgomery path

The Montgomery implementation:

- uses Suyama parametrization
- uses x-only projective arithmetic
- avoids inversions during ladder work
- is the default APP ECM path

## Limitations

- ECM is still probabilistic; `INCOMPLETE` is a normal miss state
- affine ECM is not intended to be the fast default path
- the default APP dispatcher currently exposes explicit `B2` control only through the affine path

## Related Documentation

- [Documentation Index](../../Documentation.md)

- APP overview: [../../APP/APP_FactorisationGuide.md](../../APP/APP_FactorisationGuide.md)
- Fermat: [../Fermat/Fermat.md](../Fermat/Fermat.md)
- Pollard rho: [../Rho/Rho.md](../Rho/Rho.md)
