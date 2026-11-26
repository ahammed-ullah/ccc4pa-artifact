// Context layout and hybrid/Huffman coding derive from Zuckerli.
// Copyright 2019 Google LLC, licensed under Apache License 2.0.
#ifndef ZK_CON_CODEC_HPP
#define ZK_CON_CODEC_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace zk {

// Zuckerli's integer contexts. The fixed (k, i, j) = (3, 1, 0) hybrid code
// still uses 256 possible Huffman symbols, though its largest valid token is
// 129 for a uint64 value. Context IDs match the upstream context_model.h.
constexpr std::uint32_t num_symbols = 256;
constexpr std::uint32_t num_contexts = 214;
constexpr std::uint32_t max_huffman_bits = 8;

constexpr std::uint32_t first_degree_context = 0;
constexpr std::uint32_t degree_base_context = 1;
constexpr std::uint32_t reference_base_context = 33;
constexpr std::uint32_t block_count_context = 97;
constexpr std::uint32_t block_context = 98;
constexpr std::uint32_t block_even_context = 99;
constexpr std::uint32_t block_odd_context = 100;
constexpr std::uint32_t first_residual_base_context = 101;
constexpr std::uint32_t residual_base_context = 133;
constexpr std::uint32_t rle_context = 213;

std::uint8_t token(std::uint64_t value);
std::uint32_t degree_context(std::uint64_t last_degree_delta);
std::uint32_t reference_context(std::uint64_t last_reference);
std::uint32_t first_residual_context(std::uint64_t edges_left);
std::uint32_t residual_context(std::uint64_t last_residual_delta);

// Bits are written least-significant first, as in Zuckerli. A byte-aligned
// header may already be in the stream; initial_bits makes positions absolute
// within the complete graph file. finish() writes the final partial byte with
// zero padding, without counting the padding as data bits.
class BitWriter {
public:
    explicit BitWriter(std::ostream& output, std::uint64_t initial_bits = 0);
    void write(unsigned nbits, std::uint64_t value);
    std::uint64_t bits_written() const { return bits_written_; }
    void finish();

private:
    std::ostream& output_;
    std::uint64_t bits_written_;
    std::uint8_t pending_ = 0;
    unsigned used_bits_ = 0;
    bool finished_ = false;
};

using Histogram = std::array<std::uint64_t, num_symbols>;

// A separate length-limited canonical Huffman code is built for each context.
// The table bitstream is compatible with upstream Zuckerli's Huffman mode:
// 8 bits for the highest present symbol, then one presence bit and, when
// present, three bits for code_length - 1 for each symbol through that one.
class HuffmanTables {
public:
    static HuffmanTables build(const std::vector<Histogram>& histograms);

    void write(BitWriter& output) const;
    void encode(std::uint32_t context, std::uint64_t value,
                BitWriter& output) const;
    std::size_t contexts() const { return codes_.size(); }

private:
    struct Code {
        std::uint8_t length = 0;
        std::uint8_t bits = 0;
    };
    std::vector<std::array<Code, num_symbols>> codes_;

    void make_codes(std::size_t context);
};

}  // namespace zk

#endif
