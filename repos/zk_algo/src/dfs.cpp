#include "zk_algo/algorithms.hpp"

#include <vector>

using namespace std;

namespace zk_algo {

int dfs(const Graph& graph) {
    const int n = graph.n();

    // A pending vertex belongs to one active vertex's child list. These four
    // arrays represent the lists and the DFS parent, using O(n) integers.
    vector<int> parent(n, -1);
    vector<int> first_child(n, -1);
    vector<int> previous(n, -1);
    vector<int> next(n, -1);

    enum State : unsigned char { unseen, pending, entered, scanned, finished };
    vector<unsigned char> state(n, unseen);
    vector<int> row;

    auto remove_pending = [&](int v) {
        int u = parent[v];
        if (previous[v] == -1) first_child[u] = next[v];
        else next[previous[v]] = next[v];
        if (next[v] != -1) previous[next[v]] = previous[v];
        previous[v] = next[v] = -1;
    };
    auto add_pending = [&](int u, int v) {
        parent[v] = u;
        next[v] = first_child[u];
        previous[v] = -1;
        if (next[v] != -1) previous[next[v]] = v;
        first_child[u] = v;
    };

    int components = 0;
    for (int root = 0; root < n; ++root) {
        if (state[root] != unseen) continue;
        ++components;
        state[root] = entered;
        int u = root;

        while (u != -1) {
            if (state[u] == entered) {
                // A pending neighbor of an ancestor moves below u in the
                // DFS tree. The next query reuses this row buffer.
                graph.decode_row(u, row);
                for (int v : row) {
                    if (state[v] == unseen) {
                        state[v] = pending;
                        add_pending(u, v);
                    } else if (state[v] == pending && parent[v] != u) {
                        remove_pending(v);
                        add_pending(u, v);
                    }
                }
                state[u] = scanned;
            }

            if (first_child[u] != -1) {
                int v = first_child[u];
                remove_pending(v);
                state[v] = entered;
                u = v;
            } else {
                state[u] = finished;
                u = parent[u];
            }
        }
    }
    return components;
}

}  // namespace zk_algo
