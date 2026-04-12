/**
 * nql_special_functions.c - Special mathematical functions for NQL
 * Part of the NQL mathematical expansion - Phase 10.
 *
 * Implements: Gamma, Beta, Zeta, error function (erf/erfc),
 * digamma, log-gamma, Bessel J0/J1, incomplete gamma/beta.
 */

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);
NqlValue nql_val_float(double f);
NqlValue nql_val_null(void);

/* ======================================================================
 * CORE HELPERS
 * ====================================================================== */

static double nql_sf_arg(const NqlValue* v) {
    return (v->type == NQL_VAL_INT) ? (double)v->v.ival : v->v.fval;
}

/** Lanczos approximation for ln(Gamma(x)), x > 0 */
static double nql_sf_lgamma(double x) {
    static const double c[7] = {
        1.000000000190015, 76.18009172947146, -86.50532032941677,
        24.01409824083091, -1.231739572450155, 0.001208650973866179,
        -0.000005395239384953
    };
    double y = x, tmp = x + 5.5;
    tmp -= (x + 0.5) * log(tmp);
    double ser = c[0];
    for (int j = 1; j < 7; j++) ser += c[j] / ++y;
    return -tmp + log(2.5066282746310005 * ser / x);
}

/* ======================================================================
 * GAMMA & RELATED
 * ====================================================================== */

/** GAMMA(x) -- Gamma function via exp(lgamma(x)) */
static NqlValue nql_fn_gamma(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    double x = nql_sf_arg(&args[0]);
    if (x <= 0.0 && x == (double)(int64_t)x) return nql_val_null(); /* poles */
    return nql_val_float(exp(nql_sf_lgamma(x)));
}

/** LNGAMMA(x) -- log-Gamma */
static NqlValue nql_fn_lngamma(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    double x = nql_sf_arg(&args[0]);
    if (x <= 0.0) return nql_val_null();
    return nql_val_float(nql_sf_lgamma(x));
}

/** BETA(a, b) -- B(a,b) = Gamma(a)*Gamma(b)/Gamma(a+b) */
static NqlValue nql_fn_beta(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    if (argc != 2) return nql_val_null();
    double a = nql_sf_arg(&args[0]);
    double b = nql_sf_arg(&args[1]);
    if (a <= 0.0 || b <= 0.0) return nql_val_null();
    return nql_val_float(exp(nql_sf_lgamma(a) + nql_sf_lgamma(b) - nql_sf_lgamma(a + b)));
}

/** DIGAMMA(x) -- psi function, derivative of lnGamma.
 *  Uses asymptotic series + recurrence for small x. */
static NqlValue nql_fn_digamma(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    double x = nql_sf_arg(&args[0]);
    if (x <= 0.0 && x == (double)(int64_t)x) return nql_val_null();
    double result = 0.0;
    /* Recurrence: psi(x) = psi(x+1) - 1/x */
    while (x < 8.0) {
        result -= 1.0 / x;
        x += 1.0;
    }
    /* Asymptotic series for large x */
    double inv_x2 = 1.0 / (x * x);
    result += log(x) - 0.5 / x
            - inv_x2 * (1.0/12.0 - inv_x2 * (1.0/120.0 - inv_x2 * (1.0/252.0)));
    return nql_val_float(result);
}

/* ======================================================================
 * RIEMANN ZETA
 * ====================================================================== */

/** ZETA(s) -- Riemann zeta function for real s > 1 (Euler-Maclaurin) */
static NqlValue nql_fn_zeta(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    double s = nql_sf_arg(&args[0]);
    if (s <= 1.0) return nql_val_null(); /* pole at s=1, analytic continuation not implemented */
    /* Direct summation + Euler-Maclaurin correction */
    double sum = 0.0;
    int N = 100;
    for (int n = 1; n <= N; n++)
        sum += pow((double)n, -s);
    /* Euler-Maclaurin remainder terms */
    double Ns = pow((double)N, -s);
    sum += Ns / 2.0; /* 0th correction */
    sum += Ns / ((s - 1.0) * N); /* integral remainder */
    /* First Bernoulli correction */
    sum += s * Ns / (12.0 * N);
    return nql_val_float(sum);
}

/* ======================================================================
 * ERROR FUNCTION
 * ====================================================================== */

/** ERF(x) -- error function via Abramowitz & Stegun */
static NqlValue nql_fn_erf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    double x = nql_sf_arg(&args[0]);
    double t = 1.0 / (1.0 + 0.5 * fabs(x));
    double erf_approx = 1.0 - t * exp(-x * x - 1.26551223 +
                                      t * (1.00002368 +
                                      t * (0.37409196 +
                                      t * (0.09678418 +
                                      t * (-0.18628806 +
                                      t * (0.27886807 +
                                      t * (-1.13520398 +
                                      t * (1.48851587 +
                                      t * (-0.82215223 +
                                      t * 0.17087277)))))))));
    return nql_val_float(x < 0 ? -erf_approx : erf_approx);
}

/** ERFC(x) -- complementary error function */
static NqlValue nql_fn_erfc(const NqlValue* args, uint32_t argc, void* ctx) {
    NqlValue e = nql_fn_erf(args, argc, ctx);
    if (e.type != NQL_VAL_FLOAT) return nql_val_null();
    return nql_val_float(1.0 - e.v.fval);
}

/* ======================================================================
 * BESSEL FUNCTIONS
 * ====================================================================== */

/** BESSEL_J0(x) -- Bessel function of first kind, order 0 (series) */
static NqlValue nql_fn_bessel_j0(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    double x = nql_sf_arg(&args[0]);
    double sum = 1.0, term = 1.0;
    double x2 = -0.25 * x * x;
    for (int k = 1; k < 50; k++) {
        term *= x2 / ((double)k * (double)k);
        sum += term;
        if (fabs(term) < 1e-16 * fabs(sum)) break;
    }
    return nql_val_float(sum);
}

/** BESSEL_J1(x) -- Bessel function of first kind, order 1 (series) */
static NqlValue nql_fn_bessel_j1(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD1(args);
    if (argc != 1) return nql_val_null();
    double x = nql_sf_arg(&args[0]);
    double sum = 0.5 * x, term = 0.5 * x;
    double x2 = -0.25 * x * x;
    for (int k = 1; k < 50; k++) {
        term *= x2 / ((double)k * (double)(k + 1));
        sum += term;
        if (fabs(term) < 1e-16 * fabs(sum)) break;
    }
    return nql_val_float(sum);
}

/* ======================================================================
 * INCOMPLETE GAMMA & BETA
 * ====================================================================== */

/** LOWER_GAMMA(a, x) -- regularized lower incomplete gamma P(a,x) */
static NqlValue nql_fn_lower_gamma(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    if (argc != 2) return nql_val_null();
    double a = nql_sf_arg(&args[0]);
    double x = nql_sf_arg(&args[1]);
    if (a <= 0.0 || x < 0.0) return nql_val_null();
    if (x == 0.0) return nql_val_float(0.0);
    /* Series expansion */
    double sum = 0.0, term = 1.0 / a;
    sum = term;
    for (int n = 1; n < 200; n++) {
        term *= x / (a + n);
        sum += term;
        if (fabs(term) < 1e-15 * fabs(sum)) break;
    }
    double result = sum * exp(-x + a * log(x) - nql_sf_lgamma(a));
    if (result > 1.0) result = 1.0;
    return nql_val_float(result);
}

/** UPPER_GAMMA(a, x) -- regularized upper incomplete gamma Q(a,x) = 1 - P(a,x) */
static NqlValue nql_fn_upper_gamma(const NqlValue* args, uint32_t argc, void* ctx) {
    NqlValue lg = nql_fn_lower_gamma(args, argc, ctx);
    if (lg.type != NQL_VAL_FLOAT) return nql_val_null();
    return nql_val_float(1.0 - lg.v.fval);
}

/** REG_BETA(x, a, b) -- regularized incomplete beta I_x(a,b) via continued fraction */
static NqlValue nql_fn_reg_beta(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD3(args);
    if (argc != 3) return nql_val_null();
    double x = nql_sf_arg(&args[0]);
    double a = nql_sf_arg(&args[1]);
    double b = nql_sf_arg(&args[2]);
    if (x < 0.0 || x > 1.0 || a <= 0.0 || b <= 0.0) return nql_val_null();
    if (x == 0.0) return nql_val_float(0.0);
    if (x == 1.0) return nql_val_float(1.0);
    /* Use symmetry if x > (a+1)/(a+b+2) */
    if (x > (a + 1.0) / (a + b + 2.0)) {
        NqlValue rev_args[3] = { nql_val_float(1.0 - x), nql_val_float(b), nql_val_float(a) };
        NqlValue rev = nql_fn_reg_beta(rev_args, 3, ctx);
        if (rev.type != NQL_VAL_FLOAT) return nql_val_null();
        return nql_val_float(1.0 - rev.v.fval);
    }
    /* Lentz continued fraction for I_x(a,b) */
    double front = exp(a * log(x) + b * log(1.0 - x) - nql_sf_lgamma(a) - nql_sf_lgamma(b) + nql_sf_lgamma(a + b)) / a;
    double f = 1.0, c = 1.0, d = 1.0 - (a + b) * x / (a + 1.0);
    if (fabs(d) < 1e-30) d = 1e-30;
    d = 1.0 / d; f = d;
    for (int m = 1; m <= 100; m++) {
        /* Even step */
        double am = (double)m * (b - (double)m) * x / (((a + 2.0 * m - 1.0) * (a + 2.0 * m)));
        d = 1.0 + am * d; if (fabs(d) < 1e-30) d = 1e-30; d = 1.0 / d;
        c = 1.0 + am / c; if (fabs(c) < 1e-30) c = 1e-30;
        f *= d * c;
        /* Odd step */
        am = -((a + m) * (a + b + m) * x) / ((a + 2.0 * m) * (a + 2.0 * m + 1.0));
        d = 1.0 + am * d; if (fabs(d) < 1e-30) d = 1e-30; d = 1.0 / d;
        c = 1.0 + am / c; if (fabs(c) < 1e-30) c = 1e-30;
        double delta = d * c;
        f *= delta;
        if (fabs(delta - 1.0) < 1e-12) break;
    }
    double result = front * f;
    if (result > 1.0) result = 1.0;
    if (result < 0.0) result = 0.0;
    return nql_val_float(result);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_special_functions_register(void) {
    /* Gamma family */
    nql_register_func("GAMMA",          nql_fn_gamma,        1, 1, "Gamma function");
    nql_register_func("LNGAMMA",        nql_fn_lngamma,      1, 1, "Log-Gamma ln(Gamma(x))");
    nql_register_func("BETA",           nql_fn_beta,         2, 2, "Beta function B(a,b)");
    nql_register_func("DIGAMMA",        nql_fn_digamma,      1, 1, "Digamma (psi) function");

    /* Zeta */
    nql_register_func("ZETA",           nql_fn_zeta,         1, 1, "Riemann zeta (s > 1)");

    /* Error function */
    nql_register_func("ERF",            nql_fn_erf,          1, 1, "Error function erf(x)");
    nql_register_func("ERFC",           nql_fn_erfc,         1, 1, "Complementary error function");

    /* Bessel */
    nql_register_func("BESSEL_J0",      nql_fn_bessel_j0,    1, 1, "Bessel J0(x)");
    nql_register_func("BESSEL_J1",      nql_fn_bessel_j1,    1, 1, "Bessel J1(x)");

    /* Incomplete gamma & beta */
    nql_register_func("LOWER_GAMMA",    nql_fn_lower_gamma,  2, 2, "Regularized lower incomplete gamma P(a,x)");
    nql_register_func("UPPER_GAMMA",    nql_fn_upper_gamma,  2, 2, "Regularized upper incomplete gamma Q(a,x)");
    nql_register_func("REG_BETA",       nql_fn_reg_beta,     3, 3, "Regularized incomplete beta I_x(a,b)");
}
