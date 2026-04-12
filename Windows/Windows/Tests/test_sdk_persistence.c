/**
 * test_sdk_persistence.c -- Persistence chunk-cache regression checks.
 *
 * Build (MSVC, from the Windows/ directory):
 *   cl /O2 /W4 /I Windows/NQL/SDK /I Windows/NQL/SDK/Core ^
 *      Windows/Tests/test_sdk_persistence.c ^
 *      Windows/NQL/SDK/Core/sdk_chunk.c ^
 *      Windows/NQL/SDK/Core/sdk_persistence.c ^
 *      /Fe:Windows/Tests/test_sdk_persistence.exe
 */
#include "../NQL/SDK/Core/sdk_persistence.h"
#include "../NQL/SDK/Core/sdk_chunk_manager.h"
#include "../NQL/SDK/Core/sdk_construction_cells.h"
#include "../NQL/SDK/Core/sdk_simulation.h"
#include "../NQL/SDK/Core/sdk_settings.h"
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void init_test_world_desc(SdkWorldDesc* out_desc, uint32_t seed)
{
    memset(out_desc, 0, sizeof(*out_desc));
    out_desc->seed = seed;
    out_desc->sea_level = 192;
    out_desc->macro_cell_size = 32;
}

static void make_temp_save_path(char* out_path, size_t out_path_len)
{
    char temp_dir[MAX_PATH];
    DWORD len = GetTempPathA((DWORD)sizeof(temp_dir), temp_dir);
    if (len == 0 || len >= sizeof(temp_dir)) {
        strcpy_s(temp_dir, sizeof(temp_dir), ".\\");
    }
    sprintf_s(out_path, out_path_len, "%scpss_persist_%lu.json",
              temp_dir, (unsigned long)GetCurrentProcessId());
}

static int count_chunk_entries_in_file(const char* path)
{
    FILE* file;
    long size;
    char* text;
    int count = 0;
    char* cursor;

    if (!path) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    text = (char*)malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        return 0;
    }
    if (fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return 0;
    }
    fclose(file);
    text[size] = '\0';

    cursor = text;
    while ((cursor = strstr(cursor, "\"cx\":")) != NULL) {
        count++;
        cursor += 5;
    }

    free(text);
    return count;
}

static void seed_chunk(SdkChunk* chunk)
{
    ASSERT_TRUE(chunk != NULL);
    ASSERT_TRUE(chunk->blocks != NULL);
    if (!chunk || !chunk->blocks) return;
    sdk_chunk_set_block(chunk, 0, 0, 0, BLOCK_STONE);
}

TEST(persistence_roundtrip_preserves_superchunk_neighborhood)
{
    char save_path[MAX_PATH];
    SdkWorldDesc world_desc;
    SdkPersistence persistence;
    int stored_chunks = 0;

    make_temp_save_path(save_path, sizeof(save_path));
    DeleteFileA(save_path);

    init_test_world_desc(&world_desc, 0x12345678u);
    memset(&persistence, 0, sizeof(persistence));
    sdk_persistence_init(&persistence, &world_desc, save_path);

    for (int cz = -1; cz <= 16; ++cz) {
        for (int cx = -1; cx <= 16; ++cx) {
            SdkChunk chunk;
            sdk_chunk_init(&chunk, cx, cz, NULL);
            ASSERT_TRUE(chunk.blocks != NULL);
            seed_chunk(&chunk);
            sdk_persistence_store_chunk(&persistence, &chunk);
            sdk_chunk_free(&chunk);
            stored_chunks++;
        }
    }

    sdk_persistence_save(&persistence);
    sdk_persistence_shutdown(&persistence);

    ASSERT_INT_EQ(count_chunk_entries_in_file(save_path), stored_chunks);

    memset(&persistence, 0, sizeof(persistence));
    sdk_persistence_init(&persistence, &world_desc, save_path);

    for (int cz = -1; cz <= 16; ++cz) {
        for (int cx = -1; cx <= 16; ++cx) {
            SdkChunk chunk;
            sdk_chunk_init(&chunk, cx, cz, NULL);
            ASSERT_TRUE(chunk.blocks != NULL);
            ASSERT_TRUE(sdk_persistence_load_chunk(&persistence, cx, cz, &chunk));
            ASSERT_INT_EQ((int)sdk_world_cell_decode_full_block(chunk.blocks[0]), (int)BLOCK_STONE);
            sdk_chunk_free(&chunk);
        }
    }

    sdk_persistence_shutdown(&persistence);
    DeleteFileA(save_path);
}

int main(void)
{
    RUN_TEST(persistence_roundtrip_preserves_superchunk_neighborhood);
    test_print_summary();
    return g_test_failed == 0 ? 0 : 1;
}

void sdk_renderer_free_chunk_mesh(SdkChunk* chunk)
{
    (void)chunk;
}

void sdk_renderer_free_chunk_unified_buffer(SdkChunk* chunk)
{
    (void)chunk;
}

bool sdk_frustum_contains_aabb(const SdkFrustum* frustum,
                               float min_x, float min_y, float min_z,
                               float max_x, float max_y, float max_z)
{
    (void)frustum;
    (void)min_x;
    (void)min_y;
    (void)min_z;
    (void)max_x;
    (void)max_y;
    (void)max_z;
    return true;
}

SdkChunkSimState* sdk_simulation_chunk_state_create(void)
{
    return (SdkChunkSimState*)calloc(1, sizeof(SdkChunkSimState));
}

void sdk_simulation_chunk_state_destroy(SdkChunkSimState* state)
{
    free(state);
}

void sdk_simulation_chunk_state_clear(SdkChunkSimState* state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

char* sdk_simulation_encode_chunk_fluids(const SdkChunk* chunk)
{
    char* encoded;
    (void)chunk;
    encoded = (char*)malloc(1u);
    if (encoded) encoded[0] = '\0';
    return encoded;
}

int sdk_simulation_decode_chunk_fluids(SdkChunk* chunk, const char* encoded)
{
    (void)chunk;
    (void)encoded;
    return 1;
}

void sdk_construction_store_free(SdkConstructionCellStore* store)
{
    (void)store;
}

void sdk_construction_payload_refresh_metadata(SdkConstructionItemPayload* payload)
{
    (void)payload;
}

void sdk_construction_registry_clear(SdkConstructionArchetypeRegistry* registry)
{
    (void)registry;
}

char* sdk_construction_encode_registry(const SdkConstructionArchetypeRegistry* registry)
{
    char* encoded;
    (void)registry;
    encoded = (char*)malloc(1u);
    if (encoded) encoded[0] = '\0';
    return encoded;
}

int sdk_construction_decode_registry(SdkConstructionArchetypeRegistry* registry, const char* encoded)
{
    (void)registry;
    (void)encoded;
    return 1;
}

char* sdk_construction_encode_store(const SdkChunk* chunk)
{
    char* encoded;
    (void)chunk;
    encoded = (char*)malloc(1u);
    if (encoded) encoded[0] = '\0';
    return encoded;
}

int sdk_construction_decode_store(SdkChunk* chunk, const char* encoded)
{
    (void)chunk;
    (void)encoded;
    return 1;
}

void sdk_graphics_settings_default(SdkGraphicsSettings* out_settings)
{
    if (!out_settings) return;
    memset(out_settings, 0, sizeof(*out_settings));
    out_settings->chunk_grid_size = CHUNK_GRID_DEFAULT_SIZE;
}

int sdk_graphics_settings_load(SdkGraphicsSettings* out_settings)
{
    sdk_graphics_settings_default(out_settings);
    return 0;
}

void sdk_graphics_settings_save(const SdkGraphicsSettings* settings)
{
    (void)settings;
}
