#pragma once

#include <cstdint>
#include <vector>

namespace wg_algo {

// Only the graph operations used by mvm.cpp are needed by this test.
struct Graph {
    std::vector<std::vector<int>> rows;
    int n() const { return static_cast<int>(rows.size()); }
    void decode_row(int vertex, std::vector<int>& row) const { row = rows[vertex]; }
};

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
