#include "bound_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace std;

namespace cc_con {

// Extend an optional independent seed to a maximal independent set in G[N[v]].
// candidates are considered in canonical vertex order. For a selected vertex
// u, only N(u) intersect N[v] must be excluded; the hybrid choice avoids a full
// scan of a high-degree N(u) when N[v] is much smaller.
static int extend_local_mis(const vector<vector<int>>& adj,
                            int v,
                            const vector<int>& seed,
                            vector<int>& neighborhood_mark,
                            vector<int>& excluded_mark,
                            int mark,
                            vector<int>& selected) {
    const auto& N_v = adj[v];
    for (int u : N_v) neighborhood_mark[u] = mark;
    neighborhood_mark[v] = mark;

    selected.clear();
    for (int u : seed) {
        if (excluded_mark[u] == mark) continue;
        selected.push_back(u);
        excluded_mark[u] = mark;

        const auto& N_u = adj[u];
        if (N_u.size() <= N_v.size() * 8ULL) {
            for (int w : N_u) {
                if (neighborhood_mark[w] == mark) excluded_mark[w] = mark;
            }
        } else {
            for (int w : N_v) {
                if (binary_search(N_u.begin(), N_u.end(), w))
                    excluded_mark[w] = mark;
            }
            if (binary_search(N_u.begin(), N_u.end(), v))
                excluded_mark[v] = mark;
        }
    }

    // Process N(v) before v. Since the graph has no isolated vertices, at least
    // one neighbor is selected unless the seed already excludes all of them;
    // v is then excluded by that selected neighbor. This is equivalent to the
    // useful MIS in G[N(v)] while retaining the closed-neighborhood statement
    // needed by the simplicial seed.
    for (int u : N_v) {
        if (excluded_mark[u] == mark) continue;
        selected.push_back(u);
        excluded_mark[u] = mark;

        const auto& N_u = adj[u];
        if (N_u.size() <= N_v.size() * 8ULL) {
            for (int w : N_u) {
                if (neighborhood_mark[w] == mark) excluded_mark[w] = mark;
            }
        } else {
            for (int w : N_v) {
                if (binary_search(N_u.begin(), N_u.end(), w))
                    excluded_mark[w] = mark;
            }
            if (binary_search(N_u.begin(), N_u.end(), v))
                excluded_mark[v] = mark;
        }
    }
    if (excluded_mark[v] != mark) selected.push_back(v);
    return (int)selected.size();
}

void compute_local_mis_bounds(const vector<vector<int>>& adj,
                              const vector<uint64_t>& triangle_count,
                              vector<int>& local_mis,
                              vector<int>& simplicial_mis) {
    int n = (int)adj.size();
    vector<unsigned char> simplicial(n, 0), representative(n, 0);

    for (int v = 0; v < n; ++v) {
        uint64_t degree = (uint64_t)adj[v].size();
        simplicial[v] =
            triangle_count[v] == degree * (degree - 1) / 2;
    }

    // Adjacent simplicial vertices have the same closed neighborhood. Marking
    // all simplicial neighbors of a chosen representative therefore selects
    // exactly one vertex from each distinct simplicial closed-neighborhood
    // class. Representatives from different classes are nonadjacent.
    vector<unsigned char> represented(n, 0);
    for (int s = 0; s < n; ++s) {
        if (!simplicial[s] || represented[s]) continue;
        representative[s] = 1;
        represented[s] = 1;
        for (int u : adj[s]) {
            if (simplicial[u]) represented[u] = 1;
        }
    }

    local_mis.resize(n);
    simplicial_mis.resize(n);
    vector<int> neighborhood_mark(n, 0), excluded_mark(n, 0);
    vector<int> seed, selected;
    int mark = 0;

    for (int v = 0; v < n; ++v) {
        // Two marks are consumed for each v. Resetting the arrays on integer
        // exhaustion keeps the marker scheme defined even for unusual builds
        // with a very small int type.
        if (mark > 2147483644) {
            fill(neighborhood_mark.begin(), neighborhood_mark.end(), 0);
            fill(excluded_mark.begin(), excluded_mark.end(), 0);
            mark = 0;
        }

        seed.clear();
        local_mis[v] = extend_local_mis(
            adj, v, seed, neighborhood_mark, excluded_mark, ++mark, selected);

        seed.clear();
        if (representative[v]) seed.push_back(v);
        for (int u : adj[v]) {
            if (representative[u]) seed.push_back(u);
        }
        simplicial_mis[v] = extend_local_mis(
            adj, v, seed, neighborhood_mark, excluded_mark, ++mark, selected);
    }
}

}  // namespace cc_con
