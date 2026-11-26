#include "zk_algo/algorithms.hpp"
#include "zk_algo/config.hpp"
#include "zk_algo/report.hpp"
#include "zk_algo/weights.hpp"

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
    cerr << "Usage: zk_algo <matching|bfs|dfs|wvc|mvm> graph.zk "
            "[--config config.toml]\n";
}

uint64_t mvm_seed_from_env(uint64_t fallback) {
    const char* value = getenv("MVM_SEED");
    if (!value) return fallback;
    const string raw(value);
    if (raw.empty() || !all_of(raw.begin(), raw.end(), [](unsigned char ch) {
        return isdigit(ch) != 0;
    })) throw runtime_error("MVM_SEED must be a uint64 integer");
    size_t consumed = 0;
    uint64_t seed;
    try { seed = stoull(raw, &consumed); }
    catch (const exception&) { throw runtime_error("MVM_SEED must be a uint64 integer"); }
    if (consumed != raw.size()) throw runtime_error("MVM_SEED must be a uint64 integer");
    return seed;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 5) { usage(); return 2; }
        const string algorithm = argv[1];
        if (algorithm != "matching" && algorithm != "bfs" &&
            algorithm != "dfs" && algorithm != "wvc" && algorithm != "mvm") {
            usage(); return 2;
        }
        const filesystem::path input_path = argv[2];
        if (input_path.extension() != ".zk")
            throw runtime_error("input path must end in .zk");

        zk_algo::Config config;
        if (argc == 5) {
            if (string(argv[3]) != "--config") { usage(); return 2; }
            config = zk_algo::load_config(argv[4]);
        }
        if (algorithm != "matching" && config.matching_variant_specified)
            throw runtime_error("matching_variant only applies to matching");
        if (algorithm != "wvc" && algorithm != "mvm" && config.weights_file)
            throw runtime_error("weights_file only applies to wvc or mvm");
        if (algorithm != "mvm" &&
            (config.mvm_epsilon_specified || config.mvm_seed_specified))
            throw runtime_error("mvm_epsilon and mvm_seed only apply to mvm");
        if (algorithm == "mvm") {
            config.mvm_seed = mvm_seed_from_env(config.mvm_seed);
            // The warning can also arrive during graph or weight preparation.
            zk_algo::install_mvm_stop_signal();
        }

        const auto load_start = Clock::now();
        const auto graph = zk_algo::Graph::load(input_path.string());
        const uint64_t graph_bytes = graph.graph_bytes();
        const uint64_t index_bytes = graph.index_bytes();
        const auto permutation_path = companion(input_path, ".perm");
        const bool has_permutation = filesystem::exists(permutation_path);
        const uint64_t perm_bytes = has_permutation
            ? filesystem::file_size(permutation_path) : 0;
        const double zuckerli_load_seconds = seconds(load_start, Clock::now());

        vector<uint64_t> weights;
        optional<double> weight_preparation_seconds;
        if (algorithm == "wvc" || algorithm == "mvm") {
            const auto weight_start = Clock::now();
            weights = config.weights_file ? zk_algo::load_weights(*config.weights_file, graph.n())
                                          : zk_algo::generated_weights(graph.n());
            if (has_permutation) zk_algo::reorder_weights(weights, permutation_path);
            weight_preparation_seconds = seconds(weight_start, Clock::now());
        }

        // Claim the report name before timing, so concurrent Slurm jobs can
        // publish independent JSON files in the same directory.
        zk_algo::ReportSlot report_slot(algorithm, input_path);
        const uint64_t mvm_samples = algorithm == "mvm"
            ? zk_algo::mvm_sample_limit(graph.n(), config.mvm_epsilon) : 0;
        string result_key;
        string result_value;
        bool result_is_decimal_string = false;
        int integer_result = 0;
        __uint128_t weighted_result = 0;
        zk_algo::MvmResult mvm_result;

        const auto algorithm_start = Clock::now();
        if (algorithm == "matching") {
            integer_result = zk_algo::matching(graph, config.matching_variant);
            result_key = "matching_size";
        } else if (algorithm == "bfs") {
            integer_result = zk_algo::bfs(graph);
            result_key = "connected_components";
        } else if (algorithm == "dfs") {
            integer_result = zk_algo::dfs(graph);
            result_key = "connected_components";
        } else if (algorithm == "mvm") {
            mvm_result = zk_algo::mvm(graph, weights, mvm_samples,
                                      config.mvm_seed);
            weighted_result = mvm_result.weight;
            result_key = "matched_vertex_weight_sum";
        } else {
            weighted_result = zk_algo::wvc(graph, weights);
            result_key = "selected_weight_sum";
        }
        const double algorithm_seconds = seconds(algorithm_start, Clock::now());
        const uint64_t peak_rss_bytes = process_peak_rss_bytes();
        if (algorithm == "wvc" || algorithm == "mvm") {
            result_value = zk_algo::decimal(weighted_result);
            result_is_decimal_string = true;
        } else {
            result_value = to_string(integer_result);
        }

        zk_algo::RunReport report{
            algorithm, config.matching_variant, input_path,
            config.source_path, config.weights_file,
            graph.n(), graph.arcs(),
            graph_bytes, index_bytes, perm_bytes,
            result_key, result_value, result_is_decimal_string,
            peak_rss_bytes, zuckerli_load_seconds,
            weight_preparation_seconds, algorithm_seconds, nullopt
        };
        if (algorithm == "mvm") {
            report.mvm_execution = zk_algo::MvmExecution{
                config.mvm_epsilon, config.mvm_seed, mvm_result.sample_limit,
                mvm_result.samples_completed, mvm_result.positive_augmentations,
                mvm_result.complete, mvm_result.all_vertices_matched
            };
        }
        report_slot.publish(report);
        cout << report_slot.path().string() << '\n';
        return 0;
    } catch (const exception& error) {
        cerr << "zk_algo: " << error.what() << '\n';
        return 1;
    }
}
