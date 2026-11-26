#include "cc_con/output.hpp"

#include "artifact_support.hpp"
#include "cc_con/cgf.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace cc_con {
namespace {

constexpr std::array<std::uint8_t, 8> kCcfMagic{
    'C', 'C', 'F', 0, '\r', '\n', 0x1aU, '\n'};
constexpr std::uint32_t kHeaderBytes = 160;
constexpr std::uint64_t kChecksumOffset = 128;
constexpr std::uint64_t kChecksumBytes = 32;

struct CcfHeader {
    std::uint64_t n = 0;
    std::uint64_t m = 0;
    std::uint64_t num_cliques = 0;
    std::uint64_t cover_size = 0;
    std::uint64_t offsets_offset = 0;
    std::uint64_t offsets_count = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_bytes = 0;
    std::uint64_t file_bytes = 0;
    Sha256Digest graph_fingerprint{};
    Sha256Digest file_checksum{};
};

std::uint64_t checked_offsets_count(const std::uint64_t records) {
    if (records == std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("CCF has too many clique records");
    }
    return records + 1;
}

std::uint64_t checked_payload_offset(const std::uint64_t offsets_count) {
    if (offsets_count >
        (std::numeric_limits<std::uint64_t>::max() - kHeaderBytes) / 8U) {
        throw std::runtime_error("CCF offset table is too large");
    }
    return kHeaderBytes + 8U * offsets_count;
}

std::uint64_t stream_position(std::ostream& output,
                              const std::string& context) {
    const std::streampos position = output.tellp();
    if (position < 0) throw std::runtime_error("Unable to obtain " + context);
    return static_cast<std::uint64_t>(position);
}

void validate_clique(const std::vector<int>& clique, const int n) {
    if (clique.size() < 2) {
        throw std::runtime_error("CCF cannot store a clique with fewer than two vertices");
    }
    for (std::size_t i = 0; i < clique.size(); ++i) {
        if (clique[i] < 0 || clique[i] >= n) {
            throw std::runtime_error("CCF clique contains a vertex outside the graph");
        }
        if (i > 0 && clique[i - 1] >= clique[i]) {
            throw std::runtime_error("CCF cliques must be sorted and duplicate-free");
        }
    }
}

std::array<std::uint8_t, kHeaderBytes> make_header(
    const CcfHeader& header,
    const bool include_checksum) {
    std::array<std::uint8_t, kHeaderBytes> bytes{};
    std::copy(kCcfMagic.begin(), kCcfMagic.end(), bytes.begin());
    store_u32_le(bytes.data() + 8, kCcfFormatVersion);
    store_u32_le(bytes.data() + 12, kHeaderBytes);
    store_u32_le(bytes.data() + 16, kDeltaVbyteEncoding);
    store_u32_le(bytes.data() + 20, 0);
    store_u64_le(bytes.data() + 24, header.n);
    store_u64_le(bytes.data() + 32, header.m);
    store_u64_le(bytes.data() + 40, header.num_cliques);
    store_u64_le(bytes.data() + 48, header.cover_size);
    store_u64_le(bytes.data() + 56, header.offsets_offset);
    store_u64_le(bytes.data() + 64, header.offsets_count);
    store_u64_le(bytes.data() + 72, header.payload_offset);
    store_u64_le(bytes.data() + 80, header.payload_bytes);
    store_u64_le(bytes.data() + 88, header.file_bytes);
    std::copy(header.graph_fingerprint.begin(),
              header.graph_fingerprint.end(), bytes.begin() + 96);
    if (include_checksum) {
        std::copy(header.file_checksum.begin(), header.file_checksum.end(),
                  bytes.begin() + static_cast<std::ptrdiff_t>(kChecksumOffset));
    }
    return bytes;
}

CcfHeader parse_header(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open CCF file '" + path.string() + "'");
    }
    std::array<std::uint8_t, kHeaderBytes> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("CCF file is smaller than its header");

    if (!std::equal(kCcfMagic.begin(), kCcfMagic.end(), bytes.begin())) {
        throw std::runtime_error("CCF magic is invalid");
    }
    if (load_u32_le(bytes.data() + 8) != kCcfFormatVersion) {
        throw std::runtime_error("Unsupported CCF format version");
    }
    if (load_u32_le(bytes.data() + 12) != kHeaderBytes) {
        throw std::runtime_error("Unsupported CCF header size");
    }
    if (load_u32_le(bytes.data() + 16) != kDeltaVbyteEncoding) {
        throw std::runtime_error("Unsupported CCF encoding");
    }
    if (load_u32_le(bytes.data() + 20) != 0) {
        throw std::runtime_error("CCF uses unsupported flags");
    }

    CcfHeader header;
    header.n = load_u64_le(bytes.data() + 24);
    header.m = load_u64_le(bytes.data() + 32);
    header.num_cliques = load_u64_le(bytes.data() + 40);
    header.cover_size = load_u64_le(bytes.data() + 48);
    header.offsets_offset = load_u64_le(bytes.data() + 56);
    header.offsets_count = load_u64_le(bytes.data() + 64);
    header.payload_offset = load_u64_le(bytes.data() + 72);
    header.payload_bytes = load_u64_le(bytes.data() + 80);
    header.file_bytes = load_u64_le(bytes.data() + 88);
    std::copy(bytes.begin() + 96, bytes.begin() + 128,
              header.graph_fingerprint.begin());
    std::copy(bytes.begin() + 128, bytes.end(), header.file_checksum.begin());

    const std::uint64_t expected_offsets_count =
        checked_offsets_count(header.num_cliques);
    const std::uint64_t expected_payload_offset =
        checked_payload_offset(expected_offsets_count);
    if (header.offsets_offset != kHeaderBytes ||
        header.offsets_count != expected_offsets_count ||
        header.payload_offset != expected_payload_offset) {
        throw std::runtime_error("CCF offset-table layout is inconsistent");
    }
    if (header.payload_bytes >
            std::numeric_limits<std::uint64_t>::max() - header.payload_offset ||
        header.file_bytes != header.payload_offset + header.payload_bytes) {
        throw std::runtime_error("CCF payload length is inconsistent");
    }

    std::error_code error;
    const std::uintmax_t physical_size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error("Unable to inspect CCF size: " + error.message());
    }
    if (physical_size != header.file_bytes) {
        throw std::runtime_error("CCF physical size differs from its header");
    }
    return header;
}

void write_header(const std::filesystem::path& path,
                  const CcfHeader& header,
                  const bool include_checksum) {
    std::fstream output(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!output) throw std::runtime_error("Unable to reopen temporary CCF header");
    const auto bytes = make_header(header, include_checksum);
    output.seekp(0);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("Failed while writing CCF header");
}

}  // namespace

CcfArtifactInfo write_ccf(
    const std::filesystem::path& path,
    const std::vector<std::vector<int>>& C,
    const int n,
    const long long m,
    const std::string& graph_fingerprint_sha256) {
    if (n < 0 || m < 0) {
        throw std::runtime_error("CCF cannot store negative graph statistics");
    }

    CcfHeader header;
    header.n = static_cast<std::uint64_t>(n);
    header.m = static_cast<std::uint64_t>(m);
    header.num_cliques = static_cast<std::uint64_t>(C.size());
    header.offsets_offset = kHeaderBytes;
    header.offsets_count = checked_offsets_count(header.num_cliques);
    header.payload_offset = checked_payload_offset(header.offsets_count);
    header.graph_fingerprint = parse_sha256_hex(graph_fingerprint_sha256);

    for (const std::vector<int>& clique : C) {
        validate_clique(clique, n);
        if (clique.size() >
            std::numeric_limits<std::uint64_t>::max() - header.cover_size) {
            throw std::runtime_error("CCF cover size overflows uint64");
        }
        header.cover_size += static_cast<std::uint64_t>(clique.size());
    }

    const std::filesystem::path temporary = temporary_artifact_path(path);
    try {
        std::fstream payload(temporary,
                             std::ios::in | std::ios::out | std::ios::binary |
                                 std::ios::trunc);
        if (!payload) {
            throw std::runtime_error("Unable to open temporary CCF '" +
                                     temporary.string() + "'");
        }
        write_zero_bytes(payload, header.payload_offset);
        payload.flush();
        if (!payload) throw std::runtime_error("Failed while reserving CCF layout");
        payload.seekp(static_cast<std::streamoff>(header.payload_offset));

        // The offset and payload streams advance independently through disjoint
        // file ranges. This retains R+1 random-access offsets without holding a
        // potentially large directory twice in memory.
        std::fstream offsets(temporary,
                             std::ios::in | std::ios::out | std::ios::binary);
        if (!offsets) throw std::runtime_error("Unable to open CCF offset table");
        offsets.seekp(static_cast<std::streamoff>(header.offsets_offset));

        for (const std::vector<int>& clique : C) {
            write_u64_le(offsets, stream_position(payload, "CCF payload position"));
            write_vbyte(payload, static_cast<std::uint64_t>(clique.size()));
            std::uint64_t previous = 0;
            for (std::size_t i = 0; i < clique.size(); ++i) {
                const std::uint64_t vertex = static_cast<std::uint64_t>(clique[i]);
                write_vbyte(payload, i == 0 ? vertex : vertex - previous);
                previous = vertex;
            }
        }
        header.file_bytes = stream_position(payload, "CCF file size");
        write_u64_le(offsets, header.file_bytes);
        header.payload_bytes = header.file_bytes - header.payload_offset;

        payload.close();
        offsets.close();
        if (!payload || !offsets) {
            throw std::runtime_error("Failed while finalizing temporary CCF");
        }

        write_header(temporary, header, false);
        header.file_checksum = sha256_file_with_zero_range(
            temporary, kChecksumOffset, kChecksumBytes);
        write_header(temporary, header, true);
        publish_artifact(temporary, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }

    CcfArtifactInfo info;
    info.n = header.n;
    info.m = header.m;
    info.num_cliques = header.num_cliques;
    info.cover_size = header.cover_size;
    info.file_bytes = header.file_bytes;
    info.graph_fingerprint_sha256 = sha256_hex(header.graph_fingerprint);
    info.file_checksum_sha256 = sha256_hex(header.file_checksum);
    return info;
}

CcfArtifactInfo read_and_validate_ccf(
    const std::filesystem::path& path,
    std::vector<std::vector<int>>& C) {
    const CcfHeader header = parse_header(path);
    if (header.n > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("CCF graph order exceeds int vertex identifiers");
    }
    if (header.m > static_cast<std::uint64_t>(
                       std::numeric_limits<long long>::max())) {
        throw std::runtime_error("CCF edge count exceeds long long");
    }
    const std::uint64_t maximum_edges =
        header.n < 2 ? 0 : header.n * (header.n - 1U) / 2U;
    if (header.m > maximum_edges) {
        throw std::runtime_error("CCF m exceeds the simple-graph maximum");
    }
    if (header.num_cliques > static_cast<std::uint64_t>(
                                 std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("CCF clique count exceeds addressable memory");
    }

    const Sha256Digest computed_checksum = sha256_file_with_zero_range(
        path, kChecksumOffset, kChecksumBytes);
    if (computed_checksum != header.file_checksum) {
        throw std::runtime_error("CCF SHA-256 checksum does not match its contents");
    }

    std::ifstream offsets(path, std::ios::binary);
    std::ifstream payload(path, std::ios::binary);
    if (!offsets || !payload) throw std::runtime_error("Unable to reopen CCF");
    offsets.seekg(static_cast<std::streamoff>(header.offsets_offset));
    payload.seekg(static_cast<std::streamoff>(header.payload_offset));

    std::uint64_t record_begin = read_u64_le(offsets, "CCF offset table");
    if (record_begin != header.payload_offset) {
        throw std::runtime_error("CCF first offset does not start the payload");
    }

    C.assign(static_cast<std::size_t>(header.num_cliques), {});
    std::uint64_t decoded_cover_size = 0;
    for (std::uint64_t label = 0; label < header.num_cliques; ++label) {
        const std::uint64_t record_end =
            read_u64_le(offsets, "CCF offset table");
        if (record_end < record_begin || record_end > header.file_bytes) {
            throw std::runtime_error("CCF offsets are not monotone and in range");
        }

        std::uint64_t position = record_begin;
        const std::uint64_t cardinality =
            read_vbyte(payload, position, record_end, "CCF clique");
        if (cardinality < 2 || cardinality > header.n ||
            cardinality > static_cast<std::uint64_t>(
                              std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error("CCF clique cardinality is invalid");
        }
        std::vector<int>& clique = C[static_cast<std::size_t>(label)];
        clique.reserve(static_cast<std::size_t>(cardinality));

        std::uint64_t previous = 0;
        for (std::uint64_t i = 0; i < cardinality; ++i) {
            const std::uint64_t difference =
                read_vbyte(payload, position, record_end, "CCF clique");
            if (i > 0 && difference == 0) {
                throw std::runtime_error("CCF clique contains a duplicate vertex");
            }
            if (difference > std::numeric_limits<std::uint64_t>::max() - previous) {
                throw std::runtime_error("CCF delta decoding overflows");
            }
            const std::uint64_t vertex = previous + difference;
            if (vertex >= header.n) {
                throw std::runtime_error("CCF clique vertex is out of range");
            }
            clique.push_back(static_cast<int>(vertex));
            previous = vertex;
        }
        if (position != record_end) {
            throw std::runtime_error("CCF record has bytes after its clique");
        }
        if (decoded_cover_size >
            std::numeric_limits<std::uint64_t>::max() - cardinality) {
            throw std::runtime_error("CCF decoded cover size overflows");
        }
        decoded_cover_size += cardinality;
        record_begin = record_end;
    }
    if (record_begin != header.file_bytes) {
        throw std::runtime_error("CCF final offset does not end the payload");
    }
    if (decoded_cover_size != header.cover_size) {
        throw std::runtime_error("CCF decoded cover size differs from its header");
    }

    CcfArtifactInfo info;
    info.n = header.n;
    info.m = header.m;
    info.num_cliques = header.num_cliques;
    info.cover_size = header.cover_size;
    info.file_bytes = header.file_bytes;
    info.graph_fingerprint_sha256 = sha256_hex(header.graph_fingerprint);
    info.file_checksum_sha256 = sha256_hex(header.file_checksum);
    return info;
}

}  // namespace cc_con
