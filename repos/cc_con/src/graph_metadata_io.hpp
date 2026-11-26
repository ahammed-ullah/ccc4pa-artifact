#ifndef CC_CON_GRAPH_METADATA_IO_HPP
#define CC_CON_GRAPH_METADATA_IO_HPP

#include "cc_con/cgf.hpp"
#include "cc_con/graph.hpp"

#include <cstdint>
#include <filesystem>

namespace cc_con {

struct GraphPreparationRecord {
    GraphMetadata metadata;
    std::uint64_t n = 0;
    std::uint64_t m = 0;
    CgfArtifactInfo artifact;
    double graph_time_seconds = 0.0;
    double encoding_and_write_time_seconds = 0.0;
};

void write_graph_preparation_report(
    const std::filesystem::path& report_path,
    const std::filesystem::path& artifact_path,
    const GraphPreparationRecord& record);
GraphPreparationRecord read_graph_preparation_report(
    const std::filesystem::path& report_path);

// cg_prepare writes this last. Its relative paths make the three generated files
// movable as one directory and directly consumable by cc_con.
void write_prepared_graph_specification(
    const std::filesystem::path& specification_path,
    const std::string& graph_name,
    const std::filesystem::path& artifact_path,
    const std::filesystem::path& report_path);

}  // namespace cc_con

#endif
