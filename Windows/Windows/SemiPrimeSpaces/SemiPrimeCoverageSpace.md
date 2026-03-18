# Coverage Space

**Type:** Engine overlay ("what have we already tried, and what still looks worth trying?").

## Purpose

After each failed method, regions move downward in plausibility. This prevents the engine from re-doing low-value work.

## Axes

- **x:** Cost to test
- **y:** Remaining plausibility

## Routing Map

```
remaining plausibility ^

high      |   BEST targets
          |   plausible + cheap enough
          |
medium    |   situational
          |
low       |   mostly ruled out / low ROI
          +---------------------------------> cost
              low               medium             high
```
