# SDK Worldgen Hydro Documentation

Comprehensive documentation for macro-tile drainage, coast distance calculation, and province refinement.

**Module:** `SDK/Core/World/Worldgen/Hydro/`  
**Source:** `sdk_worldgen_hydro.c`

## Table of Contents

- [Module Overview](#module-overview)
- [Drainage System](#drainage-system)
- [Coast Distance](#coast-distance)
- [Province Classification](#province-classification)
- [Water Table Calculation](#water-table-calculation)
- [Key Functions](#key-functions)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Hydro module runs on macro tiles after initial synthesis to refine hydrology. It calculates drainage networks, coast distances, and final province classifications based on the processed terrain and climate data.

**Key Features:**
- 8-direction flow routing (D8 algorithm)
- Flow accumulation and stream ordering
- Signed coast distance calculation
- Province family classification
- Detail amplitude assignment
- Water table depth estimation

**Called By:** `sdk_worldgen_build_macro_tile()` after initial synthesis

---

## Drainage System

### Flow Routing (D8 Algorithm)

```c
static const int g_dir8_dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const int g_dir8_dz[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

uint8_t compute_flow_direction(const SdkWorldGenMacroCell* cell,
                                int sea_level) {
    // Find lowest neighbor
    uint8_t best_dir = 255;  // Sink (no lower neighbor)
    float min_slope = 0;
    
    for (int d = 0; d < 8; d++) {
        // Check if neighbor is valid
        if (!in_bounds(nx, nz)) continue;
        
        float neighbor_height = get_height(nx, nz);
        float height_diff = cell->surface_height - neighbor_height;
        
        // Account for diagonal distance (sqrt(2) for diagonals)
        float dist = (d % 2 == 0) ? 1.0f : 1.414f;
        float slope = height_diff / dist;
        
        if (slope > min_slope) {
            min_slope = slope;
            best_dir = d;
        }
    }
    
    // If no downward slope, check for flat areas
    if (best_dir == 255 && is_land(cell, sea_level)) {
        best_dir = route_to_nearest_sink(cell);
    }
    
    return best_dir;
}
```

### Flow Accumulation

```c
void compute_flow_accumulation(uint32_t* accum, uint8_t* flow_dir) {
    // Initialize: each cell contributes 1 (itself)
    for (int i = 0; i < cell_count; i++) {
        accum[i] = 1;
    }
    
    // Sort cells by elevation (descending)
    int* sorted = sort_by_elevation_desc();
    
    // Process from high to low
    for (int i = 0; i < cell_count; i++) {
        int idx = sorted[i];
        uint8_t dir = flow_dir[idx];
        
        if (dir < 8) {  // Valid flow direction
            int nx = x + g_dir8_dx[dir];
            int nz = z + g_dir8_dz[dir];
            int downstream = idx(nx, nz);
            
            // Add this cell's accumulation to downstream
            accum[downstream] += accum[idx];
        }
    }
}
```

### Stream Ordering (Strahler)

```c
uint8_t compute_stream_order(uint32_t* flow_accum, uint8_t* flow_dir) {
    // Threshold for "stream" vs "hillslope"
    const uint32_t STREAM_THRESHOLD = 100;  // ~100 cells drain here
    
    if (flow_accum[idx] < STREAM_THRESHOLD) {
        return 0;  // Not a defined stream
    }
    
    // Count upstream streams
    int upstream_count = 0;
    uint8_t max_upstream_order = 0;
    
    for each neighbor:
        if (flow_dir[neighbor] points to this cell) {
            uint8_t order = stream_order[neighbor];
            if (order > 0) {
                upstream_count++;
                max_upstream_order = max(max_upstream_order, order);
            }
        }
    }
    
    // Strahler ordering
    if (upstream_count == 0) {
        return 1;  // First order (source)
    } else if (upstream_count == 1) {
        return max_upstream_order;  // Continue same order
    } else {
        return max_upstream_order + 1;  // Increase at confluence
    }
}
```

---

## Coast Distance

### Signed Distance Calculation

```c
void compute_coast_distance(int16_t* coast_dist, uint8_t* ocean_mask) {
    // Initialize
    for each cell:
        if (ocean_mask[idx]) {
            coast_dist[idx] = 0;  // At coast
        } else {
            coast_dist[idx] = INT16_MAX;  // Unknown
        }
    
    // Multi-pass distance transform
    bool changed;
    do {
        changed = false;
        
        for each cell not ocean:
            // Check 8 neighbors for shorter path
            for each direction:
                int nx = x + g_dir8_dx[d];
                int nz = z + g_dir8_dz[d];
                
                if (in_bounds(nx, nz)) {
                    float dist = (d % 2 == 0) ? 32.0f : 45.25f;
                    int16_t new_dist = coast_dist[neighbor] + (int16_t)dist;
                    
                    if (new_dist < coast_dist[idx]) {
                        coast_dist[idx] = new_dist;
                        changed = true;
                    }
                }
            }
        }
    } while (changed);
    
    // Negative for inland (distance from ocean)
    for each cell not ocean:
        coast_dist[idx] = -coast_dist[idx];
}
```

### Harbor Scoring

```c
uint8_t compute_harbor_score(int x, int z, int16_t* coast_dist) {
    // Good harbor = protected, deep water access
    
    float score = 0.0f;
    
    // Check coastal configuration
    int water_neighbors = 0;
    int land_neighbors = 0;
    
    for each direction:
        if (is_ocean(nx, nz)) water_neighbors++;
        else land_neighbors++;
    
    // Bay shape (land on 3 sides, water on 1)
    if (land_neighbors >= 3 && water_neighbors >= 1) {
        score += 100.0f;
    }
    
    // Deep water nearby
    if (coast_dist[idx] < -50) {  // >50m deep
        score += 50.0f;
    }
    
    return (uint8_t)min(255, (int)score);
}
```

---

## Province Classification

### Province Family Assignment

```c
uint8_t classify_province_family(SdkWorldGenMacroCell* cell) {
    // Marine/Coast
    if (cell->surface_height <= sea_level + 5) {
        return SDK_WORLDGEN_PROVINCE_FAMILY_MARINE_COAST;
    }
    
    // Wet lowland (floodplains, wetlands)
    if (cell->wetness > 0.7f && cell->relief_strength < 5) {
        return SDK_WORLDGEN_PROVINCE_FAMILY_WET_LOWLAND;
    }
    
    // Hardrock (mountains, highlands)
    if (cell->relief_strength > 10 || cell->surface_height > 150) {
        return SDK_WORLDGEN_PROVINCE_FAMILY_HARDROCK;
    }
    
    // Volcanic (vents, calderas)
    if (cell->vent_mask || cell->caldera_mask) {
        return SDK_WORLDGEN_PROVINCE_FAMILY_VOLCANIC;
    }
    
    // Default to basin/upland
    return SDK_WORLDGEN_PROVINCE_FAMILY_BASIN_UPLAND;
}
```

### Detail Amplitude by Province

```c
float compute_detail_amp(uint8_t province) {
    switch (province) {
        case TERRAIN_PROVINCE_OPEN_OCEAN:
            return 2.0f;   // Flat
        case TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND:
            return 2.0f;   // Flat
        case TERRAIN_PROVINCE_SILICICLASTIC_HILLS:
            return 10.0f;  // Rolling
        case TERRAIN_PROVINCE_HARDROCK_HIGHLAND:
            return 14.0f;  // Rugged
        case TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT:
            return 14.0f;  // Very rugged
        default:
            return 6.0f;   // Moderate
    }
}
```

---

## Water Table Calculation

```c
uint8_t compute_water_table_depth(SdkWorldGenMacroCell* cell) {
    // Water table closer to surface in:
    // - Low areas (valleys)
    // - Wet climates
    // - Poor drainage
    
    float base_depth = 10.0f;  // Default 10m
    
    // Elevation effect (higher = deeper)
    float elevation_factor = cell->surface_height / 100.0f;
    base_depth += elevation_factor * 5.0f;
    
    // Climate effect (wetter = shallower)
    float moisture_factor = (5 - cell->moisture_band) * 2.0f;
    base_depth -= moisture_factor;
    
    // Drainage effect (poor drainage = shallower)
    float drainage_factor = cell->drainage_class * 1.5f;
    base_depth -= drainage_factor;
    
    // Clamp to reasonable range
    return (uint8_t)clamp(base_depth, 1.0f, 50.0f);
}
```

---

## Key Functions

### Main Entry Point

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_run_hydrology` | `(SdkWorldGen*, SdkWorldGenMacroTile*) → void` | Run full hydrology pass on tile |

### Internal Functions

| Function | Description |
|----------|-------------|
| `route_flow` | D8 flow direction assignment |
| `accumulate_flow` | Drainage area accumulation |
| `order_streams` | Strahler stream ordering |
| `compute_coast_distance` | Signed distance to ocean |
| `compute_water_access` | Proximity to navigable water |
| `classify_provinces` | Assign province families |
| `compute_wetness` | Soil moisture estimation |

---

## Integration Notes

### In Macro Tile Build

```c
void sdk_worldgen_build_macro_tile(SdkWorldGen* wg, 
                                   SdkWorldGenMacroTile* tile) {
    // 1. Initial synthesis from continental
    for each cell:
        synthesize_from_continental(cell, ...);
    
    // 2. Run hydrology refinement
    sdk_worldgen_run_hydrology(wg, tile);
    
    // 3. Finalize
    finalize_macro_tile(tile);
}
```

### With River Generation

```c
// After hydrology, rivers are placed where flow_accum is high
void place_rivers_from_hydro(SdkWorldGenMacroTile* tile) {
    for each cell:
        if (tile->cells[idx].river_order > 0) {
            // Carve river channel
            int depth = tile->cells[idx].river_order * 2;
            tile->cells[idx].surface_height -= depth;
            
            // Set river channel properties
            tile->cells[idx].river_strength = 
                logf(tile->cells[idx].flow_accum) * 0.1f;
        }
    }
}
```

---

## AI Context Hints

### Custom Flow Routing

```c
// D-infinity (multiple flow directions) instead of D8
void route_flow_dinf(SdkWorldGenMacroTile* tile) {
    for each cell:
        // Find slope to all 8 neighbors
        float slopes[8];
        for d in 0..7:
            slopes[d] = compute_slope(cell, d);
        
        // Distribute flow proportionally to slope
        float total_slope = sum(slopes);  // Only positive slopes
        
        for d in 0..7:
            if (slopes[d] > 0) {
                flow_fraction[idx][d] = slopes[d] / total_slope;
            }
        }
}
```

### Wetness Index

```c
// Topographic Wetness Index (TWI)
float compute_twi(int x, int z, uint32_t flow_accum, float slope) {
    // TWI = ln(flow_accum / slope)
    if (slope < 0.001f) slope = 0.001f;
    
    float twi = logf(flow_accum / slope);
    
    // Normalize to 0-255
    return clamp((twi + 5.0f) / 15.0f * 255.0f, 0, 255);
}
```

### Flood Risk Assessment

```c
uint8_t assess_flood_risk(SdkWorldGenMacroCell* cell) {
    float risk = 0.0f;
    
    // Proximity to rivers
    if (cell->river_order > 0) {
        risk += cell->river_order * 20.0f;
    }
    
    // In floodplain
    if (cell->floodplain_mask > 0.5f) {
        risk += 50.0f;
    }
    
    // Poor drainage
    if (cell->drainage_class >= DRAINAGE_IMPERFECT) {
        risk += 30.0f;
    }
    
    // Low elevation
    if (cell->surface_height < 70) {  // Near sea level
        risk += 20.0f;
    }
    
    return (uint8_t)min(255, (int)risk);
}
```

---

## Related Documentation

- [SDK_Worldgen.md](../SDK_Worldgen.md) - Public API
- [SDK_WorldgenInternal.md](../Internal/SDK_WorldgenInternal.md) - Internal structures
- [SDK_WorldgenContinental.md](../Continental/SDK_WorldgenContinental.md) - Input data
- [SDK_WorldgenMacro.md](../Macro/SDK_WorldgenMacro.md) - Consumer
- [SDK_WorldgenColumn.md](../Column/SDK_WorldgenColumn.md) - Downstream

---

**Source Files:**
- `SDK/Core/World/Worldgen/Hydro/sdk_worldgen_hydro.c` (~1412 lines)
