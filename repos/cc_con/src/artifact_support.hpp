#ifndef CC_CON_ARTIFACT_SUPPORT_HPP
#define CC_CON_ARTIFACT_SUPPORT_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace cc_con {

using Sha256Digest = std::array<std::uint8_t, 32>;

// Small streaming SHA-256 implementation used by both artifact formats. Keeping
// it here avoids a platform-specific OpenSSL/CommonCrypto dependency on clusters.
struct Sha256State {
    std::array<std::uint32_t, 8> state{};
    std::array<std::uint8_t, 64> block{};
    std::uint64_t total_bytes = 0;
    std::size_t block_size = 0;

    Sha256State();
    void update(const void* data, std::size_t size);
    Sha256Digest finish();
};

std::string sha256_hex(const Sha256Digest& digest);
Sha256Digest parse_sha256_hex(const std::string& text);

// The selected byte range is treated as zero. Artifact checksums use this to
// cover the entire file while excluding the checksum field itself.
Sha256Digest sha256_file_with_zero_range(
    const std::filesystem::path& path,
    std::uint64_t zero_offset,
    std::uint64_t zero_length);

void store_u32_le(std::uint8_t* destination, std::uint32_t value);
void store_u64_le(std::uint8_t* destination, std::uint64_t value);
std::uint32_t load_u32_le(const std::uint8_t* source);
std::uint64_t load_u64_le(const std::uint8_t* source);

void write_u64_le(std::ostream& output, std::uint64_t value);
std::uint64_t read_u64_le(std::istream& input, const std::string& context);

void write_vbyte(std::ostream& output, std::uint64_t value);
std::uint64_t read_vbyte(std::istream& input,
                         std::uint64_t& position,
                         std::uint64_t end,
                         const std::string& context);

void write_zero_bytes(std::ostream& output, std::uint64_t count);
std::filesystem::path temporary_artifact_path(
    const std::filesystem::path& final_path);
void publish_artifact(const std::filesystem::path& temporary_path,
                      const std::filesystem::path& final_path);

}  // namespace cc_con

#endif
