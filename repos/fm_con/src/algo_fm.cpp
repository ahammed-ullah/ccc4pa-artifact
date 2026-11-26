#include "fm_con/fm.hpp"

#include "fm_con/big_uint.hpp"
#include "fm_con/linear_index.hpp"
#include "fm_con/neighborhood_tree.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace fm_con {
namespace {

using Clock = chrono::steady_clock;

struct DeltaCandidate {
    long double delta;
    int initial_k;
};

double elapsed_seconds(Clock::time_point start) {
    return chrono::duration<double>(Clock::now() - start).count();
}

uint64_t checked_add(uint64_t a, uint64_t b, const string& quantity) {
    if (a > numeric_limits<uint64_t>::max() - b)
        throw runtime_error(quantity + " overflows uint64");
    return a + b;
}

uint64_t checked_multiply(uint64_t a,
                          uint64_t b,
                          const string& quantity) {
    if (a != 0 && b > numeric_limits<uint64_t>::max() / a)
        throw runtime_error(quantity + " overflows uint64");
    return a * b;
}

// Definition 2.1 uses the current number of bipartite edges. Returning zero
// when the floor is zero resolves the threshold inconsistency between the
// prose/proof and the displayed loop in Algorithm Partition: no nonempty
// delta-clique is requested when its prescribed right side has size zero.
int fm_k(int n, uint64_t remaining_edges, long double delta) {
    if (n < 2 || remaining_edges == 0) return 0;

    long double numerator = delta * log2l((long double)n);
    long double denominator = log2l(
        2.0L * (long double)n * (long double)n /
        (long double)remaining_edges);
    if (!(denominator > 0.0L))
        throw runtime_error("FM encountered a nonpositive logarithm denominator");

    long double value = numerator / denominator;
    if (value < 1.0L) return 0;
    if (value > (long double)numeric_limits<int>::max())
        throw runtime_error("FM k exceeds int");
    return (int)floorl(value);
}

BigUInt falling_factorial(uint32_t value, int terms) {
    if (terms == 0) return BigUInt(1);
    if (value < (uint32_t)terms) return BigUInt();

    BigUInt product(1);
    for (int i = 0; i < terms; ++i)
        product.multiply(value - (uint32_t)i);
    return product;
}

uint64_t real_vertices(uint64_t begin,
                       uint64_t length,
                       int n) {
    if (begin >= (uint64_t)n) return 0;
    uint64_t end = min(begin + length, (uint64_t)n);
    return end - begin;
}

int selected_vertices(const vector<int>& K,
                      uint64_t begin,
                      uint64_t length) {
    uint64_t end = begin + length;
    int count = 0;
    for (int v : K) {
        if ((uint64_t)v >= begin && (uint64_t)v < end) ++count;
    }
    return count;
}

// Clique Stripping, Steps 1--6. The paper updates the neighborhood trees after
// each selected y_t so they represent V minus {y_1,...,y_t}. Here the trees
// continue to represent the residual graph before this stripping call. Every
// active left vertex is adjacent to every already selected y, so subtracting
// the number of selected vertices in a range gives exactly the paper's d-values.
// Only the final U_K x K is removed from the trees.
void find_biclique_prefix_tree(const NeighborhoodTrees& trees,
                               int k,
                               vector<int>& U,
                               vector<int>& K) {
    int n = trees.n;
    U.resize(n);
    for (int u = 0; u < n; ++u) U[u] = u;
    K.clear();
    K.reserve(k);

    vector<uint32_t> current_node;
    vector<int> next_U;

    for (int t = 1; t <= k; ++t) {
        current_node.assign(U.size(), 0);
        uint64_t range_begin = 0;
        uint64_t range_length = trees.leaf_range;

        for (int depth = 0; depth < trees.depth; ++depth) {
            uint64_t half = range_length / 2U;
            uint64_t left_begin = range_begin;
            uint64_t right_begin = range_begin + half;
            int selected_left = selected_vertices(K, left_begin, half);
            int selected_right = selected_vertices(K, right_begin, half);
            uint64_t available_left =
                real_vertices(left_begin, half, n) - (uint64_t)selected_left;
            uint64_t available_right =
                real_vertices(right_begin, half, n) - (uint64_t)selected_right;

            BigUInt score_left;
            BigUInt score_right;
            for (int i = 0; i < (int)U.size(); ++i) {
                int u = U[i];
                uint32_t root_degree = tree_count(trees, u, 0);
                if (root_degree < (uint32_t)(t - 1))
                    throw runtime_error("FM active-set/tree invariant failed");

                uint32_t degree = root_degree - (uint32_t)(t - 1);
                BigUInt completions = degree == 0
                    ? BigUInt()
                    : falling_factorial(degree - 1U, k - t);

                uint32_t left_node =
                    tree_child(trees, u, current_node[i], 0);
                uint32_t right_node =
                    tree_child(trees, u, current_node[i], 1);
                uint32_t raw_left = tree_count(trees, u, left_node);
                uint32_t raw_right = tree_count(trees, u, right_node);
                if (raw_left < (uint32_t)selected_left ||
                    raw_right < (uint32_t)selected_right) {
                    throw runtime_error("FM selected-vertex count exceeds a tree count");
                }

                uint32_t d_left = raw_left - (uint32_t)selected_left;
                uint32_t d_right = raw_right - (uint32_t)selected_right;
                score_left.add_scaled(completions, d_left);
                score_right.add_scaled(completions, d_right);
            }

            if (available_left == 0 && available_right == 0)
                throw runtime_error("FM binary search exhausted its candidates");
            if (score_left.is_zero() && score_right.is_zero())
                throw runtime_error("FM binary search has zero conditional score");

            int branch;
            if (available_left == 0) {
                branch = 1;
            } else if (available_right == 0) {
                branch = 0;
            } else {
                // The paper compares c_0 and c_1 because its two branches have
                // equal cardinality. For arbitrary n, compare the exact averages
                // by cross multiplication. Equality keeps the lower-ID branch.
                BigUInt normalized_left = score_left;
                BigUInt normalized_right = score_right;
                normalized_left.multiply((uint32_t)available_right);
                normalized_right.multiply((uint32_t)available_left);
                branch = compare(normalized_left, normalized_right) >= 0 ? 0 : 1;
            }

            for (int i = 0; i < (int)U.size(); ++i)
                current_node[i] =
                    tree_child(trees, U[i], current_node[i], branch);

            if (branch == 1) range_begin = right_begin;
            range_length = half;
        }

        if (range_begin >= (uint64_t)n)
            throw runtime_error("FM selected a padded right vertex");
        int y = (int)range_begin;
        if (find(K.begin(), K.end(), y) != K.end())
            throw runtime_error("FM selected the same right vertex twice");

        next_U.clear();
        next_U.reserve(U.size());
        for (int i = 0; i < (int)U.size(); ++i) {
            if (tree_count(trees, U[i], current_node[i]) != 0)
                next_U.push_back(U[i]);
        }
        if (next_U.empty())
            throw runtime_error("FM Clique Stripping produced an empty left side");

        K.push_back(y);
        U.swap(next_U);
    }
}

// Linear-space version of Clique Stripping, Steps 1--6. For every active u,
// low[i] and high[i] delimit the packed neighbors in the current right-vertex
// range. One packed lower_bound splits that interval at each binary-search
// level. The deleted-edge rank structure then gives the two current d-values.
// This computes the same scores as find_biclique_prefix_tree, but each range
// count has an additional logarithmic cost.
void find_biclique_linear(const LinearAdjacency& adjacency,
                          const LinearState& state,
                          int k,
                          vector<int>& U,
                          vector<int>& K) {
    int n = adjacency.n;
    U.resize(n);
    for (int u = 0; u < n; ++u) U[u] = u;
    K.clear();
    K.reserve(k);

    vector<uint32_t> low;
    vector<uint32_t> high;
    vector<uint32_t> middle;
    vector<int> next_U;

    uint64_t leaf_range = 1;
    int tree_depth = 0;
    while (leaf_range < (uint64_t)n) {
        leaf_range <<= 1U;
        ++tree_depth;
    }

    for (int t = 1; t <= k; ++t) {
        low.assign(U.size(), 0);
        high.resize(U.size());
        for (int i = 0; i < (int)U.size(); ++i)
            high[i] = linear_degree(adjacency, U[i]);

        uint64_t range_begin = 0;
        uint64_t range_length = leaf_range;

        for (int depth = 0; depth < tree_depth; ++depth) {
            uint64_t half = range_length / 2U;
            uint64_t left_begin = range_begin;
            uint64_t right_begin = range_begin + half;
            int selected_left = selected_vertices(K, left_begin, half);
            int selected_right = selected_vertices(K, right_begin, half);
            uint64_t available_left =
                real_vertices(left_begin, half, n) - (uint64_t)selected_left;
            uint64_t available_right =
                real_vertices(right_begin, half, n) - (uint64_t)selected_right;

            middle.resize(U.size());
            BigUInt score_left;
            BigUInt score_right;

            for (int i = 0; i < (int)U.size(); ++i) {
                int u = U[i];
                uint32_t root_degree = state.left_degree[u];
                if (root_degree < (uint32_t)(t - 1))
                    throw runtime_error("FM active-set/linear-index invariant failed");

                uint32_t degree = root_degree - (uint32_t)(t - 1);
                BigUInt completions = degree == 0
                    ? BigUInt()
                    : falling_factorial(degree - 1U, k - t);

                uint32_t split = linear_lower_bound(
                    adjacency, u, low[i], high[i], (int)right_begin);
                middle[i] = split;
                uint32_t raw_left;
                uint32_t raw_right;
                linear_active_split_counts(
                    state, u, low[i], split, high[i], raw_left, raw_right);
                if (raw_left < (uint32_t)selected_left ||
                    raw_right < (uint32_t)selected_right) {
                    throw runtime_error(
                        "FM selected-vertex count exceeds a linear range count");
                }

                uint32_t d_left = raw_left - (uint32_t)selected_left;
                uint32_t d_right = raw_right - (uint32_t)selected_right;
                score_left.add_scaled(completions, d_left);
                score_right.add_scaled(completions, d_right);
            }

            if (available_left == 0 && available_right == 0)
                throw runtime_error("FM binary search exhausted its candidates");
            if (score_left.is_zero() && score_right.is_zero())
                throw runtime_error("FM binary search has zero conditional score");

            int branch;
            if (available_left == 0) {
                branch = 1;
            } else if (available_right == 0) {
                branch = 0;
            } else {
                BigUInt normalized_left = score_left;
                BigUInt normalized_right = score_right;
                normalized_left.multiply((uint32_t)available_right);
                normalized_right.multiply((uint32_t)available_left);
                branch = compare(normalized_left, normalized_right) >= 0 ? 0 : 1;
            }

            for (int i = 0; i < (int)U.size(); ++i) {
                if (branch == 0)
                    high[i] = middle[i];
                else
                    low[i] = middle[i];
            }

            if (branch == 1) range_begin = right_begin;
            range_length = half;
        }

        if (range_begin >= (uint64_t)n)
            throw runtime_error("FM selected a padded right vertex");
        int y = (int)range_begin;
        if (find(K.begin(), K.end(), y) != K.end())
            throw runtime_error("FM selected the same right vertex twice");

        next_U.clear();
        next_U.reserve(U.size());
        for (int i = 0; i < (int)U.size(); ++i) {
            uint32_t active =
                linear_active_count(state, U[i], low[i], high[i]);
            if (active != 0) next_U.push_back(U[i]);
        }
        if (next_U.empty())
            throw runtime_error("FM Clique Stripping produced an empty left side");

        K.push_back(y);
        U.swap(next_U);
    }
}

void record_biclique(FmRunStatistics& statistics,
                     uint64_t left_size,
                     int right_size,
                     bool star) {
    uint64_t edges = checked_multiply(
        left_size, (uint64_t)right_size, "FM stripped-edge count");
    uint64_t size = checked_add(
        left_size, (uint64_t)right_size, "FM nontrivial biclique size");

    statistics.nontrivial_size = checked_add(
        statistics.nontrivial_size, size, "FM nontrivial biclique size");
    ++statistics.center_count;
    if (star) {
        statistics.edges_in_star_bicliques = checked_add(
            statistics.edges_in_star_bicliques, edges,
            "FM star-biclique edge count");
        ++statistics.star_bicliques;
    } else {
        statistics.edges_in_k_ge_2_bicliques = checked_add(
            statistics.edges_in_k_ge_2_bicliques, edges,
            "FM k>=2 biclique edge count");
        ++statistics.k_ge_2_bicliques;
    }
}

vector<uint32_t> read_degrees(const CgfInfo& graph) {
    vector<uint32_t> degree((size_t)graph.n);
    CgfReader reader = open_cgf(graph);
    uint64_t sum = 0;
    for (int v = 0; v < (int)graph.n; ++v) {
        degree[v] = read_cgf_degree(reader);
        sum = checked_add(sum, degree[v], "CGF degree sum");
    }
    finish_cgf_pass(reader);
    if (sum != graph.adjacency_entries)
        throw runtime_error("CGF degree sum differs from 2m");
    return degree;
}

// At k=1, Lemma 2.1 gives c_b = sum_u d_{u,w.b}, which is
// exactly the sum of current right degrees in branch b. This one aggregate tree
// therefore makes precisely the same conditional-average choices as n separate
// neighborhood trees.
void strip_stars(int n,
                 long double delta,
                 vector<uint32_t>& right_degree,
                 uint64_t& remaining_edges,
                 int& k,
                 FmRunStatistics& statistics) {
    uint64_t leaf_range = 1;
    while (leaf_range < (uint64_t)n) leaf_range <<= 1U;
    if (leaf_range > (uint64_t)numeric_limits<size_t>::max() / 2U)
        throw runtime_error("FM aggregate degree tree exceeds size_t");

    size_t slots = (size_t)(2U * leaf_range);
    vector<uint64_t> sum;
    try {
        sum.assign(slots, 0);
    } catch (const bad_alloc&) {
        throw runtime_error(
            "Unable to allocate the aggregate FM degree tree (" +
            to_string(slots) + " slots)");
    }

    for (int v = 0; v < n; ++v)
        sum[(size_t)leaf_range + (size_t)v] = right_degree[v];
    for (uint64_t node = leaf_range - 1; node > 0; --node)
        sum[(size_t)node] = sum[(size_t)(2U * node)] +
                            sum[(size_t)(2U * node + 1U)];
    vector<uint32_t>().swap(right_degree);

    if (sum[1] != remaining_edges)
        throw runtime_error("FM aggregate degree sum differs from remaining edges");

    while (k == 1) {
        uint64_t node = 1;
        uint64_t range_begin = 0;
        uint64_t range_length = leaf_range;

        while (range_length > 1) {
            uint64_t half = range_length / 2U;
            uint64_t left = 2U * node;
            uint64_t right = left + 1U;
            uint64_t left_vertices =
                real_vertices(range_begin, half, n);
            uint64_t right_vertices =
                real_vertices(range_begin + half, half, n);

            int branch;
            if (left_vertices == 0) {
                branch = 1;
            } else if (right_vertices == 0) {
                branch = 0;
            } else {
                BigUInt normalized_left(sum[(size_t)left]);
                BigUInt normalized_right(sum[(size_t)right]);
                normalized_left.multiply((uint32_t)right_vertices);
                normalized_right.multiply((uint32_t)left_vertices);
                branch = compare(normalized_left, normalized_right) >= 0 ? 0 : 1;
            }

            node = branch == 0 ? left : right;
            if (branch == 1) range_begin += half;
            range_length = half;
        }

        if (range_begin >= (uint64_t)n)
            throw runtime_error("FM star search selected a padded right vertex");
        uint64_t degree = sum[(size_t)node];
        if (degree <= 1) {
            throw runtime_error(
                "FM k=1 search did not find a nontrivial star");
        }

        record_biclique(statistics, degree, 1, true);
        if (degree > remaining_edges)
            throw runtime_error("FM star removes more edges than remain");
        remaining_edges -= degree;

        sum[(size_t)node] = 0;
        while (node > 1) {
            node /= 2U;
            sum[(size_t)node] = sum[(size_t)(2U * node)] +
                                sum[(size_t)(2U * node + 1U)];
        }
        if (sum[1] != remaining_edges)
            throw runtime_error("FM star update changed the wrong edge count");

        k = fm_k(n, remaining_edges, delta);
    }
}

void finish_statistics(int n,
                       uint64_t remaining_edges,
                       int k,
                       FmRunStatistics& statistics) {
    statistics.final_k = k;
    statistics.residual_edges = remaining_edges;
    statistics.residual_size = checked_multiply(
        2U, remaining_edges, "FM residual biclique size");
    statistics.total_size = checked_add(
        statistics.nontrivial_size,
        statistics.residual_size,
        "FM total biclique-partition size");
    statistics.num_parts = checked_add(
        statistics.center_count, remaining_edges, "FM partition count");
    statistics.m_star = checked_add(
        checked_add(statistics.nontrivial_size, remaining_edges, "FM m_star"),
        (uint64_t)n,
        "FM m_star");
    statistics.n_star = checked_add(
        checked_multiply(2U, (uint64_t)n, "FM n_star"),
        statistics.center_count,
        "FM n_star");

    uint64_t partitioned_edges = checked_add(
        statistics.edges_in_k_ge_2_bicliques,
        statistics.edges_in_star_bicliques,
        "FM partitioned-edge count");
    partitioned_edges = checked_add(
        partitioned_edges, remaining_edges, "FM partitioned-edge count");
    if (partitioned_edges != statistics.initial_cross_edges)
        throw runtime_error("FM partition statistics do not sum to 2m");
    if (statistics.center_count !=
        statistics.k_ge_2_bicliques + statistics.star_bicliques) {
        throw runtime_error("FM center count is inconsistent");
    }
}

FmRunStatistics run_prefix_tree(const CgfInfo& graph,
                                const DeltaCandidate& candidate) {
    FmRunStatistics statistics;
    statistics.delta = candidate.delta;
    statistics.initial_k = candidate.initial_k;
    statistics.initial_cross_edges = checked_multiply(
        2U, graph.m, "FM initial cross-edge count");
    uint64_t remaining_edges = statistics.initial_cross_edges;
    int n = (int)graph.n;
    int k = fm_k(n, remaining_edges, candidate.delta);
    if (k != candidate.initial_k)
        throw runtime_error("FM delta did not produce its recorded initial k");

    Clock::time_point run_start = Clock::now();
    vector<uint32_t> right_degree;
    NeighborhoodTrees trees;

    Clock::time_point initialization_start = Clock::now();
    if (k >= 2) {
        build_neighborhood_trees(graph, trees, right_degree);
        statistics.neighborhood_tree_nodes = trees.nodes.size();
    } else if (k == 1) {
        right_degree = read_degrees(graph);
    }
    statistics.initialization_seconds = elapsed_seconds(initialization_start);

    Clock::time_point k_ge_2_start = Clock::now();
    vector<int> U;
    vector<int> K;
    while (k >= 2) {
        find_biclique_prefix_tree(trees, k, U, K);
        uint64_t removed = checked_multiply(
            (uint64_t)U.size(), (uint64_t)K.size(),
            "FM k>=2 biclique edge count");
        if (removed > remaining_edges)
            throw runtime_error("FM biclique removes more edges than remain");

        remove_biclique(trees, U, K);
        for (int v : K) {
            if (right_degree[v] < U.size())
                throw runtime_error("FM right-degree update underflows");
            right_degree[v] -= (uint32_t)U.size();
        }
        record_biclique(statistics, U.size(), (int)K.size(), false);
        remaining_edges -= removed;
        k = fm_k(n, remaining_edges, candidate.delta);
    }
    statistics.k_ge_2_stripping_seconds = elapsed_seconds(k_ge_2_start);

    Clock::time_point star_start = Clock::now();
    if (k == 1) {
        // k never increases as edges are removed. Once k>=2 stripping has
        // finished, its dominant tree allocation cannot be needed again.
        trees = {};
        strip_stars(n, candidate.delta, right_degree,
                    remaining_edges, k, statistics);
    }
    statistics.star_stripping_seconds = elapsed_seconds(star_start);

    finish_statistics(n, remaining_edges, k, statistics);
    statistics.total_seconds = elapsed_seconds(run_start);
    return statistics;
}

FmRunStatistics run_linear(const CgfInfo& graph,
                           const DeltaCandidate& candidate,
                           const LinearAdjacency& adjacency) {
    FmRunStatistics statistics;
    statistics.delta = candidate.delta;
    statistics.initial_k = candidate.initial_k;
    statistics.initial_cross_edges = checked_multiply(
        2U, graph.m, "FM initial cross-edge count");
    uint64_t remaining_edges = statistics.initial_cross_edges;
    int n = (int)graph.n;
    int k = fm_k(n, remaining_edges, candidate.delta);
    if (k != candidate.initial_k)
        throw runtime_error("FM delta did not produce its recorded initial k");

    Clock::time_point run_start = Clock::now();
    vector<uint32_t> right_degree;
    LinearState state;

    Clock::time_point initialization_start = Clock::now();
    if (k >= 2) {
        initialize_linear_state(adjacency, state, right_degree);
    } else if (k == 1) {
        right_degree = read_degrees(graph);
    }
    statistics.initialization_seconds = elapsed_seconds(initialization_start);

    Clock::time_point k_ge_2_start = Clock::now();
    vector<int> U;
    vector<int> K;
    while (k >= 2) {
        find_biclique_linear(adjacency, state, k, U, K);
        uint64_t removed = checked_multiply(
            (uint64_t)U.size(), (uint64_t)K.size(),
            "FM k>=2 biclique edge count");
        if (removed > remaining_edges)
            throw runtime_error("FM biclique removes more edges than remain");

        // The immutable packed adjacency remains shared by all candidate runs. Only
        // deletion bits, block ranks, and the two endpoint degrees change.
        for (int u : U)
            for (int v : K) delete_linear_edge(state, u, v);

        for (int v : K) {
            if (right_degree[v] < U.size())
                throw runtime_error("FM right-degree update underflows");
            right_degree[v] -= (uint32_t)U.size();
        }

        record_biclique(statistics, U.size(), (int)K.size(), false);
        remaining_edges -= removed;
        k = fm_k(n, remaining_edges, candidate.delta);
    }
    statistics.k_ge_2_stripping_seconds = elapsed_seconds(k_ge_2_start);

    Clock::time_point star_start = Clock::now();
    if (k == 1) {
        // No later iteration can return to k >= 2. Release the mutable deletion
        // index before allocating the aggregate right-degree tree.
        state = {};
        strip_stars(n, candidate.delta, right_degree,
                    remaining_edges, k, statistics);
    }
    statistics.star_stripping_seconds = elapsed_seconds(star_start);

    finish_statistics(n, remaining_edges, k, statistics);
    statistics.total_seconds = elapsed_seconds(run_start);
    return statistics;
}

FmRunStatistics trivial_partition(const CgfInfo& graph) {
    FmRunStatistics statistics;
    statistics.initial_k = 0;
    statistics.initial_cross_edges = checked_multiply(
        2U, graph.m, "FM initial cross-edge count");
    finish_statistics((int)graph.n, statistics.initial_cross_edges,
                      0, statistics);
    return statistics;
}

bool better_run(const FmRunStatistics& candidate,
                const FmRunStatistics& current) {
    if (candidate.total_size != current.total_size)
        return candidate.total_size < current.total_size;
    if (candidate.m_star != current.m_star)
        return candidate.m_star < current.m_star;
    if (candidate.center_count != current.center_count)
        return candidate.center_count < current.center_count;
    return candidate.delta < current.delta;
}

long double initial_delta_scale(const CgfInfo& graph) {
    if (graph.n < 2 || graph.m == 0) {
        return 0.0L;
    }

    // Initially the directed bipartite graph has 2m edges, so Definition 2.1's
    // log(2n^2/m_hat) denominator becomes log(n^2/m).
    return log2l((long double)graph.n) /
           log2l((long double)graph.n * (long double)graph.n /
                 (long double)graph.m);
}

vector<DeltaCandidate> delta_candidates(const CgfInfo& graph,
                                        long double& scale_a) {
    vector<DeltaCandidate> candidates;
    scale_a = initial_delta_scale(graph);

    for (int initial_k = 1; (long double)initial_k < scale_a; ++initial_k) {
        long double lower = (long double)initial_k / scale_a;
        long double upper = min(
            (long double)(initial_k + 1) / scale_a, 1.0L);
        if (!(lower < upper)) continue;
        candidates.push_back({(lower + upper) / 2.0L, initial_k});
    }
    return candidates;
}

}  // namespace

const char* workspace_method_name(WorkspaceMethod method) {
    switch (method) {
        case WorkspaceMethod::Linear: return "linear";
        case WorkspaceMethod::PrefixTree: return "prefix-tree";
    }
    return "unknown";
}

FmResult construct_fm(const filesystem::path& cgf_path,
                      FmOptions options) {
    if (options.fixed_delta &&
        (!isfinite(options.delta) ||
         !(options.delta > 0.0L && options.delta < 1.0L))) {
        throw runtime_error("FM delta must satisfy 0 < delta < 1");
    }

    FmResult result;
    result.workspace = options.workspace;
    result.fixed_delta = options.fixed_delta;
    result.delta_argument = options.delta_argument;
    result.graph = read_cgf_info(cgf_path);

    vector<DeltaCandidate> candidates;
    if (options.fixed_delta) {
        result.scale_a = initial_delta_scale(result.graph);
        uint64_t initial_cross_edges = checked_multiply(
            2U, result.graph.m, "FM initial cross-edge count");
        int initial_k = fm_k(
            (int)result.graph.n, initial_cross_edges, options.delta);
        candidates.push_back({options.delta, initial_k});
    } else {
        candidates = delta_candidates(result.graph, result.scale_a);
    }

    result.candidate_count = candidates.size();
    if (candidates.empty()) {
        result.selected = trivial_partition(result.graph);
        return result;
    }

    result.initial_k_min = candidates.front().initial_k;
    result.initial_k_max = candidates.back().initial_k;
    result.candidate_delta_min = candidates.front().delta;
    result.candidate_delta_max = candidates.back().delta;
    result.selected_delta_exists = true;

    Clock::time_point selection_start = Clock::now();
    LinearAdjacency linear_adjacency;
    if (options.workspace == WorkspaceMethod::Linear &&
        candidates.back().initial_k >= 2) {
        Clock::time_point input_index_start = Clock::now();
        build_linear_adjacency(result.graph, linear_adjacency);
        result.input_index_seconds = elapsed_seconds(input_index_start);
        result.packed_neighbor_bits = linear_adjacency.bits_per_neighbor;
        result.packed_neighbor_words = linear_adjacency.neighbor_word.size();
        result.deleted_edge_words =
            (linear_adjacency.entries + 63U) / 64U;
        result.deletion_rank_entries = linear_adjacency.rank_entries;
    }

    bool have_best = false;
    for (const DeltaCandidate& candidate : candidates) {
        FmRunStatistics run;
        if (options.workspace == WorkspaceMethod::Linear) {
            run = run_linear(
                result.graph, candidate, linear_adjacency);
        } else {
            run = run_prefix_tree(result.graph, candidate);
        }
        if (!have_best || better_run(run, result.selected)) {
            result.selected = run;
            have_best = true;
        }
    }
    result.delta_runs_seconds = elapsed_seconds(selection_start);
    return result;
}

}  // namespace fm_con
