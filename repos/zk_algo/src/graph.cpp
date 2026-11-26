// Decoding is the inverse of zk_con's fixed Zuckerli variant. The Huffman
// contexts, hybrid integers, reference blocks, and zero runs follow its
// codec.cpp and encode.cpp; the .zk/.zki container is documented there.

#include "zk_algo/graph.hpp"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace std;

namespace zk_algo {
namespace {

constexpr size_t graph_header_bytes = 64;
constexpr size_t index_header_bytes = 24;
constexpr unsigned contexts = 214;
constexpr unsigned first_degree_context = 0;
constexpr unsigned reference_context = 33; // C=1 resets the prior reference
constexpr unsigned block_count_context = 97;
constexpr unsigned block_context = 98;
constexpr unsigned block_even_context = 99;
constexpr unsigned block_odd_context = 100;
constexpr unsigned first_residual_base_context = 101;
constexpr unsigned residual_base_context = 133;
constexpr unsigned rle_context = 213;

[[noreturn]] void invalid(const string& detail) {
    throw runtime_error("invalid Zuckerli graph: " + detail);
}

vector<uint8_t> read_file(const string& path) {
    ifstream in(path, ios::binary | ios::ate);
    if (!in) throw runtime_error("cannot open " + path);
    const auto length = in.tellg();
    if (length < 0 || static_cast<uint64_t>(length) >
                          numeric_limits<size_t>::max()) {
        throw runtime_error("cannot size " + path);
    }
    vector<uint8_t> bytes(static_cast<size_t>(length));
    in.seekg(0);
    if (!bytes.empty() && !in.read(reinterpret_cast<char*>(bytes.data()),
                                   static_cast<streamsize>(bytes.size()))) {
        throw runtime_error("cannot read " + path);
    }
    return bytes;
}

uint64_t little_endian(const vector<uint8_t>& bytes, size_t at, unsigned count) {
    if (at > bytes.size() || count > bytes.size() - at) invalid("short header");
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) {
        value |= uint64_t(bytes[at + i]) << (8 * i);
    }
    return value;
}

class BitReader {
public:
    BitReader(const vector<uint8_t>& bytes, uint64_t begin, uint64_t end)
        : bytes_(bytes), bit_(begin), end_(end) {
        if (begin > end || end > uint64_t(bytes.size()) * 8) {
            invalid("bit range exceeds .zk file");
        }
    }

    uint64_t bit() const { return bit_; }
    uint64_t remaining() const { return end_ - bit_; }

    uint64_t peek(unsigned count) const {
        if (count > 64 || count > remaining()) invalid("truncated bitstream");
        uint64_t value = 0;
        uint64_t at = bit_;
        unsigned used = 0;
        while (used < count) {
            const unsigned shift = static_cast<unsigned>(at & 7);
            const unsigned take = min(8u - shift, count - used);
            const unsigned mask = (1u << take) - 1;
            value |= uint64_t((bytes_[at / 8] >> shift) & mask) << used;
            at += take;
            used += take;
        }
        return value;
    }

    void skip(unsigned count) {
        if (count > remaining()) invalid("truncated bitstream");
        bit_ += count;
    }

    uint64_t read(unsigned count) {
        const uint64_t value = peek(count);
        skip(count);
        return value;
    }

private:
    const vector<uint8_t>& bytes_;
    uint64_t bit_;
    uint64_t end_;
};

uint8_t reverse_byte(uint8_t value) {
    value = uint8_t(((value & 0x55u) << 1) | ((value & 0xaau) >> 1));
    value = uint8_t(((value & 0x33u) << 2) | ((value & 0xccu) >> 2));
    return uint8_t((value << 4) | (value >> 4));
}

// Zuckerli's (k,i,j)=(3,1,0) hybrid integer: 0..7 are direct symbols;
// larger values store an exponent and one high mantissa bit in the symbol.
unsigned token(uint64_t value) {
    if (value < 8) return static_cast<unsigned>(value);
    const unsigned exponent = 63u - static_cast<unsigned>(__builtin_clzll(value));
    return 8 + 2 * (exponent - 3) +
           static_cast<unsigned>((value >> (exponent - 1)) & 1);
}

uint64_t integer(BitReader& bits,
                 const vector<array<uint16_t, 256>>& tables,
                 unsigned context) {
    const auto look = static_cast<unsigned>(min<uint64_t>(8, bits.remaining()));
    if (look == 0) invalid("truncated Huffman symbol");
    const uint16_t entry = tables[context][bits.peek(look)];
    const unsigned length = entry >> 8;
    if (length == 0 || length > bits.remaining()) {
        invalid("absent or truncated Huffman code");
    }
    bits.skip(length);
    const unsigned symbol = entry & 255;
    if (symbol < 8) return symbol;
    if (symbol > 129) invalid("hybrid integer symbol exceeds 64 bits");
    const unsigned exponent = 3 + (symbol - 8) / 2;
    const unsigned extra_bits = exponent - 1;
    const uint64_t high = (uint64_t{1} << exponent) |
                          (uint64_t(symbol & 1) << extra_bits);
    return high | bits.read(extra_bits);
}

unsigned first_residual_context(uint64_t count) {
    return first_residual_base_context + min(token(count), 31u);
}

unsigned residual_context(uint64_t last_delta) {
    return residual_base_context + min(token(last_delta), 79u);
}

struct RowInfo {
    int vertex;
    int degree;
    int reference;
    uint64_t data_bit;
    uint64_t end_bit;
};

} // namespace

Graph Graph::load(const string& zk_path) {
    if (zk_path.size() < 3 || zk_path.substr(zk_path.size() - 3) != ".zk") {
        throw invalid_argument("Zuckerli graph path must end in .zk");
    }
    Graph result;
    result.path_ = zk_path;
    result.graph_ = read_file(zk_path);
    result.index_ = read_file(zk_path.substr(0, zk_path.size() - 3) + ".zki");
    if (result.graph_.size() < graph_header_bytes ||
        result.index_.size() < index_header_bytes ||
        memcmp(result.graph_.data(), "ZKCCF001", 8) != 0 ||
        memcmp(result.index_.data(), "ZKIDX001", 8) != 0) {
        invalid("file header or magic");
    }

    const uint64_t n = little_endian(result.graph_, 8, 4);
    const uint64_t m = little_endian(result.graph_, 16, 8);
    if (n > INT_MAX || m > (n == 0 ? 0 : n * (n - 1) / 2)) {
        invalid("vertex or edge count");
    }
    result.n_ = static_cast<int>(n);
    result.arcs_ = 2 * m;
    if (little_endian(result.graph_, 12, 4) != 128 ||
        result.graph_[24] != 1 || result.graph_[25] != 3 ||
        result.graph_[26] != 3 || result.graph_[27] != 1 ||
        result.graph_[28] != 0 || result.graph_[29] != 2 ||
        result.graph_[30] != 1 || result.graph_[31] != 1) {
        invalid("unsupported Zuckerli settings");
    }
    if (little_endian(result.index_, 8, 4) != n ||
        result.index_[13] != 0 || result.index_[14] != 0 ||
        result.index_[15] != 0) {
        invalid("index header does not match graph");
    }
    result.offset_width_ = result.index_[12];
    if (result.offset_width_ == 0 || result.offset_width_ > 64) {
        invalid("index offset width");
    }
    const uint64_t end_bit = little_endian(result.index_, 16, 8);
    const uint64_t offset_bits = (n + 1) * result.offset_width_;
    if (result.index_.size() != index_header_bytes + (offset_bits + 7) / 8 ||
        end_bit > uint64_t(result.graph_.size()) * 8 ||
        result.graph_.size() != (end_bit + 7) / 8) {
        invalid("index or graph file length");
    }

    // The table descriptions use 8 bits for the highest symbol, then one
    // presence bit and a three-bit length for each present symbol. Rebuild
    // the same canonical codes as zk_con, reversed for low-bit-first input.
    BitReader bits(result.graph_, 8 * graph_header_bytes,
                   uint64_t(result.graph_.size()) * 8);
    result.huffman_.resize(contexts);
    for (unsigned context = 0; context < contexts; ++context) {
        const unsigned highest = static_cast<unsigned>(bits.read(8));
        vector<pair<unsigned, unsigned>> present;
        for (unsigned symbol = 0; symbol <= highest; ++symbol) {
            if (bits.read(1) != 0) {
                const unsigned length = static_cast<unsigned>(bits.read(3)) + 1;
                present.emplace_back(length, symbol);
            }
        }
        if ((!present.empty() && present.back().second != highest) ||
            (present.empty() && highest != 0)) {
            invalid("Huffman highest symbol");
        }
        if (present.empty()) continue;
        unsigned kraft = 0;
        for (const auto& item : present) kraft += 1u << (8 - item.first);
        if ((present.size() == 1 && present[0].first != 1) ||
            (present.size() > 1 && kraft != 256)) {
            invalid("Huffman code lengths");
        }
        sort(present.begin(), present.end());
        unsigned canonical = 0;
        unsigned last_length = present[0].first;
        for (const auto& item : present) {
            const unsigned length = item.first;
            canonical <<= length - last_length;
            if (canonical >= (1u << length)) invalid("Huffman code overflow");
            const unsigned code = reverse_byte(static_cast<uint8_t>(canonical)) >>
                                  (8 - length);
            const uint16_t entry = static_cast<uint16_t>((length << 8) | item.second);
            for (unsigned suffix = 0; suffix < (1u << (8 - length)); ++suffix) {
                const unsigned slot = code | (suffix << length);
                if (result.huffman_[context][slot] != 0) {
                    invalid("overlapping Huffman codes");
                }
                result.huffman_[context][slot] = entry;
            }
            ++canonical;
            last_length = length;
        }
    }
    if (result.offset(0) != bits.bit() || result.offset(result.n_) != end_bit) {
        invalid("row offsets do not meet Huffman tables or final row");
    }
    uint64_t prior = result.offset(0);
    for (int u = 1; u <= result.n_; ++u) {
        const uint64_t next = result.offset(u);
        if (next <= prior || next > end_bit) invalid("row offsets are not increasing");
        prior = next;
    }
    if (end_bit % 8 != 0 &&
        (result.graph_.back() >> (end_bit % 8)) != 0) {
        invalid("nonzero final padding bits");
    }
    return result;
}

uint64_t Graph::offset(int vertex) const {
    if (vertex < 0 || vertex > n_) throw out_of_range("Zuckerli vertex");
    uint64_t bit = uint64_t(vertex) * offset_width_;
    uint64_t value = 0;
    unsigned used = 0;
    while (used < offset_width_) {
        const unsigned shift = static_cast<unsigned>(bit & 7);
        const unsigned take = min(8u - shift, unsigned(offset_width_ - used));
        const unsigned mask = (1u << take) - 1;
        const size_t at = index_header_bytes + static_cast<size_t>(bit / 8);
        value |= uint64_t((index_[at] >> shift) & mask) << used;
        bit += take;
        used += take;
    }
    return value;
}

void Graph::decode_row(int vertex, vector<int>& row) const {
    if (vertex < 0 || vertex >= n_) throw out_of_range("Zuckerli vertex");
    row.clear();

    // Every reference points backward. Read only the small row prefix while
    // walking to an unreferenced row; then rebuild the requested row upward.
    // This avoids recursion even when the encoder chose an unlimited chain.
    vector<RowInfo> chain;
    int current = vertex;
    while (true) {
        const uint64_t start = offset(current);
        const uint64_t end = offset(current + 1);
        BitReader bits(graph_, start, end);
        const uint64_t degree = integer(bits, huffman_, first_degree_context);
        if (degree >= static_cast<uint64_t>(n_) && n_ != 0) {
            invalid("row degree exceeds simple graph limit");
        }
        int reference = 0;
        if (degree != 0 && current != 0) {
            const uint64_t distance = integer(bits, huffman_, reference_context);
            if (distance > 128 || distance > static_cast<uint64_t>(current)) {
                invalid("reference leaves its window");
            }
            reference = static_cast<int>(distance);
        }
        chain.push_back({current, static_cast<int>(degree), reference,
                         bits.bit(), end});
        if (reference == 0) break;
        current -= reference;
    }

    vector<int> copied;
    vector<int> residuals;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const RowInfo& info = *it;
        BitReader bits(graph_, info.data_bit, info.end_bit);
        copied.clear();
        residuals.clear();
        if (info.reference != 0) {
            const uint64_t count = integer(bits, huffman_, block_count_context);
            if (count > row.size() + 1) invalid("reference block count");
            size_t pos = 0;
            bool copy = true;
            for (uint64_t j = 0; j < count; ++j) {
                const unsigned context = j == 0 ? block_context :
                                         (j % 2 == 0 ? block_even_context :
                                                       block_odd_context);
                const uint64_t raw = integer(bits, huffman_, context);
                const size_t available = row.size() - pos;
                if (raw > available || (j != 0 && raw == available)) {
                    invalid("reference block length");
                }
                const size_t length = static_cast<size_t>(raw) + (j != 0);
                if (copy) {
                    copied.insert(copied.end(), row.begin() + pos,
                                  row.begin() + pos + length);
                }
                pos += length;
                copy = !copy;
            }
            if (copy) copied.insert(copied.end(), row.begin() + pos, row.end());
        }
        if (copied.size() > static_cast<size_t>(info.degree)) {
            invalid("copied neighbors exceed degree");
        }
        const size_t residual_count = static_cast<size_t>(info.degree) - copied.size();
        residuals.reserve(residual_count);
        size_t copied_pos = 0;
        uint64_t last_delta = 0;
        uint64_t pending_zeros = 0;
        unsigned zero_run = 0;
        bool after_run = false;
        for (size_t j = 0; j < residual_count; ++j) {
            uint64_t delta;
            if (pending_zeros != 0) {
                delta = 0;
                --pending_zeros;
            } else {
                const unsigned context = j == 0 ?
                    first_residual_context(residual_count) :
                    residual_context(last_delta);
                delta = integer(bits, huffman_, context);
                if (after_run && delta == 0) invalid("split zero run");
            }
            if (delta == 0 && !after_run) {
                if (++zero_run == 3) {
                    pending_zeros = integer(bits, huffman_, rle_context);
                    if (pending_zeros > residual_count - j - 1) {
                        invalid("zero run exceeds residual count");
                    }
                    zero_run = 0;
                    after_run = true;
                }
            } else if (delta != 0) {
                zero_run = 0;
                after_run = false;
            }

            uint64_t value;
            if (j == 0) {
                if (delta > 2 * static_cast<uint64_t>(n_)) {
                    invalid("first residual distance");
                }
                const int64_t difference = (delta & 1) ?
                    -static_cast<int64_t>(delta / 2) - 1 :
                    static_cast<int64_t>(delta / 2);
                const int64_t first = static_cast<int64_t>(info.vertex) + difference;
                if (first < 0 || first >= n_) invalid("first residual vertex");
                value = static_cast<uint64_t>(first);
            } else {
                const uint64_t next = static_cast<uint64_t>(residuals.back()) + 1;
                while (copied_pos < copied.size() &&
                       static_cast<uint64_t>(copied[copied_pos]) < next) {
                    ++copied_pos;
                }
                if (next >= static_cast<uint64_t>(n_) ||
                    delta >= static_cast<uint64_t>(n_) - next) {
                    invalid("residual gap exceeds vertex range");
                }
                value = next + delta;
                while (copied_pos < copied.size() &&
                       static_cast<uint64_t>(copied[copied_pos]) <= value) {
                    if (++value >= static_cast<uint64_t>(n_)) {
                        invalid("residual crosses end of vertex range");
                    }
                    ++copied_pos;
                }
            }
            residuals.push_back(static_cast<int>(value));
            last_delta = delta;
        }
        if (bits.bit() != info.end_bit) invalid("row does not end at indexed bit");

        // The referenced list is no longer needed once its copied neighbors
        // have been collected. Reuse that same allocation for the new row.
        row.resize(static_cast<size_t>(info.degree));
        merge(copied.begin(), copied.end(), residuals.begin(), residuals.end(),
              row.begin());
    }
}

} // namespace zk_algo
