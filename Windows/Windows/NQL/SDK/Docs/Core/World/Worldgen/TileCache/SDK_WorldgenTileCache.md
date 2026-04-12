# SDK Worldgen Tile Cache Documentation

Comprehensive documentation for the disk-backed continental tile cache using LZ4 compression.

**Module:** `SDK/Core/World/Worldgen/TileCache/`  
**Files:** `sdk_worldgen_tile_cache.c`, `sdk_worldgen_tile_cache.h`

## Table of Contents

- [Module Overview](#module-overview)
- [Cache File Format](#cache-file-format)
- [Cache Header Structure](#cache-header-structure)
- [Key Functions](#key-functions)
- [Cache Directory Layout](#cache-directory-layout)
- [Usage Patterns](#usage-patterns)
- [Statistics and Monitoring](#statistics-and-monitoring)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Tile Cache module provides persistent storage for expensive continental tile generation results. Since continental tiles contain climate, hydrology, and ocean data that is costly to regenerate, this cache stores compressed tile data to disk for fast retrieval on subsequent world loads.

**Key Features:**
- LZ4 compression (~50-100 KB per tile compressed vs ~256 KB raw)
- World seed-based cache keys
- Automatic cache directory creation
- Cache statistics tracking
- Cache invalidation and clearing

**Typical Sizes:**
- Raw tile: 6,400 cells × 40 bytes ≈ 256 KB
- Compressed: 50-100 KB (60-80% compression)
- Cache directory: `WorldGenCache/` by default

---

## Cache File Format

### File Layout

```
┌─────────────────────────────────────┐
│ SdkTileCacheHeader (32 bytes)       │
├─────────────────────────────────────┤
│ LZ4 Compressed Cell Data            │
│ (SDK_WORLDGEN_CONTINENT_CELL_COUNT  │
│  × sizeof(SdkContinentalCell))      │
└─────────────────────────────────────┘
```

### Header Magic and Version

```c
#define SDK_TILE_CACHE_MAGIC    0x544C4332u  // "TLC2"
#define SDK_TILE_CACHE_VERSION  1u
```

---

## Cache Header Structure

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;              // 0x544C4332 "TLC2"
    uint32_t version;            // Currently 1
    uint32_t world_seed;         // World seed for cache key
    int32_t  tile_x;             // Tile X coordinate
    int32_t  tile_z;             // Tile Z coordinate
    uint32_t uncompressed_size;  // Size before compression
    uint32_t compressed_size;    // Size after compression
} SdkTileCacheHeader;
#pragma pack(pop)
```

**Total Header Size:** 32 bytes (packed)

---

## Key Functions

### Lifecycle Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_tile_cache_init` | `(SdkWorldGenTileCache*, uint32_t seed, const char* cache_dir) → void` | Initialize cache, create directory |
| `sdk_worldgen_tile_cache_shutdown` | `(SdkWorldGenTileCache*) → void` | Cleanup cache instance |
| `sdk_worldgen_tile_cache_clear` | `(SdkWorldGenTileCache*) → void` | Delete all cached tiles for seed |

### Load/Save Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_tile_cache_load_continental` | `(cache, tile_x, tile_z, out_cells) → int` | Load tile from cache (returns 1 if found) |
| `sdk_worldgen_tile_cache_save_continental` | `(cache, tile_x, tile_z, cells) → int` | Save tile to cache (returns 1 if success) |

### Statistics

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_tile_cache_get_stats` | `(cache, out_hits, out_misses, out_bytes_loaded, out_bytes_saved) → void` | Get cache statistics |

---

## Cache Directory Layout

### Default Location

```
WorldGenCache/
├── tile_0_0.bin      // Tile at (0, 0)
├── tile_0_1.bin      // Tile at (0, 1)
├── tile_1_0.bin      // Tile at (1, 0)
├── tile_-1_0.bin     // Tile at (-1, 0)
└── ...
```

### File Naming

```c
// Build path from coordinates
sprintf(filename, "tile_%d_%d.bin", tile_x, tile_z);

// Full path
char path[MAX_PATH];
snprintf(path, MAX_PATH, "%s/tile_%d_%d.bin", 
         cache->cache_dir, tile_x, tile_z);
```

### Directory Creation

The cache automatically creates the directory if it doesn't exist:

```c
static int ensure_directory_exists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return 1;  // Already exists
    }
    return CreateDirectoryA(path, NULL) || 
           GetLastError() == ERROR_ALREADY_EXISTS;
}
```

---

## Usage Patterns

### Initialization

```c
SdkWorldGenTileCache tile_cache;

// Initialize with default directory
sdk_worldgen_tile_cache_init(&tile_cache, world_seed, NULL);

// Or with custom directory
sdk_worldgen_tile_cache_init(&tile_cache, world_seed, "MyWorld/Cache");
```

### Tile Loading (Cache Path)

```c
SdkContinentalCell cells[SDK_WORLDGEN_CONTINENT_CELL_COUNT];

// Try to load from cache
if (sdk_worldgen_tile_cache_load_continental(&tile_cache, tile_x, tile_z, cells)) {
    // Cache hit - use loaded data
    populate_tile_from_cells(tile, cells);
} else {
    // Cache miss - generate tile
    sdk_worldgen_build_continental_tile(wg, tile);
    
    // Save to cache for next time
    sdk_worldgen_tile_cache_save_continental(&tile_cache, tile_x, tile_z, 
                                              tile->cells);
}
```

### Cache Warming (Proactive)

```c
// Pre-generate and cache tiles around spawn
for (int dz = -2; dz <= 2; dz++) {
    for (int dx = -2; dx <= 2; dx++) {
        int tx = spawn_tile_x + dx;
        int tz = spawn_tile_z + dz;
        
        // Check if already cached
        if (!tile_is_cached(&tile_cache, tx, tz)) {
            // Generate and cache
            SdkWorldGenContinentalTile tile;
            build_continental_tile(wg, &tile, tx, tz);
            sdk_worldgen_tile_cache_save_continental(&tile_cache, tx, tz, 
                                                      tile.cells);
        }
    }
}
```

---

## Statistics and Monitoring

### Cache Stats Structure

```c
typedef struct {
    void* impl;
    // Internal implementation includes:
    // - uint32_t cache_hits
    // - uint32_t cache_misses
    // - uint64_t bytes_loaded
    // - uint64_t bytes_saved
} SdkWorldGenTileCache;
```

### Retrieving Statistics

```c
uint32_t hits, misses;
uint64_t bytes_loaded, bytes_saved;

sdk_worldgen_tile_cache_get_stats(&tile_cache, &hits, &misses,
                                   &bytes_loaded, &bytes_saved);

// Calculate hit rate
float hit_rate = (float)hits / (float)(hits + misses);

// Calculate compression ratio
float avg_compression = (float)bytes_saved / (float)bytes_loaded;

printf("Cache: %u hits, %u misses (%.1f%% hit rate)\n", 
       hits, misses, hit_rate * 100.0f);
printf("Total: %.1f MB loaded, %.1f MB saved (%.1fx compression)\n",
       bytes_loaded / 1e6, bytes_saved / 1e6, 
       bytes_loaded / (float)bytes_saved);
```

### Performance Metrics

| Metric | Typical Value | Notes |
|--------|---------------|-------|
| Hit Rate | 80-95% | Higher for settled areas |
| Compression | 3-5x | Varies with terrain complexity |
| Load Time | ~1-5ms | From SSD |
| Save Time | ~2-10ms | LZ4 is fast |

---

## Integration Notes

### Worldgen Init Integration

```c
void sdk_worldgen_init_ex(SdkWorldGen* wg, const SdkWorldDesc* desc, 
                          SdkWorldGenCacheMode cache_mode) {
    // ... other init ...
    
    if (cache_mode == SDK_WORLDGEN_CACHE_DISK) {
        // Initialize tile cache
        wg->impl->tile_cache = malloc(sizeof(SdkWorldGenTileCache));
        sdk_worldgen_tile_cache_init(wg->impl->tile_cache, 
                                      desc->seed, NULL);
    }
}
```

### Shutdown Cleanup

```c
void sdk_worldgen_shutdown(SdkWorldGen* wg) {
    // ... other cleanup ...
    
    if (wg->impl->tile_cache) {
        sdk_worldgen_tile_cache_shutdown(wg->impl->tile_cache);
        free(wg->impl->tile_cache);
        wg->impl->tile_cache = NULL;
    }
}
```

### Error Handling

```c
// Save with error handling
if (!sdk_worldgen_tile_cache_save_continental(&tile_cache, tx, tz, cells)) {
    // Log error but don't fail - generation can continue
    log_warning("Failed to cache tile (%d, %d)", tx, tz);
}

// Load handles missing files gracefully
if (!sdk_worldgen_tile_cache_load_continental(&tile_cache, tx, tz, cells)) {
    // Expected for first-time generation
    // Fall through to generation path
}
```

---

## AI Context Hints

### Custom Compression Level

```c
// Current implementation uses LZ4 default (fast)
// For better compression at cost of speed:

// In sdk_worldgen_tile_cache.c, modify save:
int compress_level = 9;  // LZ4 HC (high compression)
int compressed_size = LZ4_compress_HC(
    (const char*)cells, compressed_buffer,
    cell_data_size, max_compressed_size,
    compress_level);
```

### Cache Validation

```c
// Add checksum to header for corruption detection
typedef struct {
    // ... existing fields ...
    uint32_t crc32;  // CRC of uncompressed data
} SdkTileCacheHeader;

// Validate on load
if (header.crc32 != compute_crc32(cells, header.uncompressed_size)) {
    // Cache corrupted, regenerate
    return 0;
}
```

### Multi-Seed Cache

```c
// Organize cache by seed subdirectory
void build_cache_path(SdkWorldGenTileCache* cache, int tx, int tz,
                      char* out_path, size_t size) {
    snprintf(out_path, size, "%s/%08X/tile_%d_%d.bin",
             cache->cache_dir, cache->world_seed, tx, tz);
}

// Creates: WorldGenCache/1234ABCD/tile_0_0.bin
```

### Async Cache Operations

```c
// Queue cache writes to avoid blocking generation
typedef struct {
    SdkWorldGenTileCache* cache;
    int tile_x, tile_z;
    SdkContinentalCell* cells;  // copied
} CacheWriteJob;

void queue_cache_write(SdkWorldGen* wg, int tx, int tz, 
                       SdkContinentalCell* cells) {
    CacheWriteJob* job = malloc(sizeof(CacheWriteJob));
    job->cache = wg->impl->tile_cache;
    job->tile_x = tx;
    job->tile_z = tz;
    job->cells = malloc(sizeof(SdkContinentalCell) * 
                        SDK_WORLDGEN_CONTINENT_CELL_COUNT);
    memcpy(job->cells, cells, sizeof(SdkContinentalCell) * 
           SDK_WORLDGEN_CONTINENT_CELL_COUNT);
    
    // Queue for background thread
    submit_background_job(cache_write_worker, job);
}
```

### Cache Pruning

```c
// Remove old cache files when exceeding size limit
void prune_cache_if_needed(SdkWorldGenTileCache* cache, 
                           uint64_t max_size_bytes) {
    uint64_t current_size = 0;
    // Calculate total cache size
    // ... enumerate files ...
    
    if (current_size > max_size_bytes) {
        // Sort by access time, delete oldest
        // Or: delete tiles furthest from spawn
    }
}
```

---

## Related Documentation

- [SDK_Worldgen.md](../SDK_Worldgen.md) - Public API and generation pipeline
- [SDK_WorldgenInternal.md](../Internal/SDK_WorldgenInternal.md) - Internal structures
- [SDK_WorldgenContinental.md](../Continental/SDK_WorldgenContinental.md) - Tile generation
- [SDK_WorldgenSharedCache.md](../SharedCache/SDK_WorldgenSharedCache.md) - In-memory cache

---

**Source Files:**
- `SDK/Core/World/Worldgen/TileCache/sdk_worldgen_tile_cache.h` (85 lines)
- `SDK/Core/World/Worldgen/TileCache/sdk_worldgen_tile_cache.c` (~400 lines)
