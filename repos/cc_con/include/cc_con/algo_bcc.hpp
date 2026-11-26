#ifndef CC_CON_ALGO_BCC_HPP
#define CC_CON_ALGO_BCC_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cc_con {

// One clique R union Z emitted by CrossCover. The two sorted sides are retained
// separately because R x Z is exactly the set of graph edges owned by this
// clique. Direct BCC merges the sides into one output vector; CPA uses the
// separation to find the endpoints of still-uncovered owned edges.
struct CrossCoverClique {
    std::vector<int> R;
    std::vector<int> Z;
};

// Arrays shared by direct BCC and CPA after the initial complement coloring.
// Part p consists of
//
//   part_vertices[part_begin[p] .. part_begin[p+1]).
//
// Each record is one inter-color graph edge encoded so integer sorting order
// is exactly (part pair, CrossCover source, CrossCover target) order. The
// records are decoded one nonempty part pair at a time; no hash table or full
// copy of the CrossCover family is required.
struct BccCrossCoverData {
    std::vector<int> part_size;
    std::vector<std::size_t> part_begin;
    std::vector<int> part_vertices;
    std::vector<std::uint64_t> pair_row_begin;
    std::vector<std::uint64_t> records;
};

// Structural quantities produced while constructing the BCC family. Generic
// cover statistics such as |C| and sigma(C) are calculated by main.cpp.
struct BccStatistics {
    long long mu = 0;
    long long part_size_limit = 0;
    bool splitting_applied = false;
    long long complement_degeneracy = 0;
    long long num_complement_colors = 0;
    long long num_parts = 0;
};

// Balanced Cross-Cover construction. adj is the canonical graph: simple,
// undirected, without isolated vertices, and stored as sorted, duplicate-free
// open adjacency lists. m is calculated before this timed call.
void construct_bcc(const std::vector<std::vector<int>>& adj,
                   long long m,
                   std::vector<std::vector<int>>& C,
                   BccStatistics& statistics);

// Prepare the shared BCC family. F is always the original
// complement-degeneracy coloring and is used as the initial cover. The parts in
// data define only the CrossCover calls. They follow the paper's split unless
// the implementation can omit it while retaining the same asymptotic bound.
void prepare_bcc_cross_cover(
    const std::vector<std::vector<int>>& adj,
    long long m,
    std::vector<std::vector<int>>& F,
    BccCrossCoverData& data,
    BccStatistics& statistics);

// Construct all CrossCover cliques for the part pair containing
// data.records[record_begin]. The returned position begins the next nonempty
// part pair. pair_cover is cleared first and owns only the current pair.
std::size_t construct_next_cross_cover_pair(
    const BccCrossCoverData& data,
    std::size_t record_begin,
    std::vector<CrossCoverClique>& pair_cover);

}  // namespace cc_con

#endif
