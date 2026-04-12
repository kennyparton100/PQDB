/**
 * sdk_types.h — NQL SDK shared type definitions
 *
 * Core structs, enums, and typedefs used across the SDK.
 * Pure C header — no C++ dependencies.
 */
#ifndef NQLSDK_TYPES_H
#define NQLSDK_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * ERROR / RESULT CODES
 * ====================================================================== */

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

/* ======================================================================
 * MATH TYPES
 * ====================================================================== */

typedef struct SdkVec2 {
    float x, y;
} SdkVec2;

typedef struct SdkVec3 {
    float x, y, z;
} SdkVec3;

typedef struct SdkVec4 {
    float x, y, z, w;
} SdkVec4;

/** 4x4 matrix (column-major for HLSL compatibility) */
typedef struct SdkMat4 {
    float m[4][4];  /* m[col][row] - column-major */
} SdkMat4;

/* Math constants */
#define SDK_PI 3.14159265358979f
#define SDK_DEG2RAD (SDK_PI / 180.0f)
#define SDK_RAD2DEG (180.0f / SDK_PI)

typedef struct SdkColor4 {
    float r, g, b, a;
} SdkColor4;

/* ======================================================================
 * VERTEX FORMAT
 * ====================================================================== */

/** Per-vertex data for Milestone 1 (position + color). */
typedef struct SdkVertex {
    SdkVec3   position;
    SdkColor4 color;
} SdkVertex;

/* ======================================================================
 * ITEM / TOOL TYPES
 * ====================================================================== */

typedef enum ItemType {
    ITEM_NONE = 0,

    /* Legacy/general block items */
    ITEM_BLOCK_GRASS = 1,
    ITEM_BLOCK_DIRT,
    ITEM_BLOCK_STONE,
    ITEM_BLOCK_SAND,
    ITEM_BLOCK_WATER,
    ITEM_BLOCK_SNOW,
    ITEM_BLOCK_GRAVEL,
    ITEM_BLOCK_BEDROCK,
    ITEM_BLOCK_LOG,
    ITEM_BLOCK_LEAVES,
    ITEM_BLOCK_PLANKS,
    ITEM_BLOCK_COBBLESTONE,
    ITEM_BLOCK_CRAFTING_TABLE,
    ITEM_BLOCK_FURNACE,
    ITEM_BLOCK_CAMPFIRE,
    ITEM_BLOCK_ANVIL,
    ITEM_BLOCK_BLACKSMITHING_TABLE,
    ITEM_BLOCK_LEATHERWORKING_TABLE,
    ITEM_BLOCK_BERRY_BUSH,
    ITEM_BLOCK_REEDS,
    ITEM_BLOCK_COAL_ORE,
    ITEM_BLOCK_IRON_ORE,

    /* Engineering materials */
    ITEM_BLOCK_CRUSHED_STONE,
    ITEM_BLOCK_COMPACTED_FILL,
    ITEM_BLOCK_SANDBAGS,
    ITEM_BLOCK_BRICK,
    ITEM_BLOCK_STONE_BRICKS,
    ITEM_BLOCK_CONCRETE,
    ITEM_BLOCK_REINFORCED_CONCRETE,
    ITEM_BLOCK_TIMBER_LIGHT,
    ITEM_BLOCK_TIMBER_DARK,
    ITEM_BLOCK_TIMBER_WEATHERED,
    ITEM_BLOCK_THATCH,
    ITEM_BLOCK_WOOD_SHINGLES,
    ITEM_BLOCK_WATTLE_DAUB,
    ITEM_BLOCK_ADOBE,
    ITEM_BLOCK_MUDBRICK,
    ITEM_BLOCK_PLASTER_WHITE,
    ITEM_BLOCK_PLASTER_OCHRE,
    ITEM_BLOCK_CUT_STONE_LIGHT,
    ITEM_BLOCK_CUT_STONE_DARK,
    ITEM_BLOCK_RUBBLE_MASONRY,
    ITEM_BLOCK_FLAGSTONE,
    ITEM_BLOCK_CLAY_ROOF_TILE,
    ITEM_BLOCK_SLATE_ROOF,
    ITEM_BLOCK_CORRUGATED_METAL,
    ITEM_BLOCK_WROUGHT_IRON,
    ITEM_BLOCK_GLASS,
    ITEM_BLOCK_ROPE_NETTING,

    ITEM_BLOCK_MAX,

    /* Tool items (start at 64) */
    ITEM_WOOD_PICKAXE = 64,
    ITEM_WOOD_AXE,
    ITEM_WOOD_SHOVEL,
    ITEM_WOOD_SWORD,
    ITEM_STONE_PICKAXE,
    ITEM_STONE_AXE,
    ITEM_STONE_SHOVEL,
    ITEM_STONE_SWORD,
    ITEM_IRON_PICKAXE,
    ITEM_IRON_AXE,
    ITEM_IRON_SHOVEL,
    ITEM_IRON_SAW = 75,
    ITEM_IRON_CHISEL = 76,

    /* Crafting / strategic materials */
    ITEM_STICK = 128,
    ITEM_COAL,
    ITEM_IRON_INGOT,
    ITEM_CLAY,
    ITEM_LIMESTONE,
    ITEM_AGGREGATE,
    ITEM_COPPER_ORE,
    ITEM_SULFUR,
    ITEM_TUNGSTEN_ORE,
    ITEM_BAUXITE,
    ITEM_LEAD_ZINC_ORE,
    ITEM_SALT,
    ITEM_HIDE,
    ITEM_IRONSTONE,

    /* Food items */
    ITEM_RAW_MEAT = 192,
    ITEM_COOKED_MEAT,
    ITEM_BERRIES,
    ITEM_PISTOL,
    ITEM_ASSAULT_RIFLE,
    ITEM_SNIPER_RIFLE,
    ITEM_HAND_GRANADE,
    ITEM_SEMTEX,
    ITEM_TACTICAL_GRANADE,
    ITEM_SMOKE_GRANADE,

    /* Creative-only entity spawners */
    ITEM_SPAWNER_BUILDER = 208,
    ITEM_SPAWNER_BLACKSMITH,
    ITEM_SPAWNER_MINER,
    ITEM_SPAWNER_SOLDIER,
    ITEM_SPAWNER_GENERAL,
    ITEM_SPAWNER_CAR,
    ITEM_SPAWNER_MOTORBIKE,
    ITEM_SPAWNER_TANK,

    ITEM_TYPE_COUNT = 256
} ItemType;

typedef uint16_t SdkWorldCellCode;
typedef uint32_t SdkConstructionArchetypeId;

typedef enum SdkWorldCellKind {
    SDK_WORLD_CELL_KIND_FULL_BLOCK = 0,
    SDK_WORLD_CELL_KIND_INLINE_CONSTRUCTION = 1,
    SDK_WORLD_CELL_KIND_OVERFLOW_CONSTRUCTION = 2
} SdkWorldCellKind;

typedef enum SdkInlineConstructionProfile {
    SDK_INLINE_PROFILE_NONE = 0,

    SDK_INLINE_PROFILE_HALF_NEG_X,
    SDK_INLINE_PROFILE_HALF_POS_X,
    SDK_INLINE_PROFILE_HALF_NEG_Y,
    SDK_INLINE_PROFILE_HALF_POS_Y,
    SDK_INLINE_PROFILE_HALF_NEG_Z,
    SDK_INLINE_PROFILE_HALF_POS_Z,

    SDK_INLINE_PROFILE_QUARTER_NEG_X,
    SDK_INLINE_PROFILE_QUARTER_POS_X,
    SDK_INLINE_PROFILE_QUARTER_NEG_Y,
    SDK_INLINE_PROFILE_QUARTER_POS_Y,
    SDK_INLINE_PROFILE_QUARTER_NEG_Z,
    SDK_INLINE_PROFILE_QUARTER_POS_Z,

    SDK_INLINE_PROFILE_BEAM_X,
    SDK_INLINE_PROFILE_BEAM_Y,
    SDK_INLINE_PROFILE_BEAM_Z,

    SDK_INLINE_PROFILE_STRIP_X,
    SDK_INLINE_PROFILE_STRIP_Y,
    SDK_INLINE_PROFILE_STRIP_Z,

    SDK_INLINE_PROFILE_COUNT
} SdkInlineConstructionProfile;

enum {
    SDK_CONSTRUCTION_CELL_RESOLUTION = 16u,
    SDK_CONSTRUCTION_CELL_VOXELS = SDK_CONSTRUCTION_CELL_RESOLUTION *
                                   SDK_CONSTRUCTION_CELL_RESOLUTION *
                                   SDK_CONSTRUCTION_CELL_RESOLUTION,
    SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT = SDK_CONSTRUCTION_CELL_VOXELS / 64u,
    SDK_PERSISTENCE_SHAPED_ITEM_B64_MAX = 689,
    SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE = 32u,
    SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID = 0u
};

enum {
    SDK_WORLD_CELL_INLINE_BASE = 0x8000u,
    SDK_WORLD_CELL_OVERFLOW_CODE = 0xFFFFu,
    SDK_WORLD_CELL_INLINE_PROFILE_SHIFT = 8u,
    SDK_WORLD_CELL_INLINE_MATERIAL_MASK = 0x00FFu,
    SDK_WORLD_CELL_INLINE_PROFILE_MASK = 0x7F00u
};

typedef enum {
    TOOL_NONE = 0,
    TOOL_PICKAXE,
    TOOL_AXE,
    TOOL_SHOVEL,
    TOOL_SWORD,
    TOOL_SAW,
    TOOL_CHISEL,
} ToolClass;

typedef enum {
    TIER_HAND = 0,
    TIER_WOOD,
    TIER_STONE,
    TIER_IRON,
} ToolTier;

typedef enum {
    BLOCK_TOOL_NONE = 0,
    BLOCK_TOOL_PICKAXE,
    BLOCK_TOOL_AXE,
    BLOCK_TOOL_SHOVEL,
} BlockToolPref;

typedef enum SdkItemPayloadKind {
    SDK_ITEM_PAYLOAD_NONE = 0,
    SDK_ITEM_PAYLOAD_SHAPED_CONSTRUCTION = 1
} SdkItemPayloadKind;

typedef enum SdkConstructionItemIdentityKind {
    SDK_CONSTRUCTION_ITEM_IDENTITY_NONE = 0,
    SDK_CONSTRUCTION_ITEM_IDENTITY_UNORDERED_BOX = 1
} SdkConstructionItemIdentityKind;

typedef struct SdkConstructionItemPayload {
    uint16_t material; /* BlockType */
    uint8_t  inline_profile_hint;
    uint8_t  item_identity_kind;
    uint16_t occupied_count;
    uint16_t unordered_box_dims_packed;
    uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
} SdkConstructionItemPayload;

typedef struct SdkPickupItem {
    ItemType item;
    uint16_t display_block_type; /* BlockType */
    uint8_t payload_kind;
    uint8_t reserved0;
    int count;
    int durability;
    SdkConstructionItemPayload shaped;
} SdkPickupItem;

/* ======================================================================
 * WINDOW DESCRIPTOR
 * ====================================================================== */

typedef struct SdkWindowDesc {
    const char* title;          /**< Window title (UTF-8). NULL → "NQL SDK" */
    uint32_t    width;          /**< Client area width.  0 → 800 */
    uint32_t    height;         /**< Client area height. 0 → 600 */
    bool        resizable;      /**< Allow user resize. Default true */
} SdkWindowDesc;

/* ======================================================================
 * WORLD DESCRIPTOR
 * ====================================================================== */

typedef enum SdkWorldCoordinateSystem {
    SDK_WORLD_COORDSYS_CHUNK_SYSTEM = 0,
    SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM = 1,
    SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM = 2
} SdkWorldCoordinateSystem;

typedef struct SdkWorldDesc {
    uint32_t seed;              /**< 0 → default deterministic seed */
    int16_t  sea_level;         /**< 0 → default sea level chosen by worldgen */
    uint16_t macro_cell_size;   /**< 0 → default 32 block macro cell */
    uint8_t coordinate_system;
    bool settlements_enabled;
    bool walls_enabled;
    bool construction_cells_enabled;
} SdkWorldDesc;

/* ======================================================================
 * INIT DESCRIPTOR
 * ====================================================================== */

typedef struct SdkInitDesc {
    SdkWindowDesc window;       /**< Window parameters */
    SdkColor4     clear_color;  /**< Background clear colour. {0,0,0,0} → dark grey */
    bool          enable_debug; /**< Enable D3D12 debug layer */
    bool          vsync;        /**< Present with vsync. Default true */
    SdkWorldDesc  world;        /**< World generation descriptor */
} SdkInitDesc;

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_TYPES_H */
