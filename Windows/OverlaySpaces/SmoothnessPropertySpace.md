# Smoothness-Property Space

**Type:** Property overlay (not geometric in the usual size sense).
**Relevance:** `p-1`, `p+1`, ECM routing.

## Purpose

Two semiprimes can sit in similar size regions but behave totally differently under `p-1` methods. This space captures that distinction.

## Axes

- **x:** Size of smaller factor
- **y:** Smoothness-friendliness of `p-1`

## Routing Map

```
smoothness of p-1 ^

high     |   Pollard p-1 happy zone
         |   even if factor isn't ultra tiny
         |
medium   |   maybe worth trying
         |
low      |   p-1 probably waste of time
         +---------------------------------> smaller factor size
             tiny            medium                large
```
