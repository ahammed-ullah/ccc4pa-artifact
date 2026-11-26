#include "cc_con/graph.hpp"
#include "cc_con/output.hpp"
#include "cc_con/verification.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void print_usage(std::ostream& output, const char* program) {
    output << "Usage:\n"
           << "  " << program << " graph --graph <graph.cgf.toml>\n"
           << "  " << program << " cover --cover <cover.ccf>\n"
           << "  " << program
           << " cover --cover <cover.ccf> --graph <graph.toml>\n"
           << "  " << program << " --help\n";
}

void check_graph(const std::filesystem::path& graph_specification) {
    std::vector<std::vector<int>> adj;
    cc_con::GraphMetadata metadata;
    cc_con::prepare_and_validate_graph(graph_specification, adj, metadata);
    if (!metadata.prepared_input) {
        throw std::runtime_error(
            "'cc_check graph' requires a CGF graph specification");
    }

    std::cout << "valid graph: name=" << metadata.name
              << " n=" << adj.size()
              << " m=" << cc_con::count_edges(adj)
              << " fingerprint=" << metadata.canonical_fingerprint_sha256
              << '\n';
}

void check_cover(const std::filesystem::path& cover_path,
                 const std::filesystem::path* graph_specification) {
    std::vector<std::vector<int>> C;
    const cc_con::CcfArtifactInfo cover =
        cc_con::read_and_validate_ccf(cover_path, C);

    if (graph_specification == nullptr) {
        std::cout << "valid cover structure: n=" << cover.n
                  << " m=" << cover.m
                  << " cliques=" << cover.num_cliques
                  << " cover_size=" << cover.cover_size
                  << " bytes=" << cover.file_bytes
                  << " checksum=" << cover.file_checksum_sha256
                  << '\n';
        return;
    }

    std::vector<std::vector<int>> adj;
    cc_con::GraphMetadata metadata;
    cc_con::prepare_and_validate_graph(*graph_specification, adj, metadata);
    const long long m = cc_con::count_edges(adj);
    if (cover.n != adj.size() || cover.m != static_cast<std::uint64_t>(m)) {
        throw std::runtime_error(
            "CCF graph order/size does not match the supplied graph");
    }
    if (cover.graph_fingerprint_sha256 !=
        metadata.canonical_fingerprint_sha256) {
        throw std::runtime_error(
            "CCF graph fingerprint does not match the supplied graph");
    }

    std::string diagnostic;
    if (!cc_con::verify_clique_cover(adj, C, diagnostic)) {
        throw std::runtime_error("Cover verification failed: " + diagnostic);
    }
    std::cout << "valid clique cover: graph=" << metadata.name
              << " cliques=" << cover.num_cliques
              << " cover_size=" << cover.cover_size
              << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            print_usage(std::cout, argv[0]);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "graph" &&
            std::string(argv[2]) == "--graph") {
            check_graph(argv[3]);
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "cover" &&
            std::string(argv[2]) == "--cover") {
            check_cover(argv[3], nullptr);
            return 0;
        }
        if (argc == 6 && std::string(argv[1]) == "cover" &&
            std::string(argv[2]) == "--cover" &&
            std::string(argv[4]) == "--graph") {
            const std::filesystem::path graph_specification = argv[5];
            check_cover(argv[3], &graph_specification);
            return 0;
        }

        print_usage(std::cerr, argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "cc_check: " << error.what() << '\n';
        return 1;
    }
}
