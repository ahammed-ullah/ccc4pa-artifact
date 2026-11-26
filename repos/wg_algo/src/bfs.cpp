#include "wg_algo/algorithms.hpp"

#include <deque>
#include <vector>

using namespace std;

namespace wg_algo {

int bfs(const Graph& graph) {
    const int n = graph.n();
    vector<unsigned char> seen(n, 0);
    deque<int> queue;
    vector<int> row;
    int components = 0;

    for (int root = 0; root < n; ++root) {
        if (seen[root]) continue;
        ++components;
        seen[root] = 1;
        queue.clear();
        queue.push_back(root);

        while (!queue.empty()) {
            int u = queue.front();
            queue.pop_front();
            graph.decode_row(u, row);
            for (int v : row) {
                if (seen[v]) continue;
                seen[v] = 1;
                queue.push_back(v);
            }
        }
    }
    return components;
}

}  // namespace wg_algo
