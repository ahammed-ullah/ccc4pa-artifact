#include "wg_algo/algorithms.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace std;

namespace wg_algo {

__uint128_t wvc(const Graph& graph, const vector<uint64_t>& weights) {
    // Edge form of Figure 16's primal-dual update. Vertex weights are in
    // WebGraph order, but represent the same canonical weighted instance.
    vector<uint64_t> residual = weights;
    vector<int> row;
    __uint128_t cover_weight = 0;

    for (int u = 0; u < graph.n(); ++u) {
        if (residual[u] == 0) continue;
        graph.decode_row(u, row);
        for (int v : row) {
            if (v <= u || residual[v] == 0) continue;
            uint64_t delta = min(residual[u], residual[v]);
            residual[u] -= delta;
            residual[v] -= delta;
            if (residual[u] == 0) cover_weight += weights[u];
            if (residual[v] == 0) cover_weight += weights[v];
            if (residual[u] == 0) break;
        }
    }
    return cover_weight;
}

}  // namespace wg_algo
