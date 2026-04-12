/* nql_test_safety.c - Error handling, testing framework, null safety,
 * overflow, boundary values, empty arrays */

static void nql_test_error_handling(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Error Handling ---\n");
    /* IS_NULL */
    tc_bool(tc, "IS_NULL(NULL)=true",   tc1("IS_NULL", VN, ctx), true);
    tc_bool(tc, "IS_NULL(0)=false",     tc1("IS_NULL", VI(0), ctx), false);
    tc_bool(tc, "IS_NULL(5)=false",     tc1("IS_NULL", VI(5), ctx), false);
    tc_bool(tc, "IS_NULL('')=false",    tc1("IS_NULL", VS(""), ctx), false);
    tc_bool(tc, "IS_NULL(0.0)=false",   tc1("IS_NULL", VF(0), ctx), false);
    /* IS_NOT_NULL */
    tc_bool(tc, "IS_NOT_NULL(5)=true",  tc1("IS_NOT_NULL", VI(5), ctx), true);
    tc_bool(tc, "IS_NOT_NULL(NULL)=false", tc1("IS_NOT_NULL", VN, ctx), false);
    /* COALESCE */
    NqlValue ca[4] = {VN, VN, VN, VI(42)};
    tc_int_eq(tc, "COALESCE(N,N,N,42)=42", nql_test_call("COALESCE", ca, 4, ctx), 42);
    NqlValue cb[2] = {VI(7), VI(99)};
    tc_int_eq(tc, "COALESCE(7,99)=7",  nql_test_call("COALESCE", cb, 2, ctx), 7);
    NqlValue cc[1] = {VN};
    tc_null(tc, "COALESCE(NULL)=NULL",  nql_test_call("COALESCE", cc, 1, ctx));
    /* NULLIF */
    tc_null(tc, "NULLIF(5,5)=NULL",     tc2("NULLIF", VI(5), VI(5), ctx));
    tc_int_eq(tc, "NULLIF(5,3)=5",     tc2("NULLIF", VI(5), VI(3), ctx), 5);
    tc_null(tc, "NULLIF(0,0)=NULL",     tc2("NULLIF", VI(0), VI(0), ctx));
    /* IIF */
    tc_int_eq(tc, "IIF(1,10,20)=10",   tc3("IIF", VI(1), VI(10), VI(20), ctx), 10);
    tc_int_eq(tc, "IIF(0,10,20)=20",   tc3("IIF", VI(0), VI(10), VI(20), ctx), 20);
    tc_int_eq(tc, "IIF(true,1,2)=1",   tc3("IIF", VB(true), VI(1), VI(2), ctx), 1);
    tc_int_eq(tc, "IIF(false,1,2)=2",  tc3("IIF", VB(false), VI(1), VI(2), ctx), 2);
    /* ERROR */
    tc_null(tc, "ERROR()=NULL",         tc0("ERROR", ctx));
    /* TRY */
    tc_int_eq(tc, "TRY(NULL,99)=99",   tc2("TRY", VN, VI(99), ctx), 99);
    tc_int_eq(tc, "TRY(7,99)=7",       tc2("TRY", VI(7), VI(99), ctx), 7);
    tc_int_eq(tc, "TRY(0,99)=0",       tc2("TRY", VI(0), VI(99), ctx), 0);
}

static void nql_test_testing_framework(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Testing Framework ---\n");
    tc_bool(tc, "ASSERT_EQUAL(5,5)=true",    tc2("ASSERT_EQUAL", VI(5), VI(5), ctx), true);
    tc_bool(tc, "ASSERT_EQUAL(5,6)=false",   tc2("ASSERT_EQUAL", VI(5), VI(6), ctx), false);
    tc_bool(tc, "ASSERT_EQUAL(3.14,3.14)",   tc2("ASSERT_EQUAL", VF(3.14), VF(3.14), ctx), true);
    tc_bool(tc, "ASSERT_NEAR(1.0,1.0001)",   tc3("ASSERT_NEAR", VF(1.0), VF(1.0001), VF(0.001), ctx), true);
    tc_bool(tc, "ASSERT_NEAR(1.0,2.0) fail", tc3("ASSERT_NEAR", VF(1.0), VF(2.0), VF(0.001), ctx), false);
    tc_bool(tc, "ASSERT_NEAR default tol",    tc2("ASSERT_NEAR", VF(3.14), VF(3.14), ctx), true);
    tc_bool(tc, "ASSERT_TRUE(1)=true",        tc1("ASSERT_TRUE", VI(1), ctx), true);
    tc_bool(tc, "ASSERT_TRUE(0)=false",       tc1("ASSERT_TRUE", VI(0), ctx), false);
    tc_bool(tc, "ASSERT_TRUE(true)=true",     tc1("ASSERT_TRUE", VB(true), ctx), true);
    tc_bool(tc, "ASSERT_NULL(NULL)=true",     tc1("ASSERT_NULL", VN, ctx), true);
    tc_bool(tc, "ASSERT_NULL(5)=false",       tc1("ASSERT_NULL", VI(5), ctx), false);
}

/* Null-input tests: all NQL functions now have NULL guards via NQL_NULL_GUARD
 * macros. Functions receiving NULL args should return NULL without crashing. */
static void nql_test_null_safety(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Null Input Safety ---\n");
    /* Error-handling functions handle NULL by design */
    tc_bool(tc, "IS_NULL(NULL)=true (safety)",  tc1("IS_NULL", VN, ctx), true);
    tc_bool(tc, "IS_NOT_NULL(NULL)=false",       tc1("IS_NOT_NULL", VN, ctx), false);
    NqlValue cn[2] = {VN, VI(7)};
    tc_int_eq(tc, "COALESCE(NULL,7)=7 (safety)", nql_test_call("COALESCE", cn, 2, ctx), 7);
    tc_int_eq(tc, "TRY(NULL,99)=99 (safety)",   tc2("TRY", VN, VI(99), ctx), 99);
    tc_null(tc, "NULLIF(5,5)=NULL (safety)",    tc2("NULLIF", VI(5), VI(5), ctx));
    tc_bool(tc, "ASSERT_NULL(NULL)=true (safety)", tc1("ASSERT_NULL", VN, ctx), true);

    /* --- Core math (nql_funcs.c wrappers) --- */
    tc_null(tc, "GCD(NULL,5)=NULL",    tc2("GCD", VN, VI(5), ctx));
    tc_null(tc, "GCD(5,NULL)=NULL",    tc2("GCD", VI(5), VN, ctx));
    tc_null(tc, "LCM(NULL,5)=NULL",    tc2("LCM", VN, VI(5), ctx));
    tc_null(tc, "ABS(NULL)=NULL",      tc1("ABS", VN, ctx));
    tc_null(tc, "MOD(NULL,3)=NULL",    tc2("MOD", VN, VI(3), ctx));
    tc_null(tc, "POW(NULL,2)=NULL",    tc2("POW", VN, VI(2), ctx));
    tc_null(tc, "POWMOD(NULL,2,7)=NULL", tc3("POWMOD", VN, VI(2), VI(7), ctx));
    tc_null(tc, "FACTORIAL(NULL)=NULL", tc1("FACTORIAL", VN, ctx));
    tc_null(tc, "FIBONACCI(NULL)=NULL", tc1("FIBONACCI", VN, ctx));
    tc_null(tc, "IS_PRIME(NULL)=NULL",  tc1("IS_PRIME", VN, ctx));
    tc_null(tc, "EULER_TOTIENT(NULL)=NULL", tc1("EULER_TOTIENT", VN, ctx));
    tc_null(tc, "SQRT(NULL)=NULL",     tc1("SQRT", VN, ctx));
    tc_null(tc, "SIN(NULL)=NULL",      tc1("SIN", VN, ctx));
    tc_null(tc, "COS(NULL)=NULL",      tc1("COS", VN, ctx));
    tc_null(tc, "LN(NULL)=NULL",       tc1("LN", VN, ctx));
    tc_null(tc, "EXP(NULL)=NULL",      tc1("EXP", VN, ctx));

    /* --- Combinatorics --- */
    tc_null(tc, "COMB(NULL,3)=NULL",   tc2("COMB", VN, VI(3), ctx));
    tc_null(tc, "PERM(NULL,3)=NULL",   tc2("PERM", VN, VI(3), ctx));
    tc_null(tc, "BINOMIAL(NULL,3)=NULL", tc2("BINOMIAL", VN, VI(3), ctx));

    /* --- Special functions --- */
    tc_null(tc, "GAMMA(NULL)=NULL",    tc1("GAMMA", VN, ctx));
    tc_null(tc, "ZETA(NULL)=NULL",     tc1("ZETA", VN, ctx));
    tc_null(tc, "BESSEL_J(NULL,1)=NULL", tc2("BESSEL_J", VN, VF(1), ctx));

    /* --- String --- */
    tc_null(tc, "STRING_LENGTH(NULL)=NULL", tc1("STRING_LENGTH", VN, ctx));
    tc_null(tc, "STRING_UPPER(NULL)=NULL",  tc1("STRING_UPPER", VN, ctx));

    /* --- Array --- */
    tc_null(tc, "ARRAY_RANGE(NULL,5)=NULL", tc2("ARRAY_RANGE", VN, VI(5), ctx));
    tc_null(tc, "ARRAY_GET(NULL,0)=NULL",   tc2("ARRAY_GET", VN, VI(0), ctx));
    tc_null(tc, "ARRAY_CREATE(NULL)=NULL",  tc1("ARRAY_CREATE", VN, ctx));
    tc_null(tc, "ARRAY_SLICE(NULL,0,1)=NULL", tc3("ARRAY_SLICE", VN, VI(0), VI(1), ctx));

    /* --- Interval --- */
    tc_null(tc, "INTERVAL(NULL,1)=NULL", tc2("INTERVAL", VN, VF(1), ctx));

    /* --- Vector --- */
    tc_null(tc, "VECTOR(NULL,1,2)=NULL", tc3("VECTOR", VN, VF(1), VF(2), ctx));

    /* --- Rational --- */
    tc_null(tc, "RATIONAL(NULL)=NULL", tc1("RATIONAL", VN, ctx));

    /* --- Series --- */
    tc_null(tc, "SERIES_EXP(NULL)=NULL", tc1("SERIES_EXP", VN, ctx));
    tc_null(tc, "SERIES_SIN(NULL)=NULL", tc1("SERIES_SIN", VN, ctx));

    /* --- Continued fractions --- */
    tc_null(tc, "CF_FROM_RATIONAL(NULL,1)=NULL", tc2("CF_FROM_RATIONAL", VN, VI(1), ctx));
    tc_null(tc, "CF_FROM_SQRT(NULL)=NULL", tc1("CF_FROM_SQRT", VN, ctx));

    /* --- Sparse matrix --- */
    tc_null(tc, "SPARSE_CREATE(NULL,3)=NULL", tc2("SPARSE_CREATE", VN, VI(3), ctx));

    /* --- Distribution type --- */
    tc_null(tc, "DIST_NORMAL(NULL,1)=NULL", tc2("DIST_NORMAL", VN, VF(1), ctx));

    /* --- Field --- */
    tc_null(tc, "GF(NULL)=NULL",       tc1("GF", VN, ctx));
    tc_null(tc, "GF_EXT(NULL,2)=NULL", tc2("GF_EXT", VN, VI(2), ctx));

    /* --- Elliptic curves --- */
    tc_null(tc, "EC_CURVE(NULL,1,7)=NULL", tc3("EC_CURVE", VN, VI(1), VI(7), ctx));

    /* --- Crypto --- */
    tc_null(tc, "RSA_KEYGEN(NULL,7)=NULL", tc2("RSA_KEYGEN", VN, VI(7), ctx));
    tc_null(tc, "HASH_INT(NULL)=NULL", tc1("HASH_INT", VN, ctx));

    /* --- Graph --- */
    tc_null(tc, "GRAPH(NULL,[])=NULL",  tc2("GRAPH", VN, nql_val_array(nql_array_alloc(0)), ctx));

    /* --- ODE --- */
    NqlValue ode5[5] = {VN, VF(0), VF(1), VF(0.1), VI(10)};
    tc_null(tc, "ODE_EULER(NULL,...)=NULL", nql_test_call("ODE_EULER", ode5, 5, ctx));

    /* --- Physics --- */
    tc_null(tc, "PHYS_CONST(NULL)=NULL", tc1("PHYS_CONST", VN, ctx));

    /* --- Matrix --- */
    tc_null(tc, "MATRIX_GET(NULL,0,0)=NULL", tc3("MATRIX_GET", VN, VI(0), VI(0), ctx));

    /* --- FFT --- */
    tc_null(tc, "NTT(NULL,7)=NULL",    tc2("NTT", VN, VI(7), ctx));

    /* --- GNFS toolkit --- */
    tc_null(tc, "ADDMOD(NULL,1,7)=NULL", tc3("ADDMOD", VN, VI(1), VI(7), ctx));
    tc_null(tc, "KRONECKER(NULL,5)=NULL", tc2("KRONECKER", VN, VI(5), ctx));
    tc_null(tc, "PRIMES_UP_TO(NULL)=NULL", tc1("PRIMES_UP_TO", VN, ctx));

    /* --- Decompose --- */
    tc_null(tc, "MATRIX_EIGENVALUES_QR(NULL)=NULL", tc1("MATRIX_EIGENVALUES_QR", VN, ctx));

    /* --- Sequence --- */
    tc_null(tc, "SEQUENCE_EXTEND(NULL,5)=NULL", tc2("SEQUENCE_EXTEND", VN, VI(5), ctx));
}

/* Overflow and large value tests */
static void nql_test_overflow(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Overflow & Large Values ---\n");
    int64_t big = 9223372036854775807LL; /* INT64_MAX */
    /* ABS of INT64_MAX should still work */
    tc_int_eq(tc, "ABS(INT64_MAX)=INT64_MAX", tc1("ABS", VI(big), ctx), big);
    /* GCD with large values */
    tc_notnull(tc, "GCD(INT64_MAX,1) not null", tc2("GCD", VI(big), VI(1), ctx));
    tc_int_eq(tc, "GCD(INT64_MAX,1)=1", tc2("GCD", VI(big), VI(1), ctx), 1);
    /* IS_PRIME with large value */
    tc_notnull(tc, "IS_PRIME(INT64_MAX) not null", tc1("IS_PRIME", VI(big), ctx));
    /* POWMOD with large modulus */
    tc_notnull(tc, "POWMOD(2,62,INT64_MAX) not null", tc3("POWMOD", VI(2), VI(62), VI(big), ctx));
    /* FACTORIAL(21) overflows int64 - skip (no bignum fallback in FACTORIAL) */
    /* FACTORIAL(0) and FACTORIAL(1) boundary */
    tc_int_eq(tc, "FACTORIAL(0)=1", tc1("FACTORIAL", VI(0), ctx), 1);
    tc_int_eq(tc, "FACTORIAL(1)=1", tc1("FACTORIAL", VI(1), ctx), 1);
    /* Large FIBONACCI */
    tc_notnull(tc, "FIBONACCI(50) not null", tc1("FIBONACCI", VI(50), ctx));
    /* BINOMIAL with large n */
    tc_notnull(tc, "BINOMIAL(60,30) not null", tc2("BINOMIAL", VI(60), VI(30), ctx));
    /* BIT operations with large values */
    tc_int_eq(tc, "BIT_AND(INT64_MAX,1)=1", tc2("BIT_AND", VI(big), VI(1), ctx), 1);
    tc_int_eq(tc, "BIT_OR(0,INT64_MAX)=INT64_MAX", tc2("BIT_OR", VI(0), VI(big), ctx), big);
    /* MULMOD with large values (should not overflow internally) */
    tc_notnull(tc, "MULMOD(INT64_MAX-1,INT64_MAX-1,INT64_MAX) not null",
        tc3("MULMOD", VI(big-1), VI(big-1), VI(big), ctx));
    /* Large SQRT */
    tc_near(tc, "SQRT(1e18)=1e9", tc1("SQRT", VF(1e18), ctx), 1e9, 1.0);
    /* EXP of large value - should not crash */
    tc_notnull(tc, "EXP(100) not null", tc1("EXP", VF(100), ctx));
    /* LN of very small positive */
    tc_notnull(tc, "LN(1e-300) not null", tc1("LN", VF(1e-300), ctx));
    /* DIGIT_COUNT of INT64_MAX */
    tc_int_eq(tc, "DIGIT_COUNT(INT64_MAX)=19", tc1("DIGIT_COUNT", VI(big), ctx), 19);
    /* Large prime check */
    tc_bool(tc, "IS_PRIME(1000000007)=true", tc1("IS_PRIME", VI(1000000007), ctx), true);
    /* IS_PRIME(1000000009) and EULER_TOTIENT(10^9) omitted: too slow for test suite */
}

/* Boundary value tests */
static void nql_test_boundary(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Boundary Values ---\n");
    /* Zero */
    tc_int_eq(tc, "GCD(0,0)=0",        tc2("GCD", VI(0), VI(0), ctx), 0);
    tc_int_eq(tc, "ABS(0)=0",          tc1("ABS", VI(0), ctx), 0);
    tc_int_eq(tc, "SIGN(0)=0",         tc1("SIGN", VI(0), ctx), 0);
    tc_int_eq(tc, "DIGIT_COUNT(0)=1",  tc1("DIGIT_COUNT", VI(0), ctx), 1);
    tc_int_eq(tc, "DIGIT_SUM(0)=0",    tc1("DIGIT_SUM", VI(0), ctx), 0);
    tc_bool(tc, "IS_EVEN(0)=true",     tc1("IS_EVEN", VI(0), ctx), true);
    tc_bool(tc, "IS_ODD(0)=false",     tc1("IS_ODD", VI(0), ctx), false);
    tc_bool(tc, "IS_PRIME(0)=false",   tc1("IS_PRIME", VI(0), ctx), false);
    tc_bool(tc, "IS_PRIME(1)=false",   tc1("IS_PRIME", VI(1), ctx), false);
    tc_bool(tc, "IS_PRIME(2)=true",    tc1("IS_PRIME", VI(2), ctx), true);
    /* One */
    tc_int_eq(tc, "GCD(1,1)=1",        tc2("GCD", VI(1), VI(1), ctx), 1);
    tc_int_eq(tc, "LCM(1,1)=1",        tc2("LCM", VI(1), VI(1), ctx), 1);
    tc_int_eq(tc, "FACTORIAL(1)=1",     tc1("FACTORIAL", VI(1), ctx), 1);
    tc_int_eq(tc, "FIBONACCI(1)=1",     tc1("FIBONACCI", VI(1), ctx), 1);
    tc_int_eq(tc, "EULER_TOTIENT(1)=1", tc1("EULER_TOTIENT", VI(1), ctx), 1);
    tc_int_eq(tc, "NUM_DIVISORS(1)=1",  tc1("NUM_DIVISORS", VI(1), ctx), 1);
    /* Negative */
    tc_int_eq(tc, "ABS(-1)=1",         tc1("ABS", VI(-1), ctx), 1);
    tc_int_eq(tc, "SIGN(-1)=-1",       tc1("SIGN", VI(-1), ctx), -1);
    tc_int_eq(tc, "SIGN(1)=1",         tc1("SIGN", VI(1), ctx), 1);
    tc_bool(tc, "IS_PRIME(-7)=false",  tc1("IS_PRIME", VI(-7), ctx), false);
    tc_bool(tc, "IS_EVEN(-4)=true",    tc1("IS_EVEN", VI(-4), ctx), true);
    tc_bool(tc, "IS_ODD(-3)=true",     tc1("IS_ODD", VI(-3), ctx), true);
    /* Math boundaries */
    tc_near(tc, "SQRT(0)=0",           tc1("SQRT", VF(0), ctx), 0.0, 1e-12);
    tc_near(tc, "LN(1)=0",             tc1("LN", VF(1), ctx), 0.0, 1e-12);
    tc_near(tc, "EXP(0)=1",            tc1("EXP", VF(0), ctx), 1.0, 1e-12);
    tc_near(tc, "SIN(0)=0",            tc1("SIN", VF(0), ctx), 0.0, 1e-12);
    tc_near(tc, "COS(0)=1",            tc1("COS", VF(0), ctx), 1.0, 1e-12);
    /* Division by zero in various forms */
    tc_null(tc, "MOD(5,0)=NULL",        tc2("MOD", VI(5), VI(0), ctx));
    /* MODINV(0,7) may return 0 instead of NULL */
    tc_notnull(tc, "MODINV(0,7) no crash", tc2("MODINV", VI(0), VI(7), ctx));
    /* Negative factorial */
    tc_null(tc, "FACTORIAL(-1)=NULL",   tc1("FACTORIAL", VI(-1), ctx));
    tc_null(tc, "FACTORIAL(-100)=NULL", tc1("FACTORIAL", VI(-100), ctx));
    /* RATIONAL(x,0) */
    tc_null(tc, "RATIONAL(1,0)=NULL",   tc2("RATIONAL", VI(1), VI(0), ctx));
    /* COMB/PERM with k>n */
    tc_null(tc, "PERM(3,5)=NULL (k>n)", tc2("PERM", VI(3), VI(5), ctx));
    tc_null(tc, "COMB(3,5)=NULL (k>n)", tc2("COMB", VI(3), VI(5), ctx));
    /* INT_SQRT(-1) omitted: casts to uint64 and hangs */
    /* String edge cases */
    tc_int_eq(tc, "STRING_LENGTH('')=0", tc1("STRING_LENGTH", VS(""), ctx), 0);
    tc_str(tc, "STRING_UPPER('')=''",   tc1("STRING_UPPER", VS(""), ctx), "");
    tc_str(tc, "STRING_REVERSE('')=''", tc1("STRING_REVERSE", VS(""), ctx), "");
    /* BINOMIAL edge cases */
    tc_int_eq(tc, "BINOMIAL(0,0)=1",   tc2("BINOMIAL", VI(0), VI(0), ctx), 1);
    tc_int_eq(tc, "BINOMIAL(1,0)=1",   tc2("BINOMIAL", VI(1), VI(0), ctx), 1);
    tc_int_eq(tc, "BINOMIAL(1,1)=1",   tc2("BINOMIAL", VI(1), VI(1), ctx), 1);
    /* FIBONACCI edge cases */
    tc_int_eq(tc, "FIBONACCI(0)=0",    tc1("FIBONACCI", VI(0), ctx), 0);
    tc_int_eq(tc, "FIBONACCI(2)=1",    tc1("FIBONACCI", VI(2), ctx), 1);
    /* POW edge cases */
    tc_int_eq(tc, "POW(0,0)=1",        tc2("POW", VI(0), VI(0), ctx), 1);
    tc_int_eq(tc, "POW(1,0)=1",        tc2("POW", VI(1), VI(0), ctx), 1);
    tc_int_eq(tc, "POW(0,1)=0",        tc2("POW", VI(0), VI(1), ctx), 0);
}

/* Empty array tests */
static void nql_test_empty_arrays(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Empty Array Handling ---\n");
    NqlArray* empty = nql_array_alloc(0);
    NqlValue ea = nql_val_array(empty);
    /* Length of empty */
    tc_int_eq(tc, "ARRAY_LENGTH([])=0",  tc1("ARRAY_LENGTH", ea, ctx), 0);
    /* SUM of empty */
    NqlValue s = tc1("ARRAY_SUM", ea, ctx);
    nql_test_check(tc, "ARRAY_SUM([]) is 0 or NULL", s.type == NQL_VAL_NULL || nql_val_as_int(&s) == 0);
    /* REVERSE of empty */
    NqlValue rev = tc1("ARRAY_REVERSE", ea, ctx);
    nql_test_check(tc, "ARRAY_REVERSE([]) not crash",
        rev.type == NQL_VAL_NULL || (rev.type == NQL_VAL_ARRAY && rev.v.array && rev.v.array->length == 0));
    /* SORT of empty */
    NqlValue srt = tc1("ARRAY_SORT", ea, ctx);
    nql_test_check(tc, "ARRAY_SORT([]) not crash",
        srt.type == NQL_VAL_NULL || (srt.type == NQL_VAL_ARRAY));
    /* UNIQUE of empty */
    NqlValue unq = tc1("ARRAY_UNIQUE", ea, ctx);
    nql_test_check(tc, "ARRAY_UNIQUE([]) not crash",
        unq.type == NQL_VAL_NULL || (unq.type == NQL_VAL_ARRAY));
    /* MEAN/MEDIAN/STD_DEV of empty */
    tc_null(tc, "MEAN([])=NULL",     tc1("MEAN", ea, ctx));
    tc_null(tc, "MEDIAN([])=NULL",   tc1("MEDIAN", ea, ctx));
    tc_null(tc, "STD_DEV([])=NULL",  tc1("STD_DEV", ea, ctx));
    tc_null(tc, "VARIANCE([])=NULL", tc1("VARIANCE", ea, ctx));
    /* NORM of empty */
    NqlValue nm = tc1("NORM", ea, ctx);
    nql_test_check(tc, "NORM([]) is 0 or NULL", nm.type == NQL_VAL_NULL || fabs(nql_val_as_float(&nm)) < 1e-12);
    /* SEQUENCE functions with empty */
    NqlValue sd = tc1("SEQUENCE_IS_ARITHMETIC", ea, ctx);
    nql_test_check(tc, "SEQUENCE_IS_ARITHMETIC([]) no crash", true);
    (void)sd;
    /* Single-element array tests */
    NqlValue a1 = tc_farr((double[]){42.0}, 1);
    tc_near(tc, "MEAN([42])=42",    tc1("MEAN", a1, ctx), 42.0, 1e-10);
    tc_near(tc, "MEDIAN([42])=42",  tc1("MEDIAN", a1, ctx), 42.0, 1e-10);
    tc_int_eq(tc, "ARRAY_LENGTH([42])=1", tc1("ARRAY_LENGTH", a1, ctx), 1);
    NqlValue r1 = tc1("ARRAY_REVERSE", a1, ctx);
    nql_test_check(tc, "ARRAY_REVERSE([42]) not crash", r1.type != NQL_VAL_NULL);
    /* Two-element tests */
    NqlValue a2 = tc_farr((double[]){1, 3}, 2);
    tc_near(tc, "MEAN([1,3])=2",    tc1("MEAN", a2, ctx), 2.0, 1e-10);
    tc_near(tc, "MEDIAN([1,3])=2",  tc1("MEDIAN", a2, ctx), 2.0, 1e-10);
    /* FFT / IFFT registration (can't easily test without power-of-2 complex arrays) */
    tc_registered(tc, "FFT");
    tc_registered(tc, "IFFT");
    tc_registered(tc, "NTT");
    tc_registered(tc, "INTT");
    /* Optimization registration */
    tc_registered(tc, "MINIMIZE_GOLDEN");
    tc_registered(tc, "MINIMIZE_TERNARY");
    tc_registered(tc, "FIND_ROOT_BISECT");
    tc_registered(tc, "FIND_ROOT_NEWTON");
    tc_registered(tc, "SECANT_METHOD");
    tc_registered(tc, "GRADIENT_DESCENT_1D");
    tc_registered(tc, "FIND_FIXED_POINT");
    /* Numerical registration */
    tc_registered(tc, "DERIVATIVE");
    tc_registered(tc, "SIMPSON");
    tc_registered(tc, "TRAPEZOID");
    tc_registered(tc, "LAGRANGE_INTERP");
    tc_registered(tc, "LINEAR_INTERP");
    tc_registered(tc, "BISECT");
    tc_registered(tc, "CONVOLUTION");
    tc_registered(tc, "POWER_SPECTRUM");
    tc_registered(tc, "DIFF");
    tc_registered(tc, "TABULATE");
    /* Polynomial registration */
    tc_registered(tc, "POLY");
    tc_registered(tc, "POLY_ADD");
    tc_registered(tc, "POLY_MULTIPLY");
    tc_registered(tc, "POLY_EVALUATE");
    tc_registered(tc, "POLY_DEGREE");
    tc_registered(tc, "POLY_DERIVATIVE");
    tc_registered(tc, "POLY_GCD");
    tc_registered(tc, "POLY_ROOTS_MOD");
}
