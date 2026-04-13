# Architectural Concern: Missing Unified World Configuration

## Summary

The NQL SDK lacks a unified world configuration structure. World settings are scattered across multiple globals and subsystem-specific configs, making it difficult for both humans and AI to understand the complete state of a world. This architectural flaw increases cognitive load, introduces bugs, and slows development as the project grows.

## The Problem

### Current State: Fragmented Configuration

World configuration is decomposed across multiple locations:

| Setting | Current Location | Access Pattern |
|---------|------------------|----------------|
| `coordinate_system` | `g_superchunk_config` (static global in superchunk module) | `sdk_superchunk_get_coordinate_system()` |
| `seed` | Duplicated in `g_sdk.world_seed` and `g_sdk.worldgen.desc.seed` | Direct access |
| `sea_level` | `g_sdk.worldgen.desc.sea_level` | Through worldgen context |
| `chunk_span` | `g_superchunk_config` | `sdk_superchunk_get_chunk_span()` |
| `walls_enabled` | `g_superchunk_config` | `sdk_superchunk_get_config()->walls_enabled` |
| `superchunks_enabled` | Derived from `coordinate_system` in `g_superchunk_config` | Indirect |
| `world_save_id` | `g_sdk.world_save_id` | Direct access on `g_sdk` |
| `macro_cell_size` | `g_sdk.worldgen.desc.macro_cell_size` | Through worldgen context |

### The Consequence: Circuitous Dependencies

The chunk manager (which should be world-agnostic) must query the superchunk config module to determine if the world uses superchunks:

```c
// sdk_chunk_manager.c
SdkWorldCoordinateSystem coord_sys = sdk_superchunk_get_coordinate_system();  // Wrong module!
if (coord_sys != SDK_WORLD_COORDSYS_CHUNK_SYSTEM) {
    // Superchunk logic
}
```

This creates a **misleading dependency**: the chunk manager appears to depend on superchunks, when actually it depends on **world topology**.

## Why This Matters

### 1. AI Development Velocity

As projects grow beyond a certain file count, AI assistants struggle to:
- **Trace configuration flow**: Settings pass through 3+ files before reaching usage sites
- **Identify authoritative sources**: Which file "owns" a setting?
- **Understand side effects**: Changing one global may affect distant subsystems

**Example**: To find where `coordinate_system` is set, an AI must:
1. Search for `coordinate_system` → finds it in 5+ files
2. Determine which is the runtime value → `g_superchunk_config`
3. Find who writes to it → `sdk_superchunk_set_coordinate_system()`
4. Find callers → `sdk_superchunk_normalize_config()`
5. Trace back to world load → `sdk_world_meta_to_superchunk_config()`
6. Finally discover it originates from `meta.txt` → `SdkWorldSaveMeta`

This 6-step trace is **fragile** and **expensive** in context windows.

### 2. Human Cognitive Load

Developers must remember:
- Which module owns which slice of world config
- Whether a setting is cached, derived, or queried live
- The implicit initialization order (worldgen before chunk manager, etc.)

### 3. Bug Amplification

Scattered configuration enables:
- **Inconsistent state**: `g_sdk.world_seed` vs `g_sdk.worldgen.desc.seed`
- **Ordering bugs**: Querying config before it's loaded from persistence
- **Testing gaps**: Mocking requires setting multiple globals

## The Root Cause

The SDK evolved from a **single-world, single-session** model where globals were acceptable. As features were added:
- Superchunks needed config → `g_superchunk_config` created
- Worldgen needed seed → stored in `SdkWorldGen`
- Persistence needed ID → stored in `g_sdk`
- No unification pass was done

The coordinate system refactor (replacing `walls_detached`) exposed this: a **world-level** setting naturally belongs in a **world config**, but one doesn't exist, so it was stuffed into the closest match (`g_superchunk_config`).

## Proposed Solution

### Phase 1: Document the Pattern (This File)
✅ Identify the problem and its scope

### Phase 2: Introduce `SdkWorldConfig`

Create a unified structure that owns all world-level settings:

```c
typedef struct SdkWorldConfig {
    // Identity
    char world_save_id[64];
    char world_save_name[64];
    uint32_t seed;
    
    // Coordinate System (the core topology setting)
    SdkWorldCoordinateSystem coordinate_system;
    
    // Superchunk Settings (only meaningful for superchunk worlds)
    int chunk_span;
    bool walls_enabled;
    int wall_grid_size;
    int wall_grid_offset_x;
    int wall_grid_offset_z;
    
    // Worldgen Parameters
    int sea_level;
    int macro_cell_size;
    
    // Runtime State (not persisted)
    bool initialized;
    uint64_t session_start_time;
} SdkWorldConfig;
```

### Phase 3: Centralize Access

Replace scattered globals with a single authoritative source:

```c
// Single global instance
SdkWorldConfig g_world_config;

// All subsystems query through consistent API
SdkWorldCoordinateSystem sdk_world_get_coordinate_system(void);
int sdk_world_get_seed(void);
const SdkWorldConfig* sdk_world_get_config(void);

// Chunk manager no longer depends on superchunk module
// It depends on world config (correct dependency)
```

### Phase 4: Deprecate Legacy Access

- Keep `sdk_superchunk_get_config()` for superchunk-specific settings
- Have it return a view into `g_world_config`
- Eventually migrate all callers to `sdk_world_get_config()`

## Specific Examples of Harm

### Example 1: Coordinate System Confusion

The coordinate system is a **world topology** setting, but:
- It's stored in `g_superchunk_config`
- Accessor is `sdk_superchunk_get_coordinate_system()`
- Used by chunk manager, which has nothing to do with superchunks

This caused the refactor at `sdk_chunk_manager.c:1015` to use pseudocode (`if (cm->has_superchunks)`) because the correct check (`sdk_world_uses_superchunks()`) doesn't exist.

### Example 2: Seed Duplication

```c
// sdk_api.c:21-22
g_sdk.world_seed = 0u;
// ...
// Also stored in g_sdk.worldgen.desc.seed
```

Which is authoritative? Can they diverge?

### Example 3: Wall Period Calculation

Wall period depends on:
- `chunk_span` (from superchunk config)
- `coordinate_system` (from superchunk config, but world-level)
- `walls_detached` (legacy, now derived from coordinate system)

The calculation is split between:
- `sdk_superchunk_get_wall_period()`
- `sdk_superchunk_get_wall_grid_period()`
- Inline calculations in geometry headers

All accessing different parts of scattered config.

## Impact on AI Development

### Context Window Pressure

When working on coordinate system logic, an AI needs to hold in context:
- `SdkWorldSaveMeta` (persistence)
- `SdkWorldDesc` (worldgen)
- `SdkSuperchunkConfig` (superchunk config)
- `SdkChunkManager` (chunk management)
- `g_superchunk_config` (runtime global)
- Plus multiple accessor functions

This is **~500+ lines** of context just to understand one setting.

### Error Prone Refactoring

The recent coordinate system refactor required changes in:
- `sdk_types.h` (enum definition)
- `sdk_superchunk_config.c/h` (storage + accessors)
- `sdk_chunk_manager.c` (usage)
- `sdk_worldgen_column.c` (routing logic)
- `sdk_api_session_core.c` (initialization)
- 3x debugger files
- Persistence layer

With a unified config, this would have been:
- Add field to `SdkWorldConfig`
- Update normalization logic
- Single accessor updates all usage sites

## Recommendation

1. **Accept this as P0 technical debt**: The scattered config pattern will increasingly block AI-assisted development as the project grows.

2. **Implement `SdkWorldConfig` incrementally**:
   - Create the structure
   - Migrate `coordinate_system` as the first field (proving the pattern)
   - Gradually migrate other settings

3. **Establish the rule**: New world-level settings go in `SdkWorldConfig` first, then exposed through subsystem APIs if needed.

4. **Document the dependency graph**: Make explicit which subsystems depend on world config vs. their own specialized config.

## Related Files

- `Core/World/Superchunks/Config/sdk_superchunk_config.c` - Contains `g_superchunk_config`
- `Core/API/Internal/sdk_api_internal.h` - Defines `SdkApiState g_sdk`
- `Core/World/Chunks/ChunkManager/sdk_chunk_manager.c` - Forced to query superchunk config
- `Core/World/Worldgen/sdk_worldgen.h` - `SdkWorldGen` owns slice of world config
- `Core/World/Persistence/sdk_world_tooling.c` - Bridges persistence to scattered globals

## See Also

- Wall System Contract Failure (adjacent architectural issue)
- Engine Audit Fix Plan (prioritization)
