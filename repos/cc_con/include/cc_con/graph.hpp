#ifndef CC_CON_GRAPH_HPP
#define CC_CON_GRAPH_HPP

#include "cc_con/config.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace cc_con {

// This structure is preparation/report metadata only. Construction algorithms
// do not receive it; they receive the concrete vector<vector<int>> adjacency.
struct GraphMetadata {
    std::string name;
    std::string source;
    std::string type;
    std::map<std::string, ConfigValue> parameters;
    // Measurements produced while generating or projecting a graph. Input
    // parameters remain separate so a report never confuses requested values
    // with properties observed while preparing the canonical graph.
    std::map<std::string, ConfigValue> generation_statistics;

    // The current graph specification is the TOML passed to prepare_graph().
    // origin_specification_path remains the raw specification when the current
    // input is a prepared CGF file.
    std::string specification_path;
    std::string origin_specification_path;
    std::string canonical_fingerprint_sha256;
    bool prepared_input = false;
    std::string prepared_artifact_path;
    std::string preparation_report_path;
};

// Loads one graph specification, dispatches its source, and returns the only
// representation accepted by constructions: canonical vector<vector<int>>.
// Raw sources are canonicalized and validated. A prepared CGF is decoded
// without repeating hashes or graph validation; use
// prepare_and_validate_graph for the explicit cc_check path.
void prepare_graph(const std::filesystem::path& graph_specification,
                   std::vector<std::vector<int>>& adj,
                   GraphMetadata& metadata);
void prepare_and_validate_graph(
    const std::filesystem::path& graph_specification,
    std::vector<std::vector<int>>& adj,
    GraphMetadata& metadata);

// Converts any supplied adjacency data to the one representation accepted by
// constructions: simple, undirected, no isolated vertices, dense ids 0..n-1,
// and sorted, duplicate-free open adjacency lists.
void canonicalize_graph(std::vector<std::vector<int>>& adj);
void validate_canonical_graph(const std::vector<std::vector<int>>& adj);
long long count_edges(const std::vector<std::vector<int>>& adj);

}  // namespace cc_con

#endif
