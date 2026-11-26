#include "cc_con/report.hpp"

#include "cc_con/output.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>

namespace cc_con {
namespace {

constexpr std::uint32_t kRunReportSchemaVersion = 1;

void write_json_string(std::ostream& output, const std::string& value) {
    output << '"';
    for (const unsigned char c : value) {
        switch (c) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned>(c) << std::dec << std::setfill(' ');
                } else {
                    output << static_cast<char>(c);
                }
        }
    }
    output << '"';
}

void write_json_value(std::ostream& output, const ConfigValue& value) {
    std::visit([&](const auto& typed) {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            output << "null";
        } else if constexpr (std::is_same_v<T, std::string>) {
            write_json_string(output, typed);
        } else if constexpr (std::is_same_v<T, bool>) {
            output << (typed ? "true" : "false");
        } else if constexpr (std::is_same_v<T, double>) {
            if (std::isfinite(typed)) output << std::setprecision(15) << typed;
            else output << "null";
        } else {
            output << typed;
        }
    }, value);
}

void write_json_map(std::ostream& output,
                    const std::map<std::string, ConfigValue>& values,
                    const unsigned indent) {
    output << '{';
    if (!values.empty()) output << '\n';
    std::size_t written = 0;
    for (const auto& [name, value] : values) {
        output << std::string(indent + 2, ' ');
        write_json_string(output, name);
        output << ": ";
        write_json_value(output, value);
        if (++written < values.size()) output << ',';
        output << '\n';
    }
    if (!values.empty()) output << std::string(indent, ' ');
    output << '}';
}

void write_json_report(std::ostream& output, const RunReport& report) {
    output << std::setprecision(15);
    output << "{\n";
    output << "  \"schema_version\": " << kRunReportSchemaVersion << ",\n";

    output << "  \"algorithm\": {\n";
    output << "    \"name\": ";
    write_json_string(output, report.algorithm);
    output << ",\n    \"parameters\": ";
    write_json_map(output, report.algorithm_parameters, 4);
    output << "\n  },\n";

    output << "  \"graph\": {\n";
    output << "    \"name\": "; write_json_string(output, report.graph.name); output << ",\n";
    output << "    \"source\": "; write_json_string(output, report.graph.source); output << ",\n";
    output << "    \"type\": "; write_json_string(output, report.graph.type); output << ",\n";
    output << "    \"parameters\": ";
    write_json_map(output, report.graph.parameters, 4);
    output << ",\n";
    output << "    \"generation_statistics\": ";
    write_json_map(output, report.graph.generation_statistics, 4);
    output << ",\n";
    output << "    \"specification_path\": ";
    write_json_string(output, report.graph.specification_path);
    output << ",\n";
    output << "    \"origin_specification_path\": ";
    write_json_string(output, report.graph.origin_specification_path);
    output << ",\n";
    output << "    \"input_format\": ";
    write_json_string(output, report.graph.prepared_input ? "cgf" : report.graph.type);
    output << ",\n";
    if (report.graph.prepared_input) {
        output << "    \"prepared_artifact_path\": ";
        write_json_string(output, report.graph.prepared_artifact_path);
        output << ",\n";
        output << "    \"preparation_report_path\": ";
        write_json_string(output, report.graph.preparation_report_path);
        output << ",\n";
    }
    output << "    \"canonical_fingerprint_sha256\": ";
    write_json_string(output, report.graph.canonical_fingerprint_sha256);
    output << ",\n";
    output << "    \"n\": " << report.n << ",\n";
    output << "    \"m\": " << report.m << ",\n";
    output << "    \"time_seconds\": " << report.graph_time_seconds << '\n';
    output << "  },\n";

    output << "  \"constructed_cover\": {\n";
    output << "    \"num_cliques\": " << report.constructed_num_cliques << ",\n";
    output << "    \"cover_size\": " << report.constructed_cover_size << ",\n";
    output << "    \"time_seconds\": " << report.construction_time_seconds << '\n';
    output << "  },\n";

    output << "  \"algorithm_statistics\": ";
    write_json_map(output, report.algorithm_statistics, 2);
    output << ",\n";

    output << "  \"postprocess\": {\n";
    output << "    \"assignment_minimal\": "
           << (report.assignment_minimal ? "true" : "false");
    if (report.assignment_minimal) {
        output << ",\n    \"num_cliques\": " << report.postprocessed_num_cliques;
        output << ",\n    \"cover_size\": " << report.postprocessed_cover_size;
        output << ",\n    \"time_seconds\": " << report.postprocessing_time_seconds << '\n';
    } else {
        output << '\n';
    }
    output << "  },\n";

    output << "  \"output\": {\n";
    output << "    \"written\": " << (report.output_written ? "true" : "false");
    if (report.output_written) {
        output << ",\n    \"format\": \"ccf\"";
        output << ",\n    \"format_version\": " << kCcfFormatVersion;
        output << ",\n    \"path\": "; write_json_string(output, report.output_path);
        output << ",\n    \"encoded_bytes\": " << report.encoded_bytes;
        output << ",\n    \"file_checksum_sha256\": ";
        write_json_string(output, report.output_checksum_sha256);
        output << ",\n    \"encoding_and_write_time_seconds\": "
               << report.output_time_seconds << '\n';
    } else {
        output << '\n';
    }
    output << "  },\n";

    output << "  \"verification\": {\n";
    output << "    \"explicitly_requested\": "
           << (report.verification_explicitly_requested ? "true" : "false");
    output << ",\n    \"performed\": "
           << (report.verification_performed ? "true" : "false");
    if (report.verification_performed) {
        output << ",\n    \"trigger\": ";
        write_json_string(output, report.verification_trigger);
        output << ",\n    \"passed\": "
               << (report.verification_passed ? "true" : "false");
        output << ",\n    \"time_seconds\": " << report.verification_time_seconds << '\n';
    } else {
        output << '\n';
    }
    output << "  }\n";
    output << "}\n";
}

bool path_exists(const std::filesystem::path& path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        throw std::runtime_error("Unable to inspect report path '" + path.string() +
                                 "': " + error.message());
    }
    return exists;
}

}  // namespace

ReportSlot::ReportSlot(const std::string& algorithm,
                       const std::string& graph_name) {
    const std::filesystem::path directory = std::filesystem::current_path();
    const std::string stem = algorithm + "-" + graph_name;

    for (std::uint64_t suffix = 1;
         suffix < std::numeric_limits<std::uint64_t>::max();
         ++suffix) {
        const std::string filename =
            stem + (suffix == 1 ? "" : "-" + std::to_string(suffix)) + ".json";
        const std::filesystem::path candidate = directory / filename;
        const std::filesystem::path claim = candidate.string() + ".claim";

        // create_directory is the reservation operation: only one process can
        // create this path, even when several runs test the same suffix at once.
        std::error_code error;
        if (!std::filesystem::create_directory(claim, error)) {
            if (error && error != std::errc::file_exists) {
                throw std::runtime_error("Unable to reserve report name: " +
                                         error.message());
            }
            continue;
        }

        // A completed report may predate this reservation mechanism. Keep it
        // and continue to the next suffix after releasing the temporary claim.
        if (path_exists(candidate)) {
            std::filesystem::remove(claim, error);
            if (error) {
                throw std::runtime_error("Unable to release report claim '" +
                                         claim.string() + "': " + error.message());
            }
            continue;
        }

        report_path_ = candidate;
        claim_path_ = claim;
        return;
    }
    throw std::runtime_error("Unable to reserve an unused report filename");
}

ReportSlot::~ReportSlot() {
    if (claim_path_.empty()) return;
    std::error_code ignored;
    std::filesystem::remove(claim_path_, ignored);
}

void ReportSlot::publish(const RunReport& report) {
    if (published_) {
        throw std::logic_error("Report slot has already been published");
    }

    std::filesystem::path temporary_path = report_path_;
    temporary_path += ".tmp";

    try {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Unable to open temporary report '" +
                                     temporary_path.string() + "'");
        }
        write_json_report(output, report);
        output.close();
        if (!output) {
            throw std::runtime_error("Failed while writing temporary report '" +
                                     temporary_path.string() + "'");
        }

        // Both paths are in the current directory, so rename publishes the
        // complete report atomically on the filesystems used for experiments.
        std::error_code error;
        std::filesystem::rename(temporary_path, report_path_, error);
        if (error) {
            throw std::runtime_error("Unable to publish report '" +
                                     report_path_.string() + "': " + error.message());
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        throw;
    }

    published_ = true;
}

}  // namespace cc_con
