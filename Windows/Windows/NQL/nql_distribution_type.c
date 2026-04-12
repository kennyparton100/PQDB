/**
 * nql_distribution_type.c - NQL first-class Distribution objects.
 * Part of the CPSS Viewer amalgamation.
 *
 * Wraps distribution parameters into an opaque object so PDF/CDF/quantile/sample
 * can be called generically. Builds on existing nql_distributions.c scalar functions.
 */

#include <math.h>
#include <stdlib.h>

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ======================================================================
 * NqlDistribution STRUCT
 * ====================================================================== */

typedef enum {
    NQL_DIST_NORMAL, NQL_DIST_UNIFORM, NQL_DIST_EXPONENTIAL,
    NQL_DIST_POISSON, NQL_DIST_BINOMIAL, NQL_DIST_GAMMA,
    NQL_DIST_BETA, NQL_DIST_CHI_SQUARED, NQL_DIST_STUDENT_T,
    NQL_DIST_GEOMETRIC, NQL_DIST_BERNOULLI
} NqlDistType;

struct NqlDistribution_s {
    NqlDistType type;
    double params[4];  /* up to 4 parameters */
    uint32_t nparam;
};

static NqlDistribution* nql_dist_alloc(NqlDistType t, double p0, double p1, double p2, double p3, uint32_t np) {
    NqlDistribution* d = (NqlDistribution*)malloc(sizeof(NqlDistribution));
    if (!d) return NULL;
    d->type = t; d->nparam = np;
    d->params[0] = p0; d->params[1] = p1; d->params[2] = p2; d->params[3] = p3;
    return d;
}

static NqlValue nql_val_dist(NqlDistribution* d) {
    NqlValue v; memset(&v, 0, sizeof(v));
    v.type = NQL_VAL_DISTRIBUTION; v.v.distribution = d;
    return v;
}

/* -- Helpers for PDF/CDF computation -- */

static double nql_dist_normal_pdf(double x, double mu, double sigma) {
    double z = (x - mu) / sigma;
    return exp(-0.5 * z * z) / (sigma * sqrt(2.0 * M_PI));
}

static double nql_dist_normal_cdf(double x, double mu, double sigma) {
    return 0.5 * (1.0 + erf((x - mu) / (sigma * sqrt(2.0))));
}

static double nql_dist_normal_quantile(double p, double mu, double sigma) {
    /* Rational approximation (Abramowitz & Stegun) */
    if (p <= 0) return -1e18; if (p >= 1) return 1e18;
    double t = (p < 0.5) ? sqrt(-2.0 * log(p)) : sqrt(-2.0 * log(1.0 - p));
    double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
    double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
    double z = t - (c0 + c1 * t + c2 * t * t) / (1.0 + d1 * t + d2 * t * t + d3 * t * t * t);
    if (p < 0.5) z = -z;
    return mu + sigma * z;
}

static double nql_dist_exp_pdf(double x, double lambda) { return (x < 0) ? 0 : lambda * exp(-lambda * x); }
static double nql_dist_exp_cdf(double x, double lambda) { return (x < 0) ? 0 : 1.0 - exp(-lambda * x); }

static double nql_dist_uniform_pdf(double x, double a, double b) { return (x >= a && x <= b) ? 1.0 / (b - a) : 0.0; }
static double nql_dist_uniform_cdf(double x, double a, double b) { if (x < a) return 0; if (x > b) return 1; return (x - a) / (b - a); }

/* Generic PDF dispatch */
static double nql_dist_pdf(NqlDistribution* d, double x) {
    switch (d->type) {
        case NQL_DIST_NORMAL:      return nql_dist_normal_pdf(x, d->params[0], d->params[1]);
        case NQL_DIST_UNIFORM:     return nql_dist_uniform_pdf(x, d->params[0], d->params[1]);
        case NQL_DIST_EXPONENTIAL: return nql_dist_exp_pdf(x, d->params[0]);
        case NQL_DIST_POISSON: {
            int k = (int)x; if (k < 0) return 0;
            double l = d->params[0], p = exp(-l);
            for (int i = 1; i <= k; i++) p *= l / i;
            return p;
        }
        case NQL_DIST_BINOMIAL: {
            int k = (int)x, n = (int)d->params[0]; double p = d->params[1];
            if (k < 0 || k > n) return 0;
            double r = 1.0;
            for (int i = 0; i < k; i++) r *= (double)(n - i) / (i + 1) * p;
            for (int i = 0; i < n - k; i++) r *= (1.0 - p);
            /* Recompute properly */
            r = 1.0;
            for (int i = 0; i < k; i++) r *= (double)(n - i) / (i + 1);
            r *= pow(p, k) * pow(1.0 - p, n - k);
            return r;
        }
        case NQL_DIST_GEOMETRIC: {
            int k = (int)x; if (k < 1) return 0;
            return d->params[0] * pow(1.0 - d->params[0], k - 1);
        }
        case NQL_DIST_BERNOULLI: return (x == 1.0) ? d->params[0] : (x == 0.0) ? (1.0 - d->params[0]) : 0.0;
        case NQL_DIST_CHI_SQUARED: {
            double k2 = d->params[0] / 2.0;
            if (x <= 0) return 0;
            return pow(x, k2 - 1) * exp(-x / 2.0) / (pow(2.0, k2) * tgamma(k2));
        }
        case NQL_DIST_GAMMA: {
            double a = d->params[0], b = d->params[1];
            if (x <= 0) return 0;
            return pow(x, a - 1) * exp(-x / b) / (pow(b, a) * tgamma(a));
        }
        case NQL_DIST_BETA: {
            double a = d->params[0], b = d->params[1];
            if (x <= 0 || x >= 1) return 0;
            return pow(x, a - 1) * pow(1 - x, b - 1) * tgamma(a + b) / (tgamma(a) * tgamma(b));
        }
        default: return 0;
    }
}

/* Generic CDF dispatch */
static double nql_dist_cdf(NqlDistribution* d, double x) {
    switch (d->type) {
        case NQL_DIST_NORMAL:      return nql_dist_normal_cdf(x, d->params[0], d->params[1]);
        case NQL_DIST_UNIFORM:     return nql_dist_uniform_cdf(x, d->params[0], d->params[1]);
        case NQL_DIST_EXPONENTIAL: return nql_dist_exp_cdf(x, d->params[0]);
        case NQL_DIST_POISSON: {
            int k = (int)x; if (k < 0) return 0;
            double s = 0; for (int i = 0; i <= k; i++) s += nql_dist_pdf(d, (double)i);
            return s;
        }
        case NQL_DIST_BINOMIAL: {
            int k = (int)x; if (k < 0) return 0; if (k >= (int)d->params[0]) return 1.0;
            double s = 0; for (int i = 0; i <= k; i++) s += nql_dist_pdf(d, (double)i);
            return s;
        }
        case NQL_DIST_GEOMETRIC: {
            int k = (int)x; if (k < 1) return 0;
            return 1.0 - pow(1.0 - d->params[0], k);
        }
        case NQL_DIST_BERNOULLI: return (x < 0) ? 0 : (x < 1) ? (1.0 - d->params[0]) : 1.0;
        default: {
            /* Numerical integration for continuous distributions without closed-form CDF */
            double lo = -20.0, step = (x - lo) / 1000.0, s = 0;
            if (d->type == NQL_DIST_CHI_SQUARED || d->type == NQL_DIST_GAMMA || d->type == NQL_DIST_BETA) lo = 0;
            if (d->type == NQL_DIST_BETA) { lo = 0; step = x / 1000.0; }
            for (int i = 0; i < 1000; i++) {
                double t = lo + (i + 0.5) * step;
                s += nql_dist_pdf(d, t);
            }
            return s * step;
        }
    }
}

/* Generic mean */
static double nql_dist_mean(NqlDistribution* d) {
    switch (d->type) {
        case NQL_DIST_NORMAL:      return d->params[0];
        case NQL_DIST_UNIFORM:     return (d->params[0] + d->params[1]) / 2.0;
        case NQL_DIST_EXPONENTIAL: return 1.0 / d->params[0];
        case NQL_DIST_POISSON:     return d->params[0];
        case NQL_DIST_BINOMIAL:    return d->params[0] * d->params[1];
        case NQL_DIST_GAMMA:       return d->params[0] * d->params[1];
        case NQL_DIST_BETA:        return d->params[0] / (d->params[0] + d->params[1]);
        case NQL_DIST_CHI_SQUARED: return d->params[0];
        case NQL_DIST_GEOMETRIC:   return 1.0 / d->params[0];
        case NQL_DIST_BERNOULLI:   return d->params[0];
        default: return 0;
    }
}

/* Generic variance */
static double nql_dist_variance(NqlDistribution* d) {
    switch (d->type) {
        case NQL_DIST_NORMAL:      return d->params[1] * d->params[1];
        case NQL_DIST_UNIFORM:     { double w = d->params[1] - d->params[0]; return w * w / 12.0; }
        case NQL_DIST_EXPONENTIAL: return 1.0 / (d->params[0] * d->params[0]);
        case NQL_DIST_POISSON:     return d->params[0];
        case NQL_DIST_BINOMIAL:    return d->params[0] * d->params[1] * (1.0 - d->params[1]);
        case NQL_DIST_GAMMA:       return d->params[0] * d->params[1] * d->params[1];
        case NQL_DIST_BETA: {
            double a = d->params[0], b = d->params[1];
            return (a * b) / ((a + b) * (a + b) * (a + b + 1));
        }
        case NQL_DIST_CHI_SQUARED: return 2.0 * d->params[0];
        case NQL_DIST_GEOMETRIC:   return (1.0 - d->params[0]) / (d->params[0] * d->params[0]);
        case NQL_DIST_BERNOULLI:   return d->params[0] * (1.0 - d->params[0]);
        default: return 0;
    }
}

/* Simple LCG for sampling */
static uint64_t nql_dist_rng_state = 0x123456789ABCDEF0ULL;
static double nql_dist_rand01(void) {
    nql_dist_rng_state = nql_dist_rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)(nql_dist_rng_state >> 11) / (double)(1ULL << 53);
}

static double nql_dist_sample(NqlDistribution* d) {
    switch (d->type) {
        case NQL_DIST_NORMAL: {
            /* Box-Muller */
            double u1 = nql_dist_rand01(), u2 = nql_dist_rand01();
            if (u1 < 1e-15) u1 = 1e-15;
            return d->params[0] + d->params[1] * sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
        }
        case NQL_DIST_UNIFORM: return d->params[0] + nql_dist_rand01() * (d->params[1] - d->params[0]);
        case NQL_DIST_EXPONENTIAL: { double u = nql_dist_rand01(); return (u < 1e-15) ? 0 : -log(u) / d->params[0]; }
        case NQL_DIST_BERNOULLI: return (nql_dist_rand01() < d->params[0]) ? 1.0 : 0.0;
        case NQL_DIST_GEOMETRIC: {
            double u = nql_dist_rand01(); if (u < 1e-15) u = 1e-15;
            return ceil(log(u) / log(1.0 - d->params[0]));
        }
        case NQL_DIST_POISSON: {
            double L = exp(-d->params[0]), p = 1.0; int k = 0;
            do { k++; p *= nql_dist_rand01(); } while (p > L);
            return (double)(k - 1);
        }
        case NQL_DIST_BINOMIAL: {
            int n = (int)d->params[0]; double p = d->params[1], count = 0;
            for (int i = 0; i < n; i++) if (nql_dist_rand01() < p) count++;
            return count;
        }
        default: return nql_dist_mean(d); /* fallback */
    }
}

/* ======================================================================
 * NQL FUNCTIONS
 * ====================================================================== */

#define DIST_CTOR(NAME, TYPE, NP) \
static NqlValue nql_fn_dist_##NAME(const NqlValue* args, uint32_t argc, void* ctx) { \
    (void)ctx; (void)argc; NQL_NULL_GUARD_N(args, NP); \
    double p[4] = {0}; for (uint32_t i = 0; i < NP && i < argc; i++) p[i] = nql_val_as_float(&args[i]); \
    NqlDistribution* d = nql_dist_alloc(TYPE, p[0], p[1], p[2], p[3], NP); \
    return d ? nql_val_dist(d) : nql_val_null(); \
}

DIST_CTOR(normal,      NQL_DIST_NORMAL,      2)
DIST_CTOR(uniform,     NQL_DIST_UNIFORM,     2)
DIST_CTOR(exponential, NQL_DIST_EXPONENTIAL, 1)
DIST_CTOR(poisson,     NQL_DIST_POISSON,     1)
DIST_CTOR(binomial,    NQL_DIST_BINOMIAL,    2)
DIST_CTOR(gamma_d,     NQL_DIST_GAMMA,       2)
DIST_CTOR(beta_d,      NQL_DIST_BETA,        2)
DIST_CTOR(chi_sq,      NQL_DIST_CHI_SQUARED, 1)
DIST_CTOR(student_t,   NQL_DIST_STUDENT_T,   1)
DIST_CTOR(geometric,   NQL_DIST_GEOMETRIC,   1)
DIST_CTOR(bernoulli,   NQL_DIST_BERNOULLI,   1)

static NqlValue nql_fn_dist_pdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_DISTRIBUTION || !args[0].v.distribution) return nql_val_null();
    return nql_val_float(nql_dist_pdf(args[0].v.distribution, nql_val_as_float(&args[1])));
}

static NqlValue nql_fn_dist_cdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_DISTRIBUTION || !args[0].v.distribution) return nql_val_null();
    return nql_val_float(nql_dist_cdf(args[0].v.distribution, nql_val_as_float(&args[1])));
}

static NqlValue nql_fn_dist_quantile(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_DISTRIBUTION || !args[0].v.distribution) return nql_val_null();
    NqlDistribution* d = args[0].v.distribution;
    double p = nql_val_as_float(&args[1]);
    if (d->type == NQL_DIST_NORMAL) return nql_val_float(nql_dist_normal_quantile(p, d->params[0], d->params[1]));
    if (d->type == NQL_DIST_UNIFORM) return nql_val_float(d->params[0] + p * (d->params[1] - d->params[0]));
    if (d->type == NQL_DIST_EXPONENTIAL) return nql_val_float((p <= 0) ? 0 : -log(1.0 - p) / d->params[0]);
    /* Bisection fallback */
    double lo = nql_dist_mean(d) - 6 * sqrt(nql_dist_variance(d));
    double hi = nql_dist_mean(d) + 6 * sqrt(nql_dist_variance(d));
    if (lo < 0 && (d->type == NQL_DIST_CHI_SQUARED || d->type == NQL_DIST_GAMMA)) lo = 0;
    if (d->type == NQL_DIST_BETA) { lo = 0; hi = 1; }
    for (int i = 0; i < 100; i++) {
        double mid = (lo + hi) / 2.0;
        if (nql_dist_cdf(d, mid) < p) lo = mid; else hi = mid;
    }
    return nql_val_float((lo + hi) / 2.0);
}

static NqlValue nql_fn_dist_sample_one(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_DISTRIBUTION || !args[0].v.distribution) return nql_val_null();
    return nql_val_float(nql_dist_sample(args[0].v.distribution));
}

static NqlValue nql_fn_dist_sample_n(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_DISTRIBUTION || !args[0].v.distribution) return nql_val_null();
    uint32_t n = (uint32_t)nql_val_as_int(&args[1]);
    if (n > 100000) n = 100000;
    NqlArray* result = nql_array_alloc(n);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < n; i++) nql_array_push(result, nql_val_float(nql_dist_sample(args[0].v.distribution)));
    return nql_val_array(result);
}

static NqlValue nql_fn_dist_mean_f(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_DISTRIBUTION || !args[0].v.distribution) return nql_val_null();
    return nql_val_float(nql_dist_mean(args[0].v.distribution));
}

static NqlValue nql_fn_dist_variance_f(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_DISTRIBUTION || !args[0].v.distribution) return nql_val_null();
    return nql_val_float(nql_dist_variance(args[0].v.distribution));
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_distribution_type_register_functions(void) {
    nql_register_func("DIST_NORMAL",      nql_fn_dist_normal,      2, 2, "Normal(mu, sigma) distribution object");
    nql_register_func("DIST_UNIFORM",     nql_fn_dist_uniform,     2, 2, "Uniform(a, b) distribution object");
    nql_register_func("DIST_EXPONENTIAL", nql_fn_dist_exponential, 1, 1, "Exponential(lambda) distribution");
    nql_register_func("DIST_POISSON",     nql_fn_dist_poisson,     1, 1, "Poisson(lambda) distribution");
    nql_register_func("DIST_BINOMIAL",    nql_fn_dist_binomial,    2, 2, "Binomial(n, p) distribution");
    nql_register_func("DIST_GAMMA",       nql_fn_dist_gamma_d,     2, 2, "Gamma(shape, scale) distribution");
    nql_register_func("DIST_BETA",        nql_fn_dist_beta_d,      2, 2, "Beta(a, b) distribution");
    nql_register_func("DIST_CHI_SQUARED", nql_fn_dist_chi_sq,      1, 1, "Chi-squared(k) distribution");
    nql_register_func("DIST_STUDENT_T",   nql_fn_dist_student_t,   1, 1, "Student's t(nu) distribution");
    nql_register_func("DIST_GEOMETRIC",   nql_fn_dist_geometric,   1, 1, "Geometric(p) distribution");
    nql_register_func("DIST_BERNOULLI",   nql_fn_dist_bernoulli,   1, 1, "Bernoulli(p) distribution");
    nql_register_func("DIST_PDF",         nql_fn_dist_pdf,         2, 2, "PDF of distribution at x");
    nql_register_func("DIST_CDF",         nql_fn_dist_cdf,         2, 2, "CDF of distribution at x");
    nql_register_func("DIST_QUANTILE",    nql_fn_dist_quantile,    2, 2, "Quantile (inverse CDF) at p");
    nql_register_func("DIST_SAMPLE",      nql_fn_dist_sample_one,  1, 1, "Draw one random sample");
    nql_register_func("DIST_SAMPLE_N",    nql_fn_dist_sample_n,    2, 2, "Draw n random samples as array");
    nql_register_func("DIST_MEAN",        nql_fn_dist_mean_f,      1, 1, "Theoretical mean of distribution");
    nql_register_func("DIST_VARIANCE",    nql_fn_dist_variance_f,  1, 1, "Theoretical variance of distribution");
}
