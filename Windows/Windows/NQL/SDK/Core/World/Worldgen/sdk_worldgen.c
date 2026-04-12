/**
 * sdk_worldgen.c -- World generation core
 */
#include "sdk_worldgen.h"
#include "Internal/sdk_worldgen_internal.h"
#include "TileCache/sdk_worldgen_tile_cache.h"
#include "SharedCache/sdk_worldgen_shared_cache.h"
#include "ConstructionCells/sdk_worldgen_construction_cells.h"
#include "../Settlements/sdk_settlement.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static SdkWorldGen g_default_worldgen;
static int g_default_worldgen_ready = 0;
static uint32_t g_default_worldgen_seed = 0;
static SdkTerrainColumnProfile g_default_profile;
static SdkWorldGen* g_active_worldgen_ctx = NULL;
static SdkWorldGenDebugMode g_worldgen_debug_mode = SDK_WORLDGEN_DEBUG_OFF;

typedef struct {
    int idx;
    int16_t h;
} SdkWorldGenContinentHeapEntry;

typedef struct {
    int idx;
    int16_t filled;
    int16_t raw;
    int16_t coast;
} SdkWorldGenContinentSortEntry;

static int worldgen_floor_div(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

uint32_t sdk_worldgen_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint32_t sdk_worldgen_hash2d(int x, int z, uint32_t seed)
{
    uint32_t h = seed;
    h ^= sdk_worldgen_hash32((uint32_t)x + 0x9e3779b9u);
    h ^= sdk_worldgen_hash32((uint32_t)z + 0x85ebca6bu + h);
    return h;
}

float sdk_worldgen_hashf(uint32_t h)
{
    return (float)(h & 0x00ffffffu) / (float)0x00ffffffu;
}

float sdk_worldgen_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int sdk_worldgen_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float smoothstepf(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float sdk_worldgen_value_noise(float x, float z, uint32_t seed)
{
    int ix = (int)floorf(x);
    int iz = (int)floorf(z);
    float fx = x - (float)ix;
    float fz = z - (float)iz;
    float sx = smoothstepf(fx);
    float sz = smoothstepf(fz);

    float v00 = sdk_worldgen_hashf(sdk_worldgen_hash2d(ix,     iz,     seed)) * 2.0f - 1.0f;
    float v10 = sdk_worldgen_hashf(sdk_worldgen_hash2d(ix + 1, iz,     seed)) * 2.0f - 1.0f;
    float v01 = sdk_worldgen_hashf(sdk_worldgen_hash2d(ix,     iz + 1, seed)) * 2.0f - 1.0f;
    float v11 = sdk_worldgen_hashf(sdk_worldgen_hash2d(ix + 1, iz + 1, seed)) * 2.0f - 1.0f;

    float vx0 = v00 * (1.0f - sx) + v10 * sx;
    float vx1 = v01 * (1.0f - sx) + v11 * sx;
    return vx0 * (1.0f - sz) + vx1 * sz;
}

float sdk_worldgen_fbm(float x, float z, uint32_t seed, int octaves)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    int i;

    for (i = 0; i < octaves; ++i) {
        total += sdk_worldgen_value_noise(x * frequency, z * frequency, seed + (uint32_t)i * 131u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    if (norm <= 0.0f) return 0.0f;
    return total / norm;
}

float sdk_worldgen_ridged(float x, float z, uint32_t seed, int octaves)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float norm = 0.0f;
    int i;

    for (i = 0; i < octaves; ++i) {
        float n = sdk_worldgen_value_noise(x * frequency, z * frequency, seed + (uint32_t)i * 193u);
        total += (1.0f - fabsf(n)) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    if (norm <= 0.0f) return 0.0f;
    return total / norm;
}

static void sdk_worldgen_apply_defaults(SdkWorldDesc* out_desc, const SdkWorldDesc* desc)
{
    memset(out_desc, 0, sizeof(*out_desc));
    if (desc) *out_desc = *desc;
    if (out_desc->seed == 0u) out_desc->seed = 0x12345678u;
    if (out_desc->sea_level == 0) out_desc->sea_level = 192;
    if (out_desc->macro_cell_size == 0u) out_desc->macro_cell_size = SDK_WORLDGEN_MACRO_CELL_BLOCKS;
    if (!desc) {
        out_desc->settlements_enabled = true;
        out_desc->construction_cells_enabled = false;
    }
}

static void free_continent_scratch(SdkWorldGenImpl* impl)
{
    SdkWorldGenContinentScratch* scratch;

    if (!impl || !impl->continent_scratch) return;
    scratch = impl->continent_scratch;
    free(scratch->raw_height);
    free(scratch->filled_height);
    free(scratch->coast_distance);
    free(scratch->ocean_mask);
    free(scratch->precipitation);
    free(scratch->runoff);
    free(scratch->flatness);
    free(scratch->downstream);
    free(scratch->flow_accum);
    free(scratch->basin_id);
    free(scratch->trunk_order);
    free(scratch->lake_mask);
    free(scratch->closed_mask);
    free(scratch->lake_level);
    free(scratch->lake_id);
    free(scratch->water_access);
    free(scratch->harbor_score);
    free(scratch->confluence);
    free(scratch->flood_risk);
    free(scratch->queue);
    free(scratch->visited);
    free(scratch->heap);
    free(scratch->sort_entries);
    free(scratch->stack);
    free(impl->continent_scratch);
    impl->continent_scratch = NULL;
}

static int allocate_continent_scratch(SdkWorldGenImpl* impl)
{
    SdkWorldGenContinentScratch* scratch;

    if (!impl) return 0;
    
    impl->continent_scratch = (SdkWorldGenContinentScratch*)calloc(1, sizeof(SdkWorldGenContinentScratch));
    if (!impl->continent_scratch) return 0;
    scratch = impl->continent_scratch;

    scratch->raw_height = (int16_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(int16_t));
    scratch->filled_height = (int16_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(int16_t));
    scratch->coast_distance = (int16_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(int16_t));
    scratch->ocean_mask = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->precipitation = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->runoff = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->flatness = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->downstream = (int*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(int));
    scratch->flow_accum = (uint32_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint32_t));
    scratch->basin_id = (uint32_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint32_t));
    scratch->trunk_order = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->lake_mask = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->closed_mask = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->lake_level = (int16_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(int16_t));
    scratch->lake_id = (uint16_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint16_t));
    scratch->water_access = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->harbor_score = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->confluence = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->flood_risk = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->queue = (int*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(int));
    scratch->visited = (uint8_t*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(uint8_t));
    scratch->heap = calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(SdkWorldGenContinentHeapEntry));
    scratch->sort_entries = calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(SdkWorldGenContinentSortEntry));
    scratch->stack = (int*)calloc(SDK_WORLDGEN_CONTINENT_ANALYSIS_COUNT, sizeof(int));

    if (!scratch->raw_height || !scratch->filled_height || !scratch->coast_distance ||
        !scratch->ocean_mask || !scratch->precipitation || !scratch->runoff ||
        !scratch->flatness || !scratch->downstream || !scratch->flow_accum ||
        !scratch->basin_id || !scratch->trunk_order || !scratch->lake_mask ||
        !scratch->closed_mask || !scratch->lake_level || !scratch->lake_id ||
        !scratch->water_access || !scratch->harbor_score || !scratch->confluence ||
        !scratch->flood_risk || !scratch->queue || !scratch->visited ||
        !scratch->heap || !scratch->sort_entries || !scratch->stack) {
        free_continent_scratch(impl);
        return 0;
    }

    return 1;
}

void sdk_worldgen_init_ex(SdkWorldGen* wg, const SdkWorldDesc* desc, SdkWorldGenCacheMode cache_mode)
{
    SdkWorldGenImpl* impl;
    SdkWorldDesc world_desc;
    int i;

    if (!wg) return;

    memset(wg, 0, sizeof(*wg));
    sdk_worldgen_apply_defaults(&world_desc, desc);
    wg->desc = world_desc;

    impl = (SdkWorldGenImpl*)calloc(1, sizeof(SdkWorldGenImpl));
    if (!impl) return;

    impl->seed = wg->desc.seed;
    impl->sea_level = wg->desc.sea_level;
    impl->macro_cell_size = wg->desc.macro_cell_size;
    impl->stamp_clock = 1u;
    impl->world_path[0] = '\0';
    impl->settlements_enabled = wg->desc.settlements_enabled;
    impl->construction_cells_enabled = wg->desc.construction_cells_enabled;

    if (cache_mode == SDK_WORLDGEN_CACHE_DISK) {
        impl->tile_cache = malloc(sizeof(SdkWorldGenTileCache));
        if (impl->tile_cache) {
            sdk_worldgen_tile_cache_init((SdkWorldGenTileCache*)impl->tile_cache, wg->desc.seed, NULL);
        }
    } else {
        impl->tile_cache = NULL;
    }

    if (allocate_continent_scratch(impl) == 0) {
        if (impl->tile_cache) {
            sdk_worldgen_tile_cache_shutdown((SdkWorldGenTileCache*)impl->tile_cache);
            free(impl->tile_cache);
        }
        free(impl);
        return;
    }

    for (i = 0; i < SDK_SETTLEMENT_CACHE_SLOTS; ++i) {
        impl->settlement_cache[i] = NULL;
        impl->settlement_cache_scx[i] = 0;
        impl->settlement_cache_scz[i] = 0;
        impl->settlement_cache_stamps[i] = 0;
    }

    wg->impl = impl;
    g_active_worldgen_ctx = wg;
}

void sdk_worldgen_init(SdkWorldGen* wg, const SdkWorldDesc* desc)
{
    sdk_worldgen_init_ex(wg, desc, SDK_WORLDGEN_CACHE_DISK);
}

void sdk_worldgen_shutdown(SdkWorldGen* wg)
{
    SdkWorldGenImpl* impl;
    int i;

    if (!wg || !wg->impl) return;

    impl = (SdkWorldGenImpl*)wg->impl;

    sdk_settlement_flush_cache(wg);
    for (i = 0; i < SDK_SETTLEMENT_CACHE_SLOTS; ++i) {
        free(impl->settlement_cache[i]);
        impl->settlement_cache[i] = NULL;
        impl->settlement_cache_stamps[i] = 0;
    }
    
    if (impl->tile_cache) {
        sdk_worldgen_tile_cache_shutdown((SdkWorldGenTileCache*)impl->tile_cache);
        free(impl->tile_cache);
        impl->tile_cache = NULL;  
    }
    
    free_continent_scratch(impl);
    free(impl);
    wg->impl = NULL;
    if (g_active_worldgen_ctx == wg) {
        g_active_worldgen_ctx = NULL;
        g_worldgen_debug_mode = SDK_WORLDGEN_DEBUG_OFF;
    }
    memset(&wg->desc, 0, sizeof(wg->desc));
}

static SdkWorldGenImpl* worldgen_impl(SdkWorldGen* wg)
{
    if (!wg) return NULL;
    return (SdkWorldGenImpl*)wg->impl;
}

SdkWorldGenMacroTile* sdk_worldgen_require_macro_tile(SdkWorldGen* wg, int wx, int wz)
{
    return sdk_worldgen_shared_get_macro_tile(wg, wx, wz);
}

SdkWorldGenRegionTile* sdk_worldgen_require_region_tile(SdkWorldGen* wg, int wx, int wz)
{
    return sdk_worldgen_shared_get_region_tile(wg, wx, wz);
}

int sdk_worldgen_sample_column_ctx(SdkWorldGen* wg, int wx, int wz, SdkTerrainColumnProfile* out_profile)
{
    SdkWorldGenRegionTile* tile;
    if (!wg || !wg->impl || !out_profile) return 0;
    tile = sdk_worldgen_require_region_tile(wg, wx, wz);
    if (!tile) return 0;
    sdk_worldgen_sample_column_from_region_tile(tile, wg, wx, wz, out_profile);
    return 1;
}

int sdk_worldgen_get_surface_y_ctx(SdkWorldGen* wg, int wx, int wz)
{
    SdkTerrainColumnProfile profile;
    if (!sdk_worldgen_sample_column_ctx(wg, wx, wz, &profile)) return 0;
    return (int)profile.surface_height;
}

void sdk_worldgen_generate_chunk_ctx(SdkWorldGen* wg, SdkChunk* chunk)
{
    if (!wg || !wg->impl || !chunk) return;
    sdk_worldgen_fill_chunk(wg, chunk);
}

void sdk_worldgen_set_debug_mode_ctx(SdkWorldGen* wg, SdkWorldGenDebugMode mode)
{
    if (!wg || !wg->impl) return;
    if ((int)mode < (int)SDK_WORLDGEN_DEBUG_OFF || (int)mode > (int)SDK_WORLDGEN_DEBUG_BODIES) {
        mode = SDK_WORLDGEN_DEBUG_OFF;
    }
    g_active_worldgen_ctx = wg;
    g_worldgen_debug_mode = mode;
}

SdkWorldGenDebugMode sdk_worldgen_get_debug_mode_ctx(const SdkWorldGen* wg)
{
    if (!wg || !wg->impl) return SDK_WORLDGEN_DEBUG_OFF;
    return g_worldgen_debug_mode;
}

void sdk_worldgen_scan_resource_signature(SdkWorldGen* wg, int center_wx, int center_wz, int radius, SdkResourceSignature* out_signature)
{
    static const int timber_ecologies[] = {
        ECOLOGY_RIPARIAN_FOREST,
        ECOLOGY_TEMPERATE_DECIDUOUS_FOREST,
        ECOLOGY_TEMPERATE_CONIFER_FOREST,
        ECOLOGY_BOREAL_TAIGA,
        ECOLOGY_TROPICAL_SEASONAL_FOREST,
        ECOLOGY_TROPICAL_RAINFOREST,
        ECOLOGY_SAVANNA_GRASSLAND,
        ECOLOGY_MANGROVE_SWAMP
    };
    int step;
    int samples = 0;
    int dx;
    int dz;

    if (!out_signature) return;
    memset(out_signature, 0, sizeof(*out_signature));
    if (!wg || !wg->impl) return;

    step = sdk_worldgen_clampi(radius / 2, 24, 96);
    for (dz = -radius; dz <= radius; dz += step) {
        for (dx = -radius; dx <= radius; dx += step) {
            SdkTerrainColumnProfile profile;
            float hydrocarbon_weight = 0.0f;
            float timber_weight = 0.0f;
            int i;

            if (!sdk_worldgen_sample_column_ctx(wg, center_wx + dx, center_wz + dz, &profile)) continue;
            ++samples;

            switch (profile.hydrocarbon_class) {
                case SDK_HYDROCARBON_OIL_SHALE:   hydrocarbon_weight = 0.45f; break;
                case SDK_HYDROCARBON_OIL_SAND:    hydrocarbon_weight = 0.58f; break;
                case SDK_HYDROCARBON_CRUDE_OIL:   hydrocarbon_weight = 0.82f; break;
                case SDK_HYDROCARBON_NATURAL_GAS: hydrocarbon_weight = 0.95f; break;
                case SDK_HYDROCARBON_NONE:
                default: break;
            }
            if (profile.resource_province == RESOURCE_PROVINCE_OIL_FIELD) {
                out_signature->hydrocarbon_score += hydrocarbon_weight + sdk_worldgen_unpack_unorm8(profile.resource_grade) * 0.35f;
            }
            if (profile.resource_province == RESOURCE_PROVINCE_COALFIELD) {
                out_signature->coal_score += 0.85f;
            }
            if (profile.resource_province == RESOURCE_PROVINCE_IRON_BELT ||
                profile.resource_province == RESOURCE_PROVINCE_STRATEGIC_ALLOY_BELT) {
                out_signature->iron_score += 0.75f;
            }
            if (profile.resource_province == RESOURCE_PROVINCE_CARBONATE_CEMENT_DISTRICT ||
                profile.bedrock_province == BEDROCK_PROVINCE_CARBONATE_PLATFORM ||
                profile.parent_material == PARENT_MATERIAL_CARBONATE) {
                out_signature->carbonate_score += 0.72f;
            }
            if (profile.resource_province == RESOURCE_PROVINCE_CLAY_DISTRICT ||
                profile.surface_sediment == SEDIMENT_FINE_ALLUVIUM ||
                profile.surface_sediment == SEDIMENT_LACUSTRINE_CLAY ||
                profile.surface_sediment == SEDIMENT_DELTAIC_SILT) {
                out_signature->clay_score += 0.70f;
            }
            if (profile.surface_sediment == SEDIMENT_BEACH_SAND ||
                profile.surface_sediment == SEDIMENT_EOILIAN_SAND ||
                profile.surface_sediment == SEDIMENT_MARINE_SAND ||
                profile.bedrock_province == BEDROCK_PROVINCE_SILICICLASTIC_BASIN) {
                out_signature->sand_score += 0.62f;
            }
            for (i = 0; i < (int)(sizeof(timber_ecologies) / sizeof(timber_ecologies[0])); ++i) {
                if ((int)profile.ecology == timber_ecologies[i]) {
                    timber_weight = 0.65f;
                    if (profile.ecology == ECOLOGY_TEMPERATE_DECIDUOUS_FOREST ||
                        profile.ecology == ECOLOGY_TEMPERATE_CONIFER_FOREST ||
                        profile.ecology == ECOLOGY_TROPICAL_SEASONAL_FOREST ||
                        profile.ecology == ECOLOGY_TROPICAL_RAINFOREST) {
                        timber_weight = 0.82f;
                    }
                    break;
                }
            }
            out_signature->timber_score += timber_weight;
            if (profile.resource_province == RESOURCE_PROVINCE_SULFUR_VOLCANIC_DISTRICT ||
                profile.terrain_province == TERRAIN_PROVINCE_VOLCANIC_ARC) {
                out_signature->sulfur_score += 0.80f;
            }
        }
    }

    if (samples <= 0) return;
    out_signature->hydrocarbon_score = sdk_worldgen_clampf(out_signature->hydrocarbon_score / (float)samples, 0.0f, 1.0f);
    out_signature->coal_score = sdk_worldgen_clampf(out_signature->coal_score / (float)samples, 0.0f, 1.0f);
    out_signature->iron_score = sdk_worldgen_clampf(out_signature->iron_score / (float)samples, 0.0f, 1.0f);
    out_signature->carbonate_score = sdk_worldgen_clampf(out_signature->carbonate_score / (float)samples, 0.0f, 1.0f);
    out_signature->clay_score = sdk_worldgen_clampf(out_signature->clay_score / (float)samples, 0.0f, 1.0f);
    out_signature->sand_score = sdk_worldgen_clampf(out_signature->sand_score / (float)samples, 0.0f, 1.0f);
    out_signature->timber_score = sdk_worldgen_clampf(out_signature->timber_score / (float)samples, 0.0f, 1.0f);
    out_signature->sulfur_score = sdk_worldgen_clampf(out_signature->sulfur_score / (float)samples, 0.0f, 1.0f);
}

static void ensure_default_worldgen(uint32_t seed)
{
    SdkWorldDesc desc;
    if (!g_default_worldgen_ready || g_default_worldgen_seed != seed) {
        desc.seed = seed;
        desc.sea_level = 192;
        desc.macro_cell_size = SDK_WORLDGEN_MACRO_CELL_BLOCKS;
        sdk_worldgen_shutdown(&g_default_worldgen);
        sdk_worldgen_init(&g_default_worldgen, &desc);
        g_default_worldgen_seed = seed;
        g_default_worldgen_ready = (g_default_worldgen.impl != NULL);
    }
}

const SdkTerrainColumnProfile* sdk_worldgen_sample_column(int wx, int wz)
{
    ensure_default_worldgen(0x12345678u);
    if (!g_default_worldgen_ready) return NULL;
    if (!sdk_worldgen_sample_column_ctx(&g_default_worldgen, wx, wz, &g_default_profile)) return NULL;
    return &g_default_profile;
}

int sdk_worldgen_get_surface_y(int wx, int wz)
{
    ensure_default_worldgen(0x12345678u);
    if (!g_default_worldgen_ready) return 0;
    return sdk_worldgen_get_surface_y_ctx(&g_default_worldgen, wx, wz);
}

void sdk_worldgen_generate_chunk(SdkChunk* chunk, uint32_t seed)
{
    ensure_default_worldgen(seed);
    if (!g_default_worldgen_ready) return;
    sdk_worldgen_generate_chunk_ctx(&g_default_worldgen, chunk);
}

void sdk_worldgen_set_debug_mode(SdkWorldGenDebugMode mode)
{
    if (!g_active_worldgen_ctx || !g_active_worldgen_ctx->impl) return;
    sdk_worldgen_set_debug_mode_ctx(g_active_worldgen_ctx, mode);
}

SdkWorldGenDebugMode sdk_worldgen_get_debug_mode(void)
{
    return g_worldgen_debug_mode;
}

uint32_t sdk_worldgen_get_debug_color(int wx, int wy, int wz, BlockType actual_block)
{
    if (!g_active_worldgen_ctx || !g_active_worldgen_ctx->impl) return 0u;
    if (g_worldgen_debug_mode == SDK_WORLDGEN_DEBUG_OFF) return 0u;
    return sdk_worldgen_debug_color_ctx_impl(g_active_worldgen_ctx, wx, wy, wz, actual_block);
}
