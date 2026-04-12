/**
 * master_benchmark.c - Master benchmark runner
 * Executes all individual benchmarks and generates consolidated report
 */
#include "bench_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const char* name;
    const char* exe_name;
    int exit_code;
    double execution_time_ms;
} BenchmarkEntry;

static BenchmarkEntry g_benchmarks[] = {
    {"Continental Worldgen", "bench_worldgen_continental.exe", 0, 0.0},
    {"Chunk Worldgen", "bench_worldgen_chunk.exe", 0, 0.0},
    {"Mesh Builder", "bench_mesh_builder.exe", 0, 0.0},
    {"Chunk Streaming", "bench_chunk_streaming.exe", 0, 0.0},
    {"Simulation", "bench_simulation.exe", 0, 0.0},
    {"Persistence", "bench_persistence.exe", 0, 0.0},
    {"Settlement", "bench_settlement.exe", 0, 0.0},
};

static const int g_benchmark_count = sizeof(g_benchmarks) / sizeof(g_benchmarks[0]);

static void get_timestamp(char* buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_s(&tm_info, &now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", &tm_info);
}

static int run_benchmark(BenchmarkEntry* bench)
{
    BenchTimer timer;
    char cmd[256];
    
    bench_timer_init(&timer);
    snprintf(cmd, sizeof(cmd), "%s > nul 2>&1", bench->exe_name);
    
    printf("  Running %s...\n", bench->name);
    
    bench_timer_start(&timer);
    bench->exit_code = system(cmd);
    bench->execution_time_ms = bench_timer_end_ms(&timer);
    
    if (bench->exit_code != 0) {
        printf("    FAILED (exit code: %d)\n", bench->exit_code);
        return 0;
    } else {
        printf("    OK (%.3f ms)\n", bench->execution_time_ms);
        return 1;
    }
}

static void write_consolidated_json(void)
{
    BenchJsonWriter json;
    char timestamp[64];
    
    get_timestamp(timestamp, sizeof(timestamp));
    
    bench_json_start(&json, "benchmark_results_master.json");
    bench_json_write_string(&json, "timestamp", timestamp);
    bench_json_write_string(&json, "suite", "SDK Master Benchmark");
    bench_json_write_int(&json, "total_benchmarks", g_benchmark_count);
    
    for (int i = 0; i < g_benchmark_count; i++) {
        bench_json_start_object(&json, g_benchmarks[i].name);
        bench_json_write_int(&json, "exit_code", g_benchmarks[i].exit_code);
        bench_json_write_double(&json, "execution_time_ms", g_benchmarks[i].execution_time_ms);
        bench_json_end_object(&json);
    }
    
    bench_json_end(&json);
}

static void write_consolidated_csv(void)
{
    BenchCsvWriter csv;
    char timestamp[64];
    
    get_timestamp(timestamp, sizeof(timestamp));
    
    const char* headers[] = {"timestamp", "benchmark", "exit_code", "execution_time_ms"};
    bench_csv_start(&csv, "benchmark_results_master.csv");
    bench_csv_write_header(&csv, headers, 4);
    
    for (int i = 0; i < g_benchmark_count; i++) {
        bench_csv_write_row_start(&csv);
        bench_csv_write_string(&csv, timestamp);
        bench_csv_write_string(&csv, g_benchmarks[i].name);
        bench_csv_write_int(&csv, g_benchmarks[i].exit_code);
        bench_csv_write_double(&csv, g_benchmarks[i].execution_time_ms);
        bench_csv_write_row_end(&csv);
    }
    
    bench_csv_end(&csv);
}

int main(void)
{
    int passed = 0;
    int failed = 0;
    double total_time_ms = 0.0;
    
    printf("========================================\n");
    printf("SDK Master Benchmark Suite\n");
    printf("========================================\n\n");
    
    for (int i = 0; i < g_benchmark_count; i++) {
        if (run_benchmark(&g_benchmarks[i])) {
            passed++;
        } else {
            failed++;
        }
        total_time_ms += g_benchmarks[i].execution_time_ms;
    }
    
    printf("\n========================================\n");
    printf("Results Summary\n");
    printf("========================================\n");
    printf("Total benchmarks: %d\n", g_benchmark_count);
    printf("Passed:          %d\n", passed);
    printf("Failed:          %d\n", failed);
    printf("Total time:      %.3f ms\n", total_time_ms);
    printf("Memory usage:    %llu MB\n", bench_get_memory_usage_mb());
    printf("\n");
    
    write_consolidated_json();
    write_consolidated_csv();
    
    printf("Consolidated results written to:\n");
    printf("  - benchmark_results_master.json\n");
    printf("  - benchmark_results_master.csv\n");
    
    return (failed > 0) ? 1 : 0;
}
