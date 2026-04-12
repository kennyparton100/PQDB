/**
 * sdk_item.h -- Item helpers for tools, blocks, and consumables.
 */
#ifndef NQLSDK_ITEM_H
#define NQLSDK_ITEM_H

#include "../World/Blocks/sdk_block.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int sdk_item_is_block(ItemType type) {
    return type >= ITEM_BLOCK_GRASS && type < ITEM_BLOCK_MAX;
}

static inline BlockType sdk_item_to_block(ItemType type) {
    switch (type) {
        case ITEM_BLOCK_GRASS:                return BLOCK_GRASS;
        case ITEM_BLOCK_DIRT:                 return BLOCK_DIRT;
        case ITEM_BLOCK_STONE:                return BLOCK_STONE;
        case ITEM_BLOCK_SAND:                 return BLOCK_SAND;
        case ITEM_BLOCK_WATER:                return BLOCK_WATER;
        case ITEM_BLOCK_SNOW:                 return BLOCK_SNOW;
        case ITEM_BLOCK_GRAVEL:               return BLOCK_GRAVEL;
        case ITEM_BLOCK_BEDROCK:              return BLOCK_BEDROCK;
        case ITEM_BLOCK_LOG:                  return BLOCK_LOG;
        case ITEM_BLOCK_LEAVES:               return BLOCK_LEAVES;
        case ITEM_BLOCK_PLANKS:               return BLOCK_PLANKS;
        case ITEM_BLOCK_COBBLESTONE:          return BLOCK_COBBLESTONE;
        case ITEM_BLOCK_CRAFTING_TABLE:       return BLOCK_CRAFTING_TABLE;
        case ITEM_BLOCK_FURNACE:              return BLOCK_FURNACE;
        case ITEM_BLOCK_CAMPFIRE:             return BLOCK_CAMPFIRE;
        case ITEM_BLOCK_ANVIL:                return BLOCK_ANVIL;
        case ITEM_BLOCK_BLACKSMITHING_TABLE:  return BLOCK_BLACKSMITHING_TABLE;
        case ITEM_BLOCK_LEATHERWORKING_TABLE: return BLOCK_LEATHERWORKING_TABLE;
        case ITEM_BLOCK_BERRY_BUSH:           return BLOCK_BERRY_BUSH;
        case ITEM_BLOCK_REEDS:                return BLOCK_REEDS;
        case ITEM_BLOCK_COAL_ORE:             return BLOCK_COAL_ORE;
        case ITEM_BLOCK_IRON_ORE:             return BLOCK_IRON_ORE;
        case ITEM_BLOCK_CRUSHED_STONE:        return BLOCK_CRUSHED_STONE;
        case ITEM_BLOCK_COMPACTED_FILL:       return BLOCK_COMPACTED_FILL;
        case ITEM_BLOCK_SANDBAGS:             return BLOCK_SANDBAGS;
        case ITEM_BLOCK_BRICK:                return BLOCK_BRICK;
        case ITEM_BLOCK_STONE_BRICKS:         return BLOCK_STONE_BRICKS;
        case ITEM_BLOCK_CONCRETE:             return BLOCK_CONCRETE;
        case ITEM_BLOCK_REINFORCED_CONCRETE:  return BLOCK_REINFORCED_CONCRETE;
        case ITEM_BLOCK_TIMBER_LIGHT:         return BLOCK_TIMBER_LIGHT;
        case ITEM_BLOCK_TIMBER_DARK:          return BLOCK_TIMBER_DARK;
        case ITEM_BLOCK_TIMBER_WEATHERED:     return BLOCK_TIMBER_WEATHERED;
        case ITEM_BLOCK_THATCH:               return BLOCK_THATCH;
        case ITEM_BLOCK_WOOD_SHINGLES:        return BLOCK_WOOD_SHINGLES;
        case ITEM_BLOCK_WATTLE_DAUB:          return BLOCK_WATTLE_DAUB;
        case ITEM_BLOCK_ADOBE:                return BLOCK_ADOBE;
        case ITEM_BLOCK_MUDBRICK:             return BLOCK_MUDBRICK;
        case ITEM_BLOCK_PLASTER_WHITE:        return BLOCK_PLASTER_WHITE;
        case ITEM_BLOCK_PLASTER_OCHRE:        return BLOCK_PLASTER_OCHRE;
        case ITEM_BLOCK_CUT_STONE_LIGHT:      return BLOCK_CUT_STONE_LIGHT;
        case ITEM_BLOCK_CUT_STONE_DARK:       return BLOCK_CUT_STONE_DARK;
        case ITEM_BLOCK_RUBBLE_MASONRY:       return BLOCK_RUBBLE_MASONRY;
        case ITEM_BLOCK_FLAGSTONE:            return BLOCK_FLAGSTONE;
        case ITEM_BLOCK_CLAY_ROOF_TILE:       return BLOCK_CLAY_ROOF_TILE;
        case ITEM_BLOCK_SLATE_ROOF:           return BLOCK_SLATE_ROOF;
        case ITEM_BLOCK_CORRUGATED_METAL:     return BLOCK_CORRUGATED_METAL;
        case ITEM_BLOCK_WROUGHT_IRON:         return BLOCK_WROUGHT_IRON;
        case ITEM_BLOCK_GLASS:                return BLOCK_GLASS;
        case ITEM_BLOCK_ROPE_NETTING:         return BLOCK_ROPE_NETTING;
        default: return BLOCK_AIR;
    }
}

static inline ItemType sdk_block_to_item(BlockType type) {
    return sdk_block_get_drop_item(type);
}

static inline int sdk_item_is_tool(ItemType type) {
    return (type >= ITEM_WOOD_PICKAXE && type <= ITEM_STONE_SWORD) ||
           (type >= ITEM_IRON_PICKAXE && type <= ITEM_IRON_SHOVEL) ||
           type == ITEM_IRON_SAW ||
           type == ITEM_IRON_CHISEL;
}

static inline int sdk_item_is_firearm(ItemType type) {
    return type == ITEM_PISTOL ||
           type == ITEM_ASSAULT_RIFLE ||
           type == ITEM_SNIPER_RIFLE;
}

static inline int sdk_item_is_throwable(ItemType type) {
    return type == ITEM_HAND_GRANADE ||
           type == ITEM_SEMTEX ||
           type == ITEM_TACTICAL_GRANADE ||
           type == ITEM_SMOKE_GRANADE;
}

static inline int sdk_item_is_combat_utility(ItemType type) {
    return sdk_item_is_firearm(type) || sdk_item_is_throwable(type);
}

static inline ToolClass sdk_item_get_tool_class(ItemType type) {
    switch (type) {
        case ITEM_WOOD_PICKAXE:
        case ITEM_STONE_PICKAXE:
        case ITEM_IRON_PICKAXE:
            return TOOL_PICKAXE;
        case ITEM_WOOD_AXE:
        case ITEM_STONE_AXE:
        case ITEM_IRON_AXE:
            return TOOL_AXE;
        case ITEM_WOOD_SHOVEL:
        case ITEM_STONE_SHOVEL:
        case ITEM_IRON_SHOVEL:
            return TOOL_SHOVEL;
        case ITEM_WOOD_SWORD:
        case ITEM_STONE_SWORD:
            return TOOL_SWORD;
        case ITEM_IRON_SAW:
            return TOOL_SAW;
        case ITEM_IRON_CHISEL:
            return TOOL_CHISEL;
        default:
            return TOOL_NONE;
    }
}

static inline ToolTier sdk_item_get_tool_tier(ItemType type) {
    if (type == ITEM_IRON_SAW || type == ITEM_IRON_CHISEL) return TIER_IRON;
    if (type >= ITEM_IRON_PICKAXE && type <= ITEM_IRON_SHOVEL) return TIER_IRON;
    if (type >= ITEM_STONE_PICKAXE && type <= ITEM_STONE_SWORD) return TIER_STONE;
    if (type >= ITEM_WOOD_PICKAXE && type <= ITEM_WOOD_SWORD) return TIER_WOOD;
    return TIER_HAND;
}

static inline int sdk_item_get_durability(ItemType type) {
    switch (sdk_item_get_tool_tier(type)) {
        case TIER_WOOD:  return 60;
        case TIER_STONE: return 132;
        case TIER_IRON:  return 280;
        default: return 0;
    }
}

static inline float sdk_item_get_speed(ItemType type) {
    switch (sdk_item_get_tool_tier(type)) {
        case TIER_WOOD:  return 2.0f;
        case TIER_STONE: return 4.0f;
        case TIER_IRON:  return 6.0f;
        default: return 1.0f;
    }
}

static inline int sdk_item_get_attack(ItemType type) {
    switch (type) {
        case ITEM_WOOD_SWORD:  return 4;
        case ITEM_STONE_SWORD: return 5;
        case ITEM_PISTOL: return 2;
        case ITEM_ASSAULT_RIFLE: return 3;
        case ITEM_SNIPER_RIFLE: return 4;
        default: return 1;
    }
}

static inline int sdk_tool_matches_block(ToolClass tool, BlockToolPref pref) {
    if (pref == BLOCK_TOOL_NONE) return 1;
    if (pref == BLOCK_TOOL_PICKAXE && tool == TOOL_PICKAXE) return 1;
    if (pref == BLOCK_TOOL_AXE && tool == TOOL_AXE) return 1;
    if (pref == BLOCK_TOOL_SHOVEL && tool == TOOL_SHOVEL) return 1;
    return 0;
}

static inline int sdk_item_is_food(ItemType type) {
    return type == ITEM_RAW_MEAT || type == ITEM_COOKED_MEAT || type == ITEM_BERRIES;
}

static inline int sdk_item_is_spawn_item(ItemType type) {
    return type >= ITEM_SPAWNER_BUILDER && type <= ITEM_SPAWNER_TANK;
}

static inline int sdk_item_get_nutrition(ItemType type) {
    switch (type) {
        case ITEM_RAW_MEAT:    return 3;
        case ITEM_COOKED_MEAT: return 8;
        case ITEM_BERRIES:     return 2;
        default: return 0;
    }
}

static inline const char* sdk_item_get_name(ItemType type) {
    if (sdk_item_is_block(type)) {
        BlockType bt = sdk_item_to_block(type);
        return (bt != BLOCK_AIR) ? sdk_block_get_name(bt) : "unknown";
    }

    switch (type) {
        case ITEM_WOOD_PICKAXE: return "wood_pickaxe";
        case ITEM_WOOD_AXE: return "wood_axe";
        case ITEM_WOOD_SHOVEL: return "wood_shovel";
        case ITEM_WOOD_SWORD: return "wood_sword";
        case ITEM_STONE_PICKAXE: return "stone_pickaxe";
        case ITEM_STONE_AXE: return "stone_axe";
        case ITEM_STONE_SHOVEL: return "stone_shovel";
        case ITEM_STONE_SWORD: return "stone_sword";
        case ITEM_IRON_PICKAXE: return "iron_pickaxe";
        case ITEM_IRON_AXE: return "iron_axe";
        case ITEM_IRON_SHOVEL: return "iron_shovel";
        case ITEM_IRON_SAW: return "iron_saw";
        case ITEM_IRON_CHISEL: return "iron_chisel";
        case ITEM_STICK: return "stick";
        case ITEM_COAL: return "coal";
        case ITEM_IRON_INGOT: return "iron_ingot";
        case ITEM_IRONSTONE: return "ironstone";
        case ITEM_CLAY: return "clay";
        case ITEM_LIMESTONE: return "limestone";
        case ITEM_AGGREGATE: return "aggregate";
        case ITEM_COPPER_ORE: return "copper_ore";
        case ITEM_SULFUR: return "sulfur";
        case ITEM_TUNGSTEN_ORE: return "tungsten_ore";
        case ITEM_BAUXITE: return "bauxite";
        case ITEM_LEAD_ZINC_ORE: return "lead_zinc_ore";
        case ITEM_SALT: return "salt";
        case ITEM_HIDE: return "hide";
        case ITEM_RAW_MEAT: return "raw_meat";
        case ITEM_COOKED_MEAT: return "cooked_meat";
        case ITEM_BERRIES: return "berries";
        case ITEM_PISTOL: return "pistol";
        case ITEM_ASSAULT_RIFLE: return "assault_rifle";
        case ITEM_SNIPER_RIFLE: return "sniper_rifle";
        case ITEM_HAND_GRANADE: return "hand_granade";
        case ITEM_SEMTEX: return "semtex";
        case ITEM_TACTICAL_GRANADE: return "tactical_granade";
        case ITEM_SMOKE_GRANADE: return "smoke_granade";
        case ITEM_SPAWNER_BUILDER: return "builder_spawner";
        case ITEM_SPAWNER_BLACKSMITH: return "blacksmith_spawner";
        case ITEM_SPAWNER_MINER: return "miner_spawner";
        case ITEM_SPAWNER_SOLDIER: return "soldier_spawner";
        case ITEM_SPAWNER_GENERAL: return "general_spawner";
        case ITEM_SPAWNER_CAR: return "car_spawner";
        case ITEM_SPAWNER_MOTORBIKE: return "motorbike_spawner";
        case ITEM_SPAWNER_TANK: return "tank_spawner";
        default: return "unknown";
    }
}

static inline uint32_t sdk_item_get_color(ItemType type) {
    if (sdk_item_is_block(type)) {
        BlockType bt = sdk_item_to_block(type);
        if (bt != BLOCK_AIR) return sdk_block_get_face_color(bt, 3);
    }

    switch (type) {
        case ITEM_WOOD_PICKAXE:
        case ITEM_WOOD_AXE:
        case ITEM_WOOD_SHOVEL:
        case ITEM_WOOD_SWORD:
            return 0xFF8A6A48u;
        case ITEM_STONE_PICKAXE:
        case ITEM_STONE_AXE:
        case ITEM_STONE_SHOVEL:
        case ITEM_STONE_SWORD:
            return 0xFF9A9A9Au;
        case ITEM_IRON_PICKAXE:
        case ITEM_IRON_AXE:
        case ITEM_IRON_SHOVEL:
        case ITEM_IRON_SAW:
        case ITEM_IRON_CHISEL:
            return 0xFFC8CDD4u;
        case ITEM_STICK:
            return 0xFF77553Au;
        case ITEM_COAL:
            return 0xFF242424u;
        case ITEM_IRON_INGOT:
            return 0xFFD0D0D0u;
        case ITEM_IRONSTONE:
            return 0xFF8C634Eu;
        case ITEM_CLAY:
            return 0xFF8A7468u;
        case ITEM_LIMESTONE:
            return 0xFFD6D0C2u;
        case ITEM_AGGREGATE:
            return 0xFF8C8C8Cu;
        case ITEM_COPPER_ORE:
            return 0xFF7C6A74u;
        case ITEM_SULFUR:
            return 0xFF4AB4D6u;
        case ITEM_TUNGSTEN_ORE:
            return 0xFF5C5650u;
        case ITEM_BAUXITE:
            return 0xFF6E7EB4u;
        case ITEM_LEAD_ZINC_ORE:
            return 0xFF806A84u;
        case ITEM_SALT:
            return 0xFFF0F0E6u;
        case ITEM_HIDE:
            return 0xFF70543Au;
        case ITEM_RAW_MEAT:
            return 0xFF5A58C4u;
        case ITEM_COOKED_MEAT:
            return 0xFF4A6E98u;
        case ITEM_BERRIES:
            return 0xFF6C2CB0u;
        case ITEM_PISTOL:
            return 0xFF505860u;
        case ITEM_ASSAULT_RIFLE:
            return 0xFF3C463Eu;
        case ITEM_SNIPER_RIFLE:
            return 0xFF2C3C44u;
        case ITEM_HAND_GRANADE:
            return 0xFF4A5C2Eu;
        case ITEM_SEMTEX:
            return 0xFF505050u;
        case ITEM_TACTICAL_GRANADE:
            return 0xFFDCE4EAu;
        case ITEM_SMOKE_GRANADE:
            return 0xFF767E86u;
        case ITEM_SPAWNER_BUILDER:
            return 0xFF58D6F4u;
        case ITEM_SPAWNER_BLACKSMITH:
            return 0xFF3A77CCu;
        case ITEM_SPAWNER_MINER:
            return 0xFF48D0F2u;
        case ITEM_SPAWNER_SOLDIER:
            return 0xFF244424u;
        case ITEM_SPAWNER_GENERAL:
            return 0xFF40C8F0u;
        case ITEM_SPAWNER_CAR:
            return 0xFF2C5CE0u;
        case ITEM_SPAWNER_MOTORBIKE:
            return 0xFF14A0F0u;
        case ITEM_SPAWNER_TANK:
            return 0xFF486848u;
        default:
            return 0xFF808080u;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_ITEM_H */
