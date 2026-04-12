/**
 * nql_numerical.c - Numerical analysis functions for NQL
 * Part of the NQL mathematical expansion.
 * 
 * Implements: numerical integration (Simpson's, trapezoidal),
 * interpolation (Lagrange, Newton), root finding (bisection, Newton-Raphson),
 * and numerical differentiation.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);
NqlValue nql_val_float(double f);
NqlValue nql_val_null(void);
NqlValue nql_val_int(int64_t i);
static NqlArray* nql_array_alloc(uint32_t capacity);
static bool nql_array_push(NqlArray* a, NqlValue val);
static NqlValue nql_val_array(NqlArray* a);

/* ======================================================================
 * NUMERICAL INTEGRATION
 * ====================================================================== */

/** TRAPEZOID(y_array, h) -- trapezoidal rule integration.
 *  y_array: array of y values at evenly spaced x points.
 *  h: spacing between x points. */
static NqlValue nql_fn_trapezoid(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    
    NqlArray* y = args[0].v.array;
    double h = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    
    if (y->length < 2 || h <= 0.0) return nql_val_null();
    
    double sum = 0.0;
    for (uint32_t i = 0; i < y->length; i++) {
        double yi = (y->items[i].type == NQL_VAL_INT) ? (double)y->items[i].v.ival : y->items[i].v.fval;
        if (i == 0 || i == y->length - 1)
            sum += yi;
        else
            sum += 2.0 * yi;
    }
    
    return nql_val_float(sum * h / 2.0);
}

/** SIMPSON(y_array, h) -- Simpson's 1/3 rule integration.
 *  Requires odd number of points (even number of intervals). */
static NqlValue nql_fn_simpson(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    
    NqlArray* y = args[0].v.array;
    double h = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    
    if (y->length < 3 || y->length % 2 == 0 || h <= 0.0) return nql_val_null();
    
    double sum = 0.0;
    for (uint32_t i = 0; i < y->length; i++) {
        double yi = (y->items[i].type == NQL_VAL_INT) ? (double)y->items[i].v.ival : y->items[i].v.fval;
        if (i == 0 || i == y->length - 1)
            sum += yi;
        else if (i % 2 == 1)
            sum += 4.0 * yi;
        else
            sum += 2.0 * yi;
    }
    
    return nql_val_float(sum * h / 3.0);
}

/* ======================================================================
 * INTERPOLATION
 * ====================================================================== */

/** LAGRANGE_INTERP(x_arr, y_arr, x_target) -- Lagrange polynomial interpolation. */
static NqlValue nql_fn_lagrange_interp(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    
    NqlArray* x_arr = args[0].v.array;
    NqlArray* y_arr = args[1].v.array;
    double xt = (args[2].type == NQL_VAL_INT) ? (double)args[2].v.ival : args[2].v.fval;
    
    if (x_arr->length != y_arr->length || x_arr->length == 0 || x_arr->length > 100)
        return nql_val_null();
    
    uint32_t n = x_arr->length;
    double result = 0.0;
    
    for (uint32_t i = 0; i < n; i++) {
        double xi = (x_arr->items[i].type == NQL_VAL_INT) ? (double)x_arr->items[i].v.ival : x_arr->items[i].v.fval;
        double yi = (y_arr->items[i].type == NQL_VAL_INT) ? (double)y_arr->items[i].v.ival : y_arr->items[i].v.fval;
        
        double basis = 1.0;
        for (uint32_t j = 0; j < n; j++) {
            if (i == j) continue;
            double xj = (x_arr->items[j].type == NQL_VAL_INT) ? (double)x_arr->items[j].v.ival : x_arr->items[j].v.fval;
            if (fabs(xi - xj) < 1e-15) return nql_val_null();
            basis *= (xt - xj) / (xi - xj);
        }
        
        result += yi * basis;
    }
    
    return nql_val_float(result);
}

/** LINEAR_INTERP(x_arr, y_arr, x_target) -- piecewise linear interpolation. */
static NqlValue nql_fn_linear_interp(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    
    NqlArray* x_arr = args[0].v.array;
    NqlArray* y_arr = args[1].v.array;
    double xt = (args[2].type == NQL_VAL_INT) ? (double)args[2].v.ival : args[2].v.fval;
    
    if (x_arr->length != y_arr->length || x_arr->length < 2) return nql_val_null();
    
    uint32_t n = x_arr->length;
    
    /* Find bracketing interval (assumes sorted x) */
    for (uint32_t i = 0; i < n - 1; i++) {
        double x0 = (x_arr->items[i].type == NQL_VAL_INT) ? (double)x_arr->items[i].v.ival : x_arr->items[i].v.fval;
        double x1 = (x_arr->items[i+1].type == NQL_VAL_INT) ? (double)x_arr->items[i+1].v.ival : x_arr->items[i+1].v.fval;
        
        if (xt >= x0 && xt <= x1) {
            double y0 = (y_arr->items[i].type == NQL_VAL_INT) ? (double)y_arr->items[i].v.ival : y_arr->items[i].v.fval;
            double y1 = (y_arr->items[i+1].type == NQL_VAL_INT) ? (double)y_arr->items[i+1].v.ival : y_arr->items[i+1].v.fval;
            
            if (fabs(x1 - x0) < 1e-15) return nql_val_float(y0);
            double t = (xt - x0) / (x1 - x0);
            return nql_val_float(y0 + t * (y1 - y0));
        }
    }
    
    return nql_val_null();
}

/* ======================================================================
 * ROOT FINDING
 * ====================================================================== */

/** BISECT(y_array, x_lo, x_hi, n_points) -- find root by bisection on sampled data.
 *  y_array contains function values sampled at n_points evenly spaced points in [x_lo, x_hi].
 *  Returns x where y crosses zero (linear interpolation between sign changes). */
static NqlValue nql_fn_bisect(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    
    NqlArray* y = args[0].v.array;
    double x_lo = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    double x_hi = (args[2].type == NQL_VAL_INT) ? (double)args[2].v.ival : args[2].v.fval;
    
    if (y->length < 2 || x_hi <= x_lo) return nql_val_null();
    
    double h = (x_hi - x_lo) / (double)(y->length - 1);
    
    /* Find first sign change */
    for (uint32_t i = 0; i < y->length - 1; i++) {
        double y0 = (y->items[i].type == NQL_VAL_INT) ? (double)y->items[i].v.ival : y->items[i].v.fval;
        double y1 = (y->items[i+1].type == NQL_VAL_INT) ? (double)y->items[i+1].v.ival : y->items[i+1].v.fval;
        
        if (fabs(y0) < 1e-15) return nql_val_float(x_lo + i * h);
        if (y0 * y1 < 0.0) {
            /* Linear interpolation to find zero crossing */
            double t = y0 / (y0 - y1);
            return nql_val_float(x_lo + (i + t) * h);
        }
    }
    
    /* Check last point */
    double ylast = (y->items[y->length-1].type == NQL_VAL_INT) ?
        (double)y->items[y->length-1].v.ival : y->items[y->length-1].v.fval;
    if (fabs(ylast) < 1e-15) return nql_val_float(x_hi);
    
    return nql_val_null();
}

/* ======================================================================
 * NUMERICAL DIFFERENTIATION
 * ====================================================================== */

/** DIFF(y_array, h) -- numerical derivative using central differences.
 *  Returns array of derivative values (length = input length). */
static NqlValue nql_fn_diff(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    
    NqlArray* y = args[0].v.array;
    double h = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    
    if (y->length < 2 || h <= 0.0) return nql_val_null();
    
    NqlArray* result = nql_array_alloc(y->length);
    if (!result) return nql_val_null();
    
    for (uint32_t i = 0; i < y->length; i++) {
        double deriv;
        if (i == 0) {
            /* Forward difference */
            double y0 = (y->items[0].type == NQL_VAL_INT) ? (double)y->items[0].v.ival : y->items[0].v.fval;
            double y1 = (y->items[1].type == NQL_VAL_INT) ? (double)y->items[1].v.ival : y->items[1].v.fval;
            deriv = (y1 - y0) / h;
        } else if (i == y->length - 1) {
            /* Backward difference */
            double yn = (y->items[i].type == NQL_VAL_INT) ? (double)y->items[i].v.ival : y->items[i].v.fval;
            double yn1 = (y->items[i-1].type == NQL_VAL_INT) ? (double)y->items[i-1].v.ival : y->items[i-1].v.fval;
            deriv = (yn - yn1) / h;
        } else {
            /* Central difference */
            double yp = (y->items[i+1].type == NQL_VAL_INT) ? (double)y->items[i+1].v.ival : y->items[i+1].v.fval;
            double ym = (y->items[i-1].type == NQL_VAL_INT) ? (double)y->items[i-1].v.ival : y->items[i-1].v.fval;
            deriv = (yp - ym) / (2.0 * h);
        }
        nql_array_push(result, nql_val_float(deriv));
    }
    
    return nql_val_array(result);
}

/* ======================================================================
 * CUMULATIVE SUM / RUNNING OPERATIONS
 * ====================================================================== */

/** CUMSUM(array) -- cumulative sum. Returns array of running totals. */
static NqlValue nql_fn_cumsum(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    
    NqlArray* arr = args[0].v.array;
    NqlArray* result = nql_array_alloc(arr->length);
    if (!result) return nql_val_null();
    
    double sum = 0.0;
    for (uint32_t i = 0; i < arr->length; i++) {
        double val = (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
        sum += val;
        nql_array_push(result, nql_val_float(sum));
    }
    
    return nql_val_array(result);
}

/** CUMPROD(array) -- cumulative product. Returns array of running products. */
static NqlValue nql_fn_cumprod(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    
    NqlArray* arr = args[0].v.array;
    NqlArray* result = nql_array_alloc(arr->length);
    if (!result) return nql_val_null();
    
    double prod = 1.0;
    for (uint32_t i = 0; i < arr->length; i++) {
        double val = (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
        prod *= val;
        nql_array_push(result, nql_val_float(prod));
    }
    
    return nql_val_array(result);
}

/** LINSPACE(start, end, n) -- generate array of n evenly spaced values. */
static NqlValue nql_fn_linspace(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    
    double start = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double end   = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    int64_t n    = (args[2].type == NQL_VAL_INT) ? args[2].v.ival : (int64_t)args[2].v.fval;
    
    if (n < 2 || n > 10000) return nql_val_null();
    
    NqlArray* result = nql_array_alloc((uint32_t)n);
    if (!result) return nql_val_null();
    
    double step = (end - start) / (double)(n - 1);
    for (int64_t i = 0; i < n; i++) {
        nql_array_push(result, nql_val_float(start + i * step));
    }
    
    return nql_val_array(result);
}

/** DOT_PRODUCT(a, b) -- dot product of two arrays. */
static NqlValue nql_fn_dot_product(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    
    if (a->length != b->length || a->length == 0) return nql_val_null();
    
    double sum = 0.0;
    for (uint32_t i = 0; i < a->length; i++) {
        double ai = (a->items[i].type == NQL_VAL_INT) ? (double)a->items[i].v.ival : a->items[i].v.fval;
        double bi = (b->items[i].type == NQL_VAL_INT) ? (double)b->items[i].v.ival : b->items[i].v.fval;
        sum += ai * bi;
    }
    
    return nql_val_float(sum);
}

/** NORM(array [, p]) -- L-p norm of a vector. Default p=2 (Euclidean). */
static NqlValue nql_fn_norm(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc < 1 || argc > 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    
    NqlArray* a = args[0].v.array;
    double p = (argc >= 2) ? ((args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval) : 2.0;
    
    if (a->length == 0 || p < 1.0) return nql_val_null();
    
    double sum = 0.0;
    for (uint32_t i = 0; i < a->length; i++) {
        double val = (a->items[i].type == NQL_VAL_INT) ? (double)a->items[i].v.ival : a->items[i].v.fval;
        sum += pow(fabs(val), p);
    }
    
    return nql_val_float(pow(sum, 1.0 / p));
}

/** CROSS_PRODUCT(a, b) -- cross product of two 3D vectors. */
static NqlValue nql_fn_cross_product(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    
    if (a->length != 3 || b->length != 3) return nql_val_null();
    
    double a0 = (a->items[0].type == NQL_VAL_INT) ? (double)a->items[0].v.ival : a->items[0].v.fval;
    double a1 = (a->items[1].type == NQL_VAL_INT) ? (double)a->items[1].v.ival : a->items[1].v.fval;
    double a2 = (a->items[2].type == NQL_VAL_INT) ? (double)a->items[2].v.ival : a->items[2].v.fval;
    double b0 = (b->items[0].type == NQL_VAL_INT) ? (double)b->items[0].v.ival : b->items[0].v.fval;
    double b1 = (b->items[1].type == NQL_VAL_INT) ? (double)b->items[1].v.ival : b->items[1].v.fval;
    double b2 = (b->items[2].type == NQL_VAL_INT) ? (double)b->items[2].v.ival : b->items[2].v.fval;
    
    NqlArray* result = nql_array_alloc(3);
    if (!result) return nql_val_null();
    
    nql_array_push(result, nql_val_float(a1 * b2 - a2 * b1));
    nql_array_push(result, nql_val_float(a2 * b0 - a0 * b2));
    nql_array_push(result, nql_val_float(a0 * b1 - a1 * b0));
    
    return nql_val_array(result);
}

/* Register numerical analysis functions */
void nql_numerical_register_functions(void) {
    /* Integration */
    nql_register_func("TRAPEZOID",        nql_fn_trapezoid,        2, 2, "Trapezoidal rule integration (y_array, h)");
    nql_register_func("SIMPSON",          nql_fn_simpson,          2, 2, "Simpson's 1/3 rule integration (y_array, h)");
    
    /* Interpolation */
    nql_register_func("LAGRANGE_INTERP",  nql_fn_lagrange_interp,  3, 3, "Lagrange polynomial interpolation (x, y, target)");
    nql_register_func("LINEAR_INTERP",    nql_fn_linear_interp,    3, 3, "Piecewise linear interpolation (x, y, target)");
    
    /* Root finding */
    nql_register_func("BISECT",           nql_fn_bisect,           3, 3, "Find root by bisection on sampled data (y, x_lo, x_hi)");
    
    /* Differentiation */
    nql_register_func("DIFF",             nql_fn_diff,             2, 2, "Numerical derivative (y_array, h)");
    
    /* Cumulative operations */
    nql_register_func("CUMSUM",           nql_fn_cumsum,           1, 1, "Cumulative sum");
    nql_register_func("CUMPROD",          nql_fn_cumprod,          1, 1, "Cumulative product");
    
    /* Sequence generation */
    nql_register_func("LINSPACE",         nql_fn_linspace,         3, 3, "Generate evenly spaced values (start, end, n)");
    
    /* Vector operations */
    nql_register_func("DOT_PRODUCT",      nql_fn_dot_product,      2, 2, "Dot product of two arrays");
    nql_register_func("CROSS_PRODUCT",    nql_fn_cross_product,    2, 2, "Cross product of two 3D vectors");
    nql_register_func("NORM",             nql_fn_norm,             1, 2, "L-p norm (default p=2, Euclidean)");
}
