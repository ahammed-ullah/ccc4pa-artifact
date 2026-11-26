#include "wg_algo/graph.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace wg_algo {
namespace {

using byte = std::uint8_t;

std::vector<byte> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("cannot open " + path);
    const auto size = in.tellg();
    if (size < 0) throw std::runtime_error("cannot size " + path);
    std::vector<byte> data(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!data.empty() && !in.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("cannot read " + path);
    }
    return data;
}

std::uint64_t le64(const std::vector<byte>& data, std::size_t pos) {
    if (pos > data.size() || data.size() - pos < 8) {
        throw std::runtime_error("truncated WebGraph .ef");
    }
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | data[pos + i];
    return value;
}

std::uint32_t le32(const std::vector<byte>& data, std::size_t pos) {
    if (pos > data.size() || data.size() - pos < 4) {
        throw std::runtime_error("truncated WebGraph .ef");
    }
    std::uint32_t value = 0;
    for (int i = 3; i >= 0; --i) value = (value << 8) | data[pos + i];
    return value;
}

std::uint64_t decimal(const std::map<std::string, std::string>& props,
                      const std::string& key) {
    const auto it = props.find(key);
    if (it == props.end()) throw std::runtime_error("missing WebGraph property: " + key);
    std::size_t end = 0;
    const auto value = std::stoull(it->second, &end);
    if (end != it->second.size()) throw std::runtime_error("invalid WebGraph property: " + key);
    return value;
}

std::map<std::string, std::string> read_properties(const std::vector<byte>& bytes) {
    std::map<std::string, std::string> props;
    const std::string text(bytes.begin(), bytes.end());
    std::size_t begin = 0;
    while (begin < text.size()) {
        auto end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        auto line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != '#') {
            const auto eq = line.find('=');
            if (eq != std::string::npos) props[line.substr(0, eq)] = line.substr(eq + 1);
        }
        begin = end + 1;
    }
    return props;
}

// epserde 0.12.6 writes each usize as a native-endian, aligned 64-bit word.
// wg_con builds .ef on little-endian x86_64. These methods only keep spans
// into the original .ef byte buffer; no array of offsets is expanded.
class EfReader {
public:
    explicit EfReader(const std::vector<byte>& data) : data_(data) {}
    std::uint64_t word() {
        const auto value = le64(data_, pos_);
        pos_ += 8;
        return value;
    }
    std::uint16_t half() {
        if (pos_ > data_.size() || data_.size() - pos_ < 2) {
            throw std::runtime_error("truncated WebGraph .ef header");
        }
        const auto value = static_cast<std::uint16_t>(data_[pos_]) |
                           (static_cast<std::uint16_t>(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return value;
    }
    byte one() {
        if (pos_ >= data_.size()) throw std::runtime_error("truncated WebGraph .ef header");
        return data_[pos_++];
    }
    void skip(std::uint64_t bytes) {
        if (bytes > data_.size() - pos_) throw std::runtime_error("truncated WebGraph .ef");
        pos_ += static_cast<std::size_t>(bytes);
    }
    void align() { pos_ = (pos_ + 7) & ~std::size_t(7); }
    std::size_t pos() const { return pos_; }
    std::size_t words() {
        const auto count = word();
        align();
        if (count > (data_.size() - pos_) / 8) throw std::runtime_error("truncated WebGraph .ef words");
        const auto begin = pos_;
        pos_ += static_cast<std::size_t>(count) * 8;
        return begin;
    }
private:
    const std::vector<byte>& data_;
    std::size_t pos_ = 0;
};

std::uint64_t bits_at(const std::vector<byte>& graph, std::uint64_t& pos, unsigned count) {
    if (count > 63 || pos > graph.size() * 8ULL ||
        count > graph.size() * 8ULL - pos) {
        throw std::runtime_error("truncated WebGraph code");
    }
    std::uint64_t value = 0;
    while (count) {
        const unsigned used = static_cast<unsigned>(pos & 7);
        const unsigned take = std::min(count, 8U - used);
        const unsigned shift = 8U - used - take;
        const unsigned part = (graph[static_cast<std::size_t>(pos / 8)] >> shift) &
                              ((1U << take) - 1);
        value = (value << take) | part;
        pos += take;
        count -= take;
    }
    return value;
}

std::uint64_t unary(const std::vector<byte>& graph, std::uint64_t& pos) {
    std::uint64_t zeros = 0;
    while (pos / 8 < graph.size()) {
        const unsigned used = static_cast<unsigned>(pos & 7);
        const auto remaining = static_cast<byte>(
            graph[static_cast<std::size_t>(pos / 8)] << used);
        if (remaining) {
            const unsigned leading = static_cast<unsigned>(__builtin_clz(remaining)) - 24;
            zeros += leading;
            if (zeros >= 63) throw std::runtime_error("WebGraph code exceeds 64 bits");
            pos += leading + 1;
            return zeros;
        }
        zeros += 8 - used;
        if (zeros >= 63) throw std::runtime_error("WebGraph code exceeds 64 bits");
        pos += 8 - used;
    }
    throw std::runtime_error("truncated WebGraph .graph");
}

std::int64_t signed_value(std::uint64_t code) {
    const auto magnitude = static_cast<std::int64_t>(code >> 1);
    return (code & 1) ? -magnitude - 1 : magnitude;
}

int vertex_value(std::int64_t value, int n) {
    if (value < 0 || value >= n) throw std::runtime_error("WebGraph neighbor outside vertex range");
    return static_cast<int>(value);
}

} // namespace

Graph Graph::load(const std::string& graph_path, Decoder decoder) {
    Graph result;
    result.path_ = graph_path;
    result.decoder_ = decoder;
    if (result.path_.size() < 6 || result.path_.substr(result.path_.size() - 6) != ".graph") {
        throw std::runtime_error("expected a .graph path");
    }
    const auto base = result.path_.substr(0, result.path_.size() - 6);
    result.graph_ = read_file(result.path_);
    const auto properties_data = read_file(base + ".properties");
    result.properties_bytes_ = properties_data.size();
    result.ef_ = read_file(base + ".ef");

    const auto props = read_properties(properties_data);
    const auto graphclass = props.find("graphclass");
    if (graphclass == props.end() ||
        graphclass->second != "it.unimi.dsi.webgraph.BVGraph") {
        throw std::runtime_error("unsupported WebGraph graphclass");
    }
    const auto version = decimal(props, "version");
    if (version > 1) throw std::runtime_error("unsupported WebGraph format version");
    const auto endianness = props.find("endianness");
    if (endianness == props.end() || endianness->second != "big") {
        throw std::runtime_error("wg_algo supports only big-endian WebGraph bitstreams");
    }
    const auto n = decimal(props, "nodes");
    if (n > static_cast<std::uint64_t>(INT_MAX)) {
        throw std::runtime_error("WebGraph vertex count exceeds 32-bit IDs");
    }
    result.n_ = static_cast<int>(n);
    result.arcs_ = decimal(props, "arcs");
    if (result.arcs_ & 1) throw std::runtime_error("expected a symmetric graph with even arc count");
    const auto zeta = props.find("zetak");
    if (zeta != props.end() && decimal(props, "zetak") != 3) {
        throw std::runtime_error("wg_algo supports only zeta3 WebGraph codes");
    }
    const auto window = decimal(props, "windowsize");
    const auto interval = decimal(props, "minintervallength");
    if (window > INT_MAX || interval > INT_MAX) {
        throw std::runtime_error("WebGraph compression parameter exceeds 32-bit range");
    }
    result.window_ = static_cast<int>(window);
    result.min_interval_ = static_cast<int>(interval);

    // The current wg_con tuner changes only references to gamma. Accept the
    // standard codes too, but reject a future code choice we cannot decode.
    const auto flags = props.find("compressionflags");
    if (flags != props.end() && !flags->second.empty()) {
        std::size_t start = 0;
        while (start < flags->second.size()) {
            auto end = flags->second.find('|', start);
            if (end == std::string::npos) end = flags->second.size();
            const auto flag = flags->second.substr(start, end - start);
            const auto split = flag.find('_');
            if (split == std::string::npos) throw std::runtime_error("invalid compressionflags");
            const auto field = flag.substr(0, split);
            const auto name = flag.substr(split + 1);
            Code code;
            if (name == "UNARY") code = Code::unary;
            else if (name == "GAMMA") code = Code::gamma;
            else if (name == "DELTA") code = Code::delta;
            else if (name == "ZETA3") code = Code::zeta3;
            else throw std::runtime_error("unsupported WebGraph code: " + name);

            if (field == "OUTDEGREES") result.outdegree_code_ = code;
            else if (field == "REFERENCES") result.reference_code_ = code;
            else if (field == "BLOCKS") result.block_code_ = code;
            else if (field == "INTERVALS") result.interval_code_ = code;
            else if (field == "RESIDUALS") result.residual_code_ = code;
            else throw std::runtime_error("unsupported WebGraph flag: " + field);
            start = end + 1;
        }
    }

    // Exact field order of sux 0.12.3 EliasFano with SelectAdaptConst<12, 4>
    // as serialized by epserde 0.12.6. Keep views into all three select
    // arrays: the high bits, the two-level inventory, and its rare spill.
    EfReader ef(result.ef_);
    if (result.ef_.size() < 8 ||
        std::memcmp(result.ef_.data(), "epserde ", 8) != 0) {
        throw std::runtime_error("invalid WebGraph .ef header");
    }
    ef.skip(8);
    const auto major = ef.half();
    const auto minor = ef.half();
    const auto usize_size = ef.one();
    if (major != 1 || minor != 1 || usize_size != 8) {
        throw std::runtime_error("unsupported WebGraph .ef epserde version or word size");
    }
    ef.word(); // type hash
    ef.word(); // representation hash
    const auto type_len = ef.word();
    if (type_len > result.ef_.size() - ef.pos()) throw std::runtime_error("invalid .ef type name");
    const std::string type_name(reinterpret_cast<const char*>(result.ef_.data() + ef.pos()),
                                static_cast<std::size_t>(type_len));
    if (type_name.find("EliasFano") == std::string::npos) {
        throw std::runtime_error("unsupported WebGraph .ef type");
    }
    ef.skip(type_len);
    result.ef_n_ = static_cast<std::size_t>(ef.word());
    ef.word(); // upper bound to bit offsets
    result.ef_l_ = static_cast<std::size_t>(ef.word());
    if (result.ef_n_ != static_cast<std::size_t>(result.n_) + 1 || result.ef_l_ >= 64) {
        throw std::runtime_error("invalid WebGraph .ef dimensions");
    }

    const auto low_count = le64(result.ef_, ef.pos());
    result.low_words_ = {ef.words(), static_cast<std::size_t>(low_count)};
    const auto bit_width = ef.word();
    ef.word(); // low-bit mask
    const auto low_len = ef.word();
    if (bit_width != result.ef_l_ || low_len != result.ef_n_) {
        throw std::runtime_error("invalid WebGraph .ef low bits");
    }

    const auto high_count = le64(result.ef_, ef.pos());
    result.high_words_ = {ef.words(), static_cast<std::size_t>(high_count)};
    result.high_len_ = static_cast<std::size_t>(ef.word());
    const auto inventory_count = le64(result.ef_, ef.pos());
    result.inventory_ = {ef.words(), static_cast<std::size_t>(inventory_count)};
    const auto spill_count = le64(result.ef_, ef.pos());
    result.spill_ = {ef.words(), static_cast<std::size_t>(spill_count)};
    if (result.inventory_.count < ((result.ef_n_ + 4095) / 4096) * 17 + 1) {
        throw std::runtime_error("invalid WebGraph .ef select inventory");
    }
    if (result.offset(0) != 0 ||
        result.offset(result.n_) != decimal(props, "length") ||
        result.offset(result.n_) > result.graph_.size() * 8ULL) {
        throw std::runtime_error("invalid WebGraph .ef offsets");
    }
    return result;
}

std::uint64_t Graph::offset(int vertex) const {
    if (vertex < 0 || vertex > n_) throw std::out_of_range("WebGraph vertex");
    const auto rank = static_cast<std::size_t>(vertex);
    const auto inv_idx = (rank / 4096) * 17;
    const auto inv_pos = inventory_.pos + inv_idx * 8;
    const auto entry = le64(ef_, inv_pos);
    std::uint64_t bit = entry & 0x3fff'ffff'ffff'ffffULL;
    std::size_t skip = rank % 4096;
    // sux stores one inventory position per 4096 one bits. The next 16 words
    // give closer hints: 16-bit relative positions for a short span, adaptive
    // 32-bit relative positions for a medium span, and exact 64-bit positions
    // for a very long span. Overflow entries remain in the serialized spill.
    if ((entry >> 63) == 0) {
        const auto hint = skip / 64;
        const auto pos = inv_pos + 8 + hint * 2;
        if (pos + 2 > ef_.size()) throw std::runtime_error("invalid WebGraph .ef subinventory");
        bit += static_cast<std::uint16_t>(ef_[pos]) |
               (static_cast<std::uint16_t>(ef_[pos + 1]) << 8);
        skip %= 64;
    } else if ((entry >> 62) == 2) {
        const auto next = le64(ef_, inv_pos + 17 * 8) & 0x3fff'ffff'ffff'ffffULL;
        if (next <= bit) throw std::runtime_error("invalid WebGraph .ef inventory span");
        const auto span = next - bit;
        if (span < (1ULL << 16)) throw std::runtime_error("invalid WebGraph .ef U32 span");
        // SelectAdaptConst<12, 4>: its 16-bit hints have quantum 64. For
        // larger spans the 32-bit quantum shrinks as the span grows.
        const unsigned log_span = 63U - static_cast<unsigned>(__builtin_clzll(span >> 15));
        const unsigned shift = log_span + 1 >= 6 ? 0 : 6 - log_span - 1;
        const auto hint = skip >> shift;
        if (hint < 30) {
            bit += le32(ef_, inv_pos + 2 * 8 + hint * 4);
        } else {
            const auto spill_start = le64(ef_, inv_pos + 8);
            const auto spill_hint = hint - 30;
            if (spill_start >= spill_.count || spill_hint / 2 >= spill_.count - spill_start) {
                throw std::runtime_error("invalid WebGraph .ef select spill");
            }
            bit += le32(ef_, spill_.pos + static_cast<std::size_t>(spill_start + spill_hint / 2) * 8 +
                              (spill_hint & 1) * 4);
        }
        skip &= (std::size_t(1) << shift) - 1;
    } else {
        if (skip != 0) {
            if (skip < 16) {
                bit = le64(ef_, inv_pos + (skip + 1) * 8);
            } else {
                const auto spill_start = le64(ef_, inv_pos + 8);
                const auto spill_hint = skip - 16;
                if (spill_start >= spill_.count || spill_hint >= spill_.count - spill_start) {
                    throw std::runtime_error("invalid WebGraph .ef select spill");
                }
                bit = le64(ef_, spill_.pos + static_cast<std::size_t>(spill_start + spill_hint) * 8);
            }
        }
        skip = 0;
    }
    std::size_t word_idx = static_cast<std::size_t>(bit / 64);
    std::uint64_t word = 0;
    if (word_idx < high_words_.count) {
        word = le64(ef_, high_words_.pos + word_idx * 8) & (~0ULL << (bit & 63));
    }
    while (true) {
        const auto ones = static_cast<std::size_t>(__builtin_popcountll(word));
        if (skip < ones) {
            while (skip--) word &= word - 1;
            bit = word_idx * 64ULL + static_cast<unsigned>(__builtin_ctzll(word));
            break;
        }
        skip -= ones;
        if (++word_idx >= high_words_.count) throw std::runtime_error("invalid WebGraph .ef high bits");
        word = le64(ef_, high_words_.pos + word_idx * 8);
    }
    if (bit >= high_len_ || bit < rank) throw std::runtime_error("invalid WebGraph .ef select");

    std::uint64_t low = 0;
    if (ef_l_ != 0) {
        const std::uint64_t low_bit = rank * ef_l_;
        const auto low_word = static_cast<std::size_t>(low_bit / 64);
        const auto shift = static_cast<unsigned>(low_bit & 63);
        if (low_word >= low_words_.count) throw std::runtime_error("invalid WebGraph .ef low bits");
        low = le64(ef_, low_words_.pos + low_word * 8) >> shift;
        if (shift + ef_l_ > 64) {
            if (low_word + 1 >= low_words_.count) throw std::runtime_error("invalid WebGraph .ef low bits");
            low |= le64(ef_, low_words_.pos + (low_word + 1) * 8) << (64 - shift);
        }
        low &= (1ULL << ef_l_) - 1;
    }
    return ((bit - rank) << ef_l_) | low;
}

std::uint64_t Graph::read_code(std::uint64_t& bit, Code code) const {
    if (code == Code::unary) return unary(graph_, bit);
    if (code == Code::gamma) {
        const auto len = unary(graph_, bit);
        return bits_at(graph_, bit, static_cast<unsigned>(len)) + ((1ULL << len) - 1);
    }
    if (code == Code::delta) {
        const auto len = read_code(bit, Code::gamma);
        if (len >= 63) throw std::runtime_error("WebGraph delta code exceeds 64 bits");
        return bits_at(graph_, bit, static_cast<unsigned>(len)) + ((1ULL << len) - 1);
    }
    const auto h = unary(graph_, bit);
    const auto shift = h * 3;
    if (shift >= 60) throw std::runtime_error("WebGraph zeta3 code exceeds 64 bits");
    const auto lower = 1ULL << shift;
    const auto choices = (lower << 3) - lower;
    const auto len = 63U - static_cast<unsigned>(__builtin_clzll(choices));
    auto value = bits_at(graph_, bit, len);
    const auto limit = (1ULL << (len + 1)) - choices;
    if (value >= limit) value = (value << 1) | bits_at(graph_, bit, 1), value -= limit;
    return lower + value - 1;
}

void Graph::decode_row(int vertex, std::vector<int>& row) const {
    if (decoder_ == Decoder::Bulk) decode_row_bulk(vertex, row);
    else decode_row_iterator(vertex, row);
}

void Graph::decode_row_iterator(int vertex, std::vector<int>& row) const {
    if (vertex < 0 || vertex >= n_) throw std::out_of_range("WebGraph vertex");
    row.clear();

    // One frame represents one active reference in Rust WebGraph's Succ
    // iterator. Blocks and intervals are read once into query-local metadata;
    // only the requested row is materialized in full.
    struct Frame {
        int remaining = 0;
        int reference = 0;
        int parent_degree = 0;
        int parent_used = 0;
        int copies_left = 0;
        std::vector<int> blocks;
        std::size_t block_index = 0;
        int block_left = 0;
        bool block_copy = true;
        bool copy_ready = false;
        int next_copy = INT_MAX;

        std::vector<std::pair<int, int>> intervals; // [start, exclusive end)
        std::size_t interval_index = 0;
        int next_interval = INT_MAX;

        std::uint64_t residual_bit = 0;
        int residuals_left = 0;
        int next_residual = INT_MAX;
    };

    std::vector<Frame> chain;
    int current = vertex;
    auto bit = offset(current);
    auto degree = read_code(bit, outdegree_code_);
    while (true) {
        Frame frame;
        if (degree > static_cast<std::uint64_t>(n_)) {
            throw std::runtime_error("invalid WebGraph outdegree");
        }
        frame.remaining = static_cast<int>(degree);
        if (degree == 0) {
            chain.push_back(std::move(frame));
            break;
        }

        const auto ref = window_ ? read_code(bit, reference_code_) : 0;
        int parent = current;
        std::uint64_t parent_bit = 0;
        std::uint64_t parent_degree = 0;
        if (ref != 0) {
            if (ref > static_cast<std::uint64_t>(window_) ||
                ref > static_cast<std::uint64_t>(current)) {
                throw std::runtime_error("invalid WebGraph reference");
            }
            frame.reference = static_cast<int>(ref);
            parent = current - frame.reference;
            parent_bit = offset(parent);
            parent_degree = read_code(parent_bit, outdegree_code_);
            if (parent_degree > static_cast<std::uint64_t>(n_)) {
                throw std::runtime_error("invalid WebGraph outdegree");
            }
            frame.parent_degree = static_cast<int>(parent_degree);
            const auto count = read_code(bit, block_code_);
            if (count > static_cast<std::uint64_t>(frame.parent_degree) + 1) {
                throw std::runtime_error("invalid WebGraph block count");
            }
            frame.blocks.reserve(static_cast<std::size_t>(count) + 1);
            int covered = 0;
            for (std::uint64_t i = 0; i < count; ++i) {
                const auto length = read_code(bit, block_code_) + (i != 0);
                if (length > static_cast<std::uint64_t>(frame.parent_degree - covered)) {
                    throw std::runtime_error("invalid WebGraph reference blocks");
                }
                frame.blocks.push_back(static_cast<int>(length));
                if ((i & 1) == 0) frame.copies_left += static_cast<int>(length);
                covered += static_cast<int>(length);
            }
            // As in MaskedIter, the unlisted tail is copied after an even
            // number of explicit blocks and skipped after an odd number.
            const int tail = frame.parent_degree - covered;
            frame.blocks.push_back(tail);
            if ((count & 1) == 0) frame.copies_left += tail;
            if (frame.copies_left > frame.remaining) {
                throw std::runtime_error("invalid WebGraph copied count");
            }
            if (frame.copies_left == 0) frame.reference = 0;
        }

        int uncopied = frame.remaining - frame.copies_left;
        if (uncopied && min_interval_) {
            const auto count = read_code(bit, interval_code_);
            if (count > static_cast<std::uint64_t>(uncopied / min_interval_)) {
                throw std::runtime_error("invalid WebGraph interval count");
            }
            frame.intervals.reserve(static_cast<std::size_t>(count));
            std::int64_t end = 0;
            for (std::uint64_t i = 0; i < count; ++i) {
                const auto start_code = read_code(bit, interval_code_);
                const auto start = i == 0
                    ? static_cast<std::int64_t>(current) + signed_value(start_code)
                    : end + 1 + static_cast<std::int64_t>(start_code);
                const auto length = read_code(bit, interval_code_) +
                                    static_cast<std::uint64_t>(min_interval_);
                if (length > static_cast<std::uint64_t>(uncopied) ||
                    start < 0 || start + static_cast<std::int64_t>(length) > n_) {
                    throw std::runtime_error("invalid WebGraph interval");
                }
                end = start + static_cast<std::int64_t>(length);
                frame.intervals.emplace_back(static_cast<int>(start), static_cast<int>(end));
                uncopied -= static_cast<int>(length);
            }
            if (!frame.intervals.empty()) frame.next_interval = frame.intervals[0].first;
        }
        frame.residual_bit = bit;
        if (uncopied) {
            const auto first = read_code(frame.residual_bit, residual_code_);
            frame.next_residual = vertex_value(static_cast<std::int64_t>(current) +
                                                   signed_value(first), n_);
            frame.residuals_left = uncopied - 1;
        }

        const bool needs_parent = frame.reference != 0;
        chain.push_back(std::move(frame));
        if (!needs_parent) break;
        current = parent;
        bit = parent_bit;
        degree = parent_degree;
    }

    row.reserve(static_cast<std::size_t>(chain[0].remaining));
    const int requested_degree = chain[0].remaining;
    while (static_cast<int>(row.size()) < requested_degree) {
        std::size_t active = 0;
        while (true) {
            Frame& frame = chain[active];
            if (frame.reference && !frame.copy_ready && frame.copies_left) {
                while (frame.block_left == 0) {
                    if (frame.block_index == frame.blocks.size()) {
                        throw std::runtime_error("WebGraph reference mask exhausted");
                    }
                    frame.block_left = frame.blocks[frame.block_index];
                    frame.block_copy = (frame.block_index & 1) == 0;
                    ++frame.block_index;
                }
                ++active; // ask the referenced row for its next successor
                continue;
            }
            if (frame.remaining == 0) {
                throw std::runtime_error("WebGraph reference row exhausted");
            }

            const int copied = frame.copy_ready ? frame.next_copy : INT_MAX;
            const int value = std::min(copied,
                                       std::min(frame.next_interval, frame.next_residual));
            if (value == INT_MAX) {
                throw std::runtime_error("WebGraph row count does not match data");
            }
            --frame.remaining;
            if (value == copied) {
                frame.copy_ready = false;
            } else if (value == frame.next_residual) {
                if (frame.residuals_left) {
                    const auto gap = read_code(frame.residual_bit, residual_code_);
                    frame.next_residual = vertex_value(
                        static_cast<std::int64_t>(frame.next_residual) + 1 +
                        static_cast<std::int64_t>(gap), n_);
                    --frame.residuals_left;
                } else {
                    frame.next_residual = INT_MAX;
                }
            } else {
                ++frame.next_interval;
                if (frame.next_interval == frame.intervals[frame.interval_index].second) {
                    ++frame.interval_index;
                    frame.next_interval = frame.interval_index < frame.intervals.size()
                        ? frame.intervals[frame.interval_index].first : INT_MAX;
                }
            }

            if (active == 0) {
                row.push_back(value);
                break;
            }
            Frame& child = chain[--active];
            if (child.block_left == 0 || child.parent_used >= child.parent_degree) {
                throw std::runtime_error("invalid WebGraph reference mask");
            }
            --child.block_left;
            ++child.parent_used;
            if (child.block_copy) {
                child.next_copy = value;
                child.copy_ready = true;
                --child.copies_left;
            }
        }
    }
}

void Graph::decode_row_bulk(int vertex, std::vector<int>& row) const {
    if (vertex < 0 || vertex >= n_) throw std::out_of_range("WebGraph vertex");
    row.clear();

    // Follow the chosen references, keeping the encoded copy masks but no
    // decoded neighborhoods. A zero-copy reference needs no parent row.
    struct RowInfo {
        int vertex = 0;
        int degree = 0;
        int parent_degree = 0;
        int copied_count = 0;
        std::uint64_t extras_bit = 0;
        std::vector<int> blocks; // alternating copied and skipped runs
    };
    std::vector<RowInfo> chain;
    int current = vertex;
    while (true) {
        auto bit = offset(current);
        const auto degree = read_code(bit, outdegree_code_);
        if (degree > static_cast<std::uint64_t>(n_)) {
            throw std::runtime_error("invalid WebGraph outdegree");
        }
        RowInfo info;
        info.vertex = current;
        info.degree = static_cast<int>(degree);
        int parent = current;
        if (degree != 0) {
            const auto ref = window_ ? read_code(bit, reference_code_) : 0;
            if (ref != 0) {
                if (ref > static_cast<std::uint64_t>(window_) ||
                    ref > static_cast<std::uint64_t>(current)) {
                    throw std::runtime_error("invalid WebGraph reference");
                }
                parent = current - static_cast<int>(ref);
                auto parent_bit = offset(parent);
                const auto parent_degree = read_code(parent_bit, outdegree_code_);
                if (parent_degree > static_cast<std::uint64_t>(n_)) {
                    throw std::runtime_error("invalid WebGraph outdegree");
                }
                info.parent_degree = static_cast<int>(parent_degree);
                const auto count = read_code(bit, block_code_);
                if (count > parent_degree + 1) {
                    throw std::runtime_error("invalid WebGraph block count");
                }
                info.blocks.reserve(static_cast<std::size_t>(count) + 1);
                int covered = 0;
                for (std::uint64_t i = 0; i < count; ++i) {
                    const auto length = read_code(bit, block_code_) + (i != 0);
                    if (length > static_cast<std::uint64_t>(info.parent_degree - covered)) {
                        throw std::runtime_error("invalid WebGraph reference blocks");
                    }
                    info.blocks.push_back(static_cast<int>(length));
                    if ((i & 1) == 0) info.copied_count += static_cast<int>(length);
                    covered += static_cast<int>(length);
                }
                const int tail = info.parent_degree - covered;
                info.blocks.push_back(tail);
                if ((count & 1) == 0) info.copied_count += tail;
                if (info.copied_count > info.degree) {
                    throw std::runtime_error("invalid WebGraph copied count");
                }
            }
        }
        info.extras_bit = bit;
        const bool needs_parent = info.copied_count != 0;
        chain.push_back(std::move(info));
        if (!needs_parent) break;
        current = parent;
    }

    // Rebuild one row at a time, oldest first. `row` holds the parent;
    // `copied` and `extras` are the only complete-row temporary arrays.
    std::vector<int> copied;
    std::vector<int> extras;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const RowInfo& info = *it;
        copied.clear();
        extras.clear();
        if (info.copied_count != 0) {
            if (row.size() != static_cast<std::size_t>(info.parent_degree)) {
                throw std::runtime_error("WebGraph reference row degree mismatch");
            }
            copied.reserve(static_cast<std::size_t>(info.copied_count));
            std::size_t pos = 0;
            for (std::size_t i = 0; i < info.blocks.size(); ++i) {
                const auto length = static_cast<std::size_t>(info.blocks[i]);
                if ((i & 1) == 0) {
                    copied.insert(copied.end(), row.begin() + pos,
                                  row.begin() + pos + length);
                }
                pos += length;
            }
        }

        int uncopied = info.degree - info.copied_count;
        auto bit = info.extras_bit;
        std::vector<std::pair<int, int>> intervals; // [start, exclusive end)
        if (uncopied && min_interval_) {
            const auto count = read_code(bit, interval_code_);
            if (count > static_cast<std::uint64_t>(uncopied / min_interval_)) {
                throw std::runtime_error("invalid WebGraph interval count");
            }
            intervals.reserve(static_cast<std::size_t>(count));
            std::int64_t end = 0;
            for (std::uint64_t i = 0; i < count; ++i) {
                const auto start_code = read_code(bit, interval_code_);
                const auto start = i == 0
                    ? static_cast<std::int64_t>(info.vertex) + signed_value(start_code)
                    : end + 1 + static_cast<std::int64_t>(start_code);
                const auto length = read_code(bit, interval_code_) +
                                    static_cast<std::uint64_t>(min_interval_);
                if (length > static_cast<std::uint64_t>(uncopied) ||
                    start < 0 || start + static_cast<std::int64_t>(length) > n_) {
                    throw std::runtime_error("invalid WebGraph interval");
                }
                end = start + static_cast<std::int64_t>(length);
                intervals.emplace_back(static_cast<int>(start), static_cast<int>(end));
                uncopied -= static_cast<int>(length);
            }
        }

        // Intervals and residuals are separately sorted. Merge them into
        // `extras` as residual codes are read; only interval endpoints are
        // stored separately, not an expanded interval array.
        extras.reserve(static_cast<std::size_t>(info.degree - info.copied_count));
        std::size_t interval_index = 0;
        int next_interval = intervals.empty() ? INT_MAX : intervals[0].first;
        const auto append_intervals_before = [&](int limit) {
            while (next_interval < limit) {
                extras.push_back(next_interval++);
                if (next_interval == intervals[interval_index].second) {
                    ++interval_index;
                    next_interval = interval_index < intervals.size()
                        ? intervals[interval_index].first : INT_MAX;
                }
            }
        };
        if (uncopied != 0) {
            const auto first = read_code(bit, residual_code_);
            int residual = vertex_value(static_cast<std::int64_t>(info.vertex) +
                                            signed_value(first), n_);
            append_intervals_before(residual);
            if (next_interval == residual) {
                throw std::runtime_error("duplicate WebGraph neighbor");
            }
            extras.push_back(residual);
            for (int i = 1; i < uncopied; ++i) {
                const auto gap = read_code(bit, residual_code_);
                if (gap >= static_cast<std::uint64_t>(n_)) {
                    throw std::runtime_error("invalid WebGraph residual gap");
                }
                residual = vertex_value(static_cast<std::int64_t>(residual) + 1 +
                                            static_cast<std::int64_t>(gap), n_);
                append_intervals_before(residual);
                if (next_interval == residual) {
                    throw std::runtime_error("duplicate WebGraph neighbor");
                }
                extras.push_back(residual);
            }
        }
        append_intervals_before(INT_MAX);
        if (copied.size() + extras.size() != static_cast<std::size_t>(info.degree)) {
            throw std::runtime_error("WebGraph row count does not match data");
        }
        row.resize(static_cast<std::size_t>(info.degree));
        std::merge(copied.begin(), copied.end(), extras.begin(), extras.end(), row.begin());
    }
}

} // namespace wg_algo
