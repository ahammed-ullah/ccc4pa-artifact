#include "bound_internal.hpp"

#include "cc_con/sorted_vector_set.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace std;

namespace cc_con {
namespace {

struct LocalColorScratch {
    vector<int> common_mark;
    vector<int> color;
    vector<int> forbidden;
    vector<int> best_core;
    vector<int> best_truss;
    vector<int> core_values;
    vector<int> truss_values;
    vector<int> frequency;
    int mark = 0;
};

void local_color_orders(const vector<vector<int>>& adj,
                        const vector<int>& common,
                        int u,
                        int v,
                        const LinearBoundData& linear,
                        const TrussData* truss,
                        int& local_order,
                        int& truss_local_order,
                        LocalColorScratch& scratch) {
    if (++scratch.mark == 0x7fffffff) {
        fill(scratch.common_mark.begin(), scratch.common_mark.end(), 0);
        scratch.mark = 1;
    }

    for (int w : common) {
        scratch.common_mark[w] = scratch.mark;
        scratch.color[w] = -1;
    }
    scratch.forbidden.assign(common.size() + 1, 0);

    int num_colors = 0;
    for (int i = 0; i < (int)common.size(); ++i) {
        int w = common[i];
        const auto& N_w = adj[w];

        // Inspect whichever side is cheaper: scan N(w) and test membership in
        // the current common neighborhood, or search the already-colored prefix
        // of the common neighborhood in N(w). Both choices leave common sorted.
        if (N_w.size() <= (size_t)i * 8) {
            for (int x : N_w) {
                if (scratch.common_mark[x] != scratch.mark ||
                    scratch.color[x] < 0) continue;
                scratch.forbidden[scratch.color[x]] = i + 1;
            }
        } else {
            for (int j = 0; j < i; ++j) {
                int x = common[j];
                if (binary_search(N_w.begin(), N_w.end(), x))
                    scratch.forbidden[scratch.color[x]] = i + 1;
            }
        }

        int selected = 0;
        while (scratch.forbidden[selected] == i + 1) ++selected;
        scratch.color[w] = selected;
        num_colors = max(num_colors, selected + 1);
    }

    scratch.best_core.assign(num_colors, 0);
    if (truss) scratch.best_truss.assign(num_colors, 0);
    int endpoint_core = min(linear.core[u], linear.core[v]);

    for (int w : common) {
        int c = scratch.color[w];
        scratch.best_core[c] = max(
            scratch.best_core[c], min(endpoint_core, linear.core[w]) - 1);

        if (truss) {
            int spoke_bound = (int)min(
                truss->value[truss_edge_id(adj, *truss, u, w)],
                truss->value[truss_edge_id(adj, *truss, v, w)]) - 2;
            scratch.best_truss[c] = max(scratch.best_truss[c], spoke_bound);
        }
    }

    scratch.core_values.assign(scratch.best_core.begin(),
                               scratch.best_core.end());
    local_order = threshold_number(
        scratch.core_values, scratch.frequency) + 2;

    if (truss) {
        scratch.truss_values.assign(scratch.best_truss.begin(),
                                    scratch.best_truss.end());
        truss_local_order = threshold_number(
            scratch.truss_values, scratch.frequency) + 2;
    }
}

}  // namespace

void accumulate_linear_edge_bounds(const vector<vector<int>>& adj,
                                   const LinearBoundData& linear,
                                   uint64_t scale,
                                   EdgeBoundSums& sums) {
    int n = (int)adj.size();
    for (int u = 0; u < n; ++u) {
        for (int v : adj[u]) {
            if (u >= v) continue;
            int order = min(linear.max_clique_neighbors[u],
                            linear.max_clique_neighbors[v]) + 1;
            add_edge_reciprocal(sums.global_core_color, u, v, order, scale);
            add_edge_reciprocal(sums.combined, u, v, order, scale);
        }
    }
}

void accumulate_common_neighborhood_bounds(
    const vector<vector<int>>& adj,
    const LinearBoundData& linear,
    const TrussData* truss,
    bool compute_local_color,
    bool count_triangles,
    uint64_t scale,
    vector<uint64_t>& triangle_count,
    EdgeBoundSums& sums) {
    int n = (int)adj.size();
    vector<int> common;
    CommonColorScratch global_scratch;
    global_scratch.best.assign(linear.num_colors, 0);
    LocalColorScratch local_scratch;
    if (compute_local_color) {
        local_scratch.common_mark.assign(n, 0);
        local_scratch.color.assign(n, -1);
    }

    uint64_t id = 0;
    for (int u = 0; u < n; ++u) {
        auto first_larger = lower_bound(adj[u].begin(), adj[u].end(), u + 1);
        for (auto edge = first_larger; edge != adj[u].end(); ++edge, ++id) {
            int v = *edge;
            intersect_sorted_hybrid(adj[u], adj[v], common);

            if (count_triangles) {
                // The condition u < v < w selects one of the three edge-based
                // encounters of each triangle.
                for (int w : common) {
                    if (v >= w) continue;
                    ++triangle_count[u];
                    ++triangle_count[v];
                    ++triangle_count[w];
                }
            }

            int global_order = min(linear.max_clique_neighbors[u],
                                   linear.max_clique_neighbors[v]) + 1;
            int common_global_order = common_neighborhood_global_order(
                common, u, v, linear, global_scratch);
            int best_order = min(global_order, common_global_order);

            add_edge_reciprocal(sums.global_core_color,
                                u, v, global_order, scale);
            add_edge_reciprocal(sums.common_neighborhood_global_color,
                                u, v, common_global_order, scale);

            if (truss) {
                int truss_order = (int)truss->value[id];
                add_edge_reciprocal(sums.exact_truss,
                                    u, v, truss_order, scale);
                best_order = min(best_order, truss_order);
            }

            if (compute_local_color) {
                int local_order = 2;
                int truss_local_order = 2;
                local_color_orders(adj, common, u, v, linear, truss,
                                   local_order, truss_local_order,
                                   local_scratch);
                add_edge_reciprocal(sums.common_neighborhood_local_color,
                                    u, v, local_order, scale);
                best_order = min(best_order, local_order);

                if (truss) {
                    add_edge_reciprocal(sums.truss_filtered_local_color,
                                        u, v, truss_local_order, scale);
                    best_order = min(best_order, truss_local_order);
                }
            }
            add_edge_reciprocal(sums.combined, u, v, best_order, scale);
        }
    }
}

}  // namespace cc_con
