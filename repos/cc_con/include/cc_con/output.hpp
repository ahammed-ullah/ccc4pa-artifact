#ifndef CC_CON_OUTPUT_HPP
#define CC_CON_OUTPUT_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cc_con {

constexpr std::uint32_t kCcfFormatVersion = 1;

struct CcfArtifactInfo {
    std::uint64_t n = 0;
    std::uint64_t m = 0;
    std::uint64_t num_cliques = 0;
    std::uint64_t cover_size = 0;
    std::uint64_t file_bytes = 0;
    std::string graph_fingerprint_sha256;
    std::string file_checksum_sha256;
};

// CCF stores one independently decodable clique per record. The fixed header is
// followed by R+1 offsets and cardinality-prefixed, sorted-id delta-vbyte data.
CcfArtifactInfo write_ccf(
    const std::filesystem::path& path,
    const std::vector<std::vector<int>>& C,
    int n,
    long long m,
    const std::string& graph_fingerprint_sha256);

// Performs graph-independent structural/integrity validation and decodes the
// cliques. Exact clique and edge-coverage checks require the graph and remain in
// verify_clique_cover().
CcfArtifactInfo read_and_validate_ccf(
    const std::filesystem::path& path,
    std::vector<std::vector<int>>& C);

}  // namespace cc_con

#endif
