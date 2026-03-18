/**
 * cpss_iter.c - Prime iterator / cursor API for streaming primes across segments.
 * Part of the CPSS Viewer amalgamation.
 *
 * PrimeIterator maintains cursor state across segment boundaries, using
 * hot cache when available. Much more efficient than repeated next_prime
 * calls for sequential access patterns (e.g., trial division, p-1).
 */

/**
 * Initialize a forward iterator starting at the first prime >= start_n.
 * After init, iter->valid is true if a prime was found, and iter->current
 * holds that prime.
 */
static void cpss_iter_init(PrimeIterator* iter, CPSSStream* s, uint64_t start_n) {
    memset(iter, 0, sizeof(*iter));
    iter->stream = s;
    iter->status = (int)CPSS_OK;

    cpss_build_segment_index(s);
    if (s->index_len == 0u) {
        iter->status = (int)CPSS_UNAVAILABLE;
        return;
    }

    /* Handle wheel primes */
    for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (WHEEL_PRIMES[i] >= start_n) {
            iter->current = WHEEL_PRIMES[i];
            iter->valid = true;
            iter->seg_idx = SIZE_MAX; /* sentinel: currently on a wheel prime */
            iter->local_idx = (uint64_t)i; /* reuse as wheel prime index */
            return;
        }
    }

    /* Find the segment containing start_n (or the first segment after it) */
    size_t seg_idx = cpss_find_segment_for_n(s, start_n);
    if (seg_idx == SIZE_MAX) {
        /* start_n might be before the first segment or between segments */
        for (size_t i = 0u; i < s->index_len; ++i) {
            if (segment_start_n(&s->index_entries[i]) >= start_n) {
                seg_idx = i;
                start_n = segment_start_n(&s->index_entries[i]);
                break;
            }
        }
        if (seg_idx == SIZE_MAX) {
            iter->status = (int)CPSS_OUT_OF_RANGE;
            return;
        }
    }

    /* Position within the segment and find the first prime >= start_n */
    iter->seg_idx = seg_idx;
    const SegmentIndexEntry* entry = &s->index_entries[seg_idx];

    /* Try hot path first */
    HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
    if (hot) {
        /* Find the first candidate index >= start_n within this segment */
        uint64_t target_idx;
        if (!next_candidate_index_ge(start_n, &target_idx)) {
            iter->status = (int)CPSS_OUT_OF_RANGE;
            return;
        }
        if (target_idx < entry->start_index) target_idx = entry->start_index;

        uint64_t local = target_idx - entry->start_index;
        uint64_t spos = survivor_pos_ranked(hot->presieve, hot->rank_prefix, local);

        for (uint64_t idx = local; idx < hot->total_candidates; ++idx) {
            if (!get_bit(hot->presieve, idx)) continue;
            if (get_bit(hot->result_bits, spos)) {
                uint64_t candidate = index_to_candidate(entry->start_index + idx);
                if (candidate >= start_n) {
                    iter->current = candidate;
                    iter->valid = true;
                    iter->local_idx = idx;
                    iter->survivor_pos = spos;
                    return;
                }
            }
            ++spos;
        }

        /* No prime found in this segment at or after start_n; try next segments */
        for (size_t si = seg_idx + 1u; si < s->index_len; ++si) {
            entry = &s->index_entries[si];
            hot = hot_cache_lookup(s->stream_id, entry->segment_no);
            if (!hot) {
                /* Fall back to next_prime for non-hot segments */
                uint64_t p = cpss_next_prime(s, segment_start_n(entry) - 1u);
                if (p > 0u) {
                    iter->current = p;
                    iter->valid = true;
                    iter->seg_idx = si;
                }
                else {
                    iter->status = (int)CPSS_OUT_OF_RANGE;
                }
                return;
            }
            spos = 0u;
            for (uint64_t idx = 0u; idx < hot->total_candidates; ++idx) {
                if (!get_bit(hot->presieve, idx)) continue;
                if (get_bit(hot->result_bits, spos)) {
                    iter->current = index_to_candidate(entry->start_index + idx);
                    iter->valid = true;
                    iter->seg_idx = si;
                    iter->local_idx = idx;
                    iter->survivor_pos = spos;
                    return;
                }
                ++spos;
            }
        }
        iter->status = (int)CPSS_OUT_OF_RANGE;
        return;
    }

    /* Cold fallback: use next_prime */
    uint64_t p = (start_n > 0u) ? cpss_next_prime(s, start_n - 1u) : cpss_next_prime(s, 0u);
    if (p > 0u && p >= start_n) {
        iter->current = p;
        iter->valid = true;
    }
    else {
        iter->status = (int)CPSS_OUT_OF_RANGE;
    }
}

/**
 * Advance the iterator to the next prime.
 * Returns true if a next prime was found (iter->current updated).
 * Returns false if the end of the stream is reached.
 */
static bool cpss_iter_next(PrimeIterator* iter) {
    if (!iter->valid) return false;
    CPSSStream* s = iter->stream;

    /* If we're on a wheel prime, advance through wheel primes first */
    if (iter->seg_idx == SIZE_MAX) {
        size_t wi = (size_t)iter->local_idx;
        if (wi + 1u < ARRAY_LEN(WHEEL_PRIMES)) {
            iter->local_idx = wi + 1u;
            iter->current = WHEEL_PRIMES[wi + 1u];
            return true;
        }
        /* Transition from wheel primes to segment data */
        cpss_build_segment_index(s);
        if (s->index_len == 0u) {
            iter->valid = false;
            iter->status = (int)CPSS_UNAVAILABLE;
            return false;
        }
        /* Find first prime in segment data */
        iter->seg_idx = 0u;
        const SegmentIndexEntry* entry = &s->index_entries[0];
        HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
        if (hot) {
            uint64_t spos = 0u;
            for (uint64_t idx = 0u; idx < hot->total_candidates; ++idx) {
                if (!get_bit(hot->presieve, idx)) continue;
                if (get_bit(hot->result_bits, spos)) {
                    iter->current = index_to_candidate(entry->start_index + idx);
                    iter->local_idx = idx;
                    iter->survivor_pos = spos;
                    return true;
                }
                ++spos;
            }
        }
        else {
            uint64_t p = cpss_next_prime(s, iter->current);
            if (p > 0u) {
                iter->current = p;
                return true;
            }
        }
        iter->valid = false;
        iter->status = (int)CPSS_OUT_OF_RANGE;
        return false;
    }

    /* We're in segment data — try to advance within current segment via hot cache */
    const SegmentIndexEntry* entry = &s->index_entries[iter->seg_idx];
    HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
    if (hot) {
        uint64_t spos = iter->survivor_pos;
        for (uint64_t idx = iter->local_idx + 1u; idx < hot->total_candidates; ++idx) {
            if (!get_bit(hot->presieve, idx)) continue;
            ++spos;
            if (get_bit(hot->result_bits, spos)) {
                iter->current = index_to_candidate(entry->start_index + idx);
                iter->local_idx = idx;
                iter->survivor_pos = spos;
                return true;
            }
        }

        /* End of segment — move to next segment */
        for (size_t si = iter->seg_idx + 1u; si < s->index_len; ++si) {
            entry = &s->index_entries[si];
            hot = hot_cache_lookup(s->stream_id, entry->segment_no);
            if (!hot) {
                /* Cold fallback */
                uint64_t p = cpss_next_prime(s, iter->current);
                if (p > 0u) {
                    iter->current = p;
                    iter->seg_idx = si;
                    return true;
                }
                iter->valid = false;
                iter->status = (int)CPSS_OUT_OF_RANGE;
                return false;
            }
            spos = 0u;
            for (uint64_t idx = 0u; idx < hot->total_candidates; ++idx) {
                if (!get_bit(hot->presieve, idx)) continue;
                if (get_bit(hot->result_bits, spos)) {
                    iter->current = index_to_candidate(entry->start_index + idx);
                    iter->seg_idx = si;
                    iter->local_idx = idx;
                    iter->survivor_pos = spos;
                    return true;
                }
                ++spos;
            }
        }
        iter->valid = false;
        iter->status = (int)CPSS_OUT_OF_RANGE;
        return false;
    }

    /* Cold fallback: just use next_prime */
    uint64_t p = cpss_next_prime(s, iter->current);
    if (p > 0u) {
        iter->current = p;
        return true;
    }
    iter->valid = false;
    iter->status = (int)CPSS_OUT_OF_RANGE;
    return false;
}

/**
 * Step the iterator backward to the previous prime.
 * Returns true if a previous prime was found (iter->current updated).
 * Returns false if the beginning of the stream is reached.
 */
static bool cpss_iter_prev(PrimeIterator* iter) {
    if (!iter->valid) return false;
    CPSSStream* s = iter->stream;

    /* If on a wheel prime, step backward through wheel primes */
    if (iter->seg_idx == SIZE_MAX) {
        size_t wi = (size_t)iter->local_idx;
        if (wi > 0u) {
            iter->local_idx = wi - 1u;
            iter->current = WHEEL_PRIMES[wi - 1u];
            return true;
        }
        /* At wheel prime 2, no previous prime */
        iter->valid = false;
        iter->status = (int)CPSS_OUT_OF_RANGE;
        return false;
    }

    /* In segment data — try to step backward within current segment via hot cache */
    const SegmentIndexEntry* entry = &s->index_entries[iter->seg_idx];
    HotSegmentEntry* hot = hot_cache_lookup(s->stream_id, entry->segment_no);
    if (hot && iter->local_idx > 0u) {
        /* Walk backward through presieve from local_idx - 1 */
        uint64_t spos = iter->survivor_pos;
        for (uint64_t idx = iter->local_idx; idx > 0u; ) {
            --idx;
            if (!get_bit(hot->presieve, idx)) continue;
            --spos;
            if (get_bit(hot->result_bits, spos)) {
                iter->current = index_to_candidate(entry->start_index + idx);
                iter->local_idx = idx;
                iter->survivor_pos = spos;
                return true;
            }
        }

        /* Beginning of segment — try previous segments */
        if (iter->seg_idx > 0u) {
            for (size_t si = iter->seg_idx; si > 0u; ) {
                --si;
                entry = &s->index_entries[si];
                hot = hot_cache_lookup(s->stream_id, entry->segment_no);
                if (!hot) {
                    /* Cold fallback */
                    uint64_t p = cpss_prev_prime(s, iter->current);
                    if (p > 0u) {
                        iter->current = p;
                        iter->seg_idx = si;
                        return true;
                    }
                    break;
                }
                /* Find last prime in this segment */
                uint64_t last_spos = hot->survivor_count - 1u;
                for (uint64_t idx = hot->total_candidates; idx > 0u; ) {
                    --idx;
                    if (!get_bit(hot->presieve, idx)) continue;
                    if (get_bit(hot->result_bits, last_spos)) {
                        iter->current = index_to_candidate(entry->start_index + idx);
                        iter->seg_idx = si;
                        iter->local_idx = idx;
                        iter->survivor_pos = last_spos;
                        return true;
                    }
                    if (last_spos == 0u) break;
                    --last_spos;
                }
            }
        }

        /* Fell through to wheel primes */
        size_t last_wp = ARRAY_LEN(WHEEL_PRIMES) - 1u;
        iter->current = WHEEL_PRIMES[last_wp];
        iter->seg_idx = SIZE_MAX;
        iter->local_idx = (uint64_t)last_wp;
        return true;
    }

    /* Cold fallback or at segment start */
    uint64_t p = cpss_prev_prime(s, iter->current);
    if (p > 0u) {
        iter->current = p;
        /* Check if we crossed into wheel primes */
        for (size_t i = 0u; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
            if (p == WHEEL_PRIMES[i]) {
                iter->seg_idx = SIZE_MAX;
                iter->local_idx = (uint64_t)i;
                return true;
            }
        }
        return true;
    }
    iter->valid = false;
    iter->status = (int)CPSS_OUT_OF_RANGE;
    return false;
}
