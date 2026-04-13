# SDK World Walls Configuration

## Overview

The World Walls Configuration system owns wall-grid sizing, offsets, sharing rules, and physical wall thickness. It separates physical wall generation from wall-grid coordinate classification - walls can exist in any coordinate system, with the grid-wall space being one possible classification mode.

**Files:**
- `SDK/Core/World/Walls/Config/sdk_world_walls_config.h` (195 lines) - Public API
- `SDK/Core/World/Walls/Config/sdk_world_walls_config.c` (391 lines) - Implementation

**Responsibilities:**
- Wall ring sizing and period calculation
- Wall thickness configuration (blocks and chunks)
- Wall sharing rules (shared vs independent walls)
- Wall grid coordinate space activation
- Wall grid offset configuration
- Chunk-to-wall classification queries

**Dependencies:**
- `sdk_coordinate_space.h` - Coordinate space utilities
- `sdk_chunk.h` - Chunk constants (CHUNK_WIDTH)
- `sdk_world_config.h` - World configuration integration
- `sdk_superchunk_config.h` - Superchunk configuration

---

## Architecture

### Separation of Concerns

This module separates two distinct concepts:

1. **Physical Wall Generation:** Where walls exist in the world (chunk coordinates)
2. **Wall-Grid Classification:** How chunks are classified as wall chunks

Walls can exist in any coordinate system. The wall-grid space is only one classification mode, used when `walls_detached=true` in the superchunk configuration.

### Configuration Hierarchy

```
SdkWorldWallsConfig (this module)
       │
       ├──► Physical properties
       │     - ring_size
       │     - wall_thickness_blocks
       │     - wall_thickness_chunks
       │     - wall_rings_shared
       │
       ├──► Classification mode
       │     - use_wall_grid_space
       │
       └──► Grid parameters
             - offset_x
             - offset_z
```

---

## Data Structures

### SdkWorldWallsConfig

```c
typedef struct SdkWorldWallsConfig {
    bool enabled;
    int ring_size;
    int wall_thickness_blocks;
    int wall_thickness_chunks;
    bool wall_rings_shared;
    bool use_wall_grid_space;
    int offset_x;
    int offset_z;
    bool initialized;
} SdkWorldWallsConfig;
```

**Fields:**
- `enabled`: Whether walls are enabled for this world
- `ring_size`: Span between walls including walls themselves (chunks)
- `wall_thickness_blocks`: Physical wall thickness in blocks (max CHUNK_WIDTH)
- `wall_thickness_chunks`: Physical wall thickness in chunks (typically 1)
- `wall_rings_shared`: If true, walls are shared between adjacent grid boxes. If false, each box has its own wall (walls double up at boundaries)
- `use_wall_grid_space`: If true, walls use dedicated wall-grid coordinate space for chunk classification. If false, chunk classification is owned by the active non-grid coordinate system
- `offset_x`: X-axis offset for wall grid (chunk coordinates)
- `offset_z`: Z-axis offset for wall grid (chunk coordinates)
- `initialized`: Flag indicating configuration has been initialized

**Storage:** Static global `g_world_walls_config`

**Default Values:**
- `ring_size = 18` (default detached wall grid size)
- `wall_thickness_blocks = CHUNK_WIDTH` (64 blocks)
- `wall_thickness_chunks = 1`
- `wall_rings_shared = true`
- `use_wall_grid_space = false`
- `offset_x = 0`, `offset_z = 0`

---

## Key Concepts

### Wall Ring Size

The `ring_size` represents the full box width including both wall sides:
- For shared walls: period = `ring_size - wall_thickness_chunks`
- For independent walls: period = `ring_size` (walls at both ends)

**Example (ring_size=18, thickness=1, shared=true):**
- Period = 18 - 1 = 17 chunks
- Pattern: [wall][16 interior][wall][wall][16 interior][wall]...
- Walls repeat every 17 chunks (shared boundaries)

**Example (ring_size=18, thickness=1, shared=false):**
- Period = 18 chunks
- Pattern: [wall][16 interior][wall][wall][16 interior][wall]...
- Walls repeat every 18 chunks (independent boundaries)

### Wall Thickness

Wall thickness is specified in blocks and converted to chunks:
- `wall_thickness_blocks`: Physical thickness (1-64 blocks)
- `wall_thickness_chunks`: Thickness in chunk space (1 chunk max)

**Clamping:** Thickness is clamped to range [1, CHUNK_WIDTH] blocks.

### Wall Sharing

When `wall_rings_shared=true`:
- Adjacent grid boxes share the wall at their boundary
- Walls are at period boundaries
- More efficient (fewer wall chunks)

When `wall_rings_shared=false`:
- Each grid box has its own wall at both ends
- Walls double up at boundaries
- Less efficient but allows independent wall placement

### Wall Grid Space

When `use_wall_grid_space=true`:
- Dedicated wall-grid coordinate space is used for chunk classification
- Walls are classified based on wall-grid parameters (ring_size, offsets)
- Used when `walls_detached=true` in superchunk configuration

When `use_wall_grid_space=false`:
- Chunk classification is owned by the active coordinate system
- Walls still exist but classification is handled differently
- Used for attached walls or chunk-only worlds

### Offsets

Offsets shift the wall grid origin in chunk coordinates:
- `offset_x`: X-axis offset (chunks)
- `offset_z`: Z-axis offset (chunks)

**Use Case:** Aligning wall grid with world features or customizing wall placement.

---

## Public API

### Lifecycle Functions

#### `void sdk_world_walls_config_init(const SdkWorldSaveMeta* meta, const SdkSuperchunkConfig* sc_config, SdkWorldCoordinateSystem coord_sys)`

Initialize wall configuration from world save meta and superchunk config.

**Parameters:**
- `meta`: World save metadata (optional)
- `sc_config`: Superchunk configuration (optional)
- `coord_sys`: World coordinate system

**Behavior:**
1. Extracts chunk_span from sc_config or meta
2. Extracts wall parameters (enabled, thickness, sharing, offsets) from meta or sc_config
3. Determines if wall grid space should be used (based on coordinate system)
4. Calculates minimum ring size for detached walls: `chunk_span + 2*wall_thickness_chunks`
5. Sets ring_size to requested value (from meta/sc_config) or minimum
6. For attached/chunk-only worlds: sets ring_size to `chunk_span + 2`, forces sharing, resets offsets
7. Marks configuration as initialized

**Wall Grid Space Activation:**
Wall grid space is activated when:
- Walls are enabled
- Coordinate system detaches walls (`sdk_world_coordinate_system_detaches_walls(coord_sys)`)

#### `void sdk_world_walls_config_shutdown(void)`

Shutdown wall configuration.

**Behavior:**
- Clears all configuration fields to zero
- Marks configuration as not initialized

---

### Wall Ring Queries

#### `int sdk_world_walls_get_ring_size(void)`

Get the configured wall ring size in chunks.

**Returns:** Ring size (default 18 if not initialized)

#### `int sdk_world_walls_get_interior_span_chunks(void)`

Get the interior span (space between walls) in chunks.

**Returns:** Interior span, or superchunk chunk_span if not using wall grid space

#### `int sdk_world_walls_get_period(void)`

Get the wall period (distance between wall repetitions).

**Returns:** Period in chunks, or `chunk_span + 1` if not using wall grid space

**Calculation:**
- If not using wall grid space: `chunk_span + 1`
- If using wall grid space and shared: `ring_size - wall_thickness_chunks`
- If using wall grid space and not shared: `ring_size`

#### `bool sdk_world_walls_are_rings_shared(void)`

Check if wall rings are shared between grid boxes.

**Returns:** true if shared, false if each box has its own wall

#### `int sdk_world_walls_get_thickness_chunks(void)`

Get wall thickness in chunks.

**Returns:** Thickness (typically 1)

#### `int sdk_world_walls_get_thickness_blocks(void)`

Get wall thickness in blocks.

**Returns:** Thickness in blocks (default CHUNK_WIDTH if not initialized)

#### `bool sdk_world_walls_uses_grid_space(void)`

Check if wall grid coordinate space is used for chunk classification.

**Returns:** true if wall grid space is active

#### `void sdk_world_walls_get_offset(int* out_x, int* out_z)`

Get wall grid offsets.

**Parameters:**
- `out_x`: Output X offset (can be NULL)
- `out_z`: Output Z offset (can be NULL)

#### `void sdk_world_walls_get_effective_offset(int* out_x, int* out_z)`

Get effective offsets (only valid when using wall grid space).

**Parameters:**
- `out_x`: Output X offset (can be NULL)
- `out_z`: Output Z offset (can be NULL)

**Behavior:** Returns (0, 0) if not using wall grid space

---

### Wall Position Queries

#### `int sdk_world_walls_chunk_is_wall(int chunk_coord)`

Check if a chunk coordinate is on a wall boundary.

**Parameters:**
- `chunk_coord`: Chunk coordinate (cx or cz)

**Returns:** 1 if this chunk is a wall, 0 otherwise

**Logic:**
1. Calculates local position: `(chunk_coord - offset) % period`
2. Checks if local position is in wall range (first `wall_thickness_chunks` chunks)
3. If not shared: also checks if local position is in last `wall_thickness_chunks` chunks

#### `int sdk_world_walls_chunk_is_west_wall(int cx)`

Check if a chunk coordinate is on the west wall of its grid box.

**Parameters:**
- `cx`: Chunk X coordinate

**Returns:** 1 if west wall, 0 otherwise

**Logic:** Returns true if local X position < wall_thickness_chunks

#### `int sdk_world_walls_chunk_is_north_wall(int cz)`

Check if a chunk coordinate is on the north wall of its grid box.

**Parameters:**
- `cz`: Chunk Z coordinate

**Returns:** 1 if north wall, 0 otherwise

**Logic:** Returns true if local Z position < wall_thickness_chunks

#### `int sdk_world_walls_get_box_index(int chunk_coord)`

Get the grid box (wall ring) index for a chunk coordinate.

**Parameters:**
- `chunk_coord`: Chunk coordinate (cx or cz)

**Returns:** Grid box index (which ring this chunk belongs to)

**Logic:** Returns `floor((chunk_coord - offset) / period)`

#### `int sdk_world_walls_get_local_offset(int chunk_coord)`

Get the local offset within the current grid box.

**Parameters:**
- `chunk_coord`: Chunk coordinate (cx or cz)

**Returns:** Position within box [0, period-1]

**Logic:** Returns `(chunk_coord - offset) % period`

#### `int sdk_world_walls_get_box_origin(int chunk_coord)`

Get the origin chunk coordinate of the grid box containing this chunk.

**Parameters:**
- `chunk_coord`: Chunk coordinate (cx or cz)

**Returns:** Origin chunk of the containing grid box

**Logic:** Returns `box_index * period + offset`

#### `int sdk_world_walls_chunk_local_axis(int chunk_coord)`

Alias for `sdk_world_walls_get_local_offset(chunk_coord)`.

#### `int sdk_world_walls_chunk_local_x(int cx)`

Get the local X position within the wall period.

**Parameters:**
- `cx`: Chunk X coordinate

**Returns:** Local X position [0, period-1]

#### `int sdk_world_walls_chunk_local_z(int cz)`

Get the local Z position within the wall period.

**Parameters:**
- `cz`: Chunk Z coordinate

**Returns:** Local Z position [0, period-1]

#### `int sdk_world_walls_get_canonical_wall_chunk_owner(int cx, int cz, uint8_t* out_wall_mask, int* out_origin_cx, int* out_origin_cz, int* out_period_local_x, int* out_period_local_z)`

Get comprehensive wall information for a chunk.

**Parameters:**
- `cx`: Chunk X coordinate
- `cz`: Chunk Z coordinate
- `out_wall_mask`: Output wall face mask (can be NULL)
- `out_origin_cx`: Output box origin X (can be NULL)
- `out_origin_cz`: Output box origin Z (can be NULL)
- `out_period_local_x`: Output local X position (can be NULL)
- `out_period_local_z`: Output local Z position (can be NULL)

**Returns:** Non-zero if chunk is on any wall face

**Wall Face Mask:**
- `SDK_WORLD_WALL_FACE_WEST (1 << 0)`: Chunk is on west wall
- `SDK_WORLD_WALL_FACE_NORTH (1 << 1)`: Chunk is on north wall
- `SDK_WORLD_WALL_FACE_EAST (1 << 2)`: Chunk is on east wall (non-shared only)
- `SDK_WORLD_WALL_FACE_SOUTH (1 << 3)`: Chunk is on south wall (non-shared only)

---

### Convenience Functions

#### `int sdk_world_walls_get_ring_size_blocks(void)`

Get wall ring size in blocks.

**Returns:** Ring size in blocks (`ring_size * CHUNK_WIDTH`)

#### `bool sdk_world_walls_enabled(void)`

Check if walls are enabled for this world.

**Returns:** true if walls should be generated

#### `bool sdk_world_walls_config_is_initialized(void)`

Check if wall configuration is initialized.

**Returns:** true if initialized

---

## Integration Notes

### Wall Detection in Worldgen

```c
bool should_generate_wall_chunk(int cx, int cz) {
    if (!sdk_world_walls_enabled()) {
        return false;
    }
    
    if (sdk_world_walls_uses_grid_space()) {
        // Use wall grid classification
        return sdk_world_walls_chunk_is_wall(cx) ||
               sdk_world_walls_chunk_is_wall(cz);
    } else {
        // Use superchunk classification
        return sdk_superchunk_is_wall_chunk(cx, cz);
    }
}
```

### Wall Block Placement

```c
void generate_wall_blocks(SdkChunk* chunk, int cx, int cz) {
    uint8_t wall_mask;
    int origin_cx, origin_cz;
    int local_x, local_z;
    
    if (!sdk_world_walls_get_canonical_wall_chunk_owner(
            cx, cz, &wall_mask, &origin_cx, &origin_cz, &local_x, &local_z)) {
        return;  // Not a wall chunk
    }
    
    // Place wall blocks based on wall_mask
    if (wall_mask & SDK_WORLD_WALL_FACE_WEST) {
        fill_wall_face(chunk, WALL_FACE_WEST);
    }
    if (wall_mask & SDK_WORLD_WALL_FACE_NORTH) {
        fill_wall_face(chunk, WALL_FACE_NORTH);
    }
    // ... handle east/south for non-shared walls
}
```

### Coordinate Space Conversion

```c
void convert_to_wall_grid_space(int world_x, int world_z, 
                                int* out_grid_x, int* out_grid_z) {
    if (!sdk_world_walls_uses_grid_space()) {
        *out_grid_x = sdk_world_to_chunk_x(world_x);
        *out_grid_z = sdk_world_to_chunk_z(world_z);
        return;
    }
    
    int offset_x, offset_z;
    sdk_world_walls_get_effective_offset(&offset_x, &offset_z);
    
    int cx = sdk_world_to_chunk_x(world_x);
    int cz = sdk_world_to_chunk_z(world_z);
    
    *out_grid_x = sdk_world_walls_chunk_local_x(cx);
    *out_grid_z = sdk_world_walls_chunk_local_z(cz);
}
```

### Custom Wall Grid Alignment

```c
void align_walls_to_feature(int feature_x, int feature_z) {
    // Calculate offset to align wall grid with feature
    int period = sdk_world_walls_get_period();
    int offset_x = (period - (feature_x % period)) % period;
    int offset_z = (period - (feature_z % period)) % period;
    
    // Update configuration (requires re-init)
    SdkWorldSaveMeta meta;
    sdk_persistence_get_meta(&meta);
    meta.wall_grid_offset_x = offset_x;
    meta.wall_grid_offset_z = offset_z;
    sdk_persistence_save_meta(&meta);
}
```

---

## Design Notes

### Separation of Physical and Classification

The module separates physical wall existence from chunk classification:
- Walls can exist in any coordinate system
- Wall-grid space is one classification mode
- This allows future classification modes without changing physical generation

### Ring Size vs Period

The distinction between `ring_size` and `period` is important:
- `ring_size`: Full box width including walls
- `period`: Distance between wall repetitions
- For shared walls: `period = ring_size - thickness`
- For independent walls: `period = ring_size`

### Default Fallbacks

When configuration is not initialized:
- `ring_size` returns 18
- `thickness_chunks` returns 1
- `thickness_blocks` returns CHUNK_WIDTH
- `rings_shared` returns true
- `use_wall_grid_space` returns false

These defaults ensure the system works even if initialization fails.

### Offset Handling

Offsets are only applied when using wall grid space:
- Attached walls: offsets are forced to (0, 0)
- Chunk-only worlds: offsets are forced to (0, 0)
- Detached walls: offsets from meta are used

### Thickness Clamping

Wall thickness is clamped to [1, CHUNK_WIDTH] blocks:
- Prevents invalid thickness values
- Ensures walls fit within chunk boundaries
- Converts blocks to chunks via ceiling division

### Wall Face Mask

The wall face mask allows efficient wall block placement:
- West/North walls: always present at period start
- East/South walls: only present for non-shared walls at period end
- Multiple faces can be set (corner chunks)

---

## Related Documentation

- `SDK/Core/World/Superchunks/Config/SDK_SuperchunkConfig.md` - Superchunk configuration
- `SDK/Core/World/CoordinateSystems/SDK_WorldCoordinateSystems.md` - Coordinate systems
- `SDK/Core/World/GridWalls/SDK_GridWalls.md` - Wall generation details
- `SDK/Core/World/Config/SDK_WorldConfig.md` - World configuration
