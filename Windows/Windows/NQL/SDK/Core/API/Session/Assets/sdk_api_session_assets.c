int find_character_asset_index_by_id_local(const char* asset_id)
{
    /* Finds character asset index by ID, returns -1 if not found */
    int i;
    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_character_asset_count; ++i) {
        if (strcmp(g_character_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

int find_animation_asset_index_by_id_local(const char* asset_id)
{
    /* Finds animation asset index by ID, returns -1 if not found */
    int i;
    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_animation_asset_count; ++i) {
        if (strcmp(g_animation_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

int find_prop_asset_index_by_id_local(const char* asset_id)
{
    /* Finds prop asset index by ID, returns -1 if not found */
    int i;
    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_prop_asset_count; ++i) {
        if (strcmp(g_prop_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

int find_block_asset_index_by_id_local(const char* asset_id)
{
    /* Finds block asset index by ID, returns -1 if not found */
    int i;
    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_block_asset_count; ++i) {
        if (strcmp(g_block_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

int find_item_asset_index_by_id_local(const char* asset_id)
{
    /* Finds item asset index by ID, returns -1 if not found */
    int i;
    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_item_asset_count; ++i) {
        if (strcmp(g_item_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}

int find_particle_effect_asset_index_by_id_local(const char* asset_id)
{
    /* Finds particle effect asset index by ID, returns -1 if not found */
    int i;
    if (!asset_id || !asset_id[0]) return -1;
    for (i = 0; i < g_particle_effect_asset_count; ++i) {
        if (strcmp(g_particle_effect_assets[i].asset_id, asset_id) == 0) {
            return i;
        }
    }
    return -1;
}