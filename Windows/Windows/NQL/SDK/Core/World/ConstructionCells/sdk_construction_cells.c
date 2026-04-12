#include "sdk_construction_cells.h"
#include "../Blocks/sdk_block.h"
#include "../../MeshBuilder/sdk_mesh_builder.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum {
    SDK_CONSTRUCTION_LOCAL_NEIGHBOR_COUNT = 7,
    SDK_CONSTRUCTION_REGISTRY_PREALLOCATED_SLOTS = 65536
};

static const int k_neighbor_offsets[SDK_CONSTRUCTION_LOCAL_NEIGHBOR_COUNT][3] = {
    { 0, 0, 0 },
    { -1, 0, 0 },
    { 1, 0, 0 },
    { 0, -1, 0 },
    { 0, 1, 0 },
    { 0, 0, -1 },
    { 0, 0, 1 }
};

static void construction_payload_from_occupancy(BlockType material,
                                                const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                                SdkConstructionItemPayload* out_payload);
static int construction_cell_supported(const SdkChunkManager* cm,
                                       int wx, int wy, int wz,
                                       const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT]);
static int construction_tool_shapes_material(ToolClass tool, BlockType material);
static SdkConstructionWorkspace* construction_workspace_alloc(void);

static SdkConstructionWorkspace* construction_workspace_alloc(void)
{
    return (SdkConstructionWorkspace*)malloc(sizeof(SdkConstructionWorkspace));
}

static int construction_clamp_payload_dim(int value)
{
    if (value < 1) return 1;
    if (value > (int)SDK_CONSTRUCTION_CELL_RESOLUTION) return (int)SDK_CONSTRUCTION_CELL_RESOLUTION;
    return value;
}

static void construction_sort_dims_ascending(uint8_t dims[3])
{
    if (!dims) return;
    if (dims[0] > dims[1]) {
        uint8_t tmp = dims[0];
        dims[0] = dims[1];
        dims[1] = tmp;
    }
    if (dims[1] > dims[2]) {
        uint8_t tmp = dims[1];
        dims[1] = dims[2];
        dims[2] = tmp;
    }
    if (dims[0] > dims[1]) {
        uint8_t tmp = dims[0];
        dims[0] = dims[1];
        dims[1] = tmp;
    }
}

static uint16_t construction_pack_unordered_box_dims(const uint8_t dims[3])
{
    uint16_t packed = 0u;

    if (!dims) return 0u;
    packed |= (uint16_t)((dims[0] - 1u) & 0x0Fu);
    packed |= (uint16_t)(((dims[1] - 1u) & 0x0Fu) << 4);
    packed |= (uint16_t)(((dims[2] - 1u) & 0x0Fu) << 8);
    return packed;
}

static int construction_try_get_unordered_box_signature(
    const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
    uint16_t* out_packed_dims)
{
    uint8_t bounds_min[3];
    uint8_t bounds_max[3];
    uint16_t count = 0u;
    uint8_t dims[3] = {0, 0, 0};
    uint32_t volume;

    if (out_packed_dims) *out_packed_dims = 0u;
    if (!occupancy) return 0;

    sdk_construction_occupancy_bounds(occupancy, &count, bounds_min, bounds_max);
    if (count == 0u) return 0;

    dims[0] = (uint8_t)(bounds_max[0] - bounds_min[0]);
    dims[1] = (uint8_t)(bounds_max[1] - bounds_min[1]);
    dims[2] = (uint8_t)(bounds_max[2] - bounds_min[2]);
    volume = (uint32_t)dims[0] * (uint32_t)dims[1] * (uint32_t)dims[2];
    if (volume != (uint32_t)count) return 0;

    for (int y = bounds_min[1]; y < bounds_max[1]; ++y) {
        for (int z = bounds_min[2]; z < bounds_max[2]; ++z) {
            for (int x = bounds_min[0]; x < bounds_max[0]; ++x) {
                if (!sdk_construction_occupancy_get(occupancy, x, y, z)) {
                    return 0;
                }
            }
        }
    }

    construction_sort_dims_ascending(dims);
    if (out_packed_dims) *out_packed_dims = construction_pack_unordered_box_dims(dims);
    return 1;
}

static void construction_transform_voxel_for_face(int face, int rotation,
                                                  int sx, int sy, int sz,
                                                  int* out_dx, int* out_dy, int* out_dz)
{
    int dx = sx;
    int dy = sy;
    int dz = sz;

    rotation &= 3;

    switch (face) {
        case FACE_NEG_Y:
        case FACE_POS_Y:
            switch (rotation) {
                case 1: dx = 15 - sz; dz = sx;      break;
                case 2: dx = 15 - sx; dz = 15 - sz; break;
                case 3: dx = sz;      dz = 15 - sx; break;
                case 0:
                default: dx = sx;     dz = sz;      break;
            }
            dy = sy;
            break;
        case FACE_NEG_Z:
        case FACE_POS_Z:
            switch (rotation) {
                case 1: dx = 15 - sy; dy = sx;      break;
                case 2: dx = 15 - sx; dy = 15 - sy; break;
                case 3: dx = sy;      dy = 15 - sx; break;
                case 0:
                default: dx = sx;     dy = sy;      break;
            }
            dz = sz;
            break;
        case FACE_NEG_X:
        case FACE_POS_X:
            switch (rotation) {
                case 1: dz = 15 - sy; dy = sx;      break;
                case 2: dz = 15 - sx; dy = 15 - sy; break;
                case 3: dz = sy;      dy = 15 - sx; break;
                case 0:
                default: dz = sx;     dy = sy;      break;
            }
            dx = sz;
            break;
        default:
            break;
    }

    if (out_dx) *out_dx = dx;
    if (out_dy) *out_dy = dy;
    if (out_dz) *out_dz = dz;
}

static uint32_t construction_local_index(int lx, int ly, int lz)
{
    return (uint32_t)ly * CHUNK_BLOCKS_PER_LAYER + (uint32_t)lz * CHUNK_WIDTH + (uint32_t)lx;
}

static void construction_clear_face_masks(uint64_t face_masks[6][SDK_CONSTRUCTION_FACE_MASK_WORDS])
{
    memset(face_masks, 0, sizeof(uint64_t) * 6u * SDK_CONSTRUCTION_FACE_MASK_WORDS);
}

static void construction_face_mask_set(uint64_t face_masks[6][SDK_CONSTRUCTION_FACE_MASK_WORDS],
                                       int face, int u, int v)
{
    int bit_index;
    int word_index;
    uint64_t bit;

    if (!face_masks) return;
    if ((unsigned)face >= 6u || (unsigned)u >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)v >= SDK_CONSTRUCTION_CELL_RESOLUTION) {
        return;
    }
    bit_index = v * (int)SDK_CONSTRUCTION_CELL_RESOLUTION + u;
    word_index = bit_index >> 6;
    bit = 1ull << (bit_index & 63);
    face_masks[face][word_index] |= bit;
}

int sdk_construction_occupancy_get(const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                   int x, int y, int z)
{
    int voxel_index;
    int word_index;
    uint64_t bit;

    if (!occupancy) return 0;
    if ((unsigned)x >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)y >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)z >= SDK_CONSTRUCTION_CELL_RESOLUTION) {
        return 0;
    }
    voxel_index = y * 256 + z * 16 + x;
    word_index = voxel_index >> 6;
    bit = 1ull << (voxel_index & 63);
    return (occupancy[word_index] & bit) != 0ull;
}

void sdk_construction_occupancy_set(uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                    int x, int y, int z, int occupied)
{
    int voxel_index;
    int word_index;
    uint64_t bit;

    if (!occupancy) return;
    if ((unsigned)x >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)y >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)z >= SDK_CONSTRUCTION_CELL_RESOLUTION) {
        return;
    }
    voxel_index = y * 256 + z * 16 + x;
    word_index = voxel_index >> 6;
    bit = 1ull << (voxel_index & 63);
    if (occupied) occupancy[word_index] |= bit;
    else occupancy[word_index] &= ~bit;
}

void sdk_construction_clear_occupancy(uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    if (!occupancy) return;
    memset(occupancy, 0, sizeof(uint64_t) * SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT);
}

void sdk_construction_fill_full_occupancy(uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    if (!occupancy) return;
    memset(occupancy, 0xFF, sizeof(uint64_t) * SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT);
}

void sdk_construction_occupancy_bounds(const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                       uint16_t* out_count,
                                       uint8_t out_min[3],
                                       uint8_t out_max[3])
{
    uint16_t count = 0u;
    uint8_t min_v[3] = { 16u, 16u, 16u };
    uint8_t max_v[3] = { 0u, 0u, 0u };

    if (!occupancy) {
        if (out_count) *out_count = 0u;
        if (out_min) memset(out_min, 0, 3u);
        if (out_max) memset(out_max, 0, 3u);
        return;
    }

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                if (!sdk_construction_occupancy_get(occupancy, x, y, z)) continue;
                count++;
                if ((uint8_t)x < min_v[0]) min_v[0] = (uint8_t)x;
                if ((uint8_t)y < min_v[1]) min_v[1] = (uint8_t)y;
                if ((uint8_t)z < min_v[2]) min_v[2] = (uint8_t)z;
                if ((uint8_t)(x + 1) > max_v[0]) max_v[0] = (uint8_t)(x + 1);
                if ((uint8_t)(y + 1) > max_v[1]) max_v[1] = (uint8_t)(y + 1);
                if ((uint8_t)(z + 1) > max_v[2]) max_v[2] = (uint8_t)(z + 1);
            }
        }
    }

    if (count == 0u) {
        memset(min_v, 0, sizeof(min_v));
        memset(max_v, 0, sizeof(max_v));
    }
    if (out_count) *out_count = count;
    if (out_min) memcpy(out_min, min_v, sizeof(min_v));
    if (out_max) memcpy(out_max, max_v, sizeof(max_v));
}

void sdk_construction_fill_profile_occupancy(SdkInlineConstructionProfile profile,
                                             uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    sdk_construction_clear_occupancy(occupancy);
    switch (profile) {
        case SDK_INLINE_PROFILE_HALF_NEG_X:
            for (int y = 0; y < 16; ++y) for (int z = 0; z < 16; ++z) for (int x = 0; x < 8; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_HALF_POS_X:
            for (int y = 0; y < 16; ++y) for (int z = 0; z < 16; ++z) for (int x = 8; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_HALF_NEG_Y:
            for (int y = 0; y < 8; ++y) for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_HALF_POS_Y:
            for (int y = 8; y < 16; ++y) for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_HALF_NEG_Z:
            for (int y = 0; y < 16; ++y) for (int z = 0; z < 8; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_HALF_POS_Z:
            for (int y = 0; y < 16; ++y) for (int z = 8; z < 16; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_QUARTER_NEG_X:
            for (int y = 0; y < 16; ++y) for (int z = 0; z < 16; ++z) for (int x = 0; x < 4; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_QUARTER_POS_X:
            for (int y = 0; y < 16; ++y) for (int z = 0; z < 16; ++z) for (int x = 12; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_QUARTER_NEG_Y:
            for (int y = 0; y < 4; ++y) for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_QUARTER_POS_Y:
            for (int y = 12; y < 16; ++y) for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_QUARTER_NEG_Z:
            for (int y = 0; y < 16; ++y) for (int z = 0; z < 4; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_QUARTER_POS_Z:
            for (int y = 0; y < 16; ++y) for (int z = 12; z < 16; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_BEAM_X:
            for (int y = 6; y < 10; ++y) for (int z = 6; z < 10; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_BEAM_Y:
            for (int y = 0; y < 16; ++y) for (int z = 6; z < 10; ++z) for (int x = 6; x < 10; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_BEAM_Z:
            for (int y = 6; y < 10; ++y) for (int z = 0; z < 16; ++z) for (int x = 6; x < 10; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_STRIP_X:
            for (int y = 7; y < 9; ++y) for (int z = 6; z < 10; ++z) for (int x = 0; x < 16; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_STRIP_Y:
            for (int y = 0; y < 16; ++y) for (int z = 7; z < 9; ++z) for (int x = 6; x < 10; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        case SDK_INLINE_PROFILE_STRIP_Z:
            for (int y = 6; y < 10; ++y) for (int z = 0; z < 16; ++z) for (int x = 7; x < 9; ++x) sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            break;
        default:
            break;
    }
}

int sdk_construction_try_match_inline_profile(const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                              SdkInlineConstructionProfile* out_profile)
{
    uint64_t scratch[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];

    if (out_profile) *out_profile = SDK_INLINE_PROFILE_NONE;
    if (!occupancy) return 0;
    for (int profile = SDK_INLINE_PROFILE_HALF_NEG_X; profile < SDK_INLINE_PROFILE_COUNT; ++profile) {
        sdk_construction_fill_profile_occupancy((SdkInlineConstructionProfile)profile, scratch);
        if (memcmp(scratch, occupancy, sizeof(scratch)) == 0) {
            if (out_profile) *out_profile = (SdkInlineConstructionProfile)profile;
            return 1;
        }
    }
    return 0;
}

void sdk_construction_payload_from_full_block(BlockType material, SdkConstructionItemPayload* out_payload)
{
    uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    sdk_construction_fill_full_occupancy(occupancy);
    construction_payload_from_occupancy(material, occupancy, out_payload);
}

void sdk_construction_payload_make_box(BlockType material, int width, int height, int depth,
                                       SdkConstructionItemPayload* out_payload)
{
    uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];

    width = construction_clamp_payload_dim(width);
    height = construction_clamp_payload_dim(height);
    depth = construction_clamp_payload_dim(depth);

    sdk_construction_clear_occupancy(occupancy);
    for (int y = 0; y < height; ++y) {
        for (int z = 0; z < depth; ++z) {
            for (int x = 0; x < width; ++x) {
                sdk_construction_occupancy_set(occupancy, x, y, z, 1);
            }
        }
    }
    construction_payload_from_occupancy(material, occupancy, out_payload);
}

void sdk_construction_payload_transform_for_face(const SdkConstructionItemPayload* src,
                                                 int face, int rotation,
                                                 SdkConstructionItemPayload* out_payload)
{
    uint64_t transformed[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    int min_x = 16;
    int min_y = 16;
    int min_z = 16;
    int max_x = -1;
    int max_y = -1;
    int max_z = -1;
    int shift_x = 0;
    int shift_y = 0;
    int shift_z = 0;

    if (!out_payload) return;
    memset(out_payload, 0, sizeof(*out_payload));
    if (!src || src->occupied_count == 0u) return;

    sdk_construction_clear_occupancy(transformed);
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                int dx;
                int dy;
                int dz;
                if (!sdk_construction_occupancy_get(src->occupancy, x, y, z)) continue;
                construction_transform_voxel_for_face(face, rotation, x, y, z, &dx, &dy, &dz);
                if (dx < min_x) min_x = dx;
                if (dy < min_y) min_y = dy;
                if (dz < min_z) min_z = dz;
                if (dx > max_x) max_x = dx;
                if (dy > max_y) max_y = dy;
                if (dz > max_z) max_z = dz;
                sdk_construction_occupancy_set(transformed, dx, dy, dz, 1);
            }
        }
    }

    if (max_x < 0 || max_y < 0 || max_z < 0) {
        return;
    }

    switch (face) {
        case FACE_POS_X: shift_x = -min_x;       break;
        case FACE_NEG_X: shift_x = 15 - max_x;   break;
        case FACE_POS_Y: shift_y = -min_y;       break;
        case FACE_NEG_Y: shift_y = 15 - max_y;   break;
        case FACE_POS_Z: shift_z = -min_z;       break;
        case FACE_NEG_Z: shift_z = 15 - max_z;   break;
        default: break;
    }

    if (shift_x != 0 || shift_y != 0 || shift_z != 0) {
        uint64_t shifted[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
        sdk_construction_clear_occupancy(shifted);
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    if (!sdk_construction_occupancy_get(transformed, x, y, z)) continue;
                    sdk_construction_occupancy_set(shifted, x + shift_x, y + shift_y, z + shift_z, 1);
                }
            }
        }
        construction_payload_from_occupancy((BlockType)src->material, shifted, out_payload);
        return;
    }

    construction_payload_from_occupancy((BlockType)src->material, transformed, out_payload);
}

void sdk_construction_payload_copy(SdkConstructionItemPayload* dst, const SdkConstructionItemPayload* src)
{
    if (!dst || !src) return;
    *dst = *src;
}

void sdk_construction_payload_refresh_metadata(SdkConstructionItemPayload* payload)
{
    SdkInlineConstructionProfile profile = SDK_INLINE_PROFILE_NONE;

    if (!payload) return;

    payload->inline_profile_hint = (uint8_t)SDK_INLINE_PROFILE_NONE;
    payload->item_identity_kind = (uint8_t)SDK_CONSTRUCTION_ITEM_IDENTITY_NONE;
    payload->unordered_box_dims_packed = 0u;

    sdk_construction_occupancy_bounds(payload->occupancy, &payload->occupied_count, NULL, NULL);
    if (payload->occupied_count == 0u) return;

    if (sdk_construction_try_match_inline_profile(payload->occupancy, &profile)) {
        payload->inline_profile_hint = (uint8_t)profile;
    }
    if (construction_try_get_unordered_box_signature(payload->occupancy,
                                                     &payload->unordered_box_dims_packed)) {
        payload->item_identity_kind = (uint8_t)SDK_CONSTRUCTION_ITEM_IDENTITY_UNORDERED_BOX;
    }
}

int sdk_construction_payload_empty(const SdkConstructionItemPayload* payload)
{
    return !payload || payload->occupied_count == 0u;
}

BlockType sdk_construction_payload_material(const SdkConstructionItemPayload* payload)
{
    return (!payload || payload->occupied_count == 0u) ? BLOCK_AIR : (BlockType)payload->material;
}

int sdk_construction_payload_matches_inline(const SdkConstructionItemPayload* payload,
                                            SdkInlineConstructionProfile* out_profile)
{
    if (out_profile) *out_profile = SDK_INLINE_PROFILE_NONE;
    if (!payload || payload->occupied_count == 0u) return 0;
    return sdk_construction_try_match_inline_profile(payload->occupancy, out_profile);
}

int sdk_construction_payload_same_item_identity(const SdkConstructionItemPayload* a,
                                                const SdkConstructionItemPayload* b)
{
    if (!a || !b) return 0;
    if (a->occupied_count == 0u || b->occupied_count == 0u) return 0;
    if (a->material != b->material) return 0;

    if (a->item_identity_kind == SDK_CONSTRUCTION_ITEM_IDENTITY_UNORDERED_BOX &&
        b->item_identity_kind == SDK_CONSTRUCTION_ITEM_IDENTITY_UNORDERED_BOX) {
        return a->unordered_box_dims_packed == b->unordered_box_dims_packed;
    }

    if (a->occupied_count != b->occupied_count) return 0;
    return memcmp(a->occupancy, b->occupancy, sizeof(a->occupancy)) == 0;
}

static void construction_payload_from_occupancy(BlockType material,
                                                const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                                SdkConstructionItemPayload* out_payload)
{
    if (!out_payload) return;
    memset(out_payload, 0, sizeof(*out_payload));
    out_payload->material = (uint16_t)material;
    if (!occupancy) return;
    memcpy(out_payload->occupancy, occupancy, sizeof(out_payload->occupancy));
    sdk_construction_payload_refresh_metadata(out_payload);
}

static void construction_workspace_clear(SdkConstructionWorkspace* workspace)
{
    if (!workspace) return;
    memset(workspace->voxels, 0, sizeof(workspace->voxels));
}

static uint16_t construction_workspace_get(const SdkConstructionWorkspace* workspace, int x, int y, int z)
{
    int voxel_index;

    if (!workspace) return (uint16_t)BLOCK_AIR;
    if ((unsigned)x >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)y >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)z >= SDK_CONSTRUCTION_CELL_RESOLUTION) {
        return (uint16_t)BLOCK_AIR;
    }
    voxel_index = y * 256 + z * 16 + x;
    return workspace->voxels[voxel_index];
}

static void construction_workspace_set(SdkConstructionWorkspace* workspace, int x, int y, int z, BlockType material)
{
    int voxel_index;

    if (!workspace) return;
    if ((unsigned)x >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)y >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)z >= SDK_CONSTRUCTION_CELL_RESOLUTION) {
        return;
    }
    voxel_index = y * 256 + z * 16 + x;
    workspace->voxels[voxel_index] = (uint16_t)material;
}

static void construction_workspace_from_occupancy(const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                                  BlockType material,
                                                  SdkConstructionWorkspace* out_workspace)
{
    if (!out_workspace) return;
    construction_workspace_clear(out_workspace);
    if (!occupancy || material == BLOCK_AIR) return;
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                if (!sdk_construction_occupancy_get(occupancy, x, y, z)) continue;
                construction_workspace_set(out_workspace, x, y, z, material);
            }
        }
    }
}

static void construction_workspace_from_payload(const SdkConstructionItemPayload* payload,
                                                SdkConstructionWorkspace* out_workspace)
{
    if (!out_workspace) return;
    construction_workspace_clear(out_workspace);
    if (!payload || payload->occupied_count == 0u || (BlockType)payload->material == BLOCK_AIR) return;
    construction_workspace_from_occupancy(payload->occupancy, (BlockType)payload->material, out_workspace);
}

static void construction_workspace_copy(SdkConstructionWorkspace* dst,
                                        const SdkConstructionWorkspace* src)
{
    if (!dst) return;
    if (!src) {
        construction_workspace_clear(dst);
        return;
    }
    memcpy(dst, src, sizeof(*dst));
}

static void construction_workspace_filter_from_source(const SdkConstructionWorkspace* source,
                                                      const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                                      SdkConstructionWorkspace* out_workspace)
{
    if (!out_workspace) return;
    construction_workspace_clear(out_workspace);
    if (!source || !occupancy) return;
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                if (!sdk_construction_occupancy_get(occupancy, x, y, z)) continue;
                construction_workspace_set(out_workspace, x, y, z,
                                           (BlockType)construction_workspace_get(source, x, y, z));
            }
        }
    }
}

static int construction_workspace_any_material_matches_tool(const SdkConstructionWorkspace* workspace,
                                                            ToolClass tool)
{
    uint16_t seen[SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE];
    uint8_t seen_count = 0u;

    if (!workspace) return 0;
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < (int)SDK_CONSTRUCTION_CELL_VOXELS; ++i) {
        BlockType material = (BlockType)workspace->voxels[i];
        int already_seen = 0;
        if (material == BLOCK_AIR) continue;
        for (uint8_t j = 0u; j < seen_count; ++j) {
            if (seen[j] == (uint16_t)material) {
                already_seen = 1;
                break;
            }
        }
        if (already_seen) continue;
        if (construction_tool_shapes_material(tool, material)) return 1;
        if (seen_count < SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE) {
            seen[seen_count++] = (uint16_t)material;
        }
    }
    return 0;
}

static void construction_workspace_to_occupancy(const SdkConstructionWorkspace* workspace,
                                                uint64_t out_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                                uint16_t* out_count,
                                                uint8_t out_min[3],
                                                uint8_t out_max[3],
                                                BlockType* out_single_material,
                                                int* out_single_material_only)
{
    uint16_t count = 0u;
    uint8_t min_v[3] = { 16u, 16u, 16u };
    uint8_t max_v[3] = { 0u, 0u, 0u };
    BlockType first_material = BLOCK_AIR;
    int single_material_only = 1;

    if (out_occupancy) sdk_construction_clear_occupancy(out_occupancy);
    if (!workspace) {
        if (out_count) *out_count = 0u;
        if (out_min) memset(out_min, 0, 3u);
        if (out_max) memset(out_max, 0, 3u);
        if (out_single_material) *out_single_material = BLOCK_AIR;
        if (out_single_material_only) *out_single_material_only = 1;
        return;
    }

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                BlockType material = (BlockType)construction_workspace_get(workspace, x, y, z);
                if (material == BLOCK_AIR) continue;
                if (out_occupancy) sdk_construction_occupancy_set(out_occupancy, x, y, z, 1);
                count++;
                if ((uint8_t)x < min_v[0]) min_v[0] = (uint8_t)x;
                if ((uint8_t)y < min_v[1]) min_v[1] = (uint8_t)y;
                if ((uint8_t)z < min_v[2]) min_v[2] = (uint8_t)z;
                if ((uint8_t)(x + 1) > max_v[0]) max_v[0] = (uint8_t)(x + 1);
                if ((uint8_t)(y + 1) > max_v[1]) max_v[1] = (uint8_t)(y + 1);
                if ((uint8_t)(z + 1) > max_v[2]) max_v[2] = (uint8_t)(z + 1);
                if (first_material == BLOCK_AIR) {
                    first_material = material;
                } else if (first_material != material) {
                    single_material_only = 0;
                }
            }
        }
    }

    if (count == 0u) {
        memset(min_v, 0, sizeof(min_v));
        memset(max_v, 0, sizeof(max_v));
        first_material = BLOCK_AIR;
        single_material_only = 1;
    }

    if (out_count) *out_count = count;
    if (out_min) memcpy(out_min, min_v, sizeof(min_v));
    if (out_max) memcpy(out_max, max_v, sizeof(max_v));
    if (out_single_material) *out_single_material = first_material;
    if (out_single_material_only) *out_single_material_only = single_material_only;
}

static uint32_t construction_hash_bytes(const void* data, size_t len, uint32_t seed)
{
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t h = seed ? seed : 2166136261u;
    for (size_t i = 0u; i < len; ++i) {
        h ^= (uint32_t)bytes[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static int construction_workspace_build_palette(const SdkConstructionWorkspace* workspace,
                                                uint16_t out_palette[SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE],
                                                uint8_t* out_palette_count,
                                                uint8_t out_voxel_palette[SDK_CONSTRUCTION_CELL_VOXELS])
{
    uint8_t palette_count = 0u;

    if (out_palette_count) *out_palette_count = 0u;
    if (!workspace || !out_palette || !out_voxel_palette) return 0;
    memset(out_palette, 0, sizeof(uint16_t) * SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE);
    memset(out_voxel_palette, 0, SDK_CONSTRUCTION_CELL_VOXELS);

    for (int i = 0; i < (int)SDK_CONSTRUCTION_CELL_VOXELS; ++i) {
        uint16_t material = workspace->voxels[i];
        uint8_t palette_index = 0u;

        if ((BlockType)material == BLOCK_AIR) continue;
        for (uint8_t p = 0u; p < palette_count; ++p) {
            if (out_palette[p] == material) {
                palette_index = (uint8_t)(p + 1u);
                break;
            }
        }
        if (palette_index == 0u) {
            if (palette_count >= SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE) {
                return 0;
            }
            out_palette[palette_count] = material;
            palette_index = (uint8_t)(palette_count + 1u);
            palette_count++;
        }
        out_voxel_palette[i] = palette_index;
    }

    if (out_palette_count) *out_palette_count = palette_count;
    return 1;
}

static uint32_t construction_hash_workspace_palette(const uint16_t* palette,
                                                    uint8_t palette_count,
                                                    const uint8_t* voxel_palette)
{
    uint32_t h = construction_hash_bytes(&palette_count, sizeof(palette_count), 2166136261u);
    h = construction_hash_bytes(palette, (size_t)palette_count * sizeof(palette[0]), h);
    h = construction_hash_bytes(voxel_palette, SDK_CONSTRUCTION_CELL_VOXELS, h);
    return h ? h : 1u;
}

static int construction_palette_voxel_get(const SdkConstructionArchetype* archetype, int x, int y, int z)
{
    int voxel_index;

    if (!archetype) return 0;
    if ((unsigned)x >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)y >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)z >= SDK_CONSTRUCTION_CELL_RESOLUTION) {
        return 0;
    }
    voxel_index = y * 256 + z * 16 + x;
    return archetype->voxel_palette[voxel_index] != 0u;
}

static BlockType construction_palette_voxel_material(const SdkConstructionArchetype* archetype, int x, int y, int z)
{
    int voxel_index;
    uint8_t palette_index;

    if (!archetype) return BLOCK_AIR;
    if ((unsigned)x >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)y >= SDK_CONSTRUCTION_CELL_RESOLUTION ||
        (unsigned)z >= SDK_CONSTRUCTION_CELL_RESOLUTION) {
        return BLOCK_AIR;
    }
    voxel_index = y * 256 + z * 16 + x;
    palette_index = archetype->voxel_palette[voxel_index];
    if (palette_index == 0u || palette_index > archetype->palette_count) return BLOCK_AIR;
    return (BlockType)archetype->palette[palette_index - 1u];
}

static void construction_archetype_rebuild_cached(SdkConstructionArchetype* archetype)
{
    uint32_t top_counts[SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE] = { 0u };
    uint32_t exposed_counts[SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE] = { 0u };
    uint8_t min_bounds[3] = { 16u, 16u, 16u };
    uint8_t max_bounds[3] = { 0u, 0u, 0u };
    uint16_t occupied_count = 0u;

    if (!archetype) return;
    construction_clear_face_masks(archetype->face_masks);
    memset(archetype->bounds_min, 0, sizeof(archetype->bounds_min));
    memset(archetype->bounds_max, 0, sizeof(archetype->bounds_max));
    archetype->occupied_count = 0u;
    archetype->display_material = (uint8_t)BLOCK_AIR;

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                int voxel_index = y * 256 + z * 16 + x;
                uint8_t palette_index = archetype->voxel_palette[voxel_index];
                int exposed = 0;

                if (palette_index == 0u) continue;
                occupied_count++;
                if ((uint8_t)x < min_bounds[0]) min_bounds[0] = (uint8_t)x;
                if ((uint8_t)y < min_bounds[1]) min_bounds[1] = (uint8_t)y;
                if ((uint8_t)z < min_bounds[2]) min_bounds[2] = (uint8_t)z;
                if ((uint8_t)(x + 1) > max_bounds[0]) max_bounds[0] = (uint8_t)(x + 1);
                if ((uint8_t)(y + 1) > max_bounds[1]) max_bounds[1] = (uint8_t)(y + 1);
                if ((uint8_t)(z + 1) > max_bounds[2]) max_bounds[2] = (uint8_t)(z + 1);

                if (x == 0) construction_face_mask_set(archetype->face_masks, FACE_NEG_X, y, z);
                if (x == 15) construction_face_mask_set(archetype->face_masks, FACE_POS_X, y, z);
                if (y == 0) construction_face_mask_set(archetype->face_masks, FACE_NEG_Y, x, z);
                if (y == 15) construction_face_mask_set(archetype->face_masks, FACE_POS_Y, x, z);
                if (z == 0) construction_face_mask_set(archetype->face_masks, FACE_NEG_Z, x, y);
                if (z == 15) construction_face_mask_set(archetype->face_masks, FACE_POS_Z, x, y);

                if (x == 0 || !construction_palette_voxel_get(archetype, x - 1, y, z)) exposed = 1;
                if (x == 15 || !construction_palette_voxel_get(archetype, x + 1, y, z)) exposed = 1;
                if (y == 0 || !construction_palette_voxel_get(archetype, x, y - 1, z)) exposed = 1;
                if (y == 15 || !construction_palette_voxel_get(archetype, x, y + 1, z)) {
                    exposed = 1;
                    top_counts[palette_index - 1u]++;
                }
                if (z == 0 || !construction_palette_voxel_get(archetype, x, y, z - 1)) exposed = 1;
                if (z == 15 || !construction_palette_voxel_get(archetype, x, y, z + 1)) exposed = 1;
                if (exposed) exposed_counts[palette_index - 1u]++;
            }
        }
    }

    archetype->occupied_count = occupied_count;
    if (occupied_count == 0u) return;
    memcpy(archetype->bounds_min, min_bounds, sizeof(min_bounds));
    memcpy(archetype->bounds_max, max_bounds, sizeof(max_bounds));

    {
        uint32_t best_count = 0u;
        uint8_t best_index = 0u;
        for (uint8_t i = 0u; i < archetype->palette_count; ++i) {
            if (top_counts[i] > best_count) {
                best_count = top_counts[i];
                best_index = i;
            }
        }
        if (best_count == 0u) {
            for (uint8_t i = 0u; i < archetype->palette_count; ++i) {
                if (exposed_counts[i] > best_count) {
                    best_count = exposed_counts[i];
                    best_index = i;
                }
            }
        }
        if (archetype->palette_count > 0u) {
            archetype->display_material = (uint8_t)archetype->palette[best_index];
        }
    }
}

static int construction_archetype_content_equals(const SdkConstructionArchetype* archetype,
                                                 const uint16_t* palette,
                                                 uint8_t palette_count,
                                                 const uint8_t* voxel_palette)
{
    if (!archetype || !palette || !voxel_palette) return 0;
    if (archetype->palette_count != palette_count) return 0;
    if (memcmp(archetype->palette, palette, (size_t)palette_count * sizeof(palette[0])) != 0) return 0;
    return memcmp(archetype->voxel_palette, voxel_palette, SDK_CONSTRUCTION_CELL_VOXELS) == 0;
}

static int construction_registry_ensure_slot_capacity(SdkConstructionArchetypeRegistry* registry, uint32_t needed)
{
    uint32_t new_capacity;
    SdkConstructionArchetype** grown;

    if (!registry) return 0;
    if (needed <= registry->slot_capacity) return 1;
    new_capacity = registry->slot_capacity ? registry->slot_capacity * 2u : 32u;
    while (new_capacity < needed) new_capacity *= 2u;
    grown = (SdkConstructionArchetype**)realloc(registry->slots, (size_t)new_capacity * sizeof(*grown));
    if (!grown) return 0;
    memset(grown + registry->slot_capacity, 0, (size_t)(new_capacity - registry->slot_capacity) * sizeof(*grown));
    registry->slots = grown;
    registry->slot_capacity = new_capacity;
    return 1;
}

_Requires_lock_not_held_(registry->lock)
_Acquires_shared_lock_(registry->lock)
static void construction_registry_lock_shared(_In_ const SdkConstructionArchetypeRegistry* registry)
{
    _Analysis_assume_(registry != NULL);
    AcquireSRWLockShared((PSRWLOCK)&registry->lock);
}

_Requires_lock_held_(registry->lock)
_Releases_shared_lock_(registry->lock)
static void construction_registry_unlock_shared(_In_ const SdkConstructionArchetypeRegistry* registry)
{
    _Analysis_assume_(registry != NULL);
    ReleaseSRWLockShared((PSRWLOCK)&registry->lock);
}

_Requires_lock_not_held_(registry->lock)
_Acquires_exclusive_lock_(registry->lock)
static void construction_registry_lock_exclusive(_In_ SdkConstructionArchetypeRegistry* registry)
{
    _Analysis_assume_(registry != NULL);
    AcquireSRWLockExclusive(&registry->lock);
}

_Requires_lock_held_(registry->lock)
_Releases_exclusive_lock_(registry->lock)
static void construction_registry_unlock_exclusive(_In_ SdkConstructionArchetypeRegistry* registry)
{
    _Analysis_assume_(registry != NULL);
    ReleaseSRWLockExclusive(&registry->lock);
}

static SdkConstructionArchetype* construction_registry_get_mutable_locked(
    SdkConstructionArchetypeRegistry* registry,
    SdkConstructionArchetypeId id)
{
    uint32_t slot_index;

    if (!registry || id == SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID) return NULL;
    slot_index = (uint32_t)(id - 1u);
    if (slot_index >= registry->slot_count) return NULL;
    return registry->slots[slot_index];
}

static const SdkConstructionArchetype* construction_registry_get_const_locked(
    const SdkConstructionArchetypeRegistry* registry,
    SdkConstructionArchetypeId id)
{
    uint32_t slot_index;

    if (!registry || id == SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID) return NULL;
    slot_index = (uint32_t)(id - 1u);
    if (slot_index >= registry->slot_count) return NULL;
    return registry->slots[slot_index];
}

static int construction_registry_verify_consistency_locked(const SdkConstructionArchetypeRegistry* registry)
{
#ifdef _DEBUG
    uint32_t counted_active = 0u;

    if (!registry) return 0;
    for (uint32_t slot_index = 0u; slot_index < registry->slot_count; ++slot_index) {
        const SdkConstructionArchetype* archetype = registry->slots[slot_index];
        if (!archetype) continue;
        counted_active++;
        if (archetype->id != (SdkConstructionArchetypeId)(slot_index + 1u)) {
            return 0;
        }
    }
    if (counted_active != registry->active_count) {
        return 0;
    }
    if (registry->active_count == 0u) {
        return registry->hash_table == NULL && registry->hash_capacity == 0u;
    }
    if (!registry->hash_table || registry->hash_capacity == 0u) {
        return 0;
    }
    for (uint32_t i = 0u; i < registry->hash_capacity; ++i) {
        SdkConstructionArchetypeId id = registry->hash_table[i];
        if (id == SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID) continue;
        if (!construction_registry_get_const_locked(registry, id)) {
            return 0;
        }
    }
#else
    (void)registry;
#endif
    return 1;
}

static int construction_registry_rebuild_hash_locked(SdkConstructionArchetypeRegistry* registry)
{
    uint32_t hash_capacity;
    uint32_t old_hash_capacity;
    SdkConstructionArchetypeId* hash_table;
    SdkConstructionArchetypeId* old_hash_table;

    if (!registry) return 0;
    old_hash_table = registry->hash_table;
    old_hash_capacity = registry->hash_capacity;
    registry->hash_table = NULL;
    registry->hash_capacity = 0u;
    if (registry->active_count == 0u) {
        free(old_hash_table);
        return construction_registry_verify_consistency_locked(registry);
    }

    hash_capacity = 128u;
    while (hash_capacity < registry->active_count * 4u) hash_capacity <<= 1u;
    hash_table = (SdkConstructionArchetypeId*)calloc(hash_capacity, sizeof(*hash_table));
    if (!hash_table) {
        registry->hash_table = old_hash_table;
        registry->hash_capacity = old_hash_capacity;
        return 0;
    }
    for (uint32_t i = 0u; i < hash_capacity; ++i) {
        hash_table[i] = SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID;
    }

    for (uint32_t slot_index = 0u; slot_index < registry->slot_count; ++slot_index) {
        SdkConstructionArchetype* archetype = registry->slots[slot_index];
        if (!archetype) continue;
        {
            uint32_t mask = hash_capacity - 1u;
            uint32_t pos = archetype->hash & mask;
            while (hash_table[pos] != SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID) {
                pos = (pos + 1u) & mask;
            }
            hash_table[pos] = archetype->id;
        }
    }
    registry->hash_table = hash_table;
    registry->hash_capacity = hash_capacity;
    free(old_hash_table);
    return construction_registry_verify_consistency_locked(registry);
}

static int construction_registry_rebuild_hash(SdkConstructionArchetypeRegistry* registry)
{
    int ok;

    if (!registry) return 0;
    construction_registry_lock_exclusive(registry);
    ok = construction_registry_rebuild_hash_locked(registry);
    construction_registry_unlock_exclusive(registry);
    return ok;
}

static void construction_registry_clear_locked(SdkConstructionArchetypeRegistry* registry)
{
    if (!registry) return;
    if (registry->slots) {
        for (uint32_t i = 0u; i < registry->slot_count; ++i) {
            free(registry->slots[i]);
            registry->slots[i] = NULL;
        }
    }
    free(registry->slots);
    registry->slots = NULL;
    registry->slot_count = 0u;
    registry->slot_capacity = 0u;
    registry->active_count = 0u;
    free(registry->hash_table);
    registry->hash_table = NULL;
    registry->hash_capacity = 0u;
    registry->revision++;
}

static int construction_registry_acquire_id(SdkConstructionArchetypeRegistry* registry,
                                            SdkConstructionArchetypeId id)
{
    SdkConstructionArchetype* archetype;
    int ok = 0;

    if (!registry) return 0;
    construction_registry_lock_exclusive(registry);
    archetype = construction_registry_get_mutable_locked(registry, id);
    if (archetype) {
        InterlockedIncrement((volatile LONG*)&archetype->refcount);
        ok = 1;
    }
    construction_registry_unlock_exclusive(registry);
    return ok;
}

static void construction_registry_release_id(SdkConstructionArchetypeRegistry* registry,
                                             SdkConstructionArchetypeId id)
{
    SdkConstructionArchetype* archetype;

    if (!registry) return;
    construction_registry_lock_exclusive(registry);
    archetype = construction_registry_get_mutable_locked(registry, id);
    if (!archetype) {
        construction_registry_unlock_exclusive(registry);
        return;
    }
    if (InterlockedDecrement((volatile LONG*)&archetype->refcount) == 0) {
        uint32_t slot_index = (uint32_t)(id - 1u);
        free(archetype);
        registry->slots[slot_index] = NULL;
        if (registry->active_count > 0u) registry->active_count--;
        registry->revision++;
        construction_registry_rebuild_hash_locked(registry);
    }
    construction_registry_unlock_exclusive(registry);
}

static int construction_registry_find_match_locked(const SdkConstructionArchetypeRegistry* registry,
                                                   uint32_t hash,
                                                   const uint16_t* palette,
                                                   uint8_t palette_count,
                                                   const uint8_t* voxel_palette,
                                                   SdkConstructionArchetypeId* out_id)
{
    uint32_t mask;
    uint32_t pos;

    if (out_id) *out_id = SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID;
    if (!registry || !registry->hash_table || registry->hash_capacity == 0u) return 0;
    mask = registry->hash_capacity - 1u;
    pos = hash & mask;
    for (uint32_t probe = 0u; probe < registry->hash_capacity; ++probe) {
        SdkConstructionArchetypeId id = registry->hash_table[pos];
        const SdkConstructionArchetype* archetype;
        if (id == SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID) return 0;
        archetype = construction_registry_get_const_locked(registry, id);
        if (archetype && archetype->hash == hash &&
            construction_archetype_content_equals(archetype, palette, palette_count, voxel_palette)) {
            if (out_id) *out_id = id;
            return 1;
        }
        pos = (pos + 1u) & mask;
    }
    return 0;
}

static int construction_registry_find_match(const SdkConstructionArchetypeRegistry* registry,
                                            uint32_t hash,
                                            const uint16_t* palette,
                                            uint8_t palette_count,
                                            const uint8_t* voxel_palette,
                                            SdkConstructionArchetypeId* out_id)
{
    int found;

    if (!registry) {
        if (out_id) *out_id = SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID;
        return 0;
    }
    construction_registry_lock_shared(registry);
    found = construction_registry_find_match_locked(registry, hash, palette, palette_count, voxel_palette, out_id);
    construction_registry_unlock_shared(registry);
    return found;
}

static int construction_registry_resolve_workspace(SdkConstructionArchetypeRegistry* registry,
                                                   const SdkConstructionWorkspace* workspace,
                                                   SdkConstructionArchetypeId* out_id)
{
    uint16_t palette[SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE];
    uint8_t palette_count = 0u;
    uint8_t voxel_palette[SDK_CONSTRUCTION_CELL_VOXELS];
    uint32_t hash;
    SdkConstructionArchetypeId archetype_id = SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID;
    SdkConstructionArchetype* archetype;
    uint32_t slot_index = UINT32_MAX;

    if (out_id) *out_id = SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID;
    if (!registry || !workspace) return 0;
    if (!construction_workspace_build_palette(workspace, palette, &palette_count, voxel_palette)) return 0;
    if (palette_count == 0u) return 0;

    hash = construction_hash_workspace_palette(palette, palette_count, voxel_palette);
    construction_registry_lock_exclusive(registry);

    if (construction_registry_find_match_locked(registry,
                                                hash,
                                                palette,
                                                palette_count,
                                                voxel_palette,
                                                &archetype_id)) {
        SdkConstructionArchetype* existing = construction_registry_get_mutable_locked(registry, archetype_id);
        if (existing) {
            InterlockedIncrement((volatile LONG*)&existing->refcount);
            if (out_id) *out_id = archetype_id;
            construction_registry_unlock_exclusive(registry);
            return 1;
        }
    }

    for (uint32_t i = 0u; i < registry->slot_count; ++i) {
        if (!registry->slots[i]) {
            slot_index = i;
            break;
        }
    }
    if (slot_index == UINT32_MAX) {
        slot_index = registry->slot_count;
        if (!construction_registry_ensure_slot_capacity(registry, slot_index + 1u)) {
            construction_registry_unlock_exclusive(registry);
            return 0;
        }
        registry->slot_count = slot_index + 1u;
    }

    archetype = (SdkConstructionArchetype*)calloc(1, sizeof(*archetype));
    if (!archetype) {
        construction_registry_unlock_exclusive(registry);
        return 0;
    }
    archetype->id = (SdkConstructionArchetypeId)(slot_index + 1u);
    archetype->hash = hash;
    archetype->refcount = 1u;
    archetype->palette_count = palette_count;
    memcpy(archetype->palette, palette, (size_t)palette_count * sizeof(palette[0]));
    memcpy(archetype->voxel_palette, voxel_palette, sizeof(voxel_palette));
    construction_archetype_rebuild_cached(archetype);

    registry->slots[slot_index] = archetype;
    registry->active_count++;
    registry->revision++;
    if (!construction_registry_rebuild_hash_locked(registry)) {
        registry->slots[slot_index] = NULL;
        registry->active_count--;
        free(archetype);
        construction_registry_unlock_exclusive(registry);
        return 0;
    }

    if (out_id) *out_id = archetype->id;
    construction_registry_unlock_exclusive(registry);
    return 1;
}

SdkConstructionArchetypeRegistry* sdk_construction_registry_create(void)
{
    SdkConstructionArchetypeRegistry* registry =
        (SdkConstructionArchetypeRegistry*)calloc(1, sizeof(SdkConstructionArchetypeRegistry));
    if (!registry) return NULL;
    registry->slots = (SdkConstructionArchetype**)calloc(SDK_CONSTRUCTION_REGISTRY_PREALLOCATED_SLOTS,
                                                         sizeof(*registry->slots));
    if (!registry->slots) {
        free(registry);
        return NULL;
    }
    registry->slot_capacity = SDK_CONSTRUCTION_REGISTRY_PREALLOCATED_SLOTS;
    InitializeSRWLock(&registry->lock);
    return registry;
}

void sdk_construction_registry_clear(SdkConstructionArchetypeRegistry* registry)
{
    if (!registry) return;
    construction_registry_lock_exclusive(registry);
    construction_registry_clear_locked(registry);
    construction_registry_unlock_exclusive(registry);
}

void sdk_construction_registry_free(SdkConstructionArchetypeRegistry* registry)
{
    if (!registry) return;
    sdk_construction_registry_clear(registry);
    free(registry);
}

void sdk_construction_chunk_set_registry(SdkChunk* chunk, SdkConstructionArchetypeRegistry* registry)
{
    if (!chunk) return;
    chunk->construction_registry = registry;
    if (chunk->construction_cells) {
        chunk->construction_cells->registry = registry;
    }
}

static SdkConstructionOverflowInstance* construction_store_find(SdkConstructionCellStore* store, uint32_t local_index)
{
    uint32_t i;
    if (!store) return NULL;
    for (i = 0; i < store->count; ++i) {
        if (store->entries[i].local_index == local_index) return &store->entries[i];
    }
    return NULL;
}

static const SdkConstructionOverflowInstance* construction_store_find_const(const SdkConstructionCellStore* store,
                                                                            uint32_t local_index)
{
    uint32_t i;
    if (!store) return NULL;
    for (i = 0; i < store->count; ++i) {
        if (store->entries[i].local_index == local_index) return &store->entries[i];
    }
    return NULL;
}

static int construction_store_reserve(SdkConstructionCellStore* store, uint32_t needed)
{
    uint32_t new_capacity;
    SdkConstructionOverflowInstance* grown;

    if (!store) return 0;
    if (needed <= store->capacity) return 1;
    new_capacity = store->capacity ? store->capacity * 2u : 8u;
    while (new_capacity < needed) new_capacity *= 2u;
    grown = (SdkConstructionOverflowInstance*)realloc(store->entries,
                                                      (size_t)new_capacity * sizeof(SdkConstructionOverflowInstance));
    if (!grown) return 0;
    memset(grown + store->capacity, 0,
           (size_t)(new_capacity - store->capacity) * sizeof(SdkConstructionOverflowInstance));
    store->entries = grown;
    store->capacity = new_capacity;
    return 1;
}

static SdkConstructionCellStore* construction_store_ensure(SdkChunk* chunk)
{
    if (!chunk) return NULL;
    if (!chunk->construction_cells) {
        chunk->construction_cells = (SdkConstructionCellStore*)calloc(1, sizeof(*chunk->construction_cells));
        if (chunk->construction_cells) {
            chunk->construction_cells->registry = chunk->construction_registry;
        }
    }
    return chunk->construction_cells;
}

static void construction_store_remove(SdkChunk* chunk, uint32_t local_index)
{
    uint32_t i;
    SdkConstructionCellStore* store;

    if (!chunk || !chunk->construction_cells) return;
    store = chunk->construction_cells;
    for (i = 0; i < store->count; ++i) {
        if (store->entries[i].local_index != local_index) continue;
        if (store->entries[i].archetype_id != SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID) {
            construction_registry_release_id(store->registry, store->entries[i].archetype_id);
        }
        if (i + 1u < store->count) {
            memmove(&store->entries[i], &store->entries[i + 1u],
                    (size_t)(store->count - i - 1u) * sizeof(store->entries[0]));
        }
        store->count--;
        break;
    }
    if (store->count == 0u) {
        free(store->entries);
        store->entries = NULL;
        store->capacity = 0u;
    }
}

static int construction_store_set_instance(SdkChunk* chunk,
                                           uint32_t local_index,
                                           SdkConstructionArchetypeId archetype_id)
{
    SdkConstructionCellStore* store;
    SdkConstructionOverflowInstance* entry;

    if (!chunk) return 0;
    store = construction_store_ensure(chunk);
    if (!store) return 0;
    entry = construction_store_find(store, local_index);

    if (archetype_id == SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID) {
        construction_store_remove(chunk, local_index);
        return 1;
    }

    if (!entry) {
        if (!construction_store_reserve(store, store->count + 1u)) return 0;
        entry = &store->entries[store->count++];
        memset(entry, 0, sizeof(*entry));
        entry->local_index = local_index;
    }

    if (entry->archetype_id == archetype_id) {
        return 1;
    }
    if (!construction_registry_acquire_id(store->registry, archetype_id)) {
        return 0;
    }
    if (entry->archetype_id != SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID) {
        construction_registry_release_id(store->registry, entry->archetype_id);
    }
    entry->archetype_id = archetype_id;
    return 1;
}

void sdk_construction_store_free(SdkConstructionCellStore* store)
{
    if (!store) return;
    if (store->registry) {
        for (uint32_t i = 0u; i < store->count; ++i) {
            if (store->entries[i].archetype_id != SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID) {
                construction_registry_release_id(store->registry, store->entries[i].archetype_id);
            }
        }
    }
    free(store->entries);
    free(store);
}

SdkConstructionCellStore* sdk_construction_store_clone(const SdkConstructionCellStore* src)
{
    SdkConstructionCellStore* copy;
    const SdkConstructionOverflowInstance* src_entries;
    uint32_t entry_count;
    if (!src) return NULL;
    copy = (SdkConstructionCellStore*)calloc(1, sizeof(*copy));
    if (!copy) return NULL;
    copy->registry = src->registry;
    src_entries = src->entries;
    entry_count = src->count;
    if (entry_count > 0u && !src_entries) {
        free(copy);
        return NULL;
    }
    if (entry_count > 0u) {
        SdkConstructionOverflowInstance* dst_entries;
        copy->entries = (SdkConstructionOverflowInstance*)calloc(entry_count, sizeof(SdkConstructionOverflowInstance));
        if (!copy->entries) {
            free(copy);
            return NULL;
        }
        dst_entries = copy->entries;
        for (uint32_t i = 0u; i < entry_count; ++i) {
            SdkConstructionOverflowInstance src_entry = src_entries[i];
            dst_entries[i].local_index = src_entry.local_index;
            dst_entries[i].archetype_id = src_entry.archetype_id;
            if (src_entry.archetype_id != SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID &&
                !construction_registry_acquire_id(copy->registry, src_entry.archetype_id)) {
                copy->count = i;
                sdk_construction_store_free(copy);
                return NULL;
            }
            copy->count = i + 1u;
        }
        copy->capacity = entry_count;
    }
    return copy;
}

static int construction_axis_from_face(int face)
{
    switch (face) {
        case FACE_NEG_X:
        case FACE_POS_X: return 0;
        case FACE_NEG_Y:
        case FACE_POS_Y: return 1;
        case FACE_NEG_Z:
        case FACE_POS_Z: return 2;
        default: return 0;
    }
}

static int construction_face_negative_side(int face)
{
    return face == FACE_NEG_X || face == FACE_NEG_Y || face == FACE_NEG_Z;
}

static BlockType construction_decode_cell_material(const SdkChunk* chunk,
                                                   int lx, int ly, int lz,
                                                   SdkWorldCellCode code)
{
    uint32_t local_index;
    const SdkConstructionOverflowInstance* entry;
    const SdkConstructionArchetype* archetype;

    if (sdk_world_cell_is_full_block(code)) {
        return sdk_world_cell_decode_full_block(code);
    }
    if (sdk_world_cell_is_inline_construction(code)) {
        return sdk_world_cell_inline_material(code);
    }
    if (!chunk || !chunk->construction_cells) return BLOCK_AIR;
    local_index = construction_local_index(lx, ly, lz);
    entry = construction_store_find_const(chunk->construction_cells, local_index);
    if (!entry) return BLOCK_AIR;
    construction_registry_lock_shared(chunk->construction_registry);
    archetype = construction_registry_get_const_locked(chunk->construction_registry, entry->archetype_id);
    if (!archetype) {
        construction_registry_unlock_shared(chunk->construction_registry);
        return BLOCK_AIR;
    }
    {
        BlockType display_material = (BlockType)archetype->display_material;
        construction_registry_unlock_shared(chunk->construction_registry);
        return display_material;
    }
}

static int construction_cell_payload_from_code(const SdkChunk* chunk,
                                               int lx, int ly, int lz,
                                               SdkWorldCellCode code,
                                               BlockType* out_material,
                                               uint64_t out_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                               SdkInlineConstructionProfile* out_profile)
{
    const SdkConstructionOverflowInstance* entry;
    const SdkConstructionArchetype* archetype;
    uint32_t local_index;

    if (out_material) *out_material = BLOCK_AIR;
    if (out_profile) *out_profile = SDK_INLINE_PROFILE_NONE;
    if (out_occupancy) sdk_construction_clear_occupancy(out_occupancy);

    if (sdk_world_cell_is_full_block(code)) {
        BlockType material = sdk_world_cell_decode_full_block(code);
        if (material == BLOCK_AIR) return 0;
        if (out_material) *out_material = material;
        if (out_occupancy) sdk_construction_fill_full_occupancy(out_occupancy);
        return 1;
    }
    if (sdk_world_cell_is_inline_construction(code)) {
        BlockType material = sdk_world_cell_inline_material(code);
        SdkInlineConstructionProfile profile = sdk_world_cell_inline_profile(code);
        if (material == BLOCK_AIR || profile == SDK_INLINE_PROFILE_NONE) return 0;
        if (out_material) *out_material = material;
        if (out_profile) *out_profile = profile;
        if (out_occupancy) sdk_construction_fill_profile_occupancy(profile, out_occupancy);
        return 1;
    }
    if (!chunk || !chunk->construction_cells) return 0;
    local_index = construction_local_index(lx, ly, lz);
    entry = construction_store_find_const(chunk->construction_cells, local_index);
    if (!entry) return 0;
    construction_registry_lock_shared(chunk->construction_registry);
    archetype = construction_registry_get_const_locked(chunk->construction_registry, entry->archetype_id);
    if (!archetype || archetype->occupied_count == 0u) {
        construction_registry_unlock_shared(chunk->construction_registry);
        return 0;
    }
    if (out_material) *out_material = (BlockType)archetype->display_material;
    if (out_occupancy) {
        sdk_construction_clear_occupancy(out_occupancy);
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    if (!construction_palette_voxel_get(archetype, x, y, z)) continue;
                    sdk_construction_occupancy_set(out_occupancy, x, y, z, 1);
                }
            }
        }
    }
    construction_registry_unlock_shared(chunk->construction_registry);
    return 1;
}

static int construction_chunk_local_from_world(const SdkChunkManager* cm,
                                               int wx, int wy, int wz,
                                               SdkChunk** out_chunk,
                                               int* out_lx,
                                               int* out_lz)
{
    int cx;
    int cz;
    SdkChunk* chunk;

    if (out_chunk) *out_chunk = NULL;
    if (wy < 0 || wy >= CHUNK_HEIGHT) return 0;
    cx = sdk_world_to_chunk_x(wx);
    cz = sdk_world_to_chunk_z(wz);
    chunk = sdk_chunk_manager_get_chunk((SdkChunkManager*)cm, cx, cz);
    if (!chunk) return 0;
    if (out_chunk) *out_chunk = chunk;
    if (out_lx) *out_lx = sdk_world_to_local_x(wx, cx);
    if (out_lz) *out_lz = sdk_world_to_local_z(wz, cz);
    return 1;
}

static void construction_face_tangent_axes(int face, int* out_axis_u, int* out_axis_v)
{
    int axis_u = 0;
    int axis_v = 1;

    switch (face) {
        case FACE_NEG_X:
        case FACE_POS_X:
            axis_u = 2;
            axis_v = 1;
            break;
        case FACE_NEG_Y:
        case FACE_POS_Y:
            axis_u = 0;
            axis_v = 2;
            break;
        case FACE_NEG_Z:
        case FACE_POS_Z:
        default:
            axis_u = 0;
            axis_v = 1;
            break;
    }

    if (out_axis_u) *out_axis_u = axis_u;
    if (out_axis_v) *out_axis_v = axis_v;
}

static void construction_face_local_hit_uv(int wx, int wy, int wz,
                                           int face,
                                           float hit_x, float hit_y, float hit_z,
                                           int* out_u, int* out_v)
{
    float local_x = hit_x - (float)wx;
    float local_y = hit_y - (float)wy;
    float local_z = hit_z - (float)wz;
    float coord_u = 0.0f;
    float coord_v = 0.0f;
    int u;
    int v;

    if (local_x < 0.0f) local_x = 0.0f;
    if (local_x > 0.9999f) local_x = 0.9999f;
    if (local_y < 0.0f) local_y = 0.0f;
    if (local_y > 0.9999f) local_y = 0.9999f;
    if (local_z < 0.0f) local_z = 0.0f;
    if (local_z > 0.9999f) local_z = 0.9999f;

    switch (face) {
        case FACE_NEG_X:
        case FACE_POS_X:
            coord_u = local_z;
            coord_v = local_y;
            break;
        case FACE_NEG_Y:
        case FACE_POS_Y:
            coord_u = local_x;
            coord_v = local_z;
            break;
        case FACE_NEG_Z:
        case FACE_POS_Z:
        default:
            coord_u = local_x;
            coord_v = local_y;
            break;
    }

    u = (int)floorf(coord_u * 16.0f);
    v = (int)floorf(coord_v * 16.0f);
    if (u < 0) u = 0;
    if (u > 15) u = 15;
    if (v < 0) v = 0;
    if (v > 15) v = 15;

    if (out_u) *out_u = u;
    if (out_v) *out_v = v;
}

static void construction_shift_payload_axes(const SdkConstructionItemPayload* src,
                                            int shift_x, int shift_y, int shift_z,
                                            SdkConstructionItemPayload* out_payload)
{
    uint64_t shifted[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];

    if (!out_payload) return;
    memset(out_payload, 0, sizeof(*out_payload));
    if (!src || src->occupied_count == 0u) return;

    sdk_construction_clear_occupancy(shifted);
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                int dx;
                int dy;
                int dz;

                if (!sdk_construction_occupancy_get(src->occupancy, x, y, z)) continue;
                dx = x + shift_x;
                dy = y + shift_y;
                dz = z + shift_z;
                if ((unsigned)dx >= 16u || (unsigned)dy >= 16u || (unsigned)dz >= 16u) {
                    continue;
                }
                sdk_construction_occupancy_set(shifted, dx, dy, dz, 1);
            }
        }
    }
    construction_payload_from_occupancy((BlockType)src->material, shifted, out_payload);
}

static int construction_prepare_merged_payload(const SdkChunkManager* cm,
                                               int wx, int wy, int wz,
                                               const SdkConstructionItemPayload* payload,
                                               SdkConstructionWorkspace* out_merged,
                                               int* out_has_existing)
{
    SdkChunk* chunk;
    int lx;
    int lz;
    SdkWorldCellCode code;
    uint64_t merged_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    SdkConstructionWorkspace incoming_workspace;

    if (out_has_existing) *out_has_existing = 0;
    if (!out_merged) return 0;
    construction_workspace_clear(out_merged);
    if (!cm || !payload || sdk_construction_payload_empty(payload)) return 0;
    if (!construction_chunk_local_from_world(cm, wx, wy, wz, &chunk, &lx, &lz)) return 0;

    code = sdk_chunk_get_cell_code(chunk, lx, wy, lz);
    if (sdk_world_cell_is_full_block(code) && sdk_world_cell_decode_full_block(code) != BLOCK_AIR) {
        return 0;
    }

    if (sdk_construction_chunk_get_workspace(chunk, lx, wy, lz, out_merged)) {
        if (out_has_existing) *out_has_existing = 1;
    }

    construction_workspace_from_payload(payload, &incoming_workspace);
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                BlockType incoming_material = (BlockType)construction_workspace_get(&incoming_workspace, x, y, z);
                BlockType existing_material;
                if (incoming_material == BLOCK_AIR) continue;
                existing_material = (BlockType)construction_workspace_get(out_merged, x, y, z);
                if (existing_material != BLOCK_AIR && existing_material != incoming_material) {
                    return 0;
                }
                construction_workspace_set(out_merged, x, y, z, incoming_material);
            }
        }
    }

    construction_workspace_to_occupancy(out_merged, merged_occupancy, NULL, NULL, NULL, NULL, NULL);
    if (!construction_cell_supported(cm, wx, wy, wz, merged_occupancy)) {
        return 0;
    }
    return 1;
}

int sdk_construction_chunk_get_cell_payload(const SdkChunk* chunk,
                                            int lx, int ly, int lz,
                                            BlockType* out_material,
                                            uint64_t out_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                            SdkInlineConstructionProfile* out_profile)
{
    if (!chunk) return 0;
    return construction_cell_payload_from_code(chunk,
                                               lx, ly, lz,
                                               sdk_chunk_get_cell_code(chunk, lx, ly, lz),
                                               out_material, out_occupancy, out_profile);
}

int sdk_construction_chunk_get_workspace(const SdkChunk* chunk,
                                         int lx, int ly, int lz,
                                         SdkConstructionWorkspace* out_workspace)
{
    SdkWorldCellCode code;
    const SdkConstructionOverflowInstance* entry;
    const SdkConstructionArchetype* archetype;
    uint32_t local_index;

    if (!out_workspace) return 0;
    construction_workspace_clear(out_workspace);
    if (!chunk) return 0;

    code = sdk_chunk_get_cell_code(chunk, lx, ly, lz);
    if (sdk_world_cell_is_full_block(code)) {
        BlockType material = sdk_world_cell_decode_full_block(code);
        if (material == BLOCK_AIR) return 0;
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    construction_workspace_set(out_workspace, x, y, z, material);
                }
            }
        }
        return 1;
    }
    if (sdk_world_cell_is_inline_construction(code)) {
        uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
        BlockType material = sdk_world_cell_inline_material(code);
        SdkInlineConstructionProfile profile = sdk_world_cell_inline_profile(code);
        if (material == BLOCK_AIR || profile == SDK_INLINE_PROFILE_NONE) return 0;
        sdk_construction_fill_profile_occupancy(profile, occupancy);
        construction_workspace_from_occupancy(occupancy, material, out_workspace);
        return 1;
    }
    if (!chunk->construction_cells || !chunk->construction_registry) return 0;
    local_index = construction_local_index(lx, ly, lz);
    entry = construction_store_find_const(chunk->construction_cells, local_index);
    if (!entry) return 0;
    construction_registry_lock_shared(chunk->construction_registry);
    archetype = construction_registry_get_const_locked(chunk->construction_registry, entry->archetype_id);
    if (!archetype || archetype->occupied_count == 0u) {
        construction_registry_unlock_shared(chunk->construction_registry);
        return 0;
    }
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                construction_workspace_set(out_workspace, x, y, z,
                                           construction_palette_voxel_material(archetype, x, y, z));
            }
        }
    }
    construction_registry_unlock_shared(chunk->construction_registry);
    return 1;
}

BlockType sdk_construction_chunk_get_display_material(const SdkChunk* chunk, int lx, int ly, int lz)
{
    return construction_decode_cell_material(chunk, lx, ly, lz, sdk_chunk_get_cell_code(chunk, lx, ly, lz));
}

int sdk_construction_chunk_cell_has_occupancy(const SdkChunk* chunk, int lx, int ly, int lz)
{
    return sdk_construction_chunk_get_display_material(chunk, lx, ly, lz) != BLOCK_AIR;
}

int sdk_construction_chunk_set_cell_payload(SdkChunk* chunk,
                                            int lx, int ly, int lz,
                                            BlockType material,
                                            const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    SdkConstructionWorkspace workspace;
    construction_workspace_from_occupancy(occupancy, material, &workspace);
    return sdk_construction_chunk_set_workspace(chunk, lx, ly, lz, &workspace);
}

int sdk_construction_chunk_set_workspace(SdkChunk* chunk,
                                         int lx, int ly, int lz,
                                         const SdkConstructionWorkspace* workspace)
{
    uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    uint8_t bounds_min[3];
    uint8_t bounds_max[3];
    uint16_t occupied_count = 0u;
    BlockType single_material = BLOCK_AIR;
    int single_material_only = 1;
    SdkInlineConstructionProfile profile = SDK_INLINE_PROFILE_NONE;
    uint32_t local_index;
    SdkWorldCellCode old_code;
    SdkWorldCellCode new_code;
    BlockType old_material;
    BlockType new_display_material = BLOCK_AIR;
    SdkConstructionArchetypeId archetype_id = SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID;

    if (!chunk || !chunk->blocks || !workspace) return 0;
    old_code = sdk_chunk_get_cell_code(chunk, lx, ly, lz);
    old_material = construction_decode_cell_material(chunk, lx, ly, lz, old_code);
    construction_workspace_to_occupancy(workspace, occupancy,
                                        &occupied_count,
                                        bounds_min,
                                        bounds_max,
                                        &single_material,
                                        &single_material_only);
    local_index = construction_local_index(lx, ly, lz);

    if (occupied_count == 0u || single_material == BLOCK_AIR) {
        sdk_chunk_set_cell_code_raw(chunk, lx, ly, lz, sdk_world_cell_encode_full_block(BLOCK_AIR));
        construction_store_remove(chunk, local_index);
        sdk_chunk_recount_far_mesh_excluded_blocks(chunk);
        sdk_chunk_mark_block_dirty(chunk, lx, ly, lz);
        return 1;
    }

    if (single_material_only && occupied_count == SDK_CONSTRUCTION_CELL_VOXELS) {
        new_code = sdk_world_cell_encode_full_block(single_material);
        new_display_material = single_material;
        sdk_chunk_set_cell_code_raw(chunk, lx, ly, lz, new_code);
        construction_store_remove(chunk, local_index);
    } else if (single_material_only &&
               sdk_construction_try_match_inline_profile(occupancy, &profile) &&
               sdk_world_cell_can_inline_block(single_material)) {
        new_code = sdk_world_cell_encode_inline_construction(single_material, profile);
        new_display_material = single_material;
        sdk_chunk_set_cell_code_raw(chunk, lx, ly, lz, new_code);
        construction_store_remove(chunk, local_index);
    } else {
        if (!chunk->construction_registry ||
            !construction_registry_resolve_workspace(chunk->construction_registry, workspace, &archetype_id)) {
            return 0;
        }
        construction_registry_lock_shared(chunk->construction_registry);
        {
            const SdkConstructionArchetype* archetype =
                construction_registry_get_const_locked(chunk->construction_registry, archetype_id);
            if (!archetype) {
                construction_registry_unlock_shared(chunk->construction_registry);
                construction_registry_release_id(chunk->construction_registry, archetype_id);
                return 0;
            }
            new_display_material = (BlockType)archetype->display_material;
        }
        construction_registry_unlock_shared(chunk->construction_registry);
        sdk_chunk_set_cell_code_raw(chunk, lx, ly, lz, (SdkWorldCellCode)SDK_WORLD_CELL_OVERFLOW_CODE);
        if (!construction_store_set_instance(chunk, local_index, archetype_id)) {
            construction_registry_release_id(chunk->construction_registry, archetype_id);
            sdk_chunk_set_cell_code_raw(chunk, lx, ly, lz, old_code);
            return 0;
        }
        construction_registry_release_id(chunk->construction_registry, archetype_id);
    }

    if (old_material != new_display_material || old_code != sdk_chunk_get_cell_code(chunk, lx, ly, lz)) {
        sdk_chunk_recount_far_mesh_excluded_blocks(chunk);
    }
    chunk->empty = false;
    sdk_chunk_mark_block_dirty(chunk, lx, ly, lz);
    return 1;
}

void sdk_construction_chunk_clear_cell(SdkChunk* chunk, int lx, int ly, int lz)
{
    uint64_t empty_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    sdk_construction_clear_occupancy(empty_occupancy);
    sdk_construction_chunk_set_cell_payload(chunk, lx, ly, lz, BLOCK_AIR, empty_occupancy);
}

int sdk_construction_world_cell_has_occupancy(const SdkChunkManager* cm, int wx, int wy, int wz)
{
    return sdk_construction_world_cell_display_material(cm, wx, wy, wz) != BLOCK_AIR;
}

BlockType sdk_construction_world_cell_display_material(const SdkChunkManager* cm, int wx, int wy, int wz)
{
    SdkChunk* chunk;
    int lx;
    int lz;
    if (!construction_chunk_local_from_world(cm, wx, wy, wz, &chunk, &lx, &lz)) return BLOCK_AIR;
    return sdk_construction_chunk_get_display_material(chunk, lx, wy, lz);
}

int sdk_construction_world_cell_payload(const SdkChunkManager* cm,
                                        int wx, int wy, int wz,
                                        BlockType* out_material,
                                        uint64_t out_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    SdkChunk* chunk;
    int lx;
    int lz;
    if (!construction_chunk_local_from_world(cm, wx, wy, wz, &chunk, &lx, &lz)) return 0;
    return sdk_construction_chunk_get_cell_payload(chunk, lx, wy, lz, out_material, out_occupancy, NULL);
}

static int construction_boundary_supported_by_below(const SdkChunkManager* cm,
                                                    int wx, int wy, int wz,
                                                    const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    BlockType below_material;
    uint64_t below_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];

    if (wy <= 0) return 1;
    if (!sdk_construction_world_cell_payload(cm, wx, wy - 1, wz, &below_material, below_occupancy)) {
        return 0;
    }
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            if (!sdk_construction_occupancy_get(occupancy, x, 0, z)) continue;
            if (sdk_construction_occupancy_get(below_occupancy, x, 15, z)) return 1;
        }
    }
    return 0;
}

static int construction_horizontal_supports_cell(const SdkChunkManager* cm,
                                                 int wx, int wy, int wz,
                                                 const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    static const int side_offsets[4][3] = {
        { -1, 0, 0 },
        { 1, 0, 0 },
        { 0, 0, -1 },
        { 0, 0, 1 }
    };
    static const int side_faces[4] = { FACE_NEG_X, FACE_POS_X, FACE_NEG_Z, FACE_POS_Z };

    for (int dir = 0; dir < 4; ++dir) {
        int nx = wx + side_offsets[dir][0];
        int ny = wy;
        int nz = wz + side_offsets[dir][2];
        int face = side_faces[dir];
        BlockType neighbor_material;
        uint64_t neighbor_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
        if (!sdk_construction_world_cell_payload(cm, nx, ny, nz, &neighbor_material, neighbor_occupancy)) {
            continue;
        }
        if (!construction_boundary_supported_by_below(cm, nx, ny, nz, neighbor_occupancy)) {
            continue;
        }
        for (int u = 0; u < 16; ++u) {
            for (int v = 0; v < 16; ++v) {
                int ax;
                int ay;
                int az;
                int bx;
                int by;
                int bz;
                if (face == FACE_NEG_X) {
                    ax = 0; ay = u; az = v;
                    bx = 15; by = u; bz = v;
                } else if (face == FACE_POS_X) {
                    ax = 15; ay = u; az = v;
                    bx = 0; by = u; bz = v;
                } else if (face == FACE_NEG_Z) {
                    ax = u; ay = v; az = 0;
                    bx = u; by = v; bz = 15;
                } else {
                    ax = u; ay = v; az = 15;
                    bx = u; by = v; bz = 0;
                }
                if (sdk_construction_occupancy_get(occupancy, ax, ay, az) &&
                    sdk_construction_occupancy_get(neighbor_occupancy, bx, by, bz)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int construction_cell_supported(const SdkChunkManager* cm,
                                       int wx, int wy, int wz,
                                       const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    return construction_boundary_supported_by_below(cm, wx, wy, wz, occupancy) ||
           construction_horizontal_supports_cell(cm, wx, wy, wz, occupancy);
}

static void construction_outcome_clear(SdkConstructionEditOutcome* out_outcome)
{
    if (!out_outcome) return;
    memset(out_outcome, 0, sizeof(*out_outcome));
}

static void construction_outcome_add_fragment(SdkConstructionEditOutcome* out_outcome,
                                              int wx, int wy, int wz,
                                              BlockType material,
                                              const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    SdkConstructionFragment* fragment;

    if (!out_outcome || out_outcome->fragment_count >= SDK_CONSTRUCTION_EDIT_FRAGMENT_MAX) return;
    fragment = &out_outcome->fragments[out_outcome->fragment_count++];
    memset(fragment, 0, sizeof(*fragment));
    fragment->wx = wx;
    fragment->wy = wy;
    fragment->wz = wz;
    construction_payload_from_occupancy(material, occupancy, &fragment->payload);
}

static void construction_outcome_add_workspace_fragments(SdkConstructionEditOutcome* out_outcome,
                                                         int wx, int wy, int wz,
                                                         const SdkConstructionWorkspace* workspace)
{
    uint16_t palette[SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE];
    uint8_t palette_count = 0u;
    uint8_t voxel_palette[SDK_CONSTRUCTION_CELL_VOXELS];

    if (!out_outcome || !workspace) return;
    if (!construction_workspace_build_palette(workspace, palette, &palette_count, voxel_palette)) return;
    for (uint8_t p = 0u; p < palette_count && out_outcome->fragment_count < SDK_CONSTRUCTION_EDIT_FRAGMENT_MAX; ++p) {
        uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
        sdk_construction_clear_occupancy(occupancy);
        for (int i = 0; i < (int)SDK_CONSTRUCTION_CELL_VOXELS; ++i) {
            if (voxel_palette[i] != (uint8_t)(p + 1u)) continue;
            sdk_construction_occupancy_set(occupancy, i & 15, i >> 8, (i >> 4) & 15, 1);
        }
        construction_outcome_add_fragment(out_outcome, wx, wy, wz, (BlockType)palette[p], occupancy);
    }
}

static void construction_resolve_local_support(SdkChunkManager* cm,
                                               int wx, int wy, int wz,
                                               SdkConstructionEditOutcome* out_outcome)
{
    int changed = 1;
    int pass = 0;
    SdkConstructionWorkspace* workspace = construction_workspace_alloc();

    if (!workspace) return;

    while (changed && pass < 4) {
        changed = 0;
        pass++;
        for (int i = 0; i < SDK_CONSTRUCTION_LOCAL_NEIGHBOR_COUNT; ++i) {
            int tx = wx + k_neighbor_offsets[i][0];
            int ty = wy + k_neighbor_offsets[i][1];
            int tz = wz + k_neighbor_offsets[i][2];
            uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
            SdkChunk* chunk;
            int cx;
            int cz;
            int lx;
            int lz;

            if (!construction_chunk_local_from_world(cm, tx, ty, tz, &chunk, &lx, &lz)) continue;
            if (!sdk_construction_chunk_get_workspace(chunk, lx, ty, lz, workspace)) continue;
            construction_workspace_to_occupancy(workspace, occupancy, NULL, NULL, NULL, NULL, NULL);
            if (construction_cell_supported(cm, tx, ty, tz, occupancy)) continue;

            construction_outcome_add_workspace_fragments(out_outcome, tx, ty, tz, workspace);
            cx = sdk_world_to_chunk_x(tx);
            cz = sdk_world_to_chunk_z(tz);
            chunk = sdk_chunk_manager_get_chunk(cm, cx, cz);
            if (!chunk) continue;
            lx = sdk_world_to_local_x(tx, cx);
            lz = sdk_world_to_local_z(tz, cz);
            sdk_construction_chunk_clear_cell(chunk, lx, ty, lz);
            if (out_outcome) out_outcome->changed = 1;
            changed = 1;
        }
    }

    free(workspace);
}

static int construction_tool_shapes_material(ToolClass tool, BlockType material)
{
    const SdkBlockDef* def = sdk_block_get_def(material);
    if (!def) return 0;
    if (tool == TOOL_SAW) {
        return material == BLOCK_LOG || material == BLOCK_PLANKS || def->tool_pref == BLOCK_TOOL_AXE;
    }
    if (tool == TOOL_CHISEL) {
        return def->tool_pref == BLOCK_TOOL_PICKAXE ||
               def->material_class == SDK_BLOCK_CLASS_ROCK ||
               def->material_class == SDK_BLOCK_CLASS_RESOURCE ||
               def->material_class == SDK_BLOCK_CLASS_CONSTRUCTION;
    }
    return 0;
}

static int construction_split_by_face(const uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                      int face,
                                      uint64_t out_remainder[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT],
                                      uint64_t out_fragment[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT])
{
    uint8_t bounds_min[3];
    uint8_t bounds_max[3];
    uint16_t occupied_count = 0u;
    int axis;
    int negative_side;
    int start;
    int end;
    int split;

    if (!occupancy || !out_remainder || !out_fragment) return 0;
    sdk_construction_clear_occupancy(out_remainder);
    sdk_construction_clear_occupancy(out_fragment);
    sdk_construction_occupancy_bounds(occupancy, &occupied_count, bounds_min, bounds_max);
    if (occupied_count == 0u) return 0;

    axis = construction_axis_from_face(face);
    negative_side = construction_face_negative_side(face);
    start = bounds_min[axis];
    end = bounds_max[axis];
    if (end - start <= 1) {
        memcpy(out_fragment, occupancy, sizeof(uint64_t) * SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT);
        return 1;
    }

    split = start + ((end - start) / 2);
    if (split <= start) split = start + 1;
    if (split >= end) split = end - 1;

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                int coord;
                int in_fragment;
                if (!sdk_construction_occupancy_get(occupancy, x, y, z)) continue;
                coord = (axis == 0) ? x : (axis == 1 ? y : z);
                in_fragment = negative_side ? (coord < split) : (coord >= split);
                sdk_construction_occupancy_set(in_fragment ? out_fragment : out_remainder, x, y, z, 1);
            }
        }
    }
    return 1;
}

static int construction_aabb_intersects_box(float min_x, float min_y, float min_z,
                                            float max_x, float max_y, float max_z,
                                            float box_min_x, float box_min_y, float box_min_z,
                                            float box_max_x, float box_max_y, float box_max_z)
{
    return !(box_max_x <= min_x || box_min_x >= max_x ||
             box_max_y <= min_y || box_min_y >= max_y ||
             box_max_z <= min_z || box_min_z >= max_z);
}

static int construction_profile_bounds(SdkInlineConstructionProfile profile,
                                       float* out_min_x, float* out_min_y, float* out_min_z,
                                       float* out_max_x, float* out_max_y, float* out_max_z)
{
    float min_x = 0.0f;
    float min_y = 0.0f;
    float min_z = 0.0f;
    float max_x = 1.0f;
    float max_y = 1.0f;
    float max_z = 1.0f;

    switch (profile) {
        case SDK_INLINE_PROFILE_HALF_NEG_X: max_x = 0.5f; break;
        case SDK_INLINE_PROFILE_HALF_POS_X: min_x = 0.5f; break;
        case SDK_INLINE_PROFILE_HALF_NEG_Y: max_y = 0.5f; break;
        case SDK_INLINE_PROFILE_HALF_POS_Y: min_y = 0.5f; break;
        case SDK_INLINE_PROFILE_HALF_NEG_Z: max_z = 0.5f; break;
        case SDK_INLINE_PROFILE_HALF_POS_Z: min_z = 0.5f; break;
        case SDK_INLINE_PROFILE_QUARTER_NEG_X: max_x = 0.25f; break;
        case SDK_INLINE_PROFILE_QUARTER_POS_X: min_x = 0.75f; break;
        case SDK_INLINE_PROFILE_QUARTER_NEG_Y: max_y = 0.25f; break;
        case SDK_INLINE_PROFILE_QUARTER_POS_Y: min_y = 0.75f; break;
        case SDK_INLINE_PROFILE_QUARTER_NEG_Z: max_z = 0.25f; break;
        case SDK_INLINE_PROFILE_QUARTER_POS_Z: min_z = 0.75f; break;
        case SDK_INLINE_PROFILE_BEAM_X:
            min_y = 0.375f; max_y = 0.625f; min_z = 0.375f; max_z = 0.625f;
            break;
        case SDK_INLINE_PROFILE_BEAM_Y:
            min_x = 0.375f; max_x = 0.625f; min_z = 0.375f; max_z = 0.625f;
            break;
        case SDK_INLINE_PROFILE_BEAM_Z:
            min_x = 0.375f; max_x = 0.625f; min_y = 0.375f; max_y = 0.625f;
            break;
        case SDK_INLINE_PROFILE_STRIP_X:
            min_y = 0.4375f; max_y = 0.5625f; min_z = 0.375f; max_z = 0.625f;
            break;
        case SDK_INLINE_PROFILE_STRIP_Y:
            min_x = 0.375f; max_x = 0.625f; min_z = 0.4375f; max_z = 0.5625f;
            break;
        case SDK_INLINE_PROFILE_STRIP_Z:
            min_x = 0.4375f; max_x = 0.5625f; min_y = 0.375f; max_y = 0.625f;
            break;
        default:
            return 0;
    }

    if (out_min_x) *out_min_x = min_x;
    if (out_min_y) *out_min_y = min_y;
    if (out_min_z) *out_min_z = min_z;
    if (out_max_x) *out_max_x = max_x;
    if (out_max_y) *out_max_y = max_y;
    if (out_max_z) *out_max_z = max_z;
    return 1;
}

static int construction_ray_aabb(float ox, float oy, float oz,
                                 float dx, float dy, float dz,
                                 float min_x, float min_y, float min_z,
                                 float max_x, float max_y, float max_z,
                                 float max_dist,
                                 float* out_dist,
                                 int* out_face)
{
    float tmin = 0.0f;
    float tmax = max_dist;
    int face = FACE_POS_Y;

    for (int axis = 0; axis < 3; ++axis) {
        float origin = (axis == 0) ? ox : (axis == 1 ? oy : oz);
        float dir = (axis == 0) ? dx : (axis == 1 ? dy : dz);
        float slab_min = (axis == 0) ? min_x : (axis == 1 ? min_y : min_z);
        float slab_max = (axis == 0) ? max_x : (axis == 1 ? max_y : max_z);

        if (fabsf(dir) < 0.000001f) {
            if (origin < slab_min || origin > slab_max) return 0;
            continue;
        }

        {
            float inv_dir = 1.0f / dir;
            float t0 = (slab_min - origin) * inv_dir;
            float t1 = (slab_max - origin) * inv_dir;
            int enter_face;

            if (t0 > t1) {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
                enter_face = (axis == 0) ? FACE_POS_X :
                             (axis == 1) ? FACE_POS_Y : FACE_POS_Z;
            } else {
                enter_face = (axis == 0) ? FACE_NEG_X :
                             (axis == 1) ? FACE_NEG_Y : FACE_NEG_Z;
            }

            if (t0 > tmin) {
                tmin = t0;
                face = enter_face;
            }
            if (t1 < tmax) tmax = t1;
            if (tmax < tmin) return 0;
        }
    }

    if (tmin < 0.0f) tmin = 0.0f;
    if (tmin > max_dist) return 0;
    if (out_dist) *out_dist = tmin;
    if (out_face) *out_face = face;
    return 1;
}

int sdk_construction_world_cell_intersects_aabb(const SdkChunkManager* cm,
                                                int wx, int wy, int wz,
                                                float min_x, float min_y, float min_z,
                                                float max_x, float max_y, float max_z)
{
    SdkChunk* chunk;
    int lx;
    int lz;
    SdkWorldCellCode code;

    if (!construction_chunk_local_from_world(cm, wx, wy, wz, &chunk, &lx, &lz)) return 0;
    code = sdk_chunk_get_cell_code(chunk, lx, wy, lz);
    if (sdk_world_cell_is_full_block(code)) {
        BlockType block = sdk_world_cell_decode_full_block(code);
        if (block == BLOCK_AIR || !sdk_block_is_solid(block)) return 0;
        return construction_aabb_intersects_box(min_x, min_y, min_z, max_x, max_y, max_z,
                                                (float)wx, (float)wy, (float)wz,
                                                (float)wx + 1.0f, (float)wy + 1.0f, (float)wz + 1.0f);
    }
    if (sdk_world_cell_is_inline_construction(code)) {
        float bmin_x, bmin_y, bmin_z, bmax_x, bmax_y, bmax_z;
        if (!construction_profile_bounds(sdk_world_cell_inline_profile(code),
                                         &bmin_x, &bmin_y, &bmin_z, &bmax_x, &bmax_y, &bmax_z)) {
            return 0;
        }
        return construction_aabb_intersects_box(min_x, min_y, min_z, max_x, max_y, max_z,
                                                (float)wx + bmin_x, (float)wy + bmin_y, (float)wz + bmin_z,
                                                (float)wx + bmax_x, (float)wy + bmax_y, (float)wz + bmax_z);
    }
    if (sdk_world_cell_is_overflow_construction(code) && chunk->construction_cells && chunk->construction_registry) {
        const SdkConstructionOverflowInstance* entry =
            construction_store_find_const(chunk->construction_cells, construction_local_index(lx, wy, lz));
        const SdkConstructionArchetype* archetype;
        if (!entry) return 0;
        construction_registry_lock_shared(chunk->construction_registry);
        archetype = construction_registry_get_const_locked(chunk->construction_registry, entry->archetype_id);
        if (!archetype || archetype->occupied_count == 0u) {
            construction_registry_unlock_shared(chunk->construction_registry);
            return 0;
        }
        if (!construction_aabb_intersects_box(min_x, min_y, min_z, max_x, max_y, max_z,
                                              (float)wx + (float)archetype->bounds_min[0] / 16.0f,
                                              (float)wy + (float)archetype->bounds_min[1] / 16.0f,
                                              (float)wz + (float)archetype->bounds_min[2] / 16.0f,
                                              (float)wx + (float)archetype->bounds_max[0] / 16.0f,
                                              (float)wy + (float)archetype->bounds_max[1] / 16.0f,
                                              (float)wz + (float)archetype->bounds_max[2] / 16.0f)) {
            construction_registry_unlock_shared(chunk->construction_registry);
            return 0;
        }
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    if (!construction_palette_voxel_get(archetype, x, y, z)) continue;
                    if (construction_aabb_intersects_box(min_x, min_y, min_z, max_x, max_y, max_z,
                                                         (float)wx + (float)x / 16.0f,
                                                         (float)wy + (float)y / 16.0f,
                                                         (float)wz + (float)z / 16.0f,
                                                         (float)wx + (float)(x + 1) / 16.0f,
                                                         (float)wy + (float)(y + 1) / 16.0f,
                                                         (float)wz + (float)(z + 1) / 16.0f)) {
                        construction_registry_unlock_shared(chunk->construction_registry);
                        return 1;
                    }
                }
            }
        }
        construction_registry_unlock_shared(chunk->construction_registry);
    }
    return 0;
}

int sdk_construction_world_cell_raycast(const SdkChunkManager* cm,
                                        int wx, int wy, int wz,
                                        float ox, float oy, float oz,
                                        float dx, float dy, float dz,
                                        float max_dist,
                                        float* out_dist,
                                        int* out_face)
{
    SdkChunk* chunk;
    int lx;
    int lz;
    SdkWorldCellCode code;
    float best_dist = max_dist + 1.0f;
    int best_face = FACE_POS_Y;
    int hit = 0;

    if (!construction_chunk_local_from_world(cm, wx, wy, wz, &chunk, &lx, &lz)) return 0;
    code = sdk_chunk_get_cell_code(chunk, lx, wy, lz);

    if (sdk_world_cell_is_full_block(code)) {
        BlockType block = sdk_world_cell_decode_full_block(code);
        if (block == BLOCK_AIR || !sdk_block_is_solid(block)) return 0;
        return construction_ray_aabb(ox, oy, oz, dx, dy, dz,
                                     (float)wx, (float)wy, (float)wz,
                                     (float)wx + 1.0f, (float)wy + 1.0f, (float)wz + 1.0f,
                                     max_dist, out_dist, out_face);
    }
    if (sdk_world_cell_is_inline_construction(code)) {
        float bmin_x, bmin_y, bmin_z, bmax_x, bmax_y, bmax_z;
        if (!construction_profile_bounds(sdk_world_cell_inline_profile(code),
                                         &bmin_x, &bmin_y, &bmin_z, &bmax_x, &bmax_y, &bmax_z)) {
            return 0;
        }
        return construction_ray_aabb(ox, oy, oz, dx, dy, dz,
                                     (float)wx + bmin_x, (float)wy + bmin_y, (float)wz + bmin_z,
                                     (float)wx + bmax_x, (float)wy + bmax_y, (float)wz + bmax_z,
                                     max_dist, out_dist, out_face);
    }
    if (!sdk_world_cell_is_overflow_construction(code) || !chunk->construction_cells) return 0;
    {
        const SdkConstructionOverflowInstance* entry =
            construction_store_find_const(chunk->construction_cells, construction_local_index(lx, wy, lz));
        const SdkConstructionArchetype* archetype;
        if (!entry) return 0;
        construction_registry_lock_shared(chunk->construction_registry);
        archetype = construction_registry_get_const_locked(chunk->construction_registry, entry->archetype_id);
        if (!archetype || archetype->occupied_count == 0u) {
            construction_registry_unlock_shared(chunk->construction_registry);
            return 0;
        }
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    float dist = 0.0f;
                    int face = FACE_POS_Y;
                    if (!construction_palette_voxel_get(archetype, x, y, z)) continue;
                    if (!construction_ray_aabb(ox, oy, oz, dx, dy, dz,
                                               (float)wx + (float)x / 16.0f,
                                               (float)wy + (float)y / 16.0f,
                                               (float)wz + (float)z / 16.0f,
                                               (float)wx + (float)(x + 1) / 16.0f,
                                               (float)wy + (float)(y + 1) / 16.0f,
                                               (float)wz + (float)(z + 1) / 16.0f,
                                               max_dist, &dist, &face)) {
                        continue;
                    }
                    if (!hit || dist < best_dist) {
                        best_dist = dist;
                        best_face = face;
                        hit = 1;
                    }
                }
            }
        }
        construction_registry_unlock_shared(chunk->construction_registry);
    }
    if (hit) {
        if (out_dist) *out_dist = best_dist;
        if (out_face) *out_face = best_face;
    }
    return hit;
}

int sdk_construction_resolve_face_placement(const SdkChunkManager* cm,
                                            int wx, int wy, int wz,
                                            const SdkConstructionItemPayload* payload,
                                            int face,
                                            int rotation,
                                            float hit_x, float hit_y, float hit_z,
                                            SdkConstructionPlacementResolution* out_resolution)
{
    SdkConstructionItemPayload transformed_payload;
    SdkConstructionItemPayload shifted_payload;
    SdkConstructionWorkspace merged_workspace;
    uint8_t bounds_min[3];
    uint8_t bounds_max[3];
    uint16_t occupied_count = 0u;
    int axis_u;
    int axis_v;
    int face_u = 0;
    int face_v = 0;
    int shift[3] = { 0, 0, 0 };
    int requested[3] = { 0, 0, 0 };
    int has_existing = 0;

    if (out_resolution) {
        memset(out_resolution, 0, sizeof(*out_resolution));
    }
    if (!payload || sdk_construction_payload_empty(payload) || !out_resolution) return 0;

    sdk_construction_payload_transform_for_face(payload, face, rotation, &transformed_payload);
    if (sdk_construction_payload_empty(&transformed_payload)) return 0;

    construction_face_local_hit_uv(wx, wy, wz, face, hit_x, hit_y, hit_z, &face_u, &face_v);
    sdk_construction_occupancy_bounds(transformed_payload.occupancy, &occupied_count, bounds_min, bounds_max);
    if (occupied_count == 0u) return 0;

    construction_face_tangent_axes(face, &axis_u, &axis_v);
    requested[axis_u] = face_u;
    requested[axis_v] = face_v;
    shift[axis_u] = requested[axis_u] - (int)bounds_min[axis_u];
    shift[axis_v] = requested[axis_v] - (int)bounds_min[axis_v];

    {
        int min_shift_u = -(int)bounds_min[axis_u];
        int min_shift_v = -(int)bounds_min[axis_v];
        int max_shift_u = 16 - (int)bounds_max[axis_u];
        int max_shift_v = 16 - (int)bounds_max[axis_v];
        if (shift[axis_u] < min_shift_u) shift[axis_u] = min_shift_u;
        if (shift[axis_u] > max_shift_u) shift[axis_u] = max_shift_u;
        if (shift[axis_v] < min_shift_v) shift[axis_v] = min_shift_v;
        if (shift[axis_v] > max_shift_v) shift[axis_v] = max_shift_v;
    }

    construction_shift_payload_axes(&transformed_payload,
                                    shift[0], shift[1], shift[2],
                                    &shifted_payload);

    out_resolution->face_u = face_u;
    out_resolution->face_v = face_v;
    out_resolution->resolved_payload = shifted_payload;
    out_resolution->preview_payload = shifted_payload;
    out_resolution->valid = 0;

    if (!construction_prepare_merged_payload(cm, wx, wy, wz,
                                             &shifted_payload,
                                             &merged_workspace,
                                             &has_existing)) {
        return 1;
    }

    if (has_existing) {
        BlockType merged_material = BLOCK_AIR;
        int single_material_only = 0;
        uint64_t merged_occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];

        construction_workspace_to_occupancy(&merged_workspace,
                                            merged_occupancy,
                                            NULL, NULL, NULL,
                                            &merged_material,
                                            &single_material_only);
        if (single_material_only && merged_material != BLOCK_AIR) {
            construction_payload_from_occupancy(merged_material,
                                                merged_occupancy,
                                                &out_resolution->preview_payload);
        }
    }
    out_resolution->valid = 1;
    return 1;
}

int sdk_construction_place_payload(SdkChunkManager* cm,
                                   int wx, int wy, int wz,
                                   const SdkConstructionItemPayload* payload,
                                   SdkConstructionEditOutcome* out_outcome)
{
    SdkChunk* chunk;
    int lx;
    int lz;
    SdkConstructionWorkspace* merged_workspace;
    int result = 0;

    construction_outcome_clear(out_outcome);
    if (!cm || !payload || sdk_construction_payload_empty(payload)) return 0;
    if (!construction_chunk_local_from_world(cm, wx, wy, wz, &chunk, &lx, &lz)) return 0;
    merged_workspace = construction_workspace_alloc();
    if (!merged_workspace) return 0;
    if (!construction_prepare_merged_payload(cm, wx, wy, wz, payload, merged_workspace, NULL)) goto cleanup;
    if (!sdk_construction_chunk_set_workspace(chunk, lx, wy, lz, merged_workspace)) goto cleanup;
    if (out_outcome) out_outcome->changed = 1;
    construction_resolve_local_support(cm, wx, wy, wz, out_outcome);
    result = 1;

cleanup:
    free(merged_workspace);
    return result;
}

int sdk_construction_cut_world_cell(SdkChunkManager* cm,
                                    int wx, int wy, int wz,
                                    int face,
                                    ToolClass tool,
                                    SdkConstructionEditOutcome* out_outcome)
{
    SdkChunk* chunk;
    int lx;
    int lz;
    SdkConstructionWorkspace* workspace = NULL;
    SdkConstructionWorkspace* remainder_workspace = NULL;
    SdkConstructionWorkspace* fragment_workspace = NULL;
    uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    uint64_t remainder[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    uint64_t fragment[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
    int result = 0;

    construction_outcome_clear(out_outcome);
    if (!cm) return 0;
    if (!construction_chunk_local_from_world(cm, wx, wy, wz, &chunk, &lx, &lz)) return 0;

    /* Heap-allocate large workspaces to avoid excessive stack usage (C6262) */
    workspace = (SdkConstructionWorkspace*)malloc(sizeof(SdkConstructionWorkspace));
    remainder_workspace = (SdkConstructionWorkspace*)malloc(sizeof(SdkConstructionWorkspace));
    fragment_workspace = (SdkConstructionWorkspace*)malloc(sizeof(SdkConstructionWorkspace));
    if (!workspace || !remainder_workspace || !fragment_workspace) goto cleanup;

    if (!sdk_construction_chunk_get_workspace(chunk, lx, wy, lz, workspace)) goto cleanup;
    construction_workspace_to_occupancy(workspace, occupancy, NULL, NULL, NULL, NULL, NULL);
    if (!construction_workspace_any_material_matches_tool(workspace, tool)) goto cleanup;
    if (!construction_split_by_face(occupancy, face, remainder, fragment)) goto cleanup;
    construction_workspace_filter_from_source(workspace, remainder, remainder_workspace);
    construction_workspace_filter_from_source(workspace, fragment, fragment_workspace);
    if (!sdk_construction_chunk_set_workspace(chunk, lx, wy, lz, remainder_workspace)) goto cleanup;
    if (out_outcome) out_outcome->changed = 1;
    construction_outcome_add_workspace_fragments(out_outcome, wx, wy, wz, fragment_workspace);
    construction_resolve_local_support(cm, wx, wy, wz, out_outcome);
    result = 1;

cleanup:
    free(workspace);
    free(remainder_workspace);
    free(fragment_workspace);
    return result;
}

int sdk_construction_remove_world_cell(SdkChunkManager* cm,
                                       int wx, int wy, int wz,
                                       SdkConstructionEditOutcome* out_outcome)
{
    SdkChunk* chunk;
    int lx;
    int lz;
    SdkConstructionWorkspace* workspace = NULL;
    int result = 0;

    construction_outcome_clear(out_outcome);
    if (!cm) goto cleanup;
    if (!construction_chunk_local_from_world(cm, wx, wy, wz, &chunk, &lx, &lz)) goto cleanup;

    workspace = (SdkConstructionWorkspace*)malloc(sizeof(SdkConstructionWorkspace));
    if (!workspace) goto cleanup;

    if (!sdk_construction_chunk_get_workspace(chunk, lx, wy, lz, workspace)) goto cleanup;
    sdk_construction_chunk_clear_cell(chunk, lx, wy, lz);
    if (out_outcome) out_outcome->changed = 1;
    construction_outcome_add_workspace_fragments(out_outcome, wx, wy, wz, workspace);
    construction_resolve_local_support(cm, wx, wy, wz, out_outcome);
    result = 1;

cleanup:
    free(workspace);
    return result;
}

static int construction_write_varuint(uint8_t** io_buf, size_t* io_len, size_t* io_cap, uint32_t value)
{
    size_t needed = *io_len + 5u;
    uint8_t* grown;

    if (needed > *io_cap) {
        size_t new_cap = *io_cap ? *io_cap * 2u : 256u;
        while (new_cap < needed) new_cap *= 2u;
        grown = (uint8_t*)realloc(*io_buf, new_cap);
        if (!grown) return 0;
        *io_buf = grown;
        *io_cap = new_cap;
    }

    do {
        uint8_t byte = (uint8_t)(value & 0x7Fu);
        value >>= 7u;
        if (value) byte |= 0x80u;
        (*io_buf)[(*io_len)++] = byte;
    } while (value);
    return 1;
}

static int construction_reserve_bytes(uint8_t** io_buf, size_t* io_cap, size_t needed)
{
    size_t new_cap;
    uint8_t* grown;

    if (!io_buf || !io_cap) return 0;
    if (needed <= *io_cap) return 1;

    new_cap = *io_cap ? *io_cap * 2u : 256u;
    while (new_cap < needed) {
        if (new_cap > (((size_t)-1) / 2u)) {
            new_cap = needed;
            break;
        }
        new_cap *= 2u;
    }
    grown = (uint8_t*)realloc(*io_buf, new_cap);
    if (!grown) return 0;
    *io_buf = grown;
    *io_cap = new_cap;
    return 1;
}

static int construction_read_varuint(const uint8_t* data, size_t len, size_t* io_offset, uint32_t* out_value)
{
    uint32_t value = 0u;
    uint32_t shift = 0u;
    size_t offset;

    if (!data || !io_offset || !out_value) return 0;
    offset = *io_offset;
    while (offset < len && shift < 35u) {
        uint8_t byte = data[offset++];
        value |= (uint32_t)(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0u) {
            *io_offset = offset;
            *out_value = value;
            return 1;
        }
        shift += 7u;
    }
    return 0;
}

static int construction_write_bytes(uint8_t** io_buf, size_t* io_len, size_t* io_cap,
                                    const void* data, size_t size)
{
    size_t available;
    size_t needed;

    if (!io_buf || !io_len || !io_cap) return 0;
    if (size == 0u) return 1;
    if (!data) return 0;
    if (size > ((size_t)-1) - *io_len) return 0;
    needed = *io_len + size;
    if (!construction_reserve_bytes(io_buf, io_cap, needed)) return 0;
    if (*io_cap < needed) return 0;
    available = *io_cap - *io_len;
    if (available < size) return 0;
    if (memcpy_s(*io_buf + *io_len, available, data, size) != 0) return 0;
    *io_len += size;
    return 1;
}

static char* construction_base64_encode(const uint8_t* data, size_t len)
{
    static const char k_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len;
    size_t i;
    char* out;

    if (!data || len == 0u) {
        out = (char*)malloc(1u);
        if (out) out[0] = '\0';
        return out;
    }

    out_len = ((len + 2u) / 3u) * 4u;
    out = (char*)malloc(out_len + 1u);
    if (!out) return NULL;

    for (i = 0u; i < len; i += 3u) {
        size_t write_index = (i / 3u) * 4u;
        uint32_t a = data[i];
        uint32_t b = (i + 1u < len) ? data[i + 1u] : 0u;
        uint32_t c = (i + 2u < len) ? data[i + 2u] : 0u;
        uint32_t triple = (a << 16u) | (b << 8u) | c;
        if (write_index + 4u > out_len) {
            free(out);
            return NULL;
        }
        out[write_index + 0u] = k_table[(triple >> 18u) & 0x3Fu];
        out[write_index + 1u] = k_table[(triple >> 12u) & 0x3Fu];
        out[write_index + 2u] = (i + 1u < len) ? k_table[(triple >> 6u) & 0x3Fu] : '=';
        out[write_index + 3u] = (i + 2u < len) ? k_table[triple & 0x3Fu] : '=';
    }
    out[out_len] = '\0';
    return out;
}

static int construction_base64_decode_char(int ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static uint8_t* construction_base64_decode(const char* text, size_t* out_len)
{
    size_t text_len;
    size_t padding = 0u;
    size_t decoded_cap;
    size_t i;
    size_t out_index = 0u;
    uint8_t* out;

    if (out_len) *out_len = 0u;
    if (!text) return NULL;
    text_len = strlen(text);
    if (text_len == 0u) {
        out = (uint8_t*)malloc(1u);
        if (out_len) *out_len = 0u;
        return out;
    }
    if ((text_len % 4u) != 0u) return NULL;
    if (text[text_len - 1u] == '=') padding++;
    if (text[text_len - 2u] == '=') padding++;
    decoded_cap = ((text_len / 4u) * 3u) - padding;

    out = (uint8_t*)malloc(decoded_cap ? decoded_cap : 1u);
    if (!out) return NULL;

    for (i = 0u; i < text_len; i += 4u) {
        int a = construction_base64_decode_char((unsigned char)text[i]);
        int b = construction_base64_decode_char((unsigned char)text[i + 1u]);
        int c = (text[i + 2u] == '=') ? -2 : construction_base64_decode_char((unsigned char)text[i + 2u]);
        int d = (text[i + 3u] == '=') ? -2 : construction_base64_decode_char((unsigned char)text[i + 3u]);
        uint32_t triple;
        size_t emitted = 1u;
        if (a < 0 || b < 0 || c == -1 || d == -1) {
            free(out);
            return NULL;
        }
        if ((c == -2 && d != -2) || ((c == -2 || d == -2) && (i + 4u) != text_len)) {
            free(out);
            return NULL;
        }
        if (c != -2) emitted++;
        if (d != -2) emitted++;
        triple = ((uint32_t)a << 18u) | ((uint32_t)b << 12u) |
                 ((uint32_t)(c < 0 ? 0 : c) << 6u) |
                 (uint32_t)(d < 0 ? 0 : d);
        switch (emitted) {
            case 1u:
                if (out_index + 1u > decoded_cap) {
                    free(out);
                    return NULL;
                }
                out[out_index + 0u] = (uint8_t)((triple >> 16u) & 0xFFu);
                break;
            case 2u:
                if (out_index + 2u > decoded_cap) {
                    free(out);
                    return NULL;
                }
                out[out_index + 0u] = (uint8_t)((triple >> 16u) & 0xFFu);
                out[out_index + 1u] = (uint8_t)((triple >> 8u) & 0xFFu);
                break;
            case 3u:
                if (out_index + 3u > decoded_cap) {
                    free(out);
                    return NULL;
                }
                out[out_index + 0u] = (uint8_t)((triple >> 16u) & 0xFFu);
                out[out_index + 1u] = (uint8_t)((triple >> 8u) & 0xFFu);
                out[out_index + 2u] = (uint8_t)(triple & 0xFFu);
                break;
            default:
                free(out);
                return NULL;
        }
        out_index += emitted;
    }

    if (out_len) *out_len = out_index;
    return out;
}

enum {
    SDK_CONSTRUCTION_REGISTRY_FORMAT_VERSION = 1u,
    SDK_CONSTRUCTION_STORE_FORMAT_VERSION = 1u
};

char* sdk_construction_encode_registry(const SdkConstructionArchetypeRegistry* registry)
{
    uint8_t* buffer = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    char* encoded;

    if (!registry) {
        encoded = (char*)malloc(1u);
        if (encoded) encoded[0] = '\0';
        return encoded;
    }

    construction_registry_lock_shared(registry);
    if (registry->active_count == 0u) {
        construction_registry_unlock_shared(registry);
        encoded = (char*)malloc(1u);
        if (encoded) encoded[0] = '\0';
        return encoded;
    }
    if (!construction_write_bytes(&buffer, &len, &cap, "SCAR", 4u) ||
        !construction_write_varuint(&buffer, &len, &cap, SDK_CONSTRUCTION_REGISTRY_FORMAT_VERSION) ||
        !construction_write_varuint(&buffer, &len, &cap, registry->active_count)) {
        construction_registry_unlock_shared(registry);
        free(buffer);
        return NULL;
    }

    for (uint32_t slot_index = 0u; slot_index < registry->slot_count; ++slot_index) {
        const SdkConstructionArchetype* archetype = registry->slots[slot_index];
        if (!archetype) continue;
        if (!construction_write_varuint(&buffer, &len, &cap, archetype->id) ||
            !construction_write_varuint(&buffer, &len, &cap, archetype->palette_count) ||
            !construction_write_bytes(&buffer, &len, &cap,
                                      archetype->palette,
                                      (size_t)archetype->palette_count * sizeof(archetype->palette[0])) ||
            !construction_write_bytes(&buffer, &len, &cap,
                                      archetype->voxel_palette,
                                      sizeof(archetype->voxel_palette))) {
            construction_registry_unlock_shared(registry);
            free(buffer);
            return NULL;
        }
    }
    construction_registry_unlock_shared(registry);

    encoded = construction_base64_encode(buffer, len);
    free(buffer);
    return encoded;
}

int sdk_construction_decode_registry(SdkConstructionArchetypeRegistry* registry, const char* encoded)
{
    uint8_t* decoded;
    size_t decoded_len = 0u;
    size_t offset = 0u;
    uint32_t version = 0u;
    uint32_t count = 0u;

    if (!registry) return 0;
    construction_registry_lock_exclusive(registry);
    construction_registry_clear_locked(registry);
    if (!encoded || encoded[0] == '\0') {
        construction_registry_unlock_exclusive(registry);
        return 1;
    }

    decoded = construction_base64_decode(encoded, &decoded_len);
    if (!decoded) {
        construction_registry_unlock_exclusive(registry);
        return 0;
    }
    if (decoded_len < 4u || memcmp(decoded, "SCAR", 4u) != 0) {
        free(decoded);
        construction_registry_unlock_exclusive(registry);
        return 0;
    }
    offset = 4u;
    if (!construction_read_varuint(decoded, decoded_len, &offset, &version) ||
        version != SDK_CONSTRUCTION_REGISTRY_FORMAT_VERSION ||
        !construction_read_varuint(decoded, decoded_len, &offset, &count)) {
        free(decoded);
        construction_registry_unlock_exclusive(registry);
        return 0;
    }

    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t id = 0u;
        uint32_t palette_count_u32 = 0u;
        uint8_t palette_count = 0u;
        uint32_t slot_index;
        SdkConstructionArchetype* archetype;

        if (!construction_read_varuint(decoded, decoded_len, &offset, &id) ||
            !construction_read_varuint(decoded, decoded_len, &offset, &palette_count_u32)) {
            free(decoded);
            construction_registry_clear_locked(registry);
            construction_registry_unlock_exclusive(registry);
            return 0;
        }
        if (id == 0u || palette_count_u32 > SDK_CONSTRUCTION_ARCHETYPE_MAX_PALETTE) {
            free(decoded);
            construction_registry_clear_locked(registry);
            construction_registry_unlock_exclusive(registry);
            return 0;
        }
        palette_count = (uint8_t)palette_count_u32;
        if (offset + (size_t)palette_count * sizeof(uint16_t) + SDK_CONSTRUCTION_CELL_VOXELS > decoded_len) {
            free(decoded);
            construction_registry_clear_locked(registry);
            construction_registry_unlock_exclusive(registry);
            return 0;
        }
        slot_index = id - 1u;
        if (!construction_registry_ensure_slot_capacity(registry, slot_index + 1u)) {
            free(decoded);
            construction_registry_clear_locked(registry);
            construction_registry_unlock_exclusive(registry);
            return 0;
        }
        archetype = (SdkConstructionArchetype*)calloc(1, sizeof(*archetype));
        if (!archetype) {
            free(decoded);
            construction_registry_clear_locked(registry);
            construction_registry_unlock_exclusive(registry);
            return 0;
        }
        archetype->id = (SdkConstructionArchetypeId)id;
        archetype->palette_count = palette_count;
        memcpy(archetype->palette, decoded + offset, (size_t)palette_count * sizeof(uint16_t));
        offset += (size_t)palette_count * sizeof(uint16_t);
        memcpy(archetype->voxel_palette, decoded + offset, SDK_CONSTRUCTION_CELL_VOXELS);
        offset += SDK_CONSTRUCTION_CELL_VOXELS;
        archetype->hash = construction_hash_workspace_palette(archetype->palette,
                                                              archetype->palette_count,
                                                              archetype->voxel_palette);
        construction_archetype_rebuild_cached(archetype);
        registry->slots[slot_index] = archetype;
        if (slot_index + 1u > registry->slot_count) {
            registry->slot_count = slot_index + 1u;
        }
        registry->active_count++;
    }

    if (!construction_registry_rebuild_hash_locked(registry)) {
        free(decoded);
        construction_registry_clear_locked(registry);
        construction_registry_unlock_exclusive(registry);
        return 0;
    }
    free(decoded);
    construction_registry_unlock_exclusive(registry);
    return 1;
}

char* sdk_construction_encode_store(const SdkChunk* chunk)
{
    uint8_t* buffer = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    char* encoded;

    if (!chunk || !chunk->construction_cells || chunk->construction_cells->count == 0u) {
        encoded = (char*)malloc(1u);
        if (encoded) encoded[0] = '\0';
        return encoded;
    }

    if (!construction_write_bytes(&buffer, &len, &cap, "SCST", 4u) ||
        !construction_write_varuint(&buffer, &len, &cap, SDK_CONSTRUCTION_STORE_FORMAT_VERSION) ||
        !construction_write_varuint(&buffer, &len, &cap, chunk->construction_cells->count)) {
        free(buffer);
        return NULL;
    }
    for (uint32_t i = 0u; i < chunk->construction_cells->count; ++i) {
        const SdkConstructionOverflowInstance* entry = &chunk->construction_cells->entries[i];
        if (!construction_write_varuint(&buffer, &len, &cap, entry->local_index) ||
            !construction_write_varuint(&buffer, &len, &cap, entry->archetype_id)) {
            free(buffer);
            return NULL;
        }
    }
    encoded = construction_base64_encode(buffer, len);
    free(buffer);
    return encoded;
}

int sdk_construction_decode_store(SdkChunk* chunk, const char* encoded)
{
    uint8_t* decoded;
    size_t decoded_len = 0u;
    size_t offset = 0u;
    uint32_t count = 0u;

    if (!chunk || !encoded || encoded[0] == '\0') return 1;
    decoded = construction_base64_decode(encoded, &decoded_len);
    if (!decoded) return 0;
    if (decoded_len >= 4u && memcmp(decoded, "SCST", 4u) == 0) {
        uint32_t version = 0u;
        offset = 4u;
        if (!construction_read_varuint(decoded, decoded_len, &offset, &version) ||
            version != SDK_CONSTRUCTION_STORE_FORMAT_VERSION ||
            !construction_read_varuint(decoded, decoded_len, &offset, &count)) {
            free(decoded);
            return 0;
        }
        for (uint32_t i = 0u; i < count; ++i) {
            uint32_t local_index = 0u;
            uint32_t archetype_id_u32 = 0u;
            int lx;
            int ly;
            int lz;
            if (!construction_read_varuint(decoded, decoded_len, &offset, &local_index) ||
                !construction_read_varuint(decoded, decoded_len, &offset, &archetype_id_u32)) {
                free(decoded);
                return 0;
            }
            if (!chunk->construction_registry) {
                free(decoded);
                return 0;
            }
            construction_registry_lock_shared(chunk->construction_registry);
            if (!construction_registry_get_const_locked(chunk->construction_registry,
                                                        (SdkConstructionArchetypeId)archetype_id_u32)) {
                construction_registry_unlock_shared(chunk->construction_registry);
                free(decoded);
                return 0;
            }
            construction_registry_unlock_shared(chunk->construction_registry);
            ly = (int)(local_index / CHUNK_BLOCKS_PER_LAYER);
            lz = (int)((local_index % CHUNK_BLOCKS_PER_LAYER) / CHUNK_WIDTH);
            lx = (int)(local_index % CHUNK_WIDTH);
            sdk_chunk_set_cell_code_raw(chunk, lx, ly, lz, (SdkWorldCellCode)SDK_WORLD_CELL_OVERFLOW_CODE);
            if (!construction_store_set_instance(chunk, local_index, (SdkConstructionArchetypeId)archetype_id_u32)) {
                free(decoded);
                return 0;
            }
            chunk->empty = false;
        }
        free(decoded);
        return 1;
    }

    if (!construction_read_varuint(decoded, decoded_len, &offset, &count)) {
        free(decoded);
        return 0;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t local_index;
        uint16_t material;
        uint64_t occupancy[SDK_CONSTRUCTION_OCCUPANCY_WORD_COUNT];
        int lx;
        int ly;
        int lz;

        if (!construction_read_varuint(decoded, decoded_len, &offset, &local_index)) {
            free(decoded);
            return 0;
        }
        if (offset + sizeof(material) + sizeof(occupancy) > decoded_len) {
            free(decoded);
            return 0;
        }
        memcpy(&material, decoded + offset, sizeof(material));
        offset += sizeof(material);
        memcpy(occupancy, decoded + offset, sizeof(occupancy));
        offset += sizeof(occupancy);

        ly = (int)(local_index / CHUNK_BLOCKS_PER_LAYER);
        lz = (int)((local_index % CHUNK_BLOCKS_PER_LAYER) / CHUNK_WIDTH);
        lx = (int)(local_index % CHUNK_WIDTH);
        if (!sdk_construction_chunk_set_cell_payload(chunk, lx, ly, lz, (BlockType)material, occupancy)) {
            free(decoded);
            return 0;
        }
    }
    free(decoded);
    return 1;
}
