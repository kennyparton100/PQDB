# SDK WorldgenInternal Documentation

Internal systems documentation for the SDK world generation tile caches and geology layers.

**Module:** `SDK/Core/World/Worldgen/`  
**Output:** `SDK/Docs/Core/World/Worldgen/SDK_WorldgenInternal.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Tile Cache System](#tile-cache-system)
- [Shared Cache](#shared-cache)
- [Geology System](#geology-system)
- [Internal Types](#internal-types)
- [Key Files](#key-files)

---

## Module Overview

The internal worldgen systems provide the infrastructure for the layered terrain pipeline. These components handle tile caching for efficient multi-scale generation, region geology synthesis, and shared data between generation passes.

---

## Tile Cache System

### Overview

Tile caches store intermediate generation results at different scales:

| Cache | Tile Size | Purpose |
|-------|-----------|---------|
| Continental | 256×256 blocks | Climate, coast distance, hydrology |
| Macro | 128×128 blocks | Terrain synthesis, rivers |
| Region | 32×32 blocks | Geology, resources, soil |

### SdkWorldgenTileCache Structure

```c
#define SDK_WORLDGEN_TILE_CACHE_SLOTS 8

typedef struct {
    SdkWorldgenTile tiles[SDK_WORLDGEN_TILE_CACHE_SLOTS];
    int lru_order[SDK_WORLDGEN_TILE_CACHE_SLOTS];
    int tile_count;
} SdkWorldgenTileCache;
```

### Cache Slots

The cache uses an 8-slot LRU (Least Recently Used) system:

```
Slots: [0] [1] [2] [3] [4] [5] [6] [7]
       └───┴───┴───┴───┴───┴───┴───┘
       LRU order: most recent → least recent

On cache miss:
1. Evict slot[7] (oldest)
2. Shift slots: 6→7, 5→6, ..., 0→1
3. Load new tile into slot[0]
```

---

## Shared Cache

### SdkWorldgenSharedCache

Provides shared region tile lookup across generation passes:

```c
typedef struct {
    SdkRegionTile* tiles;
    int count;
    int capacity;
    int origin_tile_x;
    int origin_tile_z;
} SdkWorldgenSharedCache;
```

### Initialization

```c
// Must be called before worldgen
void sdk_worldgen_shared_cache_init(void);

// Ensures all region tile lookups return valid data
// Without this, lookups return NULL → empty chunks
```

---

## Geology System

### Geology Files

| File | Purpose |
|------|---------|
| `sdk_worldgen_column_geology.c` | Stratigraphy, rock layers, cave systems |
| `sdk_worldgen_column_surface.c` | Surface block placement |
| `sdk_worldgen_column_region.c` | Region tile synthesis (152KB) |
| `sdk_worldgen_continental.c` | Continental scale features |
| `sdk_worldgen_hydro.c` | Hydrology, rivers, lakes |
| `sdk_worldgen_macro.c` | Macro tile synthesis |

### Stratigraphy

Rock layers are generated based on:
- Bedrock province (craton, basin, volcanic, etc.)
- Age and weathering
- Faulting and folding
- Intrusion events

### Cave Generation

Caves are carved after initial block placement:
1. Generate 3D noise cave networks
2. Intersect with terrain
3. Add cave features (stalactites, crystals)
4. Seal exposed water surfaces

---

## Internal Types

### Tile Types

```c
// Continental tile - broad climate
typedef struct {
    int tile_x, tile_z;
    float height;
    float coast_distance;
    bool ocean;
    float precipitation;
    float runoff;
} SdkContinentalTile;

// Macro tile - terrain features
typedef struct {
    int tile_x, tile_z;
    float heights[MACRO_TILE_SIZE][MACRO_TILE_SIZE];
    float river_depths[MACRO_TILE_SIZE][MACRO_TILE_SIZE];
} SdkMacroTile;

// Region tile - geology details
typedef struct {
    int tile_x, tile_z;
    SdkBedrockProvince bedrock;
    SdkTerrainProvince terrain;
    float weathering;
    float resources[RESOURCE_TYPE_COUNT];
} SdkRegionTile;
```

### Column Generation Context

```c
// Internal context passed through column generation
typedef struct {
    SdkWorldGen* worldgen;
    SdkContinentalTile* continental;
    SdkMacroTile* macro;
    SdkRegionTile* region;
    int wx, wz;
} SdkColumnGenContext;
```

---

## Key Files

### Header Files

| File | Size | Contents |
|------|------|----------|
| `sdk_worldgen.h` | 2KB | Public API |
| `sdk_worldgen_types.h` | 7KB | Public types (profiles, provinces) |
| `sdk_worldgen_internal.h` | 17KB | Internal structures |
| `sdk_worldgen_column_internal.h` | 6KB | Column generation internals |
| `sdk_worldgen_column_shared.h` | 274B | Shared constants |
| `sdk_worldgen_scheduler.h` | 959B | Scheduler API |
| `sdk_worldgen_shared_cache.h` | 2KB | Shared cache API |
| `sdk_worldgen_tile_cache.h` | 2.7KB | Tile cache API |

### Implementation Files

| File | Size | Contents |
|------|------|----------|
| `sdk_worldgen.c` | 19KB | Public API implementation |
| `sdk_worldgen_column.c` | 86KB | Column generation |
| `sdk_worldgen_column_region.c` | 152KB | Region tile synthesis |
| `sdk_worldgen_column_geology.c` | 72KB | Geology, stratigraphy |
| `sdk_worldgen_column_surface.c` | 25KB | Surface placement |
| `sdk_worldgen_column_debug.c` | 6KB | Debug visualization |
| `sdk_worldgen_continental.c` | 49KB | Continental generation |
| `sdk_worldgen_hydro.c` | 60KB | Hydrology |
| `sdk_worldgen_macro.c` | 15KB | Macro tile synthesis |
| `sdk_worldgen_scheduler.c` | 14KB | Async scheduler |
| `sdk_worldgen_debug_report.c` | 56KB | Debug reporting |
| `sdk_worldgen_construction_cells.c` | 7KB | Construction cell gen |
| `sdk_worldgen_shared_cache.c` | 8KB | Shared cache |
| `sdk_worldgen_tile_cache.c` | 8KB | Tile cache |

---

## Related Documentation

- `SDK_Worldgen.md` - Public worldgen API
- `SDK_WorldgenTerrainPipeline.md` - Pipeline overview
- `SDK_WorldgenScheduler.md` - Async generation
- `SDK_SuperChunks.md` - Wall generation

---

**Note:** This documentation covers internal implementation details. For public API usage, see `SDK_Worldgen.md`.

**Total Worldgen Code:** ~800KB across 23 files
