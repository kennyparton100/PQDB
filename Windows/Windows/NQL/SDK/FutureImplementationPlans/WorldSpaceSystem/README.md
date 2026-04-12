# World Space System Architecture

## Overview

The World Space System provides coordinate space abstractions for world generation, enabling support for both attached and detached wall modes through explicit space type tagging rather than implicit period-based detection.

## Current Architecture (Problem)

### Single-Space Worldgen

```c
void sdk_worldgen_fill_chunk(SdkWorldGen* wg, SdkChunk* chunk) {
    sdk_chunk_clear(chunk);
    
    // All chunks treated the same - detected by period
    if (chunk_is_full_superchunk_wall_chunk(chunk)) {
        apply_superchunk_walls(wg, chunk);
    } else {
        generate_world_columns(wg, chunk);
    }
}
```

**Issues:**
- Wall detection happens at generation time via period calculations
- No explicit separation between terrain space and wall grid space
- `walls_detached` setting only affects period/offset calculations, not space ownership
- Chunk manager emits chunks without knowing which space they belong to

## Target Architecture

### Space-Aware Worldgen

```c
void sdk_worldgen_fill_chunk(SdkWorldGen* wg, SdkChunk* chunk) {
    switch(chunk->space_type) {
        case SDK_SPACE_GLOBAL_CHUNK:
            // Legacy flat world - walls embedded if enabled
            if (walls_enabled) {
                if (global_chunk_is_wall_chunk(x, z, ring_size, x_offset, z_offset)) {
                    generate_wall_chunk(wg, chunk);
                } else {
                    generate_terrain_chunk(wg, chunk);
                }
            } else {
                generate_terrain_chunk(wg, chunk);
            }
            break;
            
        case SDK_SPACE_SUPERCHUNK_TERRAIN:
            // Attached mode: walls embedded in terrain superchunks
            if (walls_enabled) {
                if (global_chunk_is_wall_chunk(x, z, ring_size, x_offset, z_offset)) {
                    generate_wall_chunk(wg, chunk);
                } else {
                    generate_terrain_chunk(wg, chunk);
                }
            } else {
                generate_terrain_chunk(wg, chunk);
            }
            break;
            
        case SDK_SPACE_TERRAIN_SUPERCHUNK:
            // Detached mode: pure terrain only (walls in separate space)
            generate_terrain_chunk(wg, chunk);
            break;
            
        case SDK_SPACE_WALL_GRID:
            // Detached mode: dedicated wall space
            generate_wall_chunk(wg, chunk);
            break;
    }
}
```

## Coordinate Spaces

### SDK_SPACE_GLOBAL_CHUNK
Legacy flat world generation. No superchunks.
- Can have embedded walls if walls_enabled=true
- Wall detection via period/offset calculation

### SDK_SPACE_SUPERCHUNK_TERRAIN
Attached mode: superchunk terrain with embedded walls.
- Period: `chunk_span + 1` (default 17)
- Walls exist at grid boundaries within this space
- Both terrain and walls generated from this space type

### SDK_SPACE_TERRAIN_SUPERCHUNK
Detached mode: pure terrain superchunks.
- Period: `chunk_span` (default 16)
- No walls in this space - walls are in SDK_SPACE_WALL_GRID
- Only terrain generation

### SDK_SPACE_WALL_GRID
Detached mode: dedicated wall grid space.
- Period: `wall_grid_size - 1` (default 17, or configured)
- Offsets: `wall_grid_offset_x`, `wall_grid_offset_z`
- Only wall chunks exist in this space
- Only wall generation

## Required Changes

### 1. SdkChunkResidencyTarget (Chunk Manager)

**File:** `Core/World/Chunks/ChunkManager/sdk_chunk_manager.h`

```c
typedef struct {
    int32_t cx, cz;
    uint8_t role;           // SDK_CHUNK_ROLE_*
    uint8_t space_type;     // SDK_SPACE_* (NEW FIELD)
    uint16_t generation;
} SdkChunkResidencyTarget;
```

### 2. SdkChunk (Runtime Chunk)

**File:** `Core/World/Chunks/sdk_chunk.h`

```c
typedef struct SdkChunk {
    int32_t cx, cz;
    uint8_t space_type;     // SDK_SPACE_* (NEW FIELD)
    uint8_t role;
    // ... existing fields
} SdkChunk;
```

### 3. Chunk Manager Emit Functions

**File:** `Core/World/Chunks/ChunkManager/sdk_chunk_manager.c`

Emit functions must tag chunks with appropriate space type:

```c
// When walls_detached=true
emit_superchunk_terrain_chunks(cm, scx, scz) {
    // Emit with SDK_SPACE_SUPERCHUNK_TERRAIN
}

emit_wall_grid_chunks(cm) {
    // Emit with SDK_SPACE_WALL_GRID
}

// When walls_detached=false
emit_superchunk_with_embedded_walls(cm, scx, scz) {
    // All chunks tagged SDK_SPACE_SUPERCHUNK_TERRAIN
    // Wall detection happens in worldgen, not emission
}
```

### 4. Worldgen Routing

**File:** `Core/World/Worldgen/Column/sdk_worldgen_column.c`

Replace period-based detection with space-based routing:

```c
void sdk_worldgen_fill_chunk(SdkWorldGen* wg, SdkChunk* chunk) {
    switch(chunk->space_type) {
        case SDK_SPACE_GLOBAL_CHUNK:
            if (walls_enabled) {
                if (global_chunk_is_wall_chunk(chunk->cx, chunk->cz, 
                        ring_size, x_offset, z_offset)) {
                    generate_wall_chunk(wg, chunk);
                    return;
                }
            }
            generate_terrain_chunk(wg, chunk);
            break;
            
        case SDK_SPACE_SUPERCHUNK_TERRAIN:
            // Attached mode - check if wall position
            if (walls_enabled) {
                if (global_chunk_is_wall_chunk(chunk->cx, chunk->cz,
                        ring_size, x_offset, z_offset)) {
                    generate_wall_chunk(wg, chunk);
                    return;
                }
            }
            generate_terrain_chunk(wg, chunk);
            break;
            
        case SDK_SPACE_TERRAIN_SUPERCHUNK:
            // Detached mode - pure terrain, no wall check needed
            generate_terrain_chunk(wg, chunk);
            break;
            
        case SDK_SPACE_WALL_GRID:
            // Detached mode - this IS a wall chunk by definition
            generate_wall_chunk(wg, chunk);
            break;
    }
}
```

## Role vs Space Type Relationship

| walls_detached | Space Type | Chunk Role | Meaning |
|---------------|------------|------------|---------|
| false | SDK_SPACE_GLOBAL_CHUNK | SDK_CHUNK_ROLE_WALL_SUPPORT | Wall in legacy flat world |
| false | SDK_SPACE_GLOBAL_CHUNK | SDK_CHUNK_ROLE_PRIMARY | Pure terrain in flat world |
| false | SDK_SPACE_SUPERCHUNK_TERRAIN | SDK_CHUNK_ROLE_WALL_SUPPORT | Wall embedded in attached superchunk |
| false | SDK_SPACE_SUPERCHUNK_TERRAIN | SDK_CHUNK_ROLE_PRIMARY | Pure terrain in attached superchunk |
| true | SDK_SPACE_WALL_GRID | SDK_CHUNK_ROLE_WALL_SUPPORT | Wall in dedicated wall space |
| true | SDK_SPACE_TERRAIN_SUPERCHUNK | SDK_CHUNK_ROLE_PRIMARY | Pure terrain (no walls in this space) |

**Key Principle:** When `walls_detached=true`:
- Wall chunks → `SDK_SPACE_WALL_GRID` + `SDK_CHUNK_ROLE_WALL_SUPPORT`
- Terrain chunks → `SDK_SPACE_TERRAIN_SUPERCHUNK` + `SDK_CHUNK_ROLE_PRIMARY`

When `walls_detached=false`:
- All chunks → `SDK_SPACE_SUPERCHUNK_TERRAIN` (or `SDK_SPACE_GLOBAL_CHUNK` for legacy)
- Wall vs terrain determined by position within the unified space

## Configuration Flow

1. **UI Settings** → `g_world_create_walls_detached`
2. **Create Request** → `SdkWorldCreateRequest.walls_detached`
3. **Metadata** → `SdkWorldSaveMeta.walls_detached` → `meta.txt`
4. **Session Start** → `SdkSuperchunkConfig.walls_detached` via `sdk_superchunk_set_config()`
5. **Chunk Manager** → Uses `sdk_superchunk_get_walls_detached()` to decide emission strategy
6. **Worldgen** → Routes by `chunk->space_type`

## Phase 1 Status (Complete)

Coordinate space headers created:
- `sdk_coordinate_space.h` - Core abstraction
- `sdk_space_global_chunk.h` - Legacy flat world
- `sdk_space_superchunk_terrain.h` - Terrain superchunks
- `sdk_space_wall_grid.h` - Detached wall grid
- `sdk_coordinate_space_conversions.h` - Cross-space utilities

## Phase 2 Tasks

1. Add `space_type` field to `SdkChunkResidencyTarget`
2. Add `space_type` field to `SdkChunk`
3. Update chunk manager emit functions to tag space type
4. Update `desired_add()` to accept and store space type
5. Update chunk allocation to copy space type from residency target
6. Refactor `sdk_worldgen_fill_chunk()` to route by space type
7. Remove/deprecate `chunk_is_full_superchunk_wall_chunk()` period-based detection
8. Add space-aware helper functions to coordinate space headers
9. Update chunk streamer to handle space-typed chunks
10. Update serialization if needed (space type persistence)

## Files to Modify

### Headers (Field Additions)
- `Core/World/Chunks/sdk_chunk.h` - Add space_type to SdkChunk
- `Core/World/Chunks/ChunkManager/sdk_chunk_manager.h` - Add space_type to SdkChunkResidencyTarget

### Implementation (Logic Changes)
- `Core/World/Chunks/ChunkManager/sdk_chunk_manager.c` - Update emit functions, desired_add
- `Core/World/Chunks/sdk_chunk.c` - Allocation, initialization
- `Core/World/Worldgen/Column/sdk_worldgen_column.c` - Space-based routing
- `Core/World/Worldgen/sdk_worldgen.c` - Chunk generation entry points

### Chunk Streamer
- `Core/World/Chunks/ChunkStreamer/sdk_chunk_streamer.c` - Handle space-typed chunks

## Migration Path

1. Add fields (backward compatible - new fields default to 0/unknown)
2. Update chunk manager to populate space type (based on `walls_detached` config)
3. Update worldgen to check space type (with fallback to period detection for legacy)
4. Once stable, remove legacy period-based detection

## Open Questions

1. Does the chunk streamer need to know space type for disk serialization?
2. Should space type be persisted or recalculated on load?
3. How do debugging/mapping tools display space-typed chunks?
4. Should the chunk manager emit terrain and wall chunks separately or interleaved?
