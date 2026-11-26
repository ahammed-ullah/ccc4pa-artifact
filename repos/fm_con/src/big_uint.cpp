#include "fm_con/big_uint.hpp"

#include <cstdint>

using namespace std;

namespace fm_con {

BigUInt::BigUInt(uint64_t value) {
    if (value == 0) return;
    word.push_back((uint32_t)value);
    uint32_t high = (uint32_t)(value >> 32U);
    if (high != 0) word.push_back(high);
}

bool BigUInt::is_zero() const {
    return word.empty();
}

void BigUInt::multiply(uint32_t factor) {
    if (factor == 0 || word.empty()) {
        word.clear();
        return;
    }

    uint64_t carry = 0;
    for (uint32_t& digit : word) {
        uint64_t product = (uint64_t)digit * factor + carry;
        digit = (uint32_t)product;
        carry = product >> 32U;
    }
    if (carry != 0) word.push_back((uint32_t)carry);
}

void BigUInt::add_scaled(const BigUInt& value, uint32_t factor) {
    if (factor == 0 || value.word.empty()) return;
    if (word.size() < value.word.size()) word.resize(value.word.size(), 0);

    uint64_t carry = 0;
    for (int i = 0; i < (int)value.word.size(); ++i) {
        uint64_t sum = (uint64_t)value.word[i] * factor + word[i] + carry;
        word[i] = (uint32_t)sum;
        carry = sum >> 32U;
    }

    int i = (int)value.word.size();
    while (carry != 0 && i < (int)word.size()) {
        uint64_t sum = (uint64_t)word[i] + carry;
        word[i] = (uint32_t)sum;
        carry = sum >> 32U;
        ++i;
    }
    if (carry != 0) word.push_back((uint32_t)carry);
}

int compare(const BigUInt& lhs, const BigUInt& rhs) {
    if (lhs.word.size() != rhs.word.size())
        return lhs.word.size() < rhs.word.size() ? -1 : 1;
    for (int i = (int)lhs.word.size() - 1; i >= 0; --i) {
        if (lhs.word[i] != rhs.word[i])
            return lhs.word[i] < rhs.word[i] ? -1 : 1;
    }
    return 0;
}

}  // namespace fm_con
