/**
 * cpss_nql_types.c - NQL (Number Query Language) type definitions.
 *
 * Token types, AST node structures, number-type registry, and value types
 * for the SQL-like number query language. No executable code here — just
 * definitions consumed by the lexer, parser, evaluator, and entry point.
 */

/* ══════════════════════════════════════════════════════════════════════
 * TOKEN TYPES
 * ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    /* Literals & identifiers */
    NQL_TOK_INT,            /* integer literal */
    NQL_TOK_FLOAT,          /* float literal (for LOG etc.) */
    NQL_TOK_STRING,         /* quoted string */
    NQL_TOK_IDENT,          /* identifier / column name */

    /* SQL keywords */
    NQL_TOK_SELECT,
    NQL_TOK_FROM,
    NQL_TOK_WHERE,
    NQL_TOK_AND,
    NQL_TOK_OR,
    NQL_TOK_NOT,
    NQL_TOK_BETWEEN,
    NQL_TOK_IN,
    NQL_TOK_ORDER,
    NQL_TOK_BY,
    NQL_TOK_ASC,
    NQL_TOK_DESC,
    NQL_TOK_LIMIT,
    NQL_TOK_AS,
    NQL_TOK_LET,
    NQL_TOK_DISTINCT,
    NQL_TOK_IS,
    NQL_TOK_NULL_KW,       /* NULL keyword */
    NQL_TOK_TRUE_KW,
    NQL_TOK_FALSE_KW,

    /* Set operations */
    NQL_TOK_UNION,
    NQL_TOK_INTERSECT,
    NQL_TOK_EXCEPT,

    /* Aggregate keywords (also handled as function calls) */
    NQL_TOK_COUNT,
    NQL_TOK_SUM,
    NQL_TOK_MIN,
    NQL_TOK_MAX,
    NQL_TOK_AVG,

    /* Window function keywords */
    NQL_TOK_LAG,
    NQL_TOK_LEAD,

    /* Conditional keywords */
    NQL_TOK_IF,
    NQL_TOK_CASE,
    NQL_TOK_WHEN,
    NQL_TOK_THEN,
    NQL_TOK_ELSE,
    NQL_TOK_END,

    /* Definition keywords */
    NQL_TOK_CREATE,
    NQL_TOK_FUNCTION,
    NQL_TOK_WITH,
    NQL_TOK_RECURSIVE,
    NQL_TOK_ALL,
    NQL_TOK_EXISTS,

    /* Imperative scripting keywords */
    NQL_TOK_FOR,
    NQL_TOK_WHILE,
    NQL_TOK_DO,
    NQL_TOK_TO,
    NQL_TOK_EACH,
    NQL_TOK_PRINT,
    NQL_TOK_ASSERT,
    NQL_TOK_BREAK,
    NQL_TOK_CONTINUE,
    NQL_TOK_RETURN,

    /* Operators */
    NQL_TOK_PLUS,           /* + */
    NQL_TOK_MINUS,          /* - */
    NQL_TOK_STAR,           /* * */
    NQL_TOK_SLASH,          /* / */
    NQL_TOK_PERCENT,        /* % */
    NQL_TOK_EQ,             /* = */
    NQL_TOK_NEQ,            /* != */
    NQL_TOK_LT,             /* < */
    NQL_TOK_GT,             /* > */
    NQL_TOK_LEQ,            /* <= */
    NQL_TOK_GEQ,            /* >= */

    /* Delimiters */
    NQL_TOK_LPAREN,         /* ( */
    NQL_TOK_RPAREN,         /* ) */
    NQL_TOK_COMMA,          /* , */
    NQL_TOK_DOT,            /* . */
    NQL_TOK_SEMICOLON,      /* ; */

    /* Special */
    NQL_TOK_EOF,
    NQL_TOK_ERROR
} NqlTokenType;

/* A single token produced by the lexer. */
typedef struct {
    NqlTokenType type;
    const char*  start;     /* pointer into source string */
    uint32_t     length;    /* token length in chars */
    uint32_t     pos;       /* character offset in source */
    union {
        int64_t  ival;      /* for NQL_TOK_INT */
        double   fval;      /* for NQL_TOK_FLOAT */
    } val;
} NqlToken;

/* Token array produced by lexer. */
#define NQL_MAX_TOKENS 1024

typedef struct {
    NqlToken tokens[NQL_MAX_TOKENS];
    uint32_t count;
    char     error[256];    /* lexer error message */
} NqlTokenList;

/* ══════════════════════════════════════════════════════════════════════
 * NQL VALUE TYPE — runtime values
 * ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    NQL_VAL_NULL,
    NQL_VAL_INT,
    NQL_VAL_FLOAT,
    NQL_VAL_BOOL,
    NQL_VAL_STRING,
    NQL_VAL_U128,
    NQL_VAL_BIGNUM,
    NQL_VAL_ARRAY,
    NQL_VAL_POLY,

    /* QS native opaque types */
    NQL_VAL_QS_FACTOR_BASE,
    NQL_VAL_QS_SIEVE,
    NQL_VAL_QS_TRIAL_RESULT,
    NQL_VAL_QS_RELATION_SET,
    NQL_VAL_QS_GF2_MATRIX,
    NQL_VAL_QS_DEPENDENCY
} NqlValType;

typedef struct {
    NqlValType type;
    union {
        int64_t  ival;
        double   fval;
        bool     bval;
        char     sval[256];
        U128     u128val;
        BigNum   bnval;        /* 516 bytes — largest union member */
        struct NqlArray_s* array; /* heap-allocated, see nql_array.c */
        void* qs_ptr;              /* heap-allocated QS opaque object, see nql_qs_types.c */
    } v;
} NqlValue;

/* Forward declare — full definition in nql_array.c */
typedef struct NqlArray_s NqlArray;

static NqlValue nql_val_null(void) {
    NqlValue r; r.type = NQL_VAL_NULL; return r;
}
static NqlValue nql_val_int(int64_t i) {
    NqlValue r; r.type = NQL_VAL_INT; r.v.ival = i; return r;
}
static NqlValue nql_val_float(double f) {
    NqlValue r; r.type = NQL_VAL_FLOAT; r.v.fval = f; return r;
}
static NqlValue nql_val_bool(bool b) {
    NqlValue r; r.type = NQL_VAL_BOOL; r.v.bval = b; return r;
}
static NqlValue nql_val_str(const char* s) {
    NqlValue r; r.type = NQL_VAL_STRING;
    strncpy(r.v.sval, s, sizeof(r.v.sval) - 1);
    r.v.sval[sizeof(r.v.sval) - 1] = '\0';
    return r;
}
static NqlValue nql_val_u128(U128 x) {
    NqlValue r; memset(&r, 0, sizeof(r)); r.type = NQL_VAL_U128; r.v.u128val = x; return r;
}
static NqlValue nql_val_bignum(const BigNum* x) {
    NqlValue r; memset(&r, 0, sizeof(r)); r.type = NQL_VAL_BIGNUM; bn_copy(&r.v.bnval, x); return r;
}
/** Construct a value from any width, auto-selecting the narrowest type. */
static NqlValue nql_val_from_bignum(const BigNum* x) {
    if (bn_fits_u64(x)) return nql_val_int((int64_t)bn_to_u64(x));
    if (bn_fits_u128(x)) return nql_val_u128(bn_to_u128(x));
    return nql_val_bignum(x);
}
static NqlValue nql_val_from_u128(U128 x) {
    if (u128_fits_u64(x)) return nql_val_int((int64_t)x.lo);
    return nql_val_u128(x);
}

/** Convert any NqlValue to int64 (truncates wide types to low 64 bits). */
static int64_t nql_val_as_int(const NqlValue* v) {
    switch (v->type) {
        case NQL_VAL_INT:    return v->v.ival;
        case NQL_VAL_FLOAT:  return (int64_t)v->v.fval;
        case NQL_VAL_BOOL:   return v->v.bval ? 1 : 0;
        case NQL_VAL_U128:   return (int64_t)v->v.u128val.lo;
        case NQL_VAL_BIGNUM: return (int64_t)bn_to_u64(&v->v.bnval);
        default:             return 0;
    }
}
/** Convert any NqlValue to U128. */
static U128 nql_val_as_u128(const NqlValue* v) {
    switch (v->type) {
        case NQL_VAL_INT:    return u128_from_u64((uint64_t)v->v.ival);
        case NQL_VAL_FLOAT:  return u128_from_u64((uint64_t)v->v.fval);
        case NQL_VAL_BOOL:   return u128_from_u64(v->v.bval ? 1u : 0u);
        case NQL_VAL_U128:   return v->v.u128val;
        case NQL_VAL_BIGNUM: return bn_to_u128(&v->v.bnval);
        default:             return u128_from_u64(0u);
    }
}
/** Convert any NqlValue to BigNum. */
static BigNum nql_val_as_bignum(const NqlValue* v) {
    BigNum r;
    switch (v->type) {
        case NQL_VAL_INT:    bn_from_u64(&r, (uint64_t)v->v.ival); return r;
        case NQL_VAL_FLOAT:  bn_from_u64(&r, (uint64_t)v->v.fval); return r;
        case NQL_VAL_BOOL:   bn_from_u64(&r, v->v.bval ? 1u : 0u); return r;
        case NQL_VAL_U128:   bn_from_u128(&r, v->v.u128val); return r;
        case NQL_VAL_BIGNUM: bn_copy(&r, &v->v.bnval); return r;
        default:             bn_zero(&r); return r;
    }
}

/** Convert any NqlValue to double. */
static double nql_val_as_float(const NqlValue* v) {
    switch (v->type) {
        case NQL_VAL_INT:    return (double)v->v.ival;
        case NQL_VAL_FLOAT:  return v->v.fval;
        case NQL_VAL_BOOL:   return v->v.bval ? 1.0 : 0.0;
        case NQL_VAL_U128:   return (double)v->v.u128val.hi * 18446744073709551616.0 + (double)v->v.u128val.lo;
        case NQL_VAL_BIGNUM: return bn_to_double(&v->v.bnval);
        default:             return 0.0;
    }
}

/** Truthy test for WHERE clause evaluation. */
static bool nql_val_is_truthy(const NqlValue* v) {
    switch (v->type) {
        case NQL_VAL_NULL:   return false;
        case NQL_VAL_INT:    return v->v.ival != 0;
        case NQL_VAL_FLOAT:  return v->v.fval != 0.0;
        case NQL_VAL_BOOL:   return v->v.bval;
        case NQL_VAL_STRING: return v->v.sval[0] != '\0';
        case NQL_VAL_U128:   return !u128_is_zero(v->v.u128val);
        case NQL_VAL_BIGNUM: return !bn_is_zero(&v->v.bnval);
        case NQL_VAL_ARRAY:  return v->v.array != NULL;
        case NQL_VAL_POLY:   return v->v.qs_ptr != NULL;
        case NQL_VAL_QS_FACTOR_BASE:
        case NQL_VAL_QS_SIEVE:
        case NQL_VAL_QS_TRIAL_RESULT:
        case NQL_VAL_QS_RELATION_SET:
        case NQL_VAL_QS_GF2_MATRIX:
        case NQL_VAL_QS_DEPENDENCY:
            return v->v.qs_ptr != NULL;
    }
    return false;
}

/** Determine the widest numeric tier between two values. */
static NqlValType nql_wider_type(const NqlValue* a, const NqlValue* b) {
    if (a->type == NQL_VAL_BIGNUM || b->type == NQL_VAL_BIGNUM) return NQL_VAL_BIGNUM;
    if (a->type == NQL_VAL_U128   || b->type == NQL_VAL_U128)   return NQL_VAL_U128;
    if (a->type == NQL_VAL_FLOAT  || b->type == NQL_VAL_FLOAT)  return NQL_VAL_FLOAT;
    return NQL_VAL_INT;
}

/* ══════════════════════════════════════════════════════════════════════
 * AST NODE TYPES
 * ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    NQL_NODE_LITERAL,       /* integer/float/bool/string constant */
    NQL_NODE_IDENT,         /* variable or column reference */
    NQL_NODE_BINOP,         /* binary operator: +, -, *, /, %, =, !=, <, >, <=, >=, AND, OR */
    NQL_NODE_UNARYOP,       /* unary: -, NOT */
    NQL_NODE_FUNC_CALL,     /* function invocation: FACTOR(n), GCD(a,b) */
    NQL_NODE_AGGREGATE,     /* COUNT, SUM, MIN, MAX, AVG */
    NQL_NODE_WINDOW,        /* LAG(expr), LEAD(expr) */
    NQL_NODE_BETWEEN,       /* expr BETWEEN lo AND hi */
    NQL_NODE_IN_LIST,       /* expr IN (v1, v2, ...) */
    NQL_NODE_ALIAS,         /* expr AS name */
    NQL_NODE_SELECT,        /* full SELECT statement */
    NQL_NODE_SET_OP,        /* UNION / INTERSECT / EXCEPT */
    NQL_NODE_LET,           /* LET ident = expr */
    NQL_NODE_STAR,          /* SELECT * wildcard */
    NQL_NODE_IF,            /* IF(cond, true_expr, false_expr) */
    NQL_NODE_CASE,          /* CASE WHEN c1 THEN e1 ... ELSE eN END */
    NQL_NODE_CREATE_FUNC,   /* CREATE FUNCTION name(params) = expr */
    NQL_NODE_WITH_RECURSIVE,/* WITH RECURSIVE name AS (...) SELECT ... */
    NQL_NODE_EXISTS,        /* EXISTS (subquery) */

    /* Imperative statement nodes */
    NQL_NODE_IF_STMT,       /* IF cond THEN body [ELSE body] END IF */
    NQL_NODE_FOR,           /* FOR var FROM lo TO hi DO body END FOR */
    NQL_NODE_FOR_EACH,      /* FOR EACH var IN source DO body END FOR */
    NQL_NODE_WHILE,         /* WHILE cond DO body END WHILE */
    NQL_NODE_PRINT,         /* PRINT expr, expr, ... */
    NQL_NODE_ASSERT,        /* ASSERT cond [, message] */
    NQL_NODE_BREAK,         /* BREAK */
    NQL_NODE_CONTINUE,      /* CONTINUE */
    NQL_NODE_RETURN,        /* RETURN expr */
    NQL_NODE_BLOCK          /* { stmt; stmt; ... } — statement list */
} NqlNodeType;

/* Forward declare for recursive structure. */
typedef struct NqlAstNode NqlAstNode;

#define NQL_MAX_CHILDREN 32

struct NqlAstNode {
    NqlNodeType type;
    NqlToken    token;      /* the primary token for error reporting */

    /* Node-specific data */
    union {
        /* NQL_NODE_LITERAL */
        NqlValue literal;

        /* NQL_NODE_IDENT */
        struct { char name[64]; } ident;

        /* NQL_NODE_BINOP */
        struct {
            NqlTokenType op;
            NqlAstNode*  left;
            NqlAstNode*  right;
        } binop;

        /* NQL_NODE_UNARYOP */
        struct {
            NqlTokenType op;
            NqlAstNode*  operand;
        } unaryop;

        /* NQL_NODE_FUNC_CALL */
        struct {
            char         name[64];
            NqlAstNode*  args[NQL_MAX_CHILDREN];
            uint32_t     arg_count;
        } func_call;

        /* NQL_NODE_AGGREGATE */
        struct {
            NqlTokenType agg_type;   /* COUNT, SUM, MIN, MAX, AVG */
            NqlAstNode*  operand;    /* NULL for COUNT(*) */
            bool         is_star;    /* COUNT(*) */
        } aggregate;

        /* NQL_NODE_WINDOW */
        struct {
            NqlTokenType win_type;   /* LAG, LEAD */
            NqlAstNode*  operand;
            NqlAstNode*  offset;     /* optional: LAG(x, 2) */
        } window;

        /* NQL_NODE_BETWEEN */
        struct {
            NqlAstNode*  expr;
            NqlAstNode*  lo;
            NqlAstNode*  hi;
        } between;

        /* NQL_NODE_IN_LIST */
        struct {
            NqlAstNode*  expr;
            NqlAstNode*  items[NQL_MAX_CHILDREN];
            uint32_t     item_count;
        } in_list;

        /* NQL_NODE_ALIAS */
        struct {
            NqlAstNode*  expr;
            char         alias[64];
        } alias;

        /* NQL_NODE_SELECT */
        struct {
            NqlAstNode*  columns[NQL_MAX_CHILDREN]; /* select list */
            uint32_t     col_count;
            char         from_type[64];              /* "PRIME", "N", etc. */
            NqlAstNode*  from_arg;                   /* e.g. SMOOTH(7) → 7 */
            char         from_alias[64];             /* alias for default col: "p" in PRIME p */
            NqlAstNode*  where_clause;               /* NULL if no WHERE */
            NqlAstNode*  order_exprs[8];             /* ORDER BY expressions */
            bool         order_desc[8];
            uint32_t     order_count;
            NqlAstNode*  limit_expr;                 /* NULL if no LIMIT */
            bool         distinct;
            NqlAstNode*  subquery_from;              /* FROM (subquery) */
            /* Cross-join: second FROM source (max 2 sources) */
            char         from_type2[64];             /* "" if no second source */
            NqlAstNode*  from_arg2;
            char         from_alias2[64];
        } select;

        /* NQL_NODE_SET_OP */
        struct {
            NqlTokenType op;         /* UNION, INTERSECT, EXCEPT */
            NqlAstNode*  left;
            NqlAstNode*  right;
        } set_op;

        /* NQL_NODE_LET */
        struct {
            char         name[64];
            NqlAstNode*  value;
        } let;

        /* NQL_NODE_IF */
        struct {
            NqlAstNode*  cond;
            NqlAstNode*  then_expr;
            NqlAstNode*  else_expr;
        } if_expr;

        /* NQL_NODE_CASE */
        struct {
            NqlAstNode*  when_conds[NQL_MAX_CHILDREN];
            NqlAstNode*  when_exprs[NQL_MAX_CHILDREN];
            uint32_t     when_count;
            NqlAstNode*  else_expr;  /* NULL if no ELSE */
        } case_expr;

        /* NQL_NODE_CREATE_FUNC */
        struct {
            char         name[64];
            char         params[8][64];
            uint32_t     param_count;
            NqlAstNode*  body;
        } create_func;

        /* NQL_NODE_WITH_RECURSIVE */
        struct {
            char         cte_name[64];
            NqlAstNode*  base_select;
            NqlAstNode*  recursive_select;
            NqlAstNode*  outer_select;
        } with_recursive;

        /* NQL_NODE_EXISTS */
        struct {
            NqlAstNode*  subquery;
            bool         negated;   /* NOT EXISTS */
        } exists;

        /* NQL_NODE_IF_STMT: IF cond THEN body [ELSE IF ...] [ELSE body] END IF */
        struct {
            NqlAstNode*  cond;
            NqlAstNode*  then_body[NQL_MAX_CHILDREN];
            uint32_t     then_count;
            NqlAstNode*  else_body[NQL_MAX_CHILDREN]; /* may contain nested IF_STMT for ELSE IF */
            uint32_t     else_count;
        } if_stmt;

        /* NQL_NODE_FOR: FOR var FROM lo TO hi DO body END FOR */
        struct {
            char         var_name[64];
            NqlAstNode*  from_expr;
            NqlAstNode*  to_expr;
            NqlAstNode*  body[NQL_MAX_CHILDREN];
            uint32_t     body_count;
        } for_loop;

        /* NQL_NODE_FOR_EACH: FOR EACH var IN source DO body END FOR */
        struct {
            char         var_name[64];
            NqlAstNode*  source;     /* expression that yields an array or SELECT */
            NqlAstNode*  body[NQL_MAX_CHILDREN];
            uint32_t     body_count;
        } for_each;

        /* NQL_NODE_WHILE: WHILE cond DO body END WHILE */
        struct {
            NqlAstNode*  cond;
            NqlAstNode*  body[NQL_MAX_CHILDREN];
            uint32_t     body_count;
        } while_loop;

        /* NQL_NODE_PRINT: PRINT expr, expr, ... */
        struct {
            NqlAstNode*  exprs[NQL_MAX_CHILDREN];
            uint32_t     expr_count;
        } print;

        /* NQL_NODE_ASSERT: ASSERT cond [, message_expr] */
        struct {
            NqlAstNode*  cond;
            NqlAstNode*  message;    /* NULL if no message */
        } assert_stmt;

        /* NQL_NODE_RETURN: RETURN expr */
        struct {
            NqlAstNode*  expr;
        } return_stmt;

        /* NQL_NODE_BLOCK: sequence of statements */
        struct {
            NqlAstNode*  stmts[NQL_MAX_CHILDREN];
            uint32_t     stmt_count;
        } block;

        /* NQL_NODE_BREAK, NQL_NODE_CONTINUE: no data needed */
    } d;
};

/* ══════════════════════════════════════════════════════════════════════
 * AST NODE POOL — arena allocator to avoid per-node malloc
 * ══════════════════════════════════════════════════════════════════════ */

#define NQL_AST_POOL_SIZE 1024

typedef struct {
    NqlAstNode nodes[NQL_AST_POOL_SIZE];
    uint32_t   used;
} NqlAstPool;

static void nql_pool_init(NqlAstPool* pool) {
    pool->used = 0u;
}

static NqlAstNode* nql_pool_alloc(NqlAstPool* pool) {
    if (pool->used >= NQL_AST_POOL_SIZE) return NULL;
    NqlAstNode* n = &pool->nodes[pool->used++];
    memset(n, 0, sizeof(*n));
    return n;
}

/* ══════════════════════════════════════════════════════════════════════
 * NUMBER TYPE REGISTRY
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * A registered number type for FROM clauses.
 * test() checks membership; next() finds the next member >= n.
 * If needs_db is true, the DB pointer must be non-NULL for correct results.
 */
typedef struct {
    const char* name;           /* "PRIME", "COMPOSITE", etc. */
    const char* column_name;    /* default column alias: "p", "n", "N" */
    bool        (*test)(uint64_t n, void* db_ctx);
    uint64_t    (*next)(uint64_t n, void* db_ctx);
    bool        needs_db;
    bool        has_param;      /* e.g. SMOOTH(B) */
} NqlNumberType;

#define NQL_MAX_NUMBER_TYPES 32

/* Global registry — populated in cpss_nql_funcs.c */
static NqlNumberType g_nql_types[NQL_MAX_NUMBER_TYPES];
static uint32_t      g_nql_type_count = 0u;

static const NqlNumberType* nql_type_lookup(const char* name) {
    for (uint32_t i = 0u; i < g_nql_type_count; ++i) {
        if (_stricmp(g_nql_types[i].name, name) == 0)
            return &g_nql_types[i];
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * BUILT-IN FUNCTION REGISTRY
 * ══════════════════════════════════════════════════════════════════════ */

/** Function signature: up to 4 args, returns NqlValue. */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);

typedef struct {
    const char*  name;
    NqlBuiltinFn fn;
    uint32_t     min_args;
    uint32_t     max_args;
    const char*  description;
} NqlFuncEntry;

#define NQL_MAX_FUNCTIONS 256

static NqlFuncEntry g_nql_funcs[NQL_MAX_FUNCTIONS];
static uint32_t     g_nql_func_count = 0u;

static const NqlFuncEntry* nql_func_lookup(const char* name) {
    for (uint32_t i = 0u; i < g_nql_func_count; ++i) {
        if (_stricmp(g_nql_funcs[i].name, name) == 0)
            return &g_nql_funcs[i];
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * VARIABLE STORE — for LET bindings
 * ══════════════════════════════════════════════════════════════════════ */

#define NQL_MAX_VARS 64

typedef struct {
    char     name[64];
    NqlValue value;
    bool     used;
} NqlVarEntry;

typedef struct {
    NqlVarEntry entries[NQL_MAX_VARS];
    uint32_t    count;
} NqlVarStore;

static void nql_vars_init(NqlVarStore* vs) {
    memset(vs, 0, sizeof(*vs));
}

static NqlValue* nql_vars_get(NqlVarStore* vs, const char* name) {
    for (uint32_t i = 0u; i < vs->count; ++i) {
        if (vs->entries[i].used && _stricmp(vs->entries[i].name, name) == 0)
            return &vs->entries[i].value;
    }
    return NULL;
}

static bool nql_vars_set(NqlVarStore* vs, const char* name, NqlValue val) {
    /* Update existing */
    for (uint32_t i = 0u; i < vs->count; ++i) {
        if (vs->entries[i].used && _stricmp(vs->entries[i].name, name) == 0) {
            vs->entries[i].value = val;
            return true;
        }
    }
    /* Insert new */
    if (vs->count >= NQL_MAX_VARS) return false;
    NqlVarEntry* e = &vs->entries[vs->count++];
    e->used = true;
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    e->value = val;
    return true;
}

/* ══════════════════════════════════════════════════════════════════════
 * USER-DEFINED FUNCTION STORE
 * ══════════════════════════════════════════════════════════════════════ */

#define NQL_MAX_USER_FUNCS 64

typedef struct {
    char         name[64];
    char         params[8][64];
    uint32_t     param_count;
    NqlAstNode*  body;          /* points into a persistent AST pool */
    NqlAstPool*  pool;          /* owning pool (kept alive for body) */
    bool         used;
} NqlUserFunc;

typedef struct {
    NqlUserFunc funcs[NQL_MAX_USER_FUNCS];
    uint32_t    count;
} NqlUserFuncStore;

static void nql_ufuncs_init(NqlUserFuncStore* fs) {
    memset(fs, 0, sizeof(*fs));
}

static NqlUserFunc* nql_ufuncs_get(NqlUserFuncStore* fs, const char* name) {
    for (uint32_t i = 0u; i < fs->count; ++i) {
        if (fs->funcs[i].used && _stricmp(fs->funcs[i].name, name) == 0)
            return &fs->funcs[i];
    }
    return NULL;
}

static NqlUserFunc* nql_ufuncs_add(NqlUserFuncStore* fs, const char* name) {
    /* Overwrite existing with same name */
    for (uint32_t i = 0u; i < fs->count; ++i) {
        if (fs->funcs[i].used && _stricmp(fs->funcs[i].name, name) == 0) {
            if (fs->funcs[i].pool) free(fs->funcs[i].pool);
            memset(&fs->funcs[i], 0, sizeof(NqlUserFunc));
            fs->funcs[i].used = true;
            strncpy(fs->funcs[i].name, name, 63);
            return &fs->funcs[i];
        }
    }
    if (fs->count >= NQL_MAX_USER_FUNCS) return NULL;
    NqlUserFunc* f = &fs->funcs[fs->count++];
    f->used = true;
    strncpy(f->name, name, 63);
    return f;
}

static void nql_ufuncs_free(NqlUserFuncStore* fs) {
    for (uint32_t i = 0u; i < fs->count; ++i) {
        if (fs->funcs[i].pool) { free(fs->funcs[i].pool); fs->funcs[i].pool = NULL; }
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * QUERY RESULT — materialised result set
 * ══════════════════════════════════════════════════════════════════════ */

#define NQL_MAX_RESULT_COLS 32
#define NQL_MAX_RESULT_ROWS 10000

typedef struct {
    char     col_names[NQL_MAX_RESULT_COLS][64];
    uint32_t col_count;
    NqlValue rows[NQL_MAX_RESULT_ROWS][NQL_MAX_RESULT_COLS];
    uint32_t row_count;
    char     error[256];
    bool     has_error;
} NqlResult;

static void nql_result_init(NqlResult* r) {
    memset(r, 0, sizeof(*r));
}

static void nql_result_error(NqlResult* r, const char* fmt, ...) {
    r->has_error = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->error, sizeof(r->error), fmt, ap);
    va_end(ap);
}
