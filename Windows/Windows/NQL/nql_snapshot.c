/**
 * nql_snapshot.c - NQL Snapshot and Restore system.
 * Part of the CPSS Viewer amalgamation.
 *
 * Provides state capture and rollback for the NQL runtime, enabling
 * speculative execution with deep copy semantics and alias preservation.
 */

/* ======================================================================
 * COPY MEMO TABLE -- for alias-preserving deep copy
 *
 * When two variables point to the same heap object, a naive deep copy
 * creates two independent copies, breaking alias identity. The memo table
 * tracks old_ptr -> new_ptr mappings to ensure shared objects remain
 * shared after copying.
 * ====================================================================== */

#define NQL_COPY_MEMO_SIZE 128

typedef struct {
    void* old_ptrs[NQL_COPY_MEMO_SIZE];
    void* new_ptrs[NQL_COPY_MEMO_SIZE];
    uint32_t count;
} NqlCopyMemo;

static void nql_copy_memo_init(NqlCopyMemo* memo) {
    memo->count = 0u;
}

/** Check if pointer is already in memo. If yes, return the new pointer. */
static void* nql_copy_memo_lookup(NqlCopyMemo* memo, void* old_ptr) {
    if (!memo || !old_ptr) return NULL;
    for (uint32_t i = 0u; i < memo->count; ++i) {
        if (memo->old_ptrs[i] == old_ptr) {
            return memo->new_ptrs[i];
        }
    }
    return NULL;
}

/** Record a mapping from old pointer to new pointer. */
static bool nql_copy_memo_record(NqlCopyMemo* memo, void* old_ptr, void* new_ptr) {
    if (!memo || !old_ptr || !new_ptr) return false;
    if (memo->count >= NQL_COPY_MEMO_SIZE) return false;
    memo->old_ptrs[memo->count] = old_ptr;
    memo->new_ptrs[memo->count] = new_ptr;
    ++memo->count;
    return true;
}

/* ======================================================================
 * TYPE SAFETY CLASSIFICATION
 * ====================================================================== */

/** Check if a type is safe to snapshot (can be deep-copied). */
static bool nql_type_is_snapshot_safe(NqlValType type) {
    switch (type) {
        /* Scalar types - always safe */
        case NQL_VAL_NULL:
        case NQL_VAL_INT:
        case NQL_VAL_FLOAT:
        case NQL_VAL_BOOL:
        case NQL_VAL_STRING:
        case NQL_VAL_U128:
        case NQL_VAL_BIGNUM:
        /* Heap types that are NQL-managed and have copy functions */
        case NQL_VAL_ARRAY:
        case NQL_VAL_INSTANCE:
        case NQL_VAL_COMPLEX:
        case NQL_VAL_MATRIX:
        case NQL_VAL_SPARSE_MATRIX:
        case NQL_VAL_VECTOR:
        case NQL_VAL_RATIONAL:
        case NQL_VAL_INTERVAL:
        case NQL_VAL_GRAPH:
        case NQL_VAL_FIELD:
        case NQL_VAL_DISTRIBUTION:
        case NQL_VAL_SERIES:
            return true;

        /* Unsafe types - complex or opaque pointers */
        case NQL_VAL_SYMBOLIC:
        case NQL_VAL_POLY:
        case NQL_VAL_GROUP:
        case NQL_VAL_QS_FACTOR_BASE:
        case NQL_VAL_QS_SIEVE:
        case NQL_VAL_QS_TRIAL_RESULT:
        case NQL_VAL_QS_RELATION_SET:
        case NQL_VAL_QS_GF2_MATRIX:
        case NQL_VAL_QS_DEPENDENCY:
            return false;
    }
    return false;
}

/* ======================================================================
 * DEEP COPY FUNCTIONS FOR HEAP TYPES
 * ====================================================================== */

/* Forward declarations from other files */
static NqlArray* nql_array_copy(const NqlArray* src);
static NqlInstance* nql_instance_copy(const NqlInstance* src);

/** Deep copy a single NqlValue with memo table for alias preservation. */
static NqlValue nql_value_deep_copy(const NqlValue* src, NqlCopyMemo* memo) {
    /* Scalar types - direct value copy */
    if (src->type != NQL_VAL_ARRAY &&
        src->type != NQL_VAL_INSTANCE &&
        src->type != NQL_VAL_COMPLEX &&
        src->type != NQL_VAL_MATRIX &&
        src->type != NQL_VAL_SPARSE_MATRIX &&
        src->type != NQL_VAL_VECTOR &&
        src->type != NQL_VAL_RATIONAL &&
        src->type != NQL_VAL_INTERVAL &&
        src->type != NQL_VAL_GRAPH &&
        src->type != NQL_VAL_FIELD &&
        src->type != NQL_VAL_DISTRIBUTION &&
        src->type != NQL_VAL_SERIES) {
        return *src;
    }

    /* Check if this pointer is already in memo (alias case) */
    void* existing = NULL;
    switch (src->type) {
        case NQL_VAL_ARRAY:       existing = nql_copy_memo_lookup(memo, src->v.array); break;
        case NQL_VAL_INSTANCE:    existing = nql_copy_memo_lookup(memo, src->v.instance); break;
        case NQL_VAL_COMPLEX:     existing = nql_copy_memo_lookup(memo, src->v.complex); break;
        case NQL_VAL_MATRIX:      existing = nql_copy_memo_lookup(memo, src->v.matrix); break;
        case NQL_VAL_SPARSE_MATRIX: existing = nql_copy_memo_lookup(memo, src->v.sparse_matrix); break;
        case NQL_VAL_VECTOR:      existing = nql_copy_memo_lookup(memo, src->v.vector); break;
        case NQL_VAL_RATIONAL:    existing = nql_copy_memo_lookup(memo, src->v.rational); break;
        case NQL_VAL_INTERVAL:    existing = nql_copy_memo_lookup(memo, src->v.interval); break;
        case NQL_VAL_GRAPH:       existing = nql_copy_memo_lookup(memo, src->v.graph); break;
        case NQL_VAL_FIELD:       existing = nql_copy_memo_lookup(memo, src->v.field); break;
        case NQL_VAL_DISTRIBUTION: existing = nql_copy_memo_lookup(memo, src->v.distribution); break;
        case NQL_VAL_SERIES:      existing = nql_copy_memo_lookup(memo, src->v.series); break;
        default: break;
    }

    if (existing) {
        /* Reuse the already-copied pointer to preserve alias identity */
        NqlValue r;
        memset(&r, 0, sizeof(r));
        r.type = src->type;
        switch (src->type) {
            case NQL_VAL_ARRAY:       r.v.array = existing; break;
            case NQL_VAL_INSTANCE:    r.v.instance = existing; break;
            case NQL_VAL_COMPLEX:     r.v.complex = existing; break;
            case NQL_VAL_MATRIX:      r.v.matrix = existing; break;
            case NQL_VAL_SPARSE_MATRIX: r.v.sparse_matrix = existing; break;
            case NQL_VAL_VECTOR:      r.v.vector = existing; break;
            case NQL_VAL_RATIONAL:    r.v.rational = existing; break;
            case NQL_VAL_INTERVAL:    r.v.interval = existing; break;
            case NQL_VAL_GRAPH:       r.v.graph = existing; break;
            case NQL_VAL_FIELD:       r.v.field = existing; break;
            case NQL_VAL_DISTRIBUTION: r.v.distribution = existing; break;
            case NQL_VAL_SERIES:      r.v.series = existing; break;
            default: break;
        }
        return r;
    }

    /* Not in memo - need to deep copy */
    NqlValue result;
    memset(&result, 0, sizeof(result));
    result.type = src->type;

    switch (src->type) {
        case NQL_VAL_ARRAY: {
            NqlArray* copy = nql_array_copy(src->v.array);
            result.v.array = copy;
            if (copy) {
                nql_copy_memo_record(memo, src->v.array, copy);
                /* Deep-copy elements (instances/arrays inside this array) */
                for (uint32_t i = 0u; i < copy->length; ++i) {
                    copy->items[i] = nql_value_deep_copy(&src->v.array->items[i], memo);
                }
            }
            break;
        }
        case NQL_VAL_INSTANCE: {
            NqlInstance* copy = nql_instance_copy(src->v.instance);
            result.v.instance = copy;
            if (copy) nql_copy_memo_record(memo, src->v.instance, copy);
            break;
        }
        /* TODO: Add copy functions for other heap types as needed */
        default:
            /* For types without copy functions, return NULL to be safe */
            result.type = NQL_VAL_NULL;
            break;
    }

    return result;
}

/* ======================================================================
 * SNAPSHOT STACK
 * ====================================================================== */

#define NQL_MAX_SNAPSHOTS 16
#define NQL_MAX_SNAPSHOT_LABEL_LEN 64

/** A single snapshot captures the entire variable store at a point in time. */
typedef struct {
    char label[NQL_MAX_SNAPSHOT_LABEL_LEN];
    NqlVarStore vars_copy;      /* deep-copied variable store */
    NqlCopyMemo memo;           /* memo table used during copy */
    bool owns_heap;             /* true if snapshot owns its heap objects */
    bool active;
} NqlSnapshot;

/** Stack of snapshots, function-scoped. */
typedef struct {
    NqlSnapshot snaps[NQL_MAX_SNAPSHOTS];
    uint32_t count;
} NqlSnapshotStack;

static void nql_snapshot_stack_init(NqlSnapshotStack* stack) {
    stack->count = 0u;
}

/** Find snapshot index by label. Returns -1 if not found. */
static int32_t nql_snapshot_find(NqlSnapshotStack* stack, const char* label) {
    for (uint32_t i = 0u; i < stack->count; ++i) {
        if (stack->snaps[i].active && _stricmp(stack->snaps[i].label, label) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/** Take a snapshot of the current variable store. Returns false on failure. */
static bool nql_snapshot_take(NqlSnapshotStack* stack, const char* label, NqlVarStore* current_vars) {
    if (stack->count >= NQL_MAX_SNAPSHOTS) {
        printf("Error: Maximum number of snapshots (16) exceeded\n");
        return false;
    }

    /* Check for duplicate label */
    if (nql_snapshot_find(stack, label) >= 0) {
        printf("Error: Snapshot label '%s' already exists\n", label);
        return false;
    }

    NqlSnapshot* snap = &stack->snaps[stack->count++];
    memset(snap, 0, sizeof(*snap));
    strncpy(snap->label, label, NQL_MAX_SNAPSHOT_LABEL_LEN - 1);
    snap->label[NQL_MAX_SNAPSHOT_LABEL_LEN - 1] = '\0';
    snap->active = true;
    snap->owns_heap = true;  /* Snapshot owns its heap objects initially */

    /* Initialize memo table for this snapshot */
    nql_copy_memo_init(&snap->memo);

    /* Deep copy the variable store */
    nql_vars_init(&snap->vars_copy);
    for (uint32_t i = 0u; i < current_vars->count; ++i) {
        NqlVarEntry* entry = &current_vars->entries[i];
        if (!entry->used) continue;

        /* Check type safety */
        if (!nql_type_is_snapshot_safe(entry->value.type)) {
            printf("Warning: Variable '%s' has type that cannot be snapshotted, storing as NULL\n", entry->name);
            nql_vars_set(&snap->vars_copy, entry->name, nql_val_null());
            continue;
        }

        /* Deep copy the value */
        NqlValue copied = nql_value_deep_copy(&entry->value, &snap->memo);
        nql_vars_set(&snap->vars_copy, entry->name, copied);
    }

    return true;
}

/** Restore variable store from a snapshot. Returns false if label not found. */
static bool nql_snapshot_restore(NqlSnapshotStack* stack, const char* label, NqlVarStore* current_vars) {
    int32_t idx = nql_snapshot_find(stack, label);
    if (idx < 0) {
        printf("Error: Snapshot '%s' not found\n", label);
        return false;
    }

    NqlSnapshot* snap = &stack->snaps[(uint32_t)idx];

    /* Free current heap objects before replacing */
    nql_vars_free(current_vars);
    nql_vars_init(current_vars);
    for (uint32_t i = 0u; i < snap->vars_copy.count; ++i) {
        NqlVarEntry* entry = &snap->vars_copy.entries[i];
        if (!entry->used) continue;

        /* Deep copy back with proper memo to preserve aliasing */
        NqlCopyMemo restore_memo;
        nql_copy_memo_init(&restore_memo);
        NqlValue copied = nql_value_deep_copy(&entry->value, &restore_memo);
        nql_vars_set(current_vars, entry->name, copied);
    }
    
    /* Transfer heap ownership to current vars */
    snap->owns_heap = false;

    return true;
}

/** Free all snapshots and their heap allocations. */
static void nql_snapshot_stack_free(NqlSnapshotStack* stack) {
    if (!stack) return;
    for (uint32_t i = 0u; i < stack->count; ++i) {
        NqlSnapshot* snap = &stack->snaps[i];
        if (!snap->active) continue;

        /* Free heap objects only if snapshot still owns them */
        if (snap->owns_heap) {
            nql_vars_free(&snap->vars_copy);
        }

        snap->active = false;
    }
    stack->count = 0u;
}
