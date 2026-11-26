#include "cc_con/verification.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <sstream>
#include <vector>

namespace cc_con {
namespace {

bool have_common_value(const std::vector<int>& lhs,
                       const std::vector<int>& rhs) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < lhs.size() && j < rhs.size()) {
        if (lhs[i] < rhs[j]) ++i;
        else if (rhs[j] < lhs[i]) ++j;
        else return true;
    }
    return false;
}

}  // namespace

bool verify_clique_cover(const std::vector<std::vector<int>>& adj,
                         const std::vector<std::vector<int>>& C,
                         std::string& diagnostic) {
    if (C.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        diagnostic = "Cover has too many labels for int";
        return false;
    }

    // Build L while checking that each emitted vector really is a clique.
    // Verification is deliberately thorough and runs outside construction time.
    std::vector<std::vector<int>> L(adj.size());
    for (std::size_t label_index = 0; label_index < C.size(); ++label_index) {
        const int ell = static_cast<int>(label_index);
        const std::vector<int>& clique = C[label_index];
        if (clique.size() < 2) {
            diagnostic = "Cover contains a clique with fewer than two vertices";
            return false;
        }

        for (std::size_t i = 0; i < clique.size(); ++i) {
            const int u = clique[i];
            if (u < 0 || static_cast<std::size_t>(u) >= adj.size()) {
                diagnostic = "Cover contains a vertex outside the graph";
                return false;
            }
            if (i > 0 && clique[i - 1] >= u) {
                diagnostic = "Cover clique is not sorted and duplicate-free";
                return false;
            }
            L[u].push_back(ell);

            for (std::size_t j = i + 1; j < clique.size(); ++j) {
                if (!std::binary_search(adj[u].begin(), adj[u].end(), clique[j])) {
                    diagnostic = "Cover contains a set that is not a clique";
                    return false;
                }
            }
        }
    }

    // Labels entered L in increasing order. A merge intersection therefore
    // tests edge coverage without constructing a temporary set.
    for (std::size_t u_index = 0; u_index < adj.size(); ++u_index) {
        const int u = static_cast<int>(u_index);
        for (const int v : adj[u]) {
            if (u < v && !have_common_value(L[u], L[v])) {
                std::ostringstream message;
                message << "Edge {" << u << ',' << v << "} is uncovered";
                diagnostic = message.str();
                return false;
            }
        }
    }

    diagnostic = "valid clique cover";
    return true;
}

bool verify_composition_minimal(const std::vector<std::vector<int>>& adj,
                                const std::vector<std::vector<int>>& C,
                                std::string& diagnostic) {
    // Two cliques cannot be composed when their union contains a nonedge. Find
    // such a witness for every pair; no witness means that pair can be merged.
    for (std::size_t i = 0; i < C.size(); ++i) {
        for (std::size_t j = i + 1; j < C.size(); ++j) {
            bool found_nonedge = false;
            for (const int u : C[i]) {
                for (const int v : C[j]) {
                    if (u != v &&
                        !std::binary_search(adj[u].begin(), adj[u].end(), v)) {
                        found_nonedge = true;
                        break;
                    }
                }
                if (found_nonedge) break;
            }
            if (!found_nonedge) {
                diagnostic = "Two cover cliques can be composed";
                return false;
            }
        }
    }
    diagnostic = "composition-minimal";
    return true;
}

}  // namespace cc_con
