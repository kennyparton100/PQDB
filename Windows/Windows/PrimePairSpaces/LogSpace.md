# Log-Factor Space (u, v)

**Axes:** `u = log(p)`, `v = log(q)`
**Coordinates:** `(u, v)` in log-transformed factor space.

## Purpose

Straightens multiplication into addition. Key geometric properties:

- **Diagonal `u = v`:** balanced semiprimes
- **Line `u + v = log(N)`:** constant product (all factor pairs of N)
- **Distance from diagonal `|u - v|`:** multiplicative imbalance

## Geometry

```
v ^
|
|\
| \
|  \      constant N: u + v = const
|   \
|    \
|     \
|      \
|       \
+--------\-----------------------> u
         /
        /
       /
      /
     /
    diagonal: u = v
```

## Key Insight

Distance from diagonal = `|u - v|`

This is multiplicative imbalance in disguise. Much better than raw index distance.
