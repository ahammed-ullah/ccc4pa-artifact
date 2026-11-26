#ifndef FM_CON_NEIGHBORHOOD_TREE_HPP
#define FM_CON_NEIGHBORHOOD_TREE_HPP

#include "fm_con/cgf.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace fm_con {

constexpr std::uint32_t kNoTreeNode =
    std::numeric_limits<std::uint32_t>::max();

// Children are offsets from the beginning of one vertex's tree. Keeping them
// local permits all trees to share one flat allocation without requiring a
// global node index to fit in 32 bits.
struct TreeNode {
    std::uint32_t child[2] = {kNoTreeNode, kNoTreeNode};
    std::uint32_t count = 0;
};

struct NeighborhoodTrees {
    int n = 0;
    int depth = 0;
    std::uint64_t leaf_range = 0;
    std::vector<std::size_t> begin;
    std::vector<TreeNode> nodes;
};

// The first CGF pass counts the exact sparse-tree allocation. The second builds
// it. This avoids vector growth temporarily retaining both old and new copies
// of a potentially large tree array.
void build_neighborhood_trees(const CgfInfo& graph,
                              NeighborhoodTrees& trees,
                              std::vector<std::uint32_t>& right_degree);

std::uint32_t tree_count(const NeighborhoodTrees& trees,
                         int vertex,
                         std::uint32_t local_node);
std::uint32_t tree_child(const NeighborhoodTrees& trees,
                         int vertex,
                         std::uint32_t local_node,
                         int branch);

// Remove exactly U x K after Clique Stripping has found both sides. Search-time
// restrictions are handled algebraically, so no temporary tree changes need to
// be restored for vertices that later leave U.
void remove_biclique(NeighborhoodTrees& trees,
                     const std::vector<int>& U,
                     const std::vector<int>& K);

}  // namespace fm_con

#endif

