#include "cc_con/graph.hpp"

#include "artifact_support.hpp"
#include "cc_con/cgf.hpp"
#include "graph_metadata_io.hpp"
#include "graph_sources.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cc_con {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool valid_graph_name(const std::string& name) {
    if (name.empty() || !std::isalnum(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    for (const unsigned char c : name) {
        if (!std::isalnum(c) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

void remove_isolated_vertices_from_simple_graph(
    std::vector<std::vector<int>>& adj) {
    // The generators and Cornell readers construct simple, symmetric, sorted
    // adjacency lists directly. Their only remaining canonicalization
    // step is removal of isolates. Since surviving old ids are mapped in
    // increasing order, replacing each neighbor by its new id preserves order.
    std::vector<int> old_to_new(adj.size(), -1);
    int canonical_n = 0;
    for (std::size_t old = 0; old < adj.size(); ++old) {
        if (!adj[old].empty()) old_to_new[old] = canonical_n++;
    }

    std::size_t write = 0;
    for (std::size_t old = 0; old < adj.size(); ++old) {
        if (adj[old].empty()) continue;

        for (int& neighbor : adj[old]) neighbor = old_to_new[neighbor];
        if (write != old) adj[write] = std::move(adj[old]);
        ++write;
    }
    adj.resize(write);
}

void reject_unused_graph_keys(const Config& config) {
    const std::vector<std::string> unused = config.unused_keys();
    if (unused.empty()) return;

    std::string message = "Unknown or inapplicable graph configuration key";
    if (unused.size() > 1) message += 's';
    message += ':';
    for (const std::string& key : unused) message += " " + key;
    throw std::runtime_error(message);
}

void require_same_cgf_metadata(const CgfArtifactInfo& file,
                               const GraphPreparationRecord& report) {
    if (file.n != report.n || file.m != report.m ||
        file.adjacency_entries != report.artifact.adjacency_entries ||
        file.file_bytes != report.artifact.file_bytes ||
        file.graph_fingerprint_sha256 !=
            report.artifact.graph_fingerprint_sha256 ||
        file.file_checksum_sha256 != report.artifact.file_checksum_sha256) {
        throw std::runtime_error(
            "CGF stored header metadata does not agree with its preparation report");
    }
}

}  // namespace

void canonicalize_graph(std::vector<std::vector<int>>& adj) {
    if (adj.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Graph has too many vertices for int identifiers");
    }

    // First reduce every supplied arc to an unordered edge. Sorting once removes
    // duplicate and mirrored entries and makes an asymmetric input undirected.
    std::size_t num_entries = 0;
    for (const std::vector<int>& neighbors : adj) {
        if (neighbors.size() > std::numeric_limits<std::size_t>::max() - num_entries) {
            throw std::runtime_error("Graph adjacency is too large");
        }
        num_entries += neighbors.size();
    }

    std::vector<std::pair<int, int>> edges;
    edges.reserve(num_entries);
    for (std::size_t u_index = 0; u_index < adj.size(); ++u_index) {
        const int u = static_cast<int>(u_index);
        for (const int v : adj[u_index]) {
            if (v < 0 || static_cast<std::size_t>(v) >= adj.size()) {
                throw std::runtime_error("Graph contains a vertex identifier outside its order");
            }
            if (u == v) continue;
            edges.emplace_back(std::min(u, v), std::max(u, v));
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    // Isolated vertices are removed only after loops and duplicates disappear.
    // Remaining vertices keep their relative order and receive dense new ids.
    std::vector<int> degree(adj.size(), 0);
    for (const auto& [u, v] : edges) {
        ++degree[u];
        ++degree[v];
    }

    std::vector<int> old_to_new(adj.size(), -1);
    int n = 0;
    for (std::size_t old = 0; old < degree.size(); ++old) {
        if (degree[old] > 0) old_to_new[old] = n++;
    }

    std::vector<std::vector<int>> canonical(static_cast<std::size_t>(n));
    for (const auto& [old_u, old_v] : edges) {
        const int u = old_to_new[old_u];
        const int v = old_to_new[old_v];
        canonical[u].push_back(v);
        canonical[v].push_back(u);
    }
    for (std::vector<int>& neighbors : canonical) {
        std::sort(neighbors.begin(), neighbors.end());
    }
    adj.swap(canonical);
}

void validate_canonical_graph(const std::vector<std::vector<int>>& adj) {
    if (adj.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Canonical graph exceeds int vertex identifiers");
    }

    for (std::size_t u = 0; u < adj.size(); ++u) {
        const std::vector<int>& neighbors = adj[u];
        if (neighbors.empty()) {
            throw std::runtime_error("Canonical graph contains an isolated vertex");
        }
        for (std::size_t i = 0; i < neighbors.size(); ++i) {
            const int v = neighbors[i];
            if (v < 0 || static_cast<std::size_t>(v) >= adj.size()) {
                throw std::runtime_error(
                    "Canonical graph contains an out-of-range neighbor");
            }
            if (v == static_cast<int>(u)) {
                throw std::runtime_error("Canonical graph contains a self-loop");
            }
            if (i > 0 && neighbors[i - 1] >= v) {
                throw std::runtime_error(
                    "Canonical adjacency lists must be sorted and duplicate-free");
            }
            if (!std::binary_search(adj[static_cast<std::size_t>(v)].begin(),
                                    adj[static_cast<std::size_t>(v)].end(),
                                    static_cast<int>(u))) {
                throw std::runtime_error("Canonical graph contains an asymmetric edge");
            }
        }
    }
}

long long count_edges(const std::vector<std::vector<int>>& adj) {
    long long entries = 0;
    for (const std::vector<int>& neighbors : adj) {
        if (neighbors.size() >
            static_cast<std::size_t>(std::numeric_limits<long long>::max() - entries)) {
            throw std::runtime_error("Graph has too many adjacency entries to count");
        }
        entries += static_cast<long long>(neighbors.size());
    }
    if (entries % 2 != 0) {
        throw std::runtime_error("Canonical graph has an odd adjacency-entry count");
    }
    return entries / 2;
}

namespace {

void prepare_graph_impl(const std::filesystem::path& graph_specification,
                        std::vector<std::vector<int>>& adj,
                        GraphMetadata& metadata,
                        const bool validate_prepared_input) {
    adj.clear();
    metadata = GraphMetadata{};
    Config config = Config::load(graph_specification);
    const std::string source = lower(config.require_string("graph.source"));
    const std::string configured_name = config.require_string("graph.name");
    if (!valid_graph_name(configured_name)) {
        throw std::runtime_error(
            "graph.name must start with a letter or digit and contain only "
            "letters, digits, '.', '-', or '_'");
    }

    if (source == "synthetic") {
        const std::string model = lower(config.require_string("graph.model"));
        if (model == "hypergraph") {
            generate_hypergraph_graph(config, configured_name, adj, metadata);
        } else {
            throw std::runtime_error("Unsupported synthetic graph model '" + model + "'");
        }

        // Synthetic sources already guarantee loop-free, duplicate-free,
        // symmetric, sorted adjacency. Compacting away isolates is therefore
        // sufficient and avoids flattening and sorting all edges again.
        remove_isolated_vertices_from_simple_graph(adj);
    } else if (source == "file") {
        const std::string format = lower(config.require_string("graph.format"));
        if (format == "mm" || format == "matrix-market") {
            load_matrix_market_graph(config, configured_name, adj, metadata);
            // Matrix Market input may contain loops, duplicates, or only one
            // triangle of a symmetric matrix, so canonicalize it fully.
            canonicalize_graph(adj);
        } else if (format == "cornell-simplices") {
            load_cornell_simplices_graph(
                config, configured_name, adj, metadata);
            remove_isolated_vertices_from_simple_graph(adj);
        } else if (format == "cornell-dual-simplices") {
            load_cornell_dual_simplices_graph(
                config, configured_name, adj, metadata);
            remove_isolated_vertices_from_simple_graph(adj);
        } else if (format == "cornell-hyperedges") {
            load_cornell_hyperedges_graph(
                config, configured_name, adj, metadata);
            remove_isolated_vertices_from_simple_graph(adj);
        } else if (format == "sx-tag-question") {
            load_sx_tag_questions_graph(
                config, configured_name, adj, metadata);
            remove_isolated_vertices_from_simple_graph(adj);
        } else if (format == "cgf") {
            const std::filesystem::path artifact_path =
                config.resolve_path(config.require_string("graph.path"));
            const std::filesystem::path report_path = config.resolve_path(
                config.require_string("graph.preparation_report"));
            const std::string expected_report_checksum = config.require_string(
                "graph.preparation_report_sha256");
            parse_sha256_hex(expected_report_checksum);
            if (validate_prepared_input) {
                const std::string actual_report_checksum = sha256_hex(
                    sha256_file_with_zero_range(report_path, 0, 0));
                if (actual_report_checksum != expected_report_checksum) {
                    throw std::runtime_error(
                        "CGF preparation-report SHA-256 checksum does not match");
                }
            }

            GraphPreparationRecord preparation =
                read_graph_preparation_report(report_path);
            if (preparation.metadata.name != configured_name) {
                throw std::runtime_error(
                    "graph.name does not agree with the CGF preparation report");
            }
            if (std::filesystem::absolute(artifact_path).lexically_normal() !=
                std::filesystem::absolute(
                    preparation.metadata.prepared_artifact_path)
                    .lexically_normal()) {
                throw std::runtime_error(
                    "graph.path does not agree with the CGF preparation report");
            }

            const CgfArtifactInfo artifact = validate_prepared_input
                ? read_and_validate_cgf(artifact_path, adj)
                : read_cgf(artifact_path, adj);
            require_same_cgf_metadata(artifact, preparation);

            metadata = std::move(preparation.metadata);
            metadata.prepared_input = true;
            metadata.prepared_artifact_path = artifact_path.string();
            metadata.preparation_report_path = report_path.string();
            metadata.canonical_fingerprint_sha256 =
                artifact.graph_fingerprint_sha256;
        } else {
            throw std::runtime_error("Unsupported graph file format '" + format + "'");
        }
    } else {
        throw std::runtime_error("graph.source must be 'synthetic' or 'file'");
    }

    reject_unused_graph_keys(config);
    if (!metadata.prepared_input) {
        validate_canonical_graph(adj);
        const long long m = count_edges(adj);
        metadata.canonical_fingerprint_sha256 =
            canonical_graph_fingerprint_sha256(adj, m);
        metadata.origin_specification_path = config.source_path().string();
    }
    metadata.specification_path = config.source_path().string();
}

}  // namespace

void prepare_graph(const std::filesystem::path& graph_specification,
                   std::vector<std::vector<int>>& adj,
                   GraphMetadata& metadata) {
    prepare_graph_impl(graph_specification, adj, metadata, false);
}

void prepare_and_validate_graph(
    const std::filesystem::path& graph_specification,
    std::vector<std::vector<int>>& adj,
    GraphMetadata& metadata) {
    prepare_graph_impl(graph_specification, adj, metadata, true);
}

}  // namespace cc_con
