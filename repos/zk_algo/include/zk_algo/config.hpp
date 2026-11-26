#ifndef ZK_ALGO_CONFIG_HPP
#define ZK_ALGO_CONFIG_HPP

#include <filesystem>
#include <optional>
#include <cstdint>

namespace zk_algo {

enum class MatchingVariant { ThreeHalves, Maximal };

struct Config {
    MatchingVariant matching_variant = MatchingVariant::ThreeHalves;
    bool matching_variant_specified = false;
    std::optional<std::filesystem::path> weights_file;
    std::optional<std::filesystem::path> source_path;
    double mvm_epsilon = 0.01;
    std::uint64_t mvm_seed = 1;
    bool mvm_epsilon_specified = false;
    bool mvm_seed_specified = false;
};

Config load_config(const std::filesystem::path& path);

}  // namespace zk_algo

#endif
