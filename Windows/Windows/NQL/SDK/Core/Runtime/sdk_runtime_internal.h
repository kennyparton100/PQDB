#ifndef SDK_RUNTIME_INTERNAL_H
#define SDK_RUNTIME_INTERNAL_H

#include "../API/Internal/sdk_api_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

int creative_item_grant_count(ItemType item);
void creative_clamp_selection(void);

#ifdef __cplusplus
}
#endif

#endif
