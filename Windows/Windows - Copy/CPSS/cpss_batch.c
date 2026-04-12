/**
 * cpss_batch.c - Query-file / factor-file batch pipeline engine.
 * Part of the CPSS Viewer amalgamation.
 *
 * Supports:
 *   - Auto-detect input format (plain text one-per-line vs CSV with header)
 *   - CSV and JSONL output
 *   - Locality sorting for cache efficiency
 *   - Preserves original input order in output
 *
 * Single-number commands: is-prime, next-prime, prev-prime, classify, support-coverage
 * Range commands: count-range (lo/hi input)
 * Factor commands: factor-file (auto-factor)
 */

static uint64_t cpss_db_next_prime_s(CPSSDatabase* db, uint64_t n, CPSSQueryStatus* out_status);
static uint64_t cpss_db_prev_prime_s(CPSSDatabase* db, uint64_t n, CPSSQueryStatus* out_status);
static CompositeClass cpss_db_classify(CPSSDatabase* db, uint64_t n);
static SupportCoverage cpss_db_support_coverage(CPSSDatabase* db, uint64_t n);
static FactorResult cpss_db_factor_auto(CPSSDatabase* db, uint64_t n);

/* ── Batch input item ─────────────────────────────────────────────── */

typedef struct {
    size_t original_index;
    uint64_t n;               /* primary input (single-number commands) */
    uint64_t lo, hi;          /* range input (count-range) */
    /* Result storage (union-like, filled by the executor) */
    int result_int;           /* is-prime result */
    uint64_t result_u64;      /* next-prime, prev-prime, count-range result */
    int status;               /* CPSSQueryStatus */
    double elapsed_us;
    /* Factor result (only for factor-file) */
    FactorResult factor_result;
    /* Classify result */
    CompositeClass classify_result;
    /* Support coverage result */
    SupportCoverage coverage_result;
} BatchItem;

/* ── Input parsing ────────────────────────────────────────────────── */

typedef enum {
    BATCH_FMT_AUTO,
    BATCH_FMT_TXT,
    BATCH_FMT_CSV
} BatchInputFormat;

typedef enum {
    BATCH_OUT_CSV,
    BATCH_OUT_JSONL
} BatchOutputFormat;

/** Read batch input from a file. Returns heap-allocated array of BatchItems. */
static BatchItem* batch_read_input(const char* path, size_t* out_count,
    BatchInputFormat fmt_hint, bool is_range_cmd) {

    FILE* fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "error: cannot open input file: %s\n", path);
        *out_count = 0u;
        return NULL;
    }

    size_t cap = 1024u;
    BatchItem* items = (BatchItem*)xcalloc(cap, sizeof(BatchItem));
    size_t count = 0u;

    char line[4096];
    bool first_line = true;
    bool is_csv = (fmt_hint == BATCH_FMT_CSV);
    int csv_col_n = -1, csv_col_lo = -1, csv_col_hi = -1;
    int csv_col_value = -1, csv_col_number = -1;

    while (fgets(line, sizeof(line), fp)) {
        /* Trim trailing newline/whitespace */
        size_t len = strlen(line);
        while (len > 0u && (line[len - 1u] == '\n' || line[len - 1u] == '\r' ||
               line[len - 1u] == ' ' || line[len - 1u] == '\t'))
            line[--len] = '\0';

        /* Skip blank lines */
        if (len == 0u) continue;

        /* Skip comment lines in txt mode */
        if (line[0] == '#' && !is_csv) continue;

        if (first_line && fmt_hint == BATCH_FMT_AUTO) {
            /* Auto-detect: if first non-empty non-comment line is a number → txt */
            char* end = NULL;
            errno = 0;
            (void)strtoull(line, &end, 10);
            if (errno == 0 && end != line && (*end == '\0' || *end == ',' || *end == ' ')) {
                is_csv = false;
            }
            else {
                is_csv = true;
            }
        }

        if (first_line && is_csv) {
            /* Parse CSV header — find column indices */
            char* tok = line;
            int col = 0;
            while (*tok) {
                /* Skip leading whitespace */
                while (*tok == ' ' || *tok == '\t') ++tok;
                char* comma = strchr(tok, ',');
                char col_name[64];
                size_t cl = comma ? (size_t)(comma - tok) : strlen(tok);
                if (cl >= sizeof(col_name)) cl = sizeof(col_name) - 1u;
                memcpy(col_name, tok, cl);
                col_name[cl] = '\0';
                /* Trim trailing whitespace */
                while (cl > 0u && (col_name[cl - 1u] == ' ' || col_name[cl - 1u] == '\t'))
                    col_name[--cl] = '\0';

                if (_stricmp(col_name, "n") == 0) csv_col_n = col;
                else if (_stricmp(col_name, "number") == 0) csv_col_number = col;
                else if (_stricmp(col_name, "value") == 0) csv_col_value = col;
                else if (_stricmp(col_name, "lo") == 0) csv_col_lo = col;
                else if (_stricmp(col_name, "hi") == 0) csv_col_hi = col;

                ++col;
                if (!comma) break;
                tok = comma + 1;
            }
            first_line = false;
            continue;
        }
        first_line = false;

        /* Grow array if needed */
        if (count == cap) {
            cap *= 2u;
            items = (BatchItem*)xrealloc(items, cap * sizeof(BatchItem));
            memset(items + count, 0, (cap - count) * sizeof(BatchItem));
        }

        BatchItem* item = &items[count];
        item->original_index = count;

        if (is_csv) {
            /* Parse CSV data row */
            char* tok = line;
            int col = 0;
            while (*tok) {
                while (*tok == ' ' || *tok == '\t') ++tok;
                char* comma = strchr(tok, ',');
                char val[128];
                size_t vl = comma ? (size_t)(comma - tok) : strlen(tok);
                if (vl >= sizeof(val)) vl = sizeof(val) - 1u;
                memcpy(val, tok, vl);
                val[vl] = '\0';

                if (is_range_cmd) {
                    if (col == csv_col_lo) item->lo = strtoull(val, NULL, 10);
                    if (col == csv_col_hi) item->hi = strtoull(val, NULL, 10);
                }
                else {
                    int target_col = csv_col_n >= 0 ? csv_col_n :
                                     csv_col_number >= 0 ? csv_col_number :
                                     csv_col_value;
                    if (col == target_col) item->n = strtoull(val, NULL, 10);
                }

                ++col;
                if (!comma) break;
                tok = comma + 1;
            }
        }
        else {
            /* Plain text: one number per line (or "lo hi" / "lo,hi" for range) */
            if (is_range_cmd) {
                char* sep = strchr(line, ',');
                if (!sep) sep = strchr(line, ' ');
                if (!sep) sep = strchr(line, '\t');
                if (sep) {
                    *sep = '\0';
                    item->lo = strtoull(line, NULL, 10);
                    item->hi = strtoull(sep + 1, NULL, 10);
                }
                else {
                    item->lo = strtoull(line, NULL, 10);
                    item->hi = item->lo;
                }
            }
            else {
                item->n = strtoull(line, NULL, 10);
            }
        }
        ++count;
    }

    fclose(fp);
    *out_count = count;
    return items;
}

/* ── Locality sort ────────────────────────────────────────────────── */

static int batch_cmp_by_n(const void* a, const void* b) {
    uint64_t na = ((const BatchItem*)a)->n;
    uint64_t nb = ((const BatchItem*)b)->n;
    return (na > nb) - (na < nb);
}

static int batch_cmp_by_original(const void* a, const void* b) {
    size_t ia = ((const BatchItem*)a)->original_index;
    size_t ib = ((const BatchItem*)b)->original_index;
    return (ia > ib) - (ia < ib);
}

/* ── Output writing ───────────────────────────────────────────────── */

static void batch_write_csv_header_isprime(FILE* fp) {
    fprintf(fp, "original_index,n,is_prime,status,elapsed_us\n");
}
static void batch_write_csv_row_isprime(FILE* fp, const BatchItem* item) {
    fprintf(fp, "%zu,%" PRIu64 ",%d,%s,%.1f\n",
        item->original_index, item->n, item->result_int,
        cpss_status_name((CPSSQueryStatus)item->status), item->elapsed_us);
}
static void batch_write_jsonl_isprime(FILE* fp, const BatchItem* item) {
    fprintf(fp, "{\"original_index\":%zu,\"n\":%" PRIu64 ",\"is_prime\":%d,\"status\":\"%s\",\"elapsed_us\":%.1f}\n",
        item->original_index, item->n, item->result_int,
        cpss_status_name((CPSSQueryStatus)item->status), item->elapsed_us);
}

static void batch_write_csv_header_u64(FILE* fp, const char* result_name) {
    fprintf(fp, "original_index,n,%s,status,elapsed_us\n", result_name);
}
static void batch_write_csv_row_u64(FILE* fp, const BatchItem* item) {
    fprintf(fp, "%zu,%" PRIu64 ",%" PRIu64 ",%s,%.1f\n",
        item->original_index, item->n, item->result_u64,
        cpss_status_name((CPSSQueryStatus)item->status), item->elapsed_us);
}
static void batch_write_jsonl_u64(FILE* fp, const BatchItem* item, const char* result_name) {
    fprintf(fp, "{\"original_index\":%zu,\"n\":%" PRIu64 ",\"%s\":%" PRIu64 ",\"status\":\"%s\",\"elapsed_us\":%.1f}\n",
        item->original_index, item->n, result_name, item->result_u64,
        cpss_status_name((CPSSQueryStatus)item->status), item->elapsed_us);
}

static void batch_write_csv_header_factor(FILE* fp) {
    fprintf(fp, "original_index,n,method,fully_factored,cofactor,factor_count,factors,status,elapsed_us\n");
}
static void batch_write_csv_row_factor(FILE* fp, const BatchItem* item) {
    const FactorResult* fr = &item->factor_result;
    fprintf(fp, "%zu,%" PRIu64 ",%s,%s,%" PRIu64 ",%zu,",
        item->original_index, fr->n, fr->method,
        fr->fully_factored ? "true" : "false", fr->cofactor, fr->factor_count);
    /* factors as "p1^e1*p2^e2" */
    for (size_t i = 0u; i < fr->factor_count; ++i) {
        if (i > 0u) fprintf(fp, "*");
        if (fr->exponents[i] > 1u)
            fprintf(fp, "%" PRIu64 "^%" PRIu64, fr->factors[i], fr->exponents[i]);
        else
            fprintf(fp, "%" PRIu64, fr->factors[i]);
    }
    fprintf(fp, ",%s,%.1f\n",
        cpss_status_name((CPSSQueryStatus)fr->status), fr->time_seconds * 1e6);
}
static void batch_write_jsonl_factor(FILE* fp, const BatchItem* item) {
    const FactorResult* fr = &item->factor_result;
    fprintf(fp, "{\"original_index\":%zu,\"n\":%" PRIu64 ",\"method\":\"%s\","
        "\"fully_factored\":%s,\"cofactor\":%" PRIu64 ",\"factor_count\":%zu,\"factors\":[",
        item->original_index, fr->n, fr->method,
        fr->fully_factored ? "true" : "false", fr->cofactor, fr->factor_count);
    for (size_t i = 0u; i < fr->factor_count; ++i) {
        if (i > 0u) fprintf(fp, ",");
        fprintf(fp, "{\"p\":%" PRIu64 ",\"e\":%" PRIu64 "}", fr->factors[i], fr->exponents[i]);
    }
    fprintf(fp, "],\"status\":\"%s\",\"elapsed_us\":%.1f}\n",
        cpss_status_name((CPSSQueryStatus)fr->status), fr->time_seconds * 1e6);
}

static void batch_write_csv_header_classify(FILE* fp) {
    fprintf(fp, "original_index,n,is_even,is_perfect_square,is_prime_power,is_prime,has_small_factor,smallest_factor,elapsed_us\n");
}
static void batch_write_csv_row_classify(FILE* fp, const BatchItem* item) {
    const CompositeClass* c = &item->classify_result;
    fprintf(fp, "%zu,%" PRIu64 ",%s,%s,%s,%s,%s,%" PRIu64 ",%.1f\n",
        item->original_index, c->n,
        c->is_even ? "true" : "false",
        c->is_perfect_square ? "true" : "false",
        c->is_prime_power ? "true" : "false",
        c->is_prime ? "true" : "false",
        c->has_small_factor ? "true" : "false",
        c->smallest_factor, item->elapsed_us);
}

static void batch_write_csv_header_countrange(FILE* fp) {
    fprintf(fp, "original_index,lo,hi,count,status,elapsed_us\n");
}
static void batch_write_csv_row_countrange(FILE* fp, const BatchItem* item) {
    fprintf(fp, "%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,%.1f\n",
        item->original_index, item->lo, item->hi, item->result_u64,
        cpss_status_name((CPSSQueryStatus)item->status), item->elapsed_us);
}

/* ── Batch executors ──────────────────────────────────────────────── */

typedef enum {
    BATCH_CMD_IS_PRIME,
    BATCH_CMD_NEXT_PRIME,
    BATCH_CMD_PREV_PRIME,
    BATCH_CMD_CLASSIFY,
    BATCH_CMD_SUPPORT_COVERAGE,
    BATCH_CMD_COUNT_RANGE,
    BATCH_CMD_FACTOR
} BatchCommand;

/**
 * Execute a batch of queries against a stream or multi-shard DB.
 * Items are sorted by n for locality, then restored to original order.
 */
static void batch_execute(BatchItem* items, size_t count, BatchCommand cmd,
    CPSSStream* single_s, CPSSDatabase* multi_db) {

    /* Sort by n for locality (or by lo for range commands) */
    if (cmd != BATCH_CMD_COUNT_RANGE) {
        qsort(items, count, sizeof(BatchItem), batch_cmp_by_n);
    }

    for (size_t i = 0u; i < count; ++i) {
        BatchItem* item = &items[i];
        double t0 = now_sec();

        switch (cmd) {
        case BATCH_CMD_IS_PRIME:
            if (multi_db) {
                item->result_int = cpss_db_is_prime(multi_db, item->n);
            }
            else {
                item->result_int = cpss_is_prime(single_s, item->n);
            }
            item->status = (item->result_int >= 0) ? (int)CPSS_OK : (int)CPSS_OUT_OF_RANGE;
            break;

        case BATCH_CMD_NEXT_PRIME: {
            CPSSQueryStatus qs;
            if (multi_db) item->result_u64 = cpss_db_next_prime_s(multi_db, item->n, &qs);
            else item->result_u64 = cpss_next_prime_s(single_s, item->n, &qs);
            item->status = (int)qs;
            break;
        }
        case BATCH_CMD_PREV_PRIME: {
            CPSSQueryStatus qs;
            if (multi_db) item->result_u64 = cpss_db_prev_prime_s(multi_db, item->n, &qs);
            else item->result_u64 = cpss_prev_prime_s(single_s, item->n, &qs);
            item->status = (int)qs;
            break;
        }
        case BATCH_CMD_CLASSIFY: {
            item->classify_result = multi_db ? cpss_db_classify(multi_db, item->n) : cpss_classify(single_s, item->n);
            item->status = (int)CPSS_OK;
            break;
        }
        case BATCH_CMD_SUPPORT_COVERAGE: {
            item->coverage_result = multi_db ? cpss_db_support_coverage(multi_db, item->n) : cpss_support_coverage(single_s, item->n);
            item->status = (int)CPSS_OK;
            break;
        }
        case BATCH_CMD_COUNT_RANGE:
            if (multi_db) {
                item->result_u64 = cpss_db_count_range(multi_db, item->lo, item->hi);
            }
            else {
                item->result_u64 = cpss_count_range_hot(single_s, item->lo, item->hi);
            }
            item->status = (int)CPSS_OK;
            break;

        case BATCH_CMD_FACTOR: {
            item->factor_result = multi_db ? cpss_db_factor_auto(multi_db, item->n) : cpss_factor_auto(single_s, item->n);
            item->status = item->factor_result.status;
            break;
        }
        }

        double t1 = now_sec();
        item->elapsed_us = (t1 - t0) * 1e6;
    }

    /* Restore original order */
    qsort(items, count, sizeof(BatchItem), batch_cmp_by_original);
}

/**
 * Run a full batch pipeline: read input, execute, write output.
 */
static void batch_run_pipeline(const char* input_path, const char* output_path,
    BatchCommand cmd, BatchInputFormat in_fmt, BatchOutputFormat out_fmt,
    CPSSStream* single_s, CPSSDatabase* multi_db) {

    bool is_range = (cmd == BATCH_CMD_COUNT_RANGE);

    size_t count = 0u;
    BatchItem* items = batch_read_input(input_path, &count, in_fmt, is_range);
    if (!items || count == 0u) {
        printf("No items read from %s\n", input_path);
        free(items);
        return;
    }

    printf("  read %zu items from %s\n", count, input_path);

    double t0 = now_sec();
    batch_execute(items, count, cmd, single_s, multi_db);
    double t1 = now_sec();

    printf("  executed %zu queries in %.3fs (%.0f qps)\n",
        count, t1 - t0, (t1 - t0) > 0.0 ? (double)count / (t1 - t0) : 0.0);

    /* Write output */
    FILE* out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "error: cannot create output file: %s\n", output_path);
        free(items);
        return;
    }

    bool csv = (out_fmt == BATCH_OUT_CSV);

    /* Write header (CSV only) */
    if (csv) {
        switch (cmd) {
        case BATCH_CMD_IS_PRIME: batch_write_csv_header_isprime(out); break;
        case BATCH_CMD_NEXT_PRIME: batch_write_csv_header_u64(out, "next_prime"); break;
        case BATCH_CMD_PREV_PRIME: batch_write_csv_header_u64(out, "prev_prime"); break;
        case BATCH_CMD_CLASSIFY: batch_write_csv_header_classify(out); break;
        case BATCH_CMD_SUPPORT_COVERAGE: batch_write_csv_header_u64(out, "sqrt_n"); break;
        case BATCH_CMD_COUNT_RANGE: batch_write_csv_header_countrange(out); break;
        case BATCH_CMD_FACTOR: batch_write_csv_header_factor(out); break;
        }
    }

    /* Write rows */
    for (size_t i = 0u; i < count; ++i) {
        const BatchItem* item = &items[i];
        switch (cmd) {
        case BATCH_CMD_IS_PRIME:
            if (csv) batch_write_csv_row_isprime(out, item);
            else batch_write_jsonl_isprime(out, item);
            break;
        case BATCH_CMD_NEXT_PRIME:
            if (csv) batch_write_csv_row_u64(out, item);
            else batch_write_jsonl_u64(out, item, "next_prime");
            break;
        case BATCH_CMD_PREV_PRIME:
            if (csv) batch_write_csv_row_u64(out, item);
            else batch_write_jsonl_u64(out, item, "prev_prime");
            break;
        case BATCH_CMD_CLASSIFY:
            if (csv) batch_write_csv_row_classify(out, item);
            break;
        case BATCH_CMD_SUPPORT_COVERAGE:
            if (csv) batch_write_csv_row_u64(out, item);
            break;
        case BATCH_CMD_COUNT_RANGE:
            if (csv) batch_write_csv_row_countrange(out, item);
            break;
        case BATCH_CMD_FACTOR:
            if (csv) batch_write_csv_row_factor(out, item);
            else batch_write_jsonl_factor(out, item);
            break;
        }
    }

    fclose(out);
    printf("  wrote %zu results to %s (%s)\n", count, output_path, csv ? "csv" : "jsonl");

    free(items);
}

/** Parse --input-format and --output-format from argv. */
static void batch_parse_format_args(int argc, char** argv, int arg_start,
    BatchInputFormat* in_fmt, BatchOutputFormat* out_fmt) {
    *in_fmt = BATCH_FMT_AUTO;
    *out_fmt = BATCH_OUT_CSV;
    for (int i = arg_start; i < argc; ++i) {
        if (strcmp(argv[i], "--input-format") == 0 && i + 1 < argc) {
            ++i;
            if (strcmp(argv[i], "txt") == 0) *in_fmt = BATCH_FMT_TXT;
            else if (strcmp(argv[i], "csv") == 0) *in_fmt = BATCH_FMT_CSV;
            else *in_fmt = BATCH_FMT_AUTO;
        }
        else if (strcmp(argv[i], "--output-format") == 0 && i + 1 < argc) {
            ++i;
            if (strcmp(argv[i], "jsonl") == 0) *out_fmt = BATCH_OUT_JSONL;
            else *out_fmt = BATCH_OUT_CSV;
        }
    }
}
