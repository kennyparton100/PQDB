# Wall and Superchunk Configuration Implementation Plan

**Status:** Future Implementation (Post-Stabilization)  
**Date:** 2026-04-09  
**Complexity:** High - Requires engine-wide refactoring  

---

## Executive Summary

This document outlines the gap between the current wall/superchunk implementation and the desired fully-independent configuration system. While the current implementation couples walls to superchunks, the target design allows walls and superchunks to exist as completely independent organizational layers.

**WARNING:** This is intentionally deferred work. The current implementation was difficult to stabilize, and these changes would require extensive refactoring across world generation, persistence, residency management, and rendering.

---

## Target Configuration Matrix (All Combinations Must Work)

| Superchunks | Walls | Detached | Grid Size | Offsets | Description |
|-------------|-------|----------|-----------|---------|-------------|
| ❌ | ❌ | N/A | N/A | N/A | Pure terrain chunks, no walls, no superchunk organization |
| ❌ | ✅ | ❌ | = wall_span | Ignored | Walls on attached grid (period = wall_span), terrain unorganized |
| ❌ | ✅ | ✅ | Configurable | Configurable | Walls on detached grid, terrain unorganized, wall-local coords separate |
| ✅ | ❌ | N/A | N/A | N/A | Terrain superchunks, no walls generated |
| ✅ | ✅ | ❌ | = chunk_span | Ignored | Attached mode - walls align with superchunk boundaries |
| ✅ | ✅ | ✅ | Configurable | Configurable | Detached mode - independent wall grid with offsets |

---

## Key Geometric Concepts

### Wall Period Calculation

**CRITICAL CORRECTION:** Period is NOT equal to wall_grid_size.

When offset = 0, walls are found at coordinates where:
```
X = 0 || X % (Wall_Ring_Size - 1) == 0
```

Where `Wall_Ring_Size` is the configurable grid size (default: chunk_span + 2 when detached, chunk_span when attached).

The period between walls is `Wall_Ring_Size - 1`, not `Wall_Ring_Size`. Using `wall_grid_size` directly as period would place walls one chunk too far apart.

### Wall Ownership Model

Current shared-boundary model (must preserve):
- West and north wall geometry lives inside the superchunk that owns it
- East and south visible perimeter support comes from adjacent outer ring
- This prevents double-authoring of wall chunks

When superchunks are disabled but walls are enabled:
- Need a global wall ownership/coordinate system
- Wall chunks exist in a separate namespace from terrain chunks
- Terrain chunks at wall positions are either empty or have wall data overlay

---

## Current Implementation

### Configuration Structure
```c
typedef struct {
    bool enabled;              // Superchunks enabled
    int  chunk_span;           // Terrain interior span (2,4,8,16,32,64,128)
    bool walls_enabled;        // Walls require superchunks (current limitation)
    bool walls_detached;       // Detached wall grid mode
    int  wall_grid_size;       // Detached wall grid ring size
    int  wall_grid_offset_x;   // Global X offset for wall grid
    int  wall_grid_offset_z;   // Global Z offset for wall grid
} SdkSuperchunkConfig;
```

### Normalization Rules (Current)
```c
config->walls_enabled = (config->enabled && config->walls_enabled) ? true : false;
config->walls_detached = (config->walls_enabled && config->walls_detached) ? true : false;

if (config->walls_detached) {
    config->wall_grid_size = normalize_detached_wall_grid_size(config->chunk_span, config->wall_grid_size);
} else {
    config->wall_grid_size = config->chunk_span;
}
```

**Current Limitation:** `walls_enabled` is forced to `false` if `enabled` is `false`.

### UI Enforcement
Frontend auto-enables superchunks when walls are enabled:
```c
if (g_world_create_walls_enabled && !g_world_create_superchunks_enabled) {
    g_world_create_superchunks_enabled = 1;
}
```

---

## Target Implementation

### Decoupled Configuration

Walls and superchunks should be independent boolean flags without cross-dependencies:

```c
typedef struct {
    // Superchunk configuration (terrain organization layer)
    bool superchunks_enabled;
    int  chunk_span;           // Valid: 2,4,8,16,32,64,128
    
    // Wall configuration (wall generation layer)
    bool walls_enabled;
    bool walls_detached;
    int  wall_ring_size;       // Renamed from wall_grid_size for clarity
    int  wall_offset_x;        // Renamed - always applies if walls_enabled
    int  wall_offset_z;        // Renamed - always applies if walls_enabled
} SdkWorldStructureConfig;
```

### Independent Normalization
```c
// Superchunk normalization - independent of walls
config->superchunks_enabled = config->superchunks_enabled ? true : false;
if (!sdk_superchunk_validate_chunk_span(config->chunk_span)) {
    config->chunk_span = 16;
}

// Wall normalization - independent of superchunks
config->walls_enabled = config->walls_enabled ? true : false;
config->walls_detached = (config->walls_enabled && config->walls_detached) ? true : false;

if (config->walls_detached) {
    // Default: chunk_span + 2, but can be any valid size
    config->wall_ring_size = normalize_wall_ring_size(config->chunk_span, config->wall_ring_size);
} else {
    // Attached mode: walls align to terrain structure
    // If superchunks enabled: align to superchunk boundaries
    // If superchunks disabled: use default period (e.g., 17)
    config->wall_ring_size = config->superchunks_enabled ? config->chunk_span : 16;
}
```

---

## Implementation Gaps

### 1. Configuration Decoupling

**Files to modify:**
- `Core/World/Superchunks/Config/sdk_superchunk_config.h`
- `Core/World/Superchunks/Config/sdk_superchunk_config.c`
- `Core/World/Persistence/sdk_world_tooling.c` (meta normalization)

**Changes needed:**
- Remove `walls_enabled = (enabled && walls_enabled)` dependency
- Rename `wall_grid_size` to `wall_ring_size` (or add comment clarifying it's ring size, not period)
- Ensure offsets apply whenever walls are enabled, regardless of superchunk state

### 2. Geometry Helper Updates

**Files to modify:**
- `Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h`
- `Core/World/Superchunks/Geometry/sdk_superchunk_geometry.c`

**Changes needed:**
- `sdk_superchunk_get_wall_period()` - needs to work when superchunks disabled
- `sdk_superchunk_wall_grid_effective_offset_x/z()` - apply offsets based on walls_enabled, not walls_detached
- `sdk_superchunk_chunk_is_wall_anywhere()` - use correct period calculation
- New helpers for "no superchunks" mode coordinate space

**Key fix needed:**
```c
// CURRENT (incorrect period calculation):
return sdk_superchunk_wall_grid_chunk_local_period_x(cx) == 0;

// CORRECT (using ring_size - 1 as period):
const int period = config->wall_ring_size - 1;
const int local = sdk_superchunk_floor_mod_i(cx - offset_x, period);
return local == 0;
```

### 3. World Generation Refactoring

**Files to modify:**
- `Core/World/Worldgen/Column/sdk_worldgen_column.c`
- `Core/World/Worldgen/Scheduler/sdk_worldgen_scheduler.c`

**Changes needed:**
- `chunk_is_full_superchunk_wall_chunk()` - handle case where no superchunk exists
- `compute_superchunk_wall_profile()` - use wall ring size, not superchunk span
- `apply_superchunk_walls()` - separate wall placement from superchunk logic
- Wall profile computation needs to work with global coordinates when superchunks disabled

### 4. Residency Management

**Files to modify:**
- `Core/World/Chunks/sdk_chunk_manager.c`
- `Core/World/Chunks/sdk_chunk_residency.c`

**Changes needed:**
- Chunk role assignment logic assumes superchunk structure
- `SDK_CHUNK_ROLE_WALL_SUPPORT` needs to work without superchunk context
- Residency set calculation for "walls only" mode

### 5. Persistence Format

**Files to modify:**
- `Core/World/Persistence/sdk_persistence.c`
- `Core/World/Persistence/sdk_chunk_save_json.c`

**Changes needed:**
- Current format assumes superchunks array OR walls array
- Need format that supports: neither, superchunks only, walls only, both
- Wall grouping logic at `sdk_persistence.c:1757` assumes superchunk dependency

### 6. Frontend UI

**Files to modify:**
- `Core/Frontend/sdk_frontend_menu.c`

**Changes needed:**
- Remove auto-enforcement of `superchunks_enabled = 1` when walls enabled
- Allow independent toggling of all four states:
  - Superchunks ON/OFF
  - Walls ON/OFF
  - Detached ON/OFF (only when walls ON)
  - Grid size editable (when detached ON)
  - Offsets editable (when walls ON)

### 7. Debugger/Tooling

**Files to modify:**
- `Debugging/debugger_mapping.c`
- `Debugging/debugger_cli_walls.c`

**Changes needed:**
- Wall mapping visualization must work without superchunk context
- Chunk analysis needs wall detection without superchunk assumptions

### 8. Documentation Updates

**Files to update:**
- `Docs/Core/World/Superchunks/SDK_SuperChunks.md`
- `Docs/Core/World/SDK_WorldOverview.md`
- `Docs/SDK_WorldSystemsGapAudit.md`
- `Docs/Debugging/SDK_Debugging.md` (fix line 62 contradiction)

---

## Coordinate System Design

### With Superchunks Enabled

Terrain coordinates are superchunk-local:
- Global chunk coord → Superchunk index + Local chunk coord
- Wall placement uses either:
  - Attached: `local_x = 0` or `local_x = chunk_span` (superchunk boundaries)
  - Detached: global wall grid with `(global_cx - offset_x) % (ring_size - 1) == 0`

### With Superchunks Disabled

Terrain coordinates are global only:
- No superchunk index exists
- Wall placement uses global wall grid:
  - Attached: `(cx - offset_x) % default_ring_size == 0`
  - Detached: `(cx - offset_x) % (ring_size - 1) == 0`

**Important:** Wall-local coordinates should not include wall chunks in the coordinate space when detached mode is enabled. Terrain chunks at wall positions are accessible via -1 and period offsets.

---

## Testing Requirements

Once implemented, each of the 6 valid configurations needs comprehensive testing:

1. **World generation** - walls placed correctly, terrain generates properly
2. **Persistence** - save/load round-trips correctly
3. **Residency** - chunks stream in/out correctly
4. **Renderer** - walls render at correct positions
5. **Player movement** - collision with walls works
6. **Debugger** - wall mapping reports correctly

---

## Recommended Implementation Order

1. **Configuration decoupling** (low risk, just flag changes)
2. **Geometry helper updates** (foundational, affects everything)
3. **Documentation updates** (clarify the model)
4. **World generation** (column/chunk fill logic)
5. **Residency management** (chunk manager updates)
6. **Persistence format** (save/load compatibility)
7. **Frontend UI** (expose new options)
8. **Debugger updates** (tooling support)
9. **Comprehensive testing** (all 6 configurations)

---

## Notes

- The current implementation was stabilized with the coupled model to reduce complexity
- Detached mode was originally designed for future flexibility but implemented as metadata-only
- The "live detached" implementation completed recently still maintains the superchunk coupling
- Full decoupling is architecturally cleaner but requires touching most world systems
- Consider doing this as part of a larger "world structure v2" refactor rather than incremental changes
