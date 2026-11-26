#include "cc_con/first_fit.hpp"

#include <cstddef>
#include <vector>

namespace cc_con {

void first_fit_complement(const std::vector<std::vector<int>>& adj,
                          std::vector<int>& h,
                          std::vector<std::vector<int>>& F) {
    const int n = static_cast<int>(adj.size());
    h.assign(static_cast<std::size_t>(n), -1);
    F.clear();

    // class_size[ell] is |F[ell]|. For the current vertex v,
    // neighbor_count[ell] records |N(v) intersect F[ell]|. Thus F[ell] can
    // accept v precisely when these two quantities are equal: v is adjacent in
    // G to every current member of the class, so F[ell] union {v} is a clique.
    std::vector<int> class_size;
    std::vector<int> neighbor_count;

    // seen_at_vertex[ell] == v means neighbor_count[ell] belongs to the current
    // vertex. Using v itself as a timestamp avoids clearing every class counter
    // before each iteration. touched_classes then lists exactly the counters
    // initialized for v.
    std::vector<int> seen_at_vertex;
    std::vector<int> touched_classes;

    for (int v = 0; v < n; ++v) {
        touched_classes.clear();

        // Only earlier vertices have class labels. Canonical adjacency lists are
        // sorted, so the scan stops as soon as it reaches u >= v.
        for (const int u : adj[v]) {
            if (u >= v) break;

            const int ell = h[u];
            if (seen_at_vertex[ell] != v) {
                seen_at_vertex[ell] = v;
                neighbor_count[ell] = 1;
                touched_classes.push_back(ell);
            } else {
                ++neighbor_count[ell];
            }
        }

        // First fit selects the smallest existing class that can accept v. An
        // accepting class is nonempty and must therefore occur in
        // touched_classes; scanning untouched labels is unnecessary.
        int chosen = -1;
        for (const int ell : touched_classes) {
            if (neighbor_count[ell] == class_size[ell] &&
                (chosen < 0 || ell < chosen)) {
                chosen = ell;
            }
        }

        if (chosen < 0) {
            // No existing clique class accepts v, so first fit creates the next
            // label. Labels are contiguous and F remains in greedy order.
            chosen = static_cast<int>(F.size());
            F.emplace_back();
            class_size.push_back(0);
            neighbor_count.push_back(0);
            seen_at_vertex.push_back(-1);
        }

        h[v] = chosen;
        F[chosen].push_back(v);
        ++class_size[chosen];
    }
}

}  // namespace cc_con
