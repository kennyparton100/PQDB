# Product/Ratio Space (N, R)

**Axes:** `N = p * q`, `R = l / s >= 1`
**Coordinates:** `(N, R)` where `l = max(p,q)`, `s = min(p,q)`.

## Purpose

For a fixed input `N`, the product is already known, so the unknown is basically just the ratio `R`.

This is one of the best routing views because it asks the right question:
**How imbalanced are the factors likely to be?**

## R Axis (for fixed N)

```
balanced                                             skewed
R=1 ----------------------------------------------------->

|---- Fermat zone ----|------ middle ------|--- small-factor territory ---|
```

## Examples

| N   | Factorization | R = l/s        |
|-----|---------------|----------------|
| 49  | 7 * 7         | 1              |
| 77  | 7 * 11        | 11/7 ~ 1.57   |
| 26  | 2 * 13        | 6.5            |
