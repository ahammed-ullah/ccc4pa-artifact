#ifndef CC_CON_BOUND_INTERNAL_HPP
#define CC_CON_BOUND_INTERNAL_HPP

#include "cc_con/bound.hpp"

#include <cstdint>
#include <vector>

namespace cc_con {

struct LinearBoundData {
    // core[v] is the core number of v. color[v] is its global first-fit color.
    // max_clique_neighbors[v] bounds |C|-1 for every clique C containing v.
    std::vector<int> core;
    std::vector<int> color;
    std::vector<int> max_clique_neighbors;

    // global_mis[v] = |S intersect N[v]| for the one global MIS S.
    std::vector<int> global_mis;
    int num_colors = 0;
};

struct EdgeBoundSums {
    std::vector<std::uint64_t> global_core_color;
    std::vector<std::uint64_t> common_neighborhood_global_color;
    std::vector<std::uint64_t> exact_truss;
    std::vector<std::uint64_t> common_neighborhood_local_color;
    std::vector<std::uint64_t> truss_filtered_local_color;
    std::vector<std::uint64_t> combined;
};

struct TrussData {
    // For u < v, the edge id of {u,v} is edge_offset[u] plus v's position
    // among the entries of adj[u] that are larger than u.
    std::vector<std::uint64_t> edge_offset;
    std::vector<int> first_larger;
    std::vector<std::uint32_t> value;
};

struct CommonColorScratch {
    std::vector<int> best;
    std::vector<int> touched;
    std::vector<int> values;
    std::vector<int> frequency;
};

void compute_linear_bound_data(const std::vector<std::vector<int>>& adj,
                               LinearBoundData& data);

void compute_local_mis_bounds(const std::vector<std::vector<int>>& adj,
                              const std::vector<std::uint64_t>& triangle_count,
                              std::vector<int>& local_mis,
                              std::vector<int>& simplicial_mis);

// The scale is a power of two chosen so degree(v)*scale cannot overflow a
// uint64_t. floor(scale/denominator) is a conservative representation of the
// reciprocal.
std::uint64_t reciprocal_scale(
    const std::vector<std::vector<int>>& adj);
std::uint64_t reciprocal_weight(std::uint64_t scale, int denominator);
std::uint64_t rounded_reciprocal_total(
    const std::vector<std::uint64_t>& sums,
    std::uint64_t scale);

std::uint64_t integer_total(const std::vector<int>& values);

// Return max {r : at least r entries of values are at least r}. All supplied
// values are nonnegative. frequency is reusable scratch storage.
int threshold_number(const std::vector<int>& values,
                     std::vector<int>& frequency);

void add_edge_reciprocal(std::vector<std::uint64_t>& sums,
                         int u,
                         int v,
                         int clique_order,
                         std::uint64_t scale);

int common_neighborhood_global_order(
    const std::vector<int>& common,
    int u,
    int v,
    const LinearBoundData& linear,
    CommonColorScratch& scratch);

std::uint64_t truss_edge_id(const std::vector<std::vector<int>>& adj,
                            const TrussData& truss,
                            int u,
                            int v);

void accumulate_linear_edge_bounds(
    const std::vector<std::vector<int>>& adj,
    const LinearBoundData& linear,
    std::uint64_t scale,
    EdgeBoundSums& sums);

// Compute the common-neighborhood global-color bound, and optionally the local
// coloring bound. When truss is non-null, exact-truss and truss-filtered local
// bounds are included as well. triangle_count is updated exactly once per
// triangle when count_triangles is true.
void accumulate_common_neighborhood_bounds(
    const std::vector<std::vector<int>>& adj,
    const LinearBoundData& linear,
    const TrussData* truss,
    bool compute_local_color,
    bool count_triangles,
    std::uint64_t scale,
    std::vector<std::uint64_t>& triangle_count,
    EdgeBoundSums& sums);

// Compute exact edge trussness. If accumulate_bounds is true, the peeling
// intersection also computes all edge-based bounds needed by local-mis
// --truss, avoiding a third common-neighborhood pass.
void compute_exact_truss(
    const std::vector<std::vector<int>>& adj,
    const LinearBoundData& linear,
    bool accumulate_bounds,
    std::uint64_t scale,
    std::vector<std::uint64_t>& triangle_count,
    TrussData& truss,
    EdgeBoundSums& sums);

}  // namespace cc_con

#endif
