# SDK Worldgen Internal Documentation

Comprehensive documentation for internal world generation cache structures, tile types, and helper functions.

**Module:** `SDK/Core/World/Worldgen/Internal/`  
**Header:** `SDK/Core/World/Worldgen/Internal/sdk_worldgen_internal.h`

## Table of Contents

- [Module Overview](#module-overview)
- [Layered Pipeline](#layered-pipeline)
- [Cache Constants](#cache-constants)
- [Internal Cell Structures](#internal-cell-structures)
- [Tile Structures](#tile-structures)
- [Strata Column](#strata-column)
- [WorldGen Implementation](#worldgen-implementation)
- [Internal Enums](#internal-enums)
- [Hash and Noise Functions](#hash-and-noise-functions)
- [Classification Helpers](#classification-helpers)
- [Tile Access Functions](#tile-access-functions)
- [Debug Capture System](#debug-capture-system)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Internal module defines the data structures and helper functions used by the world generation pipeline. These are implementation details that support the public API but are not exposed to consumers.

**Key Features:**
- Three-layer tile cache (continental, macro, region)
- Rich cell structures with hydrology and geology
- Noise and hash functions for procedural generation
- Debug capture system for chunk diagnostics

---

## Layered Pipeline

```
SdkWorldDesc (world parameters)
    ↓
Continental Tiles (climate, hydrology, ocean mask)
    ↓
Macro Tiles (128×128 block synthesis + hydrology)
    ↓
Region Tiles (32×32 block geology, resources)
    ↓
Column and Chunk Generation (block filling)
    ↓
SdkTerrainColumnProfile (stable public output)
```

Cache ownership lives on `SdkWorldGenImpl`. Each worldgen context owns its own persistent continental scratch buffers plus continental, macro, and region caches.

---

## Cache Constants

### Macro Tile Constants

```c
#define SDK_WORLDGEN_TILE_STRIDE (SDK_WORLDGEN_MACRO_TILE_SIZE + SDK_WORLDGEN_MACRO_TILE_HALO * 2)
#define SDK_WORLDGEN_TILE_CELL_COUNT (SDK_WORLDGEN_TILE_STRIDE * SDK_WORLDGEN_TILE_STRIDE)
```

| Constant | Value | Description |
|----------|-------|-------------|
| `SDK_WORLDGEN_MACRO_TILE_SIZE` | 128 | Core tile size in blocks |
| `SDK_WORLDGEN_MACRO_TILE_HALO` | 8 | Halo for seamless borders |
| `SDK_WORLDGEN_TILE_STRIDE` | 144 | Total width (128 + 8*2) |
| `SDK_WORLDGEN_TILE_CELL_COUNT` | 20736 | Total cells per tile |

### Continental Tile Constants

```c
#define SDK_WORLDGEN_CONTINENT_CELL_BLOCKS 256  // 256m per cell
#define SDK_WORLDGEN_CONTINENT_TILE_SIZE 64     // 64×64 cells
#define SDK_WORLDGEN_CONTINENT_TILE_HALO 8
#define SDK_WORLDGEN_CONTINENT_STRIDE 80
#define SDK_WORLDGEN_CONTINENT_CELL_COUNT 6400
#define SDK_WORLDGEN_CONTINENT_ANALYSIS_MARGIN 24
```

Continental tiles cover ~16×16 km each (64 cells × 256m).

### Cache Slot Counts

```c
#define SDK_WORLDGEN_CONTINENT_CACHE_SLOTS 64
#define SDK_WORLDGEN_REGION_CACHE_SLOTS 32
#define SDK_SETTLEMENT_CACHE_SLOTS 8
```

### Region Tile Constants

```c
#define SDK_WORLDGEN_REGION_TILE_BLOCKS 512
#define SDK_WORLDGEN_REGION_TILE_HALO_BLOCKS 64
#define SDK_WORLDGEN_REGION_SAMPLE_SPACING 8
#define SDK_WORLDGEN_REGION_STRIDE 85
#define SDK_WORLDGEN_REGION_SAMPLE_COUNT 7225
```

---

## Internal Cell Structures

### SdkWorldGenMacroCell

```c
typedef struct {
    // Heights
    int16_t  base_height;
    int16_t  surface_height;
    int16_t  water_height;
    int16_t  river_bed_height;
    int16_t  lake_level;
    int16_t  coast_distance;
    
    // Hydrology
    uint32_t flow_accum;
    uint32_t basin_id;
    uint8_t  river_order;
    uint8_t  floodplain_width;
    uint8_t  downstream_dir;
    
    // Classifications (packed to uint8)
    uint8_t  terrain_province;
    uint8_t  bedrock_province;
    uint8_t  temperature_band;
    uint8_t  moisture_band;
    uint8_t  surface_sediment;
    uint8_t  parent_material;
    uint8_t  drainage_class;
    uint8_t  soil_reaction;
    uint8_t  soil_fertility;
    uint8_t  soil_salinity;
    uint8_t  water_table_depth;
    uint8_t  ecology;
    uint8_t  resource_province;
    uint8_t  hydrocarbon_class;
    uint8_t  resource_grade;
    
    // Detail and masks
    uint8_t  plate_class;
    uint8_t  relief_strength;
    uint8_t  detail_amp;
    uint8_t  mountain_mask;
    uint8_t  river_strength;
    uint8_t  wetness;
    uint8_t  water_temp;
    uint8_t  water_surface_class;
    uint8_t  stratigraphy_province;
    uint8_t  province_family;
    
    // Lake and feature masks
    uint8_t  lake_mask;
    uint8_t  closed_basin_mask;
    uint8_t  ravine_mask;
    uint8_t  vent_mask;
    uint8_t  caldera_mask;
    uint16_t lake_id;
    
    // Surface features
    uint32_t landform_flags;
    int8_t   boundary_class;
    int16_t  slope;
} SdkWorldGenMacroCell;
```

### SdkContinentalCell

```c
typedef struct {
    int16_t  raw_height;
    int16_t  filled_height;
    int16_t  lake_level;
    int16_t  coast_distance;
    int32_t  downstream_cx;
    int32_t  downstream_cz;
    uint32_t flow_accum;
    uint32_t basin_id;
    uint16_t lake_id;
    uint8_t  land_mask;
    uint8_t  ocean_mask;
    uint8_t  lake_mask;
    uint8_t  closed_basin_mask;
    uint8_t  precipitation;
    uint8_t  runoff;
    uint8_t  trunk_river_order;
    uint8_t  water_access;
    uint8_t  harbor_score;
    uint8_t  confluence_score;
    uint8_t  flood_risk;
    uint8_t  buildable_flatness;
} SdkContinentalCell;
```

### SdkContinentalSample

Float version for interpolation:

```c
typedef struct {
    float    raw_height;
    float    filled_height;
    float    lake_level;
    float    coast_distance;
    float    flow_accum;
    float    precipitation;
    float    runoff;
    float    trunk_river_order;
    float    water_access;
    float    harbor_score;
    float    confluence_score;
    float    flood_risk;
    float    buildable_flatness;
    uint32_t basin_id;
    uint16_t lake_id;
    uint8_t  land_mask;
    uint8_t  ocean_mask;
    uint8_t  lake_mask;
    uint8_t  closed_basin_mask;
    int32_t  downstream_cx;
    int32_t  downstream_cz;
} SdkContinentalSample;
```

### SdkRegionFieldSample

```c
typedef struct {
    // Heights and distances
    float base_height;
    float surface_base;
    float surface_relief;
    float water_level;
    float river_bed_height;
    float coast_signed_distance;
    float ocean_depth;
    
    // Hydrology
    float river_strength;
    float floodplain_mask;
    float river_order;
    float floodplain_width;
    float river_channel_width;
    float river_channel_depth;
    float valley_mask;
    float braid_mask;
    float waterfall_mask;
    float flow_dir_x;
    float flow_dir_z;
    float wetness;
    
    // Climate
    float air_temp;
    float water_temp;
    
    // Soil and geology
    float soil_depth;
    float sediment_thickness;
    float regolith_thickness;
    float drainage_index;
    float soil_reaction_value;
    float soil_fertility_value;
    float soil_salinity_value;
    float soil_organic_value;
    float water_table_depth;
    float stratigraphy_control;
    
    // Stratigraphy
    float weathered_base;
    float upper_top;
    float lower_top;
    float basement_top;
    float deep_basement_top;
    float dip_x;
    float dip_z;
    
    // Structural
    float fault_mask;
    float fault_throw;
    float basin_axis_weight;
    
    // Resource controls
    float channel_sand_bias;
    float trap_strength;
    float seal_quality;
    float gas_bias;
    float source_richness;
    float carbonate_purity;
    
    // Volcanic features
    float vent_bias;
    float evaporite_bias;
    float lava_flow_bias;
    float ash_bias;
    
    // Lake and cave
    float lake_mask;
    float lake_level;
    float closed_basin_mask;
    float ravine_mask;
    float ravine_depth;
    float ravine_width;
    float vent_distance;
    float caldera_mask;
    
    // Cave features
    float karst_mask;
    float fracture_cave_mask;
    float lava_tube_mask;
    float cave_depth_bias;
    float cave_entrance_mask;
    
    // Volcano types
    float stratovolcano_mask;
    float shield_mask;
    float fissure_mask;
    float ashfall_mask;
    
    // Packed classifications
    uint8_t  terrain_province;
    uint8_t  bedrock_province;
    uint8_t  temperature_band;
    uint8_t  moisture_band;
    uint8_t  surface_sediment;
    uint8_t  parent_material;
    uint8_t  drainage_class;
    uint8_t  soil_reaction;
    uint8_t  soil_fertility;
    uint8_t  soil_salinity;
    uint8_t  ecology;
    uint8_t  resource_province;
    uint8_t  hydrocarbon_class;
    uint8_t  resource_grade;
    uint8_t  water_surface_class;
    uint8_t  stratigraphy_province;
    uint32_t landform_flags;
} SdkRegionFieldSample;
```

---

## Tile Structures

### SdkWorldGenContinentalTile

```c
typedef struct {
    bool                valid;
    int32_t             tile_x;
    int32_t             tile_z;
    uint32_t            stamp;
    SdkContinentalCell  cells[SDK_WORLDGEN_CONTINENT_CELL_COUNT];
} SdkWorldGenContinentalTile;
```

### SdkWorldGenMacroTile

```c
typedef struct {
    bool                  valid;
    int32_t               tile_x;
    int32_t               tile_z;
    uint32_t              stamp;
    SdkWorldGenMacroCell  cells[SDK_WORLDGEN_TILE_CELL_COUNT];
} SdkWorldGenMacroTile;
```

### SdkWorldGenRegionTile

```c
typedef struct {
    bool                 valid;
    int32_t              tile_x;
    int32_t              tile_z;
    uint32_t             stamp;
    SdkRegionFieldSample samples[SDK_WORLDGEN_REGION_SAMPLE_COUNT];
} SdkWorldGenRegionTile;
```

---

## Strata Column

```c
typedef struct {
    BlockType basement_block;
    BlockType deep_basement_block;
    BlockType lower_block;
    BlockType upper_block;
    BlockType regolith_block;
    BlockType fault_block;
    BlockType vein_block;
    int16_t   deep_basement_top;
    int16_t   basement_top;
    int16_t   lower_top;
    int16_t   upper_top;
    int16_t   weathered_base;
} SdkStrataColumn;
```

Defines the stratigraphic layering for a single column:
- Deep basement (unweathered bedrock)
- Basement (main bedrock)
- Lower stratum
- Upper stratum
- Regolith (weathered surface)
- Fault material
- Mineral veins

---

## WorldGen Implementation

### SdkWorldGenImpl

```c
typedef struct {
    uint32_t seed;
    int16_t  sea_level;
    uint16_t macro_cell_size;
    char world_path[512];
    void* tile_cache;
    SdkWorldGenContinentScratch* continent_scratch;
    uint32_t stamp_clock;
    bool settlements_enabled;
    bool construction_cells_enabled;
    
    // Settlement cache
    SuperchunkSettlementData* settlement_cache[SDK_SETTLEMENT_CACHE_SLOTS];
    int32_t settlement_cache_scx[SDK_SETTLEMENT_CACHE_SLOTS];
    int32_t settlement_cache_scz[SDK_SETTLEMENT_CACHE_SLOTS];
    uint32_t settlement_cache_stamps[SDK_SETTLEMENT_CACHE_SLOTS];
} SdkWorldGenImpl;
```

### SdkWorldGenContinentScratch

```c
typedef struct {
    int16_t*       raw_height;
    int16_t*       filled_height;
    int16_t*       coast_distance;
    uint8_t*       ocean_mask;
    uint8_t*       precipitation;
    uint8_t*       runoff;
    uint8_t*       flatness;
    int*           downstream;
    uint32_t*      flow_accum;
    uint32_t*      basin_id;
    uint8_t*       trunk_order;
    uint8_t*       lake_mask;
    uint8_t*       closed_mask;
    int16_t*       lake_level;
    uint16_t*      lake_id;
    uint8_t*       water_access;
    uint8_t*       harbor_score;
    uint8_t*       confluence;
    uint8_t*       flood_risk;
    int*           queue;
    uint8_t*       visited;
    void*          heap;
    void*          sort_entries;
    int*           stack;
} SdkWorldGenContinentScratch;
```

---

## Internal Enums

### SdkWorldGenProvinceFamily

```c
typedef enum {
    SDK_WORLDGEN_PROVINCE_FAMILY_MARINE_COAST = 0,
    SDK_WORLDGEN_PROVINCE_FAMILY_WET_LOWLAND,
    SDK_WORLDGEN_PROVINCE_FAMILY_BASIN_UPLAND,
    SDK_WORLDGEN_PROVINCE_FAMILY_HARDROCK,
    SDK_WORLDGEN_PROVINCE_FAMILY_VOLCANIC
} SdkWorldGenProvinceFamily;
```

### SdkWaterThermalBand

```c
typedef enum {
    SDK_WATER_THERMAL_FRIGID = 0,
    SDK_WATER_THERMAL_COLD,
    SDK_WATER_THERMAL_COOL,
    SDK_WATER_THERMAL_TEMPERATE,
    SDK_WATER_THERMAL_WARM
} SdkWaterThermalBand;
```

### SdkStratigraphyProvince

```c
typedef enum {
    SDK_STRAT_PROVINCE_OCEANIC = 0,
    SDK_STRAT_PROVINCE_SILICICLASTIC_BASIN,
    SDK_STRAT_PROVINCE_CARBONATE_SHELF,
    SDK_STRAT_PROVINCE_HARDROCK_BASEMENT,
    SDK_STRAT_PROVINCE_RIFT_BASIN,
    SDK_STRAT_PROVINCE_VOLCANIC_COMPLEX,
    SDK_STRAT_PROVINCE_FLOOD_BASALT
} SdkStratigraphyProvince;
```

### SdkResourceBodyKind

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
    SDK_RESOURCE_BODY_RARE_EARTH,
    SDK_RESOURCE_BODY_URANIUM,
    SDK_RESOURCE_BODY_SALTPETER,
    SDK_RESOURCE_BODY_PHOSPHATE,
    SDK_RESOURCE_BODY_CHROMIUM,
    SDK_RESOURCE_BODY_ALUMINUM
} SdkResourceBodyKind;
```

---

## Hash and Noise Functions

### Hash Functions

```c
// 32-bit hash with good avalanche
uint32_t sdk_worldgen_hash32(uint32_t x);

// 2D hash combining position and seed
uint32_t sdk_worldgen_hash2d(int x, int z, uint32_t seed);

// Hash to float [0, 1]
float sdk_worldgen_hashf(uint32_t h);
```

### Noise Functions

```c
// Smoothstep interpolation
float smoothstepf(float t);  // t*t*(3-2*t)

// Bilinear value noise
float sdk_worldgen_value_noise(float x, float z, uint32_t seed);

// Fractal Brownian Motion (multi-octave noise)
float sdk_worldgen_fbm(float x, float z, uint32_t seed, int octaves);

// Ridged multifractal
float sdk_worldgen_ridged(float x, float z, uint32_t seed, int octaves);
```

### Clamping Functions

```c
float sdk_worldgen_clampf(float v, float lo, float hi);
int sdk_worldgen_clampi(int v, int lo, int hi);

// Pack float [0,1] to uint8
static inline uint8_t sdk_worldgen_pack_unorm8(float v);

// Unpack uint8 to float [0,1]
static inline float sdk_worldgen_unpack_unorm8(uint8_t v);
```

---

## Classification Helpers

```c
// Classify temperature (0.0-1.0) to band
static inline uint8_t sdk_worldgen_classify_temperature(float temp);

// Classify moisture (0.0-1.0) to band
static inline uint8_t sdk_worldgen_classify_moisture(float moisture);

// Check if ecology is wetland type
static inline int sdk_worldgen_ecology_is_wetland(uint8_t ecology);

// Check if ecology should have turf/plants
static inline int sdk_worldgen_ecology_prefers_turf(uint8_t ecology);

// Check if passive fauna can spawn
static inline int sdk_worldgen_ecology_supports_passive_fauna(uint8_t ecology);
```

---

## Tile Access Functions

### Require Functions (Get or Build)

```c
// Get existing tile or generate new one
SdkWorldGenMacroTile* sdk_worldgen_require_macro_tile(SdkWorldGen* wg, int wx, int wz);
SdkWorldGenRegionTile* sdk_worldgen_require_region_tile(SdkWorldGen* wg, int wx, int wz);
SdkWorldGenContinentalTile* sdk_worldgen_require_continental_tile(SdkWorldGen* wg, int wx, int wz);
```

### Build Functions

```c
// Build tile contents from continental samples
void sdk_worldgen_build_macro_tile(SdkWorldGen* wg, SdkWorldGenMacroTile* tile);

// Build region tile with geology
void sdk_worldgen_build_region_tile(SdkWorldGen* wg, SdkWorldGenRegionTile* tile);

// Build continental tile with hydrology
void sdk_worldgen_build_continental_tile(SdkWorldGen* wg, SdkWorldGenContinentalTile* tile);

// Run drainage basin hydrology on macro tile
void sdk_worldgen_run_hydrology(SdkWorldGen* wg, SdkWorldGenMacroTile* tile);
```

### Sampling Functions

```c
// Get continental cell at cell coordinates
const SdkContinentalCell* sdk_worldgen_get_continental_cell(
    SdkWorldGen* wg, int cell_x, int cell_z);

// Sample continental state at world position
void sdk_worldgen_sample_continental_state(
    SdkWorldGen* wg, int wx, int wz, SdkContinentalSample* out_sample);

// Build column profile from macro tile
void sdk_worldgen_sample_column_from_tile(
    SdkWorldGenMacroTile* tile, SdkWorldGen* wg,
    int wx, int wz, SdkTerrainColumnProfile* out_profile);

// Build column profile from region tile
void sdk_worldgen_sample_column_from_region_tile(
    SdkWorldGenRegionTile* tile, SdkWorldGen* wg,
    int wx, int wz, SdkTerrainColumnProfile* out_profile);

// Sample full region fields
void sdk_worldgen_sample_region_fields(
    SdkWorldGenRegionTile* tile, SdkWorldGen* wg,
    int wx, int wz, SdkRegionFieldSample* out_sample);
```

---

## Debug Capture System

### Thread-Local Capture

```c
// Get current thread's debug capture
SdkWorldGenChunkDebugCapture* sdk_worldgen_debug_capture_get_thread_current(void);

// Begin capture for a chunk
void sdk_worldgen_debug_capture_begin(SdkWorldGenChunkDebugCapture* capture);

// End capture
void sdk_worldgen_debug_capture_end(void);
```

### Capture Notes

```c
// Log tree placement
void sdk_worldgen_debug_capture_note_tree(
    int lx, int lz, uint8_t archetype, int trunk_height);

// Log plant/placement
void sdk_worldgen_debug_capture_note_plant(int lx, int lz, BlockType plant_block);

// Log water sealing
void sdk_worldgen_debug_capture_note_water_seal(
    int lx, int lz, int waterline, BlockType cap_block, int banked);

// Log wall/gate placement
void sdk_worldgen_debug_capture_note_wall_column(
    int lx, int lz, uint8_t wall_mask, uint8_t gate_mask,
    int wall_top_y, int gate_floor_y);

// Log settlement stage reached
void sdk_worldgen_debug_capture_note_settlement_stage(void);

// Log route generation
void sdk_worldgen_debug_capture_note_route(
    int route_surface, int start_kind, int end_kind,
    int start_y, int end_y, int max_cut, int max_fill,
    int carved_columns, int candidate_index);

// Custom message
void sdk_worldgen_debug_capture_note_custom(const char* message);
```

### Report Emission

```c
// Emit debug report for a chunk
void sdk_worldgen_emit_chunk_debug_report(
    SdkWorldGen* wg, SdkChunkManager* cm, int cx, int cz);
```

---

## AI Context Hints

### Adding New Cell Field

```c
// 1. Add to SdkWorldGenMacroCell in sdk_worldgen_internal.h
uint8_t my_new_field;

// 2. Populate in sdk_worldgen_build_macro_tile()
tile->cells[i].my_new_field = compute_value(...);

// 3. Use in column sampling or chunk fill
profile->custom_value = macro_cell->my_new_field;
```

### Custom Cache Size

```c
// Adjust cache slots for memory/performance tradeoff
#define SDK_WORLDGEN_REGION_CACHE_SLOTS 64  // More slots = less regeneration
```

### Extending Debug Capture

```c
// Add custom capture type
void sdk_worldgen_debug_capture_note_cave(
    int lx, int ly, int lz, int cave_type, int depth);

// Implement in debug report generation
// Format output in sdk_worldgen_emit_chunk_debug_report()
```

### Noise Customization

```c
// Create custom fractal noise
float my_custom_noise(float x, float z, uint32_t seed) {
    float sum = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    for (int i = 0; i < octaves; i++) {
        sum += amp * sdk_worldgen_value_noise(x * freq, z * freq, seed + i);
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum;
}
```

---

## Related Documentation

- [SDK_Worldgen.md](../SDK_Worldgen.md) - Public API
- [SDK_WorldgenTypes.md](../Types/SDK_WorldgenTypes.md) - Public type definitions
- [SDK_WorldgenColumn.md](../Column/SDK_WorldgenColumn.md) - Column generation
- [SDK_WorldgenContinental.md](../Continental/SDK_WorldgenContinental.md) - Continental generation

---

**Source Files:**
- `SDK/Core/World/Worldgen/Internal/sdk_worldgen_internal.h` (508 lines)
