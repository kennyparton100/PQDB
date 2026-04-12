# SDK Buildings Documentation

Comprehensive documentation for the SDK building family system, providing building type classification and runtime markers.

**Module:** `SDK/Core/World/Buildings/`  
**Output:** `SDK/Docs/Core/World/Buildings/SDK_Buildings.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Building Families](#building-families)
- [Building Types](#building-types)
- [Building Markers](#building-markers)
- [Prop Resolution](#prop-resolution)
- [Key Functions](#key-functions)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Buildings module provides building family classification and runtime marker computation for settlement buildings. It bridges the `BuildingType` enum (defined in Settlements) to functional families and provides prop asset resolution for building visualization.

**Key Features:**
- Building family classification (11 families)
- Runtime marker generation (entrances, work points, storage)
- Default prop ID resolution
- Settlement building integration

---

## Building Families

### SdkBuildingFamily Enum

Buildings are organized into functional families:

| Family | Purpose | Examples |
|--------|---------|----------|
| `SDK_BUILDING_FAMILY_NONE` | No family | Unclassified |
| `SDK_BUILDING_FAMILY_CIVIC_ADMIN` | Administration | Town hall, meeting house |
| `SDK_BUILDING_FAMILY_HOUSING_WORKFORCE` | Housing | Houses, dormitories |
| `SDK_BUILDING_FAMILY_STORAGE_LOGISTICS` | Storage | Warehouses, stockpiles |
| `SDK_BUILDING_FAMILY_EXTRACTION` | Resource extraction | Mines, wells, lumber camps |
| `SDK_BUILDING_FAMILY_BULK_PREP` | Food processing | Kitchens, bakeries, smokehouses |
| `SDK_BUILDING_FAMILY_UTILITIES` | Utilities | Water supply, lighting |
| `SDK_BUILDING_FAMILY_METALLURGY` | Metal working | Smithies, foundries, forges |
| `SDK_BUILDING_FAMILY_REPAIR_MAINTENANCE` | Maintenance | Repair shops, carpentries |
| `SDK_BUILDING_FAMILY_DEFENSE` | Defense | Watchtowers, walls, gates |
| `SDK_BUILDING_FAMILY_WATER_PORT` | Water transport | Docks, piers, harbors |

### Family Assignment

```c
// Get family for a building type
SdkBuildingFamily family = sdk_building_family_for_type(BUILDING_TYPE_SMITHY);
// Returns: SDK_BUILDING_FAMILY_METALLURGY
```

---

## Building Types

### Settlement Building Types

Building types are defined in `sdk_settlement_types.h`:

| Type | Description | Family |
|------|-------------|--------|
| `BUILDING_TYPE_NONE` | No building | NONE |
| `BUILDING_TYPE_HOUSE` | Residential housing | HOUSING_WORKFORCE |
| `BUILDING_TYPE_SMITHY` | Blacksmith workshop | METALLURGY |
| `BUILDING_TYPE_MINE` | Mining operation | EXTRACTION |
| `BUILDING_TYPE_FARM` | Agricultural building | BULK_PREP |
| `BUILDING_TYPE_BARRACKS` | Military housing | DEFENSE |
| `BUILDING_TYPE_STORAGE` | Storage building | STORAGE_LOGISTICS |
| `BUILDING_TYPE_WATCHTOWER` | Observation tower | DEFENSE |
| `BUILDING_TYPE_GATE` | Entry gate | DEFENSE |
| `BUILDING_TYPE_WELL` | Water source | UTILITIES |
| `BUILDING_TYPE_DOCK` | Water transport | WATER_PORT |
| ... | Additional types | Various |

### Building Type vs Family

The distinction allows:
- Multiple building types per family (variety in each category)
- Shared family behavior (all METALLURGY buildings use similar mechanics)
- Runtime marker customization per type

---

## Building Markers

### SdkBuildingMarkerType Enum

Markers define functional points within buildings:

| Marker | Purpose | Usage |
|--------|---------|-------|
| `SDK_BUILDING_MARKER_NONE` | No marker | Default |
| `SDK_BUILDING_MARKER_ENTRANCE` | Entry point | Pathfinding, spawning |
| `SDK_BUILDING_MARKER_WORK` | Work position | NPC job locations |
| `SDK_BUILDING_MARKER_STORAGE` | Storage point | Resource drop-off/pickup |
| `SDK_BUILDING_MARKER_SLEEP` | Rest position | NPC sleeping |
| `SDK_BUILDING_MARKER_PATROL` | Patrol point | Guard routes |
| `SDK_BUILDING_MARKER_WATER` | Water access | Drinking, fishing |
| `SDK_BUILDING_MARKER_STATION` | Workstation | Specific job sites |

### SdkBuildingRuntimeMarker Structure

```c
typedef struct {
    uint8_t marker_type;    // SdkBuildingMarkerType
    uint8_t facing;         // Direction (0-3: N, E, S, W)
    int32_t wx, wy, wz;     // World position
    BlockType required_block; // Required block at position
} SdkBuildingRuntimeMarker;
```

### Marker Computation

```c
// Compute markers for a building placement
SdkBuildingRuntimeMarker markers[16];
int count = sdk_building_compute_runtime_markers(
    &placement,      // Building placement info
    markers,         // Output array
    16               // Max markers
);

// Example: A house might have:
// - 1 ENTRANCE marker (door)
// - 4 SLEEP markers (bed positions)
// - 1 WORK marker (chores area)
```

---

## Prop Resolution

### Default Prop IDs

Each building type maps to a prop asset:

```c
const char* prop_id = sdk_building_default_prop_id(BUILDING_TYPE_SMITHY);
// Returns: "smithy" (or similar asset ID)
```

**Resolution Flow:**
1. Settlement requests building placement
2. Building type determines family and prop ID
3. `sdk_role_assets_resolve_prop()` validates asset exists
4. Prop instantiated in world
5. Runtime markers computed for NPC interaction

### Prop Asset Structure

```
Assets/Props/{prop_id}/
├── prop.txt          # Metadata
├── model.obj         # Mesh
├── textures/         # Texture files
└── markers/          # Optional marker definitions
```

---

## Key Functions

### Family Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_building_family_name` | `(SdkBuildingFamily) → const char*` | Get family display name |
| `sdk_building_family_for_type` | `(BuildingType) → SdkBuildingFamily` | Map type to family |

### Prop Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_building_default_prop_id` | `(BuildingType) → const char*` | Get default prop asset ID |

### Marker Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_building_compute_runtime_markers` | `(placement, out, max) → int` | Compute markers for placement |

---

## API Surface

### Public Header (sdk_building_family.h)

```c
/* =================================================================
 * BUILDING FAMILY ENUM
 * ================================================================= */
typedef enum {
    SDK_BUILDING_FAMILY_NONE = 0,
    SDK_BUILDING_FAMILY_CIVIC_ADMIN,
    SDK_BUILDING_FAMILY_HOUSING_WORKFORCE,
    SDK_BUILDING_FAMILY_STORAGE_LOGISTICS,
    SDK_BUILDING_FAMILY_EXTRACTION,
    SDK_BUILDING_FAMILY_BULK_PREP,
    SDK_BUILDING_FAMILY_UTILITIES,
    SDK_BUILDING_FAMILY_METALLURGY,
    SDK_BUILDING_FAMILY_REPAIR_MAINTENANCE,
    SDK_BUILDING_FAMILY_DEFENSE,
    SDK_BUILDING_FAMILY_WATER_PORT
} SdkBuildingFamily;

/* =================================================================
 * BUILDING MARKER ENUM
 * ================================================================= */
typedef enum {
    SDK_BUILDING_MARKER_NONE = 0,
    SDK_BUILDING_MARKER_ENTRANCE,
    SDK_BUILDING_MARKER_WORK,
    SDK_BUILDING_MARKER_STORAGE,
    SDK_BUILDING_MARKER_SLEEP,
    SDK_BUILDING_MARKER_PATROL,
    SDK_BUILDING_MARKER_WATER,
    SDK_BUILDING_MARKER_STATION
} SdkBuildingMarkerType;

/* =================================================================
 * BUILDING RUNTIME MARKER
 * ================================================================= */
typedef struct {
    uint8_t marker_type;
    uint8_t facing;
    uint16_t reserved0;
    int32_t wx, wy, wz;
    BlockType required_block;
} SdkBuildingRuntimeMarker;

/* =================================================================
 * FUNCTIONS
 * ================================================================= */
const char* sdk_building_family_name(SdkBuildingFamily family);
SdkBuildingFamily sdk_building_family_for_type(BuildingType type);
const char* sdk_building_default_prop_id(BuildingType type);
int sdk_building_compute_runtime_markers(const BuildingPlacement* placement,
                                          SdkBuildingRuntimeMarker* out_markers,
                                          int max_markers);
```

### Related Header (sdk_settlement_types.h)

```c
// BuildingType is defined in settlements module
typedef enum BuildingType {
    BUILDING_TYPE_NONE = 0,
    BUILDING_TYPE_HOUSE,
    BUILDING_TYPE_SMITHY,
    BUILDING_TYPE_MINE,
    BUILDING_TYPE_FARM,
    // ... etc
} BuildingType;

typedef struct BuildingPlacement {
    BuildingType type;
    int cx, cz;           // Chunk coordinates
    int lx, ly, lz;       // Local chunk coordinates
    uint8_t facing;       // Orientation
    // ... additional fields
} BuildingPlacement;
```

---

## Integration Notes

### Settlement Building Placement

```c
void place_building_in_settlement(Settlement* s, BuildingType type, 
                                   int wx, int wz, uint8_t facing) {
    // 1. Resolve prop asset
    const char* prop_id = sdk_building_default_prop_id(type);
    int missing;
    const char* asset_id = sdk_role_assets_resolve_prop(type, &missing);
    
    if (missing) {
        log_warning("Missing prop asset for building type %d", type);
        return;
    }
    
    // 2. Create building placement record
    BuildingPlacement placement = {
        .type = type,
        .cx = sdk_world_to_chunk_x(wx),
        .cz = sdk_world_to_chunk_z(wz),
        .lx = sdk_world_to_local_x(wx, placement.cx),
        .lz = sdk_world_to_local_z(wz, placement.cz),
        .facing = facing
    };
    
    // 3. Compute runtime markers
    SdkBuildingRuntimeMarker markers[16];
    int marker_count = sdk_building_compute_runtime_markers(
        &placement, markers, 16);
    
    // 4. Store in settlement data
    add_building_to_settlement(s, &placement, markers, marker_count);
    
    // 5. Spawn prop entity
    spawn_prop_entity(asset_id, wx, surface_y, wz, facing);
}
```

### NPC Job Assignment

```c
void assign_npc_to_building_job(SdkNpc* npc, Settlement* s, int building_idx) {
    Building* b = &s->buildings[building_idx];
    SdkBuildingFamily family = sdk_building_family_for_type(b->type);
    
    // Find appropriate work marker
    for (int i = 0; i < b->marker_count; i++) {
        SdkBuildingRuntimeMarker* marker = &b->markers[i];
        
        if (marker->marker_type == SDK_BUILDING_MARKER_WORK ||
            marker->marker_type == SDK_BUILDING_MARKER_STATION) {
            
            // Assign NPC to this work position
            npc->job.building_idx = building_idx;
            npc->job.marker_idx = i;
            npc->job.type = family_to_job_type(family);
            
            // Send NPC to work position
            npc_navigate_to(npc, marker->wx, marker->wy, marker->wz);
            break;
        }
    }
}
```

### Pathfinding Integration

```c
void find_path_to_building(Settlement* s, int from_wx, int from_wz, 
                           int building_idx, SdkPath* out_path) {
    Building* b = &s->buildings[building_idx];
    
    // Find entrance marker
    for (int i = 0; i < b->marker_count; i++) {
        if (b->markers[i].marker_type == SDK_BUILDING_MARKER_ENTRANCE) {
            SdkBuildingRuntimeMarker* entrance = &b->markers[i];
            
            // Path to entrance
            compute_path(from_wx, from_wz, 
                        entrance->wx, entrance->wz, 
                        out_path);
            return;
        }
    }
}
```

### Building Family Statistics

```c
void count_buildings_by_family(Settlement* s, int* out_counts) {
    memset(out_counts, 0, sizeof(int) * SDK_BUILDING_FAMILY_COUNT);
    
    for (int i = 0; i < s->building_count; i++) {
        BuildingType type = s->buildings[i].type;
        SdkBuildingFamily family = sdk_building_family_for_type(type);
        out_counts[family]++;
    }
}

void print_settlement_economy(Settlement* s) {
    int counts[SDK_BUILDING_FAMILY_COUNT];
    count_buildings_by_family(s, counts);
    
    printf("Settlement Economy:\n");
    printf("  Housing: %d\n", counts[SDK_BUILDING_FAMILY_HOUSING_WORKFORCE]);
    printf("  Production: %d\n", 
           counts[SDK_BUILDING_FAMILY_METALLURGY] + 
           counts[SDK_BUILDING_FAMILY_BULK_PREP]);
    printf("  Extraction: %d\n", counts[SDK_BUILDING_FAMILY_EXTRACTION]);
    printf("  Storage: %d\n", counts[SDK_BUILDING_FAMILY_STORAGE_LOGISTICS]);
}
```

---

## AI Context Hints

### Custom Building Marker Types

```c
// Extend marker types for modding
typedef enum {
    // ... existing markers ...
    SDK_BUILDING_MARKER_CUSTOM_START = 128,
    SDK_BUILDING_MARKER_PRAYER,      // Religious buildings
    SDK_BUILDING_MARKER_HEALING,     // Medical buildings
    SDK_BUILDING_MARKER_ENTERTAINMENT // Taverns, theaters
} ExtendedBuildingMarkerType;

// Compute markers with extended types
void compute_extended_markers(BuildingType type, const BuildingPlacement* placement,
                             SdkBuildingRuntimeMarker* out, int max) {
    int count = sdk_building_compute_runtime_markers(placement, out, max);
    
    // Add custom markers based on building type
    if (type == BUILDING_TYPE_TEMPLE && count < max) {
        out[count++] = (SdkBuildingRuntimeMarker){
            .marker_type = SDK_BUILDING_MARKER_PRAYER,
            .wx = placement->wx,
            .wy = placement->wy,
            .wz = placement->wz
        };
    }
    
    return count;
}
```

### Dynamic Prop Resolution

```c
// Select prop variant based on settlement properties
const char* select_prop_variant(BuildingType type, Settlement* s) {
    switch (type) {
        case BUILDING_TYPE_HOUSE:
            // Wealthy settlement gets better houses
            if (s->wealth > 1000) return "house_large";
            if (s->wealth > 500)  return "house_medium";
            return "house_small";
            
        case BUILDING_TYPE_SMITHY:
            // Tech level determines smithy type
            if (s->tech_level >= 3) return "smithy_advanced";
            return "smithy_basic";
            
        default:
            return sdk_building_default_prop_id(type);
    }
}
```

### Building Validation

```c
bool validate_building_placement(Settlement* s, BuildingType type,
                                  int wx, int wz) {
    // Check terrain suitability
    SdkTerrainColumnProfile profile;
    sdk_worldgen_sample_column_ctx(worldgen, wx, wz, &profile);
    
    SdkBuildingFamily family = sdk_building_family_for_type(type);
    
    switch (family) {
        case SDK_BUILDING_FAMILY_WATER_PORT:
            // Needs water access
            if (profile.water_surface_class == SURFACE_WATER_NONE) {
                return false;
            }
            break;
            
        case SDK_BUILDING_FAMILY_EXTRACTION:
            // Mines need resource deposits
            if (profile.resource_province == RESOURCE_PROVINCE_NONE) {
                return false;
            }
            break;
            
        case SDK_BUILDING_FAMILY_DEFENSE:
            // Towers need solid ground
            if (profile.base_height < 50) {
                return false;
            }
            break;
    }
    
    return true;
}
```

### Marker Debugging

```c
void debug_draw_building_markers(Settlement* s) {
    for (int i = 0; i < s->building_count; i++) {
        Building* b = &s->buildings[i];
        
        for (int j = 0; j < b->marker_count; j++) {
            SdkBuildingRuntimeMarker* m = &b->markers[j];
            
            // Draw colored debug marker
            uint32_t color;
            switch (m->marker_type) {
                case SDK_BUILDING_MARKER_ENTRANCE: color = 0xFF00FF00; break;  // Green
                case SDK_BUILDING_MARKER_WORK:     color = 0xFF0000FF; break;  // Blue
                case SDK_BUILDING_MARKER_STORAGE:  color = 0xFFFFFF00; break;  // Yellow
                case SDK_BUILDING_MARKER_SLEEP:    color = 0xFFFF00FF; break;  // Magenta
                default: color = 0xFFFFFFFF; break;  // White
            }
            
            draw_debug_box(m->wx, m->wy, m->wz, 0.5f, color);
        }
    }
}
```

### Building Efficiency Calculation

```c
float calculate_building_efficiency(Settlement* s, int building_idx) {
    Building* b = &s->buildings[building_idx];
    int work_markers = 0;
    int occupied_markers = 0;
    
    // Count work positions
    for (int i = 0; i < b->marker_count; i++) {
        if (b->markers[i].marker_type == SDK_BUILDING_MARKER_WORK ||
            b->markers[i].marker_type == SDK_BUILDING_MARKER_STATION) {
            work_markers++;
            
            // Check if position is occupied
            if (is_marker_occupied(s, building_idx, i)) {
                occupied_markers++;
            }
        }
    }
    
    if (work_markers == 0) return 1.0f;
    return (float)occupied_markers / work_markers;
}
```

---

## Related Documentation

- `SDK_Settlements.md` - Settlement system, building placement
- `SDK_RoleAssets.md` - Prop asset resolution
- `SDK_Entities.md` - NPC job assignment
- `SDK_Worldgen.md` - Terrain suitability analysis

---

**Source Files:**
- `SDK/Core/World/Buildings/sdk_building_family.h` (1,653 bytes) - Public API
- `SDK/Core/World/Buildings/sdk_building_family.c` (8,954 bytes) - Implementation
