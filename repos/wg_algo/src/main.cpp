#include "wg_algo/algorithms.hpp"
#include "wg_algo/config.hpp"
#include "wg_algo/report.hpp"
#include "wg_algo/weights.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/resource.h>

using namespace std;

namespace {

using Clock = chrono::steady_clock;

double seconds(Clock::time_point start, Clock::time_point end) {
    return chrono::duration<double>(end - start).count();
}

// Reading the kernel high-water mark after the algorithm adds no timer work.
uint64_t process_peak_rss_bytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        throw runtime_error("cannot read process peak RSS");
#if defined(__linux__)
    return uint64_t(usage.ru_maxrss) * 1024;
#elif defined(__APPLE__)
    return uint64_t(usage.ru_maxrss);
#else
#error "process peak RSS units are defined only for Linux and macOS"
#endif
}

filesystem::path companion(const filesystem::path& graph, const char* extension) {
    filesystem::path path = graph;
    path.replace_extension(extension);
    return path;
}

void usage() {
    cerr << "Usage: wg_algo <matching|bfs|dfs|wvc|mvm> graph.graph "
            "[--config config.toml]\n";
}

uint64_t mvm_seed_from_environment(uint64_t configured) {
    const char* text = getenv("MVM_SEED");
    if (!text) return configured;
    const string value(text);
    if (value.empty() || !all_of(value.begin(), value.end(),
        [](unsigned char ch) { return isdigit(ch) != 0; }))
        throw runtime_error("MVM_SEED must be an unsigned 64-bit integer");
    size_t length = 0;
    try {
        const auto seed = stoull(value, &length);
        if (length == value.size()) return seed;
    } catch (const exception&) {}
    throw runtime_error("MVM_SEED must be an unsigned 64-bit integer");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 5) { usage(); return 2; }
        const string algorithm = argv[1];
        if (algorithm != "matching" && algorithm != "bfs" &&
            algorithm != "dfs" && algorithm != "wvc" &&
            algorithm != "mvm") {
            usage(); return 2;
        }
        const filesystem::path input_path = argv[2];
        if (input_path.extension() != ".graph")
            throw runtime_error("input path must end in .graph");

        wg_algo::Config config;
        if (argc == 5) {
            if (string(argv[3]) != "--config") { usage(); return 2; }
            config = wg_algo::load_config(argv[4]);
        }
        if (algorithm != "matching" && config.matching_variant_specified)
            throw runtime_error("matching_variant only applies to matching");
        if (algorithm != "wvc" && algorithm != "mvm" && config.weights_file)
            throw runtime_error("weights_file only applies to wvc and mvm");
        if (algorithm != "mvm" &&
            (config.mvm_epsilon_specified || config.mvm_seed_specified))
            throw runtime_error("mvm_epsilon and mvm_seed only apply to mvm");

        uint64_t mvm_seed = config.mvm_seed;
        if (algorithm == "mvm") {
            mvm_seed = mvm_seed_from_environment(mvm_seed);
            // The Slurm warning also needs a handler if it arrives during
            // graph loading or weight preparation.
            wg_algo::install_mvm_signal_handler();
        }

        const auto load_start = Clock::now();
        const auto graph = wg_algo::Graph::load(input_path, config.decoder);
        const uint64_t graph_bytes = graph.graph_bytes();
        const uint64_t properties_bytes = graph.properties_bytes();
        const uint64_t ef_bytes = graph.ef_bytes();
        const auto permutation_path = companion(input_path, ".perm");
        const bool has_permutation = filesystem::exists(permutation_path);
        const uint64_t perm_bytes = has_permutation
            ? filesystem::file_size(permutation_path) : 0;
        const double webgraph_load_seconds = seconds(load_start, Clock::now());

        vector<uint64_t> weights;
        optional<double> weight_preparation_seconds;
        if (algorithm == "wvc" || algorithm == "mvm") {
            const auto weight_start = Clock::now();
            weights = config.weights_file ? wg_algo::load_weights(*config.weights_file, graph.n())
                                          : wg_algo::generated_weights(graph.n());
            if (has_permutation) wg_algo::reorder_weights(weights, permutation_path);
            weight_preparation_seconds = seconds(weight_start, Clock::now());
        }

        // Claim the report name before timing, so concurrent Slurm jobs can
        // publish independent JSON files in the same directory.
        wg_algo::ReportSlot report_slot(algorithm, input_path);
        string result_key;
        string result_value;
        bool result_is_decimal_string = false;
        int integer_result = 0;
        __uint128_t weighted_result = 0;
        wg_algo::MvmResult mvm_result;
        const uint64_t mvm_limit = algorithm == "mvm"
            ? wg_algo::mvm_sample_limit(graph.n(), config.mvm_epsilon) : 0;

        const auto algorithm_start = Clock::now();
        if (algorithm == "matching") {
            integer_result = wg_algo::matching(graph, config.matching_variant);
            result_key = "matching_size";
        } else if (algorithm == "bfs") {
            integer_result = wg_algo::bfs(graph);
            result_key = "connected_components";
        } else if (algorithm == "dfs") {
            integer_result = wg_algo::dfs(graph);
            result_key = "connected_components";
        } else if (algorithm == "wvc") {
            weighted_result = wg_algo::wvc(graph, weights);
            result_key = "selected_weight_sum";
        } else {
            mvm_result = wg_algo::mvm(graph, weights, mvm_seed, mvm_limit);
            weighted_result = mvm_result.weight;
            result_key = "matched_vertex_weight_sum";
        }
        const double algorithm_seconds = seconds(algorithm_start, Clock::now());
        const uint64_t peak_rss_bytes = process_peak_rss_bytes();
        if (algorithm == "wvc" || algorithm == "mvm") {
            result_value = wg_algo::decimal(weighted_result);
            result_is_decimal_string = true;
        } else {
            result_value = to_string(integer_result);
        }

        const optional<wg_algo::MvmReport> mvm_report = algorithm == "mvm"
            ? optional<wg_algo::MvmReport>(wg_algo::MvmReport{
                  config.mvm_epsilon, mvm_seed, mvm_limit,
                  mvm_result.samples_completed, mvm_result.positive_augmentations,
                  mvm_result.time_limit_partial, mvm_result.all_vertices_matched})
            : nullopt;
        const wg_algo::RunReport report{
            algorithm, config.matching_variant, config.decoder, input_path,
            config.source_path, config.weights_file,
            graph.n(), graph.arcs(),
            graph_bytes, properties_bytes, ef_bytes, perm_bytes,
            result_key, result_value, result_is_decimal_string,
            peak_rss_bytes, webgraph_load_seconds,
            weight_preparation_seconds, algorithm_seconds, mvm_report
        };
        report_slot.publish(report);
        cout << report_slot.path().string() << '\n';
        return 0;
    } catch (const exception& error) {
        cerr << "wg_algo: " << error.what() << '\n';
        return 1;
    }
}
