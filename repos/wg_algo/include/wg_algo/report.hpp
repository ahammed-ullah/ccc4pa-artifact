#ifndef WG_ALGO_REPORT_HPP
#define WG_ALGO_REPORT_HPP

#include "wg_algo/config.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace wg_algo {

struct MvmReport {
    double epsilon = 0.01;
    std::uint64_t seed = 1;
    std::uint64_t sample_limit = 0;
    std::uint64_t samples_completed = 0;
    std::uint64_t positive_augmentations = 0;
    bool time_limit_partial = false;
    bool all_vertices_matched = false;
};

struct RunReport {
    std::string algorithm;
    MatchingVariant matching_variant;
    Decoder decoder;
    std::filesystem::path graph_path;
    std::optional<std::filesystem::path> config_path;
    std::optional<std::filesystem::path> weights_file;
    int n = 0;
    std::uint64_t arcs = 0;
    std::uint64_t graph_bytes = 0;
    std::uint64_t properties_bytes = 0;
    std::uint64_t ef_bytes = 0;
    std::uint64_t perm_bytes = 0;
    std::string result_key;
    std::string result_value;
    bool result_is_decimal_string = false;
    std::uint64_t process_peak_rss_bytes = 0;
    double webgraph_load_seconds = 0;
    std::optional<double> weight_preparation_seconds;
    double algorithm_seconds = 0;
    std::optional<MvmReport> mvm;
};

class ReportSlot {
public:
    ReportSlot(const std::string& algorithm, const std::filesystem::path& graph);
    ReportSlot(const ReportSlot&) = delete;
    ReportSlot& operator=(const ReportSlot&) = delete;
    ~ReportSlot();

    const std::filesystem::path& path() const { return report_path_; }
    void publish(const RunReport& report);

private:
    std::filesystem::path report_path_;
    std::filesystem::path claim_path_;
    bool published_ = false;
};

}  // namespace wg_algo

#endif
