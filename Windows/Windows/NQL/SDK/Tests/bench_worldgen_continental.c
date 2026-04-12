/**
 * bench_worldgen_continental.c - Continental-scale world generation benchmark
 */
#include "bench_common.h"
#include "../Core/World/Worldgen/Internal/sdk_worldgen_internal.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    double plate_sites_32x32_ms;
    double plate_sites_64x64_ms;
    double plate_sites_128x128_ms;
    uint64_t memory_mb;
} ContinentalBenchResults;

static void bench_plate_site_generation(ContinentalBenchResults* results)
{
    BenchTimer timer;
    bench_timer_init(&timer);
    uint32_t seed = 12345;
    
    bench_timer_start(&timer);
    for (int gz = 0; gz < 32; gz++) {
        for (int gx = 0; gx < 32; gx++) {
            sdk_worldgen_hash2d(gx, gz, seed);
        }
    }
    results->plate_sites_32x32_ms = bench_timer_end_ms(&timer);
    
    bench_timer_start(&timer);
    for (int gz = 0; gz < 64; gz++) {
        for (int gx = 0; gx < 64; gx++) {
            sdk_worldgen_hash2d(gx, gz, seed);
        }
    }
    results->plate_sites_64x64_ms = bench_timer_end_ms(&timer);
    
    bench_timer_start(&timer);
    for (int gz = 0; gz < 128; gz++) {
        for (int gx = 0; gx < 128; gx++) {
            sdk_worldgen_hash2d(gx, gz, seed);
        }
    }
    results->plate_sites_128x128_ms = bench_timer_end_ms(&timer);
}

int main(void)
{
    ContinentalBenchResults results = {0};
    BenchJsonWriter json;
    BenchCsvWriter csv;
    
    printf("Running continental worldgen benchmarks...\n");
    
    bench_plate_site_generation(&results);
    results.memory_mb = bench_get_memory_usage_mb();
    
    printf("  Plate sites 32x32:  %.3f ms\n", results.plate_sites_32x32_ms);
    printf("  Plate sites 64x64:  %.3f ms\n", results.plate_sites_64x64_ms);
    printf("  Plate sites 128x128: %.3f ms\n", results.plate_sites_128x128_ms);
    printf("  Memory usage: %llu MB\n", results.memory_mb);
    
    bench_json_start(&json, "bench_worldgen_continental.json");
    bench_json_write_string(&json, "benchmark", "worldgen_continental");
    bench_json_write_double(&json, "plate_sites_32x32_ms", results.plate_sites_32x32_ms);
    bench_json_write_double(&json, "plate_sites_64x64_ms", results.plate_sites_64x64_ms);
    bench_json_write_double(&json, "plate_sites_128x128_ms", results.plate_sites_128x128_ms);
    bench_json_write_int(&json, "memory_mb", (int)results.memory_mb);
    bench_json_end(&json);
    
    const char* headers[] = {"benchmark", "plate_sites_32x32_ms", "plate_sites_64x64_ms", 
                              "plate_sites_128x128_ms", "memory_mb"};
    bench_csv_start(&csv, "bench_worldgen_continental.csv");
    bench_csv_write_header(&csv, headers, 5);
    bench_csv_write_row_start(&csv);
    bench_csv_write_string(&csv, "worldgen_continental");
    bench_csv_write_double(&csv, results.plate_sites_32x32_ms);
    bench_csv_write_double(&csv, results.plate_sites_64x64_ms);
    bench_csv_write_double(&csv, results.plate_sites_128x128_ms);
    bench_csv_write_int(&csv, (int)results.memory_mb);
    bench_csv_write_row_end(&csv);
    bench_csv_end(&csv);
    
    printf("Results written to bench_worldgen_continental.json/csv\n");
    return 0;
}
