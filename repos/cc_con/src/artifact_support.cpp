#include "artifact_support.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace cc_con {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

std::uint32_t rotate_right(const std::uint32_t value, const unsigned shift) {
    return (value >> shift) | (value << (32U - shift));
}

void process_block(Sha256State& hash) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        const std::size_t p = 4 * i;
        words[i] = (static_cast<std::uint32_t>(hash.block[p]) << 24U) |
                   (static_cast<std::uint32_t>(hash.block[p + 1]) << 16U) |
                   (static_cast<std::uint32_t>(hash.block[p + 2]) << 8U) |
                   static_cast<std::uint32_t>(hash.block[p + 3]);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        const std::uint32_t s0 = rotate_right(words[i - 15], 7) ^
                                 rotate_right(words[i - 15], 18) ^
                                 (words[i - 15] >> 3U);
        const std::uint32_t s1 = rotate_right(words[i - 2], 17) ^
                                 rotate_right(words[i - 2], 19) ^
                                 (words[i - 2] >> 10U);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    std::uint32_t a = hash.state[0];
    std::uint32_t b = hash.state[1];
    std::uint32_t c = hash.state[2];
    std::uint32_t d = hash.state[3];
    std::uint32_t e = hash.state[4];
    std::uint32_t f = hash.state[5];
    std::uint32_t g = hash.state[6];
    std::uint32_t h = hash.state[7];

    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                                   rotate_right(e, 25);
        const std::uint32_t choose = (e & f) ^ ((~e) & g);
        const std::uint32_t temporary1 =
            h + sum1 + choose + kRoundConstants[i] + words[i];
        const std::uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                                   rotate_right(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    hash.state[0] += a;
    hash.state[1] += b;
    hash.state[2] += c;
    hash.state[3] += d;
    hash.state[4] += e;
    hash.state[5] += f;
    hash.state[6] += g;
    hash.state[7] += h;
}

unsigned hex_digit(const char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
    throw std::runtime_error("Invalid hexadecimal SHA-256 digest");
}

}  // namespace

Sha256State::Sha256State()
    : state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
            0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

void Sha256State::update(const void* raw_data, const std::size_t size) {
    const auto* data = static_cast<const std::uint8_t*>(raw_data);
    if (size > std::numeric_limits<std::uint64_t>::max() - total_bytes) {
        throw std::runtime_error("SHA-256 input is too large");
    }
    total_bytes += static_cast<std::uint64_t>(size);

    std::size_t consumed = 0;
    while (consumed < size) {
        const std::size_t available = block.size() - block_size;
        const std::size_t copied = std::min(available, size - consumed);
        std::memcpy(block.data() + block_size, data + consumed, copied);
        block_size += copied;
        consumed += copied;
        if (block_size == block.size()) {
            process_block(*this);
            block_size = 0;
        }
    }
}

Sha256Digest Sha256State::finish() {
    if (total_bytes > std::numeric_limits<std::uint64_t>::max() / 8U) {
        throw std::runtime_error("SHA-256 bit length overflows uint64");
    }
    const std::uint64_t bit_length = total_bytes * 8U;

    block[block_size++] = 0x80U;
    if (block_size > 56) {
        std::fill(block.begin() + static_cast<std::ptrdiff_t>(block_size),
                  block.end(), 0);
        process_block(*this);
        block_size = 0;
    }
    std::fill(block.begin() + static_cast<std::ptrdiff_t>(block_size),
              block.begin() + 56, 0);
    for (unsigned byte = 0; byte < 8; ++byte) {
        block[63 - byte] =
            static_cast<std::uint8_t>((bit_length >> (8U * byte)) & 0xffU);
    }
    process_block(*this);

    Sha256Digest digest{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        digest[4 * i] = static_cast<std::uint8_t>(state[i] >> 24U);
        digest[4 * i + 1] = static_cast<std::uint8_t>(state[i] >> 16U);
        digest[4 * i + 2] = static_cast<std::uint8_t>(state[i] >> 8U);
        digest[4 * i + 3] = static_cast<std::uint8_t>(state[i]);
    }
    return digest;
}

std::string sha256_hex(const Sha256Digest& digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint8_t byte : digest) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

Sha256Digest parse_sha256_hex(const std::string& text) {
    if (text.size() != 64) {
        throw std::runtime_error("SHA-256 digest must contain 64 hexadecimal digits");
    }
    Sha256Digest digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        digest[i] = static_cast<std::uint8_t>(
            (hex_digit(text[2 * i]) << 4U) | hex_digit(text[2 * i + 1]));
    }
    return digest;
}

Sha256Digest sha256_file_with_zero_range(
    const std::filesystem::path& path,
    const std::uint64_t zero_offset,
    const std::uint64_t zero_length) {
    if (zero_length > std::numeric_limits<std::uint64_t>::max() - zero_offset) {
        throw std::runtime_error("Invalid checksum exclusion range");
    }
    const std::uint64_t zero_end = zero_offset + zero_length;

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open artifact for checksumming '" +
                                 path.string() + "'");
    }

    Sha256State hash;
    std::array<std::uint8_t, 1U << 16U> buffer{};
    std::uint64_t file_position = 0;
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0) break;

        const std::uint64_t chunk_end =
            file_position + static_cast<std::uint64_t>(count);
        const std::uint64_t overlap_begin = std::max(file_position, zero_offset);
        const std::uint64_t overlap_end = std::min(chunk_end, zero_end);
        if (overlap_begin < overlap_end) {
            std::fill(buffer.begin() + static_cast<std::ptrdiff_t>(
                          overlap_begin - file_position),
                      buffer.begin() + static_cast<std::ptrdiff_t>(
                          overlap_end - file_position),
                      0);
        }
        hash.update(buffer.data(), static_cast<std::size_t>(count));
        file_position = chunk_end;
    }
    if (!input.eof()) {
        throw std::runtime_error("Failed while checksumming artifact '" +
                                 path.string() + "'");
    }
    return hash.finish();
}

void store_u32_le(std::uint8_t* destination, const std::uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) {
        destination[byte] =
            static_cast<std::uint8_t>((value >> (8U * byte)) & 0xffU);
    }
}

void store_u64_le(std::uint8_t* destination, const std::uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) {
        destination[byte] =
            static_cast<std::uint8_t>((value >> (8U * byte)) & 0xffU);
    }
}

std::uint32_t load_u32_le(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(source[byte]) << (8U * byte);
    }
    return value;
}

std::uint64_t load_u64_le(const std::uint8_t* source) {
    std::uint64_t value = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(source[byte]) << (8U * byte);
    }
    return value;
}

void write_u64_le(std::ostream& output, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    store_u64_le(bytes.data(), value);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("Failed while writing artifact offset");
}

std::uint64_t read_u64_le(std::istream& input, const std::string& context) {
    std::array<std::uint8_t, 8> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("Truncated " + context);
    return load_u64_le(bytes.data());
}

void write_vbyte(std::ostream& output, std::uint64_t value) {
    while (value >= 128U) {
        output.put(static_cast<char>((value & 127U) | 128U));
        value >>= 7U;
    }
    output.put(static_cast<char>(value));
    if (!output) throw std::runtime_error("Failed while writing vbyte payload");
}

std::uint64_t read_vbyte(std::istream& input,
                         std::uint64_t& position,
                         const std::uint64_t end,
                         const std::string& context) {
    std::uint64_t value = 0;
    unsigned shift = 0;
    while (position < end) {
        const int raw = input.get();
        if (raw == std::char_traits<char>::eof()) {
            throw std::runtime_error("Truncated " + context);
        }
        ++position;
        const std::uint8_t byte = static_cast<std::uint8_t>(raw);
        const std::uint64_t low = byte & 127U;
        if (shift == 63 && low > 1U) {
            throw std::runtime_error("Overflowing vbyte integer in " + context);
        }
        value |= low << shift;
        if ((byte & 128U) == 0) {
            if (shift > 0 && low == 0) {
                throw std::runtime_error("Noncanonical vbyte integer in " + context);
            }
            return value;
        }
        if (shift >= 63) {
            throw std::runtime_error("Overlong vbyte integer in " + context);
        }
        shift += 7;
    }
    throw std::runtime_error("Record ends inside a vbyte integer in " + context);
}

void write_zero_bytes(std::ostream& output, std::uint64_t count) {
    const std::array<char, 1U << 16U> zeros{};
    while (count > 0) {
        const std::uint64_t chunk = std::min<std::uint64_t>(count, zeros.size());
        output.write(zeros.data(), static_cast<std::streamsize>(chunk));
        if (!output) throw std::runtime_error("Failed while reserving artifact bytes");
        count -= chunk;
    }
}

std::filesystem::path temporary_artifact_path(
    const std::filesystem::path& final_path) {
    std::filesystem::path temporary = final_path;
    temporary += ".tmp";
    return temporary;
}

void publish_artifact(const std::filesystem::path& temporary_path,
                      const std::filesystem::path& final_path) {
    std::error_code error;
    std::filesystem::rename(temporary_path, final_path, error);
    if (error) {
        throw std::runtime_error("Unable to publish artifact '" +
                                 final_path.string() + "': " + error.message());
    }
}

}  // namespace cc_con
