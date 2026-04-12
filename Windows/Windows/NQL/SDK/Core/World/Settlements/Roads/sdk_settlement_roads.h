#ifndef NQLSDK_SETTLEMENT_ROADS_H
#define NQLSDK_SETTLEMENT_ROADS_H

#include "../sdk_settlement.h"

#ifdef __cplusplus
extern "C" {
#endif

void sdk_settlement_generate_routes_for_chunk(SdkWorldGen* wg,
                                              SdkChunk* chunk,
                                              const SuperchunkSettlementData* settlement_data,
                                              const SettlementMetadata* settlement,
                                              const SettlementLayout* layout);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SETTLEMENT_ROADS_H */

