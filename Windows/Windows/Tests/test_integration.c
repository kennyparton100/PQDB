/**
 * test_integration.c - Integration tests for the CPSS Viewer engine.
 *
 * Tests NQL functions, number types, factorisation methods, and prime-pair
 * spaces without requiring a loaded CPSS database (uses Miller-Rabin fallback).
 *
 * Build (MSVC, from the Windows/ directory):
 *   cl /O2 /W4 /DCPSS_TEST_MODE Tests\test_integration.c zlib.lib /Fe:test_runner.exe
 *
 * Run:
 *   test_runner.exe
 */

/* Include the full amalgamation so all internal APIs are available */
#include "../CompressedPrimalityStateStreamViewer.c"
#include "test_harness.h"

/* ══════════════════════════════════════════════════════════════════════
 * NQL FUNCTION TESTS (no DB required — pass NULL as db_ctx)
 * ══════════════════════════════════════════════════════════════════════ */

TEST(nql_is_prime_basic) {
    ASSERT_TRUE(miller_rabin_u64(2u));
    ASSERT_TRUE(miller_rabin_u64(3u));
    ASSERT_TRUE(miller_rabin_u64(97u));
    ASSERT_TRUE(miller_rabin_u64(1000003u));
    ASSERT_FALSE(miller_rabin_u64(1u));
    ASSERT_FALSE(miller_rabin_u64(4u));
    ASSERT_FALSE(miller_rabin_u64(100u));
    ASSERT_FALSE(miller_rabin_u64(561u)); /* Carmichael number, not prime */
}

TEST(nql_totient) {
    ASSERT_UINT_EQ(nql_totient(1u, NULL), 1u);
    ASSERT_UINT_EQ(nql_totient(2u, NULL), 1u);
    ASSERT_UINT_EQ(nql_totient(6u, NULL), 2u);
    ASSERT_UINT_EQ(nql_totient(10u, NULL), 4u);
    ASSERT_UINT_EQ(nql_totient(12u, NULL), 4u);
    ASSERT_UINT_EQ(nql_totient(100u, NULL), 40u);
}

TEST(nql_mobius) {
    ASSERT_INT_EQ(nql_mobius(1u, NULL), 1);
    ASSERT_INT_EQ(nql_mobius(2u, NULL), -1);
    ASSERT_INT_EQ(nql_mobius(6u, NULL), 1);   /* 2*3, even number of factors */
    ASSERT_INT_EQ(nql_mobius(4u, NULL), 0);   /* 2^2, not squarefree */
    ASSERT_INT_EQ(nql_mobius(30u, NULL), -1); /* 2*3*5, 3 factors */
}

TEST(nql_sigma) {
    ASSERT_UINT_EQ(nql_sigma(1u, NULL), 1u);
    ASSERT_UINT_EQ(nql_sigma(6u, NULL), 12u);   /* 1+2+3+6 */
    ASSERT_UINT_EQ(nql_sigma(12u, NULL), 28u);  /* 1+2+3+4+6+12 */
    ASSERT_UINT_EQ(nql_sigma(28u, NULL), 56u);  /* perfect number: sigma(28)=56 */
}

TEST(nql_num_divisors) {
    ASSERT_UINT_EQ(nql_num_divisors(1u, NULL), 1u);
    ASSERT_UINT_EQ(nql_num_divisors(6u, NULL), 4u);
    ASSERT_UINT_EQ(nql_num_divisors(12u, NULL), 6u);
    ASSERT_UINT_EQ(nql_num_divisors(60u, NULL), 12u);
}

TEST(nql_omega_bigomega) {
    ASSERT_UINT_EQ(nql_omega(1u, NULL), 0u);
    ASSERT_UINT_EQ(nql_omega(12u, NULL), 2u);  /* 2^2 * 3 => 2 distinct */
    ASSERT_UINT_EQ(nql_omega(30u, NULL), 3u);  /* 2*3*5 */
    ASSERT_UINT_EQ(nql_bigomega(12u, NULL), 3u); /* 2+2+3 => 2^2*3 => 3 with mult */
    ASSERT_UINT_EQ(nql_bigomega(30u, NULL), 3u); /* 2*3*5 => 3 */
}

/* ── Phase 2: New function tests ── */

TEST(nql_liouville) {
    ASSERT_INT_EQ(nql_liouville(1u, NULL), 1);
    ASSERT_INT_EQ(nql_liouville(2u, NULL), -1);  /* 1 prime factor */
    ASSERT_INT_EQ(nql_liouville(4u, NULL), 1);   /* 2 prime factors with mult */
    ASSERT_INT_EQ(nql_liouville(6u, NULL), 1);   /* 2 distinct factors */
    ASSERT_INT_EQ(nql_liouville(8u, NULL), -1);  /* 2^3, 3 factors */
    ASSERT_INT_EQ(nql_liouville(12u, NULL), -1); /* 2^2*3, 3 factors */
}

TEST(nql_multiplicative_order) {
    ASSERT_UINT_EQ(nql_multiplicative_order(2u, 7u), 3u);  /* 2^3=8≡1 mod 7 */
    ASSERT_UINT_EQ(nql_multiplicative_order(3u, 7u), 6u);  /* 3 is a generator mod 7 */
    ASSERT_UINT_EQ(nql_multiplicative_order(2u, 5u), 4u);  /* 2^4=16≡1 mod 5 */
    ASSERT_UINT_EQ(nql_multiplicative_order(4u, 6u), 0u);  /* gcd(4,6)!=1 */
    ASSERT_UINT_EQ(nql_multiplicative_order(1u, 7u), 1u);  /* 1^k=1 always */
}

TEST(nql_primitive_root) {
    ASSERT_UINT_EQ(nql_primitive_root(2u, NULL), 1u);
    ASSERT_UINT_EQ(nql_primitive_root(5u, NULL), 2u);
    ASSERT_UINT_EQ(nql_primitive_root(7u, NULL), 3u);
    ASSERT_UINT_EQ(nql_primitive_root(11u, NULL), 2u);
    ASSERT_UINT_EQ(nql_primitive_root(13u, NULL), 2u);
}

TEST(nql_primitive_root_count) {
    /* phi(p-1) for prime p */
    ASSERT_UINT_EQ(nql_primitive_root_count(5u, NULL), 2u);   /* phi(4)=2 */
    ASSERT_UINT_EQ(nql_primitive_root_count(7u, NULL), 2u);   /* phi(6)=2 */
    ASSERT_UINT_EQ(nql_primitive_root_count(11u, NULL), 4u);  /* phi(10)=4 */
    ASSERT_UINT_EQ(nql_primitive_root_count(13u, NULL), 4u);  /* phi(12)=4 */
}

TEST(nql_chebyshev_theta) {
    /* theta(10) = log(2)+log(3)+log(5)+log(7) */
    double expected = log(2.0) + log(3.0) + log(5.0) + log(7.0);
    ASSERT_FLOAT_NEAR(nql_chebyshev_theta(10u, NULL), expected, 1e-10);
}

TEST(nql_chebyshev_psi) {
    /* psi(10) = log(2)+log(4)+log(8)+log(3)+log(9)+log(5)+log(7) */
    /* = 3*log(2) + 2*log(3) + log(5) + log(7) */
    double expected = 3.0*log(2.0) + 2.0*log(3.0) + log(5.0) + log(7.0);
    ASSERT_FLOAT_NEAR(nql_chebyshev_psi(10u, NULL), expected, 1e-10);
}

TEST(nql_radical) {
    ASSERT_UINT_EQ(nql_radical(1u, NULL), 1u);
    ASSERT_UINT_EQ(nql_radical(12u, NULL), 6u);   /* rad(2^2*3) = 2*3 */
    ASSERT_UINT_EQ(nql_radical(30u, NULL), 30u);   /* squarefree */
    ASSERT_UINT_EQ(nql_radical(72u, NULL), 6u);    /* rad(2^3*3^2) = 2*3 */
}

TEST(nql_sopfr) {
    ASSERT_UINT_EQ(nql_sopfr(1u, NULL), 0u);
    ASSERT_UINT_EQ(nql_sopfr(12u, NULL), 7u);   /* 2+2+3 */
    ASSERT_UINT_EQ(nql_sopfr(30u, NULL), 10u);  /* 2+3+5 */
    ASSERT_UINT_EQ(nql_sopfr(8u, NULL), 6u);    /* 2+2+2 */
}

TEST(nql_digital_root) {
    ASSERT_UINT_EQ(nql_digital_root(0u), 0u);
    ASSERT_UINT_EQ(nql_digital_root(1u), 1u);
    ASSERT_UINT_EQ(nql_digital_root(9u), 9u);
    ASSERT_UINT_EQ(nql_digital_root(123u), 6u);  /* 1+2+3=6 */
    ASSERT_UINT_EQ(nql_digital_root(999u), 9u);  /* 9+9+9=27, 2+7=9 */
}

TEST(nql_is_carmichael) {
    ASSERT_TRUE(nql_is_carmichael(561u, NULL));    /* 3*11*17 */
    ASSERT_TRUE(nql_is_carmichael(1105u, NULL));   /* 5*13*17 */
    ASSERT_TRUE(nql_is_carmichael(1729u, NULL));   /* 7*13*19 */
    ASSERT_FALSE(nql_is_carmichael(560u, NULL));
    ASSERT_FALSE(nql_is_carmichael(100u, NULL));
    ASSERT_FALSE(nql_is_carmichael(97u, NULL));    /* prime */
}

/* ── Number type tests ── */

TEST(type_carmichael) {
    ASSERT_TRUE(nql_type_carmichael_test(561u, NULL));
    ASSERT_TRUE(nql_type_carmichael_test(1105u, NULL));
    ASSERT_FALSE(nql_type_carmichael_test(100u, NULL));
    ASSERT_UINT_EQ(nql_type_carmichael_next(1u, NULL), 561u);
    ASSERT_UINT_EQ(nql_type_carmichael_next(562u, NULL), 1105u);
}

TEST(type_cuban_prime) {
    /* Cuban primes: 7, 19, 37, 61, 127, 271, ... */
    ASSERT_TRUE(nql_type_cuban_test(7u, NULL));    /* 3*1*1+3*1+1 = 7 */
    ASSERT_TRUE(nql_type_cuban_test(19u, NULL));   /* 3*4+6+1=19 */
    ASSERT_TRUE(nql_type_cuban_test(37u, NULL));   /* 3*9+9+1=37 */
    ASSERT_FALSE(nql_type_cuban_test(11u, NULL));  /* prime but not cuban */
    ASSERT_UINT_EQ(nql_type_cuban_next(1u, NULL), 7u);
    ASSERT_UINT_EQ(nql_type_cuban_next(8u, NULL), 19u);
}

/* ══════════════════════════════════════════════════════════════════════
 * FACTORISATION TESTS
 * ══════════════════════════════════════════════════════════════════════ */

TEST(factor_rho_basic) {
    FactorResult fr = cpss_factor_rho(143u, 0u, 0u); /* 11 * 13 */
    ASSERT_TRUE(fr.fully_factored);
    ASSERT_TRUE(fr.factor_count >= 2u);
}

TEST(factor_rho_prime) {
    FactorResult fr = cpss_factor_rho(97u, 0u, 0u);
    /* Should find 97 is prime (single factor, exponent 1) */
    ASSERT_TRUE(fr.factor_count >= 1u);
}

TEST(factor_fermat_near_square) {
    FactorResult fr = cpss_factor_fermat(5959u, 0u); /* 59*101 */
    ASSERT_TRUE(fr.fully_factored);
}

TEST(factor_squfof) {
    FactorResult fr = cpss_factor_squfof(8051u); /* 83*97 */
    ASSERT_TRUE(fr.fully_factored);
}

/* ══════════════════════════════════════════════════════════════════════
 * PRIME-PAIR SPACE TESTS
 * ══════════════════════════════════════════════════════════════════════ */

TEST(value_space_basics) {
    uint64_t dist = value_space_additive_distance(11u, 13u);
    ASSERT_UINT_EQ(dist, 2u);
    double ratio = value_space_balance_ratio(11u, 13u);
    ASSERT_TRUE(ratio > 0.8 && ratio <= 1.0);
}

TEST(log_space_basics) {
    double u, v;
    ASSERT_TRUE(log_space_coords(11u, 13u, &u, &v));
    ASSERT_FLOAT_NEAR(u, log(11.0), 1e-10);
    ASSERT_FLOAT_NEAR(v, log(13.0), 1e-10);
    double diag = log_space_diagonal_distance(11u, 13u);
    ASSERT_TRUE(diag >= 0.0);
}

TEST(fermat_space_basics) {
    double A, B;
    fermat_space_from_factors(11u, 13u, &A, &B);
    /* A = (p+q)/2 = 12, B = (q-p)/2 = 1 */
    ASSERT_FLOAT_NEAR(A, 12.0, 1e-10);
    ASSERT_FLOAT_NEAR(B, 1.0, 1e-10);
}

TEST(cfrac_space_basics) {
    /* CF expansion of 13/11 = [1; 5, 2] */
    CFracExpansion cf = cfrac_expand(11u, 13u);
    ASSERT_TRUE(cf.length > 0u);
    ASSERT_UINT_EQ(cf.quotients[0], 1u); /* 13/11 = 1 remainder 2 */
    double diff = cfrac_difficulty(11u, 13u);
    ASSERT_TRUE(diff >= 0.0);
}

TEST(cfrac_convergent) {
    /* CF of 13/11 = [1; 5, 2] => convergents: 1/1, 6/5, 13/11 */
    CFracExpansion cf = cfrac_expand(11u, 13u);
    uint64_t num, den;
    cfrac_convergent(&cf, 0u, &num, &den);
    ASSERT_UINT_EQ(num, 1u);
    ASSERT_UINT_EQ(den, 1u);
    cfrac_convergent(&cf, cf.length - 1u, &num, &den);
    ASSERT_UINT_EQ(num, 13u);
    ASSERT_UINT_EQ(den, 11u);
}

TEST(lattice_space_basics) {
    LatticeMetrics lm = lattice_analyse(11u, 13u);
    ASSERT_TRUE(lm.shortest_vector > 0.0);
    ASSERT_TRUE(lm.orthogonality_defect >= 1.0);
    ASSERT_TRUE(lm.balance_angle > 0.0);
    double diff = lattice_difficulty(11u, 13u);
    ASSERT_TRUE(diff >= 0.0);
}

TEST(lattice_near_square) {
    /* Near-square factors should have distinctive lattice properties */
    LatticeMetrics balanced = lattice_analyse(97u, 101u);
    LatticeMetrics skewed   = lattice_analyse(3u, 3271u);
    /* Balanced pair should have shorter shortest vector relative to sqrt(N) */
    double sqrtN_bal = sqrt(97.0 * 101.0);
    double sqrtN_skew = sqrt(3.0 * 3271.0);
    double rel_bal = balanced.shortest_vector / sqrtN_bal;
    double rel_skew = skewed.shortest_vector / sqrtN_skew;
    ASSERT_TRUE(rel_bal < rel_skew);
}

/* ══════════════════════════════════════════════════════════════════════
 * NQL WRAPPER FUNCTION TESTS (using NqlValue interface)
 * ══════════════════════════════════════════════════════════════════════ */

TEST(nql_fn_wrappers_phase2) {
    /* Ensure wrapper functions work through the NqlValue interface */
    nql_init_registry();

    NqlValue args[2];

    /* LIOUVILLE(2) = -1 */
    args[0] = nql_val_int(2);
    NqlValue r = nql_fn_liouville(args, 1u, NULL);
    ASSERT_INT_EQ(nql_val_as_int(&r), -1);

    /* RADICAL(12) = 6 */
    args[0] = nql_val_int(12);
    r = nql_fn_radical(args, 1u, NULL);
    ASSERT_INT_EQ(nql_val_as_int(&r), 6);

    /* SOPFR(12) = 7 */
    args[0] = nql_val_int(12);
    r = nql_fn_sopfr(args, 1u, NULL);
    ASSERT_INT_EQ(nql_val_as_int(&r), 7);

    /* DIGITAL_ROOT(123) = 6 */
    args[0] = nql_val_int(123);
    r = nql_fn_digital_root(args, 1u, NULL);
    ASSERT_INT_EQ(nql_val_as_int(&r), 6);

    /* IS_CARMICHAEL(561) = true */
    args[0] = nql_val_int(561);
    r = nql_fn_is_carmichael(args, 1u, NULL);
    ASSERT_TRUE(nql_val_is_truthy(&r));

    /* MULTIPLICATIVE_ORDER(2, 7) = 3 */
    args[0] = nql_val_int(2);
    args[1] = nql_val_int(7);
    r = nql_fn_multiplicative_order(args, 2u, NULL);
    ASSERT_INT_EQ(nql_val_as_int(&r), 3);

    /* PRIMITIVE_ROOT(7) = 3 */
    args[0] = nql_val_int(7);
    r = nql_fn_primitive_root(args, 1u, NULL);
    ASSERT_INT_EQ(nql_val_as_int(&r), 3);
}

/* ══════════════════════════════════════════════════════════════════════
 * ARITHMETIC HELPER TESTS
 * ══════════════════════════════════════════════════════════════════════ */

TEST(mulmod_u64_basic) {
    ASSERT_UINT_EQ(mulmod_u64(3u, 4u, 5u), 2u);
    ASSERT_UINT_EQ(mulmod_u64(7u, 8u, 13u), 4u);
    /* Large values */
    ASSERT_UINT_EQ(mulmod_u64(UINT64_MAX - 1u, 2u, UINT64_MAX), UINT64_MAX - 2u);
}

TEST(isqrt_u64_basic) {
    ASSERT_UINT_EQ(isqrt_u64(0u), 0u);
    ASSERT_UINT_EQ(isqrt_u64(1u), 1u);
    ASSERT_UINT_EQ(isqrt_u64(4u), 2u);
    ASSERT_UINT_EQ(isqrt_u64(9u), 3u);
    ASSERT_UINT_EQ(isqrt_u64(10u), 3u);
    ASSERT_UINT_EQ(isqrt_u64(100u), 10u);
}

TEST(digit_helpers) {
    ASSERT_UINT_EQ(nql_digit_count(0u), 1u);
    ASSERT_UINT_EQ(nql_digit_count(999u), 3u);
    ASSERT_UINT_EQ(nql_digit_sum(123u), 6u);
    ASSERT_UINT_EQ(nql_digit_reverse(123u), 321u);
    ASSERT_TRUE(nql_is_palindrome(121u));
    ASSERT_FALSE(nql_is_palindrome(123u));
}

/* ══════════════════════════════════════════════════════════════════════
 * MAIN — run all tests
 * ══════════════════════════════════════════════════════════════════════ */

int main(void) {
    /* Initialise global state needed by the engine */
    init_popcount_table();
    init_wheel();
    g_nql_registry_init = false; /* force fresh registry init */

    printf("CPSS Viewer Integration Tests\n");
    printf("══════════════════════════════════════════\n\n");

    /* NQL existing function tests */
    RUN_TEST(nql_is_prime_basic);
    RUN_TEST(nql_totient);
    RUN_TEST(nql_mobius);
    RUN_TEST(nql_sigma);
    RUN_TEST(nql_num_divisors);
    RUN_TEST(nql_omega_bigomega);

    /* Phase 2 new function tests */
    RUN_TEST(nql_liouville);
    RUN_TEST(nql_multiplicative_order);
    RUN_TEST(nql_primitive_root);
    RUN_TEST(nql_primitive_root_count);
    RUN_TEST(nql_chebyshev_theta);
    RUN_TEST(nql_chebyshev_psi);
    RUN_TEST(nql_radical);
    RUN_TEST(nql_sopfr);
    RUN_TEST(nql_digital_root);
    RUN_TEST(nql_is_carmichael);

    /* Number type tests */
    RUN_TEST(type_carmichael);
    RUN_TEST(type_cuban_prime);

    /* Factorisation tests */
    RUN_TEST(factor_rho_basic);
    RUN_TEST(factor_rho_prime);
    RUN_TEST(factor_fermat_near_square);
    RUN_TEST(factor_squfof);

    /* Space tests */
    RUN_TEST(value_space_basics);
    RUN_TEST(log_space_basics);
    RUN_TEST(fermat_space_basics);
    RUN_TEST(cfrac_space_basics);
    RUN_TEST(cfrac_convergent);
    RUN_TEST(lattice_space_basics);
    RUN_TEST(lattice_near_square);

    /* NQL wrapper tests */
    RUN_TEST(nql_fn_wrappers_phase2);

    /* Arithmetic helpers */
    RUN_TEST(mulmod_u64_basic);
    RUN_TEST(isqrt_u64_basic);
    RUN_TEST(digit_helpers);

    test_print_summary();
    return g_test_failed > 0 ? 1 : 0;
}
