#ifndef FM_CON_FM_HPP
#define FM_CON_FM_HPP

#include "fm_con/cgf.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace fm_con {

enum class WorkspaceMethod {
    Linear,
    PrefixTree
};

struct FmOptions {
    WorkspaceMethod workspace = WorkspaceMethod::Linear;
    bool fixed_delta = false;
    long double delta = 0.0L;
    std::string delta_argument;
};

struct FmRunStatistics {
    long double delta = 0.0L;
    int initial_k = 0;
    int final_k = 0;

    std::uint64_t initial_cross_edges = 0;
    std::uint64_t edges_in_k_ge_2_bicliques = 0;
    std::uint64_t edges_in_star_bicliques = 0;
    std::uint64_t residual_edges = 0;

    std::uint64_t k_ge_2_bicliques = 0;
    std::uint64_t star_bicliques = 0;
    std::uint64_t center_count = 0;
    std::uint64_t num_parts = 0;

    std::uint64_t nontrivial_size = 0;
    std::uint64_t residual_size = 0;
    std::uint64_t total_size = 0;
    std::uint64_t m_star = 0;
    std::uint64_t n_star = 0;

    std::uint64_t neighborhood_tree_nodes = 0;

    double initialization_seconds = 0.0;
    double k_ge_2_stripping_seconds = 0.0;
    double star_stripping_seconds = 0.0;
    double total_seconds = 0.0;
};

struct FmResult {
    CgfInfo graph;
    WorkspaceMethod workspace = WorkspaceMethod::Linear;
    bool fixed_delta = false;
    std::string delta_argument;
    long double scale_a = 0.0L;
    long double candidate_delta_min = 0.0L;
    long double candidate_delta_max = 0.0L;
    int initial_k_min = 0;
    int initial_k_max = 0;
    std::uint64_t candidate_count = 0;
    bool selected_delta_exists = false;
    double delta_runs_seconds = 0.0;
    double input_index_seconds = 0.0;

    int packed_neighbor_bits = 0;
    std::uint64_t packed_neighbor_words = 0;
    std::uint64_t deleted_edge_words = 0;
    std::uint64_t deletion_rank_entries = 0;

    FmRunStatistics selected;
};

const char* workspace_method_name(WorkspaceMethod method);

FmResult construct_fm(const std::filesystem::path& cgf_path,
                      FmOptions options = {});

}  // namespace fm_con

#endif
