/**
 * cpss_nql.c - NQL entry point, dispatcher, and help text.
 *
 * Provides nql_execute() called from cpss_app.c when the user types
 * "query <NQL statement>".  Manages the variable store and dispatches
 * to the parser and executor.
 */

/**
 * Execute a single NQL statement.
 *
 * @param src       The NQL source string (everything after "query ").
 * @param db        CPSSStream* (may be NULL if no DB loaded).
 * @param db_open   Whether the DB pointer is valid.
 * @param vars      Persistent variable store (across queries in a session).
 */
static void nql_execute_ex(const char* src, CPSSStream* db, bool db_open,
                           NqlVarStore* vars, NqlUserFuncStore* ufuncs) {
    nql_init_registry();

    if (!src || src[0] == '\0') {
        printf("Usage: query <NQL statement>\n"
               "  Example: query SELECT p FROM PRIME WHERE p BETWEEN 1 AND 100\n"
               "  Type 'help nql' for full language reference.\n");
        return;
    }

    /* Parse — heap-allocate pool (too large for stack) */
    NqlAstPool* pool = (NqlAstPool*)malloc(sizeof(NqlAstPool));
    if (!pool) { printf("NQL error: out of memory\n"); return; }
    nql_pool_init(pool);
    char err[256];
    NqlAstNode* root = nql_parse(src, pool, err, sizeof(err));
    if (!root) {
        printf("NQL error: %s\n", err);
        free(pool);
        return;
    }

    /* Build execution context */
    NqlEvalCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.db_ctx = db_open ? (void*)db : NULL;
    ctx.vars = vars;
    ctx.ufuncs = ufuncs;
    ctx.col_name = "n";
    ctx.current_val = 0u;
    ctx.subquery_result = NULL;
    ctx.subquery_row = 0u;

    /* Dispatch by AST root type */
    switch (root->type) {
    case NQL_NODE_WITH_RECURSIVE: {
        NqlResult* result = (NqlResult*)malloc(sizeof(NqlResult));
        if (!result) { printf("NQL error: out of memory\n"); break; }
        nql_exec_with_recursive(&ctx, root, result);
        nql_print_result(result);
        free(result);
        break;
    }

    case NQL_NODE_CREATE_FUNC: {
        NqlUserFunc* uf = nql_ufuncs_add(ufuncs, root->d.create_func.name);
        if (!uf) { printf("NQL error: too many user functions\n"); break; }
        uf->param_count = root->d.create_func.param_count;
        for (uint32_t i = 0u; i < uf->param_count; ++i)
            strncpy(uf->params[i], root->d.create_func.params[i], 63);
        uf->body = root->d.create_func.body;
        uf->pool = pool;
        pool = NULL; /* transfer ownership — don't free below */
        printf("function %s(%u params) created\n", uf->name, uf->param_count);
        break;
    }

    case NQL_NODE_LET: {
        NqlValue val = nql_eval_expr(&ctx, root->d.let.value);
        nql_vars_set(vars, root->d.let.name, val);
        char buf[256];
        nql_val_to_str(&val, buf, sizeof(buf));
        printf("%s = %s\n", root->d.let.name, buf);
        break;
    }

    case NQL_NODE_SELECT: {
        NqlResult* result = (NqlResult*)malloc(sizeof(NqlResult));
        if (!result) { printf("NQL error: out of memory\n"); break; }
        nql_exec_select(&ctx, root, result);
        nql_print_result(result);
        free(result);
        break;
    }

    case NQL_NODE_SET_OP: {
        NqlResult* result = (NqlResult*)malloc(sizeof(NqlResult));
        if (!result) { printf("NQL error: out of memory\n"); break; }
        nql_exec_set_op(&ctx, root, result);
        nql_print_result(result);
        free(result);
        break;
    }

    default:
        /* Imperative statements and bare expressions handled by nql_exec_stmt */
        nql_exec_stmt(&ctx, root);
        break;
    }
    fflush(stdout);
    free(pool); /* NULL if transferred to user func */
}

/** Multi-statement wrapper: splits on ';' and executes each statement. */
static void nql_execute(const char* src, CPSSStream* db, bool db_open,
                        NqlVarStore* vars, NqlUserFuncStore* ufuncs) {
    if (!src || !strchr(src, ';')) {
        nql_execute_ex(src, db, db_open, vars, ufuncs);
        return;
    }
    /* Copy and split on semicolons */
    size_t len = strlen(src);
    char* buf = (char*)malloc(len + 1u);
    if (!buf) { nql_execute_ex(src, db, db_open, vars, ufuncs); return; }
    memcpy(buf, src, len + 1u);
    char* p = buf;
    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        char* stmt = p;
        /* Find next semicolon or end */
        while (*p && *p != ';') ++p;
        if (*p == ';') { *p = '\0'; ++p; }
        /* Trim trailing whitespace */
        char* end = stmt + strlen(stmt) - 1;
        while (end > stmt && (*end == ' ' || *end == '\t')) { *end = '\0'; --end; }
        if (stmt[0] != '\0')
            nql_execute_ex(stmt, db, db_open, vars, ufuncs);
    }
    free(buf);
}

/** Print NQL help text. */
static void nql_print_help(void) {
    printf(
        "NQL - Number Query Language\n"
        "  SQL-like queries over number types backed by CPSS tools.\n"
        "\n"
        "Syntax:\n"
        "  query SELECT <cols> FROM <type> [WHERE <cond>] [ORDER BY <expr>] [LIMIT <n>]\n"
        "  query LET <var> = <expr>\n"
        "  query CREATE FUNCTION name(params) = <expr>\n"
        "  query WITH RECURSIVE name AS (base UNION ALL step) SELECT ...\n"
        "  query <expr>                       -- evaluate and print\n"
        "  query <stmt1>; <stmt2>; ...        -- multi-statement\n"
        "\n"
        "Number Types (FROM clause):\n"
        "  N, PRIME, COMPOSITE, SEMIPRIME, EVEN, ODD, PERFECT_POWER,\n"
        "  TWIN_PRIME, MERSENNE, FIBONACCI, PALINDROME, SQUARE, CUBE,\n"
        "  POWERFUL, SQUAREFREE, SMOOTH(B), RANGE(lo, hi),\n"
        "  CARMICHAEL, CUBAN_PRIME\n"
        "\n"
        "Conditionals:\n"
        "  IF(cond, then_val, else_val)\n"
        "  CASE WHEN c1 THEN e1 [WHEN c2 THEN e2 ...] [ELSE eN] END\n"
        "\n"
        "Functions (77 built-in):\n"
        "  Primality:     IS_PRIME, NEXT_PRIME, PREV_PRIME, PRIME_GAP\n"
        "  Counting:      PRIME_COUNT*, NTH_PRIME*           (* requires DB)\n"
        "  Factoring:     FACTORISE, SMALLEST_FACTOR, LARGEST_FACTOR\n"
        "  Factor stats:  DISTINCT_PRIME_FACTORS, PRIME_FACTOR_COUNT, RADICAL, SOPFR\n"
        "  Divisors:      DIVISORS, NUM_DIVISORS, DIVISOR_SUM, DIVISORS_OF\n"
        "  Classical:     EULER_TOTIENT, MOBIUS, CARMICHAEL_LAMBDA, LIOUVILLE\n"
        "  Modular:       POWMOD(b,e,m), MODINV(a,m), JACOBI(a,n)\n"
        "  Group theory:  MULTIPLICATIVE_ORDER(a,n), PRIMITIVE_ROOT(p),\n"
        "                 PRIMITIVE_ROOT_COUNT(p)\n"
        "  Analytic:      CHEBYSHEV_THETA(x), CHEBYSHEV_PSI(x)\n"
        "  Arithmetic:    GCD, LCM, INT_SQRT, POW, MOD, ABS, LEAST, GREATEST\n"
        "  Real math:     SQRT, LN, LOG2, LOG10, FLOOR, CEIL, ROUND, SIGN\n"
        "  Combinatorics: FACTORIAL, BINOMIAL, FIBONACCI(n)\n"
        "  Digits:        DIGIT_COUNT, DIGIT_SUM, REVERSE_DIGITS, IS_PALINDROME,\n"
        "                 DIGITAL_ROOT\n"
        "  Bitwise:       BIT_AND, BIT_OR, BIT_XOR, BIT_NOT, BIT_SHIFT_LEFT,\n"
        "                 BIT_SHIFT_RIGHT, BIT_LENGTH\n"
        "  Type tests:    IS_COMPOSITE, IS_SEMIPRIME, IS_EVEN, IS_ODD,\n"
        "                 IS_PERFECT_POWER, IS_TWIN_PRIME, IS_MERSENNE,\n"
        "                 IS_FIBONACCI, IS_SQUARE, IS_CUBE, IS_POWERFUL,\n"
        "                 IS_SQUAREFREE, IS_SMOOTH(n,B)\n"
        "\n"
        "Aggregates:  COUNT(*), SUM, MIN, MAX, AVG\n"
        "Window:      LAG(expr [,off]), LEAD(expr [,off])\n"
        "Set ops:     UNION, INTERSECT, EXCEPT\n"
        "Variables:   LET x = 42\n"
        "Subqueries:  SELECT ... FROM (SELECT ...) WHERE ...\n"
        "User funcs:  CREATE FUNCTION name(p1,p2) = <body expr>\n"
        "Recursion:   WITH RECURSIVE name AS (base UNION ALL step) SELECT ...\n"
        "Existence:   EXISTS(SELECT ...), use NOT before for negation\n"
        "\n"
        "Examples:\n"
        "  query SELECT p FROM PRIME WHERE p BETWEEN 100 AND 200\n"
        "  query SELECT COUNT(*) FROM PRIME WHERE p <= 1000000\n"
        "  query SELECT n, FACTORISE(n) FROM SEMIPRIME WHERE N BETWEEN 100 AND 120\n"
        "  query SELECT n, EULER_TOTIENT(n), DIVISOR_SUM(n) FROM N WHERE n BETWEEN 1 AND 20\n"
        "  query SELECT n, IF(IS_PRIME(n), 'prime', 'not') FROM N WHERE n BETWEEN 1 AND 20\n"
        "  query POWMOD(2, 31, 1000000007)\n"
        "  query CREATE FUNCTION f(n) = n * n + 1; SELECT n, f(n) FROM N WHERE n BETWEEN 1 AND 10\n"
        "  query SELECT n FROM RANGE(1, 100) WHERE IS_PRIME(n) AND IS_PALINDROME(n)\n"
    );
}
