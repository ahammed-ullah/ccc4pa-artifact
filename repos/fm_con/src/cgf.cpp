#include "fm_con/cgf.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace std;

namespace fm_con {
namespace {

constexpr array<uint8_t, 8> cgf_magic{
    'C', 'G', 'F', 0, '\r', '\n', 0x1aU, '\n'};
constexpr uint32_t header_bytes = 160;
constexpr uint32_t delta_vbyte_encoding = 1;

uint32_t load_u32_le(const uint8_t* source) {
    uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte)
        value |= (uint32_t)source[byte] << (8U * byte);
    return value;
}

uint64_t load_u64_le(const uint8_t* source) {
    uint64_t value = 0;
    for (unsigned byte = 0; byte < 8; ++byte)
        value |= (uint64_t)source[byte] << (8U * byte);
    return value;
}

uint64_t read_u64_le(istream& input, const string& context) {
    array<uint8_t, 8> bytes{};
    input.read((char*)bytes.data(), (streamsize)bytes.size());
    if (!input) throw runtime_error("Truncated " + context);
    return load_u64_le(bytes.data());
}

string hexadecimal(const uint8_t* bytes, int count) {
    ostringstream output;
    output << hex << setfill('0');
    for (int i = 0; i < count; ++i)
        output << setw(2) << (unsigned)bytes[i];
    return output.str();
}

streamoff file_position(uint64_t position, const string& context) {
    if (position > (uint64_t)numeric_limits<streamoff>::max())
        throw runtime_error(context + " exceeds the stream position range");
    return (streamoff)position;
}

uint64_t read_vbyte(istream& input,
                    uint64_t& position,
                    uint64_t end,
                    const string& context) {
    uint64_t value = 0;
    unsigned shift = 0;

    while (position < end) {
        int raw = input.get();
        if (raw == char_traits<char>::eof())
            throw runtime_error("Truncated " + context);

        ++position;
        uint8_t byte = (uint8_t)raw;
        uint64_t low = byte & 127U;
        if (shift == 63 && low > 1U)
            throw runtime_error("Overflowing vbyte integer in " + context);

        value |= low << shift;
        if ((byte & 128U) == 0) {
            if (shift > 0 && low == 0)
                throw runtime_error("Noncanonical vbyte integer in " + context);
            return value;
        }
        if (shift >= 63)
            throw runtime_error("Overlong vbyte integer in " + context);
        shift += 7;
    }
    throw runtime_error("CGF record ends inside a vbyte integer");
}

uint64_t next_record_end(CgfReader& reader) {
    if (reader.next_vertex >= (int)reader.info->n)
        throw runtime_error("Attempted to read beyond the CGF neighborhood records");

    uint64_t end = read_u64_le(reader.offsets, "CGF offset table");
    if (end < reader.record_begin || end > reader.info->file_bytes)
        throw runtime_error("CGF offsets are not monotone and in range");

    reader.payload.seekg(file_position(reader.record_begin, "CGF record offset"));
    if (!reader.payload) throw runtime_error("Unable to seek to a CGF neighborhood");
    return end;
}

uint64_t begin_record(CgfReader& reader,
                      uint64_t record_end,
                      uint64_t& position) {
    position = reader.record_begin;
    uint64_t cardinality = read_vbyte(
        reader.payload, position, record_end, "CGF neighborhood cardinality");
    if (cardinality > reader.info->n ||
        cardinality > (uint64_t)numeric_limits<uint32_t>::max()) {
        throw runtime_error("CGF neighborhood cardinality is invalid");
    }
    return cardinality;
}

void complete_record(CgfReader& reader,
                     uint64_t record_end,
                     uint64_t cardinality) {
    if (reader.decoded_entries >
        numeric_limits<uint64_t>::max() - cardinality) {
        throw runtime_error("CGF adjacency-entry count overflows");
    }
    reader.decoded_entries += cardinality;
    reader.record_begin = record_end;
    ++reader.next_vertex;
}

}  // namespace

CgfInfo read_cgf_info(const filesystem::path& path) {
    ifstream input(path, ios::binary);
    if (!input)
        throw runtime_error("Unable to open CGF file '" + path.string() + "'");

    array<uint8_t, header_bytes> bytes{};
    input.read((char*)bytes.data(), (streamsize)bytes.size());
    if (!input) throw runtime_error("CGF file is smaller than its header");

    if (!equal(cgf_magic.begin(), cgf_magic.end(), bytes.begin()))
        throw runtime_error("CGF magic is invalid");
    if (load_u32_le(bytes.data() + 8) != kCgfFormatVersion)
        throw runtime_error("Unsupported CGF format version");
    if (load_u32_le(bytes.data() + 12) != header_bytes)
        throw runtime_error("Unsupported CGF header size");
    if (load_u32_le(bytes.data() + 16) != delta_vbyte_encoding)
        throw runtime_error("Unsupported CGF neighborhood encoding");
    if (load_u32_le(bytes.data() + 20) != 0)
        throw runtime_error("CGF uses unsupported flags");

    CgfInfo info;
    info.path = filesystem::absolute(path);
    info.n = load_u64_le(bytes.data() + 24);
    info.m = load_u64_le(bytes.data() + 32);
    uint64_t records = load_u64_le(bytes.data() + 40);
    info.adjacency_entries = load_u64_le(bytes.data() + 48);
    info.offsets_offset = load_u64_le(bytes.data() + 56);
    uint64_t offsets_count = load_u64_le(bytes.data() + 64);
    info.payload_offset = load_u64_le(bytes.data() + 72);
    uint64_t payload_bytes = load_u64_le(bytes.data() + 80);
    info.file_bytes = load_u64_le(bytes.data() + 88);
    info.graph_fingerprint_sha256 = hexadecimal(bytes.data() + 96, 32);
    info.file_checksum_sha256 = hexadecimal(bytes.data() + 128, 32);

    if (info.n > (uint64_t)numeric_limits<int>::max())
        throw runtime_error("CGF graph exceeds int vertex identifiers");
    if (records != info.n)
        throw runtime_error("CGF record count does not equal n");
    if (info.n == numeric_limits<uint64_t>::max() ||
        offsets_count != info.n + 1) {
        throw runtime_error("CGF offset count does not equal n+1");
    }
    if (info.offsets_offset != header_bytes ||
        info.n + 1 > (numeric_limits<uint64_t>::max() - header_bytes) / 8U ||
        info.payload_offset != header_bytes + 8U * (info.n + 1)) {
        throw runtime_error("CGF offset-table layout is inconsistent");
    }
    if (payload_bytes > numeric_limits<uint64_t>::max() - info.payload_offset ||
        info.file_bytes != info.payload_offset + payload_bytes) {
        throw runtime_error("CGF payload length is inconsistent");
    }
    if (info.m > numeric_limits<uint64_t>::max() / 2U ||
        info.adjacency_entries != 2U * info.m) {
        throw runtime_error("CGF adjacency-entry count is inconsistent with m");
    }

    uint64_t maximum_edges =
        info.n < 2 ? 0 : info.n * (info.n - 1U) / 2U;
    if (info.m > maximum_edges)
        throw runtime_error("CGF m exceeds the simple-graph maximum");

    error_code error;
    uintmax_t physical_size = filesystem::file_size(path, error);
    if (error)
        throw runtime_error("Unable to inspect CGF size: " + error.message());
    if (physical_size != info.file_bytes)
        throw runtime_error("CGF physical size differs from its header");

    file_position(info.file_bytes, "CGF file size");
    return info;
}

CgfReader open_cgf(const CgfInfo& info) {
    CgfReader reader;
    reader.info = &info;
    reader.offsets.open(info.path, ios::binary);
    reader.payload.open(info.path, ios::binary);
    if (!reader.offsets || !reader.payload)
        throw runtime_error("Unable to reopen CGF file '" + info.path.string() + "'");

    reader.offsets.seekg(file_position(info.offsets_offset, "CGF offset table"));
    reader.record_begin = read_u64_le(reader.offsets, "CGF offset table");
    if (reader.record_begin != info.payload_offset)
        throw runtime_error("CGF first offset does not start the payload");
    return reader;
}

uint32_t read_cgf_degree(CgfReader& reader) {
    uint64_t record_end = next_record_end(reader);
    uint64_t position;
    uint64_t cardinality = begin_record(reader, record_end, position);

    // This path deliberately avoids decoding IDs. Under the required canonical
    // CGF input contract, the row cardinality is the right degree in the directed
    // bipartite representation of an undirected graph.
    reader.payload.seekg(file_position(record_end, "CGF record end"));
    if (!reader.payload) throw runtime_error("Unable to skip a CGF neighborhood");
    complete_record(reader, record_end, cardinality);
    return (uint32_t)cardinality;
}

void read_cgf_neighborhood(CgfReader& reader, vector<int>& neighbors) {
    uint64_t record_end = next_record_end(reader);
    uint64_t position;
    uint64_t cardinality = begin_record(reader, record_end, position);

    neighbors.clear();
    neighbors.reserve((size_t)cardinality);
    uint64_t previous = 0;
    for (uint64_t i = 0; i < cardinality; ++i) {
        uint64_t difference = read_vbyte(
            reader.payload, position, record_end, "CGF neighborhood");
        if (difference > numeric_limits<uint64_t>::max() - previous)
            throw runtime_error("CGF delta decoding overflows");
        uint64_t vertex = previous + difference;
        if (vertex >= reader.info->n)
            throw runtime_error("CGF neighborhood vertex is out of range");
        neighbors.push_back((int)vertex);
        previous = vertex;
    }
    if (position != record_end)
        throw runtime_error("CGF record has bytes after its neighborhood");

    complete_record(reader, record_end, cardinality);
}

void finish_cgf_pass(CgfReader& reader) {
    if (reader.next_vertex != (int)reader.info->n)
        throw runtime_error("CGF pass ended before all neighborhoods were read");
    if (reader.record_begin != reader.info->file_bytes)
        throw runtime_error("CGF final offset does not end the payload");
    if (reader.decoded_entries != reader.info->adjacency_entries)
        throw runtime_error("CGF decoded adjacency count differs from its header");
}

}  // namespace fm_con
