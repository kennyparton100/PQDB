/**
 * bench_persistence.c - Save/load performance benchmark
 */
#include "bench_common.h"
#include "../Core/World/Persistence/sdk_persistence.h"
#include "../Core/World/Chunks/sdk_chunk.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    double chunk_serialize_ms;
    double chunk_deserialize_ms;
    uint64_t memory_mb;
} PersistenceBenchResults;

static void bench_chunk_serialization(PersistenceBenchResults* results)
{
    BenchTimer timer;
    SdkChunk chunk;
    uint8_t buffer[128 * 1024];
    size_t written;
    int iterations = 100;
    
    bench_timer_init(&timer);
    sdk_chunk_init(&chunk, 0, 0);
    
    for (int i = 0; i < CHUNK_TOTAL_BLOCKS; i++) {
        chunk.blocks[i] = (i % 2) ? BLOCK_STONE : BLOCK_DIRT;
    }
    
    bench_timer_start(&timer);
    for (int i = 0; i < iterations; i++) {
        sdk_chunk_serialize(&chunk, buffer, sizeof(buffer), &written);
    }
    results->chunk_serialize_ms = bench_timer_end_ms(&timer) / iterations;
    
    bench_timer_start(&timer);
    for (int i = 0; i < iterations; i++) {
        SdkChunk temp;
        sdk_chunk_init(&temp, 0, 0);
        sdk_chunk_deserialize(&temp, buffer, written);
        sdk_chunk_shutdown(&temp);
    }
    results->chunk_deserialize_ms = bench_timer_end_ms(&timer) / iterations;
    
    sdk_chunk_shutdown(&chunk);
}

int main(void)
{
    PersistenceBenchResults results = {0};
    BenchJsonWriter json;
    BenchCsvWriter csv;
    
    printf("Running persistence benchmarks...\n");
    
    bench_chunk_serialization(&results);
    results.memory_mb = bench_get_memory_usage_mb();
    
    printf("  Chunk serialize avg:   %.6f ms\n", results.chunk_serialize_ms);
    printf("  Chunk deserialize avg: %.6f ms\n", results.chunk_deserialize_ms);
    printf("  Memory usage: %llu MB\n", results.memory_mb);
    
    bench_json_start(&json, "bench_persistence.json");
    bench_json_write_string(&json, "benchmark", "persistence");
    bench_json_write_double(&json, "chunk_serialize_ms", results.chunk_serialize_ms);
    bench_json_write_double(&json, "chunk_deserialize_ms", results.chunk_deserialize_ms);
    bench_json_write_int(&json, "memory_mb", (int)results.memory_mb);
    bench_json_end(&json);
    
    const char* headers[] = {"benchmark", "chunk_serialize_ms", "chunk_deserialize_ms", "memory_mb"};
    bench_csv_start(&csv, "bench_persistence.csv");
    bench_csv_write_header(&csv, headers, 4);
    bench_csv_write_row_start(&csv);
    bench_csv_write_string(&csv, "persistence");
    bench_csv_write_double(&csv, results.chunk_serialize_ms);
    bench_csv_write_double(&csv, results.chunk_deserialize_ms);
    bench_csv_write_int(&csv, (int)results.memory_mb);
    bench_csv_write_row_end(&csv);
    bench_csv_end(&csv);
    
    printf("Results written to bench_persistence.json/csv\n");
    return 0;
}
