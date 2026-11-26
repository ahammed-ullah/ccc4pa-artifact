#include "fm_con/neighborhood_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace std;

namespace fm_con {
namespace {

void set_tree_shape(int n, NeighborhoodTrees& trees) {
    trees.n = n;
    trees.depth = 0;
    trees.leaf_range = 1;
    while (trees.leaf_range < (uint64_t)n) {
        trees.leaf_range <<= 1U;
        ++trees.depth;
    }
}

// In sorted order, a new leaf reuses exactly the common path prefix with the
// preceding leaf. The highest differing bit therefore gives the number of new
// sparse nodes without constructing a temporary tree.
uint64_t nodes_for_neighborhood(const vector<int>& neighbors, int depth) {
    uint64_t count = 1;  // Every left vertex has a root, including an empty row.
    if (neighbors.empty()) return count;

    count += (uint64_t)depth;
    for (int i = 1; i < (int)neighbors.size(); ++i) {
        unsigned difference =
            (unsigned)neighbors[i - 1] ^ (unsigned)neighbors[i];
        int new_levels = 0;
        while (difference != 0) {
            ++new_levels;
            difference >>= 1U;
        }
        count += (uint64_t)new_levels;
    }
    return count;
}

void increment(uint32_t& count) {
    if (count == numeric_limits<uint32_t>::max())
        throw runtime_error("A neighborhood-tree count exceeds uint32");
    ++count;
}

}  // namespace

void build_neighborhood_trees(const CgfInfo& graph,
                              NeighborhoodTrees& trees,
                              vector<uint32_t>& right_degree) {
    int n = (int)graph.n;
    trees = {};
    set_tree_shape(n, trees);
    right_degree.assign(n, 0);

    // Count every sparse prefix first. Reserving exactly this amount prevents a
    // growing vector from briefly holding two copies of the dominant FM data
    // structure.
    uint64_t total_nodes = 0;
    vector<int> neighbors;
    CgfReader count_pass = open_cgf(graph);
    for (int u = 0; u < n; ++u) {
        read_cgf_neighborhood(count_pass, neighbors);
        right_degree[u] = (uint32_t)neighbors.size();
        uint64_t row_nodes = nodes_for_neighborhood(neighbors, trees.depth);
        if (total_nodes > numeric_limits<uint64_t>::max() - row_nodes)
            throw runtime_error("Neighborhood-tree node count overflows uint64");
        total_nodes += row_nodes;
    }
    finish_cgf_pass(count_pass);

    if (total_nodes > (uint64_t)numeric_limits<size_t>::max() ||
        total_nodes > (uint64_t)trees.nodes.max_size()) {
        throw runtime_error("Neighborhood trees exceed the addressable vector size");
    }

    trees.begin.resize((size_t)n + 1);
    try {
        trees.nodes.reserve((size_t)total_nodes);
    } catch (const bad_alloc&) {
        throw runtime_error(
            "Unable to allocate the sparse FM neighborhood trees (" +
            to_string(total_nodes) + " nodes)");
    }

    CgfReader build_pass = open_cgf(graph);
    for (int u = 0; u < n; ++u) {
        read_cgf_neighborhood(build_pass, neighbors);
        trees.begin[u] = trees.nodes.size();
        size_t base = trees.nodes.size();
        trees.nodes.push_back({});

        for (int v : neighbors) {
            uint32_t local_node = 0;
            increment(trees.nodes[base].count);

            for (int bit = trees.depth - 1; bit >= 0; --bit) {
                int branch = ((unsigned)v >> bit) & 1U;
                size_t global_node = base + local_node;
                uint32_t child = trees.nodes[global_node].child[branch];

                if (child == kNoTreeNode) {
                    size_t next_local = trees.nodes.size() - base;
                    if (next_local >= (size_t)kNoTreeNode) {
                        throw runtime_error(
                            "One FM neighborhood tree exceeds 32-bit local indices");
                    }
                    child = (uint32_t)next_local;
                    trees.nodes.push_back({});
                    trees.nodes[global_node].child[branch] = child;
                }

                local_node = child;
                increment(trees.nodes[base + local_node].count);
            }
        }
    }
    trees.begin[n] = trees.nodes.size();
    finish_cgf_pass(build_pass);

    if (trees.nodes.size() != (size_t)total_nodes)
        throw runtime_error("Internal sparse-tree node count is inconsistent");
}

uint32_t tree_count(const NeighborhoodTrees& trees,
                    int vertex,
                    uint32_t local_node) {
    if (local_node == kNoTreeNode) return 0;
    return trees.nodes[trees.begin[vertex] + local_node].count;
}

uint32_t tree_child(const NeighborhoodTrees& trees,
                    int vertex,
                    uint32_t local_node,
                    int branch) {
    if (local_node == kNoTreeNode) return kNoTreeNode;
    return trees.nodes[trees.begin[vertex] + local_node].child[branch];
}

void remove_biclique(NeighborhoodTrees& trees,
                     const vector<int>& U,
                     const vector<int>& K) {
    for (int u : U) {
        size_t base = trees.begin[u];
        for (int v : K) {
            uint32_t local_node = 0;
            TreeNode& root = trees.nodes[base];
            if (root.count == 0)
                throw runtime_error("FM attempted to remove a missing tree edge");
            --root.count;

            for (int bit = trees.depth - 1; bit >= 0; --bit) {
                int branch = ((unsigned)v >> bit) & 1U;
                uint32_t child = trees.nodes[base + local_node].child[branch];
                if (child == kNoTreeNode || trees.nodes[base + child].count == 0)
                    throw runtime_error("FM attempted to remove a missing tree edge");
                local_node = child;
                --trees.nodes[base + local_node].count;
            }
        }
    }
}

}  // namespace fm_con
