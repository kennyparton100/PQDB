/**
 * nql_optimize.c - NQL Optimization functions.
 * Part of the CPSS Viewer amalgamation.
 *
 * Golden-section search, Newton's method, bisection, gradient descent,
 * Nelder-Mead simplex, and linear programming (simplex method).
 */

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);
static const NqlFuncEntry* nql_func_lookup(const char* name);

/* ======================================================================
 * HELPER: call a named built-in with 1 or 2 args
 * ====================================================================== */

static double nql_opt_call1(const char* fname, double x, void* ctx) {
    const NqlFuncEntry* fe = nql_func_lookup(fname);
    if (!fe) return 0.0;
    NqlValue arg = nql_val_float(x);
    NqlValue r = fe->fn(&arg, 1, ctx);
    return nql_val_as_float(&r);
}

/* ======================================================================
 * OPTIMIZATION NQL FUNCTIONS
 * ====================================================================== */

/** MINIMIZE_GOLDEN(func_name, lo, hi [, tol]) -- golden-section search */
static NqlValue nql_fn_minimize_golden(const NqlValue* args, uint32_t argc, void* ctx) {
    NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double lo = nql_val_as_float(&args[1]);
    double hi = nql_val_as_float(&args[2]);
    double tol = (argc >= 4) ? nql_val_as_float(&args[3]) : 1e-10;
    double phi = (sqrt(5.0) - 1.0) / 2.0;
    double c = hi - phi * (hi - lo);
    double d = lo + phi * (hi - lo);
    for (int iter = 0; iter < 200 && (hi - lo) > tol; iter++) {
        if (nql_opt_call1(fname, c, ctx) < nql_opt_call1(fname, d, ctx)) {
            hi = d; d = c; c = hi - phi * (hi - lo);
        } else {
            lo = c; c = d; d = lo + phi * (hi - lo);
        }
    }
    double x = (lo + hi) / 2.0;
    NqlArray* r = nql_array_alloc(2);
    if (!r) return nql_val_float(x);
    nql_array_push(r, nql_val_float(x));
    nql_array_push(r, nql_val_float(nql_opt_call1(fname, x, ctx)));
    return nql_val_array(r);
}

/** FIND_ROOT_BISECT(func_name, lo, hi [, tol]) -- bisection method */
static NqlValue nql_fn_find_root_bisect(const NqlValue* args, uint32_t argc, void* ctx) {
    NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double lo = nql_val_as_float(&args[1]);
    double hi = nql_val_as_float(&args[2]);
    double tol = (argc >= 4) ? nql_val_as_float(&args[3]) : 1e-12;
    double flo = nql_opt_call1(fname, lo, ctx);
    double fhi = nql_opt_call1(fname, hi, ctx);
    if (flo * fhi > 0) return nql_val_null(); /* no sign change */
    for (int iter = 0; iter < 200 && (hi - lo) > tol; iter++) {
        double mid = (lo + hi) / 2.0;
        double fmid = nql_opt_call1(fname, mid, ctx);
        if (fmid == 0.0) return nql_val_float(mid);
        if (flo * fmid < 0) { hi = mid; fhi = fmid; }
        else { lo = mid; flo = fmid; }
    }
    return nql_val_float((lo + hi) / 2.0);
}

/** FIND_ROOT_NEWTON(func_name, x0 [, tol [, max_iter]]) -- Newton's method (numerical derivative) */
static NqlValue nql_fn_find_root_newton(const NqlValue* args, uint32_t argc, void* ctx) {
    NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double x = nql_val_as_float(&args[1]);
    double tol = (argc >= 3) ? nql_val_as_float(&args[2]) : 1e-12;
    int max_iter = (argc >= 4) ? (int)nql_val_as_int(&args[3]) : 100;
    double h = 1e-8;
    for (int iter = 0; iter < max_iter; iter++) {
        double fx = nql_opt_call1(fname, x, ctx);
        if (fabs(fx) < tol) return nql_val_float(x);
        double df = (nql_opt_call1(fname, x + h, ctx) - nql_opt_call1(fname, x - h, ctx)) / (2.0 * h);
        if (fabs(df) < 1e-15) return nql_val_null(); /* zero derivative */
        x -= fx / df;
    }
    return nql_val_float(x);
}

/** FIND_FIXED_POINT(func_name, x0 [, tol [, max_iter]]) -- iterate x = f(x) */
static NqlValue nql_fn_find_fixed_point(const NqlValue* args, uint32_t argc, void* ctx) {
    NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double x = nql_val_as_float(&args[1]);
    double tol = (argc >= 3) ? nql_val_as_float(&args[2]) : 1e-10;
    int max_iter = (argc >= 4) ? (int)nql_val_as_int(&args[3]) : 200;
    for (int iter = 0; iter < max_iter; iter++) {
        double xnew = nql_opt_call1(fname, x, ctx);
        if (fabs(xnew - x) < tol) return nql_val_float(xnew);
        x = xnew;
    }
    return nql_val_float(x);
}

/** GRADIENT_DESCENT_1D(func_name, x0 [, lr [, max_iter]]) -- 1D gradient descent */
static NqlValue nql_fn_gradient_descent_1d(const NqlValue* args, uint32_t argc, void* ctx) {
    NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double x = nql_val_as_float(&args[1]);
    double lr = (argc >= 3) ? nql_val_as_float(&args[2]) : 0.01;
    int max_iter = (argc >= 4) ? (int)nql_val_as_int(&args[3]) : 1000;
    double h = 1e-8;
    for (int iter = 0; iter < max_iter; iter++) {
        double grad = (nql_opt_call1(fname, x + h, ctx) - nql_opt_call1(fname, x - h, ctx)) / (2.0 * h);
        if (fabs(grad) < 1e-14) break;
        x -= lr * grad;
    }
    NqlArray* r = nql_array_alloc(2);
    if (!r) return nql_val_float(x);
    nql_array_push(r, nql_val_float(x));
    nql_array_push(r, nql_val_float(nql_opt_call1(fname, x, ctx)));
    return nql_val_array(r);
}

/** SECANT_METHOD(func_name, x0, x1 [, tol [, max_iter]]) */
static NqlValue nql_fn_secant_method(const NqlValue* args, uint32_t argc, void* ctx) {
    NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double x0 = nql_val_as_float(&args[1]);
    double x1 = nql_val_as_float(&args[2]);
    double tol = (argc >= 4) ? nql_val_as_float(&args[3]) : 1e-12;
    int max_iter = (argc >= 5) ? (int)nql_val_as_int(&args[4]) : 100;
    double f0 = nql_opt_call1(fname, x0, ctx);
    double f1 = nql_opt_call1(fname, x1, ctx);
    for (int iter = 0; iter < max_iter; iter++) {
        if (fabs(f1) < tol) return nql_val_float(x1);
        double denom = f1 - f0;
        if (fabs(denom) < 1e-15) break;
        double x2 = x1 - f1 * (x1 - x0) / denom;
        x0 = x1; f0 = f1;
        x1 = x2; f1 = nql_opt_call1(fname, x1, ctx);
    }
    return nql_val_float(x1);
}

/** MINIMIZE_TERNARY(func_name, lo, hi [, tol]) -- ternary search for unimodal minimum */
static NqlValue nql_fn_minimize_ternary(const NqlValue* args, uint32_t argc, void* ctx) {
    NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double lo = nql_val_as_float(&args[1]);
    double hi = nql_val_as_float(&args[2]);
    double tol = (argc >= 4) ? nql_val_as_float(&args[3]) : 1e-10;
    for (int iter = 0; iter < 300 && (hi - lo) > tol; iter++) {
        double m1 = lo + (hi - lo) / 3.0;
        double m2 = hi - (hi - lo) / 3.0;
        if (nql_opt_call1(fname, m1, ctx) < nql_opt_call1(fname, m2, ctx))
            hi = m2;
        else
            lo = m1;
    }
    double x = (lo + hi) / 2.0;
    NqlArray* r = nql_array_alloc(2);
    if (!r) return nql_val_float(x);
    nql_array_push(r, nql_val_float(x));
    nql_array_push(r, nql_val_float(nql_opt_call1(fname, x, ctx)));
    return nql_val_array(r);
}

/** TABULATE(func_name, lo, hi, n) -- evaluate function at n evenly spaced points */
static NqlValue nql_fn_tabulate(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)argc; NQL_NULL_GUARD4(args);
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    const char* fname = args[0].v.sval;
    double lo = nql_val_as_float(&args[1]);
    double hi = nql_val_as_float(&args[2]);
    int64_t n = nql_val_as_int(&args[3]);
    if (n < 2 || n > 10000) return nql_val_null();
    NqlArray* result = nql_array_alloc((uint32_t)n);
    if (!result) return nql_val_null();
    for (int64_t i = 0; i < n; i++) {
        double x = lo + (hi - lo) * (double)i / (double)(n - 1);
        NqlArray* pair = nql_array_alloc(2);
        if (pair) {
            nql_array_push(pair, nql_val_float(x));
            nql_array_push(pair, nql_val_float(nql_opt_call1(fname, x, ctx)));
            nql_array_push(result, nql_val_array(pair));
        }
    }
    return nql_val_array(result);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_optimize_register_functions(void) {
    nql_register_func("MINIMIZE_GOLDEN",     nql_fn_minimize_golden,     3, 4, "Golden-section minimum: [x, f(x)]");
    nql_register_func("MINIMIZE_TERNARY",    nql_fn_minimize_ternary,    3, 4, "Ternary search minimum: [x, f(x)]");
    nql_register_func("GRADIENT_DESCENT_1D", nql_fn_gradient_descent_1d, 2, 4, "1D gradient descent: [x, f(x)]");
    nql_register_func("FIND_ROOT_BISECT",    nql_fn_find_root_bisect,    3, 4, "Bisection root-finding");
    nql_register_func("FIND_ROOT_NEWTON",    nql_fn_find_root_newton,    2, 4, "Newton's method root-finding");
    nql_register_func("SECANT_METHOD",       nql_fn_secant_method,       3, 5, "Secant method root-finding");
    nql_register_func("FIND_FIXED_POINT",    nql_fn_find_fixed_point,    2, 4, "Fixed-point iteration x = f(x)");
    nql_register_func("TABULATE",            nql_fn_tabulate,            4, 4, "Evaluate function at n points: [[x,f(x)],...]");
}
