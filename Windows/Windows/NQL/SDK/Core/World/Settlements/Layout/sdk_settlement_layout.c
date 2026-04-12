/**
 * sdk_settlement_layout.c -- Settlement layout generation
 */
#include "../sdk_settlement.h"
#include "../../Worldgen/sdk_worldgen.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void layout_init(SettlementLayout* layout, SettlementType type, SettlementPurpose purpose, int center_wx, int center_wz, uint16_t radius)
{
    memset(layout, 0, sizeof(SettlementLayout));
    layout->type = type;
    layout->purpose = purpose;
    layout->center_wx = center_wx;
    layout->center_wz = center_wz;
    layout->radius = radius;
    layout->building_capacity = 64;
    layout->buildings = (BuildingPlacement*)malloc(sizeof(BuildingPlacement) * layout->building_capacity);
    layout->building_count = 0;
}

static uint32_t layout_seed(const SdkWorldGen* wg, const SettlementMetadata* metadata, uint32_t salt)
{
    uint32_t seed = salt;

    if (wg) seed ^= wg->desc.seed;
    if (metadata) {
        seed ^= metadata->settlement_id;
        seed ^= (uint32_t)metadata->type * 131u;
        seed ^= (uint32_t)metadata->purpose * 313u;
        seed ^= (uint32_t)metadata->geographic_variant * 911u;
    }
    return sdk_worldgen_hash32(seed);
}

static uint32_t layout_rand_u32(uint32_t* state)
{
    uint32_t x = (*state != 0u) ? *state : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int layout_rand_range(uint32_t* state, int upper_exclusive)
{
    if (!state || upper_exclusive <= 0) return 0;
    return (int)(layout_rand_u32(state) % (uint32_t)upper_exclusive);
}

static BuildingZoneType preferred_zone_for_building(BuildingType type)
{
    switch (type) {
        case BUILDING_TYPE_HUT:
        case BUILDING_TYPE_HOUSE:
        case BUILDING_TYPE_MANOR:
            return ZONE_TYPE_RESIDENTIAL;
        case BUILDING_TYPE_FARM:
        case BUILDING_TYPE_BARN:
            return ZONE_TYPE_AGRICULTURAL;
        case BUILDING_TYPE_WORKSHOP:
        case BUILDING_TYPE_FORGE:
        case BUILDING_TYPE_MILL:
            return ZONE_TYPE_INDUSTRIAL;
        case BUILDING_TYPE_STOREHOUSE:
        case BUILDING_TYPE_WAREHOUSE:
        case BUILDING_TYPE_SILO:
            return ZONE_TYPE_COMMERCIAL;
        case BUILDING_TYPE_WATCHTOWER:
        case BUILDING_TYPE_BARRACKS:
        case BUILDING_TYPE_WALL_SECTION:
            return ZONE_TYPE_DEFENSIVE;
        case BUILDING_TYPE_WELL:
        case BUILDING_TYPE_MARKET:
            return ZONE_TYPE_CIVIC;
        case BUILDING_TYPE_DOCK:
            return ZONE_TYPE_HARBOR;
        default:
            return ZONE_TYPE_CIVIC;
    }
}

static int16_t resolve_building_base_elevation(const SettlementMetadata* metadata, int wx, int wz, BuildingType type)
{
    int best_index = -1;
    int preferred_index = -1;
    int best_dist_sq = 0;
    int preferred_dist_sq = 0;
    uint32_t i;
    BuildingZoneType preferred_zone;

    if (!metadata || metadata->zone_count == 0) return 64;

    preferred_zone = preferred_zone_for_building(type);
    for (i = 0; i < metadata->zone_count; ++i) {
        const BuildingZone* zone = &metadata->zones[i];
        int dx = wx - zone->center_wx;
        int dz = wz - zone->center_wz;
        int dist_sq = dx * dx + dz * dz;

        if (best_index < 0 || dist_sq < best_dist_sq) {
            best_index = (int)i;
            best_dist_sq = dist_sq;
        }
        if (zone->zone_type == preferred_zone &&
            (preferred_index < 0 || dist_sq < preferred_dist_sq)) {
            preferred_index = (int)i;
            preferred_dist_sq = dist_sq;
        }
    }

    if (preferred_index >= 0) {
        return metadata->zones[preferred_index].base_elevation;
    }
    return metadata->zones[best_index].base_elevation;
}

static void layout_add_building(SettlementLayout* layout, const SettlementMetadata* metadata, int wx, int wz, BuildingType type, uint8_t rotation, uint8_t footprint_x, uint8_t footprint_z, uint8_t height)
{
    BuildingPlacement* building;
    
    if (!layout || !layout->buildings) return;
    
    if (layout->building_count >= layout->building_capacity) {
        layout->building_capacity *= 2;
        layout->buildings = (BuildingPlacement*)realloc(layout->buildings, sizeof(BuildingPlacement) * layout->building_capacity);
    }
    
    building = &layout->buildings[layout->building_count++];
    building->wx = wx;
    building->wz = wz;
    building->base_elevation = resolve_building_base_elevation(metadata, wx, wz, type);
    building->type = type;
    building->rotation = rotation;
    building->footprint_x = footprint_x;
    building->footprint_z = footprint_z;
    building->height = height;
}

static void add_template_building(SettlementLayout* layout, const SettlementMetadata* metadata, int wx, int wz, BuildingType type, uint8_t rotation)
{
    const BuildingTemplate* template = sdk_settlement_get_building_template(type);

    if (!template) return;
    layout_add_building(layout, metadata, wx, wz, type, rotation, template->footprint_x, template->footprint_z, template->height);
}

static void generate_village_linear_layout(SettlementLayout* layout, const SettlementMetadata* metadata, uint32_t* rng_state)
{
    int i, count, spacing, wx, wz;
    int farm_rows;
    
    count = 8 + layout_rand_range(rng_state, 8);
    spacing = 8;

    add_template_building(layout, metadata, layout->center_wx, layout->center_wz, BUILDING_TYPE_WELL, 0);
    
    for (i = 0; i < count; ++i) {
        int side = (i % 2 == 0) ? 1 : -1;
        int offset = (i / 2 + 1) * spacing;
        
        wx = layout->center_wx + offset;
        wz = layout->center_wz + side * 6;

        if (metadata->geographic_variant == GEOGRAPHIC_VARIANT_FOREST && layout_rand_range(rng_state, 100) < 75) {
            add_template_building(layout, metadata, wx, wz, BUILDING_TYPE_HUT, 0);
        } else if (layout_rand_range(rng_state, 100) < 65) {
            add_template_building(layout, metadata, wx, wz, BUILDING_TYPE_HUT, 0);
        } else {
            add_template_building(layout, metadata, wx, wz, BUILDING_TYPE_HOUSE, 0);
        }
    }

    if (metadata->purpose == SETTLEMENT_PURPOSE_FISHING ||
        metadata->geographic_variant == GEOGRAPHIC_VARIANT_COASTAL ||
        metadata->geographic_variant == GEOGRAPHIC_VARIANT_RIVERSIDE) {
        add_template_building(layout, metadata, layout->center_wx, layout->center_wz - 20, BUILDING_TYPE_DOCK, 0);
        add_template_building(layout, metadata, layout->center_wx + 14, layout->center_wz - 4, BUILDING_TYPE_STOREHOUSE, 0);
    } else {
        farm_rows = 3 + layout_rand_range(rng_state, 2);
        for (i = 0; i < farm_rows; ++i) {
            wx = layout->center_wx + (i - 1) * 16;
            wz = layout->center_wz + 16;
            add_template_building(layout, metadata, wx, wz, BUILDING_TYPE_FARM, 0);
        }
        add_template_building(layout, metadata, layout->center_wx + 26, layout->center_wz + 18, BUILDING_TYPE_BARN, 0);
    }
}

static BuildingType choose_town_core_type(const SettlementMetadata* metadata, uint32_t* rng_state)
{
    int roll = layout_rand_range(rng_state, 100);

    if (!metadata) return BUILDING_TYPE_STOREHOUSE;

    switch (metadata->purpose) {
        case SETTLEMENT_PURPOSE_MINING:
            if (roll < 35) return BUILDING_TYPE_WORKSHOP;
            if (roll < 60) return BUILDING_TYPE_FORGE;
            if (roll < 82) return BUILDING_TYPE_STOREHOUSE;
            return BUILDING_TYPE_HOUSE;
        case SETTLEMENT_PURPOSE_LOGISTICS:
            if (roll < 35) return BUILDING_TYPE_STOREHOUSE;
            if (roll < 60) return BUILDING_TYPE_WAREHOUSE;
            if (roll < 82) return BUILDING_TYPE_WORKSHOP;
            return BUILDING_TYPE_HOUSE;
        case SETTLEMENT_PURPOSE_PROCESSING:
            if (roll < 30) return BUILDING_TYPE_WORKSHOP;
            if (roll < 55) return BUILDING_TYPE_MILL;
            if (roll < 80) return BUILDING_TYPE_STOREHOUSE;
            return BUILDING_TYPE_HOUSE;
        case SETTLEMENT_PURPOSE_HYDROCARBON:
            if (roll < 28) return BUILDING_TYPE_WAREHOUSE;
            if (roll < 54) return BUILDING_TYPE_STOREHOUSE;
            if (roll < 78) return BUILDING_TYPE_WORKSHOP;
            return BUILDING_TYPE_HOUSE;
        case SETTLEMENT_PURPOSE_CEMENT:
            if (roll < 30) return BUILDING_TYPE_MILL;
            if (roll < 58) return BUILDING_TYPE_STOREHOUSE;
            if (roll < 82) return BUILDING_TYPE_WORKSHOP;
            return BUILDING_TYPE_HOUSE;
        case SETTLEMENT_PURPOSE_TIMBER:
            if (roll < 24) return BUILDING_TYPE_BARN;
            if (roll < 50) return BUILDING_TYPE_STOREHOUSE;
            if (roll < 76) return BUILDING_TYPE_WORKSHOP;
            return BUILDING_TYPE_HOUSE;
        default:
            if (roll < 40) return BUILDING_TYPE_HOUSE;
            if (roll < 70) return BUILDING_TYPE_WORKSHOP;
            return BUILDING_TYPE_STOREHOUSE;
    }
}

static void generate_town_grid_layout(SettlementLayout* layout, const SettlementMetadata* metadata, uint32_t* rng_state)
{
    int grid_x, grid_z, wx, wz;
    BuildingType type;
    
    for (grid_x = -2; grid_x <= 2; ++grid_x) {
        for (grid_z = -2; grid_z <= 2; ++grid_z) {
            if (grid_x == 0 && grid_z == 0) {
                type = BUILDING_TYPE_MARKET;
            } else {
                type = choose_town_core_type(metadata, rng_state);
            }

            wx = layout->center_wx + grid_x * 16;
            wz = layout->center_wz + grid_z * 16;
            add_template_building(layout, metadata, wx, wz, type, 0);
        }
    }

    if (metadata->purpose == SETTLEMENT_PURPOSE_MINING) {
        add_template_building(layout, metadata, layout->center_wx + 42, layout->center_wz - 18, BUILDING_TYPE_FORGE, 0);
        add_template_building(layout, metadata, layout->center_wx + 58, layout->center_wz - 18, BUILDING_TYPE_STOREHOUSE, 0);
    } else if (metadata->purpose == SETTLEMENT_PURPOSE_LOGISTICS) {
        add_template_building(layout, metadata, layout->center_wx + 46, layout->center_wz + 12, BUILDING_TYPE_WAREHOUSE, 0);
        add_template_building(layout, metadata, layout->center_wx - 46, layout->center_wz + 12, BUILDING_TYPE_WAREHOUSE, 0);
    } else if (metadata->purpose == SETTLEMENT_PURPOSE_HYDROCARBON) {
        add_template_building(layout, metadata, layout->center_wx + 46, layout->center_wz - 12, BUILDING_TYPE_WAREHOUSE, 0);
        add_template_building(layout, metadata, layout->center_wx - 46, layout->center_wz - 12, BUILDING_TYPE_STOREHOUSE, 0);
        add_template_building(layout, metadata, layout->center_wx + 58, layout->center_wz + 16, BUILDING_TYPE_WORKSHOP, 0);
    } else if (metadata->purpose == SETTLEMENT_PURPOSE_CEMENT) {
        add_template_building(layout, metadata, layout->center_wx + 46, layout->center_wz - 12, BUILDING_TYPE_MILL, 0);
        add_template_building(layout, metadata, layout->center_wx - 46, layout->center_wz - 12, BUILDING_TYPE_STOREHOUSE, 0);
    } else if (metadata->purpose == SETTLEMENT_PURPOSE_TIMBER) {
        add_template_building(layout, metadata, layout->center_wx + 46, layout->center_wz - 12, BUILDING_TYPE_BARN, 0);
        add_template_building(layout, metadata, layout->center_wx - 46, layout->center_wz - 12, BUILDING_TYPE_STOREHOUSE, 0);
    }

    add_template_building(layout, metadata, layout->center_wx + 32, layout->center_wz + 32, BUILDING_TYPE_WATCHTOWER, 0);
    add_template_building(layout, metadata, layout->center_wx - 32, layout->center_wz + 32, BUILDING_TYPE_WATCHTOWER, 0);

    if (metadata->geographic_variant == GEOGRAPHIC_VARIANT_JUNCTION ||
        metadata->geographic_variant == GEOGRAPHIC_VARIANT_MOUNTAIN) {
        add_template_building(layout, metadata, layout->center_wx + 32, layout->center_wz - 32, BUILDING_TYPE_WATCHTOWER, 0);
    }
}

static void generate_city_layout(SettlementLayout* layout, const SettlementMetadata* metadata, uint32_t* rng_state)
{
    int i, district;
    
    for (district = 0; district < 4; ++district) {
        int district_x = layout->center_wx + ((district % 2) * 64 - 32);
        int district_z = layout->center_wz + ((district / 2) * 64 - 32);
        
        for (i = 0; i < 16; ++i) {
            int local_x = (i % 4) * 14;
            int local_z = (i / 4) * 14;
            BuildingType type = (layout_rand_range(rng_state, 100) < 68) ? BUILDING_TYPE_HOUSE : BUILDING_TYPE_MANOR;
            add_template_building(layout, metadata, district_x + local_x, district_z + local_z, type, 0);
        }
    }

    add_template_building(layout, metadata, layout->center_wx, layout->center_wz, BUILDING_TYPE_MARKET, 0);

    if (metadata->purpose == SETTLEMENT_PURPOSE_PORT ||
        metadata->geographic_variant == GEOGRAPHIC_VARIANT_COASTAL ||
        metadata->geographic_variant == GEOGRAPHIC_VARIANT_RIVERSIDE) {
        add_template_building(layout, metadata, layout->center_wx, layout->center_wz - 64, BUILDING_TYPE_DOCK, 0);
        add_template_building(layout, metadata, layout->center_wx + 20, layout->center_wz - 40, BUILDING_TYPE_WAREHOUSE, 0);
        add_template_building(layout, metadata, layout->center_wx - 20, layout->center_wz - 40, BUILDING_TYPE_WAREHOUSE, 0);
    } else {
        add_template_building(layout, metadata, layout->center_wx + 42, layout->center_wz - 30, BUILDING_TYPE_BARRACKS, 0);
        add_template_building(layout, metadata, layout->center_wx - 42, layout->center_wz - 30, BUILDING_TYPE_STOREHOUSE, 0);
    }
}

SettlementLayout* sdk_settlement_generate_layout(SdkWorldGen* wg, const SettlementMetadata* metadata)
{
    SettlementLayout* layout;
    uint32_t rng_state;
    
    if (!metadata) return NULL;
    
    layout = (SettlementLayout*)malloc(sizeof(SettlementLayout));
    if (!layout) return NULL;
    
    layout_init(layout, metadata->type, metadata->purpose, metadata->center_wx, metadata->center_wz, metadata->radius);
    rng_state = layout_seed(wg, metadata, 0x51a7e1u);
    
    switch (metadata->type) {
        case SETTLEMENT_TYPE_VILLAGE:
            generate_village_linear_layout(layout, metadata, &rng_state);
            break;
        case SETTLEMENT_TYPE_TOWN:
            generate_town_grid_layout(layout, metadata, &rng_state);
            break;
        case SETTLEMENT_TYPE_CITY:
            generate_city_layout(layout, metadata, &rng_state);
            break;
        default:
            break;
    }
    
    return layout;
}

void sdk_settlement_free_layout(SettlementLayout* layout)
{
    if (!layout) return;
    if (layout->buildings) {
        free(layout->buildings);
    }
    free(layout);
}

