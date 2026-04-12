/**
 * nql_matrix_gf2.c - GF(2) binary matrix operations for NQL.
 * Part of the CPSS Viewer amalgamation.
 *
 * A GF(2) matrix is stored as an array of row-arrays. Each row is an array
 * of uint64 words where bit j of word[j/64] represents column j.
 * This matches the C QS implementation's parity matrix layout.
 *
 * Functions: MATRIX_GF2_CREATE, MATRIX_GF2_SET_BIT, MATRIX_GF2_GET_BIT,
 *            MATRIX_GF2_XOR_ROW, MATRIX_GF2_ELIMINATE, MATRIX_GF2_NULL_SPACE
 */

/** MATRIX_GF2_CREATE(rows, cols) — create a rows×cols binary matrix (all zeros).
 *  Returns an array of `rows` row-arrays, each containing ceil(cols/64) uint64 words. */
static NqlValue nql_fn_matrix_gf2_create(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    uint32_t rows = (uint32_t)nql_val_as_int(&args[0]);
    uint32_t cols = (uint32_t)nql_val_as_int(&args[1]);
    if (rows == 0u || cols == 0u) return nql_val_null();
    if (rows > 10000u) rows = 10000u;
    uint32_t words = (cols + 63u) / 64u;

    NqlArray* matrix = nql_array_alloc(rows);
    if (!matrix) return nql_val_null();
    for (uint32_t r = 0u; r < rows; ++r) {
        NqlArray* row = nql_array_alloc(words);
        if (!row) return nql_val_null();
        for (uint32_t w = 0u; w < words; ++w)
            nql_array_push(row, nql_val_int(0));
        nql_array_push(matrix, nql_val_array(row));
    }
    return nql_val_array(matrix);
}

/** MATRIX_GF2_SET_BIT(matrix, row, col) — set bit at (row, col) to 1. Mutates in place. */
static NqlValue nql_fn_matrix_gf2_set_bit(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    uint32_t row = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t col = (uint32_t)nql_val_as_int(&args[2]);
    NqlArray* matrix = args[0].v.array;
    if (row >= matrix->length) return args[0];
    if (matrix->items[row].type != NQL_VAL_ARRAY || !matrix->items[row].v.array) return args[0];
    NqlArray* row_arr = matrix->items[row].v.array;
    uint32_t word_idx = col / 64u;
    uint32_t bit_idx = col % 64u;
    if (word_idx >= row_arr->length) return args[0];
    int64_t word = nql_val_as_int(&row_arr->items[word_idx]);
    word |= ((int64_t)1 << bit_idx);
    row_arr->items[word_idx] = nql_val_int(word);
    return args[0];
}

/** MATRIX_GF2_FLIP_BIT(matrix, row, col) — XOR bit at (row, col). Mutates in place. */
static NqlValue nql_fn_matrix_gf2_flip_bit(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    uint32_t row = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t col = (uint32_t)nql_val_as_int(&args[2]);
    NqlArray* matrix = args[0].v.array;
    if (row >= matrix->length) return args[0];
    if (matrix->items[row].type != NQL_VAL_ARRAY || !matrix->items[row].v.array) return args[0];
    NqlArray* row_arr = matrix->items[row].v.array;
    uint32_t word_idx = col / 64u;
    uint32_t bit_idx = col % 64u;
    if (word_idx >= row_arr->length) return args[0];
    int64_t word = nql_val_as_int(&row_arr->items[word_idx]);
    word ^= ((int64_t)1 << bit_idx);
    row_arr->items[word_idx] = nql_val_int(word);
    return args[0];
}

/** MATRIX_GF2_GET_BIT(matrix, row, col) — return bit at (row, col) as 0 or 1. */
static NqlValue nql_fn_matrix_gf2_get_bit(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_int(0);
    uint32_t row = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t col = (uint32_t)nql_val_as_int(&args[2]);
    NqlArray* matrix = args[0].v.array;
    if (row >= matrix->length) return nql_val_int(0);
    if (matrix->items[row].type != NQL_VAL_ARRAY || !matrix->items[row].v.array) return nql_val_int(0);
    NqlArray* row_arr = matrix->items[row].v.array;
    uint32_t word_idx = col / 64u;
    uint32_t bit_idx = col % 64u;
    if (word_idx >= row_arr->length) return nql_val_int(0);
    int64_t word = nql_val_as_int(&row_arr->items[word_idx]);
    return nql_val_int((word >> bit_idx) & 1);
}

/** MATRIX_GF2_XOR_ROW(matrix, dst_row, src_row) — XOR src into dst. Mutates in place. */
static NqlValue nql_fn_matrix_gf2_xor_row(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    uint32_t dst = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t src = (uint32_t)nql_val_as_int(&args[2]);
    NqlArray* matrix = args[0].v.array;
    if (dst >= matrix->length || src >= matrix->length) return args[0];
    NqlArray* dst_row = matrix->items[dst].v.array;
    NqlArray* src_row = matrix->items[src].v.array;
    if (!dst_row || !src_row) return args[0];
    uint32_t words = dst_row->length < src_row->length ? dst_row->length : src_row->length;
    for (uint32_t w = 0u; w < words; ++w) {
        int64_t d = nql_val_as_int(&dst_row->items[w]);
        int64_t s = nql_val_as_int(&src_row->items[w]);
        dst_row->items[w] = nql_val_int(d ^ s);
    }
    return args[0];
}

/** MATRIX_GF2_ELIMINATE(matrix, rows, cols) — Gaussian elimination over GF(2).
 *  Mutates matrix in place. Returns an array of pivot column indices (one per row that has a pivot).
 *  Non-pivot rows are potential null-space generators. */
static NqlValue nql_fn_matrix_gf2_eliminate(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    uint32_t nrows = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t ncols = (uint32_t)nql_val_as_int(&args[2]);
    NqlArray* matrix = args[0].v.array;
    if (nrows > matrix->length) nrows = matrix->length;

    NqlArray* pivots = nql_array_alloc(nrows);
    if (!pivots) return nql_val_null();
    /* Fill with -1 (no pivot) */
    for (uint32_t i = 0u; i < nrows; ++i)
        nql_array_push(pivots, nql_val_int(-1));

    uint32_t pivot_row = 0u;
    for (uint32_t col = 0u; col < ncols && pivot_row < nrows; ++col) {
        uint32_t word_idx = col / 64u;
        uint32_t bit_idx = col % 64u;

        /* Find pivot row */
        uint32_t found = UINT32_MAX;
        for (uint32_t r = pivot_row; r < nrows; ++r) {
            NqlArray* row = matrix->items[r].v.array;
            if (!row || word_idx >= row->length) continue;
            int64_t word = nql_val_as_int(&row->items[word_idx]);
            if ((word >> bit_idx) & 1) { found = r; break; }
        }
        if (found == UINT32_MAX) continue;

        /* Swap rows if needed */
        if (found != pivot_row) {
            NqlValue tmp = matrix->items[pivot_row];
            matrix->items[pivot_row] = matrix->items[found];
            matrix->items[found] = tmp;
        }

        /* Record pivot */
        pivots->items[pivot_row] = nql_val_int((int64_t)col);

        /* Eliminate column in all other rows */
        for (uint32_t r = 0u; r < nrows; ++r) {
            if (r == pivot_row) continue;
            NqlArray* row = matrix->items[r].v.array;
            if (!row || word_idx >= row->length) continue;
            int64_t word = nql_val_as_int(&row->items[word_idx]);
            if (!((word >> bit_idx) & 1)) continue;
            /* XOR pivot row into this row */
            NqlArray* prow = matrix->items[pivot_row].v.array;
            uint32_t words = row->length < prow->length ? row->length : prow->length;
            for (uint32_t w = 0u; w < words; ++w) {
                int64_t d = nql_val_as_int(&row->items[w]);
                int64_t s = nql_val_as_int(&prow->items[w]);
                row->items[w] = nql_val_int(d ^ s);
            }
        }
        ++pivot_row;
    }
    return nql_val_array(pivots);
}

/** MATRIX_GF2_NULL_SPACE(matrix, pivots, rows, cols) — extract null-space vectors.
 *  Returns an array of arrays, where each inner array is a row-index combination
 *  (which original rows to XOR together to get a zero vector). */
static NqlValue nql_fn_matrix_gf2_null_space(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* matrix = args[0].v.array;
    NqlArray* pivots = args[1].v.array;
    uint32_t nrows = (uint32_t)nql_val_as_int(&args[2]);
    uint32_t ncols = (uint32_t)nql_val_as_int(&args[3]);
    (void)ncols;

    if (nrows > matrix->length) nrows = matrix->length;
    if (nrows > pivots->length) nrows = pivots->length;

    /* Rows with pivot == -1 are null-space generators.
     * For each such row r, the null-space vector is: row r itself XORed
     * back against pivot rows. We return the indices of rows that combine
     * to form each null-space vector. */
    NqlArray* vectors = nql_array_alloc(64u);
    if (!vectors) return nql_val_null();

    for (uint32_t r = 0u; r < nrows; ++r) {
        if (nql_val_as_int(&pivots->items[r]) >= 0) continue; /* has pivot, skip */
        /* This row is a null-space generator.
         * The dependency includes row r plus all pivot rows that were used
         * to reduce it. Since we did full elimination, the remaining bits
         * in this row indicate which pivot columns (and thus which pivot rows)
         * are in the dependency. */
        NqlArray* dep = nql_array_alloc(32u);
        if (!dep) continue;
        nql_array_push(dep, nql_val_int((int64_t)r));
        /* Check which pivot columns have bits set in this row */
        NqlArray* row = matrix->items[r].v.array;
        if (row) {
            for (uint32_t pr = 0u; pr < nrows; ++pr) {
                int64_t pcol = nql_val_as_int(&pivots->items[pr]);
                if (pcol < 0) continue;
                uint32_t word_idx = (uint32_t)pcol / 64u;
                uint32_t bit_idx = (uint32_t)pcol % 64u;
                if (word_idx < row->length) {
                    int64_t word = nql_val_as_int(&row->items[word_idx]);
                    if ((word >> bit_idx) & 1) {
                        nql_array_push(dep, nql_val_int((int64_t)pr));
                    }
                }
            }
        }
        nql_array_push(vectors, nql_val_array(dep));
    }
    return nql_val_array(vectors);
}
