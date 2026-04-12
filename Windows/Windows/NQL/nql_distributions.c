/**
 * nql_distributions.c - Probability distribution functions for NQL
 * Part of the NQL mathematical expansion - Phase 3: Statistical Functions
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);
NqlValue nql_val_float(double f);
NqlValue nql_val_null(void);

/* Normal distribution functions */
static NqlValue nql_fn_normal_pdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    
    if (args[0].type != NQL_VAL_FLOAT && args[0].type != NQL_VAL_INT) return nql_val_null();
    if (args[1].type != NQL_VAL_FLOAT && args[1].type != NQL_VAL_INT) return nql_val_null();
    if (args[2].type != NQL_VAL_FLOAT && args[2].type != NQL_VAL_INT) return nql_val_null();
    
    double x = (args[0].type == NQL_VAL_FLOAT) ? args[0].v.fval : (double)args[0].v.ival;
    double mu = (args[1].type == NQL_VAL_FLOAT) ? args[1].v.fval : (double)args[1].v.ival;
    double sigma = (args[2].type == NQL_VAL_FLOAT) ? args[2].v.fval : (double)args[2].v.ival;
    
    if (sigma <= 0.0) return nql_val_null();
    
    /* Normal PDF: f(x) = (1/(sigmasqrt(2pi))) * exp(-((x-mu)^2)/(2sigma^2)) */
    double variance = sigma * sigma;
    double exponent = -((x - mu) * (x - mu)) / (2.0 * variance);
    double coefficient = 1.0 / (sigma * sqrt(2.0 * M_PI));
    double result = coefficient * exp(exponent);
    
    return nql_val_float(result);
}

static NqlValue nql_fn_normal_cdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    
    if (args[0].type != NQL_VAL_FLOAT && args[0].type != NQL_VAL_INT) return nql_val_null();
    if (args[1].type != NQL_VAL_FLOAT && args[1].type != NQL_VAL_INT) return nql_val_null();
    if (args[2].type != NQL_VAL_FLOAT && args[2].type != NQL_VAL_INT) return nql_val_null();
    
    double x = (args[0].type == NQL_VAL_FLOAT) ? args[0].v.fval : (double)args[0].v.ival;
    double mu = (args[1].type == NQL_VAL_FLOAT) ? args[1].v.fval : (double)args[1].v.ival;
    double sigma = (args[2].type == NQL_VAL_FLOAT) ? args[2].v.fval : (double)args[2].v.ival;
    
    if (sigma <= 0.0) return nql_val_null();
    
    /* Standard normal CDF using error function approximation */
    double z = (x - mu) / sigma;
    
    /* Abramowitz and Stegun approximation for erf(z) */
    double t = 1.0 / (1.0 + 0.5 * fabs(z));
    double erf_approx = 1.0 - t * exp(-z * z - 1.26551223 +
                                      t * (1.00002368 +
                                      t * (0.37409196 +
                                      t * (0.09678418 +
                                      t * (-0.18628806 +
                                      t * (0.27886807 +
                                      t * (-1.13520398 +
                                      t * (1.48851587 +
                                      t * (-0.82215223 +
                                      t * 0.17087277)))))))));
    
    double cdf = 0.5 * (1.0 + (z < 0 ? -erf_approx : erf_approx));
    return nql_val_float(cdf);
}

/* Binomial distribution functions */
static NqlValue nql_fn_binomial_pmf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    
    if (args[0].type != NQL_VAL_INT) return nql_val_null();
    if (args[1].type != NQL_VAL_INT) return nql_val_null();
    if (args[2].type != NQL_VAL_FLOAT && args[2].type != NQL_VAL_INT) return nql_val_null();
    
    int64_t k = args[0].v.ival;
    int64_t n = args[1].v.ival;
    double p = (args[2].type == NQL_VAL_FLOAT) ? args[2].v.fval : (double)args[2].v.ival;
    
    if (k < 0 || n < 0 || k > n || p < 0.0 || p > 1.0) return nql_val_null();
    
    /* Binomial PMF: P(X=k) = C(n,k) * p^k * (1-p)^(n-k) */
    
    /* Calculate C(n,k) */
    if (k > n - k) k = n - k; /* Use symmetry */
    
    double combination = 1.0;
    for (int64_t i = 1; i <= k; i++) {
        combination *= (double)(n - k + i) / (double)i;
    }
    
    double pmf = combination * pow(p, (double)args[0].v.ival) * pow(1.0 - p, (double)(n - args[0].v.ival));
    return nql_val_float(pmf);
}

static NqlValue nql_fn_binomial_cdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    
    if (args[0].type != NQL_VAL_INT) return nql_val_null();
    if (args[1].type != NQL_VAL_INT) return nql_val_null();
    if (args[2].type != NQL_VAL_FLOAT && args[2].type != NQL_VAL_INT) return nql_val_null();
    
    int64_t k = args[0].v.ival;
    int64_t n = args[1].v.ival;
    double p = (args[2].type == NQL_VAL_FLOAT) ? args[2].v.fval : (double)args[2].v.ival;
    
    if (k < 0 || n < 0 || k > n || p < 0.0 || p > 1.0) return nql_val_null();
    
    /* Binomial CDF: P(X <= k) = sigma(i=0 to k) C(n,i) * p^i * (1-p)^(n-i) */
    double cdf = 0.0;
    double q = 1.0 - p;
    
    for (int64_t i = 0; i <= k; i++) {
        /* Calculate C(n,i) */
        int64_t temp_i = i;
        if (temp_i > n - temp_i) temp_i = n - temp_i;
        
        double combination = 1.0;
        for (int64_t j = 1; j <= temp_i; j++) {
            combination *= (double)(n - temp_i + j) / (double)j;
        }
        
        cdf += combination * pow(p, (double)i) * pow(q, (double)(n - i));
    }
    
    return nql_val_float(cdf);
}

/* Poisson distribution functions */
static NqlValue nql_fn_poisson_pmf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    
    if (args[0].type != NQL_VAL_INT) return nql_val_null();
    if (args[1].type != NQL_VAL_FLOAT && args[1].type != NQL_VAL_INT) return nql_val_null();
    
    int64_t k = args[0].v.ival;
    double lambda = (args[1].type == NQL_VAL_FLOAT) ? args[1].v.fval : (double)args[1].v.ival;
    
    if (k < 0 || lambda < 0.0) return nql_val_null();
    
    /* Poisson PMF: P(X=k) = (lambda^k * e^(-lambda)) / k! */
    
    /* Calculate k! */
    double factorial = 1.0;
    for (int64_t i = 2; i <= k; i++) {
        factorial *= (double)i;
    }
    
    double pmf = pow(lambda, (double)k) * exp(-lambda) / factorial;
    return nql_val_float(pmf);
}

static NqlValue nql_fn_poisson_cdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    
    if (args[0].type != NQL_VAL_INT) return nql_val_null();
    if (args[1].type != NQL_VAL_FLOAT && args[1].type != NQL_VAL_INT) return nql_val_null();
    
    int64_t k = args[0].v.ival;
    double lambda = (args[1].type == NQL_VAL_FLOAT) ? args[1].v.fval : (double)args[1].v.ival;
    
    if (k < 0 || lambda < 0.0) return nql_val_null();
    
    /* Poisson CDF: P(X <= k) = sigma(i=0 to k) (lambda^i * e^(-lambda)) / i! */
    double cdf = 0.0;
    
    for (int64_t i = 0; i <= k; i++) {
        /* Calculate i! */
        double factorial = 1.0;
        for (int64_t j = 2; j <= i; j++) {
            factorial *= (double)j;
        }
        
        cdf += pow(lambda, (double)i) * exp(-lambda) / factorial;
    }
    
    return nql_val_float(cdf);
}

/* -- Exponential distribution -- */

static NqlValue nql_fn_exponential_pdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    double x = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double lambda = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    if (lambda <= 0.0) return nql_val_null();
    if (x < 0.0) return nql_val_float(0.0);
    return nql_val_float(lambda * exp(-lambda * x));
}

static NqlValue nql_fn_exponential_cdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    double x = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double lambda = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    if (lambda <= 0.0) return nql_val_null();
    if (x < 0.0) return nql_val_float(0.0);
    return nql_val_float(1.0 - exp(-lambda * x));
}

/* -- Uniform distribution -- */

static NqlValue nql_fn_uniform_pdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    double x = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double a = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    double b = (args[2].type == NQL_VAL_INT) ? (double)args[2].v.ival : args[2].v.fval;
    if (b <= a) return nql_val_null();
    if (x < a || x > b) return nql_val_float(0.0);
    return nql_val_float(1.0 / (b - a));
}

static NqlValue nql_fn_uniform_cdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 3) return nql_val_null();
    double x = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double a = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    double b = (args[2].type == NQL_VAL_INT) ? (double)args[2].v.ival : args[2].v.fval;
    if (b <= a) return nql_val_null();
    if (x < a) return nql_val_float(0.0);
    if (x > b) return nql_val_float(1.0);
    return nql_val_float((x - a) / (b - a));
}

/* -- Geometric distribution -- */

static NqlValue nql_fn_geometric_pmf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_INT) return nql_val_null();
    int64_t k = args[0].v.ival;
    double p = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    if (k < 1 || p <= 0.0 || p > 1.0) return nql_val_null();
    return nql_val_float(p * pow(1.0 - p, (double)(k - 1)));
}

static NqlValue nql_fn_geometric_cdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_INT) return nql_val_null();
    int64_t k = args[0].v.ival;
    double p = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    if (k < 1 || p <= 0.0 || p > 1.0) return nql_val_null();
    return nql_val_float(1.0 - pow(1.0 - p, (double)k));
}

/* -- Chi-Squared distribution (uses Lanczos gamma approximation) -- */

static double nql_lgamma_approx(double x) {
    /* Lanczos approximation for ln(Gamma(x)) */
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

static NqlValue nql_fn_chi_squared_pdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    double x = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double k = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    if (k < 1.0 || x < 0.0) return nql_val_null();
    if (x == 0.0) return (k == 2.0) ? nql_val_float(0.5) : nql_val_float(0.0);
    double half_k = k / 2.0;
    double pdf = exp((half_k - 1.0) * log(x) - x / 2.0 - half_k * log(2.0) - nql_lgamma_approx(half_k));
    return nql_val_float(pdf);
}

static NqlValue nql_fn_chi_squared_cdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    double x = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double k = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    if (k < 1.0 || x < 0.0) return nql_val_null();
    if (x == 0.0) return nql_val_float(0.0);
    /* Regularized lower incomplete gamma via series expansion */
    double a = k / 2.0;
    double z = x / 2.0;
    double sum = 0.0, term = 1.0 / a;
    sum = term;
    for (int n = 1; n < 200; n++) {
        term *= z / (a + n);
        sum += term;
        if (fabs(term) < 1e-15 * fabs(sum)) break;
    }
    double result = sum * exp(-z + a * log(z) - nql_lgamma_approx(a));
    if (result > 1.0) result = 1.0;
    if (result < 0.0) result = 0.0;
    return nql_val_float(result);
}

/* -- Student's t-distribution -- */

static NqlValue nql_fn_t_pdf(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    double x = (args[0].type == NQL_VAL_INT) ? (double)args[0].v.ival : args[0].v.fval;
    double nu = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    if (nu < 1.0) return nql_val_null();
    double coeff = exp(nql_lgamma_approx((nu + 1.0) / 2.0) - nql_lgamma_approx(nu / 2.0))
                 / sqrt(nu * M_PI);
    double pdf = coeff * pow(1.0 + x * x / nu, -(nu + 1.0) / 2.0);
    return nql_val_float(pdf);
}

/* Register distribution functions */
void nql_distributions_register_functions(void) {
    /* Normal distribution */
    nql_register_func("NORMAL_PDF",        nql_fn_normal_pdf,             3, 3, "Normal probability density function");
    nql_register_func("NORMAL_CDF",        nql_fn_normal_cdf,             3, 3, "Normal cumulative distribution function");
    
    /* Binomial distribution */
    nql_register_func("BINOMIAL_PMF",      nql_fn_binomial_pmf,           3, 3, "Binomial probability mass function");
    nql_register_func("BINOMIAL_CDF",      nql_fn_binomial_cdf,           3, 3, "Binomial cumulative distribution function");
    
    /* Poisson distribution */
    nql_register_func("POISSON_PMF",       nql_fn_poisson_pmf,            2, 2, "Poisson probability mass function");
    nql_register_func("POISSON_CDF",       nql_fn_poisson_cdf,            2, 2, "Poisson cumulative distribution function");
    
    /* Exponential distribution */
    nql_register_func("EXPONENTIAL_PDF",   nql_fn_exponential_pdf,        2, 2, "Exponential PDF (x, lambda)");
    nql_register_func("EXPONENTIAL_CDF",   nql_fn_exponential_cdf,        2, 2, "Exponential CDF (x, lambda)");
    
    /* Uniform distribution */
    nql_register_func("UNIFORM_PDF",       nql_fn_uniform_pdf,            3, 3, "Uniform PDF (x, a, b)");
    nql_register_func("UNIFORM_CDF",       nql_fn_uniform_cdf,            3, 3, "Uniform CDF (x, a, b)");
    
    /* Geometric distribution */
    nql_register_func("GEOMETRIC_PMF",     nql_fn_geometric_pmf,          2, 2, "Geometric PMF (k, p)");
    nql_register_func("GEOMETRIC_CDF",     nql_fn_geometric_cdf,          2, 2, "Geometric CDF (k, p)");
    
    /* Chi-squared distribution */
    nql_register_func("CHI_SQUARED_PDF",   nql_fn_chi_squared_pdf,        2, 2, "Chi-squared PDF (x, k)");
    nql_register_func("CHI_SQUARED_CDF",   nql_fn_chi_squared_cdf,        2, 2, "Chi-squared CDF (x, k)");
    
    /* Student's t-distribution */
    nql_register_func("T_PDF",             nql_fn_t_pdf,                  2, 2, "Student t PDF (x, nu)");
}
