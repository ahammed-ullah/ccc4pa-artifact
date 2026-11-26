#include "format.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace zk {
namespace {

constexpr char graph_magic[] = "ZKCCF001";
constexpr char index_magic[] = "ZKIDX001";
constexpr std::size_t index_header_bytes = 24;

void put_number(std::vector<std::uint8_t>& out, std::size_t at,
                std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
        out[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

unsigned hex_digit(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
    throw std::runtime_error("graph fingerprint is not hexadecimal");
}

void write_bytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("cannot write " + path);
    }
}

} // namespace

void write_graph_header(const std::string& path, const GraphHeader& header) {
    if (header.fingerprint.size() != 64) {
        throw std::runtime_error("graph fingerprint must have 64 hexadecimal digits");
    }
    std::vector<std::uint8_t> bytes(graph_header_bytes, 0);
    std::copy_n(graph_magic, 8, bytes.begin());
    put_number(bytes, 8, header.n, 4);
    put_number(bytes, 12, window, 4);
    put_number(bytes, 16, header.m, 8);
    bytes[24] = chunk_size;
    bytes[25] = zero_run_min;
    bytes[26] = hybrid_k;
    bytes[27] = hybrid_i;
    bytes[28] = hybrid_j;
    bytes[29] = reference_rounds;
    bytes[30] = 1; // Huffman
    bytes[31] = 1; // unlimited reference chain
    for (std::size_t i = 0; i < 32; ++i) {
        bytes[32 + i] = static_cast<std::uint8_t>(
            16 * hex_digit(header.fingerprint[2 * i]) +
            hex_digit(header.fingerprint[2 * i + 1]));
    }
    write_bytes(path, bytes);
}

void write_index(const std::string& path,
                 const std::vector<std::uint64_t>& offsets) {
    if (offsets.empty() || offsets.size() - 1 >
                               std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("invalid row count for ZK index");
    }
    if (offsets.front() < 8 * graph_header_bytes ||
        !std::is_sorted(offsets.begin(), offsets.end())) {
        throw std::runtime_error("row offsets are invalid");
    }
    const std::uint64_t last = offsets.back();
    std::uint8_t width = 1;
    while (width < 64 && (last >> width) != 0) ++width;
    const std::uint64_t nbits = std::uint64_t(offsets.size()) * width;
    if (nbits > std::numeric_limits<std::size_t>::max() - 7) {
        throw std::runtime_error("ZK index is too large");
    }
    const std::size_t packed_bytes = static_cast<std::size_t>((nbits + 7) / 8);
    std::vector<std::uint8_t> bytes(index_header_bytes + packed_bytes, 0);
    std::copy_n(index_magic, 8, bytes.begin());
    put_number(bytes, 8, offsets.size() - 1, 4);
    bytes[12] = width;
    put_number(bytes, 16, last, 8);
    for (std::size_t row = 0; row < offsets.size(); ++row) {
        const std::uint64_t bit = std::uint64_t(row) * width;
        const std::uint64_t value = offsets[row];
        for (unsigned k = 0; k < width; ++k) {
            if ((value >> k) & 1) {
                const auto pos = bit + k;
                bytes[index_header_bytes + pos / 8] |=
                    std::uint8_t(1u << (pos % 8));
            }
        }
    }
    write_bytes(path, bytes);
}

} // namespace zk
