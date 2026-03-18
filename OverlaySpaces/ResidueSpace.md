# Residue Space (p mod M, q mod M)

**Type:** Overlay (colours other spaces, does not replace them).
**Coordinates:** `(p mod M, q mod M)` for a chosen modulus `M`.

## Purpose

- Wheel filters
- Residue exclusions
- Congruence overlays
- Infrastructure constraints

## Example: M = 30

Primes greater than 5 can only live in these residue classes mod 30:

```
1, 7, 11, 13, 17, 19, 23, 29
```

Residue grid:

```
             q mod 30 ->
         1   7  11  13  17  19  23  29
p mod 30
   1     .   .   .   .   .   .   .   .
   7     .   .   .   .   .   .   .   .
  11     .   .   .   .   .   .   .   .
  13     .   .   .   .   .   .   .   .
  17     .   .   .   .   .   .   .   .
  19     .   .   .   .   .   .   .   .
  23     .   .   .   .   .   .   .   .
  29     .   .   .   .   .   .   .   .
```

Everything outside this grid is impossible for odd primes > 5.
