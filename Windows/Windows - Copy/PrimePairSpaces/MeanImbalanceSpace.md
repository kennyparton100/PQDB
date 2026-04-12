# Mean/Imbalance Space (m, delta)

**Axes:** `m = (u + v) / 2`, `delta = (v - u) / 2`
**Coordinates:** Derived from log space `u = log(p)`, `v = log(q)`.
**Inverse:** `p = e^(m - delta)`, `q = e^(m + delta)`

## Purpose

For a fixed `N`, `m` is fixed (`m = log(N) / 2`), so all the uncertainty collapses into `delta`.

**This is probably the most engine-friendly hidden space**, because it reduces the factor-shape question to basically one knob.

## Delta Axis (for fixed N)

```
delta ->
0 ------------------------------------------------------------->

perfectly
balanced      slight imbalance       medium imbalance       very skewed
```

## Routing Picture

```
delta ->
0 -------------------------------------------------------------------->

[Fermat]
    [general methods / fallback]
                 [ECM / rho more plausible]
                               [CPSS / trial division territory]
```

---

Back to [Documentation Index](../Documentation.md)
