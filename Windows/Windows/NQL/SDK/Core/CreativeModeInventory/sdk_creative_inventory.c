#include "../Runtime/sdk_runtime_internal.h"

const ItemType g_creative_spawn_items[] = {
    ITEM_PISTOL,
    ITEM_ASSAULT_RIFLE,
    ITEM_SNIPER_RIFLE,
    ITEM_HAND_GRANADE,
    ITEM_SEMTEX,
    ITEM_TACTICAL_GRANADE,
    ITEM_SMOKE_GRANADE,
    ITEM_SPAWNER_BUILDER,
    ITEM_SPAWNER_BLACKSMITH,
    ITEM_SPAWNER_MINER,
    ITEM_SPAWNER_SOLDIER,
    ITEM_SPAWNER_GENERAL,
    ITEM_SPAWNER_CAR,
    ITEM_SPAWNER_MOTORBIKE,
    ITEM_SPAWNER_TANK
};

void normalize_label(const char* src, char* dst, size_t dst_cap)
{
    /* Normalizes string: underscores to spaces, uppercase to lowercase */
    size_t i = 0;
    if (!dst || dst_cap == 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[i] != '\0' && i + 1 < dst_cap) {
        char ch = src[i];
        if (ch == '_') ch = ' ';
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        dst[i] = ch;
        ++i;
    }
    dst[i] = '\0';
}

int block_matches_creative_search(BlockType block)
{
    /* Checks if block matches current creative menu filter and search text */
    char name_buf[64];
    char search_buf[SDK_PAUSE_MENU_SEARCH_MAX];

    if (block <= BLOCK_AIR || block >= BLOCK_COUNT) return 0;
    if (g_creative_menu_filter == SDK_CREATIVE_FILTER_BUILDING_BLOCKS && sdk_block_is_color(block)) return 0;
    if (g_creative_menu_filter == SDK_CREATIVE_FILTER_COLORS && !sdk_block_is_color(block)) return 0;
    if (g_creative_menu_filter == SDK_CREATIVE_FILTER_ITEMS) return 0;
    if (g_creative_menu_search_len <= 0) return 1;

    normalize_label(sdk_block_get_name(block), name_buf, sizeof(name_buf));
    normalize_label(g_creative_menu_search, search_buf, sizeof(search_buf));
    return strstr(name_buf, search_buf) != NULL;
}

int creative_item_grant_count(ItemType item)
{
    /* Returns stack size when granting item in creative mode */
    if (sdk_item_is_throwable(item)) return 8;
    if (sdk_item_is_firearm(item)) return 1;
    return sdk_item_is_spawn_item(item) ? CREATIVE_SPAWNER_STACK : 1;
}

CreativeEntry creative_entry_none(void)
{
    /* Returns empty creative entry (AIR block) */
    CreativeEntry entry;
    entry.kind = SDK_CREATIVE_ENTRY_BLOCK;
    entry.id = BLOCK_AIR;
    return entry;
}

int item_matches_creative_search(ItemType item)
{
    /* Checks if item matches current creative menu filter and search text */
    char name_buf[64];
    char search_buf[SDK_PAUSE_MENU_SEARCH_MAX];

    if (item == ITEM_NONE) return 0;
    if (g_creative_menu_filter == SDK_CREATIVE_FILTER_BUILDING_BLOCKS) return 0;
    if (g_creative_menu_filter == SDK_CREATIVE_FILTER_COLORS) return 0;
    if (g_creative_menu_filter == SDK_CREATIVE_FILTER_ITEMS) {
        /* Items-only filter stays item-only. */
    }
    if (g_creative_menu_search_len <= 0) return 1;

    normalize_label(sdk_item_get_name(item), name_buf, sizeof(name_buf));
    normalize_label(g_creative_menu_search, search_buf, sizeof(search_buf));
    return strstr(name_buf, search_buf) != NULL;
}

int creative_visible_entry_count(void)
{
    /* Counts total visible entries (items + blocks) matching current filter/search */
    int count = 0;

    for (int i = 0; i < (int)(sizeof(g_creative_spawn_items) / sizeof(g_creative_spawn_items[0])); ++i) {
        if (item_matches_creative_search(g_creative_spawn_items[i])) {
            count++;
        }
    }
    for (int block = BLOCK_AIR + 1; block < BLOCK_COUNT; ++block) {
        if (block_matches_creative_search((BlockType)block)) {
            count++;
        }
    }
    return count;
}

CreativeEntry creative_entry_for_filtered_index(int index)
{
    /* Returns creative entry at filtered index (items first, then blocks) */
    int cursor = 0;
    CreativeEntry entry = creative_entry_none();

    for (int i = 0; i < (int)(sizeof(g_creative_spawn_items) / sizeof(g_creative_spawn_items[0])); ++i) {
        if (!item_matches_creative_search(g_creative_spawn_items[i])) continue;
        if (cursor == index) {
            entry.kind = SDK_CREATIVE_ENTRY_ITEM;
            entry.id = (int)g_creative_spawn_items[i];
            return entry;
        }
        cursor++;
    }

    for (int block = BLOCK_AIR + 1; block < BLOCK_COUNT; ++block) {
        if (!block_matches_creative_search((BlockType)block)) continue;
        if (cursor == index) {
            entry.kind = SDK_CREATIVE_ENTRY_BLOCK;
            entry.id = block;
            return entry;
        }
        cursor++;
    }

    return entry;
}

void creative_clamp_selection(void)
{
    /* Clamps selection and scroll to valid range for visible entries */
    int total = creative_visible_entry_count();

    if (total <= 0) {
        g_creative_menu_selected = 0;
        g_creative_menu_scroll = 0;
        return;
    }
    if (g_creative_menu_selected < 0) g_creative_menu_selected = 0;
    if (g_creative_menu_selected >= total) g_creative_menu_selected = total - 1;
    if (g_creative_menu_scroll > g_creative_menu_selected) {
        g_creative_menu_scroll = g_creative_menu_selected;
    }
    if (g_creative_menu_selected >= g_creative_menu_scroll + SDK_CREATIVE_MENU_VISIBLE_ROWS) {
        g_creative_menu_scroll = g_creative_menu_selected - SDK_CREATIVE_MENU_VISIBLE_ROWS + 1;
    }
    if (g_creative_menu_scroll < 0) g_creative_menu_scroll = 0;
}
