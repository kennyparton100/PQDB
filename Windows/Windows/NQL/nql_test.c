/**
 * nql_test.c - Comprehensive NQL self-test suite.
 *
 * Provides TEST_NQL() which exercises every major module and prints
 * PASS/FAIL for each test case, plus a summary.  Sub-test files are
 * included below; each defines static void nql_test_xxx() functions.
 *
 * Usage:  query TEST_NQL()
 */

/* -- Test harness -- */

typedef struct { uint32_t pass; uint32_t fail; uint32_t total; } NqlTestCtx;

static void nql_test_check(NqlTestCtx* tc, const char* label, bool ok) {
    tc->total++;
    if (ok) { tc->pass++; printf("  PASS: %s\n", label); }
    else    { tc->fail++; printf("  FAIL: %s\n", label); }
}

/* Call a named NQL function with 0-5 args */
static NqlValue nql_test_call(const char* name, const NqlValue* args, uint32_t argc, void* ctx) {
    const NqlFuncEntry* fe = nql_func_lookup(name);
    if (!fe) return nql_val_null();
    return fe->fn(args, argc, ctx);
}
static NqlValue tc0(const char* n, void* c) { return nql_test_call(n, NULL, 0, c); }
static NqlValue tc1(const char* n, NqlValue a, void* c) { return nql_test_call(n, &a, 1, c); }
static NqlValue tc2(const char* n, NqlValue a, NqlValue b, void* c) {
    NqlValue av[2] = {a, b}; return nql_test_call(n, av, 2, c);
}
static NqlValue tc3(const char* n, NqlValue a, NqlValue b, NqlValue cc, void* c) {
    NqlValue av[3] = {a, b, cc}; return nql_test_call(n, av, 3, c);
}
static NqlValue tc4(const char* n, NqlValue a, NqlValue b, NqlValue cc, NqlValue d, void* c) {
    NqlValue av[4] = {a, b, cc, d}; return nql_test_call(n, av, 4, c);
}
static NqlValue tc5(const char* n, NqlValue a, NqlValue b, NqlValue cc, NqlValue d, NqlValue e, void* c) {
    NqlValue av[5] = {a, b, cc, d, e}; return nql_test_call(n, av, 5, c);
}

/* Shorthand helpers */
#define VI(x)   nql_val_int((int64_t)(x))
#define VF(x)   nql_val_float((double)(x))
#define VB(x)   nql_val_bool(x)
#define VS(x)   nql_val_str(x)
#define VN      nql_val_null()

static void tc_int_eq(NqlTestCtx* tc, const char* lbl, NqlValue r, int64_t ex) {
    nql_test_check(tc, lbl, r.type == NQL_VAL_INT && r.v.ival == ex);
}
static void tc_near(NqlTestCtx* tc, const char* lbl, NqlValue r, double ex, double tol) {
    nql_test_check(tc, lbl, fabs(nql_val_as_float(&r) - ex) <= tol);
}
static void tc_bool(NqlTestCtx* tc, const char* lbl, NqlValue r, bool ex) {
    nql_test_check(tc, lbl, r.type == NQL_VAL_BOOL && r.v.bval == ex);
}
static void tc_null(NqlTestCtx* tc, const char* lbl, NqlValue r) {
    nql_test_check(tc, lbl, r.type == NQL_VAL_NULL);
}
static void tc_notnull(NqlTestCtx* tc, const char* lbl, NqlValue r) {
    nql_test_check(tc, lbl, r.type != NQL_VAL_NULL);
}
static void tc_arrlen(NqlTestCtx* tc, const char* lbl, NqlValue r, uint32_t ex) {
    nql_test_check(tc, lbl, r.type == NQL_VAL_ARRAY && r.v.array && r.v.array->length == ex);
}
static void tc_str(NqlTestCtx* tc, const char* lbl, NqlValue r, const char* sub) {
    nql_test_check(tc, lbl, r.type == NQL_VAL_STRING && r.v.sval && strstr(r.v.sval, sub));
}
static void tc_registered(NqlTestCtx* tc, const char* name) {
    char lbl[80]; snprintf(lbl, sizeof(lbl), "%s registered", name);
    nql_test_check(tc, lbl, nql_func_lookup(name) != NULL);
}
/* Make a simple float array */
static NqlValue tc_farr(const double* vals, int n) {
    NqlArray* a = nql_array_alloc((uint32_t)n);
    for (int i = 0; i < n; i++) nql_array_push(a, VF(vals[i]));
    return nql_val_array(a);
}
static NqlValue tc_iarr(const int64_t* vals, int n) {
    NqlArray* a = nql_array_alloc((uint32_t)n);
    for (int i = 0; i < n; i++) nql_array_push(a, VI(vals[i]));
    return nql_val_array(a);
}

/* ======================================================================
 * Include sub-test files (each defines static void nql_test_xxx())
 * ====================================================================== */
#include "Tests/nql_test_core.c"
#include "Tests/nql_test_types.c"
#include "Tests/nql_test_modules.c"
#include "Tests/nql_test_safety.c"

/* ======================================================================
 * TEST_NQL() -- main entry point
 * ====================================================================== */
static NqlValue nql_fn_test_nql(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)args; (void)argc;
    NqlTestCtx tc = {0, 0, 0};
    printf("\n+----------------------------------------------+\n");
    printf("|        NQL Comprehensive Self-Test           |\n");
    printf("+----------------------------------------------+\n");
    /* Registry */
    printf("\n--- Registry ---\n");
    { NqlValue r = tc0("NQL_FUNC_COUNT", ctx);
      int64_t cnt = nql_val_as_int(&r);
      printf("  Function count: %lld\n", (long long)cnt);
      nql_test_check(&tc, "NQL_FUNC_COUNT >= 500", cnt >= 500); }
    /* Core + math + arrays + strings */
    nql_test_core_arithmetic(&tc, ctx);
    nql_test_number_theory(&tc, ctx);
    nql_test_trig_math(&tc, ctx);
    nql_test_arrays(&tc, ctx);
    nql_test_strings(&tc, ctx);
    /* Types: complex, matrix, symbolic, rational, vector, interval, series, graph */
    nql_test_complex(&tc, ctx);
    nql_test_matrix(&tc, ctx);
    nql_test_symbolic(&tc, ctx);
    nql_test_rational(&tc, ctx);
    nql_test_vector(&tc, ctx);
    nql_test_interval(&tc, ctx);
    nql_test_series(&tc, ctx);
    nql_test_graph(&tc, ctx);
    /* Modules: stats, distributions, combinatorics, special, sparse, dist-obj, ODE, seq, cfrac, decompose, field, EC, quat, crypto, physics */
    nql_test_statistics(&tc, ctx);
    nql_test_distributions(&tc, ctx);
    nql_test_combinatorics(&tc, ctx);
    nql_test_special_funcs(&tc, ctx);
    nql_test_sparse(&tc, ctx);
    nql_test_dist_objects(&tc, ctx);
    nql_test_ode(&tc, ctx);
    nql_test_sequence(&tc, ctx);
    nql_test_cfrac(&tc, ctx);
    nql_test_decompose(&tc, ctx);
    nql_test_field(&tc, ctx);
    nql_test_elliptic(&tc, ctx);
    nql_test_quaternion(&tc, ctx);
    nql_test_crypto(&tc, ctx);
    nql_test_physics(&tc, ctx);
    /* Safety: error handling, testing framework, nulls, overflow, boundaries */
    nql_test_error_handling(&tc, ctx);
    nql_test_testing_framework(&tc, ctx);
    nql_test_null_safety(&tc, ctx);
    nql_test_overflow(&tc, ctx);
    nql_test_boundary(&tc, ctx);
    nql_test_empty_arrays(&tc, ctx);
    /* Summary */
    printf("\n+----------------------------------------------+\n");
    if (tc.fail == 0)
        printf("|  ALL %u TESTS PASSED                        |\n", tc.total);
    else
        printf("|  %u PASSED, %u FAILED (of %u)                |\n", tc.pass, tc.fail, tc.total);
    printf("+----------------------------------------------+\n");
    fflush(stdout);
    return nql_val_int((int64_t)tc.fail);
}

void nql_test_register_functions(void) {
    nql_register_func("TEST_NQL", nql_fn_test_nql, 0, 0, "Run comprehensive NQL self-test suite");
}
