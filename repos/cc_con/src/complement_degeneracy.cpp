#include "cc_con/complement_degeneracy.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace cc_con {

int complement_degeneracy_coloring(
    const std::vector<std::vector<int>>& adj,
    std::vector<std::vector<int>>& F) {
    const int n = static_cast<int>(adj.size());
    F.clear();
    if (n == 0) return 0;

    // A minimum-degree peel in the complement is a maximum-degree peel in G:
    // for a current induced graph on s vertices,
    //
    //   degree_complement(v) = s - 1 - degree_G(v).
    //
    // The degree buckets below are intrusive linked lists. Each active vertex
    // occurs in exactly one bucket, and decrementing a neighbor moves it in
    // O(1) time. This uses four flat O(n) integer arrays and avoids both a heap's
    // logarithmic updates and the O(m) stale entries of a lazy bucket queue.
    std::vector<int> degree(static_cast<std::size_t>(n));
    std::vector<int> bucket_head(static_cast<std::size_t>(n), -1);
    std::vector<int> previous(static_cast<std::size_t>(n), -1);
    std::vector<int> next(static_cast<std::size_t>(n), -1);

    int maximum_degree = 0;
    for (int v = 0; v < n; ++v) {
        degree[v] = static_cast<int>(adj[v].size());
        maximum_degree = std::max(maximum_degree, degree[v]);

        next[v] = bucket_head[degree[v]];
        if (next[v] >= 0) previous[next[v]] = v;
        bucket_head[degree[v]] = v;
    }

    std::vector<int> peel_order;
    peel_order.reserve(static_cast<std::size_t>(n));
    int complement_degeneracy = 0;

    for (int step = 0; step < n; ++step) {
        while (maximum_degree >= 0 && bucket_head[maximum_degree] < 0) {
            --maximum_degree;
        }

        // Remove one maximum-degree vertex from the current induced graph.
        // Vertices inserted later are at the bucket head, making all tie choices
        // deterministic for a fixed canonical adjacency list.
        const int v = bucket_head[maximum_degree];
        bucket_head[maximum_degree] = next[v];
        if (next[v] >= 0) previous[next[v]] = -1;

        const int active_vertices = n - step;
        const int complement_degree =
            active_vertices - 1 - degree[v];
        complement_degeneracy =
            std::max(complement_degeneracy, complement_degree);
        peel_order.push_back(v);
        degree[v] = -1;  // negative means that v is no longer active

        // Only active graph neighbors lose degree. Remove each one from its old
        // bucket and insert it at the head of the next lower bucket.
        for (const int u : adj[v]) {
            if (degree[u] < 0) continue;

            const int old_degree = degree[u];
            if (previous[u] >= 0) {
                next[previous[u]] = next[u];
            } else {
                bucket_head[old_degree] = next[u];
            }
            if (next[u] >= 0) previous[next[u]] = previous[u];

            --degree[u];
            previous[u] = -1;
            next[u] = bucket_head[degree[u]];
            if (next[u] >= 0) previous[next[u]] = u;
            bucket_head[degree[u]] = u;
        }
    }

    // Reversing a minimum-degree peel gives the convention used by BCC and CPA:
    // each vertex has few earlier complement neighbors. We color
    // the complement greedily in that order, but test a color through G: class
    // F[ell] accepts v exactly when v is adjacent in G to every member of it.
    std::reverse(peel_order.begin(), peel_order.end());

    std::vector<int> color(static_cast<std::size_t>(n), -1);
    std::vector<int> neighbor_count;
    std::vector<int> seen_at_vertex;
    std::vector<int> touched_colors;

    for (const int v : peel_order) {
        touched_colors.clear();
        for (const int u : adj[v]) {
            const int ell = color[u];
            if (ell < 0) continue;  // u occurs later in the coloring order

            if (seen_at_vertex[ell] != v) {
                seen_at_vertex[ell] = v;
                neighbor_count[ell] = 1;
                touched_colors.push_back(ell);
            } else {
                ++neighbor_count[ell];
            }
        }

        // A nonempty class that accepts v must be touched by an edge from v.
        // Looking only at touched colors therefore avoids scanning every color
        // on every vertex while still choosing the smallest valid color.
        int chosen = -1;
        for (const int ell : touched_colors) {
            if (neighbor_count[ell] == static_cast<int>(F[ell].size()) &&
                (chosen < 0 || ell < chosen)) {
                chosen = ell;
            }
        }

        if (chosen < 0) {
            chosen = static_cast<int>(F.size());
            F.emplace_back();
            neighbor_count.push_back(0);
            seen_at_vertex.push_back(-1);
        }
        color[v] = chosen;
        F[chosen].push_back(v);
    }

    // Coloring order is unrelated to canonical vertex id. Sorting each class
    // once lets every later BCC set operation and emitted clique use the same
    // increasing-vector representation as the rest of the repository.
    for (std::vector<int>& color_class : F) {
        std::sort(color_class.begin(), color_class.end());
    }
    return complement_degeneracy;
}

}  // namespace cc_con
