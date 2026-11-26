#ifndef FM_CON_LINEAR_INDEX_HPP
#define FM_CON_LINEAR_INDEX_HPP

#include "fm_con/cgf.hpp"

#include <cstdint>
#include <vector>

namespace fm_con {

// One Fenwick entry summarizes this many consecutive positions in one
// adjacency list. A larger block reduces memory; 256 keeps that overhead at
// one bit per eight edges while keeping each boundary scan to a small,
// constant number of 64-bit words.
constexpr std::uint32_t kLinearBlockEdges = 256;

// Immutable adjacency used by every candidate run in one automatic search.
// Neighbor IDs occupy exactly bits_per_neighbor bits each. begin[u] and
// begin[u+1] delimit the positions belonging to u. rank_begin gives each row's
// section in the per-run Fenwick array.
struct LinearAdjacency {
    int n = 0;
    int bits_per_neighbor = 0;
    std::uint64_t entries = 0;
    std::uint64_t rank_entries = 0;
    std::vector<std::uint64_t> begin;
    std::vector<std::uint64_t> rank_begin;
    std::vector<std::uint64_t> neighbor_word;
};

// Mutable state for one construction run. deleted has one bit per directed cross
// edge. deleted_rank is a separate Fenwick tree for each adjacency row, with
// one entry per kLinearBlockEdges positions. left_degree is the current root
// count of each left neighborhood.
struct LinearState {
    const LinearAdjacency* adjacency = nullptr;
    std::vector<std::uint64_t> deleted;
    std::vector<std::uint32_t> deleted_rank;
    std::vector<std::uint32_t> left_degree;
};

void build_linear_adjacency(const CgfInfo& graph,
                            LinearAdjacency& adjacency);

void initialize_linear_state(const LinearAdjacency& adjacency,
                             LinearState& state,
                             std::vector<std::uint32_t>& right_degree);

std::uint32_t linear_degree(const LinearAdjacency& adjacency, int u);
std::uint32_t linear_neighbor(const LinearAdjacency& adjacency,
                              int u,
                              std::uint32_t position);

// Return the first local adjacency position in [low,high) whose neighbor ID is
// at least value. The packed IDs remain sorted exactly as in the CGF row.
std::uint32_t linear_lower_bound(const LinearAdjacency& adjacency,
                                 int u,
                                 std::uint32_t low,
                                 std::uint32_t high,
                                 int value);

// Count currently undeleted edges in the local adjacency interval [low,high).
std::uint32_t linear_active_count(const LinearState& state,
                                  int u,
                                  std::uint32_t low,
                                  std::uint32_t high);

// Count [low,middle) and [middle,high) together so the deleted prefix at
// middle is computed only once during the innermost FM scoring loop.
void linear_active_split_counts(const LinearState& state,
                                int u,
                                std::uint32_t low,
                                std::uint32_t middle,
                                std::uint32_t high,
                                std::uint32_t& left,
                                std::uint32_t& right);

// Delete the directed cross edge (u,v) and update u's dynamic range counts.
// Clique Stripping calls this only for edges known to be present and active.
void delete_linear_edge(LinearState& state, int u, int v);

}  // namespace fm_con

#endif
