#ifndef CC_CON_CGF_HPP
#define CC_CON_CGF_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cc_con {

constexpr std::uint32_t kCgfFormatVersion = 1;
constexpr std::uint32_t kDeltaVbyteEncoding = 1;

struct CgfArtifactInfo {
    std::uint64_t n = 0;
    std::uint64_t m = 0;
    std::uint64_t adjacency_entries = 0;
    std::uint64_t file_bytes = 0;
    std::string graph_fingerprint_sha256;
    std::string file_checksum_sha256;
};

// Computes an ID-sensitive fingerprint of the canonical graph. The logical
// serialization is documented with CGF so independent readers can reproduce it
// without reproducing the on-disk adjacency encoding.
std::string canonical_graph_fingerprint_sha256(
    const std::vector<std::vector<int>>& adj,
    long long m);

// read_cgf fully decodes a file previously produced by cg_prepare. It checks the
// counts and offsets needed to keep every read within the file, but deliberately
// does not hash the file, validate symmetry, or recompute the logical graph
// fingerprint. read_and_validate_cgf performs those additional checks for
// cc_check.
CgfArtifactInfo write_cgf(const std::filesystem::path& path,
                          const std::vector<std::vector<int>>& adj,
                          long long m,
                          const std::string& graph_fingerprint_sha256);
CgfArtifactInfo read_cgf(const std::filesystem::path& path,
                         std::vector<std::vector<int>>& adj);
CgfArtifactInfo read_and_validate_cgf(
    const std::filesystem::path& path,
    std::vector<std::vector<int>>& adj);

}  // namespace cc_con

#endif
