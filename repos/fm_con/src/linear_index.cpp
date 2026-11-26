#include "fm_con/linear_index.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace fm_con {
namespace {

int bits_needed(uint64_t n) {
    int bits = 1;
    while ((1ULL << bits) < n) ++bits;
    return bits;
}

uint64_t packed_mask(int bits) {
    return (1ULL << bits) - 1U;
}

void store_neighbor(LinearAdjacency& adjacency,
                    uint64_t position,
                    uint32_t value) {
    uint64_t bit_position = position * (uint64_t)adjacency.bits_per_neighbor;
    size_t word = (size_t)(bit_position >> 6U);
    int shift = (int)(bit_position & 63U);

    adjacency.neighbor_word[word] |= (uint64_t)value << shift;
    if (shift + adjacency.bits_per_neighbor > 64) {
        adjacency.neighbor_word[word + 1] |=
            (uint64_t)value >> (64 - shift);
    }
}

uint32_t load_neighbor(const LinearAdjacency& adjacency,
                       uint64_t position) {
    uint64_t bit_position = position * (uint64_t)adjacency.bits_per_neighbor;
    size_t word = (size_t)(bit_position >> 6U);
    int shift = (int)(bit_position & 63U);

    uint64_t value = adjacency.neighbor_word[word] >> shift;
    if (shift + adjacency.bits_per_neighbor > 64) {
        value |= adjacency.neighbor_word[word + 1] << (64 - shift);
    }
    return (uint32_t)(value & packed_mask(adjacency.bits_per_neighbor));
}

uint32_t popcount_deleted(const LinearState& state,
                          uint64_t first,
                          uint32_t length) {
    uint32_t count = 0;
    while (length != 0) {
        size_t word = (size_t)(first >> 6U);
        int offset = (int)(first & 63U);
        uint32_t take = min<uint32_t>(length, 64U - (uint32_t)offset);
        uint64_t bits = state.deleted[word] >> offset;
        if (take < 64) bits &= (1ULL << take) - 1U;
        count += (uint32_t)__builtin_popcountll(bits);
        first += take;
        length -= take;
    }
    return count;
}

uint32_t deleted_block_prefix(const LinearState& state,
                              int u,
                              uint32_t blocks) {
    const LinearAdjacency& adjacency = *state.adjacency;
    uint64_t base = adjacency.rank_begin[u];
    uint32_t count = 0;
    for (uint32_t i = blocks; i != 0; i -= i & -i)
        count += state.deleted_rank[(size_t)(base + i)];
    return count;
}

uint32_t deleted_prefix(const LinearState& state,
                        int u,
                        uint32_t position) {
    const LinearAdjacency& adjacency = *state.adjacency;
    uint32_t full_blocks = position / kLinearBlockEdges;
    uint32_t remainder = position % kLinearBlockEdges;
    uint32_t count = deleted_block_prefix(state, u, full_blocks);
    uint64_t first = adjacency.begin[u] +
                     (uint64_t)full_blocks * kLinearBlockEdges;
    return count + popcount_deleted(state, first, remainder);
}

bool is_deleted(const LinearState& state, uint64_t position) {
    return (state.deleted[(size_t)(position >> 6U)] &
            (1ULL << (position & 63U))) != 0;
}

}  // namespace

void build_linear_adjacency(const CgfInfo& graph,
                            LinearAdjacency& adjacency) {
    adjacency = {};
    adjacency.n = (int)graph.n;
    adjacency.entries = graph.adjacency_entries;
    adjacency.bits_per_neighbor = bits_needed(graph.n);

    if (adjacency.entries >
        (numeric_limits<uint64_t>::max() - 63U) /
        (uint64_t)adjacency.bits_per_neighbor) {
        throw runtime_error("Packed FM adjacency bit count overflows uint64");
    }
    uint64_t total_bits =
        adjacency.entries * (uint64_t)adjacency.bits_per_neighbor;
    uint64_t packed_words = (total_bits + 63U) / 64U;
    if (packed_words == numeric_limits<uint64_t>::max())
        throw runtime_error("Packed FM adjacency word count overflows uint64");
    ++packed_words;  // Guard word for values crossing the final word boundary.

    if (packed_words > (uint64_t)numeric_limits<size_t>::max() ||
        packed_words > (uint64_t)adjacency.neighbor_word.max_size()) {
        throw runtime_error("Packed FM adjacency exceeds vector address space");
    }

    try {
        adjacency.begin.resize((size_t)graph.n + 1U);
        adjacency.rank_begin.resize((size_t)graph.n + 1U);
        adjacency.neighbor_word.assign((size_t)packed_words, 0);
    } catch (const bad_alloc&) {
        throw runtime_error(
            "Unable to allocate the packed linear-space FM adjacency (" +
            to_string(packed_words) + " 64-bit words)");
    }

    vector<int> neighbors;
    uint64_t position = 0;
    uint64_t rank_entries = 0;
    CgfReader reader = open_cgf(graph);

    for (int u = 0; u < adjacency.n; ++u) {
        read_cgf_neighborhood(reader, neighbors);
        adjacency.begin[u] = position;
        adjacency.rank_begin[u] = rank_entries;

        uint64_t blocks =
            (neighbors.size() + kLinearBlockEdges - 1U) /
            kLinearBlockEdges;
        if (rank_entries > numeric_limits<uint64_t>::max() - blocks - 1U)
            throw runtime_error("Linear-space FM rank size overflows uint64");
        rank_entries += blocks + 1U;  // Local Fenwick index zero is unused.

        for (int v : neighbors) {
            store_neighbor(adjacency, position, (uint32_t)v);
            ++position;
        }
    }
    finish_cgf_pass(reader);

    adjacency.begin[adjacency.n] = position;
    adjacency.rank_begin[adjacency.n] = rank_entries;
    adjacency.rank_entries = rank_entries;
    if (position != adjacency.entries)
        throw runtime_error("Packed FM adjacency size differs from CGF 2m");
}

void initialize_linear_state(const LinearAdjacency& adjacency,
                             LinearState& state,
                             vector<uint32_t>& right_degree) {
    state = {};
    state.adjacency = &adjacency;

    uint64_t deleted_words = (adjacency.entries + 63U) / 64U;
    if (deleted_words > (uint64_t)numeric_limits<size_t>::max() ||
        adjacency.rank_entries > (uint64_t)numeric_limits<size_t>::max()) {
        throw runtime_error("Linear-space FM state exceeds vector address space");
    }

    try {
        state.deleted.assign((size_t)deleted_words, 0);
        state.deleted_rank.assign((size_t)adjacency.rank_entries, 0);
        state.left_degree.resize(adjacency.n);
        right_degree.resize(adjacency.n);
    } catch (const bad_alloc&) {
        throw runtime_error(
            "Unable to allocate the dynamic linear-space FM state (" +
            to_string(deleted_words) + " deletion words, " +
            to_string(adjacency.rank_entries) + " rank entries)");
    }

    for (int u = 0; u < adjacency.n; ++u) {
        uint32_t degree = linear_degree(adjacency, u);
        state.left_degree[u] = degree;
        right_degree[u] = degree;
    }
}

uint32_t linear_degree(const LinearAdjacency& adjacency, int u) {
    return (uint32_t)(adjacency.begin[u + 1] - adjacency.begin[u]);
}

uint32_t linear_neighbor(const LinearAdjacency& adjacency,
                         int u,
                         uint32_t position) {
    return load_neighbor(adjacency, adjacency.begin[u] + position);
}

uint32_t linear_lower_bound(const LinearAdjacency& adjacency,
                            int u,
                            uint32_t low,
                            uint32_t high,
                            int value) {
    while (low < high) {
        uint32_t middle = low + (high - low) / 2U;
        if (linear_neighbor(adjacency, u, middle) < (uint32_t)value)
            low = middle + 1U;
        else
            high = middle;
    }
    return low;
}

uint32_t linear_active_count(const LinearState& state,
                             int u,
                             uint32_t low,
                             uint32_t high) {
    uint32_t removed =
        deleted_prefix(state, u, high) - deleted_prefix(state, u, low);
    return high - low - removed;
}

void linear_active_split_counts(const LinearState& state,
                                int u,
                                uint32_t low,
                                uint32_t middle,
                                uint32_t high,
                                uint32_t& left,
                                uint32_t& right) {
    uint32_t deleted_low = deleted_prefix(state, u, low);
    uint32_t deleted_middle = deleted_prefix(state, u, middle);
    uint32_t deleted_high = deleted_prefix(state, u, high);
    left = middle - low - (deleted_middle - deleted_low);
    right = high - middle - (deleted_high - deleted_middle);
}

void delete_linear_edge(LinearState& state, int u, int v) {
    const LinearAdjacency& adjacency = *state.adjacency;
    uint32_t degree = linear_degree(adjacency, u);
    uint32_t position = linear_lower_bound(adjacency, u, 0, degree, v);
    if (position == degree || linear_neighbor(adjacency, u, position) !=
                              (uint32_t)v) {
        throw runtime_error("FM attempted to delete an absent linear-index edge");
    }

    uint64_t global_position = adjacency.begin[u] + position;
    if (is_deleted(state, global_position))
        throw runtime_error("FM attempted to delete an edge twice");
    state.deleted[(size_t)(global_position >> 6U)] |=
        1ULL << (global_position & 63U);

    uint32_t blocks =
        (degree + kLinearBlockEdges - 1U) / kLinearBlockEdges;
    uint32_t block = position / kLinearBlockEdges + 1U;
    uint64_t base = adjacency.rank_begin[u];
    for (uint32_t i = block; i <= blocks; i += i & -i)
        ++state.deleted_rank[(size_t)(base + i)];

    if (state.left_degree[u] == 0)
        throw runtime_error("FM linear-index left degree underflows");
    --state.left_degree[u];
}

}  // namespace fm_con
