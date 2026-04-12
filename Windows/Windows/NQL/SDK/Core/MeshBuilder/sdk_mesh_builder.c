/**
 * sdk_mesh_builder.c — Mesh generation implementation
 *
 * Simple face-culling mesh builder for voxel chunks.
 */
#include "sdk_mesh_builder.h"
#include "../World/Chunks/ChunkManager/sdk_chunk_manager.h"
#include "../World/ConstructionCells/sdk_construction_cells.h"
#include "../World/Simulation/sdk_simulation.h"
#include "../World/Superchunks/Geometry/sdk_superchunk_geometry.h"
#include "../World/Worldgen/sdk_worldgen.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <windows.h>

static __declspec(thread) int g_mesh_worldgen_debug_enabled = 1;
static volatile LONG g_mesh_smooth_lighting_enabled = 1;

#define FAR_PROXY_CELL_STRIDE 4
#define FAR_PROXY_GRID_SIZE ((CHUNK_WIDTH / FAR_PROXY_CELL_STRIDE) + 1)
#define EXPERIMENTAL_FAR_PROXY_CELL_STRIDE 8
#define EXPERIMENTAL_FAR_PROXY_GRID_SIZE ((CHUNK_WIDTH / EXPERIMENTAL_FAR_PROXY_CELL_STRIDE) + 1)
#define FAR_PROXY_BORDER_FLOOR_Y 0.0f

void sdk_mesh_set_thread_worldgen_debug_enabled(int enabled)
{
    g_mesh_worldgen_debug_enabled = enabled ? 1 : 0;
}

void sdk_mesh_set_smooth_lighting_enabled(int enabled)
{
    InterlockedExchange(&g_mesh_smooth_lighting_enabled, enabled ? 1 : 0);
}

/* Face normal vectors for lighting (optional) */
static const float face_normals[6][3] = {
    {-1.0f,  0.0f,  0.0f},  /* NEG_X */
    { 1.0f,  0.0f,  0.0f},  /* POS_X */
    { 0.0f, -1.0f,  0.0f},  /* NEG_Y */
    { 0.0f,  1.0f,  0.0f},  /* POS_Y */
    { 0.0f,  0.0f, -1.0f},  /* NEG_Z */
    { 0.0f,  0.0f,  1.0f},  /* POS_Z */
};

/* Light levels per face - DISABLED for now to show true colors */
static const float face_light[6] = {
    1.0f,  /* NEG_X - full brightness */
    1.0f,  /* POS_X - full brightness */
    1.0f,  /* NEG_Y - full brightness */
    1.0f,  /* POS_Y - full brightness */
    1.0f,  /* NEG_Z - full brightness */
    1.0f,  /* POS_Z - full brightness */
};

static BlockType get_neighbor_block(const SdkChunk* chunk, SdkChunkManager* cm,
                                    int lx, int ly, int lz);

void sdk_mesh_buffer_init(SdkMeshBuffer* buf, uint32_t capacity)
{
    if (!buf) return;
    if (capacity == 0) capacity = MESH_BUFFER_INITIAL_VERTS;
    buf->vertices = (BlockVertex*)malloc(capacity * sizeof(BlockVertex));
    buf->count = 0;
    buf->capacity = capacity;
}

void sdk_mesh_buffer_free(SdkMeshBuffer* buf)
{
    if (!buf) return;
    if (buf->vertices) {
        free(buf->vertices);
        buf->vertices = NULL;
    }
    buf->count = 0;
    buf->capacity = 0;
}

void sdk_mesh_buffer_clear(SdkMeshBuffer* buf)
{
    if (!buf) return;
    buf->count = 0;
}

void sdk_mesh_buffer_add_tri(SdkMeshBuffer* buf, 
    const BlockVertex* v0, const BlockVertex* v1, 
    const BlockVertex* v2)
{
    BlockVertex* grown;
    uint32_t new_capacity;
    if (!buf) return;
    if (!buf->vertices || buf->count + 3 > buf->capacity) {
        new_capacity = (buf->capacity > 0) ? buf->capacity : MESH_BUFFER_INITIAL_VERTS;
        while (buf->count + 3 > new_capacity) {
            if (new_capacity > 0x3fffffffu) return;
            new_capacity *= 2;
        }
        grown = (BlockVertex*)realloc(buf->vertices, new_capacity * sizeof(BlockVertex));
        if (!grown) return;
        buf->vertices = grown;
        buf->capacity = new_capacity;
    }
    
    buf->vertices[buf->count++] = *v0;
    buf->vertices[buf->count++] = *v1;
    buf->vertices[buf->count++] = *v2;
}

void sdk_mesh_buffer_add_quad(SdkMeshBuffer* buf,
    const BlockVertex* v0, const BlockVertex* v1,
    const BlockVertex* v2, const BlockVertex* v3)
{
    sdk_mesh_buffer_add_tri(buf, v0, v1, v2);
    sdk_mesh_buffer_add_tri(buf, v2, v1, v3);
}

void sdk_mesh_get_neighbor_offsets(int face, int* out_dx, int* out_dy, int* out_dz)
{
    switch (face) {
        case FACE_NEG_X: *out_dx = -1; *out_dy = 0; *out_dz = 0; break;
        case FACE_POS_X: *out_dx = 1;  *out_dy = 0; *out_dz = 0; break;
        case FACE_NEG_Y: *out_dx = 0;  *out_dy = -1; *out_dz = 0; break;
        case FACE_POS_Y: *out_dx = 0;  *out_dy = 1;  *out_dz = 0; break;
        case FACE_NEG_Z: *out_dx = 0;  *out_dy = 0; *out_dz = -1; break;
        case FACE_POS_Z: *out_dx = 0;  *out_dy = 0; *out_dz = 1;  break;
        default: *out_dx = *out_dy = *out_dz = 0; break;
    }
}

/* Apply light level to color */
static uint32_t apply_light(uint32_t color, float light)
{
    float rf = ((float)((color >> 0)  & 0xFF)) * light;
    float gf = ((float)((color >> 8)  & 0xFF)) * light;
    float bf = ((float)((color >> 16) & 0xFF)) * light;
    if (rf < 0.0f) rf = 0.0f; else if (rf > 255.0f) rf = 255.0f;
    if (gf < 0.0f) gf = 0.0f; else if (gf > 255.0f) gf = 255.0f;
    if (bf < 0.0f) bf = 0.0f; else if (bf > 255.0f) bf = 255.0f;
    uint8_t r = (uint8_t)rf;
    uint8_t g = (uint8_t)gf;
    uint8_t b = (uint8_t)bf;
    uint8_t a = (uint8_t)((color >> 24) & 0xFF);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

static uint32_t hash3d(int x, int y, int z)
{
    uint32_t h = (uint32_t)x * 0x8da6b343u;
    h ^= (uint32_t)y * 0xd8163841u;
    h ^= (uint32_t)z * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;
    return h;
}

static int block_occludes_ao(BlockType block)
{
    return block != BLOCK_AIR &&
           sdk_block_is_opaque(block) &&
           (sdk_block_get_behavior_flags(block) & SDK_BLOCK_BEHAVIOR_FLUID) == 0u;
}

static float ao_factor_from_neighbors(int side_a, int side_b, int corner)
{
    int occ = (side_a && side_b) ? 3 : (side_a + side_b + corner);
    switch (occ) {
        case 0:  return 1.00f;
        case 1:  return 0.88f;
        case 2:  return 0.74f;
        default: return 0.60f;
    }
}

static void multiply_vertex_color(BlockVertex* v, float factor)
{
    v->color = apply_light(v->color, factor);
}

static void init_face_visuals(BlockVertex* v, int vertex_count, int face,
                              BlockType block_type, float world_x, float world_y, float world_z)
{
    uint32_t debug_color = g_mesh_worldgen_debug_enabled
        ? sdk_worldgen_get_debug_color((int)world_x, (int)world_y, (int)world_z, block_type)
        : 0u;
    uint32_t base_color = debug_color ? debug_color : 0xFFFFFFFFu;
    float shade = face_light[face];
    uint32_t tex_index = debug_color ? UINT32_MAX : ((uint32_t)block_type * 6u + (uint32_t)face);
    int i;

    if (block_type == BLOCK_LEAVES && debug_color == 0u) {
        uint32_t h = hash3d((int)world_x, (int)world_y, (int)world_z);
        float tint = 0.82f + ((float)(h & 0xFF) / 255.0f) * 0.18f;
        shade *= tint;
    }

    for (i = 0; i < vertex_count; ++i) {
        v[i].color = apply_light(base_color, shade);
        v[i].normal = (uint32_t)face;
        v[i].tex_index = tex_index;
    }
}

static void emit_face_quad(SdkMeshBuffer* buf, BlockVertex* v)
{
    sdk_mesh_buffer_add_tri(buf, &v[0], &v[1], &v[2]);
    sdk_mesh_buffer_add_tri(buf, &v[2], &v[1], &v[3]);
}

static int ao_neighbor_solid(const SdkChunk* chunk, SdkChunkManager* cm, int lx, int ly, int lz)
{
    return block_occludes_ao(get_neighbor_block(chunk, cm, lx, ly, lz));
}

static void apply_face_ao(BlockVertex* v, const SdkChunk* chunk, SdkChunkManager* cm,
                          int lx, int ly, int lz, int face)
{
    int s1[4] = {0};
    int s2[4] = {0};
    int c[4] = {0};
    int i;

    if (InterlockedCompareExchange(&g_mesh_smooth_lighting_enabled, 0, 0) == 0) {
        return;
    }

    switch (face) {
        case FACE_POS_Y:
            s1[0] = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz);
            s2[0] = ao_neighbor_solid(chunk, cm, lx, ly, lz - 1);
            c[0]  = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz - 1);
            s1[1] = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz);
            s2[1] = ao_neighbor_solid(chunk, cm, lx, ly, lz - 1);
            c[1]  = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz - 1);
            s1[2] = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz);
            s2[2] = ao_neighbor_solid(chunk, cm, lx, ly, lz + 1);
            c[2]  = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz + 1);
            s1[3] = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz);
            s2[3] = ao_neighbor_solid(chunk, cm, lx, ly, lz + 1);
            c[3]  = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz + 1);
            break;
        case FACE_NEG_Y:
            s1[0] = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz);
            s2[0] = ao_neighbor_solid(chunk, cm, lx, ly, lz - 1);
            c[0]  = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz - 1);
            s1[1] = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz);
            s2[1] = ao_neighbor_solid(chunk, cm, lx, ly, lz - 1);
            c[1]  = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz - 1);
            s1[2] = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz + 1);
            s2[2] = ao_neighbor_solid(chunk, cm, lx, ly, lz + 1);
            c[2]  = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz + 1);
            s1[3] = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz);
            s2[3] = ao_neighbor_solid(chunk, cm, lx, ly, lz + 1);
            c[3]  = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz + 1);
            break;
        case FACE_NEG_X:
        case FACE_POS_X:
            s1[0] = ao_neighbor_solid(chunk, cm, lx, ly + 1, lz);
            s2[0] = ao_neighbor_solid(chunk, cm, lx, ly, lz + 1);
            c[0]  = ao_neighbor_solid(chunk, cm, lx, ly + 1, lz + 1);
            s1[1] = ao_neighbor_solid(chunk, cm, lx, ly + 1, lz);
            s2[1] = ao_neighbor_solid(chunk, cm, lx, ly, lz - 1);
            c[1]  = ao_neighbor_solid(chunk, cm, lx, ly + 1, lz - 1);
            s1[2] = ao_neighbor_solid(chunk, cm, lx, ly - 1, lz);
            s2[2] = ao_neighbor_solid(chunk, cm, lx, ly, lz + 1);
            c[2]  = ao_neighbor_solid(chunk, cm, lx, ly - 1, lz + 1);
            s1[3] = ao_neighbor_solid(chunk, cm, lx, ly - 1, lz);
            s2[3] = ao_neighbor_solid(chunk, cm, lx, ly, lz - 1);
            c[3]  = ao_neighbor_solid(chunk, cm, lx, ly - 1, lz - 1);
            break;
        case FACE_NEG_Z:
        case FACE_POS_Z:
            s1[0] = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz);
            s2[0] = ao_neighbor_solid(chunk, cm, lx, ly + 1, lz);
            c[0]  = ao_neighbor_solid(chunk, cm, lx + 1, ly + 1, lz);
            s1[1] = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz);
            s2[1] = ao_neighbor_solid(chunk, cm, lx, ly + 1, lz);
            c[1]  = ao_neighbor_solid(chunk, cm, lx - 1, ly + 1, lz);
            s1[2] = ao_neighbor_solid(chunk, cm, lx + 1, ly, lz);
            s2[2] = ao_neighbor_solid(chunk, cm, lx, ly - 1, lz);
            c[2]  = ao_neighbor_solid(chunk, cm, lx + 1, ly - 1, lz);
            s1[3] = ao_neighbor_solid(chunk, cm, lx - 1, ly, lz);
            s2[3] = ao_neighbor_solid(chunk, cm, lx, ly - 1, lz);
            c[3]  = ao_neighbor_solid(chunk, cm, lx - 1, ly - 1, lz);
            break;
        default:
            return;
    }

    for (i = 0; i < 4; ++i) {
        multiply_vertex_color(&v[i], ao_factor_from_neighbors(s1[i], s2[i], c[i]));
    }
}

/* Add a face as 2 triangles at the given block position */
static void add_face(SdkMeshBuffer* buf, const SdkChunk* chunk, SdkChunkManager* cm,
                     int lx, int ly, int lz, int face, BlockType block_type,
                     float world_x, float world_y, float world_z)
{
    float x = (float)world_x;
    float y = (float)world_y;
    float z = (float)world_z;
    
    BlockVertex v[4];
    init_face_visuals(v, 4, face, block_type, world_x, world_y, world_z);
    
    /* Generate quad vertices based on face direction */
    switch (face) {
        case FACE_NEG_X: /* Left face, facing -X */
            v[0].position[0] = x;     v[0].position[1] = y + 1.0f; v[0].position[2] = z + 1.0f;
            v[1].position[0] = x;     v[1].position[1] = y + 1.0f; v[1].position[2] = z;
            v[2].position[0] = x;     v[2].position[1] = y;     v[2].position[2] = z + 1.0f;
            v[3].position[0] = x;     v[3].position[1] = y;     v[3].position[2] = z;
            break;
        case FACE_POS_X: /* Right face, facing +X */
            v[0].position[0] = x + 1.0f; v[0].position[1] = y + 1.0f; v[0].position[2] = z;
            v[1].position[0] = x + 1.0f; v[1].position[1] = y + 1.0f; v[1].position[2] = z + 1.0f;
            v[2].position[0] = x + 1.0f; v[2].position[1] = y;     v[2].position[2] = z;
            v[3].position[0] = x + 1.0f; v[3].position[1] = y;     v[3].position[2] = z + 1.0f;
            break;
        case FACE_NEG_Y: /* Bottom face, facing -Y */
            v[0].position[0] = x + 1.0f; v[0].position[1] = y; v[0].position[2] = z;
            v[1].position[0] = x;     v[1].position[1] = y; v[1].position[2] = z;
            v[2].position[0] = x + 1.0f; v[2].position[1] = y; v[2].position[2] = z + 1.0f;
            v[3].position[0] = x;     v[3].position[1] = y; v[3].position[2] = z + 1.0f;
            break;
        case FACE_POS_Y: /* Top face, facing +Y */
            v[0].position[0] = x;     v[0].position[1] = y + 1.0f; v[0].position[2] = z;
            v[1].position[0] = x + 1.0f; v[1].position[1] = y + 1.0f; v[1].position[2] = z;
            v[2].position[0] = x;     v[2].position[1] = y + 1.0f; v[2].position[2] = z + 1.0f;
            v[3].position[0] = x + 1.0f; v[3].position[1] = y + 1.0f; v[3].position[2] = z + 1.0f;
            break;
        case FACE_NEG_Z: /* Back face, facing -Z */
            v[0].position[0] = x + 1.0f; v[0].position[1] = y + 1.0f; v[0].position[2] = z;
            v[1].position[0] = x;     v[1].position[1] = y + 1.0f; v[1].position[2] = z;
            v[2].position[0] = x + 1.0f; v[2].position[1] = y;     v[2].position[2] = z;
            v[3].position[0] = x;     v[3].position[1] = y;     v[3].position[2] = z;
            break;
        case FACE_POS_Z: /* Front face, facing +Z */
            v[0].position[0] = x;     v[0].position[1] = y + 1.0f; v[0].position[2] = z + 1.0f;
            v[1].position[0] = x + 1.0f; v[1].position[1] = y + 1.0f; v[1].position[2] = z + 1.0f;
            v[2].position[0] = x;     v[2].position[1] = y;     v[2].position[2] = z + 1.0f;
            v[3].position[0] = x + 1.0f; v[3].position[1] = y;     v[3].position[2] = z + 1.0f;
            break;
    }

    v[0].uv[0] = 0.0f; v[0].uv[1] = 0.0f;
    v[1].uv[0] = 1.0f; v[1].uv[1] = 0.0f;
    v[2].uv[0] = 0.0f; v[2].uv[1] = 1.0f;
    v[3].uv[0] = 1.0f; v[3].uv[1] = 1.0f;
    apply_face_ao(v, chunk, cm, lx, ly, lz, face);
    
    /* Emit 2 triangles per face for TRIANGLELIST topology:
     * Tri 0: v0, v1, v2
     * Tri 1: v2, v1, v3 */
    emit_face_quad(buf, v);
}

static void add_box_face_bounds(SdkMeshBuffer* buf,
                                int face,
                                BlockType block_type,
                                float min_x, float min_y, float min_z,
                                float max_x, float max_y, float max_z)
{
    BlockVertex v[4];
    init_face_visuals(v, 4, face, block_type, min_x, min_y, min_z);

    switch (face) {
        case FACE_NEG_X:
            v[0].position[0] = min_x; v[0].position[1] = max_y; v[0].position[2] = max_z;
            v[1].position[0] = min_x; v[1].position[1] = max_y; v[1].position[2] = min_z;
            v[2].position[0] = min_x; v[2].position[1] = min_y; v[2].position[2] = max_z;
            v[3].position[0] = min_x; v[3].position[1] = min_y; v[3].position[2] = min_z;
            break;
        case FACE_POS_X:
            v[0].position[0] = max_x; v[0].position[1] = max_y; v[0].position[2] = min_z;
            v[1].position[0] = max_x; v[1].position[1] = max_y; v[1].position[2] = max_z;
            v[2].position[0] = max_x; v[2].position[1] = min_y; v[2].position[2] = min_z;
            v[3].position[0] = max_x; v[3].position[1] = min_y; v[3].position[2] = max_z;
            break;
        case FACE_NEG_Y:
            v[0].position[0] = max_x; v[0].position[1] = min_y; v[0].position[2] = min_z;
            v[1].position[0] = min_x; v[1].position[1] = min_y; v[1].position[2] = min_z;
            v[2].position[0] = max_x; v[2].position[1] = min_y; v[2].position[2] = max_z;
            v[3].position[0] = min_x; v[3].position[1] = min_y; v[3].position[2] = max_z;
            break;
        case FACE_POS_Y:
            v[0].position[0] = min_x; v[0].position[1] = max_y; v[0].position[2] = min_z;
            v[1].position[0] = max_x; v[1].position[1] = max_y; v[1].position[2] = min_z;
            v[2].position[0] = min_x; v[2].position[1] = max_y; v[2].position[2] = max_z;
            v[3].position[0] = max_x; v[3].position[1] = max_y; v[3].position[2] = max_z;
            break;
        case FACE_NEG_Z:
            v[0].position[0] = max_x; v[0].position[1] = max_y; v[0].position[2] = min_z;
            v[1].position[0] = min_x; v[1].position[1] = max_y; v[1].position[2] = min_z;
            v[2].position[0] = max_x; v[2].position[1] = min_y; v[2].position[2] = min_z;
            v[3].position[0] = min_x; v[3].position[1] = min_y; v[3].position[2] = min_z;
            break;
        case FACE_POS_Z:
            v[0].position[0] = min_x; v[0].position[1] = max_y; v[0].position[2] = max_z;
            v[1].position[0] = max_x; v[1].position[1] = max_y; v[1].position[2] = max_z;
            v[2].position[0] = min_x; v[2].position[1] = min_y; v[2].position[2] = max_z;
            v[3].position[0] = max_x; v[3].position[1] = min_y; v[3].position[2] = max_z;
            break;
        default:
            return;
    }

    v[0].uv[0] = 0.0f; v[0].uv[1] = 0.0f;
    v[1].uv[0] = 1.0f; v[1].uv[1] = 0.0f;
    v[2].uv[0] = 0.0f; v[2].uv[1] = 1.0f;
    v[3].uv[0] = 1.0f; v[3].uv[1] = 1.0f;
    emit_face_quad(buf, v);
}

static void emit_construction_box(SdkMeshBuffer* buf,
                                  BlockType block_type,
                                  float min_x, float min_y, float min_z,
                                  float max_x, float max_y, float max_z)
{
    int face;

    if (!buf || block_type == BLOCK_AIR) return;
    if (max_x <= min_x || max_y <= min_y || max_z <= min_z) return;
    for (face = 0; face < 6; ++face) {
        add_box_face_bounds(buf, face, block_type, min_x, min_y, min_z, max_x, max_y, max_z);
    }
}

static void emit_overflow_construction_mesh(SdkMeshBuffer* buf,
                                            int wx, int wy, int wz,
                                            const SdkConstructionWorkspace* workspace)
{
    int x;
    int y;
    int z;

    if (!buf || !workspace) return;

    for (y = 0; y < 16; ++y) {
        for (z = 0; z < 16; ++z) {
            for (x = 0; x < 16; ++x) {
                BlockType material;
                float min_x;
                float min_y;
                float min_z;
                float max_x;
                float max_y;
                float max_z;

                material = (BlockType)workspace->voxels[y * 256 + z * 16 + x];
                if (material == BLOCK_AIR) continue;
                min_x = (float)wx + (float)x / 16.0f;
                min_y = (float)wy + (float)y / 16.0f;
                min_z = (float)wz + (float)z / 16.0f;
                max_x = (float)wx + (float)(x + 1) / 16.0f;
                max_y = (float)wy + (float)(y + 1) / 16.0f;
                max_z = (float)wz + (float)(z + 1) / 16.0f;

                if (x == 0 || workspace->voxels[y * 256 + z * 16 + (x - 1)] == BLOCK_AIR) {
                    add_box_face_bounds(buf, FACE_NEG_X, material, min_x, min_y, min_z, max_x, max_y, max_z);
                }
                if (x == 15 || workspace->voxels[y * 256 + z * 16 + (x + 1)] == BLOCK_AIR) {
                    add_box_face_bounds(buf, FACE_POS_X, material, min_x, min_y, min_z, max_x, max_y, max_z);
                }
                if (y == 0 || workspace->voxels[(y - 1) * 256 + z * 16 + x] == BLOCK_AIR) {
                    add_box_face_bounds(buf, FACE_NEG_Y, material, min_x, min_y, min_z, max_x, max_y, max_z);
                }
                if (y == 15 || workspace->voxels[(y + 1) * 256 + z * 16 + x] == BLOCK_AIR) {
                    add_box_face_bounds(buf, FACE_POS_Y, material, min_x, min_y, min_z, max_x, max_y, max_z);
                }
                if (z == 0 || workspace->voxels[y * 256 + (z - 1) * 16 + x] == BLOCK_AIR) {
                    add_box_face_bounds(buf, FACE_NEG_Z, material, min_x, min_y, min_z, max_x, max_y, max_z);
                }
                if (z == 15 || workspace->voxels[y * 256 + (z + 1) * 16 + x] == BLOCK_AIR) {
                    add_box_face_bounds(buf, FACE_POS_Z, material, min_x, min_y, min_z, max_x, max_y, max_z);
                }
            }
        }
    }
}

static BlockType get_neighbor_block(const SdkChunk* chunk, SdkChunkManager* cm,
                                    int lx, int ly, int lz)
{
    if (ly < 0 || ly >= CHUNK_HEIGHT) return BLOCK_AIR;

    if (lx >= 0 && lx < CHUNK_WIDTH &&
        lz >= 0 && lz < CHUNK_DEPTH) {
        return sdk_construction_chunk_get_display_material(chunk, lx, ly, lz);
    }

    if (!cm) return BLOCK_AIR;

    {
        int wx = sdk_chunk_to_world_x(lx, chunk->cx);
        int wz = sdk_chunk_to_world_z(lz, chunk->cz);
        int ncx = sdk_world_to_chunk_x(wx);
        int ncz = sdk_world_to_chunk_z(wz);
        SdkChunk* neighbor = sdk_chunk_manager_get_chunk(cm, ncx, ncz);
        if (!neighbor) return BLOCK_AIR;
        return sdk_construction_chunk_get_display_material(neighbor,
                                                           sdk_world_to_local_x(wx, ncx),
                                                           ly,
                                                           sdk_world_to_local_z(wz, ncz));
    }
}

static const SdkChunk* get_chunk_for_local_neighbor(const SdkChunk* chunk, SdkChunkManager* cm,
                                                    int* io_lx, int ly, int* io_lz)
{
    if (!chunk || ly < 0 || ly >= CHUNK_HEIGHT) return NULL;

    if (*io_lx >= 0 && *io_lx < CHUNK_WIDTH &&
        *io_lz >= 0 && *io_lz < CHUNK_DEPTH) {
        return chunk;
    }

    if (!cm) return NULL;

    {
        int wx = sdk_chunk_to_world_x(*io_lx, chunk->cx);
        int wz = sdk_chunk_to_world_z(*io_lz, chunk->cz);
        int ncx = sdk_world_to_chunk_x(wx);
        int ncz = sdk_world_to_chunk_z(wz);
        SdkChunk* neighbor = sdk_chunk_manager_get_chunk(cm, ncx, ncz);
        if (!neighbor) return NULL;
        *io_lx = sdk_world_to_local_x(wx, ncx);
        *io_lz = sdk_world_to_local_z(wz, ncz);
        return neighbor;
    }
}

static uint8_t get_neighbor_fluid_fill(const SdkChunk* chunk, SdkChunkManager* cm,
                                       int lx, int ly, int lz, BlockType* out_block, int* out_known)
{
    const SdkChunk* owner;

    if (out_block) *out_block = BLOCK_AIR;
    if (out_known) *out_known = 0;
    owner = get_chunk_for_local_neighbor(chunk, cm, &lx, ly, &lz);
    if (!owner) return 0u;
    if (out_known) *out_known = 1;
    if (out_block) *out_block = sdk_construction_chunk_get_display_material(owner, lx, ly, lz);
    return sdk_simulation_get_fluid_fill(owner, lx, ly, lz);
}

static void add_fluid_top_face(SdkMeshBuffer* buf, int face, BlockType block_type,
                               float x, float y, float z, float top_height)
{
    BlockVertex v[4];
    init_face_visuals(v, 4, face, block_type, x, y, z);

    v[0].position[0] = x;        v[0].position[1] = y + top_height; v[0].position[2] = z;
    v[1].position[0] = x + 1.0f; v[1].position[1] = y + top_height; v[1].position[2] = z;
    v[2].position[0] = x;        v[2].position[1] = y + top_height; v[2].position[2] = z + 1.0f;
    v[3].position[0] = x + 1.0f; v[3].position[1] = y + top_height; v[3].position[2] = z + 1.0f;

    v[0].uv[0] = 0.0f; v[0].uv[1] = 0.0f;
    v[1].uv[0] = 1.0f; v[1].uv[1] = 0.0f;
    v[2].uv[0] = 0.0f; v[2].uv[1] = 1.0f;
    v[3].uv[0] = 1.0f; v[3].uv[1] = 1.0f;
    emit_face_quad(buf, v);
}

static void add_fluid_side_face(SdkMeshBuffer* buf, int face, BlockType block_type,
                                float x, float y, float z, float bottom_height, float top_height)
{
    BlockVertex v[4];
    if (top_height <= bottom_height + 0.001f) return;
    init_face_visuals(v, 4, face, block_type, x, y, z);

    switch (face) {
        case FACE_NEG_X:
            v[0].position[0] = x; v[0].position[1] = y + top_height;    v[0].position[2] = z + 1.0f;
            v[1].position[0] = x; v[1].position[1] = y + top_height;    v[1].position[2] = z;
            v[2].position[0] = x; v[2].position[1] = y + bottom_height; v[2].position[2] = z + 1.0f;
            v[3].position[0] = x; v[3].position[1] = y + bottom_height; v[3].position[2] = z;
            break;
        case FACE_POS_X:
            v[0].position[0] = x + 1.0f; v[0].position[1] = y + top_height;    v[0].position[2] = z;
            v[1].position[0] = x + 1.0f; v[1].position[1] = y + top_height;    v[1].position[2] = z + 1.0f;
            v[2].position[0] = x + 1.0f; v[2].position[1] = y + bottom_height; v[2].position[2] = z;
            v[3].position[0] = x + 1.0f; v[3].position[1] = y + bottom_height; v[3].position[2] = z + 1.0f;
            break;
        case FACE_NEG_Z:
            v[0].position[0] = x + 1.0f; v[0].position[1] = y + top_height;    v[0].position[2] = z;
            v[1].position[0] = x;        v[1].position[1] = y + top_height;    v[1].position[2] = z;
            v[2].position[0] = x + 1.0f; v[2].position[1] = y + bottom_height; v[2].position[2] = z;
            v[3].position[0] = x;        v[3].position[1] = y + bottom_height; v[3].position[2] = z;
            break;
        case FACE_POS_Z:
            v[0].position[0] = x;        v[0].position[1] = y + top_height;    v[0].position[2] = z + 1.0f;
            v[1].position[0] = x + 1.0f; v[1].position[1] = y + top_height;    v[1].position[2] = z + 1.0f;
            v[2].position[0] = x;        v[2].position[1] = y + bottom_height; v[2].position[2] = z + 1.0f;
            v[3].position[0] = x + 1.0f; v[3].position[1] = y + bottom_height; v[3].position[2] = z + 1.0f;
            break;
        default:
            return;
    }

    v[0].uv[0] = 0.0f; v[0].uv[1] = 0.0f;
    v[1].uv[0] = 1.0f; v[1].uv[1] = 0.0f;
    v[2].uv[0] = 0.0f; v[2].uv[1] = 1.0f;
    v[3].uv[0] = 1.0f; v[3].uv[1] = 1.0f;
    emit_face_quad(buf, v);
}

static void emit_fluid_faces(SdkMeshBuffer* buf, const SdkChunk* chunk, SdkChunkManager* cm,
                             int lx, int ly, int lz, BlockType block_type,
                             float world_x, float world_y, float world_z)
{
    BlockType neighbor_block;
    int neighbor_known = 0;
    uint8_t fill = sdk_simulation_get_fluid_fill(chunk, lx, ly, lz);
    float top_height;

    if (fill == 0u) return;
    top_height = (float)fill / 255.0f;
    if (top_height < 0.01f) return;

    /* Render top face (water surface) */
    {
        uint8_t neighbor_fill = get_neighbor_fluid_fill(chunk, cm, lx, ly + 1, lz, &neighbor_block, &neighbor_known);
        int neighbor_has_fluid = neighbor_known &&
                                 (sdk_block_get_behavior_flags(neighbor_block) & SDK_BLOCK_BEHAVIOR_FLUID) != 0u &&
                                 neighbor_fill > 0u;
        if (!neighbor_has_fluid && neighbor_known) {
            add_fluid_top_face(buf, FACE_POS_Y, block_type, world_x, world_y, world_z, top_height);
        }
    }

    /* Render side faces for external visibility */
    {
        static const int faces[4] = { FACE_NEG_X, FACE_POS_X, FACE_NEG_Z, FACE_POS_Z };
        static const int dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        int i;
        for (i = 0; i < 4; ++i) {
            uint8_t neighbor_fill = get_neighbor_fluid_fill(chunk, cm, lx + dirs[i][0], ly, lz + dirs[i][1],
                                                            &neighbor_block, &neighbor_known);
            float bottom_height = 0.0f;
            if (!neighbor_known) {
                continue;
            }
            if ((sdk_block_get_behavior_flags(neighbor_block) & SDK_BLOCK_BEHAVIOR_FLUID) != 0u && neighbor_fill > 0u) {
                bottom_height = (float)neighbor_fill / 255.0f;
            }
            if (top_height > bottom_height + 0.001f) {
                add_fluid_side_face(buf, faces[i], block_type, world_x, world_y, world_z, bottom_height, top_height);
            }
        }
    }
}

static int should_emit_face(BlockType block, BlockType neighbor)
{
    const SdkBlockDef* block_def;
    const SdkBlockDef* neighbor_def;

    if (block == BLOCK_AIR) return 0;
    if (neighbor == BLOCK_AIR) return 1;
    if (neighbor == block && sdk_block_is_full(block)) {
        return 0;
    }
    if (sdk_block_is_opaque(neighbor)) {
        return 0;
    }

    block_def = sdk_block_get_def(block);
    neighbor_def = sdk_block_get_def(neighbor);

    /* Cull interior fluid-fluid faces; they explode ocean mesh sizes. */
    if ((block_def->flags & SDK_BLOCK_FLAG_FLUID) != 0u &&
        (neighbor_def->flags & SDK_BLOCK_FLAG_FLUID) != 0u) {
        return 0;
    }

    return 1;
}

static void clear_subchunk_mesh(SdkChunkSubmesh* sub)
{
    if (!sub) return;
    if (sub->cpu_vertices) {
        free(sub->cpu_vertices);
        sub->cpu_vertices = NULL;
    }
    sub->vertex_count = 0;
    sub->vertex_capacity = 0;
    memset(sub->bounds_min, 0, sizeof(sub->bounds_min));
    memset(sub->bounds_max, 0, sizeof(sub->bounds_max));
    sub->empty = true;
    sub->dirty = false;
    sub->upload_dirty = true;
}

static void store_subchunk_mesh(SdkChunkSubmesh* sub, const SdkMeshBuffer* output)
{
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;
    uint32_t i;

    if (!sub || !output) return;

    if (output->count == 0) {
        clear_subchunk_mesh(sub);
        return;
    }

    min_x = FLT_MAX;
    min_y = FLT_MAX;
    min_z = FLT_MAX;
    max_x = -FLT_MAX;
    max_y = -FLT_MAX;
    max_z = -FLT_MAX;

    for (i = 0; i < output->count; ++i) {
        const BlockVertex* v = &output->vertices[i];
        if (v->position[0] < min_x) min_x = v->position[0];
        if (v->position[1] < min_y) min_y = v->position[1];
        if (v->position[2] < min_z) min_z = v->position[2];
        if (v->position[0] > max_x) max_x = v->position[0];
        if (v->position[1] > max_y) max_y = v->position[1];
        if (v->position[2] > max_z) max_z = v->position[2];
    }

    sub->bounds_min[0] = min_x;
    sub->bounds_min[1] = min_y;
    sub->bounds_min[2] = min_z;
    sub->bounds_max[0] = max_x;
    sub->bounds_max[1] = max_y;
    sub->bounds_max[2] = max_z;

    if (sub->cpu_vertices && sub->vertex_capacity < output->count) {
        free(sub->cpu_vertices);
        sub->cpu_vertices = NULL;
    }
    if (!sub->cpu_vertices) {
        sub->cpu_vertices = (BlockVertex*)malloc(output->count * sizeof(BlockVertex));
        if (!sub->cpu_vertices) {
            clear_subchunk_mesh(sub);
            return;
        }
        sub->vertex_capacity = output->count;
    }

    memcpy(sub->cpu_vertices, output->vertices, output->count * sizeof(BlockVertex));
    sub->vertex_count = output->count;
    sub->empty = false;
    sub->dirty = false;
    sub->upload_dirty = true;
}

static void set_proxy_vertex(BlockVertex* v, float x, float y, float z, uint32_t color, uint32_t face)
{
    if (!v) return;
    v->position[0] = x;
    v->position[1] = y;
    v->position[2] = z;
    v->color = color;
    v->normal = face;
    v->uv[0] = 0.0f;
    v->uv[1] = 0.0f;
    v->tex_index = UINT32_MAX;
}

static void set_proxy_textured_vertex(BlockVertex* v,
                                      float x, float y, float z,
                                      uint32_t color, uint32_t face,
                                      float u, float vv, uint32_t tex_index)
{
    if (!v) return;
    v->position[0] = x;
    v->position[1] = y;
    v->position[2] = z;
    v->color = color;
    v->normal = face;
    v->uv[0] = u;
    v->uv[1] = vv;
    v->tex_index = tex_index;
}

static uint32_t proxy_face_tex_index(BlockType block, uint32_t face)
{
    if (block == BLOCK_AIR) return UINT32_MAX;
    return ((uint32_t)block * 6u) + face;
}

static int proxy_floor_div_i(int value, int denom)
{
    if (denom <= 0) return 0;
    if (value >= 0) return value / denom;
    return -(((-value) + denom - 1) / denom);
}

static int block_excluded_from_far_mesh(BlockType block)
{
    return sdk_chunk_block_excluded_from_far_mesh(block);
}

static int chunk_local_superchunk_x(const SdkChunk* chunk)
{
    SdkSuperchunkCell cell;

    if (!chunk) return 0;
    sdk_superchunk_cell_from_chunk(chunk->cx, chunk->cz, &cell);
    return chunk->cx - cell.interior_min_cx;
}

static int chunk_local_superchunk_z(const SdkChunk* chunk)
{
    SdkSuperchunkCell cell;

    if (!chunk) return 0;
    sdk_superchunk_cell_from_chunk(chunk->cx, chunk->cz, &cell);
    return chunk->cz - cell.interior_min_cz;
}

static int chunk_is_superchunk_west_edge(const SdkChunk* chunk)
{
    return chunk_local_superchunk_x(chunk) == 0;
}

static int chunk_is_superchunk_north_edge(const SdkChunk* chunk)
{
    return chunk_local_superchunk_z(chunk) == 0;
}

static int chunk_is_superchunk_east_edge(const SdkChunk* chunk)
{
    return chunk_local_superchunk_x(chunk) == (SDK_SUPERCHUNK_CHUNK_SPAN - 1);
}

static int chunk_is_superchunk_south_edge(const SdkChunk* chunk)
{
    return chunk_local_superchunk_z(chunk) == (SDK_SUPERCHUNK_CHUNK_SPAN - 1);
}

static int chunk_is_superchunk_wall_chunk_exact(const SdkChunk* chunk)
{
    uint8_t wall_mask = 0u;
    int period_local_x = 0;
    int period_local_z = 0;

    if (!chunk) return 0;

    if (!sdk_superchunk_get_canonical_wall_chunk_owner(chunk->cx,
                                                       chunk->cz,
                                                       &wall_mask,
                                                       NULL,
                                                       NULL,
                                                       &period_local_x,
                                                       &period_local_z)) {
        return 0;
    }

    if ((wall_mask & SDK_SUPERCHUNK_WALL_FACE_WEST) != 0u &&
        period_local_z >= SDK_SUPERCHUNK_GATE_SUPPORT_START_CHUNK &&
        period_local_z <= SDK_SUPERCHUNK_GATE_SUPPORT_END_CHUNK) {
        return 1;
    }
    if ((wall_mask & SDK_SUPERCHUNK_WALL_FACE_NORTH) != 0u &&
        period_local_x >= SDK_SUPERCHUNK_GATE_SUPPORT_START_CHUNK &&
        period_local_x <= SDK_SUPERCHUNK_GATE_SUPPORT_END_CHUNK) {
        return 1;
    }
    return 0;
}

static int block_is_superchunk_perimeter_exact(const SdkChunk* chunk, int lx, int lz)
{
    if (!chunk) return 0;
    if (chunk_is_superchunk_west_edge(chunk) && lx <= 0) return 1;
    if (chunk_is_superchunk_north_edge(chunk) && lz <= 0) return 1;
    if (chunk_is_superchunk_east_edge(chunk) && lx >= (CHUNK_WIDTH - 1)) return 1;
    if (chunk_is_superchunk_south_edge(chunk) && lz >= (CHUNK_DEPTH - 1)) return 1;
    return 0;
}

static int block_should_stay_exact(const SdkChunk* chunk, int lx, int lz, BlockType block)
{
    if (chunk_is_superchunk_wall_chunk_exact(chunk)) {
        return block != BLOCK_AIR;
    }
    return sdk_chunk_block_excluded_from_far_mesh(block) ||
           block_is_superchunk_perimeter_exact(chunk, lx, lz);
}

static void adjust_far_proxy_sample_coords(const SdkChunk* chunk, int* inout_lx, int* inout_lz)
{
    if (!chunk || !inout_lx || !inout_lz) return;

    if (chunk_is_superchunk_west_edge(chunk) && *inout_lx <= 0) {
        *inout_lx = 1;
    }
    if (chunk_is_superchunk_north_edge(chunk) && *inout_lz <= 0) {
        *inout_lz = 1;
    }
    if (chunk_is_superchunk_east_edge(chunk) && *inout_lx >= CHUNK_WIDTH) {
        *inout_lx = CHUNK_WIDTH - 2;
    }
    if (chunk_is_superchunk_south_edge(chunk) && *inout_lz >= CHUNK_DEPTH) {
        *inout_lz = CHUNK_DEPTH - 2;
    }
}

static void trimmed_far_proxy_cell_bounds(const SdkChunk* chunk,
                                          int gx, int gz, int grid_size, int stride,
                                          float origin_x, float origin_z,
                                          float* out_x0, float* out_z0,
                                          float* out_x1, float* out_z1)
{
    float x0;
    float z0;
    float x1;
    float z1;

    if (!out_x0 || !out_z0 || !out_x1 || !out_z1) return;

    x0 = origin_x + (float)(gx * stride);
    z0 = origin_z + (float)(gz * stride);
    x1 = origin_x + (float)((gx + 1) * stride);
    z1 = origin_z + (float)((gz + 1) * stride);

    if (chunk_is_superchunk_west_edge(chunk) && gx == 0) x0 += 1.0f;
    if (chunk_is_superchunk_north_edge(chunk) && gz == 0) z0 += 1.0f;
    if (chunk_is_superchunk_east_edge(chunk) && gx == (grid_size - 2)) x1 -= 1.0f;
    if (chunk_is_superchunk_south_edge(chunk) && gz == (grid_size - 2)) z1 -= 1.0f;

    *out_x0 = x0;
    *out_z0 = z0;
    *out_x1 = x1;
    *out_z1 = z1;
}

static int resolve_proxy_sample_owner(const SdkChunk* chunk, SdkChunkManager* cm,
                                      int* io_lx, int* io_lz,
                                      const SdkChunk** out_owner)
{
    const SdkChunk* owner = NULL;
    int lx;
    int lz;

    if (!chunk || !io_lx || !io_lz || !out_owner) return 0;

    lx = *io_lx;
    lz = *io_lz;
    if (lx < 0) lx = 0;
    if (lz < 0) lz = 0;
    if (lx > CHUNK_WIDTH) lx = CHUNK_WIDTH;
    if (lz > CHUNK_DEPTH) lz = CHUNK_DEPTH;

    if (lx == CHUNK_WIDTH || lz == CHUNK_DEPTH) {
        if (cm) {
            owner = get_chunk_for_local_neighbor(chunk, cm, &lx, 0, &lz);
        }
        if (!owner) {
            owner = chunk;
            if (lx >= CHUNK_WIDTH) lx = CHUNK_WIDTH - 1;
            if (lz >= CHUNK_DEPTH) lz = CHUNK_DEPTH - 1;
        }
    } else {
        owner = chunk;
    }

    *io_lx = lx;
    *io_lz = lz;
    *out_owner = owner;
    return owner && owner->blocks;
}

static int sample_far_surface_point(const SdkChunk* chunk, SdkChunkManager* cm,
                                    int sample_lx, int sample_lz,
                                    float* out_height, uint32_t* out_color, BlockType* out_block)
{
    const SdkChunk* owner = NULL;
    int lx = sample_lx;
    int lz = sample_lz;
    int ly;

    if (out_height) *out_height = 0.0f;
    if (out_color) *out_color = 0x00000000u;
    if (out_block) *out_block = BLOCK_AIR;
    if (!chunk) return 0;

    if (!resolve_proxy_sample_owner(chunk, cm, &lx, &lz, &owner)) {
        return 0;
    }

    for (ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
        BlockType block = sdk_construction_chunk_get_display_material(owner, lx, ly, lz);
        uint32_t flags;

        if (block == BLOCK_AIR) continue;
        if (block_should_stay_exact(owner, lx, lz, block)) continue;
        flags = sdk_block_get_behavior_flags(block);
        if ((flags & SDK_BLOCK_BEHAVIOR_FLUID) != 0u) {
            continue;
        }
        if (!sdk_block_is_solid(block)) continue;
        if (out_height) *out_height = (float)ly + 1.0f;
        if (out_color) *out_color = sdk_block_get_face_color(block, FACE_POS_Y);
        if (out_block) *out_block = block;
        return 1;
    }

    return 0;
}

static BlockType proxy_dominant_block4(BlockType b00, BlockType b10, BlockType b01, BlockType b11);
static BlockType proxy_dominant_block2(BlockType b0, BlockType b1);
static uint32_t experimental_proxy_dominant_color(BlockType b00, BlockType b10, BlockType b01, BlockType b11,
                                                  uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11);

static void emit_proxy_top_quad(SdkMeshBuffer* output,
                                float x0, float z0, float x1, float z1,
                                float h00, float h10, float h01, float h11,
                                BlockType b00, BlockType b10, BlockType b01, BlockType b11,
                                uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11)
{
    BlockVertex v00;
    BlockVertex v10;
    BlockVertex v01;
    BlockVertex v11;
    BlockType dominant_block = proxy_dominant_block4(b00, b10, b01, b11);
    uint32_t color = experimental_proxy_dominant_color(b00, b10, b01, b11, c00, c10, c01, c11);
    uint32_t tex_index = proxy_face_tex_index(dominant_block, FACE_POS_Y);

    if (!output) return;

    if (tex_index != UINT32_MAX) {
        set_proxy_textured_vertex(&v00, x0, h00, z0, 0xFFFFFFFFu, FACE_POS_Y, 0.0f, 0.0f, tex_index);
        set_proxy_textured_vertex(&v10, x1, h10, z0, 0xFFFFFFFFu, FACE_POS_Y, 1.0f, 0.0f, tex_index);
        set_proxy_textured_vertex(&v01, x0, h01, z1, 0xFFFFFFFFu, FACE_POS_Y, 0.0f, 1.0f, tex_index);
        set_proxy_textured_vertex(&v11, x1, h11, z1, 0xFFFFFFFFu, FACE_POS_Y, 1.0f, 1.0f, tex_index);
    } else {
        set_proxy_vertex(&v00, x0, h00, z0, color, FACE_POS_Y);
        set_proxy_vertex(&v10, x1, h10, z0, color, FACE_POS_Y);
        set_proxy_vertex(&v01, x0, h01, z1, color, FACE_POS_Y);
        set_proxy_vertex(&v11, x1, h11, z1, color, FACE_POS_Y);
    }

    sdk_mesh_buffer_add_tri(output, &v00, &v10, &v01);
    sdk_mesh_buffer_add_tri(output, &v01, &v10, &v11);
}

static BlockType proxy_dominant_block4(BlockType b00, BlockType b10, BlockType b01, BlockType b11)
{
    BlockType blocks[4] = { b00, b10, b01, b11 };
    int best_index = -1;
    int best_count = -1;
    int i;

    for (i = 0; i < 4; ++i) {
        int count = 0;
        int j;

        if (blocks[i] == BLOCK_AIR) continue;
        for (j = 0; j < 4; ++j) {
            if (blocks[j] == blocks[i]) {
                count++;
            }
        }
        if (count > best_count) {
            best_count = count;
            best_index = i;
        }
    }

    if (best_index >= 0) {
        return blocks[best_index];
    }
    return BLOCK_AIR;
}

static BlockType proxy_dominant_block2(BlockType b0, BlockType b1)
{
    if (b0 != BLOCK_AIR) return b0;
    return b1;
}

static uint32_t experimental_proxy_dominant_color(BlockType b00, BlockType b10, BlockType b01, BlockType b11,
                                                  uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11)
{
    uint32_t colors[4] = { c00, c10, c01, c11 };
    BlockType dominant_block = proxy_dominant_block4(b00, b10, b01, b11);
    int i;

    if (dominant_block != BLOCK_AIR) {
        BlockType blocks[4] = { b00, b10, b01, b11 };
        for (i = 0; i < 4; ++i) {
            if (blocks[i] == dominant_block && colors[i] != 0u) {
                return colors[i];
            }
        }
    }
    return c00 ? c00 : (c10 ? c10 : (c01 ? c01 : c11));
}

static void emit_proxy_top_quad_experimental(SdkMeshBuffer* output,
                                             float x0, float z0, float x1, float z1,
                                             float h00, float h10, float h01, float h11,
                                             BlockType b00, BlockType b10, BlockType b01, BlockType b11,
                                             uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11)
{
    BlockVertex v00;
    BlockVertex v10;
    BlockVertex v01;
    BlockVertex v11;
    BlockType dominant_block = proxy_dominant_block4(b00, b10, b01, b11);
    uint32_t color = experimental_proxy_dominant_color(b00, b10, b01, b11, c00, c10, c01, c11);
    uint32_t tex_index = proxy_face_tex_index(dominant_block, FACE_POS_Y);
    float diag_main = fabsf(h00 - h11);
    float diag_cross = fabsf(h10 - h01);

    if (!output) return;

    if (tex_index != UINT32_MAX) {
        set_proxy_textured_vertex(&v00, x0, h00, z0, 0xFFFFFFFFu, FACE_POS_Y, 0.0f, 0.0f, tex_index);
        set_proxy_textured_vertex(&v10, x1, h10, z0, 0xFFFFFFFFu, FACE_POS_Y, 1.0f, 0.0f, tex_index);
        set_proxy_textured_vertex(&v01, x0, h01, z1, 0xFFFFFFFFu, FACE_POS_Y, 0.0f, 1.0f, tex_index);
        set_proxy_textured_vertex(&v11, x1, h11, z1, 0xFFFFFFFFu, FACE_POS_Y, 1.0f, 1.0f, tex_index);
    } else {
        set_proxy_vertex(&v00, x0, h00, z0, color, FACE_POS_Y);
        set_proxy_vertex(&v10, x1, h10, z0, color, FACE_POS_Y);
        set_proxy_vertex(&v01, x0, h01, z1, color, FACE_POS_Y);
        set_proxy_vertex(&v11, x1, h11, z1, color, FACE_POS_Y);
    }

    if (diag_main <= diag_cross) {
        sdk_mesh_buffer_add_tri(output, &v00, &v10, &v11);
        sdk_mesh_buffer_add_tri(output, &v00, &v11, &v01);
    } else {
        sdk_mesh_buffer_add_tri(output, &v00, &v10, &v01);
        sdk_mesh_buffer_add_tri(output, &v01, &v10, &v11);
    }
}

static void emit_proxy_vertical_quad(SdkMeshBuffer* output,
                                     float x0, float z0, float x1, float z1,
                                     float top0, float top1, float bottom0, float bottom1,
                                     BlockType block0, BlockType block1,
                                     uint32_t color0, uint32_t color1,
                                     uint32_t face)
{
    BlockVertex v0;
    BlockVertex v1;
    BlockVertex v2;
    BlockVertex v3;
    BlockType dominant_block;
    uint32_t tex_index;
    uint32_t color;

    if (!output) return;
    if (top0 <= bottom0 + 0.01f && top1 <= bottom1 + 0.01f) return;

    dominant_block = proxy_dominant_block2(block0, block1);
    tex_index = proxy_face_tex_index(dominant_block, face);
    color = (dominant_block == block1 && block0 == BLOCK_AIR) ? color1 : color0;

    if (tex_index != UINT32_MAX) {
        set_proxy_textured_vertex(&v0, x0, top0, z0, 0xFFFFFFFFu, face, 0.0f, 0.0f, tex_index);
        set_proxy_textured_vertex(&v1, x1, top1, z1, 0xFFFFFFFFu, face, 1.0f, 0.0f, tex_index);
        set_proxy_textured_vertex(&v2, x0, bottom0, z0, 0xFFFFFFFFu, face, 0.0f, 1.0f, tex_index);
        set_proxy_textured_vertex(&v3, x1, bottom1, z1, 0xFFFFFFFFu, face, 1.0f, 1.0f, tex_index);
    } else {
        set_proxy_vertex(&v0, x0, top0, z0, color, face);
        set_proxy_vertex(&v1, x1, top1, z1, color, face);
        set_proxy_vertex(&v2, x0, bottom0, z0, color, face);
        set_proxy_vertex(&v3, x1, bottom1, z1, color, face);
    }
    sdk_mesh_buffer_add_quad(output, &v0, &v1, &v2, &v3);
}

static void build_exact_far_overlay_mesh(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output)
{
    int cx;
    int cz;

    if (!chunk || !output) return;

    cx = chunk->cx;
    cz = chunk->cz;
    sdk_mesh_buffer_clear(output);

    for (int ly = 0; ly < CHUNK_HEIGHT; ++ly) {
        for (int lz = 0; lz < CHUNK_DEPTH; ++lz) {
            for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
                BlockType block = sdk_construction_chunk_get_display_material(chunk, lx, ly, lz);
                int wx;
                int wy;
                int wz;

                if (block == BLOCK_AIR) continue;
                if (!block_should_stay_exact(chunk, lx, lz, block)) continue;

                wx = cx * CHUNK_WIDTH + lx;
                wy = ly;
                wz = cz * CHUNK_DEPTH + lz;

                if ((sdk_block_get_behavior_flags(block) & SDK_BLOCK_BEHAVIOR_FLUID) != 0u) {
                    emit_fluid_faces(output, chunk, cm, lx, ly, lz, block, (float)wx, (float)wy, (float)wz);
                    continue;
                }

                for (int face = 0; face < 6; ++face) {
                    int ndx;
                    int ndy;
                    int ndz;
                    int nlx;
                    int nly;
                    int nlz;
                    BlockType neighbor;

                    sdk_mesh_get_neighbor_offsets(face, &ndx, &ndy, &ndz);
                    nlx = lx + ndx;
                    nly = ly + ndy;
                    nlz = lz + ndz;
                    neighbor = get_neighbor_block(chunk, cm, nlx, nly, nlz);
                    if (should_emit_face(block, neighbor)) {
                        add_face(output, chunk, cm, lx, ly, lz, face, block, (float)wx, (float)wy, (float)wz);
                    }
                }
            }
        }
    }

    store_subchunk_mesh(&chunk->far_exact_overlay_mesh, output);
}

void sdk_mesh_build_chunk_far_proxy(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output)
{
    float heights[FAR_PROXY_GRID_SIZE][FAR_PROXY_GRID_SIZE];
    uint32_t colors[FAR_PROXY_GRID_SIZE][FAR_PROXY_GRID_SIZE];
    BlockType blocks[FAR_PROXY_GRID_SIZE][FAR_PROXY_GRID_SIZE];
    float origin_x;
    float origin_z;
    int gx;
    int gz;

    if (!chunk || !output) return;

    sdk_mesh_buffer_clear(output);
    origin_x = (float)(chunk->cx * CHUNK_WIDTH);
    origin_z = (float)(chunk->cz * CHUNK_DEPTH);

    for (gz = 0; gz < FAR_PROXY_GRID_SIZE; ++gz) {
        for (gx = 0; gx < FAR_PROXY_GRID_SIZE; ++gx) {
            int sample_lx = gx * FAR_PROXY_CELL_STRIDE;
            int sample_lz = gz * FAR_PROXY_CELL_STRIDE;
            float height = 0.0f;
            uint32_t color = 0x00000000u;
            BlockType block = BLOCK_AIR;

            adjust_far_proxy_sample_coords(chunk, &sample_lx, &sample_lz);

            sample_far_surface_point(chunk, cm, sample_lx, sample_lz, &height, &color, &block);
            heights[gz][gx] = height;
            colors[gz][gx] = color;
            blocks[gz][gx] = block;
        }
    }

    for (gz = 0; gz < FAR_PROXY_GRID_SIZE - 1; ++gz) {
        for (gx = 0; gx < FAR_PROXY_GRID_SIZE - 1; ++gx) {
            float x0, z0, x1, z1;
            trimmed_far_proxy_cell_bounds(chunk, gx, gz, FAR_PROXY_GRID_SIZE, FAR_PROXY_CELL_STRIDE,
                                          origin_x, origin_z, &x0, &z0, &x1, &z1);
            if (blocks[gz][gx] == BLOCK_AIR &&
                blocks[gz][gx + 1] == BLOCK_AIR &&
                blocks[gz + 1][gx] == BLOCK_AIR &&
                blocks[gz + 1][gx + 1] == BLOCK_AIR) {
                continue;
            }
            emit_proxy_top_quad(output, x0, z0, x1, z1,
                                heights[gz][gx], heights[gz][gx + 1],
                                heights[gz + 1][gx], heights[gz + 1][gx + 1],
                                blocks[gz][gx], blocks[gz][gx + 1],
                                blocks[gz + 1][gx], blocks[gz + 1][gx + 1],
                                colors[gz][gx], colors[gz][gx + 1],
                                colors[gz + 1][gx], colors[gz + 1][gx + 1]);
        }
    }

    for (gz = 0; gz < FAR_PROXY_GRID_SIZE - 1; ++gz) {
        float z0 = origin_z + (float)(gz * FAR_PROXY_CELL_STRIDE);
        float z1 = origin_z + (float)((gz + 1) * FAR_PROXY_CELL_STRIDE);
        float west_x = origin_x + (chunk_is_superchunk_west_edge(chunk) ? 1.0f : 0.0f);
        float east_x = origin_x + (float)CHUNK_WIDTH - (chunk_is_superchunk_east_edge(chunk) ? 1.0f : 0.0f);
        if (chunk_is_superchunk_north_edge(chunk) && gz == 0) z0 += 1.0f;
        if (chunk_is_superchunk_south_edge(chunk) && gz == (FAR_PROXY_GRID_SIZE - 2)) z1 -= 1.0f;
        emit_proxy_vertical_quad(output,
                                 west_x, z0, west_x, z1,
                                 heights[gz][0], heights[gz + 1][0],
                                 FAR_PROXY_BORDER_FLOOR_Y, FAR_PROXY_BORDER_FLOOR_Y,
                                 blocks[gz][0], blocks[gz + 1][0],
                                 colors[gz][0], colors[gz + 1][0],
                                 FACE_NEG_X);
        emit_proxy_vertical_quad(output,
                                 east_x, z0, east_x, z1,
                                 heights[gz][FAR_PROXY_GRID_SIZE - 1], heights[gz + 1][FAR_PROXY_GRID_SIZE - 1],
                                 FAR_PROXY_BORDER_FLOOR_Y, FAR_PROXY_BORDER_FLOOR_Y,
                                 blocks[gz][FAR_PROXY_GRID_SIZE - 1], blocks[gz + 1][FAR_PROXY_GRID_SIZE - 1],
                                 colors[gz][FAR_PROXY_GRID_SIZE - 1], colors[gz + 1][FAR_PROXY_GRID_SIZE - 1],
                                 FACE_POS_X);
    }

    for (gx = 0; gx < FAR_PROXY_GRID_SIZE - 1; ++gx) {
        float x0 = origin_x + (float)(gx * FAR_PROXY_CELL_STRIDE);
        float x1 = origin_x + (float)((gx + 1) * FAR_PROXY_CELL_STRIDE);
        float north_z = origin_z + (chunk_is_superchunk_north_edge(chunk) ? 1.0f : 0.0f);
        float south_z = origin_z + (float)CHUNK_DEPTH - (chunk_is_superchunk_south_edge(chunk) ? 1.0f : 0.0f);
        if (chunk_is_superchunk_west_edge(chunk) && gx == 0) x0 += 1.0f;
        if (chunk_is_superchunk_east_edge(chunk) && gx == (FAR_PROXY_GRID_SIZE - 2)) x1 -= 1.0f;
        emit_proxy_vertical_quad(output,
                                 x0, north_z, x1, north_z,
                                 heights[0][gx], heights[0][gx + 1],
                                 FAR_PROXY_BORDER_FLOOR_Y, FAR_PROXY_BORDER_FLOOR_Y,
                                 blocks[0][gx], blocks[0][gx + 1],
                                 colors[0][gx], colors[0][gx + 1],
                                 FACE_NEG_Z);
        emit_proxy_vertical_quad(output,
                                 x0, south_z, x1, south_z,
                                 heights[FAR_PROXY_GRID_SIZE - 1][gx], heights[FAR_PROXY_GRID_SIZE - 1][gx + 1],
                                 FAR_PROXY_BORDER_FLOOR_Y, FAR_PROXY_BORDER_FLOOR_Y,
                                 blocks[FAR_PROXY_GRID_SIZE - 1][gx], blocks[FAR_PROXY_GRID_SIZE - 1][gx + 1],
                                 colors[FAR_PROXY_GRID_SIZE - 1][gx], colors[FAR_PROXY_GRID_SIZE - 1][gx + 1],
                                 FACE_POS_Z);
    }

    store_subchunk_mesh(&chunk->far_mesh, output);
}

void sdk_mesh_build_chunk_experimental_far_proxy(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output)
{
    float heights[EXPERIMENTAL_FAR_PROXY_GRID_SIZE][EXPERIMENTAL_FAR_PROXY_GRID_SIZE];
    uint32_t colors[EXPERIMENTAL_FAR_PROXY_GRID_SIZE][EXPERIMENTAL_FAR_PROXY_GRID_SIZE];
    BlockType blocks[EXPERIMENTAL_FAR_PROXY_GRID_SIZE][EXPERIMENTAL_FAR_PROXY_GRID_SIZE];
    float origin_x;
    float origin_z;
    int gx;
    int gz;

    if (!chunk || !output) return;

    sdk_mesh_buffer_clear(output);
    origin_x = (float)(chunk->cx * CHUNK_WIDTH);
    origin_z = (float)(chunk->cz * CHUNK_DEPTH);

    for (gz = 0; gz < EXPERIMENTAL_FAR_PROXY_GRID_SIZE; ++gz) {
        for (gx = 0; gx < EXPERIMENTAL_FAR_PROXY_GRID_SIZE; ++gx) {
            int sample_lx = gx * EXPERIMENTAL_FAR_PROXY_CELL_STRIDE;
            int sample_lz = gz * EXPERIMENTAL_FAR_PROXY_CELL_STRIDE;
            float height = 0.0f;
            uint32_t color = 0x00000000u;
            BlockType block = BLOCK_AIR;

            adjust_far_proxy_sample_coords(chunk, &sample_lx, &sample_lz);
            sample_far_surface_point(chunk, cm, sample_lx, sample_lz, &height, &color, &block);
            heights[gz][gx] = height;
            colors[gz][gx] = color;
            blocks[gz][gx] = block;
        }
    }

    for (gz = 0; gz < EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1; ++gz) {
        for (gx = 0; gx < EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1; ++gx) {
            float x0, z0, x1, z1;
            trimmed_far_proxy_cell_bounds(chunk, gx, gz, EXPERIMENTAL_FAR_PROXY_GRID_SIZE,
                                          EXPERIMENTAL_FAR_PROXY_CELL_STRIDE,
                                          origin_x, origin_z, &x0, &z0, &x1, &z1);
            if (blocks[gz][gx] == BLOCK_AIR &&
                blocks[gz][gx + 1] == BLOCK_AIR &&
                blocks[gz + 1][gx] == BLOCK_AIR &&
                blocks[gz + 1][gx + 1] == BLOCK_AIR) {
                continue;
            }
            emit_proxy_top_quad_experimental(output, x0, z0, x1, z1,
                                             heights[gz][gx], heights[gz][gx + 1],
                                             heights[gz + 1][gx], heights[gz + 1][gx + 1],
                                             blocks[gz][gx], blocks[gz][gx + 1],
                                             blocks[gz + 1][gx], blocks[gz + 1][gx + 1],
                                             colors[gz][gx], colors[gz][gx + 1],
                                             colors[gz + 1][gx], colors[gz + 1][gx + 1]);
        }
    }

    for (gz = 0; gz < EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1; ++gz) {
        float z0 = origin_z + (float)(gz * EXPERIMENTAL_FAR_PROXY_CELL_STRIDE);
        float z1 = origin_z + (float)((gz + 1) * EXPERIMENTAL_FAR_PROXY_CELL_STRIDE);
        float west_x = origin_x + (chunk_is_superchunk_west_edge(chunk) ? 1.0f : 0.0f);
        float east_x = origin_x + (float)CHUNK_WIDTH - (chunk_is_superchunk_east_edge(chunk) ? 1.0f : 0.0f);
        if (chunk_is_superchunk_north_edge(chunk) && gz == 0) z0 += 1.0f;
        if (chunk_is_superchunk_south_edge(chunk) && gz == (EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 2)) z1 -= 1.0f;
        emit_proxy_vertical_quad(output,
                                 west_x, z0, west_x, z1,
                                 heights[gz][0], heights[gz + 1][0],
                                 FAR_PROXY_BORDER_FLOOR_Y, FAR_PROXY_BORDER_FLOOR_Y,
                                 blocks[gz][0], blocks[gz + 1][0],
                                 colors[gz][0], colors[gz + 1][0],
                                 FACE_NEG_X);
        emit_proxy_vertical_quad(output,
                                 east_x, z0, east_x, z1,
                                 heights[gz][EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1],
                                 heights[gz + 1][EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1],
                                 FAR_PROXY_BORDER_FLOOR_Y, FAR_PROXY_BORDER_FLOOR_Y,
                                 blocks[gz][EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1],
                                 blocks[gz + 1][EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1],
                                 colors[gz][EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1],
                                 colors[gz + 1][EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1],
                                 FACE_POS_X);
    }

    for (gx = 0; gx < EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1; ++gx) {
        float x0 = origin_x + (float)(gx * EXPERIMENTAL_FAR_PROXY_CELL_STRIDE);
        float x1 = origin_x + (float)((gx + 1) * EXPERIMENTAL_FAR_PROXY_CELL_STRIDE);
        float north_z = origin_z + (chunk_is_superchunk_north_edge(chunk) ? 1.0f : 0.0f);
        float south_z = origin_z + (float)CHUNK_DEPTH - (chunk_is_superchunk_south_edge(chunk) ? 1.0f : 0.0f);
        if (chunk_is_superchunk_west_edge(chunk) && gx == 0) x0 += 1.0f;
        if (chunk_is_superchunk_east_edge(chunk) && gx == (EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 2)) x1 -= 1.0f;
        emit_proxy_vertical_quad(output,
                                 x0, north_z, x1, north_z,
                                 heights[0][gx], heights[0][gx + 1],
                                 FAR_PROXY_BORDER_FLOOR_Y, FAR_PROXY_BORDER_FLOOR_Y,
                                 blocks[0][gx], blocks[0][gx + 1],
                                 colors[0][gx], colors[0][gx + 1],
                                 FACE_NEG_Z);
        emit_proxy_vertical_quad(output,
                                 x0, south_z, x1, south_z,
                                 heights[EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1][gx],
                                 heights[EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1][gx + 1],
                                 FAR_PROXY_BORDER_FLOOR_Y, FAR_PROXY_BORDER_FLOOR_Y,
                                 blocks[EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1][gx],
                                 blocks[EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1][gx + 1],
                                 colors[EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1][gx],
                                 colors[EXPERIMENTAL_FAR_PROXY_GRID_SIZE - 1][gx + 1],
                                 FACE_POS_Z);
    }

    store_subchunk_mesh(&chunk->experimental_far_mesh, output);
}

static void build_subchunk_mesh_layer(SdkChunk* chunk,
                                      SdkChunkManager* cm,
                                      int subchunk_index,
                                      SdkMeshBuffer* output,
                                      int water_only)
{
    int y_start;
    int y_end;
    int cx;
    int cz;

    if (!chunk || !output) return;
    if ((unsigned)subchunk_index >= CHUNK_SUBCHUNK_COUNT) return;

    y_start = sdk_chunk_subchunk_min_y(subchunk_index);
    y_end = sdk_chunk_subchunk_max_y(subchunk_index);
    cx = chunk->cx;
    cz = chunk->cz;
    sdk_mesh_buffer_clear(output);

    for (int ly = y_start; ly <= y_end; ++ly) {
        for (int lz = 0; lz < CHUNK_DEPTH; ++lz) {
            for (int lx = 0; lx < CHUNK_WIDTH; ++lx) {
                SdkWorldCellCode cell_code = sdk_chunk_get_cell_code(chunk, lx, ly, lz);
                BlockType block = sdk_world_cell_decode_full_block(cell_code);
                uint32_t behavior_flags;
                BlockType construction_material = BLOCK_AIR;
                uint64_t construction_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
                SdkConstructionWorkspace construction_workspace;
                uint8_t bounds_min[3];
                uint8_t bounds_max[3];
                uint16_t occupied_count = 0u;

                if (!sdk_world_cell_is_full_block(cell_code)) {
                    if (water_only) continue;
                    if (!sdk_construction_chunk_get_cell_payload(chunk, lx, ly, lz,
                                                                &construction_material,
                                                                construction_occupancy,
                                                                NULL) ||
                        construction_material == BLOCK_AIR) {
                        continue;
                    }
                    if (sdk_world_cell_is_inline_construction(cell_code)) {
                        sdk_construction_occupancy_bounds(construction_occupancy,
                                                          &occupied_count,
                                                          bounds_min,
                                                          bounds_max);
                        if (occupied_count > 0u) {
                            emit_construction_box(output,
                                                  construction_material,
                                                  (float)cx * (float)CHUNK_WIDTH + (float)lx + (float)bounds_min[0] / 16.0f,
                                                  (float)ly + (float)bounds_min[1] / 16.0f,
                                                  (float)cz * (float)CHUNK_DEPTH + (float)lz + (float)bounds_min[2] / 16.0f,
                                                  (float)cx * (float)CHUNK_WIDTH + (float)lx + (float)bounds_max[0] / 16.0f,
                                                  (float)ly + (float)bounds_max[1] / 16.0f,
                                                  (float)cz * (float)CHUNK_DEPTH + (float)lz + (float)bounds_max[2] / 16.0f);
                        }
                    } else {
                        if (!sdk_construction_chunk_get_workspace(chunk, lx, ly, lz, &construction_workspace)) {
                            continue;
                        }
                        emit_overflow_construction_mesh(output,
                                                        cx * CHUNK_WIDTH + lx,
                                                        ly,
                                                        cz * CHUNK_DEPTH + lz,
                                                        &construction_workspace);
                    }
                    continue;
                }

                if (block == BLOCK_AIR) continue;

                behavior_flags = sdk_block_get_behavior_flags(block);

                int wx = cx * CHUNK_WIDTH + lx;
                int wy = ly;
                int wz = cz * CHUNK_DEPTH + lz;

                if ((behavior_flags & SDK_BLOCK_BEHAVIOR_FLUID) != 0u) {
                    if (!water_only) {
                        continue;
                    }
                    emit_fluid_faces(output, chunk, cm, lx, ly, lz, block, (float)wx, (float)wy, (float)wz);
                    continue;
                }
                if (water_only) continue;

                for (int face = 0; face < 6; ++face) {
                    int ndx;
                    int ndy;
                    int ndz;
                    int nlx;
                    int nly;
                    int nlz;
                    BlockType neighbor;

                    sdk_mesh_get_neighbor_offsets(face, &ndx, &ndy, &ndz);
                    nlx = lx + ndx;
                    nly = ly + ndy;
                    nlz = lz + ndz;

                    neighbor = get_neighbor_block(chunk, cm, nlx, nly, nlz);
                    if (should_emit_face(block, neighbor)) {
                        add_face(output, chunk, cm, lx, ly, lz, face, block, (float)wx, (float)wy, (float)wz);
                    }
                }
            }
        }
    }
}

static void build_subchunk_mesh(SdkChunk* chunk, SdkChunkManager* cm, int subchunk_index, SdkMeshBuffer* output)
{
    if (!chunk || !output) return;
    if ((unsigned)subchunk_index >= CHUNK_SUBCHUNK_COUNT) return;

    build_subchunk_mesh_layer(chunk, cm, subchunk_index, output, 0);
    store_subchunk_mesh(&chunk->subchunks[subchunk_index], output);
    build_subchunk_mesh_layer(chunk, cm, subchunk_index, output, 1);
    store_subchunk_mesh(&chunk->water_subchunks[subchunk_index], output);
}

void sdk_mesh_build_chunk(SdkChunk* chunk, SdkChunkManager* cm, SdkMeshBuffer* output)
{
    if (!chunk || !chunk->blocks || !output) {
        OutputDebugStringA("[NQL SDK] Mesh build failed: null ptr\n");
        return;
    }
    
    /* Validate buffer is initialized */
    if (!output->vertices || output->capacity == 0) {
        OutputDebugStringA("[NQL SDK] Mesh build failed: buffer not initialized\n");
        return;
    }

    for (int subchunk_index = 0; subchunk_index < CHUNK_SUBCHUNK_COUNT; ++subchunk_index) {
        if (!(chunk->dirty_subchunks_mask & (1u << subchunk_index))) continue;
        build_subchunk_mesh(chunk, cm, subchunk_index, output);
    }

    if (chunk->far_mesh.dirty) {
        sdk_mesh_build_chunk_far_proxy(chunk, cm, output);
    }
    if (chunk->experimental_far_mesh.dirty) {
        sdk_mesh_build_chunk_experimental_far_proxy(chunk, cm, output);
    }
    if (chunk->far_exact_overlay_mesh.dirty) {
        build_exact_far_overlay_mesh(chunk, cm, output);
    }

    sdk_chunk_refresh_mesh_state(chunk);
    chunk->cpu_mesh_generation = chunk->mesh_job_generation;
}

void sdk_mesh_build_all(SdkChunkManager* cm)
{
    if (!cm) return;
    
    /* Static buffer for mesh generation - re-used for efficiency */
    static SdkMeshBuffer buffer = { NULL, 0, 0 };
    
    if (!buffer.vertices) {
        sdk_mesh_buffer_init(&buffer, MESH_BUFFER_INITIAL_VERTS);
    }
    
    for (int slot_index = 0; slot_index < sdk_chunk_manager_slot_capacity(); ++slot_index) {
        SdkChunkResidentSlot* slot = sdk_chunk_manager_get_slot_at(cm, slot_index);
        SdkChunk* chunk;
        if (!slot || !slot->occupied) continue;
        chunk = &slot->chunk;
        if (chunk->blocks && chunk->dirty) {
            sdk_mesh_build_chunk(chunk, cm, &buffer);
            /* Note: GPU upload is handled by renderer */
        }
    }
}
