# Construction Registry Crash (P0)

**Severity:** P0 (Critical - Crash)
**Status:** Research Complete, Pending Implementation
**Assigned:** Engine Team
**Date Identified:** 2026-04-10

---

## Executive Summary

The `SdkConstructionArchetypeRegistry` suffers from a **use-after-free race condition** between concurrent reader threads and hash table rebuilds. When multiple worker threads call `construction_registry_find_match()` while the main thread or persistence system calls `construction_registry_release_id()` (which triggers `rebuild_hash()`), the hash table pointer can be freed mid-read, causing crashes with characteristic bogus pointer values like `0x1110111011139E6`.

---

## Root Cause Analysis

### The Race Condition

```
Thread A (Reader/Worker)          Thread B (Writer/Main)
--------------------------------  --------------------------------
1. Reads registry->hash_table      1. Calls release_id()
2. Dereferences for lookup         2. refcount hits 0
                                   3. Frees archetype
                                   4. Decrements active_count
                                   5. Increments revision
                                   6. Calls rebuild_hash()
                                   7. **FREE(registry->hash_table)**
                                   8. Allocates new hash_table
2. **CRASH** - use-after-free!
```

### Critical Code Locations

#### 1. The Unsafe Free (sdk_construction_cells.c:913-947)
```c
static int construction_registry_rebuild_hash(SdkConstructionArchetypeRegistry* registry)
{
    // ...
    free(registry->hash_table);        // <-- RACE: freed while readers active
    registry->hash_table = NULL;
    registry->hash_capacity = 0u;
    // ...
    hash_table = (SdkConstructionArchetypeId*)calloc(hash_capacity, sizeof(*hash_table));
    // ...
    registry->hash_table = hash_table;  // <-- RACE: pointer swap mid-iteration
    registry->hash_capacity = hash_capacity;
    return 1;
}
```

#### 2. The Unprotected Read (sdk_construction_cells.c:997-1024)
```c
static int construction_registry_find_match(const SdkConstructionArchetypeRegistry* registry,
                                            uint32_t hash,
                                            const uint16_t* palette,
                                            uint8_t palette_count,
                                            const uint8_t* voxel_palette,
                                            SdkConstructionArchetypeId* out_id)
{
    // ...
    mask = registry->hash_capacity - 1u;     // <-- RACE: reads capacity
    pos = hash & mask;
    for (uint32_t probe = 0u; probe < registry->hash_capacity; ++probe) {
        SdkConstructionArchetypeId id = registry->hash_table[pos];  // <-- RACE: reads table
        // ...
    }
}
```

#### 3. The Trigger Point (sdk_construction_cells.c:981-995)
```c
static void construction_registry_release_id(SdkConstructionArchetypeRegistry* registry,
                                           SdkConstructionArchetypeId id)
{
    SdkConstructionArchetype* archetype = construction_registry_get_mutable(registry, id);
    if (!archetype) return;
    if (InterlockedDecrement((volatile LONG*)&archetype->refcount) == 0) {
        uint32_t slot_index = (uint32_t)(id - 1u);
        free(archetype);
        registry->slots[slot_index] = NULL;
        if (registry->active_count > 0u) registry->active_count--;
        registry->revision++;
        construction_registry_rebuild_hash(registry);  // <-- TRIGGERS REBUILD
    }
}
```

---

## Affected Code Paths

### 1. Worker Threads (High Frequency, 16 Workers)
- **File:** `sdk_chunk_streamer.c` (all worker threads)
- **Path:** `worker_thread_func()` → `sdk_worldgen_generate_chunk_ctx()` → `sdk_construction_store_lookup_archetype()` → `construction_registry_find_match()`
- **Risk:** Multiple concurrent readers; any rebuild during worldgen causes crash

### 2. Main Thread (Medium Frequency)
- **Files:** 
  - `sdk_construction_store.c` - Construction placement operations
  - `sdk_persistence.c` - Registry deserialization
- **Path:** `sdk_construction_store_set_cell_payload()` → `resolve_workspace()` → `find_match()` OR `acquire_id()` → `rebuild_hash()`
- **Risk:** Writes can collide with worker reads

### 3. Persistence System (Low Frequency, High Impact)
- **File:** `sdk_persistence.c`
- **Path:** `decode_construction_registry()` → `acquire_id()` → `rebuild_hash()`
- **Risk:** Large batch operations increase rebuild frequency

---

## Data Structures

### Current Registry Structure (sdk_construction_cells.h:40-48)
```c
typedef struct SdkConstructionArchetypeRegistry {
    SdkConstructionArchetype** slots;       // Preallocated (65536)
    uint32_t slot_count;
    uint32_t slot_capacity;                 // 65536
    uint32_t active_count;
    SdkConstructionArchetypeId* hash_table; // Dynamically resized
    uint32_t hash_capacity;
    uint32_t revision;                      // Incremented on rebuild
    // SRWLOCK lock;                        // <-- NEEDS TO BE ADDED
} SdkConstructionArchetypeRegistry;
```

### Archetype Structure
```c
typedef struct SdkConstructionArchetype {
    SdkConstructionArchetypeId id;          // 1-based index into slots
    uint32_t hash;                          // Content hash for lookup
    uint32_t refcount;                      // Atomic decrement on release
    uint8_t palette_count;
    uint16_t palette[SDK_CONSTRUCTION_PALETTE_MAX_ENTRIES];
    uint8_t voxel_palette[SDK_CONSTRUCTION_PALETTE_MAX_ENTRIES];
} SdkConstructionArchetype;
```

---

## Proposed Fix

### Phase 1: Add Synchronization Primitive

#### 1.1 Add SRWLOCK to Registry Struct
```c
typedef struct SdkConstructionArchetypeRegistry {
    SdkConstructionArchetype** slots;
    uint32_t slot_count;
    uint32_t slot_capacity;
    uint32_t active_count;
    SdkConstructionArchetypeId* hash_table;
    uint32_t hash_capacity;
    uint32_t revision;
    SRWLOCK lock;        // <-- ADD THIS
} SdkConstructionArchetypeRegistry;
```

#### 1.2 Initialize in Registry Create (sdk_construction_cells.c:1086-1099)
```c
SdkConstructionArchetypeRegistry* sdk_construction_registry_create(void)
{
    SdkConstructionArchetypeRegistry* registry = (SdkConstructionArchetypeRegistry*)calloc(1, sizeof(*registry));
    if (!registry) return NULL;
    
    // ... existing initialization ...
    
    InitializeSRWLock(&registry->lock);  // <-- ADD THIS
    return registry;
}
```

#### 1.3 Cleanup in Registry Free (sdk_construction_cells.c:1113-1128)
```c
void sdk_construction_registry_free(SdkConstructionArchetypeRegistry* registry)
{
    if (!registry) return;
    
    // SRWLOCK doesn't need explicit cleanup (kernel object)
    // ... existing cleanup ...
}
```

### Phase 2: Add Locking to Critical Sections

#### 2.1 Shared Lock for find_match (sdk_construction_cells.c:997-1024)
```c
static int construction_registry_find_match(const SdkConstructionArchetypeRegistry* registry,
                                            uint32_t hash,
                                            const uint16_t* palette,
                                            uint8_t palette_count,
                                            const uint8_t* voxel_palette,
                                            SdkConstructionArchetypeId* out_id)
{
    uint32_t revision_before;
    
    if (out_id) *out_id = SDK_CONSTRUCTION_ARCHETYPE_INVALID_ID;
    if (!registry || !registry->hash_table || registry->hash_capacity == 0u) return 0;
    
    AcquireSRWLockShared(&registry->lock);  // <-- ADD THIS
    revision_before = registry->revision;
    
    // ... existing lookup code ...
    
    ReleaseSRWLockShared(&registry->lock);  // <-- ADD THIS
    
    // Verify no rebuild occurred during our read (optional: retry if stale)
    if (registry->revision != revision_before) {
        // Stale read - retry or handle appropriately
    }
    
    return result;
}
```

#### 2.2 Shared Lock for get_mutable/get_const (sdk_construction_cells.c:949-975)
```c
static SdkConstructionArchetype* construction_registry_get_mutable(
    SdkConstructionArchetypeRegistry* registry,
    SdkConstructionArchetypeId id)
{
    AcquireSRWLockShared(&registry->lock);  // <-- ADD THIS
    
    // ... existing bounds check and slot access ...
    
    ReleaseSRWLockShared(&registry->lock);  // <-- ADD THIS
    return archetype;
}
```

#### 2.3 Exclusive Lock for rebuild_hash (sdk_construction_cells.c:913-947)
```c
static int construction_registry_rebuild_hash(SdkConstructionArchetypeRegistry* registry)
{
    if (!registry) return 0;
    
    AcquireSRWLockExclusive(&registry->lock);  // <-- ADD THIS
    
    // ... existing rebuild code ...
    
    ReleaseSRWLockExclusive(&registry->lock);  // <-- ADD THIS
    return 1;
}
```

#### 2.4 Exclusive Lock for acquire_id (sdk_construction_cells.c:1050-1084)
```c
static SdkConstructionArchetypeId construction_registry_acquire_id(
    SdkConstructionArchetypeRegistry* registry,
    uint32_t hash,
    const uint16_t* palette,
    uint8_t palette_count,
    const uint8_t* voxel_palette,
    int* out_created)
{
    AcquireSRWLockExclusive(&registry->lock);  // <-- ADD THIS
    
    // ... existing acquire logic, including potential rebuild_hash call ...
    
    ReleaseSRWLockExclusive(&registry->lock);  // <-- ADD THIS
    return id;
}
```

#### 2.5 Exclusive Lock for release_id (sdk_construction_cells.c:981-995)
```c
static void construction_registry_release_id(SdkConstructionArchetypeRegistry* registry,
                                           SdkConstructionArchetypeId id)
{
    AcquireSRWLockExclusive(&registry->lock);  // <-- ADD THIS
    
    // ... existing release logic ...
    
    ReleaseSRWLockExclusive(&registry->lock);  // <-- ADD THIS
}
```

### Phase 3: Revision Validation (Optional Enhancement)

For additional safety, readers can validate the revision hasn't changed:

```c
typedef struct SdkConstructionReadToken {
    const SdkConstructionArchetypeRegistry* registry;
    uint32_t revision_at_acquire;
} SdkConstructionReadToken;

// Acquire shared lock and capture revision
void sdk_construction_registry_read_begin(SdkConstructionReadToken* token, 
                                           const SdkConstructionArchetypeRegistry* registry);

// Release lock and validate no rebuild occurred
int sdk_construction_registry_read_end(SdkConstructionReadToken* token);  // Returns 1 if valid, 0 if stale
```

---

## Testing Strategy

### 1. Stress Test
```c
// Spawn multiple threads repeatedly calling find_match and release_id
// Run for extended period (hours) to catch races
```

### 2. Validation Test
```c
// Instrumented build with artificial delays in rebuild_hash
// Verify no crashes occur with concurrent readers
```

### 3. Regression Test
```c
// Re-run existing construction cell unit tests
// Verify no performance degradation
```

---

## Files to Modify

| File | Lines | Change |
|------|-------|--------|
| `sdk_construction_cells.h:40-48` | Struct definition | Add `SRWLOCK lock;` |
| `sdk_construction_cells.c:1086-1099` | `sdk_construction_registry_create()` | Add `InitializeSRWLock()` |
| `sdk_construction_cells.c:913-947` | `construction_registry_rebuild_hash()` | Add exclusive lock |
| `sdk_construction_cells.c:949-975` | `construction_registry_get_mutable()` | Add shared lock |
| `sdk_construction_cells.c:976-980` | `construction_registry_get_const()` | Add shared lock |
| `sdk_construction_cells.c:997-1024` | `construction_registry_find_match()` | Add shared lock |
| `sdk_construction_cells.c:1050-1084` | `construction_registry_acquire_id()` | Add exclusive lock |
| `sdk_construction_cells.c:981-995` | `construction_registry_release_id()` | Add exclusive lock |

---

## Related Issues

- **Startup Streaming Instability:** Worker threads affected by this crash may contribute to startup instability
- **Wall System Contract Failure:** Wall chunk generation uses construction cells, may be impacted

---

## References

- Windows SRWLOCK Documentation: https://learn.microsoft.com/en-us/windows/win32/sync/slim-reader-writer--srw--locks
- Original Audit Report: `../engine_audit_2026-04-10.md`
