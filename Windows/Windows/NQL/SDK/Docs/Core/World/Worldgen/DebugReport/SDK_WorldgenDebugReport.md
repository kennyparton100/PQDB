# SDK Worldgen Debug Report Documentation

Comprehensive documentation for world generation debug capture and reporting.

**Module:** `SDK/Core/World/Worldgen/DebugReport/`  
**Source:** `sdk_worldgen_debug_report.c`

## Table of Contents

- [Module Overview](#module-overview)
- [Debug Capture System](#debug-capture-system)
- [Capture API](#capture-api)
- [Report Format](#report-format)
- [Usage Patterns](#usage-patterns)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Debug Report module provides detailed logging of world generation decisions for a single chunk. It captures information about terrain features, flora placement, water handling, settlement integration, and route generation to aid debugging and development.

**Key Features:**
- Per-chunk debug capture
- Thread-local capture context
- Structured report format
- Multiple note types (trees, plants, walls, routes, etc.)
- Custom message support

**Output:** Text report saved to `DebugReports/` directory

---

## Debug Capture System

### Capture Lifecycle

```c
// 1. Begin capture at chunk start
SdkWorldGenChunkDebugCapture* capture = 
    sdk_worldgen_debug_capture_get_thread_current();
sdk_worldgen_debug_capture_begin(capture);

// 2. Generation notes during chunk fill
// (Various sdk_worldgen_debug_capture_note_* calls)

// 3. End capture when done
sdk_worldgen_debug_capture_end();

// 4. Emit report
sdk_worldgen_emit_chunk_debug_report(wg, chunk_manager, cx, cz);
```

### Capture Structure

```c
typedef struct SdkWorldGenChunkDebugCapture {
    int cx, cz;                    // Chunk coordinates
    uint32_t timestamp;
    
    // Tree placements
    struct {
        int lx, lz;
        uint8_t archetype;
        int trunk_height;
    } trees[128];
    int tree_count;
    
    // Plant placements
    struct {
        int lx, lz;
        BlockType block;
    } plants[256];
    int plant_count;
    
    // Water seals
    struct {
        int lx, lz;
        int waterline;
        BlockType cap_block;
        int banked;
    } water_seals[64];
    int water_seal_count;
    
    // Wall/gate columns
    struct {
        int lx, lz;
        uint8_t wall_mask;
        uint8_t gate_mask;
        int wall_top_y;
        int gate_floor_y;
    } walls[32];
    int wall_count;
    
    // Settlement stages reached
    int settlement_stage_count;
    
    // Routes generated
    struct {
        int surface;
        int start_kind, end_kind;
        int start_y, end_y;
        int max_cut, max_fill;
        int carved_columns;
        int candidate_index;
    } routes[16];
    int route_count;
    
    // Custom messages
    char messages[16][256];
    int message_count;
    
} SdkWorldGenChunkDebugCapture;
```

---

## Capture API

### Thread Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_debug_capture_get_thread_current` | `(void) → SdkWorldGenChunkDebugCapture*` | Get thread-local capture |
| `sdk_worldgen_debug_capture_begin` | `(SdkWorldGenChunkDebugCapture*) → void` | Start capture |
| `sdk_worldgen_debug_capture_end` | `(void) → void` | End capture |

### Note Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_worldgen_debug_capture_note_tree` | `(lx, lz, archetype, trunk_height) → void` | Log tree placement |
| `sdk_worldgen_debug_capture_note_plant` | `(lx, lz, plant_block) → void` | Log plant/block placement |
| `sdk_worldgen_debug_capture_note_water_seal` | `(lx, lz, waterline, cap_block, banked) → void` | Log water sealing |
| `sdk_worldgen_debug_capture_note_wall_column` | `(lx, lz, wall_mask, gate_mask, wall_top_y, gate_floor_y) → void` | Log wall/gate |
| `sdk_worldgen_debug_capture_note_settlement_stage` | `(void) → void` | Mark settlement stage reached |
| `sdk_worldgen_debug_capture_note_route` | `(surface, start_kind, end_kind, start_y, end_y, max_cut, max_fill, carved_columns, candidate_index) → void` | Log route generation |
| `sdk_worldgen_debug_capture_note_custom` | `(const char* message) → void` | Custom message |

---

## Report Format

### File Location

```
DebugReports/
├── world_1234ABCD_cx0_cz0_20240115_143022.txt
├── world_1234ABCD_cx1_cz0_20240115_143025.txt
└── ...
```

### Report Structure

```
=== CHUNK DEBUG REPORT ===
Chunk: (0, 0)
World Seed: 1234ABCD
Timestamp: 2024-01-15 14:30:22

--- TERRAIN ---
Surface Height: 64m
Bedrock Height: -10m
Province: SILICICLASTIC_HILLS
Ecology: TEMPERATE_DECIDUOUS_FOREST

--- TREES (3) ---
[0] (10, 20): Oak, height=12
[1] (15, 25): Oak, height=10
[2] (30, 40): Birch, height=8

--- PLANTS (5) ---
[0] (12, 22): Tall Grass
[1] (16, 26): Tall Grass
[2] (31, 41): Fern
[3] (45, 50): Flower
[4] (50, 55): Berry Bush

--- WATER SEALS (1) ---
[0] (20, 30): level=63, block=Water, banked=0

--- WALLS/GATES (2) ---
[0] (0, 32): wall_mask=0xFF, gate_mask=0x00, top=80
[1] (32, 0): wall_mask=0xFF, gate_mask=0x03, top=80, gate_floor=64

--- SETTLEMENT ---
Stages: 3
- Metadata generated
- Terrain modified
- Routes generated

--- ROUTES (1) ---
[0] Path from HUB to BUILDING
    Start: (100, 64), End: (120, 66)
    Max cut: 3, Max fill: 2
    Carved: 20 columns

--- CUSTOM MESSAGES ---
[0] "Warning: steep slope at (25, 35), reduced tree density"

=== END REPORT ===
```

---

## Usage Patterns

### Enabling Debug Capture

```c
// Enable via worldgen context
sdk_worldgen_set_debug_mode_ctx(wg, SDK_WORLDGEN_DEBUG_FORMATIONS);

// Or globally
sdk_worldgen_set_debug_mode(SDK_WORLDGEN_DEBUG_STRUCTURES);
```

### Capturing Specific Features

```c
// In chunk generation code:
void place_tree(int wx, int wy, int wz, int archetype, int height) {
    // Place tree blocks...
    
    // Log to debug capture
    int lx = wx % 64;
    int lz = wz % 64;
    sdk_worldgen_debug_capture_note_tree(lx, lz, archetype, height);
}
```

### Analyzing Reports

```c
// Read and parse debug report
void analyze_debug_report(const char* filename) {
    FILE* f = fopen(filename, "r");
    char line[256];
    
    int tree_count = 0;
    int plant_count = 0;
    int wall_count = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "TREES (")) {
            sscanf(line, "--- TREES (%d) ---", &tree_count);
        }
        // ... parse other sections
    }
    
    printf("Chunk contains: %d trees, %d plants, %d walls\n",
           tree_count, plant_count, wall_count);
}
```

---

## Integration Notes

### In Chunk Fill

```c
void sdk_worldgen_fill_chunk(SdkWorldGen* wg, SdkChunk* chunk) {
    SdkWorldGenChunkDebugCapture* capture = 
        sdk_worldgen_debug_capture_get_thread_current();
    
    // Start capture if in debug mode
    if (sdk_worldgen_get_debug_mode_ctx(wg) != SDK_WORLDGEN_DEBUG_OFF) {
        sdk_worldgen_debug_capture_begin(capture);
    }
    
    // 1. Clear and sample terrain
    // ...
    
    // 2. Carve caves
    // ...
    
    // 3. Add water
    // ...
    
    // 4. Place flora (with debug notes)
    for each tree position:
        place_tree(...);
        if (in_debug_mode) {
            sdk_worldgen_debug_capture_note_tree(lx, lz, 
                                                 tree_archetype, height);
        }
    
    // 5. Settlement integration
    if (settlement) {
        sdk_worldgen_debug_capture_note_settlement_stage();
        apply_settlement_terrain(chunk, settlement);
        generate_routes(chunk, settlement);
    }
    
    // 6. Apply walls
    if (is_wall_chunk) {
        apply_walls(chunk);
        sdk_worldgen_debug_capture_note_wall_column(...);
    }
    
    // 7. Seal water
    seal_water(chunk);
    sdk_worldgen_debug_capture_note_water_seal(...);
    
    // End capture and emit report
    if (in_debug_mode) {
        sdk_worldgen_debug_capture_end();
        sdk_worldgen_emit_chunk_debug_report(wg, cm, cx, cz);
    }
}
```

### Debug Mode Selection

```c
typedef enum {
    SDK_WORLDGEN_DEBUG_OFF = 0,        // No debug output
    SDK_WORLDGEN_DEBUG_FORMATIONS,     // Basic terrain info
    SDK_WORLDGEN_DEBUG_STRUCTURES,   // Include settlement/routes
    SDK_WORLDGEN_DEBUG_BODIES          // Include resource bodies
} SdkWorldGenDebugMode;
```

---

## AI Context Hints

### Custom Capture Fields

```c
// Extend capture for specific debugging needs
typedef struct SdkWorldGenChunkDebugCapture {
    // ... existing fields ...
    
    // Custom: ore placement
    struct {
        BlockType ore_type;
        int y_level;
        int quantity;
    } ores[32];
    int ore_count;
    
    // Custom: cave placement
    struct {
        int lx, ly, lz;
        int cave_type;
    } caves[64];
    int cave_count;
} SdkWorldGenChunkDebugCapture;

void sdk_worldgen_debug_capture_note_ore(BlockType ore, int y, int qty) {
    SdkWorldGenChunkDebugCapture* cap = 
        sdk_worldgen_debug_capture_get_thread_current();
    int i = cap->ore_count++;
    cap->ores[i].ore_type = ore;
    cap->ores[i].y_level = y;
    cap->ores[i].quantity = qty;
}
```

### Conditional Capture

```c
// Only capture for specific chunks
void maybe_begin_capture(SdkWorldGen* wg, int cx, int cz) {
    // Capture spawn chunks
    if (abs(cx) <= 2 && abs(cz) <= 2) {
        sdk_worldgen_debug_capture_begin(
            sdk_worldgen_debug_capture_get_thread_current());
        return;
    }
    
    // Capture chunks with specific features
    if (chunk_has_settlement(wg, cx, cz)) {
        sdk_worldgen_debug_capture_begin(...);
    }
}
```

### Report Comparison

```c
// Compare two chunk reports
void compare_reports(const char* report_a, const char* report_b) {
    ChunkReport a = parse_report(report_a);
    ChunkReport b = parse_report(report_b);
    
    printf("Differences:\n");
    
    if (a.tree_count != b.tree_count) {
        printf("  Trees: %d vs %d\n", a.tree_count, b.tree_count);
    }
    
    if (a.wall_count != b.wall_count) {
        printf("  Walls: %d vs %d\n", a.wall_count, b.wall_count);
    }
    
    // Check specific wall positions
    for (int i = 0; i < a.wall_count && i < b.wall_count; i++) {
        if (a.walls[i].lx != b.walls[i].lx ||
            a.walls[i].lz != b.walls[i].lz) {
            printf("  Wall %d position differs: (%d,%d) vs (%d,%d)\n",
                   i, a.walls[i].lx, a.walls[i].lz,
                   b.walls[i].lx, b.walls[i].lz);
        }
    }
}
```

### Automated Analysis

```c
// Scan all reports for anomalies
void analyze_all_reports(const char* reports_dir) {
    DIR* dir = opendir(reports_dir);
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, ".txt")) continue;
        
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", reports_dir, entry->d_name);
        
        ChunkReport report = parse_report(path);
        
        // Check for anomalies
        if (report.wall_count == 0 && chunk_should_have_walls(report.cx, report.cz)) {
            printf("WARNING: %s - missing walls!\n", entry->d_name);
        }
        
        if (report.water_seal_count == 0 && has_water(report)) {
            printf("WARNING: %s - water not sealed!\n", entry->d_name);
        }
        
        if (report.settlement_stage_count > 0 && report.route_count == 0) {
            printf("WARNING: %s - settlement but no routes!\n", entry->d_name);
        }
    }
    
    closedir(dir);
}
```

---

## Related Documentation

- [SDK_Worldgen.md](../SDK_Worldgen.md) - Public API and debug modes
- [SDK_WorldgenInternal.md](../Internal/SDK_WorldgenInternal.md) - Capture structures
- [SDK_WorldgenColumn.md](../Column/SDK_WorldgenColumn.md) - Column generation
- [SDK_SettlementRoads.md](../../Settlements/Roads/SDK_SettlementRoads.md) - Route generation

---

**Source Files:**
- `SDK/Core/World/Worldgen/DebugReport/sdk_worldgen_debug_report.c`
