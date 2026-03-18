/* ── Fundamental unit computation for cubic number fields ──
 * For f(α) = α³ + a₁α + a₀ (depressed monic cubic), attempts to find
 * the fundamental unit ε of Z[α] using Minkowski-guided search.
 *
 * Norm formula: N(c₀ + c₁α + c₂α²) computed via 3×3 matrix determinant.
 * M = | c₀        c₁              c₂          |
 *     | -q·c₂     c₀-p·c₂         c₁          |
 *     | -q·c₁     -p·c₁-q·c₂      c₀-p·c₂     |
 * N(β) = det(M).
 */

/* Compute norm of c₀ + c₁α + c₂α² in Z[α]/(α³+pα+q) as signed 128-bit integer. */
static __int128 cubic_norm_i128(int64_t c0, int64_t c1, int64_t c2, int64_t p_coeff, int64_t q_coeff) {
    __int128 m00 = c0,       m01 = c1,                    m02 = c2;
    __int128 m10 = -q_coeff*c2, m11 = c0 - p_coeff*c2,    m12 = c1;
    __int128 m20 = -q_coeff*c1, m21 = -p_coeff*c1 - q_coeff*c2, m22 = c0 - p_coeff*c2;
    __int128 det = m00 * (m11*m22 - m12*m21)
                 - m01 * (m10*m22 - m12*m20)
                 + m02 * (m10*m21 - m11*m20);
    return det;
}

/* Simplified 3×3 LLL lattice reduction (Gram-Schmidt style).
 * Operates on double-precision vectors. delta = 3/4 (standard). */
static void lll_reduce_3d(double b[3][3], int dim) {
    double mu[3][3] = {{0}};
    double B[3] = {0};
    for (int iter = 0; iter < 100; ++iter) {
        double bstar[3][3];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < dim; ++j) bstar[i][j] = b[i][j];
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < i; ++j) {
                double dot_bi_bsj = 0, dot_bsj_bsj = 0;
                for (int d = 0; d < dim; ++d) { dot_bi_bsj += b[i][d] * bstar[j][d]; dot_bsj_bsj += bstar[j][d] * bstar[j][d]; }
                mu[i][j] = (dot_bsj_bsj > 1e-30) ? dot_bi_bsj / dot_bsj_bsj : 0;
                for (int d = 0; d < dim; ++d) bstar[i][d] -= mu[i][j] * bstar[j][d];
            }
            B[i] = 0; for (int d = 0; d < dim; ++d) B[i] += bstar[i][d] * bstar[i][d];
        }
        bool changed = false;
        for (int i = 1; i < 3; ++i) {
            for (int j = i - 1; j >= 0; --j) {
                if (mu[i][j] > 0.5 || mu[i][j] < -0.5) {
                    int r = (int)(mu[i][j] + (mu[i][j] > 0 ? 0.5 : -0.5));
                    for (int d = 0; d < dim; ++d) b[i][d] -= r * b[j][d];
                    changed = true;
                }
            }
        }
        for (int i = 1; i < 3; ++i) {
            double lhs = B[i];
            double rhs = (0.75 - mu[i][i-1]*mu[i][i-1]) * B[i-1];
            if (lhs < rhs - 1e-10) {
                for (int d = 0; d < dim; ++d) { double t = b[i][d]; b[i][d] = b[i-1][d]; b[i-1][d] = t; }
                changed = true;
            }
        }
        if (!changed) break;
    }
}

/* Find the fundamental unit of Q(α) where α³ + p_coeff*α + q_coeff = 0.
 * Uses Minkowski-guided search along the embedding line.
 * Returns false if no unit found within search bound. */
static bool find_fundamental_unit(int64_t p_coeff, int64_t q_coeff,
    int64_t* u0, int64_t* u1, int64_t* u2, double* real_embedding, bool diag) {
    double pd = (double)p_coeff, qd = (double)q_coeff;
    double r1 = -qd / pd;
    for (int i = 0; i < 100; ++i) {
        double fx = r1*r1*r1 + pd*r1 + qd;
        double fpx = 3.0*r1*r1 + pd;
        if (fpx == 0.0) break;
        double dx = fx / fpx;
        r1 -= dx;
        if (dx > -1e-15 && dx < 1e-15) break;
    }
    double b_q = r1, c_q = r1*r1 + pd;
    double disc = b_q*b_q - 4.0*c_q;
    double r2_re, r2_im;
    if (disc < 0) { r2_re = -b_q / 2.0; r2_im = sqrt(-disc) / 2.0; }
    else { r2_re = (-b_q + sqrt(disc)) / 2.0; r2_im = 0.0; }

    if (diag) {
        printf("  [UNIT] roots: r1=%.10f  r2=%.10f+%.10fi\n", r1, r2_re, r2_im);
    }

    /* LLL on Minkowski embedding */
    double s2 = sqrt(2.0);
    double r2_sq_re = r2_re*r2_re - r2_im*r2_im;
    double r2_sq_im = 2.0*r2_re*r2_im;
    double basis[3][3] = {
        {1.0, s2*1.0, s2*0.0},
        {r1, s2*r2_re, s2*r2_im},
        {r1*r1, s2*r2_sq_re, s2*r2_sq_im}
    };
    lll_reduce_3d(basis, 3);
    if (diag) {
        printf("  [UNIT] LLL-reduced Minkowski basis:\n");
        for (int i = 0; i < 3; ++i)
            printf("    b[%d] = (%.6f, %.6f, %.6f)\n", i, basis[i][0], basis[i][1], basis[i][2]);
    }

    /* Verify norm formula */
    if (diag) {
        __int128 n1 = cubic_norm_i128(1, 0, 0, p_coeff, q_coeff);
        __int128 na = cubic_norm_i128(0, 1, 0, p_coeff, q_coeff);
        printf("  [UNIT] norm check: N(1)=%"PRId64" (expect 1)  N(α)=%"PRId64" (expect %"PRId64")\n",
            (int64_t)n1, (int64_t)na, -q_coeff);
    }

    int64_t best_c[3] = {0, 0, 0};
    double best_log = 1e30;
    bool found = false;

    /* Minkowski-guided search: for a unit, σ₁ must be very small when c1,c2 are nonzero.
     * c0 ≈ -(c1*r1 + c2*r1²). Search along this line for each (c1,c2). */
    if (diag) printf("  [UNIT] searching along Minkowski-guided line...\n");
    for (int64_t c1 = -100000; c1 <= 100000 && !found; ++c1) {
        if (c1 == 0) continue;
        double target_c0 = -(double)c1 * r1;
        for (int64_t dc = -3; dc <= 3; ++dc) {
            int64_t c0 = (int64_t)round(target_c0) + dc;
            __int128 n = cubic_norm_i128(c0, c1, 0, p_coeff, q_coeff);
            if (n != 1 && n != -1) continue;
            if (c1 == 0 && (c0 == 1 || c0 == -1)) continue;
            double s1 = (double)c0 + (double)c1*r1;
            double log_s1 = log(s1 > 0 ? s1 : -s1);
            if (log_s1 < 0) log_s1 = -log_s1;
            if (log_s1 > 1e-10 && log_s1 < best_log) {
                best_log = log_s1;
                best_c[0] = c0; best_c[1] = c1; best_c[2] = 0;
                found = true;
                if (diag) printf("  [UNIT] found unit: ε=(%"PRId64",%"PRId64",0) N=%s1\n",
                    c0, c1, (n==1)?"":"-");
            }
        }
        for (int64_t c2 = -1; c2 <= 1; c2 += 2) {
            double target_c0_2 = -(double)c1 * r1 - (double)c2 * r1 * r1;
            for (int64_t dc = -3; dc <= 3; ++dc) {
                int64_t c0 = (int64_t)round(target_c0_2) + dc;
                __int128 n = cubic_norm_i128(c0, c1, c2, p_coeff, q_coeff);
                if (n != 1 && n != -1) continue;
                double s1 = (double)c0 + (double)c1*r1 + (double)c2*r1*r1;
                double log_s1 = log(s1 > 0 ? s1 : -s1);
                if (log_s1 < 0) log_s1 = -log_s1;
                if (log_s1 > 1e-10 && log_s1 < best_log) {
                    best_log = log_s1;
                    best_c[0] = c0; best_c[1] = c1; best_c[2] = c2;
                    found = true;
                    if (diag) printf("  [UNIT] found unit: ε=(%"PRId64",%"PRId64",%"PRId64") N=%s1\n",
                        c0, c1, c2, (n==1)?"":"-");
                }
            }
        }
    }

    if (!found) {
        if (diag) printf("  [UNIT] no fundamental unit found within search bound\n");
        return false;
    }

    *u0 = best_c[0]; *u1 = best_c[1]; *u2 = best_c[2];
    double s1_val = (double)best_c[0] + (double)best_c[1]*r1 + (double)best_c[2]*r1*r1;
    *real_embedding = s1_val;

    if (diag) {
        __int128 nn = cubic_norm_i128(best_c[0], best_c[1], best_c[2], p_coeff, q_coeff);
        printf("  [UNIT] fundamental unit: ε = %"PRId64" + %"PRId64"·α + %"PRId64"·α²\n",
            best_c[0], best_c[1], best_c[2]);
        printf("  [UNIT] Norm(ε) = %s1\n", (nn == 1) ? "" : "-");
        printf("  [UNIT] σ₁(ε) = %.15f\n", s1_val);
        printf("  [UNIT] log|σ₁(ε)| = %.15f\n", log(s1_val > 0 ? s1_val : -s1_val));
    }
    return true;
}

/* Compute per-relation unit parity: floor(log|σ₁(a-bα)| / log|σ₁(ε)|) mod 2. */
static int unit_parity(int64_t a, uint64_t b, double alpha_real, double log_sigma1_eps) {
    double sigma1 = (double)a - (double)b * alpha_real;
    if (sigma1 < 0) sigma1 = -sigma1;
    if (sigma1 < 1e-30) return 0;
    double log_val = log(sigma1);
    double k = log_val / log_sigma1_eps;
    int ki = (int)floor(k + 0.5);
    if (ki < 0) ki = -ki;
    return ki & 1;
}
