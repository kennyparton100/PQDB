/**
 * sdk_settlement.h -- Settlement generation and management public API
 */
#ifndef NQLSDK_SETTLEMENT_H
#define NQLSDK_SETTLEMENT_H

#include "Types/sdk_settlement_types.h"
#include "../Chunks/sdk_chunk.h"
#include "../Worldgen/sdk_worldgen.h"
#include "../Worldgen/Types/sdk_worldgen_types.h"
#include "../Worldgen/Internal/sdk_worldgen_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

void sdk_settlement_generate_for_chunk(SdkWorldGen* wg, SdkChunk* chunk, SuperchunkSettlementData* settlement_data);

SuperchunkSettlementData* sdk_settlement_get_or_create_data(SdkWorldGen* wg, int cx, int cz);
void sdk_settlement_query_debug_at(SdkWorldGen* wg, int wx, int wz, SettlementDebugInfo* out_info);

SettlementSuitability sdk_settlement_evaluate_suitability(SdkWorldGen* wg, int wx, int wz);

SettlementSuitability sdk_settlement_evaluate_suitability_full(SdkWorldGen* wg, int wx, int wz, const SdkTerrainColumnProfile* profile, const SdkContinentalSample* continental);

void sdk_settlement_sample_continental_approximation(SdkWorldGen* wg, int wx, int wz, SdkContinentalSample* out_sample);

SettlementPurpose sdk_settlement_determine_purpose_for_type(SdkWorldGen* wg,
                                                            int wx,
                                                            int wz,
                                                            const SdkTerrainColumnProfile* profile,
                                                            const SdkContinentalSample* continental,
                                                            SettlementType type);

GeographicVariant sdk_settlement_determine_variant(SdkWorldGen* wg, const SdkTerrainColumnProfile* profile, const SdkContinentalSample* continental, int wx, int wz, int surface_y);

void sdk_settlement_apply_damage(SettlementMetadata* settlement, float damage_amount, uint32_t current_tick);

uint32_t sdk_settlement_calculate_population(const SettlementMetadata* settlement, uint32_t residential_count);

float sdk_settlement_calculate_food_production(const SettlementMetadata* settlement,
                                               float soil_fertility,
                                               uint32_t farm_count,
                                               uint32_t dock_count);

const BuildingTemplate* sdk_settlement_get_building_template(BuildingType type);

void sdk_settlement_generate_building(SdkChunk* chunk, const BuildingPlacement* placement, const BuildingTemplate* building_template, float integrity, SdkBedrockProvince bedrock);

SettlementLayout* sdk_settlement_generate_layout(SdkWorldGen* wg, const SettlementMetadata* metadata);

void sdk_settlement_free_layout(SettlementLayout* layout);

int sdk_settlement_save_superchunk(const char* world_path, int scx, int scz, const SuperchunkSettlementData* data);

int sdk_settlement_load_superchunk(const char* world_path, int scx, int scz, SuperchunkSettlementData* out_data);

void sdk_settlement_set_world_path(SdkWorldGen* wg, const char* path);
void sdk_settlement_set_diagnostics_enabled(bool enabled);

void sdk_settlement_flush_cache(SdkWorldGen* wg);

void sdk_settlement_generate_foundation(SettlementMetadata* settlement, int surface_y);

void sdk_settlement_prepare_zone_terrain(SdkChunk* chunk, const BuildingZone* zone);

/* Fast chunk-to-settlement lookup (requires settlement data with chunk index) */
int sdk_settlement_get_for_chunk(SdkWorldGen* wg, int cx, int cz, 
                                 SettlementMetadata** out_settlements, 
                                 int max_count);

#ifdef __cplusplus
}
#endif

#endif /* NQLSDK_SETTLEMENT_H */
