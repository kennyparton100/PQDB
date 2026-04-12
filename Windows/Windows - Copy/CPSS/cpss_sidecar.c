/**
 * cpss_sidecar.c - Persistent sidecar index (.cpssi) for fast stream loading.
 * Part of the CPSS Viewer amalgamation.
 *
 * Binary format (little-endian, fixed-size records):
 *   Header: 96 bytes (magic, version, flags, record_size, segment_count,
 *           source identity, optional block offsets, reserved)
 *   Per-segment records: 80 bytes each (mirrors SegmentIndexEntry + reserved)
 *   Optional prime_counts block: segment_count * uint64
 *   Optional prime_prefix block: (segment_count + 1) * uint64
 *
 * Staleness detection: source file size + mtime + fast content hash.
 */

#define CPSSI_MAGIC       0x49535043u  /* "CPSI" little-endian */
#define CPSSI_VERSION     1u
#define CPSSI_HEADER_SIZE 96u
#define CPSSI_RECORD_SIZE 80u

/** On-disk sidecar header (96 bytes). */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t record_size;
    uint64_t segment_count;
    uint64_t source_file_size;
    uint64_t source_file_mtime;
    uint64_t source_content_hash;
    uint64_t prime_counts_offset;
    uint64_t prime_prefix_offset;
    uint8_t  reserved[32];
} CpssiHeader;

/** On-disk per-segment record (80 bytes). */
typedef struct {
    uint64_t segment_no;
    uint64_t start_index;
    uint64_t next_index;
    uint32_t total_candidates;
    uint32_t survivor_count;
    uint32_t result_bits_len;
    uint32_t compressed_size;
    uint64_t raw_size;
    uint64_t file_offset;
    uint64_t start_n;
    uint64_t end_n;
    uint64_t reserved;
} CpssiRecord;

/* ── Helpers ──────────────────────────────────────────────────────── */

/** Compute the sidecar path for a CPSS file: replace/append .cpssi */
static void cpssi_path_for(const char* cpss_path, char* out, size_t out_cap) {
    snprintf(out, out_cap, "%s.cpssi", cpss_path);
}

/** Get Windows FILETIME for a file as uint64. Returns 0 on failure. */
static uint64_t cpssi_get_mtime(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attr)) return 0u;
    return ((uint64_t)attr.ftLastWriteTime.dwHighDateTime << 32u) |
           (uint64_t)attr.ftLastWriteTime.dwLowDateTime;
}

/** Fast content hash: hash first 4KB + last 4KB of file. */
static uint64_t cpssi_content_hash(const uint8_t* map, size_t file_size) {
    uint64_t h = 0xcbf29ce484222325ull; /* FNV-1a offset basis */
    size_t head = file_size < 4096u ? file_size : 4096u;
    for (size_t i = 0u; i < head; ++i) {
        h ^= map[i];
        h *= 0x100000001b3ull;
    }
    if (file_size > 4096u) {
        size_t tail_start = file_size - 4096u;
        for (size_t i = tail_start; i < file_size; ++i) {
            h ^= map[i];
            h *= 0x100000001b3ull;
        }
    }
    return h;
}

/* ── Write ────────────────────────────────────────────────────────── */

/**
 * Build and write a .cpssi sidecar for a CPSSStream.
 * The stream must have its segment index built.
 * If include_primes is true and prime index is built, includes prime count blocks.
 * Returns 0 on success, -1 on error.
 */
static int cpssi_write(CPSSStream* s, const char* sidecar_path) {
    /* Caller must have built the segment index before calling this. */
    if (s->index_len == 0u) {
        fprintf(stderr, "error: no segments to index\n");
        return -1;
    }

    FILE* fp = fopen(sidecar_path, "wb");
    if (!fp) {
        fprintf(stderr, "error: cannot create sidecar %s: %s\n", sidecar_path, strerror(errno));
        return -1;
    }

    /* Build header */
    CpssiHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = CPSSI_MAGIC;
    hdr.version = CPSSI_VERSION;
    hdr.record_size = CPSSI_RECORD_SIZE;
    hdr.segment_count = (uint64_t)s->index_len;
    hdr.source_file_size = (uint64_t)s->file_size;
    hdr.source_file_mtime = s->path ? cpssi_get_mtime(s->path) : 0u;
    hdr.source_content_hash = s->map ? cpssi_content_hash(s->map, s->file_size) : 0u;

    /* Compute optional block offsets */
    uint64_t records_end = CPSSI_HEADER_SIZE + (uint64_t)s->index_len * CPSSI_RECORD_SIZE;

    bool have_primes = s->prime_index_built && s->seg_prime_counts != NULL;
    if (have_primes) {
        hdr.prime_counts_offset = records_end;
        hdr.prime_prefix_offset = records_end + (uint64_t)s->index_len * sizeof(uint64_t);
    }

    /* Write header */
    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1) goto write_err;

    /* Write per-segment records */
    for (size_t i = 0u; i < s->index_len; ++i) {
        const SegmentIndexEntry* e = &s->index_entries[i];
        CpssiRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.segment_no = e->segment_no;
        rec.start_index = e->start_index;
        rec.next_index = e->next_index;
        rec.total_candidates = e->total_candidates;
        rec.survivor_count = e->survivor_count;
        rec.result_bits_len = e->result_bits_len;
        rec.compressed_size = e->compressed_size;
        rec.raw_size = e->raw_size;
        rec.file_offset = e->file_offset;
        rec.start_n = e->start_n;
        rec.end_n = e->end_n;
        if (fwrite(&rec, sizeof(rec), 1, fp) != 1) goto write_err;
    }

    /* Write optional prime count blocks */
    if (have_primes) {
        if (fwrite(s->seg_prime_counts, sizeof(uint64_t), s->index_len, fp) != s->index_len)
            goto write_err;
        if (fwrite(s->seg_prime_prefix, sizeof(uint64_t), s->index_len + 1u, fp) != s->index_len + 1u)
            goto write_err;
    }

    fclose(fp);
    return 0;

write_err:
    fprintf(stderr, "error: failed writing sidecar %s: %s\n", sidecar_path, strerror(errno));
    fclose(fp);
    remove(sidecar_path);
    return -1;
}

/* ── Read / Validate ──────────────────────────────────────────────── */

/** Result of trying to load a sidecar. */
typedef struct {
    bool loaded;
    bool has_prime_counts;
    size_t segment_count;
    SegmentIndexEntry* entries;       /* heap-allocated, caller owns */
    uint64_t* prime_counts;           /* heap-allocated or NULL */
    uint64_t* prime_prefix;           /* heap-allocated or NULL */
    const char* reject_reason;        /* if !loaded, why */
} CpssiLoadResult;

/**
 * Try to load a sidecar index.
 * Validates magic, version, staleness.
 * On success: result.loaded=true, result.entries is a heap array.
 * On failure: result.loaded=false, result.reject_reason explains why.
 */
static CpssiLoadResult cpssi_load(const char* sidecar_path,
    uint64_t expected_file_size, uint64_t expected_mtime, uint64_t expected_hash) {

    CpssiLoadResult r;
    memset(&r, 0, sizeof(r));

    FILE* fp = fopen(sidecar_path, "rb");
    if (!fp) {
        r.reject_reason = "sidecar file not found";
        return r;
    }

    /* Read header */
    CpssiHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        r.reject_reason = "sidecar header too short";
        fclose(fp);
        return r;
    }

    if (hdr.magic != CPSSI_MAGIC) {
        r.reject_reason = "bad magic";
        fclose(fp);
        return r;
    }
    if (hdr.version != CPSSI_VERSION) {
        r.reject_reason = "unsupported version";
        fclose(fp);
        return r;
    }
    if (hdr.record_size != CPSSI_RECORD_SIZE) {
        r.reject_reason = "unexpected record size";
        fclose(fp);
        return r;
    }

    /* Staleness check */
    if (hdr.source_file_size != expected_file_size) {
        r.reject_reason = "source file size mismatch (stale)";
        fclose(fp);
        return r;
    }
    if (expected_mtime != 0u && hdr.source_file_mtime != expected_mtime) {
        r.reject_reason = "source file mtime mismatch (stale)";
        fclose(fp);
        return r;
    }
    if (expected_hash != 0u && hdr.source_content_hash != expected_hash) {
        r.reject_reason = "source content hash mismatch (stale)";
        fclose(fp);
        return r;
    }

    if (hdr.segment_count == 0u) {
        r.reject_reason = "sidecar has 0 segments";
        fclose(fp);
        return r;
    }

    /* Read segment records */
    size_t seg_count = (size_t)hdr.segment_count;
    SegmentIndexEntry* entries = (SegmentIndexEntry*)xmalloc(seg_count * sizeof(SegmentIndexEntry));

    for (size_t i = 0u; i < seg_count; ++i) {
        CpssiRecord rec;
        if (fread(&rec, sizeof(rec), 1, fp) != 1) {
            free(entries);
            r.reject_reason = "truncated segment records";
            fclose(fp);
            return r;
        }
        entries[i].segment_no = rec.segment_no;
        entries[i].start_index = rec.start_index;
        entries[i].next_index = rec.next_index;
        entries[i].total_candidates = rec.total_candidates;
        entries[i].survivor_count = rec.survivor_count;
        entries[i].result_bits_len = rec.result_bits_len;
        entries[i].compressed_size = rec.compressed_size;
        entries[i].raw_size = (size_t)rec.raw_size;
        entries[i].file_offset = rec.file_offset;
        entries[i].start_n = rec.start_n;
        entries[i].end_n = rec.end_n;
    }

    r.entries = entries;
    r.segment_count = seg_count;
    r.loaded = true;

    /* Read optional prime count blocks */
    if (hdr.prime_counts_offset > 0u && hdr.prime_prefix_offset > 0u) {
        uint64_t* counts = (uint64_t*)xmalloc(seg_count * sizeof(uint64_t));
        uint64_t* prefix = (uint64_t*)xmalloc((seg_count + 1u) * sizeof(uint64_t));

        fseek(fp, (long)hdr.prime_counts_offset, SEEK_SET);
        if (fread(counts, sizeof(uint64_t), seg_count, fp) == seg_count) {
            fseek(fp, (long)hdr.prime_prefix_offset, SEEK_SET);
            if (fread(prefix, sizeof(uint64_t), seg_count + 1u, fp) == seg_count + 1u) {
                r.prime_counts = counts;
                r.prime_prefix = prefix;
                r.has_prime_counts = true;
            }
            else {
                free(counts);
                free(prefix);
            }
        }
        else {
            free(counts);
            free(prefix);
        }
    }

    fclose(fp);
    return r;
}

/**
 * Verify a sidecar against a live file scan.
 * Returns 0 if matching, -1 if mismatched (prints differences).
 */
static int cpssi_verify(CPSSStream* s, const char* sidecar_path) {
    /* Caller must have built the segment index before calling this. */

    uint64_t mtime = s->path ? cpssi_get_mtime(s->path) : 0u;
    uint64_t hash = s->map ? cpssi_content_hash(s->map, s->file_size) : 0u;
    CpssiLoadResult lr = cpssi_load(sidecar_path, (uint64_t)s->file_size, mtime, hash);

    if (!lr.loaded) {
        printf("verify-index: cannot load sidecar: %s\n", lr.reject_reason);
        return -1;
    }

    int errors = 0;
    if (lr.segment_count != s->index_len) {
        printf("  MISMATCH: segment count: sidecar=%zu file=%zu\n", lr.segment_count, s->index_len);
        ++errors;
    }

    size_t check_count = lr.segment_count < s->index_len ? lr.segment_count : s->index_len;
    for (size_t i = 0u; i < check_count; ++i) {
        const SegmentIndexEntry* a = &lr.entries[i];
        const SegmentIndexEntry* b = &s->index_entries[i];
        if (a->segment_no != b->segment_no || a->start_index != b->start_index ||
            a->next_index != b->next_index || a->total_candidates != b->total_candidates ||
            a->file_offset != b->file_offset || a->start_n != b->start_n || a->end_n != b->end_n) {
            printf("  MISMATCH: segment %zu differs\n", i);
            ++errors;
            if (errors > 10) { printf("  ... (truncated)\n"); break; }
        }
    }

    free(lr.entries);
    free(lr.prime_counts);
    free(lr.prime_prefix);

    if (errors == 0) {
        printf("verify-index: OK (%zu segments match)\n", check_count);
        return 0;
    }
    printf("verify-index: FAILED (%d mismatches)\n", errors);
    return -1;
}
