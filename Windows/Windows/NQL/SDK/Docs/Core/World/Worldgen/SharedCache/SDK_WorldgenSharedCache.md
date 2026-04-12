# SDK Worldgen Shared Cache Documentation

Comprehensive documentation for the global shared in-memory tile cache system.

**Module:** `SDK/Core/World/Worldgen/SharedCache/`  
**Files:** `sdk_worldgen_shared_cache.c`, `sdk_worldgen_shared_cache.h`

## Table of Contents

- [Module Overview](#module-overview)
- [Cache Architecture](#cache-architecture)
- [Cache Slot Counts](#cache-slot-counts)
- [Key Functions](#key-functions)
- [Usage Pattern](#usage-pattern)
- [Thread Safety](#thread-safety)
- [Cache Statistics](#cache-statistics)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Shared Cache module provides a global, thread-safe cache for world generation tiles. Unlike the per-worldgen-instance tile cache, this shared cache eliminates memory duplication when multiple worldgen contexts operate concurrently (e.g., in multiplayer or with background generation threads).

**Key Features:**
- Global singleton cache (one per application)
- Thread-safe access with synchronization
- Separate caches for continental, macro, and region tiles
- Automatic eviction on cache miss
- Statistics tracking

**Use Case:** When multiple workers generate chunks in parallel, they can share expensive continental tiles instead of each loading/generating their own copy.

---

## Cache Architecture

```
┌─────────────────────────────────────────────────────┐
│           Global Shared Cache (Singleton)           │
├─────────────────────────────────────────────────────┤
│  Continental Cache │ 64 slots │ 6,400 cells each   │
├─────────────────────────────────────────────────────┤
│  Macro Cache       │ 32 slots │ 20,736 cells each    │
├─────────────────────────────────────────────────────┤
│  Region Cache      │ 32 slots │ 7,225 samples each   │
└─────────────────────────────────────────────────────┘
                          │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
    ┌──────────┐   ┌──────────┐   ┌──────────┐
    │ Worker 1 │   │ Worker 2 │   │ Worker N │
    │ WG ctx 1 │   │ WG ctx 2 │   │ WG ctx N │
    └──────────┘   └──────────┘   └──────────┘
```

---

## Cache Slot Counts

| Cache Type | Slots | Tile Size | Total Memory (approx) |
|------------|-------|-----------|----------------------|
| Continental | 64 | 6,400 cells | 64 × 6,400 × 40B = ~16 MB |
| Macro | 32 | 20,736 cells | 32 × 20,736 × 80B = ~53 MB |
| Region | 32 | 7,225 samples | 32 × 7,225 × 200B = ~46 MB |
| **Total** | | | **~115 MB** |

*Note: Actual sizes vary with structure packing and cell/sample sizes.*

---

## Key Functions

### Lifecycle Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_shared_cache_init` | `(void) → void` | Initialize global cache (call once at startup) |
| `sdk_worldgen_shared_cache_shutdown` | `(void) → void` | Shutdown global cache (call at exit) |

### Tile Access Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_shared_get_continental_tile` | `(SdkWorldGen*, int wx, int wz) → SdkWorldGenContinentalTile*` | Get or generate continental tile |
| `sdk_worldgen_shared_get_macro_tile` | `(SdkWorldGen*, int wx, int wz) → SdkWorldGenMacroTile*` | Get or generate macro tile |
| `sdk_worldgen_shared_get_region_tile` | `(SdkWorldGen*, int wx, int wz) → SdkWorldGenRegionTile*` | Get or generate region tile |

### Statistics

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_shared_get_stats` | `(*out_continental_hits, *out_continental_misses, *out_macro_hits, *out_macro_misses, *out_region_hits, *out_region_misses) → void` | Get hit/miss counts for all cache levels |

---

## Usage Pattern

### Application Startup

```c
int main(int argc, char** argv) {
    // Initialize shared cache before any worldgen
    sdk_worldgen_shared_cache_init();
    
    // ... rest of initialization ...
    
    // Run game loop
    run_game();
    
    // Cleanup
    sdk_worldgen_shared_cache_shutdown();
    return 0;
}
```

### Tile Access in Generation

```c
void generate_chunk_with_shared_cache(SdkWorldGen* wg, SdkChunk* chunk) {
    int wx = chunk->cx * SDK_CHUNK_WIDTH;
    int wz = chunk->cz * SDK_CHUNK_DEPTH;
    
    // Get tiles from shared cache
    SdkWorldGenContinentalTile* cont = 
        sdk_worldgen_shared_get_continental_tile(wg, wx, wz);
    
    SdkWorldGenMacroTile* macro = 
        sdk_worldgen_shared_get_macro_tile(wg, wx, wz);
    
    SdkWorldGenRegionTile* region = 
        sdk_worldgen_shared_get_region_tile(wg, wx, wz);
    
    // Use tiles to fill chunk
    // ... generation logic ...
}
```

### Comparing with Instance Cache

```c
// Without shared cache (per-instance):
// Each WG context has its own cache
// Tile A loaded 4 times for 4 workers

// With shared cache:
// Tile A loaded once, shared by all workers
// Memory: 1× instead of 4×
// Cache miss: 1 instead of 4
```

---

## Thread Safety

The shared cache uses internal synchronization:

```c
// Thread-safe - can be called from any worker thread
SdkWorldGenContinentalTile* tile = 
    sdk_worldgen_shared_get_continental_tile(wg, wx, wz);

// Safe to read tile data after retrieval
// (Tile remains valid until potential eviction)
```

**Important:** Tile pointers remain valid only until the next cache operation. Do not store tile pointers across long operations without copying data.

---

## Cache Statistics

### Retrieving Stats

```c
uint32_t cont_hits, cont_misses;
uint32_t macro_hits, macro_misses;
uint32_t region_hits, region_misses;

sdk_worldgen_shared_get_stats(&cont_hits, &cont_misses,
                               &macro_hits, &macro_misses,
                               &region_hits, &region_misses);

// Calculate hit rates
float cont_hit_rate = (float)cont_hits / (cont_hits + cont_misses);
float macro_hit_rate = (float)macro_hits / (macro_hits + macro_misses);
float region_hit_rate = (float)region_hits / (region_hits + region_misses);

printf("Cache Hit Rates:\n");
printf("  Continental: %.1f%% (%u/%u)\n", 
       cont_hit_rate * 100, cont_hits, cont_hits + cont_misses);
printf("  Macro: %.1f%% (%u/%u)\n",
       macro_hit_rate * 100, macro_hits, macro_hits + macro_misses);
printf("  Region: %.1f%% (%u/%u)\n",
       region_hit_rate * 100, region_hits, region_hits + region_misses);
```

### Expected Performance

| Cache Level | Expected Hit Rate | Notes |
|-------------|-------------------|-------|
| Continental | 85-98% | Large tiles, few unique tiles needed |
| Macro | 70-90% | Medium tiles, moderate churn |
| Region | 60-85% | Small tiles, higher churn |

---

## Integration Notes

### With Disk Cache (TileCache)

The shared cache works alongside the disk cache:

```
1. Request tile at (wx, wz)
2. Check shared cache (memory) ← Fastest
   ↓ Miss
3. Check disk cache (SSD/HDD) ← Fast
   ↓ Miss
4. Generate tile (expensive) ← Slow
   ↓
5. Store in both shared and disk cache
```

### With Scheduler

When using the scheduler for bulk generation:

```c
// Scheduler workers automatically use shared cache
// Each worker thread benefits from tiles cached by others

void worker_generate_superchunk(SdkWorldGen* wg, int scx, int scz) {
    for (int cz = scz * 16; cz < (scz + 1) * 16; cz++) {
        for (int cx = scx * 16; cx < (scx + 1) * 16; cx++) {
            // All workers share continental/macro tiles
            // through the shared cache
            SdkChunk chunk;
            sdk_worldgen_generate_chunk_ctx(wg, &chunk);
        }
    }
}
```

---

## AI Context Hints

### Custom Cache Sizes

```c
// Adjust cache sizes based on available memory

// For low-memory systems:
#define SDK_WORLDGEN_CONTINENT_CACHE_SLOTS 16
#define SDK_WORLDGEN_REGION_CACHE_SLOTS 16

// For high-memory systems:
#define SDK_WORLDGEN_CONTINENT_CACHE_SLOTS 256
#define SDK_WORLDGEN_REGION_CACHE_SLOTS 128
```

### LRU Tracking

```c
// Add timestamp-based LRU for better eviction

typedef struct {
    SdkWorldGenContinentalTile tile;
    uint64_t last_access_time;
    uint32_t access_count;
} CachedContinentalTile;

// Evict oldest instead of random
int find_lru_slot(CachedContinentalTile* cache, int slot_count) {
    int lru_index = 0;
    uint64_t oldest_time = cache[0].last_access_time;
    
    for (int i = 1; i < slot_count; i++) {
        if (cache[i].last_access_time < oldest_time) {
            oldest_time = cache[i].last_access_time;
            lru_index = i;
        }
    }
    return lru_index;
}
```

### Hot Tile Tracking

```c
// Keep frequently-accessed tiles pinned

typedef struct {
    SdkWorldGenContinentalTile tile;
    uint32_t access_count;
    bool pinned;
} CachedTile;

// Pin tiles near spawn
void pin_spawn_tiles(int spawn_x, int spawn_z) {
    for (int dz = -2; dz <= 2; dz++) {
        for (int dx = -2; dx <= 2; dx++) {
            int tx = (spawn_x / (64 * 256)) + dx;
            int tz = (spawn_z / (64 * 256)) + dz;
            pin_tile_continental(tx, tz);
        }
    }
}
```

### Preloading

```c
// Preload tiles in a region before player arrives
void preload_region_tiles(SdkWorldGen* wg, int center_wx, int center_wz,
                          int radius_chunks) {
    int radius_tiles = (radius_chunks * 16) / 128 + 1;  // macro tiles
    
    for (int dz = -radius_tiles; dz <= radius_tiles; dz++) {
        for (int dx = -radius_tiles; dx <= radius_tiles; dx++) {
            int wx = center_wx + dx * 128;
            int wz = center_wz + dz * 128;
            
            // Force load into cache
            sdk_worldgen_shared_get_macro_tile(wg, wx, wz);
        }
    }
}
```

### Cache Warming Worker

```c
// Background thread to warm cache
void* cache_warming_worker(void* arg) {
    SdkWorldGen* wg = (SdkWorldGen*)arg;
    
    while (!should_stop) {
        // Get next tile to warm from queue
        TileCoord coord = get_next_warm_tile();
        
        // Load into cache (if not already present)
        sdk_worldgen_shared_get_continental_tile(wg, 
                                                  coord.x * 256 * 64,
                                                  coord.z * 256 * 64);
    }
    return NULL;
}
```

---

## Related Documentation

- [SDK_Worldgen.md](../SDK_Worldgen.md) - Public API
- [SDK_WorldgenInternal.md](../Internal/SDK_WorldgenInternal.md) - Internal structures
- [SDK_WorldgenTileCache.md](../TileCache/SDK_WorldgenTileCache.md) - Disk cache
- [SDK_WorldgenScheduler.md](../Scheduler/SDK_WorldgenScheduler.md) - Async generation

---

**Source Files:**
- `SDK/Core/World/Worldgen/SharedCache/sdk_worldgen_shared_cache.h` (67 lines)
- `SDK/Core/World/Worldgen/SharedCache/sdk_worldgen_shared_cache.c`
