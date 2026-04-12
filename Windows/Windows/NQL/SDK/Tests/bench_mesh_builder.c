/**
 * bench_mesh_builder.c - Voxel mesh generation benchmark
 */
#include "bench_common.h"
#include "../Core/MeshBuilder/sdk_mesh_builder.h"
#include "../Core/World/Chunks/sdk_chunk.h"
#include "../Core/World/Blocks/sdk_block.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double empty_chunk_ms;
    double full_chunk_ms;
    double complex_chunk_ms;
    int empty_vertex_count;
    int full_vertex_count;
    int complex_vertex_count;
    uint64_t memory_mb;
} MeshBuildBenchResults;

static void setup_empty_chunk(SdkChunk* chunk)
{
    sdk_chunk_init(chunk, 0, 0);
    memset(chunk->blocks, BLOCK_AIR, CHUNK_TOTAL_BLOCKS);
}

static void setup_full_chunk(SdkChunk* chunk)
{
    sdk_chunk_init(chunk, 0, 0);
    memset(chunk->blocks, BLOCK_STONE, CHUNK_TOTAL_BLOCKS);
}

static void setup_complex_chunk(SdkChunk* chunk)
{
    sdk_chunk_init(chunk, 0, 0);
    for (int y = 0; y < CHUNK_HEIGHT; y++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            for (int x = 0; x < CHUNK_SIZE; x++) {
                int idx = y * CHUNK_AREA + z * CHUNK_SIZE + x;
                if (y < 32) {
                    chunk->blocks[idx] = BLOCK_STONE;
                } else if (y < 64) {
                    chunk->blocks[idx] = ((x + z) % 2) ? BLOCK_DIRT : BLOCK_STONE;
                } else if (y == 64) {
                    chunk->blocks[idx] = BLOCK_GRASS;
                } else {
                    chunk->blocks[idx] = BLOCK_AIR;
                }
            }
        }
    }
}

static int count_vertices(SdkChunk* chunk)
{
    int total = 0;
    for (int i = 0; i < CHUNK_SUBCHUNK_COUNT; i++) {
        total += chunk->mesh_slices[i].vertex_count;
    }
    return total;
}

static void bench_mesh_generation(MeshBuildBenchResults* results)
{
    BenchTimer timer;
    SdkChunk chunk;
    
    bench_timer_init(&timer);
    
    setup_empty_chunk(&chunk);
    bench_timer_start(&timer);
    sdk_mesh_builder_build_chunk(&chunk);
    results->empty_chunk_ms = bench_timer_end_ms(&timer);
    results->empty_vertex_count = count_vertices(&chunk);
    sdk_chunk_shutdown(&chunk);
    
    setup_full_chunk(&chunk);
    bench_timer_start(&timer);
    sdk_mesh_builder_build_chunk(&chunk);
    results->full_chunk_ms = bench_timer_end_ms(&timer);
    results->full_vertex_count = count_vertices(&chunk);
    sdk_chunk_shutdown(&chunk);
    
    setup_complex_chunk(&chunk);
    bench_timer_start(&timer);
    sdk_mesh_builder_build_chunk(&chunk);
    results->complex_chunk_ms = bench_timer_end_ms(&timer);
    results->complex_vertex_count = count_vertices(&chunk);
    sdk_chunk_shutdown(&chunk);
}

int main(void)
{
    MeshBuildBenchResults results = {0};
    BenchJsonWriter json;
    BenchCsvWriter csv;
    
    printf("Running mesh builder benchmarks...\n");
    
    bench_mesh_generation(&results);
    results.memory_mb = bench_get_memory_usage_mb();
    
    printf("  Empty chunk:   %.3f ms (%d vertices)\n", results.empty_chunk_ms, results.empty_vertex_count);
    printf("  Full chunk:    %.3f ms (%d vertices)\n", results.full_chunk_ms, results.full_vertex_count);
    printf("  Complex chunk: %.3f ms (%d vertices)\n", results.complex_chunk_ms, results.complex_vertex_count);
    printf("  Memory usage: %llu MB\n", results.memory_mb);
    
    bench_json_start(&json, "bench_mesh_builder.json");
    bench_json_write_string(&json, "benchmark", "mesh_builder");
    bench_json_write_double(&json, "empty_chunk_ms", results.empty_chunk_ms);
    bench_json_write_double(&json, "full_chunk_ms", results.full_chunk_ms);
    bench_json_write_double(&json, "complex_chunk_ms", results.complex_chunk_ms);
    bench_json_write_int(&json, "empty_vertex_count", results.empty_vertex_count);
    bench_json_write_int(&json, "full_vertex_count", results.full_vertex_count);
    bench_json_write_int(&json, "complex_vertex_count", results.complex_vertex_count);
    bench_json_write_int(&json, "memory_mb", (int)results.memory_mb);
    bench_json_end(&json);
    
    const char* headers[] = {"benchmark", "empty_chunk_ms", "full_chunk_ms", "complex_chunk_ms",
                              "empty_vertices", "full_vertices", "complex_vertices", "memory_mb"};
    bench_csv_start(&csv, "bench_mesh_builder.csv");
    bench_csv_write_header(&csv, headers, 8);
    bench_csv_write_row_start(&csv);
    bench_csv_write_string(&csv, "mesh_builder");
    bench_csv_write_double(&csv, results.empty_chunk_ms);
    bench_csv_write_double(&csv, results.full_chunk_ms);
    bench_csv_write_double(&csv, results.complex_chunk_ms);
    bench_csv_write_int(&csv, results.empty_vertex_count);
    bench_csv_write_int(&csv, results.full_vertex_count);
    bench_csv_write_int(&csv, results.complex_vertex_count);
    bench_csv_write_int(&csv, (int)results.memory_mb);
    bench_csv_write_row_end(&csv);
    bench_csv_end(&csv);
    
    printf("Results written to bench_mesh_builder.json/csv\n");
    return 0;
}
