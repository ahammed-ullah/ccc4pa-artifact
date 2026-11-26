#include "cc_con/cgf.hpp"
#include "cc_con/graph.hpp"
#include "graph_metadata_io.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void print_usage(std::ostream& output, const char* program) {
    output << "Usage:\n"
           << "  " << program
           << " --graph <source.toml> --output-dir <directory>\n"
           << "  " << program << " --help\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            print_usage(std::cout, argv[0]);
            return 0;
        }
        if (argc != 5 || std::string(argv[1]) != "--graph" ||
            std::string(argv[3]) != "--output-dir") {
            print_usage(std::cerr, argv[0]);
            return 2;
        }

        const std::filesystem::path graph_specification = argv[2];
        const std::filesystem::path output_directory =
            std::filesystem::absolute(argv[4]).lexically_normal();
        std::error_code directory_error;
        std::filesystem::create_directories(output_directory, directory_error);
        if (directory_error) {
            throw std::runtime_error("Unable to create output directory '" +
                                     output_directory.string() + "': " +
                                     directory_error.message());
        }

        std::vector<std::vector<int>> adj;
        cc_con::GraphMetadata metadata;
        const Clock::time_point graph_start = Clock::now();
        // Raw sources follow the same canonicalization path either way. If a
        // prepared CGF is supplied deliberately, do not re-encode it unless its
        // explicit validation succeeds first.
        cc_con::prepare_and_validate_graph(graph_specification, adj, metadata);
        const long long m = cc_con::count_edges(adj);
        const double graph_time_seconds = elapsed_seconds(graph_start);

        const std::filesystem::path artifact_path =
            output_directory / (metadata.name + ".cgf");
        const std::filesystem::path report_path =
            output_directory / (metadata.name + ".cgf.json");
        const std::filesystem::path prepared_specification =
            output_directory / (metadata.name + ".cgf.toml");

        const Clock::time_point encoding_start = Clock::now();
        const cc_con::CgfArtifactInfo artifact = cc_con::write_cgf(
            artifact_path, adj, m, metadata.canonical_fingerprint_sha256);
        const double encoding_and_write_time_seconds =
            elapsed_seconds(encoding_start);

        cc_con::GraphPreparationRecord record;
        record.metadata = metadata;
        record.n = static_cast<std::uint64_t>(adj.size());
        record.m = static_cast<std::uint64_t>(m);
        record.artifact = artifact;
        record.graph_time_seconds = graph_time_seconds;
        record.encoding_and_write_time_seconds =
            encoding_and_write_time_seconds;
        cc_con::write_graph_preparation_report(
            report_path, artifact_path, record);

        // The generated graph specification is the completion marker. A caller
        // that sees it can expect both the CGF and its metadata report to exist.
        cc_con::write_prepared_graph_specification(
            prepared_specification, metadata.name, artifact_path, report_path);

        std::cout << "prepared: graph=" << metadata.name
                  << " n=" << adj.size()
                  << " m=" << m
                  << " specification=" << prepared_specification.string()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cg_prepare: " << error.what() << '\n';
        return 1;
    }
}
