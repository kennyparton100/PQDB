/**
 * bench_common.h - Shared benchmark utilities
 */
#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    LARGE_INTEGER freq;
    LARGE_INTEGER start;
    LARGE_INTEGER end;
} BenchTimer;

typedef struct {
    double* samples;
    int count;
    int capacity;
} BenchSamples;

void bench_timer_init(BenchTimer* timer);
void bench_timer_start(BenchTimer* timer);
double bench_timer_end_ms(BenchTimer* timer);

void bench_samples_init(BenchSamples* samples, int capacity);
void bench_samples_add(BenchSamples* samples, double value);
double bench_samples_mean(const BenchSamples* samples);
double bench_samples_median(BenchSamples* samples);
double bench_samples_stddev(const BenchSamples* samples);
void bench_samples_free(BenchSamples* samples);

uint64_t bench_get_memory_usage_mb(void);

typedef struct {
    FILE* file;
    int first_entry;
} BenchJsonWriter;

void bench_json_start(BenchJsonWriter* writer, const char* filepath);
void bench_json_write_string(BenchJsonWriter* writer, const char* key, const char* value);
void bench_json_write_double(BenchJsonWriter* writer, const char* key, double value);
void bench_json_write_int(BenchJsonWriter* writer, const char* key, int value);
void bench_json_start_object(BenchJsonWriter* writer, const char* key);
void bench_json_end_object(BenchJsonWriter* writer);
void bench_json_end(BenchJsonWriter* writer);

typedef struct {
    FILE* file;
} BenchCsvWriter;

void bench_csv_start(BenchCsvWriter* writer, const char* filepath);
void bench_csv_write_header(BenchCsvWriter* writer, const char** columns, int count);
void bench_csv_write_row_start(BenchCsvWriter* writer);
void bench_csv_write_string(BenchCsvWriter* writer, const char* value);
void bench_csv_write_double(BenchCsvWriter* writer, double value);
void bench_csv_write_int(BenchCsvWriter* writer, int value);
void bench_csv_write_row_end(BenchCsvWriter* writer);
void bench_csv_end(BenchCsvWriter* writer);

#ifdef __cplusplus
}
#endif

#endif /* BENCH_COMMON_H */
