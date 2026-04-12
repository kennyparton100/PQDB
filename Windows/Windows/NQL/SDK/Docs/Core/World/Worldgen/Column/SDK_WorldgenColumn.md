# SDK Worldgen Column Documentation

> Status: parts of this page preserve older detached-wall examples and simplified helper snippets. For current wall-grid behavior, prefer `sdk_superchunk_geometry.h` and the wall helpers in `sdk_worldgen_column.c`. Live gameplay wall placement now follows shared-boundary superchunks (`period = chunk_span + 1`); detached-wall fields remain persisted metadata and debugging inputs, not a second live placement grid.

Comprehensive documentation for column sampling, geology, and surface generation.

**Module:** `SDK/Core/World/Worldgen/Column/`  
**Files:** `sdk_worldgen_column*.c/h` (7 files)

## Table of Contents

- [Module Overview](#module-overview)
- [Column Sampling](#column-sampling)
- [Stratigraphy System](#stratigraphy-system)
- [Geology Types](#geology-types)
- [Superchunk Walls](#superchunk-walls)
- [Surface Generation](#surface-generation)
- [Key Functions](#key-functions)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Column module samples the full column stack at a single world position, converting tile data into a `SdkTerrainColumnProfile`. It handles stratigraphy (layered rock), resource bodies, surface materials, and superchunk wall integration.

**Key Features:**
- Column profile generation from tiles
- Stratigraphic layering (basement, lower, upper, regolith)
- Resource body placement (coal, iron, etc.)
- Geology simulation (faults, folding)
- Superchunk wall profile computation
- Surface material selection

**Output:** `SdkTerrainColumnProfile` per column

---

## Column Sampling

### From Macro Tile

```c
void sdk_worldgen_sample_column_from_tile(
    SdkWorldGenMacroTile* tile,
    SdkWorldGen* wg,
    int wx, int wz,
    SdkTerrainColumnProfile* out_profile)
{
    // Find macro cell
    int mx = (wx - tile->tile_x * 128) / 32;
    int mz = (wz - tile->tile_z * 128) / 32;
    SdkWorldGenMacroCell* cell = &tile->cells[idx(mx, mz)];
    
    // Copy base data
    out_profile->surface_height = cell->surface_height;
    out_profile->base_height = cell->base_height;
    out_profile->water_height = cell->water_height;
    out_profile->river_bed_height = cell->river_bed_height;
    
    // Classifications
    out_profile->terrain_province = cell->terrain_province;
    out_profile->bedrock_province = cell->bedrock_province;
    out_profile->temperature_band = cell->temperature_band;
    out_profile->moisture_band = cell->moisture_band;
    out_profile->ecology = cell->ecology;
    
    // Soil
    out_profile->soil_depth = cell->soil_depth;
    out_profile->drainage_class = cell->drainage_class;
    out_profile->soil_fertility = cell->soil_fertility;
    
    // Water
    out_profile->water_table_depth = cell->water_table_depth;
    out_profile->water_surface_class = cell->water_surface_class;
    
    // Resources
    out_profile->resource_province = cell->resource_province;
    out_profile->resource_grade = cell->resource_grade;
}
```

### From Region Tile

```c
void sdk_worldgen_sample_column_from_region_tile(
    SdkWorldGenRegionTile* tile,
    SdkWorldGen* wg,
    int wx, int wz,
    SdkTerrainColumnProfile* out_profile)
{
    // Region has 8m sample spacing
    int rx = (wx - tile->tile_x * 512) / 8;
    int rz = (wz - tile->tile_z * 512) / 8;
    
    SdkRegionFieldSample* sample = &tile->samples[idx(rx, rz)];
    
    // More detailed data from region
    out_profile->surface_height = (int16_t)sample->surface_base;
    // ... etc
}
```

---

## Stratigraphy System

### SdkStrataColumn

```c
typedef struct {
    BlockType basement_block;       // Main bedrock
    BlockType deep_basement_block;  // Lower basement
    BlockType lower_block;          // Lower stratum
    BlockType upper_block;          // Upper stratum
    BlockType regolith_block;       // Weathered surface
    BlockType fault_block;          // Fault zone material
    BlockType vein_block;           // Mineral veins
    
    // Heights
    int16_t deep_basement_top;
    int16_t basement_top;
    int16_t lower_top;
    int16_t upper_top;
    int16_t weathered_base;
} SdkStrataColumn;
```

### Layer Thickness by Province

```c
void compute_strata(SdkStrataColumn* strata, 
                    SdkBedrockProvince bedrock_province,
                    int16_t surface_height) {
    switch (bedrock_province) {
        case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:
            strata->basement_block = BLOCK_STONE;
            strata->deep_basement_block = BLOCK_BEDROCK;
            strata->lower_block = BLOCK_STONE;
            strata->upper_block = BLOCK_COBBLESTONE;
            break;
            
        case BEDROCK_PROVINCE_SILICICLASTIC_BASIN:
            strata->basement_block = BLOCK_SANDSTONE;
            strata->deep_basement_block = BLOCK_STONE;
            strata->lower_block = BLOCK_SANDSTONE;
            strata->upper_block = BLOCK_SAND;
            break;
            
        case BEDROCK_PROVINCE_CARBONATE_PLATFORM:
            strata->basement_block = BLOCK_LIMESTONE;
            strata->deep_basement_block = BLOCK_STONE;
            strata->lower_block = BLOCK_LIMESTONE;
            strata->upper_block = BLOCK_CALCAREOUS_RESIDUAL;
            break;
            
        // ... etc
    }
    
    // Calculate layer boundaries
    strata->upper_top = surface_height - 2;  // Regolith thickness
    strata->lower_top = strata->upper_top - 10;
    strata->basement_top = strata->lower_top - 20;
    strata->deep_basement_top = strata->basement_top - 30;
}
```

---

## Geology Types

### Stratigraphy Provinces

```c
typedef enum {
    SDK_STRAT_PROVINCE_OCEANIC = 0,
    SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN,   // Sandstone, shale
    SDK_STRAT_PROVINCE_CARBONATE_SHELF,       // Limestone
    SDK_STRAT_PROVINCE_HARDROCK_BASEMENT,     // Granite, gneiss
    SDK_STRAT_PROVINCE_RIFT_BASIN,            // Volcanic + sediment
    SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX,      // Igneous
    SDK_STRAT_PROVINCE_FLOOD_BASALT           // Layered basalt
} SdkStratigraphyProvince;
```

### Resource Body Kinds

```c
typedef enum {
    SDK_RESOURCE_BODY_NONE = 0,
    SDK_RESOURCE_BODY_COAL,
    SDK_RESOURCE_BODY_CLAY,
    SDK_RESOURCE_BODY_IRONSTONE,
    SDK_RESOURCE_BODY_COPPER,
    SDK_RESOURCE_BODY_SULFUR,
    SDK_RESOURCE_BODY_LEAD_ZINC,
    SDK_RESOURCE_BODY_TUNGSTEN,
    SDK_RESOURCE_BODY_BAUXITE,
    SDK_RESOURCE_BODY_SALT,
    SDK_RESOURCE_BODY_OIL,
    SDK_RESOURCE_BODY_GAS,
    // ... 18 total
} SdkResourceBodyKind;
```

### Resource Body Placement

```c
void place_resource_body(SdkStrataColumn* strata,
                         int16_t surface_height,
                         SdkResourceBodyKind kind,
                         uint8_t grade) {
    // Determine appropriate depth for resource type
    int16_t depth;
    BlockType block;
    
    switch (kind) {
        case SDK_RESOURCE_BODY_COAL:
            depth = 20 + (grade % 30);  // 20-50m depth
            block = BLOCK_COAL_ORE;
            break;
            
        case SDK_RESOURCE_BODY_IRONSTONE:
            depth = 10 + (grade % 20);
            block = BLOCK_IRON_ORE;
            break;
            
        case SDK_RESOURCE_BODY_OIL:
            depth = 50 + (grade % 100);  // Deep
            // Oil is not a block, stored in separate data
            return;
            
        // ... etc
    }
    
    int16_t y = surface_height - depth;
    
    // Replace stratum at this depth with ore
    if (y > strata->basement_top) {
        // In regolith/upper - place as vein
        strata->vein_block = block;
    } else if (y > strata->lower_top) {
        // In upper stratum
        strata->upper_block = block;
    } else {
        // In lower stratum
        strata->lower_block = block;
    }
}
```

---

## Superchunk Walls

### Wall Detection

Historical note: the code snippets in this section show the older detached-wall intent. Current live wall detection is shared-boundary and should be read from `sdk_superchunk_geometry.h` plus the current helpers in `sdk_worldgen_column.c`.

```c
// Check if chunk is a wall chunk (period 17)
int chunk_is_full_superchunk_wall_chunk(int cx, int cz, 
                                          const SdkSuperchunkConfig* cfg) {
    if (!cfg->walls_enabled) return 0;
    
    int period = cfg->walls_detached ? cfg->wall_grid_size : 
                 (SDK_SUPERCHUNK_CHUNK_SPAN + 1);
    int origin_x = cfg->walls_detached ? cfg->wall_grid_offset_x : 0;
    int origin_z = cfg->walls_detached ? cfg->wall_grid_offset_z : 0;
    
    // Check if on wall grid line
    int on_x_wall = ((cx - origin_x) % period) == 0;
    int on_z_wall = ((cz - origin_z) % period) == 0;
    
    return on_x_wall || on_z_wall;
}
```

### Wall Profile Computation

Historical note: east/west/north/south gate and wall ownership are now driven by the shared geometry helpers. Treat the snippet below as background context, not the authoritative implementation.

```c
void compute_superchunk_wall_profile(SdkTerrainColumnProfile* profile,
                                      int cx, int cz,
                                      const SdkSuperchunkConfig* cfg) {
    // Determine which walls affect this chunk
    int is_x_wall = ((cx - cfg->wall_grid_offset_x) % cfg->wall_grid_size) == 0;
    int is_z_wall = ((cz - cfg->wall_grid_offset_z) % cfg->wall_grid_size) == 0;
    
    if (!is_x_wall && !is_z_wall) return;  // Not a wall chunk
    
    // Compute wall positions in chunk
    int sc_origin_x = superchunk_origin_for_coord(cx, cfg->wall_grid_size,
                                                 cfg->wall_grid_offset_x);
    int sc_origin_z = superchunk_origin_for_coord(cz, cfg->wall_grid_size,
                                                 cfg->wall_grid_offset_z);
    
    // Wall bounds within chunk
    if (is_x_wall) {
        // Wall runs N-S through this chunk
        int wall_x_in_chunk = (cx * 64) - sc_origin_x;
        if (wall_x_in_chunk >= 0 && wall_x_in_chunk < 64) {
            // This chunk contains an E-W wall
            profile->wall_mask_x = 1;
            profile->wall_x = wall_x_in_chunk;
        }
    }
    
    if (is_z_wall) {
        // Wall runs E-W through this chunk
        int wall_z_in_chunk = (cz * 64) - sc_origin_z;
        if (wall_z_in_chunk >= 0 && wall_z_in_chunk < 64) {
            profile->wall_mask_z = 1;
            profile->wall_z = wall_z_in_chunk;
        }
    }
}
```

---

## Surface Generation

### Surface Material Selection

```c
BlockType select_surface_block(SdkTerrainColumnProfile* profile,
                                int depth_from_surface) {
    // Top layer: grass or sand or snow based on climate
    if (depth_from_surface == 0) {
        switch (profile->terrain_province) {
            case TERRAIN_PROVINCE_OPEN_OCEAN:
                return BLOCK_SAND;
                
            case TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND:
                return BLOCK_GRASS;  // Or mud when flooded
                
            case TERRAIN_PROVINCE_ARID_FAN_STEPPE:
            case TERRAIN_PROVINCE_SALT_FLAT_PLAYA:
                return BLOCK_SAND;
                
            default:
                if (profile->temperature_band <= TEMP_SUBPOLAR) {
                    return BLOCK_SNOW;  // Tundra/snow
                }
                if (profile->moisture_band >= MOISTURE_HUMID &&
                    profile->drainage_class <= DRAINAGE_IMPERFECT) {
                    return BLOCK_MUD;  // Wetland
                }
                return BLOCK_GRASS;
        }
    }
    
    // Subsurface: soil
    if (depth_from_surface <= profile->soil_depth) {
        return BLOCK_DIRT;
    }
    
    // Below soil: regolith or bedrock
    if (depth_from_surface <= profile->soil_depth + profile->sediment_depth) {
        return BLOCK_GRAVEL;  // Regolith
    }
    
    // Bedrock
    return select_bedrock_block(profile);
}
```

---

## Key Functions

### Column Sampling

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_sample_column_from_tile` | `(macro, wg, wx, wz, out) → void` | Sample from macro tile |
| `sdk_worldgen_sample_column_from_region_tile` | `(region, wg, wx, wz, out) → void` | Sample from region tile |
| `sdk_worldgen_sample_continental_state` | `(wg, wx, wz, out) → void` | Direct continental sample |

### Geology

| Function | Description |
|----------|-------------|
| `compute_strata` | Build stratigraphic column |
| `place_resource_body` | Insert ore/resource deposits |
| `apply_faults` | Displace strata along fault lines |
| `compute_dip` | Calculate stratum tilt |

### Walls

| Function | Description |
|----------|-------------|
| `chunk_is_full_superchunk_wall_chunk` | Check if chunk is on wall grid |
| `compute_superchunk_wall_profile` | Calculate wall positions |
| `apply_superchunk_walls` | Modify profile for wall chunks |

### Surface

| Function | Description |
|----------|-------------|
| `select_surface_block` | Choose surface material |
| `compute_soil_depth` | Calculate soil thickness |
| `select_bedrock_block` | Choose bedrock type |

---

## Integration Notes

### In Chunk Fill

```c
void fill_chunk_column(SdkChunk* chunk, int lx, int lz,
                       SdkTerrainColumnProfile* profile) {
    // Get strata for this column
    SdkStrataColumn strata;
    compute_strata(&strata, profile->bedrock_province, 
                   profile->surface_height);
    
    // Fill from bottom up
    for (int y = 0; y < 1024; y++) {
        BlockType block;
        
        if (y > profile->surface_height) {
            block = BLOCK_AIR;
        } else if (y == profile->surface_height) {
            block = select_surface_block(profile, 0);
        } else if (y > strata.upper_top) {
            block = select_subsurface_block(profile, 
                                           profile->surface_height - y);
        } else if (y > strata.basement_top) {
            block = strata.upper_block;
        } else {
            block = strata.basement_block;
        }
        
        sdk_chunk_set_block(chunk, lx, y, lz, block);
    }
}
```

---

## AI Context Hints

### Custom Ore Distribution

```c
// Add new ore type with depth-varying grade
void place_custom_ore(SdkStrataColumn* strata, int surface_y,
                      int min_depth, int max_depth,
                      float abundance, BlockType ore_block) {
    // Random depth within range
    int depth = min_depth + (rand() % (max_depth - min_depth));
    int y = surface_y - depth;
    
    // Place ore patch
    int patch_size = (int)(abundance * 10);
    for (int dy = 0; dy < patch_size; dy++) {
        if (y - dy > 0) {
            set_stratum_at_y(strata, y - dy, ore_block);
        }
    }
}
```

### Cave Generation Integration

```c
// Mark cave locations in column
void mark_caves_in_column(SdkTerrainColumnProfile* profile,
                          SdkRegionFieldSample* region) {
    if (region->karst_mask > 0.5f) {
        // Limestone karst caves
        profile->cave_probability = region->karst_mask;
        profile->cave_min_y = profile->water_table_depth + 10;
        profile->cave_max_y = profile->surface_height - 20;
    }
    
    if (region->lava_tube_mask > 0.5f) {
        // Volcanic lava tubes
        profile->lava_tube_probability = region->lava_tube_mask;
        profile->lava_tube_y = profile->surface_height - 30;
    }
}
```

### Dynamic Wall Configuration

```c
// Runtime wall configuration changes
void configure_walls_for_world_type(SdkSuperchunkConfig* cfg,
                                      WorldType type) {
    switch (type) {
        case WORLD_TYPE_DEFAULT:
            cfg->walls_enabled = true;
            cfg->walls_detached = false;
            break;
            
        case WORLD_TYPE_OPEN:
            cfg->walls_enabled = false;
            break;
            
        case WORLD_TYPE_GRID:
            cfg->walls_enabled = true;
            cfg->walls_detached = true;
            cfg->wall_grid_size = 18;  // Custom period
            cfg->wall_grid_offset_x = 5;
            cfg->wall_grid_offset_z = 5;
            break;
    }
}
```

---

## Related Documentation

- [SDK_Worldgen.md](../SDK_Worldgen.md) - Public API
- [SDK_WorldgenInternal.md](../Internal/SDK_WorldgenInternal.md) - Internal structures
- [SDK_WorldgenTypes.md](../Types/SDK_WorldgenTypes.md) - Type definitions
- [SDK_WorldgenMacro.md](../Macro/SDK_WorldgenMacro.md) - Input data
- [SDK_WorldgenTypes.md](../Types/SDK_WorldgenTypes.md) - Shared sampled types and geology enums

---

**Source Files:**
- `SDK/Core/World/Worldgen/Column/sdk_worldgen_column.c`
- `SDK/Core/World/Worldgen/Column/sdk_worldgen_column_geology.c`
- `SDK/Core/World/Worldgen/Column/sdk_worldgen_column_surface.c`
- `SDK/Core/World/Worldgen/Column/sdk_worldgen_column_region.c`
- `SDK/Core/World/Worldgen/Column/sdk_worldgen_column_internal.h`
