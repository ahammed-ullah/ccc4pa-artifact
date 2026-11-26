#ifndef CC_CON_BOUND_HPP
#define CC_CON_BOUND_HPP

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace cc_con {

enum class BoundMethod {
    Linear,
    LocalMis,
    LocalColor,
};

struct BoundOptions {
    BoundMethod method = BoundMethod::Linear;
    bool exact_truss = false;
};

struct BoundTimings {
    double linear_seconds = 0.0;
    double common_neighborhood_seconds = 0.0;
    double local_mis_seconds = 0.0;
    double truss_seconds = 0.0;
    double local_color_seconds = 0.0;
    double total_seconds = 0.0;
};

struct BoundResult {
    std::map<std::string, std::uint64_t> lower_bounds;
    BoundTimings timings;
};

struct BoundReport {
    std::string method;
    bool exact_truss = false;

    std::string graph_name;
    std::string graph_path;
    std::uint64_t n = 0;
    std::uint64_t m = 0;
    std::uint64_t cgf_bytes = 0;
    double graph_read_seconds = 0.0;

    BoundResult result;
};

const char* bound_method_name(BoundMethod method);

// Every procedure assumes that adj is the canonical graph decoded from CGF:
// simple, undirected, sorted, duplicate-free, and without isolated vertices.
void compute_clique_cover_lower_bounds(
    const std::vector<std::vector<int>>& adj,
    const BoundOptions& options,
    BoundResult& result);

// Reports are written in the current directory. The preferred filename is
// bound-<method>[-truss]-<graph>.json; an integer suffix preserves an existing
// completed report with the same name.
std::filesystem::path write_bound_report(const BoundReport& report);

}  // namespace cc_con

#endif
