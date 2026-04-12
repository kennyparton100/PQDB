/**
 * nql_combinatorics.c - Combinatorics functions for NQL
 * Part of the NQL mathematical expansion - Phase 10.
 *
 * Implements: permutations, combinations, derangements, Catalan numbers,
 * Stirling numbers, Bell numbers, integer partitions, multinomial coefficients.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

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
 * PERMUTATIONS & COMBINATIONS
 * ====================================================================== */

/** PERM(n, k) -- number of k-permutations of n: n!/(n-k)! */
static NqlValue nql_fn_perm(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    if (argc != 2) return nql_val_null();
    int64_t n = (args[0].type == NQL_VAL_INT) ? args[0].v.ival : (int64_t)args[0].v.fval;
    int64_t k = (args[1].type == NQL_VAL_INT) ? args[1].v.ival : (int64_t)args[1].v.fval;
    if (n < 0 || k < 0 || k > n) return nql_val_null();
    double result = 1.0;
    for (int64_t i = 0; i < k; i++)
        result *= (double)(n - i);
    if (result <= 9.007199254740992e15) /* fits int64 exactly */
        return nql_val_int((int64_t)result);
    return nql_val_float(result);
}

/** COMB(n, k) -- binomial coefficient C(n,k) = n!/(k!(n-k)!) */
static NqlValue nql_fn_comb(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    if (argc != 2) return nql_val_null();
    int64_t n = (args[0].type == NQL_VAL_INT) ? args[0].v.ival : (int64_t)args[0].v.fval;
    int64_t k = (args[1].type == NQL_VAL_INT) ? args[1].v.ival : (int64_t)args[1].v.fval;
    if (n < 0 || k < 0 || k > n) return nql_val_null();
    if (k > n - k) k = n - k;
    double result = 1.0;
    for (int64_t i = 0; i < k; i++)
        result = result * (double)(n - i) / (double)(i + 1);
    if (result <= 9.007199254740992e15)
        return nql_val_int((int64_t)(result + 0.5));
    return nql_val_float(result);
}

/** MULTICHOOSE(n, k) -- multiset coefficient C(n+k-1, k) */
static NqlValue nql_fn_multichoose(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    if (argc != 2) return nql_val_null();
    int64_t n = (args[0].type == NQL_VAL_INT) ? args[0].v.ival : (int64_t)args[0].v.fval;
    int64_t k = (args[1].type == NQL_VAL_INT) ? args[1].v.ival : (int64_t)args[1].v.fval;
    if (n < 0 || k < 0) return nql_val_null();
    NqlValue new_args[2] = { nql_val_int(n + k - 1), nql_val_int(k) };
    return nql_fn_comb(new_args, 2, ctx);
}

/* ======================================================================
 * NAMED SEQUENCES
 * ====================================================================== */

/** DERANGEMENT(n) -- number of derangements D(n) = n! * sigma(-1)^k/k! */
static NqlValue nql_fn_derangement(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    int64_t n = (args[0].type == NQL_VAL_INT) ? args[0].v.ival : (int64_t)args[0].v.fval;
    if (n < 0) return nql_val_null();
    if (n == 0) return nql_val_int(1);
    if (n == 1) return nql_val_int(0);
    /* Use recurrence: D(n) = (n-1)(D(n-1) + D(n-2)) */
    double d_prev2 = 1.0, d_prev1 = 0.0;
    for (int64_t i = 2; i <= n; i++) {
        double d = (double)(i - 1) * (d_prev1 + d_prev2);
        d_prev2 = d_prev1;
        d_prev1 = d;
    }
    if (d_prev1 <= 9.007199254740992e15)
        return nql_val_int((int64_t)(d_prev1 + 0.5));
    return nql_val_float(d_prev1);
}

/** CATALAN(n) -- C(n) = C(2n,n)/(n+1) */
static NqlValue nql_fn_catalan(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    int64_t n = (args[0].type == NQL_VAL_INT) ? args[0].v.ival : (int64_t)args[0].v.fval;
    if (n < 0) return nql_val_null();
    NqlValue comb_args[2] = { nql_val_int(2 * n), nql_val_int(n) };
    NqlValue c = nql_fn_comb(comb_args, 2, ctx);
    if (c.type == NQL_VAL_INT) return nql_val_int(c.v.ival / (n + 1));
    if (c.type == NQL_VAL_FLOAT) return nql_val_float(c.v.fval / (double)(n + 1));
    return nql_val_null();
}

/** BELL(n) -- Bell number via triangle */
static NqlValue nql_fn_bell(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    int64_t n = (args[0].type == NQL_VAL_INT) ? args[0].v.ival : (int64_t)args[0].v.fval;
    if (n < 0 || n > 25) return nql_val_null(); /* overflow guard */
    if (n == 0) return nql_val_int(1);
    /* Bell triangle */
    double* prev = (double*)malloc((n + 1) * sizeof(double));
    double* curr = (double*)malloc((n + 1) * sizeof(double));
    if (!prev || !curr) { free(prev); free(curr); return nql_val_null(); }
    prev[0] = 1.0;
    for (int64_t i = 1; i <= n; i++) {
        curr[0] = prev[i - 1];
        for (int64_t j = 1; j <= i; j++)
            curr[j] = curr[j - 1] + prev[j - 1];
        double* tmp = prev; prev = curr; curr = tmp;
    }
    double result = prev[0];
    free(prev); free(curr);
    if (result <= 9.007199254740992e15)
        return nql_val_int((int64_t)(result + 0.5));
    return nql_val_float(result);
}

/** STIRLING1(n, k) -- unsigned Stirling number of the first kind */
static NqlValue nql_fn_stirling1(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    if (argc != 2) return nql_val_null();
    int64_t n = (args[0].type == NQL_VAL_INT) ? args[0].v.ival : (int64_t)args[0].v.fval;
    int64_t k = (args[1].type == NQL_VAL_INT) ? args[1].v.ival : (int64_t)args[1].v.fval;
    if (n < 0 || k < 0 || k > n || n > 20) return nql_val_null();
    if (k == 0) return nql_val_int(n == 0 ? 1 : 0);
    if (k == n) return nql_val_int(1);
    /* DP: s(n,k) = (n-1)*s(n-1,k) + s(n-1,k-1) */
    double* prev = (double*)calloc(k + 1, sizeof(double));
    if (!prev) return nql_val_null();
    prev[0] = 1.0; /* s(0,0) = 1 */
    for (int64_t i = 1; i <= n; i++) {
        double* curr = (double*)calloc(k + 1, sizeof(double));
        if (!curr) { free(prev); return nql_val_null(); }
        for (int64_t j = 1; j <= k && j <= i; j++)
            curr[j] = (double)(i - 1) * prev[j] + prev[j - 1];
        free(prev);
        prev = curr;
    }
    double result = prev[k];
    free(prev);
    if (result <= 9.007199254740992e15)
        return nql_val_int((int64_t)(result + 0.5));
    return nql_val_float(result);
}

/** STIRLING2(n, k) -- Stirling number of the second kind */
static NqlValue nql_fn_stirling2(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    if (argc != 2) return nql_val_null();
    int64_t n = (args[0].type == NQL_VAL_INT) ? args[0].v.ival : (int64_t)args[0].v.fval;
    int64_t k = (args[1].type == NQL_VAL_INT) ? args[1].v.ival : (int64_t)args[1].v.fval;
    if (n < 0 || k < 0 || k > n || n > 25) return nql_val_null();
    if (k == 0) return nql_val_int(n == 0 ? 1 : 0);
    if (k == n) return nql_val_int(1);
    /* DP: S(n,k) = k*S(n-1,k) + S(n-1,k-1) */
    double* prev = (double*)calloc(k + 1, sizeof(double));
    if (!prev) return nql_val_null();
    prev[0] = 1.0;
    for (int64_t i = 1; i <= n; i++) {
        double* curr = (double*)calloc(k + 1, sizeof(double));
        if (!curr) { free(prev); return nql_val_null(); }
        for (int64_t j = 1; j <= k && j <= i; j++)
            curr[j] = (double)j * prev[j] + prev[j - 1];
        free(prev);
        prev = curr;
    }
    double result = prev[k];
    free(prev);
    if (result <= 9.007199254740992e15)
        return nql_val_int((int64_t)(result + 0.5));
    return nql_val_float(result);
}

/** PARTITION_COUNT(n) -- number of integer partitions of n */
static NqlValue nql_fn_partition_count(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    int64_t n = (args[0].type == NQL_VAL_INT) ? args[0].v.ival : (int64_t)args[0].v.fval;
    if (n < 0 || n > 500) return nql_val_null();
    if (n == 0) return nql_val_int(1);
    /* Standard DP for partition function */
    double* p = (double*)calloc(n + 1, sizeof(double));
    if (!p) return nql_val_null();
    p[0] = 1.0;
    for (int64_t k = 1; k <= n; k++)
        for (int64_t j = k; j <= n; j++)
            p[j] += p[j - k];
    double result = p[n];
    free(p);
    if (result <= 9.007199254740992e15)
        return nql_val_int((int64_t)(result + 0.5));
    return nql_val_float(result);
}

/** MULTINOMIAL(array) -- multinomial coefficient n!/(k1!*k2!*...*km!) where n=sum(ki) */
static NqlValue nql_fn_multinomial(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    if (arr->length == 0) return nql_val_int(1);
    int64_t total = 0;
    for (uint32_t i = 0; i < arr->length; i++) {
        int64_t ki = (arr->items[i].type == NQL_VAL_INT) ? arr->items[i].v.ival : (int64_t)arr->items[i].v.fval;
        if (ki < 0) return nql_val_null();
        total += ki;
    }
    /* Compute as product of C(running_sum, ki) */
    double result = 1.0;
    int64_t running = 0;
    for (uint32_t i = 0; i < arr->length; i++) {
        int64_t ki = (arr->items[i].type == NQL_VAL_INT) ? arr->items[i].v.ival : (int64_t)arr->items[i].v.fval;
        running += ki;
        /* Multiply by C(running, ki) */
        int64_t k = ki;
        if (k > running - k) k = running - k;
        for (int64_t j = 0; j < k; j++)
            result = result * (double)(running - j) / (double)(j + 1);
    }
    if (result <= 9.007199254740992e15)
        return nql_val_int((int64_t)(result + 0.5));
    return nql_val_float(result);
}

/* ======================================================================
 * SET OPERATIONS (on arrays)
 * ====================================================================== */

static bool nql_val_equal(NqlValue a, NqlValue b) {
    if (a.type == NQL_VAL_INT && b.type == NQL_VAL_INT) return a.v.ival == b.v.ival;
    double av = (a.type == NQL_VAL_INT) ? (double)a.v.ival : a.v.fval;
    double bv = (b.type == NQL_VAL_INT) ? (double)b.v.ival : b.v.fval;
    return av == bv;
}

static bool nql_array_contains_val(NqlArray* arr, NqlValue val) {
    for (uint32_t i = 0; i < arr->length; i++)
        if (nql_val_equal(arr->items[i], val)) return true;
    return false;
}

/** SET_UNION(a, b) -- union of two arrays (unique elements) */
static NqlValue nql_fn_set_union(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    NqlArray* result = nql_array_alloc(a->length + b->length);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < a->length; i++)
        if (!nql_array_contains_val(result, a->items[i]))
            nql_array_push(result, a->items[i]);
    for (uint32_t i = 0; i < b->length; i++)
        if (!nql_array_contains_val(result, b->items[i]))
            nql_array_push(result, b->items[i]);
    return nql_val_array(result);
}

/** SET_INTERSECT(a, b) -- intersection */
static NqlValue nql_fn_set_intersect(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    NqlArray* result = nql_array_alloc(a->length < b->length ? a->length : b->length);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < a->length; i++)
        if (nql_array_contains_val(b, a->items[i]) && !nql_array_contains_val(result, a->items[i]))
            nql_array_push(result, a->items[i]);
    return nql_val_array(result);
}

/** SET_DIFF(a, b) -- elements in a but not in b */
static NqlValue nql_fn_set_diff(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    NqlArray* result = nql_array_alloc(a->length);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < a->length; i++)
        if (!nql_array_contains_val(b, a->items[i]) && !nql_array_contains_val(result, a->items[i]))
            nql_array_push(result, a->items[i]);
    return nql_val_array(result);
}

/** SET_SYMDIFF(a, b) -- symmetric difference */
static NqlValue nql_fn_set_symdiff(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    NqlArray* result = nql_array_alloc(a->length + b->length);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < a->length; i++)
        if (!nql_array_contains_val(b, a->items[i]))
            nql_array_push(result, a->items[i]);
    for (uint32_t i = 0; i < b->length; i++)
        if (!nql_array_contains_val(a, b->items[i]))
            nql_array_push(result, b->items[i]);
    return nql_val_array(result);
}

/** SET_IS_SUBSET(a, b) -- true if a <= b */
static NqlValue nql_fn_set_is_subset(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    NqlArray* b = args[1].v.array;
    for (uint32_t i = 0; i < a->length; i++)
        if (!nql_array_contains_val(b, a->items[i])) return nql_val_int(0);
    return nql_val_int(1);
}

/** SET_POWERSET(a) -- all subsets (limit 16 elements to avoid 2^n explosion) */
static NqlValue nql_fn_set_powerset(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* a = args[0].v.array;
    if (a->length > 16) return nql_val_null(); /* safety limit */
    uint32_t total = 1u << a->length;
    NqlArray* result = nql_array_alloc(total);
    if (!result) return nql_val_null();
    for (uint32_t mask = 0; mask < total; mask++) {
        uint32_t cnt = 0;
        for (uint32_t b = 0; b < a->length; b++)
            if (mask & (1u << b)) cnt++;
        NqlArray* subset = nql_array_alloc(cnt);
        if (!subset) return nql_val_null();
        for (uint32_t b = 0; b < a->length; b++)
            if (mask & (1u << b))
                nql_array_push(subset, a->items[b]);
        nql_array_push(result, nql_val_array(subset));
    }
    return nql_val_array(result);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_combinatorics_register_functions(void) {
    /* Permutations & combinations */
    nql_register_func("PERM",              nql_fn_perm,             2, 2, "k-permutations P(n,k)");
    nql_register_func("COMB",              nql_fn_comb,             2, 2, "Binomial coefficient C(n,k)");
    nql_register_func("MULTICHOOSE",       nql_fn_multichoose,      2, 2, "Multiset coefficient C(n+k-1,k)");
    nql_register_func("MULTINOMIAL",       nql_fn_multinomial,      1, 1, "Multinomial coefficient (array of k_i)");

    /* Named sequences */
    nql_register_func("DERANGEMENT",       nql_fn_derangement,      1, 1, "Number of derangements D(n)");
    nql_register_func("CATALAN",           nql_fn_catalan,          1, 1, "Catalan number C(n)");
    nql_register_func("BELL",              nql_fn_bell,             1, 1, "Bell number B(n)");
    nql_register_func("STIRLING1",         nql_fn_stirling1,        2, 2, "Unsigned Stirling 1st kind");
    nql_register_func("STIRLING2",         nql_fn_stirling2,        2, 2, "Stirling 2nd kind");
    nql_register_func("PARTITION_COUNT",   nql_fn_partition_count,  1, 1, "Integer partition count p(n)");

    /* Set operations */
    nql_register_func("SET_UNION",         nql_fn_set_union,        2, 2, "Set union of arrays");
    nql_register_func("SET_INTERSECT",     nql_fn_set_intersect,    2, 2, "Set intersection of arrays");
    nql_register_func("SET_DIFF",          nql_fn_set_diff,         2, 2, "Set difference A \\ B");
    nql_register_func("SET_SYMDIFF",       nql_fn_set_symdiff,      2, 2, "Symmetric difference");
    nql_register_func("SET_IS_SUBSET",     nql_fn_set_is_subset,    2, 2, "Test if A is subset of B");
    nql_register_func("SET_POWERSET",      nql_fn_set_powerset,     1, 1, "Power set (max 16 elements)");
}
