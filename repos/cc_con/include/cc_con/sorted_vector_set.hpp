#ifndef CC_CON_SORTED_VECTOR_SET_HPP
#define CC_CON_SORTED_VECTOR_SET_HPP

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace cc_con {

inline constexpr std::size_t kSortedIntersectionSkewFactor = 8;

// These functions treat vector<int> as a set whose values are strictly
// increasing. They contain only representation-level mechanics: no function
// knows what a vertex, label, clique, or admissibility set means. Keeping the
// concrete vector type in every signature makes that representation visible at
// each call site.

// Insert value at its sorted position. Return true exactly when the set changed.
// A single lower_bound supplies both the membership test and insertion point.
inline bool insert_sorted_unique(std::vector<int>& values, const int value) {
    const auto position = std::lower_bound(values.begin(), values.end(), value);
    if (position != values.end() && *position == value) return false;
    values.insert(position, value);
    return true;
}

// Remove value when present. The return value allows an algorithm that tracks a
// set's total size to update its counter only when an erasure actually occurred.
inline bool erase_sorted_if_found(std::vector<int>& values, const int value) {
    const auto position = std::lower_bound(values.begin(), values.end(), value);
    if (position == values.end() || *position != value) return false;
    values.erase(position);
    return true;
}

// Remove every member of removed from values. Both inputs are sorted and
// duplicate-free. Retained values are written to the front of the same vector,
// which avoids one vector::erase call for every removed value.
inline void erase_sorted_values(std::vector<int>& values,
                                const std::vector<int>& removed) {
    if (values.empty() || removed.empty()) return;

    std::size_t read = 0;
    std::size_t remove_position = 0;
    std::size_t write = 0;
    while (read < values.size()) {
        const int value = values[read++];
        while (remove_position < removed.size() &&
               removed[remove_position] < value) {
            ++remove_position;
        }
        if (remove_position == removed.size() ||
            removed[remove_position] != value) {
            values[write++] = value;
        }
    }
    values.resize(write);
}

// Test whether two sorted sets intersect without constructing the intersection.
// Comparable vectors use a forward merge that stops at the first common value.
// For strongly skewed vectors, advancing lower_bounds over only the smaller set
// avoids scanning a long membership list. The factor matches the full
// intersection routine below.
inline bool have_common_value(const std::vector<int>& lhs,
                              const std::vector<int>& rhs) {
    if (lhs.empty() || rhs.empty()) return false;

    const std::vector<int>* small = &lhs;
    const std::vector<int>* large = &rhs;
    if (small->size() > large->size()) std::swap(small, large);

    if (large->size() <=
        small->size() * kSortedIntersectionSkewFactor) {
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < small->size() && j < large->size()) {
            if ((*small)[i] < (*large)[j]) {
                ++i;
            } else if ((*large)[j] < (*small)[i]) {
                ++j;
            } else {
                return true;
            }
        }
        return false;
    }

    auto position = large->begin();
    for (const int value : *small) {
        position = std::lower_bound(position, large->end(), value);
        if (position == large->end()) return false;
        if (*position == value) return true;
    }
    return false;
}

// Set out to lhs intersect rhs. All three vectors use the sorted, unique
// representation. out may retain capacity from an earlier call, but it must not
// alias lhs or rhs because it is cleared before either input is read.
inline void intersect_sorted_hybrid(const std::vector<int>& lhs,
                                    const std::vector<int>& rhs,
                                    std::vector<int>& out) {
    out.clear();
    if (lhs.empty() || rhs.empty()) return;

    const std::vector<int>* small = &lhs;
    const std::vector<int>* large = &rhs;
    if (small->size() > large->size()) std::swap(small, large);

    // The fixed skew factor selects the same strategy as have_common_value.
    // Comparable vectors use a merge scan. For a strongly skewed pair,
    // searching only the smaller set avoids traversing a much larger
    // neighborhood or clique.
    if (large->size() <=
        small->size() * kSortedIntersectionSkewFactor) {
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < small->size() && j < large->size()) {
            if ((*small)[i] < (*large)[j]) {
                ++i;
            } else if ((*large)[j] < (*small)[i]) {
                ++j;
            } else {
                out.push_back((*small)[i]);
                ++i;
                ++j;
            }
        }
        return;
    }

    // Values in small increase, so each lower_bound continues from the position
    // reached by the preceding search rather than restarting at large.begin().
    auto position = large->begin();
    for (const int value : *small) {
        position = std::lower_bound(position, large->end(), value);
        if (position == large->end()) break;
        if (*position == value) {
            out.push_back(value);
            ++position;
        }
    }
}

}  // namespace cc_con

#endif
