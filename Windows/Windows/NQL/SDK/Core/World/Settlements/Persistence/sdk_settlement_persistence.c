/**
 * sdk_settlement_persistence.c -- Settlement save/load functions
 */
#include "../sdk_settlement.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define SETTLEMENT_FILE_MAGIC 0x53455454u
#define SETTLEMENT_FILE_VERSION 3u

typedef struct {
    int16_t center_wx, center_wz;
    int16_t radius_x, radius_z;
    int16_t base_elevation;
    uint8_t terrain_modification;
    BuildingZoneType zone_type;
    uint8_t reserved[3];
} BuildingZoneV1;

typedef struct {
    int32_t perimeter_wx, perimeter_wz;
    uint8_t perimeter_type;
    int16_t outer_radius;
    int16_t inner_radius;
} SettlementPerimeterV1;

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
    BuildingZoneV1 zones[16];
    SettlementPerimeterV1 perimeter;
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
} SettlementMetadataV1;

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
    uint8_t chunk_coords[SDK_MAX_CHUNKS_PER_SETTLEMENT * 2];
} SettlementMetadataV3;

static void upgrade_settlement_metadata_v1(SettlementMetadata* dst, const SettlementMetadataV1* src)
{
    uint32_t i;

    if (!dst || !src) return;

    memset(dst, 0, sizeof(*dst));
    dst->settlement_id = src->settlement_id;
    dst->type = src->type;
    dst->purpose = src->purpose;
    dst->state = src->state;
    dst->center_wx = src->center_wx;
    dst->center_wz = src->center_wz;
    dst->radius = src->radius;
    dst->geographic_variant = src->geographic_variant;
    dst->zone_count = src->zone_count;
    dst->perimeter.perimeter_wx = src->perimeter.perimeter_wx;
    dst->perimeter.perimeter_wz = src->perimeter.perimeter_wz;
    dst->perimeter.perimeter_type = src->perimeter.perimeter_type;
    dst->perimeter.outer_radius = src->perimeter.outer_radius;
    dst->perimeter.inner_radius = src->perimeter.inner_radius;
    dst->population = src->population;
    dst->max_population = src->max_population;
    dst->food_production = src->food_production;
    dst->resource_output = src->resource_output;
    dst->residential_count = src->residential_count;
    dst->production_count = src->production_count;
    dst->storage_count = src->storage_count;
    dst->defense_count = src->defense_count;
    dst->integrity = src->integrity;
    dst->last_damage_tick = src->last_damage_tick;
    dst->rebuild_start_tick = src->rebuild_start_tick;
    dst->water_access = src->water_access;
    dst->fertility = src->fertility;
    dst->defensibility = src->defensibility;
    dst->flatness = src->flatness;

    for (i = 0; i < dst->zone_count && i < 16u; ++i) {
        dst->zones[i].center_wx = src->zones[i].center_wx;
        dst->zones[i].center_wz = src->zones[i].center_wz;
        dst->zones[i].radius_x = src->zones[i].radius_x;
        dst->zones[i].radius_z = src->zones[i].radius_z;
        dst->zones[i].base_elevation = src->zones[i].base_elevation;
        dst->zones[i].terrain_modification = src->zones[i].terrain_modification;
        dst->zones[i].zone_type = src->zones[i].zone_type;
        memcpy(dst->zones[i].reserved, src->zones[i].reserved, sizeof(dst->zones[i].reserved));
    }
}

static void settlement_to_v3(SettlementMetadataV3* dst, const SettlementMetadata* src)
{
    uint32_t i;
    if (!dst || !src) return;
    
    memset(dst, 0, sizeof(*dst));
    dst->settlement_id = src->settlement_id;
    dst->type = src->type;
    dst->purpose = src->purpose;
    dst->state = src->state;
    dst->center_wx = src->center_wx;
    dst->center_wz = src->center_wz;
    dst->radius = src->radius;
    dst->geographic_variant = src->geographic_variant;
    dst->zone_count = src->zone_count;
    memcpy(dst->zones, src->zones, sizeof(dst->zones));
    dst->perimeter = src->perimeter;
    dst->population = src->population;
    dst->max_population = src->max_population;
    dst->food_production = src->food_production;
    dst->resource_output = src->resource_output;
    dst->residential_count = src->residential_count;
    dst->production_count = src->production_count;
    dst->storage_count = src->storage_count;
    dst->defense_count = src->defense_count;
    dst->integrity = src->integrity;
    dst->last_damage_tick = src->last_damage_tick;
    dst->rebuild_start_tick = src->rebuild_start_tick;
    dst->water_access = src->water_access;
    dst->fertility = src->fertility;
    dst->defensibility = src->defensibility;
    dst->flatness = src->flatness;
    
    dst->chunk_count = src->chunk_count;
    for (i = 0; i < src->chunk_count && i < SDK_MAX_CHUNKS_PER_SETTLEMENT; i++) {
        dst->chunk_coords[i * 2] = (uint8_t)src->chunks[i].cx;
        dst->chunk_coords[i * 2 + 1] = (uint8_t)src->chunks[i].cz;
    }
}

static void settlement_from_v3(SettlementMetadata* dst, const SettlementMetadataV3* src)
{
    uint32_t i;
    if (!dst || !src) return;
    
    memset(dst, 0, sizeof(*dst));
    dst->settlement_id = src->settlement_id;
    dst->type = src->type;
    dst->purpose = src->purpose;
    dst->state = src->state;
    dst->center_wx = src->center_wx;
    dst->center_wz = src->center_wz;
    dst->radius = src->radius;
    dst->geographic_variant = src->geographic_variant;
    dst->zone_count = src->zone_count;
    memcpy(dst->zones, src->zones, sizeof(dst->zones));
    dst->perimeter = src->perimeter;
    dst->population = src->population;
    dst->max_population = src->max_population;
    dst->food_production = src->food_production;
    dst->resource_output = src->resource_output;
    dst->residential_count = src->residential_count;
    dst->production_count = src->production_count;
    dst->storage_count = src->storage_count;
    dst->defense_count = src->defense_count;
    dst->integrity = src->integrity;
    dst->last_damage_tick = src->last_damage_tick;
    dst->rebuild_start_tick = src->rebuild_start_tick;
    dst->water_access = src->water_access;
    dst->fertility = src->fertility;
    dst->defensibility = src->defensibility;
    dst->flatness = src->flatness;
    
    dst->chunk_count = src->chunk_count;
    for (i = 0; i < src->chunk_count && i < SDK_MAX_CHUNKS_PER_SETTLEMENT; i++) {
        dst->chunks[i].cx = (int16_t)src->chunk_coords[i * 2];
        dst->chunks[i].cz = (int16_t)src->chunk_coords[i * 2 + 1];
    }
}

int sdk_settlement_save_superchunk(const char* world_path, int scx, int scz, const SuperchunkSettlementData* data)
{
    char filepath[512];
    FILE* f;
    uint32_t magic = SETTLEMENT_FILE_MAGIC;
    uint32_t version = SETTLEMENT_FILE_VERSION;
    uint32_t i;
    
    if (!world_path || !data) return 0;
    
    sprintf(filepath, "%s/settlements_sc%d_%d.dat", world_path, scx, scz);
    
    f = fopen(filepath, "wb");
    if (!f) return 0;
    
    fwrite(&magic, sizeof(uint32_t), 1, f);
    fwrite(&version, sizeof(uint32_t), 1, f);
    fwrite(&scx, sizeof(int32_t), 1, f);
    fwrite(&scz, sizeof(int32_t), 1, f);
    fwrite(&data->settlement_count, sizeof(uint32_t), 1, f);
    
    if (data->settlement_count > 0) {
        for (i = 0; i < data->settlement_count; i++) {
            SettlementMetadataV3 v3;
            settlement_to_v3(&v3, &data->settlements[i]);
            fwrite(&v3, sizeof(SettlementMetadataV3), 1, f);
        }
        
        fwrite(data->chunk_settlement_count, sizeof(data->chunk_settlement_count), 1, f);
        fwrite(data->chunk_settlement_indices, sizeof(data->chunk_settlement_indices), 1, f);
    }
    
    fclose(f);
    return 1;
}

int sdk_settlement_load_superchunk(const char* world_path, int scx, int scz, SuperchunkSettlementData* out_data)
{
    char filepath[512];
    FILE* f;
    uint32_t magic, version, count;
    int32_t file_scx, file_scz;
    
    if (!world_path || !out_data) return 0;
    
    sprintf(filepath, "%s/settlements_sc%d_%d.dat", world_path, scx, scz);
    
    f = fopen(filepath, "rb");
    if (!f) return 0;
    
    if (fread(&magic, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    
    if (magic != SETTLEMENT_FILE_MAGIC) {
        fclose(f);
        return 0;
    }
    
    if (fread(&version, sizeof(uint32_t), 1, f) != 1 ||
        fread(&file_scx, sizeof(int32_t), 1, f) != 1 ||
        fread(&file_scz, sizeof(int32_t), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    
    if (file_scx != scx || file_scz != scz) {
        fclose(f);
        return 0;
    }

    if (version != 1u && version != 2u && version != SETTLEMENT_FILE_VERSION) {
        fclose(f);
        return 0;
    }
    
    if (fread(&count, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    
    if (count > SDK_MAX_SETTLEMENTS_PER_SUPERCHUNK) {
        fclose(f);
        return 0;
    }
    
    out_data->superchunk_x = scx;
    out_data->superchunk_z = scz;
    out_data->settlement_count = count;
    
    if (count > 0) {
        if (version == 1u) {
            uint32_t i;
            SettlementMetadataV1 legacy[SDK_MAX_SETTLEMENTS_PER_SUPERCHUNK];

            if (fread(legacy, sizeof(SettlementMetadataV1), count, f) != count) {
                fclose(f);
                return 0;
            }
            for (i = 0; i < count; ++i) {
                upgrade_settlement_metadata_v1(&out_data->settlements[i], &legacy[i]);
            }
            memset(out_data->chunk_settlement_count, 0, sizeof(out_data->chunk_settlement_count));
            memset(out_data->chunk_settlement_indices, 0xFF, sizeof(out_data->chunk_settlement_indices));
        } else if (version == 2u) {
            uint32_t i;
            size_t v2_settlement_size = offsetof(SettlementMetadata, chunk_count);
            for (i = 0; i < count; ++i) {
                memset(&out_data->settlements[i], 0, sizeof(SettlementMetadata));
                if (fread(&out_data->settlements[i], v2_settlement_size, 1, f) != 1) {
                    fclose(f);
                    return 0;
                }
            }
            memset(out_data->chunk_settlement_count, 0, sizeof(out_data->chunk_settlement_count));
            memset(out_data->chunk_settlement_indices, 0xFF, sizeof(out_data->chunk_settlement_indices));
        } else if (version == SETTLEMENT_FILE_VERSION) {
            uint32_t i;
            for (i = 0; i < count; ++i) {
                SettlementMetadataV3 v3;
                memset(&v3, 0, sizeof(v3));
                if (fread(&v3, sizeof(SettlementMetadataV3), 1, f) != 1) {
                    fclose(f);
                    return 0;
                }
                settlement_from_v3(&out_data->settlements[i], &v3);
            }
            
            memset(out_data->chunk_settlement_count, 0, sizeof(out_data->chunk_settlement_count));
            memset(out_data->chunk_settlement_indices, 0xFF, sizeof(out_data->chunk_settlement_indices));
            
            if (fread(out_data->chunk_settlement_count, sizeof(out_data->chunk_settlement_count), 1, f) != 1 ||
                fread(out_data->chunk_settlement_indices, sizeof(out_data->chunk_settlement_indices), 1, f) != 1) {
                fclose(f);
                return 0;
            }
        } else {
            fclose(f);
            return 0;
        }
    }
    
    fclose(f);
    return 1;
}

