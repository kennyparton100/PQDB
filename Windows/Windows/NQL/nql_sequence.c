/**
 * nql_sequence.c - NQL Sequence Recognition & Analysis.
 * Part of the CPSS Viewer amalgamation.
 *
 * First differences, ratios, arithmetic/geometric detection,
 * polynomial fitting, and sequence extension.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * SEQUENCE FUNCTIONS
 * ====================================================================== */

/** SEQUENCE_DIFF(array) -- first differences */
static NqlValue nql_fn_seq_diff(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    if (a->length < 2) return nql_val_null();
    NqlArray* r = nql_array_alloc(a->length - 1);
    if (!r) return nql_val_null();
    for (uint32_t i = 0; i < a->length - 1; i++) {
        double d = nql_val_as_float(&a->items[i + 1]) - nql_val_as_float(&a->items[i]);
        nql_array_push(r, nql_val_float(d));
    }
    return nql_val_array(r);
}

/** SEQUENCE_RATIO(array) -- consecutive ratios a[i+1]/a[i] */
static NqlValue nql_fn_seq_ratio(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    if (a->length < 2) return nql_val_null();
    NqlArray* r = nql_array_alloc(a->length - 1);
    if (!r) return nql_val_null();
    for (uint32_t i = 0; i < a->length - 1; i++) {
        double prev = nql_val_as_float(&a->items[i]);
        double next = nql_val_as_float(&a->items[i + 1]);
        nql_array_push(r, (fabs(prev) < 1e-15) ? nql_val_null() : nql_val_float(next / prev));
    }
    return nql_val_array(r);
}

/** SEQUENCE_IS_ARITHMETIC(array) -- check if constant first differences */
static NqlValue nql_fn_seq_is_arith(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    if (a->length < 3) return nql_val_bool(true);
    double d0 = nql_val_as_float(&a->items[1]) - nql_val_as_float(&a->items[0]);
    for (uint32_t i = 1; i < a->length - 1; i++) {
        double d = nql_val_as_float(&a->items[i + 1]) - nql_val_as_float(&a->items[i]);
        if (fabs(d - d0) > 1e-9) return nql_val_bool(false);
    }
    return nql_val_bool(true);
}

/** SEQUENCE_IS_GEOMETRIC(array) -- check if constant ratios */
static NqlValue nql_fn_seq_is_geom(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    if (a->length < 3) return nql_val_bool(true);
    double v0 = nql_val_as_float(&a->items[0]);
    if (fabs(v0) < 1e-15) return nql_val_bool(false);
    double r0 = nql_val_as_float(&a->items[1]) / v0;
    for (uint32_t i = 1; i < a->length - 1; i++) {
        double vi = nql_val_as_float(&a->items[i]);
        if (fabs(vi) < 1e-15) return nql_val_bool(false);
        double r = nql_val_as_float(&a->items[i + 1]) / vi;
        if (fabs(r - r0) > 1e-9 * fabs(r0)) return nql_val_bool(false);
    }
    return nql_val_bool(true);
}

/** SEQUENCE_POLY_FIT(array [, degree]) -- fit polynomial to 0-indexed sequence, returns coeff array */
static NqlValue nql_fn_seq_poly_fit(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    int n = (int)a->length;
    if (n < 2 || n > 100) return nql_val_null();
    int deg = (argc >= 2) ? (int)nql_val_as_int(&args[1]) : (n < 8 ? n - 1 : 7);
    if (deg >= n) deg = n - 1;
    if (deg < 1 || deg > 20) return nql_val_null();
    int m = deg + 1;

    /* Normal equations: (X^T X) c = X^T y */
    double* XtX = (double*)calloc(m * m, sizeof(double));
    double* XtY = (double*)calloc(m, sizeof(double));
    if (!XtX || !XtY) { free(XtX); free(XtY); return nql_val_null(); }

    for (int i = 0; i < n; i++) {
        double yi = nql_val_as_float(&a->items[i]);
        double xi = (double)i;
        double xpow[21]; xpow[0] = 1.0;
        for (int j = 1; j < m; j++) xpow[j] = xpow[j - 1] * xi;
        for (int r = 0; r < m; r++) {
            XtY[r] += xpow[r] * yi;
            for (int c = 0; c < m; c++) XtX[r * m + c] += xpow[r] * xpow[c];
        }
    }

    /* Gaussian elimination */
    for (int k = 0; k < m; k++) {
        int piv = k; double mx = fabs(XtX[k * m + k]);
        for (int i = k + 1; i < m; i++) if (fabs(XtX[i * m + k]) > mx) { mx = fabs(XtX[i * m + k]); piv = i; }
        if (piv != k) {
            for (int j = 0; j < m; j++) { double t = XtX[k * m + j]; XtX[k * m + j] = XtX[piv * m + j]; XtX[piv * m + j] = t; }
            double t = XtY[k]; XtY[k] = XtY[piv]; XtY[piv] = t;
        }
        if (fabs(XtX[k * m + k]) < 1e-15) { free(XtX); free(XtY); return nql_val_null(); }
        for (int i = k + 1; i < m; i++) {
            double f = XtX[i * m + k] / XtX[k * m + k];
            for (int j = k; j < m; j++) XtX[i * m + j] -= f * XtX[k * m + j];
            XtY[i] -= f * XtY[k];
        }
    }
    /* Back substitution */
    for (int i = m - 1; i >= 0; i--) {
        for (int j = i + 1; j < m; j++) XtY[i] -= XtX[i * m + j] * XtY[j];
        XtY[i] /= XtX[i * m + i];
    }

    NqlArray* result = nql_array_alloc(m);
    if (!result) { free(XtX); free(XtY); return nql_val_null(); }
    for (int i = 0; i < m; i++) nql_array_push(result, nql_val_float(XtY[i]));
    free(XtX); free(XtY);
    return nql_val_array(result);
}

/** SEQUENCE_EXTEND(array, n) -- predict next n terms using detected pattern */
static NqlValue nql_fn_seq_extend(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    int64_t ext = nql_val_as_int(&args[1]);
    if (ext < 1 || ext > 1000 || a->length < 2) return nql_val_null();
    uint32_t n = a->length;

    /* Try arithmetic */
    double d0 = nql_val_as_float(&a->items[1]) - nql_val_as_float(&a->items[0]);
    bool is_arith = true;
    for (uint32_t i = 1; i < n - 1 && is_arith; i++) {
        double d = nql_val_as_float(&a->items[i + 1]) - nql_val_as_float(&a->items[i]);
        if (fabs(d - d0) > 1e-9) is_arith = false;
    }
    if (is_arith) {
        NqlArray* r = nql_array_alloc((uint32_t)ext);
        if (!r) return nql_val_null();
        double last = nql_val_as_float(&a->items[n - 1]);
        for (int64_t i = 0; i < ext; i++) { last += d0; nql_array_push(r, nql_val_float(last)); }
        return nql_val_array(r);
    }

    /* Try geometric */
    double v0 = nql_val_as_float(&a->items[0]);
    if (fabs(v0) > 1e-15) {
        double r0 = nql_val_as_float(&a->items[1]) / v0;
        bool is_geom = true;
        for (uint32_t i = 1; i < n - 1 && is_geom; i++) {
            double vi = nql_val_as_float(&a->items[i]);
            if (fabs(vi) < 1e-15) { is_geom = false; break; }
            double r = nql_val_as_float(&a->items[i + 1]) / vi;
            if (fabs(r - r0) > 1e-9 * fabs(r0)) is_geom = false;
        }
        if (is_geom) {
            NqlArray* r = nql_array_alloc((uint32_t)ext);
            if (!r) return nql_val_null();
            double last = nql_val_as_float(&a->items[n - 1]);
            for (int64_t i = 0; i < ext; i++) { last *= r0; nql_array_push(r, nql_val_float(last)); }
            return nql_val_array(r);
        }
    }

    /* Fallback: polynomial extrapolation -- fit degree min(n-1, 5) */
    int deg = (n <= 6) ? (int)n - 1 : 5;
    NqlValue fit_args[2]; fit_args[0] = args[0]; fit_args[1] = nql_val_int(deg);
    NqlValue coeffs = nql_fn_seq_poly_fit(fit_args, 2, ctx);
    if (coeffs.type != NQL_VAL_ARRAY || !coeffs.v.array) return nql_val_null();
    NqlArray* ca = coeffs.v.array;
    NqlArray* r = nql_array_alloc((uint32_t)ext);
    if (!r) return nql_val_null();
    for (int64_t i = 0; i < ext; i++) {
        double x = (double)(n + i), val = 0, xp = 1.0;
        for (uint32_t j = 0; j < ca->length; j++) { val += nql_val_as_float(&ca->items[j]) * xp; xp *= x; }
        nql_array_push(r, nql_val_float(val));
    }
    return nql_val_array(r);
}

/** SEQUENCE_DETECT(array) -- returns string: "arithmetic", "geometric", "polynomial:N", or "unknown" */
static NqlValue nql_fn_seq_detect(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    if (a->length < 3) { NqlValue v; v.type = NQL_VAL_STRING; strncpy(v.v.sval, "too_short", 256); return v; }

    /* Arithmetic? */
    double d0 = nql_val_as_float(&a->items[1]) - nql_val_as_float(&a->items[0]);
    bool arith = true;
    for (uint32_t i = 1; i < a->length - 1 && arith; i++) {
        double d = nql_val_as_float(&a->items[i + 1]) - nql_val_as_float(&a->items[i]);
        if (fabs(d - d0) > 1e-9) arith = false;
    }
    if (arith) { NqlValue v; v.type = NQL_VAL_STRING; strncpy(v.v.sval, "arithmetic", 256); return v; }

    /* Geometric? */
    double v0 = nql_val_as_float(&a->items[0]);
    if (fabs(v0) > 1e-15) {
        double r0 = nql_val_as_float(&a->items[1]) / v0;
        bool geom = true;
        for (uint32_t i = 1; i < a->length - 1 && geom; i++) {
            double vi = nql_val_as_float(&a->items[i]);
            if (fabs(vi) < 1e-15) { geom = false; break; }
            double r = nql_val_as_float(&a->items[i + 1]) / vi;
            if (fabs(r - r0) > 1e-9 * fabs(r0)) geom = false;
        }
        if (geom) { NqlValue v; v.type = NQL_VAL_STRING; strncpy(v.v.sval, "geometric", 256); return v; }
    }

    /* Polynomial degree detection via repeated differencing */
    int n = (int)a->length;
    double* vals = (double*)malloc(n * sizeof(double));
    if (!vals) return nql_val_null();
    for (int i = 0; i < n; i++) vals[i] = nql_val_as_float(&a->items[i]);
    for (int deg = 1; deg < n && deg <= 10; deg++) {
        int len = n - deg;
        for (int i = 0; i < len; i++) vals[i] = vals[i + 1] - vals[i];
        bool constant = true;
        for (int i = 1; i < len; i++) if (fabs(vals[i] - vals[0]) > 1e-6) { constant = false; break; }
        if (constant) {
            free(vals);
            NqlValue v; v.type = NQL_VAL_STRING;
            snprintf(v.v.sval, 256, "polynomial:%d", deg);
            return v;
        }
    }
    free(vals);
    NqlValue v; v.type = NQL_VAL_STRING; strncpy(v.v.sval, "unknown", 256); return v;
}

/** SEQUENCE_CUMSUM(array) -- cumulative sums */
static NqlValue nql_fn_seq_partial_sums(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    NqlArray* r = nql_array_alloc(a->length);
    if (!r) return nql_val_null();
    double sum = 0;
    for (uint32_t i = 0; i < a->length; i++) {
        sum += nql_val_as_float(&a->items[i]);
        nql_array_push(r, nql_val_float(sum));
    }
    return nql_val_array(r);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_sequence_register_functions(void) {
    nql_register_func("SEQUENCE_DIFF",          nql_fn_seq_diff,         1, 1, "First differences");
    nql_register_func("SEQUENCE_RATIO",         nql_fn_seq_ratio,        1, 1, "Consecutive ratios");
    nql_register_func("SEQUENCE_IS_ARITHMETIC", nql_fn_seq_is_arith,     1, 1, "Test constant first differences");
    nql_register_func("SEQUENCE_IS_GEOMETRIC",  nql_fn_seq_is_geom,      1, 1, "Test constant ratios");
    nql_register_func("SEQUENCE_POLY_FIT",      nql_fn_seq_poly_fit,     1, 2, "Least-squares polynomial fit: [c0,c1,...]");
    nql_register_func("SEQUENCE_EXTEND",        nql_fn_seq_extend,       2, 2, "Predict next n terms");
    nql_register_func("SEQUENCE_DETECT",        nql_fn_seq_detect,       1, 1, "Detect pattern: arithmetic/geometric/polynomial:d/unknown");
    nql_register_func("SEQUENCE_PARTIAL_SUMS",  nql_fn_seq_partial_sums, 1, 1, "Cumulative partial sums");
}
