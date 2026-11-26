#ifndef CC_CON_REPORT_HPP
#define CC_CON_REPORT_HPP

#include "cc_con/config.hpp"
#include "cc_con/graph.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace cc_con {

// The report structure is deliberately flat and mirrors the values assembled in
// main.cpp. It is not passed to a construction and adds no algorithmic indirection.
struct RunReport {
    std::string algorithm;
    std::map<std::string, ConfigValue> algorithm_parameters;
    std::map<std::string, ConfigValue> algorithm_statistics;

    GraphMetadata graph;
    long long n = 0;
    long long m = 0;
    double graph_time_seconds = 0.0;

    long long constructed_num_cliques = 0;
    long long constructed_cover_size = 0;
    double construction_time_seconds = 0.0;

    bool assignment_minimal = false;
    long long postprocessed_num_cliques = 0;
    long long postprocessed_cover_size = 0;
    double postprocessing_time_seconds = 0.0;

    bool output_written = false;
    std::string output_path;
    std::uint64_t encoded_bytes = 0;
    std::string output_checksum_sha256;
    double output_time_seconds = 0.0;

    bool verification_explicitly_requested = false;
    bool verification_performed = false;
    std::string verification_trigger;
    bool verification_passed = false;
    double verification_time_seconds = 0.0;
};

// Reserves one report name in the current directory. Creating the claim
// directory is atomic, so simultaneous runs of the same algorithm and graph
// receive distinct numeric suffixes. The final JSON file becomes visible only
// after publish() has written the complete document successfully.
class ReportSlot {
public:
    ReportSlot(const std::string& algorithm, const std::string& graph_name);
    ReportSlot(const ReportSlot&) = delete;
    ReportSlot& operator=(const ReportSlot&) = delete;
    ~ReportSlot();

    const std::filesystem::path& path() const { return report_path_; }
    void publish(const RunReport& report);

private:
    std::filesystem::path report_path_;
    std::filesystem::path claim_path_;
    bool published_ = false;
};

}  // namespace cc_con

#endif
