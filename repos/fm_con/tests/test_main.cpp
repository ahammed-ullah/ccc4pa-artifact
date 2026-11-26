#include "fm_con/big_uint.hpp"
#include "fm_con/cgf.hpp"
#include "fm_con/fm.hpp"
#include "fm_con/linear_index.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
using namespace fm_con;

namespace {

struct RefRun {
    long double delta = 0;
    int initial_k = 0;
    uint64_t P = 0;
    uint64_t r = 0;
    uint64_t q = 0;
    uint64_t k_ge_2 = 0;
    uint64_t stars = 0;
    uint64_t total_size = 0;
    uint64_t m_star = 0;
};

void require(bool condition, const string& message) {
    if (!condition) throw runtime_error(message);
}

void store_u32(vector<uint8_t>& bytes, int position, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        bytes[position + i] = (uint8_t)(value >> (8 * i));
}

void store_u64(vector<uint8_t>& bytes, int position, uint64_t value) {
    for (int i = 0; i < 8; ++i)
        bytes[position + i] = (uint8_t)(value >> (8 * i));
}

void append_u64(vector<uint8_t>& bytes, uint64_t value) {
    for (int i = 0; i < 8; ++i)
        bytes.push_back((uint8_t)(value >> (8 * i)));
}

void append_vbyte(vector<uint8_t>& bytes, uint64_t value) {
    while (value >= 128) {
        bytes.push_back((uint8_t)((value & 127U) | 128U));
        value >>= 7U;
    }
    bytes.push_back((uint8_t)value);
}

filesystem::path write_test_cgf(const vector<vector<int>>& adj,
                                const string& name) {
    int n = (int)adj.size();
    uint64_t entries = 0;
    for (auto& N : adj) entries += N.size();
    require(entries % 2 == 0, "test graph has odd adjacency size");
    uint64_t m = entries / 2;

    constexpr int header_size = 160;
    uint64_t payload_offset = header_size + 8ULL * (n + 1ULL);
    vector<uint8_t> payload;
    vector<uint64_t> offset;
    offset.reserve(n + 1);

    for (auto& N : adj) {
        offset.push_back(payload_offset + payload.size());
        append_vbyte(payload, N.size());
        uint64_t previous = 0;
        for (int i = 0; i < (int)N.size(); ++i) {
            uint64_t v = (uint64_t)N[i];
            append_vbyte(payload, i == 0 ? v : v - previous);
            previous = v;
        }
    }
    offset.push_back(payload_offset + payload.size());

    vector<uint8_t> file(header_size, 0);
    array<uint8_t, 8> magic{'C', 'G', 'F', 0, '\r', '\n', 0x1aU, '\n'};
    copy(magic.begin(), magic.end(), file.begin());
    store_u32(file, 8, 1);
    store_u32(file, 12, header_size);
    store_u32(file, 16, 1);
    store_u64(file, 24, n);
    store_u64(file, 32, m);
    store_u64(file, 40, n);
    store_u64(file, 48, entries);
    store_u64(file, 56, header_size);
    store_u64(file, 64, n + 1ULL);
    store_u64(file, 72, payload_offset);
    store_u64(file, 80, payload.size());
    store_u64(file, 88, payload_offset + payload.size());
    for (uint64_t value : offset) append_u64(file, value);
    file.insert(file.end(), payload.begin(), payload.end());

    filesystem::path path = filesystem::temp_directory_path() / name;
    ofstream output(path, ios::binary | ios::trunc);
    output.write((char*)file.data(), (streamsize)file.size());
    output.close();
    require((bool)output, "failed to write test CGF");
    return path;
}

vector<vector<int>> complete_graph(int n) {
    vector<vector<int>> adj(n);
    for (int u = 0; u < n; ++u) {
        for (int v = 0; v < n; ++v) {
            if (u != v) adj[u].push_back(v);
        }
    }
    return adj;
}

vector<vector<int>> cycle_graph(int n) {
    vector<vector<int>> adj(n);
    for (int u = 0; u < n; ++u) {
        adj[u].push_back((u + n - 1) % n);
        adj[u].push_back((u + 1) % n);
        sort(adj[u].begin(), adj[u].end());
    }
    return adj;
}

void remove_edge(vector<vector<int>>& adj, int u, int v) {
    adj[u].erase(find(adj[u].begin(), adj[u].end(), v));
    adj[v].erase(find(adj[v].begin(), adj[v].end(), u));
}

int ref_k(int n, uint64_t m, long double delta) {
    if (m == 0) return 0;
    long double value = delta * log2l((long double)n) /
        log2l(2.0L * n * n / (long double)m);
    return value < 1 ? 0 : (int)floorl(value);
}

BigUInt ref_falling(uint32_t value, int terms) {
    if (terms == 0) return BigUInt(1);
    if (value < (uint32_t)terms) return BigUInt();
    BigUInt result(1);
    for (int i = 0; i < terms; ++i) result.multiply(value - i);
    return result;
}

int count_selected(const vector<int>& K, uint64_t begin, uint64_t length) {
    int count = 0;
    for (int v : K)
        if ((uint64_t)v >= begin && (uint64_t)v < begin + length) ++count;
    return count;
}

uint64_t count_real(uint64_t begin, uint64_t length, int n) {
    if (begin >= (uint64_t)n) return 0;
    return min(begin + length, (uint64_t)n) - begin;
}

void reference_biclique(const vector<vector<unsigned char>>& edge,
                        int k,
                        vector<int>& U,
                        vector<int>& K) {
    int n = (int)edge.size();
    uint64_t leaf_range = 1;
    while (leaf_range < (uint64_t)n) leaf_range <<= 1U;

    U.resize(n);
    for (int u = 0; u < n; ++u) U[u] = u;
    K.clear();

    for (int t = 1; t <= k; ++t) {
        uint64_t begin = 0;
        uint64_t length = leaf_range;
        while (length > 1) {
            uint64_t half = length / 2;
            int selected_left = count_selected(K, begin, half);
            int selected_right = count_selected(K, begin + half, half);
            uint64_t available_left = count_real(begin, half, n) - selected_left;
            uint64_t available_right =
                count_real(begin + half, half, n) - selected_right;

            BigUInt left_score, right_score;
            for (int u : U) {
                uint32_t degree = 0;
                uint32_t left_degree = 0;
                uint32_t right_degree = 0;
                for (int v = 0; v < n; ++v) {
                    if (!edge[u][v] || find(K.begin(), K.end(), v) != K.end())
                        continue;
                    ++degree;
                    if ((uint64_t)v >= begin && (uint64_t)v < begin + half)
                        ++left_degree;
                    if ((uint64_t)v >= begin + half &&
                        (uint64_t)v < begin + length)
                        ++right_degree;
                }
                BigUInt completions = degree == 0
                    ? BigUInt()
                    : ref_falling(degree - 1, k - t);
                left_score.add_scaled(completions, left_degree);
                right_score.add_scaled(completions, right_degree);
            }

            int branch;
            if (available_left == 0) branch = 1;
            else if (available_right == 0) branch = 0;
            else {
                BigUInt lhs = left_score;
                BigUInt rhs = right_score;
                lhs.multiply((uint32_t)available_right);
                rhs.multiply((uint32_t)available_left);
                branch = compare(lhs, rhs) >= 0 ? 0 : 1;
            }
            if (branch == 1) begin += half;
            length = half;
        }

        int y = (int)begin;
        vector<int> next;
        for (int u : U)
            if (edge[u][y]) next.push_back(u);
        require(!next.empty(), "reference FM selected an empty side");
        K.push_back(y);
        U.swap(next);
    }
}

RefRun reference_run(const vector<vector<int>>& adj,
                     long double delta,
                     int initial_k) {
    int n = (int)adj.size();
    vector<vector<unsigned char>> edge(n, vector<unsigned char>(n, 0));
    uint64_t remaining = 0;
    for (int u = 0; u < n; ++u) {
        for (int v : adj[u]) {
            edge[u][v] = 1;
            ++remaining;
        }
    }

    RefRun result;
    result.delta = delta;
    result.initial_k = initial_k;
    int k = ref_k(n, remaining, delta);
    require(k == initial_k, "reference candidate has wrong initial k");

    vector<int> U, K;
    while (k >= 1) {
        reference_biclique(edge, k, U, K);
        uint64_t removed = (uint64_t)U.size() * K.size();
        require(removed > 1, "reference FM produced a singleton stripping");
        for (int u : U)
            for (int v : K) edge[u][v] = 0;

        result.P += U.size() + K.size();
        ++result.q;
        if (k == 1) ++result.stars;
        else ++result.k_ge_2;
        remaining -= removed;
        k = ref_k(n, remaining, delta);
    }

    result.r = remaining;
    result.total_size = result.P + 2 * result.r;
    result.m_star = result.P + result.r + n;
    return result;
}

RefRun reference_automatic(const vector<vector<int>>& adj,
                           uint64_t m,
                           int& candidate_count) {
    int n = (int)adj.size();
    long double a = log2l((long double)n) /
        log2l((long double)n * n / m);
    bool have_best = false;
    RefRun best;
    candidate_count = 0;

    for (int j = 1; (long double)j < a; ++j) {
        long double lo = j / a;
        long double hi = min((j + 1) / a, 1.0L);
        RefRun run = reference_run(adj, (lo + hi) / 2, j);
        ++candidate_count;
        bool better = !have_best || run.total_size < best.total_size ||
            (run.total_size == best.total_size && run.m_star < best.m_star) ||
            (run.total_size == best.total_size && run.m_star == best.m_star &&
             run.q < best.q) ||
            (run.total_size == best.total_size && run.m_star == best.m_star &&
             run.q == best.q && run.delta < best.delta);
        if (better) {
            best = run;
            have_best = true;
        }
    }
    return best;
}

void check_result(const FmResult& actual,
                  const RefRun& expected,
                  int candidate_count,
                  const string& name) {
    require(actual.candidate_count == (uint64_t)candidate_count,
            name + ": candidate count differs");
    require(actual.selected.initial_k == expected.initial_k,
            name + ": selected initial k differs");
    require(actual.selected.nontrivial_size == expected.P,
            name + ": nontrivial size differs");
    require(actual.selected.residual_edges == expected.r,
            name + ": residual edge count differs");
    require(actual.selected.center_count == expected.q,
            name + ": center count differs");
    require(actual.selected.k_ge_2_bicliques == expected.k_ge_2,
            name + ": k>=2 biclique count differs");
    require(actual.selected.star_bicliques == expected.stars,
            name + ": star biclique count differs");
    require(actual.selected.total_size == expected.total_size,
            name + ": total size differs");
    require(actual.selected.m_star == expected.m_star,
            name + ": m_star differs");
}

void compare_with_reference(vector<vector<int>> adj, const string& name) {
    uint64_t m = 0;
    for (auto& N : adj) m += N.size();
    m /= 2;
    filesystem::path path = write_test_cgf(adj, name + ".cgf");

    int candidate_count;
    RefRun expected = reference_automatic(adj, m, candidate_count);

    FmOptions linear_options;
    FmResult linear = construct_fm(path, linear_options);
    check_result(linear, expected, candidate_count, name + " linear");

    FmOptions tree_options;
    tree_options.workspace = WorkspaceMethod::PrefixTree;
    FmResult tree = construct_fm(path, tree_options);
    check_result(tree, expected, candidate_count, name + " prefix-tree");

    require(linear.selected.total_size == tree.selected.total_size,
            name + ": backends select different total sizes");
    require(linear.selected.nontrivial_size == tree.selected.nontrivial_size,
            name + ": backends select different nontrivial sizes");
    filesystem::remove(path);
}

void test_fixed_delta() {
    vector<vector<int>> adj = complete_graph(8);
    filesystem::path path = write_test_cgf(adj, "fm-fixed-delta.cgf");
    long double delta = 0.8L;
    uint64_t cross_edges = 0;
    for (auto& N : adj) cross_edges += N.size();
    int initial_k = ref_k((int)adj.size(), cross_edges, delta);
    require(initial_k >= 2, "fixed-delta test must exercise k>=2 stripping");
    RefRun expected = reference_run(adj, delta, initial_k);

    FmOptions linear_options;
    linear_options.fixed_delta = true;
    linear_options.delta = delta;
    linear_options.delta_argument = "0.8";
    FmResult linear = construct_fm(path, linear_options);
    check_result(linear, expected, 1, "fixed delta linear");
    require(linear.fixed_delta,
            "fixed-delta result lost its selection mode");
    require(linear.initial_k_min == initial_k &&
                linear.initial_k_max == initial_k,
            "fixed-delta initial-k range differs");

    FmOptions tree_options = linear_options;
    tree_options.workspace = WorkspaceMethod::PrefixTree;
    FmResult tree = construct_fm(path, tree_options);
    check_result(tree, expected, 1, "fixed delta prefix-tree");
    require(linear.selected.total_size == tree.selected.total_size,
            "fixed-delta backends produce different total sizes");

    filesystem::remove(path);
}

void test_linear_index_blocks() {
    vector<vector<int>> adj = complete_graph(260);
    filesystem::path path = write_test_cgf(adj, "fm-linear-blocks.cgf");
    CgfInfo graph = read_cgf_info(path);

    LinearAdjacency adjacency;
    build_linear_adjacency(graph, adjacency);
    require(adjacency.bits_per_neighbor == 9,
            "linear index should use nine-bit neighbor IDs");
    require(linear_degree(adjacency, 0) == 259,
            "linear index degree differs");
    for (int position = 0; position < 259; ++position) {
        require(linear_neighbor(adjacency, 0, position) ==
                    (uint32_t)(position + 1),
                "packed neighbor differs");
    }

    LinearState state;
    vector<uint32_t> right_degree;
    initialize_linear_state(adjacency, state, right_degree);
    delete_linear_edge(state, 0, 1);    // Local position 0.
    delete_linear_edge(state, 0, 256);  // Local position 255.
    delete_linear_edge(state, 0, 257);  // Local position 256.

    require(linear_active_count(state, 0, 0, 259) == 256,
            "linear full-row count differs after deletions");
    require(linear_active_count(state, 0, 0, 256) == 254,
            "linear first-block count differs after deletions");
    require(linear_active_count(state, 0, 256, 259) == 2,
            "linear second-block count differs after deletions");
    require(state.left_degree[0] == 256,
            "linear left degree differs after deletions");

    FmResult linear = construct_fm(path);
    FmOptions tree_options;
    tree_options.workspace = WorkspaceMethod::PrefixTree;
    FmResult tree = construct_fm(path, tree_options);
    require(linear.selected.initial_k == tree.selected.initial_k,
            "block test backends select different initial k");
    require(linear.selected.nontrivial_size ==
                tree.selected.nontrivial_size,
            "block test backends produce different nontrivial sizes");
    require(linear.selected.residual_edges == tree.selected.residual_edges,
            "block test backends produce different residual sizes");
    require(linear.selected.total_size == tree.selected.total_size,
            "block test backends produce different total sizes");

    filesystem::remove(path);
}

void test_sparse_direct_path() {
    vector<vector<int>> adj = cycle_graph(5);
    filesystem::path path = write_test_cgf(adj, "fm-cycle-five.cgf");
    FmResult result = construct_fm(path);
    require(result.candidate_count == 0, "C5 should have no nonzero-k candidate");
    require(result.selected.residual_edges == 10, "C5 residual cross-edge count");
    require(result.selected.total_size == 20, "C5 total size");
    require(result.selected.m_star == 15, "C5 m_star");
    require(result.selected.n_star == 10, "C5 n_star");
    filesystem::remove(path);
}

void test_k_one_path() {
    vector<vector<int>> adj = complete_graph(4);
    filesystem::path path = write_test_cgf(adj, "fm-complete-four.cgf");
    FmResult result = construct_fm(path);
    require(result.candidate_count == 1, "K4 should have one delta candidate");
    require(result.selected.initial_k == 1, "K4 candidate should start at k=1");
    require(result.selected.neighborhood_tree_nodes == 0,
            "K4 should use only the aggregate k=1 tree");
    require(result.selected.nontrivial_size == 4, "K4 nontrivial size");
    require(result.selected.residual_edges == 9, "K4 residual edge count");
    require(result.selected.total_size == 22, "K4 total size");
    require(result.selected.center_count == 1, "K4 center count");
    filesystem::remove(path);
}

void test_general_tree_path() {
    // Seven vertices exercise the unequal branch sizes used when n is not a
    // power of two. The direct reference computes the same branch averages
    // without neighborhood trees.
    compare_with_reference(complete_graph(7), "fm-complete-seven");
    compare_with_reference(complete_graph(8), "fm-complete-eight");

    vector<vector<int>> adj = complete_graph(8);
    remove_edge(adj, 0, 1);
    remove_edge(adj, 2, 3);
    remove_edge(adj, 4, 5);
    remove_edge(adj, 6, 7);
    compare_with_reference(adj, "fm-dense-eight");
}

void test_big_uint() {
    BigUInt value(1);
    for (int i = 0; i < 20; ++i) value.multiply(1000000);
    BigUInt twice;
    twice.add_scaled(value, 2);
    BigUInt other = value;
    other.multiply(2);
    require(compare(twice, other) == 0, "BigUInt scaled addition differs");
    other.multiply(2);
    require(compare(other, twice) > 0, "BigUInt comparison differs");
}

}  // namespace

int main() {
    try {
        test_big_uint();
        test_linear_index_blocks();
        test_fixed_delta();
        test_sparse_direct_path();
        test_k_one_path();
        test_general_tree_path();
        cout << "all fm_con tests passed\n";
        return 0;
    } catch (const exception& error) {
        cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
