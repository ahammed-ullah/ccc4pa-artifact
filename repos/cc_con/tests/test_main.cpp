#include "cc_con/algo_bcc.hpp"
#include "cc_con/algo_bpa.hpp"
#include "cc_con/cgf.hpp"
#include "cc_con/complement_degeneracy.hpp"
#include "cc_con/config.hpp"
#include "cc_con/algo_cpa.hpp"
#include "cc_con/algo_dpf.hpp"
#include "cc_con/first_fit.hpp"
#include "cc_con/algo_fpa.hpp"
#include "cc_con/graph.hpp"
#include "cc_con/algo_lzf.hpp"
#include "cc_con/output.hpp"
#include "cc_con/postprocess.hpp"
#include "cc_con/sorted_vector_set.hpp"
#include "cc_con/verification.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::vector<int>> graph_from_edges(
    const int n,
    const std::vector<std::pair<int, int>>& edges) {
    std::vector<std::vector<int>> adj(static_cast<std::size_t>(n));
    for (const auto& [u, v] : edges) {
        adj[u].push_back(v);
        adj[v].push_back(u);
    }
    cc_con::canonicalize_graph(adj);
    return adj;
}

long long cover_size(const std::vector<std::vector<int>>& C) {
    long long size = 0;
    for (const std::vector<int>& clique : C) {
        size += static_cast<long long>(clique.size());
    }
    return size;
}

std::int64_t metadata_integer(
    const std::map<std::string, cc_con::ConfigValue>& values,
    const std::string& key) {
    const auto found = values.find(key);
    require(found != values.end(), "Missing integer metadata value " + key);
    const auto* value = std::get_if<std::int64_t>(&found->second);
    require(value != nullptr, "Metadata value is not an integer: " + key);
    return *value;
}

double metadata_number(
    const std::map<std::string, cc_con::ConfigValue>& values,
    const std::string& key) {
    const auto found = values.find(key);
    require(found != values.end(), "Missing numeric metadata value " + key);
    if (const auto* value = std::get_if<double>(&found->second)) return *value;
    if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
        return static_cast<double>(*value);
    }
    throw std::runtime_error("Metadata value is not numeric: " + key);
}

std::string metadata_string(
    const std::map<std::string, cc_con::ConfigValue>& values,
    const std::string& key) {
    const auto found = values.find(key);
    require(found != values.end(), "Missing string metadata value " + key);
    const auto* value = std::get_if<std::string>(&found->second);
    require(value != nullptr, "Metadata value is not a string: " + key);
    return *value;
}

bool metadata_bool(
    const std::map<std::string, cc_con::ConfigValue>& values,
    const std::string& key) {
    const auto found = values.find(key);
    require(found != values.end(), "Missing boolean metadata value " + key);
    const auto* value = std::get_if<bool>(&found->second);
    require(value != nullptr, "Metadata value is not boolean: " + key);
    return *value;
}

void require_metadata_null(
    const std::map<std::string, cc_con::ConfigValue>& values,
    const std::string& key) {
    const auto found = values.find(key);
    require(found != values.end(), "Missing nullable metadata value " + key);
    require(std::holds_alternative<std::monostate>(found->second),
            "Metadata value should be unavailable: " + key);
}

void require_canonical_generated_graph(
    const std::vector<std::vector<int>>& adj,
    const std::string& graph_name) {
    for (std::size_t u = 0; u < adj.size(); ++u) {
        const std::vector<int>& neighbors = adj[u];
        require(!neighbors.empty(), graph_name + " retained an isolated vertex");
        require(std::is_sorted(neighbors.begin(), neighbors.end()),
                graph_name + " has an unsorted adjacency list");
        require(std::adjacent_find(neighbors.begin(), neighbors.end()) == neighbors.end(),
                graph_name + " has a duplicate adjacency entry");

        for (const int v : neighbors) {
            require(v >= 0 && static_cast<std::size_t>(v) < adj.size(),
                    graph_name + " has an out-of-range neighbor");
            require(v != static_cast<int>(u), graph_name + " has a self-loop");
            require(std::binary_search(adj[v].begin(), adj[v].end(),
                                       static_cast<int>(u)),
                    graph_name + " has an asymmetric edge");
        }
    }
}

void test_shared_construction_helpers() {
    // Exercise insertion and erasure at the beginning, middle, and existing
    // positions so algorithms can rely on the sorted-set invariant.
    std::vector<int> values{1, 3, 5};
    require(cc_con::insert_sorted_unique(values, 4),
            "Sorted insertion did not report a new value");
    require(values == std::vector<int>({1, 3, 4, 5}),
            "Sorted insertion placed a value incorrectly");
    require(!cc_con::insert_sorted_unique(values, 3),
            "Sorted insertion accepted a duplicate");
    require(cc_con::insert_sorted_unique(values, 0),
            "Sorted insertion failed at the beginning");
    require(cc_con::erase_sorted_if_found(values, 4),
            "Sorted erasure did not find an existing value");
    require(!cc_con::erase_sorted_if_found(values, 2),
            "Sorted erasure reported a missing value");
    require(values == std::vector<int>({0, 1, 3, 5}),
            "Sorted insertion or erasure broke ordering");

    cc_con::erase_sorted_values(values, {0, 3, 8});
    require(values == std::vector<int>({1, 5}),
            "Bulk sorted erasure produced an incorrect set");

    require(cc_con::have_common_value({1, 4, 7}, {2, 4, 8}),
            "Common-value detection missed an intersection");
    require(!cc_con::have_common_value({1, 4, 7}, {2, 5, 8}),
            "Common-value detection reported a false intersection");

    std::vector<int> skewed_values;
    for (int value = 0; value < 40; ++value) skewed_values.push_back(value);
    require(cc_con::have_common_value({37}, skewed_values),
            "Skewed common-value detection missed an intersection");
    require(!cc_con::have_common_value({47}, skewed_values),
            "Skewed common-value detection reported a false intersection");

    // The first pair takes the merge path. The second is deliberately skewed
    // enough to take the advancing-lower_bound path.
    std::vector<int> intersection{99};
    cc_con::intersect_sorted_hybrid(
        {1, 3, 5, 7}, {0, 1, 2, 5, 8}, intersection);
    require(intersection == std::vector<int>({1, 5}),
            "Merge-based sorted intersection is incorrect");

    std::vector<int> large;
    for (int value = 0; value < 24; ++value) large.push_back(value);
    cc_con::intersect_sorted_hybrid({3, 17}, large, intersection);
    require(intersection == std::vector<int>({3, 17}),
            "Skewed sorted intersection is incorrect");

    // C4 has two first-fit complement classes in canonical vertex order. Seeded
    // outputs confirm that the helper replaces rather than appends to them.
    const std::vector<std::vector<int>> cycle =
        graph_from_edges(4, {{0, 1}, {1, 2}, {2, 3}, {0, 3}});
    std::vector<int> h{99};
    std::vector<std::vector<int>> F{{99}};
    cc_con::first_fit_complement(cycle, h, F);
    require(h == std::vector<int>({0, 0, 1, 1}),
            "First-fit complement labels for C4 are incorrect");
    require(F == std::vector<std::vector<int>>({{0, 1}, {2, 3}}),
            "First-fit complement classes for C4 are incorrect");

    // The complement of C4 is a matching, so its degeneracy is one. The
    // maximum-degree peel in G followed by greedy complement coloring should
    // recover the same two graph-clique color classes for this canonical input.
    std::vector<std::vector<int>> degeneracy_F{{99}};
    const int complement_degeneracy =
        cc_con::complement_degeneracy_coloring(cycle, degeneracy_F);
    require(complement_degeneracy == 1,
            "Complement degeneracy for C4 is incorrect");
    require(degeneracy_F ==
                std::vector<std::vector<int>>({{0, 1}, {2, 3}}),
            "Complement degeneracy coloring for C4 is incorrect");
}

void verify_bpa_for_all_rules(const std::vector<std::vector<int>>& adj,
                              const double tau) {
    const std::vector<cc_con::BpaLabelRule> rules{
        cc_con::BpaLabelRule::MinLabel,
        cc_con::BpaLabelRule::MaxLabel,
        cc_con::BpaLabelRule::MinClique,
        cc_con::BpaLabelRule::MaxClique,
    };
    const long long m = cc_con::count_edges(adj);

    for (const cc_con::BpaLabelRule rule : rules) {
        cc_con::BpaOptions options;
        options.tau = tau;
        options.label_rule = rule;

        std::vector<std::vector<int>> C;
        cc_con::BpaStatistics statistics;
        cc_con::construct_bpa(adj, m, options, C, statistics);

        std::string diagnostic;
        require(cc_con::verify_clique_cover(adj, C, diagnostic),
                "BPA cover failed verification: " + diagnostic);
        require(cc_con::verify_composition_minimal(adj, C, diagnostic),
                "BPA cover is not composition-minimal: " + diagnostic);
        require(cover_size(C) <= 2 * m, "BPA violated its assignment bound");
        require(statistics.num_phases >= (m == 0 ? 0 : 1),
                "BPA reported too few phases");
    }
}

void verify_lzf(const std::vector<std::vector<int>>& adj) {
    std::vector<std::vector<int>> C;
    cc_con::construct_lzf(adj, C);

    std::string diagnostic;
    require(cc_con::verify_clique_cover(adj, C, diagnostic),
            "LZF cover failed verification: " + diagnostic);
}

void verify_dpf(const std::vector<std::vector<int>>& adj,
                const int expected_degeneracy = -1) {
    const long long m = cc_con::count_edges(adj);
    std::vector<std::vector<int>> C;
    cc_con::DpfStatistics statistics;
    cc_con::construct_dpf(adj, C, statistics);

    std::string diagnostic;
    require(cc_con::verify_clique_cover(adj, C, diagnostic),
            "DPF cover failed verification: " + diagnostic);
    require(C.size() <= static_cast<std::size_t>(m),
            "DPF violated its clique-count bound");
    require(cover_size(C) <= 2 * m,
            "DPF violated its assignment bound");
    if (expected_degeneracy >= 0) {
        require(statistics.degeneracy == expected_degeneracy,
                "DPF reported an incorrect degeneracy");
    }
}

int exact_degeneracy_of_small_graph(
    const std::vector<std::vector<int>>& adj) {
    // d = max_H delta(H). Exhausting induced vertex subsets is deliberately
    // independent of DPF's bucket peel and is practical for the ten-vertex
    // random fixtures that call this test helper.
    const int n = static_cast<int>(adj.size());
    require(n < 63, "Exact degeneracy test helper received too many vertices");
    int degeneracy = 0;
    const std::uint64_t subset_count = std::uint64_t{1} << n;

    for (std::uint64_t subset = 1; subset < subset_count; ++subset) {
        int minimum_degree = n;
        for (int v = 0; v < n; ++v) {
            if ((subset & (std::uint64_t{1} << v)) == 0) continue;

            int induced_degree = 0;
            for (const int u : adj[v]) {
                if ((subset & (std::uint64_t{1} << u)) != 0) {
                    ++induced_degree;
                }
            }
            minimum_degree = std::min(minimum_degree, induced_degree);
        }
        degeneracy = std::max(degeneracy, minimum_degree);
    }
    return degeneracy;
}

void verify_fpa_for_all_rules(const std::vector<std::vector<int>>& adj,
                              const double tau) {
    const std::vector<cc_con::FpaLabelRule> rules{
        cc_con::FpaLabelRule::MinLabel,
        cc_con::FpaLabelRule::MaxLabel,
        cc_con::FpaLabelRule::MinClique,
        cc_con::FpaLabelRule::MaxClique,
    };
    const long long m = cc_con::count_edges(adj);

    for (const cc_con::FpaLabelRule rule : rules) {
        cc_con::FpaOptions options;
        options.tau = tau;
        options.label_rule = rule;

        std::vector<std::vector<int>> C;
        cc_con::FpaStatistics statistics;
        cc_con::construct_fpa(adj, m, options, C, statistics);

        std::string diagnostic;
        require(cc_con::verify_clique_cover(adj, C, diagnostic),
                "FPA cover failed verification: " + diagnostic);
        require(cc_con::verify_composition_minimal(adj, C, diagnostic),
                "FPA cover is not composition-minimal: " + diagnostic);
        require(cover_size(C) <= 2 * m, "FPA violated its assignment bound");
        require(statistics.num_phases >= 0,
                "FPA reported a negative phase count");
    }
}

void verify_bcc(const std::vector<std::vector<int>>& adj) {
    const long long m = cc_con::count_edges(adj);
    std::vector<std::vector<int>> F;
    cc_con::BccCrossCoverData data;
    std::vector<cc_con::CrossCoverClique> cross_cover;
    cc_con::BccStatistics statistics;
    cc_con::prepare_bcc_cross_cover(adj, m, F, data, statistics);

    // Production BCC and CPA retain only one pair. This small semantic test
    // deliberately accumulates the family so it can inspect every owned edge.
    std::vector<cc_con::CrossCoverClique> pair_cover;
    std::size_t record_position = 0;
    while (record_position < data.records.size()) {
        record_position = cc_con::construct_next_cross_cover_pair(
            data, record_position, pair_cover);
        for (const cc_con::CrossCoverClique& cross_clique : pair_cover) {
            cross_cover.push_back(cross_clique);
        }
    }

    std::vector<std::vector<int>> C;
    for (const std::vector<int>& color_class : F) {
        if (color_class.size() >= 2) C.push_back(color_class);
    }
    for (const cc_con::CrossCoverClique& cross_clique : cross_cover) {
        require(!cross_clique.R.empty() && !cross_clique.Z.empty(),
                "CrossCover emitted an empty side");
        require(std::is_sorted(cross_clique.R.begin(), cross_clique.R.end()) &&
                    std::is_sorted(cross_clique.Z.begin(), cross_clique.Z.end()),
                "CrossCover emitted an unsorted side");

        C.emplace_back(cross_clique.R.size() + cross_clique.Z.size());
        std::merge(cross_clique.R.begin(), cross_clique.R.end(),
                   cross_clique.Z.begin(), cross_clique.Z.end(),
                   C.back().begin());
    }

    std::string diagnostic;
    require(cc_con::verify_clique_cover(adj, C, diagnostic),
            "BCC cover failed verification: " + diagnostic);
    require(C.size() <= static_cast<std::size_t>(m),
            "BCC violated its clique-count bound");
    require(cover_size(C) <= 2 * m,
            "BCC violated its assignment bound");

    const long long n = static_cast<long long>(adj.size());
    require(statistics.mu == n * (n - 1) / 2 - m + 1,
            "BCC reported an incorrect mu");
    require(statistics.num_parts >= statistics.num_complement_colors,
            "BCC reported fewer parts than complement color classes");
    require(statistics.num_complement_colors <=
                statistics.complement_degeneracy + (adj.empty() ? 0 : 1),
            "BCC complement coloring exceeded the degeneracy-color bound");

    // This is the ownership invariant used to form CPA's B_i'. The original
    // color classes own their internal pairs, while a CrossCover clique owns
    // exactly R x Z. Those sets must partition E exactly once.
    std::vector<std::vector<int>> owner_count(
        adj.size(), std::vector<int>(adj.size(), 0));
    for (const std::vector<int>& color_class : F) {
        for (std::size_t left = 0; left < color_class.size(); ++left) {
            for (std::size_t right = left + 1;
                 right < color_class.size();
                 ++right) {
                ++owner_count[color_class[left]][color_class[right]];
            }
        }
    }
    for (const cc_con::CrossCoverClique& cross_clique : cross_cover) {
        for (const int r : cross_clique.R) {
            for (const int z : cross_clique.Z) {
                const int u = std::min(r, z);
                const int v = std::max(r, z);
                ++owner_count[u][v];
            }
        }
    }
    for (std::size_t u = 0; u < adj.size(); ++u) {
        for (std::size_t v = u + 1; v < adj.size(); ++v) {
            const bool edge =
                std::binary_search(adj[u].begin(), adj[u].end(),
                                   static_cast<int>(v));
            require(owner_count[u][v] == (edge ? 1 : 0),
                    "BCC owned edges do not partition E");
        }
    }
}

void verify_cpa_for_all_rules(const std::vector<std::vector<int>>& adj,
                              const double tau) {
    const std::vector<cc_con::CpaLabelRule> rules{
        cc_con::CpaLabelRule::MinLabel,
        cc_con::CpaLabelRule::MaxLabel,
        cc_con::CpaLabelRule::MinClique,
        cc_con::CpaLabelRule::MaxClique,
    };
    const long long m = cc_con::count_edges(adj);

    for (const cc_con::CpaLabelRule rule : rules) {
        cc_con::CpaOptions options;
        options.tau = tau;
        options.label_rule = rule;

        std::vector<std::vector<int>> C;
        cc_con::CpaStatistics statistics;
        cc_con::construct_cpa(adj, m, options, C, statistics);

        std::string diagnostic;
        require(cc_con::verify_clique_cover(adj, C, diagnostic),
                "CPA cover failed verification: " + diagnostic);
        require(cc_con::verify_composition_minimal(adj, C, diagnostic),
                "CPA cover is not composition-minimal: " + diagnostic);
        require(C.size() <=
                    static_cast<std::size_t>(statistics.bcc_num_cliques),
                "CPA created more cliques than its BCC family");
        require(cover_size(C) <= statistics.bcc_cover_size,
                "CPA increased the BCC family's assignment count");
        require(statistics.num_phases >= 0,
                "CPA reported a negative phase count");
    }
}

void test_triangle() {
    const std::vector<std::vector<int>> adj =
        graph_from_edges(3, {{0, 1}, {0, 2}, {1, 2}});

    std::vector<std::vector<int>> C;
    cc_con::BpaStatistics statistics;
    cc_con::construct_bpa(adj, cc_con::count_edges(adj), {}, C, statistics);
    require(C.size() == 1, "Triangle should produce one clique");
    require(C.front() == std::vector<int>({0, 1, 2}),
            "Triangle should produce C[0] = {0,1,2}");
    require(statistics.num_phases == 1, "Triangle should finish in one phase");
    require(statistics.peak_size_D == 3,
            "Triangle should have peak sigma(D) = 3");
    verify_bpa_for_all_rules(adj, 1.0);

    std::vector<std::vector<int>> fpa_C;
    cc_con::FpaStatistics fpa_statistics;
    cc_con::construct_fpa(
        adj, cc_con::count_edges(adj), {}, fpa_C, fpa_statistics);
    require(fpa_C == std::vector<std::vector<int>>({{0, 1, 2}}),
            "FPA should retain the triangle as one first-fit clique");
    require(fpa_statistics.num_phases == 0,
            "FPA should need no augmentation phase for one first-fit clique");
    require(fpa_statistics.peak_size_D == 3,
            "FPA triangle should have peak sigma(D) = 3");
    verify_fpa_for_all_rules(adj, 1.0);

    std::vector<std::vector<int>> lzf_C;
    cc_con::construct_lzf(adj, lzf_C);
    require(lzf_C == std::vector<std::vector<int>>({{0, 1, 2}}),
            "LZF should produce one first-fit class on a triangle");

    std::vector<std::vector<int>> dpf_C;
    cc_con::DpfStatistics dpf_statistics;
    cc_con::construct_dpf(adj, dpf_C, dpf_statistics);
    require(dpf_C == std::vector<std::vector<int>>({{0, 1, 2}}),
            "DPF should produce one clique on a triangle");
    require(dpf_statistics.degeneracy == 2,
            "DPF triangle degeneracy should be two");

    std::vector<std::vector<int>> bcc_C;
    cc_con::BccStatistics bcc_statistics;
    cc_con::construct_bcc(
        adj, cc_con::count_edges(adj), bcc_C, bcc_statistics);
    require(bcc_C == std::vector<std::vector<int>>({{0, 1, 2}}),
            "BCC should produce its single degeneracy class on a triangle");
    require(bcc_statistics.mu == 1 &&
                bcc_statistics.part_size_limit == 0 &&
                !bcc_statistics.splitting_applied &&
                bcc_statistics.num_complement_colors == 1 &&
                bcc_statistics.num_parts == 1,
            "BCC triangle statistics are incorrect");

    std::vector<std::vector<int>> cpa_C;
    cc_con::CpaStatistics cpa_statistics;
    cc_con::construct_cpa(
        adj, cc_con::count_edges(adj), {}, cpa_C, cpa_statistics);
    require(cpa_C == std::vector<std::vector<int>>({{0, 1, 2}}),
            "CPA should initialize with the triangle color class");
    require(cpa_statistics.num_phases == 0 &&
                cpa_statistics.peak_size_D == 3,
            "CPA triangle phase statistics are incorrect");
}

void test_fpa_known_graphs_and_phasing() {
    const std::vector<std::vector<int>> path =
        graph_from_edges(3, {{0, 1}, {1, 2}});
    std::vector<std::vector<int>> C;
    cc_con::FpaStatistics statistics;
    cc_con::construct_fpa(path, cc_con::count_edges(path), {}, C, statistics);
    require(C == std::vector<std::vector<int>>({{0, 1}, {1, 2}}),
            "FPA produced an unexpected cover for a three-vertex path");
    require(statistics.num_phases == 1,
            "FPA should need one augmentation phase for a three-vertex path");
    require(statistics.peak_size_D == 4,
            "FPA path should have peak sigma(D) = 4");

    // Canonical first fit partitions C4 into F[0]={0,1} and F[1]={2,3}.
    // Their internal edges initialize two cover cliques and sigma(D)=4. At
    // tau=1, creating {1,2} raises sigma(D) to 6 > m=4 and closes the phase;
    // {0,3} is therefore created in phase two.
    const std::vector<std::vector<int>> cycle =
        graph_from_edges(4, {{0, 1}, {1, 2}, {2, 3}, {0, 3}});
    cc_con::FpaOptions tau_one;
    tau_one.tau = 1.0;
    cc_con::construct_fpa(
        cycle, cc_con::count_edges(cycle), tau_one, C, statistics);
    require(C == std::vector<std::vector<int>>(
                     {{0, 1}, {2, 3}, {1, 2}, {0, 3}}),
            "FPA produced an unexpected tau=1 cover for C4");
    require(statistics.num_phases == 2,
            "FPA tau=1 should process C4 in two phases");
    require(statistics.peak_size_D == 6,
            "FPA tau=1 C4 should have peak sigma(D) = 6");

    cc_con::FpaOptions tau_two;
    tau_two.tau = 2.0;
    cc_con::construct_fpa(
        cycle, cc_con::count_edges(cycle), tau_two, C, statistics);
    require(C == std::vector<std::vector<int>>(
                     {{0, 1}, {2, 3}, {1, 2}, {0, 3}}),
            "FPA produced an unexpected tau=2 cover for C4");
    require(statistics.num_phases == 1,
            "FPA tau=2 should process C4 in one phase");
    require(statistics.peak_size_D == 8,
            "FPA tau=2 C4 should have peak sigma(D) = 8");

    verify_fpa_for_all_rules(cycle, 1.0);
    verify_fpa_for_all_rules(cycle, 2.0);
}

void test_lzf_known_graphs() {
    const std::vector<std::vector<int>> path =
        graph_from_edges(3, {{0, 1}, {1, 2}});
    std::vector<std::vector<int>> C;
    cc_con::construct_lzf(path, C);
    require(C == std::vector<std::vector<int>>({{0, 1}, {1, 2}}),
            "LZF produced an unexpected cover for a three-vertex path");

    const std::vector<std::vector<int>> cycle =
        graph_from_edges(4, {{0, 1}, {1, 2}, {2, 3}, {0, 3}});
    cc_con::construct_lzf(cycle, C);
    require(C == std::vector<std::vector<int>>(
                     {{0, 1}, {2, 3}, {1, 2}, {0, 3}}),
            "LZF produced an unexpected first-fit cover for a four-cycle");
    verify_lzf(cycle);
}

void test_dpf_known_graphs() {
    // The flat bucket peel is deterministic for a canonical graph. These exact
    // covers pin down both its tie behavior and DPF's canonical-id local
    // first-fit order. verify_dpf checks the semantic properties separately.
    const std::vector<std::vector<int>> path =
        graph_from_edges(3, {{0, 1}, {1, 2}});
    std::vector<std::vector<int>> C;
    cc_con::DpfStatistics statistics;
    cc_con::construct_dpf(path, C, statistics);
    require(C == std::vector<std::vector<int>>({{0, 1}, {1, 2}}),
            "DPF produced an unexpected cover for a three-vertex path");
    require(statistics.degeneracy == 1,
            "DPF path degeneracy should be one");

    const std::vector<std::vector<int>> cycle =
        graph_from_edges(4, {{0, 1}, {1, 2}, {2, 3}, {0, 3}});
    cc_con::construct_dpf(cycle, C, statistics);
    require(C == std::vector<std::vector<int>>(
                     {{0, 1}, {0, 3}, {1, 2}, {2, 3}}),
            "DPF produced an unexpected cover for a four-cycle");
    require(statistics.degeneracy == 2,
            "DPF cycle degeneracy should be two");
    verify_dpf(cycle, 2);
}

void test_bcc_and_cpa_known_graphs() {
    // Complement degeneracy coloring partitions C4 into {0,1} and {2,3}.
    // The fixed sparse-graph rule bypasses b=3 and leaves those classes intact.
    // CrossCover then emits one edge for each distinct cross-neighborhood.
    const std::vector<std::vector<int>> cycle =
        graph_from_edges(4, {{0, 1}, {1, 2}, {2, 3}, {0, 3}});
    std::vector<std::vector<int>> C;
    cc_con::BccStatistics bcc_statistics;
    cc_con::construct_bcc(
        cycle, cc_con::count_edges(cycle), C, bcc_statistics);
    require(C == std::vector<std::vector<int>>(
                     {{0, 1}, {2, 3}, {0, 3}, {1, 2}}),
            "BCC produced an unexpected cover for C4");
    require(bcc_statistics.mu == 3 &&
                bcc_statistics.part_size_limit == 0 &&
                !bcc_statistics.splitting_applied &&
                bcc_statistics.num_parts == 2,
            "BCC C4 statistics are incorrect");

    cc_con::CpaStatistics cpa_statistics;
    cc_con::construct_cpa(
        cycle, cc_con::count_edges(cycle), {}, C, cpa_statistics);
    require(C == std::vector<std::vector<int>>(
                     {{0, 1}, {2, 3}, {0, 3}, {1, 2}}),
            "CPA produced an unexpected cover for C4");
    require(cpa_statistics.bcc_num_cliques == 4 &&
                cpa_statistics.bcc_cover_size == 8 &&
                cpa_statistics.num_phases == 1,
            "CPA C4 statistics are incorrect");

    // At tau=1, the third edge clique raises sigma(D) from four to six and
    // closes the first phase. The fourth edge is deferred, exercising CPA's
    // BPA-style label memberships and a reduced CrossCover clique in phase two.
    cc_con::CpaOptions tau_one;
    tau_one.tau = 1.0;
    cc_con::construct_cpa(
        cycle, cc_con::count_edges(cycle), tau_one, C, cpa_statistics);
    require(C == std::vector<std::vector<int>>(
                     {{0, 1}, {2, 3}, {0, 3}, {1, 2}}),
            "CPA tau=1 produced an unexpected cover for C4");
    require(cpa_statistics.num_phases == 2 &&
                cpa_statistics.peak_size_D == 6,
            "CPA tau=1 should process C4 in two phases");

    // Repeated cross-neighborhoods and partially overlapping graph cliques
    // exercise BCC's exact grouping rather than only singleton equivalence
    // classes. The resulting partition depends on the degeneracy order, so the
    // semantic cover and bounds are the stable properties asserted here.
    const std::vector<std::vector<int>> repeated = graph_from_edges(
        6,
        {{0, 1}, {0, 2}, {1, 2},
         {3, 4}, {3, 5}, {4, 5},
         {0, 3}, {0, 4}, {1, 3}, {1, 4}, {2, 4}, {2, 5}});
    verify_bcc(repeated);
    verify_cpa_for_all_rules(repeated, 1.0);
    verify_cpa_for_all_rules(repeated, 4.0);
}

void test_multiple_phases_with_tau_one() {
    // The first four vertices form two triangles sharing edge {0,1}. The two
    // disjoint edges appear later in lexicographic order. The first disjoint edge
    // raises sigma(D) from 6 to 8 > m=7, so {6,7} is deferred to phase two.
    const std::vector<std::vector<int>> adj = graph_from_edges(
        8,
        {{0, 1}, {0, 2}, {1, 2}, {0, 3}, {1, 3}, {4, 5}, {6, 7}});

    std::vector<std::vector<int>> C;
    cc_con::BpaStatistics statistics;
    cc_con::BpaOptions tau_one;
    tau_one.tau = 1.0;
    cc_con::construct_bpa(
        adj, cc_con::count_edges(adj), tau_one, C, statistics);
    require(statistics.num_phases == 2, "tau=1 test graph should require two phases");
    require(statistics.peak_size_D == 8,
            "Unexpected peak sigma(D) on phase test");

    std::string diagnostic;
    require(cc_con::verify_clique_cover(adj, C, diagnostic), diagnostic);
}

void test_canonicalization() {
    std::vector<std::vector<int>> adj(6);
    adj[0] = {0, 1, 1};  // loop and duplicate
    adj[2] = {1};        // asymmetric edge; vertices 3,4,5 are isolated
    cc_con::canonicalize_graph(adj);

    require(adj.size() == 3, "Canonicalization did not remove isolated vertices");
    require(adj[0] == std::vector<int>({1}), "Canonical adjacency for vertex 0 is wrong");
    require(adj[1] == std::vector<int>({0, 2}), "Canonical adjacency for vertex 1 is wrong");
    require(adj[2] == std::vector<int>({1}), "Canonical adjacency for vertex 2 is wrong");
    require(cc_con::count_edges(adj) == 2, "Canonical edge count is wrong");
}

void test_graph_sources_and_config() {
    std::vector<std::vector<int>> mm_adj;
    cc_con::GraphMetadata mm_metadata;
    cc_con::prepare_graph("tests/data/mm_graph.toml", mm_adj, mm_metadata);
    require(mm_adj.size() == 5 && cc_con::count_edges(mm_adj) == 4,
            "Matrix Market graph was parsed incorrectly");
    require(mm_metadata.type == "mm", "Matrix Market type was not reported");

    cc_con::Config bpa_config = cc_con::Config::load("tests/data/bpa_run.toml");
    const cc_con::CommonOptions bpa_common = cc_con::parse_common_options(bpa_config);
    const cc_con::BpaOptions mm_options = cc_con::parse_bpa_options(bpa_config);
    require(!bpa_common.assignment_minimal && bpa_common.verification,
            "BPA common run options were parsed incorrectly");
    require(bpa_config.unused_keys().empty(), "BPA run config has unused keys");

    cc_con::Config output_config =
        cc_con::Config::load("tests/data/output_run.toml");
    const cc_con::CommonOptions output_common =
        cc_con::parse_common_options(output_config);
    (void)cc_con::parse_bpa_options(output_config);
    require(output_common.write_cover && !output_common.verification &&
                output_common.cover_directory.filename() == "integration",
            "Fixed CCF output options were parsed incorrectly");
    require(output_config.unused_keys().empty(),
            "CCF output run config has unused keys");

    std::vector<std::vector<int>> C;
    cc_con::BpaStatistics statistics;
    cc_con::construct_bpa(mm_adj, cc_con::count_edges(mm_adj), mm_options, C, statistics);
    std::string diagnostic;
    require(cc_con::verify_clique_cover(mm_adj, C, diagnostic),
            "BPA failed on Matrix Market input: " + diagnostic);

    const std::vector<std::vector<int>> complete_adj = graph_from_edges(
        6,
        {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5},
         {1, 2}, {1, 3}, {1, 4}, {1, 5},
         {2, 3}, {2, 4}, {2, 5},
         {3, 4}, {3, 5}, {4, 5}});
    cc_con::construct_bpa(
        complete_adj, cc_con::count_edges(complete_adj), mm_options, C, statistics);
    require(C.size() == 1 && C.front().size() == 6,
            "BPA should construct one clique for K6");

    cc_con::Config lzf_config = cc_con::Config::load("tests/data/lzf_run.toml");
    const cc_con::CommonOptions lzf_common = cc_con::parse_common_options(lzf_config);
    const cc_con::BpaOptions default_bpa = cc_con::parse_bpa_options(lzf_config);
    const cc_con::FpaOptions default_fpa = cc_con::parse_fpa_options(lzf_config);
    require(!lzf_common.write_cover && lzf_common.verification,
            "LZF common run options were parsed incorrectly");
    require(default_bpa.tau == 4.0 && default_fpa.tau == 4.0,
            "Phased constructions should default to tau=4");
    require(lzf_config.unused_keys().empty(), "LZF run config has unused keys");
    cc_con::construct_lzf(complete_adj, C);
    require(C.size() == 1 && C.front().size() == 6,
            "LZF should construct one first-fit clique for K6");

    cc_con::DpfStatistics dpf_statistics;
    cc_con::construct_dpf(complete_adj, C, dpf_statistics);
    require(C.size() == 1 && C.front().size() == 6,
            "DPF should construct one clique for K6");
    require(dpf_statistics.degeneracy == 5,
            "DPF should report degeneracy five for K6");

    cc_con::Config dpf_config = cc_con::Config::load("config/runs/dpf.toml");
    const cc_con::CommonOptions dpf_common =
        cc_con::parse_common_options(dpf_config);
    require(!dpf_common.write_cover && !dpf_common.verification,
            "DPF common run options were parsed incorrectly");
    require(dpf_config.unused_keys().empty(),
            "DPF run config has unused keys");

    cc_con::Config fpa_config = cc_con::Config::load("tests/data/fpa_run.toml");
    const cc_con::CommonOptions fpa_common =
        cc_con::parse_common_options(fpa_config);
    const cc_con::FpaOptions fpa_options = cc_con::parse_fpa_options(fpa_config);
    require(!fpa_common.write_cover && fpa_common.verification,
            "FPA common run options were parsed incorrectly");
    require(fpa_options.tau == 2.0 &&
                fpa_options.label_rule == cc_con::FpaLabelRule::MaxClique,
            "FPA construction options were parsed incorrectly");
    require(fpa_config.unused_keys().empty(), "FPA run config has unused keys");

    cc_con::FpaStatistics fpa_statistics;
    cc_con::construct_fpa(
        complete_adj, cc_con::count_edges(complete_adj),
        fpa_options, C, fpa_statistics);
    require(C.size() == 1 && C.front().size() == 6,
            "FPA should construct one first-fit clique for K6");

    cc_con::Config bcc_config =
        cc_con::Config::load("config/runs/bcc.toml");
    const cc_con::CommonOptions bcc_common =
        cc_con::parse_common_options(bcc_config);
    require(!bcc_common.write_cover && !bcc_common.verification,
            "BCC common run options were parsed incorrectly");
    require(bcc_config.unused_keys().empty(),
            "BCC run config has unused keys");
    cc_con::BccStatistics bcc_statistics;
    cc_con::construct_bcc(
        complete_adj, cc_con::count_edges(complete_adj), C, bcc_statistics);
    require(C.size() == 1 && C.front().size() == 6,
            "BCC should construct one degeneracy class for K6");

    cc_con::Config cpa_config =
        cc_con::Config::load("config/runs/cpa.toml");
    const cc_con::CommonOptions cpa_common =
        cc_con::parse_common_options(cpa_config);
    const cc_con::CpaOptions cpa_options =
        cc_con::parse_cpa_options(cpa_config);
    require(cpa_common.assignment_minimal && !cpa_common.write_cover &&
                !cpa_common.verification,
            "CPA common run options were parsed incorrectly");
    require(cpa_options.tau == 4.0 &&
                cpa_options.label_rule == cc_con::CpaLabelRule::MinLabel,
            "CPA construction options were parsed incorrectly");
    require(cpa_config.unused_keys().empty(),
            "CPA run config has unused keys");
    cc_con::CpaStatistics cpa_statistics;
    cc_con::construct_cpa(
        complete_adj, cc_con::count_edges(complete_adj),
        cpa_options, C, cpa_statistics);
    require(C.size() == 1 && C.front().size() == 6,
            "CPA should construct one atomic clique for K6");
}

void test_hypergraph_sources() {
    const std::vector<std::pair<std::string, std::string>> graph_configs{
        {"tests/data/hyper_k_uniform.toml", "k-uniform"},
        {"tests/data/hyper_k_isolates.toml", "k-uniform"},
        {"tests/data/hyper_poisson.toml", "poisson"},
        {"tests/data/hyper_lognormal.toml", "lognormal"},
        {"tests/data/hyper_power_law.toml", "power-law"},
    };

    for (std::size_t test = 0; test < graph_configs.size(); ++test) {
        std::vector<std::vector<int>> adj;
        cc_con::GraphMetadata metadata;
        cc_con::prepare_graph(graph_configs[test].first, adj, metadata);

        require(metadata.source == "synthetic" &&
                    metadata.type == "hypergraph",
                "Hypergraph source metadata is incorrect");
        require(metadata_string(metadata.parameters, "distribution") ==
                    graph_configs[test].second,
                "Hypergraph distribution metadata is incorrect");
        require_canonical_generated_graph(adj, metadata.name);

        const std::int64_t requested_n =
            metadata_integer(metadata.parameters, "n");
        const std::int64_t requested_r =
            metadata_integer(metadata.parameters, "r");
        const std::int64_t nontrivial = metadata_integer(
            metadata.generation_statistics, "nontrivial_hyperedges");
        const std::int64_t size_sum = metadata_integer(
            metadata.generation_statistics, "sampled_size_sum");
        const std::int64_t size_min = metadata_integer(
            metadata.generation_statistics, "sampled_size_min");
        const std::int64_t size_max = metadata_integer(
            metadata.generation_statistics, "sampled_size_max");
        const double size_mean = metadata_number(
            metadata.generation_statistics, "sampled_size_mean");
        const std::int64_t pair_occurrences = metadata_integer(
            metadata.generation_statistics, "pair_occurrences");
        const double estimated_distinct_edges = metadata_number(
            metadata.generation_statistics, "estimated_distinct_edges");
        const double estimated_ratio = metadata_number(
            metadata.generation_statistics,
            "estimated_pair_to_edge_ratio");
        const std::string deduplication_strategy = metadata_string(
            metadata.generation_statistics, "deduplication_strategy");
        const std::int64_t distinct_edges = metadata_integer(
            metadata.generation_statistics, "distinct_edges");
        const long long m = cc_con::count_edges(adj);

        require(static_cast<std::int64_t>(adj.size()) <= requested_n,
                "Canonical hypergraph has more vertices than requested");
        require(nontrivial >= 0 && nontrivial <= requested_r,
                "Nontrivial hyperedge count is outside [0,r]");
        require(size_sum >= 0 && size_sum <= requested_r * requested_n,
                "Sampled hyperedge-size sum is outside its possible range");
        require(size_min >= 0 && size_min <= size_max && size_max <= requested_n,
                "Sampled hyperedge-size extrema are inconsistent");
        require(std::abs(size_mean -
                         static_cast<double>(size_sum) /
                             static_cast<double>(requested_r)) < 1e-12,
                "Sampled hyperedge-size mean is inconsistent");
        require(pair_occurrences >= distinct_edges && distinct_edges == m,
                "Hypergraph edge-generation statistics are inconsistent");
        require(estimated_distinct_edges >= 0.0 &&
                    estimated_distinct_edges <=
                        static_cast<double>(pair_occurrences),
                "Estimated distinct-edge count is outside [0,P]");
        if (pair_occurrences == 0) {
            require(estimated_ratio == 0.0 &&
                        deduplication_strategy == "sort-unique",
                    "Empty projection selected an inconsistent strategy");
        } else {
            require(estimated_distinct_edges > 0.0 &&
                        std::abs(estimated_ratio -
                                 static_cast<double>(pair_occurrences) /
                                     estimated_distinct_edges) < 1e-12,
                    "Estimated pair-to-edge ratio is inconsistent");
            require((estimated_ratio <= 2.0 &&
                         deduplication_strategy == "sort-unique") ||
                        (estimated_ratio > 2.0 &&
                         deduplication_strategy == "hash"),
                    "Hypergraph deduplication strategy violates its threshold");
        }

        if (test == 0) {
            // Four identical all-vertex hyperedges generate every K6 edge four
            // times. rho=4 deterministically exercises the pre-sized hash path.
            require(adj.size() == 6 && m == 15,
                    "Repeated K6 projections should produce one simple K6");
            require(nontrivial == 4 && size_sum == 24 &&
                        size_min == 6 && size_max == 6 && size_mean == 6.0 &&
                        pair_occurrences == 60 &&
                        estimated_distinct_edges == 15.0 &&
                        estimated_ratio == 4.0 &&
                        deduplication_strategy == "hash" &&
                        distinct_edges == 15,
                    "K-uniform generation statistics are incorrect");
        } else if (test == 1) {
            // One sampled three-set leaves seven requested vertices isolated.
            // rho=1 chooses sort/unique, and the public graph is a densely
            // renumbered K3.
            require(adj.size() == 3 && m == 3 &&
                        estimated_distinct_edges == 3.0 &&
                        estimated_ratio == 1.0 &&
                        deduplication_strategy == "sort-unique",
                    "Generated isolates were not removed before construction");
        }

        // Every construction receives exactly the same concrete canonical graph
        // regardless of which generator produced it.
        verify_lzf(adj);
        verify_bpa_for_all_rules(adj, 4.0);
        verify_fpa_for_all_rules(adj, 4.0);
        verify_bcc(adj);
        verify_cpa_for_all_rules(adj, 4.0);
    }

    // A fixed seed reproduces both the graph and all recorded observations in
    // one build environment. Exact samples are intentionally not hard-coded for
    // standard-library distributions.
    std::vector<std::vector<int>> first_adj;
    std::vector<std::vector<int>> second_adj;
    cc_con::GraphMetadata first_metadata;
    cc_con::GraphMetadata second_metadata;
    cc_con::prepare_graph(
        "tests/data/hyper_poisson.toml", first_adj, first_metadata);
    cc_con::prepare_graph(
        "tests/data/hyper_poisson.toml", second_adj, second_metadata);
    require(first_adj == second_adj &&
                first_metadata.generation_statistics ==
                    second_metadata.generation_statistics,
            "A fixed hypergraph seed was not reproducible");

    const auto require_generation_error = [](const std::string& path,
                                             const std::string& expected_text) {
        std::vector<std::vector<int>> adj;
        cc_con::GraphMetadata metadata;
        bool rejected = false;
        try {
            cc_con::prepare_graph(path, adj, metadata);
        } catch (const std::runtime_error& error) {
            rejected = true;
            require(std::string(error.what()).find(expected_text) != std::string::npos,
                    "Hypergraph validation produced an unclear diagnostic");
        }
        require(rejected, "Invalid hypergraph parameters were accepted");
        require(adj.empty(),
                "Invalid hypergraph parameters allocated graph adjacency");
    };

    require_generation_error("tests/data/hyper_bad_poisson.toml", "lambda");
    require_generation_error(
        "tests/data/hyper_inapplicable_parameter.toml", "graph.parameters.k");
}

void verify_every_construction(const std::vector<std::vector<int>>& adj) {
    verify_lzf(adj);
    verify_dpf(adj);
    verify_bpa_for_all_rules(adj, 4.0);
    verify_fpa_for_all_rules(adj, 4.0);
    verify_bcc(adj);
    verify_cpa_for_all_rules(adj, 4.0);
}

void require_native_cornell_statistics(
    const cc_con::GraphMetadata& metadata,
    const std::int64_t expected_r,
    const std::int64_t expected_minimum,
    const double expected_mean) {
    const auto& statistics = metadata.generation_statistics;
    require(metadata_bool(statistics, "native_statistics_available"),
            "Native Cornell statistics were marked unavailable");
    require(metadata_integer(statistics, "r") == expected_r &&
                metadata_integer(statistics, "r_nontrivial") == 3 &&
                metadata_integer(statistics, "incidence_sum") == 9 &&
                metadata_integer(statistics, "incidence_sum_nontrivial") == 8,
            "Native Cornell incidence statistics are incorrect");
    require(metadata_integer(statistics, "min_hyperedge_size") ==
                expected_minimum &&
                metadata_integer(statistics, "max_hyperedge_size") == 3 &&
                std::abs(metadata_number(statistics, "mean_hyperedge_size") -
                         expected_mean) < 1e-12,
            "Native Cornell hyperedge-size statistics are incorrect");
    require(metadata_integer(statistics, "pair_occurrences") == 7 &&
                metadata_integer(statistics, "distinct_edges") == 6 &&
                metadata_integer(statistics, "native_cover_size") == 8 &&
                std::abs(metadata_number(statistics, "native_CR") - 1.5) <
                    1e-12 &&
                std::abs(metadata_number(statistics, "overlap_factor") -
                         7.0 / 6.0) < 1e-12,
            "Native Cornell projection statistics are incorrect");
}

void test_cornell_sources() {
    // The two native fixtures describe the same hyperedges in Cornell's two
    // actual archive layouts. External IDs are noncontiguous, member order is
    // arbitrary, one record repeats vertex 40, and vertex 99 occurs only in a
    // singleton. Their canonical projection is therefore K4 on four dense IDs.
    std::vector<std::vector<int>> simplex_adj;
    cc_con::GraphMetadata simplex_metadata;
    cc_con::prepare_graph(
        "tests/data/cornell_simplices.toml", simplex_adj, simplex_metadata);
    require(simplex_metadata.type == "cornell-simplices",
            "Cornell simplex type metadata is incorrect");
    require(simplex_adj.size() == 4 && cc_con::count_edges(simplex_adj) == 6,
            "Cornell simplex projection did not produce canonical K4");
    require_canonical_generated_graph(simplex_adj, simplex_metadata.name);
    require_native_cornell_statistics(simplex_metadata, 5, 0, 1.8);
    verify_every_construction(simplex_adj);

    std::vector<std::vector<int>> hyperedge_adj;
    cc_con::GraphMetadata hyperedge_metadata;
    cc_con::prepare_graph(
        "tests/data/cornell_hyperedges.toml",
        hyperedge_adj, hyperedge_metadata);
    require(hyperedge_metadata.type == "cornell-hyperedges" &&
                hyperedge_adj == simplex_adj,
            "Cornell direct-hyperedge projection differs from simplex projection");
    require_native_cornell_statistics(hyperedge_metadata, 4, 1, 2.25);
    verify_every_construction(hyperedge_adj);

}

void test_cornell_dual_projection() {
    // Specification Test 1:
    // C0={a,b}, C1={b,c}, C2={b,d}, C3={a,d} gives
    // D_a={0,3}, D_b={0,1,2}, D_c={1}, D_d={2,3}.
    std::vector<std::vector<int>> first_adj;
    cc_con::GraphMetadata first_metadata;
    cc_con::prepare_graph(
        "tests/data/cornell_dual_test1.toml", first_adj, first_metadata);

    const std::vector<std::vector<int>> expected_first = graph_from_edges(
        4, {{0, 3}, {0, 1}, {0, 2}, {1, 2}, {2, 3}});
    require(first_metadata.type == "cornell-dual-simplices" &&
                first_adj == expected_first,
            "First Cornell dual projection has incorrect edges");
    require_canonical_generated_graph(first_adj, first_metadata.name);

    const auto& first_statistics = first_metadata.generation_statistics;
    require(metadata_bool(first_statistics,
                          "dual_native_statistics_available"),
            "First dual projection marked native statistics unavailable");
    require(metadata_integer(first_statistics, "original_vertex_count") == 4 &&
                metadata_integer(first_statistics,
                                 "original_simplex_count") == 4 &&
                metadata_integer(first_statistics,
                                 "dual_hyperedge_count") == 4 &&
                metadata_integer(first_statistics,
                                 "dual_nontrivial_hyperedge_count") == 3,
            "First dual projection has incorrect source counts");
    require(metadata_integer(first_statistics, "dual_incidence_sum") == 8 &&
                metadata_integer(first_statistics,
                                 "dual_incidence_sum_nontrivial") == 7 &&
                metadata_integer(first_statistics,
                                 "min_dual_hyperedge_size") == 1 &&
                metadata_integer(first_statistics,
                                 "max_dual_hyperedge_size") == 3 &&
                metadata_number(first_statistics,
                                "mean_dual_hyperedge_size") == 2.0,
            "First dual projection has incorrect incidence statistics");
    require(metadata_integer(first_statistics,
                             "dual_pair_occurrences") == 5 &&
                metadata_integer(first_statistics, "n_dual") == 4 &&
                metadata_integer(first_statistics, "m_dual") == 5 &&
                metadata_integer(first_statistics,
                                 "dual_native_cover_size") == 7 &&
                std::abs(metadata_number(first_statistics,
                                         "dual_native_CR") -
                         10.0 / 7.0) < 1e-12 &&
                metadata_number(first_statistics,
                                "dual_overlap_factor") == 1.0 &&
                metadata_integer(first_statistics,
                                 "dual_isolated_vertices_removed") == 0,
            "First dual projection has incorrect projection statistics");
    verify_every_construction(first_adj);

    // Specification Test 2 keeps identical events distinct. Both a and b
    // generate {0,1}, but the globally deduplicated graph contains that edge
    // once and is K3.
    std::vector<std::vector<int>> second_adj;
    cc_con::GraphMetadata second_metadata;
    cc_con::prepare_graph(
        "tests/data/cornell_dual_test2.toml", second_adj, second_metadata);

    require(second_adj == graph_from_edges(
                              3, {{0, 1}, {0, 2}, {1, 2}}),
            "Overlapping dual cliques did not produce one simple K3");
    require_canonical_generated_graph(second_adj, second_metadata.name);

    const auto& second_statistics = second_metadata.generation_statistics;
    require(metadata_integer(second_statistics, "original_vertex_count") == 2 &&
                metadata_integer(second_statistics,
                                 "original_simplex_count") == 3 &&
                metadata_integer(second_statistics,
                                 "dual_hyperedge_count") == 2 &&
                metadata_integer(second_statistics,
                                 "dual_nontrivial_hyperedge_count") == 2 &&
                metadata_integer(second_statistics,
                                 "dual_incidence_sum") == 5 &&
                metadata_integer(second_statistics,
                                 "dual_incidence_sum_nontrivial") == 5,
            "Second dual projection has incorrect incidence counts");
    require(metadata_integer(second_statistics,
                             "min_dual_hyperedge_size") == 2 &&
                metadata_integer(second_statistics,
                                 "max_dual_hyperedge_size") == 3 &&
                metadata_number(second_statistics,
                                "mean_dual_hyperedge_size") == 2.5 &&
                metadata_integer(second_statistics,
                                 "dual_pair_occurrences") == 4 &&
                metadata_integer(second_statistics, "n_dual") == 3 &&
                metadata_integer(second_statistics, "m_dual") == 3 &&
                metadata_integer(second_statistics,
                                 "dual_native_cover_size") == 5 &&
                std::abs(metadata_number(second_statistics,
                                         "dual_native_CR") - 1.2) < 1e-12 &&
                std::abs(metadata_number(second_statistics,
                                         "dual_overlap_factor") -
                         4.0 / 3.0) < 1e-12,
            "Second dual projection has incorrect projection statistics");
    verify_every_construction(second_adj);

    // Reuse the original-entity Cornell fixture through the independent dual reader.
    // Its five events include a singleton-only event and an empty event, both
    // of which become isolates and disappear. The surviving events 0,1,3 are
    // densely renumbered to a canonical K3. Vertex 40 is repeated within C3;
    // deduplicating it keeps the dual incidence sum at nine.
    std::vector<std::vector<int>> isolate_adj;
    cc_con::GraphMetadata isolate_metadata;
    cc_con::prepare_graph(
        "tests/data/cornell_dual_isolates.toml",
        isolate_adj, isolate_metadata);

    require(isolate_adj == graph_from_edges(
                               3, {{0, 1}, {0, 2}, {1, 2}}),
            "Cornell dual isolate removal did not produce canonical K3");
    require_canonical_generated_graph(isolate_adj, isolate_metadata.name);

    const auto& isolate_statistics =
        isolate_metadata.generation_statistics;
    require(metadata_integer(isolate_statistics,
                             "original_vertex_count") == 5 &&
                metadata_integer(isolate_statistics,
                                 "original_simplex_count") == 5 &&
                metadata_integer(isolate_statistics,
                                 "dual_hyperedge_count") == 5 &&
                metadata_integer(isolate_statistics,
                                 "dual_nontrivial_hyperedge_count") == 4 &&
                metadata_integer(isolate_statistics,
                                 "dual_incidence_sum") == 9 &&
                metadata_integer(isolate_statistics,
                                 "dual_incidence_sum_nontrivial") == 8,
            "Dual isolate fixture has incorrect incidence statistics");
    require(metadata_integer(isolate_statistics,
                             "min_dual_hyperedge_size") == 1 &&
                metadata_integer(isolate_statistics,
                                 "max_dual_hyperedge_size") == 2 &&
                std::abs(metadata_number(isolate_statistics,
                                         "mean_dual_hyperedge_size") - 1.8) <
                    1e-12 &&
                metadata_integer(isolate_statistics,
                                 "dual_pair_occurrences") == 4 &&
                metadata_integer(isolate_statistics, "n_dual") == 3 &&
                metadata_integer(isolate_statistics, "m_dual") == 3 &&
                metadata_integer(isolate_statistics,
                                 "dual_isolated_vertices_removed") == 2 &&
                metadata_integer(isolate_statistics,
                                 "dual_native_cover_size") == 8 &&
                metadata_number(isolate_statistics,
                                "dual_native_CR") == 0.75 &&
                std::abs(metadata_number(isolate_statistics,
                                         "dual_overlap_factor") -
                         4.0 / 3.0) < 1e-12,
            "Dual isolate fixture has incorrect projection statistics");
    verify_every_construction(isolate_adj);
}

void test_sx_tag_question_projection() {
    // Tags 1 and 2 generate overlapping cliques on questions 100 and 101.
    // Question 103 occurs only under singleton tag 3, so question_count remains
    // four while the canonical graph passed to constructions is K3.
    std::vector<std::vector<int>> adj;
    cc_con::GraphMetadata metadata;
    cc_con::prepare_graph(
        "tests/data/sx_tag_questions.toml", adj, metadata);

    require(metadata.type == "sx-tag-question",
            "Stack Exchange graph type metadata is incorrect");
    require(adj == graph_from_edges(
                       3, {{0, 1}, {0, 2}, {1, 2}}),
            "Stack Exchange tag projection did not produce canonical K3");
    require_canonical_generated_graph(adj, metadata.name);

    const auto& statistics = metadata.generation_statistics;
    require(metadata_bool(statistics, "native_statistics_available"),
            "Stack Exchange native statistics were marked unavailable");
    require(metadata_integer(statistics, "question_count") == 4 &&
                metadata_integer(statistics, "tag_count") == 3 &&
                metadata_integer(statistics, "nontrivial_tag_count") == 2 &&
                metadata_integer(statistics, "incidence_sum") == 6 &&
                metadata_integer(statistics,
                                 "incidence_sum_nontrivial") == 5,
            "Stack Exchange source incidence statistics are incorrect");
    require(metadata_integer(statistics, "min_tag_degree") == 1 &&
                metadata_integer(statistics, "max_tag_degree") == 3 &&
                metadata_number(statistics, "mean_tag_degree") == 2.0 &&
                metadata_integer(statistics, "pair_occurrences") == 4 &&
                std::abs(metadata_number(statistics, "potential_CR") - 1.6) <
                    1e-12,
            "Stack Exchange prescreen statistics are incorrect");
    require(metadata_integer(statistics, "distinct_edges") == 3 &&
                metadata_integer(statistics, "canonical_question_count") == 3 &&
                metadata_integer(statistics,
                                 "isolated_questions_removed") == 1 &&
                metadata_integer(statistics, "native_cover_size") == 5 &&
                std::abs(metadata_number(statistics, "native_CR") - 1.2) <
                    1e-12 &&
                std::abs(metadata_number(statistics, "overlap_factor") -
                         4.0 / 3.0) < 1e-12,
            "Stack Exchange projected-graph statistics are incorrect");
    verify_every_construction(adj);

    // The repeated (tag 8, question 200) record denotes one incidence. Both
    // resulting tag sets are singletons, so the canonical graph is empty even
    // though two distinct source questions were observed.
    std::vector<std::vector<int>> singleton_adj;
    cc_con::GraphMetadata singleton_metadata;
    cc_con::prepare_graph(
        "tests/data/sx_tag_questions_singletons.toml",
        singleton_adj, singleton_metadata);

    require(singleton_adj.empty(),
            "Singleton-only Stack Exchange input retained graph vertices");
    const auto& singleton_statistics =
        singleton_metadata.generation_statistics;
    require(metadata_integer(singleton_statistics, "question_count") == 2 &&
                metadata_integer(singleton_statistics, "tag_count") == 2 &&
                metadata_integer(singleton_statistics,
                                 "nontrivial_tag_count") == 0 &&
                metadata_integer(singleton_statistics, "incidence_sum") == 2 &&
                metadata_integer(singleton_statistics,
                                 "incidence_sum_nontrivial") == 0 &&
                metadata_integer(singleton_statistics,
                                 "pair_occurrences") == 0 &&
                metadata_integer(singleton_statistics,
                                 "canonical_question_count") == 0 &&
                metadata_integer(singleton_statistics,
                                 "isolated_questions_removed") == 2,
            "Repeated Stack Exchange incidence was not deduplicated");
    require_metadata_null(singleton_statistics, "potential_CR");
    require_metadata_null(singleton_statistics, "native_CR");
    require_metadata_null(singleton_statistics, "overlap_factor");
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                       const std::size_t position) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[position + i]) << (8 * i);
    }
    return value;
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes,
                       const std::size_t position) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[position + i]) << (8 * i);
    }
    return value;
}

std::vector<std::uint8_t> read_binary_file(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "Unable to open binary test artifact");
    const std::streampos end = input.tellg();
    require(end >= 0, "Unable to determine binary test artifact size");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(input), "Unable to read binary test artifact");
    return bytes;
}

std::vector<std::uint8_t> read_hex_fixture(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    require(static_cast<bool>(input), "Unable to open hexadecimal fixture");

    std::vector<std::uint8_t> bytes;
    int high_nibble = -1;
    char c = 0;
    while (input.get(c)) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        int value = -1;
        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
        require(value >= 0, "Hexadecimal fixture contains a non-hex digit");
        if (high_nibble < 0) high_nibble = value;
        else {
            bytes.push_back(static_cast<std::uint8_t>(16 * high_nibble + value));
            high_nibble = -1;
        }
    }
    require(high_nibble < 0, "Hexadecimal fixture has an unmatched nibble");
    return bytes;
}

void write_binary_fixture(const std::filesystem::path& path,
                          const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "Unable to materialize binary fixture");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    require(static_cast<bool>(output), "Unable to finish binary fixture");
}

void test_postprocess_and_canonical_formats() {
    const std::vector<std::vector<int>> adj = graph_from_edges(
        5,
        {{0, 1}, {0, 2}, {1, 2}, {2, 3}, {2, 4}, {3, 4}});

    std::vector<std::vector<int>> C;
    cc_con::BpaStatistics statistics;
    cc_con::construct_bpa(adj, cc_con::count_edges(adj), {}, C, statistics);
    const long long raw_size = cover_size(C);
    cc_con::make_assignment_minimal(C, static_cast<int>(adj.size()));
    require(cover_size(C) <= raw_size, "Postprocessing increased cover size");

    std::string diagnostic;
    require(cc_con::verify_clique_cover(adj, C, diagnostic),
            "Assignment-minimal postprocessing broke coverage: " + diagnostic);

    const std::filesystem::path directory = "build/format-tests";
    std::filesystem::create_directories(directory);
    const long long m = cc_con::count_edges(adj);
    const std::string fingerprint =
        cc_con::canonical_graph_fingerprint_sha256(adj, m);

    const std::filesystem::path cgf_path = directory / "two-triangles.cgf";
    const cc_con::CgfArtifactInfo written_cgf =
        cc_con::write_cgf(cgf_path, adj, m, fingerprint);
    std::vector<std::vector<int>> decoded_adj;
    const cc_con::CgfArtifactInfo read_cgf =
        cc_con::read_and_validate_cgf(cgf_path, decoded_adj);
    require(decoded_adj == adj && read_cgf.n == adj.size() &&
                read_cgf.m == static_cast<std::uint64_t>(m) &&
                read_cgf.file_checksum_sha256 ==
                    written_cgf.file_checksum_sha256 &&
                read_cgf.graph_fingerprint_sha256 == fingerprint,
            "CGF round trip changed the canonical graph or metadata");

    std::vector<std::vector<int>> unchecked_adj;
    const cc_con::CgfArtifactInfo unchecked_cgf =
        cc_con::read_cgf(cgf_path, unchecked_adj);
    require(unchecked_adj == adj &&
                unchecked_cgf.graph_fingerprint_sha256 == fingerprint &&
                unchecked_cgf.file_checksum_sha256 ==
                    written_cgf.file_checksum_sha256,
            "Normal CGF decoding changed the graph or stored metadata");
    require(read_binary_file(cgf_path) == read_hex_fixture(
                "tests/data/formats/two-triangles.cgf.hex"),
            "CGF writer differs from the fixed golden fixture");

    const std::filesystem::path corrupted_cgf_path =
        directory / "corrupted.cgf";
    std::filesystem::copy_file(
        cgf_path, corrupted_cgf_path,
        std::filesystem::copy_options::overwrite_existing);
    std::fstream corrupted_cgf(
        corrupted_cgf_path, std::ios::in | std::ios::out | std::ios::binary);
    require(static_cast<bool>(corrupted_cgf),
            "Unable to open corrupted CGF fixture");
    corrupted_cgf.seekg(-1, std::ios::end);
    char cgf_final_byte = 0;
    corrupted_cgf.get(cgf_final_byte);
    corrupted_cgf.seekp(-1, std::ios::end);
    corrupted_cgf.put(static_cast<char>(cgf_final_byte ^ 1));
    corrupted_cgf.close();

    bool rejected_cgf = false;
    try {
        cc_con::read_and_validate_cgf(corrupted_cgf_path, decoded_adj);
    } catch (const std::runtime_error& error) {
        rejected_cgf =
            std::string(error.what()).find("checksum") != std::string::npos;
    }
    require(rejected_cgf, "CGF corruption was not rejected by its checksum");

    // The normal decoding path reads the checksum field as metadata but does
    // not hash a prepared CGF again. Altering only that field must therefore
    // leave decoding intact while explicit validation rejects it.
    std::vector<std::uint8_t> bad_stored_checksum = read_binary_file(cgf_path);
    bad_stored_checksum[128] ^= 1U;
    const std::filesystem::path bad_checksum_cgf_path =
        directory / "bad-stored-checksum.cgf";
    write_binary_fixture(bad_checksum_cgf_path, bad_stored_checksum);
    cc_con::read_cgf(bad_checksum_cgf_path, unchecked_adj);
    require(unchecked_adj == adj,
            "Normal CGF decoder depended on the file checksum");
    bool rejected_stored_checksum = false;
    try {
        cc_con::read_and_validate_cgf(bad_checksum_cgf_path, decoded_adj);
    } catch (const std::runtime_error& error) {
        rejected_stored_checksum =
            std::string(error.what()).find("checksum") != std::string::npos;
    }
    require(rejected_stored_checksum,
            "Explicit CGF validation accepted a false stored checksum");

    const std::filesystem::path ccf_path = directory / "two-triangles.ccf";
    const cc_con::CcfArtifactInfo written_ccf = cc_con::write_ccf(
        ccf_path, C, static_cast<int>(adj.size()), m, fingerprint);
    std::vector<std::vector<int>> decoded_C;
    const cc_con::CcfArtifactInfo read_ccf =
        cc_con::read_and_validate_ccf(ccf_path, decoded_C);
    require(decoded_C == C && read_ccf.num_cliques == C.size() &&
                read_ccf.cover_size == static_cast<std::uint64_t>(cover_size(C)) &&
                read_ccf.file_checksum_sha256 ==
                    written_ccf.file_checksum_sha256 &&
                read_ccf.graph_fingerprint_sha256 == fingerprint,
            "CCF round trip changed the cover or metadata");
    require(read_binary_file(ccf_path) == read_hex_fixture(
                "tests/data/formats/two-triangles.ccf.hex"),
            "CCF writer differs from the fixed golden fixture");

    // Independently inspect the fixed CCF header and its R+1 directory shape.
    const std::vector<std::uint8_t> bytes = read_binary_file(ccf_path);
    require(bytes.size() >= 160 &&
                std::string(bytes.begin(), bytes.begin() + 3) == "CCF",
            "CCF magic or fixed-header size is invalid");
    require(read_u32(bytes, 8) == 1 && read_u32(bytes, 12) == 160 &&
                read_u32(bytes, 16) == 1,
            "CCF version/header/encoding fields are incorrect");
    require(read_u64(bytes, 24) == adj.size() &&
                read_u64(bytes, 32) == static_cast<std::uint64_t>(m) &&
                read_u64(bytes, 40) == C.size() &&
                read_u64(bytes, 48) ==
                    static_cast<std::uint64_t>(cover_size(C)),
            "CCF graph and cover counts are incorrect");
    require(read_u64(bytes, 56) == 160 &&
                read_u64(bytes, 64) == C.size() + 1 &&
                read_u64(bytes, 160) == read_u64(bytes, 72),
            "CCF does not use the documented R+1 offset table");

    // Fixed corrupted fixtures distinguish checksum failure from a malformed
    // directory whose checksum has been recomputed correctly.
    const std::filesystem::path corrupted_path = directory / "corrupted.ccf";
    write_binary_fixture(corrupted_path, read_hex_fixture(
        "tests/data/formats/bad-checksum.ccf.hex"));
    bool rejected_checksum = false;
    try {
        cc_con::read_and_validate_ccf(corrupted_path, decoded_C);
    } catch (const std::runtime_error& error) {
        rejected_checksum =
            std::string(error.what()).find("checksum") != std::string::npos;
    }
    require(rejected_checksum, "CCF corruption was not rejected by its checksum");

    const std::filesystem::path bad_offset_path = directory / "bad-offset.ccf";
    write_binary_fixture(bad_offset_path, read_hex_fixture(
        "tests/data/formats/bad-offset.ccf.hex"));
    bool rejected_offset = false;
    try {
        cc_con::read_and_validate_ccf(bad_offset_path, decoded_C);
    } catch (const std::runtime_error& error) {
        rejected_offset =
            std::string(error.what()).find("first offset") != std::string::npos;
    }
    require(rejected_offset,
            "Checksum-valid malformed CCF offset table was accepted");
}

void test_empty_graph() {
    std::vector<std::vector<int>> adj(4);
    cc_con::canonicalize_graph(adj);
    require(adj.empty(), "All-isolate input should canonicalize to the empty graph");

    std::vector<std::vector<int>> C;
    cc_con::BpaStatistics statistics;
    cc_con::construct_bpa(adj, 0, {}, C, statistics);
    require(C.empty() && statistics.num_phases == 0,
            "Empty graph should produce an empty cover in zero phases");

    cc_con::construct_lzf(adj, C);
    require(C.empty(), "LZF should produce an empty cover for an empty graph");

    cc_con::DpfStatistics dpf_statistics;
    cc_con::construct_dpf(adj, C, dpf_statistics);
    require(C.empty() && dpf_statistics.degeneracy == 0,
            "DPF should produce an empty cover with degeneracy zero");

    cc_con::FpaStatistics fpa_statistics;
    cc_con::construct_fpa(adj, 0, {}, C, fpa_statistics);
    require(C.empty() && fpa_statistics.num_phases == 0 &&
                fpa_statistics.peak_size_D == 0,
            "FPA should produce an empty cover with empty statistics");

    cc_con::BccStatistics bcc_statistics;
    cc_con::construct_bcc(adj, 0, C, bcc_statistics);
    require(C.empty() && bcc_statistics.mu == 1 &&
                bcc_statistics.num_parts == 0,
            "BCC should produce an empty cover with empty statistics");

    cc_con::CpaStatistics cpa_statistics;
    cc_con::construct_cpa(adj, 0, {}, C, cpa_statistics);
    require(C.empty() && cpa_statistics.mu == 1 &&
                cpa_statistics.num_phases == 0 &&
                cpa_statistics.peak_size_D == 0,
            "CPA should produce an empty cover with empty statistics");
}

void test_random_graphs() {
    std::mt19937 generator(73);
    std::bernoulli_distribution edge(0.38);
    for (int trial = 0; trial < 30; ++trial) {
        std::vector<std::pair<int, int>> edges;
        for (int u = 0; u < 10; ++u) {
            for (int v = u + 1; v < 10; ++v) {
                if (edge(generator)) edges.emplace_back(u, v);
            }
        }
        const std::vector<std::vector<int>> adj = graph_from_edges(10, edges);
        verify_bpa_for_all_rules(adj, trial % 2 == 0 ? 1.0 : 2.0);
        verify_fpa_for_all_rules(adj, trial % 2 == 0 ? 1.0 : 2.0);
        verify_lzf(adj);
        verify_dpf(adj, exact_degeneracy_of_small_graph(adj));
        verify_bcc(adj);
        verify_cpa_for_all_rules(adj, trial % 2 == 0 ? 1.0 : 4.0);
    }
}

}  // namespace

int main() {
    try {
        test_shared_construction_helpers();
        test_triangle();
        test_fpa_known_graphs_and_phasing();
        test_lzf_known_graphs();
        test_dpf_known_graphs();
        test_bcc_and_cpa_known_graphs();
        test_multiple_phases_with_tau_one();
        test_canonicalization();
        test_graph_sources_and_config();
        test_hypergraph_sources();
        test_cornell_sources();
        test_cornell_dual_projection();
        test_sx_tag_question_projection();
        test_postprocess_and_canonical_formats();
        test_empty_graph();
        test_random_graphs();
        std::cout << "All cc_con tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
