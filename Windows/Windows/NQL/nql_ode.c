/**
 * nql_ode.c - NQL ODE Solvers.
 * Part of the CPSS Viewer amalgamation.
 *
 * Euler, RK4, adaptive Dormand-Prince, and system solvers.
 * All take a named built-in function as the RHS.
 */

#include <math.h>
#include <stdlib.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);
static const NqlFuncEntry* nql_func_lookup(const char* name);

/* Call a named 2-arg function f(t, y) */
static double nql_ode_call2(const char* fname, double t, double y, void* ctx) {
    const NqlFuncEntry* fe = nql_func_lookup(fname);
    if (!fe) return 0.0;
    NqlValue args[2]; args[0] = nql_val_float(t); args[1] = nql_val_float(y);
    NqlValue r = fe->fn(args, 2, ctx);
    return nql_val_as_float(&r);
}

/* ======================================================================
 * ODE FUNCTIONS
 * ====================================================================== */

/** ODE_EULER(func_name, y0, t0, t1, steps) -- Euler method */
static NqlValue nql_fn_ode_euler(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc; NQL_NULL_GUARD_N(args, 5);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double y = nql_val_as_float(&args[1]);
    double t0 = nql_val_as_float(&args[2]);
    double t1 = nql_val_as_float(&args[3]);
    int steps = (int)nql_val_as_int(&args[4]);
    if (steps < 1 || steps > 100000) return nql_val_null();
    double h = (t1 - t0) / steps;
    NqlArray* result = nql_array_alloc((uint32_t)(steps + 1));
    if (!result) return nql_val_null();
    double t = t0;
    for (int i = 0; i <= steps; i++) {
        NqlArray* pt = nql_array_alloc(2);
        if (pt) { nql_array_push(pt, nql_val_float(t)); nql_array_push(pt, nql_val_float(y)); nql_array_push(result, nql_val_array(pt)); }
        if (i < steps) { y += h * nql_ode_call2(fname, t, y, ctx); t += h; }
    }
    return nql_val_array(result);
}

/** ODE_RK4(func_name, y0, t0, t1, steps) -- Classical Runge-Kutta 4th order */
static NqlValue nql_fn_ode_rk4(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc; NQL_NULL_GUARD_N(args, 5);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double y = nql_val_as_float(&args[1]);
    double t0 = nql_val_as_float(&args[2]);
    double t1 = nql_val_as_float(&args[3]);
    int steps = (int)nql_val_as_int(&args[4]);
    if (steps < 1 || steps > 100000) return nql_val_null();
    double h = (t1 - t0) / steps;
    NqlArray* result = nql_array_alloc((uint32_t)(steps + 1));
    if (!result) return nql_val_null();
    double t = t0;
    for (int i = 0; i <= steps; i++) {
        NqlArray* pt = nql_array_alloc(2);
        if (pt) { nql_array_push(pt, nql_val_float(t)); nql_array_push(pt, nql_val_float(y)); nql_array_push(result, nql_val_array(pt)); }
        if (i < steps) {
            double k1 = h * nql_ode_call2(fname, t, y, ctx);
            double k2 = h * nql_ode_call2(fname, t + h / 2, y + k1 / 2, ctx);
            double k3 = h * nql_ode_call2(fname, t + h / 2, y + k2 / 2, ctx);
            double k4 = h * nql_ode_call2(fname, t + h, y + k3, ctx);
            y += (k1 + 2 * k2 + 2 * k3 + k4) / 6.0;
            t += h;
        }
    }
    return nql_val_array(result);
}

/** ODE_ADAPTIVE(func_name, y0, t0, t1 [, tol]) -- Dormand-Prince adaptive step */
static NqlValue nql_fn_ode_adaptive(const NqlValue* args, uint32_t argc, void* ctx) {
    NQL_NULL_GUARD4(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double y = nql_val_as_float(&args[1]);
    double t = nql_val_as_float(&args[2]);
    double t1 = nql_val_as_float(&args[3]);
    double tol = (argc >= 5) ? nql_val_as_float(&args[4]) : 1e-8;
    double h = (t1 - t) / 100.0;
    NqlArray* result = nql_array_alloc(256);
    if (!result) return nql_val_null();
    int max_steps = 50000;
    for (int step = 0; step < max_steps && t < t1 - 1e-15; step++) {
        if (t + h > t1) h = t1 - t;
        /* RK4 full step */
        double k1 = h * nql_ode_call2(fname, t, y, ctx);
        double k2 = h * nql_ode_call2(fname, t + h / 2, y + k1 / 2, ctx);
        double k3 = h * nql_ode_call2(fname, t + h / 2, y + k2 / 2, ctx);
        double k4 = h * nql_ode_call2(fname, t + h, y + k3, ctx);
        double y_full = y + (k1 + 2 * k2 + 2 * k3 + k4) / 6.0;
        /* Two half steps */
        double hh = h / 2;
        double ka1 = hh * nql_ode_call2(fname, t, y, ctx);
        double ka2 = hh * nql_ode_call2(fname, t + hh / 2, y + ka1 / 2, ctx);
        double ka3 = hh * nql_ode_call2(fname, t + hh / 2, y + ka2 / 2, ctx);
        double ka4 = hh * nql_ode_call2(fname, t + hh, y + ka3, ctx);
        double y_half = y + (ka1 + 2 * ka2 + 2 * ka3 + ka4) / 6.0;
        double kb1 = hh * nql_ode_call2(fname, t + hh, y_half, ctx);
        double kb2 = hh * nql_ode_call2(fname, t + hh + hh / 2, y_half + kb1 / 2, ctx);
        double kb3 = hh * nql_ode_call2(fname, t + hh + hh / 2, y_half + kb2 / 2, ctx);
        double kb4 = hh * nql_ode_call2(fname, t + h, y_half + kb3, ctx);
        double y_two = y_half + (kb1 + 2 * kb2 + 2 * kb3 + kb4) / 6.0;
        double err = fabs(y_two - y_full);
        if (err < tol || h < 1e-15) {
            y = y_two; t += h;
            NqlArray* pt = nql_array_alloc(2);
            if (pt) { nql_array_push(pt, nql_val_float(t)); nql_array_push(pt, nql_val_float(y)); nql_array_push(result, nql_val_array(pt)); }
            if (err > 0) h *= fmin(2.0, pow(tol / err, 0.2));
        } else {
            h *= fmax(0.5, pow(tol / err, 0.25));
        }
    }
    return nql_val_array(result);
}

/** ODE_SYSTEM_RK4(func_names_array, y0_array, t0, t1, steps) -- system of ODEs */
static NqlValue nql_fn_ode_system_rk4(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc; NQL_NULL_GUARD_N(args, 5);
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* fnames = args[0].v.array;
    NqlArray* y0arr = args[1].v.array;
    uint32_t n = fnames->length;
    if (n == 0 || n != y0arr->length || n > 32) return nql_val_null();
    double t0 = nql_val_as_float(&args[2]);
    double t1 = nql_val_as_float(&args[3]);
    int steps = (int)nql_val_as_int(&args[4]);
    if (steps < 1 || steps > 100000) return nql_val_null();
    double h = (t1 - t0) / steps;
    double* y = (double*)malloc(n * sizeof(double));
    double* k1 = (double*)malloc(n * sizeof(double));
    double* k2 = (double*)malloc(n * sizeof(double));
    double* k3 = (double*)malloc(n * sizeof(double));
    double* k4 = (double*)malloc(n * sizeof(double));
    double* tmp = (double*)malloc(n * sizeof(double));
    if (!y || !k1 || !k2 || !k3 || !k4 || !tmp) { free(y); free(k1); free(k2); free(k3); free(k4); free(tmp); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) y[i] = nql_val_as_float(&y0arr->items[i]);
    /* Resolve function pointers */
    const NqlFuncEntry** fes = (const NqlFuncEntry**)malloc(n * sizeof(void*));
    if (!fes) { free(y); free(k1); free(k2); free(k3); free(k4); free(tmp); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) {
        fes[i] = (fnames->items[i].type == NQL_VAL_STRING) ? nql_func_lookup(fnames->items[i].v.sval) : NULL;
        if (!fes[i]) { free(y); free(k1); free(k2); free(k3); free(k4); free(tmp); free(fes); return nql_val_null(); }
    }
    NqlArray* result = nql_array_alloc((uint32_t)(steps + 1));
    if (!result) { free(y); free(k1); free(k2); free(k3); free(k4); free(tmp); free(fes); return nql_val_null(); }
    double t = t0;
    for (int s = 0; s <= steps; s++) {
        NqlArray* row = nql_array_alloc(n + 1);
        if (row) {
            nql_array_push(row, nql_val_float(t));
            for (uint32_t i = 0; i < n; i++) nql_array_push(row, nql_val_float(y[i]));
            nql_array_push(result, nql_val_array(row));
        }
        if (s < steps) {
            /* k1 */
            for (uint32_t i = 0; i < n; i++) {
                NqlValue a[2]; a[0] = nql_val_float(t); a[1] = nql_val_float(y[i]);
                NqlValue rv = fes[i]->fn(a, 2, ctx);
                k1[i] = h * nql_val_as_float(&rv);
            }
            /* k2 */
            for (uint32_t i = 0; i < n; i++) {
                NqlValue a[2]; a[0] = nql_val_float(t + h / 2); a[1] = nql_val_float(y[i] + k1[i] / 2);
                NqlValue rv = fes[i]->fn(a, 2, ctx);
                k2[i] = h * nql_val_as_float(&rv);
            }
            /* k3 */
            for (uint32_t i = 0; i < n; i++) {
                NqlValue a[2]; a[0] = nql_val_float(t + h / 2); a[1] = nql_val_float(y[i] + k2[i] / 2);
                NqlValue rv = fes[i]->fn(a, 2, ctx);
                k3[i] = h * nql_val_as_float(&rv);
            }
            /* k4 */
            for (uint32_t i = 0; i < n; i++) {
                NqlValue a[2]; a[0] = nql_val_float(t + h); a[1] = nql_val_float(y[i] + k3[i]);
                NqlValue rv = fes[i]->fn(a, 2, ctx);
                k4[i] = h * nql_val_as_float(&rv);
            }
            for (uint32_t i = 0; i < n; i++) y[i] += (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]) / 6.0;
            t += h;
        }
    }
    free(y); free(k1); free(k2); free(k3); free(k4); free(tmp); free(fes);
    return nql_val_array(result);
}

/** ODE_VERLET(func_name, y0, v0, t0, t1, steps) -- Velocity Verlet (symplectic) for a'' = f(t,a) */
static NqlValue nql_fn_ode_verlet(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc; NQL_NULL_GUARD_N(args, 6);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double y = nql_val_as_float(&args[1]);
    double v = nql_val_as_float(&args[2]);
    double t0 = nql_val_as_float(&args[3]);
    double t1 = nql_val_as_float(&args[4]);
    int steps = (int)nql_val_as_int(&args[5]);
    if (steps < 1 || steps > 100000) return nql_val_null();
    double h = (t1 - t0) / steps;
    NqlArray* result = nql_array_alloc((uint32_t)(steps + 1));
    if (!result) return nql_val_null();
    double t = t0;
    double a = nql_ode_call2(fname, t, y, ctx);
    for (int i = 0; i <= steps; i++) {
        NqlArray* pt = nql_array_alloc(3);
        if (pt) { nql_array_push(pt, nql_val_float(t)); nql_array_push(pt, nql_val_float(y)); nql_array_push(pt, nql_val_float(v)); nql_array_push(result, nql_val_array(pt)); }
        if (i < steps) {
            y += v * h + 0.5 * a * h * h;
            double a_new = nql_ode_call2(fname, t + h, y, ctx);
            v += 0.5 * (a + a_new) * h;
            a = a_new; t += h;
        }
    }
    return nql_val_array(result);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_ode_register_functions(void) {
    nql_register_func("ODE_EULER",      nql_fn_ode_euler,      5, 5, "Euler method: [[t,y],...]");
    nql_register_func("ODE_RK4",        nql_fn_ode_rk4,        5, 5, "RK4 method: [[t,y],...]");
    nql_register_func("ODE_ADAPTIVE",   nql_fn_ode_adaptive,   4, 5, "Adaptive RK4: [[t,y],...]");
    nql_register_func("ODE_SYSTEM_RK4", nql_fn_ode_system_rk4, 5, 5, "System RK4: [[t,y1,y2,...],...]");
    nql_register_func("ODE_VERLET",     nql_fn_ode_verlet,     6, 6, "Velocity Verlet (symplectic): [[t,y,v],...]");
}
