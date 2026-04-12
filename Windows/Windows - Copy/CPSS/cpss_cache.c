/**
 * cpss_cache.c - Cache infrastructure: hash map, wheel cache, hot cache, rank index
 * Part of the CPSS Viewer amalgamation.
 */
/* ── Cache hash map helpers ─────────────────────────────────────────── */

/**
 * A simple open-addressing hash map implementation used for O(1) cache lookups.
 * The hash map stores indices into a dense array of cache entries.
 * Keys are implicitly the (stream_id, segment_no) pair stored in the dense entry.
 */

/** Hash a (stream_id, segment_no) key for cache lookup. */
static inline size_t cache_key_hash(uint32_t stream_id, uint64_t segment_no) {
    uint64_t h = (uint64_t)stream_id * 2654435761ull ^ segment_no * 2246822519ull;
    h ^= h >> 17u;
    return (size_t)h;
}

/** Allocate or clear a hash map to all-empty (SIZE_MAX). */
static void hashmap_clear(size_t* map, size_t cap) {
    for (size_t i = 0; i < cap; ++i) map[i] = SIZE_MAX;
}

/** Ensure hash map is allocated with capacity >= needed (power of 2). */
static void hashmap_ensure(size_t** map, size_t* cap, size_t needed) {
    if (*cap >= needed && *map) return;
    size_t new_cap = *cap ? *cap : 16u;
    while (new_cap < needed) new_cap *= 2u;
    free(*map);
    *map = (size_t*)xmalloc(new_cap * sizeof(size_t));
    *cap = new_cap;
    hashmap_clear(*map, new_cap);
}

/** Insert slot_idx into the hash map (open addressing, linear probe). */
static void hashmap_put(size_t* map, size_t cap, uint32_t stream_id, uint64_t segment_no, size_t slot_idx) {
    size_t mask = cap - 1u;
    size_t h = cache_key_hash(stream_id, segment_no) & mask;
    while (map[h] != SIZE_MAX) {
        h = (h + 1u) & mask;
    }
    map[h] = slot_idx;
}

/** Probe the hash map for (stream_id, segment_no). Returns the slot_idx or SIZE_MAX. */
#define HASHMAP_PROBE(map, cap, entries, sid, segno, result) do { \
    size_t _mask = (cap) - 1u; \
    size_t _h = cache_key_hash((sid), (segno)) & _mask; \
    (result) = SIZE_MAX; \
    while ((map)[_h] != SIZE_MAX) { \
        size_t _si = (map)[_h]; \
        if ((entries)[_si].occupied && (entries)[_si].stream_id == (sid) && \
            (entries)[_si].segment_no == (segno)) { \
            (result) = _si; break; \
        } \
        _h = (_h + 1u) & _mask; \
    } \
} while(0)

/* ── Wheel / presieve cache helpers ─────────────────────────────────── */

/**
 * The Wheel Cache (or Presieve Cache) stores the generated candidate bitmaps 
 * for segments. Since generating presieve bitmaps takes CPU time, caching them
 * speeds up sequential scans and queries within the same segment.
 * It uses a strict byte-budget LRU eviction policy.
 */

/** Initialize the global wheel cache with the given budget. */
static void wheel_cache_init(size_t budget) {
    memset(&g_wheel_cache, 0, sizeof(g_wheel_cache));
    g_wheel_cache.budget = budget;
}

/** Free all entries and reset the cache. */
static void wheel_cache_reset(void) {
    for (size_t i = 0; i < g_wheel_cache.cap; ++i) {
        if (g_wheel_cache.entries[i].occupied) {
            free(g_wheel_cache.entries[i].data);
        }
    }
    size_t budget = g_wheel_cache.budget;
    free(g_wheel_cache.entries);
    free(g_wheel_cache.hash_map);
    memset(&g_wheel_cache, 0, sizeof(g_wheel_cache));
    g_wheel_cache.budget = budget;
}

/** Return the current bytes used in the cache. */
static size_t wheel_cache_bytes_used(void) {
    return g_wheel_cache.bytes_used;
}

/** Return the current number of entries in the cache. */
static size_t wheel_cache_entry_count(void) {
    return g_wheel_cache.count;
}

/** Look up a segment in the cache by (stream_id, segment_no). O(1) average via hash. */
static const uint8_t* wheel_cache_lookup(uint32_t stream_id, uint64_t segment_no, size_t* out_len) {
    if (g_wheel_cache.hash_cap == 0u || g_wheel_cache.count == 0u) return NULL;
    size_t si;
    HASHMAP_PROBE(g_wheel_cache.hash_map, g_wheel_cache.hash_cap,
        g_wheel_cache.entries, stream_id, segment_no, si);
    if (si == SIZE_MAX) return NULL;
    WheelCacheEntry* e = &g_wheel_cache.entries[si];
    if (!g_cache_readonly) {
        e->last_use = ++g_wheel_cache.use_counter;
    }
    if (out_len) *out_len = e->bytes;
    return e->data;
}

/** Rebuild the wheel cache hash map from all occupied entries. */
static void wheel_cache_rebuild_hash(void) {
    size_t needed = g_wheel_cache.cap < 16u ? 32u : g_wheel_cache.cap * 2u;
    hashmap_ensure(&g_wheel_cache.hash_map, &g_wheel_cache.hash_cap, needed);
    hashmap_clear(g_wheel_cache.hash_map, g_wheel_cache.hash_cap);
    for (size_t i = 0; i < g_wheel_cache.cap; ++i) {
        if (g_wheel_cache.entries[i].occupied) {
            hashmap_put(g_wheel_cache.hash_map, g_wheel_cache.hash_cap,
                g_wheel_cache.entries[i].stream_id,
                g_wheel_cache.entries[i].segment_no, i);
        }
    }
}

/** Evict the least-recently-used entry. Returns false if cache is empty. */
static bool wheel_cache_evict_lru(void) {
    size_t victim = SIZE_MAX;
    uint64_t oldest = UINT64_MAX;
    for (size_t i = 0; i < g_wheel_cache.cap; ++i) {
        WheelCacheEntry* e = &g_wheel_cache.entries[i];
        if (e->occupied && e->last_use < oldest) {
            oldest = e->last_use;
            victim = i;
        }
    }
    if (victim == SIZE_MAX) return false;
    WheelCacheEntry* v = &g_wheel_cache.entries[victim];
    g_wheel_cache.bytes_used -= v->bytes;
    g_wheel_cache.count -= 1u;
    free(v->data);
    memset(v, 0, sizeof(*v));
    wheel_cache_rebuild_hash();
    return true;
}

/**
 * Insert a presieve bitmap into the cache for the given segment.
 * Takes ownership of `data` (caller must NOT free it afterwards).
 * If the entry is larger than the budget, it is freed and not cached.
 * Returns true if successfully cached.
 */
static bool wheel_cache_insert(uint32_t stream_id, uint64_t segment_no, uint64_t start_index,
    uint32_t total_candidates, uint8_t* data, size_t bytes) {
    if (g_wheel_cache.budget == 0 || bytes > g_wheel_cache.budget) {
        free(data);
        return false;
    }

    /* Evict until there is room */
    while (g_wheel_cache.bytes_used + bytes > g_wheel_cache.budget) {
        if (!wheel_cache_evict_lru()) break;
    }
    if (g_wheel_cache.bytes_used + bytes > g_wheel_cache.budget) {
        free(data);
        return false;
    }

    /* Find a free slot, growing if needed */
    size_t slot = SIZE_MAX;
    for (size_t i = 0; i < g_wheel_cache.cap; ++i) {
        if (!g_wheel_cache.entries[i].occupied) { slot = i; break; }
    }
    bool grew = false;
    if (slot == SIZE_MAX) {
        size_t new_cap = g_wheel_cache.cap ? g_wheel_cache.cap * 2u : 16u;
        g_wheel_cache.entries = (WheelCacheEntry*)xrealloc(
            g_wheel_cache.entries, new_cap * sizeof(WheelCacheEntry));
        for (size_t i = g_wheel_cache.cap; i < new_cap; ++i) {
            memset(&g_wheel_cache.entries[i], 0, sizeof(WheelCacheEntry));
        }
        slot = g_wheel_cache.cap;
        g_wheel_cache.cap = new_cap;
        grew = true;
    }

    WheelCacheEntry* e = &g_wheel_cache.entries[slot];
    e->stream_id = stream_id;
    e->segment_no = segment_no;
    e->start_index = start_index;
    e->total_candidates = total_candidates;
    e->data = data;
    e->bytes = bytes;
    e->last_use = ++g_wheel_cache.use_counter;
    e->occupied = true;
    g_wheel_cache.bytes_used += bytes;
    g_wheel_cache.count += 1u;

    /* Incremental hash insert; full rebuild only when array grew */
    if (grew) {
        wheel_cache_rebuild_hash();
    } else {
        size_t needed = g_wheel_cache.count * 2u;
        if (needed > g_wheel_cache.hash_cap) {
            wheel_cache_rebuild_hash();
        } else {
            hashmap_put(g_wheel_cache.hash_map, g_wheel_cache.hash_cap,
                stream_id, segment_no, slot);
        }
    }
    return true;
}

/** Purge all wheel cache entries belonging to a specific stream_id. */
static void wheel_cache_purge_stream(uint32_t stream_id) {
    bool any_removed = false;
    for (size_t i = 0; i < g_wheel_cache.cap; ++i) {
        WheelCacheEntry* e = &g_wheel_cache.entries[i];
        if (e->occupied && e->stream_id == stream_id) {
            g_wheel_cache.bytes_used -= e->bytes;
            g_wheel_cache.count -= 1u;
            free(e->data);
            memset(e, 0, sizeof(*e));
            any_removed = true;
        }
    }
    if (any_removed) wheel_cache_rebuild_hash();
}

/* ── Hot decoded segment cache helpers ──────────────────────────────── */

static void hot_cache_init(size_t budget) {
    memset(&g_hot_cache, 0, sizeof(g_hot_cache));
    g_hot_cache.budget = budget;
}

static void hot_cache_free_entry(HotSegmentEntry* e) {
    free(e->presieve);
    free(e->result_bits);
    free(e->rank_prefix);
    free(e->rbits_rank);
    memset(e, 0, sizeof(*e));
}

static void hot_cache_reset(void) {
    for (size_t i = 0; i < g_hot_cache.cap; ++i) {
        if (g_hot_cache.entries[i].occupied) hot_cache_free_entry(&g_hot_cache.entries[i]);
    }
    size_t budget = g_hot_cache.budget;
    free(g_hot_cache.entries);
    free(g_hot_cache.hash_map);
    memset(&g_hot_cache, 0, sizeof(g_hot_cache));
    g_hot_cache.budget = budget;
}

/** Rebuild the hot cache hash map from all occupied entries. */
static void hot_cache_rebuild_hash(void) {
    size_t needed = g_hot_cache.cap < 16u ? 32u : g_hot_cache.cap * 2u;
    hashmap_ensure(&g_hot_cache.hash_map, &g_hot_cache.hash_cap, needed);
    hashmap_clear(g_hot_cache.hash_map, g_hot_cache.hash_cap);
    for (size_t i = 0; i < g_hot_cache.cap; ++i) {
        if (g_hot_cache.entries[i].occupied) {
            hashmap_put(g_hot_cache.hash_map, g_hot_cache.hash_cap,
                g_hot_cache.entries[i].stream_id,
                g_hot_cache.entries[i].segment_no, i);
        }
    }
}

/** Purge all hot cache entries belonging to a specific stream_id. */
static void hot_cache_purge_stream(uint32_t stream_id) {
    bool any_removed = false;
    for (size_t i = 0; i < g_hot_cache.cap; ++i) {
        HotSegmentEntry* e = &g_hot_cache.entries[i];
        if (e->occupied && e->stream_id == stream_id) {
            g_hot_cache.bytes_used -= e->total_bytes;
            g_hot_cache.count -= 1u;
            hot_cache_free_entry(e);
            any_removed = true;
        }
    }
    if (any_removed) hot_cache_rebuild_hash();
}

static bool hot_cache_evict_lru(void) {
    size_t victim = SIZE_MAX;
    uint64_t oldest = UINT64_MAX;
    for (size_t i = 0; i < g_hot_cache.cap; ++i) {
        HotSegmentEntry* e = &g_hot_cache.entries[i];
        if (e->occupied && e->last_use < oldest) { oldest = e->last_use; victim = i; }
    }
    if (victim == SIZE_MAX) return false;
    HotSegmentEntry* v = &g_hot_cache.entries[victim];
    g_hot_cache.bytes_used -= v->total_bytes;
    g_hot_cache.count -= 1u;
    hot_cache_free_entry(v);
    hot_cache_rebuild_hash();
    return true;
}

/** Look up a hot segment by (stream_id, segment_no). O(1) average via hash. */
static HotSegmentEntry* hot_cache_lookup(uint32_t stream_id, uint64_t segment_no) {
    if (g_hot_cache.hash_cap == 0u || g_hot_cache.count == 0u) return NULL;
    size_t si;
    HASHMAP_PROBE(g_hot_cache.hash_map, g_hot_cache.hash_cap,
        g_hot_cache.entries, stream_id, segment_no, si);
    if (si == SIZE_MAX) return NULL;
    HotSegmentEntry* e = &g_hot_cache.entries[si];
    if (!g_cache_readonly) {
        e->last_use = ++g_hot_cache.use_counter;
    }
    return e;
}

/**
 * Build a survivor-rank prefix index for a presieve bitmap.
 * rank_prefix[k] = number of set bits in presieve[0 .. k*RANK_BLOCK_SIZE).
 * Allows computing survivor_pos for any local index in ~RANK_BLOCK_SIZE/8 pops.
 */
static void build_rank_index(const uint8_t* presieve, uint32_t total_candidates,
    uint32_t** out_rank, size_t* out_blocks) {
    size_t n_blocks = ((size_t)total_candidates + RANK_BLOCK_SIZE - 1u) / RANK_BLOCK_SIZE;
    uint32_t* rank = (uint32_t*)xmalloc((n_blocks + 1u) * sizeof(uint32_t));
    rank[0] = 0u;
    for (size_t b = 0u; b < n_blocks; ++b) {
        uint64_t block_start = (uint64_t)b * RANK_BLOCK_SIZE;
        uint64_t block_end = block_start + RANK_BLOCK_SIZE;
        if (block_end > total_candidates) block_end = total_candidates;
        uint64_t full_bytes = (block_end - block_start) >> 3u;
        uint64_t survivors = 0u;
        const uint8_t* p = presieve + (block_start >> 3u);
        /* Handle alignment: block_start is always block-aligned (multiple of RANK_BLOCK_SIZE)
           and RANK_BLOCK_SIZE is a multiple of 8, so block_start is byte-aligned. */
        for (uint64_t j = 0u; j < full_bytes; ++j) {
            survivors += g_popcount8[p[j]];
        }
        for (uint64_t bit = block_start + (full_bytes << 3u); bit < block_end; ++bit) {
            survivors += (uint64_t)((presieve[bit >> 3u] >> (bit & 7u)) & 1u);
        }
        rank[b + 1u] = rank[b] + (uint32_t)survivors;
    }
    *out_rank = rank;
    *out_blocks = n_blocks;
}

/**
 * Compute survivor_pos for local_idx using the rank index.
 * Equivalent to count_set_bits_prefix(presieve, local_idx) but O(RANK_BLOCK_SIZE/8).
 */
static uint64_t survivor_pos_ranked(const uint8_t* presieve, const uint32_t* rank_prefix,
    uint64_t local_idx) {
    size_t block = (size_t)(local_idx / RANK_BLOCK_SIZE);
    uint64_t block_start = (uint64_t)block * RANK_BLOCK_SIZE;
    uint64_t count = rank_prefix[block];
    /* Scan only within the block */
    uint64_t scan_end = local_idx;
    uint64_t full_bytes = (scan_end - block_start) >> 3u;
    const uint8_t* p = presieve + (block_start >> 3u);
    for (uint64_t j = 0u; j < full_bytes; ++j) {
        count += g_popcount8[p[j]];
    }
    for (uint64_t bit = block_start + (full_bytes << 3u); bit < scan_end; ++bit) {
        count += (uint64_t)((presieve[bit >> 3u] >> (bit & 7u)) & 1u);
    }
    return count;
}

/**
 * Select the k-th set bit in result_bits (1-indexed).
 * Returns the bit index of the k-th set bit, or UINT64_MAX if k exceeds total.
 * Uses binary search on rbits_rank for O(log(blocks) + RANK_BLOCK_SIZE/8).
 */
static uint64_t rbits_select(const HotSegmentEntry* hot, uint64_t k) {
    if (!hot || !hot->rbits_rank || k == 0u) return UINT64_MAX;

    size_t rb_total = (size_t)hot->result_bits_len * 8u;
    uint64_t total_primes = hot->rbits_rank[hot->rbits_rank_blocks];
    if (k > total_primes) return UINT64_MAX;

    /* Binary search rbits_rank to find the block containing the k-th set bit.
     * rbits_rank[b] = number of set bits in result_bits[0 .. b*RANK_BLOCK_SIZE). */
    size_t block = 0u;
    {
        size_t lo = 0u, hi = hot->rbits_rank_blocks;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2u;
            if (hot->rbits_rank[mid + 1u] < (uint32_t)k) lo = mid + 1u;
            else hi = mid;
        }
        block = lo;
    }

    /* k-th set bit is within this block; find it with a linear scan */
    uint64_t primes_before = hot->rbits_rank[block];
    uint64_t target = k - primes_before; /* 1-indexed within block */
    uint64_t rb_start = (uint64_t)block * RANK_BLOCK_SIZE;
    uint64_t rb_end = rb_start + RANK_BLOCK_SIZE;
    if (rb_end > rb_total) rb_end = rb_total;

    /* Byte-level scan for speed: count set bits per byte until we reach the target byte */
    uint64_t count = 0u;
    uint64_t byte_start = rb_start >> 3u;
    uint64_t byte_end = (rb_end + 7u) >> 3u;
    for (uint64_t bi = byte_start; bi < byte_end; ++bi) {
        uint8_t byte_val = hot->result_bits[bi];
        uint8_t bc = g_popcount8[byte_val];
        if (count + bc >= target) {
            /* Target bit is in this byte — find exact position */
            for (uint8_t bit = 0u; bit < 8u; ++bit) {
                uint64_t abs_bit = bi * 8u + bit;
                if (abs_bit < rb_start) continue;
                if (abs_bit >= rb_end) return UINT64_MAX;
                if ((byte_val >> bit) & 1u) {
                    ++count;
                    if (count == target) return abs_bit;
                }
            }
        }
        count += bc;
    }
    return UINT64_MAX;
}

/* Forward declaration - defined after cpss_stream.c provides get_bit/index_to_candidate */
static uint64_t rbits_pos_to_candidate(const HotSegmentEntry* hot, uint64_t start_index, uint64_t rb_pos);

/**
 * Insert a fully decoded hot segment into the cache.
 * Takes ownership of presieve, result_bits, rank_prefix.
 * Returns true if successfully cached.
 */
static bool hot_cache_insert(
    uint32_t stream_id, uint64_t segment_no, uint64_t start_index,
    uint32_t total_candidates, uint32_t survivor_count,
    uint8_t* presieve, size_t presieve_len,
    uint8_t* rbits, uint32_t rbits_len,
    uint32_t* rank_prefix, size_t rank_blocks,
    uint64_t prime_count
) {
    size_t total_bytes = presieve_len + rbits_len + (rank_blocks + 1u) * sizeof(uint32_t);

    if (g_hot_cache.budget == 0 || total_bytes > g_hot_cache.budget) {
        free(presieve); free(rbits); free(rank_prefix);
        return false;
    }

    while (g_hot_cache.bytes_used + total_bytes > g_hot_cache.budget) {
        if (!hot_cache_evict_lru()) break;
    }
    if (g_hot_cache.bytes_used + total_bytes > g_hot_cache.budget) {
        free(presieve); free(rbits); free(rank_prefix);
        return false;
    }

    /* Find free slot */
    size_t slot = SIZE_MAX;
    for (size_t i = 0; i < g_hot_cache.cap; ++i) {
        if (!g_hot_cache.entries[i].occupied) { slot = i; break; }
    }
    bool grew = false;
    if (slot == SIZE_MAX) {
        size_t new_cap = g_hot_cache.cap ? g_hot_cache.cap * 2u : 16u;
        g_hot_cache.entries = (HotSegmentEntry*)xrealloc(
            g_hot_cache.entries, new_cap * sizeof(HotSegmentEntry));
        for (size_t i = g_hot_cache.cap; i < new_cap; ++i) {
            memset(&g_hot_cache.entries[i], 0, sizeof(HotSegmentEntry));
        }
        slot = g_hot_cache.cap;
        g_hot_cache.cap = new_cap;
        grew = true;
    }

    HotSegmentEntry* e = &g_hot_cache.entries[slot];
    e->stream_id = stream_id;
    e->segment_no = segment_no;
    e->start_index = start_index;
    e->total_candidates = total_candidates;
    e->survivor_count = survivor_count;
    e->presieve = presieve;
    e->presieve_len = presieve_len;
    e->result_bits = rbits;
    e->result_bits_len = rbits_len;
    e->rank_prefix = rank_prefix;
    e->rank_blocks = rank_blocks;

    /* Build result_bits rank prefix for fast prime counting within segments.
     * rbits_rank[k] = number of set bits in result_bits[0 .. k*RANK_BLOCK_SIZE). */
    {
        size_t rb_total = (size_t)rbits_len * 8u; /* total bits in result_bits */
        size_t rb_nblocks = (rb_total + RANK_BLOCK_SIZE - 1u) / RANK_BLOCK_SIZE;
        uint32_t* rr = (uint32_t*)xmalloc((rb_nblocks + 1u) * sizeof(uint32_t));
        rr[0] = 0u;
        for (size_t b = 0u; b < rb_nblocks; ++b) {
            uint64_t bstart = (uint64_t)b * RANK_BLOCK_SIZE;
            uint64_t bend = bstart + RANK_BLOCK_SIZE;
            if (bend > rb_total) bend = rb_total;
            uint64_t fbytes = (bend - bstart) >> 3u;
            uint64_t primes = 0u;
            const uint8_t* p = rbits + (bstart >> 3u);
            for (uint64_t j = 0u; j < fbytes; ++j) primes += g_popcount8[p[j]];
            for (uint64_t bit = bstart + (fbytes << 3u); bit < bend; ++bit)
                primes += (uint64_t)((rbits[bit >> 3u] >> (bit & 7u)) & 1u);
            rr[b + 1u] = rr[b] + (uint32_t)primes;
        }
        e->rbits_rank = rr;
        e->rbits_rank_blocks = rb_nblocks;
        total_bytes += (rb_nblocks + 1u) * sizeof(uint32_t);
    }

    e->prime_count = prime_count;
    e->total_bytes = total_bytes;
    e->last_use = ++g_hot_cache.use_counter;
    e->occupied = true;
    g_hot_cache.bytes_used += total_bytes;
    g_hot_cache.count += 1u;

    /* Evict duplicate wheel cache entry via hash lookup (not linear scan) */
    if (g_wheel_cache.hash_cap > 0u && g_wheel_cache.count > 0u) {
        size_t wsi;
        HASHMAP_PROBE(g_wheel_cache.hash_map, g_wheel_cache.hash_cap,
            g_wheel_cache.entries, stream_id, segment_no, wsi);
        if (wsi != SIZE_MAX) {
            WheelCacheEntry* wce = &g_wheel_cache.entries[wsi];
            g_wheel_cache.bytes_used -= wce->bytes;
            g_wheel_cache.count -= 1u;
            free(wce->data);
            memset(wce, 0, sizeof(*wce));
            wheel_cache_rebuild_hash();
        }
    }

    /* Incremental hash insert; full rebuild only when array grew */
    if (grew) {
        hot_cache_rebuild_hash();
    } else {
        size_t needed = g_hot_cache.count * 2u;
        if (needed > g_hot_cache.hash_cap) {
            hot_cache_rebuild_hash();
        } else {
            hashmap_put(g_hot_cache.hash_map, g_hot_cache.hash_cap,
                stream_id, segment_no, slot);
        }
    }
    return true;
}
