/**
 * test_harness.h - Lightweight integration test framework for CPSS Viewer.
 *
 * Usage:
 *   TEST(name) { ... }           — define a test
 *   ASSERT_INT_EQ(a, b)          — assert two int64_t values are equal
 *   ASSERT_UINT_EQ(a, b)         — assert two uint64_t values are equal
 *   ASSERT_FLOAT_NEAR(a, b, eps) — assert two doubles are within epsilon
 *   ASSERT_TRUE(expr)            — assert expression is truthy
 *   ASSERT_FALSE(expr)           — assert expression is falsy
 *   ASSERT_STR_EQ(a, b)          — assert two strings are equal
 *   ASSERT_STR_CONTAINS(hay, needle) — assert substring exists
 *
 * The test runner collects results and prints a summary at the end.
 */

#ifndef CPSS_TEST_HARNESS_H
#define CPSS_TEST_HARNESS_H

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ── Test bookkeeping ── */

static int g_test_total = 0;
static int g_test_passed = 0;
static int g_test_failed = 0;
static int g_assert_total = 0;
static int g_assert_failed = 0;
static const char* g_current_test = NULL;
static int g_current_test_ok = 1;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        g_current_test = #name; \
        g_current_test_ok = 1; \
        ++g_test_total; \
        test_##name(); \
        if (g_current_test_ok) { \
            ++g_test_passed; \
            printf("  PASS  %s\n", #name); \
        } else { \
            ++g_test_failed; \
        } \
    } \
    static void test_##name(void)

#define RUN_TEST(name) run_test_##name()

/* ── Assertion macros ── */

#define ASSERT_INT_EQ(a, b) do { \
    ++g_assert_total; \
    int64_t _a = (int64_t)(a), _b = (int64_t)(b); \
    if (_a != _b) { \
        printf("  FAIL  %s  (%s:%d)\n    expected: %lld\n    actual:   %lld\n", \
            g_current_test, __FILE__, __LINE__, (long long)_b, (long long)_a); \
        g_current_test_ok = 0; ++g_assert_failed; \
    } \
} while(0)

#define ASSERT_UINT_EQ(a, b) do { \
    ++g_assert_total; \
    uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b); \
    if (_a != _b) { \
        printf("  FAIL  %s  (%s:%d)\n    expected: %llu\n    actual:   %llu\n", \
            g_current_test, __FILE__, __LINE__, (unsigned long long)_b, (unsigned long long)_a); \
        g_current_test_ok = 0; ++g_assert_failed; \
    } \
} while(0)

#define ASSERT_FLOAT_NEAR(a, b, eps) do { \
    ++g_assert_total; \
    double _a = (double)(a), _b = (double)(b), _e = (double)(eps); \
    if (fabs(_a - _b) > _e) { \
        printf("  FAIL  %s  (%s:%d)\n    expected: %.10f\n    actual:   %.10f  (eps=%.10f)\n", \
            g_current_test, __FILE__, __LINE__, _b, _a, _e); \
        g_current_test_ok = 0; ++g_assert_failed; \
    } \
} while(0)

#define ASSERT_TRUE(expr) do { \
    ++g_assert_total; \
    if (!(expr)) { \
        printf("  FAIL  %s  (%s:%d)\n    expected true: %s\n", \
            g_current_test, __FILE__, __LINE__, #expr); \
        g_current_test_ok = 0; ++g_assert_failed; \
    } \
} while(0)

#define ASSERT_FALSE(expr) do { \
    ++g_assert_total; \
    if ((expr)) { \
        printf("  FAIL  %s  (%s:%d)\n    expected false: %s\n", \
            g_current_test, __FILE__, __LINE__, #expr); \
        g_current_test_ok = 0; ++g_assert_failed; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    ++g_assert_total; \
    const char* _a = (a); const char* _b = (b); \
    if (strcmp(_a, _b) != 0) { \
        printf("  FAIL  %s  (%s:%d)\n    expected: \"%s\"\n    actual:   \"%s\"\n", \
            g_current_test, __FILE__, __LINE__, _b, _a); \
        g_current_test_ok = 0; ++g_assert_failed; \
    } \
} while(0)

#define ASSERT_STR_CONTAINS(hay, needle) do { \
    ++g_assert_total; \
    const char* _h = (hay); const char* _n = (needle); \
    if (!strstr(_h, _n)) { \
        printf("  FAIL  %s  (%s:%d)\n    string: \"%s\"\n    missing: \"%s\"\n", \
            g_current_test, __FILE__, __LINE__, _h, _n); \
        g_current_test_ok = 0; ++g_assert_failed; \
    } \
} while(0)

/* ── Summary printer ── */

static void test_print_summary(void) {
    printf("\n══════════════════════════════════════════\n");
    printf("  Tests:   %d passed, %d failed, %d total\n",
        g_test_passed, g_test_failed, g_test_total);
    printf("  Asserts: %d passed, %d failed, %d total\n",
        g_assert_total - g_assert_failed, g_assert_failed, g_assert_total);
    printf("══════════════════════════════════════════\n");
    if (g_test_failed == 0) {
        printf("  ALL TESTS PASSED\n");
    } else {
        printf("  %d TEST(S) FAILED\n", g_test_failed);
    }
    printf("══════════════════════════════════════════\n");
}

#endif /* CPSS_TEST_HARNESS_H */
