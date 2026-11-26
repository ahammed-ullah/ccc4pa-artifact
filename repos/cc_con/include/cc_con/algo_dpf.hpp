#ifndef CC_CON_ALGO_DPF_HPP
#define CC_CON_ALGO_DPF_HPP

#include <vector>

namespace cc_con {

struct DpfStatistics {
    int degeneracy = 0;
};

// Degeneracy Pivoted First-Fit. adj is the canonical graph: simple,
// undirected, isolate-free, and stored as sorted adjacency vectors. DPF has no
// construction parameters. It writes sorted vertex lists directly into C and
// reports the degeneracy of adj.
void construct_dpf(const std::vector<std::vector<int>>& adj,
                   std::vector<std::vector<int>>& C,
                   DpfStatistics& statistics);

}  // namespace cc_con

#endif
