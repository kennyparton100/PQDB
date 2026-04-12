# Continued Fraction Space

Analyses the continued fraction (CF) expansion of the factor ratio q/p for a semiprime N = p × q.

---

## Purpose

The CF expansion of q/p encodes the "algebraic difficulty" of the factor ratio. Ratios with short expansions or small partial quotients are structurally simpler and more amenable to CF-based factorisation methods (CFRAC, SQUFOF).

## Axes

| Axis | Formula | Meaning |
|------|---------|---------|
| `cf_len` | length of CF expansion [a₀; a₁, a₂, ...] | Number of partial quotients |
| `max_a` | max(aᵢ) | Largest partial quotient |
| `mean_a` | mean(aᵢ for i ≥ 1) | Average partial quotient size |
| `bits/step` | log₂(q/p) / (length - 1) | How many bits each CF step resolves |
| `diff` | length × log₂(max_a + 1) | Composite difficulty score |

## Coordinates

For a factor pair (p, q) with p ≤ q:

```
q/p = [a₀; a₁, a₂, ..., aₖ]
```

The convergents h₀/k₀, h₁/k₁, ... are the best rational approximations at each step.

## Interpretation

- **Short CF with small quotients** → CF-easy; SQUFOF and CFRAC-style methods converge quickly
- **Long CF with large quotients** → CF-hard; the ratio is "irrational-like" and harder to approximate
- **Near-square pairs** (p ≈ q) → CF expansion is `[1; large, ...]` — short but with one big quotient
- **Balanced pairs** → typically moderate length, moderate quotients

## APP Usage

```text
> space cfrac diagonal 4 8
> space cfrac triangle 4 20
> space-compare diagonal 4 8 --spaces cfrac,fermat,lattice
```

## Example Output

```
  (4,5)  7*11=77  cf_len=2  max_a=1  mean_a=1.00  bits/step=0.652  diff=1.00
  (4,6)  7*13=91  cf_len=3  max_a=1  mean_a=1.00  bits/step=0.451  diff=1.58
```

## Related Spaces

- [Fermat Space](FermatSpace.md) — near-square structure from a different angle
- [Lattice Space](LatticeSpace.md) — lattice-reduction difficulty for the same pair
- [Log Space](LogSpace.md) — multiplicative balance in additive coordinates

---

Back to [Documentation Index](../Documentation.md)
