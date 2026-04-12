/**
 * debugger_mapping.c -- Chunk/wall mapping analysis implementation
 */
#include "debugger_mapping.h"
#include "../Core/World/Worldgen/sdk_worldgen.h"
#include "../Core/World/Chunks/sdk_chunk.h"
#include "../Core/World/Chunks/ChunkCompression/sdk_chunk_codec.h"
#include "../Core/World/ConstructionCells/sdk_construction_cells.h"
#include "../Core/World/Persistence/sdk_chunk_save_json.h"
#include "../Core/World/Persistence/sdk_persistence.h"
#include "../Core/World/Persistence/sdk_world_tooling.h"
#include "../Core/World/Blocks/sdk_block.h"
#include "../Core/World/Superchunks/Config/sdk_superchunk_config.h"
#include "../Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h"
#include "../Core/World/Worldgen/SharedCache/sdk_worldgen_shared_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper: floor division that rounds toward negative infinity */
static int floor_div_local(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return (value - denom + 1) / denom;
}

static uint8_t mapping_coordinate_system_from_legacy_flags(int superchunks_enabled,
                                                           int walls_detached)
{
    if (!superchunks_enabled) {
        return (uint8_t)SDK_WORLD_COORDSYS_CHUNK_SYSTEM;
    }
    return (uint8_t)(walls_detached
        ? SDK_WORLD_COORDSYS_GRID_AND_TERRAIN_SUPERCHUNK_SYSTEM
        : SDK_WORLD_COORDSYS_SUPERCHUNK_SYSTEM);
}

static int normalize_wall_grid_size_local(int chunk_span, bool walls_detached, int wall_grid_size)
{
    int default_size = chunk_span + 2;
    if (!walls_detached) return chunk_span;
    if (wall_grid_size <= 2 || wall_grid_size < default_size) return default_size;
    return wall_grid_size;
}

static int wall_period_local(int chunk_span, bool walls_detached, int wall_grid_size)
{
    if (walls_detached) {
        return normalize_wall_grid_size_local(chunk_span, walls_detached, wall_grid_size) - 1;
    }
    return chunk_span + 1;
}

static void get_wall_side_info(int cx, int cz, int chunk_span, bool walls_detached,
                               int wall_grid_size, int offset_x, int offset_z,
                               bool* is_west, bool* is_east, bool* is_north, bool* is_south,
                               const char** side_name)
{
    const int period = wall_period_local(chunk_span, walls_detached, wall_grid_size);
    const int local_x = sdk_superchunk_floor_mod_i(cx - offset_x, period);
    const int local_z = sdk_superchunk_floor_mod_i(cz - offset_z, period);
    const int prev_local_x = sdk_superchunk_floor_mod_i(cx - offset_x - 1, period);
    const int prev_local_z = sdk_superchunk_floor_mod_i(cz - offset_z - 1, period);

    *is_west = (local_x == 0);
    *is_east = (prev_local_x == 0);
    *is_north = (local_z == 0);
    *is_south = (prev_local_z == 0);

    if (*is_west && *is_north) *side_name = "CORNER_NW";
    else if (*is_east && *is_north) *side_name = "CORNER_NE";
    else if (*is_west && *is_south) *side_name = "CORNER_SW";
    else if (*is_east && *is_south) *side_name = "CORNER_SE";
    else if (*is_west) *side_name = "WEST";
    else if (*is_east) *side_name = "EAST";
    else if (*is_north) *side_name = "NORTH";
    else if (*is_south) *side_name = "SOUTH";
    else *side_name = "NONE";
}

int analyze_chunk_mapping(int chunk_span, bool walls_detached, int wall_grid_size,
                         int offset_x, int offset_z, int range,
                         ChunkMappingResult* results, int max_results)
{
    int count = 0;
    int cx, cz;
    
    for (cz = -range; cz <= range && count < max_results; ++cz) {
        for (cx = -range; cx <= range && count < max_results; ++cx) {
            int period = wall_period_local(chunk_span, walls_detached, wall_grid_size);
            int scx = floor_div_local(cx - offset_x, period);
            int scz = floor_div_local(cz - offset_z, period);
            int origin_cx = scx * period + offset_x;
            int origin_cz = scz * period + offset_z;
            
            results[count].cx = cx;
            results[count].cz = cz;
            results[count].scx = scx;
            results[count].scz = scz;
            results[count].origin_cx = origin_cx;
            results[count].origin_cz = origin_cz;
            
            results[count].is_wall =
                (sdk_superchunk_floor_mod_i(cx - offset_x, period) == 0) ||
                (sdk_superchunk_floor_mod_i(cz - offset_z, period) == 0);
            
            get_wall_side_info(cx, cz, chunk_span, walls_detached,
                              wall_grid_size, offset_x, offset_z,
                              &results[count].is_west,
                              &results[count].is_east,
                              &results[count].is_north,
                              &results[count].is_south,
                              &results[count].side_name);
            
            count++;
        }
    }
    
    return count;
}

void calculate_statistics(const ChunkMappingResult* results, int count,
                          MappingStatistics* out_stats)
{
    int i;
    
    memset(out_stats, 0, sizeof(MappingStatistics));
    out_stats->total_chunks = count;
    
    for (i = 0; i < count; ++i) {
        if (results[i].is_wall) {
            out_stats->wall_chunks++;
            if (results[i].is_west) out_stats->west_wall_chunks++;
            if (results[i].is_east) out_stats->east_wall_chunks++;
            if (results[i].is_north) out_stats->north_wall_chunks++;
            if (results[i].is_south) out_stats->south_wall_chunks++;
            
            /* Count corners (chunks with two wall sides) */
            int side_count = (results[i].is_west ? 1 : 0) +
                           (results[i].is_east ? 1 : 0) +
                           (results[i].is_north ? 1 : 0) +
                           (results[i].is_south ? 1 : 0);
            if (side_count >= 2) {
                out_stats->corner_chunks++;
            }
        } else {
            out_stats->superchunk_chunks++;
        }
    }
}

void generate_ascii_grid(const ChunkMappingResult* results, int count,
                         int range, char* output, int output_size)
{
    int grid_size = range * 2 + 1;
    char* grid;
    int cx, cz, i;
    int pos = 0;
    
    if (output_size < grid_size * grid_size * 2 + 100) {
        snprintf(output, output_size, "ERROR: Output buffer too small");
        return;
    }
    
    /* Allocate temporary grid */
    grid = (char*)calloc(grid_size * grid_size, sizeof(char));
    if (!grid) {
        snprintf(output, output_size, "ERROR: Memory allocation failed");
        return;
    }
    
    /* Fill grid with '.' for superchunk chunks */
    memset(grid, '.', grid_size * grid_size);
    
    /* Mark wall chunks */
    for (i = 0; i < count; ++i) {
        int gx = results[i].cx + range;
        int gz = results[i].cz + range;
        int idx = gz * grid_size + gx;
        
        if (gx >= 0 && gx < grid_size && gz >= 0 && gz < grid_size) {
            if (results[i].is_wall) {
                /* Check for corner */
                int side_count = (results[i].is_west ? 1 : 0) +
                               (results[i].is_east ? 1 : 0) +
                               (results[i].is_north ? 1 : 0) +
                               (results[i].is_south ? 1 : 0);
                if (side_count >= 2) {
                    grid[idx] = '+';
                } else {
                    grid[idx] = 'W';
                }
            }
        }
    }
    
    /* Generate output */
    pos += snprintf(output + pos, output_size - pos, "ASCII Grid (range -%d to +%d):\n", range, range);
    pos += snprintf(output + pos, output_size - pos, "Legend: . = superchunk, W = wall, + = corner\n\n");
    
    for (cz = 0; cz < grid_size; ++cz) {
        for (cx = 0; cx < grid_size; ++cx) {
            if (pos < output_size - 2) {
                output[pos++] = grid[cz * grid_size + cx];
                output[pos++] = ' ';
            }
        }
        if (pos < output_size - 2) {
            output[pos++] = '\n';
        }
    }
    output[pos] = '\0';
    
    free(grid);
}

int export_csv(const ChunkMappingResult* results, int count, const char* filepath)
{
    FILE* file;
    int i;
    
    file = fopen(filepath, "w");
    if (!file) {
        return 0;
    }
    
    /* Write header */
    fprintf(file, "cx,cz,scx,scz,origin_cx,origin_cz,is_wall,is_west,is_east,is_north,is_south,side_name\n");
    
    /* Write data */
    for (i = 0; i < count; ++i) {
        fprintf(file, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s\n",
                results[i].cx,
                results[i].cz,
                results[i].scx,
                results[i].scz,
                results[i].origin_cx,
                results[i].origin_cz,
                results[i].is_wall ? 1 : 0,
                results[i].is_west ? 1 : 0,
                results[i].is_east ? 1 : 0,
                results[i].is_north ? 1 : 0,
                results[i].is_south ? 1 : 0,
                results[i].side_name);
    }
    
    fclose(file);
    return 1;
}

void print_summary_statistics(const MappingStatistics* stats, int chunk_span,
                              bool walls_detached, int wall_grid_size,
                              int offset_x, int offset_z)
{
    printf("\n=== Configuration ===\n");
    printf("  Chunk Span: %d\n", chunk_span);
    printf("  Walls Detached: %s\n", walls_detached ? "true" : "false");
    printf("  Wall Grid Size: %d\n", wall_grid_size);
    printf("  Offset X: %d\n", offset_x);
    printf("  Offset Z: %d\n", offset_z);
    
    printf("\n=== Mapping Statistics ===\n");
    printf("  Total chunks analyzed: %d\n", stats->total_chunks);
    printf("  Wall chunks: %d\n", stats->wall_chunks);
    printf("  Superchunk chunks: %d\n", stats->superchunk_chunks);
    printf("\n  Wall chunk breakdown:\n");
    printf("    West walls: %d\n", stats->west_wall_chunks);
    printf("    East walls: %d\n", stats->east_wall_chunks);
    printf("    North walls: %d\n", stats->north_wall_chunks);
    printf("    South walls: %d\n", stats->south_wall_chunks);
    printf("    Corners: %d\n", stats->corner_chunks);
    
    printf("\n=== Key Finding ===\n");
    printf("  Runtime wall placement uses period %d\n",
           wall_period_local(chunk_span, walls_detached, wall_grid_size));
    printf("  Detached wall offsets shift boundaries by (%d,%d)\n", offset_x, offset_z);
    printf("  Terrain grouping and wall grouping now share one normalized detached-wall interpretation.\n");
}

/**
 * Create a detached-wall preview visualization.
 * This remains available as a tooling preview for the preserved detached
 * configuration fields, but gameplay/runtime wall placement now follows the
 * shared superchunk boundary model at chunk_span + 1.
 */
int create_world_theoretical(int wall_grid_size, int offset_x, int offset_z,
                              int range, char* output, int output_size)
{
    int grid_size = range * 2 + 1;
    char* grid;
    int cx, cz;
    int pos = 0;
    int period = wall_grid_size - 1;  /* period = wall_grid_size - 1 */
    int superchunk_size = wall_grid_size - 2;  /* superchunk_size = wall_grid_size - 2 */
    
    if (wall_grid_size < 3) {
        snprintf(output, output_size, "ERROR: wall_grid_size must be at least 3");
        return 0;
    }
    
    if (output_size < grid_size * grid_size * 2 + 200) {
        snprintf(output, output_size, "ERROR: Output buffer too small");
        return 0;
    }
    
    /* Allocate temporary grid */
    grid = (char*)calloc(grid_size * grid_size, sizeof(char));
    if (!grid) {
        snprintf(output, output_size, "ERROR: Memory allocation failed");
        return 0;
    }
    
    /* Fill grid with '.' for superchunk interior */
    memset(grid, '.', grid_size * grid_size);
    
    /* Mark walls using period = wall_grid_size - 1 */
    /* Walls are at positions where coord % period == 0 */
    /* Superchunk interior is positions 1 to (period - 1) */
    /* This creates a grid pattern with walls at regular intervals */
    for (cz = -range; cz <= range; ++cz) {
        for (cx = -range; cx <= range; ++cx) {
            int gx = cx + range;
            int gz = cz + range;
            int idx = gz * grid_size + gx;
            
            if (gx >= 0 && gx < grid_size && gz >= 0 && gz < grid_size) {
                int adjusted_cx = cx - offset_x;
                int adjusted_cz = cz - offset_z;
                
                /* Check if this is a wall position */
                /* Walls at position 0 modulo period */
                int mod_x = ((adjusted_cx % period) + period) % period;
                int mod_z = ((adjusted_cz % period) + period) % period;
                
                bool is_wall_x = (mod_x == 0);
                bool is_wall_z = (mod_z == 0);
                
                /* Mark as wall if BOTH x and z are at wall positions (grid intersection) */
                /* OR if x is at wall position (vertical wall line) */
                /* OR if z is at wall position (horizontal wall line) */
                /* This creates a grid pattern */
                if (is_wall_x && is_wall_z) {
                    grid[idx] = '+';  /* Corner where walls intersect */
                } else if (is_wall_x) {
                    grid[idx] = 'W';  /* Vertical wall line */
                } else if (is_wall_z) {
                    grid[idx] = 'W';  /* Horizontal wall line */
                }
            }
        }
    }
    
    /* Generate output */
    pos += snprintf(output + pos, output_size - pos, "=== DETACHED WALL PREVIEW (wall_grid_size=%d) ===\n", wall_grid_size);
    pos += snprintf(output + pos, output_size - pos, "Period: %d chunks (walls at position 0)\n", period);
    pos += snprintf(output + pos, output_size - pos, "Superchunk: %dx%d chunks (positions 1 to %d)\n", superchunk_size, superchunk_size, period - 1);
    pos += snprintf(output + pos, output_size - pos, "Offset: (%d, %d)\n", offset_x, offset_z);
    pos += snprintf(output + pos, output_size - pos,
                    "Note: detached mode preview uses the same wall period/offset interpretation as the current tooling.\n");
    pos += snprintf(output + pos, output_size - pos, "ASCII Grid (range -%d to +%d):\n", range, range);
    pos += snprintf(output + pos, output_size - pos, "Legend: . = superchunk interior, W = wall, + = corner\n\n");
    
    for (cz = 0; cz < grid_size; ++cz) {
        for (cx = 0; cx < grid_size; ++cx) {
            if (pos < output_size - 2) {
                output[pos++] = grid[cz * grid_size + cx];
                output[pos++] = ' ';
            }
        }
        if (pos < output_size - 2) {
            output[pos++] = '\n';
        }
    }
    output[pos] = '\0';
    
    free(grid);
    return 1;
}

/* ========== Real World Generation (uses SDK worldgen + shared codec save format) ========== */

int generate_real_world(uint32_t seed, int gen_distance_superchunks,
                        int chunk_span, bool walls_enabled, bool walls_detached,
                        int wall_grid_size, int offset_x, int offset_z,
                        bool construction_cells_enabled,
                        const char* output_path, char* output, int output_size)
{
    int pos = 0;
    int half_range, preview_range, total_generated = 0;
    int period = chunk_span + 1;
    int cx, cz, i, j;
    char save_path[512];
    FILE* save_file;
    SdkSuperchunkConfig sc_config;
    SdkChunk chunk;
    SdkWorldDesc world_desc;
    SdkWorldGen worldgen;
    SdkWorldCreateRequest create_request;
    SdkWorldSaveMeta generated_meta;
    SdkWorldTarget generated_target;
    SdkConstructionArchetypeRegistry* construction_registry = NULL;
    char* encoded_construction_registry = NULL;

    typedef struct {
        int cx;
        int cz;
        int top_y;
        int payload_version;
        char* codec;
        char* payload_b64;
    } ChunkEntry;
    typedef struct { int origin_cx, origin_cz; ChunkEntry* chunks; int count, cap; } SCEntry;

    ChunkEntry* wall_chunks = NULL;
    int wall_count = 0, wall_cap = 0;
    SCEntry* superchunks = NULL;
    int sc_count = 0, sc_cap = 0;

    memset(&create_request, 0, sizeof(create_request));
    memset(&generated_meta, 0, sizeof(generated_meta));
    memset(&generated_target, 0, sizeof(generated_target));
    create_request.seed = seed;
    create_request.render_distance_chunks = gen_distance_superchunks * chunk_span;
    create_request.settlements_enabled = true;
    create_request.construction_cells_enabled = construction_cells_enabled ? true : false;
    create_request.coordinate_system =
        mapping_coordinate_system_from_legacy_flags(1, walls_detached ? 1 : 0);
    create_request.superchunks_enabled = true;
    create_request.superchunk_chunk_span = chunk_span;
    create_request.walls_enabled = walls_enabled ? true : false;
    create_request.wall_grid_size = wall_grid_size;
    create_request.wall_grid_offset_x = offset_x;
    create_request.wall_grid_offset_z = offset_z;
    strncpy_s(create_request.display_name, sizeof(create_request.display_name), "Generated World", _TRUNCATE);

    if (!sdk_world_target_resolve(NULL, output_path, &generated_target)) {
        return 0;
    }
    if (!sdk_world_ensure_directory_exists(generated_target.world_dir)) {
        return 0;
    }
    strncpy_s(create_request.folder_id, sizeof(create_request.folder_id), generated_target.folder_id, _TRUNCATE);
    sdk_world_apply_create_request_to_meta(&create_request, &generated_meta);
    strncpy_s(generated_meta.folder_id, sizeof(generated_meta.folder_id), generated_target.folder_id, _TRUNCATE);
    strncpy_s(generated_meta.save_path, sizeof(generated_meta.save_path), generated_target.save_path, _TRUNCATE);
    sdk_world_save_meta_file(&generated_meta);
    strncpy_s(save_path, sizeof(save_path), generated_target.save_path, _TRUNCATE);
    sdk_world_meta_to_world_desc(&generated_meta, &world_desc);

    /* Show preview using theoretical world */
    preview_range = gen_distance_superchunks * chunk_span / 2;
    if (preview_range < 17) preview_range = 17;
    create_world_theoretical(wall_grid_size, offset_x, offset_z, preview_range, output, output_size);

    pos += snprintf(output + pos, output_size - pos, "\nGenerating real world (SDK worldgen)...\n");
    pos += snprintf(output + pos, output_size - pos, "Seed: %u, Output: %s\n", seed, output_path);

    /* Configure superchunk geometry */
    sc_config.coordinate_system = create_request.coordinate_system;
    sc_config.enabled = true;
    sc_config.chunk_span = chunk_span;
    sc_config.walls_enabled = walls_enabled;
    sc_config.wall_grid_size = wall_grid_size;
    sc_config.wall_grid_offset_x = offset_x;
    sc_config.wall_grid_offset_z = offset_z;
    sdk_superchunk_set_config(&sc_config);

    /* Initialize worldgen shared cache (required before any worldgen calls) */
    sdk_worldgen_shared_cache_init();
    memset(&worldgen, 0, sizeof(worldgen));
    sdk_worldgen_init_ex(&worldgen, &world_desc, SDK_WORLDGEN_CACHE_NONE);
    if (!worldgen.impl) {
        return 0;
    }
    if (construction_cells_enabled) {
        construction_registry = sdk_construction_registry_create();
        if (!construction_registry) {
            sdk_worldgen_shutdown(&worldgen);
            return 0;
        }
    }

    /* Generate chunks using the real SDK worldgen */
    half_range = (gen_distance_superchunks * period) / 2;
    printf("Generating %dx%d chunks [-%d..%d]...\n", half_range*2+1, half_range*2+1, half_range, half_range);
    fflush(stdout);

    for (cz = -half_range; cz <= half_range; cz++) {
        for (cx = -half_range; cx <= half_range; cx++) {
            int top_y;
            int payload_version = SDK_CHUNK_CODEC_PAYLOAD_VERSION;
            char* codec = NULL;
            char* payload_b64 = NULL;
            int scx, scz, origin_cx, origin_cz;
            int is_wall;

            sdk_chunk_init(&chunk, (int16_t)cx, (int16_t)cz, construction_registry);
            if (!chunk.blocks) continue;

            sdk_worldgen_generate_chunk_ctx(&worldgen, &chunk);
            sdk_chunk_codec_encode_auto(&chunk, &codec, &payload_b64, &payload_version, &top_y);
            sdk_chunk_free(&chunk);

            if (!codec || !payload_b64) {
                free(codec);
                free(payload_b64);
                continue;
            }
            total_generated++;
            if (total_generated % 50 == 0) { printf("  %d chunks...\n", total_generated); fflush(stdout); }

            /* Determine wall vs terrain using the shared gameplay wall model. */
            {
                scx = floor_div_local(cx, period);
                scz = floor_div_local(cz, period);
                origin_cx = scx * period;
                origin_cz = scz * period;
                is_wall = walls_enabled &&
                          (sdk_superchunk_floor_mod_i(cx, period) == 0 ||
                           sdk_superchunk_floor_mod_i(cz, period) == 0);
            }

            if (is_wall) {
                if (wall_count >= wall_cap) {
                    int nc = wall_cap ? wall_cap * 2 : 64;
                    ChunkEntry* na = (ChunkEntry*)realloc(wall_chunks, nc * sizeof(ChunkEntry));
                    if (!na) { free(codec); free(payload_b64); continue; }
                    wall_chunks = na; wall_cap = nc;
                }
                wall_chunks[wall_count].cx = cx;
                wall_chunks[wall_count].cz = cz;
                wall_chunks[wall_count].top_y = top_y;
                wall_chunks[wall_count].payload_version = payload_version;
                wall_chunks[wall_count].codec = codec;
                wall_chunks[wall_count].payload_b64 = payload_b64;
                wall_count++;
            } else {
                /* Find or create superchunk entry */
                int found = -1;
                for (i = 0; i < sc_count; i++) {
                    if (superchunks[i].origin_cx == origin_cx && superchunks[i].origin_cz == origin_cz) { found = i; break; }
                }
                if (found < 0) {
                    if (sc_count >= sc_cap) {
                        int nc = sc_cap ? sc_cap * 2 : 16;
                        SCEntry* na = (SCEntry*)realloc(superchunks, nc * sizeof(SCEntry));
                        if (!na) { free(codec); free(payload_b64); continue; }
                        superchunks = na; sc_cap = nc;
                    }
                    found = sc_count++;
                    superchunks[found].origin_cx = origin_cx;
                    superchunks[found].origin_cz = origin_cz;
                    superchunks[found].chunks = NULL;
                    superchunks[found].count = 0;
                    superchunks[found].cap = 0;
                }
                if (superchunks[found].count >= superchunks[found].cap) {
                    int nc = superchunks[found].cap ? superchunks[found].cap * 2 : 64;
                    ChunkEntry* na = (ChunkEntry*)realloc(superchunks[found].chunks, nc * sizeof(ChunkEntry));
                    if (!na) { free(codec); free(payload_b64); continue; }
                    superchunks[found].chunks = na; superchunks[found].cap = nc;
                }
                j = superchunks[found].count++;
                superchunks[found].chunks[j].cx = cx;
                superchunks[found].chunks[j].cz = cz;
                superchunks[found].chunks[j].top_y = top_y;
                superchunks[found].chunks[j].payload_version = payload_version;
                superchunks[found].chunks[j].codec = codec;
                superchunks[found].chunks[j].payload_b64 = payload_b64;
            }
        }
    }

    printf("Generated %d chunks (%d wall, rest terrain)\n", total_generated, wall_count);
    fflush(stdout);

    /* Write save.json */
    snprintf(save_path, sizeof(save_path), "%s/save.json", output_path);
    save_file = fopen(save_path, "wb");
    if (!save_file) {
        pos += snprintf(output + pos, output_size - pos, "ERROR: Failed to create save.json\n");
        /* cleanup */
        for (i = 0; i < wall_count; i++) {
            free(wall_chunks[i].codec);
            free(wall_chunks[i].payload_b64);
        }
        free(wall_chunks);
        for (i = 0; i < sc_count; i++) {
            for (j = 0; j < superchunks[i].count; j++) {
                free(superchunks[i].chunks[j].codec);
                free(superchunks[i].chunks[j].payload_b64);
            }
            free(superchunks[i].chunks);
        }
        free(superchunks);
        sdk_construction_registry_free(construction_registry);
        sdk_worldgen_shutdown(&worldgen);
        return 0;
    }

    if (construction_registry) {
        encoded_construction_registry = sdk_construction_encode_registry(construction_registry);
    }

    fprintf(save_file, "{\n  \"version\": 7,\n  \"worldgen_revision\": %d,\n", SDK_PERSISTENCE_WORLDGEN_REVISION);
    fprintf(save_file, "  \"world\": {\"seed\": %u, \"sea_level\": 192, \"macro_cell_size\": 32},\n", seed);
    fprintf(save_file, "  \"construction_archetypes\": \"%s\",\n",
            encoded_construction_registry ? encoded_construction_registry : "");
    fprintf(save_file, "  \"player\": {\"pos\": [0,200,0], \"spawn\": [0,200,0], \"yaw\": 0, \"pitch\": 0, ");
    fprintf(save_file, "\"health\": 100, \"hunger\": 100, \"selected_hotbar\": 0, \"chunk_grid_size\": 16, ");
    fprintf(save_file, "\"level\": 1, \"xp\": 0, \"xp_to_next\": 100, \"selected_character_id\": \"0\"},\n");
    fprintf(save_file, "  \"hotbar\": [[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0],[0,0,0,0]],\n");
    fprintf(save_file, "  \"station_states\": [],\n");

    /* terrain_superchunks */
    fprintf(save_file, "  \"terrain_superchunks\": [\n");
    for (i = 0; i < sc_count; i++) {
        fprintf(save_file, "    %s{\"origin_cx\": %d, \"origin_cz\": %d, \"chunk_span\": %d, \"chunks\": [\n",
                i ? "," : "", superchunks[i].origin_cx, superchunks[i].origin_cz, chunk_span);
        for (j = 0; j < superchunks[i].count; j++) {
            SdkChunkSaveJsonEntry entry;
            sdk_chunk_save_json_entry_init(&entry);
            entry.cx = superchunks[i].chunks[j].cx;
            entry.cz = superchunks[i].chunks[j].cz;
            entry.top_y = superchunks[i].chunks[j].top_y;
            entry.payload_version = superchunks[i].chunks[j].payload_version;
            entry.codec = superchunks[i].chunks[j].codec;
            entry.payload_b64 = superchunks[i].chunks[j].payload_b64;
            sdk_chunk_save_json_write_entry(save_file,
                                            (j == 0) ? "      " : "      ,",
                                            &entry,
                                            SDK_CHUNK_CODEC_PAYLOAD_VERSION);
            free(superchunks[i].chunks[j].codec);
            free(superchunks[i].chunks[j].payload_b64);
        }
        fprintf(save_file, "    ]}%s\n", (i < sc_count - 1) ? "," : "");
        free(superchunks[i].chunks);
    }
    fprintf(save_file, "  ],\n");
    free(superchunks);

    /* wall_chunks */
    fprintf(save_file, "  \"wall_chunks\": [\n");
    for (i = 0; i < wall_count; i++) {
        SdkChunkSaveJsonEntry entry;
        sdk_chunk_save_json_entry_init(&entry);
        entry.cx = wall_chunks[i].cx;
        entry.cz = wall_chunks[i].cz;
        entry.top_y = wall_chunks[i].top_y;
        entry.payload_version = wall_chunks[i].payload_version;
        entry.codec = wall_chunks[i].codec;
        entry.payload_b64 = wall_chunks[i].payload_b64;
        sdk_chunk_save_json_write_entry(save_file,
                                        (i == 0) ? "    " : "    ,",
                                        &entry,
                                        SDK_CHUNK_CODEC_PAYLOAD_VERSION);
        free(wall_chunks[i].codec);
        free(wall_chunks[i].payload_b64);
    }
    fprintf(save_file, "  ]\n}\n");
    free(wall_chunks);
    fclose(save_file);
    free(encoded_construction_registry);

    pos += snprintf(output + pos, output_size - pos, "Created save.json: %d superchunks, %d wall chunks\n", sc_count, wall_count);
    pos += snprintf(output + pos, output_size - pos, "World generation complete!\n");
    sdk_construction_registry_free(construction_registry);
    sdk_worldgen_shutdown(&worldgen);
    return 1;
}
