#include "sdk_frontend_internal.h"

int ensure_directory_exists_a(const char* path)
{
    char scratch[MAX_PATH];
    DWORD attrs;
    size_t i;

    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    strncpy_s(scratch, sizeof(scratch), path, _TRUNCATE);
    for (i = 0; scratch[i] != '\0'; ++i) {
        if (scratch[i] == '\\' || scratch[i] == '/') {
            char saved = scratch[i];

            scratch[i] = '\0';
            if (scratch[0] &&
                !(i == 2u && scratch[1] == ':') &&
                !(i == 0u)) {
                attrs = GetFileAttributesA(scratch);
                if (attrs != INVALID_FILE_ATTRIBUTES) {
                    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                        scratch[i] = saved;
                        return 0;
                    }
                } else if (!CreateDirectoryA(scratch, NULL) &&
                           GetLastError() != ERROR_ALREADY_EXISTS) {
                    scratch[i] = saved;
                    return 0;
                }
            }
            scratch[i] = saved;
        }
    }

    if (CreateDirectoryA(path, NULL)) {
        return 1;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

void copy_ascii_trimmed(char* dst, size_t dst_len, const char* src)
{
    size_t len = 0;

    if (!dst || dst_len == 0) return;
    dst[0] = '\0';
    if (!src) return;

    while (*src == ' ' || *src == '\t') src++;
    while (src[len] != '\0' && src[len] != '\r' && src[len] != '\n' &&
           len + 1 < dst_len) {
        char ch = src[len];
        if (ch == '=') ch = '-';
        dst[len] = ch;
        len++;
    }
    while (len > 0 && (dst[len - 1] == ' ' || dst[len - 1] == '\t')) {
        len--;
    }
    dst[len] = '\0';
}

static int build_character_library_root_path(char* out_path, size_t out_path_len)
{
    if (!out_path || out_path_len == 0) return 0;
    out_path[0] = '\0';
    if (!ensure_directory_exists_a(SDK_CHARACTER_LIBRARY_ROOT)) return 0;
    return snprintf(out_path, out_path_len, "%s", SDK_CHARACTER_LIBRARY_ROOT) > 0;
}

static int build_character_dir_path(const char* character_id, char* out_path, size_t out_path_len)
{
    char root[MAX_PATH];

    if (!character_id || !character_id[0] || !out_path || out_path_len == 0) return 0;
    if (!build_character_library_root_path(root, sizeof(root))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", root, character_id) > 0;
}

static int build_character_meta_path(const char* character_id, char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!build_character_dir_path(character_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", dir_path, SDK_CHARACTER_META_NAME) > 0;
}

static int build_character_model_path(const char* character_id, char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!build_character_dir_path(character_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", dir_path, SDK_CHARACTER_MODEL_NAME) > 0;
}

static int build_character_animations_dir_path(const char* character_id, char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!build_character_dir_path(character_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", dir_path, SDK_CHARACTER_ANIMATIONS_DIR) > 0;
}

static int build_animation_dir_path(const char* character_id, const char* animation_id,
                                    char* out_path, size_t out_path_len)
{
    char animations_dir[MAX_PATH];

    if (!character_id || !character_id[0] || !animation_id || !animation_id[0]) return 0;
    if (!build_character_animations_dir_path(character_id, animations_dir, sizeof(animations_dir))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", animations_dir, animation_id) > 0;
}

static int build_animation_meta_path(const char* character_id, const char* animation_id,
                                     char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!build_animation_dir_path(character_id, animation_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", dir_path, SDK_CHARACTER_META_NAME) > 0;
}

static int build_animation_frame_path(const char* character_id, const char* animation_id,
                                      int frame_index, char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!build_animation_dir_path(character_id, animation_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\frame_%03d.bin", dir_path, frame_index) > 0;
}

static int build_character_profile_path(char* out_path, size_t out_path_len)
{
    if (!out_path || out_path_len == 0) return 0;
    out_path[0] = '\0';
    if (!ensure_directory_exists_a(SDK_CHARACTER_PROFILE_ROOT)) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s",
                    SDK_CHARACTER_PROFILE_ROOT, SDK_CHARACTER_PROFILE_NAME) > 0;
}

static int build_simple_asset_library_root_path(const char* root_name, char* out_path, size_t out_path_len)
{
    if (!root_name || !root_name[0] || !out_path || out_path_len == 0) return 0;
    out_path[0] = '\0';
    if (!ensure_directory_exists_a(root_name)) return 0;
    return snprintf(out_path, out_path_len, "%s", root_name) > 0;
}

static int build_simple_asset_dir_path(const char* root_name, const char* asset_id,
                                       char* out_path, size_t out_path_len)
{
    char root[MAX_PATH];

    if (!root_name || !root_name[0] || !asset_id || !asset_id[0] || !out_path || out_path_len == 0) return 0;
    if (!build_simple_asset_library_root_path(root_name, root, sizeof(root))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", root, asset_id) > 0;
}

static int build_simple_asset_meta_path(const char* root_name, const char* asset_id,
                                        char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!build_simple_asset_dir_path(root_name, asset_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", dir_path, SDK_CHARACTER_META_NAME) > 0;
}

static int build_simple_asset_file_path(const char* root_name, const char* asset_id, const char* file_name,
                                        char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!file_name || !file_name[0]) return 0;
    if (!build_simple_asset_dir_path(root_name, asset_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\%s", dir_path, file_name) > 0;
}

static int build_prop_chunks_dir_path(const char* asset_id, char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!asset_id || !asset_id[0] || !out_path || out_path_len == 0) return 0;
    if (!build_simple_asset_dir_path(SDK_PROP_LIBRARY_ROOT, asset_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\chunks", dir_path) > 0;
}

static int build_prop_chunk_file_path(const char* asset_id, int chunk_x, int chunk_z,
                                      char* out_path, size_t out_path_len)
{
    char chunks_dir[MAX_PATH];

    if (!build_prop_chunks_dir_path(asset_id, chunks_dir, sizeof(chunks_dir))) return 0;
    return snprintf(out_path, out_path_len, "%s\\chunk_%02d_%02d.bin",
                    chunks_dir, api_clampi(chunk_x, 0, SDK_PROP_EDITOR_CHUNK_SPAN - 1),
                    api_clampi(chunk_z, 0, SDK_PROP_EDITOR_CHUNK_SPAN - 1)) > 0;
}

static int build_prop_chunk_cells_v2_path(const char* asset_id, int chunk_x, int chunk_z,
                                          char* out_path, size_t out_path_len)
{
    char chunks_dir[MAX_PATH];

    if (!build_prop_chunks_dir_path(asset_id, chunks_dir, sizeof(chunks_dir))) return 0;
    return snprintf(out_path, out_path_len, "%s\\chunk_%02d_%02d.cells.bin",
                    chunks_dir, api_clampi(chunk_x, 0, SDK_PROP_EDITOR_CHUNK_SPAN - 1),
                    api_clampi(chunk_z, 0, SDK_PROP_EDITOR_CHUNK_SPAN - 1)) > 0;
}

static int build_prop_chunk_construction_path(const char* asset_id, int chunk_x, int chunk_z,
                                              char* out_path, size_t out_path_len)
{
    char chunks_dir[MAX_PATH];

    if (!build_prop_chunks_dir_path(asset_id, chunks_dir, sizeof(chunks_dir))) return 0;
    return snprintf(out_path, out_path_len, "%s\\chunk_%02d_%02d.construction.txt",
                    chunks_dir, api_clampi(chunk_x, 0, SDK_PROP_EDITOR_CHUNK_SPAN - 1),
                    api_clampi(chunk_z, 0, SDK_PROP_EDITOR_CHUNK_SPAN - 1)) > 0;
}

static int build_prop_construction_registry_path(const char* asset_id,
                                                 char* out_path, size_t out_path_len)
{
    char dir_path[MAX_PATH];

    if (!asset_id || !asset_id[0] || !out_path || out_path_len == 0) return 0;
    if (!build_simple_asset_dir_path(SDK_PROP_LIBRARY_ROOT, asset_id, dir_path, sizeof(dir_path))) return 0;
    return snprintf(out_path, out_path_len, "%s\\construction_archetypes.txt", dir_path) > 0;
}

typedef struct PropChunkCellsV2Header {
    char magic[4];
    uint32_t version;
    uint32_t width;
    uint32_t depth;
    uint32_t height;
} PropChunkCellsV2Header;

enum {
    SDK_PROP_CHUNK_CELLS_V2_VERSION = 2u
};

static void character_voxel_buffer_clear(uint8_t* voxels, size_t voxel_count)
{
    if (!voxels || voxel_count == 0) return;
    memset(voxels, (int)BLOCK_AIR, voxel_count);
}

static int voxel_buffer_is_empty(const uint8_t* voxels, size_t voxel_count)
{
    size_t i;

    if (!voxels) return 1;
    for (i = 0; i < voxel_count; ++i) {
        if ((BlockType)voxels[i] != BLOCK_AIR) {
            return 0;
        }
    }
    return 1;
}

static void cell_code_buffer_clear(SdkWorldCellCode* cells, size_t cell_count)
{
    if (!cells || cell_count == 0u) return;
    for (size_t i = 0; i < cell_count; ++i) {
        cells[i] = sdk_world_cell_encode_full_block(BLOCK_AIR);
    }
}

static int cell_code_buffer_is_empty(const SdkWorldCellCode* cells, size_t cell_count)
{
    if (!cells) return 1;
    for (size_t i = 0; i < cell_count; ++i) {
        if (sdk_world_cell_decode_full_block(cells[i]) != BLOCK_AIR ||
            !sdk_world_cell_is_full_block(cells[i])) {
            return 0;
        }
    }
    return 1;
}

static int write_character_meta_file(const char* character_id, const char* display_name)
{
    char path[MAX_PATH];
    FILE* file;

    if (!build_character_meta_path(character_id, path, sizeof(path))) return 0;
    file = fopen(path, "wb");
    if (!file) return 0;
    fprintf(file, "name=%s\n", display_name ? display_name : "CHARACTER");
    fclose(file);
    return 1;
}

static int write_animation_meta_file(const char* character_id, const char* animation_id,
                                     const char* display_name, int frame_count)
{
    char path[MAX_PATH];
    FILE* file;

    if (!build_animation_meta_path(character_id, animation_id, path, sizeof(path))) return 0;
    file = fopen(path, "wb");
    if (!file) return 0;
    fprintf(file, "name=%s\n", display_name ? display_name : "ANIMATION");
    fprintf(file, "frame_count=%d\n", api_clampi(frame_count, 1, 64));
    fclose(file);
    return 1;
}

static int write_simple_asset_meta_file(const char* root_name, const char* asset_id, const char* display_name)
{
    char path[MAX_PATH];
    FILE* file;

    if (!build_simple_asset_meta_path(root_name, asset_id, path, sizeof(path))) return 0;
    file = fopen(path, "wb");
    if (!file) return 0;
    fprintf(file, "name=%s\n", display_name ? display_name : "ASSET");
    fclose(file);
    return 1;
}

static int write_prop_meta_file(const SdkPropAssetMeta* meta)
{
    char path[MAX_PATH];
    FILE* file;

    if (!meta) return 0;
    if (!build_simple_asset_meta_path(SDK_PROP_LIBRARY_ROOT, meta->asset_id, path, sizeof(path))) return 0;
    file = fopen(path, "wb");
    if (!file) return 0;
    fprintf(file, "name=%s\n", meta->display_name[0] ? meta->display_name : "PROP");
    fprintf(file, "family=%s\n", meta->family_name[0] ? meta->family_name : "unassigned");
    fprintf(file, "footprint_x=%d\n", api_clampi(meta->footprint_x, 0, SUPERCHUNK_BLOCKS));
    fprintf(file, "footprint_z=%d\n", api_clampi(meta->footprint_z, 0, SUPERCHUNK_BLOCKS));
    fprintf(file, "anchor_x=%d\n", meta->anchor_x);
    fprintf(file, "anchor_y=%d\n", meta->anchor_y);
    fprintf(file, "anchor_z=%d\n", meta->anchor_z);
    fprintf(file, "shell_compatible=%d\n", meta->shell_compatible ? 1 : 0);
    fclose(file);
    return 1;
}

static int write_particle_effect_meta_file(const char* asset_id, const char* display_name, int slice_count)
{
    char path[MAX_PATH];
    FILE* file;

    if (!build_simple_asset_meta_path(SDK_PARTICLE_EFFECT_LIBRARY_ROOT, asset_id, path, sizeof(path))) return 0;
    file = fopen(path, "wb");
    if (!file) return 0;
    fprintf(file, "name=%s\n", display_name ? display_name : "PARTICLE EFFECT");
    fprintf(file, "slice_count=%d\n", api_clampi(slice_count, 1, SDK_PARTICLE_EFFECT_SLICE_COUNT));
    fclose(file);
    return 1;
}

static int recursive_copy_directory_a(const char* src_dir, const char* dst_dir)
{
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char pattern[MAX_PATH];

    if (!src_dir || !src_dir[0] || !dst_dir || !dst_dir[0]) return 0;
    if (!ensure_directory_exists_a(dst_dir)) return 0;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", src_dir) <= 0) return 0;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) return 0;

    do {
        char src_path[MAX_PATH];
        char dst_path[MAX_PATH];

        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        if (snprintf(src_path, sizeof(src_path), "%s\\%s", src_dir, find_data.cFileName) <= 0) continue;
        if (snprintf(dst_path, sizeof(dst_path), "%s\\%s", dst_dir, find_data.cFileName) <= 0) continue;

        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!recursive_copy_directory_a(src_path, dst_path)) {
                FindClose(handle);
                return 0;
            }
        } else if (!CopyFileA(src_path, dst_path, FALSE)) {
            FindClose(handle);
            return 0;
        }
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);
    return 1;
}

static int recursive_delete_directory_a(const char* dir_path)
{
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char pattern[MAX_PATH];

    if (!dir_path || !dir_path[0]) return 0;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", dir_path) <= 0) return 0;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            char child_path[MAX_PATH];

            if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
            if (snprintf(child_path, sizeof(child_path), "%s\\%s", dir_path, find_data.cFileName) <= 0) continue;

            if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                recursive_delete_directory_a(child_path);
            } else {
                DeleteFileA(child_path);
            }
        } while (FindNextFileA(handle, &find_data));
        FindClose(handle);
    }

    return RemoveDirectoryA(dir_path) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

static void load_asset_display_name(const char* meta_path, char* out_name, size_t out_name_len)
{
    FILE* file;
    char line[160];

    if (!out_name || out_name_len == 0) return;
    if (!meta_path || !meta_path[0]) return;
    file = fopen(meta_path, "rb");
    if (!file) return;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "name=", 5) == 0) {
            copy_ascii_trimmed(out_name, out_name_len, line + 5);
            break;
        }
    }

    fclose(file);
}

static void load_meta_string(const char* meta_path, const char* key, char* out_value, size_t out_value_len)
{
    FILE* file;
    char line[160];
    char prefix[64];
    size_t prefix_len;

    if (!out_value || out_value_len == 0) return;
    out_value[0] = '\0';
    if (!meta_path || !meta_path[0] || !key || !key[0]) return;
    if (snprintf(prefix, sizeof(prefix), "%s=", key) <= 0) return;
    prefix_len = strlen(prefix);
    file = fopen(meta_path, "rb");
    if (!file) return;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, prefix, prefix_len) == 0) {
            copy_ascii_trimmed(out_value, out_value_len, line + prefix_len);
            break;
        }
    }

    fclose(file);
}

static int load_animation_frame_count(const char* meta_path)
{
    FILE* file;
    char line[160];
    int frame_count = SDK_EDITOR_DEFAULT_FRAME_COUNT;

    if (!meta_path || !meta_path[0]) return frame_count;
    file = fopen(meta_path, "rb");
    if (!file) return frame_count;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "frame_count=", 12) == 0) {
            frame_count = api_clampi((int)strtol(line + 12, NULL, 10), 1, 64);
        }
    }

    fclose(file);
    return frame_count;
}

static int load_meta_integer(const char* meta_path, const char* key, int default_value, int min_value, int max_value)
{
    FILE* file;
    char line[160];
    char prefix[64];
    size_t prefix_len;
    int value = default_value;

    if (!meta_path || !meta_path[0] || !key || !key[0]) return default_value;
    if (snprintf(prefix, sizeof(prefix), "%s=", key) <= 0) return default_value;
    prefix_len = strlen(prefix);
    file = fopen(meta_path, "rb");
    if (!file) return default_value;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, prefix, prefix_len) == 0) {
            value = api_clampi((int)strtol(line + prefix_len, NULL, 10), min_value, max_value);
            break;
        }
    }

    fclose(file);
    return value;
}

static int load_binary_asset_file(const char* path, uint8_t* out_voxels, size_t voxel_count)
{
    FILE* file;

    if (!out_voxels || voxel_count == 0 || !path || !path[0]) return 0;
    character_voxel_buffer_clear(out_voxels, voxel_count);
    file = fopen(path, "rb");
    if (!file) return 0;
    fread(out_voxels, 1u, voxel_count, file);
    fclose(file);
    return 1;
}

static int save_binary_asset_file(const char* path, const uint8_t* voxels, size_t voxel_count)
{
    FILE* file;

    if (!voxels || voxel_count == 0 || !path || !path[0]) return 0;
    file = fopen(path, "wb");
    if (!file) return 0;
    fwrite(voxels, 1u, voxel_count, file);
    fclose(file);
    return 1;
}

static int compare_character_asset_desc(const void* a, const void* b)
{
    const SdkCharacterAssetMeta* lhs = (const SdkCharacterAssetMeta*)a;
    const SdkCharacterAssetMeta* rhs = (const SdkCharacterAssetMeta*)b;

    if (lhs->last_write_time < rhs->last_write_time) return 1;
    if (lhs->last_write_time > rhs->last_write_time) return -1;
    return strcmp(lhs->asset_id, rhs->asset_id);
}

static int compare_animation_asset_desc(const void* a, const void* b)
{
    const SdkAnimationAssetMeta* lhs = (const SdkAnimationAssetMeta*)a;
    const SdkAnimationAssetMeta* rhs = (const SdkAnimationAssetMeta*)b;

    if (lhs->last_write_time < rhs->last_write_time) return 1;
    if (lhs->last_write_time > rhs->last_write_time) return -1;
    return strcmp(lhs->asset_id, rhs->asset_id);
}

static int compare_block_asset_desc(const void* a, const void* b)
{
    const SdkBlockAssetMeta* lhs = (const SdkBlockAssetMeta*)a;
    const SdkBlockAssetMeta* rhs = (const SdkBlockAssetMeta*)b;

    if (lhs->last_write_time < rhs->last_write_time) return 1;
    if (lhs->last_write_time > rhs->last_write_time) return -1;
    return strcmp(lhs->asset_id, rhs->asset_id);
}

static int compare_item_asset_desc(const void* a, const void* b)
{
    const SdkItemAssetMeta* lhs = (const SdkItemAssetMeta*)a;
    const SdkItemAssetMeta* rhs = (const SdkItemAssetMeta*)b;

    if (lhs->last_write_time < rhs->last_write_time) return 1;
    if (lhs->last_write_time > rhs->last_write_time) return -1;
    return strcmp(lhs->asset_id, rhs->asset_id);
}

static int compare_prop_asset_desc(const void* a, const void* b)
{
    const SdkPropAssetMeta* lhs = (const SdkPropAssetMeta*)a;
    const SdkPropAssetMeta* rhs = (const SdkPropAssetMeta*)b;

    if (lhs->last_write_time < rhs->last_write_time) return 1;
    if (lhs->last_write_time > rhs->last_write_time) return -1;
    return strcmp(lhs->asset_id, rhs->asset_id);
}

static int compare_particle_effect_asset_desc(const void* a, const void* b)
{
    const SdkParticleEffectAssetMeta* lhs = (const SdkParticleEffectAssetMeta*)a;
    const SdkParticleEffectAssetMeta* rhs = (const SdkParticleEffectAssetMeta*)b;

    if (lhs->last_write_time < rhs->last_write_time) return 1;
    if (lhs->last_write_time > rhs->last_write_time) return -1;
    return strcmp(lhs->asset_id, rhs->asset_id);
}

static int count_animation_directories(const char* character_id)
{
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char animations_dir[MAX_PATH];
    char pattern[MAX_PATH];
    int count = 0;

    if (!build_character_animations_dir_path(character_id, animations_dir, sizeof(animations_dir))) return 0;
    if (!ensure_directory_exists_a(animations_dir)) return 0;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", animations_dir) <= 0) return 0;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) return 0;

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        count++;
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);
    return count;
}

void refresh_character_assets(void)
{
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char pattern[MAX_PATH];

    g_character_asset_count = 0;
    if (!build_character_library_root_path(pattern, sizeof(pattern))) return;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", SDK_CHARACTER_LIBRARY_ROOT) <= 0) return;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        g_character_menu_selected = 0;
        g_selected_character_index = -1;
        return;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        if (g_character_asset_count >= SDK_CHARACTER_ASSET_MAX) break;

        {
            SdkCharacterAssetMeta* meta = &g_character_assets[g_character_asset_count];
            char meta_path[MAX_PATH];

            memset(meta, 0, sizeof(*meta));
            strcpy_s(meta->asset_id, sizeof(meta->asset_id), find_data.cFileName);
            strcpy_s(meta->display_name, sizeof(meta->display_name), find_data.cFileName);
            build_character_dir_path(meta->asset_id, meta->dir_path, sizeof(meta->dir_path));
            meta->last_write_time = filetime_to_u64(find_data.ftLastWriteTime);
            meta->animation_count = count_animation_directories(meta->asset_id);
            if (build_character_meta_path(meta->asset_id, meta_path, sizeof(meta_path))) {
                load_asset_display_name(meta_path, meta->display_name, sizeof(meta->display_name));
            }
            if (meta->display_name[0] == '\0') {
                strcpy_s(meta->display_name, sizeof(meta->display_name), meta->asset_id);
            }
            g_character_asset_count++;
        }
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);

    if (g_character_asset_count > 1) {
        qsort(g_character_assets, (size_t)g_character_asset_count, sizeof(g_character_assets[0]),
              compare_character_asset_desc);
    }

    if (g_character_menu_selected < 0) g_character_menu_selected = 0;
    if (g_character_menu_selected > g_character_asset_count) g_character_menu_selected = g_character_asset_count;
    if (g_selected_character_index >= g_character_asset_count) g_selected_character_index = g_character_asset_count - 1;
    if (g_selected_character_index < 0 && g_character_asset_count > 0) g_selected_character_index = 0;
}

void refresh_animation_assets_for_selected_character(void)
{
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char animations_dir[MAX_PATH];
    char pattern[MAX_PATH];

    g_animation_asset_count = 0;
    if (g_selected_character_index < 0 || g_selected_character_index >= g_character_asset_count) {
        g_selected_animation_index = -1;
        return;
    }

    if (!build_character_animations_dir_path(g_character_assets[g_selected_character_index].asset_id,
                                             animations_dir, sizeof(animations_dir))) {
        g_selected_animation_index = -1;
        return;
    }
    if (!ensure_directory_exists_a(animations_dir)) {
        g_selected_animation_index = -1;
        return;
    }
    if (snprintf(pattern, sizeof(pattern), "%s\\*", animations_dir) <= 0) return;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        g_selected_animation_index = -1;
        return;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        if (g_animation_asset_count >= SDK_ANIMATION_ASSET_MAX) break;

        {
            SdkAnimationAssetMeta* meta = &g_animation_assets[g_animation_asset_count];
            char meta_path[MAX_PATH];

            memset(meta, 0, sizeof(*meta));
            strcpy_s(meta->character_id, sizeof(meta->character_id),
                     g_character_assets[g_selected_character_index].asset_id);
            strcpy_s(meta->asset_id, sizeof(meta->asset_id), find_data.cFileName);
            strcpy_s(meta->display_name, sizeof(meta->display_name), find_data.cFileName);
            build_animation_dir_path(meta->character_id, meta->asset_id, meta->dir_path, sizeof(meta->dir_path));
            meta->last_write_time = filetime_to_u64(find_data.ftLastWriteTime);
            meta->frame_count = SDK_EDITOR_DEFAULT_FRAME_COUNT;
            if (build_animation_meta_path(meta->character_id, meta->asset_id, meta_path, sizeof(meta_path))) {
                load_asset_display_name(meta_path, meta->display_name, sizeof(meta->display_name));
                meta->frame_count = load_animation_frame_count(meta_path);
            }
            if (meta->display_name[0] == '\0') {
                strcpy_s(meta->display_name, sizeof(meta->display_name), meta->asset_id);
            }
            g_animation_asset_count++;
        }
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);

    if (g_animation_asset_count > 1) {
        qsort(g_animation_assets, (size_t)g_animation_asset_count, sizeof(g_animation_assets[0]),
              compare_animation_asset_desc);
    }

    if (g_animation_menu_selected < 0) g_animation_menu_selected = 0;
    if (g_animation_menu_selected >= g_animation_asset_count) {
        g_animation_menu_selected = (g_animation_asset_count > 0) ? (g_animation_asset_count - 1) : 0;
    }
    if (g_selected_animation_index >= g_animation_asset_count) g_selected_animation_index = g_animation_asset_count - 1;
    if (g_selected_animation_index < 0 && g_animation_asset_count > 0) g_selected_animation_index = 0;
}

int load_character_model_bytes(const char* character_id, uint8_t* out_voxels, size_t voxel_count)
{
    char path[MAX_PATH];
    FILE* file;

    if (!out_voxels || voxel_count == 0) return 0;
    character_voxel_buffer_clear(out_voxels, voxel_count);
    if (!build_character_model_path(character_id, path, sizeof(path))) return 0;

    file = fopen(path, "rb");
    if (!file) return 0;
    fread(out_voxels, 1u, voxel_count, file);
    fclose(file);
    return 1;
}

int save_character_model_bytes(const char* character_id, const uint8_t* voxels, size_t voxel_count)
{
    char path[MAX_PATH];
    FILE* file;

    if (!voxels || voxel_count == 0) return 0;
    if (!build_character_model_path(character_id, path, sizeof(path))) return 0;

    file = fopen(path, "wb");
    if (!file) return 0;
    fwrite(voxels, 1u, voxel_count, file);
    fclose(file);
    return 1;
}

int load_animation_frame_bytes(const char* character_id, const char* animation_id,
                               int frame_index, uint8_t* out_voxels, size_t voxel_count)
{
    char path[MAX_PATH];
    FILE* file;

    if (!out_voxels || voxel_count == 0) return 0;
    character_voxel_buffer_clear(out_voxels, voxel_count);
    if (!build_animation_frame_path(character_id, animation_id, frame_index, path, sizeof(path))) return 0;

    file = fopen(path, "rb");
    if (!file) return 0;
    fread(out_voxels, 1u, voxel_count, file);
    fclose(file);
    return 1;
}

int save_animation_frame_bytes(const char* character_id, const char* animation_id,
                               int frame_index, const uint8_t* voxels, size_t voxel_count)
{
    char path[MAX_PATH];
    FILE* file;

    if (!voxels || voxel_count == 0) return 0;
    if (!build_animation_frame_path(character_id, animation_id, frame_index, path, sizeof(path))) return 0;

    file = fopen(path, "wb");
    if (!file) return 0;
    fwrite(voxels, 1u, voxel_count, file);
    fclose(file);
    return 1;
}

int create_character_asset(SdkCharacterAssetMeta* out_meta)
{
    int index;

    if (!ensure_directory_exists_a(SDK_CHARACTER_LIBRARY_ROOT)) return 0;

    for (index = 1; index < 1000; ++index) {
        char character_id[64];
        char dir_path[MAX_PATH];
        char animations_dir[MAX_PATH];
        char display_name[SDK_START_MENU_ASSET_NAME_MAX];
        uint8_t voxels[SDK_CHARACTER_MODEL_VOXELS];
        SdkCharacterAssetMeta meta;

        snprintf(character_id, sizeof(character_id), "character_%03d", index);
        if (!build_character_dir_path(character_id, dir_path, sizeof(dir_path))) return 0;
        if (GetFileAttributesA(dir_path) != INVALID_FILE_ATTRIBUTES) continue;

        if (!ensure_directory_exists_a(dir_path)) return 0;
        if (!build_character_animations_dir_path(character_id, animations_dir, sizeof(animations_dir))) return 0;
        if (!ensure_directory_exists_a(animations_dir)) return 0;
        snprintf(display_name, sizeof(display_name), "CHARACTER %03d", index);
        if (!write_character_meta_file(character_id, display_name)) return 0;
        character_voxel_buffer_clear(voxels, sizeof(voxels));
        if (!save_character_model_bytes(character_id, voxels, sizeof(voxels))) return 0;

        memset(&meta, 0, sizeof(meta));
        strcpy_s(meta.asset_id, sizeof(meta.asset_id), character_id);
        strcpy_s(meta.display_name, sizeof(meta.display_name), display_name);
        strcpy_s(meta.dir_path, sizeof(meta.dir_path), dir_path);
        meta.animation_count = 0;
        meta.last_write_time = GetTickCount64();
        if (out_meta) *out_meta = meta;
        g_frontend_refresh_pending = true;
        return 1;
    }

    return 0;
}

int copy_character_asset(int character_index)
{
    SdkCharacterAssetMeta copy_meta;
    char dst_dir[MAX_PATH];
    char animations_dir[MAX_PATH];
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];

    if (character_index < 0 || character_index >= g_character_asset_count) return 0;
    if (!create_character_asset(&copy_meta)) return 0;

    recursive_delete_directory_a(copy_meta.dir_path);
    if (!recursive_copy_directory_a(g_character_assets[character_index].dir_path, copy_meta.dir_path)) return 0;
    if (!build_character_animations_dir_path(copy_meta.asset_id, animations_dir, sizeof(animations_dir))) return 0;
    ensure_directory_exists_a(animations_dir);
    strcpy_s(dst_dir, sizeof(dst_dir), copy_meta.dir_path);
    snprintf(display_name, sizeof(display_name), "%s COPY", g_character_assets[character_index].display_name);
    write_character_meta_file(copy_meta.asset_id, display_name);
    g_frontend_refresh_pending = true;
    return 1;
}

int delete_character_asset(int character_index)
{
    if (character_index < 0 || character_index >= g_character_asset_count) return 0;
    if (!recursive_delete_directory_a(g_character_assets[character_index].dir_path)) return 0;
    g_frontend_refresh_pending = true;
    return 1;
}

int create_animation_asset_for_selected_character(SdkAnimationAssetMeta* out_meta)
{
    int index;
    const SdkCharacterAssetMeta* character;
    uint8_t base_model[SDK_CHARACTER_MODEL_VOXELS];

    if (g_selected_character_index < 0 || g_selected_character_index >= g_character_asset_count) return 0;
    character = &g_character_assets[g_selected_character_index];
    load_character_model_bytes(character->asset_id, base_model, sizeof(base_model));

    for (index = 1; index < 1000; ++index) {
        char animation_id[64];
        char dir_path[MAX_PATH];
        char display_name[SDK_START_MENU_ASSET_NAME_MAX];
        SdkAnimationAssetMeta meta;

        snprintf(animation_id, sizeof(animation_id), "animation_%03d", index);
        if (!build_animation_dir_path(character->asset_id, animation_id, dir_path, sizeof(dir_path))) return 0;
        if (GetFileAttributesA(dir_path) != INVALID_FILE_ATTRIBUTES) continue;

        if (!ensure_directory_exists_a(dir_path)) return 0;
        snprintf(display_name, sizeof(display_name), "ANIMATION %03d", index);
        if (!write_animation_meta_file(character->asset_id, animation_id, display_name,
                                       SDK_EDITOR_DEFAULT_FRAME_COUNT)) return 0;
        for (int frame = 0; frame < SDK_EDITOR_DEFAULT_FRAME_COUNT; ++frame) {
            if (!save_animation_frame_bytes(character->asset_id, animation_id, frame,
                                            base_model, sizeof(base_model))) {
                return 0;
            }
        }

        memset(&meta, 0, sizeof(meta));
        strcpy_s(meta.asset_id, sizeof(meta.asset_id), animation_id);
        strcpy_s(meta.character_id, sizeof(meta.character_id), character->asset_id);
        strcpy_s(meta.display_name, sizeof(meta.display_name), display_name);
        strcpy_s(meta.dir_path, sizeof(meta.dir_path), dir_path);
        meta.frame_count = SDK_EDITOR_DEFAULT_FRAME_COUNT;
        meta.last_write_time = GetTickCount64();
        if (out_meta) *out_meta = meta;
        g_frontend_refresh_pending = true;
        return 1;
    }

    return 0;
}

int copy_selected_animation_asset(void)
{
    SdkAnimationAssetMeta copy_meta;
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];

    if (g_selected_character_index < 0 || g_selected_character_index >= g_character_asset_count) return 0;
    if (g_selected_animation_index < 0 || g_selected_animation_index >= g_animation_asset_count) return 0;
    if (!create_animation_asset_for_selected_character(&copy_meta)) return 0;

    recursive_delete_directory_a(copy_meta.dir_path);
    if (!recursive_copy_directory_a(g_animation_assets[g_selected_animation_index].dir_path,
                                    copy_meta.dir_path)) {
        return 0;
    }
    snprintf(display_name, sizeof(display_name), "%s COPY",
             g_animation_assets[g_selected_animation_index].display_name);
    write_animation_meta_file(g_character_assets[g_selected_character_index].asset_id, copy_meta.asset_id,
                              display_name, g_animation_assets[g_selected_animation_index].frame_count);
    g_frontend_refresh_pending = true;
    return 1;
}

int delete_selected_animation_asset(void)
{
    if (g_selected_animation_index < 0 || g_selected_animation_index >= g_animation_asset_count) return 0;
    if (!recursive_delete_directory_a(g_animation_assets[g_selected_animation_index].dir_path)) return 0;
    g_frontend_refresh_pending = true;
    return 1;
}

void refresh_block_assets(void)
{
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char pattern[MAX_PATH];

    g_block_asset_count = 0;
    if (!build_simple_asset_library_root_path(SDK_BLOCK_LIBRARY_ROOT, pattern, sizeof(pattern))) return;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", SDK_BLOCK_LIBRARY_ROOT) <= 0) return;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        g_block_menu_selected = 0;
        g_selected_block_index = -1;
        return;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        if (g_block_asset_count >= SDK_BLOCK_ASSET_MAX) break;

        {
            SdkBlockAssetMeta* meta = &g_block_assets[g_block_asset_count];
            char meta_path[MAX_PATH];

            memset(meta, 0, sizeof(*meta));
            strcpy_s(meta->asset_id, sizeof(meta->asset_id), find_data.cFileName);
            strcpy_s(meta->display_name, sizeof(meta->display_name), find_data.cFileName);
            build_simple_asset_dir_path(SDK_BLOCK_LIBRARY_ROOT, meta->asset_id,
                                        meta->dir_path, sizeof(meta->dir_path));
            meta->last_write_time = filetime_to_u64(find_data.ftLastWriteTime);
            if (build_simple_asset_meta_path(SDK_BLOCK_LIBRARY_ROOT, meta->asset_id,
                                             meta_path, sizeof(meta_path))) {
                load_asset_display_name(meta_path, meta->display_name, sizeof(meta->display_name));
            }
            if (meta->display_name[0] == '\0') {
                strcpy_s(meta->display_name, sizeof(meta->display_name), meta->asset_id);
            }
            g_block_asset_count++;
        }
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);

    if (g_block_asset_count > 1) {
        qsort(g_block_assets, (size_t)g_block_asset_count, sizeof(g_block_assets[0]),
              compare_block_asset_desc);
    }

    if (g_block_menu_selected < 0) g_block_menu_selected = 0;
    if (g_block_menu_selected > g_block_asset_count) g_block_menu_selected = g_block_asset_count;
    if (g_selected_block_index >= g_block_asset_count) g_selected_block_index = g_block_asset_count - 1;
    if (g_selected_block_index < 0 && g_block_asset_count > 0) g_selected_block_index = 0;
}

void refresh_item_assets(void)
{
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char pattern[MAX_PATH];

    g_item_asset_count = 0;
    if (!build_simple_asset_library_root_path(SDK_ITEM_LIBRARY_ROOT, pattern, sizeof(pattern))) return;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", SDK_ITEM_LIBRARY_ROOT) <= 0) return;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        g_item_menu_selected = 0;
        g_selected_item_index = -1;
        return;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        if (g_item_asset_count >= SDK_ITEM_ASSET_MAX) break;

        {
            SdkItemAssetMeta* meta = &g_item_assets[g_item_asset_count];
            char meta_path[MAX_PATH];

            memset(meta, 0, sizeof(*meta));
            strcpy_s(meta->asset_id, sizeof(meta->asset_id), find_data.cFileName);
            strcpy_s(meta->display_name, sizeof(meta->display_name), find_data.cFileName);
            build_simple_asset_dir_path(SDK_ITEM_LIBRARY_ROOT, meta->asset_id,
                                        meta->dir_path, sizeof(meta->dir_path));
            meta->last_write_time = filetime_to_u64(find_data.ftLastWriteTime);
            if (build_simple_asset_meta_path(SDK_ITEM_LIBRARY_ROOT, meta->asset_id,
                                             meta_path, sizeof(meta_path))) {
                load_asset_display_name(meta_path, meta->display_name, sizeof(meta->display_name));
            }
            if (meta->display_name[0] == '\0') {
                strcpy_s(meta->display_name, sizeof(meta->display_name), meta->asset_id);
            }
            g_item_asset_count++;
        }
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);

    if (g_item_asset_count > 1) {
        qsort(g_item_assets, (size_t)g_item_asset_count, sizeof(g_item_assets[0]),
              compare_item_asset_desc);
    }

    if (g_item_menu_selected < 0) g_item_menu_selected = 0;
    if (g_item_menu_selected > g_item_asset_count) g_item_menu_selected = g_item_asset_count;
    if (g_selected_item_index >= g_item_asset_count) g_selected_item_index = g_item_asset_count - 1;
    if (g_selected_item_index < 0 && g_item_asset_count > 0) g_selected_item_index = 0;
}

void refresh_particle_effect_assets(void)
{
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char pattern[MAX_PATH];

    g_particle_effect_asset_count = 0;
    if (!build_simple_asset_library_root_path(SDK_PARTICLE_EFFECT_LIBRARY_ROOT, pattern, sizeof(pattern))) return;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", SDK_PARTICLE_EFFECT_LIBRARY_ROOT) <= 0) return;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        g_particle_effect_menu_selected = 0;
        g_selected_particle_effect_index = -1;
        return;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        if (g_particle_effect_asset_count >= SDK_PARTICLE_EFFECT_ASSET_MAX) break;

        {
            SdkParticleEffectAssetMeta* meta = &g_particle_effect_assets[g_particle_effect_asset_count];
            char meta_path[MAX_PATH];

            memset(meta, 0, sizeof(*meta));
            strcpy_s(meta->asset_id, sizeof(meta->asset_id), find_data.cFileName);
            strcpy_s(meta->display_name, sizeof(meta->display_name), find_data.cFileName);
            build_simple_asset_dir_path(SDK_PARTICLE_EFFECT_LIBRARY_ROOT, meta->asset_id,
                                        meta->dir_path, sizeof(meta->dir_path));
            meta->last_write_time = filetime_to_u64(find_data.ftLastWriteTime);
            meta->slice_count = SDK_PARTICLE_EFFECT_SLICE_COUNT;
            if (build_simple_asset_meta_path(SDK_PARTICLE_EFFECT_LIBRARY_ROOT, meta->asset_id,
                                             meta_path, sizeof(meta_path))) {
                load_asset_display_name(meta_path, meta->display_name, sizeof(meta->display_name));
                meta->slice_count = load_meta_integer(meta_path, "slice_count",
                                                      SDK_PARTICLE_EFFECT_SLICE_COUNT,
                                                      1, SDK_PARTICLE_EFFECT_SLICE_COUNT);
            }
            if (meta->display_name[0] == '\0') {
                strcpy_s(meta->display_name, sizeof(meta->display_name), meta->asset_id);
            }
            g_particle_effect_asset_count++;
        }
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);

    if (g_particle_effect_asset_count > 1) {
        qsort(g_particle_effect_assets, (size_t)g_particle_effect_asset_count,
              sizeof(g_particle_effect_assets[0]), compare_particle_effect_asset_desc);
    }

    if (g_particle_effect_menu_selected < 0) g_particle_effect_menu_selected = 0;
    if (g_particle_effect_menu_selected > g_particle_effect_asset_count) {
        g_particle_effect_menu_selected = g_particle_effect_asset_count;
    }
    if (g_selected_particle_effect_index >= g_particle_effect_asset_count) {
        g_selected_particle_effect_index = g_particle_effect_asset_count - 1;
    }
    if (g_selected_particle_effect_index < 0 && g_particle_effect_asset_count > 0) {
        g_selected_particle_effect_index = 0;
    }
}

void refresh_prop_assets(void)
{
    WIN32_FIND_DATAA find_data;
    HANDLE handle;
    char pattern[MAX_PATH];

    g_prop_asset_count = 0;
    if (!build_simple_asset_library_root_path(SDK_PROP_LIBRARY_ROOT, pattern, sizeof(pattern))) return;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", SDK_PROP_LIBRARY_ROOT) <= 0) return;

    handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        g_prop_menu_selected = 0;
        g_selected_prop_index = -1;
        return;
    }

    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) continue;
        if (g_prop_asset_count >= SDK_PROP_ASSET_MAX) break;

        {
            SdkPropAssetMeta* meta = &g_prop_assets[g_prop_asset_count];
            char meta_path[MAX_PATH];

            memset(meta, 0, sizeof(*meta));
            strcpy_s(meta->asset_id, sizeof(meta->asset_id), find_data.cFileName);
            strcpy_s(meta->display_name, sizeof(meta->display_name), find_data.cFileName);
            build_simple_asset_dir_path(SDK_PROP_LIBRARY_ROOT, meta->asset_id,
                                        meta->dir_path, sizeof(meta->dir_path));
            meta->last_write_time = filetime_to_u64(find_data.ftLastWriteTime);
            if (build_simple_asset_meta_path(SDK_PROP_LIBRARY_ROOT, meta->asset_id,
                                             meta_path, sizeof(meta_path))) {
                load_asset_display_name(meta_path, meta->display_name, sizeof(meta->display_name));
                load_meta_string(meta_path, "family", meta->family_name, sizeof(meta->family_name));
                meta->footprint_x = load_meta_integer(meta_path, "footprint_x", 0, 0, SUPERCHUNK_BLOCKS);
                meta->footprint_z = load_meta_integer(meta_path, "footprint_z", 0, 0, SUPERCHUNK_BLOCKS);
                meta->anchor_x = load_meta_integer(meta_path, "anchor_x", 0, -SUPERCHUNK_BLOCKS, SUPERCHUNK_BLOCKS);
                meta->anchor_y = load_meta_integer(meta_path, "anchor_y", 0, -CHUNK_HEIGHT, CHUNK_HEIGHT);
                meta->anchor_z = load_meta_integer(meta_path, "anchor_z", 0, -SUPERCHUNK_BLOCKS, SUPERCHUNK_BLOCKS);
                meta->shell_compatible = load_meta_integer(meta_path, "shell_compatible", 1, 0, 1);
            }
            if (meta->display_name[0] == '\0') {
                strcpy_s(meta->display_name, sizeof(meta->display_name), meta->asset_id);
            }
            g_prop_asset_count++;
        }
    } while (FindNextFileA(handle, &find_data));

    FindClose(handle);

    if (g_prop_asset_count > 1) {
        qsort(g_prop_assets, (size_t)g_prop_asset_count, sizeof(g_prop_assets[0]),
              compare_prop_asset_desc);
    }

    if (g_prop_menu_selected < 0) g_prop_menu_selected = 0;
    if (g_prop_menu_selected > g_prop_asset_count) g_prop_menu_selected = g_prop_asset_count;
    if (g_selected_prop_index >= g_prop_asset_count) g_selected_prop_index = g_prop_asset_count - 1;
    if (g_selected_prop_index < 0 && g_prop_asset_count > 0) g_selected_prop_index = 0;
}

int load_prop_chunk_bytes(const char* asset_id, int chunk_x, int chunk_z,
                          uint8_t* out_voxels, size_t voxel_count)
{
    char path[MAX_PATH];
    DWORD attrs;

    if (!out_voxels || voxel_count == 0u) return 0;
    character_voxel_buffer_clear(out_voxels, voxel_count);
    if (!build_prop_chunk_file_path(asset_id, chunk_x, chunk_z, path, sizeof(path))) {
        return 0;
    }
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return 1;
    }
    return load_binary_asset_file(path, out_voxels, voxel_count);
}

int save_prop_chunk_bytes(const char* asset_id, int chunk_x, int chunk_z,
                          const uint8_t* voxels, size_t voxel_count)
{
    char chunks_dir[MAX_PATH];
    char path[MAX_PATH];

    if (!asset_id || !asset_id[0] || !voxels || voxel_count == 0u) return 0;
    if (!build_prop_chunks_dir_path(asset_id, chunks_dir, sizeof(chunks_dir))) return 0;
    if (!ensure_directory_exists_a(chunks_dir)) return 0;
    if (!build_prop_chunk_file_path(asset_id, chunk_x, chunk_z, path, sizeof(path))) return 0;

    if (voxel_buffer_is_empty(voxels, voxel_count)) {
        if (DeleteFileA(path)) return 1;
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    return save_binary_asset_file(path, voxels, voxel_count);
}

int load_prop_chunk_cell_codes_v2(const char* asset_id, int chunk_x, int chunk_z,
                                  SdkWorldCellCode* out_cells,
                                  int width, int depth, int height,
                                  int* out_found)
{
    char path[MAX_PATH];
    DWORD attrs;
    FILE* file;
    PropChunkCellsV2Header header;
    size_t cell_count;

    if (out_found) *out_found = 0;
    if (!out_cells || width <= 0 || depth <= 0 || height <= 0) return 0;
    cell_count = (size_t)width * (size_t)depth * (size_t)height;
    cell_code_buffer_clear(out_cells, cell_count);
    if (!build_prop_chunk_cells_v2_path(asset_id, chunk_x, chunk_z, path, sizeof(path))) {
        return 0;
    }
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return 0;
    }

    file = fopen(path, "rb");
    if (!file) return 0;
    memset(&header, 0, sizeof(header));
    if (fread(&header, sizeof(header), 1u, file) != 1u) {
        fclose(file);
        return 0;
    }
    if (memcmp(header.magic, "SDKP", 4u) != 0 ||
        header.version != SDK_PROP_CHUNK_CELLS_V2_VERSION ||
        header.width != (uint32_t)width ||
        header.depth != (uint32_t)depth ||
        header.height != (uint32_t)height) {
        fclose(file);
        return 0;
    }
    if (fread(out_cells, sizeof(SdkWorldCellCode), cell_count, file) != cell_count) {
        fclose(file);
        cell_code_buffer_clear(out_cells, cell_count);
        return 0;
    }
    fclose(file);
    if (out_found) *out_found = 1;
    return 1;
}

int save_prop_chunk_cell_codes_v2(const char* asset_id, int chunk_x, int chunk_z,
                                  const SdkWorldCellCode* cells,
                                  int width, int depth, int height)
{
    char chunks_dir[MAX_PATH];
    char path[MAX_PATH];
    FILE* file;
    PropChunkCellsV2Header header;
    size_t cell_count;

    if (!asset_id || !asset_id[0] || !cells || width <= 0 || depth <= 0 || height <= 0) return 0;
    cell_count = (size_t)width * (size_t)depth * (size_t)height;
    if (!build_prop_chunks_dir_path(asset_id, chunks_dir, sizeof(chunks_dir))) return 0;
    if (!ensure_directory_exists_a(chunks_dir)) return 0;
    if (!build_prop_chunk_cells_v2_path(asset_id, chunk_x, chunk_z, path, sizeof(path))) return 0;

    if (cell_code_buffer_is_empty(cells, cell_count)) {
        if (DeleteFileA(path)) return 1;
        {
            DWORD err = GetLastError();
            return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
        }
    }

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, "SDKP", 4u);
    header.version = SDK_PROP_CHUNK_CELLS_V2_VERSION;
    header.width = (uint32_t)width;
    header.depth = (uint32_t)depth;
    header.height = (uint32_t)height;

    file = fopen(path, "wb");
    if (!file) return 0;
    if (fwrite(&header, sizeof(header), 1u, file) != 1u ||
        fwrite(cells, sizeof(SdkWorldCellCode), cell_count, file) != cell_count) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

int load_prop_chunk_construction_text(const char* asset_id, int chunk_x, int chunk_z,
                                      char** out_text, int* out_found)
{
    char path[MAX_PATH];
    DWORD attrs;
    FILE* file;
    long size;
    char* text;
    size_t read_bytes;

    if (out_found) *out_found = 0;
    if (out_text) *out_text = NULL;
    if (!out_text) return 0;
    if (!build_prop_chunk_construction_path(asset_id, chunk_x, chunk_z, path, sizeof(path))) {
        return 0;
    }
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return 0;
    }

    file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    text = (char*)malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        return 0;
    }
    read_bytes = fread(text, 1u, (size_t)size, file);
    fclose(file);
    if (read_bytes != (size_t)size) {
        free(text);
        return 0;
    }
    text[size] = '\0';
    *out_text = text;
    if (out_found) *out_found = 1;
    return 1;
}

int save_prop_chunk_construction_text(const char* asset_id, int chunk_x, int chunk_z,
                                      const char* encoded)
{
    char chunks_dir[MAX_PATH];
    char path[MAX_PATH];
    FILE* file;
    size_t len = 0u;

    if (!asset_id || !asset_id[0]) return 0;
    if (!build_prop_chunks_dir_path(asset_id, chunks_dir, sizeof(chunks_dir))) return 0;
    if (!ensure_directory_exists_a(chunks_dir)) return 0;
    if (!build_prop_chunk_construction_path(asset_id, chunk_x, chunk_z, path, sizeof(path))) return 0;

    if (!encoded || encoded[0] == '\0') {
        if (DeleteFileA(path)) return 1;
        {
            DWORD err = GetLastError();
            return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
        }
    }

    len = strlen(encoded);
    file = fopen(path, "wb");
    if (!file) return 0;
    if (len > 0u && fwrite(encoded, 1u, len, file) != len) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

int load_prop_construction_registry_text(const char* asset_id,
                                         char** out_text,
                                         int* out_found)
{
    char path[MAX_PATH];
    DWORD attrs;
    FILE* file;
    long size;
    char* text;
    size_t read_bytes;

    if (out_found) *out_found = 0;
    if (out_text) *out_text = NULL;
    if (!out_text) return 0;
    if (!build_prop_construction_registry_path(asset_id, path, sizeof(path))) {
        return 0;
    }
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return 0;
    }

    file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    text = (char*)malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        return 0;
    }
    read_bytes = fread(text, 1u, (size_t)size, file);
    fclose(file);
    if (read_bytes != (size_t)size) {
        free(text);
        return 0;
    }
    text[size] = '\0';
    *out_text = text;
    if (out_found) *out_found = 1;
    return 1;
}

int save_prop_construction_registry_text(const char* asset_id,
                                         const char* encoded)
{
    char dir_path[MAX_PATH];
    char path[MAX_PATH];
    FILE* file;
    size_t len = 0u;

    if (!asset_id || !asset_id[0]) return 0;
    if (!build_simple_asset_dir_path(SDK_PROP_LIBRARY_ROOT, asset_id, dir_path, sizeof(dir_path))) return 0;
    if (!ensure_directory_exists_a(dir_path)) return 0;
    if (!build_prop_construction_registry_path(asset_id, path, sizeof(path))) return 0;

    if (!encoded || encoded[0] == '\0') {
        if (DeleteFileA(path)) return 1;
        {
            DWORD err = GetLastError();
            return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
        }
    }

    len = strlen(encoded);
    file = fopen(path, "wb");
    if (!file) return 0;
    if (len > 0u && fwrite(encoded, 1u, len, file) != len) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

int load_block_model_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_BLOCK_LIBRARY_ROOT, asset_id, SDK_CHARACTER_MODEL_NAME,
                                      path, sizeof(path))) {
        return 0;
    }
    return load_binary_asset_file(path, out_voxels, voxel_count);
}

int save_block_model_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_BLOCK_LIBRARY_ROOT, asset_id, SDK_CHARACTER_MODEL_NAME,
                                      path, sizeof(path))) {
        return 0;
    }
    return save_binary_asset_file(path, voxels, voxel_count);
}

int load_block_icon_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_BLOCK_LIBRARY_ROOT, asset_id, SDK_ASSET_ICON_NAME,
                                      path, sizeof(path))) {
        return 0;
    }
    return load_binary_asset_file(path, out_voxels, voxel_count);
}

int save_block_icon_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_BLOCK_LIBRARY_ROOT, asset_id, SDK_ASSET_ICON_NAME,
                                      path, sizeof(path))) {
        return 0;
    }
    return save_binary_asset_file(path, voxels, voxel_count);
}

int load_item_model_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_ITEM_LIBRARY_ROOT, asset_id, SDK_CHARACTER_MODEL_NAME,
                                      path, sizeof(path))) {
        return 0;
    }
    return load_binary_asset_file(path, out_voxels, voxel_count);
}

int save_item_model_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_ITEM_LIBRARY_ROOT, asset_id, SDK_CHARACTER_MODEL_NAME,
                                      path, sizeof(path))) {
        return 0;
    }
    return save_binary_asset_file(path, voxels, voxel_count);
}

int load_item_icon_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_ITEM_LIBRARY_ROOT, asset_id, SDK_ASSET_ICON_NAME,
                                      path, sizeof(path))) {
        return 0;
    }
    return load_binary_asset_file(path, out_voxels, voxel_count);
}

int save_item_icon_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_ITEM_LIBRARY_ROOT, asset_id, SDK_ASSET_ICON_NAME,
                                      path, sizeof(path))) {
        return 0;
    }
    return save_binary_asset_file(path, voxels, voxel_count);
}

int load_particle_effect_timeline_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_PARTICLE_EFFECT_LIBRARY_ROOT, asset_id,
                                      SDK_PARTICLE_TIMELINE_NAME, path, sizeof(path))) {
        return 0;
    }
    return load_binary_asset_file(path, out_voxels, voxel_count);
}

int save_particle_effect_timeline_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_PARTICLE_EFFECT_LIBRARY_ROOT, asset_id,
                                      SDK_PARTICLE_TIMELINE_NAME, path, sizeof(path))) {
        return 0;
    }
    return save_binary_asset_file(path, voxels, voxel_count);
}

int load_particle_effect_icon_bytes(const char* asset_id, uint8_t* out_voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_PARTICLE_EFFECT_LIBRARY_ROOT, asset_id,
                                      SDK_ASSET_ICON_NAME, path, sizeof(path))) {
        return 0;
    }
    return load_binary_asset_file(path, out_voxels, voxel_count);
}

int save_particle_effect_icon_bytes(const char* asset_id, const uint8_t* voxels, size_t voxel_count)
{
    char path[MAX_PATH];

    if (!build_simple_asset_file_path(SDK_PARTICLE_EFFECT_LIBRARY_ROOT, asset_id,
                                      SDK_ASSET_ICON_NAME, path, sizeof(path))) {
        return 0;
    }
    return save_binary_asset_file(path, voxels, voxel_count);
}

int create_prop_asset(SdkPropAssetMeta* out_meta)
{
    int index;

    if (!ensure_directory_exists_a(SDK_PROP_LIBRARY_ROOT)) return 0;

    for (index = 1; index < 1000; ++index) {
        char asset_id[64];
        char dir_path[MAX_PATH];
        char chunks_dir[MAX_PATH];
        char display_name[SDK_START_MENU_ASSET_NAME_MAX];
        SdkPropAssetMeta meta;

        snprintf(asset_id, sizeof(asset_id), "prop_%03d", index);
        if (!build_simple_asset_dir_path(SDK_PROP_LIBRARY_ROOT, asset_id, dir_path, sizeof(dir_path))) return 0;
        if (GetFileAttributesA(dir_path) != INVALID_FILE_ATTRIBUTES) continue;

        if (!ensure_directory_exists_a(dir_path)) return 0;
        if (!build_prop_chunks_dir_path(asset_id, chunks_dir, sizeof(chunks_dir))) return 0;
        if (!ensure_directory_exists_a(chunks_dir)) return 0;
        snprintf(display_name, sizeof(display_name), "PROP %03d", index);
        if (!write_simple_asset_meta_file(SDK_PROP_LIBRARY_ROOT, asset_id, display_name)) return 0;

        memset(&meta, 0, sizeof(meta));
        strcpy_s(meta.asset_id, sizeof(meta.asset_id), asset_id);
        strcpy_s(meta.display_name, sizeof(meta.display_name), display_name);
        strcpy_s(meta.dir_path, sizeof(meta.dir_path), dir_path);
        strcpy_s(meta.family_name, sizeof(meta.family_name), "unassigned");
        meta.shell_compatible = 1;
        meta.last_write_time = GetTickCount64();
        if (!write_prop_meta_file(&meta)) return 0;
        if (out_meta) *out_meta = meta;
        g_frontend_refresh_pending = true;
        return 1;
    }

    return 0;
}

int copy_prop_asset(int prop_index)
{
    SdkPropAssetMeta copy_meta;
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];
    char new_asset_id[64];
    char new_dir_path[MAX_PATH];

    if (prop_index < 0 || prop_index >= g_prop_asset_count) return 0;
    if (!create_prop_asset(&copy_meta)) return 0;

    strcpy_s(new_asset_id, sizeof(new_asset_id), copy_meta.asset_id);
    strcpy_s(new_dir_path, sizeof(new_dir_path), copy_meta.dir_path);

    recursive_delete_directory_a(copy_meta.dir_path);
    if (!recursive_copy_directory_a(g_prop_assets[prop_index].dir_path, copy_meta.dir_path)) return 0;
    copy_meta = g_prop_assets[prop_index];
    strcpy_s(copy_meta.asset_id, sizeof(copy_meta.asset_id), new_asset_id);
    strcpy_s(copy_meta.dir_path, sizeof(copy_meta.dir_path), new_dir_path);
    snprintf(display_name, sizeof(display_name), "%s COPY", g_prop_assets[prop_index].display_name);
    strcpy_s(copy_meta.display_name, sizeof(copy_meta.display_name), display_name);
    if (!write_prop_meta_file(&copy_meta)) return 0;
    g_frontend_refresh_pending = true;
    return 1;
}

int delete_prop_asset(int prop_index)
{
    if (prop_index < 0 || prop_index >= g_prop_asset_count) return 0;
    if (!recursive_delete_directory_a(g_prop_assets[prop_index].dir_path)) return 0;
    g_frontend_refresh_pending = true;
    return 1;
}

int create_block_asset(SdkBlockAssetMeta* out_meta)
{
    int index;

    if (!ensure_directory_exists_a(SDK_BLOCK_LIBRARY_ROOT)) return 0;

    for (index = 1; index < 1000; ++index) {
        char asset_id[64];
        char dir_path[MAX_PATH];
        char display_name[SDK_START_MENU_ASSET_NAME_MAX];
        uint8_t model[SDK_BLOCK_ITEM_MODEL_VOXELS];
        uint8_t icon[SDK_ASSET_ICON_VOXELS];
        SdkBlockAssetMeta meta;

        snprintf(asset_id, sizeof(asset_id), "block_%03d", index);
        if (!build_simple_asset_dir_path(SDK_BLOCK_LIBRARY_ROOT, asset_id, dir_path, sizeof(dir_path))) return 0;
        if (GetFileAttributesA(dir_path) != INVALID_FILE_ATTRIBUTES) continue;

        if (!ensure_directory_exists_a(dir_path)) return 0;
        snprintf(display_name, sizeof(display_name), "BLOCK %03d", index);
        if (!write_simple_asset_meta_file(SDK_BLOCK_LIBRARY_ROOT, asset_id, display_name)) return 0;
        character_voxel_buffer_clear(model, sizeof(model));
        character_voxel_buffer_clear(icon, sizeof(icon));
        if (!save_block_model_bytes(asset_id, model, sizeof(model))) return 0;
        if (!save_block_icon_bytes(asset_id, icon, sizeof(icon))) return 0;

        memset(&meta, 0, sizeof(meta));
        strcpy_s(meta.asset_id, sizeof(meta.asset_id), asset_id);
        strcpy_s(meta.display_name, sizeof(meta.display_name), display_name);
        strcpy_s(meta.dir_path, sizeof(meta.dir_path), dir_path);
        meta.last_write_time = GetTickCount64();
        if (out_meta) *out_meta = meta;
        g_frontend_refresh_pending = true;
        return 1;
    }

    return 0;
}

int copy_block_asset(int block_index)
{
    SdkBlockAssetMeta copy_meta;
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];

    if (block_index < 0 || block_index >= g_block_asset_count) return 0;
    if (!create_block_asset(&copy_meta)) return 0;

    recursive_delete_directory_a(copy_meta.dir_path);
    if (!recursive_copy_directory_a(g_block_assets[block_index].dir_path, copy_meta.dir_path)) return 0;
    snprintf(display_name, sizeof(display_name), "%s COPY", g_block_assets[block_index].display_name);
    write_simple_asset_meta_file(SDK_BLOCK_LIBRARY_ROOT, copy_meta.asset_id, display_name);
    g_frontend_refresh_pending = true;
    return 1;
}

int delete_block_asset(int block_index)
{
    if (block_index < 0 || block_index >= g_block_asset_count) return 0;
    if (!recursive_delete_directory_a(g_block_assets[block_index].dir_path)) return 0;
    g_frontend_refresh_pending = true;
    return 1;
}

int create_item_asset(SdkItemAssetMeta* out_meta)
{
    int index;

    if (!ensure_directory_exists_a(SDK_ITEM_LIBRARY_ROOT)) return 0;

    for (index = 1; index < 1000; ++index) {
        char asset_id[64];
        char dir_path[MAX_PATH];
        char display_name[SDK_START_MENU_ASSET_NAME_MAX];
        uint8_t model[SDK_BLOCK_ITEM_MODEL_VOXELS];
        uint8_t icon[SDK_ASSET_ICON_VOXELS];
        SdkItemAssetMeta meta;

        snprintf(asset_id, sizeof(asset_id), "item_%03d", index);
        if (!build_simple_asset_dir_path(SDK_ITEM_LIBRARY_ROOT, asset_id, dir_path, sizeof(dir_path))) return 0;
        if (GetFileAttributesA(dir_path) != INVALID_FILE_ATTRIBUTES) continue;

        if (!ensure_directory_exists_a(dir_path)) return 0;
        snprintf(display_name, sizeof(display_name), "ITEM %03d", index);
        if (!write_simple_asset_meta_file(SDK_ITEM_LIBRARY_ROOT, asset_id, display_name)) return 0;
        character_voxel_buffer_clear(model, sizeof(model));
        character_voxel_buffer_clear(icon, sizeof(icon));
        if (!save_item_model_bytes(asset_id, model, sizeof(model))) return 0;
        if (!save_item_icon_bytes(asset_id, icon, sizeof(icon))) return 0;

        memset(&meta, 0, sizeof(meta));
        strcpy_s(meta.asset_id, sizeof(meta.asset_id), asset_id);
        strcpy_s(meta.display_name, sizeof(meta.display_name), display_name);
        strcpy_s(meta.dir_path, sizeof(meta.dir_path), dir_path);
        meta.last_write_time = GetTickCount64();
        if (out_meta) *out_meta = meta;
        g_frontend_refresh_pending = true;
        return 1;
    }

    return 0;
}

int copy_item_asset(int item_index)
{
    SdkItemAssetMeta copy_meta;
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];

    if (item_index < 0 || item_index >= g_item_asset_count) return 0;
    if (!create_item_asset(&copy_meta)) return 0;

    recursive_delete_directory_a(copy_meta.dir_path);
    if (!recursive_copy_directory_a(g_item_assets[item_index].dir_path, copy_meta.dir_path)) return 0;
    snprintf(display_name, sizeof(display_name), "%s COPY", g_item_assets[item_index].display_name);
    write_simple_asset_meta_file(SDK_ITEM_LIBRARY_ROOT, copy_meta.asset_id, display_name);
    g_frontend_refresh_pending = true;
    return 1;
}

int delete_item_asset(int item_index)
{
    if (item_index < 0 || item_index >= g_item_asset_count) return 0;
    if (!recursive_delete_directory_a(g_item_assets[item_index].dir_path)) return 0;
    g_frontend_refresh_pending = true;
    return 1;
}

int create_particle_effect_asset(SdkParticleEffectAssetMeta* out_meta)
{
    int index;

    if (!ensure_directory_exists_a(SDK_PARTICLE_EFFECT_LIBRARY_ROOT)) return 0;

    for (index = 1; index < 1000; ++index) {
        char asset_id[64];
        char dir_path[MAX_PATH];
        char display_name[SDK_START_MENU_ASSET_NAME_MAX];
        uint8_t timeline[SDK_PARTICLE_EFFECT_TIMELINE_VOXELS];
        uint8_t icon[SDK_ASSET_ICON_VOXELS];
        SdkParticleEffectAssetMeta meta;

        snprintf(asset_id, sizeof(asset_id), "particle_effect_%03d", index);
        if (!build_simple_asset_dir_path(SDK_PARTICLE_EFFECT_LIBRARY_ROOT, asset_id, dir_path, sizeof(dir_path))) {
            return 0;
        }
        if (GetFileAttributesA(dir_path) != INVALID_FILE_ATTRIBUTES) continue;

        if (!ensure_directory_exists_a(dir_path)) return 0;
        snprintf(display_name, sizeof(display_name), "PARTICLE EFFECT %03d", index);
        if (!write_particle_effect_meta_file(asset_id, display_name, SDK_PARTICLE_EFFECT_SLICE_COUNT)) return 0;
        character_voxel_buffer_clear(timeline, sizeof(timeline));
        character_voxel_buffer_clear(icon, sizeof(icon));
        if (!save_particle_effect_timeline_bytes(asset_id, timeline, sizeof(timeline))) return 0;
        if (!save_particle_effect_icon_bytes(asset_id, icon, sizeof(icon))) return 0;

        memset(&meta, 0, sizeof(meta));
        strcpy_s(meta.asset_id, sizeof(meta.asset_id), asset_id);
        strcpy_s(meta.display_name, sizeof(meta.display_name), display_name);
        strcpy_s(meta.dir_path, sizeof(meta.dir_path), dir_path);
        meta.slice_count = SDK_PARTICLE_EFFECT_SLICE_COUNT;
        meta.last_write_time = GetTickCount64();
        if (out_meta) *out_meta = meta;
        g_frontend_refresh_pending = true;
        return 1;
    }

    return 0;
}

int copy_particle_effect_asset(int particle_effect_index)
{
    SdkParticleEffectAssetMeta copy_meta;
    char display_name[SDK_START_MENU_ASSET_NAME_MAX];

    if (particle_effect_index < 0 || particle_effect_index >= g_particle_effect_asset_count) return 0;
    if (!create_particle_effect_asset(&copy_meta)) return 0;

    recursive_delete_directory_a(copy_meta.dir_path);
    if (!recursive_copy_directory_a(g_particle_effect_assets[particle_effect_index].dir_path,
                                    copy_meta.dir_path)) {
        return 0;
    }
    snprintf(display_name, sizeof(display_name), "%s COPY",
             g_particle_effect_assets[particle_effect_index].display_name);
    write_particle_effect_meta_file(copy_meta.asset_id, display_name,
                                    g_particle_effect_assets[particle_effect_index].slice_count);
    g_frontend_refresh_pending = true;
    return 1;
}

int delete_particle_effect_asset(int particle_effect_index)
{
    if (particle_effect_index < 0 || particle_effect_index >= g_particle_effect_asset_count) return 0;
    if (!recursive_delete_directory_a(g_particle_effect_assets[particle_effect_index].dir_path)) return 0;
    g_frontend_refresh_pending = true;
    return 1;
}

static void migrate_legacy_character_profile_if_needed(void)
{
    char legacy_path[MAX_PATH];
    FILE* file;
    char line[128];
    char legacy_name[SDK_START_MENU_ASSET_NAME_MAX];

    if (g_character_asset_count > 0) return;
    if (!build_character_profile_path(legacy_path, sizeof(legacy_path))) return;
    file = fopen(legacy_path, "rb");
    if (!file) return;

    strcpy_s(legacy_name, sizeof(legacy_name), "RANGER");
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "name=", 5) == 0) {
            copy_ascii_trimmed(legacy_name, sizeof(legacy_name), line + 5);
            if (!legacy_name[0]) {
                strcpy_s(legacy_name, sizeof(legacy_name), "RANGER");
            }
            break;
        }
    }
    fclose(file);

    {
        SdkCharacterAssetMeta meta;
        if (create_character_asset(&meta)) {
            write_character_meta_file(meta.asset_id, legacy_name);
            g_frontend_refresh_pending = true;
        }
    }
}

void load_character_profile(void)
{
    refresh_character_assets();
    migrate_legacy_character_profile_if_needed();
    if (g_frontend_refresh_pending) {
        refresh_character_assets();
    }
    refresh_animation_assets_for_selected_character();
}
