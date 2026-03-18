/**
 * cpss_cli.c - CLI helpers and legacy viewer commands: parse, print_usage, cmd_info/list/export/clip/split
 * Part of the CPSS Viewer amalgamation.
 */
static const char* human_size(uint64_t n, char* buf, size_t buf_len) {
    static const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double x = (double)n;
    size_t unit = 0u;
    while (x >= 1024.0 && unit + 1u < ARRAY_LEN(units)) {
        x /= 1024.0;
        ++unit;
    }
    snprintf(buf, buf_len, "%.2f %s", x, units[unit]);
    return buf;
}

/** Parse a string as uint64, dying on error. */
static uint64_t parse_u64(const char* s, const char* name) {
    errno = 0;
    char* end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        fprintf(stderr, "error: invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (uint64_t)v;
}

/** Parse a string as U128, dying on error. Handles numbers > 2^64. */
static U128 parse_u128(const char* s, const char* name) {
    /* Skip leading whitespace */
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '\0' || (*s < '0' || *s > '9')) {
        fprintf(stderr, "error: invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    /* Check if it fits in uint64 first (common fast path) */
    size_t len = strlen(s);
    if (len <= 19) { /* max uint64 is 20 digits, 19 always fits */
        return u128_from_u64(parse_u64(s, name));
    }
    /* For longer numbers, use u128_from_str */
    U128 result = u128_from_str(s);
    /* Validate: check that the entire string was consumed (no trailing junk) */
    const char* p = s;
    while (*p >= '0' && *p <= '9') ++p;
    if (*p != '\0') {
        fprintf(stderr, "error: invalid %s: %s (trailing characters)\n", name, s);
        exit(EXIT_FAILURE);
    }
    return result;
}

/** Parse a string as BigNum (up to 1024 bits), dying on error. */
static BigNum parse_bignum(const char* s, const char* name) {
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '\0' || *s < '0' || *s > '9') {
        fprintf(stderr, "error: invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    BigNum result;
    if (!bn_from_str(&result, s)) {
        fprintf(stderr, "error: %s overflows %d-bit limit: %s\n", name, BN_MAX_LIMBS * 64, s);
        exit(EXIT_FAILURE);
    }
    /* Validate no trailing junk */
    const char* p = s;
    while (*p >= '0' && *p <= '9') ++p;
    if (*p != '\0') {
        fprintf(stderr, "error: invalid %s: %s (trailing characters)\n", name, s);
        exit(EXIT_FAILURE);
    }
    return result;
}

/** Universal numeric parser with tier detection.
 *  Parses any decimal string into ParsedNum, auto-detecting the smallest
 *  tier that can represent the value. Never truncates silently. */
static ParsedNum parse_numeric(const char* s, const char* name) {
    ParsedNum pn;
    memset(&pn, 0, sizeof(pn));
    pn.bn = parse_bignum(s, name); /* dies on parse error / overflow */
    pn.bits = bn_bitlen(&pn.bn);
    if (bn_fits_u64(&pn.bn)) {
        pn.tier = TIER_U64;
        pn.u64 = bn_to_u64(&pn.bn);
        pn.u128 = u128_from_u64(pn.u64);
    } else if (bn_fits_u128(&pn.bn)) {
        pn.tier = TIER_U128;
        pn.u128 = bn_to_u128(&pn.bn);
    } else {
        pn.tier = TIER_BN;
    }
    return pn;
}

static const char* tier_name(NumericTier t) {
    switch (t) { case TIER_U64: return "uint64"; case TIER_U128: return "U128"; default: return "BigNum"; }
}

/** Parse a string as int, dying on error. */
static int parse_int(const char* s, const char* name) {
    errno = 0;
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || !end || *end != '\0' || v < INT_MIN || v > INT_MAX) {
        fprintf(stderr, "error: invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (int)v;
}

/** Print CLI usage to the given file pointer. */
static void print_usage(FILE* fp) {
    fprintf(fp,
        "Usage:\n"
        "  cpss_viewer info <file> [--deep] [--max-deep-segments N]\n"
        "  cpss_viewer list <file> [--limit N]\n"
        "  cpss_viewer export-primes <file> <lo> <hi> <out> [--limit N]\n"
        "  cpss_viewer clip <file> <lo> <hi> <out> [--force]\n"
        "  cpss_viewer split-trillion <file> <out_dir> [--trillion N] [--start-bucket N] [--end-bucket N] [--force]\n"
        "  cpss_viewer benchmark <file> <lo> <hi> [--limit N] [--include-write] [--mode hot|fair|auto]\n"
        "  cpss_viewer decompress-file <input> <output>   Convert compressed V4 -> raw CPSR format\n"
        "  cpss_viewer compress-file <input> <output>     Convert raw CPSR -> compressed V4 format\n"
        "\n"
        "Benchmark modes:\n"
        "  hot  - Assumes sector is in production-hot in-memory form (decomp+presieve excluded if applicable)\n"
        "  fair - Includes per-segment online work (decomp if 1.2x rule, presieve always)\n"
        "  auto - Same as fair with per-sector 1.2x decompression decision reported (default)\n"
    );
}

/** CLI handler: print stream summary and validation results. */
static int cmd_info(int argc, char** argv) {
    if (argc < 3) {
        print_usage(stderr);
        return 1;
    }

    bool deep = false;
    uint64_t max_deep_segments = UINT64_MAX;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--deep") == 0) {
            deep = true;
        }
        else if (strcmp(argv[i], "--max-deep-segments") == 0 && i + 1 < argc) {
            max_deep_segments = parse_u64(argv[++i], "max-deep-segments");
        }
        else {
            print_usage(stderr);
            return 1;
        }
    }

    CPSSStream cpss;
    cpss_open(&cpss, argv[2]);
    ValidateSummary summary = cpss_validate(&cpss, deep, max_deep_segments);

    char raw_buf[64];
    char comp_buf[64];
    printf("file                = %s\n", argv[2]);
    printf("wheel modulus       = %" PRIu32 "\n", g_wheel_modulus);
    printf("wheel residues      = %" PRIu32 "\n", g_residue_count);
    printf("presieve limit      = %" PRIu32 "\n", PRESIEVE_LIMIT);
    printf("segments            = %zu\n", summary.segments);
    printf("total candidates    = %" PRIu64 "\n", summary.total_candidates);
    printf("total survivors     = %" PRIu64 "\n", summary.total_survivors);
    printf("total primes        = %" PRIu64 "\n", summary.total_primes);
    printf("last index          = %" PRIu64 "\n", summary.last_next_index);
    printf("last candidate      = %" PRIu64 "\n", summary.last_candidate);
    printf("raw total           = %s\n", human_size(summary.raw_total_bytes, raw_buf, sizeof(raw_buf)));
    printf("compressed total    = %s\n", human_size(summary.compressed_total_bytes, comp_buf, sizeof(comp_buf)));
    printf("compression ratio   = %.3fx\n", summary.compression_ratio);

    cpss_close(&cpss);
    return 0;
}

/** CLI handler: list segment ranges with per-segment statistics. */
static int cmd_list(int argc, char** argv) {
    if (argc < 3) {
        print_usage(stderr);
        return 1;
    }

    uint64_t limit = 20u;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = parse_u64(argv[++i], "limit");
        }
        else {
            print_usage(stderr);
            return 1;
        }
    }

    CPSSStream cpss;
    cpss_open(&cpss, argv[2]);
    cpss_build_segment_index(&cpss);

    for (size_t i = 0u; i < cpss.index_len && i < limit; ++i) {
        SegmentRecord rec;
        if (!cpss_load_segment(&cpss, i, &rec)) continue;

        char raw_buf[64];
        char comp_buf[64];
        printf(
            "seg=%" PRIu64 " | idx=%" PRIu64 "..%" PRIu64 " | n=%" PRIu64 "..%" PRIu64
            " | candidates=%" PRIu32 " | survivors=%" PRIu32 " | primes=%" PRIu64
            " | raw=%s | z=%s\n",
            rec.segment_no,
            rec.start_index,
            rec.next_index - 1u,
            record_start_n(&rec),
            record_end_n(&rec),
            rec.total_candidates,
            rec.survivor_count,
            bit_count_bytes(rec.result_bits, rec.result_bits_len),
            human_size(rec.raw_size, raw_buf, sizeof(raw_buf)),
            human_size(rec.compressed_size, comp_buf, sizeof(comp_buf))
        );

        segment_record_free(&rec);
    }

    cpss_close(&cpss);
    return 0;
}

/** CLI handler: export decoded primes in a numeric range to a text file. */
static int cmd_export_primes(int argc, char** argv) {
    if (argc < 6) {
        print_usage(stderr);
        return 1;
    }

    uint64_t lo = parse_u64(argv[3], "lo");
    uint64_t hi = parse_u64(argv[4], "hi");
    uint64_t limit = 0u;
    for (int i = 6; i < argc; ++i) {
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = parse_u64(argv[++i], "limit");
        }
        else {
            print_usage(stderr);
            return 1;
        }
    }

    CPSSStream cpss;
    cpss_open(&cpss, argv[2]);

    FILE* out = fopen(argv[5], "w");
    if (!out) {
        cpss_close(&cpss);
        die_errno("failed to open export output");
    }

    double t0 = now_sec();
    uint64_t count = 0u;
    cpss_scan_range(&cpss, lo, hi, limit, out, &count);
    double t1 = now_sec();

    fclose(out);
    cpss_close(&cpss);

    printf("wrote %" PRIu64 " primes to %s in %.2fs\n", count, argv[5], t1 - t0);
    return 0;
}

/** CLI handler: create a new CPSS file clipped to [lo,hi]. */
static int cmd_clip(int argc, char** argv) {
    if (argc < 6) {
        print_usage(stderr);
        return 1;
    }

    uint64_t lo = parse_u64(argv[3], "lo");
    uint64_t hi = parse_u64(argv[4], "hi");
    bool force = false;
    for (int i = 6; i < argc; ++i) {
        if (strcmp(argv[i], "--force") == 0) {
            force = true;
        }
        else {
            print_usage(stderr);
            return 1;
        }
    }

    CPSSStream cpss;
    cpss_open(&cpss, argv[2]);
    cpss_clip_to_range(&cpss, argv[5], lo, hi, force);
    cpss_close(&cpss);
    return 0;
}

/** CLI handler: split the stream into per-trillion CPSS files. */
static int cmd_split_trillion(int argc, char** argv) {
    if (argc < 4) {
        print_usage(stderr);
        return 1;
    }

    uint64_t trillion = DEFAULT_TRILLION;
    uint64_t start_bucket = 0u;
    uint64_t end_bucket = 0u;
    bool end_bucket_given = false;
    bool force = false;

    for (int i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--trillion") == 0 && i + 1 < argc) {
            trillion = parse_u64(argv[++i], "trillion");
        }
        else if (strcmp(argv[i], "--start-bucket") == 0 && i + 1 < argc) {
            start_bucket = parse_u64(argv[++i], "start-bucket");
        }
        else if (strcmp(argv[i], "--end-bucket") == 0 && i + 1 < argc) {
            end_bucket = parse_u64(argv[++i], "end-bucket");
            end_bucket_given = true;
        }
        else if (strcmp(argv[i], "--force") == 0) {
            force = true;
        }
        else {
            print_usage(stderr);
            return 1;
        }
    }

    CPSSStream cpss;
    cpss_open(&cpss, argv[2]);
    cpss_split_into_trillion_files(&cpss, argv[3], trillion, force, start_bucket, end_bucket, end_bucket_given);
    cpss_close(&cpss);
    return 0;
}

/** CLI handler: decompress a CPSS V4 file into raw CPSR format. */
static int cmd_decompress_file(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: cpss_viewer decompress-file <input.bin> <output.bin>\n");
        return 1;
    }
    const char* in_path = argv[2];
    const char* out_path = argv[3];

    CPSSStream cpss;
    cpss_open(&cpss, in_path);

    if (cpss.is_raw) {
        printf("File is already in raw (CPSR) format: %s\n", in_path);
        cpss_close(&cpss);
        return 0;
    }

    cpss_build_segment_index(&cpss);
    printf("decompressing %zu segments from %s -> %s\n", cpss.index_len, in_path, out_path);

    FILE* out = fopen(out_path, "wb");
    if (!out) { cpss_close(&cpss); die_errno("failed to open output file"); }

    /* Write CPSR header: magic(4) + version(4) + flags(4) + reserved(4) */
    write_u32_le(out, CPSR_MAGIC);
    write_u32_le(out, CPSR_VERSION);
    write_u32_le(out, 0u); /* flags: 0 = raw */
    write_u32_le(out, 0u); /* reserved */

    char cb[64];
    uint64_t total_written = CPSR_HEADER_SIZE;
    for (size_t i = 0u; i < cpss.index_len; ++i) {
        SegmentRecord rec;
        if (!cpss_load_segment(&cpss, i, &rec)) {
            fprintf(stderr, "error: failed to load segment %zu\n", i);
            fclose(out);
            cpss_close(&cpss);
            return 1;
        }

        /* Write [4-byte raw_len][12-byte header][result_bits...] */
        uint32_t raw_len = (uint32_t)rec.raw_size;
        write_u32_le(out, raw_len);
        write_u32_le(out, rec.total_candidates);
        write_u32_le(out, rec.survivor_count);
        write_u32_le(out, rec.result_bits_len);
        if (fwrite(rec.result_bits, 1, rec.result_bits_len, out) != rec.result_bits_len) {
            die_errno("failed writing result_bits");
        }
        total_written += 4u + raw_len;
        segment_record_free(&rec);

        if ((i + 1u) % 100u == 0u || i + 1u == cpss.index_len) {
            printf("\r  %zu / %zu segments (%s written)", i + 1u, cpss.index_len,
                human_size(total_written, cb, sizeof(cb)));
            fflush(stdout);
        }
    }
    printf("\n  done. output = %s (%s)\n", out_path, human_size(total_written, cb, sizeof(cb)));

    fclose(out);
    cpss_close(&cpss);
    return 0;
}

/** CLI handler: compress a raw CPSR file back into compressed V4 format. */
static int cmd_compress_file(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: cpss_viewer compress-file <input.bin> <output.bin>\n");
        return 1;
    }
    const char* in_path = argv[2];
    const char* out_path = argv[3];

    CPSSStream cpss;
    cpss_open(&cpss, in_path);

    if (!cpss.is_raw) {
        printf("File is already in compressed (V4) format: %s\n", in_path);
        cpss_close(&cpss);
        return 0;
    }

    cpss_build_segment_index(&cpss);
    printf("compressing %zu segments from %s -> %s\n", cpss.index_len, in_path, out_path);

    FILE* out = fopen(out_path, "wb");
    if (!out) { cpss_close(&cpss); die_errno("failed to open output file"); }

    /* No header for V4 compressed format (backward compatible) */
    char cb[64];
    uint64_t total_written = 0u;
    for (size_t i = 0u; i < cpss.index_len; ++i) {
        SegmentRecord rec;
        if (!cpss_load_segment(&cpss, i, &rec)) {
            fprintf(stderr, "error: failed to load segment %zu\n", i);
            fclose(out);
            cpss_close(&cpss);
            return 1;
        }

        /* Build raw payload: [12-byte header][result_bits...] */
        size_t raw_len = 12u + rec.result_bits_len;
        uint8_t* raw = (uint8_t*)xmalloc(raw_len);
        raw[0] = (uint8_t)(rec.total_candidates & 0xFFu);
        raw[1] = (uint8_t)((rec.total_candidates >> 8) & 0xFFu);
        raw[2] = (uint8_t)((rec.total_candidates >> 16) & 0xFFu);
        raw[3] = (uint8_t)((rec.total_candidates >> 24) & 0xFFu);
        raw[4] = (uint8_t)(rec.survivor_count & 0xFFu);
        raw[5] = (uint8_t)((rec.survivor_count >> 8) & 0xFFu);
        raw[6] = (uint8_t)((rec.survivor_count >> 16) & 0xFFu);
        raw[7] = (uint8_t)((rec.survivor_count >> 24) & 0xFFu);
        raw[8] = (uint8_t)(rec.result_bits_len & 0xFFu);
        raw[9] = (uint8_t)((rec.result_bits_len >> 8) & 0xFFu);
        raw[10] = (uint8_t)((rec.result_bits_len >> 16) & 0xFFu);
        raw[11] = (uint8_t)((rec.result_bits_len >> 24) & 0xFFu);
        memcpy(raw + 12u, rec.result_bits, rec.result_bits_len);

        uint8_t* compressed = NULL;
        size_t compressed_len = 0u;
        if (!zlib_compress_alloc(raw, raw_len, &compressed, &compressed_len)) {
            free(raw);
            segment_record_free(&rec);
            fclose(out);
            die("zlib compression failed");
        }

        /* Write [4-byte compressed_len][compressed_data] */
        write_u32_le(out, (uint32_t)compressed_len);
        if (fwrite(compressed, 1, compressed_len, out) != compressed_len) {
            die_errno("failed writing compressed segment");
        }
        total_written += 4u + compressed_len;

        free(raw);
        free(compressed);
        segment_record_free(&rec);

        if ((i + 1u) % 100u == 0u || i + 1u == cpss.index_len) {
            printf("\r  %zu / %zu segments (%s written)", i + 1u, cpss.index_len,
                human_size(total_written, cb, sizeof(cb)));
            fflush(stdout);
        }
    }
    printf("\n  done. output = %s (%s)\n", out_path, human_size(total_written, cb, sizeof(cb)));

    fclose(out);
    cpss_close(&cpss);
    return 0;
}

/** CLI handler: compare CPSS read speed vs segmented sieve over a range. */
static int cmd_benchmark(int argc, char** argv) {
    if (argc < 5) {
        print_usage(stderr);
        return 1;
    }

    uint64_t lo = parse_u64(argv[3], "lo");
    uint64_t hi = parse_u64(argv[4], "hi");
    uint64_t limit = 0u;
    bool include_write = false;
    BenchmarkMode mode = BENCH_AUTO;

    for (int i = 5; i < argc; ++i) {
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = parse_u64(argv[++i], "limit");
        }
        else if (strcmp(argv[i], "--include-write") == 0) {
            include_write = true;
        }
        else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = parse_benchmark_mode(argv[++i]);
        }
        else {
            print_usage(stderr);
            return 1;
        }
    }

    if (hi < lo) {
        die("benchmark: hi must be >= lo");
    }

    CPSSStream cpss;
    cpss_open(&cpss, argv[2]);
    cpss_build_segment_index(&cpss);

    /* Pre-warm base prime cache */
    uint32_t root = isqrt_u64(hi);
    ensure_base_primes_cached(root);

    bool exact_seg = false;
    size_t exact_seg_no = 0u;
    for (size_t i = 0u; i < cpss.index_len; ++i) {
        const SegmentIndexEntry* e = &cpss.index_entries[i];
        if (segment_start_n(e) == lo && segment_end_n(e) == hi) {
            exact_seg = true;
            exact_seg_no = i;
            break;
        }
    }

    printf("benchmark range     = %" PRIu64 "..%" PRIu64 "\n", lo, hi);
    printf("benchmark mode      = %s\n", benchmark_mode_name(mode));
    if (g_app_wheel_cache_bytes > 0) {
        char cb[64];
        printf("wheel cache budget  = %s\n", human_size((uint64_t)g_app_wheel_cache_bytes, cb, sizeof(cb)));
    }
    if (limit) {
        printf("prime export limit  = %" PRIu64 "\n", limit);
    }

    if (exact_seg) {
        /* Exact segment path: full component timing via benchmark_sector_fair */
        BenchmarkSectorResult r = benchmark_sector_fair(&cpss, exact_seg_no, 1, limit, include_write, mode);

        printf("segment             = %" PRIu64 "\n", r.segment_no);
        printf("compressed size     = %" PRIu32 " bytes\n", r.compressed_size);
        printf("raw size            = %zu bytes\n", r.raw_size);
        printf("expansion ratio     = %.2fx\n", r.expansion_ratio);
        printf("decomp included     = %s\n", r.decomp_included ? "YES" : "NO (production-resident)");
        printf("prep included       = %s\n", r.prep_included ? "YES" : "NO (production-resident)");
        if (mode == BENCH_HOT) {
            printf("wheel-cache hit     = %s\n", r.cache_hit ? "YES" : "NO");
            printf("presieve size       = %zu bytes\n", r.presieve_bytes);
            printf("presieve fits cache = %s\n", r.cache_fits ? "YES" : "NO");
        }
        printf("cpss decompress     = %.6fs\n", r.cpss_decompress_median);
        printf("cpss prep           = %.6fs\n", r.cpss_prep_median);
        printf("cpss query          = %.6fs\n", r.cpss_query_median);
        printf("cpss total (verdict)= %.6fs\n", r.cpss_total_median);
        printf("sieve total         = %.6fs\n", r.sieve_total_median);
        printf("count match         = %s\n", r.count_match ? "true" : "false");

        if (!r.count_match) {
            printf("WARNING: CPSS count and sieve count do not match.\n");
        }

        if (r.sieve_total_median > 0.0) {
            if (r.cpss_total_median < r.sieve_total_median) {
                printf("verdict             = CPSS is %.2fx faster\n", r.sieve_total_median / r.cpss_total_median);
            }
            else {
                printf("verdict             = sieve is %.2fx faster\n", r.cpss_total_median / r.sieve_total_median);
            }
        }

        benchmark_sector_result_free(&r);
    }
    else {
        /* Non-exact-segment path: total-time only, clearly labeled */
        printf("NOTE: range does not match exactly one segment.\n");
        printf("      Reporting total-time only (no component split).\n");

        BenchmarkRow row = benchmark_range_once(&cpss, lo, hi, limit, include_write);

        printf("cpss count          = %" PRIu64 "\n", row.cpss_count);
        printf("cpss total time     = %.6fs\n", row.cpss_total_time);
        printf("sieve count         = %" PRIu64 "\n", row.sieve_count);
        printf("sieve total time    = %.6fs\n", row.sieve_total_time);

        if (row.cpss_count != row.sieve_count) {
            printf("WARNING: CPSS count and sieve count do not match.\n");
        }

        if (row.sieve_total_time > 0.0) {
            double speed_ratio = row.cpss_total_time / row.sieve_total_time;
            if (speed_ratio < 1.0) {
                printf("verdict             = CPSS is %.2fx faster\n", 1.0 / speed_ratio);
            }
            else {
                printf("verdict             = sieve is %.2fx faster\n", speed_ratio);
            }
        }

        if (include_write && row.cpss_write_time_valid && row.sieve_write_time_valid && row.sieve_write_time > 0.0) {
            double write_ratio = row.cpss_write_time / row.sieve_write_time;
            printf("--- write timing (separate from verdict) ---\n");
            if (write_ratio < 1.0) {
                printf("write verdict       = CPSS path is %.2fx faster\n", 1.0 / write_ratio);
            }
            else {
                printf("write verdict       = sieve path is %.2fx faster\n", write_ratio);
            }
        }
    }

    cpss_close(&cpss);
    return 0;
}
