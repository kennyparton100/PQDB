/**
 * nql_symbolic.c - Symbolic expression implementation for NQL
 * Part of the NQL mathematical expansion.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* Symbolic expression AST */
typedef enum {
    NQL_SYM_CONST,
    NQL_SYM_VAR,
    NQL_SYM_ADD,
    NQL_SYM_MUL,
    NQL_SYM_POW,
    NQL_SYM_FUNC,
    NQL_SYM_DERIV
} NqlSymType;

/* Forward declaration for nql_sym_func */
static NqlSymbolic* nql_sym_func(const char* name, NqlSymbolic* arg);

typedef struct NqlSymbolic_s {
    NqlSymType type;
    union {
        double const_val;
        char var_name[64];
        struct {
            struct NqlSymbolic_s* left;
            struct NqlSymbolic_s* right;
        } binary;
        struct {
            char func_name[64];
            struct NqlSymbolic_s* arg;
        } func;
    } d;
} NqlSymbolic;

/* Core operations */
static NqlSymbolic* nql_sym_var(const char* name) {
    NqlSymbolic* sym = (NqlSymbolic*)malloc(sizeof(NqlSymbolic));
    if (!sym) return NULL;
    
    sym->type = NQL_SYM_VAR;
    strncpy(sym->d.var_name, name, sizeof(sym->d.var_name) - 1);
    sym->d.var_name[sizeof(sym->d.var_name) - 1] = '\0';
    
    return sym;
}

static NqlSymbolic* nql_sym_const(double val) {
    NqlSymbolic* sym = (NqlSymbolic*)malloc(sizeof(NqlSymbolic));
    if (!sym) return NULL;
    
    sym->type = NQL_SYM_CONST;
    sym->d.const_val = val;
    
    return sym;
}

static NqlSymbolic* nql_sym_copy(const NqlSymbolic* src) {
    if (!src) return NULL;
    
    NqlSymbolic* sym = (NqlSymbolic*)malloc(sizeof(NqlSymbolic));
    if (!sym) return NULL;
    
    sym->type = src->type;
    
    switch (src->type) {
        case NQL_SYM_CONST:
            sym->d.const_val = src->d.const_val;
            break;
        case NQL_SYM_VAR:
            strcpy(sym->d.var_name, src->d.var_name);
            break;
        case NQL_SYM_ADD:
        case NQL_SYM_MUL:
        case NQL_SYM_POW:
            sym->d.binary.left = nql_sym_copy(src->d.binary.left);
            sym->d.binary.right = nql_sym_copy(src->d.binary.right);
            break;
        case NQL_SYM_FUNC:
            strcpy(sym->d.func.func_name, src->d.func.func_name);
            sym->d.func.arg = nql_sym_copy(src->d.func.arg);
            break;
        case NQL_SYM_DERIV:
            /* For derivatives, copy the expression being derived */
            sym->d.binary.left = nql_sym_copy(src->d.binary.left);
            sym->d.binary.right = NULL; /* Variable name stored elsewhere */
            break;
    }
    
    return sym;
}

static void nql_sym_free(NqlSymbolic* sym) {
    if (!sym) return;
    
    switch (sym->type) {
        case NQL_SYM_ADD:
        case NQL_SYM_MUL:
        case NQL_SYM_POW:
        case NQL_SYM_DERIV:
            nql_sym_free(sym->d.binary.left);
            nql_sym_free(sym->d.binary.right);
            break;
        case NQL_SYM_FUNC:
            nql_sym_free(sym->d.func.arg);
            break;
        case NQL_SYM_CONST:
        case NQL_SYM_VAR:
            /* No additional cleanup needed */
            break;
    }
    
    free(sym);
}

static NqlSymbolic* nql_sym_add(NqlSymbolic* a, NqlSymbolic* b) {
    NqlSymbolic* sym = (NqlSymbolic*)malloc(sizeof(NqlSymbolic));
    if (!sym) return NULL;
    
    sym->type = NQL_SYM_ADD;
    sym->d.binary.left = a;
    sym->d.binary.right = b;
    
    return sym;
}

static NqlSymbolic* nql_sym_mul(NqlSymbolic* a, NqlSymbolic* b) {
    NqlSymbolic* sym = (NqlSymbolic*)malloc(sizeof(NqlSymbolic));
    if (!sym) return NULL;
    
    sym->type = NQL_SYM_MUL;
    sym->d.binary.left = a;
    sym->d.binary.right = b;
    
    return sym;
}

static NqlSymbolic* nql_sym_pow(NqlSymbolic* base, NqlSymbolic* exp) {
    NqlSymbolic* sym = (NqlSymbolic*)malloc(sizeof(NqlSymbolic));
    if (!sym) return NULL;
    
    sym->type = NQL_SYM_POW;
    sym->d.binary.left = base;
    sym->d.binary.right = exp;
    
    return sym;
}

/* Simplification rules */
static NqlSymbolic* nql_sym_simplify(NqlSymbolic* expr) {
    if (!expr) return NULL;
    
    switch (expr->type) {
        case NQL_SYM_CONST:
            /* Constants are already simplified */
            return expr;
            
        case NQL_SYM_VAR:
            /* Variables are already simplified */
            return expr;
            
        case NQL_SYM_ADD: {
            NqlSymbolic* left = nql_sym_simplify(expr->d.binary.left);
            NqlSymbolic* right = nql_sym_simplify(expr->d.binary.right);
            
            /* 0 + x = x */
            if (left->type == NQL_SYM_CONST && left->d.const_val == 0.0) {
                nql_sym_free(expr);
                nql_sym_free(right);
                return left;
            }
            /* x + 0 = x */
            if (right->type == NQL_SYM_CONST && right->d.const_val == 0.0) {
                nql_sym_free(expr);
                nql_sym_free(left);
                return right;
            }
            /* Constant addition */
            if (left->type == NQL_SYM_CONST && right->type == NQL_SYM_CONST) {
                NqlSymbolic* result = nql_sym_const(left->d.const_val + right->d.const_val);
                nql_sym_free(expr);
                nql_sym_free(left);
                nql_sym_free(right);
                return result;
            }
            
            expr->d.binary.left = left;
            expr->d.binary.right = right;
            return expr;
        }
        
        case NQL_SYM_MUL: {
            NqlSymbolic* left = nql_sym_simplify(expr->d.binary.left);
            NqlSymbolic* right = nql_sym_simplify(expr->d.binary.right);
            
            /* 0 * x = 0 */
            if ((left->type == NQL_SYM_CONST && left->d.const_val == 0.0) ||
                (right->type == NQL_SYM_CONST && right->d.const_val == 0.0)) {
                nql_sym_free(expr);
                nql_sym_free(left);
                nql_sym_free(right);
                return nql_sym_const(0.0);
            }
            /* 1 * x = x */
            if (left->type == NQL_SYM_CONST && left->d.const_val == 1.0) {
                nql_sym_free(expr);
                nql_sym_free(left);
                return right;
            }
            /* x * 1 = x */
            if (right->type == NQL_SYM_CONST && right->d.const_val == 1.0) {
                nql_sym_free(expr);
                nql_sym_free(right);
                return left;
            }
            /* Constant multiplication */
            if (left->type == NQL_SYM_CONST && right->type == NQL_SYM_CONST) {
                NqlSymbolic* result = nql_sym_const(left->d.const_val * right->d.const_val);
                nql_sym_free(expr);
                nql_sym_free(left);
                nql_sym_free(right);
                return result;
            }
            
            expr->d.binary.left = left;
            expr->d.binary.right = right;
            return expr;
        }
        
        case NQL_SYM_POW: {
            NqlSymbolic* base = nql_sym_simplify(expr->d.binary.left);
            NqlSymbolic* exp = nql_sym_simplify(expr->d.binary.right);
            
            /* x^0 = 1 */
            if (exp->type == NQL_SYM_CONST && exp->d.const_val == 0.0) {
                nql_sym_free(expr);
                nql_sym_free(base);
                nql_sym_free(exp);
                return nql_sym_const(1.0);
            }
            /* x^1 = x */
            if (exp->type == NQL_SYM_CONST && exp->d.const_val == 1.0) {
                nql_sym_free(expr);
                nql_sym_free(exp);
                return base;
            }
            /* 0^x = 0 (for x > 0) */
            if (base->type == NQL_SYM_CONST && base->d.const_val == 0.0 && 
                exp->type == NQL_SYM_CONST && exp->d.const_val > 0.0) {
                nql_sym_free(expr);
                nql_sym_free(base);
                nql_sym_free(exp);
                return nql_sym_const(0.0);
            }
            /* Constant exponentiation */
            if (base->type == NQL_SYM_CONST && exp->type == NQL_SYM_CONST) {
                NqlSymbolic* result = nql_sym_const(pow(base->d.const_val, exp->d.const_val));
                nql_sym_free(expr);
                nql_sym_free(base);
                nql_sym_free(exp);
                return result;
            }
            
            expr->d.binary.left = base;
            expr->d.binary.right = exp;
            return expr;
        }
        
        default:
            return expr;
    }
}

/* Symbolic differentiation */
static NqlSymbolic* nql_sym_derivative(NqlSymbolic* expr, const char* var) {
    if (!expr || !var) return NULL;
    
    switch (expr->type) {
        case NQL_SYM_CONST:
            /* Derivative of constant is 0 */
            return nql_sym_const(0.0);
            
        case NQL_SYM_VAR:
            /* Derivative of variable is 1 if it's the target variable, 0 otherwise */
            return nql_sym_const(strcmp(expr->d.var_name, var) == 0 ? 1.0 : 0.0);
            
        case NQL_SYM_ADD: {
            /* (f + g)' = f' + g' */
            NqlSymbolic* left_deriv = nql_sym_derivative(expr->d.binary.left, var);
            NqlSymbolic* right_deriv = nql_sym_derivative(expr->d.binary.right, var);
            return nql_sym_add(left_deriv, right_deriv);
        }
        
        case NQL_SYM_MUL: {
            /* (f * g)' = f' * g + f * g' */
            NqlSymbolic* f = expr->d.binary.left;
            NqlSymbolic* g = expr->d.binary.right;
            NqlSymbolic* f_prime = nql_sym_derivative(f, var);
            NqlSymbolic* g_prime = nql_sym_derivative(g, var);
            
            NqlSymbolic* term1 = nql_sym_mul(f_prime, nql_sym_copy(g));
            NqlSymbolic* term2 = nql_sym_mul(nql_sym_copy(f), g_prime);
            
            return nql_sym_add(term1, term2);
        }
        
        case NQL_SYM_POW: {
            /* (f^g)' = g * f^(g-1) * f' + f^g * ln(f) * g' */
            NqlSymbolic* f = expr->d.binary.left;
            NqlSymbolic* g = expr->d.binary.right;
            
            NqlSymbolic* f_prime = nql_sym_derivative(f, var);
            NqlSymbolic* g_prime = nql_sym_derivative(g, var);
            
            /* First term: g * f^(g-1) * f' */
            NqlSymbolic* g_minus_1 = nql_sym_add(g, nql_sym_const(-1.0));
            NqlSymbolic* f_pow_g_minus_1 = nql_sym_pow(nql_sym_copy(f), g_minus_1);
            NqlSymbolic* term1 = nql_sym_mul(nql_sym_copy(g), nql_sym_mul(f_pow_g_minus_1, f_prime));
            
            /* Second term: f^g * ln(f) * g' */
            NqlSymbolic* log_f = nql_sym_func("ln", nql_sym_copy(f));
            NqlSymbolic* term2 = nql_sym_mul(nql_sym_pow(nql_sym_copy(f), nql_sym_copy(g)), 
                                         nql_sym_mul(log_f, g_prime));
            
            return nql_sym_add(term1, term2);
        }
        
        default:
            return nql_sym_const(0.0);
    }
}

/* Function creation helper */
static NqlSymbolic* nql_sym_func(const char* name, NqlSymbolic* arg) {
    NqlSymbolic* sym = (NqlSymbolic*)malloc(sizeof(NqlSymbolic));
    if (!sym) return NULL;
    
    sym->type = NQL_SYM_FUNC;
    strncpy(sym->d.func.func_name, name, sizeof(sym->d.func.func_name) - 1);
    sym->d.func.func_name[sizeof(sym->d.func.func_name) - 1] = '\0';
    sym->d.func.arg = arg;
    
    return sym;
}

/* NQL value constructors */
static NqlValue nql_val_symbolic(NqlSymbolic* sym) {
    NqlValue r;
    r.type = NQL_VAL_SYMBOLIC;
    r.v.symbolic = sym;
    return r;
}

/* NQL function implementations */
static NqlValue nql_fn_symbol_var(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    
    return nql_val_symbolic(nql_sym_var(args[0].v.sval));
}

static NqlValue nql_fn_symbolic_expr(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_STRING) return nql_val_null();
    
    /* Simple parsing for basic expressions - this is a simplified implementation */
    const char* expr = args[0].v.sval;
    
    /* Handle simple cases like "x", "y", "z" */
    if (strcmp(expr, "x") == 0) {
        return nql_val_symbolic(nql_sym_var("x"));
    } else if (strcmp(expr, "y") == 0) {
        return nql_val_symbolic(nql_sym_var("y"));
    } else if (strcmp(expr, "z") == 0) {
        return nql_val_symbolic(nql_sym_var("z"));
    }
    
    /* Try to parse as constant */
    char* endptr;
    double val = strtod(expr, &endptr);
    if (endptr != expr && *endptr == '\0') {
        return nql_val_symbolic(nql_sym_const(val));
    }
    
    /* For more complex expressions, return null for now */
    return nql_val_null();
}

static NqlValue nql_fn_derivative(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_SYMBOLIC || !args[0].v.symbolic) return nql_val_null();
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    
    NqlSymbolic* result = nql_sym_derivative(args[0].v.symbolic, args[1].v.sval);
    if (!result) return nql_val_null();
    
    return nql_val_symbolic(result);
}

static NqlValue nql_fn_sym_const(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    double val = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    return nql_val_symbolic(nql_sym_const(val));
}

static NqlValue nql_fn_sym_add(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_SYMBOLIC || !args[0].v.symbolic) return nql_val_null();
    if (args[1].type != NQL_VAL_SYMBOLIC || !args[1].v.symbolic) return nql_val_null();
    return nql_val_symbolic(nql_sym_add(nql_sym_copy(args[0].v.symbolic), nql_sym_copy(args[1].v.symbolic)));
}

static NqlValue nql_fn_sym_mul(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_SYMBOLIC || !args[0].v.symbolic) return nql_val_null();
    if (args[1].type != NQL_VAL_SYMBOLIC || !args[1].v.symbolic) return nql_val_null();
    return nql_val_symbolic(nql_sym_mul(nql_sym_copy(args[0].v.symbolic), nql_sym_copy(args[1].v.symbolic)));
}

static NqlValue nql_fn_sym_pow(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_SYMBOLIC || !args[0].v.symbolic) return nql_val_null();
    if (args[1].type != NQL_VAL_SYMBOLIC || !args[1].v.symbolic) return nql_val_null();
    return nql_val_symbolic(nql_sym_pow(nql_sym_copy(args[0].v.symbolic), nql_sym_copy(args[1].v.symbolic)));
}

static NqlValue nql_fn_simplify(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_SYMBOLIC || !args[0].v.symbolic) return nql_val_null();
    NqlSymbolic* copy = nql_sym_copy(args[0].v.symbolic);
    if (!copy) return nql_val_null();
    NqlSymbolic* simplified = nql_sym_simplify(copy);
    return nql_val_symbolic(simplified);
}

/* Numeric evaluation: substitute a value for a variable */
static double nql_sym_eval(const NqlSymbolic* expr, const char* var, double val) {
    if (!expr) return 0.0;
    switch (expr->type) {
        case NQL_SYM_CONST: return expr->d.const_val;
        case NQL_SYM_VAR:   return (strcmp(expr->d.var_name, var) == 0) ? val : 0.0;
        case NQL_SYM_ADD:   return nql_sym_eval(expr->d.binary.left, var, val) + nql_sym_eval(expr->d.binary.right, var, val);
        case NQL_SYM_MUL:   return nql_sym_eval(expr->d.binary.left, var, val) * nql_sym_eval(expr->d.binary.right, var, val);
        case NQL_SYM_POW:   return pow(nql_sym_eval(expr->d.binary.left, var, val), nql_sym_eval(expr->d.binary.right, var, val));
        case NQL_SYM_FUNC: {
            double a = nql_sym_eval(expr->d.func.arg, var, val);
            if (strcmp(expr->d.func.func_name, "sin") == 0) return sin(a);
            if (strcmp(expr->d.func.func_name, "cos") == 0) return cos(a);
            if (strcmp(expr->d.func.func_name, "ln") == 0)  return log(a);
            if (strcmp(expr->d.func.func_name, "exp") == 0) return exp(a);
            if (strcmp(expr->d.func.func_name, "sqrt") == 0) return sqrt(a);
            return 0.0;
        }
        default: return 0.0;
    }
}

static NqlValue nql_fn_sym_eval(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    if (args[0].type != NQL_VAL_SYMBOLIC || !args[0].v.symbolic) return nql_val_null();
    if (args[1].type != NQL_VAL_STRING) return nql_val_null();
    double val = (args[2].type == NQL_VAL_INT) ? (double)args[2].v.ival : args[2].v.fval;
    return nql_val_float(nql_sym_eval(args[0].v.symbolic, args[1].v.sval, val));
}

/* String representation of symbolic expression */
static void nql_sym_to_str(const NqlSymbolic* expr, char* buf, size_t buflen) {
    if (!expr || buflen < 2) { if (buflen > 0) buf[0] = '\0'; return; }
    switch (expr->type) {
        case NQL_SYM_CONST:
            if (expr->d.const_val == (double)(int64_t)expr->d.const_val)
                snprintf(buf, buflen, "%lld", (long long)(int64_t)expr->d.const_val);
            else
                snprintf(buf, buflen, "%.6g", expr->d.const_val);
            break;
        case NQL_SYM_VAR:
            snprintf(buf, buflen, "%s", expr->d.var_name);
            break;
        case NQL_SYM_ADD: {
            char lbuf[256], rbuf[256];
            nql_sym_to_str(expr->d.binary.left, lbuf, sizeof(lbuf));
            nql_sym_to_str(expr->d.binary.right, rbuf, sizeof(rbuf));
            snprintf(buf, buflen, "(%s + %s)", lbuf, rbuf);
            break;
        }
        case NQL_SYM_MUL: {
            char lbuf[256], rbuf[256];
            nql_sym_to_str(expr->d.binary.left, lbuf, sizeof(lbuf));
            nql_sym_to_str(expr->d.binary.right, rbuf, sizeof(rbuf));
            snprintf(buf, buflen, "(%s * %s)", lbuf, rbuf);
            break;
        }
        case NQL_SYM_POW: {
            char lbuf[256], rbuf[256];
            nql_sym_to_str(expr->d.binary.left, lbuf, sizeof(lbuf));
            nql_sym_to_str(expr->d.binary.right, rbuf, sizeof(rbuf));
            snprintf(buf, buflen, "(%s^%s)", lbuf, rbuf);
            break;
        }
        case NQL_SYM_FUNC: {
            char abuf[256];
            nql_sym_to_str(expr->d.func.arg, abuf, sizeof(abuf));
            snprintf(buf, buflen, "%s(%s)", expr->d.func.func_name, abuf);
            break;
        }
        default:
            snprintf(buf, buflen, "?");
            break;
    }
}

static NqlValue nql_fn_sym_to_string(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_SYMBOLIC || !args[0].v.symbolic) return nql_val_null();
    NqlValue r;
    r.type = NQL_VAL_STRING;
    nql_sym_to_str(args[0].v.symbolic, r.v.sval, sizeof(r.v.sval));
    return r;
}

/* Register symbolic functions */
void nql_symbolic_register_functions(void) {
    nql_register_func("SYMBOL",            nql_fn_symbol_var,             1, 1, "Create symbolic variable");
    nql_register_func("SYMBOLIC",          nql_fn_symbolic_expr,          1, 1, "Create symbolic from string");
    nql_register_func("SYM_CONST",         nql_fn_sym_const,              1, 1, "Create symbolic constant");
    nql_register_func("SYM_ADD",           nql_fn_sym_add,                2, 2, "Symbolic addition");
    nql_register_func("SYM_MUL",           nql_fn_sym_mul,                2, 2, "Symbolic multiplication");
    nql_register_func("SYM_POW",           nql_fn_sym_pow,                2, 2, "Symbolic exponentiation");
    nql_register_func("SIMPLIFY",          nql_fn_simplify,               1, 1, "Simplify symbolic expression");
    nql_register_func("DERIVATIVE",        nql_fn_derivative,             2, 2, "Symbolic derivative d/dx");
    nql_register_func("SYM_EVAL",          nql_fn_sym_eval,               3, 3, "Evaluate expression at point (expr, var, value)");
    nql_register_func("SYM_TO_STRING",     nql_fn_sym_to_string,          1, 1, "Convert symbolic expression to string");
}
