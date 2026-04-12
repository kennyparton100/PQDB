/**
 * sdk_building_family.c -- Runtime building families and shell markers.
 */
#include "sdk_building_family.h"

#include <string.h>

typedef struct {
    BuildingType type;
    SdkBuildingFamily family;
    const char* prop_id;
} SdkBuildingFamilyEntry;

static const SdkBuildingFamilyEntry g_building_families[] = {
    { BUILDING_TYPE_HUT,         SDK_BUILDING_FAMILY_HOUSING_WORKFORCE, "hut_small" },
    { BUILDING_TYPE_HOUSE,       SDK_BUILDING_FAMILY_HOUSING_WORKFORCE, "house_small" },
    { BUILDING_TYPE_MANOR,       SDK_BUILDING_FAMILY_CIVIC_ADMIN,       "meeting_hall" },
    { BUILDING_TYPE_FARM,        SDK_BUILDING_FAMILY_EXTRACTION,        "farm_plot_compound" },
    { BUILDING_TYPE_BARN,        SDK_BUILDING_FAMILY_EXTRACTION,        "barn_small" },
    { BUILDING_TYPE_WORKSHOP,    SDK_BUILDING_FAMILY_METALLURGY,        "workshop_general" },
    { BUILDING_TYPE_FORGE,       SDK_BUILDING_FAMILY_METALLURGY,        "forge_small" },
    { BUILDING_TYPE_MILL,        SDK_BUILDING_FAMILY_BULK_PREP,         "mill_small" },
    { BUILDING_TYPE_STOREHOUSE,  SDK_BUILDING_FAMILY_STORAGE_LOGISTICS, "storehouse_small" },
    { BUILDING_TYPE_WAREHOUSE,   SDK_BUILDING_FAMILY_STORAGE_LOGISTICS, "warehouse_small" },
    { BUILDING_TYPE_SILO,        SDK_BUILDING_FAMILY_STORAGE_LOGISTICS, "open_stockyard" },
    { BUILDING_TYPE_WATCHTOWER,  SDK_BUILDING_FAMILY_DEFENSE,           "watchtower_small" },
    { BUILDING_TYPE_BARRACKS,    SDK_BUILDING_FAMILY_HOUSING_WORKFORCE, "barracks_small" },
    { BUILDING_TYPE_WALL_SECTION,SDK_BUILDING_FAMILY_DEFENSE,           "wall_section_straight" },
    { BUILDING_TYPE_WELL,        SDK_BUILDING_FAMILY_CIVIC_ADMIN,       "well_small" },
    { BUILDING_TYPE_MARKET,      SDK_BUILDING_FAMILY_CIVIC_ADMIN,       "market_stalls" },
    { BUILDING_TYPE_DOCK,        SDK_BUILDING_FAMILY_WATER_PORT,        "dock_small" }
};

static int placement_center(const BuildingPlacement* placement, int along_x)
{
    /* Calculates center coordinate of building placement (x or z based on along_x) */
    if (!placement) return 0;
    if (along_x) return placement->wx + (int)placement->footprint_x / 2;
    return placement->wz + (int)placement->footprint_z / 2;
}

static int placement_front_z(const BuildingPlacement* placement)
{
    /* Calculates front z coordinate of building placement */
    if (!placement) return 0;
    return placement->wz + (int)placement->footprint_z;
}

static int add_marker(const BuildingPlacement* placement,
                      SdkBuildingRuntimeMarker* out_markers,
                      int max_markers,
                      int count,
                      SdkBuildingMarkerType marker_type,
                      int wx,
                      int wy,
                      int wz,
                      BlockType required_block)
{
    /* Adds runtime marker to output array, returns updated count */
    if (!placement || !out_markers || count < 0 || count >= max_markers) return count;
    memset(&out_markers[count], 0, sizeof(out_markers[count]));
    out_markers[count].marker_type = (uint8_t)marker_type;
    out_markers[count].wx = wx;
    out_markers[count].wy = wy;
    out_markers[count].wz = wz;
    out_markers[count].required_block = required_block;
    return count + 1;
}

const char* sdk_building_family_name(SdkBuildingFamily family)
{
    /* Returns string name for building family enum */
    switch (family) {
        case SDK_BUILDING_FAMILY_CIVIC_ADMIN: return "civic_admin";
        case SDK_BUILDING_FAMILY_HOUSING_WORKFORCE: return "housing_workforce";
        case SDK_BUILDING_FAMILY_STORAGE_LOGISTICS: return "storage_logistics";
        case SDK_BUILDING_FAMILY_EXTRACTION: return "extraction";
        case SDK_BUILDING_FAMILY_BULK_PREP: return "bulk_material_prep";
        case SDK_BUILDING_FAMILY_UTILITIES: return "utilities";
        case SDK_BUILDING_FAMILY_METALLURGY: return "metallurgy";
        case SDK_BUILDING_FAMILY_REPAIR_MAINTENANCE: return "repair_maintenance";
        case SDK_BUILDING_FAMILY_DEFENSE: return "defense";
        case SDK_BUILDING_FAMILY_WATER_PORT: return "water_port";
        default: return "none";
    }
}

SdkBuildingFamily sdk_building_family_for_type(BuildingType type)
{
    /* Returns building family for given building type */
    int i;
    for (i = 0; i < (int)(sizeof(g_building_families) / sizeof(g_building_families[0])); ++i) {
        if (g_building_families[i].type == type) return g_building_families[i].family;
    }
    return SDK_BUILDING_FAMILY_NONE;
}

const char* sdk_building_default_prop_id(BuildingType type)
{
    /* Returns default prop asset ID for building type */
    int i;
    for (i = 0; i < (int)(sizeof(g_building_families) / sizeof(g_building_families[0])); ++i) {
        if (g_building_families[i].type == type) return g_building_families[i].prop_id;
    }
    return "";
}

int sdk_building_compute_runtime_markers(const BuildingPlacement* placement,
                                         SdkBuildingRuntimeMarker* out_markers,
                                         int max_markers)
{
    /* Computes runtime markers (entrance, sleep, work, etc.) for building placement */
    int count = 0;
    int center_x;
    int center_z;
    int front_z;
    int base_y;

    if (!placement || !out_markers || max_markers <= 0) return 0;

    center_x = placement_center(placement, 1);
    center_z = placement_center(placement, 0);
    front_z = placement_front_z(placement);
    base_y = placement->base_elevation;

    count = add_marker(placement, out_markers, max_markers, count,
                       SDK_BUILDING_MARKER_ENTRANCE, center_x, base_y, front_z, BLOCK_AIR);

    switch (placement->type) {
        case BUILDING_TYPE_HUT:
        case BUILDING_TYPE_HOUSE:
        case BUILDING_TYPE_MANOR:
        case BUILDING_TYPE_BARRACKS:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_SLEEP, center_x, base_y + 1, center_z, BLOCK_AIR);
            break;
        case BUILDING_TYPE_FARM:
        case BUILDING_TYPE_BARN:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_WORK, center_x, base_y, front_z + 2, BLOCK_AIR);
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_STORAGE, center_x, base_y, center_z, BLOCK_AIR);
            break;
        case BUILDING_TYPE_WORKSHOP:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_STATION, center_x, base_y + 1, center_z, BLOCK_CRAFTING_TABLE);
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_WORK, center_x, base_y + 1, center_z + 1, BLOCK_AIR);
            break;
        case BUILDING_TYPE_FORGE:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_STATION, center_x - 1, base_y + 1, center_z, BLOCK_FURNACE);
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_STATION, center_x + 1, base_y + 1, center_z, BLOCK_ANVIL);
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_STATION, center_x, base_y + 1, center_z + 1, BLOCK_BLACKSMITHING_TABLE);
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_WORK, center_x, base_y + 1, center_z - 1, BLOCK_AIR);
            break;
        case BUILDING_TYPE_MILL:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_WORK, center_x, base_y + 1, center_z, BLOCK_AIR);
            break;
        case BUILDING_TYPE_STOREHOUSE:
        case BUILDING_TYPE_WAREHOUSE:
        case BUILDING_TYPE_SILO:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_STORAGE, center_x, base_y + 1, center_z, BLOCK_AIR);
            break;
        case BUILDING_TYPE_WATCHTOWER:
        case BUILDING_TYPE_WALL_SECTION:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_PATROL, center_x, base_y + 1, center_z, BLOCK_AIR);
            break;
        case BUILDING_TYPE_WELL:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_WATER, center_x, base_y, center_z, BLOCK_WATER);
            break;
        case BUILDING_TYPE_MARKET:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_WORK, center_x, base_y + 1, center_z, BLOCK_AIR);
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_STORAGE, center_x + 2, base_y + 1, center_z, BLOCK_AIR);
            break;
        case BUILDING_TYPE_DOCK:
            count = add_marker(placement, out_markers, max_markers, count,
                               SDK_BUILDING_MARKER_WORK, center_x, base_y, center_z, BLOCK_AIR);
            break;
        default:
            break;
    }

    return count;
}
