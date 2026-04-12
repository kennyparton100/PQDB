/**
 * d3d12_renderer_internal.h -- Shared internals for split D3D12 renderer modules.
 */
#ifndef NQLSDK_D3D12_RENDERER_INTERNAL_H
#define NQLSDK_D3D12_RENDERER_INTERNAL_H

#include "d3d12_renderer.h"
#include "d3d12_helpers.h"
#include "d3d12_pipeline.h"
#include "../Core/Camera/sdk_camera.h"
#include "../Core/Scene/sdk_scene.h"
#include "../Core/Math/sdk_math.h"
#include "../Core/World/Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../Core/World/Terrain/sdk_terrain.h"
#include "../Core/MeshBuilder/sdk_mesh_builder.h"
#include "../Core/World/Simulation/sdk_simulation.h"
#include "../Core/Entities/sdk_entity.h"
#include "../Core/Items/sdk_item.h"
#include "../Core/World/Blocks/sdk_block.h"
#include "../Core/Profiler/sdk_profiler.h"

#include <wincodec.h>
#include <string>
#include <vector>
#include <string.h>
#include <math.h>
#include <stdio.h>

#pragma comment(lib, "windowscodecs.lib")

/* ======================================================================
 * CONSTANTS
 * ====================================================================== */

static const UINT FRAME_COUNT = 2; /* Double-buffered */
static const UINT RETIRED_RESOURCE_CAPACITY = 4096;
static const UINT BLOCK_TEXTURE_TILE_SIZE = 16;
static const UINT BLOCK_TEXTURE_FACE_COUNT = 6;
static const UINT HUD_FONT_FIRST_CHAR = 32;
static const UINT HUD_FONT_LAST_CHAR = 126;
static const UINT HUD_FONT_GLYPH_COUNT = HUD_FONT_LAST_CHAR - HUD_FONT_FIRST_CHAR + 1;
static const uint32_t HUD_TEXT_NORMAL_SENTINEL = 0xFFFFFFFFu;

/* ======================================================================
 * RENDERER STATE
 * ====================================================================== */

struct RendererState {
    struct RetiredResource {
        ID3D12Resource* resource;
        UINT64          retire_fence;
    };

    /* Core D3D12 objects */
    ComPtr<IDXGIFactory4>           factory;
    ComPtr<ID3D12Device>            device;
    ComPtr<ID3D12CommandQueue>      cmd_queue;
    ComPtr<ID3D12CommandAllocator>  cmd_alloc[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList> cmd_list;

    /* Swap chain */
    ComPtr<IDXGISwapChain3>         swap_chain;
    ComPtr<ID3D12Resource>          render_targets[FRAME_COUNT];
    ComPtr<ID3D12DescriptorHeap>    rtv_heap;
    UINT                            rtv_descriptor_size;

    /* Synchronisation */
    ComPtr<ID3D12Fence>             fence;
    UINT64                          fence_value;        /* Next fence value to signal */
    UINT64                          fence_values[FRAME_COUNT]; /* Per-allocator: last submitted fence */
    HANDLE                          fence_event;
    UINT                            frame_index;

    /* Pipeline */
    ComPtr<ID3D12RootSignature>     root_sig;
    ComPtr<ID3D12PipelineState>     pso;
    ComPtr<ID3D12PipelineState>     water_pso;
    ComPtr<ID3D12Resource>          block_texture_array;
    ComPtr<ID3D12DescriptorHeap>    texture_srv_heap;
    uint32_t                        block_texture_layers;
    uint32_t                        font_texture_layer_base;
    bool                            com_initialized;

    /* Triangle vertex buffer */
    ComPtr<ID3D12Resource>          vertex_buffer;
    D3D12_VERTEX_BUFFER_VIEW        vb_view;

    /* MVP constant buffer (per-frame) */
    ComPtr<ID3D12Resource>          constant_buffer[FRAME_COUNT];
    UINT8*                          cb_mapped[FRAME_COUNT];  /* Persistent mapped pointer */
    
    /* 3D scene */
    SdkCamera                       camera;
    SdkScene                        scene;
    
    /* Animation */
    LARGE_INTEGER                   perf_freq;
    LARGE_INTEGER                   last_time;
    bool                            anim_started;

    /* Depth buffer */
    ComPtr<ID3D12Resource>          depth_buffer;
    ComPtr<ID3D12DescriptorHeap>    dsv_heap;

    /* Block outline highlight */
    ComPtr<ID3D12Resource>          outline_vb;
    int                             outline_bx, outline_by, outline_bz;
    bool                            outline_visible;
    bool                            outline_dirty;
    ComPtr<ID3D12Resource>          placement_preview_vb;
    uint32_t                        placement_preview_vert_count;
    SdkPlacementPreview             placement_preview;
    bool                            placement_preview_visible;
    bool                            placement_preview_dirty;

    /* Entity rendering (item drops) */
    ComPtr<ID3D12Resource>          entity_vb;
    uint32_t                        entity_vert_count;
    const SdkEntityList*            entity_list;  /* Borrowed pointer, valid during frame */
    ComPtr<ID3D12Resource>          player_character_vb;
    uint32_t                        player_character_vert_count;
    BlockVertex*                    player_character_cpu_vertices;
    uint32_t                        player_character_cpu_count;

    /* HUD rendering */
    ComPtr<ID3D12PipelineState>     hud_pso;
    ComPtr<ID3D12Resource>          hud_vb[FRAME_COUNT];
    UINT8*                          hud_vb_mapped[FRAME_COUNT];
    size_t                          hud_vb_capacity[FRAME_COUNT];
    ComPtr<ID3D12Resource>          hud_cb[FRAME_COUNT];     /* Ortho MVP for HUD */
    UINT8*                          hud_cb_mapped[FRAME_COUNT];
    uint32_t                        hud_vert_count;
    SdkHotbarSlot                   hotbar[10];
    int                             hotbar_selected;
    bool                            hotbar_dirty;

    /* Health display */
    int                             player_health;
    int                             player_max_health;
    bool                            player_dead;
    bool                            player_invincible;
    int                             player_hunger;
    int                             player_max_hunger;

    /* Day/night lighting */
    ComPtr<ID3D12Resource>          lighting_cb[FRAME_COUNT];
    UINT8*                          lighting_cb_mapped[FRAME_COUNT];
    float                           ambient_level;   /* 0.15–1.0 */
    float                           sky_r, sky_g, sky_b;
    bool                            atmosphere_override;
    float                           atmosphere_ambient;
    float                           atmosphere_sky_r, atmosphere_sky_g, atmosphere_sky_b;
    float                           atmosphere_sun_x, atmosphere_sun_y, atmosphere_sun_z;
    float                           atmosphere_fog_r, atmosphere_fog_g, atmosphere_fog_b;
    float                           atmosphere_fog_density;
    float                           atmosphere_water_r, atmosphere_water_g, atmosphere_water_b;
    float                           atmosphere_water_alpha;
    bool                            atmosphere_camera_submerged;
    float                           atmosphere_waterline_y;
    float                           atmosphere_water_view_depth;
    float                           frame_ms;
    float                           render_ms;
    uint32_t                        perf_visible_chunks;
    uint32_t                        perf_renderable_chunks;
    uint32_t                        perf_drawn_chunks;
    uint32_t                        perf_drawn_subchunks;
    uint32_t                        perf_drawn_verts;
    uint32_t                        perf_visible_full_chunks;
    uint32_t                        perf_visible_far_chunks;
    uint32_t                        perf_visible_proxy_chunks;
    uint32_t                        perf_representation_transitions;
    uint32_t                        perf_missing_mesh_chunks;
    uint32_t                        perf_retired_resource_backlog;
    uint64_t                        perf_upload_bytes;
    uint64_t                        perf_upload_bytes_pending;

    /* Crafting UI */
    SdkCraftingUI                   crafting;
    SdkStationUI                    station;
    SdkSkillsUI                     skills;
    SdkPauseMenuUI                  pause_menu;
    SdkStartMenuUI                  start_menu;
    SdkEditorUI                     editor_ui;
    SdkCommandLineUI                command_line;
    SdkMapUI                        map_ui;
    SdkFluidDebugUI                 fluid_debug;
    SdkPerfDebugUI                  perf_debug;
    SdkSettlementDebugUI            settlement_debug;
    bool                            screenshot_request_pending;
    bool                            screenshot_capture_pending;
    bool                            screenshot_result_pending;
    char                            screenshot_request_path[MAX_PATH];
    SdkScreenshotResult             screenshot_result;
    ComPtr<ID3D12Resource>          screenshot_readback;
    UINT64                          screenshot_readback_size;
    UINT                            screenshot_row_pitch;
    UINT                            screenshot_width;
    UINT                            screenshot_height;
    DXGI_FORMAT                     screenshot_format;

    /* Settings */
    HWND        hwnd;
    uint32_t    width;
    uint32_t    height;
    float       clear_color[4];
    bool        vsync;
    bool        initialized;
    RetiredResource                 retired_resources[RETIRED_RESOURCE_CAPACITY];
    UINT                            retired_resource_count;
};


extern RendererState g_rs;
extern SdkChunkManager* g_chunk_mgr;

#ifdef __cplusplus
extern "C" {
#endif
extern SdkProfiler g_profiler;
#ifdef __cplusplus
}
#endif

inline void set_untextured_vertex(
    BlockVertex* v,
    float x, float y, float z,
    uint32_t col,
    uint32_t normal)
{
    v->position[0] = x;
    v->position[1] = y;
    v->position[2] = z;
    v->color = col;
    v->normal = normal;
    v->uv[0] = 0.0f;
    v->uv[1] = 0.0f;
    v->tex_index = UINT32_MAX;
}

inline UINT renderer_frame_slot(void)
{
    return g_rs.frame_index % FRAME_COUNT;
}

inline ID3D12Resource* renderer_current_constant_buffer(void)
{
    return g_rs.constant_buffer[renderer_frame_slot()].Get();
}

inline UINT8* renderer_current_constant_buffer_mapped(void)
{
    return g_rs.cb_mapped[renderer_frame_slot()];
}

inline ID3D12Resource* renderer_current_hud_buffer(void)
{
    return g_rs.hud_vb[renderer_frame_slot()].Get();
}

inline UINT8* renderer_current_hud_buffer_mapped(void)
{
    return g_rs.hud_vb_mapped[renderer_frame_slot()];
}

inline size_t* renderer_current_hud_buffer_capacity_ptr(void)
{
    return &g_rs.hud_vb_capacity[renderer_frame_slot()];
}

inline ID3D12Resource* renderer_current_hud_cb(void)
{
    return g_rs.hud_cb[renderer_frame_slot()].Get();
}

inline UINT8* renderer_current_hud_cb_mapped(void)
{
    return g_rs.hud_cb_mapped[renderer_frame_slot()];
}

inline ID3D12Resource* renderer_current_lighting_cb(void)
{
    return g_rs.lighting_cb[renderer_frame_slot()].Get();
}

inline UINT8* renderer_current_lighting_cb_mapped(void)
{
    return g_rs.lighting_cb_mapped[renderer_frame_slot()];
}

SdkResult create_render_targets(void);
SdkResult create_depth_buffer(uint32_t width, uint32_t height);
void wait_for_gpu(void);
void retire_resource_later(ID3D12Resource* resource);
void reclaim_retired_resources(void);
SdkResult create_block_texture_array(void);
void build_hud_verts(void);
void synthesize_atmosphere_from_lighting(void);
void write_atmosphere_constant_buffer(void);
void reset_perf_stats(void);
float qpc_ms(const LARGE_INTEGER& start, const LARGE_INTEGER& end);
void release_wall_proxy_cache(void);
bool renderer_record_screenshot_copy(UINT frame_index);
void renderer_finalize_screenshot(UINT64 fence_value);

#endif /* NQLSDK_D3D12_RENDERER_INTERNAL_H */
