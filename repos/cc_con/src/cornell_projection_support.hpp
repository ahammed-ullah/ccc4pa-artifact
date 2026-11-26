#ifndef CC_CON_CORNELL_PROJECTION_SUPPORT_HPP
#define CC_CON_CORNELL_PROJECTION_SUPPORT_HPP

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cc_con::cornell_detail {

inline std::uint64_t mix_edge_key(std::uint64_t value) {
    // The endpoint pair itself is the key. This finalizer only spreads nearby
    // integer IDs across the power-of-two open-addressing table.
    value ^= value >> 30;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    return value;
}

inline std::uint64_t edge_key(const int first, const int second) {
    const int u = std::min(first, second);
    const int v = std::max(first, second);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(u)) << 32) |
           static_cast<std::uint32_t>(v);
}

inline int edge_key_first(const std::uint64_t key) {
    return static_cast<int>(static_cast<std::uint32_t>(key >> 32));
}

inline int edge_key_second(const std::uint64_t key) {
    return static_cast<int>(static_cast<std::uint32_t>(key));
}

inline std::uint64_t choose_two(const std::size_t size) {
    const std::uint64_t s = static_cast<std::uint64_t>(size);
    return s < 2 ? 0 : s * (s - 1) / 2;
}

inline void add_to_counter(std::int64_t& counter,
                           const std::uint64_t amount,
                           const std::string& name) {
    if (amount > static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max() - counter)) {
        throw std::runtime_error(name + " exceeds signed 64-bit range");
    }
    counter += static_cast<std::int64_t>(amount);
}

inline int checked_external_id(const long long value,
                               const std::string& location) {
    // INT_MAX is excluded because max_id + 1 must itself fit the project's
    // int-sized canonical vertex range before isolate compaction.
    if (value < 0 || value >= std::numeric_limits<int>::max()) {
        throw std::runtime_error(
            "Vertex identifier at " + location +
            " must be in [0, INT_MAX - 1]");
    }
    return static_cast<int>(value);
}

class PackedEdgeSet {
public:
    static constexpr std::size_t npos =
        std::numeric_limits<std::size_t>::max();

    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }
    std::size_t slot_count() const { return slots_.size(); }

    void reserve_for_edges(const std::uint64_t expected_edges) {
        if (expected_edges == 0) return;
        if (expected_edges >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error(
                "Cornell projection edge hash exceeds addressable memory");
        }

        std::size_t capacity = slots_.empty() ? 16 : slots_.size();
        while (static_cast<std::size_t>(expected_edges) > max_load(capacity)) {
            if (capacity > slots_.max_size() / 2) {
                throw std::runtime_error(
                    "Cornell projection edge hash exceeds addressable memory");
            }
            capacity *= 2;
        }
        if (capacity > slots_.size()) rehash(capacity);
    }

    bool insert(const std::uint64_t key) {
        if (key == 0) {
            throw std::runtime_error("Internal error: zero is not a valid edge key");
        }
        if (slots_.empty()) slots_.assign(16, 0);

        std::size_t position = find_position(key);
        if (slots_[position] == key) return false;

        // Probe before growing. Repeated pair occurrences at the load boundary
        // therefore do not trigger an unnecessary allocation.
        if (size_ >= max_load(slots_.size())) {
            if (slots_.size() > slots_.max_size() / 2) {
                throw std::runtime_error(
                    "Cornell projection edge hash exceeds addressable memory");
            }
            rehash(slots_.size() * 2);
            position = find_position(key);
        }

        slots_[position] = key;
        ++size_;
        return true;
    }

    std::size_t find_existing_slot(const std::uint64_t key) const {
        if (slots_.empty()) return npos;
        const std::size_t position = find_position(key);
        return slots_[position] == key ? position : npos;
    }

    template <typename Function>
    void for_each(Function function) const {
        for (const std::uint64_t key : slots_) {
            if (key != 0) function(key);
        }
    }

    void release() {
        std::vector<std::uint64_t>().swap(slots_);
        size_ = 0;
    }

private:
    static std::size_t max_load(const std::size_t capacity) {
        // A 3/4 load factor keeps probing short while avoiding multiplication
        // overflow in the capacity test.
        return capacity - capacity / 4;
    }

    std::size_t find_position(const std::uint64_t key) const {
        const std::size_t mask = slots_.size() - 1;
        std::size_t position =
            static_cast<std::size_t>(mix_edge_key(key)) & mask;
        while (slots_[position] != 0 && slots_[position] != key) {
            position = (position + 1) & mask;
        }
        return position;
    }

    void rehash(const std::size_t capacity) {
        std::vector<std::uint64_t> old_slots = std::move(slots_);
        slots_.assign(capacity, 0);
        for (const std::uint64_t key : old_slots) {
            if (key != 0) slots_[find_position(key)] = key;
        }
    }

    // Zero is the empty marker. For a canonical edge u < v, (u << 32) | v is
    // nonzero even when u is vertex zero.
    std::vector<std::uint64_t> slots_;
    std::size_t size_ = 0;
};

inline void build_adjacency_and_release_edges(
    PackedEdgeSet& edges,
    const int maximum_external_id,
    std::vector<std::vector<int>>& adj) {
    const std::size_t temporary_n = maximum_external_id < 0
        ? 0
        : static_cast<std::size_t>(maximum_external_id) + 1;

    // While the hash is resident, retain only the larger endpoint of every
    // edge at its smaller endpoint. This costs one int per edge, rather than a
    // full symmetric adjacency list alongside the hash.
    std::vector<std::size_t> forward_degree(temporary_n, 0);
    edges.for_each([&](const std::uint64_t key) {
        ++forward_degree[static_cast<std::size_t>(edge_key_first(key))];
    });

    std::vector<std::vector<int>> forward(temporary_n);
    for (std::size_t u = 0; u < temporary_n; ++u) {
        forward[u].reserve(forward_degree[u]);
    }
    std::vector<std::size_t>().swap(forward_degree);

    edges.for_each([&](const std::uint64_t key) {
        forward[static_cast<std::size_t>(edge_key_first(key))].push_back(
            edge_key_second(key));
    });
    for (std::vector<int>& neighbors : forward) {
        std::sort(neighbors.begin(), neighbors.end());
    }

    // The duplicate index has served its purpose. Release it before allocating
    // the two adjacency entries per final edge.
    edges.release();

    std::vector<std::size_t> degree(temporary_n, 0);
    for (std::size_t u = 0; u < temporary_n; ++u) {
        degree[u] += forward[u].size();
        for (const int v : forward[u]) ++degree[static_cast<std::size_t>(v)];
    }

    adj.assign(temporary_n, {});
    for (std::size_t u = 0; u < temporary_n; ++u) adj[u].reserve(degree[u]);
    std::vector<std::size_t>().swap(degree);

    // Processing smaller endpoints in increasing order first appends all lower
    // neighbors of a vertex, also in increasing order. Its sorted forward list
    // then appends all higher neighbors. The final symmetric lists are therefore
    // already sorted and need no additional full-graph sort.
    for (std::size_t u_index = 0; u_index < temporary_n; ++u_index) {
        const int u = static_cast<int>(u_index);
        for (const int v : forward[u_index]) {
            adj[u_index].push_back(v);
            adj[static_cast<std::size_t>(v)].push_back(u);
        }
    }
}

class ProjectionProgress {
public:
    ProjectionProgress(std::string graph_name, std::string stage)
        : graph_name_(std::move(graph_name)),
          stage_(std::move(stage)),
          next_message_(std::chrono::steady_clock::now() +
                        std::chrono::seconds(10)) {}

    void maybe_report(const std::int64_t records,
                      const std::int64_t pair_occurrences,
                      const std::size_t distinct_edges) {
        const auto now = std::chrono::steady_clock::now();
        if (now < next_message_) return;

        std::cerr << "cc_con: " << graph_name_ << ": " << stage_
                  << ": records=" << records
                  << ", pair_occurrences=" << pair_occurrences
                  << ", distinct_edges=" << distinct_edges << '\n';
        next_message_ = now + std::chrono::seconds(10);
    }

private:
    std::string graph_name_;
    std::string stage_;
    std::chrono::steady_clock::time_point next_message_;
};

}  // namespace cc_con::cornell_detail

#endif
