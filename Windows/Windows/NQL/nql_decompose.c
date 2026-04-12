/**
 * nql_decompose.c - NQL Matrix Decompositions.
 * Part of the CPSS Viewer amalgamation.
 *
 * LU, QR (Gram-Schmidt), Cholesky, SVD (2x2), general eigenvalues (QR algorithm).
 * Operates on NqlMatrix (dense, from nql_matrix.c).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* -- Helper: allocate dense matrix as flat array -- */
static double* nql_dec_mat_copy(const NqlMatrix* m) {
    size_t sz = (size_t)m->rows * m->cols * sizeof(double);
    double* d = (double*)malloc(sz);
    if (d) memcpy(d, m->data, sz);
    return d;
}

static NqlMatrix* nql_dec_mat_alloc(uint32_t r, uint32_t c) {
    NqlMatrix* m = (NqlMatrix*)malloc(sizeof(NqlMatrix));
    if (!m) return NULL;
    m->rows = r; m->cols = c; m->is_complex = false;
    m->data = (double*)calloc((size_t)r * c, sizeof(double));
    if (!m->data) { free(m); return NULL; }
    return m;
}

static NqlValue nql_dec_val_matrix(NqlMatrix* m) {
    NqlValue v; memset(&v, 0, sizeof(v));
    v.type = NQL_VAL_MATRIX; v.v.matrix = m;
    return v;
}

/* ======================================================================
 * MATRIX_LU(m) -> [L, U, P_perm_array]
 * Partial-pivoting LU decomposition: PA = LU
 * ====================================================================== */

static NqlValue nql_fn_matrix_lu(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* src = args[0].v.matrix;
    uint32_t n = src->rows;
    if (n != src->cols || n == 0 || n > 256) return nql_val_null();

    double* A = nql_dec_mat_copy(src);
    int* perm = (int*)malloc(n * sizeof(int));
    if (!A || !perm) { free(A); free(perm); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) perm[i] = (int)i;

    for (uint32_t k = 0; k < n; k++) {
        /* Pivot */
        uint32_t piv = k; double mx = fabs(A[k * n + k]);
        for (uint32_t i = k + 1; i < n; i++) if (fabs(A[i * n + k]) > mx) { mx = fabs(A[i * n + k]); piv = i; }
        if (piv != k) {
            for (uint32_t j = 0; j < n; j++) { double t = A[k * n + j]; A[k * n + j] = A[piv * n + j]; A[piv * n + j] = t; }
            int t = perm[k]; perm[k] = perm[piv]; perm[piv] = t;
        }
        if (fabs(A[k * n + k]) < 1e-15) continue;
        for (uint32_t i = k + 1; i < n; i++) {
            A[i * n + k] /= A[k * n + k];
            for (uint32_t j = k + 1; j < n; j++) A[i * n + j] -= A[i * n + k] * A[k * n + j];
        }
    }

    NqlMatrix* L = nql_dec_mat_alloc(n, n);
    NqlMatrix* U = nql_dec_mat_alloc(n, n);
    if (!L || !U) { free(A); free(perm); free(L); free(U); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) {
        L->data[i * n + i] = 1.0;
        for (uint32_t j = 0; j < i; j++) L->data[i * n + j] = A[i * n + j];
        for (uint32_t j = i; j < n; j++) U->data[i * n + j] = A[i * n + j];
    }
    NqlArray* P = nql_array_alloc(n);
    if (!P) { free(A); free(perm); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) nql_array_push(P, nql_val_int(perm[i]));

    NqlArray* result = nql_array_alloc(3);
    if (!result) { free(A); free(perm); return nql_val_null(); }
    nql_array_push(result, nql_dec_val_matrix(L));
    nql_array_push(result, nql_dec_val_matrix(U));
    nql_array_push(result, nql_val_array(P));
    free(A); free(perm);
    return nql_val_array(result);
}

/* ======================================================================
 * MATRIX_QR(m) -> [Q, R]
 * Classical Gram-Schmidt
 * ====================================================================== */

static NqlValue nql_fn_matrix_qr(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* src = args[0].v.matrix;
    uint32_t m = src->rows, n = src->cols;
    if (m == 0 || n == 0 || m > 256 || n > 256) return nql_val_null();
    uint32_t k = (m < n) ? m : n;

    NqlMatrix* Q = nql_dec_mat_alloc(m, k);
    NqlMatrix* R = nql_dec_mat_alloc(k, n);
    if (!Q || !R) { free(Q); free(R); return nql_val_null(); }

    for (uint32_t j = 0; j < k; j++) {
        /* Copy column j of src into Q[:,j] */
        for (uint32_t i = 0; i < m; i++) Q->data[i * k + j] = src->data[i * n + j];
        /* Subtract projections */
        for (uint32_t p = 0; p < j; p++) {
            double dot = 0;
            for (uint32_t i = 0; i < m; i++) dot += Q->data[i * k + p] * Q->data[i * k + j];
            R->data[p * n + j] = dot;
            for (uint32_t i = 0; i < m; i++) Q->data[i * k + j] -= dot * Q->data[i * k + p];
        }
        /* Normalize */
        double norm = 0;
        for (uint32_t i = 0; i < m; i++) norm += Q->data[i * k + j] * Q->data[i * k + j];
        norm = sqrt(norm);
        R->data[j * n + j] = norm;
        if (norm > 1e-15) for (uint32_t i = 0; i < m; i++) Q->data[i * k + j] /= norm;
    }

    NqlArray* result = nql_array_alloc(2);
    if (!result) return nql_val_null();
    nql_array_push(result, nql_dec_val_matrix(Q));
    nql_array_push(result, nql_dec_val_matrix(R));
    return nql_val_array(result);
}

/* ======================================================================
 * MATRIX_CHOLESKY(m) -> L where A = LL^T
 * ====================================================================== */

static NqlValue nql_fn_matrix_cholesky(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* src = args[0].v.matrix;
    uint32_t n = src->rows;
    if (n != src->cols || n == 0 || n > 256) return nql_val_null();

    NqlMatrix* L = nql_dec_mat_alloc(n, n);
    if (!L) return nql_val_null();

    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j <= i; j++) {
            double sum = 0;
            for (uint32_t k = 0; k < j; k++) sum += L->data[i * n + k] * L->data[j * n + k];
            if (i == j) {
                double val = src->data[i * n + i] - sum;
                if (val <= 0) { free(L->data); free(L); return nql_val_null(); } /* Not positive definite */
                L->data[i * n + j] = sqrt(val);
            } else {
                L->data[i * n + j] = (src->data[i * n + j] - sum) / L->data[j * n + j];
            }
        }
    }
    return nql_dec_val_matrix(L);
}

/* ======================================================================
 * MATRIX_EIGENVALUES_QR(m [, max_iter]) -> eigenvalue array (NxN)
 * QR algorithm for general real eigenvalues
 * ====================================================================== */

static NqlValue nql_fn_matrix_eigen_qr(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* src = args[0].v.matrix;
    uint32_t n = src->rows;
    if (n != src->cols || n == 0 || n > 64) return nql_val_null();
    int max_iter = (argc >= 2) ? (int)nql_val_as_int(&args[1]) : 200;

    double* A = nql_dec_mat_copy(src);
    double* Q = (double*)malloc(n * n * sizeof(double));
    double* R = (double*)malloc(n * n * sizeof(double));
    if (!A || !Q || !R) { free(A); free(Q); free(R); return nql_val_null(); }

    for (int iter = 0; iter < max_iter; iter++) {
        /* QR decomposition of A (Gram-Schmidt) */
        memcpy(Q, A, n * n * sizeof(double));
        memset(R, 0, n * n * sizeof(double));
        for (uint32_t j = 0; j < n; j++) {
            for (uint32_t p = 0; p < j; p++) {
                double dot = 0;
                for (uint32_t i = 0; i < n; i++) dot += Q[i * n + p] * Q[i * n + j];
                R[p * n + j] = dot;
                for (uint32_t i = 0; i < n; i++) Q[i * n + j] -= dot * Q[i * n + p];
            }
            double norm = 0;
            for (uint32_t i = 0; i < n; i++) norm += Q[i * n + j] * Q[i * n + j];
            norm = sqrt(norm);
            R[j * n + j] = norm;
            if (norm > 1e-15) for (uint32_t i = 0; i < n; i++) Q[i * n + j] /= norm;
        }
        /* A = R * Q */
        for (uint32_t i = 0; i < n; i++) {
            for (uint32_t j = 0; j < n; j++) {
                double s = 0;
                for (uint32_t k = 0; k < n; k++) s += R[i * n + k] * Q[k * n + j];
                A[i * n + j] = s;
            }
        }
        /* Check convergence: sub-diagonal elements */
        double off = 0;
        for (uint32_t i = 1; i < n; i++) off += fabs(A[i * n + (i - 1)]);
        if (off < 1e-12 * n) break;
    }

    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(A); free(Q); free(R); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) nql_array_push(result, nql_val_float(A[i * n + i]));
    free(A); free(Q); free(R);
    return nql_val_array(result);
}

/* ======================================================================
 * MATRIX_SVD(m) -> [U, S_array, Vt] (for small matrices, <= 32x32)
 * One-sided Jacobi SVD
 * ====================================================================== */

static NqlValue nql_fn_matrix_svd(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_MATRIX || !args[0].v.matrix) return nql_val_null();
    NqlMatrix* src = args[0].v.matrix;
    uint32_t m = src->rows, n = src->cols;
    if (m == 0 || n == 0 || m > 32 || n > 32) return nql_val_null();
    uint32_t k = (m < n) ? m : n;

    /* Compute A^T A */
    double* AtA = (double*)calloc(n * n, sizeof(double));
    if (!AtA) return nql_val_null();
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++)
            for (uint32_t p = 0; p < m; p++)
                AtA[i * n + j] += src->data[p * n + i] * src->data[p * n + j];

    /* Eigendecomposition of A^T A via QR algorithm */
    double* V = (double*)calloc(n * n, sizeof(double));
    for (uint32_t i = 0; i < n; i++) V[i * n + i] = 1.0;
    double* W = (double*)malloc(n * n * sizeof(double));
    memcpy(W, AtA, n * n * sizeof(double));
    double* Qm = (double*)malloc(n * n * sizeof(double));
    double* Rm = (double*)malloc(n * n * sizeof(double));
    if (!V || !W || !Qm || !Rm) { free(AtA); free(V); free(W); free(Qm); free(Rm); return nql_val_null(); }

    for (int iter = 0; iter < 200; iter++) {
        memcpy(Qm, W, n * n * sizeof(double));
        memset(Rm, 0, n * n * sizeof(double));
        for (uint32_t j = 0; j < n; j++) {
            for (uint32_t p = 0; p < j; p++) {
                double dot = 0;
                for (uint32_t i = 0; i < n; i++) dot += Qm[i * n + p] * Qm[i * n + j];
                Rm[p * n + j] = dot;
                for (uint32_t i = 0; i < n; i++) Qm[i * n + j] -= dot * Qm[i * n + p];
            }
            double norm = 0;
            for (uint32_t i = 0; i < n; i++) norm += Qm[i * n + j] * Qm[i * n + j];
            norm = sqrt(norm);
            Rm[j * n + j] = norm;
            if (norm > 1e-15) for (uint32_t i = 0; i < n; i++) Qm[i * n + j] /= norm;
        }
        /* W = Rm * Qm */
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t j = 0; j < n; j++) {
                double s = 0;
                for (uint32_t l = 0; l < n; l++) s += Rm[i * n + l] * Qm[l * n + j];
                W[i * n + j] = s;
            }
        /* V = V * Qm */
        double* Vnew = (double*)calloc(n * n, sizeof(double));
        if (!Vnew) break;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t j = 0; j < n; j++)
                for (uint32_t l = 0; l < n; l++)
                    Vnew[i * n + j] += V[i * n + l] * Qm[l * n + j];
        memcpy(V, Vnew, n * n * sizeof(double));
        free(Vnew);
        double off = 0;
        for (uint32_t i = 1; i < n; i++) off += fabs(W[i * n + (i - 1)]);
        if (off < 1e-12) break;
    }

    /* Singular values = sqrt(eigenvalues of A^T A) */
    NqlArray* S = nql_array_alloc(k);
    if (!S) { free(AtA); free(V); free(W); free(Qm); free(Rm); return nql_val_null(); }
    for (uint32_t i = 0; i < k; i++) {
        double ev = W[i * n + i];
        nql_array_push(S, nql_val_float(ev > 0 ? sqrt(ev) : 0));
    }

    /* U = A * V * S^{-1} */
    NqlMatrix* U = nql_dec_mat_alloc(m, k);
    NqlMatrix* Vt = nql_dec_mat_alloc(k, n);
    if (!U || !Vt) { free(AtA); free(V); free(W); free(Qm); free(Rm); return nql_val_null(); }

    for (uint32_t i = 0; i < m; i++)
        for (uint32_t j = 0; j < k; j++) {
            double s = 0;
            for (uint32_t l = 0; l < n; l++) s += src->data[i * n + l] * V[l * n + j];
            double sv = nql_val_as_float(&S->items[j]);
            U->data[i * k + j] = (sv > 1e-15) ? s / sv : 0;
        }

    for (uint32_t i = 0; i < k; i++)
        for (uint32_t j = 0; j < n; j++)
            Vt->data[i * n + j] = V[j * n + i];

    free(AtA); free(V); free(W); free(Qm); free(Rm);

    NqlArray* result = nql_array_alloc(3);
    if (!result) return nql_val_null();
    nql_array_push(result, nql_dec_val_matrix(U));
    nql_array_push(result, nql_val_array(S));
    nql_array_push(result, nql_dec_val_matrix(Vt));
    return nql_val_array(result);
}

/** MATRIX_CONDITION(m) -- condition number (ratio of max/min singular values) */
static NqlValue nql_fn_matrix_condition(const NqlValue* args, uint32_t argc, void* ctx) {
    NqlValue svd = nql_fn_matrix_svd(args, argc, ctx);
    if (svd.type != NQL_VAL_ARRAY || !svd.v.array || svd.v.array->length < 2) return nql_val_null();
    NqlValue sarr = svd.v.array->items[1];
    if (sarr.type != NQL_VAL_ARRAY || !sarr.v.array || sarr.v.array->length == 0) return nql_val_null();
    NqlArray* S = sarr.v.array;
    double smax = 0, smin = 1e300;
    for (uint32_t i = 0; i < S->length; i++) {
        double sv = nql_val_as_float(&S->items[i]);
        if (sv > smax) smax = sv;
        if (sv < smin && sv > 0) smin = sv;
    }
    if (smin < 1e-15) return nql_val_float(1e18);
    return nql_val_float(smax / smin);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_decompose_register_functions(void) {
    nql_register_func("MATRIX_LU",             nql_fn_matrix_lu,        1, 1, "LU decomposition: [L, U, P_perm]");
    nql_register_func("MATRIX_QR",             nql_fn_matrix_qr,        1, 1, "QR decomposition: [Q, R]");
    nql_register_func("MATRIX_CHOLESKY",       nql_fn_matrix_cholesky,  1, 1, "Cholesky: L where A=LL^T");
    nql_register_func("MATRIX_EIGENVALUES_QR", nql_fn_matrix_eigen_qr,  1, 2, "QR-algorithm eigenvalues (NxN)");
    nql_register_func("MATRIX_SVD",            nql_fn_matrix_svd,       1, 1, "SVD: [U, S_array, Vt]");
    nql_register_func("MATRIX_CONDITION",      nql_fn_matrix_condition, 1, 1, "Condition number (SVD-based)");
}
