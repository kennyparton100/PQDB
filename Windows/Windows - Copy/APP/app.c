/**
 * cpss_app.c - Entry point: main(), APP mode loop, command dispatch
 * Part of the CPSS Viewer amalgamation.
 */
#define MAX_LINE 1024
#define MAX_ARGS 64

/* ── Shared bounded-view infrastructure for hidden-factor-space commands ── */

/** Per-space cell printer: receives one IndexSpaceCell and prints its space-specific analysis. */
typedef void (*SpaceCellPrinter)(const IndexSpaceCell* cell);

static void print_cell_value_space(const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    ValueSpaceRegion reg = value_space_region(c->p, c->q);
    const char* rn = reg == VS_NEAR_SQUARE ? "near-sq" : reg == VS_MODERATE ? "moderate" : "skewed";
    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64"*%"PRIu64"=%s  dist=%"PRIu64"  logimb=%.4f  ratio=%.4f  %s\n",
        c->i, c->j, c->p, c->q, nbuf,
        value_space_additive_distance(c->p, c->q),
        value_space_log_imbalance(c->p, c->q),
        value_space_balance_ratio(c->p, c->q), rn);
}

static void print_cell_log_space(const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    double u, v;
    log_space_coords(c->p, c->q, &u, &v);
    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64"*%"PRIu64"=%s  u=%.4f v=%.4f  u+v=%.4f  |u-v|=%.4f  imb=%.4f\n",
        c->i, c->j, c->p, c->q, nbuf, u, v, u + v,
        log_space_diagonal_distance(c->p, c->q),
        log_space_imbalance(c->p, c->q));
}

static void print_cell_mean_imbalance(const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    double m, delta;
    mean_imbalance_from_factors(c->p, c->q, &m, &delta);
    MeanImbalanceBias bias = mean_imbalance_bias(m, delta);
    const char* bn = bias == MI_BIAS_NEAR_SQUARE ? "near-sq"
                   : bias == MI_BIAS_MODERATE ? "moderate"
                   : bias == MI_BIAS_UNBALANCED ? "unbal" : "skewed";
    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64"*%"PRIu64"=%s  m=%.4f  d=%.4f  |d|/m=%.4f  %s\n",
        c->i, c->j, c->p, c->q, nbuf, m, delta,
        m > 0.0 ? fabs(delta) / m : 0.0, bn);
}

static void print_cell_product_ratio(const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    double R = ratio_from_factors(c->p, c->q);
    RatioClass rc = ratio_classify(R);
    const char* rn = rc == RC_BALANCED ? "balanced" : rc == RC_MODERATE ? "moderate" : "skewed";
    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64"*%"PRIu64"=%s  R=%.4f  %s\n",
        c->i, c->j, c->p, c->q, nbuf, R, rn);
}

static void print_cell_fermat_space(const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    double Af, Bf;
    fermat_space_from_factors(c->p, c->q, &Af, &Bf);
    uint64_t Ae, Be;
    bool exact = fermat_space_from_factors_exact(c->p, c->q, &Ae, &Be);
    uint64_t N64 = u128_fits_u64(c->product) ? c->product.lo : 0u;
    if (exact && N64 > 0u) {
        uint64_t start = fermat_space_search_start(N64);
        uint64_t steps = fermat_space_estimated_steps(N64, c->p, c->q);
        printf("  (%"PRIu64",%"PRIu64")  %"PRIu64"*%"PRIu64"=%s  A=%"PRIu64" B=%"PRIu64"  start=%"PRIu64" steps=%"PRIu64"\n",
            c->i, c->j, c->p, c->q, nbuf, Ae, Be, start, steps);
    } else {
        printf("  (%"PRIu64",%"PRIu64")  %"PRIu64"*%"PRIu64"=%s  A=%.1f B=%.1f  (half-int)\n",
            c->i, c->j, c->p, c->q, nbuf, Af, Bf);
    }
}

static void print_cell_minmax_space(const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    uint64_t s, l;
    minmax_canonicalize(c->p, c->q, &s, &l);
    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64"*%"PRIu64"=%s  s=%"PRIu64" l=%"PRIu64"  cov13=%s cov100=%s\n",
        c->i, c->j, c->p, c->q, nbuf, s, l,
        minmax_is_covered(s, 13u) ? "y" : "n",
        minmax_is_covered(s, 100u) ? "y" : "n");
}

static void print_cell_cfrac_space(const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    CFracExpansion cf = cfrac_expand(c->p, c->q);
    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64"*%"PRIu64"=%s  cf_len=%u  max_a=%"PRIu64"  mean_a=%.2f  bits/step=%.3f  diff=%.2f\n",
        c->i, c->j, c->p, c->q, nbuf,
        cf.length, cf.max_partial, cf.mean_partial, cf.convergent_quality,
        cfrac_difficulty(c->p, c->q));
}

static void print_cell_lattice_space(const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    LatticeMetrics lm = lattice_analyse(c->p, c->q);
    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64"*%"PRIu64"=%s  sv=%.2f  orth=%.4f  angle=%.4f  gauss=%s  diff=%.2f\n",
        c->i, c->j, c->p, c->q, nbuf,
        lm.shortest_vector, lm.orthogonality_defect, lm.balance_angle,
        lm.gauss_reduced ? "y" : "n",
        lattice_difficulty(c->p, c->q));
}

/** Region types for the unified space command. */
typedef enum {
    REGION_POINT,       /* single (i,j) */
    REGION_ROW,         /* row i, j_lo..j_hi */
    REGION_COL,         /* col j, i_lo..i_hi */
    REGION_RECT,        /* full rectangle */
    REGION_RECT_UNIQUE, /* upper triangle within rectangle */
    REGION_DIAGONAL,    /* (i,i) for i in range */
    REGION_BAND,        /* (i, i+offset) for i in range */
    REGION_TRIANGLE,    /* i <= j within range */
    REGION_WINDOW       /* centered window with radius */
} RegionType;

/** Region parameters: enough fields to describe any region type. */
typedef struct {
    RegionType type;
    uint64_t a, b, c, d;  /* meaning depends on type */
    int64_t offset;        /* for band */
} RegionParams;

/** Index-space cell printer (raw index view). */
static void print_cell_index_space(const IndexSpaceCell* c) {
    char nbuf[64]; u128_to_str(c->product, nbuf, sizeof(nbuf));
    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64" * %"PRIu64" = %s\n",
        c->i, c->j, c->p, c->q, nbuf);
}

/** Space registry: name -> printer mapping. */
typedef struct {
    const char* name;
    SpaceCellPrinter printer;
} SpaceEntry;

static const SpaceEntry SPACE_REGISTRY[] = {
    { "index",          print_cell_index_space },
    { "value",          print_cell_value_space },
    { "log",            print_cell_log_space },
    { "mean-imbalance", print_cell_mean_imbalance },
    { "product-ratio",  print_cell_product_ratio },
    { "fermat",         print_cell_fermat_space },
    { "minmax",         print_cell_minmax_space },
    { "cfrac",          print_cell_cfrac_space },
    { "lattice",        print_cell_lattice_space },
};
#define SPACE_REGISTRY_COUNT (sizeof(SPACE_REGISTRY) / sizeof(SPACE_REGISTRY[0]))

static const SpaceEntry* space_lookup(const char* name) {
    for (size_t i = 0u; i < SPACE_REGISTRY_COUNT; ++i) {
        if (strcmp(SPACE_REGISTRY[i].name, name) == 0) return &SPACE_REGISTRY[i];
    }
    return NULL;
}

/* ── Space family infrastructure ── */

typedef enum { FAMILY_PRIMEPAIR, FAMILY_SEMIPRIME, FAMILY_COUNT, FAMILY_UNKNOWN } SpaceFamily;

static const char* family_display_name(SpaceFamily f) {
    switch (f) { case FAMILY_PRIMEPAIR: return "Prime-Pair"; case FAMILY_SEMIPRIME: return "SemiPrime"; default: return "Unknown"; }
}

/** Resolve family name from string with aliases. Returns FAMILY_UNKNOWN on no match. */
static SpaceFamily family_resolve(const char* name) {
    if (!name) return FAMILY_UNKNOWN;
    if (strcmp(name, "prime-pair") == 0 || strcmp(name, "Prime-Pair") == 0 ||
        strcmp(name, "PrimePair") == 0 || strcmp(name, "pair") == 0 || strcmp(name, "pp") == 0)
        return FAMILY_PRIMEPAIR;
    if (strcmp(name, "semiprime") == 0 || strcmp(name, "SemiPrime") == 0 ||
        strcmp(name, "semi-prime") == 0 || strcmp(name, "semi") == 0 || strcmp(name, "sp") == 0)
        return FAMILY_SEMIPRIME;
    return FAMILY_UNKNOWN;
}

/* Per-family space registries: prime-pair reuses SPACE_REGISTRY; semiprime has none yet. */
#define PRIMEPAIR_SPACE_REGISTRY       SPACE_REGISTRY
#define PRIMEPAIR_SPACE_REGISTRY_COUNT SPACE_REGISTRY_COUNT

/* SemiPrime spaces: no cell-printer spaces exist yet.
 * When semiprime observation/coverage spaces gain cell views, add entries here. */
static const SpaceEntry SEMIPRIME_SPACE_REGISTRY[] = {
    /* placeholder — no entries yet */
};
#define SEMIPRIME_SPACE_REGISTRY_COUNT 0u

static const SpaceEntry* family_registry(SpaceFamily f, size_t* out_count) {
    switch (f) {
    case FAMILY_PRIMEPAIR:
        *out_count = PRIMEPAIR_SPACE_REGISTRY_COUNT;
        return PRIMEPAIR_SPACE_REGISTRY;
    case FAMILY_SEMIPRIME:
        *out_count = SEMIPRIME_SPACE_REGISTRY_COUNT;
        return SEMIPRIME_SPACE_REGISTRY;
    default:
        *out_count = 0u;
        return NULL;
    }
}

static const SpaceEntry* family_space_lookup(SpaceFamily f, const char* name) {
    size_t count = 0u;
    const SpaceEntry* reg = family_registry(f, &count);
    for (size_t i = 0u; i < count; ++i) {
        if (strcmp(reg[i].name, name) == 0) return &reg[i];
    }
    return NULL;
}

/* Forward declarations for SemiPrime space registry (defined in cpss_semiprime_space_registry.c,
 * included later via cpss_space_analysis.c) */
typedef struct SemiPrimeSpaceEntry SemiPrimeSpaceEntry;
static void semiprime_list_spaces(void);
static const SemiPrimeSpaceEntry* semiprime_space_lookup(const char* name);
static void semiprime_print_metrics(const SemiPrimeSpaceEntry* sp);

/** Print available spaces in a family. */
static void family_list_spaces(SpaceFamily f) {
    if (f == FAMILY_SEMIPRIME) {
        /* SemiPrime uses its own named registry (per-N report model) */
        semiprime_list_spaces();
        return;
    }
    size_t count = 0u;
    const SpaceEntry* reg = family_registry(f, &count);
    if (count == 0u) {
        printf("  (no spaces available in %s family yet)\n", family_display_name(f));
        return;
    }
    for (size_t i = 0u; i < count; ++i) printf("  %s\n", reg[i].name);
}

/**
 * Unified space resolver: tries [family] [space], then bare [space] → prime-pair.
 * argv/argc: argument array starting at the first candidate token.
 * Returns: SpaceEntry* or NULL. Sets *family_out and *tokens_consumed.
 * If bare name matched, prints a soft compatibility hint.
 */
static const SpaceEntry* resolve_family_space(char** argv, int argc,
                                                SpaceFamily* family_out, int* tokens_consumed) {
    *family_out = FAMILY_PRIMEPAIR;
    *tokens_consumed = 0;
    if (argc < 1) return NULL;

    /* Try argv[0] as family name */
    SpaceFamily fam = family_resolve(argv[0]);
    if (fam != FAMILY_UNKNOWN) {
        if (argc < 2) {
            /* Family given but no space name */
            printf("Missing space name after '%s'. Available %s spaces:\n", argv[0], family_display_name(fam));
            family_list_spaces(fam);
            return NULL;
        }
        const SpaceEntry* sp = family_space_lookup(fam, argv[1]);
        if (sp) {
            *family_out = fam;
            *tokens_consumed = 2;
            return sp;
        }
        /* Family valid but space not found in that family */
        printf("Unknown %s space '%s'. Available %s spaces:\n",
            family_display_name(fam), argv[1], family_display_name(fam));
        family_list_spaces(fam);
        return NULL;
    }

    /* argv[0] is not a family name — try as bare space in prime-pair (backward compat) */
    const SpaceEntry* sp = family_space_lookup(FAMILY_PRIMEPAIR, argv[0]);
    if (sp) {
        *family_out = FAMILY_PRIMEPAIR;
        *tokens_consumed = 1;
        /* Soft compatibility hint (only once per session would be ideal, but keep it simple) */
        return sp;
    }

    /* Not found anywhere */
    printf("Unknown space '%s'. Available spaces:\n", argv[0]);
    printf("  Prime-Pair:\n"); family_list_spaces(FAMILY_PRIMEPAIR);
    printf("  SemiPrime:\n");  family_list_spaces(FAMILY_SEMIPRIME);
    return NULL;
}

/**
 * Fetch cells for any region type into a caller-provided buffer.
 * Returns cell count. Allocates *out_cells (caller must free).
 */
static size_t region_fetch(CPSSStream* qs, const RegionParams* rp,
                            IndexSpaceCell** out_cells) {
    size_t cap = 0u;
    size_t n = 0u;
    IndexSpaceCell* cells = NULL;

    switch (rp->type) {
    case REGION_POINT:
        cap = 1u;
        cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
        cells[0] = index_space_cell(qs, rp->a, rp->b);
        n = cells[0].valid ? 1u : 0u;
        break;
    case REGION_ROW:
        cap = (size_t)(rp->c - rp->b + 1u);
        if (cap > 10000u) { printf("Range too large.\n"); *out_cells = NULL; return 0u; }
        cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
        n = index_space_row_range(qs, rp->a, rp->b, rp->c, cells);
        break;
    case REGION_COL:
        cap = (size_t)(rp->c - rp->b + 1u);
        if (cap > 10000u) { printf("Range too large.\n"); *out_cells = NULL; return 0u; }
        cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
        n = index_space_col_range(qs, rp->a, rp->b, rp->c, cells);
        break;
    case REGION_RECT:
    case REGION_RECT_UNIQUE: {
        uint64_t rows = rp->b - rp->a + 1u, cols = rp->d - rp->c + 1u;
        cap = (size_t)(rows * cols);
        if (cap > 100000u) { printf("Range too large.\n"); *out_cells = NULL; return 0u; }
        cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
        n = index_space_rect(qs, rp->a, rp->b, rp->c, rp->d,
                             rp->type == REGION_RECT_UNIQUE, cells, cap);
        break;
    }
    case REGION_DIAGONAL:
        cap = (size_t)(rp->b - rp->a + 1u);
        cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
        n = index_space_diagonal(qs, rp->a, rp->b, cells);
        break;
    case REGION_BAND:
        cap = (size_t)(rp->b - rp->a + 1u);
        cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
        n = index_space_band(qs, rp->offset, rp->a, rp->b, cells);
        break;
    case REGION_TRIANGLE: {
        uint64_t side = rp->b - rp->a + 1u;
        cap = (size_t)(side * (side + 1u) / 2u);
        if (cap > 100000u) { printf("Range too large.\n"); *out_cells = NULL; return 0u; }
        cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
        n = index_space_triangle(qs, rp->a, rp->b, cells, cap);
        break;
    }
    case REGION_WINDOW: {
        uint64_t side = 2u * rp->c + 1u;
        cap = (size_t)(side * side);
        if (cap > 100000u) { printf("Range too large.\n"); *out_cells = NULL; return 0u; }
        cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
        n = index_space_window(qs, rp->a, rp->b, rp->c, cells, cap);
        break;
    }
    }
    *out_cells = cells;
    return n;
}

/** Format a region description for printing. */
static void region_print_header(const char* space_name, const RegionParams* rp) {
    switch (rp->type) {
    case REGION_POINT:       printf("%s point(%"PRIu64",%"PRIu64"):\n", space_name, rp->a, rp->b); break;
    case REGION_ROW:         printf("%s row(i=%"PRIu64", j=%"PRIu64"..%"PRIu64"):\n", space_name, rp->a, rp->b, rp->c); break;
    case REGION_COL:         printf("%s col(j=%"PRIu64", i=%"PRIu64"..%"PRIu64"):\n", space_name, rp->a, rp->b, rp->c); break;
    case REGION_RECT:        printf("%s rect [%"PRIu64"..%"PRIu64"] x [%"PRIu64"..%"PRIu64"]:\n", space_name, rp->a, rp->b, rp->c, rp->d); break;
    case REGION_RECT_UNIQUE: printf("%s rect-unique [%"PRIu64"..%"PRIu64"] x [%"PRIu64"..%"PRIu64"] (i<=j):\n", space_name, rp->a, rp->b, rp->c, rp->d); break;
    case REGION_DIAGONAL:    printf("%s diagonal [%"PRIu64"..%"PRIu64"]:\n", space_name, rp->a, rp->b); break;
    case REGION_BAND:        printf("%s band(offset=%"PRId64", i=%"PRIu64"..%"PRIu64"):\n", space_name, (long long)rp->offset, rp->a, rp->b); break;
    case REGION_TRIANGLE:    printf("%s triangle [%"PRIu64"..%"PRIu64"]:\n", space_name, rp->a, rp->b); break;
    case REGION_WINDOW:      printf("%s window(%"PRIu64",%"PRIu64" r=%"PRIu64"):\n", space_name, rp->a, rp->b, rp->c); break;
    }
}

/** Render pre-fetched cells through a space printer with header/footer. */
static void region_render(const char* space_name, const RegionParams* rp,
                           const IndexSpaceCell* cells, size_t n,
                           SpaceCellPrinter printer) {
    region_print_header(space_name, rp);
    for (size_t k = 0u; k < n; ++k) printer(&cells[k]);
    printf("--- %zu cells ---\n", n);
}

/**
 * Legacy bounded-view dispatch: wraps region_fetch + region_render for
 * the old-style <space>-row-range / -col-range / -rect / -rect-unique commands.
 * mode: 0=row, 1=col, 2=rect, 3=rect-unique
 */
static void bounded_view_dispatch(CPSSStream* qs, const char* space_name,
                                   int mode, uint64_t a, uint64_t b, uint64_t c_arg, uint64_t d,
                                   SpaceCellPrinter printer) {
    RegionParams rp;
    memset(&rp, 0, sizeof(rp));
    if (mode == 0) { rp.type = REGION_ROW; rp.a = a; rp.b = b; rp.c = c_arg; }
    else if (mode == 1) { rp.type = REGION_COL; rp.a = a; rp.b = b; rp.c = c_arg; }
    else if (mode == 3) { rp.type = REGION_RECT_UNIQUE; rp.a = a; rp.b = b; rp.c = c_arg; rp.d = d; }
    else { rp.type = REGION_RECT; rp.a = a; rp.b = b; rp.c = c_arg; rp.d = d; }

    IndexSpaceCell* cells = NULL;
    size_t n = region_fetch(qs, &rp, &cells);
    if (!cells) return;
    /* Build legacy header compatible with old output format */
    if (mode == 0) {
        uint64_t row_p = cpss_nth_prime(qs, a);
        printf("%s-row-range(i=%"PRIu64" [p=%"PRIu64"], j=%"PRIu64"..%"PRIu64"):\n", space_name, a, row_p, b, c_arg);
    } else if (mode == 1) {
        uint64_t col_q = cpss_nth_prime(qs, a);
        printf("%s-col-range(j=%"PRIu64" [q=%"PRIu64"], i=%"PRIu64"..%"PRIu64"):\n", space_name, a, col_q, b, c_arg);
    } else {
        bool unique = (mode == 3);
        printf("%s%s [%"PRIu64"..%"PRIu64"] x [%"PRIu64"..%"PRIu64"]%s:\n",
            space_name, unique ? "-rect-unique" : "-rect", a, b, c_arg, d,
            unique ? " (i<=j only)" : "");
    }
    for (size_t k = 0u; k < n; ++k) printer(&cells[k]);
    printf("--- %zu cells ---\n", n);
    free(cells);
}

/* Phase 3: stats, factor-explain, export — depends on pre-main infrastructure above */
#include "../SpaceAnalysis/primepair_analysis.c"
#include "../SemiPrimeSpaces/SemiPrimeBoundsSpace.c"
#include "../SemiPrimeSpaces/SemiPrimeRoutingSpace.c"
#include "../SemiPrimeSpaces/SemiPrimeCorpusSpace.c"
#include "../SemiPrimeSpaces/SemiPrimeCompetitionSpace.c"
#include "../SpaceAnalysis/semiprime_registry.c"

/**
 * Shared region parser: parse region type and args from app_argv starting at argv_start.
 * Returns true on success, fills rp. On failure, prints usage and returns false.
 */
static bool parse_region(char** argv, int argc, int argv_start, RegionParams* rp) {
    memset(rp, 0, sizeof(*rp));
    if (argv_start >= argc) return false;
    const char* region = argv[argv_start];
    int ra = argv_start + 1;
    if (strcmp(region, "point") == 0 && ra + 1 < argc) {
        rp->type = REGION_POINT; rp->a = parse_u64(argv[ra], "i"); rp->b = parse_u64(argv[ra+1], "j"); return true;
    } else if (strcmp(region, "row") == 0 && ra + 2 < argc) {
        rp->type = REGION_ROW; rp->a = parse_u64(argv[ra], "i"); rp->b = parse_u64(argv[ra+1], "j_lo"); rp->c = parse_u64(argv[ra+2], "j_hi"); return true;
    } else if (strcmp(region, "col") == 0 && ra + 2 < argc) {
        rp->type = REGION_COL; rp->a = parse_u64(argv[ra], "j"); rp->b = parse_u64(argv[ra+1], "i_lo"); rp->c = parse_u64(argv[ra+2], "i_hi"); return true;
    } else if (strcmp(region, "rect") == 0 && ra + 3 < argc) {
        rp->type = REGION_RECT; rp->a = parse_u64(argv[ra], "i_lo"); rp->b = parse_u64(argv[ra+1], "i_hi"); rp->c = parse_u64(argv[ra+2], "j_lo"); rp->d = parse_u64(argv[ra+3], "j_hi"); return true;
    } else if (strcmp(region, "rect-unique") == 0 && ra + 3 < argc) {
        rp->type = REGION_RECT_UNIQUE; rp->a = parse_u64(argv[ra], "i_lo"); rp->b = parse_u64(argv[ra+1], "i_hi"); rp->c = parse_u64(argv[ra+2], "j_lo"); rp->d = parse_u64(argv[ra+3], "j_hi"); return true;
    } else if (strcmp(region, "diagonal") == 0 && ra + 1 < argc) {
        rp->type = REGION_DIAGONAL; rp->a = parse_u64(argv[ra], "i_lo"); rp->b = parse_u64(argv[ra+1], "i_hi"); return true;
    } else if (strcmp(region, "band") == 0 && ra + 2 < argc) {
        rp->type = REGION_BAND; rp->offset = (int64_t)_strtoi64(argv[ra], NULL, 10); rp->a = parse_u64(argv[ra+1], "i_lo"); rp->b = parse_u64(argv[ra+2], "i_hi"); return true;
    } else if (strcmp(region, "triangle") == 0 && ra + 1 < argc) {
        rp->type = REGION_TRIANGLE; rp->a = parse_u64(argv[ra], "i_lo"); rp->b = parse_u64(argv[ra+1], "i_hi"); return true;
    } else if (strcmp(region, "window") == 0 && ra + 2 < argc) {
        rp->type = REGION_WINDOW; rp->a = parse_u64(argv[ra], "i"); rp->b = parse_u64(argv[ra+1], "j"); rp->c = parse_u64(argv[ra+2], "radius"); return true;
    }
    printf("Unknown region '%s' or insufficient args.\n", region);
    return false;
}

/** Count how many argv positions a region consumes (name + args). */
static int region_arg_count(const char* region) {
    if (strcmp(region, "point") == 0) return 3;
    if (strcmp(region, "row") == 0 || strcmp(region, "col") == 0 || strcmp(region, "band") == 0 ||
        strcmp(region, "window") == 0) return 4;
    if (strcmp(region, "rect") == 0 || strcmp(region, "rect-unique") == 0) return 5;
    if (strcmp(region, "diagonal") == 0 || strcmp(region, "triangle") == 0) return 3;
    return 0;
}

int main(int argc, char** argv) {
    init_popcount_table();
    init_wheel();
    init_presieve_primes();

    if (argc < 2) {
        print_usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "info") == 0) {
        return cmd_info(argc, argv);
    }
    if (strcmp(argv[1], "list") == 0) {
        return cmd_list(argc, argv);
    }
    if (strcmp(argv[1], "export-primes") == 0) {
        return cmd_export_primes(argc, argv);
    }
    if (strcmp(argv[1], "clip") == 0) {
        return cmd_clip(argc, argv);
    }
    if (strcmp(argv[1], "split-trillion") == 0) {
        return cmd_split_trillion(argc, argv);
    }
    if (strcmp(argv[1], "benchmark") == 0) {
        return cmd_benchmark(argc, argv);
    }
    if (strcmp(argv[1], "decompress-file") == 0) {
        return cmd_decompress_file(argc, argv);
    }
    if (strcmp(argv[1], "compress-file") == 0) {
        return cmd_compress_file(argc, argv);
    }

    if (strcmp(argv[1], "APP") == 0) {
        /* Parse APP-level options */
        size_t app_hot_cache_bytes = 0u;
        size_t app_db_ram_limit = 0u; /* 0 = no limit */
        for (int ai = 2; ai < argc; ++ai) {
            if (strcmp(argv[ai], "--wheel-cache") == 0 && ai + 1 < argc) {
                g_app_wheel_cache_bytes = parse_size_bytes(argv[++ai], "wheel-cache");
            }
            else if (strcmp(argv[ai], "--hot-cache") == 0 && ai + 1 < argc) {
                app_hot_cache_bytes = parse_size_bytes(argv[++ai], "hot-cache");
            }
            else if (strcmp(argv[ai], "--db-ram") == 0 && ai + 1 < argc) {
                app_db_ram_limit = parse_size_bytes(argv[++ai], "db-ram");
            }
            else if (strcmp(argv[ai], "--threads") == 0 && ai + 1 < argc) {
                g_app_threads = (uint32_t)parse_u64(argv[++ai], "threads");
                if (g_app_threads < 1u) g_app_threads = 1u;
            }
            else {
                fprintf(stderr, "error: unknown APP option: %s\n", argv[ai]);
                print_usage(stderr);
                return 1;
            }
        }

        /* Initialize caches */
        wheel_cache_init(g_app_wheel_cache_bytes);
        hot_cache_init(app_hot_cache_bytes);
        {
            char cb[64];
            printf("=== CPSS Prime Number Query Database - APP mode ===\n");
            if (g_app_wheel_cache_bytes > 0) {
                printf("wheel cache budget   = %s\n",
                    human_size((uint64_t)g_app_wheel_cache_bytes, cb, sizeof(cb)));
            }
            else {
                printf("wheel cache budget   = 0 (disabled)\n");
            }
            if (app_hot_cache_bytes > 0) {
                printf("hot segment cache    = %s\n",
                    human_size((uint64_t)app_hot_cache_bytes, cb, sizeof(cb)));
            }
            else {
                printf("hot segment cache    = 0 (is_prime will decompress each query)\n");
            }
            if (app_db_ram_limit > 0) {
                printf("db-ram limit         = %s\n",
                    human_size((uint64_t)app_db_ram_limit, cb, sizeof(cb)));
            }
            if (g_app_threads > 1u) {
                printf("threads              = %u\n", g_app_threads);
            }
            printf("Type 'help' for commands, 'load <file>' to open a CPSS stream.\n");
        }

        /* Persistent database state for query commands */
        enum { LOAD_BENCHMARK, LOAD_PERFORMANCE } app_load_mode = LOAD_PERFORMANCE;
        size_t app_cold_seg_start = 0u, app_cold_seg_end = 0u; /* segments with file data in RAM */
        size_t app_hot_seg_start = 0u, app_hot_seg_end = 0u;   /* segments in hot cache */

        CPSSStream app_db;
        bool app_db_open = false;
        memset(&app_db, 0, sizeof(app_db));
        app_db.file_handle = INVALID_HANDLE_VALUE;

        CPSSDatabase app_multi_db;
        bool app_multi_db_open = false;
        memset(&app_multi_db, 0, sizeof(app_multi_db));

        NqlVarStore nql_vars;
        nql_vars_init(&nql_vars);
        NqlUserFuncStore nql_ufuncs;
        nql_ufuncs_init(&nql_ufuncs);

        char line[MAX_LINE];

        while (1) {
            char* app_argv[MAX_ARGS];
            int app_argc = 0;

            printf("> ");
            fflush(stdout);

            if (!fgets(line, sizeof(line), stdin)) {
                break;
            }

            line[strcspn(line, "\r\n")] = '\0';

            if (line[0] == '\0') {
                continue;
            }

            if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
                break;
            }

            /* ── Hierarchical help system ── */
            if (strncmp(line, "help", 4) == 0 && (line[4] == '\0' || line[4] == ' ')) {
                const char* topic = (line[4] == ' ') ? line + 5 : "";
                while (*topic == ' ') ++topic;

                if (*topic == '\0') {
                    /* Top-level help: category index */
                    printf("CPSS APP - Command Categories\n"
                           "  help query       Prime queries (is-prime, next-prime, pi, ...)\n"
                           "  help factor      Factorisation commands\n"
                           "  help space       Space navigation (prime-pair / semiprime families)\n"
                           "  help semiprime   SemiPrime analysis (N-centric inference)\n"
                           "  help overlay     Overlay space commands (transitional)\n"
                           "  help stats       Region statistics & analysis\n"
                           "  help export      Export & batch commands\n"
                           "  help eval        Corpus generation & router evaluation\n"
                           "  help benchmark   Benchmark suite\n"
                           "  help db          Database loading, cache, sidecar, manifest\n"
                           "  help legacy      Legacy per-space commands (compatibility)\n"
                           "  help workflow    Recommended exploration workflow\n"
                           "  help nql         Number Query Language (SQL for numbers)\n"
                           "\nDiscoverability:\n"
                           "  space-list [family]          List spaces (prime-pair and semiprime)\n"
                           "  space-regions                List region types (Prime-Pair only)\n"
                           "  space-metrics [family] <name>  Metrics for any space\n");
                }
                else if (strcmp(topic, "query") == 0) {
                    printf("Prime Query Commands (require loaded DB):\n"
                           "  is-prime <n>                    Test if n is prime\n"
                           "  is-prime-bigint <n> [--rounds N] Probable-prime (no DB needed)\n"
                           "  next-prime <n>                  Smallest prime > n\n"
                           "  prev-prime <n>                  Largest prime < n\n"
                           "  pi <n>                          Count of primes <= n\n"
                           "  nth-prime <k>                   The k-th prime (1-indexed)\n"
                           "  prime-gap <n>                   Gap info at n\n"
                           "  count-range <lo> <hi>           Count primes in [lo, hi]\n"
                           "  list-range <lo> <hi> [limit]    List primes in [lo, hi]\n"
                           "  primes-near <n> [radius]        Primes near n\n"
                           "  nearest <n> <k>                 k nearest primes to n\n"
                           "  range-stats <lo> <hi>           Summary stats\n"
                           "  prime-iter <start> [count]      Stream forward\n"
                           "  prime-iter-prev <start> [count] Stream backward\n"
                           "  classify <n>                    Composite classification\n"
                           "  support-coverage <n>            DB coverage check\n"
                           "  segment-info <n>                Segment details\n"
                           "  is-prime-batch <n1> <n2> ...    Batch primality\n");
                }
                else if (strcmp(topic, "factor") == 0) {
                    printf("Factorisation Commands:\n"
                           "  factor <n>                      Auto-factor (tier-aware: u64/U128/BigNum)\n"
                           "  factor-bigint <n>               Force BigNum path\n"
                           "  factor-explain <N>              Explain routing with calibrated reasoning\n"
                           "    uint64: full env/routing/post-hoc analysis\n"
                           "    BigNum: observables, bounds, routing, factorisation attempt\n"
                           "  difference-bounds <N> [opts]    Factor-difference bounds (tier-aware)\n"
                           "    --auto                        Use cached evidence automatically\n"
                           "    --fermat-failed-steps K       Strengthen lower bound from K failed steps\n"
                           "    --min-factor-lower-bound L    Upper bound from p >= L\n"
                           "  factor-fermat <n> [--max-steps N]            Fermat (no DB)\n"
                           "  factor-rho <n> [--max-iterations N] [--seed N]  Pollard rho (no DB)\n"
                           "  factor-ecm <n> [--B1 N] [--curves N] [--seed N] [--affine]\n"
                           "                                  Montgomery ECM, Suyama curves (Stage 1, BigNum)\n"
                           "                                  --affine: legacy affine Weierstrass + Stage 2\n"
                           "  factor-squfof <n>               Shanks' SQUFOF (uint64, no DB)\n"
                           "  factor-qs <n> [--fb N] [--sieve N] [--polys N] [--rels N]\n"
                           "                [--lp-mult N] [--sp-warmup-polys N] [--siqs-only]\n"
                           "                                  SIQS with LP variation (uint64, no DB)\n"
                           "  factor-gnfs <n>                 GNFS (not yet implemented — scaffold only)\n"
                           "  factor-trial <n> [limit]        Trial division (DB)\n"
                           "  factor-pminus1 <n> [B1]         Pollard p-1 (DB)\n"
                           "  factor-pplus1 <n> [B1]          Williams p+1 (DB)\n"
                           "  smallest-factor <n>             Smallest prime factor\n"
                           "  smoothness-check <n> [bound]    B-smoothness test\n"
                           "  factor-batch <n1> <n2> ...      Batch factoring\n"
                           "\nBounds & Evidence:\n"
                           "  difference-bounds <N> [--auto] [--fermat-failed-steps K] [--min-factor-lower-bound L]\n"
                           "\nTerminology:\n"
                           "  ideal     = predicted best method assuming full DB\n"
                           "  available = predicted best given current environment\n"
                           "  actual    = method that won the factorisation\n"
                           "  Per-N evidence cache accumulates actual work history.\n"
                           "\nExamples:\n"
                           "  factor 143\n"
                           "  factor-explain 323\n"
                           "  difference-bounds 77 --auto\n"
                           "  difference-bounds 143 --min-factor-lower-bound 11\n");
                }
                else if (strcmp(topic, "space") == 0) {
                    printf("Space Navigation (family-qualified):\n"
                           "\n  Two navigation models:\n"
                           "\n  Prime-Pair (region-based, exact (p,q) geometry):\n"
                           "    space [prime-pair] <name> <region> <args...>\n"
                           "    Spaces: index, value, log, mean-imbalance, product-ratio, fermat, minmax\n"
                           "    Regions: point, row, col, rect, rect-unique, diagonal, band, triangle, window\n"
                           "    Bare space names default to prime-pair.\n"
                           "\n  SemiPrime (per-N report, N-centric inference):\n"
                           "    space semiprime <name> <N> [flags]\n"
                           "    Spaces: observation, coverage, bounds, routing\n"
                           "    These produce structured reports for a single N, not cell grids.\n"
                           "\n  Family aliases:\n"
                           "    prime-pair  (Prime-Pair, PrimePair, pair, pp)\n"
                           "    semiprime   (SemiPrime, semi-prime, semi, sp)\n"
                           "\nExamples:\n"
                           "  space index diagonal 4 8\n"
                           "  space prime-pair fermat triangle 4 6\n"
                           "  space semiprime observation 143\n"
                           "  space semiprime bounds 143 --auto\n"
                           "\nMore help:\n"
                           "  help space <name>     Details for a specific Prime-Pair space\n"
                           "  help space region     Region type reference\n"
                           "  space-list [family]   List spaces (optionally filtered by family)\n"
                           "  space-metrics [family] <name>  Metrics for a space\n");
                }
                else if (strcmp(topic, "space index") == 0) {
                    printf("Index Space (i, j) - Storage / Coverage / Bookkeeping\n"
                           "  Axes: 1-based prime indices. product(i,j) = prime(i) * prime(j)\n"
                           "  Uniform index steps map to non-uniform prime gaps.\n"
                           "\n  Metrics: i, j, p, q, N (U128), diagonal/off-diagonal counts\n"
                           "\n  Examples:\n"
                           "    space index point 4 5\n"
                           "    space index diagonal 1 6\n"
                           "    space index triangle 4 6\n"
                           "    space-stats index diagonal 4 100\n");
                }
                else if (strcmp(topic, "space value") == 0) {
                    printf("Value Space (p, q) - Literal Factor Geometry\n"
                           "  Axes: actual prime values. Near diagonal = balanced.\n"
                           "\n  Metrics: additive_distance, log_imbalance, balance_ratio, region\n"
                           "  Regions: near-square (ratio>=0.7), moderate (>=0.1), skewed (<0.1)\n"
                           "\n  Examples:\n"
                           "    space value band 1 4 8\n"
                           "    space-stats value triangle 4 20\n");
                }
                else if (strcmp(topic, "space log") == 0) {
                    printf("Log Space (u, v) - Geometric Base Layer\n"
                           "  u=log(p), v=log(q). Straightens multiplication into addition.\n"
                           "  Diagonal u=v = balanced. |u-v| = multiplicative imbalance.\n"
                           "\n  Metrics: u, v, u+v (=log N), |u-v|, normalised_imbalance\n"
                           "\n  Examples:\n"
                           "    space log diagonal 4 8\n"
                           "    space-stats log band 1 4 50\n");
                }
                else if (strcmp(topic, "space mean-imbalance") == 0) {
                    printf("Mean-Imbalance Space (m, delta) - Best Routing Geometry\n"
                           "  Rotated view on Log Space: m=log(N)/2, delta=(v-u)/2.\n"
                           "  For fixed N, m is known; all uncertainty is in delta.\n"
                           "\n  Metrics: m, delta, |delta|/m, bias (near-sq/moderate/unbal/skewed)\n"
                           "\n  Examples:\n"
                           "    space mean-imbalance triangle 4 6\n"
                           "    space-stats mean-imbalance rect-unique 4 20 4 20\n");
                }
                else if (strcmp(topic, "space product-ratio") == 0) {
                    printf("Product-Ratio Space (N, R) - Balance Routing\n"
                           "  R = max(p,q)/min(p,q). For fixed N, the unknown is R.\n"
                           "\n  Metrics: R, class (balanced R<2 / moderate R<100 / skewed R>=100)\n"
                           "\n  Examples:\n"
                           "    space product-ratio diagonal 4 8\n"
                           "    product-ratio-bounds 143\n");
                }
                else if (strcmp(topic, "space fermat") == 0) {
                    printf("Fermat Space (A, B) - Near-Square Targeting\n"
                           "  A=(p+q)/2, B=(q-p)/2. N = A^2 - B^2.\n"
                           "  B=0 => perfect square. B small => Fermat finds it fast.\n"
                           "\n  Metrics: A, B (exact or float), search_start, estimated_steps\n"
                           "\n  Extra commands:\n"
                           "    fermat-space-verify <N> <A> <B>   Verify identity\n"
                           "    fermat-space-start <N>            ceil(sqrt(N))\n"
                           "\n  Examples:\n"
                           "    space fermat window 5 5 1\n"
                           "    space-stats fermat triangle 4 20\n");
                }
                else if (strcmp(topic, "space minmax") == 0) {
                    printf("MinMax Space (s, l) - Small-Factor Coverage\n"
                           "  s=min(p,q), l=max(p,q). Upper triangle only.\n"
                           "  Every semiprime appears once. Natural for coverage visualisation.\n"
                           "\n  Metrics: s, l, coverage flags (cov13, cov100)\n"
                           "\n  Extra commands:\n"
                           "    minmax-coverage <limit> <N_max>   Count covered semiprimes\n"
                           "    minmax-uncovered <limit>          First uncovered boundary\n"
                           "\n  Examples:\n"
                           "    space minmax triangle 4 6\n"
                           "    space-stats minmax window 10 12 2\n");
                }
                else if (strcmp(topic, "space region") == 0) {
                    printf("Region Types for space / space-compare / space-stats / space-export:\n"
                           "\n  point <i> <j>                   Single cell\n"
                           "  row <i> <j_lo> <j_hi>           Row i, bounded columns\n"
                           "  col <j> <i_lo> <i_hi>           Column j, bounded rows\n"
                           "  rect <i_lo> <i_hi> <j_lo> <j_hi>  Full rectangle\n"
                           "  rect-unique <i_lo> <i_hi> <j_lo> <j_hi>  Upper triangle (i<=j)\n"
                           "  diagonal <i_lo> <i_hi>          (i,i) cells = perfect squares\n"
                           "  band <offset> <i_lo> <i_hi>     (i, i+offset) = consecutive-prime gaps\n"
                           "  triangle <i_lo> <i_hi>          All (i,j) with i<=j in range\n"
                           "  window <i> <j> <radius>         Square window centered at (i,j)\n");
                }
                else if (strcmp(topic, "space compare") == 0) {
                    printf("Comparison Commands:\n"
                           "  space-compare <region> <args> --spaces <comma-list>\n"
                           "    Fetch region once, render through multiple space views.\n"
                           "\n  space-stats-compare <region> <args> --spaces <comma-list>\n"
                           "    Aggregate stats from multiple spaces for same region.\n"
                           "\nExamples:\n"
                           "  space-compare diagonal 4 8 --spaces index,value,fermat\n"
                           "  space-stats-compare triangle 4 6 --spaces value,mean-imbalance,minmax\n");
                }
                else if (strcmp(topic, "space stats") == 0 || strcmp(topic, "stats") == 0) {
                    printf("Region Statistics & Analysis:\n"
                           "  space-stats <space> <region> <args>\n"
                           "    Aggregate stats: cell count, N range, mean, space-specific metrics.\n"
                           "\n  space-stats-compare <region> <args> --spaces <comma-list>\n"
                           "    Compare stats across multiple spaces for same region.\n"
                           "\n  factor-explain <N>\n"
                           "    Routing explanation: observable signals, coverage-sim, space intuition.\n"
                           "\nExamples:\n"
                           "  space-stats index diagonal 4 8\n"
                           "  space-stats value band 1 4 100\n"
                           "  space-stats fermat rect-unique 4 20 4 20\n"
                           "  space-stats-compare diagonal 4 20 --spaces index,value,product-ratio\n"
                           "  factor-explain 143\n");
                }
                else if (strcmp(topic, "semiprime") == 0) {
                    printf("SemiPrime Analysis (N-centric inference, factors may be unknown):\n"
                           "\n  Unified space access (per-N reports):\n"
                           "    space semiprime observation <N>     Observable features\n"
                           "    space semiprime coverage <N>        Method plausibility priors\n"
                           "    space semiprime bounds <N> [flags]  Factor-difference bounds\n"
                           "    space semiprime routing <N>         Method routing analysis\n"
                           "\n  Standalone commands (same functionality):\n"
                           "    observable <N>                  Observable feature vector\n"
                           "    coverage-sim <N>                Coverage space priors\n"
                           "    factor-explain <N>              Routing analysis\n"
                           "    difference-bounds <N> [opts]    Factor-shape bounds\n"
                           "\n  Evaluation tools:\n"
                           "    router-eval [opts]              Evaluate predictor (BigNum at --bits >62)\n"
                           "    method-compete [opts]           Method competition (BigNum at --bits >62)\n"
                           "    corpus-generate semiprime <out> Corpus CSV (BigNum at --bits >62)\n"
                           "    corpus-validate <file>          Validate corpus\n"
                           "\n  All commands accept uint64, U128, or BigNum inputs.\n"
                           "  space-metrics semiprime <name>   Per-space metric descriptions\n"
                           "  space-list semiprime             List SemiPrime spaces\n");
                }
                else if (strcmp(topic, "overlay") == 0) {
                    printf("Overlay Space Commands (transitional — migrating to Prime-Pair / SemiPrime):\n"
                           "  residue-classes <M>             Coprime residue classes mod M (pair-grid)\n"
                           "  residue-grid <N> <M>            N-driven elimination (SemiPrime)\n"
                           "  smoothness <p> [B]              Smoothness analysis for prime p\n"
                           "  observable <N>                  Observable feature vector (SemiPrime)\n"
                           "  coverage-sim <N>                Coverage space (SemiPrime)\n");
                }
                else if (strcmp(topic, "eval") == 0) {
                    printf("Corpus Generation & Router Evaluation:\n"
                           "  corpus-generate semiprime <outfile> [options]\n"
                           "    Generate semiprime corpus as CSV.\n"
                           "    --cases N    Number of cases (default 100)\n"
                           "    --bits B     Total bit size (default 32; >62 uses BigNum)\n"
                           "    --seed S     RNG seed (default 42)\n"
                           "    --shape      balanced|skewed|banded|mixed|rsa-like|\n"
                           "                 no-wheel-factor|pminus1-smooth|pminus1-hostile\n"
                           "                 (no-wheel-factor/pminus1-* only for uint64 corpus)\n"
                           "\n  router-eval [options]\n"
                           "    Evaluate predictor: ideal vs available vs actual winning method.\n"
                           "    Same --cases, --bits, --seed, --shape options.\n"
                           "    --csv-out <file>   Export per-case results to CSV.\n"
                           "    Reports: accuracy, confusion, method-region map.\n"
                           "    --bits >62: uses BigNum method-compete (no ideal/avail prediction).\n"
                           "    NOTE: results depend on DB state. Load DB first for DB-loaded validation.\n"
                           "\n  router-eval-file <input.csv> [--limit N] [--csv-out <file>]\n"
                           "    Evaluate predictor on a pre-generated corpus CSV.\n"
                           "    --csv-out <file>   Export per-case results to CSV.\n"
                           "\n  metric-eval [options]\n"
                           "    Grouped metric summaries across generated semiprimes.\n"
                           "    --group-by corpus|observed|winner|ideal|available  (default: winner)\n"
                           "    Reports: min/max/mean for balance_ratio, log_imb, R, Delta, etc.\n"
                           "\n  Terminology:\n"
                           "    ideal     = best method assuming full DB support\n"
                           "    available = best method given current environment\n"
                           "    actual    = method that actually won the factorisation\n"
                           "    corpus_shape  = generator family (e.g. no-wheel-factor)\n"
                           "    observed_shape = geometric classification from actual p,q\n"
                           "    Per-N evidence cache tracks actual work history.\n"
                           "    CSV exports include both corpus_shape and observed_shape.\n"
                           "\n  corpus-validate <input.csv> [--B1 N]\n"
                           "    Validate arithmetic properties of corpus against method bounds.\n"
                           "    Checks p-1/q-1/p+1/q+1 smoothness relative to B1 (default 1000000).\n"
                           "    Detects regime degeneracy (all factors trivially < B1).\n"
                           "    Reports graded hostility, label truth, per-row and summary stats.\n"
                           "    NOTE: 'pminus1-hostile' requires neither p+/-1 nor q+/-1 to be B1-smooth.\n"
                           "\nExamples:\n"
                           "  corpus-generate semiprime data.csv --cases 50 --bits 16 --seed 42 --shape balanced\n"
                           "  corpus-validate data.csv\n"
                           "  corpus-validate data.csv --B1 50\n"
                           "  router-eval --cases 20 --bits 16 --seed 42 --shape rsa-like\n"
                           "  metric-eval --cases 20 --shape no-wheel-factor --group-by corpus\n");
                }
                else if (strcmp(topic, "export") == 0) {
                    printf("Export Commands:\n"
                           "  space-export <space> <region> <args> <outfile>\n"
                           "    Export region cells to CSV. One row per cell.\n"
                           "    Columns: i, j, p, q, N, then space-specific derived columns.\n"
                           "\nExamples:\n"
                           "  space-export value triangle 4 20 values.csv\n"
                           "  space-export fermat band 1 4 30 fermat.csv\n"
                           "  space-export minmax rect-unique 4 20 4 20 coverage.csv\n"
                           "\nBatch Pipelines:\n"
                           "  query-file is-prime <in> <out> [--input-format ...] [--output-format ...]\n"
                           "  query-file next-prime|prev-prime|classify|count-range <in> <out> [...]\n"
                           "  factor-file <in> <out> [--input-format ...] [--output-format ...]\n");
                }
                else if (strcmp(topic, "benchmark") == 0) {
                    printf("Benchmark Commands:\n"
                           "  benchmark-everything [--count N] [--bits B] [--seed S] [--mode cold|hot] [--threads N]\n"
                           "  benchmark-fermat [--count N] [--bits B] [--seed S] [--max-steps N] [--threads N]\n"
                           "  benchmark-rho [--count N] [--bits B] [--seed S] [--max-iterations N] [--threads N]\n"
                           "  benchmark-ecm [--count N] [--bits B] [--seed S] [--B1 N] [--curves N] [--threads N]\n"
                           "  benchmark-is-prime [--count N] [--pattern P] [--seed S] [--mode cold|hot]\n"
                           "  benchmark-{count-range,list-range,mixed,next-prime,prev-prime,pi,\n"
                           "    nth-prime,prime-gap,primes-near,range-stats,factor-support,\n"
                           "    pminus1,pplus1,iter,factor-trial,factor-auto,classify,\n"
                           "    semiprime-factorisation} [options]\n"
                           "  benchmark-all-sectors [--start-seg N] [--end-seg N] [--mode hot|fair|auto]\n");
                }
                else if (strcmp(topic, "db") == 0) {
                    printf("Database & Infrastructure:\n"
                           "  load <file> [--seg-start N] [--seg-end M] [--mode benchmark|performance]\n"
                           "  load-db <dir> [--seg-start N] [--seg-end M] [--mode benchmark|performance]\n"
                           "  db-status                       Database info and cache stats\n"
                           "  preload [start] [end]           Warm wheel cache\n"
                           "  preload hot [start] [end]       Warm hot segment cache\n"
                           "  preload index                   Build prime count index\n"
                           "  cache-status / cache-reset      Cache management\n"
                           "\nSidecar Index:\n"
                           "  build-index <file>              Build .cpssi sidecar\n"
                           "  rebuild-index <file>            Force rebuild\n"
                           "  verify-index <file>             Verify sidecar\n"
                           "  build-index-db <dir>            Build for all shards\n"
                           "\nManifest:\n"
                           "  build-manifest <dir>            Build .cpss-manifest.json\n"
                           "  verify-manifest <dir>           Verify manifest\n"
                           "  db-coverage / db-gaps / db-overlaps\n");
                }
                else if (strcmp(topic, "legacy") == 0) {
                    printf("Legacy Commands (still work, prefer unified 'space' commands):\n"
                           "  <space>-row-range / -col-range / -rect / -rect-unique\n"
                           "  e.g.: value-space-row-range 4 4 6\n"
                           "        fermat-space-rect-unique 4 6 4 6\n"
                           "\n  Prefer instead:\n"
                           "    space value row 4 4 6\n"
                           "    space fermat rect-unique 4 6 4 6\n"
                           "\n  Single-point legacy commands (still useful directly):\n"
                           "    value-space <p> <q>\n"
                           "    log-space <p> <q>\n"
                           "    mean-imbalance <p> <q> / mean-imbalance-n <N>\n"
                           "    product-ratio <p> <q> / product-ratio-bounds <N>\n"
                           "    fermat-space <p> <q> / fermat-space-verify / fermat-space-start\n"
                           "    minmax-space <p> <q> / minmax-coverage / minmax-uncovered\n"
                           "    index-space <i> <j> / index-space-reverse / index-space-row / index-space-bounds\n");
                }
                else if (strcmp(topic, "workflow") == 0) {
                    printf("Recommended Hidden Factor Space Exploration Workflow:\n"
                           "\n  1. Load database:\n"
                           "       load <file>\n"
                           "\n  2. Explore a region across spaces:\n"
                           "       space index triangle 4 6\n"
                           "       space-compare triangle 4 6 --spaces value,fermat,minmax\n"
                           "\n  3. Get aggregate statistics:\n"
                           "       space-stats value triangle 4 20\n"
                           "       space-stats-compare diagonal 4 50 --spaces value,log,product-ratio\n"
                           "\n  4. Understand factorisation routing:\n"
                           "       factor-explain 143\n"
                           "       factor-explain 1000003\n"
                           "\n  5. Export for external analysis:\n"
                           "       space-export value triangle 4 20 data.csv\n"
                           "\n  6. Discover available tools:\n"
                           "       space-list\n"
                           "       space-regions\n"
                           "       space-metrics fermat\n");
                }
                else if (strcmp(topic, "nql") == 0) {
                    nql_print_help();
                }
                else {
                    printf("Unknown help topic '%s'. Try: help\n", topic);
                }
                continue;
            }

            /* ── Discoverability commands ── */
            if (strncmp(line, "space-list", 10) == 0 && (line[10] == '\0' || line[10] == ' ')) {
                const char* farg = (line[10] == ' ') ? line + 11 : NULL;
                while (farg && *farg == ' ') ++farg;
                if (farg && *farg != '\0') {
                    SpaceFamily fam = family_resolve(farg);
                    if (fam == FAMILY_UNKNOWN) {
                        printf("Unknown family '%s'. Use: prime-pair, semiprime\n", farg);
                    } else {
                        printf("%s spaces:\n", family_display_name(fam));
                        family_list_spaces(fam);
                    }
                } else {
                    for (int fi = 0; fi < FAMILY_COUNT; ++fi) {
                        printf("%s spaces:\n", family_display_name((SpaceFamily)fi));
                        family_list_spaces((SpaceFamily)fi);
                    }
                }
                printf("\nUse: space [prime-pair] <name> <region> <args>   (region navigation)\n"
                       "     space semiprime <name> <N> [flags]         (per-N reports)\n"
                       "     space-metrics [family] <name>              (metric descriptions)\n");
                continue;
            }
            if (strcmp(line, "space-regions") == 0) {
                printf("Available region types:\n"
                       "  point <i> <j>                   Single cell\n"
                       "  row <i> <j_lo> <j_hi>           Fixed row, bounded columns\n"
                       "  col <j> <i_lo> <i_hi>           Fixed column, bounded rows\n"
                       "  rect <i_lo> <i_hi> <j_lo> <j_hi>    Full rectangle\n"
                       "  rect-unique <...same...>             Upper triangle (i<=j)\n"
                       "  diagonal <i_lo> <i_hi>          (i,i) = perfect squares\n"
                       "  band <offset> <i_lo> <i_hi>     (i, i+offset)\n"
                       "  triangle <i_lo> <i_hi>          i<=j within range\n"
                       "  window <i> <j> <radius>         Centered square window\n"
                       "\nRegions apply to Prime-Pair spaces only.\n"
                       "SemiPrime spaces use: space semiprime <name> <N> [flags]\n");
                continue;
            }
            if (strcmp(line, "cache-status") == 0) {
                char cb[64];
                printf("wheel cache budget   = %s (%zu bytes)\n",
                    g_app_wheel_cache_bytes > 0
                    ? human_size((uint64_t)g_app_wheel_cache_bytes, cb, sizeof(cb))
                    : "0 (disabled)",
                    g_app_wheel_cache_bytes);
                printf("wheel cache used     = %zu bytes\n", wheel_cache_bytes_used());
                printf("wheel cache entries  = %zu\n", wheel_cache_entry_count());
                printf("hot cache budget     = %s (%zu bytes)\n",
                    g_hot_cache.budget > 0
                    ? human_size((uint64_t)g_hot_cache.budget, cb, sizeof(cb))
                    : "0 (disabled)",
                    g_hot_cache.budget);
                printf("hot cache used       = %zu bytes\n", g_hot_cache.bytes_used);
                printf("hot cache entries    = %zu\n", g_hot_cache.count);
                continue;
            }

            if (strcmp(line, "cache-reset") == 0) {
                wheel_cache_reset();
                hot_cache_reset();
                printf("wheel + hot caches cleared.\n");
                continue;
            }

            /* ── NQL query command (pass raw line, before strtok) ── */
            if (strncmp(line, "query", 5) == 0 && (line[5] == '\0' || line[5] == ' ')) {
                const char* nql_src = (line[5] == ' ') ? line + 6 : "";
                while (*nql_src == ' ') ++nql_src;
                CPSSStream* nql_db = app_db_open ? &app_db : (app_multi_db_open ? &app_multi_db.streams[0] : NULL);
                nql_execute(nql_src, nql_db, nql_db != NULL, &nql_vars, &nql_ufuncs);
                continue;
            }

            /* ── run-nql: execute an NQL script file line by line ── */
            if (strncmp(line, "run-nql ", 8) == 0) {
                const char* path = line + 8;
                while (*path == ' ') ++path;
                FILE* nql_fp = fopen(path, "r");
                if (!nql_fp) { printf("Cannot open NQL script: %s\n", path); continue; }
                printf("Running NQL script: %s\n", path);
                CPSSStream* nql_db = app_db_open ? &app_db : (app_multi_db_open ? &app_multi_db.streams[0] : NULL);
                /* Read line by line, accumulate multi-line blocks, execute when complete */
                char script_line[2048];
                char block_buf[65536];
                size_t block_len = 0u;
                int block_depth = 0; /* tracks IF/FOR/WHILE nesting */
                while (fgets(script_line, sizeof(script_line), nql_fp)) {
                    /* Strip trailing newline/CR */
                    size_t slen = strlen(script_line);
                    while (slen > 0u && (script_line[slen-1] == '\n' || script_line[slen-1] == '\r'))
                        script_line[--slen] = '\0';
                    /* Skip empty lines and comments */
                    const char* trimmed = script_line;
                    while (*trimmed == ' ' || *trimmed == '\t') ++trimmed;
                    if (*trimmed == '\0') continue;
                    if (trimmed[0] == '-' && trimmed[1] == '-') continue;
                    /* Check for block-opening keywords (case-insensitive) */
                    char upper[2048];
                    for (size_t ci = 0; ci <= slen && ci < sizeof(upper)-1; ++ci)
                        upper[ci] = (script_line[ci] >= 'a' && script_line[ci] <= 'z')
                            ? (char)(script_line[ci] - 32) : script_line[ci];
                    if (strstr(upper, "IF ") && strstr(upper, " THEN")) ++block_depth;
                    if (strstr(upper, "FOR ") && strstr(upper, " DO")) ++block_depth;
                    if (strstr(upper, "WHILE ") && strstr(upper, " DO")) ++block_depth;
                    /* Append to block buffer with semicolon separator */
                    if (block_len > 0u && block_len < sizeof(block_buf) - 2u) {
                        block_buf[block_len++] = ';';
                        block_buf[block_len++] = ' ';
                    }
                    size_t copy_len = slen;
                    if (block_len + copy_len >= sizeof(block_buf) - 1u)
                        copy_len = sizeof(block_buf) - 1u - block_len;
                    memcpy(block_buf + block_len, script_line, copy_len);
                    block_len += copy_len;
                    block_buf[block_len] = '\0';
                    /* Check for block-closing keywords */
                    if (strstr(upper, "END IF") || strstr(upper, "END FOR") || strstr(upper, "END WHILE")) {
                        if (block_depth > 0) --block_depth;
                    }
                    /* Execute when block is complete (depth == 0 and we have content) */
                    if (block_depth == 0 && block_len > 0u) {
                        nql_execute_ex(block_buf, nql_db, nql_db != NULL, &nql_vars, &nql_ufuncs);
                        block_len = 0u;
                        block_buf[0] = '\0';
                    }
                }
                /* Execute any remaining content */
                if (block_len > 0u) {
                    nql_execute_ex(block_buf, nql_db, nql_db != NULL, &nql_vars, &nql_ufuncs);
                }
                fclose(nql_fp);
                printf("Script complete.\n");
                continue;
            }

            /* Tokenize the line into argv-style array */
            app_argv[app_argc++] = "cpss_viewer";
            {
                char* context = NULL;
                char* token = strtok_s(line, " ", &context);
                while (token != NULL && app_argc < MAX_ARGS) {
                    app_argv[app_argc++] = token;
                    token = strtok_s(NULL, " ", &context);
                }
            }

            if (app_argc < 2) continue;

            /* ── Query Database commands ── */

            if (strcmp(app_argv[1], "load") == 0) {
                if (app_argc < 3) {
                    printf("Usage: load <cpss_file> [--seg-start N] [--seg-end M] [--mode benchmark|performance]\n");
                    continue;
                }
                /* Parse optional --seg-start / --seg-end / --mode */
                size_t seg_start = 0u;
                size_t seg_end = SIZE_MAX;
                app_load_mode = LOAD_PERFORMANCE; /* default */
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--seg-start") == 0 && ai + 1 < app_argc) {
                        seg_start = (size_t)parse_u64(app_argv[++ai], "seg-start");
                    }
                    else if (strcmp(app_argv[ai], "--seg-end") == 0 && ai + 1 < app_argc) {
                        seg_end = (size_t)parse_u64(app_argv[++ai], "seg-end");
                    }
                    else if (strcmp(app_argv[ai], "--mode") == 0 && ai + 1 < app_argc) {
                        ++ai;
                        if (strcmp(app_argv[ai], "benchmark") == 0) app_load_mode = LOAD_BENCHMARK;
                        else if (strcmp(app_argv[ai], "performance") == 0) app_load_mode = LOAD_PERFORMANCE;
                        else { printf("Unknown mode '%s' (use benchmark or performance)\n", app_argv[ai]); continue; }
                    }
                }

                /* Close any previous database */
                if (app_multi_db_open) { cpss_db_close(&app_multi_db); app_multi_db_open = false; }
                if (app_db_open) { cpss_close(&app_db); app_db_open = false; }

                double t0 = now_sec();
                cpss_open(&app_db, app_argv[2]);
                cpss_build_segment_index(&app_db);
                double t1 = now_sec();
                app_db_open = true;

                /* Clamp seg_end to valid range */
                if (app_db.index_len > 0u && seg_end >= app_db.index_len) {
                    seg_end = app_db.index_len - 1u;
                }

                uint64_t first_n = 0u, last_n = 0u;
                if (app_db.index_len > 0u) {
                    first_n = segment_start_n(&app_db.index_entries[0]);
                    last_n = segment_end_n(&app_db.index_entries[app_db.index_len - 1u]);
                }
                char cb[64];
                printf("loaded single stream: %s (%.2fs)\n", app_argv[2], t1 - t0);
                printf("  segments           = %zu\n", app_db.index_len);
                printf("  range              = %" PRIu64 " .. %" PRIu64 "\n", first_n, last_n);

                /* Compute total compressed and decompressed sizes */
                uint64_t total_compressed = (uint64_t)app_db.file_size;
                uint64_t total_decompressed = 0u;
                for (size_t i = 0u; i < app_db.index_len; ++i) {
                    total_decompressed += app_db.index_entries[i].raw_size;
                }

                printf("  total file size    = %s\n", human_size(total_compressed, cb, sizeof(cb)));
                printf("  total decompressed = %s\n", human_size(total_decompressed, cb, sizeof(cb)));

                /*
                 * db-ram: how many segments we can keep cold-loaded from the binary file.
                 * This is the RAM cost of the memory-mapped file data, NOT the hot cache.
                 * If db-ram < file size, only a subset of segments are loaded.
                 */
                size_t cold_segs = 0u;
                uint64_t cold_bytes = 0u;
                size_t cold_limit = app_db.index_len; /* default: all segments */
                if (app_db_ram_limit > 0 && total_compressed > (uint64_t)app_db_ram_limit) {
                    /* Compute how many segments fit in db-ram by file size */
                    for (size_t i = seg_start; i <= seg_end && i < app_db.index_len; ++i) {
                        const SegmentIndexEntry* e = &app_db.index_entries[i];
                        uint64_t seg_file_cost = (uint64_t)e->compressed_size + 4u; /* 4-byte length prefix */
                        if (cold_bytes + seg_file_cost > (uint64_t)app_db_ram_limit) break;
                        cold_bytes += seg_file_cost;
                        ++cold_segs;
                    }
                    cold_limit = cold_segs;
                }
                else {
                    /* File fits entirely within db-ram (or no limit set) */
                    cold_segs = (seg_end >= seg_start) ? seg_end - seg_start + 1u : 0u;
                    if (cold_segs > app_db.index_len) cold_segs = app_db.index_len;
                    cold_bytes = total_compressed;
                    cold_limit = cold_segs;
                }
                size_t cold_end = (cold_limit > 0u) ? seg_start + cold_limit - 1u : seg_start;
                if (cold_end > seg_end) cold_end = seg_end;
                app_cold_seg_start = seg_start;
                app_cold_seg_end = cold_end;

                size_t total_requested = (seg_end >= seg_start) ? seg_end - seg_start + 1u : 0u;
                size_t cold_overflow = total_requested > cold_limit ? total_requested - cold_limit : 0u;

                if (app_db_ram_limit > 0) {
                    printf("  db-ram limit       = %s\n", human_size((uint64_t)app_db_ram_limit, cb, sizeof(cb)));
                    printf("  cold-loaded        = %zu of %zu segments [%zu..%zu] (%s of file)\n",
                        cold_limit, total_requested, seg_start, cold_end,
                        human_size(cold_bytes, cb, sizeof(cb)));
                    if (cold_overflow > 0u) {
                        printf("  not loaded         = %zu segments (exceed db-ram limit)\n", cold_overflow);
                    }
                }
                else {
                    printf("  cold-loaded        = all %zu segments (no db-ram limit)\n", cold_limit);
                }

                printf("  load mode          = %s\n",
                    app_load_mode == LOAD_BENCHMARK ? "benchmark (file stays mapped)" : "performance (file unmapped after preload)");

                /* Pre-estimate how many cold-loaded segments fit in the hot cache budget */
                size_t hot_est_segs = 0u;
                size_t hot_est_bytes = 0u;
                if (g_hot_cache.budget > 0) {
                    size_t hot_budget = g_hot_cache.budget - g_hot_cache.bytes_used;
                    for (size_t i = seg_start; i <= cold_end && i < app_db.index_len; ++i) {
                        const SegmentIndexEntry* e = &app_db.index_entries[i];
                        /* presieve bitmap: 1 bit per candidate, rounded to bytes */
                        size_t ps = (size_t)((e->total_candidates + 7u) / 8u);
                        /* rank index: one uint32 prefix sum per RANK_BLOCK_SIZE candidates, plus sentinel */
                        size_t rk = (((size_t)e->total_candidates + RANK_BLOCK_SIZE - 1u) / RANK_BLOCK_SIZE + 1u) * sizeof(uint32_t);
                        /* total hot entry cost = presieve + result_bits + rank index */
                        size_t cost = ps + e->result_bits_len + rk;
                        if (hot_est_bytes + cost > hot_budget) break;
                        hot_est_bytes += cost;
                        ++hot_est_segs;
                    }
                }

                printf("  hot cache budget   = %s\n",
                    g_hot_cache.budget > 0 ? human_size((uint64_t)g_hot_cache.budget, cb, sizeof(cb)) : "0 (disabled)");
                if (g_hot_cache.budget > 0) {
                    printf("  hot estimate       = %zu of %zu cold segments fit (%s)\n",
                        hot_est_segs, cold_limit,
                        human_size((uint64_t)hot_est_bytes, cb, sizeof(cb)));
                }

                /* Auto-preload hot cache for cold-loaded segments */
                if (g_hot_cache.budget > 0 && cold_limit > 0u && hot_est_segs > 0u) {
                    size_t hot_end = seg_start + hot_est_segs - 1u;
                    if (hot_end > cold_end) hot_end = cold_end;
                    app_hot_seg_start = seg_start;
                    app_hot_seg_end = hot_end;

                    printf("\npreloading hot segment cache [%zu..%zu]...\n", seg_start, hot_end);
                    double pt0 = now_sec();
                    size_t cached = cpss_preload_hot(&app_db, seg_start, hot_end);
                    double pt1 = now_sec();
                    printf("  loaded into hot cache = %zu segments (%.2fs)\n", cached, pt1 - pt0);
                    printf("  hot cache used        = %s / %s\n",
                        human_size((uint64_t)g_hot_cache.bytes_used, cb, sizeof(cb)),
                        human_size((uint64_t)g_hot_cache.budget, cb, sizeof(cb)));
                }

                /* Performance mode: unmap file to free binary file RAM */
                if (app_load_mode == LOAD_PERFORMANCE) {
                    cpss_unmap(&app_db);
                    printf("  file unmapped (performance mode, %s freed)\n",
                        human_size(total_compressed, cb, sizeof(cb)));
                }
                else {
                    printf("  file stays mapped (benchmark mode, cold path available for all %zu cold segments)\n",
                        cold_limit);
                }
                continue;
            }

            if (strcmp(app_argv[1], "load-db") == 0) {
                if (app_argc < 3) {
                    printf("Usage: load-db <shard_directory> [--seg-start N] [--seg-end M] [--mode benchmark|performance]\n");
                    continue;
                }
                /* Parse optional --seg-start / --seg-end / --mode */
                size_t seg_start = 0u;
                size_t seg_end = SIZE_MAX;
                app_load_mode = LOAD_PERFORMANCE;
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--seg-start") == 0 && ai + 1 < app_argc) {
                        seg_start = (size_t)parse_u64(app_argv[++ai], "seg-start");
                    }
                    else if (strcmp(app_argv[ai], "--seg-end") == 0 && ai + 1 < app_argc) {
                        seg_end = (size_t)parse_u64(app_argv[++ai], "seg-end");
                    }
                    else if (strcmp(app_argv[ai], "--mode") == 0 && ai + 1 < app_argc) {
                        ++ai;
                        if (strcmp(app_argv[ai], "benchmark") == 0) app_load_mode = LOAD_BENCHMARK;
                        else if (strcmp(app_argv[ai], "performance") == 0) app_load_mode = LOAD_PERFORMANCE;
                        else { printf("Unknown mode '%s' (use benchmark or performance)\n", app_argv[ai]); continue; }
                    }
                }

                /* Close any previous database */
                if (app_multi_db_open) { cpss_db_close(&app_multi_db); app_multi_db_open = false; }
                if (app_db_open) { cpss_close(&app_db); app_db_open = false; }

                double t0 = now_sec();
                int rc = cpss_db_open(&app_multi_db, app_argv[2]);
                double t1 = now_sec();
                if (rc != 0) {
                    printf("Failed to open shard database.\n");
                    continue;
                }
                app_multi_db_open = true;

                /* Clamp seg_end to valid range */
                if (app_multi_db.total_segments > 0u && seg_end >= app_multi_db.total_segments) {
                    seg_end = app_multi_db.total_segments - 1u;
                }

                char cb[64];
                printf("loaded multi-shard database: %s (%.2fs)\n", app_argv[2], t1 - t0);
                printf("  shards             = %zu\n", app_multi_db.shard_count);
                printf("  total segments     = %zu\n", app_multi_db.total_segments);
                printf("  range              = %" PRIu64 " .. %" PRIu64 "\n",
                    app_multi_db.db_lo, app_multi_db.db_hi);

                /* Compute total compressed and decompressed sizes */
                uint64_t total_compressed = 0u;
                uint64_t total_decompressed = 0u;
                for (size_t si = 0u; si < app_multi_db.shard_count; ++si) {
                    CPSSStream* s = &app_multi_db.streams[si];
                    total_compressed += s->file_size;
                    for (size_t i = 0u; i < s->index_len; ++i) {
                        total_decompressed += s->index_entries[i].raw_size;
                    }
                }

                /* Count decomp-resident segments across all shards */
                size_t decomp_resident = 0u;
                for (size_t si = 0u; si < app_multi_db.shard_count; ++si) {
                    CPSSStream* s = &app_multi_db.streams[si];
                    for (size_t i = 0u; i < s->index_len; ++i) {
                        const SegmentIndexEntry* e = &s->index_entries[i];
                        if (e->raw_size < (e->compressed_size * 12u) / 10u) ++decomp_resident;
                    }
                }

                printf("  total compressed   = %s\n", human_size(total_compressed, cb, sizeof(cb)));
                printf("  total decompressed = %s\n", human_size(total_decompressed, cb, sizeof(cb)));
                printf("  decomp-resident    = %zu of %zu segments (< 1.2x compressed)\n",
                    decomp_resident, app_multi_db.total_segments);

                /* Pre-estimate how many segments fit in the budget */
                size_t est_budget = g_hot_cache.budget > 0 ? g_hot_cache.budget - g_hot_cache.bytes_used : 0u;
                size_t est_segs = 0u;
                size_t est_bytes = 0u;
                size_t total_requested = (seg_end >= seg_start) ? seg_end - seg_start + 1u : 0u;
                {
                    size_t global_idx = 0u;
                    bool est_done = false;
                    for (size_t si = 0u; si < app_multi_db.shard_count && !est_done; ++si) {
                        CPSSStream* s = &app_multi_db.streams[si];
                        for (size_t i = 0u; i < s->index_len && !est_done; ++i, ++global_idx) {
                            if (global_idx < seg_start) continue;
                            if (global_idx > seg_end) { est_done = true; break; }
                            const SegmentIndexEntry* e = &s->index_entries[i];
                            size_t ps = (size_t)((e->total_candidates + 7u) / 8u);
                            size_t rk = (((size_t)e->total_candidates + RANK_BLOCK_SIZE - 1u) / RANK_BLOCK_SIZE + 1u) * sizeof(uint32_t);
                            size_t cost = ps + e->result_bits_len + rk;
                            if (est_bytes + cost > est_budget) { est_done = true; break; }
                            est_bytes += cost;
                            ++est_segs;
                        }
                    }
                }
                size_t overflow_segs = total_requested > est_segs ? total_requested - est_segs : 0u;

                printf("  hot cache budget   = %s\n",
                    g_hot_cache.budget > 0 ? human_size((uint64_t)g_hot_cache.budget, cb, sizeof(cb)) : "0 (disabled)");
                if (app_db_ram_limit > 0) {
                    printf("  db-ram limit       = %s (allows ~%zu hot segments)\n",
                        human_size((uint64_t)app_db_ram_limit, cb, sizeof(cb)), est_segs);
                }
                printf("  hot estimate       = %zu of %zu segments fit (%s), %zu overflow\n",
                    est_segs, total_requested,
                    human_size((uint64_t)est_bytes, cb, sizeof(cb)), overflow_segs);

                for (size_t si = 0u; si < app_multi_db.shard_count; ++si) {
                    printf("  shard[%zu]           = %" PRIu64 "..%" PRIu64 " (%zu segs) %s\n",
                        si, app_multi_db.shard_lo[si], app_multi_db.shard_hi[si],
                        app_multi_db.streams[si].index_len,
                        human_size((uint64_t)app_multi_db.streams[si].file_size, cb, sizeof(cb)));
                }

                printf("  load mode          = %s\n",
                    app_load_mode == LOAD_BENCHMARK ? "benchmark (files stay mapped)" : "performance (files unmapped after preload)");

                /* Auto-preload hot cache if enabled */
                if (g_hot_cache.budget > 0 && app_multi_db.total_segments > 0u && est_segs > 0u) {
                    size_t effective_end = seg_start + est_segs - 1u;
                    if (effective_end > seg_end) effective_end = seg_end;
                    app_hot_seg_start = seg_start;
                    app_hot_seg_end = effective_end;

                    printf("\npreloading hot segment cache [%zu..%zu]...\n", seg_start, effective_end);
                    double pt0 = now_sec();
                    size_t cached = cpss_db_preload_hot(&app_multi_db, seg_start, effective_end);
                    double pt1 = now_sec();
                    printf("  loaded into hot cache = %zu segments (%.2fs)\n", cached, pt1 - pt0);
                    printf("  hot cache used        = %s / %s\n",
                        human_size((uint64_t)g_hot_cache.bytes_used, cb, sizeof(cb)),
                        human_size((uint64_t)g_hot_cache.budget, cb, sizeof(cb)));

                    /* Performance mode: unmap all shard files to free RAM */
                    if (app_load_mode == LOAD_PERFORMANCE) {
                        for (size_t si = 0u; si < app_multi_db.shard_count; ++si) {
                            cpss_unmap(&app_multi_db.streams[si]);
                        }
                        printf("  files unmapped (performance mode, %s freed)\n",
                            human_size(total_compressed, cb, sizeof(cb)));
                        printf("  only hot-cached segments [%zu..%zu] queryable\n",
                            seg_start, effective_end);
                    }
                    else {
                        printf("  files stay mapped (benchmark mode, cold path available)\n");
                    }
                }

                continue;
            }

            if (strcmp(app_argv[1], "db-status") == 0) {
                char cb[64];
                if (app_multi_db_open || app_db_open) {
                    size_t total_segs = app_multi_db_open ? app_multi_db.total_segments : app_db.index_len;
                    bool file_mapped = app_multi_db_open
                        ? (app_multi_db.shard_count > 0 && app_multi_db.streams[0].map != NULL)
                        : (app_db.map != NULL);

                    printf("mode                 = %s\n", app_multi_db_open ? "multi-shard" : "single stream");
                    printf("load mode            = %s\n",
                        app_load_mode == LOAD_BENCHMARK ? "benchmark" : "performance");
                    if (!app_multi_db_open && app_db.path) {
                        printf("file                 = %s\n", app_db.path);
                    }
                    printf("total segments       = %zu\n", total_segs);
                    if (app_multi_db_open) {
                        uint64_t first_n = app_multi_db.db_lo;
                        uint64_t last_n = app_multi_db.db_hi;
                        printf("range                = %" PRIu64 " .. %" PRIu64 "\n", first_n, last_n);
                        printf("shards               = %zu\n", app_multi_db.shard_count);
                    }
                    else if (app_db.index_len > 0u) {
                        uint64_t first_n = segment_start_n(&app_db.index_entries[0]);
                        uint64_t last_n = segment_end_n(&app_db.index_entries[app_db.index_len - 1u]);
                        printf("range                = %" PRIu64 " .. %" PRIu64 "\n", first_n, last_n);
                        printf("file size            = %s\n", human_size((uint64_t)app_db.file_size, cb, sizeof(cb)));
                    }

                    /* Cold-loaded segments (binary file data in RAM) */
                    size_t cold_count = (app_cold_seg_end >= app_cold_seg_start)
                        ? app_cold_seg_end - app_cold_seg_start + 1u : 0u;
                    printf("cold-loaded          = [%zu..%zu] (%zu of %zu segments)\n",
                        app_cold_seg_start, app_cold_seg_end, cold_count, total_segs);
                    printf("file mapped          = %s\n",
                        file_mapped ? "yes (cold queries available)" : "no (unmapped, performance mode)");
                    if (!file_mapped && cold_count < total_segs) {
                        printf("not loaded           = %zu segments (outside db-ram or unmapped)\n",
                            total_segs - cold_count);
                    }

                    /* Hot cache (derived data: presieve + result_bits + rank) */
                    printf("hot cache            = %s / %s (%zu entries)\n",
                        human_size((uint64_t)g_hot_cache.bytes_used, cb, sizeof(cb)),
                        human_size((uint64_t)g_hot_cache.budget, cb, sizeof(cb)),
                        g_hot_cache.count);
                    if (g_hot_cache.count > 0u) {
                        printf("hot segments         = [%zu..%zu] (%zu of %zu cold)\n",
                            app_hot_seg_start, app_hot_seg_end,
                            g_hot_cache.count, cold_count);
                        if (file_mapped && cold_count > g_hot_cache.count) {
                            printf("cold-only segments   = %zu (require on-demand decompress from file)\n",
                                cold_count - g_hot_cache.count);
                        }
                    }
                    printf("wheel cache          = %s / %s (%zu entries)\n",
                        human_size((uint64_t)wheel_cache_bytes_used(), cb, sizeof(cb)),
                        g_app_wheel_cache_bytes > 0
                        ? human_size((uint64_t)g_app_wheel_cache_bytes, cb, sizeof(cb))
                        : "0",
                        wheel_cache_entry_count());
                }
                else {
                    printf("No database loaded. Use 'load <file>' or 'load-db <dir>'.\n");
                }
                continue;
            }

            if (strcmp(app_argv[1], "preload") == 0) {
                if (!app_db_open && !app_multi_db_open) {
                    printf("No database loaded. Use 'load <file>' or 'load-db <dir>'.\n");
                    continue;
                }

                /* Determine preload mode: plain, hot, or index */
                bool preload_hot_mode = false;
                bool preload_index_mode = false;
                int arg_offset = 2; /* first non-mode argument position */

                if (app_argc >= 3 && strcmp(app_argv[2], "hot") == 0) {
                    preload_hot_mode = true;
                    arg_offset = 3;
                }
                else if (app_argc >= 3 && strcmp(app_argv[2], "index") == 0) {
                    preload_index_mode = true;
                }

                if (preload_index_mode) {
                    /* Build per-segment prime count index */
                    printf("building per-segment prime count index...\n");
                    double t0 = now_sec();
                    if (app_multi_db_open) {
                        for (size_t si = 0u; si < app_multi_db.shard_count; ++si) {
                            cpss_build_prime_index(&app_multi_db.streams[si]);
                        }
                    }
                    else {
                        cpss_build_prime_index(&app_db);
                    }
                    double t1 = now_sec();
                    printf("  done in %.2fs\n", t1 - t0);
                    continue;
                }

                if (preload_hot_mode && g_hot_cache.budget == 0u) {
                    printf("Hot cache is disabled (budget = 0). Use --hot-cache at startup.\n");
                    continue;
                }
                if (!preload_hot_mode && g_app_wheel_cache_bytes == 0u) {
                    printf("Wheel cache is disabled (budget = 0). Use --wheel-cache at startup.\n");
                    continue;
                }

                /* Parse optional start_seg / end_seg */
                size_t p_start = 0u;
                size_t p_end = SIZE_MAX;
                if (app_argc > arg_offset) {
                    p_start = (size_t)parse_u64(app_argv[arg_offset], "start_seg");
                }
                if (app_argc > arg_offset + 1) {
                    p_end = (size_t)parse_u64(app_argv[arg_offset + 1], "end_seg");
                }

                size_t total_segs = app_multi_db_open ? app_multi_db.total_segments : app_db.index_len;
                const char* cache_name = preload_hot_mode ? "hot" : "wheel";

                if (p_end == SIZE_MAX && p_start == 0u) {
                    printf("preloading %s cache for all %zu segments...\n", cache_name, total_segs);
                }
                else {
                    printf("preloading %s cache for segments %zu..%zu...\n", cache_name,
                        p_start, p_end == SIZE_MAX ? total_segs - 1u : p_end);
                }

                double t0 = now_sec();
                size_t cached = 0u;
                if (preload_hot_mode) {
                    cached = app_multi_db_open
                        ? cpss_db_preload_hot(&app_multi_db, p_start, p_end)
                        : cpss_preload_hot(&app_db, p_start, p_end);
                }
                else {
                    cached = app_multi_db_open
                        ? cpss_db_preload(&app_multi_db, p_start, p_end)
                        : cpss_preload_cache(&app_db, p_start, p_end);
                }
                double t1 = now_sec();
                char cb[64];
                printf("  cached             = %zu segments\n", cached);
                if (preload_hot_mode) {
                    printf("  hot cache used     = %s\n", human_size((uint64_t)g_hot_cache.bytes_used, cb, sizeof(cb)));
                    printf("  hot cache entries  = %zu\n", g_hot_cache.count);
                }
                else {
                    printf("  wheel cache used   = %s\n", human_size((uint64_t)wheel_cache_bytes_used(), cb, sizeof(cb)));
                    printf("  wheel cache entries= %zu\n", wheel_cache_entry_count());
                }
                printf("  time               = %.2fs\n", t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "is-prime-bigint") == 0) {
                if (app_argc < 3) {
                    printf("Usage: is-prime-bigint <n> [--rounds N]\n");
                    continue;
                }

                unsigned rounds = 12u;
                bool bad_opt = false;
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--rounds") == 0 && ai + 1 < app_argc) {
                        rounds = (unsigned)parse_u64(app_argv[++ai], "rounds");
                    }
                    else {
                        printf("Unknown option for is-prime-bigint: %s\n", app_argv[ai]);
                        bad_opt = true;
                        break;
                    }
                }
                if (bad_opt) continue;

                BigNum bn = parse_bignum(app_argv[2], "n");
                char nbuf[512];
                bn_to_str(&bn, nbuf, sizeof(nbuf));

                double t0 = now_sec();
                bool prime = bn_is_probable_prime(&bn, rounds);
                double t1 = now_sec();

                if (bn_fits_u128(&bn)) {
                    printf("%s is %s (%.6fs)\n", nbuf, prime ? "PRIME" : "NOT prime", t1 - t0);
                }
                else {
                    printf("%s is %s (%u Miller-Rabin rounds, %.6fs)\n",
                        nbuf, prime ? "PROBABLY PRIME" : "COMPOSITE", rounds, t1 - t0);
                }
                continue;
            }

            if (strcmp(app_argv[1], "is-prime") == 0) {
                if (app_argc < 3) {
                    printf("Usage: is-prime <n>\n");
                    continue;
                }
                if (!app_db_open && !app_multi_db_open) {
                    printf("No database loaded. Use 'load <file>' or 'load-db <dir>'.\n");
                    continue;
                }
                uint64_t n = parse_u64(app_argv[2], "n");
                double t0 = now_sec();
                int result = app_multi_db_open
                    ? cpss_db_is_prime(&app_multi_db, n)
                    : cpss_is_prime(&app_db, n);
                double t1 = now_sec();
                if (result == 1) {
                    printf("%" PRIu64 " is PRIME (%.6fs)\n", n, t1 - t0);
                }
                else if (result == 0) {
                    printf("%" PRIu64 " is NOT prime (%.6fs)\n", n, t1 - t0);
                }
                else {
                    printf("%" PRIu64 " is OUTSIDE the loaded database range\n", n);
                }
                continue;
            }

            if (strcmp(app_argv[1], "count-range") == 0) {
                if (app_argc < 4) {
                    printf("Usage: count-range <lo> <hi>\n");
                    continue;
                }
                if (!app_db_open && !app_multi_db_open) {
                    printf("No database loaded. Use 'load <file>' or 'load-db <dir>'.\n");
                    continue;
                }
                uint64_t lo = parse_u64(app_argv[2], "lo");
                uint64_t hi = parse_u64(app_argv[3], "hi");
                double t0 = now_sec();
                uint64_t count = app_multi_db_open
                    ? cpss_db_count_range(&app_multi_db, lo, hi)
                    : cpss_count_range_hot(&app_db, lo, hi);
                double t1 = now_sec();
                printf("primes in [%" PRIu64 ", %" PRIu64 "] = %" PRIu64 " (%.6fs)\n",
                    lo, hi, count, t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "list-range") == 0) {
                if (app_argc < 4) {
                    printf("Usage: list-range <lo> <hi> [limit]\n");
                    continue;
                }
                if (!app_db_open && !app_multi_db_open) {
                    printf("No database loaded. Use 'load <file>' or 'load-db <dir>'.\n");
                    continue;
                }
                uint64_t lo = parse_u64(app_argv[2], "lo");
                uint64_t hi = parse_u64(app_argv[3], "hi");
                size_t limit = 100u; /* default */
                if (app_argc >= 5) {
                    limit = (size_t)parse_u64(app_argv[4], "limit");
                }

                uint64_t* buf = (uint64_t*)xmalloc(limit * sizeof(uint64_t));
                double t0 = now_sec();
                size_t found = app_multi_db_open
                    ? cpss_db_list_range(&app_multi_db, lo, hi, buf, limit)
                    : cpss_list_range_hot(&app_db, lo, hi, buf, limit);
                double t1 = now_sec();

                for (size_t pi = 0u; pi < found; ++pi) {
                    printf("%" PRIu64 "\n", buf[pi]);
                }
                printf("--- %zu primes listed (%.6fs) ---\n", found, t1 - t0);
                free(buf);
                continue;
            }

            /* ── Extended query commands ── */

            if (strcmp(app_argv[1], "next-prime") == 0) {
                if (app_argc < 3) { printf("Usage: next-prime <n>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                double t0 = now_sec();
                CPSSQueryStatus qs = CPSS_OK;
                uint64_t p = app_multi_db_open
                    ? cpss_db_next_prime_s(&app_multi_db, n, &qs)
                    : cpss_next_prime_s(&app_db, n, &qs);
                double t1 = now_sec();
                if (qs == CPSS_OK) printf("next_prime(%" PRIu64 ") = %" PRIu64 " (%.6fs)\n", n, p, t1 - t0);
                else printf("next_prime(%" PRIu64 ") unavailable (%s, %.6fs)\n", n, cpss_status_name(qs), t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "prev-prime") == 0) {
                if (app_argc < 3) { printf("Usage: prev-prime <n>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                double t0 = now_sec();
                CPSSQueryStatus qs = CPSS_OK;
                uint64_t p = app_multi_db_open
                    ? cpss_db_prev_prime_s(&app_multi_db, n, &qs)
                    : cpss_prev_prime_s(&app_db, n, &qs);
                double t1 = now_sec();
                if (qs == CPSS_OK) printf("prev_prime(%" PRIu64 ") = %" PRIu64 " (%.6fs)\n", n, p, t1 - t0);
                else printf("prev_prime(%" PRIu64 ") unavailable (%s, %.6fs)\n", n, cpss_status_name(qs), t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "pi") == 0 || strcmp(app_argv[1], "prime-rank") == 0) {
                if (app_argc < 3) { printf("Usage: pi <n>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                double t0 = now_sec();
                uint64_t c = app_multi_db_open
                    ? cpss_db_count_range(&app_multi_db, 1u, n)
                    : cpss_pi(&app_db, n);
                double t1 = now_sec();
                printf("pi(%" PRIu64 ") = %" PRIu64 " (%.6fs)\n", n, c, t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "nth-prime") == 0) {
                if (app_argc < 3) { printf("Usage: nth-prime <k>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t k = parse_u64(app_argv[2], "k");
                double t0 = now_sec();
                CPSSQueryStatus qs = CPSS_OK;
                uint64_t p = app_multi_db_open
                    ? cpss_db_nth_prime_s(&app_multi_db, k, &qs)
                    : cpss_nth_prime(&app_db, k);
                double t1 = now_sec();
                if (p > 0u) printf("prime(%" PRIu64 ") = %" PRIu64 " (%.6fs)\n", k, p, t1 - t0);
                else if (app_multi_db_open) printf("prime(%" PRIu64 ") unavailable (%s, %.6fs)\n", k, cpss_status_name(qs), t1 - t0);
                else printf("%" PRIu64 "-th prime exceeds stream coverage\n", k);
                continue;
            }

            if (strcmp(app_argv[1], "prime-gap") == 0) {
                if (app_argc < 3) { printf("Usage: prime-gap <n>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                double t0 = now_sec();
                PrimeGapInfo g;
                memset(&g, 0, sizeof(g));
                g.n = n;
                if (app_multi_db_open) {
                    CPSSQueryStatus prev_qs = CPSS_OK;
                    CPSSQueryStatus next_qs = CPSS_OK;
                    g.n_is_prime = (cpss_db_is_prime(&app_multi_db, n) == 1);
                    g.prev = cpss_db_prev_prime_s(&app_multi_db, n, &prev_qs);
                    g.next = cpss_db_next_prime_s(&app_multi_db, n, &next_qs);
                    if (g.prev > 0u) g.gap_below = n - g.prev;
                    if (g.next > 0u) g.gap_above = g.next - n;
                    if (g.prev > 0u && g.next > 0u) g.gap_around = g.next - g.prev;
                    if (prev_qs != CPSS_OK) {
                        printf("prime_gap(%" PRIu64 "): prev unavailable (%s)\n", n, cpss_status_name(prev_qs));
                    }
                    if (next_qs != CPSS_OK) {
                        printf("prime_gap(%" PRIu64 "): next unavailable (%s)\n", n, cpss_status_name(next_qs));
                    }
                }
                else {
                    g = cpss_prime_gap_at(&app_db, n);
                }
                double t1 = now_sec();
                printf("prime_gap(%" PRIu64 "):\n", n);
                printf("  n is %s\n", g.n_is_prime ? "PRIME" : "composite");
                if (g.prev > 0u) printf("  prev prime = %" PRIu64 " (gap = %" PRIu64 ")\n", g.prev, g.gap_below);
                if (g.next > 0u) printf("  next prime = %" PRIu64 " (gap = %" PRIu64 ")\n", g.next, g.gap_above);
                if (g.prev > 0u && g.next > 0u)
                    printf("  full gap   = %" PRIu64 " (%" PRIu64 " .. %" PRIu64 ")\n", g.gap_around, g.prev, g.next);
                printf("  (%.6fs)\n", t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "primes-near") == 0) {
                if (app_argc < 3) { printf("Usage: primes-near <n> [radius]\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                uint64_t radius = 1000u;
                if (app_argc >= 4) radius = parse_u64(app_argv[3], "radius");
                uint64_t lo = n > radius ? n - radius : 1u;
                uint64_t hi = n + radius;
                size_t limit = 200u;
                uint64_t* buf = (uint64_t*)xmalloc(limit * sizeof(uint64_t));
                double t0 = now_sec();
                size_t found = app_multi_db_open
                    ? cpss_db_list_range(&app_multi_db, lo, hi, buf, limit)
                    : cpss_list_range_hot(&app_db, lo, hi, buf, limit);
                double t1 = now_sec();
                for (size_t pi2 = 0u; pi2 < found; ++pi2) {
                    int64_t delta = (int64_t)buf[pi2] - (int64_t)n;
                    printf("  %" PRIu64 " (%+" PRId64 ")\n", buf[pi2], delta);
                }
                printf("--- %zu primes near %" PRIu64 " +/-%" PRIu64 " (%.6fs) ---\n",
                    found, n, radius, t1 - t0);
                free(buf);
                continue;
            }

            if (strcmp(app_argv[1], "nearest") == 0) {
                if (app_argc < 4) { printf("Usage: nearest <n> <k>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                size_t k = (size_t)parse_u64(app_argv[3], "k");
                /* Expand radius until we have k primes */
                uint64_t radius = 1000u;
                uint64_t* buf = NULL;
                size_t found = 0u;
                double t0 = now_sec();
                while (found < k && radius < 100000000u) {
                    uint64_t lo = n > radius ? n - radius : 1u;
                    uint64_t hi = n + radius;
                    free(buf);
                    buf = (uint64_t*)xmalloc((k + 100u) * sizeof(uint64_t));
                    found = app_multi_db_open
                        ? cpss_db_list_range(&app_multi_db, lo, hi, buf, k + 100u)
                        : cpss_list_range_hot(&app_db, lo, hi, buf, k + 100u);
                    radius *= 4u;
                }
                double t1 = now_sec();
                /* Sort by distance to n, print k nearest */
                size_t printed = 0u;
                /* Simple approach: scan and find closest */
                for (size_t pass = 0u; pass < found && printed < k; ++pass) {
                    size_t best = SIZE_MAX;
                    uint64_t best_dist = UINT64_MAX;
                    for (size_t j = 0u; j < found; ++j) {
                        uint64_t d = buf[j] > n ? buf[j] - n : n - buf[j];
                        if (d < best_dist) { best_dist = d; best = j; }
                    }
                    if (best == SIZE_MAX) break;
                    int64_t delta = (int64_t)buf[best] - (int64_t)n;
                    printf("  %" PRIu64 " (%+" PRId64 ")\n", buf[best], delta);
                    buf[best] = UINT64_MAX; /* mark as used */
                    ++printed;
                }
                printf("--- %zu nearest primes to %" PRIu64 " (%.6fs) ---\n", printed, n, t1 - t0);
                free(buf);
                continue;
            }

            if (strcmp(app_argv[1], "range-stats") == 0) {
                if (app_argc < 4) { printf("Usage: range-stats <lo> <hi>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t lo = parse_u64(app_argv[2], "lo");
                uint64_t hi = parse_u64(app_argv[3], "hi");
                double t0 = now_sec();
                CPSSQueryStatus qs = CPSS_OK;
                RangeStats st = app_multi_db_open
                    ? cpss_db_range_stats(&app_multi_db, lo, hi, &qs)
                    : cpss_range_stats(&app_db, lo, hi);
                double t1 = now_sec();
                if (app_multi_db_open && qs != CPSS_OK) {
                    printf("range-stats [%" PRIu64 ", %" PRIu64 "] unavailable (%s, %.6fs)\n",
                        lo, hi, cpss_status_name(qs), t1 - t0);
                    continue;
                }
                printf("range-stats [%" PRIu64 ", %" PRIu64 "]:\n", lo, hi);
                printf("  prime count        = %" PRIu64 "\n", st.prime_count);
                printf("  range width        = %" PRIu64 "\n", st.range_width);
                printf("  density            = %.8f\n", st.density);
                printf("  expected (1/ln)    = %.8f\n", st.expected_density);
                printf("  density ratio      = %.4f\n", st.expected_density > 0.0 ? st.density / st.expected_density : 0.0);
                if (st.first_prime > 0u) printf("  first prime        = %" PRIu64 "\n", st.first_prime);
                if (st.last_prime > 0u) printf("  last prime         = %" PRIu64 "\n", st.last_prime);
                if (st.prime_count >= 2u) {
                    printf("  min gap            = %" PRIu64 "\n", st.min_gap);
                    printf("  max gap            = %" PRIu64 "\n", st.max_gap);
                    printf("  avg gap            = %.1f\n", st.avg_gap);
                }
                printf("  (%.6fs)\n", t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "factor-support") == 0) {
                if (app_argc < 3) { printf("Usage: factor-support <n>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                uint64_t sqrt_n = isqrt_u64(n);
                if (sqrt_n * sqrt_n < n) ++sqrt_n; /* ceiling */
                size_t limit = 10000u;
                uint64_t* buf = (uint64_t*)xmalloc(limit * sizeof(uint64_t));
                double t0 = now_sec();
                uint64_t count = app_multi_db_open
                    ? cpss_db_count_range(&app_multi_db, 2u, sqrt_n)
                    : cpss_count_range_hot(&app_db, 2u, sqrt_n);
                size_t found = app_multi_db_open
                    ? cpss_db_list_range(&app_multi_db, 2u, sqrt_n, buf, limit)
                    : cpss_list_range_hot(&app_db, 2u, sqrt_n, buf, limit);
                double t1 = now_sec();
                printf("factor-support(%" PRIu64 "): trial range [2, %" PRIu64 "]\n", n, sqrt_n);
                printf("  primes in range    = %" PRIu64 "\n", count);
                printf("  listed (capped)    = %zu\n", found);
                if (found <= 100u) {
                    for (size_t pi2 = 0u; pi2 < found; ++pi2) {
                        printf("  %" PRIu64, buf[pi2]);
                        if ((pi2 + 1u) % 10u == 0u || pi2 == found - 1u) printf("\n");
                        else printf(" ");
                    }
                }
                else {
                    printf("  (too many to list; first 10: ");
                    for (size_t pi2 = 0u; pi2 < 10u && pi2 < found; ++pi2) printf("%" PRIu64 " ", buf[pi2]);
                    printf("...)\n");
                }
                printf("  (%.6fs)\n", t1 - t0);
                free(buf);
                continue;
            }

            /* ── New Phase 1-3 query commands ── */

            if (strcmp(app_argv[1], "support-coverage") == 0) {
                if (app_argc < 3) { printf("Usage: support-coverage <n>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                SupportCoverage cov = app_multi_db_open
                    ? cpss_db_support_coverage(&app_multi_db, n)
                    : cpss_support_coverage(&app_db, n);
                printf("support-coverage(%" PRIu64 "):\n", n);
                printf("  sqrt(n)            = %" PRIu64 "\n", cov.sqrt_n);
                printf("  DB level:\n");
                printf("    db_hi            = %" PRIu64 "\n", cov.db_hi);
                printf("    complete         = %s\n", cov.db_complete ? "YES" : "NO");
                printf("    coverage         = %.2f%%\n", cov.db_coverage * 100.0);
                printf("  Runtime availability:\n");
                printf("    file mapped      = %s\n", cov.rt_file_mapped ? "yes (cold queries work)" : "no (unmapped)");
                printf("    hot_hi           = %" PRIu64 "\n", cov.rt_hot_hi);
                printf("    hot complete     = %s\n", cov.rt_hot_complete ? "YES (hot cache can certify now)" : "NO");
                printf("    hot coverage     = %.2f%%\n", cov.rt_hot_coverage * 100.0);
                if (cov.db_complete && !cov.rt_file_mapped && !cov.rt_hot_complete) {
                    printf("  WARNING: DB covers sqrt(n) but runtime cannot certify (file unmapped, hot cache insufficient)\n");
                }
                continue;
            }

            if (strcmp(app_argv[1], "segment-info") == 0) {
                if (app_argc < 3) { printf("Usage: segment-info <n>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                SegmentInfo si = app_multi_db_open
                    ? cpss_db_segment_info(&app_multi_db, n)
                    : cpss_segment_info(&app_db, n);
                if (!si.found) {
                    printf("%" PRIu64 " is not within any loaded segment.\n", n);
                }
                else {
                    printf("segment-info(%" PRIu64 "):\n", n);
                    printf("  segment            = %" PRIu64 "\n", si.segment_no);
                    printf("  range              = %" PRIu64 " .. %" PRIu64 "\n", si.seg_start_n, si.seg_end_n);
                    printf("  candidates         = %" PRIu32 "\n", si.total_candidates);
                    printf("  survivors          = %" PRIu32 "\n", si.survivor_count);
                    printf("  hot cached         = %s\n", si.is_hot ? "yes" : "no");
                    printf("  file mapped        = %s\n", si.is_cold ? "yes" : "no");
                    if (si.prime_count > 0u) printf("  primes in segment  = %" PRIu64 "\n", si.prime_count);
                }
                continue;
            }

            if (strcmp(app_argv[1], "classify") == 0) {
                if (app_argc < 3) { printf("Usage: classify <n>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                double t0 = now_sec();
                CompositeClass cls = app_multi_db_open
                    ? cpss_db_classify(&app_multi_db, n)
                    : cpss_classify(&app_db, n);
                double t1 = now_sec();
                printf("classify(%" PRIu64 "):\n", n);
                printf("  even               = %s\n", cls.is_even ? "yes" : "no");
                printf("  perfect square     = %s\n", cls.is_perfect_square ? "yes" : "no");
                printf("  perfect power      = %s\n", cls.is_prime_power ? "yes" : "no");
                printf("  prime              = %s", cls.is_prime ? "yes" : "no");
                if (cls.prime_source == 1) printf(" (DB)\n");
                else if (cls.prime_source == 2) printf(" (Miller-Rabin)\n");
                else printf("\n");
                printf("  has small factor   = %s\n", cls.has_small_factor ? "yes" : "no");
                if (cls.smallest_factor > 0u) printf("  smallest factor    = %" PRIu64 "\n", cls.smallest_factor);
                printf("  (%.6fs)\n", t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "prime-iter") == 0) {
                if (app_argc < 3) { printf("Usage: prime-iter <start> [count]\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t start = parse_u64(app_argv[2], "start");
                size_t count = 20u;
                if (app_argc >= 4) count = (size_t)parse_u64(app_argv[3], "count");
                double t0 = now_sec();
                size_t printed = 0u;
                CPSSQueryStatus qs = CPSS_OK;
                if (app_multi_db_open) {
                    uint64_t current = 0u;
                    int prime_state = cpss_db_is_prime(&app_multi_db, start);
                    if (prime_state == 1) current = start;
                    else if (prime_state == 0) current = cpss_db_next_prime_s(&app_multi_db, start, &qs);
                    else qs = (prime_state == -2) ? CPSS_UNAVAILABLE : CPSS_OUT_OF_RANGE;

                    while (qs == CPSS_OK && current > 0u && printed < count) {
                        printf("%" PRIu64 "\n", current);
                        ++printed;
                        if (printed < count) current = cpss_db_next_prime_s(&app_multi_db, current, &qs);
                    }
                }
                else {
                    PrimeIterator iter;
                    cpss_iter_init(&iter, &app_db, start);
                    while (iter.valid && printed < count) {
                        printf("%" PRIu64 "\n", iter.current);
                        ++printed;
                        cpss_iter_next(&iter);
                    }
                    qs = (CPSSQueryStatus)iter.status;
                }
                double t1 = now_sec();
                printf("--- %zu primes from %" PRIu64 " (%.6fs, status=%s) ---\n",
                    printed, start, t1 - t0, cpss_status_name(qs));
                continue;
            }

            if (strcmp(app_argv[1], "prime-iter-prev") == 0) {
                if (app_argc < 3) { printf("Usage: prime-iter-prev <start> [count]\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t start = parse_u64(app_argv[2], "start");
                size_t count = 20u;
                if (app_argc >= 4) count = (size_t)parse_u64(app_argv[3], "count");
                double t0 = now_sec();
                size_t printed = 0u;
                CPSSQueryStatus qs = CPSS_OK;
                if (app_multi_db_open) {
                    uint64_t probe = start;
                    uint64_t current = 0u;

                    if (probe > app_multi_db.db_hi) probe = app_multi_db.db_hi;
                    if (probe >= 2u) {
                        int prime_state = cpss_db_is_prime(&app_multi_db, probe);
                        if (prime_state == 1) current = probe;
                        else if (prime_state == 0) current = cpss_db_prev_prime_s(&app_multi_db, probe, &qs);
                        else qs = (prime_state == -2) ? CPSS_UNAVAILABLE : CPSS_OUT_OF_RANGE;
                    }

                    while (qs == CPSS_OK && current > 0u && printed < count) {
                        printf("%" PRIu64 "\n", current);
                        ++printed;
                        if (printed < count) current = cpss_db_prev_prime_s(&app_multi_db, current, &qs);
                    }
                }
                else {
                    PrimeIterator iter;
                    /* Init forward to find the prime at or just before start, then step back */
                    cpss_iter_init(&iter, &app_db, start);
                    if (iter.valid && iter.current > start) {
                        cpss_iter_prev(&iter);
                    }
                    else if (!iter.valid) {
                        /* start is past end of DB — find last prime */
                        uint64_t p = cpss_prev_prime(&app_db, start + 1u);
                        if (p > 0u) {
                            cpss_iter_init(&iter, &app_db, p);
                        }
                    }
                    while (iter.valid && printed < count) {
                        printf("%" PRIu64 "\n", iter.current);
                        ++printed;
                        cpss_iter_prev(&iter);
                    }
                    qs = (CPSSQueryStatus)iter.status;
                }
                double t1 = now_sec();
                printf("--- %zu primes backward from %" PRIu64 " (%.6fs, status=%s) ---\n",
                    printed, start, t1 - t0, cpss_status_name(qs));
                continue;
            }

            if (strcmp(app_argv[1], "factor") == 0 || strcmp(app_argv[1], "factor-bigint") == 0) {
                if (app_argc < 3) { printf("Usage: factor <n>\n"); continue; }
                ParsedNum pn = parse_numeric(app_argv[2], "n");
                bool force_bn = (strcmp(app_argv[1], "factor-bigint") == 0);
                CPSSStream* qs = (app_db_open || app_multi_db_open)
                    ? (app_db_open ? &app_db : &app_multi_db.streams[0]) : NULL;

                if (!force_bn && pn.tier == TIER_U64) {
                    /* Tier A: uint64 fast path */
                    if (app_multi_db_open) {
                        FactorResult fr = cpss_db_factor_auto(&app_multi_db, pn.u64);
                        work_history_record_auto(pn.u64, &fr);
                        factor_result_print(&fr);
                    } else if (qs) {
                        FactorResult fr = cpss_factor_auto(qs, pn.u64);
                        work_history_record_auto(pn.u64, &fr);
                        factor_result_print(&fr);
                    } else {
                        /* No DB: rho + fermat fallback */
                        FactorResult fr = cpss_factor_rho(pn.u64, 0u, 0u);
                        if (!fr.fully_factored) fr = cpss_factor_fermat(pn.u64, 0u);
                        factor_result_print(&fr);
                    }
                } else if (!force_bn && pn.tier == TIER_U128) {
                    /* Tier B: U128 path */
                    if (qs) {
                        FactorResultU128 fr = cpss_factor_auto_u128(qs, pn.u128);
                        factor_result_u128_print(&fr);
                    } else {
                        printf("  [Tier B input, %u bits] no DB loaded; falling back to BigNum methods\n", pn.bits);
                        FactorResultBigNum fr = cpss_factor_rho_bn(&pn.bn, 0u, 0u);
                        if (!fr.factor_found) fr = cpss_factor_fermat_bn(&pn.bn, 0u);
                        if (!fr.factor_found) fr = cpss_factor_ecm_bn(&pn.bn, 0u, 0u, 0u);
                        factor_result_bn_print_recursive_ex(&fr, qs, "Tier B: U128 (BigNum fallback)");
                    }
                } else {
                    /* Tier C: BigNum path */
                    printf("  [Tier C: BigNum, %u bits]", pn.bits);
                    if (qs) {
                        printf(" DB loaded; trying p-1, p+1, rho, fermat, ECM\n");
                        FactorResultBigNum fr = cpss_factor_auto_bn(qs, &pn.bn);
                        factor_result_bn_print_recursive(&fr, qs);
                    } else {
                        printf(" no DB; trying rho, fermat, ECM (DB-backed methods unavailable)\n");
                        FactorResultBigNum fr = cpss_factor_rho_bn(&pn.bn, 0u, 0u);
                        if (!fr.factor_found) fr = cpss_factor_fermat_bn(&pn.bn, 0u);
                        if (!fr.factor_found) fr = cpss_factor_ecm_bn(&pn.bn, 0u, 0u, 0u);
                        factor_result_bn_print_recursive(&fr, qs);
                    }
                }
                continue;
            }

            if (strcmp(app_argv[1], "factor-fermat") == 0) {
                if (app_argc < 3) { printf("Usage: factor-fermat <n> [--max-steps N]\n"); continue; }
                uint64_t max_steps = 0u;
                bool bad_opt = false;
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--max-steps") == 0 && ai + 1 < app_argc) {
                        max_steps = parse_u64(app_argv[++ai], "max-steps");
                    }
                    else {
                        printf("Unknown option for factor-fermat: %s\n", app_argv[ai]);
                        bad_opt = true;
                        break;
                    }
                }
                if (bad_opt) continue;

                BigNum bn = parse_bignum(app_argv[2], "n");
                if (bn_fits_u64(&bn)) {
                    uint64_t n64 = bn_to_u64(&bn);
                    FactorResult fr = cpss_factor_fermat(n64, max_steps);
                    work_history_record_fermat(n64, max_steps > 0u ? max_steps : fermat_default_steps_small(0u), &fr);
                    factor_result_print(&fr);
                }
                else if (bn_fits_u128(&bn)) {
                    FactorResultU128 fr = cpss_factor_fermat_u128(bn_to_u128(&bn), max_steps);
                    factor_result_u128_print(&fr);
                }
                else {
                    FactorResultBigNum fr = cpss_factor_fermat_bn(&bn, max_steps);
                    factor_result_bn_print(&fr);
                }
                continue;
            }

            if (strcmp(app_argv[1], "factor-rho") == 0) {
                if (app_argc < 3) { printf("Usage: factor-rho <n> [--max-iterations N] [--seed N]\n"); continue; }
                uint64_t max_iterations = 0u;
                uint64_t seed = 0u;
                bool bad_opt = false;
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--max-iterations") == 0 && ai + 1 < app_argc) {
                        max_iterations = parse_u64(app_argv[++ai], "max-iterations");
                    }
                    else if (strcmp(app_argv[ai], "--seed") == 0 && ai + 1 < app_argc) {
                        seed = parse_u64(app_argv[++ai], "seed");
                    }
                    else {
                        printf("Unknown option for factor-rho: %s\n", app_argv[ai]);
                        bad_opt = true;
                        break;
                    }
                }
                if (bad_opt) continue;

                ParsedNum pn = parse_numeric(app_argv[2], "n");
                if (pn.tier == TIER_U64) {
                    FactorResult fr = cpss_factor_rho(pn.u64, max_iterations, seed);
                    factor_result_print(&fr);
                } else if (pn.tier == TIER_U128) {
                    FactorResultU128 fr = cpss_factor_rho_u128(pn.u128, max_iterations, seed);
                    factor_result_u128_print(&fr);
                } else {
                    FactorResultBigNum fr = cpss_factor_rho_bn(&pn.bn, max_iterations, seed);
                    factor_result_bn_print(&fr);
                }
                continue;
            }

            if (strcmp(app_argv[1], "factor-ecm") == 0) {
                if (app_argc < 3) { printf("Usage: factor-ecm <n> [--B1 N] [--B2 N] [--curves N] [--seed N] [--affine]\n"); continue; }
                uint64_t B1 = 0u, B2 = 0u;
                uint64_t curves = 0u;
                uint64_t seed = 0u;
                bool use_affine = false;
                bool bad_opt = false;
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--B1") == 0 && ai + 1 < app_argc) {
                        B1 = parse_u64(app_argv[++ai], "B1");
                    }
                    else if (strcmp(app_argv[ai], "--B2") == 0 && ai + 1 < app_argc) {
                        B2 = parse_u64(app_argv[++ai], "B2");
                    }
                    else if (strcmp(app_argv[ai], "--curves") == 0 && ai + 1 < app_argc) {
                        curves = parse_u64(app_argv[++ai], "curves");
                    }
                    else if (strcmp(app_argv[ai], "--seed") == 0 && ai + 1 < app_argc) {
                        seed = parse_u64(app_argv[++ai], "seed");
                    }
                    else if (strcmp(app_argv[ai], "--affine") == 0) {
                        use_affine = true;
                    }
                    else {
                        printf("Unknown option for factor-ecm: %s\n", app_argv[ai]);
                        bad_opt = true;
                        break;
                    }
                }
                if (bad_opt) continue;

                ParsedNum pn = parse_numeric(app_argv[2], "n");
                if (pn.tier == TIER_U64 && !use_affine) {
                    FactorResult fr = cpss_factor_ecm_u64(pn.u64, B1, curves, seed);
                    factor_result_print(&fr);
                } else if (pn.tier == TIER_U128 && !use_affine) {
                    FactorResultU128 fr = cpss_factor_ecm_u128(pn.u128, B1, curves, seed);
                    factor_result_u128_print(&fr);
                } else {
                    BigNum bn;
                    if (pn.tier == TIER_U64) { bn_from_u64(&bn, pn.u64); }
                    else if (pn.tier == TIER_U128) {
                        bn_from_u64(&bn, pn.u128.lo);
                        if (pn.u128.hi) { BigNum hi; bn_from_u64(&hi, pn.u128.hi);
                            for (int sh = 0; sh < 64; ++sh) bn_shl1(&hi); bn_add(&bn, &hi); }
                    } else { bn_copy(&bn, &pn.bn); }
                    if (use_affine) {
                        FactorResultBigNum fr = cpss_factor_ecm_bn_ex(&bn, B1, B2, curves, seed);
                        factor_result_bn_print(&fr);
                    } else {
                        FactorResultBigNum fr = cpss_factor_ecm_mont_stage1(&bn, B1, curves, seed);
                        factor_result_bn_print(&fr);
                    }
                }
                continue;
            }

            if (strcmp(app_argv[1], "factor-ecm-bench") == 0) {
                uint64_t rng = 0xABCDEF0123456789ULL;
                uint64_t B1 = 500u;
                uint32_t curves = 4u;
                uint32_t samples = 5u;
                static const unsigned buckets[] = { 128, 192, 256, 384, 512 };

                printf("=== ECM Benchmark: Affine vs Montgomery ===\n");
                printf("B1=%"PRIu64"  curves=%u  samples=%u per bucket\n\n", B1, curves, samples);

                /* ── Benchmark A: random probable-prime moduli, full work (INCOMPLETE) ── */
                printf("--- Benchmark A: Random prime moduli (equal-work throughput) ---\n");
                printf("  %-6s  %-12s %-12s  ratio\n", "bits", "affine(ms)", "mont(ms)");
                for (size_t bi = 0u; bi < ARRAY_LEN(buckets); ++bi) {
                    unsigned bits = buckets[bi];
                    double affine_sum = 0.0, mont_sum = 0.0;
                    for (uint32_t si = 0u; si < samples; ++si) {
                        BigNum p; bn_random_prime(&p, bits, &rng);
                        /* Affine: Stage 1 only (B2=1 disables Stage 2) */
                        FactorResultBigNum fra = cpss_factor_ecm_bn_ex(&p, B1, 1u, curves, rng + si);
                        affine_sum += fra.time_seconds;
                        /* Montgomery: same */
                        FactorResultBigNum frm = cpss_factor_ecm_mont_stage1(&p, B1, curves, rng + si);
                        mont_sum += frm.time_seconds;
                    }
                    double a_med = (affine_sum / samples) * 1000.0;
                    double m_med = (mont_sum / samples) * 1000.0;
                    printf("  %-6u  %-12.3f %-12.3f  %.1fx\n", bits, a_med, m_med,
                        m_med > 0.001 ? a_med / m_med : 0.0);
                }

                /* ── Benchmark B: random semiprimes, normal early exit ── */
                printf("\n--- Benchmark B: Random semiprimes (practical ECM, normal exit) ---\n");
                printf("  %-6s  %-6s %-12s %-12s  %-6s %-6s\n",
                    "N-bits", "p-bits", "affine(ms)", "mont(ms)", "a-ok?", "m-ok?");
                static const unsigned semi_N[]  = { 128, 192, 256, 384 };
                static const unsigned semi_p[]  = {  32,  40,  50,  50 };
                uint64_t B1b = 5000u;
                uint32_t curves_b = 8u;
                for (size_t bi = 0u; bi < ARRAY_LEN(semi_N); ++bi) {
                    unsigned n_bits = semi_N[bi];
                    unsigned p_bits = semi_p[bi];
                    unsigned q_bits = n_bits - p_bits;
                    double affine_sum = 0.0, mont_sum = 0.0;
                    uint32_t affine_ok = 0u, mont_ok = 0u;
                    for (uint32_t si = 0u; si < samples; ++si) {
                        BigNum pp, qq, nn;
                        bn_random_prime(&pp, p_bits, &rng);
                        bn_random_prime(&qq, q_bits, &rng);
                        bn_mul(&nn, &pp, &qq);
                        /* Affine: Stage 1 only (B2=1 disables Stage 2 for fair comparison) */
                        FactorResultBigNum fra = cpss_factor_ecm_bn_ex(&nn, B1b, 1u, curves_b, rng + si * 7u);
                        affine_sum += fra.time_seconds;
                        if (fra.factor_found) ++affine_ok;
                        /* Montgomery */
                        FactorResultBigNum frm = cpss_factor_ecm_mont_stage1(&nn, B1b, curves_b, rng + si * 7u);
                        mont_sum += frm.time_seconds;
                        if (frm.factor_found) ++mont_ok;
                    }
                    double a_avg = (affine_sum / samples) * 1000.0;
                    double m_avg = (mont_sum / samples) * 1000.0;
                    printf("  %-6u  %-6u %-12.3f %-12.3f  %u/%u   %u/%u\n",
                        n_bits, p_bits, a_avg, m_avg,
                        affine_ok, samples, mont_ok, samples);
                }
                printf("\nDone.\n");
                continue;
            }

            if (strcmp(app_argv[1], "factor-qs-bench") == 0) {
                /* Verified balanced-semiprime benchmark for QS.
                 * Generates p*q where p,q are random primes of half the target bit size. */
                static const unsigned buckets[] = { 36, 40, 44, 48, 52, 56 };
                uint32_t samples = 5u;
                uint64_t rng = 0x9E3779B97F4A7C15ULL;

                printf("=== QS Balanced-Semiprime Benchmark ===\n");
                printf("%-20s %4s %4s %6s %8s %5s %5s %4s %4s %s\n",
                    "N", "Nb", "pb", "result", "time_ms", "rels", "full", "part", "lpc", "factor");

                for (size_t bi = 0u; bi < ARRAY_LEN(buckets); ++bi) {
                    unsigned nbits = buckets[bi];
                    unsigned half = nbits / 2;
                    int wins = 0;
                    printf("--- %u-bit balanced semiprimes ---\n", nbits);

                    for (uint32_t si = 0u; si < samples; ++si) {
                        /* Generate two random primes of half bits */
                        uint64_t p = 0u, q = 0u;
                        for (;;) {
                            uint64_t cand = factor_xorshift64(&rng);
                            cand |= ((uint64_t)1u << (half - 1u)); /* ensure MSB set */
                            cand &= (((uint64_t)1u << half) - 1u); /* mask to half bits */
                            cand |= 1u; /* ensure odd */
                            if (miller_rabin_u64(cand)) { p = cand; break; }
                        }
                        for (;;) {
                            uint64_t cand = factor_xorshift64(&rng);
                            cand |= ((uint64_t)1u << (half - 1u));
                            cand &= (((uint64_t)1u << half) - 1u);
                            cand |= 1u;
                            if (miller_rabin_u64(cand) && cand != p) { q = cand; break; }
                        }
                        if (p > q) { uint64_t t = p; p = q; q = t; }
                        uint64_t N = p * q;

                        /* Verify N = p*q and both prime */
                        if (N / p != q || !miller_rabin_u64(p) || !miller_rabin_u64(q)) {
                            printf("  SKIP: overflow or verification failure\n");
                            continue;
                        }

                        FactorResult fr = cpss_factor_qs_u64(N);
                        bool ok = fr.fully_factored || (fr.factor_count > 0u && fr.factors[0] > 1u && fr.factors[0] < N);
                        if (ok) ++wins;

                        unsigned actual_nbits = 0u;
                        { uint64_t tmp = N; while (tmp > 0u) { ++actual_nbits; tmp >>= 1u; } }
                        unsigned actual_pbits = 0u;
                        { uint64_t tmp = p; while (tmp > 0u) { ++actual_pbits; tmp >>= 1u; } }

                        printf("%-20"PRIu64" %4u %4u %6s %8.1f",
                            N, actual_nbits, actual_pbits,
                            ok ? "OK" : "FAIL",
                            fr.time_seconds * 1000.0);

                        /* Print factor if found */
                        if (fr.factor_count > 0u && fr.factors[0] > 1u)
                            printf(" %5d %5s %4s %4s %" PRIu64,
                                0, "-", "-", "-", fr.factors[0]);
                        else
                            printf(" %5s %5s %4s %4s %s", "-", "-", "-", "-", "-");
                        printf("\n");
                    }
                    printf("  bucket %u-bit: %d/%u succeeded\n\n", nbits, wins, samples);
                }
                printf("Done.\n");
                continue;
            }

            if (strcmp(app_argv[1], "factor-squfof") == 0) {
                if (app_argc < 3) { printf("Usage: factor-squfof <n>  (uint64 only, no DB needed)\n"); continue; }
                ParsedNum pn = parse_numeric(app_argv[2], "n");
                if (pn.tier != TIER_U64) {
                    printf("SQUFOF is a uint64-only method. Input has %u bits.\n", pn.bits);
                    continue;
                }
                FactorResult fr = cpss_factor_squfof(pn.u64);
                factor_result_print(&fr);
                continue;
            }

            if (strcmp(app_argv[1], "factor-qs") == 0) {
                if (app_argc < 3) {
                    printf("Usage: factor-qs <n> [advanced flags]\n"
                           "  Advanced tuning (research/debug):\n"
                           "    --fb <int>              factor base size\n"
                           "    --sieve <int>           sieve half-width\n"
                           "    --polys <int>           max polynomial count\n"
                           "    --rels <int>            relation budget\n"
                           "    --lp-mult <int>         LP bound multiplier (0=disable LP)\n"
                           "    --sp-warmup-polys <int> single-poly warmup iterations\n"
                           "    --sp-warmup-rels <int>  single-poly relation cap\n"
                           "    --siqs-only             skip single-poly warmup\n");
                    continue;
                }
                ParsedNum pn = parse_numeric(app_argv[2], "n");
                /* Parse advanced QS flags */
                bool has_overrides = false;
                QSConfig qs_cfg;
                qs_config_default_for_bits(pn.bits, &qs_cfg);
                bool bad_opt = false;
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--fb") == 0 && ai + 1 < app_argc) {
                        qs_cfg.fb_size = (int)parse_u64(app_argv[++ai], "fb"); has_overrides = true;
                    } else if (strcmp(app_argv[ai], "--sieve") == 0 && ai + 1 < app_argc) {
                        qs_cfg.sieve_half = (int)parse_u64(app_argv[++ai], "sieve"); has_overrides = true;
                    } else if (strcmp(app_argv[ai], "--polys") == 0 && ai + 1 < app_argc) {
                        qs_cfg.max_polys = (int)parse_u64(app_argv[++ai], "polys"); has_overrides = true;
                    } else if (strcmp(app_argv[ai], "--rels") == 0 && ai + 1 < app_argc) {
                        qs_cfg.max_rels = (int)parse_u64(app_argv[++ai], "rels"); has_overrides = true;
                    } else if (strcmp(app_argv[ai], "--lp-mult") == 0 && ai + 1 < app_argc) {
                        qs_cfg.lp_mult = (int)parse_u64(app_argv[++ai], "lp-mult"); has_overrides = true;
                    } else if (strcmp(app_argv[ai], "--sp-warmup-polys") == 0 && ai + 1 < app_argc) {
                        qs_cfg.sp_warmup_polys = (int)parse_u64(app_argv[++ai], "sp-warmup-polys"); has_overrides = true;
                    } else if (strcmp(app_argv[ai], "--sp-warmup-rels") == 0 && ai + 1 < app_argc) {
                        qs_cfg.sp_warmup_rels = (int)parse_u64(app_argv[++ai], "sp-warmup-rels"); has_overrides = true;
                    } else if (strcmp(app_argv[ai], "--siqs-only") == 0) {
                        qs_cfg.siqs_only = true; has_overrides = true;
                    } else if (strcmp(app_argv[ai], "--diag") == 0) {
                        qs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--diverse") == 0) {
                        qs_cfg.diverse = true;
                    } else if (strcmp(app_argv[ai], "--family-cap") == 0 && ai + 1 < app_argc) {
                        qs_cfg.family_cap = (int)parse_u64(app_argv[++ai], "family-cap");
                        qs_cfg.diverse = true;
                    } else {
                        printf("Unknown option for factor-qs: %s\n", app_argv[ai]);
                        bad_opt = true; break;
                    }
                }
                if (bad_opt) continue;
                (void)has_overrides;
                if (pn.tier == TIER_U64) {
                    FactorResult fr = cpss_factor_qs_u64_ex(pn.u64, &qs_cfg);
                    factor_result_print(&fr);
                } else if (pn.tier == TIER_U128 && u128_fits_u64(pn.u128)) {
                    FactorResult fr = cpss_factor_qs_u64_ex(pn.u128.lo, &qs_cfg);
                    factor_result_print(&fr);
                } else {
                    /* BigNum QS attempt mode */
                    BigNum bn;
                    if (pn.tier == TIER_U128) {
                        bn_from_u64(&bn, pn.u128.lo);
                        /* For U128: construct BigNum from both halves */
                        BigNum hi; bn_from_u64(&hi, pn.u128.hi);
                        BigNum shift; bn_from_u64(&shift, 0u);
                        for (int sh = 0; sh < 64; ++sh) bn_shl1(&hi);
                        bn_add(&bn, &hi);
                    } else {
                        bn_copy(&bn, &pn.bn);
                    }
                    FactorResultBigNum fr = cpss_factor_qs_bn(&bn, &qs_cfg);
                    factor_result_bn_print(&fr);
                }
                continue;
            }

            if (strcmp(app_argv[1], "factor-gnfs") == 0) {
                if (app_argc < 3) {
                    printf("Usage: factor-gnfs <n> [flags]\n"
                           "  GNFS Milestone 1: pipeline validation (no factor extraction)\n"
                           "  Tuning flags:\n"
                           "    --degree <int>       polynomial degree (default 3)\n"
                           "    --rfb <int>          rational FB bound\n"
                           "    --afb <int>          algebraic FB bound\n"
                           "    --arange <int>       sieve a half-width\n"
                           "    --brange <int>       sieve b max\n"
                           "    --target-rels <int>  relation target (0=auto)\n"
                           "    --diag               stage-by-stage diagnostics\n");
                    continue;
                }
                ParsedNum pn = parse_numeric(app_argv[2], "n");
                GNFSConfig gnfs_cfg;
                gnfs_config_default(pn.bits, &gnfs_cfg);
                bool bad_opt = false;
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--degree") == 0 && ai + 1 < app_argc) {
                        gnfs_cfg.degree = (int)parse_u64(app_argv[++ai], "degree");
                    } else if (strcmp(app_argv[ai], "--rfb") == 0 && ai + 1 < app_argc) {
                        gnfs_cfg.rfb_bound = (int)parse_u64(app_argv[++ai], "rfb");
                    } else if (strcmp(app_argv[ai], "--afb") == 0 && ai + 1 < app_argc) {
                        gnfs_cfg.afb_bound = (int)parse_u64(app_argv[++ai], "afb");
                    } else if (strcmp(app_argv[ai], "--arange") == 0 && ai + 1 < app_argc) {
                        gnfs_cfg.a_range = (int)parse_u64(app_argv[++ai], "arange");
                    } else if (strcmp(app_argv[ai], "--brange") == 0 && ai + 1 < app_argc) {
                        gnfs_cfg.b_range = (int)parse_u64(app_argv[++ai], "brange");
                    } else if (strcmp(app_argv[ai], "--target-rels") == 0 && ai + 1 < app_argc) {
                        gnfs_cfg.target_rels = (int)parse_u64(app_argv[++ai], "target-rels");
                    } else if (strcmp(app_argv[ai], "--chars") == 0 && ai + 1 < app_argc) {
                        gnfs_cfg.char_cols = (int)parse_u64(app_argv[++ai], "chars");
                    } else if (strcmp(app_argv[ai], "--diag") == 0) {
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--verify-rows") == 0) {
                        gnfs_cfg.verify_rows = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--audit-deps") == 0) {
                        gnfs_cfg.audit_deps = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--audit-chars") == 0) {
                        gnfs_cfg.audit_chars = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--sweep-chars") == 0) {
                        gnfs_cfg.sweep_chars = true;
                        gnfs_cfg.char_cols = 32;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--measure-alg") == 0) {
                        gnfs_cfg.measure_alg = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--recover-one") == 0) {
                        gnfs_cfg.recover_one = true;
                        gnfs_cfg.measure_alg = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--audit-units") == 0) {
                        gnfs_cfg.audit_units = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--audit-real-sign") == 0) {
                        gnfs_cfg.audit_real_sign = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--audit-order") == 0) {
                        gnfs_cfg.audit_order = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--schk2") == 0) {
                        gnfs_cfg.schk2 = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--schirokauer") == 0) {
                        gnfs_cfg.schirokauer = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--crt-recover") == 0) {
                        gnfs_cfg.crt_recover = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--synth-square") == 0) {
                        gnfs_cfg.synth_square = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--selftest") == 0) {
                        gnfs_cfg.selftest = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--synth-hensel") == 0) {
                        gnfs_cfg.synth_hensel = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--local-square") == 0) {
                        gnfs_cfg.local_square = true;
                        gnfs_cfg.schirokauer = true;
                        gnfs_cfg.crt_recover = true;
                        gnfs_cfg.diag = true;
                    } else if (strcmp(app_argv[ai], "--variant-sweep") == 0) {
                        gnfs_cfg.variant_sweep = true;
                        gnfs_cfg.schirokauer = true;
                        gnfs_cfg.diag = true;
                    } else {
                        printf("Unknown option for factor-gnfs: %s\n", app_argv[ai]);
                        bad_opt = true; break;
                    }
                }
                if (bad_opt) continue;
                /* Route to GNFS pipeline */
                BigNum bn;
                if (pn.tier == TIER_U64) { bn_from_u64(&bn, pn.u64); }
                else if (pn.tier == TIER_U128) {
                    bn_from_u64(&bn, pn.u128.lo);
                    if (pn.u128.hi) { BigNum hi; bn_from_u64(&hi, pn.u128.hi);
                        for (int sh = 0; sh < 64; ++sh) bn_shl1(&hi); bn_add(&bn, &hi); }
                } else { bn_copy(&bn, &pn.bn); }
                FactorResultBigNum fr = cpss_factor_gnfs_pipeline(&bn, &gnfs_cfg);
                factor_result_bn_print(&fr);
                continue;
            }

            if (strcmp(app_argv[1], "factor-trial") == 0) {
                if (app_argc < 3) { printf("Usage: factor-trial <n> [limit]\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                U128 n = parse_u128(app_argv[2], "n");
                uint64_t limit = 0u;
                if (app_argc >= 4) limit = parse_u64(app_argv[3], "limit");
                if (app_multi_db_open && u128_fits_u64(n)) {
                    uint64_t n64 = u128_to_u64(n);
                    FactorResult fr = cpss_db_factor_trial(&app_multi_db, n64, limit);
                    work_history_record_trial(n64, limit > 0u ? limit : (uint64_t)isqrt_u64(n64), &fr);
                    factor_result_print(&fr);
                }
                else if (app_db_open && u128_fits_u64(n)) {
                    uint64_t n64 = u128_to_u64(n);
                    CPSSStream* qs = &app_db;
                    FactorResult fr = cpss_factor_trial(qs, n64, limit);
                    work_history_record_trial(n64, limit > 0u ? limit : (uint64_t)isqrt_u64(n64), &fr);
                    factor_result_print(&fr);
                }
                else {
                    CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                    FactorResultU128 fr = cpss_factor_trial_u128(qs, n, limit);
                    factor_result_u128_print(&fr);
                }
                continue;
            }

            if (strcmp(app_argv[1], "factor-pminus1") == 0) {
                if (app_argc < 3) { printf("Usage: factor-pminus1 <n> [B1]\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                BigNum bn = parse_bignum(app_argv[2], "n");
                uint64_t B1 = 0u;
                if (app_argc >= 4) B1 = parse_u64(app_argv[3], "B1");
                if (app_multi_db_open && bn_fits_u64(&bn)) {
                    FactorResult fr = cpss_db_factor_pminus1(&app_multi_db, bn_to_u64(&bn), B1);
                    factor_result_print(&fr);
                }
                else if (bn_fits_u128(&bn)) {
                    CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                    U128 n = bn_to_u128(&bn);
                    FactorResultU128 fr = cpss_factor_pminus1_u128(qs, n, B1);
                    factor_result_u128_print(&fr);
                }
                else {
                    CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                    FactorResultBigNum fr = cpss_factor_pminus1_bn(qs, &bn, B1);
                    factor_result_bn_print(&fr);
                }
                continue;
            }

            if (strcmp(app_argv[1], "factor-pplus1") == 0) {
                if (app_argc < 3) { printf("Usage: factor-pplus1 <n> [B1]\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                BigNum bn = parse_bignum(app_argv[2], "n");
                uint64_t B1 = 0u;
                if (app_argc >= 4) B1 = parse_u64(app_argv[3], "B1");
                if (app_multi_db_open && bn_fits_u64(&bn)) {
                    FactorResult fr = cpss_db_factor_pplus1(&app_multi_db, bn_to_u64(&bn), B1);
                    factor_result_print(&fr);
                }
                else if (bn_fits_u128(&bn)) {
                    CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                    U128 n = bn_to_u128(&bn);
                    FactorResultU128 fr = cpss_factor_pplus1_u128(qs, n, B1);
                    factor_result_u128_print(&fr);
                }
                else {
                    CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                    FactorResultBigNum fr = cpss_factor_pplus1_bn(qs, &bn, B1);
                    factor_result_bn_print(&fr);
                }
                continue;
            }

            if (strcmp(app_argv[1], "smallest-factor") == 0) {
                if (app_argc < 3) { printf("Usage: smallest-factor <n>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                double t0 = now_sec();
                uint64_t f = 0u;
                int status = (int)CPSS_OK;
                if (app_multi_db_open) {
                    FactorResult fr = cpss_db_factor_trial(&app_multi_db, n, 0u);
                    status = fr.status;
                    if (fr.factor_count > 0u) f = fr.factors[0];
                }
                else {
                    f = cpss_smallest_factor(&app_db, n);
                }
                double t1 = now_sec();
                if (app_multi_db_open && f == 0u) printf("smallest_factor(%" PRIu64 ") unavailable (%s, %.6fs)\n", n, cpss_status_name((CPSSQueryStatus)status), t1 - t0);
                else if (f == n) printf("%" PRIu64 " is prime (%.6fs)\n", n, t1 - t0);
                else printf("smallest_factor(%" PRIu64 ") = %" PRIu64 " (%.6fs)\n", n, f, t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "smoothness-check") == 0) {
                if (app_argc < 3) { printf("Usage: smoothness-check <n> [bound]\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t n = parse_u64(app_argv[2], "n");
                uint64_t bound = 1000000u;
                if (app_argc >= 4) bound = parse_u64(app_argv[3], "bound");
                double t0 = now_sec();
                bool smooth = false;
                int status = (int)CPSS_OK;
                if (app_multi_db_open) {
                    FactorResult fr = cpss_db_factor_trial(&app_multi_db, n, bound);
                    smooth = fr.fully_factored;
                    status = fr.status;
                }
                else {
                    smooth = cpss_is_smooth(&app_db, n, bound);
                }
                double t1 = now_sec();
                printf("%" PRIu64 " is %s%" PRIu64 "-smooth (%.6fs)\n",
                    n, smooth ? "" : "NOT ", bound, t1 - t0);
                if (app_multi_db_open && status != (int)CPSS_OK) {
                    printf("  status            = %s\n", cpss_status_name((CPSSQueryStatus)status));
                }
                continue;
            }

            if (strcmp(app_argv[1], "is-prime-batch") == 0) {
                if (app_argc < 3) { printf("Usage: is-prime-batch <n1> <n2> ...\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                double t0 = now_sec();
                for (int bi = 2; bi < app_argc; ++bi) {
                    uint64_t n = parse_u64(app_argv[bi], "n");
                    int result = app_multi_db_open
                        ? cpss_db_is_prime(&app_multi_db, n)
                        : cpss_is_prime(&app_db, n);
                    printf("%" PRIu64 " = %s\n", n,
                        result == 1 ? "PRIME" : result == 0 ? "composite" : "OUT_OF_RANGE");
                }
                double t1 = now_sec();
                printf("--- %d values tested (%.6fs) ---\n", app_argc - 2, t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "factor-batch") == 0) {
                if (app_argc < 3) { printf("Usage: factor-batch <n1> <n2> ...\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                double t0 = now_sec();
                for (int bi = 2; bi < app_argc; ++bi) {
                    uint64_t n = parse_u64(app_argv[bi], "n");
                    FactorResult fr = app_multi_db_open
                        ? cpss_db_factor_auto(&app_multi_db, n)
                        : cpss_factor_auto(&app_db, n);
                    factor_result_print(&fr);
                    if (bi < app_argc - 1) printf("\n");
                }
                double t1 = now_sec();
                printf("--- %d values factored (%.6fs total) ---\n", app_argc - 2, t1 - t0);
                continue;
            }

            /* ── Hidden Factor Space commands ── */

            if (strcmp(app_argv[1], "index-space") == 0) {
                if (app_argc < 4) { printf("Usage: index-space <i> <j>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t i = parse_u64(app_argv[2], "i");
                uint64_t j = parse_u64(app_argv[3], "j");
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                IndexSpaceProduct r = index_space_product(qs, i, j);
                if (!r.valid) { printf("index-space(%"PRIu64", %"PRIu64"): out of DB range\n", i, j); continue; }
                char pbuf[64];
                u128_to_str(r.product, pbuf, sizeof(pbuf));
                printf("index-space(%"PRIu64", %"PRIu64"):\n", i, j);
                printf("  prime(%"PRIu64") = %"PRIu64"\n", i, r.p);
                printf("  prime(%"PRIu64") = %"PRIu64"\n", j, r.q);
                printf("  product    = %s\n", pbuf);
                continue;
            }

            if (strcmp(app_argv[1], "index-space-reverse") == 0) {
                if (app_argc < 4) { printf("Usage: index-space-reverse <p> <q>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t p = parse_u64(app_argv[2], "p");
                uint64_t q = parse_u64(app_argv[3], "q");
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                uint64_t i_out, j_out;
                if (index_space_from_primes(qs, p, q, &i_out, &j_out)) {
                    printf("index-space-reverse(%"PRIu64", %"PRIu64"):\n", p, q);
                    printf("  i = %"PRIu64"\n", i_out);
                    printf("  j = %"PRIu64"\n", j_out);
                } else {
                    printf("index-space-reverse(%"PRIu64", %"PRIu64"): failed (not prime or outside DB)\n", p, q);
                }
                continue;
            }

            if (strcmp(app_argv[1], "index-space-row") == 0) {
                if (app_argc < 3) { printf("Usage: index-space-row <i> [max_j]\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t i = parse_u64(app_argv[2], "i");
                uint64_t max_j = 20u;
                if (app_argc >= 4) max_j = parse_u64(app_argv[3], "max_j");
                if (max_j > 1000u) max_j = 1000u;
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                U128* products = (U128*)xmalloc((size_t)max_j * sizeof(U128));
                uint64_t* primes = (uint64_t*)xmalloc((size_t)max_j * sizeof(uint64_t));
                size_t count = index_space_row(qs, i, max_j, products, primes);
                uint64_t row_prime = cpss_nth_prime(qs, i);
                printf("index-space-row(%"PRIu64") [prime(%"PRIu64") = %"PRIu64"]:\n", i, i, row_prime);
                for (size_t k = 0u; k < count; ++k) {
                    char pbuf[64];
                    u128_to_str(products[k], pbuf, sizeof(pbuf));
                    printf("  j=%-4"PRIu64"  q=%-12"PRIu64"  N=%s\n", (uint64_t)(k + 1u), primes[k], pbuf);
                }
                printf("--- %zu products ---\n", count);
                free(products);
                free(primes);
                continue;
            }

            if (strcmp(app_argv[1], "index-space-bounds") == 0) {
                if (app_argc < 6) { printf("Usage: index-space-bounds <i_lo> <i_hi> <j_lo> <j_hi>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t i_lo = parse_u64(app_argv[2], "i_lo");
                uint64_t i_hi = parse_u64(app_argv[3], "i_hi");
                uint64_t j_lo = parse_u64(app_argv[4], "j_lo");
                uint64_t j_hi = parse_u64(app_argv[5], "j_hi");
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                U128 N_min, N_max;
                if (index_space_bounds(qs, i_lo, i_hi, j_lo, j_hi, &N_min, &N_max)) {
                    char lo_buf[64], hi_buf[64];
                    u128_to_str(N_min, lo_buf, sizeof(lo_buf));
                    u128_to_str(N_max, hi_buf, sizeof(hi_buf));
                    printf("index-space-bounds [%"PRIu64"..%"PRIu64"] x [%"PRIu64"..%"PRIu64"]:\n", i_lo, i_hi, j_lo, j_hi);
                    printf("  N_min = %s\n", lo_buf);
                    printf("  N_max = %s\n", hi_buf);
                } else {
                    printf("index-space-bounds: out of DB range\n");
                }
                continue;
            }

            if (strcmp(app_argv[1], "index-space-row-range") == 0) {
                if (app_argc < 5) { printf("Usage: index-space-row-range <i> <j_lo> <j_hi>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t i = parse_u64(app_argv[2], "i");
                uint64_t j_lo = parse_u64(app_argv[3], "j_lo");
                uint64_t j_hi = parse_u64(app_argv[4], "j_hi");
                if (j_hi < j_lo || j_hi - j_lo > 10000u) { printf("Range too large or invalid.\n"); continue; }
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                size_t cap = (size_t)(j_hi - j_lo + 1u);
                IndexSpaceCell* cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
                size_t n = index_space_row_range(qs, i, j_lo, j_hi, cells);
                uint64_t row_p = cpss_nth_prime(qs, i);
                printf("index-space-row-range(i=%"PRIu64" [p=%"PRIu64"], j=%"PRIu64"..%"PRIu64"):\n", i, row_p, j_lo, j_hi);
                for (size_t k = 0u; k < n; ++k) {
                    char pbuf[64];
                    u128_to_str(cells[k].product, pbuf, sizeof(pbuf));
                    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64" * %"PRIu64" = %s\n",
                        cells[k].i, cells[k].j, cells[k].p, cells[k].q, pbuf);
                }
                printf("--- %zu cells ---\n", n);
                free(cells);
                continue;
            }

            if (strcmp(app_argv[1], "index-space-col-range") == 0) {
                if (app_argc < 5) { printf("Usage: index-space-col-range <j> <i_lo> <i_hi>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t j = parse_u64(app_argv[2], "j");
                uint64_t i_lo = parse_u64(app_argv[3], "i_lo");
                uint64_t i_hi = parse_u64(app_argv[4], "i_hi");
                if (i_hi < i_lo || i_hi - i_lo > 10000u) { printf("Range too large or invalid.\n"); continue; }
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                size_t cap = (size_t)(i_hi - i_lo + 1u);
                IndexSpaceCell* cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
                size_t n = index_space_col_range(qs, j, i_lo, i_hi, cells);
                uint64_t col_q = cpss_nth_prime(qs, j);
                printf("index-space-col-range(j=%"PRIu64" [q=%"PRIu64"], i=%"PRIu64"..%"PRIu64"):\n", j, col_q, i_lo, i_hi);
                for (size_t k = 0u; k < n; ++k) {
                    char pbuf[64];
                    u128_to_str(cells[k].product, pbuf, sizeof(pbuf));
                    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64" * %"PRIu64" = %s\n",
                        cells[k].i, cells[k].j, cells[k].p, cells[k].q, pbuf);
                }
                printf("--- %zu cells ---\n", n);
                free(cells);
                continue;
            }

            if (strcmp(app_argv[1], "index-space-rect") == 0 ||
                strcmp(app_argv[1], "index-space-rect-unique") == 0) {
                if (app_argc < 6) { printf("Usage: %s <i_lo> <i_hi> <j_lo> <j_hi>\n", app_argv[1]); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                bool unique_only = (strcmp(app_argv[1], "index-space-rect-unique") == 0);
                uint64_t i_lo = parse_u64(app_argv[2], "i_lo");
                uint64_t i_hi = parse_u64(app_argv[3], "i_hi");
                uint64_t j_lo = parse_u64(app_argv[4], "j_lo");
                uint64_t j_hi = parse_u64(app_argv[5], "j_hi");
                uint64_t rows = i_hi - i_lo + 1u;
                uint64_t cols = j_hi - j_lo + 1u;
                if (i_hi < i_lo || j_hi < j_lo || rows * cols > 100000u) {
                    printf("Range too large or invalid (max 100000 cells).\n"); continue;
                }
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                size_t cap = (size_t)(rows * cols);
                IndexSpaceCell* cells = (IndexSpaceCell*)xmalloc(cap * sizeof(IndexSpaceCell));
                size_t n = index_space_rect(qs, i_lo, i_hi, j_lo, j_hi, unique_only, cells, cap);
                printf("%s [%"PRIu64"..%"PRIu64"] x [%"PRIu64"..%"PRIu64"]%s:\n",
                    unique_only ? "index-space-rect-unique" : "index-space-rect",
                    i_lo, i_hi, j_lo, j_hi,
                    unique_only ? " (i<=j only)" : "");
                for (size_t k = 0u; k < n; ++k) {
                    char pbuf[64];
                    u128_to_str(cells[k].product, pbuf, sizeof(pbuf));
                    printf("  (%"PRIu64",%"PRIu64")  %"PRIu64" * %"PRIu64" = %s\n",
                        cells[k].i, cells[k].j, cells[k].p, cells[k].q, pbuf);
                }
                printf("--- %zu cells ---\n", n);
                free(cells);
                continue;
            }

            if (strcmp(app_argv[1], "value-space") == 0) {
                if (app_argc < 4) { printf("Usage: value-space <p> <q>\n"); continue; }
                uint64_t p = parse_u64(app_argv[2], "p");
                uint64_t q = parse_u64(app_argv[3], "q");
                U128 prod = value_space_product_u128(p, q);
                char pbuf[64];
                u128_to_str(prod, pbuf, sizeof(pbuf));
                ValueSpaceRegion reg = value_space_region(p, q);
                const char* reg_name = reg == VS_NEAR_SQUARE ? "near-square"
                                     : reg == VS_MODERATE ? "moderate" : "skewed";
                printf("value-space(%"PRIu64", %"PRIu64"):\n", p, q);
                printf("  product          = %s\n", pbuf);
                printf("  additive dist    = %"PRIu64"\n", value_space_additive_distance(p, q));
                printf("  log imbalance    = %.6f\n", value_space_log_imbalance(p, q));
                printf("  balance ratio    = %.6f\n", value_space_balance_ratio(p, q));
                printf("  region           = %s\n", reg_name);
                continue;
            }

            if (strcmp(app_argv[1], "log-space") == 0) {
                if (app_argc < 4) { printf("Usage: log-space <p> <q>\n"); continue; }
                uint64_t p = parse_u64(app_argv[2], "p");
                uint64_t q = parse_u64(app_argv[3], "q");
                double u, v;
                if (!log_space_coords(p, q, &u, &v)) { printf("log-space: invalid inputs\n"); continue; }
                printf("log-space(%"PRIu64", %"PRIu64"):\n", p, q);
                printf("  u = log(%"PRIu64") = %.6f\n", p, u);
                printf("  v = log(%"PRIu64") = %.6f\n", q, v);
                printf("  u + v            = %.6f  (= log(N))\n", u + v);
                printf("  |u - v|          = %.6f  (diagonal distance)\n", log_space_diagonal_distance(p, q));
                printf("  imbalance        = %.6f  (normalised)\n", log_space_imbalance(p, q));
                continue;
            }

            if (strcmp(app_argv[1], "mean-imbalance") == 0) {
                if (app_argc < 4) { printf("Usage: mean-imbalance <p> <q>\n"); continue; }
                uint64_t p = parse_u64(app_argv[2], "p");
                uint64_t q = parse_u64(app_argv[3], "q");
                double m, delta;
                if (!mean_imbalance_from_factors(p, q, &m, &delta)) { printf("mean-imbalance: invalid inputs\n"); continue; }
                MeanImbalanceBias bias = mean_imbalance_bias(m, delta);
                const char* bias_name = bias == MI_BIAS_NEAR_SQUARE ? "near-square"
                                      : bias == MI_BIAS_MODERATE ? "moderate"
                                      : bias == MI_BIAS_UNBALANCED ? "unbalanced"
                                      : "highly-skewed";
                double p_approx, q_approx;
                mean_imbalance_to_factors(m, delta, &p_approx, &q_approx);
                printf("mean-imbalance(%"PRIu64", %"PRIu64"):\n", p, q);
                printf("  m                = %.6f\n", m);
                printf("  delta            = %.6f\n", delta);
                printf("  |delta|/m        = %.6f\n", m > 0.0 ? fabs(delta) / m : 0.0);
                printf("  bias             = %s\n", bias_name);
                printf("  inverse approx   = (%.1f, %.1f)\n", p_approx, q_approx);
                continue;
            }

            if (strcmp(app_argv[1], "mean-imbalance-n") == 0) {
                if (app_argc < 3) { printf("Usage: mean-imbalance-n <N>\n"); continue; }
                uint64_t N = parse_u64(app_argv[2], "N");
                double m = mean_imbalance_m_from_N(N);
                printf("mean-imbalance-n(%"PRIu64"):\n", N);
                printf("  m = %.6f  (= log(%"PRIu64")/2)\n", m, N);
                continue;
            }

            if (strcmp(app_argv[1], "product-ratio") == 0) {
                if (app_argc < 4) { printf("Usage: product-ratio <p> <q>\n"); continue; }
                uint64_t p = parse_u64(app_argv[2], "p");
                uint64_t q = parse_u64(app_argv[3], "q");
                double R = ratio_from_factors(p, q);
                RatioClass rc = ratio_classify(R);
                const char* rc_name = rc == RC_BALANCED ? "balanced"
                                    : rc == RC_MODERATE ? "moderate" : "skewed";
                printf("product-ratio(%"PRIu64", %"PRIu64"):\n", p, q);
                printf("  R                = %.6f\n", R);
                printf("  class            = %s\n", rc_name);
                continue;
            }

            if (strcmp(app_argv[1], "product-ratio-bounds") == 0) {
                if (app_argc < 3) { printf("Usage: product-ratio-bounds <N>\n"); continue; }
                uint64_t N = parse_u64(app_argv[2], "N");
                double R_min, R_max;
                ratio_bounds_for_N(N, &R_min, &R_max);
                printf("product-ratio-bounds(%"PRIu64"):\n", N);
                printf("  R_min            = %.6f\n", R_min);
                printf("  R_max            = %.6f\n", R_max);
                continue;
            }

            if (strcmp(app_argv[1], "fermat-space") == 0) {
                if (app_argc < 4) { printf("Usage: fermat-space <p> <q>\n"); continue; }
                uint64_t p = parse_u64(app_argv[2], "p");
                uint64_t q = parse_u64(app_argv[3], "q");
                double A_f, B_f;
                fermat_space_from_factors(p, q, &A_f, &B_f);
                printf("fermat-space(%"PRIu64", %"PRIu64"):\n", p, q);
                printf("  A (float)        = %.1f\n", A_f);
                printf("  B (float)        = %.1f\n", B_f);
                uint64_t A_e, B_e;
                if (fermat_space_from_factors_exact(p, q, &A_e, &B_e)) {
                    printf("  A (exact)        = %"PRIu64"\n", A_e);
                    printf("  B (exact)        = %"PRIu64"\n", B_e);
                    U128 prod = u128_mul64(p, q);
                    uint64_t N64 = u128_fits_u64(prod) ? prod.lo : 0u;
                    if (N64 > 0u) {
                        printf("  verify N=A^2-B^2 = %s\n", fermat_space_verify(N64, A_e, B_e) ? "OK" : "FAIL");
                        printf("  search start     = %"PRIu64"\n", fermat_space_search_start(N64));
                        printf("  est. steps       = %"PRIu64"\n", fermat_space_estimated_steps(N64, p, q));
                    }
                } else {
                    printf("  (exact coords not available: different parity)\n");
                }
                continue;
            }

            if (strcmp(app_argv[1], "fermat-space-verify") == 0) {
                if (app_argc < 5) { printf("Usage: fermat-space-verify <N> <A> <B>\n"); continue; }
                uint64_t N = parse_u64(app_argv[2], "N");
                uint64_t A = parse_u64(app_argv[3], "A");
                uint64_t B = parse_u64(app_argv[4], "B");
                bool ok = fermat_space_verify(N, A, B);
                printf("fermat-space-verify(%"PRIu64", %"PRIu64", %"PRIu64") = %s\n", N, A, B, ok ? "VALID" : "INVALID");
                if (ok) {
                    printf("  p = A - B = %"PRIu64"\n", A - B);
                    printf("  q = A + B = %"PRIu64"\n", A + B);
                }
                continue;
            }

            if (strcmp(app_argv[1], "fermat-space-start") == 0) {
                if (app_argc < 3) { printf("Usage: fermat-space-start <N>\n"); continue; }
                uint64_t N = parse_u64(app_argv[2], "N");
                uint64_t start = fermat_space_search_start(N);
                printf("fermat-space-start(%"PRIu64") = %"PRIu64"  (ceil(sqrt(N)))\n", N, start);
                continue;
            }

            if (strcmp(app_argv[1], "minmax-space") == 0) {
                if (app_argc < 4) { printf("Usage: minmax-space <p> <q>\n"); continue; }
                uint64_t p = parse_u64(app_argv[2], "p");
                uint64_t q = parse_u64(app_argv[3], "q");
                uint64_t s, l;
                minmax_canonicalize(p, q, &s, &l);
                printf("minmax-space(%"PRIu64", %"PRIu64"):\n", p, q);
                printf("  s (min)          = %"PRIu64"\n", s);
                printf("  l (max)          = %"PRIu64"\n", l);
                printf("  covered(s<=13)   = %s\n", minmax_is_covered(s, 13u) ? "yes" : "no");
                printf("  covered(s<=100)  = %s\n", minmax_is_covered(s, 100u) ? "yes" : "no");
                printf("  covered(s<=1000) = %s\n", minmax_is_covered(s, 1000u) ? "yes" : "no");
                continue;
            }

            if (strcmp(app_argv[1], "minmax-coverage") == 0) {
                if (app_argc < 4) { printf("Usage: minmax-coverage <trial_limit> <N_max>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t trial_limit = parse_u64(app_argv[2], "trial_limit");
                uint64_t N_max = parse_u64(app_argv[3], "N_max");
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                double t0 = now_sec();
                uint64_t count = minmax_coverage_count(qs, trial_limit, N_max);
                double t1 = now_sec();
                printf("minmax-coverage(trial<=%"PRIu64", N<=%"PRIu64"):\n", trial_limit, N_max);
                printf("  covered semiprimes = %"PRIu64" (DB-bounded lower bound)\n", count);
                printf("  (%.6fs)\n", t1 - t0);
                continue;
            }

            if (strcmp(app_argv[1], "minmax-uncovered") == 0) {
                if (app_argc < 3) { printf("Usage: minmax-uncovered <trial_limit>\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t trial_limit = parse_u64(app_argv[2], "trial_limit");
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                CPSSQueryStatus status = CPSS_OK;
                uint64_t boundary = minmax_first_uncovered(qs, trial_limit, &status);
                printf("minmax-uncovered(trial_limit=%"PRIu64"):\n", trial_limit);
                if (status == CPSS_OK && boundary > 0u) {
                    printf("  first uncovered N = %"PRIu64"  (= next_prime(%"PRIu64")^2)\n", boundary, trial_limit);
                } else {
                    printf("  unavailable (%s)\n", cpss_status_name(status));
                }
                continue;
            }

            /* ── space-metrics [family] <name> ── */
            if (strcmp(app_argv[1], "space-metrics") == 0) {
                if (app_argc < 3) {
                    printf("Usage: space-metrics [family] <space-name>\n"
                           "  Families: prime-pair (default), semiprime\n"
                           "  Examples: space-metrics value\n"
                           "            space-metrics prime-pair fermat\n"
                           "            space-metrics semiprime observation\n");
                    continue;
                }
                /* Check if argv[2] is a family name */
                SpaceFamily fam = family_resolve(app_argv[2]);
                if (fam == FAMILY_SEMIPRIME) {
                    /* SemiPrime family: use dedicated registry */
                    if (app_argc < 4) {
                        printf("Missing space name after 'semiprime'. Available SemiPrime spaces:\n");
                        semiprime_list_spaces();
                        continue;
                    }
                    const SemiPrimeSpaceEntry* ssp = semiprime_space_lookup(app_argv[3]);
                    if (!ssp) {
                        printf("Unknown SemiPrime space '%s'. Available SemiPrime spaces:\n", app_argv[3]);
                        semiprime_list_spaces();
                        continue;
                    }
                    semiprime_print_metrics(ssp);
                    continue;
                }
                /* PrimePair family (explicit or bare name) */
                SpaceFamily pfam; int consumed;
                const SpaceEntry* sp = resolve_family_space(&app_argv[2], app_argc - 2, &pfam, &consumed);
                if (!sp) continue;
                const char* sn = sp->name;
                printf("%s %s space metrics:\n", family_display_name(pfam), sn);
                if (strcmp(sn, "index") == 0) {
                    printf("  i, j          prime indices (1-based)\n"
                           "  p, q          prime values\n"
                           "  N             product (U128)\n"
                           "  Stats: min/max i,j; diagonal/off-diagonal count\n");
                } else if (strcmp(sn, "value") == 0) {
                    printf("  additive_distance   |p - q|\n"
                           "  log_imbalance       |log(p) - log(q)|\n"
                           "  balance_ratio       min(p,q) / max(p,q)  [0..1]\n"
                           "  region              near-square / moderate / skewed\n"
                           "  Stats: min/max/mean for distance, ratio; counts by region\n");
                } else if (strcmp(sn, "log") == 0) {
                    printf("  u, v            log(p), log(q)\n"
                           "  u+v             = log(N), constant-product line\n"
                           "  |u-v|           diagonal distance (multiplicative imbalance)\n"
                           "  imbalance       |u-v| / (u+v), normalised [0..1)\n"
                           "  Stats: min/max/mean for |u-v|, imbalance\n");
                } else if (strcmp(sn, "mean-imbalance") == 0) {
                    printf("  m               (u+v)/2 = log(N)/2, fixed for given N\n"
                           "  delta           (v-u)/2, the unknown imbalance\n"
                           "  |delta|/m       normalised imbalance ratio\n"
                           "  bias            near-square / moderate / unbalanced / highly-skewed\n"
                           "  Stats: min/max/mean for delta, |delta|/m; counts by bias\n");
                } else if (strcmp(sn, "product-ratio") == 0) {
                    printf("  R               max(p,q) / min(p,q), always >= 1\n"
                           "  class           balanced (R<2) / moderate (R<100) / skewed (R>=100)\n"
                           "  Stats: min/max/mean R; counts by class\n");
                } else if (strcmp(sn, "fermat") == 0) {
                    printf("  A               (p+q)/2\n"
                           "  B               (q-p)/2; B=0 => perfect square\n"
                           "  search_start    ceil(sqrt(N)), where Fermat search begins\n"
                           "  est_steps       A - search_start (0 for near-square)\n"
                           "  exact           yes if both factors same parity (B is integer)\n"
                           "  Stats: min/max/mean B, steps; exact vs half-integer count\n");
                } else if (strcmp(sn, "minmax") == 0) {
                    printf("  s               min(p, q), the smaller factor\n"
                           "  l               max(p, q), the larger factor\n"
                           "  cov13           covered by trial division up to 13?\n"
                           "  cov100          covered by trial division up to 100?\n"
                           "  Stats: min/max/mean s,l; coverage counts\n");
                } else {
                    printf("  (no detailed metrics available for this space yet)\n");
                }
                continue;
            }

            /* ── Unified space command: space [family] <space-name> ... ── */
            if (strcmp(app_argv[1], "space") == 0) {
                if (app_argc < 4) {
                    printf("Usage:\n"
                           "  space [family] <space-name> <region> <args...>   (Prime-Pair)\n"
                           "  space semiprime <space-name> <N> [flags]         (SemiPrime)\n"
                           "\n  Families: prime-pair (default), semiprime\n"
                           "  Prime-Pair Spaces: index, value, log, mean-imbalance, product-ratio, fermat, minmax\n"
                           "  SemiPrime Spaces:  observation, coverage, bounds, routing\n"
                           "  Regions (Prime-Pair): point, row, col, rect, rect-unique, diagonal, band, triangle, window\n"
                           "\n  Examples:\n"
                           "    space value diagonal 4 8\n"
                           "    space prime-pair fermat triangle 4 6\n"
                           "    space semiprime observation 143\n"
                           "    space semiprime bounds 143 --auto\n");
                    continue;
                }

                /* Check if argv[2] is a family name */
                SpaceFamily fam = family_resolve(app_argv[2]);
                if (fam == FAMILY_SEMIPRIME) {
                    /* SemiPrime per-N report path */
                    if (app_argc < 4) {
                        printf("Missing space name. Available SemiPrime spaces:\n");
                        semiprime_list_spaces();
                        continue;
                    }
                    const SemiPrimeSpaceEntry* ssp = semiprime_space_lookup(app_argv[3]);
                    if (!ssp) {
                        printf("Unknown SemiPrime space '%s'. Available SemiPrime spaces:\n", app_argv[3]);
                        semiprime_list_spaces();
                        continue;
                    }
                    if (app_argc < 5) {
                        printf("Usage: space semiprime %s <N> [flags]\n"
                               "  SemiPrime spaces take <N>, not regions.\n", ssp->name);
                        continue;
                    }
                    ParsedNum pn = parse_numeric(app_argv[4], "N");
                    CPSSStream* qs = (app_db_open || app_multi_db_open)
                        ? (app_db_open ? &app_db : &app_multi_db.streams[0]) : NULL;
                    /* Pass remaining args (after N) as extra flags */
                    ssp->dispatch(qs, &pn, &app_argv[5], app_argc - 5);
                    continue;
                }

                /* PrimePair path (explicit family or bare name) */
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                int consumed;
                const SpaceEntry* sp = resolve_family_space(&app_argv[2], app_argc - 2, &fam, &consumed);
                if (!sp) continue;
                int rbase = 2 + consumed;
                if (rbase >= app_argc) {
                    printf("Missing region. Use: point, row, col, rect, rect-unique, diagonal, band, triangle, window\n");
                    continue;
                }
                RegionParams rp;
                if (!parse_region(app_argv, app_argc, rbase, &rp)) continue;
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                IndexSpaceCell* cells = NULL;
                size_t n = region_fetch(qs, &rp, &cells);
                if (cells) {
                    char hdr[128];
                    snprintf(hdr, sizeof(hdr), "%s %s", family_display_name(fam), sp->name);
                    region_render(hdr, &rp, cells, n, sp->printer);
                    free(cells);
                }
                continue;
            }

            /* ── space-compare: same region, multiple spaces ── */
            if (strcmp(app_argv[1], "space-compare") == 0) {
                if (app_argc < 4) {
                    printf("Usage: space-compare <region> <args...> --spaces <comma-list>\n"
                           "  Example: space-compare diagonal 4 8 --spaces index,value,fermat\n");
                    continue;
                }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                /* Find --spaces flag */
                int spaces_idx = -1;
                for (int ai = 2; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--spaces") == 0 && ai + 1 < app_argc) { spaces_idx = ai + 1; break; }
                }
                if (spaces_idx < 0) { printf("Missing --spaces flag.\n"); continue; }
                /* Parse region from argv[2..spaces_idx-2] */
                const char* region = app_argv[2];
                RegionParams rp; memset(&rp, 0, sizeof(rp));
                bool parsed = true;
                int ra = 3; /* first region arg index */
                if (strcmp(region, "point") == 0 && ra + 1 < spaces_idx - 1) {
                    rp.type = REGION_POINT; rp.a = parse_u64(app_argv[ra], "i"); rp.b = parse_u64(app_argv[ra+1], "j");
                } else if (strcmp(region, "row") == 0 && ra + 2 < spaces_idx - 1) {
                    rp.type = REGION_ROW; rp.a = parse_u64(app_argv[ra], "i"); rp.b = parse_u64(app_argv[ra+1], "j_lo"); rp.c = parse_u64(app_argv[ra+2], "j_hi");
                } else if (strcmp(region, "col") == 0 && ra + 2 < spaces_idx - 1) {
                    rp.type = REGION_COL; rp.a = parse_u64(app_argv[ra], "j"); rp.b = parse_u64(app_argv[ra+1], "i_lo"); rp.c = parse_u64(app_argv[ra+2], "i_hi");
                } else if (strcmp(region, "rect") == 0 && ra + 3 < spaces_idx - 1) {
                    rp.type = REGION_RECT; rp.a = parse_u64(app_argv[ra], "i_lo"); rp.b = parse_u64(app_argv[ra+1], "i_hi"); rp.c = parse_u64(app_argv[ra+2], "j_lo"); rp.d = parse_u64(app_argv[ra+3], "j_hi");
                } else if (strcmp(region, "rect-unique") == 0 && ra + 3 < spaces_idx - 1) {
                    rp.type = REGION_RECT_UNIQUE; rp.a = parse_u64(app_argv[ra], "i_lo"); rp.b = parse_u64(app_argv[ra+1], "i_hi"); rp.c = parse_u64(app_argv[ra+2], "j_lo"); rp.d = parse_u64(app_argv[ra+3], "j_hi");
                } else if (strcmp(region, "diagonal") == 0 && ra + 1 < spaces_idx - 1) {
                    rp.type = REGION_DIAGONAL; rp.a = parse_u64(app_argv[ra], "i_lo"); rp.b = parse_u64(app_argv[ra+1], "i_hi");
                } else if (strcmp(region, "band") == 0 && ra + 2 < spaces_idx - 1) {
                    rp.type = REGION_BAND; rp.offset = (int64_t)_strtoi64(app_argv[ra], NULL, 10); rp.a = parse_u64(app_argv[ra+1], "i_lo"); rp.b = parse_u64(app_argv[ra+2], "i_hi");
                } else if (strcmp(region, "triangle") == 0 && ra + 1 < spaces_idx - 1) {
                    rp.type = REGION_TRIANGLE; rp.a = parse_u64(app_argv[ra], "i_lo"); rp.b = parse_u64(app_argv[ra+1], "i_hi");
                } else if (strcmp(region, "window") == 0 && ra + 2 < spaces_idx - 1) {
                    rp.type = REGION_WINDOW; rp.a = parse_u64(app_argv[ra], "i"); rp.b = parse_u64(app_argv[ra+1], "j"); rp.c = parse_u64(app_argv[ra+2], "radius");
                } else {
                    printf("Unknown region '%s' or insufficient args.\n", region);
                    parsed = false;
                }
                if (parsed) {
                    CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                    IndexSpaceCell* cells = NULL;
                    size_t n = region_fetch(qs, &rp, &cells);
                    if (cells) {
                        /* Parse comma-separated space list */
                        char space_buf[256];
                        strncpy(space_buf, app_argv[spaces_idx], sizeof(space_buf) - 1u);
                        space_buf[sizeof(space_buf) - 1u] = '\0';
                        char* ctx = NULL;
                        char* tok = strtok_s(space_buf, ",", &ctx);
                        while (tok) {
                            const SpaceEntry* sp = space_lookup(tok);
                            if (sp) {
                                region_render(sp->name, &rp, cells, n, sp->printer);
                                printf("\n");
                            } else {
                                printf("Unknown space '%s', skipping.\n\n", tok);
                            }
                            tok = strtok_s(NULL, ",", &ctx);
                        }
                        free(cells);
                    }
                }
                continue;
            }

            /* ── Phase 3: space-stats ── */
            if (strcmp(app_argv[1], "space-stats") == 0) {
                if (app_argc < 5) {
                    printf("Usage: space-stats <space> <region> <args...>  (Prime-Pair only)\n"
                           "  SemiPrime spaces use per-N reports: space semiprime <name> <N>\n"); continue;
                }
                /* Reject SemiPrime family early with honest message (before DB check) */
                if (family_resolve(app_argv[2]) == FAMILY_SEMIPRIME) {
                    printf("SemiPrime spaces use per-N reports, not region statistics.\n"
                           "Use: space semiprime <name> <N> [flags]\n");
                    continue;
                }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                SpaceFamily fam; int consumed;
                const SpaceEntry* se = resolve_family_space(&app_argv[2], app_argc - 2, &fam, &consumed);
                if (!se) continue;
                SpaceStatsPrinter sp = family_stats_lookup(fam, se);
                if (!sp) { printf("No stats available for %s %s.\n", family_display_name(fam), se->name); continue; }
                int rbase = 2 + consumed;
                RegionParams rp;
                if (!parse_region(app_argv, app_argc, rbase, &rp)) continue;
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                IndexSpaceCell* cells = NULL;
                size_t n = region_fetch(qs, &rp, &cells);
                if (cells) {
                    char hdr[128];
                    snprintf(hdr, sizeof(hdr), "%s %s", family_display_name(fam), se->name);
                    region_print_header(hdr, &rp);
                    sp(cells, n);
                    free(cells);
                }
                continue;
            }

            /* ── Phase 3: space-stats-compare ── */
            if (strcmp(app_argv[1], "space-stats-compare") == 0) {
                if (app_argc < 4) {
                    printf("Usage: space-stats-compare <region> <args...> --spaces <comma-list>\n"); continue;
                }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                int spaces_idx = -1;
                for (int ai = 2; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--spaces") == 0 && ai + 1 < app_argc) { spaces_idx = ai + 1; break; }
                }
                if (spaces_idx < 0) { printf("Missing --spaces flag.\n"); continue; }
                RegionParams rp;
                if (!parse_region(app_argv, app_argc, 2, &rp)) continue;
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                IndexSpaceCell* cells = NULL;
                size_t n = region_fetch(qs, &rp, &cells);
                if (cells) {
                    char space_buf[256];
                    strncpy(space_buf, app_argv[spaces_idx], sizeof(space_buf) - 1u);
                    space_buf[sizeof(space_buf) - 1u] = '\0';
                    char* ctx = NULL;
                    char* tok = strtok_s(space_buf, ",", &ctx);
                    while (tok) {
                        const SpaceEntry* se = space_lookup(tok);
                        SpaceStatsPrinter sp = se ? family_stats_lookup(FAMILY_PRIMEPAIR, se) : NULL;
                        if (sp) {
                            char hdr[128];
                            snprintf(hdr, sizeof(hdr), "Prime-Pair %s", tok);
                            region_print_header(hdr, &rp);
                            sp(cells, n);
                            printf("\n");
                        } else {
                            printf("Unknown space '%s', skipping.\n\n", tok);
                        }
                        tok = strtok_s(NULL, ",", &ctx);
                    }
                    free(cells);
                }
                continue;
            }

            /* ── Phase 3/5/13: factor-explain (tiered) ── */
            if (strcmp(app_argv[1], "factor-explain") == 0) {
                if (app_argc < 3) { printf("Usage: factor-explain <N>\n"); continue; }
                ParsedNum pn = parse_numeric(app_argv[2], "N");
                CPSSStream* qs = (app_db_open || app_multi_db_open)
                    ? (app_db_open ? &app_db : &app_multi_db.streams[0]) : NULL;
                if (pn.tier == TIER_U64) {
                    factor_explain_v2(qs, pn.u64);
                } else {
                    char nbuf[512]; bn_to_str(&pn.bn, nbuf, sizeof(nbuf));
                    printf("factor-explain(%s): [%s, %u bits]\n", nbuf, tier_name(pn.tier), pn.bits);

                    /* 1. Environment */
                    printf("  environment:\n");
                    printf("    tier           = %s (%u bits)\n", tier_name(pn.tier), pn.bits);
                    printf("    DB loaded      = %s\n", qs ? "yes" : "no");
                    printf("    trial/p-1/p+1  = unavailable (DB cannot cover sqrt of %u-bit N)\n", pn.bits);
                    printf("    wheel/fermat/rho/ecm = always available\n");

                    /* 2. Observables */
                    ObservableFeatureVecBN obs;
                    observable_compute_bn(&pn.bn, &obs);
                    printf("  observables:\n");
                    printf("    near_square    = %.4f\n", obs.near_square_score);
                    printf("    perfect_square = %s\n", obs.is_perfect_square ? "yes" : "no");
                    printf("    wheel_divides  = %s\n", obs.divisible_by_wheel_prime ? "yes" : "no");

                    /* Near-square gap (exact BigNum) */
                    {
                        BigNum s; bn_isqrt_ceil(&s, &pn.bn);
                        BigNum s2; bn_mul(&s2, &s, &s);
                        BigNum gap; bn_copy(&gap, &s2); bn_sub(&gap, &pn.bn);
                        char gbuf[512]; bn_to_str(&gap, gbuf, sizeof(gbuf));
                        printf("    near-square gap = %s\n", gbuf);
                    }

                    /* 3. Factor-shape bounds (inline difference-bounds) */
                    printf("  factor-shape bounds:\n");
                    if (!obs.is_perfect_square) {
                        BigNum a0; bn_isqrt_ceil(&a0, &pn.bn);
                        BigNum a0_sq; bn_mul(&a0_sq, &a0, &a0);
                        BigNum r0; bn_copy(&r0, &a0_sq); bn_sub(&r0, &pn.bn);
                        double r0_d = bn_fits_u64(&r0) ? (double)bn_to_u64(&r0) : pow(2.0, (double)bn_bitlen(&r0));
                        double delta_lower = 2.0 * sqrt(r0_d);
                        printf("    Delta >= ~%.4f  (baseline square-gap)\n", delta_lower);
                        /* Wheel exclusion upper bound */
                        if (!obs.divisible_by_wheel_prime) {
                            uint64_t min_L = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u;
                            printf("    p >= %"PRIu64"  (wheel-exclusion)\n", min_L);
                        }
                    } else {
                        printf("    perfect square => Delta=0, R=1\n");
                    }

                    /* 4. Routing (method-specific reasoning) */
                    printf("  routing:\n");
                    const char* bn_route = observable_route_suggest_bn(&obs);
                    printf("    suggested      = %s\n", bn_route);
                    printf("  reasoning:\n");
                    if (obs.divisible_by_wheel_prime) {
                        printf("    wheel: has wheel-prime factor => instant.\n");
                    } else {
                        printf("    wheel: no wheel factor => eliminated.\n");
                    }
                    printf("    trial: unavailable (DB cannot cover sqrt of %u-bit N).\n", pn.bits);
                    printf("    pminus1: unavailable (requires DB).\n");
                    printf("    pplus1: unavailable (requires DB).\n");
                    if (obs.near_square_score > 0.8)
                        printf("    fermat: ns_score=%.3f (>0.8) => strongly favoured.\n", obs.near_square_score);
                    else if (obs.near_square_score > 0.5)
                        printf("    fermat: ns_score=%.3f (>0.5) => moderate chance.\n", obs.near_square_score);
                    else
                        printf("    fermat: ns_score=%.3f => unlikely to help at %u bits.\n", obs.near_square_score, pn.bits);
                    printf("    rho: always available, general-purpose.\n");
                    if (pn.bits > 128u)
                        printf("    ecm: primary method for %u-bit inputs with small factors.\n", pn.bits);
                    else
                        printf("    ecm: available, expensive but powerful.\n");

                    /* 5. Attempt factorisation */
                    printf("  factorisation:\n");
                    FactorResultBigNum fr;
                    if (qs) {
                        fr = cpss_factor_auto_bn(qs, &pn.bn);
                    } else {
                        fr = cpss_factor_rho_bn(&pn.bn, 0u, 0u);
                        if (!fr.factor_found) fr = cpss_factor_fermat_bn(&pn.bn, 0u);
                        if (!fr.factor_found) fr = cpss_factor_ecm_bn(&pn.bn, 0u, 0u, 0u);
                    }
                    printf("    method         = %s\n", fr.method ? fr.method : "none");
                    if (fr.factor_found) {
                        char fbuf[512], cbuf[512];
                        bn_to_str(&fr.factor, fbuf, sizeof(fbuf));
                        bn_to_str(&fr.cofactor, cbuf, sizeof(cbuf));
                        printf("    factor         = %s%s\n", fbuf, fr.factor_is_prime ? " (prime)" : "");
                        printf("    cofactor       = %s%s\n", cbuf, fr.cofactor_is_prime ? " (prime)" : "");
                        BigNum product; bn_mul(&product, &fr.factor, &fr.cofactor);
                        if (bn_cmp(&product, &pn.bn) != 0)
                            printf("    WARNING: factor * cofactor != N (internal error)\n");
                    }
                    if (fr.fully_factored)
                        printf("    factoring status = fully factored\n");
                    else if (fr.factor_found)
                        printf("    factoring status = partial (%s cofactor)\n",
                            fr.cofactor_is_prime ? "prime" : "composite");
                    else
                        printf("    factoring status = no factor found\n");

                    /* 6. Classification — derived from factoring outcome */
                    printf("  classification:\n");
                    if (fr.factor_found) {
                        printf("    composite (factored)\n");
                    } else {
                        bool prp = bn_is_probable_prime(&pn.bn, 20);
                        if (prp)
                            printf("    probable prime (Miller-Rabin, 20 witnesses)\n");
                        else
                            printf("    composite (unfactored)\n");
                    }

                    /* 7. Post-hoc: if factored, compute geometry */
                    if (fr.factor_found && fr.factor_is_prime && fr.cofactor_is_prime) {
                        printf("  post-hoc (factors known):\n");
                        if (bn_fits_u64(&fr.factor) && bn_fits_u64(&fr.cofactor)) {
                            uint64_t p = bn_to_u64(&fr.factor), q_val = bn_to_u64(&fr.cofactor);
                            if (p > q_val) { uint64_t t = p; p = q_val; q_val = t; }
                            printf("    Delta          = %"PRIu64"\n", q_val - p);
                            printf("    R              = %.4f\n", (double)q_val / (double)p);
                        } else {
                            printf("    factors too large for exact Delta/R (BigNum)\n");
                        }
                    }
                    printf("  time             = %.6fs\n", fr.time_seconds);
                }
                continue;
            }

            /* ── Phase 4/5/13: difference-bounds (tiered) ── */
            if (strcmp(app_argv[1], "difference-bounds") == 0) {
                if (app_argc < 3) {
                    printf("Usage: difference-bounds <N> [--auto] [--fermat-failed-steps K] [--min-factor-lower-bound L]\n"); continue;
                }
                ParsedNum pn = parse_numeric(app_argv[2], "N");
                int64_t fermat_K = 0;
                uint64_t min_L = 0u;
                bool have_K = false, have_L = false, use_auto = false;
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--fermat-failed-steps") == 0 && ai + 1 < app_argc) {
                        fermat_K = (int64_t)_strtoi64(app_argv[++ai], NULL, 10);
                        have_K = true;
                    }
                    else if (strcmp(app_argv[ai], "--min-factor-lower-bound") == 0 && ai + 1 < app_argc) {
                        min_L = parse_u64(app_argv[++ai], "L");
                        have_L = true;
                    }
                    else if (strcmp(app_argv[ai], "--auto") == 0) {
                        use_auto = true;
                    }
                }
                if (pn.tier == TIER_U64) {
                    uint64_t N = pn.u64;
                    if (use_auto && !have_L) {
                        FactorShapeEvidence ev = gather_auto_evidence(N);
                        if (ev.has_min_factor_lb) {
                            min_L = ev.min_factor_lb;
                            have_L = true;
                        }
                    }
                    difference_bounds(N, fermat_K, min_L, have_K, have_L);
                } else {
                    if (use_auto && !have_L) {
                        /* Wheel exclusion for BigNum */
                        bool found_wheel = false;
                        for (size_t wi = 0u; wi < ARRAY_LEN(WHEEL_PRIMES); ++wi) {
                            if (bn_mod_u64(&pn.bn, WHEEL_PRIMES[wi]) == 0u) { found_wheel = true; break; }
                        }
                        if (!found_wheel) {
                            min_L = WHEEL_PRIMES[ARRAY_LEN(WHEEL_PRIMES) - 1u] + 1u;
                            have_L = true;
                        }
                    }
                    difference_bounds_bn(&pn.bn, fermat_K, min_L, have_K, have_L);
                }
                continue;
            }

            /* ── Phase 3: space-export ── */
            if (strcmp(app_argv[1], "space-export") == 0) {
                if (app_argc < 5) {
                    printf("Usage: space-export <space> <region> <args...> <outfile>  (Prime-Pair only)\n"
                           "  SemiPrime spaces use per-N reports: space semiprime <name> <N>\n"); continue;
                }
                /* Reject SemiPrime family early with honest message (before DB check) */
                if (family_resolve(app_argv[2]) == FAMILY_SEMIPRIME) {
                    printf("SemiPrime spaces use per-N reports, not CSV export.\n"
                           "Use: space semiprime <name> <N> [flags]\n");
                    continue;
                }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                SpaceFamily fam; int consumed;
                const SpaceEntry* se = resolve_family_space(&app_argv[2], app_argc - 2, &fam, &consumed);
                if (!se) continue;
                int sidx = family_csv_lookup_idx(fam, se);
                if (sidx < 0) { printf("No CSV export available for %s %s.\n", family_display_name(fam), se->name); continue; }
                int region_start = 2 + consumed;
                if (region_start >= app_argc) { printf("Missing region.\n"); continue; }
                int rac = region_arg_count(app_argv[region_start]);
                if (rac == 0) { printf("Unknown region '%s'.\n", app_argv[region_start]); continue; }
                int outfile_idx = region_start + rac;
                if (outfile_idx >= app_argc) { printf("Missing output file.\n"); continue; }
                RegionParams rp;
                if (!parse_region(app_argv, app_argc, region_start, &rp)) continue;
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                IndexSpaceCell* cells = NULL;
                size_t n = region_fetch(qs, &rp, &cells);
                if (cells) {
                    FILE* fp = fopen(app_argv[outfile_idx], "w");
                    if (!fp) { printf("Cannot open '%s' for writing.\n", app_argv[outfile_idx]); free(cells); continue; }
                    CSV_HEADERS[sidx](fp);
                    for (size_t k = 0u; k < n; ++k) CSV_ROWS[sidx](fp, &cells[k]);
                    fclose(fp);
                    printf("exported %zu cells to %s (%s %s space)\n", n, app_argv[outfile_idx], family_display_name(fam), se->name);
                    free(cells);
                }
                continue;
            }

            /* ── Phase 5: corpus-generate ── */
            if (strcmp(app_argv[1], "corpus-generate") == 0) {
                if (app_argc < 4 || strcmp(app_argv[2], "semiprime") != 0) {
                    printf("Usage: corpus-generate semiprime <outfile> [--cases N] [--bits B] [--seed S]\n"
                           "  [--shape balanced|skewed|banded|mixed|rsa-like|no-wheel-factor|pminus1-smooth|pminus1-hostile]\n");
                    continue;
                }
                const char* outpath = app_argv[3];
                int cg_cases = 100; int cg_bits = 32; uint64_t cg_seed = 42u; const char* cg_shape = "mixed";
                for (int ai = 4; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--cases") == 0 && ai+1 < app_argc) cg_cases = (int)parse_u64(app_argv[++ai], "cases");
                    else if (strcmp(app_argv[ai], "--bits") == 0 && ai+1 < app_argc) cg_bits = (int)parse_u64(app_argv[++ai], "bits");
                    else if (strcmp(app_argv[ai], "--seed") == 0 && ai+1 < app_argc) cg_seed = parse_u64(app_argv[++ai], "seed");
                    else if (strcmp(app_argv[ai], "--shape") == 0 && ai+1 < app_argc) cg_shape = app_argv[++ai];
                }
                if (cg_bits > 62) {
                    corpus_generate_semiprime_bn(outpath, cg_cases, cg_bits, cg_seed, cg_shape);
                } else {
                    corpus_generate_semiprime(outpath, cg_cases, cg_bits, cg_seed, cg_shape);
                }
                continue;
            }

            /* ── Phase 12a: corpus-validate ── */
            if (strcmp(app_argv[1], "corpus-validate") == 0) {
                if (app_argc < 3) {
                    printf("Usage: corpus-validate <input.csv> [--B1 N]\n"); continue;
                }
                const char* cv_path = app_argv[2];
                uint64_t cv_B1 = 0u; /* 0 = default 1000000 */
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--B1") == 0 && ai+1 < app_argc) cv_B1 = parse_u64(app_argv[++ai], "B1");
                }
                CPSSStream* qs = (app_db_open || app_multi_db_open)
                    ? (app_db_open ? &app_db : &app_multi_db.streams[0]) : NULL;
                if (!qs) { printf("No database loaded. corpus-validate requires DB for prime iteration.\n"); continue; }
                corpus_validate(qs, cv_path, cv_B1);
                continue;
            }

            /* ── Phase 12b: method-compete ── */
            if (strcmp(app_argv[1], "method-compete") == 0) {
                int mc_cases = 20; int mc_bits = 32; uint64_t mc_seed = 42u;
                const char* mc_shape = "mixed"; const char* mc_csv = NULL;
                uint64_t mc_B1 = 0u; uint64_t mc_trial_limit = 0u; double mc_timeout = 5.0;
                for (int ai = 2; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--cases") == 0 && ai+1 < app_argc) mc_cases = (int)parse_u64(app_argv[++ai], "cases");
                    else if (strcmp(app_argv[ai], "--bits") == 0 && ai+1 < app_argc) mc_bits = (int)parse_u64(app_argv[++ai], "bits");
                    else if (strcmp(app_argv[ai], "--seed") == 0 && ai+1 < app_argc) mc_seed = parse_u64(app_argv[++ai], "seed");
                    else if (strcmp(app_argv[ai], "--shape") == 0 && ai+1 < app_argc) mc_shape = app_argv[++ai];
                    else if (strcmp(app_argv[ai], "--B1") == 0 && ai+1 < app_argc) mc_B1 = parse_u64(app_argv[++ai], "B1");
                    else if (strcmp(app_argv[ai], "--trial-limit") == 0 && ai+1 < app_argc) mc_trial_limit = parse_u64(app_argv[++ai], "trial-limit");
                    else if (strcmp(app_argv[ai], "--method-timeout") == 0 && ai+1 < app_argc) mc_timeout = (double)parse_u64(app_argv[++ai], "timeout");
                    else if (strcmp(app_argv[ai], "--csv-out") == 0 && ai+1 < app_argc) mc_csv = app_argv[++ai];
                }
                CPSSStream* qs = (app_db_open || app_multi_db_open)
                    ? (app_db_open ? &app_db : &app_multi_db.streams[0]) : NULL;
                if (mc_bits > 62) {
                    method_compete_bn(qs, mc_cases, mc_bits, mc_seed, mc_shape, mc_csv);
                } else {
                    method_compete(qs, mc_cases, mc_bits, mc_seed, mc_shape, mc_B1, mc_trial_limit, mc_timeout, mc_csv);
                }
                continue;
            }

            /* ── Phase 5/9: router-eval ── */
            if (strcmp(app_argv[1], "router-eval") == 0) {
                int re_cases = 20; int re_bits = 32; uint64_t re_seed = 42u; const char* re_shape = "mixed";
                const char* re_csv = NULL;
                for (int ai = 2; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--cases") == 0 && ai+1 < app_argc) re_cases = (int)parse_u64(app_argv[++ai], "cases");
                    else if (strcmp(app_argv[ai], "--bits") == 0 && ai+1 < app_argc) re_bits = (int)parse_u64(app_argv[++ai], "bits");
                    else if (strcmp(app_argv[ai], "--seed") == 0 && ai+1 < app_argc) re_seed = parse_u64(app_argv[++ai], "seed");
                    else if (strcmp(app_argv[ai], "--shape") == 0 && ai+1 < app_argc) re_shape = app_argv[++ai];
                    else if (strcmp(app_argv[ai], "--csv-out") == 0 && ai+1 < app_argc) re_csv = app_argv[++ai];
                }
                CPSSStream* qs = (app_db_open || app_multi_db_open)
                    ? (app_db_open ? &app_db : &app_multi_db.streams[0]) : NULL;
                if (re_bits > 62) {
                    printf("  NOTE: router-eval at %d bits uses BigNum method-compete (no ideal/available prediction).\n", re_bits);
                    method_compete_bn(qs, re_cases, re_bits, re_seed, re_shape, re_csv);
                } else {
                    router_eval(qs, re_cases, re_bits, re_seed, re_shape, re_csv);
                }
                continue;
            }

            /* ── Phase 5/9: metric-eval ── */
            if (strcmp(app_argv[1], "metric-eval") == 0) {
                int me_cases = 20; int me_bits = 32; uint64_t me_seed = 42u; const char* me_shape = "mixed";
                MetricGroupMode me_group = METRIC_GROUP_WINNER;
                for (int ai = 2; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--cases") == 0 && ai+1 < app_argc) me_cases = (int)parse_u64(app_argv[++ai], "cases");
                    else if (strcmp(app_argv[ai], "--bits") == 0 && ai+1 < app_argc) me_bits = (int)parse_u64(app_argv[++ai], "bits");
                    else if (strcmp(app_argv[ai], "--seed") == 0 && ai+1 < app_argc) me_seed = parse_u64(app_argv[++ai], "seed");
                    else if (strcmp(app_argv[ai], "--shape") == 0 && ai+1 < app_argc) me_shape = app_argv[++ai];
                    else if (strcmp(app_argv[ai], "--group-by") == 0 && ai+1 < app_argc) {
                        ++ai;
                        if (strcmp(app_argv[ai], "corpus") == 0) me_group = METRIC_GROUP_CORPUS;
                        else if (strcmp(app_argv[ai], "observed") == 0) me_group = METRIC_GROUP_OBSERVED;
                        else if (strcmp(app_argv[ai], "shape") == 0) me_group = METRIC_GROUP_OBSERVED; /* compat alias */
                        else if (strcmp(app_argv[ai], "winner") == 0) me_group = METRIC_GROUP_WINNER;
                        else if (strcmp(app_argv[ai], "ideal") == 0) me_group = METRIC_GROUP_IDEAL;
                        else if (strcmp(app_argv[ai], "available") == 0) me_group = METRIC_GROUP_AVAILABLE;
                        else printf("Unknown --group-by mode '%s'. Using 'winner'.\n", app_argv[ai]);
                    }
                }
                CPSSStream* qs = (app_db_open || app_multi_db_open)
                    ? (app_db_open ? &app_db : &app_multi_db.streams[0]) : NULL;
                if (me_bits > 62) {
                    printf("  metric-eval: --bits %d exceeds uint64 range.\n", me_bits);
                    printf("  Use method-compete --bits %d for BigNum competition.\n", me_bits);
                } else {
                    metric_eval(qs, me_cases, me_bits, me_seed, me_shape, me_group);
                }
                continue;
            }

            /* ── Phase 8/9: router-eval-file ── */
            if (strcmp(app_argv[1], "router-eval-file") == 0) {
                if (app_argc < 3) { printf("Usage: router-eval-file <input.csv> [--limit N] [--csv-out <file>]\n"); continue; }
                const char* inpath = app_argv[2];
                int limit = 0;
                const char* ref_csv = NULL;
                for (int ai = 3; ai < app_argc; ++ai) {
                    if (strcmp(app_argv[ai], "--limit") == 0 && ai+1 < app_argc) limit = (int)parse_u64(app_argv[++ai], "limit");
                    else if (strcmp(app_argv[ai], "--csv-out") == 0 && ai+1 < app_argc) ref_csv = app_argv[++ai];
                }
                CPSSStream* qs = (app_db_open || app_multi_db_open)
                    ? (app_db_open ? &app_db : &app_multi_db.streams[0]) : NULL;
                router_eval_file(qs, inpath, limit, ref_csv);
                continue;
            }

            /* ── Bounded views for all hidden-factor-space modules ──
             * Compact dispatch: match "<space>-row-range", "<space>-col-range",
             * "<space>-rect", "<space>-rect-unique" for each of the 6 spaces.
             * All use IndexSpace as the common cell enumerator.
             */
            {
                static const struct {
                    const char* prefix;
                    SpaceCellPrinter printer;
                } space_table[] = {
                    { "value-space",     print_cell_value_space },
                    { "log-space",       print_cell_log_space },
                    { "mean-imbalance",  print_cell_mean_imbalance },
                    { "product-ratio",   print_cell_product_ratio },
                    { "fermat-space",    print_cell_fermat_space },
                    { "minmax-space",    print_cell_minmax_space },
                };
                static const struct { const char* suffix; int mode; int min_args; } mode_table[] = {
                    { "-row-range",   0, 5 },
                    { "-col-range",   1, 5 },
                    { "-rect-unique", 3, 6 },
                    { "-rect",        2, 6 },
                };
                bool handled = false;
                for (size_t si = 0u; si < ARRAY_LEN(space_table) && !handled; ++si) {
                    for (size_t mi = 0u; mi < ARRAY_LEN(mode_table) && !handled; ++mi) {
                        char cmd[64];
                        snprintf(cmd, sizeof(cmd), "%s%s", space_table[si].prefix, mode_table[mi].suffix);
                        if (strcmp(app_argv[1], cmd) == 0) {
                            if (app_argc < mode_table[mi].min_args) {
                                if (mode_table[mi].mode <= 1)
                                    printf("Usage: %s <%s> <%s> <%s>\n", cmd,
                                        mode_table[mi].mode == 0 ? "i" : "j",
                                        mode_table[mi].mode == 0 ? "j_lo" : "i_lo",
                                        mode_table[mi].mode == 0 ? "j_hi" : "i_hi");
                                else
                                    printf("Usage: %s <i_lo> <i_hi> <j_lo> <j_hi>\n", cmd);
                                handled = true; break;
                            }
                            if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); handled = true; break; }
                            CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                            uint64_t a = parse_u64(app_argv[2], "arg1");
                            uint64_t b = parse_u64(app_argv[3], "arg2");
                            uint64_t c_val = parse_u64(app_argv[4], "arg3");
                            uint64_t d_val = (mode_table[mi].mode >= 2 && app_argc >= 6) ? parse_u64(app_argv[5], "arg4") : 0u;
                            bounded_view_dispatch(qs, space_table[si].prefix, mode_table[mi].mode,
                                a, b, c_val, d_val, space_table[si].printer);
                            handled = true;
                        }
                    }
                }
                if (handled) continue;
            }

            /* ── Overlay Space commands ── */

            if (strcmp(app_argv[1], "residue-classes") == 0) {
                if (app_argc < 3) { printf("Usage: residue-classes <M>\n"); continue; }
                uint32_t M = (uint32_t)parse_u64(app_argv[2], "M");
                if (M == 0u || M > 10000u) { printf("M must be in [1, 10000]\n"); continue; }
                uint32_t* classes = (uint32_t*)xmalloc(M * sizeof(uint32_t));
                size_t count = 0u;
                residue_valid_classes(M, classes, &count);
                printf("residue-classes(mod %"PRIu32"): %zu coprime classes\n", M, count);
                printf("  surviving fraction = %.4f  (%.1f%% of grid eliminated)\n",
                    residue_surviving_fraction(M), (1.0 - residue_surviving_fraction(M)) * 100.0);
                printf("  classes: ");
                for (size_t k = 0u; k < count; ++k) {
                    if (k > 0u) printf(", ");
                    printf("%"PRIu32, classes[k]);
                    if (k >= 30u && k < count - 1u) { printf(", ... (%zu more)", count - k - 1u); break; }
                }
                printf("\n");
                free(classes);
                continue;
            }

            if (strcmp(app_argv[1], "residue-grid") == 0) {
                if (app_argc < 4) { printf("Usage: residue-grid <N> <M>\n"); continue; }
                uint64_t N = parse_u64(app_argv[2], "N");
                uint32_t M = (uint32_t)parse_u64(app_argv[3], "M");
                if (M == 0u || M > 10000u) { printf("M must be in [1, 10000]\n"); continue; }
                uint32_t* classes = (uint32_t*)xmalloc(M * sizeof(uint32_t));
                size_t grid_size = 0u;
                residue_valid_classes(M, classes, &grid_size);
                if (grid_size > 100u) { printf("Grid too large (%zu classes). Use M <= ~200.\n", grid_size); free(classes); continue; }
                uint8_t* grid = (uint8_t*)xmalloc(grid_size * grid_size);
                size_t survivors = residue_eliminate_for_N(N, M, classes, grid_size, grid);
                printf("residue-grid(N=%"PRIu64", mod %"PRIu32"): %zu of %zu cells survive\n",
                    N, M, survivors, grid_size * grid_size);
                if (grid_size <= 20u) {
                    printf("     ");
                    for (size_t j = 0u; j < grid_size; ++j) printf("%4"PRIu32, classes[j]);
                    printf("\n");
                    for (size_t i = 0u; i < grid_size; ++i) {
                        printf("%4"PRIu32" ", classes[i]);
                        for (size_t j = 0u; j < grid_size; ++j) {
                            printf("  %c ", grid[i * grid_size + j] ? 'X' : '.');
                        }
                        printf("\n");
                    }
                }
                free(grid);
                free(classes);
                continue;
            }

            if (strcmp(app_argv[1], "smoothness") == 0) {
                if (app_argc < 3) { printf("Usage: smoothness <p> [B]\n"); continue; }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                uint64_t p = parse_u64(app_argv[2], "p");
                uint64_t B = 1000000u;
                if (app_argc >= 4) B = parse_u64(app_argv[3], "B");
                CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                double pm1_score = smoothness_pminus1_friendly(qs, p);
                SmoothnessClass cls = smoothness_classify(pm1_score);
                const char* cls_name = cls == SMOOTH_HIGH ? "HIGH" : cls == SMOOTH_MEDIUM ? "MEDIUM" : "LOW";
                double score = smoothness_score_u64(qs, p > 1u ? p - 1u : 0u, B);
                printf("smoothness(%"PRIu64"):\n", p);
                printf("  p-1 friendliness = %.6f  (%s)\n", pm1_score, cls_name);
                printf("  p-1 B-smooth(%"PRIu64") = %.4f\n", B, score);
                continue;
            }

            if (strcmp(app_argv[1], "observable") == 0) {
                if (app_argc < 3) { printf("Usage: observable <N>\n"); continue; }
                ParsedNum pn = parse_numeric(app_argv[2], "N");
                if (pn.tier == TIER_U64) {
                    if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }
                    CPSSStream* qs = app_db_open ? &app_db : &app_multi_db.streams[0];
                    ObservableFeatureVec vec;
                    observable_compute(qs, pn.u64, &vec);
                    const char* suggestion;
                    if (vec.is_prime) { suggestion = "prime"; }
                    else {
                        RouterEnv obs_env = router_env_from_stream(qs);
                        CoverageMethod obs_best = predict_available(pn.u64, &obs_env);
                        suggestion = (obs_best < COV_METHOD_COUNT) ? MNAMES[obs_best] : "auto";
                    }
                    printf("observable(%"PRIu64"):\n", pn.u64);
                    printf("  tier               = uint64\n");
                    printf("  is_prime           = %s\n", vec.is_prime ? "yes" : "no");
                    printf("  is_perfect_square  = %s\n", vec.is_perfect_square ? "yes" : "no");
                    printf("  wheel_prime_divides = %s\n", vec.divisible_by_wheel_prime ? "yes" : "no");
                    printf("  near_square_score  = %.6f\n", vec.near_square_score);
                    printf("  small_factor_ev    = %.6f\n", vec.small_factor_evidence);
                    printf("  cpss_coverage      = %.4f  (%s)\n", vec.cpss_coverage_ratio,
                        vec.cpss_coverage_complete ? "complete" : "partial");
                    printf("  route suggestion   = %s\n", suggestion);
                } else {
                    char nbuf[512]; bn_to_str(&pn.bn, nbuf, sizeof(nbuf));
                    ObservableFeatureVecBN vec;
                    observable_compute_bn(&pn.bn, &vec);
                    const char* suggestion = observable_route_suggest_bn(&vec);
                    printf("observable(%s): [%s, %u bits]\n", nbuf, tier_name(pn.tier), pn.bits);
                    printf("  is_probable_prime  = %s\n", vec.is_probable_prime ? "yes" : "no");
                    printf("  is_perfect_square  = %s\n", vec.is_perfect_square ? "yes" : "no");
                    printf("  wheel_prime_divides = %s\n", vec.divisible_by_wheel_prime ? "yes" : "no");
                    printf("  near_square_score  = %.6f\n", vec.near_square_score);
                    printf("  small_factor_ev    = %.6f\n", vec.small_factor_evidence);
                    printf("  cpss_coverage      = n/a  (DB cannot cover sqrt of %u-bit input)\n", pn.bits);
                    printf("  route suggestion   = %s\n", suggestion);
                }
                continue;
            }

            if (strcmp(app_argv[1], "coverage-sim") == 0) {
                if (app_argc < 3) { printf("Usage: coverage-sim <N>\n"); continue; }
                ParsedNum pn = parse_numeric(app_argv[2], "N");
                CPSSStream* qs = (app_db_open || app_multi_db_open)
                    ? (app_db_open ? &app_db : &app_multi_db.streams[0]) : NULL;
                RouterEnv env = router_env_from_stream(qs);

                if (pn.tier == TIER_U64) {
                    uint64_t N = pn.u64;
                    /* Ideal priors (full DB assumed) */
                    CoverageState ideal_st;
                    coverage_init(&ideal_st, N);
                    CoverageMethod ideal_best = coverage_best_next(&ideal_st);

                    /* Available priors (current environment) */
                    CoverageState avail_st;
                    coverage_init(&avail_st, N);
                    if (!env.trial_db_available) avail_st.methods[COV_METHOD_TRIAL].plausibility = 0.0;
                    if (!env.pminus1_available)  avail_st.methods[COV_METHOD_PMINUS1].plausibility = 0.0;
                    if (!env.pplus1_available)   avail_st.methods[COV_METHOD_PPLUS1].plausibility = 0.0;
                    if (!env.db_loaded) {
                        CoverageFeatures f = coverage_extract_features(N);
                        if (!f.has_wheel_div && !f.is_even) {
                            avail_st.methods[COV_METHOD_RHO].plausibility += 0.20;
                            if (f.ns_score < 0.7)
                                avail_st.methods[COV_METHOD_FERMAT].plausibility *= 0.5;
                        }
                    }
                    CoverageMethod avail_best = coverage_best_next(&avail_st);

                    printf("coverage-sim(%"PRIu64"):\n", N);
                    router_env_print(&env);
                    printf("  ideal priors (assumes full DB):\n");
                    for (int mi = 0; mi < COV_METHOD_COUNT; ++mi) {
                        printf("    %-10s plausibility=%.2f  cost=%.2f  ratio=%.1f\n",
                            MNAMES[mi], ideal_st.methods[mi].plausibility,
                            COV_DEFAULT_COST[mi],
                            COV_DEFAULT_COST[mi] > 0.0 ? ideal_st.methods[mi].plausibility / COV_DEFAULT_COST[mi] : 0.0);
                    }
                    printf("  ideal best         = %s\n", ideal_best < COV_METHOD_COUNT ? MNAMES[ideal_best] : "none");
                    printf("  available priors (current environment):\n");
                    for (int mi = 0; mi < COV_METHOD_COUNT; ++mi) {
                        const char* tag = "";
                        if (avail_st.methods[mi].plausibility <= 0.001 && ideal_st.methods[mi].plausibility > 0.01)
                            tag = "  [unavailable]";
                        printf("    %-10s plausibility=%.2f  cost=%.2f  ratio=%.1f%s\n",
                            MNAMES[mi], avail_st.methods[mi].plausibility,
                            COV_DEFAULT_COST[mi],
                            COV_DEFAULT_COST[mi] > 0.0 ? avail_st.methods[mi].plausibility / COV_DEFAULT_COST[mi] : 0.0,
                            tag);
                    }
                    printf("  available best     = %s\n", avail_best < COV_METHOD_COUNT ? MNAMES[avail_best] : "none");

                    /* Simulate: wheel fails on the available state */
                    coverage_mark_tried(&avail_st, COV_METHOD_WHEEL, 0.001, false, 1u);
                    CoverageMethod after_wheel = coverage_best_next(&avail_st);
                    printf("  after wheel fail   = %s\n",
                        after_wheel < COV_METHOD_COUNT ? MNAMES[after_wheel] : "none");
                } else {
                    /* BigNum coverage-sim: compute priors from BigNum-safe features */
                    char nbuf[512]; bn_to_str(&pn.bn, nbuf, sizeof(nbuf));
                    printf("coverage-sim(%s): [%s, %u bits]\n", nbuf, tier_name(pn.tier), pn.bits);
                    router_env_print(&env);

                    ObservableFeatureVecBN obs;
                    observable_compute_bn(&pn.bn, &obs);

                    /* Build priors manually from BigNum features */
                    double p_wheel = obs.divisible_by_wheel_prime ? 0.95 : 0.0;
                    double p_trial = 0.0;    /* DB cannot cover sqrt of large N */
                    double p_fermat = obs.is_perfect_square ? 0.90
                                    : obs.near_square_score > 0.8 ? 0.70
                                    : obs.near_square_score > 0.5 ? 0.35 : 0.10;
                    double p_rho = 0.50;
                    double p_pminus1 = 0.0;  /* DB-backed: unavailable for BigNum */
                    double p_pplus1 = 0.0;   /* DB-backed: unavailable for BigNum */
                    double p_ecm = (pn.bits > 128u) ? 0.60 : 0.40;

                    /* For very large inputs, ECM is the primary method */
                    if (pn.bits > 256u) { p_ecm = 0.70; p_rho = 0.40; p_fermat *= 0.5; }

                    printf("  NOTE: DB-backed methods (trial, p-1, p+1) unavailable for %u-bit input.\n", pn.bits);
                    printf("  available priors:\n");
                    printf("    %-10s plausibility=%.2f  cost=%.2f  %s\n", "wheel", p_wheel, COV_DEFAULT_COST[0],
                        p_wheel > 0.01 ? "" : "[no wheel factor]");
                    printf("    %-10s plausibility=%.2f  cost=%.2f  [unavailable: DB cannot cover sqrt]\n", "trial", p_trial, COV_DEFAULT_COST[1]);
                    printf("    %-10s plausibility=%.2f  cost=%.2f  ns_score=%.4f\n", "fermat", p_fermat, COV_DEFAULT_COST[2], obs.near_square_score);
                    printf("    %-10s plausibility=%.2f  cost=%.2f\n", "rho", p_rho, COV_DEFAULT_COST[3]);
                    printf("    %-10s plausibility=%.2f  cost=%.2f  [unavailable: no DB]\n", "pminus1", p_pminus1, COV_DEFAULT_COST[4]);
                    printf("    %-10s plausibility=%.2f  cost=%.2f  [unavailable: no DB]\n", "pplus1", p_pplus1, COV_DEFAULT_COST[5]);
                    printf("    %-10s plausibility=%.2f  cost=%.2f%s\n", "ecm", p_ecm, COV_DEFAULT_COST[6],
                        pn.bits > 128u ? "  [boosted for large input]" : "");

                    /* Best available */
                    const char* best = "rho";
                    double best_ratio = p_rho / COV_DEFAULT_COST[3];
                    if (p_wheel / COV_DEFAULT_COST[0] > best_ratio && p_wheel > 0.01) { best = "wheel"; best_ratio = p_wheel / COV_DEFAULT_COST[0]; }
                    if (p_fermat / COV_DEFAULT_COST[2] > best_ratio) { best = "fermat"; best_ratio = p_fermat / COV_DEFAULT_COST[2]; }
                    if (p_ecm / COV_DEFAULT_COST[6] > best_ratio) { best = "ecm"; best_ratio = p_ecm / COV_DEFAULT_COST[6]; }
                    printf("  available best     = %s\n", best);
                }
                continue;
            }

            /* ── Sidecar index commands ── */

            if (strcmp(app_argv[1], "build-index") == 0) {
                if (app_argc < 3) { printf("Usage: build-index <cpss_file>\n"); continue; }
                CPSSStream tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.file_handle = INVALID_HANDLE_VALUE;
                cpss_open(&tmp, app_argv[2]);
                cpss_build_segment_index(&tmp);
                cpss_build_prime_index(&tmp);
                char sidecar[1024];
                cpssi_path_for(app_argv[2], sidecar, sizeof(sidecar));
                double t0 = now_sec();
                int rc = cpssi_write(&tmp, sidecar);
                double t1 = now_sec();
                if (rc == 0) {
                    printf("built sidecar: %s (%zu segments, %.2fs)\n", sidecar, tmp.index_len, t1 - t0);
                }
                cpss_close(&tmp);
                continue;
            }

            if (strcmp(app_argv[1], "rebuild-index") == 0) {
                if (app_argc < 3) { printf("Usage: rebuild-index <cpss_file>\n"); continue; }
                char sidecar[1024];
                cpssi_path_for(app_argv[2], sidecar, sizeof(sidecar));
                /* Delete existing sidecar so it won't be loaded during build */
                remove(sidecar);
                CPSSStream tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.file_handle = INVALID_HANDLE_VALUE;
                cpss_open(&tmp, app_argv[2]);
                cpss_build_segment_index(&tmp);
                cpss_build_prime_index(&tmp);
                double t0 = now_sec();
                int rc = cpssi_write(&tmp, sidecar);
                double t1 = now_sec();
                if (rc == 0) {
                    printf("rebuilt sidecar: %s (%zu segments, %.2fs)\n", sidecar, tmp.index_len, t1 - t0);
                }
                cpss_close(&tmp);
                continue;
            }

            if (strcmp(app_argv[1], "verify-index") == 0) {
                if (app_argc < 3) { printf("Usage: verify-index <cpss_file>\n"); continue; }
                char sidecar[1024];
                cpssi_path_for(app_argv[2], sidecar, sizeof(sidecar));
                /* Delete cached index so we get a fresh file scan */
                CPSSStream tmp;
                memset(&tmp, 0, sizeof(tmp));
                tmp.file_handle = INVALID_HANDLE_VALUE;
                cpss_open(&tmp, app_argv[2]);
                /* Force file scan (don't use sidecar for verification target) */
                /* We need to temporarily hide the sidecar path */
                char* saved_path = tmp.path;
                tmp.path = NULL; /* prevent sidecar fast-path */
                cpss_build_segment_index(&tmp);
                tmp.path = saved_path;
                cpssi_verify(&tmp, sidecar);
                cpss_close(&tmp);
                continue;
            }

            if (strcmp(app_argv[1], "build-index-db") == 0) {
                if (app_argc < 3) { printf("Usage: build-index-db <shard_directory>\n"); continue; }
                /* Enumerate shard files and build sidecars for each */
                char pattern[1024];
                snprintf(pattern, sizeof(pattern), "%s\\PrimeStateV4_*.bin", app_argv[2]);
                WIN32_FIND_DATAA fd;
                HANDLE hFind = FindFirstFileA(pattern, &fd);
                if (hFind == INVALID_HANDLE_VALUE) {
                    snprintf(pattern, sizeof(pattern), "%s\\CompressedPrimeStateStreamV4_*.bin", app_argv[2]);
                    hFind = FindFirstFileA(pattern, &fd);
                }
                if (hFind == INVALID_HANDLE_VALUE) {
                    printf("No shard files found in %s\n", app_argv[2]);
                    continue;
                }
                size_t built = 0u;
                double t0 = now_sec();
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    char full[1024];
                    snprintf(full, sizeof(full), "%s\\%s", app_argv[2], fd.cFileName);

                    CPSSStream tmp;
                    memset(&tmp, 0, sizeof(tmp));
                    tmp.file_handle = INVALID_HANDLE_VALUE;
                    cpss_open(&tmp, full);
                    cpss_build_segment_index(&tmp);
                    cpss_build_prime_index(&tmp);

                    char sidecar[1024];
                    cpssi_path_for(full, sidecar, sizeof(sidecar));
                    if (cpssi_write(&tmp, sidecar) == 0) {
                        printf("  built: %s (%zu segments)\n", fd.cFileName, tmp.index_len);
                        ++built;
                    }
                    cpss_close(&tmp);
                } while (FindNextFileA(hFind, &fd));
                FindClose(hFind);
                double t1 = now_sec();
                printf("built %zu sidecars in %.2fs\n", built, t1 - t0);
                continue;
            }

            /* ── Manifest / catalog commands ── */

            if (strcmp(app_argv[1], "build-manifest") == 0) {
                if (app_argc < 3) { printf("Usage: build-manifest <shard_directory>\n"); continue; }
                /* Open the DB (via directory scan), then write manifest from it */
                CPSSDatabase tmp_db;
                memset(&tmp_db, 0, sizeof(tmp_db));
                double t0 = now_sec();
                int rc = cpss_db_open(&tmp_db, app_argv[2]);
                if (rc != 0) { printf("Failed to scan shard directory.\n"); continue; }
                rc = manifest_build_from_db(&tmp_db, app_argv[2]);
                double t1 = now_sec();
                if (rc == 0) {
                    printf("built manifest: %s\\" MANIFEST_FILENAME " (%zu shards, %.2fs)\n",
                        app_argv[2], tmp_db.shard_count, t1 - t0);
                }
                cpss_db_close(&tmp_db);
                continue;
            }

            if (strcmp(app_argv[1], "verify-manifest") == 0) {
                if (app_argc < 3) { printf("Usage: verify-manifest <shard_directory>\n"); continue; }
                ManifestLoadResult mr = manifest_load(app_argv[2]);
                if (!mr.loaded) {
                    printf("verify-manifest: cannot load manifest: %s\n", mr.reject_reason);
                    continue;
                }
                /* Compare against a live directory scan */
                CPSSDatabase tmp_db;
                memset(&tmp_db, 0, sizeof(tmp_db));
                /* Temporarily rename manifest so db_open does a fresh scan */
                char manifest_path[1024], manifest_bak[1024];
                snprintf(manifest_path, sizeof(manifest_path), "%s\\" MANIFEST_FILENAME, app_argv[2]);
                snprintf(manifest_bak, sizeof(manifest_bak), "%s\\" MANIFEST_FILENAME ".bak", app_argv[2]);
                rename(manifest_path, manifest_bak);
                int rc = cpss_db_open(&tmp_db, app_argv[2]);
                rename(manifest_bak, manifest_path);
                if (rc != 0) {
                    printf("verify-manifest: failed to scan directory for comparison.\n");
                    manifest_load_free(&mr);
                    continue;
                }
                int errors = 0;
                if (mr.shard_count != tmp_db.shard_count) {
                    printf("  MISMATCH: shard count: manifest=%zu scan=%zu\n", mr.shard_count, tmp_db.shard_count);
                    ++errors;
                }
                size_t check = mr.shard_count < tmp_db.shard_count ? mr.shard_count : tmp_db.shard_count;
                for (size_t i = 0u; i < check; ++i) {
                    if (mr.shards[i].lo != tmp_db.shard_lo[i] || mr.shards[i].hi != tmp_db.shard_hi[i]) {
                        printf("  MISMATCH: shard %zu range: manifest=%" PRIu64 "..%" PRIu64 " scan=%" PRIu64 "..%" PRIu64 "\n",
                            i, mr.shards[i].lo, mr.shards[i].hi, tmp_db.shard_lo[i], tmp_db.shard_hi[i]);
                        ++errors;
                    }
                }
                cpss_db_close(&tmp_db);
                manifest_load_free(&mr);
                if (errors == 0) printf("verify-manifest: OK (%zu shards match)\n", check);
                else printf("verify-manifest: FAILED (%d mismatches)\n", errors);
                continue;
            }

            if (strcmp(app_argv[1], "db-coverage") == 0) {
                if (!app_db_open && !app_multi_db_open) {
                    printf("No database loaded. Use 'load <file>' or 'load-db <dir>'.\n");
                    continue;
                }
                if (app_multi_db_open) {
                    manifest_print_coverage(&app_multi_db);
                }
                else {
                    printf("coverage (single stream):\n");
                    if (app_db.index_len > 0u) {
                        printf("  range              = %" PRIu64 " .. %" PRIu64 "\n",
                            segment_start_n(&app_db.index_entries[0]),
                            segment_end_n(&app_db.index_entries[app_db.index_len - 1u]));
                        printf("  segments           = %zu\n", app_db.index_len);
                    }
                }
                continue;
            }

            if (strcmp(app_argv[1], "db-gaps") == 0) {
                if (!app_multi_db_open) { printf("No multi-shard database loaded.\n"); continue; }
                size_t gaps = 0u;
                for (size_t i = 1u; i < app_multi_db.shard_count; ++i) {
                    if (app_multi_db.shard_lo[i] > app_multi_db.shard_hi[i - 1u] + 1u) {
                        printf("gap [%zu-%zu]: %" PRIu64 " .. %" PRIu64 "\n",
                            i - 1u, i, app_multi_db.shard_hi[i - 1u] + 1u, app_multi_db.shard_lo[i] - 1u);
                        ++gaps;
                    }
                }
                if (gaps == 0u) printf("No gaps between shards.\n");
                else printf("%zu gap(s) found.\n", gaps);
                continue;
            }

            if (strcmp(app_argv[1], "db-overlaps") == 0) {
                if (!app_multi_db_open) { printf("No multi-shard database loaded.\n"); continue; }
                size_t overlaps = 0u;
                for (size_t i = 1u; i < app_multi_db.shard_count; ++i) {
                    if (app_multi_db.shard_lo[i] <= app_multi_db.shard_hi[i - 1u]) {
                        printf("overlap [%zu-%zu]: %" PRIu64 " .. %" PRIu64 "\n",
                            i - 1u, i, app_multi_db.shard_lo[i], app_multi_db.shard_hi[i - 1u]);
                        ++overlaps;
                    }
                }
                if (overlaps == 0u) printf("No overlaps between shards.\n");
                else printf("%zu overlap(s) found.\n", overlaps);
                continue;
            }

            /* ── Batch pipeline commands ── */

            if (strcmp(app_argv[1], "query-file") == 0) {
                if (app_argc < 5) {
                    printf("Usage: query-file <command> <input> <output> [--input-format auto|txt|csv] [--output-format csv|jsonl]\n");
                    printf("  Commands: is-prime, next-prime, prev-prime, classify, count-range\n");
                    continue;
                }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }

                const char* sub_cmd = app_argv[2];
                const char* in_path = app_argv[3];
                const char* out_path = app_argv[4];
                BatchInputFormat in_fmt;
                BatchOutputFormat out_fmt;
                batch_parse_format_args(app_argc, app_argv, 5, &in_fmt, &out_fmt);

                CPSSStream* qs = app_db_open ? &app_db : NULL;
                CPSSDatabase* qdb = app_multi_db_open ? &app_multi_db : NULL;

                BatchCommand cmd;
                if (strcmp(sub_cmd, "is-prime") == 0) cmd = BATCH_CMD_IS_PRIME;
                else if (strcmp(sub_cmd, "next-prime") == 0) cmd = BATCH_CMD_NEXT_PRIME;
                else if (strcmp(sub_cmd, "prev-prime") == 0) cmd = BATCH_CMD_PREV_PRIME;
                else if (strcmp(sub_cmd, "classify") == 0) cmd = BATCH_CMD_CLASSIFY;
                else if (strcmp(sub_cmd, "count-range") == 0) cmd = BATCH_CMD_COUNT_RANGE;
                else {
                    printf("Unknown query-file command: %s\n", sub_cmd);
                    continue;
                }

                printf("query-file %s:\n", sub_cmd);
                batch_run_pipeline(in_path, out_path, cmd, in_fmt, out_fmt, qs, qdb);
                continue;
            }

            if (strcmp(app_argv[1], "factor-file") == 0) {
                if (app_argc < 4) {
                    printf("Usage: factor-file <input> <output> [--input-format auto|txt|csv] [--output-format csv|jsonl]\n");
                    continue;
                }
                if (!app_db_open && !app_multi_db_open) { printf("No database loaded.\n"); continue; }

                const char* in_path = app_argv[2];
                const char* out_path = app_argv[3];
                BatchInputFormat in_fmt;
                BatchOutputFormat out_fmt;
                batch_parse_format_args(app_argc, app_argv, 4, &in_fmt, &out_fmt);

                CPSSStream* qs = app_db_open ? &app_db : NULL;
                CPSSDatabase* qdb = app_multi_db_open ? &app_multi_db : NULL;

                printf("factor-file:\n");
                batch_run_pipeline(in_path, out_path, BATCH_CMD_FACTOR, in_fmt, out_fmt, qs, qdb);
                continue;
            }

            /* ── Benchmark commands ── */
            if (strcmp(app_argv[1], "benchmark-fermat") == 0
                || strcmp(app_argv[1], "benchmark-factor-fermat") == 0
                || strcmp(app_argv[1], "benchmark-fermat-factor") == 0) {
                cmd_bench_fermat(NULL, app_argc, app_argv, 0u, UINT64_MAX);
                continue;
            }
            if (strcmp(app_argv[1], "benchmark-rho") == 0) {
                cmd_bench_rho(NULL, app_argc, app_argv, 0u, UINT64_MAX);
                continue;
            }
            if (strcmp(app_argv[1], "benchmark-ecm") == 0) {
                cmd_bench_ecm(NULL, app_argc, app_argv, 0u, UINT64_MAX);
                continue;
            }
            if (strcmp(app_argv[1], "benchmark-everything") == 0) {
                CPSSStream* bench_s = NULL;
                uint64_t bench_lo = 0u, bench_hi = UINT64_MAX;

                if (app_db_open || app_multi_db_open) {
                    bench_s = app_db_open ? &app_db : &app_multi_db.streams[0];
                    for (int bi = 2; bi < app_argc; ++bi) {
                        if (strcmp(app_argv[bi], "--mode") == 0 && bi + 1 < app_argc) {
                            const char* bm = app_argv[bi + 1];
                            if (strcmp(bm, "cold") == 0) {
                                cpss_build_segment_index(bench_s);
                                if (app_cold_seg_end < bench_s->index_len) {
                                    bench_lo = segment_start_n(&bench_s->index_entries[app_cold_seg_start]);
                                    bench_hi = segment_end_n(&bench_s->index_entries[app_cold_seg_end]);
                                }
                                printf("  [benchmark mode: cold, range %" PRIu64 "..%" PRIu64 "]\n", bench_lo, bench_hi);
                            }
                            else if (strcmp(bm, "hot") == 0) {
                                cpss_build_segment_index(bench_s);
                                if (app_hot_seg_end < bench_s->index_len) {
                                    bench_lo = segment_start_n(&bench_s->index_entries[app_hot_seg_start]);
                                    bench_hi = segment_end_n(&bench_s->index_entries[app_hot_seg_end]);
                                }
                                printf("  [benchmark mode: hot, range %" PRIu64 "..%" PRIu64 "]\n", bench_lo, bench_hi);
                            }
                            break;
                        }
                    }
                }

                cmd_bench_everything(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                continue;
            }

            /* Parse --mode cold|hot for benchmark range constraint */
            if (app_argc >= 2 && strncmp(app_argv[1], "benchmark-", 10) == 0) {
                if (!app_db_open && !app_multi_db_open) {
                    printf("No database loaded.\n"); continue;
                }
                CPSSStream* bench_s = app_db_open ? &app_db : &app_multi_db.streams[0];

                /* Determine constrain range from --mode cold|hot */
                uint64_t bench_lo = 0u, bench_hi = UINT64_MAX;
                for (int bi = 2; bi < app_argc; ++bi) {
                    if (strcmp(app_argv[bi], "--mode") == 0 && bi + 1 < app_argc) {
                        const char* bm = app_argv[bi + 1];
                        if (strcmp(bm, "cold") == 0) {
                            /* Restrict to cold-loaded segment range */
                            cpss_build_segment_index(bench_s);
                            if (app_cold_seg_end < bench_s->index_len) {
                                bench_lo = segment_start_n(&bench_s->index_entries[app_cold_seg_start]);
                                bench_hi = segment_end_n(&bench_s->index_entries[app_cold_seg_end]);
                            }
                            printf("  [benchmark mode: cold, range %" PRIu64 "..%" PRIu64 "]\n", bench_lo, bench_hi);
                        }
                        else if (strcmp(bm, "hot") == 0) {
                            /* Restrict to hot-cached segment range */
                            cpss_build_segment_index(bench_s);
                            if (app_hot_seg_end < bench_s->index_len) {
                                bench_lo = segment_start_n(&bench_s->index_entries[app_hot_seg_start]);
                                bench_hi = segment_end_n(&bench_s->index_entries[app_hot_seg_end]);
                            }
                            printf("  [benchmark mode: hot, range %" PRIu64 "..%" PRIu64 "]\n", bench_lo, bench_hi);
                        }
                        break;
                    }
                }

                if (strcmp(app_argv[1], "benchmark-is-prime") == 0) {
                    cmd_bench_is_prime(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-count-range") == 0) {
                    cmd_bench_count_range(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-list-range") == 0) {
                    cmd_bench_list_range(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-mixed") == 0) {
                    cmd_bench_mixed(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-semiprime-factorisation") == 0) {
                    cmd_bench_semiprime_factorisation(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-next-prime") == 0) {
                    cmd_bench_next_prime(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-prev-prime") == 0) {
                    cmd_bench_prev_prime(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-pi") == 0) {
                    cmd_bench_pi(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-nth-prime") == 0) {
                    cmd_bench_nth_prime(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-prime-gap") == 0) {
                    cmd_bench_prime_gap(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-primes-near") == 0) {
                    cmd_bench_primes_near(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-range-stats") == 0) {
                    cmd_bench_range_stats(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-factor-support") == 0) {
                    cmd_bench_factor_support(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-pminus1") == 0) {
                    cmd_bench_pminus1(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-pplus1") == 0) {
                    cmd_bench_pplus1(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-iter") == 0) {
                    cmd_bench_iter(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-factor-trial") == 0) {
                    cmd_bench_factor_trial(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-factor-auto") == 0) {
                    cmd_bench_factor_auto(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-classify") == 0) {
                    cmd_bench_classify(bench_s, app_argc, app_argv, bench_lo, bench_hi);
                }
                else if (strcmp(app_argv[1], "benchmark-all-sectors") == 0) {
                    cmd_benchmark_all_sectors(app_argc, app_argv);
                }
                else {
                    printf("Unknown benchmark command: %s\n", app_argv[1]);
                }
                continue;
            }

            /* ── Legacy viewer commands (pass-through) ── */

            printf("Parsed %d args:\n", app_argc);
            for (int i = 0; i < app_argc; i++) {
                printf("  argv[%d] = %s\n", i, app_argv[i]);
            }

            if (strcmp(app_argv[1], "info") == 0) {
                cmd_info(app_argc, app_argv);
            }
            else if (strcmp(app_argv[1], "list") == 0) {
                cmd_list(app_argc, app_argv);
            }
            else if (strcmp(app_argv[1], "export-primes") == 0) {
                cmd_export_primes(app_argc, app_argv);
            }
            else if (strcmp(app_argv[1], "clip") == 0) {
                cmd_clip(app_argc, app_argv);
            }
            else if (strcmp(app_argv[1], "split-trillion") == 0) {
                cmd_split_trillion(app_argc, app_argv);
            }
            else if (strcmp(app_argv[1], "benchmark") == 0) {
                cmd_benchmark(app_argc, app_argv);
            }
            else if (strcmp(app_argv[1], "decompress-file") == 0) {
                cmd_decompress_file(app_argc, app_argv);
            }
            else if (strcmp(app_argv[1], "compress-file") == 0) {
                cmd_compress_file(app_argc, app_argv);
            }
            else {
                printf("Unknown command: %s (type 'help' for commands)\n", app_argv[1]);
            }
        }

        if (app_multi_db_open) cpss_db_close(&app_multi_db);
        if (app_db_open) cpss_close(&app_db);
        hot_cache_reset();
        wheel_cache_reset();
        return 0;
    }

    print_usage(stderr);
    return 1;
}
