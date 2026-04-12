/**
 * bench_standalone_timing.c - Standalone timing test
 * Tests basic operations without SDK dependencies
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <psapi.h>

typedef struct {
    LARGE_INTEGER freq;
    LARGE_INTEGER start;
} Timer;

void timer_init(Timer* t) {
    QueryPerformanceFrequency(&t->freq);
}

void timer_start(Timer* t) {
    QueryPerformanceCounter(&t->start);
}

double timer_end_ms(Timer* t) {
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    return ((double)(end.QuadPart - t->start.QuadPart) * 1000.0) / (double)t->freq.QuadPart;
}

uint64_t get_memory_mb(void) {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024 * 1024);
    }
    return 0;
}

uint32_t simple_hash(uint32_t x, uint32_t z, uint32_t seed) {
    uint32_t h = seed;
    h ^= x * 0x9e3779b9u;
    h ^= z * 0x85ebca6bu;
    h ^= h >> 16;
    h *= 0x21f0aaadu;
    h ^= h >> 15;
    h *= 0x735a2d97u;
    h ^= h >> 15;
    return h;
}

typedef struct {
    uint8_t* blocks;
    int cx, cz;
} FakeChunk;

void init_fake_chunk(FakeChunk* c, int cx, int cz) {
    c->blocks = (uint8_t*)malloc(64 * 64 * 1024);
    c->cx = cx;
    c->cz = cz;
}

void free_fake_chunk(FakeChunk* c) {
    free(c->blocks);
}

int main(void) {
    Timer timer;
    double elapsed;
    FILE* f;
    
    timer_init(&timer);
    
    printf("=== Standalone Performance Analysis ===\n\n");
    
    timer_start(&timer);
    for (int i = 0; i < 100000; i++) {
        simple_hash(i % 1000, i / 1000, 12345);
    }
    elapsed = timer_end_ms(&timer);
    printf("Hash operations (100k):      %.3f ms (%.6f ms per hash)\n", elapsed, elapsed / 100000.0);
    
    timer_start(&timer);
    FakeChunk chunks[10];
    for (int i = 0; i < 10; i++) {
        init_fake_chunk(&chunks[i], i, i);
    }
    elapsed = timer_end_ms(&timer);
    printf("Chunk allocation (10):       %.3f ms (%.3f ms per chunk)\n", elapsed, elapsed / 10.0);
    
    timer_start(&timer);
    for (int i = 0; i < 10; i++) {
        memset(chunks[i].blocks, 0, 64 * 64 * 1024);
    }
    elapsed = timer_end_ms(&timer);
    printf("Chunk memset (10 x 4MB):     %.3f ms (%.3f ms per chunk)\n", elapsed, elapsed / 10.0);
    
    for (int i = 0; i < 10; i++) {
        free_fake_chunk(&chunks[i]);
    }
    
    uint64_t mem = get_memory_mb();
    printf("\nMemory usage: %llu MB\n", mem);
    
    fopen_s(&f, "bench_standalone.json", "w");
    if (f) {
        fprintf(f, "{\n");
        fprintf(f, "  \"hash_100k_ms\": %.6f,\n", elapsed);
        fprintf(f, "  \"chunk_alloc_10_ms\": %.6f,\n", elapsed);
        fprintf(f, "  \"memory_mb\": %llu\n", mem);
        fprintf(f, "}\n");
        fclose(f);
    }
    
    printf("\nResults written to bench_standalone.json\n");
    return 0;
}
