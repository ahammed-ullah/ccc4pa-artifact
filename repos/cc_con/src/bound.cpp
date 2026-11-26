#include "cc_con/bound.hpp"

#include "bound_internal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace cc_con {
namespace {

using Clock = chrono::steady_clock;

double elapsed_seconds(Clock::time_point start) {
    return chrono::duration<double>(Clock::now() - start).count();
}

void allocate_edge_sums(int n,
                        const BoundOptions& options,
                        EdgeBoundSums& sums) {
    sums.global_core_color.assign(n, 0);
    sums.combined.assign(n, 0);

    if (options.method != BoundMethod::Linear)
        sums.common_neighborhood_global_color.assign(n, 0);
    if (options.exact_truss)
        sums.exact_truss.assign(n, 0);
    if (options.method == BoundMethod::LocalColor)
        sums.common_neighborhood_local_color.assign(n, 0);
    if (options.method == BoundMethod::LocalColor && options.exact_truss)
        sums.truss_filtered_local_color.assign(n, 0);
}

uint64_t combined_total(const vector<int>& global_mis,
                        const vector<int>& local_mis,
                        const vector<int>& simplicial_mis,
                        const vector<uint64_t>& edge_sums,
                        uint64_t scale) {
    uint64_t total = 0;
    for (int v = 0; v < (int)global_mis.size(); ++v) {
        uint64_t edge_bound = edge_sums[v] / scale;
        if (edge_sums[v] % scale != 0) ++edge_bound;

        uint64_t best = (uint64_t)global_mis[v];
        if (!local_mis.empty())
            best = max(best, (uint64_t)local_mis[v]);
        if (!simplicial_mis.empty())
            best = max(best, (uint64_t)simplicial_mis[v]);
        best = max(best, edge_bound);
        total += best;
    }
    return total;
}

}  // namespace

const char* bound_method_name(BoundMethod method) {
    switch (method) {
        case BoundMethod::Linear: return "linear";
        case BoundMethod::LocalMis: return "local-mis";
        case BoundMethod::LocalColor: return "local-color";
    }
    return "unknown";
}

void compute_clique_cover_lower_bounds(const vector<vector<int>>& adj,
                                       const BoundOptions& options,
                                       BoundResult& result) {
    if (options.method == BoundMethod::Linear && options.exact_truss)
        throw runtime_error("--truss is not available with the linear method");

    result = {};
    Clock::time_point total_start = Clock::now();

    LinearBoundData linear;
    Clock::time_point phase_start = Clock::now();
    compute_linear_bound_data(adj, linear);
    result.timings.linear_seconds = elapsed_seconds(phase_start);

    int n = (int)adj.size();
    uint64_t scale = reciprocal_scale(adj);
    EdgeBoundSums edge_sums;
    allocate_edge_sums(n, options, edge_sums);
    vector<uint64_t> triangle_count;
    vector<int> local_mis, simplicial_mis;

    if (options.method == BoundMethod::Linear) {
        phase_start = Clock::now();
        accumulate_linear_edge_bounds(adj, linear, scale, edge_sums);
        result.timings.linear_seconds += elapsed_seconds(phase_start);
    } else {
        triangle_count.assign(n, 0);

        if (options.exact_truss) {
            TrussData truss;
            phase_start = Clock::now();
            bool integrate_edge_bounds =
                options.method == BoundMethod::LocalMis;
            compute_exact_truss(adj, linear, integrate_edge_bounds,
                                scale, triangle_count, truss, edge_sums);
            result.timings.truss_seconds = elapsed_seconds(phase_start);

            if (options.method == BoundMethod::LocalColor) {
                phase_start = Clock::now();
                accumulate_common_neighborhood_bounds(
                    adj, linear, &truss, true, false, scale,
                    triangle_count, edge_sums);
                result.timings.local_color_seconds =
                    elapsed_seconds(phase_start);
            }
        } else {
            phase_start = Clock::now();
            bool local_color = options.method == BoundMethod::LocalColor;
            accumulate_common_neighborhood_bounds(
                adj, linear, nullptr, local_color, true, scale,
                triangle_count, edge_sums);
            if (local_color)
                result.timings.local_color_seconds =
                    elapsed_seconds(phase_start);
            else
                result.timings.common_neighborhood_seconds =
                    elapsed_seconds(phase_start);
        }

        phase_start = Clock::now();
        compute_local_mis_bounds(adj, triangle_count,
                                 local_mis, simplicial_mis);
        result.timings.local_mis_seconds = elapsed_seconds(phase_start);
    }

    result.lower_bounds["global_mis"] = integer_total(linear.global_mis);
    result.lower_bounds["global_core_color"] = rounded_reciprocal_total(
        edge_sums.global_core_color, scale);

    if (options.method != BoundMethod::Linear) {
        result.lower_bounds["local_mis"] = integer_total(local_mis);
        result.lower_bounds["simplicial_dominating_local_mis"] =
            integer_total(simplicial_mis);
        result.lower_bounds["common_neighborhood_global_color"] =
            rounded_reciprocal_total(
                edge_sums.common_neighborhood_global_color, scale);
    }
    if (options.exact_truss) {
        result.lower_bounds["exact_truss"] = rounded_reciprocal_total(
            edge_sums.exact_truss, scale);
    }
    if (options.method == BoundMethod::LocalColor) {
        result.lower_bounds["common_neighborhood_local_color"] =
            rounded_reciprocal_total(
                edge_sums.common_neighborhood_local_color, scale);
    }
    if (options.method == BoundMethod::LocalColor && options.exact_truss) {
        result.lower_bounds["truss_filtered_local_color"] =
            rounded_reciprocal_total(
                edge_sums.truss_filtered_local_color, scale);
    }

    result.lower_bounds["combined_best"] = combined_total(
        linear.global_mis, local_mis, simplicial_mis,
        edge_sums.combined, scale);
    result.timings.total_seconds = elapsed_seconds(total_start);
}

}  // namespace cc_con
