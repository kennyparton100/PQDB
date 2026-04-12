/**
 * sdk_settlement_types.h -- Settlement system type definitions
 */
#ifndef NQLSDK_SETTLEMENT_TYPES_H
#define NQLSDK_SETTLEMENT_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "../../Blocks/sdk_block.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SETTLEMENT_TYPE_NONE = 0,
    SETTLEMENT_TYPE_VILLAGE,
    SETTLEMENT_TYPE_TOWN,
    SETTLEMENT_TYPE_CITY
} SettlementType;

typedef enum {
    SETTLEMENT_PURPOSE_FARMING = 0,
    SETTLEMENT_PURPOSE_FISHING,
    SETTLEMENT_PURPOSE_MINING,
    SETTLEMENT_PURPOSE_LOGISTICS,
    SETTLEMENT_PURPOSE_PROCESSING,
    SETTLEMENT_PURPOSE_PORT,
    SETTLEMENT_PURPOSE_CAPITAL,
    SETTLEMENT_PURPOSE_FORTRESS,
    SETTLEMENT_PURPOSE_HYDROCARBON,
    SETTLEMENT_PURPOSE_CEMENT,
    SETTLEMENT_PURPOSE_TIMBER
} SettlementPurpose;

typedef enum {
    SETTLEMENT_STATE_PRISTINE = 0,
    SETTLEMENT_STATE_DAMAGED,
    SETTLEMENT_STATE_RUINS,
    SETTLEMENT_STATE_ABANDONED,
    SETTLEMENT_STATE_REBUILDING
} SettlementState;

typedef enum {
    GEOGRAPHIC_VARIANT_PLAINS = 0,
    GEOGRAPHIC_VARIANT_COASTAL,
    GEOGRAPHIC_VARIANT_RIVERSIDE,
    GEOGRAPHIC_VARIANT_MOUNTAIN,
    GEOGRAPHIC_VARIANT_DESERT,
    GEOGRAPHIC_VARIANT_FOREST,
    GEOGRAPHIC_VARIANT_JUNCTION
} GeographicVariant;

typedef enum {
    ZONE_TYPE_RESIDENTIAL = 0,
    ZONE_TYPE_COMMERCIAL,
    ZONE_TYPE_INDUSTRIAL,
    ZONE_TYPE_AGRICULTURAL,
    ZONE_TYPE_DEFENSIVE,
    ZONE_TYPE_CIVIC,
    ZONE_TYPE_HARBOR
} BuildingZoneType;

typedef struct {
    int32_t center_wx, center_wz;
    int16_t radius_x, radius_z;
    int16_t base_elevation;
    uint8_t terrain_modification;
    BuildingZoneType zone_type;
    uint8_t reserved[3];
} BuildingZone;

typedef struct {
    int32_t perimeter_wx, perimeter_wz;
    uint8_t perimeter_type;
    int16_t outer_radius;
    int16_t inner_radius;
} SettlementPerimeter;

#define SDK_MAX_CHUNKS_PER_SETTLEMENT 64

typedef struct {
    int16_t cx;
    int16_t cz;
} SettlementChunkCoord;

typedef struct {
    uint32_t settlement_id;
    SettlementType type;
    SettlementPurpose purpose;
    SettlementState state;
    
    int32_t center_wx;
    int32_t center_wz;
    uint16_t radius;
    
    GeographicVariant geographic_variant;
    uint8_t zone_count;
    BuildingZone zones[16];
    SettlementPerimeter perimeter;
    
    uint32_t population;
    uint32_t max_population;
    float food_production;
    float resource_output;
    
    uint16_t residential_count;
    uint16_t production_count;
    uint16_t storage_count;
    uint16_t defense_count;
    
    float integrity;
    uint32_t last_damage_tick;
    uint32_t rebuild_start_tick;
    
    float water_access;
    float fertility;
    float defensibility;
    float flatness;
    
    uint16_t chunk_count;
    SettlementChunkCoord chunks[SDK_MAX_CHUNKS_PER_SETTLEMENT];
} SettlementMetadata;

#define SDK_MAX_SETTLEMENTS_PER_SUPERCHUNK 32

typedef struct {
    int32_t superchunk_x;
    int32_t superchunk_z;
    uint32_t settlement_count;
    SettlementMetadata settlements[SDK_MAX_SETTLEMENTS_PER_SUPERCHUNK];
    
    uint8_t chunk_settlement_count[16][16];
    uint8_t chunk_settlement_indices[16][16][8];
} SuperchunkSettlementData;

typedef enum {
    BUILDING_TYPE_NONE = 0,
    BUILDING_TYPE_HUT,
    BUILDING_TYPE_HOUSE,
    BUILDING_TYPE_MANOR,
    BUILDING_TYPE_FARM,
    BUILDING_TYPE_BARN,
    BUILDING_TYPE_WORKSHOP,
    BUILDING_TYPE_FORGE,
    BUILDING_TYPE_MILL,
    BUILDING_TYPE_STOREHOUSE,
    BUILDING_TYPE_WAREHOUSE,
    BUILDING_TYPE_SILO,
    BUILDING_TYPE_WATCHTOWER,
    BUILDING_TYPE_BARRACKS,
    BUILDING_TYPE_WALL_SECTION,
    BUILDING_TYPE_WELL,
    BUILDING_TYPE_MARKET,
    BUILDING_TYPE_DOCK
} BuildingType;

typedef struct {
    BuildingType type;
    uint8_t footprint_x;
    uint8_t footprint_z;
    uint8_t height;
    BlockType primary_material;
    BlockType secondary_material;
    float spawn_weight;
} BuildingTemplate;

typedef struct {
    int wx, wz;
    int16_t base_elevation;
    BuildingType type;
    uint8_t rotation;
    uint8_t footprint_x;
    uint8_t footprint_z;
    uint8_t height;
} BuildingPlacement;

typedef struct {
    SettlementType type;
    SettlementPurpose purpose;
    int center_wx, center_wz;
    uint16_t radius;
    BuildingPlacement* buildings;
    uint32_t building_count;
    uint32_t building_capacity;
} SettlementLayout;

typedef struct {
    float city_score;
    float town_score;
    float village_score;
    SettlementPurpose recommended_purpose;
} SettlementSuitability;

typedef struct {
    uint8_t found;
    uint8_t in_zone;
    uint8_t in_building;
    uint8_t reserved0;
    uint32_t settlement_id;
    SettlementType type;
    SettlementPurpose purpose;
    SettlementState state;
    GeographicVariant geographic_variant;
    int32_t center_wx;
    int32_t center_wz;
    uint16_t radius;
    int16_t zone_base_elevation;
    int16_t building_base_elevation;
    uint8_t building_footprint_x;
    uint8_t building_footprint_z;
    uint8_t building_height;
    uint8_t reserved1;
    uint32_t population;
    uint32_t max_population;
    float integrity;
    float water_access;
    float fertility;
    float defensibility;
    float flatness;
    float food_production;
    float resource_output;
    int32_t zone_index;
    BuildingZoneType zone_type;
    int32_t building_index;
    BuildingType building_type;
    int32_t building_wx;
    int32_t building_wz;
} SettlementDebugInfo;

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SETTLEMENT_TYPES_H */
