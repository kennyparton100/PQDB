/**
 * cpss_util.c - Utility functions: memory, die, bitops, wheel init, presieve, zlib
 * Part of the CPSS Viewer amalgamation.
 */
/** Print error message and exit. */
static void die(const char* msg) {
    fprintf(stderr, "error: %s\n", msg);
    exit(EXIT_FAILURE);
}

/** Print error message with errno description and exit. */
static void die_errno(const char* msg) {
    fprintf(stderr, "error: %s: %s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

/** Print error message with Win32 GetLastError description and exit. */
static void die_win32(const char* msg) {
    DWORD err = GetLastError();
    LPSTR text = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    (void)FormatMessageA(flags, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&text, 0, NULL);
    if (text) {
        size_t len = strlen(text);
        while (len && (text[len - 1] == '\r' || text[len - 1] == '\n')) text[--len] = '\0';
        fprintf(stderr, "error: %s: %s (0x%08lX)\n", msg, text, (unsigned long)err);
        LocalFree(text);
    }
    else {
        fprintf(stderr, "error: %s: Windows error 0x%08lX\n", msg, (unsigned long)err);
    }
    exit(EXIT_FAILURE);
}

/** Allocate memory or die on failure. */
static void* xmalloc(size_t n) {
    void* p = malloc(n == 0 ? 1u : n);
    if (!p) {
        die("out of memory");
    }
    return p;
}

/** Zero-allocate memory or die on failure. */
static void* xcalloc(size_t count, size_t size) {
    void* p = calloc(count == 0 ? 1u : count, size == 0 ? 1u : size);
    if (!p) {
        die("out of memory");
    }
    return p;
}

/** Reallocate memory or die on failure. */
static void* xrealloc(void* ptr, size_t n) {
    void* p = realloc(ptr, n == 0 ? 1u : n);
    if (!p) {
        die("out of memory");
    }
    return p;
}

/** Read a little-endian uint32 from raw bytes. */
static uint32_t read_u32_le(const uint8_t* p) {
    return ((uint32_t)p[0]) |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
}

/** Write a little-endian uint32 to a file. */
static void write_u32_le(FILE* fp, uint32_t v) {
    uint8_t buf[4];
    buf[0] = (uint8_t)(v & 0xFFu);
    buf[1] = (uint8_t)((v >> 8) & 0xFFu);
    buf[2] = (uint8_t)((v >> 16) & 0xFFu);
    buf[3] = (uint8_t)((v >> 24) & 0xFFu);
    if (fwrite(buf, 1, sizeof(buf), fp) != sizeof(buf)) {
        die_errno("failed writing file");
    }
}

/** Compute GCD of two uint64 values. */
static uint64_t gcd_u64(uint64_t a, uint64_t b) {
    while (b != 0u) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/** Integer square root of a uint64, returned as uint32. */
static uint32_t isqrt_u64(uint64_t x) {
    long double r = sqrtl((long double)x);
    uint64_t y = (uint64_t)r;
    while ((y + 1u) * (uint64_t)(y + 1u) <= x) {
        ++y;
    }
    while (y * y > x) {
        --y;
    }
    if (y > UINT32_MAX) {
        die("square root exceeds uint32 range");
    }
    return (uint32_t)y;
}

/** High-resolution timestamp in seconds via QueryPerformanceCounter. */
static double now_sec(void) {
    static LARGE_INTEGER freq;
    static BOOL ready = FALSE;
    LARGE_INTEGER now;
    if (!ready) {
        if (!QueryPerformanceFrequency(&freq)) die_win32("QueryPerformanceFrequency failed");
        ready = TRUE;
    }
    if (!QueryPerformanceCounter(&now)) die_win32("QueryPerformanceCounter failed");
    return (double)now.QuadPart / (double)freq.QuadPart;
}

/** Build the 256-entry byte popcount lookup table. */
static void init_popcount_table(void) {
    for (int i = 0; i < 256; ++i) {
        uint8_t x = (uint8_t)i;
        uint8_t c = 0;
        while (x) {
            c += (uint8_t)(x & 1u);
            x >>= 1u;
        }
        g_popcount8[i] = c;
    }
}

/** Append a value to a dynamic uint32 vector, growing if needed. */
static void u32vec_push(U32Vec* v, uint32_t x) {
    if (v->len == v->cap) {
        size_t new_cap = v->cap ? v->cap * 2u : 16u;
        v->data = (uint32_t*)xrealloc(v->data, new_cap * sizeof(*v->data));
        v->cap = new_cap;
    }
    v->data[v->len++] = x;
}

/** Return true if p is one of the wheel primes. */
static bool is_wheel_prime(uint32_t p) {
    for (size_t i = 0; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        if (WHEEL_PRIMES[i] == p) {
            return true;
        }
    }
    return false;
}

/** Compute wheel modulus, residues, and residue-to-position map (once). */
static void init_wheel(void) {
    if (g_wheel_modulus != 0u) {
        return;
    }

    uint64_t modulus = 1u;
    for (size_t i = 0; i < ARRAY_LEN(WHEEL_PRIMES); ++i) {
        modulus *= WHEEL_PRIMES[i];
    }
    if (modulus > UINT32_MAX) {
        die("wheel modulus overflow");
    }
    g_wheel_modulus = (uint32_t)modulus;

    g_wheel_residues = (uint32_t*)xmalloc(g_wheel_modulus * sizeof(*g_wheel_residues));
    g_residue_to_pos = (int32_t*)xmalloc(g_wheel_modulus * sizeof(*g_residue_to_pos));
    for (uint32_t i = 0; i < g_wheel_modulus; ++i) {
        g_residue_to_pos[i] = -1;
    }

    uint32_t count = 0u;
    for (uint32_t r = 0; r < g_wheel_modulus; ++r) {
        if (gcd_u64(r, g_wheel_modulus) == 1u) {
            g_wheel_residues[count] = r;
            g_residue_to_pos[r] = (int32_t)count;
            ++count;
        }
    }
    g_residue_count = count;
    g_wheel_residues = (uint32_t*)xrealloc(g_wheel_residues, g_residue_count * sizeof(*g_wheel_residues));

    /* Validate that cpss_presieve.c compile-time constants match runtime values.
     * PRESIEVE_WMOD and PRESIEVE_RCNT are hardcoded in the separate TU for
     * codegen optimization (avoids costly div/mod in the hot inner loop). */
    if (g_wheel_modulus != 30030u || g_residue_count != 5760u) {
        fprintf(stderr, "FATAL: wheel constants mismatch! g_wheel_modulus=%u (expected 30030), "
            "g_residue_count=%u (expected 5760). Update PRESIEVE_WMOD/PRESIEVE_RCNT in cpss_presieve.c.\n",
            g_wheel_modulus, g_residue_count);
        exit(1);
    }
}

/** Generate all primes up to limit via an odd-only sieve of Eratosthenes. */
static U32Vec simple_base_primes(uint32_t limit) {
    U32Vec primes = { 0 };
    if (limit < 2u) {
        return primes;
    }
    if (limit == 2u) {
        u32vec_push(&primes, 2u);
        return primes;
    }

    size_t size = (size_t)(limit / 2u) + 1u;
    uint8_t* sieve = (uint8_t*)xmalloc(size);
    memset(sieve, 1, size);
    sieve[0] = 0u;

    uint32_t root = isqrt_u64(limit);
    for (uint32_t i = 1u; i <= root / 2u; ++i) {
        if (sieve[i]) {
            uint32_t p = 2u * i + 1u;
            uint64_t start = ((uint64_t)p * (uint64_t)p) / 2u;
            for (uint64_t j = start; j < size; j += p) {
                sieve[j] = 0u;
            }
        }
    }

    u32vec_push(&primes, 2u);
    for (size_t i = 1u; i < size; ++i) {
        if (sieve[i]) {
            u32vec_push(&primes, (uint32_t)(2u * (uint32_t)i + 1u));
        }
    }

    free(sieve);
    return primes;
}

/** Build the global list of presieve primes (excluding wheel primes). */
static void init_presieve_primes(void) {
    if (g_presieve_primes) {
        return;
    }

    U32Vec base = simple_base_primes(PRESIEVE_LIMIT);
    U32Vec filtered = { 0 };
    for (size_t i = 0; i < base.len; ++i) {
        if (!is_wheel_prime(base.data[i])) {
            u32vec_push(&filtered, base.data[i]);
        }
    }
    free(base.data);
    g_presieve_primes = filtered.data;
    g_presieve_prime_count = filtered.len;
}

/** Parse a benchmark mode string. Dies on invalid input. */
static BenchmarkMode parse_benchmark_mode(const char* s) {
    if (strcmp(s, "hot") == 0) return BENCH_HOT;
    if (strcmp(s, "fair") == 0) return BENCH_FAIR;
    if (strcmp(s, "auto") == 0) return BENCH_AUTO;
    fprintf(stderr, "error: invalid benchmark mode '%s' (expected hot, fair, auto)\n", s);
    exit(EXIT_FAILURE);
}

/** Return human-readable name for a benchmark mode. */
static const char* benchmark_mode_name(BenchmarkMode m) {
    switch (m) {
    case BENCH_HOT:  return "hot";
    case BENCH_FAIR: return "fair";
    case BENCH_AUTO: return "auto";
    }
    return "unknown";
}

/** Ensure global base prime cache covers all primes up to `up_to`. */
static void ensure_base_primes_cached(uint32_t up_to) {
    if (up_to <= g_cached_base_primes_limit) {
        return;
    }
    if (g_cached_base_primes.data) {
        free(g_cached_base_primes.data);
        memset(&g_cached_base_primes, 0, sizeof(g_cached_base_primes));
    }
    g_cached_base_primes = simple_base_primes(up_to);
    g_cached_base_primes_limit = up_to;
}

/**
 * Parse a human-readable byte size string.  Accepts optional suffix:
 *   B, KB, MB, GB, TB  (case-insensitive, powers of 2).
 * A bare number with no suffix is treated as bytes.
 */
static size_t parse_size_bytes(const char* s, const char* name) {
    errno = 0;
    char* end = NULL;
    double val = strtod(s, &end);
    if (errno != 0 || end == s || val < 0.0) {
        fprintf(stderr, "error: invalid %s: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    uint64_t multiplier = 1u;
    if (end && *end != '\0') {
        if (_stricmp(end, "B") == 0)       multiplier = 1ull;
        else if (_stricmp(end, "KB") == 0)  multiplier = 1024ull;
        else if (_stricmp(end, "MB") == 0)  multiplier = 1024ull * 1024ull;
        else if (_stricmp(end, "GB") == 0)  multiplier = 1024ull * 1024ull * 1024ull;
        else if (_stricmp(end, "TB") == 0)  multiplier = 1024ull * 1024ull * 1024ull * 1024ull;
        else {
            fprintf(stderr, "error: invalid size suffix in %s: %s\n", name, s);
            exit(EXIT_FAILURE);
        }
    }
    double result = val * (double)multiplier;
    if (result > (double)SIZE_MAX) {
        fprintf(stderr, "error: %s value too large: %s\n", name, s);
        exit(EXIT_FAILURE);
    }
    return (size_t)result;
}
