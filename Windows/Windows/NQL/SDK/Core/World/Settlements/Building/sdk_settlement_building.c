/**
 * sdk_settlement_building.c -- Building template definitions and generation
 */
#include "../sdk_settlement.h"
#include "../../Buildings/sdk_building_family.h"
#include "../../Blocks/sdk_block.h"
#include <stdlib.h>
#include <string.h>

#define SDK_SETTLEMENT_BUILDING_SUPPORT_MARKER_CAP 8

static const BuildingTemplate g_building_templates[] = {
    /* Residential */
    { BUILDING_TYPE_HUT,    4, 4, 3, BLOCK_LOG, BLOCK_PLANKS, 1.0f },
    { BUILDING_TYPE_HOUSE,  6, 6, 5, BLOCK_COBBLESTONE, BLOCK_PLANKS, 0.8f },
    { BUILDING_TYPE_MANOR,  10, 8, 7, BLOCK_STONE_BRICKS, BLOCK_PLANKS, 0.1f },
    
    /* Production */
    { BUILDING_TYPE_FARM,   8, 12, 3, BLOCK_PLANKS, BLOCK_LOG, 1.0f },
    { BUILDING_TYPE_BARN,   8, 8, 6, BLOCK_PLANKS, BLOCK_LOG, 0.6f },
    { BUILDING_TYPE_WORKSHOP, 7, 7, 4, BLOCK_COBBLESTONE, BLOCK_PLANKS, 0.5f },
    { BUILDING_TYPE_FORGE,  6, 6, 4, BLOCK_COBBLESTONE, BLOCK_STONE, 0.5f },
    { BUILDING_TYPE_MILL,   6, 6, 8, BLOCK_COBBLESTONE, BLOCK_PLANKS, 0.3f },
    
    /* Storage */
    { BUILDING_TYPE_STOREHOUSE, 8, 8, 5, BLOCK_PLANKS, BLOCK_LOG, 0.6f },
    { BUILDING_TYPE_WAREHOUSE,  12, 12, 6, BLOCK_COBBLESTONE, BLOCK_PLANKS, 0.4f },
    { BUILDING_TYPE_SILO,   5, 5, 10, BLOCK_COBBLESTONE, BLOCK_STONE, 0.3f },
    
    /* Defense */
    { BUILDING_TYPE_WATCHTOWER, 5, 5, 12, BLOCK_COBBLESTONE, BLOCK_STONE, 0.3f },
    { BUILDING_TYPE_BARRACKS, 10, 8, 5, BLOCK_STONE_BRICKS, BLOCK_PLANKS, 0.4f },
    { BUILDING_TYPE_WALL_SECTION, 16, 2, 6, BLOCK_COBBLESTONE, BLOCK_STONE, 0.2f },
    
    /* Special */
    { BUILDING_TYPE_WELL,   3, 3, 2, BLOCK_COBBLESTONE, BLOCK_STONE, 1.0f },
    { BUILDING_TYPE_MARKET, 12, 12, 4, BLOCK_PLANKS, BLOCK_LOG, 0.3f },
    { BUILDING_TYPE_DOCK,   8, 16, 2, BLOCK_PLANKS, BLOCK_LOG, 0.5f }
};

#define BUILDING_TEMPLATE_COUNT (sizeof(g_building_templates) / sizeof(g_building_templates[0]))

const BuildingTemplate* sdk_settlement_get_building_template(BuildingType type)
{
    size_t i;
    for (i = 0; i < BUILDING_TEMPLATE_COUNT; ++i) {
        if (g_building_templates[i].type == type) {
            return &g_building_templates[i];
        }
    }
    return NULL;
}

static void place_block_safe(SdkChunk* chunk, int lx, int ly, int lz, BlockType block)
{
    if (lx < 0 || lx >= CHUNK_WIDTH || ly < 0 || ly >= CHUNK_HEIGHT || lz < 0 || lz >= CHUNK_DEPTH) {
        return;
    }
    sdk_chunk_set_block(chunk, lx, ly, lz, block);
}

static BlockType adapt_material_to_terrain(BlockType base_material, SdkBedrockProvince bedrock)
{
    switch (bedrock) {
        case BEDROCK_PROVINCE_CRATON_GRANITE_GNEISS:
        case BEDROCK_PROVINCE_GRANITIC_INTRUSIVE:
            if (base_material == BLOCK_COBBLESTONE) return BLOCK_STONE;
            break;
        case BEDROCK_PROVINCE_CARBONATE_PLATFORM:
            if (base_material == BLOCK_COBBLESTONE) return BLOCK_STONE_BRICKS;
            break;
        case BEDROCK_PROVINCE_SILICICLASTIC_BASIN:
            if (base_material == BLOCK_STONE) return BLOCK_COBBLESTONE;
            break;
        default:
            break;
    }
    return base_material;
}

static void place_runtime_support_markers(SdkChunk* chunk, const BuildingPlacement* placement)
{
    SdkBuildingRuntimeMarker markers[SDK_SETTLEMENT_BUILDING_SUPPORT_MARKER_CAP];
    int marker_count;

    if (!chunk || !placement) return;

    marker_count = sdk_building_compute_runtime_markers(placement,
                                                        markers,
                                                        SDK_SETTLEMENT_BUILDING_SUPPORT_MARKER_CAP);
    for (int marker_index = 0; marker_index < marker_count; ++marker_index) {
        const SdkBuildingRuntimeMarker* marker = &markers[marker_index];
        int chunk_min_x;
        int chunk_min_z;
        int chunk_max_x;
        int chunk_max_z;
        int lx;
        int lz;

        if (marker->required_block == BLOCK_AIR || marker->required_block == BLOCK_WATER) {
            continue;
        }

        chunk_min_x = chunk->cx * CHUNK_WIDTH;
        chunk_min_z = chunk->cz * CHUNK_DEPTH;
        chunk_max_x = chunk_min_x + CHUNK_WIDTH - 1;
        chunk_max_z = chunk_min_z + CHUNK_DEPTH - 1;
        if (marker->wx < chunk_min_x || marker->wx > chunk_max_x ||
            marker->wz < chunk_min_z || marker->wz > chunk_max_z ||
            marker->wy < 0 || marker->wy >= CHUNK_HEIGHT) {
            continue;
        }

        lx = marker->wx - chunk_min_x;
        lz = marker->wz - chunk_min_z;
        place_block_safe(chunk, lx, marker->wy, lz, marker->required_block);
    }
}

static int should_skip_damaged_block(const BuildingPlacement* placement, int world_x, int world_y, int world_z, int damage_skip_chance)
{
    uint32_t seed;
    uint32_t roll;

    if (!placement || damage_skip_chance <= 0) return 0;

    seed = sdk_worldgen_hash2d(world_x, world_z, ((uint32_t)placement->type << 16) ^ (uint32_t)(placement->wx & 0xffff));
    roll = sdk_worldgen_hash32(seed ^ (uint32_t)world_y);
    return (int)(roll % 100u) < damage_skip_chance;
}

void sdk_settlement_generate_building(SdkChunk* chunk, const BuildingPlacement* placement, const BuildingTemplate* building_template, float integrity, SdkBedrockProvince bedrock)
{
    int wx, wy, wz, lx, ly, lz, x, y, z;
    BlockType primary, secondary;
    int damage_skip_chance;
    
    if (!chunk || !placement || !building_template) return;
    
    wx = placement->wx;
    wz = placement->wz;
    wy = placement->base_elevation + 1;
    if (wy < 1) wy = 1;
    if (wy >= CHUNK_HEIGHT) return;
    
    primary = adapt_material_to_terrain(building_template->primary_material, bedrock);
    secondary = adapt_material_to_terrain(building_template->secondary_material, bedrock);
    
    damage_skip_chance = (int)((1.0f - integrity) * 100.0f);
    
    for (x = 0; x < placement->footprint_x; ++x) {
        for (z = 0; z < placement->footprint_z; ++z) {
            int world_x = wx + x;
            int world_z = wz + z;
            
            if (world_x < chunk->cx * CHUNK_WIDTH || world_x >= (chunk->cx + 1) * CHUNK_WIDTH ||
                world_z < chunk->cz * CHUNK_DEPTH || world_z >= (chunk->cz + 1) * CHUNK_DEPTH) {
                continue;
            }
            
            lx = world_x - chunk->cx * CHUNK_WIDTH;
            lz = world_z - chunk->cz * CHUNK_DEPTH;
            
            for (y = 0; y < placement->height; ++y) {
                ly = wy + y;
                
                if (ly >= CHUNK_HEIGHT) break;
                
                if (should_skip_damaged_block(placement, world_x, ly, world_z, damage_skip_chance)) {
                    continue;
                }
                
                if (y == 0) {
                    place_block_safe(chunk, lx, ly, lz, primary);
                }
                else if (y == placement->height - 1) {
                    place_block_safe(chunk, lx, ly, lz, secondary);
                }
                else if (x == 0 || x == placement->footprint_x - 1 || z == 0 || z == placement->footprint_z - 1) {
                    place_block_safe(chunk, lx, ly, lz, primary);
                }
                else {
                    place_block_safe(chunk, lx, ly, lz, BLOCK_AIR);
                }
            }
        }
    }

    place_runtime_support_markers(chunk, placement);
}
