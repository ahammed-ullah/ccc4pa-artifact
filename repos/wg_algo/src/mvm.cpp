#include "wg_algo/algorithms.hpp"

#include <cmath>
#include <csignal>
#include <limits>
#include <random>
#include <stdexcept>
#include <tuple>
#include <vector>

using namespace std;

namespace wg_algo {
namespace {

volatile sig_atomic_t stop_requested = 0;

void request_stop(int) { stop_requested = 1; }

struct Side {
    int free1 = -1;
    int free2 = -1;
    int matched = -1;
};

// The heaviest two free arms resolve the case in which both sides of a
// matching edge choose the same free endpoint. For a matched arm, only the
// weight of the endpoint released by the flip matters.
Side scan_side(const Graph& graph, int center, int exclude,
               const vector<int>& mate, const vector<uint64_t>& weights,
               vector<int>& row) {
    graph.decode_row(center, row);
    Side side;
    auto heavier = [&](int a, int b) {
        return b < 0 || weights[a] > weights[b] ||
               (weights[a] == weights[b] && a < b);
    };
    for (int u : row) {
        if (u == exclude) continue; // the current matching edge is not an arm
        if (mate[u] < 0) {
            if (heavier(u, side.free1)) {
                side.free2 = side.free1;
                side.free1 = u;
            } else if (heavier(u, side.free2)) {
                side.free2 = u;
            }
        } else if (side.matched < 0 ||
                   weights[mate[u]] < weights[mate[side.matched]] ||
                   (weights[mate[u]] == weights[mate[side.matched]] &&
                    u < side.matched)) {
            side.matched = u;
        }
    }
    return side;
}

struct Choice {
    int left = -1;
    int right = -1;
    int released_left = -1;
    int released_right = -1;
    __uint128_t gain = 0;
};

void consider(Choice& best, const Choice& choice) {
    if (choice.gain > best.gain ||
        (choice.gain == best.gain && choice.gain > 0 &&
         tie(choice.left, choice.right, choice.released_left,
             choice.released_right) <
         tie(best.left, best.right, best.released_left,
             best.released_right)))
        best = choice;
}

// A completed choice replaces the center edge and, for a length-four path,
// the matched edge at one arm. Every inserted edge touches one center.
void apply(int center, int partner, const Choice& choice, vector<int>& mate) {
    mate[center] = -1;
    mate[partner] = -1;
    if (choice.released_left >= 0) mate[choice.released_left] = -1;
    if (choice.released_right >= 0) mate[choice.released_right] = -1;
    if (choice.left >= 0) {
        mate[center] = choice.left;
        mate[choice.left] = center;
    }
    if (choice.right >= 0) {
        mate[partner] = choice.right;
        mate[choice.right] = partner;
    }
}

int sampled_vertex(mt19937_64& generator, uint64_t bound) {
    const uint64_t threshold = (uint64_t(0) - bound) % bound;
    uint64_t draw;
    do { draw = generator(); } while (draw < threshold);
    return static_cast<int>(draw % bound);
}

}  // namespace

void install_mvm_signal_handler() {
    stop_requested = 0;
    struct sigaction action {};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &action, nullptr) != 0)
        throw runtime_error("cannot install MVM SIGUSR1 handler");
}

uint64_t mvm_sample_limit(int n, double epsilon) {
    if (n == 0) return 0;
    const long double e = epsilon;
    // Lemma 3.3: E[w(M_k)] >= 2/3 (1 - exp(-6k/(5n))) OPT.
    // This k makes the right side at least OPT/(3/2 + epsilon).
    const long double count = ceill((5.0L * n / 6.0L) *
                                    (logl(1.5L + e) - logl(e)));
    if (!isfinite(count) || count > numeric_limits<uint64_t>::max())
        throw runtime_error("MVM sample limit exceeds uint64 range");
    // For extremely large finite epsilon, log subtraction can round to
    // zero, although the positive mathematical sample count rounds to one.
    return count < 1 ? 1 : static_cast<uint64_t>(count);
}

MvmResult mvm(const Graph& graph, const vector<uint64_t>& weights,
              uint64_t seed, uint64_t sample_limit) {
    const int n = graph.n();
    vector<int> mate(n, -1);
    vector<int> row;
    mt19937_64 generator(seed);
    MvmResult result;
    int matched_vertices = 0;

    for (uint64_t sample = 0; sample < sample_limit; ++sample) {
        if (stop_requested) {
            result.time_limit_partial = true;
            return result;
        }
        const int center = sampled_vertex(generator, static_cast<uint64_t>(n));
        const int partner = mate[center];
        const Side left = scan_side(graph, center, partner, mate, weights, row);
        if (partner >= 0 && stop_requested) {
            result.time_limit_partial = true;
            return result;
        }

        if (partner < 0) {
            // With positive weights, a free--free edge beats every two-edge
            // replacement centered at the same free vertex.
            if (left.free1 >= 0) {
                const int u = left.free1;
                mate[center] = u;
                mate[u] = center;
                result.weight += __uint128_t(weights[center]) + weights[u];
                ++result.positive_augmentations;
                matched_vertices += 2;
            } else if (left.matched >= 0) {
                const int u = left.matched;
                const int released = mate[u];
                if (weights[center] > weights[released]) {
                    mate[released] = -1;
                    mate[center] = u;
                    mate[u] = center;
                    result.weight += weights[center] - weights[released];
                    ++result.positive_augmentations;
                }
            }
        } else {
            // Every nonmatching edge of a 2-augmentation centered at this
            // matched vertex touches center or partner. No row is saved after
            // the current query. Four-cycles have zero vertex-weight gain.
            const Side right = scan_side(graph, partner, center, mate,
                                         weights, row);
            Choice best;
            if (left.free1 >= 0 && weights[left.free1] > weights[partner])
                consider(best, {left.free1, -1, -1, -1,
                                weights[left.free1] - weights[partner]});
            if (right.free1 >= 0 && weights[right.free1] > weights[center])
                consider(best, {-1, right.free1, -1, -1,
                                weights[right.free1] - weights[center]});

            const int free_left[2] = {left.free1, left.free2};
            const int free_right[2] = {right.free1, right.free2};
            for (int a : free_left)
                for (int b : free_right)
                    if (a >= 0 && b >= 0 && a != b)
                        consider(best, {a, b, -1, -1,
                                        __uint128_t(weights[a]) + weights[b]});

            if (left.free1 >= 0 && right.matched >= 0) {
                const int released = mate[right.matched];
                if (weights[left.free1] > weights[released])
                    consider(best, {left.free1, right.matched, -1, released,
                                    weights[left.free1] - weights[released]});
            }
            if (right.free1 >= 0 && left.matched >= 0) {
                const int released = mate[left.matched];
                if (weights[right.free1] > weights[released])
                    consider(best, {left.matched, right.free1, released, -1,
                                    weights[right.free1] - weights[released]});
            }
            if (best.gain > 0) {
                // Only a three-edge path with two free ends grows the matching.
                if (best.left >= 0 && best.right >= 0 &&
                    best.released_left < 0 && best.released_right < 0)
                    matched_vertices += 2;
                apply(center, partner, best, mate);
                result.weight += best.gain;
                ++result.positive_augmentations;
            }
        }
        ++result.samples_completed;
        // All positive-weight vertices are now covered, so this is an exact
        // maximum-weight matching regardless of the remaining sample limit.
        if (matched_vertices == n) {
            result.all_vertices_matched = true;
            return result;
        }
    }
    return result;
}

}  // namespace wg_algo
