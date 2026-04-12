#include "chunk_analysis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int floor_div_int(int value, int divisor)
{
    if (divisor <= 0) {
        return 0;
    }
    if (value >= 0) {
        return value / divisor;
    }
    return -(((-value) + divisor - 1) / divisor);
}

/* Find most common non-air block in a column (x,z) across all y levels */
static uint16_t find_most_common_block_in_column(const CA_RawChunk* chunk, int x, int z) {
    uint32_t block_counts[65536] = {0};
    uint32_t max_count = 0;
    uint16_t most_common = 0;
    
    if (!chunk || x < 0 || x >= CA_CHUNK_SIZE_X || z < 0 || z >= CA_CHUNK_SIZE_Z) {
        return 0;
    }
    
    /* Count blocks in this column */
    for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
        uint16_t block = chunk->blocks[x][z][y];
        if (block != 0) {  /* Exclude air */
            block_counts[block]++;
            if (block_counts[block] > max_count) {
                max_count = block_counts[block];
                most_common = block;
            }
        }
    }
    
    return most_common;
}

/* Check if a column is entirely air */
static int is_column_air(const CA_RawChunk* chunk, int x, int z) {
    if (!chunk || x < 0 || x >= CA_CHUNK_SIZE_X || z < 0 || z >= CA_CHUNK_SIZE_Z) {
        return 0;
    }
    
    for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
        if (chunk->blocks[x][z][y] != 0) {
            return 0;  /* Found non-air block */
        }
    }
    
    return 1;  /* Entire column is air */
}

/* Check if a chunk is entirely air */
static int is_chunk_air(const CA_RawChunk* chunk) {
    if (!chunk) return 0;
    
    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
            if (!is_column_air(chunk, x, z)) {
                return 0;  /* Found non-air column */
            }
        }
    }
    
    return 1;  /* Entire chunk is air */
}

/* Search for air-only chunks and columns */
static void search_air_columns(CA_ChunkCollection* collection) {
    if (!collection || collection->count == 0) return;
    
    printf("\n=== AIR COLUMN SEARCH ===\n\n");
    
    int air_only_chunks = 0;
    int total_air_columns = 0;
    
    /* Search for air-only chunks */
    printf("Air-only chunks:\n");
    for (int i = 0; i < collection->count; i++) {
        if (is_chunk_air(&collection->chunks[i])) {
            printf("  Chunk [%d] at (%d,%d)\n", i, collection->chunks[i].cx, collection->chunks[i].cz);
            air_only_chunks++;
        }
    }
    
    if (air_only_chunks == 0) {
        printf("  None found\n");
    }
    
    /* Search for air-only columns in non-air chunks */
    printf("\nAir-only columns in non-air chunks:\n");
    for (int i = 0; i < collection->count; i++) {
        CA_RawChunk* chunk = &collection->chunks[i];
        
        if (is_chunk_air(chunk)) {
            continue;  /* Skip air-only chunks */
        }
        
        int chunk_air_columns = 0;
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                if (is_column_air(chunk, x, z)) {
                    chunk_air_columns++;
                }
            }
        }
        
        if (chunk_air_columns > 0) {
            printf("  Chunk [%d] at (%d,%d): %d/%d air columns (%.1f%%)\n",
                   i, chunk->cx, chunk->cz, chunk_air_columns,
                   CA_CHUNK_SIZE_X * CA_CHUNK_SIZE_Z,
                   100.0 * chunk_air_columns / (CA_CHUNK_SIZE_X * CA_CHUNK_SIZE_Z));
            total_air_columns += chunk_air_columns;
        }
    }
    
    if (total_air_columns == 0) {
        printf("  None found\n");
    }
    
    printf("\nSummary:\n");
    printf("  Air-only chunks: %d / %d (%.1f%%)\n", air_only_chunks, collection->count,
           100.0 * air_only_chunks / collection->count);
    printf("  Total air columns in non-air chunks: %d\n", total_air_columns);
    printf("\n");
}

/* Check if a column is predominantly one block type (excluding air) */
static int is_column_single_block(const CA_RawChunk* chunk, int x, int z, uint16_t target_block, float threshold) {
    if (!chunk || x < 0 || x >= CA_CHUNK_SIZE_X || z < 0 || z >= CA_CHUNK_SIZE_Z) {
        return 0;
    }
    
    int total_blocks = CA_CHUNK_SIZE_Y;
    int target_count = 0;
    int non_air_count = 0;
    
    for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
        if (chunk->blocks[x][z][y] == target_block) {
            target_count++;
        }
        if (chunk->blocks[x][z][y] != 0) {
            non_air_count++;
        }
    }
    
    /* If column is all air, return false */
    if (non_air_count == 0) {
        return 0;
    }
    
    float percentage = (float)target_count / non_air_count;
    return percentage >= threshold;
}

/* Check if a chunk is predominantly one block type (excluding air columns) */
static int is_chunk_single_block(const CA_RawChunk* chunk, uint16_t target_block, float threshold) {
    if (!chunk) return 0;
    
    int total_columns = CA_CHUNK_SIZE_X * CA_CHUNK_SIZE_Z;
    int target_columns = 0;
    int non_air_columns = 0;
    
    for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
        for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
            if (is_column_single_block(chunk, x, z, target_block, threshold)) {
                target_columns++;
            }
            /* Check if column has any non-air blocks */
            int has_non_air = 0;
            for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                if (chunk->blocks[x][z][y] != 0) {
                    has_non_air = 1;
                    break;
                }
            }
            if (has_non_air) {
                non_air_columns++;
            }
        }
    }
    
    /* If chunk is all air, return false */
    if (non_air_columns == 0) {
        return 0;
    }
    
    float percentage = (float)target_columns / non_air_columns;
    return percentage >= threshold;
}

/* Search for chunks/columns dominated by a specific block type */
static void search_single_block_columns(CA_ChunkCollection* collection, uint16_t target_block, float threshold) {
    if (!collection || collection->count == 0) return;
    
    printf("\n=== SINGLE BLOCK TYPE SEARCH (Block %d, Threshold %.1f%%) ===\n", target_block, threshold * 100.0f);
    printf("(Excluding air from threshold calculation)\n\n");
    
    int single_block_chunks = 0;
    int total_single_block_columns = 0;
    
    /* Search for single-block-type chunks */
    printf("Chunks predominantly block %d (%.1f%%+ of non-air):\n", target_block, threshold * 100.0f);
    for (int i = 0; i < collection->count; i++) {
        CA_RawChunk* chunk = &collection->chunks[i];
        
        int chunk_single_columns = 0;
        int chunk_non_air_columns = 0;
        
        for (int x = 0; x < CA_CHUNK_SIZE_X; x++) {
            for (int z = 0; z < CA_CHUNK_SIZE_Z; z++) {
                if (is_column_single_block(chunk, x, z, target_block, threshold)) {
                    chunk_single_columns++;
                }
                /* Check if column has any non-air blocks */
                int has_non_air = 0;
                for (int y = 0; y < CA_CHUNK_SIZE_Y; y++) {
                    if (chunk->blocks[x][z][y] != 0) {
                        has_non_air = 1;
                        break;
                    }
                }
                if (has_non_air) {
                    chunk_non_air_columns++;
                }
            }
        }
        
        /* Skip chunks with no non-air columns */
        if (chunk_non_air_columns == 0) {
            continue;
        }
        
        float percentage = (float)chunk_single_columns / chunk_non_air_columns;
        
        if (percentage >= threshold) {
            printf("  Chunk [%d] at (%d,%d): %d/%d non-air columns (%.1f%%)\n",
                   i, chunk->cx, chunk->cz, chunk_single_columns,
                   chunk_non_air_columns, percentage * 100.0f);
            single_block_chunks++;
            total_single_block_columns += chunk_single_columns;
        }
    }
    
    if (single_block_chunks == 0) {
        printf("  None found\n");
    }
    
    printf("\nSummary:\n");
    printf("  Single-block chunks: %d / %d (%.1f%%)\n", single_block_chunks, collection->count,
           100.0f * single_block_chunks / collection->count);
    printf("  Total single-block columns: %d\n", total_single_block_columns);
    printf("\n");
}

static void print_chunk_mask(CA_ChunkCollection* collection) {
    if (!collection || collection->count == 0) return;
    
    printf("\n=== CHUNK MASK (Most Common Non-Air Block per Column) ===\n");
    printf("Legend: Each cell shows block ID%%10, '.' = air/empty\n");
    printf("Note: Showing 8x8 downscaled view (each cell = 8x8 block region)\n\n");
    
    /* Print each chunk individually with downscaled view */
    for (int i = 0; i < collection->count; i++) {
        CA_RawChunk* chunk = &collection->chunks[i];
        
        printf("--- Chunk [%d] at (%d,%d) ---\n", i, chunk->cx, chunk->cz);
        
        /* Downscale to 8x8 for readability (each cell = 8x8 region) */
        int scale = CA_CHUNK_SIZE_X / 8;  /* 8 */
        
        for (int z_block = 0; z_block < 8; z_block++) {
            for (int x_block = 0; x_block < 8; x_block++) {
                /* Sample the most common block in this 8x8 region */
                uint32_t block_counts[65536] = {0};
                uint32_t max_count = 0;
                uint16_t most_common = 0;
                
                for (int z = 0; z < scale; z++) {
                    for (int x = 0; x < scale; x++) {
                        int actual_x = x_block * scale + x;
                        int actual_z = (7 - z_block) * scale + z;  /* Flip Z for top-down view */
                        
                        uint16_t block = find_most_common_block_in_column(chunk, actual_x, actual_z);
                        if (block != 0) {
                            block_counts[block]++;
                            if (block_counts[block] > max_count) {
                                max_count = block_counts[block];
                                most_common = block;
                            }
                        }
                    }
                }
                
                if (most_common == 0) {
                    printf(".");
                } else {
                    printf("%d", most_common % 10);
                }
            }
            printf("\n");
        }
        printf("\n");
    }
}

static void print_usage(const char* prog_name) {
    printf("Usage: %s <save.json> [options]\n", prog_name);
    printf("\nOptions:\n");
    printf("  -m, --max-chunks N      Analyze at most N chunks (default: all)\n");
    printf("  -o, --output PREFIX     Output file prefix (default: analysis)\n");
    printf("  --statistical-only      Only run statistical analysis\n");
    printf("  --geometric-only        Only run geometric analysis\n");
    printf("  --similarity-only       Only run similarity analysis\n");
    printf("  --clusters K            Cluster chunks into K groups (default: 5)\n");
    printf("  --no-csv                Don't export CSV files\n");
    printf("  --no-json               Don't export JSON report\n");
    printf("  --analyze-block-types   Enable block type distribution analysis\n");
    printf("  --superchunk-analysis   Analyze superchunks (auto-detect from walls)\n");
    printf("  --range-analysis CX1 CZ1 CX2 CZ2  Analyze rectangular chunk range\n");
    printf("  --export-block-dist FILE  Export block distribution to CSV\n");
    printf("  --kernel-analysis       Enable kernel-based pattern analysis\n");
    printf("  --kernel-stride N       Kernel stride (1=overlapping, 0=auto)\n");
    printf("  --kernel-region CX1 CZ1 CX2 CZ2  Specify kernel analysis region\n");
    printf("  --export-kernel-results FILE  Export kernel results to CSV\n");
    printf("  --subchunk-divisor N    Analyze Y-axis subchunks (N must divide 1024)\n");
    printf("  --block-kernel-efficiency  Analyze best kernels for each block ID\n");
    printf("  --list-blocks CX1 CZ1 CX2 CZ2  List blocks in region\n");
    printf("  --list-blocks-detailed CX1 CZ1 CX2 CZ2  Detailed block statistics\n");
    printf("  --rank-kernels-for-block BLOCK_ID [CX1 CZ1 CX2 CZ2]  Rank kernels for block\n");
    printf("  --rank-kernels-for-blocks IDS [CX1 CZ1 CX2 CZ2]  Rank kernels for multiple blocks\n");
    printf("  --kernel-coverage BLOCK_ID MIN_RATE [CX1 CZ1 CX2 CZ2]  Show coverage stats\n");
    printf("  --find-minimal-kernel-set BLOCK_ID MIN_RATE [CX1 CZ1 CX2 CZ2]  Find minimal set\n");
    printf("  --chunk-mask            Print 2D mask of most common blocks per column\n");
    printf("  --air-column-search      Search for air-only chunks and columns\n");
    printf("  --single-block-search BLOCK_ID THRESHOLD  Search for chunks/columns with specific block\n");
    printf("  --wall-analysis        Enable wall chunk analysis\n");
    printf("  --wall-threshold N     Minimum wall blocks to consider a chunk has walls (default: 100)\n");
    printf("  --wall-range N          Coordinate range for ASCII grid (default: 17)\n");
    printf("  --air-threshold N      Air-only threshold percentage (default: 5)\n");
    printf("  -v, --verbose           Verbose output\n");
    printf("  -h, --help              Show this help\n");
    printf("\nExamples:\n");
    printf("  %s save.json\n", prog_name);
    printf("  %s save.json -m 10 --clusters 3\n", prog_name);
    printf("  %s save.json -o my_analysis --statistical-only\n", prog_name);
    printf("  %s save.json --superchunk-analysis --export-block-dist block_dist.csv\n", prog_name);
    printf("  %s save.json --range-analysis 0 0 15 15\n", prog_name);
    printf("  %s save.json --kernel-analysis --export-kernel-results kernels.csv\n", prog_name);
    printf("  %s save.json --kernel-analysis --kernel-region 0 0 10 10 --kernel-stride 1\n", prog_name);
    printf("  %s save.json --list-blocks 0 0 10 10\n", prog_name);
    printf("  %s save.json --rank-kernels-for-block 29\n", prog_name);
    printf("  %s save.json --kernel-coverage 29 0.8\n", prog_name);
}

static void parse_args(int argc, char** argv, CA_AnalysisConfig* config, char** filename, int* max_chunks,
                       bool* do_block_analysis, bool* do_superchunk_analysis, bool* do_range_analysis,
                       int* range_cx1, int* range_cz1, int* range_cx2, int* range_cz2, char** block_dist_file,
                       bool* do_kernel_analysis, int* kernel_stride, char** kernel_export_file,
                       int* kernel_cx1, int* kernel_cz1, int* kernel_cx2, int* kernel_cz2, bool* kernel_auto_region,
                       bool* do_list_blocks, int* list_cx1, int* list_cz1, int* list_cx2, int* list_cz2, bool* list_detailed,
                       bool* do_rank_kernels, uint16_t* rank_block_id, bool* rank_multiple,
                       int* rank_cx1, int* rank_cz1, int* rank_cx2, int* rank_cz2, bool* rank_auto_region,
                       bool* do_coverage, uint16_t* coverage_block_id, double* coverage_min_rate,
                       int* cov_cx1, int* cov_cz1, int* cov_cx2, int* cov_cz2, bool* cov_auto_region,
                       int* subchunk_divisor, bool* do_block_kernel_efficiency, bool* do_chunk_mask, bool* do_air_column_search,
                       bool* do_single_block_search, uint16_t* single_block_id, float* single_block_threshold,
                       bool* do_wall_analysis, uint32_t* wall_threshold, int* wall_range,
                       int* air_threshold_percent) {
    /* Defaults */
    config->do_statistical = true;
    config->do_geometric = true;
    config->do_similarity = true;
    config->do_clustering = true;
    config->num_clusters = 5;
    config->verbose = false;
    strcpy_s(config->output_prefix, sizeof(config->output_prefix), "analysis");
    *max_chunks = 0;  /* 0 = unlimited */
    *filename = NULL;
    *do_wall_analysis = false;
    *wall_threshold = 100;
    *wall_range = 17;
    *air_threshold_percent = 5;
    
    *do_block_analysis = false;
    *do_superchunk_analysis = false;
    *do_range_analysis = false;
    *range_cx1 = 0; *range_cz1 = 0; *range_cx2 = 0; *range_cz2 = 0;
    *block_dist_file = NULL;
    
    *do_kernel_analysis = false;
    *kernel_stride = 0;  /* 0 = auto (kernel size) */
    *kernel_export_file = NULL;
    *kernel_cx1 = 0; *kernel_cz1 = 0; *kernel_cx2 = 0; *kernel_cz2 = 0;
    *kernel_auto_region = true;  /* Auto-detect region from loaded chunks */
    
    *do_list_blocks = false;
    *list_cx1 = 0; *list_cz1 = 0; *list_cx2 = 0; *list_cz2 = 0;
    *list_detailed = false;
    
    *do_rank_kernels = false;
    *rank_block_id = 0;
    *rank_multiple = false;
    *rank_cx1 = 0; *rank_cz1 = 0; *rank_cx2 = 0; *rank_cz2 = 0;
    *rank_auto_region = true;
    
    *do_coverage = false;
    *coverage_block_id = 0;
    *coverage_min_rate = 0.0;
    *cov_cx1 = 0; *cov_cz1 = 0; *cov_cx2 = 0; *cov_cz2 = 0;
    *cov_auto_region = true;
    
    *subchunk_divisor = 0;  /* 0 = disabled */
    *do_block_kernel_efficiency = false;
    *do_chunk_mask = false;
    *do_air_column_search = false;
    *do_single_block_search = false;
    *single_block_id = 0;
    *single_block_threshold = 0.0f;
    
    bool csv_enabled = true;
    bool json_enabled = true;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            config->verbose = true;
        } else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--max-chunks") == 0) && i + 1 < argc) {
            *max_chunks = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            strncpy_s(config->output_prefix, sizeof(config->output_prefix), argv[++i], 255);
        } else if (strcmp(argv[i], "--statistical-only") == 0) {
            config->do_statistical = true;
            config->do_geometric = false;
            config->do_similarity = false;
            config->do_clustering = false;
        } else if (strcmp(argv[i], "--geometric-only") == 0) {
            config->do_statistical = false;
            config->do_geometric = true;
            config->do_similarity = false;
            config->do_clustering = false;
        } else if (strcmp(argv[i], "--similarity-only") == 0) {
            config->do_statistical = false;
            config->do_geometric = false;
            config->do_similarity = true;
            config->do_clustering = true;
        } else if (strcmp(argv[i], "--clusters") == 0 && i + 1 < argc) {
            config->num_clusters = atoi(argv[++i]);
            if (config->num_clusters < 2) config->num_clusters = 2;
            if (config->num_clusters > 20) config->num_clusters = 20;
        } else if (strcmp(argv[i], "--no-csv") == 0) {
            csv_enabled = false;
        } else if (strcmp(argv[i], "--no-json") == 0) {
            json_enabled = false;
        } else if (strcmp(argv[i], "--analyze-block-types") == 0) {
            *do_block_analysis = true;
        } else if (strcmp(argv[i], "--superchunk-analysis") == 0) {
            *do_superchunk_analysis = true;
            *do_block_analysis = true;
        } else if ((strcmp(argv[i], "--range-analysis") == 0) && i + 4 < argc) {
            *do_range_analysis = true;
            *do_block_analysis = true;
            *range_cx1 = atoi(argv[++i]);
            *range_cz1 = atoi(argv[++i]);
            *range_cx2 = atoi(argv[++i]);
            *range_cz2 = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--export-block-dist") == 0) && i + 1 < argc) {
            *block_dist_file = argv[++i];
        } else if (strcmp(argv[i], "--kernel-analysis") == 0) {
            *do_kernel_analysis = true;
            *do_block_analysis = true;
        } else if ((strcmp(argv[i], "--kernel-stride") == 0) && i + 1 < argc) {
            *kernel_stride = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--export-kernel-results") == 0) && i + 1 < argc) {
            *kernel_export_file = argv[++i];
        } else if ((strcmp(argv[i], "--kernel-region") == 0) && i + 4 < argc) {
            *kernel_auto_region = false;
            *kernel_cx1 = atoi(argv[++i]);
            *kernel_cz1 = atoi(argv[++i]);
            *kernel_cx2 = atoi(argv[++i]);
            *kernel_cz2 = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--subchunk-divisor") == 0) && i + 1 < argc) {
            *subchunk_divisor = atoi(argv[++i]);
            /* Validate divisor divides 1024 */
            if (*subchunk_divisor > 0 && 1024 % *subchunk_divisor != 0) {
                fprintf(stderr, "Error: Subchunk divisor %d does not divide chunk height 1024\n", *subchunk_divisor);
                fprintf(stderr, "Valid divisors: 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024\n");
                exit(1);
            }
        } else if (strcmp(argv[i], "--block-kernel-efficiency") == 0) {
            *do_block_kernel_efficiency = true;
        } else if (strcmp(argv[i], "--chunk-mask") == 0) {
            *do_chunk_mask = true;
        } else if (strcmp(argv[i], "--air-column-search") == 0) {
            *do_air_column_search = true;
        } else if ((strcmp(argv[i], "--single-block-search") == 0) && i + 2 < argc) {
            *do_single_block_search = true;
            *single_block_id = (uint16_t)atoi(argv[++i]);
            *single_block_threshold = (float)atof(argv[++i]) / 100.0f;  /* Convert percentage to fraction */
        } else if ((strcmp(argv[i], "--list-blocks") == 0) && i + 4 < argc) {
            *do_list_blocks = true;
            *list_cx1 = atoi(argv[++i]);
            *list_cz1 = atoi(argv[++i]);
            *list_cx2 = atoi(argv[++i]);
            *list_cz2 = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--list-blocks-detailed") == 0) && i + 4 < argc) {
            *do_list_blocks = true;
            *list_detailed = true;
            *list_cx1 = atoi(argv[++i]);
            *list_cz1 = atoi(argv[++i]);
            *list_cx2 = atoi(argv[++i]);
            *list_cz2 = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--rank-kernels-for-block") == 0) && i + 1 < argc) {
            *do_rank_kernels = true;
            *rank_block_id = (uint16_t)atoi(argv[++i]);
            /* Check for optional region */
            if (i + 4 < argc && argv[i+1][0] != '-') {
                *rank_auto_region = false;
                *rank_cx1 = atoi(argv[++i]);
                *rank_cz1 = atoi(argv[++i]);
                *rank_cx2 = atoi(argv[++i]);
                *rank_cz2 = atoi(argv[++i]);
            }
        } else if ((strcmp(argv[i], "--kernel-coverage") == 0) && i + 2 < argc) {
            *do_coverage = true;
            *coverage_block_id = (uint16_t)atoi(argv[++i]);
            *coverage_min_rate = atof(argv[++i]);
            /* Check for optional region */
            if (i + 4 < argc && argv[i+1][0] != '-') {
                *cov_auto_region = false;
                *cov_cx1 = atoi(argv[++i]);
                *cov_cz1 = atoi(argv[++i]);
                *cov_cx2 = atoi(argv[++i]);
                *cov_cz2 = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--wall-analysis") == 0) {
            *do_wall_analysis = true;
        } else if ((strcmp(argv[i], "--wall-threshold") == 0) && i + 1 < argc) {
            *wall_threshold = (uint32_t)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--wall-range") == 0) && i + 1 < argc) {
            *wall_range = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--air-threshold") == 0) && i + 1 < argc) {
            *air_threshold_percent = atoi(argv[++i]);
        } else if (i == argc - 1 && argv[i][0] != '-') {
            *filename = argv[i];
        }
    }
    
    /* Adjust config based on CSV/JSON flags */
    if (!csv_enabled && !json_enabled) {
        /* Console only - still do all analysis for display */
    }
}

int chunk_analysis_cli_main(int argc, char** argv) {
    CA_AnalysisConfig config;
    char* filename = NULL;
    int max_chunks = 0;
    
    bool do_block_analysis = false;
    bool do_superchunk_analysis = false;
    bool do_range_analysis = false;
    int range_cx1 = 0, range_cz1 = 0, range_cx2 = 0, range_cz2 = 0;
    char* block_dist_file = NULL;
    
    bool do_kernel_analysis = false;
    int kernel_stride = 0;
    char* kernel_export_file = NULL;
    int kernel_cx1 = 0, kernel_cz1 = 0, kernel_cx2 = 0, kernel_cz2 = 0;
    bool kernel_auto_region = true;
    
    bool do_list_blocks = false;
    int list_cx1 = 0, list_cz1 = 0, list_cx2 = 0, list_cz2 = 0;
    bool list_detailed = false;
    
    bool do_rank_kernels = false;
    uint16_t rank_block_id = 0;
    bool rank_multiple = false;
    int rank_cx1 = 0, rank_cz1 = 0, rank_cx2 = 0, rank_cz2 = 0;
    bool rank_auto_region = true;
    
    bool do_coverage = false;
    uint16_t coverage_block_id = 0;
    double coverage_min_rate = 0.0;
    int cov_cx1 = 0, cov_cz1 = 0, cov_cx2 = 0, cov_cz2 = 0;
    bool cov_auto_region = true;
    
    int subchunk_divisor = 0;
    bool do_block_kernel_efficiency = false;
    bool do_chunk_mask = false;
    bool do_air_column_search = false;
    bool do_single_block_search = false;
    uint16_t single_block_id = 0;
    float single_block_threshold = 0.0f;
    
    bool do_wall_analysis = false;
    uint32_t wall_threshold = 100;
    int wall_range = 17;
    int air_threshold_percent = 5;
    
    parse_args(argc, argv, &config, &filename, &max_chunks,
               &do_block_analysis, &do_superchunk_analysis, &do_range_analysis,
               &range_cx1, &range_cz1, &range_cx2, &range_cz2, &block_dist_file,
               &do_kernel_analysis, &kernel_stride, &kernel_export_file,
               &kernel_cx1, &kernel_cz1, &kernel_cx2, &kernel_cz2, &kernel_auto_region,
               &do_list_blocks, &list_cx1, &list_cz1, &list_cx2, &list_cz2, &list_detailed,
               &do_rank_kernels, &rank_block_id, &rank_multiple,
               &rank_cx1, &rank_cz1, &rank_cx2, &rank_cz2, &rank_auto_region,
               &do_coverage, &coverage_block_id, &coverage_min_rate,
               &cov_cx1, &cov_cz1, &cov_cx2, &cov_cz2, &cov_auto_region,
               &subchunk_divisor, &do_block_kernel_efficiency, &do_chunk_mask, &do_air_column_search,
               &do_single_block_search, &single_block_id, &single_block_threshold,
               &do_wall_analysis, &wall_threshold, &wall_range, &air_threshold_percent);
    
    if (!filename) {
        print_usage(argv[0]);
        return 1;
    }
    
    printf("ChunkAnalysis - Comprehensive Chunk Pattern Analysis\n");
    printf("==================================================\n\n");
    
    /* Load chunks */
    printf("Loading chunks from %s...\n", filename);
    clock_t start = clock();
    
    CA_ChunkCollection* collection = (CA_ChunkCollection*)malloc(sizeof(CA_ChunkCollection));
    if (!collection) {
        fprintf(stderr, "Error: Failed to allocate collection\n");
        return 1;
    }
    memset(collection, 0, sizeof(CA_ChunkCollection));
    
    if (ca_load_json_save(filename, collection, max_chunks) != 0 || collection->count == 0) {
        fprintf(stderr, "Error: Failed to load chunk data from save file\n");
        free(collection);
        return 1;
    }
    
    clock_t load_end = clock();
    double load_time = 1000.0 * (load_end - start) / CLOCKS_PER_SEC;
    
    printf("Loaded %d chunks in %.2f ms\n\n", collection->count, load_time);
    
    /* Wall Chunk Analysis */
    if (do_wall_analysis) {
        printf("=== WALL CHUNK ANALYSIS ===\n");
        
        clock_t wall_start = clock();
        
        /* Load world configuration from meta.txt */
        CA_WorldConfig world_config;
        int config_result = ca_load_world_config(filename, &world_config);
        if (config_result == 0) {
            printf("Loaded world configuration from meta.txt\n");
        } else {
            printf("Using default world configuration (meta.txt not found)\n");
        }
        world_config.air_threshold_percent = air_threshold_percent;
        
        /* Allocate wall info array */
        CA_WallChunkInfo* wall_info = (CA_WallChunkInfo*)calloc(collection->count, sizeof(CA_WallChunkInfo));
        if (!wall_info) {
            fprintf(stderr, "Error: Failed to allocate wall info array\n");
        } else {
            /* Detect wall blocks in all chunks */
            ca_detect_wall_blocks_collection(collection, wall_info, air_threshold_percent);
            
            /* Calculate expected wall positions */
            ca_calculate_expected_walls(collection, &world_config, wall_info);
            
            /* Compare expected vs actual */
            ca_compare_wall_mapping(collection, wall_info, wall_threshold);
            
            clock_t wall_end = clock();
            printf("Wall analysis complete in %.2f ms\n", 1000.0 * (wall_end - wall_start) / CLOCKS_PER_SEC);
            
            /* Print summary */
            ca_print_wall_analysis_summary(collection, wall_info, &world_config);
            
            /* Generate ASCII grid */
            char* grid_output = (char*)malloc(10000);
            if (grid_output) {
                int grid_result = ca_generate_wall_grid(collection, wall_info, wall_range, grid_output, 10000);
                if (grid_result == 0) {
                    printf("\nASCII Grid (range -%d to +%d):\n", wall_range, wall_range);
                    printf("Legend: W = correct wall (expected + has blocks), w = missing wall blocks (expected, no blocks), X = unexpected wall (has blocks, not expected), A = air-only chunk, . = normal, + = corner, M = missing chunk (not in save file)\n\n");
                    printf("%s\n", grid_output);
                }
                free(grid_output);
            }
            
            /* Export CSV */
            char csv_filename[512];
            snprintf(csv_filename, sizeof(csv_filename), "%s_wall_analysis.csv", config.output_prefix);
            int csv_result = ca_export_wall_analysis_csv(collection, wall_info, csv_filename);
            if (csv_result == 0) {
                printf("Wall analysis exported to: %s\n", csv_filename);
            }
            
            free(wall_info);
        }
        
        printf("=== END WALL CHUNK ANALYSIS ===\n\n");
    }
    
    /* Air Column Search */
    if (do_air_column_search) {
        search_air_columns(collection);
    }
    
    /* Single Block Type Search */
    if (do_single_block_search) {
        search_single_block_columns(collection, single_block_id, single_block_threshold);
    }
    
    /* Chunk Mask Visualization */
    if (do_chunk_mask) {
        print_chunk_mask(collection);
    }
    
    /* Block Inventory Queries */
    if (do_list_blocks) {
        printf("=== BLOCK INVENTORY ===\n");
        printf("Region: (%d,%d) to (%d,%d)\n\n", list_cx1, list_cz1, list_cx2, list_cz2);
        
        if (list_detailed) {
            CA_BlockStat* stats = (CA_BlockStat*)calloc(65536, sizeof(CA_BlockStat));
            uint32_t stat_count = 0;
            int result = ca_get_blocks_in_region_detailed(collection, list_cx1, list_cz1, list_cx2, list_cz2, stats, &stat_count);
            
            if (result == 0) {
                printf("Found %u unique block types:\n\n", stat_count);
                for (uint32_t i = 0; i < stat_count; i++) {
                    printf("  Block %u: %u occurrences (%.2f%%)\n", 
                           stats[i].block_id, stats[i].count, stats[i].percentage * 100.0);
                }
            } else {
                printf("Error: Failed to get block statistics\n");
            }
            
            free(stats);
        } else {
            uint16_t* blocks = (uint16_t*)calloc(65536, sizeof(uint16_t));
            uint32_t block_count = 0;
            int result = ca_get_blocks_in_region(collection, list_cx1, list_cz1, list_cx2, list_cz2, blocks, &block_count);
            
            if (result == 0) {
                printf("Found %u unique block types:\n", block_count);
                for (uint32_t i = 0; i < block_count; i++) {
                    printf("  %u", blocks[i]);
                    if ((i + 1) % 10 == 0) printf("\n");
                    else printf(" ");
                }
                if (block_count % 10 != 0) printf("\n");
            } else {
                printf("Error: Failed to get block list\n");
            }
            
            free(blocks);
        }
        
        printf("\n=== END BLOCK INVENTORY ===\n\n");
        
        /* For list-blocks mode, skip other analyses */
        ca_free_collection(collection);
        return 0;
    }
    
    /* Reverse Kernel Lookup */
    if (do_rank_kernels) {
        printf("=== REVERSE KERNEL LOOKUP ===\n");
        printf("Block ID: %u\n", rank_block_id);
        
        int rcx1 = rank_cx1, rcz1 = rank_cz1, rcx2 = rank_cx2, rcz2 = rank_cz2;
        if (rank_auto_region) {
            rcx1 = collection->chunks[0].cx;
            rcz1 = collection->chunks[0].cz;
            rcx2 = collection->chunks[collection->count - 1].cx;
            rcz2 = collection->chunks[collection->count - 1].cz;
        }
        printf("Region: (%d,%d) to (%d,%d)\n\n", rcx1, rcz1, rcx2, rcz2);
        
        CA_KernelRanking* rankings = (CA_KernelRanking*)calloc(CA_MAX_KERNELS, sizeof(CA_KernelRanking));
        uint32_t ranking_count = 0;
        int result = ca_rank_kernels_for_block(collection, rank_block_id, rcx1, rcz1, rcx2, rcz2, 1,
                                               rankings, &ranking_count);
        
        if (result == 0 && ranking_count > 0) {
            printf("Kernels ranked by score for block %u:\n\n", rank_block_id);
            for (uint32_t i = 0; i < ranking_count; i++) {
                printf("  #%u: %s (score: %.6f, activations: %u)\n", 
                       i + 1, rankings[i].kernel->name, rankings[i].score, rankings[i].activations);
            }
        } else {
            printf("No kernels found for block %u\n", rank_block_id);
        }
        
        free(rankings);
        printf("\n=== END REVERSE KERNEL LOOKUP ===\n\n");
        
        ca_free_collection(collection);
        return 0;
    }
    
    /* Coverage Analysis */
    if (do_coverage) {
        printf("=== KERNEL COVERAGE ANALYSIS ===\n");
        printf("Block ID: %u\n", coverage_block_id);
        printf("Min Pass Rate: %.2f%%\n", coverage_min_rate * 100.0);
        
        int ccx1 = cov_cx1, ccz1 = cov_cz1, ccx2 = cov_cx2, ccz2 = cov_cz2;
        if (cov_auto_region) {
            ccx1 = collection->chunks[0].cx;
            ccz1 = collection->chunks[0].cz;
            ccx2 = collection->chunks[collection->count - 1].cx;
            ccz2 = collection->chunks[collection->count - 1].cz;
        }
        printf("Region: (%d,%d) to (%d,%d)\n\n", ccx1, ccz1, ccx2, ccz2);
        
        CA_KernelCoverage* coverage = (CA_KernelCoverage*)calloc(CA_MAX_KERNELS, sizeof(CA_KernelCoverage));
        uint32_t coverage_count = 0;
        int result = ca_analyze_kernel_coverage_for_block(collection, coverage_block_id, ccx1, ccz1, ccx2, ccz2, 1,
                                                         coverage, &coverage_count);
        
        if (result == 0 && coverage_count > 0) {
            printf("Kernel coverage for block %u:\n\n", coverage_block_id);
            for (uint32_t i = 0; i < coverage_count; i++) {
                printf("  %s: pass_rate=%.2f%% (%u/%u chunks)\n",
                       coverage[i].kernel->name,
                       coverage[i].pass_rate * 100.0,
                       coverage[i].chunks_found,
                       coverage[i].chunks_tested);
            }
            
            /* Find minimal kernel set */
            printf("\nFinding minimal kernel set for %.2f%% pass rate...\n", coverage_min_rate * 100.0);
            
            const CA_Kernel* minimal_set[CA_MAX_KERNELS];
            uint32_t minimal_count = 0;
            result = ca_find_minimal_kernel_set(collection, coverage_block_id, coverage_min_rate,
                                              ccx1, ccz1, ccx2, ccz2, 1, 5000, minimal_set, &minimal_count);
            
            if (result == 0 && minimal_count > 0) {
                printf("Minimal kernel set (%u kernels):\n", minimal_count);
                for (uint32_t i = 0; i < minimal_count; i++) {
                    printf("  %u: %s\n", i + 1, minimal_set[i]->name);
                }
            } else {
                printf("Could not find kernel set meeting criteria\n");
            }
        } else {
            printf("No coverage data available\n");
        }
        
        free(coverage);
        printf("\n=== END KERNEL COVERAGE ANALYSIS ===\n\n");
        
        ca_free_collection(collection);
        return 0;
    }
    
    if (collection->count == 0) {
        fprintf(stderr, "Error: No chunks found in file\n");
        free(collection);
        return 1;
    }
    
    /* Block Type Distribution Analysis */
    if (do_block_analysis) {
        printf("=== BLOCK TYPE DISTRIBUTION ANALYSIS ===\n\n");
        
        if (do_superchunk_analysis) {
            /* Detect wall chunks */
            bool* is_wall = (bool*)calloc(collection->count, sizeof(bool));
            if (is_wall) {
                ca_detect_wall_chunks(collection, is_wall);
                
                int wall_count = 0;
                for (int i = 0; i < collection->count; i++) {
                    if (is_wall[i]) wall_count++;
                }
                printf("Detected %d wall chunks (95%% stone bricks)\n", wall_count);
                
                /* Detect superchunks */
                CA_Superchunk superchunks[64];
                int sc_count = ca_detect_superchunks(collection, is_wall, superchunks, 64);
                printf("Detected %d superchunks (16x16 areas between walls)\n\n", sc_count);
                
                /* Analyze each superchunk */
                for (int sc = 0; sc < sc_count; sc++) {
                    printf("--- Superchunk %d ---\n", sc);
                    printf("  Bounds: (%d, %d) to (%d, %d)\n", 
                           superchunks[sc].cx_min, superchunks[sc].cz_min,
                           superchunks[sc].cx_max, superchunks[sc].cz_max);
                    printf("  Chunks: %d\n", superchunks[sc].chunk_count);
                    
                    uint32_t block_type_count = 0;
                    ca_count_block_types_superchunk(collection, &superchunks[sc], &block_type_count);
                    printf("  Block types: %u\n", block_type_count);
                    
                    uint32_t shared_count = 0;
                    ca_count_shared_block_types_superchunk(collection, &superchunks[sc], &shared_count);
                    printf("  Shared block types: %u\n", shared_count);
                    
                    /* Block ID distribution */
                    uint16_t* block_ids = (uint16_t*)malloc(65536 * sizeof(uint16_t));
                    uint32_t* chunk_counts = (uint32_t*)malloc(65536 * sizeof(uint32_t));
                    uint32_t dist_count = 0;
                    
                    if (block_ids && chunk_counts) {
                        ca_block_id_distribution_superchunk(collection, &superchunks[sc], block_ids, chunk_counts, &dist_count);
                        
                        printf("  Top 10 block IDs by chunk count:\n");
                        for (int i = 0; i < 10 && i < (int)dist_count; i++) {
                            printf("    Block %u: in %u chunks (%.1f%%)\n", 
                                   block_ids[i], chunk_counts[i],
                                   100.0 * chunk_counts[i] / superchunks[sc].chunk_count);
                        }
                        
                        printf("\n");
                        
                        /* Export to CSV if requested */
                        if (block_dist_file) {
                            char filename[512];
                            sprintf_s(filename, sizeof(filename), "%s_sc%d.csv", block_dist_file, sc);
                            FILE* f = fopen(filename, "w");
                            if (f) {
                                fprintf(f, "BlockID,ChunkCount,Percentage\n");
                                for (uint32_t i = 0; i < dist_count; i++) {
                                    fprintf(f, "%u,%u,%.2f\n", block_ids[i], chunk_counts[i],
                                            100.0 * chunk_counts[i] / superchunks[sc].chunk_count);
                                }
                                fclose(f);
                                printf("  Exported distribution to: %s\n", filename);
                            }
                        }
                    }
                    
                    if (block_ids) free(block_ids);
                    if (chunk_counts) free(chunk_counts);
                }
                
                free(is_wall);
            }
        }
        
        if (do_range_analysis) {
            printf("--- Range Analysis ---\n");
            printf("  Range: (%d, %d) to (%d, %d)\n", range_cx1, range_cz1, range_cx2, range_cz2);
            
            uint32_t block_type_count = 0;
            ca_count_block_types_range(collection, range_cx1, range_cz1, range_cx2, range_cz2, &block_type_count);
            printf("  Block types: %u\n", block_type_count);
            
            uint32_t shared_count = 0;
            ca_count_shared_block_types_range(collection, range_cx1, range_cz1, range_cx2, range_cz2, &shared_count);
            printf("  Shared block types: %u\n", shared_count);
            
            /* Block ID distribution */
            uint16_t* block_ids = (uint16_t*)malloc(65536 * sizeof(uint16_t));
            uint32_t* chunk_counts = (uint32_t*)malloc(65536 * sizeof(uint32_t));
            uint32_t dist_count = 0;
            
            if (block_ids && chunk_counts) {
                ca_block_id_distribution_range(collection, range_cx1, range_cz1, range_cx2, range_cz2, 
                                               block_ids, chunk_counts, &dist_count);
                
                printf("  Top 10 block IDs by chunk count:\n");
                for (int i = 0; i < 10 && i < (int)dist_count; i++) {
                    printf("    Block %u: in %u chunks\n", block_ids[i], chunk_counts[i]);
                }
                
                /* Export to CSV if requested */
                if (block_dist_file) {
                    FILE* f = fopen(block_dist_file, "w");
                    if (f) {
                        fprintf(f, "BlockID,ChunkCount\n");
                        for (uint32_t i = 0; i < dist_count; i++) {
                            fprintf(f, "%u,%u\n", block_ids[i], chunk_counts[i]);
                        }
                        fclose(f);
                        printf("  Exported distribution to: %s\n", block_dist_file);
                    }
                }
            }
            
            if (block_ids) free(block_ids);
            if (chunk_counts) free(chunk_counts);
            
            printf("\n");
        }
        
        printf("=== END BLOCK TYPE DISTRIBUTION ANALYSIS ===\n\n");
    }
    
    /* Kernel-Based Pattern Analysis */
    if (do_kernel_analysis) {
        printf("=== KERNEL-BASED PATTERN ANALYSIS ===\n\n");
        
        /* Determine region bounds */
        int cx1, cz1, cx2, cz2;
        
        if (kernel_auto_region) {
            /* Auto-detect from loaded chunks */
            cx1 = collection->chunks[0].cx;
            cz1 = collection->chunks[0].cz;
            cx2 = cx1;
            cz2 = cz1;
            
            for (int i = 1; i < collection->count; i++) {
                if (collection->chunks[i].cx < cx1) cx1 = collection->chunks[i].cx;
                if (collection->chunks[i].cx > cx2) cx2 = collection->chunks[i].cx;
                if (collection->chunks[i].cz < cz1) cz1 = collection->chunks[i].cz;
                if (collection->chunks[i].cz > cz2) cz2 = collection->chunks[i].cz;
            }
            printf("Region bounds: auto-detected (%d,%d) to (%d,%d)\n", cx1, cz1, cx2, cz2);
        } else {
            /* Use specified region */
            cx1 = kernel_cx1;
            cz1 = kernel_cz1;
            cx2 = kernel_cx2;
            cz2 = kernel_cz2;
            printf("Region bounds: specified (%d,%d) to (%d,%d)\n", cx1, cz1, cx2, cz2);
        }
        
        /* Use kernel_size as default stride if not specified */
        int stride = kernel_stride > 0 ? kernel_stride : 3;
        printf("Stride: %d\n", stride);
        
        CA_KernelAnalysisResult* results = NULL;
        int result_count = 0;
        
        clock_t kernel_start = clock();
        int result = ca_analyze_region_full(collection, cx1, cz1, cx2, cz2, stride, subchunk_divisor, &results, &result_count);
        clock_t kernel_end = clock();
        
        if (result == 0 && results) {
            printf("Kernel analysis complete in %.2f ms\n", 1000.0 * (kernel_end - kernel_start) / CLOCKS_PER_SEC);
            printf("Kernels analyzed: %d\n\n", result_count);
            
            /* Print summary of top block types per kernel */
            for (int i = 0; i < result_count; i++) {
                if (!results[i].kernel) continue;
                
                printf("--- %s ---\n", results[i].kernel->name);
                
                if (results[i].result_count == 0) {
                    printf("  No block types matched criteria\n\n");
                    continue;
                }
                
                printf("  Block types tested: %u\n", results[i].result_count);
                
                /* Find top 5 block types by score */
                for (int j = 0; j < 5 && j < (int)results[i].result_count; j++) {
                    uint32_t best_idx = j;
                    for (uint32_t k = j + 1; k < results[i].result_count; k++) {
                        if (results[i].results[k].score > results[i].results[best_idx].score) {
                            best_idx = k;
                        }
                    }
                    /* Swap */
                    CA_KernelBlockResult temp = results[i].results[j];
                    results[i].results[j] = results[i].results[best_idx];
                    results[i].results[best_idx] = temp;
                    
                    printf("  #%d: Block %u (score: %.6f, activations: %u)\n",
                           j + 1, results[i].results[j].block_type,
                           results[i].results[j].score, results[i].results[j].total_activations);
                }
                
                /* Print subchunk analysis if available */
                if (results[i].subchunk_analysis && subchunk_divisor > 0) {
                    printf("  Subchunk analysis (%d subchunks):\n", subchunk_divisor);
                    printf("    Best: subchunk %d (unused IDs: %u, matched: %u, total unique: %u)\n",
                           results[i].subchunk_analysis->best_subchunk,
                           (uint32_t)results[i].subchunk_analysis->best_score,
                           results[i].subchunk_analysis->subchunk_results[results[i].subchunk_analysis->best_subchunk].matched_block_count,
                           results[i].subchunk_analysis->subchunk_results[results[i].subchunk_analysis->best_subchunk].blocks_in_slice);
                    
                    /* Show top 3 subchunks by unused ID count */
                    int top_count = 0;
                    for (uint32_t s = 0; s < results[i].subchunk_analysis->num_subchunks && top_count < 3; s++) {
                        uint32_t unused = results[i].subchunk_analysis->subchunk_results[s].unused_id_count;
                        if (unused > 0) {
                            printf("    #%d: subchunk %d (unused IDs: %u, matched: %u, total: %u)\n",
                                   top_count + 1, s,
                                   unused,
                                   results[i].subchunk_analysis->subchunk_results[s].matched_block_count,
                                   results[i].subchunk_analysis->subchunk_results[s].blocks_in_slice);
                            top_count++;
                        }
                    }
                }
                
                printf("\n");
            }
            
            /* Export to CSV if requested */
            if (kernel_export_file) {
                result = ca_export_kernel_results_csv(results, result_count, kernel_export_file,
                                                      cx1, cz1, cx2, cz2, stride);
                if (result == 0) {
                    printf("Exported kernel results to: %s\n", kernel_export_file);
                } else {
                    fprintf(stderr, "Error: Failed to export kernel results\n");
                }
            }
            
            /* Free results */
            for (int i = 0; i < result_count; i++) {
                ca_free_kernel_analysis_result(&results[i]);
            }
            free(results);
        } else {
            fprintf(stderr, "Error: Kernel analysis failed\n");
        }
        
        printf("=== END KERNEL-BASED PATTERN ANALYSIS ===\n\n");
    }
    
    /* Block Kernel Efficiency Analysis */
    if (do_block_kernel_efficiency && subchunk_divisor > 0) {
        printf("=== BLOCK KERNEL EFFICIENCY ANALYSIS ===\n");
        printf("Finding best kernels for each block ID based on unused ID count\n\n");
        
        CA_BlockKernelEfficiency* efficiency_results = NULL;
        uint32_t efficiency_count = 0;
        
        clock_t eff_start = clock();
        int eff_result = ca_analyze_block_kernel_efficiency(collection, subchunk_divisor,
                                                            &efficiency_results, &efficiency_count);
        clock_t eff_end = clock();
        
        if (eff_result == 0 && efficiency_results) {
            printf("Block kernel efficiency analysis complete in %.2f ms\n", 
                   1000.0 * (eff_end - eff_start) / CLOCKS_PER_SEC);
            printf("Block IDs analyzed: %u\n\n", efficiency_count);
            
            /* Sort by efficiency score (highest first) */
            for (uint32_t i = 0; i < efficiency_count; i++) {
                for (uint32_t j = i + 1; j < efficiency_count; j++) {
                    if (efficiency_results[j].efficiency_score > efficiency_results[i].efficiency_score) {
                        CA_BlockKernelEfficiency temp = efficiency_results[i];
                        efficiency_results[i] = efficiency_results[j];
                        efficiency_results[j] = temp;
                    }
                }
            }
            
            /* Show top 20 blocks by efficiency */
            printf("Top 20 Block IDs by Kernel Efficiency:\n");
            for (uint32_t i = 0; i < 20 && i < efficiency_count; i++) {
                if (efficiency_results[i].best_kernel) {
                    printf("  #%2u: Block %5u - Best: %s (subchunk %d, unused: %u, matched: %u, efficiency: %.4f)\n",
                           i + 1,
                           efficiency_results[i].block_id,
                           efficiency_results[i].best_kernel->name,
                           efficiency_results[i].best_subchunk,
                           efficiency_results[i].best_unused_ids,
                           efficiency_results[i].best_matched_blocks,
                           efficiency_results[i].efficiency_score);
                }
            }
            printf("\n");
            
            ca_free_block_kernel_efficiency(efficiency_results, efficiency_count);
        } else {
            printf("Block kernel efficiency analysis failed\n\n");
        }
        
        printf("=== END BLOCK KERNEL EFFICIENCY ANALYSIS ===\n\n");
    }


    
    /* Allocate report */
    CA_AnalysisReport report;
    memset(&report, 0, sizeof(report));
    report.count = collection->count;
    report.analyses = (CA_ChunkAnalysis*)calloc(report.count, sizeof(CA_ChunkAnalysis));
    
    if (!report.analyses) {
        fprintf(stderr, "Error: Failed to allocate analysis memory\n");
        ca_free_collection(collection);
        free(collection);
        return 1;
    }
    
    /* Analyze each chunk */
    start = clock();
    
    for (int i = 0; i < collection->count; i++) {
        if (config.verbose) {
            printf("  Analyzing chunk %d/%d (%.1f%%)...\r", i + 1, collection->count,
                   100.0 * (i + 1) / collection->count);
            fflush(stdout);
        }
        
        report.analyses[i].chunk_idx = i;
        ca_analyze_single_chunk(&collection->chunks[i], &config, &report.analyses[i]);
    }
    
    if (config.verbose) printf("\n");
    
    clock_t analysis_end = clock();
    double analysis_time = 1000.0 * (analysis_end - start) / CLOCKS_PER_SEC;
    printf("Analysis complete in %.2f ms (%.2f ms per chunk)\n\n", 
           analysis_time, analysis_time / collection->count);
    
    /* Calculate global statistics */
    report.total_blocks = (uint32_t)collection->count * CA_CHUNK_TOTAL_BLOCKS;
    for (int i = 0; i < collection->count; i++) {
        report.total_non_air += report.analyses[i].block_stats.non_air_count;
        report.avg_entropy += report.analyses[i].block_stats.entropy;
        report.avg_compression_potential += report.analyses[i].estimated_compression_ratio;
    }
    report.avg_entropy /= collection->count;
    report.avg_compression_potential /= collection->count;
    
    /* Print summary */
    printf("=== STATISTICAL SUMMARY ===\n");
    printf("Total chunks: %d\n", collection->count);
    printf("Total blocks: %u (%.1f MB raw)\n", 
           report.total_blocks, 
           (double)report.total_blocks * sizeof(uint16_t) / (1024.0 * 1024.0));
    printf("Non-air blocks: %u (%.1f%%)\n", 
           report.total_non_air,
           100.0 * report.total_non_air / report.total_blocks);
    printf("Average entropy: %.2f bits\n", report.avg_entropy);
    printf("Basic compression potential: %.1f%%\n", report.avg_compression_potential);
    printf("(Note: Actual compression testing requires running ChunkCompression tool separately)\n\n");
    
    /* Per-chunk breakdown */
    printf("\n=== PER-CHUNK BREAKDOWN ===\n");
    printf("%-6s %-6s %-6s %-8s %-10s %-8s %-6s %s\n",
           "Index", "X", "Z", "Top Y", "Palette", "Non-air", "Ent", "Recommended");
    printf("%-6s %-6s %-6s %-8s %-10s %-8s %-6s %s\n",
           "-----", "-", "-", "-----", "-------", "-------", "---", "-----------");
    
    for (int i = 0; i < collection->count && i < 20; i++) {
        const CA_ChunkAnalysis* a = &report.analyses[i];
        printf("%5d  %5d  %5d  %5d    %-8s  %-8u  %6.1f  %s\n",
               i,
               collection->chunks[i].cx,
               collection->chunks[i].cz,
               (int)a->layer_stats.avg_surface_height > 0 ? 
                   (int)a->layer_stats.avg_surface_height : collection->chunks[i].top_y,
               a->block_stats.palette_size > 0 ? "Yes" : "No",
               a->block_stats.non_air_count,
               a->block_stats.entropy,
               ca_compression_method_name(a->recommended_compression_method));
    }
    if (collection->count > 20) {
        printf("... (%d more chunks)\n", collection->count - 20);
    }
    
    /* Top blocks across all chunks */
    printf("\n=== TOP 10 BLOCKS (ACROSS ALL CHUNKS) ===\n");
    
    /* Aggregate block counts */
    uint32_t total_block_counts[65536] = {0};
    for (int i = 0; i < collection->count; i++) {
        for (int b = 0; b < 65536; b++) {
            total_block_counts[b] += report.analyses[i].block_stats.block_counts[b];
        }
    }
    
    /* Find top 10 */
    typedef struct { uint16_t id; uint32_t count; } BlockCount;
    BlockCount top[10];
    memset(top, 0, sizeof(top));
    
    for (int b = 0; b < 65536; b++) {
        if (total_block_counts[b] > 0) {
            /* Check if in top 10 */
            for (int i = 0; i < 10; i++) {
                if (total_block_counts[b] > top[i].count) {
                    /* Shift and insert */
                    for (int j = 9; j > i; j--) {
                        top[j] = top[j-1];
                    }
                    top[i].id = (uint16_t)b;
                    top[i].count = total_block_counts[b];
                    break;
                }
            }
        }
    }
    
    for (int i = 0; i < 10 && top[i].count > 0; i++) {
        printf("  #%d: Block %u - %u occurrences (%.2f%%)\n",
               i + 1, top[i].id, top[i].count,
               100.0 * top[i].count / report.total_blocks);
    }
    
    /* Export if enabled */
    char csv_stats_filename[512];
    char json_filename[512];
    
    snprintf(csv_stats_filename, sizeof(csv_stats_filename), "%s_stats.csv", config.output_prefix);
    snprintf(json_filename, sizeof(json_filename), "%s_report.json", config.output_prefix);
    
    printf("\n=== EXPORT ===\n");
    
    if (ca_export_csv_stats(&report, csv_stats_filename) == 0) {
        printf("Stats exported to: %s\n", csv_stats_filename);
    }
    
    if (ca_export_json_report(&report, json_filename) == 0) {
        printf("JSON report exported to: %s\n", json_filename);
    }
    
    /* Cleanup */
    for (int i = 0; i < report.count; i++) {
        ca_free_volume_list(&report.analyses[i].volumes);
        ca_free_layer_list(&report.analyses[i].layers);
    }
    
    free(report.analyses);
    if (report.similarity_matrix) free(report.similarity_matrix);
    if (report.cluster_assignments) free(report.cluster_assignments);
    if (report.cluster_info) free(report.cluster_info);
    
    ca_free_collection(collection);
    free(collection);
    
    printf("\nAnalysis complete.\n");
    return 0;
}

#ifndef SDK_CHUNK_ANALYSIS_NO_MAIN
int main(int argc, char** argv) {
    return chunk_analysis_cli_main(argc, argv);
}
#endif
