/**
 * sdk_worldgen_shared_cache.c -- Global shared cache implementation
 */
#include "sdk_worldgen_shared_cache.h"
#include "../sdk_worldgen.h"
#include "../../../API/Internal/sdk_load_trace.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SHARED_CONTINENTAL_SLOTS 64
#define SHARED_MACRO_SLOTS 32
#define SHARED_REGION_SLOTS 32

typedef struct {
    CRITICAL_SECTION lock;
    SdkWorldGenContinentalTile tiles[SHARED_CONTINENTAL_SLOTS];
    uint32_t stamp_clock;
    uint32_t hits;
    uint32_t misses;
} SharedContinentalCache;

typedef struct {
    CRITICAL_SECTION lock;
    SdkWorldGenMacroTile tiles[SHARED_MACRO_SLOTS];
    uint32_t stamp_clock;
    uint32_t hits;
    uint32_t misses;
} SharedMacroCache;

typedef struct {
    CRITICAL_SECTION lock;
    SdkWorldGenRegionTile tiles[SHARED_REGION_SLOTS];
    uint32_t stamp_clock;
    uint32_t hits;
    uint32_t misses;
} SharedRegionCache;

static SharedContinentalCache g_continental_cache;
static SharedMacroCache g_macro_cache;
static SharedRegionCache g_region_cache;
static int g_cache_initialized = 0;

void sdk_worldgen_shared_cache_init(void)
{
    if (g_cache_initialized) return;
    
    memset(&g_continental_cache, 0, sizeof(g_continental_cache));
    memset(&g_macro_cache, 0, sizeof(g_macro_cache));
    memset(&g_region_cache, 0, sizeof(g_region_cache));
    
    InitializeCriticalSection(&g_continental_cache.lock);
    InitializeCriticalSection(&g_macro_cache.lock);
    InitializeCriticalSection(&g_region_cache.lock);
    
    g_continental_cache.stamp_clock = 1;
    g_macro_cache.stamp_clock = 1;
    g_region_cache.stamp_clock = 1;
    
    g_cache_initialized = 1;
    
    sdk_debug_log_output("[SHARED_CACHE] Initialized global worldgen cache\n");
}

void sdk_worldgen_shared_cache_shutdown(void)
{
    if (!g_cache_initialized) return;
    
    DeleteCriticalSection(&g_continental_cache.lock);
    DeleteCriticalSection(&g_macro_cache.lock);
    DeleteCriticalSection(&g_region_cache.lock);
    
    g_cache_initialized = 0;
    
    char msg[256];
    sprintf_s(msg, sizeof(msg),
              "[SHARED_CACHE] Shutdown - Continental hits: %u misses: %u, Macro hits: %u misses: %u, Region hits: %u misses: %u\n",
              g_continental_cache.hits, g_continental_cache.misses,
              g_macro_cache.hits, g_macro_cache.misses,
              g_region_cache.hits, g_region_cache.misses);
    sdk_debug_log_output(msg);
}

static int floor_div_local(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return (value - denom + 1) / denom;
}

SdkWorldGenContinentalTile* sdk_worldgen_shared_get_continental_tile(SdkWorldGen* wg, int wx, int wz)
{
    int cell_x, cell_z, tile_x, tile_z;
    int i, best;
    uint32_t oldest;
    SdkWorldGenContinentalTile* result = NULL;
    
    if (!g_cache_initialized || !wg) return NULL;
    
    cell_x = floor_div_local(wx, SDK_WORLDGEN_CONTINENT_CELL_BLOCKS);
    cell_z = floor_div_local(wz, SDK_WORLDGEN_CONTINENT_CELL_BLOCKS);
    tile_x = floor_div_local(cell_x, SDK_WORLDGEN_CONTINENT_TILE_SIZE);
    tile_z = floor_div_local(cell_z, SDK_WORLDGEN_CONTINENT_TILE_SIZE);
    
    EnterCriticalSection(&g_continental_cache.lock);
    
    for (i = 0; i < SHARED_CONTINENTAL_SLOTS; ++i) {
        SdkWorldGenContinentalTile* tile = &g_continental_cache.tiles[i];
        if (tile->valid && tile->tile_x == tile_x && tile->tile_z == tile_z) {
            tile->stamp = ++g_continental_cache.stamp_clock;
            g_continental_cache.hits++;
            result = tile;
            LeaveCriticalSection(&g_continental_cache.lock);
            return result;
        }
    }
    
    g_continental_cache.misses++;
    
    best = -1;
    oldest = 0xffffffffu;
    for (i = 0; i < SHARED_CONTINENTAL_SLOTS; ++i) {
        SdkWorldGenContinentalTile* tile = &g_continental_cache.tiles[i];
        if (!tile->valid) {
            best = i;
            break;
        }
        if (tile->stamp < oldest) {
            oldest = tile->stamp;
            best = i;
        }
    }
    
    if (best < 0) best = 0;
    
    g_continental_cache.tiles[best].valid = 1u;
    g_continental_cache.tiles[best].tile_x = tile_x;
    g_continental_cache.tiles[best].tile_z = tile_z;
    g_continental_cache.tiles[best].stamp = ++g_continental_cache.stamp_clock;
    result = &g_continental_cache.tiles[best];
    
    LeaveCriticalSection(&g_continental_cache.lock);
    
    sdk_worldgen_build_continental_tile(wg, result);
    
    return result;
}

SdkWorldGenMacroTile* sdk_worldgen_shared_get_macro_tile(SdkWorldGen* wg, int wx, int wz)
{
    SdkWorldGenImpl* impl = (SdkWorldGenImpl*)wg->impl;
    int macro_x, macro_z, tile_x, tile_z;
    int i, best;
    uint32_t oldest;
    SdkWorldGenMacroTile* result = NULL;
    
    if (!g_cache_initialized || !wg || !impl) return NULL;
    
    macro_x = floor_div_local(wx, (int)impl->macro_cell_size);
    macro_z = floor_div_local(wz, (int)impl->macro_cell_size);
    tile_x = floor_div_local(macro_x, SDK_WORLDGEN_MACRO_TILE_SIZE);
    tile_z = floor_div_local(macro_z, SDK_WORLDGEN_MACRO_TILE_SIZE);
    
    EnterCriticalSection(&g_macro_cache.lock);
    
    for (i = 0; i < SHARED_MACRO_SLOTS; ++i) {
        SdkWorldGenMacroTile* tile = &g_macro_cache.tiles[i];
        if (tile->valid && tile->tile_x == tile_x && tile->tile_z == tile_z) {
            tile->stamp = ++g_macro_cache.stamp_clock;
            g_macro_cache.hits++;
            result = tile;
            LeaveCriticalSection(&g_macro_cache.lock);
            return result;
        }
    }
    
    g_macro_cache.misses++;
    
    best = -1;
    oldest = 0xffffffffu;
    for (i = 0; i < SHARED_MACRO_SLOTS; ++i) {
        SdkWorldGenMacroTile* tile = &g_macro_cache.tiles[i];
        if (!tile->valid) {
            best = i;
            break;
        }
        if (tile->stamp < oldest) {
            oldest = tile->stamp;
            best = i;
        }
    }
    
    if (best < 0) best = 0;
    
    g_macro_cache.tiles[best].valid = 1;
    g_macro_cache.tiles[best].tile_x = tile_x;
    g_macro_cache.tiles[best].tile_z = tile_z;
    g_macro_cache.tiles[best].stamp = ++g_macro_cache.stamp_clock;
    result = &g_macro_cache.tiles[best];
    
    LeaveCriticalSection(&g_macro_cache.lock);
    
    sdk_worldgen_build_macro_tile(wg, result);
    sdk_worldgen_run_hydrology(wg, result);
    
    return result;
}

SdkWorldGenRegionTile* sdk_worldgen_shared_get_region_tile(SdkWorldGen* wg, int wx, int wz)
{
    int tile_x, tile_z;
    int i, best;
    uint32_t oldest;
    SdkWorldGenRegionTile* result = NULL;
    
    if (!g_cache_initialized || !wg) return NULL;
    
    tile_x = floor_div_local(wx, SDK_WORLDGEN_REGION_TILE_BLOCKS);
    tile_z = floor_div_local(wz, SDK_WORLDGEN_REGION_TILE_BLOCKS);
    
    EnterCriticalSection(&g_region_cache.lock);
    
    for (i = 0; i < SHARED_REGION_SLOTS; ++i) {
        SdkWorldGenRegionTile* tile = &g_region_cache.tiles[i];
        if (tile->valid && tile->tile_x == tile_x && tile->tile_z == tile_z) {
            tile->stamp = ++g_region_cache.stamp_clock;
            g_region_cache.hits++;
            result = tile;
            LeaveCriticalSection(&g_region_cache.lock);
            return result;
        }
    }
    
    g_region_cache.misses++;
    
    best = -1;
    oldest = 0xffffffffu;
    for (i = 0; i < SHARED_REGION_SLOTS; ++i) {
        SdkWorldGenRegionTile* tile = &g_region_cache.tiles[i];
        if (!tile->valid) {
            best = i;
            break;
        }
        if (tile->stamp < oldest) {
            oldest = tile->stamp;
            best = i;
        }
    }
    
    if (best < 0) best = 0;
    
    g_region_cache.tiles[best].valid = 1;
    g_region_cache.tiles[best].tile_x = tile_x;
    g_region_cache.tiles[best].tile_z = tile_z;
    g_region_cache.tiles[best].stamp = ++g_region_cache.stamp_clock;
    result = &g_region_cache.tiles[best];
    
    LeaveCriticalSection(&g_region_cache.lock);
    
    sdk_worldgen_build_region_tile(wg, result);
    
    return result;
}

void sdk_worldgen_shared_get_stats(uint32_t* out_continental_hits,
                                    uint32_t* out_continental_misses,
                                    uint32_t* out_macro_hits,
                                    uint32_t* out_macro_misses,
                                    uint32_t* out_region_hits,
                                    uint32_t* out_region_misses)
{
    if (!g_cache_initialized) return;
    
    if (out_continental_hits) *out_continental_hits = g_continental_cache.hits;
    if (out_continental_misses) *out_continental_misses = g_continental_cache.misses;
    if (out_macro_hits) *out_macro_hits = g_macro_cache.hits;
    if (out_macro_misses) *out_macro_misses = g_macro_cache.misses;
    if (out_region_hits) *out_region_hits = g_region_cache.hits;
    if (out_region_misses) *out_region_misses = g_region_cache.misses;
}
