# SDK Core Types

## Overview

`sdk_types.h` provides fundamental type definitions, constants, and descriptors used across the entire NQL SDK. This header defines error codes, math types, item types, construction system types, world descriptors, and initialization descriptors.

**File:** `SDK/Core/sdk_types.h` (350 lines)  
**Purpose:** Central type definitions for SDK API  
**Dependencies:** None (pure C, no C++ dependencies)

---

## Error / Result Codes

### SdkResult Enum

```c
typedef enum SdkResult {
    SDK_OK                  = 0,
    SDK_ERR_GENERIC         = -1,
    SDK_ERR_INVALID_ARG     = -2,
    SDK_ERR_OUT_OF_MEMORY   = -3,
    SDK_ERR_WINDOW_FAILED   = -4,
    SDK_ERR_DEVICE_FAILED   = -5,
    SDK_ERR_SWAPCHAIN_FAILED = -6,
    SDK_ERR_PIPELINE_FAILED = -7,
    SDK_ERR_SHADER_FAILED   = -8,
    SDK_ERR_ALREADY_INIT    = -9,
    SDK_ERR_NOT_INIT        = -10
} SdkResult;
```

**Usage:** Most SDK functions return `SdkResult` to indicate success or failure. Always check return values.

---

## Math Types

### Vector Types

```c
typedef struct SdkVec2 {
    float x, y;
} SdkVec2;

typedef struct SdkVec3 {
    float x, y, z;
} SdkVec3;

typedef struct SdkVec4 {
    float x, y, z, w;
} SdkVec4;
```

### Matrix Type

```c
typedef struct SdkMat4 {
    float m[4][4];  /* m[col][row] - column-major for HLSL compatibility */
} SdkMat4;
```

**Note:** Matrix is stored in column-major order for HLSL shader compatibility.

### Math Constants

```c
#define SDK_PI 3.14159265358979f
#define SDK_DEG2RAD (SDK_PI / 180.0f)
#define SDK_RAD2DEG (180.0f / SDK_PI)
```

### Color Type

```c
typedef struct SdkColor4 {
    float r, g, b, a;
} SdkColor4;
```

---

## Vertex Format

### SdkVertex

```c
typedef struct SdkVertex {
    SdkVec3   position;
    SdkColor4 color;
} SdkVertex;
```

**Purpose:** Per-vertex data for Milestone 1 rendering (position + color). Extended in later versions.

---

## Item / Tool Types

### ItemType Enum

The `ItemType` enum defines all placeable items in the game. Values are organized into ranges:

| Range | Start | End | Category |
|-------|-------|-----|----------|
| Legacy blocks | 1 | 105 | Natural and engineered blocks |
| Tools | 64 | 76 | Pickaxes, axes, shovels, swords, saw, chisel |
| Materials | 128 | 167 | Crafting and strategic materials |
| Food | 192 | 178 | Raw/cooked meat, berries |
| Weapons | 172 | 178 | Guns and grenades |
| Entity spawners | 208 | 215 | Creative-only NPC spawners |

**Special Values:**
- `ITEM_NONE = 0` - Empty slot
- `ITEM_BLOCK_MAX` - Last block type marker
- `ITEM_TYPE_COUNT = 256` - Total item type count

**Key Block Types:**
- Natural: Grass, Dirt, Stone, Sand, Water, Snow, Gravel, Bedrock, Log, Leaves
- Engineering: Crushed stone, Compacted fill, Sandbags, Bricks, Stone bricks, Concrete variants, Timber variants, Thatch, Adobe, Mudbrick, Plaster, Cut stone, Flagstone, Roof tiles, Corrugated metal, Wrought iron, Glass, Rope netting

**Tool Tiers:**
- Wood (64-67): Wood pickaxe, axe, shovel, sword
- Stone (142-145): Stone pickaxe, axe, shovel, sword
- Iron (146-150): Iron pickaxe, axe, shovel, saw, chisel

**Material Types:**
- Building materials: Stick, Coal, Iron ingot, Clay, Limestone, Aggregate
- Ores: Copper, Sulfur, Tungsten, Bauxite, Lead-zinc, Salt
- Organic: Hide, Ironstone

---

## Construction System Types

### World Cell Codes

```c
typedef uint16_t SdkWorldCellCode;
typedef uint32_t SdkConstructionArchetypeId;
```

### SdkWorldCellKind

```c
typedef enum SdkWorldCellKind {
    SDK_WORLD_CELL_KIND_FULL_BLOCK = 0,
    SDK_WORLD_CELL_KIND_INLINE_CONSTRUCTION = 1,
    SDK_WORLD_CELL_KIND_OVERFLOW_CONSTRUCTION = 2
} SdkWorldCellKind;
```

**Purpose:** Distinguishes between solid blocks and inline/overflow construction cells.

### SdkInlineConstructionProfile

```c
typedef enum SdkInlineConstructionProfile {
    SDK_INLINE_PROFILE_NONE = 0,
    
    // Half blocks (axis-aligned)
    SDK_INLINE_PROFILE_HALF_NEG_X,
    SDK_INLINE_PROFILE_HALF_POS_X,
    SDK_INLINE_PROFILE_HALF_NEG_Y,
    SDK_INLINE_PROFILE_HALF_POS_Y,
    SDK_INLINE_PROFILE_HALF_NEG_Z,
    SDK_INLINE_PROFILE_HALF_POS_Z,
    
    // Quarter blocks (axis-aligned)
    SDK_INLINE_PROFILE_QUARTER_NEG_X,
    SDK_INLINE_PROFILE_QUARTER_POS_X,
    SDK_INLINE_PROFILE_QUARTER_NEG_Y,
    SDK_INLINE_PROFILE_QUARTER_POS_Y,
    SDK_INLINE_PROFILE_QUARTER_NEG_Z,
    SDK_INLINE_PROFILE_QUARTER_POS_Z,
    
    // Beam profiles (centered)
    SDK_INLINE_PROFILE_BEAM_X,
    SDK_INLINE_PROFILE_BEAM_Y,
    SDK_INLINE_PROFILE_BEAM_Z,
    
    // Strip profiles (2-block wide)
    SDK_INLINE_PROFILE_STRIP_X,
    SDK_INLINE_PROFILE_STRIP_Y,
    SDK_INLINE_PROFILE_STRIP_Z,
    
    SDK_INLINE_PROFILE_COUNT
} SdkInlineConstructionProfile;
```

**Purpose:** Defines voxel-level construction shapes for inline construction cells.

### Construction Constants

```c
enum {
    SDK_CONSTRUCTION_CELL_RESOLUTION = 16u,           // 16x16x16 voxel grid
    SDK_CONSTRUCTION_CELL_VOXELS = 4096u,              // 16^3
    SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT = 64u,       // 4096 / 64 bits
    SDK_PERSISTENCE_SHAPED_ITEM_B64_MAX = 689,         // Max base64 encoded payload
    SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE = 32u,       // Max palette entries
    SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID = 0u          // Invalid archetype marker
};
```

### World Cell Encoding

```c
enum {
    SDK_WORLD_CELL_INLINE_BASE = 0x8000u,              // High bit marks inline construction
    SDK_WORLD_CELL_OVERFLOW_CODE = 0xFFFFu,             // Marker for overflow cell
    SDK_WORLD_CELL_INLINE_PROFILE_SHIFT = 8u,            // Bits 8-14: profile ID
    SDK_WORLD_CELL_INLINE_MATERIAL_MASK = 0x00FFu,       // Bits 0-7: material
    SDK_WORLD_CELL_INLINE_PROFILE_MASK = 0x7F00u        // Bits 8-14: profile
};
```

**Encoding Scheme:**
- Bits 0-7: Material type (BlockType)
- Bits 8-14: Inline construction profile ID
- Bit 15: Set if inline construction cell (0x8000)
- Value 0xFFFF: Marker for overflow construction cell

### Tool Classes

```c
typedef enum {
    TOOL_NONE = 0,
    TOOL_PICKAXE,
    TOOL_AXE,
    TOOL_SHOVEL,
    TOOL_SWORD,
    TOOL_SAW,
    TOOL_CHISEL,
} ToolClass;
```

### Tool Tiers

```c
typedef enum {
    TIER_HAND = 0,
    TIER_WOOD,
    TIER_STONE,
    TIER_IRON,
} ToolTier;
```

### Block Tool Preferences

```c
typedef enum {
    BLOCK_TOOL_NONE = 0,
    BLOCK_TOOL_PICKAXE,
    BLOCK_TOOL_AXE,
    BLOCK_TOOL_SHOVEL,
} BlockToolPref;
```

**Purpose:** Indicates which tool is most effective for breaking a block type.

### Item Payload Types

```c
typedef enum SdkItemPayloadKind {
    SDK_ITEM_PAYLOAD_NONE = 0,
    SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION = 1
} SdkItemPayloadKind;

typedef enum SdkConstructionItemIdentityKind {
    SDK_CONSTRUCTION_ITEM_IDENTITY_NONE = 0,
    SDK_CONSTRUCTION_ITEM_IDENTITY_UNORDERED_BOX = 1
} SdkConstructionItemIdentityKind;
```

### Construction Item Payload

```c
typedef struct SdkConstructionItemPayload {
    uint16_t material;                          // BlockType
    uint8_t  inline_profile_hint;              // SdkInlineConstructionProfile
    uint8_t  item_identity_kind;               // Identity comparison mode
    uint16_t occupied_count;                   // Number of occupied voxels
    uint16_t unordered_box_dims_packed;        // Packed box dimensions
    uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];  // 64-word bitmap
} SdkConstructionItemPayload;
```

**Purpose:** Encodes shaped construction items (voxel occupancy data) for persistence and crafting.

### Pickup Item

```c
typedef struct SdkPickupItem {
    ItemType item;                              // Item type
    uint16_t display_block_type;               // BlockType for display
    uint8_t payload_kind;                      // SdkItemPayloadKind
    uint8_t reserved0;
    int count;                                  // Stack size
    int durability;                             // Tool durability
    SdkConstructionItemPayload shaped;          // Construction data if applicable
} SdkPickupItem;
```

**Purpose:** Represents an item in inventory or as a world pickup entity.

---

## Window Descriptor

### SdkWindowDesc

```c
typedef struct SdkWindowDesc {
    const char* title;          // Window title (UTF-8). NULL → "NQL SDK"
    uint32_t    width;          // Client area width.  0 → 800
    uint32_t    height;         // Client area height. 0 → 600
    bool        resizable;      // Allow user resize. Default true
} SdkWindowDesc;
```

**Usage:** Passed to `sdk_init()` to configure the render window.

---

## World Descriptor

### SdkWorldCoordinateSystem

```c
typedef enum SdkWorldCoordinateSystem {
    SDK_WORLD_COORDSYS_CHUNK_SYSTEM = 0,
    SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM = 1,
    SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM = 2
} SdkWorldCoordinateSystem;
```

**Purpose:** Selects the coordinate system for world generation:
- **CHUNK_SYSTEM:** Standard chunk-based coordinates (no superchunks)
- **SUPERCHUNK_SYSTEM:** Superchunk-based terrain (16x16 chunk groups)
- **GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM:** Detached wall grid + terrain superchunks

### SdkWorldDesc

```c
typedef struct SdkWorldDesc {
    uint32_t seed;              // 0 → default deterministic seed
    int16_t  sea_level;         // 0 → default sea level chosen by worldgen
    uint16_t macro_cell_size;   // 0 → default 32 block macro cell
    uint8_t coordinate_system;  // SdkWorldCoordinateSystem
    bool settlements_enabled;
    bool walls_enabled;
    bool construction_cells_enabled;
} SdkWorldDesc;
```

**Usage:** Passed to `sdk_init()` to configure world generation parameters.

**Parameters:**
- `seed`: World generation seed. 0 uses a deterministic default.
- `sea_level`: Water surface Y level. 0 lets worldgen choose.
- `macro_cell_size`: Size of macro cells for terrain synthesis. 0 uses default (32).
- `coordinate_system`: Coordinate system selection (see above).
- `settlements_enabled`: Enable settlement generation.
- `walls_enabled`: Enable superchunk wall generation.
- `construction_cells_enabled`: Enable inline construction cell system.

---

## Initialization Descriptor

### SdkInitDesc

```c
typedef struct SdkInitDesc {
    SdkWindowDesc window;       // Window parameters
    SdkColor4     clear_color;  // Background clear colour. {0,0,0,0} → dark grey
    bool          enable_debug; // Enable D3D12 debug layer
    bool          vsync;         // Present with vsync. Default true
    SdkWorldDesc  world;        // World generation descriptor
} SdkInitDesc;
```

**Usage:** Top-level configuration passed to `sdk_init()`.

**Parameters:**
- `window`: Window configuration (title, size, resizable)
- `clear_color`: Background clear color. {0,0,0,0} uses dark grey default.
- `enable_debug`: Enable D3D12 validation layer (development only)
- `vsync`: Enable vertical sync for presentation
- `world`: World generation configuration

---

## Usage Example

```c
#include "sdk_types.h"

int main(void) {
    // Configure window
    SdkWindowDesc window = {
        .title = "My Game",
        .width = 1280,
        .height = 720,
        .resizable = true
    };
    
    // Configure world
    SdkWorldDesc world = {
        .seed = 12345,
        .sea_level = 62,
        .macro_cell_size = 32,
        .coordinate_system = SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM,
        .settlements_enabled = true,
        .walls_enabled = true,
        .construction_cells_enabled = true
    };
    
    // Configure initialization
    SdkInitDesc init = {
        .window = window,
        .clear_color = {0.1f, 0.2f, 0.3f, 1.0f},
        .enable_debug = false,
        .vsync = true,
        .world = world
    };
    
    // Initialize SDK
    SdkResult result = sdk_init(&init);
    if (result != SDK_OK) {
        fprintf(stderr, "SDK init failed: %d\n", result);
        return 1;
    }
    
    // ... game loop ...
    
    sdk_shutdown();
    return 0;
}
```

---

## Design Notes

### Coordinate System Selection

Choose coordinate system based on game requirements:
- **Chunk System:** Simplest, no walls, suitable for flat worlds
- **Superchunk System:** Terrain superchunks, attached walls
- **Grid + Terrain:** Detached wall grid (configurable size/offset), terrain superchunks

### Construction Cell Encoding

The inline construction encoding is compact:
- 16-bit cell code stores both material and profile
- High bit (0x8000) distinguishes inline from full blocks
- Overflow cells (0xFFFF) reference external voxel data

### Item Type Ranges

Item type ranges are organized for efficient filtering:
- Tools start at 64 (allow 63 block types before tools)
- Materials start at 128 (allow 64 tool types before materials)
- Food starts at 192 (allow 64 material types before food)
- Spawners start at 208 (allow 16 food types before spawners

---

## Related Documentation

- `SDK/Core/API/SDK_APIReference.md` - API initialization functions
- `SDK/Core/World/CoordinateSystems/SDK_WorldCoordinateSystems.md` - Coordinate system details
- `SDK/Core/World/ConstructionCells/SDK_ConstructionSystem.md` - Construction cell system
- `SDK/Core/Items/SDK_Items.md` - Item system details
