# NQL Technical Documentation

Architecture, internals, and extension guide for the Number Query Language engine.

---

## File Structure

| File | Lines | Role |
|------|-------|------|
| `nql_types.c` | ~570 | Token enum, AST node tagged union, NqlValue, NqlVarStore, NqlUserFuncStore, NqlResult |
| `nql_lexer.c` | ~300 | Hand-written single-pass tokeniser, 46 keywords, case-insensitive |
| `nql_parser.c` | ~730 | Recursive-descent parser producing AST from token stream |
| `nql_funcs.c` | ~910 | 67 built-in functions, 17 number types, registry initialisation |
| `nql_eval.c` | ~980 | Expression evaluator, SELECT executor, set ops, WITH RECURSIVE |
| `nql_entry.c` | ~210 | Entry point (`nql_execute`), multi-statement splitting, help text |

All files are `#include`d into the amalgamation (`CompressedPrimalityStateStreamViewer.c`) — no separate compilation units.

---

## Execution Pipeline

```
Input string
    │
    ▼
┌─────────┐
│  Lexer   │  nql_lex() → NqlTokenList (flat array of NqlToken)
└────┬─────┘
     ▼
┌─────────┐
│ Parser   │  nql_parse() → NqlAstNode* (tree)
└────┬─────┘
     ▼
┌──────────┐
│ Executor  │  nql_execute_ex() dispatches by root node type:
│           │    SELECT  → nql_exec_select()
│           │    LET     → evaluate + store in NqlVarStore
│           │    CREATE  → store AST in NqlUserFuncStore
│           │    WITH    → nql_exec_with_recursive()
│           │    SET_OP  → nql_exec_set_op()
│           │    expr    → nql_eval_expr() + print
└────┬─────┘
     ▼
┌──────────┐
│ Formatter │  nql_print_result() — aligned columnar output
└──────────┘
```

## Memory Model

- **NqlAstPool**: Heap-allocated arena (512 nodes). Freed after query unless transferred to a user-defined function.
- **NqlResult**: Heap-allocated (10000 rows × 32 cols). Each NqlValue is 264 bytes (tagged union with 256-byte string buffer).
- **NqlVarStore / NqlUserFuncStore**: Stack-allocated in APP session, persists across queries.
- **Window buffers / order keys**: Heap-allocated per-query, freed on completion.

## Adding a New Built-in Function

1. Write the C implementation in `nql_funcs.c` with signature:
   ```c
   static NqlValue nql_fn_myname(const NqlValue* args, uint32_t argc, void* db_ctx);
   ```
2. Register it in `nql_init_registry()`:
   ```c
   nql_register_func("MY_FUNC", nql_fn_myname, 1, 1, "Description");
   ```
3. Rebuild. The function is immediately available in NQL queries.

## Adding a New Number Type

1. Write `test(n, ctx)` and `next(n, ctx)` functions in `nql_funcs.c`.
2. Register in `nql_init_registry()`:
   ```c
   nql_register_type("MY_TYPE", "n", my_test, my_next, false, false);
   ```
3. The type is immediately available in `FROM MY_TYPE`.

## AST Node Types

| Node | Fields | Description |
|------|--------|-------------|
| LITERAL | NqlValue | Integer, float, bool, string, null |
| IDENT | name[64] | Variable or column reference |
| BINOP | op, left, right | +, -, *, /, %, =, !=, <, >, AND, OR |
| UNARYOP | op, operand | -, NOT |
| FUNC_CALL | name, args[], arg_count | Built-in or user function |
| AGGREGATE | agg_type, operand, is_star | COUNT, SUM, MIN, MAX, AVG |
| WINDOW | win_type, operand, offset | LAG, LEAD |
| BETWEEN | expr, lo, hi | expr BETWEEN lo AND hi |
| IN_LIST | expr, items[], item_count | expr IN (v1, v2, ...) |
| ALIAS | expr, alias | expr AS name |
| IF | cond, then_expr, else_expr | IF(cond, then, else) |
| CASE | when_conds[], when_exprs[], else_expr | CASE WHEN...END |
| SELECT | columns, from_type, where, order, limit | Full SELECT |
| SET_OP | op, left, right | UNION/INTERSECT/EXCEPT |
| LET | name, value | LET x = expr |
| CREATE_FUNC | name, params, body | CREATE FUNCTION |
| WITH_RECURSIVE | cte_name, base, recursive, outer | WITH RECURSIVE |
| EXISTS | subquery, negated | EXISTS(SELECT...) |
| STAR | — | SELECT * wildcard |

## Range Extraction

The executor extracts scan ranges from WHERE clauses to avoid scanning all integers:
- `BETWEEN lo AND hi` → direct range
- `p >= X AND p <= Y` → extracted from AND children
- `p <= X` → range 1..X
- `p = X` → point query
- Default: 1..1000000 (capped at 100M for safety)

## User-Defined Functions

- Body stored as AST (pointer into a persistent NqlAstPool)
- Pool ownership transferred from query to NqlUserFuncStore on CREATE
- At call site: parameters bound as temporary variables, body evaluated, variables restored
- Max 8 parameters, max 64 user functions per session
