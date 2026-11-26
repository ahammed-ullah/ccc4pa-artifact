#include "cc_con/graph.hpp"

#include "cornell_projection_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cc_con {
namespace {

void record_native_statistics(
    const std::int64_t r,
    const std::int64_t r_nontrivial,
    const std::int64_t incidence_sum,
    const std::int64_t incidence_sum_nontrivial,
    const std::int64_t minimum_size,
    const std::int64_t maximum_size,
    const std::int64_t pair_occurrences,
    const std::int64_t distinct_edges,
    GraphMetadata& metadata) {
    auto& statistics = metadata.generation_statistics;
    statistics["native_statistics_available"] = true;
    statistics["r"] = r;
    statistics["r_nontrivial"] = r_nontrivial;
    statistics["incidence_sum"] = incidence_sum;
    statistics["incidence_sum_nontrivial"] = incidence_sum_nontrivial;
    statistics["pair_occurrences"] = pair_occurrences;
    statistics["distinct_edges"] = distinct_edges;
    statistics["native_cover_size"] = incidence_sum_nontrivial;

    if (r == 0) {
        statistics["min_hyperedge_size"] = std::monostate{};
        statistics["max_hyperedge_size"] = std::monostate{};
        statistics["mean_hyperedge_size"] = std::monostate{};
    } else {
        statistics["min_hyperedge_size"] = minimum_size;
        statistics["max_hyperedge_size"] = maximum_size;
        statistics["mean_hyperedge_size"] =
            static_cast<double>(incidence_sum) / static_cast<double>(r);
    }

    if (incidence_sum_nontrivial == 0) {
        statistics["native_CR"] = std::monostate{};
    } else {
        statistics["native_CR"] =
            2.0 * static_cast<double>(distinct_edges) /
            static_cast<double>(incidence_sum_nontrivial);
    }
    if (distinct_edges == 0) {
        statistics["overlap_factor"] = std::monostate{};
    } else {
        statistics["overlap_factor"] =
            static_cast<double>(pair_occurrences) /
            static_cast<double>(distinct_edges);
    }
}

}  // namespace

void load_cornell_simplices_graph(const Config& config,
                                  const std::string& configured_name,
                                  std::vector<std::vector<int>>& adj,
                                  GraphMetadata& metadata) {
    const std::filesystem::path nverts_path = config.resolve_path(
        config.require_string("graph.nverts_path"));
    const std::filesystem::path simplices_path = config.resolve_path(
        config.require_string("graph.simplices_path"));

    std::ifstream sizes_input(nverts_path);
    if (!sizes_input) {
        throw std::runtime_error(
            "Unable to open Cornell simplex-size file '" +
            nverts_path.string() + "'");
    }
    std::ifstream members_input(simplices_path);
    if (!members_input) {
        throw std::runtime_error(
            "Unable to open Cornell simplex-member file '" +
            simplices_path.string() + "'");
    }

    metadata.name = configured_name;
    metadata.source = "file";
    metadata.type = "cornell-simplices";
    metadata.parameters["nverts_path"] = nverts_path.string();
    metadata.parameters["simplices_path"] = simplices_path.string();

    cornell_detail::PackedEdgeSet projected_edges;
    cornell_detail::ProjectionProgress progress(
        configured_name, "projecting native simplices");

    std::int64_t r = 0;
    std::int64_t r_nontrivial = 0;
    std::int64_t incidence_sum = 0;
    std::int64_t incidence_sum_nontrivial = 0;
    std::int64_t pair_occurrences = 0;
    std::int64_t minimum_size = std::numeric_limits<std::int64_t>::max();
    std::int64_t maximum_size = 0;
    int maximum_external_id = -1;

    std::vector<int> members;
    long long raw_size = 0;
    std::uint64_t pairs_until_progress_check = 0;
    while (sizes_input >> raw_size) {
        if (raw_size < 0 || raw_size > std::numeric_limits<int>::max()) {
            throw std::runtime_error(
                "Simplex size " + std::to_string(raw_size) +
                " at record " + std::to_string(r + 1) +
                " is outside [0, INT_MAX]");
        }

        cornell_detail::add_to_counter(r, 1, "Simplex count");
        members.clear();
        members.reserve(static_cast<std::size_t>(raw_size));
        for (long long offset = 0; offset < raw_size; ++offset) {
            long long external_id = 0;
            if (!(members_input >> external_id)) {
                throw std::runtime_error(
                    "Cornell simplex-member file ended or became malformed "
                    "while reading simplex " + std::to_string(r));
            }
            const int vertex = cornell_detail::checked_external_id(
                external_id, "simplex " + std::to_string(r));
            members.push_back(vertex);
            maximum_external_id = std::max(maximum_external_id, vertex);
        }

        // Cornell simplices are conceptually sets. Sorting one current record is
        // enough to remove accidental repeated IDs before every statistic and
        // pair calculation below.
        std::sort(members.begin(), members.end());
        members.erase(std::unique(members.begin(), members.end()), members.end());
        const std::size_t size = members.size();
        cornell_detail::add_to_counter(
            incidence_sum, static_cast<std::uint64_t>(size), "Incidence sum");
        minimum_size = std::min(
            minimum_size, static_cast<std::int64_t>(size));
        maximum_size = std::max(
            maximum_size, static_cast<std::int64_t>(size));

        if (size >= 2) {
            cornell_detail::add_to_counter(
                r_nontrivial, 1, "Nontrivial simplex count");
            cornell_detail::add_to_counter(
                incidence_sum_nontrivial,
                static_cast<std::uint64_t>(size),
                "Nontrivial incidence sum");
        }

        const std::uint64_t pairs = cornell_detail::choose_two(size);
        cornell_detail::add_to_counter(
            pair_occurrences, pairs, "Pair-occurrence count");

        // Reserving for the first nontrivial simplex avoids repeated hash-table
        // growth when a single large simplex creates most of the graph. Later
        // overlap is unknown, so subsequent records grow the table normally.
        if (projected_edges.empty() && pairs > 0) {
            projected_edges.reserve_for_edges(pairs);
        }
        for (std::size_t first = 0; first < size; ++first) {
            for (std::size_t second = first + 1; second < size; ++second) {
                projected_edges.insert(
                    cornell_detail::edge_key(members[first], members[second]));
                if (++pairs_until_progress_check == (UINT64_C(1) << 20)) {
                    pairs_until_progress_check = 0;
                    progress.maybe_report(
                        r, pair_occurrences, projected_edges.size());
                }
            }
        }
        progress.maybe_report(r, pair_occurrences, projected_edges.size());
    }

    if (!sizes_input.eof()) {
        throw std::runtime_error(
            "Malformed value in Cornell simplex-size file '" +
            nverts_path.string() + "'");
    }
    long long extra_member = 0;
    if (members_input >> extra_member) {
        throw std::runtime_error(
            "Cornell simplex-member file contains more vertex IDs than the "
            "simplex-size file describes");
    }
    if (!members_input.eof()) {
        throw std::runtime_error(
            "Malformed value at the end of Cornell simplex-member file '" +
            simplices_path.string() + "'");
    }

    if (projected_edges.size() > static_cast<std::size_t>(
                                     std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error("Distinct edge count exceeds signed 64-bit range");
    }
    const std::int64_t distinct_edges =
        static_cast<std::int64_t>(projected_edges.size());
    record_native_statistics(
        r, r_nontrivial, incidence_sum, incidence_sum_nontrivial,
        minimum_size, maximum_size, pair_occurrences, distinct_edges,
        metadata);

    cornell_detail::build_adjacency_and_release_edges(
        projected_edges, maximum_external_id, adj);
}

}  // namespace cc_con
