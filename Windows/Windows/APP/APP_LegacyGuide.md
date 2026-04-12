# APP Legacy Guide

This guide helps you translate older APP/space commands into the newer unified command surface.

For the guide index, see [APP_Guide.md](APP_Guide.md).

---

## Why This Guide Exists

APP still supports older compatibility commands, but the shell now prefers a unified interface centered on:

- `space`
- `space-compare`
- `space-stats`
- `space-export`
- `space-metrics`

If you already know the older commands, this file tells you what to use now.

---

## Preferred Modern Commands

### Region rendering

Prefer:

```text
> space value row 4 4 6
> space fermat rect-unique 4 6 4 6
```

Instead of older forms such as:

```text
> value-space-row-range 4 4 6
> fermat-space-rect-unique 4 6 4 6
```

### Multi-space comparison

Prefer:

```text
> space-compare diagonal 4 8 --spaces index,value,fermat
> space-stats-compare triangle 4 6 --spaces value,mean-imbalance,minmax
```

These have no equally clean legacy equivalent and are part of the modern APP workflow.

### Metric discovery

Prefer:

```text
> space-list
> space-regions
> space-metrics value
```

---

## Legacy Commands Still Mentioned by APP

The APP help keeps these as compatibility commands:

- `<space>-row-range`
- `<space>-col-range`
- `<space>-rect`
- `<space>-rect-unique`

for these space prefixes:

- `value-space`
- `log-space`
- `mean-imbalance`
- `product-ratio`
- `fermat-space`
- `minmax-space`

These still work, but the unified `space` command is the recommended path.

---

## Useful Single-Point Legacy Commands

APP help still calls out some single-point commands as directly useful:

```text
> value-space <p> <q>
> log-space <p> <q>
> mean-imbalance <p> <q>
> mean-imbalance-n <N>
> product-ratio <p> <q>
> product-ratio-bounds <N>
> fermat-space <p> <q>
> fermat-space-verify <N> <A> <B>
> fermat-space-start <N>
> minmax-space <p> <q>
> minmax-coverage <limit> <N_max>
> minmax-uncovered <limit>
> index-space <i> <j>
> index-space-reverse ...
> index-space-row ...
> index-space-bounds ...
```

These can still be useful when you want a direct, narrow operation without going through the higher-level region interface.

---

## Translation Examples

### Old to new: region view

```text
Old: value-space-row-range 4 4 6
New: space value row 4 4 6
```

### Old to new: rectangle

```text
Old: fermat-space-rect-unique 4 6 4 6
New: space fermat rect-unique 4 6 4 6
```

### Old to new: concept discovery

```text
Old habit: remember exact command names
New approach:
> space-list
> space-regions
> space-metrics fermat
```

---

## When to Keep Using Legacy Commands

Keep a legacy command when:

- it is a short single-purpose helper you already know
- you are debugging an older workflow or transcript
- you are comparing old output conventions with new ones

Move to the unified commands when:

- you want consistent syntax across spaces
- you want discoverability via `space-list` and `space-regions`
- you want comparison, stats, export, or semiprime-family support

---

## Related Guides

- Unified space commands: [APP_SpaceGuide.md](APP_SpaceGuide.md)
- Semiprime family: [APP_SemiPrimeGuide.md](APP_SemiPrimeGuide.md)
- Workflow recipes: [APP_WorkflowGuide.md](APP_WorkflowGuide.md)
