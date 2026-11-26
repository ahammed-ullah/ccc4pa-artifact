#include "bound_internal.hpp"

#include "cc_con/sorted_vector_set.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace std;

namespace cc_con {
namespace {

void build_edge_index(const vector<vector<int>>& adj, TrussData& truss) {
    int n = (int)adj.size();
    truss.edge_offset.assign(n + 1, 0);
    truss.first_larger.resize(n);

    for (int u = 0; u < n; ++u) {
        auto first = lower_bound(adj[u].begin(), adj[u].end(), u + 1);
        truss.first_larger[u] = (int)(first - adj[u].begin());
        truss.edge_offset[u + 1] = truss.edge_offset[u] +
            (uint64_t)(adj[u].end() - first);
    }
    if (truss.edge_offset[n] >
        (uint64_t)numeric_limits<size_t>::max()) {
        throw runtime_error("The truss edge array exceeds size_t");
    }
    truss.value.resize((size_t)truss.edge_offset[n]);
}

void edge_endpoints(const vector<vector<int>>& adj,
                    const TrussData& truss,
                    uint64_t id,
                    int& u,
                    int& v) {
    auto next = upper_bound(truss.edge_offset.begin(),
                            truss.edge_offset.end(), id);
    u = (int)(next - truss.edge_offset.begin()) - 1;
    uint64_t local = id - truss.edge_offset[u];
    v = adj[u][truss.first_larger[u] + (size_t)local];
}

void decrement_support(uint64_t edge,
                       uint32_t current_support,
                       vector<uint32_t>& support,
                       vector<uint64_t>& bin,
                       vector<uint64_t>& order,
                       vector<uint64_t>& position) {
    if (support[edge] <= current_support) return;

    uint32_t old_support = support[edge];
    uint64_t old_position = position[edge];
    uint64_t first_position = bin[old_support];
    uint64_t first_edge = order[first_position];

    if (edge != first_edge) {
        order[old_position] = first_edge;
        order[first_position] = edge;
        position[edge] = first_position;
        position[first_edge] = old_position;
    }
    ++bin[old_support];
    --support[edge];
}

}  // namespace

void compute_exact_truss(const vector<vector<int>>& adj,
                         const LinearBoundData& linear,
                         bool accumulate_bounds,
                         uint64_t scale,
                         vector<uint64_t>& triangle_count,
                         TrussData& truss,
                         EdgeBoundSums& sums) {
    int n = (int)adj.size();
    build_edge_index(adj, truss);
    uint64_t m = truss.edge_offset[n];

    vector<int> common;
    uint32_t maximum_support = 0;

    // Initial support is the number of triangles containing an edge. At the
    // same time, count every triangle once for simplicial-vertex detection.
    for (int u = 0; u < n; ++u) {
        uint64_t id = truss.edge_offset[u];
        for (int slot = truss.first_larger[u];
             slot < (int)adj[u].size();
             ++slot, ++id) {
            int v = adj[u][slot];
            intersect_sorted_hybrid(adj[u], adj[v], common);
            truss.value[id] = (uint32_t)common.size();
            maximum_support = max(maximum_support, truss.value[id]);

            for (int w : common) {
                if (v >= w) continue;
                ++triangle_count[u];
                ++triangle_count[v];
                ++triangle_count[w];
            }
        }
    }

    // Counting-sort edge IDs by support. order and position are inverse arrays;
    // bin[s] identifies the first unprocessed edge with current support s.
    vector<uint64_t> bin((size_t)maximum_support + 1, 0);
    for (uint32_t support : truss.value) ++bin[support];

    uint64_t first = 0;
    for (uint32_t support = 0; support <= maximum_support; ++support) {
        uint64_t bucket_size = bin[support];
        bin[support] = first;
        first += bucket_size;
    }

    vector<uint64_t> order((size_t)m), position((size_t)m);
    for (uint64_t edge = 0; edge < m; ++edge) {
        uint32_t support = truss.value[edge];
        position[edge] = bin[support];
        order[position[edge]] = edge;
        ++bin[support];
    }
    for (uint32_t support = maximum_support; support >= 1; --support)
        bin[support] = bin[support - 1];
    if (!bin.empty()) bin[0] = 0;

    CommonColorScratch global_scratch;
    if (accumulate_bounds)
        global_scratch.best.assign(linear.num_colors, 0);

    // This is the edge analogue of array-bucket core decomposition. When edge
    // e is removed with support k, each still-present triangle lowers the
    // support of its other two edges, but never below k. Its final trussness is
    // k+2: an edge outside every triangle therefore has trussness two.
    for (uint64_t i = 0; i < m; ++i) {
        uint64_t edge = order[i];
        uint32_t support = truss.value[edge];
        int u, v;
        edge_endpoints(adj, truss, edge, u, v);
        intersect_sorted_hybrid(adj[u], adj[v], common);

        if (accumulate_bounds) {
            int global_order = min(linear.max_clique_neighbors[u],
                                   linear.max_clique_neighbors[v]) + 1;
            int common_order = common_neighborhood_global_order(
                common, u, v, linear, global_scratch);
            int truss_order = (int)support + 2;
            int best_order = min(global_order,
                                 min(common_order, truss_order));

            add_edge_reciprocal(sums.global_core_color,
                                u, v, global_order, scale);
            add_edge_reciprocal(sums.common_neighborhood_global_color,
                                u, v, common_order, scale);
            add_edge_reciprocal(sums.exact_truss,
                                u, v, truss_order, scale);
            add_edge_reciprocal(sums.combined,
                                u, v, best_order, scale);
        }

        for (int w : common) {
            uint64_t uw = truss_edge_id(adj, truss, u, w);
            uint64_t vw = truss_edge_id(adj, truss, v, w);
            if (position[uw] <= i || position[vw] <= i) continue;

            decrement_support(uw, support, truss.value,
                              bin, order, position);
            decrement_support(vw, support, truss.value,
                              bin, order, position);
        }
        truss.value[edge] = support + 2;
    }
}

}  // namespace cc_con
