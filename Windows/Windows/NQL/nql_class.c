/**
 * nql_class.c - NQL Class and Instance system.
 * Part of the CPSS Viewer amalgamation.
 *
 * Provides native class declarations, instance creation, field access,
 * and field mutation. Instances are heap-allocated with dynamically-sized
 * field arrays matching the class definition.
 */

/* ======================================================================
 * CLASS REGISTRY
 * ====================================================================== */

#define NQL_MAX_CLASSES 64
#define NQL_MAX_CLASS_FIELDS 32

typedef struct {
    char     name[64];
    struct { char name[64]; NqlValType type; } fields[NQL_MAX_CLASS_FIELDS];
    uint32_t field_count;
    bool     used;
} NqlClassDef;

static NqlClassDef g_nql_classes[NQL_MAX_CLASSES];
static uint32_t    g_nql_class_count = 0u;

/** Look up a class by name. Returns NULL if not found. */
static const NqlClassDef* nql_class_lookup(const char* name) {
    for (uint32_t i = 0u; i < g_nql_class_count; ++i) {
        if (g_nql_classes[i].used && _stricmp(g_nql_classes[i].name, name) == 0)
            return &g_nql_classes[i];
    }
    return NULL;
}

/** Look up a class by index. */
static const NqlClassDef* nql_class_by_id(uint32_t id) {
    if (id < g_nql_class_count && g_nql_classes[id].used) return &g_nql_classes[id];
    return NULL;
}

/** Register a class. Returns the class index, or UINT32_MAX on failure. */
static uint32_t nql_class_register(const char* name, uint32_t field_count) {
    if (g_nql_class_count >= NQL_MAX_CLASSES) return UINT32_MAX;
    /* Check for duplicate */
    for (uint32_t i = 0u; i < g_nql_class_count; ++i) {
        if (g_nql_classes[i].used && _stricmp(g_nql_classes[i].name, name) == 0)
            return UINT32_MAX; /* duplicate class name */
    }
    uint32_t id = g_nql_class_count++;
    NqlClassDef* c = &g_nql_classes[id];
    memset(c, 0, sizeof(*c));
    c->used = true;
    strncpy(c->name, name, sizeof(c->name) - 1);
    c->field_count = field_count;
    return id;
}

/** Find field index by name in a class. Returns -1 if not found. */
static int32_t nql_class_field_index(const NqlClassDef* cls, const char* field_name) {
    for (uint32_t i = 0u; i < cls->field_count; ++i) {
        if (_stricmp(cls->fields[i].name, field_name) == 0) return (int32_t)i;
    }
    return -1;
}

/** Return default value for a given type. */
static NqlValue nql_default_for_type(NqlValType type) {
    switch (type) {
        case NQL_VAL_INT:    return nql_val_int(0);
        case NQL_VAL_FLOAT:  return nql_val_float(0.0);
        case NQL_VAL_BOOL:   return nql_val_bool(false);
        case NQL_VAL_STRING: return nql_val_str("");
        default:             return nql_val_null();
    }
}

/* ======================================================================
 * INSTANCE ALLOCATION AND MANAGEMENT
 * ====================================================================== */

struct NqlInstance_s {
    uint32_t  class_id;
    uint32_t  field_count;
    uint32_t  ref_count;     /* Reference counting for safe freeing */
    bool      freed;         /* Flag to prevent double-free */
    NqlValue* fields;       /* heap-allocated, exact-size */
};

/** Allocate a new instance of the given class with default field values. */
static NqlInstance* nql_instance_alloc(uint32_t class_id) {
    const NqlClassDef* cls = nql_class_by_id(class_id);
    if (!cls) return NULL;
    NqlInstance* inst = (NqlInstance*)malloc(sizeof(NqlInstance));
    if (!inst) return NULL;
    inst->class_id = class_id;
    inst->field_count = cls->field_count;
    inst->ref_count = 1; /* Start with 1 reference */
    inst->freed = false; /* Not freed yet */
    if (cls->field_count > 0u) {
        inst->fields = (NqlValue*)calloc(cls->field_count, sizeof(NqlValue));
        if (!inst->fields) { free(inst); return NULL; }
        for (uint32_t i = 0u; i < cls->field_count; ++i)
            inst->fields[i] = nql_default_for_type(cls->fields[i].type);
    } else {
        inst->fields = NULL;
    }
    return inst;
}

/** Free an instance and its field array using reference counting. */
static void nql_instance_free(NqlInstance* inst) {
    if (!inst) {
        return;
    }
    
    if (inst->freed) {
        return;
    }
    
    /* Decrease reference count */
    if (inst->ref_count > 0) {
        inst->ref_count--;
    }
    
    /* Only actually free if reference count reaches 0 */
    if (inst->ref_count > 0) {
        return;
    }
    
    /* Mark as freed BEFORE actually freeing to prevent race conditions */
    inst->freed = true;
    
    /* Safety check: don't free NULL fields pointer */
    if (inst->fields) {
        free(inst->fields);
        inst->fields = NULL;
    }
    
    /* Don't clear the freed flag - keep it to prevent double-free */
    free(inst);
}

/** Deep copy an instance (allocates new instance + new field array). */
static NqlInstance* nql_instance_copy(const NqlInstance* src) {
    if (!src) return NULL;
    NqlInstance* dst = (NqlInstance*)malloc(sizeof(NqlInstance));
    if (!dst) return NULL;
    dst->class_id = src->class_id;
    dst->field_count = src->field_count;
    dst->ref_count = 1; /* New copy starts with 1 reference */
    dst->freed = false; /* Not freed yet */
    if (src->field_count > 0u && src->fields) {
        dst->fields = (NqlValue*)malloc(src->field_count * sizeof(NqlValue));
        if (!dst->fields) { free(dst); return NULL; }
        memcpy(dst->fields, src->fields, src->field_count * sizeof(NqlValue));
    } else {
        dst->fields = NULL;
    }
    return dst;
}

/** Construct an NqlValue holding an instance pointer. */
static NqlValue nql_val_instance(NqlInstance* inst) {
    NqlValue r;
    memset(&r, 0, sizeof(r));
    r.type = NQL_VAL_INSTANCE;
    r.v.instance = inst;
    return r;
}

/** Get a field value from an instance by field name. Returns NULL if invalid. */
static NqlValue nql_instance_get_field(const NqlInstance* inst, const char* field_name) {
    if (!inst || !inst->fields) return nql_val_null();
    const NqlClassDef* cls = nql_class_by_id(inst->class_id);
    if (!cls) return nql_val_null();
    int32_t idx = nql_class_field_index(cls, field_name);
    if (idx < 0 || (uint32_t)idx >= inst->field_count) return nql_val_null();
    return inst->fields[idx];
}

/** Set a field value on an instance by field name. Returns false if invalid. */
static bool nql_instance_set_field(NqlInstance* inst, const char* field_name, NqlValue val) {
    if (!inst || !inst->fields) return false;
    const NqlClassDef* cls = nql_class_by_id(inst->class_id);
    if (!cls) return false;
    int32_t idx = nql_class_field_index(cls, field_name);
    if (idx < 0 || (uint32_t)idx >= inst->field_count) return false;
    inst->fields[idx] = val;
    return true;
}

/** Map NQL type name string to NqlValType. */
static NqlValType nql_type_from_name(const char* name) {
    if (_stricmp(name, "INT") == 0)    return NQL_VAL_INT;
    if (_stricmp(name, "FLOAT") == 0)  return NQL_VAL_FLOAT;
    if (_stricmp(name, "BOOL") == 0)   return NQL_VAL_BOOL;
    if (_stricmp(name, "STRING") == 0) return NQL_VAL_STRING;
    if (_stricmp(name, "ARRAY") == 0)  return NQL_VAL_ARRAY;
    return NQL_VAL_NULL; /* unknown type defaults to NULL */
}
