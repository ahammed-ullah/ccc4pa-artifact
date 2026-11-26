// Exhaustive small-graph oracle for the centered paths used by Random-Match.
#include "zk_algo/algorithms.hpp"
#include "zk_algo/weights.hpp"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

struct Choice {
    vector<int> path;
    __int128_t gain = 0;
};

struct OracleResult {
    __uint128_t weight = 0;
    uint64_t positive_augmentations = 0;
    uint64_t samples_completed = 0;
    bool all_vertices_matched = false;
};

int random_vertex(mt19937_64& generator, uint64_t n) {
    const uint64_t threshold = (uint64_t(0) - n) % n;
    uint64_t draw;
    do { draw = generator(); } while (draw < threshold);
    return static_cast<int>(draw % n);
}

void examine(Choice& best, vector<int> path, int center,
             const vector<int>& mate, const vector<uint64_t>& weights) {
    const int length = static_cast<int>(path.size()) - 1;
    const int end = path.back();
    if ((length % 2 == 1) != (mate[end] == -1)) return;
    const __int128_t gain = length % 2 == 1
        ? __int128_t(weights[path.front()]) + weights[end]
        : __int128_t(weights[path.front()]) - weights[end];
    if (gain <= 0) return;

    // The production search presents three-edge paths from center toward its
    // mate; these reversals only standardize equal-gain tie breaking.
    if (length == 1 && path[0] != center) reverse(path.begin(), path.end());
    if (length == 3 && path[1] != center) reverse(path.begin(), path.end());
    if (gain > best.gain || (gain == best.gain && path < best.path)) {
        best = {path, gain};
    }
}

void enumerate(const vector<vector<int>>& neighbors, const vector<int>& mate,
               const vector<uint64_t>& weights, int center, int partner,
               vector<int>& path, vector<unsigned char>& used, Choice& best) {
    const int length = static_cast<int>(path.size()) - 1;
    if (length == 4) return;
    const int last = path.back();
    if (length % 2 == 1) {
        // The next edge must be the current matching edge.
        const int next = mate[last];
        if (next == -1 || used[next]) return;
        path.push_back(next);
        used[next] = 1;
        examine(best, path, center, mate, weights);
        enumerate(neighbors, mate, weights, center, partner, path, used, best);
        used[next] = 0;
        path.pop_back();
    } else {
        // Every nonmatching edge of a centered 2-augmentation touches the
        // sampled center or its mate. This is the paper's center condition.
        for (int next : neighbors[last]) {
            if (next == mate[last] || used[next]) continue;
            if (last != center && next != center &&
                last != partner && next != partner) continue;
            path.push_back(next);
            used[next] = 1;
            examine(best, path, center, mate, weights);
            enumerate(neighbors, mate, weights, center, partner, path, used, best);
            used[next] = 0;
            path.pop_back();
        }
    }
}

void flip(const vector<int>& path, vector<int>& mate) {
    const int length = static_cast<int>(path.size()) - 1;
    for (int i = 1; i < length; i += 2) {
        mate[path[i]] = -1;
        mate[path[i + 1]] = -1;
    }
    for (int i = 0; i < length; i += 2) {
        mate[path[i]] = path[i + 1];
        mate[path[i + 1]] = path[i];
    }
}

OracleResult oracle(const vector<vector<int>>& neighbors,
                    const vector<uint64_t>& weights,
                    uint64_t seed, uint64_t samples) {
    const int n = static_cast<int>(neighbors.size());
    vector<int> mate(n, -1);
    mt19937_64 generator(seed);
    OracleResult result;
    for (uint64_t sample = 0; sample < samples; ++sample) {
        const int center = random_vertex(generator, static_cast<uint64_t>(n));
        const int partner = mate[center] == -1 ? center : mate[center];
        Choice best;
        for (int start = 0; start < n; ++start) {
            if (mate[start] != -1) continue;
            vector<int> path{start};
            vector<unsigned char> used(n, 0);
            used[start] = 1;
            enumerate(neighbors, mate, weights, center, partner, path, used, best);
        }
        if (best.gain > 0) {
            flip(best.path, mate);
            result.weight += static_cast<__uint128_t>(best.gain);
            ++result.positive_augmentations;
        }
        ++result.samples_completed;
        if (all_of(mate.begin(), mate.end(), [](int partner) { return partner >= 0; })) {
            result.all_vertices_matched = true;
            break;
        }
    }
    __uint128_t recounted = 0;
    for (int v = 0; v < n; ++v) {
        if (mate[v] != -1) {
            if (mate[mate[v]] != v) throw runtime_error("oracle made an invalid matching");
            recounted += weights[v];
        }
    }
    if (recounted != result.weight)
        throw runtime_error("oracle weight changed incorrectly");
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4)
            throw runtime_error("Usage: zk_mvm_check graph.zk weights.txt [graph.perm]");
        const auto graph = zk_algo::Graph::load(argv[1]);
        if (graph.n() == 0 || graph.n() > 16)
            throw runtime_error("MVM oracle needs 1..16 vertices");
        auto weights = zk_algo::load_weights(argv[2], graph.n());
        if (argc == 4) zk_algo::reorder_weights(weights, argv[3]);
        vector<vector<int>> neighbors(graph.n());
        for (int v = 0; v < graph.n(); ++v) graph.decode_row(v, neighbors[v]);
        const uint64_t samples = zk_algo::mvm_sample_limit(graph.n(), 0.01);

        for (uint64_t seed = 1; seed <= 5; ++seed) {
            zk_algo::install_mvm_stop_signal();
            const auto actual = zk_algo::mvm(graph, weights, samples, seed);
            const auto expected = oracle(neighbors, weights, seed, actual.sample_limit);
            if (!actual.complete || actual.samples_completed != expected.samples_completed ||
                actual.weight != expected.weight ||
                actual.positive_augmentations != expected.positive_augmentations ||
                actual.all_vertices_matched != expected.all_vertices_matched) {
                throw runtime_error("MVM differs from exhaustive centered-path oracle at seed " +
                                    to_string(seed));
            }
            if (seed == 1) cout << zk_algo::decimal(actual.weight) << ' '
                                << actual.sample_limit << '\n';
        }

        // A warning received during loading must lead to a valid 0-sample
        // partial run. This tests the signal path without timing a process.
        zk_algo::install_mvm_stop_signal();
        raise(SIGUSR1);
        const auto partial = zk_algo::mvm(graph, weights, samples, 1);
        if (partial.complete || partial.samples_completed != 0 ||
            partial.positive_augmentations != 0 || partial.weight != 0 ||
            partial.all_vertices_matched)
            throw runtime_error("MVM did not honor a pending Slurm signal");
        return 0;
    } catch (const exception& error) {
        cerr << "zk_mvm_check: " << error.what() << '\n';
        return 1;
    }
}
