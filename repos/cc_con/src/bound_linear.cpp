#include "bound_internal.hpp"

#include <algorithm>
#include <vector>

using namespace std;

namespace cc_con {

// Compute core numbers and a degeneracy order by the array-bucket algorithm.
// peel lists vertices in removal order. Reversing peel gives an order in which
// first fit sees at most d already-colored neighbors.
static void core_decomposition(const vector<vector<int>>& adj,
                               vector<int>& core,
                               vector<int>& peel) {
    int n = (int)adj.size();
    core.resize(n);
    peel.resize(n);

    int maximum_degree = 0;
    for (int v = 0; v < n; ++v) {
        core[v] = (int)adj[v].size();
        maximum_degree = max(maximum_degree, core[v]);
    }

    vector<int> bin(maximum_degree + 1, 0);
    for (int v = 0; v < n; ++v) ++bin[core[v]];

    int first = 0;
    for (int degree = 0; degree <= maximum_degree; ++degree) {
        int bucket_size = bin[degree];
        bin[degree] = first;
        first += bucket_size;
    }

    vector<int> position(n);
    for (int v = 0; v < n; ++v) {
        position[v] = bin[core[v]];
        peel[position[v]] = v;
        ++bin[core[v]];
    }
    for (int degree = maximum_degree; degree >= 1; --degree)
        bin[degree] = bin[degree - 1];
    if (!bin.empty()) bin[0] = 0;

    for (int i = 0; i < n; ++i) {
        int v = peel[i];
        for (int u : adj[v]) {
            if (core[u] <= core[v]) continue;

            int degree = core[u];
            int first_position = bin[degree];
            int first_vertex = peel[first_position];
            int u_position = position[u];

            if (u != first_vertex) {
                peel[u_position] = first_vertex;
                peel[first_position] = u;
                position[u] = first_position;
                position[first_vertex] = u_position;
            }
            ++bin[degree];
            --core[u];
        }
    }
}

static int global_first_fit(const vector<vector<int>>& adj,
                            const vector<int>& peel,
                            vector<int>& color) {
    int n = (int)adj.size();
    color.assign(n, -1);
    vector<int> forbidden(n + 1, -1);
    int num_colors = 0;

    // Process the reverse peel order. color[u] >= 0 is therefore exactly the
    // test that u has already been colored.
    for (int i = n - 1; i >= 0; --i) {
        int v = peel[i];
        for (int u : adj[v]) {
            if (color[u] >= 0) forbidden[color[u]] = v;
        }

        int selected = 0;
        while (forbidden[selected] == v) ++selected;
        color[v] = selected;
        num_colors = max(num_colors, selected + 1);
    }
    return num_colors;
}

static void global_mis_bound(const vector<vector<int>>& adj,
                             vector<int>& local_size) {
    int n = (int)adj.size();
    vector<unsigned char> excluded(n, 0);
    local_size.assign(n, 0);

    // Canonical vertex order fixes the result independently of allocator or
    // traversal details. For every selected s, add its contribution to all
    // closed neighborhoods containing s. This computes |S intersect N[v]| for
    // every v in O(n+m) time without materializing those intersections.
    for (int s = 0; s < n; ++s) {
        if (excluded[s]) continue;
        excluded[s] = 1;
        ++local_size[s];
        for (int v : adj[s]) {
            excluded[v] = 1;
            ++local_size[v];
        }
    }
}

static void global_core_color_bound(const vector<vector<int>>& adj,
                                    const vector<int>& core,
                                    const vector<int>& color,
                                    int num_colors,
                                    vector<int>& max_clique_neighbors) {
    int n = (int)adj.size();
    max_clique_neighbors.assign(n, 0);

    vector<int> best(num_colors, 0), touched, values, frequency;
    for (int v = 0; v < n; ++v) {
        touched.clear();

        // In a clique containing v, the other vertices have distinct global
        // colors. If there are r such vertices, every one of them and v has
        // core number at least r. best[c] records the largest possible r
        // witnessed by a neighbor of global color c.
        for (int u : adj[v]) {
            int c = color[u];
            int value = min(core[v], core[u]);
            if (best[c] == 0) touched.push_back(c);
            best[c] = max(best[c], value);
        }

        values.clear();
        values.reserve(touched.size());
        for (int c : touched) {
            values.push_back(best[c]);
            best[c] = 0;
        }
        max_clique_neighbors[v] = threshold_number(values, frequency);
    }
}

void compute_linear_bound_data(const vector<vector<int>>& adj,
                               LinearBoundData& data) {
    vector<int> peel;
    core_decomposition(adj, data.core, peel);
    data.num_colors = global_first_fit(adj, peel, data.color);
    global_mis_bound(adj, data.global_mis);
    global_core_color_bound(adj, data.core, data.color,
                            data.num_colors, data.max_clique_neighbors);
}

}  // namespace cc_con
