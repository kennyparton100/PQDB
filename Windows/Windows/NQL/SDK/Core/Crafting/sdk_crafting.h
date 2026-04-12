/**
 * sdk_crafting.h — Crafting recipe definitions and matching
 *
 * Supports shaped 2x2 and 3x3 recipes. The crafting grid stores ItemType
 * values; recipes are checked against the grid pattern.
 */
#ifndef NQLSDK_CRAFTING_H
#define NQLSDK_CRAFTING_H

#include "../Items/sdk_item.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CRAFT_GRID_MAX 9  /* 3x3 max */

typedef struct {
    ItemType pattern[9]; /* Row-major 3x3 (unused cells = ITEM_NONE) */
    int      width;      /* Pattern width (1-3) */
    int      height;     /* Pattern height (1-3) */
    ItemType result;
    int      result_count;
} CraftRecipe;

/* All recipes */
static const CraftRecipe g_recipes[] = {
    /* === 1x1 recipes (hand-craft) === */
    /* Log → 4 Planks */
    /* === 1x2 recipes === */
    /* 2 Planks → 4 Sticks */
    { { ITEM_BLOCK_PLANKS,
        ITEM_BLOCK_PLANKS }, 1, 2, ITEM_STICK, 4 },

    /* === 2x2 recipes === */
    /* 4 Planks → Crafting Table */
    /* === 3x3 recipes (need crafting table) === */
    /* Furnace: CCC / C_C / CCC */
    { { ITEM_BLOCK_COBBLESTONE, ITEM_BLOCK_COBBLESTONE, ITEM_BLOCK_COBBLESTONE,
        ITEM_BLOCK_COBBLESTONE, ITEM_NONE,              ITEM_BLOCK_COBBLESTONE,
        ITEM_BLOCK_COBBLESTONE, ITEM_BLOCK_COBBLESTONE, ITEM_BLOCK_COBBLESTONE }, 3, 3, ITEM_BLOCK_FURNACE, 1 },

    /* Campfire: LLL / SCS / _S_ */
    { { ITEM_BLOCK_LOG, ITEM_BLOCK_LOG, ITEM_BLOCK_LOG,
        ITEM_STICK,     ITEM_COAL,      ITEM_STICK,
        ITEM_NONE,      ITEM_STICK,     ITEM_NONE }, 3, 3, ITEM_BLOCK_CAMPFIRE, 1 },

    /* Anvil: III / ICI / CCC */
    { { ITEM_IRON_INGOT,        ITEM_IRON_INGOT,        ITEM_IRON_INGOT,
        ITEM_IRON_INGOT,        ITEM_BLOCK_COBBLESTONE, ITEM_IRON_INGOT,
        ITEM_BLOCK_COBBLESTONE, ITEM_BLOCK_COBBLESTONE, ITEM_BLOCK_COBBLESTONE }, 3, 3, ITEM_BLOCK_ANVIL, 1 },

    /* Blacksmithing table: II_ / PP_ / PP_ */
    { { ITEM_IRON_INGOT,  ITEM_IRON_INGOT,  ITEM_NONE,
        ITEM_BLOCK_PLANKS, ITEM_BLOCK_PLANKS, ITEM_NONE,
        ITEM_BLOCK_PLANKS, ITEM_BLOCK_PLANKS, ITEM_NONE }, 3, 3, ITEM_BLOCK_BLACKSMITHING_TABLE, 1 },

    /* Leatherworking table: HH_ / PP_ / PP_ */
    { { ITEM_HIDE,         ITEM_HIDE,         ITEM_NONE,
        ITEM_BLOCK_PLANKS, ITEM_BLOCK_PLANKS, ITEM_NONE,
        ITEM_BLOCK_PLANKS, ITEM_BLOCK_PLANKS, ITEM_NONE }, 3, 3, ITEM_BLOCK_LEATHERWORKING_TABLE, 1 },

    /* Wood Pickaxe: PPP / _S_ / _S_ */
    { { ITEM_BLOCK_PLANKS, ITEM_BLOCK_PLANKS, ITEM_BLOCK_PLANKS,
        ITEM_NONE,         ITEM_STICK,        ITEM_NONE,
        ITEM_NONE,         ITEM_STICK,        ITEM_NONE }, 3, 3, ITEM_WOOD_PICKAXE, 1 },

    /* Wood Axe: PP_ / PS_ / _S_ */
    { { ITEM_BLOCK_PLANKS, ITEM_BLOCK_PLANKS, ITEM_NONE,
        ITEM_BLOCK_PLANKS, ITEM_STICK,        ITEM_NONE,
        ITEM_NONE,         ITEM_STICK,        ITEM_NONE }, 3, 3, ITEM_WOOD_AXE, 1 },

    /* Wood Shovel: _P_ / _S_ / _S_ */
    { { ITEM_NONE,         ITEM_BLOCK_PLANKS, ITEM_NONE,
        ITEM_NONE,         ITEM_STICK,        ITEM_NONE,
        ITEM_NONE,         ITEM_STICK,        ITEM_NONE }, 3, 3, ITEM_WOOD_SHOVEL, 1 },

    /* Wood Sword: _P_ / _P_ / _S_ */
    { { ITEM_NONE,         ITEM_BLOCK_PLANKS, ITEM_NONE,
        ITEM_NONE,         ITEM_BLOCK_PLANKS, ITEM_NONE,
        ITEM_NONE,         ITEM_STICK,        ITEM_NONE }, 3, 3, ITEM_WOOD_SWORD, 1 },

    /* Stone Pickaxe: CCC / _S_ / _S_ */
    { { ITEM_BLOCK_COBBLESTONE, ITEM_BLOCK_COBBLESTONE, ITEM_BLOCK_COBBLESTONE,
        ITEM_NONE,              ITEM_STICK,             ITEM_NONE,
        ITEM_NONE,              ITEM_STICK,             ITEM_NONE }, 3, 3, ITEM_STONE_PICKAXE, 1 },

    /* Stone Axe: CC_ / CS_ / _S_ */
    { { ITEM_BLOCK_COBBLESTONE, ITEM_BLOCK_COBBLESTONE, ITEM_NONE,
        ITEM_BLOCK_COBBLESTONE, ITEM_STICK,             ITEM_NONE,
        ITEM_NONE,              ITEM_STICK,             ITEM_NONE }, 3, 3, ITEM_STONE_AXE, 1 },

    /* Stone Shovel: _C_ / _S_ / _S_ */
    { { ITEM_NONE,              ITEM_BLOCK_COBBLESTONE, ITEM_NONE,
        ITEM_NONE,              ITEM_STICK,             ITEM_NONE,
        ITEM_NONE,              ITEM_STICK,             ITEM_NONE }, 3, 3, ITEM_STONE_SHOVEL, 1 },

    /* Stone Sword: _C_ / _C_ / _S_ */
    { { ITEM_NONE,              ITEM_BLOCK_COBBLESTONE, ITEM_NONE,
        ITEM_NONE,              ITEM_BLOCK_COBBLESTONE, ITEM_NONE,
        ITEM_NONE,              ITEM_STICK,             ITEM_NONE }, 3, 3, ITEM_STONE_SWORD, 1 },

    /* Iron Pickaxe: III / _S_ / _S_ */
    { { ITEM_IRON_INGOT, ITEM_IRON_INGOT, ITEM_IRON_INGOT,
        ITEM_NONE,       ITEM_STICK,      ITEM_NONE,
        ITEM_NONE,       ITEM_STICK,      ITEM_NONE }, 3, 3, ITEM_IRON_PICKAXE, 1 },

    /* Iron Axe: II_ / IS_ / _S_ */
    { { ITEM_IRON_INGOT, ITEM_IRON_INGOT, ITEM_NONE,
        ITEM_IRON_INGOT, ITEM_STICK,      ITEM_NONE,
        ITEM_NONE,       ITEM_STICK,      ITEM_NONE }, 3, 3, ITEM_IRON_AXE, 1 },

    /* Iron Shovel: _I_ / _S_ / _S_ */
    { { ITEM_NONE,       ITEM_IRON_INGOT, ITEM_NONE,
        ITEM_NONE,       ITEM_STICK,      ITEM_NONE,
        ITEM_NONE,       ITEM_STICK,      ITEM_NONE }, 3, 3, ITEM_IRON_SHOVEL, 1 },
};

#define RECIPE_COUNT (int)(sizeof(g_recipes) / sizeof(g_recipes[0]))

/**
 * Check if a grid matches a recipe. Grid is row-major, grid_w x grid_h.
 * Returns recipe index or -1 if no match.
 */
static inline int sdk_crafting_match(const ItemType* grid, int grid_w, int grid_h)
{
    for (int r = 0; r < RECIPE_COUNT; r++) {
        const CraftRecipe* rec = &g_recipes[r];
        if (rec->width > grid_w || rec->height > grid_h) continue;

        /* Try all valid offsets for the pattern within the grid */
        for (int oy = 0; oy <= grid_h - rec->height; oy++) {
            for (int ox = 0; ox <= grid_w - rec->width; ox++) {
                int match = 1;
                /* Check that every grid cell either matches pattern or is empty outside pattern */
                for (int gy = 0; gy < grid_h && match; gy++) {
                    for (int gx = 0; gx < grid_w && match; gx++) {
                        int px = gx - ox, py = gy - oy;
                        ItemType expected = ITEM_NONE;
                        if (px >= 0 && px < rec->width && py >= 0 && py < rec->height) {
                            expected = rec->pattern[py * rec->width + px];
                        }
                        if (grid[gy * grid_w + gx] != expected) match = 0;
                    }
                }
                if (match) return r;
            }
        }
    }
    return -1;
}

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_CRAFTING_H */
