/**
 * cpss_manifest.c - Shard manifest (.cpss-manifest.json) for fast DB open.
 * Part of the CPSS Viewer amalgamation.
 *
 * JSON control-plane file that describes a shard directory:
 *   - ordered shard list with paths, ranges, segment counts
 *   - coverage summary (db_lo, db_hi, gaps, overlaps)
 *   - optional sidecar index paths
 *
 * Drives cpss_db_open fast-path: skip directory enumeration when
 * manifest is valid and all referenced shard files exist.
 *
 * Uses hand-written JSON emitter (no library dependency).
 * Parser is line-based extraction — not a full JSON parser, but
 * sufficient for the fixed schema we produce.
 */

#define MANIFEST_FILENAME ".cpss-manifest.json"
#define MANIFEST_SCHEMA_VERSION 1

/* ── JSON writing helpers ─────────────────────────────────────────── */

static void json_indent(FILE* fp, int depth) {
    for (int i = 0; i < depth; ++i) fprintf(fp, "  ");
}

/* ── Manifest write ───────────────────────────────────────────────── */

/** Shard info collected for manifest generation. */
typedef struct {
    char* path;          /* filename only (not full path) */
    char* sidecar;       /* sidecar filename or NULL */
    uint64_t lo, hi;
    size_t segments;
    uint64_t file_size;
} ManifestShard;

/** Gap/overlap record. */
typedef struct {
    size_t shard_a, shard_b;
    uint64_t a_hi, b_lo;
    bool is_gap;         /* true = gap, false = overlap */
} ManifestIssue;

/**
 * Write a .cpss-manifest.json for a shard directory.
 * shards must be sorted by lo. Returns 0 on success.
 */
static int manifest_write(const char* dir_path,
    ManifestShard* shards, size_t shard_count,
    ManifestIssue* issues, size_t issue_count) {

    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s\\" MANIFEST_FILENAME, dir_path);

    FILE* fp = fopen(manifest_path, "w");
    if (!fp) {
        fprintf(stderr, "error: cannot create manifest %s: %s\n", manifest_path, strerror(errno));
        return -1;
    }

    /* Compute coverage */
    uint64_t db_lo = shard_count > 0u ? shards[0].lo : 0u;
    uint64_t db_hi = shard_count > 0u ? shards[shard_count - 1u].hi : 0u;
    size_t total_segments = 0u;
    for (size_t i = 0u; i < shard_count; ++i) total_segments += shards[i].segments;

    size_t gap_count = 0u, overlap_count = 0u;
    for (size_t i = 0u; i < issue_count; ++i) {
        if (issues[i].is_gap) ++gap_count;
        else ++overlap_count;
    }

    fprintf(fp, "{\n");
    json_indent(fp, 1); fprintf(fp, "\"schema_version\": %d,\n", MANIFEST_SCHEMA_VERSION);

    /* Timestamp */
    {
        SYSTEMTIME st;
        GetSystemTime(&st);
        json_indent(fp, 1);
        fprintf(fp, "\"created\": \"%04d-%02d-%02dT%02d:%02d:%02dZ\",\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    }

    /* Shards array */
    json_indent(fp, 1); fprintf(fp, "\"shards\": [\n");
    for (size_t i = 0u; i < shard_count; ++i) {
        json_indent(fp, 2); fprintf(fp, "{\n");
        json_indent(fp, 3); fprintf(fp, "\"path\": \"%s\",\n", shards[i].path);
        json_indent(fp, 3); fprintf(fp, "\"lo\": %" PRIu64 ",\n", shards[i].lo);
        json_indent(fp, 3); fprintf(fp, "\"hi\": %" PRIu64 ",\n", shards[i].hi);
        json_indent(fp, 3); fprintf(fp, "\"segments\": %zu,\n", shards[i].segments);
        json_indent(fp, 3); fprintf(fp, "\"file_size\": %" PRIu64 ",\n", shards[i].file_size);
        if (shards[i].sidecar) {
            json_indent(fp, 3); fprintf(fp, "\"sidecar\": \"%s\"\n", shards[i].sidecar);
        }
        else {
            json_indent(fp, 3); fprintf(fp, "\"sidecar\": null\n");
        }
        json_indent(fp, 2); fprintf(fp, "}%s\n", i + 1u < shard_count ? "," : "");
    }
    json_indent(fp, 1); fprintf(fp, "],\n");

    /* Coverage */
    json_indent(fp, 1); fprintf(fp, "\"coverage\": {\n");
    json_indent(fp, 2); fprintf(fp, "\"db_lo\": %" PRIu64 ",\n", db_lo);
    json_indent(fp, 2); fprintf(fp, "\"db_hi\": %" PRIu64 ",\n", db_hi);
    json_indent(fp, 2); fprintf(fp, "\"total_segments\": %zu,\n", total_segments);
    json_indent(fp, 2); fprintf(fp, "\"total_shards\": %zu,\n", shard_count);

    /* Gaps */
    json_indent(fp, 2); fprintf(fp, "\"gaps\": [");
    {
        bool first = true;
        for (size_t i = 0u; i < issue_count; ++i) {
            if (!issues[i].is_gap) continue;
            if (!first) fprintf(fp, ",");
            fprintf(fp, "\n");
            json_indent(fp, 3);
            fprintf(fp, "{\"after_shard\": %zu, \"before_shard\": %zu, \"gap_start\": %" PRIu64 ", \"gap_end\": %" PRIu64 "}",
                issues[i].shard_a, issues[i].shard_b, issues[i].a_hi + 1u, issues[i].b_lo - 1u);
            first = false;
        }
        if (!first) { fprintf(fp, "\n"); json_indent(fp, 2); }
    }
    fprintf(fp, "],\n");

    /* Overlaps */
    json_indent(fp, 2); fprintf(fp, "\"overlaps\": [");
    {
        bool first = true;
        for (size_t i = 0u; i < issue_count; ++i) {
            if (issues[i].is_gap) continue;
            if (!first) fprintf(fp, ",");
            fprintf(fp, "\n");
            json_indent(fp, 3);
            fprintf(fp, "{\"shard_a\": %zu, \"shard_b\": %zu, \"overlap_start\": %" PRIu64 ", \"overlap_end\": %" PRIu64 "}",
                issues[i].shard_a, issues[i].shard_b, issues[i].b_lo, issues[i].a_hi);
            first = false;
        }
        if (!first) { fprintf(fp, "\n"); json_indent(fp, 2); }
    }
    fprintf(fp, "]\n");

    json_indent(fp, 1); fprintf(fp, "}\n");
    fprintf(fp, "}\n");
    fclose(fp);
    return 0;
}

/* ── Manifest read (minimal line-based JSON extraction) ───────────── */

/* ManifestShardEntry and ManifestLoadResult are defined in cpss_types.c */

/** Extract a uint64 value from a JSON line like '    "key": 12345,' */
static bool json_extract_u64(const char* line, const char* key, uint64_t* out) {
    const char* p = strstr(line, key);
    if (!p) return false;
    p += strlen(key);
    /* Skip to colon, then whitespace */
    p = strchr(p, ':');
    if (!p) return false;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    char* end = NULL;
    errno = 0;
    *out = strtoull(p, &end, 10);
    return (errno == 0 && end != p);
}

/** Extract a string value from a JSON line like '    "key": "value",' */
static bool json_extract_str(const char* line, const char* key, char* out, size_t out_cap) {
    const char* p = strstr(line, key);
    if (!p) return false;
    p = strchr(p + strlen(key), ':');
    if (!p) return false;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 'n') { /* null */
        out[0] = '\0';
        return true;
    }
    if (*p != '"') return false;
    ++p;
    const char* end = strchr(p, '"');
    if (!end) return false;
    size_t len = (size_t)(end - p);
    if (len >= out_cap) len = out_cap - 1u;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/**
 * Load a .cpss-manifest.json from a shard directory.
 * Returns ManifestLoadResult. Caller must free result.shards.
 */
static ManifestLoadResult manifest_load(const char* dir_path) {
    ManifestLoadResult r;
    memset(&r, 0, sizeof(r));

    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s\\" MANIFEST_FILENAME, dir_path);

    FILE* fp = fopen(manifest_path, "r");
    if (!fp) {
        r.reject_reason = "manifest file not found";
        return r;
    }

    /* Allocate shards array (grow as needed) */
    size_t shard_cap = 16u;
    ManifestShardEntry* shards = (ManifestShardEntry*)xcalloc(shard_cap, sizeof(ManifestShardEntry));
    size_t shard_count = 0u;
    bool in_shards = false;
    bool in_shard_obj = false;
    bool in_coverage = false;

    char line[2048];
    ManifestShardEntry cur;
    memset(&cur, 0, sizeof(cur));

    while (fgets(line, sizeof(line), fp)) {
        /* Schema version */
        {
            uint64_t v;
            if (json_extract_u64(line, "\"schema_version\"", &v)) {
                r.schema_version = (int)v;
                if (r.schema_version != MANIFEST_SCHEMA_VERSION) {
                    r.reject_reason = "unsupported schema version";
                    free(shards);
                    fclose(fp);
                    return r;
                }
            }
        }

        /* Detect sections */
        if (strstr(line, "\"shards\"")) { in_shards = true; continue; }
        if (strstr(line, "\"coverage\"")) { in_coverage = true; in_shards = false; continue; }

        if (in_shards) {
            if (strchr(line, '{') && !in_shard_obj) {
                in_shard_obj = true;
                memset(&cur, 0, sizeof(cur));
                continue;
            }
            if (in_shard_obj) {
                json_extract_str(line, "\"path\"", cur.path, sizeof(cur.path));
                json_extract_u64(line, "\"lo\"", &cur.lo);
                json_extract_u64(line, "\"hi\"", &cur.hi);
                { uint64_t v; if (json_extract_u64(line, "\"segments\"", &v)) cur.segments = (size_t)v; }
                json_extract_u64(line, "\"file_size\"", &cur.file_size);
                if (json_extract_str(line, "\"sidecar\"", cur.sidecar, sizeof(cur.sidecar))) {
                    cur.has_sidecar = (cur.sidecar[0] != '\0');
                }

                if (strchr(line, '}')) {
                    /* End of shard object */
                    if (shard_count == shard_cap) {
                        shard_cap *= 2u;
                        shards = (ManifestShardEntry*)xrealloc(shards, shard_cap * sizeof(ManifestShardEntry));
                    }
                    shards[shard_count++] = cur;
                    in_shard_obj = false;
                }
            }
            /* Detect end of shards array */
            if (!in_shard_obj && strchr(line, ']')) {
                in_shards = false;
            }
        }

        if (in_coverage) {
            json_extract_u64(line, "\"db_lo\"", &r.db_lo);
            json_extract_u64(line, "\"db_hi\"", &r.db_hi);
            { uint64_t v; if (json_extract_u64(line, "\"total_segments\"", &v)) r.total_segments = (size_t)v; }
        }
    }

    fclose(fp);

    if (shard_count == 0u) {
        r.reject_reason = "no shards found in manifest";
        free(shards);
        return r;
    }

    r.shards = shards;
    r.shard_count = shard_count;
    r.loaded = true;
    return r;
}

/** Free a manifest load result. */
static void manifest_load_free(ManifestLoadResult* r) {
    free(r->shards);
    memset(r, 0, sizeof(*r));
}

/**
 * Build a manifest from a live CPSSDatabase (after cpss_db_open).
 * Returns 0 on success, -1 on error.
 */
static int manifest_build_from_db(CPSSDatabase* db, const char* dir_path) {
    ManifestShard* shards = (ManifestShard*)xcalloc(db->shard_count, sizeof(ManifestShard));
    ManifestIssue* issues = (ManifestIssue*)xcalloc(db->shard_count, sizeof(ManifestIssue));
    size_t issue_count = 0u;

    for (size_t i = 0u; i < db->shard_count; ++i) {
        /* Extract just the filename from the full path */
        const char* full = db->shard_paths[i];
        const char* fname = strrchr(full, '\\');
        if (!fname) fname = strrchr(full, '/');
        shards[i].path = _strdup(fname ? fname + 1 : full);
        shards[i].lo = db->shard_lo[i];
        shards[i].hi = db->shard_hi[i];
        shards[i].segments = db->streams[i].index_len;
        shards[i].file_size = (uint64_t)db->streams[i].file_size;

        /* Check for existing sidecar */
        char sidecar[1024];
        cpssi_path_for(full, sidecar, sizeof(sidecar));
        if (GetFileAttributesA(sidecar) != INVALID_FILE_ATTRIBUTES) {
            const char* sc_fname = strrchr(sidecar, '\\');
            if (!sc_fname) sc_fname = strrchr(sidecar, '/');
            shards[i].sidecar = _strdup(sc_fname ? sc_fname + 1 : sidecar);
        }
    }

    /* Detect gaps and overlaps */
    for (size_t i = 1u; i < db->shard_count; ++i) {
        if (db->shard_lo[i] > db->shard_hi[i - 1u] + 1u) {
            issues[issue_count].shard_a = i - 1u;
            issues[issue_count].shard_b = i;
            issues[issue_count].a_hi = db->shard_hi[i - 1u];
            issues[issue_count].b_lo = db->shard_lo[i];
            issues[issue_count].is_gap = true;
            ++issue_count;
        }
        else if (db->shard_lo[i] <= db->shard_hi[i - 1u]) {
            issues[issue_count].shard_a = i - 1u;
            issues[issue_count].shard_b = i;
            issues[issue_count].a_hi = db->shard_hi[i - 1u];
            issues[issue_count].b_lo = db->shard_lo[i];
            issues[issue_count].is_gap = false;
            ++issue_count;
        }
    }

    int rc = manifest_write(dir_path, shards, db->shard_count, issues, issue_count);

    for (size_t i = 0u; i < db->shard_count; ++i) {
        free(shards[i].path);
        free(shards[i].sidecar);
    }
    free(shards);
    free(issues);
    return rc;
}

/**
 * Print coverage info for a shard DB to stdout.
 */
static void manifest_print_coverage(CPSSDatabase* db) {
    printf("coverage:\n");
    printf("  db_lo              = %" PRIu64 "\n", db->db_lo);
    printf("  db_hi              = %" PRIu64 "\n", db->db_hi);
    printf("  total shards       = %zu\n", db->shard_count);
    printf("  total segments     = %zu\n", db->total_segments);

    /* Gaps */
    size_t gaps = 0u;
    for (size_t i = 1u; i < db->shard_count; ++i) {
        if (db->shard_lo[i] > db->shard_hi[i - 1u] + 1u) {
            if (gaps == 0u) printf("  gaps:\n");
            printf("    [%zu-%zu] %" PRIu64 "..%" PRIu64 "\n",
                i - 1u, i, db->shard_hi[i - 1u] + 1u, db->shard_lo[i] - 1u);
            ++gaps;
        }
    }
    if (gaps == 0u) printf("  gaps               = none\n");

    /* Overlaps */
    size_t overlaps = 0u;
    for (size_t i = 1u; i < db->shard_count; ++i) {
        if (db->shard_lo[i] <= db->shard_hi[i - 1u]) {
            if (overlaps == 0u) printf("  overlaps:\n");
            printf("    [%zu-%zu] %" PRIu64 "..%" PRIu64 "\n",
                i - 1u, i, db->shard_lo[i], db->shard_hi[i - 1u]);
            ++overlaps;
        }
    }
    if (overlaps == 0u) printf("  overlaps           = none\n");
}
