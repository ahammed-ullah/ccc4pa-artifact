#include "cc_con/bound.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>

using namespace std;

namespace cc_con {
namespace {

constexpr uint32_t kBoundReportSchemaVersion = 2;

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

void write_report_json(ostream& output, const BoundReport& report) {
    output << setprecision(15);
    output << "{\n";
    output << "  \"schema_version\": " << kBoundReportSchemaVersion << ",\n";

    output << "  \"algorithm\": {\n";
    output << "    \"method\": ";
    write_json_string(output, report.method);
    output << ",\n";
    output << "    \"exact_truss\": "
           << (report.exact_truss ? "true" : "false") << '\n';
    output << "  },\n";

    output << "  \"graph\": {\n";
    output << "    \"name\": ";
    write_json_string(output, report.graph_name);
    output << ",\n";
    output << "    \"path\": ";
    write_json_string(output, report.graph_path);
    output << ",\n";
    output << "    \"format\": \"cgf\",\n";
    output << "    \"n\": " << report.n << ",\n";
    output << "    \"m\": " << report.m << ",\n";
    output << "    \"encoded_bytes\": " << report.cgf_bytes << ",\n";
    output << "    \"read_time_seconds\": "
           << report.graph_read_seconds << '\n';
    output << "  },\n";

    output << "  \"lower_bounds\": {\n";
    size_t written = 0;
    for (const auto& [name, value] : report.result.lower_bounds) {
        output << "    ";
        write_json_string(output, name);
        output << ": " << value;
        if (++written < report.result.lower_bounds.size()) output << ',';
        output << '\n';
    }
    output << "  },\n";

    const BoundTimings& timing = report.result.timings;
    output << "  \"timing_seconds\": {\n";
    output << "    \"linear_bounds\": " << timing.linear_seconds << ",\n";
    output << "    \"common_neighborhood\": "
           << timing.common_neighborhood_seconds << ",\n";
    output << "    \"local_mis\": " << timing.local_mis_seconds << ",\n";
    output << "    \"exact_truss\": " << timing.truss_seconds << ",\n";
    output << "    \"local_color\": " << timing.local_color_seconds << ",\n";
    output << "    \"bound_computation\": " << timing.total_seconds << ",\n";
    output << "    \"total_including_graph_read\": "
           << timing.total_seconds + report.graph_read_seconds << '\n';
    output << "  }\n";
    output << "}\n";
}

bool path_exists(const filesystem::path& path) {
    error_code error;
    bool exists = filesystem::exists(path, error);
    if (error) {
        throw runtime_error("Unable to inspect report path '" +
                            path.string() + "': " + error.message());
    }
    return exists;
}

filesystem::path choose_report_path(const BoundReport& report) {
    string stem = "bound-" + report.method;
    if (report.exact_truss) stem += "-truss";
    stem += "-" + report.graph_name;

    filesystem::path directory = filesystem::current_path();
    filesystem::path path = directory / (stem + ".json");
    if (!path_exists(path)) return path;

    for (uint64_t suffix = 2;
         suffix < numeric_limits<uint64_t>::max();
         ++suffix) {
        path = directory / (stem + "-" + to_string(suffix) + ".json");
        if (!path_exists(path)) return path;
    }
    throw runtime_error("Unable to choose an unused bound-report filename");
}

}  // namespace

filesystem::path write_bound_report(const BoundReport& report) {
    filesystem::path final_path = choose_report_path(report);
    filesystem::path temporary_path = final_path;
    temporary_path += ".tmp";

    try {
        ofstream output(temporary_path, ios::trunc);
        if (!output) {
            throw runtime_error("Unable to open temporary report '" +
                                temporary_path.string() + "'");
        }
        write_report_json(output, report);
        output.close();
        if (!output) {
            throw runtime_error("Failed while writing temporary report '" +
                                temporary_path.string() + "'");
        }

        error_code error;
        filesystem::rename(temporary_path, final_path, error);
        if (error) {
            throw runtime_error("Unable to publish report '" +
                                final_path.string() + "': " + error.message());
        }
    } catch (...) {
        error_code ignored;
        filesystem::remove(temporary_path, ignored);
        throw;
    }
    return final_path;
}

}  // namespace cc_con
