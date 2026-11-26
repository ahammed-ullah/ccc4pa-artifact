#ifndef ZK_CON_FORMAT_HPP
#define ZK_CON_FORMAT_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace zk {

// This experiment has one format. The numbers below are checked in every file
// so that a decoder built for different Zuckerli settings cannot read it.
constexpr std::uint32_t window = 128;
constexpr std::uint32_t chunk_size = 1;
constexpr std::uint32_t zero_run_min = 3;
constexpr std::uint32_t hybrid_k = 3;
constexpr std::uint32_t hybrid_i = 1;
constexpr std::uint32_t hybrid_j = 0;
constexpr std::uint32_t reference_rounds = 2;
constexpr std::uint32_t graph_header_bytes = 64;

struct GraphHeader {
    std::uint32_t n = 0;
    std::uint64_t m = 0; // undirected edges; the stream contains 2m arcs
    std::string fingerprint; // 64 hexadecimal digits, in canonical IDs
};

void write_graph_header(const std::string& path, const GraphHeader& header);

// The index holds the start bit of every row and one final end bit. Its values
// have a fixed width chosen from the last bit position.
void write_index(const std::string& path, const std::vector<std::uint64_t>& offsets);

} // namespace zk

#endif
