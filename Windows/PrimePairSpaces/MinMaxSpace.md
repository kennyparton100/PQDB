# Min/Max Factor Space (s, l)

**Axes:** `s = min(p, q)`, `l = max(p, q)`
**Coordinates:** `(s, l)` with `s <= l`.
**Product:** `N = s * l`

## Purpose

Only the upper triangle exists. Every semiprime appears exactly once.

- Clean representation for small-factor coverage
- Natural for region elimination

## Example (primes 2, 3, 5, 7, 11, 13)

```
          l ->
          2    3    5    7    11   13
s v
2         4    6   10   14    22   26
3              9   15   21    33   39
5                  25   35    55   65
7                       49    77   91
11                            121  143
13                                 169
```

## Small-Factor Coverage Visualization

Suppose CPSS / trial division covers `s <= 7`:

```
          l ->
          2    3    5    7    11   13
s v
2        [X]  [X]  [X]  [X]  [X]  [X]
3             [X]  [X]  [X]  [X]  [X]
5                  [X]  [X]  [X]  [X]
7                       [X]  [X]  [X]
11                            .    .
13                                 .
```

This is a powerful picture for region elimination.
