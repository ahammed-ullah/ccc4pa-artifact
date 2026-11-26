#ifndef ZK_CON_ENCODE_HPP
#define ZK_CON_ENCODE_HPP

#include <cstdint>
#include <string>

namespace zk {

class CcfGraph;

struct EncodeStats {
    std::uint32_t n = 0;
    std::uint64_t m = 0;
    std::uint64_t graph_bytes = 0;
    std::uint64_t index_bytes = 0;
    std::uint64_t selected_references = 0;
};

// Writes basename.zk and basename.zki. Graph-level decoding belongs to
// zk_algo; this construction step records the output files and their hashes.
EncodeStats encode_graph(const CcfGraph& graph, const std::string& basename);

} // namespace zk

#endif
