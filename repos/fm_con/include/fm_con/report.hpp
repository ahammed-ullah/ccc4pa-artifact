#ifndef FM_CON_REPORT_HPP
#define FM_CON_REPORT_HPP

#include "fm_con/fm.hpp"

#include <filesystem>
#include <string>

namespace fm_con {

// Reserves one report name before the FM construction starts. The claim
// directory is created atomically, so simultaneous runs with the same graph,
// delta, and working directory receive distinct numeric suffixes.
class ReportSlot {
public:
    ReportSlot(const std::filesystem::path& graph_path,
               const FmOptions& options);
    ReportSlot(const ReportSlot&) = delete;
    ReportSlot& operator=(const ReportSlot&) = delete;
    ~ReportSlot();

    const std::filesystem::path& path() const { return report_path_; }
    void publish(const FmResult& result);

private:
    std::filesystem::path report_path_;
    std::filesystem::path claim_path_;
    bool published_ = false;
};

}  // namespace fm_con

#endif
