/**
 * LatticeSpace.c - Lattice-reduction space for factor-pair analysis.
 * Part of the CPSS Prime-Pair Spaces module.
 *
 * For N = p * q, the factoring problem can be cast as a shortest-vector
 * problem in a 2D lattice with basis vectors (1, p) and (0, N). The
 * quality of the lattice (orthogonality defect, shortest vector length)
 * predicts how amenable N is to lattice-based attacks.
 *
 * Axes:
 *   shortest_vector  - norm of the shortest non-trivial lattice vector
 *   orthogonality_defect - ratio of basis norms to det^(1/dim), >= 1.0
 *   gauss_reduced    - whether the basis is already Gauss-reduced
 *   balance_angle    - angle between basis vectors (radians)
 *
 * Depends on: <math.h> (sqrt, fabs, atan2, acos)
 */

/** Lattice analysis result for a factor pair (p, q) with N = p*q. */
typedef struct {
    double basis1_norm;          /* ||v1|| where v1 = (1, p mod N) normalised */
    double basis2_norm;          /* ||v2|| */
    double shortest_vector;      /* norm of shortest non-trivial vector after reduction */
    double orthogonality_defect; /* product of norms / det^(1/dim) — 1.0 = orthogonal */
    double balance_angle;        /* angle between reduced basis vectors (radians) */
    bool   gauss_reduced;        /* true if basis is already Gauss-reduced */
} LatticeMetrics;

/**
 * 2D Gauss lattice reduction on basis vectors (a1,a2) and (b1,b2).
 * Reduces in-place. This is the 2D specialisation of LLL.
 */
static void lattice_gauss_reduce_2d(double* a1, double* a2, double* b1, double* b2) {
    int iter = 0;
    while (iter < 200) {
        double norm_a = (*a1) * (*a1) + (*a2) * (*a2);
        double norm_b = (*b1) * (*b1) + (*b2) * (*b2);

        /* Ensure |a| <= |b| */
        if (norm_a > norm_b) {
            double t;
            t = *a1; *a1 = *b1; *b1 = t;
            t = *a2; *a2 = *b2; *b2 = t;
            double tn = norm_a; norm_a = norm_b; norm_b = tn;
        }

        /* Reduce b by a */
        double dot = (*a1) * (*b1) + (*a2) * (*b2);
        double mu = (norm_a > 1e-15) ? dot / norm_a : 0.0;
        double mu_round = (mu >= 0.0) ? (double)(int64_t)(mu + 0.5) : (double)(int64_t)(mu - 0.5);

        *b1 -= mu_round * (*a1);
        *b2 -= mu_round * (*a2);

        double new_norm_b = (*b1) * (*b1) + (*b2) * (*b2);
        if (new_norm_b >= norm_b - 1e-10) break; /* converged */
        ++iter;
    }
}

/**
 * Compute lattice metrics for the factor pair (p, q) with N = p * q.
 *
 * The natural factoring lattice for N uses basis:
 *   v1 = (1, q)    — encodes one factor
 *   v2 = (1, -p)   — encodes the other (since p + (-p) = 0 mod N alignment)
 *
 * After Gauss reduction, the shortest vector length and basis quality
 * indicate how "lattice-easy" the factorisation is.
 */
static LatticeMetrics lattice_analyse(uint64_t p, uint64_t q) {
    LatticeMetrics m;
    memset(&m, 0, sizeof(m));

    if (p == 0u || q == 0u) return m;

    /* Canonical: p <= q */
    if (p > q) { uint64_t t = p; p = q; q = t; }

    /* Basis: v1 = (1, q), v2 = (1, -p) cast to doubles */
    double a1 = 1.0, a2 = (double)q;
    double b1 = 1.0, b2 = -(double)p;

    m.basis1_norm = sqrt(a1 * a1 + a2 * a2);
    m.basis2_norm = sqrt(b1 * b1 + b2 * b2);

    /* Check if already Gauss-reduced before reduction */
    double dot_before = a1 * b1 + a2 * b2;
    double norm_a_sq = a1 * a1 + a2 * a2;
    m.gauss_reduced = (fabs(dot_before) <= 0.5 * norm_a_sq);

    /* Reduce */
    lattice_gauss_reduce_2d(&a1, &a2, &b1, &b2);

    /* Shortest vector is the smaller of the two reduced basis vectors */
    double r1 = sqrt(a1 * a1 + a2 * a2);
    double r2 = sqrt(b1 * b1 + b2 * b2);
    m.shortest_vector = (r1 < r2) ? r1 : r2;

    /* Orthogonality defect: (||v1|| * ||v2||) / |det|
     * For a 2D lattice, perfect orthogonality gives defect = 1.0.
     * det = a1*b2 - a2*b1 */
    double det = fabs(a1 * b2 - a2 * b1);
    if (det > 1e-15) {
        m.orthogonality_defect = (r1 * r2) / det;
    } else {
        m.orthogonality_defect = 1e15; /* degenerate */
    }

    /* Angle between reduced basis vectors */
    double dot = a1 * b1 + a2 * b2;
    double cos_angle = (r1 > 1e-15 && r2 > 1e-15) ? dot / (r1 * r2) : 0.0;
    if (cos_angle > 1.0) cos_angle = 1.0;
    if (cos_angle < -1.0) cos_angle = -1.0;
    m.balance_angle = acos(cos_angle);

    return m;
}

/**
 * Lattice "difficulty" score for a factor pair.
 * Higher score = harder to find via lattice methods.
 * Score = log2(shortest_vector) * orthogonality_defect.
 */
static double lattice_difficulty(uint64_t p, uint64_t q) {
    LatticeMetrics m = lattice_analyse(p, q);
    if (m.shortest_vector < 1.0) return 0.0;
    return log2(m.shortest_vector) * m.orthogonality_defect;
}

/**
 * Is the factor pair "lattice-easy"?
 * True if the shortest vector is small relative to sqrt(N).
 */
static bool lattice_is_easy(uint64_t p, uint64_t q) {
    LatticeMetrics m = lattice_analyse(p, q);
    double sqrtN = sqrt((double)p * (double)q);
    return m.shortest_vector < sqrtN * 0.1;
}
