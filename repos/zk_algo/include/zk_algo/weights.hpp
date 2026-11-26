#ifndef ZK_ALGO_WEIGHTS_HPP
#define ZK_ALGO_WEIGHTS_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zk_algo {

constexpr std::uint32_t default_weight_seed = 19u;

// These use the same vertex order and generator as cp_algo.
std::vector<std::uint64_t> generated_weights(int n);
std::vector<std::uint64_t> load_weights(const std::filesystem::path& path, int n);

// .perm maps each encoded vertex to its original CCF vertex. Reorder weights
// in place before the algorithm timer, using one 32-bit entry per vertex.
void reorder_weights(std::vector<std::uint64_t>& weights,
                     const std::filesystem::path& permutation_path);

std::string decimal(__uint128_t value);

}  // namespace zk_algo

#endif
