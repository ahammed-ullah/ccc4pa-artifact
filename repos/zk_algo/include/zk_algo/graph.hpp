#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace zk_algo {

// A graph written by zk_con. Both compressed files are loaded once. An index
// entry is read directly from the packed .zki data for each requested row;
// decoded neighborhoods are never kept between calls.
class Graph {
public:
    static Graph load(const std::string& zk_path);

    int n() const { return n_; }
    std::uint64_t arcs() const { return arcs_; }
    std::uint64_t m() const { return arcs_ / 2; }
    std::uint64_t graph_bytes() const { return graph_.size(); }
    std::uint64_t index_bytes() const { return index_.size(); }
    const std::string& path() const { return path_; }

    // Decode one complete, sorted neighborhood. Reference chains are followed
    // afresh for every call; row is the only buffer that survives the call.
    void decode_row(int vertex, std::vector<int>& row) const;

private:
    std::string path_;
    int n_ = 0;
    std::uint64_t arcs_ = 0;
    std::uint8_t offset_width_ = 0;
    std::vector<std::uint8_t> graph_;
    std::vector<std::uint8_t> index_;

    // Each entry packs a Huffman symbol in the low byte and its code length
    // in the high byte. An absent code has length zero. These are fixed-size
    // decoding tables, independent of the graph's number of vertices.
    std::vector<std::array<std::uint16_t, 256>> huffman_;

    std::uint64_t offset(int vertex) const;
};

} // namespace zk_algo
