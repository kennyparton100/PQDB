/**
 * cpss_mutate.c - Mutation operations: clip, split-trillion, encode_result_bits
 * Part of the CPSS Viewer amalgamation.
 */

/** Re-encode a per-candidate prime bitmap into the CPSS two-layer format. */
static void encode_result_bits_from_prime_flags(
    const ByteBuf* presieve_state,
    const uint8_t* prime_flags,
    uint32_t total_candidates,
    uint8_t** out_result_bits,
    uint32_t* out_result_bits_len,
    uint32_t* out_survivor_count
) {
    uint64_t survivor_count64 = bit_count_bytes(presieve_state->data, presieve_state->len);
    if (survivor_count64 > UINT32_MAX) {
        die("survivor_count overflow");
    }
    uint32_t survivor_count = (uint32_t)survivor_count64;
    uint32_t result_bits_len = (survivor_count + 7u) / 8u;
    uint8_t* result_bits = (uint8_t*)xcalloc(result_bits_len ? result_bits_len : 1u, 1u);

    uint32_t survivor_pos = 0u;
    for (uint32_t idx = 0u; idx < total_candidates; ++idx) {
        if (!get_bit(presieve_state->data, idx)) {
            continue;
        }
        if (get_bit(prime_flags, idx)) {
            set_bit(result_bits, survivor_pos);
        }
        ++survivor_pos;
    }

    *out_result_bits = result_bits;
    *out_result_bits_len = result_bits_len;
    *out_survivor_count = survivor_count;
}

/** Clip a segment's candidate range to [lo,hi] and re-encode result bits. */
static bool clip_segment_to_numeric_range(const SegmentRecord* rec, uint64_t lo, uint64_t hi, ClippedSegment* out) {
    memset(out, 0, sizeof(*out));

    uint64_t rec_lo = record_start_n(rec);
    uint64_t rec_hi = record_end_n(rec);
    if (hi < rec_lo || lo > rec_hi) {
        return false;
    }

    uint64_t keep_start_index = 0u;
    uint64_t keep_end_index = 0u;

    if (!candidate_to_index(lo > rec_lo ? lo : rec_lo, &keep_start_index)) {
        if (!next_candidate_index_ge(lo > rec_lo ? lo : rec_lo, &keep_start_index)) {
            return false;
        }
    }
    if (!candidate_to_index(hi < rec_hi ? hi : rec_hi, &keep_end_index)) {
        if (!prev_candidate_index_le(hi < rec_hi ? hi : rec_hi, &keep_end_index)) {
            return false;
        }
    }

    if (keep_start_index < rec->start_index) {
        keep_start_index = rec->start_index;
    }
    if (keep_end_index >= rec->next_index) {
        keep_end_index = rec->next_index - 1u;
    }
    if (keep_end_index < keep_start_index) {
        return false;
    }

    uint32_t local_start = (uint32_t)(keep_start_index - rec->start_index);
    uint32_t local_end = (uint32_t)(keep_end_index - rec->start_index);
    uint32_t clipped_total_candidates = local_end - local_start + 1u;

    ByteBuf presieve_full = build_presieve_state_for_index_range(rec->start_index, rec->total_candidates);
    uint8_t* prime_flags_full = (uint8_t*)xcalloc((rec->total_candidates + 7u) / 8u, 1u);

    uint32_t survivor_pos = 0u;
    for (uint32_t idx = 0u; idx < rec->total_candidates; ++idx) {
        if (get_bit(presieve_full.data, idx)) {
            if (get_bit(rec->result_bits, survivor_pos)) {
                set_bit(prime_flags_full, idx);
            }
            ++survivor_pos;
        }
    }
    if (survivor_pos != rec->survivor_count) {
        free(presieve_full.data);
        free(prime_flags_full);
        die("survivor decoding mismatch during clip");
    }

    uint64_t new_start_index = rec->start_index + local_start;
    ByteBuf presieve_clipped = build_presieve_state_for_index_range(new_start_index, clipped_total_candidates);
    uint8_t* prime_flags_clipped = (uint8_t*)xcalloc((clipped_total_candidates + 7u) / 8u, 1u);

    for (uint32_t new_idx = 0u; new_idx < clipped_total_candidates; ++new_idx) {
        uint32_t old_idx = local_start + new_idx;
        if (get_bit(prime_flags_full, old_idx)) {
            set_bit(prime_flags_clipped, new_idx);
        }
    }

    uint8_t* result_bits = NULL;
    uint32_t result_bits_len = 0u;
    uint32_t survivor_count = 0u;
    encode_result_bits_from_prime_flags(
        &presieve_clipped,
        prime_flags_clipped,
        clipped_total_candidates,
        &result_bits,
        &result_bits_len,
        &survivor_count
    );

    free(presieve_full.data);
    free(prime_flags_full);
    free(presieve_clipped.data);
    free(prime_flags_clipped);

    out->total_candidates = clipped_total_candidates;
    out->survivor_count = survivor_count;
    out->result_bits = result_bits;
    out->result_bits_len = result_bits_len;
    return true;
}

/** Free a clipped segment's heap data. */
static void clipped_segment_free(ClippedSegment* seg) {
    free(seg->result_bits);
    memset(seg, 0, sizeof(*seg));
}

/** Write a new CPSS file containing only primes in [lo,hi]. */
static void cpss_clip_to_range(CPSSStream* s, const char* out_path, uint64_t lo, uint64_t hi, bool force) {
    if (hi < lo) {
        die("clip: hi must be >= lo");
    }
    if (!force && GetFileAttributesA(out_path) != INVALID_FILE_ATTRIBUTES) {
        die("clip: output file already exists; use --force");
    }

    FILE* out = fopen(out_path, "wb");
    if (!out) {
        die_errno("failed to open clip output");
    }

    cpss_build_segment_index(s);

    uint64_t written_segments = 0u;
    uint64_t written_candidates = 0u;
    uint64_t written_survivors = 0u;
    uint64_t written_primes = 0u;

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

        ClippedSegment clipped;
        if (clip_segment_to_numeric_range(&rec, lo, hi, &clipped)) {
            size_t raw_len = 12u + clipped.result_bits_len;
            uint8_t* raw = (uint8_t*)xmalloc(raw_len);
            raw[0] = (uint8_t)(clipped.total_candidates & 0xFFu);
            raw[1] = (uint8_t)((clipped.total_candidates >> 8) & 0xFFu);
            raw[2] = (uint8_t)((clipped.total_candidates >> 16) & 0xFFu);
            raw[3] = (uint8_t)((clipped.total_candidates >> 24) & 0xFFu);
            raw[4] = (uint8_t)(clipped.survivor_count & 0xFFu);
            raw[5] = (uint8_t)((clipped.survivor_count >> 8) & 0xFFu);
            raw[6] = (uint8_t)((clipped.survivor_count >> 16) & 0xFFu);
            raw[7] = (uint8_t)((clipped.survivor_count >> 24) & 0xFFu);
            raw[8] = (uint8_t)(clipped.result_bits_len & 0xFFu);
            raw[9] = (uint8_t)((clipped.result_bits_len >> 8) & 0xFFu);
            raw[10] = (uint8_t)((clipped.result_bits_len >> 16) & 0xFFu);
            raw[11] = (uint8_t)((clipped.result_bits_len >> 24) & 0xFFu);
            memcpy(raw + 12u, clipped.result_bits, clipped.result_bits_len);

            uint8_t* compressed = NULL;
            size_t compressed_len = 0u;
            if (!zlib_compress_alloc(raw, raw_len, &compressed, &compressed_len)) {
                free(raw);
                clipped_segment_free(&clipped);
                fclose(out);
                die("zlib compression failed during clip");
            }

            if (compressed_len > UINT32_MAX) {
                free(raw);
                free(compressed);
                clipped_segment_free(&clipped);
                fclose(out);
                die("compressed segment length overflow");
            }

            write_u32_le(out, (uint32_t)compressed_len);
            if (fwrite(compressed, 1, compressed_len, out) != compressed_len) {
                free(raw);
                free(compressed);
                clipped_segment_free(&clipped);
                fclose(out);
                die_errno("failed writing clipped segment");
            }

            written_segments += 1u;
            written_candidates += clipped.total_candidates;
            written_survivors += clipped.survivor_count;
            written_primes += bit_count_bytes(clipped.result_bits, clipped.result_bits_len);

            free(raw);
            free(compressed);
            clipped_segment_free(&clipped);
        }

        segment_record_free(&rec);
    }

    fclose(out);
    printf("output              = %s\n", out_path);
    printf("segments            = %" PRIu64 "\n", written_segments);
    printf("candidates          = %" PRIu64 "\n", written_candidates);
    printf("survivors           = %" PRIu64 "\n", written_survivors);
    printf("primes              = %" PRIu64 "\n", written_primes);
}

/** Split the stream into per-trillion-range CPSS files. */
static void cpss_split_into_trillion_files(
    CPSSStream* s,
    const char* out_dir,
    uint64_t trillion,
    bool force,
    uint64_t start_bucket,
    uint64_t end_bucket,
    bool end_bucket_given
) {
    ValidateSummary summary = cpss_validate(s, false, 0u);
    if (summary.last_candidate < 1u) {
        printf("created 0 files in %s\n", out_dir);
        return;
    }

    if (!CreateDirectoryA(out_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        die_win32("failed to create output directory");
    }

    uint64_t last_bucket = (summary.last_candidate - 1u) / trillion;
    if (!end_bucket_given || end_bucket > last_bucket) {
        end_bucket = last_bucket;
    }

    uint64_t created = 0u;
    for (uint64_t bucket = start_bucket; bucket <= end_bucket; ++bucket) {
        uint64_t lo = (bucket == 0u) ? 1u : bucket * trillion + 1u;
        uint64_t hi = (bucket + 1u) * trillion;
        if (hi > summary.last_candidate) {
            hi = summary.last_candidate;
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s\\PrimeStateV4_%" PRIu64 "_%" PRIu64 ".bin", out_dir, lo, hi);
        cpss_clip_to_range(s, path, lo, hi, force);
        ++created;
    }

    printf("created %" PRIu64 " files in %s\n", created, out_dir);
}

