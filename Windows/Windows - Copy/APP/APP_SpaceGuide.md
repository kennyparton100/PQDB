# APP Space Guide

This guide covers prime-pair space navigation, region commands, metric discovery, and multi-space comparison inside APP mode.

For the overall doc map, see [APP_Guide.md](APP_Guide.md).

---

## Two Space Models in APP

APP exposes two distinct space families:

- **Prime-pair spaces**: region-based views over exact factor pairs `(p, q)`
- **Semiprime spaces**: per-`N` report views

This file focuses on **prime-pair** spaces.

For semiprime per-`N` views, use [APP_SemiPrimeGuide.md](APP_SemiPrimeGuide.md).

---

## Prime-Pair Spaces

Available space names:

- `index`
- `value`
- `log`
- `mean-imbalance`
- `product-ratio`
- `fermat`
- `minmax`
- `cfrac`
- `lattice`

Bare names default to the prime-pair family.

Recognized family aliases from APP help:

- `prime-pair`, `Prime-Pair`, `PrimePair`, `pair`, `pp`
- `semiprime`, `SemiPrime`, `semi-prime`, `semi`, `sp`

Examples:

```text
> space index diagonal 4 8
> space prime-pair fermat triangle 4 6
> space minmax window 10 12 2
```

---

## Discoverability Commands

```text
> space-list
> space-list prime-pair
> space-regions
> space-metrics value
> space-metrics prime-pair fermat
```

Use these before guessing syntax.

- `space-list` shows available spaces
- `space-regions` shows valid region shapes
- `space-metrics` explains the columns and aggregates reported by a space

---

## Region Syntax

Prime-pair region commands use these shapes:

```text
point <i> <j>
row <i> <j_lo> <j_hi>
col <j> <i_lo> <i_hi>
rect <i_lo> <i_hi> <j_lo> <j_hi>
rect-unique <i_lo> <i_hi> <j_lo> <j_hi>
diagonal <i_lo> <i_hi>
band <offset> <i_lo> <i_hi>
triangle <i_lo> <i_hi>
window <i> <j> <radius>
```

Examples:

```text
> space value point 4 5
> space index row 4 4 8
> space fermat triangle 4 20
> space log band 1 4 50
```

---

## Core Prime-Pair Commands

### `space`

Render one space for one region.

```text
> space value diagonal 4 8
> space fermat triangle 4 6
```

### `space-compare`

Render the same region through multiple spaces.

```text
> space-compare diagonal 4 8 --spaces index,value,fermat
```

### `space-stats`

Aggregate statistics for one space over a region.

```text
> space-stats value triangle 4 20
> space-stats fermat rect-unique 4 20 4 20
```

### `space-stats-compare`

Compare aggregate stats across multiple spaces.

```text
> space-stats-compare diagonal 4 20 --spaces index,value,product-ratio
```

### `space-export`

Export a prime-pair region to CSV.

```text
> space-export value triangle 4 20 values.csv
```

Batch/export details are covered in [APP_ExportGuide.md](APP_ExportGuide.md).

---

## When to Use Each Space

- `index`: storage layout, coverage bounds, bookkeeping
- `value`: literal factor geometry in actual prime values
- `log`: multiplicative balance in additive coordinates
- `mean-imbalance`: routing-oriented rotated view of factor imbalance
- `product-ratio`: balance as a ratio class
- `fermat`: near-square structure and Fermat search intuition
- `minmax`: small-factor coverage viewpoint

Detailed conceptual docs already exist here:

- [../PrimePairSpaces/IndexSpace.md](../PrimePairSpaces/IndexSpace.md)
- [../PrimePairSpaces/ValueSpace.md](../PrimePairSpaces/ValueSpace.md)
- [../PrimePairSpaces/LogSpace.md](../PrimePairSpaces/LogSpace.md)
- [../PrimePairSpaces/MeanImbalanceSpace.md](../PrimePairSpaces/MeanImbalanceSpace.md)
- [../PrimePairSpaces/ProductRatioSpace.md](../PrimePairSpaces/ProductRatioSpace.md)
- [../PrimePairSpaces/FermatSpace.md](../PrimePairSpaces/FermatSpace.md)
- [../PrimePairSpaces/MinMaxSpace.md](../PrimePairSpaces/MinMaxSpace.md)

---

## DB Requirement

Prime-pair region navigation is DB-backed because APP must fetch prime-indexed cells from loaded data.

Typical workflow:

```text
> load C:\GitHub\CPSS\CompressedPrimeStateStreams\0_TO_1_TRILLION\Decompressed\DecompressedPrimeStateStreamV4_1_TO_1_TRILLION_decompressed.bin --mode benchmark
> space-list
> space-regions
> space value diagonal 4 8
> space-compare triangle 4 6 --spaces value,fermat,minmax
> space-stats log band 1 4 100
```

---

## Related Guides

- Semiprime spaces: [APP_SemiPrimeGuide.md](APP_SemiPrimeGuide.md)
- Export pipelines: [APP_ExportGuide.md](APP_ExportGuide.md)
- Factor routing context: [APP_FactorisationGuide.md](APP_FactorisationGuide.md)
