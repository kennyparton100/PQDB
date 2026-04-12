/**
 * sdk_building_family.h -- Runtime building families and shell markers.
 */
#ifndef NQLSDK_BUILDING_FAMILY_H
#define NQLSDK_BUILDING_FAMILY_H

#include "../Settlements/Types/sdk_settlement_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDK_BUILDING_FAMILY_NONE = 0,
    SDK_BUILDING_FAMILY_CIVIC_ADMIN,
    SDK_BUILDING_FAMILY_HOUSING_WORKFORCE,
    SDK_BUILDING_FAMILY_STORAGE_LOGISTICS,
    SDK_BUILDING_FAMILY_EXTRACTION,
    SDK_BUILDING_FAMILY_BULK_PREP,
    SDK_BUILDING_FAMILY_UTILITIES,
    SDK_BUILDING_FAMILY_METALLURGY,
    SDK_BUILDING_FAMILY_REPAIR_MAINTENANCE,
    SDK_BUILDING_FAMILY_DEFENSE,
    SDK_BUILDING_FAMILY_WATER_PORT
} SdkBuildingFamily;

typedef enum {
    SDK_BUILDING_MARKER_NONE = 0,
    SDK_BUILDING_MARKER_ENTRANCE,
    SDK_BUILDING_MARKER_WORK,
    SDK_BUILDING_MARKER_STORAGE,
    SDK_BUILDING_MARKER_SLEEP,
    SDK_BUILDING_MARKER_PATROL,
    SDK_BUILDING_MARKER_WATER,
    SDK_BUILDING_MARKER_STATION
} SdkBuildingMarkerType;

typedef struct {
    uint8_t marker_type;
    uint8_t facing;
    uint16_t reserved0;
    int32_t wx;
    int32_t wy;
    int32_t wz;
    BlockType required_block;
} SdkBuildingRuntimeMarker;

const char* sdk_building_family_name(SdkBuildingFamily family);
SdkBuildingFamily sdk_building_family_for_type(BuildingType type);
const char* sdk_building_default_prop_id(BuildingType type);
int sdk_building_compute_runtime_markers(const BuildingPlacement* placement,
                                         SdkBuildingRuntimeMarker* out_markers,
                                         int max_markers);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_BUILDING_FAMILY_H */
