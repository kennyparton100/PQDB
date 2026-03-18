/**
 * cpss_nql_lexer.c - NQL tokeniser.
 *
 * Single-pass hand-written scanner. Case-insensitive keywords,
 * integer literals, identifiers, operators, and delimiters.
 * Produces a flat NqlTokenList consumed by the parser.
 */

/* ── Keyword table ── */

typedef struct {
    const char*  text;
    NqlTokenType type;
} NqlKeyword;

static const NqlKeyword g_nql_keywords[] = {
    { "SELECT",    NQL_TOK_SELECT },
    { "FROM",      NQL_TOK_FROM },
    { "WHERE",     NQL_TOK_WHERE },
    { "AND",       NQL_TOK_AND },
    { "OR",        NQL_TOK_OR },
    { "NOT",       NQL_TOK_NOT },
    { "BETWEEN",   NQL_TOK_BETWEEN },
    { "IN",        NQL_TOK_IN },
    { "ORDER",     NQL_TOK_ORDER },
    { "BY",        NQL_TOK_BY },
    { "ASC",       NQL_TOK_ASC },
    { "DESC",      NQL_TOK_DESC },
    { "LIMIT",     NQL_TOK_LIMIT },
    { "AS",        NQL_TOK_AS },
    { "LET",       NQL_TOK_LET },
    { "DISTINCT",  NQL_TOK_DISTINCT },
    { "IS",        NQL_TOK_IS },
    { "NULL",      NQL_TOK_NULL_KW },
    { "TRUE",      NQL_TOK_TRUE_KW },
    { "FALSE",     NQL_TOK_FALSE_KW },
    { "UNION",     NQL_TOK_UNION },
    { "INTERSECT", NQL_TOK_INTERSECT },
    { "EXCEPT",    NQL_TOK_EXCEPT },
    { "COUNT",     NQL_TOK_COUNT },
    { "SUM",       NQL_TOK_SUM },
    { "MIN",       NQL_TOK_MIN },
    { "MAX",       NQL_TOK_MAX },
    { "AVG",       NQL_TOK_AVG },
    { "LAG",       NQL_TOK_LAG },
    { "LEAD",      NQL_TOK_LEAD },
    { "IF",        NQL_TOK_IF },
    { "CASE",      NQL_TOK_CASE },
    { "WHEN",      NQL_TOK_WHEN },
    { "THEN",      NQL_TOK_THEN },
    { "ELSE",      NQL_TOK_ELSE },
    { "END",       NQL_TOK_END },
    { "CREATE",    NQL_TOK_CREATE },
    { "FUNCTION",  NQL_TOK_FUNCTION },
    { "WITH",      NQL_TOK_WITH },
    { "RECURSIVE", NQL_TOK_RECURSIVE },
    { "ALL",       NQL_TOK_ALL },
    { "EXISTS",    NQL_TOK_EXISTS },
};

#define NQL_KEYWORD_COUNT (sizeof(g_nql_keywords) / sizeof(g_nql_keywords[0]))

static bool nql_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool nql_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool nql_is_alnum(char c) {
    return nql_is_alpha(c) || nql_is_digit(c);
}

/** Case-insensitive comparison of exactly `len` chars. */
static bool nql_keyword_match(const char* src, uint32_t len, const char* kw) {
    for (uint32_t i = 0u; i < len; ++i) {
        char a = src[i];
        char b = kw[i];
        if (b == '\0') return false;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (a != b) return false;
    }
    return kw[len] == '\0';
}

/** Try to match an identifier against the keyword table. */
static NqlTokenType nql_classify_ident(const char* start, uint32_t len) {
    for (size_t i = 0u; i < NQL_KEYWORD_COUNT; ++i) {
        if (nql_keyword_match(start, len, g_nql_keywords[i].text))
            return g_nql_keywords[i].type;
    }
    return NQL_TOK_IDENT;
}

/**
 * Tokenise a NQL source string into tl.
 * Returns true on success, false on error (message in tl->error).
 */
static bool nql_lex(const char* src, NqlTokenList* tl) {
    tl->count = 0u;
    tl->error[0] = '\0';

    const char* p = src;
    uint32_t pos = 0u;

    while (*p != '\0') {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            ++p; ++pos;
        }
        if (*p == '\0') break;

        if (tl->count >= NQL_MAX_TOKENS) {
            snprintf(tl->error, sizeof(tl->error), "too many tokens (max %d)", NQL_MAX_TOKENS);
            return false;
        }

        NqlToken* t = &tl->tokens[tl->count];
        memset(t, 0, sizeof(*t));
        t->start = p;
        t->pos = pos;

        /* ── Single-line comment: -- */
        if (p[0] == '-' && p[1] == '-') {
            while (*p != '\0' && *p != '\n') { ++p; ++pos; }
            continue;
        }

        /* ── Integer literal ── */
        if (nql_is_digit(*p)) {
            int64_t val = 0;
            const char* num_start = p;
            while (nql_is_digit(*p)) {
                val = val * 10 + (*p - '0');
                ++p; ++pos;
            }
            /* Check for float */
            if (*p == '.' && nql_is_digit(p[1])) {
                ++p; ++pos;
                double fval = (double)val;
                double frac = 0.1;
                while (nql_is_digit(*p)) {
                    fval += (*p - '0') * frac;
                    frac *= 0.1;
                    ++p; ++pos;
                }
                t->type = NQL_TOK_FLOAT;
                t->length = (uint32_t)(p - num_start);
                t->val.fval = fval;
            } else {
                t->type = NQL_TOK_INT;
                t->length = (uint32_t)(p - num_start);
                t->val.ival = val;
            }
            ++tl->count;
            continue;
        }

        /* ── Identifier or keyword ── */
        if (nql_is_alpha(*p)) {
            const char* id_start = p;
            while (nql_is_alnum(*p)) { ++p; ++pos; }
            uint32_t len = (uint32_t)(p - id_start);
            t->type = nql_classify_ident(id_start, len);
            t->length = len;
            ++tl->count;
            continue;
        }

        /* ── String literal (single quotes) ── */
        if (*p == '\'') {
            ++p; ++pos;
            const char* str_start = p;
            while (*p != '\0' && *p != '\'') { ++p; ++pos; }
            if (*p != '\'') {
                snprintf(tl->error, sizeof(tl->error), "unterminated string at pos %u", t->pos);
                return false;
            }
            t->type = NQL_TOK_STRING;
            t->start = str_start;
            t->length = (uint32_t)(p - str_start);
            ++p; ++pos; /* skip closing quote */
            ++tl->count;
            continue;
        }

        /* ── Two-char operators ── */
        if (p[0] == '!' && p[1] == '=') { t->type = NQL_TOK_NEQ;  t->length = 2; p += 2; pos += 2; ++tl->count; continue; }
        if (p[0] == '<' && p[1] == '=') { t->type = NQL_TOK_LEQ;  t->length = 2; p += 2; pos += 2; ++tl->count; continue; }
        if (p[0] == '>' && p[1] == '=') { t->type = NQL_TOK_GEQ;  t->length = 2; p += 2; pos += 2; ++tl->count; continue; }
        if (p[0] == '<' && p[1] == '>') { t->type = NQL_TOK_NEQ;  t->length = 2; p += 2; pos += 2; ++tl->count; continue; }

        /* ── Single-char operators ── */
        switch (*p) {
            case '+': t->type = NQL_TOK_PLUS;      break;
            case '-': t->type = NQL_TOK_MINUS;      break;
            case '*': t->type = NQL_TOK_STAR;       break;
            case '/': t->type = NQL_TOK_SLASH;      break;
            case '%': t->type = NQL_TOK_PERCENT;    break;
            case '=': t->type = NQL_TOK_EQ;         break;
            case '<': t->type = NQL_TOK_LT;         break;
            case '>': t->type = NQL_TOK_GT;         break;
            case '(': t->type = NQL_TOK_LPAREN;     break;
            case ')': t->type = NQL_TOK_RPAREN;     break;
            case ',': t->type = NQL_TOK_COMMA;      break;
            case '.': t->type = NQL_TOK_DOT;        break;
            case ';': t->type = NQL_TOK_SEMICOLON;  break;
            default:
                snprintf(tl->error, sizeof(tl->error), "unexpected character '%c' at pos %u", *p, pos);
                return false;
        }
        t->length = 1u;
        ++p; ++pos;
        ++tl->count;
    }

    /* Append EOF */
    if (tl->count < NQL_MAX_TOKENS) {
        NqlToken* eof = &tl->tokens[tl->count];
        memset(eof, 0, sizeof(*eof));
        eof->type = NQL_TOK_EOF;
        eof->start = p;
        eof->pos = pos;
        ++tl->count;
    }

    return true;
}

/** Debug: token type name (for error messages). */
static const char* nql_tok_name(NqlTokenType t) {
    switch (t) {
        case NQL_TOK_INT:       return "integer";
        case NQL_TOK_FLOAT:     return "float";
        case NQL_TOK_STRING:    return "string";
        case NQL_TOK_IDENT:     return "identifier";
        case NQL_TOK_SELECT:    return "SELECT";
        case NQL_TOK_FROM:      return "FROM";
        case NQL_TOK_WHERE:     return "WHERE";
        case NQL_TOK_AND:       return "AND";
        case NQL_TOK_OR:        return "OR";
        case NQL_TOK_NOT:       return "NOT";
        case NQL_TOK_BETWEEN:   return "BETWEEN";
        case NQL_TOK_IN:        return "IN";
        case NQL_TOK_ORDER:     return "ORDER";
        case NQL_TOK_BY:        return "BY";
        case NQL_TOK_ASC:       return "ASC";
        case NQL_TOK_DESC:      return "DESC";
        case NQL_TOK_LIMIT:     return "LIMIT";
        case NQL_TOK_AS:        return "AS";
        case NQL_TOK_LET:       return "LET";
        case NQL_TOK_DISTINCT:  return "DISTINCT";
        case NQL_TOK_IS:        return "IS";
        case NQL_TOK_NULL_KW:   return "NULL";
        case NQL_TOK_TRUE_KW:   return "TRUE";
        case NQL_TOK_FALSE_KW:  return "FALSE";
        case NQL_TOK_UNION:     return "UNION";
        case NQL_TOK_INTERSECT: return "INTERSECT";
        case NQL_TOK_EXCEPT:    return "EXCEPT";
        case NQL_TOK_COUNT:     return "COUNT";
        case NQL_TOK_SUM:       return "SUM";
        case NQL_TOK_MIN:       return "MIN";
        case NQL_TOK_MAX:       return "MAX";
        case NQL_TOK_AVG:       return "AVG";
        case NQL_TOK_LAG:       return "LAG";
        case NQL_TOK_LEAD:      return "LEAD";
        case NQL_TOK_IF:        return "IF";
        case NQL_TOK_CASE:      return "CASE";
        case NQL_TOK_WHEN:      return "WHEN";
        case NQL_TOK_THEN:      return "THEN";
        case NQL_TOK_ELSE:      return "ELSE";
        case NQL_TOK_END:       return "END";
        case NQL_TOK_CREATE:    return "CREATE";
        case NQL_TOK_FUNCTION:  return "FUNCTION";
        case NQL_TOK_WITH:      return "WITH";
        case NQL_TOK_RECURSIVE: return "RECURSIVE";
        case NQL_TOK_ALL:       return "ALL";
        case NQL_TOK_EXISTS:    return "EXISTS";
        case NQL_TOK_PLUS:      return "+";
        case NQL_TOK_MINUS:     return "-";
        case NQL_TOK_STAR:      return "*";
        case NQL_TOK_SLASH:     return "/";
        case NQL_TOK_PERCENT:   return "%";
        case NQL_TOK_EQ:        return "=";
        case NQL_TOK_NEQ:       return "!=";
        case NQL_TOK_LT:        return "<";
        case NQL_TOK_GT:        return ">";
        case NQL_TOK_LEQ:       return "<=";
        case NQL_TOK_GEQ:       return ">=";
        case NQL_TOK_LPAREN:    return "(";
        case NQL_TOK_RPAREN:    return ")";
        case NQL_TOK_COMMA:     return ",";
        case NQL_TOK_DOT:       return ".";
        case NQL_TOK_SEMICOLON: return ";";
        case NQL_TOK_EOF:       return "end of input";
        case NQL_TOK_ERROR:     return "error";
    }
    return "?";
}
