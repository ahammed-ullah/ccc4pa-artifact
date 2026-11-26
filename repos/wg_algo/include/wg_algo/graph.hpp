#pragma once

#include "wg_algo/config.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wg_algo {

// A BVGraph stored by wg_con. The graph and its Elias--Fano index are loaded
// once. Each request decodes one complete neighborhood into a caller-owned
// buffer; no decoded neighborhoods are retained between requests.
class Graph {
public:
    static Graph load(const std::string& graph_path,
                      Decoder decoder = Decoder::Bulk);

    int n() const { return n_; }
    std::uint64_t arcs() const { return arcs_; }
    std::uint64_t m() const { return arcs_ / 2; }
    std::uint64_t graph_bytes() const { return graph_.size(); }
    std::uint64_t properties_bytes() const { return properties_bytes_; }
    std::uint64_t ef_bytes() const { return ef_.size(); }
    const std::string& path() const { return path_; }

    void decode_row(int vertex, std::vector<int>& row) const;

private:
    enum class Code { unary, gamma, delta, zeta3 };
    struct Words {
        std::size_t pos = 0;
        std::size_t count = 0;
    };

    std::string path_;
    int n_ = 0;
    std::uint64_t arcs_ = 0;
    std::uint64_t properties_bytes_ = 0;
    Decoder decoder_ = Decoder::Bulk;
    int window_ = 0;
    int min_interval_ = 0;
    Code outdegree_code_ = Code::gamma;
    Code reference_code_ = Code::unary;
    Code block_code_ = Code::gamma;
    Code interval_code_ = Code::gamma;
    Code residual_code_ = Code::zeta3;
    std::vector<std::uint8_t> graph_;
    std::vector<std::uint8_t> ef_;

    // Views into ef_, not copies of its word arrays.
    std::size_t ef_n_ = 0;
    std::size_t ef_l_ = 0;
    std::size_t high_len_ = 0;
    Words low_words_;
    Words high_words_;
    Words inventory_;
    Words spill_;

    std::uint64_t offset(int vertex) const;
    std::uint64_t read_code(std::uint64_t& bit, Code code) const;
    void decode_row_iterator(int vertex, std::vector<int>& row) const;
    void decode_row_bulk(int vertex, std::vector<int>& row) const;
};

} // namespace wg_algo
