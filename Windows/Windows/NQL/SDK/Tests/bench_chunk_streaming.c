/**
 * bench_chunk_streaming.c - Chunk manager and streaming benchmark
 */
#include "bench_common.h"
#include "../Core/World/Chunks/ChunkManager/sdk_chunk_manager.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    double alloc_dealloc_ms;
    double hash_lookup_ms;
    double residency_calc_ms;
    double topology_rebuild_ms;
    uint64_t memory_mb;
} ChunkStreamingBenchResults;

static void bench_allocation(ChunkStreamingBenchResults* results)
{
    BenchTimer timer;
    SdkChunkManager cm;
    int iterations = 100;
    
    sdk_chunk_manager_init(&cm);
    bench_timer_init(&timer);
    
    bench_timer_start(&timer);
    for (int i = 0; i < iterations; i++) {
        int cx = i % 10;
        int cz = i / 10;
        SdkChunkResidentSlot* slot = sdk_chunk_manager_reserve_slot(&cm, cx, cz, SDK_CHUNK_ROLE_PRIMARY);
        if (slot) {
            sdk_chunk_manager_release_slot(&cm, slot);
        }
    }
    results->alloc_dealloc_ms = bench_timer_end_ms(&timer) / iterations;
    
    sdk_chunk_manager_shutdown(&cm);
}

static void bench_lookups(ChunkStreamingBenchResults* results)
{
    BenchTimer timer;
    SdkChunkManager cm;
    int iterations = 10000;
    
    sdk_chunk_manager_init(&cm);
    bench_timer_init(&timer);
    
    for (int i = 0; i < 50; i++) {
        sdk_chunk_manager_reserve_slot(&cm, i % 10, i / 10, SDK_CHUNK_ROLE_PRIMARY);
    }
    
    bench_timer_start(&timer);
    for (int i = 0; i < iterations; i++) {
        int cx = i % 10;
        int cz = i / 10;
        sdk_chunk_manager_find_slot(&cm, cx, cz);
    }
    results->hash_lookup_ms = bench_timer_end_ms(&timer) / iterations;
    
    sdk_chunk_manager_shutdown(&cm);
}

static void bench_residency(ChunkStreamingBenchResults* results)
{
    BenchTimer timer;
    SdkChunkManager cm;
    
    sdk_chunk_manager_init(&cm);
    bench_timer_init(&timer);
    
    bench_timer_start(&timer);
    for (int i = 0; i < 100; i++) {
        sdk_chunk_manager_update(&cm, i, i);
    }
    results->residency_calc_ms = bench_timer_end_ms(&timer) / 100;
    
    sdk_chunk_manager_shutdown(&cm);
}

static void bench_topology(ChunkStreamingBenchResults* results)
{
    BenchTimer timer;
    SdkChunkManager cm;
    
    sdk_chunk_manager_init(&cm);
    bench_timer_init(&timer);
    
    for (int i = 0; i < 100; i++) {
        sdk_chunk_manager_reserve_slot(&cm, i % 10, i / 10, SDK_CHUNK_ROLE_PRIMARY);
    }
    
    bench_timer_start(&timer);
    sdk_chunk_manager_rebuild_lookup(&cm);
    results->topology_rebuild_ms = bench_timer_end_ms(&timer);
    
    sdk_chunk_manager_shutdown(&cm);
}

int main(void)
{
    ChunkStreamingBenchResults results = {0};
    BenchJsonWriter json;
    BenchCsvWriter csv;
    
    printf("Running chunk streaming benchmarks...\n");
    
    bench_allocation(&results);
    bench_lookups(&results);
    bench_residency(&results);
    bench_topology(&results);
    results.memory_mb = bench_get_memory_usage_mb();
    
    printf("  Alloc/dealloc avg: %.6f ms\n", results.alloc_dealloc_ms);
    printf("  Hash lookup avg:   %.6f ms\n", results.hash_lookup_ms);
    printf("  Residency calc avg:%.6f ms\n", results.residency_calc_ms);
    printf("  Topology rebuild:  %.3f ms\n", results.topology_rebuild_ms);
    printf("  Memory usage: %llu MB\n", results.memory_mb);
    
    bench_json_start(&json, "bench_chunk_streaming.json");
    bench_json_write_string(&json, "benchmark", "chunk_streaming");
    bench_json_write_double(&json, "alloc_dealloc_ms", results.alloc_dealloc_ms);
    bench_json_write_double(&json, "hash_lookup_ms", results.hash_lookup_ms);
    bench_json_write_double(&json, "residency_calc_ms", results.residency_calc_ms);
    bench_json_write_double(&json, "topology_rebuild_ms", results.topology_rebuild_ms);
    bench_json_write_int(&json, "memory_mb", (int)results.memory_mb);
    bench_json_end(&json);
    
    const char* headers[] = {"benchmark", "alloc_dealloc_ms", "hash_lookup_ms", 
                              "residency_calc_ms", "topology_rebuild_ms", "memory_mb"};
    bench_csv_start(&csv, "bench_chunk_streaming.csv");
    bench_csv_write_header(&csv, headers, 6);
    bench_csv_write_row_start(&csv);
    bench_csv_write_string(&csv, "chunk_streaming");
    bench_csv_write_double(&csv, results.alloc_dealloc_ms);
    bench_csv_write_double(&csv, results.hash_lookup_ms);
    bench_csv_write_double(&csv, results.residency_calc_ms);
    bench_csv_write_double(&csv, results.topology_rebuild_ms);
    bench_csv_write_int(&csv, (int)results.memory_mb);
    bench_csv_write_row_end(&csv);
    bench_csv_end(&csv);
    
    printf("Results written to bench_chunk_streaming.json/csv\n");
    return 0;
}
