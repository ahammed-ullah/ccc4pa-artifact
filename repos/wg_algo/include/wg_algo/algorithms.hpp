#ifndef WG_ALGO_ALGORITHMS_HPP
#define WG_ALGO_ALGORITHMS_HPP

#include "wg_algo/config.hpp"
#include "wg_algo/graph.hpp"

#include <cstdint>
#include <vector>

namespace wg_algo {

#if !defined(__SIZEOF_INT128__)
#error "wg_algo needs a C++ compiler with 128-bit integer arithmetic"
#endif

int matching(const Graph& graph, MatchingVariant variant);
int bfs(const Graph& graph);
int dfs(const Graph& graph);
__uint128_t wvc(const Graph& graph,
                const std::vector<std::uint64_t>& weights);

struct MvmResult {
    __uint128_t weight = 0;
    std::uint64_t samples_completed = 0;
    std::uint64_t positive_augmentations = 0;
    bool time_limit_partial = false;
    bool all_vertices_matched = false;
};

void install_mvm_signal_handler();
std::uint64_t mvm_sample_limit(int n, double epsilon);
MvmResult mvm(const Graph& graph, const std::vector<std::uint64_t>& weights,
              std::uint64_t seed, std::uint64_t sample_limit);

}  // namespace wg_algo

#endif
