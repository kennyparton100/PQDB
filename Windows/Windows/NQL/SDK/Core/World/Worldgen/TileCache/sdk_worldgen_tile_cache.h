/**
 * sdk_worldgen_tile_cache.h -- Disk-backed continental tile cache
 * 
 * Provides persistent caching of expensive continental tile generation results.
 * Uses LZ4 compression to minimize disk space (~50-100 KB per tile compressed).
 */
#ifndef NQLSDK_WORLDGEN_TILE_CACHE_H
#define NQLSDK_WORLDGEN_TILE_CACHE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDK_TILE_CACHE_MAGIC 0x544C4332u  /* "TLC2" */
#define SDK_TILE_CACHE_VERSION 1u

typedef struct {
    void* impl;
} SdkWorldGenTileCache;

/**
 * Initialize the tile cache system.
 * Creates cache directory if needed.
 * 
 * @param cache The cache instance to initialize
 * @param world_seed The world seed (used in cache key)
 * @param cache_dir Path to cache directory (NULL = default "WorldGenCache")
 */
void sdk_worldgen_tile_cache_init(SdkWorldGenTileCache* cache, uint32_t world_seed, const char* cache_dir);

/**
 * Shutdown and cleanup the tile cache.
 */
void sdk_worldgen_tile_cache_shutdown(SdkWorldGenTileCache* cache);

/**
 * Try to load a continental tile from disk cache.
 * 
 * @param cache The cache instance
 * @param tile_x Tile X coordinate
 * @param tile_z Tile Z coordinate
 * @param out_cells Output buffer for cell data (SDK_WORLDGEN_CONTINENT_CELL_COUNT cells)
 * @return 1 if loaded successfully, 0 if not found or failed
 */
int sdk_worldgen_tile_cache_load_continental(SdkWorldGenTileCache* cache, 
                                              int32_t tile_x, 
                                              int32_t tile_z,
                                              void* out_cells);

/**
 * Save a continental tile to disk cache.
 * 
 * @param cache The cache instance
 * @param tile_x Tile X coordinate
 * @param tile_z Tile Z coordinate
 * @param cells Cell data to save (SDK_WORLDGEN_CONTINENT_CELL_COUNT cells)
 * @return 1 if saved successfully, 0 if failed
 */
int sdk_worldgen_tile_cache_save_continental(SdkWorldGenTileCache* cache,
                                              int32_t tile_x,
                                              int32_t tile_z,
                                              const void* cells);

/**
 * Clear all cached tiles for the current world seed.
 */
void sdk_worldgen_tile_cache_clear(SdkWorldGenTileCache* cache);

/**
 * Get cache statistics.
 */
void sdk_worldgen_tile_cache_get_stats(const SdkWorldGenTileCache* cache,
                                        uint32_t* out_hits,
                                        uint32_t* out_misses,
                                        uint64_t* out_bytes_loaded,
                                        uint64_t* out_bytes_saved);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_WORLDGEN_TILE_CACHE_H */
