# SDK Blocks Documentation

Comprehensive documentation for the SDK block system, covering block types, material classes, properties, and behaviors.

**Module:** `SDK/Core/World/Blocks/`  
**Output:** `SDK/Docs/Core/World/Blocks/SDK_Blocks.md`

## Table of Contents

- [Module Overview](#module-overview)
- [Block Types](#block-types)
- [Block Classes](#block-classes)
- [Block Properties](#block-properties)
- [Flags and Behaviors](#flags-and-behaviors)
- [Block Definitions](#block-definitions)
- [Key Functions](#key-functions)
- [API Surface](#api-surface)
- [Integration Notes](#integration-notes)
- [AI Context Hints](#ai-context-hints)

---

## Module Overview

The Blocks module defines the complete taxonomy of placeable materials in the world. It provides block type enumeration, material classification, property definitions, and runtime behavior queries. The system supports everything from simple terrain blocks to complex construction materials and resource ores.

**Key Features:**
- 160+ defined block types
- Hierarchical material classification
- Physical properties (hardness, permeability, collapse behavior)
- Tool preferences for harvesting
- Runtime behavior flags (fluid, granular, support-sensitive)
- Color and texture metadata

---

## Block Types

### Type Enumeration

**BlockType enum** defines all placeable materials:

| Category | Block Types |
|----------|-------------|
| **Basic** | AIR, WATER, ICE, SNOW, BEDROCK |
| **Legacy Terrain** | GRASS, DIRT, STONE, SAND, GRAVEL |
| **Organic/Wood** | LOG, LEAVES, PLANKS |
| **Legacy Crafted** | COBBLESTONE, CRAFTING_TABLE, FURNACE, CAMPFIRE, ANVIL, etc. |
| **Bedrock Provinces** | GRANITE, GNEISS, SCHIST, BASALT, VOLCANIC_ROCK, SANDSTONE, SHALE, LIMESTONE, DOLOSTONE, etc. |
| **Sediments/Soils** | TOPSOIL, SUBSOIL, SILT, CLAY, ALLUVIUM, COLLUVIUM, TALUS, PEAT, TURF, LOESS, etc. |
| **Resources** | COAL_SEAM, IRONSTONE, CLAY_DEPOSIT, COPPER_BEARING_ROCK, SULFUR_BEARING_VOLCANIC_ROCK, TUNGSTEN_SKARN, BAUXITE, LEAD_ZINC_VEIN_ROCK, etc. |
| **Human-made** | CRUSHED_STONE, COMPACTED_FILL, SANDBAGS, BRICK, STONE_BRICKS, CONCRETE, REINFORCED_CONCRETE, LAVA |
| **Flora** | TALL_GRASS, DRY_GRASS, FERN, SHRUB, HEATHER, CATTAILS, WILDFLOWERS, CACTUS, SUCCULENT, MOSS |
| **Construction** | TIMBER_LIGHT, TIMBER_DARK, THATCH, WOOD_SHINGLES, WATTLE_DAUB, ADOBE, MUDBRICK, PLASTER_WHITE, PLASTER_OCHRE, CUT_STONE_LIGHT, CUT_STONE_DARK, RUBBLE_MASONRY, FLAGSTONE, etc. |
| **Character Colors** | COLOR_WHITE, COLOR_BLACK, COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN, COLOR_BLUE, COLOR_PURPLE, COLOR_PINK, COLOR_BROWN, COLOR_GRAY |

**Total Count:** `BLOCK_COUNT` (currently ~165 block types)

---

## Block Classes

### Material Classification

**SdkBlockClass enum** groups blocks by geological/material type:

| Class | Description | Examples |
|-------|-------------|----------|
| `SDK_BLOCK_CLASS_VOID` | Empty space | AIR |
| `SDK_BLOCK_CLASS_FLUID` | Liquids | WATER, LAVA |
| `SDK_BLOCK_CLASS_BEDROCK` | Crystalline bedrock | GRANITE, BASALT |
| `SDK_BLOCK_CLASS_ROCK` | General rock | STONE, SANDSTONE |
| `SDK_BLOCK_CLASS_SOIL` | Soil horizons | TOPSOIL, SUBSOIL |
| `SDK_BLOCK_CLASS_SEDIMENT` | Unconsolidated | SAND, GRAVEL, CLAY |
| `SDK_BLOCK_CLASS_ORGANIC` | Living/dead organic | LOG, LEAVES, PEAT |
| `SDK_BLOCK_CLASS_RESOURCE` | Ore/Resource bearing | COAL_SEAM, IRONSTONE |
| `SDK_BLOCK_CLASS_CONSTRUCTION` | Player-crafted | BRICK, CONCRETE, TIMBER |

### Collapse Classification

**SdkCollapseClass** determines structural stability:

| Class | Stability | Behavior |
|-------|-----------|----------|
| `SDK_COLLAPSE_NONE` | No collapse | AIR, fluids |
| `SDK_COLLAPSE_COMPETENT_ROCK` | Very stable | Granite, bedrock |
| `SDK_COLLAPSE_FRACTURED_ROCK` | Moderately stable | Weathered rock |
| `SDK_COLLAPSE_COMPACT_SEDIMENT` | Somewhat stable | Compacted soil |
| `SDK_COLLAPSE_LOOSE_SEDIMENT` | Unstable | Sand, gravel |
| `SDK_COLLAPSE_ORGANIC_SOFT` | Very unstable | Peat, loose organic |

### Runtime Material Class

**SdkRuntimeMaterialClass** for simulation behavior:

| Class | Simulation Behavior |
|-------|---------------------|
| `SDK_MATERIAL_STATIC_COMPETENT` | Rigid, no movement |
| `SDK_MATERIAL_STATIC_WEAK` | Rigid but fragile |
| `SDK_MATERIAL_GRANULAR_LOOSE` | Flows like sand |
| `SDK_MATERIAL_GRANULAR_COMPACTED` | Flows when disturbed |
| `SDK_MATERIAL_FLUID_WATER` | Water physics |
| `SDK_MATERIAL_FLUID_LAVA` | Lava physics |
| `SDK_MATERIAL_ORGANIC_SOFT` | Compressible organic |

---

## Block Properties

### SdkBlockDef Structure

```c
typedef struct {
    BlockType         id;               // Block type enum value
    const char*       name;             // Display name
    SdkBlockClass     material_class;   // Material classification
    uint32_t          flags;            // General flags
    BlockToolPref     tool_pref;        // Preferred harvesting tool
    uint8_t           hardness;         // Mining difficulty (0-255)
    uint8_t           tunnel_difficulty; // Digging difficulty
    SdkCollapseClass  collapse_class;   // Structural stability
    uint8_t           permeability;     // Water permeability (0-255)
    uint8_t           wet_strength_loss; // Strength reduction when wet
    uint8_t           road_support;      // Road building quality
    uint8_t           fortification_value; // Defense value
    ItemType          drop_item;         // Item dropped when mined
    uint32_t          face_color[6];     // Per-face colors
} SdkBlockDef;
```

### Property Details

| Property | Range | Description |
|----------|-------|-------------|
| `hardness` | 0-255 | Mining resistance. 0 = instant break, 255 = unbreakable |
| `tunnel_difficulty` | 0-255 | Digging effort for tunnels |
| `permeability` | 0-255 | Water flow-through. 0 = impermeable, 255 = fully permeable |
| `wet_strength_loss` | 0-255 | % strength reduction when saturated |
| `road_support` | 0-255 | Quality for road foundations |
| `fortification_value` | 0-255 | Defensive value for fortifications |

---

## Flags and Behaviors

### General Flags

```c
enum {
    SDK_BLOCK_FLAG_SOLID      = 1u << 0,  // Has collision
    SDK_BLOCK_FLAG_OPAQUE     = 1u << 1,  // Blocks light
    SDK_BLOCK_FLAG_FULL_CUBE  = 1u << 2,  // Occupies full 1x1x1
    SDK_BLOCK_FLAG_FLUID      = 1u << 3,  // Is a fluid
    SDK_BLOCK_FLAG_RESOURCE   = 1u << 4,  // Contains resources
    SDK_BLOCK_FLAG_NATURAL    = 1u << 5,  // Spawns naturally
    SDK_BLOCK_FLAG_PLACEABLE  = 1u << 6   // Can be placed by player
};
```

### Behavior Flags

```c
enum {
    SDK_BLOCK_BEHAVIOR_FLUID             = 1u << 0,  // Flows like liquid
    SDK_BLOCK_BEHAVIOR_GRANULAR          = 1u << 1,  // Flows like sand
    SDK_BLOCK_BEHAVIOR_SUPPORT_SENSITIVE = 1u << 2   // Needs support below
};
```

### Common Flag Combinations

| Block Type | Flags |
|------------|-------|
| Stone | SOLID \| OPAQUE \| FULL_CUBE \| NATURAL |
| Glass | SOLID \| FULL_CUBE \| PLACEABLE |
| Water | FLUID |
| Leaves | OPAQUE (partial) \| NATURAL |
| Sand | SOLID \| OPAQUE \| NATURAL + BEHAVIOR_GRANULAR |
| Log | SOLID \| OPAQUE \| FULL_CUBE \| NATURAL |
| Air | (none) |

---

## Block Definitions

### Definition Table

Block properties are defined in a static const table in `sdk_block.c`:

```c
static const SdkBlockDef k_block_defs[] = {
    [BLOCK_AIR] = {
        .id = BLOCK_AIR,
        .name = "air",
        .material_class = SDK_BLOCK_CLASS_VOID,
        .flags = 0,
        .tool_pref = BLOCK_TOOL_NONE,
        .hardness = 0,
        .collapse_class = SDK_COLLAPSE_NONE,
        .drop_item = ITEM_NONE,
    },
    
    [BLOCK_STONE] = {
        .id = BLOCK_STONE,
        .name = "stone",
        .material_class = SDK_BLOCK_CLASS_ROCK,
        .flags = SDK_BLOCK_FLAG_SOLID | SDK_BLOCK_FLAG_OPAQUE | 
                 SDK_BLOCK_FLAG_FULL_CUBE | SDK_BLOCK_FLAG_NATURAL,
        .tool_pref = BLOCK_TOOL_PICKAXE,
        .hardness = 100,
        .tunnel_difficulty = 80,
        .collapse_class = SDK_COLLAPSE_COMPETENT_ROCK,
        .permeability = 5,
        .drop_item = ITEM_COBBLESTONE,
    },
    
    [BLOCK_WATER] = {
        .id = BLOCK_WATER,
        .name = "water",
        .material_class = SDK_BLOCK_CLASS_FLUID,
        .flags = SDK_BLOCK_FLAG_FLUID,
        .tool_pref = BLOCK_TOOL_NONE,
        .hardness = 0,
        .collapse_class = SDK_COLLAPSE_NONE,
        .permeability = 255,
    },
    
    // ... more definitions
};
```

---

## Key Functions

### Property Queries

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_block_get_def` | `(BlockType) → const SdkBlockDef*` | Full definition struct |
| `sdk_block_get_name` | `(BlockType) → const char*` | Display name |
| `sdk_block_get_hardness` | `(BlockType) → int` | Mining hardness |
| `sdk_block_is_solid` | `(BlockType) → int` | Has collision |
| `sdk_block_is_opaque` | `(BlockType) → int` | Blocks light/occludes |
| `sdk_block_is_full` | `(BlockType) → int` | Full cube geometry |
| `sdk_block_is_color` | `(BlockType) → int` | Is a color block |

### Tool and Harvesting

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_block_get_tool_pref` | `(BlockType) → BlockToolPref` | Preferred tool |
| `sdk_block_get_drop_item` | `(BlockType) → ItemType` | Dropped item |

### Simulation

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_block_get_runtime_material` | `(BlockType) → SdkRuntimeMaterialClass` | Sim behavior |
| `sdk_block_get_behavior_flags` | `(BlockType) → uint32_t` | Behavior flags |
| `sdk_block_get_settling_rate` | `(BlockType) → uint8_t` | Granular settling |
| `sdk_block_get_angle_of_repose` | `(BlockType) → uint8_t` | Slope before slide |
| `sdk_block_get_fluid_viscosity` | `(BlockType) → uint8_t` | Flow resistance |

### Rendering

| Function | Signature | Description |
|----------|-----------|-------------|
| `sdk_block_get_color` | `(BlockType) → BlockColor` | Base color |
| `sdk_block_get_face_color` | `(type, face) → BlockColor` | Per-face color |
| `sdk_block_get_texture_asset` | `(type, face) → const char*` | Texture path |

---

## API Surface

### Public Header (sdk_block.h)

```c
/* =================================================================
 * BLOCK TYPE ENUM
 * ================================================================= */
typedef enum {
    BLOCK_AIR = 0,
    BLOCK_WATER,
    BLOCK_ICE,
    BLOCK_SNOW,
    BLOCK_BEDROCK,
    
    /* Legacy terrain */
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_SAND,
    BLOCK_GRAVEL,
    
    /* Organic / wood */
    BLOCK_LOG,
    BLOCK_LEAVES,
    BLOCK_PLANKS,
    
    /* More categories... */
    
    BLOCK_COUNT
} BlockType;

/* =================================================================
 * CLASSIFICATION ENUMS
 * ================================================================= */
typedef enum {
    SDK_BLOCK_CLASS_VOID = 0,
    SDK_BLOCK_CLASS_FLUID,
    SDK_BLOCK_CLASS_BEDROCK,
    SDK_BLOCK_CLASS_ROCK,
    SDK_BLOCK_CLASS_SOIL,
    SDK_BLOCK_CLASS_SEDIMENT,
    SDK_BLOCK_CLASS_ORGANIC,
    SDK_BLOCK_CLASS_RESOURCE,
    SDK_BLOCK_CLASS_CONSTRUCTION
} SdkBlockClass;

typedef enum {
    SDK_COLLAPSE_NONE = 0,
    SDK_COLLAPSE_COMPETENT_ROCK,
    SDK_COLLAPSE_FRACTURED_ROCK,
    SDK_COLLAPSE_COMPACT_SEDIMENT,
    SDK_COLLAPSE_LOOSE_SEDIMENT,
    SDK_COLLAPSE_ORGANIC_SOFT
} SdkCollapseClass;

typedef enum {
    SDK_MATERIAL_STATIC_COMPETENT = 0,
    SDK_MATERIAL_STATIC_WEAK,
    SDK_MATERIAL_GRANULAR_LOOSE,
    SDK_MATERIAL_GRANULAR_COMPACTED,
    SDK_MATERIAL_FLUID_WATER,
    SDK_MATERIAL_FLUID_LAVA,
    SDK_MATERIAL_ORGANIC_SOFT
} SdkRuntimeMaterialClass;

/* =================================================================
 * FLAGS
 * ================================================================= */
enum {
    SDK_BLOCK_FLAG_SOLID      = 1u << 0,
    SDK_BLOCK_FLAG_OPAQUE     = 1u << 1,
    SDK_BLOCK_FLAG_FULL_CUBE  = 1u << 2,
    SDK_BLOCK_FLAG_FLUID      = 1u << 3,
    SDK_BLOCK_FLAG_RESOURCE   = 1u << 4,
    SDK_BLOCK_FLAG_NATURAL    = 1u << 5,
    SDK_BLOCK_FLAG_PLACEABLE  = 1u << 6
};

enum {
    SDK_BLOCK_BEHAVIOR_FLUID             = 1u << 0,
    SDK_BLOCK_BEHAVIOR_GRANULAR          = 1u << 1,
    SDK_BLOCK_BEHAVIOR_SUPPORT_SENSITIVE = 1u << 2
};

/* =================================================================
 * BLOCK DEFINITION
 * ================================================================= */
typedef uint32_t BlockColor;

typedef struct {
    BlockType         id;
    const char*       name;
    SdkBlockClass     material_class;
    uint32_t          flags;
    BlockToolPref     tool_pref;
    uint8_t           hardness;
    uint8_t           tunnel_difficulty;
    SdkCollapseClass  collapse_class;
    uint8_t           permeability;
    uint8_t           wet_strength_loss;
    uint8_t           road_support;
    uint8_t           fortification_value;
    ItemType          drop_item;
    uint32_t          face_color[6];
} SdkBlockDef;

/* =================================================================
 * FUNCTIONS
 * ================================================================= */
const SdkBlockDef* sdk_block_get_def(BlockType type);
const char* sdk_block_get_name(BlockType type);
const char* sdk_block_get_texture_asset(BlockType type, int face);
BlockColor sdk_block_get_color(BlockType type);
BlockColor sdk_block_get_face_color(BlockType type, int face);
int sdk_block_get_hardness(BlockType type);
int sdk_block_is_solid(BlockType type);
int sdk_block_is_full(BlockType type);
int sdk_block_is_opaque(BlockType type);
BlockToolPref sdk_block_get_tool_pref(BlockType type);
ItemType sdk_block_get_drop_item(BlockType type);
int sdk_block_is_color(BlockType type);
SdkRuntimeMaterialClass sdk_block_get_runtime_material(BlockType type);
uint32_t sdk_block_get_behavior_flags(BlockType type);
uint8_t sdk_block_get_settling_rate(BlockType type);
uint8_t sdk_block_get_angle_of_repose(BlockType type);
uint8_t sdk_block_get_fluid_viscosity(BlockType type);
```

---

## Integration Notes

### Block Placement Validation

```c
bool can_place_block(SdkChunk* chunk, int lx, int ly, int lz, BlockType type) {
    // Check if space is empty or replaceable
    BlockType existing = sdk_chunk_get_block(chunk, lx, ly, lz);
    if (existing != BLOCK_AIR && sdk_block_is_solid(existing)) {
        return false;
    }
    
    // Check if block needs support
    if (sdk_block_get_behavior_flags(type) & SDK_BLOCK_BEHAVIOR_SUPPORT_SENSITIVE) {
        BlockType below = sdk_chunk_get_block(chunk, lx, ly - 1, lz);
        if (!sdk_block_is_solid(below)) {
            return false;
        }
    }
    
    return true;
}
```

### Mining/Harvesting

```c
void mine_block(SdkChunk* chunk, int lx, int ly, int lz, BlockTool held_tool) {
    BlockType type = sdk_chunk_get_block(chunk, lx, ly, lz);
    const SdkBlockDef* def = sdk_block_get_def(type);
    
    // Check tool compatibility
    if (def->tool_pref != BLOCK_TOOL_NONE && def->tool_pref != held_tool) {
        // Mining is slower or impossible
        return;
    }
    
    // Give dropped item
    ItemType drop = sdk_block_get_drop_item(type);
    if (drop != ITEM_NONE) {
        give_player_item(drop);
    }
    
    // Remove block
    sdk_chunk_set_block(chunk, lx, ly, lz, BLOCK_AIR);
}
```

### Fluid Detection

```c
bool is_fluid_block(BlockType type) {
    return sdk_block_get_behavior_flags(type) & SDK_BLOCK_BEHAVIOR_FLUID;
}

bool is_granular_block(BlockType type) {
    return sdk_block_get_behavior_flags(type) & SDK_BLOCK_BEHAVIOR_GRANULAR;
}
```

### Rendering Decision

```c
bool should_render_face(BlockType neighbor_type) {
    // Render face if neighbor is:
    // - AIR
    // - Non-opaque fluid
    // - Non-solid
    if (neighbor_type == BLOCK_AIR) return true;
    
    if (sdk_block_get_behavior_flags(neighbor_type) & SDK_BLOCK_BEHAVIOR_FLUID) {
        return !sdk_block_is_opaque(neighbor_type);
    }
    
    return !sdk_block_is_solid(neighbor_type);
}
```

---

## AI Context Hints

### Adding a New Block Type

1. **Add to enum** (sdk_block.h):
   ```c
   typedef enum {
       // ... existing ...
       BLOCK_MY_NEW_BLOCK,
       BLOCK_COUNT
   } BlockType;
   ```

2. **Add definition** (sdk_block.c):
   ```c
   [BLOCK_MY_NEW_BLOCK] = {
       .id = BLOCK_MY_NEW_BLOCK,
       .name = "my_new_block",
       .material_class = SDK_BLOCK_CLASS_CONSTRUCTION,
       .flags = SDK_BLOCK_FLAG_SOLID | SDK_BLOCK_FLAG_OPAQUE | 
                SDK_BLOCK_FLAG_FULL_CUBE | SDK_BLOCK_FLAG_PLACEABLE,
       .tool_pref = BLOCK_TOOL_PICKAXE,
       .hardness = 80,
       .collapse_class = SDK_COLLAPSE_COMPETENT_ROCK,
       .permeability = 0,
       .drop_item = ITEM_MY_NEW_BLOCK,
   },
   ```

3. **Add texture** to asset system

4. **Add item** for drops (if applicable)

### Custom Block Behavior

```c
// Add custom behavior flag
#define SDK_BLOCK_BEHAVIOR_CUSTOM_SPECIAL (1u << 3)

// Use in block definition
.flags = ... | SDK_BLOCK_BEHAVIOR_CUSTOM_SPECIAL

// Check in gameplay code
if (sdk_block_get_behavior_flags(type) & SDK_BLOCK_BEHAVIOR_CUSTOM_SPECIAL) {
    apply_special_effect();
}
```

### Block Category Iteration

```c
// Iterate all blocks of a material class
void for_each_block_in_class(SdkBlockClass cls, void (*callback)(BlockType)) {
    for (int i = 0; i < BLOCK_COUNT; i++) {
        const SdkBlockDef* def = sdk_block_get_def((BlockType)i);
        if (def->material_class == cls) {
            callback((BlockType)i);
        }
    }
}

// Usage: find all construction blocks
for_each_block_in_class(SDK_BLOCK_CLASS_CONSTRUCTION, print_block_name);
```

### Block Property Modification at Runtime

```c
// Create runtime property override system
typedef struct {
    BlockType type;
    uint8_t hardness_override;
    uint8_t flags_override;
    bool active;
} BlockPropertyOverride;

BlockPropertyOverride g_block_overrides[BLOCK_COUNT];

int sdk_block_get_hardness_with_override(BlockType type) {
    if (g_block_overrides[type].active) {
        return g_block_overrides[type].hardness_override;
    }
    return sdk_block_get_hardness(type);
}
```

### Block Validation

```c
bool validate_block_placement(SdkChunkManager* cm, int wx, int wy, int wz, 
                               BlockType type) {
    // Check bounds
    if (wy < 0 || wy >= CHUNK_HEIGHT) return false;
    
    // Get chunk
    int cx = sdk_world_to_chunk_x(wx);
    int cz = sdk_world_to_chunk_z(wz);
    SdkChunk* chunk = sdk_chunk_manager_get_chunk(cm, cx, cz);
    if (!chunk) return false;
    
    // Get local coords
    int lx = sdk_world_to_local_x(wx, cx);
    int lz = sdk_world_to_local_z(wz, cz);
    
    // Check existing block
    BlockType existing = sdk_chunk_get_block(chunk, lx, wy, lz);
    if (existing != BLOCK_AIR) return false;
    
    // Check support for support-sensitive blocks
    if (sdk_block_get_behavior_flags(type) & SDK_BLOCK_BEHAVIOR_SUPPORT_SENSITIVE) {
        BlockType below = (wy > 0) ? sdk_chunk_get_block(chunk, lx, wy - 1, lz) : BLOCK_AIR;
        if (!sdk_block_is_solid(below)) return false;
    }
    
    return true;
}
```

---

## Related Documentation

- `SDK_Chunks.md` - Chunk storage of blocks
- `SDK_Items.md` - Block items (tools, placement)
- `SDK_Simulation.md` - Block behavior in simulation
- `SDK_MeshBuilder.md` - Block face culling
- `SDK_ConstructionSystem.md` - Construction cells as blocks

---

**Source Files:**
- `SDK/Core/World/Blocks/sdk_block.h` (6,134 bytes) - Public API
- `SDK/Core/World/Blocks/sdk_block.c` (48,105 bytes) - Implementation
