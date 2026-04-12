/**
 * nql_sparse_matrix.c - NQL Sparse Matrix type (CSR format).
 * Part of the CPSS Viewer amalgamation.
 *
 * Compressed Sparse Row storage with arithmetic, transpose,
 * matrix-vector multiply, and conjugate gradient solver.
 */

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * NqlSparseMatrix STRUCT (CSR)
 * ====================================================================== */

struct NqlSparseMatrix_s {
    uint32_t rows, cols;
    uint32_t nnz;           /* number of non-zeros */
    uint32_t capacity;
    double*   values;       /* nnz values */
    uint32_t* col_idx;      /* nnz column indices */
    uint32_t* row_ptr;      /* rows+1 row pointers */
};

static void nql_sparse_free(NqlSparseMatrix* m) {
    if (!m) return;
    free(m->values); free(m->col_idx); free(m->row_ptr); free(m);
}

static NqlSparseMatrix* nql_sparse_alloc(uint32_t rows, uint32_t cols, uint32_t cap) {
    NqlSparseMatrix* m = (NqlSparseMatrix*)malloc(sizeof(NqlSparseMatrix));
    if (!m) return NULL;
    m->rows = rows; m->cols = cols; m->nnz = 0; m->capacity = cap;
    m->values = (double*)calloc(cap, sizeof(double));
    m->col_idx = (uint32_t*)calloc(cap, sizeof(uint32_t));
    m->row_ptr = (uint32_t*)calloc(rows + 1, sizeof(uint32_t));
    if (!m->values || !m->col_idx || !m->row_ptr) { nql_sparse_free(m); return NULL; }
    return m;
}

static NqlValue nql_val_sparse(NqlSparseMatrix* m) {
    NqlValue v; memset(&v, 0, sizeof(v));
    v.type = NQL_VAL_SPARSE_MATRIX; v.v.sparse_matrix = m;
    return v;
}

/* ======================================================================
 * NQL FUNCTIONS
 * ====================================================================== */

/** SPARSE(rows, cols) */
static NqlValue nql_fn_sparse_create(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    uint32_t r = (uint32_t)nql_val_as_int(&args[0]);
    uint32_t c = (uint32_t)nql_val_as_int(&args[1]);
    if (r == 0 || c == 0 || r > 4096 || c > 4096) return nql_val_null();
    NqlSparseMatrix* m = nql_sparse_alloc(r, c, 64);
    return m ? nql_val_sparse(m) : nql_val_null();
}

/** SPARSE_SET(m, row, col, val) -- returns new sparse matrix */
static NqlValue nql_fn_sparse_set(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD4(args);
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    NqlSparseMatrix* src = args[0].v.sparse_matrix;
    uint32_t r = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t c = (uint32_t)nql_val_as_int(&args[2]);
    double val = nql_val_as_float(&args[3]);
    if (r >= src->rows || c >= src->cols) return nql_val_null();

    /* Build new triplet list, insert/replace, then rebuild CSR */
    uint32_t new_cap = src->nnz + 1;
    NqlSparseMatrix* m = nql_sparse_alloc(src->rows, src->cols, new_cap);
    if (!m) return nql_val_null();

    bool replaced = false;
    uint32_t idx = 0;
    for (uint32_t i = 0; i < src->rows; i++) {
        m->row_ptr[i] = idx;
        for (uint32_t j = src->row_ptr[i]; j < src->row_ptr[i + 1]; j++) {
            if (i == r && src->col_idx[j] == c) {
                if (val != 0.0) { m->values[idx] = val; m->col_idx[idx] = c; idx++; }
                replaced = true;
            } else {
                m->values[idx] = src->values[j]; m->col_idx[idx] = src->col_idx[j]; idx++;
            }
        }
        if (i == r && !replaced && val != 0.0) {
            /* Insert in sorted position */
            uint32_t ins = m->row_ptr[i];
            while (ins < idx && m->col_idx[ins] < c) ins++;
            memmove(&m->values[ins + 1], &m->values[ins], (idx - ins) * sizeof(double));
            memmove(&m->col_idx[ins + 1], &m->col_idx[ins], (idx - ins) * sizeof(uint32_t));
            m->values[ins] = val; m->col_idx[ins] = c; idx++;
            replaced = true;
        }
    }
    m->row_ptr[src->rows] = idx;
    m->nnz = idx;
    return nql_val_sparse(m);
}

/** SPARSE_GET(m, row, col) */
static NqlValue nql_fn_sparse_get(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    NqlSparseMatrix* m = args[0].v.sparse_matrix;
    uint32_t r = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t c = (uint32_t)nql_val_as_int(&args[2]);
    if (r >= m->rows || c >= m->cols) return nql_val_null();
    for (uint32_t j = m->row_ptr[r]; j < m->row_ptr[r + 1]; j++)
        if (m->col_idx[j] == c) return nql_val_float(m->values[j]);
    return nql_val_float(0.0);
}

/** SPARSE_NNZ(m) */
static NqlValue nql_fn_sparse_nnz(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    return nql_val_int((int64_t)args[0].v.sparse_matrix->nnz);
}

/** SPARSE_ROWS(m) / SPARSE_COLS(m) */
static NqlValue nql_fn_sparse_rows(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    return nql_val_int((int64_t)args[0].v.sparse_matrix->rows);
}
static NqlValue nql_fn_sparse_cols(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    return nql_val_int((int64_t)args[0].v.sparse_matrix->cols);
}

/** SPARSE_MUL_VEC(m, vec_array) -- Ax */
static NqlValue nql_fn_sparse_mul_vec(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlSparseMatrix* m = args[0].v.sparse_matrix;
    NqlArray* x = args[1].v.array;
    if (x->length != m->cols) return nql_val_null();
    NqlArray* result = nql_array_alloc(m->rows);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < m->rows; i++) {
        double sum = 0.0;
        for (uint32_t j = m->row_ptr[i]; j < m->row_ptr[i + 1]; j++)
            sum += m->values[j] * nql_val_as_float(&x->items[m->col_idx[j]]);
        nql_array_push(result, nql_val_float(sum));
    }
    return nql_val_array(result);
}

/** SPARSE_ADD(a, b) */
static NqlValue nql_fn_sparse_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    if (args[1].type != NQL_VAL_SPARSE_MATRIX || !args[1].v.sparse_matrix) return nql_val_null();
    NqlSparseMatrix* a = args[0].v.sparse_matrix;
    NqlSparseMatrix* b = args[1].v.sparse_matrix;
    if (a->rows != b->rows || a->cols != b->cols) return nql_val_null();
    NqlSparseMatrix* r = nql_sparse_alloc(a->rows, a->cols, a->nnz + b->nnz);
    if (!r) return nql_val_null();
    uint32_t idx = 0;
    for (uint32_t i = 0; i < a->rows; i++) {
        r->row_ptr[i] = idx;
        uint32_t ja = a->row_ptr[i], jb = b->row_ptr[i];
        uint32_t ea = a->row_ptr[i + 1], eb = b->row_ptr[i + 1];
        while (ja < ea && jb < eb) {
            if (a->col_idx[ja] < b->col_idx[jb]) {
                r->values[idx] = a->values[ja]; r->col_idx[idx] = a->col_idx[ja]; ja++; idx++;
            } else if (a->col_idx[ja] > b->col_idx[jb]) {
                r->values[idx] = b->values[jb]; r->col_idx[idx] = b->col_idx[jb]; jb++; idx++;
            } else {
                double s = a->values[ja] + b->values[jb];
                if (s != 0.0) { r->values[idx] = s; r->col_idx[idx] = a->col_idx[ja]; idx++; }
                ja++; jb++;
            }
        }
        while (ja < ea) { r->values[idx] = a->values[ja]; r->col_idx[idx] = a->col_idx[ja]; ja++; idx++; }
        while (jb < eb) { r->values[idx] = b->values[jb]; r->col_idx[idx] = b->col_idx[jb]; jb++; idx++; }
    }
    r->row_ptr[a->rows] = idx; r->nnz = idx;
    return nql_val_sparse(r);
}

/** SPARSE_SCALE(m, scalar) */
static NqlValue nql_fn_sparse_scale(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    NqlSparseMatrix* src = args[0].v.sparse_matrix;
    double s = nql_val_as_float(&args[1]);
    NqlSparseMatrix* r = nql_sparse_alloc(src->rows, src->cols, src->nnz);
    if (!r) return nql_val_null();
    memcpy(r->row_ptr, src->row_ptr, (src->rows + 1) * sizeof(uint32_t));
    memcpy(r->col_idx, src->col_idx, src->nnz * sizeof(uint32_t));
    for (uint32_t i = 0; i < src->nnz; i++) r->values[i] = src->values[i] * s;
    r->nnz = src->nnz;
    return nql_val_sparse(r);
}

/** SPARSE_TRANSPOSE(m) */
static NqlValue nql_fn_sparse_transpose(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    NqlSparseMatrix* src = args[0].v.sparse_matrix;
    NqlSparseMatrix* r = nql_sparse_alloc(src->cols, src->rows, src->nnz);
    if (!r) return nql_val_null();
    /* Count entries per column */
    uint32_t* counts = (uint32_t*)calloc(src->cols, sizeof(uint32_t));
    if (!counts) { nql_sparse_free(r); return nql_val_null(); }
    for (uint32_t i = 0; i < src->nnz; i++) counts[src->col_idx[i]]++;
    r->row_ptr[0] = 0;
    for (uint32_t i = 0; i < src->cols; i++) r->row_ptr[i + 1] = r->row_ptr[i] + counts[i];
    memset(counts, 0, src->cols * sizeof(uint32_t));
    for (uint32_t i = 0; i < src->rows; i++) {
        for (uint32_t j = src->row_ptr[i]; j < src->row_ptr[i + 1]; j++) {
            uint32_t c = src->col_idx[j];
            uint32_t pos = r->row_ptr[c] + counts[c]++;
            r->values[pos] = src->values[j]; r->col_idx[pos] = i;
        }
    }
    r->nnz = src->nnz;
    free(counts);
    return nql_val_sparse(r);
}

/** SPARSE_TO_DENSE(m) -- convert to dense matrix */
static NqlValue nql_fn_sparse_to_dense(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    NqlSparseMatrix* m = args[0].v.sparse_matrix;
    NqlArray* data = nql_array_alloc(m->rows * m->cols);
    if (!data) return nql_val_null();
    for (uint32_t i = 0; i < m->rows * m->cols; i++) nql_array_push(data, nql_val_float(0.0));
    for (uint32_t i = 0; i < m->rows; i++)
        for (uint32_t j = m->row_ptr[i]; j < m->row_ptr[i + 1]; j++)
            data->items[i * m->cols + m->col_idx[j]] = nql_val_float(m->values[j]);
    /* Return [rows, cols, flat_data] */
    NqlArray* result = nql_array_alloc(3);
    if (!result) return nql_val_null();
    nql_array_push(result, nql_val_int((int64_t)m->rows));
    nql_array_push(result, nql_val_int((int64_t)m->cols));
    nql_array_push(result, nql_val_array(data));
    return nql_val_array(result);
}

/** SPARSE_SOLVE_CG(A, b_array [, tol [, max_iter]]) -- Conjugate Gradient */
static NqlValue nql_fn_sparse_solve_cg(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (args[0].type != NQL_VAL_SPARSE_MATRIX || !args[0].v.sparse_matrix) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlSparseMatrix* A = args[0].v.sparse_matrix;
    NqlArray* b = args[1].v.array;
    if (A->rows != A->cols || b->length != A->rows) return nql_val_null();
    uint32_t n = A->rows;
    double tol = (argc >= 3) ? nql_val_as_float(&args[2]) : 1e-10;
    int max_iter = (argc >= 4) ? (int)nql_val_as_int(&args[3]) : (int)n * 2;

    double* x = (double*)calloc(n, sizeof(double));
    double* r = (double*)malloc(n * sizeof(double));
    double* p = (double*)malloc(n * sizeof(double));
    double* Ap = (double*)malloc(n * sizeof(double));
    if (!x || !r || !p || !Ap) { free(x); free(r); free(p); free(Ap); return nql_val_null(); }

    /* r = b - Ax (x=0, so r=b) */
    for (uint32_t i = 0; i < n; i++) { r[i] = nql_val_as_float(&b->items[i]); p[i] = r[i]; }
    double rsold = 0; for (uint32_t i = 0; i < n; i++) rsold += r[i] * r[i];

    for (int iter = 0; iter < max_iter; iter++) {
        /* Ap = A * p */
        for (uint32_t i = 0; i < n; i++) {
            Ap[i] = 0;
            for (uint32_t j = A->row_ptr[i]; j < A->row_ptr[i + 1]; j++)
                Ap[i] += A->values[j] * p[A->col_idx[j]];
        }
        double pAp = 0; for (uint32_t i = 0; i < n; i++) pAp += p[i] * Ap[i];
        if (fabs(pAp) < 1e-30) break;
        double alpha = rsold / pAp;
        double rsnew = 0;
        for (uint32_t i = 0; i < n; i++) { x[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; rsnew += r[i] * r[i]; }
        if (sqrt(rsnew) < tol) break;
        double beta = rsnew / rsold;
        for (uint32_t i = 0; i < n; i++) p[i] = r[i] + beta * p[i];
        rsold = rsnew;
    }
    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(x); free(r); free(p); free(Ap); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) nql_array_push(result, nql_val_float(x[i]));
    free(x); free(r); free(p); free(Ap);
    return nql_val_array(result);
}

/** SPARSE_FROM_DENSE(matrix) -- convert dense NqlMatrix to sparse */
static NqlValue nql_fn_sparse_from_dense(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* dm = args[0].v.matrix;
    uint32_t nnz = 0;
    for (uint32_t i = 0; i < dm->rows * dm->cols; i++) if (dm->data[i] != 0.0) nnz++;
    NqlSparseMatrix* m = nql_sparse_alloc(dm->rows, dm->cols, nnz > 0 ? nnz : 1);
    if (!m) return nql_val_null();
    uint32_t idx = 0;
    for (uint32_t i = 0; i < dm->rows; i++) {
        m->row_ptr[i] = idx;
        for (uint32_t j = 0; j < dm->cols; j++) {
            double v = dm->data[i * dm->cols + j];
            if (v != 0.0) { m->values[idx] = v; m->col_idx[idx] = j; idx++; }
        }
    }
    m->row_ptr[dm->rows] = idx; m->nnz = idx;
    return nql_val_sparse(m);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_sparse_matrix_register_functions(void) {
    nql_register_func("SPARSE",            nql_fn_sparse_create,     2, 2, "Create empty sparse matrix (rows, cols)");
    nql_register_func("SPARSE_SET",        nql_fn_sparse_set,        4, 4, "Set element, returns new sparse matrix");
    nql_register_func("SPARSE_GET",        nql_fn_sparse_get,        3, 3, "Get element (0 if absent)");
    nql_register_func("SPARSE_NNZ",        nql_fn_sparse_nnz,        1, 1, "Non-zero count");
    nql_register_func("SPARSE_ROWS",       nql_fn_sparse_rows,       1, 1, "Row count");
    nql_register_func("SPARSE_COLS",       nql_fn_sparse_cols,       1, 1, "Column count");
    nql_register_func("SPARSE_MUL_VEC",    nql_fn_sparse_mul_vec,    2, 2, "Sparse matrix-vector multiply");
    nql_register_func("SPARSE_ADD",        nql_fn_sparse_add,        2, 2, "Sparse matrix addition");
    nql_register_func("SPARSE_SCALE",      nql_fn_sparse_scale,      2, 2, "Scalar multiplication");
    nql_register_func("SPARSE_TRANSPOSE",  nql_fn_sparse_transpose,  1, 1, "Transpose");
    nql_register_func("SPARSE_TO_DENSE",   nql_fn_sparse_to_dense,   1, 1, "Convert to [rows, cols, flat_array]");
    nql_register_func("SPARSE_FROM_DENSE", nql_fn_sparse_from_dense, 1, 1, "Convert dense matrix to sparse");
    nql_register_func("SPARSE_SOLVE_CG",   nql_fn_sparse_solve_cg,   2, 4, "Conjugate gradient solver Ax=b");
}
