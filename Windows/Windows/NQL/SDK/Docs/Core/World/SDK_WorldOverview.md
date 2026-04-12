<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../SDK_Overview.md) > [Core](../SDK_CoreOverview.md) > World

---

# SDK World Overview

High-level architectural overview of the SDK World module, describing how chunks, world generation, simulation, and persistence work together to create the game world.

**Module:** `SDK/Core/World/`  
**Output:** `SDK/Docs/Core/World/SDK_WorldOverview.md`

## Table of Contents

- [System Architecture](#system-architecture)
- [Chunk System](#chunk-system)
- [World Generation Pipeline](#world-generation-pipeline)
- [Superchunk Structure](#superchunk-structure)
- [Simulation System](#simulation-system)
- [Settlement System](#settlement-system)
- [Persistence](#persistence)
- [Module Interactions](#module-interactions)

---

## System Architecture

The World module is organized into 10 subsystems:

```
SDK/Core/World/
├── Blocks/         # Block type definitions and properties
├── Buildings/      # Building type registry (uses props)
├── Chunks/         # Chunk data, chunk manager, streaming
├── ConstructionCells/  # Sparse construction placement system
├── Persistence/    # World save/load, JSON world descriptors
├── Settlements/    # NPC settlements, roads, runtime simulation
├── Simulation/     # Fluid simulation, reservoirs, cell codes
├── SuperChunks/    # Wall geometry, superchunk configuration
├── Terrain/        # Terrain sampling helpers
└── Worldgen/       # Terrain generation, scheduling, tile caches
```

### Runtime Data Flow

```
World Generation:
    SdkWorldDesc
        → Continental tiles (broad climate, hydrology)
        → Macro tiles (128x128 block synthesis)
        → Region tiles (32x32 block geology)
        → Column profiles (surface sampling)
        → Chunk fill (4096 block placement)
        → Settlement overlay
        → Superchunk wall sealing

Chunk Lifecycle:
    Desired (chunk manager)
        → Async Generation (worldgen scheduler)
        → Resident (chunk manager slot)
        → Meshed (MeshBuilder)
        → GPU Upload (Renderer)
        → Simulated (Fluid sim)
        → Persisted (Save file)

Settlement Lifecycle:
    Terrain Analysis
        → Placement (suitable location)
        → Foundation (clear terrain)
        → Layout (building positions)
        → Roads (path network)
        → Runtime (NPC simulation)
```

---

## Chunk System

### Chunk Dimensions

```c
#define CHUNK_WIDTH   64    // X dimension in blocks
#define CHUNK_DEPTH   64    // Z dimension in blocks  
#define CHUNK_HEIGHT  1024  // Y dimension in blocks

// Derived constants
#define CHUNK_SUBCHUNK_HEIGHT 64
#define CHUNK_SUBCHUNK_COUNT  16  // 1024 / 64
#define CHUNK_BLOCKS_PER_LAYER (64 * 64)  // 4096
#define CHUNK_TOTAL_BLOCKS     (64 * 64 * 1024)  // 4,194,304
```

**Key Properties:**
- 64×64×1024 blocks = 4.2 million blocks per chunk
- Split into 16 subchunks (64 blocks tall each)
- Subchunks enable partial mesh updates and culling
- World coordinates use block precision (integers)

### Chunk Coordinate System

```
World Space → Chunk Space:
    chunk_x = floor(world_x / 64)
    chunk_z = floor(world_z / 64)

Chunk Space → Local Space:
    local_x = world_x - (chunk_x * 64)  // 0-63
    local_y = world_y                    // 0-1023
    local_z = world_z - (chunk_z * 64)  // 0-63
```

### Residency Management

The `SdkChunkManager` maintains two sets:

- **Desired Set:** Chunks that should exist (based on camera position)
- **Resident Set:** Chunks that currently have allocated slots

**Residency Roles:**
- `SDK_CHUNK_ROLE_PRIMARY` - Core 16×16 superchunk
- `SDK_CHUNK_ROLE_WALL_SUPPORT` - Wall structure support
- `SDK_CHUNK_ROLE_FRONTIER` - Outer ring around superchunk
- `SDK_CHUNK_ROLE_TRANSITION_PRELOAD` - Preloading during superchunk transition
- `SDK_CHUNK_ROLE_EVICT_PENDING` - Marked for removal

---

## World Generation Pipeline

The worldgen system produces terrain through progressive refinement:

### Layer Stack

```
1. Continental Layer (broadest)
   - Height, coast distance, ocean mask
   - Precipitation, runoff
   - Lake/basin detection
   - Water access scoring

2. Macro Layer (128×128 block tiles)
   - Terrain synthesis
   - Hydrology refinement
   - River network

3. Region Layer (32×32 block tiles)
   - Geology provinces
   - Stratigraphy boundaries
   - Soil descriptors
   - Resource deposits

4. Column Sampling
   - SdkTerrainColumnProfile per (x,z)
   - Surface height, water table
   - Biome, soil fertility

5. Chunk Fill
   - Bedrock, soil, sediment
   - Caves, water, lava
   - Flora (trees, plants)
   - Settlement modifications
   - Wall application
```

### Key Data Structures

**SdkTerrainColumnProfile:**
```c
typedef struct {
    int16_t base_height;           // Bedrock surface
    int16_t surface_height;        // Ground level
    int16_t water_height;          // Water surface (if present)
    SdkTerrainProvince terrain_province;
    SdkBedrockProvince bedrock_province;
    SdkTemperatureBand temperature_band;
    SdkMoistureBand moisture_band;
    SdkBiomeEcology ecology;
    SdkResourceProvince resource_province;
    // ... 30+ fields total
} SdkTerrainColumnProfile;
```

---

## Superchunk Structure

Superchunks are the fundamental organizational unit for world generation and residency.

### Geometry

```c
// Legacy constants (still used for compatibility)
#define SDK_SUPERCHUNK_CHUNK_SPAN_LEGACY 16
#define SDK_SUPERCHUNK_BLOCK_SPAN_LEGACY (16 * 64)  // 1024 blocks
#define SDK_SUPERCHUNK_WALL_PERIOD_LEGACY 17  // 16 + 1 separator

// Runtime-configurable values
SDK_SUPERCHUNK_CHUNK_SPAN  // 16 chunks (default)
SDK_SUPERCHUNK_BLOCK_SPAN  // 1024 blocks
SDK_SUPERCHUNK_WALL_PERIOD // 17 chunks (16 + wall)
```

### Wall System

**Purpose:** Physical barriers between superchunks that:
- Prevent players from walking out of generated areas
- Create natural boundaries
- Support gate structures for controlled passage

**Wall Placement:**
- Walls occur every 17th chunk (`chunk_x % 17 == 0` or `chunk_z % 17 == 0`)
- 64 blocks thick (1 chunk)
- Stone brick material (block 114)
- Gates are 64-block openings centered at chunk positions 6-9

### Residency Layout

For a fully-resident superchunk:

```
Total: 340 chunks
├── 256 primary (16×16 core)
├── 68 outer ring frontier
└── 16 gate frontier support
```

### Runtime Configuration

The superchunk system now supports configurable sizes and a live detached wall grid:

```c
typedef struct SdkSuperchunkConfig {
    int chunk_span;          // Terrain interior span
    int wall_grid_size;      // Live detached wall-grid input
    int wall_grid_offset_x;  // Live detached wall-grid offset
    int wall_grid_offset_z;
    bool walls_detached;     // Enables detached wall-grid classification
} SdkSuperchunkConfig;
```

Current runtime behavior:

- terrain superchunk ownership uses the shared-boundary model (`period = chunk_span + 1`)
- detached wall settings are no longer metadata only
- attached mode normalizes `wall_grid_size = chunk_span`
- detached mode defaults to `wall_grid_size = chunk_span + 2`

---

## Simulation System

The simulation system handles runtime fluid and material behavior.

### Fluid Simulation

**Reservoir-Based System:**
- Groups connected fluid cells into reservoirs
- Solves hydraulic equilibrium per reservoir
- Supports water and lava

**Cell States:**
```c
typedef struct {
    SdkSimCellKey key;    // Packed (x,y,z)
    uint8_t fill;         // 0-255 fill level
    uint8_t material_kind;// Water, lava, etc.
    uint8_t flags;        // Flow flags
    uint8_t settled_ticks;// Stability counter
} SdkFluidCellState;
```

### Granular Materials

- Sand, gravel, and other loose materials
- Angle of repose simulation
- Collapse/settling behavior

### Integration

```c
// On chunk load
sdk_simulation_on_chunk_loaded(cm, chunk);

// Per-frame tick
sdk_simulation_tick_chunk_manager(cm, max_cells_per_frame);

// On block change
sdk_simulation_on_block_changed(cm, wx, wy, wz, old_type, new_type);
```

---

## Settlement System

Settlements are procedurally placed NPC communities.

### Lifecycle Phases

1. **Placement** - Find suitable terrain location
2. **Foundation** - Clear vegetation, level terrain
3. **Layout** - Position buildings, gates, wells
4. **Roads** - Generate path network
5. **Runtime** - Daily NPC simulation

### Building Types

| Type | Purpose |
|------|---------|
| HOUSE | Residential |
| SMITHY | Crafting, tools |
| MINE | Resource extraction |
| FARM | Agriculture |
| BARRACKS | Military |

### Road Network

- Main roads: Wide, connect gates to center
- Branch roads: Medium, connect buildings to main roads
- Paths: Narrow, pedestrian shortcuts

---

## Persistence

### Save File Structure

```
SaveDirectory/
├── World.json           # World descriptor (seed, name, etc.)
├── Region/
│   └── r.X.Z.bin        # Region files with chunks
└── Superchunks/
    └── sc.X.Z.json      # Per-superchunk settlement data
```

### Chunk Serialization

Chunks store:
- Block data (compressed)
- Construction cell references
- Simulation state (fluid cells)
- Mesh data (regeneratable, often not saved)

### World Descriptor

```json
{
  "name": "My World",
  "seed": 123456789,
  "version": 1,
  "created": "2024-01-15T10:30:00Z",
  "player": {
    "x": 1024.5,
    "y": 64.0,
    "z": 2048.5
  }
}
```

---

## Module Interactions

### Critical Integration Points

1. **ChunkManager → ChunkStreamer**
   - Manager defines desired set
   - Streamer generates/remeshes asynchronously
   - Results adopted back into manager slots

2. **Worldgen → Chunk**
   - `sdk_worldgen_generate_chunk_ctx()` fills blocks
   - Column profiles drive surface placement
   - Settlement data overlays modifications

3. **Settlement → Worldgen**
   - Settlements registered in world descriptor
   - Worldgen applies terrain modifications
   - Building placement uses terrain profile

4. **Superchunk Walls → Worldgen**
   - Wall profile computed during generation
   - Applied after terrain, before sealing
   - Gates carved through wall chunks

5. **Simulation → ChunkManager**
   - Simulation state stored per-chunk
   - Chunk load triggers sim state restoration
   - Block changes wake simulation cells

6. **Construction → Chunk**
   - Sparse construction cells in chunk storage
   - Overflow references for high-density areas
   - MeshBuilder renders construction geometry

7. **Persistence → ChunkManager**
   - Save iterates resident chunks
   - Load populates chunk slots
   - Settlement data loaded separately

---

## Key Constants Reference

| Constant | Value | Purpose |
|----------|-------|---------|
| `CHUNK_WIDTH/DEPTH` | 64 | Chunk X/Z size in blocks |
| `CHUNK_HEIGHT` | 1024 | Chunk Y size in blocks |
| `CHUNK_SUBCHUNK_COUNT` | 16 | Subchunk divisions |
| `SDK_SUPERCHUNK_CHUNK_SPAN` | 16 | Superchunk size in chunks |
| `SDK_SUPERCHUNK_WALL_PERIOD` | 17 | Wall spacing (16+1) |
| `SDK_CHUNK_MANAGER_MAX_RESIDENT` | 768 | Max loaded chunks |
| `CHUNK_GRID_DEFAULT_SIZE` | 17 | Default render distance |

---

---

## Related Documentation

### Up to Parent
- [SDK Core Overview](../SDK_CoreOverview.md) - Back to Core subsystems

### Chunk System
- [Chunks/SDK_Chunks.md](Chunks/SDK_Chunks.md) - Chunk data structures
- [Chunks/SDK_ChunkManager.md](Chunks/SDK_ChunkManager.md) - Residency management
- [Chunks/SDK_ChunkResidencyAndStreaming.md](Chunks/SDK_ChunkResidencyAndStreaming.md) - Streaming system
- [Chunks/SDK_ChunkCompression.md](Chunks/SDK_ChunkCompression.md) - Serialization
- [Chunks/SDK_ChunkAnalysis.md](Chunks/SDK_ChunkAnalysis.md) - Analysis tools
- [Chunks/SDK_ChunkStreamer.md](Chunks/SDK_ChunkStreamer.md) - Async streaming

### World Generation
- [SDK_WorldgenTerrainPipeline.md](SDK_WorldgenTerrainPipeline.md) - Terrain pipeline
- [Terrain/SDK_Worldgen.md](Terrain/SDK_Worldgen.md) - World generation details

### Systems
- [Simulation/SDK_Simulation.md](Simulation/SDK_Simulation.md) - Fluid simulation
- [Simulation/SDK_SimulationAndMaterials.md](Simulation/SDK_SimulationAndMaterials.md) - Materials
- [Settlements/SDK_SettlementSystem.md](Settlements/SDK_SettlementSystem.md) - NPC settlements
- [Persistence/SDK_PersistenceAndStorage.md](Persistence/SDK_PersistenceAndStorage.md) - Save/load
- [ConstructionCells/SDK_ConstructionSystem.md](ConstructionCells/SDK_ConstructionSystem.md) - Construction
- [Blocks/SDK_Blocks.md](Blocks/SDK_Blocks.md) - Block types
- [Buildings/SDK_Buildings.md](Buildings/SDK_Buildings.md) - Building types

### Related Core Systems
- [../MeshBuilder/SDK_MeshBuilder.md](../MeshBuilder/SDK_MeshBuilder.md) - Mesh generation
- [../Map/SDK_MapSchedulerAndTileCache.md](../Map/SDK_MapSchedulerAndTileCache.md) - Map system

---

**Source Organization:**
- 10 subdirectories
- ~50 source files
- ~1.2MB total code

**Key Files:**
- `sdk_chunk.h` - Core chunk structure
- `sdk_chunk_manager.h` - Residency management
- `sdk_worldgen.h` - Public generation API
- `sdk_superchunk_geometry.h` - Wall constants
- `sdk_simulation.h` - Fluid simulation

---
*Documentation for `SDK/Core/World/`*
