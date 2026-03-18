/**
 * cpss_status.c - Query status model for reliable result reporting.
 * Part of the CPSS Viewer amalgamation.
 *
 * Every query that depends on DB coverage should report a CPSSQueryStatus
 * so callers (especially factorisation routines) can distinguish between
 * "definitely zero primes" and "we don't know because data is missing".
 */

typedef enum {
    CPSS_OK,            /* result is complete and correct */
    CPSS_INCOMPLETE,    /* partial result: some segments unavailable */
    CPSS_OUT_OF_RANGE,  /* query falls entirely outside loaded DB range */
    CPSS_UNAVAILABLE    /* no data at all (no DB loaded, file unmapped) */
} CPSSQueryStatus;

/** Return a human-readable name for a query status. */
static const char* cpss_status_name(CPSSQueryStatus s) {
    switch (s) {
    case CPSS_OK:           return "OK";
    case CPSS_INCOMPLETE:   return "INCOMPLETE";
    case CPSS_OUT_OF_RANGE: return "OUT_OF_RANGE";
    case CPSS_UNAVAILABLE:  return "UNAVAILABLE";
    }
    return "UNKNOWN";
}

/** Return true if the status indicates a usable (though possibly partial) result. */
static inline bool cpss_status_usable(CPSSQueryStatus s) {
    return s == CPSS_OK || s == CPSS_INCOMPLETE;
}
