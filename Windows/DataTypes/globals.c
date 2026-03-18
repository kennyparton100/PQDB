/**
 * cpss_globals.c - Global state, wheel tables, cache struct instances
 * Part of the CPSS Viewer amalgamation.
 */
/* Non-static: visible to cpss_presieve.c (separate TU) via cpss_presieve.h extern decls */
uint32_t g_wheel_modulus = 0;
uint32_t* g_wheel_residues = NULL;
uint32_t g_residue_count = 0;
int32_t* g_residue_to_pos = NULL;
uint32_t* g_presieve_primes = NULL;
size_t g_presieve_prime_count = 0;
static uint8_t g_popcount8[256];

/** Global cached base primes for benchmark sieve baseline. */
static U32Vec g_cached_base_primes = { 0 };
static uint32_t g_cached_base_primes_limit = 0;

/** APP-level wheel/presieve cache budget (bytes). 0 = disabled. */
static size_t g_app_wheel_cache_bytes = 0;

/** APP-level thread count for parallel preload. 1 = single-threaded (default). */
static uint32_t g_app_threads = 1u;

/**
 * Cache read-only mode for thread-safe batch execution.
 * When true, cache lookups skip LRU counter updates (no writes at all).
 * Combined with budget=0, this makes the entire cache layer truly read-only
 * with zero data races. Must be set BEFORE spawning worker threads.
 */
static bool g_cache_readonly = false;

/**
 * Per-segment presieve/helper cache entry.
 * Keyed by (stream_id, segment_no) for multi-shard safety.
 * Stores the presieve bitmap returned by build_presieve_state_for_index_range().
 * LRU ordering is maintained via a use_counter that increments globally.
 */
typedef struct {
    uint32_t stream_id;   /* owning stream ID for multi-shard disambiguation */
    uint64_t segment_no;
    uint64_t start_index;
    uint32_t total_candidates;
    uint8_t* data;
    size_t   bytes;       /* byte length of data */
    uint64_t last_use;    /* monotonic counter for LRU */
    bool     occupied;
} WheelCacheEntry;

typedef struct {
    WheelCacheEntry* entries;
    size_t cap;           /* allocated entry slots */
    size_t count;         /* occupied slots */
    size_t bytes_used;    /* sum of all entry->bytes */
    size_t budget;        /* max bytes allowed */
    uint64_t use_counter; /* monotonic LRU clock */
    size_t* hash_map;     /* open-addressing hash table: slot index or SIZE_MAX */
    size_t hash_cap;      /* hash table capacity (power of 2) */
} WheelCache;

/** Global wheel cache instance. */
static WheelCache g_wheel_cache = { 0 };

/**
 * Hot decoded segment cache entry.
 * Stores decompressed segment payload (result_bits) alongside the presieve
 * bitmap and a survivor-rank index for O(1) is_prime lookups.
 *
 * The rank index divides the presieve into blocks of RANK_BLOCK_SIZE
 * candidates.  rank_prefix[k] = number of survivors in presieve bits
 * [0, k*RANK_BLOCK_SIZE).  This allows computing survivor_pos for any
 * candidate index with a single block popcount instead of scanning from 0.
 */
#define RANK_BLOCK_SIZE 4096u

typedef struct {
    uint32_t stream_id;
    uint64_t segment_no;
    uint64_t start_index;
    uint32_t total_candidates;
    uint32_t survivor_count;
    uint8_t* presieve;         /* presieve bitmap (owned) */
    size_t   presieve_len;
    uint8_t* result_bits;      /* decoded result_bits (owned, separate from raw) */
    uint32_t result_bits_len;
    uint32_t* rank_prefix;     /* rank_prefix[k] = survivors before block k */
    size_t   rank_blocks;      /* number of blocks */
    uint32_t* rbits_rank;      /* rbits_rank[k] = primes in result_bits[0..k*RANK_BLOCK_SIZE) */
    size_t   rbits_rank_blocks;
    uint64_t prime_count;      /* primes in result_bits (excludes wheel primes) */
    size_t   total_bytes;      /* total heap bytes for this entry (for budget tracking) */
    uint64_t last_use;
    bool     occupied;
} HotSegmentEntry;

typedef struct {
    HotSegmentEntry* entries;
    size_t cap;
    size_t count;
    size_t bytes_used;
    size_t budget;          /* separate budget from wheel cache */
    uint64_t use_counter;
    size_t* hash_map;       /* open-addressing hash table: slot index or SIZE_MAX */
    size_t hash_cap;        /* hash table capacity (power of 2) */
} HotSegmentCache;

/** Global hot segment cache instance. */
static HotSegmentCache g_hot_cache = { 0 };
