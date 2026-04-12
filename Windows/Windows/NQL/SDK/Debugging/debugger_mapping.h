/**
 * debugger_mapping.h -- Chunk/wall mapping analysis for worldgen debugger
 */
#ifndef DEBUGGER_MAPPING_H
#define DEBUGGER_MAPPING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int cx;
    int cz;
    int scx;
    int scz;
    int origin_cx;
    int origin_cz;
    bool is_wall;
    bool is_west;
    bool is_east;
    bool is_north;
    bool is_south;
    const char* side_name;
} ChunkMappingResult;

typedef struct {
    int total_chunks;
    int wall_chunks;
    int superchunk_chunks;
    int corner_chunks;
    int west_wall_chunks;
    int east_wall_chunks;
    int north_wall_chunks;
    int south_wall_chunks;
} MappingStatistics;

/**
 * Analyze chunk mapping for a range of coordinates.
 * @param chunk_span Superchunk chunk span (e.g., 16)
 * @param walls_detached Whether walls are detached mode
 * @param wall_grid_size Wall-grid size metadata/runtime input
 * @param offset_x Grid offset X
 * @param offset_z Grid offset Z
 * @param range Coordinate range to analyze (e.g., 34 means -34 to +34)
 * @param results Array to store results (must be large enough)
 * @param max_results Maximum number of results to store
 * @return Number of results stored
 */
int analyze_chunk_mapping(int chunk_span, bool walls_detached, int wall_grid_size,
                         int offset_x, int offset_z, int range,
                         ChunkMappingResult* results, int max_results);

/**
 * Calculate mapping statistics from results.
 */
void calculate_statistics(const ChunkMappingResult* results, int count,
                          MappingStatistics* out_stats);

/**
 * Generate ASCII grid visualization of wall chunk positions.
 * @param results Mapping results
 * @param count Number of results
 * @param range Coordinate range used
 * @param output Buffer to write visualization to
 * @param output_size Size of output buffer
 */
void generate_ascii_grid(const ChunkMappingResult* results, int count,
                         int range, char* output, int output_size);

/**
 * Export mapping results to CSV format.
 * @param results Mapping results
 * @param count Number of results
 * @param filepath Output file path
 * @return 1 on success, 0 on failure
 */
int export_csv(const ChunkMappingResult* results, int count, const char* filepath);

/**
 * Print summary statistics to console.
 */
void print_summary_statistics(const MappingStatistics* stats, int chunk_span,
                              bool walls_detached, int wall_grid_size,
                              int offset_x, int offset_z);

/**
 * Create a detached-wall preview visualization.
 * This preview mirrors the detached wall-grid math used by the runtime wall
 * classification helpers and save/debug tooling.
 * @param wall_grid_size Total grid size including walls (e.g., 18 for 16x16 interior + walls)
 * @param offset_x Grid offset X
 * @param offset_z Grid offset Z
 * @param range Coordinate range to visualize
 * @param output Buffer to write visualization to
 * @param output_size Size of output buffer
 * @return 1 on success, 0 on failure
 */
int create_world_theoretical(int wall_grid_size, int offset_x, int offset_z,
                              int range, char* output, int output_size);

/**
 * Generate a real world with actual chunk data that can be loaded in the game.
 * Creates save.json and meta.txt files at the specified output path.
 * @param seed World seed
 * @param gen_distance_superchunks Generation distance in superchunks (N means NxN superchunks)
 * @param chunk_span Superchunk chunk span (e.g., 16)
 * @param walls_enabled Whether to generate walls
 * @param walls_detached Whether walls are detached from superchunks
 * @param wall_grid_size Wall grid size for detached mode
 * @param offset_x Grid offset X
 * @param offset_z Grid offset Z
 * @param output_path Output directory path (e.g., WorldSaves/world_123)
 * @param output Buffer to write status/preview to
 * @param output_size Size of output buffer
 * @return 1 on success, 0 on failure
 */
int generate_real_world(uint32_t seed, int gen_distance_superchunks,
                        int chunk_span, bool walls_enabled, bool walls_detached,
                        int wall_grid_size, int offset_x, int offset_z,
                        bool construction_cells_enabled,
                        const char* output_path, char* output, int output_size);

#ifdef __cplusplus
}
#endif

#endif /* DEBUGGER_MAPPING_H */
