/**
 * sdk_role_assets.c -- Resolve runtime role and building bindings to authored assets.
 */
#include "sdk_role_assets.h"
#include "../API/Internal/sdk_api_internal.h"
#include "../World/Buildings/sdk_building_family.h"

#include <string.h>

typedef struct {
    SdkNpcRole role;
    const char* asset_id;
} SdkRoleAssetEntry;

static const SdkRoleAssetEntry g_role_asset_bindings[] = {
    { SDK_NPC_ROLE_COMMONER,   "settler_commoner" },
    { SDK_NPC_ROLE_BUILDER,    "builder" },
    { SDK_NPC_ROLE_BLACKSMITH, "blacksmith" },
    { SDK_NPC_ROLE_MINER,      "miner" },
    { SDK_NPC_ROLE_FOREMAN,    "foreman" },
    { SDK_NPC_ROLE_SOLDIER,    "soldier_placeholder" }
};

static int find_character_asset(const char* asset_id)
{
    /* Finds character asset index by ID, returns -1 if not found */
    int i;
    if (!asset_id || !asset_id[0]) return -1;
    refresh_character_assets();
    for (i = 0; i < g_character_asset_count; ++i) {
        if (_stricmp(g_character_assets[i].asset_id, asset_id) == 0) return i;
    }
    return -1;
}

static int find_prop_asset(const char* asset_id)
{
    /* Finds prop asset index by ID, returns -1 if not found */
    int i;
    if (!asset_id || !asset_id[0]) return -1;
    refresh_prop_assets();
    for (i = 0; i < g_prop_asset_count; ++i) {
        if (_stricmp(g_prop_assets[i].asset_id, asset_id) == 0) return i;
    }
    return -1;
}

const char* sdk_role_assets_resolve_character(SdkNpcRole role, int* out_missing)
{
    /* Resolves NPC role to character asset ID, sets out_missing if not loaded */
    int i;
    const char* asset_id = "";
    if (out_missing) *out_missing = 1;

    for (i = 0; i < (int)(sizeof(g_role_asset_bindings) / sizeof(g_role_asset_bindings[0])); ++i) {
        if (g_role_asset_bindings[i].role == role) {
            asset_id = g_role_asset_bindings[i].asset_id;
            break;
        }
    }

    if (!asset_id[0]) return "";
    if (find_character_asset(asset_id) >= 0) {
        if (out_missing) *out_missing = 0;
    }
    return asset_id;
}

const char* sdk_role_assets_resolve_prop(BuildingType type, int* out_missing)
{
    /* Resolves building type to prop asset ID, sets out_missing if not loaded */
    const char* asset_id = sdk_building_default_prop_id(type);
    if (out_missing) *out_missing = 1;
    if (!asset_id || !asset_id[0]) return "";
    if (find_prop_asset(asset_id) >= 0) {
        if (out_missing) *out_missing = 0;
    }
    return asset_id;
}
