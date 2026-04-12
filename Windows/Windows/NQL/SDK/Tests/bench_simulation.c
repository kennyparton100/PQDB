/**
 * bench_simulation.c - Physics and simulation benchmark
 */
#include "bench_common.h"
#include "../Core/World/Simulation/sdk_simulation.h"
#include "../Core/Entities/sdk_entity.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    double entity_step_ms;
    double entity_list_allocation_ms;
    uint64_t memory_mb;
} SimulationBenchResults;

static void bench_entity_simulation(SimulationBenchResults* results)
{
    BenchTimer timer;
    SdkEntityList entities;
    int iterations = 1000;
    
    bench_timer_init(&timer);
    sdk_entity_list_init(&entities);
    
    for (int i = 0; i < 100; i++) {
        SdkEntity* entity = sdk_entity_list_spawn(&entities, SDK_ENTITY_ITEM_DROP);
        if (entity) {
            entity->x = (float)i;
            entity->y = 64.0f;
            entity->z = (float)i;
            entity->vx = 0.1f;
            entity->vy = 0.0f;
            entity->vz = 0.1f;
        }
    }
    
    bench_timer_start(&timer);
    for (int i = 0; i < iterations; i++) {
        sdk_entity_list_step(&entities, 0.016f);
    }
    results->entity_step_ms = bench_timer_end_ms(&timer) / iterations;
    
    sdk_entity_list_shutdown(&entities);
}

static void bench_entity_allocation(SimulationBenchResults* results)
{
    BenchTimer timer;
    SdkEntityList entities;
    int iterations = 1000;
    
    bench_timer_init(&timer);
    
    bench_timer_start(&timer);
    for (int i = 0; i < iterations; i++) {
        sdk_entity_list_init(&entities);
        for (int j = 0; j < 10; j++) {
            sdk_entity_list_spawn(&entities, SDK_ENTITY_ITEM_DROP);
        }
        sdk_entity_list_shutdown(&entities);
    }
    results->entity_list_allocation_ms = bench_timer_end_ms(&timer) / iterations;
}

int main(void)
{
    SimulationBenchResults results = {0};
    BenchJsonWriter json;
    BenchCsvWriter csv;
    
    printf("Running simulation benchmarks...\n");
    
    bench_entity_simulation(&results);
    bench_entity_allocation(&results);
    results.memory_mb = bench_get_memory_usage_mb();
    
    printf("  Entity step avg:   %.6f ms\n", results.entity_step_ms);
    printf("  Entity alloc avg:  %.6f ms\n", results.entity_list_allocation_ms);
    printf("  Memory usage: %llu MB\n", results.memory_mb);
    
    bench_json_start(&json, "bench_simulation.json");
    bench_json_write_string(&json, "benchmark", "simulation");
    bench_json_write_double(&json, "entity_step_ms", results.entity_step_ms);
    bench_json_write_double(&json, "entity_list_allocation_ms", results.entity_list_allocation_ms);
    bench_json_write_int(&json, "memory_mb", (int)results.memory_mb);
    bench_json_end(&json);
    
    const char* headers[] = {"benchmark", "entity_step_ms", "entity_list_allocation_ms", "memory_mb"};
    bench_csv_start(&csv, "bench_simulation.csv");
    bench_csv_write_header(&csv, headers, 4);
    bench_csv_write_row_start(&csv);
    bench_csv_write_string(&csv, "simulation");
    bench_csv_write_double(&csv, results.entity_step_ms);
    bench_csv_write_double(&csv, results.entity_list_allocation_ms);
    bench_csv_write_int(&csv, (int)results.memory_mb);
    bench_csv_write_row_end(&csv);
    bench_csv_end(&csv);
    
    printf("Results written to bench_simulation.json/csv\n");
    return 0;
}
