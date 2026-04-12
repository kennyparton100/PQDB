/**
 * nql_matrix.c - Dense matrix implementation for NQL
 * Part of the NQL mathematical expansion.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* Dense matrix structure */
typedef struct NqlMatrix_s {
    double* data;        /* Row-major storage */
    uint32_t rows;
    uint32_t cols;
    bool is_complex;     /* Flag for complex matrices */
} NqlMatrix;

/* Core operations */
static NqlMatrix* nql_matrix_alloc(uint32_t rows, uint32_t cols, bool complex) {
    if (rows == 0 || cols == 0) return NULL;
    
    NqlMatrix* m = (NqlMatrix*)malloc(sizeof(NqlMatrix));
    if (!m) return NULL;
    
    size_t data_size = (size_t)rows * cols * (complex ? 2 : 1);
    m->data = (double*)calloc(data_size, sizeof(double));
    if (!m->data) {
        free(m);
        return NULL;
    }
    
    m->rows = rows;
    m->cols = cols;
    m->is_complex = complex;
    return m;
}

static NqlMatrix* nql_matrix_copy(const NqlMatrix* src) {
    if (!src) return NULL;
    
    NqlMatrix* m = nql_matrix_alloc(src->rows, src->cols, src->is_complex);
    if (!m) return NULL;
    
    size_t data_size = (size_t)src->rows * src->cols * (src->is_complex ? 2 : 1);
    memcpy(m->data, src->data, data_size * sizeof(double));
    return m;
}

static void nql_matrix_free(NqlMatrix* m) {
    if (m) {
        free(m->data);
        free(m);
    }
}

static double* nql_matrix_get(const NqlMatrix* m, uint32_t row, uint32_t col) {
    if (!m || row >= m->rows || col >= m->cols) return NULL;
    return &m->data[row * m->cols + col];
}

static bool nql_matrix_set(NqlMatrix* m, uint32_t row, uint32_t col, double value) {
    if (!m || row >= m->rows || col >= m->cols) return false;
    m->data[row * m->cols + col] = value;
    return true;
}

static NqlMatrix* nql_matrix_add(const NqlMatrix* A, const NqlMatrix* B) {
    if (!A || !B || A->rows != B->rows || A->cols != B->cols || A->is_complex != B->is_complex) {
        return NULL;
    }
    
    NqlMatrix* result = nql_matrix_alloc(A->rows, A->cols, A->is_complex);
    if (!result) return NULL;
    
    size_t data_size = (size_t)A->rows * A->cols * (A->is_complex ? 2 : 1);
    for (size_t i = 0; i < data_size; i++) {
        result->data[i] = A->data[i] + B->data[i];
    }
    
    return result;
}

static NqlMatrix* nql_matrix_subtract(const NqlMatrix* A, const NqlMatrix* B) {
    if (!A || !B || A->rows != B->rows || A->cols != B->cols || A->is_complex != B->is_complex) {
        return NULL;
    }
    
    NqlMatrix* result = nql_matrix_alloc(A->rows, A->cols, A->is_complex);
    if (!result) return NULL;
    
    size_t data_size = (size_t)A->rows * A->cols * (A->is_complex ? 2 : 1);
    for (size_t i = 0; i < data_size; i++) {
        result->data[i] = A->data[i] - B->data[i];
    }
    
    return result;
}

static NqlMatrix* nql_matrix_mul(const NqlMatrix* A, const NqlMatrix* B) {
    if (!A || !B || A->cols != B->rows || A->is_complex != B->is_complex) {
        return NULL;
    }
    
    NqlMatrix* result = nql_matrix_alloc(A->rows, B->cols, A->is_complex);
    if (!result) return NULL;
    
    uint32_t m = A->rows;
    uint32_t n = A->cols; /* = B->rows */
    uint32_t p = B->cols;
    uint32_t elem_size = A->is_complex ? 2 : 1;
    
    for (uint32_t i = 0; i < m; i++) {
        for (uint32_t j = 0; j < p; j++) {
            for (uint32_t k = 0; k < n; k++) {
                for (uint32_t e = 0; e < elem_size; e++) {
                    size_t a_idx = (i * n + k) * elem_size + e;
                    size_t b_idx = (k * p + j) * elem_size + e;
                    size_t r_idx = (i * p + j) * elem_size + e;
                    
                    if (A->is_complex && e == 1) {
                        /* Imaginary part: a_i,k_imag * b_k,j_real + a_i,k_real * b_k,j_imag */
                        result->data[r_idx] += A->data[a_idx] * B->data[b_idx - 1] + A->data[a_idx - 1] * B->data[b_idx];
                    } else {
                        /* Real part or complex real part */
                        if (A->is_complex && e == 0) {
                            /* Complex multiplication: (a+bi)(c+di) = (ac-bd) + i(ad+bc) */
                            result->data[r_idx] += A->data[a_idx] * B->data[b_idx] - A->data[a_idx + 1] * B->data[b_idx + 1];
                        } else {
                            /* Real multiplication */
                            result->data[r_idx] += A->data[a_idx] * B->data[b_idx];
                        }
                    }
                }
            }
        }
    }
    
    return result;
}

static NqlMatrix* nql_matrix_transpose(const NqlMatrix* A) {
    if (!A) return NULL;
    
    NqlMatrix* result = nql_matrix_alloc(A->cols, A->rows, A->is_complex);
    if (!result) return NULL;
    
    uint32_t elem_size = A->is_complex ? 2 : 1;
    
    for (uint32_t i = 0; i < A->rows; i++) {
        for (uint32_t j = 0; j < A->cols; j++) {
            for (uint32_t e = 0; e < elem_size; e++) {
                size_t src_idx = (i * A->cols + j) * elem_size + e;
                size_t dst_idx = (j * A->rows + i) * elem_size + e;
                result->data[dst_idx] = A->data[src_idx];
            }
        }
    }
    
    return result;
}

static double nql_matrix_determinant(const NqlMatrix* A) {
    if (!A || A->rows != A->cols || A->is_complex) return 0.0;
    
    uint32_t n = A->rows;
    if (n == 1) return A->data[0];
    if (n == 2) return A->data[0] * A->data[3] - A->data[1] * A->data[2];
    
    /* For larger matrices, use LU decomposition (simplified version) */
    NqlMatrix* temp = nql_matrix_copy(A);
    if (!temp) return 0.0;
    
    double det = 1.0;
    
    for (uint32_t i = 0; i < n; i++) {
        /* Find pivot */
        uint32_t pivot_row = i;
        for (uint32_t j = i + 1; j < n; j++) {
            if (fabs(temp->data[j * n + i]) > fabs(temp->data[pivot_row * n + i])) {
                pivot_row = j;
            }
        }
        
        if (fabs(temp->data[pivot_row * n + i]) < 1e-10) {
            nql_matrix_free(temp);
            return 0.0; /* Singular matrix */
        }
        
        /* Swap rows if needed */
        if (pivot_row != i) {
            for (uint32_t j = i; j < n; j++) {
                double temp_val = temp->data[i * n + j];
                temp->data[i * n + j] = temp->data[pivot_row * n + j];
                temp->data[pivot_row * n + j] = temp_val;
            }
            det = -det;
        }
        
        det *= temp->data[i * n + i];
        
        /* Eliminate column */
        for (uint32_t j = i + 1; j < n; j++) {
            double factor = temp->data[j * n + i] / temp->data[i * n + i];
            for (uint32_t k = i; k < n; k++) {
                temp->data[j * n + k] -= factor * temp->data[i * n + k];
            }
        }
    }
    
    nql_matrix_free(temp);
    return det;
}

static NqlMatrix* nql_matrix_inverse(const NqlMatrix* A) {
    if (!A || A->rows != A->cols || A->is_complex) return NULL;
    
    uint32_t n = A->rows;
    NqlMatrix* result = nql_matrix_alloc(n, n, false);
    if (!result) return NULL;
    
    NqlMatrix* temp = nql_matrix_copy(A);
    if (!temp) {
        nql_matrix_free(result);
        return NULL;
    }
    
    /* Initialize result as identity matrix */
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < n; j++) {
            result->data[i * n + j] = (i == j) ? 1.0 : 0.0;
        }
    }
    
    /* Gaussian elimination */
    for (uint32_t i = 0; i < n; i++) {
        /* Find pivot */
        uint32_t pivot_row = i;
        for (uint32_t j = i + 1; j < n; j++) {
            if (fabs(temp->data[j * n + i]) > fabs(temp->data[pivot_row * n + i])) {
                pivot_row = j;
            }
        }
        
        if (fabs(temp->data[pivot_row * n + i]) < 1e-10) {
            nql_matrix_free(temp);
            nql_matrix_free(result);
            return NULL; /* Singular matrix */
        }
        
        /* Swap rows if needed */
        if (pivot_row != i) {
            for (uint32_t j = 0; j < n; j++) {
                double temp_val = temp->data[i * n + j];
                temp->data[i * n + j] = temp->data[pivot_row * n + j];
                temp->data[pivot_row * n + j] = temp_val;
                
                temp_val = result->data[i * n + j];
                result->data[i * n + j] = result->data[pivot_row * n + j];
                result->data[pivot_row * n + j] = temp_val;
            }
        }
        
        /* Normalize pivot row */
        double pivot = temp->data[i * n + i];
        for (uint32_t j = 0; j < n; j++) {
            temp->data[i * n + j] /= pivot;
            result->data[i * n + j] /= pivot;
        }
        
        /* Eliminate column */
        for (uint32_t j = 0; j < n; j++) {
            if (j != i) {
                double factor = temp->data[j * n + i];
                for (uint32_t k = 0; k < n; k++) {
                    temp->data[j * n + k] -= factor * temp->data[i * n + k];
                    result->data[j * n + k] -= factor * result->data[i * n + k];
                }
            }
        }
    }
    
    nql_matrix_free(temp);
    return result;
}

static double nql_matrix_trace(const NqlMatrix* A) {
    if (!A || A->rows != A->cols || A->is_complex) return 0.0;
    
    double trace = 0.0;
    for (uint32_t i = 0; i < A->rows; i++) {
        trace += A->data[i * A->cols + i];
    }
    
    return trace;
}

/* NQL value constructors */
static NqlValue nql_val_matrix(uint32_t rows, uint32_t cols, bool complex) {
    NqlValue r;
    r.type = NQL_VAL_MATRIX;
    r.v.matrix = nql_matrix_alloc(rows, cols, complex);
    return r;
}

/* NQL function implementations */
static NqlValue nql_fn_matrix_create(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_INT || args[1].type != NQL_VAL_INT) return nql_val_null();
    
    uint32_t rows = (uint32_t)args[0].v.ival;
    uint32_t cols = (uint32_t)args[1].v.ival;
    
    if (rows == 0 || cols == 0) return nql_val_null();
    
    return nql_val_matrix(rows, cols, false);
}

static NqlValue nql_fn_matrix_rows(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    
    return nql_val_int(args[0].v.matrix->rows);
}

static NqlValue nql_fn_matrix_cols(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    
    return nql_val_int(args[0].v.matrix->cols);
}

static NqlValue nql_fn_matrix_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    if (args[1].type != NQL_VAL_MATRIX || !args[1].v.matrix) return nql_val_null();
    
    NqlMatrix* result = nql_matrix_add(args[0].v.matrix, args[1].v.matrix);
    if (!result) return nql_val_null();
    
    NqlValue r;
    r.type = NQL_VAL_MATRIX;
    r.v.matrix = result;
    return r;
}

static NqlValue nql_fn_matrix_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    if (args[1].type != NQL_VAL_MATRIX || !args[1].v.matrix) return nql_val_null();
    
    NqlMatrix* result = nql_matrix_mul(args[0].v.matrix, args[1].v.matrix);
    if (!result) return nql_val_null();
    
    NqlValue r;
    r.type = NQL_VAL_MATRIX;
    r.v.matrix = result;
    return r;
}

static NqlValue nql_fn_matrix_from_array(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    if (args[0].type != NQL_VAL_INT || args[1].type != NQL_VAL_INT) return nql_val_null();
    if (args[2].type != NQL_VAL_ARRAY || !args[2].v.array) return nql_val_null();
    uint32_t rows = (uint32_t)args[0].v.ival;
    uint32_t cols = (uint32_t)args[1].v.ival;
    NqlArray* data = args[2].v.array;
    if (rows == 0 || cols == 0 || data->length < rows * cols) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_MATRIX;
    r.v.matrix = nql_matrix_alloc(rows, cols, false);
    if (!r.v.matrix) return nql_val_null();
    for (uint32_t i = 0; i < rows * cols; i++) {
        r.v.matrix->data[i] = (data->items[i].type == NQL_VAL_INT) ?
            (double)data->items[i].v.ival : data->items[i].v.fval;
    }
    return r;
}

static NqlValue nql_fn_matrix_identity(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1 || args[0].type != NQL_VAL_INT) return nql_val_null();
    uint32_t n = (uint32_t)args[0].v.ival;
    if (n == 0 || n > 1000) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_MATRIX;
    r.v.matrix = nql_matrix_alloc(n, n, false);
    if (!r.v.matrix) return nql_val_null();
    for (uint32_t i = 0; i < n; i++) r.v.matrix->data[i * n + i] = 1.0;
    return r;
}

static NqlValue nql_fn_matrix_get(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null(); NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    uint32_t row = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t col = (uint32_t)nql_val_as_int(&args[2]);
    double* val = nql_matrix_get(args[0].v.matrix, row, col);
    if (!val) return nql_val_null();
    return nql_val_float(*val);
}

static NqlValue nql_fn_matrix_set(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 4) return nql_val_null(); NQL_NULL_GUARD4(args);
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    uint32_t row = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t col = (uint32_t)nql_val_as_int(&args[2]);
    double val = (args[3].type == NQL_VAL_INT) ? (double)args[3].v.ival : args[3].v.fval;
    NqlMatrix* m = nql_matrix_copy(args[0].v.matrix);
    if (!m) return nql_val_null();
    if (!nql_matrix_set(m, row, col, val)) { nql_matrix_free(m); return nql_val_null(); }
    NqlValue r; r.type = NQL_VAL_MATRIX; r.v.matrix = m; return r;
}

static NqlValue nql_fn_matrix_sub(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    if (args[1].type != NQL_VAL_MATRIX || !args[1].v.matrix) return nql_val_null();
    NqlMatrix* result = nql_matrix_subtract(args[0].v.matrix, args[1].v.matrix);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_MATRIX; r.v.matrix = result; return r;
}

static NqlValue nql_fn_matrix_transpose(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* result = nql_matrix_transpose(args[0].v.matrix);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_MATRIX; r.v.matrix = result; return r;
}

static NqlValue nql_fn_matrix_det(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    if (args[0].v.matrix->rows != args[0].v.matrix->cols) return nql_val_null();
    return nql_val_float(nql_matrix_determinant(args[0].v.matrix));
}

static NqlValue nql_fn_matrix_inverse(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* result = nql_matrix_inverse(args[0].v.matrix);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_MATRIX; r.v.matrix = result; return r;
}

static NqlValue nql_fn_matrix_trace(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    if (args[0].v.matrix->rows != args[0].v.matrix->cols) return nql_val_null();
    return nql_val_float(nql_matrix_trace(args[0].v.matrix));
}

static NqlValue nql_fn_matrix_scale(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    double scalar = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    NqlMatrix* src = args[0].v.matrix;
    NqlMatrix* result = nql_matrix_copy(src);
    if (!result) return nql_val_null();
    size_t n = (size_t)src->rows * src->cols;
    for (size_t i = 0; i < n; i++) result->data[i] *= scalar;
    NqlValue r; r.type = NQL_VAL_MATRIX; r.v.matrix = result; return r;
}

/* -- Extended Matrix Operations -- */

/** MATRIX_SOLVE(A, b) -- solve Ax=b via Gaussian elimination with partial pivoting.
 *  A: square matrix, b: array (column vector). Returns solution array x. */
static NqlValue nql_fn_matrix_solve(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlMatrix* A = args[0].v.matrix;
    NqlArray* b = args[1].v.array;
    uint32_t n = A->rows;
    if (A->cols != n || b->length != n || n == 0) return nql_val_null();
    /* Build augmented matrix [A|b] */
    double* aug = (double*)malloc(n * (n + 1) * sizeof(double));
    if (!aug) return nql_val_null();
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < n; j++)
            aug[i * (n + 1) + j] = A->data[i * n + j];
        aug[i * (n + 1) + n] = (b->items[i].type == NQL_VAL_INT) ? (double)b->items[i].v.ival : b->items[i].v.fval;
    }
    /* Forward elimination with partial pivoting */
    for (uint32_t col = 0; col < n; col++) {
        uint32_t max_row = col;
        double max_val = fabs(aug[col * (n + 1) + col]);
        for (uint32_t row = col + 1; row < n; row++) {
            double v = fabs(aug[row * (n + 1) + col]);
            if (v > max_val) { max_val = v; max_row = row; }
        }
        if (max_val < 1e-15) { free(aug); return nql_val_null(); }
        if (max_row != col)
            for (uint32_t j = 0; j <= n; j++) {
                double t = aug[col * (n + 1) + j];
                aug[col * (n + 1) + j] = aug[max_row * (n + 1) + j];
                aug[max_row * (n + 1) + j] = t;
            }
        for (uint32_t row = col + 1; row < n; row++) {
            double factor = aug[row * (n + 1) + col] / aug[col * (n + 1) + col];
            for (uint32_t j = col; j <= n; j++)
                aug[row * (n + 1) + j] -= factor * aug[col * (n + 1) + j];
        }
    }
    /* Back substitution */
    double* x = (double*)malloc(n * sizeof(double));
    if (!x) { free(aug); return nql_val_null(); }
    for (int32_t i = (int32_t)n - 1; i >= 0; i--) {
        x[i] = aug[i * (n + 1) + n];
        for (uint32_t j = (uint32_t)i + 1; j < n; j++)
            x[i] -= aug[i * (n + 1) + j] * x[j];
        x[i] /= aug[i * (n + 1) + i];
    }
    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(aug); free(x); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++)
        nql_array_push(result, nql_val_float(x[i]));
    free(aug); free(x);
    return nql_val_array(result);
}

static NqlValue nql_fn_matrix_rank(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* m = args[0].v.matrix;
    uint32_t rows = m->rows, cols = m->cols;
    double* work = (double*)malloc(rows * cols * sizeof(double));
    if (!work) return nql_val_null();
    memcpy(work, m->data, rows * cols * sizeof(double));
    uint32_t rank = 0;
    for (uint32_t col = 0; col < cols && rank < rows; col++) {
        uint32_t pivot = rank;
        for (uint32_t r = rank + 1; r < rows; r++)
            if (fabs(work[r * cols + col]) > fabs(work[pivot * cols + col])) pivot = r;
        if (fabs(work[pivot * cols + col]) < 1e-12) continue;
        if (pivot != rank)
            for (uint32_t j = 0; j < cols; j++) {
                double t = work[rank * cols + j]; work[rank * cols + j] = work[pivot * cols + j]; work[pivot * cols + j] = t;
            }
        for (uint32_t r = rank + 1; r < rows; r++) {
            double f = work[r * cols + col] / work[rank * cols + col];
            for (uint32_t j = col; j < cols; j++)
                work[r * cols + j] -= f * work[rank * cols + j];
        }
        rank++;
    }
    free(work);
    return nql_val_int((int64_t)rank);
}

static NqlValue nql_fn_matrix_norm(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* m = args[0].v.matrix;
    double sum = 0.0;
    size_t n = (size_t)m->rows * m->cols;
    for (size_t i = 0; i < n; i++) sum += m->data[i] * m->data[i];
    return nql_val_float(sqrt(sum));
}

/** MATRIX_EIGENVALUES -- 2x2 only, returns [lambda1, lambda2] */
static NqlValue nql_fn_matrix_eigenvalues(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* m = args[0].v.matrix;
    if (m->rows != 2 || m->cols != 2) return nql_val_null();
    double a = m->data[0], b = m->data[1], c = m->data[2], d = m->data[3];
    double trace = a + d;
    double det = a * d - b * c;
    double disc = trace * trace - 4.0 * det;
    if (disc < 0.0) return nql_val_null(); /* Complex eigenvalues unsupported */
    double sq = sqrt(disc);
    NqlArray* result = nql_array_alloc(2);
    if (!result) return nql_val_null();
    nql_array_push(result, nql_val_float((trace + sq) / 2.0));
    nql_array_push(result, nql_val_float((trace - sq) / 2.0));
    return nql_val_array(result);
}

static NqlValue nql_fn_matrix_to_array(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* m = args[0].v.matrix;
    uint32_t n = m->rows * m->cols;
    NqlArray* result = nql_array_alloc(n);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < n; i++)
        nql_array_push(result, nql_val_float(m->data[i]));
    return nql_val_array(result);
}

static NqlValue nql_fn_matrix_row(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* m = args[0].v.matrix;
    uint32_t row = (uint32_t)((args[1].type == NQL_VAL_INT) ? args[1].v.ival : (int64_t)args[1].v.fval);
    if (row >= m->rows) return nql_val_null();
    NqlArray* result = nql_array_alloc(m->cols);
    if (!result) return nql_val_null();
    for (uint32_t j = 0; j < m->cols; j++)
        nql_array_push(result, nql_val_float(m->data[row * m->cols + j]));
    return nql_val_array(result);
}

static NqlValue nql_fn_matrix_col(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* m = args[0].v.matrix;
    uint32_t col = (uint32_t)((args[1].type == NQL_VAL_INT) ? args[1].v.ival : (int64_t)args[1].v.fval);
    if (col >= m->cols) return nql_val_null();
    NqlArray* result = nql_array_alloc(m->rows);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < m->rows; i++)
        nql_array_push(result, nql_val_float(m->data[i * m->cols + col]));
    return nql_val_array(result);
}

/* Register matrix functions */
void nql_matrix_register_functions(void) {
    nql_register_func("MATRIX",              nql_fn_matrix_create,      2, 2, "Create zero matrix (rows, cols)");
    nql_register_func("MATRIX_FROM_ARRAY",   nql_fn_matrix_from_array,  3, 3, "Create matrix from data (rows, cols, array)");
    nql_register_func("MATRIX_IDENTITY",     nql_fn_matrix_identity,    1, 1, "Create n x n identity matrix");
    nql_register_func("MATRIX_ROWS",         nql_fn_matrix_rows,        1, 1, "Row count");
    nql_register_func("MATRIX_COLS",         nql_fn_matrix_cols,        1, 1, "Column count");
    nql_register_func("MATRIX_GET",          nql_fn_matrix_get,         3, 3, "Get element at (row, col)");
    nql_register_func("MATRIX_SET",          nql_fn_matrix_set,         4, 4, "Set element, returns new matrix");
    nql_register_func("MATRIX_ADD",          nql_fn_matrix_add,         2, 2, "Matrix addition");
    nql_register_func("MATRIX_SUB",          nql_fn_matrix_sub,         2, 2, "Matrix subtraction");
    nql_register_func("MATRIX_MUL",          nql_fn_matrix_mul,         2, 2, "Matrix multiplication");
    nql_register_func("MATRIX_SCALE",        nql_fn_matrix_scale,       2, 2, "Scalar multiplication");
    nql_register_func("MATRIX_TRANSPOSE",    nql_fn_matrix_transpose,   1, 1, "Matrix transpose");
    nql_register_func("MATRIX_DETERMINANT",  nql_fn_matrix_det,         1, 1, "Determinant (square matrix)");
    nql_register_func("MATRIX_INVERSE",      nql_fn_matrix_inverse,     1, 1, "Matrix inverse (square matrix)");
    nql_register_func("MATRIX_TRACE",        nql_fn_matrix_trace,       1, 1, "Trace (sum of diagonal)");
    nql_register_func("MATRIX_SOLVE",        nql_fn_matrix_solve,       2, 2, "Solve Ax=b (matrix, array)");
    nql_register_func("MATRIX_RANK",         nql_fn_matrix_rank,        1, 1, "Matrix rank");
    nql_register_func("MATRIX_NORM",         nql_fn_matrix_norm,        1, 1, "Frobenius norm");
    nql_register_func("MATRIX_EIGENVALUES",  nql_fn_matrix_eigenvalues, 1, 1, "Eigenvalues (2x2 only)");
    nql_register_func("MATRIX_TO_ARRAY",     nql_fn_matrix_to_array,    1, 1, "Flatten matrix to array");
    nql_register_func("MATRIX_ROW",          nql_fn_matrix_row,         2, 2, "Extract row as array");
    nql_register_func("MATRIX_COL",          nql_fn_matrix_col,         2, 2, "Extract column as array");
}
