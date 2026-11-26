#ifndef CC_CON_COMPLEMENT_DEGENERACY_HPP
#define CC_CON_COMPLEMENT_DEGENERACY_HPP

#include <vector>

namespace cc_con {

// Compute a degeneracy coloring of the complement without constructing the
// complement graph. F contains the resulting color classes, which are cliques
// of the input graph. The return value is the degeneracy of the complement.
// Both the peel and the greedy coloring are included in a construction's timed
// call whenever this subroutine is used.
int complement_degeneracy_coloring(
    const std::vector<std::vector<int>>& adj,
    std::vector<std::vector<int>>& F);

}  // namespace cc_con

#endif
