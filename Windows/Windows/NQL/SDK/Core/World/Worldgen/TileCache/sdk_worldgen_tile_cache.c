/**
 * sdk_worldgen_tile_cache.c -- Disk-backed continental tile cache implementation
 */
#include "sdk_worldgen_tile_cache.h"
#include "../Internal/sdk_worldgen_internal.h"
#include "../../../API/Internal/sdk_load_trace.h"
#include "../../../LZ4/lz4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t world_seed;
    int32_t tile_x;
    int32_t tile_z;
    uint32_t uncompressed_size;
    uint32_t compressed_size;
} SdkTileCacheHeader;
#pragma pack(pop)

typedef struct {
    char cache_dir[MAX_PATH];
    uint32_t world_seed;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint64_t bytes_loaded;
    uint64_t bytes_saved;
} SdkWorldGenTileCacheInternal;

static int ensure_directory_exists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return 1;
    }
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static void build_cache_path(const SdkWorldGenTileCacheInternal* impl, 
                              int32_t tile_x, 
                              int32_t tile_z,
                              char* out_path,
                              size_t out_path_size) {
    snprintf(out_path, out_path_size, "%s\\seed_%08x_tx%d_tz%d.ctc",
             impl->cache_dir, impl->world_seed, tile_x, tile_z);
}

void sdk_worldgen_tile_cache_init(SdkWorldGenTileCache* cache, uint32_t world_seed, const char* cache_dir) {
    SdkWorldGenTileCacheInternal* impl;
    
    if (!cache) return;
    
    impl = (SdkWorldGenTileCacheInternal*)calloc(1, sizeof(SdkWorldGenTileCacheInternal));
    if (!impl) return;
    
    impl->world_seed = world_seed;
    
    if (cache_dir && cache_dir[0]) {
        strncpy_s(impl->cache_dir, sizeof(impl->cache_dir), cache_dir, _TRUNCATE);
    } else {
        strcpy_s(impl->cache_dir, sizeof(impl->cache_dir), "WorldGenCache");
    }
    
    if (!ensure_directory_exists(impl->cache_dir)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "[TILECACHE] Warning: Could not create cache directory: %s\n", impl->cache_dir);
        sdk_debug_log_output(msg);
    }
    
    cache->impl = impl;
    
}

void sdk_worldgen_tile_cache_shutdown(SdkWorldGenTileCache* cache) {
    SdkWorldGenTileCacheInternal* impl;
    
    if (!cache || !cache->impl) return;
    
    impl = (SdkWorldGenTileCacheInternal*)cache->impl;
    
    free(impl);
    cache->impl = NULL;
}

int sdk_worldgen_tile_cache_load_continental(SdkWorldGenTileCache* cache,
                                              int32_t tile_x,
                                              int32_t tile_z,
                                              void* out_cells) {
    SdkWorldGenTileCacheInternal* impl;
    char path[MAX_PATH];
    FILE* file;
    SdkTileCacheHeader header;
    char* compressed_data = NULL;
    int result = 0;
    
    if (!cache || !cache->impl || !out_cells) return 0;
    
    impl = (SdkWorldGenTileCacheInternal*)cache->impl;
    build_cache_path(impl, tile_x, tile_z, path, sizeof(path));
    
    if (fopen_s(&file, path, "rb") != 0 || !file) {
        impl->cache_misses++;
        return 0;
    }
    
    if (fread(&header, sizeof(header), 1, file) != 1) {
        fclose(file);
        impl->cache_misses++;
        return 0;
    }
    
    if (header.magic != SDK_TILE_CACHE_MAGIC ||
        header.version != SDK_TILE_CACHE_VERSION ||
        header.world_seed != impl->world_seed ||
        header.tile_x != tile_x ||
        header.tile_z != tile_z) {
        fclose(file);
        impl->cache_misses++;
        return 0;
    }
    
    const uint32_t expected_size = sizeof(SdkContinentalCell) * SDK_WORLDGEN_CONTINENT_CELL_COUNT;
    if (header.uncompressed_size != expected_size) {
        fclose(file);
        impl->cache_misses++;
        return 0;
    }
    
    compressed_data = (char*)malloc(header.compressed_size);
    if (!compressed_data) {
        fclose(file);
        impl->cache_misses++;
        return 0;
    }
    
    if (fread(compressed_data, header.compressed_size, 1, file) != 1) {
        free(compressed_data);
        fclose(file);
        impl->cache_misses++;
        return 0;
    }
    
    int decompressed = LZ4_decompress_safe(compressed_data, (char*)out_cells, 
                                            header.compressed_size, expected_size);
    
    if (decompressed == (int)expected_size) {
        impl->cache_hits++;
        impl->bytes_loaded += header.compressed_size + sizeof(header);
        result = 1;
    } else {
        impl->cache_misses++;
    }
    
    free(compressed_data);
    fclose(file);
    
    return result;
}

int sdk_worldgen_tile_cache_save_continental(SdkWorldGenTileCache* cache,
                                              int32_t tile_x,
                                              int32_t tile_z,
                                              const void* cells) {
    SdkWorldGenTileCacheInternal* impl;
    char path[MAX_PATH];
    FILE* file;
    SdkTileCacheHeader header;
    char* compressed_data = NULL;
    int compressed_size;
    int result = 0;
    
    if (!cache || !cache->impl || !cells) return 0;
    
    impl = (SdkWorldGenTileCacheInternal*)cache->impl;
    
    const uint32_t uncompressed_size = sizeof(SdkContinentalCell) * SDK_WORLDGEN_CONTINENT_CELL_COUNT;
    const int max_compressed = LZ4_compressBound(uncompressed_size);
    
    compressed_data = (char*)malloc(max_compressed);
    if (!compressed_data) return 0;
    
    compressed_size = LZ4_compress_default((const char*)cells, compressed_data,
                                            uncompressed_size, max_compressed);
    
    if (compressed_size <= 0) {
        free(compressed_data);
        return 0;
    }
    
    build_cache_path(impl, tile_x, tile_z, path, sizeof(path));
    
    if (fopen_s(&file, path, "wb") != 0 || !file) {
        free(compressed_data);
        return 0;
    }
    
    header.magic = SDK_TILE_CACHE_MAGIC;
    header.version = SDK_TILE_CACHE_VERSION;
    header.world_seed = impl->world_seed;
    header.tile_x = tile_x;
    header.tile_z = tile_z;
    header.uncompressed_size = uncompressed_size;
    header.compressed_size = (uint32_t)compressed_size;
    
    if (fwrite(&header, sizeof(header), 1, file) == 1 &&
        fwrite(compressed_data, compressed_size, 1, file) == 1) {
        impl->bytes_saved += compressed_size + sizeof(header);
        result = 1;
        
        char msg[256];
        snprintf(msg, sizeof(msg), 
                 "[TILECACHE] Saved tile [%d,%d]: %u bytes -> %d bytes (%.1f%% compression)\n",
                 tile_x, tile_z, uncompressed_size, compressed_size,
                 100.0 * (1.0 - (double)compressed_size / (double)uncompressed_size));
        sdk_debug_log_output(msg);
    }
    
    fclose(file);
    free(compressed_data);
    
    return result;
}

void sdk_worldgen_tile_cache_clear(SdkWorldGenTileCache* cache) {
    SdkWorldGenTileCacheInternal* impl;
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char pattern[MAX_PATH];
    int deleted_count = 0;
    
    if (!cache || !cache->impl) return;
    
    impl = (SdkWorldGenTileCacheInternal*)cache->impl;
    
    snprintf(pattern, sizeof(pattern), "%s\\seed_%08x_*.ctc", impl->cache_dir, impl->world_seed);
    
    find_handle = FindFirstFileA(pattern, &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            char file_path[MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s\\%s", impl->cache_dir, find_data.cFileName);
            if (DeleteFileA(file_path)) {
                deleted_count++;
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "[TILECACHE] Cleared %d cached tiles for seed 0x%08x\n",
             deleted_count, impl->world_seed);
    sdk_debug_log_output(msg);
}

void sdk_worldgen_tile_cache_get_stats(const SdkWorldGenTileCache* cache,
                                        uint32_t* out_hits,
                                        uint32_t* out_misses,
                                        uint64_t* out_bytes_loaded,
                                        uint64_t* out_bytes_saved) {
    SdkWorldGenTileCacheInternal* impl;
    
    if (!cache || !cache->impl) return;
    
    impl = (SdkWorldGenTileCacheInternal*)cache->impl;
    
    if (out_hits) *out_hits = impl->cache_hits;
    if (out_misses) *out_misses = impl->cache_misses;
    if (out_bytes_loaded) *out_bytes_loaded = impl->bytes_loaded;
    if (out_bytes_saved) *out_bytes_saved = impl->bytes_saved;
}
