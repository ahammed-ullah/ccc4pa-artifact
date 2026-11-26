#include "wg_algo/report.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

using namespace std;

namespace wg_algo {
namespace {

string json_string(const string& value) {
    ostringstream out;
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << hex << setw(4) << setfill('0')
                        << unsigned(ch) << dec;
                } else out << ch;
        }
    }
    out << '"';
    return out.str();
}

string safe_stem(const filesystem::path& graph) {
    string name = graph.stem().string();
    if (name.empty()) name = "graph";
    for (char& ch : name) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) ch = '_';
    }
    return name;
}

}  // namespace

ReportSlot::ReportSlot(const string& algorithm, const filesystem::path& graph) {
    const string stem = algorithm + "-" + safe_stem(graph);
    for (size_t suffix = 1;; ++suffix) {
        const auto filename = stem + (suffix == 1 ? "" : "-" + to_string(suffix)) + ".json";
        const auto candidate = filesystem::current_path() / filename;
        const auto claim = filesystem::path(candidate.string() + ".claim");
        error_code error;
        if (!filesystem::create_directory(claim, error)) {
            if (error && error != errc::file_exists)
                throw runtime_error("cannot reserve report name: " + error.message());
            continue;
        }
        if (filesystem::exists(candidate)) {
            filesystem::remove(claim);
            continue;
        }
        report_path_ = candidate;
        claim_path_ = claim;
        break;
    }
}

ReportSlot::~ReportSlot() {
    if (!claim_path_.empty()) {
        error_code ignored;
        filesystem::remove(claim_path_, ignored);
    }
}

void ReportSlot::publish(const RunReport& report) {
    if (published_) throw logic_error("report already published");
    const auto temp = filesystem::path(report_path_.string() + ".writing");
    ofstream out(temp, ios::binary | ios::trunc);
    if (!out) throw runtime_error("cannot write report: " + temp.string());
    const auto input = filesystem::absolute(report.graph_path).lexically_normal();
    const char* job_id = getenv("SLURM_JOB_ID");
    out << "{\n"
        << "  \"algorithm\": " << json_string(report.algorithm) << ",\n"
        << "  \"representation\": \"webgraph\",\n"
        << "  \"parameters\": {\"decoder\": "
        << json_string(report.decoder == Decoder::Bulk ? "bulk" : "iterator");
    if (report.algorithm == "matching") {
        out << ", \"matching_variant\": "
            << json_string(report.matching_variant == MatchingVariant::Maximal
                           ? "maximal" : "three_halves");
    } else if (report.algorithm == "wvc" || report.algorithm == "mvm") {
        if (report.weights_file) {
            out << ", \"weights_file\": " << json_string(report.weights_file->string());
        } else {
            out << ", \"weight_generator\": \"mt19937_64_rejection\", "
                << "\"weight_seed\": 19, \"weight_range\": "
                << json_string("[1,n^2]");
        }
    }
    if (report.mvm) {
        out << ", \"mvm_epsilon\": " << setprecision(17)
            << report.mvm->epsilon
            << ", \"mvm_seed\": " << report.mvm->seed
            << ", \"mvm_sample_limit\": " << report.mvm->sample_limit;
    }
    out << "},\n"
        << "  \"input\": {\n"
        << "    \"graph_path\": " << json_string(input.string()) << ",\n"
        << "    \"graph_name\": " << json_string(input.stem().string()) << ",\n"
        << "    \"n\": " << report.n << ",\n"
        << "    \"m\": " << report.arcs / 2 << ",\n"
        << "    \"arcs\": " << report.arcs << ",\n"
        << "    \"graph_bytes\": " << report.graph_bytes << ",\n"
        << "    \"properties_bytes\": " << report.properties_bytes << ",\n"
        << "    \"ef_bytes\": " << report.ef_bytes << ",\n"
        << "    \"perm_bytes\": " << report.perm_bytes << ",\n"
        << "    \"bundle_bytes\": "
        << report.graph_bytes + report.properties_bytes + report.ef_bytes + report.perm_bytes
        << "\n"
        << "  },\n"
        << "  \"config_path\": "
        << (report.config_path ? json_string(report.config_path->string()) : "null") << ",\n"
        << "  \"slurm_job_id\": "
        << (job_id ? json_string(job_id) : "null") << ",\n"
        << "  \"result\": {" << json_string(report.result_key) << ": "
        << (report.result_is_decimal_string ? json_string(report.result_value)
                                            : report.result_value) << "},\n";
    if (report.mvm) {
        out << "  \"mvm_execution\": {\"status\": "
            << json_string(report.mvm->time_limit_partial
                           ? "time_limit_partial" : "complete")
            << ", \"stop_reason\": "
            << json_string(report.mvm->time_limit_partial ? "slurm_warning" :
                           report.mvm->all_vertices_matched ? "all_vertices_matched" :
                           "sample_limit")
            << ", \"samples_completed\": " << report.mvm->samples_completed
            << ", \"positive_augmentations\": "
            << report.mvm->positive_augmentations << "},\n";
    }
    out << "  \"memory\": {\"process_peak_rss_bytes\": "
        << report.process_peak_rss_bytes << "},\n"
        << "  \"timing_seconds\": {\n"
        << fixed << setprecision(9)
        << "    \"webgraph_load\": " << report.webgraph_load_seconds << ",\n";
    if (report.weight_preparation_seconds)
        out << "    \"weight_preparation\": " << *report.weight_preparation_seconds << ",\n";
    out << "    \"algorithm\": " << report.algorithm_seconds << "\n"
        << "  }\n"
        << "}\n";
    out.close();
    if (!out) throw runtime_error("failed writing report: " + temp.string());
    filesystem::rename(temp, report_path_);
    published_ = true;
}

}  // namespace wg_algo
