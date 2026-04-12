# SDK Worldgen Types Documentation

Comprehensive documentation for the SDK world generation public type definitions, enums, and terrain column profile structure.

**Module:** `SDK/Core/World/Worldgen/Types/`  
**Header:** `SDK/Core/World/Worldgen/Types/sdk_worldgen_types.h`

## Table of Contents

- [Module Overview](#module-overview)
- [Terrain Province](#terrain-province)
- [Bedrock Province](#bedrock-province)
- [Climate Bands](#climate-bands)
- [Surface Classification](#surface-classification)
- [Soil Properties](#soil-properties)
- [Ecology and Biomes](#ecology-and-biomes)
- [Resources](#resources)
- [Terrain Column Profile](#terrain-column-profile)
- [Resource Signature](#resource-signature)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Worldgen Types module defines the public enums and data structures used by the world generation system. These types represent the geological, climatic, and ecological classification systems that drive terrain generation.

**Key Features:**
- 18 terrain province types
- 9 bedrock province types
- 6 temperature and moisture bands
- 17 sediment and parent material types
- 6 soil property classifications
- 24 ecology/biome types
- 17 resource province types
- Comprehensive terrain column profile (29 fields)

---

## Terrain Province

```c
typedef enum {
    TERRAIN_PROVINCE_OPEN_OCEAN = 0,
    TERRAIN_PROVINCE_CONTINENTAL_SHELF,
    TERRAIN_PROVINCE_DYNAMIC_COAST,
    TERRAIN_PROVINCE_ESTUARY_DELTA,
    TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND,
    TERRAIN_PROVINCE_PEAT_WETLAND,
    TERRAIN_PROVINCE_SILICICLASTIC_HILLS,
    TERRAIN_PROVINCE_CARBONATE_UPLAND,
    TERRAIN_PROVINCE_HARDROCK_HIGHLAND,
    TERRAIN_PROVINCE_UPLIFTED_PLATEAU,
    TERRAIN_PROVINCE_RIFT_VALLEY,
    TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT,
    TERRAIN_PROVINCE_VOLCANIC_ARC,
    TERRAIN_PROVINCE_BASALT_PLATEAU,
    TERRAIN_PROVINCE_ARID_FAN_STEPPE,
    TERRAIN_PROVINCE_ALPINE_BELT,
    TERRAIN_PROVINCE_SALT_FLAT_PLAYA,
    TERRAIN_PROVINCE_BADLANDS_DISSECTED
} SdkTerrainProvince;
```

### Province Groups

| Group | Provinces |
|-------|-----------|
| Marine | `OPEN_OCEAN`, `CONTINENTAL_SHELF`, `DYNAMIC_COAST`, `ESTUARY_DELTA` |
| Lowland | `FLOODPLAIN_ALLUVIAL_LOWLAND`, `PEAT_WETLAND` |
| Upland/Hills | `SILICICLASTIC_HILLS`, `CARBONATE_UPLAND` |
| Highland/Mountains | `HARDROCK_HIGHLAND`, `UPLIFTED_PLATEAU`, `FOLD_MOUNTAIN_BELT`, `ALPINE_BELT` |
| Tectonic | `RIFT_VALLEY`, `VOLCANIC_ARC`, `BASALT_PLATEAU` |
| Arid | `ARID_FAN_STEPPE`, `SALT_FLAT_PLAYA`, `BADLANDS_DISSECTED` |

---

## Bedrock Province

```c
typedef enum {
    BEDROCK_PROVINCE_OCEANIC_BASALT = 0,
    BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS,
    BEDROCK_PROVINCE_METAMORPHIC_BELT,
    BEDROCK_PROVINCE_GRANITIC_INTRUSIVE,
    BEDROCK_PROVINCE_SILICICLASTIC_BASIN,
    BEDROCK_PROVINCE_CARBONATE_PLATFORM,
    BEDROCK_PROVINCE_RIFT_SEDIMENTARY,
    BEDROCK_PROVINCE_VOLCANIC_ARC,
    BEDROCK_PROVINCE_FLOOD_BASALT
} SdkBedrockProvince;
```

Controls basement rock type and determines:
- Ore deposit types
- Weathering products
- Stratigraphy patterns

---

## Climate Bands

### Temperature Bands

```c
typedef enum {
    TEMP_POLAR = 0,
    TEMP_SUBPOLAR,
    TEMP_COOL_TEMPERATE,
    TEMP_WARM_TEMPERATE,
    TEMP_SUBTROPICAL,
    TEMP_TROPICAL
} SdkTemperatureBand;
```

### Moisture Bands

```c
typedef enum {
    MOISTURE_ARID = 0,
    MOISTURE_SEMI_ARID,
    MOISTURE_SUBHUMID,
    MOISTURE_HUMID,
    MOISTURE_PERHUMID,
    MOISTURE_WATERLOGGED
} SdkMoistureBand;
```

### Classification Functions

```c
// Temperature from normalized value (0.0-1.0)
uint8_t sdk_worldgen_classify_temperature(float temp);

// Moisture from normalized value (0.0-1.0)
uint8_t sdk_worldgen_classify_moisture(float moisture);
```

| Input Range | Temperature | Moisture |
|-------------|-------------|----------|
| 0.00-0.12 | POLAR | - |
| 0.12-0.26 | SUBPOLAR | - |
| 0.26-0.45 | COOL_TEMPERATE | - |
| 0.00-0.12 | - | ARID |
| 0.12-0.28 | - | SEMI_ARID |
| 0.28-0.48 | - | SUBHUMID |
| 0.48-0.66 | - | HUMID |
| 0.66-0.82 | - | PERHUMID |
| 0.82-1.00 | SUBTROPICAL/TROPICAL | WATERLOGGED |

---

## Surface Classification

### Surface Sediment

```c
typedef enum {
    SEDIMENT_RESIDUAL_SOIL,
    SEDIMENT_COLLUVIUM,        // Hillslope debris
    SEDIMENT_TALUS,            // Rockfall at cliff base
    SEDIMENT_COARSE_ALLUVIUM,  // River gravels
    SEDIMENT_FINE_ALLUVIUM,    // River silts
    SEDIMENT_DELTAIC_SILT,
    SEDIMENT_LACUSTRINE_CLAY,  // Lake bottom
    SEDIMENT_BEACH_SAND,
    SEDIMENT_EOILIAN_SAND,     // Wind-blown
    SEDIMENT_PEAT,
    SEDIMENT_VOLCANIC_ASH,
    SEDIMENT_MARINE_MUD,
    SEDIMENT_MARINE_SAND,
    SEDIMENT_LOESS,            // Wind-blown silt
    SEDIMENT_CALCAREOUS_RESIDUAL,
    SEDIMENT_SAPROLITE         // Chemically weathered bedrock
} SdkSurfaceSediment;
```

### Parent Material Class

```c
typedef enum {
    PARENT_MATERIAL_GRANITIC,
    PARENT_MATERIAL_METAMORPHIC,
    PARENT_MATERIAL_MAFIC_VOLCANIC,
    PARENT_MATERIAL_INTERMEDIATE_VOLCANIC,
    PARENT_MATERIAL_SILICICLASTIC,
    PARENT_MATERIAL_CARBONATE,
    PARENT_MATERIAL_ORGANIC,
    PARENT_MATERIAL_ALLUVIAL,
    PARENT_MATERIAL_AEOLIAN,    // Wind-deposited
    PARENT_MATERIAL_EVAPORITIC
} SdkParentMaterialClass;
```

### Drainage Class

```c
typedef enum {
    DRAINAGE_EXCESSIVE = 0,  // Rapid drainage
    DRAINAGE_WELL,           // Good drainage
    DRAINAGE_MODERATE,
    DRAINAGE_IMPERFECT,      // Slow drainage
    DRAINAGE_POOR,           // Very slow
    DRAINAGE_WATERLOGGED     // Saturation
} SdkDrainageClass;
```

---

## Soil Properties

### Soil Reaction (pH)

```c
typedef enum {
    SOIL_REACTION_STRONGLY_ACID = 0,
    SOIL_REACTION_ACID,
    SOIL_REACTION_SLIGHTLY_ACID,
    SOIL_REACTION_NEUTRAL,
    SOIL_REACTION_CALCAREOUS,
    SOIL_REACTION_SALINE_ALKALINE
} SdkSoilReactionClass;
```

### Soil Fertility

```c
typedef enum {
    SOIL_FERTILITY_VERY_LOW = 0,
    SOIL_FERTILITY_LOW,
    SOIL_FERTILITY_MODERATE,
    SOIL_FERTILITY_HIGH,
    SOIL_FERTILITY_VERY_HIGH
} SdkSoilFertilityClass;
```

### Soil Salinity

```c
typedef enum {
    SOIL_SALINITY_NONE = 0,
    SOIL_SALINITY_SLIGHT,
    SOIL_SALINITY_MODERATE,
    SOIL_SALINITY_HIGH
} SdkSoilSalinityClass;
```

---

## Ecology and Biomes

```c
typedef enum {
    ECOLOGY_BARREN = 0,
    ECOLOGY_DUNE_COAST,
    ECOLOGY_ESTUARY_WETLAND,
    ECOLOGY_RIPARIAN_FOREST,
    ECOLOGY_FLOODPLAIN_MEADOW,
    ECOLOGY_FEN,
    ECOLOGY_BOG,
    ECOLOGY_PRAIRIE,
    ECOLOGY_STEPPE,
    ECOLOGY_MEDITERRANEAN_SCRUB,
    ECOLOGY_TEMPERATE_DECIDUOUS_FOREST,
    ECOLOGY_TEMPERATE_CONIFER_FOREST,
    ECOLOGY_BOREAL_TAIGA,
    ECOLOGY_TROPICAL_SEASONAL_FOREST,
    ECOLOGY_TROPICAL_RAINFOREST,
    ECOLOGY_HOT_DESERT,
    ECOLOGY_TUNDRA,
    ECOLOGY_ALPINE_MEADOW,
    ECOLOGY_VOLCANIC_BARRENS,
    ECOLOGY_NIVAL_ICE,
    ECOLOGY_SALT_DESERT,
    ECOLOGY_SCRUB_BADLANDS,
    ECOLOGY_SAVANNA_GRASSLAND,
    ECOLOGY_MANGROVE_SWAMP
} SdkBiomeEcology;
```

### Legacy Aliases

```c
#define ECOLOGY_GRASSLAND ECOLOGY_PRAIRIE
#define ECOLOGY_SHRUB_STEPPE ECOLOGY_STEPPE
#define ECOLOGY_TEMPERATE_FOREST ECOLOGY_TEMPERATE_DECIDUOUS_FOREST
#define ECOLOGY_BOREAL_FOREST ECOLOGY_BOREAL_TAIGA
#define ECOLOGY_RIPARIAN_WOODLAND ECOLOGY_RIPARIAN_FOREST
#define ECOLOGY_WETLAND_FEN ECOLOGY_FEN
#define ECOLOGY_PEAT_BOG ECOLOGY_BOG
```

### Ecology Helpers

```c
// Check if ecology is wetland (fen, bog, estuary)
int sdk_worldgen_ecology_is_wetland(uint8_t ecology);

// Check if ecology prefers turf/plants
int sdk_worldgen_ecology_prefers_turf(uint8_t ecology);

// Check if passive fauna can spawn
int sdk_worldgen_ecology_supports_passive_fauna(uint8_t ecology);
```

---

## Resources

### Resource Province

```c
typedef enum {
    RESOURCE_PROVINCE_NONE = 0,
    RESOURCE_PROVINCE_AGGREGATE_DISTRICT,
    RESOURCE_PROVINCE_CLAY_DISTRICT,
    RESOURCE_PROVINCE_CARBONATE_CEMENT_DISTRICT,
    RESOURCE_PROVINCE_COALFIELD,
    RESOURCE_PROVINCE_IRON_BELT,
    RESOURCE_PROVINCE_VOLCANIC_METALS,
    RESOURCE_PROVINCE_OIL_FIELD,
    RESOURCE_PROVINCE_COPPER_PORPHYRY_BELT,
    RESOURCE_PROVINCE_BAUXITE_DEPOSIT,
    RESOURCE_PROVINCE_RARE_EARTH_DISTRICT,
    RESOURCE_PROVINCE_URANIUM_GRANITE_BELT,
    RESOURCE_PROVINCE_SULFUR_VOLCANIC_DISTRICT,
    RESOURCE_PROVINCE_SALTPETER_NITRATE_FIELD,
    RESOURCE_PROVINCE_PHOSPHATE_DEPOSIT,
    RESOURCE_PROVINCE_STRATEGIC_ALLOY_BELT,
    RESOURCE_PROVINCE_LEAD_ZINC_DISTRICT
} SdkResourceProvince;
```

### Hydrocarbon Class

```c
typedef enum {
    SDK_HYDROCARBON_NONE = 0,
    SDK_HYDROCARBON_OIL_SHALE,
    SDK_HYDROCARBON_OIL_SAND,
    SDK_HYDROCARBON_CRUDE_OIL,
    SDK_HYDROCARBON_NATURAL_GAS
} SdkHydrocarbonClass;
```

### Surface Water Class

```c
typedef enum {
    SURFACE_WATER_NONE = 0,
    SURFACE_WATER_OPEN,
    SURFACE_WATER_SEASONAL_ICE,
    SURFACE_WATER_PERENNIAL_ICE
} SdkSurfaceWaterClass;
```

### Landform Flags

```c
typedef enum {
    SDK_LANDFORM_NONE          = 0u,
    SDK_LANDFORM_RIVER_CHANNEL = 1u << 0,
    SDK_LANDFORM_FLOODPLAIN    = 1u << 1,
    SDK_LANDFORM_RAVINE        = 1u << 2,
    SDK_LANDFORM_LAKE_BASIN    = 1u << 3,
    SDK_LANDFORM_CAVE_ENTRANCE = 1u << 4,
    SDK_LANDFORM_VOLCANIC_VENT = 1u << 5,
    SDK_LANDFORM_CALDERA       = 1u << 6,
    SDK_LANDFORM_LAVA_FIELD    = 1u << 7
} SdkLandformFlags;
```

---

## Terrain Column Profile

```c
typedef struct {
    // Heights (meters)
    int16_t base_height;           // Bedrock surface
    int16_t surface_height;        // Ground surface
    int16_t water_height;          // Water table/river
    int16_t river_bed_height;      // River channel bottom
    
    // Depths (blocks)
    uint8_t soil_depth;            // Soil layer thickness
    uint8_t sediment_depth;        // Sediment thickness
    uint8_t sediment_thickness;    // Active sediment
    uint8_t regolith_thickness;    // Weathered material
    
    // Hydrology
    uint8_t river_order;           // Stream order (Strahler)
    uint8_t floodplain_width;      // Flood zone width
    uint8_t water_table_depth;     // Depth to saturated zone
    
    // Classifications
    SdkTerrainProvince    terrain_province;
    SdkBedrockProvince    bedrock_province;
    SdkTemperatureBand    temperature_band;
    SdkMoistureBand       moisture_band;
    SdkSurfaceSediment    surface_sediment;
    SdkParentMaterialClass parent_material;
    SdkDrainageClass      drainage_class;
    SdkSoilReactionClass  soil_reaction;
    SdkSoilFertilityClass soil_fertility;
    SdkSoilSalinityClass  soil_salinity;
    SdkSurfaceWaterClass  water_surface_class;
    SdkBiomeEcology       ecology;
    SdkResourceProvince   resource_province;
    SdkHydrocarbonClass   hydrocarbon_class;
    
    // Resource grade (0-255 quality)
    uint8_t resource_grade;
    
    // Landform flags (bitmask)
    uint32_t landform_flags;
} SdkTerrainColumnProfile;
```

### Key Profile Fields

| Field | Range | Description |
|-------|-------|-------------|
| `surface_height` | -64 to ~32000 | World Y of ground surface |
| `base_height` | -64 to surface | Bedrock elevation |
| `water_height` | varies | Water table or river level |
| `soil_depth` | 0-255 | Thickness of soil layer in blocks |
| `river_order` | 0-8 | Stream hierarchy (0 = no river) |
| `landform_flags` | bitmask | See SdkLandformFlags |

---

## Resource Signature

```c
typedef struct {
    float hydrocarbon_score;
    float coal_score;
    float iron_score;
    float carbonate_score;
    float clay_score;
    float sand_score;
    float timber_score;
    float sulfur_score;
} SdkResourceSignature;
```

Used by `sdk_worldgen_scan_resource_signature()` for strategic placement of settlements and spawn points.

---

## AI Context Hints

### Adding New Terrain Province

```c
// 1. Add to enum in sdk_worldgen_types.h
typedef enum {
    // ... existing provinces ...
    TERRAIN_PROVINCE_KARST_PLATEAU,  // New
    TERRAIN_PROVINCE_COUNT
} SdkTerrainProvince;

// 2. Add to internal province family classification
// In sdk_worldgen_internal.h or similar:
case TERRAIN_PROVINCE_KARST_PLATEAU:
    return SDK_WORLDGEN_PROVINCE_FAMILY_BASIN_UPLAND;

// 3. Add generation logic in continental or macro layer
// Set appropriate flags in continental generation

// 4. Add surface material mapping
// In column surface sampling:
case TERRAIN_PROVINCE_KARST_PLATEAU:
    return SEDIMENT_CALCAREOUS_RESIDUAL;
```

### Creating Custom Ecology

```c
// Add to SdkBiomeEcology enum
ECOLOGY_BAMBOO_FOREST,

// Add ecology helper
int sdk_worldgen_ecology_is_bamboo_friendly(uint8_t ecology) {
    return ecology == ECOLOGY_BAMBOO_FOREST ||
           ecology == ECOLOGY_TROPICAL_RAINFOREST;
}

// Add tree placement rule in chunk fill
if (sdk_worldgen_ecology_is_bamboo_friendly(profile->ecology)) {
    // Place bamboo instead of normal trees
    place_bamboo_cluster(wx, surface_y, wz);
}
```

### Resource Scanner Usage

```c
// Find good settlement location based on resources
SdkResourceSignature sig;
sdk_worldgen_scan_resource_signature(wg, center_x, center_z, 
                                     256, // 256-block radius
                                     &sig);

// Score location
float settlement_score = sig.iron_score * 2.0f +
                         sig.coal_score * 1.5f +
                         sig.timber_score * 0.5f +
                         sig.clay_score * 0.3f;

if (settlement_score > MIN_SETTLEMENT_THRESHOLD) {
    // Good location for industrial settlement
}
```

---

## Related Documentation

- [SDK_Worldgen.md](../SDK_Worldgen.md) - Public API and generation pipeline
- [SDK_WorldgenInternal.md](../Internal/SDK_WorldgenInternal.md) - Internal cache structures
- [SDK_WorldgenColumn.md](../Column/SDK_WorldgenColumn.md) - Column sampling
- [SDK_WorldgenContinental.md](../Continental/SDK_WorldgenContinental.md) - Climate and hydrology

---

**Source Files:**
- `SDK/Core/World/Worldgen/Types/sdk_worldgen_types.h` (260 lines) - Public type definitions
