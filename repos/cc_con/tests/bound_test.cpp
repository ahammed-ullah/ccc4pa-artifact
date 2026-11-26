#include "cc_con/bound.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace {

void require(bool condition, const string& message) {
    if (!condition) throw runtime_error(message);
}

vector<vector<int>> graph_from_edges(
    int n,
    const vector<pair<int, int>>& edges) {
    vector<vector<int>> adj(n);
    for (const auto& [u, v] : edges) {
        adj[u].push_back(v);
        adj[v].push_back(u);
    }
    for (auto& neighbors : adj) sort(neighbors.begin(), neighbors.end());
    return adj;
}

uint64_t exact_minimum_cover_size(const vector<vector<int>>& adj) {
    int n = (int)adj.size();
    vector<pair<int, int>> edges;
    int edge_index[8][8];
    for (int u = 0; u < 8; ++u)
        for (int v = 0; v < 8; ++v) edge_index[u][v] = -1;

    for (int u = 0; u < n; ++u) {
        for (int v : adj[u]) {
            if (u >= v) continue;
            edge_index[u][v] = edge_index[v][u] = (int)edges.size();
            edges.push_back({u, v});
        }
    }
    require(edges.size() < 63, "Exact test graph has too many edges");

    struct Clique {
        uint64_t edges;
        int order;
    };
    vector<Clique> cliques;
    for (uint64_t vertices = 0; vertices < (uint64_t{1} << n); ++vertices) {
        int order = __builtin_popcountll(vertices);
        if (order < 2) continue;

        bool clique = true;
        uint64_t covered = 0;
        for (int u = 0; u < n && clique; ++u) {
            if ((vertices & (uint64_t{1} << u)) == 0) continue;
            for (int v = u + 1; v < n; ++v) {
                if ((vertices & (uint64_t{1} << v)) == 0) continue;
                if (edge_index[u][v] < 0) {
                    clique = false;
                    break;
                }
                covered |= uint64_t{1} << edge_index[u][v];
            }
        }
        if (clique) cliques.push_back({covered, order});
    }

    uint64_t state_count = uint64_t{1} << edges.size();
    vector<uint64_t> best(state_count, numeric_limits<uint64_t>::max());
    best[0] = 0;
    for (uint64_t state = 0; state < state_count; ++state) {
        if (best[state] == numeric_limits<uint64_t>::max()) continue;
        for (const Clique& clique : cliques) {
            uint64_t next = state | clique.edges;
            best[next] = min(best[next], best[state] + clique.order);
        }
    }
    return best.back();
}

cc_con::BoundResult run_bounds(const vector<vector<int>>& adj,
                               cc_con::BoundMethod method,
                               bool truss) {
    cc_con::BoundOptions options;
    options.method = method;
    options.exact_truss = truss;
    cc_con::BoundResult result;
    cc_con::compute_clique_cover_lower_bounds(adj, options, result);
    return result;
}

void verify_result(const vector<vector<int>>& adj,
                   cc_con::BoundMethod method,
                   bool truss,
                   uint64_t optimum) {
    cc_con::BoundResult result = run_bounds(adj, method, truss);
    auto combined = result.lower_bounds.find("combined_best");
    require(combined != result.lower_bounds.end(),
            "Bound result omitted combined_best");

    uint64_t largest_individual = 0;
    for (const auto& [name, value] : result.lower_bounds) {
        require(value <= optimum,
                string(cc_con::bound_method_name(method)) +
                    " produced an invalid " + name + " bound");
        if (name != "combined_best")
            largest_individual = max(largest_individual, value);
    }
    require(combined->second >= largest_individual,
            "Vertexwise combination is weaker than an individual bound");

    bool high = method != cc_con::BoundMethod::Linear;
    require(result.lower_bounds.count("global_mis") == 1,
            "A run omitted global_mis");
    require(result.lower_bounds.count("global_core_color") == 1,
            "A run omitted global_core_color");
    require(result.lower_bounds.count("local_mis") == (high ? 1U : 0U),
            "Local-MIS field availability is incorrect");
    require(result.lower_bounds.count("simplicial_dominating_local_mis") ==
                (high ? 1U : 0U),
            "Simplicial-MIS field availability is incorrect");
    require(result.lower_bounds.count("exact_truss") == (truss ? 1U : 0U),
            "Exact-truss field availability is incorrect");
    require(result.lower_bounds.count("common_neighborhood_local_color") ==
                (method == cc_con::BoundMethod::LocalColor ? 1U : 0U),
            "Local-color field availability is incorrect");
}

void test_named_graphs() {
    const vector<vector<int>> triangle = graph_from_edges(
        3, {{0, 1}, {0, 2}, {1, 2}});
    const vector<vector<int>> cycle = graph_from_edges(
        4, {{0, 1}, {1, 2}, {2, 3}, {0, 3}});
    const vector<vector<int>> bow_tie = graph_from_edges(
        5, {{0, 1}, {0, 2}, {1, 2},
            {0, 3}, {0, 4}, {3, 4}});
    const vector<vector<int>> diamond = graph_from_edges(
        4, {{0, 1}, {0, 2}, {1, 2},
            {0, 3}, {1, 3}});

    const vector<pair<vector<vector<int>>, uint64_t>> cases{
        {triangle, 3}, {cycle, 8}, {bow_tie, 6}, {diamond, 6}};

    for (const auto& [adj, optimum] : cases) {
        const vector<pair<cc_con::BoundMethod, bool>> modes{
            {cc_con::BoundMethod::Linear, false},
            {cc_con::BoundMethod::LocalMis, false},
            {cc_con::BoundMethod::LocalMis, true},
            {cc_con::BoundMethod::LocalColor, false},
            {cc_con::BoundMethod::LocalColor, true},
        };
        for (const auto& [method, truss] : modes) {
            verify_result(adj, method, truss, optimum);
            cc_con::BoundResult result = run_bounds(adj, method, truss);
            require(result.lower_bounds.at("combined_best") == optimum,
                    "A named exact fixture did not attain its optimum");
        }
    }

    cc_con::BoundResult bow_tie_result = run_bounds(
        bow_tie, cc_con::BoundMethod::LocalMis, false);
    require(bow_tie_result.lower_bounds.at(
                "simplicial_dominating_local_mis") == 6,
            "Simplicial-dominating MIS missed the bow-tie lower bound");

    cc_con::BoundResult diamond_result = run_bounds(
        diamond, cc_con::BoundMethod::LocalMis, true);
    require(diamond_result.lower_bounds.at("exact_truss") == 6,
            "Exact truss bound is incorrect on the diamond graph");
}

void test_all_six_vertex_graphs() {
    // A deterministic sample exercises the bounds on graphs whose optimum is
    // computed independently by weighted set-cover dynamic programming.
    const vector<vector<pair<int, int>>> edge_sets{
        {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {0, 5}},
        {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 4}, {2, 5}, {3, 4}, {4, 5}},
        {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3},
         {2, 4}, {3, 4}, {4, 5}, {2, 5}},
    };

    for (const auto& edges : edge_sets) {
        vector<vector<int>> adj = graph_from_edges(6, edges);
        uint64_t optimum = exact_minimum_cover_size(adj);
        verify_result(adj, cc_con::BoundMethod::LocalColor, true, optimum);
    }
}

void test_all_graphs_on_five_vertices() {
    vector<pair<int, int>> possible_edges;
    for (int u = 0; u < 5; ++u)
        for (int v = u + 1; v < 5; ++v)
            possible_edges.push_back({u, v});

    for (uint64_t mask = 0;
         mask < (uint64_t{1} << possible_edges.size());
         ++mask) {
        vector<pair<int, int>> edges;
        vector<int> degree(5, 0);
        for (int e = 0; e < (int)possible_edges.size(); ++e) {
            if ((mask & (uint64_t{1} << e)) == 0) continue;
            auto [u, v] = possible_edges[e];
            edges.push_back({u, v});
            ++degree[u];
            ++degree[v];
        }
        if (find(degree.begin(), degree.end(), 0) != degree.end()) continue;

        vector<vector<int>> adj = graph_from_edges(5, edges);
        uint64_t optimum = exact_minimum_cover_size(adj);
        verify_result(adj, cc_con::BoundMethod::LocalMis, true, optimum);
        verify_result(adj, cc_con::BoundMethod::LocalColor, true, optimum);

        cc_con::BoundResult mis = run_bounds(
            adj, cc_con::BoundMethod::LocalMis, true);
        cc_con::BoundResult color = run_bounds(
            adj, cc_con::BoundMethod::LocalColor, true);
        for (const string& name : {
                 "global_mis",
                 "global_core_color",
                 "local_mis",
                 "simplicial_dominating_local_mis",
                 "common_neighborhood_global_color",
                 "exact_truss"}) {
            require(mis.lower_bounds.at(name) == color.lower_bounds.at(name),
                    "Shared lower bound differs between truss modes: " + name);
        }
    }
}

void test_invalid_mode() {
    vector<vector<int>> edge = graph_from_edges(2, {{0, 1}});
    cc_con::BoundOptions options;
    options.method = cc_con::BoundMethod::Linear;
    options.exact_truss = true;
    cc_con::BoundResult result;

    bool rejected = false;
    try {
        cc_con::compute_clique_cover_lower_bounds(edge, options, result);
    } catch (const runtime_error&) {
        rejected = true;
    }
    require(rejected, "Linear method accepted exact truss");
}

}  // namespace

int main() {
    try {
        test_named_graphs();
        test_all_six_vertex_graphs();
        test_all_graphs_on_five_vertices();
        test_invalid_mode();
        cout << "all cc_bound tests passed\n";
        return 0;
    } catch (const exception& error) {
        cerr << "cc_bound test failure: " << error.what() << '\n';
        return 1;
    }
}
