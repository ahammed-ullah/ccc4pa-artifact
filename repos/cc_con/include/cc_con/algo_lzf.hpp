#ifndef CC_CON_ALGO_LZF_HPP
#define CC_CON_ALGO_LZF_HPP

#include <vector>

namespace cc_con {

// First-fit variant of Lovasz's construction. adj is the canonical graph:
// simple, undirected, without isolated vertices, and represented by sorted,
// duplicate-free open adjacency lists. The function writes the cover directly
// into C and has no algorithm parameters or algorithm-specific statistics.
void construct_lzf(const std::vector<std::vector<int>>& adj,
                   std::vector<std::vector<int>>& C);

}  // namespace cc_con

#endif
