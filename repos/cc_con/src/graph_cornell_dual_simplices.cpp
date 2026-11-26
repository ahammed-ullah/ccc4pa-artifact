#include "cc_con/graph.hpp"

#include "cornell_projection_support.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cc_con {
namespace {

void record_dual_statistics(
    const std::int64_t original_vertex_count,
    const std::int64_t original_simplex_count,
    const std::int64_t dual_nontrivial_hyperedge_count,
    const std::int64_t dual_incidence_sum,
    const std::int64_t dual_incidence_sum_nontrivial,
    const std::int64_t minimum_dual_hyperedge_size,
    const std::int64_t maximum_dual_hyperedge_size,
    const std::int64_t dual_pair_occurrences,
    const std::int64_t n_dual,
    const std::int64_t m_dual,
    GraphMetadata& metadata) {
    auto& statistics = metadata.generation_statistics;
    statistics["dual_native_statistics_available"] = true;
    statistics["original_vertex_count"] = original_vertex_count;
    statistics["original_simplex_count"] = original_simplex_count;

    // Every represented original vertex v contributes one dual hyperedge D_v.
    // Cornell's files do not separately describe vertices with no incidences,
    // so these two counts are necessarily equal for this input format.
    statistics["dual_hyperedge_count"] = original_vertex_count;
    statistics["dual_nontrivial_hyperedge_count"] =
        dual_nontrivial_hyperedge_count;
    statistics["dual_incidence_sum"] = dual_incidence_sum;
    statistics["dual_incidence_sum_nontrivial"] =
        dual_incidence_sum_nontrivial;
    statistics["dual_pair_occurrences"] = dual_pair_occurrences;
    statistics["dual_native_cover_size"] =
        dual_incidence_sum_nontrivial;

    if (original_vertex_count == 0) {
        statistics["min_dual_hyperedge_size"] = std::monostate{};
        statistics["max_dual_hyperedge_size"] = std::monostate{};
        statistics["mean_dual_hyperedge_size"] = std::monostate{};
    } else {
        statistics["min_dual_hyperedge_size"] =
            minimum_dual_hyperedge_size;
        statistics["max_dual_hyperedge_size"] =
            maximum_dual_hyperedge_size;
        statistics["mean_dual_hyperedge_size"] =
            static_cast<double>(dual_incidence_sum) /
            static_cast<double>(original_vertex_count);
    }

    if (dual_incidence_sum_nontrivial == 0) {
        statistics["dual_native_CR"] = std::monostate{};
    } else {
        statistics["dual_native_CR"] =
            2.0 * static_cast<double>(m_dual) /
            static_cast<double>(dual_incidence_sum_nontrivial);
    }
    if (m_dual == 0) {
        statistics["dual_overlap_factor"] = std::monostate{};
    } else {
        statistics["dual_overlap_factor"] =
            static_cast<double>(dual_pair_occurrences) /
            static_cast<double>(m_dual);
    }

    // n_dual is the canonical graph order. Input events that share no original
    // vertex with another event are original-entity projection isolates and are
    // removed exactly as they are for every other graph source.
    statistics["n_dual"] = n_dual;
    statistics["m_dual"] = m_dual;
    statistics["dual_isolated_vertices_removed"] =
        original_simplex_count - n_dual;
}

}  // namespace

void load_cornell_dual_simplices_graph(
    const Config& config,
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
    metadata.type = "cornell-dual-simplices";
    metadata.parameters["nverts_path"] = nverts_path.string();
    metadata.parameters["simplices_path"] = simplices_path.string();

    // dual_hyperedges[v] is D_v: the increasing list of input-event IDs that
    // contain original vertex v. Raw Cornell IDs index this temporary outer
    // vector. Empty positions represent gaps in the external ID space and are
    // not counted as original vertices or dual hyperedges.
    std::vector<std::vector<int>> dual_hyperedges;
    std::vector<int> members;
    std::int64_t original_simplex_count = 0;
    std::int64_t dual_incidence_sum = 0;

    auto next_read_message =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    long long raw_size = 0;
    while (sizes_input >> raw_size) {
        if (raw_size < 0 || raw_size > std::numeric_limits<int>::max()) {
            throw std::runtime_error(
                "Simplex size " + std::to_string(raw_size) +
                " at record " +
                std::to_string(original_simplex_count + 1) +
                " is outside [0, INT_MAX]");
        }
        if (original_simplex_count >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(
                "Cornell dual projection has too many input events for int "
                "graph identifiers");
        }

        // The input position is the temporary dual vertex ID. Identical events
        // deliberately receive different IDs and are never merged.
        const int event_id = static_cast<int>(original_simplex_count);
        cornell_detail::add_to_counter(
            original_simplex_count, 1, "Original simplex count");

        members.clear();
        members.reserve(static_cast<std::size_t>(raw_size));
        for (long long offset = 0; offset < raw_size; ++offset) {
            long long external_id = 0;
            if (!(members_input >> external_id)) {
                throw std::runtime_error(
                    "Cornell simplex-member file ended or became malformed "
                    "while reading simplex " +
                    std::to_string(original_simplex_count));
            }
            members.push_back(cornell_detail::checked_external_id(
                external_id,
                "simplex " + std::to_string(original_simplex_count)));
        }

        // A malformed repeated member denotes one incidence, not several. The
        // sort also makes appending one event ID to each affected D_v direct.
        std::sort(members.begin(), members.end());
        members.erase(std::unique(members.begin(), members.end()), members.end());
        cornell_detail::add_to_counter(
            dual_incidence_sum,
            static_cast<std::uint64_t>(members.size()),
            "Dual incidence sum");

        for (const int vertex : members) {
            if (dual_hyperedges.size() <= static_cast<std::size_t>(vertex)) {
                dual_hyperedges.resize(static_cast<std::size_t>(vertex) + 1);
            }
            dual_hyperedges[static_cast<std::size_t>(vertex)].push_back(event_id);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= next_read_message) {
            std::cerr << "cc_con: " << configured_name
                      << ": building incidence dual: simplices="
                      << original_simplex_count
                      << ", incidences=" << dual_incidence_sum << '\n';
            next_read_message = now + std::chrono::seconds(10);
        }
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

    // Project the incidence dual. D_v lists are already sorted and unique: input
    // event IDs increase monotonically, and each event appended itself once
    // after its member list was deduplicated above.
    cornell_detail::PackedEdgeSet projected_edges;
    cornell_detail::ProjectionProgress projection_progress(
        configured_name, "projecting incidence dual");
    std::int64_t original_vertex_count = 0;
    std::int64_t dual_nontrivial_hyperedge_count = 0;
    std::int64_t dual_incidence_sum_nontrivial = 0;
    std::int64_t dual_pair_occurrences = 0;
    std::int64_t minimum_dual_hyperedge_size =
        std::numeric_limits<std::int64_t>::max();
    std::int64_t maximum_dual_hyperedge_size = 0;
    std::uint64_t pairs_until_progress_check = 0;

    for (std::vector<int>& events : dual_hyperedges) {
        if (events.empty()) continue;

        cornell_detail::add_to_counter(
            original_vertex_count, 1, "Original vertex count");
        const std::size_t size = events.size();
        minimum_dual_hyperedge_size = std::min(
            minimum_dual_hyperedge_size,
            static_cast<std::int64_t>(size));
        maximum_dual_hyperedge_size = std::max(
            maximum_dual_hyperedge_size,
            static_cast<std::int64_t>(size));

        if (size >= 2) {
            cornell_detail::add_to_counter(
                dual_nontrivial_hyperedge_count, 1,
                "Nontrivial dual hyperedge count");
            cornell_detail::add_to_counter(
                dual_incidence_sum_nontrivial,
                static_cast<std::uint64_t>(size),
                "Nontrivial dual incidence sum");
        }

        const std::uint64_t pairs = cornell_detail::choose_two(size);
        cornell_detail::add_to_counter(
            dual_pair_occurrences, pairs,
            "Dual pair-occurrence count");
        if (projected_edges.empty() && pairs > 0) {
            // No pair in the first nontrivial D_v can be a duplicate yet, so
            // this reservation is exact for that initial clique. Later overlap
            // is unknown and the table grows only when distinct edges require it.
            projected_edges.reserve_for_edges(pairs);
        }

        for (std::size_t first = 0; first < size; ++first) {
            for (std::size_t second = first + 1; second < size; ++second) {
                projected_edges.insert(
                    cornell_detail::edge_key(events[first], events[second]));
                if (++pairs_until_progress_check == (UINT64_C(1) << 20)) {
                    pairs_until_progress_check = 0;
                    projection_progress.maybe_report(
                        original_vertex_count,
                        dual_pair_occurrences,
                        projected_edges.size());
                }
            }
        }
        projection_progress.maybe_report(
            original_vertex_count,
            dual_pair_occurrences,
            projected_edges.size());

        // This D_v is never needed again. Returning its capacity while the edge
        // hash grows avoids retaining the complete incidence dual until final
        // adjacency allocation.
        std::vector<int>().swap(events);
    }
    std::vector<std::vector<int>>().swap(dual_hyperedges);

    if (projected_edges.size() > static_cast<std::size_t>(
                                     std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error(
            "Distinct dual edge count exceeds signed 64-bit range");
    }
    const std::int64_t m_dual =
        static_cast<std::int64_t>(projected_edges.size());

    const int maximum_dual_vertex_id = original_simplex_count == 0
        ? -1
        : static_cast<int>(original_simplex_count - 1);
    cornell_detail::build_adjacency_and_release_edges(
        projected_edges, maximum_dual_vertex_id, adj);

    // prepare_graph() performs the actual stable compaction immediately after
    // this reader returns. Counting nonempty lists now gives the same canonical
    // n without adding a second graph-sized marker structure.
    std::int64_t n_dual = 0;
    for (const std::vector<int>& neighbors : adj) {
        if (!neighbors.empty()) {
            cornell_detail::add_to_counter(
                n_dual, 1, "Canonical dual vertex count");
        }
    }

    record_dual_statistics(
        original_vertex_count,
        original_simplex_count,
        dual_nontrivial_hyperedge_count,
        dual_incidence_sum,
        dual_incidence_sum_nontrivial,
        minimum_dual_hyperedge_size,
        maximum_dual_hyperedge_size,
        dual_pair_occurrences,
        n_dual,
        m_dual,
        metadata);
}

}  // namespace cc_con
