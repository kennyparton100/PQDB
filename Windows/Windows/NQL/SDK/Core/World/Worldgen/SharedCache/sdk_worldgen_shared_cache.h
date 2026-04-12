/**
 * sdk_worldgen_shared_cache.h -- Global shared cache for worldgen tiles
 * 
 * Provides thread-safe shared caching of continental, macro, and region tiles
 * to eliminate memory duplication across multiple worldgen instances.
 */
#ifndef NQLSDK_WORLDGEN_SHARED_CACHE_H
#define NQLSDK_WORLDGEN_SHARED_CACHE_H

#include "../Internal/sdk_worldgen_internal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the global shared cache system.
 * Must be called once during application startup before any worldgen instances.
 */
void sdk_worldgen_shared_cache_init(void);

/**
 * Shutdown the global shared cache system.
 * Must be called during application shutdown after all worldgen instances destroyed.
 */
void sdk_worldgen_shared_cache_shutdown(void);

/**
 * Get or generate a continental tile from the shared cache.
 * Thread-safe. Returns pointer valid until cache eviction.
 * 
 * @param wg Worldgen context (for generation if needed)
 * @param wx World X coordinate
 * @param wz World Z coordinate
 * @return Pointer to cached tile, or NULL on failure
 */
SdkWorldGenContinentalTile* sdk_worldgen_shared_get_continental_tile(SdkWorldGen* wg, int wx, int wz);

/**
 * Get or generate a macro tile from the shared cache.
 * Thread-safe. Returns pointer valid until cache eviction.
 */
SdkWorldGenMacroTile* sdk_worldgen_shared_get_macro_tile(SdkWorldGen* wg, int wx, int wz);

/**
 * Get or generate a region tile from the shared cache.
 * Thread-safe. Returns pointer valid until cache eviction.
 */
SdkWorldGenRegionTile* sdk_worldgen_shared_get_region_tile(SdkWorldGen* wg, int wx, int wz);

/**
 * Get cache statistics.
 */
void sdk_worldgen_shared_get_stats(uint32_t* out_continental_hits,
                                    uint32_t* out_continental_misses,
                                    uint32_t* out_macro_hits,
                                    uint32_t* out_macro_misses,
                                    uint32_t* out_region_hits,
                                    uint32_t* out_region_misses);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_WORLDGEN_SHARED_CACHE_H */
