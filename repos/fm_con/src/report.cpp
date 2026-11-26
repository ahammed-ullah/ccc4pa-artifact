#include "fm_con/report.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

using namespace std;

namespace fm_con {
namespace {

constexpr uint32_t report_schema_version = 4;

void write_json_string(ostream& output, const string& value) {
    output << '"';
    for (unsigned char c : value) {
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
                    output << "\\u" << hex << setw(4) << setfill('0')
                           << (unsigned)c << dec << setfill(' ');
                } else {
                    output << (char)c;
                }
        }
    }
    output << '"';
}

string graph_name(const filesystem::path& path) {
    string name = path.filename().string();
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".cgf")
        name.resize(name.size() - 4);
    return name;
}

string delta_filename_text(const FmOptions& options) {
    string text = options.delta_argument;
    if (text.empty()) {
        ostringstream output;
        output << setprecision(numeric_limits<long double>::digits10)
               << options.delta;
        text = output.str();
    }

    // The CLI accepts only decimal-number characters. Keep this replacement
    // for callers of the C++ interface so a report tag cannot become a path.
    for (char& c : text) {
        bool safe = (c >= '0' && c <= '9') || c == '.' || c == '+' ||
                    c == '-' || c == 'e' || c == 'E';
        if (!safe) c = '_';
    }
    return text;
}

void write_json(ostream& output, const FmResult& result) {
    const FmRunStatistics& run = result.selected;
    output << setprecision(numeric_limits<long double>::max_digits10);
    output << "{\n";
    output << "  \"schema_version\": " << report_schema_version << ",\n";
    output << "  \"algorithm\": {\n";
    output << "    \"name\": \"fm\",\n";
    output << "    \"delta_selection\": \""
           << (result.fixed_delta ? "fixed" : "automatic")
           << "\",\n";
    output << "    \"workspace\": ";
    write_json_string(output, workspace_method_name(result.workspace));
    output << "\n";
    output << "  },\n";

    output << "  \"graph\": {\n";
    output << "    \"name\": "; write_json_string(output, graph_name(result.graph.path)); output << ",\n";
    output << "    \"path\": "; write_json_string(output, result.graph.path.string()); output << ",\n";
    output << "    \"format\": \"cgf\",\n";
    output << "    \"format_version\": " << kCgfFormatVersion << ",\n";
    output << "    \"file_bytes\": " << result.graph.file_bytes << ",\n";
    output << "    \"canonical_fingerprint_sha256\": ";
    write_json_string(output, result.graph.graph_fingerprint_sha256);
    output << ",\n";
    output << "    \"file_checksum_sha256\": ";
    write_json_string(output, result.graph.file_checksum_sha256);
    output << ",\n";
    output << "    \"n\": " << result.graph.n << ",\n";
    output << "    \"m\": " << result.graph.m << "\n";
    output << "  },\n";

    output << "  \"delta_search\": {\n";
    output << "    \"policy\": \""
           << (result.fixed_delta
                   ? "fixed-delta"
                   : "distinct-initial-k-midpoints-v1")
           << "\",\n";
    output << "    \"scale_a\": " << result.scale_a << ",\n";
    output << "    \"initial_k_min\": " << result.initial_k_min << ",\n";
    output << "    \"initial_k_max\": " << result.initial_k_max << ",\n";
    output << "    \"candidate_count\": " << result.candidate_count << ",\n";
    output << "    \"candidate_delta_min\": ";
    if (result.selected_delta_exists) output << result.candidate_delta_min;
    else output << "null";
    output << ",\n";
    output << "    \"candidate_delta_max\": ";
    if (result.selected_delta_exists) output << result.candidate_delta_max;
    else output << "null";
    output << ",\n";
    output << "    \"selected_delta\": ";
    if (result.selected_delta_exists) output << run.delta;
    else output << "null";
    output << ",\n";
    output << "    \"selected_initial_k\": " << run.initial_k << ",\n";
    output << "    \"time_seconds\": "
           << result.delta_runs_seconds << "\n";
    output << "  },\n";

    output << "  \"partition\": {\n";
    output << "    \"initial_cross_edges\": " << run.initial_cross_edges << ",\n";
    output << "    \"k_ge_2_bicliques\": " << run.k_ge_2_bicliques << ",\n";
    output << "    \"star_bicliques\": " << run.star_bicliques << ",\n";
    output << "    \"num_parts\": " << run.num_parts << ",\n";
    output << "    \"edges_in_k_ge_2_bicliques\": "
           << run.edges_in_k_ge_2_bicliques << ",\n";
    output << "    \"edges_in_star_bicliques\": "
           << run.edges_in_star_bicliques << ",\n";
    output << "    \"residual_edges\": " << run.residual_edges << ",\n";
    output << "    \"nontrivial_size\": " << run.nontrivial_size << ",\n";
    output << "    \"residual_size\": " << run.residual_size << ",\n";
    output << "    \"total_size\": " << run.total_size << "\n";
    output << "  },\n";

    output << "  \"fm_representation\": {\n";
    output << "    \"m_star\": " << run.m_star << ",\n";
    output << "    \"n_star\": " << run.n_star << ",\n";
    output << "    \"center_count\": " << run.center_count << ",\n";
    output << "    \"return_arcs\": " << result.graph.n << "\n";
    output << "  },\n";

    output << "  \"construction_state\": {\n";
    output << "    \"space_bound\": ";
    if (result.workspace == WorkspaceMethod::Linear)
        write_json_string(output, "O(n+m)");
    else
        write_json_string(output, "O(n+m log n)");
    output << ",\n";
    output << "    \"packed_neighbor_bits\": "
           << result.packed_neighbor_bits << ",\n";
    output << "    \"packed_neighbor_words\": "
           << result.packed_neighbor_words << ",\n";
    output << "    \"deleted_edge_words\": "
           << result.deleted_edge_words << ",\n";
    output << "    \"deletion_rank_entries\": "
           << result.deletion_rank_entries << ",\n";
    output << "    \"neighborhood_tree_nodes\": "
           << run.neighborhood_tree_nodes << "\n";
    output << "  },\n";

    output << "  \"timing\": {\n";
    output << "    \"input_index_seconds\": "
           << result.input_index_seconds << ",\n";
    output << "    \"selected_run_seconds\": " << run.total_seconds << ",\n";
    output << "    \"initialization_seconds\": "
           << run.initialization_seconds << ",\n";
    output << "    \"k_ge_2_stripping_seconds\": "
           << run.k_ge_2_stripping_seconds << ",\n";
    output << "    \"star_stripping_seconds\": "
           << run.star_stripping_seconds << "\n";
    output << "  }\n";
    output << "}\n";
}

}  // namespace

ReportSlot::ReportSlot(const filesystem::path& graph_path,
                       const FmOptions& options) {
    string stem = "fm-" + graph_name(graph_path);
    if (options.fixed_delta)
        stem += "-delta-" + delta_filename_text(options);

    for (uint64_t suffix = 1;
         suffix < numeric_limits<uint64_t>::max();
         ++suffix) {
        string filename =
            stem + (suffix == 1 ? "" : "-" + to_string(suffix)) + ".json";
        filesystem::path candidate = filesystem::current_path() / filename;
        filesystem::path claim = candidate.string() + ".claim";

        // mkdir is the reservation operation. Exactly one simultaneous process
        // can create this claim; the others continue to the next suffix.
        error_code error;
        if (!filesystem::create_directory(claim, error)) {
            if (error && error != errc::file_exists)
                throw runtime_error("Unable to reserve report name: " +
                                    error.message());
            continue;
        }

        error.clear();
        bool completed_report_exists = filesystem::exists(candidate, error);
        if (error) {
            error_code ignored;
            filesystem::remove(claim, ignored);
            throw runtime_error("Unable to inspect report path '" +
                                candidate.string() + "': " + error.message());
        }
        if (completed_report_exists) {
            filesystem::remove(claim, error);
            if (error)
                throw runtime_error("Unable to release report claim '" +
                                    claim.string() + "': " + error.message());
            continue;
        }

        report_path_ = candidate;
        claim_path_ = claim;
        return;
    }
    throw runtime_error("Unable to reserve an unused report filename");
}

ReportSlot::~ReportSlot() {
    if (claim_path_.empty()) return;
    error_code ignored;
    filesystem::remove(claim_path_, ignored);
}

void ReportSlot::publish(const FmResult& result) {
    if (published_)
        throw logic_error("Report slot has already been published");

    filesystem::path temporary_path = report_path_;
    temporary_path += ".tmp";

    try {
        ofstream output(temporary_path, ios::trunc);
        if (!output)
            throw runtime_error("Unable to open temporary report '" +
                                temporary_path.string() + "'");
        write_json(output, result);
        output.close();
        if (!output)
            throw runtime_error("Failed while writing temporary report '" +
                                temporary_path.string() + "'");

        error_code error;
        filesystem::rename(temporary_path, report_path_, error);
        if (error)
            throw runtime_error("Unable to publish report '" +
                                report_path_.string() + "': " + error.message());
    } catch (...) {
        error_code ignored;
        filesystem::remove(temporary_path, ignored);
        throw;
    }
    published_ = true;
}

}  // namespace fm_con
