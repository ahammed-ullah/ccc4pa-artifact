#include "cc_con/postprocess.hpp"
#include "cc_con/sorted_vector_set.hpp"

#include <algorithm>
#include <utility>
#include <vector>

using namespace std;

namespace cc_con {

void make_assignment_minimal(vector<vector<int>>& C, int n) {
    int p = (int)C.size();

    // C[ell] is clique ell, and L[v] contains its clique labels. Sorting and
    // deduplication establish the representation needed by sorted-vector
    // erasure. Since labels are visited in increasing order, every L[v] is also
    // produced in increasing order.
    vector<vector<int>> L(n);
    for (int ell = 0; ell < p; ++ell) {
        auto& C_ell = C[ell];
        sort(C_ell.begin(), C_ell.end());
        C_ell.erase(unique(C_ell.begin(), C_ell.end()), C_ell.end());
        for (int v : C_ell) {
            L[v].push_back(ell);
        }
    }

    vector<int> multiplicity(n, 0);
    vector<int> touched;

    for (int v = 0; v < n; ++v) {
        if (L[v].empty()) continue;

        // At the start of v's iteration, no earlier iteration could have
        // removed v, so L[v] is exactly the current set of labels containing v.
        // For every other vertex u, compute
        //
        //   multiplicity[u] = |{ell in L[v] : u is in C[ell]}|,
        //
        // the current number of cliques covering {u,v}. Only entries reached
        // by these cliques are reset, avoiding an O(n) clear for every v.
        touched.clear();
        for (int ell : L[v]) {
            for (int u : C[ell]) {
                if (u == v) continue;
                if (multiplicity[u] == 0) touched.push_back(u);
                ++multiplicity[u];
            }
        }

        for (int ell : L[v]) {
            auto& C_ell = C[ell];
            bool removable = true;

            // Assignment (v,C[ell]) is removable exactly when every edge
            // {u,v} represented by it has multiplicity at least two.
            for (int u : C_ell) {
                if (u != v && multiplicity[u] == 1) {
                    removable = false;
                    break;
                }
            }
            if (!removable) continue;

            erase_sorted_if_found(C_ell, v);
            for (int u : C_ell) --multiplicity[u];
        }

        // L[v] is deliberately not updated as assignments of v are removed.
        // Its iteration is already complete, and later vertices discover the
        // current clique members by scanning C rather than consulting L[v].
        for (int u : touched) multiplicity[u] = 0;
    }

    // Empty and singleton sets cover no edge. Compact only after processing all
    // assignments because L uses the original clique labels throughout.
    int write = 0;
    for (int read = 0; read < p; ++read) {
        if (C[read].size() < 2) continue;
        if (write != read) C[write] = std::move(C[read]);
        ++write;
    }
    C.resize(write);
}

}  // namespace cc_con
