/**
 * debugger_stubs.c -- No-op stubs for functions not needed by the headless debugger.
 *
 * These stubs satisfy link-time references from SDK modules that are compiled
 * for worldgen + persistence but whose callers are never invoked at runtime
 * in the debugger (e.g. frustum culling and LZ4 tile caching).
 */
#include "../Core/World/Chunks/sdk_chunk.h"
#include "../Core/Camera/sdk_camera.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- Renderer ---- */
void sdk_renderer_free_chunk_mesh(SdkChunk* chunk) { (void)chunk; }
void sdk_renderer_free_chunk_unified_buffer(SdkChunk* chunk) { (void)chunk; }

/* ---- Camera / Frustum ---- */
bool sdk_frustum_contains_aabb(const SdkFrustum* frustum,
                               float min_x, float min_y, float min_z,
                               float max_x, float max_y, float max_z)
{
    (void)frustum; (void)min_x; (void)min_y; (void)min_z;
    (void)max_x; (void)max_y; (void)max_z;
    return false;
}

/* ---- LZ4 (tile cache compression — not needed for debugger) ---- */
int LZ4_compress_default(const char* src, char* dst, int srcSize, int dstCapacity)
{
    (void)src; (void)dst; (void)srcSize; (void)dstCapacity;
    return 0;
}

int LZ4_decompress_safe(const char* src, char* dst, int compressedSize, int dstCapacity)
{
    (void)src; (void)dst; (void)compressedSize; (void)dstCapacity;
    return -1;
}

int LZ4_compressBound(int inputSize)
{
    (void)inputSize;
    return 0;
}

/* ---- Settlement runtime hooks (not needed for headless CLI diagnostics) ---- */
void sdk_settlement_runtime_notify_chunk_loaded(int cx, int cz)
{
    (void)cx;
    (void)cz;
}

void sdk_settlement_runtime_notify_chunk_unloaded(int cx, int cz)
{
    (void)cx;
    (void)cz;
}
