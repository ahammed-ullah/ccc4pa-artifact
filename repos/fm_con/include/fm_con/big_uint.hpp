#ifndef FM_CON_BIG_UINT_HPP
#define FM_CON_BIG_UINT_HPP

#include <cstdint>
#include <vector>

namespace fm_con {

// FM's conditional scores can be much larger than 64 bits, but the algorithm
// needs only nonnegative integers, multiplication by a graph-sized count,
// scaled addition, and comparison. This small base-2^32 representation keeps
// those comparisons exact without an external multiprecision dependency.
struct BigUInt {
    std::vector<std::uint32_t> word;

    BigUInt() = default;
    explicit BigUInt(std::uint64_t value);

    bool is_zero() const;
    void multiply(std::uint32_t factor);
    void add_scaled(const BigUInt& value, std::uint32_t factor);
};

int compare(const BigUInt& lhs, const BigUInt& rhs);

}  // namespace fm_con

#endif

