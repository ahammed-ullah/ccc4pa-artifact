#include "bound_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace std;

namespace cc_con {

uint64_t reciprocal_scale(const vector<vector<int>>& adj) {
    int maximum_degree = 0;
    for (const auto& neighbors : adj)
        maximum_degree = max(maximum_degree, (int)neighbors.size());

    // CGF graphs have no isolated vertices, so a nonempty graph has positive
    // maximum degree. Keeping this case defined also makes small unit fixtures
    // harmless if they call the bound code directly.
    if (maximum_degree == 0) return uint64_t{1} << 63;

    int degree_bits = 0;
    int value = maximum_degree - 1;
    while (value > 0) {
        ++degree_bits;
        value >>= 1;
    }

    // maximum_degree*2^(63-degree_bits) <= 2^63. Hence every per-vertex
    // accumulator remains below 2^64 even when every incident edge has
    // denominator one. On the current datasets this uses substantially more
    // than 32 fractional bits, while retaining a proof that no addition can
    // overflow.
    return uint64_t{1} << (63 - degree_bits);
}

uint64_t reciprocal_weight(uint64_t scale, int denominator) {
    return scale / (uint64_t)denominator;
}

uint64_t rounded_reciprocal_total(const vector<uint64_t>& sums,
                                  uint64_t scale) {
    uint64_t total = 0;
    for (uint64_t sum : sums) {
        uint64_t bound = sum / scale;
        if (sum % scale != 0) ++bound;
        total += bound;
    }
    return total;
}

uint64_t integer_total(const vector<int>& values) {
    uint64_t total = 0;
    for (int value : values) total += (uint64_t)value;
    return total;
}

int threshold_number(const vector<int>& values, vector<int>& frequency) {
    int k = (int)values.size();
    frequency.assign(k + 1, 0);
    for (int value : values) ++frequency[min(value, k)];

    int at_least = 0;
    for (int r = k; r >= 1; --r) {
        at_least += frequency[r];
        if (at_least >= r) return r;
    }
    return 0;
}

void add_edge_reciprocal(vector<uint64_t>& sums,
                         int u,
                         int v,
                         int clique_order,
                         uint64_t scale) {
    uint64_t weight = reciprocal_weight(scale, clique_order - 1);
    sums[u] += weight;
    sums[v] += weight;
}

int common_neighborhood_global_order(const vector<int>& common,
                                     int u,
                                     int v,
                                     const LinearBoundData& linear,
                                     CommonColorScratch& scratch) {
    scratch.touched.clear();
    int endpoint_core = min(linear.core[u], linear.core[v]);

    // A q-clique containing {u,v} has q-2 common neighbors. Those vertices
    // have distinct global colors and core number at least q-1. Subtracting one
    // turns each color's best core value into an upper bound for q-2.
    for (int w : common) {
        int c = linear.color[w];
        int value = min(endpoint_core, linear.core[w]) - 1;
        if (scratch.best[c] == 0) scratch.touched.push_back(c);
        scratch.best[c] = max(scratch.best[c], value);
    }

    scratch.values.clear();
    scratch.values.reserve(scratch.touched.size());
    for (int c : scratch.touched) {
        scratch.values.push_back(scratch.best[c]);
        scratch.best[c] = 0;
    }
    return threshold_number(scratch.values, scratch.frequency) + 2;
}

uint64_t truss_edge_id(const vector<vector<int>>& adj,
                       const TrussData& truss,
                       int u,
                       int v) {
    if (u > v) swap(u, v);
    auto begin = adj[u].begin() + truss.first_larger[u];
    auto position = lower_bound(begin, adj[u].end(), v);
    return truss.edge_offset[u] + (uint64_t)(position - begin);
}

}  // namespace cc_con
