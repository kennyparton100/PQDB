# Fermat Space (A, B)

**Axes:** `A = (p + q) / 2`, `B = (q - p) / 2`
**Identity:** `N = A^2 - B^2`

## Purpose

Natural space for Fermat factorization.

- **B = 0:** perfect square, factors are equal
- **B small:** near-square, Fermat finds it fast
- **B large:** Fermat gets cooked

## B Axis

```
B ->
0 -------------------------------------------------------------->

very close factors      moderately apart          far apart
```

## Examples

| N   | Factorization | A    | B    |
|-----|---------------|------|------|
| 49  | 7 * 7         | 7    | 0    |
| 77  | 7 * 11        | 9    | 2    |
| 26  | 2 * 13        | 7.5  | 5.5  |

## How Fermat Search Works

```
Try A = ceil(sqrt(N)), ceil(sqrt(N))+1, ceil(sqrt(N))+2, ...

At each step:
    test whether A^2 - N = B^2

If B is small, success happens quickly.
If B is large, Fermat gets cooked.
```

```
A search ->
start at ceil(sqrt(N))  ->  +1  ->  +1  ->  +1  -> ...

check:
A^2-N = square?   no      no      yes     ...
                  ^               ^
               not hit         found B^2
```

---

Back to [Documentation Index](../Documentation.md)
