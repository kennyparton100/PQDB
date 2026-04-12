/* nql_test_types.c - Complex, matrix, symbolic, rational, vector, interval, series, graph */

static void nql_test_complex(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Complex Numbers ---\n");
    NqlValue c1 = tc2("COMPLEX", VF(3), VF(4), ctx);
    tc_notnull(tc, "COMPLEX(3,4) created", c1);
    /* Parts */
    tc_near(tc, "COMPLEX_REAL(3+4i)=3", tc1("COMPLEX_REAL", c1, ctx), 3.0, 1e-12);
    tc_near(tc, "COMPLEX_IMAG(3+4i)=4", tc1("COMPLEX_IMAG", c1, ctx), 4.0, 1e-12);
    tc_near(tc, "COMPLEX_ABS(3+4i)=5",  tc1("COMPLEX_ABS", c1, ctx), 5.0, 1e-10);
    /* Conjugate: 3-4i */
    NqlValue cj = tc1("COMPLEX_CONJ", c1, ctx);
    tc_notnull(tc, "COMPLEX_CONJ not null", cj);
    tc_near(tc, "CONJ imag=-4", tc1("COMPLEX_IMAG", cj, ctx), -4.0, 1e-12);
    /* Arithmetic */
    NqlValue c2 = tc2("COMPLEX", VF(1), VF(2), ctx);
    NqlValue sum = tc2("COMPLEX_ADD", c1, c2, ctx);
    tc_near(tc, "COMPLEX_ADD real=4", tc1("COMPLEX_REAL", sum, ctx), 4.0, 1e-12);
    tc_near(tc, "COMPLEX_ADD imag=6", tc1("COMPLEX_IMAG", sum, ctx), 6.0, 1e-12);
    NqlValue diff = tc2("COMPLEX_SUB", c1, c2, ctx);
    tc_near(tc, "COMPLEX_SUB real=2", tc1("COMPLEX_REAL", diff, ctx), 2.0, 1e-12);
    NqlValue prod = tc2("COMPLEX_MUL", c1, c2, ctx);
    tc_notnull(tc, "COMPLEX_MUL not null", prod);
    /* (3+4i)*(1+2i) = 3+6i+4i+8i^2 = 3+10i-8 = -5+10i */
    tc_near(tc, "COMPLEX_MUL real=-5", tc1("COMPLEX_REAL", prod, ctx), -5.0, 1e-10);
    tc_near(tc, "COMPLEX_MUL imag=10", tc1("COMPLEX_IMAG", prod, ctx), 10.0, 1e-10);
    /* Division */
    NqlValue quot = tc2("COMPLEX_DIV", c1, c2, ctx);
    tc_notnull(tc, "COMPLEX_DIV not null", quot);
    /* (3+4i)/(1+2i) = (3+4i)(1-2i)/5 = (3-6i+4i-8i^2)/5 = (11-2i)/5 = 2.2-0.4i */
    tc_near(tc, "COMPLEX_DIV real=2.2", tc1("COMPLEX_REAL", quot, ctx), 2.2, 1e-10);
    tc_near(tc, "COMPLEX_DIV imag=-0.4", tc1("COMPLEX_IMAG", quot, ctx), -0.4, 1e-10);
    /* Div by zero complex */
    NqlValue z = tc2("COMPLEX", VF(0), VF(0), ctx);
    NqlValue divz = tc2("COMPLEX_DIV", c1, z, ctx);
    /* Might return inf or null - just don't crash */
    (void)divz;
    nql_test_check(tc, "COMPLEX_DIV by zero no crash", true);
    /* COMPLEX_EXP, COMPLEX_LOG */
    NqlValue zero_c = tc2("COMPLEX", VF(0), VF(0), ctx);
    NqlValue exp0 = tc1("COMPLEX_EXP", zero_c, ctx);
    tc_near(tc, "COMPLEX_EXP(0) real=1", tc1("COMPLEX_REAL", exp0, ctx), 1.0, 1e-10);
    tc_near(tc, "COMPLEX_EXP(0) imag=0", tc1("COMPLEX_IMAG", exp0, ctx), 0.0, 1e-10);
    /* COMPLEX_SQRT */
    NqlValue sq = tc1("COMPLEX_SQRT", c1, ctx);
    tc_notnull(tc, "COMPLEX_SQRT(3+4i) not null", sq);
    /* sqrt(3+4i) = 2+i, verify: (2+i)^2 = 4+4i+i^2 = 3+4i */
    tc_near(tc, "COMPLEX_SQRT real=2", tc1("COMPLEX_REAL", sq, ctx), 2.0, 1e-10);
    tc_near(tc, "COMPLEX_SQRT imag=1", tc1("COMPLEX_IMAG", sq, ctx), 1.0, 1e-10);
    /* COMPLEX_POLAR */
    NqlValue polar = tc2("COMPLEX_POLAR", VF(5), VF(0), ctx);
    tc_near(tc, "COMPLEX_POLAR(5,0) real=5", tc1("COMPLEX_REAL", polar, ctx), 5.0, 1e-10);
    /* COMPLEX_ARG */
    NqlValue pure_i = tc2("COMPLEX", VF(0), VF(1), ctx);
    tc_near(tc, "COMPLEX_ARG(i)=pi/2", tc1("COMPLEX_ARG", pure_i, ctx), 1.5707963, 1e-5);
}

static void nql_test_matrix(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Dense Matrices ---\n");
    NqlValue r;
    /* Identity */
    NqlValue id3 = tc1("MATRIX_IDENTITY", VI(3), ctx);
    tc_notnull(tc, "MATRIX_IDENTITY(3) created", id3);
    tc_int_eq(tc, "MATRIX_ROWS(I3)=3", tc1("MATRIX_ROWS", id3, ctx), 3);
    tc_int_eq(tc, "MATRIX_COLS(I3)=3", tc1("MATRIX_COLS", id3, ctx), 3);
    tc_near(tc, "DET(I3)=1",   tc1("MATRIX_DETERMINANT", id3, ctx), 1.0, 1e-10);
    tc_near(tc, "TRACE(I3)=3", tc1("MATRIX_TRACE", id3, ctx), 3.0, 1e-10);
    /* GET element */
    tc_near(tc, "I3[0,0]=1", tc3("MATRIX_GET", id3, VI(0), VI(0), ctx), 1.0, 1e-12);
    tc_near(tc, "I3[0,1]=0", tc3("MATRIX_GET", id3, VI(0), VI(1), ctx), 0.0, 1e-12);
    /* 1x1 matrix */
    NqlValue id1 = tc1("MATRIX_IDENTITY", VI(1), ctx);
    tc_near(tc, "DET(I1)=1", tc1("MATRIX_DETERMINANT", id1, ctx), 1.0, 1e-12);
    /* 2x2 from array: [[1,2],[3,4]] */
    NqlArray* d = nql_array_alloc(4);
    nql_array_push(d, VF(1)); nql_array_push(d, VF(2));
    nql_array_push(d, VF(3)); nql_array_push(d, VF(4));
    NqlValue m22 = tc3("MATRIX_FROM_ARRAY", VI(2), VI(2), nql_val_array(d), ctx);
    tc_notnull(tc, "MATRIX_FROM_ARRAY(2x2) created", m22);
    tc_near(tc, "DET([[1,2],[3,4]])=-2", tc1("MATRIX_DETERMINANT", m22, ctx), -2.0, 1e-10);
    tc_near(tc, "TRACE([[1,2],[3,4]])=5", tc1("MATRIX_TRACE", m22, ctx), 5.0, 1e-10);
    /* Transpose */
    NqlValue mt = tc1("MATRIX_TRANSPOSE", m22, ctx);
    tc_notnull(tc, "MATRIX_TRANSPOSE not null", mt);
    tc_near(tc, "Transpose[0,1]=3", tc3("MATRIX_GET", mt, VI(0), VI(1), ctx), 3.0, 1e-12);
    tc_near(tc, "Transpose[1,0]=2", tc3("MATRIX_GET", mt, VI(1), VI(0), ctx), 2.0, 1e-12);
    /* Inverse of I3 = I3 */
    NqlValue inv = tc1("MATRIX_INVERSE", id3, ctx);
    tc_notnull(tc, "MATRIX_INVERSE(I3) not null", inv);
    tc_near(tc, "INV(I3)[0,0]=1", tc3("MATRIX_GET", inv, VI(0), VI(0), ctx), 1.0, 1e-10);
    /* Inverse of 2x2 */
    NqlValue inv22 = tc1("MATRIX_INVERSE", m22, ctx);
    tc_notnull(tc, "MATRIX_INVERSE(2x2) not null", inv22);
    /* Matrix multiplication: I*M = M */
    NqlValue prod = tc2("MATRIX_MUL", id3, id3, ctx);
    tc_notnull(tc, "MATRIX_MUL(I3,I3) not null", prod);
    tc_near(tc, "I3*I3 DET=1", tc1("MATRIX_DETERMINANT", prod, ctx), 1.0, 1e-10);
    /* ADD, SUB, SCALE */
    NqlValue sum = tc2("MATRIX_ADD", m22, m22, ctx);
    tc_near(tc, "M+M [0,0]=2", tc3("MATRIX_GET", sum, VI(0), VI(0), ctx), 2.0, 1e-12);
    NqlValue sub = tc2("MATRIX_SUB", m22, m22, ctx);
    tc_near(tc, "M-M [0,0]=0", tc3("MATRIX_GET", sub, VI(0), VI(0), ctx), 0.0, 1e-12);
    NqlValue sc = tc2("MATRIX_SCALE", m22, VF(3), ctx);
    tc_near(tc, "3*M [0,0]=3", tc3("MATRIX_GET", sc, VI(0), VI(0), ctx), 3.0, 1e-12);
    /* RANK */
    tc_int_eq(tc, "RANK(I3)=3", tc1("MATRIX_RANK", id3, ctx), 3);
    /* NORM */
    tc_notnull(tc, "MATRIX_NORM not null", tc1("MATRIX_NORM", m22, ctx));
    /* TO_ARRAY */
    r = tc1("MATRIX_TO_ARRAY", m22, ctx);
    tc_arrlen(tc, "MATRIX_TO_ARRAY(2x2) len=4", r, 4);
}

static void nql_test_symbolic(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Symbolic ---\n");
    NqlValue x = tc1("SYMBOL", VS("x"), ctx);
    tc_notnull(tc, "SYMBOL('x') created", x);
    NqlValue c = tc1("SYM_CONST", VF(42), ctx);
    tc_notnull(tc, "SYM_CONST(42) created", c);
    NqlValue sum = tc2("SYM_ADD", x, c, ctx);
    tc_notnull(tc, "SYM_ADD(x,42) created", sum);
    NqlValue prod = tc2("SYM_MUL", x, c, ctx);
    tc_notnull(tc, "SYM_MUL(x,42) created", prod);
    NqlValue pw = tc2("SYM_POW", x, tc1("SYM_CONST", VF(2), ctx), ctx);
    tc_notnull(tc, "SYM_POW(x,2) created", pw);
    NqlValue s = tc1("SYM_TO_STRING", sum, ctx);
    tc_notnull(tc, "SYM_TO_STRING(x+42) not null", s);
    /* SYM_EVAL */
    NqlValue zero = tc1("SYM_CONST", VF(0), ctx);
    tc_notnull(tc, "SYM_CONST(0) created", zero);
    /* SYM_EVAL requires variable binding - just check registration */
    tc_registered(tc, "SYM_EVAL");
}

static void nql_test_rational(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Rational Numbers ---\n");
    NqlValue r34 = tc2("RATIONAL", VI(3), VI(4), ctx);
    tc_notnull(tc, "RATIONAL(3,4) created", r34);
    NqlValue r14 = tc2("RATIONAL", VI(1), VI(4), ctx);
    /* ADD: 3/4 + 1/4 = 1 */
    NqlValue sum = tc2("RATIONAL_ADD", r34, r14, ctx);
    tc_notnull(tc, "RATIONAL_ADD(3/4,1/4) not null", sum);
    tc_near(tc, "3/4+1/4=1.0", tc1("RATIONAL_TO_FLOAT", sum, ctx), 1.0, 1e-10);
    /* SUB: 3/4 - 1/4 = 1/2 */
    NqlValue diff = tc2("RATIONAL_SUB", r34, r14, ctx);
    tc_near(tc, "3/4-1/4=0.5", tc1("RATIONAL_TO_FLOAT", diff, ctx), 0.5, 1e-10);
    /* MUL: 3/4 * 1/4 = 3/16 */
    NqlValue prod = tc2("RATIONAL_MUL", r34, r14, ctx);
    tc_near(tc, "3/4*1/4=0.1875", tc1("RATIONAL_TO_FLOAT", prod, ctx), 0.1875, 1e-10);
    /* DIV: 3/4 / 1/4 = 3 */
    NqlValue quot = tc2("RATIONAL_DIV", r34, r14, ctx);
    tc_near(tc, "3/4 / 1/4 = 3", tc1("RATIONAL_TO_FLOAT", quot, ctx), 3.0, 1e-10);
    /* NUM / DEN */
    tc_int_eq(tc, "RATIONAL_NUM(3/4)=3", tc1("RATIONAL_NUM", r34, ctx), 3);
    tc_int_eq(tc, "RATIONAL_DEN(3/4)=4", tc1("RATIONAL_DEN", r34, ctx), 4);
    /* ABS */
    NqlValue neg = tc2("RATIONAL", VI(-3), VI(4), ctx);
    NqlValue ab = tc1("RATIONAL_ABS", neg, ctx);
    tc_near(tc, "RATIONAL_ABS(-3/4)=0.75", tc1("RATIONAL_TO_FLOAT", ab, ctx), 0.75, 1e-10);
    /* CMP */
    NqlValue cmp = tc2("RATIONAL_CMP", r34, r14, ctx);
    nql_test_check(tc, "RATIONAL_CMP(3/4,1/4)>0", nql_val_as_int(&cmp) > 0);
    /* DIV by zero: RATIONAL(1,0) */
    NqlValue bad = tc2("RATIONAL", VI(1), VI(0), ctx);
    tc_null(tc, "RATIONAL(1,0)=NULL (div-by-zero)", bad);
    /* TO_STRING */
    tc_notnull(tc, "RATIONAL_TO_STRING not null", tc1("RATIONAL_TO_STRING", r34, ctx));
    /* TO_CF */
    tc_notnull(tc, "RATIONAL_TO_CF not null", tc1("RATIONAL_TO_CF", r34, ctx));
}

static void nql_test_vector(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Vectors ---\n");
    NqlValue v1 = tc_farr((double[]){3, 4}, 2);
    NqlValue v2 = tc_farr((double[]){1, 0}, 2);
    /* NORM */
    tc_near(tc, "NORM([3,4])=5",       tc1("NORM", v1, ctx), 5.0, 1e-10);
    /* VECTOR_NORM */
    tc_near(tc, "VECTOR_NORM([3,4])=5", tc1("VECTOR_NORM", v1, ctx), 5.0, 1e-10);
    /* DOT_PRODUCT */
    tc_near(tc, "DOT([3,4],[1,0])=3",  tc2("DOT_PRODUCT", v1, v2, ctx), 3.0, 1e-10);
    tc_near(tc, "VECTOR_DOT same",     tc2("VECTOR_DOT", v1, v2, ctx), 3.0, 1e-10);
    /* ADD / SUB */
    NqlValue vsum = tc2("VECTOR_ADD", v1, v2, ctx);
    tc_notnull(tc, "VECTOR_ADD not null", vsum);
    NqlValue vsub = tc2("VECTOR_SUB", v1, v2, ctx);
    tc_notnull(tc, "VECTOR_SUB not null", vsub);
    /* SCALE */
    NqlValue vsc = tc2("VECTOR_SCALE", v1, VF(2), ctx);
    tc_notnull(tc, "VECTOR_SCALE not null", vsc);
    /* NORMALIZE */
    NqlValue vn = tc1("VECTOR_NORMALIZE", v1, ctx);
    tc_near(tc, "NORMALIZE([3,4]) norm=1", tc1("VECTOR_NORM", vn, ctx), 1.0, 1e-10);
    /* NEGATE */
    NqlValue vneg = tc1("VECTOR_NEGATE", v1, ctx);
    tc_notnull(tc, "VECTOR_NEGATE not null", vneg);
    /* DISTANCE */
    tc_near(tc, "DISTANCE([3,4],[1,0])=sqrt(20)", tc2("VECTOR_DISTANCE", v1, v2, ctx), sqrt(20.0), 1e-10);
    /* DIM */
    tc_int_eq(tc, "VECTOR_DIM([3,4])=2", tc1("VECTOR_DIM", v1, ctx), 2);
    /* CROSS 3D */
    NqlValue a3 = tc_farr((double[]){1, 0, 0}, 3);
    NqlValue b3 = tc_farr((double[]){0, 1, 0}, 3);
    NqlValue cr = tc2("CROSS_PRODUCT", a3, b3, ctx);
    tc_notnull(tc, "CROSS_PRODUCT not null", cr);
    /* ANGLE */
    tc_near(tc, "VECTOR_ANGLE([1,0],[0,1])=pi/2", tc2("VECTOR_ANGLE", tc_farr((double[]){1,0}, 2), tc_farr((double[]){0,1}, 2), ctx), 1.5707963, 1e-5);
}

static void nql_test_interval(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Interval Arithmetic ---\n");
    NqlValue i1 = tc2("INTERVAL", VF(1), VF(3), ctx);
    tc_notnull(tc, "INTERVAL(1,3) created", i1);
    NqlValue i2 = tc2("INTERVAL", VF(2), VF(5), ctx);
    /* LO / HI */
    tc_near(tc, "INTERVAL_LO([1,3])=1", tc1("INTERVAL_LO", i1, ctx), 1.0, 1e-12);
    tc_near(tc, "INTERVAL_HI([1,3])=3", tc1("INTERVAL_HI", i1, ctx), 3.0, 1e-12);
    /* WIDTH / MIDPOINT */
    tc_near(tc, "INTERVAL_WIDTH([1,3])=2",    tc1("INTERVAL_WIDTH", i1, ctx), 2.0, 1e-12);
    tc_near(tc, "INTERVAL_MIDPOINT([1,3])=2", tc1("INTERVAL_MIDPOINT", i1, ctx), 2.0, 1e-12);
    /* ADD: [1,3]+[2,5] = [3,8] */
    NqlValue ia = tc2("INTERVAL_ADD", i1, i2, ctx);
    tc_near(tc, "INTERVAL_ADD lo=3", tc1("INTERVAL_LO", ia, ctx), 3.0, 1e-12);
    tc_near(tc, "INTERVAL_ADD hi=8", tc1("INTERVAL_HI", ia, ctx), 8.0, 1e-12);
    /* SUB: [1,3]-[2,5] = [1-5,3-2] = [-4,1] */
    NqlValue is = tc2("INTERVAL_SUB", i1, i2, ctx);
    tc_near(tc, "INTERVAL_SUB lo=-4", tc1("INTERVAL_LO", is, ctx), -4.0, 1e-12);
    tc_near(tc, "INTERVAL_SUB hi=1",  tc1("INTERVAL_HI", is, ctx), 1.0, 1e-12);
    /* MUL */
    NqlValue im = tc2("INTERVAL_MUL", i1, i2, ctx);
    tc_notnull(tc, "INTERVAL_MUL not null", im);
    /* OVERLAPS */
    tc_bool(tc, "OVERLAPS([1,3],[2,5])=true",  tc2("INTERVAL_OVERLAPS", i1, i2, ctx), true);
    NqlValue i3 = tc2("INTERVAL", VF(10), VF(20), ctx);
    tc_bool(tc, "OVERLAPS([1,3],[10,20])=false", tc2("INTERVAL_OVERLAPS", i1, i3, ctx), false);
    /* CONTAINS */
    tc_bool(tc, "CONTAINS([1,3],2)=true",  tc2("INTERVAL_CONTAINS", i1, VF(2), ctx), true);
    tc_bool(tc, "CONTAINS([1,3],5)=false", tc2("INTERVAL_CONTAINS", i1, VF(5), ctx), false);
    /* HULL */
    NqlValue ih = tc2("INTERVAL_HULL", i1, i3, ctx);
    tc_near(tc, "HULL([1,3],[10,20]) lo=1",  tc1("INTERVAL_LO", ih, ctx), 1.0, 1e-12);
    tc_near(tc, "HULL([1,3],[10,20]) hi=20", tc1("INTERVAL_HI", ih, ctx), 20.0, 1e-12);
    /* SQRT */
    NqlValue isq = tc1("INTERVAL_SQRT", tc2("INTERVAL", VF(4), VF(9), ctx), ctx);
    tc_near(tc, "INTERVAL_SQRT([4,9]) lo=2", tc1("INTERVAL_LO", isq, ctx), 2.0, 1e-10);
    tc_near(tc, "INTERVAL_SQRT([4,9]) hi=3", tc1("INTERVAL_HI", isq, ctx), 3.0, 1e-10);
}

static void nql_test_series(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Formal Power Series ---\n");
    /* 1 + 0*x + 1*x^2 = 1+x^2, order 3 */
    NqlArray* c = nql_array_alloc(3);
    nql_array_push(c, VF(1)); nql_array_push(c, VF(0)); nql_array_push(c, VF(1));
    NqlValue s = tc2("SERIES", nql_val_array(c), VI(3), ctx);
    tc_notnull(tc, "SERIES(1+x^2,3) created", s);
    /* EVAL */
    tc_near(tc, "SERIES_EVAL(1+x^2,2)=5", tc2("SERIES_EVAL", s, VF(2), ctx), 5.0, 1e-10);
    tc_near(tc, "SERIES_EVAL(1+x^2,0)=1", tc2("SERIES_EVAL", s, VF(0), ctx), 1.0, 1e-10);
    /* ORDER */
    tc_int_eq(tc, "SERIES_ORDER=3", tc1("SERIES_ORDER", s, ctx), 3);
    /* COEFF */
    tc_near(tc, "SERIES_COEFF(0)=1", tc2("SERIES_COEFF", s, VI(0), ctx), 1.0, 1e-12);
    tc_near(tc, "SERIES_COEFF(1)=0", tc2("SERIES_COEFF", s, VI(1), ctx), 0.0, 1e-12);
    tc_near(tc, "SERIES_COEFF(2)=1", tc2("SERIES_COEFF", s, VI(2), ctx), 1.0, 1e-12);
    /* ADD */
    NqlValue s2 = tc2("SERIES_ADD", s, s, ctx);
    tc_near(tc, "SERIES_ADD eval(2)=10", tc2("SERIES_EVAL", s2, VF(2), ctx), 10.0, 1e-10);
    /* SUB */
    NqlValue sd = tc2("SERIES_SUB", s, s, ctx);
    tc_near(tc, "SERIES_SUB eval(2)=0", tc2("SERIES_EVAL", sd, VF(2), ctx), 0.0, 1e-10);
    /* MUL */
    NqlValue sm = tc2("SERIES_MUL", s, s, ctx);
    tc_notnull(tc, "SERIES_MUL not null", sm);
    /* DERIVATIVE: d/dx(1+x^2) = 2x, eval(3)=6 */
    NqlValue der = tc1("SERIES_DERIVATIVE", s, ctx);
    tc_near(tc, "SERIES_DERIVATIVE eval(3)=6", tc2("SERIES_EVAL", der, VF(3), ctx), 6.0, 1e-10);
    /* TO_ARRAY */
    NqlValue arr = tc1("SERIES_TO_ARRAY", s, ctx);
    tc_notnull(tc, "SERIES_TO_ARRAY not null", arr);
    /* SERIES_EXP(order) - e^x series */
    tc_registered(tc, "SERIES_EXP");
    tc_registered(tc, "SERIES_SIN");
    tc_registered(tc, "SERIES_COS");
    tc_registered(tc, "SERIES_LOG");
}

static void nql_test_graph(NqlTestCtx* tc, void* ctx) {
    printf("\n--- Graphs ---\n");
    /* Build triangle: 0-1-2-0 */
    NqlArray* edges = nql_array_alloc(3);
    NqlArray* e1 = nql_array_alloc(2); nql_array_push(e1, VI(0)); nql_array_push(e1, VI(1));
    NqlArray* e2 = nql_array_alloc(2); nql_array_push(e2, VI(1)); nql_array_push(e2, VI(2));
    NqlArray* e3 = nql_array_alloc(2); nql_array_push(e3, VI(2)); nql_array_push(e3, VI(0));
    nql_array_push(edges, nql_val_array(e1));
    nql_array_push(edges, nql_val_array(e2));
    nql_array_push(edges, nql_val_array(e3));
    NqlValue g = tc2("GRAPH", VI(3), nql_val_array(edges), ctx);
    tc_notnull(tc, "GRAPH(3, edges) created", g);
    tc_int_eq(tc, "GRAPH_NODE_COUNT=3", tc1("GRAPH_NODE_COUNT", g, ctx), 3);
    tc_int_eq(tc, "GRAPH_EDGE_COUNT=3", tc1("GRAPH_EDGE_COUNT", g, ctx), 3);
    /* BFS */
    NqlValue bfs = tc2("GRAPH_BFS", g, VI(0), ctx);
    tc_arrlen(tc, "GRAPH_BFS from 0 returns 3 nodes", bfs, 3);
    /* DFS */
    NqlValue dfs = tc2("GRAPH_DFS", g, VI(0), ctx);
    tc_arrlen(tc, "GRAPH_DFS from 0 returns 3 nodes", dfs, 3);
    /* DEGREE */
    tc_int_eq(tc, "GRAPH_DEGREE(g,0)=2", tc2("GRAPH_DEGREE", g, VI(0), ctx), 2);
    /* NEIGHBORS */
    NqlValue nb = tc2("GRAPH_NEIGHBORS", g, VI(0), ctx);
    tc_notnull(tc, "GRAPH_NEIGHBORS(0) not null", nb);
    /* HAS_CYCLE */
    tc_bool(tc, "GRAPH_HAS_CYCLE(triangle)=true", tc1("GRAPH_HAS_CYCLE", g, ctx), true);
    /* CONNECTED_COMPONENTS */
    tc_notnull(tc, "GRAPH_CONNECTED_COMPONENTS not null", tc1("GRAPH_CONNECTED_COMPONENTS", g, ctx));
    /* MST */
    tc_notnull(tc, "GRAPH_MST not null", tc1("GRAPH_MST", g, ctx));
    /* Line graph (no cycle): 0-1-2 */
    NqlArray* line_e = nql_array_alloc(2);
    NqlArray* le1 = nql_array_alloc(2); nql_array_push(le1, VI(0)); nql_array_push(le1, VI(1));
    NqlArray* le2 = nql_array_alloc(2); nql_array_push(le2, VI(1)); nql_array_push(le2, VI(2));
    nql_array_push(line_e, nql_val_array(le1));
    nql_array_push(line_e, nql_val_array(le2));
    NqlValue lg = tc2("GRAPH", VI(3), nql_val_array(line_e), ctx);
    /* SHORTEST_PATH */
    NqlValue sp = tc3("GRAPH_SHORTEST_PATH", lg, VI(0), VI(2), ctx);
    tc_notnull(tc, "GRAPH_SHORTEST_PATH(0->2) not null", sp);
    /* IS_BIPARTITE - line is bipartite */
    tc_bool(tc, "GRAPH_IS_BIPARTITE(line)=true", tc1("GRAPH_IS_BIPARTITE", lg, ctx), true);
    /* TOPOLOGICAL_SORT - registration check only (needs directed graph) */
    tc_registered(tc, "GRAPH_TOPOLOGICAL_SORT");
}
