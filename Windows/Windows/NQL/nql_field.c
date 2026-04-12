/**
 * nql_field.c - NQL Finite Field arithmetic.
 * Part of the CPSS Viewer amalgamation.
 *
 * GF(p) prime fields and GF(p^n) extension fields defined by irreducible polynomials.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * NqlField STRUCT
 * ====================================================================== */

struct NqlField_s {
    int64_t p;           /* characteristic (prime) */
    uint32_t n;          /* extension degree (1 = prime field) */
    int64_t* irred;      /* irreducible polynomial coefficients [c0..cn], NULL for prime field */
};

static NqlField* nql_field_alloc_prime(int64_t p) {
    NqlField* f = (NqlField*)malloc(sizeof(NqlField));
    if (!f) return NULL;
    f->p = p; f->n = 1; f->irred = NULL;
    return f;
}

static NqlField* nql_field_alloc_ext(int64_t p, uint32_t deg, const int64_t* irred) {
    NqlField* f = (NqlField*)malloc(sizeof(NqlField));
    if (!f) return NULL;
    f->p = p; f->n = deg;
    f->irred = (int64_t*)malloc((deg + 1) * sizeof(int64_t));
    if (!f->irred) { free(f); return NULL; }
    memcpy(f->irred, irred, (deg + 1) * sizeof(int64_t));
    return f;
}

static NqlValue nql_val_field(NqlField* f) {
    NqlValue v; memset(&v, 0, sizeof(v));
    v.type = NQL_VAL_FIELD; v.v.field = f;
    return v;
}

static int64_t nql_field_mod(int64_t a, int64_t p) {
    int64_t r = a % p;
    return (r < 0) ? r + p : r;
}

static int64_t nql_field_inv(int64_t a, int64_t p) {
    int64_t g = p, x = 0, y = 1;
    int64_t aa = nql_field_mod(a, p);
    while (aa > 0) {
        int64_t q = g / aa;
        int64_t tmp = g - q * aa; g = aa; aa = tmp;
        tmp = x - q * y; x = y; y = tmp;
    }
    return nql_field_mod(x, p);
}

/* ======================================================================
 * NQL FUNCTIONS
 * ====================================================================== */

/** GF(p) -- create prime field */
static NqlValue nql_fn_gf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    int64_t p = nql_val_as_int(&args[0]);
    if (p < 2) return nql_val_null();
    NqlField* f = nql_field_alloc_prime(p);
    return f ? nql_val_field(f) : nql_val_null();
}

/** GF_EXT(p, irred_poly_array) -- extension field GF(p^n) */
static NqlValue nql_fn_gf_ext(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    int64_t p = nql_val_as_int(&args[0]);
    if (p < 2 || args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* poly = args[1].v.array;
    uint32_t deg = poly->length - 1;
    if (deg < 2 || deg > 16) return nql_val_null();
    int64_t* coeffs = (int64_t*)malloc(poly->length * sizeof(int64_t));
    if (!coeffs) return nql_val_null();
    for (uint32_t i = 0; i < poly->length; i++) coeffs[i] = nql_field_mod(nql_val_as_int(&poly->items[i]), p);
    NqlField* f = nql_field_alloc_ext(p, deg, coeffs);
    free(coeffs);
    return f ? nql_val_field(f) : nql_val_null();
}

/** GF_ADD(field, a, b) */
static NqlValue nql_fn_gf_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_FIELD || !args[0].v.field) return nql_val_null();
    NqlField* f = args[0].v.field;
    if (f->n == 1) {
        return nql_val_int(nql_field_mod(nql_val_as_int(&args[1]) + nql_val_as_int(&args[2]), f->p));
    }
    /* Extension: args are arrays of coefficients */
    if (args[1].type != NQL_VAL_ARRAY || args[2].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* a = args[1].v.array; NqlArray* b = args[2].v.array;
    NqlArray* r = nql_array_alloc(f->n);
    if (!r) return nql_val_null();
    for (uint32_t i = 0; i < f->n; i++) {
        int64_t ai = (i < a->length) ? nql_val_as_int(&a->items[i]) : 0;
        int64_t bi = (i < b->length) ? nql_val_as_int(&b->items[i]) : 0;
        nql_array_push(r, nql_val_int(nql_field_mod(ai + bi, f->p)));
    }
    return nql_val_array(r);
}

/** GF_SUB(field, a, b) */
static NqlValue nql_fn_gf_sub(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_FIELD || !args[0].v.field) return nql_val_null();
    NqlField* f = args[0].v.field;
    if (f->n == 1) {
        return nql_val_int(nql_field_mod(nql_val_as_int(&args[1]) - nql_val_as_int(&args[2]), f->p));
    }
    if (args[1].type != NQL_VAL_ARRAY || args[2].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* a = args[1].v.array; NqlArray* b = args[2].v.array;
    NqlArray* r = nql_array_alloc(f->n);
    if (!r) return nql_val_null();
    for (uint32_t i = 0; i < f->n; i++) {
        int64_t ai = (i < a->length) ? nql_val_as_int(&a->items[i]) : 0;
        int64_t bi = (i < b->length) ? nql_val_as_int(&b->items[i]) : 0;
        nql_array_push(r, nql_val_int(nql_field_mod(ai - bi, f->p)));
    }
    return nql_val_array(r);
}

/** GF_MUL(field, a, b) */
static NqlValue nql_fn_gf_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_FIELD || !args[0].v.field) return nql_val_null();
    NqlField* f = args[0].v.field;
    if (f->n == 1) {
        return nql_val_int(nql_field_mod(nql_val_as_int(&args[1]) * nql_val_as_int(&args[2]), f->p));
    }
    if (args[1].type != NQL_VAL_ARRAY || args[2].type != NQL_VAL_ARRAY) return nql_val_null();
    NqlArray* a = args[1].v.array; NqlArray* b = args[2].v.array;
    uint32_t deg = f->n;
    /* Polynomial multiplication mod irred */
    int64_t* prod = (int64_t*)calloc(2 * deg, sizeof(int64_t));
    if (!prod) return nql_val_null();
    for (uint32_t i = 0; i < deg && i < a->length; i++)
        for (uint32_t j = 0; j < deg && j < b->length; j++)
            prod[i + j] = nql_field_mod(prod[i + j] + nql_val_as_int(&a->items[i]) * nql_val_as_int(&b->items[j]), f->p);
    /* Reduce mod irred */
    for (int i = (int)(2 * deg - 2); i >= (int)deg; i--) {
        int64_t c = nql_field_mod(prod[i], f->p);
        if (c == 0) continue;
        int64_t leading_inv = nql_field_inv(f->irred[deg], f->p);
        c = nql_field_mod(c * leading_inv, f->p);
        for (uint32_t j = 0; j <= deg; j++)
            prod[i - deg + j] = nql_field_mod(prod[i - deg + j] - c * f->irred[j], f->p);
    }
    NqlArray* r = nql_array_alloc(deg);
    if (!r) { free(prod); return nql_val_null(); }
    for (uint32_t i = 0; i < deg; i++) nql_array_push(r, nql_val_int(nql_field_mod(prod[i], f->p)));
    free(prod);
    return nql_val_array(r);
}

/** GF_INV(field, a) -- multiplicative inverse */
static NqlValue nql_fn_gf_inv(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_FIELD || !args[0].v.field) return nql_val_null();
    NqlField* f = args[0].v.field;
    if (f->n == 1) {
        int64_t a = nql_field_mod(nql_val_as_int(&args[1]), f->p);
        if (a == 0) return nql_val_null();
        return nql_val_int(nql_field_inv(a, f->p));
    }
    /* Extension field: use Fermat's little theorem a^(p^n-2) */
    /* For simplicity, compute via repeated squaring */
    if (args[1].type != NQL_VAL_ARRAY) return nql_val_null();
    /* Build identity element */
    NqlArray* result = nql_array_alloc(f->n);
    if (!result) return nql_val_null();
    nql_array_push(result, nql_val_int(1));
    for (uint32_t i = 1; i < f->n; i++) nql_array_push(result, nql_val_int(0));
    /* Compute order = p^n - 2 (exponent for inverse) */
    int64_t order = 1;
    for (uint32_t i = 0; i < f->n; i++) { order *= f->p; if (order > 1000000) { order = -1; break; } }
    if (order <= 0) return nql_val_null();
    order -= 2;
    /* Square-and-multiply */
    NqlValue base = args[1];
    NqlValue acc; acc.type = NQL_VAL_ARRAY; acc.v.array = result;
    NqlValue fv = args[0];
    for (int64_t e = order; e > 0; e >>= 1) {
        if (e & 1) {
            NqlValue mul_args[3] = {fv, acc, base};
            acc = nql_fn_gf_mul(mul_args, 3, ctx);
        }
        NqlValue sq_args[3] = {fv, base, base};
        base = nql_fn_gf_mul(sq_args, 3, ctx);
    }
    return acc;
}

/** GF_POW(field, a, exp) -- exponentiation */
static NqlValue nql_fn_gf_pow(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_FIELD || !args[0].v.field) return nql_val_null();
    NqlField* f = args[0].v.field;
    int64_t exp = nql_val_as_int(&args[2]);
    if (f->n == 1) {
        int64_t base = nql_field_mod(nql_val_as_int(&args[1]), f->p);
        int64_t result = 1;
        if (exp < 0) { base = nql_field_inv(base, f->p); exp = -exp; }
        while (exp > 0) {
            if (exp & 1) result = nql_field_mod(result * base, f->p);
            base = nql_field_mod(base * base, f->p);
            exp >>= 1;
        }
        return nql_val_int(result);
    }
    /* Extension: square-and-multiply */
    NqlArray* one = nql_array_alloc(f->n);
    if (!one) return nql_val_null();
    nql_array_push(one, nql_val_int(1));
    for (uint32_t i = 1; i < f->n; i++) nql_array_push(one, nql_val_int(0));
    NqlValue acc; acc.type = NQL_VAL_ARRAY; acc.v.array = one;
    NqlValue base = args[1];
    NqlValue fv = args[0];
    while (exp > 0) {
        if (exp & 1) { NqlValue ma[3] = {fv, acc, base}; acc = nql_fn_gf_mul(ma, 3, ctx); }
        NqlValue sa[3] = {fv, base, base}; base = nql_fn_gf_mul(sa, 3, ctx);
        exp >>= 1;
    }
    return acc;
}

/** GF_ORDER(field) -- field order p^n */
static NqlValue nql_fn_gf_order(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_FIELD || !args[0].v.field) return nql_val_null();
    NqlField* f = args[0].v.field;
    int64_t ord = 1;
    for (uint32_t i = 0; i < f->n; i++) ord *= f->p;
    return nql_val_int(ord);
}

/** GF_CHAR(field) -- characteristic p */
static NqlValue nql_fn_gf_char(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_FIELD || !args[0].v.field) return nql_val_null();
    return nql_val_int(args[0].v.field->p);
}

/** GF_DEGREE(field) -- extension degree n */
static NqlValue nql_fn_gf_degree(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_FIELD || !args[0].v.field) return nql_val_null();
    return nql_val_int((int64_t)args[0].v.field->n);
}

/** GF_ELEMENTS(field) -- list all elements (only for small fields, order <= 1000) */
static NqlValue nql_fn_gf_elements(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD1(args);
    if (args[0].type != NQL_VAL_FIELD || !args[0].v.field) return nql_val_null();
    NqlField* f = args[0].v.field;
    int64_t ord = 1;
    for (uint32_t i = 0; i < f->n; i++) { ord *= f->p; if (ord > 1000) return nql_val_null(); }
    if (f->n == 1) {
        NqlArray* r = nql_array_alloc((uint32_t)ord);
        if (!r) return nql_val_null();
        for (int64_t i = 0; i < ord; i++) nql_array_push(r, nql_val_int(i));
        return nql_val_array(r);
    }
    /* Extension: enumerate all coefficient vectors */
    NqlArray* r = nql_array_alloc((uint32_t)ord);
    if (!r) return nql_val_null();
    int64_t* coords = (int64_t*)calloc(f->n, sizeof(int64_t));
    if (!coords) return nql_val_null();
    for (int64_t idx = 0; idx < ord; idx++) {
        NqlArray* elem = nql_array_alloc(f->n);
        if (elem) {
            for (uint32_t j = 0; j < f->n; j++) nql_array_push(elem, nql_val_int(coords[j]));
            nql_array_push(r, nql_val_array(elem));
        }
        /* Increment coords (mixed-radix) */
        for (uint32_t j = 0; j < f->n; j++) {
            coords[j]++;
            if (coords[j] < f->p) break;
            coords[j] = 0;
        }
    }
    free(coords);
    return nql_val_array(r);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_field_register_functions(void) {
    nql_register_func("GF",          nql_fn_gf,          1, 1, "Prime field GF(p)");
    nql_register_func("GF_EXT",      nql_fn_gf_ext,      2, 2, "Extension field GF(p^n) from irred poly");
    nql_register_func("GF_ADD",      nql_fn_gf_add,      3, 3, "Field addition");
    nql_register_func("GF_SUB",      nql_fn_gf_sub,      3, 3, "Field subtraction");
    nql_register_func("GF_MUL",      nql_fn_gf_mul,      3, 3, "Field multiplication");
    nql_register_func("GF_INV",      nql_fn_gf_inv,      2, 2, "Multiplicative inverse");
    nql_register_func("GF_POW",      nql_fn_gf_pow,      3, 3, "Field exponentiation");
    nql_register_func("GF_ORDER",    nql_fn_gf_order,    1, 1, "Field order p^n");
    nql_register_func("GF_CHAR",     nql_fn_gf_char,     1, 1, "Field characteristic p");
    nql_register_func("GF_DEGREE",   nql_fn_gf_degree,   1, 1, "Extension degree n");
    nql_register_func("GF_ELEMENTS", nql_fn_gf_elements, 1, 1, "List all elements (small fields only)");
}
