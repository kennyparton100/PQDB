# APP SemiPrime Guide

This guide covers the semiprime-analysis side of APP mode: per-`N` reports, routing views, and transitional overlay commands.

For the full guide map, see [APP_Guide.md](APP_Guide.md).

---

## Prime-Pair vs Semiprime Views

Prime-pair spaces operate on explicit factor-pair regions.

Semiprime spaces are different:

- they are **per-input reports**
- they are keyed by a single `N`
- they do **not** use region syntax

That means this is valid:

```text
> space semiprime observation 143
```

and this is not the right model:

```text
> space semiprime observation triangle 4 6
```

For region-based prime-pair work, use [APP_SpaceGuide.md](APP_SpaceGuide.md).

---

## Unified Semiprime Space Commands

```text
> space semiprime observation <N>
> space semiprime coverage <N>
> space semiprime bounds <N> [flags]
> space semiprime routing <N>
```

From the APP help surface, the semiprime family currently exposes:

- `observation`
- `coverage`
- `bounds`
- `routing`

These are structured per-`N` reports rather than cell grids.

Discoverability commands for this family:

```text
> space-list semiprime
> space-metrics semiprime observation
> space-metrics semiprime coverage
> space-metrics semiprime bounds
> space-metrics semiprime routing
```

---

## Standalone Commands for the Same Ideas

APP also exposes standalone commands that overlap with the semiprime reports:

```text
> observable <N>
> coverage-sim <N>
> factor-explain <N>
> difference-bounds <N> [opts]
```

Use these when you want one focused report without going through `space semiprime ...`.

---

## What Each Semiprime View Is For

### `observation`

Use this to inspect cheap signals visible before factorisation succeeds.

Typical questions:

- does `N` look near-square?
- is there wheel-prime evidence?
- what does the engine know cheaply?

Related detailed space doc:

- [../SemiPrimeSpaces/SemiPrimeObservationSpace.md](../SemiPrimeSpaces/SemiPrimeObservationSpace.md)

### `coverage`

Use this to reason about which methods are plausible or supported.

Related detailed space doc:

- [../SemiPrimeSpaces/SemiPrimeCoverageSpace.md](../SemiPrimeSpaces/SemiPrimeCoverageSpace.md)

### `bounds`

Use this when you want factor-difference or factor-shape constraints.

The APP help and dispatcher align this with the `difference-bounds` option family, so typical flags include:

- `--auto`
- `--fermat-failed-steps K`
- `--min-factor-lower-bound L`

This overlaps strongly with:

```text
> difference-bounds <N> [opts]
```

### `routing`

Use this when you want a method-selection explanation rather than just a factorisation result.

This overlaps strongly with:

```text
> factor-explain <N>
```

---

## Transitional Overlay Commands

APP still includes some overlay-style commands while functionality migrates toward the prime-pair and semiprime families.

```text
> residue-classes <M>
> residue-grid <N> <M>
> smoothness <p> [B]
> observable <N>
> coverage-sim <N>
```

These are useful when you want older or more focused views without using the unified `space` interface.

---

## Typical Semiprime Session

```text
> space semiprime observation 143
> space semiprime coverage 143
> space semiprime bounds 143 --auto
> space semiprime routing 143
> factor-explain 143
```

This is the APP-native path from raw observables to routing interpretation.

---

## Evaluation Tie-In

The semiprime reports connect directly to the APP evaluation tools:

- `corpus-generate semiprime`
- `corpus-validate`
- `router-eval`
- `method-compete`
- `metric-eval`

Those commands are covered in [APP_EvaluationGuide.md](APP_EvaluationGuide.md).

---

## Notes on Numeric Tiers

The APP help text explicitly treats semiprime analysis as tier-aware.

You can use these tools with:

- `uint64`
- `U128`
- `BigNum`

That makes semiprime analysis useful both for small practical numbers and for large experimental inputs.

---

## Related Guides

- Prime-pair spaces: [APP_SpaceGuide.md](APP_SpaceGuide.md)
- Factor routing and bounds: [APP_FactorisationGuide.md](APP_FactorisationGuide.md)
- Evaluation: [APP_EvaluationGuide.md](APP_EvaluationGuide.md)
