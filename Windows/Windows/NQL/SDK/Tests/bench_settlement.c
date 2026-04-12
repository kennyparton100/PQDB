/**
 * bench_settlement.c - Settlement generation/layout benchmark
 */
#include "bench_common.h"
#include "../Core/World/Worldgen/sdk_worldgen.h"
#include "../Core/World/Settlements/sdk_settlement.h"
#include "../Core/World/Superchunks/Geometry/sdk_superchunk_geometry.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    double superchunk_generate_ms;
    double layout_generate_ms;
    int settlement_count;
    int wall_violations;
    uint64_t memory_mb;
} SettlementBenchResults;

static int bench_floor_div_i(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

static int bench_floor_mod_i(int value, int denom)
{
    int div = bench_floor_div_i(value, denom);
    return value - div * denom;
}

static int settlement_fits_inside_wall_bounds(const SettlementMetadata* settlement)
{
    int local_x;
    int local_z;
    int clearance;
    int max_local;

    if (!settlement) return 1;

    clearance = SDK_SUPERCHUNK_WALL_THICKNESS_BLOCKS + (int)settlement->radius + 16;
    if (clearance > SDK_SUPERCHUNK_BLOCK_SPAN / 2 - 1) {
        clearance = SDK_SUPERCHUNK_BLOCK_SPAN / 2 - 1;
    }

    local_x = bench_floor_mod_i(settlement->center_wx, SDK_SUPERCHUNK_BLOCK_SPAN);
    local_z = bench_floor_mod_i(settlement->center_wz, SDK_SUPERCHUNK_BLOCK_SPAN);
    max_local = SDK_SUPERCHUNK_BLOCK_SPAN - clearance - 1;

    return local_x >= clearance && local_x <= max_local &&
           local_z >= clearance && local_z <= max_local;
}

static void bench_settlement_operations(SettlementBenchResults* results)
{
    BenchTimer timer;
    SdkWorldGen wg;
    SdkWorldDesc world_desc;
    SuperchunkSettlementData* data;
    SettlementLayout* layout;
    int i;
    int iterations = 12;
    double total_layout_ms = 0.0;
    int total_layout_count = 0;

    memset(&wg, 0, sizeof(wg));
    memset(&world_desc, 0, sizeof(world_desc));
    world_desc.seed = 12345u;
    world_desc.sea_level = 192;
    world_desc.macro_cell_size = SDK_WORLDGEN_MACRO_CELL_BLOCKS;

    sdk_worldgen_init_ex(&wg, &world_desc, SDK_WORLDGEN_CACHE_NONE);
    if (!wg.impl) {
        memset(results, 0, sizeof(*results));
        return;
    }

    bench_timer_init(&timer);
    bench_timer_start(&timer);
    for (i = 0; i < iterations; ++i) {
        int cx = i * SDK_SUPERCHUNK_CHUNK_SPAN;
        data = sdk_settlement_get_or_create_data(&wg, cx, 0);
        if (i == 0 && data) {
            results->settlement_count = (int)data->settlement_count;
        }
    }
    results->superchunk_generate_ms = bench_timer_end_ms(&timer) / iterations;

    data = sdk_settlement_get_or_create_data(&wg, 0, 0);
    if (data) {
        for (i = 0; i < (int)data->settlement_count; ++i) {
            if (!settlement_fits_inside_wall_bounds(&data->settlements[i])) {
                results->wall_violations++;
            }
            bench_timer_start(&timer);
            layout = sdk_settlement_generate_layout(&wg, &data->settlements[i]);
            total_layout_ms += bench_timer_end_ms(&timer);
            total_layout_count++;
            sdk_settlement_free_layout(layout);
        }
    }

    if (total_layout_count > 0) {
        results->layout_generate_ms = total_layout_ms / (double)total_layout_count;
    }

    sdk_worldgen_shutdown(&wg);
}

int main(void)
{
    SettlementBenchResults results;
    BenchJsonWriter json;
    BenchCsvWriter csv;
    const char* headers[] = {"benchmark", "superchunk_generate_ms", "layout_generate_ms", "settlement_count", "wall_violations", "memory_mb"};

    memset(&results, 0, sizeof(results));

    printf("Running settlement benchmarks...\n");

    bench_settlement_operations(&results);
    results.memory_mb = bench_get_memory_usage_mb();

    printf("  Superchunk generate avg:   %.6f ms\n", results.superchunk_generate_ms);
    printf("  Layout generate avg:       %.6f ms\n", results.layout_generate_ms);
    printf("  Settlements in sample SC:  %d\n", results.settlement_count);
    printf("  Wall violations:           %d\n", results.wall_violations);
    printf("  Memory usage: %llu MB\n", results.memory_mb);

    bench_json_start(&json, "bench_settlement.json");
    bench_json_write_string(&json, "benchmark", "settlement");
    bench_json_write_double(&json, "superchunk_generate_ms", results.superchunk_generate_ms);
    bench_json_write_double(&json, "layout_generate_ms", results.layout_generate_ms);
    bench_json_write_int(&json, "settlement_count", results.settlement_count);
    bench_json_write_int(&json, "wall_violations", results.wall_violations);
    bench_json_write_int(&json, "memory_mb", (int)results.memory_mb);
    bench_json_end(&json);

    bench_csv_start(&csv, "bench_settlement.csv");
    bench_csv_write_header(&csv, headers, 6);
    bench_csv_write_row_start(&csv);
    bench_csv_write_string(&csv, "settlement");
    bench_csv_write_double(&csv, results.superchunk_generate_ms);
    bench_csv_write_double(&csv, results.layout_generate_ms);
    bench_csv_write_int(&csv, results.settlement_count);
    bench_csv_write_int(&csv, results.wall_violations);
    bench_csv_write_int(&csv, (int)results.memory_mb);
    bench_csv_write_row_end(&csv);
    bench_csv_end(&csv);

    printf("Results written to bench_settlement.json/csv\n");
    return 0;
}
