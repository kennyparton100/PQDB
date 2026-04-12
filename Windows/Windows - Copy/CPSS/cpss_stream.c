/**
 * cpss_stream.c - CPSSStream: open, close, segment index, load, prime index, is_prime, count, list, preload
 * Part of the CPSS Viewer amalgamation.
 */

/** Convert a compressed wheel index to the actual candidate number. */
static inline uint64_t index_to_candidate(uint64_t index) {
    uint64_t block = index / g_residue_count;
    uint32_t pos = (uint32_t)(index % g_residue_count);
    return block * (uint64_t)g_wheel_modulus + (uint64_t)g_wheel_residues[pos];
}

/** Convert a candidate number to its wheel index. Returns false if not a residue. */
static bool candidate_to_index(uint64_t n, uint64_t* out_index) {
    if (n < 1u) {
        return false;
    }
    uint64_t block = n / g_wheel_modulus;
    uint32_t residue = (uint32_t)(n % g_wheel_modulus);
    int32_t pos = g_residue_to_pos[residue];
    if (pos < 0) {
        return false;
    }
    *out_index = block * (uint64_t)g_residue_count + (uint64_t)pos;
    return true;
}

/** Create a bitset with the first bit_count bits set to 1. */
static ByteBuf bitset_all_ones(uint64_t bit_count) {
    ByteBuf buf = { 0 };
    size_t byte_count = (size_t)((bit_count + 7u) / 8u);
    buf.data = (uint8_t*)xmalloc(byte_count ? byte_count : 1u);
    buf.len = byte_count;
    memset(buf.data, 0xFF, byte_count);
    if (byte_count > 0u) {
        uint32_t extra_bits = (uint32_t)(byte_count * 8u - bit_count);
        if (extra_bits) {
            uint32_t used = 8u - extra_bits;
            buf.data[byte_count - 1u] = (uint8_t)((1u << used) - 1u);
        }
    }
    return buf;
}

/** Return 1 if the bit at bit_index is set, else 0. */
#if defined(__clang__) || defined(__GNUC__)
static __attribute__((always_inline)) inline int get_bit(const uint8_t* data, uint64_t bit_index) {
#elif defined(_MSC_VER)
static __forceinline int get_bit(const uint8_t* data, uint64_t bit_index) {
#else
static inline int get_bit(const uint8_t* data, uint64_t bit_index) {
#endif
    return (data[bit_index >> 3u] >> (bit_index & 7u)) & 1u;
}

/** Set the bit at bit_index to 1. */
#if defined(__clang__) || defined(__GNUC__)
static __attribute__((always_inline)) inline void set_bit(uint8_t* data, uint64_t bit_index) {
#elif defined(_MSC_VER)
static __forceinline void set_bit(uint8_t* data, uint64_t bit_index) {
#else
static inline void set_bit(uint8_t* data, uint64_t bit_index) {
#endif
    data[bit_index >> 3u] |= (uint8_t)(1u << (bit_index & 7u));
}

/** Clear the bit at bit_index to 0. */
#if defined(__clang__) || defined(__GNUC__)
static __attribute__((always_inline)) inline void clear_bit(uint8_t* data, uint64_t bit_index) {
#elif defined(_MSC_VER)
static __forceinline void clear_bit(uint8_t* data, uint64_t bit_index) {
#else
static inline void clear_bit(uint8_t* data, uint64_t bit_index) {
#endif
    data[bit_index >> 3u] &= (uint8_t)~(1u << (bit_index & 7u));
}

/** Count set bits in a byte array using the popcount lookup table. */
static uint64_t bit_count_bytes(const uint8_t* data, size_t byte_len) {
    uint64_t total = 0u;
    for (size_t i = 0u; i < byte_len; ++i) {
        total += g_popcount8[data[i]];
    }
    return total;
}

/** Count set bits in the first bit_count bits of data. */
static uint64_t count_set_bits_prefix(const uint8_t* data, uint64_t bit_count) {
    uint64_t full_bytes = bit_count >> 3u;
    uint64_t total = 0u;
    for (uint64_t i = 0u; i < full_bytes; ++i) {
        total += g_popcount8[data[i]];
    }
    for (uint64_t i = full_bytes << 3u; i < bit_count; ++i) {
        total += (uint64_t)get_bit(data, i);
    }
    return total;
}

/** Build a presieve bitmap — delegates to cpss_presieve.c (separate TU).
 *  The actual implementation is compiled in its own translation unit so the
 *  optimizer works on a small, focused unit without LTO interference from
 *  the 21K-line amalgamation.
 */
static ByteBuf build_presieve_state_for_index_range_wrap(uint64_t start_index, uint32_t total_candidates) {
    PresieveByteBuf pb = build_presieve_state_for_index_range(start_index, total_candidates);
    ByteBuf bb;
    bb.data = pb.data;
    bb.len = pb.len;
    return bb;
}
/* Macro so all existing call sites work without renaming */
#define build_presieve_state_for_index_range build_presieve_state_for_index_range_wrap



/** Decompress zlib data into a newly allocated buffer. Returns false on failure. */
static bool zlib_decompress_alloc(const uint8_t* in, size_t in_len, uint8_t** out, size_t* out_len) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) {
        return false;
    }

    size_t cap = in_len * 4u;
    if (cap < 1024u) {
        cap = 1024u;
    }
    uint8_t* buf = (uint8_t*)xmalloc(cap);

    zs.next_in = (Bytef*)in;
    zs.avail_in = (uInt)in_len;

    int ret = Z_OK;
    do {
        if (zs.total_out == cap) {
            cap *= 2u;
            buf = (uint8_t*)xrealloc(buf, cap);
        }
        zs.next_out = (Bytef*)(buf + zs.total_out);
        zs.avail_out = (uInt)(cap - zs.total_out);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            free(buf);
            return false;
        }
    } while (ret != Z_STREAM_END);

    *out = buf;
    *out_len = zs.total_out;
    inflateEnd(&zs);
    return true;
}

/** Decompress zlib data into a pre-sized buffer when output size is known. Returns false on failure. */
static bool zlib_decompress_exact(const uint8_t* in, size_t in_len, size_t expected_out, uint8_t** out, size_t* out_len) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) return false;

    uint8_t* buf = (uint8_t*)xmalloc(expected_out);
    zs.next_in = (Bytef*)in;
    zs.avail_in = (uInt)in_len;
    zs.next_out = (Bytef*)buf;
    zs.avail_out = (uInt)expected_out;

    int ret = inflate(&zs, Z_FINISH);
    if (ret != Z_STREAM_END) {
        inflateEnd(&zs);
        free(buf);
        return false;
    }

    *out = buf;
    *out_len = zs.total_out;
    inflateEnd(&zs);
    return true;
}

/** Compress data with zlib level 6 into a newly allocated buffer. Returns false on failure. */
static bool zlib_compress_alloc(const uint8_t* in, size_t in_len, uint8_t** out, size_t* out_len) {
    uLongf bound = compressBound((uLong)in_len);
    uint8_t* buf = (uint8_t*)xmalloc((size_t)bound);
    int rc = compress2(buf, &bound, in, (uLong)in_len, 6);
    if (rc != Z_OK) {
        free(buf);
        return false;
    }
    *out = buf;
    *out_len = (size_t)bound;
    return true;
}

/** Open a CPSS file via Win32 CreateFileMapping and map it for reading. */
/** Monotonic stream ID counter for unique cache keys. */
static uint32_t g_next_stream_id = 1u;

static void cpss_open(CPSSStream* s, const char* path) {
    memset(s, 0, sizeof(*s));
    s->file_handle = INVALID_HANDLE_VALUE;
    s->path = _strdup(path);
    if (!s->path) die("out of memory");
    s->stream_id = g_next_stream_id++;

    s->file_handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s->file_handle == INVALID_HANDLE_VALUE) die_win32("failed to open state file");

    LARGE_INTEGER size_li;
    if (!GetFileSizeEx(s->file_handle, &size_li)) die_win32("failed to get file size");

    if ((uint64_t)size_li.QuadPart > (uint64_t)SIZE_MAX) die("file too large for this build; use x64");
    s->file_size = (size_t)size_li.QuadPart;
    if (s->file_size == 0u) return;

    s->mapping_handle = CreateFileMappingA(s->file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!s->mapping_handle) die_win32("CreateFileMapping failed");
    s->map = (uint8_t*)MapViewOfFile(s->mapping_handle, FILE_MAP_READ, 0, 0, 0);
    if (!s->map) die_win32("MapViewOfFile failed");

    /* Detect CPSR (raw/decompressed) format via magic header */
    s->is_raw = false;
    s->data_offset = 0u;
    if (s->file_size >= CPSR_HEADER_SIZE) {
        uint32_t magic = read_u32_le(s->map);
        if (magic == CPSR_MAGIC) {
            s->is_raw = true;
            s->data_offset = CPSR_HEADER_SIZE;
        }
    }
}

/**
 * Release the file mapping but keep the segment index and metadata.
 * After this, hot-cached segments are still queryable but cold-path
 * decompress will fail (s->map == NULL).  Frees ~filesize of RAM.
 */
static void cpss_unmap(CPSSStream* s) {
    if (s->map) { (void)UnmapViewOfFile(s->map); s->map = NULL; }
    if (s->mapping_handle) { (void)CloseHandle(s->mapping_handle); s->mapping_handle = NULL; }
    if (s->file_handle != INVALID_HANDLE_VALUE) { (void)CloseHandle(s->file_handle); s->file_handle = INVALID_HANDLE_VALUE; }
}

/** Unmap the file, close handles, and free the segment index. */
static void cpss_close(CPSSStream* s) {
    /* Purge cache entries keyed to this stream before destroying it */
    if (s->stream_id > 0u) {
        wheel_cache_purge_stream(s->stream_id);
        hot_cache_purge_stream(s->stream_id);
    }
    if (s->map) (void)UnmapViewOfFile(s->map);
    if (s->mapping_handle) (void)CloseHandle(s->mapping_handle);
    if (s->file_handle != INVALID_HANDLE_VALUE) (void)CloseHandle(s->file_handle);
    free(s->index_entries);
    free(s->seg_prime_counts);
    free(s->seg_prime_prefix);
    free(s->path);
    memset(s, 0, sizeof(*s));
    s->file_handle = INVALID_HANDLE_VALUE;
}

/** Append a segment index entry to the stream's index array. */
static void cpss_index_push(CPSSStream* s, const SegmentIndexEntry* entry) {
    if (s->index_len == s->index_cap) {
        size_t new_cap = s->index_cap ? s->index_cap * 2u : 64u;
        s->index_entries = (SegmentIndexEntry*)xrealloc(s->index_entries, new_cap * sizeof(*s->index_entries));
        s->index_cap = new_cap;
    }
    s->index_entries[s->index_len++] = *entry;
}

/** Read and decompress one segment at the given file offset. Returns false if file is unmapped. */
static bool read_segment_raw(const CPSSStream* s, uint64_t file_offset, uint32_t* compressed_len, uint8_t** raw, size_t* raw_len) {
    if (!s->map) return false;
    if (file_offset + 4u > s->file_size) {
        die("truncated size header in state file");
    }

    *compressed_len = read_u32_le(s->map + file_offset);
    uint64_t comp_start = file_offset + 4u;
    uint64_t comp_end = comp_start + *compressed_len;
    if (comp_end > s->file_size) {
        die("truncated compressed payload in state file");
    }

    if (!zlib_decompress_alloc(s->map + comp_start, *compressed_len, raw, raw_len)) {
        die("zlib decompression failed");
    }
    return true;
}

/**
 * Read only the 12-byte segment header via partial zlib inflate.
 * Avoids allocating and decompressing the full segment payload.
 * Returns the compressed_len (from the 4-byte size prefix) and fills
 * total_candidates, survivor_count, result_bits_len, and raw_size.
 */
static void read_segment_header_only(
    const CPSSStream* s, uint64_t file_offset,
    uint32_t* out_compressed_len,
    uint32_t* out_total_candidates,
    uint32_t* out_survivor_count,
    uint32_t* out_result_bits_len,
    size_t* out_raw_size
) {
    if (file_offset + 4u > s->file_size) {
        die("truncated size header in state file");
    }

    uint32_t compressed_len = read_u32_le(s->map + file_offset);
    uint64_t comp_start = file_offset + 4u;
    uint64_t comp_end = comp_start + compressed_len;
    if (comp_end > s->file_size) {
        die("truncated compressed payload in state file");
    }

    /* Partial inflate: only need 12 bytes of output.
     * The raw segment format is [12-byte header][result_bits payload],
     * so raw_size = 12 + result_bits_len.  No need to drain the full stream. */
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) {
        die("inflateInit failed in header-only read");
    }

    uint8_t hdr[12];
    zs.next_in = (Bytef*)(s->map + comp_start);
    zs.avail_in = (uInt)compressed_len;
    zs.next_out = (Bytef*)hdr;
    zs.avail_out = 12u;

    while (zs.total_out < 12u) {
        int ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            die("zlib inflate failed during header-only read");
        }
        if (ret == Z_STREAM_END && zs.total_out < 12u) {
            inflateEnd(&zs);
            die("raw payload too short (header-only)");
        }
    }
    inflateEnd(&zs);

    *out_compressed_len = compressed_len;
    *out_total_candidates = read_u32_le(hdr + 0u);
    *out_survivor_count = read_u32_le(hdr + 4u);
    *out_result_bits_len = read_u32_le(hdr + 8u);
    *out_raw_size = 12u + (size_t)(*out_result_bits_len);
}

/** Lazily scan the entire file and build the in-memory segment index. */
static void cpss_build_segment_index(CPSSStream* s) {
    if (s->index_entries) {
        return;
    }

    /* Fast path: try loading from .cpssi sidecar */
    if (s->path) {
        char sidecar_path[1024];
        cpssi_path_for(s->path, sidecar_path, sizeof(sidecar_path));

        uint64_t mtime = cpssi_get_mtime(s->path);
        uint64_t hash = s->map ? cpssi_content_hash(s->map, s->file_size) : 0u;

        CpssiLoadResult lr = cpssi_load(sidecar_path, (uint64_t)s->file_size, mtime, hash);
        if (lr.loaded) {
            /* Use sidecar entries directly */
            s->index_entries = lr.entries;
            s->index_len = lr.segment_count;
            s->index_cap = lr.segment_count;

            /* If sidecar has prime counts, install them too */
            if (lr.has_prime_counts && lr.prime_counts && lr.prime_prefix) {
                s->seg_prime_counts = lr.prime_counts;
                s->seg_prime_prefix = lr.prime_prefix;
                s->prime_index_built = true;
            }
            return;
        }
        /* Sidecar not usable — fall through to file scan */
    }

    uint64_t current_start = 0u;
    uint64_t segment_no = 0u;
    uint64_t file_offset = s->data_offset; /* skip CPSR header for raw files */

    while (file_offset < s->file_size) {
        uint32_t total_candidates = 0u;
        uint32_t survivor_count = 0u;
        uint32_t result_bits_len = 0u;
        size_t raw_len = 0u;
        uint32_t on_disk_len = 0u; /* compressed_len for V4, raw_len for CPSR */

        if (s->is_raw) {
            /* Raw format: [4-byte raw_len][12-byte header][result_bits...] */
            if (file_offset + 4u > s->file_size) die("truncated raw segment length");
            uint32_t seg_raw_len = read_u32_le(s->map + file_offset);
            if (file_offset + 4u + seg_raw_len > s->file_size) die("truncated raw segment data");
            if (seg_raw_len < 12u) die("raw segment too short");

            /* Read header directly from mapped memory — zero inflate */
            const uint8_t* hdr = s->map + file_offset + 4u;
            total_candidates = read_u32_le(hdr + 0u);
            survivor_count = read_u32_le(hdr + 4u);
            result_bits_len = read_u32_le(hdr + 8u);
            raw_len = (size_t)seg_raw_len;
            on_disk_len = seg_raw_len;
        }
        else {
            /* Compressed V4 format: partial inflate for header */
            uint32_t compressed_len = 0u;
            read_segment_header_only(s, file_offset,
                &compressed_len, &total_candidates, &survivor_count,
                &result_bits_len, &raw_len);
            on_disk_len = compressed_len;
        }

        if ((size_t)result_bits_len != raw_len - 12u) {
            die("segment result_bits_len mismatch");
        }
        if (result_bits_len != (survivor_count + 7u) / 8u) {
            die("impossible survivor bit payload length");
        }

        SegmentIndexEntry entry;
        entry.segment_no = segment_no;
        entry.start_index = current_start;
        entry.next_index = current_start + total_candidates;
        entry.total_candidates = total_candidates;
        entry.survivor_count = survivor_count;
        entry.result_bits_len = result_bits_len;
        entry.raw_size = raw_len;
        entry.compressed_size = s->is_raw ? 0u : on_disk_len;
        entry.file_offset = file_offset;
        entry.start_n = index_to_candidate(current_start);
        entry.end_n = index_to_candidate(current_start + total_candidates - 1u);
        cpss_index_push(s, &entry);

        current_start = entry.next_index;
        file_offset += 4u + on_disk_len;
        ++segment_no;
    }
}

/** Return the index entry for a segment, or NULL if out of range. */
static const SegmentIndexEntry* cpss_get_segment_entry(CPSSStream* s, size_t seg_no) {
    cpss_build_segment_index(s);
    if (seg_no >= s->index_len) {
        return NULL;
    }
    return &s->index_entries[seg_no];
}

/** First candidate number in a segment index entry (cached). */
static inline uint64_t segment_start_n(const SegmentIndexEntry* e) {
    return e->start_n;
}

/** Last candidate number in a segment index entry (cached). */
static inline uint64_t segment_end_n(const SegmentIndexEntry* e) {
    return e->end_n;
}

/** First candidate number in a loaded segment record. */
static uint64_t record_start_n(const SegmentRecord* r) {
    return index_to_candidate(r->start_index);
}

/** Last candidate number in a loaded segment record. */
static uint64_t record_end_n(const SegmentRecord* r) {
    return index_to_candidate(r->next_index - 1u);
}

/** Free a loaded segment record's heap data. */
static void segment_record_free(SegmentRecord* rec) {
    free(rec->raw_data);
    memset(rec, 0, sizeof(*rec));
}

/** Load a segment record.  For raw files, borrows directly from mmap (zero alloc).
 *  For compressed files, decompresses into a heap buffer.
 *  Returns false if the file is unmapped (segment not available). */
static bool cpss_load_segment(CPSSStream* s, size_t seg_no, SegmentRecord* rec) {
    const SegmentIndexEntry* entry = cpss_get_segment_entry(s, seg_no);
    if (!entry) {
        die("segment number out of range");
    }
    memset(rec, 0, sizeof(*rec));

    if (!s->map) return false; /* file unmapped */

    uint64_t foff = entry->file_offset;

    if (s->is_raw) {
        /* Raw format: [4-byte raw_len][12-byte header][result_bits...]
         * Borrow directly from mmap — zero heap allocation. */
        if (foff + 4u > s->file_size) die("truncated raw segment length");
        uint32_t seg_raw_len = read_u32_le(s->map + foff);
        if (foff + 4u + seg_raw_len > s->file_size) die("truncated raw segment data");

        const uint8_t* raw = s->map + foff + 4u;

        rec->segment_no = entry->segment_no;
        rec->start_index = entry->start_index;
        rec->next_index = entry->next_index;
        rec->total_candidates = read_u32_le(raw + 0u);
        rec->survivor_count = read_u32_le(raw + 4u);
        rec->result_bits_len = read_u32_le(raw + 8u);
        rec->raw_data = NULL;  /* borrowed from mmap, not heap — do NOT free */
        rec->raw_size = (size_t)seg_raw_len;
        rec->result_bits = (uint8_t*)(raw + 12u); /* points into mmap */
        rec->compressed_size = 0u;
        rec->file_offset = foff;
        return true;
    }

    /* Compressed V4 format */
    if (foff + 4u > s->file_size) die("truncated size header in state file");
    uint32_t compressed_len = read_u32_le(s->map + foff);
    uint64_t comp_start = foff + 4u;
    if (comp_start + compressed_len > s->file_size) die("truncated compressed payload");

    uint8_t* raw = NULL;
    size_t raw_len = 0u;
    if (!zlib_decompress_exact(s->map + comp_start, compressed_len, entry->raw_size, &raw, &raw_len)) {
        if (!zlib_decompress_alloc(s->map + comp_start, compressed_len, &raw, &raw_len)) {
            die("zlib decompression failed");
        }
    }

    if (raw_len < 12u) {
        free(raw);
        die("raw payload too short in loaded segment");
    }

    uint32_t total_candidates = read_u32_le(raw + 0u);
    uint32_t survivor_count = read_u32_le(raw + 4u);
    uint32_t result_bits_len = read_u32_le(raw + 8u);
    if ((size_t)result_bits_len != raw_len - 12u) {
        free(raw);
        die("loaded segment result_bits_len mismatch");
    }
    if (result_bits_len != (survivor_count + 7u) / 8u) {
        free(raw);
        die("loaded segment survivor payload impossible");
    }

    rec->segment_no = entry->segment_no;
    rec->start_index = entry->start_index;
    rec->next_index = entry->next_index;
    rec->total_candidates = total_candidates;
    rec->survivor_count = survivor_count;
    rec->result_bits_len = result_bits_len;
    rec->raw_data = raw;
    rec->raw_size = raw_len;
    rec->result_bits = raw + 12u;
    rec->compressed_size = compressed_len;
    rec->file_offset = foff;
    return true;
}

/**
 * Build per-segment prime count and cumulative prefix arrays.
 * seg_prime_counts[i] = number of primes in segment i (excludes wheel primes).
 * seg_prime_prefix[i] = sum of seg_prime_counts[0..i-1].
 * This enables O(1) count-range for segments fully contained in the query range.
 * Must be called after cpss_build_segment_index.
 */
static void cpss_build_prime_index(CPSSStream* s) {
    if (s->prime_index_built) return;
    cpss_build_segment_index(s);
    if (s->index_len == 0u) { s->prime_index_built = true; return; }

    s->seg_prime_counts = (uint64_t*)xmalloc(s->index_len * sizeof(uint64_t));
    s->seg_prime_prefix = (uint64_t*)xmalloc((s->index_len + 1u) * sizeof(uint64_t));
    s->seg_prime_prefix[0] = 0u;

    for (size_t i = 0u; i < s->index_len; ++i) {
        /* Try hot cache first (works even when file is unmapped) */
        const SegmentIndexEntry* entry = &s->index_entries[i];
        HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
        if (hot) {
            uint64_t pc = bit_count_bytes(hot->result_bits, hot->result_bits_len);
            s->seg_prime_counts[i] = pc;
            s->seg_prime_prefix[i + 1u] = s->seg_prime_prefix[i] + pc;
            continue;
        }

        /* Fall back to loading from file */
        SegmentRecord rec;
        if (!cpss_load_segment(s, i, &rec)) {
            /* Segment unavailable: use UINT64_MAX sentinel so consumers can detect */
            s->seg_prime_counts[i] = UINT64_MAX;
            s->seg_prime_prefix[i + 1u] = s->seg_prime_prefix[i];
            continue;
        }
        uint64_t pc = bit_count_bytes(rec.result_bits, rec.result_bits_len);
        s->seg_prime_counts[i] = pc;
        s->seg_prime_prefix[i + 1u] = s->seg_prime_prefix[i] + pc;
        segment_record_free(&rec);
    }

    s->prime_index_built = true;
}

/** Recompute presieve survivors and assert they match the segment header. */
static void deep_validate_segment(const SegmentRecord* rec) {
    ByteBuf presieve = build_presieve_state_for_index_range(rec->start_index, rec->total_candidates);
    uint64_t actual_survivors = bit_count_bytes(presieve.data, presieve.len);
    free(presieve.data);
    if (actual_survivors != rec->survivor_count) {
        die("deep validation failed: survivor_count mismatch");
    }
}

/** Validate the stream and return a summary of segments, primes, and sizes. */
static ValidateSummary cpss_validate(CPSSStream* s, bool deep, uint64_t max_deep_segments) {
    cpss_build_segment_index(s);

    ValidateSummary summary;
    memset(&summary, 0, sizeof(summary));

    for (size_t i = 0u; i < s->index_len; ++i) {
        const SegmentIndexEntry* e = &s->index_entries[i];
        summary.segments += 1u;
        summary.total_candidates += e->total_candidates;
        summary.total_survivors += e->survivor_count;
        summary.raw_total_bytes += e->raw_size;
        summary.compressed_total_bytes += e->compressed_size;
        summary.last_next_index = e->next_index;

        SegmentRecord rec;
        if (!cpss_load_segment(s, i, &rec)) continue;
        summary.total_primes += bit_count_bytes(rec.result_bits, rec.result_bits_len);
        if (deep && summary.segments <= max_deep_segments) {
            deep_validate_segment(&rec);
        }
        segment_record_free(&rec);
    }

    if (summary.last_next_index > 0u) {
        summary.last_candidate = index_to_candidate(summary.last_next_index - 1u);
    }
    if (summary.compressed_total_bytes > 0u) {
        summary.compression_ratio = (double)summary.raw_total_bytes / (double)summary.compressed_total_bytes;
    }

    return summary;
}

/** Return the wheel index of the smallest candidate >= n, or false if none. */
static bool next_candidate_index_ge(uint64_t n, uint64_t* out_idx) {
    while (true) {
        if (candidate_to_index(n, out_idx)) {
            return true;
        }
        if (n == UINT64_MAX) {
            return false;
        }
        ++n;
    }
}

/** Return the wheel index of the largest candidate <= n, or false if none. */
static bool prev_candidate_index_le(uint64_t n, uint64_t* out_idx) {
    while (n >= 1u) {
        if (candidate_to_index(n, out_idx)) {
            return true;
        }
        if (n == 1u) {
            break;
        }
        --n;
    }
    return false;
}

/** Write a prime to fp if fp is non-NULL. */
static void maybe_write_prime(FILE* fp, uint64_t p) {
    if (fp) {
        if (fprintf(fp, "%" PRIu64 "\n", p) < 0) {
            die_errno("failed writing prime output");
        }
    }
}

/** Decode primes from a loaded segment in [lo,hi], optionally writing to out_fp. */
static void process_loaded_segment_range(
    const SegmentRecord* rec,
    const ByteBuf* presieve_state,
    uint64_t lo,
    uint64_t hi,
    uint64_t limit,
    FILE* out_fp,
    uint64_t* count_io
) {
    uint64_t seg_lo = record_start_n(rec);
    uint64_t seg_hi = record_end_n(rec);
    if (lo < seg_lo) {
        lo = seg_lo;
    }
    if (hi > seg_hi) {
        hi = seg_hi;
    }
    if (hi < lo) {
        return;
    }

    uint64_t keep_start_index = 0u;
    uint64_t keep_end_index = 0u;
    if (!next_candidate_index_ge(lo, &keep_start_index)) {
        return;
    }
    if (!prev_candidate_index_le(hi, &keep_end_index)) {
        return;
    }
    if (keep_start_index < rec->start_index) {
        keep_start_index = rec->start_index;
    }
    if (keep_end_index >= rec->next_index) {
        keep_end_index = rec->next_index - 1u;
    }
    if (keep_end_index < keep_start_index) {
        return;
    }

    uint64_t local_start = keep_start_index - rec->start_index;
    uint64_t local_end = keep_end_index - rec->start_index;
    uint64_t survivor_pos = count_set_bits_prefix(presieve_state->data, local_start);

    for (uint64_t idx = local_start; idx <= local_end; ++idx) {
        if (!get_bit(presieve_state->data, idx)) {
            continue;
        }
        if (get_bit(rec->result_bits, survivor_pos)) {
            ++(*count_io);
            maybe_write_prime(out_fp, index_to_candidate(rec->start_index + idx));
            if (limit && *count_io >= limit) {
                return;
            }
        }
        ++survivor_pos;
    }
}

/** Scan the stream for primes in [lo,hi], counting and optionally writing them. */
static void cpss_scan_range(
    CPSSStream* s,
    uint64_t lo,
    uint64_t hi,
    uint64_t limit,
    FILE* out_fp,
    uint64_t* count_out
) {
    *count_out = 0u;
    if (hi < lo) {
        return;
    }

    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        uint64_t p = WHEEL_PRIMES[i];
        if (lo <= p && p <= hi) {
            ++(*count_out);
            maybe_write_prime(out_fp, p);
            if (limit && *count_out >= limit) {
                return;
            }
        }
    }

    cpss_build_segment_index(s);
    for (size_t i = 0u; i < s->index_len; ++i) {
        const SegmentIndexEntry* entry = &s->index_entries[i];
        uint64_t start_n = segment_start_n(entry);
        uint64_t end_n = segment_end_n(entry);
        if (end_n < lo) {
            continue;
        }
        if (start_n > hi) {
            break;
        }

        SegmentRecord rec;
        if (!cpss_load_segment(s, i, &rec)) continue;
        ByteBuf presieve = build_presieve_state_for_index_range(rec.start_index, rec.total_candidates);
        process_loaded_segment_range(&rec, &presieve, lo, hi, limit, out_fp, count_out);
        free(presieve.data);
        segment_record_free(&rec);

        if (limit && *count_out >= limit) {
            return;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * PRIME NUMBER QUERY DATABASE - Phase 1 core query functions
 *
 * These functions provide fast hot-path primality queries against a
 * CPSS stream.  They use binary search on the segment index and
 * integrate with the wheel/presieve cache for hot-mode performance.
 * ══════════════════════════════════════════════════════════════════════ */

 /**
  * Binary search for the segment containing candidate number n.
  * Returns the segment array index, or SIZE_MAX if n is outside the stream.
  * Requires the segment index to be built already.
  */
static size_t cpss_find_segment_for_n(CPSSStream* s, uint64_t n) {
    cpss_build_segment_index(s);
    if (s->index_len == 0u) return SIZE_MAX;

    /* Binary search: segments are sorted by start_index / start_n */
    size_t lo = 0u;
    size_t hi = s->index_len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        uint64_t mid_end = segment_end_n(&s->index_entries[mid]);
        if (mid_end < n) {
            lo = mid + 1u;
        }
        else {
            hi = mid;
        }
    }
    if (lo >= s->index_len) return SIZE_MAX;

    /* Verify n is within [start_n, end_n] of the found segment */
    uint64_t seg_start = segment_start_n(&s->index_entries[lo]);
    uint64_t seg_end = segment_end_n(&s->index_entries[lo]);
    if (n < seg_start || n > seg_end) return SIZE_MAX;

    return lo;
}

/**
 * Get a presieve bitmap for a segment, using the wheel cache if available.
 * If the cache holds the presieve, returns a borrowed pointer (do NOT free).
 * If not cached, builds the presieve, optionally inserts it into the cache,
 * and returns an owned pointer (caller must free via out->data).
 * Sets *is_borrowed = true if the returned data is a cache borrow.
 */
static ByteBuf cpss_get_presieve_hot(uint32_t stream_id, const SegmentIndexEntry* entry, bool* is_borrowed) {
    *is_borrowed = false;

    /* Check wheel cache first */
    if (g_wheel_cache.budget > 0) {
        size_t cached_len = 0u;
        const uint8_t* cached = wheel_cache_lookup(stream_id, entry->segment_no, &cached_len);
        if (cached) {
            ByteBuf result;
            result.data = (uint8_t*)cached;
            result.len = cached_len;
            *is_borrowed = true;
            return result;
        }
    }

    /* Build presieve */
    ByteBuf presieve = build_presieve_state_for_index_range(entry->start_index, entry->total_candidates);

    /* Try to cache it */
    if (g_wheel_cache.budget > 0 && presieve.len <= g_wheel_cache.budget) {
        uint8_t* copy = (uint8_t*)xmalloc(presieve.len);
        memcpy(copy, presieve.data, presieve.len);
        wheel_cache_insert(stream_id, entry->segment_no, entry->start_index,
            entry->total_candidates, copy, presieve.len);
    }

    return presieve;
}

/* ── Parallel preload infrastructure ──────────────────────────────── */

/** Per-segment work result produced by a worker thread. */
typedef struct {
    size_t seg_idx;
    uint32_t stream_id;
    uint64_t segment_no;
    uint64_t start_index;
    uint32_t total_candidates;
    uint32_t survivor_count;
    uint8_t* presieve;
    size_t presieve_len;
    uint8_t* result_bits;
    uint32_t result_bits_len;
    uint32_t* rank_prefix;
    size_t rank_blocks;
    bool ok;               /* false if load/build failed */
    double t_load;
    double t_presieve;
    double t_copy;
    double t_rank;
} PreloadWorkItem;

/** Shared state for the parallel preload thread pool. */
typedef struct {
    CPSSStream* stream;
    const SegmentIndexEntry* entries;
    size_t* work_queue;       /* array of segment indices to process */
    volatile LONG next_item;  /* atomic index into work_queue */
    size_t queue_len;
    PreloadWorkItem* results; /* one result per queue entry */
} PreloadPool;

/** Worker thread function: pick segments from the queue and build presieve+rank. */
static DWORD WINAPI preload_worker(LPVOID param) {
    PreloadPool* pool = (PreloadPool*)param;
    CPSSStream* s = pool->stream;

    while (1) {
        LONG idx = InterlockedIncrement(&pool->next_item) - 1;
        if ((size_t)idx >= pool->queue_len) break;

        size_t seg_idx = pool->work_queue[idx];
        PreloadWorkItem* item = &pool->results[idx];
        const SegmentIndexEntry* entry = &pool->entries[seg_idx];

        item->seg_idx = seg_idx;
        item->stream_id = s->stream_id;
        item->segment_no = entry->segment_no;
        item->start_index = entry->start_index;
        item->total_candidates = entry->total_candidates;
        item->ok = false;

        /* Load segment data (reads from mmap — thread-safe) */
        double ts0 = now_sec();
        SegmentRecord rec;
        if (!cpss_load_segment(s, seg_idx, &rec)) return 0;
        double ts1 = now_sec();
        item->t_load = ts1 - ts0;
        item->survivor_count = rec.survivor_count;

        /* Build presieve (pure computation — thread-safe) */
        ts0 = now_sec();
        ByteBuf presieve = build_presieve_state_for_index_range(entry->start_index, entry->total_candidates);
        ts1 = now_sec();
        item->t_presieve = ts1 - ts0;

        /* Copy result_bits */
        ts0 = now_sec();
        uint8_t* rb_copy = (uint8_t*)xmalloc(rec.result_bits_len);
        memcpy(rb_copy, rec.result_bits, rec.result_bits_len);
        ts1 = now_sec();
        item->t_copy = ts1 - ts0;

        /* Build rank index (pure computation — thread-safe) */
        ts0 = now_sec();
        uint32_t* rank = NULL;
        size_t rblocks = 0u;
        build_rank_index(presieve.data, rec.total_candidates, &rank, &rblocks);
        ts1 = now_sec();
        item->t_rank = ts1 - ts0;

        item->presieve = presieve.data;
        item->presieve_len = presieve.len;
        item->result_bits = rb_copy;
        item->result_bits_len = rec.result_bits_len;
        item->rank_prefix = rank;
        item->rank_blocks = rblocks;
        item->ok = true;

        segment_record_free(&rec);
    }
    return 0;
}

/**
 * Preload the hot segment cache for segments [start_seg, end_seg].
 * Uses g_app_threads workers for parallel presieve+rank construction.
 * Cache insertion is serialized on the main thread.
 * Pass start_seg=0, end_seg=SIZE_MAX to preload all.
 * Returns the number of segments successfully cached.
 */
static size_t cpss_preload_hot(CPSSStream* s, size_t start_seg, size_t end_seg) {
    if (g_hot_cache.budget == 0u) return 0u;

    cpss_build_segment_index(s);
    size_t cached = 0u;
    size_t last = end_seg < s->index_len ? end_seg : s->index_len - 1u;

    /* Build work queue: segments that need loading, in order */
    size_t max_items = (last >= start_seg) ? last - start_seg + 1u : 0u;
    if (max_items == 0u) return 0u;

    size_t* work_queue = (size_t*)xmalloc(max_items * sizeof(size_t));
    size_t queue_len = 0u;
    size_t already_cached = 0u;

    for (size_t i = start_seg; i <= last && i < s->index_len; ++i) {
        const SegmentIndexEntry* entry = &s->index_entries[i];

        if (hot_cache_lookup(s->stream_id, entry->segment_no) != NULL) {
            ++already_cached;
            continue;
        }

        size_t presieve_est = (size_t)((entry->total_candidates + 7u) / 8u);
        size_t rank_est = (((size_t)entry->total_candidates + RANK_BLOCK_SIZE - 1u) / RANK_BLOCK_SIZE + 1u) * sizeof(uint32_t);
        size_t entry_est = presieve_est + entry->result_bits_len + rank_est;

        if (entry_est > g_hot_cache.budget) continue;
        if (g_hot_cache.bytes_used + entry_est > g_hot_cache.budget) break;

        /* Reserve budget space so we don't over-commit */
        g_hot_cache.bytes_used += entry_est;
        work_queue[queue_len++] = i;
    }
    /* Unreserve — actual insertion will track real sizes */
    for (size_t qi = 0u; qi < queue_len; ++qi) {
        const SegmentIndexEntry* entry = &s->index_entries[work_queue[qi]];
        size_t presieve_est = (size_t)((entry->total_candidates + 7u) / 8u);
        size_t rank_est = (((size_t)entry->total_candidates + RANK_BLOCK_SIZE - 1u) / RANK_BLOCK_SIZE + 1u) * sizeof(uint32_t);
        g_hot_cache.bytes_used -= presieve_est + entry->result_bits_len + rank_est;
    }

    cached = already_cached;

    if (queue_len == 0u) {
        free(work_queue);
        printf("  preload: 0 segments to process (%zu already cached)\n", already_cached);
        return cached;
    }

    uint32_t n_threads = g_app_threads;
    if (n_threads > (uint32_t)queue_len) n_threads = (uint32_t)queue_len;

    PreloadWorkItem* results = (PreloadWorkItem*)xcalloc(queue_len, sizeof(PreloadWorkItem));

    double t_parallel_start = now_sec();

    if (n_threads <= 1u) {
        /* Single-threaded fallback: same logic, just inline */
        PreloadPool pool;
        pool.stream = s;
        pool.entries = s->index_entries;
        pool.work_queue = work_queue;
        pool.next_item = 0;
        pool.queue_len = queue_len;
        pool.results = results;
        preload_worker(&pool);
    }
    else {
        PreloadPool pool;
        pool.stream = s;
        pool.entries = s->index_entries;
        pool.work_queue = work_queue;
        pool.next_item = 0;
        pool.queue_len = queue_len;
        pool.results = results;

        HANDLE* threads = (HANDLE*)xmalloc(n_threads * sizeof(HANDLE));
        for (uint32_t t = 0u; t < n_threads; ++t) {
            threads[t] = CreateThread(NULL, 0, preload_worker, &pool, 0, NULL);
            if (!threads[t]) die_win32("CreateThread failed for preload worker");
        }
        WaitForMultipleObjects(n_threads, threads, TRUE, INFINITE);
        for (uint32_t t = 0u; t < n_threads; ++t) CloseHandle(threads[t]);
        free(threads);
    }

    double t_parallel_end = now_sec();

    /* Serial phase: insert results into hot cache */
    double t_insert = 0.0;
    double t_load_sum = 0.0, t_presieve_sum = 0.0, t_copy_sum = 0.0, t_rank_sum = 0.0;

    for (size_t qi = 0u; qi < queue_len; ++qi) {
        PreloadWorkItem* item = &results[qi];
        if (!item->ok) continue;

        t_load_sum += item->t_load;
        t_presieve_sum += item->t_presieve;
        t_copy_sum += item->t_copy;
        t_rank_sum += item->t_rank;

        double ts0 = now_sec();
        if (hot_cache_insert(item->stream_id, item->segment_no, item->start_index,
            item->total_candidates, item->survivor_count,
            item->presieve, item->presieve_len,
            item->result_bits, item->result_bits_len,
            item->rank_prefix, item->rank_blocks, 0u)) {
            ++cached;
        }
        double ts1 = now_sec();
        t_insert += ts1 - ts0;
    }

    printf("  preload breakdown: threads=%u parallel=%.2fs insert=%.2fs\n",
        n_threads, t_parallel_end - t_parallel_start, t_insert);
    printf("    worker totals:   load=%.2fs presieve=%.2fs copy=%.2fs rank=%.2fs\n",
        t_load_sum, t_presieve_sum, t_copy_sum, t_rank_sum);

    free(results);
    free(work_queue);
    return cached;
}

/** Preload hot cache across all shards in a database. */
static size_t cpss_db_preload_hot(CPSSDatabase* db, size_t start_seg, size_t end_seg) {
    size_t total = 0u;
    size_t global_offset = 0u;
    for (size_t i = 0u; i < db->shard_count; ++i) {
        size_t shard_len = db->streams[i].index_len;
        size_t shard_end = global_offset + shard_len - 1u;
        if (shard_end < start_seg) { global_offset += shard_len; continue; }
        if (global_offset > end_seg) break;
        size_t local_start = (start_seg > global_offset) ? start_seg - global_offset : 0u;
        size_t local_end = (end_seg < shard_end) ? end_seg - global_offset : shard_len - 1u;
        total += cpss_preload_hot(&db->streams[i], local_start, local_end);
        global_offset += shard_len;
    }
    return total;
}

/**
 * Query whether a single number n is prime, using the CPSS stream.
 * Returns:  1 = prime,  0 = not prime,  -1 = n is outside stream range.
 *
 * Hot path: if the segment is in the hot cache, uses rank index for
 * O(RANK_BLOCK_SIZE/8) survivor lookup - no zlib decompress needed.
 * Cold path: decompresses segment, builds presieve, optionally populates
 * the hot cache for next time.
 */
static int cpss_is_prime(CPSSStream* s, uint64_t n) {
    if (n < 2u) return 0;

    /* Check wheel primes directly */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (n == WHEEL_PRIMES[i]) return 1;
    }

    /* n must be a wheel residue to possibly be prime */
    uint64_t wheel_idx;
    if (!candidate_to_index(n, &wheel_idx)) {
        return 0; /* not coprime to wheel modulus → composite */
    }

    /* Find the segment containing n */
    size_t seg_idx = cpss_find_segment_for_n(s, n);
    if (seg_idx == SIZE_MAX) return -1; /* outside stream range */

    const SegmentIndexEntry* entry = &s->index_entries[seg_idx];
    uint64_t local_idx = wheel_idx - entry->start_index;

    /* === Hot path: check hot segment cache === */
    HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
    if (hot && local_idx < hot->total_candidates) {
        if (!get_bit(hot->presieve, local_idx)) return 0;
        uint64_t spos = survivor_pos_ranked(hot->presieve, hot->rank_prefix, local_idx);
        return get_bit(hot->result_bits, spos) ? 1 : 0;
    }

    /* === Cold path: decompress + presieve === */
    SegmentRecord rec;
    if (!cpss_load_segment(s, seg_idx, &rec)) return -2; /* file unmapped */

    bool presieve_borrowed = false;
    ByteBuf presieve = cpss_get_presieve_hot(s->stream_id, entry, &presieve_borrowed);

    int result = 0;
    if (local_idx < rec.total_candidates) {
        if (!get_bit(presieve.data, local_idx)) {
            result = 0;
        }
        else {
            uint64_t survivor_pos = count_set_bits_prefix(presieve.data, local_idx);
            result = get_bit(rec.result_bits, survivor_pos) ? 1 : 0;
        }
    }

    /* Try to populate the hot cache for future queries */
    if (g_hot_cache.budget > 0 && !hot) {
        size_t total_hot_bytes = presieve.len + rec.result_bits_len;
        if (total_hot_bytes <= g_hot_cache.budget) {
            uint8_t* ps_copy = (uint8_t*)xmalloc(presieve.len);
            memcpy(ps_copy, presieve.data, presieve.len);
            uint8_t* rb_copy = (uint8_t*)xmalloc(rec.result_bits_len);
            memcpy(rb_copy, rec.result_bits, rec.result_bits_len);
            uint32_t* rank = NULL;
            size_t rblocks = 0u;
            build_rank_index(ps_copy, rec.total_candidates, &rank, &rblocks);
            hot_cache_insert(s->stream_id, entry->segment_no, entry->start_index,
                rec.total_candidates, rec.survivor_count,
                ps_copy, presieve.len, rb_copy, rec.result_bits_len,
                rank, rblocks, 0u);
        }
    }

    if (!presieve_borrowed) free(presieve.data);
    segment_record_free(&rec);
    return result;
}
