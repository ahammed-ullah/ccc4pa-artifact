#include "cc_con/algo_cpa.hpp"
#include "cc_con/algo_bcc.hpp"
#include "cc_con/sorted_vector_set.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace cc_con {

CpaOptions parse_cpa_options(const Config& config) {
    CpaOptions options;
    options.tau = config.number_or("construction.tau", 4.0);
    if (!isfinite(options.tau) || options.tau < 1.0) {
        throw runtime_error("construction.tau must be finite and at least 1");
    }

    string rule =
        config.string_or("construction.label_selection", "min-label");
    transform(rule.begin(), rule.end(), rule.begin(), [](unsigned char c) {
        return (char)tolower(c);
    });
    if (rule == "min-label") {
        options.label_rule = CpaLabelRule::MinLabel;
    } else if (rule == "max-label") {
        options.label_rule = CpaLabelRule::MaxLabel;
    } else if (rule == "min-clique") {
        options.label_rule = CpaLabelRule::MinClique;
    } else if (rule == "max-clique") {
        options.label_rule = CpaLabelRule::MaxClique;
    } else {
        throw runtime_error(
            "construction.label_selection must be min-label, max-label, "
            "min-clique, or max-clique");
    }
    return options;
}

const char* cpa_label_rule_name(CpaLabelRule rule) {
    switch (rule) {
        case CpaLabelRule::MinLabel: return "min-label";
        case CpaLabelRule::MaxLabel: return "max-label";
        case CpaLabelRule::MinClique: return "min-clique";
        case CpaLabelRule::MaxClique: return "max-clique";
    }
    return "unknown";
}

// Select one label from the sorted set accepting all of B_i'. Min-label and
// max-label are direct endpoint choices. The size rules inspect all candidates;
// strict comparison retains the smallest label when clique sizes tie.
static inline int select_label(const vector<int>& admissible_labels,
                               const vector<vector<int>>& C,
                               CpaLabelRule rule) {
    if (admissible_labels.empty()) return -1;

    switch (rule) {
        case CpaLabelRule::MinLabel:
            return admissible_labels.front();
        case CpaLabelRule::MaxLabel:
            return admissible_labels.back();
        case CpaLabelRule::MinClique:
        case CpaLabelRule::MaxClique:
            break;
        default:
            return -1;
    }

    int label = -1;
    int selected_size = 0;
    for (int candidate : admissible_labels) {
        int candidate_size = (int)C[candidate].size();
        bool better =
            label < 0 ||
            (rule == CpaLabelRule::MinClique
                 ? candidate_size < selected_size
                 : candidate_size > selected_size);
        if (better) {
            label = candidate;
            selected_size = candidate_size;
        }
    }
    return label;
}

void construct_cpa(const vector<vector<int>>& adj,
                   long long m,
                   const CpaOptions& options,
                   vector<vector<int>>& C,
                   CpaStatistics& statistics) {
    int n = (int)adj.size();
    C.clear();
    statistics = {};

    // CrossCover-PA Step 1 constructs the BCC family. The shared preparation
    // routine returns its two components separately: F is the original
    // complement-degeneracy coloring used as our initial cover, and cross_data
    // encodes the CrossCover cliques generated from temporary part pairs. The
    // complete BCC family and its trees are never stored together.
    vector<vector<int>> F;
    BccCrossCoverData cross_data;
    BccStatistics bcc_statistics;
    prepare_bcc_cross_cover(adj, m, F, cross_data, bcc_statistics);

    statistics.mu = bcc_statistics.mu;
    statistics.part_size_limit = bcc_statistics.part_size_limit;
    statistics.splitting_applied = bcc_statistics.splitting_applied;
    statistics.complement_degeneracy =
        bcc_statistics.complement_degeneracy;
    statistics.num_complement_colors =
        bcc_statistics.num_complement_colors;
    statistics.num_parts = bcc_statistics.num_parts;

    int num_initial_cliques = 0;
    for (auto& color_class : F) {
        if (color_class.size() < 2) continue;
        ++num_initial_cliques;
        statistics.bcc_cover_size +=
            (long long)color_class.size();
    }
    statistics.bcc_num_cliques = num_initial_cliques;

    C.reserve(num_initial_cliques);

    // Phased-admissibility state:
    //
    //   L[v] contains every output label q for which v belongs to C[q].
    //   A[v] contains current-phase labels whose cliques may accept v.
    //   D[q-first_label] is the dual of current-phase label q.
    //
    // All inner vectors are sorted and duplicate-free. L persists across phases
    // and determines whether an owned edge is already covered. A and D contain
    // only the current phase's admissibility relation.
    vector<vector<int>> L(n);
    vector<vector<int>> A(n);
    vector<vector<int>> D;
    vector<int> intersection;
    long long sigma_D = 0;

    // Implementation choice for Figure 4, Steps 1--2: insert each nontrivial F
    // class directly into C. Distinct classes are already nonmergeable, so they
    // need neither a tree nor atomic PA processing. Their duals initialize the
    // first phase's A and D relation before CrossCover cliques are considered.
    for (auto& color_class : F) {
        if (color_class.size() < 2) continue;

        int q = (int)C.size();
        C.push_back(std::move(color_class));
        auto& C_q = C.back();
        for (int v : C_q) L[v].push_back(q);

        // D[q] is the intersection of the closed neighborhoods of C[q]. Start
        // from one edge. The current D always contains C[q], so once its size is
        // exactly |C[q]| no later intersection can change it and the loop stops.
        D.emplace_back();
        auto& D_q = D.back();
        intersect_sorted_hybrid(adj[C_q[0]], adj[C_q[1]], D_q);
        insert_sorted_unique(D_q, C_q[0]);
        insert_sorted_unique(D_q, C_q[1]);

        for (int position = 2; position < (int)C_q.size(); ++position) {
            if (D_q.size() == C_q.size()) break;
            int z = C_q[position];
            intersect_sorted_hybrid(D_q, adj[z], intersection);
            insert_sorted_unique(intersection, z);
            D_q.swap(intersection);
        }

        sigma_D += (long long)D_q.size();
        statistics.peak_size_D =
            max(statistics.peak_size_D, sigma_D);
        for (int w : D_q) A[w].push_back(q);
    }
    vector<vector<int>>().swap(F);

    // If all edges lie inside the initial color classes, there is no CrossCover
    // clique to process and the phased loop is never entered.
    if (cross_data.records.empty()) return;

    long double phase_bound = (long double)options.tau * (long double)m;

    // The vectors below are reused for each B_i. endpoint_mark[v] stores the
    // number of the most recent B_i that marked v. Increasing endpoint_epoch
    // makes all older marks irrelevant, avoiding an O(n) clear for each B_i.
    vector<uint64_t> endpoint_mark(n, 0);
    uint64_t endpoint_epoch = 0;
    vector<pair<int, int>> tree_edges;
    vector<int> B_prime;
    vector<int> admissible_labels;
    vector<int> added;
    vector<int> merged;
    vector<int> updated_dual;

    // The initial color classes have global labels beginning at zero, so their
    // D entries use first_label=0 in the first phase. At later boundaries A and
    // D are discarded and only labels introduced in the new phase may be used.
    int first_label = 0;

    // The first pass constructs one nonempty BCC part pair at a time. If a
    // phase closes, only reduced B_i' cliques are retained in pending; the ranked
    // BCC records and part maps are released as soon as that first pass ends.
    // Later passes therefore have the same explicit R/Z representation but
    // do not retain or reconstruct the original BCC family.
    vector<CrossCoverClique> pair_cover;
    size_t record_position = 0;
    int pair_position = 0;
    vector<CrossCoverClique> pending;
    bool first_pass = true;

    while (first_pass || !pending.empty()) {
        ++statistics.num_phases;
        bool phase_open = true;
        vector<CrossCoverClique> next_pending;
        size_t pending_position = 0;

        while (true) {
            CrossCoverClique* current_B = nullptr;
            if (first_pass) {
                if (pair_position == (int)pair_cover.size()) {
                    if (record_position == cross_data.records.size()) break;

                    record_position = construct_next_cross_cover_pair(
                        cross_data, record_position, pair_cover);
                    pair_position = 0;

                    // CPA reports the size of the BCC family before any B_i is
                    // restricted or merged. Account for a pair immediately
                    // after CrossCover constructs it and before PA mutates it.
                    statistics.bcc_num_cliques +=
                        (long long)pair_cover.size();
                    for (auto& cross_clique : pair_cover) {
                        statistics.bcc_cover_size +=
                            (long long)(cross_clique.R.size() +
                                        cross_clique.Z.size());
                    }
                }
                current_B = &pair_cover[pair_position++];
            } else {
                if (pending_position == pending.size()) break;
                current_B = &pending[pending_position++];
            }
            auto& B_i = *current_B;

            // Restrict B_i before its atomic PA step. Its owned edges are R x Z.
            // An edge {u,v} is covered exactly when L[u] and L[v] have a common
            // output label. B_i' retains every endpoint of an uncovered owned
            // edge, so it contains both endpoints of every edge still assigned
            // to this BCC clique and omits vertices no longer needed from it.
            ++endpoint_epoch;
            int marked_R = 0;
            int marked_Z = 0;

            for (int u : B_i.R) {
                // With no cover membership for u, every edge from u to Z is
                // uncovered. Mark the whole row without calling the set test.
                if (L[u].empty()) {
                    if (endpoint_mark[u] != endpoint_epoch) {
                        endpoint_mark[u] = endpoint_epoch;
                        ++marked_R;
                    }
                    for (int v : B_i.Z) {
                        if (endpoint_mark[v] != endpoint_epoch) {
                            endpoint_mark[v] = endpoint_epoch;
                            ++marked_Z;
                        }
                    }
                } else {
                    for (int v : B_i.Z) {
                        bool uncovered =
                            L[v].empty() || !have_common_value(L[u], L[v]);
                        if (!uncovered) continue;

                        if (endpoint_mark[u] != endpoint_epoch) {
                            endpoint_mark[u] = endpoint_epoch;
                            ++marked_R;
                        }
                        if (endpoint_mark[v] != endpoint_epoch) {
                            endpoint_mark[v] = endpoint_epoch;
                            ++marked_Z;
                        }

                        // Once this u and all vertices of Z are known to belong
                        // to B_i', later edges in the same row cannot add an
                        // endpoint and need not be queried.
                        if (marked_Z == (int)B_i.Z.size()) break;
                    }
                }

                // If every vertex on both sides is marked, B_i'=B_i regardless
                // of the unqueried owner edges. This is the common first-pass
                // case and avoids scanning the rest of a large Cartesian product.
                if (marked_R == (int)B_i.R.size() &&
                    marked_Z == (int)B_i.Z.size()) {
                    break;
                }
            }

            if (marked_R == 0) {
                // No uncovered owned edge remains. The owner is consumed even
                // if some non-owned edge inside R union Z is not covered here.
                vector<int>().swap(B_i.R);
                vector<int>().swap(B_i.Z);
                continue;
            }

            // Retain only endpoints of uncovered owned edges. Writing marked
            // vertices to the front keeps both sides sorted. These reduced
            // sides become a pending B_i if the closed phase must defer it.
            if (marked_R != (int)B_i.R.size()) {
                int write = 0;
                for (int u : B_i.R) {
                    if (endpoint_mark[u] == endpoint_epoch) {
                        B_i.R[write++] = u;
                    }
                }
                B_i.R.resize(write);
            }
            if (marked_Z != (int)B_i.Z.size()) {
                int write = 0;
                for (int v : B_i.Z) {
                    if (endpoint_mark[v] == endpoint_epoch) {
                        B_i.Z[write++] = v;
                    }
                }
                B_i.Z.resize(write);
            }

            // Merge the two sorted, disjoint sides into the sorted clique B_i'.
            B_prime.resize(B_i.R.size() + B_i.Z.size());
            merge(B_i.R.begin(), B_i.R.end(),
                  B_i.Z.begin(), B_i.Z.end(), B_prime.begin());

            // Build CrossCover-PA's unrooted tree only after restriction. For
            // x in R and y in Z, its edge set is
            //
            //   { {x,z}: z in Z } union { {y,r}: r in R - {x} }.
            //
            // No parent/child interpretation or retained tree family is used.
            int x = B_i.R.front();
            int y = B_i.Z.front();
            tree_edges.clear();
            tree_edges.reserve(B_prime.size() - 1);
            for (int z : B_i.Z) tree_edges.emplace_back(x, z);
            for (int position = 1;
                 position < (int)B_i.R.size();
                 ++position) {
                tree_edges.emplace_back(y, B_i.R[position]);
            }

            // CPA's atomic admissibility test is written directly from Figure 4:
            //
            //   admissible_labels
            //     = intersection_{{u,v} in E(T_i)} (A[u] intersect A[v]).
            //
            // The paper calls this local set L. The longer code name avoids a
            // collision with the persistent membership lists L[v] above.
            //
            // Begin with the edge having the smallest upper bound on its first
            // intersection. A zero-cost edge is decisive, so that search also
            // stops early. The tree remains an unordered edge set; this affects
            // only evaluation order, not the result.
            int first_tree_edge = 0;
            int best_cost = min(
                (int)A[tree_edges[0].first].size(),
                (int)A[tree_edges[0].second].size());
            for (int edge = 1; edge < (int)tree_edges.size(); ++edge) {
                if (best_cost == 0) break;
                int cost = min(
                    (int)A[tree_edges[edge].first].size(),
                    (int)A[tree_edges[edge].second].size());
                if (cost < best_cost) {
                    best_cost = cost;
                    first_tree_edge = edge;
                }
            }
            if (first_tree_edge != 0) {
                swap(tree_edges[0], tree_edges[first_tree_edge]);
            }

            admissible_labels.clear();
            int first_u = tree_edges[0].first;
            int first_v = tree_edges[0].second;
            intersect_sorted_hybrid(
                A[first_u], A[first_v], admissible_labels);

            for (int edge = 1;
                 edge < (int)tree_edges.size() && !admissible_labels.empty();
                 ++edge) {
                int u = tree_edges[edge].first;
                int v = tree_edges[edge].second;

                // Once only one label remains, two binary searches determine
                // whether that label survives this edge. This avoids allocating
                // and scanning another intersection for every remaining edge.
                if (admissible_labels.size() == 1) {
                    int candidate = admissible_labels.front();
                    bool in_u = binary_search(
                        A[u].begin(), A[u].end(), candidate);
                    bool in_v = binary_search(
                        A[v].begin(), A[v].end(), candidate);
                    if (!in_u || !in_v) {
                        admissible_labels.clear();
                    }
                    continue;
                }

                // Intersect with the smaller endpoint set first. Empty results
                // stop immediately, including between the two endpoints of one
                // tree edge. Algebraically this is exactly
                // L <- L intersect (A[u] intersect A[v]).
                int first_vertex = u;
                int second_vertex = v;
                if (A[first_vertex].size() > A[second_vertex].size()) {
                    swap(first_vertex, second_vertex);
                }

                intersect_sorted_hybrid(
                    admissible_labels, A[first_vertex], intersection);
                if (intersection.empty()) {
                    admissible_labels.clear();
                    break;
                }
                intersect_sorted_hybrid(
                    intersection, A[second_vertex], admissible_labels);
            }

            // CrossCover-PA permits any label in this intersection. The
            // configured rule makes that choice deterministic.
            int label = select_label(
                admissible_labels, C, options.label_rule);

            if (label >= 0) {
                // Figure 4, Step 3(b)(ii), applied to the reduced B_i': add the
                // entire clique atomically. Because B_i' contains an uncovered
                // owned edge, at least one vertex is new to the selected clique.
                auto& C_label = C[label];
                added.clear();
                set_difference(B_prime.begin(), B_prime.end(),
                               C_label.begin(), C_label.end(),
                               back_inserter(added));
                merged.clear();
                merged.reserve(C_label.size() + added.size());
                set_union(C_label.begin(), C_label.end(),
                          B_prime.begin(), B_prime.end(),
                          back_inserter(merged));

                // L is the only covered-edge state. An old label may be inserted
                // after newer labels, so preserve sorted order rather than
                // appending it.
                for (int v : added) {
                    insert_sorted_unique(L[v], label);
                }

                auto& D_label = D[label - first_label];

                // Intersect the dual only with closed neighborhoods of newly
                // added vertices. D always contains the enlarged clique. If its
                // size reaches exactly that lower bound, later intersections
                // cannot change it and are skipped.
                updated_dual.assign(D_label.begin(), D_label.end());
                for (int z : added) {
                    if (updated_dual.size() == merged.size()) break;
                    intersect_sorted_hybrid(
                        updated_dual, adj[z], intersection);
                    insert_sorted_unique(intersection, z);
                    updated_dual.swap(intersection);
                }

                // Remove label from exactly D\D'. A forward comparison avoids
                // one binary search for every old dual member.
                int new_position = 0;
                for (int w : D_label) {
                    while (new_position < (int)updated_dual.size() &&
                           updated_dual[new_position] < w) {
                        ++new_position;
                    }
                    if (new_position == (int)updated_dual.size() ||
                        updated_dual[new_position] != w) {
                        erase_sorted_if_found(A[w], label);
                        --sigma_D;
                    }
                }
                D_label.swap(updated_dual);
                C_label.swap(merged);

                // This owner has now covered every edge that kept an endpoint in
                // B_i'. Release its separated BCC representation immediately;
                // retaining it until the end of a large pass needlessly overlaps
                // processed BCC storage with the growing CPA cover and duals.
                vector<int>().swap(B_i.R);
                vector<int>().swap(B_i.Z);
                continue;
            }

            if (phase_open) {
                // Figure 4, Step 3(b)(iii), applied to the reduced B_i': no
                // current label accepts it, so create the newest output clique.
                // Appending the new label keeps every L[v] vector sorted.
                label = (int)C.size();
                C.push_back(B_prime);
                for (int v : B_prime) {
                    L[v].push_back(label);
                }

                D.emplace_back();
                auto& D_label = D.back();

                // Initialize the closed-neighborhood intersection from the tree
                // edge with the least adjacency-list work. The choice changes
                // only set-operation order. As above, D contains B_i', so reaching
                // |B_i'| permits an immediate stop.
                int dual_pivot = 0;
                int dual_pivot_cost = min(
                    (int)adj[tree_edges[0].first].size(),
                    (int)adj[tree_edges[0].second].size());
                for (int edge = 1; edge < (int)tree_edges.size(); ++edge) {
                    int cost = min(
                        (int)adj[tree_edges[edge].first].size(),
                        (int)adj[tree_edges[edge].second].size());
                    if (cost < dual_pivot_cost) {
                        dual_pivot_cost = cost;
                        dual_pivot = edge;
                    }
                }

                int pivot_u = tree_edges[dual_pivot].first;
                int pivot_v = tree_edges[dual_pivot].second;
                intersect_sorted_hybrid(adj[pivot_u], adj[pivot_v], D_label);
                insert_sorted_unique(D_label, pivot_u);
                insert_sorted_unique(D_label, pivot_v);

                for (int z : B_prime) {
                    if (D_label.size() == B_prime.size()) break;
                    if (z == pivot_u || z == pivot_v) continue;
                    intersect_sorted_hybrid(D_label, adj[z], intersection);
                    insert_sorted_unique(intersection, z);
                    D_label.swap(intersection);
                }

                sigma_D += (long long)D_label.size();
                statistics.peak_size_D =
                    max(statistics.peak_size_D, sigma_D);
                for (int w : D_label) A[w].push_back(label);

                // The label causing strict overflow belongs to this phase. The
                // closed phase may augment existing labels but creates no later
                // label until the next pass.
                if ((long double)sigma_D > phase_bound) {
                    phase_open = false;
                }

                vector<int>().swap(B_i.R);
                vector<int>().swap(B_i.Z);
                continue;
            }

            // The phase is closed and no current label accepts B_i'. Retain only
            // the reduced sides, and give the new vectors capacities based on
            // those reduced sizes. Moving B_i directly would also move the
            // possibly much larger capacities of the original B_i and would lose
            // the intended memory benefit of the B_i' restriction.
            next_pending.emplace_back();
            auto& deferred = next_pending.back();
            deferred.R.reserve(B_i.R.size());
            deferred.R.assign(B_i.R.begin(), B_i.R.end());
            deferred.Z.reserve(B_i.Z.size());
            deferred.Z.assign(B_i.Z.begin(), B_i.Z.end());
            vector<int>().swap(B_i.R);
            vector<int>().swap(B_i.Z);
        }

        if (first_pass) {
            // Every BCC pair has now been counted and processed. Later phases
            // need only the reduced cliques in next_pending, so release the
            // global ranked records, part maps, and current pair's vectors here.
            first_pass = false;
            vector<CrossCoverClique>().swap(pair_cover);
            cross_data = {};
        }

        // The old pending vector is destroyed after the swap. The next pass
        // therefore retains only reduced cliques, not already processed cliques
        // or their unused vector capacity from the preceding pass.
        pending.swap(next_pending);
        if (!pending.empty()) {
            // CPA's phase boundary: C and L persist, while the old
            // admissibility relation is discarded. New D indices begin at the
            // current global label count.
            for (auto& A_v : A) A_v.clear();
            D.clear();
            sigma_D = 0;
            first_label = (int)C.size();
        }
    }
}

}  // namespace cc_con
