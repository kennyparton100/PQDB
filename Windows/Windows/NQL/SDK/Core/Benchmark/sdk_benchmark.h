/**
 * sdk_benchmark.h - Performance benchmarking
 */
#ifndef SDK_BENCHMARK_H
#define SDK_BENCHMARK_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run performance benchmarks and write results to JSON and CSV files.
 * Files will be created in the working directory.
 */
void run_performance_benchmarks(void);

#ifdef __cplusplus
}
#endif

#endif /* SDK_BENCHMARK_H */
