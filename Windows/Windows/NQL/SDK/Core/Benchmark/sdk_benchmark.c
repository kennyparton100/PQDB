/**
 * sdk_benchmark.c - Comprehensive Performance Benchmarking
 * Tests worldgen edge cases, mesh building, and rendering performance
 */
#include "../API/Internal/sdk_api_internal.h"
#include "../World/Worldgen/Internal/sdk_worldgen_internal.h"
#include <stdio.h>
#include <time.h>
#include <math.h>

#define BENCH_WORLDGEN_SAMPLES 30
#define BENCH_MESH_SAMPLES 15
#define BENCH_DETAILED_CSV_ROWS 15
#define BENCH_WORLDGEN_TIMEOUT_MS 3000.0   // 3 seconds - anything longer is a fail
#define BENCH_MESH_TIMEOUT_MS 8000.0       // 8 seconds

static LARGE_INTEGER g_qpc_freq;
static int g_qpc_initialized = 0;

static void init_timing(void) {
    /* Initializes high-resolution timing using QueryPerformanceCounter */
    if (!g_qpc_initialized) {
        QueryPerformanceFrequency(&g_qpc_freq);
        g_qpc_initialized = 1;
    }
}

static double get_time_ms(void) {
    /* Returns current time in milliseconds using QPC */
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return ((double)now.QuadPart * 1000.0) / (double)g_qpc_freq.QuadPart;
}

static uint64_t get_memory_mb(void) {
    /* Returns current working set memory usage in megabytes */
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024 * 1024);
    }
    return 0;
}

typedef struct {
    double min, max, mean, stddev;
    int min_idx, max_idx;
} BenchStats;

static void compute_stats(double* samples, int count, BenchStats* out) {
    /* Computes min, max, mean, and stddev from sample array */
    double sum = 0.0, sum_sq = 0.0;
    out->min = samples[0];
    out->max = samples[0];
    out->min_idx = 0;
    out->max_idx = 0;
    
    for (int i = 0; i < count; i++) {
        sum += samples[i];
        sum_sq += samples[i] * samples[i];
        if (samples[i] < out->min) { out->min = samples[i]; out->min_idx = i; }
        if (samples[i] > out->max) { out->max = samples[i]; out->max_idx = i; }
    }
    
    out->mean = sum / (double)count;
    double variance = (sum_sq / (double)count) - (out->mean * out->mean);
    out->stddev = (variance > 0.0) ? sqrt(variance) : 0.0;
}

void run_performance_benchmarks(void) {
    /* Runs comprehensive performance benchmarks for worldgen and mesh building */
    FILE* json_file = NULL;
    FILE* csv_file = NULL;
    FILE* detail_csv = NULL;
    double start_time;
    uint64_t memory_start, memory_peak, memory_end;
    char timestamp[64];
    char msg[2048];
    time_t now = time(NULL);
    struct tm tm_info;
    SdkWorldGen* wg = NULL;
    
    init_timing();
    localtime_s(&tm_info, &now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_info);
    memory_start = get_memory_mb();
    
    OutputDebugStringA("[BENCHMARK] Starting comprehensive performance benchmarks...\n");
    
    // Initialize temporary worldgen instance for benchmarking
    SdkWorldGen temp_worldgen = {0};
    SdkWorldDesc world_desc = {0};
    wg = &temp_worldgen;
    
    world_desc.seed = (uint32_t)time(NULL);
    world_desc.sea_level = 0;  // Use default
    world_desc.macro_cell_size = 0;  // Use default
    
    sdk_worldgen_init_ex(wg, &world_desc, SDK_WORLDGEN_CACHE_DISK);
    
    OutputDebugStringA("[BENCHMARK] Worldgen initialized for testing\n");
    
    // Allocate sampling arrays
    double* worldgen_times = (double*)malloc(BENCH_WORLDGEN_SAMPLES * sizeof(double));
    double* mesh_times = (double*)malloc(BENCH_MESH_SAMPLES * sizeof(double));
    int* chunk_coords_x = (int*)malloc(BENCH_WORLDGEN_SAMPLES * sizeof(int));
    int* chunk_coords_z = (int*)malloc(BENCH_WORLDGEN_SAMPLES * sizeof(int));
    int* worldgen_timeouts = (int*)calloc(BENCH_WORLDGEN_SAMPLES, sizeof(int));
    int* mesh_timeouts = (int*)calloc(BENCH_MESH_SAMPLES, sizeof(int));
    
    if (!worldgen_times || !mesh_times || !chunk_coords_x || !chunk_coords_z || !worldgen_timeouts || !mesh_timeouts) {
        MessageBoxA(NULL, "Memory allocation failed", "Benchmark Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Test coordinates: spawn to extreme distances (>3s = fail)
    // Note: Reduced duplicates to minimize total slow chunk count
    int coord_patterns[] = {
        0, 0,        // Spawn
        1, 1,
        -1, -1,
        5, 5,
        -5, -5,
        100, 100,    // Far field
        -100, -100,
        500, 0,      // Known slow case (will likely timeout)
        1000, 1000,  // Very far
        5000, 0,     // Extreme (will likely timeout)
        10000, 10000, // Will likely timeout
        16384, 0     // Near world edge (will likely timeout)
    };
    int pattern_count = sizeof(coord_patterns) / (2 * sizeof(int));
    
    // Generate test coordinates
    for (int i = 0; i < BENCH_WORLDGEN_SAMPLES; i++) {
        if (i < pattern_count) {
            chunk_coords_x[i] = coord_patterns[i * 2];
            chunk_coords_z[i] = coord_patterns[i * 2 + 1];
        } else {
            // Random coordinates in various ranges including extremes
            int range_selector = i % 5;
            int range = (range_selector == 0) ? 50 : (range_selector == 1) ? 200 : 
                       (range_selector == 2) ? 1000 : (range_selector == 3) ? 5000 : 10000;
            uint32_t hash_x = sdk_worldgen_hash32((uint32_t)i * 12345u);
            uint32_t hash_z = sdk_worldgen_hash32((uint32_t)i * 54321u);
            chunk_coords_x[i] = (int)((hash_x % (range * 2)) - range);
            chunk_coords_z[i] = (int)((hash_z % (range * 2)) - range);
        }
    }
    
    OutputDebugStringA("[BENCHMARK] Testing worldgen across varied coordinates...\n");
    
    // Worldgen benchmark - varied coordinates
    int worldgen_timeout_count = 0;
    for (int i = 0; i < BENCH_WORLDGEN_SAMPLES; i++) {
        SdkChunk chunk;
        sdk_chunk_init(&chunk, chunk_coords_x[i], chunk_coords_z[i], NULL);
        
        // Log which chunk we're about to test
        char test_msg[128];
        sprintf_s(test_msg, sizeof(test_msg), "[BENCHMARK] Testing chunk [%d,%d]...\n", 
                  chunk_coords_x[i], chunk_coords_z[i]);
        OutputDebugStringA(test_msg);
        
        start_time = get_time_ms();
        
        // Execute and check afterward (can't interrupt mid-generation)
        sdk_worldgen_generate_chunk_ctx(wg, &chunk);
        double elapsed = get_time_ms() - start_time;
        
        if (elapsed > BENCH_WORLDGEN_TIMEOUT_MS) {
            worldgen_times[i] = BENCH_WORLDGEN_TIMEOUT_MS;
            worldgen_timeouts[i] = 1;
            worldgen_timeout_count++;
            
            char warn[256];
            sprintf_s(warn, sizeof(warn), "[BENCHMARK] FAILED: Chunk [%d,%d] took %.1f seconds (>3s threshold)\n",
                      chunk_coords_x[i], chunk_coords_z[i], elapsed / 1000.0);
            OutputDebugStringA(warn);
        } else {
            worldgen_times[i] = elapsed;
            char ok_msg[128];
            sprintf_s(ok_msg, sizeof(ok_msg), "[BENCHMARK] OK: Chunk [%d,%d] completed in %.1f ms\n",
                      chunk_coords_x[i], chunk_coords_z[i], elapsed);
            OutputDebugStringA(ok_msg);
        }
        
        sdk_chunk_free(&chunk);
        
        if ((i + 1) % 5 == 0 || i == 0) {
            char progress[128];
            sprintf_s(progress, sizeof(progress), "[BENCHMARK] Worldgen: %d/%d chunks (%d timeouts)...\n",
                      i + 1, BENCH_WORLDGEN_SAMPLES, worldgen_timeout_count);
            OutputDebugStringA(progress);
        }
    }
    
    OutputDebugStringA("[BENCHMARK] Testing mesh generation with real terrain...\n");
    
    // Mesh benchmark - use generated chunks (skip if worldgen timed out)
    int mesh_timeout_count = 0;
    int mesh_skipped_count = 0;
    for (int i = 0; i < BENCH_MESH_SAMPLES; i++) {
        SdkChunk chunk;
        SdkMeshBuffer mesh_buf;
        int idx = i * (BENCH_WORLDGEN_SAMPLES / BENCH_MESH_SAMPLES);
        
        // Skip mesh test if worldgen failed for this coordinate
        if (worldgen_timeouts[idx]) {
            mesh_times[i] = 0.0;
            mesh_skipped_count++;
            char skip_msg[128];
            sprintf_s(skip_msg, sizeof(skip_msg), "[BENCHMARK] Skipping mesh for failed chunk [%d,%d]\n",
                      chunk_coords_x[idx], chunk_coords_z[idx]);
            OutputDebugStringA(skip_msg);
            continue;
        }
        
        sdk_chunk_init(&chunk, chunk_coords_x[idx], chunk_coords_z[idx], NULL);
        sdk_worldgen_generate_chunk_ctx(wg, &chunk);
        sdk_mesh_buffer_init(&mesh_buf, 65536);
        
        start_time = get_time_ms();
        sdk_mesh_build_chunk(&chunk, NULL, &mesh_buf);
        double elapsed = get_time_ms() - start_time;
        
        if (elapsed > BENCH_MESH_TIMEOUT_MS) {
            mesh_times[i] = BENCH_MESH_TIMEOUT_MS;
            mesh_timeouts[i] = 1;
            mesh_timeout_count++;
            
            char warn[256];
            sprintf_s(warn, sizeof(warn), "[BENCHMARK] WARNING: Mesh build chunk [%d,%d] took %.1f seconds (timeout)\n",
                      chunk_coords_x[idx], chunk_coords_z[idx], elapsed / 1000.0);
            OutputDebugStringA(warn);
        } else {
            mesh_times[i] = elapsed;
        }
        
        sdk_mesh_buffer_free(&mesh_buf);
        sdk_chunk_free(&chunk);
        
        if ((i + 1) % 5 == 0) {
            char progress[128];
            sprintf_s(progress, sizeof(progress), "[BENCHMARK] Mesh: %d/%d chunks (%d timeouts)...\n",
                      i + 1, BENCH_MESH_SAMPLES, mesh_timeout_count);
            OutputDebugStringA(progress);
        }
    }
    
    memory_peak = get_memory_mb();
    
    // Compute statistics
    BenchStats worldgen_stats, mesh_stats;
    compute_stats(worldgen_times, BENCH_WORLDGEN_SAMPLES, &worldgen_stats);
    compute_stats(mesh_times, BENCH_MESH_SAMPLES, &mesh_stats);
    
    memory_end = get_memory_mb();
    
    // Write comprehensive JSON results
    fopen_s(&json_file, "sdk_benchmark_results.json", "w");
    if (json_file) {
        fprintf(json_file, "{\n");
        fprintf(json_file, "  \"timestamp\": \"%s\",\n", timestamp);
        fprintf(json_file, "  \"worldgen\": {\n");
        fprintf(json_file, "    \"samples\": %d,\n", BENCH_WORLDGEN_SAMPLES);
        fprintf(json_file, "    \"mean_ms\": %.6f,\n", worldgen_stats.mean);
        fprintf(json_file, "    \"min_ms\": %.6f,\n", worldgen_stats.min);
        fprintf(json_file, "    \"max_ms\": %.6f,\n", worldgen_stats.max);
        fprintf(json_file, "    \"stddev_ms\": %.6f,\n", worldgen_stats.stddev);
        fprintf(json_file, "    \"worst_case_coords\": [%d, %d],\n", 
                chunk_coords_x[worldgen_stats.max_idx], chunk_coords_z[worldgen_stats.max_idx]);
        fprintf(json_file, "    \"timeout_count\": %d,\n", worldgen_timeout_count);
        fprintf(json_file, "    \"estimated_100sqkm_minutes\": %.2f\n", 
                (worldgen_stats.mean * 156250.0) / 60000.0);
        fprintf(json_file, "  },\n");
        fprintf(json_file, "  \"mesh_building\": {\n");
        fprintf(json_file, "    \"samples\": %d,\n", BENCH_MESH_SAMPLES);
        fprintf(json_file, "    \"mean_ms\": %.6f,\n", mesh_stats.mean);
        fprintf(json_file, "    \"min_ms\": %.6f,\n", mesh_stats.min);
        fprintf(json_file, "    \"max_ms\": %.6f,\n", mesh_stats.max);
        fprintf(json_file, "    \"stddev_ms\": %.6f,\n", mesh_stats.stddev);
        fprintf(json_file, "    \"timeout_count\": %d\n", mesh_timeout_count);
        fprintf(json_file, "  },\n");
        fprintf(json_file, "  \"memory\": {\n");
        fprintf(json_file, "    \"start_mb\": %llu,\n", memory_start);
        fprintf(json_file, "    \"peak_mb\": %llu,\n", memory_peak);
        fprintf(json_file, "    \"end_mb\": %llu\n", memory_end);
        fprintf(json_file, "  }\n");
        fprintf(json_file, "}\n");
        fclose(json_file);
        OutputDebugStringA("[BENCHMARK] Written sdk_benchmark_results.json\n");
    }
    
    // Write summary CSV
    fopen_s(&csv_file, "sdk_benchmark_results.csv", "w");
    if (csv_file) {
        fprintf(csv_file, "timestamp,worldgen_samples,worldgen_mean_ms,worldgen_min_ms,worldgen_max_ms,worldgen_stddev_ms,worldgen_timeouts,");
        fprintf(csv_file, "mesh_samples,mesh_mean_ms,mesh_min_ms,mesh_max_ms,mesh_stddev_ms,mesh_timeouts,");
        fprintf(csv_file, "worst_chunk_x,worst_chunk_z,est_100sqkm_min,memory_peak_mb\n");
        fprintf(csv_file, "%s,%d,%.6f,%.6f,%.6f,%.6f,%d,%d,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%.2f,%llu\n",
                timestamp, BENCH_WORLDGEN_SAMPLES, worldgen_stats.mean, worldgen_stats.min, 
                worldgen_stats.max, worldgen_stats.stddev, worldgen_timeout_count,
                BENCH_MESH_SAMPLES, mesh_stats.mean,
                mesh_stats.min, mesh_stats.max, mesh_stats.stddev, mesh_timeout_count,
                chunk_coords_x[worldgen_stats.max_idx], chunk_coords_z[worldgen_stats.max_idx],
                (worldgen_stats.mean * 156250.0) / 60000.0, memory_peak);
        fclose(csv_file);
        OutputDebugStringA("[BENCHMARK] Written sdk_benchmark_results.csv\n");
    }
    
    // Write detailed per-chunk CSV for worst cases
    fopen_s(&detail_csv, "sdk_benchmark_details.csv", "w");
    if (detail_csv) {
        fprintf(detail_csv, "chunk_x,chunk_z,worldgen_ms,mesh_ms\n");
        
        // Sort and output worst worldgen cases
        for (int i = 0; i < BENCH_WORLDGEN_SAMPLES && i < BENCH_DETAILED_CSV_ROWS; i++) {
            int worst_idx = 0;
            double worst_time = -1.0;
            for (int j = 0; j < BENCH_WORLDGEN_SAMPLES; j++) {
                if (worldgen_times[j] > worst_time) {
                    worst_time = worldgen_times[j];
                    worst_idx = j;
                }
            }
            
            int mesh_idx = -1;
            for (int j = 0; j < BENCH_MESH_SAMPLES; j++) {
                int wg_idx = j * (BENCH_WORLDGEN_SAMPLES / BENCH_MESH_SAMPLES);
                if (wg_idx == worst_idx) {
                    mesh_idx = j;
                    break;
                }
            }
            
            fprintf(detail_csv, "%d,%d,%.6f,%.6f\n",
                    chunk_coords_x[worst_idx], chunk_coords_z[worst_idx],
                    worldgen_times[worst_idx], 
                    (mesh_idx >= 0) ? mesh_times[mesh_idx] : 0.0);
            
            worldgen_times[worst_idx] = -1.0;
        }
        
        fclose(detail_csv);
        OutputDebugStringA("[BENCHMARK] Written sdk_benchmark_details.csv\n");
    }
    
    // Show comprehensive results
    sprintf_s(msg, sizeof(msg),
              "=== COMPREHENSIVE BENCHMARK RESULTS ===\n\n"
              "WORLDGEN (%d chunks tested):\n"
              "  Mean: %.3f ms  |  Min: %.3f ms  |  Max: %.3f ms\n"
              "  StdDev: %.3f ms  |  Variance: %.1fx\n"
              "  Worst case: chunk [%d, %d]\n"
              "  Est. 100 km\u00b2 generation: %.1f minutes\n\n"
              "MESH BUILDING (%d chunks tested):\n"
              "  Mean: %.3f ms  |  Min: %.3f ms  |  Max: %.3f ms\n"
              "  StdDev: %.3f ms  |  Variance: %.1fx\n\n"
              "MEMORY:\n"
              "  Peak: %llu MB  |  Delta: %lld MB\n\n"
              "EDGE CASES DETECTED:\n"
              "  Worldgen outliers: %d chunks > 2x mean\n"
              "  Worldgen FAILURES: %d chunks > 3 seconds\n"
              "  Mesh outliers: %d chunks > 2x mean\n"
              "  Mesh timeouts: %d chunks > 8 seconds\n\n"
              "Files written:\n"
              "  - sdk_benchmark_results.json\n"
              "  - sdk_benchmark_results.csv\n"
              "  - sdk_benchmark_details.csv",
              BENCH_WORLDGEN_SAMPLES,
              worldgen_stats.mean, worldgen_stats.min, worldgen_stats.max, worldgen_stats.stddev,
              worldgen_stats.max / (worldgen_stats.min > 0.0 ? worldgen_stats.min : 1.0),
              chunk_coords_x[worldgen_stats.max_idx], chunk_coords_z[worldgen_stats.max_idx],
              (worldgen_stats.mean * 156250.0) / 60000.0,
              BENCH_MESH_SAMPLES,
              mesh_stats.mean, mesh_stats.min, mesh_stats.max, mesh_stats.stddev,
              mesh_stats.max / (mesh_stats.min > 0.0 ? mesh_stats.min : 1.0),
              memory_peak, (long long)(memory_end - memory_start),
              0, worldgen_timeout_count, 0, mesh_timeout_count);
    
    // Count outliers
    int wg_outliers = 0, mesh_outliers = 0;
    for (int i = 0; i < BENCH_WORLDGEN_SAMPLES; i++) {
        if (worldgen_times[i] > worldgen_stats.mean * 2.0) wg_outliers++;
    }
    for (int i = 0; i < BENCH_MESH_SAMPLES; i++) {
        if (mesh_times[i] > mesh_stats.mean * 2.0) mesh_outliers++;
    }
    
    // Update message with outlier counts
    char* outlier_pos = strstr(msg, "Worldgen outliers:");
    if (outlier_pos) {
        sprintf_s(outlier_pos, 200, "Worldgen outliers: %d chunks > 2x mean\n  Worldgen FAILURES: %d chunks > 3 seconds\n  Mesh outliers: %d chunks > 2x mean\n  Mesh timeouts: %d chunks > 8 seconds",
                  wg_outliers, worldgen_timeout_count, mesh_outliers, mesh_timeout_count);
    }
    
    MessageBoxA(NULL, msg, "Comprehensive Benchmark Results", MB_OK | MB_ICONINFORMATION);
    
    // Cleanup
    sdk_worldgen_shutdown(wg);
    free(worldgen_times);
    free(mesh_times);
    free(chunk_coords_x);
    free(chunk_coords_z);
    free(worldgen_timeouts);
    free(mesh_timeouts);
    
    OutputDebugStringA("[BENCHMARK] Comprehensive benchmarks complete\n");
}
