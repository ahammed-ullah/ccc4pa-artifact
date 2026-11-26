#include "ccf_graph.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

namespace zk {
namespace {

constexpr size_t kHeaderBytes = 160;
constexpr array<uint8_t, 8> kMagic = {'C', 'C', 'F', 0, '\r', '\n', 0x1a, '\n'};

[[noreturn]] void invalid(const string& detail) {
    throw runtime_error("invalid CCF: " + detail);
}

// The input mapping is needed only while constructing the cover and incidence
// lists. Mapping avoids a second in-memory copy of the encoded CCF file.
class MappedFile {
public:
    explicit MappedFile(const string& path) {
        const int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) throw system_error(errno, generic_category(), "open " + path);

        struct stat info;
        if (fstat(fd, &info) != 0) {
            const int error = errno;
            close(fd);
            throw system_error(error, generic_category(), "stat " + path);
        }
        if (!S_ISREG(info.st_mode) || info.st_size < static_cast<off_t>(kHeaderBytes) ||
            static_cast<uint64_t>(info.st_size) > numeric_limits<size_t>::max()) {
            close(fd);
            invalid("file is not a regular file with a complete header");
        }

        size_ = static_cast<size_t>(info.st_size);
        void* mapped = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        const int error = errno;
        close(fd);
        if (mapped == MAP_FAILED) {
            throw system_error(error, generic_category(), "map " + path);
        }
        bytes_ = static_cast<const uint8_t*>(mapped);
    }

    ~MappedFile() { if (bytes_) munmap(const_cast<uint8_t*>(bytes_), size_); }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    const uint8_t* bytes() const { return bytes_; }
    size_t size() const { return size_; }

private:
    const uint8_t* bytes_ = nullptr;
    size_t size_ = 0;
};

uint64_t little_endian(const uint8_t* bytes, size_t at, unsigned count) {
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) {
        value |= static_cast<uint64_t>(bytes[at + i]) << (8 * i);
    }
    return value;
}

uint64_t read_vbyte(const uint8_t* bytes, size_t& at, size_t end) {
    uint64_t value = 0;
    unsigned shift = 0;
    while (at < end) {
        const uint8_t byte = bytes[at++];
        const uint64_t low = byte & 127u;
        if (shift == 63 && low > 1) invalid("vbyte exceeds 64 bits");
        value |= low << shift;
        if ((byte & 128u) == 0) {
            if (shift != 0 && low == 0) invalid("vbyte is not shortest-form");
            return value;
        }
        if (shift >= 63) invalid("vbyte is overlong");
        shift += 7;
    }
    invalid("clique ends inside a vbyte integer");
}

string hex_fingerprint(const uint8_t* bytes) {
    constexpr char digits[] = "0123456789abcdef";
    string result;
    result.reserve(64);
    for (size_t i = 96; i < 128; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 15]);
    }
    return result;
}

struct HeapEntry {
    uint32_t vertex;
    uint32_t clique;
    size_t position;
};

struct EarliestVertex {
    bool operator()(const HeapEntry& a, const HeapEntry& b) const {
        if (a.vertex != b.vertex) return a.vertex > b.vertex;
        return a.clique > b.clique;
    }
};

}  // namespace

CcfGraph::CcfGraph(const string& ccf_path, const string& perm_path) {
    const MappedFile file(ccf_path);
    const uint8_t* bytes = file.bytes();
    if (!equal(kMagic.begin(), kMagic.end(), bytes)) invalid("magic");
    if (little_endian(bytes, 8, 4) != 1 ||
        little_endian(bytes, 12, 4) != kHeaderBytes ||
        little_endian(bytes, 16, 4) != 1 ||
        little_endian(bytes, 20, 4) != 0) {
        invalid("version, header size, encoding, or flags");
    }

    const uint64_t n = little_endian(bytes, 24, 8);
    m_ = little_endian(bytes, 32, 8);
    const uint64_t records = little_endian(bytes, 40, 8);
    const uint64_t cover_size = little_endian(bytes, 48, 8);
    const uint64_t offsets_start = little_endian(bytes, 56, 8);
    const uint64_t offsets_count = little_endian(bytes, 64, 8);
    const uint64_t payload_start = little_endian(bytes, 72, 8);
    const uint64_t payload_bytes = little_endian(bytes, 80, 8);
    const uint64_t file_bytes = little_endian(bytes, 88, 8);

    if (n > INT_MAX || records > INT_MAX) invalid("32-bit vertex or clique limit");
    n_ = static_cast<uint32_t>(n);
    if (m_ > (n == 0 ? 0 : n * (n - 1) / 2)) {
        invalid("edge count exceeds simple-graph maximum");
    }
    if (offsets_start != kHeaderBytes || offsets_count != records + 1 ||
        payload_start != kHeaderBytes + 8 * offsets_count ||
        payload_start > file.size() ||
        payload_bytes != file.size() - payload_start ||
        file_bytes != file.size()) {
        invalid("header, offset table, or file length");
    }
    if (cover_size > payload_bytes || cover_size > numeric_limits<size_t>::max()) {
        invalid("cover size exceeds payload or address space");
    }
    fingerprint_ = hex_fingerprint(bytes);

    // A saved WebGraph order is a plain little-endian uint32 array:
    // encoded ID -> original CCF ID. Invert it once to relabel each clique.
    vector<uint32_t> old_to_new;
    if (!perm_path.empty()) {
        ifstream perm(perm_path, ios::binary | ios::ate);
        if (!perm) throw runtime_error("cannot open permutation: " + perm_path);
        if (perm.tellg() != static_cast<streamoff>(4 * n)) {
            invalid("permutation has the wrong byte length");
        }
        perm.seekg(0);
        new_to_old_.resize(n_);
        old_to_new.assign(n_, UINT32_MAX);
        array<char, 65536> chunk;
        size_t rank = 0;
        while (rank < n_) {
            const size_t count = min<size_t>(n_ - rank, chunk.size() / 4);
            perm.read(chunk.data(), static_cast<streamsize>(4 * count));
            if (!perm) throw runtime_error("cannot read permutation: " + perm_path);
            for (size_t i = 0; i < count; ++i) {
                const uint8_t* raw = reinterpret_cast<const uint8_t*>(chunk.data() + 4 * i);
                const uint32_t old = static_cast<uint32_t>(little_endian(raw, 0, 4));
                if (old >= n_ || old_to_new[old] != UINT32_MAX) {
                    invalid("permutation is not a bijection");
                }
                new_to_old_[rank + i] = old;
                old_to_new[old] = static_cast<uint32_t>(rank + i);
            }
            rank += count;
        }
    }

    vertices_.reserve(static_cast<size_t>(cover_size));
    clique_offsets_.reserve(static_cast<size_t>(records) + 1);
    incidence_offsets_.assign(static_cast<size_t>(n_) + 1, 0);

    uint64_t begin = little_endian(bytes, kHeaderBytes, 8);
    if (begin != payload_start) invalid("first clique offset");
    for (uint32_t c = 0; c < records; ++c) {
        const uint64_t end = little_endian(bytes, kHeaderBytes + 8 * (static_cast<size_t>(c) + 1), 8);
        if (end <= begin || end > file.size()) invalid("clique offsets are not increasing");

        size_t at = static_cast<size_t>(begin);
        const uint64_t cardinality = read_vbyte(bytes, at, static_cast<size_t>(end));
        if (cardinality < 2 || cardinality > n ||
            cardinality > cover_size - vertices_.size()) {
            invalid("clique cardinality");
        }

        const size_t first = vertices_.size();
        clique_offsets_.push_back(first);
        uint64_t previous = 0;
        for (uint64_t i = 0; i < cardinality; ++i) {
            const uint64_t gap = read_vbyte(bytes, at, static_cast<size_t>(end));
            if (i != 0 && gap == 0) invalid("duplicate vertex in clique");
            if (gap >= n - previous) invalid("clique vertex out of range");
            previous += gap;
            const uint32_t old = static_cast<uint32_t>(previous);
            vertices_.push_back(perm_path.empty() ? old : old_to_new[old]);
        }
        if (at != end) invalid("trailing bytes in clique record");
        if (!perm_path.empty()) {
            sort(vertices_.begin() + first, vertices_.end());
        }
        for (size_t i = first; i < vertices_.size(); ++i) {
            ++incidence_offsets_[static_cast<size_t>(vertices_[i]) + 1];
        }
        begin = end;
    }
    if (begin != file.size() || vertices_.size() != cover_size) {
        invalid("final offset or decoded cover size");
    }
    clique_offsets_.push_back(vertices_.size());

    for (size_t u = 0; u < n_; ++u) {
        incidence_offsets_[u + 1] += incidence_offsets_[u];
    }
    vector<size_t> next(incidence_offsets_.begin(), incidence_offsets_.end() - 1);
    incidence_.resize(vertices_.size());
    for (uint32_t c = 0; c < records; ++c) {
        for (size_t i = clique_offsets_[c]; i < clique_offsets_[c + 1]; ++i) {
            const uint32_t v = vertices_[i];
            incidence_[next[v]++] = c;
        }
    }
}

void CcfGraph::neighbors(uint32_t u, vector<uint32_t>& out) const {
    if (u >= n_) throw out_of_range("CCF vertex out of range");
    out.clear();
    const size_t first = incidence_offsets_[u];
    const size_t end = incidence_offsets_[static_cast<size_t>(u) + 1];
    if (first == end) return;

    if (end == first + 1) {
        const uint32_t c = incidence_[first];
        for (size_t i = clique_offsets_[c]; i < clique_offsets_[c + 1]; ++i) {
            if (vertices_[i] != u) out.push_back(vertices_[i]);
        }
        return;
    }

    // Each incident clique is sorted. Merge them, removing shared edges.
    priority_queue<HeapEntry, vector<HeapEntry>, EarliestVertex> heap;
    for (size_t i = first; i < end; ++i) {
        const uint32_t c = incidence_[i];
        const size_t position = clique_offsets_[c];
        heap.push({vertices_[position], c, position});
    }
    uint32_t previous = UINT32_MAX;
    while (!heap.empty()) {
        const HeapEntry item = heap.top();
        heap.pop();
        if (item.vertex != u && item.vertex != previous) {
            out.push_back(item.vertex);
            previous = item.vertex;
        }
        const size_t next = item.position + 1;
        if (next < clique_offsets_[static_cast<size_t>(item.clique) + 1]) {
            heap.push({vertices_[next], item.clique, next});
        }
    }
}

}  // namespace zk
