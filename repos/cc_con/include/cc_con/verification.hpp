#ifndef CC_CON_VERIFICATION_HPP
#define CC_CON_VERIFICATION_HPP

#include <string>
#include <vector>

namespace cc_con {

// Checks that every C[ell] is a sorted clique and every graph edge is covered.
// The diagnostic is set to the first failure, or "valid clique cover" on success.
bool verify_clique_cover(const std::vector<std::vector<int>>& adj,
                         const std::vector<std::vector<int>>& C,
                         std::string& diagnostic);

// Checks that no two distinct cover cliques have a union that is also a clique.
// Algorithm tests use it for BPA, FPA, and CPA.
bool verify_composition_minimal(const std::vector<std::vector<int>>& adj,
                                const std::vector<std::vector<int>>& C,
                                std::string& diagnostic);

}  // namespace cc_con

#endif
