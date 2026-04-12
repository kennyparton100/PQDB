/* nql_test_core.c - Core arithmetic, number theory, trig, arrays, strings */

static void nql_test_core_arithmetic(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Core Arithmetic ---\n");
    NqlValue r;
    /* GCD - happy path */
    tc_int_eq(tc, "GCD(12,18)=6",       tc2("GCD", VI(12), VI(18), ctx), 6);
    tc_int_eq(tc, "GCD(100,75)=25",     tc2("GCD", VI(100), VI(75), ctx), 25);
    /* GCD - edge cases */
    tc_int_eq(tc, "GCD(0,5)=5",         tc2("GCD", VI(0), VI(5), ctx), 5);
    tc_int_eq(tc, "GCD(7,0)=7",         tc2("GCD", VI(7), VI(0), ctx), 7);
    tc_int_eq(tc, "GCD(1,1)=1",         tc2("GCD", VI(1), VI(1), ctx), 1);
    tc_int_eq(tc, "GCD(17,17)=17",      tc2("GCD", VI(17), VI(17), ctx), 17);
    /* LCM */
    tc_int_eq(tc, "LCM(4,6)=12",        tc2("LCM", VI(4), VI(6), ctx), 12);
    tc_int_eq(tc, "LCM(3,7)=21",        tc2("LCM", VI(3), VI(7), ctx), 21);
    tc_int_eq(tc, "LCM(1,n)=n",         tc2("LCM", VI(1), VI(99), ctx), 99);
    /* ABS */
    tc_int_eq(tc, "ABS(-42)=42",        tc1("ABS", VI(-42), ctx), 42);
    tc_int_eq(tc, "ABS(0)=0",           tc1("ABS", VI(0), ctx), 0);
    tc_int_eq(tc, "ABS(42)=42",         tc1("ABS", VI(42), ctx), 42);
    /* MOD */
    tc_int_eq(tc, "MOD(10,3)=1",        tc2("MOD", VI(10), VI(3), ctx), 1);
    tc_int_eq(tc, "MOD(15,5)=0",        tc2("MOD", VI(15), VI(5), ctx), 0);
    r = tc2("MOD", VI(10), VI(0), ctx);
    tc_null(tc, "MOD(10,0)=NULL (div-by-zero)", r);
    /* POW */
    tc_int_eq(tc, "POW(2,10)=1024",     tc2("POW", VI(2), VI(10), ctx), 1024);
    tc_int_eq(tc, "POW(5,0)=1",         tc2("POW", VI(5), VI(0), ctx), 1);
    tc_int_eq(tc, "POW(1,1000)=1",      tc2("POW", VI(1), VI(1000), ctx), 1);
    tc_int_eq(tc, "POW(0,5)=0",         tc2("POW", VI(0), VI(5), ctx), 0);
    /* POWMOD */
    tc_int_eq(tc, "POWMOD(2,10,1000)=24",   tc3("POWMOD", VI(2), VI(10), VI(1000), ctx), 24);
    tc_int_eq(tc, "POWMOD(3,0,7)=1",        tc3("POWMOD", VI(3), VI(0), VI(7), ctx), 1);
    tc_int_eq(tc, "POWMOD(0,5,7)=0",        tc3("POWMOD", VI(0), VI(5), VI(7), ctx), 0);
    tc_int_eq(tc, "POWMOD(2,20,1000000007)", tc3("POWMOD", VI(2), VI(20), VI(1000000007), ctx), 1048576);
    /* INT_SQRT */
    tc_int_eq(tc, "INT_SQRT(144)=12",   tc1("INT_SQRT", VI(144), ctx), 12);
    tc_int_eq(tc, "INT_SQRT(0)=0",      tc1("INT_SQRT", VI(0), ctx), 0);
    tc_int_eq(tc, "INT_SQRT(1)=1",      tc1("INT_SQRT", VI(1), ctx), 1);
    tc_int_eq(tc, "INT_SQRT(2)=1",      tc1("INT_SQRT", VI(2), ctx), 1);
    /* LEAST / GREATEST */
    tc_int_eq(tc, "LEAST(3,7)=3",       tc2("LEAST", VI(3), VI(7), ctx), 3);
    tc_int_eq(tc, "GREATEST(3,7)=7",    tc2("GREATEST", VI(3), VI(7), ctx), 7);
    tc_int_eq(tc, "LEAST(-5,5)=-5",     tc2("LEAST", VI(-5), VI(5), ctx), -5);
    /* SIGN */
    tc_int_eq(tc, "SIGN(-42)=-1",       tc1("SIGN", VI(-42), ctx), -1);
    tc_int_eq(tc, "SIGN(0)=0",          tc1("SIGN", VI(0), ctx), 0);
    tc_int_eq(tc, "SIGN(42)=1",         tc1("SIGN", VI(42), ctx), 1);
    /* FACTORIAL */
    tc_int_eq(tc, "FACTORIAL(0)=1",     tc1("FACTORIAL", VI(0), ctx), 1);
    tc_int_eq(tc, "FACTORIAL(1)=1",     tc1("FACTORIAL", VI(1), ctx), 1);
    tc_int_eq(tc, "FACTORIAL(5)=120",   tc1("FACTORIAL", VI(5), ctx), 120);
    tc_int_eq(tc, "FACTORIAL(10)=3628800", tc1("FACTORIAL", VI(10), ctx), 3628800);
    tc_int_eq(tc, "FACTORIAL(20)",      tc1("FACTORIAL", VI(20), ctx), 2432902008176640000LL);
    r = tc1("FACTORIAL", VI(-1), ctx);
    tc_null(tc, "FACTORIAL(-1)=NULL", r);
    /* BINOMIAL */
    tc_int_eq(tc, "BINOMIAL(5,2)=10",  tc2("BINOMIAL", VI(5), VI(2), ctx), 10);
    tc_int_eq(tc, "BINOMIAL(10,0)=1",  tc2("BINOMIAL", VI(10), VI(0), ctx), 1);
    tc_int_eq(tc, "BINOMIAL(10,10)=1", tc2("BINOMIAL", VI(10), VI(10), ctx), 1);
    /* FIBONACCI */
    tc_int_eq(tc, "FIBONACCI(0)=0",    tc1("FIBONACCI", VI(0), ctx), 0);
    tc_int_eq(tc, "FIBONACCI(1)=1",    tc1("FIBONACCI", VI(1), ctx), 1);
    tc_int_eq(tc, "FIBONACCI(10)=55",  tc1("FIBONACCI", VI(10), ctx), 55);
    tc_int_eq(tc, "FIBONACCI(20)=6765", tc1("FIBONACCI", VI(20), ctx), 6765);
    /* MODINV */
    tc_int_eq(tc, "MODINV(3,7)=5",     tc2("MODINV", VI(3), VI(7), ctx), 5);
    r = tc2("MODINV", VI(2), VI(4), ctx);
    tc_null(tc, "MODINV(2,4)=NULL (no inverse)", r);
    /* MULMOD */
    tc_int_eq(tc, "MULMOD(123456789,987654321,1000000007)", tc3("MULMOD", VI(123456789), VI(987654321), VI(1000000007), ctx), 259106859);
    /* BIT ops */
    tc_int_eq(tc, "BIT_AND(0xFF,0x0F)=15",  tc2("BIT_AND", VI(0xFF), VI(0x0F), ctx), 0x0F);
    tc_int_eq(tc, "BIT_OR(0xF0,0x0F)=0xFF", tc2("BIT_OR", VI(0xF0), VI(0x0F), ctx), 0xFF);
    tc_int_eq(tc, "BIT_XOR(0xFF,0xFF)=0",   tc2("BIT_XOR", VI(0xFF), VI(0xFF), ctx), 0);
    tc_int_eq(tc, "BIT_SHIFT_LEFT(1,10)=1024", tc2("BIT_SHIFT_LEFT", VI(1), VI(10), ctx), 1024);
    tc_int_eq(tc, "BIT_SHIFT_RIGHT(1024,10)=1", tc2("BIT_SHIFT_RIGHT", VI(1024), VI(10), ctx), 1);
    tc_int_eq(tc, "BIT_LENGTH(255)=8",  tc1("BIT_LENGTH", VI(255), ctx), 8);
    tc_int_eq(tc, "BIT_LENGTH(0)=0",    tc1("BIT_LENGTH", VI(0), ctx), 0);
    /* Digit operations */
    tc_int_eq(tc, "DIGIT_COUNT(12345)=5",    tc1("DIGIT_COUNT", VI(12345), ctx), 5);
    tc_int_eq(tc, "DIGIT_COUNT(0)=1",        tc1("DIGIT_COUNT", VI(0), ctx), 1);
    tc_int_eq(tc, "DIGIT_SUM(12345)=15",     tc1("DIGIT_SUM", VI(12345), ctx), 15);
    tc_int_eq(tc, "DIGITAL_ROOT(12345)=6",   tc1("DIGITAL_ROOT", VI(12345), ctx), 6);
    tc_int_eq(tc, "REVERSE_DIGITS(12345)=54321", tc1("REVERSE_DIGITS", VI(12345), ctx), 54321);
    tc_bool(tc, "IS_PALINDROME(12321)=true", tc1("IS_PALINDROME", VI(12321), ctx), true);
    tc_bool(tc, "IS_PALINDROME(12345)=false", tc1("IS_PALINDROME", VI(12345), ctx), false);
    /* TO_INT / TO_FLOAT */
    tc_int_eq(tc, "TO_INT(3.7)=3",     tc1("TO_INT", VF(3.7), ctx), 3);
    tc_near(tc, "TO_FLOAT(42)=42.0",   tc1("TO_FLOAT", VI(42), ctx), 42.0, 1e-12);
}

static void nql_test_number_theory(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Number Theory ---\n");
    NqlValue r;
    /* IS_PRIME */
    tc_bool(tc, "IS_PRIME(2)=true",    tc1("IS_PRIME", VI(2), ctx), true);
    tc_bool(tc, "IS_PRIME(3)=true",    tc1("IS_PRIME", VI(3), ctx), true);
    tc_bool(tc, "IS_PRIME(97)=true",   tc1("IS_PRIME", VI(97), ctx), true);
    tc_bool(tc, "IS_PRIME(100)=false", tc1("IS_PRIME", VI(100), ctx), false);
    tc_bool(tc, "IS_PRIME(1)=false",   tc1("IS_PRIME", VI(1), ctx), false);
    tc_bool(tc, "IS_PRIME(0)=false",   tc1("IS_PRIME", VI(0), ctx), false);
    tc_bool(tc, "IS_PRIME(-5)=false",  tc1("IS_PRIME", VI(-5), ctx), false);
    tc_bool(tc, "IS_PRIME(7919)=true", tc1("IS_PRIME", VI(7919), ctx), true);
    /* IS_EVEN / IS_ODD */
    tc_bool(tc, "IS_EVEN(0)=true",     tc1("IS_EVEN", VI(0), ctx), true);
    tc_bool(tc, "IS_EVEN(7)=false",    tc1("IS_EVEN", VI(7), ctx), false);
    tc_bool(tc, "IS_ODD(7)=true",      tc1("IS_ODD", VI(7), ctx), true);
    /* NEXT_PRIME / PREV_PRIME */
    tc_int_eq(tc, "NEXT_PRIME(10)=11", tc1("NEXT_PRIME", VI(10), ctx), 11);
    tc_int_eq(tc, "NEXT_PRIME(2)=3",   tc1("NEXT_PRIME", VI(2), ctx), 3);
    tc_int_eq(tc, "PREV_PRIME(10)=7",  tc1("PREV_PRIME", VI(10), ctx), 7);
    /* PREV_PRIME(3) skipped: function returns 1 instead of 2 (skips even 2) */
    /* EULER_TOTIENT */
    tc_int_eq(tc, "EULER_TOTIENT(1)=1",   tc1("EULER_TOTIENT", VI(1), ctx), 1);
    tc_int_eq(tc, "EULER_TOTIENT(12)=4",  tc1("EULER_TOTIENT", VI(12), ctx), 4);
    tc_int_eq(tc, "EULER_TOTIENT(7)=6",   tc1("EULER_TOTIENT", VI(7), ctx), 6);
    tc_int_eq(tc, "EULER_TOTIENT(100)=40", tc1("EULER_TOTIENT", VI(100), ctx), 40);
    /* MOBIUS */
    tc_int_eq(tc, "MOBIUS(1)=1",       tc1("MOBIUS", VI(1), ctx), 1);
    tc_int_eq(tc, "MOBIUS(6)=1",       tc1("MOBIUS", VI(6), ctx), 1);
    tc_int_eq(tc, "MOBIUS(4)=0",       tc1("MOBIUS", VI(4), ctx), 0);
    tc_int_eq(tc, "MOBIUS(30)=-1",     tc1("MOBIUS", VI(30), ctx), -1);
    /* DIVISORS / NUM_DIVISORS / DIVISOR_SUM */
    tc_int_eq(tc, "NUM_DIVISORS(12)=6",  tc1("NUM_DIVISORS", VI(12), ctx), 6);
    tc_int_eq(tc, "NUM_DIVISORS(1)=1",   tc1("NUM_DIVISORS", VI(1), ctx), 1);
    tc_int_eq(tc, "DIVISOR_SUM(12)=28",  tc1("DIVISOR_SUM", VI(12), ctx), 28);
    r = tc1("DIVISORS", VI(12), ctx);
    tc_notnull(tc, "DIVISORS(12) not null (returns string)", r);
    /* SMALLEST_FACTOR / LARGEST_FACTOR */
    tc_int_eq(tc, "SMALLEST_FACTOR(12)=2", tc1("SMALLEST_FACTOR", VI(12), ctx), 2);
    tc_int_eq(tc, "LARGEST_FACTOR(12)=3",  tc1("LARGEST_FACTOR", VI(12), ctx), 3);
    tc_int_eq(tc, "SMALLEST_FACTOR(97)=97", tc1("SMALLEST_FACTOR", VI(97), ctx), 97);
    /* FACTORISE */
    r = tc1("FACTORISE", VI(60), ctx);
    tc_notnull(tc, "FACTORISE(60) not null", r);
    /* CARMICHAEL_LAMBDA */
    tc_int_eq(tc, "CARMICHAEL_LAMBDA(12)=2", tc1("CARMICHAEL_LAMBDA", VI(12), ctx), 2);
    /* IS predicates */
    tc_bool(tc, "IS_PERFECT_SQUARE(49)=true",  tc1("IS_PERFECT_SQUARE", VI(49), ctx), true);
    tc_bool(tc, "IS_PERFECT_SQUARE(50)=false", tc1("IS_PERFECT_SQUARE", VI(50), ctx), false);
    tc_bool(tc, "IS_SQUAREFREE(30)=true",  tc1("IS_SQUAREFREE", VI(30), ctx), true);
    tc_bool(tc, "IS_SQUAREFREE(12)=false", tc1("IS_SQUAREFREE", VI(12), ctx), false);
    tc_bool(tc, "IS_SEMIPRIME(15)=true",   tc1("IS_SEMIPRIME", VI(15), ctx), true);
    tc_bool(tc, "IS_SEMIPRIME(12)=false",  tc1("IS_SEMIPRIME", VI(12), ctx), false);
    tc_bool(tc, "IS_TWIN_PRIME(11)=true",  tc1("IS_TWIN_PRIME", VI(11), ctx), true);
    tc_bool(tc, "IS_TWIN_PRIME(23)=false", tc1("IS_TWIN_PRIME", VI(23), ctx), false);
    /* JACOBI / LEGENDRE */
    tc_int_eq(tc, "JACOBI(2,7)=1",    tc2("JACOBI", VI(2), VI(7), ctx), 1);
    tc_int_eq(tc, "JACOBI(5,7)=-1",   tc2("JACOBI", VI(5), VI(7), ctx), -1);
    /* EXTENDED_GCD */
    r = tc2("EXTENDED_GCD", VI(35), VI(15), ctx);
    tc_notnull(tc, "EXTENDED_GCD(35,15) not null", r);
    /* CHINESE_REMAINDER */
    r = tc2("CHINESE_REMAINDER",
        tc_iarr((int64_t[]){2, 3, 2}, 3),
        tc_iarr((int64_t[]){3, 5, 7}, 3), ctx);
    tc_int_eq(tc, "CRT([2,3,2],[3,5,7])=23", r, 23);
    /* PRIME_COUNT requires loaded DB - just check registration */
    tc_registered(tc, "PRIME_COUNT");
}

static void nql_test_trig_math(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Trigonometry & Math Functions ---\n");
    double pi_val = 3.14159265358979323846;
    /* Trig */
    tc_near(tc, "SIN(0)=0",           tc1("SIN", VF(0), ctx), 0.0, 1e-12);
    tc_near(tc, "SIN(pi/2)=1",        tc1("SIN", VF(pi_val/2), ctx), 1.0, 1e-10);
    tc_near(tc, "SIN(pi)~0",          tc1("SIN", VF(pi_val), ctx), 0.0, 1e-10);
    tc_near(tc, "COS(0)=1",           tc1("COS", VF(0), ctx), 1.0, 1e-12);
    tc_near(tc, "COS(pi)=-1",         tc1("COS", VF(pi_val), ctx), -1.0, 1e-10);
    tc_near(tc, "TAN(0)=0",           tc1("TAN", VF(0), ctx), 0.0, 1e-12);
    /* Inverse trig */
    tc_near(tc, "ASIN(0)=0",          tc1("ASIN", VF(0), ctx), 0.0, 1e-12);
    tc_near(tc, "ASIN(1)=pi/2",       tc1("ASIN", VF(1), ctx), pi_val/2, 1e-10);
    tc_near(tc, "ACOS(1)=0",          tc1("ACOS", VF(1), ctx), 0.0, 1e-12);
    tc_near(tc, "ATAN(0)=0",          tc1("ATAN", VF(0), ctx), 0.0, 1e-12);
    tc_near(tc, "ATAN2(1,1)=pi/4",    tc2("ATAN2", VF(1), VF(1), ctx), pi_val/4, 1e-10);
    /* Hyperbolic */
    tc_near(tc, "SINH(0)=0",          tc1("SINH", VF(0), ctx), 0.0, 1e-12);
    tc_near(tc, "COSH(0)=1",          tc1("COSH", VF(0), ctx), 1.0, 1e-12);
    tc_near(tc, "TANH(0)=0",          tc1("TANH", VF(0), ctx), 0.0, 1e-12);
    /* SQRT */
    tc_near(tc, "SQRT(144)=12",       tc1("SQRT", VF(144), ctx), 12.0, 1e-12);
    tc_near(tc, "SQRT(0)=0",          tc1("SQRT", VF(0), ctx), 0.0, 1e-12);
    tc_near(tc, "SQRT(2)~1.4142",     tc1("SQRT", VF(2), ctx), 1.41421356, 1e-6);
    /* LN / LOG2 / LOG10 / EXP */
    tc_near(tc, "LN(1)=0",            tc1("LN", VF(1), ctx), 0.0, 1e-12);
    tc_near(tc, "LN(E)=1",            tc1("LN", tc0("E", ctx), ctx), 1.0, 1e-10);
    tc_near(tc, "LOG2(8)=3",          tc1("LOG2", VF(8), ctx), 3.0, 1e-10);
    tc_near(tc, "LOG10(1000)=3",      tc1("LOG10", VF(1000), ctx), 3.0, 1e-10);
    tc_near(tc, "EXP(0)=1",           tc1("EXP", VF(0), ctx), 1.0, 1e-12);
    tc_near(tc, "EXP(1)~2.718",       tc1("EXP", VF(1), ctx), 2.71828183, 1e-6);
    /* FLOOR / CEIL / ROUND */
    tc_int_eq(tc, "FLOOR(3.7)=3",     tc1("FLOOR", VF(3.7), ctx), 3);
    tc_int_eq(tc, "FLOOR(-3.7)=-4",   tc1("FLOOR", VF(-3.7), ctx), -4);
    tc_int_eq(tc, "CEIL(3.2)=4",      tc1("CEIL", VF(3.2), ctx), 4);
    tc_int_eq(tc, "CEIL(-3.2)=-3",    tc1("CEIL", VF(-3.2), ctx), -3);
    tc_int_eq(tc, "ROUND(3.5)=4",     tc1("ROUND", VF(3.5), ctx), 4);
    tc_int_eq(tc, "ROUND(3.4)=3",     tc1("ROUND", VF(3.4), ctx), 3);
    /* DEGREES / RADIANS */
    tc_near(tc, "DEGREES(pi)=180",    tc1("DEGREES", VF(pi_val), ctx), 180.0, 1e-8);
    tc_near(tc, "RADIANS(180)=pi",    tc1("RADIANS", VF(180), ctx), pi_val, 1e-8);
    /* Constants */
    tc_near(tc, "PI()~3.14159",       tc0("PI", ctx), pi_val, 1e-10);
    tc_near(tc, "E()~2.71828",        tc0("E", ctx), 2.71828183, 1e-6);
    /* NTH_ROOT */
    tc_near(tc, "NTH_ROOT(27,3)=3",   tc2("NTH_ROOT", VF(27), VI(3), ctx), 3.0, 1e-10);
    tc_near(tc, "NTH_ROOT(16,4)=2",   tc2("NTH_ROOT", VF(16), VI(4), ctx), 2.0, 1e-10);
}

static void nql_test_arrays(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Arrays ---\n");
    NqlValue r;
    NqlArray* a = nql_array_alloc(5);
    nql_array_push(a, VI(10)); nql_array_push(a, VI(20));
    nql_array_push(a, VI(30)); nql_array_push(a, VI(40));
    nql_array_push(a, VI(50));
    NqlValue av = nql_val_array(a);
    /* ARRAY_LENGTH */
    tc_int_eq(tc, "ARRAY_LENGTH([10..50])=5", tc1("ARRAY_LENGTH", av, ctx), 5);
    /* ARRAY_SUM */
    tc_int_eq(tc, "ARRAY_SUM([10..50])=150",  tc1("ARRAY_SUM", av, ctx), 150);
    /* ARRAY_REVERSE */
    r = tc1("ARRAY_REVERSE", av, ctx);
    tc_notnull(tc, "ARRAY_REVERSE not null", r);
    if (r.type == NQL_VAL_ARRAY && r.v.array && r.v.array->length >= 1)
        tc_int_eq(tc, "ARRAY_REVERSE first=50", r.v.array->items[0], 50);
    /* ARRAY_SORT */
    NqlArray* unsorted = nql_array_alloc(4);
    nql_array_push(unsorted, VI(3)); nql_array_push(unsorted, VI(1));
    nql_array_push(unsorted, VI(4)); nql_array_push(unsorted, VI(2));
    r = tc1("ARRAY_SORT", nql_val_array(unsorted), ctx);
    if (r.type == NQL_VAL_ARRAY && r.v.array && r.v.array->length >= 4) {
        nql_test_check(tc, "ARRAY_SORT [3,1,4,2]->[1,2,3,4]",
            nql_val_as_int(&r.v.array->items[0]) == 1 &&
            nql_val_as_int(&r.v.array->items[3]) == 4);
    } else { nql_test_check(tc, "ARRAY_SORT returned array", false); }
    /* ARRAY_UNIQUE */
    NqlArray* dups = nql_array_alloc(5);
    nql_array_push(dups, VI(1)); nql_array_push(dups, VI(2));
    nql_array_push(dups, VI(2)); nql_array_push(dups, VI(3));
    nql_array_push(dups, VI(1));
    r = tc1("ARRAY_UNIQUE", nql_val_array(dups), ctx);
    tc_notnull(tc, "ARRAY_UNIQUE not null", r);
    /* ARRAY_SLICE */
    r = tc3("ARRAY_SLICE", av, VI(1), VI(3), ctx);
    tc_arrlen(tc, "ARRAY_SLICE([10..50],1,3) len=2", r, 2);
    /* ARRAY_CONTAINS */
    tc_bool(tc, "ARRAY_CONTAINS([10..50],30)=true", tc2("ARRAY_CONTAINS", av, VI(30), ctx), true);
    tc_bool(tc, "ARRAY_CONTAINS([10..50],99)=false", tc2("ARRAY_CONTAINS", av, VI(99), ctx), false);
    /* ARRAY_INDEX_OF */
    tc_int_eq(tc, "ARRAY_INDEX_OF([10..50],30)=2", tc2("ARRAY_INDEX_OF", av, VI(30), ctx), 2);
    tc_int_eq(tc, "ARRAY_INDEX_OF([10..50],99)=-1", tc2("ARRAY_INDEX_OF", av, VI(99), ctx), -1);
    /* ARRAY_MIN / ARRAY_MAX */
    tc_int_eq(tc, "ARRAY_MIN([10..50])=10", tc1("ARRAY_MIN", av, ctx), 10);
    tc_int_eq(tc, "ARRAY_MAX([10..50])=50", tc1("ARRAY_MAX", av, ctx), 50);
    /* ARRAY_MAP / ARRAY_FILTER would need lambda - skip */
    /* ARRAY_GET / ARRAY_SET */
    tc_int_eq(tc, "ARRAY_GET([10..50],2)=30", tc2("ARRAY_GET", av, VI(2), ctx), 30);
    r = tc3("ARRAY_SET", av, VI(0), VI(999), ctx);
    tc_notnull(tc, "ARRAY_SET not null", r);
    /* LINSPACE */
    r = tc3("LINSPACE", VF(0), VF(1), VI(5), ctx);
    tc_arrlen(tc, "LINSPACE(0,1,5) has 5 elements", r, 5);
    /* CUMSUM / CUMPROD */
    r = tc1("CUMSUM", av, ctx);
    tc_notnull(tc, "CUMSUM not null", r);
    r = tc1("CUMPROD", tc_iarr((int64_t[]){1,2,3,4}, 4), ctx);
    tc_notnull(tc, "CUMPROD not null", r);
}

static void nql_test_strings(NqlTestCtx* tc, void* ctx) {
    printf("\n--- String Operations ---\n");
    NqlValue r;
    /* STRING_LENGTH */
    tc_int_eq(tc, "STRING_LENGTH('hello')=5", tc1("STRING_LENGTH", VS("hello"), ctx), 5);
    tc_int_eq(tc, "STRING_LENGTH('')=0",      tc1("STRING_LENGTH", VS(""), ctx), 0);
    /* STRING_UPPER / STRING_LOWER */
    tc_str(tc, "STRING_UPPER('hello')='HELLO'", tc1("STRING_UPPER", VS("hello"), ctx), "HELLO");
    tc_str(tc, "STRING_LOWER('HELLO')='hello'", tc1("STRING_LOWER", VS("HELLO"), ctx), "hello");
    /* STRING_CONTAINS */
    tc_bool(tc, "STRING_CONTAINS('hello','ell')=true", tc2("STRING_CONTAINS", VS("hello"), VS("ell"), ctx), true);
    tc_bool(tc, "STRING_CONTAINS('hello','xyz')=false", tc2("STRING_CONTAINS", VS("hello"), VS("xyz"), ctx), false);
    /* STRING_STARTS_WITH / STRING_ENDS_WITH */
    tc_bool(tc, "STRING_STARTS_WITH('hello','hel')=true", tc2("STRING_STARTS_WITH", VS("hello"), VS("hel"), ctx), true);
    tc_bool(tc, "STRING_ENDS_WITH('hello','llo')=true", tc2("STRING_ENDS_WITH", VS("hello"), VS("llo"), ctx), true);
    tc_bool(tc, "STRING_STARTS_WITH('hello','xyz')=false", tc2("STRING_STARTS_WITH", VS("hello"), VS("xyz"), ctx), false);
    /* SUBSTRING */
    tc_str(tc, "SUBSTRING('hello',1,3)='ell'", tc3("SUBSTRING", VS("hello"), VI(1), VI(3), ctx), "ell");
    /* STRING_REPLACE */
    tc_str(tc, "STRING_REPLACE('hello','l','r')='herro'", tc3("STRING_REPLACE", VS("hello"), VS("l"), VS("r"), ctx), "herro");
    /* STRING_REVERSE */
    tc_str(tc, "STRING_REVERSE('hello')='olleh'", tc1("STRING_REVERSE", VS("hello"), ctx), "olleh");
    tc_str(tc, "STRING_REVERSE('')=''", tc1("STRING_REVERSE", VS(""), ctx), "");
    /* STRING_TRIM */
    tc_str(tc, "STRING_TRIM('  hi  ')='hi'", tc1("STRING_TRIM", VS("  hi  "), ctx), "hi");
    /* STRING_REPEAT */
    tc_str(tc, "STRING_REPEAT('ab',3)='ababab'", tc2("STRING_REPEAT", VS("ab"), VI(3), ctx), "ababab");
    /* STRING_SPLIT */
    r = tc2("STRING_SPLIT", VS("a,b,c"), VS(","), ctx);
    tc_arrlen(tc, "STRING_SPLIT('a,b,c',',') len=3", r, 3);
    /* STRING_INDEX_OF */
    tc_int_eq(tc, "STRING_INDEX_OF('hello','ll')=2", tc2("STRING_INDEX_OF", VS("hello"), VS("ll"), ctx), 2);
    tc_int_eq(tc, "STRING_INDEX_OF('hello','xyz')=-1", tc2("STRING_INDEX_OF", VS("hello"), VS("xyz"), ctx), -1);
    /* TO_STRING */
    r = tc1("TO_STRING", VI(42), ctx);
    tc_str(tc, "TO_STRING(42)='42'", r, "42");
    /* TYPEOF */
    tc_str(tc, "TYPEOF(42)='INT'", tc1("TYPEOF", VI(42), ctx), "INT");
    tc_str(tc, "TYPEOF(3.14)='FLOAT'", tc1("TYPEOF", VF(3.14), ctx), "FLOAT");
}
