# Index Space (i, j)

**Axes:** Prime indices, not prime values.
**Coordinates:** `(i, j)` where `p = prime(i)`, `q = prime(j)`.
**Product:** `N = prime(i) * prime(j)`

## Purpose

- Storage ranges
- CPSS coverage ranges
- Index-bounded sweeps
- Bookkeeping

## Example (primes 2, 3, 5, 7, 11, 13)

```
          j ->
          1    2    3    4    5    6
i v
1         4    6   10   14   22   26
2         6    9   15   21   33   39
3        10   15   25   35   55   65
4        14   21   35   49   77   91
5        22   33   55   77  121  143
6        26   39   65   91  143  169
```

With prime labels:

```
          j ->
          1    2    3    4    5    6
          2    3    5    7   11   13
i v
1   2      4    6   10   14   22   26
2   3      6    9   15   21   33   39
3   5     10   15   25   35   55   65
4   7     14   21   35   49   77   91
5  11     22   33   55   77  121  143
6  13     26   39   65   91  143  169
```

## Why It Feels Warped

```
index steps:   1 -> 2 -> 3 -> 4 -> 5 -> 6
prime steps:   2 -> 3 -> 5 -> 7 -> 11 -> 13
gaps:         +1   +2   +2   +4   +2
```

"Move one square right" is uniform in index, but not in actual factor size.

---

Back to [Documentation Index](../Documentation.md)
