#include "cc_con/algo_fpa.hpp"
#include "cc_con/first_fit.hpp"
#include "cc_con/sorted_vector_set.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace cc_con {

FpaOptions parse_fpa_options(const Config& config) {
    FpaOptions options;
    options.tau = config.number_or("construction.tau", 4.0);
    if (!isfinite(options.tau) || options.tau < 1.0) {
        throw runtime_error("construction.tau must be finite and at least 1");
    }

    string rule = config.string_or("construction.label_selection", "min-label");
    transform(rule.begin(), rule.end(), rule.begin(), [](unsigned char c) {
        return (char)tolower(c);
    });
    if (rule == "min-label") {
        options.label_rule = FpaLabelRule::MinLabel;
    } else if (rule == "max-label") {
        options.label_rule = FpaLabelRule::MaxLabel;
    } else if (rule == "min-clique") {
        options.label_rule = FpaLabelRule::MinClique;
    } else if (rule == "max-clique") {
        options.label_rule = FpaLabelRule::MaxClique;
    } else {
        throw runtime_error(
            "construction.label_selection must be min-label, max-label, "
            "min-clique, or max-clique");
    }
    return options;
}

const char* fpa_label_rule_name(FpaLabelRule rule) {
    switch (rule) {
        case FpaLabelRule::MinLabel: return "min-label";
        case FpaLabelRule::MaxLabel: return "max-label";
        case FpaLabelRule::MinClique: return "min-clique";
        case FpaLabelRule::MaxClique: return "max-clique";
    }
    return "unknown";
}

// Select a cover label q from A_u intersect A_v. The vectors are sorted by
// cover label. Min-label and max-label stop at the first common value in their
// respective scan directions. The size rules inspect every common label; strict
// comparison retains the smallest label when clique sizes tie.
//
// Direct merges are deliberate here. They were faster on the evaluation graphs
// than switching skewed inputs to binary searches, despite the latter matching
// the set-intersection cost used in the analysis.
static inline int select_label(const vector<int>& A_u,
                               const vector<int>& A_v,
                               const vector<vector<int>>& C,
                               FpaLabelRule rule) {
    int q = -1;

    switch (rule) {
        case FpaLabelRule::MinLabel: {
            int i = 0, j = 0;
            while (i < (int)A_u.size() && j < (int)A_v.size()) {
                if (A_u[i] < A_v[j]) ++i;
                else if (A_v[j] < A_u[i]) ++j;
                else {
                    q = A_u[i];
                    break;
                }
            }
            break;
        }

        case FpaLabelRule::MaxLabel: {
            int i = (int)A_u.size() - 1;
            int j = (int)A_v.size() - 1;
            while (i >= 0 && j >= 0) {
                if (A_u[i] > A_v[j]) --i;
                else if (A_v[j] > A_u[i]) --j;
                else {
                    q = A_u[i];
                    break;
                }
            }
            break;
        }

        case FpaLabelRule::MinClique:
        case FpaLabelRule::MaxClique: {
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
                        q < 0 ||
                        (rule == FpaLabelRule::MinClique
                             ? candidate_size < selected_size
                             : candidate_size > selected_size);

                    if (better) {
                        q = candidate;
                        selected_size = candidate_size;
                    }
                    ++i;
                    ++j;
                }
            }
            break;
        }
    }

    return q;
}

void construct_fpa(const vector<vector<int>>& adj,
                   long long m,
                   const FpaOptions& options,
                   vector<vector<int>>& C,
                   FpaStatistics& statistics) {
    int n = (int)adj.size();
    C.clear();
    statistics = {};
    if (m == 0) return;

    // FirstFit-PA notation and concrete representation:
    //   F[k] is first-fit class k, and h[v] is the class containing v.
    //   C[q] is cover clique q.
    //   L[v] contains every cover label q for which v belongs to C[q].
    //   A[v] contains current-phase cover labels that may accept v.
    //   D[j] is the admissible-vertex set of label first_label + j.
    // The code uses q for a cover label and ell for a first-fit label because the
    // paper uses one symbol in both roles. All set vectors are sorted and
    // duplicate-free. The formal uncovered-neighbor set M[v] is implicit:
    //
    //   u belongs to M[v]  iff
    //   u belongs to N(v) and L[u] and L[v] have no common label.
    //
    // C and L are updated together, so this equivalence remains exact without
    // copying adjacency lists or storing a status byte for every adjacency entry.
    vector<int> h;
    vector<vector<int>> F;
    first_fit_complement(adj, h, F);

    vector<vector<int>> L(n);
    vector<vector<int>> A(n);
    vector<vector<int>> D;
    long long sigma_D = 0;

    // FirstFit-PA Steps 3--5: initialize the cover with every non-singleton
    // first-fit class, update its membership lists, and build its admissibility
    // dual. Singleton F classes receive no cover label.
    C.reserve(F.size());
    for (auto& F_k : F) {
        if (F_k.size() < 2) continue;

        int q = (int)C.size();
        C.push_back(F_k);
        for (int v : F_k) L[v].push_back(q);

        // Initialize D[q] to the intersection of the closed neighborhoods of
        // all vertices in C[q]. Start with the first edge, then intersect each
        // remaining closed neighborhood by writing retained values to the front
        // of the same vector. Clique members are retained by the w == z case
        // even though adj[z] is open.
        D.emplace_back();
        auto& D_q = D.back();
        intersect_sorted_hybrid(adj[F_k[0]], adj[F_k[1]], D_q);
        insert_sorted_unique(D_q, F_k[0]);
        insert_sorted_unique(D_q, F_k[1]);

        for (int i = 2; i < (int)F_k.size(); ++i) {
            int z = F_k[i];
            int write = 0;
            for (int read = 0; read < (int)D_q.size(); ++read) {
                int w = D_q[read];
                if (w == z ||
                    binary_search(adj[w].begin(), adj[w].end(), z)) {
                    D_q[write++] = w;
                }
            }
            D_q.resize(write);
        }

        sigma_D += (long long)D_q.size();
        statistics.peak_size_D = max(statistics.peak_size_D, sigma_D);
        for (int w : D_q) A[w].push_back(q);
    }

    // Step 6 defines T_v as the first-fit labels ell < h[v] for which
    // F[ell] intersects N(v). The implementation stores only the current T_v.
    // Its restrictions F[ell] intersect N(v) occupy consecutive ranges in
    // bucket_members; bucket_begin and bucket_count delimit those ranges.
    int p = (int)F.size();
    vector<int> bucket_count(p, 0);
    vector<int> bucket_begin(p, 0);
    vector<unsigned char> in_T_v(p, 0);
    vector<int> T_v;
    vector<int> bucket_members;

    long double phase_bound = (long double)options.tau * (long double)m;

    // During the first phase, A and D describe the initial first-fit cliques and
    // any edge cliques subsequently created in that phase. At a phase boundary
    // they are discarded. Labels from completed phases remain in C and L but can
    // no longer be selected for augmentation.
    struct PendingPair {
        int v;
        int ell;
    };

    int first_label = 0;
    bool first_phase = true;
    vector<PendingPair> pending_pairs;
    vector<PendingPair> next_pending_pairs;

    while (first_phase || !pending_pairs.empty()) {
        bool phase_open = true;
        bool phase_counted = false;
        int next_vertex = 0;
        auto pending = pending_pairs.begin();

        // Step 7 visits pairs in increasing v and then increasing ell. The first
        // phase discovers T_v while scanning N(v). A later phase rebuilds T_v from
        // its pending pairs, which already have the same order. No restriction is
        // retained after its vertex has been processed.
        next_pending_pairs.clear();
        while (first_phase ? next_vertex < n
                           : pending != pending_pairs.end()) {
            int v;
            T_v.clear();

            if (first_phase) {
                v = next_vertex++;
                int k = h[v];
                if (k == 0) continue;

                // Bucket the lower-class neighbors by h[u]. For a fixed v, these
                // restrictions partition its lower-class neighbors, so one scan
                // counts all of them without intersecting each F[ell] with N(v).
                for (int u : adj[v]) {
                    int ell = h[u];
                    if (ell >= k) continue;
                    if (bucket_count[ell]++ == 0) T_v.push_back(ell);
                }
                if (T_v.empty()) continue;
                sort(T_v.begin(), T_v.end());
            } else {
                // Pending pairs are contiguous by v. Mark their labels and rebuild
                // all corresponding restrictions with one scan of N(v).
                v = pending->v;
                while (pending != pending_pairs.end() && pending->v == v) {
                    int ell = pending->ell;
                    T_v.push_back(ell);
                    in_T_v[ell] = 1;
                    ++pending;
                }
                for (int u : adj[v]) {
                    int ell = h[u];
                    if (in_T_v[ell] != 0) ++bucket_count[ell];
                }
            }

            if (!phase_counted) {
                ++statistics.num_phases;
                phase_counted = true;
            }

            // Prefix the counts into one flat, reusable workspace. A second scan
            // fills every restriction. Since adj[v] is sorted, each bucket is
            // sorted without a separate member sort.
            int next_offset = 0;
            for (int ell : T_v) {
                bucket_begin[ell] = next_offset;
                next_offset += bucket_count[ell];
                bucket_count[ell] = bucket_begin[ell];
            }
            bucket_members.resize(next_offset);
            for (int u : adj[v]) {
                int ell = h[u];
                bool belongs = first_phase ? ell < h[v] : in_T_v[ell] != 0;
                if (!belongs) continue;
                bucket_members[bucket_count[ell]++] = u;
            }

            for (int ell : T_v) {
                int members_begin = bucket_begin[ell];
                int members_end = bucket_count[ell];
                bool needs_recheck = false;

                for (int member = members_begin; member < members_end; ++member) {
                    int u = bucket_members[member];

                    // Step 7(a)(i): filtering the bucket through L gives
                    // F[ell] intersect M[v], and hence exactly the edges of E'.
                    // A common cover label means that {u,v} is already covered.
                    if (have_common_value(L[u], L[v])) continue;

                    // AugmentCover Step 1 chooses one current-phase cover label
                    // admissible to both endpoints, when such a label exists.
                    int q = select_label(A[u], A[v], C, options.label_rule);

                    if (q >= 0) {
                        // AugmentCover: C[q] <- C[q] union {u,v}. Membership of
                        // Membership of q in both A sets means that adding the
                        // two endpoints preserves the clique property.
                        auto& C_q = C[q];
                        bool u_added = insert_sorted_unique(C_q, u);
                        bool v_added = insert_sorted_unique(C_q, v);
                        if (u_added) insert_sorted_unique(L[u], q);
                        if (v_added) insert_sorted_unique(L[v], q);

                        auto& D_q = D[q - first_label];

                        // AugmentCover: D[q] <- D[q] intersect N[u] intersect
                        // N[v], using closed neighborhoods. Known members of
                        // C[q] survive without adjacency searches. The closed
                        // neighborhood of an endpoint already in C[q] was
                        // applied when it joined, so only newly added endpoints
                        // require another adjacency test. Whenever w leaves
                        // D[q], q must simultaneously leave A[w].
                        int write = 0;
                        int clique_position = 0;
                        for (int read = 0; read < (int)D_q.size(); ++read) {
                            int w = D_q[read];
                            while (clique_position < (int)C_q.size() &&
                                   C_q[clique_position] < w) {
                                ++clique_position;
                            }
                            bool in_clique =
                                clique_position < (int)C_q.size() &&
                                C_q[clique_position] == w;
                            bool keep = in_clique;
                            if (!keep) {
                                bool adjacent_to_u =
                                    !u_added || w == u ||
                                    binary_search(adj[w].begin(), adj[w].end(), u);
                                bool adjacent_to_v =
                                    !v_added || w == v ||
                                    binary_search(adj[w].begin(), adj[w].end(), v);
                                keep = adjacent_to_u && adjacent_to_v;
                            }

                            if (keep) {
                                D_q[write++] = w;
                            } else {
                                erase_sorted_if_found(A[w], q);
                                --sigma_D;
                            }
                        }
                        D_q.resize(write);
                        continue;
                    }

                    // A closed phase may still augment its existing labels, but
                    // this uncovered edge has no admissible label and is deferred
                    // to the next phase.
                    if (!phase_open) {
                        needs_recheck = true;
                        continue;
                    }

                    // AugmentCover creates the edge clique C[q] = {u,v}. The
                    // endpoints are sorted because all clique/set vectors use
                    // one canonical representation.
                    q = (int)C.size();
                    if (u < v) C.push_back({u, v});
                    else C.push_back({v, u});
                    L[u].push_back(q);
                    L[v].push_back(q);
                    D.emplace_back();

                    // For an edge, (N(u) intersect N(v)) union {u,v} equals
                    // N[u] intersect N[v], the vertices admissible to C[q].
                    auto& D_q = D.back();
                    intersect_sorted_hybrid(adj[u], adj[v], D_q);
                    insert_sorted_unique(D_q, u);
                    insert_sorted_unique(D_q, v);

                    sigma_D += (long long)D_q.size();
                    statistics.peak_size_D = max(statistics.peak_size_D, sigma_D);
                    for (int w : D_q) A[w].push_back(q);

                    // The clique causing strict overflow belongs to this phase.
                    // Only later edges without an admissible label are deferred.
                    if ((long double)sigma_D > phase_bound) {
                        phase_open = false;
                    }
                }

                // FirstFit-PA Step 7(a)(iii) removes (v,ell) if its restriction
                // contains no uncovered edge after AugmentCover. Recheck only a
                // pair that reached the closed-phase branch; this extra scan is
                // absent from the expected open, one-phase execution.
                if (needs_recheck) {
                    bool remains_uncovered = false;
                    for (int member = members_begin; member < members_end;
                         ++member) {
                        int u = bucket_members[member];
                        if (!have_common_value(L[u], L[v])) {
                            remains_uncovered = true;
                            break;
                        }
                    }
                    if (remains_uncovered) {
                        next_pending_pairs.push_back({v, ell});
                    }
                }

                bucket_count[ell] = 0;
                bucket_begin[ell] = 0;
                in_T_v[ell] = 0;
            }
        }

        first_phase = false;
        pending_pairs.swap(next_pending_pairs);
        if (!pending_pairs.empty()) {
            // FirstFit-PA step 7(b): begin a fresh phased-admissibility relation.
            // Clearing inner vectors retains their capacity for later phases;
            // D labels from this point onward start at the current |C|.
            for (auto& A_v : A) A_v.clear();
            D.clear();
            sigma_D = 0;
            first_label = (int)C.size();
        }
    }
}

}  // namespace cc_con
