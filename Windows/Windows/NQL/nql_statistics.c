/**
 * nql_statistics.c - Statistical functions implementation for NQL
 * Part of the NQL mathematical expansion - Phase 3: Statistical Functions
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations for static array functions from nql_array.c */
static NqlArray* nql_array_alloc(uint32_t capacity);
static bool nql_array_push(NqlArray* a, NqlValue val);
static NqlValue nql_val_array(NqlArray* a);

/* Forward declarations for NQL function registration */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* Helper function to extract array data safely */
static double* extract_array_data_safe(const NqlValue* array_val, uint32_t* count) {
    if (!array_val || array_val->type != NQL_VAL_ARRAY || !array_val->v.array) {
        *count = 0;
        return NULL;
    }
    
    NqlArray* arr = array_val->v.array;
    *count = arr->length;
    
    if (*count == 0) return NULL;
    
    double* data = (double*)malloc(*count * sizeof(double));
    if (!data) return NULL;
    
    /* Access array elements directly using existing NQL array structure */
    for (uint32_t i = 0; i < *count; i++) {
        NqlValue element = arr->items[i];
        
        if (element.type == NQL_VAL_INT) {
            data[i] = (double)element.v.ival;
        } else if (element.type == NQL_VAL_FLOAT) {
            data[i] = element.v.fval;
        } else {
            free(data);
            *count = 0;
            return NULL;
        }
    }
    
    return data;
}

/* Comparison function for sorting */
static int compare_double(const void* a, const void* b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

/* Descriptive Statistics Functions */
static NqlValue nql_fn_mean(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    
    uint32_t count;
    double* data = extract_array_data_safe(&args[0], &count);
    if (!data || count == 0) {
        if (data) free(data);
        return nql_val_null();
    }
    
    double sum = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        sum += data[i];
    }
    
    double mean = sum / count;
    free(data);
    return nql_val_float(mean);
}

static NqlValue nql_fn_median(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    
    uint32_t count;
    double* data = extract_array_data_safe(&args[0], &count);
    if (!data || count == 0) {
        if (data) free(data);
        return nql_val_null();
    }
    
    /* Sort the data */
    qsort(data, count, sizeof(double), compare_double);
    
    double median;
    if (count % 2 == 0) {
        /* Even number of elements - average middle two */
        median = (data[count/2 - 1] + data[count/2]) / 2.0;
    } else {
        /* Odd number of elements - middle element */
        median = data[count/2];
    }
    
    free(data);
    return nql_val_float(median);
}

static NqlValue nql_fn_mode(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    
    uint32_t count;
    double* data = extract_array_data_safe(&args[0], &count);
    if (!data || count == 0) {
        if (data) free(data);
        return nql_val_null();
    }
    
    /* Find mode using frequency counting */
    double mode = data[0];
    uint32_t max_freq = 1;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t freq = 1;
        for (uint32_t j = i + 1; j < count; j++) {
            if (fabs(data[i] - data[j]) < 1e-10) {
                freq++;
            }
        }
        
        if (freq > max_freq) {
            max_freq = freq;
            mode = data[i];
        }
    }
    
    free(data);
    return nql_val_float(mode);
}

static NqlValue nql_fn_variance(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    
    uint32_t count;
    double* data = extract_array_data_safe(&args[0], &count);
    if (!data || count == 0) {
        if (data) free(data);
        return nql_val_null();
    }
    
    /* Calculate mean */
    double sum = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        sum += data[i];
    }
    double mean = sum / count;
    
    /* Calculate variance */
    double variance = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        double diff = data[i] - mean;
        variance += diff * diff;
    }
    variance /= count; /* Population variance */
    
    free(data);
    return nql_val_float(variance);
}

static NqlValue nql_fn_std_dev(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    
    uint32_t count;
    double* data = extract_array_data_safe(&args[0], &count);
    if (!data || count == 0) {
        if (data) free(data);
        return nql_val_null();
    }
    
    /* Calculate mean */
    double sum = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        sum += data[i];
    }
    double mean = sum / count;
    
    /* Calculate variance */
    double variance = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        double diff = data[i] - mean;
        variance += diff * diff;
    }
    variance /= count; /* Population variance */
    
    double std_dev = sqrt(variance);
    free(data);
    return nql_val_float(std_dev);
}

/* Correlation and Covariance Functions */
static NqlValue nql_fn_correlation(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    
    /* Direct array access - don't use extract_array_data_safe */
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    
    NqlArray* arr1 = args[0].v.array;
    NqlArray* arr2 = args[1].v.array;
    
    if (arr1->length == 0 || arr2->length == 0 || arr1->length != arr2->length) {
        return nql_val_null();
    }
    
    uint32_t n = arr1->length;
    
    /* Calculate means */
    double sum1 = 0.0, sum2 = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        double val1 = (arr1->items[i].type == NQL_VAL_INT) ? (double)arr1->items[i].v.ival : arr1->items[i].v.fval;
        double val2 = (arr2->items[i].type == NQL_VAL_INT) ? (double)arr2->items[i].v.ival : arr2->items[i].v.fval;
        sum1 += val1;
        sum2 += val2;
    }
    double mean1 = sum1 / n;
    double mean2 = sum2 / n;
    
    /* Calculate correlation */
    double numerator = 0.0;
    double denom1 = 0.0, denom2 = 0.0;
    
    for (uint32_t i = 0; i < n; i++) {
        double val1 = (arr1->items[i].type == NQL_VAL_INT) ? (double)arr1->items[i].v.ival : arr1->items[i].v.fval;
        double val2 = (arr2->items[i].type == NQL_VAL_INT) ? (double)arr2->items[i].v.ival : arr2->items[i].v.fval;
        double diff1 = val1 - mean1;
        double diff2 = val2 - mean2;
        numerator += diff1 * diff2;
        denom1 += diff1 * diff1;
        denom2 += diff2 * diff2;
    }
    
    double correlation = 0.0;
    if (denom1 > 0 && denom2 > 0) {
        correlation = numerator / (sqrt(denom1) * sqrt(denom2));
    }
    
    return nql_val_float(correlation);
}

static NqlValue nql_fn_covariance(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    
    /* Direct array access - don't use extract_array_data_safe */
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    
    NqlArray* arr1 = args[0].v.array;
    NqlArray* arr2 = args[1].v.array;
    
    if (arr1->length == 0 || arr2->length == 0 || arr1->length != arr2->length) {
        return nql_val_null();
    }
    
    uint32_t n = arr1->length;
    
    /* Calculate means */
    double sum1 = 0.0, sum2 = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        double val1 = (arr1->items[i].type == NQL_VAL_INT) ? (double)arr1->items[i].v.ival : arr1->items[i].v.fval;
        double val2 = (arr2->items[i].type == NQL_VAL_INT) ? (double)arr2->items[i].v.ival : arr2->items[i].v.fval;
        sum1 += val1;
        sum2 += val2;
    }
    double mean1 = sum1 / n;
    double mean2 = sum2 / n;
    
    /* Calculate covariance */
    double covariance = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        double val1 = (arr1->items[i].type == NQL_VAL_INT) ? (double)arr1->items[i].v.ival : arr1->items[i].v.fval;
        double val2 = (arr2->items[i].type == NQL_VAL_INT) ? (double)arr2->items[i].v.ival : arr2->items[i].v.fval;
        covariance += (val1 - mean1) * (val2 - mean2);
    }
    covariance /= n; /* Population covariance */
    
    return nql_val_float(covariance);
}

/* Linear Regression Functions */
static NqlValue nql_fn_linear_regression(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    
    /* Direct array access - don't use extract_array_data_safe */
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    
    NqlArray* arr1 = args[0].v.array;
    NqlArray* arr2 = args[1].v.array;
    
    if (arr1->length == 0 || arr2->length == 0 || arr1->length != arr2->length) {
        return nql_val_null();
    }
    
    uint32_t n = arr1->length;
    
    /* Calculate sums for linear regression */
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;
    
    for (uint32_t i = 0; i < n; i++) {
        double x_val = (arr1->items[i].type == NQL_VAL_INT) ? (double)arr1->items[i].v.ival : arr1->items[i].v.fval;
        double y_val = (arr2->items[i].type == NQL_VAL_INT) ? (double)arr2->items[i].v.ival : arr2->items[i].v.fval;
        sum_x += x_val;
        sum_y += y_val;
        sum_xy += x_val * y_val;
        sum_x2 += x_val * x_val;
    }
    
    /* Calculate slope and intercept */
    double denominator = n * sum_x2 - sum_x * sum_x;
    if (fabs(denominator) < 1e-10) {
        return nql_val_null(); /* Vertical line - no linear regression */
    }
    
    double slope = (n * sum_xy - sum_x * sum_y) / denominator;
    double intercept = (sum_y - slope * sum_x) / n;
    
    /* Create result array: [slope, intercept] using proper NQL array functions */
    NqlArray* result_arr = nql_array_alloc(2);
    if (!result_arr) return nql_val_null();
    
    nql_array_push(result_arr, nql_val_float(slope));
    nql_array_push(result_arr, nql_val_float(intercept));
    
    return nql_val_array(result_arr);
}

/* -- Extended Statistics -- */

static NqlValue nql_fn_percentile(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    double p = (args[1].type == NQL_VAL_INT) ? (double)args[1].v.ival : args[1].v.fval;
    if (arr->length == 0 || p < 0.0 || p > 100.0) return nql_val_null();
    /* Copy and sort */
    double* sorted = (double*)malloc(arr->length * sizeof(double));
    if (!sorted) return nql_val_null();
    for (uint32_t i = 0; i < arr->length; i++)
        sorted[i] = (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
    for (uint32_t i = 0; i < arr->length - 1; i++)
        for (uint32_t j = i + 1; j < arr->length; j++)
            if (sorted[j] < sorted[i]) { double t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }
    double rank = (p / 100.0) * (arr->length - 1);
    uint32_t lo = (uint32_t)rank;
    double frac = rank - lo;
    double result;
    if (lo + 1 >= arr->length) result = sorted[arr->length - 1];
    else result = sorted[lo] + frac * (sorted[lo + 1] - sorted[lo]);
    free(sorted);
    return nql_val_float(result);
}

static NqlValue nql_fn_quartiles(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* result = nql_array_alloc(3);
    if (!result) return nql_val_null();
    NqlValue p25_args[2] = { args[0], nql_val_float(25.0) };
    NqlValue p50_args[2] = { args[0], nql_val_float(50.0) };
    NqlValue p75_args[2] = { args[0], nql_val_float(75.0) };
    nql_array_push(result, nql_fn_percentile(p25_args, 2, ctx));
    nql_array_push(result, nql_fn_percentile(p50_args, 2, ctx));
    nql_array_push(result, nql_fn_percentile(p75_args, 2, ctx));
    return nql_val_array(result);
}

static NqlValue nql_fn_skewness(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    if (arr->length < 3) return nql_val_null();
    double n = (double)arr->length;
    double sum = 0.0;
    for (uint32_t i = 0; i < arr->length; i++)
        sum += (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
    double mean = sum / n;
    double m2 = 0.0, m3 = 0.0;
    for (uint32_t i = 0; i < arr->length; i++) {
        double d = ((arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval) - mean;
        m2 += d * d;
        m3 += d * d * d;
    }
    m2 /= n; m3 /= n;
    if (m2 == 0.0) return nql_val_float(0.0);
    return nql_val_float(m3 / pow(m2, 1.5));
}

static NqlValue nql_fn_kurtosis(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    if (arr->length < 4) return nql_val_null();
    double n = (double)arr->length;
    double sum = 0.0;
    for (uint32_t i = 0; i < arr->length; i++)
        sum += (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
    double mean = sum / n;
    double m2 = 0.0, m4 = 0.0;
    for (uint32_t i = 0; i < arr->length; i++) {
        double d = ((arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval) - mean;
        double d2 = d * d;
        m2 += d2;
        m4 += d2 * d2;
    }
    m2 /= n; m4 /= n;
    if (m2 == 0.0) return nql_val_float(0.0);
    return nql_val_float((m4 / (m2 * m2)) - 3.0);
}

static NqlValue nql_fn_zscore(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    if (arr->length < 2) return nql_val_null();
    double n = (double)arr->length, sum = 0.0;
    for (uint32_t i = 0; i < arr->length; i++)
        sum += (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
    double mean = sum / n;
    double var_sum = 0.0;
    for (uint32_t i = 0; i < arr->length; i++) {
        double d = ((arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval) - mean;
        var_sum += d * d;
    }
    double sd = sqrt(var_sum / n);
    if (sd == 0.0) return nql_val_null();
    NqlArray* result = nql_array_alloc(arr->length);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < arr->length; i++) {
        double val = (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
        nql_array_push(result, nql_val_float((val - mean) / sd));
    }
    return nql_val_array(result);
}

static NqlValue nql_fn_moving_avg(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    uint32_t window = (uint32_t)((args[1].type == NQL_VAL_INT) ? args[1].v.ival : (int64_t)args[1].v.fval);
    if (window == 0 || window > arr->length) return nql_val_null();
    NqlArray* result = nql_array_alloc(arr->length - window + 1);
    if (!result) return nql_val_null();
    double sum = 0.0;
    for (uint32_t i = 0; i < window; i++)
        sum += (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
    nql_array_push(result, nql_val_float(sum / window));
    for (uint32_t i = window; i < arr->length; i++) {
        sum += (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
        sum -= (arr->items[i - window].type == NQL_VAL_INT) ? (double)arr->items[i - window].v.ival : arr->items[i - window].v.fval;
        nql_array_push(result, nql_val_float(sum / window));
    }
    return nql_val_array(result);
}

static NqlValue nql_fn_weighted_mean(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 2) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    NqlArray* vals = args[0].v.array;
    NqlArray* wts = args[1].v.array;
    if (vals->length != wts->length || vals->length == 0) return nql_val_null();
    double wsum = 0.0, wtotal = 0.0;
    for (uint32_t i = 0; i < vals->length; i++) {
        double v = (vals->items[i].type == NQL_VAL_INT) ? (double)vals->items[i].v.ival : vals->items[i].v.fval;
        double w = (wts->items[i].type == NQL_VAL_INT) ? (double)wts->items[i].v.ival : wts->items[i].v.fval;
        wsum += v * w;
        wtotal += w;
    }
    if (wtotal == 0.0) return nql_val_null();
    return nql_val_float(wsum / wtotal);
}

static NqlValue nql_fn_harmonic_mean(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    if (arr->length == 0) return nql_val_null();
    double sum_inv = 0.0;
    for (uint32_t i = 0; i < arr->length; i++) {
        double v = (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
        if (v == 0.0) return nql_val_null();
        sum_inv += 1.0 / v;
    }
    return nql_val_float((double)arr->length / sum_inv);
}

static NqlValue nql_fn_geometric_mean(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    if (arr->length == 0) return nql_val_null();
    double log_sum = 0.0;
    for (uint32_t i = 0; i < arr->length; i++) {
        double v = (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
        if (v <= 0.0) return nql_val_null();
        log_sum += log(v);
    }
    return nql_val_float(exp(log_sum / (double)arr->length));
}

static NqlValue nql_fn_sample_variance(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx;
    if (argc != 1) return nql_val_null();
    if (args[0].type != NQL_VAL_ARRAY || !args[0].v.array) return nql_val_null();
    NqlArray* arr = args[0].v.array;
    if (arr->length < 2) return nql_val_null();
    double n = (double)arr->length, sum = 0.0;
    for (uint32_t i = 0; i < arr->length; i++)
        sum += (arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval;
    double mean = sum / n;
    double var_sum = 0.0;
    for (uint32_t i = 0; i < arr->length; i++) {
        double d = ((arr->items[i].type == NQL_VAL_INT) ? (double)arr->items[i].v.ival : arr->items[i].v.fval) - mean;
        var_sum += d * d;
    }
    return nql_val_float(var_sum / (n - 1.0));
}

static NqlValue nql_fn_sample_std_dev(const NqlValue* args, uint32_t argc, void* ctx) {
    NqlValue sv = nql_fn_sample_variance(args, argc, ctx);
    if (sv.type != NQL_VAL_FLOAT) return nql_val_null();
    return nql_val_float(sqrt(sv.v.fval));
}

/* Register statistical functions */
void nql_statistics_register_functions(void) {
    /* Descriptive statistics */
    nql_register_func("MEAN",              nql_fn_mean,                   1, 1, "Arithmetic mean");
    nql_register_func("MEDIAN",            nql_fn_median,                 1, 1, "Median");
    nql_register_func("MODE",              nql_fn_mode,                   1, 1, "Mode (most frequent value)");
    nql_register_func("VARIANCE",          nql_fn_variance,               1, 1, "Population variance");
    nql_register_func("STD_DEV",           nql_fn_std_dev,                1, 1, "Population standard deviation");
    
    /* Correlation and covariance */
    nql_register_func("CORRELATION",       nql_fn_correlation,            2, 2, "Pearson correlation coefficient");
    nql_register_func("COVARIANCE",        nql_fn_covariance,             2, 2, "Population covariance");
    
    /* Regression */
    nql_register_func("LINEAR_REGRESSION", nql_fn_linear_regression,      2, 2, "Linear regression [slope, intercept]");
    
    /* Extended statistics */
    nql_register_func("PERCENTILE",        nql_fn_percentile,             2, 2, "Percentile (array, 0-100)");
    nql_register_func("QUARTILES",         nql_fn_quartiles,              1, 1, "Returns [Q1, Q2, Q3]");
    nql_register_func("SKEWNESS",          nql_fn_skewness,               1, 1, "Population skewness");
    nql_register_func("KURTOSIS",          nql_fn_kurtosis,               1, 1, "Excess kurtosis");
    nql_register_func("ZSCORE",            nql_fn_zscore,                 1, 1, "Z-score normalization");
    nql_register_func("MOVING_AVG",        nql_fn_moving_avg,             2, 2, "Simple moving average (array, window)");
    nql_register_func("WEIGHTED_MEAN",     nql_fn_weighted_mean,          2, 2, "Weighted arithmetic mean");
    nql_register_func("HARMONIC_MEAN",     nql_fn_harmonic_mean,          1, 1, "Harmonic mean");
    nql_register_func("GEOMETRIC_MEAN",    nql_fn_geometric_mean,         1, 1, "Geometric mean");
    nql_register_func("SAMPLE_VARIANCE",   nql_fn_sample_variance,        1, 1, "Sample variance (Bessel-corrected)");
    nql_register_func("SAMPLE_STD_DEV",    nql_fn_sample_std_dev,         1, 1, "Sample standard deviation");
}
