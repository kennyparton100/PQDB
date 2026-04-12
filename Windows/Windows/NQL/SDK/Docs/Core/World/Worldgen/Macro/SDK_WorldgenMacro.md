# SDK Worldgen Macro Documentation

Comprehensive documentation for macro tile generation (128×128 block synthesis).

**Module:** `SDK/Core/World/Worldgen/Macro/`  
**Source:** `sdk_worldgen_macro.c`

## Table of Contents

- [Module Overview](#module-overview)
- [Tile Structure](#tile-structure)
- [Generation Pipeline](#generation-pipeline)
- [Key Functions](#key-functions)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Macro module synthesizes 128×128 block tiles from continental samples. These tiles provide the intermediate layer between continental-scale features and region-scale geology, adding local relief, detail, and refined climate/hydrology.

**Key Features:**
- 128×128 block tiles with 8-block halo
- Synthesis from continental tile samples
- Terrain detail generation
- Province family classification
- Relief and amplitude control
- Hydrology refinement integration

**Output:** `SdkWorldGenMacroCell[20,736]` per tile (144×144 with halo)

---

## Tile Structure

### Coverage

| Property | Value |
|----------|-------|
| Core size | 128 × 128 blocks |
| Halo | 8 blocks |
| Total stride | 144 blocks |
| Total cells | 20,736 |
| Cell size | 32 blocks |

### SdkWorldGenMacroCell

See [SDK_WorldgenInternal.md](../Internal/SDK_WorldgenInternal.md) for full structure.

Key fields populated by macro generation:
- `surface_height` - Final terrain elevation
- `base_height` - Bedrock elevation
- `detail_amp` - Local relief amplitude
- `relief_strength` - Terrain roughness
- `province_family` - Terrain classification group
- `river_strength` - Channel incision depth
- `wetness` - Soil moisture

---

## Generation Pipeline

```
1. Sample Continental Data
   For each macro cell (32m grid):
   - Sample continental tiles at world position
   - Interpolate between continental cells
   - Get: height, climate, flow data
   
2. Synthesize Base Terrain
   - Apply continental base height
   - Add fractal detail noise
   - Scale by detail_amp from province
   
3. Province Classification
   - Classify province family
   - Assign terrain characteristics
   - Set relief/amplitude values
   
4. Refine Hydrology
   - Transfer flow data
   - Refine river channels
   - Set river_strength
   
5. Set Derived Fields
   - Temperature/moisture bands
   - Ecology from climate
   - Resource province hints
```

---

## Key Functions

### Build Function

```c
void sdk_worldgen_build_macro_tile(SdkWorldGen* wg, 
                                   SdkWorldGenMacroTile* tile);
```

Main entry point. Populates all cells in the tile.

### Synthesis Helper

```c
void synthesize_macro_cell(SdkWorldGenMacroCell* cell,
                           SdkContinentalSample* sample,
                           int lx, int lz,  // Local tile coordinates
                           uint32_t seed);
```

Creates a single macro cell from continental sample data.

### Province Family Classification

```c
uint8_t classify_province_family(SdkContinentalSample* sample);
```

Maps continental data to province family:
- `SDK_WORLDGEN_PROVINCE_FAMILY_MARINE_COAST`
- `SDK_WORLDGEN_PROVINCE_FAMILY_WET_LOWLAND`
- `SDK_WORLDGEN_PROVINCE_FAMILY_BASIN_UPLAND`
- `SDK_WORLDGEN_PROVINCE_FAMILY_HARDROCK`
- `SDK_WORLDGEN_PROVINCE_FAMILY_VOLCANIC`

---

## Detail Generation

### Fractal Noise Addition

```c
float add_detail_noise(float base_height, int lx, int lz,
                       uint32_t seed, float amplitude) {
    // Multi-octave noise
    float noise = 0.0f;
    float freq = 1.0f / 32.0f;  // 32m base wavelength
    float amp = amplitude;
    
    for (int octave = 0; octave < 4; octave++) {
        noise += amp * sdk_worldgen_value_noise(
            lx * freq, lz * freq, seed + octave);
        freq *= 2.0f;
        amp *= 0.5f;
    }
    
    return base_height + noise;
}
```

### Province-Specific Detail

```c
float get_detail_amplitude_for_province(uint8_t province) {
    switch (province) {
        case TERRAIN_PROVINCE_HARDROCK_HIGHLAND:
        case TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT:
            return 14.0f;  // Very rough
            
        case TERRAIN_PROVINCE_SILICICLASTIC_HILLS:
            return 10.0f;  // Moderately rough
            
        case TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND:
        case TERRAIN_PROVINCE_PEAT_WETLAND:
            return 2.0f;   // Flat
            
        default:
            return 6.0f;   // Moderate
    }
}
```

---

## Integration Notes

### With Hydrology

The hydrology pass runs after initial macro build:

```c
void build_macro_tile(SdkWorldGen* wg, SdkWorldGenMacroTile* tile) {
    // 1. Synthesize from continental
    for each cell:
        sample continental at world pos
        synthesize_macro_cell(cell, sample, ...)
    
    // 2. Run hydrology refinement
    sdk_worldgen_run_hydrology(wg, tile);
    
    // 3. Post-process
    finalize_macro_tile(tile);
}
```

### With Region Tiles

Region tiles sample macro cells for their base terrain:

```c
void build_region_from_macro(SdkWorldGen* wg, 
                              SdkWorldGenRegionTile* region) {
    for each region sample point (8m spacing):
        // Find containing macro cell
        int mx = wx / 32;
        int mz = wz / 32;
        
        // Get macro cell
        SdkWorldGenMacroCell* cell = get_macro_cell(mx, mz);
        
        // Copy base data
        sample->base_height = cell->surface_height;
        sample->temperature_band = cell->temperature_band;
        // ... etc
        
        // Add region-scale detail
        add_region_detail(sample, wx, wz);
}
```

---

## AI Context Hints

### Custom Noise Wavelength

```c
// Change detail scale for different terrain character
void set_custom_detail_scale(SdkWorldGenMacroTile* tile, float wavelength) {
    float freq = 1.0f / wavelength;
    
    for each cell:
        cell->surface_height += 
            detail_amplitude * sdk_worldgen_fbm(
                lx * freq, lz * freq, seed, 4);
}
```

### Mountain Ridge Generation

```c
// Add sharp ridges for mountain terrain
void add_ridges(SdkWorldGenMacroTile* tile, uint8_t province) {
    if (!is_mountainous(province)) return;
    
    // Ridged multifractal for sharp peaks
    for each cell:
        float ridge = sdk_worldgen_ridged(lx * freq, lz * freq, seed, 6);
        cell->surface_height += ridge_height * ridge;
}
```

### Valley Carving

```c
// Pre-carve river valleys
void carve_river_valleys(SdkWorldGenMacroTile* tile) {
    for each cell with high river_order:
        // Lower terrain along river path
        float valley_depth = cell->river_order * 2.0f;
        cell->surface_height -= (int16_t)valley_depth;
        
        // Mark as floodplain
        if (cell->river_order > 3) {
            cell->floodplain_mask = 1.0f;
        }
    }
}
```

---

## Related Documentation

- [SDK_Worldgen.md](../SDK_Worldgen.md) - Public API
- [SDK_WorldgenInternal.md](../Internal/SDK_WorldgenInternal.md) - Internal structures
- [SDK_WorldgenContinental.md](../Continental/SDK_WorldgenContinental.md) - Continental source data
- [SDK_WorldgenHydro.md](../Hydro/SDK_WorldgenHydro.md) - Hydrology pass
- [SDK_WorldgenTypes.md](../Types/SDK_WorldgenTypes.md) - Shared sampled types used downstream

---

**Source Files:**
- `SDK/Core/World/Worldgen/Macro/sdk_worldgen_macro.c`
