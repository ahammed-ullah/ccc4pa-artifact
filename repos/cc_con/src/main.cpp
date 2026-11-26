#include "cc_con/algo_bcc.hpp"
#include "cc_con/algo_bpa.hpp"
#include "cc_con/config.hpp"
#include "cc_con/algo_cpa.hpp"
#include "cc_con/algo_dpf.hpp"
#include "cc_con/algo_fpa.hpp"
#include "cc_con/graph.hpp"
#include "cc_con/algo_lzf.hpp"
#include "cc_con/output.hpp"
#include "cc_con/postprocess.hpp"
#include "cc_con/report.hpp"
#include "cc_con/verification.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

long long cover_size(const std::vector<std::vector<int>>& C) {
    long long size = 0;
    for (const std::vector<int>& clique : C) {
        size += static_cast<long long>(clique.size());
    }
    return size;
}

void print_usage(std::ostream& output, const char* program) {
    output << "Usage:\n"
           << "  " << program
           << " <algorithm> --graph <graph.toml> --run <run.toml>\n"
           << "  " << program << " <algorithm> --help\n"
           << "  " << program << " --list-algorithms\n"
           << "  " << program << " --help\n";
}

void print_bpa_help(std::ostream& output) {
    output << "BPA run configuration:\n"
           << "  [construction]\n"
           << "  tau = 4.0\n"
           << "  label_selection = \"min-label\"\n"
           << "Label rules: min-label, max-label, min-clique, max-clique.\n";
}

void print_bcc_help(std::ostream& output) {
    output << "BCC has no construction parameters. Its fixed split-bypass "
              "decision is derived from the canonical graph; the part-size "
              "limit is calculated only when splitting is applied.\n";
}

void print_cpa_help(std::ostream& output) {
    output << "CPA run configuration:\n"
           << "  [construction]\n"
           << "  tau = 4.0\n"
           << "  label_selection = \"min-label\"\n"
           << "Label rules: min-label, max-label, min-clique, max-clique.\n";
}

void print_dpf_help(std::ostream& output) {
    output << "DPF has no construction parameters. It uses an array-bucket "
              "degeneracy order for pivots and canonical vertex-id order for "
              "local first fit.\n";
}

void print_lzf_help(std::ostream& output) {
    output << "LZF has no construction parameters. It uses canonical vertex-id "
              "order for first fit.\n";
}

void print_fpa_help(std::ostream& output) {
    output << "FPA run configuration:\n"
           << "  [construction]\n"
           << "  tau = 4.0\n"
           << "  label_selection = \"min-label\"\n"
           << "Label rules: min-label, max-label, min-clique, max-clique.\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            print_usage(std::cout, argv[0]);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--list-algorithms") {
            std::cout << "bcc\nbpa\ncpa\ndpf\nfpa\nlzf\n";
            return 0;
        }
        if (argc == 3 && std::string(argv[2]) == "--help") {
            const std::string algorithm = lower(argv[1]);
            if (algorithm == "bcc") {
                print_bcc_help(std::cout);
            } else if (algorithm == "bpa") {
                print_bpa_help(std::cout);
            } else if (algorithm == "cpa") {
                print_cpa_help(std::cout);
            } else if (algorithm == "dpf") {
                print_dpf_help(std::cout);
            } else if (algorithm == "fpa") {
                print_fpa_help(std::cout);
            } else if (algorithm == "lzf") {
                print_lzf_help(std::cout);
            } else {
                throw std::runtime_error(
                    "Unknown construction algorithm '" + algorithm + "'");
            }
            return 0;
        }
        if (argc != 6 || std::string(argv[2]) != "--graph" ||
            std::string(argv[4]) != "--run") {
            print_usage(std::cerr, argv[0]);
            return 2;
        }

        const std::string algorithm = lower(argv[1]);
        if (algorithm != "bcc" && algorithm != "bpa" &&
            algorithm != "cpa" && algorithm != "dpf" &&
            algorithm != "fpa" && algorithm != "lzf") {
            throw std::runtime_error(
                "Unknown construction algorithm '" + algorithm + "'");
        }

        // Graph and run specifications are independent. One graph specification can
        // therefore be reused for every construction without duplicating its
        // model parameters or input path.
        cc_con::Config run_config = cc_con::Config::load(argv[5]);

        // Parse every run option before graph preparation and construction
        // timing. BCC, DPF, and LZF deliberately consume no [construction]
        // keys.
        const cc_con::CommonOptions common =
            cc_con::parse_common_options(run_config);
        cc_con::BpaOptions bpa_options;
        cc_con::CpaOptions cpa_options;
        cc_con::FpaOptions fpa_options;
        if (algorithm == "bpa") {
            bpa_options = cc_con::parse_bpa_options(run_config);
        } else if (algorithm == "cpa") {
            cpa_options = cc_con::parse_cpa_options(run_config);
        } else if (algorithm == "fpa") {
            fpa_options = cc_con::parse_fpa_options(run_config);
        }

        const std::vector<std::string> unused_run = run_config.unused_keys();
        if (!unused_run.empty()) {
            std::string message = "Unknown or inapplicable run configuration key";
            if (unused_run.size() > 1) message += 's';
            message += ':';
            for (const std::string& key : unused_run) message += " " + key;
            throw std::runtime_error(message);
        }

        std::vector<std::vector<int>> adj;
        cc_con::GraphMetadata graph_metadata;
        const Clock::time_point graph_start = Clock::now();
        cc_con::prepare_graph(argv[3], adj, graph_metadata);
        const long long m = cc_con::count_edges(adj);
        const double graph_time_seconds = elapsed_seconds(graph_start);

        // Reserve a unique report name before construction. The atomic claim
        // permits repeated jobs for this algorithm and graph to run in the same
        // directory without adding report-file work to the construction timer.
        cc_con::ReportSlot report_slot(algorithm, graph_metadata.name);

        cc_con::RunReport report;
        report.algorithm = algorithm;
        report.graph = graph_metadata;
        report.n = static_cast<long long>(adj.size());
        report.m = m;
        report.graph_time_seconds = graph_time_seconds;

        // Each construction is a direct call. Parsing, branch
        // selection, and report bookkeeping all remain outside the timed region.
        std::vector<std::vector<int>> C;
        if (algorithm == "bcc") {
            cc_con::BccStatistics statistics;
            const Clock::time_point construction_start = Clock::now();
            cc_con::construct_bcc(adj, m, C, statistics);
            report.construction_time_seconds = elapsed_seconds(construction_start);

            report.algorithm_statistics["mu"] =
                static_cast<std::int64_t>(statistics.mu);
            report.algorithm_statistics["splitting_applied"] =
                statistics.splitting_applied;
            if (statistics.splitting_applied) {
                report.algorithm_statistics["part_size_limit"] =
                    static_cast<std::int64_t>(statistics.part_size_limit);
            }
            report.algorithm_statistics["complement_degeneracy"] =
                static_cast<std::int64_t>(statistics.complement_degeneracy);
            report.algorithm_statistics["num_complement_colors"] =
                static_cast<std::int64_t>(statistics.num_complement_colors);
            report.algorithm_statistics["num_parts"] =
                static_cast<std::int64_t>(statistics.num_parts);
        } else if (algorithm == "bpa") {
            cc_con::BpaStatistics statistics;
            const Clock::time_point construction_start = Clock::now();
            cc_con::construct_bpa(adj, m, bpa_options, C, statistics);
            report.construction_time_seconds = elapsed_seconds(construction_start);

            report.algorithm_parameters["tau"] = bpa_options.tau;
            report.algorithm_parameters["label_selection"] =
                std::string(cc_con::bpa_label_rule_name(bpa_options.label_rule));
            report.algorithm_statistics["num_phases"] =
                static_cast<std::int64_t>(statistics.num_phases);
            report.algorithm_statistics["peak_size_D"] =
                static_cast<std::int64_t>(statistics.peak_size_D);
        } else if (algorithm == "cpa") {
            cc_con::CpaStatistics statistics;
            const Clock::time_point construction_start = Clock::now();
            cc_con::construct_cpa(adj, m, cpa_options, C, statistics);
            report.construction_time_seconds = elapsed_seconds(construction_start);

            report.algorithm_parameters["tau"] = cpa_options.tau;
            report.algorithm_parameters["label_selection"] =
                std::string(cc_con::cpa_label_rule_name(cpa_options.label_rule));
            report.algorithm_statistics["mu"] =
                static_cast<std::int64_t>(statistics.mu);
            report.algorithm_statistics["splitting_applied"] =
                statistics.splitting_applied;
            if (statistics.splitting_applied) {
                report.algorithm_statistics["part_size_limit"] =
                    static_cast<std::int64_t>(statistics.part_size_limit);
            }
            report.algorithm_statistics["complement_degeneracy"] =
                static_cast<std::int64_t>(statistics.complement_degeneracy);
            report.algorithm_statistics["num_complement_colors"] =
                static_cast<std::int64_t>(statistics.num_complement_colors);
            report.algorithm_statistics["num_parts"] =
                static_cast<std::int64_t>(statistics.num_parts);
            report.algorithm_statistics["bcc_num_cliques"] =
                static_cast<std::int64_t>(statistics.bcc_num_cliques);
            report.algorithm_statistics["bcc_cover_size"] =
                static_cast<std::int64_t>(statistics.bcc_cover_size);
            report.algorithm_statistics["num_phases"] =
                static_cast<std::int64_t>(statistics.num_phases);
            report.algorithm_statistics["peak_size_D"] =
                static_cast<std::int64_t>(statistics.peak_size_D);
        } else if (algorithm == "dpf") {
            cc_con::DpfStatistics statistics;
            const Clock::time_point construction_start = Clock::now();
            cc_con::construct_dpf(adj, C, statistics);
            report.construction_time_seconds = elapsed_seconds(construction_start);

            report.algorithm_statistics["degeneracy"] =
                static_cast<std::int64_t>(statistics.degeneracy);
        } else if (algorithm == "fpa") {
            cc_con::FpaStatistics statistics;
            const Clock::time_point construction_start = Clock::now();
            cc_con::construct_fpa(adj, m, fpa_options, C, statistics);
            report.construction_time_seconds = elapsed_seconds(construction_start);

            report.algorithm_parameters["tau"] = fpa_options.tau;
            report.algorithm_parameters["label_selection"] =
                std::string(cc_con::fpa_label_rule_name(fpa_options.label_rule));
            report.algorithm_statistics["num_phases"] =
                static_cast<std::int64_t>(statistics.num_phases);
            report.algorithm_statistics["peak_size_D"] =
                static_cast<std::int64_t>(statistics.peak_size_D);
        } else {
            const Clock::time_point construction_start = Clock::now();
            cc_con::construct_lzf(adj, C);
            report.construction_time_seconds = elapsed_seconds(construction_start);
        }
        report.constructed_num_cliques = static_cast<long long>(C.size());
        report.constructed_cover_size = cover_size(C);

        // Raw statistics are now saved, so optional assignment-minimal pruning
        // can modify C directly without copying the whole cover.
        report.assignment_minimal = common.assignment_minimal;
        if (common.assignment_minimal) {
            const Clock::time_point postprocess_start = Clock::now();
            cc_con::make_assignment_minimal(C, static_cast<int>(adj.size()));
            report.postprocessing_time_seconds = elapsed_seconds(postprocess_start);
            report.postprocessed_num_cliques = static_cast<long long>(C.size());
            report.postprocessed_cover_size = cover_size(C);
        }

        report.verification_explicitly_requested = common.verification;
        report.verification_performed =
            common.verification || common.write_cover;
        if (report.verification_performed) {
            if (common.verification && common.write_cover) {
                report.verification_trigger = "explicit-and-cover-output";
            } else if (common.write_cover) {
                report.verification_trigger = "cover-output";
            } else {
                report.verification_trigger = "explicit";
            }
            std::string diagnostic;
            const Clock::time_point verification_start = Clock::now();
            report.verification_passed =
                cc_con::verify_clique_cover(adj, C, diagnostic);
            report.verification_time_seconds = elapsed_seconds(verification_start);
            if (!report.verification_passed) {
                throw std::runtime_error("Cover verification failed: " + diagnostic);
            }
        }

        if (common.write_cover) {
            const std::filesystem::path cover_path =
                common.cover_directory / (graph_metadata.name + ".ccf");

            report.output_path = cover_path.string();
            const Clock::time_point output_start = Clock::now();
            const cc_con::CcfArtifactInfo artifact = cc_con::write_ccf(
                cover_path, C, static_cast<int>(adj.size()), m,
                graph_metadata.canonical_fingerprint_sha256);
            report.output_time_seconds = elapsed_seconds(output_start);
            report.encoded_bytes = artifact.file_bytes;
            report.output_checksum_sha256 = artifact.file_checksum_sha256;
            report.output_written = true;
        }

        // The JSON report is the completion marker: all requested construction,
        // postprocessing, verification, encoding, and cover writing succeeded
        // before its temporary file is renamed to the final report name.
        report_slot.publish(report);
        std::cout << "completed: algorithm=" << algorithm
                  << " graph=" << graph_metadata.name
                  << " report=" << report_slot.path().string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cc_con: " << error.what() << '\n';
        return 1;
    }
}
