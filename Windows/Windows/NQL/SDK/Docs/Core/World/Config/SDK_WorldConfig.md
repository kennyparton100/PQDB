# SDK World Configuration

## Overview

The World Configuration system provides unified access to world topology settings, coordinating coordinate systems, walls, superchunks, and persistence. It serves as the central initialization point for world systems during world load/creation.

**Files:**
- `SDK/Core/World/Config/sdk_world_config.h` (51 lines) - Public API
- `SDK/Core/World/Config/sdk_world_config.c` (81 lines) - Implementation

**Responsibilities:**
- Initializing world configuration from save metadata
- Providing authoritative access to coordinate system
- Coordinating superchunk and wall configuration initialization
- Managing world configuration lifecycle (init/shutdown)

**Dependencies:**
- `sdk_types.h` - SdkWorldCoordinateSystem, SdkWorldSaveMeta
- `sdk_world_tooling.h` - Persistence integration
- `sdk_superchunk_config.h` - Superchunk configuration
- `sdk_world_walls_config.h` - Wall configuration

---

## Architecture

### Initialization Flow

```
World Load/Creation
       │
       ▼
SdkWorldSaveMeta (from persistence or new world)
       │
       ▼
sdk_world_config_init(meta)
       │
       ├──► Validate coordinate system
       ├──► Set coordinate_system
       ├──► Set has_walls
       ├──► Convert meta → SdkSuperchunkConfig
       ├──► sdk_superchunk_set_config()
       ├──► sdk_world_walls_config_init()
       └──► Mark initialized = true
```

### Configuration Hierarchy

```
SdkWorldConfig (this module)
       │
       ├──► SdkSuperchunkConfig (Superchunks/Config/)
       │     - Chunk span, wall detachment, wall grid params
       │
       └──► SdkWorldWallsConfig (Walls/Config/)
             - Wall grid size, offsets, thickness, sharing rules
```

---

## Data Structures

### SdkWorldConfig

```c
typedef struct SdkWorldConfig {
    SdkWorldCoordinateSystem coordinate_system;
    bool has_walls;
    bool initialized;
} SdkWorldConfig;
```

**Fields:**
- `coordinate_system`: The world's coordinate system (CHUNK_SYSTEM, SUPERCHUNK_SYSTEM, or GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM)
- `has_walls`: Whether walls are enabled for this world
- `initialized`: Flag indicating configuration has been initialized

**Storage:** Static global `g_world_config`

---

## Public API

### Lifecycle Functions

#### `void sdk_world_config_init(const SdkWorldSaveMeta* meta)`

Initialize world configuration from save metadata.

**Parameters:**
- `meta`: World save metadata (must not be NULL)

**Behavior:**
1. Validates meta is not NULL (exits with error if NULL)
2. Extracts and validates coordinate system from meta
3. Sets coordinate_system and has_walls from meta
4. Converts meta to SdkSuperchunkConfig
5. Initializes superchunk configuration
6. Initializes wall configuration
7. Marks configuration as initialized

**Error Handling:**
- NULL meta: Prints error and exits with code 1
- Invalid coordinate system: Prints error and exits with code 1

**Called During:** World load/creation, after persistence metadata is available

#### `void sdk_world_config_shutdown(void)`

Shutdown world configuration.

**Behavior:**
1. Shuts down wall configuration
2. Resets coordinate_system to CHUNK_SYSTEM (default)
3. Resets has_walls to false
4. Marks configuration as not initialized

**Called During:** World unload, before freeing world resources

---

### Accessor Functions

#### `SdkWorldCoordinateSystem sdk_world_get_coordinate_system(void)`

Get the world's coordinate system.

**Returns:** Current coordinate system, or CHUNK_SYSTEM if not initialized

**Note:** This is the authoritative accessor for world topology. Always use this instead of reading the meta directly.

#### `bool sdk_world_has_walls(void)`

Check if the world has walls enabled.

**Returns:** true if walls are enabled, false otherwise

#### `bool sdk_world_config_is_initialized(void)`

Check if world configuration is initialized.

**Returns:** true if configuration has been initialized, false otherwise

**Use Case:** Guard against accessing configuration before world load

#### `bool sdk_world_has_superchunks(void)`

Check if the current world uses superchunks.

**Returns:** true if coordinate system is SUPERCHUNK_SYSTEM or GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM, false otherwise

**Design Note:** Explicitly checks for known superchunk coordinate systems. Future coordinate systems won't automatically be treated as superchunk systems (future-proof).

---

## Integration Notes

### World Load Sequence

```c
void load_world(const char* world_path) {
    SdkWorldSaveMeta meta;
    
    // 1. Load metadata from persistence
    sdk_persistence_load_meta(world_path, &meta);
    
    // 2. Initialize world configuration
    sdk_world_config_init(&meta);
    
    // 3. Initialize coordinate space conversions
    sdk_coordinate_space_init(sdk_world_get_coordinate_system());
    
    // 4. Initialize worldgen with config
    SdkWorldDesc desc = { /* ... from meta ... */ };
    sdk_worldgen_init(&worldgen, &desc);
    
    // 5. Load chunks
    // ...
}
```

### World Creation Sequence

```c
void create_new_world(SdkWorldDesc* desc) {
    SdkWorldSaveMeta meta;
    
    // 1. Create metadata from descriptor
    sdk_world_meta_from_desc(desc, &meta);
    
    // 2. Initialize world configuration
    sdk_world_config_init(&meta);
    
    // 3. Initialize coordinate space conversions
    sdk_coordinate_space_init(sdk_world_get_coordinate_system());
    
    // 4. Initialize worldgen
    sdk_worldgen_init(&worldgen, desc);
    
    // 5. Generate initial chunks
    // ...
}
```

### Coordinate System Detection

```c
void setup_chunk_streamer(SdkChunkStreamer* streamer) {
    if (sdk_world_has_superchunks()) {
        // Use superchunk-aware streaming
        streamer->mode = STREAMER_MODE_SUPERCHUNK;
    } else {
        // Use simple chunk streaming
        streamer->mode = STREAMER_MODE_CHUNK;
    }
}
```

### Wall Detection

```c
bool is_wall_chunk(int cx, int cz) {
    if (!sdk_world_has_walls()) {
        return false;
    }
    
    SdkWorldCoordinateSystem coord_sys = sdk_world_get_coordinate_system();
    
    if (coord_sys == SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM) {
        // Use detached wall grid logic
        return sdk_wall_grid_is_wall_chunk(cx, cz);
    } else if (coord_sys == SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM) {
        // Use attached wall logic
        return sdk_superchunk_is_wall_chunk(cx, cz);
    }
    
    return false;
}
```

---

## Design Notes

### Configuration Authority

The world configuration is the single source of truth for:
- Coordinate system selection
- Wall enablement
- Superchunk enablement (via coordinate system)

Other systems should query this module rather than reading persistence metadata directly.

### Initialization Order

World configuration must be initialized early in the world load sequence:
1. Load persistence metadata
2. Initialize world configuration (this module)
3. Initialize coordinate space conversions
4. Initialize worldgen
5. Initialize chunk streaming

### Coordinate System Validation

The module validates coordinate systems during initialization to prevent invalid states. Invalid coordinate systems cause immediate termination with an error message.

### Future-Proofing

The `sdk_world_has_superchunks()` function explicitly checks for known superchunk coordinate systems rather than assuming any non-CHUNK_SYSTEM uses superchunks. This prevents new coordinate systems from being incorrectly classified.

### Shutdown Safety

Shutdown resets configuration to default values, preventing stale state from affecting subsequent world loads.

---

## Related Documentation

- `SDK/Core/World/Superchunks/Config/SDK_SuperchunkConfig.md` - Superchunk configuration details
- `SDK/Core/World/Walls/Config/SDK_WorldWallsConfig.md` - Wall configuration details
- `SDK/Core/World/CoordinateSystems/SDK_WorldCoordinateSystems.md` - Coordinate system details
- `SDK/Core/World/Persistence/SDK_PersistenceAndStorage.md` - Persistence integration
