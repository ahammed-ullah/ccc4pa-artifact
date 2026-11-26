#include "zk_algo/algorithms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <csignal>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using namespace std;

namespace zk_algo {
namespace {

volatile sig_atomic_t stop_requested = 0;

void request_stop(int) { stop_requested = 1; }

// The same rejection rule as generated_weights, but with an independent seed.
// Uniform vertices remain uniform after zk_con's permutation.
int random_vertex(mt19937_64& generator, uint64_t n) {
    const uint64_t threshold = (uint64_t(0) - n) % n;
    uint64_t draw;
    do { draw = generator(); } while (draw < threshold);
    return static_cast<int>(draw % n);
}

bool heavier(int a, int b, const vector<uint64_t>& weights) {
    return b == -1 || weights[a] > weights[b] ||
           (weights[a] == weights[b] && a < b);
}

struct Arms {
    int free_first = -1;
    int free_second = -1;
    int matched_neighbor = -1;
};

// An arm is either center--free or center--u--mate[u]. We need the two best
// free ends because both sides of the central matched edge may choose the
// same free vertex. For a matched arm, only the released mate's weight matters.
Arms arms_from_row(const vector<int>& row, int central_partner,
                   const vector<int>& mate, const vector<uint64_t>& weights) {
    Arms arms;
    for (int u : row) {
        if (u == central_partner) continue;
        if (mate[u] == -1) {
            if (heavier(u, arms.free_first, weights)) {
                arms.free_second = arms.free_first;
                arms.free_first = u;
            } else if (heavier(u, arms.free_second, weights)) {
                arms.free_second = u;
            }
        } else if (arms.matched_neighbor == -1 ||
                   weights[mate[u]] < weights[mate[arms.matched_neighbor]] ||
                   (weights[mate[u]] == weights[mate[arms.matched_neighbor]] &&
                    u < arms.matched_neighbor)) {
            arms.matched_neighbor = u;
        }
    }
    return arms;
}

struct Path {
    array<int, 5> vertex{{-1, -1, -1, -1, -1}};
    int length = 0;             // number of edges, at most four
    __int128_t gain = 0;
};

void consider(Path& best, initializer_list<int> vertices, __int128_t gain) {
    if (gain <= 0) return;
    const int length = static_cast<int>(vertices.size()) - 1;
    const bool better_tie = gain == best.gain &&
        lexicographical_compare(vertices.begin(), vertices.end(),
                                best.vertex.begin(), best.vertex.begin() + best.length + 1);
    if (gain < best.gain || (gain == best.gain && !better_tie)) return;
    best.length = length;
    best.gain = gain;
    copy(vertices.begin(), vertices.end(), best.vertex.begin());
}

// All paths begin with a free endpoint and alternate between nonmatching and
// matching edges. Remove their old matched edges, then insert their new edges.
void flip(const Path& path, vector<int>& mate) {
    for (int i = 1; i < path.length; i += 2) {
        mate[path.vertex[i]] = -1;
        mate[path.vertex[i + 1]] = -1;
    }
    for (int i = 0; i < path.length; i += 2) {
        mate[path.vertex[i]] = path.vertex[i + 1];
        mate[path.vertex[i + 1]] = path.vertex[i];
    }
}

}  // namespace

uint64_t mvm_sample_limit(int n, double epsilon) {
    // Lemma 3.3 gives E[w(M_k)] >= (2/3)(1-exp(-6k/(5n))) OPT.
    // This k makes its right side at least OPT/(3/2+epsilon).
    const long double log_ratio = log(1.5L + epsilon) - log(epsilon);
    const long double count = ceil((5.0L * n / 6.0L) * log_ratio);
    if (!isfinite(count) || count > numeric_limits<uint64_t>::max())
        throw runtime_error("MVM sample limit exceeds uint64 range");
    // For an extremely large finite epsilon, floating-point subtraction may
    // round the positive logarithm to zero; one sample preserves the formula.
    return n == 0 ? 0 : max<uint64_t>(1, static_cast<uint64_t>(count));
}

void install_mvm_stop_signal() {
    stop_requested = 0;
    struct sigaction action{};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &action, nullptr) != 0)
        throw runtime_error("cannot install MVM SIGUSR1 handler");
}

MvmResult mvm(const Graph& graph, const vector<uint64_t>& weights,
              uint64_t sample_limit, uint64_t seed) {
    const int n = graph.n();
    MvmResult result;
    result.sample_limit = sample_limit;
    if (n == 0) return result;

    vector<int> mate(n, -1);
    vector<int> row;  // The next request replaces the preceding neighborhood.
    mt19937_64 generator(seed);
    int matched_vertices = 0;

    for (uint64_t sample = 0; sample < result.sample_limit; ++sample) {
        if (stop_requested) { result.complete = false; break; }
        const int v = random_vertex(generator, static_cast<uint64_t>(n));
        Path best;

        if (mate[v] == -1) {
            graph.decode_row(v, row);
            const Arms arms = arms_from_row(row, -1, mate, weights);
            if (arms.free_first != -1) {
                const int x = arms.free_first;
                consider(best, {v, x}, __int128_t(weights[v]) + weights[x]);
            } else if (arms.matched_neighbor != -1) {
                const int u = arms.matched_neighbor;
                const int released = mate[u];
                consider(best, {v, u, released},
                         __int128_t(weights[v]) - weights[released]);
            }
        } else {
            const int p = mate[v];
            graph.decode_row(v, row);
            const Arms left = arms_from_row(row, p, mate, weights);
            // A signal here leaves the matching unchanged and does not count
            // this sample. The next row could take longer than the warning.
            if (stop_requested) { result.complete = false; break; }
            graph.decode_row(p, row);
            const Arms right = arms_from_row(row, v, mate, weights);

            if (left.free_first != -1) {
                const int x = left.free_first;
                consider(best, {x, v, p},
                         __int128_t(weights[x]) - weights[p]);
            }
            if (right.free_first != -1) {
                const int y = right.free_first;
                consider(best, {y, p, v},
                         __int128_t(weights[y]) - weights[v]);
            }

            // Two distinct free ends give a three-edge augmentation. Testing
            // the top two on each side resolves a shared heaviest neighbor.
            for (int x : {left.free_first, left.free_second}) {
                if (x == -1) continue;
                for (int y : {right.free_first, right.free_second}) {
                    if (y == -1 || x == y) continue;
                    consider(best, {x, v, p, y},
                             __int128_t(weights[x]) + weights[y]);
                }
            }

            // A four-edge path has one free arm and one matched arm. Its only
            // lost matched vertex is the end of the matched arm.
            if (left.free_first != -1 && right.matched_neighbor != -1) {
                const int x = left.free_first;
                const int y = right.matched_neighbor;
                const int released = mate[y];
                consider(best, {x, v, p, y, released},
                         __int128_t(weights[x]) - weights[released]);
            }
            if (right.free_first != -1 && left.matched_neighbor != -1) {
                const int x = right.free_first;
                const int y = left.matched_neighbor;
                const int released = mate[y];
                consider(best, {x, p, v, y, released},
                         __int128_t(weights[x]) - weights[released]);
            }
        }

        if (best.gain > 0) {
            flip(best, mate);
            result.weight += static_cast<__uint128_t>(best.gain);
            ++result.positive_augmentations;
            // Odd-length paths add one matching edge and cover two vertices;
            // even-length paths only exchange which vertices are covered.
            if (best.length % 2 == 1) matched_vertices += 2;
        }
        ++result.samples_completed;
        if (matched_vertices == n) {
            // All positive-weight vertices now contribute to the objective,
            // so this matching has the maximum possible weight.
            result.all_vertices_matched = true;
            break;
        }
    }
    return result;
}

}  // namespace zk_algo
