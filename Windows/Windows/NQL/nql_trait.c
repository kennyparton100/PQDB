/**
 * nql_trait.c - NQL Trait and Mapped Trait system.
 * Part of the CPSS Viewer amalgamation.
 *
 * Provides trait declarations, trait-to-class mappings with remap tables,
 * and trait function dispatch with O(1) field access via index remapping.
 */

/* ======================================================================
 * TRAIT REGISTRY
 * ====================================================================== */

#define NQL_MAX_TRAITS 64
#define NQL_MAX_TRAIT_SLOTS 32
#define NQL_MAX_TRAIT_FUNCS 8
#define NQL_TRAIT_FUNC_SRC_MAX 4096

typedef struct {
    char name[64];
    struct { 
        char name[64]; 
        NqlValType type; 
        bool writable; 
    } slots[NQL_MAX_TRAIT_SLOTS];
    uint32_t slot_count;
    struct {
        char name[64];       /* original function name (e.g. "move") */
        char* source;        /* heap-allocated full CREATE FUNCTION ... END FUNCTION source */
    } funcs[NQL_MAX_TRAIT_FUNCS];
    uint32_t func_count;
    bool used;
} NqlTraitDef;

static NqlTraitDef g_nql_traits[NQL_MAX_TRAITS];
static uint32_t    g_nql_trait_count = 0u;

/** Look up a trait by name. Returns NULL if not found. */
static const NqlTraitDef* nql_trait_lookup(const char* name) {
    for (uint32_t i = 0u; i < g_nql_trait_count; ++i) {
        if (g_nql_traits[i].used && _stricmp(g_nql_traits[i].name, name) == 0)
            return &g_nql_traits[i];
    }
    return NULL;
}

/** Look up a trait by index. */
static const NqlTraitDef* nql_trait_by_id(uint32_t id) {
    if (id < g_nql_trait_count && g_nql_traits[id].used) return &g_nql_traits[id];
    return NULL;
}

/** Register a trait. Returns the trait index, or UINT32_MAX on failure. */
static uint32_t nql_trait_register(const char* name, uint32_t slot_count) {
    if (g_nql_trait_count >= NQL_MAX_TRAITS) return UINT32_MAX;
    /* Check for duplicate */
    for (uint32_t i = 0u; i < g_nql_trait_count; ++i) {
        if (g_nql_traits[i].used && _stricmp(g_nql_traits[i].name, name) == 0)
            return UINT32_MAX; /* duplicate trait name */
    }
    if (slot_count > NQL_MAX_TRAIT_SLOTS) return UINT32_MAX;
    uint32_t id = g_nql_trait_count++;
    NqlTraitDef* t = &g_nql_traits[id];
    memset(t, 0, sizeof(*t));
    t->used = true;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->slot_count = slot_count;
    return id;
}

/** Find slot index by name in a trait. Returns -1 if not found. */
static int32_t nql_trait_slot_index(const NqlTraitDef* trait, const char* slot_name) {
    for (uint32_t i = 0u; i < trait->slot_count; ++i) {
        if (_stricmp(trait->slots[i].name, slot_name) == 0) return (int32_t)i;
    }
    return -1;
}

/* ======================================================================
 * TRAIT MAPPING REGISTRY
 * ====================================================================== */

#define NQL_MAX_TRAIT_MAPPINGS 64

typedef struct {
    char map_name[64];          /* e.g. "PlayerPhysics" */
    uint32_t trait_id;          /* index into trait registry */
    uint32_t class_id;          /* index into class registry */
    uint32_t field_remap[NQL_MAX_TRAIT_SLOTS]; /* trait_slot_index -> class_field_index */
    bool used;
} NqlTraitMapping;

static NqlTraitMapping g_nql_trait_mappings[NQL_MAX_TRAIT_MAPPINGS];
static uint32_t        g_nql_trait_mapping_count = 0u;

/** Look up a trait mapping by name. Returns NULL if not found. */
static const NqlTraitMapping* nql_trait_mapping_lookup(const char* name) {
    for (uint32_t i = 0u; i < g_nql_trait_mapping_count; ++i) {
        if (g_nql_trait_mappings[i].used && _stricmp(g_nql_trait_mappings[i].map_name, name) == 0)
            return &g_nql_trait_mappings[i];
    }
    return NULL;
}

/** Register a trait mapping. Returns the mapping index, or UINT32_MAX on failure. */
static uint32_t nql_trait_mapping_register(const char* map_name, uint32_t trait_id, uint32_t class_id) {
    if (g_nql_trait_mapping_count >= NQL_MAX_TRAIT_MAPPINGS) return UINT32_MAX;
    /* Check for duplicate mapping name */
    for (uint32_t i = 0u; i < g_nql_trait_mapping_count; ++i) {
        if (g_nql_trait_mappings[i].used && _stricmp(g_nql_trait_mappings[i].map_name, map_name) == 0)
            return UINT32_MAX; /* duplicate mapping name */
    }
    uint32_t id = g_nql_trait_mapping_count++;
    NqlTraitMapping* m = &g_nql_trait_mappings[id];
    memset(m, 0, sizeof(*m));
    m->used = true;
    strncpy(m->map_name, map_name, sizeof(m->map_name) - 1);
    m->trait_id = trait_id;
    m->class_id = class_id;
    /* Initialize remap table to invalid indices */
    for (uint32_t i = 0u; i < NQL_MAX_TRAIT_SLOTS; ++i) {
        m->field_remap[i] = UINT32_MAX;
    }
    return id;
}

/** Set a field remap entry. Returns false if invalid indices. */
static bool nql_trait_mapping_set_remap(uint32_t mapping_id, uint32_t slot_idx, uint32_t field_idx) {
    if (mapping_id >= g_nql_trait_mapping_count) return false;
    NqlTraitMapping* m = &g_nql_trait_mappings[mapping_id];
    if (!m->used) return false;
    if (slot_idx >= NQL_MAX_TRAIT_SLOTS) return false;
    m->field_remap[slot_idx] = field_idx;
    return true;
}

/** Get class field index for a trait slot. Returns UINT32_MAX if not mapped. */
static uint32_t nql_trait_mapping_get_field_idx(const NqlTraitMapping* mapping, uint32_t slot_idx) {
    if (!mapping || slot_idx >= NQL_MAX_TRAIT_SLOTS) return UINT32_MAX;
    return mapping->field_remap[slot_idx];
}

/* ======================================================================
 * TRAIT-AWARE FIELD ACCESS
 * ====================================================================== */

/**
 * Context for trait function execution.
 * Stores the current mapping and self instance for field remapping.
 */
typedef struct {
    const NqlTraitMapping* mapping;  /* Current trait mapping (NULL if not in trait func) */
    NqlInstance* self;               /* Self instance for trait function */
} NqlTraitContext;

/* Global trait context stack (max nesting depth 8) */
#define NQL_MAX_TRAIT_DEPTH 8
static NqlTraitContext g_nql_trait_ctx_stack[NQL_MAX_TRAIT_DEPTH];
static uint32_t        g_nql_trait_ctx_depth = 0u;

/** Push trait context for trait function execution. Returns false if stack full. */
static bool nql_trait_ctx_push(const NqlTraitMapping* mapping, NqlInstance* self) {
    if (g_nql_trait_ctx_depth >= NQL_MAX_TRAIT_DEPTH) return false;
    NqlTraitContext* ctx = &g_nql_trait_ctx_stack[g_nql_trait_ctx_depth++];
    ctx->mapping = mapping;
    ctx->self = self;
    return true;
}

/** Pop trait context after trait function execution. */
static void nql_trait_ctx_pop(void) {
    if (g_nql_trait_ctx_depth > 0u) --g_nql_trait_ctx_depth;
}

/** Get current trait context (NULL if not in trait function). */
static const NqlTraitContext* nql_trait_ctx_current(void) {
    if (g_nql_trait_ctx_depth == 0u) return NULL;
    return &g_nql_trait_ctx_stack[g_nql_trait_ctx_depth - 1u];
}

/** Get slot index that maps to a given class field. Returns -1 if not found. */
static int32_t nql_trait_mapping_slot_for_field(const NqlTraitMapping* mapping, const NqlTraitDef* trait, const char* field_name) {
    if (!mapping || !trait) return -1;
    /* Find the class field index for this field name */
    const NqlClassDef* cls = nql_class_by_id(mapping->class_id);
    if (!cls) return -1;
    int32_t field_idx = nql_class_field_index(cls, field_name);
    if (field_idx < 0) return -1;
    /* Find which slot maps to this field index */
    for (uint32_t i = 0u; i < trait->slot_count; ++i) {
        if (mapping->field_remap[i] == (uint32_t)field_idx) {
            return (int32_t)i;
        }
    }
    return -1;
}

/** 
 * Access a field with trait remapping support.
 * If inside a trait function, field_name is a SLOT name (e.g. "value").
 * We look up the slot index in the trait, then remap to the class field index.
 */
static NqlValue nql_trait_get_field(NqlInstance* inst, const char* field_name) {
    const NqlTraitContext* ctx = nql_trait_ctx_current();
    if (ctx && ctx->mapping && ctx->self == inst) {
        const NqlTraitDef* trait = nql_trait_by_id(ctx->mapping->trait_id);
        if (trait) {
            /* field_name is a slot name - look it up in the trait */
            int32_t slot_idx = nql_trait_slot_index(trait, field_name);
            if (slot_idx >= 0) {
                uint32_t field_idx = nql_trait_mapping_get_field_idx(ctx->mapping, (uint32_t)slot_idx);
                if (field_idx != UINT32_MAX && field_idx < inst->field_count) {
                    return inst->fields[field_idx];
                }
            }
        }
        /* Fall through to direct access if slot not found (could be a real field) */
    }
    return nql_instance_get_field(inst, field_name);
}

/**
 * Set a field with trait remapping support.
 * If inside a trait function, field_name is a SLOT name.
 */
static bool nql_trait_set_field(NqlInstance* inst, const char* field_name, NqlValue val) {
    const NqlTraitContext* ctx = nql_trait_ctx_current();
    if (ctx && ctx->mapping && ctx->self == inst) {
        const NqlTraitDef* trait = nql_trait_by_id(ctx->mapping->trait_id);
        if (trait) {
            /* field_name is a slot name - look it up in the trait */
            int32_t slot_idx = nql_trait_slot_index(trait, field_name);
            if (slot_idx >= 0) {
                uint32_t field_idx = nql_trait_mapping_get_field_idx(ctx->mapping, (uint32_t)slot_idx);
                if (field_idx != UINT32_MAX && field_idx < inst->field_count) {
                    inst->fields[field_idx] = val;
                    return true;
                }
            }
        }
        /* Fall through to direct access if slot not found */
    }
    return nql_instance_set_field(inst, field_name, val);
}

/* ======================================================================
 * MANGLED NAME HELPERS
 * ====================================================================== */

/** Build mangled trait function name: __trait_<TraitName>_<func_name> */
static void nql_mangle_trait_func(char* buf, size_t bufsz, const char* trait_name, const char* func_name) {
    snprintf(buf, bufsz, "__trait_%s_%s", trait_name, func_name);
}

/** Extract trait name and function name from mangled name. Returns false if not a trait func. */
static bool nql_demangle_trait_func(const char* mangled, char* trait_buf, size_t trait_sz, 
                                     char* func_buf, size_t func_sz) {
    if (strncmp(mangled, "__trait_", 8) != 0) return false;
    const char* p = mangled + 8;
    /* Find next underscore (separator between trait and func name) */
    const char* sep = strchr(p, '_');
    if (!sep) return false;
    size_t trait_len = (size_t)(sep - p);
    if (trait_len >= trait_sz) trait_len = trait_sz - 1;
    memcpy(trait_buf, p, trait_len);
    trait_buf[trait_len] = '\0';
    strncpy(func_buf, sep + 1, func_sz - 1);
    func_buf[func_sz - 1] = '\0';
    return true;
}
