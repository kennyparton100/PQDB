/**
 * gnfs_types.c — GNFS constants, struct definitions, and config defaults.
 * Part of the GNFS sub-module (included via cpss_factor_gnfs.c stub).
 */

#define GNFS_MAX_FB      10000   /* max primes in rational or algebraic FB */
#define GNFS_MAX_ALG_FB  30000   /* max algebraic ideals (d roots per prime) */
#define GNFS_MAX_RELS     4096   /* max relations stored */
#define GNFS_MAX_DEGREE      5

typedef enum {
    GNFS_POLY_BASE_M,     /* Base-m digit extraction (default) */
    GNFS_POLY_STRATEGY_COUNT
} GNFSPolyStrategy;

/** GNFS tunable configuration. */
typedef struct {
    int degree;          /* polynomial degree (default 3) */
    int rfb_bound;       /* rational FB: primes up to this */
    int afb_bound;       /* algebraic FB: primes up to this */
    int a_range;         /* sieve a half-width: test a in [-a_range, a_range] */
    int b_range;         /* sieve b max: test b in [1, b_range] */
    int target_rels;     /* 0 = auto */
    int char_cols;       /* number of quadratic character columns (default 8) */
    GNFSPolyStrategy poly_strategy; /* polynomial selection strategy */
    bool diag;           /* diagnostic output */
    bool verify_rows;    /* audit stored matrix rows against recomputed values */
    bool audit_deps;     /* dependency-level sign/character effectiveness audit */
    bool audit_chars;    /* character differential audit: no-char vs full-char matrix comparison */
    bool sweep_chars;    /* character-strength sweep: test chars=0,8,16,32 */
    bool measure_alg;    /* exact integer algebraic-product measurement */
    bool recover_one;    /* guarded single-prime algebraic recovery attempt */
    bool audit_units;    /* infinite-place / unit obstruction audit */
    bool audit_real_sign; /* differential audit: real-embedding sign column */
    bool audit_order;     /* order/discriminant/index audit */
    bool schk2;           /* 2-adic Schirokauer map columns */
    bool schirokauer;     /* \ell-adic Schirokauer map columns (ell=3) */
    bool crt_recover;     /* CRT multi-prime algebraic sqrt recovery */
    bool synth_square;    /* synthetic-square CRT pipeline test */
    bool selftest;        /* run alg_mul reference oracle fuzz test */
    bool synth_hensel;    /* synthetic Hensel lift test only */
    bool local_square;    /* local-square battery on real deps */
    bool variant_sweep;   /* matrix-variant local-square comparison */
} GNFSConfig;

/** Algebraic factor base element: prime ideal (p, r) where f(r) ≡ 0 mod p. */
typedef struct { uint64_t p; uint64_t r; } GNFSAlgPrime;

/** GNFS polynomial: f(x) = coeffs[d]*x^d + ... + coeffs[0], with root m. */
typedef struct {
    int degree;
    BigNum coeffs[GNFS_MAX_DEGREE + 1];
    BigNum m;  /* f(m) ≡ 0 (mod N) */
} GNFSPoly;

/** Set GNFS config defaults for given bit length. */
static void gnfs_config_default(unsigned bits, GNFSConfig* cfg) {
    cfg->degree = (bits < 200) ? 3 : (bits < 400) ? 4 : 5;
    cfg->rfb_bound = (bits < 30) ? 200 : (bits < 60) ? 2000 : (bits < 100) ? 5000 : 10000;
    cfg->afb_bound = cfg->rfb_bound;
    cfg->a_range = (bits < 30) ? 1000 : (bits < 60) ? 50000 : 100000;
    cfg->b_range = (bits < 30) ? 10 : (bits < 60) ? 100 : (bits < 100) ? 500 : 1000;
    cfg->target_rels = 0; /* auto */
    cfg->char_cols = 8;
    cfg->poly_strategy = GNFS_POLY_BASE_M;
    cfg->diag = false;
    cfg->verify_rows = false;
    cfg->audit_deps = false;
    cfg->audit_chars = false;
    cfg->sweep_chars = false;
    cfg->measure_alg = false;
    cfg->recover_one = false;
    cfg->audit_units = false;
    cfg->audit_real_sign = false;
    cfg->audit_order = false;
    cfg->schk2 = false;
    cfg->schirokauer = false;
    cfg->crt_recover = false;
    cfg->synth_square = false;
    cfg->selftest = false;
    cfg->synth_hensel = false;
    cfg->local_square = false;
    cfg->variant_sweep = false;
}
