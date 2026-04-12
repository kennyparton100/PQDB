/**
 * sdk_role_assets.h -- Resolve runtime role and building bindings to authored assets.
 */
#ifndef NQLSDK_ROLE_ASSETS_H
#define NQLSDK_ROLE_ASSETS_H

#include "../Entities/sdk_entity.h"
#include "../World/Settlements/Types/sdk_settlement_types.h"

#ifdef __cplusplus
extern "C" {
#endif

const char* sdk_role_assets_resolve_character(SdkNpcRole role, int* out_missing);
const char* sdk_role_assets_resolve_prop(BuildingType type, int* out_missing);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_ROLE_ASSETS_H */
