/**
 * bench_common.c - Shared benchmark utilities implementation
 */
#include "bench_common.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <psapi.h>

void bench_timer_init(BenchTimer* timer)
{
    QueryPerformanceFrequency(&timer->freq);
    timer->start.QuadPart = 0;
    timer->end.QuadPart = 0;
}

void bench_timer_start(BenchTimer* timer)
{
    QueryPerformanceCounter(&timer->start);
}

double bench_timer_end_ms(BenchTimer* timer)
{
    QueryPerformanceCounter(&timer->end);
    double elapsed = (double)(timer->end.QuadPart - timer->start.QuadPart);
    return (elapsed * 1000.0) / (double)timer->freq.QuadPart;
}

void bench_samples_init(BenchSamples* samples, int capacity)
{
    samples->samples = (double*)malloc(capacity * sizeof(double));
    samples->count = 0;
    samples->capacity = capacity;
}

void bench_samples_add(BenchSamples* samples, double value)
{
    if (samples->count < samples->capacity) {
        samples->samples[samples->count++] = value;
    }
}

double bench_samples_mean(const BenchSamples* samples)
{
    if (samples->count == 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < samples->count; i++) {
        sum += samples->samples[i];
    }
    return sum / samples->count;
}

static int compare_double(const void* a, const void* b)
{
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

double bench_samples_median(BenchSamples* samples)
{
    if (samples->count == 0) return 0.0;
    qsort(samples->samples, samples->count, sizeof(double), compare_double);
    if (samples->count % 2 == 0) {
        return (samples->samples[samples->count / 2 - 1] + samples->samples[samples->count / 2]) / 2.0;
    } else {
        return samples->samples[samples->count / 2];
    }
}

double bench_samples_stddev(const BenchSamples* samples)
{
    if (samples->count == 0) return 0.0;
    double mean = bench_samples_mean(samples);
    double sum_sq = 0.0;
    for (int i = 0; i < samples->count; i++) {
        double diff = samples->samples[i] - mean;
        sum_sq += diff * diff;
    }
    return sqrt(sum_sq / samples->count);
}

void bench_samples_free(BenchSamples* samples)
{
    free(samples->samples);
    samples->samples = NULL;
    samples->count = 0;
    samples->capacity = 0;
}

uint64_t bench_get_memory_usage_mb(void)
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024 * 1024);
    }
    return 0;
}

void bench_json_start(BenchJsonWriter* writer, const char* filepath)
{
    fopen_s(&writer->file, filepath, "w");
    if (writer->file) {
        fprintf(writer->file, "{\n");
        writer->first_entry = 1;
    }
}

void bench_json_write_string(BenchJsonWriter* writer, const char* key, const char* value)
{
    if (!writer->file) return;
    if (!writer->first_entry) fprintf(writer->file, ",\n");
    fprintf(writer->file, "  \"%s\": \"%s\"", key, value);
    writer->first_entry = 0;
}

void bench_json_write_double(BenchJsonWriter* writer, const char* key, double value)
{
    if (!writer->file) return;
    if (!writer->first_entry) fprintf(writer->file, ",\n");
    fprintf(writer->file, "  \"%s\": %.6f", key, value);
    writer->first_entry = 0;
}

void bench_json_write_int(BenchJsonWriter* writer, const char* key, int value)
{
    if (!writer->file) return;
    if (!writer->first_entry) fprintf(writer->file, ",\n");
    fprintf(writer->file, "  \"%s\": %d", key, value);
    writer->first_entry = 0;
}

void bench_json_start_object(BenchJsonWriter* writer, const char* key)
{
    if (!writer->file) return;
    if (!writer->first_entry) fprintf(writer->file, ",\n");
    fprintf(writer->file, "  \"%s\": {\n", key);
    writer->first_entry = 1;
}

void bench_json_end_object(BenchJsonWriter* writer)
{
    if (!writer->file) return;
    fprintf(writer->file, "\n  }");
    writer->first_entry = 0;
}

void bench_json_end(BenchJsonWriter* writer)
{
    if (!writer->file) return;
    fprintf(writer->file, "\n}\n");
    fclose(writer->file);
    writer->file = NULL;
}

void bench_csv_start(BenchCsvWriter* writer, const char* filepath)
{
    fopen_s(&writer->file, filepath, "w");
}

void bench_csv_write_header(BenchCsvWriter* writer, const char** columns, int count)
{
    if (!writer->file) return;
    for (int i = 0; i < count; i++) {
        fprintf(writer->file, "%s", columns[i]);
        if (i < count - 1) fprintf(writer->file, ",");
    }
    fprintf(writer->file, "\n");
}

void bench_csv_write_row_start(BenchCsvWriter* writer)
{
}

void bench_csv_write_string(BenchCsvWriter* writer, const char* value)
{
    if (!writer->file) return;
    fprintf(writer->file, "\"%s\",", value);
}

void bench_csv_write_double(BenchCsvWriter* writer, double value)
{
    if (!writer->file) return;
    fprintf(writer->file, "%.6f,", value);
}

void bench_csv_write_int(BenchCsvWriter* writer, int value)
{
    if (!writer->file) return;
    fprintf(writer->file, "%d,", value);
}

void bench_csv_write_row_end(BenchCsvWriter* writer)
{
    if (!writer->file) return;
    fseek(writer->file, -1, SEEK_CUR);
    fprintf(writer->file, "\n");
}

void bench_csv_end(BenchCsvWriter* writer)
{
    if (writer->file) {
        fclose(writer->file);
        writer->file = NULL;
    }
}
