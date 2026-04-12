/**
 * bench_worldgen_chunk.c - Chunk-level terrain generation benchmark
 */
#include "bench_common.h"
#include "../Core/World/Worldgen/sdk_worldgen.h"
#include "../Core/World/Chunks/sdk_chunk.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    double single_chunk_ms;
    double batch_10_chunks_ms;
    double batch_100_chunks_ms;
    double column_sample_ms;
    uint64_t memory_mb;
} ChunkGenBenchResults;

static void bench_single_chunk(ChunkGenBenchResults* results)
{
    BenchTimer timer;
    SdkChunk chunk;
    uint32_t seed = 12345;
    
    bench_timer_init(&timer);
    sdk_chunk_init(&chunk, 0, 0);
    
    bench_timer_start(&timer);
    sdk_worldgen_generate_chunk(&chunk, seed);
    results->single_chunk_ms = bench_timer_end_ms(&timer);
    
    sdk_chunk_shutdown(&chunk);
}

static void bench_batch_chunks(ChunkGenBenchResults* results)
{
    BenchTimer timer;
    uint32_t seed = 12345;
    int batch_size;
    
    bench_timer_init(&timer);
    
    batch_size = 10;
    bench_timer_start(&timer);
    for (int i = 0; i < batch_size; i++) {
        SdkChunk chunk;
        sdk_chunk_init(&chunk, i % 4, i / 4);
        sdk_worldgen_generate_chunk(&chunk, seed);
        sdk_chunk_shutdown(&chunk);
    }
    results->batch_10_chunks_ms = bench_timer_end_ms(&timer);
    
    batch_size = 100;
    bench_timer_start(&timer);
    for (int i = 0; i < batch_size; i++) {
        SdkChunk chunk;
        sdk_chunk_init(&chunk, i % 10, i / 10);
        sdk_worldgen_generate_chunk(&chunk, seed);
        sdk_chunk_shutdown(&chunk);
    }
    results->batch_100_chunks_ms = bench_timer_end_ms(&timer);
}

static void bench_column_sampling(ChunkGenBenchResults* results)
{
    BenchTimer timer;
    int iterations = 10000;
    
    bench_timer_init(&timer);
    bench_timer_start(&timer);
    for (int i = 0; i < iterations; i++) {
        int wx = i % 100;
        int wz = i / 100;
        sdk_worldgen_get_surface_y(wx, wz);
    }
    results->column_sample_ms = bench_timer_end_ms(&timer) / iterations;
}

int main(void)
{
    ChunkGenBenchResults results = {0};
    BenchJsonWriter json;
    BenchCsvWriter csv;
    
    printf("Running chunk worldgen benchmarks...\n");
    
    bench_single_chunk(&results);
    bench_batch_chunks(&results);
    bench_column_sampling(&results);
    results.memory_mb = bench_get_memory_usage_mb();
    
    printf("  Single chunk:       %.3f ms\n", results.single_chunk_ms);
    printf("  Batch 10 chunks:    %.3f ms\n", results.batch_10_chunks_ms);
    printf("  Batch 100 chunks:   %.3f ms\n", results.batch_100_chunks_ms);
    printf("  Column sample avg:  %.6f ms\n", results.column_sample_ms);
    printf("  Memory usage: %llu MB\n", results.memory_mb);
    
    bench_json_start(&json, "bench_worldgen_chunk.json");
    bench_json_write_string(&json, "benchmark", "worldgen_chunk");
    bench_json_write_double(&json, "single_chunk_ms", results.single_chunk_ms);
    bench_json_write_double(&json, "batch_10_chunks_ms", results.batch_10_chunks_ms);
    bench_json_write_double(&json, "batch_100_chunks_ms", results.batch_100_chunks_ms);
    bench_json_write_double(&json, "column_sample_ms", results.column_sample_ms);
    bench_json_write_int(&json, "memory_mb", (int)results.memory_mb);
    bench_json_end(&json);
    
    const char* headers[] = {"benchmark", "single_chunk_ms", "batch_10_chunks_ms", 
                              "batch_100_chunks_ms", "column_sample_ms", "memory_mb"};
    bench_csv_start(&csv, "bench_worldgen_chunk.csv");
    bench_csv_write_header(&csv, headers, 6);
    bench_csv_write_row_start(&csv);
    bench_csv_write_string(&csv, "worldgen_chunk");
    bench_csv_write_double(&csv, results.single_chunk_ms);
    bench_csv_write_double(&csv, results.batch_10_chunks_ms);
    bench_csv_write_double(&csv, results.batch_100_chunks_ms);
    bench_csv_write_double(&csv, results.column_sample_ms);
    bench_csv_write_int(&csv, (int)results.memory_mb);
    bench_csv_write_row_end(&csv);
    bench_csv_end(&csv);
    
    printf("Results written to bench_worldgen_chunk.json/csv\n");
    return 0;
}
