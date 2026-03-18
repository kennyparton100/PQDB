/**
 * cpss_nql_eval.c - NQL query executor.
 *
 * Walks the AST produced by the parser.  Execution model:
 *   1. Generator: FROM type produces candidates via next()
 *   2. Filter:    WHERE clause evaluated per candidate
 *   3. Project:   SELECT expressions computed per surviving row
 *   4. Aggregate: COUNT/SUM/MIN/MAX/AVG accumulated
 *   5. Window:    LAG/LEAD computed over result window
 *   6. Order:     Sort result set (ORDER BY)
 *   7. Limit:     Truncate (LIMIT), default safety cap 10000
 *   8. Format:    Print aligned columnar output
 */

/* ── Execution context ── */

typedef struct {
    void*             db_ctx;        /* CPSSStream* or NULL */
    NqlVarStore*      vars;          /* session variable store */
    NqlUserFuncStore* ufuncs;        /* user-defined functions */
    uint64_t          current_val;   /* current candidate from generator */
    const char*       col_name;      /* default column name from type */
    NqlResult*        result;        /* output accumulator */

    /* Subquery result (for FROM subquery) */
    NqlResult*        subquery_result;
    uint32_t          subquery_row;  /* current row index in subquery */
} NqlEvalCtx;

/* Forward declare */
static NqlValue nql_eval_expr(NqlEvalCtx* ctx, const NqlAstNode* node);
static void nql_exec_select(NqlEvalCtx* ctx, const NqlAstNode* node, NqlResult* result);

/* ── Expression evaluator ── */

static NqlValue nql_eval_expr(NqlEvalCtx* ctx, const NqlAstNode* node) {
    if (!node) return nql_val_null();

    switch (node->type) {
    case NQL_NODE_LITERAL:
        return node->d.literal;

    case NQL_NODE_STAR:
        return nql_val_int((int64_t)ctx->current_val);

    case NQL_NODE_IDENT: {
        /* Check if it's the default column name */
        if (_stricmp(node->d.ident.name, ctx->col_name) == 0 ||
            _stricmp(node->d.ident.name, "n") == 0 ||
            _stricmp(node->d.ident.name, "p") == 0 ||
            _stricmp(node->d.ident.name, "N") == 0) {
            /* In subquery context, look up column by name */
            if (ctx->subquery_result && ctx->subquery_row < ctx->subquery_result->row_count) {
                for (uint32_t c = 0u; c < ctx->subquery_result->col_count; ++c) {
                    if (_stricmp(ctx->subquery_result->col_names[c], node->d.ident.name) == 0) {
                        return ctx->subquery_result->rows[ctx->subquery_row][c];
                    }
                }
            }
            return nql_val_int((int64_t)ctx->current_val);
        }
        /* Check subquery columns */
        if (ctx->subquery_result && ctx->subquery_row < ctx->subquery_result->row_count) {
            for (uint32_t c = 0u; c < ctx->subquery_result->col_count; ++c) {
                if (_stricmp(ctx->subquery_result->col_names[c], node->d.ident.name) == 0) {
                    return ctx->subquery_result->rows[ctx->subquery_row][c];
                }
            }
        }
        /* Check variables */
        NqlValue* var = nql_vars_get(ctx->vars, node->d.ident.name);
        if (var) return *var;
        /* Unknown: return null */
        return nql_val_null();
    }

    case NQL_NODE_BINOP: {
        NqlValue left = nql_eval_expr(ctx, node->d.binop.left);
        /* Short-circuit for AND/OR */
        if (node->d.binop.op == NQL_TOK_AND) {
            if (!nql_val_is_truthy(&left)) return nql_val_bool(false);
            NqlValue right = nql_eval_expr(ctx, node->d.binop.right);
            return nql_val_bool(nql_val_is_truthy(&right));
        }
        if (node->d.binop.op == NQL_TOK_OR) {
            if (nql_val_is_truthy(&left)) return nql_val_bool(true);
            NqlValue right = nql_eval_expr(ctx, node->d.binop.right);
            return nql_val_bool(nql_val_is_truthy(&right));
        }
        NqlValue right = nql_eval_expr(ctx, node->d.binop.right);

        /* If either is float, promote to float arithmetic */
        if (left.type == NQL_VAL_FLOAT || right.type == NQL_VAL_FLOAT) {
            double l = nql_val_as_float(&left);
            double r = nql_val_as_float(&right);
            switch (node->d.binop.op) {
                case NQL_TOK_PLUS:  return nql_val_float(l + r);
                case NQL_TOK_MINUS: return nql_val_float(l - r);
                case NQL_TOK_STAR:  return nql_val_float(l * r);
                case NQL_TOK_SLASH: return r == 0.0 ? nql_val_null() : nql_val_float(l / r);
                case NQL_TOK_EQ:    return nql_val_bool(l == r);
                case NQL_TOK_NEQ:   return nql_val_bool(l != r);
                case NQL_TOK_LT:    return nql_val_bool(l < r);
                case NQL_TOK_GT:    return nql_val_bool(l > r);
                case NQL_TOK_LEQ:   return nql_val_bool(l <= r);
                case NQL_TOK_GEQ:   return nql_val_bool(l >= r);
                default: break;
            }
        }
        /* Integer arithmetic */
        int64_t l = nql_val_as_int(&left);
        int64_t r = nql_val_as_int(&right);
        switch (node->d.binop.op) {
            case NQL_TOK_PLUS:    return nql_val_int(l + r);
            case NQL_TOK_MINUS:   return nql_val_int(l - r);
            case NQL_TOK_STAR:    return nql_val_int(l * r);
            case NQL_TOK_SLASH:   return r == 0 ? nql_val_null() : nql_val_int(l / r);
            case NQL_TOK_PERCENT: return r == 0 ? nql_val_null() : nql_val_int(l % r);
            case NQL_TOK_EQ:      return nql_val_bool(l == r);
            case NQL_TOK_NEQ:     return nql_val_bool(l != r);
            case NQL_TOK_LT:      return nql_val_bool(l < r);
            case NQL_TOK_GT:      return nql_val_bool(l > r);
            case NQL_TOK_LEQ:     return nql_val_bool(l <= r);
            case NQL_TOK_GEQ:     return nql_val_bool(l >= r);
            default: break;
        }
        return nql_val_null();
    }

    case NQL_NODE_UNARYOP: {
        NqlValue operand = nql_eval_expr(ctx, node->d.unaryop.operand);
        if (node->d.unaryop.op == NQL_TOK_MINUS) {
            if (operand.type == NQL_VAL_FLOAT) return nql_val_float(-operand.v.fval);
            return nql_val_int(-nql_val_as_int(&operand));
        }
        if (node->d.unaryop.op == NQL_TOK_NOT)
            return nql_val_bool(!nql_val_is_truthy(&operand));
        return nql_val_null();
    }

    case NQL_NODE_FUNC_CALL: {
        const NqlFuncEntry* fe = nql_func_lookup(node->d.func_call.name);
        if (fe) {
            if (node->d.func_call.arg_count < fe->min_args || node->d.func_call.arg_count > fe->max_args)
                return nql_val_null();
            NqlValue args[NQL_MAX_CHILDREN];
            for (uint32_t i = 0u; i < node->d.func_call.arg_count; ++i)
                args[i] = nql_eval_expr(ctx, node->d.func_call.args[i]);
            return fe->fn(args, node->d.func_call.arg_count, ctx->db_ctx);
        }
        /* Try user-defined function */
        if (ctx->ufuncs) {
            NqlUserFunc* uf = nql_ufuncs_get(ctx->ufuncs, node->d.func_call.name);
            if (uf && uf->body && node->d.func_call.arg_count == uf->param_count) {
                /* Bind parameters as temporary variables */
                NqlValue saved[8];
                bool had[8];
                for (uint32_t i = 0u; i < uf->param_count; ++i) {
                    NqlValue* existing = nql_vars_get(ctx->vars, uf->params[i]);
                    had[i] = (existing != NULL);
                    if (existing) saved[i] = *existing;
                    NqlValue arg = nql_eval_expr(ctx, node->d.func_call.args[i]);
                    nql_vars_set(ctx->vars, uf->params[i], arg);
                }
                NqlValue result = nql_eval_expr(ctx, uf->body);
                /* Restore previous values */
                for (uint32_t i = 0u; i < uf->param_count; ++i) {
                    if (had[i]) nql_vars_set(ctx->vars, uf->params[i], saved[i]);
                }
                return result;
            }
        }
        return nql_val_null();
    }

    case NQL_NODE_BETWEEN: {
        NqlValue bv_expr = nql_eval_expr(ctx, node->d.between.expr);
        NqlValue bv_lo   = nql_eval_expr(ctx, node->d.between.lo);
        NqlValue bv_hi   = nql_eval_expr(ctx, node->d.between.hi);
        int64_t val = nql_val_as_int(&bv_expr);
        int64_t lo  = nql_val_as_int(&bv_lo);
        int64_t hi  = nql_val_as_int(&bv_hi);
        return nql_val_bool(val >= lo && val <= hi);
    }

    case NQL_NODE_IN_LIST: {
        NqlValue in_expr = nql_eval_expr(ctx, node->d.in_list.expr);
        int64_t val = nql_val_as_int(&in_expr);
        for (uint32_t i = 0u; i < node->d.in_list.item_count; ++i) {
            NqlValue in_item = nql_eval_expr(ctx, node->d.in_list.items[i]);
            int64_t item = nql_val_as_int(&in_item);
            if (val == item) return nql_val_bool(true);
        }
        return nql_val_bool(false);
    }

    case NQL_NODE_ALIAS:
        return nql_eval_expr(ctx, node->d.alias.expr);

    case NQL_NODE_IF: {
        NqlValue cond = nql_eval_expr(ctx, node->d.if_expr.cond);
        if (nql_val_is_truthy(&cond))
            return nql_eval_expr(ctx, node->d.if_expr.then_expr);
        else
            return nql_eval_expr(ctx, node->d.if_expr.else_expr);
    }

    case NQL_NODE_CASE: {
        for (uint32_t i = 0u; i < node->d.case_expr.when_count; ++i) {
            NqlValue cond = nql_eval_expr(ctx, node->d.case_expr.when_conds[i]);
            if (nql_val_is_truthy(&cond))
                return nql_eval_expr(ctx, node->d.case_expr.when_exprs[i]);
        }
        if (node->d.case_expr.else_expr)
            return nql_eval_expr(ctx, node->d.case_expr.else_expr);
        return nql_val_null();
    }

    case NQL_NODE_EXISTS: {
        NqlResult* sub = (NqlResult*)malloc(sizeof(NqlResult));
        if (!sub) return nql_val_bool(false);
        nql_exec_select(ctx, node->d.exists.subquery, sub);
        bool has_rows = sub->row_count > 0u && !sub->has_error;
        free(sub);
        return nql_val_bool(node->d.exists.negated ? !has_rows : has_rows);
    }

    case NQL_NODE_AGGREGATE:
    case NQL_NODE_WINDOW:
        return nql_val_null();

    case NQL_NODE_SELECT:
    case NQL_NODE_SET_OP:
    case NQL_NODE_LET:
    case NQL_NODE_CREATE_FUNC:
    case NQL_NODE_WITH_RECURSIVE:
        return nql_val_null();
    }
    return nql_val_null();
}

/* ── Column name derivation ── */

static void nql_derive_col_name(const NqlAstNode* node, char* buf, size_t bufsz) {
    if (!node) { strncpy(buf, "?", bufsz); return; }
    switch (node->type) {
        case NQL_NODE_ALIAS:
            strncpy(buf, node->d.alias.alias, bufsz - 1u);
            buf[bufsz - 1u] = '\0';
            return;
        case NQL_NODE_IDENT:
            strncpy(buf, node->d.ident.name, bufsz - 1u);
            buf[bufsz - 1u] = '\0';
            return;
        case NQL_NODE_STAR:
            strncpy(buf, "*", bufsz);
            return;
        case NQL_NODE_FUNC_CALL:
            snprintf(buf, bufsz, "%s(...)", node->d.func_call.name);
            return;
        case NQL_NODE_AGGREGATE: {
            const char* an = nql_tok_name(node->d.aggregate.agg_type);
            if (node->d.aggregate.is_star)
                snprintf(buf, bufsz, "%s(*)", an);
            else
                snprintf(buf, bufsz, "%s(...)", an);
            return;
        }
        case NQL_NODE_WINDOW: {
            const char* wn = nql_tok_name(node->d.window.win_type);
            snprintf(buf, bufsz, "%s(...)", wn);
            return;
        }
        case NQL_NODE_BINOP: {
            char lb[32], rb[32];
            nql_derive_col_name(node->d.binop.left, lb, sizeof(lb));
            nql_derive_col_name(node->d.binop.right, rb, sizeof(rb));
            snprintf(buf, bufsz, "%s%s%s", lb, nql_tok_name(node->d.binop.op), rb);
            return;
        }
        default:
            snprintf(buf, bufsz, "expr");
            return;
    }
}

/* ── Check if select list contains aggregates ── */

static bool nql_has_aggregate(NqlAstNode* const* cols, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) {
        if (!cols[i]) continue;
        if (cols[i]->type == NQL_NODE_AGGREGATE) return true;
        if (cols[i]->type == NQL_NODE_ALIAS && cols[i]->d.alias.expr &&
            cols[i]->d.alias.expr->type == NQL_NODE_AGGREGATE) return true;
    }
    return false;
}

/* ── Check if select list contains window functions ── */

static bool nql_has_window(NqlAstNode* const* cols, uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) {
        if (!cols[i]) continue;
        if (cols[i]->type == NQL_NODE_WINDOW) return true;
        if (cols[i]->type == NQL_NODE_ALIAS && cols[i]->d.alias.expr &&
            cols[i]->d.alias.expr->type == NQL_NODE_WINDOW) return true;
    }
    return false;
}

/* ── Result formatting ── */

static void nql_val_to_str(const NqlValue* v, char* buf, size_t bufsz) {
    switch (v->type) {
        case NQL_VAL_NULL:   strncpy(buf, "NULL", bufsz); break;
        case NQL_VAL_INT:    snprintf(buf, bufsz, "%" PRId64, v->v.ival); break;
        case NQL_VAL_FLOAT:  snprintf(buf, bufsz, "%.6g", v->v.fval); break;
        case NQL_VAL_BOOL:   strncpy(buf, v->v.bval ? "true" : "false", bufsz); break;
        case NQL_VAL_STRING: strncpy(buf, v->v.sval, bufsz); break;
    }
    buf[bufsz - 1u] = '\0';
}

static void nql_print_result(const NqlResult* r) {
    if (r->has_error) {
        printf("NQL error: %s\n", r->error);
        return;
    }
    if (r->row_count == 0u && r->col_count == 0u) {
        printf("(empty result)\n");
        return;
    }

    /* Compute column widths */
    uint32_t widths[NQL_MAX_RESULT_COLS];
    for (uint32_t c = 0u; c < r->col_count; ++c) {
        widths[c] = (uint32_t)strlen(r->col_names[c]);
    }
    for (uint32_t row = 0u; row < r->row_count; ++row) {
        for (uint32_t c = 0u; c < r->col_count; ++c) {
            char buf[256];
            nql_val_to_str(&r->rows[row][c], buf, sizeof(buf));
            uint32_t len = (uint32_t)strlen(buf);
            if (len > widths[c]) widths[c] = len;
        }
    }

    /* Print header */
    for (uint32_t c = 0u; c < r->col_count; ++c) {
        if (c > 0u) printf(" | ");
        printf("%-*s", (int)widths[c], r->col_names[c]);
    }
    printf("\n");

    /* Separator */
    for (uint32_t c = 0u; c < r->col_count; ++c) {
        if (c > 0u) printf("-+-");
        for (uint32_t i = 0u; i < widths[c]; ++i) printf("-");
    }
    printf("\n");

    /* Rows */
    for (uint32_t row = 0u; row < r->row_count; ++row) {
        for (uint32_t c = 0u; c < r->col_count; ++c) {
            char buf[256];
            nql_val_to_str(&r->rows[row][c], buf, sizeof(buf));
            if (c > 0u) printf(" | ");
            /* Right-align numbers, left-align strings */
            if (r->rows[row][c].type == NQL_VAL_INT || r->rows[row][c].type == NQL_VAL_FLOAT)
                printf("%*s", (int)widths[c], buf);
            else
                printf("%-*s", (int)widths[c], buf);
        }
        printf("\n");
    }

    printf("(%u row%s)\n", r->row_count, r->row_count == 1u ? "" : "s");
}

/* ── SELECT executor ── */

static void nql_exec_select(NqlEvalCtx* ctx, const NqlAstNode* node, NqlResult* result);

/** Execute a subquery SELECT and return its result. */
static void nql_exec_subquery(NqlEvalCtx* ctx, const NqlAstNode* node, NqlResult* result) {
    nql_result_init(result);
    if (!node || node->type != NQL_NODE_SELECT) {
        nql_result_error(result, "subquery must be a SELECT");
        return;
    }
    nql_exec_select(ctx, node, result);
}

static void nql_exec_select(NqlEvalCtx* ctx, const NqlAstNode* node, NqlResult* result) {
    nql_result_init(result);
    if (!node || node->type != NQL_NODE_SELECT) {
        nql_result_error(result, "internal: not a SELECT node");
        return;
    }

    const char* from_type_name = node->d.select.from_type;
    bool has_agg = nql_has_aggregate(node->d.select.columns, node->d.select.col_count);
    bool has_win = nql_has_window(node->d.select.columns, node->d.select.col_count);

    /* Determine limit */
    uint64_t limit = NQL_MAX_RESULT_ROWS;
    if (node->d.select.limit_expr) {
        NqlValue lv = nql_eval_expr(ctx, node->d.select.limit_expr);
        int64_t lim = nql_val_as_int(&lv);
        if (lim > 0 && (uint64_t)lim < limit) limit = (uint64_t)lim;
    }

    /* Subquery FROM — heap-allocate (NqlResult is too large for stack) */
    NqlResult* subq_result = NULL;
    bool from_subquery = (node->d.select.subquery_from != NULL);
    if (from_subquery) {
        subq_result = (NqlResult*)malloc(sizeof(NqlResult));
        if (!subq_result) { nql_result_error(result, "out of memory"); return; }
        nql_exec_subquery(ctx, node->d.select.subquery_from, subq_result);
        if (subq_result->has_error) {
            nql_result_error(result, "subquery error: %s", subq_result->error);
            free(subq_result);
            return;
        }
    }

    /* Resolve FROM type */
    const NqlNumberType* ntype = NULL;
    const NqlNumberType* ntype2 = NULL;
    bool is_cross_join = (!from_subquery && node->d.select.from_type2[0] != '\0');

    if (!from_subquery) {
        if (from_type_name[0] == '\0') {
            nql_result_error(result, "missing FROM type");
            return;
        }
        ntype = nql_type_lookup(from_type_name);
        if (!ntype) {
            nql_result_error(result, "unknown type '%s'", from_type_name);
            return;
        }
        if (ntype->has_param && node->d.select.from_arg) {
            NqlValue arg = nql_eval_expr(ctx, node->d.select.from_arg);
            g_nql_smooth_B = (uint64_t)nql_val_as_int(&arg);
            if (g_nql_smooth_B < 2u) g_nql_smooth_B = 2u;
        }
        if (is_cross_join) {
            ntype2 = nql_type_lookup(node->d.select.from_type2);
            if (!ntype2) {
                nql_result_error(result, "unknown type '%s'", node->d.select.from_type2);
                return;
            }
        }
    }

    /* Set column name */
    const char* saved_col_name = ctx->col_name;
    if (!from_subquery && ntype) {
        ctx->col_name = ntype->column_name;
    }

    /* Derive column names */
    result->col_count = node->d.select.col_count;
    bool star_expand = false;
    for (uint32_t c = 0u; c < node->d.select.col_count; ++c) {
        if (node->d.select.columns[c] && node->d.select.columns[c]->type == NQL_NODE_STAR) {
            star_expand = true;
            if (from_subquery) {
                /* Expand star from subquery columns */
                result->col_count = subq_result->col_count;
                for (uint32_t sc = 0u; sc < subq_result->col_count; ++sc) {
                    strncpy(result->col_names[sc], subq_result->col_names[sc], 63);
                }
            } else {
                result->col_count = 1u;
                strncpy(result->col_names[0], ctx->col_name, 63);
            }
            break;
        }
        nql_derive_col_name(node->d.select.columns[c], result->col_names[c], 64);
    }

    /* Aggregate accumulators */
    int64_t  agg_count = 0;
    double   agg_sum[NQL_MAX_RESULT_COLS];
    int64_t  agg_min[NQL_MAX_RESULT_COLS];
    int64_t  agg_max[NQL_MAX_RESULT_COLS];
    if (has_agg) {
        for (uint32_t c = 0u; c < result->col_count; ++c) {
            agg_sum[c] = 0.0;
            agg_min[c] = INT64_MAX;
            agg_max[c] = INT64_MIN;
        }
    }

    /* Window pre-pass buffer: collect raw values first (heap-allocated) */
    int64_t* win_vals = NULL;
    uint32_t win_total = 0u;
    if (has_win) {
        win_vals = (int64_t*)malloc(sizeof(int64_t) * NQL_MAX_RESULT_ROWS);
        if (!win_vals) { nql_result_error(result, "out of memory"); free(subq_result); return; }
    }

    /* ── Cross-join nested loop ── */
    if (is_cross_join && ntype && ntype2) {
        /* Determine aliases: use explicit alias or type's default column name */
        const char* alias1 = node->d.select.from_alias[0] ? node->d.select.from_alias : ntype->column_name;
        const char* alias2 = node->d.select.from_alias2[0] ? node->d.select.from_alias2 : ntype2->column_name;

        /* Default scan ranges — will be refined by WHERE if possible */
        uint64_t lo1 = 2u, hi1 = 1000u;
        uint64_t lo2 = 2u, hi2 = 1000u;

        /* Try to extract range hints from BETWEEN in WHERE for either alias */
        /* (basic: use the overall BETWEEN if present, otherwise defaults) */
        if (node->d.select.where_clause) {
            const NqlAstNode* w = node->d.select.where_clause;
            /* Check for AND tree with comparison leaves that reference aliases */
            /* For simplicity, use scan defaults and let WHERE filter do the work */
            /* Users should use p <= X AND q <= Y style constraints */
            if (w->type == NQL_NODE_BINOP && w->d.binop.op == NQL_TOK_AND) {
                const NqlAstNode* parts[2] = { w->d.binop.left, w->d.binop.right };
                for (int pi = 0; pi < 2; ++pi) {
                    const NqlAstNode* pp = parts[pi];
                    if (!pp) continue;
                    if (pp->type == NQL_NODE_BINOP && (pp->d.binop.op == NQL_TOK_LEQ || pp->d.binop.op == NQL_TOK_LT)) {
                        if (pp->d.binop.left && pp->d.binop.left->type == NQL_NODE_IDENT) {
                            NqlEvalCtx tmp = *ctx; tmp.current_val = 0u;
                            NqlValue _rv = nql_eval_expr(&tmp, pp->d.binop.right);
                            int64_t v = nql_val_as_int(&_rv);
                            uint64_t bound = (pp->d.binop.op == NQL_TOK_LT) ? (uint64_t)(v - 1) : (uint64_t)v;
                            if (_stricmp(pp->d.binop.left->d.ident.name, alias1) == 0) hi1 = bound;
                            else if (_stricmp(pp->d.binop.left->d.ident.name, alias2) == 0) hi2 = bound;
                        }
                    }
                }
            }
        }

        /* Nested loop */
        uint64_t n1 = ntype->next(lo1, ctx->db_ctx);
        while (n1 <= hi1 && n1 > 0u) {
            nql_vars_set(ctx->vars, alias1, nql_val_int((int64_t)n1));
            ctx->current_val = n1;

            uint64_t n2 = ntype2->next(lo2, ctx->db_ctx);
            while (n2 <= hi2 && n2 > 0u) {
                nql_vars_set(ctx->vars, alias2, nql_val_int((int64_t)n2));

                /* WHERE filter */
                if (node->d.select.where_clause) {
                    NqlValue wv = nql_eval_expr(ctx, node->d.select.where_clause);
                    if (!nql_val_is_truthy(&wv)) {
                        ++n2;
                        if (n2 > hi2) break;
                        n2 = ntype2->next(n2, ctx->db_ctx);
                        continue;
                    }
                }

                if (result->row_count >= limit || result->row_count >= NQL_MAX_RESULT_ROWS) goto cross_join_done;

                uint32_t r = result->row_count;
                for (uint32_t c = 0u; c < node->d.select.col_count; ++c) {
                    result->rows[r][c] = nql_eval_expr(ctx, node->d.select.columns[c]);
                }
                ++result->row_count;

                ++n2;
                if (n2 > hi2 || n2 == 0u) break;
                n2 = ntype2->next(n2, ctx->db_ctx);
            }

            ++n1;
            if (n1 > hi1 || n1 == 0u) break;
            n1 = ntype->next(n1, ctx->db_ctx);
        }
        cross_join_done:;
    }
    /* ── Main scan loop (single source) ── */
    else if (from_subquery) {
        /* Iterate over subquery rows */
        NqlResult* saved_sub = ctx->subquery_result;
        uint32_t saved_row = ctx->subquery_row;
        ctx->subquery_result = subq_result;

        for (uint32_t row = 0u; row < subq_result->row_count; ++row) {
            ctx->subquery_row = row;
            /* Use first column as current_val */
            if (subq_result->col_count > 0u)
                ctx->current_val = (uint64_t)nql_val_as_int(&subq_result->rows[row][0]);

            /* WHERE filter */
            if (node->d.select.where_clause) {
                NqlValue wv = nql_eval_expr(ctx, node->d.select.where_clause);
                if (!nql_val_is_truthy(&wv)) continue;
            }

            if (has_agg) {
                agg_count++;
                for (uint32_t c = 0u; c < node->d.select.col_count; ++c) {
                    const NqlAstNode* col = node->d.select.columns[c];
                    const NqlAstNode* agg_node = col;
                    if (col && col->type == NQL_NODE_ALIAS) agg_node = col->d.alias.expr;
                    if (agg_node && agg_node->type == NQL_NODE_AGGREGATE && !agg_node->d.aggregate.is_star) {
                        NqlValue v = nql_eval_expr(ctx, agg_node->d.aggregate.operand);
                        double fv = nql_val_as_float(&v);
                        int64_t iv = nql_val_as_int(&v);
                        agg_sum[c] += fv;
                        if (iv < agg_min[c]) agg_min[c] = iv;
                        if (iv > agg_max[c]) agg_max[c] = iv;
                    }
                }
                continue;
            }

            if (result->row_count >= limit || result->row_count >= NQL_MAX_RESULT_ROWS) break;

            uint32_t r = result->row_count;
            if (star_expand) {
                for (uint32_t sc = 0u; sc < subq_result->col_count && sc < NQL_MAX_RESULT_COLS; ++sc) {
                    result->rows[r][sc] = subq_result->rows[row][sc];
                }
            } else {
                for (uint32_t c = 0u; c < node->d.select.col_count; ++c) {
                    result->rows[r][c] = nql_eval_expr(ctx, node->d.select.columns[c]);
                }
            }
            ++result->row_count;
        }
        ctx->subquery_result = saved_sub;
        ctx->subquery_row = saved_row;
    } else {
        /* Iterate over number type */
        /* We need a range from WHERE to know start/end. Extract BETWEEN if present. */
        uint64_t scan_lo = 1u, scan_hi = 1000000u; /* default safety range */
        bool range_found = false;

        /* Try to extract range from WHERE clause */
        if (node->d.select.where_clause) {
            const NqlAstNode* w = node->d.select.where_clause;
            /* Direct BETWEEN */
            if (w->type == NQL_NODE_BETWEEN) {
                NqlEvalCtx tmp = *ctx; tmp.current_val = 0u;
                { NqlValue _lo = nql_eval_expr(&tmp, w->d.between.lo); scan_lo = (uint64_t)nql_val_as_int(&_lo); }
                { NqlValue _hi = nql_eval_expr(&tmp, w->d.between.hi); scan_hi = (uint64_t)nql_val_as_int(&_hi); }
                range_found = true;
            }
            /* AND with BETWEEN on left or right */
            else if (w->type == NQL_NODE_BINOP && w->d.binop.op == NQL_TOK_AND) {
                const NqlAstNode* left = w->d.binop.left;
                const NqlAstNode* right = w->d.binop.right;
                if (left && left->type == NQL_NODE_BETWEEN) {
                    NqlEvalCtx tmp = *ctx; tmp.current_val = 0u;
                    { NqlValue _lo = nql_eval_expr(&tmp, left->d.between.lo); scan_lo = (uint64_t)nql_val_as_int(&_lo); }
                    { NqlValue _hi = nql_eval_expr(&tmp, left->d.between.hi); scan_hi = (uint64_t)nql_val_as_int(&_hi); }
                    range_found = true;
                } else if (right && right->type == NQL_NODE_BETWEEN) {
                    NqlEvalCtx tmp = *ctx; tmp.current_val = 0u;
                    { NqlValue _lo = nql_eval_expr(&tmp, right->d.between.lo); scan_lo = (uint64_t)nql_val_as_int(&_lo); }
                    { NqlValue _hi = nql_eval_expr(&tmp, right->d.between.hi); scan_hi = (uint64_t)nql_val_as_int(&_hi); }
                    range_found = true;
                }
                /* Try extracting from comparison operators: p > X, p < Y, p >= X, p <= Y */
                if (!range_found) {
                    uint64_t lo = 1u, hi = 1000000u;
                    bool lo_set = false, hi_set = false;
                    const NqlAstNode* parts[2] = { left, right };
                    for (int pi = 0; pi < 2; ++pi) {
                        const NqlAstNode* p = parts[pi];
                        if (!p || p->type != NQL_NODE_BINOP) continue;
                        NqlEvalCtx tmp = *ctx; tmp.current_val = 0u;
                        if (p->d.binop.op == NQL_TOK_GEQ || p->d.binop.op == NQL_TOK_GT) {
                            NqlValue _rv = nql_eval_expr(&tmp, p->d.binop.right);
                            int64_t v = nql_val_as_int(&_rv);
                            lo = (p->d.binop.op == NQL_TOK_GT) ? (uint64_t)(v + 1) : (uint64_t)v;
                            lo_set = true;
                        } else if (p->d.binop.op == NQL_TOK_LEQ || p->d.binop.op == NQL_TOK_LT) {
                            NqlValue _rv = nql_eval_expr(&tmp, p->d.binop.right);
                            int64_t v = nql_val_as_int(&_rv);
                            hi = (p->d.binop.op == NQL_TOK_LT) ? (uint64_t)(v - 1) : (uint64_t)v;
                            hi_set = true;
                        }
                    }
                    if (lo_set || hi_set) {
                        scan_lo = lo; scan_hi = hi;
                        range_found = true;
                    }
                }
            }
            /* Single comparison: p <= X means 1..X */
            else if (w->type == NQL_NODE_BINOP &&
                     (w->d.binop.op == NQL_TOK_LEQ || w->d.binop.op == NQL_TOK_LT)) {
                NqlEvalCtx tmp = *ctx; tmp.current_val = 0u;
                NqlValue _rv = nql_eval_expr(&tmp, w->d.binop.right);
                int64_t v = nql_val_as_int(&_rv);
                scan_hi = (w->d.binop.op == NQL_TOK_LT) ? (uint64_t)(v - 1) : (uint64_t)v;
                range_found = true;
            }
            /* p = X (equality) */
            else if (w->type == NQL_NODE_BINOP && w->d.binop.op == NQL_TOK_EQ) {
                NqlEvalCtx tmp = *ctx; tmp.current_val = 0u;
                NqlValue _rv2 = nql_eval_expr(&tmp, w->d.binop.right);
                int64_t v = nql_val_as_int(&_rv2);
                scan_lo = scan_hi = (uint64_t)v;
                range_found = true;
            }
        }

        if (!range_found && !has_agg) {
            /* No range constraint: apply limit as range cap */
            scan_hi = scan_lo + limit * 100u; /* generous scan window */
        }

        /* Safety: cap scan range to avoid runaway */
        if (scan_hi - scan_lo > 100000000ull && !has_agg) {
            scan_hi = scan_lo + 100000000ull;
        }

        /* Scan */
        uint64_t n = ntype->next(scan_lo, ctx->db_ctx);
        while (n <= scan_hi && n > 0u) {
            ctx->current_val = n;

            /* WHERE filter */
            if (node->d.select.where_clause) {
                NqlValue wv = nql_eval_expr(ctx, node->d.select.where_clause);
                if (!nql_val_is_truthy(&wv)) {
                    ++n;
                    if (n > scan_hi) break;
                    n = ntype->next(n, ctx->db_ctx);
                    continue;
                }
            }

            if (has_agg) {
                agg_count++;
                for (uint32_t c = 0u; c < node->d.select.col_count; ++c) {
                    const NqlAstNode* col = node->d.select.columns[c];
                    const NqlAstNode* agg_node = col;
                    if (col && col->type == NQL_NODE_ALIAS) agg_node = col->d.alias.expr;
                    if (agg_node && agg_node->type == NQL_NODE_AGGREGATE && !agg_node->d.aggregate.is_star) {
                        NqlValue v = nql_eval_expr(ctx, agg_node->d.aggregate.operand);
                        double fv = nql_val_as_float(&v);
                        int64_t iv = nql_val_as_int(&v);
                        agg_sum[c] += fv;
                        if (iv < agg_min[c]) agg_min[c] = iv;
                        if (iv > agg_max[c]) agg_max[c] = iv;
                    }
                }
            } else if (has_win) {
                /* Buffer values for window pass */
                if (win_total < NQL_MAX_RESULT_ROWS) {
                    win_vals[win_total] = (int64_t)n;
                    ++win_total;
                }
            } else {
                /* Normal row output */
                if (result->row_count >= limit || result->row_count >= NQL_MAX_RESULT_ROWS) break;
                uint32_t r = result->row_count;
                if (star_expand) {
                    result->rows[r][0] = nql_val_int((int64_t)n);
                } else {
                    for (uint32_t c = 0u; c < node->d.select.col_count; ++c) {
                        result->rows[r][c] = nql_eval_expr(ctx, node->d.select.columns[c]);
                    }
                }
                ++result->row_count;
            }

            ++n;
            if (n > scan_hi || n == 0u) break;
            n = ntype->next(n, ctx->db_ctx);
        }
    }

    /* ── Aggregate finalisation ── */
    if (has_agg) {
        result->row_count = 1u;
        for (uint32_t c = 0u; c < node->d.select.col_count; ++c) {
            const NqlAstNode* col = node->d.select.columns[c];
            const NqlAstNode* agg_node = col;
            if (col && col->type == NQL_NODE_ALIAS) agg_node = col->d.alias.expr;
            if (agg_node && agg_node->type == NQL_NODE_AGGREGATE) {
                switch (agg_node->d.aggregate.agg_type) {
                    case NQL_TOK_COUNT:
                        result->rows[0][c] = nql_val_int(agg_count);
                        break;
                    case NQL_TOK_SUM:
                        result->rows[0][c] = nql_val_int((int64_t)agg_sum[c]);
                        break;
                    case NQL_TOK_MIN:
                        result->rows[0][c] = (agg_count > 0) ? nql_val_int(agg_min[c]) : nql_val_null();
                        break;
                    case NQL_TOK_MAX:
                        result->rows[0][c] = (agg_count > 0) ? nql_val_int(agg_max[c]) : nql_val_null();
                        break;
                    case NQL_TOK_AVG:
                        result->rows[0][c] = (agg_count > 0) ? nql_val_float(agg_sum[c] / (double)agg_count) : nql_val_null();
                        break;
                    default:
                        result->rows[0][c] = nql_val_null();
                        break;
                }
            } else {
                /* Non-aggregate column in aggregate query: show last value */
                result->rows[0][c] = nql_eval_expr(ctx, col);
            }
        }
    }

    /* ── Window function post-pass ── */
    if (has_win && win_total > 0u) {
        for (uint32_t i = 0u; i < win_total && result->row_count < limit && result->row_count < NQL_MAX_RESULT_ROWS; ++i) {
            ctx->current_val = (uint64_t)win_vals[i];
            uint32_t r = result->row_count;
            for (uint32_t c = 0u; c < node->d.select.col_count; ++c) {
                const NqlAstNode* col = node->d.select.columns[c];
                const NqlAstNode* win_node = col;
                if (col && col->type == NQL_NODE_ALIAS) win_node = col->d.alias.expr;

                if (win_node && win_node->type == NQL_NODE_WINDOW) {
                    int64_t off = 1;
                    if (win_node->d.window.offset) {
                        NqlValue ov = nql_eval_expr(ctx, win_node->d.window.offset);
                        off = nql_val_as_int(&ov);
                    }
                    if (win_node->d.window.win_type == NQL_TOK_LAG) {
                        int64_t idx = (int64_t)i - off;
                        result->rows[r][c] = (idx >= 0 && idx < (int64_t)win_total)
                            ? nql_val_int(win_vals[idx]) : nql_val_null();
                    } else { /* LEAD */
                        int64_t idx = (int64_t)i + off;
                        result->rows[r][c] = (idx >= 0 && idx < (int64_t)win_total)
                            ? nql_val_int(win_vals[idx]) : nql_val_null();
                    }
                } else {
                    result->rows[r][c] = nql_eval_expr(ctx, col);
                }
            }
            ++result->row_count;
        }
    }

    /* ── ORDER BY ── */
    if (node->d.select.order_count > 0u && result->row_count > 1u) {
        /* Simple insertion sort on first ORDER BY column */
        uint32_t oc = 0u; /* order column index — find it */
        /* Evaluate order expression for each row */
        int64_t* order_keys = (int64_t*)malloc(sizeof(int64_t) * result->row_count);
        if (order_keys) {
            NqlEvalCtx tmp = *ctx;
            for (uint32_t r = 0u; r < result->row_count; ++r) {
                tmp.current_val = (uint64_t)nql_val_as_int(&result->rows[r][0]);
                if (ctx->subquery_result) tmp.subquery_row = r;
                NqlValue kv = nql_eval_expr(&tmp, node->d.select.order_exprs[0]);
                order_keys[r] = nql_val_as_int(&kv);
            }
            bool desc = node->d.select.order_desc[0];
            for (uint32_t i = 1u; i < result->row_count; ++i) {
                int64_t key = order_keys[i];
                NqlValue row_copy[NQL_MAX_RESULT_COLS];
                memcpy(row_copy, result->rows[i], sizeof(NqlValue) * result->col_count);
                int32_t j = (int32_t)i - 1;
                while (j >= 0 && (desc ? order_keys[j] < key : order_keys[j] > key)) {
                    order_keys[j + 1] = order_keys[j];
                    memcpy(result->rows[j + 1], result->rows[j], sizeof(NqlValue) * result->col_count);
                    --j;
                }
                order_keys[j + 1] = key;
                memcpy(result->rows[j + 1], row_copy, sizeof(NqlValue) * result->col_count);
            }
            free(order_keys);
        }
        (void)oc;
    }

    /* ── DISTINCT ── */
    if (node->d.select.distinct && result->row_count > 1u) {
        uint32_t out = 0u;
        for (uint32_t i = 0u; i < result->row_count; ++i) {
            bool dup = false;
            for (uint32_t j = 0u; j < out; ++j) {
                bool same = true;
                for (uint32_t c = 0u; c < result->col_count; ++c) {
                    if (nql_val_as_int(&result->rows[i][c]) != nql_val_as_int(&result->rows[j][c])) {
                        same = false; break;
                    }
                }
                if (same) { dup = true; break; }
            }
            if (!dup) {
                if (out != i) memcpy(result->rows[out], result->rows[i], sizeof(NqlValue) * result->col_count);
                ++out;
            }
        }
        result->row_count = out;
    }

    ctx->col_name = saved_col_name;
    free(win_vals);
    free(subq_result);
}

/* ── SET OPERATIONS ── */

static void nql_exec_set_op(NqlEvalCtx* ctx, const NqlAstNode* node, NqlResult* result) {
    nql_result_init(result);
    NqlResult* left_r = (NqlResult*)malloc(sizeof(NqlResult));
    NqlResult* right_r = (NqlResult*)malloc(sizeof(NqlResult));
    if (!left_r || !right_r) {
        nql_result_error(result, "out of memory");
        free(left_r); free(right_r); return;
    }
    nql_result_init(left_r);
    nql_result_init(right_r);

    if (node->d.set_op.left->type == NQL_NODE_SELECT)
        nql_exec_select(ctx, node->d.set_op.left, left_r);
    else if (node->d.set_op.left->type == NQL_NODE_SET_OP)
        nql_exec_set_op(ctx, node->d.set_op.left, left_r);
    else { nql_result_error(result, "left side of set op must be SELECT"); free(left_r); free(right_r); return; }

    if (node->d.set_op.right->type == NQL_NODE_SELECT)
        nql_exec_select(ctx, node->d.set_op.right, right_r);
    else if (node->d.set_op.right->type == NQL_NODE_SET_OP)
        nql_exec_set_op(ctx, node->d.set_op.right, right_r);
    else { nql_result_error(result, "right side of set op must be SELECT"); free(left_r); free(right_r); return; }

    if (left_r->has_error) { *result = *left_r; free(left_r); free(right_r); return; }
    if (right_r->has_error) { *result = *right_r; free(left_r); free(right_r); return; }

    /* Use left result column names */
    result->col_count = left_r->col_count;
    for (uint32_t c = 0u; c < result->col_count; ++c)
        strncpy(result->col_names[c], left_r->col_names[c], 63);

    switch (node->d.set_op.op) {
    case NQL_TOK_UNION:
        for (uint32_t i = 0u; i < left_r->row_count && result->row_count < NQL_MAX_RESULT_ROWS; ++i) {
            memcpy(result->rows[result->row_count++], left_r->rows[i], sizeof(NqlValue) * result->col_count);
        }
        for (uint32_t i = 0u; i < right_r->row_count && result->row_count < NQL_MAX_RESULT_ROWS; ++i) {
            bool dup = false;
            for (uint32_t j = 0u; j < left_r->row_count; ++j) {
                if (nql_val_as_int(&right_r->rows[i][0]) == nql_val_as_int(&left_r->rows[j][0])) {
                    dup = true; break;
                }
            }
            if (!dup) memcpy(result->rows[result->row_count++], right_r->rows[i], sizeof(NqlValue) * result->col_count);
        }
        break;
    case NQL_TOK_INTERSECT:
        for (uint32_t i = 0u; i < left_r->row_count && result->row_count < NQL_MAX_RESULT_ROWS; ++i) {
            for (uint32_t j = 0u; j < right_r->row_count; ++j) {
                if (nql_val_as_int(&left_r->rows[i][0]) == nql_val_as_int(&right_r->rows[j][0])) {
                    memcpy(result->rows[result->row_count++], left_r->rows[i], sizeof(NqlValue) * result->col_count);
                    break;
                }
            }
        }
        break;
    case NQL_TOK_EXCEPT:
        for (uint32_t i = 0u; i < left_r->row_count && result->row_count < NQL_MAX_RESULT_ROWS; ++i) {
            bool found = false;
            for (uint32_t j = 0u; j < right_r->row_count; ++j) {
                if (nql_val_as_int(&left_r->rows[i][0]) == nql_val_as_int(&right_r->rows[j][0])) {
                    found = true; break;
                }
            }
            if (!found) memcpy(result->rows[result->row_count++], left_r->rows[i], sizeof(NqlValue) * result->col_count);
        }
        break;
    default:
        nql_result_error(result, "unknown set operation");
        break;
    }
    free(left_r);
    free(right_r);
}

/* ── WITH RECURSIVE executor ── */

static void nql_exec_with_recursive(NqlEvalCtx* ctx, const NqlAstNode* node, NqlResult* result) {
    nql_result_init(result);
    if (!node || node->type != NQL_NODE_WITH_RECURSIVE) {
        nql_result_error(result, "internal: not a WITH RECURSIVE node");
        return;
    }

    /* 1. Execute base SELECT into accumulated result */
    NqlResult* accum = (NqlResult*)malloc(sizeof(NqlResult));
    NqlResult* iter_in = (NqlResult*)malloc(sizeof(NqlResult));
    NqlResult* iter_out = (NqlResult*)malloc(sizeof(NqlResult));
    if (!accum || !iter_in || !iter_out) {
        nql_result_error(result, "out of memory");
        free(accum); free(iter_in); free(iter_out); return;
    }

    nql_exec_select(ctx, node->d.with_recursive.base_select, accum);
    if (accum->has_error) { *result = *accum; free(accum); free(iter_in); free(iter_out); return; }

    /* Copy base rows as first iteration input */
    memcpy(iter_in, accum, sizeof(NqlResult));

    /* 2. Iterate: run recursive SELECT with iter_in as the CTE source */
    uint32_t max_iterations = 10000u;
    for (uint32_t iteration = 0u; iteration < max_iterations; ++iteration) {
        if (iter_in->row_count == 0u) break;

        /* Set up context: recursive SELECT's FROM <cte_name> reads from iter_in */
        NqlResult* saved_sub = ctx->subquery_result;
        uint32_t saved_row = ctx->subquery_row;
        const char* saved_col = ctx->col_name;

        /* Execute recursive step with iter_in as subquery source */
        ctx->subquery_result = iter_in;
        if (iter_in->col_count > 0u)
            ctx->col_name = iter_in->col_names[0];

        nql_result_init(iter_out);
        nql_exec_select(ctx, node->d.with_recursive.recursive_select, iter_out);

        ctx->subquery_result = saved_sub;
        ctx->subquery_row = saved_row;
        ctx->col_name = saved_col;

        if (iter_out->has_error || iter_out->row_count == 0u) break;

        /* Append new rows to accumulated result */
        for (uint32_t r = 0u; r < iter_out->row_count && accum->row_count < NQL_MAX_RESULT_ROWS; ++r) {
            memcpy(accum->rows[accum->row_count], iter_out->rows[r],
                   sizeof(NqlValue) * accum->col_count);
            ++accum->row_count;
        }

        /* Next iteration input = this iteration's output */
        memcpy(iter_in, iter_out, sizeof(NqlResult));

        if (accum->row_count >= NQL_MAX_RESULT_ROWS) break;
    }

    /* 3. Execute outer SELECT with accumulated CTE as subquery source */
    NqlResult* saved_sub = ctx->subquery_result;
    uint32_t saved_row = ctx->subquery_row;
    const char* saved_col = ctx->col_name;

    ctx->subquery_result = accum;
    if (accum->col_count > 0u)
        ctx->col_name = accum->col_names[0];

    nql_exec_select(ctx, node->d.with_recursive.outer_select, result);

    ctx->subquery_result = saved_sub;
    ctx->subquery_row = saved_row;
    ctx->col_name = saved_col;

    free(accum);
    free(iter_in);
    free(iter_out);
}
