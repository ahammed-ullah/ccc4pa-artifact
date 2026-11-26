#include "cc_con/algo_bpa.hpp"
#include "cc_con/sorted_vector_set.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace cc_con {

BpaOptions parse_bpa_options(const Config& config) {
    BpaOptions options;
    options.tau = config.number_or("construction.tau", 4.0);
    if (!isfinite(options.tau) || options.tau < 1.0) {
        throw runtime_error("construction.tau must be finite and at least 1");
    }

    string rule = config.string_or("construction.label_selection", "min-label");
    transform(rule.begin(), rule.end(), rule.begin(), [](unsigned char c) {
        return (char)tolower(c);
    });
    if (rule == "min-label") {
        options.label_rule = BpaLabelRule::MinLabel;
    } else if (rule == "max-label") {
        options.label_rule = BpaLabelRule::MaxLabel;
    } else if (rule == "min-clique") {
        options.label_rule = BpaLabelRule::MinClique;
    } else if (rule == "max-clique") {
        options.label_rule = BpaLabelRule::MaxClique;
    } else {
        throw runtime_error(
            "construction.label_selection must be min-label, max-label, "
            "min-clique, or max-clique");
    }
    return options;
}

const char* bpa_label_rule_name(BpaLabelRule rule) {
    switch (rule) {
        case BpaLabelRule::MinLabel: return "min-label";
        case BpaLabelRule::MaxLabel: return "max-label";
        case BpaLabelRule::MinClique: return "min-clique";
        case BpaLabelRule::MaxClique: return "max-clique";
    }
    return "unknown";
}

// Select a label from A_u intersect A_v. The vectors are sorted by label.
// Min-label and max-label stop at the first common value in their respective
// scan directions. The size rules inspect every common label; strict comparison
// leaves the smallest label selected when clique sizes tie.
//
// Direct merges are deliberate here. They were faster on the evaluation graphs
// than switching skewed inputs to binary searches, despite the latter matching
// the set-intersection cost used in the analysis.
static inline int select_label(const vector<int>& A_u,
                               const vector<int>& A_v,
                               const vector<vector<int>>& C,
                               BpaLabelRule rule) {
    int ell = -1;

    switch (rule) {
        case BpaLabelRule::MinLabel: {
            int i = 0, j = 0;
            while (i < (int)A_u.size() && j < (int)A_v.size()) {
                if (A_u[i] < A_v[j]) ++i;
                else if (A_v[j] < A_u[i]) ++j;
                else {
                    ell = A_u[i];
                    break;
                }
            }
            break;
        }

        case BpaLabelRule::MaxLabel: {
            int i = (int)A_u.size() - 1;
            int j = (int)A_v.size() - 1;
            while (i >= 0 && j >= 0) {
                if (A_u[i] > A_v[j]) --i;
                else if (A_v[j] > A_u[i]) --j;
                else {
                    ell = A_u[i];
                    break;
                }
            }
            break;
        }

        case BpaLabelRule::MinClique:
        case BpaLabelRule::MaxClique: {
            int i = 0, j = 0;
            int selected_size = 0;

            while (i < (int)A_u.size() && j < (int)A_v.size()) {
                if (A_u[i] < A_v[j]) {
                    ++i;
                } else if (A_v[j] < A_u[i]) {
                    ++j;
                } else {
                    int candidate = A_u[i];
                    int candidate_size = (int)C[candidate].size();
                    bool better =
                        ell < 0 ||
                        (rule == BpaLabelRule::MinClique
                             ? candidate_size < selected_size
                             : candidate_size > selected_size);

                    if (better) {
                        ell = candidate;
                        selected_size = candidate_size;
                    }
                    ++i;
                    ++j;
                }
            }
            break;
        }
    }

    return ell;
}

void construct_bpa(const vector<vector<int>>& adj,
                   long long m,
                   const BpaOptions& options,
                   vector<vector<int>>& C,
                   BpaStatistics& statistics) {
    int n = (int)adj.size();

    // BPA notation and concrete representation:
    //   C[ell] is clique ell.
    //   L[v] contains every label ell for which v is in C[ell].
    //   A[v] contains current-phase labels that may accept v.
    //   D[j] contains vertices admissible for the j-th label created in this
    //        phase. The global label is first_label + j.
    //   sigma_D is sum_j |D[j]| in the current phase.
    // Every inner vector is sorted and duplicate-free.
    vector<vector<int>> L(n);
    C.clear();
    statistics = {};

    if (m == 0) return;

    long double tau = (long double)options.tau;
    long double phase_bound = tau * (long double)m;

    bool next_phase = true;
    while (next_phase) {
        ++statistics.num_phases;
        next_phase = false;
        bool phase_open = true;
        long long sigma_D = 0;

        // A and D contain only the current phase's admissibility relation. Their
        // destruction here is the implementation of discarding that relation at
        // a phase boundary. C and L persist across phases.
        vector<vector<int>> A(n);
        vector<vector<int>> D;
        int first_label = (int)C.size();

        // Sorted adjacency visits every undirected edge once in lexicographic
        // endpoint order. L[u] intersect L[v] recognizes covered edges. This
        // avoids copying adjacency into uncovered-neighbor sets and deleting an
        // entry whenever an edge becomes covered. The tradeoff appears only if
        // a phase closes: every later phase scans E again and skips covered
        // edges through L.
        for (int u = 0; u < n; ++u) {
            auto& N_u = adj[u];
            for (int v : N_u) {
                if (u >= v) continue;

                // BPA considers only uncovered edges. An edge is covered exactly
                // when its endpoints occur together in some C[ell].
                if (have_common_value(L[u], L[v])) continue;

                // Choose ell from A[u] intersect A[v]. No intersection vector is
                // constructed, and the configured rule fixes the exact choice.
                int ell = select_label(A[u], A[v], C, options.label_rule);

                if (ell >= 0) {
                    // Augment C[ell] by {u,v}. Since ell is in both A sets, both
                    // endpoints are in D[ell], so the enlarged C[ell] is a clique.
                    auto& C_ell = C[ell];
                    if (insert_sorted_unique(C_ell, u)) {
                        insert_sorted_unique(L[u], ell);
                    }
                    if (insert_sorted_unique(C_ell, v)) {
                        insert_sorted_unique(L[v], ell);
                    }

                    auto& D_ell = D[ell - first_label];

                    // Restrict D[ell] to D[ell] intersect N[u] intersect N[v],
                    // using closed neighborhoods. Clique members automatically
                    // survive. All other vertices must be adjacent to both new
                    // endpoints. Retained values are written to the front of
                    // D[ell], so no replacement vector is allocated.
                    int write = 0;
                    int clique_position = 0;
                    for (int read = 0; read < (int)D_ell.size(); ++read) {
                        int w = D_ell[read];
                        while (clique_position < (int)C_ell.size() &&
                               C_ell[clique_position] < w) {
                            ++clique_position;
                        }
                        bool in_clique =
                            clique_position < (int)C_ell.size() &&
                            C_ell[clique_position] == w;
                        bool keep = in_clique;
                        if (!keep) {
                            bool adjacent_to_u =
                                w == u ||
                                binary_search(adj[w].begin(), adj[w].end(), u);
                            bool adjacent_to_v =
                                w == v ||
                                binary_search(adj[w].begin(), adj[w].end(), v);
                            keep = adjacent_to_u && adjacent_to_v;
                        }

                        if (keep) {
                            D_ell[write++] = w;
                        } else {
                            // Maintain ell in A[w] iff w is in D[ell].
                            erase_sorted_if_found(A[w], ell);
                            --sigma_D;
                        }
                    }
                    D_ell.resize(write);
                    continue;
                }

                // Once sigma(D) crosses tau*m, this phase creates no more labels.
                // It still scans the remaining edges because they may augment a
                // label already created in this phase. An edge reaching this
                // branch is deferred and guarantees that another phase is needed.
                if (!phase_open) {
                    next_phase = true;
                    continue;
                }

                // No current label accepts {u,v}, so create C[ell] = {u,v}.
                // Labels increase globally, hence appending ell keeps L and A
                // sorted without individual insertion searches.
                ell = (int)C.size();
                C.push_back({u, v});
                L[u].push_back(ell);
                L[v].push_back(ell);
                D.emplace_back();

                // Initialize D[ell] = (N(u) intersect N(v)) union {u,v}.
                // For an edge, this is N[u] intersect N[v] in closed-neighborhood
                // notation, exactly the vertices that can join {u,v}.
                auto& D_ell = D.back();
                intersect_sorted_hybrid(N_u, adj[v], D_ell);
                insert_sorted_unique(D_ell, u);
                insert_sorted_unique(D_ell, v);

                sigma_D += (long long)D_ell.size();
                statistics.peak_size_D = max(statistics.peak_size_D, sigma_D);
                for (int w : D_ell) A[w].push_back(ell);

                // The label that causes the strict overflow remains part of this
                // phase. Only subsequent non-admissible edges are deferred.
                if ((long double)sigma_D > phase_bound) {
                    phase_open = false;
                }
            }
        }
    }
}

}  // namespace cc_con
