#include "debugger_cli.h"
#include "debugger_mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAX_RESULTS 10000
#define ASCII_GRID_BUFFER_SIZE 50000
#define DEFAULT_CHUNK_SPAN 16
#define DEFAULT_WALLS_ENABLED 1
#define DEFAULT_WALLS_DETACHED 1
#define DEFAULT_WALL_GRID_SIZE 18
#define DEFAULT_OFFSET_X 0
#define DEFAULT_OFFSET_Z 0

void debugger_print_walls_usage(const char* program_name)
{
    printf("Usage:\n");
    printf("  %s walls map [options]\n", program_name);
}

static void print_detailed_results(const ChunkMappingResult* results, int count)
{
    int i;

    printf("\n=== Detailed Chunk Analysis (first 50 wall chunks) ===\n");
    printf("Chunk (cx,cz) -> SC (scx,scz) Origin (ocx,ocz) -> Type\n");
    printf("--------------------------------------------------------\n");

    for (i = 0; i < count && i < 50; ++i) {
        if (results[i].is_wall) {
            printf("(%4d,%4d) -> (%4d,%4d) Origin (%4d,%4d) -> WALL %s\n",
                   results[i].cx, results[i].cz,
                   results[i].scx, results[i].scz,
                   results[i].origin_cx, results[i].origin_cz,
                   results[i].side_name);
        }
    }
}

static int debugger_run_walls_map_mode(int argc, char** argv)
{
    int chunk_span = DEFAULT_CHUNK_SPAN;
    int walls_enabled = DEFAULT_WALLS_ENABLED;
    int walls_detached = DEFAULT_WALLS_DETACHED;
    int wall_grid_size = DEFAULT_WALL_GRID_SIZE;
    int offset_x = DEFAULT_OFFSET_X;
    int offset_z = DEFAULT_OFFSET_Z;
    int range = 34;
    const char* output_path = NULL;
    int visualize = 0;
    int create_world = 0;
    int create_world_size = DEFAULT_WALL_GRID_SIZE;
    int create_world_offset_x = DEFAULT_OFFSET_X;
    int create_world_offset_z = DEFAULT_OFFSET_Z;
    int create_world_range = 17;
    int generate_world = 0;
    int json = 0;
    int gen_distance_superchunks = 2;
    uint32_t seed = 12345;
    int construction_cells_enabled = 0;
    char gen_output_path[512] = {0};
    int i;
    ChunkMappingResult results[MAX_RESULTS];
    MappingStatistics stats;
    int result_count;

    for (i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            debugger_print_walls_usage("nql_debug");
            return 0;
        } else if (strcmp(argv[i], "--chunk-span") == 0 && i + 1 < argc) {
            chunk_span = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--walls-detached") == 0 && i + 1 < argc) {
            walls_detached = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--wall-grid-size") == 0 && i + 1 < argc) {
            wall_grid_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--offset-x") == 0 && i + 1 < argc) {
            offset_x = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--offset-z") == 0 && i + 1 < argc) {
            offset_z = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--range") == 0 && i + 1 < argc) {
            range = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--visualize") == 0) {
            visualize = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "--create-world") == 0) {
            create_world = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                create_world_size = atoi(argv[++i]);
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    create_world_offset_x = atoi(argv[++i]);
                    if (i + 1 < argc && argv[i + 1][0] != '-') {
                        create_world_offset_z = atoi(argv[++i]);
                        if (i + 1 < argc && argv[i + 1][0] != '-') {
                            create_world_range = atoi(argv[++i]);
                        }
                    }
                }
            }
        } else if (strcmp(argv[i], "--generate-world") == 0) {
            generate_world = 1;
        } else if (strcmp(argv[i], "--gen-distance-superchunks") == 0 && i + 1 < argc) {
            gen_distance_superchunks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            strncpy_s(gen_output_path, sizeof(gen_output_path), argv[++i], _TRUNCATE);
        } else if (strcmp(argv[i], "--walls") == 0 && i + 1 < argc) {
            walls_enabled = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--construction-cells") == 0 && i + 1 < argc) {
            construction_cells_enabled = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown walls map option: %s\n", argv[i]);
            return 1;
        }
    }

    if (create_world) {
        char grid_buffer[ASCII_GRID_BUFFER_SIZE];
        if (!create_world_theoretical(create_world_size,
                                      create_world_offset_x,
                                      create_world_offset_z,
                                      create_world_range,
                                      grid_buffer,
                                      sizeof(grid_buffer))) {
            fprintf(stderr, "Failed to create theoretical world preview\n");
            return 1;
        }
        printf("%s\n", grid_buffer);
        return 0;
    }

    if (generate_world) {
        char output_buffer[ASCII_GRID_BUFFER_SIZE];
        char final_path[512];

        if (gen_output_path[0] == '\0') {
            snprintf(final_path, sizeof(final_path), "WorldSaves\\world_%u", seed);
        } else {
            strncpy_s(final_path, sizeof(final_path), gen_output_path, _TRUNCATE);
        }

        if (!generate_real_world(seed, gen_distance_superchunks,
                                 chunk_span, walls_enabled != 0, walls_detached != 0,
                                 wall_grid_size, offset_x, offset_z,
                                 construction_cells_enabled != 0,
                                 final_path, output_buffer, sizeof(output_buffer))) {
            fprintf(stderr, "Failed to generate real world\n");
            return 1;
        }
        printf("%s\n", output_buffer);
        return 0;
    }

    result_count = analyze_chunk_mapping(chunk_span, walls_detached != 0,
                                         wall_grid_size, offset_x, offset_z,
                                         range, results, MAX_RESULTS);
    if (result_count <= 0) {
        fprintf(stderr, "No results generated\n");
        return 1;
    }

    calculate_statistics(results, result_count, &stats);
    if (json) {
        printf("{\"chunk_span\":%d,\"walls_enabled\":%s,\"walls_detached\":%s,\"wall_grid_size\":%d,\"offset_x\":%d,\"offset_z\":%d,\"range\":%d,",
               chunk_span,
               walls_enabled ? "true" : "false",
               walls_detached ? "true" : "false",
               wall_grid_size,
               offset_x,
               offset_z,
               range);
        printf("\"total_chunks\":%d,\"wall_chunks\":%d,\"superchunk_chunks\":%d,",
               stats.total_chunks,
               stats.wall_chunks,
               stats.superchunk_chunks);
        printf("\"west_walls\":%d,\"east_walls\":%d,\"north_walls\":%d,\"south_walls\":%d,\"corners\":%d",
               stats.west_wall_chunks,
               stats.east_wall_chunks,
               stats.north_wall_chunks,
               stats.south_wall_chunks,
               stats.corner_chunks);
        if (output_path) {
            printf(",\"csv_path\":\"");
            for (i = 0; output_path[i] != '\0'; ++i) {
                char ch = output_path[i];
                if (ch == '\\' || ch == '"') putchar('\\');
                putchar(ch);
            }
            printf("\"");
        }
        printf("}\n");
    } else {
        print_summary_statistics(&stats, chunk_span, walls_detached != 0,
                                 wall_grid_size, offset_x, offset_z);
        print_detailed_results(results, result_count);
    }

    if (visualize) {
        char grid_buffer[ASCII_GRID_BUFFER_SIZE];
        generate_ascii_grid(results, result_count, range, grid_buffer, sizeof(grid_buffer));
        if (!json) {
            printf("\n%s\n", grid_buffer);
        }
    }

    if (output_path) {
        if (!export_csv(results, result_count, output_path)) {
            fprintf(stderr, "Failed to export CSV to %s\n", output_path);
            return 1;
        }
        if (!json) {
            printf("\nCSV exported to: %s\n", output_path);
        }
    }

    return 0;
}

int debugger_cmd_walls(int argc, char** argv)
{
    if (argc >= 1 && strcmp(argv[0], "map") == 0) {
        return debugger_run_walls_map_mode(argc - 1, argv + 1);
    }
    return debugger_run_walls_map_mode(argc, argv);
}
