#include "wg_algo/algorithms.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <csignal>
#include <iostream>
#include <limits>
#include <random>
#include <tuple>
#include <vector>

using namespace std;
using namespace wg_algo;

namespace {

struct Move {
    int left = -1;
    int right = -1;
    int released_left = -1;
    int released_right = -1;
    int edges = 0;
    int path_length = 0;
    int64_t gain = 0;
    vector<int> after;
};

uint64_t weight_of(const vector<int>& mate, const vector<uint64_t>& weights) {
    uint64_t sum = 0;
    for (size_t v = 0; v < mate.size(); ++v)
        if (mate[v] >= 0) sum += weights[v];
    return sum;
}

// Enumerate all one- and two-edge changes centered at v (or at its matched
// edge). Unlike mvm.cpp, this recomputes the whole matching's weight.
void try_move(Move& best, const vector<int>& mate,
              const vector<uint64_t>& weights, int center, int left, int right) {
    const int partner = mate[center];
    if (left < 0 && right < 0) return;
    if (left >= 0 && right >= 0 && left == right) return;
    Move move;
    move.left = left;
    move.right = right;
    move.released_left = left >= 0 ? mate[left] : -1;
    move.released_right = right >= 0 ? mate[right] : -1;
    move.edges = (left >= 0) + (right >= 0);
    move.after = mate;
    auto remove_edge = [&](int u) {
        const int p = move.after[u];
        if (p >= 0) {
            move.after[u] = -1;
            move.after[p] = -1;
        }
    };
    remove_edge(center);
    if (left >= 0) remove_edge(left);
    if (right >= 0) remove_edge(right);
    if (left >= 0) { move.after[center] = left; move.after[left] = center; }
    if (right >= 0) { move.after[partner] = right; move.after[right] = partner; }
    const uint64_t before = weight_of(mate, weights);
    const uint64_t after = weight_of(move.after, weights);
    move.gain = static_cast<int64_t>(after) - static_cast<int64_t>(before);
    int old_edges = 0;
    for (size_t u = 0; u < mate.size(); ++u)
        if (mate[u] > static_cast<int>(u) &&
            move.after[u] != mate[u]) ++old_edges;
    move.path_length = move.edges + old_edges;
    if (move.gain > best.gain ||
        (move.gain == best.gain && move.gain > 0 &&
         tie(move.left, move.right, move.released_left, move.released_right) <
         tie(best.left, best.right, best.released_left, best.released_right)))
        best = move;
}

int sampled_vertex(mt19937_64& generator, uint64_t bound) {
    const uint64_t threshold = (uint64_t(0) - bound) % bound;
    uint64_t draw;
    do { draw = generator(); } while (draw < threshold);
    return static_cast<int>(draw % bound);
}

void check(const Graph& graph, const vector<uint64_t>& weights,
           uint64_t seed, uint64_t count, array<int, 6>& used) {
    const MvmResult actual = mvm(graph, weights, seed, count);
    vector<int> mate(graph.n(), -1);
    mt19937_64 generator(seed);
    uint64_t augmentations = 0;
    uint64_t completed_samples = 0;
    bool all_vertices_matched = false;
    for (uint64_t sample = 0; sample < count; ++sample) {
        const int center = sampled_vertex(generator, graph.n());
        const int partner = mate[center];
        Move best;
        if (partner < 0) {
            for (int u : graph.rows[center])
                try_move(best, mate, weights, center, u, -1);
        } else {
            for (int u : graph.rows[center])
                if (u != partner)
                    try_move(best, mate, weights, center, u, -1);
            for (int u : graph.rows[partner])
                if (u != center)
                    try_move(best, mate, weights, center, -1, u);
            for (int a : graph.rows[center])
                if (a != partner)
                    for (int b : graph.rows[partner])
                        if (b != center)
                            try_move(best, mate, weights, center, a, b);
        }
        if (best.gain > 0) {
            mate = best.after;
            ++augmentations;
            assert(best.path_length >= 1 && best.path_length <= 4);
            ++used[best.path_length];
        }
        ++completed_samples;
        all_vertices_matched = all_of(mate.begin(), mate.end(),
                                      [](int partner) { return partner >= 0; });
        if (all_vertices_matched) break;
    }
    if (actual.weight != weight_of(mate, weights) ||
        actual.positive_augmentations != augmentations ||
        actual.samples_completed != completed_samples ||
        actual.all_vertices_matched != all_vertices_matched ||
        actual.time_limit_partial) {
        cerr << "MVM disagrees with exhaustive centered-path search, seed "
             << seed << ", n " << graph.n() << '\n';
        abort();
    }
}

}  // namespace

int main() {
    assert(mvm_sample_limit(8, 0.01) == 34);
    mt19937 generator(271828);
    array<int, 6> used{};
    for (int trial = 0; trial < 300; ++trial) {
        Graph graph;
        graph.rows.resize(8);
        for (int u = 0; u < 8; ++u)
            for (int v = u + 1; v < 8; ++v)
                if (generator() % 100 < 40) {
                    graph.rows[u].push_back(v);
                    graph.rows[v].push_back(u);
                }
        vector<uint64_t> weights(8);
        uint64_t power = 1;
        for (auto& weight : weights) { weight = power; power *= 10; }
        shuffle(weights.begin(), weights.end(), generator);
        for (uint64_t seed = 1; seed <= 5; ++seed)
            check(graph, weights, seed, 40, used);
    }
    for (int length = 1; length <= 4; ++length) {
        if (used[length] == 0) {
            cerr << "No selected path of length " << length << " was exercised\n";
            return 1;
        }
    }
    Graph heavy;
    heavy.rows = {{1}, {0}};
    const uint64_t largest = numeric_limits<uint64_t>::max();
    const MvmResult wide = mvm(heavy, {largest, largest}, 1, 1);
    assert(wide.weight == __uint128_t(largest) * 2);
    const MvmResult exact = mvm(heavy, {2, 3}, 1, 5);
    assert(exact.weight == 5 && exact.samples_completed == 1 &&
           exact.positive_augmentations == 1 && exact.all_vertices_matched &&
           !exact.time_limit_partial);
    Graph odd;
    odd.rows = {{1}, {0}, {}};
    const MvmResult limit = mvm(odd, {2, 3, 4}, 1, 5);
    assert(limit.samples_completed == 5 && !limit.all_vertices_matched &&
           !limit.time_limit_partial);
    // A warning present before the next sample produces a valid zero-sample
    // partial result, rather than allowing the signal to terminate the job.
    install_mvm_signal_handler();
    raise(SIGUSR1);
    Graph graph;
    graph.rows = {{1}, {0}};
    const MvmResult partial = mvm(graph, {2, 3}, 1, 5);
    assert(partial.time_limit_partial && partial.samples_completed == 0 &&
           partial.positive_augmentations == 0 && partial.weight == 0 &&
           !partial.all_vertices_matched);
    cout << "MVM oracle: all four positive path lengths exercised\n";
}
