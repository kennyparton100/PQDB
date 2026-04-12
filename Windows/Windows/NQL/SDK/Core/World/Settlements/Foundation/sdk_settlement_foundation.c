/**
 * sdk_settlement_foundation.c -- Settlement foundation and zone generation
 */
#include "../sdk_settlement.h"
#include "../../Worldgen/sdk_worldgen.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void add_zone(SettlementMetadata* settlement, int32_t cx, int32_t cz, int16_t rx, int16_t rz, int16_t elevation, BuildingZoneType type)
{
    BuildingZone* zone;
    
    if (!settlement || settlement->zone_count >= 16) return;
    
    zone = &settlement->zones[settlement->zone_count++];
    zone->center_wx = cx;
    zone->center_wz = cz;
    zone->radius_x = rx;
    zone->radius_z = rz;
    zone->base_elevation = elevation;
    zone->terrain_modification = 1;
    zone->zone_type = type;
}

static void generate_plains_foundation(SettlementMetadata* settlement, int surface_y)
{
    int center_x = settlement->center_wx;
    int center_z = settlement->center_wz;
    int radius = settlement->radius;
    
    settlement->zone_count = 0;
    
    settlement->perimeter.perimeter_wx = center_x;
    settlement->perimeter.perimeter_wz = center_z;
    settlement->perimeter.perimeter_type = 0;
    settlement->perimeter.outer_radius = radius;
    settlement->perimeter.inner_radius = 0;
    
    if (settlement->type == SETTLEMENT_TYPE_CITY) {
        add_zone(settlement, center_x, center_z, 40, 40, surface_y, ZONE_TYPE_CIVIC);
        add_zone(settlement, center_x - 100, center_z, 60, 60, surface_y, ZONE_TYPE_COMMERCIAL);
        add_zone(settlement, center_x + 100, center_z, 60, 60, surface_y, ZONE_TYPE_COMMERCIAL);
        add_zone(settlement, center_x, center_z - 100, 80, 80, surface_y, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x, center_z + 100, 80, 80, surface_y, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x - 150, center_z - 150, 50, 50, surface_y, ZONE_TYPE_INDUSTRIAL);
        add_zone(settlement, center_x + 150, center_z + 150, 50, 50, surface_y, ZONE_TYPE_INDUSTRIAL);
    } else if (settlement->type == SETTLEMENT_TYPE_TOWN) {
        add_zone(settlement, center_x, center_z, 30, 30, surface_y, ZONE_TYPE_CIVIC);
        add_zone(settlement, center_x - 60, center_z, 40, 40, surface_y, ZONE_TYPE_COMMERCIAL);
        add_zone(settlement, center_x + 60, center_z, 40, 40, surface_y, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x, center_z - 80, 50, 50, surface_y, ZONE_TYPE_AGRICULTURAL);
        add_zone(settlement, center_x, center_z + 80, 35, 35, surface_y, ZONE_TYPE_INDUSTRIAL);
    } else {
        add_zone(settlement, center_x, center_z, 20, 20, surface_y, ZONE_TYPE_CIVIC);
        add_zone(settlement, center_x - 30, center_z, 25, 25, surface_y, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x + 30, center_z, 25, 25, surface_y, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x, center_z - 50, 40, 40, surface_y, ZONE_TYPE_AGRICULTURAL);
    }
}

static void generate_coastal_foundation(SettlementMetadata* settlement, int surface_y)
{
    int center_x = settlement->center_wx;
    int center_z = settlement->center_wz;
    int radius = settlement->radius;
    
    settlement->zone_count = 0;
    
    settlement->perimeter.perimeter_wx = center_x;
    settlement->perimeter.perimeter_wz = center_z;
    settlement->perimeter.perimeter_type = 1;
    settlement->perimeter.outer_radius = radius;
    settlement->perimeter.inner_radius = 0;
    
    if (settlement->type == SETTLEMENT_TYPE_CITY) {
        add_zone(settlement, center_x, center_z - 50, 60, 40, 64, ZONE_TYPE_HARBOR);
        add_zone(settlement, center_x - 70, center_z, 50, 50, 70, ZONE_TYPE_COMMERCIAL);
        add_zone(settlement, center_x + 70, center_z, 50, 50, 70, ZONE_TYPE_COMMERCIAL);
        add_zone(settlement, center_x, center_z + 60, 80, 60, 75, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x, center_z + 130, 70, 60, 80, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x, center_z + 200, 50, 50, 85, ZONE_TYPE_CIVIC);
    } else if (settlement->type == SETTLEMENT_TYPE_TOWN) {
        add_zone(settlement, center_x, center_z - 40, 40, 30, 64, ZONE_TYPE_HARBOR);
        add_zone(settlement, center_x, center_z + 20, 50, 40, 70, ZONE_TYPE_COMMERCIAL);
        add_zone(settlement, center_x - 60, center_z + 60, 40, 40, 75, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x + 60, center_z + 60, 40, 40, 75, ZONE_TYPE_RESIDENTIAL);
    } else {
        add_zone(settlement, center_x, center_z - 20, 25, 20, 64, ZONE_TYPE_HARBOR);
        add_zone(settlement, center_x, center_z + 20, 30, 25, 70, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x, center_z + 55, 25, 25, 72, ZONE_TYPE_AGRICULTURAL);
    }
}

static void generate_mountain_foundation(SettlementMetadata* settlement, int surface_y)
{
    int center_x = settlement->center_wx;
    int center_z = settlement->center_wz;
    int radius = settlement->radius;
    
    settlement->zone_count = 0;
    
    settlement->perimeter.perimeter_wx = center_x;
    settlement->perimeter.perimeter_wz = center_z;
    settlement->perimeter.perimeter_type = 2;
    settlement->perimeter.outer_radius = radius;
    settlement->perimeter.inner_radius = 0;
    
    if (settlement->type == SETTLEMENT_TYPE_CITY) {
        add_zone(settlement, center_x, center_z - 100, 50, 40, surface_y + 15, ZONE_TYPE_CIVIC);
        add_zone(settlement, center_x - 80, center_z - 50, 60, 50, surface_y + 10, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x + 80, center_z - 50, 60, 50, surface_y + 10, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x, center_z, 70, 60, surface_y + 5, ZONE_TYPE_COMMERCIAL);
        add_zone(settlement, center_x - 100, center_z + 70, 50, 50, surface_y, ZONE_TYPE_INDUSTRIAL);
        add_zone(settlement, center_x + 100, center_z + 70, 50, 50, surface_y, ZONE_TYPE_INDUSTRIAL);
        add_zone(settlement, center_x, center_z + 150, 60, 50, surface_y - 5, ZONE_TYPE_AGRICULTURAL);
    } else if (settlement->type == SETTLEMENT_TYPE_TOWN) {
        add_zone(settlement, center_x, center_z - 60, 40, 35, surface_y + 10, ZONE_TYPE_CIVIC);
        add_zone(settlement, center_x - 60, center_z, 45, 40, surface_y + 5, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x + 60, center_z, 45, 40, surface_y + 5, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x, center_z + 70, 50, 45, surface_y, ZONE_TYPE_INDUSTRIAL);
    } else {
        add_zone(settlement, center_x, center_z - 30, 25, 25, surface_y + 5, ZONE_TYPE_CIVIC);
        add_zone(settlement, center_x - 35, center_z + 10, 30, 30, surface_y, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x + 35, center_z + 10, 30, 30, surface_y, ZONE_TYPE_RESIDENTIAL);
        add_zone(settlement, center_x, center_z + 60, 40, 35, surface_y - 3, ZONE_TYPE_AGRICULTURAL);
    }
}

void sdk_settlement_generate_foundation(SettlementMetadata* settlement, int surface_y)
{
    if (!settlement) return;
    
    switch (settlement->geographic_variant) {
        case GEOGRAPHIC_VARIANT_COASTAL:
            generate_coastal_foundation(settlement, surface_y);
            break;
        case GEOGRAPHIC_VARIANT_MOUNTAIN:
            generate_mountain_foundation(settlement, surface_y);
            break;
        case GEOGRAPHIC_VARIANT_PLAINS:
        default:
            generate_plains_foundation(settlement, surface_y);
            break;
    }
}

