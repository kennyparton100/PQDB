/**
 * d3d12_renderer_chunks.cpp -- Chunk meshes, camera helpers, and scene attachments.
 */
#include "d3d12_renderer_internal.h"

SdkChunkManager* g_chunk_mgr = nullptr;

extern "C" void sdk_renderer_set_chunk_manager(SdkChunkManager* cm)
{
    g_chunk_mgr = cm;
}

static SdkResult upload_mesh_slice(SdkChunkSubmesh* sub)
{
    size_t vb_size;
    D3D12_HEAP_PROPERTIES hp = {};
    D3D12_RESOURCE_DESC rd = {};
    ID3D12Resource* vb = nullptr;
    void* mapped = nullptr;
    D3D12_RANGE read_range = {0, 0};
    HRESULT hr;

    if (!sub || !sub->upload_dirty) {
        return SDK_OK;
    }

    if (sub->vertex_buffer) {
        retire_resource_later((ID3D12Resource*)sub->vertex_buffer);
        sub->vertex_buffer = nullptr;
        sub->vb_gpu = nullptr;
    }

    if (sub->vertex_count == 0 || !sub->cpu_vertices) {
        sub->upload_dirty = false;
        return SDK_OK;
    }

    vb_size = (size_t)sub->vertex_count * sizeof(BlockVertex);
    if (vb_size == 0) {
        sub->upload_dirty = false;
        return SDK_OK;
    }

    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = vb_size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = g_rs.device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE,
        &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&vb));
    if (FAILED(hr)) return SDK_ERR_GENERIC;

    hr = vb->Map(0, &read_range, &mapped);
    if (FAILED(hr)) {
        vb->Release();
        return SDK_ERR_GENERIC;
    }

    memcpy(mapped, sub->cpu_vertices, sub->vertex_count * sizeof(BlockVertex));
    vb->Unmap(0, nullptr);

    sub->vertex_buffer = vb;
    sub->vb_gpu = (void*)vb->GetGPUVirtualAddress();
    sub->upload_dirty = false;
    g_rs.perf_upload_bytes_pending += (uint64_t)vb_size;
    return SDK_OK;
}

extern "C" SdkResult sdk_renderer_upload_chunk_mesh(SdkChunk* chunk)
{
    if (!chunk) {
        OutputDebugStringA("[UPLOAD] Failed validation: null chunk\n");
        return SDK_OK;
    }

    for (int sub_index = 0; sub_index < CHUNK_SUBCHUNK_COUNT; ++sub_index) {
        SdkChunkSubmesh* sub = &chunk->subchunks[sub_index];
        SdkResult upload_result = upload_mesh_slice(sub);
        if (upload_result != SDK_OK) {
            char dbg[160];
            sprintf_s(dbg, sizeof(dbg),
                      "[UPLOAD] opaque subchunk upload failed chunk=(%d,%d) sub=%d\n",
                      chunk->cx, chunk->cz, sub_index);
            OutputDebugStringA(dbg);
            return upload_result;
        }
        upload_result = upload_mesh_slice(&chunk->water_subchunks[sub_index]);
        if (upload_result != SDK_OK) {
            char dbg[160];
            sprintf_s(dbg, sizeof(dbg),
                      "[UPLOAD] water subchunk upload failed chunk=(%d,%d) sub=%d\n",
                      chunk->cx, chunk->cz, sub_index);
            OutputDebugStringA(dbg);
            return upload_result;
        }
    }

    if (upload_mesh_slice(&chunk->far_mesh) != SDK_OK) {
        char dbg[160];
        sprintf_s(dbg, sizeof(dbg),
                  "[UPLOAD] stable far upload failed chunk=(%d,%d)\n",
                  chunk->cx, chunk->cz);
        OutputDebugStringA(dbg);
        return SDK_ERR_GENERIC;
    }
    if (upload_mesh_slice(&chunk->experimental_far_mesh) != SDK_OK) {
        char dbg[160];
        sprintf_s(dbg, sizeof(dbg),
                  "[UPLOAD] experimental far upload failed chunk=(%d,%d)\n",
                  chunk->cx, chunk->cz);
        OutputDebugStringA(dbg);
        return SDK_ERR_GENERIC;
    }
    if (upload_mesh_slice(&chunk->far_exact_overlay_mesh) != SDK_OK) {
        char dbg[160];
        sprintf_s(dbg, sizeof(dbg),
                  "[UPLOAD] exact overlay upload failed chunk=(%d,%d)\n",
                  chunk->cx, chunk->cz);
        OutputDebugStringA(dbg);
        return SDK_ERR_GENERIC;
    }

    sdk_chunk_refresh_mesh_state(chunk);
    return SDK_OK;
}

extern "C" void sdk_renderer_free_chunk_mesh(SdkChunk* chunk)
{
    if (!chunk) return;

    for (int sub_index = 0; sub_index < CHUNK_SUBCHUNK_COUNT; ++sub_index) {
        SdkChunkSubmesh* sub = &chunk->subchunks[sub_index];
        if (sub->vertex_buffer) {
            retire_resource_later((ID3D12Resource*)sub->vertex_buffer);
            sub->vertex_buffer = nullptr;
        }
        sub->vb_gpu = nullptr;
        sub->upload_dirty = false;
        sub = &chunk->water_subchunks[sub_index];
        if (sub->vertex_buffer) {
            retire_resource_later((ID3D12Resource*)sub->vertex_buffer);
            sub->vertex_buffer = nullptr;
        }
        sub->vb_gpu = nullptr;
        sub->upload_dirty = false;
    }
    if (chunk->far_mesh.vertex_buffer) {
        retire_resource_later((ID3D12Resource*)chunk->far_mesh.vertex_buffer);
        chunk->far_mesh.vertex_buffer = nullptr;
    }
    chunk->far_mesh.vb_gpu = nullptr;
    chunk->far_mesh.upload_dirty = false;
    if (chunk->experimental_far_mesh.vertex_buffer) {
        retire_resource_later((ID3D12Resource*)chunk->experimental_far_mesh.vertex_buffer);
        chunk->experimental_far_mesh.vertex_buffer = nullptr;
    }
    chunk->experimental_far_mesh.vb_gpu = nullptr;
    chunk->experimental_far_mesh.upload_dirty = false;
    if (chunk->far_exact_overlay_mesh.vertex_buffer) {
        retire_resource_later((ID3D12Resource*)chunk->far_exact_overlay_mesh.vertex_buffer);
        chunk->far_exact_overlay_mesh.vertex_buffer = nullptr;
    }
    chunk->far_exact_overlay_mesh.vb_gpu = nullptr;
    chunk->far_exact_overlay_mesh.upload_dirty = false;
    chunk->upload_subchunks_mask = 0;
    chunk->gpu_mesh_generation = 0u;
}

extern "C" SdkResult sdk_renderer_draw_chunks(void)
{
    /* This function is called during rendering, but actual drawing happens
     * inside sdk_renderer_frame after the triangle is drawn.
     * For now, just validate state. */
    if (!g_rs.initialized) return SDK_ERR_NOT_INIT;
    return SDK_OK;
}

extern "C" void sdk_renderer_get_camera_pos(float* out_x, float* out_y, float* out_z)
{
    if (!out_x || !out_y || !out_z) return;
    
    *out_x = g_rs.camera.position.x;
    *out_y = g_rs.camera.position.y;
    *out_z = g_rs.camera.position.z;
}

extern "C" void sdk_renderer_set_camera_pos(float x, float y, float z)
{
    g_rs.camera.position.x = x;
    g_rs.camera.position.y = y;
    g_rs.camera.position.z = z;
}

extern "C" void sdk_renderer_get_camera_target(float* out_x, float* out_y, float* out_z)
{
    if (!out_x || !out_y || !out_z) return;
    *out_x = g_rs.camera.target.x;
    *out_y = g_rs.camera.target.y;
    *out_z = g_rs.camera.target.z;
}

extern "C" void sdk_renderer_set_camera_target(float x, float y, float z)
{
    g_rs.camera.target.x = x;
    g_rs.camera.target.y = y;
    g_rs.camera.target.z = z;
}

extern "C" void sdk_renderer_set_entities(const SdkEntityList* entities)
{
    g_rs.entity_list = entities;
}

extern "C" void sdk_renderer_set_player_character_mesh(const BlockVertex* vertices, uint32_t vertex_count)
{
    if (g_rs.player_character_cpu_vertices) {
        free(g_rs.player_character_cpu_vertices);
        g_rs.player_character_cpu_vertices = nullptr;
    }
    g_rs.player_character_cpu_count = 0;
    g_rs.player_character_vert_count = 0;

    if (g_rs.player_character_vb) {
        wait_for_gpu();
        g_rs.player_character_vb.Reset();
    }

    if (!vertices || vertex_count == 0) {
        return;
    }

    g_rs.player_character_cpu_vertices =
        (BlockVertex*)malloc((size_t)vertex_count * sizeof(BlockVertex));
    if (!g_rs.player_character_cpu_vertices) {
        return;
    }

    memcpy(g_rs.player_character_cpu_vertices, vertices, (size_t)vertex_count * sizeof(BlockVertex));
    g_rs.player_character_cpu_count = vertex_count;
}

extern "C" void sdk_renderer_set_outline(int bx, int by, int bz, bool visible)
{
    if (!visible) {
        g_rs.outline_visible = false;
        return;
    }
    if (bx != g_rs.outline_bx || by != g_rs.outline_by || bz != g_rs.outline_bz || !g_rs.outline_visible) {
        g_rs.outline_dirty = true;
    }
    g_rs.outline_bx = bx;
    g_rs.outline_by = by;
    g_rs.outline_bz = bz;
    g_rs.outline_visible = true;
}

extern "C" void sdk_renderer_set_placement_preview(const SdkPlacementPreview* preview)
{
    SdkPlacementPreview next = {};

    if (preview && preview->visible) {
        next = *preview;
    }

    if (!next.visible) {
        g_rs.placement_preview_visible = false;
        memset(&g_rs.placement_preview, 0, sizeof(g_rs.placement_preview));
        return;
    }

    if (!g_rs.placement_preview_visible ||
        memcmp(&g_rs.placement_preview, &next, sizeof(next)) != 0) {
        g_rs.placement_preview = next;
        g_rs.placement_preview_dirty = true;
    }
    g_rs.placement_preview_visible = true;
}
