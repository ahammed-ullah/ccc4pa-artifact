#ifndef WG_ALGO_CONFIG_HPP
#define WG_ALGO_CONFIG_HPP

#include <filesystem>
#include <optional>
#include <cstdint>

namespace wg_algo {

enum class MatchingVariant { ThreeHalves, Maximal };
enum class Decoder { Iterator, Bulk };

struct Config {
    MatchingVariant matching_variant = MatchingVariant::ThreeHalves;
    bool matching_variant_specified = false;
    Decoder decoder = Decoder::Bulk;
    double mvm_epsilon = 0.01;
    bool mvm_epsilon_specified = false;
    std::uint64_t mvm_seed = 1;
    bool mvm_seed_specified = false;
    std::optional<std::filesystem::path> weights_file;
    std::optional<std::filesystem::path> source_path;
};

Config load_config(const std::filesystem::path& path);

}  // namespace wg_algo

#endif
