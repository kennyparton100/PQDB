/**
 * nql_complex.c - Complex number implementation for NQL
 * Part of the NQL mathematical expansion.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* Complex number structure */
typedef struct NqlComplex_s {
    double real;
    double imag;
} NqlComplex;

/* Core operations */
static NqlComplex* nql_complex_alloc(double real, double imag) {
    NqlComplex* z = (NqlComplex*)malloc(sizeof(NqlComplex));
    if (!z) return NULL;
    z->real = real;
    z->imag = imag;
    return z;
}

static NqlComplex* nql_complex_copy(const NqlComplex* src) {
    if (!src) return NULL;
    return nql_complex_alloc(src->real, src->imag);
}

static void nql_complex_free(NqlComplex* z) {
    if (z) free(z);
}

static NqlComplex* nql_complex_add(const NqlComplex* a, const NqlComplex* b) {
    if (!a || !b) return NULL;
    return nql_complex_alloc(a->real + b->real, a->imag + b->imag);
}

static NqlComplex* nql_complex_subtract(const NqlComplex* a, const NqlComplex* b) {
    if (!a || !b) return NULL;
    return nql_complex_alloc(a->real - b->real, a->imag - b->imag);
}

static NqlComplex* nql_complex_mul(const NqlComplex* a, const NqlComplex* b) {
    if (!a || !b) return NULL;
    double real = a->real * b->real - a->imag * b->imag;
    double imag = a->real * b->imag + a->imag * b->real;
    return nql_complex_alloc(real, imag);
}

static NqlComplex* nql_complex_div(const NqlComplex* a, const NqlComplex* b) {
    if (!a || !b) return NULL;
    double denom = b->real * b->real + b->imag * b->imag;
    if (denom == 0.0) return NULL; /* Division by zero */
    
    double real = (a->real * b->real + a->imag * b->imag) / denom;
    double imag = (a->imag * b->real - a->real * b->imag) / denom;
    return nql_complex_alloc(real, imag);
}

static NqlComplex* nql_complex_pow(const NqlComplex* a, double exp) {
    if (!a) return NULL;
    
    /* Convert to polar form */
    double r = sqrt(a->real * a->real + a->imag * a->imag);
    double theta = atan2(a->imag, a->real);
    
    /* Apply exponent */
    double new_r = pow(r, exp);
    double new_theta = theta * exp;
    
    /* Convert back to rectangular form */
    return nql_complex_alloc(new_r * cos(new_theta), new_r * sin(new_theta));
}

static double nql_complex_abs(const NqlComplex* z) {
    if (!z) return 0.0;
    return sqrt(z->real * z->real + z->imag * z->imag);
}

static NqlComplex* nql_complex_conj(const NqlComplex* z) {
    if (!z) return NULL;
    return nql_complex_alloc(z->real, -z->imag);
}

static NqlComplex* nql_complex_sqrt(const NqlComplex* z) {
    if (!z) return NULL;
    
    double r = sqrt(z->real * z->real + z->imag * z->imag);
    double theta = atan2(z->imag, z->real);
    
    double new_r = sqrt(r);
    double new_theta = theta / 2.0;
    
    return nql_complex_alloc(new_r * cos(new_theta), new_r * sin(new_theta));
}

static NqlComplex* nql_complex_exp(const NqlComplex* z) {
    if (!z) return NULL;
    double exp_real = exp(z->real);
    return nql_complex_alloc(exp_real * cos(z->imag), exp_real * sin(z->imag));
}

static NqlComplex* nql_complex_log(const NqlComplex* z) {
    if (!z) return NULL;
    if (z->real == 0.0 && z->imag == 0.0) return NULL; /* Log of zero */
    
    double r = sqrt(z->real * z->real + z->imag * z->imag);
    double theta = atan2(z->imag, z->real);
    
    return nql_complex_alloc(log(r), theta);
}

static NqlComplex* nql_complex_sin(const NqlComplex* z) {
    if (!z) return NULL;
    /* sin(a+bi) = sin(a)cosh(b) + i*cos(a)sinh(b) */
    double real = sin(z->real) * cosh(z->imag);
    double imag = cos(z->real) * sinh(z->imag);
    return nql_complex_alloc(real, imag);
}

static NqlComplex* nql_complex_cos(const NqlComplex* z) {
    if (!z) return NULL;
    /* cos(a+bi) = cos(a)cosh(b) - i*sin(a)sinh(b) */
    double real = cos(z->real) * cosh(z->imag);
    double imag = -sin(z->real) * sinh(z->imag);
    return nql_complex_alloc(real, imag);
}

/* NQL value constructors */
static NqlValue nql_val_complex(double real, double imag) {
    NqlValue r;
    r.type = NQL_VAL_COMPLEX;
    r.v.complex = nql_complex_alloc(real, imag);
    return r;
}

/* NQL function implementations */
static NqlValue nql_fn_complex_create(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_INT && args[0].type != NQL_VAL_FLOAT) return nql_val_null();
    if (args[1].type != NQL_VAL_INT && args[1].type != NQL_VAL_FLOAT) return nql_val_null();
    
    double real = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double imag = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    
    return nql_val_complex(real, imag);
}

static NqlValue nql_fn_complex_real(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    
    return nql_val_float(args[0].v.complex->real);
}

static NqlValue nql_fn_complex_imag(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    
    return nql_val_float(args[0].v.complex->imag);
}

static NqlValue nql_fn_complex_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    if (args[1].type != NQL_VAL_COMPLEX || !args[1].v.complex) return nql_val_null();
    
    NqlComplex* result = nql_complex_add(args[0].v.complex, args[1].v.complex);
    if (!result) return nql_val_null();
    
    NqlValue r;
    r.type = NQL_VAL_COMPLEX;
    r.v.complex = result;
    return r;
}

static NqlValue nql_fn_complex_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    if (args[1].type != NQL_VAL_COMPLEX || !args[1].v.complex) return nql_val_null();
    
    NqlComplex* result = nql_complex_mul(args[0].v.complex, args[1].v.complex);
    if (!result) return nql_val_null();
    
    NqlValue r;
    r.type = NQL_VAL_COMPLEX;
    r.v.complex = result;
    return r;
}

static NqlValue nql_fn_complex_abs(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    
    return nql_val_float(nql_complex_abs(args[0].v.complex));
}

static NqlValue nql_fn_complex_sub(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    if (args[1].type != NQL_VAL_COMPLEX || !args[1].v.complex) return nql_val_null();
    NqlComplex* result = nql_complex_subtract(args[0].v.complex, args[1].v.complex);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_div(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    if (args[1].type != NQL_VAL_COMPLEX || !args[1].v.complex) return nql_val_null();
    NqlComplex* result = nql_complex_div(args[0].v.complex, args[1].v.complex);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_pow(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    double exp_val = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    NqlComplex* result = nql_complex_pow(args[0].v.complex, exp_val);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_conj(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    NqlComplex* result = nql_complex_conj(args[0].v.complex);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_sqrt(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    NqlComplex* result = nql_complex_sqrt(args[0].v.complex);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_exp(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    NqlComplex* result = nql_complex_exp(args[0].v.complex);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_log(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    NqlComplex* result = nql_complex_log(args[0].v.complex);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_sin(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    NqlComplex* result = nql_complex_sin(args[0].v.complex);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_cos(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    NqlComplex* result = nql_complex_cos(args[0].v.complex);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_arg(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    return nql_val_float(atan2(args[0].v.complex->imag, args[0].v.complex->real));
}

static NqlValue nql_fn_complex_polar(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    double r_val = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double theta = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    return nql_val_complex(r_val * cos(theta), r_val * sin(theta));
}

/* Extended complex trig/hyperbolic */
static NqlValue nql_fn_complex_tan(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    NqlComplex* s = nql_complex_sin(args[0].v.complex);
    NqlComplex* c = nql_complex_cos(args[0].v.complex);
    if (!s || !c) { free(s); free(c); return nql_val_null(); }
    NqlComplex* result = nql_complex_div(s, c);
    free(s); free(c);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

static NqlValue nql_fn_complex_sinh(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    double a = args[0].v.complex->real, b = args[0].v.complex->imag;
    /* sinh(a+bi) = sinh(a)cos(b) + i*cosh(a)sin(b) */
    return nql_val_complex(sinh(a) * cos(b), cosh(a) * sin(b));
}

static NqlValue nql_fn_complex_cosh(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    double a = args[0].v.complex->real, b = args[0].v.complex->imag;
    /* cosh(a+bi) = cosh(a)cos(b) + i*sinh(a)sin(b) */
    return nql_val_complex(cosh(a) * cos(b), sinh(a) * sin(b));
}

static NqlValue nql_fn_complex_tanh(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_COMPLEX || !args[0].v.complex) return nql_val_null();
    NqlValue sh = nql_fn_complex_sinh(args, argc, ctx);
    NqlValue ch = nql_fn_complex_cosh(args, argc, ctx);
    if (sh.type != NQL_VAL_COMPLEX || ch.type != NQL_VAL_COMPLEX) return nql_val_null();
    NqlComplex* result = nql_complex_div(sh.v.complex, ch.v.complex);
    if (!result) return nql_val_null();
    NqlValue r; r.type = NQL_VAL_COMPLEX; r.v.complex = result; return r;
}

/* Register complex number functions */
void nql_complex_register_functions(void) {
    nql_register_func("COMPLEX",           nql_fn_complex_create,        2, 2, "Create complex number (real, imag)");
    nql_register_func("COMPLEX_POLAR",     nql_fn_complex_polar,         2, 2, "Create complex from polar (r, theta)");
    nql_register_func("COMPLEX_REAL",      nql_fn_complex_real,          1, 1, "Real part");
    nql_register_func("COMPLEX_IMAG",      nql_fn_complex_imag,          1, 1, "Imaginary part");
    nql_register_func("COMPLEX_ABS",       nql_fn_complex_abs,           1, 1, "Complex magnitude |z|");
    nql_register_func("COMPLEX_ARG",       nql_fn_complex_arg,           1, 1, "Complex argument (angle in radians)");
    nql_register_func("COMPLEX_CONJ",      nql_fn_complex_conj,          1, 1, "Complex conjugate");
    nql_register_func("COMPLEX_ADD",       nql_fn_complex_add,           2, 2, "Complex addition");
    nql_register_func("COMPLEX_SUB",       nql_fn_complex_sub,           2, 2, "Complex subtraction");
    nql_register_func("COMPLEX_MUL",       nql_fn_complex_mul,           2, 2, "Complex multiplication");
    nql_register_func("COMPLEX_DIV",       nql_fn_complex_div,           2, 2, "Complex division");
    nql_register_func("COMPLEX_POW",       nql_fn_complex_pow,           2, 2, "Complex power z^n");
    nql_register_func("COMPLEX_SQRT",      nql_fn_complex_sqrt,          1, 1, "Complex square root");
    nql_register_func("COMPLEX_EXP",       nql_fn_complex_exp,           1, 1, "Complex exponential e^z");
    nql_register_func("COMPLEX_LOG",       nql_fn_complex_log,           1, 1, "Complex natural logarithm");
    nql_register_func("COMPLEX_SIN",       nql_fn_complex_sin,           1, 1, "Complex sine");
    nql_register_func("COMPLEX_COS",       nql_fn_complex_cos,           1, 1, "Complex cosine");
    nql_register_func("COMPLEX_TAN",       nql_fn_complex_tan,           1, 1, "Complex tangent");
    nql_register_func("COMPLEX_SINH",      nql_fn_complex_sinh,          1, 1, "Complex hyperbolic sine");
    nql_register_func("COMPLEX_COSH",      nql_fn_complex_cosh,          1, 1, "Complex hyperbolic cosine");
    nql_register_func("COMPLEX_TANH",      nql_fn_complex_tanh,          1, 1, "Complex hyperbolic tangent");
}
