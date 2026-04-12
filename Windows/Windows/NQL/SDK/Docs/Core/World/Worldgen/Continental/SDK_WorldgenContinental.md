# SDK Worldgen Continental Documentation

Comprehensive documentation for continental-scale climate, hydrology, and basin analysis.

**Module:** `SDK/Core/World/Worldgen/Continental/`  
**Source:** `sdk_worldgen_continental.c`

## Table of Contents

- [Module Overview](#module-overview)
- [Tile Structure](#tile-structure)
- [Generation Pipeline](#generation-pipeline)
- [Plate Tectonics](#plate-tectonics)
- [Climate Synthesis](#climate-synthesis)
- [Hydrology System](#hydrology-system)
- [Key Functions](#key-functions)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Continental module generates large-scale continental tiles (64×64 cells, 256m per cell = ~16×16 km coverage). These tiles provide the foundation for all downstream terrain generation, containing climate, hydrology, ocean boundaries, and basin structure.

**Key Features:**
- Plate tectonics simulation
- Global climate synthesis (temperature, precipitation)
- Full hydrology (flow accumulation, basin detection)
- Lake identification
- Coast distance and harbor analysis
- Water access scoring

**Output:** `SdkContinentalCell[6400]` per tile

---

## Tile Structure

### Coverage

| Property | Value |
|----------|-------|
| Cells per tile | 64 × 64 = 4,096 |
| Cell size | 256 blocks (meters) |
| Total coverage | 16,384 × 16,384 blocks (~16 km²) |
| Halo | 8 cells (2,048m) |
| Stride | 80 cells |
| Total cells (with halo) | 6,400 |

### SdkContinentalCell Fields

```c
typedef struct {
    // Heights
    int16_t  raw_height;         // Unprocessed terrain
    int16_t  filled_height;      // Post-fill (lakes, basins)
    int16_t  lake_level;         // Lake surface elevation
    int16_t  coast_distance;     // Distance to ocean (signed)
    
    // Flow
    int32_t  downstream_cx;      // Flow direction X
    int32_t  downstream_cz;      // Flow direction Z
    uint32_t flow_accum;         // Accumulated drainage area
    uint32_t basin_id;           // Watershed identifier
    
    // Lakes
    uint16_t lake_id;            // Lake identifier
    uint8_t  lake_mask;          // Part of lake
    uint8_t  closed_basin_mask;  // Endorheic basin
    
    // Masks
    uint8_t  land_mask;          // Is land
    uint8_t  ocean_mask;         // Is ocean
    
    // Climate
    uint8_t  precipitation;      // 0-255 rainfall
    uint8_t  runoff;             // 0-255 runoff
    
    // Derived scores
    uint8_t  trunk_river_order;  // Stream hierarchy
    uint8_t  water_access;       // 0-255 water proximity
    uint8_t  harbor_score;       // 0-255 natural harbor quality
    uint8_t  confluence_score;   // 0-255 river merge importance
    uint8_t  flood_risk;         // 0-255 flood potential
    uint8_t  buildable_flatness; // 0-255 settlement suitability
} SdkContinentalCell;
```

---

## Generation Pipeline

```
1. Plate Simulation
   - Generate continental plates
   - Assign hotspot/tectonic properties
   - Calculate base elevations
   
2. Height Synthesis
   - Raw height from plates + noise
   - Lake basin detection
   - Depression filling (hydro-enforcement)
   
3. Flow Routing
   - 8-direction flow direction
   - Flow accumulation (drainage area)
   - Basin identification
   - Lake finding (closed basins)
   
4. Climate Synthesis
   - Pseudo-latitude temperature
   - Continental/maritime influence
   - Precipitation from moisture transport
   - Runoff calculation
   
5. Scoring
   - River orders (Strahler)
   - Water access
   - Harbor quality
   - Buildable flatness
```

---

## Plate Tectonics

### ContinentalPlateSite

```c
typedef struct {
    float x;            // Position
    float z;
    float vx;           // Velocity
    float vz;
    uint8_t plate_class; // Craton, margin, etc.
    float hotspot;      // Thermal activity
} ContinentalPlateSite;
```

### Height Generation

```c
// Height from plate boundaries and hotspots
float compute_base_height(int cell_x, int cell_z, 
                          ContinentalPlateSite* plates, int plate_count) {
    float height = 0;
    
    for (int i = 0; i < plate_count; i++) {
        float dx = cell_x - plates[i].x;
        float dz = cell_z - plates[i].z;
        float dist = sqrtf(dx*dx + dz*dz);
        
        // Plate contribution falls off with distance
        height += plates[i].hotspot * expf(-dist * 0.01f);
    }
    
    return height;
}
```

---

## Climate Synthesis

### Pseudo-Latitude

The world uses a synthetic latitude system based on world Z coordinate:

```c
float pseudo_latitude_abs(int cell_z) {
    float world_z = cell_z * SDK_WORLDGEN_CONTINENT_CELL_BLOCKS;
    float phase = fmodf(world_z / 262144.0f, 2.0f);
    if (phase < 0.0f) phase += 2.0f;
    return fabsf(phase - 1.0f);  // 0 = equator, 1 = poles
}
```

This creates repeating climate zones (pseudo-planet concept).

### Temperature Calculation

```c
// Base temperature from latitude
float base_temp = 1.0f - pseudo_latitude;

// Continental effect (interiors cooler in winter)
float continental_factor = distance_from_ocean / 10000.0f;

// Final temperature
float temperature = base_temp - continental_factor * 0.1f;
```

### Precipitation Model

```c
// Moisture from ocean proximity and prevailing winds
float moisture = 0.0f;

if (land_mask) {
    // Distance from ocean reduces moisture
    float ocean_distance = max(0, -coast_distance);
    moisture = expf(-ocean_distance / 5000.0f);
    
    // Orographic lift (mountains increase precipitation)
    float relief = compute_relief(cell_x, cell_z);
    moisture += relief * 0.3f;
}
```

---

## Hydrology System

### Flow Direction (8-Direction D8 Algorithm)

```c
static const int g_dir8_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int g_dir8_dz[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

uint8_t compute_flow_direction(int x, int z, int16_t* heights) {
    int16_t my_height = heights[idx(x, z)];
    uint8_t best_dir = 255;
    int16_t min_height = my_height;
    
    for (int d = 0; d < 8; d++) {
        int nx = x + g_dir8_dx[d];
        int nz = z + g_dir8_dz[d];
        int16_t neighbor_h = heights[idx(nx, nz)];
        
        if (neighbor_h < min_height) {
            min_height = neighbor_h;
            best_dir = d;
        }
    }
    
    return best_dir;  // 255 = sink (no lower neighbor)
}
```

### Flow Accumulation

```c
// Count cells draining through each cell
void compute_flow_accum(uint32_t* accum, uint8_t* flow_dir, int cell_count) {
    // Initialize: each cell contributes 1
    for (int i = 0; i < cell_count; i++) {
        accum[i] = 1;
    }
    
    // Sort by elevation (descending)
    // Process high to low, adding to downstream
    
    for (int i = 0; i < cell_count; i++) {
        int idx = sorted_indices[i];  // Highest first
        uint8_t dir = flow_dir[idx];
        
        if (dir < 8) {
            int nx = x + g_dir8_dx[dir];
            int nz = z + g_dir8_dz[dir];
            int downstream_idx = idx(nx, nz);
            accum[downstream_idx] += accum[idx];
        }
    }
}
```

### Lake Detection (Closed Basins)

```c
// Identify cells with no downstream outlet
void find_closed_basins(uint8_t* closed_mask, uint8_t* flow_dir) {
    for each cell:
        if (flow_dir[cell] == 255) {  // No downstream
            closed_mask[cell] = 1;
            
            // Trace upstream to find basin extent
            flood_fill_upstream(cell, closed_mask);
        }
}

// Fill to create lake level
void fill_depressions(int16_t* heights, uint8_t* closed_mask) {
    for each closed basin:
        // Find spill elevation (lowest point on rim)
        int16_t spill_elevation = find_rim_lowest(basin);
        
        // Fill all cells in basin to spill elevation
        for each cell in basin:
            if (heights[cell] < spill_elevation) {
                filled_height[cell] = spill_elevation;
                lake_mask[cell] = 1;
            }
        }
}
```

---

## Key Functions

### Public API (via SdkWorldGen)

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_build_continental_tile` | `(SdkWorldGen*, SdkWorldGenContinentalTile*) → void` | Generate tile contents |
| `sdk_worldgen_get_continental_cell` | `(SdkWorldGen*, int, int) → SdkContinentalCell*` | Get cell at coordinates |
| `sdk_worldgen_sample_continental_state` | `(SdkWorldGen*, int, int, SdkContinentalSample*) → void` | Sample with interpolation |

### Internal Functions

| Function | Description |
|----------|-------------|
| `build_plate_sites` | Generate continental plate configuration |
| `compute_base_elevation` | Height from plates and noise |
| `route_flow` | D8 flow direction routing |
| `accumulate_flow` | Flow accumulation calculation |
| `find_lakes` | Closed basin detection and filling |
| `synthesize_climate` | Temperature and precipitation |
| `compute_access_scores` | Harbor, water access, buildability |

---

## Integration Notes

### With Disk Cache

Continental tiles are expensive to generate and are prime candidates for caching:

```c
void get_or_build_continental_tile(SdkWorldGen* wg, int tx, int tz) {
    SdkWorldGenContinentalTile* tile = alloc_tile();
    
    // Try disk cache first
    if (tile_cache_load(wg->tile_cache, tx, tz, tile->cells)) {
        tile->valid = true;
        return tile;
    }
    
    // Build from scratch
    tile->tile_x = tx;
    tile->tile_z = tz;
    sdk_worldgen_build_continental_tile(wg, tile);
    
    // Save to disk cache
    tile_cache_save(wg->tile_cache, tx, tz, tile->cells);
    
    return tile;
}
```

### With Macro Tiles

Macro tiles are synthesized from continental samples:

```c
void build_macro_from_continental(SdkWorldGen* wg, SdkWorldGenMacroTile* macro) {
    for (int z = 0; z < SDK_WORLDGEN_TILE_STRIDE; z++) {
        for (int x = 0; x < SDK_WORLDGEN_TILE_STRIDE; x++) {
            int wx = macro->tile_x * 128 + x;
            int wz = macro->tile_z * 128 + z;
            
            // Sample continental at this position
            SdkContinentalSample sample;
            sdk_worldgen_sample_continental_state(wg, wx, wz, &sample);
            
            // Synthesize macro cell from continental sample
            synthesize_macro_cell(&macro->cells[idx(x,z)], &sample);
        }
    }
}
```

---

## AI Context Hints

### Custom Climate Model

```c
// Add monsoon effect to precipitation
void apply_monsoon_effect(float* precipitation, int cell_x, int cell_z,
                          float season_strength) {
    // Prevailing wind direction
    float wind_x = 1.0f;  // East to west
    float wind_z = 0.0f;
    
    // Check upwind for ocean
    int upwind_x = cell_x - (int)(wind_x * 10);
    int upwind_z = cell_z - (int)(wind_z * 10);
    
    if (is_ocean(upwind_x, upwind_z)) {
        // Monsoon rain
        *precipitation += season_strength * 0.5f;
    }
}
```

### Volcanic Activity

```c
// Add hotspot tracks
void add_hotspot_track(int16_t* heights, 
                       float start_x, float start_z,
                       float end_x, float end_z,
                       float intensity) {
    // Interpolate along track
    for (float t = 0; t <= 1.0f; t += 0.001f) {
        float x = lerp(start_x, end_x, t);
        float z = lerp(start_z, end_z, t);
        
        // Age decreases with t
        float age = t;
        float current_intensity = intensity * (1.0f - age * 0.7f);
        
        // Raise terrain
        raise_terrain_gaussian(heights, x, z, current_intensity, 1000.0f);
    }
}
```

### Custom Lake Generation

```c
// Create specific lake shapes
void create_reservoir_lake(int16_t* heights, int center_x, int center_z,
                         int radius, int target_level) {
    for (int dz = -radius; dz <= radius; dz++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int x = center_x + dx;
            int z = center_z + dz;
            int idx = z * stride + x;
            
            // Distance from center
            float dist = sqrtf(dx*dx + dz*dz);
            if (dist > radius) continue;
            
            // Dam creates flat lake surface
            if (heights[idx] < target_level) {
                heights[idx] = target_level;
                lake_mask[idx] = 1;
            }
        }
    }
}
```

### Erosion Simulation

```c
// Simple fluvial erosion
void apply_fluvial_erosion(int16_t* heights, uint32_t* flow_accum) {
    for each cell:
        // More water = more erosion
        float erosion = logf(1.0f + flow_accum[idx] * 0.001f) * 0.1f;
        
        // Don't erode below base level
        heights[idx] = max(base_level, heights[idx] - (int16_t)erosion);
}
```

---

## Related Documentation

- [SDK_Worldgen.md](../SDK_Worldgen.md) - Public API
- [SDK_WorldgenTypes.md](../Types/SDK_WorldgenTypes.md) - Type definitions
- [SDK_WorldgenInternal.md](../Internal/SDK_WorldgenInternal.md) - Internal structures
- [SDK_WorldgenHydro.md](../Hydro/SDK_WorldgenHydro.md) - Hydrology details
- [SDK_WorldgenMacro.md](../Macro/SDK_WorldgenMacro.md) - Macro tile synthesis
- [SDK_WorldgenTileCache.md](../TileCache/SDK_WorldgenTileCache.md) - Disk caching

---

**Source Files:**
- `SDK/Core/World/Worldgen/Continental/sdk_worldgen_continental.c` (~1232 lines)
