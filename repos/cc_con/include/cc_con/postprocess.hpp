#ifndef CC_CON_POSTPROCESS_HPP
#define CC_CON_POSTPROCESS_HPP

#include <vector>

namespace cc_con {

// Greedily removes an assignment (v,C[ell]) whenever every edge represented by
// it is still covered elsewhere. C is changed in place because raw statistics
// are collected before this optional stage. See docs/assignment-minimal.md.
void make_assignment_minimal(std::vector<std::vector<int>>& C, int n);

}  // namespace cc_con

#endif
