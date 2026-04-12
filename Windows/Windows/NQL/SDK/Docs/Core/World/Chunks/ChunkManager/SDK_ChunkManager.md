<!-- Navigation: breadcrumb trail -->
**Navigation:** [SDK Overview](../../../../SDK_Overview.md) > [Core](../../../SDK_CoreOverview.md) > [World](../../SDK_WorldOverview.md) > [Chunks](../SDK_Chunks.md) > ChunkManager

---

# SDK ChunkManager Documentation

Comprehensive documentation for the SDK chunk manager, responsible for maintaining the authoritative desired and resident chunk sets.

**Module:** `SDK/Core/World/Chunks/`  
**Output:** `SDK/Docs/Core/World/Chunks/ChunkManager/SDK_ChunkManager.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Architecture](#architecture)
- [Residency Roles](#residency-roles)
- [Superchunk Mode](#superchunk-mode)
- [Window Mode](#window-mode)
- [Chunk Lookup](#chunk-lookup)
- [Key Functions](#key-functions)
- [Global State](#global-state)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The ChunkManager is the authoritative source for which chunks should exist (desired set) and which chunks currently have allocated memory (resident set). It handles:

- Camera-driven chunk residency updates
- Superchunk-aware residency layout
- Slot allocation and hash-based lookup
- Wall support chunk management
- Background expansion and transitions

**Key Features:**
- 768 max resident chunks
- 768 max desired chunks  
- Hash-based O(1) coordinate lookup
- Two residency modes: Window and Superchunk
- Frontier/wall support chunk roles
- Topology generation tracking

---

## Architecture

### Core Data Structures

```c
typedef struct {
    SdkChunk chunk;          // The actual chunk data
    uint8_t  occupied;       // Slot has a chunk
    uint8_t  desired;        // Chunk is in desired set
    uint8_t  role;           // SdkChunkResidencyRole
    uint8_t  desired_role;   // Target role
    uint8_t  render_representation;  // Full/Far/Proxy
    uint8_t  render_far_mesh_kind;
} SdkChunkResidentSlot;

typedef struct {
    SdkChunkResidentSlot slots[SDK_CHUNK_MANAGER_MAX_RESIDENT];
    int32_t lookup[SDK_CHUNK_MANAGER_HASH_CAPACITY];  // 4096 entry hash table
    
    // Camera tracking
    int cam_cx, cam_cz;
    int grid_size;  // Legacy graphics setting
    
    // Topology
    uint32_t topology_generation;
    int primary_scx, primary_scz;  // Current superchunk
    int prev_scx, prev_scz;        // Previous superchunk
    
    // Desired set
    SdkChunkResidencyTarget desired[SDK_CHUNK_MANAGER_MAX_DESIRED];
    int desired_count;
} SdkChunkManager;
```

### Two Residency Modes

**Window Mode** (radius < 16 chunks):
- Camera-centered square window
- Recenters when camera moves 2+ chunks
- No full-superchunk residency
- Used for lower render distances

**Superchunk Mode** (radius ≥ 16 chunks):
- Full 16×16 superchunk + frontier
- Immediate rebuild on superchunk crossing
- 340 chunks steady-state (256 primary + 84 frontier)
- Used for higher render distances

### Residency Flow

```
Camera Moves:
           │
           ├──► sdk_chunk_manager_update(cm, new_cx, new_cz)
           │       ├──► Compute new desired set
           │       │       ├──► Window mode: camera-centered square
           │       │       └──► Superchunk mode: full superchunk + frontier
           │       ├──► Mark slots as EVICT_PENDING if no longer desired
           │       ├──► Reserve slots for new desired chunks
           │       └──► Return topology_dirty flag
           │
           ├──► If topology_dirty:
           │       ├──► Streamer: schedule generate/remesh jobs
           │       ├──► Bootstrap: sync load critical chunks
           │       └──► Simulation: invalidate reservoirs
           │
           └──► Frame: render resident chunks
```

---

## Residency Roles

**SdkChunkResidencyRole enum:**

| Role | Value | Purpose |
|------|-------|---------|
| `SDK_CHUNK_ROLE_NONE` | 0 | Unassigned/empty slot |
| `SDK_CHUNK_ROLE_PRIMARY` | 1 | Core superchunk (16×16) |
| `SDK_CHUNK_ROLE_WALL_SUPPORT` | 2 | Supports wall geometry |
| `SDK_CHUNK_ROLE_FRONTIER` | 3 | Outer ring support |
| `SDK_CHUNK_ROLE_TRANSITION_PRELOAD` | 4 | Preload for superchunk transition |
| `SDK_CHUNK_ROLE_EVICT_PENDING` | 5 | Marked for removal |

### Superchunk Layout

```
┌─────────────────────────────────────────────────────┐
│  Frontier Ring (68 chunks)                          │
│  ┌─────────────────────────────────────────────┐   │
│  │ Wall Chunks (68)                            │   │
│  │  ┌─────────────────────────────────────┐     │   │
│  │  │ Primary Superchunk (256 chunks)     │     │   │
│  │  │  16×16 core chunks                  │     │   │
│  │  │                                     │     │   │
│  │  └─────────────────────────────────────┘     │   │
│  └─────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘

Plus: 16 gate frontier chunks beyond the ring
```

### Wall Chunk Detection

```c
// Wall chunks are at positions relative to superchunk origin
static inline int sdk_superchunk_active_wall_chunk_contains_chunk(
    int scx, int scz, int cx, int cz) {
    const int origin_cx = scx * SDK_SUPERCHUNK_WALL_PERIOD;
    const int origin_cz = scz * SDK_SUPERCHUNK_WALL_PERIOD;
    const int west_x = origin_cx - 1;
    const int north_z = origin_cz - 1;
    const int east_x = origin_cx + SDK_SUPERCHUNK_CHUNK_SPAN;
    const int south_z = origin_cz + SDK_SUPERCHUNK_CHUNK_SPAN;
    
    // West wall: cx = origin_cx - 1, cz in [origin_cz, origin_cz+16)
    // North wall: cz = origin_cz - 1, cx in [origin_cx, origin_cx+16)
    // East wall: cx = origin_cx + 16
    // South wall: cz = origin_cz + 16
}
```

---

## Superchunk Mode

### Activation

Superchunk mode activates when the normalized grid size radius ≥ 16 chunks:

```c
int radius = sdk_chunk_manager_radius_from_grid_size(grid_size);
if (radius >= SDK_SUPERCHUNK_CHUNK_SPAN) {
    // Use superchunk residency layout
}
```

### Layout Building

```c
// Primary superchunk (256 chunks)
void emit_full_superchunk(SdkChunkManager* cm, int scx, int scz);

// Outer ring frontier (68 chunks)
void emit_superchunk_outer_ring(SdkChunkManager* cm, int scx, int scz);

// Gate frontier support (16 chunks)
void emit_superchunk_extra_gate_frontier(SdkChunkManager* cm, int scx, int scz);
```

### Transition Handling

When crossing superchunk boundaries:

1. New superchunk becomes primary
2. Previous superchunk's walls may become frontier
3. Wall support chunks from previous may be reclassified
4. Generation counter increments for stale detection

---

## Window Mode

### Activation

Window mode is used when render distance is below superchunk span:

```c
#define CHUNK_GRID_MIN_SIZE 1
#define CHUNK_GRID_DEFAULT_SIZE 17
#define CHUNK_GRID_MAX_SIZE 33

// Normalized to radii: 1, 3, 5, 7, 9, 11, 13, 15, 16, 20, 24, 28, 32
// Window mode: sizes 1-15
// Superchunk mode: sizes 16+
```

### Recenter Logic

```c
// Recenters when camera moves more than 2 chunks from anchor
if (abs(cam_cx - anchor_cx) > 2 || abs(cam_cz - anchor_cz) > 2) {
    // Rebuild desired set around new anchor
}
```

---

## Chunk Lookup

### Hash Table

The manager uses a 4096-entry hash table for O(1) lookups:

```c
#define SDK_CHUNK_MANAGER_HASH_CAPACITY 4096

int32_t lookup[SDK_CHUNK_MANAGER_HASH_CAPACITY];  // Maps hash → slot index

// Hash function: mix coordinates
coord = ((uint32_t)cx << 16) ^ (uint32_t)cz;  // simple combination
```

### Slot Operations

```c
// Find existing slot
SdkChunkResidentSlot* sdk_chunk_manager_find_slot(SdkChunkManager* cm, 
                                                   int cx, int cz);

// Reserve new slot
SdkChunkResidentSlot* sdk_chunk_manager_reserve_slot(SdkChunkManager* cm,
                                                       int cx, int cz,
                                                       SdkChunkResidencyRole role);

// Release slot
void sdk_chunk_manager_release_slot(SdkChunkManager* cm, 
                                     SdkChunkResidentSlot* slot);
```

---

## Key Functions

### Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_chunk_manager_init` | `(SdkChunkManager*) → void` | Initialize empty manager |
| `sdk_chunk_manager_shutdown` | `(SdkChunkManager*) → void` | Free all chunks |
| `sdk_chunk_manager_set_grid_size` | `(cm, size) → void` | Update render distance |

### Update

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_chunk_manager_update` | `(cm, cam_cx, cam_cz) → bool` | Update desired set, return if changed |
| `sdk_chunk_manager_normalize_grid_size` | `(size) → int` | Clamp to valid range |
| `sdk_chunk_manager_radius_from_grid_size` | `(size) → int` | Convert to radius |

### Queries

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_chunk_manager_get_chunk` | `(cm, cx, cz) → SdkChunk*` | Get chunk at coords (or NULL) |
| `sdk_chunk_manager_find_slot` | `(cm, cx, cz) → SdkChunkResidentSlot*` | Find resident slot |
| `sdk_chunk_manager_is_desired` | `(cm, cx, cz, role, gen) → bool` | Check if desired |
| `sdk_chunk_manager_get_chunk_role` | `(cm, cx, cz) → SdkChunkResidencyRole` | Get current role |

### Adoption

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_chunk_manager_adopt_built_chunk` | `(cm, chunk, role) → SdkChunk*` | Adopt streamer result |

### Iteration

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_chunk_manager_foreach` | `(cm, callback, user) → void` | Iterate all slots |
| `sdk_chunk_manager_memory_usage` | `(cm) → uint64_t` | Compute total memory |

---

## Global State

### Manager Constants

```c
#define CHUNK_GRID_MIN_SIZE 1
#define CHUNK_GRID_DEFAULT_SIZE 17
#define CHUNK_GRID_MAX_SIZE 33
#define CHUNK_GRID_MAX_COUNT (CHUNK_GRID_MAX_SIZE * CHUNK_GRID_MAX_SIZE)  // 1089

#define SDK_CHUNK_MANAGER_MAX_RESIDENT 768
#define SDK_CHUNK_MANAGER_MAX_DESIRED 768
#define SDK_CHUNK_MANAGER_HASH_CAPACITY 4096
```

### Grid Size Normalization

```c
// Grid sizes map to these radii
static const int k_grid_size_to_radius[] = {
    1, 3, 5, 7, 9, 11, 13, 15,  // Window mode (8 sizes)
    16, 20, 24, 28, 32         // Superchunk mode (5 sizes)
};
// Index: 0-12 (CHUNK_GRID_MIN_SIZE-1 to CHUNK_GRID_MAX_SIZE-1)
```

---

## API Surface

### Public Header (sdk_chunk_manager.h)

```c
/* =================================================================
 * CONSTANTS
 * ================================================================= */
#define CHUNK_GRID_MIN_SIZE 1
#define CHUNK_GRID_DEFAULT_SIZE 17
#define CHUNK_GRID_MAX_SIZE 33
#define SDK_CHUNK_MANAGER_MAX_RESIDENT 768
#define SDK_CHUNK_MANAGER_MAX_DESIRED 768
#define SDK_CHUNK_MANAGER_HASH_CAPACITY 4096

#define SDK_GATE_FRONTIER_DEPTH_CHUNKS SDK_SUPERCHUNK_WALL_THICKNESS_CHUNKS
#define SDK_GATE_FRONTIER_WIDTH_CHUNKS SDK_SUPERCHUNK_GATE_SUPPORT_WIDTH_CHUNKS

/* =================================================================
 * ENUMS
 * ================================================================= */
typedef enum {
    SDK_CHUNK_ROLE_NONE = 0,
    SDK_CHUNK_ROLE_PRIMARY,
    SDK_CHUNK_ROLE_WALL_SUPPORT,
    SDK_CHUNK_ROLE_FRONTIER,
    SDK_CHUNK_ROLE_TRANSITION_PRELOAD,
    SDK_CHUNK_ROLE_EVICT_PENDING
} SdkChunkResidencyRole;

typedef enum {
    SDK_CHUNK_RENDER_REPRESENTATION_FULL = 0,
    SDK_CHUNK_RENDER_REPRESENTATION_FAR,
    SDK_CHUNK_RENDER_REPRESENTATION_PROXY
} SdkChunkRenderRepresentation;

typedef enum {
    SDK_SUPERCHUNK_RING_NONE = -1,
    SDK_SUPERCHUNK_RING_WEST = 0,
    SDK_SUPERCHUNK_RING_NORTH = 1,
    SDK_SUPERCHUNK_RING_EAST = 2,
    SDK_SUPERCHUNK_RING_SOUTH = 3
} SdkSuperChunkRingSide;

typedef enum {
    SDK_ACTIVE_WALL_STAGE_NONE = 0,
    SDK_ACTIVE_WALL_STAGE_SUPPORT,
    SDK_ACTIVE_WALL_STAGE_EDGE,
    SDK_ACTIVE_WALL_STAGE_CORNER
} SdkActiveWallStage;

/* =================================================================
 * STRUCTS
 * ================================================================= */
typedef struct {
    int scx, scz;
} SdkSuperChunkCoord;

typedef struct {
    int      cx, cz;
    uint8_t  role;
    uint32_t generation;
} SdkChunkResidencyTarget;

typedef struct {
    SdkChunkResidentSlot   slots[SDK_CHUNK_MANAGER_MAX_RESIDENT];
    int32_t                lookup[SDK_CHUNK_MANAGER_HASH_CAPACITY];
    SdkConstructionArchetypeRegistry* construction_registry;
    
    int                    cam_cx, cam_cz;
    int                    grid_size;
    uint32_t               next_slot;
    uint32_t               topology_generation;
    uint16_t               resident_count;
    uint8_t                primary_valid;
    uint8_t                transition_active;
    uint8_t                primary_expanded;
    uint8_t                background_expansion_enabled;
    uint8_t                topology_dirty;
    
    int                    primary_scx, primary_scz;
    int                    prev_scx, prev_scz;
    int                    primary_load_radius;
    int                    primary_anchor_cx, primary_anchor_cz;
    int                    desired_scx, desired_scz;
    int                    transition_entry_cx, transition_entry_cz;
    
    SdkChunkResidencyTarget desired[SDK_CHUNK_MANAGER_MAX_DESIRED];
    int                    desired_count;
} SdkChunkManager;

/* =================================================================
 * FUNCTIONS
 * ================================================================= */
void sdk_chunk_manager_init(SdkChunkManager* cm);
int  sdk_chunk_manager_normalize_grid_size(int grid_size);
int  sdk_chunk_manager_grid_size_from_radius(int radius);
int  sdk_chunk_manager_radius_from_grid_size(int grid_size);
void sdk_chunk_manager_set_grid_size(SdkChunkManager* cm, int grid_size);
void sdk_chunk_manager_set_background_expansion(SdkChunkManager* cm, bool enabled);
void sdk_chunk_manager_shutdown(SdkChunkManager* cm);

bool sdk_chunk_manager_update(SdkChunkManager* cm, int new_cx, int new_cz);
SdkChunk* sdk_chunk_manager_get_chunk(SdkChunkManager* cm, int cx, int cz);

SdkChunkResidentSlot* sdk_chunk_manager_find_slot(SdkChunkManager* cm, int cx, int cz);
SdkChunkResidentSlot* sdk_chunk_manager_reserve_slot(SdkChunkManager* cm, int cx, int cz, 
                                                       SdkChunkResidencyRole role);
SdkChunk* sdk_chunk_manager_adopt_built_chunk(SdkChunkManager* cm, SdkChunk* built_chunk, 
                                               SdkChunkResidencyRole role);
void sdk_chunk_manager_release_slot(SdkChunkManager* cm, SdkChunkResidentSlot* slot);
void sdk_chunk_manager_rebuild_lookup(SdkChunkManager* cm);

bool sdk_chunk_manager_is_desired(const SdkChunkManager* cm, int cx, int cz,
                                  SdkChunkResidencyRole* out_role, uint32_t* out_generation);
SdkChunkResidencyRole sdk_chunk_manager_get_chunk_role(const SdkChunkManager* cm, int cx, int cz);

typedef void (*SdkChunkCallback)(SdkChunk* chunk, int slot_index, int role, void* user);
void sdk_chunk_manager_foreach(SdkChunkManager* cm, SdkChunkCallback cb, void* user);
uint64_t sdk_chunk_manager_memory_usage(const SdkChunkManager* cm);

/* =================================================================
 * INLINE HELPERS
 * ================================================================= */
static inline int sdk_chunk_manager_grid_size(const SdkChunkManager* cm);
static inline int sdk_chunk_manager_active_count(const SdkChunkManager* cm);
static inline int sdk_chunk_manager_slot_capacity(void);
static inline SdkChunkResidentSlot* sdk_chunk_manager_get_slot_at(SdkChunkManager* cm, int index);
static inline int sdk_chunk_manager_desired_count(const SdkChunkManager* cm);

/* Superchunk helpers */
static inline SdkSuperChunkRingSide sdk_superchunk_outer_ring_side_for_chunk(
    int scx, int scz, int cx, int cz);
static inline int sdk_superchunk_outer_ring_contains_chunk(int scx, int scz, int cx, int cz);
static inline int sdk_superchunk_full_neighborhood_contains_chunk(int scx, int scz, int cx, int cz);
static inline int sdk_superchunk_active_wall_corner_contains_chunk(int scx, int scz, int cx, int cz);
static inline int sdk_superchunk_active_wall_edge_contains_chunk(int scx, int scz, int cx, int cz);
static inline int sdk_superchunk_active_wall_chunk_contains_chunk(int scx, int scz, int cx, int cz);
static inline int sdk_superchunk_active_wall_support_contains_chunk(int scx, int scz, int cx, int cz);
static inline SdkActiveWallStage sdk_superchunk_active_wall_stage_for_chunk(int scx, int scz, int cx, int cz);

/* Chunk state helpers */
static inline int sdk_chunk_has_full_upload_ready_mesh(const SdkChunk* chunk);
static inline int sdk_chunk_is_active_wall_chunk_fully_ready(const SdkChunkManager* cm, 
                                                              const SdkChunk* chunk);
```

---

## Integration Notes

### Initialization Flow

```c
SdkChunkManager cm;
sdk_chunk_manager_init(&cm);
sdk_chunk_manager_set_grid_size(&cm, 17);  // Default

// Set construction registry for new chunks
cm.construction_registry = g_world_construction_registry;
```

### Per-Frame Update

```c
// Get camera position
int cam_cx = sdk_world_to_chunk_x((int)floorf(camera_x));
int cam_cz = sdk_world_to_chunk_z((int)floorf(camera_z));

// Update manager
bool topology_dirty = sdk_chunk_manager_update(&cm, cam_cx, cam_cz);

if (topology_dirty) {
    // Trigger async operations
    sdk_chunk_streamer_schedule_visible(&streamer, &cm);
    sdk_simulation_invalidate_reservoirs();
    
    // Sync bootstrap critical chunks
    bootstrap_nearby_visible_chunks_sync();
}
```

### Rendering Integration

```c
// Iterate visible chunks
void render_world(SdkChunkManager* cm, SdkCamera* camera) {
    for (int i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; i++) {
        SdkChunkResidentSlot* slot = &cm->slots[i];
        if (!slot->occupied) continue;
        
        SdkChunk* chunk = &slot->chunk;
        if (!sdk_chunk_is_visible(chunk, camera)) continue;
        
        // Check GPU upload status
        if (!sdk_chunk_has_full_upload_ready_mesh(chunk)) {
            continue;  // Skip not-ready chunks
        }
        
        // Render based on role
        switch (slot->role) {
            case SDK_CHUNK_ROLE_PRIMARY:
                render_full_mesh(chunk);
                break;
            case SDK_CHUNK_ROLE_FRONTIER:
                if (distance > FAR_DISTANCE) {
                    render_far_proxy(chunk);
                } else {
                    render_full_mesh(chunk);
                }
                break;
            // ... etc
        }
    }
}
```

### Changing Grid Size

```c
void change_render_distance(int new_size) {
    // Update settings
    g_graphics_settings.chunk_grid_size = new_size;
    save_graphics_settings_now();
    
    // Apply to manager
    sdk_chunk_manager_set_grid_size(&cm, new_size);
    
    // Immediate update
    sdk_chunk_manager_update(&cm, cam_cx, cam_cz);
    
    // Rebuild chunk grid
    rebuild_chunk_grid_for_current_camera(new_size);
}
```

---

## AI Context Hints

### Custom Residency Logic

```c
// Add custom chunks to desired set
void add_custom_desired_chunks(SdkChunkManager* cm, int center_cx, int center_cz) {
    // Ensure space in desired array
    if (cm->desired_count >= SDK_CHUNK_MANAGER_MAX_DESIRED) return;
    
    // Add specific chunk
    SdkChunkResidencyTarget* target = &cm->desired[cm->desired_count++];
    target->cx = center_cx + 5;
    target->cz = center_cz + 5;
    target->role = SDK_CHUNK_ROLE_PRIMARY;
    target->generation = cm->topology_generation;
}
```

### Monitoring Residency

```c
void print_residency_stats(SdkChunkManager* cm) {
    int primary = 0, frontier = 0, wall = 0, pending = 0;
    
    for (int i = 0; i < SDK_CHUNK_MANAGER_MAX_RESIDENT; i++) {
        if (!cm->slots[i].occupied) continue;
        
        switch (cm->slots[i].role) {
            case SDK_CHUNK_ROLE_PRIMARY: primary++; break;
            case SDK_CHUNK_ROLE_FRONTIER: frontier++; break;
            case SDK_CHUNK_ROLE_WALL_SUPPORT: wall++; break;
            case SDK_CHUNK_ROLE_EVICT_PENDING: pending++; break;
        }
    }
    
    printf("Resident: %d primary, %d frontier, %d wall, %d pending eviction\n",
           primary, frontier, wall, pending);
}
```

### Force Chunk Load

```c
// Ensure a specific chunk is loaded (for gameplay-critical operations)
SdkChunk* force_load_chunk(SdkChunkManager* cm, int cx, int cz) {
    // Check if already resident
    SdkChunk* chunk = sdk_chunk_manager_get_chunk(cm, cx, cz);
    if (chunk) return chunk;
    
    // Reserve slot
    SdkChunkResidentSlot* slot = sdk_chunk_manager_reserve_slot(
        cm, cx, cz, SDK_CHUNK_ROLE_PRIMARY);
    if (!slot) return NULL;
    
    // Initialize chunk
    sdk_chunk_init(&slot->chunk, cx, cz, cm->construction_registry);
    slot->occupied = 1;
    
    // Generate immediately (blocking)
    sdk_worldgen_generate_chunk_ctx(worldgen, &slot->chunk);
    
    return &slot->chunk;
}
```

### Custom Wall Logic

```c
// Check if chunk needs wall geometry
bool needs_wall_geometry(SdkChunkManager* cm, int cx, int cz) {
    // Check if it's a wall chunk for current superchunk
    if (sdk_superchunk_active_wall_chunk_contains_chunk(
            cm->primary_scx, cm->primary_scz, cx, cz)) {
        return true;
    }
    
    // Check if it's a wall chunk for any desired superchunk
    // ... custom logic ...
    
    return false;
}
```

---

## Related Documentation

### Up to Parent
- [SDK World Overview](../../SDK_WorldOverview.md) - World systems hub
- [SDK Chunks](../SDK_Chunks.md) - Chunk data structures

### Siblings - Chunk System
- [SDK_ChunkResidencyAndStreaming.md](../SDK_ChunkResidencyAndStreaming.md) - Streaming coordination
- [ChunkStreamer/SDK_ChunkStreamer.md](../ChunkStreamer/SDK_ChunkStreamer.md) - Async streaming worker
- [ChunkCompression/SDK_ChunkCompression.md](../ChunkCompression/SDK_ChunkCompression.md) - Serialization
- [ChunkAnalysis/SDK_ChunkAnalysis.md](../ChunkAnalysis/SDK_ChunkAnalysis.md) - Analysis tools

### Related World Systems
- [../../SuperChunks/SDK_SuperChunks.md](../../SuperChunks/SDK_SuperChunks.md) - Wall geometry (if exists)
- [../../SDK_WorldgenTerrainPipeline.md](../../SDK_WorldgenTerrainPipeline.md) - Chunk generation
- [../../Simulation/SDK_Simulation.md](../../Simulation/SDK_Simulation.md) - Fluid simulation
- [../../Persistence/SDK_PersistenceAndStorage.md](../../Persistence/SDK_PersistenceAndStorage.md) - Save/load

### Core Integration
- [../../../API/SDK_APIReference.md](../../../API/SDK_APIReference.md) - Public API
- [../../../Frontend/SDK_Frontend.md](../../../Frontend/SDK_Frontend.md) - Frontend flow
- [../../../Map/SDK_MapSchedulerAndTileCache.md](../../../Map/SDK_MapSchedulerAndTileCache.md) - Map system

---

**Source Files:**
- `SDK/Core/World/Chunks/sdk_chunk_manager.h` (13,960 bytes) - Public API
- `SDK/Core/World/Chunks/sdk_chunk_manager.c` (34,759 bytes) - Implementation

---
*Documentation for `SDK/Core/World/Chunks/`*
