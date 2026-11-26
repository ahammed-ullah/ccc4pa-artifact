#include "cc_con/first_fit.hpp"
#include "cc_con/algo_lzf.hpp"
#include "cc_con/sorted_vector_set.hpp"

#include <algorithm>
#include <vector>

using namespace std;

namespace cc_con {

void construct_lzf(const vector<vector<int>>& adj,
                   vector<vector<int>>& C) {
    C.clear();

    // First-fit partitions the vertices into complement color classes:
    //   h[v] is the first-fit class assigned to v.
    //   F[ell] is the ell-th color class of the complement, hence a clique in G.
    // The shared subroutine processes vertices in canonical vertex-id order. It
    // is called inside LZF so the reported construction time includes this step.
    vector<int> h;
    vector<vector<int>> F;
    first_fit_complement(adj, h, F);
    int n = (int)adj.size();

    // Add the non-singleton F[ell] classes to cover all within-class edges.
    // Singleton classes cover no edge and are therefore omitted.
    C.reserve(F.size());
    for (auto& F_ell : F) {
        if (F_ell.size() >= 2) C.push_back(F_ell);
    }

    // For every v in F[k] and every ell < k, LZF adds
    //
    //   {v} union S(v,ell), where S(v,ell) = F[ell] intersect N(v),
    //
    // whenever S(v,ell) is nonempty. For a fixed v, these sets partition its
    // lower-class neighbors. Scanning N(v) and grouping neighbors by h[u] forms
    // the sets directly, without separately intersecting every F[ell] with N(v).
    int p = (int)F.size();
    vector<int> S_size(p, 0);
    vector<int> clique_index(p, -1);
    vector<int> nonempty_labels;

    for (int v = 0; v < n; ++v) {
        int k = h[v];
        if (k == 0) continue;

        // The first scan finds the nonempty S(v,ell) sets and their exact sizes.
        nonempty_labels.clear();
        for (int u : adj[v]) {
            int ell = h[u];
            if (ell >= k) continue;
            if (S_size[ell]++ == 0) nonempty_labels.push_back(ell);
        }
        if (nonempty_labels.empty()) continue;

        // Process ell in increasing order so the output follows the construction's
        // deterministic (v,ell) order. Reserve the exact final clique sizes before
        // writing their members in the second scan.
        sort(nonempty_labels.begin(), nonempty_labels.end());
        for (int ell : nonempty_labels) {
            clique_index[ell] = (int)C.size();
            C.emplace_back();
            C.back().reserve(S_size[ell] + 1);
        }

        // Since adj[v] is sorted, appending its members leaves every S(v,ell)
        // sorted without a separate sort.
        for (int u : adj[v]) {
            int ell = h[u];
            if (ell < k) C[clique_index[ell]].push_back(u);
        }

        // Insert v into each completed set and clear only the scratch entries
        // touched for this vertex.
        for (int ell : nonempty_labels) {
            insert_sorted_unique(C[clique_index[ell]], v);
            S_size[ell] = 0;
            clique_index[ell] = -1;
        }
    }
}

}  // namespace cc_con
