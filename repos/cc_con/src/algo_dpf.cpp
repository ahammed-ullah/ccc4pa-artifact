#include "cc_con/algo_dpf.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using namespace std;

namespace cc_con {

// Compute a degeneracy order pi in which every vertex has at most d earlier
// neighbors. pos[v] is the position of v in pi, and degeneracy receives d.
// maximum_degree is also returned because DPF later precomputes binary-search
// costs for adjacency-list sizes up to this value.
static void degeneracy_order(const vector<vector<int>>& adj,
                             vector<int>& pi,
                             vector<int>& pos,
                             int& degeneracy,
                             int& maximum_degree) {
    int n = (int)adj.size();
    pi.resize(n);
    pos.resize(n);

    // During the peel:
    //
    //   degree[v]  = current degree of v;
    //   pi[i]      = vertex currently at position i;
    //   pos[v]     = current position of v in pi;
    //   bin[k]     = first unprocessed position in degree bucket k.
    //
    // Thus pi and pos are inverse arrays. Vertices in pi are grouped by their
    // current degree, and bin identifies the beginning of every group.
    vector<int> degree(n);
    maximum_degree = 0;
    for (int v = 0; v < n; ++v) {
        degree[v] = (int)adj[v].size();
        maximum_degree = max(maximum_degree, degree[v]);
    }

    // Counting sort the vertices by initial degree. Iterating through vertex
    // IDs gives deterministic tie order within each initial bucket.
    vector<int> bin(maximum_degree + 1, 0);
    for (int v = 0; v < n; ++v) ++bin[degree[v]];

    int first = 0;
    for (int k = 0; k <= maximum_degree; ++k) {
        int bucket_size = bin[k];
        bin[k] = first;
        first += bucket_size;
    }

    for (int v = 0; v < n; ++v) {
        pos[v] = bin[degree[v]];
        pi[pos[v]] = v;
        ++bin[degree[v]];
    }

    // Restore bin[k] from the end of bucket k to its beginning.
    for (int k = maximum_degree; k >= 1; --k) bin[k] = bin[k - 1];
    bin[0] = 0;

    // This is the standard array-bucket core-decomposition update. When v is
    // peeled, an unprocessed neighbor u with degree[u] > degree[v] is moved to
    // the front of its current bucket before its degree is decreased:
    //
    //   w = pi[bin[degree[u]]]
    //
    // is the first vertex in that bucket. Swapping u with w puts u at the
    // boundary. Advancing the boundary removes u from the old bucket, and then
    // degree[u]-- places it in the preceding bucket without a linked list.
    degeneracy = 0;
    for (int i = 0; i < n; ++i) {
        int v = pi[i];
        degeneracy = max(degeneracy, degree[v]);

        for (int u : adj[v]) {
            if (degree[u] <= degree[v]) continue;

            int du = degree[u];
            int pu = pos[u];
            int pw = bin[du];
            int w = pi[pw];

            if (u != w) {
                pi[pu] = w;
                pi[pw] = u;
                pos[u] = pw;
                pos[w] = pu;
            }
            ++bin[du];
            --degree[u];
        }
    }

    // The peel order gives at most d later neighbors. DPF uses the reverse
    // convention, in which every vertex has at most d earlier neighbors.
    reverse(pi.begin(), pi.end());
    for (int i = 0; i < n; ++i) pos[pi[i]] = i;
}

// First-fit color the complement of G[X]. X contains canonical vertex IDs in
// increasing order, so appending a vertex to Q[ell] also keeps Q[ell] sorted.
//
// When u joins Q[ell], all edges from u to the previous members of Q[ell]
// become covered. First fit has just found those same edges while deciding
// whether Q[ell] accepts u. The routine therefore retains their positions in
// the packed covered-edge array and sets the bits immediately after selecting
// ell. This avoids finding every internal Q[ell] edge again after coloring.
//
// rank[u] is u's position in the current X, or -1 outside X. The remaining
// scratch vectors are also passed in so their allocated memory is reused across
// pivots.
static void greedy_color_complement(const vector<vector<int>>& adj,
                                    const vector<int>& pos,
                                    const vector<size_t>& adj_begin,
                                    const vector<int>& X,
                                    const vector<unsigned char>& search_height,
                                    vector<int>& rank,
                                    vector<int>& color,
                                    vector<vector<int>>& Q,
                                    vector<int>& count,
                                    vector<int>& seen,
                                    vector<int>& touched,
                                    vector<size_t>& edge_bit,
                                    vector<uint64_t>& covered) {
    for (int j = 0; j < (int)X.size(); ++j) rank[X[j]] = j;

    Q.clear();
    count.clear();
    seen.clear();
    edge_bit.resize(X.size());

    for (int j = 0; j < (int)X.size(); ++j) {
        int u = X[j];
        auto& N_u = adj[u];
        touched.clear();
        bool scanned_adjacency = false;

        if (j > 0) {
            // We need the neighbors of u in X[0..j). Search once for every
            // prefix member, using the adjacency list that owns its covered
            // bit, or scan N(u) once. The inexpensive estimate below chooses
            // between these routes. Either route has O(d log n) work because
            // j <= |X| <= d; the scan is selected only when degree(u) is no
            // larger than the estimated candidate-search work.
            int scan_work = (int)N_u.size();
            long long search_work = 1LL * j * search_height[scan_work];

            if (search_work < scan_work) {
                for (int k = 0; k < j; ++k) {
                    int w = X[k];

                    // The endpoint later in pi owns the authoritative bit for
                    // {u,w}. Search that endpoint's adjacency list once: the
                    // result both establishes adjacency and gives the bit that
                    // must be set if u selects w's color class.
                    int owner = pos[u] > pos[w] ? u : w;
                    int other = owner == u ? w : u;
                    auto& N_owner = adj[owner];
                    auto edge = lower_bound(N_owner.begin(), N_owner.end(), other);
                    if (edge == N_owner.end() || *edge != other) continue;

                    edge_bit[k] = adj_begin[owner] + (edge - N_owner.begin());

                    int ell = color[w];
                    if (seen[ell] != j) {
                        seen[ell] = j;
                        count[ell] = 1;
                        touched.push_back(ell);
                    } else {
                        ++count[ell];
                    }
                }
            } else {
                scanned_adjacency = true;
                for (int slot = 0; slot < (int)N_u.size(); ++slot) {
                    int w = N_u[slot];
                    int k = rank[w];
                    if (k < 0 || k >= j) continue;

                    // If u is later in pi, this scan already gives the
                    // authoritative bit. Otherwise w owns the bit; its exact
                    // position is found only if u ultimately selects w's
                    // class, avoiding searches for irrelevant color classes.
                    if (pos[u] > pos[w])
                        edge_bit[k] = adj_begin[u] + slot;

                    int ell = color[w];
                    if (seen[ell] != j) {
                        seen[ell] = j;
                        count[ell] = 1;
                        touched.push_back(ell);
                    } else {
                        ++count[ell];
                    }
                }
            }
        }

        // Q[ell] accepts u exactly when every current member of Q[ell] was
        // counted as a graph neighbor of u. First fit selects the least such ell.
        int selected = -1;
        for (int ell : touched) {
            if (count[ell] == (int)Q[ell].size() &&
                (selected < 0 || ell < selected)) {
                selected = ell;
            }
        }

        if (selected < 0) {
            selected = (int)Q.size();
            Q.emplace_back();
            Q.back().reserve(2);
            count.push_back(0);
            seen.push_back(-1);
        }

        color[u] = selected;

        // Every previous member w of the selected class is adjacent to u.
        // The binary-search route retained the authoritative bit directly. On
        // the adjacency-scan route, a search is still needed exactly when w,
        // rather than u, owns that bit. No edge is searched again after this
        // vertex has been processed.
        for (int w : Q[selected]) {
            int k = rank[w];
            size_t bit;

            if (!scanned_adjacency || pos[u] > pos[w]) {
                bit = edge_bit[k];
            } else {
                auto& N_w = adj[w];
                auto edge = lower_bound(N_w.begin(), N_w.end(), u);
                bit = adj_begin[w] + (edge - N_w.begin());
            }

            covered[bit >> 6] |= 1ULL << (bit & 63);
        }

        Q[selected].push_back(u);
    }

    for (int u : X) rank[u] = -1;
}

void construct_dpf(const vector<vector<int>>& adj,
                   vector<vector<int>>& C,
                   DpfStatistics& statistics) {
    int n = (int)adj.size();
    C.clear();
    statistics = {};
    if (n == 0) return;

    vector<int> pi, pos;
    int maximum_degree;
    degeneracy_order(adj, pi, pos,
                     statistics.degeneracy, maximum_degree);

    // adj_begin[v]+j is the global position of adj[v][j]. Only the position at
    // the endpoint later in pi is used for an undirected edge. Global positions
    // can exceed the range of int, which is why adj_begin and bit are size_t.
    vector<size_t> adj_begin(n + 1, 0);
    for (int v = 0; v < n; ++v)
        adj_begin[v + 1] = adj_begin[v] + adj[v].size();

    size_t num_entries = adj_begin[n];
    size_t num_words = num_entries / 64;
    if (num_entries % 64 != 0) ++num_words;
    vector<uint64_t> covered(num_words, 0);

    // Maximum number of comparisons made by binary search on a list of size s.
    vector<unsigned char> search_height(maximum_degree + 1, 0);
    for (int s = 1; s <= maximum_degree; ++s)
        search_height[s] = search_height[s / 2] + 1;

    // Workspaces reused by greedy_color_complement. X and every Q[ell] contain
    // canonical vertex IDs. rank and color are indexed by canonical vertex ID.
    vector<int> X;
    vector<vector<int>> Q;
    vector<int> count, seen, touched;
    vector<int> rank(n, -1);
    vector<int> color(n, -1);
    vector<size_t> edge_bit;

    // DPF: process pivots in reverse pi order. For pivot v=pi[i], X is the set
    // of uncovered earlier neighbors. First fit partitions X into graph
    // cliques, and every Q[ell] union {v} is added to the cover.
    for (int i = n - 1; i >= 1; --i) {
        int v = pi[i];
        auto& N_v = adj[v];
        X.clear();

        for (int j = 0; j < (int)N_v.size(); ++j) {
            int u = N_v[j];
            if (pos[u] >= i) continue;

            size_t bit = adj_begin[v] + j;
            if ((covered[bit >> 6] & (1ULL << (bit & 63))) == 0)
                X.push_back(u);
        }
        if (X.empty()) continue;

        // N_v is sorted, and filtering it preserves canonical order in X.
        // Consequently each first-fit class is sorted without any insertion or
        // final sorting of its members.
        greedy_color_complement(adj, pos, adj_begin, X, search_height,
                                rank, color, Q, count, seen, touched,
                                edge_bit, covered);

        // Q[ell] is already sorted. Insert the pivot once, then transfer the
        // completed clique directly to the output cover. Across all classes at
        // this pivot, vector insertion moves at most |X| existing vertices.
        for (auto& clique : Q) {
            auto insertion_point = lower_bound(clique.begin(), clique.end(), v);
            clique.insert(insertion_point, v);
            C.push_back(std::move(clique));
        }
    }
}

}  // namespace cc_con
