// The hybrid integer mapping, context IDs, Huffman table format, and
// length-limited package-merge construction are adapted from Zuckerli:
// Copyright 2019 Google LLC, licensed under Apache License 2.0.
// See https://github.com/google/zuckerli/tree/master/src.

#include "codec.hpp"

#include "format.hpp"

#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace zk {
namespace {

static_assert(hybrid_k == 3 && hybrid_i == 1 && hybrid_j == 0,
              "codec.cpp implements the fixed (3,1,0) hybrid code");
static_assert(zero_run_min == 3 && window == 128,
              "codec.cpp implements this experiment's fixed graph settings");
static_assert(rle_context + 1 == num_contexts, "context count mismatch");

struct HybridInteger {
    std::uint8_t symbol;
    unsigned extra_bits;
    std::uint64_t extra;
};

unsigned floor_log2_nonzero(std::uint64_t value) {
#if defined(__GNUC__) || defined(__clang__)
    return 63u - static_cast<unsigned>(__builtin_clzll(value));
#else
    unsigned result = 0;
    while (value >>= 1) ++result;
    return result;
#endif
}

HybridInteger split_integer(std::uint64_t value) {
    if (value < 8) return {static_cast<std::uint8_t>(value), 0, 0};
    const unsigned exponent = floor_log2_nonzero(value);
    const std::uint64_t mantissa = value - (std::uint64_t{1} << exponent);
    const unsigned symbol = 8 + 2 * (exponent - 3) +
                            static_cast<unsigned>(mantissa >> (exponent - 1));
    const unsigned extra_bits = exponent - 1;
    const std::uint64_t extra = value & ((std::uint64_t{1} << extra_bits) - 1);
    return {static_cast<std::uint8_t>(symbol), extra_bits, extra};
}

std::uint8_t reverse_byte(std::uint8_t value) {
    value = static_cast<std::uint8_t>(((value & 0x55u) << 1) |
                                      ((value & 0xaau) >> 1));
    value = static_cast<std::uint8_t>(((value & 0x33u) << 2) |
                                      ((value & 0xccu) >> 2));
    return static_cast<std::uint8_t>((value << 4) | (value >> 4));
}

using Package = std::pair<std::uint64_t, std::vector<std::uint8_t>>;

// This is Zuckerli's quadratic package-merge construction. Sorting pairs
// retains its tie-breaking order: weight, then the symbol sequence. With a
// maximum length of eight, it finds minimum-cost lengths under that bound.
std::array<std::uint8_t, num_symbols> code_lengths(const Histogram& histogram) {
    std::array<std::uint8_t, num_symbols> lengths{};
    std::size_t present = 0;
    std::uint8_t only_symbol = 0;
    for (std::size_t symbol = 0; symbol < num_symbols; ++symbol) {
        if (histogram[symbol] == 0) continue;
        ++present;
        only_symbol = static_cast<std::uint8_t>(symbol);
    }
    if (present == 0) return lengths;
    if (present == 1) {
        lengths[only_symbol] = 1;
        return lengths;
    }

    std::array<std::vector<Package>, max_huffman_bits> bags;
    for (auto& bag : bags) {
        bag.reserve(2 * num_symbols);
        for (std::size_t symbol = 0; symbol < num_symbols; ++symbol) {
            if (histogram[symbol] != 0) {
                bag.push_back({histogram[symbol],
                               {static_cast<std::uint8_t>(symbol)}});
            }
        }
    }
    for (std::size_t level = 0; level + 1 < max_huffman_bits; ++level) {
        auto& bag = bags[level];
        std::sort(bag.begin(), bag.end());
        for (std::size_t i = 0; i + 1 < bag.size(); i += 2) {
            if (bag[i].first > std::numeric_limits<std::uint64_t>::max() -
                                   bag[i + 1].first) {
                throw std::overflow_error("Huffman histogram weight overflows");
            }
            std::vector<std::uint8_t> symbols = std::move(bag[i].second);
            symbols.insert(symbols.end(), bag[i + 1].second.begin(),
                           bag[i + 1].second.end());
            bags[level + 1].push_back(
                {bag[i].first + bag[i + 1].first, std::move(symbols)});
        }
    }
    auto& last = bags[max_huffman_bits - 1];
    std::sort(last.begin(), last.end());
    const std::size_t required = 2 * present - 2;
    if (last.size() < required) {
        throw std::runtime_error("Huffman package-merge has too few packages");
    }
    for (std::size_t i = 0; i < required; ++i) {
        for (std::uint8_t symbol : last[i].second) {
            if (++lengths[symbol] > max_huffman_bits) {
                throw std::runtime_error("Huffman code exceeds eight bits");
            }
        }
    }
    return lengths;
}

}  // namespace

std::uint8_t token(std::uint64_t value) {
    return split_integer(value).symbol;
}

std::uint32_t degree_context(std::uint64_t last_degree_delta) {
    return degree_base_context + std::min<std::uint32_t>(token(last_degree_delta), 31);
}

std::uint32_t reference_context(std::uint64_t last_reference) {
    // With C=1 the previous reference resets at every row, so this is usually
    // the base context. Keep Zuckerli's 64-context range for format alignment.
    return reference_base_context +
           static_cast<std::uint32_t>(std::min<std::uint64_t>(last_reference, 63));
}

std::uint32_t first_residual_context(std::uint64_t edges_left) {
    return first_residual_base_context +
           std::min<std::uint32_t>(token(edges_left), 31);
}

std::uint32_t residual_context(std::uint64_t last_residual_delta) {
    return residual_base_context +
           std::min<std::uint32_t>(token(last_residual_delta), 79);
}

BitWriter::BitWriter(std::ostream& output, std::uint64_t initial_bits)
    : output_(output), bits_written_(initial_bits) {
    if (initial_bits % 8 != 0) {
        throw std::invalid_argument("BitWriter initial position must be byte-aligned");
    }
}

void BitWriter::write(unsigned nbits, std::uint64_t value) {
    if (finished_) throw std::logic_error("BitWriter already finished");
    if (nbits > 64 || (nbits < 64 && (value >> nbits) != 0)) {
        throw std::invalid_argument("BitWriter value does not fit bit count");
    }
    if (nbits > std::numeric_limits<std::uint64_t>::max() - bits_written_) {
        throw std::overflow_error("BitWriter position overflows");
    }
    unsigned consumed = 0;
    while (consumed < nbits) {
        const unsigned take = std::min(8u - used_bits_, nbits - consumed);
        const std::uint8_t piece = static_cast<std::uint8_t>(
            (value >> consumed) & ((1u << take) - 1));
        pending_ = static_cast<std::uint8_t>(pending_ | (piece << used_bits_));
        used_bits_ += take;
        consumed += take;
        if (used_bits_ == 8) {
            output_.put(static_cast<char>(pending_));
            if (!output_) throw std::runtime_error("BitWriter output failed");
            pending_ = 0;
            used_bits_ = 0;
        }
    }
    bits_written_ += nbits;
}

void BitWriter::finish() {
    if (finished_) return;
    if (used_bits_ != 0) {
        output_.put(static_cast<char>(pending_));
        if (!output_) throw std::runtime_error("BitWriter output failed");
        pending_ = 0;
        used_bits_ = 0;
    }
    finished_ = true;
}

HuffmanTables HuffmanTables::build(const std::vector<Histogram>& histograms) {
    if (histograms.empty() || histograms.size() > num_contexts) {
        throw std::invalid_argument("Huffman context count is invalid");
    }
    HuffmanTables tables;
    tables.codes_.resize(histograms.size());
    for (std::size_t context = 0; context < histograms.size(); ++context) {
        const auto lengths = code_lengths(histograms[context]);
        for (std::size_t symbol = 0; symbol < num_symbols; ++symbol) {
            tables.codes_[context][symbol].length = lengths[symbol];
        }
        tables.make_codes(context);
    }
    return tables;
}

void HuffmanTables::write(BitWriter& output) const {
    for (const auto& codes : codes_) {
        std::uint32_t highest = 0;
        for (std::uint32_t symbol = 0; symbol < num_symbols; ++symbol) {
            if (codes[symbol].length != 0) highest = symbol;
        }
        output.write(8, highest);
        for (std::uint32_t symbol = 0; symbol <= highest; ++symbol) {
            const unsigned length = codes[symbol].length;
            output.write(1, length != 0);
            if (length != 0) output.write(3, length - 1);
        }
    }
}

void HuffmanTables::encode(std::uint32_t context, std::uint64_t value,
                           BitWriter& output) const {
    if (context >= codes_.size()) {
        throw std::out_of_range("Huffman context is invalid");
    }
    const HybridInteger integer = split_integer(value);
    const Code code = codes_[context][integer.symbol];
    if (code.length == 0) {
        throw std::runtime_error("Huffman token was absent from its histogram");
    }
    output.write(code.length, code.bits);
    output.write(integer.extra_bits, integer.extra);
}

void HuffmanTables::make_codes(std::size_t context) {
    auto& codes = codes_[context];
    std::vector<std::pair<unsigned, unsigned>> symbols;
    symbols.reserve(num_symbols);
    std::uint32_t kraft = 0;
    for (std::size_t symbol = 0; symbol < num_symbols; ++symbol) {
        const unsigned length = codes[symbol].length;
        if (length == 0) continue;
        if (length > max_huffman_bits) {
            throw std::runtime_error("Huffman code exceeds eight bits");
        }
        symbols.push_back({length, static_cast<unsigned>(symbol)});
        kraft += 1u << (max_huffman_bits - length);
    }
    if (symbols.empty()) return; // Zuckerli writes a 9-bit empty table.
    if (symbols.size() == 1) {
        if (symbols[0].first != 1) {
            throw std::runtime_error("single-symbol Huffman code must have length one");
        }
        codes[symbols[0].second].bits = 0;
        return;
    }
    if (kraft != (1u << max_huffman_bits)) {
        throw std::runtime_error("Huffman lengths do not fill an eight-bit tree");
    }

    std::sort(symbols.begin(), symbols.end());
    unsigned canonical = 0;
    unsigned previous_length = symbols[0].first;
    for (const auto& item : symbols) {
        const unsigned length = item.first;
        canonical <<= length - previous_length;
        if (canonical >= (1u << length)) {
            throw std::runtime_error("Huffman canonical code overflows its length");
        }
        const std::uint8_t bits = static_cast<std::uint8_t>(
            reverse_byte(static_cast<std::uint8_t>(canonical)) >>
            (max_huffman_bits - length));
        codes[item.second].bits = bits;
        ++canonical;
        previous_length = length;
    }
}

}  // namespace zk
