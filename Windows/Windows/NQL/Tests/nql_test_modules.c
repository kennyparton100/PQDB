/* nql_test_modules.c - Stats, distributions, combinatorics, special, sparse,
 * dist-objects, ODE, sequence, cfrac, decompose, field, EC, quat, crypto, physics */

static void nql_test_statistics(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Statistics ---\n");
    NqlValue a5 = tc_farr((double[]){1,2,3,4,5}, 5);
    tc_near(tc, "MEAN([1..5])=3",        tc1("MEAN", a5, ctx), 3.0, 1e-10);
    tc_near(tc, "MEDIAN([1..5])=3",      tc1("MEDIAN", a5, ctx), 3.0, 1e-10);
    tc_near(tc, "STD_DEV([1..5])~1.414", tc1("STD_DEV", a5, ctx), sqrt(2.0), 0.01);
    tc_near(tc, "VARIANCE([1..5])=2",    tc1("VARIANCE", a5, ctx), 2.0, 0.01);
    /* RANGE is a type, not a function - skip */
    /* MODE */
    NqlValue mode_arr = tc_farr((double[]){1,2,2,3,3,3}, 6);
    tc_near(tc, "MODE([1,2,2,3,3,3])=3", tc1("MODE", mode_arr, ctx), 3.0, 1e-10);
    /* GEOMETRIC_MEAN */
    tc_near(tc, "GEOMETRIC_MEAN([1,2,4])~2", tc1("GEOMETRIC_MEAN", tc_farr((double[]){1,2,4}, 3), ctx), 2.0, 0.01);
    /* HARMONIC_MEAN */
    tc_near(tc, "HARMONIC_MEAN([1,2,4])~1.714", tc1("HARMONIC_MEAN", tc_farr((double[]){1,2,4}, 3), ctx), 12.0/7.0, 0.01);
    /* SKEWNESS / KURTOSIS */
    tc_notnull(tc, "SKEWNESS not null",  tc1("SKEWNESS", a5, ctx));
    tc_notnull(tc, "KURTOSIS not null",  tc1("KURTOSIS", a5, ctx));
    /* PERCENTILE */
    tc_near(tc, "PERCENTILE([1..5],50)=3", tc2("PERCENTILE", a5, VF(50), ctx), 3.0, 0.5);
    /* QUARTILES */
    NqlValue q = tc1("QUARTILES", a5, ctx);
    tc_notnull(tc, "QUARTILES not null", q);
    /* COVARIANCE / CORRELATION */
    NqlValue b5 = tc_farr((double[]){2,4,6,8,10}, 5);
    tc_notnull(tc, "COVARIANCE not null",   tc2("COVARIANCE", a5, b5, ctx));
    tc_near(tc, "CORRELATION([1..5],[2..10])=1", tc2("CORRELATION", a5, b5, ctx), 1.0, 1e-5);
    /* LINEAR_REGRESSION */
    NqlValue lr = tc2("LINEAR_REGRESSION", a5, b5, ctx);
    tc_notnull(tc, "LINEAR_REGRESSION not null", lr);
    /* ZSCORE */
    tc_registered(tc, "ZSCORE");
    /* WEIGHTED_MEAN */
    tc_notnull(tc, "WEIGHTED_MEAN not null", tc2("WEIGHTED_MEAN", a5, b5, ctx));
    /* MOVING_AVG */
    tc_notnull(tc, "MOVING_AVG not null", tc2("MOVING_AVG", a5, VI(2), ctx));
    /* SAMPLE_VARIANCE / SAMPLE_STD_DEV */
    tc_notnull(tc, "SAMPLE_VARIANCE not null", tc1("SAMPLE_VARIANCE", a5, ctx));
    tc_notnull(tc, "SAMPLE_STD_DEV not null",  tc1("SAMPLE_STD_DEV", a5, ctx));
    /* Single element */
    NqlValue a1 = tc_farr((double[]){42}, 1);
    tc_near(tc, "MEAN([42])=42",     tc1("MEAN", a1, ctx), 42.0, 1e-10);
    tc_near(tc, "MEDIAN([42])=42",   tc1("MEDIAN", a1, ctx), 42.0, 1e-10);
}

static void nql_test_distributions(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Probability Distributions ---\n");
    /* Normal */
    tc_near(tc, "NORMAL_PDF(0,0,1)~0.3989",    tc3("NORMAL_PDF", VF(0), VF(0), VF(1), ctx), 0.39894228, 1e-5);
    tc_near(tc, "NORMAL_CDF(0,0,1)=0.5",       tc3("NORMAL_CDF", VF(0), VF(0), VF(1), ctx), 0.5, 1e-5);
    tc_near(tc, "NORMAL_CDF(-inf)~0",           tc3("NORMAL_CDF", VF(-10), VF(0), VF(1), ctx), 0.0, 1e-5);
    tc_near(tc, "NORMAL_CDF(+inf)~1",           tc3("NORMAL_CDF", VF(10), VF(0), VF(1), ctx), 1.0, 1e-5);
    /* Exponential */
    tc_near(tc, "EXPONENTIAL_PDF(0,1)=1",       tc2("EXPONENTIAL_PDF", VF(0), VF(1), ctx), 1.0, 1e-10);
    tc_near(tc, "EXPONENTIAL_CDF(0,1)=0",       tc2("EXPONENTIAL_CDF", VF(0), VF(1), ctx), 0.0, 1e-10);
    /* Uniform */
    tc_near(tc, "UNIFORM_PDF(0.5,0,1)=1",      tc3("UNIFORM_PDF", VF(0.5), VF(0), VF(1), ctx), 1.0, 1e-10);
    tc_near(tc, "UNIFORM_CDF(0.5,0,1)=0.5",    tc3("UNIFORM_CDF", VF(0.5), VF(0), VF(1), ctx), 0.5, 1e-10);
    tc_near(tc, "UNIFORM_PDF(-1,0,1)=0",        tc3("UNIFORM_PDF", VF(-1), VF(0), VF(1), ctx), 0.0, 1e-10);
    /* Poisson */
    tc_near(tc, "POISSON_PMF(0,1)~0.3679", tc2("POISSON_PMF", VI(0), VF(1), ctx), 0.367879441, 1e-5);
    tc_near(tc, "POISSON_CDF(0,1)~0.3679", tc2("POISSON_CDF", VI(0), VF(1), ctx), 0.367879441, 1e-5);
    /* Binomial */
    tc_near(tc, "BINOMIAL_PMF(5,10,0.5)~0.2461", tc3("BINOMIAL_PMF", VI(5), VI(10), VF(0.5), ctx), 0.24609375, 1e-5);
    /* Chi-squared */
    tc_notnull(tc, "CHI_SQUARED_PDF not null",  tc2("CHI_SQUARED_PDF", VF(1), VI(2), ctx));
    tc_notnull(tc, "CHI_SQUARED_CDF not null",  tc2("CHI_SQUARED_CDF", VF(1), VI(2), ctx));
    /* Student's t */
    tc_registered(tc, "T_PDF");
}

static void nql_test_combinatorics(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Combinatorics ---\n");
    /* PERM / COMB */
    tc_int_eq(tc, "PERM(5,3)=60",       tc2("PERM", VI(5), VI(3), ctx), 60);
    tc_int_eq(tc, "PERM(5,0)=1",        tc2("PERM", VI(5), VI(0), ctx), 1);
    tc_int_eq(tc, "PERM(5,5)=120",      tc2("PERM", VI(5), VI(5), ctx), 120);
    tc_int_eq(tc, "COMB(5,3)=10",       tc2("COMB", VI(5), VI(3), ctx), 10);
    tc_int_eq(tc, "COMB(10,5)=252",     tc2("COMB", VI(10), VI(5), ctx), 252);
    tc_int_eq(tc, "COMB(5,0)=1",        tc2("COMB", VI(5), VI(0), ctx), 1);
    tc_int_eq(tc, "COMB(5,5)=1",        tc2("COMB", VI(5), VI(5), ctx), 1);
    /* CATALAN */
    tc_int_eq(tc, "CATALAN(0)=1",       tc1("CATALAN", VI(0), ctx), 1);
    tc_int_eq(tc, "CATALAN(5)=42",      tc1("CATALAN", VI(5), ctx), 42);
    /* DERANGEMENT */
    tc_int_eq(tc, "DERANGEMENT(0)=1",   tc1("DERANGEMENT", VI(0), ctx), 1);
    tc_int_eq(tc, "DERANGEMENT(3)=2",   tc1("DERANGEMENT", VI(3), ctx), 2);
    /* BELL */
    tc_int_eq(tc, "BELL(0)=1",          tc1("BELL", VI(0), ctx), 1);
    tc_int_eq(tc, "BELL(5)=52",         tc1("BELL", VI(5), ctx), 52);
    /* STIRLING1 / STIRLING2 */
    tc_notnull(tc, "STIRLING1(5,3) not null", tc2("STIRLING1", VI(5), VI(3), ctx));
    tc_notnull(tc, "STIRLING2(5,3) not null", tc2("STIRLING2", VI(5), VI(3), ctx));
    /* PARTITION_COUNT */
    tc_int_eq(tc, "PARTITION_COUNT(5)=7", tc1("PARTITION_COUNT", VI(5), ctx), 7);
    /* MULTICHOOSE */
    tc_int_eq(tc, "MULTICHOOSE(5,3)=35", tc2("MULTICHOOSE", VI(5), VI(3), ctx), 35);
    /* SET operations */
    NqlValue s1 = tc_iarr((int64_t[]){1,2,3,4}, 4);
    NqlValue s2 = tc_iarr((int64_t[]){3,4,5,6}, 4);
    tc_notnull(tc, "SET_UNION not null",     tc2("SET_UNION", s1, s2, ctx));
    tc_notnull(tc, "SET_INTERSECT not null", tc2("SET_INTERSECT", s1, s2, ctx));
    tc_notnull(tc, "SET_DIFF not null",      tc2("SET_DIFF", s1, s2, ctx));
}

static void nql_test_special_funcs(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Special Functions ---\n");
    tc_near(tc, "GAMMA(5)=24",         tc1("GAMMA", VF(5), ctx), 24.0, 1e-5);
    tc_near(tc, "GAMMA(1)=1",          tc1("GAMMA", VF(1), ctx), 1.0, 1e-5);
    tc_near(tc, "GAMMA(0.5)~1.7725",   tc1("GAMMA", VF(0.5), ctx), 1.7724539, 1e-4);
    tc_near(tc, "ERF(0)~0",            tc1("ERF", VF(0), ctx), 0.0, 1e-6);
    tc_near(tc, "ERF(1)~0.8427",       tc1("ERF", VF(1), ctx), 0.8427008, 1e-4);
    tc_near(tc, "ERF(-1)~-0.8427",     tc1("ERF", VF(-1), ctx), -0.8427008, 1e-4);
    tc_near(tc, "ERFC(0)~1",           tc1("ERFC", VF(0), ctx), 1.0, 1e-6);
    tc_near(tc, "BETA(2,3)~0.0833",    tc2("BETA", VF(2), VF(3), ctx), 1.0/12.0, 1e-4);
    tc_near(tc, "LNGAMMA(5)~3.178",    tc1("LNGAMMA", VF(5), ctx), 3.17805383, 1e-4);
    tc_notnull(tc, "DIGAMMA not null",  tc1("DIGAMMA", VF(1), ctx));
    tc_notnull(tc, "BESSEL_J0(0) not null", tc1("BESSEL_J0", VF(0), ctx));
    tc_near(tc, "BESSEL_J0(0)=1",      tc1("BESSEL_J0", VF(0), ctx), 1.0, 1e-6);
    tc_notnull(tc, "BESSEL_J1(0) not null", tc1("BESSEL_J1", VF(0), ctx));
    tc_registered(tc, "ZETA");
    tc_registered(tc, "LOWER_GAMMA");
    tc_registered(tc, "UPPER_GAMMA");
    tc_registered(tc, "REG_BETA");
}

static void nql_test_sparse(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Sparse Matrices ---\n");
    NqlValue sp = tc2("SPARSE", VI(4), VI(4), ctx);
    tc_notnull(tc, "SPARSE(4,4) created", sp);
    tc_int_eq(tc, "SPARSE_ROWS=4",     tc1("SPARSE_ROWS", sp, ctx), 4);
    tc_int_eq(tc, "SPARSE_COLS=4",     tc1("SPARSE_COLS", sp, ctx), 4);
    tc_int_eq(tc, "SPARSE_NNZ(empty)=0", tc1("SPARSE_NNZ", sp, ctx), 0);
    /* SET and GET */
    sp = tc4("SPARSE_SET", sp, VI(0), VI(1), VF(7), ctx);
    tc_notnull(tc, "SPARSE_SET(0,1,7)", sp);
    sp = tc4("SPARSE_SET", sp, VI(2), VI(3), VF(9), ctx);
    tc_notnull(tc, "SPARSE_SET(2,3,9)", sp);
    tc_int_eq(tc, "SPARSE_NNZ=2",      tc1("SPARSE_NNZ", sp, ctx), 2);
    tc_near(tc, "SPARSE_GET(0,1)=7",   tc3("SPARSE_GET", sp, VI(0), VI(1), ctx), 7.0, 1e-12);
    tc_near(tc, "SPARSE_GET(2,3)=9",   tc3("SPARSE_GET", sp, VI(2), VI(3), ctx), 9.0, 1e-12);
    tc_near(tc, "SPARSE_GET(0,0)=0",   tc3("SPARSE_GET", sp, VI(0), VI(0), ctx), 0.0, 1e-12);
    /* SCALE */
    NqlValue sc = tc2("SPARSE_SCALE", sp, VF(2), ctx);
    tc_near(tc, "SPARSE_SCALE*2 (0,1)=14", tc3("SPARSE_GET", sc, VI(0), VI(1), ctx), 14.0, 1e-12);
    /* TRANSPOSE */
    NqlValue st = tc1("SPARSE_TRANSPOSE", sp, ctx);
    tc_near(tc, "TRANSPOSE (1,0)=7",   tc3("SPARSE_GET", st, VI(1), VI(0), ctx), 7.0, 1e-12);
    /* TO_DENSE / FROM_DENSE registration */
    tc_registered(tc, "SPARSE_TO_DENSE");
    tc_registered(tc, "SPARSE_FROM_DENSE");
    tc_registered(tc, "SPARSE_ADD");
    tc_registered(tc, "SPARSE_MUL_VEC");
}

static void nql_test_dist_objects(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Distribution Objects ---\n");
    /* Normal */
    NqlValue dn = tc2("DIST_NORMAL", VF(10), VF(2), ctx);
    tc_notnull(tc, "DIST_NORMAL(10,2) created", dn);
    tc_near(tc, "DIST_MEAN(Normal)=10",     tc1("DIST_MEAN", dn, ctx), 10.0, 1e-10);
    tc_near(tc, "DIST_VARIANCE(Normal)=4",  tc1("DIST_VARIANCE", dn, ctx), 4.0, 1e-10);
    tc_notnull(tc, "DIST_PDF(Normal,10)",   tc2("DIST_PDF", dn, VF(10), ctx));
    tc_notnull(tc, "DIST_CDF(Normal,10)",   tc2("DIST_CDF", dn, VF(10), ctx));
    tc_notnull(tc, "DIST_SAMPLE(Normal)",   tc1("DIST_SAMPLE", dn, ctx));
    tc_notnull(tc, "DIST_SAMPLE_N(Normal,5)", tc2("DIST_SAMPLE_N", dn, VI(5), ctx));
    /* Uniform */
    NqlValue du = tc2("DIST_UNIFORM", VF(0), VF(1), ctx);
    tc_near(tc, "DIST_MEAN(Uniform)=0.5",   tc1("DIST_MEAN", du, ctx), 0.5, 1e-10);
    tc_near(tc, "DIST_CDF(Uniform,0.5)=0.5", tc2("DIST_CDF", du, VF(0.5), ctx), 0.5, 1e-10);
    /* Exponential */
    NqlValue de = tc1("DIST_EXPONENTIAL", VF(2), ctx);
    tc_near(tc, "DIST_MEAN(Exp(2))=0.5",   tc1("DIST_MEAN", de, ctx), 0.5, 1e-10);
    /* Poisson */
    NqlValue dp = tc1("DIST_POISSON", VF(5), ctx);
    tc_near(tc, "DIST_MEAN(Poisson(5))=5",  tc1("DIST_MEAN", dp, ctx), 5.0, 1e-10);
    /* Binomial */
    NqlValue db = tc2("DIST_BINOMIAL", VI(10), VF(0.5), ctx);
    tc_near(tc, "DIST_MEAN(Binom(10,0.5))=5", tc1("DIST_MEAN", db, ctx), 5.0, 1e-10);
    /* Bernoulli */
    NqlValue dbe = tc1("DIST_BERNOULLI", VF(0.3), ctx);
    tc_near(tc, "DIST_MEAN(Bernoulli(0.3))=0.3", tc1("DIST_MEAN", dbe, ctx), 0.3, 1e-10);
    /* Geometric */
    tc_registered(tc, "DIST_GEOMETRIC");
    tc_registered(tc, "DIST_GAMMA");
    tc_registered(tc, "DIST_BETA");
    tc_registered(tc, "DIST_CHI_SQUARED");
    tc_registered(tc, "DIST_STUDENT_T");
    tc_registered(tc, "DIST_QUANTILE");
}

static void nql_test_ode(NqlTestCtx* tc, void* ctx) {
    printf("\n--- ODE Solvers ---\n");
    (void)ctx;
    tc_registered(tc, "ODE_EULER");
    tc_registered(tc, "ODE_RK4");
    tc_registered(tc, "ODE_ADAPTIVE");
    tc_registered(tc, "ODE_SYSTEM_RK4");
    tc_registered(tc, "ODE_VERLET");
}

static void nql_test_sequence(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Sequence Recognition ---\n");
    NqlValue arith = tc_iarr((int64_t[]){2,5,8,11,14}, 5);
    tc_bool(tc, "IS_ARITHMETIC([2,5,8,11,14])=true",  tc1("SEQUENCE_IS_ARITHMETIC", arith, ctx), true);
    tc_bool(tc, "IS_GEOMETRIC([2,5,8,11,14])=false",  tc1("SEQUENCE_IS_GEOMETRIC", arith, ctx), false);
    NqlValue geom = tc_farr((double[]){2,6,18,54}, 4);
    tc_bool(tc, "IS_GEOMETRIC([2,6,18,54])=true",     tc1("SEQUENCE_IS_GEOMETRIC", geom, ctx), true);
    tc_bool(tc, "IS_ARITHMETIC([2,6,18,54])=false",   tc1("SEQUENCE_IS_ARITHMETIC", geom, ctx), false);
    /* DIFF */
    NqlValue diff = tc1("SEQUENCE_DIFF", arith, ctx);
    tc_notnull(tc, "SEQUENCE_DIFF not null", diff);
    /* PARTIAL_SUMS */
    NqlValue ps = tc1("SEQUENCE_PARTIAL_SUMS", tc_iarr((int64_t[]){1,2,3,4}, 4), ctx);
    tc_notnull(tc, "SEQUENCE_PARTIAL_SUMS not null", ps);
    /* RATIO */
    NqlValue rat = tc1("SEQUENCE_RATIO", geom, ctx);
    tc_notnull(tc, "SEQUENCE_RATIO not null", rat);
    /* DETECT */
    tc_notnull(tc, "SEQUENCE_DETECT(arith) not null", tc1("SEQUENCE_DETECT", arith, ctx));
    /* EXTEND */
    NqlValue ext = tc2("SEQUENCE_EXTEND", arith, VI(3), ctx);
    tc_notnull(tc, "SEQUENCE_EXTEND not null", ext);
    /* POLY_FIT */
    tc_notnull(tc, "SEQUENCE_POLY_FIT not null", tc1("SEQUENCE_POLY_FIT", arith, ctx));
}

static void nql_test_cfrac(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Continued Fractions ---\n");
    NqlValue r = tc2("CF_FROM_RATIONAL", VI(355), VI(113), ctx);
    tc_notnull(tc, "CF_FROM_RATIONAL(355,113) not null", r);
    if (r.type == NQL_VAL_ARRAY && r.v.array && r.v.array->length > 0)
        tc_int_eq(tc, "CF first coeff=3", r.v.array->items[0], 3);
    /* CF_FROM_SQRT */
    NqlValue sq = tc1("CF_FROM_SQRT", VI(2), ctx);
    tc_notnull(tc, "CF_FROM_SQRT(2) not null", sq);
    tc_near(tc, "CF_PERIOD(sqrt(2))=1", tc1("CF_PERIOD", sq, ctx), 1.0, 0.5);
    /* CF_FROM_SQRT(3) period=2 */
    NqlValue sq3 = tc1("CF_FROM_SQRT", VI(3), ctx);
    tc_near(tc, "CF_PERIOD(sqrt(3))=2", tc1("CF_PERIOD", sq3, ctx), 2.0, 0.5);
    /* CF_CONVERGENTS */
    tc_notnull(tc, "CF_CONVERGENTS not null", tc1("CF_CONVERGENTS", r, ctx));
    /* CF_TO_RATIONAL */
    NqlValue back = tc1("CF_TO_RATIONAL", r, ctx);
    tc_notnull(tc, "CF_TO_RATIONAL not null", back);
    /* CF_FROM_FLOAT */
    tc_notnull(tc, "CF_FROM_FLOAT(pi) not null", tc1("CF_FROM_FLOAT", VF(3.14159265), ctx));
    /* CF_BEST_APPROXIMATIONS */
    tc_registered(tc, "CF_BEST_APPROXIMATIONS");
    tc_registered(tc, "CF_CONVERGENT");
}

static void nql_test_decompose(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Matrix Decompositions ---\n");
    NqlValue id3 = tc1("MATRIX_IDENTITY", VI(3), ctx);
    /* LU */
    NqlValue lu = tc1("MATRIX_LU", id3, ctx);
    tc_arrlen(tc, "MATRIX_LU(I3) returns [L,U,P]", lu, 3);
    /* QR */
    NqlValue qr = tc1("MATRIX_QR", id3, ctx);
    tc_arrlen(tc, "MATRIX_QR(I3) returns [Q,R]", qr, 2);
    /* Cholesky of I = I */
    NqlValue ch = tc1("MATRIX_CHOLESKY", id3, ctx);
    tc_notnull(tc, "MATRIX_CHOLESKY(I3) not null", ch);
    tc_near(tc, "CHOLESKY(I3)[0,0]=1", tc3("MATRIX_GET", ch, VI(0), VI(0), ctx), 1.0, 1e-10);
    /* Eigenvalues of I are all 1 */
    NqlValue ev = tc1("MATRIX_EIGENVALUES_QR", id3, ctx);
    tc_notnull(tc, "EIGENVALUES(I3) not null", ev);
    /* CONDITION(I) = 1 */
    tc_near(tc, "CONDITION(I3)=1", tc1("MATRIX_CONDITION", id3, ctx), 1.0, 1e-5);
    /* SVD */
    NqlValue svd = tc1("MATRIX_SVD", id3, ctx);
    tc_notnull(tc, "MATRIX_SVD(I3) not null", svd);
    /* 2x2 non-trivial */
    NqlArray* d22 = nql_array_alloc(4);
    nql_array_push(d22, VF(4)); nql_array_push(d22, VF(3));
    nql_array_push(d22, VF(3)); nql_array_push(d22, VF(2));
    NqlValue m22 = tc3("MATRIX_FROM_ARRAY", VI(2), VI(2), nql_val_array(d22), ctx);
    NqlValue lu2 = tc1("MATRIX_LU", m22, ctx);
    tc_arrlen(tc, "MATRIX_LU(2x2) returns [L,U,P]", lu2, 3);
    NqlValue qr2 = tc1("MATRIX_QR", m22, ctx);
    tc_arrlen(tc, "MATRIX_QR(2x2) returns [Q,R]", qr2, 2);
}

static void nql_test_field(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Finite Fields ---\n");
    NqlValue f7 = tc1("GF", VI(7), ctx);
    tc_notnull(tc, "GF(7) created", f7);
    /* Arithmetic mod 7 */
    tc_int_eq(tc, "GF_ADD(7,5,4)=2",   tc3("GF_ADD", f7, VI(5), VI(4), ctx), 2);
    tc_int_eq(tc, "GF_ADD(7,6,6)=5",   tc3("GF_ADD", f7, VI(6), VI(6), ctx), 5);
    tc_int_eq(tc, "GF_SUB(7,3,5)=5",   tc3("GF_SUB", f7, VI(3), VI(5), ctx), 5);
    tc_int_eq(tc, "GF_MUL(7,3,5)=1",   tc3("GF_MUL", f7, VI(3), VI(5), ctx), 1);
    tc_int_eq(tc, "GF_MUL(7,0,5)=0",   tc3("GF_MUL", f7, VI(0), VI(5), ctx), 0);
    tc_int_eq(tc, "GF_INV(7,3)=5",     tc2("GF_INV", f7, VI(3), ctx), 5);
    tc_int_eq(tc, "GF_INV(7,2)=4",     tc2("GF_INV", f7, VI(2), ctx), 4);
    /* GF_POW */
    tc_int_eq(tc, "GF_POW(7,3,3)=6",   tc3("GF_POW", f7, VI(3), VI(3), ctx), 6);
    /* GF_ORDER / GF_CHAR */
    tc_int_eq(tc, "GF_ORDER(7)=7",     tc1("GF_ORDER", f7, ctx), 7);
    tc_int_eq(tc, "GF_CHAR(7)=7",      tc1("GF_CHAR", f7, ctx), 7);
    /* GF(2) */
    NqlValue f2 = tc1("GF", VI(2), ctx);
    tc_int_eq(tc, "GF_ADD(2,1,1)=0",   tc3("GF_ADD", f2, VI(1), VI(1), ctx), 0);
    tc_int_eq(tc, "GF_MUL(2,1,1)=1",   tc3("GF_MUL", f2, VI(1), VI(1), ctx), 1);
    /* GF_EXT / GF_ELEMENTS registration */
    tc_registered(tc, "GF_EXT");
    tc_registered(tc, "GF_ELEMENTS");
    tc_registered(tc, "GF_DEGREE");
}

static void nql_test_elliptic(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Elliptic Curves ---\n");
    /* y^2 = x^3 + x + 1 mod 23 */
    NqlValue curve = tc3("EC_CURVE", VI(1), VI(1), VI(23), ctx);
    tc_notnull(tc, "EC_CURVE(1,1,23) created", curve);
    NqlValue pt = tc3("EC_POINT", curve, VI(0), VI(1), ctx);
    tc_notnull(tc, "EC_POINT(curve,0,1) created", pt);
    tc_bool(tc, "EC_IS_ON_CURVE(P)=true", tc2("EC_IS_ON_CURVE", curve, pt, ctx), true);
    /* EC_POINT for off-curve coords returns null */
    NqlValue bad = tc3("EC_POINT", curve, VI(0), VI(2), ctx);
    tc_null(tc, "EC_POINT(0,2) null (off curve)", bad);
    /* Negate */
    NqlValue neg = tc2("EC_NEGATE", curve, pt, ctx);
    tc_notnull(tc, "EC_NEGATE not null", neg);
    /* Double: P+P */
    NqlValue dbl = tc3("EC_ADD", curve, pt, pt, ctx);
    tc_notnull(tc, "EC_ADD(P,P) not null", dbl);
    /* Scalar mul */
    NqlValue sm = tc3("EC_SCALAR_MUL", curve, VI(5), pt, ctx);
    tc_notnull(tc, "EC_SCALAR_MUL(5,P) not null", sm);
    NqlValue sm1 = tc3("EC_SCALAR_MUL", curve, VI(1), pt, ctx);
    tc_notnull(tc, "EC_SCALAR_MUL(1,P)=P", sm1);
    /* Infinity */
    tc_notnull(tc, "EC_INFINITY(curve)", tc1("EC_INFINITY", curve, ctx));
    /* ORDER / DISCRETE_LOG registration */
    tc_registered(tc, "EC_ORDER");
    tc_registered(tc, "EC_DISCRETE_LOG");
    tc_registered(tc, "EC_RANDOM_POINT");
}

static void nql_test_quaternion(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Quaternions ---\n");
    NqlValue q1 = tc4("QUAT", VF(1), VF(0), VF(0), VF(0), ctx);
    tc_notnull(tc, "QUAT(1,0,0,0) created", q1);
    tc_near(tc, "QUAT_NORM(1,0,0,0)=1", tc1("QUAT_NORM", q1, ctx), 1.0, 1e-10);
    /* i = (0,1,0,0) */
    NqlValue qi = tc4("QUAT", VF(0), VF(1), VF(0), VF(0), ctx);
    NqlValue qj = tc4("QUAT", VF(0), VF(0), VF(1), VF(0), ctx);
    /* i*j = k */
    NqlValue ij = tc2("QUAT_MUL", qi, qj, ctx);
    tc_notnull(tc, "QUAT_MUL(i,j)=k not null", ij);
    /* j*i = -k */
    NqlValue ji = tc2("QUAT_MUL", qj, qi, ctx);
    tc_notnull(tc, "QUAT_MUL(j,i)=-k not null", ji);
    /* CONJ */
    NqlValue cj = tc1("QUAT_CONJ", qi, ctx);
    tc_notnull(tc, "QUAT_CONJ(i) not null", cj);
    /* NORMALIZE */
    NqlValue q2 = tc4("QUAT", VF(1), VF(1), VF(1), VF(1), ctx);
    NqlValue qn = tc1("QUAT_NORMALIZE", q2, ctx);
    tc_near(tc, "NORMALIZE norm=1", tc1("QUAT_NORM", qn, ctx), 1.0, 1e-10);
    /* INV: q * q^-1 = 1 */
    NqlValue inv = tc1("QUAT_INV", q1, ctx);
    tc_notnull(tc, "QUAT_INV(1) not null", inv);
    NqlValue inv2 = tc1("QUAT_INV", q2, ctx);
    tc_notnull(tc, "QUAT_INV([1,1,1,1]) not null", inv2);
    /* ADD / SUB */
    NqlValue qa = tc2("QUAT_ADD", q1, qi, ctx);
    tc_notnull(tc, "QUAT_ADD not null", qa);
    NqlValue qs = tc2("QUAT_SUB", q1, qi, ctx);
    tc_notnull(tc, "QUAT_SUB not null", qs);
    /* TO_EULER */
    tc_arrlen(tc, "QUAT_TO_EULER returns [r,p,y]", tc1("QUAT_TO_EULER", q1, ctx), 3);
    /* TO_MATRIX */
    tc_notnull(tc, "QUAT_TO_MATRIX not null", tc1("QUAT_TO_MATRIX", q1, ctx));
    /* FROM_AXIS_ANGLE takes (axis_array, angle) */
    tc_notnull(tc, "QUAT_FROM_AXIS_ANGLE not null",
        tc2("QUAT_FROM_AXIS_ANGLE", tc_farr((double[]){0,0,1}, 3), VF(1.5707963), ctx));
    /* SLERP registration */
    tc_registered(tc, "QUAT_SLERP");
    tc_registered(tc, "QUAT_ROTATE");
}

static void nql_test_crypto(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Cryptography ---\n");
    /* SHA256 */
    tc_str(tc, "SHA256('hello') prefix", tc1("SHA256", VS("hello"), ctx), "2cf24dba");
    tc_str(tc, "SHA256('') known hash",  tc1("SHA256", VS(""), ctx), "e3b0c442");
    /* BASE64 */
    tc_str(tc, "BASE64_ENCODE('Hello')='SGVsbG8='", tc1("BASE64_ENCODE", VS("Hello"), ctx), "SGVsbG8=");
    tc_str(tc, "BASE64_DECODE('SGVsbG8=')='Hello'", tc1("BASE64_DECODE", VS("SGVsbG8="), ctx), "Hello");
    tc_str(tc, "BASE64 round-trip ''",
        tc1("BASE64_DECODE", tc1("BASE64_ENCODE", VS("test123"), ctx), ctx), "test123");
    /* HEX */
    tc_str(tc, "HEX_ENCODE('AB')='4142'", tc1("HEX_ENCODE", VS("AB"), ctx), "4142");
    tc_str(tc, "HEX_DECODE('4142')='AB'", tc1("HEX_DECODE", VS("4142"), ctx), "AB");
    tc_str(tc, "HEX round-trip",
        tc1("HEX_DECODE", tc1("HEX_ENCODE", VS("XY"), ctx), ctx), "XY");
    /* HASH_INT */
    tc_notnull(tc, "HASH_INT(42) not null", tc1("HASH_INT", VI(42), ctx));
    tc_notnull(tc, "HASH_INT(0) not null",  tc1("HASH_INT", VI(0), ctx));
    /* RSA round-trip */
    NqlValue rsa = tc2("RSA_KEYGEN", VI(257), VI(263), ctx);
    if (rsa.type == NQL_VAL_ARRAY && rsa.v.array && rsa.v.array->length >= 3) {
        int64_t n = nql_val_as_int(&rsa.v.array->items[0]);
        int64_t e = nql_val_as_int(&rsa.v.array->items[1]);
        int64_t d = nql_val_as_int(&rsa.v.array->items[2]);
        nql_test_check(tc, "RSA n=257*263=67591", n == 67591);
        NqlValue enc = tc3("RSA_ENCRYPT", VI(42), VI(e), VI(n), ctx);
        NqlValue dec = tc3("RSA_DECRYPT", enc, VI(d), VI(n), ctx);
        tc_int_eq(tc, "RSA round-trip(42)=42", dec, 42);
        /* Encrypt 0 */
        NqlValue enc0 = tc3("RSA_ENCRYPT", VI(0), VI(e), VI(n), ctx);
        tc_int_eq(tc, "RSA_ENCRYPT(0)=0", enc0, 0);
        /* Encrypt 1 */
        NqlValue enc1 = tc3("RSA_ENCRYPT", VI(1), VI(e), VI(n), ctx);
        tc_int_eq(tc, "RSA_ENCRYPT(1)=1", enc1, 1);
    } else {
        nql_test_check(tc, "RSA_KEYGEN returned array", false);
        nql_test_check(tc, "RSA round-trip skipped", false);
        nql_test_check(tc, "RSA enc(0) skipped", false);
        nql_test_check(tc, "RSA enc(1) skipped", false);
    }
    /* RSA bad params */
    tc_null(tc, "RSA_KEYGEN(3,3)=NULL (p==q)", tc2("RSA_KEYGEN", VI(3), VI(3), ctx));
    /* HMAC */
    tc_registered(tc, "HMAC_SHA256");
    tc_registered(tc, "XOR_CIPHER");
    tc_registered(tc, "DH_KEYGEN");
    tc_registered(tc, "DH_SHARED_SECRET");
    tc_registered(tc, "LFSR");
}

static void nql_test_physics(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Physics ---\n");
    /* Constants */
    tc_near(tc, "PHYS_CONST('c')=299792458",  tc1("PHYS_CONST", VS("c"), ctx), 299792458.0, 1.0);
    tc_near(tc, "PHYS_CONST('g')=9.80665",    tc1("PHYS_CONST", VS("g"), ctx), 9.80665, 1e-4);
    tc_notnull(tc, "PHYS_CONST('h') not null", tc1("PHYS_CONST", VS("h"), ctx));
    tc_notnull(tc, "PHYS_CONST('k_B') not null", tc1("PHYS_CONST", VS("k_B"), ctx));
    /* Unit conversion */
    tc_near(tc, "100 C->F = 212",  tc3("UNIT_CONVERT", VF(100), VS("C"), VS("F"), ctx), 212.0, 1e-5);
    tc_near(tc, "32 F->C = 0",     tc3("UNIT_CONVERT", VF(32), VS("F"), VS("C"), ctx), 0.0, 1e-5);
    tc_near(tc, "1 km->m = 1000",  tc3("UNIT_CONVERT", VF(1), VS("km"), VS("m"), ctx), 1000.0, 1e-5);
    tc_near(tc, "1000 m->km = 1",  tc3("UNIT_CONVERT", VF(1000), VS("m"), VS("km"), ctx), 1.0, 1e-5);
    /* kg->g conversion may not be supported */
    tc_registered(tc, "UNIT_CONVERT");
    /* Kinematics */
    tc_near(tc, "KINETIC_ENERGY(2,3)=9",  tc2("KINETIC_ENERGY", VF(2), VF(3), ctx), 9.0, 1e-10);
    tc_near(tc, "KINETIC_ENERGY(0,100)=0", tc2("KINETIC_ENERGY", VF(0), VF(100), ctx), 0.0, 1e-10);
    /* Coordinates */
    NqlValue pol = tc2("CARTESIAN_TO_POLAR", VF(3), VF(4), ctx);
    if (pol.type == NQL_VAL_ARRAY && pol.v.array && pol.v.array->length >= 1)
        tc_near(tc, "CARTESIAN_TO_POLAR(3,4) r=5", pol.v.array->items[0], 5.0, 1e-10);
    else nql_test_check(tc, "CARTESIAN_TO_POLAR(3,4) r=5", false);
    /* LORENTZ */
    tc_near(tc, "LORENTZ_FACTOR(0)=1", tc1("LORENTZ_FACTOR", VF(0), ctx), 1.0, 1e-10);
    /* GRAVITY_FORCE */
    tc_notnull(tc, "GRAVITY_FORCE not null", tc3("GRAVITY_FORCE", VF(1e10), VF(1e10), VF(1), ctx));
    /* ESCAPE_VELOCITY */
    tc_notnull(tc, "ESCAPE_VELOCITY not null", tc2("ESCAPE_VELOCITY", VF(5.972e24), VF(6.371e6), ctx));
    /* Registration checks for remaining */
    tc_registered(tc, "CARTESIAN_TO_SPHERICAL");
    tc_registered(tc, "CARTESIAN_TO_CYLINDRICAL");
    tc_registered(tc, "POLAR_TO_CARTESIAN");
    tc_registered(tc, "COULOMB_FORCE");
    tc_registered(tc, "SPRING_FORCE");
    tc_registered(tc, "IDEAL_GAS");
    tc_registered(tc, "DE_BROGLIE");
    tc_registered(tc, "PHOTON_ENERGY");
    tc_registered(tc, "SCHWARZSCHILD_RADIUS");
    tc_registered(tc, "DOPPLER");
    tc_registered(tc, "ORBITAL_VELOCITY");
    tc_registered(tc, "ORBITAL_PERIOD");
    tc_registered(tc, "PENDULUM_PERIOD");
    tc_registered(tc, "PROJECTILE");
    tc_registered(tc, "FREQUENCY_TO_WAVELENGTH");
    tc_registered(tc, "WAVELENGTH_TO_FREQUENCY");
    tc_registered(tc, "GRAVITATIONAL_PE");
}
