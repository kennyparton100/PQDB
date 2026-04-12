#include "../Internal/sdk_worldgen_internal.h"
#include "sdk_worldgen_construction_cells.h"
#include "sdk_worldgen_terrain_edge_cells.h"
#include "../../Settlements/sdk_settlement.h"
#include "../../ConstructionCells/sdk_construction_cells.h"
#include "../../Buildings/sdk_building_family.h"
#include "../../Blocks/sdk_block.h"

#define SDK_WORLDGEN_CONSTRUCTION_MARKER_CAP 8

static int building_overlaps_chunk(const BuildingPlacement* placement, const SdkChunk* chunk)
{
    int chunk_min_x;
    int chunk_min_z;
    int chunk_max_x;
    int chunk_max_z;
    int building_max_x;
    int building_max_z;

    if (!placement || !chunk) return 0;

    chunk_min_x = chunk->cx * CHUNK_WIDTH;
    chunk_min_z = chunk->cz * CHUNK_DEPTH;
    chunk_max_x = chunk_min_x + CHUNK_WIDTH;
    chunk_max_z = chunk_min_z + CHUNK_DEPTH;
    building_max_x = placement->wx + placement->footprint_x;
    building_max_z = placement->wz + placement->footprint_z;

    if (building_max_x <= chunk_min_x || placement->wx >= chunk_max_x) return 0;
    if (building_max_z <= chunk_min_z || placement->wz >= chunk_max_z) return 0;
    return 1;
}

static BlockType construction_material_for_marker(const BuildingTemplate* building_template,
                                                  const SdkBuildingRuntimeMarker* marker)
{
    if (marker && marker->required_block != BLOCK_AIR && marker->required_block != BLOCK_WATER) {
        return marker->required_block;
    }
    if (!building_template) return BLOCK_PLANKS;

    switch ((SdkBuildingMarkerType)(marker ? marker->marker_type : SDK_BUILDING_MARKER_NONE)) {
        case SDK_BUILDING_MARKER_STORAGE:
            return building_template->secondary_material;
        case SDK_BUILDING_MARKER_PATROL:
            return BLOCK_STONE_BRICKS;
        case SDK_BUILDING_MARKER_STATION:
        case SDK_BUILDING_MARKER_WORK:
            return building_template->secondary_material;
        case SDK_BUILDING_MARKER_SLEEP:
        case SDK_BUILDING_MARKER_ENTRANCE:
        case SDK_BUILDING_MARKER_WATER:
        case SDK_BUILDING_MARKER_NONE:
        default:
            return building_template->primary_material;
    }
}

static void construction_marker_box_dims(SdkBuildingMarkerType marker_type,
                                         int* out_width,
                                         int* out_height,
                                         int* out_depth)
{
    int width = 6;
    int height = 6;
    int depth = 6;

    switch (marker_type) {
        case SDK_BUILDING_MARKER_ENTRANCE:
            width = 10;
            height = 6;
            depth = 4;
            break;
        case SDK_BUILDING_MARKER_STORAGE:
            width = 12;
            height = 10;
            depth = 12;
            break;
        case SDK_BUILDING_MARKER_PATROL:
            width = 5;
            height = 12;
            depth = 5;
            break;
        case SDK_BUILDING_MARKER_SLEEP:
            width = 12;
            height = 4;
            depth = 8;
            break;
        case SDK_BUILDING_MARKER_STATION:
            width = 8;
            height = 8;
            depth = 8;
            break;
        case SDK_BUILDING_MARKER_WORK:
            width = 10;
            height = 8;
            depth = 10;
            break;
        case SDK_BUILDING_MARKER_WATER:
        case SDK_BUILDING_MARKER_NONE:
        default:
            break;
    }

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    if (out_depth) *out_depth = depth;
}

static int place_marker_construction_cell(SdkChunk* chunk,
                                          const BuildingTemplate* building_template,
                                          const SdkBuildingRuntimeMarker* marker)
{
    SdkConstructionItemPayload payload;
    BlockType material;
    int lx;
    int lz;
    int width;
    int height;
    int depth;

    if (!chunk || !building_template || !marker || !chunk->construction_registry) {
        return 0;
    }
    if (marker->wy < 0 || marker->wy >= CHUNK_HEIGHT) {
        return 0;
    }

    lx = marker->wx - chunk->cx * CHUNK_WIDTH;
    lz = marker->wz - chunk->cz * CHUNK_DEPTH;
    if (lx < 0 || lx >= CHUNK_WIDTH || lz < 0 || lz >= CHUNK_DEPTH) {
        return 0;
    }
    if (sdk_chunk_get_block(chunk, lx, marker->wy, lz) != BLOCK_AIR) {
        return 0;
    }
    if (sdk_construction_chunk_cell_has_occupancy(chunk, lx, marker->wy, lz)) {
        return 0;
    }

    material = construction_material_for_marker(building_template, marker);
    if (material == BLOCK_AIR || material == BLOCK_WATER) {
        return 0;
    }

    construction_marker_box_dims((SdkBuildingMarkerType)marker->marker_type,
                                 &width, &height, &depth);
    sdk_construction_payload_make_box(material, width, height, depth, &payload);
    return sdk_construction_chunk_set_cell_payload(chunk,
                                                   lx,
                                                   marker->wy,
                                                   lz,
                                                   material,
                                                   payload.occupancy);
}

static void generate_building_construction_cells(SdkChunk* chunk,
                                                 const BuildingPlacement* placement,
                                                 const BuildingTemplate* building_template)
{
    SdkBuildingRuntimeMarker markers[SDK_WORLDGEN_CONSTRUCTION_MARKER_CAP];
    int marker_count;

    if (!chunk || !placement || !building_template) return;

    marker_count = sdk_building_compute_runtime_markers(placement,
                                                        markers,
                                                        SDK_WORLDGEN_CONSTRUCTION_MARKER_CAP);
    for (int marker_index = 0; marker_index < marker_count; ++marker_index) {
        place_marker_construction_cell(chunk, building_template, &markers[marker_index]);
    }
}

void generate_world_cells(SdkWorldGen* wg, SdkChunk* chunk)
{
    SuperchunkSettlementData* settlement_data;

    if (!wg || !chunk) return;
    if (!wg->impl || !((SdkWorldGenImpl*)wg->impl)->construction_cells_enabled) return;
    if (!chunk->construction_registry) return;

    generate_terrain_edge_cells(wg, chunk);

    settlement_data = sdk_settlement_get_or_create_data(wg, chunk->cx, chunk->cz);
    if (!settlement_data) return;

    for (uint32_t settlement_index = 0; settlement_index < settlement_data->settlement_count; ++settlement_index) {
        SettlementMetadata* settlement = &settlement_data->settlements[settlement_index];
        SettlementLayout* layout = sdk_settlement_generate_layout(wg, settlement);

        if (!layout) {
            continue;
        }

        for (uint32_t building_index = 0; building_index < layout->building_count; ++building_index) {
            const BuildingPlacement* placement = &layout->buildings[building_index];
            const BuildingTemplate* building_template;

            if (!building_overlaps_chunk(placement, chunk)) {
                continue;
            }

            building_template = sdk_settlement_get_building_template(placement->type);
            if (!building_template) {
                continue;
            }

            generate_building_construction_cells(chunk, placement, building_template);
        }

        sdk_settlement_free_layout(layout);
    }
}
