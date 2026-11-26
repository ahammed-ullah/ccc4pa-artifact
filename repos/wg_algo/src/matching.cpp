#include "wg_algo/algorithms.hpp"

#include <vector>

using namespace std;

namespace wg_algo {
namespace {

struct Choices {
    int first = -1;
    int second = -1;
};

// Figure 6 needs the first two distinct neighbors still free when an
// original matching edge is examined. The decoder fills the current row;
// this function only inspects it and never saves an adjacency or Gamma list.
Choices free_neighbors(const Graph& graph, int u, const vector<int>& mate,
                       vector<int>& row) {
    Choices choices;
    graph.decode_row(u, row);
    for (int v : row) {
        if (mate[v] != -1) continue;
        if (choices.first == -1) choices.first = v;
        else if (v != choices.first) {
            choices.second = v;
            break;
        }
    }
    return choices;
}

}  // namespace

int matching(const Graph& graph, MatchingVariant variant) {
    const int n = graph.n();
    vector<int> mate(n, -1);
    vector<int> row;
    int matching_size = 0;

    // Build a maximal matching M0 by examining each undirected edge once.
    // A free vertex u needs only its first free neighbor with v > u.
    for (int u = 0; u < n; ++u) {
        if (mate[u] != -1) continue;
        graph.decode_row(u, row);
        for (int v : row) {
            if (v <= u || mate[v] != -1) continue;
            mate[u] = v;
            mate[v] = u;
            ++matching_size;
            break;
        }
    }
    if (variant == MatchingVariant::Maximal) return matching_size;

    // The vertices matched in M0 identify its edges even after an edge is
    // replaced by two edges. New edges always touch a vertex free in M0.
    vector<unsigned char> initially_matched(n, 0);
    for (int u = 0; u < n; ++u) initially_matched[u] = mate[u] != -1;

    // Figure 6: remove every available augmenting path of length three.
    // Live free-neighbor queries replace the paper's stored Gamma lists.
    for (int u = 0; u < n; ++u) {
        if (!initially_matched[u]) continue;
        int v = mate[u];
        if (v <= u || !initially_matched[v]) continue;

        Choices left = free_neighbors(graph, u, mate, row);
        if (left.first == -1) continue;
        Choices right = free_neighbors(graph, v, mate, row);
        if (right.first == -1) continue;

        int x = left.first;
        int y = right.first;
        if (x == y) {
            if (left.second != -1) x = left.second;
            else y = right.second;
        }
        if (y == -1) continue;

        mate[u] = x;
        mate[x] = u;
        mate[v] = y;
        mate[y] = v;
        ++matching_size;
    }
    return matching_size;
}

}  // namespace wg_algo
