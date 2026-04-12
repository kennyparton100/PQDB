/**
 * nql_graph.c - NQL Graph type and algorithms.
 * Part of the CPSS Viewer amalgamation.
 *
 * Adjacency-list graph with weighted edges. Stored as NQL_VAL_GRAPH via v.graph.
 * Supports directed and undirected graphs, BFS, DFS, shortest path, MST,
 * connected components, topological sort, and cycle detection.
 */

/* Forward declarations */
typedef NqlValue (*NqlBuiltinFn)(const NqlValue* args, uint32_t argc, void* db_ctx);
void nql_register_func(const char* name, NqlBuiltinFn fn, uint32_t min_args, uint32_t max_args, const char* desc);

/* ======================================================================
 * NqlGraph STRUCT
 * ====================================================================== */

#define NQL_GRAPH_MAX_NODES 4096
#define NQL_GRAPH_MAX_EDGES 16384

typedef struct {
    uint32_t to;
    double   weight;
} NqlEdge;

struct NqlGraph_s {
    uint32_t  node_count;
    uint32_t  edge_count;
    bool      directed;
    NqlEdge*  edges;        /* flat edge array */
    uint32_t* adj_offset;   /* adj_offset[i] = start index in edges for node i */
    uint32_t* adj_count;    /* adj_count[i] = number of edges from node i */
};

static void nql_graph_free(NqlGraph* g) {
    if (!g) return;
    free(g->edges);
    free(g->adj_offset);
    free(g->adj_count);
    free(g);
}

static NqlValue nql_val_graph(NqlGraph* g) {
    NqlValue v;
    memset(&v, 0, sizeof(v));
    v.type = NQL_VAL_GRAPH;
    v.v.graph = g;
    return v;
}

/** Build graph from node_count and edge list [[from, to, weight], ...] */
static NqlGraph* nql_graph_build(uint32_t n, NqlArray* edge_arr, bool directed) {
    if (n == 0 || n > NQL_GRAPH_MAX_NODES || !edge_arr) return NULL;
    uint32_t ne = edge_arr->length;
    if (ne > NQL_GRAPH_MAX_EDGES) return NULL;

    /* Count edges per node */
    uint32_t* counts = (uint32_t*)calloc(n, sizeof(uint32_t));
    if (!counts) return NULL;
    uint32_t total_edges = 0;
    for (uint32_t i = 0; i < ne; i++) {
        if (edge_arr->items[i].type != NQL_VAL_ARRAY || !edge_arr->items[i].v.array) continue;
        NqlArray* e = edge_arr->items[i].v.array;
        if (e->length < 2) continue;
        uint32_t from = (uint32_t)nql_val_as_int(&e->items[0]);
        uint32_t to = (uint32_t)nql_val_as_int(&e->items[1]);
        if (from >= n || to >= n) continue;
        counts[from]++;
        total_edges++;
        if (!directed) { counts[to]++; total_edges++; }
    }

    NqlGraph* g = (NqlGraph*)malloc(sizeof(NqlGraph));
    if (!g) { free(counts); return NULL; }
    g->node_count = n;
    g->edge_count = ne;
    g->directed = directed;
    g->adj_count = counts;
    g->adj_offset = (uint32_t*)malloc(n * sizeof(uint32_t));
    g->edges = (NqlEdge*)malloc(total_edges * sizeof(NqlEdge));
    if (!g->adj_offset || !g->edges) { nql_graph_free(g); return NULL; }

    /* Compute offsets */
    uint32_t off = 0;
    for (uint32_t i = 0; i < n; i++) { g->adj_offset[i] = off; off += counts[i]; counts[i] = 0; }

    /* Fill edges */
    for (uint32_t i = 0; i < ne; i++) {
        if (edge_arr->items[i].type != NQL_VAL_ARRAY || !edge_arr->items[i].v.array) continue;
        NqlArray* e = edge_arr->items[i].v.array;
        if (e->length < 2) continue;
        uint32_t from = (uint32_t)nql_val_as_int(&e->items[0]);
        uint32_t to = (uint32_t)nql_val_as_int(&e->items[1]);
        double w = (e->length >= 3) ? nql_val_as_float(&e->items[2]) : 1.0;
        if (from >= n || to >= n) continue;
        g->edges[g->adj_offset[from] + counts[from]++] = (NqlEdge){to, w};
        if (!directed) g->edges[g->adj_offset[to] + counts[to]++] = (NqlEdge){from, w};
    }
    return g;
}

/* ======================================================================
 * GRAPH NQL FUNCTIONS
 * ====================================================================== */

/** GRAPH(node_count, edges_array [, directed]) */
static NqlValue nql_fn_graph(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; NQL_NULL_GUARD2(args);
    uint32_t n = (uint32_t)nql_val_as_int(&args[0]);
    if (args[1].type != NQL_VAL_ARRAY || !args[1].v.array) return nql_val_null();
    bool directed = (argc >= 3) ? nql_val_is_truthy(&args[2]) : false;
    NqlGraph* g = nql_graph_build(n, args[1].v.array, directed);
    return g ? nql_val_graph(g) : nql_val_null();
}

/** GRAPH_NODE_COUNT(g) */
static NqlValue nql_fn_graph_node_count(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    return nql_val_int((int64_t)args[0].v.graph->node_count);
}

/** GRAPH_EDGE_COUNT(g) */
static NqlValue nql_fn_graph_edge_count(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    return nql_val_int((int64_t)args[0].v.graph->edge_count);
}

/** GRAPH_DEGREE(g, node) */
static NqlValue nql_fn_graph_degree(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    uint32_t node = (uint32_t)nql_val_as_int(&args[1]);
    if (node >= g->node_count) return nql_val_null();
    return nql_val_int((int64_t)g->adj_count[node]);
}

/** GRAPH_NEIGHBORS(g, node) -- array of [to, weight] pairs */
static NqlValue nql_fn_graph_neighbors(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    uint32_t node = (uint32_t)nql_val_as_int(&args[1]);
    if (node >= g->node_count) return nql_val_null();
    NqlArray* result = nql_array_alloc(g->adj_count[node]);
    if (!result) return nql_val_null();
    for (uint32_t i = 0; i < g->adj_count[node]; i++) {
        NqlEdge* e = &g->edges[g->adj_offset[node] + i];
        NqlArray* pair = nql_array_alloc(2);
        if (pair) {
            nql_array_push(pair, nql_val_int((int64_t)e->to));
            nql_array_push(pair, nql_val_float(e->weight));
            nql_array_push(result, nql_val_array(pair));
        }
    }
    return nql_val_array(result);
}

/** GRAPH_BFS(g, start) -- BFS traversal order as array */
static NqlValue nql_fn_graph_bfs(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    uint32_t start = (uint32_t)nql_val_as_int(&args[1]);
    if (start >= g->node_count) return nql_val_null();
    bool* visited = (bool*)calloc(g->node_count, sizeof(bool));
    uint32_t* queue = (uint32_t*)malloc(g->node_count * sizeof(uint32_t));
    if (!visited || !queue) { free(visited); free(queue); return nql_val_null(); }
    NqlArray* result = nql_array_alloc(g->node_count);
    if (!result) { free(visited); free(queue); return nql_val_null(); }
    uint32_t head = 0, tail = 0;
    queue[tail++] = start; visited[start] = true;
    while (head < tail) {
        uint32_t u = queue[head++];
        nql_array_push(result, nql_val_int((int64_t)u));
        for (uint32_t i = 0; i < g->adj_count[u]; i++) {
            uint32_t v = g->edges[g->adj_offset[u] + i].to;
            if (!visited[v]) { visited[v] = true; queue[tail++] = v; }
        }
    }
    free(visited); free(queue);
    return nql_val_array(result);
}

/** GRAPH_DFS(g, start) -- DFS traversal order as array */
static NqlValue nql_fn_graph_dfs(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD2(args);
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    uint32_t start = (uint32_t)nql_val_as_int(&args[1]);
    if (start >= g->node_count) return nql_val_null();
    bool* visited = (bool*)calloc(g->node_count, sizeof(bool));
    uint32_t* stack = (uint32_t*)malloc(g->node_count * sizeof(uint32_t));
    if (!visited || !stack) { free(visited); free(stack); return nql_val_null(); }
    NqlArray* result = nql_array_alloc(g->node_count);
    if (!result) { free(visited); free(stack); return nql_val_null(); }
    uint32_t top = 0;
    stack[top++] = start;
    while (top > 0) {
        uint32_t u = stack[--top];
        if (visited[u]) continue;
        visited[u] = true;
        nql_array_push(result, nql_val_int((int64_t)u));
        for (int32_t i = (int32_t)g->adj_count[u] - 1; i >= 0; i--) {
            uint32_t v = g->edges[g->adj_offset[u] + (uint32_t)i].to;
            if (!visited[v]) stack[top++] = v;
        }
    }
    free(visited); free(stack);
    return nql_val_array(result);
}

/** GRAPH_SHORTEST_PATH(g, from, to) -- Dijkstra, returns [distance, path_array] */
static NqlValue nql_fn_graph_shortest_path(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc; NQL_NULL_GUARD3(args);
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    uint32_t src = (uint32_t)nql_val_as_int(&args[1]);
    uint32_t dst = (uint32_t)nql_val_as_int(&args[2]);
    if (src >= g->node_count || dst >= g->node_count) return nql_val_null();
    uint32_t n = g->node_count;
    double* dist = (double*)malloc(n * sizeof(double));
    uint32_t* prev = (uint32_t*)malloc(n * sizeof(uint32_t));
    bool* done = (bool*)calloc(n, sizeof(bool));
    if (!dist || !prev || !done) { free(dist); free(prev); free(done); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) { dist[i] = 1e18; prev[i] = UINT32_MAX; }
    dist[src] = 0.0;
    /* Simple O(V^2) Dijkstra -- fine for NQL's graph sizes */
    for (uint32_t iter = 0; iter < n; iter++) {
        uint32_t u = UINT32_MAX; double best = 1e18;
        for (uint32_t i = 0; i < n; i++)
            if (!done[i] && dist[i] < best) { best = dist[i]; u = i; }
        if (u == UINT32_MAX) break;
        done[u] = true;
        if (u == dst) break;
        for (uint32_t i = 0; i < g->adj_count[u]; i++) {
            NqlEdge* e = &g->edges[g->adj_offset[u] + i];
            double nd = dist[u] + e->weight;
            if (nd < dist[e->to]) { dist[e->to] = nd; prev[e->to] = u; }
        }
    }
    if (dist[dst] >= 1e17) { free(dist); free(prev); free(done); return nql_val_null(); }
    /* Reconstruct path */
    NqlArray* path = nql_array_alloc(16);
    if (!path) { free(dist); free(prev); free(done); return nql_val_null(); }
    uint32_t cur = dst;
    while (cur != UINT32_MAX) { nql_array_push(path, nql_val_int((int64_t)cur)); cur = prev[cur]; }
    /* Reverse path */
    for (uint32_t i = 0; i < path->length / 2; i++) {
        NqlValue tmp = path->items[i];
        path->items[i] = path->items[path->length - 1 - i];
        path->items[path->length - 1 - i] = tmp;
    }
    double d = dist[dst];
    free(dist); free(prev); free(done);
    NqlArray* result = nql_array_alloc(2);
    if (!result) return nql_val_null();
    nql_array_push(result, nql_val_float(d));
    nql_array_push(result, nql_val_array(path));
    return nql_val_array(result);
}

/** GRAPH_CONNECTED_COMPONENTS(g) -- array of component arrays */
static NqlValue nql_fn_graph_components(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    uint32_t n = g->node_count;
    int32_t* comp = (int32_t*)malloc(n * sizeof(int32_t));
    uint32_t* queue = (uint32_t*)malloc(n * sizeof(uint32_t));
    if (!comp || !queue) { free(comp); free(queue); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) comp[i] = -1;
    NqlArray* result = nql_array_alloc(8);
    if (!result) { free(comp); free(queue); return nql_val_null(); }
    int32_t cid = 0;
    for (uint32_t s = 0; s < n; s++) {
        if (comp[s] >= 0) continue;
        NqlArray* cc = nql_array_alloc(8);
        if (!cc) break;
        uint32_t head = 0, tail = 0;
        queue[tail++] = s; comp[s] = cid;
        while (head < tail) {
            uint32_t u = queue[head++];
            nql_array_push(cc, nql_val_int((int64_t)u));
            for (uint32_t i = 0; i < g->adj_count[u]; i++) {
                uint32_t v = g->edges[g->adj_offset[u] + i].to;
                if (comp[v] < 0) { comp[v] = cid; queue[tail++] = v; }
            }
        }
        nql_array_push(result, nql_val_array(cc));
        cid++;
    }
    free(comp); free(queue);
    return nql_val_array(result);
}

/** GRAPH_HAS_CYCLE(g) -- cycle detection for directed graphs via DFS coloring */
static NqlValue nql_fn_graph_has_cycle(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    uint32_t n = g->node_count;
    /* 0=white, 1=gray, 2=black */
    uint8_t* color = (uint8_t*)calloc(n, sizeof(uint8_t));
    uint32_t* stack = (uint32_t*)malloc(n * 2 * sizeof(uint32_t)); /* node + edge_index pairs */
    if (!color || !stack) { free(color); free(stack); return nql_val_null(); }
    bool has_cycle = false;
    for (uint32_t s = 0; s < n && !has_cycle; s++) {
        if (color[s] != 0) continue;
        uint32_t top = 0;
        stack[top++] = s; stack[top++] = 0; /* node, edge_idx */
        color[s] = 1;
        while (top > 0 && !has_cycle) {
            uint32_t ei = stack[top - 1];
            uint32_t u = stack[top - 2];
            if (ei < g->adj_count[u]) {
                stack[top - 1] = ei + 1;
                uint32_t v = g->edges[g->adj_offset[u] + ei].to;
                if (color[v] == 1) { has_cycle = true; break; }
                if (color[v] == 0) { color[v] = 1; stack[top++] = v; stack[top++] = 0; }
            } else { color[u] = 2; top -= 2; }
        }
    }
    free(color); free(stack);
    return nql_val_bool(has_cycle);
}

/** GRAPH_TOPOLOGICAL_SORT(g) -- Kahn's algorithm, returns array or NULL if cycle */
static NqlValue nql_fn_graph_topo_sort(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    if (!g->directed) return nql_val_null();
    uint32_t n = g->node_count;
    uint32_t* in_deg = (uint32_t*)calloc(n, sizeof(uint32_t));
    uint32_t* queue = (uint32_t*)malloc(n * sizeof(uint32_t));
    if (!in_deg || !queue) { free(in_deg); free(queue); return nql_val_null(); }
    for (uint32_t u = 0; u < n; u++)
        for (uint32_t i = 0; i < g->adj_count[u]; i++)
            in_deg[g->edges[g->adj_offset[u] + i].to]++;
    uint32_t head = 0, tail = 0;
    for (uint32_t i = 0; i < n; i++) if (in_deg[i] == 0) queue[tail++] = i;
    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(in_deg); free(queue); return nql_val_null(); }
    uint32_t processed = 0;
    while (head < tail) {
        uint32_t u = queue[head++]; processed++;
        nql_array_push(result, nql_val_int((int64_t)u));
        for (uint32_t i = 0; i < g->adj_count[u]; i++) {
            uint32_t v = g->edges[g->adj_offset[u] + i].to;
            if (--in_deg[v] == 0) queue[tail++] = v;
        }
    }
    free(in_deg); free(queue);
    if (processed < n) return nql_val_null(); /* cycle detected */
    return nql_val_array(result);
}

/** GRAPH_MST(g) -- Prim's MST, returns [[from,to,weight],...] */
static NqlValue nql_fn_graph_mst(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    uint32_t n = g->node_count;
    if (n == 0) return nql_val_null();
    bool* in_mst = (bool*)calloc(n, sizeof(bool));
    double* key = (double*)malloc(n * sizeof(double));
    uint32_t* parent = (uint32_t*)malloc(n * sizeof(uint32_t));
    double* pw = (double*)malloc(n * sizeof(double)); /* weight of edge to parent */
    if (!in_mst || !key || !parent || !pw) { free(in_mst); free(key); free(parent); free(pw); return nql_val_null(); }
    for (uint32_t i = 0; i < n; i++) { key[i] = 1e18; parent[i] = UINT32_MAX; pw[i] = 0; }
    key[0] = 0;
    for (uint32_t iter = 0; iter < n; iter++) {
        uint32_t u = UINT32_MAX; double best = 1e18;
        for (uint32_t i = 0; i < n; i++)
            if (!in_mst[i] && key[i] < best) { best = key[i]; u = i; }
        if (u == UINT32_MAX) break;
        in_mst[u] = true;
        for (uint32_t i = 0; i < g->adj_count[u]; i++) {
            NqlEdge* e = &g->edges[g->adj_offset[u] + i];
            if (!in_mst[e->to] && e->weight < key[e->to]) {
                key[e->to] = e->weight; parent[e->to] = u; pw[e->to] = e->weight;
            }
        }
    }
    NqlArray* result = nql_array_alloc(n);
    if (!result) { free(in_mst); free(key); free(parent); free(pw); return nql_val_null(); }
    for (uint32_t i = 1; i < n; i++) {
        if (parent[i] != UINT32_MAX) {
            NqlArray* edge = nql_array_alloc(3);
            if (edge) {
                nql_array_push(edge, nql_val_int((int64_t)parent[i]));
                nql_array_push(edge, nql_val_int((int64_t)i));
                nql_array_push(edge, nql_val_float(pw[i]));
                nql_array_push(result, nql_val_array(edge));
            }
        }
    }
    free(in_mst); free(key); free(parent); free(pw);
    return nql_val_array(result);
}

/** GRAPH_IS_BIPARTITE(g) -- 2-coloring test */
static NqlValue nql_fn_graph_is_bipartite(const NqlValue* args, uint32_t argc, void* ctx) {
    (void)ctx; (void)argc;
    if (args[0].type != NQL_VAL_GRAPH || !args[0].v.graph) return nql_val_null();
    NqlGraph* g = args[0].v.graph;
    uint32_t n = g->node_count;
    int8_t* color = (int8_t*)malloc(n * sizeof(int8_t));
    uint32_t* queue = (uint32_t*)malloc(n * sizeof(uint32_t));
    if (!color || !queue) { free(color); free(queue); return nql_val_null(); }
    memset(color, -1, n);
    bool bipartite = true;
    for (uint32_t s = 0; s < n && bipartite; s++) {
        if (color[s] >= 0) continue;
        uint32_t head = 0, tail = 0;
        color[s] = 0; queue[tail++] = s;
        while (head < tail && bipartite) {
            uint32_t u = queue[head++];
            for (uint32_t i = 0; i < g->adj_count[u]; i++) {
                uint32_t v = g->edges[g->adj_offset[u] + i].to;
                if (color[v] < 0) { color[v] = 1 - color[u]; queue[tail++] = v; }
                else if (color[v] == color[u]) bipartite = false;
            }
        }
    }
    free(color); free(queue);
    return nql_val_bool(bipartite);
}

/* ======================================================================
 * REGISTRATION
 * ====================================================================== */

void nql_graph_register_functions(void) {
    nql_register_func("GRAPH",                   nql_fn_graph,               2, 3, "Create graph(n, edges [, directed])");
    nql_register_func("GRAPH_NODE_COUNT",        nql_fn_graph_node_count,    1, 1, "Number of nodes");
    nql_register_func("GRAPH_EDGE_COUNT",        nql_fn_graph_edge_count,    1, 1, "Number of edges");
    nql_register_func("GRAPH_DEGREE",            nql_fn_graph_degree,        2, 2, "Degree of node");
    nql_register_func("GRAPH_NEIGHBORS",         nql_fn_graph_neighbors,     2, 2, "Adjacent nodes as [[to,weight],...]");
    nql_register_func("GRAPH_BFS",               nql_fn_graph_bfs,           2, 2, "BFS traversal order from start node");
    nql_register_func("GRAPH_DFS",               nql_fn_graph_dfs,           2, 2, "DFS traversal order from start node");
    nql_register_func("GRAPH_SHORTEST_PATH",     nql_fn_graph_shortest_path, 3, 3, "Dijkstra: [distance, path_array]");
    nql_register_func("GRAPH_CONNECTED_COMPONENTS", nql_fn_graph_components, 1, 1, "Array of component arrays");
    nql_register_func("GRAPH_HAS_CYCLE",         nql_fn_graph_has_cycle,     1, 1, "Cycle detection (directed DFS)");
    nql_register_func("GRAPH_TOPOLOGICAL_SORT",  nql_fn_graph_topo_sort,     1, 1, "Kahn's toposort (directed only)");
    nql_register_func("GRAPH_MST",              nql_fn_graph_mst,            1, 1, "Prim's MST: [[from,to,weight],...]");
    nql_register_func("GRAPH_IS_BIPARTITE",      nql_fn_graph_is_bipartite,  1, 1, "2-coloring bipartiteness test");
}
