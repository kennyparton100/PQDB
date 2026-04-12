/**
 * sdk_block.h -- Block taxonomy and material properties.
 */
#ifndef NQLSDK_BLOCK_H
#define NQLSDK_BLOCK_H

#include "../../sdk_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLOCK_AIR = 0,
    BLOCK_WATER,
    BLOCK_ICE,
    BLOCK_SNOW,
    BLOCK_BEDROCK,

    /* Legacy gameplay terrain */
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_SAND,
    BLOCK_GRAVEL,

    /* Organic / wood */
    BLOCK_LOG,
    BLOCK_LEAVES,
    BLOCK_PLANKS,

    /* Legacy crafted / ore */
    BLOCK_COBBLESTONE,
    BLOCK_CRAFTING_TABLE,
    BLOCK_FURNACE,
    BLOCK_CAMPFIRE,
    BLOCK_ANVIL,
    BLOCK_BLACKSMITHING_TABLE,
    BLOCK_LEATHERWORKING_TABLE,
    BLOCK_BERRY_BUSH,
    BLOCK_REEDS,
    BLOCK_COAL_ORE,
    BLOCK_IRON_ORE,

    /* Bedrock provinces */
    BLOCK_GRANITE,
    BLOCK_GNEISS,
    BLOCK_SCHIST,
    BLOCK_BASALT,
    BLOCK_VOLCANIC_ROCK,
    BLOCK_SANDSTONE,
    BLOCK_SHALE,
    BLOCK_LIMESTONE,
    BLOCK_DOLOSTONE,
    BLOCK_MUDSTONE,
    BLOCK_SILTSTONE,
    BLOCK_CONGLOMERATE,
    BLOCK_MARL,
    BLOCK_CHALK,
    BLOCK_ANDESITE,
    BLOCK_TUFF,
    BLOCK_SCORIA,

    /* Sediments / soils */
    BLOCK_TOPSOIL,
    BLOCK_SUBSOIL,
    BLOCK_SILT,
    BLOCK_CLAY,
    BLOCK_COARSE_ALLUVIUM,
    BLOCK_FINE_ALLUVIUM,
    BLOCK_COLLUVIUM,
    BLOCK_TALUS,
    BLOCK_PEAT,
    BLOCK_TURF,
    BLOCK_WETLAND_SOD,
    BLOCK_MARINE_SAND,
    BLOCK_MARINE_MUD,
    BLOCK_BEACH_GRAVEL,
    BLOCK_TIDAL_SILT,
    BLOCK_LOESS,
    BLOCK_CALCAREOUS_SOIL,
    BLOCK_SEA_ICE,

    /* Resources */
    BLOCK_COAL_SEAM,
    BLOCK_IRONSTONE,
    BLOCK_CLAY_DEPOSIT,
    BLOCK_COPPER_BEARING_ROCK,
    BLOCK_SULFUR_BEARING_VOLCANIC_ROCK,
    BLOCK_TUNGSTEN_SKARN,
    BLOCK_BAUXITE,
    BLOCK_LEAD_ZINC_VEIN_ROCK,
    BLOCK_SALT_EVAPORITE,
    BLOCK_BRECCIA,
    BLOCK_SAPROLITE,
    BLOCK_VEIN_QUARTZ,
    BLOCK_OIL_SHALE,
    BLOCK_OIL_SAND,
    BLOCK_CRUDE_BEARING_SANDSTONE,
    BLOCK_GAS_BEARING_SANDSTONE,
    BLOCK_RARE_EARTH_ORE,
    BLOCK_URANIUM_ORE,
    BLOCK_SALTPETER_BEARING_ROCK,
    BLOCK_PHOSPHATE_ROCK,
    BLOCK_CHROMITE,
    BLOCK_ALUMINUM_ORE,

    /* Human-made bulk materials */
    BLOCK_CRUSHED_STONE,
    BLOCK_COMPACTED_FILL,
    BLOCK_SANDBAGS,
    BLOCK_BRICK,
    BLOCK_STONE_BRICKS,
    BLOCK_CONCRETE,
    BLOCK_REINFORCED_CONCRETE,
    BLOCK_LAVA,
    BLOCK_TALL_GRASS,
    BLOCK_DRY_GRASS,
    BLOCK_FERN,
    BLOCK_SHRUB,
    BLOCK_HEATHER,
    BLOCK_CATTAILS,
    BLOCK_WILDFLOWERS,
    BLOCK_CACTUS,
    BLOCK_SUCCULENT,
    BLOCK_MOSS,

    /* Authoring-first construction materials */
    BLOCK_TIMBER_LIGHT,
    BLOCK_TIMBER_DARK,
    BLOCK_TIMBER_WEATHERED,
    BLOCK_THATCH,
    BLOCK_WOOD_SHINGLES,
    BLOCK_WATTLE_DAUB,
    BLOCK_ADOBE,
    BLOCK_MUDBRICK,
    BLOCK_PLASTER_WHITE,
    BLOCK_PLASTER_OCHRE,
    BLOCK_CUT_STONE_LIGHT,
    BLOCK_CUT_STONE_DARK,
    BLOCK_RUBBLE_MASONRY,
    BLOCK_FLAGSTONE,
    BLOCK_CLAY_ROOF_TILE,
    BLOCK_SLATE_ROOF,
    BLOCK_CORRUGATED_METAL,
    BLOCK_WROUGHT_IRON,
    BLOCK_GLASS,
    BLOCK_ROPE_NETTING,

    /* Character builder colors */
    BLOCK_COLOR_WHITE,
    BLOCK_COLOR_BLACK,
    BLOCK_COLOR_RED,
    BLOCK_COLOR_ORANGE,
    BLOCK_COLOR_YELLOW,
    BLOCK_COLOR_GREEN,
    BLOCK_COLOR_CYAN,
    BLOCK_COLOR_BLUE,
    BLOCK_COLOR_PURPLE,
    BLOCK_COLOR_PINK,
    BLOCK_COLOR_BROWN,
    BLOCK_COLOR_GRAY,

    BLOCK_COUNT
} BlockType;

typedef uint32_t BlockColor;

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

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_BLOCK_H */
