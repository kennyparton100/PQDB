# SDK Worldgen Documentation

Comprehensive documentation for the SDK world generation system, covering the layered terrain pipeline and public API.

**Module:** `SDK/Core/World/Worldgen/`  
**Output:** `SDK/Docs/Core/World/Worldgen/SDK_Worldgen.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Generation Pipeline](#generation-pipeline)
- [Public API](#public-api)
- [Terrain Column Profile](#terrain-column-profile)
- [Debug Modes](#debug-modes)
- [Key Functions](#key-functions)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Worldgen module implements a geology-first terrain generation pipeline that progressively refines large-scale continental features into detailed chunk block data. It uses a tile-based caching system with continental, macro, and region layers.

**Key Features:**
- Layered generation (Continental → Macro → Region → Column → Chunk)
- Tile caching for performance
- Resource signature scanning
- Debug visualization modes
- Settlement integration

---

## Generation Pipeline

### Layer Order

```
1. SdkWorldDesc (world parameters)
   ↓
2. Continental Tiles (climate, hydrology, ocean mask)
   ↓
3. Macro Tiles (128×128 block synthesis + hydrology)
   ↓
4. Region Tiles (32×32 block geology, resources)
   ↓
5. Column Sampling (surface height, soil profiles)
   ↓
6. Chunk Fill (block placement)
   ↓
7. Settlement Overlay (terrain modifications)
   ↓
8. Wall Application (superchunk barriers)
```

### Layer Details

**Continental Layer:**
- Height and coast distance
- Ocean mask
- Precipitation and runoff
- Flatness analysis
- Lake and basin detection
- Water access scoring

**Macro Layer:**
- 128×128 block tiles with 8-block halo
- Terrain synthesis
- `sdk_worldgen_run_hydrology()` refinement

**Region Layer:**
- 32×32 block tiles
- Geology provinces
- Stratigraphy control
- Soil descriptors
- Water table depth
- Resource controls

**Column Sampling:**
- Produces `SdkTerrainColumnProfile`
- Surface height, bedrock depth
- Biome and ecology
- Resource deposits

**Chunk Fill:**
```
1. Clear chunk
2. Sample columns for bedrock, soil, sediment
3. Carve caves
4. Add water, rivers, lava
5. Place flora (trees, plants)
6. Apply settlement terrain
7. Seal water
8. Apply superchunk walls
```

---

## Public API

### Context Functions (Preferred)

```c
// Initialize with world descriptor
SdkWorldGen worldgen;
sdk_worldgen_init(&worldgen, &world_desc);

// Column sampling with context
SdkTerrainColumnProfile profile;
sdk_worldgen_sample_column_ctx(&worldgen, wx, wz, &profile);

// Surface height
int surface_y = sdk_worldgen_get_surface_y_ctx(&worldgen, wx, wz);

// Generate chunk
sdk_worldgen_generate_chunk_ctx(&worldgen, chunk);

// Shutdown
sdk_worldgen_shutdown(&worldgen);
```

### Global Functions (Legacy)

```c
// Uses internal singleton context
int surface_y = sdk_worldgen_get_surface_y(wx, wz);
void sdk_worldgen_generate_chunk(SdkChunk* chunk, uint32_t seed);
```

---

## Terrain Column Profile

### SdkTerrainColumnProfile Structure

```c
typedef struct {
    // Heights
    int16_t base_height;       // Bedrock surface
    int16_t surface_height;    // Ground level
    int16_t water_height;      // Water surface
    int16_t river_bed_height;  // River bed depth
    
    // Depths
    uint8_t soil_depth;
    uint8_t sediment_depth;
    uint8_t sediment_thickness;
    uint8_t regolith_thickness;
    
    // Hydrology
    uint8_t river_order;
    uint8_t floodplain_width;
    uint8_t water_table_depth;
    SdkSurfaceWaterClass water_surface_class;
    
    // Classification
    SdkTerrainProvince terrain_province;
    SdkBedrockProvince bedrock_province;
    SdkTemperatureBand temperature_band;
    SdkMoistureBand moisture_band;
    SdkBiomeEcology ecology;
    
    // Soil
    SdkSurfaceSediment surface_sediment;
    SdkParentMaterialClass parent_material;
    SdkDrainageClass drainage_class;
    SdkSoilReactionClass soil_reaction;
    SdkSoilFertilityClass soil_fertility;
    SdkSoilSalinityClass soil_salinity;
    
    // Resources
    SdkResourceProvince resource_province;
    SdkHydrocarbonClass hydrocarbon_class;
    uint8_t resource_grade;
    
    // Features
    uint32_t landform_flags;
} SdkTerrainColumnProfile;
```

### Key Profile Values

| Field | Range | Description |
|-------|-------|-------------|
| `base_height` | 0-1024 | Bedrock surface Y |
| `surface_height` | 0-1024 | Ground surface Y |
| `water_height` | 0-1024 | Water surface Y (if present) |
| `soil_depth` | 0-255 | Soil layer thickness |
| `water_table_depth` | 0-255 | Depth to water table |
| `resource_grade` | 0-255 | Resource quality (0 = none) |

---

## Debug Modes

### SdkWorldGenDebugMode

```c
typedef enum {
    SDK_WORLDGEN_DEBUG_OFF = 0,           // Normal generation
    SDK_WORLDGEN_DEBUG_FORMATIONS,        // Visualize geological formations
    SDK_WORLDGEN_DEBUG_STRUCTURES,        // Visualize structures
    SDK_WORLDGEN_DEBUG_BODIES             // Visualize ore/resource bodies
} SdkWorldGenDebugMode;
```

### Debug Colors

```c
// Get debug color for block position
uint32_t color = sdk_worldgen_get_debug_color(wx, wy, wz, actual_block);

// Usage in mesh building:
if (debug_color) {
    vertex.color = debug_color;
} else {
    vertex.color = block_color;
}
```

---

## Key Functions

### Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_init` | `(wg, desc) → void` | Initialize worldgen |
| `sdk_worldgen_init_ex` | `(wg, desc, cache_mode) → void` | Init with cache |
| `sdk_worldgen_shutdown` | `(wg) → void` | Cleanup worldgen |

### Generation

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_sample_column_ctx` | `(wg, wx, wz, out) → int` | Sample column |
| `sdk_worldgen_get_surface_y_ctx` | `(wg, wx, wz) → int` | Get surface height |
| `sdk_worldgen_generate_chunk_ctx` | `(wg, chunk) → void` | Generate chunk |
| `sdk_worldgen_scan_resource_signature` | `(wg, wx, wz, radius, out) → void` | Scan resources |

### Debug

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_set_debug_mode_ctx` | `(wg, mode) → void` | Set debug mode |
| `sdk_worldgen_get_debug_mode_ctx` | `(wg) → SdkWorldGenDebugMode` | Get debug mode |
| `sdk_worldgen_get_debug_color` | `(wx, wy, wz, block) → uint32_t` | Get debug color |

---

## API Surface

```c
#ifndef NQLSDK_WORLDGEN_H
#define NQLSDK_WORLDGEN_H

#include "../Chunks/sdk_chunk.h"
#include "../../sdk_types.h"
#include "sdk_worldgen_types.h"

#define SDK_WORLDGEN_MACRO_CELL_BLOCKS 32
#define SDK_WORLDGEN_MACRO_TILE_SIZE   128
#define SDK_WORLDGEN_MACRO_TILE_HALO   8
#define SDK_WORLDGEN_TILE_CACHE_SLOTS  8

typedef struct {
    SdkWorldDesc desc;
    void*        impl;
} SdkWorldGen;

typedef enum {
    SDK_WORLDGEN_CACHE_NONE = 0,
    SDK_WORLDGEN_CACHE_DISK = 1
} SdkWorldGenCacheMode;

typedef enum {
    SDK_WORLDGEN_DEBUG_OFF = 0,
    SDK_WORLDGEN_DEBUG_FORMATIONS,
    SDK_WORLDGEN_DEBUG_STRUCTURES,
    SDK_WORLDGEN_DEBUG_BODIES
} SdkWorldGenDebugMode;

/* Lifecycle */
void sdk_worldgen_init(SdkWorldGen* wg, const SdkWorldDesc* desc);
void sdk_worldgen_init_ex(SdkWorldGen* wg, const SdkWorldDesc* desc, 
                          SdkWorldGenCacheMode cache_mode);
void sdk_worldgen_shutdown(SdkWorldGen* wg);

/* Context generation */
int  sdk_worldgen_sample_column_ctx(SdkWorldGen* wg, int wx, int wz, 
                                    SdkTerrainColumnProfile* out_profile);
int  sdk_worldgen_get_surface_y_ctx(SdkWorldGen* wg, int wx, int wz);
void sdk_worldgen_generate_chunk_ctx(SdkWorldGen* wg, SdkChunk* chunk);
void sdk_worldgen_set_debug_mode_ctx(SdkWorldGen* wg, SdkWorldGenDebugMode mode);
SdkWorldGenDebugMode sdk_worldgen_get_debug_mode_ctx(const SdkWorldGen* wg);
void sdk_worldgen_scan_resource_signature(SdkWorldGen* wg, int center_wx, 
                                          int center_wz, int radius,
                                          SdkResourceSignature* out_signature);

/* Global functions (legacy) */
const SdkTerrainColumnProfile* sdk_worldgen_sample_column(int wx, int wz);
int  sdk_worldgen_get_surface_y(int wx, int wz);
void sdk_worldgen_generate_chunk(SdkChunk* chunk, uint32_t seed);
void sdk_worldgen_set_debug_mode(SdkWorldGenDebugMode mode);
SdkWorldGenDebugMode sdk_worldgen_get_debug_mode(void);
uint32_t sdk_worldgen_get_debug_color(int wx, int wy, int wz, BlockType actual_block);

#endif
```

---

## Integration Notes

### Basic Chunk Generation

```c
void generate_world_chunk(SdkChunk* chunk, int cx, int cz, 
                          const SdkWorldDesc* world_desc) {
    // Init worldgen
    SdkWorldGen worldgen;
    sdk_worldgen_init(&worldgen, world_desc);
    
    // Set chunk coordinates
    chunk->cx = cx;
    chunk->cz = cz;
    
    // Generate
    sdk_worldgen_generate_chunk_ctx(&worldgen, chunk);
    
    // Cleanup
    sdk_worldgen_shutdown(&worldgen);
}
```

### Settlement Integration

```c
void generate_chunk_with_settlement(SdkChunk* chunk, SdkWorldGen* wg,
                                     Settlement* settlement) {
    // 1. Generate base terrain
    sdk_worldgen_generate_chunk_ctx(wg, chunk);
    
    // 2. Apply settlement modifications
    if (settlement_overlaps_chunk(settlement, chunk->cx, chunk->cz)) {
        apply_settlement_terrain_to_chunk(chunk, settlement);
    }
    
    // 3. Apply walls
    if (chunk_is_wall_chunk(chunk->cx, chunk->cz)) {
        apply_superchunk_walls(chunk);
    }
}
```

### Resource Prospecting

```c
void prospect_area(SdkWorldGen* wg, int center_wx, int center_wz) {
    SdkResourceSignature sig;
    sdk_worldgen_scan_resource_signature(wg, center_wx, center_wz, 
                                          100, &sig);  // 100 block radius
    
    printf("Resource prospects:\n");
    printf("  Coal: %.1f\n", sig.coal_score);
    printf("  Iron: %.1f\n", sig.iron_score);
    printf("  Clay: %.1f\n", sig.clay_score);
    printf("  Oil:  %.1f\n", sig.hydrocarbon_score);
}
```

---

## AI Context Hints

### Custom Generation Layer

```c
// Add custom generation step
void generate_chunk_custom(SdkChunk* chunk, SdkWorldGen* wg) {
    // 1. Standard generation
    sdk_worldgen_generate_chunk_ctx(wg, chunk);
    
    // 2. Custom feature injection
    add_custom_features(chunk);
    
    // 3. Custom ore deposit
    if (should_spawn_custom_ore(chunk->cx, chunk->cz)) {
        spawn_custom_ore_deposit(chunk);
    }
}

void add_custom_features(SdkChunk* chunk) {
    // Add crystalline formations in caves
    for (int ly = 0; ly < 256; ly++) {
        for (int lz = 0; lz < CHUNK_DEPTH; lz++) {
            for (int lx = 0; lx < CHUNK_WIDTH; lx++) {
                BlockType type = sdk_chunk_get_block(chunk, lx, ly, lz);
                if (type == BLOCK_CAVE_AIR && is_cave_ceiling(chunk, lx, ly, lz)) {
                    if (random_chance(0.01f)) {
                        place_crystal_formation(chunk, lx, ly, lz);
                    }
                }
            }
        }
    }
}
```

### Biome Override

```c
// Force specific biome in area
void force_biome_in_radius(SdkWorldGen* wg, int center_wx, int center_wz,
                           int radius, SdkBiomeEcology target_biome) {
    for (int dz = -radius; dz <= radius; dz++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int wx = center_wx + dx * CHUNK_WIDTH;
            int wz = center_wz + dz * CHUNK_DEPTH;
            
            // Modify region tile directly (advanced)
            SdkRegionTile* tile = get_region_tile_at(wg, wx, wz);
            if (tile) {
                tile->ecology = target_biome;
                tile->modified = true;
            }
        }
    }
}
```

### Performance Profiling

```c
void profile_chunk_generation(SdkWorldGen* wg, int cx, int cz) {
    LARGE_INTEGER start, end, freq;
    QueryPerformanceFrequency(&freq);
    
    // Allocate chunk
    SdkChunk chunk;
    sdk_chunk_init(&chunk, cx, cz, NULL);
    
    // Time generation
    QueryPerformanceCounter(&start);
    sdk_worldgen_generate_chunk_ctx(wg, &chunk);
    QueryPerformanceCounter(&end);
    
    double ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
    printf("Chunk (%d, %d) generated in %.2f ms\n", cx, cz, ms);
    
    sdk_chunk_free(&chunk);
}
```

---

## Related Documentation

- `SDK_WorldgenTerrainPipeline.md` - Detailed pipeline documentation
- `SDK_WorldgenScheduler.md` - Async generation scheduling
- `SDK_WorldgenInternal.md` - Tile cache and internal systems
- `SDK_SuperChunks.md` - Wall application
- `SDK_Settlements.md` - Settlement overlay

---

**Source Files:**
- `SDK/Core/World/Worldgen/sdk_worldgen.h` (2,005 bytes) - Public API
- `SDK/Core/World/Worldgen/sdk_worldgen.c` (19,677 bytes) - Implementation
- `SDK/Core/World/Worldgen/sdk_worldgen_types.h` (7,141 bytes) - Type definitions
