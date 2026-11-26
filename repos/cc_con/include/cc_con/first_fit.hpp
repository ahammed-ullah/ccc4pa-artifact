#ifndef CC_CON_FIRST_FIT_HPP
#define CC_CON_FIRST_FIT_HPP

#include <vector>

namespace cc_con {

// Compute a first-fit greedy coloring of the complement without constructing
// the complement graph. adj must be the canonical graph: every adjacency list
// is sorted and duplicate-free, and vertex identifiers are 0,...,n-1. Vertices
// are processed in that canonical id order.
//
// On return:
//   h[v] is the zero-based class label assigned to v;
//   F[ell] contains the vertices assigned label ell, in increasing id order.
//
// Every F[ell] is therefore a clique in the input graph. Both output vectors are
// replaced rather than appended to, allowing callers to reuse their storage.
void first_fit_complement(const std::vector<std::vector<int>>& adj,
                          std::vector<int>& h,
                          std::vector<std::vector<int>>& F);

}  // namespace cc_con

#endif
