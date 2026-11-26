#include "cc_con/cgf.hpp"

#include "artifact_support.hpp"
#include "cc_con/graph.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace cc_con {
namespace {

constexpr std::array<std::uint8_t, 8> kCgfMagic{
    'C', 'G', 'F', 0, '\r', '\n', 0x1aU, '\n'};
constexpr std::uint32_t kHeaderBytes = 160;
constexpr std::uint64_t kChecksumOffset = 128;
constexpr std::uint64_t kChecksumBytes = 32;

struct CgfHeader {
    std::uint64_t n = 0;
    std::uint64_t m = 0;
    std::uint64_t adjacency_entries = 0;
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
        throw std::runtime_error("CGF has too many neighborhood records");
    }
    return records + 1;
}

std::uint64_t checked_payload_offset(const std::uint64_t offsets_count) {
    if (offsets_count >
        (std::numeric_limits<std::uint64_t>::max() - kHeaderBytes) / 8U) {
        throw std::runtime_error("CGF offset table is too large");
    }
    return kHeaderBytes + 8U * offsets_count;
}

std::uint64_t stream_position(std::ostream& output,
                              const std::string& context) {
    const std::streampos position = output.tellp();
    if (position < 0) throw std::runtime_error("Unable to obtain " + context);
    return static_cast<std::uint64_t>(position);
}

std::array<std::uint8_t, kHeaderBytes> make_header(
    const CgfHeader& header,
    const bool include_checksum) {
    std::array<std::uint8_t, kHeaderBytes> bytes{};
    std::copy(kCgfMagic.begin(), kCgfMagic.end(), bytes.begin());
    store_u32_le(bytes.data() + 8, kCgfFormatVersion);
    store_u32_le(bytes.data() + 12, kHeaderBytes);
    store_u32_le(bytes.data() + 16, kDeltaVbyteEncoding);
    store_u32_le(bytes.data() + 20, 0);
    store_u64_le(bytes.data() + 24, header.n);
    store_u64_le(bytes.data() + 32, header.m);
    store_u64_le(bytes.data() + 40, header.n);
    store_u64_le(bytes.data() + 48, header.adjacency_entries);
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

CgfHeader parse_header(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open CGF file '" + path.string() + "'");
    }
    std::array<std::uint8_t, kHeaderBytes> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("CGF file is smaller than its header");

    if (!std::equal(kCgfMagic.begin(), kCgfMagic.end(), bytes.begin())) {
        throw std::runtime_error("CGF magic is invalid");
    }
    if (load_u32_le(bytes.data() + 8) != kCgfFormatVersion) {
        throw std::runtime_error("Unsupported CGF format version");
    }
    if (load_u32_le(bytes.data() + 12) != kHeaderBytes) {
        throw std::runtime_error("Unsupported CGF header size");
    }
    if (load_u32_le(bytes.data() + 16) != kDeltaVbyteEncoding) {
        throw std::runtime_error("Unsupported CGF encoding");
    }
    if (load_u32_le(bytes.data() + 20) != 0) {
        throw std::runtime_error("CGF uses unsupported flags");
    }

    CgfHeader header;
    header.n = load_u64_le(bytes.data() + 24);
    header.m = load_u64_le(bytes.data() + 32);
    if (load_u64_le(bytes.data() + 40) != header.n) {
        throw std::runtime_error("CGF record count does not equal n");
    }
    header.adjacency_entries = load_u64_le(bytes.data() + 48);
    header.offsets_offset = load_u64_le(bytes.data() + 56);
    header.offsets_count = load_u64_le(bytes.data() + 64);
    header.payload_offset = load_u64_le(bytes.data() + 72);
    header.payload_bytes = load_u64_le(bytes.data() + 80);
    header.file_bytes = load_u64_le(bytes.data() + 88);
    std::copy(bytes.begin() + 96, bytes.begin() + 128,
              header.graph_fingerprint.begin());
    std::copy(bytes.begin() + 128, bytes.end(), header.file_checksum.begin());

    const std::uint64_t expected_offsets_count = checked_offsets_count(header.n);
    const std::uint64_t expected_payload_offset =
        checked_payload_offset(expected_offsets_count);
    if (header.offsets_offset != kHeaderBytes ||
        header.offsets_count != expected_offsets_count ||
        header.payload_offset != expected_payload_offset) {
        throw std::runtime_error("CGF offset-table layout is inconsistent");
    }
    if (header.payload_bytes >
            std::numeric_limits<std::uint64_t>::max() - header.payload_offset ||
        header.file_bytes != header.payload_offset + header.payload_bytes) {
        throw std::runtime_error("CGF payload length is inconsistent");
    }
    if (header.m > std::numeric_limits<std::uint64_t>::max() / 2U ||
        header.adjacency_entries != 2U * header.m) {
        throw std::runtime_error("CGF adjacency-entry count is inconsistent with m");
    }

    std::error_code error;
    const std::uintmax_t physical_size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error("Unable to inspect CGF size: " + error.message());
    }
    if (physical_size != header.file_bytes) {
        throw std::runtime_error("CGF physical size differs from its header");
    }
    return header;
}

void write_header(const std::filesystem::path& path,
                  const CgfHeader& header,
                  const bool include_checksum) {
    std::fstream output(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!output) throw std::runtime_error("Unable to reopen temporary CGF header");
    const auto bytes = make_header(header, include_checksum);
    output.seekp(0);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("Failed while writing CGF header");
}

}  // namespace

std::string canonical_graph_fingerprint_sha256(
    const std::vector<std::vector<int>>& adj,
    const long long m) {
    if (m < 0) throw std::runtime_error("Cannot fingerprint a negative edge count");

    // This logical stream is independent of CGF's physical encoding:
    // "CC-CON-CANONICAL-GRAPH-v1\0", n:u64, m:u64, followed by every
    // lexicographically ordered edge (u:u64, v:u64) with u < v.
    constexpr char prefix[] = "CC-CON-CANONICAL-GRAPH-v1";
    Sha256State hash;
    hash.update(prefix, sizeof(prefix));
    std::array<std::uint8_t, 16> pair{};
    store_u64_le(pair.data(), static_cast<std::uint64_t>(adj.size()));
    store_u64_le(pair.data() + 8, static_cast<std::uint64_t>(m));
    hash.update(pair.data(), pair.size());

    long long observed_m = 0;
    for (std::size_t u = 0; u < adj.size(); ++u) {
        for (const int v : adj[u]) {
            if (u >= static_cast<std::size_t>(v)) continue;
            store_u64_le(pair.data(), static_cast<std::uint64_t>(u));
            store_u64_le(pair.data() + 8, static_cast<std::uint64_t>(v));
            hash.update(pair.data(), pair.size());
            ++observed_m;
        }
    }
    if (observed_m != m) {
        throw std::runtime_error("Canonical graph edge count changed during fingerprinting");
    }
    return sha256_hex(hash.finish());
}

CgfArtifactInfo write_cgf(const std::filesystem::path& path,
                          const std::vector<std::vector<int>>& adj,
                          const long long m,
                          const std::string& graph_fingerprint_sha256) {
    if (m < 0) throw std::runtime_error("CGF cannot store a negative edge count");
    CgfHeader header;
    header.n = static_cast<std::uint64_t>(adj.size());
    header.m = static_cast<std::uint64_t>(m);
    header.adjacency_entries = 2U * header.m;
    header.offsets_offset = kHeaderBytes;
    header.offsets_count = checked_offsets_count(header.n);
    header.payload_offset = checked_payload_offset(header.offsets_count);
    // prepare_graph() has already validated the adjacency lists and computed
    // this fingerprint. Reusing it avoids another full edge traversal before
    // the streaming encoding pass on very large prepared graphs.
    header.graph_fingerprint = parse_sha256_hex(graph_fingerprint_sha256);

    const std::filesystem::path temporary = temporary_artifact_path(path);
    try {
        // Reserve the fixed header and offset table, then keep two sequential
        // file positions: one writes offsets while the other writes payloads.
        // This avoids an additional 8(n+1)-byte in-memory offsets vector.
        std::fstream payload(temporary,
                             std::ios::in | std::ios::out | std::ios::binary |
                                 std::ios::trunc);
        if (!payload) {
            throw std::runtime_error("Unable to open temporary CGF '" +
                                     temporary.string() + "'");
        }
        write_zero_bytes(payload, header.payload_offset);
        payload.flush();
        if (!payload) throw std::runtime_error("Failed while reserving CGF layout");
        payload.seekp(static_cast<std::streamoff>(header.payload_offset));

        std::fstream offsets(temporary,
                             std::ios::in | std::ios::out | std::ios::binary);
        if (!offsets) throw std::runtime_error("Unable to open CGF offset table");
        offsets.seekp(static_cast<std::streamoff>(header.offsets_offset));

        for (const std::vector<int>& neighbors : adj) {
            write_u64_le(offsets, stream_position(payload, "CGF payload position"));
            write_vbyte(payload, static_cast<std::uint64_t>(neighbors.size()));
            std::uint64_t previous = 0;
            for (std::size_t i = 0; i < neighbors.size(); ++i) {
                const std::uint64_t vertex =
                    static_cast<std::uint64_t>(neighbors[i]);
                write_vbyte(payload, i == 0 ? vertex : vertex - previous);
                previous = vertex;
            }
        }
        header.file_bytes = stream_position(payload, "CGF file size");
        write_u64_le(offsets, header.file_bytes);
        header.payload_bytes = header.file_bytes - header.payload_offset;

        payload.close();
        offsets.close();
        if (!payload || !offsets) {
            throw std::runtime_error("Failed while finalizing temporary CGF");
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

    CgfArtifactInfo info;
    info.n = header.n;
    info.m = header.m;
    info.adjacency_entries = header.adjacency_entries;
    info.file_bytes = header.file_bytes;
    info.graph_fingerprint_sha256 = sha256_hex(header.graph_fingerprint);
    info.file_checksum_sha256 = sha256_hex(header.file_checksum);
    return info;
}

namespace {

template <bool FullValidation>
CgfArtifactInfo read_cgf_impl(const std::filesystem::path& path,
                              std::vector<std::vector<int>>& adj) {
    const CgfHeader header = parse_header(path);
    if (header.n > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("CGF graph exceeds int vertex identifiers");
    }
    if (header.m > static_cast<std::uint64_t>(
                       std::numeric_limits<long long>::max())) {
        throw std::runtime_error("CGF edge count exceeds long long");
    }
    const std::uint64_t maximum_edges =
        header.n < 2 ? 0 : header.n * (header.n - 1U) / 2U;
    if (header.m > maximum_edges) {
        throw std::runtime_error("CGF m exceeds the simple-graph maximum");
    }

    if constexpr (FullValidation) {
        const Sha256Digest computed_checksum = sha256_file_with_zero_range(
            path, kChecksumOffset, kChecksumBytes);
        if (computed_checksum != header.file_checksum) {
            throw std::runtime_error(
                "CGF SHA-256 checksum does not match its contents");
        }
    }

    std::ifstream offsets(path, std::ios::binary);
    std::ifstream payload(path, std::ios::binary);
    if (!offsets || !payload) throw std::runtime_error("Unable to reopen CGF");
    offsets.seekg(static_cast<std::streamoff>(header.offsets_offset));
    payload.seekg(static_cast<std::streamoff>(header.payload_offset));

    std::uint64_t record_begin = read_u64_le(offsets, "CGF offset table");
    if (record_begin != header.payload_offset) {
        throw std::runtime_error("CGF first offset does not start the payload");
    }

    adj.assign(static_cast<std::size_t>(header.n), {});
    std::uint64_t decoded_entries = 0;
    for (std::uint64_t u = 0; u < header.n; ++u) {
        const std::uint64_t record_end =
            read_u64_le(offsets, "CGF offset table");
        if (record_end < record_begin || record_end > header.file_bytes) {
            throw std::runtime_error("CGF offsets are not monotone and in range");
        }

        std::uint64_t position = record_begin;
        const std::uint64_t cardinality =
            read_vbyte(payload, position, record_end, "CGF neighborhood");
        if (cardinality > static_cast<std::uint64_t>(
                              std::numeric_limits<std::size_t>::max()) ||
            cardinality > header.n) {
            throw std::runtime_error("CGF neighborhood cardinality is invalid");
        }
        std::vector<int>& neighbors = adj[static_cast<std::size_t>(u)];
        neighbors.reserve(static_cast<std::size_t>(cardinality));

        std::uint64_t previous = 0;
        for (std::uint64_t i = 0; i < cardinality; ++i) {
            const std::uint64_t difference =
                read_vbyte(payload, position, record_end, "CGF neighborhood");
            if constexpr (FullValidation) {
                if (i > 0 && difference == 0) {
                    throw std::runtime_error(
                        "CGF neighborhood contains a duplicate");
                }
            }
            if (difference > std::numeric_limits<std::uint64_t>::max() - previous) {
                throw std::runtime_error("CGF delta decoding overflows");
            }
            const std::uint64_t vertex = previous + difference;
            if (vertex >= header.n) {
                throw std::runtime_error("CGF neighborhood vertex is out of range");
            }
            neighbors.push_back(static_cast<int>(vertex));
            previous = vertex;
        }
        if (position != record_end) {
            throw std::runtime_error("CGF record has bytes after its neighborhood");
        }
        if (decoded_entries >
            std::numeric_limits<std::uint64_t>::max() - cardinality) {
            throw std::runtime_error("CGF adjacency-entry count overflows");
        }
        decoded_entries += cardinality;
        record_begin = record_end;
    }
    if (record_begin != header.file_bytes) {
        throw std::runtime_error("CGF final offset does not end the payload");
    }
    if (decoded_entries != header.adjacency_entries) {
        throw std::runtime_error("CGF decoded adjacency count differs from its header");
    }

    std::string fingerprint = sha256_hex(header.graph_fingerprint);
    if constexpr (FullValidation) {
        validate_canonical_graph(adj);
        if (count_edges(adj) != static_cast<long long>(header.m)) {
            throw std::runtime_error(
                "CGF decoded edge count differs from its header");
        }
        fingerprint = canonical_graph_fingerprint_sha256(
            adj, static_cast<long long>(header.m));
        if (parse_sha256_hex(fingerprint) != header.graph_fingerprint) {
            throw std::runtime_error(
                "CGF graph fingerprint does not match its adjacency lists");
        }
    }

    CgfArtifactInfo info;
    info.n = header.n;
    info.m = header.m;
    info.adjacency_entries = header.adjacency_entries;
    info.file_bytes = header.file_bytes;
    info.graph_fingerprint_sha256 = fingerprint;
    info.file_checksum_sha256 = sha256_hex(header.file_checksum);
    return info;
}

}  // namespace

CgfArtifactInfo read_cgf(const std::filesystem::path& path,
                         std::vector<std::vector<int>>& adj) {
    return read_cgf_impl<false>(path, adj);
}

CgfArtifactInfo read_and_validate_cgf(
    const std::filesystem::path& path,
    std::vector<std::vector<int>>& adj) {
    return read_cgf_impl<true>(path, adj);
}

}  // namespace cc_con
