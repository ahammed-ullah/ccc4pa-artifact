#ifndef ZK_CON_CCF_GRAPH_HPP
#define ZK_CON_CCF_GRAPH_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zk {

// A CCF cover, with an incidence list for each vertex. The cliques and all
// vertex IDs use the encoding order. A missing permutation means that this is
// also the original CCF order.
class CcfGraph {
public:
    explicit CcfGraph(const std::string& ccf_path,
                      const std::string& perm_path = "");

    uint32_t n() const { return n_; }
    uint64_t m() const { return m_; }
    const std::string& fingerprint() const { return fingerprint_; }

    // Fill out with the sorted, distinct open neighborhood of encoded_vertex.
    // The caller may reuse out for the next vertex; no neighborhoods are cached.
    void neighbors(uint32_t encoded_vertex, std::vector<uint32_t>& out) const;

    // Empty for original order; otherwise new vertex ID -> canonical vertex ID.
    const std::vector<uint32_t>& permutation() const { return new_to_old_; }

private:
    uint32_t n_ = 0;
    uint64_t m_ = 0;
    std::string fingerprint_;
    std::vector<uint32_t> new_to_old_;

    // Clique c occupies vertices_[clique_offsets_[c]..clique_offsets_[c+1]).
    std::vector<uint32_t> vertices_;
    std::vector<size_t> clique_offsets_;

    // Vertex u belongs to the cliques in
    // incidence_[incidence_offsets_[u]..incidence_offsets_[u+1]).
    std::vector<uint32_t> incidence_;
    std::vector<size_t> incidence_offsets_;
};

}  // namespace zk

#endif
