# Observable-Feature Space

**Type:** Engine overlay (what the engine can actually see before it wins).

## Purpose

This is the engine's routing dashboard. Not the true factor space — it's the space of cheap signals available before factorization succeeds.

## Axes

- **x:** Evidence of small factor
- **y:** Evidence of near-square structure

## Routing Map

```
near-square evidence ^

high      |   Fermat gets a cheap shot
          |
medium    |   maybe hybrid routing
          |
low       |   small-factor methods / broad fallback
          +-----------------------------------------> small-factor evidence
              low                  medium                 high
```

## Cheap Signals Per Input N

- Closeness to a square
- Small-prime coverage exhausted or not
- Residues
- `p-1` attempts failed or not
- ECM work already spent
- Primality of cofactors after partial work

---

Back to [Documentation Index](../Documentation.md)
