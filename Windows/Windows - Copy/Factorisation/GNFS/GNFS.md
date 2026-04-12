# GNFS

This folder contains the current GNFS pipeline implementation used for APP experimentation and validation.

## APP Command Surface

```text
> factor-gnfs <n>
```

## Current Status

The implementation is explicitly a milestone-stage pipeline rather than a normal production factor extractor.

From `gnfs_entry.c`:

- implementation status: `Milestone 1`
- pipeline stages present: polynomial selection, sieve, filter, dense linear algebra scaffolding
- square-root / final ordinary factor extraction is not the normal completed path yet

## Source Layout

This folder contains the GNFS submodules, including:

- polynomial selection
- field/algebra helpers
- pipeline entry
- synthetic/audit utilities
- unit and type support

## Practical Meaning in APP

Use `factor-gnfs` when you want:

- GNFS pipeline validation
- configuration inspection
- milestone testing of the current GNFS work

Do **not** treat it as the normal APP factoring command for routine extraction.

## Related Documentation

- [Documentation Index](../../Documentation.md)

- APP overview: [../../APP/APP_FactorisationGuide.md](../../APP/APP_FactorisationGuide.md)
- Quadratic Sieve: [../QuadraticSieve/QuadraticSieve.md](../QuadraticSieve/QuadraticSieve.md)
