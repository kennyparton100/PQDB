/**
 * nql_assume.c - NQL ASSUME/OTHERWISE speculative execution.
 * Part of the CPSS Viewer amalgamation.
 *
 * Provides speculative execution with automatic snapshot/restore.
 * ASSUME blocks create snapshots and can fail with ASSUMPTION_FAILED,
 * which rolls back state and jumps to the next OTHERWISE branch.
 */

/* Function declarations for external linkage */
NqlValue nql_fn_assumption_failed(const NqlValue* args, uint32_t argc, void* ctx);
NqlValue nql_fn_active_assumptions(const NqlValue* args, uint32_t argc, void* ctx);

/* ======================================================================
 * ASSUMPTION CONTEXT AND STACK
 * ====================================================================== */

#define NQL_MAX_ASSUMPTION_DEPTH 8
#define NQL_MAX_ASSUMPTION_DESC 256

/** A single assumption context with auto-snapshot */
typedef struct {
    char description[NQL_MAX_ASSUMPTION_DESC];  /* the assumption condition text */
    char snapshot_label[64];                     /* snapshot label for restoration */
    bool committed;                             /* true if block completed successfully */
} NqlAssumptionCtx;

/** Stack of active assumptions */
typedef struct {
    NqlAssumptionCtx stack[NQL_MAX_ASSUMPTION_DEPTH];
    uint32_t depth;
} NqlAssumptionStack;

/* Forward declarations */
static NqlSnapshotStack* g_assume_snapshot_stack;
static NqlAssumptionStack* g_active_assumption_stack = NULL;
static uint32_t g_assume_counter = 0u;

/** Initialize assumption stack */
void nql_assume_stack_init(NqlAssumptionStack* stack) {
    stack->depth = 0u;
}

/** Push a new assumption context without auto-snapshot */
bool nql_assume_push_simple(NqlAssumptionStack* stack, const char* description) {
    if (stack->depth >= NQL_MAX_ASSUMPTION_DEPTH) {
        printf("Error: Maximum assumption depth (%d) exceeded\n", NQL_MAX_ASSUMPTION_DEPTH);
        return false;
    }
    
    NqlAssumptionCtx* ctx = &stack->stack[stack->depth++];
    memset(ctx, 0, sizeof(*ctx));
    
    /* Copy description */
    if (description) {
        strncpy(ctx->description, description, sizeof(ctx->description) - 1);
        ctx->description[sizeof(ctx->description) - 1] = '\0';
    }
    
    ctx->committed = false;
    return true;
}

/** Push a new assumption context with auto-snapshot */
bool nql_assume_push(NqlAssumptionStack* stack, const char* description, 
                     NqlVarStore* current_vars, NqlSnapshotStack* snapshot_stack) {
    if (stack->depth >= NQL_MAX_ASSUMPTION_DEPTH) {
        printf("Error: Maximum assumption depth (%d) exceeded\n", NQL_MAX_ASSUMPTION_DEPTH);
        return false;
    }
    
    NqlAssumptionCtx* ctx = &stack->stack[stack->depth++];
    memset(ctx, 0, sizeof(*ctx));
    
    /* Copy description */
    if (description) {
        strncpy(ctx->description, description, sizeof(ctx->description) - 1);
        ctx->description[sizeof(ctx->description) - 1] = '\0';
    }
    
    /* Take auto-snapshot */
    char snapshot_label[64];
    snprintf(snapshot_label, sizeof(snapshot_label), "__assume_%u_%u", stack->depth, ++g_assume_counter);
    if (!nql_snapshot_take(snapshot_stack, snapshot_label, current_vars)) {
        printf("Error: Failed to create assumption snapshot\n");
        stack->depth--;
        return false;
    }
    
    /* Store snapshot label for later restoration */
    strncpy(ctx->snapshot_label, snapshot_label, sizeof(ctx->snapshot_label) - 1);
    ctx->snapshot_label[sizeof(ctx->snapshot_label) - 1] = '\0';
    
    ctx->committed = false;
    return true;
}

/** Pop and commit the current assumption (without snapshot) */
void nql_assume_pop_commit(NqlAssumptionStack* stack) {
    if (stack->depth == 0u) return;
    
    NqlAssumptionCtx* ctx = &stack->stack[stack->depth - 1];
    ctx->committed = true;
    
    stack->depth--;
}

/** Pop and commit the current assumption (with snapshot) */
void nql_assume_pop_commit_with_snapshot(NqlAssumptionStack* stack, NqlSnapshotStack* snapshot_stack) {
    if (stack->depth == 0u) return;
    
    NqlAssumptionCtx* ctx = &stack->stack[stack->depth - 1];
    ctx->committed = true;
    
    /* No need to free snapshot - it will be cleaned up with snapshot_stack */
    stack->depth--;
}

/** Pop and restore from the current assumption (failure case) */
void nql_assume_pop_restore(NqlAssumptionStack* stack, NqlVarStore* current_vars,
                           NqlSnapshotStack* snapshot_stack) {
    if (stack->depth == 0u) return;
    
    NqlAssumptionCtx* ctx = &stack->stack[stack->depth - 1];
    
    /* Restore from snapshot using stored label */
    nql_snapshot_restore(snapshot_stack, ctx->snapshot_label, current_vars);
    
    stack->depth--;
}

/** Get the current assumption depth */
uint32_t nql_assume_depth(NqlAssumptionStack* stack) {
    return stack->depth;
}

/** Get active assumptions as an array of strings */
NqlValue nql_active_assumptions(NqlAssumptionStack* stack) {
    NqlArray* arr = nql_array_alloc(stack->depth);
    if (!arr) return nql_val_null();
    
    for (uint32_t i = 0u; i < stack->depth; ++i) {
        NqlAssumptionCtx* ctx = &stack->stack[i];
        nql_array_push(arr, nql_val_str(ctx->description));
    }
    
    return nql_val_array(arr);
}

/* ======================================================================
 * BUILT-IN FUNCTIONS
 * ====================================================================== */

/** ASSUMPTION_FAILED(reason) - fail current assumption */
NqlValue nql_fn_assumption_failed(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    const char* reason = "Assumption failed";
    
    if (argc >= 1u && args[0].type == NQL_VAL_STRING) {
        reason = args[0].v.sval;
    }
    
    /* This will be handled by the evaluator - we just return a special value */
    NqlValue result = nql_val_null();
    /* Mark this as a special failure - evaluator will check for this */
    result.type = (NqlValType)0xFFFF;  /* Use invalid type as marker */
    return result;
}

/** ACTIVE_ASSUMPTIONS() - return array of active assumption descriptions */
NqlValue nql_fn_active_assumptions(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)args; (void)argc; (void)ctx;
    if (g_active_assumption_stack) {
        return nql_active_assumptions(g_active_assumption_stack);
    }
    return nql_val_array(nql_array_alloc(0));
}
