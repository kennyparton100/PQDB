# SDK Settlement Roads Documentation

Comprehensive documentation for the SDK settlement road and route generation system.

**Module:** `SDK/Core/World/Settlements/`  
**Output:** `SDK/Docs/Core/World/Settlements/Roads/SDK_SettlementRoads.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Route Types](#route-types)
- [Route Endpoints](#route-endpoints)
- [Route Generation](#route-generation)
- [Surface Material Selection](#surface-material-selection)
- [Key Functions](#key-functions)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Settlement Roads module generates path networks connecting settlements to buildings, hubs, gates, and peer towns. It handles terrain following, cut-and-fill earthworks, surface material selection based on local geology, and integration with superchunk wall gates.

**Key Features:**
- Three route surface types: Path, Road, City Road
- Automatic terrain following with cut/fill constraints
- Ecologically-aware surface material selection
- Hub-to-building, hub-to-gate, and inter-town connections
- Integration with superchunk gate system

---

## Route Types

### SdkSettlementRouteSurface Enum

| Type | Max Cut | Max Fill | Max Step | Ramp Span | Use Case |
|------|---------|----------|----------|-----------|----------|
| `SDK_SETTLEMENT_ROUTE_PATH` | 2 | 2 | 2 | 12 | Villages, light traffic |
| `SDK_SETTLEMENT_ROUTE_ROAD` | 3 | 3 | 2 | 18 | Towns, regular traffic |
| `SDK_SETTLEMENT_ROUTE_CITY_ROAD` | 4 | 4 | 3 | 24 | Cities, heavy traffic |

### Route Constraints

Routes are constrained by:
- **Cut depth**: How much terrain can be excavated
- **Fill depth**: How much terrain can be raised
- **Step height**: Maximum grade between adjacent points
- **Clearance**: Minimum air space above (3 for paths, 4 for roads)

---

## Route Endpoints

### SdkSettlementRouteEndpointKind Enum

| Kind | Description |
|------|-------------|
| `SDK_SETTLEMENT_ROUTE_ENDPOINT_UNKNOWN` | Unspecified endpoint |
| `SDK_SETTLEMENT_ROUTE_ENDPOINT_HUB` | Settlement center/market |
| `SDK_SETTLEMENT_ROUTE_ENDPOINT_BUILDING` | Individual building entrance |
| `SDK_SETTLEMENT_ROUTE_ENDPOINT_GATE` | Superchunk wall gate |
| `SDK_SETTLEMENT_ROUTE_ENDPOINT_PEER_TOWN` | Other settlement hub |

### SdkSettlementRouteEndpoint Structure

```c
typedef struct {
    int wx, wz;      // World XZ position
    int y;           // Target Y height
    uint8_t kind;    // EndpointKind
} SdkSettlementRouteEndpoint;
```

### Hub Endpoint

The settlement hub is the central meeting point:
- **Villages**: Located near the well (offset from center)
- **Towns/Cities**: Located at the market building
- **Height**: Surface level + 1, clamped to valid range

```c
// Village hub: offset from center
endpoint.wx = settlement->center_wx + 1;
endpoint.wz = settlement->center_wz + 3;

// Town/City hub: at market building
endpoint.wx = building->wx + building->footprint_x / 2;
endpoint.wz = building->wz + building->footprint_z;
```

### Building Entrance Endpoint

Building entrances are computed relative to the hub direction:

```
1. Calculate vector from building center to hub
2. If |dx| >= |dz|: entrance is on east/west face
3. If |dz| > |dx|: entrance is on north/south face
4. Position is centered on that face
```

### Gate Endpoint

Gate endpoints connect settlements to superchunk wall gates:

```c
// Gate positions (center of superchunk edge)
gate_center = SDK_SUPERCHUNK_GATE_START_BLOCK + SDK_SUPERCHUNK_GATE_WIDTH_BLOCKS / 2;

// Side 0 (West):  wx = origin_x + WALL_THICKNESS
// Side 1 (North): wz = origin_z + WALL_THICKNESS
// Side 2 (East):  wx = origin_x + BLOCK_SPAN - 1
// Side 3 (South): wz = origin_z + BLOCK_SPAN - 1
```

---

## Route Generation

### Route Point Generation

Routes are generated using Bresenham's line algorithm:

```c
// Build route from waypoints
SdkSettlementRouteCandidate candidate;
build_candidate_points(waypoints, waypoint_count, &candidate);

// Points are generated between each waypoint pair
// Maximum 4096 points per route (SDK_SETTLEMENT_ROUTE_MAX_POINTS)
```

### Target Height Calculation

For each route point, target Y is computed considering:
1. Surface height at that position
2. Distance from start/end (ramp weighting)
3. Step constraints between adjacent points
4. Cut/fill limits

```
weighted_sum = surface_y * 1.0 +
               start_y * start_weight * 2.0 +
               end_y * end_weight * 2.0
target_y = weighted_sum / total_weight
```

### Passes

1. **Forward pass**: Apply step limits from start to end
2. **Backward pass**: Apply step limits from end to start
3. **Clamp**: Ensure within cut/fill windows

---

## Surface Material Selection

### Natural Coarse Blocks

Preferred for blending with natural terrain:
- `BLOCK_BEACH_GRAVEL`
- `BLOCK_COARSE_ALLUVIUM`
- `BLOCK_TALUS`
- `BLOCK_COLLUVIUM`

### Terrain-Aware Selection

```c
// Sample 5 points in a + pattern
offsets = {{0,0}, {-2,0}, {2,0}, {0,-2}, {0,2}}

// Count natural block occurrences
for each offset:
    profile = sample_terrain(wx + offset[0], wz + offset[1])
    visual = get_surface_block(profile)
    if visual in natural_blocks:
        natural_counts[visual]++

// Use most common natural block, or fallback
```

### Fallback Selection

Based on terrain profile characteristics:

| Condition | Fallback Block |
|-----------|----------------|
| Natural coarse nearby | Use that block |
| Wetland/floodplain | `BLOCK_COMPACTED_FILL` |
| Desert/coastal sand | `BLOCK_SAND` or `BLOCK_MARINE_SAND` |
| Rocky uplands | `BLOCK_CRUSHED_STONE` |
| Default | `BLOCK_COMPACTED_FILL` |

### Terrain Preference Functions

**Soft fill preferred (wetlands):**
- `ECOLOGY_ESTUARY_WETLAND`, `ECOLOGY_RIPARIAN_FOREST`, `ECOLOGY_FEN`, etc.
- `TERRAIN_PROVINCE_ESTUARY_DELTA`, `TERRAIN_PROVINCE_FLOODPLAIN_ALLUVIAL_LOWLAND`

**Sand preferred (arid regions):**
- `ECOLOGY_HOT_DESERT`, `ECOLOGY_DUNE_COAST`, `ECOLOGY_SALT_DESERT`
- `TERRAIN_PROVINCE_ARID_FAN_STEPPE`, `TERRAIN_PROVINCE_SALT_FLAT_PLAYA`

**Stone preferred (rocky uplands):**
- `TERRAIN_PROVINCE_CARBONATE_UPLAND`, `TERRAIN_PROVINCE_HARDROCK_HIGHLAND`
- `TERRAIN_PROVINCE_FOLD_MOUNTAIN_BELT`, `TERRAIN_PROVINCE_ALPINE_BELT`

---

## Key Functions

### Route Generation

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_settlement_generate_routes_for_chunk` | `(wg, chunk, data, settlement, layout) → void` | Generate all routes for settlement |

### Internal Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `settlement_hub_endpoint` | `(wg, settlement, layout) → SdkSettlementRouteEndpoint` | Get hub position |
| `building_entrance_endpoint` | `(settlement, building, hub) → SdkSettlementRouteEndpoint` | Get building entrance |
| `nearest_gate_endpoint` | `(wg, settlement) → SdkSettlementRouteEndpoint` | Find closest gate |
| `nearest_peer_town_endpoint` | `(wg, data, settlement, out, id) → int` | Find nearest town |
| `build_candidate_points` | `(waypoints, count, out) → int` | Generate route points |
| `clamp_route_candidate_targets` | `(wg, chunk, candidate, start, end, surface) → void` | Apply constraints |
| `route_select_local_surface_block` | `(wg, chunk, wx, wz) → BlockType` | Choose surface material |

### Constraint Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `route_max_cut_for_surface` | int | Max excavation depth |
| `route_max_fill_for_surface` | int | Max raise height |
| `route_max_step_for_surface` | int | Max grade step |
| `route_ramp_span_for_surface` | int | Blend distance to endpoints |
| `route_clearance_height_for_surface` | int | Minimum air clearance |

---

## API Surface

### Public Header (sdk_settlement_roads.h)

```c
#ifndef NQLSDK_SETTLEMENT_ROADS_H
#define NQLSDK_SETTLEMENT_ROADS_H

#include "sdk_settlement.h"

/* Route surface types */
typedef enum {
    SDK_SETTLEMENT_ROUTE_PATH = 0,
    SDK_SETTLEMENT_ROUTE_ROAD,
    SDK_SETTLEMENT_ROUTE_CITY_ROAD
} SdkSettlementRouteSurface;

/* Route endpoint kinds */
typedef enum {
    SDK_SETTLEMENT_ROUTE_ENDPOINT_UNKNOWN = 0,
    SDK_SETTLEMENT_ROUTE_ENDPOINT_HUB,
    SDK_SETTLEMENT_ROUTE_ENDPOINT_BUILDING,
    SDK_SETTLEMENT_ROUTE_ENDPOINT_GATE,
    SDK_SETTLEMENT_ROUTE_ENDPOINT_PEER_TOWN
} SdkSettlementRouteEndpointKind;

/* Route endpoint */
typedef struct {
    int wx, wz;
    int y;
    uint8_t kind;
} SdkSettlementRouteEndpoint;

/* Route point along path */
typedef struct {
    int wx, wz;
    int surface_y;
    int target_y;
} SdkSettlementRoutePoint;

/* Route candidate with points */
typedef struct {
    SdkSettlementRoutePoint points[SDK_SETTLEMENT_ROUTE_MAX_POINTS];  // 4096 max
    int count;
    float score;
    int candidate_index;
} SdkSettlementRouteCandidate;

/* Generate routes for a settlement chunk */
void sdk_settlement_generate_routes_for_chunk(SdkWorldGen* wg,
                                               SdkChunk* chunk,
                                               const SuperchunkSettlementData* settlement_data,
                                               const SettlementMetadata* settlement,
                                               const SettlementLayout* layout);

#endif
```

---

## Integration Notes

### World Generation Integration

```c
void generate_settlement_terrain(SdkWorldGen* wg, SdkChunk* chunk,
                                  Settlement* settlement) {
    // 1. Generate base terrain
    sdk_worldgen_generate_chunk_ctx(wg, chunk);
    
    // 2. Apply settlement foundation (clear terrain for buildings)
    apply_settlement_foundation(chunk, settlement);
    
    // 3. Generate routes/roads
    sdk_settlement_generate_routes_for_chunk(
        wg, chunk, 
        settlement_data, 
        settlement->metadata, 
        settlement->layout
    );
    
    // 4. Apply superchunk walls
    if (chunk_is_wall_chunk(chunk->cx, chunk->cz)) {
        apply_superchunk_walls(chunk);
    }
}
```

### Route Clearance

Blocks that don't obstruct routes:
- Air, water, ice, sea ice
- Logs, leaves (can be cleared)
- Grass, ferns, shrubs, heather
- Cattails, wildflowers, cactus
- Moss, berry bushes, reeds

### Settlement Types and Routes

| Settlement Type | Route Type | Hub Location |
|-----------------|------------|--------------|
| Village | Path | Near well building |
| Town | Road | At market building |
| City | City Road | At market building |

### Superchunk Gate Connection

Towns and cities automatically connect to:
1. Nearest peer town (if exists in same superchunk)
2. Nearest wall gate (if no peer town)

```c
if (settlement->type >= SETTLEMENT_TYPE_TOWN) {
    // Try to connect to another town first
    if (!nearest_peer_town_endpoint(...)) {
        // Fall back to nearest gate
        endpoint = nearest_gate_endpoint(wg, settlement);
    }
}
```

---

## AI Context Hints

### Custom Route Surface Type

```c
// Add a new route type for specialized settlements
typedef enum {
    // ... existing types ...
    SDK_SETTLEMENT_ROUTE_HIGHWAY = 3  // New: major arterial
} SdkSettlementRouteSurface;

// Add constraints for new type
int route_max_cut_for_surface(SdkSettlementRouteSurface surface) {
    switch (surface) {
        // ... existing cases ...
        case SDK_SETTLEMENT_ROUTE_HIGHWAY: return 6;  // Deep cuts OK
    }
}

int route_max_fill_for_surface(SdkSettlementRouteSurface surface) {
    switch (surface) {
        // ... existing cases ...
        case SDK_SETTLEMENT_ROUTE_HIGHWAY: return 6;  // Tall embankments OK
    }
}
```

### Custom Surface Material

```c
// Use specific material for themed settlements
BlockType select_themed_surface(SdkWorldGen* wg, int wx, int wz, 
                                 SettlementTheme theme) {
    switch (theme) {
        case SETTLEMENT_THEME_DWARVEN:
            // Prefer stone in all conditions
            return BLOCK_CRUSHED_STONE;
            
        case SETTLEMENT_THEME_ELVEN:
            // Use gravel that blends with forest
            return BLOCK_COARSE_ALLUVIUM;
            
        case SETTLEMENT_THEME_DESERT:
            // Force sand regardless of terrain
            return BLOCK_SAND;
            
        default:
            // Use standard selection
            return route_select_local_surface_block(wg, NULL, wx, wz);
    }
}
```

### Route Debug Visualization

```c
void debug_draw_route(const SdkSettlementRouteCandidate* route) {
    // Draw line between all points
    for (int i = 0; i < route->count - 1; i++) {
        SdkSettlementRoutePoint* a = &route->points[i];
        SdkSettlementRoutePoint* b = &route->points[i + 1];
        
        // Color by height difference
        uint32_t color = (abs(a->target_y - b->target_y) > 1) 
                        ? 0xFF0000FF   // Blue: steep grade
                        : 0xFF00FF00;  // Green: gentle
        
        draw_debug_line(a->wx, a->target_y, a->wz,
                       b->wx, b->target_y, b->wz,
                       color);
    }
    
    // Draw endpoints
    draw_debug_sphere(route->points[0].wx, 
                     route->points[0].target_y, 
                     route->points[0].wz, 
                     1.0f, 0xFF00FF00);  // Green: start
    
    draw_debug_sphere(route->points[route->count-1].wx, 
                     route->points[route->count-1].target_y, 
                     route->points[route->count-1].wz, 
                     1.0f, 0xFFFF0000);  // Red: end
}
```

### Waypoint-Based Routing

```c
// Create routes with intermediate waypoints for scenic paths
void generate_scenic_route(SdkWorldGen* wg, SdkChunk* chunk,
                           SdkSettlementRouteEndpoint start,
                           SdkSettlementRouteEndpoint end) {
    // Define scenic waypoints (e.g., view points, landmarks)
    SdkSettlementRouteEndpoint waypoints[5];
    int waypoint_count = 0;
    
    waypoints[waypoint_count++] = start;
    
    // Add waypoint at scenic overlook
    waypoints[waypoint_count++] = (SdkSettlementRouteEndpoint){
        .wx = scenic_overlook_wx,
        .wz = scenic_overlook_wz,
        .y = scenic_overlook_y,
        .kind = SDK_SETTLEMENT_ROUTE_ENDPOINT_UNKNOWN
    };
    
    // Add waypoint at bridge crossing
    waypoints[waypoint_count++] = (SdkSettlementRouteEndpoint){
        .wx = bridge_wx,
        .wz = bridge_wz,
        .y = bridge_y,
        .kind = SDK_SETTLEMENT_ROUTE_ENDPOINT_UNKNOWN
    };
    
    waypoints[waypoint_count++] = end;
    
    // Build route through all waypoints
    SdkSettlementRouteCandidate candidate;
    build_candidate_points(waypoints, waypoint_count, &candidate);
    
    // Apply constraints
    clamp_route_candidate_targets(wg, chunk, &candidate, 
                                   &start, &end, 
                                   SDK_SETTLEMENT_ROUTE_PATH);
    
    // Stamp into chunk
    stamp_route_into_chunk(chunk, &candidate);
}
```

### Route Statistics

```c
typedef struct {
    int total_points;
    int cut_columns;
    int fill_columns;
    int natural_surface_blocks;
    int fill_blocks;
    float avg_grade;
    float max_grade;
} RouteStats;

RouteStats analyze_route(const SdkSettlementRouteCandidate* route) {
    RouteStats stats = {0};
    stats.total_points = route->count;
    
    for (int i = 0; i < route->count; i++) {
        int surface = route->points[i].surface_y;
        int target = route->points[i].target_y;
        
        if (target < surface) stats.cut_columns++;
        if (target > surface) stats.fill_columns++;
        
        // Track grades
        if (i > 0) {
            int dy = abs(target - route->points[i-1].target_y);
            int dx = 1;  // Adjacent points
            float grade = (float)dy / dx;
            stats.avg_grade += grade;
            if (grade > stats.max_grade) stats.max_grade = grade;
        }
    }
    
    stats.avg_grade /= (route->count - 1);
    return stats;
}
```

---

## Related Documentation

- `SDK_SettlementSystem.md` - Settlement placement and lifecycle
- `SDK_Buildings.md` - Building types and markers
- `SDK_SuperChunks.md` - Wall gate geometry
- `SDK_Worldgen.md` - Terrain sampling
- `SDK_Blocks.md` - Block type definitions

---

**Source Files:**
- `SDK/Core/World/Settlements/sdk_settlement_roads.h` (600 bytes) - Public API
- `SDK/Core/World/Settlements/sdk_settlement_roads.c` (151KB) - Implementation
