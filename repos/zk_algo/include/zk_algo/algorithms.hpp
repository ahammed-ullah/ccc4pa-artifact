#ifndef ZK_ALGO_ALGORITHMS_HPP
#define ZK_ALGO_ALGORITHMS_HPP

#include "zk_algo/config.hpp"
#include "zk_algo/graph.hpp"

#include <cstdint>
#include <vector>

namespace zk_algo {

#if !defined(__SIZEOF_INT128__)
#error "zk_algo needs a C++ compiler with 128-bit integer arithmetic"
#endif

int matching(const Graph& graph, MatchingVariant variant);
int bfs(const Graph& graph);
int dfs(const Graph& graph);
__uint128_t wvc(const Graph& graph,
                const std::vector<std::uint64_t>& weights);

struct MvmResult {
    __uint128_t weight = 0;
    std::uint64_t sample_limit = 0;
    std::uint64_t samples_completed = 0;
    std::uint64_t positive_augmentations = 0;
    bool complete = true;
    bool all_vertices_matched = false;
};

// Pettie--Sanders Random-Match, specialized to vertex weights. Install the
// SIGUSR1 handler before loading a graph when a Slurm warning is requested.
void install_mvm_stop_signal();
std::uint64_t mvm_sample_limit(int n, double epsilon);
MvmResult mvm(const Graph& graph, const std::vector<std::uint64_t>& weights,
              std::uint64_t sample_limit, std::uint64_t seed);

}  // namespace zk_algo

#endif
