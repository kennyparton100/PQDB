/**
 * cpss_database.c - Multi-shard database: open, close, shard routing, db queries, preload, status
 * Part of the CPSS Viewer amalgamation.
 */
static uint64_t cpss_db_count_range(CPSSDatabase* db, uint64_t lo, uint64_t hi);
static CompositeClass cpss_classify(CPSSStream* s, uint64_t n);

/**
 * Count primes in [lo, hi] within a single segment.
 * Hot path: uses hot segment cache + rank index - no zlib, no full prefix scan.
 * Cold fallback: decompress + presieve + bit-walk via process_loaded_segment_range.
 *
 * This is the key primitive for fast boundary-segment counting.
 * Does NOT count wheel primes - caller is responsible for those.
 */
static uint64_t count_segment_partial_hot(CPSSStream* s, size_t seg_idx,
    uint64_t lo, uint64_t hi) {
    const SegmentIndexEntry* entry = &s->index_entries[seg_idx];

    /* Clamp [lo, hi] to segment range */
    uint64_t seg_lo = segment_start_n(entry);
    uint64_t seg_hi = segment_end_n(entry);
    if (lo < seg_lo) lo = seg_lo;
    if (hi > seg_hi) hi = seg_hi;
    if (hi < lo) return 0u;

    /* Map lo/hi to local candidate indices within the segment */
    uint64_t keep_start = 0u, keep_end = 0u;
    if (!next_candidate_index_ge(lo, &keep_start)) return 0u;
    if (!prev_candidate_index_le(hi, &keep_end)) return 0u;
    if (keep_start < entry->start_index) keep_start = entry->start_index;
    if (keep_end >= entry->next_index) keep_end = entry->next_index - 1u;
    if (keep_end < keep_start) return 0u;

    uint64_t local_start = keep_start - entry->start_index;
    uint64_t local_end = keep_end - entry->start_index;

    /* === Hot path: use hot segment cache === */
    HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
    if (hot && local_end < hot->total_candidates) {
        /* Use rank index to find survivor positions at boundaries */
        uint64_t spos_start = survivor_pos_ranked(hot->presieve, hot->rank_prefix, local_start);
        uint64_t spos_end_plus1 = survivor_pos_ranked(hot->presieve, hot->rank_prefix, local_end);
        if (get_bit(hot->presieve, local_end)) spos_end_plus1 += 1u;

        /* Use rbits_rank for fast prime counting in result_bits[spos_start..spos_end_plus1) */
        if (hot->rbits_rank && spos_end_plus1 > spos_start) {
            /* Block-accelerated count: full blocks via rank prefix, edge blocks via scan */
            uint64_t first_bit = spos_start;
            uint64_t last_bit_excl = spos_end_plus1;
            size_t first_block = (size_t)(first_bit / RANK_BLOCK_SIZE);
            size_t last_block = (size_t)((last_bit_excl > 0u ? last_bit_excl - 1u : 0u) / RANK_BLOCK_SIZE);

            uint64_t count = 0u;
            if (first_block == last_block) {
                /* Same block: scan directly */
                for (uint64_t b = first_bit; b < last_bit_excl; ++b)
                    count += (uint64_t)get_bit(hot->result_bits, b);
            }
            else {
                /* Edge of first block */
                uint64_t fb_end = (uint64_t)(first_block + 1u) * RANK_BLOCK_SIZE;
                for (uint64_t b = first_bit; b < fb_end; ++b)
                    count += (uint64_t)get_bit(hot->result_bits, b);
                /* Full middle blocks via rank prefix */
                if (last_block > first_block + 1u) {
                    count += hot->rbits_rank[last_block] - hot->rbits_rank[first_block + 1u];
                }
                /* Edge of last block */
                uint64_t lb_start = (uint64_t)last_block * RANK_BLOCK_SIZE;
                for (uint64_t b = lb_start; b < last_bit_excl; ++b)
                    count += (uint64_t)get_bit(hot->result_bits, b);
            }
            return count;
        }

        /* Fallback if rbits_rank not available: byte-aligned popcount */
        {
            uint64_t count = 0u;
            for (uint64_t b = spos_start; b < spos_end_plus1; ++b)
                count += (uint64_t)get_bit(hot->result_bits, b);
            return count;
        }
    }

    /* === Cold fallback: decompress + presieve === */
    uint64_t count = 0u;
    SegmentRecord rec;
    if (!cpss_load_segment(s, seg_idx, &rec)) {
        fprintf(stderr, "warning: segment %zu unavailable (file unmapped, result incomplete)\n",
            (size_t)entry->segment_no);
        return 0u;
    }
    bool borrowed = false;
    ByteBuf presieve = cpss_get_presieve_hot(s->stream_id, entry, &borrowed);
    process_loaded_segment_range(&rec, &presieve, lo, hi, 0, NULL, &count);
    if (!borrowed) free(presieve.data);
    segment_record_free(&rec);
    return count;
}

/**
 * Count primes in [lo, hi] using cache-aware presieve lookup.
 * Uses the per-segment prime index for O(1) on fully-contained middle segments.
 * Boundary segments use count_segment_partial_hot() for hot-cache acceleration.
 */
static uint64_t cpss_count_range_hot(CPSSStream* s, uint64_t lo, uint64_t hi) {
    uint64_t count = 0u;
    if (hi < lo) return 0u;

    /* Count wheel primes in range */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        if (lo <= p && p <= hi) ++count;
    }

    cpss_build_segment_index(s);
    cpss_build_prime_index(s);

    if (s->index_len == 0u) return count;

    /* Find first and last relevant segments via binary search */
    size_t first_seg = 0u;
    {
        size_t a = 0u, b = s->index_len;
        while (a < b) {
            size_t mid = a + (b - a) / 2u;
            if (segment_end_n(&s->index_entries[mid]) < lo) a = mid + 1u;
            else b = mid;
        }
        first_seg = a;
    }
    if (first_seg >= s->index_len) return count;
    if (segment_start_n(&s->index_entries[first_seg]) > hi) return count;

    size_t last_seg = first_seg;
    {
        size_t a = first_seg, b = s->index_len;
        while (a < b) {
            size_t mid = a + (b - a) / 2u;
            if (segment_start_n(&s->index_entries[mid]) <= hi) a = mid + 1u;
            else b = mid;
        }
        last_seg = (a > 0u) ? a - 1u : 0u;
    }

    if (first_seg == last_seg) {
        count += count_segment_partial_hot(s, first_seg, lo, hi);
        return count;
    }

    /* First boundary segment */
    count += count_segment_partial_hot(s, first_seg, lo, hi);

    /* O(1) addition for fully-contained middle segments */
    if (last_seg > first_seg + 1u) {
        /* Check for unavailable segments in the middle range */
        bool has_gap = false;
        for (size_t mi = first_seg + 1u; mi < last_seg; ++mi) {
            if (s->seg_prime_counts[mi] == UINT64_MAX) { has_gap = true; break; }
        }
        if (has_gap) {
            fprintf(stderr, "warning: count-range spans unavailable segments (result incomplete)\n");
        }
        count += s->seg_prime_prefix[last_seg] - s->seg_prime_prefix[first_seg + 1u];
    }

    /* Last boundary segment */
    count += count_segment_partial_hot(s, last_seg, lo, hi);

    return count;
}

/**
 * List primes in [lo, hi] into a caller-provided buffer.
 * Uses cache-aware presieve lookup.
 * Returns the number of primes written.  Stops when buf_cap is reached.
 */
static size_t cpss_list_range_hot(
    CPSSStream* s, uint64_t lo, uint64_t hi,
    uint64_t* out_buf, size_t buf_cap
) {
    size_t written = 0u;
    if (hi < lo || buf_cap == 0u) return 0u;

    /* Wheel primes in range */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        if (lo <= p && p <= hi) {
            if (written < buf_cap) out_buf[written] = p;
            ++written;
            if (written >= buf_cap) return written;
        }
    }

    cpss_build_segment_index(s);

    /* Find first relevant segment via binary search */
    size_t start_seg = 0u;
    if (s->index_len > 0u && lo > segment_start_n(&s->index_entries[0])) {
        size_t slo = 0u, shi = s->index_len;
        while (slo < shi) {
            size_t mid = slo + (shi - slo) / 2u;
            if (segment_end_n(&s->index_entries[mid]) < lo) slo = mid + 1u;
            else shi = mid;
        }
        start_seg = slo;
    }

    for (size_t i = start_seg; i < s->index_len; ++i) {
        const SegmentIndexEntry* entry = &s->index_entries[i];
        uint64_t seg_lo = segment_start_n(entry);
        uint64_t seg_hi = segment_end_n(entry);
        if (seg_lo > hi) break;

        uint64_t eff_lo = lo > seg_lo ? lo : seg_lo;
        uint64_t eff_hi = hi < seg_hi ? hi : seg_hi;
        if (eff_lo > eff_hi) continue;

        uint64_t keep_start = 0u, keep_end = 0u;
        if (!next_candidate_index_ge(eff_lo, &keep_start)) continue;
        if (!prev_candidate_index_le(eff_hi, &keep_end)) continue;
        if (keep_start < entry->start_index) keep_start = entry->start_index;
        if (keep_end >= entry->next_index) keep_end = entry->next_index - 1u;
        if (keep_end < keep_start) continue;

        uint64_t ls = keep_start - entry->start_index;
        uint64_t le = keep_end - entry->start_index;

        /* Try hot segment cache first - avoids zlib inflate */
        HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
        if (hot && le < hot->total_candidates) {
            uint64_t spos = survivor_pos_ranked(hot->presieve, hot->rank_prefix, ls);
            for (uint64_t idx = ls; idx <= le; ++idx) {
                if (!get_bit(hot->presieve, idx)) continue;
                if (get_bit(hot->result_bits, spos)) {
                    if (written < buf_cap) {
                        out_buf[written] = index_to_candidate(entry->start_index + idx);
                    }
                    ++written;
                    if (written >= buf_cap) return written;
                }
                ++spos;
            }
            continue;
        }

        /* Cold fallback: decompress */
        SegmentRecord rec;
        if (!cpss_load_segment(s, i, &rec)) continue;
        bool presieve_borrowed = false;
        ByteBuf presieve = cpss_get_presieve_hot(s->stream_id, entry, &presieve_borrowed);

        uint64_t spos = count_set_bits_prefix(presieve.data, ls);
        for (uint64_t idx = ls; idx <= le; ++idx) {
            if (!get_bit(presieve.data, idx)) continue;
            if (get_bit(rec.result_bits, spos)) {
                if (written < buf_cap) {
                    out_buf[written] = index_to_candidate(rec.start_index + idx);
                }
                ++written;
                if (written >= buf_cap) {
                    if (!presieve_borrowed) free(presieve.data);
                    segment_record_free(&rec);
                    return written;
                }
            }
            ++spos;
        }

        if (!presieve_borrowed) free(presieve.data);
        segment_record_free(&rec);
    }

    return written;
}

/**
 * Preload the wheel cache for segments [start_seg, end_seg] in the stream.
 * Pass start_seg=0, end_seg=SIZE_MAX to preload all segments.
 * Decompresses each segment to build the presieve bitmap, then caches it.
 * Segments that don't fit in the remaining budget are skipped.
 * Returns the number of segments successfully cached.
 */
static size_t cpss_preload_cache(CPSSStream* s, size_t start_seg, size_t end_seg) {
    if (g_wheel_cache.budget == 0u) return 0u;

    cpss_build_segment_index(s);
    size_t cached = 0u;
    size_t last = end_seg < s->index_len ? end_seg : s->index_len - 1u;

    for (size_t i = start_seg; i <= last && i < s->index_len; ++i) {
        const SegmentIndexEntry* entry = &s->index_entries[i];

        /* Skip if already cached */
        if (wheel_cache_lookup(s->stream_id, entry->segment_no, NULL) != NULL) {
            ++cached;
            continue;
        }

        size_t presieve_bytes = (size_t)((entry->total_candidates + 7u) / 8u);
        if (presieve_bytes > g_wheel_cache.budget) continue; /* too large */

        ByteBuf presieve = build_presieve_state_for_index_range(entry->start_index, entry->total_candidates);
        /* wheel_cache_insert takes ownership of data */
        if (wheel_cache_insert(s->stream_id, entry->segment_no, entry->start_index,
            entry->total_candidates, presieve.data, presieve.len)) {
            ++cached;
        }
        /* Note: if insert failed (budget full), presieve.data was freed by insert */
    }

    return cached;
}

/* ══════════════════════════════════════════════════════════════════════
 * PRIME NUMBER QUERY DATABASE - Phase 2: Multi-shard database layer
 *
 * CPSSDatabase opens a directory of split-trillion shard files
 * (PrimeStateV4_<lo>_<hi>.bin) and routes queries to the correct shard.
 * Shards are sorted by numeric range for O(log N) shard lookup.
 * ══════════════════════════════════════════════════════════════════════ */

 /* Forward declaration - needed because cpss_db_open validation calls cpss_db_close on error */
static void cpss_db_close(CPSSDatabase* db);

/* Forward declarations — manifest functions are defined in cpss_manifest.c (after this file) */
static ManifestLoadResult manifest_load(const char* dir_path);
static void manifest_load_free(ManifestLoadResult* r);

/**
 * Open a multi-shard CPSS database from a directory.
 * Fast path: if .cpss-manifest.json exists and is valid, uses it to
 * skip directory enumeration. Falls back to FindFirstFile scan.
 * Returns 0 on success, -1 on error (prints message to stderr).
 */
static int cpss_db_open(CPSSDatabase* db, const char* shard_dir) {
    memset(db, 0, sizeof(*db));

    /* Fast path: try loading from manifest */
    {
        ManifestLoadResult mr = manifest_load(shard_dir);
        if (mr.loaded && mr.shard_count > 0u) {
            size_t path_count = mr.shard_count;
            db->streams = (CPSSStream*)xcalloc(path_count, sizeof(CPSSStream));
            db->shard_lo = (uint64_t*)xmalloc(path_count * sizeof(uint64_t));
            db->shard_hi = (uint64_t*)xmalloc(path_count * sizeof(uint64_t));
            db->shard_paths = (char**)xmalloc(path_count * sizeof(char*));
            db->shard_count = path_count;

            bool all_ok = true;
            for (size_t i = 0u; i < path_count; ++i) {
                char full[1024];
                snprintf(full, sizeof(full), "%s\\%s", shard_dir, mr.shards[i].path);

                /* Check file exists */
                if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) {
                    fprintf(stderr, "warning: manifest references missing shard: %s\n", full);
                    all_ok = false;
                    break;
                }

                size_t len = strlen(full) + 1u;
                db->shard_paths[i] = (char*)xmalloc(len);
                memcpy(db->shard_paths[i], full, len);
                db->streams[i].file_handle = INVALID_HANDLE_VALUE;
                cpss_open(&db->streams[i], full);
                cpss_build_segment_index(&db->streams[i]);

                if (db->streams[i].index_len == 0u) {
                    db->shard_lo[i] = 0u;
                    db->shard_hi[i] = 0u;
                }
                else {
                    db->streams[i].base_index = db->streams[i].index_entries[0].start_index;
                    db->shard_lo[i] = segment_start_n(&db->streams[i].index_entries[0]);
                    db->shard_hi[i] = segment_end_n(&db->streams[i].index_entries[db->streams[i].index_len - 1u]);
                }
            }

            if (all_ok) {
                /* Shards are already in order from manifest — compute totals */
                db->total_segments = 0u;
                db->db_lo = db->shard_lo[0];
                db->db_hi = db->shard_hi[path_count - 1u];
                for (size_t i = 0u; i < path_count; ++i)
                    db->total_segments += db->streams[i].index_len;

                printf("  (loaded from manifest)\n");
                manifest_load_free(&mr);
                return 0;
            }

            /* Manifest referenced a missing file — fall through to directory scan */
            cpss_db_close(db);
            memset(db, 0, sizeof(*db));
        }
        manifest_load_free(&mr);
    }

    /* Enumerate PrimeStateV4_*.bin files using Win32 FindFirstFile */
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\PrimeStateV4_*.bin", shard_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        /* Also try single-file pattern for monolithic streams */
        snprintf(pattern, sizeof(pattern), "%s\\CompressedPrimeStateStreamV4_*.bin", shard_dir);
        hFind = FindFirstFileA(pattern, &fd);
        if (hFind == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "error: no CPSS shard files found in %s\n", shard_dir);
            return -1;
        }
    }

    /* Collect file paths */
    size_t path_count = 0u;
    size_t path_cap = 16u;
    char** paths = (char**)xmalloc(path_cap * sizeof(char*));

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s\\%s", shard_dir, fd.cFileName);

        if (path_count == path_cap) {
            path_cap *= 2u;
            paths = (char**)xrealloc(paths, path_cap * sizeof(char*));
        }
        size_t len = strlen(full) + 1u;
        paths[path_count] = (char*)xmalloc(len);
        memcpy(paths[path_count], full, len);
        ++path_count;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    if (path_count == 0u) {
        free(paths);
        fprintf(stderr, "error: no shard files matched in %s\n", shard_dir);
        return -1;
    }

    /* Open each shard and build index */
    db->streams = (CPSSStream*)xcalloc(path_count, sizeof(CPSSStream));
    db->shard_lo = (uint64_t*)xmalloc(path_count * sizeof(uint64_t));
    db->shard_hi = (uint64_t*)xmalloc(path_count * sizeof(uint64_t));
    db->shard_paths = paths;
    db->shard_count = path_count;

    for (size_t i = 0u; i < path_count; ++i) {
        db->streams[i].file_handle = INVALID_HANDLE_VALUE;
        cpss_open(&db->streams[i], paths[i]);
        cpss_build_segment_index(&db->streams[i]);

        if (db->streams[i].index_len == 0u) {
            db->shard_lo[i] = 0u;
            db->shard_hi[i] = 0u;
        }
        else {
            db->streams[i].base_index = db->streams[i].index_entries[0].start_index;
            db->shard_lo[i] = segment_start_n(&db->streams[i].index_entries[0]);
            db->shard_hi[i] = segment_end_n(&db->streams[i].index_entries[db->streams[i].index_len - 1u]);
        }
    }

    /* Sort shards by shard_lo using simple insertion sort (shard count is small) */
    for (size_t i = 1u; i < path_count; ++i) {
        size_t j = i;
        while (j > 0u && db->shard_lo[j] < db->shard_lo[j - 1u]) {
            /* Swap all parallel arrays */
            { CPSSStream tmp = db->streams[j]; db->streams[j] = db->streams[j - 1]; db->streams[j - 1] = tmp; }
            { uint64_t tmp = db->shard_lo[j]; db->shard_lo[j] = db->shard_lo[j - 1]; db->shard_lo[j - 1] = tmp; }
            { uint64_t tmp = db->shard_hi[j]; db->shard_hi[j] = db->shard_hi[j - 1]; db->shard_hi[j - 1] = tmp; }
            { char* tmp = db->shard_paths[j]; db->shard_paths[j] = db->shard_paths[j - 1]; db->shard_paths[j - 1] = tmp; }
            --j;
        }
    }

    /* Validate: reject overlapping or duplicate shards, warn on gaps */
    for (size_t i = 1u; i < path_count; ++i) {
        if (db->shard_lo[i] == db->shard_lo[i - 1u] &&
            db->shard_hi[i] == db->shard_hi[i - 1u]) {
            fprintf(stderr, "error: duplicate shard ranges:\n  %s\n  %s\n"
                "  both cover %" PRIu64 "..%" PRIu64 "\n",
                db->shard_paths[i - 1u], db->shard_paths[i],
                db->shard_lo[i], db->shard_hi[i]);
            cpss_db_close(db);
            return -1;
        }
        if (db->shard_lo[i] <= db->shard_hi[i - 1u]) {
            fprintf(stderr, "error: overlapping shards:\n  %s covers %" PRIu64 "..%" PRIu64 "\n"
                "  %s covers %" PRIu64 "..%" PRIu64 "\n"
                "  overlap at %" PRIu64 "..%" PRIu64 "\n",
                db->shard_paths[i - 1u], db->shard_lo[i - 1u], db->shard_hi[i - 1u],
                db->shard_paths[i], db->shard_lo[i], db->shard_hi[i],
                db->shard_lo[i], db->shard_hi[i - 1u]);
            cpss_db_close(db);
            return -1;
        }
        if (db->shard_lo[i] > db->shard_hi[i - 1u] + 1u) {
            fprintf(stderr, "warning: gap between shards:\n  %s ends at %" PRIu64 "\n"
                "  %s starts at %" PRIu64 "\n",
                db->shard_paths[i - 1u], db->shard_hi[i - 1u],
                db->shard_paths[i], db->shard_lo[i]);
        }
    }

    /* Compute global range and total segments */
    db->total_segments = 0u;
    db->db_lo = db->shard_lo[0];
    db->db_hi = db->shard_hi[path_count - 1u];
    for (size_t i = 0u; i < path_count; ++i) {
        db->total_segments += db->streams[i].index_len;
    }

    return 0;
}

/** Close all shards and free the database. */
static void cpss_db_close(CPSSDatabase* db) {
    for (size_t i = 0u; i < db->shard_count; ++i) {
        cpss_close(&db->streams[i]);
        free(db->shard_paths[i]);
    }
    free(db->streams);
    free(db->shard_lo);
    free(db->shard_hi);
    free(db->shard_paths);
    memset(db, 0, sizeof(*db));
}

/** Binary search for the shard containing candidate n. Returns shard index or SIZE_MAX. */
static size_t cpss_db_find_shard(const CPSSDatabase* db, uint64_t n) {
    if (db->shard_count == 0u) return SIZE_MAX;
    size_t lo = 0u, hi = db->shard_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (db->shard_hi[mid] < n) lo = mid + 1u;
        else hi = mid;
    }
    if (lo >= db->shard_count) return SIZE_MAX;
    if (n < db->shard_lo[lo] || n > db->shard_hi[lo]) return SIZE_MAX;
    return lo;
}

/** Query whether n is prime via the multi-shard database. Returns 1/0/-1. */
static int cpss_db_is_prime(CPSSDatabase* db, uint64_t n) {
    if (n < 2u) return 0;
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (n == WHEEL_PRIMES[i]) return 1;
    }
    size_t si = cpss_db_find_shard(db, n);
    if (si == SIZE_MAX) return -1;
    return cpss_is_prime(&db->streams[si], n);
}

/** First shard whose hi bound can cover n, or db->shard_count if none. */
static size_t cpss_db_first_shard_covering_or_after(const CPSSDatabase* db, uint64_t n) {
    size_t lo = 0u, hi = db->shard_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (db->shard_hi[mid] < n) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

/** Last shard whose lo bound is <= n, or SIZE_MAX if none. */
static size_t cpss_db_last_shard_covering_or_before(const CPSSDatabase* db, uint64_t n) {
    size_t lo = 0u, hi = db->shard_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (db->shard_lo[mid] <= n) lo = mid + 1u;
        else hi = mid;
    }
    return lo > 0u ? lo - 1u : SIZE_MAX;
}

/** Return true if candidate residues are missing between covered_hi and next_lo. */
static bool cpss_db_has_candidate_gap_after(uint64_t covered_hi, uint64_t next_lo) {
    uint64_t next_idx = 0u;
    if (covered_hi == UINT64_MAX) return false;
    if (!next_candidate_index_ge(covered_hi + 1u, &next_idx)) return false;
    return next_lo > index_to_candidate(next_idx);
}

/**
 * Highest integer covered contiguously from the wheel-prime prefix onward.
 * If require_mapped is true, stop before the first shard whose file is unmapped.
 */
static uint64_t cpss_db_contiguous_hi(const CPSSDatabase* db, bool require_mapped) {
    uint64_t hi = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u];
    if (!db || db->shard_count == 0u) return 0u;

    for (size_t i = 0u; i < db->shard_count; ++i) {
        if (db->shard_hi[i] <= hi) continue;
        if (cpss_db_has_candidate_gap_after(hi, db->shard_lo[i])) break;
        if (require_mapped && db->streams[i].map == NULL) break;
        hi = db->shard_hi[i];
    }

    return hi;
}

/** Highest candidate found in any hot segment within the contiguous shard prefix. */
static uint64_t cpss_db_hot_hi(const CPSSDatabase* db) {
    uint64_t contig_hi = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u];
    uint64_t hot_hi = 0u;
    if (!db || db->shard_count == 0u) return 0u;

    for (size_t i = 0u; i < db->shard_count; ++i) {
        if (db->shard_hi[i] <= contig_hi) continue;
        if (cpss_db_has_candidate_gap_after(contig_hi, db->shard_lo[i])) break;

        CPSSStream* s = &db->streams[i];
        for (size_t seg = s->index_len; seg > 0u; --seg) {
            const SegmentIndexEntry* entry = &s->index_entries[seg - 1u];
            if (hot_cache_lookup(s->stream_id, entry->segment_no)) {
                if (entry->end_n > hot_hi) hot_hi = entry->end_n;
                break;
            }
        }

        contig_hi = db->shard_hi[i];
    }

    return hot_hi;
}

/**
 * Find the smallest prime > n across the multi-shard DB.
 * Returns 0 on failure and writes an honest status via out_status.
 */
static uint64_t cpss_db_next_prime_s(CPSSDatabase* db, uint64_t n, CPSSQueryStatus* out_status) {
    if (!db || db->shard_count == 0u) {
        if (out_status) *out_status = CPSS_UNAVAILABLE;
        return 0u;
    }
    if (n == UINT64_MAX) {
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return 0u;
    }

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (WHEEL_PRIMES[i] > n) {
            if (out_status) *out_status = CPSS_OK;
            return WHEEL_PRIMES[i];
        }
    }

    if (n >= db->db_hi) {
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return 0u;
    }

    {
        uint64_t start = n + 1u;
        size_t si = cpss_db_first_shard_covering_or_after(db, start);
        if (si >= db->shard_count) {
            if (out_status) *out_status = CPSS_OUT_OF_RANGE;
            return 0u;
        }

        if (start < db->shard_lo[si]) {
            if (cpss_db_has_candidate_gap_after(n, db->shard_lo[si])) {
                if (out_status) *out_status = (si == 0u) ? CPSS_OUT_OF_RANGE : CPSS_INCOMPLETE;
                return 0u;
            }
        }

        for (; si < db->shard_count; ++si) {
            uint64_t query_n = n;
            if (db->shard_lo[si] > 0u && query_n + 1u < db->shard_lo[si]) {
                query_n = db->shard_lo[si] - 1u;
            }

            CPSSQueryStatus qs = CPSS_OK;
            uint64_t p = cpss_next_prime_s(&db->streams[si], query_n, &qs);
            if (qs == CPSS_OK) {
                if (out_status) *out_status = CPSS_OK;
                return p;
            }
            if (qs == CPSS_UNAVAILABLE) {
                if (out_status) *out_status = CPSS_UNAVAILABLE;
                return 0u;
            }

            if (si + 1u < db->shard_count &&
                cpss_db_has_candidate_gap_after(db->shard_hi[si], db->shard_lo[si + 1u])) {
                if (out_status) *out_status = CPSS_INCOMPLETE;
                return 0u;
            }

            n = db->shard_hi[si];
        }
    }

    if (out_status) *out_status = CPSS_OUT_OF_RANGE;
    return 0u;
}

/**
 * Find the largest prime < n across the multi-shard DB.
 * Returns 0 on failure and writes an honest status via out_status.
 */
static uint64_t cpss_db_prev_prime_s(CPSSDatabase* db, uint64_t n, CPSSQueryStatus* out_status) {
    if (!db || db->shard_count == 0u) {
        if (out_status) *out_status = CPSS_UNAVAILABLE;
        return 0u;
    }
    if (n <= 2u) {
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return 0u;
    }

    if (n <= WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u) {
        for (int i = (int)ARRAY_LEN(WHEEL_PRIMES) - 1; i >= 0; --i) {
            if (WHEEL_PRIMES[i] < n) {
                if (out_status) *out_status = CPSS_OK;
                return WHEEL_PRIMES[i];
            }
        }
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return 0u;
    }

    {
        uint64_t target = n - 1u;
        if (target > db->db_hi) {
            if (out_status) *out_status = CPSS_OUT_OF_RANGE;
            return 0u;
        }
        if (target < db->db_lo) {
            if (out_status) *out_status = CPSS_OUT_OF_RANGE;
            return 0u;
        }

        size_t si = cpss_db_last_shard_covering_or_before(db, target);
        if (si == SIZE_MAX) {
            if (out_status) *out_status = CPSS_OUT_OF_RANGE;
            return 0u;
        }

        if (target > db->shard_hi[si]) {
            if (si + 1u < db->shard_count &&
                cpss_db_has_candidate_gap_after(db->shard_hi[si], db->shard_lo[si + 1u])) {
                if (out_status) *out_status = CPSS_INCOMPLETE;
                return 0u;
            }
            if (out_status) *out_status = CPSS_OUT_OF_RANGE;
            return 0u;
        }

        for (;;) {
            uint64_t query_n = n;
            if (db->shard_hi[si] < query_n - 1u) {
                query_n = db->shard_hi[si] + 1u;
            }

            CPSSQueryStatus qs = CPSS_OK;
            uint64_t p = cpss_prev_prime_s(&db->streams[si], query_n, &qs);
            if (qs == CPSS_OK) {
                if (out_status) *out_status = CPSS_OK;
                return p;
            }
            if (qs == CPSS_UNAVAILABLE) {
                if (out_status) *out_status = CPSS_UNAVAILABLE;
                return 0u;
            }
            if (si == 0u) break;
            if (cpss_db_has_candidate_gap_after(db->shard_hi[si - 1u], db->shard_lo[si])) {
                if (out_status) *out_status = CPSS_INCOMPLETE;
                return 0u;
            }
            n = db->shard_lo[si];
            --si;
        }
    }

    if (out_status) *out_status = CPSS_OUT_OF_RANGE;
    return 0u;
}

/** DB-aware composite classification wrapper. */
static CompositeClass cpss_db_classify(CPSSDatabase* db, uint64_t n) {
    (void)db;
    return cpss_classify(NULL, n);
}

/** DB-aware support coverage summary across the contiguous shard prefix. */
static SupportCoverage cpss_db_support_coverage(CPSSDatabase* db, uint64_t n) {
    SupportCoverage cov;
    memset(&cov, 0, sizeof(cov));
    cov.n = n;
    cov.sqrt_n = (uint64_t)isqrt_u64(n);
    if (cov.sqrt_n * cov.sqrt_n < n) ++cov.sqrt_n;

    if (!db || db->shard_count == 0u) {
        return cov;
    }

    cov.db_hi = db->db_hi;
    {
        uint64_t contig_hi = cpss_db_contiguous_hi(db, false);
        uint64_t db_eff = contig_hi < cov.sqrt_n ? contig_hi : cov.sqrt_n;
        cov.db_complete = (contig_hi >= cov.sqrt_n);
        cov.db_coverage = cov.sqrt_n > 0u ? (double)db_eff / (double)cov.sqrt_n : 1.0;
        cov.prime_count = db_eff >= 2u ? cpss_db_count_range(db, 2u, db_eff) : 0u;
    }

    {
        uint64_t mapped_hi = cpss_db_contiguous_hi(db, true);
        cov.rt_file_mapped = (cov.sqrt_n <= WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u]) ||
            (mapped_hi > WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u]);
        cov.rt_hot_hi = cpss_db_hot_hi(db);
        cov.rt_hot_complete = (cov.rt_hot_hi >= cov.sqrt_n);
        cov.rt_hot_coverage = cov.sqrt_n > 0u
            ? (double)((cov.rt_hot_hi < cov.sqrt_n) ? cov.rt_hot_hi : cov.sqrt_n) / (double)cov.sqrt_n
            : 0.0;
    }

    return cov;
}

/**
 * Count primes in [lo, hi] across all relevant shards.
 * Uses per-shard prime index prefix sums for O(1) middle segments,
 * and count_segment_partial_hot() for boundary segments.
 */
static uint64_t cpss_db_count_range(CPSSDatabase* db, uint64_t lo, uint64_t hi) {
    if (hi < lo || db->shard_count == 0u) return 0u;

    uint64_t count = 0u;

    /* Count wheel primes (only once, not per-shard) */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        if (lo <= p && p <= hi) ++count;
    }

    /* Find first relevant shard */
    size_t start = 0u;
    {
        size_t slo = 0u, shi = db->shard_count;
        while (slo < shi) {
            size_t mid = slo + (shi - slo) / 2u;
            if (db->shard_hi[mid] < lo) slo = mid + 1u;
            else shi = mid;
        }
        start = slo;
    }

    for (size_t si = start; si < db->shard_count; ++si) {
        if (db->shard_lo[si] > hi) break;

        uint64_t slo = lo > db->shard_lo[si] ? lo : db->shard_lo[si];
        uint64_t shi = hi < db->shard_hi[si] ? hi : db->shard_hi[si];

        CPSSStream* s = &db->streams[si];
        cpss_build_segment_index(s);
        cpss_build_prime_index(s);
        if (s->index_len == 0u) continue;

        /* Find first and last segments within this shard for [slo, shi] */
        size_t first_seg = 0u;
        {
            size_t a = 0u, b = s->index_len;
            while (a < b) {
                size_t mid = a + (b - a) / 2u;
                if (segment_end_n(&s->index_entries[mid]) < slo) a = mid + 1u;
                else b = mid;
            }
            first_seg = a;
        }
        if (first_seg >= s->index_len || segment_start_n(&s->index_entries[first_seg]) > shi)
            continue;

        size_t last_seg = first_seg;
        {
            size_t a = first_seg, b = s->index_len;
            while (a < b) {
                size_t mid = a + (b - a) / 2u;
                if (segment_start_n(&s->index_entries[mid]) <= shi) a = mid + 1u;
                else b = mid;
            }
            last_seg = (a > 0u) ? a - 1u : 0u;
        }

        if (first_seg == last_seg) {
            count += count_segment_partial_hot(s, first_seg, slo, shi);
        }
        else {
            count += count_segment_partial_hot(s, first_seg, slo, shi);
            if (last_seg > first_seg + 1u) {
                count += s->seg_prime_prefix[last_seg] - s->seg_prime_prefix[first_seg + 1u];
            }
            count += count_segment_partial_hot(s, last_seg, slo, shi);
        }
    }

    return count;
}

/** List primes in [lo, hi] across shards into a caller buffer. Returns count written. */
static size_t cpss_db_list_range(
    CPSSDatabase* db, uint64_t lo, uint64_t hi,
    uint64_t* out_buf, size_t buf_cap
) {
    size_t written = 0u;
    if (hi < lo || db->shard_count == 0u || buf_cap == 0u) return 0u;

    /* Wheel primes */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        if (lo <= p && p <= hi) {
            if (written < buf_cap) out_buf[written] = p;
            ++written;
            if (written >= buf_cap) return written;
        }
    }

    /* Find first relevant shard */
    size_t start = 0u;
    {
        size_t slo = 0u, shi = db->shard_count;
        while (slo < shi) {
            size_t mid = slo + (shi - slo) / 2u;
            if (db->shard_hi[mid] < lo) slo = mid + 1u;
            else shi = mid;
        }
        start = slo;
    }

    for (size_t si = start; si < db->shard_count&& written < buf_cap; ++si) {
        if (db->shard_lo[si] > hi) break;

        uint64_t slo = lo > db->shard_lo[si] ? lo : db->shard_lo[si];
        uint64_t shi = hi < db->shard_hi[si] ? hi : db->shard_hi[si];

        /* Delegate to single-stream list but skip wheel primes (already emitted) */
        CPSSStream* s = &db->streams[si];
        cpss_build_segment_index(s);

        size_t seg_start = 0u;
        if (s->index_len > 0u && slo > segment_start_n(&s->index_entries[0])) {
            size_t a = 0u, b = s->index_len;
            while (a < b) {
                size_t mid = a + (b - a) / 2u;
                if (segment_end_n(&s->index_entries[mid]) < slo) a = mid + 1u;
                else b = mid;
            }
            seg_start = a;
        }

        for (size_t i = seg_start; i < s->index_len&& written < buf_cap; ++i) {
            const SegmentIndexEntry* entry = &s->index_entries[i];
            if (segment_start_n(entry) > shi) break;

            SegmentRecord rec;
            if (!cpss_load_segment(s, i, &rec)) continue;
            bool borrowed = false;
            ByteBuf presieve = cpss_get_presieve_hot(s->stream_id, entry, &borrowed);

            uint64_t seg_lo_n = record_start_n(&rec);
            uint64_t seg_hi_n = record_end_n(&rec);
            uint64_t eff_lo = slo > seg_lo_n ? slo : seg_lo_n;
            uint64_t eff_hi = shi < seg_hi_n ? shi : seg_hi_n;

            if (eff_lo <= eff_hi) {
                uint64_t ks = 0u, ke = 0u;
                if (next_candidate_index_ge(eff_lo, &ks) &&
                    prev_candidate_index_le(eff_hi, &ke)) {
                    if (ks < rec.start_index) ks = rec.start_index;
                    if (ke >= rec.next_index) ke = rec.next_index - 1u;
                    if (ks <= ke) {
                        uint64_t ls = ks - rec.start_index;
                        uint64_t le = ke - rec.start_index;
                        uint64_t spos = count_set_bits_prefix(presieve.data, ls);
                        for (uint64_t idx = ls; idx <= le && written < buf_cap; ++idx) {
                            if (!get_bit(presieve.data, idx)) continue;
                            if (get_bit(rec.result_bits, spos)) {
                                out_buf[written++] = index_to_candidate(rec.start_index + idx);
                            }
                            ++spos;
                        }
                    }
                }
            }

            if (!borrowed) free(presieve.data);
            segment_record_free(&rec);
        }
    }

    return written;
}

/** Report whether [lo, hi] is fully queryable from the loaded multi-shard DB. */
static CPSSQueryStatus cpss_db_interval_status(
    const CPSSDatabase* db, uint64_t lo, uint64_t hi, bool require_mapped
) {
    const uint64_t wheel_hi = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u];
    if (!db || db->shard_count == 0u) return CPSS_UNAVAILABLE;
    if (hi < lo) return CPSS_OUT_OF_RANGE;
    if (hi <= wheel_hi) return CPSS_OK;

    {
        uint64_t query_lo = lo <= wheel_hi ? wheel_hi + 1u : lo;
        size_t si = cpss_db_first_shard_covering_or_after(db, query_lo);
        if (si >= db->shard_count) return CPSS_OUT_OF_RANGE;

        if (query_lo < db->shard_lo[si]) {
            uint64_t prev_hi = (si > 0u) ? db->shard_hi[si - 1u] : wheel_hi;
            if (cpss_db_has_candidate_gap_after(prev_hi, db->shard_lo[si])) {
                return (si == 0u) ? CPSS_OUT_OF_RANGE : CPSS_INCOMPLETE;
            }
        }

        for (; si < db->shard_count; ++si) {
            if (require_mapped && db->streams[si].map == NULL) return CPSS_UNAVAILABLE;
            if (db->shard_hi[si] >= hi) return CPSS_OK;
            if (si + 1u >= db->shard_count) return CPSS_OUT_OF_RANGE;
            if (cpss_db_has_candidate_gap_after(db->shard_hi[si], db->shard_lo[si + 1u])) {
                return CPSS_INCOMPLETE;
            }
        }
    }

    return CPSS_OUT_OF_RANGE;
}

/** DB-aware nth-prime query. Returns 0 on failure and writes an honest status. */
static uint64_t cpss_db_nth_prime_s(CPSSDatabase* db, uint64_t k, CPSSQueryStatus* out_status) {
    const uint64_t wheel_count = (uint64_t)ARRAY_LEN(WHEEL_PRIMES);
    const uint64_t wheel_hi = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u];

    if (!db || db->shard_count == 0u) {
        if (out_status) *out_status = CPSS_UNAVAILABLE;
        return 0u;
    }
    if (k == 0u) {
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return 0u;
    }
    if (k <= wheel_count) {
        if (out_status) *out_status = CPSS_OK;
        return WHEEL_PRIMES[k - 1u];
    }

    {
        uint64_t remaining = k - wheel_count;
        uint64_t covered_hi = wheel_hi;

        for (size_t si = 0u; si < db->shard_count; ++si) {
            CPSSStream* s = &db->streams[si];
            uint64_t shard_prime_count = 0u;

            if (db->shard_hi[si] <= covered_hi) continue;
            if (cpss_db_has_candidate_gap_after(covered_hi, db->shard_lo[si])) {
                if (out_status) *out_status = CPSS_INCOMPLETE;
                return 0u;
            }

            cpss_build_segment_index(s);
            cpss_build_prime_index(s);
            if (s->index_len == 0u || !s->seg_prime_prefix) {
                if (out_status) *out_status = CPSS_UNAVAILABLE;
                return 0u;
            }

            shard_prime_count = s->seg_prime_prefix[s->index_len];
            if (remaining <= shard_prime_count) {
                uint64_t p = cpss_nth_prime(s, remaining + wheel_count);
                if (p > 0u) {
                    if (out_status) *out_status = CPSS_OK;
                    return p;
                }
                if (out_status) *out_status = (s->map == NULL) ? CPSS_UNAVAILABLE : CPSS_OUT_OF_RANGE;
                return 0u;
            }

            remaining -= shard_prime_count;
            covered_hi = db->shard_hi[si];
        }
    }

    if (out_status) *out_status = CPSS_OUT_OF_RANGE;
    return 0u;
}

/** DB-aware segment-info wrapper that routes to the shard containing n. */
static SegmentInfo cpss_db_segment_info(CPSSDatabase* db, uint64_t n) {
    SegmentInfo info;
    memset(&info, 0, sizeof(info));
    info.n = n;

    if (!db || db->shard_count == 0u) return info;

    {
        size_t si = cpss_db_find_shard(db, n);
        if (si == SIZE_MAX) return info;
        return cpss_segment_info(&db->streams[si], n);
    }
}

/** DB-aware range statistics for a fully covered, mapped interval. */
static RangeStats cpss_db_range_stats(CPSSDatabase* db, uint64_t lo, uint64_t hi, CPSSQueryStatus* out_status) {
    RangeStats st;
    memset(&st, 0, sizeof(st));
    st.lo = lo;
    st.hi = hi;
    st.range_width = (hi >= lo) ? hi - lo + 1u : 0u;

    if (!db || db->shard_count == 0u) {
        if (out_status) *out_status = CPSS_UNAVAILABLE;
        return st;
    }
    if (hi < lo) {
        if (out_status) *out_status = CPSS_OUT_OF_RANGE;
        return st;
    }

    {
        CPSSQueryStatus qs = cpss_db_interval_status(db, lo, hi, true);
        if (out_status) *out_status = qs;
        if (qs != CPSS_OK) return st;
    }

    st.prime_count = cpss_db_count_range(db, lo, hi);

    if (st.prime_count > 0u) {
        int lo_prime = cpss_db_is_prime(db, lo);
        if (lo_prime == 1) st.first_prime = lo;
        else {
            CPSSQueryStatus qs = CPSS_OK;
            uint64_t fp = cpss_db_next_prime_s(db, lo, &qs);
            if (qs == CPSS_OK && fp <= hi) st.first_prime = fp;
        }

        int hi_prime = cpss_db_is_prime(db, hi);
        if (hi_prime == 1) st.last_prime = hi;
        else {
            CPSSQueryStatus qs = CPSS_OK;
            uint64_t lp = cpss_db_prev_prime_s(db, hi, &qs);
            if (qs == CPSS_OK && lp >= lo) st.last_prime = lp;
        }
    }

    st.density = st.range_width > 0u ? (double)st.prime_count / (double)st.range_width : 0.0;
    {
        double mid = (double)lo / 2.0 + (double)hi / 2.0;
        st.expected_density = mid > 2.0 ? 1.0 / log(mid) : 1.0;
    }

    if (st.prime_count >= 2u) {
        size_t gap_cap = (size_t)st.prime_count;
        if (gap_cap > 10000u) gap_cap = 10000u;
        {
            uint64_t* primes = (uint64_t*)xmalloc(gap_cap * sizeof(uint64_t));
            size_t listed = cpss_db_list_range(db, lo, hi, primes, gap_cap);
            if (listed >= 2u) {
                st.min_gap = UINT64_MAX;
                st.max_gap = 0u;
                for (size_t i = 1u; i < listed; ++i) {
                    uint64_t gap = primes[i] - primes[i - 1u];
                    if (gap < st.min_gap) st.min_gap = gap;
                    if (gap > st.max_gap) st.max_gap = gap;
                    st.avg_gap += (double)gap;
                }
                st.avg_gap /= (double)(listed - 1u);
            }
            free(primes);
        }
    }

    if (out_status) *out_status = CPSS_OK;
    return st;
}

/**
 * Preload wheel cache for shards in the database.
 * start_seg/end_seg are global segment indices across all shards (0-based).
 * Pass start_seg=0, end_seg=SIZE_MAX to preload everything.
 */
static size_t cpss_db_preload(CPSSDatabase* db, size_t start_seg, size_t end_seg) {
    size_t total = 0u;
    size_t global_offset = 0u;
    for (size_t i = 0u; i < db->shard_count; ++i) {
        size_t shard_len = db->streams[i].index_len;
        size_t shard_end = global_offset + shard_len - 1u;

        /* Skip shards entirely before the range */
        if (shard_end < start_seg) {
            global_offset += shard_len;
            continue;
        }
        /* Stop if shard starts after the range */
        if (global_offset > end_seg) break;

        /* Map global range to local shard range */
        size_t local_start = (start_seg > global_offset) ? start_seg - global_offset : 0u;
        size_t local_end = (end_seg < shard_end) ? end_seg - global_offset : shard_len - 1u;

        total += cpss_preload_cache(&db->streams[i], local_start, local_end);
        global_offset += shard_len;
    }
    return total;
}

/** Fill a status struct for the database. */
static void cpss_db_status(const CPSSDatabase* db, CPSSDBStatus* out) {
    memset(out, 0, sizeof(*out));
    out->shard_count = db->shard_count;
    out->total_segments = db->total_segments;
    out->db_lo = db->db_lo;
    out->db_hi = db->db_hi;
    out->cache_budget = g_wheel_cache.budget;
    out->cache_used = wheel_cache_bytes_used();
    out->cache_entries = wheel_cache_entry_count();
}
