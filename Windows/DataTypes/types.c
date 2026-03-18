/**
 * cpss_types.c - Type definitions (structs, enums, typedefs)
 * Part of the CPSS Viewer amalgamation.
 */

/** Portable 128-bit unsigned integer (works on all compilers, no __int128 needed). */
typedef struct {
    uint64_t lo;
    uint64_t hi;
} U128;

static const uint32_t WHEEL_PRIMES[] = { 2u, 3u, 5u, 7u, 11u, 13u };

typedef struct {
    uint32_t* data;
    size_t len;
    size_t cap;
} U32Vec;

typedef struct {
    uint8_t* data;
    size_t len;
} ByteBuf;

typedef struct {
    uint64_t segment_no;
    uint64_t start_index;
    uint64_t next_index;
    uint32_t total_candidates;
    uint32_t survivor_count;
    uint32_t result_bits_len;
    size_t raw_size;
    uint32_t compressed_size;
    uint64_t file_offset;
    uint64_t start_n;         /* cached: first candidate number in segment */
    uint64_t end_n;           /* cached: last candidate number in segment */
} SegmentIndexEntry;

typedef struct {
    uint64_t segment_no;
    uint64_t start_index;
    uint64_t next_index;
    uint32_t total_candidates;
    uint32_t survivor_count;
    uint32_t result_bits_len;
    uint8_t* raw_data;
    size_t raw_size;
    uint8_t* result_bits;
    uint32_t compressed_size;
    uint64_t file_offset;
} SegmentRecord;

typedef struct {
    size_t segments;
    uint64_t total_candidates;
    uint64_t total_survivors;
    uint64_t total_primes;
    uint64_t last_next_index;
    uint64_t last_candidate;
    uint64_t raw_total_bytes;
    uint64_t compressed_total_bytes;
    double compression_ratio;
} ValidateSummary;

typedef struct {
    HANDLE file_handle;
    HANDLE mapping_handle;
    uint8_t* map;
    size_t file_size;
    SegmentIndexEntry* index_entries;
    size_t index_len;
    size_t index_cap;
    char* path;           /* owned copy of the file path */
    uint32_t stream_id;    /* unique ID for cache key disambiguation */
    uint64_t base_index;   /* absolute starting wheel index (0 for monolithic) */
    uint64_t* seg_prime_counts;  /* per-segment prime count (excludes wheel primes) */
    uint64_t* seg_prime_prefix;  /* seg_prime_prefix[i] = total primes in segments [0,i) */
    bool prime_index_built;
    bool is_raw;           /* true if file is in raw (decompressed) CPSR format */
    uint64_t data_offset;  /* byte offset where segment data starts (0 for V4, CPSR_HEADER_SIZE for raw) */
} CPSSStream;

typedef struct {
    uint32_t total_candidates;
    uint32_t survivor_count;
    uint8_t* result_bits;
    uint32_t result_bits_len;
} ClippedSegment;

/**
 * Multi-shard CPSS database.
 * Opens a directory of PrimeStateV4_*.bin shard files and provides
 * unified query routing across all shards.  Shards are sorted by
 * their numeric range and must not overlap.
 */
typedef struct {
    CPSSStream* streams;   /* array of open shard streams */
    uint64_t* shard_lo;    /* per-shard first candidate number */
    uint64_t* shard_hi;    /* per-shard last candidate number */
    char** shard_paths;    /* per-shard file path (owned, heap-allocated) */
    size_t shard_count;
    uint64_t db_lo;        /* min candidate across all shards */
    uint64_t db_hi;        /* max candidate across all shards */
    size_t total_segments; /* sum of segment counts across all shards */
} CPSSDatabase;

typedef struct {
    size_t shard_count;
    size_t total_segments;
    uint64_t db_lo;
    uint64_t db_hi;
    size_t cache_budget;
    size_t cache_used;
    size_t cache_entries;
} CPSSDBStatus;

/*
 * Benchmark modes control what CPSS costs are included in timed measurement.
 *
 *   hot  - Assumes sector is in its production-hot in-memory form.
 *          Decompression excluded if raw_size < 1.2 * compressed_size.
 *          Presieve/helper state excluded (considered resident).
 *
 *   fair - Measures online work after shard is already in RAM.
 *          Decompression included if raw_size >= 1.2 * compressed_size.
 *          Per-segment presieve build included.
 *
 *   auto - Same logic as fair; the 1.2x rule auto-decides decompression
 *          per sector.  Per-sector decision is printed.
 *
 * The 1.2x decompression rule:
 *   if (raw_size >= 1.2 * compressed_size) decompression cost is included,
 *   because production would not keep the decompressed form resident for free.
 *   Otherwise production keeps it decompressed, so decompression is excluded.
 *
 * Global reusable setup (popcount table, wheel, presieve primes, cached base
 * primes) is allowed outside the timer for BOTH sides.
 */
typedef enum {
    BENCH_HOT,
    BENCH_FAIR,
    BENCH_AUTO
} BenchmarkMode;

typedef struct {
    uint64_t cpss_count;
    double cpss_decompress_time;
    double cpss_prep_time;
    double cpss_query_time;
    double cpss_total_time;
    bool decomp_included;
    bool prep_included;
    double cpss_write_time;
    bool cpss_write_time_valid;
    uint64_t sieve_count;
    double sieve_total_time;
    double sieve_write_time;
    bool sieve_write_time_valid;
} BenchmarkRow;

typedef struct {
    uint64_t segment_no;
    uint64_t start_n;
    uint64_t end_n;
    uint32_t candidates;
    uint32_t survivors;
    uint64_t stream_prime_count;
    uint32_t compressed_size;
    size_t raw_size;
    double expansion_ratio;
    const char* mode_name;
    bool decomp_included;
    bool prep_included;
    int repeats;
    uint64_t limit;
    double* cpss_total_times;
    double* sieve_total_times;
    double* cpss_decompress_times;
    double* cpss_prep_times;
    double* cpss_query_times;
    double cpss_total_median;
    double sieve_total_median;
    double cpss_decompress_median;
    double cpss_prep_median;
    double cpss_query_median;
    const char* winner;
    double speedup;
    bool count_match;
    bool cache_hit;       /* true if presieve was served from wheel cache (hot mode) */
    bool cache_fits;      /* true if presieve fits in the cache budget */
    size_t presieve_bytes; /* byte size of the presieve bitmap for this segment */
} BenchmarkSectorResult;

/* ══════════════════════════════════════════════════════════════════════
 * Phase 1-3 new types: iterators, coverage, classification, factors
 * ══════════════════════════════════════════════════════════════════════ */

/** Prime iterator / cursor for streaming primes across segments. */
typedef struct {
    CPSSStream* stream;
    uint64_t current;         /* current prime value (0 if invalid) */
    size_t seg_idx;           /* current segment array index */
    uint64_t local_idx;       /* candidate index within segment (relative to start_index) */
    uint64_t survivor_pos;    /* position in result_bits */
    bool valid;               /* true if current holds a valid prime */
    int status;               /* CPSSQueryStatus (stored as int for forward-decl compat) */
} PrimeIterator;

/** Support coverage result for factorisation planning.
 *  Distinguishes DB-level coverage (segment index metadata) from
 *  runtime availability (file mapped or hot-cached right now). */
typedef struct {
    uint64_t n;
    uint64_t sqrt_n;          /* ceil(sqrt(n)) */
    uint64_t db_hi;           /* highest candidate in DB segment index */
    bool db_complete;         /* db_hi >= sqrt_n (DB theoretically covers it) */
    double db_coverage;       /* min(db_hi, sqrt_n) / sqrt_n */
    bool rt_file_mapped;      /* file is still memory-mapped (cold queries work) */
    uint64_t rt_hot_hi;       /* highest candidate in a hot-cached segment */
    bool rt_hot_complete;     /* rt_hot_hi >= sqrt_n (hot cache can certify now) */
    double rt_hot_coverage;   /* min(rt_hot_hi, sqrt_n) / sqrt_n */
    uint64_t prime_count;     /* primes in [2, min(sqrt_n, db_hi)] (0 if not computed) */
} SupportCoverage;

/** Segment info for a given candidate number. */
typedef struct {
    uint64_t n;
    bool found;               /* true if n falls within a segment */
    uint64_t segment_no;
    uint64_t seg_start_n;
    uint64_t seg_end_n;
    uint32_t total_candidates;
    uint32_t survivor_count;
    bool is_hot;              /* segment is in hot cache */
    bool is_cold;             /* segment data is available (file mapped) */
    uint64_t prime_count;     /* primes in segment (0 if unknown) */
} SegmentInfo;

/** Composite classification for factor routing. */
typedef struct {
    uint64_t n;
    bool is_even;
    bool is_perfect_square;
    bool is_prime;            /* via DB if in range, else Miller-Rabin */
    bool is_prime_power;
    bool has_small_factor;    /* divisible by a wheel prime */
    uint64_t smallest_factor; /* smallest factor found (0 if none yet) */
    int prime_source;         /* 0=unknown, 1=DB, 2=Miller-Rabin */
} CompositeClass;

/** Factor result returned by factorisation engines. */
typedef struct {
    uint64_t n;               /* original input */
    uint64_t factors[64];     /* found prime factors */
    uint64_t exponents[64];   /* corresponding exponents */
    size_t factor_count;      /* number of distinct factors found */
    uint64_t cofactor;        /* remaining unfactored part (1 if fully factored) */
    bool fully_factored;
    int status;               /* CPSSQueryStatus */
    const char* method;       /* "trial", "pminus1", "auto", etc. */
    double time_seconds;
} FactorResult;

/** Factor result for 128-bit input numbers. Factors are still uint64 (from DB). */
typedef struct {
    U128 n;                   /* original 128-bit input */
    uint64_t factors[64];     /* found prime factors (all fit in uint64) */
    uint64_t exponents[64];
    size_t factor_count;
    U128 cofactor;            /* remaining unfactored part */
    bool fully_factored;
    int status;               /* CPSSQueryStatus */
    const char* method;
    double time_seconds;
} FactorResultU128;

/** Fixed-max-width big integer for 1024-bit support. */
#define BN_MAX_LIMBS 64   /* 64 × 64 = 4096 bits. Do not raise without measured coefficient bounds (see GNFS dev spec Phase 4). */

typedef struct {
    uint64_t limbs[BN_MAX_LIMBS];  /* little-endian: limbs[0] = least significant */
    uint32_t used;                  /* active limb count (1..BN_MAX_LIMBS) */
} BigNum;

/** Montgomery context for modular arithmetic with a fixed odd modulus. */
typedef struct {
    BigNum n;                      /* the modulus (must be odd) */
    BigNum r2;                     /* R^2 mod n, for converting to Montgomery form */
    uint64_t n_inv;                /* -n^(-1) mod 2^64 */
    uint32_t r_limbs;              /* R = 2^(64 * r_limbs) */
} MontCtx;

/** Factor result for BigNum (1024-bit) factorisation. */
typedef struct {
    BigNum n;                      /* original input */
    BigNum factor;                 /* found non-trivial factor (BigNum) */
    BigNum cofactor;               /* n / factor */
    bool factor_found;             /* true if a non-trivial factor was found */
    bool factor_is_prime;          /* true if factor was proven/tested prime */
    bool cofactor_is_prime;        /* true if cofactor was proven/tested prime */
    bool fully_factored;           /* true only if both parts are proven prime */
    int status;                    /* CPSSQueryStatus */
    const char* method;
    double time_seconds;
} FactorResultBigNum;

/** Numeric tier for tiered execution model. */
typedef enum { TIER_U64, TIER_U128, TIER_BN } NumericTier;

/** Universal parsed numeric value with tier detection. */
typedef struct {
    BigNum bn;        /* always populated */
    uint64_t u64;     /* valid if tier == TIER_U64 */
    U128 u128;        /* valid if tier <= TIER_U128 */
    NumericTier tier;
    unsigned bits;    /* approximate bit length */
} ParsedNum;

/** Parsed manifest shard entry (used by cpss_manifest.c and cpss_database.c). */
typedef struct {
    char path[512];
    char sidecar[512];
    uint64_t lo, hi;
    size_t segments;
    uint64_t file_size;
    bool has_sidecar;
} ManifestShardEntry;

/** Parsed manifest result. */
typedef struct {
    bool loaded;
    int schema_version;
    ManifestShardEntry* shards;
    size_t shard_count;
    uint64_t db_lo, db_hi;
    size_t total_segments;
    const char* reject_reason;
} ManifestLoadResult;
