/**
 * cpss_nql_parser.c - NQL recursive-descent parser.
 *
 * Consumes NqlTokenList, produces an AST rooted at an NqlAstNode.
 * Grammar mirrors SQL: SELECT ... FROM ... WHERE ... ORDER BY ... LIMIT ...
 * Also handles LET assignments and set operations (UNION/INTERSECT/EXCEPT).
 */

/* ── Parser state ── */

/**
 * State object for the NQL recursive-descent parser.
 * Tracks the current token stream, the AST node allocation pool,
 * and any errors encountered during parsing.
 */
typedef struct {
    const NqlTokenList* tl;         /* The input token list to parse */
    uint32_t            cur;        /* The index of the current token being examined */
    NqlAstPool*         pool;       /* Arena allocator for creating new AST nodes */
    char                error[256]; /* Buffer for the first error message encountered */
    bool                has_error;  /* Flag indicating if parsing failed */
} NqlParser;

/** Initialize a parser instance with a token list and an AST pool. */
static void nql_parser_init(NqlParser* p, const NqlTokenList* tl, NqlAstPool* pool) {
    p->tl = tl;
    p->cur = 0u;
    p->pool = pool;
    p->error[0] = '\0';
    p->has_error = false;
}

/** Peek at the current token without advancing. Returns EOF token if at end. */
static const NqlToken* nql_peek(NqlParser* p) {
    if (p->cur < p->tl->count) return &p->tl->tokens[p->cur];
    return &p->tl->tokens[p->tl->count - 1u]; /* EOF */
}

/** Return the current token and advance the parser to the next one. */
static const NqlToken* nql_advance(NqlParser* p) {
    const NqlToken* t = nql_peek(p);
    if (t->type != NQL_TOK_EOF) ++p->cur;
    return t;
}

/** Check if the current token matches the given type without advancing. */
static bool nql_check(NqlParser* p, NqlTokenType type) {
    return nql_peek(p)->type == type;
}

/** If the current token matches the given type, advance and return true. Otherwise return false. */
static bool nql_match(NqlParser* p, NqlTokenType type) {
    if (nql_peek(p)->type == type) { nql_advance(p); return true; }
    return false;
}

/** 
 * Assert the current token matches the expected type and advance.
 * If it doesn't match, record an error and return NULL.
 */
static const NqlToken* nql_expect(NqlParser* p, NqlTokenType type) {
    const NqlToken* t = nql_peek(p);
    if (t->type == type) return nql_advance(p);
    snprintf(p->error, sizeof(p->error), "expected %s, got %s at pos %u",
             nql_tok_name(type), nql_tok_name(t->type), t->pos);
    p->has_error = true;
    return NULL;
}

/** Record a formatted parser error. Only the first error is preserved. */
static void nql_error(NqlParser* p, const char* fmt, ...) {
    if (p->has_error) return;
    p->has_error = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->error, sizeof(p->error), fmt, ap);
    va_end(ap);
}

/** Allocate a new AST node from the parser's pool. */
static NqlAstNode* nql_alloc(NqlParser* p) {
    NqlAstNode* n = nql_pool_alloc(p->pool);
    if (!n) nql_error(p, "AST pool exhausted (max %d nodes)", NQL_AST_POOL_SIZE);
    return n;
}

/** Copy a token's text into a fixed-size buffer. */
static void nql_tok_copy(const NqlToken* t, char* buf, size_t bufsz) {
    size_t len = t->length;
    if (len >= bufsz) len = bufsz - 1u;
    memcpy(buf, t->start, len);
    buf[len] = '\0';
}

/* ── Forward declarations for recursive descent ── */
static NqlAstNode* nql_parse_expr(NqlParser* p);
static NqlAstNode* nql_parse_select(NqlParser* p);
static NqlAstNode* nql_parse_statement(NqlParser* p);

/* ── Expression parsing (precedence climbing) ── */

static NqlAstNode* nql_parse_atom(NqlParser* p) {
    if (p->has_error) return NULL;
    const NqlToken* t = nql_peek(p);

    /* Integer literal (may be int64, U128, or BigNum depending on magnitude) */
    if (t->type == NQL_TOK_INT) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_LITERAL;
        n->token = *t;
        /* Check if the lexer flagged overflow (ival==0 && length > 18 digits) */
        if (t->val.ival == 0 && t->length > 18u && t->start) {
            /* Re-parse from the raw digit span into BigNum, then auto-narrow */
            char digitbuf[512];
            uint32_t dlen = t->length < 511u ? t->length : 511u;
            memcpy(digitbuf, t->start, dlen);
            digitbuf[dlen] = '\0';
            BigNum bn;
            bn_from_str(&bn, digitbuf);
            n->d.literal = nql_val_from_bignum(&bn);
        } else {
            n->d.literal = nql_val_int(t->val.ival);
        }
        return n;
    }

    /* Float literal */
    if (t->type == NQL_TOK_FLOAT) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_LITERAL;
        n->token = *t;
        n->d.literal = nql_val_float(t->val.fval);
        return n;
    }

    /* String literal */
    if (t->type == NQL_TOK_STRING) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_LITERAL;
        n->token = *t;
        char buf[256];
        nql_tok_copy(t, buf, sizeof(buf));
        n->d.literal = nql_val_str(buf);
        return n;
    }

    /* TRUE / FALSE */
    if (t->type == NQL_TOK_TRUE_KW) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_LITERAL;
        n->token = *t;
        n->d.literal = nql_val_bool(true);
        return n;
    }
    if (t->type == NQL_TOK_FALSE_KW) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_LITERAL;
        n->token = *t;
        n->d.literal = nql_val_bool(false);
        return n;
    }

    /* NULL */
    if (t->type == NQL_TOK_NULL_KW) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_LITERAL;
        n->token = *t;
        n->d.literal = nql_val_null();
        return n;
    }

    /* IF(cond, then_expr, else_expr) */
    if (t->type == NQL_TOK_IF) {
        nql_advance(p);
        if (!nql_expect(p, NQL_TOK_LPAREN)) return NULL;
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_IF;
        n->token = *t;
        n->d.if_expr.cond = nql_parse_expr(p);
        if (!nql_expect(p, NQL_TOK_COMMA)) return NULL;
        n->d.if_expr.then_expr = nql_parse_expr(p);
        if (!nql_expect(p, NQL_TOK_COMMA)) return NULL;
        n->d.if_expr.else_expr = nql_parse_expr(p);
        if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
        return n;
    }

    /* CASE WHEN cond THEN expr [WHEN ...] [ELSE expr] END */
    if (t->type == NQL_TOK_CASE) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_CASE;
        n->token = *t;
        n->d.case_expr.when_count = 0u;
        n->d.case_expr.else_expr = NULL;
        while (nql_check(p, NQL_TOK_WHEN) && n->d.case_expr.when_count < NQL_MAX_CHILDREN) {
            nql_advance(p); /* consume WHEN */
            uint32_t wi = n->d.case_expr.when_count;
            n->d.case_expr.when_conds[wi] = nql_parse_expr(p);
            if (!nql_expect(p, NQL_TOK_THEN)) return NULL;
            n->d.case_expr.when_exprs[wi] = nql_parse_expr(p);
            ++n->d.case_expr.when_count;
        }
        if (nql_match(p, NQL_TOK_ELSE)) {
            n->d.case_expr.else_expr = nql_parse_expr(p);
        }
        if (!nql_expect(p, NQL_TOK_END)) return NULL;
        return n;
    }

    /* EXISTS / NOT EXISTS (subquery) */
    if (t->type == NQL_TOK_EXISTS) {
        nql_advance(p);
        if (!nql_expect(p, NQL_TOK_LPAREN)) return NULL;
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_EXISTS;
        n->token = *t;
        n->d.exists.negated = false;
        n->d.exists.subquery = nql_parse_select(p);
        if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
        return n;
    }

    /* Aggregate: COUNT/SUM/MIN/MAX/AVG */
    if (t->type == NQL_TOK_COUNT || t->type == NQL_TOK_SUM ||
        t->type == NQL_TOK_MIN || t->type == NQL_TOK_MAX || t->type == NQL_TOK_AVG) {
        NqlTokenType agg = t->type;
        nql_advance(p);
        if (!nql_expect(p, NQL_TOK_LPAREN)) return NULL;
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_AGGREGATE;
        n->token = *t;
        n->d.aggregate.agg_type = agg;
        if (nql_check(p, NQL_TOK_STAR)) {
            nql_advance(p);
            n->d.aggregate.is_star = true;
            n->d.aggregate.operand = NULL;
        } else {
            n->d.aggregate.is_star = false;
            n->d.aggregate.operand = nql_parse_expr(p);
        }
        if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
        return n;
    }

    /* Window function: LAG/LEAD */
    if (t->type == NQL_TOK_LAG || t->type == NQL_TOK_LEAD) {
        NqlTokenType wt = t->type;
        nql_advance(p);
        if (!nql_expect(p, NQL_TOK_LPAREN)) return NULL;
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_WINDOW;
        n->token = *t;
        n->d.window.win_type = wt;
        n->d.window.operand = nql_parse_expr(p);
        n->d.window.offset = NULL;
        if (nql_match(p, NQL_TOK_COMMA)) {
            n->d.window.offset = nql_parse_expr(p);
        }
        if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
        return n;
    }

    /* Parenthesised expression or subquery */
    if (t->type == NQL_TOK_LPAREN) {
        nql_advance(p);
        NqlAstNode* inner;
        if (nql_check(p, NQL_TOK_SELECT)) {
            inner = nql_parse_select(p);
        } else {
            inner = nql_parse_expr(p);
        }
        if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
        return inner;
    }

    /* Identifier or function call */
    if (t->type == NQL_TOK_IDENT) {
        nql_advance(p);
        /* Function call? */
        if (nql_check(p, NQL_TOK_LPAREN)) {
            nql_advance(p); /* consume ( */
            NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
            n->type = NQL_NODE_FUNC_CALL;
            n->token = *t;
            nql_tok_copy(t, n->d.func_call.name, sizeof(n->d.func_call.name));
            n->d.func_call.arg_count = 0u;
            if (!nql_check(p, NQL_TOK_RPAREN)) {
                n->d.func_call.args[n->d.func_call.arg_count++] = nql_parse_expr(p);
                while (nql_match(p, NQL_TOK_COMMA)) {
                    if (n->d.func_call.arg_count >= NQL_MAX_CHILDREN) {
                        nql_error(p, "too many function arguments");
                        return NULL;
                    }
                    n->d.func_call.args[n->d.func_call.arg_count++] = nql_parse_expr(p);
                }
            }
            if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
            return n;
        }
        /* Plain identifier */
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_IDENT;
        n->token = *t;
        nql_tok_copy(t, n->d.ident.name, sizeof(n->d.ident.name));
        return n;
    }

    /* STAR as wildcard in select position — handled specially */
    if (t->type == NQL_TOK_STAR) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_STAR;
        n->token = *t;
        return n;
    }

    nql_error(p, "unexpected %s at pos %u", nql_tok_name(t->type), t->pos);
    return NULL;
}

static NqlAstNode* nql_parse_unary(NqlParser* p) {
    if (p->has_error) return NULL;
    const NqlToken* t = nql_peek(p);

    if (t->type == NQL_TOK_MINUS) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_UNARYOP;
        n->token = *t;
        n->d.unaryop.op = NQL_TOK_MINUS;
        n->d.unaryop.operand = nql_parse_unary(p);
        return n;
    }
    if (t->type == NQL_TOK_NOT) {
        nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_UNARYOP;
        n->token = *t;
        n->d.unaryop.op = NQL_TOK_NOT;
        n->d.unaryop.operand = nql_parse_unary(p);
        return n;
    }
    return nql_parse_atom(p);
}

static NqlAstNode* nql_parse_term(NqlParser* p) {
    if (p->has_error) return NULL;
    NqlAstNode* left = nql_parse_unary(p);
    while (!p->has_error &&
           (nql_check(p, NQL_TOK_STAR) || nql_check(p, NQL_TOK_SLASH) || nql_check(p, NQL_TOK_PERCENT))) {
        const NqlToken* op = nql_advance(p);
        NqlAstNode* right = nql_parse_unary(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_BINOP;
        n->token = *op;
        n->d.binop.op = op->type;
        n->d.binop.left = left;
        n->d.binop.right = right;
        left = n;
    }
    return left;
}

static NqlAstNode* nql_parse_additive(NqlParser* p) {
    if (p->has_error) return NULL;
    NqlAstNode* left = nql_parse_term(p);
    while (!p->has_error &&
           (nql_check(p, NQL_TOK_PLUS) || nql_check(p, NQL_TOK_MINUS))) {
        const NqlToken* op = nql_advance(p);
        NqlAstNode* right = nql_parse_term(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_BINOP;
        n->token = *op;
        n->d.binop.op = op->type;
        n->d.binop.left = left;
        n->d.binop.right = right;
        left = n;
    }
    return left;
}

static NqlAstNode* nql_parse_comparison(NqlParser* p) {
    if (p->has_error) return NULL;
    NqlAstNode* left = nql_parse_additive(p);
    if (p->has_error) return NULL;

    /* BETWEEN */
    if (nql_check(p, NQL_TOK_BETWEEN)) {
        const NqlToken* bt = nql_advance(p);
        NqlAstNode* lo = nql_parse_additive(p);
        if (!nql_expect(p, NQL_TOK_AND)) return NULL;
        NqlAstNode* hi = nql_parse_additive(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_BETWEEN;
        n->token = *bt;
        n->d.between.expr = left;
        n->d.between.lo = lo;
        n->d.between.hi = hi;
        return n;
    }

    /* IN (list) */
    if (nql_check(p, NQL_TOK_IN)) {
        const NqlToken* it = nql_advance(p);
        if (!nql_expect(p, NQL_TOK_LPAREN)) return NULL;
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_IN_LIST;
        n->token = *it;
        n->d.in_list.expr = left;
        n->d.in_list.item_count = 0u;
        n->d.in_list.items[n->d.in_list.item_count++] = nql_parse_expr(p);
        while (nql_match(p, NQL_TOK_COMMA)) {
            if (n->d.in_list.item_count >= NQL_MAX_CHILDREN) {
                nql_error(p, "too many items in IN list");
                return NULL;
            }
            n->d.in_list.items[n->d.in_list.item_count++] = nql_parse_expr(p);
        }
        if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
        return n;
    }

    /* Comparison operators */
    if (nql_check(p, NQL_TOK_EQ) || nql_check(p, NQL_TOK_NEQ) ||
        nql_check(p, NQL_TOK_LT) || nql_check(p, NQL_TOK_GT) ||
        nql_check(p, NQL_TOK_LEQ) || nql_check(p, NQL_TOK_GEQ)) {
        const NqlToken* op = nql_advance(p);
        NqlAstNode* right = nql_parse_additive(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_BINOP;
        n->token = *op;
        n->d.binop.op = op->type;
        n->d.binop.left = left;
        n->d.binop.right = right;
        return n;
    }

    return left;
}

static NqlAstNode* nql_parse_not_expr(NqlParser* p) {
    if (p->has_error) return NULL;
    if (nql_check(p, NQL_TOK_NOT)) {
        const NqlToken* t = nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_UNARYOP;
        n->token = *t;
        n->d.unaryop.op = NQL_TOK_NOT;
        n->d.unaryop.operand = nql_parse_comparison(p);
        return n;
    }
    return nql_parse_comparison(p);
}

static NqlAstNode* nql_parse_and_expr(NqlParser* p) {
    if (p->has_error) return NULL;
    NqlAstNode* left = nql_parse_not_expr(p);
    while (!p->has_error && nql_check(p, NQL_TOK_AND)) {
        const NqlToken* op = nql_advance(p);
        NqlAstNode* right = nql_parse_not_expr(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_BINOP;
        n->token = *op;
        n->d.binop.op = NQL_TOK_AND;
        n->d.binop.left = left;
        n->d.binop.right = right;
        left = n;
    }
    return left;
}

static NqlAstNode* nql_parse_or_expr(NqlParser* p) {
    if (p->has_error) return NULL;
    NqlAstNode* left = nql_parse_and_expr(p);
    while (!p->has_error && nql_check(p, NQL_TOK_OR)) {
        const NqlToken* op = nql_advance(p);
        NqlAstNode* right = nql_parse_and_expr(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_BINOP;
        n->token = *op;
        n->d.binop.op = NQL_TOK_OR;
        n->d.binop.left = left;
        n->d.binop.right = right;
        left = n;
    }
    return left;
}

/** Top-level expression: OR precedence. */
static NqlAstNode* nql_parse_expr(NqlParser* p) {
    return nql_parse_or_expr(p);
}

/* ── SELECT statement parsing ── */

/**
 * Parse a single item in the SELECT list.
 * Can be: *, expr, expr AS alias
 */
static NqlAstNode* nql_parse_select_item(NqlParser* p) {
    if (p->has_error) return NULL;
    NqlAstNode* expr = nql_parse_expr(p);
    if (p->has_error) return NULL;

    /* AS alias? */
    if (nql_match(p, NQL_TOK_AS)) {
        const NqlToken* alias_tok = nql_expect(p, NQL_TOK_IDENT);
        if (!alias_tok) return NULL;
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_ALIAS;
        n->token = *alias_tok;
        n->d.alias.expr = expr;
        nql_tok_copy(alias_tok, n->d.alias.alias, sizeof(n->d.alias.alias));
        return n;
    }

    /* Implicit alias from identifier name */
    return expr;
}

/**
 * Parse a full SELECT statement.
 * SELECT [DISTINCT] select_list FROM type_name [(param)]
 *   [WHERE condition] [ORDER BY expr [ASC|DESC], ...] [LIMIT expr]
 */
static NqlAstNode* nql_parse_select(NqlParser* p) {
    if (p->has_error) return NULL;
    const NqlToken* sel_tok = nql_expect(p, NQL_TOK_SELECT);
    if (!sel_tok) return NULL;

    NqlAstNode* node = nql_alloc(p); if (!node) return NULL;
    node->type = NQL_NODE_SELECT;
    node->token = *sel_tok;
    memset(&node->d.select, 0, sizeof(node->d.select));

    /* DISTINCT? */
    node->d.select.distinct = nql_match(p, NQL_TOK_DISTINCT);

    /* Select list */
    node->d.select.col_count = 0u;
    node->d.select.columns[node->d.select.col_count++] = nql_parse_select_item(p);
    while (!p->has_error && nql_match(p, NQL_TOK_COMMA)) {
        if (node->d.select.col_count >= NQL_MAX_CHILDREN) {
            nql_error(p, "too many columns in SELECT");
            return NULL;
        }
        node->d.select.columns[node->d.select.col_count++] = nql_parse_select_item(p);
    }

    /* FROM */
    if (!nql_expect(p, NQL_TOK_FROM)) return NULL;

    /* FROM (subquery) or FROM type_name [alias] [, type_name2 [alias2]] */
    node->d.select.from_alias[0] = '\0';
    node->d.select.from_type2[0] = '\0';
    node->d.select.from_alias2[0] = '\0';
    node->d.select.from_arg2 = NULL;

    if (nql_check(p, NQL_TOK_LPAREN)) {
        nql_advance(p);
        node->d.select.subquery_from = nql_parse_select(p);
        if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
        node->d.select.from_type[0] = '\0';
    } else {
        /* Type name: could be a keyword like PRIME or an identifier */
        const NqlToken* type_tok = nql_advance(p);
        nql_tok_copy(type_tok, node->d.select.from_type, sizeof(node->d.select.from_type));

        /* Optional parameter: SMOOTH(7), RANGE(1,100) */
        if (nql_check(p, NQL_TOK_LPAREN)) {
            nql_advance(p);
            node->d.select.from_arg = nql_parse_expr(p);
            if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
        }

        /* Optional alias: FROM PRIME p */
        if (nql_check(p, NQL_TOK_IDENT)) {
            const NqlToken* alias_tok = nql_advance(p);
            nql_tok_copy(alias_tok, node->d.select.from_alias, sizeof(node->d.select.from_alias));
        } else if (nql_match(p, NQL_TOK_AS)) {
            const NqlToken* alias_tok = nql_expect(p, NQL_TOK_IDENT);
            if (alias_tok) nql_tok_copy(alias_tok, node->d.select.from_alias, sizeof(node->d.select.from_alias));
        }

        /* Cross-join: FROM PRIME p, PRIME q */
        if (nql_match(p, NQL_TOK_COMMA)) {
            const NqlToken* type_tok2 = nql_advance(p);
            nql_tok_copy(type_tok2, node->d.select.from_type2, sizeof(node->d.select.from_type2));

            /* Optional parameter for second source */
            if (nql_check(p, NQL_TOK_LPAREN)) {
                nql_advance(p);
                node->d.select.from_arg2 = nql_parse_expr(p);
                if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
            }

            /* Optional alias for second source */
            if (nql_check(p, NQL_TOK_IDENT)) {
                const NqlToken* alias_tok2 = nql_advance(p);
                nql_tok_copy(alias_tok2, node->d.select.from_alias2, sizeof(node->d.select.from_alias2));
            } else if (nql_match(p, NQL_TOK_AS)) {
                const NqlToken* alias_tok2 = nql_expect(p, NQL_TOK_IDENT);
                if (alias_tok2) nql_tok_copy(alias_tok2, node->d.select.from_alias2, sizeof(node->d.select.from_alias2));
            }
        }
    }

    /* WHERE */
    if (nql_match(p, NQL_TOK_WHERE)) {
        node->d.select.where_clause = nql_parse_expr(p);
    }

    /* ORDER BY */
    if (nql_check(p, NQL_TOK_ORDER)) {
        nql_advance(p);
        if (!nql_expect(p, NQL_TOK_BY)) return NULL;
        node->d.select.order_count = 0u;
        node->d.select.order_exprs[node->d.select.order_count] = nql_parse_expr(p);
        node->d.select.order_desc[node->d.select.order_count] = false;
        if (nql_match(p, NQL_TOK_DESC)) node->d.select.order_desc[node->d.select.order_count] = true;
        else nql_match(p, NQL_TOK_ASC); /* consume optional ASC */
        ++node->d.select.order_count;
        while (nql_match(p, NQL_TOK_COMMA) && node->d.select.order_count < 8u) {
            node->d.select.order_exprs[node->d.select.order_count] = nql_parse_expr(p);
            node->d.select.order_desc[node->d.select.order_count] = false;
            if (nql_match(p, NQL_TOK_DESC)) node->d.select.order_desc[node->d.select.order_count] = true;
            else nql_match(p, NQL_TOK_ASC);
            ++node->d.select.order_count;
        }
    }

    /* LIMIT */
    if (nql_match(p, NQL_TOK_LIMIT)) {
        node->d.select.limit_expr = nql_parse_expr(p);
    }

    return node;
}

/* ── LET statement ── */

static NqlAstNode* nql_parse_let(NqlParser* p) {
    if (p->has_error) return NULL;
    const NqlToken* let_tok = nql_expect(p, NQL_TOK_LET);
    if (!let_tok) return NULL;

    const NqlToken* name_tok = nql_expect(p, NQL_TOK_IDENT);
    if (!name_tok) return NULL;

    if (!nql_expect(p, NQL_TOK_EQ)) return NULL;

    NqlAstNode* value = nql_parse_expr(p);
    if (p->has_error) return NULL;

    NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
    n->type = NQL_NODE_LET;
    n->token = *let_tok;
    nql_tok_copy(name_tok, n->d.let.name, sizeof(n->d.let.name));
    n->d.let.value = value;
    return n;
}

/* ── CREATE FUNCTION name(params) = expr ── */

static NqlAstNode* nql_parse_create_func(NqlParser* p) {
    if (p->has_error) return NULL;
    const NqlToken* ct = nql_expect(p, NQL_TOK_CREATE);
    if (!ct) return NULL;
    if (!nql_expect(p, NQL_TOK_FUNCTION)) return NULL;

    const NqlToken* name_tok = nql_expect(p, NQL_TOK_IDENT);
    if (!name_tok) return NULL;

    NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
    n->type = NQL_NODE_CREATE_FUNC;
    n->token = *ct;
    nql_tok_copy(name_tok, n->d.create_func.name, sizeof(n->d.create_func.name));
    n->d.create_func.param_count = 0u;
    n->d.create_func.body = NULL;

    /* Parameter list */
    if (!nql_expect(p, NQL_TOK_LPAREN)) return NULL;
    if (!nql_check(p, NQL_TOK_RPAREN)) {
        const NqlToken* pt = nql_expect(p, NQL_TOK_IDENT);
        if (!pt) return NULL;
        nql_tok_copy(pt, n->d.create_func.params[n->d.create_func.param_count++], 64);
        while (nql_match(p, NQL_TOK_COMMA) && n->d.create_func.param_count < 8u) {
            pt = nql_expect(p, NQL_TOK_IDENT);
            if (!pt) return NULL;
            nql_tok_copy(pt, n->d.create_func.params[n->d.create_func.param_count++], 64);
        }
    }
    if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
    if (!nql_expect(p, NQL_TOK_EQ)) return NULL;

    n->d.create_func.body = nql_parse_expr(p);
    return n;
}

/* ── Helper: parse a statement body (sequence until END/ELSE/EOF/semicolon boundary) ── */

static NqlAstNode* nql_parse_statement(NqlParser* p); /* forward */

static uint32_t nql_parse_body(NqlParser* p, NqlAstNode* stmts[], uint32_t max_stmts,
                                NqlTokenType end1, NqlTokenType end2) {
    uint32_t count = 0u;
    while (!p->has_error && count < max_stmts) {
        while (nql_match(p, NQL_TOK_SEMICOLON)) {} /* skip separators */
        if (nql_check(p, NQL_TOK_EOF)) break;
        if (nql_check(p, end1)) break;
        if (end2 != NQL_TOK_EOF && nql_check(p, end2)) break;
        stmts[count] = nql_parse_statement(p);
        if (!stmts[count]) break;
        ++count;
        while (nql_match(p, NQL_TOK_SEMICOLON)) {}
    }
    return count;
}

/* ── Top-level statement, with optional set operations ── */

static NqlAstNode* nql_parse_statement(NqlParser* p) {
    if (p->has_error) return NULL;

    /* ── PRINT expr, expr, ... ── */
    if (nql_check(p, NQL_TOK_PRINT)) {
        const NqlToken* pt = nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_PRINT;
        n->token = *pt;
        n->d.print.expr_count = 0u;
        if (!nql_check(p, NQL_TOK_SEMICOLON) && !nql_check(p, NQL_TOK_EOF)) {
            n->d.print.exprs[n->d.print.expr_count++] = nql_parse_expr(p);
            while (nql_match(p, NQL_TOK_COMMA) && n->d.print.expr_count < NQL_MAX_CHILDREN) {
                n->d.print.exprs[n->d.print.expr_count++] = nql_parse_expr(p);
            }
        }
        return n;
    }

    /* ── ASSERT cond [, message] ── */
    if (nql_check(p, NQL_TOK_ASSERT)) {
        const NqlToken* at = nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_ASSERT;
        n->token = *at;
        n->d.assert_stmt.cond = nql_parse_expr(p);
        n->d.assert_stmt.message = NULL;
        if (nql_match(p, NQL_TOK_COMMA)) {
            n->d.assert_stmt.message = nql_parse_expr(p);
        }
        return n;
    }

    /* ── BREAK ── */
    if (nql_check(p, NQL_TOK_BREAK)) {
        const NqlToken* bt = nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_BREAK;
        n->token = *bt;
        return n;
    }

    /* ── CONTINUE ── */
    if (nql_check(p, NQL_TOK_CONTINUE)) {
        const NqlToken* ct = nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_CONTINUE;
        n->token = *ct;
        return n;
    }

    /* ── RETURN expr ── */
    if (nql_check(p, NQL_TOK_RETURN)) {
        const NqlToken* rt = nql_advance(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_RETURN;
        n->token = *rt;
        n->d.return_stmt.expr = nql_parse_expr(p);
        return n;
    }

    /* ── FOR var FROM lo TO hi DO body END FOR ── */
    /* ── FOR EACH var IN source DO body END FOR ── */
    if (nql_check(p, NQL_TOK_FOR)) {
        const NqlToken* ft = nql_advance(p);

        if (nql_check(p, NQL_TOK_EACH)) {
            /* FOR EACH var IN source DO body END FOR */
            nql_advance(p);
            const NqlToken* var_tok = nql_expect(p, NQL_TOK_IDENT);
            if (!var_tok) return NULL;
            if (!nql_expect(p, NQL_TOK_IN)) return NULL;
            NqlAstNode* source = nql_parse_expr(p);
            if (!nql_expect(p, NQL_TOK_DO)) return NULL;
            NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
            n->type = NQL_NODE_FOR_EACH;
            n->token = *ft;
            nql_tok_copy(var_tok, n->d.for_each.var_name, sizeof(n->d.for_each.var_name));
            n->d.for_each.source = source;
            n->d.for_each.body_count = nql_parse_body(p, n->d.for_each.body, NQL_MAX_CHILDREN, NQL_TOK_END, NQL_TOK_EOF);
            if (!nql_expect(p, NQL_TOK_END)) return NULL;
            nql_match(p, NQL_TOK_FOR); /* optional END FOR */
            return n;
        } else {
            /* FOR var FROM lo TO hi DO body END FOR */
            const NqlToken* var_tok = nql_expect(p, NQL_TOK_IDENT);
            if (!var_tok) return NULL;
            if (!nql_expect(p, NQL_TOK_FROM)) return NULL;
            NqlAstNode* from_expr = nql_parse_expr(p);
            if (!nql_expect(p, NQL_TOK_TO)) return NULL;
            NqlAstNode* to_expr = nql_parse_expr(p);
            if (!nql_expect(p, NQL_TOK_DO)) return NULL;
            NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
            n->type = NQL_NODE_FOR;
            n->token = *ft;
            nql_tok_copy(var_tok, n->d.for_loop.var_name, sizeof(n->d.for_loop.var_name));
            n->d.for_loop.from_expr = from_expr;
            n->d.for_loop.to_expr = to_expr;
            n->d.for_loop.body_count = nql_parse_body(p, n->d.for_loop.body, NQL_MAX_CHILDREN, NQL_TOK_END, NQL_TOK_EOF);
            if (!nql_expect(p, NQL_TOK_END)) return NULL;
            nql_match(p, NQL_TOK_FOR); /* optional END FOR */
            return n;
        }
    }

    /* ── WHILE cond DO body END WHILE ── */
    if (nql_check(p, NQL_TOK_WHILE)) {
        const NqlToken* wt = nql_advance(p);
        NqlAstNode* cond = nql_parse_expr(p);
        if (!nql_expect(p, NQL_TOK_DO)) return NULL;
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_WHILE;
        n->token = *wt;
        n->d.while_loop.cond = cond;
        n->d.while_loop.body_count = nql_parse_body(p, n->d.while_loop.body, NQL_MAX_CHILDREN, NQL_TOK_END, NQL_TOK_EOF);
        if (!nql_expect(p, NQL_TOK_END)) return NULL;
        nql_match(p, NQL_TOK_WHILE); /* optional END WHILE */
        return n;
    }

    /* ── IF cond THEN body [ELSE IF ... | ELSE body] END IF ── */
    if (nql_check(p, NQL_TOK_IF)) {
        const NqlToken* it = nql_advance(p);
        NqlAstNode* cond = nql_parse_expr(p);
        if (!nql_expect(p, NQL_TOK_THEN)) return NULL;
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_IF_STMT;
        n->token = *it;
        n->d.if_stmt.cond = cond;
        n->d.if_stmt.then_count = nql_parse_body(p, n->d.if_stmt.then_body, NQL_MAX_CHILDREN, NQL_TOK_END, NQL_TOK_ELSE);
        n->d.if_stmt.else_count = 0u;
        if (nql_check(p, NQL_TOK_ELSE)) {
            nql_advance(p);
            if (nql_check(p, NQL_TOK_IF)) {
                /* ELSE IF — parse as nested IF_STMT in else_body[0] */
                n->d.if_stmt.else_body[0] = nql_parse_statement(p); /* recursion handles IF */
                n->d.if_stmt.else_count = 1u;
                return n; /* nested IF handles its own END IF */
            } else {
                n->d.if_stmt.else_count = nql_parse_body(p, n->d.if_stmt.else_body, NQL_MAX_CHILDREN, NQL_TOK_END, NQL_TOK_EOF);
            }
        }
        if (!nql_expect(p, NQL_TOK_END)) return NULL;
        nql_match(p, NQL_TOK_IF); /* optional END IF */
        return n;
    }

    NqlAstNode* left;
    if (nql_check(p, NQL_TOK_WITH)) {
        /* WITH RECURSIVE name AS (base UNION ALL recursive) outer_select */
        const NqlToken* wt = nql_advance(p);
        if (!nql_expect(p, NQL_TOK_RECURSIVE)) return NULL;
        const NqlToken* cte_name = nql_expect(p, NQL_TOK_IDENT);
        if (!cte_name) return NULL;
        if (!nql_expect(p, NQL_TOK_AS)) return NULL;
        if (!nql_expect(p, NQL_TOK_LPAREN)) return NULL;
        NqlAstNode* base_sel = nql_parse_select(p);
        if (!nql_expect(p, NQL_TOK_UNION)) return NULL;
        if (!nql_expect(p, NQL_TOK_ALL)) return NULL;
        NqlAstNode* rec_sel = nql_parse_select(p);
        if (!nql_expect(p, NQL_TOK_RPAREN)) return NULL;
        NqlAstNode* outer = nql_parse_select(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_WITH_RECURSIVE;
        n->token = *wt;
        nql_tok_copy(cte_name, n->d.with_recursive.cte_name, sizeof(n->d.with_recursive.cte_name));
        n->d.with_recursive.base_select = base_sel;
        n->d.with_recursive.recursive_select = rec_sel;
        n->d.with_recursive.outer_select = outer;
        return n;
    } else if (nql_check(p, NQL_TOK_CREATE)) {
        left = nql_parse_create_func(p);
    } else if (nql_check(p, NQL_TOK_LET)) {
        left = nql_parse_let(p);
    } else if (nql_check(p, NQL_TOK_SELECT)) {
        left = nql_parse_select(p);
    } else {
        /* Bare expression: evaluate and print */
        left = nql_parse_expr(p);
    }

    /* Set operations: UNION / INTERSECT / EXCEPT */
    while (!p->has_error &&
           (nql_check(p, NQL_TOK_UNION) || nql_check(p, NQL_TOK_INTERSECT) || nql_check(p, NQL_TOK_EXCEPT))) {
        const NqlToken* op = nql_advance(p);
        NqlAstNode* right = nql_parse_select(p);
        NqlAstNode* n = nql_alloc(p); if (!n) return NULL;
        n->type = NQL_NODE_SET_OP;
        n->token = *op;
        n->d.set_op.op = op->type;
        n->d.set_op.left = left;
        n->d.set_op.right = right;
        left = n;
    }

    return left;
}

/**
 * Parse a full NQL input string into an AST.
 * Returns the root node, or NULL on error (message in p->error).
 */
static NqlAstNode* nql_parse(const char* src, NqlAstPool* pool, char* err_buf, size_t err_buf_sz) {
    NqlTokenList tl;
    if (!nql_lex(src, &tl)) {
        snprintf(err_buf, err_buf_sz, "lexer error: %s", tl.error);
        return NULL;
    }

    NqlParser parser;
    nql_parser_init(&parser, &tl, pool);
    NqlAstNode* root = nql_parse_statement(&parser);

    if (parser.has_error) {
        snprintf(err_buf, err_buf_sz, "parse error: %s", parser.error);
        return NULL;
    }

    /* Check for trailing tokens (skip semicolons) */
    while (nql_match(&parser, NQL_TOK_SEMICOLON)) {}
    if (!nql_check(&parser, NQL_TOK_EOF)) {
        const NqlToken* t = nql_peek(&parser);
        snprintf(err_buf, err_buf_sz, "unexpected %s at pos %u after statement",
                 nql_tok_name(t->type), t->pos);
        return NULL;
    }

    err_buf[0] = '\0';
    return root;
}
