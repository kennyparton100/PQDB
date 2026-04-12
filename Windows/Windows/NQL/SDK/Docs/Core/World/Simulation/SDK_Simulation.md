# SDK Simulation Documentation

Comprehensive documentation for the SDK simulation system, handling fluid dynamics and granular material behavior.

**Module:** `SDK/Core/World/Simulation/`  
**Output:** `SDK/Docs/Core/World/Simulation/SDK_Simulation.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Architecture](#architecture)
- [Fluid Simulation](#fluid-simulation)
- [Granular Materials](#granular-materials)
- [Cell State Management](#cell-state-management)
- [Debug Information](#debug-information)
- [Key Functions](#key-functions)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Simulation module provides sparse runtime material simulation for loaded chunks. It uses a reservoir-based approach for fluids and supports granular materials like sand and gravel. Simulation state is stored per-chunk and processed in configurable increments to maintain frame rate.

**Key Features:**
- Reservoir-based fluid simulation (water, lava)
- Granular material settling (sand, gravel)
- Sparse cell storage (only active cells)
- Configurable tick budget
- Hydraulic equilibrium solving
- Save/load simulation state

---

## Architecture

### Data Flow

```
Block Change / Chunk Load
           │
           ├──► Wake adjacent cells
           │       └──► Add to dirty queue
           │
           └──► Mark chunk sim_dirty

Per-Frame Tick:
           │
           ├──► sdk_simulation_tick_chunk_manager(cm, max_cells)
           │       ├──► Process dirty queue (up to max_cells)
           │       ├──► Build reservoirs from connected cells
           │       ├──► Solve hydraulic equilibrium
           │       └──► Apply changes to chunks
           │
           └──► Mark modified chunks dirty for remesh
```

### Chunk Simulation State

Each chunk optionally holds simulation state:

```c
struct SdkChunkSimState {
    // Fluid cells (sparse hash table)
    SdkFluidCellState* fluid_cells;
    uint32_t fluid_capacity;
    uint32_t fluid_count;
    
    // Lookup acceleration
    SdkSimCellKey* fluid_lookup_keys;
    uint32_t* fluid_lookup_indices;
    
    // Granular cells
    SdkLooseCellState* loose_cells;
    
    // Dirty queue (cells needing update)
    SdkSimCellKey* dirty_queue;
    uint32_t dirty_head, dirty_count;
    uint32_t* dirty_marks;  // Bitmask for deduplication
};
```

---

## Fluid Simulation

### SdkFluidCellState Structure

```c
typedef struct {
    SdkSimCellKey key;      // Packed (lx, ly, lz) position
    uint8_t fill;           // Fill level 0-255
    uint8_t material_kind;  // Water, lava, etc.
    uint8_t flags;          // Flow flags
    uint8_t settled_ticks;  // Stability counter
} SdkFluidCellState;
```

### Material Kinds

| Kind | Description |
|------|-------------|
| 0 | Water (flows, evaporates) |
| 1 | Lava (flows, damages blocks) |

### Reservoir System

Fluids are grouped into connected reservoirs for efficient solving:

```
Reservoir Formation:
1. Start from seed cell
2. Flood-fill to find connected cells
3. Calculate total volume
4. Determine target surface height
5. Distribute fluid to reach equilibrium
```

### Flow Behavior

- Water flows to lowest adjacent cell
- Respects permeability (slow flow through soil)
- Evaporation over time
- Block displacement (water pushes non-solid blocks)

---

## Granular Materials

### SdkLooseCellState Structure

```c
typedef struct {
    SdkSimCellKey key;
    uint8_t flags;
    uint8_t repose_bias;  // Angle of repose modifier
} SdkLooseCellState;
```

### Granular Behavior

| Material | Behavior |
|----------|----------|
| Sand | Flows when angle > repose, settles at bottom |
| Gravel | Flows but less readily than sand |
| Colluvium | Organic loose material |

### Settling Process

```
For each loose cell:
1. Check support below
2. If unsupported or angle > repose:
   a. Find lowest valid position
   b. Move material down
   c. Wake neighbors
3. If settled, increment settled_ticks
4. After N ticks stable, remove from simulation
```

---

## Cell State Management

### Cell Key Encoding

```c
// Pack local coordinates into 32-bit key
typedef uint32_t SdkSimCellKey;

SdkSimCellKey sdk_simulation_pack_local_key(int lx, int ly, int lz) {
    return ((uint32_t)lx << 20) | ((uint32_t)ly << 10) | (uint32_t)lz;
    // lx: 10 bits (0-63)
    // ly: 10 bits (0-1023)
    // lz: 10 bits (0-63)
}

void sdk_simulation_unpack_local_key(SdkSimCellKey key, 
                                     int* out_lx, int* out_ly, int* out_lz) {
    *out_lx = (key >> 20) & 0x3F;      // 6 bits
    *out_ly = (key >> 10) & 0x3FF;     // 10 bits  
    *out_lz = key & 0x3F;              // 6 bits
}
```

### Sparse Storage

```c
// Get fluid fill at position (returns 0 if no cell)
uint8_t sdk_simulation_get_fluid_fill(const SdkChunk* chunk, 
                                      int lx, int ly, int lz);

// Enqueue cell for update
void sdk_simulation_enqueue_local(SdkChunk* chunk, int lx, int ly, int lz);
void sdk_simulation_enqueue_world(SdkChunkManager* cm, 
                                  int wx, int wy, int wz);
```

---

## Debug Information

### SdkFluidDebugInfo Structure

```c
typedef struct {
    int      mechanism;           // Current processing mode
    int      last_seed_wx, wy, wz;  // Last processed seed
    uint32_t reservoir_columns;     // Active reservoir count
    uint32_t total_columns;         // Total columns processed
    int      worker_count;          // Worker threads
    uint32_t tick_processed;        // Cells processed this tick
    uint32_t dirty_cells;           // Cells in dirty queue
    uint32_t active_chunks;         // Chunks with simulation
    uint32_t visited_columns;       // Unique columns this tick
    uint32_t spill_columns;           // Overflow columns
    float    total_volume;            // Total fluid volume
    float    solve_ms;                // Reservoir solve time
    float    apply_ms;                // Apply changes time
    char     reason[48];              // Current activity description
} SdkFluidDebugInfo;
```

### Mechanism Modes

```c
typedef enum {
    SDK_FLUID_DEBUG_MECH_IDLE = 0,        // No work
    SDK_FLUID_DEBUG_MECH_BULK_RESERVOIR,  // Processing large reservoir
    SDK_FLUID_DEBUG_MECH_LOCAL_WAKE       // Processing local changes
} SdkFluidDebugMechanism;
```

---

## Key Functions

### Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_simulation_chunk_state_create` | `() → SdkChunkSimState*` | Create sim state |
| `sdk_simulation_chunk_state_destroy` | `(state) → void` | Destroy sim state |
| `sdk_simulation_chunk_state_clear` | `(state) → void` | Clear all cells |
| `sdk_simulation_clone_chunk_state_for_snapshot` | `(src) → SdkChunkSimState*` | Clone state |

### Per-Frame Update

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_simulation_tick_chunk_manager` | `(cm, max_cells) → void` | Run simulation tick |
| `sdk_simulation_get_debug_info` | `(cm, out) → void` | Get debug stats |

### Event Handling

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_simulation_on_chunk_loaded` | `(cm, chunk) → void` | Init sim for loaded chunk |
| `sdk_simulation_on_block_changed` | `(cm, wx, wy, wz, old, new) → void` | Wake cells |
| `sdk_simulation_enqueue_local` | `(chunk, lx, ly, lz) → void` | Wake local cell |
| `sdk_simulation_enqueue_world` | `(cm, wx, wy, wz) → void` | Wake world cell |

### State Control

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_simulation_invalidate_reservoirs` | `() → void` | Force reservoir rebuild |
| `sdk_simulation_begin_shutdown` | `() → void` | Start graceful shutdown |
| `sdk_simulation_poll_shutdown` | `() → int` | Check shutdown status |
| `sdk_simulation_shutdown` | `() → void` | Complete shutdown |

### Persistence

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_simulation_encode_chunk_fluids` | `(chunk) → char*` | Serialize fluids |
| `sdk_simulation_decode_chunk_fluids` | `(chunk, encoded) → int` | Deserialize |

---

## API Surface

### Public Header (sdk_simulation.h)

```c
#ifndef NQLSDK_SIMULATION_H
#define NQLSDK_SIMULATION_H

#include "../Chunks/sdk_chunk.h"
#include "../Chunks/sdk_chunk_manager.h"

typedef uint32_t SdkSimCellKey;

typedef struct {
    SdkSimCellKey key;
    uint8_t fill;
    uint8_t material_kind;
    uint8_t flags;
    uint8_t settled_ticks;
} SdkFluidCellState;

typedef struct {
    SdkSimCellKey key;
    uint8_t flags;
    uint8_t repose_bias;
} SdkLooseCellState;

#define SDK_FLUID_DEBUG_REASON_MAX 48

typedef enum {
    SDK_FLUID_DEBUG_MECH_IDLE = 0,
    SDK_FLUID_DEBUG_MECH_BULK_RESERVOIR,
    SDK_FLUID_DEBUG_MECH_LOCAL_WAKE
} SdkFluidDebugMechanism;

typedef struct {
    int      mechanism;
    int      last_seed_wx, last_seed_wy, last_seed_wz;
    uint32_t reservoir_columns;
    uint32_t total_columns;
    int      worker_count;
    uint32_t tick_processed;
    uint32_t dirty_cells;
    uint32_t active_chunks;
    uint32_t visited_columns;
    uint32_t spill_columns;
    uint32_t stage_index;
    uint32_t stage_count;
    uint32_t truncated_flags;
    uint32_t dedupe_hits;
    float    total_volume;
    float    target_surface_e;
    float    solve_ms;
    float    apply_ms;
    char     reason[SDK_FLUID_DEBUG_REASON_MAX];
} SdkFluidDebugInfo;

struct SdkChunkSimState {
    SdkFluidCellState* fluid_cells;
    uint32_t fluid_capacity;
    uint32_t fluid_count;
    SdkSimCellKey* fluid_lookup_keys;
    uint32_t* fluid_lookup_indices;
    uint32_t fluid_lookup_capacity;
    SdkLooseCellState* loose_cells;
    uint32_t loose_capacity;
    uint32_t loose_count;
    SdkSimCellKey* dirty_queue;
    uint32_t dirty_head;
    uint32_t dirty_count;
    uint32_t dirty_capacity;
    uint32_t* dirty_marks;
    uint32_t dirty_mark_words;
};

/* Lifecycle */
SdkChunkSimState* sdk_simulation_chunk_state_create(void);
void sdk_simulation_chunk_state_destroy(SdkChunkSimState* state);
void sdk_simulation_chunk_state_clear(SdkChunkSimState* state);
SdkChunkSimState* sdk_simulation_clone_chunk_state_for_snapshot(const SdkChunkSimState* src);

/* Cell addressing */
SdkSimCellKey sdk_simulation_pack_local_key(int lx, int ly, int lz);
void sdk_simulation_unpack_local_key(SdkSimCellKey key, 
                                     int* out_lx, int* out_ly, int* out_lz);

/* Queries */
uint8_t sdk_simulation_get_fluid_fill(const SdkChunk* chunk, int lx, int ly, int lz);

/* Event handling */
void sdk_simulation_enqueue_local(SdkChunk* chunk, int lx, int ly, int lz);
void sdk_simulation_enqueue_world(SdkChunkManager* cm, int wx, int wy, int wz);
void sdk_simulation_on_block_changed(SdkChunkManager* cm, int wx, int wy, int wz,
                                     BlockType old_type, BlockType new_type);
void sdk_simulation_on_chunk_loaded(SdkChunkManager* cm, SdkChunk* chunk);

/* Per-frame update */
void sdk_simulation_tick_chunk_manager(SdkChunkManager* cm, int max_cells);
void sdk_simulation_get_debug_info(const SdkChunkManager* cm, SdkFluidDebugInfo* out_info);

/* Control */
void sdk_simulation_invalidate_reservoirs(void);
void sdk_simulation_begin_shutdown(void);
int  sdk_simulation_poll_shutdown(void);
void sdk_simulation_shutdown(void);

/* Persistence */
char* sdk_simulation_encode_chunk_fluids(const SdkChunk* chunk);
int sdk_simulation_decode_chunk_fluids(SdkChunk* chunk, const char* encoded);

#endif
```

---

## Integration Notes

### Chunk Loading

```c
void on_chunk_loaded(SdkChunkManager* cm, SdkChunk* chunk) {
    // Create simulation state for chunk
    sdk_simulation_on_chunk_loaded(cm, chunk);
    
    // If chunk has fluid blocks, wake them
    for (int ly = 0; ly < CHUNK_HEIGHT; ly++) {
        for (int lz = 0; lz < CHUNK_DEPTH; lz++) {
            for (int lx = 0; lx < CHUNK_WIDTH; lx++) {
                BlockType type = sdk_chunk_get_block(chunk, lx, ly, lz);
                if (type == BLOCK_WATER || type == BLOCK_LAVA) {
                    sdk_simulation_enqueue_local(chunk, lx, ly, lz);
                }
            }
        }
    }
}
```

### Block Changes

```c
void place_block_with_sim(SdkChunkManager* cm, int wx, int wy, int wz,
                          BlockType type) {
    BlockType old_type = get_block_at(cm, wx, wy, wz);
    
    // Place block
    set_block_at(cm, wx, wy, wz, type);
    
    // Notify simulation
    sdk_simulation_on_block_changed(cm, wx, wy, wz, old_type, type);
    
    // Wake neighbors if placing fluid
    if (type == BLOCK_WATER || type == BLOCK_LAVA) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dz = -1; dz <= 1; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    sdk_simulation_enqueue_world(cm, 
                        wx + dx, wy + dy, wz + dz);
                }
            }
        }
    }
}
```

### Per-Frame Tick

```c
void game_frame_update(SdkChunkManager* cm) {
    // Run simulation with budget
    sdk_simulation_tick_chunk_manager(cm, 1000);  // Max 1000 cells
    
    // Check for modified chunks and mark dirty
    for (int i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; i++) {
        SdkChunkResidentSlot* slot = &cm->slots[i];
        if (!slot->occupied) continue;
        
        if (slot->chunk.sim_dirty_mask) {
            // Mark affected subchunks dirty
            for (int s = 0; s < CHUNK_SUBCHUNK_COUNT; s++) {
                if (slot->chunk.sim_dirty_mask & (1u << s)) {
                    sdk_chunk_mark_subchunk_dirty(&slot->chunk, s);
                }
            }
            slot->chunk.sim_dirty_mask = 0;
        }
    }
}
```

---

## AI Context Hints

### Custom Fluid Type

```c
// Add custom fluid (e.g., oil, acid)
#define FLUID_KIND_OIL 2
#define FLUID_KIND_ACID 3

void add_custom_fluid(SdkChunk* chunk, int lx, int ly, int lz, 
                      uint8_t fluid_kind, uint8_t fill) {
    SdkChunkSimState* state = chunk->sim_state;
    if (!state) {
        chunk->sim_state = sdk_simulation_chunk_state_create();
        state = chunk->sim_state;
    }
    
    // Add or update fluid cell
    SdkSimCellKey key = sdk_simulation_pack_local_key(lx, ly, lz);
    SdkFluidCellState* cell = find_or_create_fluid_cell(state, key);
    cell->material_kind = fluid_kind;
    cell->fill = fill;
    
    // Wake for processing
    sdk_simulation_enqueue_local(chunk, lx, ly, lz);
}

// Custom flow behavior
void process_custom_fluid(SdkFluidCellState* cell) {
    switch (cell->material_kind) {
        case FLUID_KIND_OIL:
            // Oil floats on water, burns
            if (is_above_water(cell)) {
                flow_up(cell);
            } else {
                flow_down(cell);
            }
            break;
            
        case FLUID_KIND_ACID:
            // Acid dissolves certain blocks
            BlockType below = get_block_below(cell);
            if (is_dissolvable(below)) {
                dissolve_block(below);
            }
            flow_down(cell);
            break;
    }
}
```

### Performance Monitoring

```c
void monitor_simulation_performance(SdkChunkManager* cm) {
    SdkFluidDebugInfo info;
    sdk_simulation_get_debug_info(cm, &info);
    
    static float avg_solve_ms = 0;
    avg_solve_ms = 0.9f * avg_solve_ms + 0.1f * info.solve_ms;
    
    if (avg_solve_ms > 16.0f) {
        printf("WARNING: Simulation taking %.1f ms (budget: 16 ms)\n", avg_solve_ms);
        printf("Consider reducing max_cells or optimizing\n");
    }
    
    printf("Active reservoirs: %u\n", info.reservoir_columns);
    printf("Active chunks: %u\n", info.active_chunks);
    printf("Total volume: %.0f units\n", info.total_volume);
}
```

### Snapshot System

```c
void save_simulation_snapshot(SdkChunkManager* cm, const char* filename) {
    FILE* f = fopen(filename, "wb");
    
    // Header
    uint32_t chunk_count = 0;
    for (int i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; i++) {
        if (cm->slots[i].occupied && cm->slots[i].chunk.sim_state) {
            chunk_count++;
        }
    }
    fwrite(&chunk_count, sizeof(chunk_count), 1, f);
    
    // Each chunk's simulation state
    for (int i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; i++) {
        if (!cm->slots[i].occupied) continue;
        
        SdkChunk* chunk = &cm->slots[i].chunk;
        if (!chunk->sim_state) continue;
        
        // Write chunk coordinates
        fwrite(&chunk->cx, sizeof(chunk->cx), 1, f);
        fwrite(&chunk->cz, sizeof(chunk->cz), 1, f);
        
        // Encode and write fluid data
        char* encoded = sdk_simulation_encode_chunk_fluids(chunk);
        uint32_t len = (uint32_t)strlen(encoded);
        fwrite(&len, sizeof(len), 1, f);
        fwrite(encoded, 1, len, f);
        free(encoded);
    }
    
    fclose(f);
}
```

---

## Related Documentation

- `SDK_Chunks.md` - Chunk sim_state storage
- `SDK_ChunkManager.md` - Integration with chunk lifecycle
- `SDK_Blocks.md` - Fluid and granular block types
- `SDK_Persistence.md` - Simulation state persistence

---

**Source Files:**
- `SDK/Core/World/Simulation/sdk_simulation.h` (3,302 bytes) - Public API
- `SDK/Core/World/Simulation/sdk_simulation.c` (93,995 bytes) - Implementation
