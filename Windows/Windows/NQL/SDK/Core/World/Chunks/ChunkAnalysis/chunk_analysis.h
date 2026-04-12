#ifndef CHUNK_ANALYSIS_H
#define CHUNK_ANALYSIS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * Chunk Analysis Library
 * 
 * Analyzes 64x64x1024 voxel chunks to identify patterns, structures,
 * and relationships for compression optimization and world understanding.
 * ============================================================================ */

#define CA_CHUNK_SIZE_X 64
#define CA_CHUNK_SIZE_Z 64
#define CA_CHUNK_SIZE_Y 1024
#define CA_CHUNK_TOTAL_BLOCKS (CA_CHUNK_SIZE_X * CA_CHUNK_SIZE_Z * CA_CHUNK_SIZE_Y)
#define CA_MAX_BLOCK_ID 65535

/* Maximum number of kernels that can be registered */
#define CA_MAX_KERNELS 64

/* ============================================================================
 * Core Data Structures
 * ============================================================================ */

typedef struct {
    int32_t cx, cz;
    uint16_t top_y;
    uint16_t blocks[CA_CHUNK_SIZE_X][CA_CHUNK_SIZE_Z][CA_CHUNK_SIZE_Y];
} CA_RawChunk;

typedef struct {
    CA_RawChunk* chunks;
    int count;
    int capacity;
} CA_ChunkCollection;

/* ============================================================================
 * Superchunk Detection (16x16 chunk areas bounded by wall chunks)
 * ============================================================================ */

typedef struct {
    int32_t cx_min, cz_min;  /* Minimum chunk coordinates */
    int32_t cx_max, cz_max;  /* Maximum chunk coordinates (inclusive) */
    int* chunk_indices;      /* Indices of chunks in this superchunk */
    int chunk_count;         /* Number of chunks in this superchunk */
} CA_Superchunk;

/* ============================================================================
 * Statistical Analysis Results
 * ============================================================================ */

typedef struct {
    uint32_t block_counts[65536];
    uint16_t palette[65536];
    uint32_t palette_size;
    uint32_t non_air_count;
    double entropy;
    double compression_potential;
} CA_BlockStats;

typedef struct {
    uint32_t blocks_per_layer[CA_CHUNK_SIZE_Y];
    uint32_t air_per_layer[CA_CHUNK_SIZE_Y];
    double layer_entropy[CA_CHUNK_SIZE_Y];
    uint16_t surface_height[CA_CHUNK_SIZE_X][CA_CHUNK_SIZE_Z];
    double avg_surface_height;
    double surface_variance;
} CA_LayerStats;

/* ============================================================================
 * Geometric Analysis Results
 * ============================================================================ */

typedef struct {
    uint16_t x1, y1, z1;
    uint16_t x2, y2, z2;
    uint16_t block_id;
    uint32_t volume;
    bool is_rectangular;
} CA_Volume;

typedef struct {
    CA_Volume* volumes;
    uint32_t count;
    uint32_t capacity;
    uint32_t total_volume_blocks;
    uint32_t large_volumes;  /* > 1000 blocks */
    uint32_t medium_volumes; /* 100-1000 blocks */
    uint32_t small_volumes;  /* < 100 blocks */
} CA_VolumeList;

typedef struct {
    uint16_t y_start;
    uint16_t y_end;
    uint16_t dominant_block;
    uint32_t block_count;
    double uniformity;  /* 0-1, how consistent the layer is */
} CA_Layer;

typedef struct {
    CA_Layer* layers;
    uint32_t count;
    uint32_t capacity;
} CA_LayerList;

typedef struct {
    char name[32];
    float match_score;
    uint32_t template_id;
} CA_TemplateMatch;

/* ============================================================================
 * Similarity Analysis Results
 * ============================================================================ */

typedef struct {
    int chunk1_idx;
    int chunk2_idx;
    float similarity;        /* 0-1, block-by-block match */
    float structural_sim;    /* 0-1, ignoring block types */
    uint32_t diff_count;
} CA_ChunkPairSimilarity;

typedef struct {
    int chunk_idx;
    int cluster_id;
    float distance_to_centroid;
} CA_ClusterAssignment;

typedef struct {
    int cluster_id;
    int chunk_count;
    float avg_entropy;
    uint16_t dominant_block;
    char description[64];
} CA_ClusterInfo;

/* ============================================================================
 * Comprehensive Analysis Report
 * ============================================================================ */

typedef struct {
    int chunk_idx;
    
    /* Statistical */
    CA_BlockStats block_stats;
    CA_LayerStats layer_stats;
    
    /* Geometric */
    CA_VolumeList volumes;
    CA_LayerList layers;
    CA_TemplateMatch best_template;
    
    /* Similarity (populated later) */
    int most_similar_neighbor;
    float max_similarity;
    int cluster_id;
    
    /* Recommendations */
    int recommended_compression_method;
    double estimated_compression_ratio;
} CA_ChunkAnalysis;

typedef struct {
    CA_ChunkAnalysis* analyses;
    int count;
    
    /* Global statistics */
    double avg_entropy;
    double avg_compression_potential;
    uint32_t total_blocks;
    uint32_t total_non_air;
    
    /* Similarity matrix (flat array: count * count) */
    float* similarity_matrix;
    
    /* Clustering results */
    CA_ClusterAssignment* cluster_assignments;
    CA_ClusterInfo* cluster_info;
    int num_clusters;
} CA_AnalysisReport;

/* ============================================================================
 * Analysis Configuration
 * ============================================================================ */

typedef struct {
    bool do_statistical;
    bool do_geometric;
    bool do_similarity;
    bool do_clustering;
    int num_clusters;
    bool verbose;
    char output_prefix[256];
} CA_AnalysisConfig;

/* ============================================================================
 * Core Functions
 * ============================================================================ */

/* Collection Management */
int ca_load_json_save(const char* filename, CA_ChunkCollection* collection, int max_chunks);
void ca_free_collection(CA_ChunkCollection* collection);

/* Statistical Analysis */
int ca_analyze_block_stats(const CA_RawChunk* chunk, CA_BlockStats* stats);
int ca_analyze_layer_stats(const CA_RawChunk* chunk, CA_LayerStats* stats);
double ca_calculate_entropy(const uint32_t* counts, uint32_t total);
double ca_estimate_compression_potential(const CA_BlockStats* stats, const CA_LayerStats* layer_stats);

/* Geometric Analysis */
int ca_detect_volumes(const CA_RawChunk* chunk, CA_VolumeList* volumes);
int ca_identify_layers(const CA_RawChunk* chunk, CA_LayerList* layers);
int ca_match_templates(const CA_RawChunk* chunk, CA_TemplateMatch* matches, int max_matches);
void ca_free_volume_list(CA_VolumeList* volumes);
void ca_free_layer_list(CA_LayerList* layers);

/* Similarity Analysis */
float ca_calculate_similarity(const CA_RawChunk* chunk1, const CA_RawChunk* chunk2, uint32_t* out_diff_count);
float ca_calculate_structural_similarity(const CA_RawChunk* chunk1, const CA_RawChunk* chunk2);
int ca_build_similarity_matrix(const CA_ChunkCollection* collection, float** out_matrix);
int ca_find_most_similar_pairs(const CA_ChunkCollection* collection, const float* similarity_matrix,
                                int* most_similar_indices, float* max_similarities);

/* Clustering */
int ca_cluster_chunks(const CA_ChunkCollection* collection, const float* similarity_matrix,
                      int num_clusters, CA_ClusterAssignment* assignments, CA_ClusterInfo* cluster_info);

/* Comprehensive Analysis */
int ca_analyze_single_chunk(const CA_RawChunk* chunk, const CA_AnalysisConfig* config, 
                            CA_ChunkAnalysis* analysis);
int ca_analyze_all_chunks(const CA_ChunkCollection* collection, const CA_AnalysisConfig* config,
                          CA_AnalysisReport* report);
void ca_free_report(CA_AnalysisReport* report);

/* Output */
int ca_export_csv_stats(const CA_AnalysisReport* report, const char* filename);
int ca_export_csv_similarity(const CA_AnalysisReport* report, const char* filename);
int ca_export_json_report(const CA_AnalysisReport* report, const char* filename);
void ca_print_console_summary(const CA_AnalysisReport* report);
void ca_print_chunk_analysis(const CA_ChunkAnalysis* analysis);

/* Utility */
const char* ca_compression_method_name(int method_id);
int ca_recommend_compression_method(const CA_ChunkAnalysis* analysis);

/* ============================================================================
 * Block Type Distribution Analysis (for ID scope optimization)
 * ============================================================================ */

/* Wall/Superchunk Detection */
int ca_detect_wall_chunks(const CA_ChunkCollection* collection, bool* is_wall);
int ca_detect_superchunks(const CA_ChunkCollection* collection, const bool* is_wall, 
                          CA_Superchunk* superchunks, int max_superchunks);
void ca_free_superchunks(CA_Superchunk* superchunks, int count);

/* Block Type Counting */
int ca_count_block_types_chunk(const CA_RawChunk* chunk, uint32_t* out_count);
int ca_count_block_types_superchunk(const CA_ChunkCollection* collection, const CA_Superchunk* sc, 
                                    uint32_t* out_count);
int ca_count_block_types_range(const CA_ChunkCollection* collection, int cx1, int cz1, 
                               int cx2, int cz2, uint32_t* out_count);

/* Shared Block Type Analysis */
int ca_count_shared_block_types_superchunk(const CA_ChunkCollection* collection, const CA_Superchunk* sc,
                                           uint32_t* out_shared_count);
int ca_count_shared_block_types_range(const CA_ChunkCollection* collection, int cx1, int cz1,
                                     int cx2, int cz2, uint32_t* out_shared_count);

/* Block ID Distribution */
int ca_block_id_distribution_superchunk(const CA_ChunkCollection* collection, const CA_Superchunk* sc,
                                       uint16_t* out_block_ids, uint32_t* out_chunk_counts, uint32_t* out_count);
int ca_block_id_distribution_range(const CA_ChunkCollection* collection, int cx1, int cz1,
                                 int cx2, int cz2, uint16_t* out_block_ids, uint32_t* out_chunk_counts, uint32_t* out_count);

/* ============================================================================
 * Kernel-Based Pattern Analysis (CNN-inspired for block ID scoping)
 * ============================================================================ */

typedef enum {
    KERNEL_TYPE_SPATIAL,      /* Spatial convolution patterns */
    KERNEL_TYPE_FREQUENCY,    /* Statistical frequency patterns */
    KERNEL_TYPE_COOCCURRENCE, /* Block association patterns */
    KERNEL_TYPE_DISTRIBUTION  /* Spatial distribution patterns */
} CA_KernelType;

typedef struct {
    CA_KernelType type;
    char name[64];
    char description[256];
    int size;  /* N for NxN kernel (no max limit) */
    bool* pattern;  /* Heap-allocated pattern mask (size×size) */
    int pattern_size;  /* size×size */
    float threshold;  /* Threshold value for FREQUENCY kernels */
} CA_Kernel;

/* ============================================================================
 * 3D Subchunk Analysis
 * ============================================================================ */

typedef struct {
    uint16_t* block_ids;
    uint32_t count;
    int subchunk_index;  /* Y subchunk index */
} CA_SubchunkBlockSet;

typedef struct {
    CA_SubchunkBlockSet* subchunks;
    uint32_t num_subchunks;
} CA_ChunkSubchunkData;

typedef struct {
    const CA_Kernel* kernel;
    int subchunk_index;
    double score;  /* Adjusted for subchunk size */
    double raw_score;  /* Original score before adjustment */
    uint32_t total_activations;
    uint32_t blocks_in_slice;
    uint32_t unused_id_count;  /* Block types NOT matched = wasted IDs */
    uint32_t matched_block_count;  /* Block types matched */
} CA_SubchunkKernelResult;

typedef struct {
    const CA_Kernel* kernel;
    CA_SubchunkKernelResult* subchunk_results;
    uint32_t num_subchunks;
    double best_score;
    int best_subchunk;
} CA_KernelSubchunkAnalysis;

/* Per-Block ID Kernel Efficiency Analysis */
typedef struct {
    uint16_t block_id;
    const CA_Kernel* best_kernel;
    int best_subchunk;
    uint32_t best_unused_ids;
    uint32_t best_matched_blocks;
    double efficiency_score;  /* unused_ids / total_unique */
} CA_BlockKernelEfficiency;

typedef struct {
    uint16_t block_type;
    uint32_t total_activations;
    double score;  /* Normalized score 0-1 */
} CA_KernelBlockResult;

typedef struct {
    const CA_Kernel* kernel;
    CA_KernelBlockResult* results;
    uint32_t result_count;
    CA_KernelSubchunkAnalysis* subchunk_analysis;  /* Optional subchunk analysis */
} CA_KernelAnalysisResult;

typedef struct {
    uint16_t block_ids[256];  /* Unique block IDs in chunk */
    uint32_t count;
} CA_ChunkBlockSet;

typedef struct {
    const CA_Kernel* kernel;
    double score;
    uint32_t activations;
} CA_KernelRanking;

typedef struct {
    const CA_Kernel* kernel;
    double pass_rate;          /* (chunks_found / chunks_tested) */
    uint32_t chunks_tested;
    uint32_t chunks_found;
} CA_KernelCoverage;

typedef struct {
    uint16_t block_id;
    uint32_t count;
    double percentage;
} CA_BlockStat;

/* Kernel Registration */
int ca_register_kernel(const CA_Kernel* kernel);
const CA_Kernel* ca_get_kernel_by_name(const char* name);
int ca_get_all_kernels(const CA_Kernel** out_kernels, int max_kernels);
void ca_init_default_kernels(void);

/* Block Set Extraction */
int ca_extract_chunk_block_sets(const CA_ChunkCollection* collection, CA_ChunkBlockSet* out_sets);

/* Kernel Application */
int ca_apply_kernel_to_region(const CA_ChunkCollection* collection, const CA_Kernel* kernel,
                              const CA_ChunkBlockSet* block_sets,
                              int cx1, int cz1, int cx2, int cz2, int stride,
                              CA_KernelAnalysisResult* out_result);
int ca_apply_frequency_kernel_to_region(const CA_ChunkCollection* collection, const CA_Kernel* kernel,
                                        const CA_ChunkBlockSet* block_sets,
                                        int cx1, int cz1, int cx2, int cz2,
                                        CA_KernelAnalysisResult* out_result);
void ca_free_kernel_analysis_result(CA_KernelAnalysisResult* result);

/* Comprehensive Region Analysis */
int ca_analyze_region_full(const CA_ChunkCollection* collection, int cx1, int cz1, int cx2, int cz2,
                           int kernel_stride, int subchunk_divisor,
                           CA_KernelAnalysisResult** out_results, int* out_result_count);

/* Export */
int ca_export_kernel_results_csv(const CA_KernelAnalysisResult* results, int result_count,
                                 const char* filename, int cx1, int cz1, int cx2, int cz2, int stride);

/* Block Inventory Queries */
int ca_get_blocks_in_region(const CA_ChunkCollection* collection, int cx1, int cz1, int cx2, int cz2,
                            uint16_t* out_blocks, uint32_t* out_count);
int ca_get_blocks_in_region_detailed(const CA_ChunkCollection* collection, int cx1, int cz1, int cx2, int cz2,
                                     CA_BlockStat* out_stats, uint32_t* out_count);

/* Reverse Kernel Lookup */
int ca_rank_kernels_for_block(const CA_ChunkCollection* collection, uint16_t block_id,
                              int cx1, int cz1, int cx2, int cz2, int kernel_stride,
                              CA_KernelRanking* out_rankings, uint32_t* out_count);
int ca_rank_kernels_for_blocks(const CA_ChunkCollection* collection, const uint16_t* block_ids, uint32_t num_blocks,
                               int cx1, int cz1, int cx2, int cz2, int kernel_stride,
                               CA_KernelRanking* out_rankings, uint32_t* out_count);

/* Coverage Optimization */
int ca_analyze_kernel_coverage_for_block(const CA_ChunkCollection* collection, uint16_t block_id,
                                        int cx1, int cz1, int cx2, int cz2, int kernel_stride,
                                        CA_KernelCoverage* out_coverage, uint32_t* out_count);
int ca_find_minimal_kernel_set(const CA_ChunkCollection* collection, uint16_t block_id,
                              double min_pass_rate, int cx1, int cz1, int cx2, int cz2, int kernel_stride,
                              int time_budget_ms, const CA_Kernel** out_kernel_set, uint32_t* out_set_size);

/* Memory Management */
void ca_free_kernel_rankings(CA_KernelRanking* rankings);
void ca_free_coverage_stats(CA_KernelCoverage* coverage);

int ca_extract_subchunk_data(const CA_ChunkCollection* collection, int y_divisor,
                             CA_ChunkSubchunkData* out_data);
int ca_apply_kernel_to_subchunk(const CA_ChunkCollection* collection, const CA_Kernel* kernel,
                               const CA_ChunkSubchunkData* subchunk_data, int subchunk_index,
                               int cx1, int cz1, int cx2, int cz2, int stride,
                               CA_KernelAnalysisResult* out_result);
int ca_analyze_subchunks_for_kernel(const CA_ChunkCollection* collection, const CA_Kernel* kernel,
                                   const CA_ChunkSubchunkData* subchunk_data,
                                   int cx1, int cz1, int cx2, int cz2, int stride,
                                   CA_KernelSubchunkAnalysis* out_analysis);
void ca_free_subchunk_data(CA_ChunkSubchunkData* data, uint32_t count);
void ca_free_subchunk_analysis(CA_KernelSubchunkAnalysis* analysis);

/* Per-Block ID Kernel Efficiency Analysis */
int ca_analyze_block_kernel_efficiency(const CA_ChunkCollection* collection, int subchunk_divisor,
                                       CA_BlockKernelEfficiency** out_results, uint32_t* out_count);
void ca_free_block_kernel_efficiency(CA_BlockKernelEfficiency* results, uint32_t count);

/* ============================================================================
 * Wall Chunk Analysis
 * ============================================================================ */

typedef struct {
    int32_t cx, cz;
    uint32_t wall_block_count;
    bool has_wall_blocks;
    bool expected_wall;
    bool is_problematic;  /* Has wall blocks but not expected, or vice versa */
    bool is_air_only;  /* Chunk is entirely or mostly air */
    const char* side_name;  /* "WEST", "EAST", "NORTH", "SOUTH", "CORNER_*", "NONE" */
} CA_WallChunkInfo;

typedef struct {
    int chunk_span;
    int wall_grid_size;
    int wall_grid_offset_x;
    int wall_grid_offset_z;
    int wall_thickness_blocks;
    bool wall_rings_shared;
    int air_threshold_percent;  /* Percentage threshold for air-only chunks */
    uint8_t coordinate_system;  /* SdkWorldCoordinateSystem */
} CA_WorldConfig;

typedef struct {
    CA_WorldConfig config;
    int analyzed_chunk_count;
    int expected_wall_chunk_count;
    int correct_wall_chunk_count;
    int missing_wall_chunk_count;
    int unexpected_wall_chunk_count;
    int problematic_wall_chunk_count;
    bool pass;
} CA_WallAnalysisSummary;

/* Wall Detection */
int ca_detect_wall_blocks_chunk(const CA_RawChunk* chunk, uint32_t* out_wall_block_count);
int ca_detect_wall_blocks_collection(const CA_ChunkCollection* collection, CA_WallChunkInfo* out_info,
                                     int air_threshold_percent);

/* Expected Wall Calculation (from debugger logic) */
int ca_load_world_config(const char* save_path, CA_WorldConfig* out_config);
int ca_calculate_expected_walls(const CA_ChunkCollection* collection, const CA_WorldConfig* config,
                                 CA_WallChunkInfo* wall_info);
int ca_is_expected_wall_chunk(int cx, int cz, int chunk_span,
                              int wall_grid_size, int wall_grid_offset_x, int wall_grid_offset_z,
                              bool wall_rings_shared,
                              uint8_t coordinate_system,
                              const char** out_side_name);

/* Wall Comparison */
int ca_compare_wall_mapping(const CA_ChunkCollection* collection, CA_WallChunkInfo* wall_info,
                            uint32_t wall_threshold);

/* Wall Analysis Output */
int ca_generate_wall_grid(const CA_ChunkCollection* collection, const CA_WallChunkInfo* wall_info,
                         int range, char* out_grid, size_t grid_size);
int ca_export_wall_analysis_csv(const CA_ChunkCollection* collection, const CA_WallChunkInfo* wall_info,
                                const char* filename);
void ca_print_wall_analysis_summary(const CA_ChunkCollection* collection, const CA_WallChunkInfo* wall_info,
                                    const CA_WorldConfig* config);
int ca_analyze_wall_summary(const char* save_path,
                            int max_chunks,
                            int air_threshold_percent,
                            uint32_t wall_threshold,
                            CA_WallAnalysisSummary* out_summary);

#endif /* CHUNK_ANALYSIS_H */
