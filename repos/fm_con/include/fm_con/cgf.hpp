#ifndef FM_CON_CGF_HPP
#define FM_CON_CGF_HPP

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fm_con {

constexpr std::uint32_t kCgfFormatVersion = 1;

struct CgfInfo {
    std::filesystem::path path;
    std::uint64_t n = 0;
    std::uint64_t m = 0;
    std::uint64_t adjacency_entries = 0;
    std::uint64_t offsets_offset = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t file_bytes = 0;
    std::string graph_fingerprint_sha256;
    std::string file_checksum_sha256;
};

// A CGF pass keeps independent positions in the offset table and payload. The
// reader is concrete rather than a generic graph interface because CGF is the
// only input accepted by this project.
struct CgfReader {
    const CgfInfo* info = nullptr;
    std::ifstream offsets;
    std::ifstream payload;
    std::uint64_t record_begin = 0;
    std::uint64_t decoded_entries = 0;
    int next_vertex = 0;
};

CgfInfo read_cgf_info(const std::filesystem::path& path);
CgfReader open_cgf(const CgfInfo& info);

// These two operations are used by different FM paths. The k=1 path needs only
// cardinalities; the general path decodes the sorted neighbor IDs used to build
// one neighborhood tree.
std::uint32_t read_cgf_degree(CgfReader& reader);
void read_cgf_neighborhood(CgfReader& reader, std::vector<int>& neighbors);
void finish_cgf_pass(CgfReader& reader);

}  // namespace fm_con

#endif

