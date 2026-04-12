/**
 * d3d12_renderer_chunks_unified.cpp -- Unified vertex buffer upload for chunks
 */
#include "d3d12_renderer_internal.h"
#include <stdlib.h>
#include <string.h>

static void clear_chunk_upload_dirty_flags(SdkChunk* chunk)
{
    if (!chunk) return;

    for (int i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
        chunk->subchunks[i].upload_dirty = false;
        chunk->water_subchunks[i].upload_dirty = false;
    }
    chunk->far_mesh.upload_dirty = false;
    chunk->experimental_far_mesh.upload_dirty = false;
    chunk->far_exact_overlay_mesh.upload_dirty = false;
    sdk_chunk_refresh_mesh_state(chunk);
}

static void clear_chunk_unified_ranges(SdkChunk* chunk)
{
    if (!chunk) return;
    memset(chunk->subchunk_offsets, 0, sizeof(chunk->subchunk_offsets));
    memset(chunk->subchunk_vertex_counts, 0, sizeof(chunk->subchunk_vertex_counts));
    memset(chunk->water_offsets, 0, sizeof(chunk->water_offsets));
    memset(chunk->water_vertex_counts, 0, sizeof(chunk->water_vertex_counts));
    chunk->far_mesh_offset = 0u;
    chunk->far_mesh_vertex_count = 0u;
    chunk->experimental_far_offset = 0u;
    chunk->experimental_far_vertex_count = 0u;
    chunk->exact_overlay_offset = 0u;
    chunk->exact_overlay_vertex_count = 0u;
}

static uint32_t chunk_total_vertices_for_mode(const SdkChunk* chunk, SdkChunkGpuUploadMode mode)
{
    uint32_t total_vertices = 0u;

    if (!chunk) return 0u;

    if (mode == SDK_CHUNK_GPU_UPLOAD_FULL) {
        for (int i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
            total_vertices += chunk->subchunks[i].vertex_count;
            total_vertices += chunk->water_subchunks[i].vertex_count;
        }
    }

    total_vertices += chunk->far_mesh.vertex_count;
    total_vertices += chunk->experimental_far_mesh.vertex_count;
    total_vertices += chunk->far_exact_overlay_mesh.vertex_count;
    return total_vertices;
}

static SdkResult upload_unified_chunk_buffer(SdkChunk* chunk)
{
    D3D12_HEAP_PROPERTIES hp = {};
    D3D12_RESOURCE_DESC rd = {};
    ID3D12Resource* vb = nullptr;
    void* mapped = nullptr;
    D3D12_RANGE read_range = {0, 0};
    HRESULT hr;
    uint32_t total_vertices = 0;
    size_t vb_size;

    if (!chunk || !chunk->unified_staging) {
        return SDK_OK;
    }

    total_vertices = chunk->unified_total_vertices;

    if (total_vertices == 0) {
        return SDK_OK;
    }

    if (chunk->unified_staging_capacity < total_vertices) {
        return SDK_ERR_GENERIC;
    }

    vb_size = (size_t)total_vertices * sizeof(BlockVertex);
    vb = (ID3D12Resource*)chunk->unified_vertex_buffer;

    if (!vb || chunk->unified_vertex_capacity < total_vertices) {
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = vb_size;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (vb) {
            retire_resource_later(vb);
            chunk->unified_vertex_buffer = nullptr;
            chunk->unified_vb_gpu = nullptr;
            chunk->unified_vertex_capacity = 0u;
            vb = nullptr;
        }

        hr = g_rs.device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&vb));
        if (FAILED(hr)) {
            OutputDebugStringA("[UNIFIED] Failed to create unified vertex buffer\n");
            return SDK_ERR_GENERIC;
        }

        chunk->unified_vertex_buffer = vb;
        chunk->unified_vb_gpu = (void*)vb->GetGPUVirtualAddress();
        chunk->unified_vertex_capacity = total_vertices;
    }

    hr = vb->Map(0, &read_range, &mapped);
    if (FAILED(hr)) {
        OutputDebugStringA("[UNIFIED] Failed to map unified vertex buffer\n");
        return SDK_ERR_GENERIC;
    }

    // Copy staging buffer to GPU
    memcpy(mapped, chunk->unified_staging, total_vertices * sizeof(BlockVertex));
    vb->Unmap(0, nullptr);

    g_rs.perf_upload_bytes_pending += (uint64_t)vb_size;

    return SDK_OK;
}

extern "C" SdkResult sdk_renderer_upload_chunk_mesh_unified_mode(SdkChunk* chunk,
                                                                 SdkChunkGpuUploadMode mode)
{
    if (!chunk) {
        return SDK_OK;
    }

    if (mode == SDK_CHUNK_GPU_UPLOAD_FAR_ONLY &&
        chunk_total_vertices_for_mode(chunk, SDK_CHUNK_GPU_UPLOAD_FAR_ONLY) == 0u) {
        mode = SDK_CHUNK_GPU_UPLOAD_FULL;
    }

    // Free old per-submesh GPU buffers to prevent massive leak
    for (int sub_index = 0; sub_index < CHUNK_SUBCHUNK_COUNT; ++sub_index) {
        SdkChunkSubmesh* sub = &chunk->subchunks[sub_index];
        if (sub->vertex_buffer) {
            retire_resource_later((ID3D12Resource*)sub->vertex_buffer);
            sub->vertex_buffer = nullptr;
            sub->vb_gpu = nullptr;
        }
        sub = &chunk->water_subchunks[sub_index];
        if (sub->vertex_buffer) {
            retire_resource_later((ID3D12Resource*)sub->vertex_buffer);
            sub->vertex_buffer = nullptr;
            sub->vb_gpu = nullptr;
        }
    }
    if (chunk->far_mesh.vertex_buffer) {
        retire_resource_later((ID3D12Resource*)chunk->far_mesh.vertex_buffer);
        chunk->far_mesh.vertex_buffer = nullptr;
        chunk->far_mesh.vb_gpu = nullptr;
    }
    if (chunk->experimental_far_mesh.vertex_buffer) {
        retire_resource_later((ID3D12Resource*)chunk->experimental_far_mesh.vertex_buffer);
        chunk->experimental_far_mesh.vertex_buffer = nullptr;
        chunk->experimental_far_mesh.vb_gpu = nullptr;
    }
    if (chunk->far_exact_overlay_mesh.vertex_buffer) {
        retire_resource_later((ID3D12Resource*)chunk->far_exact_overlay_mesh.vertex_buffer);
        chunk->far_exact_overlay_mesh.vertex_buffer = nullptr;
        chunk->far_exact_overlay_mesh.vb_gpu = nullptr;
    }

    // Build unified staging buffer from individual submeshes
    uint32_t total_vertices = chunk_total_vertices_for_mode(chunk, mode);
    clear_chunk_unified_ranges(chunk);

    if (total_vertices == 0) {
        sdk_renderer_free_chunk_unified_buffer(chunk);
        chunk->gpu_mesh_generation = chunk->cpu_mesh_generation;
        chunk->unified_total_vertices = 0;
        chunk->gpu_upload_mode = SDK_CHUNK_GPU_UPLOAD_NONE;
        clear_chunk_upload_dirty_flags(chunk);
        return SDK_OK;
    }

    if (chunk->unified_staging_capacity < total_vertices) {
        uint32_t new_capacity = total_vertices;
        BlockVertex* new_staging;

        if (new_capacity < 4096u) {
            new_capacity = 4096u;
        }
        new_staging = (BlockVertex*)realloc(chunk->unified_staging,
                                            (size_t)new_capacity * sizeof(BlockVertex));
        if (!new_staging) {
            return SDK_ERR_GENERIC;
        }
        chunk->unified_staging = new_staging;
        chunk->unified_staging_capacity = new_capacity;
    }
    if (!chunk->unified_staging) {
        chunk->unified_staging = (BlockVertex*)malloc((size_t)total_vertices * sizeof(BlockVertex));
        if (!chunk->unified_staging) {
            return SDK_ERR_GENERIC;
        }
        chunk->unified_staging_capacity = total_vertices;
    }

    // Copy all submeshes into unified staging buffer
    uint32_t offset = 0;
    if (mode == SDK_CHUNK_GPU_UPLOAD_FULL) {
        for (int i = 0; i < CHUNK_SUBCHUNK_COUNT; ++i) {
            chunk->subchunk_offsets[i] = offset;
            chunk->subchunk_vertex_counts[i] = chunk->subchunks[i].vertex_count;
            if (chunk->subchunks[i].vertex_count > 0 && chunk->subchunks[i].cpu_vertices) {
                memcpy(chunk->unified_staging + offset,
                       chunk->subchunks[i].cpu_vertices,
                       chunk->subchunks[i].vertex_count * sizeof(BlockVertex));
                offset += chunk->subchunks[i].vertex_count;
            }

            chunk->water_offsets[i] = offset;
            chunk->water_vertex_counts[i] = chunk->water_subchunks[i].vertex_count;
            if (chunk->water_subchunks[i].vertex_count > 0 && chunk->water_subchunks[i].cpu_vertices) {
                memcpy(chunk->unified_staging + offset,
                       chunk->water_subchunks[i].cpu_vertices,
                       chunk->water_subchunks[i].vertex_count * sizeof(BlockVertex));
                offset += chunk->water_subchunks[i].vertex_count;
            }
        }
    }

    chunk->far_mesh_offset = offset;
    chunk->far_mesh_vertex_count = chunk->far_mesh.vertex_count;
    if (chunk->far_mesh.vertex_count > 0 && chunk->far_mesh.cpu_vertices) {
        memcpy(chunk->unified_staging + offset,
               chunk->far_mesh.cpu_vertices,
               chunk->far_mesh.vertex_count * sizeof(BlockVertex));
        offset += chunk->far_mesh.vertex_count;
    }

    chunk->experimental_far_offset = offset;
    chunk->experimental_far_vertex_count = chunk->experimental_far_mesh.vertex_count;
    if (chunk->experimental_far_mesh.vertex_count > 0 && chunk->experimental_far_mesh.cpu_vertices) {
        memcpy(chunk->unified_staging + offset,
               chunk->experimental_far_mesh.cpu_vertices,
               chunk->experimental_far_mesh.vertex_count * sizeof(BlockVertex));
        offset += chunk->experimental_far_mesh.vertex_count;
    }

    chunk->exact_overlay_offset = offset;
    chunk->exact_overlay_vertex_count = chunk->far_exact_overlay_mesh.vertex_count;
    if (chunk->far_exact_overlay_mesh.vertex_count > 0 && chunk->far_exact_overlay_mesh.cpu_vertices) {
        memcpy(chunk->unified_staging + offset,
               chunk->far_exact_overlay_mesh.cpu_vertices,
               chunk->far_exact_overlay_mesh.vertex_count * sizeof(BlockVertex));
        offset += chunk->far_exact_overlay_mesh.vertex_count;
    }

    chunk->unified_total_vertices = total_vertices;
    if (upload_unified_chunk_buffer(chunk) != SDK_OK) {
        return SDK_ERR_GENERIC;
    }

    chunk->gpu_upload_mode = (uint8_t)mode;
    chunk->gpu_mesh_generation = chunk->cpu_mesh_generation;
    clear_chunk_upload_dirty_flags(chunk);
    return SDK_OK;
}

extern "C" SdkResult sdk_renderer_upload_chunk_mesh_unified(SdkChunk* chunk)
{
    return sdk_renderer_upload_chunk_mesh_unified_mode(chunk, SDK_CHUNK_GPU_UPLOAD_FULL);
}

extern "C" void sdk_renderer_free_chunk_unified_buffer(SdkChunk* chunk)
{
    if (!chunk) return;

    if (chunk->unified_vertex_buffer) {
        retire_resource_later((ID3D12Resource*)chunk->unified_vertex_buffer);
        chunk->unified_vertex_buffer = nullptr;
        chunk->unified_vb_gpu = nullptr;
    }
    
    chunk->unified_total_vertices = 0;
    chunk->unified_vertex_capacity = 0;
    chunk->gpu_upload_mode = SDK_CHUNK_GPU_UPLOAD_NONE;
    chunk->gpu_mesh_generation = 0u;
    clear_chunk_unified_ranges(chunk);
    free(chunk->unified_staging);
    chunk->unified_staging = nullptr;
    chunk->unified_staging_capacity = 0u;
}
