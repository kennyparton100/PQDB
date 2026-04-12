# Lattice Space

Analyses the 2D factoring lattice for a semiprime N = p × q using Gauss reduction.

---

## Purpose

The factoring problem for N can be cast as a shortest-vector problem in a 2D lattice. The quality of the lattice basis after Gauss reduction — orthogonality defect, shortest vector length, and basis angle — predicts how amenable N is to lattice-based attacks and provides geometric intuition about the factor pair.

## Axes

| Axis | Formula | Meaning |
|------|---------|---------|
| `sv` | ‖shortest reduced vector‖ | Norm of shortest non-trivial lattice vector |
| `orth` | (‖v₁‖ × ‖v₂‖) / |det| | Orthogonality defect (1.0 = perfect) |
| `angle` | arccos(v₁·v₂ / ‖v₁‖‖v₂‖) | Angle between reduced basis vectors (radians) |
| `gauss` | y/n | Whether the original basis was already Gauss-reduced |
| `diff` | log₂(sv) × orth | Composite difficulty score |

## Lattice Construction

For N = p × q with p ≤ q, the natural factoring lattice uses basis:

```
v₁ = (1, q)
v₂ = (1, -p)
```

After 2D Gauss reduction, the shortest vector reveals structural information about the factor pair.

## Interpretation

- **Small shortest vector** (relative to √N) → lattice-easy; factors are "close" in lattice terms
- **Orthogonality defect near 1.0** → basis is nearly orthogonal; lattice is well-conditioned
- **Large angle** (near π/2) → balanced lattice; factors are structurally independent
- **Already Gauss-reduced** → the factor pair has a naturally clean lattice structure

## APP Usage

```text
> space lattice diagonal 4 8
> space lattice triangle 4 20
> space-compare triangle 4 6 --spaces lattice,cfrac,fermat
```

## Example Output

```
  (4,5)  7*11=77  sv=2.24  orth=1.0025  angle=1.5483  gauss=n  diff=2.35
  (4,6)  7*13=91  sv=2.83  orth=1.0041  angle=1.5291  gauss=n  diff=3.01
```

## Related Spaces

- [CFrac Space](CFracSpace.md) — continued fraction difficulty for the same pair
- [Fermat Space](FermatSpace.md) — near-square structure via (A, B) coordinates
- [Value Space](ValueSpace.md) — raw factor geometry

---

Back to [Documentation Index](../Documentation.md)
